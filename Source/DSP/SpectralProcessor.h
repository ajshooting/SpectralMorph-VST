#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
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
        static constexpr size_t numFormants = 15;

        SpectralProcessor();
        ~SpectralProcessor();

        void prepare(const juce::dsp::ProcessSpec &spec);
        void process(const juce::dsp::ProcessContextReplacing<float> &context);
        void reset();

        void setTargetFormantsHz(const std::array<float, numFormants> &targetHz);

        std::array<float, numFormants> estimateFormantsFromBuffer(const juce::AudioBuffer<float> &sourceBuffer, double sourceSampleRate);

        /**
         * Retrieves the latest spectral data for the GUI.
         * Thread-safe using a lock (tryEnter pattern).
         */
        void getLatestVisualizationData(std::vector<float> &spectrum, std::vector<float> &envelope, float &f1, float &f2);

    private:
        /**
         * Processes a single FFT frame (frequency domain manipulation).
         */
        void processBlock(std::vector<float> &data);

        void detectFormants(const std::vector<float> &envelope, double sampleRate, std::array<float, numFormants> &formantBins) const;

        static constexpr int fftOrder = 10; // 1024 samples
        static constexpr int fftSize = 1 << fftOrder;
        static constexpr int hopSize = fftSize / 4; // 75% overlap (standard for STFT)

        double currentSampleRate = 44100.0;

        // Core DSP Modules
        std::unique_ptr<juce::dsp::FFT> fft;
        std::unique_ptr<juce::dsp::WindowingFunction<float>> window;

        // Buffers
        std::vector<float> inputFifo;         // Input buffering for STFT
        std::vector<float> outputAccumulator; // Overlap-Add accumulator
        std::vector<float> fftBuffer;         // Temp buffer for FFT operations

        // Spectral Data containers
        std::vector<float> magnitudeSpectrum;
        std::vector<float> extractedEnvelope;
        std::vector<float> warpedEnvelope;

        // Helper classes
        EnvelopeExtractor envelopeExtractor;
        FormantWarper formantWarper;

        std::array<float, numFormants> targetFormantsHz{
            500.0f, 1500.0f, 2500.0f, 3200.0f, 3800.0f,
            4400.0f, 5000.0f, 5600.0f, 6200.0f, 6800.0f,
            7400.0f, 8000.0f, 8600.0f, 9200.0f, 9800.0f};

        std::array<float, numFormants> currentFormantBins{};

        // Visualization (Thread Synchronization)
        juce::CriticalSection visualizationLock;
        std::vector<float> visSpectrum;
        std::vector<float> visEnvelope;
        float visF1 = 0.0f;
        float visF2 = 0.0f;

        int hopCounter = 0;
    };

} // namespace dsp
