// ANDOR ANALOG through the ANDOR Instrument Shell: its parameters and patches, its factory
// presets, MIDI at its own samples, state recall, and the rule that every control the
// interface enables moves the sound while every one it disables does not.
// (Compiled into andor_analog_tests with the shell's own tests, tests/shell/ShellTests.cpp.)

#include "AnalogEngine.h"
#include "AnalogParameters.h"
#include "Binding.h"
#include "CeramicSkin.h"
#include "FactoryPatches.h"
#include "MacroPopups.h"
#include "ModulationPanel.h"
#include "ShellEditor.h"
#include "ShellProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <map>

using andor::shell::ShellProcessor;
using namespace andor::analog;
using Catch::Approx;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int block = 512;

    struct Note
    {
        int sample, note, velocity;   ///< velocity 0: off
    };

    /** The test phrase: a chord, a pitch bend, a legato line, releases. */
    const std::vector<Note> phrase { { 0, 57, 90 }, { 0, 64, 90 }, { 0, 69, 90 }, { 9000, 72, 90 }, { 14000, 57, 0 }, { 14000, 64, 0 },
                                     { 15000, 74, 90 }, { 16000, 72, 0 }, { 20000, 69, 0 }, { 24000, 74, 0 } };

    juce::AudioBuffer<float> renderShell (ShellProcessor& p, double seconds, const std::vector<Note>& notes = phrase, bool bend = true)
    {
        p.prepareToPlay (rate, block);
        const int total = static_cast<int> (seconds * rate);
        juce::AudioBuffer<float> out (2, total), buffer (2, block);
        for (int pos = 0; pos < total; pos += block)
        {
            juce::MidiBuffer midi;
            for (const auto& n : notes)
                if (n.sample >= pos && n.sample < pos + block)
                    midi.addEvent (n.velocity > 0 ? juce::MidiMessage::noteOn (1, n.note, static_cast<juce::uint8> (n.velocity))
                                                  : juce::MidiMessage::noteOff (1, n.note),
                                   n.sample - pos);
            if (bend && pos <= 6000 && 6000 < pos + block)
                midi.addEvent (juce::MidiMessage::pitchWheel (1, 12000), 6000 - pos);
            p.processBlock (buffer, midi);
            const int n = std::min (block, total - pos);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, buffer, ch, 0, n);
        }
        return out;
    }

    double differenceRms (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        double sum = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
            {
                const double d = a.getSample (ch, i) - b.getSample (ch, i);
                sum += d * d;
            }
        return std::sqrt (sum / (2.0 * a.getNumSamples()));
    }

    bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
                if (! juce::exactlyEqual (a.getSample (ch, i), b.getSample (ch, i)))
                    return false;
        return true;
    }

    void setValue (ShellProcessor& p, const juce::String& id, float value)
    {
        auto* param = p.parameters.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    void apply (ShellProcessor& p, const Patch& patch)
    {
        for (const auto& [id, value] : params::valuesOf (patch))
            setValue (p, id, value);
    }

    /** Everything that can be heard in play (both oscillators with every wave, Poly Mod on all
        three destinations, the LFO brought in, FILTER DRIVE, DRIVE, MOVEMENT, SPACE, ECHO), and
        for a control that acts only in a mode, that mode. */
    struct SoundContext
    {
        Patch patch;
        std::map<juce::String, std::function<void (Patch&)>> modes;
    };

    SoundContext soundContext()
    {
        Patch context;
        context.osc1Wave = Osc1Wave::sawPulse;
        context.osc2Wave = Osc2Wave::all;
        context.polyModEnv = 0.3;
        context.polyModOsc2 = 0.25;
        context.polyModToPitch1 = context.polyModToWidth1 = context.polyModToCutoff = true;
        context.lfoAmount = 0.6;
        context.lfoCutoff = 6.0;
        context.lfo.rateHz = 4.0;
        context.filterDrive = 0.3;
        context.drive = 0.3;
        context.motion = 0.6;
        context.space = 0.3;
        context.echo = 0.3;
        context.effects.echoTimeMs = 90.0;
        context.effects.echoDivision = 0;
        context.effects.shaper.pattern = 0;   // (SHAPER CUSTOM starts as pattern THREE: another one, so it is heard)
        context.ampRelease = 0.08;
        context.ampSustain = 0.6;   // (at 100 % the decay has nowhere to go)
        // A few controls act only in a mode: they are tried in it.
        auto movementMode = [] (osp::MovementMode m) { return [m] (Patch& c) { c.effects.movementMode = m; }; };
        auto reimaginedMode = [] (osp::ReimaginedMode m) {
            return [m] (Patch& c) {
                c.reimagined = 0.8;
                c.reimaginedSettings.mode = m;
            };
        };
        auto granular = [] (Patch& c) { c.granular = true; };
        std::map<juce::String, std::function<void (Patch&)>> modes {
            { params::unisonDetune, [] (Patch& c) { c.unison = true; } },
            { "glide", [] (Patch& c) { c.voiceMode = VoiceMode::mono; } },
            { "mod.lfo1.division", [] (Patch& c) { c.lfo.sync = true; } },
            { "mod.lfo1.phase", [] (Patch& c) { c.lfo.mode = osp::mod::LfoMode::retrigger; } },
            { "mod.lfo1.scope", [] (Patch& c) { c.lfo.shape = osp::mod::LfoShape::smoothRandom; } },   // (each note its own values)
            { "echo.time", [] (Patch& c) { c.effects.echoSync = false; } },
            { "layerA.reimagined.mode", reimaginedMode (osp::ReimaginedMode::kaleidoscope) },
            { "layerA.reimagined.kaleidoscope.focus", reimaginedMode (osp::ReimaginedMode::kaleidoscope) },
            { "layerA.reimagined.kaleidoscope.spread", reimaginedMode (osp::ReimaginedMode::kaleidoscope) },
            { "layerA.reimagined.tapeFrame.age", reimaginedMode (osp::ReimaginedMode::tapeFrame) },
            { "layerA.reimagined.tapeFrame.stability", reimaginedMode (osp::ReimaginedMode::tapeFrame) },
            { "layerA.reimagined.tapeFrame.frame", [] (Patch& c) {
                  c.granular = true;   // FRAME is the tape's length: heard as the grains follow it
                  c.reimagined = 0.8;
                  c.reimaginedSettings.mode = osp::ReimaginedMode::tapeFrame;
              } },
            { "layerA.reimagined.toybox.motion", reimaginedMode (osp::ReimaginedMode::toybox) },
            { "layerA.reimagined.toybox.digital", reimaginedMode (osp::ReimaginedMode::toybox) },
            { "layerA.reimagined.toybox.play", [] (Patch& c) {
                  c.granular = true;   // PLAY is how its heads move: heard as the grains follow them
                  c.reimagined = 0.8;
                  c.reimaginedSettings.mode = osp::ReimaginedMode::toybox;
              } },
            { "layerA.reimagined.mirage.clock", reimaginedMode (osp::ReimaginedMode::mirage) },
            { "layerA.reimagined.mirage.filter", reimaginedMode (osp::ReimaginedMode::mirage) },
            { "layerA.reimagined.mirage.tone", reimaginedMode (osp::ReimaginedMode::mirage) },
            { "layerA.granular.position", granular },
            { "layerA.granular.size", granular },
            { "layerA.granular.density", granular },
            { "layerA.granular.tune", granular },
            { "layerA.granular.spread", granular },
            { "movement.tape.wow", movementMode (osp::MovementMode::tape) },
            { "movement.tape.flutter", movementMode (osp::MovementMode::tape) },
            { "movement.tape.wear", movementMode (osp::MovementMode::tape) },
            { "movement.chorus.rate", movementMode (osp::MovementMode::chorus) },
            { "movement.chorus.width", movementMode (osp::MovementMode::chorus) },
            { "movement.chorus.stereo", movementMode (osp::MovementMode::chorus) },
            { "movement.pulse.rate", movementMode (osp::MovementMode::pulse) },
            { "movement.pulse.shape", movementMode (osp::MovementMode::pulse) },
            { "movement.pulse.stereo", movementMode (osp::MovementMode::pulse) },
            { "movement.shaper.pattern", movementMode (osp::MovementMode::shaper) },
            { "movement.shaper.rate", movementMode (osp::MovementMode::shaper) },
            { "movement.shaper.target", movementMode (osp::MovementMode::shaper) },
            { "movement.shaper.smooth", movementMode (osp::MovementMode::shaper) },
            { "movement.shaper.custom", movementMode (osp::MovementMode::shaper) },
        };
        // KEY DNA is GRANULAR's, and its settings act while it is on.
        for (const auto& id : { params::keyDna, params::keyDnaAmount, params::keyDnaIndividuality, params::keyDnaPosition, params::keyDnaSize,
                                params::keyDnaDensity, params::keyDnaSpread, params::keyDnaTune })
            modes[id] = granular;
        return { context, modes };
    }

    /** Every parameter value a patch sets, read back through the parameters, within their steps. */
    void checkSameValues (const std::map<juce::String, float>& a, const std::map<juce::String, float>& b, ShellProcessor& p)
    {
        REQUIRE (a.size() == b.size());
        for (const auto& [id, value] : a)
        {
            INFO (id);
            const auto range = p.parameters.getParameterRange (id);
            const float step = std::max (range.interval, 1.0e-4f * (range.end - range.start));
            CHECK (b.at (id) == Approx (value).margin (step));
        }
    }
}

