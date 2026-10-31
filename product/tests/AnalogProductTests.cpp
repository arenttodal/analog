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
#include "OspLookAndFeel.h"
#include "ModulationPanel.h"
#include "ShellEditor.h"
#include "ShellProcessor.h"

#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "io/AudioFileIO.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
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

    double rmsOf (const juce::AudioBuffer<float>& a)
    {
        double sum = 0.0;
        for (int i = 0; i < a.getNumSamples(); ++i)
            sum += static_cast<double> (a.getSample (0, i)) * a.getSample (0, i);
        return std::sqrt (sum / std::max (1, a.getNumSamples()));
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

    /** A recording for ORGANIC (ANDOR OSP's test vowel), written to `dir`. */
    juce::File writeRecording (const juce::File& dir, const juce::String& name, int midi, double seconds)
    {
        const auto file = dir.getChildFile (name);
        std::string error;
        REQUIRE (osp::io::writeAudioFile (std::filesystem::path (file.getFullPathName().toStdString()),
                                          osp::testsignals::vowel (osp::midiToHz (midi), seconds, 48000.0, 3), osp::io::SampleFormat::pcm24, error));
        return file;
    }

    struct Scratch
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("andor-analog-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        Scratch() { dir.createDirectory(); }
        ~Scratch() { dir.deleteRecursively(); }
    };

    /** Waits until MORPH's analysis of the recording reached the audio thread (blocks are
        rendered meanwhile: the hand-over happens in one). */
    void waitForMorph (ShellProcessor& p)
    {
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer none;
        p.prepareToPlay (48000.0, 256);
        for (int i = 0; i < 600 && p.engineStatus ("morph").isNotEmpty(); ++i)
        {
            juce::Thread::sleep (50);
            p.processBlock (buffer, none);
        }
        REQUIRE (p.engineStatus ("morph").isEmpty());
        p.processBlock (buffer, none);   // (the profile taken by the audio thread)
    }

    /** Loads a recording into ORGANIC (layer B), waits for it and for MORPH's analysis. */
    void loadOrganic (ShellProcessor& p, const juce::File& file)
    {
        REQUIRE (p.addLayers ({ file }) == 1);
        REQUIRE (p.waitForLoads (60000));
        p.pollLoads();
        REQUIRE (p.loadState (1) == osp::plugin::InstrumentHost::LoadState::ready);
        waitForMorph (p);
    }

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
        // State version 4: each model's settings act in that model; PWM depth with a source
        // (and the source with a depth); SUPER SAW's spread on SUPER SAW; the OTA's voicing and
        // slope on the OTA; the Juno variant through the high-pass; the chorus' hiss with it on.
        auto model = [] (int slot, OscModel m) { return [slot, m] (Patch& c) { (slot == 1 ? c.osc1Model : c.osc2Model) = m; }; };
        for (int slot = 1; slot <= 2; ++slot)
        {
            const bool first = slot == 1;
            for (const auto& id : { first ? params::osc1DcoSaw : params::osc2DcoSaw, first ? params::osc1DcoPulse : params::osc2DcoPulse,
                                    first ? params::osc1DcoSub : params::osc2DcoSub })
                modes[id] = model (slot, OscModel::dco);
            modes[first ? params::osc1DcoPwmSource : params::osc2DcoPwmSource] = [slot] (Patch& c) {
                (slot == 1 ? c.osc1Model : c.osc2Model) = OscModel::dco;
                auto& d = slot == 1 ? c.dco1 : c.dco2;
                d.pulse = true;
                d.pwmDepth = 0.6;
            };
            modes[first ? params::osc1DcoPwmDepth : params::osc2DcoPwmDepth] = [slot] (Patch& c) {
                (slot == 1 ? c.osc1Model : c.osc2Model) = OscModel::dco;
                auto& d = slot == 1 ? c.dco1 : c.dco2;
                d.pulse = true;
                d.pwmSource = PwmSource::lfo;
            };
            modes[first ? params::osc1WideWave : params::osc2WideWave] = model (slot, OscModel::wide);
            modes[first ? params::osc1WideShape : params::osc2WideShape] = model (slot, OscModel::wide);
            modes[first ? params::osc1WideSpread : params::osc2WideSpread] = [slot] (Patch& c) {
                (slot == 1 ? c.osc1Model : c.osc2Model) = OscModel::wide;
                (slot == 1 ? c.wide1 : c.wide2).wave = WideWave::superSaw;
            };
        }
        modes[params::noiseColor] = [] (Patch& c) { c.noiseLevel = 0.5; };
        modes[params::otaCharacter] = [] (Patch& c) {
            c.filterModel = FilterModel::ota;
            c.resonance = 0.6;
        };
        modes[params::otaSlope] = [] (Patch& c) { c.filterModel = FilterModel::ota; };
        modes[params::junoVariant] = [] (Patch& c) {
            c.highPass = 2;
            c.filterModel = FilterModel::ota;
        };
        modes[params::chorusNoise] = [] (Patch& c) { c.chorusMode = 1; };
        // MORPH (a recording loaded and analysed by the test): on, half way.
        for (const auto& id : { params::morphEnabled, params::morphMethod, params::morphCarrier, params::morphTexture, params::morphAttack,
                                params::morphFormants, params::morphDetail, params::morphLevelMatch, params::morphOutput, params::morphPosition })
            modes[id] = [id] (Patch& c) {
                c.morph.enabled = true;
                c.morph.position = id == params::morphTexture || id == params::morphAttack ? 0.3 : 0.5;
            };

        // KEY DNA is GRANULAR's, and its AMOUNT acts while it is on (the test switches it on).
        modes[osp::plugin::InstrumentHost::keyDnaAmountId (0)] = granular;
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
    auto init = params::valuesOf (Patch()), read = params::valuesOf (reader.read());
    // The one exception: the A/B mix is ANDOR OSP's parameter, its default all A (one layer:
    // not heard). The INIT patch's 0.5 is where the shell puts it when a recording joins.
    CHECK (read.at ("ab.blend") == 0.0f);
    CHECK (init.at ("ab.blend") == 0.5f);
    init.erase ("ab.blend");
    read.erase ("ab.blend");
    checkSameValues (init, read, p);
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
        // KEY DNA (not part of the patch; ANDOR OSP's, in the preset's KeyDna tree): on, with
        // the sound's own profile, where it has one; otherwise off.
        const auto dna = p.keyDnaSettings (0);
        if (factory.keyDna > 0.0)
        {
            auto wanted = keyDnaOf (factory);
            wanted.amount = 1.0;
            CHECK (dna == wanted);
        }
        else
            CHECK_FALSE (dna.enabled);
        CHECK (p.parameterValue (osp::plugin::InstrumentHost::keyDnaAmountId (0)) == 100.0f);
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
    const juce::StringArray keyDnaOn { osp::plugin::InstrumentHost::keyDnaAmountId (0) };

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

    // Each card's EQ and DAMPEN (ANDOR OSP's): a band's settings act while the EQ and that band
    // are on (a shelf or bell with gain); DAMPEN's while it is on (its corner in its own mode).
    using Host = osp::plugin::InstrumentHost;
    for (int layer = 0; layer < 2; ++layer)
    {
        for (const auto& name : Host::eqParameterNames())
            setups[Host::eqParameterId (layer, name)] = [layer, name] (ShellProcessor& q) {
                const auto band = name == "enabled" ? juce::String ("bell") : name.upToFirstOccurrenceOf (".", false, false);
                setValue (q, Host::eqParameterId (layer, "enabled"), 1.0f);
                setValue (q, Host::eqParameterId (layer, band + ".enabled"), 1.0f);
                setValue (q, Host::eqParameterId (layer, band + ".frequency"), 2000.0f);
                if (q.parameters.getParameter (Host::eqParameterId (layer, band + ".gain")) != nullptr)
                    setValue (q, Host::eqParameterId (layer, band + ".gain"), 12.0f);
                if (name == "enabled")
                    setValue (q, Host::eqParameterId (layer, "enabled"), 0.0f);   // (then switched)
                else if (name.endsWith (".enabled"))
                    setValue (q, Host::eqParameterId (layer, name), 0.0f);
            };
        for (const char* name : { "mode", "amount", "frequency", "cutoff" })
            setups[Host::dampenParameterId (layer, name)] = [layer, name] (ShellProcessor& q) {
                setValue (q, Host::dampenParameterId (layer, "enabled"), 1.0f);
                setValue (q, Host::dampenParameterId (layer, "amount"), 100.0f);
                setValue (q, Host::dampenParameterId (layer, "cutoff"), 500.0f);
                setValue (q, Host::dampenParameterId (layer, "mode"), juce::String (name) == "cutoff" ? 1.0f : 0.0f);
            };
    }

    const auto bound = params::engineParameterIds();
    auto isBound = [&bound] (const juce::String& id) {
        for (const auto& b : bound)
            if (osp::plugin::binding::matches (id, b))
                return true;
        return false;
    };
    ShellProcessor p;
    // ORGANIC's card controls act on a recording: one is loaded (long enough to sustain, so
    // ANDOR OSP's continuation marks loops for LOOP).
    Scratch scratch;
    loadOrganic (p, writeRecording (scratch.dir, "organic.wav", 57, 2.5));
    // LOOP acts where the recording would end under a held note: START near its end.
    setups["layerB.loop"] = [] (ShellProcessor& q) {
        setValue (q, "layerB.start", 50.0f);
        setValue (q, "layerB.tune", 12.0f);   // (twice as fast: the held notes reach the loop)
    };
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
        {
            osp::keydna::Settings dna;   // (ANDOR OSP's editor's settings: on, each key its own grains)
            dna.enabled = true;
            for (const auto param : { osp::keydna::Param::position, osp::keydna::Param::size, osp::keydna::Param::density })
                dna[param].variation = 0.6;
            p.setKeyDnaSettings (0, dna, "KEY DNA", false);
        }
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
    CHECK (checkedUnbound > 100);
}

