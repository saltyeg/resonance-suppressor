#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rs::ui
{
// Light theme palette.
namespace colours
{
inline const juce::Colour background { 0xfff4f2ee };
inline const juce::Colour panel      { 0xffffffff };
inline const juce::Colour panelEdge  { 0xffe2ded6 };
inline const juce::Colour ink        { 0xff23262b };
inline const juce::Colour muted      { 0xff8a8f98 };
inline const juce::Colour grid       { 0xffe9e5de };
inline const juce::Colour spectrum   { 0xffc9cdd3 };
inline const juce::Colour output     { 0xff5b6470 };
inline const juce::Colour accent     { 0xffe4572e }; // reduction
inline const juce::Colour accentSoft { 0x33e4572e };
inline const juce::Colour track      { 0xffe6e2db };
} // namespace colours

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (13.0f); }
    juce::Font getPopupMenuFont() override { return juce::FontOptions (13.0f); }
};
} // namespace rs::ui
