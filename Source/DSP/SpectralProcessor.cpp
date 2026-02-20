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

    inputFifo.resize(fftSize, 0.0f);
    outputAccumulator.resize(fftSize, 0.0f);
    fftBuffer.resize(fftSize * 2, 0.0f);

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
    reset();
  }

  void SpectralProcessor::reset()
  {
    std::fill(inputFifo.begin(), inputFifo.end(), 0.0f);
    std::fill(outputAccumulator.begin(), outputAccumulator.end(), 0.0f);
    hopCounter = 0;
    inputWritePos = 0;
    outputReadPos = 0;
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
    std::array<float, numFormants> estimatedHz = targetFormantsHz;

    if (sourceBuffer.getNumSamples() <= 0 || sourceBuffer.getNumChannels() <= 0)
      return estimatedHz;

    std::vector<float> frame((size_t)fftSize, 0.0f);
    const int totalSamples = sourceBuffer.getNumSamples();
    const int start = std::max(0, (totalSamples / 2) - (fftSize / 2));
    const int copyCount = std::min(fftSize, totalSamples - start);

    const float *readPtr = sourceBuffer.getReadPointer(0);
    std::copy(readPtr + start, readPtr + start + copyCount, frame.begin());

    window->multiplyWithWindowingTable(frame.data(), fftSize);

    std::fill(fftBuffer.begin(), fftBuffer.end(), 0.0f);
    std::copy(frame.begin(), frame.end(), fftBuffer.begin());

    fft->performRealOnlyForwardTransform(fftBuffer.data());

    const int numBins = fftSize / 2 + 1;
    for (int i = 0; i < numBins; ++i)
    {
      const float real = fftBuffer[(size_t)i * 2];
      const float imag = fftBuffer[(size_t)i * 2 + 1];
      magnitudeSpectrum[(size_t)i] = std::sqrt(real * real + imag * imag);
    }

    envelopeExtractor.process(magnitudeSpectrum, extractedEnvelope);

    std::array<float, numFormants> bins{};
    detectFormants(extractedEnvelope, sourceSampleRate, bins);

    const float hzPerBin = (float)sourceSampleRate / (float)fftSize;
    for (size_t i = 0; i < numFormants; ++i)
      estimatedHz[i] = bins[i] * hzPerBin;

    return estimatedHz;
  }

  void SpectralProcessor::processBlock(std::vector<float> &data)
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

    std::vector<WarpingPoint> points;
    points.reserve(numFormants + 2);
    points.push_back({0.0f, 0.0f});

    const float hzPerBin = (float)currentSampleRate / (float)fftSize;
    float lastDst = 0.0f;
    for (size_t i = 0; i < numFormants; ++i)
    {
      const float src = currentFormantBins[i];
      const float targetBin = targetFormantsHz[i] / std::max(1.0f, hzPerBin);
      const float dst = juce::jlimit(lastDst + 1.0f, (float)(numBins - 2), targetBin);
      points.push_back({src, dst});
      lastDst = dst;
    }

    points.push_back({(float)(numBins - 1), (float)(numBins - 1)});

    formantWarper.calculateWarpMap(numBins, points);
    formantWarper.process(extractedEnvelope, warpedEnvelope);

    // --- Visualization data (lock-free tryEnter) ---
    if (visualizationLock.tryEnter())
    {
      visSpectrum = magnitudeSpectrum;
      visEnvelope = warpedEnvelope;
      visF1 = points[1].dstBin;
      visF2 = points[2].dstBin;
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
    const size_t numChannels = inputBlock.getNumChannels();

    auto *src = inputBlock.getChannelPointer(0);
    auto *dst = outputBlock.getChannelPointer(0);

    for (size_t i = 0; i < numSamples; ++i)
    {
      // Write new input sample into circular buffer
      inputFifo[(size_t)inputWritePos] = src[i];
      inputWritePos = (inputWritePos + 1) % fftSize;

      // Read output sample from circular accumulator
      dst[i] = outputAccumulator[(size_t)outputReadPos];
      outputAccumulator[(size_t)outputReadPos] = 0.0f;
      outputReadPos = (outputReadPos + 1) % fftSize;

      ++hopCounter;
      if (hopCounter >= hopSize)
      {
        hopCounter = 0;

        // Assemble frame from circular input buffer (oldest to newest)
        std::vector<float> frame((size_t)fftSize);
        for (int k = 0; k < fftSize; ++k)
          frame[(size_t)k] = inputFifo[(size_t)((inputWritePos + k) % fftSize)];

        processBlock(frame);

        // Overlap-add into circular output accumulator
        for (int k = 0; k < fftSize; ++k)
        {
          const int pos = (outputReadPos + k) % fftSize;
          outputAccumulator[(size_t)pos] += frame[(size_t)k];
        }
      }
    }

    // Copy channel 0 result to all other channels
    for (size_t ch = 1; ch < numChannels; ++ch)
    {
      auto *chDst = outputBlock.getChannelPointer(ch);
      std::copy(dst, dst + numSamples, chDst);
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
