#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <atomic>
#include <cmath>

namespace
{
  constexpr float f1MinHz = 200.0f;
  constexpr float f1MaxHz = 1000.0f;
  constexpr float f2MinHz = 800.0f;
  constexpr float f2MaxHz = 3500.0f;
  constexpr std::array<float, dsp::SpectralProcessor::numFormants> defaultFormantsHz{
      500.0f, 1500.0f, 2500.0f, 3200.0f, 3800.0f,
      4400.0f, 5000.0f, 5600.0f, 6200.0f, 6800.0f,
      7400.0f, 8000.0f, 8600.0f, 9200.0f, 9800.0f};

  const juce::Colour backgroundTop{0xff080d13};
  const juce::Colour backgroundBottom{0xff0d141d};
  const juce::Colour panelColour{0xff111a24};
  const juce::Colour panelInset{0xff0b121a};
  const juce::Colour borderColour{0xff243247};
  const juce::Colour accentColour{0xff4bdacb};
  const juce::Colour accentSecondary{0xff8d82ff};
  const juce::Colour warmAccent{0xffffb45e};
  const juce::Colour primaryText{0xfff2f6fa};
  const juce::Colour secondaryText{0xff8f9fb0};
  const juce::Colour successColour{0xff6fe0a0};

  juce::String formantParamId(size_t index)
  {
    return "FORMANT_" + juce::String((int)index + 1);
  }

  juce::ThreadPool &getReferenceAnalysisPool()
  {
    static juce::ThreadPool pool{
        juce::ThreadPool::Options{}
            .withNumberOfThreads(1)
            .withThreadName("Spectral Formant reference analysis")};
    return pool;
  }

  float readParamValue(SpectralFormantMorpherAudioProcessor &processor,
                       const juce::String &id,
                       float fallback)
  {
    if (const auto *value = processor.getAPVTS().getRawParameterValue(id))
      return value->load(std::memory_order_relaxed);

    return fallback;
  }

  struct EditorLayout
  {
    juce::Rectangle<int> header;
    juce::Rectangle<int> utility;
    juce::Rectangle<int> target;
    juce::Rectangle<int> analyzer;
    juce::Rectangle<int> higherFormants;
  };

  EditorLayout createEditorLayout(juce::Rectangle<int> bounds)
  {
    auto content = bounds.reduced(18);

    EditorLayout layout;
    layout.header = content.removeFromTop(64);
    content.removeFromTop(8);
    layout.utility = content.removeFromTop(58);
    content.removeFromTop(12);

    const int higherHeight = juce::jlimit(200, 250, bounds.getHeight() / 3);
    layout.higherFormants = content.removeFromBottom(higherHeight);
    content.removeFromBottom(12);

    const int targetWidth = juce::jlimit(350, 430, (int)std::round((float)content.getWidth() * 0.38f));
    layout.target = content.removeFromLeft(targetWidth);
    content.removeFromLeft(12);
    layout.analyzer = content;
    return layout;
  }

  void drawCard(juce::Graphics &g, juce::Rectangle<int> bounds)
  {
    const auto card = bounds.toFloat();
    g.setColour(juce::Colours::black.withAlpha(0.18f));
    g.fillRoundedRectangle(card.translated(0.0f, 3.0f), 12.0f);
    g.setColour(panelColour);
    g.fillRoundedRectangle(card, 12.0f);
    g.setColour(borderColour.withAlpha(0.75f));
    g.drawRoundedRectangle(card.reduced(0.5f), 12.0f, 1.0f);
  }

  void drawSectionTitle(juce::Graphics &g,
                        juce::Rectangle<int> card,
                        const juce::String &title,
                        const juce::String &detail)
  {
    auto titleArea = card.reduced(14, 0).removeFromTop(34);
    g.setFont(juce::Font(juce::FontOptions{12.0f, juce::Font::bold}));
    g.setColour(primaryText.withAlpha(0.94f));
    g.drawText(title, titleArea.removeFromLeft(180), juce::Justification::centredLeft);
    g.setFont(juce::Font(juce::FontOptions{11.0f}));
    g.setColour(secondaryText);
    g.drawText(detail, titleArea, juce::Justification::centredRight);
  }
}

struct SpectralFormantMorpherAudioProcessorEditor::ReferenceAnalysisState
{
  std::atomic<bool> cancelled{false};

  // Created, read, and cleared only on the message thread. The worker touches
  // only the atomic cancellation flag.
  SpectralFormantMorpherAudioProcessorEditor *editor = nullptr;
};

SpectralMorpherLookAndFeel::SpectralMorpherLookAndFeel()
{
  setColour(juce::Label::textColourId, primaryText);
  setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a2633));
  setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff26384a));
  setColour(juce::TextButton::textColourOffId, primaryText);
  setColour(juce::TextButton::textColourOnId, primaryText);
  setColour(juce::Slider::textBoxBackgroundColourId, panelInset);
  setColour(juce::Slider::textBoxTextColourId, primaryText);
  setColour(juce::Slider::textBoxOutlineColourId, borderColour);
  setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff263448));
  setColour(juce::Slider::rotarySliderFillColourId, accentColour);
  setColour(juce::Slider::trackColourId, accentColour);
  setColour(juce::Slider::thumbColourId, primaryText);
  setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xff17212c));
  setColour(juce::TooltipWindow::textColourId, primaryText);
  setColour(juce::TooltipWindow::outlineColourId, borderColour);
}

