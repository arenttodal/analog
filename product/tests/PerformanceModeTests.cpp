// PERFORMANCE MODE R1 in ANDOR ANALOG (shared shell integration; spec docs/specs/PERFORMANCE_MODE_R1.md).
#include "AnalogParameters.h"
#include "CeramicSkin.h"
#include "Design.h"
#include "PerformanceView.h"
#include "ShellEditor.h"
#include "ShellProcessor.h"

#include <catch2/catch_test_macros.hpp>

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
    CHECK_FALSE (ui.modulationPanel().isVisible());
    CHECK (ui.visibleCardCount() == 2);
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
    ui.setPerformance (false);
    p.setPerformanceView ({});
}
