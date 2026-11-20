// OSP parity R1 (docs/OSP_PARITY_AUDIT.md): the shared arpeggiator's STRUM in ANDOR ANALOG.
// STRUM is OSP's (src/engine/Arpeggiator, shared/andor-ui StrumPanel / ArpSection); the shell
// adds its parameters to every product. These tests play it through ANALOG's own engine, on
// ANALOG's own cards (A OSC 1, B OSC 2, C the hybrid), and drive its key in ANALOG's ARP panel.

#include "AnalogEngine.h"
#include "InstrumentFrame.h"
#include "ShellEditor.h"
#include "ShellProcessor.h"
#include "StrumPanel.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <vector>

namespace
{
    using andor::analog::AnalogEngine;
    using andor::analog::Synth;
    using andor::shell::ShellProcessor;

    constexpr double rate = 48000.0;
    constexpr int block = 32;   // (0.67 ms: fine enough to time a strum's onsets)
    constexpr int chordPattern = 5;   // ArpPattern::chord (the parameter's choice order)

    void setValue (ShellProcessor& p, const juce::String& id, float value)
    {
        auto* param = p.parameters.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    float valueOf (ShellProcessor& p, const juce::String& id)
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.parameters.getParameter (id));
        REQUIRE (param != nullptr);
        return param->convertFrom0to1 (param->getValue());
    }

    /** STRUM's parameter for a card (0 A, 1 B, 2 C): A's are the shell's own `arp.strum.*`,
        B's and C's ANALOG's `analog.arp.cardB|C.strum.*`. */
    juce::String strumId (int card, const char* field)
    {
        return (card == 0 ? juce::String ("arp.strum.") : juce::String ("analog.arp.card") + juce::String::charToString (static_cast<juce::juce_wchar> ('A' + card)) + ".strum.")
             + field;
    }

    juce::String patternId (int card)
    {
        return card == 0 ? juce::String ("arp.pattern") : juce::String ("analog.arp.card") + juce::String::charToString (static_cast<juce::juce_wchar> ('A' + card)) + ".pattern";
    }

    struct Onset
    {
        double seconds;
        int note;
        std::uint8_t cards;
    };

    struct Played
    {
        std::vector<Onset> onsets;
        int heldAtEnd = 0, activeAtEnd = 0;
    };

    /** Holds `notes` from the start until `release` seconds, renders `seconds`; `each` runs
        before every block (its start in seconds, the block's MIDI). */
    Played play (ShellProcessor& p, double seconds, double release, std::vector<int> notes = { 60, 64, 67 },
                 const std::function<void (double, juce::MidiBuffer&)>& each = {})
    {
        p.prepareToPlay (rate, block);
        const auto& synth = dynamic_cast<AnalogEngine&> (p.productEngine()).synthForTest();
        Played result;
        std::array<Synth::VoiceView, Synth::maxVoices> before {};
        juce::AudioBuffer<float> buffer (2, block);
        const int total = static_cast<int> (seconds * rate), releaseAt = static_cast<int> (release * rate);
        for (int pos = 0; pos < total; pos += block)
        {
            const double t = pos / rate;
            juce::MidiBuffer midi;
            if (each)
                each (t, midi);
            for (int note : notes)
            {
                if (pos == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)), 0);
                if (releaseAt >= pos && releaseAt < pos + block)
                    midi.addEvent (juce::MidiMessage::noteOff (1, note), releaseAt - pos);
            }
            p.processBlock (buffer, midi);
            for (int s = 0; s < Synth::maxVoices; ++s)
            {
                const auto v = synth.voiceView (s);
                const auto& b = before[static_cast<std::size_t> (s)];
                if (v.active && v.held && (! b.active || ! b.held || b.note != v.note || b.cards != v.cards))
                    result.onsets.push_back ({ t, v.note, v.cards });
                before[static_cast<std::size_t> (s)] = v;
            }
        }
        for (int s = 0; s < Synth::maxVoices; ++s)
        {
            const auto v = synth.voiceView (s);
            result.heldAtEnd += v.held ? 1 : 0;
            result.activeAtEnd += v.active ? 1 : 0;
        }
        return result;
    }

    /** The first chord step's onsets on `cards` (in time order): the notes and their spread. */
    struct Chord
    {
        std::vector<int> notes;
        double spreadMs = 0.0;
    };

    Chord chordOn (const Played& played, std::uint8_t cards, int stepIndex = 0, double stepSeconds = 0.25)
    {
        Chord c;
        double first = -1.0, last = -1.0;
        const double from = stepIndex * stepSeconds, to = from + stepSeconds;
        for (const auto& o : played.onsets)
            if (o.cards == cards && o.seconds >= from - 1.0e-9 && o.seconds < to - 0.002)
            {
                c.notes.push_back (o.note);
                if (first < 0.0)
                    first = o.seconds;
                last = o.seconds;
            }
        c.spreadMs = first < 0.0 ? 0.0 : 1000.0 * (last - first);
        return c;
    }

    constexpr std::uint8_t cardA = 1, cardB = 2, cardC = 4, allCards = 7;
    constexpr double blockMs = 1000.0 * block / rate;
    constexpr double stealMs = 3.0;   ///< a stolen voice's fade before the new note (Synth: stealFadeSeconds)
}

