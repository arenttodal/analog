// EXPERIMENT: Kontakt as card C's EXTERNAL SoundSource (docs/experiments/KONTAKT_EXTERNAL_MORPH.md).
// Kontakt is not installed here: these host "ANDOR Test Instrument" (tests/surrogate), a small
// VST3 built for the purpose, through the same VST3 path. Kontakt itself: BLOCKED (see the doc).

#include "ExternalInstrument.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <cstdlib>
#include <vector>

using andor::analog::ExternalInstrument;

namespace
{
    constexpr double testRate = 48000.0;
    constexpr int testBlock = 256;

    /** Points the host's search at the surrogate (as the env override does for the demo). */
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

    struct Rendered
    {
        std::vector<float> left, right;
        double rms (double from, double to) const
        {
            double s = 0.0;
            const auto a = static_cast<std::size_t> (from * testRate), z = std::min (left.size(), static_cast<std::size_t> (to * testRate));
            for (auto i = a; i < z; ++i)
                s += 0.5 * (static_cast<double> (left[i]) * left[i] + static_cast<double> (right[i]) * right[i]);
            return std::sqrt (s / static_cast<double> (std::max<std::size_t> (1, z - a)));
        }
    };

    /** Renders `seconds`, the events at their samples (in blocks of testBlock). */
    Rendered run (ExternalInstrument& host, double seconds, std::vector<std::pair<int, juce::MidiMessage>> events = {}, bool active = true,
                  std::function<void (int block)> eachBlock = {})
    {
        Rendered out;
        const int total = static_cast<int> (seconds * testRate);
        out.left.resize (static_cast<std::size_t> (total));
        out.right.resize (static_cast<std::size_t> (total));
        juce::MidiBuffer midi;
        ExternalInstrument::Transport transport;
        for (int pos = 0, b = 0; pos < total; pos += testBlock, ++b)
        {
            if (eachBlock)
                eachBlock (b);
            const int n = std::min (testBlock, total - pos);
            midi.clear();
            for (const auto& [at, m] : events)
                if (at >= pos && at < pos + n)
                    midi.addEvent (m, at - pos);
            host.process (midi, out.left.data() + pos, out.right.data() + pos, n, transport, active);
            transport.timeInSamples += n;
        }
        return out;
    }

    bool haveDisplay()
    {
       #if JUCE_LINUX
        const char* d = std::getenv ("DISPLAY");
        return d != nullptr && *d != 0;
       #else
        return true;
       #endif
    }
}

TEST_CASE ("EXTERNAL G1: the host finds the instrument (the override; Kontakt where installed) and loads it", "[external][g1]")
{
    SurrogateOverride env;
    ExternalInstrument host;
    host.prepare (testRate, testBlock);
    juce::PluginDescription found;
    juce::String why;
    REQUIRE (host.locate (found, why));
    CHECK (found.isInstrument);
    CHECK (found.name == "ANDOR Test Instrument");
    CHECK (found.pluginFormatName == "VST3");
    REQUIRE (host.loadKontakt());
    CHECK (host.state() == ExternalInstrument::State::ready);
    CHECK (host.displayName() == "ANDOR Test Instrument");
    CHECK (host.latencySamples() == 0);
}

TEST_CASE ("EXTERNAL G1: Kontakt not installed - an actionable message, nothing loaded, no crash", "[external][g1]")
{
    ExternalInstrument host;
    host.prepare (testRate, testBlock);
    juce::PluginDescription found;
    juce::String why;
    const bool here = host.locate (found, why);   // (this machine: no Kontakt)
    if (here)
        SKIP ("Kontakt is installed here: " << found.name);
    CHECK (why.contains ("Kontakt"));
    CHECK_FALSE (host.loadKontakt());
    CHECK (host.state() == ExternalInstrument::State::notInstalled);
    const auto out = run (host, 0.2, { { 0, juce::MidiMessage::noteOn (1, 60, 0.8f) } });
    CHECK (out.rms (0.0, 0.2) == 0.0);
}

