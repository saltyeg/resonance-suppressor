#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace rs::ui
{
// Rotary knob with a name above and a value below. While hovered or dragged, the value line shows a
// real-unit readout (e.g. "11 ms @ 1 kHz") when one is provided.
class Knob final : public juce::Component
{
public:
    using Readout = std::function<juce::String (float value)>;

    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& name,
          bool bipolar = false, Readout realUnits = {});

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };

private:
    juce::RangedAudioParameter& param;
    juce::String name;
    Readout readout;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};
} // namespace rs::ui
