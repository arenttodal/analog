// GLOBAL MORPH 2.0 (docs/GLOBAL_MORPH_2.md). Gate 1: D's path measured stage by stage - the
// source, D's FILTER, its LEVEL, the global MORPH stage's D input and output, the FX's input - in
// every source mode, with real audio, before anything new is built on it.
#include "AnalogEngine.h"
#include "AnalogParameters.h"
#include "ExternalInstrument.h"
#include "ShellProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace
{
    namespace params = andor::analog::params;
    using andor::analog::AnalogEngine;
    using andor::analog::Synth;
    using andor::shell::ShellProcessor;
    using Tap = Synth::AuxTapPoint;
    using State = Synth::AuxState;

    constexpr double rate = 48000.0;
    constexpr int block = 512;

    void set (ShellProcessor& p, const juce::String& id, float v)
    {
        auto* param = p.parameters.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (v));
    }

    AnalogEngine& engineOf (ShellProcessor& p) { return dynamic_cast<AnalogEngine&> (p.productEngine()); }

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

    /** A host transport, stopped or playing (a DAW's), for RHYTHM on the host's clock. */
    struct Transport final : juce::AudioPlayHead
    {
        bool playing = false;
        double samples = 0.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (100.0);
            info.setIsPlaying (playing);
            info.setTimeInSamples (static_cast<juce::int64> (samples));
            info.setPpqPosition (samples / rate * 100.0 / 60.0);
            info.setTimeSignature (juce::AudioPlayHead::TimeSignature {});
            return info;
        }
    };

    /** A loop of ticks (2 kHz, 30 ms) on each beat at 100 BPM, 4 bars: sparse, as a ticking clock. */
    std::shared_ptr<const andor::analog::rhythm::PreparedLoop> tickLoop()
    {
        constexpr double beat = 60.0 / 100.0;
        std::vector<float> x (static_cast<std::size_t> (16 * beat * rate), 0.0f);
        for (int k = 0; k < 16; ++k)
        {
            const auto first = static_cast<std::size_t> (std::lround (k * beat * rate));
            for (std::size_t i = 0; i < static_cast<std::size_t> (0.03 * rate) && first + i < x.size(); ++i)
            {
                const double t = static_cast<double> (i) / rate;
                x[first + i] = static_cast<float> ((k % 4 == 0 ? 0.8 : 0.5) * std::exp (-t / 0.006) * std::sin (2.0 * juce::MathConstants<double>::pi * 2000.0 * t));
            }
        }
        return andor::analog::rhythm::prepareLoop (x.data(), x.data(), x.size(), rate);
    }

    /** A bowed-string-like stand-in for a live input: a sawtooth at 220 Hz, slowly swelling. */
    std::vector<float> liveInput (double seconds)
    {
        std::vector<float> x (static_cast<std::size_t> (seconds * rate));
        double phase = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            phase += 220.0 / rate;
            phase -= std::floor (phase);
            x[i] = static_cast<float> (0.3 * (2.0 * phase - 1.0));
        }
        return x;
    }

    // Each processor is made on the heap (a host does the same): one is about 126 KB, and the
    // probe's many sections each keep one, which overran Windows' 1 MB stack (MSVC reserves them
    // all in the test's frame).

    /** What a run measured: each tap's highest peak, how many blocks D spent in each state,
        how many the stage ran, how many the old display (activity < 0.05) would call silent, and
        the output. */
    struct Measured
    {
        std::array<float, static_cast<std::size_t> (Tap::count)> peak {};
        std::map<State, int> states;
        int blocks = 0, stageBlocks = 0, oldSilentBlocks = 0, keyBlocks = 0, keySilentBlocks = 0;
        juce::AudioBuffer<float> out;
        float tap (Tap t) const { return peak[static_cast<std::size_t> (t)]; }
        int in (State s) const { return states.count (s) != 0 ? states.at (s) : 0; }
    };

    struct Run
    {
        double seconds = 3.0;
        std::vector<int> chord { 48, 55, 60, 64 };
        double holdFrom = 0.0, holdTo = 2.5;   ///< the chord held from / to (seconds)
        const std::vector<float>* sidechain = nullptr;
        Transport* transport = nullptr;
    };

    Measured measure (ShellProcessor& p, const Run& run)
    {
        if (run.sidechain != nullptr)
        {
            auto layout = p.getBusesLayout();
            layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
            REQUIRE (p.setBusesLayout (layout));
        }
        if (run.transport != nullptr)
            p.setPlayHead (run.transport);
        p.prepareToPlay (rate, block);
        Measured m;
        const int total = static_cast<int> (run.seconds * rate);
        m.out.setSize (2, total);
        juce::AudioBuffer<float> buffer (2, block);
        const int on = static_cast<int> (run.holdFrom * rate), off = static_cast<int> (run.holdTo * rate);
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            buffer.setSize (2, n, false, false, true);
            buffer.clear();
            if (run.sidechain != nullptr)
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < n; ++i)
                        buffer.setSample (ch, i, static_cast<std::size_t> (pos + i) < run.sidechain->size() ? (*run.sidechain)[static_cast<std::size_t> (pos + i)] : 0.0f);
            juce::MidiBuffer midi;
            for (const int note : run.chord)
            {
                if (on >= pos && on < pos + n)
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)), on - pos);
                if (off >= pos && off < pos + n)
                    midi.addEvent (juce::MidiMessage::noteOff (1, note), off - pos);
            }
            p.processBlock (buffer, midi);
            if (run.transport != nullptr && run.transport->playing)
                run.transport->samples += n;
            for (int ch = 0; ch < 2; ++ch)
                m.out.copyFrom (ch, pos, buffer, ch, 0, n);
            const auto t = engineOf (p).auxTelemetry();
            for (std::size_t k = 0; k < m.peak.size(); ++k)
                m.peak[k] = std::max (m.peak[k], t.taps[k].peak);
            ++m.states[t.state];
            ++m.blocks;
            m.stageBlocks += t.stageRunning ? 1 : 0;
            const auto view = engineOf (p).globalMorphView();
            const bool oldSilent = view.presence < 0.05f;   // (the old display's test for "D is silent", on the stage's presence now)
            m.oldSilentBlocks += oldSilent ? 1 : 0;
            if (pos >= on && pos + n <= off)
            {
                ++m.keyBlocks;
                m.keySilentBlocks += oldSilent ? 1 : 0;
            }
        }
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < total; ++i)
                REQUIRE (std::isfinite (m.out.getSample (ch, i)));
        return m;
    }

    double rms (const juce::AudioBuffer<float>& a, int from = 0, int to = -1)
    {
        to = to < 0 ? a.getNumSamples() : to;
        double sum = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = from; i < to; ++i)
                sum += static_cast<double> (a.getSample (ch, i)) * a.getSample (ch, i);
        return std::sqrt (sum / std::max (1, 2 * (to - from)));
    }

    /** MORPH TO D's stage's latency at 48 kHz (GLOBAL MORPH 2.0): a render with MORPH TO D
        chosen is this much later than one with MORPH TO C. */
    constexpr int stageLag = 1024;

    /** The difference between two renders, relative to the first (dB): how much D changed it.
        `lag`: `other` is that much later (MORPH TO D against MORPH TO C). */
    double changeDb (const juce::AudioBuffer<float>& reference, const juce::AudioBuffer<float>& other, int from, int to, int lag = 0)
    {
        double diff = 0.0, ref = 0.0;
        to = std::min (to, other.getNumSamples() - lag);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = from; i < to; ++i)
            {
                const double d = static_cast<double> (other.getSample (ch, i + lag)) - reference.getSample (ch, i);
                diff += d * d;
                ref += static_cast<double> (reference.getSample (ch, i)) * reference.getSample (ch, i);
            }
        return 10.0 * std::log10 ((diff + 1.0e-20) / (ref + 1.0e-20));
    }

    juce::String report (const juce::String& name, const Measured& m)
    {
        auto db = [] (float x) { return juce::String (juce::Decibels::gainToDecibels (x, -120.0f), 1); };
        juce::String s;
        s << name << ": source " << db (m.tap (Tap::source)) << " | filter " << db (m.tap (Tap::filter)) << " | level " << db (m.tap (Tap::level))
          << " | morph in " << db (m.tap (Tap::morphIn)) << " | morph out " << db (m.tap (Tap::morphOut)) << " | fx in " << db (m.tap (Tap::fxIn))
          << " dBFS peak; stage ran " << m.stageBlocks << "/" << m.blocks << " blocks; old display 'D is silent' " << m.oldSilentBlocks << "/" << m.blocks
          << " (while keys held " << m.keySilentBlocks << "/" << m.keyBlocks << "); states:";
        static const char* names[] { "off", "muted", "noSource", "waitingForKeys", "waitingForTransport", "noLiveInput", "betweenHits", "active", "silent" };
        for (const auto& [state, count] : m.states)
            s << " " << names[static_cast<int> (state)] << "=" << count;
        return s;
    }

    /** The macros whose effects move in time (MOVEMENT, SPACE's modulation, ECHO, DRIVE's
        movement) at 0: they run on the clock while MORPH TO D's sound reaches them the stage's
        latency later, so with them a delayed render is not the same samples later (any latency
        before a moving effect does that). Exactness is measured without them. */
    void stillEffects (ShellProcessor& p)
    {
        for (const char* id : { "motion", "space", "echo", "drive" })
            set (p, id, 0.0f);
    }

    /** MORPH TO D at `position` %, D on in `mode` at -3 dB. */
    void morphToD (ShellProcessor& p, int mode, float position)
    {
        set (p, params::auxEnabled, 1.0f);
        set (p, params::auxMode, static_cast<float> (mode));
        set (p, params::auxLevel, -3.0f);
        set (p, params::morphTarget, 1.0f);
        set (p, params::morphEnabled, 1.0f);
        set (p, params::morphPosition, position);
    }
}

