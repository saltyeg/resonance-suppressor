#include "Parameters.h"

namespace rs
{
namespace
{
using Apvts = juce::AudioProcessorValueTreeState;
using Float = juce::AudioParameterFloat;
using Attributes = juce::AudioParameterFloatAttributes;

juce::ParameterID pid (const juce::String& id) { return { id, 1 }; }

juce::String percent (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; }

// "100 / 0" .. "even" .. "0 / 100", like soothe3's focus display.
juce::String focusText (float v, int)
{
    if (std::abs (v - 0.5f) < 0.005f)
        return "even";
    const int a = juce::roundToInt (100.0f * juce::jmin (1.0f, 2.0f * (1.0f - v)));
    const int b = juce::roundToInt (100.0f * juce::jmin (1.0f, 2.0f * v));
    return juce::String (a) + " / " + juce::String (b);
}

juce::String hzText (float v, int)
{
    return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
}

float hzFromText (const juce::String& s)
{
    const float v = s.getFloatValue();
    return s.containsIgnoreCase ("k") ? v * 1000.0f : v;
}

juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float a, float b, float n) { return a * std::pow (b / a, n); },
             [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); } };
}

std::unique_ptr<Float> knob (const char* id, const char* name, float def)
{
    return std::make_unique<Float> (pid (id), name, juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), def,
                                    Attributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1); }));
}

std::unique_ptr<Float> offset (const char* id, const char* name)
{
    return std::make_unique<Float> (pid (id), name, juce::NormalisableRange<float> (-5.0f, 5.0f, 0.01f), 0.0f,
                                    Attributes().withStringFromValueFunction ([] (float v, int) {
                                        return v == 0.0f ? juce::String ("auto") : juce::String (v, 1);
                                    }));
}

std::unique_ptr<Float> tilt (const char* id, const char* name)
{
    // 0.25x .. 4x with 1x in the middle.
    juce::NormalisableRange<float> range (
        0.25f, 4.0f,
        [] (float, float, float n) { return std::exp2 (4.0f * n - 2.0f); },
        [] (float, float, float v) { return (std::log2 (v) + 2.0f) / 4.0f; });
    return std::make_unique<Float> (pid (id), name, range, 1.0f,
                                    Attributes()
                                        .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + "x"; })
                                        .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); }));
}

