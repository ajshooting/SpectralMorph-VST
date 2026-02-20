#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <array>

namespace
{
  constexpr std::array<float, dsp::SpectralProcessor::numFormants> defaultFormantsHz{
      500.0f, 1500.0f, 2500.0f, 3200.0f, 3800.0f,
      4400.0f, 5000.0f, 5600.0f, 6200.0f, 6800.0f,
      7400.0f, 8000.0f, 8600.0f, 9200.0f, 9800.0f};

  juce::String formantParamId(size_t index)
  {
    return "FORMANT_" + juce::String((int)index + 1);
  }
}

SpectralFormantMorpherAudioProcessor::SpectralFormantMorpherAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
#endif
{
  formatManager.registerBasicFormats();

  for (size_t i = 0; i < dsp::SpectralProcessor::numFormants; ++i)
    apvts.addParameterListener(formantParamId(i), this);

  apvts.addParameterListener("MIX", this);
  apvts.addParameterListener("OUTPUT_GAIN", this);
}

SpectralFormantMorpherAudioProcessor::~SpectralFormantMorpherAudioProcessor()
{
  for (size_t i = 0; i < dsp::SpectralProcessor::numFormants; ++i)
    apvts.removeParameterListener(formantParamId(i), this);

  apvts.removeParameterListener("MIX", this);
  apvts.removeParameterListener("OUTPUT_GAIN", this);
}

juce::AudioProcessorValueTreeState::ParameterLayout SpectralFormantMorpherAudioProcessor::createParameterLayout()
{
  std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

  for (size_t i = 0; i < dsp::SpectralProcessor::numFormants; ++i)
  {
    float minHz = 500.0f;
    float maxHz = 12000.0f;

    if (i == 0)
    {
      minHz = 200.0f;
      maxHz = 1000.0f;
    }
    else if (i == 1)
    {
      minHz = 800.0f;
      maxHz = 3500.0f;
    }

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        formantParamId(i),
        "F" + juce::String((int)i + 1) + " (Hz)",
        juce::NormalisableRange<float>(minHz, maxHz, 1.0f),
        defaultFormantsHz[i]));
  }

  // Dry/Wet Mix (0% = fully dry, 100% = fully wet)
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      "MIX", "Mix",
      juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
      100.0f));

  // Output Gain (dB)
  params.push_back(std::make_unique<juce::AudioParameterFloat>(
      "OUTPUT_GAIN", "Output Gain",
      juce::NormalisableRange<float>(-24.0f, 6.0f, 0.1f),
      0.0f));

  return {params.begin(), params.end()};
}

std::array<float, dsp::SpectralProcessor::numFormants> SpectralFormantMorpherAudioProcessor::collectTargetFormantsFromParameters() const
{
  std::array<float, dsp::SpectralProcessor::numFormants> formants = defaultFormantsHz;

  for (size_t i = 0; i < formants.size(); ++i)
  {
    if (const auto *param = apvts.getRawParameterValue(formantParamId(i)))
      formants[i] = param->load();
  }

  return formants;
}

bool SpectralFormantMorpherAudioProcessor::analyzeSourceFileAndApplyFormants(const juce::File &sourceFile, juce::String &message)
{
  if (!sourceFile.existsAsFile())
  {
    message = "ソース音源ファイルが見つかりません。";
    return false;
  }

  std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(sourceFile));
  if (reader == nullptr)
  {
    message = "音源の読み込みに失敗しました。対応フォーマットを確認してください。";
    return false;
  }

  const juce::int64 maxReadSamples = juce::jmin<juce::int64>((juce::int64)(reader->sampleRate * 6.0), reader->lengthInSamples);
  if (maxReadSamples <= 0)
  {
    message = "音源に有効なサンプルがありません。";
    return false;
  }

  juce::AudioBuffer<float> sourceBuffer(1, (int)maxReadSamples);
  if (!reader->read(&sourceBuffer, 0, (int)maxReadSamples, 0, true, true))
  {
    message = "音源サンプルの読取に失敗しました。";
    return false;
  }

  auto estimated = spectralProcessor.estimateFormantsFromBuffer(sourceBuffer, reader->sampleRate);

  for (size_t i = 0; i < estimated.size(); ++i)
  {
    if (auto *param = apvts.getParameter(formantParamId(i)))
      param->setValueNotifyingHost(param->convertTo0to1(estimated[i]));
  }

  spectralProcessor.setTargetFormantsHz(estimated);
  message = "ソース音源からF1〜F15を推定して適用しました。";
  return true;
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

