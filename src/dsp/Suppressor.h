#pragma once

#include <juce_dsp/juce_dsp.h>

#include "Bands.h"

#include <array>
#include <atomic>
#include <complex>
#include <tuple>
#include <vector>

namespace rs
{
enum class Mode { soft, hard };
enum class Quality { normal, high, ultra };
enum class StereoMode { leftRight, midSide };

// Unitless knobs follow soothe3's ranges (0..10); tilts are multipliers (0.25x..4x).
struct Params
{
    Mode mode = Mode::soft;
    float depth = 5.0f;
    float detail = 5.0f;
    float sharpnessOffset = 0.0f;   // advanced: -5..+5 on top of detail
    float selectivityOffset = 0.0f; // advanced: -5..+5 on top of detail
    float attack = 5.0f;
    float release = 3.0f;
    float detailTiltLow = 1.0f, detailTiltHigh = 1.0f;
    float attackTiltLow = 1.0f, attackTiltHigh = 1.0f;
    float releaseTiltLow = 1.0f, releaseTiltHigh = 1.0f;
    float maxCutDb = 40.0f;         // 40 = no extra limit
    float wetTrimDb = 0.0f;
    float mix = 1.0f;               // 0..1
    float outTrimDb = 0.0f;
    bool delta = false;
    Quality quality = Quality::normal;
    bool linearPhase = false;
    bool lowLatency = false;        // no look-ahead, direct FIR: zero added latency
    StereoMode stereoMode = StereoMode::leftRight;
    float stereoLink = 1.0f;        // 0 = dual mono, 1 = fully linked
    float stereoFocus = 0.5f;       // 0 = first channel (L/M) only, 0.5 = even, 1 = second (R/S) only
    bool sidechainListen = false;   // output the detection signal
    BandSet bands;
};

// Internal constants that turn the unitless knobs into DSP quantities. Defaults are fitted against
// soothe3 renders with tools/fit.py; rsdsp can override them for experiments.
struct Tuning
{
    // Detail -> widths (octaves): width = base * 2^(-detail / slope)
    float fineOctBase = 0.629079f, fineOctSlope = 0.506848f;   // resolution of the detection spectrum (octaves)...
    float fineBinsBase = 5.92463f;                         // ...but never narrower than this many bins each side
    float refOctBase = 4.50918f, refOctSlope = 12.2716f;      // reference envelope smoothing
    float cutOctBase = 1.00479f, cutOctSlope = 2.30059f;      // smoothing of the cut curve (notch width)
    float cutPower = 7.99692f;                             // 1 = average .. large = max (keeps notch depth)
    // Threshold above the envelope (dB): thrBase + thrDetail * detail + thrDepth * (10 - depth)
    float thrBase = -4.84666f, thrDetail = 0.184219f, thrDepth = 4.28391f;
    // Cut: limit(depth) * (1 - exp(-ratio * over / limit)), limit = limitScale * (depth / 10)^limitExp
    float ratio = 0.645589f, limitScale = 13.3174f, limitExp = 2.58272f;
    float knee = 3.0f;
    // Band depth (-10..10) -> sensitivity offset in dB.
    float bandScale = 1.5f;
    // Hard mode: extra "over" per dB of level above hardRef dBFS.
    float hardGain = 0.533421f, hardRef = -68.7304f;
    // Ignore content this far below the frame's loudest bin.
    float floorRange = 38.5173f;
    // Reference falls back to the louder neighbourhood when the two sides differ by more than this (dB).
    float cliffTolerance = 11.2833f;
    // Ballistics at 1 kHz: base * 2^(slope * knob) seconds; scaled by (1 kHz / f)^freqExp.
    float attackBase = 0.00503863f, attackSlope = 0.776779f;
    float releaseBase = 0.0006924f, releaseSlope = 1.9999f;
    float freqExp = 0.544875f;

    // Sets a field by name; returns false for unknown names.
    bool set (const juce::String& name, float value);

    // Real-unit readouts for the UI (at 1 kHz, before tilts).
    float attackMs (float knob) const  { return 1000.0f * attackBase * std::exp2 (attackSlope * knob); }
    float releaseMs (float knob) const { return 1000.0f * releaseBase * std::exp2 (releaseSlope * knob); }
    float depthLimitDb (float knob) const { return limitScale * std::pow (knob / 10.0f, limitExp); }
    float notchOctaves (float detail) const { return cutOctBase * std::exp2 (-detail / cutOctSlope); }
};

// Snapshot of the latest analysis frame for the GUI (log-spaced display bins).
struct DisplayFrame
{
    static constexpr int numPoints = 256;
    std::array<float, numPoints> inputDb {};
    std::array<float, numPoints> gainDb {};
};

// Dynamic resonance suppressor.
//
// Every hop: an FFT frame of the (look-ahead) input is analysed; each bin's level is compared with a
// smoothed spectral envelope; bins sticking out get a target cut, which is smoothed with
// frequency-dependent attack/release. The resulting gain curve becomes a minimum-phase (or
// linear-phase) FIR that filters the delayed audio via overlap-add fast convolution.
class Suppressor
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();
    void setParams (const Params& p) { params = p; }
    void setTuning (const Tuning& t) { tuning = t; }