TEST_CASE ("analog: the parameters' defaults are the INIT patch", "[analog][parameters]")
{
    ShellProcessor p;
    params::Reader reader;
    reader.attach (p.parameters);
    checkSameValues (params::valuesOf (Patch()), params::valuesOf (reader.read()), p);
}

TEST_CASE ("analog: value texts read as numbers, also where floating point leaves a midpoint near zero", "[analog][parameters]")
{
    // Apple Silicon computes the midpoint of -100..100 as -2.2e-6; it must read "0 ct", not
    // "2.23517e-06 ct" (the schema golden's 0.5 probe, and what a knob shows).
    ShellProcessor p;
    for (const auto& id : { params::osc2Fine, params::lfoWidth1, params::lfoWidth2, params::envDepth, params::lfoCutoff })
    {
        INFO (id);
        auto* param = p.parameters.getParameter (id);
        REQUIRE (param != nullptr);
        for (const float nearZero : { -2.23517e-6f, 0.0f, 1.0e-6f })
        {
            const auto text = param->getText (param->convertTo0to1 (nearZero), 0);
            CHECK_FALSE (text.containsChar ('e'));
            CHECK (text.startsWithChar ('0'));
        }
        CHECK (param->getText (1.0f, 0).startsWithChar ('+'));
        CHECK (param->getText (0.0f, 0).startsWithChar ('-'));
    }
}

