#include "SpectralProcessor.h"

namespace dsp
{

SpectralProcessor::SpectralProcessor()
{
    fft = std::make_unique<juce::dsp::FFT>(fftOrder);
    window = std::make_unique<juce::dsp::WindowingFunction<float>>(fftSize, juce::dsp::WindowingFunction<float>::hann);

    inputFifo.resize(fftSize, 0.0f);
    outputAccumulator.resize(fftSize, 0.0f);
    fftBuffer.resize(fftSize * 2, 0.0f);

    int numBins = fftSize / 2 + 1;
    magnitudeSpectrum.resize((size_t)numBins);
    extractedEnvelope.resize((size_t)numBins);
    warpedEnvelope.resize((size_t)numBins);

    visSpectrum.resize((size_t)numBins);
    visEnvelope.resize((size_t)numBins);
}

SpectralProcessor::~SpectralProcessor() {}

void SpectralProcessor::prepare(const juce::dsp::ProcessSpec& spec)
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
}

void SpectralProcessor::detectFormants(const std::vector<float>& envelope, float& f1Bin, float& f2Bin)
{
    float hzPerBin = (float)currentSampleRate / fftSize;
    int minF1Bin = std::max(1, (int)(200.0f / hzPerBin));
    int maxF1Bin = (int)(1000.0f / hzPerBin);
    int minF2Bin = (int)(1000.0f / hzPerBin);
    int maxF2Bin = (int)(3000.0f / hzPerBin);

    auto findPeak = [&](int minB, int maxB) {
        float maxVal = -1.0f;
        int idx = minB;
        if (maxB >= (int)envelope.size()) maxB = (int)envelope.size() - 1;
        for (int i = minB; i <= maxB; ++i) {
            if (envelope[(size_t)i] > maxVal) {
                maxVal = envelope[(size_t)i];
                idx = i;
            }
        }
        return (float)idx;
    };

    f1Bin = findPeak(minF1Bin, maxF1Bin);
    f2Bin = findPeak(minF2Bin, maxF2Bin);
}

void SpectralProcessor::processBlock(std::vector<float>& data)
{
    window->multiplyWithWindowingTable(data.data(), fftSize);
    std::copy(data.begin(), data.end(), fftBuffer.begin());
    std::fill(fftBuffer.begin() + fftSize, fftBuffer.end(), 0.0f);

    fft->performRealOnlyForwardTransform(fftBuffer.data());

    int numBins = fftSize / 2 + 1;
    for (int i = 0; i < numBins; ++i)
    {
        float real = fftBuffer[(size_t)i * 2];
        float imag = fftBuffer[(size_t)i * 2 + 1];
        magnitudeSpectrum[(size_t)i] = std::sqrt(real * real + imag * imag);
    }

    envelopeExtractor.process(magnitudeSpectrum, extractedEnvelope);
    detectFormants(extractedEnvelope, currentF1, currentF2);

    std::vector<WarpingPoint> points;
    points.push_back({0.0f, 0.0f});

    float f1Dst = currentF1 * f1ShiftFactor * overallScaleFactor;
    float f2Dst = currentF2 * f2ShiftFactor * overallScaleFactor;

    if (f1Dst < 0.1f) f1Dst = 0.1f;
    if (f2Dst <= f1Dst) f2Dst = f1Dst + 1.0f;

    points.push_back({currentF1, f1Dst});
    points.push_back({currentF2, f2Dst});

    formantWarper.calculateWarpMap(numBins, points);
    formantWarper.process(extractedEnvelope, warpedEnvelope);

    if (visualizationLock.tryEnter())
    {
        visSpectrum = magnitudeSpectrum;
        visEnvelope = warpedEnvelope;
        visF1 = f1Dst;
        visF2 = f2Dst;
        visualizationLock.exit();
    }

    for (int i = 0; i < numBins; ++i)
    {
        float originalEnv = std::max(extractedEnvelope[(size_t)i], 1e-9f);
        float scale = warpedEnvelope[(size_t)i] / originalEnv;
        fftBuffer[(size_t)i * 2] *= scale;
        fftBuffer[(size_t)i * 2 + 1] *= scale;
    }

    fft->performRealOnlyInverseTransform(fftBuffer.data());
    window->multiplyWithWindowingTable(fftBuffer.data(), fftSize);

    for(int i=0; i<fftSize; ++i) data[(size_t)i] = fftBuffer[(size_t)i];
}

void SpectralProcessor::process(const juce::dsp::ProcessContextReplacing<float>& context)
{
    const auto& inputBlock = context.getInputBlock();
    auto& outputBlock = context.getOutputBlock();
    size_t numSamples = inputBlock.getNumSamples();
    size_t numChannels = inputBlock.getNumChannels();

    auto* src = inputBlock.getChannelPointer(0);
    auto* dst = outputBlock.getChannelPointer(0);

    for (size_t i = 0; i < numSamples; ++i)
    {
        std::rotate(inputFifo.begin(), inputFifo.begin() + 1, inputFifo.end());
        inputFifo.back() = src[i];

        float outSample = outputAccumulator[0];
        std::rotate(outputAccumulator.begin(), outputAccumulator.begin() + 1, outputAccumulator.end());
        outputAccumulator.back() = 0.0f;
        dst[i] = outSample;

        hopCounter++;
        if (hopCounter >= hopSize)
        {
            hopCounter = 0;
            std::vector<float> frame = inputFifo;
            processBlock(frame);
            for (int k = 0; k < fftSize; ++k)
                outputAccumulator[(size_t)k] += frame[(size_t)k];
        }
    }

    for (size_t ch = 1; ch < numChannels; ++ch)
    {
        auto* chDst = outputBlock.getChannelPointer(ch);
        std::copy(dst, dst + numSamples, chDst);
    }
}

void SpectralProcessor::getLatestVisualizationData(std::vector<float>& spectrum, std::vector<float>& envelope, float& f1, float& f2)
{
    const juce::ScopedLock lock(visualizationLock);
    spectrum = visSpectrum;
    envelope = visEnvelope;
    f1 = visF1;
    f2 = visF2;
}

} // namespace dsp
