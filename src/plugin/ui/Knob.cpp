#include "Knob.h"
#include "Theme.h"

namespace rs::ui
{
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& n, bool bipolar, Readout realUnits)
    : param (*state.getParameter (paramId)),
      name (n),
      readout (std::move (realUnits)),
      attachment (state, paramId, slider)
{
    slider.getProperties().set ("bipolar", bipolar);
    slider.setDoubleClickReturnValue (true, param.convertFrom0to1 (param.getDefaultValue()));
    slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    slider.onValueChange = [this] { repaint(); };
    slider.addMouseListener (this, false);
    addAndMakeVisible (slider);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (18);
    r.removeFromBottom (18);
    slider.setBounds (r);
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (colours::muted);
    g.setFont (juce::FontOptions (12.0f));
    g.drawFittedText (name.toUpperCase(), r.removeFromTop (18), juce::Justification::centred, 1);

    const bool active = isMouseOverOrDragging (true) || slider.isMouseButtonDown();
    const auto value = (float) slider.getValue();
    const auto text = active && readout ? readout (value) : param.getCurrentValueAsText();
    g.setColour (active ? colours::accent : colours::ink);
    g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText (text, r.removeFromBottom (18), juce::Justification::centred, 1);
}
} // namespace rs::ui