TEST_CASE ("analog: every factory sound survives the trip through the parameters", "[analog][parameters]")
{
    ShellProcessor p;
    params::Reader reader;
    reader.attach (p.parameters);
    for (const auto& factory : factoryPatches())
    {
        INFO (factory.name);
        apply (p, factory.patch);
        checkSameValues (params::valuesOf (factory.patch), params::valuesOf (reader.read()), p);
    }
}

TEST_CASE ("analog: the factory presets are installed, listed and load exactly", "[analog][presets]")
{
    ShellProcessor p;
    const auto list = p.presetList();
    for (const auto& factory : factoryPatches())
    {
        INFO (factory.name);
        const auto entry = std::find_if (list.begin(), list.end(), [&] (const auto& e) { return e.name == juce::String (factory.name); });
        REQUIRE (entry != list.end());
        p.openPresetEntry (*entry);
        CHECK (p.presetDisplayName() == juce::String (factory.name));
        params::Reader reader;
        reader.attach (p.parameters);
        checkSameValues (params::valuesOf (factory.patch), params::valuesOf (reader.read()), p);
        // KEY DNA (not part of the patch): on, with the sound's variation, where it has one.
        const auto dna = reader.keyDna (factory.patch.seed);
        const auto wanted = keyDnaOf (factory);
        CHECK (dna.enabled == wanted.enabled);
        if (wanted.enabled)
            CHECK (dna[osp::keydna::Param::size].variation == Approx (wanted[osp::keydna::Param::size].variation).margin (1.0e-3));
    }
    // A second instance does not write them again (same version), and a musician's preset
    // beside them is kept.
    const auto own = p.presetFolder().getChildFile ("Mine.andoranalog");
    REQUIRE (p.savePreset (own));
    ShellProcessor q;
    CHECK (own.existsAsFile());
    CHECK (q.presetList().size() == list.size() + 1);
}

TEST_CASE ("analog: the plugin plays exactly what the engine renders from the same patch", "[analog][engine]")
{
    ShellProcessor p;
    apply (p, factoryPatches()[1].patch);
    const auto shell = renderShell (p, 0.6, phrase, false);

    params::Reader reader;
    reader.attach (p.parameters);
    Synth synth;
    synth.prepare (rate, block, 4);
    synth.setPatch (reader.read());
    synth.reset();
    juce::AudioBuffer<float> direct (2, shell.getNumSamples());
    for (int pos = 0; pos < direct.getNumSamples(); pos += block)
    {
        synth.setPatch (reader.read());
        synth.setTransport ({});
        int done = pos;
        const int end = std::min (pos + block, direct.getNumSamples());
        for (const auto& n : phrase)
            if (n.sample >= pos && n.sample < end)
            {
                if (n.sample > done)
                    synth.render (direct.getWritePointer (0) + done, direct.getWritePointer (1) + done, n.sample - done);
                done = n.sample;
                const std::uint8_t bytes[3] { static_cast<std::uint8_t> (n.velocity > 0 ? 0x90 : 0x80), static_cast<std::uint8_t> (n.note),
                                              static_cast<std::uint8_t> (n.velocity) };
                synth.handleMidi (bytes, 3);
            }
        synth.render (direct.getWritePointer (0) + done, direct.getWritePointer (1) + done, end - done);
    }
    CHECK (identical (shell, direct));
}

TEST_CASE ("analog: a note starts on its own sample inside a block; the latency is reported", "[analog][engine]")
{
    ShellProcessor p;
    setValue (p, "space", 0.0f);
    const auto out = renderShell (p, 0.1, { { 300, 69, 127 } }, false);
    int first = -1;
    for (int i = 0; i < out.getNumSamples() && first < 0; ++i)
        if (std::abs (out.getSample (0, i)) > 1.0e-6f)
            first = i;
    CHECK (p.getLatencySamples() > 0);
    CHECK (p.getLatencySamples() < 64);
    CHECK (first >= 300);
    CHECK (first <= 300 + p.getLatencySamples() + 4);
}

TEST_CASE ("analog: a session recalls the sound exactly", "[analog][state]")
{
    juce::MemoryBlock session;
    juce::AudioBuffer<float> before;
    {
        ShellProcessor p;
        apply (p, factoryPatches()[4].patch);
        setValue (p, params::resonance, 47.0f);
        before = renderShell (p, 0.5);
        p.getStateInformation (session);
    }
    ShellProcessor q;
    q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
    CHECK (q.parameters.getRawParameterValue (params::resonance)->load() == Approx (47.0f));
    CHECK (identical (before, renderShell (q, 0.5)));
}