TEST_CASE ("analog: every modulation destination it offers moves the sound (VEL through a route)", "[analog][modulation]")
{
    // ANDOR ANALOG's own controls (after ANDOR OSP's in the routes' destination list), and
    // ANDOR OSP's destinations it plays, by the control that shows each.
    using D = osp::mod::Dest;
    ShellProcessor p;   // (its parameters are made with the product's destinations registered)
    Scratch scratch;   // (MORPH's destinations act on a recording)
    loadOrganic (p, writeRecording (scratch.dir, "organic.wav", 57, 2.5));
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
        // (A destination whose control the engine does not play yet stays disabled; the
        // bindings test proves it inert.)
        if (! params::engineParameterIds().contains (id))
            continue;
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

TEST_CASE ("analog: a version-5 session's KEY DNA moves into ANDOR OSP's editor and sounds as it did", "[analog][state][keydna]")
{
    // Version 5 kept KEY DNA in analog.keyDna.* (seeded by the patch's seed); version 6 in
    // ANDOR OSP's KEY DNA (the session's KeyDna tree, AMOUNT layerA.keyDna.amount).
    auto prepare = [] (ShellProcessor& q) {
        apply (q, factoryPatches()[1].patch);
        setValue (q, "space", 0.0f);
        setValue (q, "echo", 0.0f);
        setValue (q, "layerA.sourceMode", 1.0f);   // GRANULAR
        setValue (q, "seed", 4242.0f);
    };
    juce::MemoryBlock session;
    {
        ShellProcessor old;
        prepare (old);
        setValue (old, params::keyDna, 1.0f);
        setValue (old, params::keyDnaAmount, 70.0f);
        setValue (old, params::keyDnaIndividuality, 80.0f);
        setValue (old, params::keyDnaPosition, 60.0f);
        setValue (old, params::keyDnaSize, 45.0f);
        setValue (old, params::keyDnaDensity, 30.0f);
        setValue (old, params::keyDnaSpread, 20.0f);
        setValue (old, params::keyDnaTune, 10.0f);
        old.getStateInformation (session);
    }
    auto xml = juce::AudioProcessor::getXmlFromBinary (session.getData(), static_cast<int> (session.getSize()));
    REQUIRE (xml != nullptr);
    xml->setAttribute ("productVersion", 5);   // as version 5 wrote it: no KeyDna tree
    xml->deleteAllChildElementsWithTagName ("KeyDna");
    juce::MemoryBlock v5;
    juce::AudioProcessor::copyXmlToBinary (*xml, v5);

    // What version 5 played: its parameters' profile (Reader::keyDna, the patch's seed).
    ShellProcessor reference;
    prepare (reference);
    osp::keydna::Settings expected;
    expected.enabled = true;
    expected.individuality = 0.8;
    expected.seed = 4242;
    using osp::keydna::Param;
    expected[Param::position].variation = 0.6;
    expected[Param::size].variation = 0.45;
    expected[Param::density].variation = 0.3;
    expected[Param::spread].variation = 0.2;
    expected[Param::tune].variation = 0.1;
    reference.setKeyDnaSettings (0, expected, "KEY DNA", false);
    setValue (reference, osp::plugin::InstrumentHost::keyDnaAmountId (0), 70.0f);
    const auto played = renderShell (reference, 0.8);

    ShellProcessor p;
    p.setStateInformation (v5.getData(), static_cast<int> (v5.getSize()));
    const auto dna = p.keyDnaSettings (0);
    CHECK (dna.enabled);
    CHECK (dna.seed == 4242u);
    CHECK (dna.individuality == Approx (0.8).margin (1.0e-4));
    CHECK (dna[Param::size].variation == Approx (0.45).margin (1.0e-4));
    CHECK (p.parameterValue (osp::plugin::InstrumentHost::keyDnaAmountId (0)) == Approx (70.0f));
    const auto migrated = renderShell (p, 0.8);
    CHECK (differenceRms (migrated, played) < 1.0e-6);

    // KEY DNA is heard (off, the sound differs), and today's session keeps it exactly.
    ShellProcessor off;
    prepare (off);
    CHECK (differenceRms (renderShell (off, 0.8), played) > 1.0e-4);
    juce::MemoryBlock now;
    p.getStateInformation (now);
    ShellProcessor q;
    q.setStateInformation (now.getData(), static_cast<int> (now.getSize()));
    CHECK (q.keyDnaSettings (0) == p.keyDnaSettings (0));
    CHECK (differenceRms (renderShell (q, 0.8), played) < 1.0e-6);
}

TEST_CASE ("analog: KEY DNA is ANDOR OSP's editor: opened from the card, edits undoable, saved with the session", "[analog][editor][keydna]")
{
    ShellProcessor p;
    CHECK (p.hasKeyDna());
    setValue (p, "layerA.sourceMode", 1.0f);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    ui.refreshNow();
    REQUIRE (ui.engineCard (0).onKeyDna != nullptr);
    ui.engineCard (0).onKeyDna (0);
    auto* panel = ui.keyDnaWindow();
    REQUIRE (panel != nullptr);
    CHECK (panel->layer() == 0);
    CHECK (panel->isVisible());
    CHECK (panel->getHeight() > 300);   // (ANDOR OSP's place: over the instrument, above the keys)

    // An edit through the host (as the editor makes it): one undo step.
    auto dna = p.keyDnaSettings (0);
    dna.enabled = true;
    dna[osp::keydna::Param::size].variation = 0.5;
    p.setKeyDnaSettings (0, dna, "KEY DNA", true);
    CHECK (p.keyDnaSettings (0).enabled);
    p.undo().undo();
    CHECK_FALSE (p.keyDnaSettings (0).enabled);
    p.undo().redo();
    CHECK (p.keyDnaSettings (0).enabled);
    // A drawn curve is kept as drawn.
    osp::mod::Curve curve;
    curve.count = 3;
    curve.points[0] = { 0.0f, 0.2f, 0.0f };
    curve.points[1] = { 0.5f, 0.8f, 0.0f };
    curve.points[2] = { 1.0f, 0.4f, 0.0f };
    p.setKeyDnaCurve (0, osp::keydna::Param::position, curve, true);
    CHECK (p.keyDnaCurve (0, osp::keydna::Param::position) == curve);
    panel->refresh();
    ui.closeKeyDna();
    CHECK (ui.keyDnaWindow() == nullptr);

    juce::MemoryBlock session;
    p.getStateInformation (session);
    ShellProcessor q;
    q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
    CHECK (q.keyDnaSettings (0) == p.keyDnaSettings (0));
    CHECK (q.keyDnaCurve (0, osp::keydna::Param::position) == curve);
    editor.reset();
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

TEST_CASE ("analog: a version-3 session opens with every version-4 setting neutral and sounds as it did", "[analog][state]")
{
    // A version-3 session has none of version 4's parameters (models, OTA, chorus, ORGANIC,
    // MORPH): loading it into an instrument that was playing other settings sets each to its
    // default, so the session plays CLASSIC + CLASSIC through the LADDER, chorus off.
    juce::MemoryBlock session;
    juce::AudioBuffer<float> before;
    {
        ShellProcessor old;
        apply (old, factoryPatches()[2].patch);
        setValue (old, "space", 0.0f);
        before = renderShell (old, 0.5, phrase, false);
        old.getStateInformation (session);
    }
    auto xml = juce::AudioProcessor::getXmlFromBinary (session.getData(), static_cast<int> (session.getSize()));
    REQUIRE (xml != nullptr);
    xml->setAttribute ("productVersion", 3);   // as version 3 wrote it: without version 4's parameters
    int removed = 0;
    for (auto* tree : xml->getChildIterator())
        for (int i = tree->getNumChildElements(); --i >= 0;)
            if (params::version4Ids().contains (tree->getChildElement (i)->getStringAttribute ("id")))
            {
                tree->removeChildElement (tree->getChildElement (i), true);
                ++removed;
            }
    CHECK (removed == params::version4Ids().size());
    juce::MemoryBlock v3;
    juce::AudioProcessor::copyXmlToBinary (*xml, v3);

    ShellProcessor p;
    // What was playing before: every version-4 setting away from its default.
    for (const auto& id : params::version4Ids())
    {
        auto* parameter = p.parameters.getParameter (id);
        REQUIRE (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->getDefaultValue() > 0.5f ? 0.0f : 1.0f);
    }
    p.setStateInformation (v3.getData(), static_cast<int> (v3.getSize()));
    for (const auto& id : params::version4Ids())
    {
        INFO (id);
        auto* parameter = p.parameters.getParameter (id);
        CHECK (parameter->getValue() == Approx (parameter->getDefaultValue()).margin (1.0e-6));
    }
    const auto after = renderShell (p, 0.5, phrase, false);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < before.getNumSamples(); ++i)
            REQUIRE (before.getSample (c, i) == after.getSample (c, i));
}

TEST_CASE ("analog: a version-4 session rests at the A/B mix's centre, where ORGANIC played beside the oscillators", "[analog][state]")
{
    // Version 4 played ORGANIC at full level beside the oscillators whatever the A/B mix
    // showed (its default: all A). Version 5 plays the mix; its centre is that sound.
    juce::MemoryBlock session;
    {
        ShellProcessor old;
        setValue (old, "ab.blend", 0.0f);
        old.getStateInformation (session);
    }
    auto xml = juce::AudioProcessor::getXmlFromBinary (session.getData(), static_cast<int> (session.getSize()));
    REQUIRE (xml != nullptr);
    xml->setAttribute ("productVersion", 4);
    juce::MemoryBlock v4;
    juce::AudioProcessor::copyXmlToBinary (*xml, v4);
    ShellProcessor p;
    p.setStateInformation (v4.getData(), static_cast<int> (v4.getSize()));
    CHECK (p.parameterValue ("ab.blend") == Approx (0.5f));
    // A version-5 session keeps the mix it was saved with.
    ShellProcessor q;
    q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
    CHECK (q.parameterValue ("ab.blend") == Approx (0.0f));
}

namespace
{
    /** A fresh editor at the reference size with the workbench's view preference reset (it is
        kept in the product's documents folder, across launches). */
    struct WorkbenchFixture
    {
        explicit WorkbenchFixture (int width = 1448)
        {
            p.setWellViewState ({});
            editor.reset (p.createEditorIfNeeded());
            ui = &dynamic_cast<andor::shell::ShellEditor&> (*editor);
            editor->setSize (width, juce::roundToInt (static_cast<float> (width) * 1086.0f / 1448.0f));
            ui->refreshNow();
        }
        ~WorkbenchFixture()
        {
            editor.reset();
            p.setWellViewState ({});
        }
        osp::plugin::EngineCard& card() { return ui->engineCard (0); }
        osp::plugin::CardWellEditor* bench() { return card().wellEditorComponent(); }
        /** The view preference, as the musician would set it (the shell keeps it). */
        void view (bool detailed, int section = 0, bool pinned = false)
        {
            osp::plugin::CardWellEditor::ViewState v;
            v.detailed = detailed;
            v.section = section;
            v.pinned = pinned;
            p.setWellViewState (v);
            if (bench() != nullptr)
                bench()->setViewState (v);
            ui->refreshNow();
        }
        /** The workbench's control bound to `id` (null: none), and whether it is on screen. */
        juce::Component* control (const juce::String& id)
        {
            juce::Component* found = nullptr;
            std::function<void (juce::Component&)> find = [&] (juce::Component& c) {
                for (auto* child : c.getChildren())
                {
                    if (found == nullptr && osp::plugin::binding::tagsOf (*child).contains (id))
                        found = child;
                    find (*child);
                }
            };
            if (bench() != nullptr)
                find (*bench());
            return found;
        }
        bool shown (const juce::String& id)
        {
            auto* c = control (id);
            for (; c != nullptr && c != bench(); c = c->getParentComponent())
                if (! c->isVisible())
                    return false;
            return c != nullptr && bench()->isVisible();
        }
        /** The view's leaf controls on screen: inside the well, none overlapping another. */
        void checkLayout()
        {
            REQUIRE (bench() != nullptr);
            const auto well = bench()->getLocalBounds();
            std::vector<juce::Rectangle<int>> boxes;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
                for (auto* child : c.getChildren())
                {
                    if (! child->isVisible())
                        continue;
                    const bool leaf = dynamic_cast<juce::Button*> (child) != nullptr || dynamic_cast<osp::plugin::MiniKnob*> (child) != nullptr
                                      || dynamic_cast<osp::plugin::SegmentedControl*> (child) != nullptr
                                      || dynamic_cast<osp::plugin::ValueSelector*> (child) != nullptr || child->getComponentID().isNotEmpty();
                    if (leaf)
                    {
                        const auto box = bench()->getLocalArea (child, child->getLocalBounds());
                        INFO (child->getTitle() << " " << child->getComponentID() << " at " << box.toString() << " in " << well.toString());
                        CHECK (well.contains (box));
                        CHECK (box.getHeight() >= 24);   // (a reachable target)
                        for (const auto& other : boxes)
                            CHECK_FALSE (box.intersects (other));
                        boxes.push_back (box);
                    }
                    else
                        walk (*child);
                }
            };
            walk (*bench());
            CHECK (boxes.size() >= 6);
        }
        std::map<juce::String, float> values()
        {
            std::map<juce::String, float> v;
            for (auto* parameter : p.getParameters())
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
                    v[withId->paramID] = parameter->getValue();
            return v;
        }

        ShellProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        andor::shell::ShellEditor* ui = nullptr;
    };

    juce::MouseEvent mouseEventFor (juce::Component& c, juce::Point<float> at)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (source, at, {}, juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, at, now, 1, false);
    }
}

