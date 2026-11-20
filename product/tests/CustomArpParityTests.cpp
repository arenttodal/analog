// OSP parity R1, Gate 4 (docs/OSP_PARITY_AUDIT.md): the shared Custom ARP Patterns in ANDOR
// ANALOG, on ANALOG's own engine and cards. The pattern model, file, capture, editor and the
// one arpeggiator are OSP's (src/model/ArpPatternModel, src/io/ArpPatternJson, src/engine,
// shared/andor-ui/CustomPatternEditor); these tests prove each card's pattern reaches the card
// it belongs to (A OSC 1, B OSC 2, C the hybrid, C2 part of C), and that it is kept.

#include "AnalogEngine.h"
#include "AnalogParameters.h"
#include "CustomPatternEditor.h"
#include "ShellEditor.h"
#include "ShellProcessor.h"
#include "io/ArpPatternJson.h"
#include "model/ArpFactoryPatterns.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <functional>
#include <vector>

namespace
{
    using andor::analog::AnalogEngine;
    using andor::analog::Synth;
    using andor::shell::ShellProcessor;
    namespace params = andor::analog::params;

    constexpr double rate = 48000.0;
    constexpr int block = 64;
    constexpr std::uint8_t cardA = 1, cardB = 2, cardC = 4, allCards = 7;

    void setValue (ShellProcessor& p, const juce::String& id, float value)
    {
        auto* param = p.parameters.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    juce::String cardId (int card, const char* field)
    {
        return card == 0 ? juce::String ("arp.") + field
                         : juce::String ("analog.arp.card") + juce::String::charToString (static_cast<juce::juce_wchar> ('A' + card)) + "." + field;
    }

    /** A pattern on the held chord's ranks: `ranks` (0 = the lowest note; -1 a rest), each a
        step long (960 ticks), mapped by DEGREES (rank r plays the held note r, wrapping: the
        notes a test can name; CONTOUR, the default, fits the gesture's shape to the chord). */
    osp::arppattern::Document pattern (const char* name, std::vector<int> ranks)
    {
        osp::arppattern::Document d;
        d.id = osp::newArpPatternId();
        d.name = name;
        int top = 0;
        for (int r : ranks)
        {
            d.pattern.events.push_back ({ r < 0 ? -1 : r, r < 0, 960, 100 });
            top = std::max (top, r);
        }
        d.pattern.maxRank = std::max (1, top);
        d.pattern.mapping = osp::arp::CustomMapping::degrees;
        d.interpretation.mapping = osp::arp::CustomMapping::degrees;
        REQUIRE (d.pattern.valid());
        return d;
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
        int heldAtEnd = 0;
    };

    /** Holds C E G from the start until `release`, renders `seconds`; `each` before every block. */
    Played play (ShellProcessor& p, double seconds, double release, const std::function<void (double)>& each = {})
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
            if (each)
                each (t);
            juce::MidiBuffer midi;
            for (int note : { 60, 64, 67 })
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
            result.heldAtEnd += synth.voiceView (s).held ? 1 : 0;
        return result;
    }

    std::vector<int> notesOn (const Played& played, std::uint8_t cards, int count)
    {
        std::vector<int> notes;
        for (const auto& o : played.onsets)
            if (o.cards == cards && static_cast<int> (notes.size()) < count)
                notes.push_back (o.note);
        return notes;
    }

    /** Card `card` arpeggiated with its own custom pattern (CUSTOM on, the document set). */
    void useCustom (ShellProcessor& p, int card, const osp::arppattern::Document& d)
    {
        p.setCustomArpDocument (card, &d);
        setValue (p, cardId (card, "custom"), 1.0f);
    }
}

TEST_CASE ("parity custom arp: three parameters, off by default; a card's pattern plays on that card alone", "[analog][arp][arp-custom][parity]")
{
    {
        ShellProcessor p;
        for (int card = 0; card < 3; ++card)
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p.parameters.getParameter (cardId (card, "custom")));
            REQUIRE (ranged != nullptr);
            CHECK (ranged->getValue() == 0.0f);
            CHECK (ranged->getVersionHint() == 41);   // (after STRUM's 40: ANALOG's own reach 39)
            CHECK (p.customArpDocument (card) == nullptr);
        }
    }
    // Each card alone (unlinked): its own pattern on its own voices; the others play UP.
    const auto custom = pattern ("Top first", { 2, 0, 1, 0 });   // G C E C
    for (int card = 0; card < 3; ++card)
    {
        INFO ("card " << card);
        ShellProcessor p;
        setValue (p, "arp.enabled", 1.0f);
        setValue (p, "analog.arp.link", 0.0f);
        useCustom (p, card, custom);
        const auto played = play (p, 2.2, 2.0);
        const std::array<std::uint8_t, 3> bits { cardA, cardB, cardC };
        for (int other = 0; other < 3; ++other)
        {
            INFO ("plays on card " << other);
            if (other == card)
                CHECK (notesOn (played, bits[static_cast<std::size_t> (other)], 4) == std::vector<int> { 67, 60, 64, 60 });
            else
                CHECK (notesOn (played, bits[static_cast<std::size_t> (other)], 4) == std::vector<int> { 60, 64, 67, 60 });
        }
        CHECK (played.heldAtEnd == 0);
    }
}