TEST_CASE ("GLOBAL MORPH 2.0 gate 1 probe: D's path measured in every source mode (WARN lines: the evidence)", "[analog][gm2][dpath]")
{
    const auto loop = tickLoop();
    const auto input = liveInput (3.0);
    juce::StringArray lines;
    auto keep = [&] (const juce::String& name, const Measured& m) {
        lines.add (report (name, m));
        WARN (lines[lines.size() - 1]);
    };

    // The synth alone (D off): the reference each D render is compared against.
    Measured synthOnly;
    {
        auto pOwned = std::make_unique<ShellProcessor>();
        auto& p = *pOwned;
        stillEffects (p);
        synthOnly = measure (p, {});
    }

    SECTION ("RHYTHM, a loaded loop, GATE KEYS (the default): keys held - D plays and reaches the stage")
    {
        auto pOwned = std::make_unique<ShellProcessor>();
        auto& p = *pOwned;
        engineOf (p).setAuxLoopForTest (loop);
        morphToD (p, 0, 50.0f);
        const auto m = measure (p, {});
        keep ("rhythm keys held, MORPH TO D 50 %", m);
        CHECK (m.tap (Tap::source) > 0.1f);
        CHECK (m.tap (Tap::level) > 0.05f);
        CHECK (m.tap (Tap::morphIn) > 0.05f);
        CHECK (m.stageBlocks > 0);
        CHECK (changeDb (synthOnly.out, m.out, 0, static_cast<int> (2.5 * rate), stageLag) > -20.0);   // (D changes the sound)
        // The repair: while keys are held and the loop plays, D is never reported silent.
        CHECK (m.in (State::silent) == 0);
        CHECK (m.in (State::waitingForKeys) <= 2);   // (after the release only: the fall, then waiting)
    }
    SECTION ("RHYTHM, keys held, MORPH 100 %: the loop itself, its silences included - never the synth between the hits")
    {
        auto pOwned = std::make_unique<ShellProcessor>();
        auto& p = *pOwned;
        engineOf (p).setAuxLoopForTest (loop);
        morphToD (p, 0, 100.0f);
        const auto m = measure (p, {});
        keep ("rhythm keys held, MORPH TO D 100 %", m);
        // Between the ticks (0.1 .. 0.55 s after a beat's tick at 0, 0.6, 1.2 s) the output is
        // D's silence, not the synth (the old stage gave the synth back between hits).
        CHECK (rms (m.out, static_cast<int> (0.1 * rate), static_cast<int> (0.55 * rate)) < 0.1 * rms (synthOnly.out, static_cast<int> (0.1 * rate), static_cast<int> (0.55 * rate)));   // (what is left: the ticks' own SPACE tail)
    }
    SECTION ("RHYTHM, a loaded loop, GATE KEYS, no key: D waits for MIDI (measured silent, said why)")
    {
        auto pOwned = std::make_unique<ShellProcessor>();
        auto& p = *pOwned;
        engineOf (p).setAuxLoopForTest (loop);
        morphToD (p, 0, 50.0f);
        Run run;
        run.chord.clear();
        const auto m = measure (p, run);
        keep ("rhythm keys, no key", m);
        CHECK (m.tap (Tap::source) == 0.0f);
        CHECK (m.in (State::waitingForKeys) == m.blocks);
        CHECK (engineOf (p).auxStatus().label.endsWith ("WAITING FOR MIDI"));   // (said so in the rack, not "D is silent")
        CHECK_FALSE (engineOf (p).auxStatus().signal);
    }
    SECTION ("RHYTHM FREE on a stopped host transport: waiting for the transport; playing: heard")
    {
        Transport transport;
        auto pOwned = std::make_unique<ShellProcessor>();
        auto& p = *pOwned;
        engineOf (p).setAuxLoopForTest (loop);
        morphToD (p, 0, 50.0f);
        stillEffects (p);
        set (p, params::auxRhythmGate, 1.0f);   // FREE
        Run run;
        run.transport = &transport;
        const auto stopped = measure (p, run);
        keep ("rhythm free, transport stopped", stopped);
        CHECK (stopped.in (State::waitingForTransport) == stopped.blocks);
        CHECK (engineOf (p).auxStatus().label.endsWith ("WAITING FOR TRANSPORT"));
        CHECK (changeDb (synthOnly.out, stopped.out, 0, static_cast<int> (2.5 * rate), stageLag) < -60.0);   // (D unavailable: the synth as it is)
        transport.playing = true;
        const auto playing = measure (p, run);
        keep ("rhythm free, transport playing", playing);
        CHECK (playing.tap (Tap::morphIn) > 0.05f);
        CHECK (playing.in (State::silent) == 0);
    }
    SECTION ("RHYTHM at MORPH 0 %: D plays into the stage's place but is not heard; never 'silent'")
    {
        auto pOwned = std::make_unique<ShellProcessor>();
        auto& p = *pOwned;
        engineOf (p).setAuxLoopForTest (loop);
        morphToD (p, 0, 0.0f);
        stillEffects (p);
        const auto m = measure (p, {});
        keep ("rhythm keys held, MORPH TO D 0 %", m);
        CHECK (m.tap (Tap::level) > 0.05f);   // (D sounds at its level...)
        CHECK (changeDb (synthOnly.out, m.out, 0, static_cast<int> (2.5 * rate), stageLag) < -60.0);   // (...and 0 % is the synth: D does not leak)
        CHECK (m.in (State::silent) == 0);
    }
    SECTION ("NOISE (keyed and FREE), KONTAKT (the test instrument), LIVE INPUT: each reaches the stage")
    {
        {
            auto pOwned = std::make_unique<ShellProcessor>();
            auto& p = *pOwned;
            morphToD (p, 1, 50.0f);
            const auto m = measure (p, {});
            keep ("noise keyed, keys held", m);
            CHECK (m.tap (Tap::morphIn) > 0.01f);
            CHECK (m.in (State::silent) == 0);
        }
        {
            auto pOwned = std::make_unique<ShellProcessor>();
            auto& p = *pOwned;
            morphToD (p, 1, 50.0f);
            set (p, params::auxNoiseGate, 1.0f);
            Run run;
            run.chord.clear();
            const auto m = measure (p, run);
            keep ("noise free, no keys", m);
            CHECK (m.tap (Tap::morphIn) > 0.01f);
            CHECK (m.in (State::active) == m.blocks);
        }
        {
            SurrogateOverride env;
            auto pOwned = std::make_unique<ShellProcessor>();
            auto& p = *pOwned;
            morphToD (p, 2, 50.0f);
            REQUIRE (engineOf (p).auxInstrument().loadKontakt());
            const auto m = measure (p, {});
            keep ("kontakt (test instrument), keys held", m);
            CHECK (m.tap (Tap::morphIn) > 0.01f);
            CHECK (changeDb (synthOnly.out, m.out, 0, static_cast<int> (2.5 * rate), stageLag) > -20.0);
        }
        {
            auto pOwned = std::make_unique<ShellProcessor>();
            auto& p = *pOwned;
            morphToD (p, 3, 50.0f);
            Run run;
            run.sidechain = &input;
            const auto m = measure (p, run);
            keep ("live input (sawtooth on the sidechain)", m);
            CHECK (m.tap (Tap::source) > 0.1f);
            CHECK (m.tap (Tap::morphIn) > 0.05f);
            CHECK (m.in (State::silent) == 0);
            auto qOwned = std::make_unique<ShellProcessor>();
            auto& q = *qOwned;
            morphToD (q, 3, 50.0f);
            const auto none = measure (q, {});
            keep ("live input, nothing on the sidechain", none);
            CHECK (none.in (State::noLiveInput) == none.blocks);
            CHECK (engineOf (q).auxStatus().label.endsWith ("NO INPUT"));
        }
    }
    SECTION ("Power, MUTE, SOLO: said as such")
    {
        auto pOwned = std::make_unique<ShellProcessor>();
        auto& p = *pOwned;
        engineOf (p).setAuxLoopForTest (loop);
        morphToD (p, 0, 50.0f);
        set (p, params::auxMute, 1.0f);
        const auto muted = measure (p, {});
        keep ("rhythm muted", muted);
        CHECK (muted.tap (Tap::level) == 0.0f);
        CHECK (muted.in (State::muted) >= muted.blocks - 2);
        set (p, params::auxMute, 0.0f);
        set (p, params::auxEnabled, 0.0f);
        const auto off = measure (p, {});
        keep ("rhythm powered off", off);
        CHECK (off.in (State::off) >= off.blocks - 2);
        CHECK (engineOf (p).auxStatus().label.endsWith ("OFF"));
    }
}

