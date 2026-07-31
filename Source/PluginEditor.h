#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "PluginProcessor.h"

class SpectralMorpherLookAndFeel : public juce::LookAndFeel_V4
{
public:
  SpectralMorpherLookAndFeel();

  void drawButtonBackground(juce::Graphics &,
                            juce::Button &,
                            const juce::Colour &backgroundColour,
                            bool isMouseOverButton,
                            bool isButtonDown) override;
  void drawButtonText(juce::Graphics &,
                      juce::TextButton &,
                      bool isMouseOverButton,
                      bool isButtonDown) override;
  void drawRotarySlider(juce::Graphics &,
                        int x,
                        int y,
                        int width,
                        int height,
                        float sliderPosProportional,
                        float rotaryStartAngle,
                        float rotaryEndAngle,
                        juce::Slider &) override;
  void drawLinearSlider(juce::Graphics &,
                        int x,
                        int y,
                        int width,
                        int height,
                        float sliderPos,
                        float minSliderPos,
                        float maxSliderPos,
                        juce::Slider::SliderStyle,
                        juce::Slider &) override;
  juce::Font getTextButtonFont(juce::TextButton &, int buttonHeight) override;
};

class SpectrumVisualizer : public juce::Component, public juce::Timer
{
public:
  explicit SpectrumVisualizer(SpectralFormantMorpherAudioProcessor &p)
      : processor(p)
  {
    setTitle("Live spectrum analyzer");
    setDescription("Input spectrum and the warped spectral envelope");
    startTimerHz(30);
  }

  ~SpectrumVisualizer() override
  {
    stopTimer();
  }

  void paint(juce::Graphics &g) override;

  void timerCallback() override
  {
    if (!isShowing())
      return;

    processor.getSpectralProcessor().getLatestVisualizationData(lastSpectrum, lastEnvelope, lastF1, lastF2);
    repaint();
  }

private:
  SpectralFormantMorpherAudioProcessor &processor;
  std::vector<float> lastSpectrum;
  std::vector<float> lastEnvelope;
  float lastF1 = 0.0f;
  float lastF2 = 0.0f;

  static float magToY(float mag, float height)
  {
    const float db = juce::jlimit(-72.0f, 0.0f, juce::Decibels::gainToDecibels(std::max(mag, 1.0e-9f)));
    return juce::jmap(db, -72.0f, 0.0f, height, 0.0f);
  }

  static float frequencyToX(float frequency, float minFrequency, float maxFrequency, float width);
  static void drawPath(juce::Graphics &g,
                       const std::vector<float> &data,
                       double sampleRate,
                       float minFrequency,
                       float maxFrequency,
                       juce::Rectangle<float> graph,
                       bool fill);
  static void drawNode(juce::Graphics &g, float x, const juce::String &label, juce::Rectangle<float> graph);
};

class XYFormantPad : public juce::Component, private juce::Timer
{
public:
  explicit XYFormantPad(SpectralFormantMorpherAudioProcessor &p);
  ~XYFormantPad() override;

  void paint(juce::Graphics &g) override;
  void mouseDown(const juce::MouseEvent &event) override;
  void mouseDrag(const juce::MouseEvent &event) override;
  void mouseUp(const juce::MouseEvent &event) override;
  bool keyPressed(const juce::KeyPress &key) override;
  void focusGained(juce::Component::FocusChangeType) override { repaint(); }
  void focusLost(juce::Component::FocusChangeType) override { repaint(); }
  std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
  SpectralFormantMorpherAudioProcessor &processor;
  bool dragging = false;
  float lastF1 = -1.0f;
  float lastF2 = -1.0f;

  void timerCallback() override;
  void beginGesture();
  void endGesture();
  void updateFromPosition(juce::Point<float> pos);
  void nudgeParameter(const juce::String &parameterID, float delta);
};

class FormantTargetSlider : public juce::Slider
{
public:
  std::function<void()> onKeyboardEdit;

  bool keyPressed(const juce::KeyPress &key) override
  {
    const bool handled = juce::Slider::keyPressed(key);
    if (handled && onKeyboardEdit)
      onKeyboardEdit();

    return handled;
  }
};

class SpectralFormantMorpherAudioProcessorEditor : public juce::AudioProcessorEditor,
                                                   private juce::Button::Listener
{
public:
  explicit SpectralFormantMorpherAudioProcessorEditor(SpectralFormantMorpherAudioProcessor &);
  ~SpectralFormantMorpherAudioProcessorEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;

private:
  SpectralFormantMorpherAudioProcessor &audioProcessor;
  SpectralMorpherLookAndFeel lookAndFeel;

  SpectrumVisualizer visualizer;
  XYFormantPad xyPad;

  std::array<FormantTargetSlider, dsp::SpectralProcessor::numFormants - 2> formantSliders;
  std::array<juce::Label, dsp::SpectralProcessor::numFormants - 2> formantLabels;
  std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> formantAttachments;

  juce::TextButton loadSourceButton{"Analyze reference"};
  juce::TextButton copyProfileButton{"Copy"};
  juce::TextButton pasteProfileButton{"Paste"};
  juce::TextButton exportProfileButton{"Export"};
  juce::TextButton importProfileButton{"Import"};
  juce::Label referenceStatusLabel;
  juce::Label statusLabel;
  std::unique_ptr<juce::FileChooser> sourceFileChooser;
  std::unique_ptr<juce::FileChooser> profileFileChooser;
  struct ReferenceAnalysisState;
  std::shared_ptr<ReferenceAnalysisState> referenceAnalysisState;

  // Mix & Output Gain controls
  juce::Slider mixSlider;
  juce::Label mixLabel;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;

  juce::Slider gainSlider;
  juce::Label gainLabel;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
  juce::TooltipWindow tooltipWindow{this, 600};

  void buttonClicked(juce::Button *button) override;
  void configureUtilityButton(juce::TextButton &button, const juce::String &tooltip);
  void setStatusMessage(const juce::String &message, bool ok);
  void copyVoiceProfileToClipboard();
  void pasteVoiceProfileFromClipboard();
  void exportVoiceProfile();
  void importVoiceProfile();

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralFormantMorpherAudioProcessorEditor)
};