TEST_CASE ("analog: the card shows the oscillators, and its picture follows them", "[analog][editor]")
{
    ShellProcessor p;
    osp::plugin::InstrumentHost::GeneratedSource a, b;
    REQUIRE (p.generatedSource (0, a));
    CHECK (a.name == "OSCILLATORS");
    setValue (p, params::osc1Wave, 1.0f);   // pulse
    REQUIRE (p.generatedSource (0, b));
    CHECK (a.revision != b.revision);
    CHECK (a.cycle != b.cycle);
}

TEST_CASE ("analog: GRANULAR's grains are shown on the card where they read, on POS's scale", "[analog][editor][granular]")
{
    ShellProcessor p;
    Patch patch;
    patch.granular = true;
    patch.grains.position = 0.6;
    patch.grains.spread = 0.0;
    patch.grains.density = 30.0;
    apply (p, patch);
    renderShell (p, 2.0, { { 0, 60, 100 } }, false);   // held past 1.5 s: the recording is full, its span steady
    const auto& grains = p.grainSnapshot (0);
    const int n = grains.count.load();
    REQUIRE (n > 3);
    for (int i = 0; i < n; ++i)
    {
        const auto k = static_cast<std::size_t> (i);
        INFO ("grain " << i);
        // SPREAD 0 leaves only the few milliseconds of jitter around POS (of the ~0.7 s span).
        CHECK (grains.position[k].load() == Approx (0.6f).margin (0.03f));
        CHECK (grains.level[k].load() >= 0.0f);
        CHECK (grains.level[k].load() <= 1.0f);
        CHECK (grains.lane[k].load() >= 0.0f);
        CHECK (grains.lane[k].load() < 1.0f);
    }
    // ONE SHOT: none.
    setValue (p, "layerA.sourceMode", 0.0f);
    renderShell (p, 0.3, { { 0, 60, 100 } }, false);
    CHECK (p.grainSnapshot (0).count.load() == 0);
}

TEST_CASE ("analog: every control the interface enables moves the sound; every one it disables does not", "[analog][bindings]")
{
    auto [context, modes] = soundContext();
    const juce::StringArray keyDnaOn { params::keyDnaAmount, params::keyDnaIndividuality, params::keyDnaPosition, params::keyDnaSize,
                                       params::keyDnaDensity, params::keyDnaSpread, params::keyDnaTune };

    // The arpeggiator's settings act while it plays (in 32nds: a few cycles in the phrase).
    // The modulation's sources and routes act through a route: each tried with one to TONE.
    std::map<juce::String, std::function<void (ShellProcessor&)>> setups;
    const int toneChoice = osp::mod::hostDestCount + 1;   // ANALOG's second destination (TONE)
    auto route = [&] (int slot, int source) {
        return [slot, source, toneChoice] (ShellProcessor& q) {
            setValue (q, osp::plugin::InstrumentHost::modRouteId (slot, "source"), static_cast<float> (source));
            setValue (q, osp::plugin::InstrumentHost::modRouteId (slot, "dest"), static_cast<float> (toneChoice));
            setValue (q, osp::plugin::InstrumentHost::modRouteId (slot, "depth"), 50.0f);
        };
    };
    for (const char* id : { "arp.pattern", "arp.rate", "arp.gate", "arp.octaves", "arp.swing", "arp.velocity" })
        setups[id] = [] (ShellProcessor& q) {
            setValue (q, "arp.enabled", 1.0f);
            setValue (q, "arp.rate", 3.0f);
        };
    for (const char* name : { "shape", "rate", "sync", "division", "phase", "polarity", "mode", "scope" })
        setups[osp::plugin::InstrumentHost::modLfoId (1, name)] = [route, name] (ShellProcessor& q) {
            route (0, 2) (q);   // LFO 2 -> TONE
            setValue (q, "mod.lfo2.rate", 4.0f);
            if (juce::String (name) == "division")
                setValue (q, "mod.lfo2.sync", 1.0f);
            if (juce::String (name) == "scope" || juce::String (name) == "phase")
                setValue (q, "mod.lfo2.mode", 1.0f);   // RETRIGGER: from PHASE; a later note starts its own
        };
    for (int e = 0; e < 2; ++e)
        for (const char* name : { "mode", "attack", "decay", "sustain", "release", "curve", "length" })
            setups[osp::plugin::InstrumentHost::modEnvId (e, name)] = [route, e, name] (ShellProcessor& q) {
                route (0, 3 + e) (q);   // ENV 1 / 2 -> TONE
                if (juce::String (name) == "length")
                    setValue (q, osp::plugin::InstrumentHost::modEnvId (e, "mode"), 1.0f);   // ONE SHOT: its curve over LENGTH
                setValue (q, osp::plugin::InstrumentHost::modEnvId (e, "length"), 400.0f);
            };
    for (int r = 0; r < osp::mod::maxRoutes; ++r)
        for (const char* name : { "source", "dest", "depth", "enabled" })
            setups[osp::plugin::InstrumentHost::modRouteId (r, name)] = route (r, 1);   // LFO 1 -> TONE

    const auto bound = params::engineParameterIds();
    auto isBound = [&bound] (const juce::String& id) {
        for (const auto& b : bound)
            if (osp::plugin::binding::matches (id, b))
                return true;
        return false;
    };
    ShellProcessor p;
    int checkedBound = 0, checkedUnbound = 0;
    for (auto* base : p.getParameters())
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (base);
        REQUIRE (param != nullptr);
        const auto id = param->getParameterID();
        INFO (id);
        auto patch = context;
        if (const auto mode = modes.find (id); mode != modes.end())
            mode->second (patch);
        p.resetSettings();
        apply (p, patch);
        if (keyDnaOn.contains (id))
            setValue (p, params::keyDna, 1.0f);
        if (const auto setup = setups.find (id); setup != setups.end())
            setup->second (p);
        const auto reference = renderShell (p, 0.6);

        // Another value: the far side of the range from where it is.
        const float now = param->getValue();
        float other = now < 0.5f ? 0.85f : 0.15f;
        if (id == params::polyphony)
            other = 0.0f;   // 1 voice (the chord is three notes)
        param->setValueNotifyingHost (other);
        const auto changed = renderShell (p, 0.6);
        if (isBound (id))
        {
            const double d = differenceRms (reference, changed);
            UNSCOPED_INFO (id << ": " << 20.0 * std::log10 (std::max (d, 1.0e-12)) << " dB");
            CHECK (d > 1.0e-5);
            ++checkedBound;
        }
        else
        {
            CHECK (identical (reference, changed));
            ++checkedUnbound;
        }
    }
    // Every listed one (the two wheels are not parameters), the arpeggiator's seven ("arp.*")
    // and the modulation's: LFO 2, ENV 1 and 2, the 16 routes ("mod.*"; LFO 1's are listed).
    CHECK (checkedBound == bound.size() - 4 + 7 + (8 + 2 * 7 + 4 * osp::mod::maxRoutes));
    CHECK (checkedUnbound > 150);
}

