#include "Suppressor.h"

#include <cmath>
#include <cstring>

namespace rs
{
namespace
{
constexpr float ln10over20 = 0.11512925464970229f; // ln(10) / 20

// Smooth weights for the "low" (< ~500 Hz) and "high" (> ~2 kHz) tilt regions.
float lowWeight (float hz)  { const float x = hz / 500.0f;  return 1.0f / (1.0f + x * x); }
float highWeight (float hz) { const float x = 2000.0f / juce::jmax (hz, 1.0f); return 1.0f / (1.0f + x * x); }

// Multiplier tilt (e.g. 0.25x..4x) blended in per frequency.
float tiltFactor (float hz, float low, float high)
{
    return std::pow (low, lowWeight (hz)) * std::pow (high, highWeight (hz));
}

int nextPow2 (int v)
{
    int p = 1;
    while (p < v)
        p <<= 1;
    return p;
}
} // namespace

bool Tuning::set (const juce::String& name, float value)
{
    const std::pair<const char*, float*> fields[] = {
        { "fineOctBase", &fineOctBase }, { "fineOctSlope", &fineOctSlope }, { "fineBinsBase", &fineBinsBase },
        { "refOctBase", &refOctBase },   { "refOctSlope", &refOctSlope },
        { "cutOctBase", &cutOctBase },   { "cutOctSlope", &cutOctSlope },
        { "thrBase", &thrBase },         { "thrDetail", &thrDetail },       { "thrDepth", &thrDepth },
        { "ratio", &ratio },             { "limitScale", &limitScale },     { "limitExp", &limitExp },
        { "knee", &knee },               { "bandScale", &bandScale },               { "hardGain", &hardGain },         { "hardRef", &hardRef },
        { "floorRange", &floorRange },
        { "attackBase", &attackBase },   { "attackSlope", &attackSlope },
        { "releaseBase", &releaseBase }, { "releaseSlope", &releaseSlope }, { "freqExp", &freqExp },
    };
    for (auto& [n, f] : fields)
        if (name == n)
        {
            *f = value;
            return true;
        }
    return false;
}

void Suppressor::prepare (double sampleRate, int numChannels)
{
    sr = sampleRate;
    channels = juce::jmax (1, numChannels);

    // ~43 ms analysis window regardless of sample rate.
    const int rateScale = juce::jmax (0, (int) std::round (std::log2 (sr / 48000.0)));
    fftOrder = 11 + rateScale;
    fftSize = 1 << fftOrder;
    numBins = fftSize / 2 + 1;
    firLength = fftSize / 2;
    convSize = fftSize; // hop (<= N/8) + FIR (N/2) - 1 always fits
    convOrder = fftOrder;

    analysisFft = std::make_unique<juce::dsp::FFT> (fftOrder);
    convFft = std::make_unique<juce::dsp::FFT> (convOrder);

    window.resize ((size_t) fftSize);
    windowSum = 0.0f;
    for (int n = 0; n < fftSize; ++n)
    {
        window[(size_t) n] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) fftSize);
        windowSum += window[(size_t) n];
    }

    binHz.resize ((size_t) numBins);
    for (int k = 0; k < numBins; ++k)
        binHz[(size_t) k] = (float) (k * sr / fftSize);

    // Worst case latency: base + linear-phase half FIR.
    histSize = nextPow2 (fftSize + fftSize / 2 + fftSize / 16 + firLength + 2 * fftSize / 8 + 16);

    auto sized = [&] (std::vector<std::vector<float>>& v, int n) {
        v.assign ((size_t) channels, std::vector<float> ((size_t) n, 0.0f));
    };
    sized (history, histSize);
    sized (detectHistory, histSize);
    sized (ola, convSize);
    sized (outBlock, fftSize / 8);
    sized (dryBlock, fftSize / 8);
    sized (gainDb, numBins);
    sized (levelScratch, numBins);
    levelScratch.resize ((size_t) channels + 1, std::vector<float> ((size_t) numBins, 0.0f));
    sized (targetScratch, numBins);
    targetScratch.resize ((size_t) channels + 1, std::vector<float> ((size_t) numBins, 0.0f));

    fineRange.assign ((size_t) numBins, {});
    refLeft.assign ((size_t) numBins, {});
    refRight.assign ((size_t) numBins, {});
    cutRange.assign ((size_t) numBins, {});
    threshold.assign ((size_t) numBins, 0.0f);
    sensitivity.assign ((size_t) channels, std::vector<float> ((size_t) numBins, 0.0f));
    tablesValid = false;
    attackCoef.assign ((size_t) numBins, 0.0f);
    releaseCoef.assign ((size_t) numBins, 0.0f);
    fftWork.assign ((size_t) fftSize * 2, 0.0f);
    prefix.assign ((size_t) numBins + 1, 0.0);
    fineScratch.assign ((size_t) numBins, 0.0f);
    cutScratch.assign ((size_t) numBins, 0.0f);
    cepA.assign ((size_t) fftSize, {});
    cepB.assign ((size_t) fftSize, {});
    convIn.assign ((size_t) convSize, {});
    convOut.assign ((size_t) convSize, {});
    filterSpectra.assign ((size_t) channels, std::vector<std::complex<float>> ((size_t) convSize));

    // Log-spaced display points, 20 Hz .. 20 kHz.
    for (int i = 0; i <= DisplayFrame::numPoints; ++i)
    {
        const double hz = 20.0 * std::pow (1000.0, (double) i / DisplayFrame::numPoints);
        displayEdges[(size_t) i] = juce::jlimit (1, numBins - 1, (int) std::round (hz * fftSize / sr));
        if (i < DisplayFrame::numPoints)
        {
            const double centre = 20.0 * std::pow (1000.0, ((double) i + 0.5) / DisplayFrame::numPoints);
            displayCentres[(size_t) i] = (float) (centre * fftSize / sr);
        }
    }

    llTaps = fftSize / 4;
    sized (llIrOld, llTaps);
    sized (llIrNew, llTaps);
    sized (llLine, 2 * llTaps);

    wetGain.reset (sr, 0.02);
    mixAmount.reset (sr, 0.02);
    outGain.reset (sr, 0.02);

    configure (params.quality, params.linearPhase, params.lowLatency);
}

