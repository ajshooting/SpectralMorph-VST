#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace dsp
{

/**
 * Extracts the Spectral Envelope using Cepstral Analysis.
 * Method: Real Cepstrum -> Low-time liftering -> FFT -> Envelope.
 */
class EnvelopeExtractor
{
public:
    EnvelopeExtractor() {}

    void prepare(int newFftSize)
    {
        this->fftSize = newFftSize;
        forwardFFT = std::make_unique<juce::dsp::FFT>((int)std::log2(fftSize));

        timeDomainBuffer.resize((size_t)fftSize * 2);
        frequencyDomainBuffer.resize((size_t)fftSize * 2);
    }

    /**
     * Extracts the spectral envelope from a magnitude spectrum.
     */
    void process(const std::vector<float>& magnitudeSpectrum, std::vector<float>& envelope, int cutoffBin = 20)
    {
        int n = fftSize;
        int halfN = n / 2 + 1;

        std::fill(frequencyDomainBuffer.begin(), frequencyDomainBuffer.end(), 0.0f);

        for (int i = 0; i < halfN; ++i)
        {
            float mag = std::max(magnitudeSpectrum[(size_t)i], 1e-9f);
            float logMag = std::log(mag);
            frequencyDomainBuffer[(size_t)i * 2] = logMag;
            frequencyDomainBuffer[(size_t)i * 2 + 1] = 0.0f;
        }

        // IFFT to get Cepstrum (Real output in timeDomainBuffer? No, inverse of real-symmetric is real)
        // juce::dsp::FFT::performRealOnlyInverseTransform expects frequency domain data of a real signal.
        forwardFFT->performRealOnlyInverseTransform(frequencyDomainBuffer.data());
        // result is in frequencyDomainBuffer (in-place?) No, it uses the array.
        // Wait, performRealOnlyInverseTransform takes one argument. It transforms in-place.

        // Liftering: Keep low quefrency
        for (int i = cutoffBin; i < n - cutoffBin; ++i)
        {
            frequencyDomainBuffer[(size_t)i] = 0.0f;
        }

        // FFT back to Frequency Domain
        forwardFFT->performRealOnlyForwardTransform(frequencyDomainBuffer.data());

        // Exponentiate
        for (int i = 0; i < halfN; ++i)
        {
            float logEnv = frequencyDomainBuffer[(size_t)i * 2];
            envelope[(size_t)i] = std::exp(logEnv);
        }
    }

private:
    int fftSize = 0;
    std::unique_ptr<juce::dsp::FFT> forwardFFT;
    std::vector<float> timeDomainBuffer;
    std::vector<float> frequencyDomainBuffer;
};

} // namespace dsp
