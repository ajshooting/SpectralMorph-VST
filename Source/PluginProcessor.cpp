#include "PluginProcessor.h"
#include "PluginEditor.h"

SpectralFormantMorpherAudioProcessor::SpectralFormantMorpherAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                     .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                       ),
       apvts(*this, nullptr, "Parameters", createParameterLayout())
#endif
{
    apvts.addParameterListener("F1_SHIFT", this);
    apvts.addParameterListener("F2_SHIFT", this);
    apvts.addParameterListener("OVERALL_SCALE", this);
}

SpectralFormantMorpherAudioProcessor::~SpectralFormantMorpherAudioProcessor()
{
    apvts.removeParameterListener("F1_SHIFT", this);
    apvts.removeParameterListener("F2_SHIFT", this);
    apvts.removeParameterListener("OVERALL_SCALE", this);
}

juce::AudioProcessorValueTreeState::ParameterLayout SpectralFormantMorpherAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "F1_SHIFT", "F1 Shift",
        juce::NormalisableRange<float>(0.5f, 2.0f, 0.01f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "F2_SHIFT", "F2 Shift",
        juce::NormalisableRange<float>(0.5f, 2.0f, 0.01f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "OVERALL_SCALE", "Overall Scale",
        juce::NormalisableRange<float>(0.5f, 2.0f, 0.01f), 1.0f));

    return { params.begin(), params.end() };
}

const juce::String SpectralFormantMorpherAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SpectralFormantMorpherAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool SpectralFormantMorpherAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool SpectralFormantMorpherAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double SpectralFormantMorpherAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SpectralFormantMorpherAudioProcessor::getNumPrograms()
{
    return 1;
}

int SpectralFormantMorpherAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SpectralFormantMorpherAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused(index);
}

const juce::String SpectralFormantMorpherAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused(index);
    return {};
}

void SpectralFormantMorpherAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

void SpectralFormantMorpherAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32)samplesPerBlock;
    spec.numChannels = (juce::uint32)getTotalNumOutputChannels();

    spectralProcessor.prepare(spec);

    float f1 = *apvts.getRawParameterValue("F1_SHIFT");
    float f2 = *apvts.getRawParameterValue("F2_SHIFT");
    float scale = *apvts.getRawParameterValue("OVERALL_SCALE");
    spectralProcessor.setParameters(f1, f2, scale);
}

void SpectralFormantMorpherAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SpectralFormantMorpherAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void SpectralFormantMorpherAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    float f1 = apvts.getRawParameterValue("F1_SHIFT")->load();
    float f2 = apvts.getRawParameterValue("F2_SHIFT")->load();
    float scale = apvts.getRawParameterValue("OVERALL_SCALE")->load();
    spectralProcessor.setParameters(f1, f2, scale);

    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    spectralProcessor.process(context);
}

bool SpectralFormantMorpherAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* SpectralFormantMorpherAudioProcessor::createEditor()
{
    return new SpectralFormantMorpherAudioProcessorEditor (*this);
}

void SpectralFormantMorpherAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void SpectralFormantMorpherAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

void SpectralFormantMorpherAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    juce::ignoreUnused(parameterID, newValue);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralFormantMorpherAudioProcessor();
}
