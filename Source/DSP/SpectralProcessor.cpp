#include "SpectralProcessor.h"

namespace dsp
{

SpectralProcessor::SpectralProcessor()
{
    // Initialize FFT with specified order (size = 2^order)
    fft = std::make_unique<juce::dsp::FFT>(fftOrder);

    // Hann window for overlap-add
    window = std::make_unique<juce::dsp::WindowingFunction<float>>(fftSize, juce::dsp::WindowingFunction<float>::hann);

    // Resize buffers
    inputFifo.resize(fftSize, 0.0f);
    outputAccumulator.resize(fftSize, 0.0f);
    fftBuffer.resize(fftSize * 2, 0.0f); // 2x for complex operations

    // Resize spectral containers
    int numBins = fftSize / 2 + 1;
    magnitudeSpectrum.resize((size_t)numBins);
    extractedEnvelope.resize((size_t)numBins);
    warpedEnvelope.resize((size_t)numBins);

    // Resize visualization buffers
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
    // Detect F1 and F2 using simple peak detection within expected frequency ranges.
    // F1 Range: 200 - 1000 Hz
    // F2 Range: 1000 - 3000 Hz

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
    // 1. Windowing
    window->multiplyWithWindowingTable(data.data(), fftSize);

    // 2. Prepare for FFT
    // Copy real input to complex buffer (imag parts are 0)
    std::copy(data.begin(), data.end(), fftBuffer.begin());
    std::fill(fftBuffer.begin() + fftSize, fftBuffer.end(), 0.0f);

    // 3. Forward FFT
    // performRealOnlyForwardTransform transforms the first half of the array in place.
    // fftBuffer now contains Frequency Domain data (interleaved real/imag).
    fft->performRealOnlyForwardTransform(fftBuffer.data());

    // 4. Compute Magnitude Spectrum
    int numBins = fftSize / 2 + 1;
    for (int i = 0; i < numBins; ++i)
    {
        float real = fftBuffer[(size_t)i * 2];
        float imag = fftBuffer[(size_t)i * 2 + 1];
        magnitudeSpectrum[(size_t)i] = std::sqrt(real * real + imag * imag);
    }

    // 5. Envelope Extraction (Cepstral Analysis)
    envelopeExtractor.process(magnitudeSpectrum, extractedEnvelope);

    // 6. Formant Detection
    detectFormants(extractedEnvelope, currentF1, currentF2);

    // 7. Calculate Warping
    // Map detected formants to target formants based on parameters.
    std::vector<WarpingPoint> points;
    points.push_back({0.0f, 0.0f}); // Anchor DC

    float f1Dst = currentF1 * f1ShiftFactor * overallScaleFactor;
    float f2Dst = currentF2 * f2ShiftFactor * overallScaleFactor;

    // Constraints to prevent crossing or invalid values
    if (f1Dst < 0.1f) f1Dst = 0.1f;
    if (f2Dst <= f1Dst) f2Dst = f1Dst + 1.0f;

    points.push_back({currentF1, f1Dst});
    points.push_back({currentF2, f2Dst});

    formantWarper.calculateWarpMap(numBins, points);
    formantWarper.process(extractedEnvelope, warpedEnvelope);

    // 8. Update Visualization (Thread-safe attempt)
    if (visualizationLock.tryEnter())
    {
        visSpectrum = magnitudeSpectrum;
        visEnvelope = warpedEnvelope;
        visF1 = f1Dst;
        visF2 = f2Dst;
        visualizationLock.exit();
    }

    // 9. Apply Spectral Morphing
    // We modify the original spectrum by applying the difference between the warped envelope and the original envelope.
    // NewMagnitude = OriginalMagnitude * (WarpedEnvelope / OriginalEnvelope)
    // This preserves the fine structure (harmonics/pitch) but imposes the new formant structure.
    for (int i = 0; i < numBins; ++i)
    {
        float originalEnv = std::max(extractedEnvelope[(size_t)i], 1e-9f);
        float scale = warpedEnvelope[(size_t)i] / originalEnv;

        // Scale both Real and Imaginary parts to preserve Phase
        fftBuffer[(size_t)i * 2] *= scale;
        fftBuffer[(size_t)i * 2 + 1] *= scale;
    }

    // 10. Inverse FFT
    fft->performRealOnlyInverseTransform(fftBuffer.data());

    // 11. Windowing (Synthesis)
    // We window again for OLA (Overlap-Add) consistency
    window->multiplyWithWindowingTable(fftBuffer.data(), fftSize);

    // Copy back to data vector
    for(int i=0; i<fftSize; ++i) data[(size_t)i] = fftBuffer[(size_t)i];
}

void SpectralProcessor::process(const juce::dsp::ProcessContextReplacing<float>& context)
{
    const auto& inputBlock = context.getInputBlock();
    auto& outputBlock = context.getOutputBlock();
    size_t numSamples = inputBlock.getNumSamples();
    size_t numChannels = inputBlock.getNumChannels();

    // Mono processing on Channel 0 (Simple demo logic, copies result to other channels)
    auto* src = inputBlock.getChannelPointer(0);
    auto* dst = outputBlock.getChannelPointer(0);

    for (size_t i = 0; i < numSamples; ++i)
    {
        // Add new sample to FIFO
        std::rotate(inputFifo.begin(), inputFifo.begin() + 1, inputFifo.end());
        inputFifo.back() = src[i];

        // Output accumulated sample
        float outSample = outputAccumulator[0];

        // Shift Accumulator
        std::rotate(outputAccumulator.begin(), outputAccumulator.begin() + 1, outputAccumulator.end());
        outputAccumulator.back() = 0.0f;

        dst[i] = outSample;

        // Check if hop size reached
        hopCounter++;
        if (hopCounter >= hopSize)
        {
            hopCounter = 0;

            // Process current frame
            std::vector<float> frame = inputFifo;
            processBlock(frame);

            // Overlap-Add result into Accumulator
            for (int k = 0; k < fftSize; ++k)
                outputAccumulator[(size_t)k] += frame[(size_t)k];
        }
    }

    // Copy to other channels (Stereo support)
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
