#include "PluginEditor.h"

ResonanceSuppressorEditor::ResonanceSuppressorEditor (ResonanceSuppressorProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      outTrimAttachment (p.apvts, "outTrim", outTrim)
{
    addAndMakeVisible (outTrim);
    setSize (640, 360);
}

void ResonanceSuppressorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xfff6f5f2));
    g.setColour (juce::Colour (0xff2a2a2a));
    g.setFont (20.0f);
    g.drawText ("resonance-suppressor  (milestone 1: pass-through)",
                getLocalBounds().removeFromTop (60), juce::Justification::centred);
}

void ResonanceSuppressorEditor::resized()
{
    outTrim.setBounds (getLocalBounds().withSizeKeepingCentre (120, 140));
}
