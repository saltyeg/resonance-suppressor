// rshost - headless VST3 host for offline renders.
//
//   rshost params <plugin.vst3>
//   rshost render <plugin.vst3> <in.wav> <out.wav> [options]
//
// render options:
//   --set "Name=value"     set a parameter by name (value as display text, or norm:0..1); repeatable
//   --sidechain <sc.wav>   feed a file into the plugin's second input bus
//   --block <n>            processing block size (default 512)
//   --load-state <file>    restore plugin state before applying --set
//   --save-state <file>    write plugin state after applying --set
//   --lenient              skip --set entries naming parameters the plugin doesn't have
//
// The output is compensated for the plugin's reported latency, so it lines up with the input.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <iostream>

namespace
{
int fail (const juce::String& message)
{
    std::cerr << "error: " << message << std::endl;
    return 1;
}

std::unique_ptr<juce::AudioPluginInstance> loadPlugin (const juce::String& path, double sampleRate, int blockSize, juce::String& error)
{
    juce::AudioPluginFormatManager formats;
    formats.addFormat (new juce::VST3PluginFormat());

    juce::OwnedArray<juce::PluginDescription> types;
    for (auto* format : formats.getFormats())
        format->findAllTypesForFile (types, path);

    if (types.isEmpty())
    {
        error = "no plugin found in " + path;
        return nullptr;
    }

    return formats.createPluginInstance (*types[0], sampleRate, blockSize, error);
}

juce::AudioProcessorParameter* findParameter (juce::AudioPluginInstance& plugin, const juce::String& name)
{
    for (auto* p : plugin.getParameters())
        if (p->getName (256).equalsIgnoreCase (name))
            return p;
    return nullptr;
}

bool applySetting (juce::AudioPluginInstance& plugin, const juce::String& setting, bool lenient)
{
    const auto name = setting.upToFirstOccurrenceOf ("=", false, false).trim();
    const auto text = setting.fromFirstOccurrenceOf ("=", false, false).trim();

    auto* param = findParameter (plugin, name);
    if (param == nullptr)
    {
        std::cerr << (lenient ? "warning" : "error") << ": no parameter named '" << name << "'" << std::endl;
        return lenient;
    }

    auto norm = text.startsWithIgnoreCase ("norm:") ? text.substring (5).getFloatValue()
                                                    : param->getValueForText (text);

    // Many plugins don't implement text->value for switches/choices; match against each step's label instead.
    const int steps = param->getNumSteps();
    if (! text.startsWithIgnoreCase ("norm:") && steps > 1 && steps < 1000)
    {
        for (int k = 0; k < steps; ++k)
        {
            const auto v = (float) k / (float) (steps - 1);
            if (param->getText (v, 256).trim().equalsIgnoreCase (text))
            {
                norm = v;
                break;
            }
        }
    }
    param->setValue (juce::jlimit (0.0f, 1.0f, norm));
    std::cout << "  " << name << " = " << param->getCurrentValueAsText() << " (norm " << param->getValue() << ")" << std::endl;
    return true;
}

std::unique_ptr<juce::AudioFormatReader> openWav (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    return std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file));
}

int listParameters (const juce::StringArray& args)
{
    if (args.size() < 2)
        return fail ("usage: rshost params <plugin.vst3>");

    juce::String error;
    auto plugin = loadPlugin (args[1], 48000.0, 512, error);
    if (plugin == nullptr)
        return fail (error);

    std::cout << plugin->getName() << " - " << plugin->getParameters().size() << " parameters" << std::endl;
    for (auto* p : plugin->getParameters())
        std::cout << p->getParameterIndex() << "\t" << p->getName (256) << "\t" << p->getCurrentValueAsText()
                  << "\tnorm=" << p->getValue() << "\tsteps=" << p->getNumSteps() << std::endl;
    return 0;
}

