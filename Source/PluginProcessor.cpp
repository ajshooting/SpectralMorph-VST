#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <array>
#include <cmath>

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

  float readParameterValue(const juce::AudioProcessorValueTreeState &apvts, const juce::String &parameterID, float fallback)
  {
    if (const auto *value = apvts.getRawParameterValue(parameterID))
      return value->load();

    return fallback;
  }

  bool setParameterPlainValue(juce::AudioProcessorValueTreeState &apvts, const juce::String &parameterID, float value)
  {
    if (auto *param = apvts.getParameter(parameterID))
    {
      const float normalised = juce::jlimit(0.0f, 1.0f, param->convertTo0to1(value));
      param->beginChangeGesture();
      param->setValueNotifyingHost(normalised);
      param->endChangeGesture();
      return true;
    }

    return false;
  }

  bool getProfileNumber(const juce::DynamicObject &object,
                        const juce::Identifier &propertyName,
                        float fallback,
                        float &result)
  {
    const auto value = object.getProperty(propertyName);
    if (value.isVoid())
    {
      result = fallback;
      return true;
    }

    if (!value.isInt() && !value.isInt64() && !value.isDouble())
      return false;

    result = (float)(double)value;
    return std::isfinite(result);
  }

  bool isParameterValueInRange(juce::AudioProcessorValueTreeState &apvts,
                               const juce::String &parameterID,
                               float value)
  {
    if (!std::isfinite(value))
      return false;

    if (auto *param = apvts.getParameter(parameterID))
    {
      const float normalised = param->convertTo0to1(value);
      const float roundTripped = param->convertFrom0to1(normalised);
      return std::isfinite(normalised)
          && std::isfinite(roundTripped)
          && std::abs(roundTripped - value) <= 1.0e-3f;
    }

    return false;
  }

  void normaliseFormantValues(
      juce::AudioProcessorValueTreeState &apvts,
      std::array<float, dsp::SpectralProcessor::numFormants> &values)
  {
    std::array<float, dsp::SpectralProcessor::numFormants> minimums{};
    std::array<float, dsp::SpectralProcessor::numFormants> maximums{};

    for (size_t i = 0; i < values.size(); ++i)
    {
      if (auto *parameter = apvts.getParameter(formantParamId(i)))
      {
        minimums[i] = parameter->convertFrom0to1(0.0f);
        maximums[i] = parameter->convertFrom0to1(1.0f);
        values[i] = juce::jlimit(minimums[i], maximums[i], values[i]);
      }
    }

    for (size_t i = 1; i < values.size(); ++i)
      values[i] = std::max(values[i], values[i - 1] + 20.0f);

    values.back() = std::min(values.back(), maximums.back());
    for (size_t i = values.size() - 1; i-- > 0;)
      values[i] = std::min(values[i], values[i + 1] - 20.0f);

    for (size_t i = 0; i < values.size(); ++i)
      values[i] = juce::jlimit(minimums[i], maximums[i], values[i]);
  }
}

SpectralFormantMorpherAudioProcessor::SpectralFormantMorpherAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
#else
    : apvts(*this, nullptr, "Parameters", createParameterLayout())
#endif
{
  for (size_t i = 0; i < dsp::SpectralProcessor::numFormants; ++i)
    formantParameterValues[i] = apvts.getRawParameterValue(formantParamId(i));

  mixParameterValue = apvts.getRawParameterValue("MIX");
  outputGainParameterValue = apvts.getRawParameterValue("OUTPUT_GAIN");

  jassert(std::all_of(formantParameterValues.begin(), formantParameterValues.end(), [](const auto *value)
                      { return value != nullptr; }));
  jassert(mixParameterValue != nullptr);
  jassert(outputGainParameterValue != nullptr);

  setLatencySamples(dsp::SpectralProcessor::getLatencySamples());
}

SpectralFormantMorpherAudioProcessor::~SpectralFormantMorpherAudioProcessor() = default;

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
    if (const auto *param = formantParameterValues[i])
      formants[i] = param->load(std::memory_order_relaxed);
  }

  return formants;
}