TEST_CASE ("EXTERNAL G1: a held note sounds in stereo; its release tail rings after the note-off with no note held", "[external][g1]")
{
    SurrogateOverride env;
    ExternalInstrument host;
    host.prepare (testRate, testBlock);
    REQUIRE (host.loadKontakt());
    const int off = static_cast<int> (1.0 * testRate);
    const auto out = run (host, 2.0, { { 0, juce::MidiMessage::noteOn (1, 57, 0.9f) }, { off, juce::MidiMessage::noteOff (1, 57) } });
    const double held = out.rms (0.5, 1.0), tail = out.rms (1.05, 1.25), gone = out.rms (1.8, 2.0);
    INFO ("held " << held << ", tail " << tail << ", 0.8 s after " << gone);
    CHECK (held > 0.01);
    CHECK (tail > 0.1 * held);   // (still ringing: processed with no note held)
    CHECK (gone < 0.1 * held);
    // Stereo: both channels, not one copied.
    double l = 0.0, r = 0.0, diff = 0.0;
    for (std::size_t i = static_cast<std::size_t> (0.5 * testRate); i < static_cast<std::size_t> (testRate); ++i)
    {
        l += std::abs (out.left[i]);
        r += std::abs (out.right[i]);
        diff += std::abs (out.left[i] - out.right[i]);
    }
    CHECK (l > 0.0);
    CHECK (r > 0.0);
    CHECK (diff > 0.05 * l);
    CHECK (host.sounding());
}

TEST_CASE ("EXTERNAL G1: its editor opens and closes; closing never stops the sound or unloads it", "[external][g1]")
{
    // (Its window stays off the desktop here: this container's X server has no window manager.
    // The editor itself - made by the hosted instrument, closed, made again - is the real one.)
    SurrogateOverride env;
    ExternalInstrument host;
    host.setEditorsOnDesktop (haveDisplay() && std::getenv ("ANDOR_TEST_DESKTOP_WINDOWS") != nullptr);
    host.prepare (testRate, testBlock);
    REQUIRE (host.loadKontakt());
    const auto before = run (host, 0.6, { { 0, juce::MidiMessage::noteOn (1, 60, 0.9f) } });
    host.openEditor();
    REQUIRE (host.editorOpen());
    CHECK (host.editorComponent() != nullptr);
    const auto during = run (host, 0.3);
    host.closeEditor();
    CHECK_FALSE (host.editorOpen());
    CHECK (host.state() == ExternalInstrument::State::ready);
    const auto after = run (host, 0.3);
    CHECK (during.rms (0.0, 0.3) > 0.5 * before.rms (0.3, 0.6));
    CHECK (after.rms (0.0, 0.3) > 0.5 * before.rms (0.3, 0.6));   // (the held note still sounding)
    host.openEditor();   // (and again)
    CHECK (host.editorOpen());
    host.unload();
    CHECK_FALSE (host.editorOpen());
}

TEST_CASE ("EXTERNAL G1: unload mid-note - its notes end, it fades out in 10 ms, it is freed; no crash", "[external][g1]")
{
    SurrogateOverride env;
    ExternalInstrument host;
    host.prepare (testRate, testBlock);
    REQUIRE (host.loadKontakt());
    bool unloaded = false;
    const int unloadBlock = static_cast<int> (0.5 * testRate) / testBlock;
    const auto out = run (host, 1.0, { { 0, juce::MidiMessage::noteOn (1, 60, 0.9f) } }, true, [&] (int b) {
        if (b == unloadBlock && ! unloaded)
        {
            host.unload();
            unloaded = true;
        }
    });
    const double at = static_cast<double> (unloadBlock * testBlock) / testRate;
    CHECK (out.rms (at - 0.2, at) > 0.01);
    CHECK (out.rms (at + 0.02, 1.0) == 0.0);   // (faded and gone: not processed any more)
    CHECK (host.state() == ExternalInstrument::State::empty);
    host.collectGarbage();
    // Inactive (card C another type): the same fade, then nothing processed.
    REQUIRE (host.loadKontakt());
    run (host, 0.3, { { 0, juce::MidiMessage::noteOn (1, 64, 0.9f) } });
    const auto inactive = run (host, 0.3, {}, false);
    CHECK (inactive.rms (0.02, 0.3) == 0.0);
}

//==============================================================================
// Gates 2-3: through the whole instrument (card C EXTERNAL, its lane, its bus)

#include "AnalogEngine.h"
#include "AnalogParameters.h"
#include "ShellProcessor.h"

namespace
{
    namespace params = andor::analog::params;
    using andor::shell::ShellProcessor;