void SpectralMorpherLookAndFeel::drawButtonBackground(juce::Graphics &g,
                                                      juce::Button &button,
                                                      const juce::Colour &backgroundColour,
                                                      bool isMouseOverButton,
                                                      bool isButtonDown)
{
  auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
  auto colour = backgroundColour;

  if (!button.isEnabled())
    colour = colour.withMultipliedAlpha(0.45f);
  else if (isButtonDown)
    colour = colour.darker(0.18f);
  else if (isMouseOverButton)
    colour = colour.brighter(0.10f);

  g.setColour(juce::Colours::black.withAlpha(0.16f));
  g.fillRoundedRectangle(bounds.translated(0.0f, 2.0f), 8.0f);
  g.setColour(colour);
  g.fillRoundedRectangle(bounds, 8.0f);
  g.setColour((button.hasKeyboardFocus(true) ? accentColour : borderColour).withAlpha(0.9f));
  g.drawRoundedRectangle(bounds, 8.0f, button.hasKeyboardFocus(true) ? 1.6f : 1.0f);
}

void SpectralMorpherLookAndFeel::drawButtonText(juce::Graphics &g,
                                                juce::TextButton &button,
                                                bool,
                                                bool)
{
  g.setFont(getTextButtonFont(button, button.getHeight()));
  g.setColour(button.findColour(button.getToggleState()
                                    ? juce::TextButton::textColourOnId
                                    : juce::TextButton::textColourOffId)
                  .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.45f));
  g.drawFittedText(button.getButtonText(),
                   button.getLocalBounds().reduced(8, 2),
                   juce::Justification::centred,
                   1);
}

void SpectralMorpherLookAndFeel::drawRotarySlider(juce::Graphics &g,
                                                  int x,
                                                  int y,
                                                  int width,
                                                  int height,
                                                  float sliderPosProportional,
                                                  float rotaryStartAngle,
                                                  float rotaryEndAngle,
                                                  juce::Slider &slider)
{
  const float radius = (float)juce::jmin(width, height) * 0.34f;
  const auto centre = juce::Point<float>((float)x + (float)width * 0.5f,
                                         (float)y + (float)height * 0.48f);
  const auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
  const auto arcBounds = juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre).reduced(4.0f);

  juce::Path backgroundArc;
  backgroundArc.addCentredArc(centre.x,
                              centre.y,
                              arcBounds.getWidth() * 0.5f,
                              arcBounds.getHeight() * 0.5f,
                              0.0f,
                              rotaryStartAngle,
                              rotaryEndAngle,
                              true);
  g.setColour(slider.findColour(juce::Slider::rotarySliderOutlineColourId));
  g.strokePath(backgroundArc, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

  juce::Path valueArc;
  valueArc.addCentredArc(centre.x,
                         centre.y,
                         arcBounds.getWidth() * 0.5f,
                         arcBounds.getHeight() * 0.5f,
                         0.0f,
                         rotaryStartAngle,
                         angle,
                         true);
  g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId));
  g.strokePath(valueArc, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

  const float knobRadius = radius * 0.50f;
  g.setColour(panelInset);
  g.fillEllipse(juce::Rectangle<float>(knobRadius * 2.0f, knobRadius * 2.0f).withCentre(centre));

  const auto pointer = centre.getPointOnCircumference(knobRadius * 0.74f, angle);
  g.setColour(primaryText.withAlpha(0.92f));
  g.drawLine(centre.x, centre.y, pointer.x, pointer.y, 2.0f);

  if (slider.hasKeyboardFocus(true))
  {
    g.setColour(accentColour.withAlpha(0.8f));
    g.drawEllipse(arcBounds.expanded(5.0f), 1.5f);
  }
}

