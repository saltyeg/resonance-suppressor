#pragma once

#include <array>
#include <vector>

namespace rs
{
// Band nodes shape *where* the suppressor listens (detection sensitivity), not the cut itself.
// Shapes follow soothe3's list and order.
enum class Shape { lowCut, lowShelf, bell, bandpass, bandReject, tilt, highShelf, highCut };
inline constexpr int numShapes = 8;
inline constexpr int numBands = 8;
inline constexpr const char* shapeNames[numShapes] = { "Low cut", "Low shelf", "Bell", "Bandpass", "Band reject", "Tilt", "High shelf", "High cut" };

struct Band
{
    bool used = false;    // the node exists
    bool enabled = true;  // the node is active
    Shape shape = Shape::bell;
    float freq = 1000.0f; // Hz
    float depth = 0.0f;   // -10..10: sensitivity change for bells, shelves and tilt
    float q = 0.7f;       // 0.1..10
    float slope = 5.0f;   // 0..10: steepness of cuts, bandpass and band reject edges
    float focus = 0.5f;   // 0 = first channel (L or M) only, 1 = second (R or S) only

    bool active() const { return used && enabled; }
    bool operator== (const Band&) const = default;
};

using BandSet = std::array<Band, numBands>;

// Per-channel weight of a band (or of the global stereo focus) for a 0..1 focus value.
inline float focusWeight (float focus, int channel, int numChannels)
{
    if (numChannels < 2)
        return 1.0f;
    const float w = channel == 0 ? 2.0f * (1.0f - focus) : 2.0f * focus;
    return w < 1.0f ? w : 1.0f;
}

// Sensitivity offset in dB at `hz` for one channel: positive listens harder (more cut), negative
// less; cut/bandpass/reject regions go down to `floorDb`. `depthScale` converts band depth to dB.
float sensitivityDb (const BandSet& bands, float hz, int channel, int numChannels, float depthScale, float floorDb = -60.0f);

// Fills `out` for every frequency in `hz`.
void sensitivityCurve (const BandSet& bands, const std::vector<float>& hz, int channel, int numChannels, float depthScale, std::vector<float>& out);
} // namespace rs
