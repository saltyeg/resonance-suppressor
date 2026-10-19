#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace rs
{
// Factory presets (defined in code as parameter overrides on top of the defaults) and user presets
// (APVTS state XML files in %APPDATA%/resonance-suppressor/Presets).
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState&);

    juce::StringArray getFactoryNames() const;
    juce::StringArray getUserNames() const;

    void loadFactory (const juce::String& name);
    void loadUser (const juce::String& name);
    bool saveUser (const juce::String& name);
    bool deleteUser (const juce::String& name);

    juce::String getCurrentName() const { return current; }
    static juce::File userDirectory();

private:
    void resetToDefaults();

    juce::AudioProcessorValueTreeState& state;
    juce::String current = "Default";
};
} // namespace rs