TEST_CASE ("analog: OSCILLATORS beside M and S opens the workbench in the waveform's well; DETUNE is a plain control", "[analog][editor][workbench]")
{
    WorkbenchFixture f;
    auto& ui = *f.ui;
    auto& card = f.card();
    auto press = [&] (juce::Component& c, juce::Point<float> at) {   // as a real click: the watch, then the component
        ui.pressAnywhere (&c);
        c.mouseDown (mouseEventFor (c, at));
        c.mouseUp (mouseEventFor (c, at));
    };

    // At first the waveform; no workbench made yet (nothing at start-up unless pinned).
    CHECK (card.display().isVisible());
    CHECK_FALSE (ui.isWellEditorShown());
    CHECK (f.bench() == nullptr);

    // The heading's door covers the word's glyphs, right of M and S, inside the text area.
    REQUIRE (card.hasHeadingDoor());
    const auto heading = card.headingBounds();
    REQUIRE (! heading.isEmpty());
    auto* door = card.getComponentAt (heading.getCentre());
    REQUIRE (door != nullptr);
    CHECK (card.isHeading (door));
    juce::Component* mute = nullptr;
    juce::Component* solo = nullptr;
    for (auto* child : card.getChildren())
        if (auto* b = dynamic_cast<osp::plugin::LayerStateButton*> (child))
            (mute == nullptr ? mute : solo) = b;
    REQUIRE (mute != nullptr);
    REQUIRE (solo != nullptr);
    CHECK (heading.getX() >= mute->getRight());
    CHECK (! heading.intersects (mute->getBounds()));
    CHECK (! heading.intersects (solo->getBounds()));
    CHECK (heading.getWidth() < card.getWidth() / 3);   // the word, not the header

    // A click shows the workbench exactly in the well (the card does not move or resize, no
    // popover opens); a second click brings the waveform back.
    const auto cardBounds = card.getBounds();
    const auto well = card.display().getBounds();
    const auto before = f.values();
    press (*door, door->getLocalBounds().getCentre().toFloat());
    REQUIRE (ui.isWellEditorShown());
    REQUIRE (f.bench() != nullptr);
    CHECK (f.bench()->getBounds() == well);
    CHECK_FALSE (card.display().isVisible());   // (it does not draw beneath)
    CHECK (card.getBounds() == cardBounds);
    CHECK (ui.openPopupIndex() == -1);
    CHECK (ui.openPopupPanel() == nullptr);
    CHECK (card.getComponentAt (well.getCentre()) != &card.display());
    // A click inside it (or on the card's knobs) does not close it.
    ui.pressAnywhere (f.bench());
    CHECK (ui.isWellEditorShown());
    press (*door, door->getLocalBounds().getCentre().toFloat());
    CHECK_FALSE (ui.isWellEditorShown());
    CHECK (card.display().isVisible());
    CHECK (f.bench() != nullptr);   // (kept, hidden: it never intercepts a click)
    CHECK_FALSE (f.bench()->isVisible());
    CHECK (card.getComponentAt (well.getCentre()) == &card.display());
    // The header: the pages (WAVE | OSC 1 | OSC 2 | PAIR) at the left, the depth and the pin at
    // the right, all on one centre line. Escape closes it, as the heading does.
    press (*door, door->getLocalBounds().getCentre().toFloat());
    REQUIRE (ui.isWellEditorShown());
    {
        auto* pages = f.bench()->findChildWithID ("pages");
        auto* depth = f.bench()->findChildWithID ("detail");
        auto* pin = f.bench()->findChildWithID ("pin");
        REQUIRE (pages != nullptr);
        REQUIRE (depth != nullptr);
        REQUIRE (pin != nullptr);
        CHECK (pages->getBounds().getCentreY() == pin->getBounds().getCentreY());
        CHECK (std::abs (depth->getBounds().getCentreY() - pin->getBounds().getCentreY()) <= 1);
        CHECK (depth->getHeight() < pages->getHeight());   // (quieter, smaller)
        CHECK (pages->getRight() < depth->getX());
    }
    ui.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    CHECK_FALSE (ui.isWellEditorShown());
    // The keyboard: Return and Space on the focused word.
    CHECK (door->getWantsKeyboardFocus());
    door->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    CHECK (ui.isWellEditorShown());
    door->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
    CHECK_FALSE (ui.isWellEditorShown());
    CHECK (door->getTitle().isNotEmpty());   // (its accessibility handler is a button; it exists once on screen)
    // Opening and closing it changed no parameter.
    CHECK (f.values() == before);

    // M and S still mute and solo the layer, the workbench open or not.
    press (*door, door->getLocalBounds().getCentre().toFloat());
    const float muteBefore = f.p.parameters.getRawParameterValue ("layerA.mute")->load();
    press (*mute, mute->getLocalBounds().getCentre().toFloat());
    CHECK (f.p.parameters.getRawParameterValue ("layerA.mute")->load() != muteBefore);
    CHECK (ui.isWellEditorShown());
    press (*mute, mute->getLocalBounds().getCentre().toFloat());

    // DETUNE: OSC 2's fine tuning, enabled, a modulation target; clicking or dragging it moves
    // the tuning and never opens anything.
    juce::Slider* detune = nullptr;
    std::function<void (juce::Component&)> find = [&] (juce::Component& c) {
        for (auto* child : c.getChildren())
        {
            if (auto* s = dynamic_cast<juce::Slider*> (child); s != nullptr && s->getProperties()["paramId"].toString() == params::osc2Fine
                                                               && c.getParentComponent() == &card)
                detune = s;
            if (child != f.bench())
                find (*child);
        }
    };
    find (card);
    REQUIRE (detune != nullptr);
    CHECK (detune->isEnabled());
    setValue (f.p, params::osc2Fine, -12.0f);
    CHECK (detune->getValue() == Approx (-12.0));
    auto* knob = detune->getParentComponent();
    REQUIRE (knob != nullptr);
    press (*door, door->getLocalBounds().getCentre().toFloat());   // (the waveform again)
    const auto caption = card.startLabelBounds();
    press (*knob, knob->getLocalPoint (&card, caption.getCentre()).toFloat());   // its name
    CHECK (ui.openPopupIndex() == -1);
    CHECK_FALSE (ui.isWellEditorShown());
    detune->setValue (25.0, juce::sendNotificationSync);   // a drag's result
    CHECK (f.p.parameters.getRawParameterValue (params::osc2Fine)->load() == Approx (25.0f).margin (0.01f));
    CHECK (ui.openPopupIndex() == -1);
    // No popover route for the oscillators any more (the old one is gone); the start
    // control has none; the first macro is MORPH's.
    ui.openPopup (andor::shell::ShellEditor::generatorHeaderPopup);
    CHECK (ui.openPopupPanel() == nullptr);
    ui.openPopup (andor::shell::ShellEditor::startControlPopup);
    CHECK (ui.openPopupPanel() == nullptr);
    ui.openPopup (0);
    REQUIRE (ui.openPopupPanel() != nullptr);
    CHECK (ui.openPopupPanel()->getTitle() == "MORPH");
    ui.closePopup();
}