TEST_CASE ("GLOBAL MORPH 2.0 gate 1: MORPH TO D's endpoints with real audio in every source mode - 0 % the synth exactly, 100 % D exactly", "[analog][gm2][dpath][endpoints]")
{
    const auto loop = tickLoop();
    // (LIVE INPUT: a sidechain already carrying signal when the notes start. Its availability is
    // measured - 2 s of silence gives the synth back - so an input that arrives together with
    // the notes after a silence lets the synth's first few ms through, by design.)
    const auto input = liveInput (3.0);
    struct Mode
    {
        const char* name;
        int mode;
        bool sidechain;
    };
    for (const auto& mode : { Mode { "RHYTHM", 0, false }, Mode { "NOISE", 1, false }, Mode { "KONTAKT", 2, false }, Mode { "LIVE INPUT", 3, true } })
    {
        INFO (mode.name);
        SurrogateOverride env;
        // `position` < 0: MORPH off. `solo`: D soloed (D itself, the reference for 100 %).
        auto renderWith = [&] (bool dOn, float position, bool solo) {
            auto pOwned = std::make_unique<ShellProcessor>();
            auto& p = *pOwned;
            stillEffects (p);
            if (mode.mode == 0)
                engineOf (p).setAuxLoopForTest (loop);
            if (dOn)
            {
                morphToD (p, mode.mode, std::max (0.0f, position));
                if (position < 0.0f)
                    set (p, params::morphEnabled, 0.0f);
                set (p, params::auxSolo, solo ? 1.0f : 0.0f);
                if (mode.mode == 2)
                    REQUIRE (engineOf (p).auxInstrument().loadKontakt());
            }
            Run run;
            run.holdFrom = 0.05;   // (the first notes just after the start, as in a session)
            if (mode.sidechain)
                run.sidechain = &input;
            return measure (p, run);
        };
        const auto synth = renderWith (false, 0.0f, false);
        const auto dItself = renderWith (true, -1.0f, true);
        const auto zero = renderWith (true, 0.0f, false);
        const auto full = renderWith (true, 100.0f, false);
        const auto half = renderWith (true, 50.0f, false);
        const int from = static_cast<int> (0.05 * rate), to = static_cast<int> (2.4 * rate);
        // (the synth alone: MORPH TO C, no stage; every other render chooses MORPH TO D - D itself
        // too, MORPH off and D soloed - and is the stage's latency later)
        const double zeroDb = changeDb (synth.out, zero.out, from, to, stageLag), fullDb = changeDb (dItself.out, full.out, from, to);
        const double halfFromSynth = changeDb (synth.out, half.out, from, to, stageLag), halfFromD = changeDb (dItself.out, half.out, from, to);
        WARN (mode.name << ": D itself " << juce::Decibels::gainToDecibels (static_cast<float> (rms (dItself.out, from, to))) << " dB RMS; MORPH 0 % vs the synth "
                        << zeroDb << " dB; 100 % vs D itself " << fullDb << " dB; 50 % vs the synth " << halfFromSynth << " dB, vs D " << halfFromD << " dB");
        CHECK (rms (dItself.out, from, to) > 1.0e-3);   // (D sounds)
        CHECK (zeroDb < -60.0);                         // (0 %: the synth, D nowhere)
        // (100 %: D, the synth nowhere. LIVE INPUT: the stage glides in over its first 15 ms
        // from power-up, a difference left only in the SPACE tail of those first ms)
        CHECK (fullDb < (mode.mode == 3 ? -45.0 : -60.0));
        CHECK (halfFromSynth > -30.0);                  // (50 %: neither)
        CHECK (halfFromD > -30.0);
    }
}

