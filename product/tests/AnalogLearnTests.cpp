// ANDOR Learn 2.0 in ANDOR Analog (docs/LEARN_2.md): every lesson driven end to end through the
// real controls (only the musician's gestures complete steps: automation, presets and
// programmatic changes never do), the exact control outlined for every step, no Skip anywhere,
// the sound prepared or verified before a lesson listens to it (the MORPH preparation matrix),
// the example sounds loaded only when asked and restored as promised, the one Learn menu entry,
// the browser, Info, and that closing Learn leaves no trace (no component, no parameter, no pixel).
#include "AnalogEngine.h"
#include "AnalogLearn.h"
#include "AnalogParameters.h"
#include "ArpSection.h"
#include "AuxUI.h"
#include "Binding.h"
#include "CeramicSkin.h"
#include "EffectRack.h"
#include "FxRouting.h"
#include "RackControls.h"
#include "ShapingPopups.h"
#include "ShellEditor.h"
#include "ShellProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <map>

using andor::learn::LearnController;
using andor::learn::LessonRunner;
using andor::shell::ShellProcessor;
namespace params = andor::analog::params;

namespace
{
    void setLearnDirTo (const juce::String& value)
    {
#if JUCE_WINDOWS
        _putenv_s ("ANDOR_LEARN_DIR", value.toRawUTF8());
#else
        setenv ("ANDOR_LEARN_DIR", value.toRawUTF8(), 1);
#endif
    }

    struct LearnDir
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("analog-learn-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        juce::String before = juce::SystemStats::getEnvironmentVariable ("ANDOR_LEARN_DIR", {});
        LearnDir()
        {
            dir.createDirectory();
            setLearnDirTo (dir.getFullPathName());
        }
        ~LearnDir()
        {
            setLearnDirTo (before);
            dir.deleteRecursively();
        }
    };

    float valueOf (ShellProcessor& p, const juce::String& id)
    {
        auto* param = p.parameters.getParameter (id);
        REQUIRE (param != nullptr);
        return param->convertFrom0to1 (param->getValue());
    }

    /** What automation, a preset or any non-UI writer does: the host-notifying setter. */
    void automate (ShellProcessor& p, const juce::String& id, float value)
    {
        auto* param = p.parameters.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    juce::MouseEvent eventAt (juce::Component& c, juce::Point<float> at, int clicks = 1)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (source, at, {}, juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, at, now, clicks, false);
    }

    std::map<juce::String, float> allValues (ShellProcessor& p)
    {
        std::map<juce::String, float> values;
        for (auto* param : p.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                values[ranged->getParameterID()] = ranged->getValue();
        return values;
    }

    andor::analog::AnalogEngine& engineOf (ShellProcessor& p)
    {
        auto* e = dynamic_cast<andor::analog::AnalogEngine*> (&p.productEngine());
        REQUIRE (e != nullptr);
        return *e;
    }

    /** What arrives at the sidechain: `enabled` routed by the host, `amplitude` its level. */
    void feedSidechain (ShellProcessor& p, bool enabled, float amplitude)
    {
        std::vector<float> l (512), r (512);
        for (int b = 0; b < 40; ++b)
        {
            for (std::size_t i = 0; i < l.size(); ++i)
            {
                const double t = (b * 512 + static_cast<double> (i)) / 48000.0;
                l[i] = amplitude * static_cast<float> (std::sin (2.0 * 3.14159 * 220.0 * t));
                r[i] = l[i];
            }
            andor::analog::AnalogEngine::ExternalInput in;
            in.busExists = true;
            in.busEnabled = enabled;
            in.channels = enabled ? 2 : 0;
            in.left = enabled ? l.data() : nullptr;
            in.right = enabled ? r.data() : nullptr;
            in.samples = enabled ? 512 : 0;
            engineOf (p).setExternalInput (in);
        }
    }

    struct Rig
    {
        LearnDir learnDir;
        ShellProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        andor::shell::ShellEditor* ui = nullptr;
        LearnController* learn = nullptr;

        explicit Rig (int width = 1448)
        {
            p.prepareToPlay (48000.0, 512);
            editor.reset (p.createEditorIfNeeded());
            ui = dynamic_cast<andor::shell::ShellEditor*> (editor.get());
            REQUIRE (ui != nullptr);
            learn = ui->learnController();
            REQUIRE (learn != nullptr);
            INFO (learn->problems().joinIntoString ("\n"));
            REQUIRE (learn->isUsable());
            // The CORE macros showing (the bank is an interface preference, kept between editors).
            p.setMacroBank (0);
            ui->showMacroBank (0);
            resize (width);
        }
        void resize (int width)
        {
            editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui->instrumentHeight() / 1448.0f));
            ui->refreshNow();
        }
        void settle()
        {
            for (int i = 0; i < 2; ++i)
            {
                ui->refreshNow();   // (the editor's tick: Learn follows its targets, checks what steps need)
                learn->layoutNow();
            }
        }
        LessonRunner& runner() { return learn->runner(); }
        osp::plugin::ArpInlinePanel& panel() { return ui->arpInlinePanel(); }
        /** A click on a part of the ARP panel, as the musician's press delivers it. */
        void clickPanel (juce::Rectangle<float> area)
        {
            REQUIRE_FALSE (area.isEmpty());
            panel().mouseDown (eventAt (panel(), area.getCentre()));
            settle();
        }
        /** A pattern row other than the one chosen (and other than `avoid`). */
        juce::Rectangle<float> otherPatternRow (int avoid = -1)
        {
            for (const int pattern : osp::plugin::ArpInlinePanel::listOrder())
                if (pattern != panel().fieldPattern() && pattern != avoid)
                    if (const auto r = panel().patternRowArea (pattern); ! r.isEmpty())
                        return r;
            return {};
        }
        void turnRate()
        {
            auto& knob = dynamic_cast<osp::plugin::MiniKnob&> (panel().shownRateKnob());
            const double next = knob.slider.getValue() + 1.0 <= knob.slider.getMaximum() ? knob.slider.getValue() + 1.0 : knob.slider.getValue() - 1.0;
            knob.slider.setValue (next, juce::sendNotificationSync);
            settle();
        }
        void clickArpLight()
        {
            auto& arp = ui->arpControl();
            arp.mouseDown (eventAt (arp, arp.lightArea().getCentre()));
            settle();
        }
        /** A watched control turned by the musician: pressed, moved, released. */
        void turnWatched (const juce::String& targetId, double fraction = 0.2)
        {
            auto* watcher = learn->touchWatcher (targetId);
            REQUIRE (watcher != nullptr);
            auto* c = watcher->watched();
            REQUIRE (c != nullptr);
            juce::Slider* slider = dynamic_cast<juce::Slider*> (c);
            if (auto* mini = dynamic_cast<osp::plugin::MiniKnob*> (c))
                slider = &mini->slider;
            REQUIRE (slider != nullptr);
            watcher->mouseDown (eventAt (*c, {}));
            const double span = slider->getMaximum() - slider->getMinimum();
            const double next = slider->getValue() + fraction * span <= slider->getMaximum() ? slider->getValue() + fraction * span : slider->getValue() - fraction * span;
            slider->setValue (next, juce::sendNotificationSync);
            settle();
            watcher->mouseUp (eventAt (*c, {}));
            settle();
        }
        /** A segment of a watched segmented switch clicked. */
        void clickSegment (const juce::String& targetId, int segment)
        {
            auto* watcher = learn->touchWatcher (targetId);
            REQUIRE (watcher != nullptr);
            auto* seg = dynamic_cast<osp::plugin::rackui::SegmentedSwitch*> (watcher->watched());
            REQUIRE (seg != nullptr);
            const auto at = seg->segmentBounds (segment).getCentre();
            seg->mouseDown (eventAt (*seg, at));
            watcher->mouseUp (eventAt (*seg, at));
            settle();
        }
        juce::Rectangle<int> contentArea (juce::Component& c, juce::Rectangle<int> local)
        {
            return ui->contentForTest().getLocalArea (&c, local);
        }
        std::optional<juce::Rectangle<int>> ring() { return learn->highlight() != nullptr ? learn->highlight()->target() : std::nullopt; }
        /** The green outline (there must be one). */
        juce::Rectangle<int> outline()
        {
            const auto r = ring();
            REQUIRE (r.has_value());
            return *r;
        }
        /** The note never covers the outlined control, and stays inside what is in view. */
        void checkNotePlacement()
        {
            REQUIRE (learn->note() != nullptr);
            const auto note = learn->note()->getBounds();
            CHECK (ui->contentForTest().getLocalBounds().contains (note));
            if (const auto r = ring())
                CHECK_FALSE (note.reduced (andor::learn::Note::margin).intersects (r->expanded (5)));
        }
        int learnChildren()
        {
            int n = 0;
            for (auto* c : ui->contentForTest().getChildren())
                if (dynamic_cast<andor::learn::HighlightLayer*> (c) != nullptr || dynamic_cast<andor::learn::Note*> (c) != nullptr
                    || dynamic_cast<andor::learn::Browser*> (c) != nullptr || dynamic_cast<andor::learn::HitRegion*> (c) != nullptr)
                    ++n;
            return n;
        }
        juce::Image snapshot() { return editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f); }
        juce::StringArray keys() { return learn->note() != nullptr ? learn->note()->actionIds() : juce::StringArray(); }
    };

    bool samePixels (const juce::Image& a, const juce::Image& b)
    {
        if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight())
            return false;
        const juce::Image::BitmapData da (a, juce::Image::BitmapData::readOnly), db (b, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (da.getPixelColour (x, y) != db.getPixelColour (x, y))
                    return false;
        return true;
    }

    /** The parameters whose values differ (for a failing comparison's message). */
    juce::String differences (const std::map<juce::String, float>& a, const std::map<juce::String, float>& b)
    {
        juce::StringArray out;
        for (const auto& [id, v] : a)
            if (const auto it = b.find (id); it == b.end() || std::abs (it->second - v) > 1.0e-6f)
                out.add (id + " " + juce::String (v) + " -> " + (it == b.end() ? juce::String ("-") : juce::String (it->second)));
        return out.joinIntoString (", ");
    }

    /** The same values (within a float's round trip through the saved state). */
    bool sameValues (const std::map<juce::String, float>& a, const std::map<juce::String, float>& b)
    {
        return a.size() == b.size() && differences (a, b).isEmpty();
    }

    juce::StringArray menuTexts (const juce::PopupMenu& menu, bool recursive)
    {
        juce::StringArray texts;
        for (juce::PopupMenu::MenuItemIterator it (menu, recursive); it.next();)
            if (it.getItem().text.isNotEmpty())
                texts.add (it.getItem().text);
        return texts;
    }
} // namespace

