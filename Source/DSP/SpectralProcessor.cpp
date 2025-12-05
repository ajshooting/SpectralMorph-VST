#include "SpectralProcessor.h"

namespace dsp
{

SpectralProcessor::SpectralProcessor()
{
    fft = std::make_unique<juce::dsp::FFT>(fftOrder);
    window = std::make_unique<juce::dsp::WindowingFunction<float>>(fftSize, juce::dsp::WindowingFunction<float>::hann);

    inputFifo.resize(fftSize, 0.0f);
    outputFifo.resize(fftSize, 0.0f);
    fftBuffer.resize(fftSize * 2, 0.0f);
    outputAccumulator.resize(fftSize, 0.0f);

    int numBins = fftSize / 2 + 1;
    magnitudeSpectrum.resize((size_t)numBins);
    extractedEnvelope.resize((size_t)numBins);
    warpedEnvelope.resize((size_t)numBins);

    visSpectrum.resize((size_t)numBins);
    visEnvelope.resize((size_t)numBins);
}

SpectralProcessor::~SpectralProcessor()
{
}

void SpectralProcessor::prepare(const juce::dsp::ProcessSpec& spec)
{
    juce::ignoreUnused(spec);
    currentSampleRate = spec.sampleRate;
    envelopeExtractor.prepare(fftSize);

    std::fill(inputFifo.begin(), inputFifo.end(), 0.0f);
    std::fill(outputFifo.begin(), outputFifo.end(), 0.0f);
    std::fill(outputAccumulator.begin(), outputAccumulator.end(), 0.0f);
    hopCounter = 0;
}

void SpectralProcessor::reset()
{
    std::fill(inputFifo.begin(), inputFifo.end(), 0.0f);
    std::fill(outputFifo.begin(), outputFifo.end(), 0.0f);
    std::fill(outputAccumulator.begin(), outputAccumulator.end(), 0.0f);
    hopCounter = 0;
}

void SpectralProcessor::detectFormants(const std::vector<float>& envelope, float& f1Bin, float& f2Bin)
{
    // Simple Peak Detection
    // Assumptions: F1 is in 200-1000Hz, F2 is in 1000-3000Hz
    // Convert Hz to Bins
    float hzPerBin = (float)currentSampleRate / fftSize;

    int minF1Bin = (int)(200.0f / hzPerBin);
    int maxF1Bin = (int)(1000.0f / hzPerBin);
    int minF2Bin = (int)(1000.0f / hzPerBin);
    int maxF2Bin = (int)(3000.0f / hzPerBin);

    // Safety checks
    int numBins = (int)envelope.size();
    if (minF1Bin >= numBins) minF1Bin = 1;
    if (maxF1Bin >= numBins) maxF1Bin = numBins - 1;
    if (minF2Bin >= numBins) minF2Bin = 1;
    if (maxF2Bin >= numBins) maxF2Bin = numBins - 1;

    // Find F1
    float maxVal = -1.0f;
    int f1Idx = minF1Bin;
    for (int i = minF1Bin; i <= maxF1Bin; ++i)
    {
        if (envelope[i] > maxVal)
        {
            maxVal = envelope[i];
            f1Idx = i;
        }
    }
    f1Bin = (float)f1Idx;

    // Find F2
    maxVal = -1.0f;
    int f2Idx = minF2Bin;
    for (int i = minF2Bin; i <= maxF2Bin; ++i)
    {
        if (envelope[i] > maxVal)
        {
            maxVal = envelope[i];
            f2Idx = i;
        }
    }
    f2Bin = (float)f2Idx;
}

void SpectralProcessor::processBlock(std::vector<float>& data)
{
    // data contains fftSize samples (time domain)
    // 1. Windowing
    window->multiplyWithWindowingTable(data.data(), fftSize);

    // 2. FFT
    std::copy(data.begin(), data.end(), fftBuffer.begin());
    std::fill(fftBuffer.begin() + fftSize, fftBuffer.end(), 0.0f);

    fft->performRealOnlyForwardTransform(fftBuffer.data());

    // 3. Compute Magnitude & Envelope
    int numBins = fftSize / 2 + 1;
    for (int i = 0; i < numBins; ++i)
    {
        float real = fftBuffer[(size_t)i * 2];
        float imag = fftBuffer[(size_t)i * 2 + 1];
        magnitudeSpectrum[(size_t)i] = std::sqrt(real * real + imag * imag);
    }

    envelopeExtractor.process(magnitudeSpectrum, extractedEnvelope);

    // Detect Formants
    detectFormants(extractedEnvelope, currentF1, currentF2);

    // 4. Warp Envelope
    // Prepare Control Points based on Shifts
    // We assume F1 shifts by f1ShiftFactor (multiplier)
    // and F2 shifts by f2ShiftFactor
    // We anchor 0Hz and Nyquist.
    // We also anchor points between F1 and F2 to avoid crossing if needed,
    // but for now let's just use F1 and F2 as control points.

    std::vector<WarpingPoint> points;
    points.push_back({0.0f, 0.0f});

    // F1 -> F1 * shift
    float f1Src = currentF1;
    float f1Dst = currentF1 * f1ShiftFactor;

    // F2 -> F2 * shift
    float f2Src = currentF2;
    float f2Dst = currentF2 * f2ShiftFactor;

    // Ensure monotonicity in Dest
    if (f1Dst < 0.1f) f1Dst = 0.1f;
    if (f2Dst <= f1Dst) f2Dst = f1Dst + 1.0f;

    // Add points
    // Note: WarpingPoint is {Src, Dst}
    // "Shifting F1 from 500 to 700" means Envelope at 700 comes from 500.
    // So Dst=700, Src=500.
    points.push_back({f1Src, f1Dst});
    points.push_back({f2Src, f2Dst});

    // Anchor Nyquist
    float nyquist = (float)(numBins - 1);
    points.push_back({nyquist, nyquist});

    formantWarper.calculateWarpMap(numBins, points);
    formantWarper.process(extractedEnvelope, warpedEnvelope);

    {
        const juce::ScopedLock lock(visualizationLock);
        visSpectrum = magnitudeSpectrum;
        visEnvelope = warpedEnvelope;
        visF1 = f1Dst; // Visualize where they ended up
        visF2 = f2Dst;
    }

    // 5. Apply processing
    for (int i = 0; i < numBins; ++i)
    {
        float originalEnv = std::max(extractedEnvelope[(size_t)i], 1e-9f);
        float scale = warpedEnvelope[(size_t)i] / originalEnv;

        fftBuffer[(size_t)i * 2] *= scale;
        fftBuffer[(size_t)i * 2 + 1] *= scale;
    }

    // 6. IFFT
    fft->performRealOnlyInverseTransform(fftBuffer.data());

    // 7. Windowing
    window->multiplyWithWindowingTable(fftBuffer.data(), fftSize); // fftBuffer is now time domain

    // Copy back
    for(int i=0; i<fftSize; ++i) data[(size_t)i] = fftBuffer[(size_t)i];
}

void SpectralProcessor::process(const juce::dsp::ProcessContextReplacing<float>& context)
{
    const auto& inputBlock = context.getInputBlock();
    auto& outputBlock = context.getOutputBlock();

    size_t numSamples = inputBlock.getNumSamples();
    size_t numChannels = inputBlock.getNumChannels();

    // Mono processing on Channel 0
    auto* src = inputBlock.getChannelPointer(0);
    auto* dst = outputBlock.getChannelPointer(0);

    for (size_t i = 0; i < numSamples; ++i)
    {
        // Add to FIFO
        std::rotate(inputFifo.begin(), inputFifo.begin() + 1, inputFifo.end());
        inputFifo.back() = src[i];

        // Add to Accumulator (Overlap-Add)
        float outSample = outputAccumulator[0];

        // Shift Accumulator
        std::rotate(outputAccumulator.begin(), outputAccumulator.begin() + 1, outputAccumulator.end());
        outputAccumulator.back() = 0.0f;

        dst[i] = outSample;

        hopCounter++;
        if (hopCounter >= hopSize)
        {
            hopCounter = 0;

            // Process Frame
            // Copy current FIFO to temp buffer
            std::vector<float> frame = inputFifo;

            processBlock(frame);

            // Overlap Add into Accumulator
            for (int k = 0; k < fftSize; ++k)
            {
                outputAccumulator[(size_t)k] += frame[(size_t)k];
            }
        }
    }

    // Copy to other channels
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