TEST_CASE ("analog: the workbench shows each model's own controls, Quick and Detailed, in the well at every size", "[analog][editor][workbench]")
{
    WorkbenchFixture f;
    f.ui->showWellEditor (true);
    REQUIRE (f.ui->isWellEditorShown());

    // The pages: BOTH (first: the two oscillators at once), OSC 1, OSC 2, PAIR; the focused
    // pages have a depth (QUICK, DETAILED). QUICK on OSC 1 / OSC 2: ON, the model, its
    // waves, its pitch, shape and level.
    CHECK_FALSE (f.bench()->viewState().detailed);
    CHECK (f.bench()->viewState().section == 0);   // (BOTH first)
    {
        // BOTH: each oscillator its own equal section (model, ON, waves, character, OCT, SEMI,
        // LEVEL), side by side in the wide well; no depth (one view).
        f.view (false, 0);
        for (const auto& id : { params::osc1Model, params::osc1Enabled, params::osc1Wave, params::osc1Width, params::lfoWidth1, params::osc1Octave,
                                params::osc1Coarse, params::osc1Level, params::osc2Model, params::osc2Enabled, params::osc2Wave, params::osc2Width,
                                params::lfoWidth2, params::osc2Octave, params::osc2Coarse, params::osc2Level })
        {
            INFO ("BOTH: " << id);
            CHECK (f.shown (id));
        }
        CHECK (f.control (params::osc2Fine) == nullptr);   // (the card's DETUNE, below the well)
        auto area = [&f] (const juce::String& id) { return f.bench()->getLocalArea (f.control (id), f.control (id)->getLocalBounds()); };
        CHECK (area (params::osc1Level).getWidth() == area (params::osc2Level).getWidth());
        CHECK (area (params::osc1Level).getY() == area (params::osc2Level).getY());
        CHECK (area (params::osc1Model).getRight() < area (params::osc2Model).getX());   // (side by side)
        CHECK (area (params::osc2Model).getX() - area (params::osc1Model).getX() == area (params::osc2Level).getX() - area (params::osc1Level).getX());
        auto* detailTabs = f.bench()->findChildWithID ("detail");
        REQUIRE (detailTabs != nullptr);
        CHECK_FALSE (detailTabs->isVisible());
        // The models' own character: WIDE's SHAPE and SPREAD side by side; the DCO's SUB, PWM
        // and its source. The same knob size and row in every model.
        const auto classicWidth = area (params::osc1Width);
        setValue (f.p, params::osc1Model, 2.0f);
        setValue (f.p, params::osc2Model, 1.0f);
        f.ui->refreshNow();
        CHECK (f.shown (params::osc1WideShape));
        CHECK (f.shown (params::osc1WideSpread));
        CHECK_FALSE (f.shown (params::osc1Width));
        CHECK (area (params::osc1WideShape) == classicWidth);
        CHECK (area (params::osc1WideShape).getY() == area (params::osc1WideSpread).getY());
        CHECK (f.shown (params::osc2DcoSub));
        CHECK (f.shown (params::osc2DcoPwmDepth));
        CHECK (f.shown (params::osc2DcoPwmSource));
        CHECK (area (params::osc2DcoSub).getHeight() == classicWidth.getHeight());
        f.checkLayout();
        setValue (f.p, params::osc1Model, 0.0f);
        setValue (f.p, params::osc2Model, 0.0f);
        f.ui->refreshNow();
        // The waveform key: back to the large waveform (as OSCILLATORS).
        auto* waveform = dynamic_cast<juce::Button*> (f.bench()->findChildWithID ("waveform"));
        REQUIRE (waveform != nullptr);
        waveform->onClick();
        CHECK_FALSE (f.ui->isWellEditorShown());
        f.ui->showWellEditor (true);
        CHECK (f.ui->isWellEditorShown());
    }
    f.view (false, 1);
    for (const auto& id : { params::osc1Model, params::osc1Enabled, params::osc1Wave, params::osc1Octave, params::osc1Coarse, params::osc1Width, params::osc1Level })
    {
        INFO (id);
        CHECK (f.shown (id));
    }
    CHECK_FALSE (f.shown (params::osc1DcoSub));
    CHECK_FALSE (f.shown (params::osc1WideShape));
    CHECK (f.control (params::osc2Level) == nullptr);   // (one oscillator in focus)
    CHECK (f.control (params::lfoPitch1) == nullptr);   // (DETAILED's: not made in QUICK)
    f.checkLayout();
    // OSC 1 and OSC 2 share one grid: the same families in the same places.
    const auto octave1 = f.bench()->getLocalArea (f.control (params::osc1Octave), f.control (params::osc1Octave)->getLocalBounds());
    const auto level1 = f.bench()->getLocalArea (f.control (params::osc1Level), f.control (params::osc1Level)->getLocalBounds());
    f.view (false, 2);
    CHECK (f.bench()->getLocalArea (f.control (params::osc2Octave), f.control (params::osc2Octave)->getLocalBounds()) == octave1);
    CHECK (f.bench()->getLocalArea (f.control (params::osc2Level), f.control (params::osc2Level)->getLocalBounds()) == level1);
    CHECK (f.shown (params::osc2Fine));   // (OSC 2's fine tuning: the card's DETUNE)
    // DETAILED adds a row under the same families: the first row stays where it was across.
    f.view (true, 2);
    CHECK (f.bench()->getLocalArea (f.control (params::osc2Level), f.control (params::osc2Level)->getLocalBounds()).getX() == level1.getX());
    CHECK (f.shown (params::lfoPitch2));
    CHECK (f.shown (params::lfoWidth2));
    // DCO on OSC 1, WIDE on OSC 2: the view follows (also a change from elsewhere: automation,
    // a preset). A model's settings are kept while another is shown.
    setValue (f.p, params::osc1Width, 30.0f);
    setValue (f.p, params::osc1Model, 1.0f);
    setValue (f.p, params::osc2Model, 2.0f);
    f.view (false, 1);
    CHECK_FALSE (f.shown (params::osc1Wave));
    CHECK (f.shown (params::osc1DcoSaw));
    CHECK (f.shown (params::osc1DcoPulse));
    CHECK (f.shown (params::osc1DcoSub));
    CHECK (f.shown (params::osc1Width));
    f.checkLayout();
    f.view (false, 2);
    CHECK (f.shown (params::osc2WideWave));
    CHECK (f.shown (params::osc2WideShape));
    CHECK (f.shown (params::osc2WideSpread));
    CHECK_FALSE (f.shown (params::osc2Wave));
    f.checkLayout();
    // A control in the workbench moves its parameter (and so the sound): its attachment.
    auto* level = dynamic_cast<osp::plugin::MiniKnob*> (f.control (params::osc2Level));
    REQUIRE (level != nullptr);
    level->slider.setValue (55.0, juce::sendNotificationSync);
    CHECK (f.p.parameterValue (params::osc2Level) == Approx (55.0f).margin (0.2f));
    // SPREAD acts on SUPER SAW only: faded otherwise, lit with it.
    CHECK (f.control (params::osc2WideSpread)->getAlpha() < 0.5f);
    setValue (f.p, params::osc2WideWave, 4.0f);
    f.ui->refreshNow();
    CHECK (f.control (params::osc2WideSpread)->getAlpha() == Approx (1.0f));
    setValue (f.p, params::osc1Model, 0.0f);
    CHECK (f.p.parameterValue (params::osc1Width) == Approx (30.0f).margin (0.1f));

    // Changing the page or the depth changes the view only, never a value. Every setting
    // the old popover had (and its MORE section) has a page; KEY DNA has ANDOR OSP's editor.
    const auto before = f.values();
    struct Expect
    {
        int page;
        bool detailed;
        std::vector<juce::String> ids;
    };
    const std::vector<Expect> expected {
        { 0, false, { params::osc1Model, params::osc1Enabled, params::osc1Octave, params::osc1Coarse, params::osc1Level, params::osc2Model,
                      params::osc2Enabled, params::osc2Octave, params::osc2Coarse, params::osc2Level } },
        { 1, true, { params::osc1Model, params::osc1Enabled, params::osc1Octave, params::osc1Coarse, params::osc1Width, params::osc1Level, params::lfoPitch1,
                     params::lfoWidth1 } },
        { 2, true, { params::osc2Model, params::osc2Enabled, params::osc2Octave, params::osc2Coarse, params::osc2Fine, params::osc2Width, params::osc2Level,
                     params::lfoPitch2, params::lfoWidth2, params::osc2WideWave, params::osc2WideShape, params::osc2WideSpread } },
        { 3, false, { params::sync, params::osc2KeyTrack, params::osc2LowFreq, params::ringMix, params::polyModEnv, params::polyModOsc2, params::polyModToPitch1,
                      params::polyModToWidth1, params::polyModToCutoff } },
        { 3, true, { params::sync, params::ringMix, params::polyModToCutoff, params::polyphony, params::unison, params::unisonDetune, params::spread,
                     params::noise, params::noiseColor } },
    };
    for (const auto& e : expected)
    {
        f.view (e.detailed, e.page);
        for (const auto& id : e.ids)
        {
            INFO ("page " << e.page << (e.detailed ? " detailed: " : " quick: ") << id);
            CHECK (f.shown (id));
        }
        f.checkLayout();
    }
    for (int page = 0; page < 4; ++page)
        for (const bool detailed : { false, true })
        {
            f.view (detailed, page);
            CHECK (f.control (params::keyDna) == nullptr);   // (not in the workbench)
            f.checkLayout();
        }
    // OSC 1 as a DCO in DETAILED: its PWM source and depth as well.
    setValue (f.p, params::osc1Model, 1.0f);
    f.view (true, 1);
    CHECK (f.shown (params::osc1DcoPwmSource));
    CHECK (f.shown (params::osc1DcoPwmDepth));
    CHECK (f.shown (params::osc1DcoSub));
    f.checkLayout();
    setValue (f.p, params::osc1Model, 0.0f);
    f.view (false);
    CHECK (f.values() == before);
    // The page is kept while the depth changes (the header's own QUICK | DETAILED; the
    // shell keeps the choice); the WAVE page's chips open an oscillator's page.
    f.view (true, 3);
    auto* detail = f.bench()->findChildWithID ("detail");
    REQUIRE (detail != nullptr);
    detail->mouseDown (mouseEventFor (*detail, { 5.0f, 5.0f }));   // QUICK
    CHECK_FALSE (f.bench()->viewState().detailed);
    CHECK_FALSE (f.p.wellViewState().detailed);
    CHECK (f.p.wellViewState().section == 3);
    CHECK (f.control (params::polyphony) == nullptr);   // (VOICES is DETAILED's)
    detail->mouseDown (mouseEventFor (*detail, { static_cast<float> (detail->getWidth()) - 5.0f, 5.0f }));   // DETAILED
    CHECK (f.bench()->viewState().detailed);
    CHECK (f.bench()->viewState().section == 3);
    CHECK (f.shown (params::polyphony));
    f.view (false, 0);
    {
        juce::Button* chip = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c) {
            for (auto* child : c.getChildren())
            {
                if (auto* b = dynamic_cast<juce::Button*> (child); b != nullptr && b->getButtonText() == "OSC 2")
                    chip = b;
                find (*child);
            }
        };
        find (*f.bench());
        REQUIRE (chip != nullptr);
        chip->onClick();
        CHECK (f.bench()->viewState().section == 2);
        CHECK (f.p.wellViewState().section == 2);
    }
    f.view (false);
    CHECK (f.values() == before);

    // Smaller windows scale the whole instrument: the well's own layout is the same. Two
    // cards (ORGANIC loaded) make the well narrower: Quick and every section still fit.
    {
        Scratch scratch;
        loadOrganic (f.p, writeRecording (scratch.dir, "workbench stand-in.wav", 57, 1.0));
        f.ui->refreshNow();
        REQUIRE (f.ui->visibleCardCount() == 2);
        REQUIRE (f.ui->isWellEditorShown());
        CHECK (f.bench()->getBounds() == f.card().display().getBounds());
        CHECK (f.bench()->getWidth() < 800);
        {
            // BOTH, compact: the two oscillators as strips, one over the other, both names,
            // models, ON keys and levels in view.
            f.view (false, 0);
            for (const auto& id : { params::osc1Model, params::osc1Enabled, params::osc1Level, params::osc2Model, params::osc2Enabled, params::osc2Level })
            {
                INFO ("BOTH compact: " << id);
                CHECK (f.shown (id));
            }
            auto area = [&f] (const juce::String& id) { return f.bench()->getLocalArea (f.control (id), f.control (id)->getLocalBounds()); };
            CHECK (area (params::osc2Model).getY() > area (params::osc1Model).getBottom());
            CHECK (area (params::osc2Model).getX() == area (params::osc1Model).getX());
        }
        for (int page = 0; page < 4; ++page)
            for (const bool detailed : { false, true })
            {
                INFO ("narrow, page " << page << (detailed ? " detailed" : " quick"));
                f.view (detailed, page);
                f.checkLayout();
            }
        // ORGANIC's card has no oscillator editor; its heading opens nothing.
        CHECK_FALSE (f.ui->engineCard (1).hasHeadingDoor());
        CHECK (f.ui->engineCard (1).wellEditorComponent() == nullptr);
        f.view (false);
    }
}