TEST_CASE ("GLOBAL MORPH 2.0 gate 1 renders: a pad x D RHYTHM (ticking loop, GATE KEYS) at MORPH TO D 0 / 50 / 100 %, with the path's telemetry per block (ANALOG_RENDER_DIR)", "[.][renders][gm2g1]")
{
    const auto dir = juce::SystemStats::getEnvironmentVariable ("ANALOG_RENDER_DIR", {});
    REQUIRE (dir.isNotEmpty());
    const juce::File out (dir);
    out.createDirectory();
    const auto loop = tickLoop();
    auto write = [&] (const juce::String& name, const juce::AudioBuffer<float>& audio) {
        const auto file = out.getChildFile (name + ".wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), rate, 2, 24, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();
        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    };
    Run run;
    run.seconds = 6.0;
    run.holdFrom = 0.05;
    run.holdTo = 5.0;
    {
        auto pOwned = std::make_unique<ShellProcessor>();
        auto& p = *pOwned;
        write ("gm2-g1-1-synth", measure (p, run).out);
    }
    {
        auto pOwned = std::make_unique<ShellProcessor>();
        auto& p = *pOwned;
        engineOf (p).setAuxLoopForTest (loop);
        morphToD (p, 0, 0.0f);
        set (p, params::morphEnabled, 0.0f);
        set (p, params::auxSolo, 1.0f);
        write ("gm2-g1-2-d-itself", measure (p, run).out);
    }
    for (const float position : { 0.0f, 50.0f, 100.0f })
    {
        auto pOwned = std::make_unique<ShellProcessor>();
        auto& p = *pOwned;
        engineOf (p).setAuxLoopForTest (loop);
        morphToD (p, 0, position);
        // The telemetry, block by block: each tap's peak (dBFS) and D's state.
        juce::String csv ("seconds,source,filter,level,morphIn,morphOut,fxIn,state,available\n");
        Run logged = run;
        p.prepareToPlay (rate, block);
        const int total = static_cast<int> (logged.seconds * rate);
        juce::AudioBuffer<float> audio (2, total), buffer (2, block);
        const int on = static_cast<int> (logged.holdFrom * rate), off = static_cast<int> (logged.holdTo * rate);
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            buffer.setSize (2, n, false, false, true);
            buffer.clear();
            juce::MidiBuffer midi;
            for (const int note : logged.chord)
            {
                if (on >= pos && on < pos + n)
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)), on - pos);
                if (off >= pos && off < pos + n)
                    midi.addEvent (juce::MidiMessage::noteOff (1, note), off - pos);
            }
            p.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                audio.copyFrom (ch, pos, buffer, ch, 0, n);
            const auto t = engineOf (p).auxTelemetry();
            csv << juce::String (static_cast<double> (pos) / rate, 3);
            for (const auto& tap : t.taps)
                csv << "," << juce::String (juce::Decibels::gainToDecibels (tap.peak, -120.0f), 1);
            csv << "," << static_cast<int> (t.state) << "," << juce::String (t.available, 2) << "\n";
        }
        const auto name = "gm2-g1-" + juce::String (3 + juce::roundToInt (position / 50.0f)) + "-morph-to-d-" + juce::String (juce::roundToInt (position));
        write (name, audio);
        out.getChildFile (name + "-telemetry.csv").replaceWithText (csv);
        WARN (name << ": RMS " << juce::Decibels::gainToDecibels (static_cast<float> (rms (audio))) << " dB, status " << engineOf (p).auxStatus().label);
    }
}


