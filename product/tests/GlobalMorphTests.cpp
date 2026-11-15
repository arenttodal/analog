// GLOBAL MORPH 2.0 (docs/GLOBAL_MORPH_2.md). Gate 1: D's path measured stage by stage - the
// source, D's FILTER, its LEVEL, the global MORPH stage's D input and output, the FX's input - in
// every source mode, with real audio, before anything new is built on it.
#include "AnalogEngine.h"
#include "AnalogParameters.h"
#include "ExternalInstrument.h"
#include "ShellProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
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
            const bool oldSilent = view.activity < 0.05f;   // (the display's old test for "D is silent")
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

    /** The difference between two renders, relative to the first (dB): how much D changed it. */
    double changeDb (const juce::AudioBuffer<float>& reference, const juce::AudioBuffer<float>& other, int from, int to)
    {
        double diff = 0.0, ref = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = from; i < to; ++i)
            {
                const double d = static_cast<double> (other.getSample (ch, i)) - reference.getSample (ch, i);
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
        ShellProcessor p;
        synthOnly = measure (p, {});
    }

    SECTION ("RHYTHM, a loaded loop, GATE KEYS (the default): keys held - D plays and reaches the stage")
    {
        ShellProcessor p;
        engineOf (p).setAuxLoopForTest (loop);
        morphToD (p, 0, 50.0f);
        const auto m = measure (p, {});
        keep ("rhythm keys held, MORPH TO D 50 %", m);
        CHECK (m.tap (Tap::source) > 0.1f);
        CHECK (m.tap (Tap::level) > 0.05f);
        CHECK (m.tap (Tap::morphIn) > 0.05f);
        CHECK (m.stageBlocks > 0);
        CHECK (changeDb (synthOnly.out, m.out, 0, static_cast<int> (2.5 * rate)) > -20.0);   // (D changes the sound)
        // The repair: while keys are held and the loop plays, D is never reported silent.
        CHECK (m.in (State::silent) == 0);
        CHECK (m.in (State::waitingForKeys) <= 2);   // (after the release only: the fall, then waiting)
    }
    SECTION ("RHYTHM, keys held, MORPH 100 %: the loop itself, its silences included - never the synth between the hits")
    {
        ShellProcessor p;
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
        ShellProcessor p;
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
        ShellProcessor p;
        engineOf (p).setAuxLoopForTest (loop);
        morphToD (p, 0, 50.0f);
        set (p, params::auxRhythmGate, 1.0f);   // FREE
        Run run;
        run.transport = &transport;
        const auto stopped = measure (p, run);
        keep ("rhythm free, transport stopped", stopped);
        CHECK (stopped.in (State::waitingForTransport) == stopped.blocks);
        CHECK (engineOf (p).auxStatus().label.endsWith ("WAITING FOR TRANSPORT"));
        CHECK (changeDb (synthOnly.out, stopped.out, 0, static_cast<int> (2.5 * rate)) < -60.0);   // (D unavailable: the synth as it is)
        transport.playing = true;
        const auto playing = measure (p, run);
        keep ("rhythm free, transport playing", playing);
        CHECK (playing.tap (Tap::morphIn) > 0.05f);
        CHECK (playing.in (State::silent) == 0);
    }
    SECTION ("RHYTHM at MORPH 0 %: D plays into the stage's place but is not heard; never 'silent'")
    {
        ShellProcessor p;
        engineOf (p).setAuxLoopForTest (loop);
        morphToD (p, 0, 0.0f);
        const auto m = measure (p, {});
        keep ("rhythm keys held, MORPH TO D 0 %", m);
        CHECK (m.tap (Tap::level) > 0.05f);   // (D sounds at its level...)
        CHECK (changeDb (synthOnly.out, m.out, 0, static_cast<int> (2.5 * rate)) < -60.0);   // (...and 0 % is the synth: D does not leak)
        CHECK (m.in (State::silent) == 0);
    }
    SECTION ("NOISE (keyed and FREE), KONTAKT (the test instrument), LIVE INPUT: each reaches the stage")
    {
        {
            ShellProcessor p;
            morphToD (p, 1, 50.0f);
            const auto m = measure (p, {});
            keep ("noise keyed, keys held", m);
            CHECK (m.tap (Tap::morphIn) > 0.01f);
            CHECK (m.in (State::silent) == 0);
        }
        {
            ShellProcessor p;
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
            ShellProcessor p;
            morphToD (p, 2, 50.0f);
            REQUIRE (engineOf (p).auxInstrument().loadKontakt());
            const auto m = measure (p, {});
            keep ("kontakt (test instrument), keys held", m);
            CHECK (m.tap (Tap::morphIn) > 0.01f);
            CHECK (changeDb (synthOnly.out, m.out, 0, static_cast<int> (2.5 * rate)) > -20.0);
        }
        {
            ShellProcessor p;
            morphToD (p, 3, 50.0f);
            Run run;
            run.sidechain = &input;
            const auto m = measure (p, run);
            keep ("live input (sawtooth on the sidechain)", m);
            CHECK (m.tap (Tap::source) > 0.1f);
            CHECK (m.tap (Tap::morphIn) > 0.05f);
            CHECK (m.in (State::silent) == 0);
            ShellProcessor q;
            morphToD (q, 3, 50.0f);
            const auto none = measure (q, {});
            keep ("live input, nothing on the sidechain", none);
            CHECK (none.in (State::noLiveInput) == none.blocks);
            CHECK (engineOf (q).auxStatus().label.endsWith ("NO INPUT"));
        }
    }
    SECTION ("Power, MUTE, SOLO: said as such")
    {
        ShellProcessor p;
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
            ShellProcessor p;
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
        const double zeroDb = changeDb (synth.out, zero.out, from, to), fullDb = changeDb (dItself.out, full.out, from, to);
        const double halfFromSynth = changeDb (synth.out, half.out, from, to), halfFromD = changeDb (dItself.out, half.out, from, to);
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
        ShellProcessor p;
        write ("gm2-g1-1-synth", measure (p, run).out);
    }
    {
        ShellProcessor p;
        engineOf (p).setAuxLoopForTest (loop);
        morphToD (p, 0, 0.0f);
        set (p, params::morphEnabled, 0.0f);
        set (p, params::auxSolo, 1.0f);
        write ("gm2-g1-2-d-itself", measure (p, run).out);
    }
    for (const float position : { 0.0f, 50.0f, 100.0f })
    {
        ShellProcessor p;
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