    void set (ShellProcessor& p, const juce::String& id, float value)
    {
        auto* param = p.parameters.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    andor::analog::AnalogEngine& engineOf (ShellProcessor& p) { return dynamic_cast<andor::analog::AnalogEngine&> (p.productEngine()); }

    struct Key
    {
        double seconds;
        int note, velocity;   ///< 0: off
    };

    juce::AudioBuffer<float> play (ShellProcessor& p, double seconds, const std::vector<Key>& keys, std::function<void (int block)> eachBlock = {})
    {
        const int total = static_cast<int> (seconds * testRate);
        juce::AudioBuffer<float> out (2, total), buffer (2, testBlock);
        for (int pos = 0, b = 0; pos < total; pos += testBlock, ++b)
        {
            if (eachBlock)
                eachBlock (b);
            juce::MidiBuffer midi;
            for (const auto& k : keys)
                if (const int at = static_cast<int> (std::lround (k.seconds * testRate)); at >= pos && at < pos + testBlock)
                    midi.addEvent (k.velocity > 0 ? juce::MidiMessage::noteOn (1, k.note, static_cast<juce::uint8> (k.velocity)) : juce::MidiMessage::noteOff (1, k.note),
                                   at - pos);
            p.processBlock (buffer, midi);
            const int n = std::min (testBlock, total - pos);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, buffer, ch, 0, n);
        }
        return out;
    }

    double rmsOf (const juce::AudioBuffer<float>& a, double from, double to)
    {
        double s = 0.0;
        const int x = static_cast<int> (from * testRate), z = std::min (a.getNumSamples(), static_cast<int> (to * testRate));
        for (int i = x; i < z; ++i)
            s += 0.5 * (static_cast<double> (a.getSample (0, i)) * a.getSample (0, i) + static_cast<double> (a.getSample (1, i)) * a.getSample (1, i));
        return std::sqrt (s / std::max (1, z - x));
    }

    bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
                if (a.getSample (ch, i) != b.getSample (ch, i))
                    return false;
        return true;
    }

    /** The effects out of the way (the measurements are the buses'). */
    void dry (ShellProcessor& p)
    {
        for (const char* id : { "space", "echo", "drive", "motion" })
            set (p, id, 0.0f);
    }

    /** Card C EXTERNAL with the stand-in loaded (LEGATO STRINGS, its default). */
    void withExternal (ShellProcessor& p, bool abSilent = false)
    {
        p.prepareToPlay (testRate, testBlock);
        dry (p);
        if (abSilent)
        {
            set (p, params::osc1Level, 0.0f);
            set (p, params::osc2Level, 0.0f);
        }
        set (p, params::hybridType, static_cast<float> (andor::analog::HybridType::external));
        REQUIRE (engineOf (p).externalInstrument().loadKontakt());
    }

    struct Seen
    {
        double seconds;
        int status, note, velocity;
    };

    std::vector<Seen> forwardedSince (const ExternalInstrument& host, int fromCount, std::int64_t fromSample)
    {
        std::vector<Seen> seen;
        for (int i = fromCount; i < host.forwardedCount(); ++i)
        {
            const auto f = host.forwarded (i);
            seen.push_back ({ static_cast<double> (f.sample - fromSample) / testRate, f.status, f.data1, f.data2 });
        }
        return seen;
    }
}