TEST_CASE ("analog: the EQ takes the well and gives it back to a pinned workbench; the pin outlives the window", "[analog][editor][workbench]")
{
    WorkbenchFixture f;
    auto& card = f.card();
    auto& ui = *f.ui;
    const auto before = f.values();

    // Unpinned: the EQ replaces it; closing the EQ shows the waveform.
    ui.showWellEditor (true);
    REQUIRE (card.onEqToggle != nullptr);
    card.onEqToggle (0, true);
    CHECK (card.isEqOpen());
    CHECK_FALSE (ui.isWellEditorShown());
    CHECK_FALSE (f.bench()->isVisible());
    card.onEqToggle (0, false);
    CHECK_FALSE (card.isEqOpen());
    CHECK_FALSE (ui.isWellEditorShown());
    CHECK (card.display().isVisible());
    // From the waveform: EQ open and closed, the waveform again.
    card.onEqToggle (0, true);
    card.onEqToggle (0, false);
    CHECK_FALSE (ui.isWellEditorShown());

    // PIN (its key): the EQ still takes the well, and gives it back when it closes.
    ui.showWellEditor (true);
    auto* pin = dynamic_cast<juce::Button*> (f.bench()->findChildWithID ("pin"));
    REQUIRE (pin != nullptr);
    CHECK (pin->getTitle().isNotEmpty());
    pin->setToggleState (true, juce::dontSendNotification);
    pin->onClick();
    CHECK (f.p.wellViewState().pinned);
    card.onEqToggle (0, true);
    CHECK (card.isEqOpen());
    CHECK_FALSE (ui.isWellEditorShown());
    card.onEqToggle (0, false);
    CHECK (ui.isWellEditorShown());
    CHECK_FALSE (card.isEqOpen());
    // The heading while the EQ is open: the workbench takes the well back, the EQ closes.
    card.onEqToggle (0, true);
    ui.toggleWellEditor();
    CHECK (ui.isWellEditorShown());
    CHECK_FALSE (card.isEqOpen());
    // None of it is sound: no parameter moved.
    CHECK (f.values() == before);

    // A preset (or a factory sound) leaves it where it is, showing the new values.
    apply (f.p, factoryPatches()[8].patch);
    ui.refreshNow();
    CHECK (ui.isWellEditorShown());
    // Detailed and its section are kept with the pin.
    f.view (true, 2, true);
    // The window closes and opens again (or the plugin): pinned, it opens with the workbench,
    // in Detailed, at the same section.
    f.editor.reset();
    f.editor.reset (f.p.createEditorIfNeeded());
    f.ui = &dynamic_cast<andor::shell::ShellEditor&> (*f.editor);
    f.ui->refreshNow();
    CHECK (f.ui->isWellEditorShown());
    REQUIRE (f.bench() != nullptr);
    CHECK (f.bench()->viewState().detailed);
    CHECK (f.bench()->viewState().section == 2);
    {
        // Another instance of the plugin (a new processor) reads the same preference.
        ShellProcessor other;
        CHECK (other.wellViewState().pinned);
        CHECK (other.wellViewState().section == 2);
    }
    // The heading hides it and unpins (the musician's own choice outranks the pin).
    f.ui->toggleWellEditor();
    CHECK_FALSE (f.ui->isWellEditorShown());
    CHECK_FALSE (f.p.wellViewState().pinned);
    f.editor.reset();
    f.editor.reset (f.p.createEditorIfNeeded());
    f.ui = &dynamic_cast<andor::shell::ShellEditor&> (*f.editor);
    f.ui->refreshNow();
    CHECK_FALSE (f.ui->isWellEditorShown());
    // Closing the window with it open is safe (no callback outlives it).
    f.ui->showWellEditor (true);
    f.editor.reset();
    // The view is not in the session (nor in a preset: presets are written from parameters).
    juce::MemoryBlock state;
    f.p.getStateInformation (state);
    CHECK_FALSE (state.toString().contains ("pinned"));
}