//==============================================================================
// GLOBAL MORPH 2.0 gate 10: the six listening examples (ANALOG_RENDER_DIR). No recordings of a
// cello, a voice, spiccato strings or a clock are in the repository: D's sources here are
// synthesised stand-ins (described in each file's name and in docs/GLOBAL_MORPH_2.md), played
// through D's real paths - LIVE INPUT's sidechain, RHYTHM's loop, NOISE. For each: the synth
// alone, D alone, an equal-power crossfade of the two (offline, for comparison), and IMPRINT,
// FUSE and WEAVE at 50 %; then a few controls' extremes.

namespace
{
    std::vector<float> cello (double seconds, std::uint32_t seed = 7)
    {
        // A bowed string stand-in: a sawtooth with a 5 Hz vibrato (onset-delayed), through three
        // body resonances (300 Hz, 1 kHz, 2.5 kHz), with bow noise following the bow's pressure;
        // legato notes C3 - G3 - E3 - A3, 1.5 s each.
        std::vector<float> x (static_cast<std::size_t> (seconds * rate));
        const double notes[] { 130.81, 196.0, 164.81, 220.0 };
        double phase = 0.0, b1 = 0, b2 = 0, c1 = 0, c2 = 0, d1 = 0, d2 = 0, noiseLow = 0.0;
        std::uint32_t s = seed;
        auto resonate = [] (double in, double hz, double bw, double& y1, double& y2) {
            const double r = std::exp (-juce::MathConstants<double>::pi * bw / rate), k = 2.0 * r * std::cos (2.0 * juce::MathConstants<double>::pi * hz / rate);
            const double y = (1.0 - r) * in + k * y1 - r * r * y2;
            y2 = y1;
            y1 = y;
            return y;
        };
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            const double t = static_cast<double> (i) / rate, inNote = std::fmod (t, 1.5);
            const double hz = notes[static_cast<std::size_t> (t / 1.5) % 4] * (1.0 + 0.005 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.0 * t) * std::min (1.0, inNote / 0.4));
            phase += hz / rate;
            phase -= std::floor (phase);
            const double pressure = std::min (1.0, inNote / 0.08) * (0.8 + 0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 0.6 * t));
            s = s * 1664525u + 1013904223u;
            noiseLow += 0.3 * ((static_cast<double> (s >> 8) / 8388608.0 - 1.0) - noiseLow);
            const double raw = pressure * ((2.0 * phase - 1.0) + 0.15 * noiseLow);
            // (about -15 dB RMS: a recording's level, not a hot synth's)
            x[i] = static_cast<float> (0.125 * (resonate (raw, 300.0, 120.0, b1, b2) * 2.0 + resonate (raw, 1000.0, 200.0, c1, c2) * 3.0 + resonate (raw, 2500.0, 400.0, d1, d2) * 2.0));
        }
        return x;
    }

    std::vector<float> spiccato (double seconds)
    {
        // Spiccato stand-in: short bowed strokes (60 ms, a fast attack and decay), eighths at 120
        // BPM, alternating G3 / D4, through the same body.
        const auto bowed = cello (seconds, 11);
        std::vector<float> x (bowed.size());
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            const double t = std::fmod (static_cast<double> (i) / rate, 0.25);
            x[i] = static_cast<float> (bowed[i] * 3.0 * std::min (1.0, t / 0.004) * std::exp (-t / 0.03));
        }
        return x;
    }

    std::vector<float> voice (double seconds)
    {
        // A sung vowel stand-in: a 196 Hz glottal pulse train through formants moving a - e - i - o
        // (2 s each), with a light vibrato.
        std::vector<float> x (static_cast<std::size_t> (seconds * rate));
        const double f1[] { 730, 530, 270, 570 }, f2[] { 1090, 1840, 2290, 840 }, f3[] { 2440, 2480, 3010, 2410 };
        double phase = 0.0;
        std::array<double, 6> st {};
        auto res = [] (double in, double hz, double bw, double& y1, double& y2) {
            const double r = std::exp (-juce::MathConstants<double>::pi * bw / rate), k = 2.0 * r * std::cos (2.0 * juce::MathConstants<double>::pi * hz / rate);
            const double y = (1.0 - r) * in + k * y1 - r * r * y2;
            y2 = y1;
            y1 = y;
            return y;
        };
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            const double t = static_cast<double> (i) / rate;
            const double pos = std::fmod (t / 2.0, 4.0);
            const auto a = static_cast<std::size_t> (pos), b = (a + 1) % 4;
            const double w = std::clamp ((pos - static_cast<double> (a)) * 4.0 - 3.0, 0.0, 1.0);   // (glide in the last quarter)
            phase += 196.0 * (1.0 + 0.006 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * t)) / rate;
            const double pulse = phase >= 1.0 ? 1.0 : 0.0;
            phase -= std::floor (phase);
            const double y = res (pulse, f1[a] + w * (f1[b] - f1[a]), 90.0, st[0], st[1]) * 3.0 + res (pulse, f2[a] + w * (f2[b] - f2[a]), 110.0, st[2], st[3]) * 2.0
                             + res (pulse, f3[a] + w * (f3[b] - f3[a]), 160.0, st[4], st[5]);
            x[i] = static_cast<float> (2.5 * y);
        }
        return x;
    }

    void writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), rate, 2, 16, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();
        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }
}

