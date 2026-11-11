// EXPERIMENT: Kontakt as card C's EXTERNAL SoundSource (docs/experiments/KONTAKT_EXTERNAL_MORPH.md).
// Kontakt is not installed here: these host "ANDOR Test Instrument" (tests/surrogate), a small
// VST3 built for the purpose, through the same VST3 path. Kontakt itself: BLOCKED (see the doc).

#include "ExternalInstrument.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
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