void Suppressor::configure (Quality q, bool linear, bool lowLat)
{
    activeQuality = q;
    activeLinear = linear;
    activeLowLatency = lowLat;

    const int divisor = q == Quality::normal ? 8 : q == Quality::high ? 16 : 32;
    hop = fftSize / divisor;

    // Constant base latency across qualities; window centre lines up with the filtered block at "normal".
    const int baseLatency = fftSize / 2 + fftSize / 16;
    lookahead = baseLatency - hop;
    latency = lowLat ? 0 : baseLatency + (linear ? firLength / 2 : 0);

    reset();
}

void Suppressor::reset()
{
    for (auto* set : { &history, &detectHistory, &ola, &outBlock, &dryBlock, &gainDb, &llIrOld, &llIrNew, &llLine })
        for (auto& v : *set)
            std::fill (v.begin(), v.end(), 0.0f);
    for (auto* set : { &llIrOld, &llIrNew })
        for (auto& v : *set)
            if (! v.empty())
                v.back() = 1.0f; // identity filter (reversed taps: newest sample last)
    histPos = 0;
    hopPos = 0;
    llPos = 0;

    wetGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (params.wetTrimDb));
    mixAmount.setCurrentAndTargetValue (params.mix);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (params.outTrimDb));
}

void Suppressor::process (juce::AudioBuffer<float>& audio, const juce::AudioBuffer<float>* detect)
{
    if (params.quality != activeQuality || params.linearPhase != activeLinear || params.lowLatency != activeLowLatency
        || params.stereoMode != activeStereoMode)
    {
        activeStereoMode = params.stereoMode;
        configure (params.quality, params.linearPhase, params.lowLatency);
    }

    wetGain.setTargetValue (juce::Decibels::decibelsToGain (params.wetTrimDb));
    mixAmount.setTargetValue (params.mix);
    outGain.setTargetValue (juce::Decibels::decibelsToGain (params.outTrimDb));

    const int numSamples = audio.getNumSamples();
    const int numCh = juce::jmin (channels, audio.getNumChannels());
    const int mask = histSize - 1;

    const bool midSide = numCh == 2 && params.stereoMode == StereoMode::midSide;
    constexpr float invSqrt2 = 0.70710678f;

    for (int i = 0; i < numSamples; ++i)
    {
        float in[2] = { audio.getSample (0, i), numCh > 1 ? audio.getSample (1, i) : 0.0f };
        float det[2] = { in[0], in[1] };
        if (detect != nullptr)
            for (int ch = 0; ch < numCh; ++ch)
                det[ch] = detect->getSample (juce::jmin (ch, detect->getNumChannels() - 1), i);

        if (midSide)
        {
            const float m = (in[0] + in[1]) * invSqrt2, sd = (in[0] - in[1]) * invSqrt2;
            in[0] = m;
            in[1] = sd;
            const float dm = (det[0] + det[1]) * invSqrt2, ds = (det[0] - det[1]) * invSqrt2;
            det[0] = dm;
            det[1] = ds;
        }
        for (int ch = 0; ch < numCh; ++ch)
        {
            history[(size_t) ch][(size_t) histPos] = in[ch];
            detectHistory[(size_t) ch][(size_t) histPos] = det[ch];
        }
        histPos = (histPos + 1) & mask;

        const float wg = wetGain.getNextValue();
        const float mx = mixAmount.getNextValue();
        const float og = outGain.getNextValue();

        float out[2] = {};
        for (int ch = 0; ch < numCh; ++ch)
        {
            float dry, wet;
            if (activeLowLatency)
            {
                wet = lowLatencySample (ch, in[ch]) * wg;
                dry = params.sidechainListen ? det[ch] : in[ch];
            }
            else
            {
                dry = dryBlock[(size_t) ch][(size_t) hopPos];
                wet = outBlock[(size_t) ch][(size_t) hopPos] * wg;
            }
            const float mixed = dry + mx * (wet - dry);
            out[ch] = params.sidechainListen ? dry : params.delta ? dry - mixed : mixed;
        }
        if (midSide)
        {
            const float l = (out[0] + out[1]) * invSqrt2, r = (out[0] - out[1]) * invSqrt2;
            out[0] = l;
            out[1] = r;
        }
        for (int ch = 0; ch < numCh; ++ch)
            audio.setSample (ch, i, og * out[ch]);
        if (activeLowLatency)
            llPos = (llPos + 1) % llTaps;

        if (++hopPos == hop)
        {
            hopPos = 0;
            runHop();
        }
    }
}

