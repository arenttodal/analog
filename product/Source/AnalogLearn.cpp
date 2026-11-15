#include "AnalogLearn.h"

#include "AnalogParameters.h"
#include "FxRouting.h"
#include "LearnTargetRegistry.h"
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

    Step task (const char* id, const char* target, const char* title, const char* body, Completion completion, bool revealOnEnter = true)
    {
        auto s = explain (id, target, title, body);
        s.completion = std::move (completion);
        s.revealOnEnter = revealOnEnter;
        return s;
    }

    // The per-card arpeggiator's parameters (the shell's ids::arpCard / arpCardSetting with
    // Analog's namespace): the visible cards' letters, whatever their storage layers.
    const juce::String cardA = "analog.arp.cardA", cardB = "analog.arp.cardB", link = "analog.arp.link";
    const juce::String cardBPattern = "analog.arp.cardB.pattern", cardBRate = "analog.arp.cardB.rate";

    Lesson quickStart()
    {
        Lesson l;
        l.id = "analog.quickstart";
        l.title = "Quick Start";
        l.group = "Getting started";
        l.revision = 2;   // (2: C1 / C2, USER macros, SUB, AUX D)
        l.minutes = 2;
        auto aux = explain ("aux", "aux.strip", "AUX D",
                            "A fourth source beside the cards: a rhythm loop, noise, a Kontakt instrument, or your DAW's audio (LIVE INPUT).");
        aux.revealOnEnter = false;   // (showing D changes the window: only Show me does)
        aux.unavailableBody = u8 ("D is hidden. Show me shows it, or choose Show AUX D from the menu at the top right.");
        l.steps = {
            explain ("sounds", "header.preset", "Sounds",
                     "Every sound starts here. The arrows step through the sounds; click the name to choose one. SAVE keeps your own."),
            explain ("cards", "sources", "Three cards",
                     "A and B are oscillators. C plays a recording, noise or an instrument; its C1 / C2 tabs stack a second layer."),
            explain ("macros", "macros", "Six macros",
                     "MORPH, DRIVE, FILTER, MOVEMENT, SPACE, ECHO: click a name for its settings, double-click for its rack. The USER tab allows for 6 custom macros."),
            explain ("arp", "arp.control", "ARP and SUB",
                     "ARP toggles the arpeggiator. SUB toggles a sub oscillator. Open their panels to change the settings, or click the light to turn it on and off."),
            aux,
        };
        return l;
    }

    Lesson arpLesson()
    {
        Lesson l;
        l.id = "analog.arp.basics";
        l.title = "Make a moving pattern";
        l.group = "Arpeggiator";
        l.capability = "analog.arp.cardRouting";
        l.minutes = 3;
        l.steps = {
            task ("open", "arp.control", "Open the arpeggiator",
                  "It turns a held chord into a sequence. Click the ARP card (not its light) to open its controls.",
                  Completion::action ({ "arp.panel.opened" }, Condition::state ("arp.panel.open"))
                      .withAlreadyDone (Condition::state ("arp.panel.open"), "Already open"),
                  false),
            task ("enable", "arp.enable", "Switch it on", "Click the light to turn the arpeggiator on. Play a chord whenever you like.",
                  Completion::action ({ "arp.enabled.changed" }, Condition::equals ("arp.enabled", 1.0f))
                      .withAlreadyDone (Condition::equals ("arp.enabled", 1.0f), "Already on"),
                  false),
            task ("pattern", "arp.pattern", "Change the pattern", "The same notes can move in different orders. Pick another pattern from the list.",
                  Completion::action ({ "arp.pattern.changed" }, Condition::changed ("arp.pattern"))),
            task ("rate", "arp.rate", "Try another rate", "Turn RATE and hear how the pulse changes.",
                  Completion::action ({ "arp.rate.changed" }, Condition::changed ("arp.rate"))),
            task ("cards", "arp.cards", "Choose the cards",
                  "PLAYS sets which cards follow the pattern. Turn A off and B on: OSC 1 holds the chord, OSC 2 arpeggiates.",
                  Completion::action ({ "arp.card.toggled" }, Condition::all ({ Condition::equals (cardA, 0.0f), Condition::equals (cardB, 1.0f) }))
                      .withAlreadyDone (Condition::all ({ Condition::equals (cardA, 0.0f), Condition::equals (cardB, 1.0f) }), "Already set")),
            task ("unlink", "arp.link", "Unlink the cards", "Turn LINK off so each card can have a pattern of its own.",
                  Completion::action ({ "arp.link.changed" }, Condition::equals (link, 0.0f))
                      .withAlreadyDone (Condition::equals (link, 0.0f), "Already unlinked")),
            task ("card-b", "arp.card.B.edit", "Give B its own rhythm", "EDIT B shows OSC 2's own pattern and rate. Change either one.",
                  Completion::action ({ "arp.pattern.changed", "arp.rate.changed" },
                                      Condition::any ({ Condition::changed (cardBPattern), Condition::changed (cardBRate) }))),
            explain ("finish", "arp.panel", "An independent layer",
                     "OSC 2 now moves on its own while OSC 1 holds. Your sound stays as you left it."),
        };
        l.steps[6].unavailableBody = u8 ("Turn LINK off, then choose EDIT B to see OSC 2's own pattern and rate.");
        l.steps[2].unavailableBody = l.steps[3].unavailableBody = u8 ("Open the ARP panel (Show me) to see it.");
        return l;
    }

    Lesson morphLesson()
    {
        Lesson l;
        l.id = "analog.morph.basics";
        l.title = "Blend with MORPH";
        l.group = "Sound sources";
        l.capability = "analog.morph";
        l.revision = 2;   // (2: its on / off lamp, MORPH TO)
        l.minutes = 2;
        l.steps = {
            task ("open", "analog.morph.name", "Open MORPH", "MORPH fuses the oscillators with card C. Double-click its name to open the MORPH rack.",
                  Completion::action ({ "rack.toggled" }, Condition::state ("rack.morph.open"))
                      .withAlreadyDone (Condition::state ("rack.morph.open"), "Already open"),
                  false),
            task ("switch", "macro.0.switch", "Switch it on", "The small lamp beside MORPH's name switches it on. While it is off, the knob rests.",
                  Completion::action ({ "macro.0.switch.touched" }, Condition::equals (params::morphEnabled, 1.0f))
                      .withAlreadyDone (Condition::equals (params::morphEnabled, 1.0f), "Already on")),
            explain ("sources", "sources", "Two sources", "At 0 % you hear A and B; at 100 %, card C. In between, MORPH fuses them into one sound."),
            task ("move", "analog.morph.knob", "Move MORPH", "Turn the MORPH knob and listen as the sound travels between the sources.",
                  Completion::action ({ "macro.0.touched" }, Condition::changed (params::morphPosition))),
            explain ("target", "rack.morph", "MORPH TO",
                     "MORPH TO chooses what it blends into: card C, or D, the AUX source, even your DAW's audio with LIVE INPUT."),
            explain ("shape", "rack.morph", "Shape the blend", "The rack sets how the two sources fuse. Your sound stays as you left it."),
        };
        // Card C with nothing to play: say so, never load anything.
        l.steps[2].variantWhen = Condition::state ("analog.hybrid.silent");
        l.steps[2].variantBody = u8 ("Card C has nothing to play yet: load a sound into it (ADD SAMPLE) to hear MORPH reach it.");
        l.steps[4].unavailableBody = l.steps[5].unavailableBody = u8 ("The MORPH rack is closed. Show me opens it again.");
        return l;
    }

    Lesson userMacroLesson()
    {
        Lesson l;
        l.id = "analog.usermacros.basics";
        l.title = "Make your own macro";
        l.group = "Macros";
        l.capability = "analog.userMacros";
        l.minutes = 2;
        juce::StringArray touched;
        std::vector<Condition> moved;
        for (int m = 0; m < 6; ++m)
        {
            touched.add ("usermacro." + juce::String (m) + ".touched");
            moved.push_back (Condition::changed (params::userMacroId (m)));
        }
        l.steps = {
            task ("tab", "macros.bank", "Open USER", "The small CORE | USER tab above the macros holds six macros of your own. Click USER.",
                  Completion::action ({ "macroBank.changed" }, Condition::state ("macros.bank.user"))
                      .withAlreadyDone (Condition::state ("macros.bank.user"), "Already open"),
                  false),
            task ("assign", "source.card.A", "Give it a control",
                  "Right-click a knob (OSC 1's LEVEL, say) and choose Assign to Macro, then Macro 1.",
                  Completion::action ({ "usermacro.assigned" }, Condition::state ("analog.userMacros.assigned"))),
            explain ("range", "source.card.A", "Its range",
                     "The ring round the knob is the macro's range: drag either end to set where the macro takes it. The tab stays where you put it."),
            task ("move", "macros", "Turn your macro", "Turn Macro 1 and hear the control follow. One macro can move many controls at once.",
                  Completion::action (touched, Condition::any (moved))),
            explain ("finish", "macros.bank", "Yours to keep",
                     "Click a USER macro's name to see everything it moves. Right-click a control for its range or Remove from Macro."),
        };
        l.steps[3].unavailableBody = u8 ("Open the USER tab (Show me) to see your macros.");
        return l;
    }

    Lesson auxLesson()
    {
        Lesson l;
        l.id = "analog.aux.live";
        l.title = "Play your DAW through D";
        l.group = "Sound sources";
        l.capability = "analog.aux";
        l.minutes = 3;
        auto strip = explain ("strip", "aux.strip", "AUX D",
                              "D is a fourth source beside the cards: a rhythm loop, noise, a Kontakt instrument, or your DAW's audio.");
        strip.unavailableBody = u8 ("D is hidden. Show me shows it, or choose Show AUX D from the menu at the top right.");
        l.steps = {
            strip,
            task ("live", "aux.strip", "Choose LIVE INPUT", "Switch D on with its power key, then choose LIVE INPUT from its source menu.",
                  Completion::action ({ "aux.strip.touched" }, Condition::all ({ Condition::equals (params::auxMode, 3.0f), Condition::equals (params::auxEnabled, 1.0f) }))
                      .withAlreadyDone (Condition::all ({ Condition::equals (params::auxMode, 3.0f), Condition::equals (params::auxEnabled, 1.0f) }), "Set")),
            explain ("route", "aux.strip", "Route a track",
                     "In your DAW, send a track to this plugin's sidechain input, AUX D Live Input. D's display moves when audio arrives."),
            explain ("shape", "aux.strip", "Shape it here",
                     "D plays with no keys held, through its LEVEL, the FILTER macro and the effects, like any card."),
            explain ("morph", "analog.morph.name", "Morph into it",
                     "In the MORPH rack, MORPH TO D blends the synth into what D plays. Your DAW's audio becomes the target."),
        };
        for (std::size_t i = 1; i < l.steps.size() - 1; ++i)
            l.steps[i].unavailableBody = strip.unavailableBody;
        return l;
    }

    Lesson fxLesson()
    {
        Lesson l;
        l.id = "analog.fx.routing";
        l.title = "See the effect chain";
        l.group = "Effects";
        l.capability = "analog.fxRouting";
        l.minutes = 2;
        l.steps = {
            task ("open", "routing.button", "Open FX ROUTING", "The branching icon beside SAVE shows how your sound flows through the effects. Click it.",
                  Completion::action ({ "sourceView.toggled" }, Condition::state ("sourceView.open"))
                      .withAlreadyDone (Condition::state ("sourceView.open"), "Already open"),
                  false),
            explain ("mixer", "routing.mixer", "The cards' mixer", "On the left: each card's level and the path it takes into the chain."),
            task ("tile", "routing.chain", "Open an effect", "Click an effect tile for its quick settings. Nothing changes until you turn something.",
                  Completion::action ({ "routing.board.touched" }, Condition::state ("routing.quick.open"))
                      .withAlreadyDone (Condition::state ("routing.quick.open"), "Already open")),
            explain ("finish", "routing.chain", "Your chain",
                     "Double-click a tile for its full rack; the gear holds Restore original. The icon again brings the cards back."),
        };
        for (auto& s : l.steps)
            if (s.id != "open")
                s.unavailableBody = u8 ("FX ROUTING is closed. Show me opens it again.");
        return l;
    }

    std::vector<HelpTopic> help()
    {
        auto h = [] (const char* target, const char* title, const char* body, const char* lesson = "") {
            return HelpTopic { target, u8 (title), u8 (body), lesson };
        };
        return {
            h ("header.preset", "Sounds", "Factory sounds and your own. The arrows step through them; click the name to choose. SAVE keeps yours.",
               "analog.quickstart"),
            h ("source.card.A", "OSC 1", "Card A, the first oscillator: detune, tune, octave and level. Its heading opens its settings in the dark well."),
            h ("source.card.B", "OSC 2", "Card B, the second oscillator, with its own tune, octave and level. The arpeggiator can play it alone.",
               "analog.arp.basics"),
            h ("source.card.C", "Hybrid C",
               "Card C plays a recording, noise, an oscillator, an instrument patch or an external instrument. Its C1 / C2 tabs stack a second layer.",
               "analog.morph.basics"),
            h ("macro.0", "MORPH", "Blends the oscillators into card C (or D, with MORPH TO). Its lamp switches it on; double-click the name for its rack.",
               "analog.morph.basics"),
            h ("macro.1", "DRIVE", "Saturation of the whole sound: tube, tape or crunch. Click the name for its settings."),
            h ("macro.2", "FILTER", "The four-pole filter's cutoff. Its settings and rack hold resonance and the filter envelope (attack to release)."),
            h ("macro.3", "MOVEMENT", "Drift, tape, chorus, pulse or a rhythmic shaper. Double-click the name for its rack."),
            h ("macro.4", "SPACE", "The room the sound plays in, from a small room to an endless hall. Double-click the name for its rack."),
            h ("macro.5", "ECHO", "Repeats from a tape echo or a bucket-brigade delay. Double-click the name for its rack."),
            h ("macros.bank", "CORE and USER", "CORE: the six macros above. USER: six of your own; right-click any knob, Assign to Macro.",
               "analog.usermacros.basics"),
            h ("mod.panel", "Modulation", "LFOs, envelopes, velocity and note. Drag a tab onto a control, or right-click a control to assign."),
            h ("keyboard", "Keyboard", "Play here or from MIDI. The wheels beside it bend the pitch and add modulation."),
            h ("arp.control", "Arpeggiator", "The light turns it on; the rest of the row opens its pattern, rate and the cards that follow it.",
               "analog.arp.basics"),
            h ("arp.companion", "SUB", "A sub-oscillator under every note, an octave or two down. Its light switches it; the row opens its keys and level."),
            h ("arp.panel", "Arpeggiator settings", "Pattern, rate and octaves. PLAYS picks the cards that follow; with LINK off each card has its own.",
               "analog.arp.basics"),
            h ("aux.strip", "AUX D", "A fourth source: a rhythm loop, noise, a Kontakt instrument, or your DAW's audio (LIVE INPUT). RACK opens its controls.",
               "analog.aux.live"),
            h ("advanced", "Advanced", "Tuning, bend range, voices, glide, how touch changes the sound, and MPE."),
            h ("routing.button", "FX Routing", "Shows the effect chain in the cards' place. Opening it never changes the sound.", "analog.fx.routing"),
            h ("routing.board", "Effect chain", "Your sound flows left to right through these tiles. Click one for quick settings; drag one to reorder.",
               "analog.fx.routing"),
            h ("rack.area", "Rack", "The full controls of the macro you opened. Its X closes it; the sound stays as set."),
        };
    }

    FxRoutingView* routingView (andor::shell::ShellEditor& editor)
    {
        return dynamic_cast<FxRoutingView*> (editor.sourceViewComponent());
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
        auto* e = &editor;
        // MORPH's name (its double-click opens the rack; Show me opens it).
        targets.add ({ "analog.morph.name", [e] { return static_cast<juce::Component*> (e->macroLabel (0)); }, {}, [e] {
                          e->setRackExpanded (Kind::morph, true);
                          return e->effectsRack().isShown (Kind::morph);
                      } });
        // MORPH's knob alone (nothing to open: the knob is always there; touches on it are the
        // shell's macro.0 watcher's).
        targets.add ({ "analog.morph.knob", [e] { return static_cast<juce::Component*> (e->macroKnob (0)); } });
        // FX ROUTING: the board (touches on it are the musician's), its mixer and its chain.
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
        targets.addState ("routing.quick.open", [e] {
            auto* v = routingView (*e);
            return v != nullptr && e->sourceViewShown() && v->quickShown().has_value();
        });
        // Any USER macro moves something (an assignment made).
        targets.addState ("analog.userMacros.assigned", [e] {
            auto* host = dynamic_cast<andor::shell::ShellProcessor*> (e->getAudioProcessor());
            const auto* u = host != nullptr ? host->userMacros() : nullptr;
            if (u == nullptr || u->activeCount == nullptr)
                return false;
            for (int m = 0; m < 6; ++m)
                if (u->activeCount (*host, m) > 0)
                    return true;
            return false;
        });
        // Card C has nothing to play (a SAMPLE or INSTRUMENT PATCH with nothing loaded).
        targets.addState ("analog.hybrid.silent", [e] {
            auto* host = dynamic_cast<andor::shell::ShellProcessor*> (e->getAudioProcessor());
            if (host == nullptr)
                return false;
            const auto* type = host->parameters.getParameter (params::hybridType);
            const int t = type != nullptr ? juce::roundToInt (type->convertFrom0to1 (type->getValue())) : 0;
            return (t == 0 || t == 3) && ! host->isLayerOccupied (andor::shell::ShellProcessor::hybridLayer);
        });
    };
    return d;
}

} // namespace andor::analog
