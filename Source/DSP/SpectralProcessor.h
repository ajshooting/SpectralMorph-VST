#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "EnvelopeExtractor.h"
#include "FormantWarper.h"

namespace dsp
{

/**
 * Main Spectral Processing Engine.
 *
 * Implements the Source-Filter separation, warping, and reconstruction pipeline:
 * 1. Analysis: STFT with Hann Window and 75% overlap.
 * 2. Envelope Extraction: Cepstral Analysis.
 * 3. Formant Warping: Piecewise linear warping of the envelope.
 * 4. Resynthesis: Flatten spectrum (Source) * Warped Envelope (Filter).
 * 5. Synthesis: Inverse STFT and Overlap-Add.
 */
class SpectralProcessor
{
public:
    SpectralProcessor();
    ~SpectralProcessor();

    void prepare(const juce::dsp::ProcessSpec& spec);
    void process(const juce::dsp::ProcessContextReplacing<float>& context);
    void reset();

    /**
     * Updates the DSP parameters.
     * @param f1Shift Multiplier for the first formant frequency.
     * @param f2Shift Multiplier for the second formant frequency.
     * @param overallScale Global scaling factor for the frequency axis.
     */
    void setParameters(float f1Shift, float f2Shift, float overallScale) {
        f1ShiftFactor = f1Shift;
        f2ShiftFactor = f2Shift;
        overallScaleFactor = overallScale;
    }

    /**
     * Retrieves the latest spectral data for the GUI.
     * Thread-safe using a lock (tryEnter pattern).
     */
    void getLatestVisualizationData(std::vector<float>& spectrum, std::vector<float>& envelope, float& f1, float& f2);

private:
    /**
     * Processes a single FFT frame (frequency domain manipulation).
     */
    void processBlock(std::vector<float>& data);

    /**
     * Estimates F1 and F2 formants from the envelope using peak detection.
     */
    void detectFormants(const std::vector<float>& envelope, float& f1Bin, float& f2Bin);

    static constexpr int fftOrder = 10;          // 1024 samples
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int hopSize = fftSize / 4;  // 75% overlap (standard for STFT)

    double currentSampleRate = 44100.0;

    // Core DSP Modules
    std::unique_ptr<juce::dsp::FFT> fft;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;

    // Buffers
    std::vector<float> inputFifo;        // Input buffering for STFT
    std::vector<float> outputAccumulator;// Overlap-Add accumulator
    std::vector<float> fftBuffer;        // Temp buffer for FFT operations

    // Spectral Data containers
    std::vector<float> magnitudeSpectrum;
    std::vector<float> extractedEnvelope;
    std::vector<float> warpedEnvelope;

    // Helper classes
    EnvelopeExtractor envelopeExtractor;
    FormantWarper formantWarper;

    // Parameter State
    float f1ShiftFactor = 1.0f;
    float f2ShiftFactor = 1.0f;
    float overallScaleFactor = 1.0f;

    // Detected Features
    float currentF1 = 0.0f;
    float currentF2 = 0.0f;

    // Visualization (Thread Synchronization)
    juce::CriticalSection visualizationLock;
    std::vector<float> visSpectrum;
    std::vector<float> visEnvelope;
    float visF1 = 0.0f;
    float visF2 = 0.0f;

    int hopCounter = 0;
};

} // namespace dsp