void Suppressor::analyse (int ch, std::vector<float>& levelDb)
{
    const int mask = histSize - 1;
    const auto& src = detectHistory[(size_t) ch];
    const int start = histPos - fftSize;

    for (int n = 0; n < fftSize; ++n)
        fftWork[(size_t) n] = src[(size_t) ((start + n) & mask)] * window[(size_t) n];
    std::fill (fftWork.begin() + fftSize, fftWork.end(), 0.0f);

    analysisFft->performRealOnlyForwardTransform (fftWork.data(), true);

    // Power normalised so a full-scale sine reads ~0 dB.
    const float norm = 4.0f / (windowSum * windowSum);
    for (int k = 0; k < numBins; ++k)
    {
        const float re = fftWork[(size_t) (2 * k)], im = fftWork[(size_t) (2 * k + 1)];
        levelDb[(size_t) k] = 10.0f * std::log10 ((re * re + im * im) * norm + 1e-20f);
    }
}

void Suppressor::updateTables()
{
    const auto& p = params;
    const auto& t = tuning;

    const auto key = std::make_tuple (p.depth, p.detail, p.sharpnessOffset, p.selectivityOffset, p.attack, p.release,
                                      p.detailTiltLow, p.detailTiltHigh, p.attackTiltLow, p.attackTiltHigh,
                                      p.releaseTiltLow, p.releaseTiltHigh, hop);
    if (tablesValid && key == tableKey && p.bands == tableBands && std::memcmp (&t, &tableTuning, sizeof (Tuning)) == 0)
        return;
    tableKey = key;
    tableTuning = t;
    tableBands = p.bands;
    tablesValid = true;

    for (int ch = 0; ch < channels; ++ch)
        sensitivityCurve (p.bands, binHz, ch, channels, t.bandScale, sensitivity[(size_t) ch]);

    auto bounds = [this] (int k, float octaves, BinRange& r) {
        const float ratio = std::exp2 (octaves * 0.5f);
        r.lo = juce::jlimit (1, numBins - 1, (int) std::floor ((float) k / ratio));
        r.hi = juce::jlimit (r.lo, numBins - 1, (int) std::ceil ((float) k * ratio));
    };

    const float hopSeconds = (float) hop / (float) sr;
    const float attack1k = t.attackBase * std::exp2 (t.attackSlope * p.attack);
    const float release1k = t.releaseBase * std::exp2 (t.releaseSlope * p.release);

    for (int k = 1; k < numBins; ++k)
    {
        const float hz = binHz[(size_t) k];
        const float d = juce::jlimit (0.0f, 10.0f, p.detail + 3.0f * std::log2 (tiltFactor (hz, p.detailTiltLow, p.detailTiltHigh)));
        const float sharp = juce::jlimit (0.0f, 12.0f, d + p.sharpnessOffset);
        const float select = juce::jlimit (0.0f, 12.0f, d + p.selectivityOffset);

        const float narrow = std::exp2 (-sharp / t.fineOctSlope);
        auto& fr = fineRange[(size_t) k];
        bounds (k, t.fineOctBase * narrow, fr);
        const int halfBins = juce::roundToInt (t.fineBinsBase * narrow);
        fr.lo = juce::jlimit (1, numBins - 1, juce::jmin (fr.lo, k - halfBins));
        fr.hi = juce::jlimit (fr.lo, numBins - 1, juce::jmax (fr.hi, k + halfBins));

        // Reference neighbourhoods either side of the detection window. Using the louder side keeps
        // the edge of a steep spectral cliff (e.g. a lowpass) from looking like a resonance.
        BinRange wide;
        bounds (k, t.refOctBase * std::exp2 (-d / t.refOctSlope), wide);
        refLeft[(size_t) k] = { wide.lo, juce::jmax (wide.lo, fr.lo - 1) };
        refRight[(size_t) k] = { juce::jmin (wide.hi, fr.hi + 1), wide.hi };
        bounds (k, t.cutOctBase * std::exp2 (-d / t.cutOctSlope), cutRange[(size_t) k]);
        threshold[(size_t) k] = t.thrBase + t.thrDetail * select + t.thrDepth * (10.0f - p.depth);

        // Frequency-dependent ballistics: faster at high frequencies, shaped further by the tilt controls.
        const float bHz = juce::jmax (40.0f, hz);
        const float scale = std::pow (1000.0f / bHz, t.freqExp);
        const float ta = attack1k * scale * tiltFactor (bHz, p.attackTiltLow, p.attackTiltHigh);
        const float tr = release1k * scale * tiltFactor (bHz, p.releaseTiltLow, p.releaseTiltHigh);
        attackCoef[(size_t) k] = std::exp (-hopSeconds / ta);
        releaseCoef[(size_t) k] = std::exp (-hopSeconds / tr);
    }
    attackCoef[0] = attackCoef[1];
    releaseCoef[0] = releaseCoef[1];
}