TEST_CASE ("parity strum: fifteen parameters, off by default; CHORD plays the chord together, as before STRUM", "[analog][arp][strum][parity]")
{
    ShellProcessor p;
    for (int card = 0; card < 3; ++card)
    {
        INFO ("card " << card);
        CHECK (valueOf (p, strumId (card, "on")) == 0.0f);
        CHECK (valueOf (p, strumId (card, "direction")) == 0.0f);   // UP
        CHECK (valueOf (p, strumId (card, "sync")) == 0.0f);        // FREE
        CHECK (valueOf (p, strumId (card, "ms")) == Catch::Approx (20.0f));
        CHECK (valueOf (p, strumId (card, "division")) == 1.0f);    // 1/32
        // The parameters are appended after ANALOG's own (hint 40, above ANALOG's highest, 39).
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p.parameters.getParameter (strumId (card, "on")));
        REQUIRE (ranged != nullptr);
        CHECK (ranged->getVersionHint() == 40);
    }
    setValue (p, "arp.enabled", 1.0f);
    setValue (p, "arp.pattern", static_cast<float> (chordPattern));
    const auto played = play (p, 1.2, 1.0);
    const auto first = chordOn (played, allCards);
    CHECK (first.notes.size() == 3);
    CHECK (first.spreadMs == 0.0);   // every note in the same block
    CHECK (played.heldAtEnd == 0);
}

TEST_CASE ("parity strum: FREE spreads a CHORD step UP, DOWN, RANDOM (seeded) and ALTERNATE on ANALOG's voices", "[analog][arp][strum][parity]")
{
    auto run = [] (int direction) {
        ShellProcessor p;
        setValue (p, "arp.enabled", 1.0f);
        setValue (p, "arp.pattern", static_cast<float> (chordPattern));
        setValue (p, "arp.rate", 1.0f);   // 1/8 at 120: 250 ms a step
        setValue (p, strumId (0, "on"), 1.0f);
        setValue (p, strumId (0, "direction"), static_cast<float> (direction));
        setValue (p, strumId (0, "ms"), 60.0f);
        return play (p, 1.1, 1.0);
    };
    {
        const auto up = chordOn (run (0), allCards);
        CHECK (up.notes == std::vector<int> { 60, 64, 67 });
        CHECK (up.spreadMs == Catch::Approx (60.0).margin (blockMs));
    }
    CHECK (chordOn (run (1), allCards).notes == std::vector<int> { 67, 64, 60 });
    {
        // RANDOM: every note once, and the same order on every run (the stored seed).
        const auto a = run (2), b = run (2);
        auto notes = chordOn (a, allCards).notes;
        CHECK (notes == chordOn (b, allCards).notes);
        std::sort (notes.begin(), notes.end());
        CHECK (notes == std::vector<int> { 60, 64, 67 });
    }
    {
        // ALTERNATE: up, then down.
        const auto alt = run (3);
        CHECK (chordOn (alt, allCards, 0).notes == std::vector<int> { 60, 64, 67 });
        CHECK (chordOn (alt, allCards, 1).notes == std::vector<int> { 67, 64, 60 });
    }
}

