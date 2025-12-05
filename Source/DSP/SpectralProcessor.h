#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "EnvelopeExtractor.h"
#include "FormantWarper.h"

namespace dsp
{

class SpectralProcessor
{
public:
    SpectralProcessor();
    ~SpectralProcessor();

    void prepare(const juce::dsp::ProcessSpec& spec);
    void process(const juce::dsp::ProcessContextReplacing<float>& context);
    void reset();

    void setParameters(float f1Shift, float f2Shift, float overallScale) {
        f1ShiftFactor = f1Shift;
        f2ShiftFactor = f2Shift;
        overallScaleFactor = overallScale;
    }

    void getLatestVisualizationData(std::vector<float>& spectrum, std::vector<float>& envelope, float& f1, float& f2);

private:
    void processBlock(std::vector<float>& data);
    void detectFormants(const std::vector<float>& envelope, float& f1Bin, float& f2Bin);

    static constexpr int fftOrder = 10;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int hopSize = fftSize / 4;

    double currentSampleRate = 44100.0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;

    std::vector<float> inputFifo;
    std::vector<float> outputAccumulator;
    std::vector<float> fftBuffer;

    std::vector<float> magnitudeSpectrum;
    std::vector<float> extractedEnvelope;
    std::vector<float> warpedEnvelope;

    EnvelopeExtractor envelopeExtractor;
    FormantWarper formantWarper;

    float f1ShiftFactor = 1.0f;
    float f2ShiftFactor = 1.0f;
    float overallScaleFactor = 1.0f;

    float currentF1 = 0.0f;
    float currentF2 = 0.0f;

    juce::CriticalSection visualizationLock;
    std::vector<float> visSpectrum;
    std::vector<float> visEnvelope;
    float visF1 = 0.0f;
    float visF2 = 0.0f;

    int hopCounter = 0;
};

} // namespace dsp
