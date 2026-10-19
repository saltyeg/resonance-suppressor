#include "BandOverlay.h"
#include "Parameters.h"
#include "Theme.h"

namespace rs::ui
{
namespace
{
const juce::Colour nodeColour { 0xff2e86ab };
constexpr float nodeRadius = 7.0f;
constexpr const char* fields[] = { "Used", "Enabled", "Shape", "Freq", "Depth", "Q", "Slope", "Focus" };
constexpr float depthScale = 1.5f; // matches Tuning::bandScale for display purposes
} // namespace

BandOverlay::BandOverlay (juce::AudioProcessorValueTreeState& s, Analyzer& a) : state (s), analyzer (a)
{
    for (int b = 0; b < numBands; ++b)
        for (auto* f : fields)
            state.addParameterListener (ids::band (b, f), this);
    bands = readBands();
    setRepaintsOnMouseActivity (false);
}

BandOverlay::~BandOverlay()
{
    for (int b = 0; b < numBands; ++b)
        for (auto* f : fields)
            state.removeParameterListener (ids::band (b, f), this);
}

juce::RangedAudioParameter& BandOverlay::param (int band, const char* field) const
{
    return *state.getParameter (ids::band (band, field));
}

void BandOverlay::setParam (int band, const char* field, float value)
{
    auto& p = param (band, field);
    p.setValueNotifyingHost (p.convertTo0to1 (value));
}

BandSet BandOverlay::readBands() const
{
    BandSet result;
    for (int b = 0; b < numBands; ++b)
    {
        auto v = [&] (const char* f) { return state.getRawParameterValue (ids::band (b, f))->load(); };
        auto& d = result[(size_t) b];
        d.used = v ("Used") > 0.5f;
        d.enabled = v ("Enabled") > 0.5f;
        d.shape = (Shape) juce::jlimit (0, numShapes - 1, juce::roundToInt (v ("Shape")));
        d.freq = v ("Freq");
        d.depth = v ("Depth");
        d.q = v ("Q");
        d.slope = v ("Slope");
        d.focus = v ("Focus");
    }
    return result;
}

void BandOverlay::handleAsyncUpdate()
{
    bands = readBands();
    repaint();
}

bool BandOverlay::usesDepth (Shape s) const
{
    return s == Shape::bell || s == Shape::lowShelf || s == Shape::highShelf || s == Shape::tilt;
}

float BandOverlay::yForSensitivity (float db) const
{
    const auto r = analyzer.plotArea();
    const float centre = r.getY() + r.getHeight() * 0.55f;
    return centre - db / sensitivityRange * r.getHeight() * 0.35f;
}

float BandOverlay::depthForY (float y) const
{
    const auto r = analyzer.plotArea();
    const float centre = r.getY() + r.getHeight() * 0.55f;
    return (centre - y) / (r.getHeight() * 0.35f) * sensitivityRange / depthScale;
}

juce::Point<float> BandOverlay::nodePosition (int b) const
{
    const auto& band = bands[(size_t) b];
    const float y = usesDepth (band.shape) ? yForSensitivity (band.depth * depthScale) : yForSensitivity (0.0f);
    return { analyzer.xForHz (band.freq), y };
}

int BandOverlay::nodeAt (juce::Point<float> p) const
{
    for (int b = numBands - 1; b >= 0; --b)
        if (bands[(size_t) b].used && nodePosition (b).getDistanceFrom (p) <= nodeRadius + 3.0f)
            return b;
    return -1;
}

bool BandOverlay::hitTest (int x, int y)
{
    return analyzer.plotArea().contains ((float) x, (float) y);
}

juce::String BandOverlay::describe (int b) const
{
    const auto& band = bands[(size_t) b];
    juce::String s = "Band " + juce::String (b + 1) + ": " + shapeNames[(int) band.shape] + "  "
                     + param (b, "Freq").getCurrentValueAsText();
    if (usesDepth (band.shape))
        s << "  " << (band.depth >= 0 ? "+" : "") << juce::String (band.depth, 1);
    s << "  Q " << param (b, "Q").getCurrentValueAsText();
    if (! usesDepth (band.shape))
        s << "  slope " << juce::String (band.slope, 1);
    if (! band.enabled)
        s << "  (off)";
    return s;
}

void BandOverlay::paint (juce::Graphics& g)
{
    const auto r = analyzer.plotArea();

    // Combined sensitivity curve (first channel).
    juce::Path curve;
    const int steps = juce::jmax (2, (int) r.getWidth() / 2);
    for (int i = 0; i <= steps; ++i)
    {
        const float x = r.getX() + r.getWidth() * (float) i / (float) steps;
        const float db = sensitivityDb (bands, analyzer.hzForX (x), 0, 2, depthScale);
        const float y = juce::jlimit (r.getY(), r.getBottom(), yForSensitivity (juce::jmax (db, -sensitivityRange * 1.2f)));
        i == 0 ? curve.startNewSubPath (x, y) : curve.lineTo (x, y);
    }
    g.setColour (nodeColour.withAlpha (0.12f));
    g.drawHorizontalLine ((int) yForSensitivity (0.0f), r.getX(), r.getRight());
    g.setColour (nodeColour.withAlpha (0.8f));
    g.strokePath (curve, juce::PathStrokeType (1.5f));

    // Nodes.
    for (int b = 0; b < numBands; ++b)
    {
        const auto& band = bands[(size_t) b];
        if (! band.used)
            continue;
        const auto p = nodePosition (b);
        const auto circle = juce::Rectangle<float> (nodeRadius * 2.0f, nodeRadius * 2.0f).withCentre (p);
        const bool hot = b == hovered || b == dragging;

        const auto colour = band.enabled ? nodeColour : colours::muted;
        if (hot)
        {
            g.setColour (colour.withAlpha (0.25f));
            g.fillEllipse (circle.expanded (4.0f));
        }
        g.setColour (colour);
        g.fillEllipse (circle);
        g.setColour (colours::panel);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (juce::String (b + 1), circle, juce::Justification::centred);
    }

    // Readout for the hovered / dragged node.
    const int shown = dragging >= 0 ? dragging : hovered;
    if (shown >= 0)
    {
        const auto text = describe (shown);
        const auto p = nodePosition (shown);
        const float w = 12.0f + (float) juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (12.0f), text);
        auto box = juce::Rectangle<float> (w, 20.0f).withCentre ({ p.x, p.y - 22.0f });
        box = box.constrainedWithin (r.reduced (2.0f));
        g.setColour (colours::ink.withAlpha (0.9f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (colours::panel);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (text, box, juce::Justification::centred);
    }
}

void BandOverlay::mouseMove (const juce::MouseEvent& e)
{
    const int n = nodeAt (e.position);
    if (n != hovered)
    {
        hovered = n;
        setMouseCursor (n >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void BandOverlay::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void BandOverlay::mouseDown (const juce::MouseEvent& e)
{
    const int n = nodeAt (e.position);
    if (n < 0)
        return;

    if (e.mods.isPopupMenu())
    {
        showMenu (n);
        return;
    }

    dragging = n;
    param (n, "Freq").beginChangeGesture();
    param (n, "Depth").beginChangeGesture();
}

void BandOverlay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;
    const auto r = analyzer.plotArea();
    const float x = juce::jlimit (r.getX(), r.getRight(), e.position.x);
    setParam (dragging, "Freq", juce::jlimit (20.0f, 20000.0f, analyzer.hzForX (x)));
    if (usesDepth (bands[(size_t) dragging].shape))
        setParam (dragging, "Depth", juce::jlimit (-10.0f, 10.0f, depthForY (e.position.y)));
}

void BandOverlay::mouseUp (const juce::MouseEvent&)
{
    if (dragging < 0)
        return;
    param (dragging, "Freq").endChangeGesture();
    param (dragging, "Depth").endChangeGesture();
    dragging = -1;
    repaint();
}

void BandOverlay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const int n = nodeAt (e.position); n >= 0)
    {
        setParam (n, "Used", 0.0f);
        hovered = -1;
        return;
    }

    for (int b = 0; b < numBands; ++b)
    {
        if (bands[(size_t) b].used)
            continue;
        setParam (b, "Shape", (float) Shape::bell);
        setParam (b, "Freq", juce::jlimit (20.0f, 20000.0f, analyzer.hzForX (e.position.x)));
        setParam (b, "Depth", juce::jlimit (-10.0f, 10.0f, depthForY (e.position.y)));
        setParam (b, "Q", 0.7f);
        setParam (b, "Slope", 5.0f);
        setParam (b, "Focus", 0.5f);
        setParam (b, "Enabled", 1.0f);
        setParam (b, "Used", 1.0f);
        return;
    }
}

void BandOverlay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int n = nodeAt (e.position);
    if (n < 0)
        return;
    const auto& band = bands[(size_t) n];
    const float step = wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f);
    if (band.shape == Shape::lowCut || band.shape == Shape::highCut)
        setParam (n, "Slope", juce::jlimit (0.0f, 10.0f, band.slope + step * 2.0f));
    else
        setParam (n, "Q", juce::jlimit (0.1f, 10.0f, band.q * std::exp2 (step * 1.5f)));
}

void BandOverlay::showMenu (int b)
{
    const auto& band = bands[(size_t) b];
    juce::PopupMenu shapes;
    for (int s = 0; s < numShapes; ++s)
        shapes.addItem (shapeNames[s], true, (int) band.shape == s, [this, b, s] { setParam (b, "Shape", (float) s); });

    juce::PopupMenu focus;
    const std::pair<const char*, float> focusOptions[] = { { "First channel (L / M)", 0.0f }, { "Even", 0.5f }, { "Second channel (R / S)", 1.0f } };
    for (auto& [label, value] : focusOptions)
        focus.addItem (label, true, std::abs (band.focus - value) < 0.01f, [this, b, v = value] { setParam (b, "Focus", v); });

    juce::PopupMenu menu;
    menu.addSectionHeader ("Band " + juce::String (b + 1));
    menu.addSubMenu ("Shape", shapes);
    menu.addSubMenu ("Stereo focus", focus);
    menu.addItem (band.enabled ? "Disable" : "Enable", [this, b, on = band.enabled] { setParam (b, "Enabled", on ? 0.0f : 1.0f); });
    menu.addSeparator();
    menu.addItem ("Remove", [this, b] { setParam (b, "Used", 0.0f); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}
} // namespace rs::ui
