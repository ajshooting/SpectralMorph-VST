#include "SpectralProcessor.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace dsp
{

  SpectralProcessor::SpectralProcessor()
  {
    fft = std::make_unique<juce::dsp::FFT>(fftOrder);
    window = std::make_unique<juce::dsp::WindowingFunction<float>>(fftSize, juce::dsp::WindowingFunction<float>::hann);

    fftBuffer.resize(fftSize * 2, 0.0f);
    warpPoints.reserve(numFormants + 2);

    const int numBins = fftSize / 2 + 1;
    magnitudeSpectrum.resize((size_t)numBins);
    extractedEnvelope.resize((size_t)numBins);
    warpedEnvelope.resize((size_t)numBins);

    visSpectrum.resize((size_t)numBins);
    visEnvelope.resize((size_t)numBins);
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
      targetFormantsHz[i] = std::max(minHz, targetFormantsHz[i]);
    }
  }

  void SpectralProcessor::detectFormants(const std::vector<float> &envelope,
                                         double sampleRate,
                                         std::array<float, numFormants> &formantBins) const
  {
    const float hzPerBin = (float)sampleRate / (float)fftSize;
    const int minBin = std::max(1, (int)(150.0f / hzPerBin));
    const int maxBin = std::min((int)envelope.size() - 2, (int)(9000.0f / hzPerBin));
    const int minDistanceBins = std::max(2, (int)(120.0f / hzPerBin));

    struct Peak
    {
      int bin = 0;
      float mag = 0.0f;
    };

    std::vector<Peak> candidates;
    candidates.reserve((size_t)std::max(0, maxBin - minBin + 1));

    for (int i = minBin; i <= maxBin; ++i)
    {
      const float v = envelope[(size_t)i];
      if (v > envelope[(size_t)i - 1] && v >= envelope[(size_t)i + 1])
        candidates.push_back({i, v});
    }

    std::sort(candidates.begin(), candidates.end(), [](const Peak &a, const Peak &b)
              { return a.mag > b.mag; });

    std::vector<int> selected;
    selected.reserve(numFormants);

    for (const auto &peak : candidates)
    {
      bool tooClose = false;
      for (int chosen : selected)
      {
        if (std::abs(chosen - peak.bin) < minDistanceBins)
        {
          tooClose = true;
          break;
        }
      }

      if (!tooClose)
        selected.push_back(peak.bin);

      if (selected.size() >= numFormants)
        break;
    }

    std::sort(selected.begin(), selected.end());

    int lastBin = std::max(minBin, 1);
    for (size_t i = 0; i < numFormants; ++i)
    {
      if (i < selected.size())
      {
        lastBin = std::max(lastBin + (i == 0 ? 0 : minDistanceBins / 2), selected[i]);
      }
      else
      {
        lastBin = std::min(maxBin, lastBin + minDistanceBins);
      }

      formantBins[i] = (float)juce::jlimit(minBin, maxBin, lastBin);
    }
  }

  std::array<float, SpectralProcessor::numFormants> SpectralProcessor::estimateFormantsFromBuffer(const juce::AudioBuffer<float> &sourceBuffer,
                                                                                                  double sourceSampleRate)
  {
    std::array<float, numFormants> estimatedHz{
        500.0f, 1500.0f, 2500.0f, 3200.0f, 3800.0f,
        4400.0f, 5000.0f, 5600.0f, 6200.0f, 6800.0f,
        7400.0f, 8000.0f, 8600.0f, 9200.0f, 9800.0f};

    if (sourceBuffer.getNumSamples() <= 0 || sourceBuffer.getNumChannels() <= 0)
      return estimatedHz;

    juce::dsp::FFT analysisFft(fftOrder);
    juce::dsp::WindowingFunction<float> analysisWindow(fftSize, juce::dsp::WindowingFunction<float>::hann);
    EnvelopeExtractor analysisEnvelopeExtractor;
    analysisEnvelopeExtractor.prepare(fftSize);

    const int numBins = fftSize / 2 + 1;
    std::vector<float> frame((size_t)fftSize, 0.0f);
    std::vector<float> analysisFftBuffer((size_t)fftSize * 2, 0.0f);
    std::vector<float> analysisMagnitude((size_t)numBins, 0.0f);
    std::vector<float> analysisEnvelope((size_t)numBins, 0.0f);

    const int totalSamples = sourceBuffer.getNumSamples();
    const int start = std::max(0, (totalSamples / 2) - (fftSize / 2));
    const int copyCount = std::min(fftSize, totalSamples - start);

    const float *readPtr = sourceBuffer.getReadPointer(0);
    std::copy(readPtr + start, readPtr + start + copyCount, frame.begin());

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

    std::array<float, numFormants> bins{};
    detectFormants(analysisEnvelope, sourceSampleRate, bins);

    const float hzPerBin = (float)sourceSampleRate / (float)fftSize;
    for (size_t i = 0; i < numFormants; ++i)
      estimatedHz[i] = bins[i] * hzPerBin;

    return estimatedHz;
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
    detectFormants(extractedEnvelope, currentSampleRate, currentFormantBins);

    warpPoints.clear();
    warpPoints.push_back({0.0f, 0.0f});

    const float hzPerBin = (float)currentSampleRate / (float)fftSize;
    float lastDst = 0.0f;
    for (size_t i = 0; i < numFormants; ++i)
    {
      const float src = currentFormantBins[i];
      const float targetBin = targetFormantsHz[i] / std::max(1.0f, hzPerBin);
      const float dst = juce::jlimit(lastDst + 1.0f, (float)(numBins - 2), targetBin);
      warpPoints.push_back({src, dst});
      lastDst = dst;
    }

    warpPoints.push_back({(float)(numBins - 1), (float)(numBins - 1)});

    formantWarper.calculateWarpMap(numBins, warpPoints);
    formantWarper.process(extractedEnvelope, warpedEnvelope);

    // --- Visualization data (lock-free tryEnter) ---
    if (updateVisualization && visualizationLock.tryEnter())
    {
      visSpectrum = magnitudeSpectrum;
      visEnvelope = warpedEnvelope;
      visF1 = warpPoints[1].dstBin;
      visF2 = warpPoints[2].dstBin;
      visualizationLock.exit();
    }

    // --- Apply warped envelope (Source-Filter resynthesis) ---
    // Scale = warpedEnv / originalEnv, clamped to prevent extreme amplification.
    const float maxGainLinear = std::pow(10.0f, maxEnvelopeGainDb / 20.0f);
    for (int i = 0; i < numBins; ++i)
    {
      const float originalEnv = std::max(extractedEnvelope[(size_t)i], 1e-7f);
      const float warpedVal = std::max(warpedEnvelope[(size_t)i], 1e-9f);
      const float scale = juce::jlimit(0.0f, maxGainLinear, warpedVal / originalEnv);

      fftBuffer[(size_t)i * 2] *= scale;
      fftBuffer[(size_t)i * 2 + 1] *= scale;
    }

    // --- Synthesis (IFFT + window) ---
    fft->performRealOnlyInverseTransform(fftBuffer.data());

    // Normalize: JUCE IFFT does not divide by N.
    // Combined with overlap-add of Hann^2 (= 1.5), total normalization = 1/(N * 1.5)
    const float normFactor = 1.0f / ((float)fftSize * overlapAddSum);
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

    if (channelStates.size() < outputChannels)
    {
      const auto oldSize = channelStates.size();
      channelStates.resize(outputChannels);
      for (size_t ch = oldSize; ch < channelStates.size(); ++ch)
        initialiseChannelState(channelStates[ch]);
    }

    for (size_t ch = 0; ch < outputChannels; ++ch)
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