void SpectralMorpherLookAndFeel::drawLinearSlider(juce::Graphics &g,
                                                  int x,
                                                  int y,
                                                  int width,
                                                  int height,
                                                  float sliderPos,
                                                  float minSliderPos,
                                                  float maxSliderPos,
                                                  juce::Slider::SliderStyle style,
                                                  juce::Slider &slider)
{
  if (style != juce::Slider::LinearVertical)
  {
    juce::LookAndFeel_V4::drawLinearSlider(
        g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
    return;
  }

  const float centreX = (float)x + (float)width * 0.5f;
  const float trackWidth = 5.0f;
  const auto track = juce::Rectangle<float>(
      centreX - trackWidth * 0.5f,
      (float)y,
      trackWidth,
      (float)height);

  g.setColour(juce::Colour(0xff283548));
  g.fillRoundedRectangle(track, trackWidth * 0.5f);

  auto activeTrack = track.withTop(juce::jlimit((float)y, (float)(y + height), sliderPos));
  g.setColour(slider.findColour(juce::Slider::trackColourId));
  g.fillRoundedRectangle(activeTrack, trackWidth * 0.5f);

  const auto thumb = juce::Rectangle<float>(14.0f, 8.0f).withCentre({centreX, sliderPos});
  g.setColour(juce::Colours::black.withAlpha(0.25f));
  g.fillRoundedRectangle(thumb.translated(0.0f, 2.0f), 4.0f);
  g.setColour(slider.findColour(juce::Slider::thumbColourId));
  g.fillRoundedRectangle(thumb, 4.0f);

  if (slider.hasKeyboardFocus(true))
  {
    g.setColour(accentColour.withAlpha(0.85f));
    g.drawRoundedRectangle(thumb.expanded(3.0f), 6.0f, 1.5f);
  }
}

juce::Font SpectralMorpherLookAndFeel::getTextButtonFont(juce::TextButton &, int buttonHeight)
{
  return juce::Font(juce::FontOptions{juce::jmin(13.0f, (float)buttonHeight * 0.42f), juce::Font::bold});
}

float SpectrumVisualizer::frequencyToX(float frequency,
                                       float minFrequency,
                                       float maxFrequency,
                                       float width)
{
  const float clamped = juce::jlimit(minFrequency, maxFrequency, frequency);
  const float proportion = std::log(clamped / minFrequency) / std::log(maxFrequency / minFrequency);
  return proportion * width;
}

void SpectrumVisualizer::drawPath(juce::Graphics &g,
                                  const std::vector<float> &data,
                                  double sampleRate,
                                  float minFrequency,
                                  float maxFrequency,
                                  juce::Rectangle<float> graph,
                                  bool fill)
{
  if (data.size() < 2 || sampleRate <= 0.0)
    return;

  juce::Path path;
  bool started = false;
  const float fftSize = (float)(data.size() - 1) * 2.0f;

  for (size_t i = 1; i < data.size(); ++i)
  {
    const float frequency = (float)i * (float)sampleRate / fftSize;
    if (frequency < minFrequency)
      continue;
    if (frequency > maxFrequency)
      break;

    const float x = graph.getX() + frequencyToX(frequency, minFrequency, maxFrequency, graph.getWidth());
    const float y = graph.getY() + magToY(data[i], graph.getHeight());

    if (!started)
    {
      path.startNewSubPath(x, fill ? graph.getBottom() : y);
      if (fill)
        path.lineTo(x, y);
      started = true;
    }
    else
    {
      path.lineTo(x, y);
    }
  }

  if (!started)
    return;

  if (fill)
  {
    path.lineTo(graph.getRight(), graph.getBottom());
    path.closeSubPath();
    g.fillPath(path);
  }
  else
  {
    g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved));
  }
}

void SpectrumVisualizer::drawNode(juce::Graphics &g,
                                  float x,
                                  const juce::String &label,
                                  juce::Rectangle<float> graph)
{
  const float y = graph.getY() + 15.0f;
  const auto area = juce::Rectangle<float>(11.0f, 11.0f).withCentre({x, y});

  g.setColour(warmAccent.withAlpha(0.22f));
  g.fillEllipse(area.expanded(4.0f));
  g.setColour(warmAccent);
  g.fillEllipse(area);
  g.setFont(juce::Font(juce::FontOptions{10.0f, juce::Font::bold}));
  g.setColour(primaryText);
  g.drawText(label, (int)x + 8, (int)y - 8, 28, 16, juce::Justification::left);
}

