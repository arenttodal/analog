#include "AnalogLearn.h"

#include "AnalogEngine.h"
#include "AnalogParameters.h"
#include "AuxUI.h"
#include "Binding.h"
#include "FxRouting.h"
#include "LearnTargetRegistry.h"
#include "RackControls.h"
#include "ShellEditor.h"
#include "ShellProcessor.h"

namespace andor::analog
{

using namespace andor::learn;

namespace
{
    juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }

    Step explain (const char* id, const char* target, const char* title, const char* body)
    {
        Step s;
        s.id = id;
        s.targetId = target;
        s.title = u8 (title);
        s.body = u8 (body);
        return s;
    }

    Step task (const char* id, const char* target, const char* title, const char* body, Completion completion, const char* done = "")
    {
        auto s = explain (id, target, title, body);
        s.completion = std::move (completion);
        s.doneText = u8 (done);
        return s;
    }

    Requirement need (const char* id, Condition holds, const char* title, const char* body, const char* target = "", const char* fix = "", bool example = true)
    {
        Requirement r;
        r.id = id;
        r.holds = std::move (holds);
        r.title = u8 (title);
        r.body = u8 (body);
        r.targetId = target;
        r.fixLabel = u8 (fix);
        r.offerExample = example;
        return r;
    }

    // The per-card arpeggiator's parameters (the shell's, with Analog's namespace).
    const juce::String cardA = "analog.arp.cardA", cardB = "analog.arp.cardB", link = "analog.arp.link";
    const juce::String cardBPattern = "analog.arp.cardB.pattern", cardBRate = "analog.arp.cardB.rate";
    const juce::String arpOn = "arp.enabled";

    //==========================================================================================
    // What a lesson needs before its sound can be heard (checked on the real state).

    Requirement oscillatorsHeard()
    {
        return need ("ab.audible", Condition::state ("analog.ab.audible"), "The oscillators are silent",
                     "OSC 1 and OSC 2 are both off, muted or turned all the way down. Raise OSC 1's LEVEL, or load the example sound.",
                     "osc.A.level");
    }

    std::vector<Requirement> hybridReady()
    {
        return {
            need ("c.heard", Condition::state ("analog.c.heard"), "OSC C is muted",
                  "OSC C is muted, or another card is soloed. Click M on OSC C (or the other card's S) to hear it.", "card.C.mute"),
            need ("c.level", Condition::state ("analog.c.levelUp"), "OSC C is silent", "OSC C's LEVEL is all the way down. Turn it up to hear OSC C.",
                  "card.C.level"),
            need ("c.loaded", Condition::state ("analog.c.playable"), "A sound is needed",
                  "OSC C has nothing loaded to play. Choose OSCILLATOR or NOISE from its source dropdown, load a sound into it, or load the example sound.",
                  "card.C.source", "Choose Source"),
        };
    }

    /** AUX D hidden: the D key beneath ARP and SUB shows it (and only shows it: D's power,
        sound and routing stay as they are; what is wrong with them is the next requirement's).
        `whenSilent`: needed only while D also has something to fix (a lesson that merely
        listens to a ready D never asks for the strip). */
    Requirement auxShown (bool whenSilent = false)
    {
        auto holds = Condition::state ("analog.aux.shown");
        if (whenSilent)
            holds = Condition::any ({ holds, Condition::all ({ Condition::equals (params::auxEnabled, 1.0f), Condition::state ("analog.d.heard"),
                                                                Condition::state ("analog.d.levelUp"), Condition::state ("analog.d.playable") }) });
        return need ("aux.shown", std::move (holds), "Show AUX D",
                     "Click D below ARP and SUB to show the AUX controls. You can then check which sound D is using.", "keyboard.auxToggle",
                     "Show AUX D", false);
    }

    std::vector<Requirement> auxReady()
    {
        // In order: the strip first (D), then what actually keeps D silent, each on its own control.
        return {
            auxShown (true),
            need ("d.on", Condition::equals (params::auxEnabled, 1.0f), "AUX D is off", "Switch AUX D on with its power key to hear it.", "aux.power"),
            need ("d.heard", Condition::state ("analog.d.heard"), "AUX D is muted",
                  "AUX D is muted, or a card is soloed. Click M on AUX D (or the other card's S) to hear it.", "aux.mute"),
            need ("d.level", Condition::state ("analog.d.levelUp"), "AUX D is silent", "AUX D's LEVEL is all the way down. Turn it up to hear D.", "aux.level"),
            need ("d.playable", Condition::state ("analog.d.playable"), "A sound is needed",
                  "AUX D has nothing to play: no RHYTHM loop, no Kontakt instrument loaded, or no audio arriving at LIVE INPUT. Choose NOISE, load a source, or load the example sound.",
                  "aux.mode", "Choose Source"),
        };
    }

    Requirement morphRackOpen()
    {
        return need ("morph.rack", Condition::state ("rack.morph.open"), "Open the MORPH rack",
                     "The control for this step is inside the MORPH rack. Open the rack to continue.", "morph.label", "Open Rack", false);
    }

    Requirement morphToC()
    {
        return need ("morph.toC", Condition::equals (params::morphTarget, 0.0f), u8 ("Choose C \xc2\xb7 HYBRID first").toRawUTF8(),
                     u8 ("This control belongs to OSC C's MORPH. Choose C \xc2\xb7 HYBRID under MORPH TO first.").toRawUTF8(), "morph.destination.C", "Open Rack",
                     false);
    }

    template <typename... More>
    std::vector<Requirement> join (std::vector<Requirement> a, More... more)
    {
        (a.insert (a.end(), more.begin(), more.end()), ...);
        return a;
    }

    //==========================================================================================
    // The lessons (docs/LEARN_2.md): manual-style, one action per step, the exact control.