    // Processes in place. `detect` (optional) replaces the main signal for analysis.
    void process (juce::AudioBuffer<float>& audio, const juce::AudioBuffer<float>* detect = nullptr);

    int getLatencySamples() const { return latency; }

    // GUI side: copies the most recent frame if a new one is available.
    bool pullDisplayFrame (DisplayFrame& dest);

private:
    void runHop();
    void analyse (int ch, std::vector<float>& levelDb);
    void computeTargets (const std::vector<float>& levelDb, const std::vector<float>& sensitivity, float weight, std::vector<float>& cutDb);
    bool needsPerChannelTargets() const;
    void smoothGains (int ch, const std::vector<float>& targetDb);
    void updateTables(); // per-bin widths, thresholds and ballistics; recomputed only when inputs change
    void buildFilter (const std::vector<float>& gainDb, std::vector<std::complex<float>>& filterSpectrum);
    void publishDisplay (const std::vector<float>& levelDb, const std::vector<float>& gainDb);
    void configure (Quality q, bool linear, bool lowLat); // no allocation; safe on the audio thread
    void buildImpulse (const std::vector<float>& gainDb, float* ir, int length, bool linear);
    float lowLatencySample (int ch, float x);

    Quality activeQuality = Quality::normal;
    StereoMode activeStereoMode = StereoMode::leftRight;
    bool activeLinear = false;
    bool activeLowLatency = false;

    // Low-latency path: per-channel time-domain FIR (reversed taps), crossfaded from the previous
    // filter to the current one over each hop. `line` holds the input twice so every window is contiguous.
    int llTaps = 512, llPos = 0;
    std::vector<std::vector<float>> llIrOld, llIrNew, llLine;

    Params params;
    Tuning tuning;
    double sr = 48000.0;
    int channels = 2;

    int fftOrder = 11, fftSize = 2048, numBins = 1025;
    int hop = 256, firLength = 1024, convSize = 2048, convOrder = 11;
    int lookahead = 0, latency = 0;

    std::unique_ptr<juce::dsp::FFT> analysisFft, convFft;
    std::vector<float> window;
    float windowSum = 1.0f;
    std::vector<float> binHz;

    // Ring buffers (per channel): input history for analysis + delayed block, detection history.
    int histSize = 0, histPos = 0;
    std::vector<std::vector<float>> history, detectHistory;

    // Overlap-add output accumulator + current output block.
    std::vector<std::vector<float>> ola;
    std::vector<std::vector<float>> outBlock;
    std::vector<std::vector<float>> dryBlock;
    int hopPos = 0;

    // Per-channel smoothed gain (dB, per bin).
    std::vector<std::vector<float>> gainDb;
    std::vector<float> attackCoef, releaseCoef;

    struct BinRange { int lo = 1, hi = 1; };
    std::vector<BinRange> fineRange, refLeft, refRight, cutRange;
    std::vector<float> threshold;
    std::vector<std::vector<float>> sensitivity; // per channel, from the band nodes
    BandSet tableBands;
    bool tablesValid = false;
    std::tuple<float, float, float, float, float, float, float, float, float, float, float, float, int> tableKey;
    Tuning tableTuning;

    // Scratch.
    std::vector<std::vector<float>> levelScratch, targetScratch;
    std::vector<float> fftWork, fineScratch, cutScratch;
    std::vector<double> prefix;
    std::vector<std::complex<float>> cepA, cepB, convIn, convOut;
    std::vector<std::vector<std::complex<float>>> filterSpectra;

    juce::SmoothedValue<float> wetGain, mixAmount, outGain;

    // Display hand-off: audio thread only try-locks, so it never blocks.
    juce::SpinLock displayLock;
    DisplayFrame display;
    bool displayFresh = false;
    std::array<int, DisplayFrame::numPoints + 1> displayEdges {};
    std::array<float, DisplayFrame::numPoints> displayCentres {}; // fractional bin of each point's centre
};
} // namespace rs
