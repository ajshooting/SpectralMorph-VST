#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <functional>
#include "DSP/SpectralProcessor.h"

class SpectralFormantMorpherAudioProcessor : public juce::AudioProcessor
{
public:
    SpectralFormantMorpherAudioProcessor();
    ~SpectralFormantMorpherAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    void processBlockBypassed(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

    juce::AudioProcessorEditor *createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String &newName) override;

    void getStateInformation(juce::MemoryBlock &destData) override;
    void setStateInformation(const void *data, int sizeInBytes) override;

    static bool analyzeSourceFile(
        const juce::File &sourceFile,
        std::array<float, dsp::SpectralProcessor::numFormants> &estimatedHz,
        size_t &detectedFormantCount,
        juce::String &message,
        const std::function<bool()> &shouldCancel = {});
    void applyReferenceFormants(
        const std::array<float, dsp::SpectralProcessor::numFormants> &estimatedHz,
        size_t detectedFormantCount);
    juce::String createVoiceProfileJson() const;
    bool applyVoiceProfileJson(const juce::String &jsonText, juce::String &message);
    void normaliseFormantParameters();

    juce::AudioProcessorValueTreeState &getAPVTS() { return apvts; }
    dsp::SpectralProcessor &getSpectralProcessor() { return spectralProcessor; }

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    std::array<float, dsp::SpectralProcessor::numFormants> collectTargetFormantsFromParameters() const;

    dsp::SpectralProcessor spectralProcessor;
    std::array<std::atomic<float> *, dsp::SpectralProcessor::numFormants> formantParameterValues{};
    std::atomic<float> *mixParameterValue = nullptr;
    std::atomic<float> *outputGainParameterValue = nullptr;

    // The STFT wet path is delayed by one FFT frame. Delay dry by the same
    // amount so intermediate mix values stay phase-aligned.
    juce::AudioBuffer<float> dryBuffer;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelayLine{
        dsp::SpectralProcessor::getLatencySamples()};

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGainSmoother;
    bool isNormalisingFormants = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralFormantMorpherAudioProcessor)
};
