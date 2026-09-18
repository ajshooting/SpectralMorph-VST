#include "SpectralProcessor.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace dsp
{

  SpectralProcessor::SpectralProcessor()
  {
    fft = std::make_unique<juce::dsp::FFT>(fftOrder);
    window = std::make_unique<juce::dsp::WindowingFunction<float>>(
        fftSize,
        juce::dsp::WindowingFunction<float>::hann,
        false);

    fftBuffer.resize(fftSize * 2, 0.0f);
    warpPoints.reserve(numFormants + 2);

    const int numBins = fftSize / 2 + 1;
    magnitudeSpectrum.resize((size_t)numBins);
    extractedEnvelope.resize((size_t)numBins);
    warpedEnvelope.resize((size_t)numBins);

    visSpectrum.resize((size_t)numBins);
    visEnvelope.resize((size_t)numBins);
    formantWarper.prepare(numBins);
  }

  SpectralProcessor::~SpectralProcessor() = default;

  void SpectralProcessor::prepare(const juce::dsp::ProcessSpec &spec)
  {
    currentSampleRate = spec.sampleRate;
    envelopeExtractor.prepare(fftSize);

    const auto numChannels = std::max<size_t>(1, spec.numChannels);
    channelStates.resize(numChannels);
    for (auto &state : channelStates)
      initialiseChannelState(state);

    reset();
  }

  void SpectralProcessor::initialiseChannelState(ChannelState &state) const
  {
    state.inputFifo.resize(fftSize, 0.0f);
    state.outputAccumulator.resize(fftSize, 0.0f);
    state.frame.resize(fftSize, 0.0f);
    state.hopCounter = 0;
    state.inputWritePos = 0;
    state.outputReadPos = 0;
  }

  void SpectralProcessor::reset()
  {
    for (auto &state : channelStates)
    {
      std::fill(state.inputFifo.begin(), state.inputFifo.end(), 0.0f);
      std::fill(state.outputAccumulator.begin(), state.outputAccumulator.end(), 0.0f);
      std::fill(state.frame.begin(), state.frame.end(), 0.0f);
      state.hopCounter = 0;
      state.inputWritePos = 0;
      state.outputReadPos = 0;
    }
  }

  void SpectralProcessor::setTargetFormantsHz(const std::array<float, numFormants> &targetHz)
  {
    targetFormantsHz = targetHz;

    for (size_t i = 0; i < targetFormantsHz.size(); ++i)
    {
      const float minHz = (i == 0) ? 200.0f : targetFormantsHz[i - 1] + 20.0f;
      if (!std::isfinite(targetFormantsHz[i]))
        targetFormantsHz[i] = minHz;

      targetFormantsHz[i] = std::max(minHz, targetFormantsHz[i]);
    }
  }

  size_t SpectralProcessor::detectFormants(const std::vector<float> &envelope,
                                           double sampleRate,
                                           std::array<float, numFormants> &formantBins) const
  {
    if (envelope.size() < 3
        || !std::isfinite(sampleRate)
        || sampleRate <= 0.0)
    {
      formantBins.fill(0.0f);
      return 0;
    }

    const float hzPerBin = (float)sampleRate / (float)fftSize;
    const int minBin = std::max(1, (int)(150.0f / hzPerBin));
    const int maxBin = std::min((int)envelope.size() - 2, (int)(9000.0f / hzPerBin));
    const int minDistanceBins = std::max(2, (int)(120.0f / hzPerBin));
    if (minBin > maxBin)
    {
      formantBins.fill((float)juce::jlimit(
          0, (int)envelope.size() - 1, minBin));
      return 0;
    }

    struct Peak
    {
      int bin = 0;
      float mag = 0.0f;
    };

    std::array<Peak, fftSize / 2 + 1> candidates{};
    size_t candidateCount = 0;

    for (int i = minBin; i <= maxBin; ++i)
    {
      const float v = envelope[(size_t)i];
      if (v > envelope[(size_t)i - 1] && v >= envelope[(size_t)i + 1])
        candidates[candidateCount++] = {i, v};
    }

    std::sort(candidates.begin(), candidates.begin() + (std::ptrdiff_t)candidateCount, [](const Peak &a, const Peak &b)
              { return a.mag > b.mag; });

    std::array<int, numFormants> selected{};
    size_t selectedCount = 0;

    for (size_t candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex)
    {
      const auto &peak = candidates[candidateIndex];
      bool tooClose = false;
      for (size_t selectedIndex = 0; selectedIndex < selectedCount; ++selectedIndex)
      {
        const int chosen = selected[selectedIndex];
        if (std::abs(chosen - peak.bin) < minDistanceBins)
        {
          tooClose = true;
          break;
        }
      }

      if (!tooClose)
        selected[selectedCount++] = peak.bin;

      if (selectedCount >= numFormants)
        break;
    }

    std::sort(selected.begin(), selected.begin() + (std::ptrdiff_t)selectedCount);

    int lastBin = std::max(minBin, 1);
    for (size_t i = 0; i < numFormants; ++i)
    {
      if (i < selectedCount)
      {
        lastBin = std::max(lastBin + (i == 0 ? 0 : minDistanceBins / 2), selected[i]);
      }
      else
      {
        lastBin = std::min(maxBin, lastBin + minDistanceBins);
      }

      formantBins[i] = (float)juce::jlimit(minBin, maxBin, lastBin);
    }

    return selectedCount;
  }

  bool SpectralProcessor::estimateFormantsFromBuffer(const juce::AudioBuffer<float> &sourceBuffer,
                                                      double sourceSampleRate,
                                                      std::array<float, numFormants> &estimatedHz,
                                                      size_t *detectedFormantCount,
                                                      const std::function<bool()> &shouldCancel) const
  {
    const auto wasCancelled = [&shouldCancel]
    {
      return shouldCancel && shouldCancel();
    };

    if (detectedFormantCount != nullptr)
      *detectedFormantCount = 0;

    estimatedHz = {
        500.0f, 1500.0f, 2500.0f, 3200.0f, 3800.0f,
        4400.0f, 5000.0f, 5600.0f, 6200.0f, 6800.0f,
        7400.0f, 8000.0f, 8600.0f, 9200.0f, 9800.0f};

    if (sourceBuffer.getNumSamples() <= 0
        || sourceBuffer.getNumChannels() <= 0
        || !std::isfinite(sourceSampleRate)
        || sourceSampleRate <= 0.0
        || wasCancelled())
      return false;

    juce::dsp::FFT analysisFft(fftOrder);
    juce::dsp::WindowingFunction<float> analysisWindow(
        fftSize,
        juce::dsp::WindowingFunction<float>::hann,
        false);
    EnvelopeExtractor analysisEnvelopeExtractor;
    analysisEnvelopeExtractor.prepare(fftSize);

    const int numBins = fftSize / 2 + 1;
    std::vector<float> frame((size_t)fftSize, 0.0f);
    std::vector<float> analysisFftBuffer((size_t)fftSize * 2, 0.0f);
    std::vector<float> analysisMagnitude((size_t)numBins, 0.0f);
    std::vector<float> analysisEnvelope((size_t)numBins, 0.0f);
    std::vector<double> accumulatedLogEnvelope((size_t)numBins, 0.0);

    const int totalSamples = sourceBuffer.getNumSamples();
    const float *readPtr = sourceBuffer.getReadPointer(0);

    struct RankedFrame
    {
      double meanSquare = 0.0;
      int start = 0;
    };

    constexpr size_t maxRankedCandidates = 64;
    constexpr size_t maxAnalysisFrames = 8;
    std::array<RankedFrame, maxRankedCandidates> strongestFrames{};
    size_t strongestFrameCount = 0;
    const int scanStep = std::max(hopSize, totalSamples / 256);
    const int lastStart = std::max(0, totalSamples - fftSize);

    for (int start = 0;; start = std::min(lastStart, start + scanStep))
    {
      if (wasCancelled())
        return false;

      const int copyCount = std::min(fftSize, totalSamples - start);
      double sumSquares = 0.0;
      for (int i = 0; i < copyCount; ++i)
      {
        const double sample = readPtr[start + i];
        sumSquares += sample * sample;
      }

      const double meanSquare = sumSquares / (double)copyCount;
      if (strongestFrameCount < strongestFrames.size())
      {
        strongestFrames[strongestFrameCount++] = {meanSquare, start};
        std::sort(strongestFrames.begin(),
                  strongestFrames.begin() + (std::ptrdiff_t)strongestFrameCount,
                  [](const RankedFrame &a, const RankedFrame &b)
                  {
                    return a.meanSquare > b.meanSquare;
                  });
      }
      else if (meanSquare > strongestFrames.back().meanSquare)
      {
        strongestFrames.back() = {meanSquare, start};
        std::sort(strongestFrames.begin(),
                  strongestFrames.end(),
                  [](const RankedFrame &a, const RankedFrame &b)
                  {
                    return a.meanSquare > b.meanSquare;
                  });
      }

      if (start == lastStart)
        break;
    }

    if (strongestFrameCount == 0
        || std::sqrt(strongestFrames.front().meanSquare) < 1.0e-5)
      return false;

    // Average several energetic windows in the log-envelope domain. This is
    // less likely than a single loud frame to lock onto a plosive or click.
    const double minimumMeanSquare = strongestFrames.front().meanSquare * 0.04;
    size_t aggregatedFrameCount = 0;
    std::array<int, maxAnalysisFrames> aggregatedFrameStarts{};
    for (size_t frameIndex = 0; frameIndex < strongestFrameCount; ++frameIndex)
    {
      if (wasCancelled())
        return false;

      const auto &ranked = strongestFrames[frameIndex];
      if (ranked.meanSquare < minimumMeanSquare)
        break;

      const bool overlapsSelectedFrame = std::any_of(
          aggregatedFrameStarts.begin(),
          aggregatedFrameStarts.begin() + (std::ptrdiff_t)aggregatedFrameCount,
          [&ranked](int selectedStart)
          {
            return std::abs(selectedStart - ranked.start) < fftSize;
          });
      if (overlapsSelectedFrame)
        continue;

      std::fill(frame.begin(), frame.end(), 0.0f);
      std::fill(analysisFftBuffer.begin(), analysisFftBuffer.end(), 0.0f);

      const int copyCount = std::min(fftSize, totalSamples - ranked.start);
      std::copy(readPtr + ranked.start,
                readPtr + ranked.start + copyCount,
                frame.begin());

      analysisWindow.multiplyWithWindowingTable(frame.data(), fftSize);
      std::copy(frame.begin(), frame.end(), analysisFftBuffer.begin());
      analysisFft.performRealOnlyForwardTransform(analysisFftBuffer.data());

      for (int i = 0; i < numBins; ++i)
      {
        const float real = analysisFftBuffer[(size_t)i * 2];
        const float imag = analysisFftBuffer[(size_t)i * 2 + 1];
        analysisMagnitude[(size_t)i] = std::sqrt(real * real + imag * imag);
      }

      analysisEnvelopeExtractor.process(analysisMagnitude, analysisEnvelope);
      for (int i = 0; i < numBins; ++i)
        accumulatedLogEnvelope[(size_t)i] +=
            std::log((double)std::max(analysisEnvelope[(size_t)i], 1.0e-9f));

      aggregatedFrameStarts[aggregatedFrameCount++] = ranked.start;
      if (aggregatedFrameCount == maxAnalysisFrames)
        break;
    }

    const size_t minimumFrameCount = totalSamples >= fftSize * 3
                                         ? 3u
                                         : (totalSamples >= fftSize + hopSize ? 2u : 1u);
    if (aggregatedFrameCount < minimumFrameCount)
      return false;

    for (int i = 0; i < numBins; ++i)
      analysisEnvelope[(size_t)i] =
          (float)std::exp(accumulatedLogEnvelope[(size_t)i]
                          / (double)aggregatedFrameCount);

    std::array<float, numFormants> bins{};
    const auto detectedCount = detectFormants(analysisEnvelope, sourceSampleRate, bins);
    if (detectedCount < 3)
      return false;

    const float hzPerBin = (float)sourceSampleRate / (float)fftSize;
    const int firstAnalysisBin = juce::jlimit(
        0, numBins - 1, (int)std::ceil(150.0f / hzPerBin));
    const int lastAnalysisBin = juce::jlimit(
        firstAnalysisBin, numBins - 1, (int)std::floor(9000.0f / hzPerBin));
    const auto envelopeRange = std::minmax_element(
        analysisEnvelope.begin() + firstAnalysisBin,
        analysisEnvelope.begin() + lastAnalysisBin + 1);
    if (*envelopeRange.second < *envelopeRange.first * 1.6f)
      return false;

    if (detectedFormantCount != nullptr)
      *detectedFormantCount = detectedCount;

    for (size_t i = 0; i < numFormants; ++i)
      estimatedHz[i] = bins[i] * hzPerBin;

    return true;
  }

  void SpectralProcessor::processFrame(std::vector<float> &data, bool updateVisualization)
  {
    // --- Analysis ---
    window->multiplyWithWindowingTable(data.data(), fftSize);

    std::copy(data.begin(), data.end(), fftBuffer.begin());
    std::fill(fftBuffer.begin() + fftSize, fftBuffer.end(), 0.0f);

    fft->performRealOnlyForwardTransform(fftBuffer.data());

    const int numBins = fftSize / 2 + 1;
    for (int i = 0; i < numBins; ++i)
    {
      const float real = fftBuffer[(size_t)i * 2];
      const float imag = fftBuffer[(size_t)i * 2 + 1];
      magnitudeSpectrum[(size_t)i] = std::sqrt(real * real + imag * imag);
    }

    // --- Envelope Extraction (Cepstral) ---
    envelopeExtractor.process(magnitudeSpectrum, extractedEnvelope);

    // --- Formant Detection & Warping ---
    const auto detectedFormantCount = detectFormants(
        extractedEnvelope, currentSampleRate, currentFormantBins);

    warpPoints.clear();
    warpPoints.push_back({0.0f, 0.0f});

    const float hzPerBin = (float)currentSampleRate / (float)fftSize;
    float lastDst = 0.0f;
    // Entries beyond the detected count are synthetic fallback positions.
    // Only actual peaks may anchor the warp; zero peaks leaves an identity map.
    for (size_t i = 0; i < detectedFormantCount; ++i)
    {
      const float src = currentFormantBins[i];
      const float targetBin = targetFormantsHz[i] / std::max(1.0f, hzPerBin);
      const float minimumDst = lastDst + 1.0f;
      const float remainingPoints = (float)(detectedFormantCount - i - 1);
      const float maximumDst = (float)(numBins - 2) - remainingPoints;
      const float dst = juce::jlimit(minimumDst, maximumDst, targetBin);
      warpPoints.push_back({src, dst});
      lastDst = dst;
    }

    warpPoints.push_back({(float)(numBins - 1), (float)(numBins - 1)});

    formantWarper.calculateWarpMap(numBins, warpPoints);
    formantWarper.process(extractedEnvelope, warpedEnvelope);

    // --- Visualization data (lock-free tryEnter) ---
    if (updateVisualization && visualizationLock.tryEnter())
    {
      // A non-normalised Hann window has a coherent gain of 0.5, so a
      // bin-centred full-scale sinusoid needs 4 / N to display near 0 dBFS.
      constexpr float displayNormalisation = 4.0f / (float)fftSize;
      for (int i = 0; i < numBins; ++i)
      {
        visSpectrum[(size_t)i] = magnitudeSpectrum[(size_t)i] * displayNormalisation;
        visEnvelope[(size_t)i] = warpedEnvelope[(size_t)i] * displayNormalisation;
      }
      visF1 = detectedFormantCount > 0 ? warpPoints[1].dstBin : 0.0f;
      visF2 = detectedFormantCount > 1 ? warpPoints[2].dstBin : 0.0f;
      visualizationLock.exit();
    }

    // --- Apply warped envelope (Source-Filter resynthesis) ---
    // Scale = warpedEnv / originalEnv, clamped to prevent extreme amplification.
    // Preserve peakless frames exactly, including signals below the gain
    // calculation's envelope floor. They still use normal STFT reconstruction.
    if (detectedFormantCount > 0)
    {
      const float maxGainLinear = std::pow(10.0f, maxEnvelopeGainDb / 20.0f);
      for (int i = 0; i < numBins; ++i)
      {
        const float originalEnv = std::max(extractedEnvelope[(size_t)i], 1e-7f);
        const float warpedVal = std::max(warpedEnvelope[(size_t)i], 1e-9f);
        const float scale = juce::jlimit(0.0f, maxGainLinear, warpedVal / originalEnv);

        fftBuffer[(size_t)i * 2] *= scale;
        fftBuffer[(size_t)i * 2 + 1] *= scale;
      }
    }

    // --- Synthesis (IFFT + window) ---
    fft->performRealOnlyInverseTransform(fftBuffer.data());

    // JUCE's IFFT is normalised. Compensate only for Hann^2 overlap-add.
    const float normFactor = 1.0f / overlapAddSum;
    for (int i = 0; i < fftSize; ++i)
      fftBuffer[(size_t)i] *= normFactor;

    window->multiplyWithWindowingTable(fftBuffer.data(), fftSize);

    for (int i = 0; i < fftSize; ++i)
      data[(size_t)i] = fftBuffer[(size_t)i];
  }

  void SpectralProcessor::process(const juce::dsp::ProcessContextReplacing<float> &context)
  {
    const auto &inputBlock = context.getInputBlock();
    auto &outputBlock = context.getOutputBlock();
    const size_t numSamples = inputBlock.getNumSamples();
    const size_t inputChannels = inputBlock.getNumChannels();
    const size_t outputChannels = outputBlock.getNumChannels();

    if (inputChannels == 0 || outputChannels == 0)
      return;

    jassert(channelStates.size() >= outputChannels);
    const size_t channelsToProcess = std::min(outputChannels, channelStates.size());

    for (size_t ch = 0; ch < channelsToProcess; ++ch)
    {
      auto &state = channelStates[ch];
      const auto sourceChannel = std::min(ch, inputChannels - 1);
      const auto *src = inputBlock.getChannelPointer(sourceChannel);
      auto *dst = outputBlock.getChannelPointer(ch);

      for (size_t i = 0; i < numSamples; ++i)
      {
        // Write new input sample into circular buffer
        state.inputFifo[(size_t)state.inputWritePos] = src[i];
        state.inputWritePos = (state.inputWritePos + 1) % fftSize;

        // Read output sample from circular accumulator
        dst[i] = state.outputAccumulator[(size_t)state.outputReadPos];
        state.outputAccumulator[(size_t)state.outputReadPos] = 0.0f;
        state.outputReadPos = (state.outputReadPos + 1) % fftSize;

        ++state.hopCounter;
        if (state.hopCounter >= hopSize)
        {
          state.hopCounter = 0;

          // Assemble frame from circular input buffer (oldest to newest)
          for (int k = 0; k < fftSize; ++k)
            state.frame[(size_t)k] = state.inputFifo[(size_t)((state.inputWritePos + k) % fftSize)];

          processFrame(state.frame, ch == 0);

          // Overlap-add into circular output accumulator
          for (int k = 0; k < fftSize; ++k)
          {
            const int pos = (state.outputReadPos + k) % fftSize;
            state.outputAccumulator[(size_t)pos] += state.frame[(size_t)k];
          }
        }
      }
    }

    for (size_t ch = channelsToProcess; ch < outputChannels; ++ch)
      juce::FloatVectorOperations::clear(outputBlock.getChannelPointer(ch), (int)numSamples);
  }

  void SpectralProcessor::getLatestVisualizationData(std::vector<float> &spectrum,
                                                     std::vector<float> &envelope,
                                                     float &f1,
                                                     float &f2)
  {
    const juce::ScopedLock lock(visualizationLock);
    spectrum = visSpectrum;
    envelope = visEnvelope;
    f1 = visF1;
    f2 = visF2;
  }

} // namespace dsp