TEST_CASE ("parity strum: SYNC spreads by the tempo's note value (1/32 at 120 BPM is 62.5 ms)", "[analog][arp][strum][parity]")
{
    ShellProcessor p;
    setValue (p, "arp.enabled", 1.0f);
    setValue (p, "arp.pattern", static_cast<float> (chordPattern));
    setValue (p, "arp.rate", 1.0f);
    setValue (p, strumId (0, "on"), 1.0f);
    setValue (p, strumId (0, "sync"), 1.0f);
    setValue (p, strumId (0, "division"), 1.0f);   // 1/32
    const auto c = chordOn (play (p, 0.6, 0.5), allCards);
    CHECK (c.notes == std::vector<int> { 60, 64, 67 });
    CHECK (c.spreadMs == Catch::Approx (62.5).margin (blockMs));
}

TEST_CASE ("parity strum: unlinked, each card strums with its own settings; linked, every card strums as A", "[analog][arp][strum][cardarp][parity]")
{
    {
        ShellProcessor p;
        setValue (p, "arp.enabled", 1.0f);
        setValue (p, "analog.arp.link", 0.0f);
        for (int card = 0; card < 3; ++card)
        {
            setValue (p, patternId (card), static_cast<float> (chordPattern));
            setValue (p, strumId (card, "ms"), 45.0f);
        }
        setValue (p, strumId (0, "on"), 1.0f);                 // A: UP
        setValue (p, strumId (2, "on"), 1.0f);                 // C: DOWN
        setValue (p, strumId (2, "direction"), 1.0f);
        // B: CHORD without STRUM (its own settings: spread asked for, but off).
        const auto played = play (p, 0.6, 0.5);
        const auto a = chordOn (played, cardA), b = chordOn (played, cardB), c = chordOn (played, cardC);
        CHECK (a.notes == std::vector<int> { 60, 64, 67 });
        CHECK (a.spreadMs == Catch::Approx (45.0).margin (blockMs + stealMs));
        CHECK (b.notes.size() == 3);
        CHECK (b.spreadMs == 0.0);
        CHECK (c.notes == std::vector<int> { 67, 64, 60 });
        // (Nine voices sound here: a note that takes a stolen voice waits for its quick fade,
        // 3 ms, as every note does in ANALOG's voice budget - not STRUM's doing.)
        CHECK (c.spreadMs == Catch::Approx (45.0).margin (blockMs + stealMs));
        CHECK (played.heldAtEnd == 0);
    }
    {
        // LINK (the default): A's STRUM for every card, B's and C's own settings ignored.
        ShellProcessor p;
        setValue (p, "arp.enabled", 1.0f);
        setValue (p, "arp.pattern", static_cast<float> (chordPattern));
        setValue (p, strumId (0, "on"), 1.0f);
        setValue (p, strumId (0, "ms"), 30.0f);
        setValue (p, strumId (1, "on"), 1.0f);
        setValue (p, strumId (1, "direction"), 1.0f);
        const auto c = chordOn (play (p, 0.6, 0.5), allCards);
        CHECK (c.notes == std::vector<int> { 60, 64, 67 });
        CHECK (c.spreadMs == Catch::Approx (30.0).margin (blockMs));
    }
    {
        // Only card C arpeggiated (A and B hold the chord): C strums alone.
        ShellProcessor p;
        setValue (p, "arp.enabled", 1.0f);
        setValue (p, "arp.pattern", static_cast<float> (chordPattern));
        setValue (p, "analog.arp.cardA", 0.0f);
        setValue (p, "analog.arp.cardB", 0.0f);
        setValue (p, strumId (0, "on"), 1.0f);
        setValue (p, strumId (0, "ms"), 40.0f);
        const auto played = play (p, 0.6, 0.5);
        const auto c = chordOn (played, cardC);
        CHECK (c.notes == std::vector<int> { 60, 64, 67 });
        CHECK (c.spreadMs == Catch::Approx (40.0).margin (blockMs));
        CHECK (chordOn (played, cardA | cardB).spreadMs == 0.0);   // the held chord, together
    }
}