void SpectrumVisualizer::paint(juce::Graphics &g)
{
  auto bounds = getLocalBounds().toFloat();
  g.setColour(panelInset);
  g.fillRoundedRectangle(bounds, 8.0f);

  auto graph = bounds.reduced(12.0f);
  graph.removeFromTop(18.0f);
  graph.removeFromLeft(30.0f);
  graph.removeFromBottom(18.0f);
  graph.removeFromRight(8.0f);

  const double sampleRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 44100.0;
  const float minFrequency = 80.0f;
  const float maxFrequency = juce::jmax(minFrequency + 1.0f,
                                        juce::jmin(12000.0f, (float)sampleRate * 0.5f));

  g.setFont(juce::Font(juce::FontOptions{9.5f}));
  for (const float db : {0.0f, -24.0f, -48.0f, -72.0f})
  {
    const float y = graph.getY() + juce::jmap(db, -72.0f, 0.0f, graph.getHeight(), 0.0f);
    g.setColour(borderColour.withAlpha(0.45f));
    g.drawHorizontalLine((int)std::round(y), graph.getX(), graph.getRight());
    g.setColour(secondaryText.withAlpha(0.8f));
    g.drawText(juce::String((int)db),
               0,
               (int)y - 7,
               (int)graph.getX() - 5,
               14,
               juce::Justification::centredRight);
  }

  for (const float frequency : {100.0f, 500.0f, 1000.0f, 5000.0f, 10000.0f})
  {
    if (frequency > maxFrequency)
      continue;

    const float x = graph.getX() + frequencyToX(frequency, minFrequency, maxFrequency, graph.getWidth());
    g.setColour(borderColour.withAlpha(0.38f));
    g.drawVerticalLine((int)std::round(x), graph.getY(), graph.getBottom());
    g.setColour(secondaryText.withAlpha(0.82f));
    const auto label = frequency >= 1000.0f
                           ? juce::String(frequency / 1000.0f, frequency == 1000.0f ? 0 : 0) + "k"
                           : juce::String((int)frequency);
    g.drawText(label,
               (int)x - 18,
               (int)graph.getBottom() + 3,
               36,
               13,
               juce::Justification::centred);
  }

  const bool hasSignal = lastSpectrum.size() >= 2
                      && lastEnvelope.size() >= 2
                      && *std::max_element(lastSpectrum.begin(), lastSpectrum.end()) > 1.0e-7f;

  if (hasSignal)
  {
    juce::ColourGradient spectrumGradient(
        accentSecondary.withAlpha(0.22f),
        graph.getCentreX(),
        graph.getY(),
        accentSecondary.withAlpha(0.02f),
        graph.getCentreX(),
        graph.getBottom(),
        false);
    g.setGradientFill(spectrumGradient);
    drawPath(g, lastSpectrum, sampleRate, minFrequency, maxFrequency, graph, true);

    g.setColour(accentColour);
    drawPath(g, lastEnvelope, sampleRate, minFrequency, maxFrequency, graph, false);

    const float fftSize = (float)(lastEnvelope.size() - 1) * 2.0f;
    if (lastF1 > 0.0f)
    {
      const float frequency = lastF1 * (float)sampleRate / fftSize;
      drawNode(g,
               graph.getX() + frequencyToX(frequency, minFrequency, maxFrequency, graph.getWidth()),
               "F1",
               graph);
    }

    if (lastF2 > 0.0f)
    {
      const float frequency = lastF2 * (float)sampleRate / fftSize;
      drawNode(g,
               graph.getX() + frequencyToX(frequency, minFrequency, maxFrequency, graph.getWidth()),
               "F2",
               graph);
    }
  }
  else
  {
    g.setFont(juce::Font(juce::FontOptions{13.0f, juce::Font::bold}));
    g.setColour(secondaryText.withAlpha(0.82f));
    g.drawText("Play audio to view the live spectrum",
               graph.toNearestInt(),
               juce::Justification::centred);
  }

  auto legend = getLocalBounds().reduced(12).removeFromTop(16);
  g.setFont(juce::Font(juce::FontOptions{10.0f, juce::Font::bold}));
  g.setColour(accentSecondary);
  g.fillEllipse((float)legend.getRight() - 142.0f, (float)legend.getY() + 4.0f, 7.0f, 7.0f);
  g.setColour(secondaryText);
  g.drawText("INPUT", legend.getRight() - 130, legend.getY(), 42, 15, juce::Justification::left);
  g.setColour(accentColour);
  g.fillEllipse((float)legend.getRight() - 78.0f, (float)legend.getY() + 4.0f, 7.0f, 7.0f);
  g.setColour(secondaryText);
  g.drawText("MORPHED", legend.getRight() - 66, legend.getY(), 66, 15, juce::Justification::left);

  g.setColour(borderColour.withAlpha(0.75f));
  g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);
}

XYFormantPad::XYFormantPad(SpectralFormantMorpherAudioProcessor &p)
    : processor(p)
{
  setMouseCursor(juce::MouseCursor::CrosshairCursor);
  setWantsKeyboardFocus(true);
  setTitle("F1 and F2 formant target");
  setDescription("Drag the target or use arrow keys. Up and down change F1; left and right change F2.");
  setHelpText("Arrow keys move by 10 Hz. Hold Shift to move by 50 Hz.");
  startTimerHz(20);
}

XYFormantPad::~XYFormantPad()
{
  endGesture();
  stopTimer();
}

void XYFormantPad::paint(juce::Graphics &g)
{
  auto bounds = getLocalBounds().toFloat().reduced(1.0f);
  g.setColour(panelInset);
  g.fillRoundedRectangle(bounds, 8.0f);

  auto graph = bounds.reduced(12.0f);

  for (int i = 1; i < 4; ++i)
  {
    const float x = graph.getX() + graph.getWidth() * (float)i / 4.0f;
    const float y = graph.getY() + graph.getHeight() * (float)i / 4.0f;
    g.setColour(borderColour.withAlpha(0.46f));
    g.drawVerticalLine((int)x, graph.getY(), graph.getBottom());
    g.drawHorizontalLine((int)y, graph.getX(), graph.getRight());
  }

  const float f1 = juce::jlimit(f1MinHz, f1MaxHz, readParamValue(processor, "FORMANT_1", 500.0f));
  const float f2 = juce::jlimit(f2MinHz, f2MaxHz, readParamValue(processor, "FORMANT_2", 1500.0f));
  const float x = juce::jmap(f2, f2MinHz, f2MaxHz, graph.getX(), graph.getRight());
  const float y = juce::jmap(f1, f1MaxHz, f1MinHz, graph.getY(), graph.getBottom());

  g.setColour(accentColour.withAlpha(0.42f));
  g.drawLine(graph.getX(), y, graph.getRight(), y, 1.0f);
  g.drawLine(x, graph.getY(), x, graph.getBottom(), 1.0f);

  g.setColour(accentColour.withAlpha(0.18f));
  g.fillEllipse(x - 13.0f, y - 13.0f, 26.0f, 26.0f);
  g.setColour(accentColour);
  g.fillEllipse(x - 7.0f, y - 7.0f, 14.0f, 14.0f);
  g.setColour(primaryText.withAlpha(0.9f));
  g.drawEllipse(x - 7.0f, y - 7.0f, 14.0f, 14.0f, 1.5f);

  g.setFont(juce::Font(juce::FontOptions{11.0f, juce::Font::bold}));
  g.setColour(primaryText.withAlpha(0.90f));
  g.drawText("F1  " + juce::String((int)std::round(f1)) + " Hz",
             graph.toNearestInt().reduced(7).removeFromTop(18),
             juce::Justification::left);
  g.drawText("F2  " + juce::String((int)std::round(f2)) + " Hz",
             graph.toNearestInt().reduced(7).removeFromBottom(18),
             juce::Justification::right);

  g.setFont(juce::Font(juce::FontOptions{9.5f}));
  g.setColour(secondaryText.withAlpha(0.8f));
  g.drawText("HEIGHT", (int)graph.getX() + 6, (int)graph.getBottom() - 18, 56, 14, juce::Justification::left);
  g.drawText("TONE", (int)graph.getRight() - 50, (int)graph.getY() + 5, 44, 14, juce::Justification::right);

  g.setColour((hasKeyboardFocus(true) ? accentColour : borderColour).withAlpha(0.9f));
  g.drawRoundedRectangle(bounds, 8.0f, hasKeyboardFocus(true) ? 1.7f : 1.0f);
}