TEST_CASE ("analog: every modulation destination it offers moves the sound (VEL through a route)", "[analog][modulation]")
{
    // ANDOR ANALOG's own controls (after ANDOR OSP's in the routes' destination list), and
    // ANDOR OSP's destinations it plays, by the control that shows each.
    using D = osp::mod::Dest;
    ShellProcessor p;   // (its parameters are made with the product's destinations registered)
    std::vector<std::pair<osp::mod::Dest, juce::String>> dests;
    const auto& own = params::modulationDestinations();
    REQUIRE (osp::mod::productDestCount() == static_cast<int> (own.size()));
    for (std::size_t k = 0; k < own.size(); ++k)
        dests.emplace_back (osp::mod::productDest (static_cast<int> (k)), juce::String (own[k].id));
    for (const auto& [d, id] : std::initializer_list<std::pair<D, const char*>> {
             { D::drive, "drive" }, { D::movement, "motion" }, { D::space, "space" }, { D::echo, "echo" },
             { D::levelA, "layerA.level" }, { D::panA, "layerA.pan" }, { D::fineTuneA, "layerA.tune" }, { D::reimaginedA, "reimagined" },
             { D::grainPositionA, "layerA.granular.position" }, { D::grainDensityA, "layerA.granular.density" },
             { D::grainSizeA, "layerA.granular.size" }, { D::grainSpreadA, "layerA.granular.spread" },
             { D::ampAttack, "attack" }, { D::ampDecay, "decay" }, { D::ampSustain, "sustainLevel" }, { D::ampRelease, "release" } })
        dests.emplace_back (d, juce::String (id));

    auto [context, modes] = soundContext();
    modes["reimagined"] = [] (Patch& c) { c.reimaginedSettings.mode = osp::ReimaginedMode::kaleidoscope; };
    const auto vel = static_cast<float> (osp::mod::sourceIndex (osp::mod::Source::vel) + 1);
    for (const auto& [dest, id] : dests)
    {
        INFO (id << " (" << osp::mod::destInfo (dest).name << ")");
        auto patch = context;
        if (const auto mode = modes.find (id); mode != modes.end())
            mode->second (patch);
        p.resetSettings();
        apply (p, patch);
        const auto reference = renderShell (p, 0.6);
        // VEL (the notes at 90: 0.71) moves the control half its travel, towards where it has room.
        auto* param = p.parameters.getParameter (id);
        REQUIRE (param != nullptr);
        const float depth = param->getValue() > 0.5f ? -50.0f : 50.0f;
        setValue (p, osp::plugin::InstrumentHost::modRouteId (0, "source"), vel);
        setValue (p, osp::plugin::InstrumentHost::modRouteId (0, "dest"), static_cast<float> (osp::mod::choiceOf (dest)));
        setValue (p, osp::plugin::InstrumentHost::modRouteId (0, "depth"), depth);
        REQUIRE (p.modulationRoutes().size() == 1);
        CHECK (p.modulationRoutes().front().state == osp::mod::RouteState::active);
        const auto moved = renderShell (p, 0.6);
        const double d = differenceRms (reference, moved);
        UNSCOPED_INFO (20.0 * std::log10 (std::max (d, 1.0e-12)) << " dB");
        CHECK (d > 1.0e-5);
        // ... and it shows on the control: the destination maps back to it.
        CHECK (osp::plugin::modui::destinationFor (id, true) == dest);
    }
}

