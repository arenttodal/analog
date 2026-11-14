// D ARP (the AUX arpeggiator follower): D in KONTAKT or NOISE plays one arpeggiator lane.
// Kontakt is not installed here: D's instrument is "ANDOR Test Instrument" (tests/surrogate),
// hosted through the same VST3 path; the MIDI it is given is logged with its samples.

#include "AnalogEngine.h"
#include "AnalogParameters.h"
#include "ExternalInstrument.h"
#include "ShellProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <map>
#include <vector>

namespace
{
    namespace params = andor::analog::params;
    using andor::analog::ExternalInstrument;
    using andor::shell::ShellProcessor;

    constexpr double rate = 48000.0;
    constexpr int block = 256;

    struct SurrogateOverride
    {
        SurrogateOverride()
        {
           #if JUCE_WINDOWS
            ::_putenv_s ("ANDOR_EXTERNAL_INSTRUMENT_VST3", ANDOR_TEST_INSTRUMENT_VST3);
           #else
            ::setenv ("ANDOR_EXTERNAL_INSTRUMENT_VST3", ANDOR_TEST_INSTRUMENT_VST3, 1);
           #endif
        }
        ~SurrogateOverride()
        {
           #if JUCE_WINDOWS
            ::_putenv_s ("ANDOR_EXTERNAL_INSTRUMENT_VST3", "");
           #else
            ::unsetenv ("ANDOR_EXTERNAL_INSTRUMENT_VST3");
           #endif
        }
    };