void XYFormantPad::mouseDown(const juce::MouseEvent &event)
{
  grabKeyboardFocus();
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

bool XYFormantPad::keyPressed(const juce::KeyPress &key)
{
  const float step = key.getModifiers().isShiftDown() ? 50.0f : 10.0f;

  if (key.getKeyCode() == juce::KeyPress::leftKey)
    nudgeParameter("FORMANT_2", -step);
  else if (key.getKeyCode() == juce::KeyPress::rightKey)
    nudgeParameter("FORMANT_2", step);
  else if (key.getKeyCode() == juce::KeyPress::upKey)
    nudgeParameter("FORMANT_1", step);
  else if (key.getKeyCode() == juce::KeyPress::downKey)
    nudgeParameter("FORMANT_1", -step);
  else
    return false;

  return true;
}

std::unique_ptr<juce::AccessibilityHandler> XYFormantPad::createAccessibilityHandler()
{
  return std::make_unique<juce::AccessibilityHandler>(
      *this,
      juce::AccessibilityRole::group);
}

void XYFormantPad::timerCallback()
{
  if (!isShowing())
    return;

  const float f1 = readParamValue(processor, "FORMANT_1", 500.0f);
  const float f2 = readParamValue(processor, "FORMANT_2", 1500.0f);
  if (std::abs(f1 - lastF1) >= 0.5f || std::abs(f2 - lastF2) >= 0.5f)
  {
    lastF1 = f1;
    lastF2 = f2;
    setTitle("F1 " + juce::String((int)std::round(f1))
             + " Hz, F2 " + juce::String((int)std::round(f2))
             + " Hz formant target");
    if (auto *handler = getAccessibilityHandler())
      handler->notifyAccessibilityEvent(juce::AccessibilityEvent::titleChanged);
    repaint();
  }
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
  auto bounds = getLocalBounds().toFloat().reduced(13.0f);
  const float x = juce::jlimit(bounds.getX(), bounds.getRight(), pos.x);
  const float y = juce::jlimit(bounds.getY(), bounds.getBottom(), pos.y);

  const float newF2 = juce::jmap(x, bounds.getX(), bounds.getRight(), f2MinHz, f2MaxHz);
  const float newF1 = juce::jmap(y, bounds.getBottom(), bounds.getY(), f1MinHz, f1MaxHz);

  if (auto *f1Param = processor.getAPVTS().getParameter("FORMANT_1"))
    f1Param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, f1Param->convertTo0to1(newF1)));

  if (auto *f2Param = processor.getAPVTS().getParameter("FORMANT_2"))
    f2Param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, f2Param->convertTo0to1(newF2)));

  processor.normaliseFormantParameters();
  repaint();
}

void XYFormantPad::nudgeParameter(const juce::String &parameterID, float delta)
{
  if (auto *parameter = processor.getAPVTS().getParameter(parameterID))
  {
    const float current = readParamValue(processor, parameterID, 0.0f);
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(
        juce::jlimit(0.0f, 1.0f, parameter->convertTo0to1(current + delta)));
    parameter->endChangeGesture();
    processor.normaliseFormantParameters();
    repaint();
  }
}

void SpectralFormantMorpherAudioProcessorEditor::configureUtilityButton(
    juce::TextButton &button,
    const juce::String &tooltip)
{
  button.setTooltip(tooltip);
  button.setWantsKeyboardFocus(true);
  button.addListener(this);
  addAndMakeVisible(button);
}

void SpectralFormantMorpherAudioProcessorEditor::setStatusMessage(const juce::String &message,
                                                                  bool ok)
{
  statusLabel.setText(message, juce::dontSendNotification);
  statusLabel.setTooltip(message);
  statusLabel.setColour(juce::Label::textColourId, ok ? successColour : warmAccent);
  if (auto *handler = statusLabel.getAccessibilityHandler())
    handler->notifyAccessibilityEvent(juce::AccessibilityEvent::textChanged);
}