TEST_CASE ("analog: its walnut finish is its own: copper, its own pieces, ANDOR OSP's within a scope; every window the same", "[analog][editor][materials]")
{
    namespace design = osp::plugin::design;
    namespace skin = osp::plugin::skin;
    const auto before = skin::look();
    skin::setLook (skin::Look::ceramic);
    ShellProcessor p;   // (the product's materials: set by every instance alike)
    const auto osp = design::Materials::osp();
    // The tokens every drawing reads are the product's: aged copper, not ANDOR OSP's coral.
    CHECK (design::colour::accent != osp.accent);
    CHECK (design::colour::accent == juce::Colour (0xffc8703f));
    CHECK (osp::plugin::palette::layer (0) == juce::Colour (0xffd78b50));
    CHECK (design::colour::identity (0).badgeTop != osp.identityA.badgeTop);
    CHECK (osp::plugin::palette::layer (1) == juce::Colour (0xff8f9bb4));   // (B and C keep their identities)
    CHECK (design::colour::cardTop == osp.cardTop);                         // (the family's ceramic)
    {
        // Its own pieces (the walnut housing, the copper badge) replace the shared ones by
        // name; every other piece is ANDOR OSP's.
        skin::Pieces::Holder holder;
        auto& pieces = holder.pieces();
        REQUIRE (pieces.isValid());
        REQUIRE (pieces.overlayPack() != nullptr);
        const auto* housing = pieces.find ("housing");
        const auto* badge = pieces.find ("badge-A");
        REQUIRE (housing != nullptr);
        REQUIRE (badge != nullptr);
        CHECK (pieces.overlayPack()->find ("housing") == housing);
        CHECK (badge->face == juce::Colour (0xffc86a3c));
        CHECK (pieces.overlayPack()->find ("card-1379") == nullptr);
        CHECK (pieces.find ("card-1379") != nullptr);
        // The walnut: a dark, warm wood where ANDOR OSP's teak is light (the cheek's middle).
        const auto image = pieces.decode (*housing, housing->levels.front());
        REQUIRE (image.isValid());
        const auto wood = image.getPixelAt (28, image.getHeight() / 2);
        CHECK (wood.getBrightness() < 0.5f);
        CHECK (wood.getRed() > wood.getBlue());
        const auto ceramic = image.getPixelAt (image.getWidth() / 2, image.getHeight() / 2);
        CHECK (ceramic.getBrightness() > 0.8f);   // (the ceramic is the family's)
        {
            // Within a scope, ANDOR OSP's materials exactly (the visual regression's parity
            // render): its coral and its own pieces; the product's come back after it.
            const design::ScopedMaterials ospMaterials (osp);
            CHECK (design::colour::accent == osp.accent);
            CHECK (osp::plugin::palette::layer (0) == osp.layerA);
            CHECK (pieces.overlayPack() == nullptr);
            REQUIRE (pieces.find ("badge-A") != nullptr);
            CHECK (pieces.find ("badge-A")->face == juce::Colour (0xffd87c60));
            ShellProcessor another;   // (a new instance inside the scope does not override it)
            CHECK (design::colour::accent == osp.accent);
        }
        CHECK (design::colour::accent == juce::Colour (0xffc8703f));
        CHECK (pieces.overlayPack() != nullptr);
    }
    // Two windows (of two instances) draw the same: no instance changes another's look.
    {
        ShellProcessor q;
        p.setWellViewState ({});
        std::unique_ptr<juce::AudioProcessorEditor> a (p.createEditorIfNeeded()), b (q.createEditorIfNeeded());
        a->setSize (1448, 1086);
        b->setSize (1448, 1086);
        dynamic_cast<andor::shell::ShellEditor&> (*a).refreshNow();
        dynamic_cast<andor::shell::ShellEditor&> (*b).refreshNow();
        const auto ia = a->createComponentSnapshot (a->getLocalBounds(), true, 1.0f);
        const auto ib = b->createComponentSnapshot (b->getLocalBounds(), true, 1.0f);
        int differing = 0;
        for (int y = 0; y < ia.getHeight(); y += 2)
            for (int x = 0; x < ia.getWidth(); x += 2)
                differing += ia.getPixelAt (x, y) == ib.getPixelAt (x, y) ? 0 : 1;
        CHECK (differing == 0);
    }
    // The finish is interface only: it is in no parameter and not in the session.
    juce::MemoryBlock state;
    p.getStateInformation (state);
    CHECK_FALSE (state.toString().containsIgnoreCase ("walnut"));
    skin::setLook (before);
}

TEST_CASE ("analog: the FILTER popover shows each model's own controls, and fits the instrument", "[analog][editor]")
{
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, 1086);
    ui.refreshNow();
    // A control of the open popover bound to `id`, and whether it shows.
    auto shown = [&ui] (const juce::String& id) {
        bool found = false, visible = false;
        auto* panel = ui.openPopupPanel();
        auto onScreen = [panel] (juce::Component* c) {   // (it and every parent up to the popover)
            for (; c != nullptr && c != panel; c = c->getParentComponent())
                if (! c->isVisible())
                    return false;
            return true;
        };
        std::function<void (juce::Component&)> find = [&] (juce::Component& c) {
            for (auto* child : c.getChildren())
            {
                if (osp::plugin::binding::tagsOf (*child).contains (id))
                {
                    found = true;
                    visible = visible || onScreen (child);
                }
                find (*child);
            }
        };
        if (auto* panel = ui.openPopupPanel())
            find (*panel);
        REQUIRE (found);
        return visible;
    };
    auto fits = [&ui] {
        auto* panel = ui.openPopupPanel();
        REQUIRE (panel != nullptr);
        const auto area = ui.getLocalArea (panel, panel->getLocalBounds().reduced (osp::plugin::MiniPanel::shadowMargin));
        INFO ("popover " << area.toString() << " in " << ui.getLocalBounds().toString());
        CHECK (ui.getLocalBounds().contains (area));
    };
    // FILTER: the OTA's settings only with the OTA; the Juno variant where it acts.
    ui.openPopup (2);
    fits();
    CHECK_FALSE (shown (params::otaCharacter));
    CHECK_FALSE (shown (params::junoVariant));
    setValue (p, params::filterModel, 1.0f);
    ui.refreshNow();
    CHECK (shown (params::otaCharacter));
    CHECK (shown (params::otaSlope));
    CHECK (shown (params::junoVariant));
    setValue (p, params::otaCharacter, 1.0f);   // JUPITER: no Juno variant
    ui.refreshNow();
    CHECK_FALSE (shown (params::junoVariant));
    CHECK (shown (params::chorusMode));
    CHECK (shown (params::lfoDelay));
    fits();
}

TEST_CASE ("analog: ORGANIC takes a recording onto its own card beside the oscillators, plays it, recalls it", "[analog][organic]")
{
    Scratch scratch;
    const auto file = writeRecording (scratch.dir, "nyckelharpa stand-in A3.wav", 57, 2.5);
    ShellProcessor p;
    setValue (p, "space", 0.0f);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, 1086);
    ui.refreshNow();
    CHECK (ui.visibleCardCount() == 1);   // the oscillators alone: the hero card
    CHECK (ui.isInterestedInFileDrag ({ file.getFullPathName() }));
    const auto synth = renderShell (p, 0.6);

    loadOrganic (p, file);
    CHECK (p.currentInstrument (1)->model->original.continuation.hasJumps());   // (it sustains: LOOP has a loop)
    CHECK (p.parameterValue ("ab.blend") == Catch::Approx (0.5f));   // (it joins at the A/B mix's centre)
    ui.refreshNow();
    CHECK (ui.visibleCardCount() == 2);   // two equal cards
    CHECK (ui.engineCard (0).hasHeadingDoor());
    CHECK_FALSE (ui.engineCard (1).hasHeadingDoor());   // ORGANIC's name opens no oscillator settings
    const auto layered = renderShell (p, 0.6);
    CHECK (differenceRms (synth, layered) > 1.0e-4);
    // ORGANIC alone: both oscillators off.
    setValue (p, params::osc1Enabled, 0.0f);
    setValue (p, params::osc2Enabled, 0.0f);
    const auto alone = renderShell (p, 0.6);
    CHECK (rmsOf (alone) > 1.0e-3);
    // Its card's M silences it; S on the oscillators' card too; S on its own silences them.
    setValue (p, "layerB.mute", 1.0f);
    CHECK (rmsOf (renderShell (p, 0.6)) < 1.0e-6);
    setValue (p, "layerB.mute", 0.0f);
    setValue (p, "layerA.solo", 1.0f);
    CHECK (rmsOf (renderShell (p, 0.6)) < 1.0e-6);
    setValue (p, "layerA.solo", 0.0f);
    setValue (p, params::osc1Enabled, 100.0f);
    setValue (p, params::osc2Enabled, 100.0f);
    setValue (p, "layerB.solo", 1.0f);
    CHECK (differenceRms (renderShell (p, 0.6), alone) < 1.0e-6);
    setValue (p, "layerB.solo", 0.0f);
    // The oscillators' card's LEVEL and TUNE are theirs: ORGANIC plays on as it was.
    setValue (p, "layerA.level", -48.0f);
    setValue (p, "layerA.tune", 7.0f);
    CHECK (differenceRms (renderShell (p, 0.6), alone) < 1.0e-6);
    setValue (p, "layerA.level", 0.0f);
    setValue (p, "layerA.tune", 0.0f);
    // The A/B mix: all B is ORGANIC alone, all A the oscillators alone, the centre both.
    setValue (p, "ab.blend", 1.0f);
    CHECK (differenceRms (renderShell (p, 0.6), alone) < 1.0e-6);
    setValue (p, "ab.blend", 0.0f);
    CHECK (differenceRms (renderShell (p, 0.6), synth) < 1.0e-6);
    setValue (p, "ab.blend", 0.5f);
    CHECK (differenceRms (renderShell (p, 0.6), layered) < 1.0e-6);

    // A session recalls the recording (by its content hash, from ANDOR OSP's sample store)
    // and the sound, exactly; also when the original file has gone.
    juce::MemoryBlock session;
    p.getStateInformation (session);
    for (const bool moved : { false, true })
    {
        INFO ((moved ? "the original file moved away" : "in place"));
        if (moved)
            REQUIRE (file.moveFileTo (scratch.dir.getChildFile ("elsewhere.wav")));
        ShellProcessor q;
        q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
        REQUIRE (q.waitForLoads (60000));
        q.pollLoads();
        REQUIRE (q.loadState (1) == osp::plugin::InstrumentHost::LoadState::ready);
        CHECK (differenceRms (renderShell (q, 0.6), layered) < 1.0e-6);
    }
    // Removed: the oscillators alone again, one card.
    p.removeLayer (1);
    p.pollLoads();
    renderShell (p, 0.05);   // (the hand-over happens in a block)
    ui.refreshNow();
    CHECK (ui.visibleCardCount() == 1);
    CHECK (differenceRms (renderShell (p, 0.6), synth) < 1.0e-6);
}

