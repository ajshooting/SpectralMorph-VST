#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace dsp
{

    /**
     * Extracts the Spectral Envelope using Cepstral Analysis.
     *
     * The Source-Filter theory states that a speech signal S(f) is the product of
     * a Source Excitation E(f) (vocal cords) and a Spectral Envelope H(f) (vocal tract).
     * S(f) = E(f) * H(f)
     *
     * In the Log domain, this becomes additive:
     * log(S(f)) = log(E(f)) + log(H(f))
     *
     * The Cepstrum is the Inverse FFT of the Log Magnitude Spectrum.
     * Since H(f) changes slowly with frequency (smooth shape), its contribution is concentrated
     * in the low-quefrency region of the Cepstrum.
     * E(f) (harmonics) changes rapidly, so it resides in the high-quefrency region.
     *
     * Algorithm:
     * 1. Compute Log Magnitude of the input spectrum.
     * 2. Perform Inverse FFT to get the Cepstrum (Pseudo-time domain).
     * 3. Lifter: Keep only the low-quefrency coefficients (below a cutoff). This isolates the envelope.
     * 4. Perform Forward FFT to get the Log Magnitude Envelope.
     * 5. Exponentiate to get the Linear Magnitude Envelope.
     */
    class EnvelopeExtractor
    {
    public:
        EnvelopeExtractor() {}

        /**
         * Initializes the FFT and buffers.
         * @param newFftSize The size of the FFT (e.g., 1024).
         */
        void prepare(int newFftSize)
        {
            this->fftSize = newFftSize;
            // FFT order is log2(size)
            forwardFFT = std::make_unique<juce::dsp::FFT>((int)std::log2(fftSize));

            // Buffers for in-place transforms
            // JUCE FFT operations often require 2x size for complex numbers if not using specific real-only buffers effectively
            frequencyDomainBuffer.resize((size_t)fftSize * 2);
        }

        /**
         * Extracts the spectral envelope from a magnitude spectrum.
         *
         * @param magnitudeSpectrum Input magnitude spectrum (size = fftSize / 2 + 1).
         * @param envelope Output envelope (size = fftSize / 2 + 1).
         * @param cutoffBin The quefrency bin cutoff for liftering. Lower values = smoother envelope.
         */
        void process(const std::vector<float> &magnitudeSpectrum, std::vector<float> &envelope, int cutoffBin = 30)
        {
            int n = fftSize;
            int halfN = n / 2 + 1;

            // 1. Prepare Log Magnitude Spectrum
            std::fill(frequencyDomainBuffer.begin(), frequencyDomainBuffer.end(), 0.0f);

            for (int i = 0; i < halfN; ++i)
            {
                float mag = std::max(magnitudeSpectrum[(size_t)i], 1e-9f);
                float logMag = std::log(mag);

                frequencyDomainBuffer[(size_t)i * 2] = logMag;
                frequencyDomainBuffer[(size_t)i * 2 + 1] = 0.0f;
            }

            // 2. IFFT to get Cepstrum
            forwardFFT->performRealOnlyInverseTransform(frequencyDomainBuffer.data());

            // 3. Liftering (Low-pass filter in Quefrency domain)
            //    Keep only the first 'cutoffBin' coefficients and the symmetric tail.
            for (int i = cutoffBin; i < n - cutoffBin; ++i)
            {
                frequencyDomainBuffer[(size_t)i] = 0.0f;
            }

            // 4. FFT back to Frequency Domain
            forwardFFT->performRealOnlyForwardTransform(frequencyDomainBuffer.data());

            // 5. Exponentiate to get Linear Magnitude Envelope
            //    CRITICAL FIX: JUCE FFT round-trip (IFFT→FFT) multiplies by N.
            //    We must divide the log-domain result by N before exponentiating,
            //    otherwise we get envelope^N instead of envelope.
            const float invN = 1.0f / (float)n;
            for (int i = 0; i < halfN; ++i)
            {
                float logEnv = frequencyDomainBuffer[(size_t)i * 2] * invN;
                // Clamp log envelope to prevent extreme values
                logEnv = std::max(-20.0f, std::min(logEnv, 20.0f));
                envelope[(size_t)i] = std::exp(logEnv);
            }
        }

    private:
        int fftSize = 0;
        std::unique_ptr<juce::dsp::FFT> forwardFFT;
        std::vector<float> frequencyDomainBuffer; // Used for both Cepstrum (Time) and Spectrum (Freq)
    };

} // namespace dsp