TEST_CASE ("parity strum: switching STRUM, CHORD and LINK under held keys, the sustain pedal and a transport stop leave no note hanging", "[analog][arp][strum][parity]")
{
    ShellProcessor p;
    setValue (p, "arp.enabled", 1.0f);
    setValue (p, "arp.pattern", static_cast<float> (chordPattern));
    setValue (p, strumId (0, "on"), 1.0f);
    setValue (p, strumId (0, "ms"), 200.0f);   // (long: notes still waiting when things change)
    const std::vector<std::pair<juce::String, float>> changes {
        { strumId (0, "on"), 0.0f }, { strumId (0, "on"), 1.0f }, { "arp.pattern", 0.0f }, { "arp.pattern", static_cast<float> (chordPattern) },
        { strumId (0, "direction"), 2.0f }, { "analog.arp.link", 0.0f }, { strumId (1, "on"), 1.0f }, { patternId (1), static_cast<float> (chordPattern) },
        { "analog.arp.link", 1.0f }, { strumId (0, "sync"), 1.0f }, { "arp.enabled", 0.0f }, { "arp.enabled", 1.0f } };
    std::size_t next = 0;
    const auto played = play (p, 8.0, 3.6, { 48, 60, 64, 67 }, [&] (double t, juce::MidiBuffer& midi) {
        if (next < changes.size() && t >= 0.21 * static_cast<double> (next + 1))
        {
            setValue (p, changes[next].first, changes[next].second);
            ++next;
        }
        // The pedal: down while the keys are still held, up after they are released.
        if (std::abs (t - 3.0) < 0.5 * block / rate)
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
        if (std::abs (t - 4.0) < 0.5 * block / rate)
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0);
    });
    CHECK (next == changes.size());
    CHECK (played.heldAtEnd == 0);
    CHECK (played.activeAtEnd == 0);

    // A host's transport stop (all notes off) in the middle of a strum.
    ShellProcessor q;
    setValue (q, "arp.enabled", 1.0f);
    setValue (q, "arp.pattern", static_cast<float> (chordPattern));
    setValue (q, strumId (0, "on"), 1.0f);
    setValue (q, strumId (0, "ms"), 200.0f);
    const auto stopped = play (q, 5.0, 99.0, { 60, 64, 67 }, [&] (double t, juce::MidiBuffer& midi) {
        if (std::abs (t - 0.27) < 0.5 * block / rate)   // (just after the second step began)
            midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    });
    CHECK (stopped.heldAtEnd == 0);
}

TEST_CASE ("parity strum: every card's STRUM is recalled from a session; an older session opens with STRUM off", "[analog][arp][strum][parity]")
{
    ShellProcessor p;
    std::map<juce::String, float> wanted;
    for (int card = 0; card < 3; ++card)
    {
        wanted[strumId (card, "on")] = 1.0f;
        wanted[strumId (card, "direction")] = static_cast<float> (card + 1);
        wanted[strumId (card, "sync")] = card == 1 ? 1.0f : 0.0f;
        wanted[strumId (card, "ms")] = 35.0f + 10.0f * static_cast<float> (card);
        wanted[strumId (card, "division")] = static_cast<float> (2 + card);
    }
    for (const auto& [id, v] : wanted)
        setValue (p, id, v);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    {
        ShellProcessor q;
        q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        for (const auto& [id, v] : wanted)
        {
            INFO (id);
            CHECK (valueOf (q, id) == Catch::Approx (v).margin (0.01));
        }
    }
    // An older session (no STRUM parameter in it at all), opened in a new instance and over an
    // instance with STRUM on: STRUM off.
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (xml != nullptr);
    int removed = 0;
    std::function<void (juce::XmlElement&)> strip = [&] (juce::XmlElement& e) {
        for (int i = e.getNumChildElements(); --i >= 0;)
        {
            auto* child = e.getChildElement (i);
            if (child->getStringAttribute ("id").contains ("strum."))
            {
                e.removeChildElement (child, true);
                ++removed;
            }
            else
                strip (*child);
        }
    };
    strip (*xml);
    CHECK (removed == 15);
    juce::MemoryBlock older;
    juce::AudioProcessor::copyXmlToBinary (*xml, older);
    for (const bool fresh : { true, false })
    {
        INFO ((fresh ? "a new instance" : "over STRUM on"));
        ShellProcessor q;
        if (! fresh)
            for (const auto& [id, v] : wanted)
                setValue (q, id, v);
        q.setStateInformation (older.getData(), static_cast<int> (older.getSize()));
        for (int card = 0; card < 3; ++card)
            CHECK (valueOf (q, strumId (card, "on")) == 0.0f);
    }
}

