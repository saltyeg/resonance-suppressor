#include "Analyzer.h"
#include "Theme.h"

namespace rs::ui
{
namespace
{
float pointHz (int i)
{
    return 20.0f * std::pow (1000.0f, ((float) i + 0.5f) / (float) rs::DisplayFrame::numPoints);
}
} // namespace

Analyzer::Analyzer (rs::Suppressor& source) : suppressor (source)
{
    shownLevel.fill (-120.0f);
    shownGain.fill (0.0f);
    peakGain.fill (0.0f);
    setOpaque (true);
    startTimerHz (30);
}

juce::Rectangle<float> Analyzer::plotArea() const
{
    return getLocalBounds().toFloat().reduced (1.0f).withTrimmedLeft (34.0f).withTrimmedBottom (18.0f).withTrimmedRight (34.0f);
}

float Analyzer::xForHz (float hz) const
{
    const auto r = plotArea();
    return r.getX() + r.getWidth() * std::log (hz / minHz) / std::log (maxHz / minHz);
}

float Analyzer::hzForX (float x) const
{
    const auto r = plotArea();
    return minHz * std::pow (maxHz / minHz, (x - r.getX()) / r.getWidth());
}

float Analyzer::yForLevel (float db, float hz) const
{
    const auto r = plotArea();
    const float tilted = db + displayTilt * std::log2 (hz / 1000.0f);
    return juce::jmap (tilted, levelTop, levelBottom, r.getY(), r.getBottom());
}

float Analyzer::yForGain (float db) const
{
    // 0 dB sits just under the top edge; -gainRange reaches 70% of the plot height.
    const auto r = plotArea();
    return juce::jmap (-db, 0.0f, gainRange, r.getY() + 6.0f, r.getY() + r.getHeight() * 0.7f);
}

void Analyzer::timerCallback()
{
    if (! suppressor.pullDisplayFrame (frame))
    {
        // No audio: let the display decay.
        for (int i = 0; i < rs::DisplayFrame::numPoints; ++i)
        {
            shownLevel[(size_t) i] = juce::jmax (-120.0f, shownLevel[(size_t) i] - 1.5f);
            shownGain[(size_t) i] *= 0.85f;
            peakGain[(size_t) i] *= 0.97f;
        }
        repaint();
        return;
    }

    hasData = true;
    for (int i = 0; i < rs::DisplayFrame::numPoints; ++i)
    {
        // Fast rise, slower fall, for a readable spectrum.
        const float level = frame.inputDb[(size_t) i];
        auto& shown = shownLevel[(size_t) i];
        shown = level > shown ? 0.6f * level + 0.4f * shown : 0.8f * shown + 0.2f * level;

        const float gain = frame.gainDb[(size_t) i];
        shownGain[(size_t) i] = 0.5f * gain + 0.5f * shownGain[(size_t) i];
        peakGain[(size_t) i] = juce::jmin (gain, peakGain[(size_t) i] * 0.985f);
    }
    repaint();
}

void Analyzer::drawGrid (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setFont (juce::FontOptions (11.0f));

    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForHz (hz);
        g.setColour (colours::grid);
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (colours::muted);
        const auto label = hz >= 1000.0f ? juce::String (hz / 1000.0f, 0) + "k" : juce::String ((int) hz);
        g.drawText (label, juce::Rectangle<float> (x - 20.0f, r.getBottom() + 2.0f, 40.0f, 14.0f), juce::Justification::centred);
    }

    // Reduction scale on the right.
    for (float db : { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f })
    {
        const float y = yForGain (db);
        g.setColour (colours::grid);
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setColour (colours::accent.withAlpha (0.7f));
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (r.getRight() + 4.0f, y - 7.0f, 30.0f, 14.0f), juce::Justification::centredLeft);
    }

    // Level scale on the left (at 1 kHz, before display tilt).
    for (float db : { -24.0f, -48.0f, -72.0f })
    {
        const float y = yForLevel (db, 1000.0f);
        g.setColour (colours::muted);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (r.getX() - 34.0f, y - 7.0f, 30.0f, 14.0f), juce::Justification::centredRight);
    }
}

void Analyzer::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    const auto r = plotArea();

    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 6.0f);
    drawGrid (g, r);

    g.saveState();
    g.reduceClipRegion (r.toNearestInt());

    constexpr int n = rs::DisplayFrame::numPoints;

    // Input spectrum (filled) and output (= input + gain) as a line.
    juce::Path input, output;
    for (int i = 0; i < n; ++i)
    {
        const float hz = pointHz (i), x = xForHz (hz);
        const float yIn = yForLevel (shownLevel[(size_t) i], hz);
        const float yOut = yForLevel (shownLevel[(size_t) i] + shownGain[(size_t) i], hz);
        if (i == 0)
        {
            input.startNewSubPath (x, r.getBottom());
            output.startNewSubPath (x, yOut);
        }
        input.lineTo (x, yIn);
        output.lineTo (x, yOut);
    }
    input.lineTo (r.getRight(), r.getBottom());
    input.closeSubPath();
    g.setColour (colours::spectrum.withAlpha (0.55f));
    g.fillPath (input);
    g.setColour (colours::output);
    g.strokePath (output, juce::PathStrokeType (1.2f));

    // Reduction: filled from the 0 dB line down, with a slower peak-hold outline.
    juce::Path reduction, peak;
    const float zeroY = yForGain (0.0f);
    reduction.startNewSubPath (r.getX(), zeroY);
    for (int i = 0; i < n; ++i)
    {
        const float x = xForHz (pointHz (i));
        reduction.lineTo (x, yForGain (shownGain[(size_t) i]));
        if (i == 0)
            peak.startNewSubPath (x, yForGain (peakGain[(size_t) i]));
        else
            peak.lineTo (x, yForGain (peakGain[(size_t) i]));
    }
    reduction.lineTo (r.getRight(), zeroY);
    reduction.closeSubPath();

    g.setColour (colours::accentSoft);
    g.fillPath (reduction);
    g.setColour (colours::accent);
    g.strokePath (reduction, juce::PathStrokeType (1.6f));
    g.setColour (colours::accent.withAlpha (0.35f));
    g.strokePath (peak, juce::PathStrokeType (1.0f));

    g.restoreState();

    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}
} // namespace rs::ui