TEST_CASE ("parity custom arp: linked, card A's pattern drives every card; unlinked, three patterns at once", "[analog][arp][arp-custom][cardarp][parity]")
{
    {
        ShellProcessor p;
        setValue (p, "arp.enabled", 1.0f);
        useCustom (p, 0, pattern ("Down skip", { 2, 0, 2, 1 }));
        // (B and C's own documents and CUSTOM are ignored while linked.)
        useCustom (p, 1, pattern ("Other", { 0, 0, 0, 0 }));
        const auto played = play (p, 2.2, 2.0);
        CHECK (notesOn (played, allCards, 4) == std::vector<int> { 67, 60, 67, 64 });
    }
    {
        ShellProcessor p;
        setValue (p, "arp.enabled", 1.0f);
        setValue (p, "analog.arp.link", 0.0f);
        useCustom (p, 0, pattern ("A", { 0, 1, 2, 1 }));
        useCustom (p, 1, pattern ("B", { 2, 2, 0, 0 }));
        useCustom (p, 2, pattern ("C", { 1, -1, 1, 2 }));   // (a rest: nothing starts on that step)
        const auto played = play (p, 2.2, 2.0);
        CHECK (notesOn (played, cardA, 4) == std::vector<int> { 60, 64, 67, 64 });
        CHECK (notesOn (played, cardB, 4) == std::vector<int> { 67, 67, 60, 60 });
        CHECK (notesOn (played, cardC, 3) == std::vector<int> { 64, 64, 67 });
        CHECK (played.heldAtEnd == 0);
    }
}

TEST_CASE ("parity custom arp: a pattern changed while playing, CUSTOM off, a card's arpeggiator off and C2 added leave no note hanging; C2 is part of C", "[analog][arp][arp-custom][hybrid2][parity]")
{
    ShellProcessor p;
    setValue (p, "arp.enabled", 1.0f);
    setValue (p, "analog.arp.link", 0.0f);
    const auto first = pattern ("First", { 0, 1, 2 }), second = pattern ("Second", { 2, 1, 0, -1 });
    useCustom (p, 2, first);
    int step = 0;
    const auto played = play (p, 4.0, 3.0, [&] (double t) {
        if (step == 0 && t >= 0.6) { p.setCustomArpDocument (2, &second); ++step; }
        else if (step == 1 && t >= 1.1) { setValue (p, params::hybrid2Enabled, 1.0f); ++step; }   // C2 added (part of card C)
        else if (step == 2 && t >= 1.6) { setValue (p, cardId (2, "custom"), 0.0f); ++step; }
        else if (step == 3 && t >= 2.0) { setValue (p, "analog.arp.cardC", 0.0f); ++step; }    // C holds the chord
        else if (step == 4 && t >= 2.4) { setValue (p, "analog.arp.cardC", 1.0f); setValue (p, cardId (2, "custom"), 1.0f); ++step; }
    });
    CHECK (step == 5);
    CHECK (played.heldAtEnd == 0);
    // C2 is not a fourth arpeggiator: no voice ever sounds on any card but A, B and C.
    for (const auto& o : played.onsets)
        CHECK ((o.cards & ~allCards) == 0);
    // C's own pattern played after the change (from the change on: 67 64 60).
    std::vector<int> afterChange;
    for (const auto& o : played.onsets)
        if (o.cards == cardC && o.seconds >= 0.75 && o.seconds < 1.1 && afterChange.size() < 2)
            afterChange.push_back (o.note);
    CHECK_FALSE (afterChange.empty());
    p.setCustomArpDocument (3, &first);   // (out of range: nothing, no crash)
    CHECK (p.customArpDocument (3) == nullptr);
}

TEST_CASE ("parity custom arp: kept in sessions and presets (a factory sound with a pattern), recalled onto the same cards, no library needed", "[analog][arp][arp-custom][state][parity]")
{
    const auto b = pattern ("B session", { 2, 0, 2, 0 }), c = pattern ("C session", { 1, 1, 2, 0 });
    juce::MemoryBlock session;
    juce::File preset;
    {
        ShellProcessor p;
        // A factory sound, then the patterns: as a musician makes a user preset of it.
        const auto list = p.presetList();
        REQUIRE_FALSE (list.empty());
        REQUIRE (p.loadPreset (list.front().file));
        setValue (p, "arp.enabled", 1.0f);
        setValue (p, "analog.arp.link", 0.0f);
        useCustom (p, 1, b);
        useCustom (p, 2, c);
        p.getStateInformation (session);
        preset = p.presetFolder().getChildFile ("Parity Custom ARP" + andor::shell::productInfo().presetExtension);
        REQUIRE (p.savePreset (preset));
    }
    auto check = [&] (ShellProcessor& q, const char* what) {
        INFO (what);
        REQUIRE (q.customArpDocument (1) != nullptr);
        REQUIRE (q.customArpDocument (2) != nullptr);
        CHECK (*q.customArpDocument (1) == b);
        CHECK (*q.customArpDocument (2) == c);
        CHECK (q.customArpDocument (0) == nullptr);
        const auto played = play (q, 2.2, 2.0);
        CHECK (notesOn (played, cardB, 4) == std::vector<int> { 67, 60, 67, 60 });
        CHECK (notesOn (played, cardC, 4) == std::vector<int> { 64, 64, 67, 60 });
    };
    {
        ShellProcessor q;   // a DAW session reopened
        q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
        check (q, "session");
    }
    {
        ShellProcessor q;   // the user preset
        REQUIRE (q.loadPreset (preset));
        check (q, "preset");
    }
    // A preset without patterns (another factory sound) clears them, as a whole sound does.
    {
        ShellProcessor q;
        REQUIRE (q.loadPreset (preset));
        REQUIRE (q.loadPreset (q.presetList().front().file));
        CHECK (q.customArpDocument (1) == nullptr);
        CHECK (q.customArpDocument (2) == nullptr);
    }
    preset.deleteFile();
}