bool Suppressor::needsPerChannelTargets() const
{
    if (channels < 2)
        return false;
    if (params.stereoLink < 1.0f || params.stereoFocus != 0.5f)
        return true;
    for (const auto& b : params.bands)
        if (b.active() && b.focus != 0.5f)
            return true;
    return false;
}

void Suppressor::computeTargets (const std::vector<float>& levelDb, const std::vector<float>& sens, float weight, std::vector<float>& cutDb)
{
    const auto& p = params;
    const auto& t = tuning;
    auto& pre = prefix;
    auto& fine = fineScratch;
    auto& cut = cutScratch;

    if (p.depth <= 0.0f || weight <= 0.0f)
    {
        std::fill (cutDb.begin(), cutDb.end(), 0.0f);
        return;
    }

    auto boxMean = [&pre] (const BinRange& r) { return (pre[(size_t) r.hi + 1] - pre[(size_t) r.lo]) / (double) (r.hi - r.lo + 1); };

    // 1. Detection spectrum: power average over a narrow fractional-octave window.
    pre[0] = 0.0;
    for (int k = 0; k < numBins; ++k)
        pre[(size_t) k + 1] = pre[(size_t) k] + std::pow (10.0, levelDb[(size_t) k] * 0.1);

    float loudest = -200.0f;
    for (int k = 1; k < numBins; ++k)
    {
        fine[(size_t) k] = 10.0f * (float) std::log10 (boxMean (fineRange[(size_t) k]) + 1e-20);
        loudest = juce::jmax (loudest, fine[(size_t) k]);
    }
    fine[0] = fine[1];
    const float floorDb = loudest - t.floorRange;

    // 2. Reference envelope (dB-domain average of the detection spectrum) and the raw cut per bin.
    pre[0] = 0.0;
    for (int k = 0; k < numBins; ++k)
        pre[(size_t) k + 1] = pre[(size_t) k] + fine[(size_t) k];

    const float limit = juce::jmax (1e-3f, t.limitScale * std::pow (p.depth / 10.0f, t.limitExp));
    const float maxHz = juce::jmin (20000.0f, (float) sr * 0.45f);
    const float h = t.knee * 0.5f;

    cut[0] = 0.0f;
    for (int k = 1; k < numBins; ++k)
    {
        const float level = fine[(size_t) k];
        const float ref = (float) juce::jmax (boxMean (refLeft[(size_t) k]), boxMean (refRight[(size_t) k]));
        float over = level - ref - threshold[(size_t) k] + sens[(size_t) k];
        if (p.mode == Mode::hard)
            over += t.hardGain * (level - t.hardRef);

        // Soft knee, then a depth-dependent saturation so huge peaks (pure tones) don't get gouged.
        float amount = over > h ? over : over > -h ? (over + h) * (over + h) / (2.0f * t.knee) : 0.0f;
        amount = limit * (1.0f - std::exp (-t.ratio * amount / limit));

        // Fade out near the noise floor and outside the audible range.
        const float floorFade = juce::jlimit (0.0f, 1.0f, (level - floorDb) * 0.1f);
        const float hz = binHz[(size_t) k];
        cut[(size_t) k] = (hz < 20.0f || hz > maxHz) ? 0.0f : amount * floorFade * weight;
    }

    // 3. Smooth the cut across frequency so notches have a musical width.
    pre[0] = 0.0;
    for (int k = 0; k < numBins; ++k)
        pre[(size_t) k + 1] = pre[(size_t) k] + cut[(size_t) k];

    cutDb[0] = 0.0f;
    for (int k = 1; k < numBins; ++k)
        cutDb[(size_t) k] = -juce::jmin ((float) boxMean (cutRange[(size_t) k]), p.maxCutDb);
}