TEST_CASE ("analog: a modulated sound recalls exactly, and the routes' destinations keep their names", "[analog][modulation][state]")
{
    juce::MemoryBlock session;
    juce::AudioBuffer<float> before;
    const int tone = osp::mod::hostDestCount + 1;
    {
        ShellProcessor p;
        apply (p, factoryPatches()[2].patch);
        setValue (p, "mod.route1.source", 2.0f);   // LFO 2
        setValue (p, "mod.route1.dest", static_cast<float> (tone));
        setValue (p, "mod.route1.depth", 40.0f);
        setValue (p, "mod.lfo2.rate", 3.0f);
        before = renderShell (p, 0.5);
        p.getStateInformation (session);
        CHECK (osp::plugin::InstrumentHost::modDestinationNames()[tone] == "FILTER");
    }
    ShellProcessor q;
    q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
    CHECK (identical (before, renderShell (q, 0.5)));
}

TEST_CASE ("analog: a version-1 session or preset is migrated and sounds as it did", "[analog][state]")
{
    // Version 1: DRIVE was the filter's input drive, the LFO its own parameters, and ANDOR OSP's
    // MOVEMENT, REIMAGINED and GRANULAR were stored at their defaults, unheard.
    juce::MemoryBlock session;
    {
        ShellProcessor old;
        setValue (old, "drive", 40.0f);
        setValue (old, params::lfoWave, 4.0f);       // S&H
        setValue (old, params::lfoRate, 7.0f);
        setValue (old, params::lfoSync, 1.0f);
        setValue (old, params::lfoDivision, 3.0f);   // 1 bar
        setValue (old, params::motion, 30.0f);       // the LFO's amount
        setValue (old, "motion", 12.0f);
        setValue (old, "layerA.sourceMode", 1.0f);
        old.getStateInformation (session);
    }
    auto xml = juce::AudioProcessor::getXmlFromBinary (session.getData(), static_cast<int> (session.getSize()));
    REQUIRE (xml != nullptr);
    xml->removeAttribute ("productVersion");   // as version 1 wrote it
    juce::MemoryBlock v1;
    juce::AudioProcessor::copyXmlToBinary (*xml, v1);

    ShellProcessor p;
    p.setStateInformation (v1.getData(), static_cast<int> (v1.getSize()));
    auto value = [&p] (const juce::String& id) { return p.parameters.getRawParameterValue (id)->load(); };
    CHECK (value (params::filterDrive) == Approx (40.0f));
    CHECK (value ("drive") == 0.0f);
    CHECK (value ("mod.lfo1.shape") == 5.0f);     // RANDOM
    CHECK (value ("mod.lfo1.rate") == Approx (7.0f).epsilon (0.01));
    CHECK (value ("mod.lfo1.sync") == 1.0f);
    CHECK (value ("mod.lfo1.division") == 5.0f);  // 1 bar
    CHECK (value (params::motion) == Approx (30.0f));
    CHECK (value ("motion") == 0.0f);
    CHECK (value ("layerA.sourceMode") == 0.0f);

    // Today's state is read as it is (no migration).
    juce::MemoryBlock now;
    p.getStateInformation (now);
    ShellProcessor q;
    setValue (q, "motion", 50.0f);
    q.setStateInformation (now.getData(), static_cast<int> (now.getSize()));
    CHECK (q.parameters.getRawParameterValue ("motion")->load() == 0.0f);
    setValue (q, "motion", 50.0f);
    juce::MemoryBlock withMovement;
    q.getStateInformation (withMovement);
    ShellProcessor r;
    r.setStateInformation (withMovement.getData(), static_cast<int> (withMovement.getSize()));
    CHECK (r.parameters.getRawParameterValue ("motion")->load() == Approx (50.0f));
}

