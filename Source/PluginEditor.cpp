#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
  constexpr float f1MinHz = 200.0f;
  constexpr float f1MaxHz = 1000.0f;
  constexpr float f2MinHz = 800.0f;
  constexpr float f2MaxHz = 3500.0f;

  juce::String formantParamId(size_t index)
  {
    return "FORMANT_" + juce::String((int)index + 1);
  }

  float readParamValue(SpectralFormantMorpherAudioProcessor &processor, const juce::String &id, float fallback)
  {
    if (const auto *value = processor.getAPVTS().getRawParameterValue(id))
      return value->load();

    return fallback;
  }
}

void SpectralFormantMorpherAudioProcessorEditor::configureUtilityButton(juce::TextButton &button)
{
  button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff273038));
  button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff334047));
  button.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.92f));
  button.addListener(this);
  addAndMakeVisible(button);
}

void SpectralFormantMorpherAudioProcessorEditor::setStatusMessage(const juce::String &message, bool ok)
{
  statusLabel.setText(message, juce::dontSendNotification);
  statusLabel.setColour(juce::Label::textColourId, ok ? juce::Colour(0xff7ee082) : juce::Colour(0xfff4c95d));
}

void XYFormantPad::paint(juce::Graphics &g)
{
  auto bounds = getLocalBounds().toFloat().reduced(10.0f);
  g.setColour(juce::Colour(0xff15191c));
  g.fillRoundedRectangle(bounds, 6.0f);
  g.setColour(juce::Colour(0xff343b42));
  g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

  for (int i = 1; i < 4; ++i)
  {
    const float x = bounds.getX() + bounds.getWidth() * (float)i / 4.0f;
    const float y = bounds.getY() + bounds.getHeight() * (float)i / 4.0f;
    g.setColour(juce::Colour(0xff252b30));
    g.drawVerticalLine((int)x, bounds.getY(), bounds.getBottom());
    g.drawHorizontalLine((int)y, bounds.getX(), bounds.getRight());
  }

  const float f1 = juce::jlimit(f1MinHz, f1MaxHz, readParamValue(processor, "FORMANT_1", 500.0f));
  const float f2 = juce::jlimit(f2MinHz, f2MaxHz, readParamValue(processor, "FORMANT_2", 1500.0f));

  const float x = juce::jmap(f2, f2MinHz, f2MaxHz, bounds.getX(), bounds.getRight());
  const float y = juce::jmap(f1, f1MaxHz, f1MinHz, bounds.getY(), bounds.getBottom());

  g.setColour(juce::Colour(0xff61d6d6).withAlpha(0.55f));
  g.drawLine(bounds.getX(), y, bounds.getRight(), y, 1.0f);
  g.drawLine(x, bounds.getY(), x, bounds.getBottom(), 1.0f);

  g.setColour(juce::Colour(0xff7ee082));
  g.fillEllipse(x - 8.0f, y - 8.0f, 16.0f, 16.0f);
  g.setColour(juce::Colour(0xff0b0d0e));
  g.drawEllipse(x - 8.0f, y - 8.0f, 16.0f, 16.0f, 2.0f);

  g.setFont(13.0f);
  g.setColour(juce::Colours::white.withAlpha(0.84f));
  g.drawText("F1 " + juce::String((int)std::round(f1)) + " Hz", bounds.toNearestInt().reduced(8).removeFromTop(20), juce::Justification::left);
  g.drawText("F2 " + juce::String((int)std::round(f2)) + " Hz", bounds.toNearestInt().reduced(8).removeFromBottom(20), juce::Justification::right);
}

void XYFormantPad::mouseDown(const juce::MouseEvent &event)
{
  beginGesture();
  updateFromPosition(event.position);
}

void XYFormantPad::mouseDrag(const juce::MouseEvent &event)
{
  updateFromPosition(event.position);
}

void XYFormantPad::mouseUp(const juce::MouseEvent &event)
{
  updateFromPosition(event.position);
  endGesture();
}

void XYFormantPad::timerCallback()
{
  repaint();
}

void XYFormantPad::beginGesture()
{
  if (dragging)
    return;

  dragging = true;

  if (auto *f1Param = processor.getAPVTS().getParameter("FORMANT_1"))
    f1Param->beginChangeGesture();

  if (auto *f2Param = processor.getAPVTS().getParameter("FORMANT_2"))
    f2Param->beginChangeGesture();
}

void XYFormantPad::endGesture()
{
  if (!dragging)
    return;

  if (auto *f1Param = processor.getAPVTS().getParameter("FORMANT_1"))
    f1Param->endChangeGesture();

  if (auto *f2Param = processor.getAPVTS().getParameter("FORMANT_2"))
    f2Param->endChangeGesture();

  dragging = false;
}