void Suppressor::smoothGains (int ch, const std::vector<float>& targetDb)
{
    auto& g = gainDb[(size_t) ch];
    for (int k = 0; k < numBins; ++k)
    {
        const float t = targetDb[(size_t) k];
        const float c = t < g[(size_t) k] ? attackCoef[(size_t) k] : releaseCoef[(size_t) k];
        g[(size_t) k] = t + c * (g[(size_t) k] - t);
    }
}

void Suppressor::buildImpulse (const std::vector<float>& gDb, float* ir, int length, bool linear)
{
    const int n = fftSize;

    // Light 3-bin smoothing so single-bin needles don't become long ringing notches.
    auto gainAt = [&] (int k) {
        const int a = juce::jmax (0, k - 1), b = juce::jmin (numBins - 1, k + 1);
        return (gDb[(size_t) a] + 2.0f * gDb[(size_t) k] + gDb[(size_t) b]) * 0.25f;
    };

    if (! linear)
    {
        // Minimum phase via the real cepstrum: fold the cepstrum of log|G| onto positive quefrency.
        for (int k = 0; k < numBins; ++k)
            cepA[(size_t) k] = { gainAt (k) * ln10over20, 0.0f };
        for (int k = numBins; k < n; ++k)
            cepA[(size_t) k] = cepA[(size_t) (n - k)];

        analysisFft->perform (cepA.data(), cepB.data(), true);
        cepA[0] = { cepB[0].real(), 0.0f };
        for (int q = 1; q < n / 2; ++q)
            cepA[(size_t) q] = { 2.0f * cepB[(size_t) q].real(), 0.0f };
        cepA[(size_t) n / 2] = { cepB[(size_t) n / 2].real(), 0.0f };
        for (int q = n / 2 + 1; q < n; ++q)
            cepA[(size_t) q] = {};

        analysisFft->perform (cepA.data(), cepB.data(), false);
        for (int k = 0; k < n; ++k)
            cepB[(size_t) k] = std::exp (cepB[(size_t) k]);
        analysisFft->perform (cepB.data(), cepA.data(), true);

        // Truncate with a short fade-out.
        const int fade = length / 4;
        for (int i = 0; i < length; ++i)
        {
            float w = 1.0f;
            if (i >= length - fade)
                w = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) (i - (length - fade)) / (float) fade);
            ir[i] = cepA[(size_t) i].real() * w;
        }
    }
    else
    {
        // Linear phase: zero-phase response, centred at length / 2, Hann windowed.
        for (int k = 0; k < numBins; ++k)
            cepA[(size_t) k] = { std::exp (gainAt (k) * ln10over20), 0.0f };
        for (int k = numBins; k < n; ++k)
            cepA[(size_t) k] = cepA[(size_t) (n - k)];
        analysisFft->perform (cepA.data(), cepB.data(), true);

        const int half = length / 2;
        for (int i = 0; i < length; ++i)
        {
            const int src = (i - half + n) % n;
            const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) length);
            ir[i] = cepB[(size_t) src].real() * w;
        }
    }
}

