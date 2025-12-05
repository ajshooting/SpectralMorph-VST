#include "PluginProcessor.h"
#include "PluginEditor.h"

SpectralFormantMorpherAudioProcessorEditor::SpectralFormantMorpherAudioProcessorEditor (SpectralFormantMorpherAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), visualizer(p)
{
    addAndMakeVisible(visualizer);

    f1ShiftSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    f1ShiftSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    addAndMakeVisible(f1ShiftSlider);

    f2ShiftSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    f2ShiftSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    addAndMakeVisible(f2ShiftSlider);

    scaleSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    scaleSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    addAndMakeVisible(scaleSlider);

    f1Attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "F1_SHIFT", f1ShiftSlider);

    f2Attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "F2_SHIFT", f2ShiftSlider);

    scaleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), "OVERALL_SCALE", scaleSlider);

    setSize (600, 400);
}

SpectralFormantMorpherAudioProcessorEditor::~SpectralFormantMorpherAudioProcessorEditor()
{
}

void SpectralFormantMorpherAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void SpectralFormantMorpherAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    auto bottomArea = area.removeFromBottom(100);
    f1ShiftSlider.setBounds(bottomArea.removeFromLeft(100));
    f2ShiftSlider.setBounds(bottomArea.removeFromLeft(100));
    scaleSlider.setBounds(bottomArea.removeFromLeft(100));

    visualizer.setBounds(area);
}