    Lesson quickStart()
    {
        Lesson l;
        l.id = "analog.quickstart";
        l.title = "Quick Start";
        l.description = "The main parts of ANDOR Analog";
        l.group = "Quick Start";
        l.revision = 5;   // (4: AUX D shown with the keyboard's D key, then the strip itself; 5: ends on INFO)
        l.minutes = 2;
        l.completionText = u8 ("You\xe2\x80\x99ve seen the main parts of Analog. Choose another lesson to try a feature in more detail.");
        // The D key beneath ARP and SUB: the musician's own click shows AUX D (already showing:
        // said so, Continue). Then the strip it showed.
        auto aux = task ("aux", "keyboard.auxToggle", "Show AUX D",
                         "Click D beneath ARP and SUB to show AUX D below the oscillator cards. AUX D can play rhythmic loops, noise, Kontakt instruments or audio from another DAW track.",
                         Completion::action ({ "aux.visibility.toggled" }, Condition::state ("analog.aux.shown"))
                             .withAlreadyDone (Condition::state ("analog.aux.shown"), "AUX D is already visible."),
                         "AUX D is showing below the oscillator cards.");
        aux.revealOnEnter = false;   // (showing D changes the window: only its key does)
        auto strip = explain ("strip", "aux.strip", "AUX D",
                              "Choose D's sound from the source dropdown in its display. Its power key, M and LEVEL work like the other cards'. Click D again to hide the strip; D keeps its sound.");
        strip.requirements = { auxShown() };
        l.steps = {
            explain ("sounds", "header.preset", "Select a sound",
                     "Click the preset name to browse sounds. Use the arrows on either side to move through them. SAVE stores your changes as a user preset."),
            explain ("cards", "cards.AB", "Oscillators A and B",
                     "OSC 1 and OSC 2 generate the main pitched sound. Each has its own tuning, octave and volume controls."),
            explain ("hybrid", "card.C.source", "The hybrid oscillator",
                     "OSC C can play another oscillator, a recording, noise or an instrument patch. Select its source type from the dropdown in the dark display."),
            explain ("macros", "macros.bank", "CORE and USER controls",
                     "The CORE knobs control the main sound: MORPH, DRIVE, FILTER, MOVEMENT, ECHO and SPACE. USER contains six macros you can assign yourself."),
            explain ("arp", "arp.opener", "Arpeggiator",
                     "ARP plays the notes you hold as a repeating sequence. Click ARP beside the keyboard to open its controls."),
            explain ("sub", "arp.companion", "Sub-oscillator",
                     "SUB adds lower notes beneath the main sound. Open SUB beside the keyboard to adjust its pitch and level."),
            aux,
            strip,
            findLearnAgainStep(),   // (the INFO key, and the menu's Learn: every ANDOR instrument's)
        };
        return l;
    }

    Lesson arpLesson()
    {
        Lesson l;
        l.id = "analog.arp.basics";
        l.title = "Make an arpeggio";
        l.description = "Turn held notes into a repeating pattern";
        l.group = "Arpeggiator";
        l.capability = "analog.arp.cardRouting";
        l.revision = 2;   // (2: Learn 2.0 - PLAYS A and B as two steps, EDIT B, exact targets)
        l.minutes = 3;
        l.exampleId = "analog.example.arp";
        l.completionText = "OSC 1 is sustaining the chord while OSC 2 plays its own arpeggio. You can use the same controls to give OSC C a different pattern.";
        auto open = task ("open", "arp.opener", "Open the arpeggiator", "Click ARP beside the keyboard to open the arpeggiator controls.",
                          Completion::action ({ "arp.panel.opened" }, Condition::state ("arp.panel.open"))
                              .withAlreadyDone (Condition::state ("arp.panel.open"), "The arpeggiator is already open."),
                          "The arpeggiator controls are open.");
        open.revealOnEnter = false;   // (opening it is the task)
        auto enable = task ("enable", "arp.on", "Turn on ARP",
                            "Switch ARP on, then hold a chord on your MIDI keyboard. The notes will play one after another instead of sounding together.",
                            Completion::action ({ "arp.enabled.changed" }, Condition::equals (arpOn, 1.0f))
                                .withAlreadyDone (Condition::equals (arpOn, 1.0f), "ARP is already on."),
                            "ARP is on. Hold a chord to hear the pattern.");
        enable.requirements = { oscillatorsHeard() };
        auto pattern = task ("pattern", "arp.pattern", "Select a pattern",
                             "PATTERN sets the order of the notes. Choose UP to play from the lowest note to the highest, or select another pattern to hear the difference.",
                             Completion::action ({ "arp.pattern.changed" }, Condition::always()), "The pattern is set.");
        auto rate = task ("rate", "arp.rate", "Change the rate", "RATE sets how quickly the notes repeat. Try 1/8 and 1/16 to hear the difference.",
                          Completion::action ({ "arp.rate.changed" }, Condition::changed ("arp.rate")), "The notes now repeat at the new rate.");
        pattern.unavailableBody = rate.unavailableBody = "The arpeggiator controls are closed. Open them to continue.";
        pattern.revealLabel = rate.revealLabel = "Open ARP";
        auto playsA = task ("plays-a", "arp.plays.A", "Keep OSC 1 sustained",
                            "Under PLAYS, turn off A. OSC 1 will now hold the notes you play instead of following the arpeggiator.",
                            Completion::action ({ "arp.card.toggled" }, Condition::equals (cardA, 0.0f))
                                .withAlreadyDone (Condition::equals (cardA, 0.0f), "A is already off: OSC 1 holds the notes."),
                            "OSC 1 now plays sustained notes.");
        auto playsB = task ("plays-b", "arp.plays.B", "Arpeggiate OSC 2",
                            "Turn on B under PLAYS. OSC 2 will follow the arpeggio while OSC 1 continues to sustain the chord.",
                            Completion::action ({ "arp.card.toggled" }, Condition::equals (cardB, 1.0f))
                                .withAlreadyDone (Condition::equals (cardB, 1.0f), "B is already on: OSC 2 follows the arpeggio."),
                            "OSC 2 now follows the arpeggio.");
        auto unlink = task ("unlink", "arp.link", "Give each oscillator its own pattern",
                            "Turn LINK off. You can now set a different pattern, rate and octave range for each oscillator.",
                            Completion::action ({ "arp.link.changed" }, Condition::equals (link, 0.0f))
                                .withAlreadyDone (Condition::equals (link, 0.0f), "LINK is already off."),
                            "Each oscillator can now have its own pattern.");
        auto linkOff = need ("link.off", Condition::equals (link, 0.0f), "Turn LINK off first",
                             "EDIT appears once LINK is off. Turn LINK off to give OSC 2 settings of its own.", "arp.link", "", false);
        auto editB = task ("edit-b", "arp.edit.B", "Edit OSC 2", "Under EDIT, select B. The pattern and timing controls now belong to OSC 2.",
                           Completion::action ({ "arp.edit.selected" }, Condition::state ("arp.edit.B"))
                               .withAlreadyDone (Condition::state ("arp.edit.B"), "B is already selected under EDIT."),
                           "The pattern and timing controls now belong to OSC 2.");
        editB.requirements = { linkOff };
        auto patternB = task ("pattern-b", "arp.card.B.pattern", "Change OSC 2's pattern",
                              "Choose another PATTERN or turn RATE. Only OSC 2's arpeggio will change; OSC 1 will continue holding the chord.",
                              Completion::action ({ "arp.pattern.changed", "arp.rate.changed" },
                                                  Condition::any ({ Condition::changed (cardBPattern), Condition::changed (cardBRate) })),
                              "OSC 2 now plays its own arpeggio.");
        patternB.keepClear = { "arp.card.B.rate" };
        patternB.requirements = { linkOff, need ("edit.b", Condition::state ("arp.edit.B"), "Select B under EDIT",
                                                 "These controls belong to OSC 2 while B is selected under EDIT.", "arp.edit.B", "", false) };
        l.steps = { open, enable, pattern, rate, playsA, playsB, unlink, editB, patternB };
        return l;
    }