void Suppressor::buildFilter (const std::vector<float>& gDb, std::vector<std::complex<float>>& spectrum)
{
    buildImpulse (gDb, fftWork.data(), firLength, activeLinear);
    for (int i = 0; i < convSize; ++i)
        convIn[(size_t) i] = { i < firLength ? fftWork[(size_t) i] : 0.0f, 0.0f };
    convFft->perform (convIn.data(), spectrum.data(), false);
}

float Suppressor::lowLatencySample (int ch, float x)
{
    auto& line = llLine[(size_t) ch];
    line[(size_t) llPos] = x;
    line[(size_t) (llPos + llTaps)] = x;

    // Window of the last llTaps samples, oldest first, ending with x.
    const float* window = line.data() + llPos + 1;
    const float* newer = llIrNew[(size_t) ch].data();
    const float* older = llIrOld[(size_t) ch].data();
    float a = 0.0f, b = 0.0f;
    for (int j = 0; j < llTaps; ++j)
    {
        a += newer[j] * window[j];
        b += older[j] * window[j];
    }
    const float fade = (float) (hopPos + 1) / (float) hop;
    return b + fade * (a - b);
}

void Suppressor::runHop()
{
    const auto& p = params;
    updateTables();

    // Analysis: per channel, plus a linked (power-averaged) spectrum.
    auto& linkedLevel = levelScratch[(size_t) channels];
    std::fill (linkedLevel.begin(), linkedLevel.end(), 0.0f);
    for (int ch = 0; ch < channels; ++ch)
    {
        analyse (ch, levelScratch[(size_t) ch]);
        for (int k = 0; k < numBins; ++k)
            linkedLevel[(size_t) k] += std::pow (10.0f, levelScratch[(size_t) ch][(size_t) k] * 0.1f) / (float) channels;
    }
    for (auto& v : linkedLevel)
        v = 10.0f * std::log10 (v + 1e-20f);

    const float link = channels > 1 ? p.stereoLink : 1.0f;
    const bool perChannel = needsPerChannelTargets();
    auto& linkedTarget = targetScratch[(size_t) channels];

    if (! perChannel)
        computeTargets (linkedLevel, sensitivity[0], 1.0f, linkedTarget);

    for (int ch = 0; ch < channels; ++ch)
    {
        auto& target = targetScratch[(size_t) ch];
        if (perChannel)
        {
            const float weight = focusWeight (p.stereoFocus, ch, channels);
            computeTargets (linkedLevel, sensitivity[(size_t) ch], weight, linkedTarget);
            if (link < 1.0f)
            {
                computeTargets (levelScratch[(size_t) ch], sensitivity[(size_t) ch], weight, target);
                for (int k = 0; k < numBins; ++k)
                    target[(size_t) k] += link * (linkedTarget[(size_t) k] - target[(size_t) k]);
            }
            else
            {
                target = linkedTarget;
            }
        }
        else
        {
            target = linkedTarget;
        }
        smoothGains (ch, target);
    }

    if (activeLowLatency)
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            std::swap (llIrOld[(size_t) ch], llIrNew[(size_t) ch]);
            if (ch == 0 || perChannel)
            {
                buildImpulse (gainDb[(size_t) ch], fftWork.data(), llTaps, false);
                auto& dst = llIrNew[(size_t) ch];
                for (int j = 0; j < llTaps; ++j)
                    dst[(size_t) j] = fftWork[(size_t) (llTaps - 1 - j)];
            }
            else
            {
                llIrNew[(size_t) ch] = llIrNew[0];
            }
        }
        publishDisplay (linkedLevel, gainDb[0]);
        return;
    }

    // Build filters (once unless channels differ) and convolve the delayed block.
    const int mask = histSize - 1;
    const int blockStart = histPos - lookahead - hop;
    for (int ch = 0; ch < channels; ++ch)
    {
        if (ch == 0 || perChannel)
            buildFilter (gainDb[(size_t) ch], filterSpectra[(size_t) ch]);
        const auto& filter = filterSpectra[(size_t) (perChannel ? ch : 0)];

        const auto& h = history[(size_t) ch];
        const auto& drySource = p.sidechainListen ? detectHistory[(size_t) ch] : h;
        for (int i = 0; i < convSize; ++i)
            convIn[(size_t) i] = { i < hop ? h[(size_t) ((blockStart + i) & mask)] : 0.0f, 0.0f };
        convFft->perform (convIn.data(), convOut.data(), false);
        for (int i = 0; i < convSize; ++i)
            convOut[(size_t) i] *= filter[(size_t) i];
        convFft->perform (convOut.data(), convIn.data(), true);

        auto& acc = ola[(size_t) ch];
        for (int i = 0; i < convSize; ++i)
            acc[(size_t) i] += convIn[(size_t) i].real();

        auto& out = outBlock[(size_t) ch];
        auto& dry = dryBlock[(size_t) ch];
        for (int i = 0; i < hop; ++i)
        {
            out[(size_t) i] = acc[(size_t) i];
            dry[(size_t) i] = drySource[(size_t) ((histPos + i - latency) & mask)];
        }
        std::move (acc.begin() + hop, acc.end(), acc.begin());
        std::fill (acc.end() - hop, acc.end(), 0.0f);
    }

    publishDisplay (linkedLevel, gainDb[0]);
}

