#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class SpectrumVisualizer : public juce::Component, public juce::Timer
{
public:
    SpectrumVisualizer(SpectralFormantMorpherAudioProcessor& p) : processor(p)
    {
        startTimerHz(60); // 60fps refresh
    }

    ~SpectrumVisualizer() override
    {
        stopTimer();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black);

        if (lastSpectrum.empty()) return;

        auto bounds = getLocalBounds();
        float width = (float)bounds.getWidth();
        float height = (float)bounds.getHeight();

        // 1. Draw Spectrum
        g.setColour(juce::Colours::darkgrey.withAlpha(0.5f));
        drawPath(g, lastSpectrum, width, height, true);

        // 2. Draw Envelope (Warped)
        g.setColour(juce::Colours::cyan);
        drawPath(g, lastEnvelope, width, height, false);

        // 3. Draw Nodes (F1, F2)
        // Convert F1, F2 bins to X coordinates
        // getLatestVisualizationData returns *Target* F1 and F2 in bins.

        float binWidth = width / (float)lastEnvelope.size();

        // F1 Node
        float f1X = lastF1 * binWidth;
        // Approximate Y for node (look up envelope magnitude at that bin)
        float f1Mag = 0.0f;
        if ((size_t)lastF1 < lastEnvelope.size()) f1Mag = lastEnvelope[(size_t)lastF1];
        float f1Y = magToY(f1Mag, height);

        drawNode(g, f1X, f1Y, "F1", draggingNode == 1);

        // F2 Node
        float f2X = lastF2 * binWidth;
        float f2Mag = 0.0f;
        if ((size_t)lastF2 < lastEnvelope.size()) f2Mag = lastEnvelope[(size_t)lastF2];
        float f2Y = magToY(f2Mag, height);

        drawNode(g, f2X, f2Y, "F2", draggingNode == 2);
    }

    void timerCallback() override
    {
        processor.getSpectralProcessor().getLatestVisualizationData(lastSpectrum, lastEnvelope, lastF1, lastF2);
        repaint();
    }

    // Mouse Interaction
    void mouseDown(const juce::MouseEvent& e) override
    {
        auto bounds = getLocalBounds();
        float width = (float)bounds.getWidth();
        float binWidth = width / (float)lastEnvelope.size();

        float f1X = lastF1 * binWidth;
        float f2X = lastF2 * binWidth;

        float mouseX = (float)e.x;

        // Check distance (simple X distance check for now)
        float sensitivity = 20.0f;

        if (std::abs(mouseX - f1X) < sensitivity)
            draggingNode = 1;
        else if (std::abs(mouseX - f2X) < sensitivity)
            draggingNode = 2;
        else
            draggingNode = 0;

        // If dragging, we capture detected F1/F2 at start of drag to apply relative shift?
        // Or we just calculate new shift based on where we are dragging vs where it "should" be?
        // Actually, we are dragging the Target.
        // The parameter is Shift Factor.
        // Target = Source * Shift.
        // Source is not known here perfectly, but we can infer it:
        // Source = Target / CurrentShift.
        // NewShift = NewTarget / Source.

        if (draggingNode > 0)
        {
            float currentShift = (draggingNode == 1) ?
                processor.getAPVTS().getRawParameterValue("F1_SHIFT")->load() :
                processor.getAPVTS().getRawParameterValue("F2_SHIFT")->load();

            float currentTarget = (draggingNode == 1) ? lastF1 : lastF2;

            // Reconstruct approximate source
            dragSourceBin = currentTarget / std::max(0.01f, currentShift);
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (draggingNode == 0) return;

        float width = (float)getWidth();
        float numBins = (float)lastEnvelope.size();

        // Calculate new target bin from Mouse X
        float mouseX = std::max(0.0f, std::min((float)e.x, width));
        float newTargetBin = mouseX / width * numBins;

        // Calculate new shift
        // NewTarget = Source * NewShift  =>  NewShift = NewTarget / Source
        float newShift = 1.0f;
        if (dragSourceBin > 0.001f)
            newShift = newTargetBin / dragSourceBin;

        // Clamp to parameter range (0.5 to 2.0)
        newShift = std::max(0.5f, std::min(newShift, 2.0f));

        if (draggingNode == 1)
        {
            // Set Parameter
            // Note: In VST3, we should normalize, but getParameter expects plain value if using setValueNotifyingHost?
            // APVTS doesn't have direct setValue. We use the Attachment or Parameter.

            if (auto* param = processor.getAPVTS().getParameter("F1_SHIFT"))
                param->setValueNotifyingHost(param->convertTo0to1(newShift));
        }
        else if (draggingNode == 2)
        {
            if (auto* param = processor.getAPVTS().getParameter("F2_SHIFT"))
                param->setValueNotifyingHost(param->convertTo0to1(newShift));
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        draggingNode = 0;
    }

private:
    SpectralFormantMorpherAudioProcessor& processor;
    std::vector<float> lastSpectrum;
    std::vector<float> lastEnvelope;
    float lastF1 = 0.0f;
    float lastF2 = 0.0f;

    int draggingNode = 0; // 0=None, 1=F1, 2=F2
    float dragSourceBin = 0.0f;

    float magToY(float mag, float height)
    {
        float db = juce::Decibels::gainToDecibels(mag);
        // Range: -100dB to 0dB
        return juce::jmap(db, -100.0f, 0.0f, height, 0.0f);
    }

    void drawPath(juce::Graphics& g, const std::vector<float>& data, float width, float height, bool fill)
    {
        if (data.empty()) return;

        juce::Path p;
        p.startNewSubPath(0, height);

        int numBins = (int)data.size();

        for (int i = 0; i < numBins; ++i)
        {
            float x = (float)i / (float)numBins * width;
            float mag = data[(size_t)i];
            float y = magToY(mag, height);
            p.lineTo(x, y);
        }

        if (fill)
        {
            p.lineTo(width, height);
            p.closeSubPath();
            g.fillPath(p);
        }
        else
        {
            g.strokePath(p, juce::PathStrokeType(2.0f));
        }
    }

    void drawNode(juce::Graphics& g, float x, float y, const juce::String& label, bool highlight)
    {
        float r = 8.0f;
        juce::Rectangle<float> area(x - r, y - r, r * 2, r * 2);

        g.setColour(highlight ? juce::Colours::white : juce::Colours::yellow);
        g.fillEllipse(area);
        g.setColour(juce::Colours::black);
        g.drawEllipse(area, 2.0f);

        g.setColour(juce::Colours::white);
        g.drawText(label, (int)x + 10, (int)y - 10, 30, 20, juce::Justification::left);
    }
};

class SpectralFormantMorpherAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    SpectralFormantMorpherAudioProcessorEditor (SpectralFormantMorpherAudioProcessor&);
    ~SpectralFormantMorpherAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SpectralFormantMorpherAudioProcessor& audioProcessor;

    SpectrumVisualizer visualizer;

    juce::Slider f1ShiftSlider;
    juce::Slider f2ShiftSlider;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> f1Attachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> f2Attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralFormantMorpherAudioProcessorEditor)
};