TEST_CASE ("GLOBAL MORPH 2.0 listening examples A-F: dry S, dry D, crossfade, IMPRINT / FUSE / WEAVE at 50 %, and controls' extremes (ANALOG_RENDER_DIR)", "[.][renders][gm2demos]")
{
    const auto dir = juce::SystemStats::getEnvironmentVariable ("ANALOG_RENDER_DIR", {});
    REQUIRE (dir.isNotEmpty());
    const juce::File out (dir);
    out.createDirectory();
    constexpr double seconds = 6.0;
    const auto celloIn = cello (seconds + 1.0), spiccatoIn = spiccato (seconds + 1.0), voiceIn = voice (seconds + 1.0);
    const auto loop = tickLoop();
    juce::StringArray notes;
    struct Example
    {
        const char* id;
        const char* what;
        int mode;                                  ///< D's source mode
        const std::vector<float>* input;           ///< LIVE INPUT's sidechain (null: none)
        std::vector<int> chord;
    };
    const std::vector<Example> examples {
        { "A-analog-cello", "pad chord x a bowed-cello stand-in (LIVE INPUT)", 3, &celloIn, { 48, 55, 60, 64 } },
        { "B-analog-spiccato", "pad chord x spiccato-string stand-in (LIVE INPUT)", 3, &spiccatoIn, { 48, 55, 60, 64 } },
        { "C-analog-clock", "pad chord x a ticking clock (D RHYTHM, GATE KEYS)", 0, nullptr, { 48, 55, 60, 64 } },
        { "D-analog-voice", "pad chord x a sung-vowel stand-in (LIVE INPUT)", 3, &voiceIn, { 48, 55, 60, 64 } },
        { "E-analog-noise", "pad chord x D NOISE (pink, moving)", 1, nullptr, { 48, 55, 60, 64 } },
        { "F-weave-complex", "a wide chord x the bowed stand-in (two complex sources)", 3, &celloIn, { 36, 48, 55, 59, 64, 71 } },
    };
    auto render = [&] (const Example& e, const std::function<void (ShellProcessor&)>& setup) {
        auto pOwned = std::make_unique<ShellProcessor>();
        auto& p = *pOwned;
        engineOf (p).setAuxLoopForTest (loop);
        set (p, params::auxEnabled, 1.0f);
        set (p, params::auxMode, static_cast<float> (e.mode));
        set (p, params::auxLevel, -3.0f);
        if (e.mode == 1)
        {
            set (p, params::auxNoiseColor, 1.0f);
            set (p, params::auxNoiseGate, 1.0f);
            set (p, params::auxNoiseMovement, 60.0f);
        }
        set (p, params::tone, 70.0f);
        if (setup)
            setup (p);
        Run run;
        run.seconds = seconds;
        run.chord = e.chord;
        run.holdFrom = 0.05;
        run.holdTo = seconds - 0.8;
        run.sidechain = e.input;
        return measure (p, run).out;
    };
    auto morph = [] (int method, float position, std::function<void (ShellProcessor&)> more = {}) {
        return [=] (ShellProcessor& p) {
            set (p, params::morphTarget, 1.0f);
            set (p, params::morphEnabled, 1.0f);
            set (p, params::morphPosition, position);
            set (p, params::morphGlobalMethod, static_cast<float> (method));
            if (more)
                more (p);
        };
    };
    for (const auto& e : examples)
    {
        auto save = [&] (const juce::String& suffix, const juce::AudioBuffer<float>& audio) {
            const auto name = juce::String ("gm2-") + e.id + "-" + suffix;
            writeWav (out.getChildFile (name + ".wav"), audio);
            const auto line = name + ".wav: " + e.what + " - RMS " + juce::String (juce::Decibels::gainToDecibels (static_cast<float> (rms (audio))), 1) + " dB, peak "
                              + juce::String (juce::Decibels::gainToDecibels (audio.getMagnitude (0, audio.getNumSamples())), 1) + " dBFS";
            notes.add (line);
            WARN (line);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < audio.getNumSamples(); ++i)
                    REQUIRE (std::isfinite (audio.getSample (ch, i)));
        };
        const auto drySynth = render (e, [] (ShellProcessor& p) { set (p, params::auxEnabled, 0.0f); });
        const auto dryD = render (e, [] (ShellProcessor& p) { set (p, params::auxSolo, 1.0f); });
        save ("1-dry-synth", drySynth);
        save ("2-dry-d", dryD);
        juce::AudioBuffer<float> fade (2, drySynth.getNumSamples());
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < fade.getNumSamples(); ++i)
                fade.setSample (ch, i, 0.7071f * (drySynth.getSample (ch, i) + dryD.getSample (ch, i)));
        save ("3-crossfade-50", fade);
        save ("4-imprint-50", render (e, morph (0, 50.0f)));
        save ("5-fuse-50", render (e, morph (1, 50.0f)));
        save ("6-weave-50", render (e, morph (2, 50.0f)));
    }
    // Controls' extremes, each on the example it is about.
    auto extreme = [&] (const Example& e, const juce::String& name, int method, std::function<void (ShellProcessor&)> more) {
        const auto audio = render (e, morph (method, 50.0f, std::move (more)));
        const auto file = juce::String ("gm2-controls-") + name;
        writeWav (out.getChildFile (file + ".wav"), audio);
        notes.add (file + ".wav: " + e.what);
    };
    extreme (examples[2], "C-clock-imprint-attack-0", 0, [] (ShellProcessor& p) { set (p, params::morphGlobalAttack, 0.0f); });
    extreme (examples[2], "C-clock-imprint-attack-100", 0, [] (ShellProcessor& p) { set (p, params::morphGlobalAttack, 100.0f); });
    extreme (examples[4], "E-noise-fuse-texture-0", 1, [] (ShellProcessor& p) { set (p, params::morphGlobalTexture, 0.0f); });
    extreme (examples[4], "E-noise-fuse-texture-100", 1, [] (ShellProcessor& p) { set (p, params::morphGlobalTexture, 100.0f); });
    extreme (examples[4], "E-noise-imprint-detail-0", 0, [] (ShellProcessor& p) { set (p, params::morphGlobalDetail, 0.0f); });
    extreme (examples[4], "E-noise-imprint-detail-100", 0, [] (ShellProcessor& p) { set (p, params::morphGlobalDetail, 100.0f); });
    extreme (examples[3], "D-voice-imprint-formants-minus12", 0, [] (ShellProcessor& p) { set (p, params::morphGlobalFormants, -12.0f); });
    extreme (examples[3], "D-voice-imprint-formants-plus12", 0, [] (ShellProcessor& p) { set (p, params::morphGlobalFormants, 12.0f); });
    extreme (examples[0], "A-cello-fuse-layered", 1, [] (ShellProcessor& p) { set (p, params::morphGlobalOutput, 1.0f); });
    extreme (examples[0], "A-cello-fuse-levelmatch-off", 1, [] (ShellProcessor& p) { set (p, params::morphGlobalLevelMatch, 0.0f); });
    extreme (examples[0], "A-cello-imprint-carrier-osc1", 0, [] (ShellProcessor& p) { set (p, params::morphGlobalCarrier, 0.0f); set (p, params::osc2Level, 100.0f); });
    extreme (examples[0], "A-cello-imprint-carrier-osc2", 0, [] (ShellProcessor& p) { set (p, params::morphGlobalCarrier, 1.0f); set (p, params::osc2Level, 100.0f); });
    extreme (examples[0], "A-cello-imprint-carrier-both", 0, [] (ShellProcessor& p) { set (p, params::morphGlobalCarrier, 2.0f); set (p, params::osc2Level, 100.0f); });
    // A sweep 0 -> 100 % for each method (the clock: transients and silences).
    for (int method = 0; method < 3; ++method)
    {
        static const char* names[] { "imprint", "fuse", "weave" };
        auto pOwned = std::make_unique<ShellProcessor>();
        auto& p = *pOwned;
        engineOf (p).setAuxLoopForTest (loop);
        morphToD (p, 3, 0.0f);
        set (p, params::morphGlobalMethod, static_cast<float> (method));
        auto layout = p.getBusesLayout();
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        REQUIRE (p.setBusesLayout (layout));
        p.prepareToPlay (rate, block);
        const int total = static_cast<int> (8.0 * rate);
        juce::AudioBuffer<float> audio (2, total), buffer (2, block);
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            buffer.setSize (2, n, false, false, true);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                    buffer.setSample (ch, i, static_cast<std::size_t> (pos + i) < celloIn.size() ? celloIn[static_cast<std::size_t> (pos + i)] : 0.0f);
            juce::MidiBuffer midi;
            if (pos == 0)
                for (const int note : { 48, 55, 60, 64 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)), 0);
            set (p, params::morphPosition, 100.0f * static_cast<float> (pos) / static_cast<float> (total));
            p.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                audio.copyFrom (ch, pos, buffer, ch, 0, n);
        }
        const auto file = juce::String ("gm2-sweep-cello-") + names[method];
        writeWav (out.getChildFile (file + ".wav"), audio);
        notes.add (file + ".wav: MORPH swept 0 -> 100 % over 8 s, the pad x the bowed stand-in");
    }
    out.getChildFile ("gm2-renders.txt").replaceWithText (notes.joinIntoString ("\n") + "\n");
}

