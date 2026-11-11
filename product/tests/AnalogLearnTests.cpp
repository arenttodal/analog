// ANDOR Learn in ANDOR Analog (docs/LEARN_V1.md): the ARP lesson end to end with the musician's
// own gestures (and proof that automation, presets and programmatic changes complete nothing),
// MORPH and FX Routing, Learn mode's regions, the browser and resumption, the one-time
// invitation, and that closing Learn leaves no trace (no component, no parameter, no pixel).
#include "AnalogLearn.h"
#include "AnalogParameters.h"
#include "ArpSection.h"
#include "CeramicSkin.h"
#include "EffectRack.h"
#include "FxRouting.h"
#include "ShapingPopups.h"
#include "ShellEditor.h"
#include "ShellProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <map>

using andor::learn::LearnController;
using andor::learn::LessonRunner;
using andor::shell::ShellProcessor;

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

    struct Rig
    {
        LearnDir learnDir;
        ShellProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        andor::shell::ShellEditor* ui = nullptr;
        LearnController* learn = nullptr;

        explicit Rig (int width = 1448)
        {
            editor.reset (p.createEditorIfNeeded());
            ui = dynamic_cast<andor::shell::ShellEditor*> (editor.get());
            REQUIRE (ui != nullptr);
            learn = ui->learnController();
            REQUIRE (learn != nullptr);
            INFO (learn->problems().joinIntoString ("\n"));
            REQUIRE (learn->isUsable());
            resize (width);
        }
        void resize (int width)
        {
            editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui->instrumentHeight() / 1448.0f));
            ui->refreshNow();
        }
        void settle()
        {
            ui->refreshNow();   // (the editor's tick: Learn follows its targets)
            learn->layoutNow();
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
        /** RATE turned one step, as a drag, the wheel or an accessibility client moves it. */
        void turnRate()
        {
            auto& knob = dynamic_cast<osp::plugin::MiniKnob&> (panel().shownRateKnob());
            const double next = knob.slider.getValue() + 1.0 <= knob.slider.getMaximum() ? knob.slider.getValue() + 1.0 : knob.slider.getValue() - 1.0;
            knob.slider.setValue (next, juce::sendNotificationSync);
            settle();
        }
        juce::Rectangle<int> contentArea (juce::Component& c, juce::Rectangle<int> local)
        {
            return ui->contentForTest().getLocalArea (&c, local);
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
} // namespace

TEST_CASE ("analog learn: the catalog is valid, idle Learn adds nothing, the menu has its two items", "[learn][analog]")
{
    Rig rig;
    CHECK (rig.learn->catalog().lessons.size() == 4);
    CHECK (rig.learn->catalog().findLesson ("analog.arp.basics") != nullptr);
    // Never advertised before they ship (C2, SUB, MAIN/SEND, independent REIMAGINED).
    for (const auto& l : rig.learn->catalog().lessons)
        for (const char* absent : { "sub", "c2", "send", "reimagined" })
            CHECK_FALSE (l.id.containsIgnoreCase (absent));
    CHECK (rig.learnChildren() == 0);
    juce::PopupMenu menu;
    rig.learn->addMenuItems (menu);
    juce::StringArray items;
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        items.add (it.getItem().text);
    CHECK (items.contains (juce::String::fromUTF8 ("Learn ANDOR\xe2\x80\xa6")));
    CHECK (items.contains ("Learn mode"));
    CHECK (items.contains ("Learn"));   // its submenu: Reset learning progress (never the audio Reset settings)
    CHECK_FALSE (items.contains ("Reset settings"));
}

TEST_CASE ("analog learn: the ARP lesson, end to end, completed only by the musician's gestures", "[learn][analog]")
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
    auto* note = rig.learn->note();
    REQUIRE (note != nullptr);

    // 1. Open the arpeggiator: the ring is on the real ARP card.
    CHECK (runner.stepIndex() == 0);
    CHECK (runner.state() == LessonRunner::State::waitingForAction);
    REQUIRE (rig.learn->highlight()->target().has_value());
    CHECK (*rig.learn->highlight()->target() == rig.contentArea (rig.ui->arpControl(), rig.ui->arpControl().getLocalBounds()));
    CHECK (rig.learn->note()->actionIds().contains ("showme"));
    // The note never covers its target and stays inside the instrument.
    CHECK_FALSE (rig.learn->note()->getBounds().reduced (andor::learn::Note::margin).intersects (*rig.learn->highlight()->target()));
    CHECK (rig.ui->contentForTest().getLocalBounds().contains (rig.learn->note()->getBounds()));
    // A session recalled with the panel open (no click): nothing completes; Continue is offered.
    rig.ui->setArpExpanded (true);
    rig.settle();
    CHECK (runner.status (0) == LessonRunner::StepStatus::pending);
    CHECK (rig.learn->note()->actionIds().contains ("continue"));
    rig.ui->setArpExpanded (false);
    rig.settle();
    // The musician clicks the card (not its light).
    auto& arp = rig.ui->arpControl();
    arp.mouseDown (eventAt (arp, arp.chevronArea().getCentre()));
    rig.settle();
    CHECK (rig.ui->arpExpanded());
    CHECK (runner.state() == LessonRunner::State::stepComplete);
    CHECK (rig.learn->note()->showsDone());
    CHECK (runner.stepIndex() == 0);   // only the musician moves on
    rig.learn->noteAction ("next");

    // 2. Switch it on: automation does not count; the light does.
    REQUIRE (runner.stepIndex() == 1);
    automate (p, "arp.enabled", 1.0f);
    rig.settle();
    CHECK (runner.status (1) == LessonRunner::StepStatus::pending);
    automate (p, "arp.enabled", 0.0f);
    rig.settle();
    arp.mouseDown (eventAt (arp, arp.lightArea().getCentre()));
    rig.settle();
    CHECK (juce::exactlyEqual (valueOf (p, "arp.enabled"), 1.0f));
    CHECK (runner.status (1) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");

    // 3. Change the pattern: automation, a preset and the programmatic call complete nothing.
    REQUIRE (runner.stepIndex() == 2);
    CHECK (rig.panel().editedCard() == 0);
    const float pattern = valueOf (p, "arp.pattern");
    automate (p, "arp.pattern", pattern < 1.0f ? 2.0f : 0.0f);
    rig.settle();
    CHECK (runner.state() == LessonRunner::State::waitingForAction);
    {
        // A preset recalled with another pattern.
        juce::MemoryBlock state;
        automate (p, "arp.pattern", 5.0f);
        p.getStateInformation (state);
        automate (p, "arp.pattern", pattern);
        p.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        rig.settle();
        CHECK (runner.state() == LessonRunner::State::waitingForAction);
        REQUIRE (rig.learn->note() != nullptr);
    }
    rig.panel().choosePattern (7);
    rig.settle();
    CHECK (runner.state() == LessonRunner::State::waitingForAction);
    rig.clickPanel (rig.otherPatternRow (juce::roundToInt (pattern)));
    CHECK (runner.status (2) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");

    // 4. Another rate.
    REQUIRE (runner.stepIndex() == 3);
    const float rate = valueOf (p, "arp.rate");
    automate (p, "arp.rate", rate + 1.0f);
    rig.settle();
    CHECK (runner.state() == LessonRunner::State::waitingForAction);
    automate (p, "arp.rate", rate);
    rig.settle();
    rig.turnRate();
    CHECK (runner.status (3) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");

    // 5. A off, B on, verified as a whole.
    REQUIRE (runner.stepIndex() == 4);
    rig.clickPanel (rig.panel().cardChipArea (1));   // B off
    CHECK (runner.state() == LessonRunner::State::waitingForAction);
    rig.clickPanel (rig.panel().cardChipArea (1));   // B on again
    CHECK (runner.state() == LessonRunner::State::waitingForAction);
    rig.clickPanel (rig.panel().cardChipArea (0));   // A off
    CHECK (juce::exactlyEqual (valueOf (p, "analog.arp.cardA"), 0.0f));
    CHECK (juce::exactlyEqual (valueOf (p, "analog.arp.cardB"), 1.0f));
    CHECK (runner.status (4) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");

    // 6. LINK off.
    REQUIRE (runner.stepIndex() == 5);
    rig.clickPanel (rig.panel().linkArea());
    CHECK (juce::exactlyEqual (valueOf (p, "analog.arp.link"), 0.0f));
    CHECK (runner.status (5) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");

    // 7. B's own pattern or rate: the step shows EDIT B (a view setting).
    REQUIRE (runner.stepIndex() == 6);
    const int bPatternAtEntry = juce::roundToInt (valueOf (p, "analog.arp.cardB.pattern"));
    CHECK (rig.panel().editedCard() == 1);
    CHECK (runner.state() == LessonRunner::State::waitingForAction);
    // Back on EDIT A, the step waits for B again (and says how).
    rig.clickPanel (rig.panel().editTabArea (0));
    CHECK (runner.state() == LessonRunner::State::paused);
    CHECK (rig.learn->note()->bodyText().contains ("EDIT B"));
    // A's pattern changes; B's does not: nothing completes.
    rig.clickPanel (rig.otherPatternRow());
    CHECK (runner.status (6) == LessonRunner::StepStatus::pending);
    rig.learn->noteAction ("showme");
    rig.settle();
    CHECK (rig.panel().editedCard() == 1);
    CHECK (runner.state() == LessonRunner::State::waitingForAction);
    automate (p, "analog.arp.cardB.pattern", bPatternAtEntry < 1 ? 3.0f : 0.0f);   // automation
    rig.settle();
    CHECK (runner.state() == LessonRunner::State::waitingForAction);
    const float globalPattern = valueOf (p, "arp.pattern");
    rig.clickPanel (rig.otherPatternRow (bPatternAtEntry));
    CHECK (runner.status (6) == LessonRunner::StepStatus::done);
    CHECK (juce::exactlyEqual (valueOf (p, "arp.pattern"), globalPattern));   // B's own, not the global pattern
    rig.learn->noteAction ("next");

    // 8. Finish: progress saved, the sound left exactly as the musician set it.
    REQUIRE (runner.stepIndex() == 7);
    CHECK (rig.learn->note()->actionIds().contains ("finish"));
    rig.learn->noteAction ("finish");
    CHECK (rig.learn->noteKind() == LearnController::NoteKind::complete);
    const auto progress = rig.learn->store().read();
    REQUIRE (progress.lessons.count ("analog.arp.basics") == 1);
    CHECK (progress.lessons.at ("analog.arp.basics").status == "complete");
    CHECK (progress.lessons.at ("analog.arp.basics").revision == 1);
    CHECK (juce::exactlyEqual (valueOf (p, "arp.enabled"), 1.0f));
    CHECK (juce::exactlyEqual (valueOf (p, "analog.arp.link"), 0.0f));
    rig.learn->noteAction ("close");
    CHECK (rig.learnChildren() == 0);
    // The file is the musician's preference only: no samples, MIDI, files or project names.
    const auto json = rig.learn->store().file().loadFileAsString();
    CHECK_FALSE (json.containsIgnoreCase (".wav"));
    CHECK_FALSE (json.containsIgnoreCase ("midi"));
}

TEST_CASE ("analog learn: navigating a lesson (Show me, Skip, Back, Close) never changes the sound", "[learn][analog]")
{
    Rig rig;
    const auto before = allValues (rig.p);
    for (const auto& lesson : rig.learn->catalog().lessons)
    {
        REQUIRE (rig.learn->startLesson (lesson.id));
        for (int guard = 0; guard < 20 && rig.runner().inTour(); ++guard)
        {
            rig.learn->noteAction ("showme");
            rig.settle();
            if (rig.runner().stepIndex() > 0 && guard % 3 == 1)
                rig.learn->noteAction ("back");
            else if (rig.runner().state() == LessonRunner::State::tourShowing)
                rig.learn->noteAction (rig.runner().isLastStep() ? "finish" : "next");
            else
                rig.learn->noteAction ("skip");
            rig.settle();
        }
        rig.learn->noteAction ("close");
        rig.settle();
    }
    CHECK (allValues (rig.p) == before);
    CHECK_FALSE (rig.p.getParameters().size() != static_cast<int> (before.size()));   // no parameter added
}

TEST_CASE ("analog learn: MORPH and FX Routing lessons", "[learn][analog]")
{
    namespace fx = andor::analog::fx;
    Rig rig;
    auto& p = rig.p;
    // MORPH: the rack opened by a double-click on the name; the knob moved by the musician.
    REQUIRE (rig.learn->startLesson ("analog.morph.basics"));
    auto& runner = rig.runner();
    auto* label = rig.ui->macroLabel (0);
    REQUIRE (label != nullptr);
    label->mouseDoubleClick (eventAt (*label, label->getLocalBounds().getCentre().toFloat(), 2));
    rig.settle();
    CHECK (rig.ui->effectsRack().isShown (osp::plugin::rack::Kind::morph));
    CHECK (runner.status (0) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");
    REQUIRE (runner.stepIndex() == 1);
    // Card C empty (a SAMPLE with no recording): the note says so instead of loading anything.
    if (! p.isLayerOccupied (ShellProcessor::hybridLayer) && valueOf (p, andor::analog::params::hybridType) < 0.5f)
        CHECK (rig.learn->note()->bodyText().contains ("nothing to play"));
    rig.learn->noteAction ("next");
    REQUIRE (runner.stepIndex() == 2);
    auto* knob = rig.ui->macroKnob (0);
    auto* watcher = rig.learn->touchWatcher ("macro.0");
    REQUIRE (watcher != nullptr);
    CHECK (watcher->watched() == knob);
    automate (p, andor::analog::params::morphPosition, valueOf (p, andor::analog::params::morphPosition) + 20.0f);   // automation
    rig.settle();
    CHECK (runner.state() == LessonRunner::State::waitingForAction);
    watcher->mouseUp (eventAt (*knob, {}));   // a touch that changed nothing
    CHECK (runner.state() == LessonRunner::State::waitingForAction);
    watcher->mouseDown (eventAt (*knob, {}));                                // the musician presses the knob,
    knob->setValue (knob->getValue() + 10.0, juce::sendNotificationSync);   // drags it (the editor ticks meanwhile)
    rig.settle();
    CHECK (runner.state() == LessonRunner::State::waitingForAction);
    watcher->mouseUp (eventAt (*knob, {}));                                  // and lets go
    CHECK (runner.status (2) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");
    rig.learn->noteAction ("finish");
    CHECK (rig.learn->store().read().lessons.at ("analog.morph.basics").status == "complete");
    rig.learn->noteAction ("close");

    // FX Routing: the header's icon, then a tile's quick settings opened by a click.
    REQUIRE (rig.learn->startLesson ("analog.fx.routing"));
    rig.settle();
    rig.ui->sourceViewToggle().onClick();   // the icon's own click handler
    rig.settle();
    CHECK (rig.ui->sourceViewShown());
    CHECK (runner.status (0) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("next");
    REQUIRE (runner.stepIndex() == 1);
    REQUIRE (rig.learn->highlight()->target().has_value());
    auto* view = dynamic_cast<andor::analog::FxRoutingView*> (rig.ui->sourceViewComponent());
    REQUIRE (view != nullptr);
    CHECK (*rig.learn->highlight()->target() == rig.contentArea (*view, view->mixerBounds()));
    rig.learn->noteAction ("next");
    REQUIRE (runner.stepIndex() == 2);
    REQUIRE_FALSE (view->tiles().empty());
    view->showQuick (view->tiles().front().id);   // opened by code (no touch): nothing completes
    rig.settle();
    CHECK (runner.status (2) == LessonRunner::StepStatus::pending);
    view->showQuick (std::nullopt);
    auto* boardWatcher = rig.learn->touchWatcher ("routing.board");
    REQUIRE (boardWatcher != nullptr);
    rig.settle();
    CHECK (boardWatcher->watched() == view);
    view->showQuick (view->tiles().front().id);   // what the tile's click does...
    boardWatcher->mouseUp (eventAt (*view, view->tiles().front().bounds.getCentre().toFloat()));   // ...on release
    CHECK (runner.status (2) == LessonRunner::StepStatus::done);
    rig.learn->noteAction ("close");
    // Closed mid-lesson: in progress, resumable at the step left.
    const auto saved = rig.learn->store().read().lessons.at ("analog.fx.routing");
    CHECK (saved.status == "in-progress");
    CHECK (saved.lastStep == "tile");
}

TEST_CASE ("analog learn: Learn mode outlines regions, explains a click, Esc leaves no trace", "[learn][analog]")
{
    Rig rig;
    const auto idle = rig.snapshot();
    const int childrenBefore = rig.ui->contentForTest().getNumChildComponents();
    rig.learn->setLearnMode (true);
    rig.settle();
    CHECK (rig.learn->banner() != nullptr);
    juce::StringArray ids;
    const auto content = rig.ui->contentForTest().getLocalBounds();
    for (auto* region : rig.learn->regions())
        if (region->isVisible())
        {
            ids.add (region->targetId());
            // Region-specific, never a full-window blocker.
            CHECK (region->getWidth() * region->getHeight() < content.getWidth() * content.getHeight() / 3);
        }
    for (const char* id : { "header.preset", "source.card.A", "source.card.B", "source.card.C", "macro.0", "macro.1", "macro.2", "macro.3", "macro.4",
                            "macro.5", "mod.panel", "keyboard", "arp.control", "advanced", "routing.button" })
        CHECK (ids.contains (id));
    // Hover outlines quietly; a click explains, with a real lesson to start.
    rig.learn->regions().front()->onHover ("macro.0");
    REQUIRE (rig.learn->highlight()->target().has_value());
    rig.learn->inspect ("macro.0");
    rig.settle();
    REQUIRE (rig.learn->note() != nullptr);
    CHECK (rig.learn->noteKind() == LearnController::NoteKind::help);
    CHECK (rig.learn->note()->titleText() == "MORPH");
    CHECK (rig.learn->note()->actionIds().contains ("start:analog.morph.basics"));
    // A region without a lesson shows no pretend Start button.
    rig.learn->inspect ("macro.1");
    rig.settle();
    for (const auto& id : rig.learn->note()->actionIds())
        CHECK_FALSE (id.startsWith ("start:"));
    // The routing board, inspected while FX ROUTING shows.
    rig.ui->setSourceViewShown (true);
    rig.settle();
    rig.learn->inspect ("routing.board");
    rig.settle();
    CHECK (rig.learn->note()->titleText() == "Effect chain");
    rig.ui->setSourceViewShown (false);
    // Escape: the note first, then Learn mode.
    CHECK (rig.learn->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK (rig.learn->note() == nullptr);
    CHECK (rig.learn->learnMode());
    CHECK (rig.learn->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK_FALSE (rig.learn->learnMode());
    CHECK_FALSE (rig.learn->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));   // the editor's popover next
    rig.settle();
    CHECK (rig.learnChildren() == 0);
    CHECK (rig.ui->contentForTest().getNumChildComponents() == childrenBefore + (rig.ui->sourceViewComponent() != nullptr ? 1 : 0));
    // The routing view stays created (as after any visit); hide it and compare the pixels.
    CHECK (samePixels (idle, rig.snapshot()));
}

TEST_CASE ("analog learn: the browser lists real lessons and resumes; the invitation appears once", "[learn][analog]")
{
    Rig rig;
    rig.learn->openBrowser();
    rig.settle();
    auto* browser = rig.learn->browser();
    REQUIRE (browser != nullptr);
    REQUIRE (browser->rows().size() == 4);
    for (const auto& row : browser->rows())
        CHECK (row.status == andor::learn::Browser::Row::Status::notStarted);
    // Start the ARP lesson from the browser, leave at step 3, come back to it.
    REQUIRE (browser->onChoose != nullptr);
    rig.learn->chooseLesson ("analog.arp.basics");
    rig.settle();
    REQUIRE (rig.runner().inTour());
    rig.learn->noteAction ("skip");
    rig.learn->noteAction ("skip");
    REQUIRE (rig.runner().stepIndex() == 2);
    rig.learn->noteAction ("close");
    rig.learn->openBrowser();
    browser = rig.learn->browser();
    REQUIRE (browser != nullptr);
    for (const auto& row : browser->rows())
        if (row.lessonId == "analog.arp.basics")
            CHECK (row.status == andor::learn::Browser::Row::Status::inProgress);
    // Chosen with the keyboard (the browser deletes itself as it starts the lesson).
    CHECK (browser->keyPressed (juce::KeyPress (juce::KeyPress::downKey)));
    CHECK (browser->keyPressed (juce::KeyPress (juce::KeyPress::downKey)));   // the second row: the ARP lesson
    CHECK (browser->keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
    CHECK (rig.learn->browser() == browser);   // still alive: the choice is delivered after the key returns
    REQUIRE (browser->onChoose != nullptr);
    browser->onChoose (juce::String ("analog.arp.basics"));   // ...as the deferred call does (it deletes the browser)
    CHECK (rig.learn->browser() == nullptr);
    REQUIRE (rig.runner().lesson() != nullptr);
    CHECK (rig.runner().lesson()->id == "analog.arp.basics");
    CHECK (rig.runner().stepIndex() == 2);   // resumed where it was left
    rig.learn->noteAction ("close");
    // Reset learning progress forgets it (and nothing of the sound).
    const auto before = allValues (rig.p);
    rig.learn->resetProgress();
    CHECK (rig.learn->store().read().lessons.empty());
    CHECK (allValues (rig.p) == before);

    // The invitation: once per product in a process, remembered once answered.
    rig.learn->showInvitation();
    REQUIRE (rig.learn->note() != nullptr);
    CHECK (rig.learn->noteKind() == LearnController::NoteKind::invitation);
    CHECK (rig.learn->note()->titleText() == "Find your sound.");
    CHECK (rig.learn->note()->actionIds().contains ("quickstart"));
    CHECK (rig.learn->note()->actionIds().contains ("browse"));
    CHECK (rig.learn->note()->actionIds().contains ("notnow"));
    {
        // A second instance does not show it while the first one does.
        ShellProcessor p2;
        std::unique_ptr<juce::AudioProcessorEditor> e2 (p2.createEditorIfNeeded());
        auto* learn2 = dynamic_cast<andor::shell::ShellEditor&> (*e2).learnController();
        REQUIRE (learn2 != nullptr);
        learn2->showInvitation();
        CHECK (learn2->note() == nullptr);
    }
    CHECK_FALSE (rig.learn->store().read().firstInviteSeen);
    rig.learn->noteAction ("notnow");
    CHECK (rig.learn->note() == nullptr);
    CHECK (rig.learn->store().read().firstInviteSeen);
    // A new editor (another DAW instance later) does not offer it again.
    rig.editor.reset();
    std::unique_ptr<juce::AudioProcessorEditor> e3 (rig.p.createEditorIfNeeded());
    auto* learn3 = dynamic_cast<andor::shell::ShellEditor&> (*e3).learnController();
    REQUIRE (learn3 != nullptr);
    for (int i = 0; i < 40; ++i)
        learn3->tick();
    CHECK (learn3->note() == nullptr);
}

TEST_CASE ("analog learn: the overlay follows its target at small and large sizes", "[learn][analog]")
{
    for (const int width : { 869, 1448, 1810 })
    {
        Rig rig (width);
        rig.ui->setArpExpanded (true);
        rig.settle();
        REQUIRE (rig.learn->startLesson ("analog.arp.basics"));
        rig.learn->noteAction ("continue");   // already open
        rig.settle();
        rig.learn->noteAction ("skip");       // to PATTERN
        rig.settle();
        REQUIRE (rig.runner().stepIndex() == 2);
        const auto ring = rig.learn->highlight()->target();
        REQUIRE (ring.has_value());
        const auto expected = rig.contentArea (rig.panel(), (rig.panel().patternFieldArea().getUnion (rig.panel().patternListArea())).getSmallestIntegerContainer());
        CHECK (*ring == expected);
        const auto noteBounds = rig.learn->note()->getBounds();
        CHECK (rig.ui->contentForTest().getLocalBounds().contains (noteBounds));
        CHECK_FALSE (noteBounds.reduced (andor::learn::Note::margin).intersects (*ring));
        // The overlay passes the pointer through everywhere but its note.
        bool self = true, children = true;
        rig.learn->highlight()->getInterceptsMouseClicks (self, children);
        CHECK_FALSE (self);
        CHECK_FALSE (children);
        const auto inPattern = ring->getCentre();
        auto* hit = rig.ui->contentForTest().getComponentAt (inPattern);
        CHECK (hit == &rig.panel());
        rig.learn->noteAction ("close");
    }
}

TEST_CASE ("analog learn: screenshots (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-learn]")
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
        rig.ui->setArpExpanded (false);
        REQUIRE (rig.learn->startLesson ("analog.arp.basics"));
        shoot ("03-arp-open");
        auto& arp = rig.ui->arpControl();
        arp.mouseDown (eventAt (arp, arp.chevronArea().getCentre()));
        shoot ("04-arp-open-done");
        rig.learn->noteAction ("next");
        shoot ("05-arp-enable");
        rig.learn->noteAction ("skip");
        shoot ("06-arp-pattern");
        rig.learn->noteAction ("skip");
        shoot ("07-arp-rate");
        rig.learn->noteAction ("skip");
        shoot ("08-arp-cards");
        rig.clickPanel (rig.panel().cardChipArea (0));
        rig.learn->noteAction ("next");
        shoot ("09-arp-link");
        rig.clickPanel (rig.panel().linkArea());
        rig.learn->noteAction ("next");
        shoot ("10-arp-card-b");
        rig.clickPanel (rig.otherPatternRow());
        shoot ("11-arp-card-b-done");
        rig.learn->noteAction ("next");
        shoot ("12-arp-finish");
        rig.learn->noteAction ("finish");
        shoot ("13-complete");
        rig.learn->noteAction ("close");
        rig.learn->setLearnMode (true);
        shoot ("14-learn-mode");
        rig.learn->regions().front()->onHover ("macro.0");
        shoot ("15-learn-mode-hover-macro");
        rig.learn->inspect ("macro.0");
        shoot ("16-learn-mode-morph-help");
        rig.ui->setSourceViewShown (true);
        rig.learn->inspect ("routing.board");
        shoot ("17-learn-mode-routing");
        rig.learn->setLearnMode (false);
        REQUIRE (rig.learn->startLesson ("analog.fx.routing"));
        rig.learn->noteAction ("continue");
        shoot ("18-fx-mixer");
        rig.learn->noteAction ("next");
        shoot ("19-fx-tile");
        rig.learn->noteAction ("close");
        rig.ui->setSourceViewShown (false);
        REQUIRE (rig.learn->startLesson ("analog.morph.basics"));
        shoot ("20-morph-open");
        rig.learn->noteAction ("showme");
        rig.learn->noteAction ("continue");
        shoot ("21-morph-sources");
        rig.learn->noteAction ("next");
        shoot ("22-morph-move");
        rig.learn->noteAction ("close");
        rig.ui->setArpExpanded (false);
        rig.ui->setRackExpanded (osp::plugin::rack::Kind::morph, false);
        shoot ("23-off-after");
    }
}