int render (const juce::StringArray& args)
{
    if (args.size() < 4)
        return fail ("usage: rshost render <plugin.vst3> <in.wav> <out.wav> [options]");

    const juce::File inFile (juce::File::getCurrentWorkingDirectory().getChildFile (args[2]));
    const juce::File outFile (juce::File::getCurrentWorkingDirectory().getChildFile (args[3]));

    juce::StringArray settings;
    juce::String sidechainPath, loadState, saveState;
    int blockSize = 512;
    bool lenient = false;

    for (int i = 4; i < args.size(); ++i)
    {
        const auto& a = args[i];
        const auto next = [&] { return i + 1 < args.size() ? args[++i] : juce::String(); };

        if (a == "--set")             settings.add (next());
        else if (a == "--sidechain")  sidechainPath = next();
        else if (a == "--block")      blockSize = next().getIntValue();
        else if (a == "--load-state") loadState = next();
        else if (a == "--save-state") saveState = next();
        else if (a == "--lenient")    lenient = true;
        else return fail ("unknown option " + a);
    }

    auto reader = openWav (inFile);
    if (reader == nullptr)
        return fail ("cannot read " + inFile.getFullPathName());

    const auto sampleRate = reader->sampleRate;
    const auto numSamples = (int) reader->lengthInSamples;
    const int numChannels = 2;

    juce::AudioBuffer<float> input (numChannels, numSamples);
    reader->read (&input, 0, numSamples, 0, true, true); // mono files are duplicated to both channels

    juce::AudioBuffer<float> sidechain;
    if (sidechainPath.isNotEmpty())
    {
        auto scReader = openWav (juce::File::getCurrentWorkingDirectory().getChildFile (sidechainPath));
        if (scReader == nullptr)
            return fail ("cannot read sidechain " + sidechainPath);
        sidechain.setSize (numChannels, numSamples);
        sidechain.clear();
        scReader->read (&sidechain, 0, juce::jmin (numSamples, (int) scReader->lengthInSamples), 0, true, true);
    }

    juce::String error;
    auto plugin = loadPlugin (args[1], sampleRate, blockSize, error);
    if (plugin == nullptr)
        return fail (error);

    auto layout = plugin->getBusesLayout();
    layout.getMainInputChannelSet() = juce::AudioChannelSet::stereo();
    layout.getMainOutputChannelSet() = juce::AudioChannelSet::stereo();
    for (int bus = 1; bus < layout.inputBuses.size(); ++bus)
        layout.inputBuses.getReference (bus) = (bus == 1 && sidechain.getNumChannels() > 0) ? juce::AudioChannelSet::stereo()
                                                                                            : juce::AudioChannelSet::disabled();
    for (int bus = 1; bus < layout.outputBuses.size(); ++bus)
        layout.outputBuses.getReference (bus) = juce::AudioChannelSet::disabled();

    if (! plugin->setBusesLayout (layout))
        std::cerr << "warning: plugin rejected the requested bus layout, using its default" << std::endl;

    if (loadState.isNotEmpty())
    {
        juce::MemoryBlock state;
        if (! juce::File (loadState).loadFileAsData (state))
            return fail ("cannot read state " + loadState);
        plugin->setStateInformation (state.getData(), (int) state.getSize());
    }

    std::cout << "plugin: " << plugin->getName() << std::endl;
    for (const auto& s : settings)
        if (! applySetting (*plugin, s, lenient))
            return 1;

    if (saveState.isNotEmpty())
    {
        juce::MemoryBlock state;
        plugin->getStateInformation (state);
        juce::File (saveState).replaceWithData (state.getData(), state.getSize());
    }

    plugin->setNonRealtime (true);
    plugin->setPlayConfigDetails (plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels(), sampleRate, blockSize);
    plugin->prepareToPlay (sampleRate, blockSize);

    const int latency = plugin->getLatencySamples();
    const int totalSamples = numSamples + latency;
    std::cout << "rate " << sampleRate << " Hz, " << numSamples << " samples, latency " << latency << " samples" << std::endl;

    juce::AudioBuffer<float> output (numChannels, numSamples);
    output.clear();

    const int bufferChannels = juce::jmax (plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
    juce::AudioBuffer<float> block (bufferChannels, blockSize);
    juce::MidiBuffer midi;

    for (int pos = 0; pos < totalSamples; pos += blockSize)
    {
        const int n = juce::jmin (blockSize, totalSamples - pos);
        block.setSize (bufferChannels, n, false, false, true);
        block.clear();

        const int available = juce::jmax (0, juce::jmin (n, numSamples - pos));

        auto mainIn = plugin->getBusBuffer (block, true, 0);
        for (int ch = 0; ch < juce::jmin (numChannels, mainIn.getNumChannels()); ++ch)
            if (available > 0)
                mainIn.copyFrom (ch, 0, input, ch, pos, available);

        if (sidechain.getNumChannels() > 0 && plugin->getBusCount (true) > 1 && plugin->getBus (true, 1)->isEnabled())
        {
            auto scIn = plugin->getBusBuffer (block, true, 1);
            for (int ch = 0; ch < juce::jmin (numChannels, scIn.getNumChannels()); ++ch)
                if (available > 0)
                    scIn.copyFrom (ch, 0, sidechain, ch, pos, available);
        }

        midi.clear();
        plugin->processBlock (block, midi);

        // Drop the first `latency` samples so output sample i matches input sample i.
        auto mainOut = plugin->getBusBuffer (block, false, 0);
        for (int i = 0; i < n; ++i)
        {
            const int dest = pos + i - latency;
            if (dest < 0 || dest >= numSamples)
                continue;
            for (int ch = 0; ch < juce::jmin (numChannels, mainOut.getNumChannels()); ++ch)
                output.setSample (ch, dest, mainOut.getSample (ch, i));
        }
    }

    plugin->releaseResources();

    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (new juce::FileOutputStream (outFile));
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sampleRate, (unsigned int) numChannels, 32, {}, 0));
    if (writer == nullptr)
        return fail ("cannot write " + outFile.getFullPathName());
    stream.release(); // the writer owns the stream now
    writer->writeFromAudioSampleBuffer (output, 0, numSamples);

    std::cout << "wrote " << outFile.getFullPathName() << std::endl;
    return 0;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));

    if (args.isEmpty())
        return fail ("usage: rshost params|render ...");

    if (args[0] == "params") return listParameters (args);
    if (args[0] == "render") return render (args);
    return fail ("unknown command " + args[0]);
}
