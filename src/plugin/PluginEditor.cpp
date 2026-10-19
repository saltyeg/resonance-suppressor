#include "PluginEditor.h"

namespace
{
using namespace rs;
const Tuning tuning; // fitted defaults, for real-unit readouts

juce::String ms (float v) { return v < 100.0f ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; }
} // namespace

ResonanceSuppressorEditor::ResonanceSuppressorEditor (ResonanceSuppressorProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      analyzer (p.suppressor),
      bandOverlay (p.apvts, analyzer),
      depth (p.apvts, ids::depth, "Depth", false, [] (float v) { return "up to ~" + juce::String (tuning.depthLimitDb (v), 1) + " dB"; }),
      detail (p.apvts, ids::detail, "Detail", false, [] (float v) { return "notch ~1/" + juce::String (1.0f / tuning.notchOctaves (v), 1) + " oct"; }),
      attack (p.apvts, ids::attack, "Attack", false, [] (float v) { return ms (tuning.attackMs (v)) + " @ 1k"; }),
      release (p.apvts, ids::release, "Release", false, [] (float v) { return ms (tuning.releaseMs (v)) + " @ 1k"; }),
      mix (p.apvts, ids::mix, "Mix"),
      maxCut (p.apvts, ids::maxCut, "Max cut"),
      wetTrim (p.apvts, ids::wetTrim, "Wet trim", true),
      outTrim (p.apvts, ids::outTrim, "Out trim", true),
      detailTiltLow (p.apvts, ids::detailTiltLow, "Detail low", true),
      detailTiltHigh (p.apvts, ids::detailTiltHigh, "Detail high", true),
      attackTiltLow (p.apvts, ids::attackTiltLow, "Attack low", true),
      attackTiltHigh (p.apvts, ids::attackTiltHigh, "Attack high", true),
      releaseTiltLow (p.apvts, ids::releaseTiltLow, "Release low", true),
      releaseTiltHigh (p.apvts, ids::releaseTiltHigh, "Release high", true),
      stereoLink (p.apvts, ids::stereoLink, "Link"),
      stereoFocus (p.apvts, ids::stereoFocus, "Focus", true),
      sharpness (p.apvts, ids::sharpness, "Sharpness", true),
      selectivity (p.apvts, ids::selectivity, "Selectivity", true)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (analyzer);
    addAndMakeVisible (bandOverlay);

    // Mode: two radio-style buttons driving the choice parameter.
    auto* modeParam = p.apvts.getParameter (ids::mode);
    auto syncMode = [this, modeParam] {
        const bool hard = modeParam->getValue() > 0.5f;
        softButton.setToggleState (! hard, juce::dontSendNotification);
        hardButton.setToggleState (hard, juce::dontSendNotification);
    };
    for (auto* b : { &softButton, &hardButton })
    {
        addAndMakeVisible (*b);
        b->setClickingTogglesState (false);
    }
    softButton.setButtonText ("soft");
    hardButton.setButtonText ("hard");
    softButton.setTooltip ("Adaptive threshold: reacts to tonal balance, largely level independent");
    hardButton.setTooltip ("Fixed threshold: louder material is cut harder");
    softButton.onClick = [modeParam, syncMode] { modeParam->setValueNotifyingHost (0.0f); syncMode(); };
    hardButton.onClick = [modeParam, syncMode] { modeParam->setValueNotifyingHost (1.0f); syncMode(); };
    syncMode();

    addToggle (linearButton, linearAttachment, ids::linearPhase, "linear phase");
    addToggle (lowLatencyButton, lowLatencyAttachment, ids::lowLatency, "zero latency");
    lowLatencyButton.setTooltip ("No look-ahead, zero added latency (for tracking); slightly less precise");
    addToggle (deltaButton, deltaAttachment, ids::delta, "delta", true);
    addToggle (bypassButton, bypassAttachment, ids::bypass, "bypass");
    linearButton.setTooltip ("Linear-phase filtering (adds latency)");
    deltaButton.setTooltip ("Listen to only what is being removed");

    qualityBox.addItemList ({ "normal", "high", "ultra" }, 1);
    qualityBox.setTooltip ("Time resolution of the detection (higher = more CPU)");
    addAndMakeVisible (qualityBox);
    qualityAttachment = std::make_unique<ComboAttachment> (p.apvts, ids::quality, qualityBox);

    panelButton.setButtonText ("more");
    panelButton.setTooltip ("Tilts, stereo and advanced detection controls");
    panelButton.onClick = [this] { panelOpen = panelButton.getToggleState(); resized(); };
    addAndMakeVisible (panelButton);

    for (auto* k : { &depth, &detail, &attack, &release, &mix, &maxCut, &wetTrim, &outTrim })
        addAndMakeVisible (*k);
    for (auto* k : { &detailTiltLow, &detailTiltHigh, &attackTiltLow, &attackTiltHigh, &releaseTiltLow, &releaseTiltHigh,
                     &stereoLink, &stereoFocus, &sharpness, &selectivity })
        addChildComponent (*k);

    stereoModeBox.addItemList ({ "L/R", "M/S" }, 1);
    stereoModeBox.setTooltip ("Process left/right or mid/side");
    addChildComponent (stereoModeBox);
    stereoModeAttachment = std::make_unique<ComboAttachment> (p.apvts, ids::stereoMode, stereoModeBox);

    for (auto* b : { &sidechainButton, &listenButton })
        addChildComponent (*b);
    sidechainButton.setButtonText ("external");
    sidechainButton.setTooltip ("Detect resonances from the sidechain input instead of the main signal");
    listenButton.setButtonText ("listen");
    listenButton.setTooltip ("Hear the sidechain signal");
    sidechainAttachment = std::make_unique<ButtonAttachment> (p.apvts, ids::sidechain, sidechainButton);
    listenAttachment = std::make_unique<ButtonAttachment> (p.apvts, ids::sidechainListen, listenButton);

    setResizable (true, true);
    setResizeLimits (720, 460, 2000, 1300);
    setSize (980, 600);
}