TEST_CASE ("EXTERNAL G2: card C's lane - the fixed overlap (C3 0, D3 450, C3 off 600, D3 off 1100 ms) reaches the instrument intact", "[external][g2]")
{
    SurrogateOverride env;
    ShellProcessor p;
    withExternal (p, true);
    auto& host = engineOf (p).externalInstrument();
    const int count = host.forwardedCount();
    const auto start = host.samplesProcessed();
    const std::vector<Key> overlap { { 0.0, 48, 100 }, { 0.45, 50, 90 }, { 0.6, 48, 0 }, { 1.1, 50, 0 } };
    const auto legato = play (p, 1.8, overlap);
    const auto seen = forwardedSince (host, count, start);
    REQUIRE (seen.size() == 4);
    // Order, notes, velocities, channel 1, the very samples they were played on.
    const std::array<Seen, 4> want { { { 0.0, 0x90, 48, 100 }, { 0.45, 0x90, 50, 90 }, { 0.6, 0x80, 48, 0 }, { 1.1, 0x80, 50, 0 } } };
    for (std::size_t i = 0; i < 4; ++i)
    {
        INFO ("event " << i << " at " << seen[i].seconds << " s, status " << seen[i].status << ", note " << seen[i].note);
        CHECK (seen[i].seconds == Catch::Approx (want[i].seconds).margin (0.5 / testRate));
        CHECK (seen[i].status == want[i].status);
        CHECK (seen[i].note == want[i].note);
        if (want[i].status == 0x90)
            CHECK (seen[i].velocity == want[i].velocity);
    }
    // Audibly legato: the stand-in's legato strings glide on the overlap (no new bow); the
    // same two notes with a gap (C3 off at 400 ms) start a new bow from silence.
    ShellProcessor q;
    withExternal (q, true);
    const auto gapped = play (q, 1.8, { { 0.0, 48, 100 }, { 0.4, 48, 0 }, { 0.45, 50, 90 }, { 1.1, 50, 0 } });
    const double before = rmsOf (legato, 0.30, 0.40);
    const double overlapAt = rmsOf (legato, 0.455, 0.475), gapAt = rmsOf (gapped, 0.455, 0.475);
    INFO ("at the second note: legato " << overlapAt / before << ", gapped " << gapAt / before << " of the first note's level");
    CHECK (overlapAt > 0.5 * before);   // (one continuous bow: no dip)
    CHECK (gapAt < 0.2 * before);       // (a new attack from silence)
    CHECK (overlapAt > 4.0 * gapAt);
    CHECK (rmsOf (legato, 1.15, 1.3) > 0.05 * before);   // (its release tail after the last note-off)
}

TEST_CASE ("EXTERNAL G2: with C's arpeggiator on only C's arpeggio reaches the instrument; A's arpeggio never does", "[external][g2]")
{
    SurrogateOverride env;
    const std::vector<Key> chord { { 0.0, 48, 100 }, { 0.0, 55, 100 }, { 0.0, 60, 100 }, { 1.95, 48, 0 }, { 1.95, 55, 0 }, { 1.95, 60, 0 } };
    auto noteOns = [] (const std::vector<Seen>& seen) {
        int n = 0;
        for (const auto& s : seen)
            n += s.status == 0x90 && s.velocity > 0 ? 1 : 0;
        return n;
    };
    auto setup = [] (ShellProcessor& p, bool a, bool c) {
        withExternal (p);
        set (p, "arp.enabled", 1.0f);
        set (p, "arp.rate", 2.0f);   // 1/16
        set (p, "analog.arp.link", 0.0f);
        set (p, "analog.arp.cardA", a ? 1.0f : 0.0f);
        set (p, "analog.arp.cardB", 0.0f);
        set (p, "analog.arp.cardC", c ? 1.0f : 0.0f);
        set (p, "analog.arp.cardC.rate", 1.0f);   // 1/8
    };
    {
        // C arpeggiates (eighths at 120 BPM: 4 a second), A and B hold the chord.
        ShellProcessor p;
        setup (p, false, true);
        play (p, 0.1, {});   // (the shell sets its lanes up on its first blocks)
        auto& host = engineOf (p).externalInstrument();
        const int count = host.forwardedCount();
        const auto start = host.samplesProcessed();
        const auto out = play (p, 2.0, chord);
        const auto seen = forwardedSince (host, count, start);
        INFO ("C arpeggiating: " << noteOns (seen) << " note-ons in 2 s");
        CHECK (noteOns (seen) >= 7);
        CHECK (noteOns (seen) <= 9);
        // Its notes in sequence on C's own grid (eighths, 0.25 s apart) - never the chord the
        // keys hold, never A's sixteenths.
        std::vector<double> ons;
        for (const auto& s : seen)
            if (s.status == 0x90 && s.velocity > 0)
                ons.push_back (s.seconds);
        for (std::size_t i = 1; i < ons.size(); ++i)
            CHECK (ons[i] - ons[i - 1] == Catch::Approx (0.25).margin (0.002));
        CHECK (rmsOf (out, 0.5, 1.9) > 0.0);
    }
    {
        // A arpeggiates (sixteenths), C holds: the instrument gets the held chord once.
        ShellProcessor p;
        setup (p, true, false);
        play (p, 0.1, {});
        auto& host = engineOf (p).externalInstrument();
        const int count = host.forwardedCount();
        const auto start = host.samplesProcessed();
        play (p, 2.0, chord);
        const auto seen = forwardedSince (host, count, start);
        CHECK (noteOns (seen) == 3);
    }
}