void SpectralFormantMorpherAudioProcessor::setCurrentProgram(int index)
{
  juce::ignoreUnused(index);
}

const juce::String SpectralFormantMorpherAudioProcessor::getProgramName(int index)
{
  juce::ignoreUnused(index);
  return {};
}

void SpectralFormantMorpherAudioProcessor::changeProgramName(int index, const juce::String &newName)
{
  juce::ignoreUnused(index, newName);
}

void SpectralFormantMorpherAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
  juce::dsp::ProcessSpec spec;
  spec.sampleRate = sampleRate;
  spec.maximumBlockSize = (juce::uint32)samplesPerBlock;
  spec.numChannels = (juce::uint32)getTotalNumOutputChannels();

  spectralProcessor.prepare(spec);
  spectralProcessor.setTargetFormantsHz(collectTargetFormantsFromParameters());

  dryBuffer.setSize(getTotalNumOutputChannels(), samplesPerBlock);
}

void SpectralFormantMorpherAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SpectralFormantMorpherAudioProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
#if JucePlugin_IsMidiEffect
  juce::ignoreUnused(layouts);
  return true;
#else
  if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
    return false;

#if !JucePlugin_IsSynth
  if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
    return false;
#endif

  return true;
#endif
}
#endif

void SpectralFormantMorpherAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midiMessages)
{
  juce::ignoreUnused(midiMessages);
  juce::ScopedNoDenormals noDenormals;

  auto totalNumInputChannels = getTotalNumInputChannels();
  auto totalNumOutputChannels = getTotalNumOutputChannels();

  for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
    buffer.clear(i, 0, buffer.getNumSamples());

  spectralProcessor.setTargetFormantsHz(collectTargetFormantsFromParameters());

  // Save dry signal for mix
  const float mix = apvts.getRawParameterValue("MIX")->load() / 100.0f;
  const float outputGainDb = apvts.getRawParameterValue("OUTPUT_GAIN")->load();
  const float outputGain = juce::Decibels::decibelsToGain(outputGainDb);

  dryBuffer.makeCopyOf(buffer, true);

  // Process wet signal
  juce::dsp::AudioBlock<float> block(buffer);
  juce::dsp::ProcessContextReplacing<float> context(block);
  spectralProcessor.process(context);

  // Apply dry/wet mix and output gain
  const int numSamples = buffer.getNumSamples();
  for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
  {
    auto *wet = buffer.getWritePointer(ch);
    const auto *dry = dryBuffer.getReadPointer(ch);

    for (int i = 0; i < numSamples; ++i)
    {
      // Mix: 0 = dry, 1 = wet
      float sample = dry[i] * (1.0f - mix) + wet[i] * mix;

      // Apply output gain
      sample *= outputGain;

      // Safety soft clip to prevent extreme values
      sample = std::tanh(sample);

      wet[i] = sample;
    }
  }
}

bool SpectralFormantMorpherAudioProcessor::hasEditor() const
{
  return true;
}

juce::AudioProcessorEditor *SpectralFormantMorpherAudioProcessor::createEditor()
{
  return new SpectralFormantMorpherAudioProcessorEditor(*this);
}

void SpectralFormantMorpherAudioProcessor::getStateInformation(juce::MemoryBlock &destData)
{
  auto state = apvts.copyState();
  std::unique_ptr<juce::XmlElement> xml(state.createXml());
  copyXmlToBinary(*xml, destData);
}

void SpectralFormantMorpherAudioProcessor::setStateInformation(const void *data, int sizeInBytes)
{
  std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
  if (xmlState.get() != nullptr)
    if (xmlState->hasTagName(apvts.state.getType()))
      apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

void SpectralFormantMorpherAudioProcessor::parameterChanged(const juce::String &parameterID, float newValue)
{
  juce::ignoreUnused(parameterID, newValue);
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter()
{
  return new SpectralFormantMorpherAudioProcessor();
}
