#include "Bands.h"

#include <algorithm>
#include <cmath>

namespace rs
{
namespace
{
// Octave bandwidth of a constant-Q band.
float bandwidthOctaves (float q)
{
    return 2.0f * std::asinh (1.0f / (2.0f * q)) / std::log (2.0f);
}

float edgeDbPerOctave (float slope)
{
    return 6.0f + 5.4f * slope; // 0..10 -> 6..60 dB/oct
}

// 0 inside [-halfWidth, halfWidth] octaves, falling at `dbPerOct` outside (soft corner).
float plateau (float x, float halfWidth, float dbPerOct)
{
    const float outside = std::max (0.0f, std::abs (x) - halfWidth);
    const float soft = std::sqrt (outside * outside + 0.01f) - 0.1f;
    return -dbPerOct * soft;
}

float sigmoid (float x) { return 1.0f / (1.0f + std::exp2 (-x)); }
} // namespace

float sensitivityDb (const BandSet& bands, float hz, int channel, int numChannels, float depthScale, float floorDb)
{
    float sum = 0.0f;
    float gate = floorDb; // best (highest) bandpass response; only applied if a bandpass exists
    bool anyBandpass = false;

    for (const auto& b : bands)
    {
        if (! b.active())
            continue;

        const float weight = focusWeight (b.focus, channel, numChannels);
        if (weight <= 0.0f)
            continue;

        const float x = std::log2 (std::max (hz, 1.0f) / b.freq);
        const float gainDb = b.depth * depthScale;
        const float steep = 1.5f + 3.0f * b.q; // shelf / tilt transition steepness
        float s = 0.0f;

        switch (b.shape)
        {
            case Shape::lowCut:   s = x < 0.0f ? -edgeDbPerOctave (b.slope) * (std::sqrt (x * x + 0.01f) - 0.1f) : 0.0f; break;
            case Shape::highCut:  s = x > 0.0f ? -edgeDbPerOctave (b.slope) * (std::sqrt (x * x + 0.01f) - 0.1f) : 0.0f; break;
            case Shape::lowShelf: s = gainDb * (1.0f - sigmoid (x * steep)); break;
            case Shape::highShelf: s = gainDb * sigmoid (x * steep); break;
            case Shape::tilt:     s = gainDb * std::tanh (x / std::max (0.2f, 2.0f / b.q)); break;
            case Shape::bell:
            {
                const float sigma = bandwidthOctaves (b.q) / 2.355f;
                s = gainDb * std::exp (-(x * x) / (2.0f * sigma * sigma));
                break;
            }
            case Shape::bandReject:
            {
                // Mirror of bandpass: nothing processed inside the band, sloped edges.
                const float inside = 0.5f * bandwidthOctaves (b.q) - std::abs (x);
                s = -std::min (-floorDb, edgeDbPerOctave (b.slope) * std::max (0.0f, inside));
                break;
            }
            case Shape::bandpass:
                // Bandpass nodes combine in parallel: a frequency is processed if any bandpass passes it.
                anyBandpass = true;
                gate = std::max (gate, weight * plateau (x, 0.5f * bandwidthOctaves (b.q), edgeDbPerOctave (b.slope)));
                continue;
        }
        sum += weight * s;
    }

    if (anyBandpass)
        sum += gate;
    return std::max (floorDb, sum);
}

void sensitivityCurve (const BandSet& bands, const std::vector<float>& hz, int channel, int numChannels, float depthScale, std::vector<float>& out)
{
    out.resize (hz.size());
    for (size_t i = 0; i < hz.size(); ++i)
        out[i] = sensitivityDb (bands, hz[i], channel, numChannels, depthScale);
}
} // namespace rs