    void set (ShellProcessor& p, const juce::String& id, float value)
    {
        auto* param = p.parameters.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    float get (ShellProcessor& p, const juce::String& id)
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.parameters.getParameter (id));
        REQUIRE (param != nullptr);
        return param->convertFrom0to1 (param->getValue());
    }

    andor::analog::AnalogEngine& engineOf (ShellProcessor& p) { return dynamic_cast<andor::analog::AnalogEngine&> (p.productEngine()); }

    struct Key
    {
        double seconds;
        int note, velocity;   ///< 0: off
    };

    /** Plays `seconds` from the processor's current position (keys timed from now). */
    juce::AudioBuffer<float> play (ShellProcessor& p, double seconds, const std::vector<Key>& keys = {}, std::function<void (int)> eachBlock = {})
    {
        const int total = static_cast<int> (seconds * rate);
        juce::AudioBuffer<float> out (2, total), buffer (2, block);
        for (int pos = 0, b = 0; pos < total; pos += block, ++b)
        {
            if (eachBlock)
                eachBlock (b);
            juce::MidiBuffer midi;
            for (const auto& k : keys)
                if (const int at = static_cast<int> (std::lround (k.seconds * rate)); at >= pos && at < pos + block)
                    midi.addEvent (k.velocity > 0 ? juce::MidiMessage::noteOn (1, k.note, static_cast<juce::uint8> (k.velocity)) : juce::MidiMessage::noteOff (1, k.note), at - pos);
            p.processBlock (buffer, midi);
            const int n = std::min (block, total - pos);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, buffer, ch, 0, n);
        }
        return out;
    }

    double rmsOf (const juce::AudioBuffer<float>& a, double from, double to)
    {
        double s = 0.0;
        const int x = static_cast<int> (from * rate), z = std::min (a.getNumSamples(), static_cast<int> (to * rate));
        for (int i = x; i < z; ++i)
            s += 0.5 * (static_cast<double> (a.getSample (0, i)) * a.getSample (0, i) + static_cast<double> (a.getSample (1, i)) * a.getSample (1, i));
        return std::sqrt (s / std::max (1, z - x));
    }

    struct Seen
    {
        std::int64_t sample;
        int status, note, velocity;
        bool operator== (const Seen&) const = default;
    };

    /** What an instrument was given since `from` (sample since `start`). */
    std::vector<Seen> seenSince (const ExternalInstrument& host, int from, std::int64_t start, bool notesOnly = true)
    {
        std::vector<Seen> seen;
        for (int i = from; i < host.forwardedCount(); ++i)
        {
            const auto f = host.forwarded (i);
            const int status = f.status & 0xf0;
            if (notesOnly && status != 0x90 && status != 0x80)
                continue;
            seen.push_back ({ f.sample - start, status == 0x90 && f.data2 == 0 ? 0x80 : status, f.data1, status == 0x90 ? f.data2 : 0 });
        }
        return seen;
    }

    std::vector<Seen> noteOns (const std::vector<Seen>& seen)
    {
        std::vector<Seen> ons;
        for (const auto& s : seen)
            if (s.status == 0x90)
                ons.push_back (s);
        return ons;
    }

    /** Notes still on after everything the instrument was given (All Notes Off ends them all). */
    int hanging (const ExternalInstrument& host)
    {
        std::array<int, 128> on {};
        for (int i = std::max (0, host.forwardedCount() - ExternalInstrument::forwardedCapacity); i < host.forwardedCount(); ++i)
        {
            const auto f = host.forwarded (i);
            const int status = f.status & 0xf0;
            if (status == 0x90 && f.data2 > 0)
                ++on[f.data1];
            else if ((status == 0x80 || status == 0x90) && on[f.data1] > 0)
                --on[f.data1];
            else if (status == 0xb0 && (f.data1 == 123 || f.data1 == 120))
                on.fill (0);
        }
        int n = 0;
        for (const int c : on)
            n += c;
        return n;
    }

    void dry (ShellProcessor& p)
    {
        for (const char* id : { "space", "echo", "drive", "motion" })
            set (p, id, 0.0f);
    }

    /** The per-card arpeggiator: LINK off, A UP 1/16, B DOWN 1/8, C RANDOM 1/8 (cards' own
        arpeggios as given). */
    void cards (ShellProcessor& p, bool a, bool b, bool c, bool link = false)
    {
        set (p, "arp.enabled", 1.0f);
        set (p, "arp.pattern", 0.0f);   // UP
        set (p, "arp.rate", 2.0f);      // 1/16
        set (p, "arp.gate", 50.0f);
        set (p, "analog.arp.link", link ? 1.0f : 0.0f);
        set (p, "analog.arp.cardA", a ? 1.0f : 0.0f);
        set (p, "analog.arp.cardB", b ? 1.0f : 0.0f);
        set (p, "analog.arp.cardC", c ? 1.0f : 0.0f);
        set (p, "analog.arp.cardB.pattern", 1.0f);   // DOWN
        set (p, "analog.arp.cardB.rate", 1.0f);      // 1/8
        set (p, "analog.arp.cardC.pattern", 4.0f);   // RANDOM
        set (p, "analog.arp.cardC.rate", 1.0f);      // 1/8
    }

    /** D on in KONTAKT with the stand-in loaded; D ARP as given. */
    void kontaktD (ShellProcessor& p, bool arp, int follow)
    {
        set (p, params::auxEnabled, 1.0f);
        set (p, params::auxMode, 2.0f);   // KONTAKT
        REQUIRE (engineOf (p).auxInstrument().loadKontakt());
        set (p, params::auxArp, arp ? 1.0f : 0.0f);
        set (p, params::auxArpFollow, static_cast<float> (follow));
    }

    const std::vector<Key> chord { { 0.0, 48, 100 }, { 0.0, 55, 90 }, { 0.0, 60, 80 }, { 1.95, 48, 0 }, { 1.95, 55, 0 }, { 1.95, 60, 0 } };

    /** D's notes for the chord, from a processor set up by `setup`. */
    std::vector<Seen> dFor (const std::function<void (ShellProcessor&)>& setup)
    {
        ShellProcessor p;
        p.prepareToPlay (rate, block);
        dry (p);
        setup (p);
        play (p, 0.1);   // (the lanes are set up on the first blocks)
        auto& host = engineOf (p).auxInstrument();
        const int count = host.forwardedCount();
        const auto start = host.samplesProcessed();
        play (p, 2.5, chord);
        CHECK (hanging (host) == 0);
        return seenSince (host, count, start);
    }
}