TEST_CASE ("analog learn: the catalog is valid; idle Learn adds nothing; ONE Learn menu entry (Tutorials, Toggle Info, Reset Tutorials)", "[learn][analog]")
{
    Rig rig;
    CHECK (rig.learnChildren() == 0);
    CHECK_FALSE (rig.learn->isActive());
    const auto catalog = andor::analog::learnCatalog();
    REQUIRE (catalog.lessons.size() == 6);
    juce::PopupMenu menu;
    rig.learn->addMenuItems (menu);
    const auto top = menuTexts (menu, false);
    CHECK (top.size() == 1);
    CHECK (top.contains ("Learn"));
    const auto all = menuTexts (menu, true);
    CHECK (all.contains (juce::String::fromUTF8 ("Tutorials\xe2\x80\xa6")));
    CHECK (all.contains ("Toggle Info"));
    CHECK (all.contains (juce::String::fromUTF8 ("Reset Tutorials\xe2\x80\xa6")));
    for (const auto& gone : { "Learn mode", "Learn ANDOR\xe2\x80\xa6", "Reset learning progress" })
        CHECK_FALSE (all.contains (juce::String::fromUTF8 (gone)));
    // Toggle Info shows its tick while Info is on.
    rig.learn->setLearnMode (true);
    juce::PopupMenu on;
    rig.learn->addMenuItems (on);
    bool ticked = false;
    for (juce::PopupMenu::MenuItemIterator it (on, true); it.next();)
        if (it.getItem().text == "Toggle Info")
            ticked = it.getItem().isTicked;
    CHECK (ticked);
    rig.learn->setLearnMode (false);
}

TEST_CASE ("analog learn: no lesson step ever shows Skip; every key is a short label (never a sentence)", "[learn][analog]")
{
    Rig rig;
    const auto before = allValues (rig.p);
    for (const auto& lesson : rig.learn->catalog().lessons)
        for (int s = 0; s < static_cast<int> (lesson.steps.size()); ++s)
        {
            INFO (lesson.id << " step " << s);
            REQUIRE (rig.learn->startLesson (lesson.id, s));
            rig.settle();
            REQUIRE (rig.learn->note() != nullptr);
            for (const auto& id : rig.keys())
                CHECK (id != "skip");
            for (const auto& label : rig.learn->note()->actionLabels())
            {
                CHECK_FALSE (label.containsIgnoreCase ("skip"));
                CHECK_FALSE (label.contains (juce::String::fromUTF8 ("\xc2\xb7")));   // ("Already open · Continue" is gone)
                CHECK (label.length() <= 24);
            }
            // Back on every step after the first; the X always closes.
            CHECK (rig.keys().contains ("back") == (s > 0));
            rig.learn->noteAction ("close");
            CHECK (rig.learn->note() == nullptr);
        }
    // Opening every step's controls is navigation only: the sound is untouched.
    CHECK (allValues (rig.p) == before);
}