std::unique_ptr<Float> decibels (const char* id, const char* name, float lo, float hi)
{
    return std::make_unique<Float> (pid (id), name, juce::NormalisableRange<float> (lo, hi, 0.01f), 0.0f,
                                    Attributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) {
                                        return juce::String (v, 1) + " dB";
                                    }));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    Apvts::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice> (pid (ids::mode), "Mode", juce::StringArray { "soft", "hard" }, 0));
    layout.add (knob (ids::depth, "Depth", 5.0f));
    layout.add (knob (ids::detail, "Detail", 5.0f));
    layout.add (offset (ids::sharpness, "Sharpness"));
    layout.add (offset (ids::selectivity, "Selectivity"));
    layout.add (knob (ids::attack, "Attack", 5.0f));
    layout.add (knob (ids::release, "Release", 3.0f));
    layout.add (tilt (ids::detailTiltLow, "Detail tilt low"));
    layout.add (tilt (ids::detailTiltHigh, "Detail tilt high"));
    layout.add (tilt (ids::attackTiltLow, "Attack tilt low"));
    layout.add (tilt (ids::attackTiltHigh, "Attack tilt high"));
    layout.add (tilt (ids::releaseTiltLow, "Release tilt low"));
    layout.add (tilt (ids::releaseTiltHigh, "Release tilt high"));

    layout.add (std::make_unique<Float> (
        pid (ids::maxCut), "Max cut", juce::NormalisableRange<float> (0.0f, maxCutOff, 0.01f), maxCutOff,
        Attributes().withStringFromValueFunction ([] (float v, int) {
            return v >= maxCutOff ? juce::String ("off") : juce::String (v, 1) + " dB";
        })));
    layout.add (decibels (ids::wetTrim, "Wet trim", -12.0f, 12.0f));
    layout.add (std::make_unique<Float> (
        pid (ids::mix), "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        Attributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; })));
    layout.add (decibels (ids::outTrim, "Out trim", -24.0f, 24.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (pid (ids::delta), "Delta", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (pid (ids::bypass), "Bypass", false));

    layout.add (std::make_unique<juce::AudioParameterChoice> (pid (ids::quality), "Quality", juce::StringArray { "normal", "high", "ultra" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (pid (ids::linearPhase), "Linear phase", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (pid (ids::lowLatency), "Low latency", false));
    layout.add (std::make_unique<Float> (
        pid (ids::stereoLink), "Stereo link", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        Attributes().withStringFromValueFunction (percent)));
    layout.add (std::make_unique<juce::AudioParameterChoice> (pid (ids::stereoMode), "Stereo mode", juce::StringArray { "L/R", "M/S" }, 0));
    layout.add (std::make_unique<Float> (pid (ids::stereoFocus), "Stereo focus", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f,
                                         Attributes().withStringFromValueFunction (focusText)));
    layout.add (std::make_unique<juce::AudioParameterBool> (pid (ids::sidechain), "Sidechain", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (pid (ids::sidechainListen), "Sidechain listen", false));

    // Bands. Defaults mirror soothe3's factory state: low cut 100 Hz, bell 4 kHz (+2.5), high cut 16 kHz.
    juce::StringArray shapes;
    for (auto* name : shapeNames)
        shapes.add (name);

    struct Default { bool used; Shape shape; float freq, depth; };
    const Default defaults[numBands] = {
        { true, Shape::lowCut, 100.0f, 0.0f }, { true, Shape::bell, 4000.0f, 2.5f }, { true, Shape::highCut, 16000.0f, 0.0f },
        { false, Shape::bell, 1000.0f, 0.0f }, { false, Shape::bell, 1000.0f, 0.0f }, { false, Shape::bell, 1000.0f, 0.0f },
        { false, Shape::bell, 1000.0f, 0.0f }, { false, Shape::bell, 1000.0f, 0.0f },
    };

    for (int b = 0; b < numBands; ++b)
    {
        const auto prefix = "Band " + juce::String (b + 1) + " ";
        const auto& d = defaults[b];
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("band" + juce::String (b + 1), prefix.trim(), "|");
        group->addChild (std::make_unique<juce::AudioParameterBool> (pid (ids::band (b, "Used")), prefix + "used", d.used));
        group->addChild (std::make_unique<juce::AudioParameterBool> (pid (ids::band (b, "Enabled")), prefix + "enabled", true));
        group->addChild (std::make_unique<juce::AudioParameterChoice> (pid (ids::band (b, "Shape")), prefix + "shape", shapes, (int) d.shape));
        group->addChild (std::make_unique<Float> (pid (ids::band (b, "Freq")), prefix + "frequency", logRange (20.0f, 20000.0f), d.freq,
                                                  Attributes().withStringFromValueFunction (hzText).withValueFromStringFunction (hzFromText)));
        group->addChild (std::make_unique<Float> (pid (ids::band (b, "Depth")), prefix + "depth", juce::NormalisableRange<float> (-10.0f, 10.0f, 0.01f), d.depth,
                                                  Attributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1); })));
        group->addChild (std::make_unique<Float> (pid (ids::band (b, "Q")), prefix + "q", logRange (0.1f, 10.0f), 0.7f,
                                                  Attributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, v < 1.0f ? 2 : 1); })));
        group->addChild (std::make_unique<Float> (pid (ids::band (b, "Slope")), prefix + "slope", juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 5.0f,
                                                  Attributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1); })));
        group->addChild (std::make_unique<Float> (pid (ids::band (b, "Focus")), prefix + "focus", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f,
                                                  Attributes().withStringFromValueFunction (focusText)));
        layout.add (std::move (group));
    }

    return layout;
}

