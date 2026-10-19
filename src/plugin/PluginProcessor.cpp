#include "PluginProcessor.h"
#include "PluginEditor.h"

ResonanceSuppressorProcessor::ResonanceSuppressorProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "STATE", rs::createParameterLayout())
{
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

void ResonanceSuppressorProcessor::prepareToPlay (double sampleRate, int)
{
    suppressor.setParams (parameters.read());
    suppressor.prepare (sampleRate, getMainBusNumOutputChannels());
    setLatencySamples (suppressor.getLatencySamples());
}

void ResonanceSuppressorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto main = getBusBuffer (buffer, true, 0);
    suppressor.setParams (parameters.read());

    // Sidechain: the second input bus drives detection when enabled and connected.
    const bool useSidechain = parameters.sidechainEnabled() && getBusCount (true) > 1
                              && getBus (true, 1)->isEnabled() && getBus (true, 1)->getNumberOfChannels() > 0;
    if (useSidechain)
    {
        const auto sc = getBusBuffer (buffer, true, 1);
        suppressor.process (main, &sc);
    }
    else
    {
        suppressor.process (main);
    }

    // Linear phase changes the latency; let the host know.
    if (suppressor.getLatencySamples() != getLatencySamples())
        setLatencySamples (suppressor.getLatencySamples());
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