TEST_CASE ("analog learn: Make an arpeggio - nine steps, each control outlined exactly, completed only by the musician", "[learn][analog]")
{
    Rig rig;
    auto& p = rig.p;
    automate (p, "arp.enabled", 0.0f);
    automate (p, "analog.arp.link", 1.0f);
    for (const auto* card : { "analog.arp.cardA", "analog.arp.cardB", "analog.arp.cardC" })
        automate (p, card, 1.0f);
    rig.ui->setArpExpanded (false);
    rig.settle();
    REQUIRE (rig.learn->startLesson ("analog.arp.basics"));
    rig.settle();
    auto& runner = rig.runner();
    auto& arp = rig.ui->arpControl();

    // 1. Open: the ARP opener itself is outlined (not the SUB row), the note beside it.
    CHECK (runner.stepIndex() == 0);
    CHECK (rig.learn->note()->metaText() == "ARPEGGIATOR");
    CHECK (rig.learn->note()->stepText() == "STEP 1 OF 9");
    REQUIRE (rig.ring().has_value());
    CHECK (rig.outline().getHeight() < rig.contentArea (arp, arp.getLocalBounds()).getHeight());
    rig.checkNotePlacement();
    // A session recalled with the panel open: Continue with a status line (no click needed).
    rig.ui->setArpExpanded (true);
    rig.settle();
    CHECK (rig.keys().contains ("continue"));
    CHECK (rig.learn->note()->statusText() == "The arpeggiator is already open.");
    rig.ui->setArpExpanded (false);
    rig.settle();
    arp.mouseDown (eventAt (arp, arp.chevronArea().getCentre()));
    rig.settle();
    CHECK (runner.state() == LessonRunner::State::stepComplete);
    CHECK (rig.learn->note()->showsDone());
    rig.learn->noteAction ("next");

    // 2. ARP on: its light outlined; automation never counts.
    REQUIRE (runner.stepIndex() == 1);
    CHECK (rig.outline() == rig.contentArea (arp, arp.lightArea().expanded (4.0f).getSmallestIntegerContainer()));
    automate (p, "arp.enabled", 1.0f);
    rig.settle();
    CHECK (runner.status (1) == LessonRunner::StepStatus::pending);
    automate (p, "arp.enabled", 0.0f);
    rig.settle();
    rig.clickArpLight();
    CHECK (runner.status (1) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");

    // 3. PATTERN, 4. RATE.
    REQUIRE (runner.stepIndex() == 2);
    rig.clickPanel (rig.otherPatternRow());
    CHECK (runner.status (2) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");
    REQUIRE (runner.stepIndex() == 3);
    CHECK (rig.outline() == rig.contentArea (rig.panel(), rig.panel().shownRateKnob().getBounds()));
    rig.turnRate();
    CHECK (runner.status (3) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");

    // 5. PLAYS A off: A's key alone is outlined; B's click does not count.
    REQUIRE (runner.stepIndex() == 4);
    CHECK (rig.outline() == rig.contentArea (rig.panel(), rig.panel().cardChipArea (0).getSmallestIntegerContainer()));
    rig.checkNotePlacement();
    rig.clickPanel (rig.panel().cardChipArea (1));   // B off: not this step's task
    CHECK (runner.state() == LessonRunner::State::waitingForAction);
    rig.clickPanel (rig.panel().cardChipArea (1));   // (B back on)
    rig.clickPanel (rig.panel().cardChipArea (0));   // A off
    CHECK (runner.status (4) == LessonRunner::StepStatus::done);
    CHECK (rig.learn->note()->statusText() == "OSC 1 now plays sustained notes.");
    rig.learn->noteAction ("next");

    // 6. PLAYS B on (already): Continue.
    REQUIRE (runner.stepIndex() == 5);
    CHECK (rig.outline() == rig.contentArea (rig.panel(), rig.panel().cardChipArea (1).getSmallestIntegerContainer()));
    CHECK (rig.keys().contains ("continue"));
    rig.learn->noteAction ("continue");

    // 7. LINK off.
    REQUIRE (runner.stepIndex() == 6);
    CHECK (rig.outline() == rig.contentArea (rig.panel(), rig.panel().linkArea().getSmallestIntegerContainer()));
    rig.clickPanel (rig.panel().linkArea());
    CHECK (runner.status (6) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");

    // 8. EDIT B: B's tab outlined; selecting it is the task (never done for the musician).
    REQUIRE (runner.stepIndex() == 7);
    CHECK (rig.panel().editedCard() == 0);
    CHECK (rig.outline() == rig.contentArea (rig.panel(), rig.panel().editTabArea (1).getSmallestIntegerContainer()));
    rig.clickPanel (rig.panel().editTabArea (1));
    CHECK (runner.status (7) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");

    // 9. B's own pattern: A's change does not count; B's does.
    REQUIRE (runner.stepIndex() == 8);
    const float globalPattern = valueOf (p, "arp.pattern");
    const int bPattern = juce::roundToInt (valueOf (p, "analog.arp.cardB.pattern"));
    rig.clickPanel (rig.otherPatternRow (bPattern));
    CHECK (runner.status (8) == LessonRunner::StepStatus::done);
    CHECK (juce::exactlyEqual (valueOf (p, "arp.pattern"), globalPattern));
    CHECK (rig.keys().contains ("finish"));
    rig.learn->noteAction ("finish");
    CHECK (rig.learn->noteKind() == LearnController::NoteKind::complete);
    CHECK (rig.learn->note()->bodyText().contains ("OSC 1 is sustaining the chord"));
    CHECK (rig.keys().contains ("browse"));   // (no example was loaded: Tutorials / Close)
    CHECK (rig.learn->store().read().lessons.at ("analog.arp.basics").status == "complete");
    rig.learn->noteAction ("close");
    CHECK (rig.learnChildren() == 0);
}

TEST_CASE ("analog learn: Understand MORPH - prepared sound, ARP off, MORPH TO's exact segments, example loaded only when asked and restored", "[learn][analog][morph]")
{
    Rig rig;
    auto& p = rig.p;
    // The musician's own sound: card C empty (a SAMPLE without a recording), D off, ARP on.
    automate (p, params::hybridType, 0.0f);
    automate (p, params::auxEnabled, 0.0f);
    automate (p, "arp.enabled", 1.0f);
    automate (p, params::morphPosition, 35.0f);
    rig.settle();
    const auto mine = allValues (p);
    REQUIRE (rig.learn->startLesson ("analog.morph.basics"));
    rig.settle();
    auto& runner = rig.runner();
    // Starting never changes the sound.
    CHECK (allValues (p) == mine);
    // Preparation: what is missing, Load Example Sound first; nothing loaded yet.
    REQUIRE (runner.step()->preparation);
    CHECK_FALSE (runner.preparationReady());
    CHECK (rig.keys() == juce::StringArray ({ "current", "example" }));
    CHECK (rig.learn->note()->statusText().contains ("OSC C"));
    rig.learn->noteAction ("example");
    rig.settle();
    // The example: oscillators, OSC C as an OSCILLATOR, D as NOISE (no recording, no Kontakt,
    // no sidechain), MORPH on toward C at 0 %, ARP off.
    CHECK (juce::roundToInt (valueOf (p, params::hybridType)) == 1);
    CHECK (juce::roundToInt (valueOf (p, params::auxMode)) == 1);
    CHECK (valueOf (p, params::auxEnabled) > 0.5f);
    CHECK (valueOf (p, params::morphEnabled) > 0.5f);
    CHECK (juce::roundToInt (valueOf (p, params::morphTarget)) == 0);
    CHECK (valueOf (p, "arp.enabled") < 0.5f);
    CHECK (p.canRestoreLearnSound());
    REQUIRE (runner.stepIndex() == 1);

    // 1. ARP off - already, in the example: Continue.
    CHECK (rig.keys().contains ("continue"));
    rig.learn->noteAction ("continue");
    // 2. Open the rack: a double-click on MORPH's name.
    REQUIRE (runner.stepIndex() == 2);
    auto* label = rig.ui->macroLabel (0);
    REQUIRE (label != nullptr);
    CHECK (rig.outline() == rig.contentArea (*label, label->getLocalBounds()));
    label->mouseDoubleClick (eventAt (*label, label->getLocalBounds().getCentre().toFloat(), 2));
    rig.settle();
    CHECK (runner.status (2) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");
    // 3. MORPH on (already).
    REQUIRE (runner.stepIndex() == 3);
    CHECK (rig.outline() == rig.contentArea (*rig.ui->macroSwitch (0), rig.ui->macroSwitch (0)->getLocalBounds()));
    rig.learn->noteAction ("continue");
    // 4. MORPH TO C: the C segment exactly - not the rack.
    REQUIRE (runner.stepIndex() == 4);
    auto* morphTo = dynamic_cast<osp::plugin::rackui::SegmentedSwitch*> (rig.learn->touchWatcher ("morph.destination")->watched());
    REQUIRE (morphTo != nullptr);
    CHECK (rig.outline() == rig.contentArea (*morphTo, morphTo->segmentBounds (0).getSmallestIntegerContainer()));
    CHECK (rig.outline().getWidth() < morphTo->getWidth());
    rig.checkNotePlacement();
    rig.learn->noteAction ("continue");
    // 5. Hear the two sources: the MORPH knob, turned by the musician.
    REQUIRE (runner.stepIndex() == 5);
    CHECK (rig.outline() == rig.contentArea (*rig.ui->macroKnob (0), rig.ui->macroKnob (0)->getLocalBounds()));
    automate (p, params::morphPosition, 60.0f);   // automation: nothing
    rig.settle();
    CHECK (runner.state() == LessonRunner::State::waitingForAction);
    rig.turnWatched ("macro.0");
    CHECK (runner.status (5) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");
    // 6. TEXTURE, 7. METHOD: each its own control.
    REQUIRE (runner.stepIndex() == 6);
    auto* texture = rig.learn->touchWatcher ("morph.texture")->watched();
    REQUIRE (texture != nullptr);
    CHECK (rig.outline() == rig.contentArea (*texture, texture->getLocalBounds()));
    rig.turnWatched ("morph.texture");
    CHECK (runner.status (6) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");
    REQUIRE (runner.stepIndex() == 7);
    const int method = juce::roundToInt (valueOf (p, params::morphMethod));
    rig.clickSegment ("morph.method", (method + 1) % 3);
    CHECK (runner.status (7) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");
    // 8. MORPH TO D: the D segment exactly; D (keyed NOISE) is ready.
    REQUIRE (runner.stepIndex() == 8);
    morphTo = dynamic_cast<osp::plugin::rackui::SegmentedSwitch*> (rig.learn->touchWatcher ("morph.destination")->watched());
    REQUIRE (morphTo != nullptr);
    CHECK (rig.outline() == rig.contentArea (*morphTo, morphTo->segmentBounds (1).getSmallestIntegerContainer()));
    rig.clickSegment ("morph.destination", 1);
    CHECK (juce::roundToInt (valueOf (p, params::morphTarget)) == 1);
    CHECK (runner.status (8) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");
    // 9. Hear D.
    REQUIRE (runner.stepIndex() == 9);
    rig.turnWatched ("macro.0");
    CHECK (runner.status (9) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("finish");
    // Complete: the lesson played on its example - Keep it, or bring back the sound from before.
    CHECK (rig.learn->noteKind() == LearnController::NoteKind::complete);
    CHECK (rig.keys() == juce::StringArray ({ "restore", "keep" }));
    rig.learn->noteAction ("restore");
    INFO (differences (mine, allValues (p)));
    CHECK (sameValues (allValues (p), mine));   // the musician's sound again
    CHECK_FALSE (p.canRestoreLearnSound());
    CHECK (rig.learn->store().read().lessons.at ("analog.morph.basics").status == "complete");
}

TEST_CASE ("analog learn: MORPH preparation matrix - every unsuitable state says what is missing and offers a next action", "[learn][analog][morph]")
{
    struct Case
    {
        const char* name;
        std::function<void (ShellProcessor&)> setUp;
        const char* missing;   ///< the first unmet requirement's id ("": ready)
    };
    const std::vector<Case> cases {
        { "empty C, D off", [] (ShellProcessor& p) { automate (p, params::hybridType, 0.0f); automate (p, params::auxEnabled, 0.0f); }, "c.loaded" },
        { "valid C (OSC), D off", [] (ShellProcessor& p) { automate (p, params::hybridType, 1.0f); automate (p, params::auxEnabled, 0.0f); }, "d.on" },
        { "C muted", [] (ShellProcessor& p) { automate (p, params::hybridType, 1.0f); automate (p, "layerB.mute", 1.0f); }, "c.heard" },
        { "C level at zero", [] (ShellProcessor& p) {
              automate (p, params::hybridType, 1.0f);
              auto* level = p.parameters.getParameter ("layerB.level");
              level->setValueNotifyingHost (0.0f);
          }, "c.level" },
        { "D RHYTHM without a loop", [] (ShellProcessor& p) {
              automate (p, params::hybridType, 1.0f); automate (p, params::auxEnabled, 1.0f); automate (p, params::auxMode, 0.0f); }, "d.playable" },
        { "D LIVE INPUT with no sidechain", [] (ShellProcessor& p) {
              automate (p, params::hybridType, 1.0f); automate (p, params::auxEnabled, 1.0f); automate (p, params::auxMode, 3.0f);
              feedSidechain (p, false, 0.0f); }, "d.playable" },
        { "D LIVE INPUT with audio arriving", [] (ShellProcessor& p) {
              automate (p, params::hybridType, 1.0f); automate (p, params::auxEnabled, 1.0f); automate (p, params::auxMode, 3.0f);
              feedSidechain (p, true, 0.5f); }, "" },
        { "D NOISE, C NOISE", [] (ShellProcessor& p) {
              automate (p, params::hybridType, 2.0f); automate (p, params::auxEnabled, 1.0f); automate (p, params::auxMode, 1.0f); }, "" },
        { "A and B silent", [] (ShellProcessor& p) {
              automate (p, params::hybridType, 1.0f); automate (p, params::auxEnabled, 1.0f); automate (p, params::auxMode, 1.0f);
              automate (p, params::osc1Enabled, 0.0f); automate (p, params::osc2Enabled, 0.0f); }, "ab.audible" },
    };
    for (const auto& c : cases)
    {
        DYNAMIC_SECTION (c.name)
        {
            Rig rig;
            c.setUp (rig.p);
            rig.settle();
            REQUIRE (rig.learn->startLesson ("analog.morph.basics"));
            rig.settle();
            auto& runner = rig.runner();
            const auto* missing = runner.unmetRequirement();
            if (juce::String (c.missing).isEmpty())
            {
                CHECK (runner.preparationReady());
                CHECK (rig.learn->note()->statusText() == "Your current sound is ready for this lesson.");
                CHECK (rig.keys() == juce::StringArray ({ "example", "current" }));   // (Use Current Sound first)
                continue;
            }
            REQUIRE (missing != nullptr);
            CHECK (missing->id == c.missing);
            CHECK (missing->title.isNotEmpty());
            CHECK (missing->body.isNotEmpty());
            CHECK (rig.keys() == juce::StringArray ({ "current", "example" }));   // (Load Example Sound first)
            // Keeping the current sound: the step that listens to it blocks with the same message,
            // its fix outlined and offered (never a silent demonstration marked done).
            rig.learn->noteAction ("current");
            const bool aux = juce::String (c.missing).startsWith ("d.");
            rig.ui->setRackExpanded (osp::plugin::rack::Kind::morph, true);
            REQUIRE (rig.learn->startLesson ("analog.morph.basics", aux ? 8 : 5));   // (MORPH TO D / hearing C)
            rig.settle();
            REQUIRE (runner.unmetRequirement() != nullptr);
            CHECK (runner.unmetRequirement()->id == c.missing);
            REQUIRE (runner.state() == LessonRunner::State::blocked);
            const auto keys = rig.keys();
            CHECK ((keys.contains ("fix") || keys.contains ("example")));
            CHECK_FALSE (keys.contains ("next"));
            CHECK_FALSE (keys.contains ("continue"));
        }
    }
    // MORPH off: hearing the blend waits for the switch, pointing at it.
    {
        Rig rig;
        automate (rig.p, params::hybridType, 1.0f);
        automate (rig.p, params::morphEnabled, 0.0f);
        rig.ui->setRackExpanded (osp::plugin::rack::Kind::morph, true);
        rig.settle();
        REQUIRE (rig.learn->startLesson ("analog.morph.basics", 5));
        rig.settle();
        REQUIRE (rig.runner().state() == LessonRunner::State::blocked);
        CHECK (rig.runner().unmetRequirement()->id == "morph.on");
        INFO ("ring " << rig.outline().toString() << " lamp " << rig.contentArea (*rig.ui->macroSwitch (0), rig.ui->macroSwitch (0)->getLocalBounds()).toString());
        CHECK (rig.outline() == rig.contentArea (*rig.ui->macroSwitch (0), rig.ui->macroSwitch (0)->getLocalBounds()));
    }
    // ARP on: the first step asks for it off and points at its light; the click completes it.
    {
        Rig rig;
        automate (rig.p, "arp.enabled", 1.0f);
        rig.settle();
        REQUIRE (rig.learn->startLesson ("analog.morph.basics"));
        rig.learn->noteAction ("current");
        rig.settle();
        REQUIRE (rig.runner().stepIndex() == 1);
        CHECK_FALSE (rig.keys().contains ("continue"));
        rig.clickArpLight();
        CHECK (rig.runner().status (1) == LessonRunner::StepStatus::done);
    }
}

TEST_CASE ("analog learn: Create a macro - OSC 1 LEVEL's menu, Macro 1 verified, its range, turned", "[learn][analog][usermacro]")
{
    Rig rig;
    auto& p = rig.p;
    p.setMacroBank (0);
    rig.ui->showMacroBank (0);
    REQUIRE (rig.learn->startLesson ("analog.usermacros.basics"));
    auto& runner = rig.runner();
    rig.settle();
    // 1. USER.
    REQUIRE (rig.ui->macroBankTab() != nullptr);
    CHECK (rig.outline() == rig.contentArea (*rig.ui->macroBankTab(), rig.ui->macroBankTab()->getLocalBounds()));
    rig.ui->macroBankTab()->onChange (1);
    rig.settle();
    CHECK (runner.status (0) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");
    // 2. OSC 1 LEVEL outlined; another control's menu does not count; its own does - and the
    // lesson moves on by itself (Next would close the menu).
    REQUIRE (runner.stepIndex() == 1);
    auto* level = osp::plugin::binding::findControl (rig.ui->instrumentContent(), params::osc1Level);
    REQUIRE (level != nullptr);
    CHECK (rig.outline() == rig.contentArea (*level, level->getLocalBounds()));
    juce::ignoreUnused (rig.ui->controlContextMenu (params::osc2Level));
    rig.settle();
    CHECK (runner.stepIndex() == 1);
    const auto menu = rig.ui->controlContextMenu (params::osc1Level);
    rig.settle();
    REQUIRE (runner.stepIndex() == 2);
    // 3. The menu item: no outline left on the knob several hundred pixels away; the note by it.
    CHECK_FALSE (rig.ring().has_value());
    CHECK (rig.learn->note()->bodyText().contains ("Assign to Macro"));
    auto choose = [&] (const juce::String& item) {
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
            if (it.getItem().text.startsWith (item) && it.getItem().action != nullptr)
            {
                it.getItem().action();
                return true;
            }
        return false;
    };
    REQUIRE (choose ("Macro 2"));   // the wrong macro: not this step's assignment
    rig.settle();
    CHECK (runner.state() == LessonRunner::State::waitingForAction);
    REQUIRE (choose ("Macro 1"));
    rig.settle();
    CHECK (runner.status (2) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");
    // 4. The range: an end dragged on the halo (committed).
    REQUIRE (runner.stepIndex() == 3);
    rig.ui->modulationOverlay().editMacroRange (0, params::osc1Level, 1, 0.9f, osp::plugin::ModulationOverlay::MacroEdit::begin);
    rig.ui->modulationOverlay().editMacroRange (0, params::osc1Level, 1, 0.9f, osp::plugin::ModulationOverlay::MacroEdit::commit);
    rig.settle();
    CHECK (runner.status (3) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");
    // 5. Turn Macro 1: automation completes nothing; the musician's turn does.
    REQUIRE (runner.stepIndex() == 4);
    automate (p, params::userMacroId (0), 40.0f);
    rig.settle();
    CHECK (runner.state() == LessonRunner::State::waitingForAction);
    rig.turnWatched ("usermacro.0");
    CHECK (runner.status (4) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");
    // 6. Review: Macro 1's name.
    REQUIRE (runner.stepIndex() == 5);
    REQUIRE (rig.ring().has_value());
    rig.learn->noteAction ("finish");
    CHECK (rig.learn->store().read().lessons.at ("analog.usermacros.basics").status == "complete");
    rig.learn->noteAction ("close");
    p.setMacroBank (0);
}

TEST_CASE ("analog learn: Use external audio - LIVE INPUT verified on real incoming audio; no host input, no pretending", "[learn][analog][aux]")
{
    using namespace andor::analog;
    Rig rig;
    auto& p = rig.p;
    rig.ui->setAuxStripShown (false);
    rig.settle();
    REQUIRE (rig.learn->startLesson ("analog.aux.live"));
    auto& runner = rig.runner();
    rig.settle();
    // 1. D hidden: Show AUX (its fix), then Next.
    CHECK (runner.state() == LessonRunner::State::blocked);
    CHECK (rig.keys().contains ("fix"));
    rig.learn->noteAction ("fix");
    rig.settle();
    CHECK (rig.ui->auxStripShown());
    CHECK (runner.state() == LessonRunner::State::tourShowing);
    rig.learn->noteAction ("next");
    // 2. LIVE INPUT: the source dropdown outlined; its choice (after the click) completes it.
    REQUIRE (runner.stepIndex() == 1);
    auto* mode = AuxStripView::part (*rig.ui->auxStripComponent(), "mode");
    REQUIRE (mode != nullptr);
    CHECK (rig.outline() == rig.contentArea (*mode, mode->getLocalBounds()));
    automate (p, params::auxMode, 1.0f);
    auto* watcher = rig.learn->touchWatcher ("aux.mode");
    REQUIRE (watcher != nullptr);
    watcher->mouseUp (eventAt (*mode, {}));   // the click opens the menu...
    rig.settle();
    CHECK (runner.state() == LessonRunner::State::waitingForAction);
    automate (p, params::auxMode, 3.0f);      // ...and LIVE INPUT is chosen in it
    rig.settle();
    CHECK (runner.status (1) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");
    // 3. Power.
    REQUIRE (runner.stepIndex() == 2);
    if (valueOf (p, params::auxEnabled) > 0.5f)
        rig.learn->noteAction ("continue");
    else
    {
        auto* power = AuxStripView::part (*rig.ui->auxStripComponent(), "power");
        REQUIRE (power != nullptr);
        CHECK (rig.outline() == rig.contentArea (*power, power->getLocalBounds()));
        power->mouseDown (eventAt (*power, power->getLocalBounds().getCentre().toFloat()));
        power->mouseUp (eventAt (*power, power->getLocalBounds().getCentre().toFloat()));
        rig.learn->touchWatcher ("aux.power")->mouseUp (eventAt (*power, {}));
        rig.settle();
        CHECK (valueOf (p, params::auxEnabled) > 0.5f);
        CHECK (runner.status (2) == LessonRunner::StepStatus::done);
        rig.learn->noteAction ("next");
    }
    // 4. Route (an explanation), 5. Verify: no input from the host - an honest message, no
    // example to pretend with, no way on until audio really arrives.
    REQUIRE (runner.stepIndex() == 3);
    rig.learn->noteAction ("next");
    REQUIRE (runner.stepIndex() == 4);
    feedSidechain (p, false, 0.0f);
    rig.settle();
    CHECK (runner.state() == LessonRunner::State::blocked);
    CHECK (rig.learn->note()->titleText() == "No incoming audio");
    CHECK_FALSE (rig.keys().contains ("example"));
    CHECK_FALSE (rig.keys().contains ("next"));
    feedSidechain (p, true, 0.0f);   // routed, silent
    rig.settle();
    CHECK (runner.state() == LessonRunner::State::blocked);
    CHECK (rig.learn->note()->bodyText().contains ("no audio is arriving"));
    feedSidechain (p, true, 0.5f);   // the track plays
    rig.settle();
    CHECK (runner.state() == LessonRunner::State::tourShowing);
    rig.learn->noteAction ("next");
    // 6. FILTER, turned while the audio plays.
    REQUIRE (runner.stepIndex() == 5);
    feedSidechain (p, true, 0.5f);
    rig.settle();
    CHECK (rig.outline() == rig.contentArea (*rig.ui->macroKnob (2), rig.ui->macroKnob (2)->getLocalBounds()));
    rig.turnWatched ("macro.2");
    CHECK (runner.status (5) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");
    // 7. MORPH TO D: the rack first (its fix opens it), then D's segment.
    REQUIRE (runner.stepIndex() == 6);
    feedSidechain (p, true, 0.5f);
    rig.ui->setRackExpanded (osp::plugin::rack::Kind::morph, false);
    rig.settle();
    REQUIRE (runner.state() == LessonRunner::State::blocked);
    CHECK (rig.keys().contains ("fix"));
    rig.learn->noteAction ("fix");
    feedSidechain (p, true, 0.5f);
    rig.settle();
    CHECK (rig.ui->effectsRack().isShown (osp::plugin::rack::Kind::morph));
    if (juce::roundToInt (valueOf (p, params::morphTarget)) == 1)
        automate (p, params::morphTarget, 0.0f);
    feedSidechain (p, true, 0.5f);
    rig.settle();
    rig.clickSegment ("morph.destination", 1);
    CHECK (runner.status (6) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("finish");
    CHECK (rig.learn->store().read().lessons.at ("analog.aux.live").status == "complete");
    rig.learn->noteAction ("close");
}

TEST_CASE ("analog learn: Explore FX Routing - a specific block, its rack, a reorder, back to the cards", "[learn][analog][fx]")
{
    namespace fx = andor::analog::fx;
    Rig rig;
    REQUIRE (rig.learn->startLesson ("analog.fx.routing"));
    auto& runner = rig.runner();
    rig.settle();
    CHECK (rig.outline() == rig.contentArea (rig.ui->sourceViewToggle(), rig.ui->sourceViewToggle().getLocalBounds()));
    rig.ui->sourceViewToggle().onClick();
    rig.settle();
    CHECK (runner.status (0) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");
    auto* view = dynamic_cast<andor::analog::FxRoutingView*> (rig.ui->sourceViewComponent());
    REQUIRE (view != nullptr);
    CHECK (rig.outline() == rig.contentArea (*view, view->mixerBounds()));
    rig.learn->noteAction ("next");
    // Select: the one block outlined (never the whole canvas).
    REQUIRE (runner.stepIndex() == 2);
    REQUIRE (view->tiles().size() >= 2);
    CHECK (rig.outline() == rig.contentArea (*view, view->tiles().front().bounds));
    auto* board = rig.learn->touchWatcher ("routing.board");
    REQUIRE (board != nullptr);
    // A block whose double-click opens a rack (ECHO / SPACE / MOVEMENT).
    std::optional<fx::InstanceId> racked;
    for (const auto& t : view->tiles())
        if (t.id.type == fx::Type::echo || t.id.type == fx::Type::space || t.id.type == fx::Type::movement)
            racked = t.id;
    REQUIRE (racked.has_value());
    view->select (racked);
    view->showQuick (racked);
    board->mouseUp (eventAt (*view, {}));
    rig.settle();
    CHECK (runner.status (2) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");
    // Its full rack.
    REQUIRE (runner.stepIndex() == 3);
    for (const auto& t : view->tiles())
        if (t.id == *racked)
            CHECK (rig.outline() == rig.contentArea (*view, t.bounds));
    view->openEditor (*racked);
    board->mouseUp (eventAt (*view, {}));
    rig.settle();
    CHECK (runner.status (3) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");
    // A reorder (a drag ends with the block in its new place).
    REQUIRE (runner.stepIndex() == 4);
    rig.ui->setRackExpanded (osp::plugin::rack::Kind::echo, false);
    rig.ui->setRackExpanded (osp::plugin::rack::Kind::space, false);
    rig.ui->setRackExpanded (osp::plugin::rack::Kind::movement, false);
    rig.settle();
    // (CLASSIC -> CUSTOM with the same order is no reorder: the order itself must change.)
    const auto orderBefore = rig.learn->targets().value ("analog.fx.order").value_or (-1.0f);
    bool moved = false;
    for (int index = 0; index < 6 && ! moved; ++index)
    {
        view->moveTo (view->tiles().front().id, {}, index);
        moved = rig.learn->targets().value ("analog.fx.order").value_or (-1.0f) != orderBefore;
    }
    REQUIRE (moved);
    board->mouseUp (eventAt (*view, {}));
    rig.settle();
    INFO ("state " << static_cast<int> (runner.state()) << " unmet " << (runner.unmetRequirement() != nullptr ? runner.unmetRequirement()->id : juce::String ("-"))
                   << " routing shown " << (int) rig.ui->sourceViewShown());
    CHECK (runner.status (4) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");
    // Back to the cards.
    REQUIRE (runner.stepIndex() == 5);
    rig.ui->sourceViewToggle().onClick();
    rig.settle();
    CHECK_FALSE (rig.ui->sourceViewShown());
    CHECK (runner.status (5) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("finish");
    CHECK (rig.learn->store().read().lessons.at ("analog.fx.routing").status == "complete");
    rig.learn->noteAction ("close");
}

TEST_CASE ("analog learn: Quick Start - seven steps, each on its own part of the instrument", "[learn][analog]")
{
    Rig rig;
    rig.ui->setArpExpanded (false);
    rig.ui->setAuxStripShown (false);
    rig.settle();
    REQUIRE (rig.learn->startLesson ("analog.quickstart"));
    auto& runner = rig.runner();
    const juce::StringArray expected { "header.preset", "cards.AB", "card.C.source", "macros.bank", "arp.opener", "arp.companion", "aux.strip" };
    for (int s = 0; s < 7; ++s)
    {
        rig.settle();
        INFO ("step " << s);
        REQUIRE (runner.stepIndex() == s);
        CHECK (runner.step()->targetId == expected[s]);
        CHECK (rig.learn->note()->stepText() == "STEP " + juce::String (s + 1) + " OF 7");
        if (s == 6)
        {
            // D hidden: the note says so and offers Show AUX (it never shows D by itself).
            CHECK (runner.state() == LessonRunner::State::paused);
            CHECK (rig.keys().contains ("showme"));
            rig.learn->noteAction ("showme");
            rig.settle();
        }
        REQUIRE (rig.ring().has_value());
        rig.checkNotePlacement();
        rig.learn->noteAction (s == 6 ? "finish" : "next");
    }
    CHECK (rig.learn->noteKind() == LearnController::NoteKind::complete);
    CHECK (rig.learn->note()->bodyText().contains ("main parts of Analog"));
}

TEST_CASE ("analog learn: Info - regions outline, explain with the rewritten texts, Esc leaves no trace", "[learn][analog]")
{
    Rig rig;
    rig.p.setMacroBank (0);   // (the CORE macros: the bank is an interface preference another test may have left on USER)
    rig.ui->showMacroBank (0);
    rig.settle();
    const auto idle = rig.snapshot();
    const int childrenBefore = rig.ui->contentForTest().getNumChildComponents();
    rig.learn->setLearnMode (true);
    rig.settle();
    REQUIRE (rig.learn->banner() != nullptr);
    juce::StringArray ids;
    for (auto* region : rig.learn->regions())
        if (region->isVisible())
            ids.add (region->targetId());
    INFO ("regions: " << ids.joinIntoString (" "));
    for (const char* id : { "header.preset", "source.card.A", "source.card.B", "source.card.C", "macro.0", "macro.1", "macro.2", "macro.3", "macro.4",
                            "macro.5", "mod.panel", "keyboard", "arp.control", "advanced", "routing.button" })
        CHECK (ids.contains (id));
    rig.learn->inspect ("macro.0");
    rig.settle();
    CHECK (rig.learn->note()->metaText() == "INFO");
    CHECK (rig.learn->note()->titleText() == "MORPH");
    CHECK (rig.learn->note()->bodyText().startsWith ("MORPH combines the synthesizer with OSC C or AUX D."));
    CHECK (rig.keys().contains ("start:analog.morph.basics"));
    rig.learn->inspect ("source.card.A");
    rig.settle();
    CHECK (rig.learn->note()->titleText() == juce::String::fromUTF8 ("OSC 1 \xe2\x80\x94 Analog oscillator"));
    rig.learn->inspect ("arp.companion");
    rig.settle();
    CHECK (rig.learn->note()->titleText() == "Sub-oscillator");
    // Escape: the note, then Info.
    CHECK (rig.learn->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK (rig.learn->note() == nullptr);
    CHECK (rig.learn->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK_FALSE (rig.learn->learnMode());
    rig.settle();
    CHECK (rig.learnChildren() == 0);
    CHECK (rig.ui->contentForTest().getNumChildComponents() == childrenBefore);
    CHECK (samePixels (idle, rig.snapshot()));
}

TEST_CASE ("analog learn: the browser - START HERE and LESSONS, Resume with its step, a check when done, keys; Reset Tutorials asks first", "[learn][analog]")
{
    Rig rig;
    rig.learn->openBrowser();
    rig.settle();
    auto* browser = rig.learn->browser();
    REQUIRE (browser != nullptr);
    REQUIRE (browser->rows().size() == 6);
    CHECK (browser->rows().front().featured);
    CHECK (browser->rows().front().lessonId == "analog.quickstart");
    for (const auto& row : browser->rows())
    {
        CHECK (row.status == andor::learn::Browser::Row::Status::notStarted);
        CHECK (row.description.isNotEmpty());
        CHECK (andor::learn::Browser::statusText (row) == juce::String (row.minutes) + " min");   // (no "Not started" on each row)
    }
    CHECK (browser->getWidth() - 2 * andor::learn::Browser::margin == andor::learn::Browser::cardWidth);
    CHECK (rig.ui->contentForTest().getLocalBounds().contains (browser->getBounds()));
    // The ARP lesson left at its third step: Resume - Step 3 of 9.
    rig.ui->setArpExpanded (true);
    automate (rig.p, "arp.enabled", 1.0f);
    rig.learn->chooseLesson ("analog.arp.basics");
    rig.settle();
    rig.learn->noteAction ("continue");
    rig.learn->noteAction ("continue");
    REQUIRE (rig.runner().stepIndex() == 2);
    rig.learn->noteAction ("close");
    rig.learn->openBrowser();
    browser = rig.learn->browser();
    REQUIRE (browser != nullptr);
    for (const auto& row : browser->rows())
        if (row.lessonId == "analog.arp.basics")
        {
            CHECK (row.status == andor::learn::Browser::Row::Status::inProgress);
            CHECK (andor::learn::Browser::statusText (row) == juce::String::fromUTF8 ("Resume \xc2\xb7 Step 3 of 9"));
        }
    // The keyboard: down, down (the ARP lesson, after the featured Quick Start), Return.
    CHECK (browser->keyPressed (juce::KeyPress (juce::KeyPress::downKey)));
    CHECK (browser->keyPressed (juce::KeyPress (juce::KeyPress::downKey)));
    CHECK (browser->selectedRow() == 1);
    CHECK (browser->keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
    browser->onChoose (juce::String ("analog.arp.basics"));   // (the deferred call: it deletes the browser)
    CHECK (rig.learn->browser() == nullptr);
    CHECK (rig.runner().stepIndex() == 2);   // resumed where it was left
    rig.learn->noteAction ("close");
    // Escape closes the browser.
    rig.learn->openBrowser();
    CHECK (rig.learn->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK (rig.learn->browser() == nullptr);
    // Reset Tutorials: asks first; Cancel changes nothing; Reset clears only lessons.
    const auto before = allValues (rig.p);
    rig.learn->store().update ([] (andor::learn::ProgressData& d) { d.firstInviteSeen = true; });
    rig.learn->confirmResetProgress();
    REQUIRE (rig.learn->noteKind() == LearnController::NoteKind::confirmReset);
    CHECK (rig.learn->note()->titleText() == "Reset tutorial progress?");
    CHECK (rig.keys() == juce::StringArray ({ "cancel", "reset" }));
    rig.learn->noteAction ("cancel");
    CHECK_FALSE (rig.learn->store().read().lessons.empty());
    rig.learn->confirmResetProgress();
    rig.learn->noteAction ("reset");
    CHECK (rig.learn->store().read().lessons.empty());
    CHECK (rig.learn->store().read().firstInviteSeen);   // (nothing starts or asks again)
    CHECK_FALSE (rig.runner().inTour());
    CHECK (allValues (rig.p) == before);
}

TEST_CASE ("analog learn: the invitation - Learn ANDOR, Not Now / Tutorials / Quick Start, once", "[learn][analog]")
{
    Rig rig;
    rig.learn->showInvitation();
    REQUIRE (rig.learn->note() != nullptr);
    CHECK (rig.learn->noteKind() == LearnController::NoteKind::invitation);
    CHECK (rig.learn->note()->titleText() == "Learn ANDOR");
    CHECK (rig.keys() == juce::StringArray ({ "notnow", "browse", "quickstart" }));
    CHECK_FALSE (rig.runner().inTour());   // (no lesson without the musician's choice)
    rig.learn->noteAction ("notnow");
    CHECK (rig.learn->store().read().firstInviteSeen);
}

TEST_CASE ("analog learn: example sounds - only on request; a failed load changes nothing; X never restores", "[learn][analog]")
{
    Rig rig;
    auto& p = rig.p;
    automate (p, params::hybridType, 0.0f);
    rig.settle();
    const auto mine = allValues (p);
    REQUIRE (rig.learn->startLesson ("analog.morph.basics"));
    rig.settle();
    CHECK_FALSE (p.canRestoreLearnSound());
    // An example the product does not have: not loaded, nothing changed, the note says so.
    const auto descriptor = andor::analog::learnDescriptor();
    CHECK (descriptor.exampleState ("analog.example.none", p) == nullptr);
    // The real one, chosen: loaded; X closes at once and keeps it (Restore stays in the menu).
    rig.learn->noteAction ("example");
    rig.settle();
    CHECK (p.canRestoreLearnSound());
    rig.learn->noteAction ("close");
    CHECK (juce::roundToInt (valueOf (p, params::hybridType)) == 1);
    juce::PopupMenu menu;
    rig.learn->addMenuItems (menu);
    CHECK (menuTexts (menu, true).contains ("Restore Previous Sound"));
    CHECK (rig.learn->restorePreviousSound());
    INFO (differences (mine, allValues (p)));
    CHECK (sameValues (allValues (p), mine));
    juce::PopupMenu after;
    rig.learn->addMenuItems (after);
    CHECK_FALSE (menuTexts (after, true).contains ("Restore Previous Sound"));
    // Every example is self-contained: oscillators and noise only.
    for (const char* id : { "analog.example.morph", "analog.example.arp", "analog.example.simple" })
    {
        INFO (id);
        auto xml = descriptor.exampleState (id, p);
        REQUIRE (xml != nullptr);
        CHECK (xml->getNumChildElements() == 1);   // (parameters only: no recording, no Kontakt state)
    }
}

TEST_CASE ("analog learn: the outline and note follow their target at every size", "[learn][analog]")
{
    for (const int width : { 869, 1448, 1810 })
    {
        INFO (width);
        Rig rig (width);
        rig.ui->setArpExpanded (true);
        automate (rig.p, "arp.enabled", 1.0f);
        rig.settle();
        REQUIRE (rig.learn->startLesson ("analog.arp.basics"));
        rig.learn->noteAction ("continue");
        rig.learn->noteAction ("continue");
        rig.settle();
        REQUIRE (rig.runner().stepIndex() == 2);
        const auto ring = rig.ring();
        REQUIRE (ring.has_value());
        const auto expected = rig.contentArea (rig.panel(), (rig.panel().patternFieldArea().getUnion (rig.panel().patternListArea())).getSmallestIntegerContainer());
        CHECK (*ring == expected);
        rig.checkNotePlacement();
        // The overlay passes the pointer through everywhere but its note.
        bool self = true, children = true;
        rig.learn->highlight()->getInterceptsMouseClicks (self, children);
        CHECK_FALSE (self);
        CHECK_FALSE (children);
        CHECK (rig.ui->contentForTest().getComponentAt (ring->getCentre()) == &rig.panel());
        rig.learn->noteAction ("close");
    }
}

TEST_CASE ("analog learn: screenshots of every lesson step, the browser, Info (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-learn]")
{
    const auto* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    for (const int width : { 1448, 869 })
    {
        Rig rig (width);
        const auto suffix = "-" + juce::String (width);
        auto shoot = [&] (const juce::String& name) {
            rig.resize (width);
            rig.settle();
            const auto image = rig.snapshot();
            const auto file = out.getChildFile ("learn-" + name + suffix + ".png");
            file.deleteFile();
            juce::FileOutputStream stream (file);
            juce::PNGImageFormat().writeImageToStream (image, stream);
        };
        shoot ("00-off");
        rig.learn->showInvitation();
        shoot ("01-invitation");
        rig.learn->noteAction ("browse");
        shoot ("02-browser");
        rig.learn->closeBrowser();

        // Each lesson, each step, in a state where the step means something.
        auto everyStep = [&] (const juce::String& lessonId, const juce::String& tag, std::function<void (int)> before = {}) {
            const auto* lesson = rig.learn->catalog().findLesson (lessonId);
            REQUIRE (lesson != nullptr);
            for (int s = 0; s < static_cast<int> (lesson->steps.size()); ++s)
            {
                if (before != nullptr)
                    before (s);
                rig.learn->startLesson (lessonId, s);
                shoot (tag + "-" + juce::String (s + 1).paddedLeft ('0', 2) + "-" + lesson->steps[static_cast<std::size_t> (s)].id);
            }
            rig.learn->noteAction ("close");
        };
        rig.ui->setArpExpanded (false);
        rig.ui->setAuxStripShown (true);
        everyStep ("analog.quickstart", "10-quickstart");
        rig.ui->setAuxStripShown (false);
        automate (rig.p, "arp.enabled", 0.0f);
        automate (rig.p, "analog.arp.link", 1.0f);
        everyStep ("analog.arp.basics", "20-arp", [&] (int s) {
            if (s == 8)
            {
                automate (rig.p, "analog.arp.link", 0.0f);
                rig.ui->arpInlinePanel().setEditCard (1);
            }
        });
        rig.ui->setArpExpanded (false);
        // MORPH: the preparation as it first shows (an empty card C), then on its example.
        automate (rig.p, params::hybridType, 0.0f);
        REQUIRE (rig.learn->startLesson ("analog.morph.basics"));
        shoot ("30-morph-00-prepare-empty-c");
        rig.learn->noteAction ("example");
        rig.learn->noteAction ("close");
        rig.ui->setRackExpanded (osp::plugin::rack::Kind::morph, true);
        everyStep ("analog.morph.basics", "30-morph", [&] (int s) {
            if (s >= 8)
                automate (rig.p, params::morphTarget, s == 8 ? 0.0f : 1.0f);
        });
        // Blocked: the sound a step needs is missing (C muted).
        automate (rig.p, params::morphTarget, 0.0f);
        automate (rig.p, "layerB.mute", 1.0f);
        REQUIRE (rig.learn->startLesson ("analog.morph.basics", 5));
        shoot ("31-morph-blocked-c-muted");
        rig.learn->noteAction ("close");
        automate (rig.p, "layerB.mute", 0.0f);
        rig.learn->restorePreviousSound();
        rig.ui->setRackExpanded (osp::plugin::rack::Kind::morph, false);
        rig.p.setMacroBank (0);
        rig.ui->showMacroBank (0);
        everyStep ("analog.usermacros.basics", "40-macro", [&] (int s) {
            if (s >= 1)
            {
                rig.p.setMacroBank (1);
                rig.ui->showMacroBank (1);
            }
        });
        rig.ui->showMacroBank (0);
        rig.p.setMacroBank (0);
        rig.ui->setAuxStripShown (true);
        everyStep ("analog.aux.live", "50-aux", [&] (int s) {
            if (s >= 2)
                automate (rig.p, params::auxMode, 3.0f);
            if (s >= 4)
                feedSidechain (rig.p, true, 0.5f);
            if (s == 6)
                rig.ui->setRackExpanded (osp::plugin::rack::Kind::morph, true);
        });
        rig.ui->setRackExpanded (osp::plugin::rack::Kind::morph, false);
        automate (rig.p, params::auxMode, 1.0f);
        rig.ui->setAuxStripShown (false);
        everyStep ("analog.fx.routing", "60-fx", [&] (int s) { rig.ui->setSourceViewShown (s >= 1 && s <= 4); });
        rig.ui->setSourceViewShown (false);
        // Complete, with an example to keep or restore; Reset Tutorials asks first.
        REQUIRE (rig.learn->startLesson ("analog.morph.basics"));
        rig.learn->noteAction ("example");
        rig.ui->setRackExpanded (osp::plugin::rack::Kind::morph, true);
        automate (rig.p, params::morphTarget, 1.0f);
        rig.learn->startLesson ("analog.morph.basics", 9);
        rig.settle();
        rig.turnWatched ("macro.0");
        rig.learn->noteAction ("finish");
        rig.ui->setRackExpanded (osp::plugin::rack::Kind::morph, false);
        shoot ("70-complete-example");
        rig.learn->noteAction ("restore");
        rig.learn->confirmResetProgress();
        shoot ("71-reset-confirm");
        rig.learn->noteAction ("cancel");
        // Info.
        rig.learn->setLearnMode (true);
        shoot ("80-info");
        rig.learn->regions().front()->onHover ("macro.0");
        shoot ("81-info-hover-macro");
        rig.learn->inspect ("macro.0");
        shoot ("82-info-morph");
        rig.learn->inspect ("aux.strip");
        shoot ("83-info-aux");
        rig.learn->setLearnMode (false);
        // The browser with progress: Quick Start done, the arpeggio under way at its third step.
        rig.learn->resetProgress();
        rig.learn->store().update ([] (andor::learn::ProgressData& d) {
            d.lessons["analog.quickstart"] = { 3, "complete", "2026-11-15T12:00:00Z", {} };
            d.lessons["analog.arp.basics"] = { 2, "in-progress", {}, "pattern" };
        });
        rig.learn->openBrowser();
        shoot ("90-browser-progress");
        rig.learn->closeBrowser();
        shoot ("99-off-after");
    }
}