bool SpectralFormantMorpherAudioProcessor::analyzeSourceFile(
    const juce::File &sourceFile,
    std::array<float, dsp::SpectralProcessor::numFormants> &estimatedHz,
    size_t &detectedFormantCount,
    juce::String &message,
    const std::function<bool()> &shouldCancel)
{
  const auto wasCancelled = [&shouldCancel]
  {
    return shouldCancel && shouldCancel();
  };

  detectedFormantCount = 0;

  if (wasCancelled())
  {
    message = "Reference analysis cancelled.";
    return false;
  }

  if (!sourceFile.existsAsFile())
  {
    message = "The reference audio file could not be found.";
    return false;
  }

  juce::AudioFormatManager localFormatManager;
  localFormatManager.registerBasicFormats();
  std::unique_ptr<juce::AudioFormatReader> reader(localFormatManager.createReaderFor(sourceFile));
  if (reader == nullptr)
  {
    message = "Could not read the reference audio. Try WAV, AIFF, FLAC, or Ogg.";
    return false;
  }

  if (!std::isfinite(reader->sampleRate)
      || reader->sampleRate <= 0.0
      || reader->numChannels == 0)
  {
    message = "The reference audio has invalid stream information.";
    return false;
  }

  const juce::int64 maxReadSamples = std::min<juce::int64>((juce::int64)(reader->sampleRate * 6.0), reader->lengthInSamples);
  if (maxReadSamples <= 0)
  {
    message = "The reference audio contains no usable samples.";
    return false;
  }

  const int channelsToRead = std::min(2, std::max(1, (int)reader->numChannels));
  juce::AudioBuffer<float> fileBuffer(channelsToRead, (int)maxReadSamples);
  fileBuffer.clear();

  constexpr int decodeChunkSize = 32768;
  for (int start = 0; start < (int)maxReadSamples; start += decodeChunkSize)
  {
    if (wasCancelled())
    {
      message = "Reference analysis cancelled.";
      return false;
    }

    const int samplesToRead = std::min(
        decodeChunkSize, (int)maxReadSamples - start);
    if (!reader->read(
            &fileBuffer,
            start,
            samplesToRead,
            start,
            true,
            channelsToRead > 1))
    {
      message = "Could not decode samples from the reference audio.";
      return false;
    }
  }

  juce::AudioBuffer<float> monoBuffer(1, (int)maxReadSamples);
  monoBuffer.clear();

  const float monoGain = 1.0f / (float)channelsToRead;
  for (int ch = 0; ch < channelsToRead; ++ch)
  {
    if (wasCancelled())
    {
      message = "Reference analysis cancelled.";
      return false;
    }

    monoBuffer.addFrom(0, 0, fileBuffer, ch, 0, (int)maxReadSamples, monoGain);
  }

  dsp::SpectralProcessor referenceAnalyzer;
  if (!referenceAnalyzer.estimateFormantsFromBuffer(
          monoBuffer,
          reader->sampleRate,
          estimatedHz,
          &detectedFormantCount,
          shouldCancel))
  {
    message = wasCancelled()
                  ? "Reference analysis cancelled."
                  : "No reliable formant pattern was found. Try a clear, sustained vocal sample.";
    return false;
  }

  message = "Reference analysis complete.";
  return true;
}

void SpectralFormantMorpherAudioProcessor::applyReferenceFormants(
    const std::array<float, dsp::SpectralProcessor::numFormants> &estimatedHz,
    size_t detectedFormantCount)
{
  auto normalised = collectTargetFormantsFromParameters();
  const size_t valuesToApply = std::min(detectedFormantCount, normalised.size());
  std::copy_n(estimatedHz.begin(), valuesToApply, normalised.begin());
  normaliseFormantValues(apvts, normalised);

  for (size_t i = 0; i < normalised.size(); ++i)
    setParameterPlainValue(apvts, formantParamId(i), normalised[i]);
}

juce::String SpectralFormantMorpherAudioProcessor::createVoiceProfileJson() const
{
  juce::DynamicObject::Ptr profile = new juce::DynamicObject();
  profile->setProperty("type", "SpectralFormantMorpherProfile");
  profile->setProperty("version", 1);
  profile->setProperty("product", "Spectral Formant Morpher");

  juce::Array<juce::var> formants;
  for (size_t i = 0; i < dsp::SpectralProcessor::numFormants; ++i)
    formants.add(readParameterValue(apvts, formantParamId(i), defaultFormantsHz[i]));

  profile->setProperty("formantsHz", formants);
  profile->setProperty("mix", readParameterValue(apvts, "MIX", 100.0f));
  profile->setProperty("outputGainDb", readParameterValue(apvts, "OUTPUT_GAIN", 0.0f));

  return juce::JSON::toString(juce::var(profile.get()), true);
}