SpectralFormantMorpherAudioProcessorEditor::SpectralFormantMorpherAudioProcessorEditor(
    SpectralFormantMorpherAudioProcessor &p)
    : AudioProcessorEditor(&p),
      audioProcessor(p),
      visualizer(p),
      xyPad(p)
{
  setLookAndFeel(&lookAndFeel);
  setOpaque(true);

  addAndMakeVisible(visualizer);
  addAndMakeVisible(xyPad);

  formantAttachments.reserve(dsp::SpectralProcessor::numFormants - 2);

  for (size_t i = 0; i < formantSliders.size(); ++i)
  {
    auto &slider = formantSliders[i];
    const auto formantName = "F" + juce::String((int)i + 3);
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 21);
    slider.setTextValueSuffix(" Hz");
    slider.setTitle(formantName + " target");
    slider.setDescription("Target frequency for " + formantName);
    slider.setHelpText("Drag vertically or type a value in hertz. Double-click to reset.");
    slider.setTooltip(formantName + " target frequency");
    slider.setDoubleClickReturnValue(true, defaultFormantsHz[i + 2]);
    slider.setColour(juce::Slider::trackColourId,
                     i % 2 == 0 ? accentColour : accentSecondary.brighter(0.08f));
    addAndMakeVisible(slider);

    auto &label = formantLabels[i];
    label.setText(formantName, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, primaryText.withAlpha(0.88f));
    label.setAccessible(false);
    addAndMakeVisible(label);

    formantAttachments.push_back(
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getAPVTS(), formantParamId(i + 2), slider));
    slider.onDragEnd = [this]
    {
      audioProcessor.normaliseFormantParameters();
    };
    slider.onKeyboardEdit = [this]
    {
      audioProcessor.normaliseFormantParameters();
    };
  }

  mixSlider.setSliderStyle(juce::Slider::Rotary);
  mixSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
  mixSlider.setTextValueSuffix(" %");
  mixSlider.setTitle("Dry wet mix");
  mixSlider.setDescription("Blend between the latency-aligned dry and morphed signals");
  mixSlider.setHelpText("0 percent is dry. 100 percent is fully morphed.");
  mixSlider.setTooltip("Dry / wet mix");
  mixSlider.setDoubleClickReturnValue(true, 100.0);
  addAndMakeVisible(mixSlider);
  mixLabel.setText("DRY / WET", juce::dontSendNotification);
  mixLabel.setJustificationType(juce::Justification::centred);
  mixLabel.setColour(juce::Label::textColourId, secondaryText);
  mixLabel.setAccessible(false);
  addAndMakeVisible(mixLabel);
  mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
      audioProcessor.getAPVTS(), "MIX", mixSlider);

  gainSlider.setSliderStyle(juce::Slider::Rotary);
  gainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
  gainSlider.setTextValueSuffix(" dB");
  gainSlider.setTitle("Output gain");
  gainSlider.setDescription("Final output level after the dry wet blend");
  gainSlider.setHelpText("Double-click to return to 0 dB.");
  gainSlider.setTooltip("Output gain");
  gainSlider.setDoubleClickReturnValue(true, 0.0);
  gainSlider.setColour(juce::Slider::rotarySliderFillColourId, warmAccent);
  addAndMakeVisible(gainSlider);
  gainLabel.setText("OUTPUT", juce::dontSendNotification);
  gainLabel.setJustificationType(juce::Justification::centred);
  gainLabel.setColour(juce::Label::textColourId, secondaryText);
  gainLabel.setAccessible(false);
  addAndMakeVisible(gainLabel);
  gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
      audioProcessor.getAPVTS(), "OUTPUT_GAIN", gainSlider);

  configureUtilityButton(loadSourceButton,
                         "Analyze a clear vocal sample and apply its estimated formants");
  configureUtilityButton(copyProfileButton, "Copy the current Voice Profile as JSON");
  configureUtilityButton(pasteProfileButton, "Apply a Voice Profile from the clipboard");
  configureUtilityButton(exportProfileButton, "Save the current Voice Profile to a file");
  configureUtilityButton(importProfileButton, "Load a Voice Profile from a file");

  loadSourceButton.setColour(juce::TextButton::buttonColourId, accentColour.darker(0.42f));
  loadSourceButton.setColour(juce::TextButton::textColourOffId, primaryText);

  referenceStatusLabel.setText("No reference loaded", juce::dontSendNotification);
  referenceStatusLabel.setJustificationType(juce::Justification::centredLeft);
  referenceStatusLabel.setColour(juce::Label::textColourId, secondaryText);
  referenceStatusLabel.setTitle("Reference audio status");
  addAndMakeVisible(referenceStatusLabel);

  statusLabel.setText("Ready", juce::dontSendNotification);
  statusLabel.setJustificationType(juce::Justification::centredLeft);
  statusLabel.setColour(juce::Label::textColourId, secondaryText);
  statusLabel.setTitle("Operation status");
  addAndMakeVisible(statusLabel);

  setResizable(true, true);
  setResizeLimits(920, 620, 1600, 1000);

  const int savedWidth = juce::jlimit(
      920, 1600, (int)audioProcessor.getAPVTS().state.getProperty("editorWidth", 1180));
  const int savedHeight = juce::jlimit(
      620, 1000, (int)audioProcessor.getAPVTS().state.getProperty("editorHeight", 760));
  setSize(savedWidth, savedHeight);
}