    Lesson morphLesson()
    {
        Lesson l;
        l.id = "analog.morph.basics";
        l.title = "Understand MORPH";
        l.description = "Blend oscillators with recorded and live sounds";
        l.group = "MORPH";
        l.capability = "analog.morph";
        l.revision = 3;   // (3: Learn 2.0 - prepared sound, ARP off, MORPH TO C and D, TEXTURE, METHOD)
        l.minutes = 4;
        l.exampleId = "analog.example.morph";
        l.completionText = "MORPH can combine the analog oscillators with OSC C or AUX D. Use MORPH TO to choose the destination, then adjust the main knob to control the blend.";
        auto prepare = explain ("prepare", "", "Choose a playable sound",
                                "MORPH needs two sounds to work with. Use your current patch if its sources are ready, or load the example sound.");
        prepare.preparation = true;
        prepare.requirements = join (std::vector<Requirement> { oscillatorsHeard() }, hybridReady(), auxReady());
        auto arpOff = task ("arp-off", "arp.on", "Turn off the arpeggiator",
                            "Turn ARP off for this lesson. A sustained chord makes changes in the sound easier to hear.",
                            Completion::action ({ "arp.enabled.changed" }, Condition::equals (arpOn, 0.0f))
                                .withAlreadyDone (Condition::equals (arpOn, 0.0f), "ARP is already off."),
                            "ARP is off. Hold a chord to hear the sound sustain.");
        auto open = task ("open", "morph.label", "Open the MORPH rack", "Double-click MORPH above the main knob to open its detailed controls.",
                          Completion::action ({ "rack.toggled" }, Condition::state ("rack.morph.open"))
                              .withAlreadyDone (Condition::state ("rack.morph.open"), "The MORPH rack is already open."),
                          "The MORPH rack is open.");
        open.revealOnEnter = false;   // (opening it is the task)
        auto enable = task ("enable", "morph.enable", "Enable MORPH", "The small switch beside MORPH turns the effect on. Make sure it is enabled.",
                            Completion::action ({ "morph.enable.touched" }, Condition::equals (params::morphEnabled, 1.0f))
                                .withAlreadyDone (Condition::equals (params::morphEnabled, 1.0f), "MORPH is already enabled."),
                            "MORPH is on.");
        auto toC = task ("to-c", "morph.destination.C", "Select OSC C",
                         u8 ("MORPH TO selects the sound you blend into. Choose C \xc2\xb7 HYBRID to use OSC C.").toRawUTF8(),
                         Completion::action ({ "morph.destination.touched" }, Condition::equals (params::morphTarget, 0.0f))
                             .withAlreadyDone (Condition::equals (params::morphTarget, 0.0f), u8 ("C \xc2\xb7 HYBRID is already selected.")),
                         "MORPH now blends toward OSC C.");
        toC.requirements = join (std::vector<Requirement> { morphRackOpen() }, hybridReady());
        toC.revealLabel = "Open Rack";
        auto hearC = task ("hear-c", "morph.amount", "Hear the two sources",
                           "At 0 %, MORPH favours the analog oscillators. At 100 %, it reaches OSC C. Play a sustained chord and turn the MORPH knob slowly between them.",
                           Completion::action ({ "macro.0.touched" }, Condition::changed (params::morphPosition)),
                           "You moved the sound between the oscillators and OSC C.");
        hearC.requirements = join (std::vector<Requirement> { oscillatorsHeard() }, hybridReady(),
                                   std::vector<Requirement> {
                                       need ("morph.on", Condition::equals (params::morphEnabled, 1.0f), "MORPH is off",
                                             "Switch MORPH on with the small switch beside its name to hear the blend.", "morph.enable", "", false),
                                       need ("toC.selected", Condition::equals (params::morphTarget, 0.0f), u8 ("Choose C \xc2\xb7 HYBRID").toRawUTF8(),
                                             u8 ("Choose C \xc2\xb7 HYBRID under MORPH TO to blend toward OSC C.").toRawUTF8(), "morph.destination.C", "Open Rack",
                                             false) });
        auto texture = task ("texture", "morph.texture", "Adjust the texture",
                             "TEXTURE controls how much of the source's noise and fine detail contributes to the blend. Turn it while listening to the sound.",
                             Completion::action ({ "morph.texture.touched" }, Condition::changed (params::morphTexture)),
                             "TEXTURE changed how much fine detail comes through.");
        texture.requirements = { morphRackOpen(), morphToC() };
        texture.revealLabel = "Open Rack";
        auto method = task ("method", "morph.method", "Understand the methods",
                            "IMPRINT transfers spectral character, FUSE combines spectral information, and WEAVE distributes different frequency regions between sources. Try each method to hear how they differ.",
                            Completion::action ({ "morph.method.touched" }, Condition::changed (params::morphMethod)), "You changed the method.");
        method.requirements = { morphRackOpen(), morphToC() };
        method.revealLabel = "Open Rack";
        auto toD = task ("to-d", "morph.destination.D", "Select AUX D",
                         "MORPH can also use AUX D. Choose D under MORPH TO (it names D's source, such as D \xc2\xb7 NOISE) to blend the synth with the sound coming from D.",
                         Completion::action ({ "morph.destination.touched" }, Condition::equals (params::morphTarget, 1.0f))
                             .withAlreadyDone (Condition::equals (params::morphTarget, 1.0f), "D is already selected."),
                         "MORPH now blends toward AUX D.");
        toD.requirements = join (std::vector<Requirement> { morphRackOpen() }, auxReady());
        toD.revealLabel = "Open Rack";
        auto hearD = task ("hear-d", "morph.amount", "Hear the AUX source",
                           "Hold a chord and move MORPH again. At the D end, you hear the AUX source; between the endpoints, the global MORPH processor combines their spectral characteristics.",
                           Completion::action ({ "macro.0.touched" }, Condition::changed (params::morphPosition)),
                           "You moved the synth toward AUX D.");
        hearD.requirements = join (auxReady(), std::vector<Requirement> {
                                                   need ("toD.selected", Condition::equals (params::morphTarget, 1.0f), "Choose D under MORPH TO",
                                                         "Choose the D segment under MORPH TO to blend toward AUX D.", "morph.destination.D", "Open Rack", false) });
        l.steps = { prepare, arpOff, open, enable, toC, hearC, texture, method, toD, hearD };
        return l;
    }

