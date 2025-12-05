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

    // 4. Warp Envelope
    formantWarper.calculateWarpMap(numBins, shiftFactor);
    formantWarper.process(extractedEnvelope, warpedEnvelope);

    {
        const juce::ScopedLock lock(visualizationLock);
        visSpectrum = magnitudeSpectrum;
        visEnvelope = warpedEnvelope;
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

void SpectralProcessor::getLatestVisualizationData(std::vector<float>& spectrum, std::vector<float>& envelope)
{
    const juce::ScopedLock lock(visualizationLock);
    spectrum = visSpectrum;
    envelope = visEnvelope;
}

} // namespace dsp