TEST_CASE ("EXTERNAL G3: EXTERNAL not chosen - the instrument as before, bit for bit (an instrument loaded or not)", "[external][g3]")
{
    SurrogateOverride env;
    const std::vector<Key> phrase { { 0.0, 48, 100 }, { 0.0, 55, 90 }, { 0.3, 64, 80 }, { 0.8, 48, 0 }, { 0.8, 55, 0 }, { 0.9, 64, 0 } };
    ShellProcessor plain;
    plain.prepareToPlay (testRate, testBlock);
    set (plain, params::hybridType, 1.0f);   // OSC 3
    play (plain, 0.1, {});   // (the same history as the other)
    const auto before = play (plain, 1.5, phrase);
    ShellProcessor loaded;
    withExternal (loaded);
    for (const char* id : { "space", "echo", "drive", "motion" })   // (as the plain one: defaults)
        set (loaded, id, plain.parameters.getParameter (id)->convertFrom0to1 (plain.parameters.getParameter (id)->getValue()));
    set (loaded, params::hybridType, 1.0f);   // (loaded, but card C plays OSC 3)
    play (loaded, 0.1, {});   // (its fade out, from a block with nothing playing)
    const auto after = play (loaded, 1.5, phrase);
    CHECK (identical (before, after));
}

TEST_CASE ("EXTERNAL G3: MORPH off - C on its own bus at card C's level, once (not per voice); MUTE silences it; release tails ring", "[external][g3]")
{
    SurrogateOverride env;
    // Twins, the same history: one at card C's 0 dB, one at -6 dB (A and B silent: what is
    // heard is C).
    ShellProcessor p, q6;
    withExternal (p, true);
    withExternal (q6, true);
    set (q6, "layerB.level", -6.0f);
    const std::vector<Key> note { { 0.0, 57, 100 }, { 1.0, 57, 0 } };
    const auto atZero = play (p, 1.6, note);
    const auto atMinus6 = play (q6, 1.6, note);
    const double held0 = rmsOf (atZero, 0.4, 0.9), held6 = rmsOf (atMinus6, 0.4, 0.9);
    INFO ("0 dB " << held0 << ", -6 dB " << held6);
    CHECK (held0 > 0.005);
    CHECK (held6 / held0 == Catch::Approx (0.501).margin (0.005));
    CHECK (rmsOf (atZero, 1.05, 1.25) > 0.1 * held0);   // (its tail after the note-off)
    set (p, "layerB.mute", 1.0f);
    CHECK (rmsOf (play (p, 1.0, note), 0.1, 0.9) < 1.0e-5 * std::max (1.0, held0 / 0.05));
    set (p, "layerB.mute", 0.0f);
    // A chord of 16 voices on A + B: C's level is its instrument's, never times the voices -
    // each key reaches it once.
    ShellProcessor q;
    withExternal (q, true);
    set (q, params::hybridType, static_cast<float> (andor::analog::HybridType::external));
    auto& host = engineOf (q).externalInstrument();
    const int count = host.forwardedCount();
    play (q, 0.5, { { 0.0, 48, 100 }, { 0.0, 52, 100 }, { 0.0, 55, 100 }, { 0.0, 59, 100 } });
    int ons = 0;
    for (int i = count; i < host.forwardedCount(); ++i)
        ons += host.forwarded (i).status == 0x90 ? 1 : 0;
    CHECK (ons == 4);
}

TEST_CASE ("EXTERNAL G3: C2 is neutral; switching card C away mid-note fades C out with no stuck note", "[external][g3]")
{
    SurrogateOverride env;
    // Card C's second layer on (an oscillator) in one of two twins: nothing of it sounds in
    // EXTERNAL - the two are the same, sample for sample.
    ShellProcessor p, c2;
    withExternal (p, true);
    withExternal (c2, true);
    set (c2, params::hybrid2Enabled, 1.0f);
    const std::vector<Key> held { { 0.0, 60, 100 } };
    const auto alone = play (p, 0.6, held);
    const auto withC2 = play (c2, 0.6, held);
    CHECK (identical (alone, withC2));
    // Card C to OSC 3 while the note is held: C fades out (10 ms) - the instrument's notes end.
    set (p, params::hybrid2Enabled, 0.0f);
    set (p, params::osc1Level, 0.0f);
    set (p, params::hybridType, 1.0f);
    set (p, "layerB.level", -48.0f);   // (OSC 3 silent: only C's leftovers would be heard)
    const auto away = play (p, 0.3, {});
    CHECK (rmsOf (away, 0.03, 0.3) < 0.01 * rmsOf (alone, 0.2, 0.6));   // (C gone: -40 dB and below)
    // Back to EXTERNAL with no key down: nothing is left sounding in the instrument.
    set (p, "layerB.level", 0.0f);
    set (p, params::hybridType, static_cast<float> (andor::analog::HybridType::external));
    const auto back = play (p, 0.5, {});
    CHECK (rmsOf (back, 0.1, 0.5) < 0.01 * rmsOf (alone, 0.2, 0.6));   // (no note left, no frozen tail resumed)
}

