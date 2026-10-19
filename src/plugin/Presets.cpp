#include "Presets.h"
#include "Parameters.h"

namespace rs
{
namespace
{
struct Factory
{
    const char* name;
    std::vector<std::pair<juce::String, float>> values; // parameter ID -> real value
};

// Band helper: used + enabled + shape/freq/depth/q/slope for band `n` (1-based).
std::vector<std::pair<juce::String, float>> band (int n, Shape shape, float freq, float depth = 0.0f, float q = 0.7f, float slope = 5.0f)
{
    const int i = n - 1;
    return { { ids::band (i, "Used"), 1.0f }, { ids::band (i, "Enabled"), 1.0f }, { ids::band (i, "Shape"), (float) shape },
             { ids::band (i, "Freq"), freq },  { ids::band (i, "Depth"), depth },  { ids::band (i, "Q"), q },
             { ids::band (i, "Slope"), slope } };
}

std::vector<std::pair<juce::String, float>> unused (int n)
{
    return { { ids::band (n - 1, "Used"), 0.0f } };
}

template <typename... Parts>
std::vector<std::pair<juce::String, float>> join (Parts&&... parts)
{
    std::vector<std::pair<juce::String, float>> out;
    (out.insert (out.end(), parts.begin(), parts.end()), ...);
    return out;
}

using V = std::vector<std::pair<juce::String, float>>;

const std::vector<Factory>& factoryPresets()
{
    static const std::vector<Factory> presets {
        { "Default", {} },
        { "Vocal de-harsh",
          join (V { { ids::depth, 5.5f }, { ids::attack, 5.0f }, { ids::release, 3.5f } },
                band (1, Shape::lowCut, 220.0f), band (2, Shape::bell, 3500.0f, 2.0f, 1.2f), band (3, Shape::highCut, 14000.0f)) },
        { "De-ess",
          join (V { { ids::depth, 6.0f }, { ids::detail, 4.0f }, { ids::attack, 2.0f }, { ids::release, 2.5f } },
                band (1, Shape::lowCut, 4200.0f, 0.0f, 0.7f, 7.0f), band (2, Shape::highShelf, 6000.0f, 3.0f), unused (3)) },
        { "Cymbal tamer",
          join (V { { ids::depth, 6.0f }, { ids::attack, 6.5f }, { ids::release, 4.0f } },
                band (1, Shape::lowCut, 1600.0f), band (2, Shape::bell, 3300.0f, 5.0f, 1.0f), band (3, Shape::highCut, 17000.0f)) },
        { "Guitar mud remover",
          join (V { { ids::depth, 6.0f }, { ids::attack, 8.0f }, { ids::release, 5.0f } },
                band (1, Shape::bandpass, 190.0f, 0.0f, 0.55f, 5.0f), band (2, Shape::tilt, 250.0f, -4.0f, 0.7f), unused (3)) },
        { "Drum bus smoother",
          join (V { { ids::depth, 5.0f }, { ids::attack, 3.0f }, { ids::release, 3.0f }, { ids::stereoMode, 1.0f } },
                band (1, Shape::lowCut, 1000.0f, 0.0f, 0.7f, 4.5f), band (2, Shape::bell, 3000.0f, 3.0f, 1.3f), band (3, Shape::highCut, 16000.0f)) },
        { "Gentle mix bus",
          join (V { { ids::depth, 3.0f }, { ids::detail, 4.0f }, { ids::attack, 6.0f }, { ids::release, 5.0f }, { ids::stereoMode, 1.0f } },
                band (1, Shape::tilt, 600.0f, 0.5f), band (2, Shape::bell, 2800.0f, 3.0f), unused (3)) },
        { "Even low end",
          join (V { { ids::depth, 6.0f }, { ids::attack, 7.5f }, { ids::release, 6.0f } },
                band (1, Shape::highCut, 370.0f, 0.0f, 0.7f, 2.0f), unused (2), unused (3)) },
        { "Bright synth softener",
          join (V { { ids::depth, 5.0f }, { ids::detail, 7.0f }, { ids::attack, 4.0f }, { ids::release, 3.0f } },
                band (1, Shape::lowCut, 2000.0f), band (2, Shape::highShelf, 5000.0f, 2.0f), unused (3)) },
        { "Zero-latency vocal tracking",
          join (V { { ids::depth, 4.0f }, { ids::lowLatency, 1.0f }, { ids::attack, 5.0f }, { ids::release, 3.0f } },
                band (1, Shape::lowCut, 250.0f), band (2, Shape::bell, 1200.0f, 3.0f), band (3, Shape::highCut, 9000.0f)) },
        { "Make room for vocal (sidechain)",
          join (V { { ids::depth, 6.0f }, { ids::attack, 0.5f }, { ids::release, 3.0f }, { ids::sidechain, 1.0f },
                    { ids::stereoMode, 1.0f }, { ids::stereoFocus, 0.2f } },
                band (1, Shape::lowCut, 440.0f, 0.0f, 0.7f, 6.5f), band (2, Shape::bell, 1500.0f, 6.0f, 0.3f), unused (3)) },
    };
    return presets;
}
} // namespace

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s) : state (s) {}

juce::File PresetManager::userDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("resonance-suppressor")
        .getChildFile ("Presets");
}

juce::StringArray PresetManager::getFactoryNames() const
{
    juce::StringArray names;
    for (const auto& p : factoryPresets())
        names.add (p.name);
    return names;
}

juce::StringArray PresetManager::getUserNames() const
{
    juce::StringArray names;
    for (const auto& f : userDirectory().findChildFiles (juce::File::findFiles, false, "*.xml"))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

void PresetManager::resetToDefaults()
{
    for (auto* p : state.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && ranged->getParameterID() != ids::bypass)
            ranged->setValueNotifyingHost (ranged->getDefaultValue());
}

void PresetManager::loadFactory (const juce::String& name)
{
    for (const auto& preset : factoryPresets())
    {
        if (name != preset.name)
            continue;
        resetToDefaults();
        for (const auto& [id, value] : preset.values)
            if (auto* p = state.getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (value));
        current = name;
        return;
    }
}

void PresetManager::loadUser (const juce::String& name)
{
    if (auto xml = juce::XmlDocument::parse (userDirectory().getChildFile (name + ".xml")))
        if (xml->hasTagName (state.state.getType()))
        {
            state.replaceState (juce::ValueTree::fromXml (*xml));
            current = name;
        }
}

bool PresetManager::saveUser (const juce::String& name)
{
    const auto safe = juce::File::createLegalFileName (name.trim());
    if (safe.isEmpty())
        return false;
    userDirectory().createDirectory();
    auto xml = state.copyState().createXml();
    if (xml == nullptr || ! xml->writeTo (userDirectory().getChildFile (safe + ".xml")))
        return false;
    current = safe;
    return true;
}

bool PresetManager::deleteUser (const juce::String& name)
{
    return userDirectory().getChildFile (name + ".xml").deleteFile();
}
} // namespace rs
