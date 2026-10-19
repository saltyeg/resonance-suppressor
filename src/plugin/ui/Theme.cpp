#include "Theme.h"

namespace rs::ui
{
LookAndFeel::LookAndFeel()
{
    setColourScheme (getLightColourScheme());
    setColour (juce::TextButton::buttonColourId, colours::panel);
    setColour (juce::TextButton::buttonOnColourId, colours::ink);
    setColour (juce::TextButton::textColourOffId, colours::ink);
    setColour (juce::TextButton::textColourOnId, colours::panel);
    setColour (juce::AlertWindow::backgroundColourId, colours::panel);
    setColour (juce::AlertWindow::textColourId, colours::ink);
    setColour (juce::TextEditor::backgroundColourId, colours::background);
    setColour (juce::TextEditor::textColourId, colours::ink);
    setColour (juce::TextEditor::outlineColourId, colours::panelEdge);
    setColour (juce::ResizableWindow::backgroundColourId, colours::background);
    setColour (juce::Label::textColourId, colours::ink);
    setColour (juce::ComboBox::backgroundColourId, colours::panel);
    setColour (juce::ComboBox::textColourId, colours::ink);
    setColour (juce::ComboBox::outlineColourId, colours::panelEdge);
    setColour (juce::ComboBox::arrowColourId, colours::muted);
    setColour (juce::PopupMenu::backgroundColourId, colours::panel);
    setColour (juce::PopupMenu::textColourId, colours::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::track);
    setColour (juce::PopupMenu::highlightedTextColourId, colours::ink);
    setColour (juce::TooltipWindow::backgroundColourId, colours::ink);
    setColour (juce::TooltipWindow::textColourId, colours::panel);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float stroke = juce::jmax (2.5f, radius * 0.12f);
    const float arcRadius = radius - stroke * 0.5f;
    const float angle = start + pos * (end - start);

    // Bipolar knobs (trims, tilts, offsets) fill from the centre.
    const bool bipolar = slider.getProperties().getWithDefault ("bipolar", false);
    const float from = bipolar ? (start + end) * 0.5f : start;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, start, end, true);
    g.setColour (colours::track);
    g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (slider.isEnabled() ? colours::ink : colours::muted);
    g.strokePath (value, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Knob face + pointer.
    const float face = arcRadius - stroke * 1.4f;
    g.setColour (colours::panel);
    g.fillEllipse (juce::Rectangle<float> (face * 2.0f, face * 2.0f).withCentre (centre));
    g.setColour (colours::panelEdge);
    g.drawEllipse (juce::Rectangle<float> (face * 2.0f, face * 2.0f).withCentre (centre), 1.0f);

    const auto tip = centre.getPointOnCircumference (face * 0.8f, angle);
    const auto base = centre.getPointOnCircumference (face * 0.35f, angle);
    g.setColour (colours::ink);
    g.drawLine ({ base, tip }, juce::jmax (1.5f, stroke * 0.6f));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    auto fill = on ? colours::ink : colours::panel;
    if (! on && (highlighted || down))
        fill = colours::track;
    g.setColour (b.isEnabled() ? fill : colours::background);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? colours::ink : colours::panelEdge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    const auto onColour = b.getProperties().getWithDefault ("accent", false) ? colours::accent : colours::ink;

    g.setColour (on ? onColour : (highlighted ? colours::track : colours::panel));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? onColour : colours::panelEdge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    g.setColour (on ? colours::panel : colours::ink);
    g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText (b.getButtonText(), r.toNearestInt(), juce::Justification::centred, 1);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1.0f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}
} // namespace rs::ui