bool SpectralFormantMorpherAudioProcessor::applyVoiceProfileJson(const juce::String &jsonText, juce::String &message)
{
  juce::var parsed;
  const auto parseResult = juce::JSON::parse(jsonText, parsed);
  if (parseResult.failed())
  {
    message = "Could not parse the profile JSON: " + parseResult.getErrorMessage();
    return false;
  }

  auto *object = parsed.getDynamicObject();
  if (object == nullptr)
  {
    message = "The profile must be a JSON object.";
    return false;
  }

  const auto type = object->getProperty("type").toString();
  if (type.isNotEmpty() && type != "SpectralFormantMorpherProfile")
  {
    message = "This profile belongs to a different product.";
    return false;
  }

  const auto versionValue = object->getProperty("version");
  if (!versionValue.isVoid()
      && ((!versionValue.isInt() && !versionValue.isInt64() && !versionValue.isDouble())
          || !std::isfinite((double)versionValue)
          || std::abs((double)versionValue - 1.0) > 1.0e-9))
  {
    message = "This profile version is not supported.";
    return false;
  }

  const auto formantsVar = object->getProperty("formantsHz");
  auto *formants = formantsVar.getArray();
  if (formants == nullptr || formants->size() < (int)dsp::SpectralProcessor::numFormants)
  {
    message = "The profile must contain F1 through F15.";
    return false;
  }

  std::array<float, dsp::SpectralProcessor::numFormants> validatedFormants{};
  for (size_t i = 0; i < dsp::SpectralProcessor::numFormants; ++i)
  {
    const auto &value = (*formants)[(int)i];
    if (!value.isInt() && !value.isInt64() && !value.isDouble())
    {
      message = "Every formant value must be a finite number.";
      return false;
    }

    const float formant = (float)(double)value;

    if (!isParameterValueInRange(apvts, formantParamId(i), formant))
    {
      message = "A formant value is outside the supported range.";
      return false;
    }

    validatedFormants[i] = formant;
  }

  normaliseFormantValues(apvts, validatedFormants);

  float validatedMix = 100.0f;
  float validatedGain = 0.0f;
  if (!getProfileNumber(*object, "mix", readParameterValue(apvts, "MIX", 100.0f), validatedMix)
      || !getProfileNumber(*object, "outputGainDb", readParameterValue(apvts, "OUTPUT_GAIN", 0.0f), validatedGain)
      || !isParameterValueInRange(apvts, "MIX", validatedMix)
      || !isParameterValueInRange(apvts, "OUTPUT_GAIN", validatedGain))
  {
    message = "Mix or output gain is invalid.";
    return false;
  }

  for (size_t i = 0; i < validatedFormants.size(); ++i)
    setParameterPlainValue(apvts, formantParamId(i), validatedFormants[i]);

  setParameterPlainValue(apvts, "MIX", validatedMix);
  setParameterPlainValue(apvts, "OUTPUT_GAIN", validatedGain);

  message = "Voice Profile applied.";
  return true;
}