TEST_CASE ("parity custom arp: one pattern library for OSP and ANALOG; a library file reads back as the same pattern in ANALOG", "[analog][arp][arp-custom][parity]")
{
    // The library is ANDOR's, not a product's: ~/Documents/ANDOR/Patterns for every product.
    const auto folder = osp::plugin::CustomPatternEditor::libraryFolder();
    CHECK (folder.getFileName() == "Patterns");
    CHECK (folder.getParentDirectory().getFileName() == "ANDOR");
    // A pattern written as ANDOR OSP writes one (the shared codec, .andorpattern schema 1),
    // read here and played on card B.
    juce::TemporaryFile file (".andorpattern");
    const auto d = osp::arppattern::factoryPatterns().front();
    std::string error;
    REQUIRE (osp::writeArpPattern (file.getFile().getFullPathName().toStdString(), d, error));
    const auto text = file.getFile().loadFileAsString();
    CHECK (text.contains ("andor.arp-pattern"));
    const auto back = osp::readArpPattern (file.getFile().getFullPathName().toStdString(), error);
    REQUIRE (back.has_value());
    CHECK (*back == d);
    ShellProcessor p;
    setValue (p, "arp.enabled", 1.0f);
    setValue (p, "analog.arp.link", 0.0f);
    useCustom (p, 1, *back);
    REQUIRE (p.customArpPattern (1) != nullptr);
    CHECK (*p.customArpPattern (1) == osp::arppattern::compile (back->pattern));
    const auto played = play (p, 2.2, 2.0);
    CHECK_FALSE (notesOn (played, cardB, 2).empty());
    CHECK (played.heldAtEnd == 0);
}


TEST_CASE ("parity custom arp: screenshots of the editor on card B and ANALOG's ARP panel with a custom row (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-parity-custom]")
{
    const auto* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    if (dir == nullptr)
        return;
    const juce::File out (juce::String::fromUTF8 (dir));
    out.createDirectory();
    auto shoot = [&out] (juce::Component& c, const juce::String& name) {
        juce::FileOutputStream stream (out.getChildFile (name));
        stream.setPosition (0);
        stream.truncate();
        juce::PNGImageFormat().writeImageToStream (c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f), stream);
    };
    using Editor = osp::plugin::CustomPatternEditor;
    ShellProcessor p;
    setValue (p, "arp.enabled", 1.0f);
    setValue (p, "analog.arp.link", 0.0f);
    {
        Editor editor (p, 1, true);   // (card B: OSC 2)
        editor.setBounds (0, 0, Editor::width + 2 * Editor::shadowMargin, Editor::height + 2 * Editor::shadowMargin);
        shoot (editor, "custom-1-editor-empty-card-b.png");
        std::vector<osp::arppattern::CapturedNote> take { { 0.0, 0.2, 60, 100 }, { 0.25, 0.2, 64, 100 }, { 0.5, 0.1, 67, 100 }, { 0.625, 0.1, 67, 100 },
                                                          { 0.75, 0.45, 72, 100 }, { 1.5, 0.2, 64, 100 }, { 1.75, 0.2, 62, 100 } };
        editor.useTake (take, 112.0);
        editor.nameField().setText ("Rise and fall", false);
        shoot (editor, "custom-2-editor-take-card-b.png");
        editor.perform (Editor::Action::use);
    }
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*e);
    e->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
    ui.arpControl().onToggleEditor();
    e->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
    ui.refreshNow();
    auto& arp = ui.arpInlinePanel();
    arp.setEditCard (1);
    arp.refresh();
    shoot (arp, "custom-3-arp-panel-card-b-custom.png");
    juce::FileOutputStream stream (out.getChildFile ("custom-4-full-arp-open.png"));
    stream.setPosition (0);
    stream.truncate();
    juce::PNGImageFormat().writeImageToStream (ui.createComponentSnapshot (ui.getLocalBounds(), true, 1.0f), stream);
}
