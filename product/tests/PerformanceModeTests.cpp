// PERFORMANCE MODE R1 in ANDOR ANALOG (shared shell integration; spec docs/specs/PERFORMANCE_MODE_R1.md).
#include "AnalogParameters.h"
#include "CeramicSkin.h"
#include "Design.h"
#include "PerformanceView.h"
#include "ShellEditor.h"
#include "InstrumentFrame.h"
#include "ShapingPopups.h"
#include "ShellProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace
{
    using andor::shell::ShellEditor;
    using andor::shell::ShellProcessor;
    namespace pv = osp::plugin::performance;
    constexpr double rate = 48000.0;
    constexpr int block = 512;

    juce::AudioBuffer<float> phrase (ShellProcessor& p, int blocks)
    {
        juce::AudioBuffer<float> out (2, blocks * block), buffer (2, block);
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)
                for (const int n : { 48, 55, 60, 64 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, n, static_cast<juce::uint8> (100)), 10);
            if (b == blocks / 2)
                for (const int n : { 48, 55, 60, 64 })
                    midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);
            p.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, b * block, buffer, ch, 0, block);
        }
        return out;
    }

    juce::StringArray parameterValues (ShellProcessor& p)
    {
        juce::StringArray out;
        for (auto* param : p.getParameters())
            out.add (juce::String (param->getValue(), 9));
        return out;
    }

    ShellEditor& editorOf (std::unique_ptr<juce::AudioProcessorEditor>& e) { return dynamic_cast<ShellEditor&> (*e); }
}

TEST_CASE ("Performance Mode (ANALOG): one key; cards become pictures; the strip shows the core macros; editing areas hidden and back; the sound never changes", "[analog][performance][ui]")
{
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p, reference;
    p.prepareToPlay (rate, block);
    reference.prepareToPlay (rate, block);
    p.setPerformanceView ({});   // (a new musician: Edit Mode)
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = editorOf (editor);
    editor->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
    ui.refreshNow();
    const auto before = parameterValues (p);
    const float editHeight = ui.instrumentHeight();
    const int editWell = ui.engineCard (0).display().getHeight();
    auto& key = ui.performanceKey();
    CHECK (key.isVisible());
    CHECK (key.getTitle() == "Enter Performance Mode");
    CHECK_FALSE (ui.performanceShown());

    // Performance: one click.
    key.onClick();
    ui.refreshNow();
    REQUIRE (ui.performanceShown());
    CHECK (key.getTitle() == "Edit Instrument");
    CHECK (p.performanceView().performance);
    CHECK (ui.instrumentHeight() <= editHeight);
    // The cards: pictures only (no badge, keys or knobs); an empty hybrid leaves no column.
    CHECK (ui.engineCard (0).inPerformance());
    // The picture keeps the dark well's Edit Mode height (the card is shorter instead).
    CHECK (std::abs (ui.engineCard (0).display().getHeight() - editWell) <= 1);

    // The knob row stays (as Edit Mode shows it), under the picture and inside the card.
    {
        auto& card = ui.engineCard (0);
        int knobs = 0;
        for (int k = 0; k < 5; ++k)
            if (auto* knob = card.cardKnob (k); knob != nullptr && knob->isVisible())
            {
                ++knobs;
                CHECK (knob->getY() >= card.display().getBottom());
                CHECK (card.getLocalBounds().contains (knob->getBounds()));
            }
        CHECK (knobs >= 3);
        bool self = true, children = false;
        card.getInterceptsMouseClicks (self, children);
        CHECK_FALSE (self);
        CHECK (children);   // (its knobs still play)
    }
    CHECK (ui.instrumentHeight() < editHeight);
    CHECK_FALSE (ui.modulationPanel().isVisible());
    CHECK (ui.visibleCardCount() == 2);
    // ARP and SUB: their lights still switch them (a click anywhere on a row); nothing opens.
    {
        auto& arp = ui.arpControl();
        REQUIRE (arp.isVisible());
        CHECK (arp.isLightsOnly());
        const bool arpWas = arp.isOn();
        const bool subWas = arp.isSecondRowOn();
        const auto upperRow = arp.chevronArea().getCentre().roundToInt();   // (where the chevron was)
        const auto lowerRow = arp.secondRowArea().getCentre().roundToInt();
        juce::ModifierKeys none;
        auto clickAt = [&] (juce::Point<int> at) {
            const auto pos = at.toFloat();
            arp.mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, none, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                             &arp, &arp, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false));
        };
        clickAt (upperRow);
        CHECK (arp.isOn() != arpWas);
        clickAt (lowerRow);
        CHECK (arp.isSecondRowOn() != subWas);
        CHECK_FALSE (ui.arpInlinePanel().isVisible());   // (the editor never opened)
        CHECK (ui.instrumentHeight() < editHeight);
        clickAt (upperRow);
        clickAt (lowerRow);
        CHECK (arp.isOn() == arpWas);
        CHECK (arp.isSecondRowOn() == subWas);
    }
    // The strip: the six core macros, in the panel's order (no USER macro is assigned).
    CHECK (ui.performanceMacrosShown().size() == 6);
    // The flap sits inside the macro panel's top edge (the panel never grows for it).
    const auto panel = osp::plugin::design::layout::macroPanel.translated (0.0f, -ui.sourceTrim()).toNearestInt();
    CHECK (panel.contains (ui.performanceFlap().getBounds()));
    CHECK (ui.performanceFlap().isVisible());
    // The picker: choose four, order them; the strip follows at once; never a value changed.
    ui.togglePerformancePicker();
    auto* picker = ui.performancePicker();
    REQUIRE (picker != nullptr);
    const auto core = ui.performanceMacrosShown();
    picker->toggle (core[0]);
    picker->toggle (core[1]);
    CHECK (ui.performanceMacrosShown().size() == 4);
    picker->move (core[5], -3);
    CHECK (ui.performanceMacrosShown()[0] == core[5]);
    picker->toggle (core[2]);
    picker->toggle (core[3]);
    picker->toggle (core[4]);   // (never none: the last one stays)
    CHECK (ui.performanceMacrosShown() == juce::StringArray ({ core[5] }));
    picker->reset();
    CHECK (ui.performanceMacrosShown() == core);
    CHECK_FALSE (p.performanceView().customised);
    ui.togglePerformancePicker();
    CHECK (ui.performancePicker() == nullptr);

    // Back to Edit: everything where it was.
    key.onClick();
    ui.refreshNow();
    CHECK_FALSE (ui.performanceShown());
    CHECK_FALSE (ui.engineCard (0).inPerformance());
    CHECK_FALSE (ui.arpControl().isLightsOnly());
    CHECK (ui.modulationPanel().isVisible());
    CHECK (ui.instrumentHeight() == editHeight);
    CHECK (ui.visibleCardCount() == 3);
    CHECK_FALSE (ui.performanceFlap().isVisible());

    // The sound and every parameter: unchanged by the view.
    CHECK (parameterValues (p) == before);
    const auto a = phrase (p, 40);
    const auto b = phrase (reference, 40);
    bool same = true;
    for (int ch = 0; ch < 2 && same; ++ch)
        for (int i = 0; i < a.getNumSamples() && same; ++i)
            same = a.getSample (ch, i) == b.getSample (ch, i);
    CHECK (same);
    p.setPerformanceView ({});
}

