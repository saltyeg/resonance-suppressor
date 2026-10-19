#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Analyzer.h"
#include "dsp/Bands.h"

namespace rs::ui
{
// Draws the sensitivity curve from the band nodes over the analyzer and lets the user edit nodes:
//   drag            frequency (x) and depth (y)
//   wheel           Q (slope for cut shapes)
//   double-click    empty space adds a bell; a node removes it
//   right-click     shape / enable / focus / remove menu
class BandOverlay final : public juce::Component,
                          private juce::AudioProcessorValueTreeState::Listener,
                          private juce::AsyncUpdater
{
public:
    BandOverlay (juce::AudioProcessorValueTreeState&, Analyzer&);
    ~BandOverlay() override;

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;

    BandSet readBands() const;
    juce::RangedAudioParameter& param (int band, const char* field) const;
    void setParam (int band, const char* field, float value);
    juce::Point<float> nodePosition (int band) const;
    int nodeAt (juce::Point<float>) const;
    float yForSensitivity (float db) const;
    float depthForY (float y) const;
    bool usesDepth (Shape) const;
    void showMenu (int band);
    juce::String describe (int band) const;

    juce::AudioProcessorValueTreeState& state;
    Analyzer& analyzer;
    BandSet bands;
    int hovered = -1, dragging = -1;

    static constexpr float sensitivityRange = 18.0f; // dB shown above/below the centre line
};
} // namespace rs::ui