ResonanceSuppressorEditor::~ResonanceSuppressorEditor()
{
    setLookAndFeel (nullptr);
}

void ResonanceSuppressorEditor::addToggle (juce::ToggleButton& b, std::unique_ptr<ButtonAttachment>& attachment,
                                           const char* paramId, const juce::String& text, bool accent)
{
    b.setButtonText (text);
    b.getProperties().set ("accent", accent);
    addAndMakeVisible (b);
    attachment = std::make_unique<ButtonAttachment> (processor.apvts, paramId, b);
}

void ResonanceSuppressorEditor::paint (juce::Graphics& g)
{
    g.fillAll (rs::ui::colours::background);

    g.setColour (rs::ui::colours::ink);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText ("resonance-suppressor", 16, 0, 260, 44, juce::Justification::centredLeft);

    if (panelOpen)
    {
        const auto side = getLocalBounds().withTrimmedTop (44).removeFromRight (230).reduced (8);
        g.setColour (rs::ui::colours::panel);
        g.fillRoundedRectangle (side.toFloat(), 6.0f);
        g.setColour (rs::ui::colours::panelEdge);
        g.drawRoundedRectangle (side.toFloat(), 6.0f, 1.0f);

        g.setColour (rs::ui::colours::muted);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        const int rowH = (side.getHeight() - 8) / numPanelRows;
        const char* headings[] = { "DETAIL TILT", "ATTACK TILT", "RELEASE TILT", "STEREO", "SIDECHAIN", "ADVANCED" };
        for (int i = 0; i < numPanelRows; ++i)
            g.drawText (headings[i], side.getX() + 10, side.getY() + 4 + i * rowH, 200, 14, juce::Justification::centredLeft);
    }
}

void ResonanceSuppressorEditor::resized()
{
    auto r = getLocalBounds();

    // Header
    auto header = r.removeFromTop (44).reduced (10, 9);
    header.removeFromLeft (200);
    softButton.setBounds (header.removeFromLeft (52));
    hardButton.setBounds (header.removeFromLeft (52));
    panelButton.setBounds (header.removeFromRight (60));
    header.removeFromRight (8);
    bypassButton.setBounds (header.removeFromRight (70));
    header.removeFromRight (6);
    deltaButton.setBounds (header.removeFromRight (60));
    header.removeFromRight (6);
    linearButton.setBounds (header.removeFromRight (104));
    header.removeFromRight (6);
    lowLatencyButton.setBounds (header.removeFromRight (100));
    header.removeFromRight (6);
    qualityBox.setBounds (header.removeFromRight (90));

    // Side panel
    for (auto* c : std::initializer_list<juce::Component*> { &detailTiltLow, &detailTiltHigh, &attackTiltLow, &attackTiltHigh,
                                                             &releaseTiltLow, &releaseTiltHigh, &stereoLink, &stereoFocus,
                                                             &stereoModeBox, &sidechainButton, &listenButton, &sharpness, &selectivity })
        c->setVisible (panelOpen);

    if (panelOpen)
    {
        auto side = r.removeFromRight (230).reduced (8);
        side.removeFromTop (4);
        const int rowH = (side.getHeight() - 4) / numPanelRows;
        auto row = [&] { auto x = side.removeFromTop (rowH); x.removeFromTop (14); return x.reduced (6, 0); };
        auto pair = [] (juce::Rectangle<int> x, rs::ui::Knob& a, rs::ui::Knob* b) {
            a.setBounds (x.removeFromLeft (x.getWidth() / 2));
            if (b != nullptr)
                b->setBounds (x);
        };
        pair (row(), detailTiltLow, &detailTiltHigh);
        pair (row(), attackTiltLow, &attackTiltHigh);
        pair (row(), releaseTiltLow, &releaseTiltHigh);
        {
            auto stereo = row();
            stereoModeBox.setBounds (stereo.removeFromLeft (64).withSizeKeepingCentre (60, 24));
            pair (stereo, stereoLink, &stereoFocus);
        }
        {
            auto sc = row().withSizeKeepingCentre (200, 26);
            sidechainButton.setBounds (sc.removeFromLeft (96));
            sc.removeFromLeft (8);
            listenButton.setBounds (sc);
        }
        pair (row(), sharpness, &selectivity);
    }

    // Knob row
    auto knobs = r.removeFromBottom (juce::jlimit (110, 170, getHeight() / 4)).reduced (12, 6);
    const int w = knobs.getWidth() / 9;
    for (auto* k : { &depth, &detail, &attack, &release })
        k->setBounds (knobs.removeFromLeft (w));
    knobs.removeFromLeft (w);
    for (auto* k : { &mix, &maxCut, &wetTrim, &outTrim })
        k->setBounds (knobs.removeFromLeft (w));

    analyzer.setBounds (r.reduced (8, 0));
    bandOverlay.setBounds (analyzer.getBounds());
}