TEST_CASE ("analog: a version-2 session's BLEND moves into the oscillators' levels and sounds as it did", "[analog][state]")
{
    // Version 2 mixed the oscillators with BLEND; version 3 with their LEVELs (BLEND at 50 %).
    juce::MemoryBlock session;
    juce::AudioBuffer<float> before;
    {
        ShellProcessor old;
        apply (old, factoryPatches()[1].patch);
        setValue (old, params::blend, 75.0f);
        setValue (old, params::osc1Level, 90.0f);
        setValue (old, params::osc2Level, 80.0f);
        setValue (old, "space", 0.0f);
        before = renderShell (old, 0.5, phrase, false);
        old.getStateInformation (session);
    }
    auto xml = juce::AudioProcessor::getXmlFromBinary (session.getData(), static_cast<int> (session.getSize()));
    REQUIRE (xml != nullptr);
    xml->setAttribute ("productVersion", 2);   // as version 2 wrote it
    juce::MemoryBlock v2;
    juce::AudioProcessor::copyXmlToBinary (*xml, v2);

    ShellProcessor p;
    p.setStateInformation (v2.getData(), static_cast<int> (v2.getSize()));
    auto value = [&p] (const juce::String& id) { return p.parameters.getRawParameterValue (id)->load(); };
    CHECK (value (params::blend) == Approx (50.0f));
    double l1 = 0.9, l2 = 0.8;
    foldBlend (0.75, l1, l2);
    CHECK (value (params::osc1Level) == Approx (100.0 * l1).margin (0.1));
    CHECK (value (params::osc2Level) == Approx (100.0 * l2).margin (0.1));
    CHECK (value (params::osc2Level) == Approx (80.0f));   // (at 75 % OSC 2 was at its full level)
    // The same sound (the levels' steps of 0.1 % aside).
    const auto after = renderShell (p, 0.5, phrase, false);
    double sum = 0.0, diff = 0.0;
    for (int i = 0; i < before.getNumSamples(); ++i)
    {
        sum += static_cast<double> (before.getSample (0, i)) * before.getSample (0, i);
        const double d = before.getSample (0, i) - after.getSample (0, i);
        diff += d * d;
    }
    CHECK (std::sqrt (diff / std::max (sum, 1.0e-12)) < 0.01);   // within 1 % (-40 dB)
}

TEST_CASE ("analog: the card's first knob is DETUNE, and its name opens the oscillators", "[analog][editor]")
{
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    ui.refreshNow();
    auto& card = ui.engineCard (0);
    // Its dial is OSC 2's fine tuning (a modulation drop target), shown enabled.
    juce::Slider* detune = nullptr;
    std::function<void (juce::Component&)> find = [&] (juce::Component& c) {
        for (auto* child : c.getChildren())
        {
            if (auto* s = dynamic_cast<juce::Slider*> (child); s != nullptr && s->getProperties()["paramId"].toString() == params::osc2Fine)
                detune = s;
            find (*child);
        }
    };
    find (card);
    REQUIRE (detune != nullptr);
    CHECK (detune->isEnabled());
    setValue (p, params::osc2Fine, -12.0f);
    CHECK (detune->getValue() == Approx (-12.0));
    // Its name opens the oscillators' popover; the first macro has none and is disabled.
    ui.openPopup (andor::shell::ShellEditor::startControlPopup);
    REQUIRE (ui.openPopupPanel() != nullptr);
    CHECK (ui.openPopupPanel()->getTitle().isEmpty() == false);
    ui.closePopup();
    ui.openPopup (0);
    CHECK (ui.openPopupPanel() == nullptr);
}

TEST_CASE ("analog: SHAPER's custom pattern is played, saved with the session and recalled", "[analog][state][movement]")
{
    ShellProcessor p;
    setValue (p, "space", 0.0f);
    setValue (p, "motion", 100.0f);
    setValue (p, "movement.mode", 4.0f);   // SHAPER
    setValue (p, "movement.shaper.custom", 1.0f);
    const auto library = renderShell (p, 0.8);
    osp::ShaperPattern pattern {};
    for (std::size_t i = 0; i < pattern.size(); ++i)
        pattern[i] = { i % 2 == 0 ? 1.0f : 0.0f, i % 2 == 0 ? 1.0f : 0.0f, osp::StepShape::hold };
    p.setShaperCustomPattern (pattern, false);
    const auto custom = renderShell (p, 0.8);
    CHECK (differenceRms (library, custom) > 1.0e-5);

    juce::MemoryBlock session;
    p.getStateInformation (session);
    ShellProcessor q;
    q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
    CHECK (q.shaperCustomPattern() == pattern);
}

TEST_CASE ("analog: DRIVE, MOVEMENT, SPACE and ECHO open ANDOR OSP's popovers; REIMAGINED opens from the card", "[analog][editor]")
{
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    // ANDOR OSP's own panels (same sizes as in ANDOR OSP, from the same code).
    for (const auto& [macro, popup] : { std::pair { 1, osp::plugin::MacroPopup::drive }, std::pair { 3, osp::plugin::MacroPopup::movement },
                                        std::pair { 4, osp::plugin::MacroPopup::space }, std::pair { 5, osp::plugin::MacroPopup::echo } })
    {
        INFO ("macro " << macro);
        ui.openPopup (macro);
        REQUIRE (ui.openPopupPanel() != nullptr);
        CHECK (ui.openPopupPanel()->cardSize() == osp::plugin::createMacroPopup (popup, p)->cardSize());
        ui.closePopup();
    }
    // REIMAGINED: the card's name opens ANDOR OSP's popover (the engine reads the amount).
    auto& card = ui.engineCard (0);
    REQUIRE (card.onReimagined != nullptr);
    card.onReimagined (0);
    CHECK (ui.openPopupIndex() == andor::shell::ShellEditor::reimaginedPopup);
    REQUIRE (ui.openPopupPanel() != nullptr);
    ui.closePopup();
    // MOSAIC says why it leaves the sound as it is; every other mode plays.
    CHECK (p.reimaginedModeUnavailable (0, static_cast<int> (osp::ReimaginedMode::mosaic)).isNotEmpty());
    for (const auto mode : { osp::ReimaginedMode::kaleidoscope, osp::ReimaginedMode::tapeFrame, osp::ReimaginedMode::toybox, osp::ReimaginedMode::mirage })
        CHECK (p.reimaginedModeUnavailable (0, static_cast<int> (mode)).isEmpty());
    // The modulation panel's LFO 1 is the synth's LFO: its controls are enabled.
    ui.modulationPanel().showTab (2);
    int lfoControls = 0, enabled = 0;
    std::function<void (juce::Component&)> visit = [&] (juce::Component& c) {
        for (auto* child : c.getChildren())
        {
            if (const auto tags = osp::plugin::binding::tagsOf (*child); tags.joinIntoString (" ").contains ("mod.lfo1."))
            {
                ++lfoControls;
                enabled += child->isEnabled() ? 1 : 0;
            }
            visit (*child);
        }
    };
    ui.refreshNow();
    visit (ui.modulationPanel());
    CHECK (lfoControls > 0);
    CHECK (enabled > 0);
}