    Lesson userMacroLesson()
    {
        Lesson l;
        l.id = "analog.usermacros.basics";
        l.title = "Create a macro";
        l.description = "Control several settings with one knob";
        l.group = "Macros";
        l.capability = "analog.userMacros";
        l.revision = 2;   // (2: Learn 2.0 - OSC 1 LEVEL, Macro 1, its range, verified)
        l.minutes = 3;
        l.exampleId = "analog.example.simple";
        l.completionText = "Macro 1 now controls OSC 1 LEVEL. You can assign several parameters to the same macro, each with its own range.";
        auto user = task ("user", "macros.bank", "Open USER macros",
                          "Click USER above the main macro knobs. This bank contains six controls you can assign to parameters throughout Analog.",
                          Completion::action ({ "macroBank.changed" }, Condition::state ("macros.bank.user"))
                              .withAlreadyDone (Condition::state ("macros.bank.user"), "The USER bank is already open."),
                          "The USER bank is open.");
        user.revealOnEnter = false;
        auto menu = task ("menu", "osc.A.level", "Choose a parameter", "Find the LEVEL knob on OSC 1. Right-click it to open the parameter menu.",
                          Completion::action ({ "control.menu.opened" }, Condition::always()).forParameter (params::osc1Level));
        menu.advanceWhenDone = true;   // (its menu is open now: the next step speaks to it)
        menu.requirements = { need ("osc1.heard", Condition::state ("analog.osc1.heard"), "OSC 1 is off",
                                    "OSC 1 is switched off or muted, so its LEVEL can't be heard. Switch it on (or clear M on its card), or load the example sound.",
                                    "osc.A.level") };
        auto assign = task ("assign", "osc.A.level", "Assign Macro 1",
                            "Choose Assign to Macro, then Macro 1. The LEVEL knob is now controlled by Macro 1.",
                            Completion::action ({ "usermacro.assigned" }, Condition::state ("analog.usermacro.1.osc1Level"))
                                .withAlreadyDone (Condition::state ("analog.usermacro.1.osc1Level"), "OSC 1 LEVEL is already assigned to Macro 1."),
                            "Macro 1 now controls OSC 1 LEVEL.");
        assign.outlineTarget = false;   // (the instruction is about the menu: the note sits by the knob)
        assign.unavailableBody = "Right-click the LEVEL knob on OSC 1 again to open its menu.";
        auto range = task ("range", "osc.A.level", "Set the range",
                           "The coloured arc shows the range Macro 1 can move this parameter through. Drag either end of the arc to change that range.",
                           Completion::action ({ "usermacro.range.changed" }, Condition::always()).forParameter (params::osc1Level),
                           "The range is set.");
        range.requirements = { need ("assigned", Condition::state ("analog.usermacro.1.osc1Level"), "Assign Macro 1 first",
                                     "Right-click OSC 1 LEVEL and choose Assign to Macro, then Macro 1.", "osc.A.level", "", false) };
        auto turn = task ("turn", "usermacro.0", "Turn Macro 1",
                          "Turn Macro 1 in the USER bank. Watch the modulation indicator around OSC 1 LEVEL and listen as its volume changes.",
                          Completion::action ({ "usermacro.0.touched" }, Condition::changed (params::userMacroId (0))), "Macro 1 moved OSC 1 LEVEL.");
        turn.keepClear = { "osc.A.level" };
        turn.requirements = { need ("user.shown", Condition::state ("macros.bank.user"), "Open the USER bank",
                                    "Macro 1 is in the USER bank. Click USER above the main knobs.", "macros.bank", "", false) };
        auto review = explain ("review", "usermacro.0.label", "Review the assignment",
                               "Click the macro name to review its assignments. You can add more parameters or remove the assignment from the original knob's right-click menu.");
        review.revealLabel = "Open USER";
        l.steps = { user, menu, assign, range, turn, review };
        return l;
    }

    Lesson auxLesson()
    {
        Lesson l;
        l.id = "analog.aux.live";
        l.title = "Use external audio";
        l.description = "Route another DAW track through AUX D";
        l.group = "AUX D";
        l.capability = "analog.aux";
        l.revision = 3;   // (3: AUX D shown with the keyboard's D key)
        l.minutes = 3;
        l.completionText = "Your DAW track is now part of Analog through AUX D. You can mix it with the oscillators, filter it, process it with effects or use it as a MORPH destination.";
        const auto shown = auxShown();
        auto inputOn = need ("input.available", Condition::state ("analog.d.input.available"), "No incoming audio",
                             "Analog isn't receiving audio through its sidechain input. Check the routing in your DAW and play the source track.");
        inputOn.offerExample = false;
        auto signal = need ("input.signal", Condition::state ("analog.d.signal"), "No incoming audio",
                            "The sidechain is connected, but no audio is arriving. Play the source track in your DAW; the AUX display moves when it arrives.",
                            "aux.display", "", false);
        auto liveSelected = need ("live.selected", Condition::equals (params::auxMode, 3.0f), "Choose LIVE INPUT",
                                  "Choose LIVE INPUT from AUX D's source dropdown first.", "aux.mode", "", false);
        // The D key beneath ARP and SUB (the musician's own click); already showing: Continue.
        auto show = task ("show", "keyboard.auxToggle", "Show AUX D",
                          "Click D beneath the ARP/SUB controls to show AUX D. The strip will appear below OSC 1, OSC 2 and OSC C.",
                          Completion::action ({ "aux.visibility.toggled" }, Condition::state ("analog.aux.shown"))
                              .withAlreadyDone (Condition::state ("analog.aux.shown"), "AUX D is already visible."),
                          "AUX D is showing.");
        show.revealOnEnter = false;
        auto live = task ("live", "aux.mode", "Select LIVE INPUT",
                          "Use the source dropdown on AUX D and choose LIVE INPUT. This mode receives audio from another track through your DAW's sidechain routing.",
                          Completion::action ({ "aux.mode.touched" }, Condition::equals (params::auxMode, 3.0f))
                              .viaMenu()   // (a dropdown: its choice comes after the click)
                              .withAlreadyDone (Condition::equals (params::auxMode, 3.0f), "LIVE INPUT is already selected."),
                          "AUX D is set to LIVE INPUT.");
        live.requirements = { shown };
        auto power = task ("power", "aux.power", "Turn on AUX D", "Switch AUX D on. Its LEVEL knob controls the incoming audio volume.",
                           Completion::action ({ "aux.power.touched" }, Condition::equals (params::auxEnabled, 1.0f))
                               .withAlreadyDone (Condition::equals (params::auxEnabled, 1.0f), "AUX D is already on."),
                           "AUX D is on.");
        power.requirements = { shown };
        auto route = explain ("route", "aux.display", "Route audio from your DAW",
                              "In your DAW, send an audio track to ANDOR Analog's sidechain input. The AUX display will show incoming signal when the connection is working.");
        route.requirements = { shown, liveSelected };
        auto verify = explain ("verify", "aux.display", "Verify the incoming signal",
                               "Play the source track in your DAW. Look for movement in the AUX display, then raise AUX LEVEL if necessary.");
        verify.keepClear = { "aux.level" };
        verify.requirements = { shown, liveSelected, inputOn, signal };
        auto shape = task ("shape", "macro.2.knob", "Shape the audio",
                           "The incoming audio passes through Analog's FILTER and global effects. Turn FILTER while the source plays to hear it change.",
                           Completion::action ({ "macro.2.touched" }, Condition::changed (params::tone)), "FILTER is shaping the incoming audio.");
        shape.requirements = { liveSelected, inputOn, signal };
        auto morph = task ("morph", "morph.destination.D", "Use the audio in MORPH",
                           "Open the MORPH rack and choose D under MORPH TO. You can now combine your synthesizer sound with the audio arriving from the DAW.",
                           Completion::action ({ "morph.destination.touched" }, Condition::equals (params::morphTarget, 1.0f))
                               .withAlreadyDone (Condition::equals (params::morphTarget, 1.0f), "D is already selected under MORPH TO."),
                           "MORPH now blends toward the audio from your DAW.");
        morph.requirements = { liveSelected, inputOn, signal, morphRackOpen() };
        morph.revealLabel = "Open Rack";
        l.steps = { show, live, power, route, verify, shape, morph };
        return l;
    }

