#include "PluginProcessor.h"
#include "PluginEditor.h"

ResonanceSuppressorProcessor::ResonanceSuppressorProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout ResonanceSuppressorProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "outTrim", 1 }, "Out trim",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f));
    return layout;
}

bool ResonanceSuppressorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& main = layouts.getMainOutputChannelSet();
    if (main != juce::AudioChannelSet::mono() && main != juce::AudioChannelSet::stereo())
        return false;
    if (main != layouts.getMainInputChannelSet())
        return false;

    const auto& sc = layouts.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
}

void ResonanceSuppressorProcessor::prepareToPlay (double, int)
{
}

void ResonanceSuppressorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto main = getBusBuffer (buffer, true, 0);
    const auto gain = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("outTrim")->load());
    main.applyGain (gain);
}

juce::AudioProcessorEditor* ResonanceSuppressorProcessor::createEditor()
{
    return new ResonanceSuppressorEditor (*this);
}

void ResonanceSuppressorProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ResonanceSuppressorProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ResonanceSuppressorProcessor();
}