SpectralFormantMorpherAudioProcessorEditor::~SpectralFormantMorpherAudioProcessorEditor()
{
  if (referenceAnalysisState != nullptr)
  {
    referenceAnalysisState->cancelled.store(true, std::memory_order_release);
    referenceAnalysisState->editor = nullptr;
    referenceAnalysisState.reset();
  }

  setLookAndFeel(nullptr);
  loadSourceButton.removeListener(this);
  copyProfileButton.removeListener(this);
  pasteProfileButton.removeListener(this);
  exportProfileButton.removeListener(this);
  importProfileButton.removeListener(this);
}

void SpectralFormantMorpherAudioProcessorEditor::paint(juce::Graphics &g)
{
  juce::ColourGradient background(
      backgroundTop,
      0.0f,
      0.0f,
      backgroundBottom,
      0.0f,
      (float)getHeight(),
      false);
  background.addColour(0.55, juce::Colour(0xff0a1119));
  g.setGradientFill(background);
  g.fillAll();

  const auto layout = createEditorLayout(getLocalBounds());

  auto header = layout.header;
  g.setFont(juce::Font(juce::FontOptions{26.0f, juce::Font::bold}));
  g.setColour(primaryText);
  g.drawText("Spectral Formant Morpher",
             header.removeFromTop(35),
             juce::Justification::centredLeft);
  g.setFont(juce::Font(juce::FontOptions{12.5f}));
  g.setColour(secondaryText);
  g.drawText("Shape vocal character through pitch-preserving spectral envelope warping",
             header,
             juce::Justification::centredLeft);

  if (getWidth() >= 1080)
  {
    auto badgeArea = layout.header;
    auto badge = badgeArea.removeFromRight(248).withSizeKeepingCentre(238, 28).toFloat();
    g.setColour(accentColour.withAlpha(0.09f));
    g.fillRoundedRectangle(badge, 14.0f);
    g.setColour(accentColour.withAlpha(0.45f));
    g.drawRoundedRectangle(badge, 14.0f, 1.0f);
    g.setFont(juce::Font(juce::FontOptions{10.5f, juce::Font::bold}));
    g.setColour(accentColour);
    g.drawText("REAL-TIME  /  1024 FFT  /  75% OVERLAP",
               badge.toNearestInt(),
               juce::Justification::centred);
  }

  drawCard(g, layout.utility);
  drawCard(g, layout.target);
  drawCard(g, layout.analyzer);
  drawCard(g, layout.higherFormants);

  drawSectionTitle(g, layout.target, "FORMANT TARGET", "F1 / F2");
  drawSectionTitle(g, layout.analyzer, "LIVE ANALYZER", "80 Hz - 12 kHz");
  drawSectionTitle(g, layout.higherFormants, "HIGHER FORMANTS", "F3 - F15");
}

