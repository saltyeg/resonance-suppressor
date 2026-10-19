// rsdsp - runs the DSP core directly (no plugin wrapper) for fast tuning experiments.
//
//   rsdsp <in.wav> <out.wav> ["Name=value" ...] ["tune:key=value" ...]
//
// Names match the plugin's parameter names (Depth, Detail, Mode, Attack, ...). Unknown names are
// ignored with a warning so the same settings lists work for soothe3 and for us. Output is latency
// compensated, 32-bit float.

#include <juce_audio_formats/juce_audio_formats.h>

#include "dsp/Suppressor.h"

#include <iostream>

namespace
{
bool isOn (const juce::String& v) { return v.equalsIgnoreCase ("on") || v == "1" || v.equalsIgnoreCase ("true"); }

bool applySetting (rs::Params& p, rs::Tuning& t, const juce::String& setting)
{
    const auto name = setting.upToFirstOccurrenceOf ("=", false, false).trim();
    const auto value = setting.fromFirstOccurrenceOf ("=", false, false).trim();
    const float v = value.getFloatValue();

    if (name.startsWith ("tune:"))
        return t.set (name.substring (5), v);

    if (name == "Mode")               p.mode = value.equalsIgnoreCase ("hard") ? rs::Mode::hard : rs::Mode::soft;
    else if (name == "Depth")         p.depth = v;
    else if (name == "Detail")        p.detail = v;
    else if (name == "Sharpness")     p.sharpnessOffset = v;
    else if (name == "Selectivity")   p.selectivityOffset = v;
    else if (name == "Attack")        p.attack = v;
    else if (name == "Release")       p.release = v;
    else if (name.startsWith ("Detail tilt") || name.startsWith ("Attack tilt") || name.startsWith ("Release tilt"))
    {
        // Accept "norm:x" (0..1 -> 0.25x..4x) or a multiplier.
        const float m = value.startsWith ("norm:") ? std::exp2 (4.0f * value.substring (5).getFloatValue() - 2.0f) : v;
        const bool low = name.endsWith ("low");
        if (name.startsWith ("Detail"))       (low ? p.detailTiltLow : p.detailTiltHigh) = m;
        else if (name.startsWith ("Attack"))  (low ? p.attackTiltLow : p.attackTiltHigh) = m;
        else                                  (low ? p.releaseTiltLow : p.releaseTiltHigh) = m;
    }
    else if (name == "Max cut")       p.maxCutDb = value.equalsIgnoreCase ("off") ? 40.0f : v;
    else if (name == "Wet trim")      p.wetTrimDb = v;
    else if (name == "Mix")           p.mix = v * 0.01f;
    else if (name == "Out trim")      p.outTrimDb = v;
    else if (name == "Delta")         p.delta = isOn (value);
    else if (name == "Quality")       p.quality = value == "ultra" ? rs::Quality::ultra : value == "high" ? rs::Quality::high : rs::Quality::normal;
    else if (name == "Linear phase")  p.linearPhase = isOn (value);
    else if (name == "Low latency")   p.lowLatency = isOn (value);
    else if (name == "Stereo mode")   p.stereoMode = value.equalsIgnoreCase ("M/S") ? rs::StereoMode::midSide : rs::StereoMode::leftRight;
    else if (name == "Stereo focus")  p.stereoFocus = value.startsWith ("norm:") ? value.substring (5).getFloatValue() : v;
    else if (name.startsWith ("Band "))
    {
        // "Band N field": used, enabled, shape, frequency, depth, q, slope, focus
        const int index = name.fromFirstOccurrenceOf ("Band ", false, false).getIntValue() - 1;
        const auto field = name.fromLastOccurrenceOf (" ", false, false);
        if (index < 0 || index >= rs::numBands)
            return false;
        auto& b = p.bands[(size_t) index];
        const auto norm = value.startsWith ("norm:") ? value.substring (5).getFloatValue() : -1.0f;
        if (field == "used")            b.used = isOn (value);
        else if (field == "enabled")    b.enabled = isOn (value);
        else if (field == "shape")
        {
            for (int s = 0; s < rs::numShapes; ++s)
                if (value.equalsIgnoreCase (rs::shapeNames[s]))
                    b.shape = (rs::Shape) s;
        }
        else if (field == "frequency")  b.freq = norm >= 0.0f ? 20.0f * std::pow (1000.0f, norm) : v;
        else if (field == "depth")      b.depth = norm >= 0.0f ? -10.0f + 20.0f * norm : v;
        else if (field == "q")          b.q = norm >= 0.0f ? 0.1f * std::pow (100.0f, norm) : v;
        else if (field == "slope")      b.slope = norm >= 0.0f ? 10.0f * norm : v;
        else if (field == "focus")      b.focus = norm >= 0.0f ? norm : v;
        else return false;
    }
    else if (name == "Stereo link")   p.stereoLink = v * 0.01f;
    else
        return false;
    return true;
}
} // namespace

int main (int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cerr << "usage: rsdsp <in.wav> <out.wav> [\"Name=value\" ...] [\"tune:key=value\" ...]" << std::endl;
        return 1;
    }

    rs::Params params;
    rs::Tuning tuning;

    // Same factory bands as the plugin (soothe3's defaults): low cut 100 Hz, bell 4 kHz +2.5, high cut 16 kHz.
    params.bands[0] = { true, true, rs::Shape::lowCut, 100.0f, 0.0f, 0.7f, 5.0f, 0.5f };
    params.bands[1] = { true, true, rs::Shape::bell, 4000.0f, 2.5f, 0.7f, 5.0f, 0.5f };
    params.bands[2] = { true, true, rs::Shape::highCut, 16000.0f, 0.0f, 0.7f, 5.0f, 0.5f };
    for (int i = 3; i < argc; ++i)
        if (! applySetting (params, tuning, juce::String::fromUTF8 (argv[i])))
            std::cerr << "warning: ignored " << argv[i] << std::endl;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])));
    if (reader == nullptr)
    {
        std::cerr << "error: cannot read " << argv[1] << std::endl;
        return 1;
    }

    const int n = (int) reader->lengthInSamples;
    const double sr = reader->sampleRate;
    juce::AudioBuffer<float> input (2, n);
    reader->read (&input, 0, n, 0, true, true);

    rs::Suppressor dsp;
    dsp.setParams (params);
    dsp.setTuning (tuning);
    dsp.prepare (sr, 2);
    const int latency = dsp.getLatencySamples();

    juce::AudioBuffer<float> work (2, n + latency);
    work.clear();
    for (int ch = 0; ch < 2; ++ch)
        work.copyFrom (ch, 0, input, ch, 0, n);

    constexpr int block = 512;
    for (int pos = 0; pos < work.getNumSamples(); pos += block)
    {
        juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), 2, pos, juce::jmin (block, work.getNumSamples() - pos));
        dsp.process (view);
    }

    juce::AudioBuffer<float> output (2, n);
    for (int ch = 0; ch < 2; ++ch)
        output.copyFrom (ch, 0, work, ch, latency, n);

    const juce::File outFile (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile();
    auto* stream = new juce::FileOutputStream (outFile);
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream, sr, 2, 32, {}, 0));
    if (writer == nullptr)
    {
        delete stream;
        std::cerr << "error: cannot write " << argv[2] << std::endl;
        return 1;
    }
    writer->writeFromAudioSampleBuffer (output, 0, n);
    std::cout << "latency " << latency << std::endl;
    return 0;
}