//==============================================================================
// Gate 4: GLOBAL MORPH through the instrument - the listening renders (ANALOG_DEMO_DIR) and the
// measurements beside them. The stand-in plays card C (Kontakt itself: BLOCKED here).

namespace
{
    std::shared_ptr<juce::MemoryBlock> surrogateState (int program, int latency = 0)
    {
        juce::XmlElement xml ("AndorTestInstrument");
        xml.setAttribute ("program", program);
        xml.setAttribute ("latency", latency);
        auto block = std::make_shared<juce::MemoryBlock>();
        juce::AudioProcessor::copyXmlToBinary (xml, *block);
        return block;
    }

    /** Card C EXTERNAL with the stand-in's instrument `program` (0 LEGATO STRINGS, 1 EVOLVING PAD). */
    void withSurrogateProgram (ShellProcessor& p, int program)
    {
        p.prepareToPlay (testRate, testBlock);
        set (p, params::hybridType, static_cast<float> (andor::analog::HybridType::external));
        auto& host = engineOf (p).externalInstrument();
        juce::PluginDescription d;
        juce::String why;
        REQUIRE (host.locate (d, why));
        REQUIRE (host.load (d, surrogateState (program).get()));
    }

    /** Legato chord changes (each next chord 60 ms before the last ends), the top voice last. */
    std::vector<Key> chordPhrase (double chordSeconds = 2.0)
    {
        const std::vector<std::vector<int>> chords { { 48, 55, 59, 64 }, { 45, 52, 55, 60 }, { 41, 48, 52, 57 }, { 43, 50, 53, 62 } };
        std::vector<Key> keys;
        for (std::size_t c = 0; c < chords.size(); ++c)
        {
            const double on = static_cast<double> (c) * chordSeconds, off = on + chordSeconds + 0.06;
            for (const int k : chords[c])
                keys.push_back ({ on, k, 96 });
            for (const int k : chords[c])
                keys.push_back ({ c + 1 < chords.size() ? off : on + chordSeconds, k, 0 });
        }
        std::sort (keys.begin(), keys.end(), [] (const Key& a, const Key& b) { return a.seconds < b.seconds || (a.seconds == b.seconds && a.velocity < b.velocity); });
        return keys;
    }

    void writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (testRate).withNumChannels (2).withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        REQUIRE (writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()));
    }

    /** Third-octave levels (dB, 100 Hz .. 8 kHz) of the mono sum over a window. */
    std::vector<double> bandProfile (const juce::AudioBuffer<float>& a, double from, double to)
    {
        std::vector<double> out;
        for (int k = 0; k < 20; ++k)
        {
            const double hz = 100.0 * std::pow (2.0, k / 3.0);
            const double w = 2.0 * juce::MathConstants<double>::pi * hz / testRate, alpha = std::sin (w) / (2.0 * 4.3), a0 = 1.0 + alpha;
            const double b0 = alpha / a0, b2 = -alpha / a0, a1 = -2.0 * std::cos (w) / a0, a2 = (1.0 - alpha) / a0;
            double z1 = 0, z2 = 0, y1 = 0, y2 = 0, energy = 0.0;
            for (int i = 0; i < std::min (a.getNumSamples(), static_cast<int> (to * testRate)); ++i)
            {
                const double x = 0.5 * (a.getSample (0, i) + a.getSample (1, i));
                const double y = b0 * x + z1;
                z1 = -a1 * y + z2;
                z2 = b2 * x - a2 * y;
                const double u = b0 * y + y1;
                y1 = -a1 * u + y2;
                y2 = b2 * y - a2 * u;
                if (i >= static_cast<int> (from * testRate))
                    energy += u * u;
            }
            out.push_back (10.0 * std::log10 (energy + 1.0e-20));
        }
        return out;
    }

    double shapeDistance (const std::vector<double>& a, const std::vector<double>& b)
    {
        double ma = 0.0, mb = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            ma += a[i] / static_cast<double> (a.size());
            mb += b[i] / static_cast<double> (b.size());
        }
        double d = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i)
            d += std::pow ((a[i] - ma) - (b[i] - mb), 2.0);
        return std::sqrt (d / static_cast<double> (a.size()));
    }
}