TEST_CASE ("D ARP: Follow B - Kontakt gets B's pattern (DOWN, eighths) at its samples, whether B's own arpeggio is on or off", "[analog][aux][auxarp]")
{
    SurrogateOverride env;
    const auto bOn = dFor ([] (ShellProcessor& p) { cards (p, true, true, false); kontaktD (p, true, 1); });
    const auto bOff = dFor ([] (ShellProcessor& p) { cards (p, true, false, false); kontaktD (p, true, 1); });
    REQUIRE (bOn.size() >= 14);
    CHECK (bOn == bOff);   // the same notes, velocities and samples: B's lane, run or not
    const auto ons = noteOns (bOn);
    // DOWN: 60 55 48 60 55 48 ..., eighths at 120 BPM (0.25 s), the played velocities.
    const std::array<int, 3> down { 60, 55, 48 };
    const std::map<int, int> velocity { { 48, 100 }, { 55, 90 }, { 60, 80 } };
    for (std::size_t i = 0; i < ons.size(); ++i)
    {
        INFO ("note-on " << i << " at sample " << ons[i].sample);
        CHECK (ons[i].note == down[i % 3]);
        CHECK (ons[i].velocity == velocity.at (ons[i].note));
        if (i > 0)
            CHECK (ons[i].sample - ons[i - 1].sample == static_cast<std::int64_t> (0.25 * rate));
    }
    CHECK (ons.size() == 8);   // (eight eighths in the two seconds held)
}

TEST_CASE ("D ARP: Follow A (UP) and Follow C (RANDOM) - card C's own instrument and D get the very same lane", "[analog][aux][auxarp]")
{
    // Card C EXTERNAL hosts the stand-in too: what C's lane sends it is the reference, note for
    // note and sample for sample (C RANDOM unlinked; LINK with A and C: the shared pattern, A's).
    SurrogateOverride env;
    for (const bool link : { false, true })
    {
        DYNAMIC_SECTION ((link ? "LINK on (A + C): the shared pattern, FOLLOW B ignored" : "LINK off: Follow C RANDOM"))
        {
            ShellProcessor p;
            p.prepareToPlay (rate, block);
            dry (p);
            cards (p, link, false, true, link);
            set (p, params::hybridType, static_cast<float> (andor::analog::HybridType::external));
            REQUIRE (engineOf (p).externalInstrument().loadKontakt());
            kontaktD (p, true, link ? 1 : 2);
            play (p, 0.1);
            auto& c = engineOf (p).externalInstrument();
            auto& d = engineOf (p).auxInstrument();
            const int cCount = c.forwardedCount(), dCount = d.forwardedCount();
            const auto cStart = c.samplesProcessed(), dStart = d.samplesProcessed();
            play (p, 2.5, chord);
            const auto fromC = seenSince (c, cCount, cStart), fromD = seenSince (d, dCount, dStart);
            INFO ("C's lane: " << fromC.size() << " note events, D: " << fromD.size());
            REQUIRE (fromC.size() >= 14);
            CHECK (fromD == fromC);
            if (link)
            {
                // UP, sixteenths (A's settings): 48 55 60 48 ...
                const auto ons = noteOns (fromD);
                for (std::size_t i = 0; i < ons.size(); ++i)
                    CHECK (ons[i].note == std::array<int, 3> { 48, 55, 60 }[i % 3]);
                CHECK (ons.size() == 16);
            }
            CHECK (hanging (c) == 0);
            CHECK (hanging (d) == 0);
        }
    }
}

TEST_CASE ("D ARP: Follow A with LINK off plays A's UP sixteenths; LINK on follows the shared pattern whatever FOLLOW says", "[analog][aux][auxarp]")
{
    SurrogateOverride env;
    const auto followA = noteOns (dFor ([] (ShellProcessor& p) { cards (p, true, true, true); kontaktD (p, true, 0); }));
    REQUIRE (followA.size() == 16);
    for (std::size_t i = 0; i < followA.size(); ++i)
    {
        CHECK (followA[i].note == std::array<int, 3> { 48, 55, 60 }[i % 3]);
        if (i > 0)
            CHECK (followA[i].sample - followA[i - 1].sample == static_cast<std::int64_t> (0.125 * rate));
    }
    // LINK on (every card: the one arpeggiator): FOLLOW C is the shared pattern too - A's.
    const auto linked = noteOns (dFor ([] (ShellProcessor& p) { cards (p, true, true, true, true); kontaktD (p, true, 2); }));
    CHECK (linked == followA);
}