    Lesson fxLesson()
    {
        Lesson l;
        l.id = "analog.fx.routing";
        l.title = "Explore FX Routing";
        l.description = "See and change the effect chain";
        l.group = "FX Routing";
        l.capability = "analog.fxRouting";
        l.revision = 2;   // (2: Learn 2.0 - a specific tile, its rack, a reorder)
        l.minutes = 2;
        l.completionText = "FX Routing controls the order and signal flow of your effects. Use it to change how the same sound is processed without rebuilding the instrument.";
        auto open = task ("open", "routing.button", "Open FX Routing", "Click the routing icon near the preset controls to open the effect chain.",
                          Completion::action ({ "sourceView.toggled" }, Condition::state ("sourceView.open"))
                              .withAlreadyDone (Condition::state ("sourceView.open"), "FX Routing is already open."),
                          "The effect chain is showing.");
        open.revealOnEnter = false;
        auto routingOpen = need ("routing.open", Condition::state ("sourceView.open"), "Open FX Routing",
                                 "Click the routing icon near the preset controls to show the effect chain again.", "routing.button", "", false);
        auto mixer = explain ("mixer", "routing.mixer", "Understand the mixer",
                              "The mixer on the left shows how the sound sources enter the effects. Adjusting a source level changes how much audio it contributes.");
        mixer.requirements = { routingOpen };
        auto select = task ("select", "routing.tile", "Select an effect",
                            "Click an effect block to open its quick settings. The block represents one processor in the signal path.",
                            Completion::action ({ "routing.board.touched" }, Condition::state ("routing.quick.open"))
                                .withAlreadyDone (Condition::state ("routing.quick.open"), "An effect's quick settings are already open."),
                            "Its quick settings are open.");
        select.requirements = { routingOpen };
        auto rack = task ("rack", "routing.tile", "Open the full rack",
                          "Double-click the effect block to open its full rack. This gives you the detailed controls for that processor.",
                          Completion::action ({ "routing.board.touched" }, Condition::state ("rack.any.open"))
                              .withAlreadyDone (Condition::state ("rack.any.open"), "A rack is already open."),
                          "Its full rack is open.");
        rack.requirements = { routingOpen };
        auto move = task ("move", "routing.tile", "Change the chain",
                          "Drag an effect block to change its position in the chain. The order matters because each processor receives the output of the one before it.",
                          Completion::action ({ "routing.board.touched" }, Condition::changed ("analog.fx.order")), "The effect is in its new place.");
        move.requirements = { routingOpen, need ("reorderable", Condition::state ("analog.fx.reorderable"), "Choose MAIN or SEND",
                                                "The OVERVIEW tab shows both chains in miniature. Choose MAIN or SEND to move an effect.", "routing.board", "", false) };
        auto back = task ("back", "routing.button", "Return to the instrument",
                          "Click the routing icon again to return to the oscillator cards. Your effect settings and routing remain active.",
                          Completion::action ({ "sourceView.toggled" }, Condition::isNot (Condition::state ("sourceView.open"))),
                          "The oscillator cards are back.");
        for (auto* s : { &mixer, &select, &rack, &move })
            s->unavailableBody = "FX Routing is closed. Open it to continue.";
        l.steps = { open, mixer, select, rack, move, back };
        return l;
    }

    //==========================================================================================
    // Info: what each region is for, in a sentence or three.

    std::vector<HelpTopic> help()
    {
        auto h = [] (const char* target, const char* title, const char* body, const char* lesson = "") {
            return HelpTopic { target, u8 (title), u8 (body), lesson };
        };
        return {
            h ("header.preset", "Presets",
               "Choose a sound from the preset name at the top. The arrows move between presets. Use SAVE to store your edited sound as a user preset.",
               "analog.quickstart"),
            h ("source.card.A", "OSC 1 \xe2\x80\x94 Analog oscillator",
               "OSC 1 generates the first analog sound source. DETUNE, TUNE and OCT adjust its pitch; LEVEL controls its volume. Open the oscillator settings to change its waveform and model."),
            h ("source.card.B", "OSC 2 \xe2\x80\x94 Analog oscillator",
               "OSC 2 is a second independent analog sound source. It can be tuned differently from OSC 1 and can follow its own arpeggiator pattern.",
               "analog.arp.basics"),
            h ("source.card.C", "OSC C \xe2\x80\x94 Hybrid source",
               "OSC C can use an oscillator, a recording, noise or an instrument patch. Choose its source type from the dropdown. C1 and C2 allow two layers where supported.",
               "analog.morph.basics"),
            h ("macro.0", "MORPH",
               "MORPH combines the synthesizer with OSC C or AUX D. Open the rack and use MORPH TO to choose the destination. The main knob controls how far the sound moves toward it.",
               "analog.morph.basics"),
            h ("macro.1", "DRIVE",
               "DRIVE adds saturation and harmonic distortion. Turn the knob to change the amount. Open the rack to choose the processing character and adjust its tone."),
            h ("macro.2", "FILTER",
               "FILTER shapes the frequency content of the sound. Lower the cutoff to remove high frequencies; raise it to let them through. Open the rack for resonance and other filter settings."),
            h ("macro.3", "MOVEMENT",
               "MOVEMENT adds changing motion to the sound, including drift, pulses, tape movement and rhythmic shaping. Open its rack to choose a mode and adjust its timing and depth."),
            h ("macro.4", "SPACE",
               "SPACE adds reverberation. It can make the sound feel as though it is playing in a room, hall or larger environment. Open the rack to adjust the reverb."),
            h ("macro.5", "ECHO",
               "ECHO repeats the sound after a chosen delay. TIME controls the space between repeats, while FEEDBACK controls how long they continue. Open the rack for more settings."),
            h ("macros.bank", "CORE and USER macros",
               "CORE contains the six main sound controls. USER contains six assignable macros. Right-click a parameter to assign it to one of the USER macros.",
               "analog.usermacros.basics"),
            h ("mod.panel", "Modulation",
               "Modulation changes a parameter automatically over time or in response to playing. Use LFOs, envelopes, velocity and other sources to control the selected parameter."),
            h ("keyboard", "Keyboard",
               "Play notes here or from an external MIDI keyboard. The pitch wheel bends notes, while the modulation wheel controls its assigned modulation."),
            h ("arp.control", "Arpeggiator",
               "The arpeggiator plays held MIDI notes in a repeating pattern. Switch ARP on to enable it, or open its panel to choose the pattern, rate and participating oscillators.",
               "analog.arp.basics"),
            h ("arp.companion", "Sub-oscillator",
               "SUB adds a lower oscillator beneath the notes you play. Open its controls to adjust its octave, level and other available settings."),
            h ("arp.panel", "Arpeggiator controls",
               "PATTERN sets the note order. RATE sets the speed. PLAYS selects which oscillators follow the pattern. Turn LINK off to give the oscillators independent patterns.",
               "analog.arp.basics"),
            h ("aux.strip", "AUX D \xe2\x80\x94 Additional source",
               "AUX D adds a fourth sound source. Select RHYTHM, NOISE, KONTAKT or LIVE INPUT. Its LEVEL knob controls the source volume, and RACK opens the detailed controls.",
               "analog.aux.live"),
            h ("keyboard.settings", "Advanced Settings",
               "Open the instrument's detailed settings, including voice and tuning options. The available controls depend on the instrument."),
            h ("keyboard.info", "Info", "Turn Info on to read explanations of controls as you point to them. Turn it off to return to the normal view."),
            h ("keyboard.auxToggle", "AUX D",
               "Show or hide AUX D below the oscillator cards. Hiding it only hides the controls: D keeps its sound, power and settings.", "analog.aux.live"),
            h ("routing.button", "FX Routing",
               "FX Routing shows how the sound sources connect to the effects. Open it to inspect or change the order and signal flow of the processors.",
               "analog.fx.routing"),
            h ("routing.board", "Effect chain",
               "Audio passes through the effect blocks in the order shown. Select a block for its quick settings, double-click for its rack, or drag it to change its position.",
               "analog.fx.routing"),
            h ("rack.area", "Effect rack",
               "A rack contains the detailed settings for one processor. Close it with X when finished. Closing the rack does not turn the effect off."),
        };
    }

