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
    for (auto* b : { &softButton, &hardButton })
    {
        addAndMakeVisible (*b);
        b->setClickingTogglesState (false);
    }
    softButton.setButtonText ("soft");
    hardButton.setButtonText ("hard");
    softButton.setTooltip ("Adaptive threshold: reacts to tonal balance, largely level independent");
    hardButton.setTooltip ("Fixed threshold: louder material is cut harder");
    modeAttachment = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter (ids::mode), [this] (float v) {
        softButton.setToggleState (v < 0.5f, juce::dontSendNotification);
        hardButton.setToggleState (v >= 0.5f, juce::dontSendNotification);
    }, &p.undoManager);
    softButton.onClick = [this] { modeAttachment->setValueAsCompleteGesture (0.0f); };
    hardButton.onClick = [this] { modeAttachment->setValueAsCompleteGesture (1.0f); };
    modeAttachment->sendInitialUpdate();

    // Presets
    presetBox.setTextWhenNothingSelected ("presets");
    presetBox.setTooltip ("Factory and user presets");
    presetBox.onChange = [this] {
        const int id = presetBox.getSelectedId();
        if (id <= 0)
            return;
        if (id < 1000)
            processor.presets.loadFactory (presetBox.getItemText (presetBox.getSelectedItemIndex()));
        else
            processor.presets.loadUser (presetBox.getItemText (presetBox.getSelectedItemIndex()));
        processor.undoManager.clearUndoHistory();
    };
    addAndMakeVisible (presetBox);
    refreshPresetList();

    addButton (saveButton, "save", "Save the current settings as a user preset", [this] { savePresetDialog(); });
    addButton (deleteButton, "delete", "Delete the selected user preset", [this] {
        if (presetBox.getSelectedId() >= 1000)
        {
            processor.presets.deleteUser (presetBox.getText());
            refreshPresetList();
        }
    });
    addButton (aButton, "A", "Settings slot A", [this] { processor.switchToSlot (0); refreshAB(); });
    addButton (bButton, "B", "Settings slot B", [this] { processor.switchToSlot (1); refreshAB(); });
    addButton (copyButton, "copy", "Copy the active slot to the other one", [this] { processor.copyActiveToOther(); });
    addButton (undoButton, "undo", "Undo (Ctrl+Z)", [this] { processor.undoManager.undo(); });
    addButton (redoButton, "redo", "Redo (Ctrl+Shift+Z)", [this] { processor.undoManager.redo(); });
    refreshAB();
    startTimerHz (5);

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
    setResizeLimits (820, 500, 2000, 1300);
    setSize (1000, 640);
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

void ResonanceSuppressorEditor::addButton (juce::TextButton& b, const juce::String& text, const juce::String& tooltip, std::function<void()> onClick)
{
    b.setButtonText (text);
    b.setTooltip (tooltip);
    b.onClick = std::move (onClick);
    addAndMakeVisible (b);
}

void ResonanceSuppressorEditor::refreshPresetList()
{
    presetBox.clear (juce::dontSendNotification);
    presetBox.addSectionHeading ("Factory");
    int id = 1;
    for (const auto& name : processor.presets.getFactoryNames())
        presetBox.addItem (name, id++);
    const auto user = processor.presets.getUserNames();
    if (! user.isEmpty())
    {
        presetBox.addSectionHeading ("User");
        id = 1000;
        for (const auto& name : user)
            presetBox.addItem (name, id++);
    }

    // Show the current preset's name without reloading it.
    const auto current = processor.presets.getCurrentName();
    for (int i = 0; i < presetBox.getNumItems(); ++i)
        if (presetBox.getItemText (i) == current)
            presetBox.setSelectedItemIndex (i, juce::dontSendNotification);
}

void ResonanceSuppressorEditor::savePresetDialog()
{
    saveDialog = std::make_unique<juce::AlertWindow> ("Save preset", "Name for the new user preset:", juce::MessageBoxIconType::NoIcon, this);
    saveDialog->addTextEditor ("name", processor.presets.getCurrentName());
    saveDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result) {
        if (result == 1 && saveDialog != nullptr)
        {
            processor.presets.saveUser (saveDialog->getTextEditorContents ("name"));
            refreshPresetList();
        }
        saveDialog.reset();
    }));
}

void ResonanceSuppressorEditor::refreshAB()
{
    const int slot = processor.getActiveSlot();
    aButton.setToggleState (slot == 0, juce::dontSendNotification);
    bButton.setToggleState (slot == 1, juce::dontSendNotification);
    copyButton.setButtonText (slot == 0 ? "A > B" : "B > A");
}

void ResonanceSuppressorEditor::timerCallback()
{
    undoButton.setEnabled (processor.undoManager.canUndo());
    redoButton.setEnabled (processor.undoManager.canRedo());
}

void ResonanceSuppressorEditor::mouseDown (const juce::MouseEvent&)
{
    // Each click/drag on any control becomes one undo step.
    processor.undoManager.beginNewTransaction();
}

bool ResonanceSuppressorEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
        return processor.undoManager.undo();
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)
        || key == juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0))
        return processor.undoManager.redo();
    return false;
}

void ResonanceSuppressorEditor::paint (juce::Graphics& g)
{
    g.fillAll (rs::ui::colours::background);

    g.setColour (rs::ui::colours::ink);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText ("resonance-suppressor", 16, 0, 260, 44, juce::Justification::centredLeft);

    if (panelOpen)
    {
        const auto side = getLocalBounds().withTrimmedTop (78).removeFromRight (230).reduced (8);
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

    // Header row: title, presets, A/B, undo ... bypass, more
    auto header = r.removeFromTop (44).reduced (10, 9);
    header.removeFromLeft (200);
    presetBox.setBounds (header.removeFromLeft (220));
    header.removeFromLeft (4);
    saveButton.setBounds (header.removeFromLeft (46));
    header.removeFromLeft (4);
    deleteButton.setBounds (header.removeFromLeft (54));
    header.removeFromLeft (14);
    aButton.setBounds (header.removeFromLeft (28));
    bButton.setBounds (header.removeFromLeft (28));
    header.removeFromLeft (4);
    copyButton.setBounds (header.removeFromLeft (52));
    header.removeFromLeft (14);
    undoButton.setBounds (header.removeFromLeft (46));
    header.removeFromLeft (4);
    redoButton.setBounds (header.removeFromLeft (46));
    panelButton.setBounds (header.removeFromRight (60));
    header.removeFromRight (6);
    bypassButton.setBounds (header.removeFromRight (70));

    // Toolbar row: mode ... quality, latency, phase, delta
    auto toolbar = r.removeFromTop (34).reduced (10, 4);
    toolbar.removeFromLeft (34);
    softButton.setBounds (toolbar.removeFromLeft (52));
    hardButton.setBounds (toolbar.removeFromLeft (52));
    deltaButton.setBounds (toolbar.removeFromRight (60));
    toolbar.removeFromRight (6);
    linearButton.setBounds (toolbar.removeFromRight (104));
    toolbar.removeFromRight (6);
    lowLatencyButton.setBounds (toolbar.removeFromRight (100));
    toolbar.removeFromRight (6);
    qualityBox.setBounds (toolbar.removeFromRight (90));

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