TEST_CASE ("analog: screenshots of the instrument and its six popovers (ANALOG_SCREENSHOT_DIR)", "[.][screenshots]")
{
    const char* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    const auto before = osp::plugin::skin::look();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    auto shoot = [&] (const juce::String& name, float scale = 1.0f) {
        ui.refreshNow();
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
        const auto file = out.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    editor->setSize (1448, 1086);
    shoot ("analog-init");
    shoot ("analog-init@2x", 2.0f);
    const char* names[] = { "unassigned", "drive", "filter", "movement", "space", "echo" };
    for (int m = 1; m < 6; ++m)
    {
        ui.openPopup (m);
        REQUIRE (ui.openPopupPanel() != nullptr);
        shoot (juce::String ("analog-popover-") + names[m]);
        ui.closePopup();
    }
    ui.openPopup (andor::shell::ShellEditor::startControlPopup);
    shoot ("analog-popover-oscillators");
    ui.closePopup();
    ui.openPopup (andor::shell::ShellEditor::advancedPopup);
    shoot ("analog-advanced");
    ui.closePopup();
    // REIMAGINED (KALEIDOSCOPE, then MOSAIC, which a synth cannot play) and GRANULAR on the card.
    setValue (p, "reimagined", 60.0f);
    ui.openPopup (andor::shell::ShellEditor::reimaginedPopup);
    shoot ("analog-reimagined-kaleidoscope");
    ui.closePopup();
    setValue (p, "layerA.reimagined.mode", 3.0f);
    ui.openPopup (andor::shell::ShellEditor::reimaginedPopup);
    shoot ("analog-reimagined-mosaic");
    ui.closePopup();
    setValue (p, "layerA.reimagined.mode", 0.0f);
    setValue (p, "reimagined", 0.0f);
    setValue (p, "layerA.sourceMode", 1.0f);
    ui.engineCard (0).showGranularControls (true, true);
    shoot ("analog-granular");
    {
        // Playing: POS, the SPREAD pane and the grains of a held chord.
        setValue (p, "layerA.granular.spread", 40.0f);
        setValue (p, "layerA.granular.density", 30.0f);
        renderShell (p, 1.6, { { 0, 57, 100 }, { 0, 64, 100 }, { 0, 69, 100 } }, false);
        shoot ("analog-granular-playing");
        shoot ("analog-granular-playing@2x", 2.0f);
        ui.engineCard (0).showGranularControls (false, true);
        shoot ("analog-granular-playing-card");
    }
    setValue (p, "layerA.sourceMode", 0.0f);
    ui.modulationPanel().showTab (2);
    shoot ("analog-lfo1");
    ui.modulationPanel().showTab (0);
    // A route: LFO 2 on TONE (its halo on the macro), and the arpeggiator open and running.
    setValue (p, "mod.route1.source", 2.0f);
    setValue (p, "mod.route1.dest", static_cast<float> (osp::mod::hostDestCount + 1));
    setValue (p, "mod.route1.depth", 45.0f);
    ui.modulationPanel().showTab (3);
    shoot ("analog-route-lfo2-tone");
    ui.modulationPanel().showTab (0);
    setValue (p, "arp.enabled", 1.0f);
    renderShell (p, 0.8, { { 0, 57, 100 }, { 0, 64, 100 }, { 0, 69, 100 } }, false);
    ui.setArpExpanded (true);
    shoot ("analog-arp");
    ui.setArpExpanded (false);
    setValue (p, "arp.enabled", 0.0f);
    // A sound with character on the card: Sync Bite's picture, keys held.
    apply (p, factoryPatches()[8].patch);
    p.keyboardState.noteOn (1, 60, 0.8f);
    p.keyboardState.noteOn (1, 64, 0.8f);
    shoot ("analog-sync-bite-keys");
    editor->setSize (869, juce::roundToInt (869.0f * 1086.0f / 1448.0f));
    shoot ("analog-window-869");
    osp::plugin::skin::setLook (before);
}