void SpectralFormantMorpherAudioProcessor::normaliseFormantParameters()
{
  if (isNormalisingFormants)
    return;

  const juce::ScopedValueSetter<bool> guard(isNormalisingFormants, true);
  auto values = collectTargetFormantsFromParameters();
  normaliseFormantValues(apvts, values);

  for (size_t i = 0; i < values.size(); ++i)
  {
    if (auto *parameter = apvts.getParameter(formantParamId(i)))
    {
      const float current = formantParameterValues[i] != nullptr
                                ? formantParameterValues[i]->load(std::memory_order_relaxed)
                                : values[i];
      if (std::abs(current - values[i]) > 0.5f)
        parameter->setValueNotifyingHost(parameter->convertTo0to1(values[i]));
    }
  }
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

  dryDelayLine.prepare(spec);
  dryDelayLine.setDelay((float)dsp::SpectralProcessor::getLatencySamples());
  dryDelayLine.reset();

  const float initialMix = mixParameterValue != nullptr
                               ? juce::jlimit(0.0f, 1.0f, mixParameterValue->load(std::memory_order_relaxed) / 100.0f)
                               : 1.0f;
  const float initialGain = juce::Decibels::decibelsToGain(
      outputGainParameterValue != nullptr
          ? outputGainParameterValue->load(std::memory_order_relaxed)
          : 0.0f);

  mixSmoother.reset(sampleRate, 0.02);
  mixSmoother.setCurrentAndTargetValue(initialMix);
  outputGainSmoother.reset(sampleRate, 0.02);
  outputGainSmoother.setCurrentAndTargetValue(initialGain);

  dryBuffer.setSize(getTotalNumOutputChannels(), samplesPerBlock, false, false, true);
  setLatencySamples(dsp::SpectralProcessor::getLatencySamples());
}

void SpectralFormantMorpherAudioProcessor::releaseResources()
{
  spectralProcessor.reset();
  dryDelayLine.reset();
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

  const float mixTarget = mixParameterValue != nullptr
                              ? juce::jlimit(0.0f, 1.0f, mixParameterValue->load(std::memory_order_relaxed) / 100.0f)
                              : 1.0f;
  const float outputGainTarget = juce::Decibels::decibelsToGain(
      outputGainParameterValue != nullptr
          ? outputGainParameterValue->load(std::memory_order_relaxed)
          : 0.0f);
  mixSmoother.setTargetValue(mixTarget);
  outputGainSmoother.setTargetValue(outputGainTarget);

  dryBuffer.makeCopyOf(buffer, true);
  juce::dsp::AudioBlock<float> dryBlock(dryBuffer);
  juce::dsp::ProcessContextReplacing<float> dryContext(dryBlock);
  dryDelayLine.process(dryContext);

  // Process wet signal
  juce::dsp::AudioBlock<float> block(buffer);
  juce::dsp::ProcessContextReplacing<float> context(block);
  spectralProcessor.process(context);

  // Apply dry/wet mix and output gain
  const int numSamples = buffer.getNumSamples();
  for (int i = 0; i < numSamples; ++i)
  {
    const float mix = mixSmoother.getNextValue();
    const float outputGain = outputGainSmoother.getNextValue();

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
      auto *wet = buffer.getWritePointer(ch);
      const auto *dry = dryBuffer.getReadPointer(ch);
      float sample = dry[i] * (1.0f - mix) + wet[i] * mix;

      sample *= outputGain;

      if (!std::isfinite(sample))
        sample = 0.0f;

      wet[i] = sample;
    }
  }
}

void SpectralFormantMorpherAudioProcessor::processBlockBypassed(
    juce::AudioBuffer<float> &buffer,
    juce::MidiBuffer &midiMessages)
{
  juce::ignoreUnused(midiMessages);
  juce::ScopedNoDenormals noDenormals;

  const int totalNumInputChannels = getTotalNumInputChannels();
  const int totalNumOutputChannels = getTotalNumOutputChannels();

  for (int channel = totalNumInputChannels; channel < totalNumOutputChannels; ++channel)
    buffer.clear(channel, 0, buffer.getNumSamples());

  dryBuffer.makeCopyOf(buffer, true);
  juce::dsp::AudioBlock<float> dryBlock(dryBuffer);
  juce::dsp::ProcessContextReplacing<float> dryContext(dryBlock);
  dryDelayLine.process(dryContext);

  // Keep the wet STFT history moving while bypassed so toggling bypass does not
  // replay stale overlap-add data.
  spectralProcessor.setTargetFormantsHz(collectTargetFormantsFromParameters());
  juce::dsp::AudioBlock<float> wetStateBlock(buffer);
  juce::dsp::ProcessContextReplacing<float> wetStateContext(wetStateBlock);
  spectralProcessor.process(wetStateContext);

  for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    buffer.copyFrom(channel, 0, dryBuffer, channel, 0, buffer.getNumSamples());
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

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter()
{
  return new SpectralFormantMorpherAudioProcessor();
}