void XYFormantPad::updateFromPosition(juce::Point<float> pos)
{
  auto bounds = getLocalBounds().toFloat().reduced(10.0f);
  const float x = juce::jlimit(bounds.getX(), bounds.getRight(), pos.x);
  const float y = juce::jlimit(bounds.getY(), bounds.getBottom(), pos.y);

  const float newF2 = juce::jmap(x, bounds.getX(), bounds.getRight(), f2MinHz, f2MaxHz);
  const float newF1 = juce::jmap(y, bounds.getBottom(), bounds.getY(), f1MinHz, f1MaxHz);

  if (auto *f1Param = processor.getAPVTS().getParameter("FORMANT_1"))
    f1Param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, f1Param->convertTo0to1(newF1)));

  if (auto *f2Param = processor.getAPVTS().getParameter("FORMANT_2"))
    f2Param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, f2Param->convertTo0to1(newF2)));

  repaint();
}

SpectralFormantMorpherAudioProcessorEditor::SpectralFormantMorpherAudioProcessorEditor(SpectralFormantMorpherAudioProcessor &p)
    : AudioProcessorEditor(&p),
      audioProcessor(p),
      visualizer(p),
      xyPad(p)
{
  addAndMakeVisible(visualizer);
  addAndMakeVisible(xyPad);

  formantAttachments.reserve(dsp::SpectralProcessor::numFormants - 2);

  for (size_t i = 0; i < formantSliders.size(); ++i)
  {
    auto &slider = formantSliders[i];
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    slider.setTextValueSuffix(" Hz");
    slider.setColour(juce::Slider::trackColourId, juce::Colour(0xff61d6d6));
    slider.setColour(juce::Slider::thumbColourId, juce::Colour(0xff7ee082));
    slider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff2a2f33));
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff101214));
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha(0.9f));
    addAndMakeVisible(slider);

    auto &label = formantLabels[i];
    label.setText("F" + juce::String((int)i + 3), juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.82f));
    addAndMakeVisible(label);

    formantAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), formantParamId(i + 2), slider));
  }

  // Mix slider
  mixSlider.setSliderStyle(juce::Slider::Rotary);
  mixSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 20);
  mixSlider.setTextValueSuffix(" %");
  mixSlider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff61d6d6));
  mixSlider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff2a2f33));
  mixSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xff7ee082));
  mixSlider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff101214));
  mixSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha(0.9f));
  addAndMakeVisible(mixSlider);
  mixLabel.setText("Mix", juce::dontSendNotification);
  mixLabel.setJustificationType(juce::Justification::centred);
  mixLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.82f));
  addAndMakeVisible(mixLabel);
  mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
      audioProcessor.getAPVTS(), "MIX", mixSlider);

  // Output Gain slider
  gainSlider.setSliderStyle(juce::Slider::Rotary);
  gainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 20);
  gainSlider.setTextValueSuffix(" dB");
  gainSlider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xfff4c95d));
  gainSlider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff2a2f33));
  gainSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xff7ee082));
  gainSlider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff101214));
  gainSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha(0.9f));
  addAndMakeVisible(gainSlider);
  gainLabel.setText("Gain", juce::dontSendNotification);
  gainLabel.setJustificationType(juce::Justification::centred);
  gainLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.82f));
  addAndMakeVisible(gainLabel);
  gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
      audioProcessor.getAPVTS(), "OUTPUT_GAIN", gainSlider);

  configureUtilityButton(loadSourceButton);
  configureUtilityButton(copyProfileButton);
  configureUtilityButton(pasteProfileButton);
  configureUtilityButton(exportProfileButton);
  configureUtilityButton(importProfileButton);

  statusLabel.setText("参照音源: 未読込", juce::dontSendNotification);
  statusLabel.setJustificationType(juce::Justification::centredLeft);
  statusLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.78f));
  addAndMakeVisible(statusLabel);

  setSize(1080, 680);
  setResizable(true, true);
  setResizeLimits(920, 560, 1440, 920);
}

SpectralFormantMorpherAudioProcessorEditor::~SpectralFormantMorpherAudioProcessorEditor()
{
  loadSourceButton.removeListener(this);
  copyProfileButton.removeListener(this);
  pasteProfileButton.removeListener(this);
  exportProfileButton.removeListener(this);
  importProfileButton.removeListener(this);
}

void SpectralFormantMorpherAudioProcessorEditor::paint(juce::Graphics &g)
{
  g.fillAll(juce::Colour(0xff0d0f10));

  auto area = getLocalBounds().reduced(10);
  g.setColour(juce::Colour(0xff171b1e));
  g.fillRoundedRectangle(area.toFloat(), 8.0f);
}