TEST_CASE ("D ARP: off, or the arpeggiator off - Kontakt gets the keys as played", "[analog][aux][auxarp]")
{
    SurrogateOverride env;
    auto keysOnly = [] (const std::vector<Seen>& seen) {
        const auto ons = noteOns (seen);
        REQUIRE (ons.size() == 3);
        for (const auto& s : ons)
            CHECK (s.sample == 0);
        CHECK (seen.size() == 6);
    };
    keysOnly (dFor ([] (ShellProcessor& p) { cards (p, true, true, true); kontaktD (p, false, 1); }));
    keysOnly (dFor ([] (ShellProcessor& p) { cards (p, true, true, true); set (p, "arp.enabled", 0.0f); kontaktD (p, true, 1); }));
}

TEST_CASE ("D ARP: A, B and C are given exactly what they were - the follower changes nothing of theirs", "[analog][aux][auxarp]")
{
    SurrogateOverride env;
    auto run = [] (bool follower) {
        ShellProcessor p;
        p.prepareToPlay (rate, block);
        dry (p);
        cards (p, true, true, true);
        set (p, params::hybridType, static_cast<float> (andor::analog::HybridType::external));
        REQUIRE (engineOf (p).externalInstrument().loadKontakt());
        kontaktD (p, follower, 1);
        set (p, params::auxMute, 1.0f);   // (D not heard: what is left is A, B and C)
        play (p, 0.1);
        auto& c = engineOf (p).externalInstrument();
        const int count = c.forwardedCount();
        const auto start = c.samplesProcessed();
        auto out = play (p, 2.5, chord);
        return std::make_pair (out, seenSince (c, count, start, false));
    };
    const auto [withOut, withC] = run (true);
    const auto [withoutOut, withoutC] = run (false);
    CHECK (withC == withoutC);
    REQUIRE (rmsOf (withOut, 0.2, 1.9) > 1.0e-4);
    bool same = true;
    for (int ch = 0; ch < 2 && same; ++ch)
        for (int i = 0; i < withOut.getNumSamples() && same; ++i)
            same = withOut.getSample (ch, i) == withoutOut.getSample (ch, i);
    CHECK (same);
}

TEST_CASE ("D ARP: keyed NOISE - each step of B's pattern opens D's gate", "[analog][aux][auxarp]")
{
    auto render = [] (bool arp) {
        ShellProcessor p;
        p.prepareToPlay (rate, block);
        dry (p);
        cards (p, true, false, false);
        set (p, params::auxEnabled, 1.0f);
        set (p, params::auxMode, 1.0f);   // NOISE, gated by its keys
        set (p, params::auxNoiseGate, 0.0f);
        set (p, params::auxNoiseAttack, 0.001f);
        set (p, params::auxNoiseRelease, 0.005f);
        set (p, params::auxSolo, 1.0f);
        set (p, params::auxArp, arp ? 1.0f : 0.0f);
        set (p, params::auxArpFollow, 1.0f);
        play (p, 0.1);
        return play (p, 2.5, chord);
    };
    const auto keys = render (false), stepped = render (true);
    // Eighths at half a step's gate: open for 0.125 s of every 0.25 s.
    std::vector<double> open, shut;
    for (int k = 1; k < 7; ++k)
    {
        open.push_back (rmsOf (stepped, 0.25 * k + 0.03, 0.25 * k + 0.10));
        shut.push_back (rmsOf (stepped, 0.25 * k + 0.16, 0.25 * k + 0.24));
    }
    for (std::size_t i = 0; i < open.size(); ++i)
    {
        INFO ("step " << i + 1 << ": open " << open[i] << ", shut " << shut[i]);
        CHECK (open[i] > 1.0e-3);
        CHECK (shut[i] < 0.2 * open[i]);
    }
    // The keys alone: one gate, open throughout.
    CHECK (rmsOf (keys, 0.41, 0.49) > 0.5 * rmsOf (keys, 0.28, 0.35));
    CHECK (rmsOf (stepped, 2.2, 2.5) < 1.0e-4);   // (released: shut)
}

