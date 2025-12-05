#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class SpectrumVisualizer : public juce::Component, public juce::Timer
{
public:
    SpectrumVisualizer(SpectralFormantMorpherAudioProcessor& p) : processor(p)
    {
        startTimerHz(30); // 30fps refresh
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

        // Draw Spectrum
        g.setColour(juce::Colours::darkgrey.withAlpha(0.5f));
        drawPath(g, lastSpectrum, width, height, true);

        // Draw Original Envelope (we don't have it easily accessible unless we fetch it,
        // currently `visEnvelope` is the WARPED one in SpectralProcessor, wait.
        // `visEnvelope` is `warpedEnvelope`.
        // To visualize what's happening, we probably want both or just the result.
        // Let's draw the Warped Envelope as the "Curve".

        g.setColour(juce::Colours::cyan);
        drawPath(g, lastEnvelope, width, height, false);
    }

    void timerCallback() override
    {
        processor.getSpectralProcessor().getLatestVisualizationData(lastSpectrum, lastEnvelope);
        repaint();
    }

private:
    SpectralFormantMorpherAudioProcessor& processor;
    std::vector<float> lastSpectrum;
    std::vector<float> lastEnvelope;

    void drawPath(juce::Graphics& g, const std::vector<float>& data, float width, float height, bool fill)
    {
        if (data.empty()) return;

        juce::Path p;
        p.startNewSubPath(0, height);

        int numBins = (int)data.size();

        for (int i = 0; i < numBins; ++i)
        {
            float x = (float)i / numBins * width;

            // Decibel scale or linear magnitude?
            // Data is linear magnitude.
            // Log scale for display is better.

            float mag = data[i];
            float db = juce::Decibels::gainToDecibels(mag);
            // Range: -100dB to 0dB?
            float normalizedY = juce::jmap(db, -100.0f, 0.0f, height, 0.0f);

            p.lineTo(x, normalizedY);
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
    juce::Slider overallScaleSlider;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> f1Attachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> scaleAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralFormantMorpherAudioProcessorEditor)
};
