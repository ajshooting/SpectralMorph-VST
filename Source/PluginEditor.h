#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <array>
#include <vector>

#include "PluginProcessor.h"

class SpectrumVisualizer : public juce::Component, public juce::Timer
{
public:
  explicit SpectrumVisualizer(SpectralFormantMorpherAudioProcessor &p)
      : processor(p)
  {
    startTimerHz(60);
  }

  ~SpectrumVisualizer() override
  {
    stopTimer();
  }

  void paint(juce::Graphics &g) override
  {
    g.fillAll(juce::Colour(0xff101214));

    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff2a2f33));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

    if (lastSpectrum.empty() || lastEnvelope.empty())
      return;

    const float width = bounds.getWidth();
    const float height = bounds.getHeight();

    g.setColour(juce::Colour(0xff22272b));
    for (int i = 1; i < 4; ++i)
    {
      const float y = height * (float)i / 4.0f;
      g.drawHorizontalLine((int)y, 0.0f, width);
    }

    g.setColour(juce::Colour(0xff384149));
    drawPath(g, lastSpectrum, width, height, true);

    g.setColour(juce::Colour(0xff61d6d6));
    drawPath(g, lastEnvelope, width, height, false);

    const float binWidth = width / (float)lastEnvelope.size();

    if (lastF1 > 0.0f)
      drawNode(g, lastF1 * binWidth, "F1", height);

    if (lastF2 > 0.0f)
      drawNode(g, lastF2 * binWidth, "F2", height);
  }

  void timerCallback() override
  {
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
    const float db = juce::jlimit(-100.0f, 0.0f, juce::Decibels::gainToDecibels(std::max(mag, 1.0e-9f)));
    return juce::jmap(db, -100.0f, 0.0f, height, 0.0f);
  }

  static void drawPath(juce::Graphics &g, const std::vector<float> &data, float width, float height, bool fill)
  {
    if (data.empty())
      return;

    juce::Path path;

    const int numBins = (int)data.size();
    const float firstY = magToY(data.front(), height);
    path.startNewSubPath(0.0f, fill ? height : firstY);
    if (fill)
      path.lineTo(0.0f, firstY);

    for (int i = 0; i < numBins; ++i)
    {
      const float x = (numBins > 1) ? (float)i / (float)(numBins - 1) * width : 0.0f;
      const float y = magToY(data[(size_t)i], height);
      path.lineTo(x, y);
    }

    if (fill)
    {
      path.lineTo(width, height);
      path.closeSubPath();
      g.fillPath(path);
    }
    else
    {
      g.strokePath(path, juce::PathStrokeType(2.0f));
    }
  }

  static void drawNode(juce::Graphics &g, float x, const juce::String &label, float height)
  {
    const float y = height * 0.15f;
    const float radius = 7.0f;
    const juce::Rectangle<float> area(x - radius, y - radius, radius * 2.0f, radius * 2.0f);

    g.setColour(juce::Colour(0xfff4c95d));
    g.fillEllipse(area);
    g.setColour(juce::Colour(0xff101214));
    g.drawEllipse(area, 2.0f);

    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.drawText(label, (int)x + 8, (int)y - 10, 28, 20, juce::Justification::left);
  }
};

class XYFormantPad : public juce::Component, private juce::Timer
{
public:
  explicit XYFormantPad(SpectralFormantMorpherAudioProcessor &p)
      : processor(p)
  {
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    startTimerHz(30);
  }

  ~XYFormantPad() override
  {
    stopTimer();
  }

  void paint(juce::Graphics &g) override;
  void mouseDown(const juce::MouseEvent &event) override;
  void mouseDrag(const juce::MouseEvent &event) override;
  void mouseUp(const juce::MouseEvent &event) override;

private:
  SpectralFormantMorpherAudioProcessor &processor;
  bool dragging = false;

  void timerCallback() override;
  void beginGesture();
  void endGesture();
  void updateFromPosition(juce::Point<float> pos);
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

  SpectrumVisualizer visualizer;
  XYFormantPad xyPad;

  std::array<juce::Slider, dsp::SpectralProcessor::numFormants - 2> formantSliders;
  std::array<juce::Label, dsp::SpectralProcessor::numFormants - 2> formantLabels;
  std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> formantAttachments;

  juce::TextButton loadSourceButton{"参照音源を解析"};
  juce::Label statusLabel;
  std::unique_ptr<juce::FileChooser> sourceFileChooser;

  // Mix & Output Gain controls
  juce::Slider mixSlider;
  juce::Label mixLabel;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;

  juce::Slider gainSlider;
  juce::Label gainLabel;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;

  void buttonClicked(juce::Button *button) override;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralFormantMorpherAudioProcessorEditor)
};