void SpectralFormantMorpherAudioProcessorEditor::resized()
{
  auto area = getLocalBounds().reduced(18);

  auto top = area.removeFromTop(38);
  loadSourceButton.setBounds(top.removeFromLeft(180));
  top.removeFromLeft(8);
  copyProfileButton.setBounds(top.removeFromLeft(64));
  top.removeFromLeft(6);
  pasteProfileButton.setBounds(top.removeFromLeft(64));
  top.removeFromLeft(6);
  exportProfileButton.setBounds(top.removeFromLeft(74));
  top.removeFromLeft(6);
  importProfileButton.setBounds(top.removeFromLeft(74));
  top.removeFromLeft(8);
  statusLabel.setBounds(top.reduced(8, 0));

  area.removeFromTop(12);

  const int sliderHeight = juce::jlimit(190, 260, area.getHeight() / 3);
  auto sliderArea = area.removeFromBottom(sliderHeight).reduced(4, 0);
  area.removeFromBottom(12);

  auto mid = area;

  // Left column: XY pad + Mix/Gain knobs
  auto left = mid.removeFromLeft(300);
  xyPad.setBounds(left.removeFromTop(juce::jmin(220, left.getHeight() - 118)));

  auto knobArea = left.removeFromTop(110);
  auto mixArea = knobArea.removeFromLeft(knobArea.getWidth() / 2);
  auto gainArea = knobArea;

  mixLabel.setBounds(mixArea.removeFromTop(18));
  mixSlider.setBounds(mixArea.reduced(8));

  gainLabel.setBounds(gainArea.removeFromTop(18));
  gainSlider.setBounds(gainArea.reduced(8));

  // Right: spectrum visualizer
  visualizer.setBounds(mid);

  // Bottom: F3~F15 sliders
  const int colWidth = sliderArea.getWidth() / (int)formantSliders.size();

  for (size_t i = 0; i < formantSliders.size(); ++i)
  {
    auto col = sliderArea.removeFromLeft(colWidth);
    formantLabels[i].setBounds(col.removeFromTop(20));
    formantSliders[i].setBounds(col.reduced(4));
  }
}

void SpectralFormantMorpherAudioProcessorEditor::buttonClicked(juce::Button *button)
{
  if (button == &copyProfileButton)
  {
    copyVoiceProfileToClipboard();
    return;
  }

  if (button == &pasteProfileButton)
  {
    pasteVoiceProfileFromClipboard();
    return;
  }

  if (button == &exportProfileButton)
  {
    exportVoiceProfile();
    return;
  }

  if (button == &importProfileButton)
  {
    importVoiceProfile();
    return;
  }

  if (button != &loadSourceButton)
    return;

  sourceFileChooser = std::make_unique<juce::FileChooser>(
      "参照音源を選択",
      juce::File(),
      "*.wav;*.aif;*.aiff;*.flac;*.mp3");

  constexpr int chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
  sourceFileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser &chooser)
                                 {
        const auto file = chooser.getResult();
        if (!file.existsAsFile())
            return;

        juce::String message;
        const bool ok = audioProcessor.analyzeSourceFileAndApplyFormants(file, message);

        setStatusMessage(message, ok);
        xyPad.repaint(); });
}

void SpectralFormantMorpherAudioProcessorEditor::copyVoiceProfileToClipboard()
{
  juce::SystemClipboard::copyTextToClipboard(audioProcessor.createVoiceProfileJson());
  setStatusMessage("Voice Profileをクリップボードにコピーしました。", true);
}

void SpectralFormantMorpherAudioProcessorEditor::pasteVoiceProfileFromClipboard()
{
  juce::String message;
  const bool ok = audioProcessor.applyVoiceProfileJson(juce::SystemClipboard::getTextFromClipboard(), message);
  setStatusMessage(message, ok);
  xyPad.repaint();
}

void SpectralFormantMorpherAudioProcessorEditor::exportVoiceProfile()
{
  profileFileChooser = std::make_unique<juce::FileChooser>(
      "Voice Profileを書き出し",
      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("SpectralFormantMorpher.sfmprofile"),
      "*.sfmprofile;*.json");

  constexpr int chooserFlags = juce::FileBrowserComponent::saveMode |
                               juce::FileBrowserComponent::canSelectFiles |
                               juce::FileBrowserComponent::warnAboutOverwriting;

  profileFileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser &chooser)
                                  {
        auto file = chooser.getResult();
        if (file == juce::File())
            return;

        if (!file.hasFileExtension(".sfmprofile") && !file.hasFileExtension(".json"))
            file = file.withFileExtension(".sfmprofile");

        const bool ok = file.replaceWithText(audioProcessor.createVoiceProfileJson());
        setStatusMessage(ok ? "Voice Profileを書き出しました。" : "Voice Profileの書き出しに失敗しました。", ok); });
}

void SpectralFormantMorpherAudioProcessorEditor::importVoiceProfile()
{
  profileFileChooser = std::make_unique<juce::FileChooser>(
      "Voice Profileを読み込み",
      juce::File(),
      "*.sfmprofile;*.json");

  constexpr int chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
  profileFileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser &chooser)
                                  {
        const auto file = chooser.getResult();
        if (!file.existsAsFile())
            return;

        juce::String message;
        const bool ok = audioProcessor.applyVoiceProfileJson(file.loadFileAsString(), message);
        setStatusMessage(message, ok);
        xyPad.repaint(); });
}
