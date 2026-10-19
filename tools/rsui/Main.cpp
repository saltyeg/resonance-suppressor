// rsui - renders the plugin editor offscreen to a PNG while audio plays through the processor.
// Nothing is shown on screen.
//
//   rsui <in.wav> <out.png> [--size WxH] [--seconds S] [--click buttonText] ["paramId=value" ...]
//
// paramId=value sets a parameter by ID using its real (denormalised) value.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <iostream>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
void clickButtonWithText (juce::Component& root, const juce::String& text)
{
    for (auto* child : root.getChildren())
    {
        if (auto* b = dynamic_cast<juce::Button*> (child); b != nullptr && b->getButtonText() == text)
        {
            b->triggerClick();
            return;
        }
        clickButtonWithText (*child, text);
    }
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 3)
    {
        std::cerr << "usage: rsui <in.wav> <out.png> [--size WxH] [--seconds S] [--click text] [\"paramId=value\" ...]" << std::endl;
        return 1;
    }

    int width = 980, height = 600;
    double seconds = 3.0;
    juce::StringArray clicks, settings;
    for (int i = 3; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--size" && i + 1 < argc)
        {
            const juce::String v (argv[++i]);
            width = v.upToFirstOccurrenceOf ("x", false, false).getIntValue();
            height = v.fromFirstOccurrenceOf ("x", false, false).getIntValue();
        }
        else if (a == "--seconds" && i + 1 < argc) seconds = juce::String (argv[++i]).getDoubleValue();
        else if (a == "--click" && i + 1 < argc)   clicks.add (argv[++i]);
        else                                       settings.add (a);
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])));
    if (reader == nullptr)
    {
        std::cerr << "error: cannot read " << argv[1] << std::endl;
        return 1;
    }

    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    for (const auto& s : settings)
    {
        const auto id = s.upToFirstOccurrenceOf ("=", false, false);
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (processor->getParameters()[0]); p == nullptr)
            break;
        bool found = false;
        for (auto* param : processor->getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param); ranged != nullptr && ranged->getParameterID() == id)
            {
                ranged->setValueNotifyingHost (ranged->convertTo0to1 (s.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
                found = true;
            }
        if (! found)
            std::cerr << "warning: no parameter " << id << std::endl;
    }

    const double sr = reader->sampleRate;
    constexpr int block = 512;
    processor->setPlayConfigDetails (2, 2, sr, block);
    processor->prepareToPlay (sr, block);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditorIfNeeded());
    editor->setSize (width, height);
    for (const auto& c : clicks)
        clickButtonWithText (*editor, c);

    // Play audio through the processor at roughly real time so the UI timers see a moving signal.
    const auto total = juce::jmin ((juce::int64) (seconds * sr), reader->lengthInSamples);
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    for (juce::int64 pos = 0; pos < total; pos += block)
    {
        buffer.clear();
        reader->read (&buffer, 0, block, pos, true, true);
        processor->processBlock (buffer, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil ((int) (1000.0 * block / sr));
    }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    const juce::File outFile (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    outFile.deleteFile();
    juce::FileOutputStream stream (outFile);
    juce::PNGImageFormat png;
    if (! png.writeImageToStream (image, stream))
    {
        std::cerr << "error: cannot write " << argv[2] << std::endl;
        return 1;
    }
    std::cout << "wrote " << outFile.getFullPathName() << std::endl;
    editor.reset();
    return 0;
}
