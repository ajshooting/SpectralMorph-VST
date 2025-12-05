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
    void process(const std::vector<float>& magnitudeSpectrum, std::vector<float>& envelope, int cutoffBin = 20)
    {
        int n = fftSize;
        int halfN = n / 2 + 1;

        // 1. Prepare Log Magnitude Spectrum
        // We construct a symmetric spectrum so the IFFT yields a real result (the Cepstrum).
        // JUCE's performRealOnlyInverseTransform expects the first half of the complex spectrum.
        // Since input is Magnitude (Real), Imaginary part is 0.

        std::fill(frequencyDomainBuffer.begin(), frequencyDomainBuffer.end(), 0.0f);

        for (int i = 0; i < halfN; ++i)
        {
            // Add small epsilon to avoid log(0)
            float mag = std::max(magnitudeSpectrum[(size_t)i], 1e-9f);
            float logMag = std::log(mag);

            // Set Real part
            frequencyDomainBuffer[(size_t)i * 2] = logMag;
            // Imag part is 0
            frequencyDomainBuffer[(size_t)i * 2 + 1] = 0.0f;
        }

        // 2. IFFT to get Cepstrum (Real sequence)
        // The output overwrites frequencyDomainBuffer.
        forwardFFT->performRealOnlyInverseTransform(frequencyDomainBuffer.data());

        // 3. Liftering (Low-pass filter in Quefrency domain)
        // We only keep the first 'cutoffBin' coefficients.
        // The rest (representing the fine harmonic structure) are zeroed out.
        // Note: We zero out both the "positive" and "negative" frequencies in the cepstral domain
        // to maintain symmetry if we were doing a full complex FFT, but for real-only transform,
        // the layout is roughly 0..N/2.
        // Actually, performRealOnlyInverseTransform output is Time Domain samples [0..N-1].
        // We zero out the middle part (high quefrencies).

        for (int i = cutoffBin; i < n - cutoffBin; ++i)
        {
            frequencyDomainBuffer[(size_t)i] = 0.0f;
        }

        // 4. FFT back to Frequency Domain
        // Input is the liftered cepstrum. Output is the smoothed Log Magnitude Spectrum.
        forwardFFT->performRealOnlyForwardTransform(frequencyDomainBuffer.data());

        // 5. Exponentiate to get Linear Magnitude Envelope
        // exp(log(H(f))) = H(f)
        for (int i = 0; i < halfN; ++i)
        {
            float logEnv = frequencyDomainBuffer[(size_t)i * 2]; // Real part
            envelope[(size_t)i] = std::exp(logEnv);
        }
    }

private:
    int fftSize = 0;
    std::unique_ptr<juce::dsp::FFT> forwardFFT;
    std::vector<float> frequencyDomainBuffer; // Used for both Cepstrum (Time) and Spectrum (Freq)
};

} // namespace dsp