TEST_CASE ("GLOBAL MORPH 2.0 CPU and latency: the whole instrument, 1 / 4 / 8 / 16 voices, at 44.1 / 48 / 96 kHz, MORPH TO C against MORPH TO D (WARN lines)", "[.][bench][gm2]")
{
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
        for (const int voices : { 1, 4, 8, 16 })
        {
            double seconds[2] {};
            int latency[2] {};
            for (int toD = 0; toD < 2; ++toD)
            {
                auto pOwned = std::make_unique<ShellProcessor>();
                auto& p = *pOwned;
                set (p, params::auxEnabled, 1.0f);
                set (p, params::auxMode, 1.0f);
                set (p, params::auxNoiseGate, 1.0f);
                if (toD == 1)
                {
                    set (p, params::morphTarget, 1.0f);
                    set (p, params::morphEnabled, 1.0f);
                    set (p, params::morphPosition, 50.0f);
                    set (p, params::morphGlobalMethod, 1.0f);   // FUSE (both sources reshaped: the costliest core)
                }
                p.prepareToPlay (sr, 256);
                const int total = static_cast<int> (4.0 * sr);
                juce::AudioBuffer<float> buffer (2, 256);
                const auto start = std::chrono::steady_clock::now();
                for (int pos = 0; pos < total; pos += 256)
                {
                    juce::MidiBuffer midi;
                    if (pos == 0)
                        for (int v = 0; v < voices; ++v)
                            midi.addEvent (juce::MidiMessage::noteOn (1, 36 + 3 * v, static_cast<juce::uint8> (100)), 0);
                    buffer.clear();
                    p.processBlock (buffer, midi);
                }
                seconds[toD] = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
                p.pollLoads();
                latency[toD] = engineOf (p).latencySamples();
            }
            WARN (sr << " Hz, " << voices << " voices: MORPH TO C " << 100.0 * seconds[0] / 4.0 << " % of a core, MORPH TO D (FUSE 50 %) "
                     << 100.0 * seconds[1] / 4.0 << " % (+" << 100.0 * (seconds[1] - seconds[0]) / 4.0 << "); latency " << latency[0] << " -> " << latency[1]
                     << " samples (" << 1000.0 * (latency[1] - latency[0]) / sr << " ms added)");
        }
}
