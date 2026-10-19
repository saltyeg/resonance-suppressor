#pragma once

#include "PluginProcessor.h"

class ResonanceSuppressorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ResonanceSuppressorEditor (ResonanceSuppressorProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ResonanceSuppressorProcessor& processor;

    juce::Slider outTrim { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::AudioProcessorValueTreeState::SliderAttachment outTrimAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResonanceSuppressorEditor)
};
