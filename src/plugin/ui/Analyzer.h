#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "dsp/Suppressor.h"

namespace rs::ui
{
// Live spectrum (input filled, output line) with the reduction curve hanging from the top.
class Analyzer final : public juce::Component, private juce::Timer
{
public:
    explicit Analyzer (rs::Suppressor& source);

    void paint (juce::Graphics&) override;

    // Plot mapping, shared with overlays (band nodes in milestone 4).
    juce::Rectangle<float> plotArea() const;
    float xForHz (float hz) const;
    float hzForX (float x) const;

private:
    void timerCallback() override;
    float yForLevel (float db, float hz) const;
    float yForGain (float db) const;
    void drawGrid (juce::Graphics&, juce::Rectangle<float>);

    rs::Suppressor& suppressor;
    rs::DisplayFrame frame;
    std::array<float, rs::DisplayFrame::numPoints> shownLevel {}, shownGain {}, peakGain {};
    bool hasData = false;

    static constexpr float minHz = 20.0f, maxHz = 20000.0f;
    static constexpr float levelTop = 0.0f, levelBottom = -96.0f;
    static constexpr float gainRange = 30.0f;  // dB shown by the reduction curve
    static constexpr float displayTilt = 3.0f; // dB/oct around 1 kHz, so pink noise reads flat
};
} // namespace rs::ui