    //==========================================================================================
    // The example sounds: the product's own material only (oscillators and noise: no recording,
    // no Kontakt, no sidechain), every other parameter at its default, nothing else carried over.

    std::unique_ptr<juce::XmlElement> exampleXml (osp::plugin::InstrumentHost& host, const std::map<juce::String, float>& values)
    {
        auto tree = host.apvts().copyState();
        for (auto* p : host.apvts().processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const auto it = values.find (ranged->getParameterID());
                const float value = it != values.end() ? it->second : ranged->convertFrom0to1 (ranged->getDefaultValue());
                auto child = tree.getChildWithProperty ("id", ranged->getParameterID());
                if (child.isValid())
                    child.setProperty ("value", ranged->convertFrom0to1 (ranged->convertTo0to1 (value)), nullptr);
            }
        // (Modulation links, macro assignments and other trees are the musician's own work: they
        // stay in the sound kept for Restore, not in the example.)
        for (int i = tree.getNumChildren(); --i >= 0;)
            if (! tree.getChild (i).hasType ("PARAM"))
                tree.removeChild (i, nullptr);
        const auto& info = andor::shell::productInfo();
        auto xml = std::make_unique<juce::XmlElement> (info.stateType);
        xml->setAttribute ("schemaVersion", 1);
        xml->setAttribute ("product", info.slug);
        xml->setAttribute ("productVersion", info.stateVersion);
        if (auto params = tree.createXml())
            xml->addChildElement (params.release());
        return xml;
    }

    std::unique_ptr<juce::XmlElement> exampleState (const juce::String& id, osp::plugin::InstrumentHost& host)
    {
        using namespace params;
        if (id == "analog.example.morph")
        {
            // A + B audible; C a third OSCILLATOR an octave up (a clearly different colour); D
            // keyed NOISE (pink); MORPH on, toward C, starting at 0 % (the analog source first);
            // ARP off.
            return exampleXml (host, { { osc1Enabled, 1.0f },
                                       { osc2Enabled, 1.0f },
                                       { hybridType, 1.0f },
                                       { osc3Octave, 1.0f },
                                       { auxEnabled, 1.0f },
                                       { auxMode, 1.0f },
                                       { auxNoiseColor, 1.0f },
                                       { auxNoiseGate, 0.0f },
                                       { morphEnabled, 1.0f },
                                       { morphTarget, 0.0f },
                                       { morphPosition, 0.0f },
                                       { arpOn, 0.0f } });
        }
        if (id == "analog.example.arp")
        {
            // Two audible oscillators an octave apart (A low, B high): the arpeggiator off, UP at
            // 1/8 waiting; A and B both following it, LINK on - so each step changes one thing.
            return exampleXml (host, { { osc1Enabled, 1.0f },
                                       { osc2Enabled, 1.0f },
                                       { osc2Octave, 1.0f },
                                       { arpOn, 0.0f },
                                       { cardA, 1.0f },
                                       { cardB, 1.0f },
                                       { link, 1.0f },
                                       { morphEnabled, 0.0f } });
        }
        if (id == "analog.example.simple")
            return exampleXml (host, { { osc1Enabled, 1.0f }, { osc2Enabled, 1.0f }, { morphEnabled, 0.0f }, { arpOn, 0.0f } });
        return nullptr;
    }

    //==========================================================================================

    FxRoutingView* routingView (andor::shell::ShellEditor& editor)
    {
        return dynamic_cast<FxRoutingView*> (editor.sourceViewComponent());
    }

    AnalogEngine* engineOf (andor::shell::ShellProcessor& shell)
    {
        return dynamic_cast<AnalogEngine*> (&shell.productEngine());
    }

    float plain (andor::shell::ShellProcessor& shell, const juce::String& id)
    {
        auto* p = shell.parameters.getParameter (id);
        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
    }

    /** The storage layer whose card is shown as `letter` (A B C). */
    int storageOf (const juce::String& letter)
    {
        for (int l = 0; l < 3; ++l)
            if (osp::plugin::InstrumentHost::shownLayerName (l) == letter)
                return l;
        return 0;
    }

    /** A card is heard: not muted, and not left out by another card's (or D's) solo. */
    bool cardHeard (andor::shell::ShellProcessor& shell, const juce::String& letter)
    {
        using osp::plugin::InstrumentHost;
        const int l = storageOf (letter);
        bool anySolo = plain (shell, params::auxSolo) >= 0.5f;
        for (int k = 0; k < 3; ++k)
            anySolo = anySolo || plain (shell, InstrumentHost::layerParameterId (k, "solo")) >= 0.5f;
        const bool soloed = plain (shell, InstrumentHost::layerParameterId (l, "solo")) >= 0.5f;
        if (plain (shell, InstrumentHost::layerParameterId (l, "mute")) >= 0.5f && ! soloed)
            return false;
        return ! anySolo || soloed;
    }

    bool parameterAtFloor (andor::shell::ShellProcessor& shell, const juce::String& id)
    {
        auto* p = shell.parameters.getParameter (id);
        return p == nullptr || p->getValue() <= 1.0e-3f;
    }
} // namespace