TEST_CASE ("parity strum: the key sits beside SATELLITE in ANALOG's ARP panel; its light and panel act on the edited card", "[analog][arp][strum][strum-ui][parity]")
{
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*e);
    e->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
    ui.refreshNow();
    ui.arpControl().onToggleEditor();
    REQUIRE (ui.arpExpanded());
    e->setSize (1448, juce::roundToInt (ui.instrumentHeight()));   // (as a host grants the taller window)
    ui.refreshNow();
    auto& arp = ui.arpInlinePanel();
    auto* key = arp.strumKey();
    auto* satellite = arp.satelliteKey();
    REQUIRE (key != nullptr);
    REQUIRE (satellite != nullptr);
    CHECK (key->isVisible());
    // Right of SATELLITE on the title row, inside the panel, the same height; nothing else of
    // the panel's under it.
    CHECK (key->getX() > satellite->getRight());
    CHECK (arp.getLocalBounds().contains (key->getBounds()));
    CHECK (key->getY() == satellite->getY());
    CHECK (key->getHeight() == satellite->getHeight());
    for (auto* child : arp.getChildren())
        if (child != key && child->isVisible() && child->getWidth() > 0)
        {
            INFO (child->getName() << " " << child->getBounds().toString());
            CHECK_FALSE (child->getBounds().intersects (key->getBounds()));
        }

    // Off; UP: unavailable. The light switches card A's STRUM, never opens anything.
    CHECK_FALSE (key->anyOn());
    CHECK_FALSE (key->isAvailable());
    key->toggle();
    CHECK (valueOf (p, "arp.strum.on") == 1.0f);
    CHECK (ui.openPopupIndex() < 0);
    setValue (p, "arp.pattern", static_cast<float> (chordPattern));
    key->refresh();
    CHECK (key->isAvailable());
    key->toggle();
    CHECK (valueOf (p, "arp.strum.on") == 0.0f);

    // The name opens the shared panel; its card B page binds ANALOG's card B parameters.
    key->openPanel();
    ui.pressAnywhere (key);
    CHECK (ui.openPopupIndex() == andor::shell::ShellEditor::anchoredPopup);
    auto* panel = dynamic_cast<osp::plugin::StrumPanel*> (ui.openPopupPanel());
    REQUIRE (panel != nullptr);
    CHECK (panel->shownCard() == 0);
    CHECK (panel->showsCards());
    panel->showCard (1);
    panel->lamp().toggle();
    CHECK (valueOf (p, "analog.arp.cardB.strum.on") == 1.0f);
    CHECK (valueOf (p, "arp.strum.on") == 0.0f);
    // The panel stays inside the window.
    juce::Desktop::getInstance().getAnimator().cancelAllAnimations (true);   // (the opening's 110 ms fade, finished)
    // The panel's card stays inside the window: it is drawn at the popovers' compact scale
    // (MiniPanel's unit), so its visible size is frame::popoverSize, inside the shadow margin.
    {
        const auto size = osp::plugin::frame::popoverSize (*panel);
        const auto at = ui.getLocalArea (panel->getParentComponent(), panel->getBounds()).getPosition().toFloat();
        const float margin = static_cast<float> (osp::plugin::MiniPanel::shadowMargin) / panel->unit();
        INFO ("card " << at.x + margin << ", " << at.y + margin << " size " << size.x << " x " << size.y << " in " << ui.getLocalBounds().toString());
        CHECK (at.y + margin + size.y <= static_cast<float> (ui.getHeight()) + 1.0f);
        CHECK (at.x + margin >= 0.0f);
        CHECK (at.x + margin + size.x <= static_cast<float> (ui.getWidth()) + 1.0f);
    }
    key->openPanel();
    CHECK_FALSE (key->isOpen());

    // Unlinked: the light acts on the card being edited (C), not A.
    setValue (p, "analog.arp.link", 0.0f);
    arp.setEditCard (2);
    arp.refresh();
    CHECK (arp.strumCard() == 2);
    key->toggle();
    CHECK (valueOf (p, "analog.arp.cardC.strum.on") == 1.0f);
    CHECK (valueOf (p, "arp.strum.on") == 0.0f);
}