TEST_CASE ("Performance Mode (ANALOG): a new editor opens in the view this instance was left in", "[analog][performance][ui][state]")
{
    ShellProcessor p;
    pv::State on;
    on.performance = true;
    p.setPerformanceView (on);
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
        CHECK (editorOf (editor).performanceShown());
    }
    p.setPerformanceView ({});
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
        CHECK_FALSE (editorOf (editor).performanceShown());
    }
}

TEST_CASE ("Performance Mode (ANALOG): two USER macros assigned - the default shows eight, mixed; the flap never covers a name; the strip at 150 % and 200 %", "[analog][performance][ui]")
{
    ShellProcessor p;
    p.prepareToPlay (rate, block);
    p.setPerformanceView ({});
    auto* hooks = p.userMacros();
    REQUIRE (hooks != nullptr);
    REQUIRE (hooks->assign (p, 0, "drive"));
    REQUIRE (hooks->assign (p, 2, "space"));
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = editorOf (editor);
    ui.setPerformance (true);
    for (const float scale : { 1.0f, 1.5f, 2.0f })
    {
        editor->setSize (juce::roundToInt (1448.0f * scale), juce::roundToInt (ui.instrumentHeight() * scale));
        ui.refreshNow();
        INFO ("scale " << scale);
        const auto shown = ui.performanceMacrosShown();
        REQUIRE (shown.size() == 8);
        CHECK (shown[6] == "user:0");
        CHECK (shown[7] == "user:2");
        // (content coordinates: the flap and every name on the same reference grid)
        const auto flap = ui.performanceFlap().getBounds();
        for (auto* child : ui.contentForTest().getChildren())
            if (dynamic_cast<osp::plugin::MacroLabel*> (child) != nullptr && child->isVisible())
                CHECK_FALSE (child->getBounds().intersects (flap));
    }
    // USER 1 cleared: kept in the selection, never a knob.
    hooks->clear (p, 0);
    ui.refreshNow();
    CHECK (ui.performanceMacrosShown().size() == 7);
    ui.setPerformance (false);
    p.setPerformanceView ({});
}

TEST_CASE ("Performance Mode screenshots (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-performance]")
{
    const auto dir = juce::SystemStats::getEnvironmentVariable ("ANALOG_SCREENSHOT_DIR", {});
    REQUIRE (dir.isNotEmpty());
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    p.prepareToPlay (rate, block);
    p.setPerformanceView ({});
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = editorOf (editor);
    auto shoot = [&] (const juce::String& name) {
        editor->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
        ui.refreshNow();
        const auto file = out.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), stream);
    };
    shoot ("performance-00-edit");
    ui.setPerformance (true);
    shoot ("performance-01-performance");
    ui.togglePerformancePicker();
    shoot ("performance-02-picker");
    ui.togglePerformancePicker();
    // Eight: two USER macros assigned (mixed core and USER), at 100 / 150 / 200 %.
    if (auto* hooks = p.userMacros())
    {
        hooks->assign (p, 0, "drive");
        hooks->assign (p, 2, "space");
    }
    ui.refreshNow();
    shoot ("performance-03-eight-mixed");
    for (const float scale : { 1.5f, 2.0f })
    {
        editor->setSize (juce::roundToInt (1448.0f * scale), juce::roundToInt (ui.instrumentHeight() * scale));
        ui.refreshNow();
        const auto file = out.getChildFile ("performance-04-eight-" + juce::String (juce::roundToInt (scale * 100)) + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), stream);
    }
    ui.setPerformance (false);
    p.setPerformanceView ({});
}