TEST_CASE ("EXTERNAL G4: GLOBAL MORPH at 50 % through the instrument - C's spectral envelope on A + B, not their crossfade", "[external][g4]")
{
    SurrogateOverride env;
    auto render = [] (bool morph, float position, bool abOn, bool cOn) {
        ShellProcessor p;
        withSurrogateProgram (p, 1);   // (EVOLVING PAD)
        dry (p);
        if (! abOn)
        {
            set (p, params::osc1Level, 0.0f);
            set (p, params::osc2Level, 0.0f);
        }
        if (! cOn)
            set (p, "layerB.mute", 1.0f);
        set (p, params::morphEnabled, morph ? 1.0f : 0.0f);
        set (p, params::morphPosition, position);
        return play (p, 3.0, { { 0.0, 48, 100 }, { 0.0, 55, 100 }, { 0.0, 64, 100 } });
    };
    const auto ab = render (false, 0.0f, true, false), c = render (false, 0.0f, false, true);
    const auto at0 = render (true, 0.0f, true, true), at50 = render (true, 50.0f, true, true), at100 = render (true, 100.0f, true, true);
    // The endpoints: A + B alone and C alone (MORPH 0 and 100).
    CHECK (rmsOf (at0, 1.0, 3.0) == Catch::Approx (rmsOf (ab, 1.0, 3.0)).epsilon (0.01));
    CHECK (rmsOf (at100, 1.0, 3.0) == Catch::Approx (rmsOf (c, 1.0, 3.0)).epsilon (0.01));
    // The middle: its spectral envelope moved to C's - and away from the dry crossfade.
    juce::AudioBuffer<float> mix (2, ab.getNumSamples());
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < mix.getNumSamples(); ++i)
            mix.setSample (ch, i, std::sqrt (0.5f) * (ab.getSample (ch, i) + c.getSample (ch, i)));
    const auto pAb = bandProfile (ab, 1.0, 3.0), pC = bandProfile (c, 1.0, 3.0), pHalf = bandProfile (at50, 1.0, 3.0), pMix = bandProfile (mix, 1.0, 3.0);
    INFO ("shape distance (dB): 50 % to C " << shapeDistance (pHalf, pC) << ", to A+B " << shapeDistance (pHalf, pAb) << ", to the crossfade "
                                             << shapeDistance (pHalf, pMix) << "; the crossfade to C " << shapeDistance (pMix, pC));
    CHECK (shapeDistance (pHalf, pC) < shapeDistance (pHalf, pAb));
    CHECK (shapeDistance (pHalf, pC) < shapeDistance (pMix, pC));
    CHECK (shapeDistance (pHalf, pMix) > 2.0);
}