Catalog learnCatalog()
{
    Catalog c;
    c.productId = "analog";
    c.lessons = { quickStart(), arpLesson(), morphLesson(), userMacroLesson(), auxLesson(), fxLesson() };
    c.help = help();
    c.quickStartLessonId = "analog.quickstart";
    return c;
}

andor::shell::LearnDescriptor learnDescriptor()
{
    andor::shell::LearnDescriptor d;
    d.catalog = [] { return learnCatalog(); };
    d.exampleState = [] (const juce::String& id, osp::plugin::InstrumentHost& host) { return exampleState (id, host); };
    d.hasCapability = [] (const juce::String& capability, osp::plugin::InstrumentHost& host) {
        if (capability == "analog.arp.cardRouting")
            return host.cardArpeggiator();
        if (auto* shell = dynamic_cast<andor::shell::ShellProcessor*> (&host))
        {
            if (capability == "analog.userMacros")
                return shell->userMacros() != nullptr;
            if (capability == "analog.aux")
                return shell->hasAuxStrip();
        }
        return capability == "analog.morph" || capability == "analog.fxRouting";
    };
    d.registerTargets = [] (TargetRegistry& targets, andor::shell::ShellEditor& editor) {
        using osp::plugin::rack::Kind;
        namespace binding = osp::plugin::binding;
        auto* e = &editor;
        auto* shell = dynamic_cast<andor::shell::ShellProcessor*> (e->getAudioProcessor());
        auto* registry = &targets;
        auto openMorph = [e] {
            e->setRackExpanded (Kind::morph, true);
            return e->effectsRack().isShown (Kind::morph);
        };
        // A control found by the parameter it edits, inside `root` (visible only).
        auto controlIn = [] (std::function<juce::Component*()> root, juce::String id) {
            return [root, id]() -> juce::Component* {
                auto* r = root();
                return r != nullptr ? binding::findControl (*r, id) : nullptr;
            };
        };
        auto content = [e] { return &e->instrumentContent(); };
        auto rack = [e] { return static_cast<juce::Component*> (&e->effectsRack()); };

        // The cards: A and B together; OSC 1's LEVEL; card C's source type, M and LEVEL.
        targets.add ({ "cards.AB", content, [e] (juce::Component&) {
                          juce::Rectangle<int> all;
                          for (int l = 0; l < 3; ++l)
                          {
                              const auto letter = osp::plugin::InstrumentHost::shownLayerName (l);
                              auto& card = e->engineCard (l);
                              if ((letter == "A" || letter == "B") && card.isVisible())
                              {
                                  const auto r = e->instrumentContent().getLocalArea (&card, card.getLocalBounds());
                                  all = all.isEmpty() ? r : all.getUnion (r);
                              }
                          }
                          return e->sourceViewShown() ? juce::Rectangle<int>() : all;
                      } });
        targets.add ({ "osc.A.level", controlIn (content, params::osc1Level) });
        targets.add ({ "card.C.source", controlIn (content, params::hybridType) });
        targets.add ({ "card.C.mute", controlIn (content, osp::plugin::InstrumentHost::layerParameterId (andor::shell::ShellProcessor::hybridLayer, "mute")) });
        targets.add ({ "card.C.level", controlIn (content, osp::plugin::InstrumentHost::layerParameterId (andor::shell::ShellProcessor::hybridLayer, "level")) });

        // MORPH: its name (double-click: the rack), its lamp, its knob; in the rack MORPH TO (and
        // its C and D segments), TEXTURE and METHOD - each the exact control.
        targets.add ({ "analog.morph.name", [e] { return static_cast<juce::Component*> (e->macroLabel (0)); }, {}, openMorph });
        targets.add ({ "morph.label", [e] { return static_cast<juce::Component*> (e->macroLabel (0)); }, {}, openMorph });
        targets.add ({ "morph.enable", [e] { return static_cast<juce::Component*> (e->macroSwitch (0)); }, {}, {}, {}, false, true });
        targets.add ({ "analog.morph.knob", [e] { return static_cast<juce::Component*> (e->macroKnob (0)); } });
        targets.add ({ "morph.amount", [e] { return static_cast<juce::Component*> (e->macroKnob (0)); } });
        auto morphTo = controlIn (rack, params::morphTarget);
        targets.add ({ "morph.destination", morphTo, {}, openMorph, {}, false, true });
        for (int segment = 0; segment < 2; ++segment)
            targets.add ({ segment == 0 ? "morph.destination.C" : "morph.destination.D", morphTo, [segment] (juce::Component& c) {
                              auto* s = dynamic_cast<osp::plugin::rackui::SegmentedSwitch*> (&c);
                              return s != nullptr ? s->segmentBounds (segment).getSmallestIntegerContainer() : c.getLocalBounds();
                          },
                           openMorph });
        targets.add ({ "morph.texture", controlIn (rack, params::morphTexture), {}, openMorph, {}, false, true });
        targets.add ({ "morph.method", controlIn (rack, params::morphMethod), {}, openMorph, {}, false, true });

        // AUX D's strip: its source dropdown, power, M, LEVEL and display (Show AUX reveals them).
        auto strip = [registry]() -> juce::Component* {
            const auto* t = registry->find ("aux.strip");
            return t != nullptr ? t->component() : nullptr;
        };
        auto showAux = [e] {
            e->setAuxStripShown (true);
            return e->auxStripShown();
        };
        auto part = [strip] (const char* name) {
            return [strip, n = juce::String (name)]() -> juce::Component* {
                auto* s = strip();
                return s != nullptr ? AuxStripView::part (*s, n) : nullptr;
            };
        };
        if (shell != nullptr && shell->hasAuxStrip())
        {
            targets.add ({ "aux.mode", part ("mode"), {}, showAux, {}, false, true });
            targets.add ({ "aux.power", part ("power"), {}, showAux, {}, false, true });
            targets.add ({ "aux.mute", part ("mute"), {}, showAux });
            targets.add ({ "aux.level", part ("LEVEL"), {}, showAux, {}, false, true });
            targets.add ({ "aux.display", part ("display"), {}, showAux });
        }
        targets.addState ("analog.aux.shown", [e] { return e->auxStripShown(); });

        // FX ROUTING: the board (touches on it are the musician's), its mixer, its chain, and the
        // one effect block a step means (the selected one, else the first).
        auto routing = [e] { return e->sourceViewShown() ? e->sourceViewComponent() : nullptr; };
        auto openRouting = [e] {
            e->setSourceViewShown (true);
            return e->sourceViewShown();
        };
        targets.add ({ "routing.board", routing, {}, openRouting, {}, true, true });
        targets.add ({ "routing.mixer", routing, [e] (juce::Component&) {
                          auto* v = routingView (*e);
                          return v != nullptr ? v->mixerBounds() : juce::Rectangle<int>();
                      },
                       openRouting });
        targets.add ({ "routing.chain", routing, [e] (juce::Component&) {
                          auto* v = routingView (*e);
                          return v != nullptr ? v->chainBounds() : juce::Rectangle<int>();
                      },
                       openRouting });
        targets.add ({ "routing.tile", routing, [e] (juce::Component&) {
                          auto* v = routingView (*e);
                          if (v == nullptr || v->tiles().empty())
                              return juce::Rectangle<int>();
                          if (const auto chosen = v->selection())
                              for (const auto& t : v->tiles())
                                  if (t.id == *chosen)
                                      return t.bounds;
                          return v->tiles().front().bounds;
                      },
                       openRouting });
        targets.addState ("routing.quick.open", [e] {
            auto* v = routingView (*e);
            return v != nullptr && e->sourceViewShown() && v->quickShown().has_value();
        });
        targets.addState ("analog.fx.reorderable", [e] {
            auto* v = routingView (*e);
            return v != nullptr && v->tab() != 2 && v->tiles().size() >= 2;
        });
        // The chain's order, as one number (a lesson compares it as it compares a parameter: only
        // the musician's drag between touches counts).
        targets.addValue ("analog.fx.order", [shell] {
            auto* engine = shell != nullptr ? engineOf (*shell) : nullptr;
            if (engine == nullptr)
                return 0.0f;
            juce::String s;
            std::function<void (const std::vector<fx::Node>&)> walk = [&] (const std::vector<fx::Node>& chain) {
                for (const auto& n : chain)
                {
                    if (n.kind == fx::Node::Kind::effect)
                        s << static_cast<int> (n.fx.type) << '.' << n.fx.slot << ',';
                    else
                        for (const auto& lane : n.lanes)
                        {
                            s << '(';
                            walk (lane);
                            s << ')';
                        }
                }
            };
            const auto g = engine->fxRoute();
            walk (g.chain);
            s << '|';
            walk (g.send);
            return static_cast<float> (s.hashCode() & 0xffffff);   // (24 bits: exact in a float)
        });

        // What a lesson's sound needs, read from the real state.
        if (shell == nullptr)
            return;
        targets.addState ("analog.ab.audible", [shell] {
            const bool a = plain (*shell, params::osc1Enabled) >= 0.5f && ! parameterAtFloor (*shell, params::osc1Level) && cardHeard (*shell, "A");
            const bool b = plain (*shell, params::osc2Enabled) >= 0.5f && ! parameterAtFloor (*shell, params::osc2Level) && cardHeard (*shell, "B");
            return a || b;
        });
        targets.addState ("analog.osc1.heard", [shell] { return plain (*shell, params::osc1Enabled) >= 0.5f && cardHeard (*shell, "A"); });
        targets.addState ("analog.c.heard", [shell] { return cardHeard (*shell, "C"); });
        targets.addState ("analog.c.levelUp", [shell] {
            return ! parameterAtFloor (*shell, osp::plugin::InstrumentHost::layerParameterId (andor::shell::ShellProcessor::hybridLayer, "level"));
        });
        targets.addState ("analog.c.playable", [shell] {
            // An oscillator and noise always play; a recording or a patch needs one loaded; the
            // hosted instrument must be running. ("A file chosen" is not enough: it must be ready.)
            const auto type = static_cast<HybridType> (std::clamp (juce::roundToInt (plain (*shell, params::hybridType)), 0, 4));
            switch (type)
            {
                case HybridType::oscillator:
                case HybridType::noise:
                    return true;
                case HybridType::sample:
                case HybridType::instrumentPatch:
                {
                    // (Its card shows a placeholder - ADD SAMPLE - while nothing is loaded.)
                    osp::plugin::InstrumentHost::GeneratedSource g;
                    return ! (shell->generatedSource (andor::shell::ShellProcessor::hybridLayer, g) && g.placeholder.isNotEmpty());
                }
                case HybridType::external:
                {
                    auto* engine = engineOf (*shell);
                    return engine != nullptr && engine->externalInstrument().state() == ExternalInstrument::State::ready;
                }
            }
            return false;
        });
        targets.addState ("analog.hybrid.silent", [shell] {
            const int t = juce::roundToInt (plain (*shell, params::hybridType));
            osp::plugin::InstrumentHost::GeneratedSource g;
            return (t == 0 || t == 3) && shell->generatedSource (andor::shell::ShellProcessor::hybridLayer, g) && g.placeholder.isNotEmpty();
        });
        targets.addState ("analog.d.heard", [shell] {
            bool anySolo = false;
            for (int k = 0; k < 3; ++k)
                anySolo = anySolo || plain (*shell, osp::plugin::InstrumentHost::layerParameterId (k, "solo")) >= 0.5f;
            const bool soloed = plain (*shell, params::auxSolo) >= 0.5f;
            if (plain (*shell, params::auxMute) >= 0.5f && ! soloed)
                return false;
            return ! anySolo || soloed;
        });
        targets.addState ("analog.d.levelUp", [shell] { return ! parameterAtFloor (*shell, params::auxLevel); });
        auto signal = [shell] {
            auto* engine = engineOf (*shell);
            if (engine == nullptr)
                return false;
            const auto in = engine->liveInput();
            return in.busExists && in.busEnabled && std::max ({ in.peakLeft, in.peakRight, in.rms }) > 0.003f;
        };
        targets.addState ("analog.d.input.available", [shell] {
            auto* engine = engineOf (*shell);
            if (engine == nullptr)
                return false;
            const auto in = engine->liveInput();
            return in.busExists && in.busEnabled;
        });
        targets.addState ("analog.d.signal", signal);
        targets.addState ("analog.d.playable", [shell, signal] {
            auto* engine = engineOf (*shell);
            if (engine == nullptr)
                return false;
            switch (std::clamp (juce::roundToInt (plain (*shell, params::auxMode)), 0, 3))
            {
                case 0: return engine->auxRecordingState().status == AnalogEngine::AuxRecordingState::Status::ready;   // RHYTHM: a loop
                case 1: return true;                                                                                       // NOISE
                case 2: return engine->auxInstrument().state() == ExternalInstrument::State::ready;                        // KONTAKT
                case 3: return signal();                                                                                   // LIVE INPUT: audio arriving
                default: break;
            }
            return false;
        });
        // USER macros: Macro 1 moves OSC 1 LEVEL (that exact assignment, not merely any).
        targets.addState ("analog.usermacro.1.osc1Level", [shell] {
            const auto* u = shell->userMacros();
            return u != nullptr && u->targets != nullptr && u->targets (*shell, 0).contains (params::osc1Level);
        });
        targets.addState ("analog.userMacros.assigned", [shell] {
            const auto* u = shell->userMacros();
            if (u == nullptr || u->activeCount == nullptr)
                return false;
            for (int m = 0; m < 6; ++m)
                if (u->activeCount (*shell, m) > 0)
                    return true;
            return false;
        });
    };
    return d;
}

} // namespace andor::analog
