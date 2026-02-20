#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
  juce::String formantParamId(size_t index)
  {
    return "FORMANT_" + juce::String((int)index + 1);
  }
}

void XYFormantPad::paint(juce::Graphics &g)
{
  auto bounds = getLocalBounds().toFloat().reduced(10.0f);
  g.setColour(juce::Colours::darkgrey.withAlpha(0.5f));
  g.fillRoundedRectangle(bounds, 8.0f);
  g.setColour(juce::Colours::grey);
  g.drawRoundedRectangle(bounds, 8.0f, 1.0f);

  const float f1 = processor.getAPVTS().getRawParameterValue("FORMANT_1")->load();
  const float f2 = processor.getAPVTS().getRawParameterValue("FORMANT_2")->load();

  const float x = juce::jmap(f2, 800.0f, 3500.0f, bounds.getX(), bounds.getRight());
  const float y = juce::jmap(f1, 1000.0f, 200.0f, bounds.getY(), bounds.getBottom());

  g.setColour(juce::Colours::lightgreen);
  g.fillEllipse(x - 8.0f, y - 8.0f, 16.0f, 16.0f);

  g.setColour(juce::Colours::white);
  g.drawText("F2", (int)bounds.getX(), (int)bounds.getBottom() + 2, (int)bounds.getWidth(), 18, juce::Justification::centred);
  g.drawText("F1", (int)bounds.getX() - 24, (int)bounds.getY(), 22, (int)bounds.getHeight(), juce::Justification::centred);
}

void XYFormantPad::mouseDown(const juce::MouseEvent &event)
{
  updateFromPosition(event.position);
}

void XYFormantPad::mouseDrag(const juce::MouseEvent &event)
{
  updateFromPosition(event.position);
}

void XYFormantPad::updateFromPosition(juce::Point<float> pos)
{
  auto bounds = getLocalBounds().toFloat().reduced(10.0f);
  const float x = juce::jlimit(bounds.getX(), bounds.getRight(), pos.x);
  const float y = juce::jlimit(bounds.getY(), bounds.getBottom(), pos.y);

  const float newF2 = juce::jmap(x, bounds.getX(), bounds.getRight(), 800.0f, 3500.0f);
  const float newF1 = juce::jmap(y, bounds.getBottom(), bounds.getY(), 200.0f, 1000.0f);

  if (auto *f1Param = processor.getAPVTS().getParameter("FORMANT_1"))
    f1Param->setValueNotifyingHost(f1Param->convertTo0to1(newF1));

  if (auto *f2Param = processor.getAPVTS().getParameter("FORMANT_2"))
    f2Param->setValueNotifyingHost(f2Param->convertTo0to1(newF2));

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
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 54, 16);
    addAndMakeVisible(slider);

    auto &label = formantLabels[i];
    label.setText("F" + juce::String((int)i + 3), juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);

    formantAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), formantParamId(i + 2), slider));
  }

  // Mix slider
  mixSlider.setSliderStyle(juce::Slider::Rotary);
  mixSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
  mixSlider.setTextValueSuffix(" %");
  addAndMakeVisible(mixSlider);
  mixLabel.setText("Mix", juce::dontSendNotification);
  mixLabel.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(mixLabel);
  mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
      audioProcessor.getAPVTS(), "MIX", mixSlider);

  // Output Gain slider
  gainSlider.setSliderStyle(juce::Slider::Rotary);
  gainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
  gainSlider.setTextValueSuffix(" dB");
  addAndMakeVisible(gainSlider);
  gainLabel.setText("Gain", juce::dontSendNotification);
  gainLabel.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(gainLabel);
  gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
      audioProcessor.getAPVTS(), "OUTPUT_GAIN", gainSlider);

  loadSourceButton.addListener(this);
  addAndMakeVisible(loadSourceButton);

  statusLabel.setText("ソース音源を読み込むとF1〜F15を自動設定します", juce::dontSendNotification);
  statusLabel.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(statusLabel);

  setSize(1080, 680);
}

SpectralFormantMorpherAudioProcessorEditor::~SpectralFormantMorpherAudioProcessorEditor()
{
  loadSourceButton.removeListener(this);
}

void SpectralFormantMorpherAudioProcessorEditor::paint(juce::Graphics &g)
{
  g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void SpectralFormantMorpherAudioProcessorEditor::resized()
{
  auto area = getLocalBounds().reduced(10);

  auto top = area.removeFromTop(34);
  loadSourceButton.setBounds(top.removeFromLeft(220));
  statusLabel.setBounds(top.reduced(8, 0));

  auto mid = area.removeFromTop(320);

  // Left column: XY pad + Mix/Gain knobs
  auto left = mid.removeFromLeft(300);
  xyPad.setBounds(left.removeFromTop(200));

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
  auto sliderArea = area.reduced(4);
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
  if (button != &loadSourceButton)
    return;

  sourceFileChooser = std::make_unique<juce::FileChooser>(
      "ソース音源を選択",
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

        statusLabel.setText(message, juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, ok ? juce::Colours::lightgreen : juce::Colours::orange);
        xyPad.repaint(); });
}