TEST_CASE ("analog: MORPH is the first macro; it tells whether it can morph, and turns the recording into the oscillators", "[analog][organic][morph]")
{
    Scratch scratch;
    ShellProcessor p;
    setValue (p, "space", 0.0f);
    // Nothing loaded: unavailable, said so; MORPH on changes nothing a musician hears as a morph
    // (the oscillators play).
    CHECK (p.engineStatus ("morph").isNotEmpty());
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, 1086);
    ui.openPopup (0);
    REQUIRE (ui.openPopupPanel() != nullptr);
    ui.closePopup();
    const auto synth = renderShell (p, 0.8);
    setValue (p, params::morphEnabled, 1.0f);
    setValue (p, params::morphPosition, 50.0f);
    CHECK (differenceRms (renderShell (p, 0.8), synth) < 1.0e-6);

    loadOrganic (p, writeRecording (scratch.dir, "stand-in A3.wav", 57, 2.5));
    CHECK (p.engineStatus ("morph").isEmpty());   // ready
    // The ends: 0 % the recording (the oscillators replaced by it), 100 % the oscillators.
    setValue (p, params::morphPosition, 100.0f);
    CHECK (differenceRms (renderShell (p, 0.8), synth) < 1.0e-3 * std::max (1.0e-6, rmsOf (synth)) + 1.0e-6);
    setValue (p, params::morphPosition, 0.0f);
    const auto recordingEnd = renderShell (p, 0.8);
    setValue (p, params::morphEnabled, 0.0f);
    setValue (p, params::osc1Enabled, 0.0f);
    setValue (p, params::osc2Enabled, 0.0f);
    CHECK (differenceRms (renderShell (p, 0.8), recordingEnd) < 1.0e-3 * rmsOf (recordingEnd) + 1.0e-6);
    setValue (p, params::osc1Enabled, 100.0f);
    setValue (p, params::osc2Enabled, 100.0f);
    setValue (p, params::morphEnabled, 1.0f);
    // The middle: each method its own sound.
    std::vector<juce::AudioBuffer<float>> methods;
    setValue (p, params::morphPosition, 50.0f);
    for (int m = 0; m < 3; ++m)
    {
        setValue (p, params::morphMethod, static_cast<float> (m));
        methods.push_back (renderShell (p, 0.8));
        CHECK (rmsOf (methods.back()) > 1.0e-3);
    }
    CHECK (differenceRms (methods[0], methods[1]) > 1.0e-4);
    CHECK (differenceRms (methods[1], methods[2]) > 1.0e-4);
    // Recalled with the session, MORPH and all.
    juce::MemoryBlock session;
    p.getStateInformation (session);
    ShellProcessor q;
    q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
    REQUIRE (q.waitForLoads (60000));
    q.pollLoads();
    waitForMorph (q);
    CHECK (differenceRms (renderShell (q, 0.8), methods[2]) < 1.0e-6);
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
    p.setWellViewState ({});
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
    // The OSCILLATOR WORKBENCH in the card's well: QUICK, each DETAILED section, each model.
    auto view = [&] (bool detailed, int section) {
        osp::plugin::CardWellEditor::ViewState v;
        v.detailed = detailed;
        v.section = section;
        p.setWellViewState (v);
        if (auto* bench = ui.engineCard (0).wellEditorComponent())
            bench->setViewState (v);
    };
    // Every page at both depths (WAVE, OSC 1, OSC 2, PAIR x QUICK, DETAILED).
    const char* pages[] = { "wave", "osc1", "osc2", "pair" };
    view (false, 0);
    ui.showWellEditor (true);
    for (int page = 0; page < 4; ++page)
        for (const bool detailed : { false, true })
        {
            view (detailed, page);
            shoot (juce::String ("analog-workbench-") + pages[page] + (detailed ? "-detailed" : "-quick"));
        }
    view (false, 1);
    shoot ("analog-workbench-osc1-quick@2x", 2.0f);
    // The models: OSC 1 a DCO (pulse, sub, PWM by LFO), OSC 2 WIDE's SUPER SAW.
    setValue (p, params::osc1Model, 1.0f);
    setValue (p, params::osc1DcoPulse, 1.0f);
    setValue (p, params::osc1DcoSub, 60.0f);
    setValue (p, params::osc1DcoPwmSource, 1.0f);
    setValue (p, params::osc1DcoPwmDepth, 40.0f);
    setValue (p, params::osc2Model, 2.0f);
    setValue (p, params::osc2WideWave, 4.0f);
    setValue (p, params::osc2WideSpread, 45.0f);
    for (const bool detailed : { false, true })
    {
        view (detailed, 1);
        shoot (juce::String ("analog-workbench-osc1-dco-") + (detailed ? "detailed" : "quick"));
        view (detailed, 2);
        shoot (juce::String ("analog-workbench-osc2-wide-") + (detailed ? "detailed" : "quick"));
    }
    view (false, 0);
    shoot ("analog-workbench-wave-quick-dco-wide");
    // OSC 2 switched off.
    setValue (p, params::osc2Enabled, 0.0f);
    view (false, 0);
    shoot ("analog-workbench-wave-osc2-off");
    view (false, 2);
    shoot ("analog-workbench-osc2-off");
    setValue (p, params::osc2Enabled, 1.0f);
    view (false, 0);
    // The EQ takes the well; the heading shows which view is open.
    ui.engineCard (0).onEqToggle (0, true);
    shoot ("analog-workbench-eq-priority");
    ui.engineCard (0).onEqToggle (0, false);
    ui.showWellEditor (false);
    shoot ("analog-workbench-hidden");
    setValue (p, params::osc1Model, 0.0f);
    setValue (p, params::osc1DcoPulse, 0.0f);
    setValue (p, params::osc1DcoSub, 0.0f);
    setValue (p, params::osc1DcoPwmSource, 0.0f);
    setValue (p, params::osc1DcoPwmDepth, 0.0f);
    setValue (p, params::osc2Model, 0.0f);
    setValue (p, params::osc2WideWave, 0.0f);
    setValue (p, params::osc2WideSpread, 0.0f);
    setValue (p, params::filterModel, 1.0f);
    setValue (p, params::otaHpf, 3.0f);
    ui.openPopup (2);
    shoot ("analog-popover-filter-ota");
    ui.closePopup();
    setValue (p, params::osc1Model, 0.0f);
    setValue (p, params::osc2Model, 0.0f);
    setValue (p, params::filterModel, 0.0f);
    setValue (p, params::otaHpf, 0.0f);
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
    apply (p, Patch());
    ui.showWellEditor (true);
    view (false, 1);
    shoot ("analog-workbench-window-869");
    editor->setSize (1810, juce::roundToInt (1810.0f * 1086.0f / 1448.0f));
    shoot ("analog-workbench-window-1810");
    editor->setSize (1448, 1086);
    {
        // Two cards (ORGANIC loaded): the narrower well, QUICK and a DETAILED section.
        Scratch scratch;
        loadOrganic (p, writeRecording (scratch.dir, "workbench stand-in A3.wav", 57, 1.0));
        for (int page = 0; page < 4; ++page)
            for (const bool detailed : { false, true })
            {
                view (detailed, page);
                shoot (juce::String ("analog-workbench-two-cards-") + pages[page] + (detailed ? "-detailed" : "-quick"));
            }
        view (false, 0);
    }
    ui.showWellEditor (false);
    p.setWellViewState ({});
    osp::plugin::skin::setLook (before);
}