TEST_CASE ("D ARP: FOLLOW, ARP, the arpeggiator, D's mode and power changed mid-chord, rapidly - no note is left hanging", "[analog][aux][auxarp]")
{
    SurrogateOverride env;
    ShellProcessor p;
    p.prepareToPlay (rate, block);
    dry (p);
    cards (p, true, false, true);
    kontaktD (p, true, 0);
    play (p, 0.1);
    auto& d = engineOf (p).auxInstrument();
    // Each change at its own block while the chord is held.
    const std::vector<std::pair<int, std::function<void()>>> changes {
        { 10, [&] { set (p, params::auxArpFollow, 1.0f); } },
        { 20, [&] { set (p, params::auxArpFollow, 2.0f); } },
        { 30, [&] { set (p, params::auxArp, 0.0f); } },
        { 40, [&] { set (p, params::auxArp, 1.0f); } },
        { 50, [&] { set (p, "arp.enabled", 0.0f); } },
        { 60, [&] { set (p, "arp.enabled", 1.0f); } },
        { 70, [&] { set (p, "analog.arp.link", 1.0f); } },
        { 80, [&] { set (p, "analog.arp.link", 0.0f); } },
        { 90, [&] { set (p, params::auxMode, 1.0f); } },    // NOISE
        { 100, [&] { set (p, params::auxMode, 2.0f); } },   // KONTAKT again
        { 110, [&] { set (p, params::auxEnabled, 0.0f); } },
        { 120, [&] { set (p, params::auxEnabled, 1.0f); } },
        { 130, [&] { set (p, "analog.arp.cardB", 1.0f); } },
    };
    std::vector<Key> held { { 0.0, 48, 100 }, { 0.0, 55, 90 }, { 0.0, 60, 80 } };
    play (p, 3.0, held, [&] (int b) {
        for (const auto& [at, change] : changes)
            if (at == b)
                change();
        // Then FOLLOW and ARP at every block for a while.
        if (b >= 160 && b < 260)
        {
            set (p, params::auxArpFollow, static_cast<float> (b % 3));
            if (b % 7 == 0)
                set (p, params::auxArp, (b / 7) % 2 == 0 ? 1.0f : 0.0f);
        }
    });
    set (p, params::auxArp, 1.0f);
    play (p, 1.0, { { 0.0, 48, 0 }, { 0.0, 55, 0 }, { 0.0, 60, 0 } });
    CHECK (hanging (d) == 0);
    // Kontakt heard alone, after its release: silent.
    set (p, params::auxSolo, 1.0f);
    const auto tail = play (p, 4.0);
    // The stand-in's strings and D's AMP release fade (a held note would not): falling, then gone.
    CHECK (rmsOf (tail, 1.0, 1.5) < 0.5 * rmsOf (tail, 0.0, 0.5));
    CHECK (rmsOf (tail, 3.5, 4.0) < 1.0e-6);
}

TEST_CASE ("D ARP: a state loaded mid-chord - the follower starts over, every note it played ends", "[analog][aux][auxarp]")
{
    SurrogateOverride env;
    ShellProcessor p;
    p.prepareToPlay (rate, block);
    dry (p);
    cards (p, true, true, false);
    kontaktD (p, true, 1);
    play (p, 0.1);
    auto& d = engineOf (p).auxInstrument();
    juce::MemoryBlock state;
    p.getStateInformation (state);
    play (p, 0.6, { { 0.0, 48, 100 }, { 0.0, 60, 100 } });
    p.setStateInformation (state.getData(), static_cast<int> (state.getSize()));   // (mid-chord)
    // At the load: everything D was given ends (All Notes Off), then B's lane goes on.
    const int before = d.forwardedCount();
    play (p, 0.6);
    bool ended = false;
    for (int i = before; i < d.forwardedCount(); ++i)
        ended = ended || ((d.forwarded (i).status & 0xf0) == 0xb0 && d.forwarded (i).data1 == 123);
    CHECK (ended);
    play (p, 0.5, { { 0.0, 48, 0 }, { 0.0, 60, 0 } });
    CHECK (hanging (d) == 0);
}