TEST_CASE ("EXTERNAL G4: the demo renders (ANALOG_DEMO_DIR)", "[.][external-renders]")
{
    const char* dir = std::getenv ("ANALOG_DEMO_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    SurrogateOverride env;
    juce::String notes;
    auto save = [&] (const juce::String& name, const juce::AudioBuffer<float>& audio) {
        writeWav (out.getChildFile (name), audio);
        const float peak = std::max (audio.getMagnitude (0, 0, audio.getNumSamples()), audio.getMagnitude (1, 0, audio.getNumSamples()));
        notes << name << juce::String::formatted (": %.1f s, peak %.1f dBFS\n", audio.getNumSamples() / testRate, 20.0 * std::log10 (std::max (peak, 1.0e-9f)));
    };
    // The morph series: legato chord changes, A + B (the init patch's oscillators) against the
    // stand-in's LEGATO STRINGS on card C (a mono legato line on the top notes).
    const auto phrase = chordPhrase();
    auto series = [&] (int program, const juce::String& stem) {
        auto render = [&] (const juce::String& what, std::function<void (ShellProcessor&)> setup) {
            ShellProcessor p;
            withSurrogateProgram (p, program);
            setup (p);
            const auto audio = play (p, 9.0, phrase);
            save (stem + "_" + what + ".wav", audio);
            return audio;
        };
        const auto ab = render ("dry_AB", [] (ShellProcessor& p) { set (p, "layerB.mute", 1.0f); });
        const auto c = render ("dry_C", [] (ShellProcessor& p) {
            set (p, params::osc1Level, 0.0f);
            set (p, params::osc2Level, 0.0f);
        });
        render ("morph_OFF", [] (ShellProcessor&) {});
        std::map<int, juce::AudioBuffer<float>> at;
        for (const int pos : { 0, 25, 50, 75, 100 })
            at[pos] = render ("morph_" + juce::String (pos), [pos] (ShellProcessor& p) {
                set (p, params::morphEnabled, 1.0f);
                set (p, params::morphPosition, static_cast<float> (pos));
            });
        juce::AudioBuffer<float> mix (2, ab.getNumSamples());
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < mix.getNumSamples(); ++i)
                mix.setSample (ch, i, std::sqrt (0.5f) * (ab.getSample (ch, i) + c.getSample (ch, i)));
        const auto pAb = bandProfile (ab, 0.5, 8.0), pC = bandProfile (c, 0.5, 8.0), pMix = bandProfile (mix, 0.5, 8.0);
        notes << "  spectral shape distance (dB, third octaves 100 Hz-8 kHz, levels removed):\n";
        for (const int pos : { 0, 25, 50, 75, 100 })
        {
            const auto pp = bandProfile (at[pos], 0.5, 8.0);
            notes << juce::String::formatted ("    MORPH %3d %%: to A+B %5.2f, to C %5.2f, to the dry 50/50 crossfade %5.2f; level %+.1f dB against A+B\n", pos,
                                              shapeDistance (pp, pAb), shapeDistance (pp, pC), shapeDistance (pp, pMix),
                                              20.0 * std::log10 (rmsOf (at[pos], 0.5, 8.0) / rmsOf (ab, 0.5, 8.0)));
        }
        notes << juce::String::formatted ("    (the crossfade itself: to A+B %5.2f, to C %5.2f)\n", shapeDistance (pMix, pAb), shapeDistance (pMix, pC));
    };
    series (0, "strings");
    // Example 2: a full chord on the stand-in's EVOLVING PAD, MORPH at 50 % (the hybrid).
    {
        ShellProcessor p;
        withSurrogateProgram (p, 1);
        set (p, params::morphEnabled, 1.0f);
        set (p, params::morphPosition, 50.0f);
        save ("example2_chord_evolving_pad_morph50.wav", play (p, 8.0, { { 0.0, 45, 100 }, { 0.0, 52, 100 }, { 0.0, 57, 100 }, { 0.0, 60, 100 }, { 0.0, 64, 100 },
                                                                      { 7.0, 45, 0 }, { 7.0, 52, 0 }, { 7.0, 57, 0 }, { 7.0, 60, 0 }, { 7.0, 64, 0 } }));
    }
    // Example 3: card C's own arpeggiator (eighths, UP/DOWN, two octaves) on LEGATO STRINGS while A
    // and B hold the chord; MORPH off (each heard as itself).
    {
        ShellProcessor p;
        withSurrogateProgram (p, 0);
        set (p, "arp.enabled", 1.0f);
        set (p, "analog.arp.link", 0.0f);
        set (p, "analog.arp.cardA", 0.0f);
        set (p, "analog.arp.cardB", 0.0f);
        set (p, "analog.arp.cardC", 1.0f);
        set (p, "analog.arp.cardC.rate", 1.0f);
        set (p, "analog.arp.cardC.pattern", 2.0f);
        set (p, "analog.arp.cardC.octaves", 2.0f);
        play (p, 0.1, {});
        save ("example3_C_arp_AB_hold.wav", play (p, 8.0, { { 0.0, 48, 100 }, { 0.0, 55, 100 }, { 0.0, 64, 100 }, { 7.0, 48, 0 }, { 7.0, 55, 0 }, { 7.0, 64, 0 } }));
    }
    out.getChildFile ("renders.txt").replaceWithText (notes);
}