void SpectralFormantMorpherAudioProcessorEditor::resized()
{
  const auto layout = createEditorLayout(getLocalBounds());

  auto utility = layout.utility.reduced(12, 10);
  loadSourceButton.setBounds(utility.removeFromLeft(164));
  utility.removeFromLeft(10);

  auto profileControls = utility.removeFromRight(280);
  copyProfileButton.setBounds(profileControls.removeFromLeft(58));
  profileControls.removeFromLeft(6);
  pasteProfileButton.setBounds(profileControls.removeFromLeft(58));
  profileControls.removeFromLeft(6);
  exportProfileButton.setBounds(profileControls.removeFromLeft(70));
  profileControls.removeFromLeft(6);
  importProfileButton.setBounds(profileControls.removeFromLeft(70));

  auto statusArea = utility;
  const int referenceWidth = juce::jmin(210, statusArea.getWidth() / 2);
  referenceStatusLabel.setBounds(statusArea.removeFromLeft(referenceWidth));
  statusArea.removeFromLeft(8);
  statusLabel.setBounds(statusArea);

  auto targetBody = layout.target.reduced(12);
  targetBody.removeFromTop(34);
  auto outputArea = targetBody.removeFromRight(116);
  targetBody.removeFromRight(10);
  xyPad.setBounds(targetBody);

  auto mixArea = outputArea.removeFromTop(outputArea.getHeight() / 2);
  mixLabel.setBounds(mixArea.removeFromTop(17));
  mixSlider.setBounds(mixArea.reduced(2, 0));

  gainLabel.setBounds(outputArea.removeFromTop(17));
  gainSlider.setBounds(outputArea.reduced(2, 0));

  auto analyzerBody = layout.analyzer.reduced(12);
  analyzerBody.removeFromTop(34);
  visualizer.setBounds(analyzerBody);

  auto sliderArea = layout.higherFormants.reduced(10);
  sliderArea.removeFromTop(34);
  const int sliderCount = (int)formantSliders.size();

  for (int i = 0; i < sliderCount; ++i)
  {
    const int remainingColumns = sliderCount - i;
    const int columnWidth = sliderArea.getWidth() / remainingColumns;
    auto column = sliderArea.removeFromLeft(columnWidth);
    formantLabels[(size_t)i].setBounds(column.removeFromTop(19));
    formantSliders[(size_t)i].setBounds(column.reduced(2, 0));
  }

  audioProcessor.getAPVTS().state.setProperty("editorWidth", getWidth(), nullptr);
  audioProcessor.getAPVTS().state.setProperty("editorHeight", getHeight(), nullptr);
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
      "Choose reference audio",
      juce::File(),
      "*.wav;*.aif;*.aiff;*.flac;*.ogg");

  constexpr int chooserFlags = juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles;
  sourceFileChooser->launchAsync(
      chooserFlags,
      [this](const juce::FileChooser &chooser)
      {
        const auto file = chooser.getResult();
        if (!file.existsAsFile())
          return;

        setStatusMessage("Analyzing reference...", true);
        loadSourceButton.setEnabled(false);

        if (referenceAnalysisState != nullptr)
        {
          referenceAnalysisState->cancelled.store(true, std::memory_order_release);
          referenceAnalysisState->editor = nullptr;
        }

        auto state = std::make_shared<ReferenceAnalysisState>();
        state->editor = this;
        referenceAnalysisState = state;
        getReferenceAnalysisPool().addJob(
            [state, file]
            {
              std::array<float, dsp::SpectralProcessor::numFormants> estimated{};
              size_t detectedFormantCount = 0;
              juce::String message;
              const auto shouldCancel = [state]
              {
                const auto *job = juce::ThreadPoolJob::getCurrentThreadPoolJob();
                return state->cancelled.load(std::memory_order_acquire)
                    || (job != nullptr && job->shouldExit());
              };
              const bool ok = SpectralFormantMorpherAudioProcessor::analyzeSourceFile(
                  file,
                  estimated,
                  detectedFormantCount,
                  message,
                  shouldCancel);

              if (shouldCancel())
                return;

              juce::MessageManager::callAsync(
                  [state,
                   file,
                   estimated,
                   detectedFormantCount,
                   message,
                   ok]
                  () mutable
                  {
                    auto *editor = state->editor;
                    if (editor == nullptr)
                      return;

                    if (editor->referenceAnalysisState != state)
                      return;

                    editor->referenceAnalysisState.reset();

                    editor->loadSourceButton.setEnabled(true);
                    if (ok)
                    {
                      editor->audioProcessor.applyReferenceFormants(
                          estimated, detectedFormantCount);
                      editor->referenceStatusLabel.setText(
                          file.getFileName(), juce::dontSendNotification);
                      editor->referenceStatusLabel.setTooltip(file.getFullPathName());
                      if (auto *handler =
                              editor->referenceStatusLabel.getAccessibilityHandler())
                        handler->notifyAccessibilityEvent(
                            juce::AccessibilityEvent::textChanged);

                      message = "Applied "
                              + juce::String((int)detectedFormantCount)
                              + " detected envelope peaks.";
                      if (detectedFormantCount < dsp::SpectralProcessor::numFormants)
                        message += " Higher targets were kept.";
                    }

                    editor->setStatusMessage(message, ok);
                    editor->xyPad.repaint();
                  });
            });
      });
}

void SpectralFormantMorpherAudioProcessorEditor::copyVoiceProfileToClipboard()
{
  juce::SystemClipboard::copyTextToClipboard(audioProcessor.createVoiceProfileJson());
  setStatusMessage("Voice Profile copied.", true);
}

void SpectralFormantMorpherAudioProcessorEditor::pasteVoiceProfileFromClipboard()
{
  juce::String message;
  const bool ok = audioProcessor.applyVoiceProfileJson(
      juce::SystemClipboard::getTextFromClipboard(), message);
  setStatusMessage(message, ok);
  xyPad.repaint();
}

void SpectralFormantMorpherAudioProcessorEditor::exportVoiceProfile()
{
  profileFileChooser = std::make_unique<juce::FileChooser>(
      "Export Voice Profile",
      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
          .getChildFile("SpectralFormantMorpher.sfmprofile"),
      "*.sfmprofile;*.json");

  constexpr int chooserFlags = juce::FileBrowserComponent::saveMode
                             | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting;

  profileFileChooser->launchAsync(
      chooserFlags,
      [this](const juce::FileChooser &chooser)
      {
        auto file = chooser.getResult();
        if (file == juce::File())
          return;

        if (!file.hasFileExtension(".sfmprofile") && !file.hasFileExtension(".json"))
          file = file.withFileExtension(".sfmprofile");

        const bool ok = file.replaceWithText(audioProcessor.createVoiceProfileJson());
        setStatusMessage(ok ? "Voice Profile exported." : "Could not export the Voice Profile.", ok);
      });
}

void SpectralFormantMorpherAudioProcessorEditor::importVoiceProfile()
{
  profileFileChooser = std::make_unique<juce::FileChooser>(
      "Import Voice Profile",
      juce::File(),
      "*.sfmprofile;*.json");

  constexpr int chooserFlags = juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles;
  profileFileChooser->launchAsync(
      chooserFlags,
      [this](const juce::FileChooser &chooser)
      {
        const auto file = chooser.getResult();
        if (!file.existsAsFile())
          return;

        juce::String message;
        const bool ok = audioProcessor.applyVoiceProfileJson(file.loadFileAsString(), message);
        setStatusMessage(message, ok);
        xyPad.repaint();
      });
}
