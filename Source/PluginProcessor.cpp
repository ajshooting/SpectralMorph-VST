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
    apvts.addParameterListener("OVERALL_SCALE", this);
}

SpectralFormantMorpherAudioProcessor::~SpectralFormantMorpherAudioProcessor()
{
    apvts.removeParameterListener("F1_SHIFT", this);
    apvts.removeParameterListener("OVERALL_SCALE", this);
}

juce::AudioProcessorValueTreeState::ParameterLayout SpectralFormantMorpherAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "F1_SHIFT", "F1 Shift",
        juce::NormalisableRange<float>(0.5f, 2.0f, 0.01f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "OVERALL_SCALE", "Warp Scale",
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
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int SpectralFormantMorpherAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SpectralFormantMorpherAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String SpectralFormantMorpherAudioProcessor::getProgramName (int index)
{
    return {};
}

void SpectralFormantMorpherAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

void SpectralFormantMorpherAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = getTotalNumOutputChannels();

    spectralProcessor.prepare(spec);

    // Initialize params
    float shift = *apvts.getRawParameterValue("F1_SHIFT"); // Simplified mapping
    float scale = *apvts.getRawParameterValue("OVERALL_SCALE");
    spectralProcessor.setShiftFactor(shift * scale); // Mixing logic for now
}

void SpectralFormantMorpherAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SpectralFormantMorpherAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
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
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // Update parameters
    float shift = apvts.getRawParameterValue("F1_SHIFT")->load();
    float scale = apvts.getRawParameterValue("OVERALL_SCALE")->load();
    spectralProcessor.setShiftFactor(shift * scale); // Logic: Combining them for the simple warper

    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    spectralProcessor.process(context);
}

bool SpectralFormantMorpherAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
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
    // Handled in processBlock for thread safety (using atomic load)
}

// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralFormantMorpherAudioProcessor();
}
