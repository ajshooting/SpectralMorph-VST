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

        // Setup FFT for Cepstrum
        // We use a complex FFT for simplicity with the juce::dsp::FFT class which often expects complex io,
        // though performRealOnlyForwardTransform is available.
        forwardFFT = std::make_unique<juce::dsp::FFT>((int)std::log2(fftSize));

        // Buffers
        timeDomainBuffer.resize((size_t)fftSize * 2);
        frequencyDomainBuffer.resize((size_t)fftSize * 2);
    }

    /**
     * Extracts the spectral envelope from a magnitude spectrum.
     *
     * @param magnitudeSpectrum Input magnitude spectrum (size = fftSize / 2 + 1).
     * @param envelope Output envelope (size = fftSize / 2 + 1).
     * @param cutoffBin The quefrency bin cutoff for liftering. Lower values = smoother envelope.
     */
    void process(const std::vector<float>& magnitudeSpectrum, std::vector<float>& envelope, int cutoffBin = 20)
    {
        // 1. Log Magnitude
        // Reconstruct symmetric full spectrum for IFFT
        // For real signals, mag[N-k] = mag[k]

        int n = fftSize;
        int halfN = n / 2 + 1;

        // We need to fill timeDomainBuffer with Log Magnitude for the FFT input
        // Since we want the "Real Cepstrum", we take the IFFT of the Log Magnitude.
        // juce::dsp::FFT::performRealOnlyInverseTransform takes frequency domain data.

        // Fill frequencyDomainBuffer
        // Structure for performRealOnlyInverseTransform:
        // Input is n * 2 floats (complex). But since Log Mag is real and symmetric, imaginary parts are 0.
        // Wait, performRealOnlyInverseTransform expects the complex spectrum of a real signal.
        // The output will be real time-domain samples (the cepstrum).

        std::fill(frequencyDomainBuffer.begin(), frequencyDomainBuffer.end(), 0.0f);

        for (int i = 0; i < halfN; ++i)
        {
            // Avoid log(0)
            float mag = std::max(magnitudeSpectrum[(size_t)i], 1e-9f);
            float logMag = std::log(mag);

            // Set Real part
            frequencyDomainBuffer[(size_t)i * 2] = logMag;
            // Imag part is 0
            frequencyDomainBuffer[(size_t)i * 2 + 1] = 0.0f;
        }

        // 2. IFFT to get Cepstrum
        // Output goes to timeDomainBuffer
        forwardFFT->performRealOnlyInverseTransform(frequencyDomainBuffer.data());

        // The output in timeDomainBuffer is the Cepstrum (real sequence).
        // 3. Liftering (Keep low quefrency)
        // We only keep indices [0, cutoffBin] and [N-cutoffBin, N-1] ?
        // Actually, simple windowing is better.
        // Just zero out everything above cutoffBin.

        // Scaling note: JUCE FFT inverse might need scaling or not, but since we are going back and forth,
        // scaling might cancel out or result in a constant gain offset.
        // We'll normalize later if needed.

        for (int i = cutoffBin; i < n - cutoffBin; ++i)
        {
            timeDomainBuffer[(size_t)i] = 0.0f;
        }

        // 4. FFT back to Frequency Domain
        // Input is the liftered cepstrum (in timeDomainBuffer).
        // We treat it as a real signal.
        // Output goes to frequencyDomainBuffer.

        forwardFFT->performRealOnlyForwardTransform(timeDomainBuffer.data());

        // 5. Exponentiate to get Linear Magnitude Envelope
        for (int i = 0; i < halfN; ++i)
        {
            // Real part of the resulting spectrum is the Log Magnitude of the envelope.
            // Imaginary part should be negligible if the cepstrum was symmetric/real properly processed.
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
