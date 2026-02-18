#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
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
    g.fillAll(juce::Colours::black);

    if (lastSpectrum.empty() || lastEnvelope.empty())
      return;

    const auto bounds = getLocalBounds();
    const float width = (float)bounds.getWidth();
    const float height = (float)bounds.getHeight();

    g.setColour(juce::Colours::darkgrey.withAlpha(0.5f));
    drawPath(g, lastSpectrum, width, height, true);

    g.setColour(juce::Colours::cyan);
    drawPath(g, lastEnvelope, width, height, false);

    const float binWidth = width / (float)lastEnvelope.size();

    drawNode(g, lastF1 * binWidth, "F1", height);
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
    const float db = juce::Decibels::gainToDecibels(mag);
    return juce::jmap(db, -100.0f, 0.0f, height, 0.0f);
  }

  static void drawPath(juce::Graphics &g, const std::vector<float> &data, float width, float height, bool fill)
  {
    if (data.empty())
      return;

    juce::Path path;
    path.startNewSubPath(0, height);

    const int numBins = (int)data.size();
    for (int i = 0; i < numBins; ++i)
    {
      const float x = (float)i / (float)numBins * width;
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

    g.setColour(juce::Colours::yellow);
    g.fillEllipse(area);
    g.setColour(juce::Colours::black);
    g.drawEllipse(area, 2.0f);

    g.setColour(juce::Colours::white);
    g.drawText(label, (int)x + 8, (int)y - 10, 28, 20, juce::Justification::left);
  }
};

class XYFormantPad : public juce::Component
{
public:
  explicit XYFormantPad(SpectralFormantMorpherAudioProcessor &p)
      : processor(p)
  {
  }

  void paint(juce::Graphics &g) override;
  void mouseDown(const juce::MouseEvent &event) override;
  void mouseDrag(const juce::MouseEvent &event) override;

private:
  SpectralFormantMorpherAudioProcessor &processor;

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

  juce::TextButton loadSourceButton{"ソース音源を読み込む"};
  juce::Label statusLabel;
  std::unique_ptr<juce::FileChooser> sourceFileChooser;

  void buttonClicked(juce::Button *button) override;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralFormantMorpherAudioProcessorEditor)
};