ParameterReader::ParameterReader (juce::AudioProcessorValueTreeState& s)
    : mode (get (s, ids::mode)), depth (get (s, ids::depth)), detail (get (s, ids::detail)),
      sharpness (get (s, ids::sharpness)), selectivity (get (s, ids::selectivity)),
      attack (get (s, ids::attack)), release (get (s, ids::release)),
      dTiltLo (get (s, ids::detailTiltLow)), dTiltHi (get (s, ids::detailTiltHigh)),
      aTiltLo (get (s, ids::attackTiltLow)), aTiltHi (get (s, ids::attackTiltHigh)),
      rTiltLo (get (s, ids::releaseTiltLow)), rTiltHi (get (s, ids::releaseTiltHigh)),
      maxCut (get (s, ids::maxCut)), wetTrim (get (s, ids::wetTrim)), mix (get (s, ids::mix)),
      outTrim (get (s, ids::outTrim)), delta (get (s, ids::delta)), bypass (get (s, ids::bypass)),
      quality (get (s, ids::quality)), linearPhase (get (s, ids::linearPhase)), lowLatency (get (s, ids::lowLatency)), stereoLink (get (s, ids::stereoLink)),
      stereoMode (get (s, ids::stereoMode)), stereoFocus (get (s, ids::stereoFocus)),
      sidechain (get (s, ids::sidechain)), sidechainListen (get (s, ids::sidechainListen))
{
    for (int b = 0; b < numBands; ++b)
        bands[(size_t) b] = { get (s, ids::band (b, "Used")), get (s, ids::band (b, "Enabled")), get (s, ids::band (b, "Shape")),
                              get (s, ids::band (b, "Freq")), get (s, ids::band (b, "Depth")), get (s, ids::band (b, "Q")),
                              get (s, ids::band (b, "Slope")), get (s, ids::band (b, "Focus")) };
}

Params ParameterReader::read() const
{
    Params p;
    p.mode = mode->load() > 0.5f ? Mode::hard : Mode::soft;
    p.depth = depth->load();
    p.detail = detail->load();
    p.sharpnessOffset = sharpness->load();
    p.selectivityOffset = selectivity->load();
    p.attack = attack->load();
    p.release = release->load();
    p.detailTiltLow = dTiltLo->load();
    p.detailTiltHigh = dTiltHi->load();
    p.attackTiltLow = aTiltLo->load();
    p.attackTiltHigh = aTiltHi->load();
    p.releaseTiltLow = rTiltLo->load();
    p.releaseTiltHigh = rTiltHi->load();
    const float mc = maxCut->load();
    p.maxCutDb = mc >= maxCutOff ? 40.0f : mc;
    p.wetTrimDb = wetTrim->load();
    p.mix = mix->load() * 0.01f;
    p.outTrimDb = outTrim->load();
    p.delta = delta->load() > 0.5f;
    p.quality = (Quality) juce::jlimit (0, 2, juce::roundToInt (quality->load()));
    p.linearPhase = linearPhase->load() > 0.5f;
    p.lowLatency = lowLatency->load() > 0.5f;
    p.stereoLink = stereoLink->load() * 0.01f;
    p.stereoMode = stereoMode->load() > 0.5f ? StereoMode::midSide : StereoMode::leftRight;
    p.stereoFocus = stereoFocus->load();
    p.sidechainListen = sidechainListen->load() > 0.5f && sidechainEnabled();

    for (int b = 0; b < numBands; ++b)
    {
        const auto& src = bands[(size_t) b];
        auto& dst = p.bands[(size_t) b];
        dst.used = src.used->load() > 0.5f;
        dst.enabled = src.enabled->load() > 0.5f;
        dst.shape = (Shape) juce::jlimit (0, numShapes - 1, juce::roundToInt (src.shape->load()));
        dst.freq = src.freq->load();
        dst.depth = src.depth->load();
        dst.q = src.q->load();
        dst.slope = src.slope->load();
        dst.focus = src.focus->load();
    }

    if (bypassed())
    {
        // Soft bypass: fade to the latency-aligned dry signal.
        p.mix = 0.0f;
        p.delta = false;
        p.wetTrimDb = 0.0f;
        p.outTrimDb = 0.0f;
    }
    return p;
}
} // namespace rs
