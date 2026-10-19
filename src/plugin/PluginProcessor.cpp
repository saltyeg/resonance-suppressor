#include "PluginProcessor.h"
#include "PluginEditor.h"

ResonanceSuppressorProcessor::ResonanceSuppressorProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, &undoManager, "STATE", rs::createParameterLayout())
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

void ResonanceSuppressorProcessor::switchToSlot (int slot)
{
    if (slot == activeSlot)
        return;
    slots[(size_t) activeSlot] = apvts.copyState();
    if (slots[(size_t) slot].isValid())
        apvts.replaceState (slots[(size_t) slot].createCopy());
    activeSlot = slot;
}

void ResonanceSuppressorProcessor::copyActiveToOther()
{
    slots[(size_t) (1 - activeSlot)] = apvts.copyState();
}

// State layout: <RS activeSlot=".." preset=".."> <STATE .../> <SLOT><STATE .../></SLOT> </RS>
void ResonanceSuppressorProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("RS");
    root.setProperty ("activeSlot", activeSlot, nullptr);
    root.setProperty ("preset", presets.getCurrentName(), nullptr);
    root.appendChild (apvts.copyState(), nullptr);
    if (const auto& other = slots[(size_t) (1 - activeSlot)]; other.isValid())
    {
        juce::ValueTree slot ("SLOT");
        slot.appendChild (other.createCopy(), nullptr);
        root.appendChild (slot, nullptr);
    }

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void ResonanceSuppressorProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto root = juce::ValueTree::fromXml (*xml);
    if (root.hasType (apvts.state.getType())) // bare APVTS state
    {
        apvts.replaceState (root);
        return;
    }
    if (! root.hasType ("RS"))
        return;

    if (const auto current = root.getChildWithName (apvts.state.getType()); current.isValid())
        apvts.replaceState (current);
    activeSlot = juce::jlimit (0, 1, (int) root.getProperty ("activeSlot", 0));
    slots[(size_t) (1 - activeSlot)] = root.getChildWithName ("SLOT").getChildWithName (apvts.state.getType()).createCopy();
    undoManager.clearUndoHistory();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ResonanceSuppressorProcessor();
}