TEST_CASE ("D ARP: its two settings are kept with the session; an older session opens with D ARP off", "[analog][aux][auxarp]")
{
    juce::MemoryBlock state;
    {
        ShellProcessor p;
        set (p, params::auxArp, 1.0f);
        set (p, params::auxArpFollow, 2.0f);
        p.getStateInformation (state);
    }
    ShellProcessor q;
    q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (get (q, params::auxArp) == 1.0f);
    CHECK (get (q, params::auxArpFollow) == 2.0f);
    // A session saved before D ARP (its parameters removed): off, FOLLOW A.
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (xml != nullptr);
    int removed = 0;
    for (auto* e = xml->getFirstChildElement(); e != nullptr;)
    {
        auto* next = e->getNextElement();
        for (auto* c = e->getFirstChildElement(); c != nullptr;)
        {
            auto* n = c->getNextElement();
            if (c->getStringAttribute ("id").startsWith ("analog.aux.arp"))
            {
                e->removeChildElement (c, true);
                ++removed;
            }
            c = n;
        }
        if (e->getStringAttribute ("id").startsWith ("analog.aux.arp"))
        {
            xml->removeChildElement (e, true);
            ++removed;
        }
        e = next;
    }
    REQUIRE (removed == 2);
    juce::MemoryBlock older;
    juce::AudioProcessor::copyXmlToBinary (*xml, older);
    q.setStateInformation (older.getData(), static_cast<int> (older.getSize()));
    CHECK (get (q, params::auxArp) == 0.0f);
    CHECK (get (q, params::auxArpFollow) == 0.0f);
}

TEST_CASE ("D ARP: the demo renders (ANALOG_DEMO_DIR)", "[.][auxarp-demo]")
{
    // D's instrument here is the stand-in (Kontakt is not installed on this machine): the same
    // VST3 path, the same MIDI. Four bars at 120 BPM of a held C minor chord, A + B + C and D.
    const auto dir = juce::SystemStats::getEnvironmentVariable ("ANALOG_DEMO_DIR", {});
    REQUIRE (dir.isNotEmpty());
    const juce::File out (dir);
    out.createDirectory();
    SurrogateOverride env;
    const std::vector<Key> keys { { 0.0, 48, 100 }, { 0.0, 51, 92 }, { 0.0, 55, 96 }, { 0.0, 60, 88 },
                                  { 7.6, 48, 0 }, { 7.6, 51, 0 }, { 7.6, 55, 0 }, { 7.6, 60, 0 } };
    auto render = [&] (const juce::String& name, const std::function<void (ShellProcessor&)>& setup) {
        ShellProcessor p;
        p.prepareToPlay (rate, block);
        cards (p, true, false, false);
        setup (p);
        play (p, 0.1);
        const auto audio = play (p, 9.0, keys);
        const auto file = out.getChildFile (name + ".wav");
        file.deleteFile();
        std::unique_ptr<juce::AudioFormatWriter> writer (juce::WavAudioFormat().createWriterFor (new juce::FileOutputStream (file), rate, 2, 24, {}, 0));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
        CHECK (rmsOf (audio, 0.5, 7.0) > 1.0e-4);
    };
    // D alone (soloed): the keys, then B's DOWN eighths, then C's RANDOM eighths.
    render ("auxarp-01-d-solo-keys", [] (ShellProcessor& p) { kontaktD (p, false, 1); set (p, params::auxSolo, 1.0f); });
    render ("auxarp-02-d-solo-follow-b-down", [] (ShellProcessor& p) { kontaktD (p, true, 1); set (p, params::auxSolo, 1.0f); });
    render ("auxarp-03-d-solo-follow-c-random", [] (ShellProcessor& p) { kontaktD (p, true, 2); set (p, params::auxSolo, 1.0f); });
    // Everything: A's UP sixteenths on the cards (B and C hold the chord), D on B's pattern.
    render ("auxarp-04-mix-a-up-with-d-follow-b", [] (ShellProcessor& p) { kontaktD (p, true, 1); });
    render ("auxarp-05-mix-d-off", [] (ShellProcessor& p) { kontaktD (p, false, 1); });
}
