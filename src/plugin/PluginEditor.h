#pragma once

#include "PluginProcessor.h"
#include "ui/Analyzer.h"
#include "ui/BandOverlay.h"
#include "ui/Knob.h"
#include "ui/Theme.h"

class ResonanceSuppressorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ResonanceSuppressorEditor (ResonanceSuppressorProcessor&);
    ~ResonanceSuppressorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void addToggle (juce::ToggleButton&, std::unique_ptr<ButtonAttachment>&, const char* paramId, const juce::String& text, bool accent = false);
    void addButton (juce::TextButton&, const juce::String& text, const juce::String& tooltip, std::function<void()> onClick);
    void timerCallback() override;
    void refreshPresetList();
    void savePresetDialog();
    void refreshAB();

    ResonanceSuppressorProcessor& processor;
    rs::ui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    rs::ui::Analyzer analyzer;
    rs::ui::BandOverlay bandOverlay;

    // Header: presets, A/B, undo
    juce::ComboBox presetBox;
    juce::TextButton saveButton, deleteButton, aButton, bButton, copyButton, undoButton, redoButton;
    std::unique_ptr<juce::AlertWindow> saveDialog;

    // Toolbar
    juce::ToggleButton softButton, hardButton, linearButton, lowLatencyButton, deltaButton, bypassButton, panelButton;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment;
    std::unique_ptr<ButtonAttachment> linearAttachment, lowLatencyAttachment, deltaAttachment, bypassAttachment;
    juce::ComboBox qualityBox;
    std::unique_ptr<ComboAttachment> qualityAttachment;

    // Main knobs
    rs::ui::Knob depth, detail, attack, release, mix, maxCut, wetTrim, outTrim;

    // Side panel
    rs::ui::Knob detailTiltLow, detailTiltHigh, attackTiltLow, attackTiltHigh, releaseTiltLow, releaseTiltHigh;
    rs::ui::Knob stereoLink, stereoFocus, sharpness, selectivity;
    juce::ComboBox stereoModeBox;
    std::unique_ptr<ComboAttachment> stereoModeAttachment;
    juce::ToggleButton sidechainButton, listenButton;
    std::unique_ptr<ButtonAttachment> sidechainAttachment, listenAttachment;
    bool panelOpen = false;
    static constexpr int numPanelRows = 6;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResonanceSuppressorEditor)
};