TEST_CASE ("analog: ORGANIC's card shows a read head per playing note, as ANDOR OSP's One Shot", "[analog][organic][display]")
{
    ShellProcessor p;
    const auto& heads = p.grainSnapshot (1);
    const std::vector<Note> held { { 0, 57, 100 } };
    renderShell (p, 0.2, held, false);
    CHECK (heads.playheads.load() == 0);   // nothing loaded: none

    Scratch scratch;
    loadOrganic (p, writeRecording (scratch.dir, "organic.wav", 57, 2.5));
    renderShell (p, 0.5, held, false);
    REQUIRE (heads.playheads.load() == 1);
    const float early = heads.playheadPosition[0].load();
    CHECK (early > 0.05f);
    CHECK (early < 0.5f);
    CHECK (heads.playheadLevel[0].load() > 0.1f);

    // A chord: one head per note; further on as it plays.
    const std::vector<Note> chord { { 0, 57, 100 }, { 0, 64, 100 }, { 0, 69, 100 } };
    renderShell (p, 0.9, chord, false);
    CHECK (heads.playheads.load() >= 3);
    CHECK (heads.playheadPosition[0].load() > early);
    // The oscillators' card has no read head (its own grains are its picture).
    CHECK (p.grainSnapshot (0).playheads.load() == 0);
}

TEST_CASE ("analog: screenshots of KEY DNA, the cards' EQ and ORGANIC's playhead (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-cards]")
{
    const char* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    const auto before = osp::plugin::skin::look();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    Scratch scratch;
    {
        ShellProcessor p;
        p.setWellViewState ({});
        loadOrganic (p, writeRecording (scratch.dir, "nyckelharpa stand-in A3.wav", 57, 2.5));
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

        // ORGANIC's read heads while a chord plays.
        renderShell (p, 0.9, { { 0, 57, 100 }, { 0, 64, 100 }, { 0, 69, 100 } }, false);
        shoot ("analog-organic-playheads");
        shoot ("analog-organic-playheads@2x", 2.0f);

        // Each card's EQ (a low cut and a presence lift on A; a high cut on B).
        using Host = osp::plugin::InstrumentHost;
        setValue (p, Host::eqParameterId (0, "enabled"), 1.0f);
        setValue (p, Host::eqParameterId (0, "hp.enabled"), 1.0f);
        setValue (p, Host::eqParameterId (0, "hp.frequency"), 120.0f);
        setValue (p, Host::eqParameterId (0, "bell.enabled"), 1.0f);
        setValue (p, Host::eqParameterId (0, "bell.frequency"), 2500.0f);
        setValue (p, Host::eqParameterId (0, "bell.gain"), 6.0f);
        ui.engineCard (0).onEqToggle (0, true);
        shoot ("analog-eq-card-a");
        ui.engineCard (0).onEqToggle (0, false);
        setValue (p, Host::eqParameterId (1, "enabled"), 1.0f);
        setValue (p, Host::eqParameterId (1, "lp.enabled"), 1.0f);
        setValue (p, Host::eqParameterId (1, "lp.frequency"), 3500.0f);
        ui.engineCard (1).onEqToggle (1, true);
        shoot ("analog-eq-card-b");
        ui.engineCard (1).onEqToggle (1, false);
        shoot ("analog-eq-keys-lit");

        // KEY DNA: the card's key in GRANULAR, then ANDOR OSP's editor.
        setValue (p, "layerA.sourceMode", 1.0f);
        osp::keydna::Settings dna;
        dna.enabled = true;
        dna.individuality = 0.5;
        for (const auto param : { osp::keydna::Param::position, osp::keydna::Param::size, osp::keydna::Param::density, osp::keydna::Param::spread })
            dna[param].variation = 0.45;
        p.setKeyDnaSettings (0, dna, "KEY DNA", false);
        ui.engineCard (0).showGranularControls (true, true);
        shoot ("analog-keydna-key");
        ui.engineCard (0).onKeyDna (0);
        shoot ("analog-keydna-editor");
        shoot ("analog-keydna-editor@2x", 2.0f);
        ui.closeKeyDna();
        editor.reset();
    }
    osp::plugin::skin::setLook (before);
}

TEST_CASE ("analog: screenshots of BOTH, wide and compact (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-both]")
{
    const char* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    const auto before = osp::plugin::skin::look();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    Scratch scratch;
    {
        ShellProcessor p;
        p.setWellViewState ({});
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
        auto models = [&] (bool wideDco) {
            setValue (p, params::osc1Model, wideDco ? 2.0f : 0.0f);
            setValue (p, params::osc1WideWave, wideDco ? 4.0f : 0.0f);
            setValue (p, params::osc1WideSpread, wideDco ? 45.0f : 0.0f);
            setValue (p, params::osc2Model, wideDco ? 1.0f : 0.0f);
            setValue (p, params::osc2DcoPulse, wideDco ? 1.0f : 0.0f);
            setValue (p, params::osc2DcoSub, wideDco ? 50.0f : 0.0f);
            setValue (p, params::osc2DcoPwmSource, wideDco ? 1.0f : 0.0f);
            setValue (p, params::osc2DcoPwmDepth, wideDco ? 35.0f : 0.0f);
        };
        // The new instance: the oscillators' card and the ADD SAMPLE dock (80/20).
        shoot ("analog-dock-empty");
        ui.fileDragEnter ({ "/tmp/a.wav" }, 1300, 400);
        shoot ("analog-dock-drag");
        ui.fileDragExit ({ "/tmp/a.wav" });
        {
            const auto broken = scratch.dir.getChildFile ("broken.wav");
            broken.replaceWithText ("not audio");
            p.loadFile (broken, ShellProcessor::hybridLayer);
            REQUIRE (p.waitForLoads (60000));
            p.pollLoads();
            shoot ("analog-dock-failed");
        }
        ui.showWellEditor (true);
        for (const bool organic : { false, true })
        {
            if (organic)
                loadOrganic (p, writeRecording (scratch.dir, "nyckelharpa stand-in A3.wav", 57, 2.5));
            const juce::String width = organic ? "compact" : "wide";
            models (false);
            shoot ("analog-both-" + width + "-classic");
            models (true);
            shoot ("analog-both-" + width + "-wide-dco");
            if (! organic)
                shoot ("analog-both-" + width + "-wide-dco@2x", 2.0f);
            setValue (p, params::osc2Enabled, 0.0f);
            shoot ("analog-both-" + width + "-osc2-off");
            setValue (p, params::osc2Enabled, 1.0f);
            models (false);
        }
        editor.reset();
    }
    osp::plugin::skin::setLook (before);
}

TEST_CASE ("analog: empty, ORGANIC is a compact ADD SAMPLE dock; a recording opens its card and the two share the area", "[analog][editor][organic][dock]")
{
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    ui.refreshNow();
    auto& dock = ui.recordingDock();
    auto& oscillators = ui.engineCard (0);
    // A new instance: the oscillators' card at about 80 %, the dock beside it at about 20 %.
    REQUIRE (dock.isVisible());
    CHECK (ui.visibleCardCount() == 1);
    const double share = static_cast<double> (dock.getWidth()) / static_cast<double> (dock.getRight() - oscillators.getX());
    CHECK (share == Approx (0.2).margin (0.02));
    CHECK (dock.getX() > oscillators.getRight());
    CHECK (dock.getY() == oscillators.getY());
    CHECK (dock.getHeight() == oscillators.getHeight());
    // The workbench is the wide one beside the dock.
    ui.showWellEditor (true);
    CHECK (oscillators.wellEditorComponent()->getWidth() >= 900);
    // The dock never changes a sound: the oscillators play as they did, at full level.
    const auto values = [&p] {
        std::vector<float> v;
        for (auto* param : p.getParameters())
            v.push_back (param->getValue());
        return v;
    };
    const auto before = values();
    // A file dragged over the instrument lights the dock.
    ui.fileDragEnter ({ "/tmp/a.wav" }, 10, 10);
    CHECK (dock.isHighlighted());
    ui.fileDragExit ({ "/tmp/a.wav" });
    CHECK_FALSE (dock.isHighlighted());
    REQUIRE (dock.onBrowse != nullptr);
    CHECK (values() == before);

    // A recording: the dock gives way to ORGANIC's own card, the two equal.
    Scratch scratch;
    loadOrganic (p, writeRecording (scratch.dir, "organic.wav", 57, 1.5));
    ui.refreshNow();
    CHECK_FALSE (dock.isVisible());
    REQUIRE (ui.visibleCardCount() == 2);
    CHECK (std::abs (ui.engineCard (0).getWidth() - ui.engineCard (1).getWidth()) <= 1);
    CHECK (ui.isWellEditorShown());   // (the workbench stays, compact)
    // Removed: the dock comes back; the oscillators' settings are unchanged.
    p.removeLayer (ShellProcessor::hybridLayer);
    ui.refreshNow();
    CHECK (dock.isVisible());
    CHECK (ui.visibleCardCount() == 1);
    // A file that cannot be read: the dock says so and offers another try; nothing else moves.
    const auto broken = scratch.dir.getChildFile ("broken.wav");
    broken.replaceWithText ("not audio");
    p.loadFile (broken, ShellProcessor::hybridLayer);
    REQUIRE (p.waitForLoads (60000));
    p.pollLoads();
    ui.refreshNow();
    CHECK (dock.isVisible());
    CHECK (dock.problemText().isNotEmpty());
    // A session with a recording opens with both cards; one without, with the dock.
    loadOrganic (p, writeRecording (scratch.dir, "organic2.wav", 57, 1.5));
    juce::MemoryBlock withRecording;
    p.getStateInformation (withRecording);
    editor.reset();
    {
        ShellProcessor q;
        q.setStateInformation (withRecording.getData(), static_cast<int> (withRecording.getSize()));
        REQUIRE (q.waitForLoads (60000));
        q.pollLoads();
        std::unique_ptr<juce::AudioProcessorEditor> e (q.createEditor());
        auto& u = dynamic_cast<andor::shell::ShellEditor&> (*e);
        u.refreshNow();
        CHECK_FALSE (u.recordingDock().isVisible());
        CHECK (u.visibleCardCount() == 2);
    }
}
