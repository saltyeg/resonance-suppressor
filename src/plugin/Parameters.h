#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Suppressor.h"

// Parameter IDs and the APVTS layout. Display names mirror soothe3's where the feature exists in both,
// so tools/render_matrix.py can drive either plugin with the same settings.
namespace rs::ids
{
inline constexpr auto mode = "mode";
inline constexpr auto depth = "depth";
inline constexpr auto detail = "detail";
inline constexpr auto sharpness = "sharpness";
inline constexpr auto selectivity = "selectivity";
inline constexpr auto attack = "attack";
inline constexpr auto release = "release";
inline constexpr auto detailTiltLow = "detailTiltLow";
inline constexpr auto detailTiltHigh = "detailTiltHigh";
inline constexpr auto attackTiltLow = "attackTiltLow";
inline constexpr auto attackTiltHigh = "attackTiltHigh";
inline constexpr auto releaseTiltLow = "releaseTiltLow";
inline constexpr auto releaseTiltHigh = "releaseTiltHigh";
inline constexpr auto maxCut = "maxCut";
inline constexpr auto wetTrim = "wetTrim";
inline constexpr auto mix = "mix";
inline constexpr auto outTrim = "outTrim";
inline constexpr auto delta = "delta";
inline constexpr auto bypass = "bypass";
inline constexpr auto quality = "quality";
inline constexpr auto linearPhase = "linearPhase";
inline constexpr auto lowLatency = "lowLatency";
inline constexpr auto stereoLink = "stereoLink";
inline constexpr auto stereoMode = "stereoMode";
inline constexpr auto stereoFocus = "stereoFocus";
inline constexpr auto sidechain = "sidechain";
inline constexpr auto sidechainListen = "sidechainListen";

// Band parameters: "band<N><Field>", N = 1..8.
inline juce::String band (int index, const char* field) { return "band" + juce::String (index + 1) + field; }
} // namespace rs::ids

namespace rs
{
inline constexpr float maxCutOff = 24.0f; // top of the Max cut range means "no limit"

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// Reads the current parameter values into the DSP parameter struct (audio thread safe).
class ParameterReader
{
public:
    explicit ParameterReader (juce::AudioProcessorValueTreeState& apvts);
    Params read() const;
    bool bypassed() const { return bypass->load() > 0.5f; }
    bool sidechainEnabled() const { return sidechain->load() > 0.5f; }

private:
    std::atomic<float>* get (juce::AudioProcessorValueTreeState& s, const juce::String& id) { return s.getRawParameterValue (id); }

    std::atomic<float> *mode, *depth, *detail, *sharpness, *selectivity, *attack, *release;
    std::atomic<float> *dTiltLo, *dTiltHi, *aTiltLo, *aTiltHi, *rTiltLo, *rTiltHi;
    std::atomic<float> *maxCut, *wetTrim, *mix, *outTrim, *delta, *bypass, *quality, *linearPhase, *lowLatency, *stereoLink;
    std::atomic<float> *stereoMode, *stereoFocus, *sidechain, *sidechainListen;

    struct BandParams { std::atomic<float> *used, *enabled, *shape, *freq, *depth, *q, *slope, *focus; };
    std::array<BandParams, numBands> bands;
};
} // namespace rs