void Suppressor::publishDisplay (const std::vector<float>& levelDb, const std::vector<float>& gDb)
{
    const juce::SpinLock::ScopedTryLockType lock (displayLock);
    if (! lock.isLocked())
        return;

    for (int i = 0; i < DisplayFrame::numPoints; ++i)
    {
        const int a = displayEdges[(size_t) i], b = displayEdges[(size_t) i + 1];
        float level = -200.0f, gain = 0.0f;
        if (b - a >= 2)
        {
            // Several bins per point: show the loudest level and the deepest cut.
            for (int k = a; k < b; ++k)
            {
                level = juce::jmax (level, levelDb[(size_t) k]);
                gain = juce::jmin (gain, gDb[(size_t) k]);
            }
        }
        else
        {
            // Low end: points fall between bins, interpolate at the point's centre frequency.
            const float pos = displayCentres[(size_t) i];
            const int k0 = juce::jlimit (1, numBins - 2, (int) pos);
            const float frac = juce::jlimit (0.0f, 1.0f, pos - (float) k0);
            level = levelDb[(size_t) k0] + frac * (levelDb[(size_t) k0 + 1] - levelDb[(size_t) k0]);
            gain = gDb[(size_t) k0] + frac * (gDb[(size_t) k0 + 1] - gDb[(size_t) k0]);
        }
        display.inputDb[(size_t) i] = level;
        display.gainDb[(size_t) i] = gain;
    }
    displayFresh = true;
}

bool Suppressor::pullDisplayFrame (DisplayFrame& dest)
{
    const juce::SpinLock::ScopedLockType lock (displayLock);
    if (! displayFresh)
        return false;
    dest = display;
    displayFresh = false;
    return true;
}
} // namespace rs
