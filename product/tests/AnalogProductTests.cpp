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
#include "SampleImport.h"
#include "SamplePatches.h"
#include "KontaktImport.h"
#include "SampleSourceUI.h"
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
    /** Points the shell's sample store at `folder` (empty: back to the default). POSIX setenv /
        unsetenv do not exist on Windows; the CRT's _putenv_s does what the shell's getenv reads. */
    void setSampleStore (const juce::File& folder)
    {
        const auto path = folder == juce::File() ? juce::String() : folder.getFullPathName();
       #if JUCE_WINDOWS
        ::_putenv_s ("ANDOR_SAMPLE_STORE", path.toRawUTF8());
       #else
        if (path.isEmpty())
            ::unsetenv ("ANDOR_SAMPLE_STORE");
        else
            ::setenv ("ANDOR_SAMPLE_STORE", path.toRawUTF8(), 1);
       #endif
    }

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
        auto granular2 = [] (Patch& c) { c.granular2 = true; };   // (card B's own, state version 8)
        auto abyss = [] (Patch& c) { c.effects.spaceEngine = osp::SpaceEngine::abyss; };   // (ABYSS's own: heard on ABYSS)
        auto vintage = [] (Patch& c) { c.effects.spaceEngine = osp::SpaceEngine::vintage; };
        auto bloom = [] (Patch& c) { c.effects.spaceEngine = osp::SpaceEngine::bloom; };
        auto bloomFree = [] (Patch& c) {   // (free TIME is heard when SYNC is off)
            c.effects.spaceEngine = osp::SpaceEngine::bloom;
            c.effects.bloomSync = false;
        };
        std::map<juce::String, std::function<void (Patch&)>> modes {
            { "space.abyss.gravity", abyss },
            { "space.abyss.freeze", abyss },
            { "space.abyss.resonance", abyss },
            { "space.vintage.variant", vintage },
            { "space.vintage.era", vintage },
            { "space.vintage.diffusion", vintage },
            { "space.bloom.variant", bloom },
            { "space.bloom.sync", bloom },
            { "space.bloom.division", bloom },
            { "space.bloom.time", bloomFree },
            { "space.bloom.diffusion", bloom },
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
            { "layerC.granular.position", granular2 },
            { "layerC.granular.size", granular2 },
            { "layerC.granular.density", granular2 },
            { "layerC.granular.tune", granular2 },
            { "layerC.granular.spread", granular2 },
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
        // State version 8: card C as OSC 3 (each model's settings in that model) or NOISE.
        auto osc3 = [] (OscModel m, std::function<void (Patch&)> more = {}) {
            return [m, more] (Patch& c) {
                c.hybridType = HybridType::oscillator;
                c.osc3Model = m;
                c.osc3Wave = Osc2Wave::all;
                if (more != nullptr)
                    more (c);
            };
        };
        for (const auto& id : { params::osc3Model, params::osc3Wave, params::osc3Octave, params::osc3Coarse, params::osc3Fine, params::osc3Width, params::lfoWidth3 })
            modes[id] = osc3 (OscModel::classic);
        for (const auto& id : { params::osc3DcoSaw, params::osc3DcoPulse, params::osc3DcoSub })
            modes[id] = osc3 (OscModel::dco);
        modes[params::osc3DcoPwmSource] = osc3 (OscModel::dco, [] (Patch& c) { c.dco3.pulse = true; c.dco3.pwmDepth = 0.6; });
        modes[params::osc3DcoPwmDepth] = osc3 (OscModel::dco, [] (Patch& c) { c.dco3.pulse = true; c.dco3.pwmSource = PwmSource::lfo; });
        modes[params::osc3WideWave] = osc3 (OscModel::wide);
        modes[params::osc3WideShape] = osc3 (OscModel::wide);
        modes[params::osc3WideSpread] = osc3 (OscModel::wide, [] (Patch& c) { c.wide3.wave = WideWave::superSaw; });
        for (const auto& id : { params::hybridNoiseColor, params::hybridNoiseTone, params::hybridNoiseMovement })
            modes[id] = [] (Patch& c) { c.hybridType = HybridType::noise; };
        modes[params::hybridNoiseKeys] = [] (Patch& c) {
            c.hybridType = HybridType::noise;
            c.hybridNoiseTone = 0.4;   // (KEYS moves the tone: heard below full brightness)
        };
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
    // The A/B mix is ANDOR OSP's parameter (its default all A); three engines put it in the
    // middle at once (every card heard), the INIT patch's 0.5.
    CHECK (read.at ("ab.blend") == 0.5f);
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

TEST_CASE ("analog: cards A and B show OSC 1 and OSC 2, each its own picture following only its oscillator", "[analog][editor]")
{
    ShellProcessor p;
    osp::plugin::InstrumentHost::GeneratedSource a, b, a2, b2;
    REQUIRE (p.generatedSource (0, a));
    REQUIRE (p.generatedSource (2, b));   // (card B is the shell's layer C)
    CHECK (a.name == "OSC 1");
    CHECK (b.name == "OSC 2");
    CHECK (osp::plugin::InstrumentHost::shownLayerName (2) == "B");
    CHECK (osp::plugin::InstrumentHost::shownLayerName (1) == "C");
    setValue (p, params::osc1Wave, 1.0f);   // OSC 1 pulse: card A's picture moves, card B's does not
    REQUIRE (p.generatedSource (0, a2));
    REQUIRE (p.generatedSource (2, b2));
    CHECK (a.cycle != a2.cycle);
    CHECK (b.cycle == b2.cycle);
    setValue (p, params::osc2Wave, 2.0f);   // OSC 2 pulse: card B's moves
    REQUIRE (p.generatedSource (2, b2));
    CHECK (b.cycle != b2.cycle);
    // The empty hybrid: a stand-in (its card full, its picture faint, ADD SAMPLE).
    osp::plugin::InstrumentHost::GeneratedSource c;
    REQUIRE (p.generatedSource (1, c));
    CHECK (c.placeholder.startsWith ("ADD SAMPLE"));
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
        for (const char* name : { "source", "dest", "depth", "enabled", "destExt" })
            setups[osp::plugin::InstrumentHost::modRouteId (r, name)] = route (r, 1);   // LFO 1 -> TONE (destExt: -> MIX)

    // Each card's EQ and DAMPEN (ANDOR OSP's): a band's settings act while the EQ and that band
    // are on (a shelf or bell with gain); DAMPEN's while it is on (its corner in its own mode).
    using Host = osp::plugin::InstrumentHost;
    for (int layer = 0; layer < 3; ++layer)
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
    // and the modulation's: LFO 2, ENV 1 and 2, the 16 routes' five ("mod.*"; LFO 1's are listed).
    CHECK (checkedBound == bound.size() - 4 + 7 + (8 + 2 * 7 + 5 * osp::mod::maxRoutes));
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
             { D::grainPositionC, "layerC.granular.position" }, { D::grainDensityC, "layerC.granular.density" },   // (card B's grains)
             { D::grainSizeC, "layerC.granular.size" }, { D::grainSpreadC, "layerC.granular.spread" },
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

    // ANDOR OSP's second destination list (mod.routeN.destExt): the header's MIX (the A/B mix
    // between the oscillators and ORGANIC) and card A's KEY DNA AMOUNT (in GRANULAR, KEY DNA on).
    for (const auto dest : { D::mixPosition, D::keyDnaAmountA })
    {
        INFO (osp::mod::destInfo (dest).name);
        auto patch = context;
        patch.granular = dest == D::keyDnaAmountA;
        p.resetSettings();
        apply (p, patch);
        if (dest == D::keyDnaAmountA)
        {
            osp::keydna::Settings dna;
            dna.enabled = true;
            for (const auto param : { osp::keydna::Param::position, osp::keydna::Param::size, osp::keydna::Param::density })
                dna[param].variation = 0.6;
            p.setKeyDnaSettings (0, dna, "KEY DNA", false);
            setValue (p, osp::plugin::InstrumentHost::keyDnaAmountId (0), 20.0f);
        }
        const auto reference = renderShell (p, 0.6);
        // (MIX is the instrument's, not a note's: a global source, LFO 1; per-note MIX is later.)
        setValue (p, osp::plugin::InstrumentHost::modRouteId (0, "source"), dest == D::mixPosition ? 1.0f : vel);
        setValue (p, osp::plugin::InstrumentHost::modRouteId (0, "destExt"), static_cast<float> (osp::mod::extendedChoiceOf (dest)));
        setValue (p, osp::plugin::InstrumentHost::modRouteId (0, "depth"), 50.0f);
        REQUIRE (p.modulationRoutes().size() == 1);
        CHECK (p.modulationRoutes().front().state == osp::mod::RouteState::active);
        const double d = differenceRms (reference, renderShell (p, 0.6));
        UNSCOPED_INFO (20.0 * std::log10 (std::max (d, 1.0e-12)) << " dB");
        CHECK (d > 1.0e-5);
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
        setValue (q, "layerA.sourceMode", 1.0f);   // GRANULAR (the whole voice's then: both cards')
        setValue (q, "layerC.sourceMode", 1.0f);
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
    /** A fresh editor at a width (the reference 1448 by default), and an oscillator card's
        well overlay (ProductInfo::wellOverlay): its controls, whether shown, its geometry. */
    struct OverlayFixture
    {
        explicit OverlayFixture (int width = 1448)
        {
            editor.reset (p.createEditorIfNeeded());
            ui = &dynamic_cast<andor::shell::ShellEditor&> (*editor);
            editor->setSize (width, juce::roundToInt (static_cast<float> (width) * 1086.0f / 1448.0f));
            ui->refreshNow();
        }
        osp::plugin::EngineCard& card (int layer) { return ui->engineCard (layer); }
        osp::plugin::WellOverlay* overlay (int layer)
        {
            ui->refreshNow();   // (a model changed: the shell remakes the overlay)
            return card (layer).wellOverlayComponent();
        }
        /** The overlay's control bound to `id` (null: none). */
        static juce::Component* control (juce::Component& root, const juce::String& id)
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
            find (root);
            return found;
        }
        /** Shown on screen (every parent visible, up to the overlay). */
        static bool shown (osp::plugin::WellOverlay& o, const juce::String& id)
        {
            auto* c = control (o, id);
            if (c == nullptr)
                return false;
            for (; c != nullptr && c != &o; c = c->getParentComponent())
                if (! c->isVisible())
                    return false;
            return o.isVisible();
        }
        /** The overlay's leaf controls on screen: inside the well, none overlapping another,
            each a reachable target. Returns their boxes by their bound ID. */
        static std::map<juce::String, juce::Rectangle<int>> checkLayout (osp::plugin::WellOverlay& o, int atLeast)
        {
            const auto well = o.getLocalBounds();
            std::map<juce::String, juce::Rectangle<int>> byId;
            std::vector<juce::Rectangle<int>> boxes;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
                for (auto* child : c.getChildren())
                {
                    if (! child->isVisible())
                        continue;
                    const bool leaf = dynamic_cast<juce::Button*> (child) != nullptr || dynamic_cast<osp::plugin::MiniKnob*> (child) != nullptr
                                      || dynamic_cast<osp::plugin::SegmentedControl*> (child) != nullptr
                                      || dynamic_cast<osp::plugin::ValueSelector*> (child) != nullptr;
                    if (leaf)
                    {
                        const auto box = o.getLocalArea (child, child->getLocalBounds());
                        INFO (child->getTitle() << " at " << box.toString() << " in " << well.toString());
                        CHECK (well.contains (box));
                        CHECK (box.getHeight() >= 22);   // (a reachable target)
                        CHECK (box.getWidth() >= 44);
                        for (const auto& other : boxes)
                            CHECK_FALSE (box.intersects (other));
                        boxes.push_back (box);
                        auto tags = osp::plugin::binding::tagsOf (*child);
                        if (auto* knob = dynamic_cast<osp::plugin::MiniKnob*> (child))
                            tags.addArray (osp::plugin::binding::tagsOf (knob->slider));
                        for (const auto& tag : tags)
                            byId[tag] = box;
                    }
                    else
                        walk (*child);
                }
            };
            walk (o);
            CHECK (static_cast<int> (boxes.size()) >= atLeast);
            return byId;
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
    // Its pages (FILTER, VOICE, PAIR): the tab row, moved with the arrow keys as a musician would.
    auto tabs = [&ui]() -> juce::Component* {
        juce::Component* found = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c) {
            for (auto* child : c.getChildren())
            {
                if (child->getTitle() == "Pages")
                    found = child;
                find (*child);
            }
        };
        if (auto* panel = ui.openPopupPanel())
            find (*panel);
        return found;
    };
    // How many of the page's controls show (its hierarchy: the first page kept short).
    auto shownControls = [&ui] {
        int n = 0;
        auto* panel = ui.openPopupPanel();
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
            for (auto* child : c.getChildren())
            {
                if (! child->isVisible())
                    continue;
                if (osp::plugin::binding::tagsOf (*child).size() > 0 || dynamic_cast<osp::plugin::MiniKnob*> (child) != nullptr)
                    ++n;
                else
                    walk (*child);
            }
        };
        walk (*panel);
        return n;
    };
    ui.openPopup (2);
    REQUIRE (tabs() != nullptr);
    // FILTER (the first page): its type, its four knobs, its envelope, its drive; the OTA's
    // settings only with the OTA. Nothing else.
    fits();
    CHECK (shown (params::filterModel));
    CHECK (shown (params::resonance));
    CHECK (shown (params::filterAttack));
    CHECK (shown (params::filterDrive));
    CHECK_FALSE (shown (params::otaCharacter));
    CHECK_FALSE (shown (params::chorusMode));
    CHECK_FALSE (shown (params::sync));
    CHECK (shownControls() <= 12);
    setValue (p, params::filterModel, 1.0f);
    ui.refreshNow();
    CHECK (shown (params::otaCharacter));
    CHECK (shown (params::otaSlope));
    CHECK (shown (params::junoVariant));
    fits();
    // VOICE: the amplifier, the voices, LFO 1's depths, the chorus, the character.
    tabs()->keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    ui.refreshNow();
    CHECK (shown (params::vcaMode));
    CHECK (shown (params::polyphony));
    CHECK (shown (params::lfoDelay));
    CHECK (shown (params::chorusMode));
    CHECK (shown (params::analogAmount));
    CHECK_FALSE (shown (params::resonance));
    fits();
    // PAIR: what the two oscillators do together.
    tabs()->keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    ui.refreshNow();
    CHECK (shown (params::sync));
    CHECK (shown (params::ringMix));
    CHECK (shown (params::polyModEnv));
    CHECK (shown (params::polyModToCutoff));
    CHECK (shown (params::noise));
    CHECK_FALSE (shown (params::vcaMode));
    fits();
    // Every control the old single page had is still on one of the three.
    tabs()->keyPressed (juce::KeyPress (juce::KeyPress::leftKey));
    tabs()->keyPressed (juce::KeyPress (juce::KeyPress::leftKey));
    ui.refreshNow();
    CHECK (shown (params::resonance));   // (back on FILTER)
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
    CHECK (ui.visibleCardCount() == 3);   // OSC 1, OSC 2 and the hybrid (empty: its stand-in)
    CHECK (ui.isInterestedInFileDrag ({ file.getFullPathName() }));
    const auto synth = renderShell (p, 0.6);

    loadOrganic (p, file);
    CHECK (p.currentInstrument (1)->model->original.continuation.hasJumps());   // (it sustains: LOOP has a loop)
    CHECK (p.parameterValue ("ab.blend") == Catch::Approx (0.5f));   // (it joins at the A/B mix's centre)
    ui.refreshNow();
    CHECK (ui.visibleCardCount() == 3);   // three equal cards
    CHECK_FALSE (ui.engineCard (0).hasHeadingDoor());   // (three engines: sound design is in the well, not behind the name)
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
    // Removed: the oscillators alone again (the hybrid's column its ADD SAMPLE card).
    p.removeLayer (1);
    p.pollLoads();
    renderShell (p, 0.05);   // (the hand-over happens in a block)
    ui.refreshNow();
    CHECK (ui.visibleCardCount() == 3);   // (card C's stand-in)
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
    // Each oscillator's well with its controls revealed (as the pointer would): every model.
    auto reveal = [&] (int layer, bool shown) {
        ui.refreshNow();
        if (auto* o = ui.engineCard (layer).wellOverlayComponent())
            o->setRevealedForTest (shown);
    };
    const char* modelNames[] = { "classic", "dco", "wide" };
    for (int model = 0; model < 3; ++model)
    {
        setValue (p, params::osc1Model, static_cast<float> (model));
        setValue (p, params::osc2Model, static_cast<float> ((model + 1) % 3));
        reveal (0, true);
        shoot (juce::String ("analog-osc1-hover-") + modelNames[model]);
        reveal (0, false);
        reveal (2, true);
        shoot (juce::String ("analog-osc2-hover-") + modelNames[(model + 1) % 3]);
        reveal (2, false);
    }
    setValue (p, params::osc1Model, 1.0f);
    setValue (p, params::osc1DcoPulse, 1.0f);
    setValue (p, params::osc1DcoSub, 60.0f);
    setValue (p, params::osc1DcoPwmSource, 1.0f);
    setValue (p, params::osc1DcoPwmDepth, 40.0f);
    setValue (p, params::osc2Model, 2.0f);
    setValue (p, params::osc2WideWave, 4.0f);
    setValue (p, params::osc2WideSpread, 45.0f);
    shoot ("analog-wide-dco-empty");
    reveal (0, true);
    shoot ("analog-wide-dco-empty-osc1-hover@2x", 2.0f);
    reveal (0, false);
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
    // The three cards at the smallest and largest windows, OSC 1's well revealed.
    reveal (0, true);
    shoot ("analog-osc1-hover-window-869");
    editor->setSize (1810, juce::roundToInt (1810.0f * 1086.0f / 1448.0f));
    shoot ("analog-osc1-hover-window-1810");
    reveal (0, false);
    editor->setSize (1448, 1086);
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

TEST_CASE ("analog: three equal cards, A OSC 1, B OSC 2, C the hybrid; empty, C is a full card with a stand-in; loading never moves them", "[analog][editor][organic][dock]")
{
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, 1086);
    ui.refreshNow();
    auto& osc1 = ui.engineCard (0);
    auto& osc2 = ui.engineCard (2);   // (card B: the shell's layer C)
    auto& c = ui.engineCard (1);      // (card C: the shell's layer B)
    auto standIn = [&p] {
        osp::plugin::InstrumentHost::GeneratedSource s;
        return p.generatedSource (1, s) && s.placeholder.isNotEmpty();
    };
    // A new instance: A, B and C side by side at equal widths, all three full cards; C empty
    // shows its stand-in (ADD SAMPLE) with its knobs and keys, never the compact dock.
    CHECK_FALSE (ui.recordingDock().isVisible());
    REQUIRE (ui.visibleCardCount() == 3);
    CHECK (standIn());
    CHECK (c.hasControl ("layerB.level"));
    CHECK (c.hasControl (params::hybridOctave));
    CHECK (osc1.getRight() < osc2.getX());
    CHECK (osc2.getRight() < c.getX());
    CHECK (std::abs (osc1.getWidth() - osc2.getWidth()) <= 1);
    CHECK (std::abs (osc1.getWidth() - c.getWidth()) <= 1);
    CHECK (c.getY() == osc1.getY());
    CHECK (c.getHeight() == osc1.getHeight());
    // The MIX starts in the middle: every card is heard.
    CHECK (p.parameterValue ("ab.blend") == Approx (0.5f));
    const auto columns = std::array<juce::Rectangle<int>, 3> { osc1.getBounds(), osc2.getBounds(), c.getBounds() };

    // A recording: card C plays it in the same column; A and B never move.
    Scratch scratch;
    loadOrganic (p, writeRecording (scratch.dir, "organic.wav", 57, 1.5));
    ui.refreshNow();
    CHECK_FALSE (standIn());
    REQUIRE (ui.visibleCardCount() == 3);
    CHECK (c.getBounds() == columns[2]);
    CHECK (osc1.getBounds() == columns[0]);
    CHECK (osc2.getBounds() == columns[1]);
    // Removed: the stand-in again, in the same column.
    p.removeLayer (ShellProcessor::hybridLayer);
    ui.refreshNow();
    CHECK (standIn());
    CHECK (c.getBounds() == columns[2]);
    CHECK (ui.visibleCardCount() == 3);
    // A file that cannot be read: said so; card C stays its stand-in.
    const auto broken = scratch.dir.getChildFile ("broken.wav");
    broken.replaceWithText ("not audio");
    p.loadFile (broken, ShellProcessor::hybridLayer);
    REQUIRE (p.waitForLoads (60000));
    p.pollLoads();
    ui.refreshNow();
    CHECK (p.statusMessage().isNotEmpty());
    // A session with a recording opens with it on card C.
    loadOrganic (p, writeRecording (scratch.dir, "organic2.wav", 57, 1.5));
    juce::MemoryBlock withRecording;
    p.getStateInformation (withRecording);
    editor.reset();
    {
        ShellProcessor q;
        q.setStateInformation (withRecording.getData(), static_cast<int> (withRecording.getSize()));
        REQUIRE (q.waitForLoads (60000));
        q.pollLoads();
        osp::plugin::InstrumentHost::GeneratedSource s;
        CHECK_FALSE ((q.generatedSource (1, s) && s.placeholder.isNotEmpty()));
        CHECK (q.currentInstrument (1) != nullptr);
    }
}

//==============================================================================
// The Sample Source importer (MVP A1) on the real Bukkehorn 4.5 recordings: they are not in the
// repository, so these run when ANALOG_BUKKEHORN_SAMPLES names a folder holding them (and are
// skipped otherwise). ANALOG_RENDER_DIR: renders for listening.
namespace
{
    juce::File bukkehornManifest()
    {
        return juce::File (__FILE__).getParentDirectory().getParentDirectory().getParentDirectory().getChildFile ("docs/sample-source/bukkehorn-4.5-manifest.json");
    }

    /** ORGANIC's part only (the oscillators off), the filter open, no effects, a plain gate. */
    Patch sampleSourcePatch()
    {
        Patch p;
        p.osc1Enabled = p.osc2Enabled = false;
        p.osc2Level = 0.0;
        p.analogAmount = 0.0;
        p.tone = 1.0;
        p.resonance = 0.0;
        p.keyTrack = 0.0;
        p.envDepthSemitones = 0.0;
        p.space = p.echo = p.motion = 0.0;
        p.ampAttack = 0.001;
        p.ampSustain = 1.0;
        p.ampRelease = 0.02;
        p.velocityRangeDb = 0.0;
        p.stereoSpread = 0.0;
        return p;
    }

    std::vector<float> renderSynthMono (Synth& synth, double seconds, const std::vector<Note>& notes)
    {
        const int total = static_cast<int> (seconds * rate);
        std::vector<float> l (static_cast<std::size_t> (total)), r (l.size()), out (l.size());
        int pos = 0;
        while (pos < total)
        {
            const int n = std::min (256, total - pos);
            for (const auto& note : notes)
                if (note.sample >= pos && note.sample < pos + n)
                {
                    const std::uint8_t m[3] { static_cast<std::uint8_t> (note.velocity > 0 ? 0x90 : 0x80), static_cast<std::uint8_t> (note.note),
                                              static_cast<std::uint8_t> (note.velocity) };
                    synth.handleMidi (m, 3);
                }
            synth.render (l.data() + pos, r.data() + pos, n);
            pos += n;
        }
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = 0.5f * (l[i] + r[i]);
        return out;
    }

    void writeListening (const juce::String& name, const std::vector<float>& mono)
    {
        const char* dir = std::getenv ("ANALOG_RENDER_DIR");
        if (dir == nullptr)
            return;
        juce::AudioBuffer<float> b (1, static_cast<int> (mono.size()));
        b.copyFrom (0, 0, mono.data(), static_cast<int> (mono.size()));
        const auto file = juce::File (dir).getChildFile (name + ".wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        if (auto stream = std::unique_ptr<juce::OutputStream> (file.createOutputStream()))
            if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (stream.get(), rate, 1, 24, {}, 0)))
            {
                stream.release();
                writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }
    }
}


TEST_CASE ("analog: Bukkehorn 4.5's Voxx imports complete and plays its recordings at their keys", "[analog][bukkehorn]")
{
    const char* folder = std::getenv ("ANALOG_BUKKEHORN_SAMPLES");
    if (folder == nullptr)
    {
        SKIP ("ANALOG_BUKKEHORN_SAMPLES not set (the recordings are not in the repository)");
    }
    const auto manifest = andor::analog::import::readManifest (bukkehornManifest());
    REQUIRE (manifest.isObject());
    // The 33 playable groups, in lane order; Voxx is lane A's eighth (group 7).
    const auto groups = andor::analog::import::playableGroups (manifest);
    CHECK (groups.size() == 33);
    const auto voxx = andor::analog::import::loadGroup (manifest, 7, juce::File (folder));
    INFO ("missing: " << voxx.missing.joinIntoString (", ") << " unreadable: " << voxx.unreadable.joinIntoString (", "));
    REQUIRE (voxx.complete());
    const auto& source = *voxx.source;
    CHECK (source.name == "Voxx");
    CHECK (source.zones.size() == 13);
    // The source's level is the instrument's raised to Kontakt's (each zone keeps its own times
    // the group's, +11.75 dB).
    CHECK (source.gain == Approx (1.2795722 * juce::Decibels::decibelsToGain (andor::analog::import::kontaktLevelMatchDb)).epsilon (1.0e-4));
    for (const auto& z : source.zones)
        CHECK (z.gain >= 0.1f * 3.866971f - 1.0e-3f);
    for (const auto& z : source.zones)
    {
        INFO (z.file);
        CHECK (z.loop.on);   // (every Voxx zone loops)
        CHECK (z.audio->sampleRate() == 44100.0);
    }

    Synth synth;
    synth.prepare (rate, block, 4);
    synth.setPatch (sampleSourcePatch());
    synth.setSampleSource (&source);
    synth.reset();

    // Key 40: one zone (VOK MOODS-St [100936], keys 36-40, root 40): the output is that
    // recording. (Many Voxx keys sound two or three overlapping zones, as below.)
    const auto out = renderSynthMono (synth, 1.2, { { 0, 40, 100 } });
    writeListening ("voxx-key40", out);
    const auto& single = *std::find_if (source.zones.begin(), source.zones.end(), [] (const SampleZone& z) { return z.keyLo == 36 && z.keyHi == 40; });
    std::vector<float> expected (out.size());
    {
        const auto& src = *single.audio;
        for (std::size_t i = 0; i < expected.size(); ++i)
            expected[i] = andor::analog::OrganicVoice::read (src, static_cast<double> (i) * src.sampleRate() / rate);
    }
    // Shape: correlated with the recording (best lag within 2 ms: the filter's small delay).
    double best = 0.0;
    const auto from = static_cast<std::size_t> (0.05 * rate), to = static_cast<std::size_t> (1.0 * rate);
    for (int lag = 0; lag < 96; ++lag)
    {
        double xy = 0.0, xx = 0.0, yy = 0.0;
        for (std::size_t i = from; i < to; ++i)
        {
            const double x = out[i + static_cast<std::size_t> (lag)], y = expected[i];
            xy += x * y;
            xx += x * x;
            yy += y * y;
        }
        best = std::max (best, xy / std::sqrt (std::max (1.0e-20, xx * yy)));
    }
    UNSCOPED_INFO ("key 40 correlation with its recording: " << best);
    CHECK (best > 0.97);
    // Level: what Kontakt's chain gives (the recording x the zone's and group's volume x the
    // instrument's), within 1.5 dB.
    {
        double o = 0.0, k = 0.0;
        const double kontakt = static_cast<double> (single.gain) * 1.2795722;
        for (std::size_t i = from; i < to; ++i)
        {
            o += static_cast<double> (out[i]) * out[i];
            k += kontakt * kontakt * static_cast<double> (expected[i]) * expected[i];
        }
        const double db = 10.0 * std::log10 (o / k);
        UNSCOPED_INFO ("level against Kontakt's chain: " << db << " dB");
        CHECK (std::abs (db) < 1.5);
    }
    CHECK (synth.sampleZonesSounding() == 1);

    // Overlapping zones sound together, as in Kontakt: key 52 three, 57 three, 47, 48 and 50 two.
    for (const auto& [key, layers] : { std::pair<int, int> { 52, 3 }, { 57, 3 }, { 47, 2 }, { 48, 2 }, { 50, 2 }, { 45, 1 } })
    {
        INFO ("key " << key);
        synth.reset();
        renderSynthMono (synth, 0.1, { { 0, key, 100 } });
        CHECK (synth.sampleZonesSounding() == layers);
    }

    // A held note outlives its recordings on the zones' loops (key 47's are 3.6 and 4.4 s long).
    synth.reset();
    const auto held = renderSynthMono (synth, 6.0, { { 0, 47, 100 } });
    writeListening ("voxx-key47-held-6s", held);
    auto rmsOver = [] (const std::vector<float>& x, double a, double b) {
        double sum = 0.0;
        const auto i0 = static_cast<std::size_t> (a * rate), i1 = std::min (x.size(), static_cast<std::size_t> (b * rate));
        for (std::size_t i = i0; i < i1; ++i)
            sum += static_cast<double> (x[i]) * x[i];
        return std::sqrt (sum / static_cast<double> (std::max<std::size_t> (1, i1 - i0)));
    };
    CHECK (rmsOver (held, 5.0, 5.8) > 0.1 * rmsOver (held, 0.2, 1.5));

    // A chord, released: nothing left sounding. Then the source removed mid-note: silent, finite.
    synth.reset();
    const auto chord = renderSynthMono (synth, 3.0, { { 0, 45, 100 }, { 0, 50, 100 }, { 0, 57, 100 }, { 96000, 45, 0 }, { 96000, 50, 0 }, { 96000, 57, 0 } });
    writeListening ("voxx-chord", chord);
    CHECK (rmsOver (chord, 2.7, 3.0) < 1.0e-6);
    CHECK (synth.activeVoices() == 0);
    renderSynthMono (synth, 0.3, { { 0, 52, 100 } });
    synth.setSampleSource (nullptr);
    const auto after = renderSynthMono (synth, 0.3, {});
    for (const float x : after)
        REQUIRE (std::isfinite (x));
    CHECK (rmsOver (after, 0.1, 0.3) < 1.0e-4);   // (-80 dB: the filter's state dying away)
    CHECK (synth.sampleZonesSounding() == 0);

    // For listening: every zone at its root (one after another), and a phrase over the range.
    if (std::getenv ("ANALOG_RENDER_DIR") != nullptr)
    {
        synth.setSampleSource (&source);
        std::vector<Note> phrase;
        int t = 0;
        for (const int key : { 36, 40, 45, 47, 48, 50, 52, 53, 57, 60, 64 })
        {
            phrase.push_back ({ t, key, 100 });
            phrase.push_back ({ t + 60000, key, 0 });
            t += 72000;
        }
        synth.reset();
        writeListening ("voxx-phrase", renderSynthMono (synth, static_cast<double> (t) / rate + 1.5, phrase));
    }
}

namespace
{
    /** A small Kontakt-like manifest (two groups, two lanes) over recordings written to `dir`. */
    juce::var syntheticManifest (const juce::File& dir)
    {
        writeRecording (dir, "Low [1].wav", 48, 1.5);
        writeRecording (dir, "High [2].wav", 60, 1.5);
        writeRecording (dir, "Other [3].wav", 55, 1.5);
        const juce::String json = R"({ "schemaVersion": 1,
            "source": { "nki": "Test.nki", "program": "Studio - Test Horn", "programVolumeLinear": 1.0 },
            "lanes": { "A": { "firstGroup": 0, "selectedSlot": 0, "selectedGroup": 0 }, "B": { "firstGroup": 1, "selectedSlot": 0, "selectedGroup": 1 } },
            "groups": [
              { "index": 0, "lane": "A", "slot": 0, "name": "Horn", "volumeLinear": 1.0, "zones": [
                  { "file": "Low [1].wav", "keyLo": 36, "keyHi": 53, "root": 48, "velLo": 1, "velHi": 127, "tuneCents": 0, "volumeLinear": 1.0, "sampleStart": 0,
                    "loops": [ { "mode": 1, "start": 12000, "length": 36000, "count": 0, "alternating": false, "xfade": 4000 } ] },
                  { "file": "High [2].wav", "keyLo": 54, "keyHi": 72, "root": 60, "velLo": 1, "velHi": 127, "tuneCents": 0, "volumeLinear": 1.0, "sampleStart": 0, "loops": [] } ] },
              { "index": 1, "lane": "B", "slot": 0, "name": "Texture", "volumeLinear": 0.5, "zones": [
                  { "file": "Other [3].wav", "keyLo": 40, "keyHi": 70, "root": 55, "velLo": 1, "velHi": 127, "tuneCents": 10, "volumeLinear": 1.0, "sampleStart": 0, "loops": [] } ] },
              { "index": 2, "lane": "C", "slot": 0, "name": "Empty", "volumeLinear": 1.0, "zones": [] } ] })";
        return juce::JSON::parse (json);
    }
}

TEST_CASE ("analog: an imported patch becomes native presets that play, save, reopen and relink", "[analog][samplesource][presets]")
{
    Scratch scratch;
    const auto samples = scratch.dir.getChildFile ("Test Horn Samples");
    samples.createDirectory();
    const auto manifest = syntheticManifest (samples);
    ShellProcessor p;
    auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
    const auto store = scratch.dir.getChildFile ("Store");
    const auto presets = scratch.dir.getChildFile ("Presets");

    // One preset per playable group, the saved lanes and the hybrid; the empty group none.
    const auto made = andor::analog::import::generatePatches (p, manifest, samples, store.getChildFile ("Imported"), presets);
    CHECK (made.missing.isEmpty());
    CHECK (made.skipped.isEmpty());
    juce::StringArray names;
    for (const auto& f : made.files)
        names.add (f.getFileNameWithoutExtension());
    CHECK (names == juce::StringArray { "A1 Horn", "B1 Texture", "Saved Lanes", "Hybrid - Horn + Oscillators" });
    CHECK (made.files[0].getParentDirectory().getFileName() == "Test Horn");
    // The recordings are kept in the store, named by their content (three files), beside the
    // import's record (its sounds).
    CHECK (store.getChildFile ("Imported").findChildFiles (juce::File::findFiles, false, "*.wav").size() == 3);
    CHECK (made.record.getFileName() == "Test Horn.import.xml");

    // A preset opened: the source loads and plays in ORGANIC's place, the oscillators off.
    auto open = [&] (ShellProcessor& q, const juce::File& file) {
        osp::plugin::InstrumentHost::PresetEntry entry;   // (as the preset list opens one)
        entry.name = file.getFileNameWithoutExtension();
        entry.file = file;
        q.openPresetEntry (entry);
    };
    // (The shell's sample store is where the engine looks for the kept recordings.)
    setSampleStore (store);
    open (p, made.files[0]);
    REQUIRE (engine.waitForSampleSource (30000));
    CHECK (engine.sampleSourceState().name == "A1 Horn");
    CHECK (engine.sampleSourceState().missing.isEmpty());
    CHECK (p.engineStatus ("sample").isEmpty());
    CHECK (p.parameterValue (params::osc1Enabled) < 0.5f);
    const std::vector<Note> note { { 0, 48, 100 } };
    const auto played = renderShell (p, 1.0, note, false);
    CHECK (rmsOf (played) > 1.0e-3);

    // Saved with the session and reopened: the same source, the same sound.
    juce::MemoryBlock session;
    p.getStateInformation (session);
    {
        ShellProcessor q;
        q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
        auto& e = dynamic_cast<AnalogEngine&> (q.productEngine());
        REQUIRE (e.waitForSampleSource (30000));
        CHECK (e.sampleSourceState().name == "A1 Horn");
        CHECK (differenceRms (renderShell (q, 1.0, note, false), played) < 1.0e-6);
    }

    // The layered saved lanes: both groups sound where they overlap (Horn's Low and Texture).
    open (p, made.files[2]);
    REQUIRE (engine.waitForSampleSource (30000));
    CHECK (engine.sampleSourceState().zones == 3);

    // A factory preset (or a reset) has no imported source: back to none, silent ORGANIC.
    p.resetSettings();
    CHECK (engine.sampleSourceDefinition() == nullptr);

    // A recording gone from the store: reported by name, and the rest still plays; relinked
    // from a folder that has it, complete again.
    open (p, made.files[0]);
    REQUIRE (engine.waitForSampleSource (30000));
    auto definition = engine.sampleSourceDefinition();
    REQUIRE (definition != nullptr);
    juce::File lowAsset;
    for (const auto* z : definition->getChildWithTagNameIterator ("Zone"))
        if (z->getStringAttribute ("file") == "Low [1].wav")
            lowAsset = store.getChildFile ("Imported").getChildFile (z->getStringAttribute ("asset"));
    REQUIRE (lowAsset.existsAsFile());
    lowAsset.deleteFile();
    {
        // (A session reopened later, on a machine without that recording.)
        ShellProcessor q;
        auto& e = dynamic_cast<AnalogEngine&> (q.productEngine());
        open (q, made.files[0]);
        REQUIRE (e.waitForSampleSource (30000));
        CHECK (e.sampleSourceState().missing == juce::StringArray { "Low [1].wav" });
        CHECK (q.engineStatus ("sample").contains ("relink"));
        CHECK (rmsOf (renderShell (q, 0.6, { { 0, 60, 100 } }, false)) > 1.0e-3);   // (High still plays)
        CHECK (andor::analog::import::relink (*definition, samples, store.getChildFile ("Imported")) == 1);
        e.setSampleSourceDefinition (std::move (definition), store.getChildFile ("Imported"));
        REQUIRE (e.waitForSampleSource (30000));
        CHECK (e.sampleSourceState().missing.isEmpty());
        CHECK (q.engineStatus ("sample").isEmpty());
    }
    setSampleStore ({});
}

TEST_CASE ("analog: Bukkehorn 4.5 becomes native presets; Voxx's plays complete through the instrument", "[analog][bukkehorn][presets]")
{
    const char* folder = std::getenv ("ANALOG_BUKKEHORN_SAMPLES");
    if (folder == nullptr)
    {
        SKIP ("ANALOG_BUKKEHORN_SAMPLES not set (the recordings are not in the repository)");
    }
    Scratch scratch;
    const auto store = scratch.dir.getChildFile ("Store");
    setSampleStore (store);
    ShellProcessor p;
    auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
    const auto manifest = andor::analog::import::readManifest (bukkehornManifest());
    const auto made = andor::analog::import::generatePatches (p, manifest, juce::File (folder), store.getChildFile ("Imported"), scratch.dir.getChildFile ("Presets"));
    juce::StringArray names;
    for (const auto& f : made.files)
        names.add (f.getFileNameWithoutExtension());
    UNSCOPED_INFO ("generated: " << names.joinIntoString (", "));
    UNSCOPED_INFO ("skipped (none of their recordings here): " << made.skipped.size());
    CHECK (names.contains ("A8 Voxx"));
    CHECK (names.contains ("Saved Lanes"));
    CHECK (names.contains ("Hybrid - Voxx + Oscillators"));
    CHECK (made.files[0].getParentDirectory().getFileName() == "Bukkehorn 4.5");

    const auto voxx = made.files[names.indexOf ("A8 Voxx")];
    osp::plugin::InstrumentHost::PresetEntry entry;
    entry.name = "A8 Voxx";
    entry.file = voxx;
    p.openPresetEntry (entry);
    REQUIRE (engine.waitForSampleSource (60000));
    CHECK (engine.sampleSourceState().missing.isEmpty());
    CHECK (engine.sampleSourceState().zones == 13);
    // The definition keeps its link to the NKI group it came from.
    const auto definition = engine.sampleSourceDefinition();
    REQUIRE (definition != nullptr);
    CHECK (definition->getStringAttribute ("origin") == "Evenant Engine v2 - Finding Rhythm - Bukkehorn 4.5.nki");
    CHECK (definition->getChildByName ("Group")->getIntAttribute ("index") == 7);

    std::vector<Note> phrase;
    int t = 0;
    for (const int key : { 40, 45, 47, 50, 52, 57, 60 })
    {
        phrase.push_back ({ t, key, 100 });
        phrase.push_back ({ t + 72000, key, 0 });
        t += 96000;
    }
    const auto played = renderShell (p, static_cast<double> (t) / rate + 1.5, phrase, false);
    CHECK (rmsOf (played) > 1.0e-3);
    if (std::getenv ("ANALOG_RENDER_DIR") != nullptr)
    {
        std::vector<float> mono (static_cast<std::size_t> (played.getNumSamples()));
        for (int i = 0; i < played.getNumSamples(); ++i)
            mono[static_cast<std::size_t> (i)] = 0.5f * (played.getSample (0, i) + played.getSample (1, i));
        writeListening ("preset-A8-Voxx", mono);
        entry.name = "Hybrid";
        entry.file = made.files[names.indexOf ("Hybrid - Voxx + Oscillators")];
        p.openPresetEntry (entry);
        REQUIRE (engine.waitForSampleSource (60000));
        const auto hybrid = renderShell (p, static_cast<double> (t) / rate + 1.5, phrase, false);
        for (int i = 0; i < hybrid.getNumSamples(); ++i)
            mono[static_cast<std::size_t> (i)] = 0.5f * (hybrid.getSample (0, i) + hybrid.getSample (1, i));
        writeListening ("preset-Hybrid-Voxx", mono);
    }
    setSampleStore ({});
}

TEST_CASE ("analog: the NKI reader refuses what it cannot read, with a reason, never a crash", "[analog][samplesource][nki]")
{
    Scratch scratch;
    auto attempt = [&] (const juce::String& name, const juce::MemoryBlock& bytes) {
        const auto f = scratch.dir.getChildFile (name);
        f.replaceWithData (bytes.getData(), bytes.getSize());
        return andor::analog::import::readNki (f);
    };
    juce::MemoryBlock garbage;
    juce::Random random (7);
    for (int i = 0; i < 4096; ++i)
        garbage.append (&i, 1);
    for (auto* r = static_cast<juce::uint8*> (garbage.getData()); r < static_cast<juce::uint8*> (garbage.getData()) + garbage.getSize(); ++r)
        *r = static_cast<juce::uint8> (random.nextInt (256));
    const auto bad = attempt ("garbage.nki", garbage);
    CHECK (bad.manifest.isVoid());
    CHECK (bad.error.isNotEmpty());
    CHECK (attempt ("empty.nki", {}).error.isNotEmpty());
    CHECK (andor::analog::import::readNki (scratch.dir.getChildFile ("absent.nki")).error.isNotEmpty());

    // The real NKI (not in the repository) when given: every truncation refused cleanly, and the
    // whole file read exactly as tools/kontakt reads it (the committed manifest).
    const char* nkiPath = std::getenv ("ANALOG_BUKKEHORN_NKI");
    if (nkiPath == nullptr)
        return;
    juce::MemoryBlock nki;
    REQUIRE (juce::File (nkiPath).loadFileAsData (nki));
    for (size_t cut : { size_t { 20 }, size_t { 300 }, nki.getSize() / 2, nki.getSize() - 1 })
    {
        INFO ("cut at " << cut);
        CHECK (attempt ("cut.nki", juce::MemoryBlock (nki.getData(), cut)).error.isNotEmpty());
    }
    const auto read = andor::analog::import::readNki (juce::File (nkiPath));
    INFO (read.error);
    REQUIRE (read.manifest.isObject());
    const auto tools = andor::analog::import::readManifest (bukkehornManifest());
    CHECK (read.manifest["source"]["program"].toString() == tools["source"]["program"].toString());
    CHECK (static_cast<double> (read.manifest["source"]["programVolumeLinear"]) == Approx (static_cast<double> (tools["source"]["programVolumeLinear"])));
    for (const char* lane : { "A", "B", "C", "D" })
        CHECK (static_cast<int> (read.manifest["lanes"][lane]["selectedGroup"]) == static_cast<int> (tools["lanes"][lane]["selectedGroup"]));
    const auto* mine = read.manifest["groups"].getArray();
    const auto* theirs = tools["groups"].getArray();
    REQUIRE (mine != nullptr);
    REQUIRE (theirs != nullptr);
    REQUIRE (mine->size() == theirs->size());
    int zones = 0;
    for (int g = 0; g < mine->size(); ++g)
    {
        const auto& a = mine->getReference (g);
        const auto& b = theirs->getReference (g);
        INFO ("group " << g << " " << b["name"].toString());
        CHECK (a["name"].toString() == b["name"].toString());
        CHECK (a["lane"].toString() == b["lane"].toString());
        CHECK (static_cast<int> (a["slot"]) == static_cast<int> (b["slot"]));
        const auto* za = a["zones"].getArray();
        const auto* zb = b["zones"].getArray();
        REQUIRE (za->size() == zb->size());
        for (int z = 0; z < za->size(); ++z)
        {
            const auto& x = za->getReference (z);
            const auto& y = zb->getReference (z);
            CHECK (x["file"].toString() == y["file"].toString());
            for (const char* key : { "keyLo", "keyHi", "velLo", "velHi", "root", "frames", "rate", "channels", "sampleStart" })
                CHECK (static_cast<int> (x[key]) == static_cast<int> (y[key]));
            CHECK (static_cast<double> (x["tuneCents"]) == Approx (static_cast<double> (y["tuneCents"])).margin (0.01));
            CHECK (static_cast<double> (x["volumeLinear"]) == Approx (static_cast<double> (y["volumeLinear"])).epsilon (1.0e-5));
            CHECK (x["loops"].size() == y["loops"].size());
            if (x["loops"].size() > 0)
            {
                CHECK (static_cast<int> (x["loops"][0]["start"]) == static_cast<int> (y["loops"][0]["start"]));
                CHECK (static_cast<int> (x["loops"][0]["length"]) == static_cast<int> (y["loops"][0]["length"]));
                CHECK (static_cast<int> (x["loops"][0]["xfade"]) == static_cast<int> (y["loops"][0]["xfade"]));
            }
            ++zones;
        }
    }
    CHECK (zones == 333);
    // Its samples are expected beside it, in the folder its file list names.
    CHECK (read.manifest["source"]["sampleFolder"].toString() == "Evenant Engine v2 - Finding Rhythm - Bukkehorn 4.5 Samples");
}

TEST_CASE ("analog: an imported source takes ORGANIC's card; its sounds are picked from its heading; the A/B mix blends it", "[analog][samplesource][editor]")
{
    Scratch scratch;
    const auto samples = scratch.dir.getChildFile ("Test Horn Samples");
    samples.createDirectory();
    const auto manifest = syntheticManifest (samples);
    const auto store = scratch.dir.getChildFile ("Store");
    setSampleStore (store);
    ShellProcessor p;
    auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
    const auto made = andor::analog::import::generatePatches (p, manifest, samples, store.getChildFile ("Imported"), scratch.dir.getChildFile ("Presets"));
    REQUIRE (made.record.existsAsFile());
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    ui.refreshNow();
    REQUIRE (ui.visibleCardCount() == 3);   // (empty: card C's stand-in)

    auto open = [&] (int index) {
        osp::plugin::InstrumentHost::PresetEntry entry;
        entry.name = made.files[index].getFileNameWithoutExtension();
        entry.file = made.files[index];
        p.openPresetEntry (entry);
        REQUIRE (engine.waitForSampleSource (30000));
        ui.refreshNow();
    };
    // An imported sound: ORGANIC's card shows it (its name, its picture), the dock gives way.
    open (0);
    CHECK (ui.visibleCardCount() == 3);
    osp::plugin::InstrumentHost::GeneratedSource shown;
    REQUIRE (p.generatedSource (1, shown));
    CHECK (shown.name == juce::String::fromUTF8 ("Test Horn \xc2\xb7 A1 Horn"));
    CHECK (shown.cycle.size() > 16);
    CHECK (ui.engineCard (1).hasHeadingDoor());

    // Its heading opens the sounds; a click plays another (the patch's settings stay).
    const auto tone = p.parameterValue (params::tone);
    ui.openPopup (andor::shell::ShellEditor::hybridHeaderPopup);
    auto* panel = ui.openPopupPanel();
    REQUIRE (panel != nullptr);
    juce::TextButton* texture = nullptr;
    std::function<void (juce::Component&)> find = [&] (juce::Component& c) {
        for (auto* child : c.getChildren())
        {
            if (auto* b = dynamic_cast<juce::TextButton*> (child); b != nullptr && b->getButtonText() == "B1 Texture")
                texture = b;
            find (*child);
        }
    };
    find (*panel);
    REQUIRE (texture != nullptr);
    texture->onClick();
    REQUIRE (engine.waitForSampleSource (30000));
    CHECK (engine.sampleSourceState().name == "B1 Texture");
    CHECK (p.parameterValue (params::tone) == tone);
    ui.closePopup();

    // A/B mix (the hybrid preset: the oscillators and the source): all A is the oscillators
    // alone, all B the source alone.
    open (3);
    const std::vector<Note> note { { 0, 48, 100 } };
    p.setParameterValue ("ab.blend", 0.0f);
    const auto allA = renderShell (p, 0.6, note, false);
    p.setParameterValue ("ab.blend", 1.0f);
    const auto allB = renderShell (p, 0.6, note, false);
    p.setParameterValue ("ab.blend", 0.5f);
    const auto both = renderShell (p, 0.6, note, false);
    engine.setSampleSourceDefinition (nullptr, {});
    REQUIRE (engine.waitForSampleSource (30000));
    p.setParameterValue ("ab.blend", 0.0f);
    const auto oscillatorsOnly = renderShell (p, 0.6, note, false);
    CHECK (differenceRms (allA, oscillatorsOnly) < 1.0e-5);
    CHECK (rmsOf (allB) > 1.0e-3);
    CHECK (differenceRms (both, allA) > 1.0e-4);
    CHECK (differenceRms (both, allB) > 1.0e-4);
    ui.refreshNow();
    // (Card C still an INSTRUMENT PATCH, none chosen: its card says so; Remove makes it a SAMPLE again.)
    REQUIRE (p.generatedSource (1, shown));
    CHECK (shown.name == "INSTRUMENT PATCH");
    p.removeLayer (ShellProcessor::hybridLayer);   // (the card's Remove: what it shows)
    ui.refreshNow();
    CHECK (engine.hybridType() == andor::analog::HybridType::sample);
    REQUIRE (p.generatedSource (1, shown));
    CHECK (shown.placeholder.startsWith ("ADD SAMPLE"));   // (nothing: the stand-in again)

    // A recording dropped onto card C plays (C becomes a SAMPLE); the imported sound stays,
    // dormant, and plays again when C is an INSTRUMENT PATCH. Nothing is lost on the way.
    open (0);
    CHECK (engine.hybridType() == andor::analog::HybridType::instrumentPatch);
    const auto patchSound = renderShell (p, 0.6, note, false);
    loadOrganic (p, writeRecording (scratch.dir, "organic.wav", 57, 1.0));
    ui.refreshNow();
    CHECK (engine.hybridType() == andor::analog::HybridType::sample);
    CHECK (engine.sampleSourceDefinition() != nullptr);
    CHECK (ui.visibleCardCount() == 3);
    const auto recordingSound = renderShell (p, 0.6, note, false);
    CHECK (differenceRms (recordingSound, patchSound) > 1.0e-4);
    engine.setHybridType (andor::analog::HybridType::instrumentPatch);
    CHECK (differenceRms (renderShell (p, 0.6, note, false), patchSound) < 2.0e-3);
    CHECK (p.loadState (ShellProcessor::hybridLayer) == osp::plugin::InstrumentHost::LoadState::ready);   // (the recording kept)
    engine.setHybridType (andor::analog::HybridType::sample);
    CHECK (differenceRms (renderShell (p, 0.6, note, false), recordingSound) < 2.0e-3);
    p.removeLayer (ShellProcessor::hybridLayer);
    ui.refreshNow();
    CHECK (engine.sampleSourceDefinition() != nullptr);   // (still kept for its type)
    REQUIRE (p.generatedSource (1, shown));
    CHECK (shown.placeholder.startsWith ("ADD SAMPLE"));
    editor.reset();
    setSampleStore ({});
}

TEST_CASE ("analog: the import review counts what it found and what is missing", "[analog][samplesource][nki]")
{
    Scratch scratch;
    const auto samples = scratch.dir.getChildFile ("Test Horn Samples");
    samples.createDirectory();
    const auto manifest = syntheticManifest (samples);
    {
        andor::analog::ImportReview review (manifest, samples);
        CHECK (review.groupsFound() == 2);
        CHECK (review.recordingsTotal() == 3);
        CHECK (review.recordingsMissing() == 0);
        int started = -2;
        review.onImport = [&started] (int g) { started = g; };
        review.setSize (560, 520);
        CHECK (review.summary().contains ("2 sounds"));
    }
    samples.getChildFile ("High [2].wav").deleteFile();
    andor::analog::ImportReview partial (manifest, samples);
    CHECK (partial.recordingsMissing() == 1);
    CHECK (partial.summary().contains ("1 not in the folder"));
}

TEST_CASE ("analog: screenshots of the Sample Source importer (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-import]")
{
    const char* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    const auto before = osp::plugin::skin::look();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    Scratch scratch;
    // The real Bukkehorn manifest and recordings when given, else the synthetic ones.
    const char* folder = std::getenv ("ANALOG_BUKKEHORN_SAMPLES");
    juce::var manifest;
    juce::File samples;
    if (folder != nullptr)
    {
        manifest = andor::analog::import::readManifest (bukkehornManifest());
        samples = juce::File (folder);
    }
    else
    {
        samples = scratch.dir.getChildFile ("Test Horn Samples");
        samples.createDirectory();
        manifest = syntheticManifest (samples);
    }
    const auto store = scratch.dir.getChildFile ("Store");
    setSampleStore (store);
    {
        ShellProcessor p;
        auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
        auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
        auto shoot = [&] (const juce::String& name, juce::Component& c) {
            ui.refreshNow();
            const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
            const auto file = out.getChildFile (name + ".png");
            file.deleteFile();
            juce::FileOutputStream stream (file);
            juce::PNGImageFormat().writeImageToStream (image, stream);
        };
        editor->setSize (1448, 1086);
        shoot ("import-dock", *editor);

        andor::analog::ImportReview review (manifest, samples);
        review.setSize (560, 520);
        shoot ("import-review", review);

        const auto made = andor::analog::import::generatePatches (p, manifest, samples, store.getChildFile ("Imported"), scratch.dir.getChildFile ("Presets"));
        int index = 0;
        for (int i = 0; i < made.files.size(); ++i)
            if (made.files[i].getFileNameWithoutExtension().contains ("Voxx") && ! made.files[i].getFileNameWithoutExtension().contains ("Hybrid"))
                index = i;
        osp::plugin::InstrumentHost::PresetEntry entry;
        entry.name = made.files[index].getFileNameWithoutExtension();
        entry.file = made.files[index];
        p.openPresetEntry (entry);
        REQUIRE (engine.waitForSampleSource (60000));
        renderShell (p, 0.8, { { 0, 45, 100 }, { 0, 52, 100 } }, false);
        shoot ("import-card-b-playing", *editor);
        ui.openPopup (andor::shell::ShellEditor::hybridHeaderPopup);
        shoot ("import-sound-popover", *editor);
        ui.closePopup();
        editor.reset();
    }
    setSampleStore ({});
    osp::plugin::skin::setLook (before);
}

namespace
{
    const andor::analog::FactoryPatch& factoryNamed (const std::string& name)
    {
        for (const auto& f : factoryPatches())
            if (f.name == name)
                return f;
        FAIL ("no factory patch " << name);
        return factoryPatches().front();
    }
}

TEST_CASE ("analog: the AMP envelope on screen is what plays in the Juno patches (ENV LINK, VCA GATE)", "[analog][juno][envelope]")
{
    const std::vector<Note> held { { 0, 57, 100 }, { 0, 64, 100 }, { 20000, 57, 0 }, { 20000, 64, 0 } };
    SECTION ("ENV LINK: the AMP envelope shapes amplitude and filter; FILTER ENVELOPE rests")
    {
        ShellProcessor p;
        apply (p, factoryNamed ("Brass Section 106").patch);
        setValue (p, "space", 0.0f);
        REQUIRE (p.parameterValue (params::envLink) >= 0.5f);
        const auto reference = renderShell (p, 0.6, held, false);
        const double again = differenceRms (reference, renderShell (p, 0.6, held, false));   // (the same settings: the floor)
        setValue (p, "attack", 900.0f);   // AMP's attack: heard
        const double ampMoved = differenceRms (reference, renderShell (p, 0.6, held, false));
        setValue (p, "attack", 90.0f);
        setValue (p, params::filterAttack, 1.5f);   // the filter's own: resting while linked
        const double filterMoved = differenceRms (reference, renderShell (p, 0.6, held, false));
        UNSCOPED_INFO ("again " << again << ", AMP attack " << ampMoved << ", filter attack " << filterMoved);
        CHECK (ampMoved > 1.0e-3);
        CHECK (filterMoved <= again * 1.5 + 1.0e-9);
    }
    SECTION ("VCA GATE: the AMP picture says GATE; editing the envelope gives the amplitude back to it")
    {
        ShellProcessor p;
        apply (p, factoryNamed ("DCO Bass 106").patch);
        REQUIRE (p.parameterValue (params::vcaMode) >= 0.5f);
        const auto& info = andor::shell::productInfo();
        REQUIRE (info.ampEnvelopeNote != nullptr);
        CHECK (info.ampEnvelopeNote (p) == "GATE");
        // A preset or automation setting AMP's values is not the musician editing it.
        setValue (p, "release", 800.0f);
        CHECK (p.parameterValue (params::vcaMode) >= 0.5f);
        const auto gated = renderShell (p, 0.6, held, false);
        // A knob or handle starts a gesture: the amplitude follows the envelope again.
        auto* attack = p.parameters.getParameter ("attack");
        attack->beginChangeGesture();
        attack->setValueNotifyingHost (attack->convertTo0to1 (400.0f));
        attack->endChangeGesture();
        CHECK (p.parameterValue (params::vcaMode) < 0.5f);
        CHECK (info.ampEnvelopeNote (p).isEmpty());
        CHECK (differenceRms (gated, renderShell (p, 0.6, held, false)) > 1.0e-3);
    }
}

TEST_CASE ("analog: a version-6 session with ENV LINK keeps its sound (its filter envelope becomes AMP's)", "[analog][state][juno]")
{
    auto prepare = [] (ShellProcessor& q) {
        apply (q, factoryNamed ("Brass Section 106").patch);
        setValue (q, "space", 0.0f);
        setValue (q, params::filterAttack, 0.2f);
        setValue (q, params::filterDecay, 0.9f);
        setValue (q, params::filterSustain, 30.0f);
        setValue (q, params::filterRelease, 0.5f);
    };
    juce::MemoryBlock session;
    {
        ShellProcessor old;
        prepare (old);
        setValue (old, "attack", 5.0f);   // AMP's, unused by version 6 while linked
        setValue (old, "release", 3000.0f);
        old.getStateInformation (session);
    }
    auto xml = juce::AudioProcessor::getXmlFromBinary (session.getData(), static_cast<int> (session.getSize()));
    REQUIRE (xml != nullptr);
    xml->setAttribute ("productVersion", 6);
    juce::MemoryBlock v6;
    juce::AudioProcessor::copyXmlToBinary (*xml, v6);

    ShellProcessor reopened;
    reopened.setStateInformation (v6.getData(), static_cast<int> (v6.getSize()));
    CHECK (reopened.parameterValue ("attack") == Approx (200.0f).margin (0.5f));
    CHECK (reopened.parameterValue ("decay") == Approx (900.0f).margin (0.5f));
    CHECK (reopened.parameterValue ("sustainLevel") == Approx (30.0f).margin (0.01f));
    CHECK (reopened.parameterValue ("release") == Approx (500.0f).margin (0.5f));

    // ... and an unlinked session's AMP is left alone.
    ShellProcessor plain;
    apply (plain, factoryPatches()[0].patch);
    setValue (plain, params::envLink, 0.0f);
    setValue (plain, "attack", 5.0f);
    juce::MemoryBlock plainSession;
    plain.getStateInformation (plainSession);
    auto plainXml = juce::AudioProcessor::getXmlFromBinary (plainSession.getData(), static_cast<int> (plainSession.getSize()));
    plainXml->setAttribute ("productVersion", 6);
    juce::AudioProcessor::copyXmlToBinary (*plainXml, plainSession);
    ShellProcessor plainReopened;
    plainReopened.setStateInformation (plainSession.getData(), static_cast<int> (plainSession.getSize()));
    CHECK (plainReopened.parameterValue ("attack") == Approx (5.0f).margin (0.01f));
}

TEST_CASE ("analog: each oscillator's well reveals its model's own controls on hover, at every window size; pitch and level stay on the card", "[analog][editor][overlay]")
{
    using namespace params;
    struct Expect
    {
        int model;
        std::vector<juce::String> one, two;   // OSC 1's and OSC 2's controls for that model
    };
    const std::vector<Expect> expected {
        { 0, { osc1Wave, osc1Width, lfoWidth1 }, { osc2Wave, osc2Width, lfoWidth2 } },
        { 1, { osc1DcoSaw, osc1DcoPulse, osc1DcoSub, osc1Width, osc1DcoPwmDepth, osc1DcoPwmSource },
          { osc2DcoSaw, osc2DcoPulse, osc2DcoSub, osc2Width, osc2DcoPwmDepth, osc2DcoPwmSource } },
        { 2, { osc1WideWave, osc1WideShape, osc1WideSpread, osc1Width, lfoWidth1 }, { osc2WideWave, osc2WideShape, osc2WideSpread, osc2Width, lfoWidth2 } },
    };
    const juce::StringArray cardLevel { osc1Level, osc1Coarse, osc1Octave, osc1Fine, osc2Level, osc2Coarse, osc2Octave, osc2Fine, "layerA.pan" };
    for (const int width : { 869, 1448, 1810 })
    {
        INFO ("window " << width);
        OverlayFixture f (width);
        for (const int layer : { 0, 2 })
        {
            const int slot = layer == 0 ? 1 : 2;
            for (const auto& e : expected)
            {
                INFO ("OSC " << slot << " model " << e.model);
                setValue (f.p, slot == 1 ? osc1Model : osc2Model, static_cast<float> (e.model));
                auto* o = f.overlay (layer);
                REQUIRE (o != nullptr);
                CHECK (o->getBounds() == f.card (layer).display().getBounds());
                // At rest: the waveform, and only the model's dropdown over it.
                o->setRevealedForTest (false);
                CHECK (OverlayFixture::shown (*o, slot == 1 ? osc1Model : osc2Model));
                for (const auto& id : slot == 1 ? e.one : e.two)
                    CHECK_FALSE (OverlayFixture::shown (*o, id));
                // Hovered: the model's own controls, inside the well, none overlapping.
                o->setRevealedForTest (true);
                const auto boxes = OverlayFixture::checkLayout (*o, static_cast<int> ((slot == 1 ? e.one : e.two).size()) + 1);
                for (const auto& id : slot == 1 ? e.one : e.two)
                {
                    INFO (id);
                    CHECK (OverlayFixture::shown (*o, id));
                }
                // Never the card's own business: pitch and level.
                for (const auto& id : cardLevel)
                    CHECK (OverlayFixture::control (*o, id) == nullptr);
                // WIDE: SHAPE and SPREAD side by side, a pair.
                if (e.model == 2)
                {
                    const auto shape = boxes.find (slot == 1 ? osc1WideShape : osc2WideShape), spread = boxes.find (slot == 1 ? osc1WideSpread : osc2WideSpread);
                    REQUIRE (shape != boxes.end());
                    REQUIRE (spread != boxes.end());
                    CHECK (spread->second.getX() >= shape->second.getRight());
                    CHECK (spread->second.getX() - shape->second.getRight() <= 4);
                    CHECK (spread->second.getY() == shape->second.getY());
                }
            }
            setValue (f.p, slot == 1 ? osc1Model : osc2Model, 0.0f);
        }
        // The cards' own knobs: DETUNE, TUNE, OCT, LEVEL of that oscillator.
        CHECK (f.card (0).hasControl (osc1Fine));
        CHECK (f.card (0).hasControl (osc1Coarse));
        CHECK (f.card (0).hasControl (osc1Octave));
        CHECK (f.card (0).hasControl (osc1Level));
        CHECK (f.card (2).hasControl (osc2Fine));
        CHECK (f.card (2).hasControl (osc2Level));
    }
    // A model's settings outlive another model playing.
    ShellProcessor p;
    setValue (p, osc1Model, 1.0f);
    setValue (p, osc1DcoSub, 60.0f);
    setValue (p, osc1Model, 2.0f);
    setValue (p, osc1WideSpread, 30.0f);
    setValue (p, osc1Model, 1.0f);
    CHECK (p.parameterValue (osc1DcoSub) == Approx (60.0f));
    CHECK (p.parameterValue (osc1WideSpread) == Approx (30.0f));
}

TEST_CASE ("analog: card C is a SAMPLE, OSC 3, NOISE or an INSTRUMENT PATCH; only the selected one plays, and each survives save and reload", "[analog][hybrid]")
{
    using namespace params;
    using andor::analog::HybridType;
    const std::vector<Note> note { { 0, 57, 100 } };
    auto prepare = [] (ShellProcessor& q) {
        setValue (q, "space", 0.0f);
        setValue (q, "echo", 0.0f);
        setValue (q, osc1Enabled, 0.0f);   // (the analog pair silent: card C alone)
        setValue (q, osc2Enabled, 0.0f);
        q.setParameterValue ("ab.blend", 0.5f);
    };
    ShellProcessor p;
    prepare (p);
    auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
    // A new instance: an empty SAMPLE, no sound from C.
    CHECK (engine.hybridType() == HybridType::sample);
    CHECK (rmsOf (renderShell (p, 0.5, note, false)) < 1.0e-5);

    // OSC 3: a third oscillator, tuned by its own card knobs.
    engine.setHybridType (HybridType::oscillator);
    const auto osc3 = renderShell (p, 0.5, note, false);
    CHECK (rmsOf (osc3) > 1.0e-3);
    setValue (p, osc3Coarse, 7.0f);
    CHECK (differenceRms (renderShell (p, 0.5, note, false), osc3) > 1.0e-3);
    osp::plugin::InstrumentHost::GeneratedSource shown;
    REQUIRE (p.generatedSource (1, shown));
    CHECK (shown.name == "OSC 3");
    CHECK (shown.periodic);

    // NOISE: its own, not following the key, the same on every render (seeded).
    engine.setHybridType (HybridType::noise);
    setValue (p, hybridNoiseTone, 40.0f);
    setValue (p, keyTrack, 0.0f);   // (the shared filter's key tracking off: the source itself is what is measured)
    const auto low = renderShell (p, 0.5, { { 0, 40, 100 } }, false), high = renderShell (p, 0.5, { { 0, 84, 100 } }, false);
    REQUIRE (rmsOf (low) > 1.0e-3);
    CHECK (std::abs (20.0 * std::log10 (rmsOf (high) / rmsOf (low))) < 1.5);
    REQUIRE (p.generatedSource (1, shown));
    CHECK (shown.name == "NOISE");
    setValue (p, hybridNoiseColor, 1.0f);
    setValue (p, hybridNoiseMovement, 70.0f);
    juce::MemoryBlock session;
    p.getStateInformation (session);
    {
        ShellProcessor a, b;
        a.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
        b.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
        auto& ea = dynamic_cast<AnalogEngine&> (a.productEngine());
        CHECK (ea.hybridType() == HybridType::noise);
        CHECK (a.parameterValue (hybridNoiseMovement) == Approx (70.0f));
        CHECK (differenceRms (renderShell (a, 0.5, note, false), renderShell (b, 0.5, note, false)) < 1.0e-7);
    }

    // A recording loaded while C is NOISE: C becomes a SAMPLE and plays it; NOISE comes back as it was.
    Scratch scratch;
    loadOrganic (p, writeRecording (scratch.dir, "hybrid.wav", 57, 1.5));
    CHECK (engine.hybridType() == HybridType::sample);
    const auto recording = renderShell (p, 0.5, note, false);
    CHECK (rmsOf (recording) > 1.0e-3);
    engine.setHybridType (HybridType::noise);
    const auto noiseAgain = renderShell (p, 0.5, note, false);
    CHECK (differenceRms (noiseAgain, recording) > 1.0e-3);   // (the recording silent beneath)
    engine.setHybridType (HybridType::sample);
    CHECK (differenceRms (renderShell (p, 0.5, note, false), recording) < 2.0e-3);

    // The cards' knobs and the well follow C's type.
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
        auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
        editor->setSize (1448, 1086);
        for (const auto type : { HybridType::sample, HybridType::oscillator, HybridType::noise, HybridType::instrumentPatch })
        {
            INFO ("type " << static_cast<int> (type));
            engine.setHybridType (type);
            ui.refreshNow();
            auto& card = ui.engineCard (1);
            REQUIRE (card.isVisible());
            CHECK (card.hasControl ("layerB.level"));
            CHECK (card.hasControl (osc3Fine) == (type == HybridType::oscillator));
            CHECK (card.hasControl (osc3Octave) == (type == HybridType::oscillator));
            auto* o = card.wellOverlayComponent();
            REQUIRE (o != nullptr);
            o->setRevealedForTest (false);
            CHECK (OverlayFixture::shown (*o, hybridType));   // (its type, always)
            o->setRevealedForTest (true);
            OverlayFixture::checkLayout (*o, 1);
            CHECK (OverlayFixture::shown (*o, hybridNoiseColor) == (type == HybridType::noise));
            CHECK (card.hasControl (hybridNoiseTone) == (type == HybridType::noise));
            CHECK (card.hasControl (hybridNoiseKeys) == (type == HybridType::noise));
            CHECK (card.hasControl (hybridVelo) == (type == HybridType::noise || type == HybridType::oscillator));
            CHECK (card.hasControl (hybridOctave) == (type == HybridType::sample || type == HybridType::instrumentPatch));
            CHECK (OverlayFixture::shown (*o, osc3Model) == (type == HybridType::oscillator));
            CHECK (OverlayFixture::shown (*o, osc3Wave) == (type == HybridType::oscillator));
        }
        engine.setHybridType (HybridType::sample);
    }
}

TEST_CASE ("analog: a session from before three engines that held an imported sound opens with card C its INSTRUMENT PATCH", "[analog][hybrid][state][samplesource]")
{
    Scratch scratch;
    const auto samples = scratch.dir.getChildFile ("Test Horn Samples");
    samples.createDirectory();
    const auto manifest = syntheticManifest (samples);
    const auto store = scratch.dir.getChildFile ("Store");
    setSampleStore (store);
    juce::MemoryBlock v7;
    {
        ShellProcessor p;
        auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
        auto definition = andor::analog::import::makeDefinition (manifest, { 0 }, "A1 Horn", samples, store.getChildFile ("Imported"));
        REQUIRE (definition.xml != nullptr);
        engine.setSampleSourceDefinition (std::move (definition.xml), store.getChildFile ("Imported"));
        REQUIRE (engine.waitForSampleSource (30000));
        juce::MemoryBlock session;
        p.getStateInformation (session);
        auto xml = juce::AudioProcessor::getXmlFromBinary (session.getData(), static_cast<int> (session.getSize()));
        REQUIRE (xml != nullptr);
        xml->setAttribute ("productVersion", 7);   // as version 7 wrote it: no hybrid type
        std::function<void (juce::XmlElement&)> drop = [&] (juce::XmlElement& e) {
            for (auto* child = e.getFirstChildElement(); child != nullptr;)
            {
                auto* next = child->getNextElement();
                if (child->getStringAttribute ("id") == params::hybridType)
                    e.removeChildElement (child, true);
                else
                    drop (*child);
                child = next;
            }
        };
        drop (*xml);
        juce::AudioProcessor::copyXmlToBinary (*xml, v7);
    }
    ShellProcessor q;
    q.setStateInformation (v7.getData(), static_cast<int> (v7.getSize()));
    auto& e = dynamic_cast<AnalogEngine&> (q.productEngine());
    REQUIRE (e.waitForSampleSource (30000));
    CHECK (e.hybridType() == andor::analog::HybridType::instrumentPatch);
    setValue (q, "space", 0.0f);
    CHECK (rmsOf (renderShell (q, 0.5, { { 0, 57, 100 } }, false)) > 1.0e-3);
    setSampleStore ({});
}

TEST_CASE ("analog: MIX sets the analog pair against whatever card C plays; MORPH only on C's recording as a SAMPLE", "[analog][hybrid][morph]")
{
    using namespace params;
    using andor::analog::HybridType;
    const std::vector<Note> note { { 0, 57, 100 } };
    ShellProcessor p;
    setValue (p, "space", 0.0f);
    setValue (p, "echo", 0.0f);
    auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
    const auto pairOnly = renderShell (p, 0.5, note, false);   // (C empty: the INIT pair, untouched)
    for (const auto type : { HybridType::oscillator, HybridType::noise })
    {
        INFO ("type " << static_cast<int> (type));
        engine.setHybridType (type);
        p.setParameterValue ("ab.blend", 0.0f);   // all the analog pair
        CHECK (differenceRms (renderShell (p, 0.5, note, false), pairOnly) < 2.0e-3);
        p.setParameterValue ("ab.blend", 1.0f);   // all card C
        const auto cOnly = renderShell (p, 0.5, note, false);
        CHECK (rmsOf (cOnly) > 1.0e-3);
        CHECK (differenceRms (cOnly, pairOnly) > 1.0e-3);
        p.setParameterValue ("ab.blend", 0.5f);   // both, each at full level (the centre's law)
        const auto both = renderShell (p, 0.5, note, false);
        CHECK (differenceRms (both, pairOnly) > 1.0e-3);
        CHECK (differenceRms (both, cOnly) > 1.0e-3);
        // MORPH says it cannot act: C plays no recording.
        CHECK (p.engineStatus ("morph").isNotEmpty());
    }
    engine.setHybridType (HybridType::sample);
    // With a recording as C's SAMPLE, MORPH analyses it; C as NOISE, the dormant recording is not morphed.
    Scratch scratch;
    loadOrganic (p, writeRecording (scratch.dir, "morph.wav", 57, 2.5));
    engine.setHybridType (HybridType::noise);
    CHECK (p.engineStatus ("morph").contains ("NOISE"));
    engine.setHybridType (HybridType::sample);
    CHECK_FALSE (p.engineStatus ("morph").contains ("NOISE"));
}

TEST_CASE ("analog: screenshots of the three engines in their combinations, every width (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-three]")
{
    using namespace params;
    using andor::analog::HybridType;
    const auto* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    const auto before = osp::plugin::skin::look();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    Scratch scratch;
    const auto store = scratch.dir.getChildFile ("Store");
    setSampleStore (store);
    ShellProcessor p;
    auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    auto shoot = [&] (const juce::String& name) {
        ui.refreshNow();
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = out.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    auto reveal = [&] (int layer, bool shown) {
        ui.refreshNow();
        if (auto* o = ui.engineCard (layer).wellOverlayComponent())
            o->setRevealedForTest (shown);
    };
    auto models = [&] (int one, int two) {
        setValue (p, osc1Model, static_cast<float> (one));
        setValue (p, osc2Model, static_cast<float> (two));
    };
    // A synthetic imported instrument (its sounds) for the INSTRUMENT PATCH state.
    const auto samples = scratch.dir.getChildFile ("Test Horn Samples");
    samples.createDirectory();
    const auto manifest = syntheticManifest (samples);
    andor::analog::import::generatePatches (p, manifest, samples, store.getChildFile ("Imported"), scratch.dir.getChildFile ("Presets"));
    const auto recording = writeRecording (scratch.dir, "nyckelharpa stand-in A3.wav", 57, 2.5);
    for (const int width : { 1448, 869, 1810 })
    {
        const auto w = juce::String (width);
        editor->setSize (width, juce::roundToInt (static_cast<float> (width) * 1086.0f / 1448.0f));
        p.removeLayer (ShellProcessor::hybridLayer);
        engine.setSampleSourceDefinition (nullptr, {});
        engine.setHybridType (HybridType::sample);
        models (0, 0);
        shoot ("three-" + w + "-1-classic-classic-empty");
        models (2, 1);
        setValue (p, osc1WideWave, 4.0f);
        setValue (p, osc1WideSpread, 45.0f);
        setValue (p, osc2DcoPulse, 1.0f);
        setValue (p, osc2DcoSub, 50.0f);
        shoot ("three-" + w + "-2-wide-dco-empty");
        reveal (0, true);
        shoot ("three-" + w + "-2-wide-dco-empty-osc1-hover");
        reveal (0, false);
        reveal (2, true);
        shoot ("three-" + w + "-2-wide-dco-empty-osc2-hover");
        reveal (2, false);
        models (0, 2);
        loadOrganic (p, recording);
        shoot ("three-" + w + "-3-classic-wide-sample");
        models (1, 0);
        andor::analog::chooseSound (p, *andor::analog::import::readRecord (store.getChildFile ("Imported"), "Test Horn.import.xml")->getFirstChildElement());
        REQUIRE (engine.waitForSampleSource (30000));
        shoot ("three-" + w + "-4-dco-classic-instrument");
        reveal (1, true);
        shoot ("three-" + w + "-4-dco-classic-instrument-c-hover");
        reveal (1, false);
        models (0, 1);
        engine.setHybridType (HybridType::oscillator);
        setValue (p, osc3Model, 2.0f);
        setValue (p, osc3WideWave, 4.0f);
        shoot ("three-" + w + "-5-classic-dco-osc3-wide");
        reveal (1, true);
        shoot ("three-" + w + "-5-classic-dco-osc3-wide-c-hover");
        reveal (1, false);
        engine.setHybridType (HybridType::noise);
        setValue (p, hybridNoiseTone, 55.0f);
        shoot ("three-" + w + "-6-classic-dco-noise");
        reveal (1, true);
        shoot ("three-" + w + "-6-classic-dco-noise-c-hover");
        reveal (1, false);
        if (width == 1448)
        {
            // Each card's EQ (A OSC 1, B OSC 2, C the hybrid).
            for (const int layer : { 0, 2, 1 })
            {
                ui.engineCard (layer).onEqToggle (layer, true);
                shoot ("three-1448-7-eq-" + osp::plugin::InstrumentHost::shownLayerName (layer));
                ui.engineCard (layer).onEqToggle (layer, false);
            }
            ui.openPopup (2);   // FILTER: its three pages
            for (int page = 0; page < 3; ++page)
            {
                shoot ("three-1448-8-filter-popover-" + juce::String (page + 1));
                std::function<juce::Component* (juce::Component&)> find = [&] (juce::Component& c) -> juce::Component* {
                    for (auto* child : c.getChildren())
                    {
                        if (child->getTitle() == "Pages")
                            return child;
                        if (auto* f = find (*child))
                            return f;
                    }
                    return nullptr;
                };
                if (auto* t = find (*ui.openPopupPanel()))
                    t->keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
            }
            ui.closePopup();
            // SPACE: the rooms, then ABYSS (GRAVITY below 0: the swell; FROZEN: held).
            ui.openPopup (4);
            shoot ("three-1448-10-space-rooms");
            setValue (p, "space.engine", 1.0f);
            setValue (p, "space.abyss.gravity", -60.0f);
            setValue (p, "space.abyss.resonance", 30.0f);
            shoot ("three-1448-10-space-abyss");
            setValue (p, "space.abyss.freeze", 1.0f);
            setValue (p, "space.abyss.gravity", 40.0f);
            shoot ("three-1448-10-space-abyss-frozen");
            setValue (p, "space.engine", 2.0f);   // VINTAGE
            setValue (p, "space.vintage.variant", 1.0f);
            shoot ("three-1448-10-space-vintage-plate");
            setValue (p, "space.engine", 3.0f);   // BLOOM, synced, then a WASH
            setValue (p, "space.bloom.diffusion", 25.0f);
            shoot ("three-1448-10-space-bloom");
            setValue (p, "space.bloom.variant", 3.0f);
            setValue (p, "space.bloom.diffusion", 85.0f);
            shoot ("three-1448-10-space-bloom-wash");
            for (const char* id : { "space.engine", "space.abyss.gravity", "space.abyss.resonance", "space.abyss.freeze", "space.vintage.variant",
                                    "space.bloom.variant", "space.bloom.diffusion" })
                if (auto* q = p.parameters.getParameter (id))
                    q->setValueNotifyingHost (q->getDefaultValue());
            ui.closePopup();
            // GRANULAR per card (state version 8): A granular, B regular; then both; the
            // oscillator overlay gives the well to the grains.
            setValue (p, "layerA.sourceMode", 1.0f);
            reveal (0, true);
            ui.engineCard (0).showGranularControls (true, true);
            renderShell (p, 1.2, { { 0, 57, 100 }, { 0, 64, 100 } }, false);
            shoot ("three-1448-9-granular-a-regular-b");
            setValue (p, "layerC.sourceMode", 1.0f);
            reveal (2, true);
            ui.engineCard (2).showGranularControls (true, true);
            renderShell (p, 1.2, { { 0, 57, 100 }, { 0, 64, 100 } }, false);
            shoot ("three-1448-9-granular-a-granular-b");
            setValue (p, "layerA.sourceMode", 0.0f);
            setValue (p, "layerC.sourceMode", 0.0f);
            reveal (0, false);
            reveal (2, false);
        }
        setValue (p, osc3Model, 0.0f);
        setValue (p, osc3WideWave, 0.0f);
        setValue (p, osc1WideWave, 0.0f);
        setValue (p, osc1WideSpread, 0.0f);
        setValue (p, osc2DcoPulse, 0.0f);
        setValue (p, osc2DcoSub, 0.0f);
    }
    editor.reset();
    setSampleStore ({});
    osp::plugin::skin::setLook (before);
}

TEST_CASE ("analog: a modulation source dragged onto a Granular card reaches its grain controls (cards A and B)", "[analog][editor][modulation][granular]")
{
    // The pointer's own hover never reaches a card while a source is dragged: resting on a
    // Granular card's display brings its POS SIZE DENS TUNE SPREAD up, as in ANDOR OSP.
    using D = osp::mod::Dest;
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, 1086);
    setValue (p, "layerA.sourceMode", 1.0f);
    setValue (p, "layerC.sourceMode", 1.0f);
    ui.refreshNow();
    auto& content = ui.contentForTest();
    const int lfo1 = osp::mod::slot::lfo1;
    for (const auto& [layer, dest] : { std::pair<int, D> { 0, D::grainPositionA }, { 2, D::grainPositionC } })
    {
        INFO ("layer " << layer);
        auto& card = ui.engineCard (layer);
        REQUIRE (card.isGranular());
        CHECK_FALSE (card.granularControlsShown());
        const auto display = content.getLocalArea (&card, card.display().getBounds()).toFloat();
        ui.dragModulationForTest (lfo1, display.getCentre());
        juce::Thread::sleep (420);   // (a rest, not a pass across it)
        ui.dragModulationForTest (lfo1, display.getCentre());
        CHECK (card.granularControlsShown());
        juce::Slider* pos = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c) {
            for (auto* child : c.getChildren())
            {
                if (auto* s = dynamic_cast<juce::Slider*> (child); s != nullptr && s->getProperties()["paramId"].toString() == osp::plugin::InstrumentHost::layerParameterId (layer, "granular.position"))
                    pos = s;
                find (*child);
            }
        };
        find (card);
        REQUIRE (pos != nullptr);
        CHECK (pos->isVisible());
        const auto at = content.getLocalArea (pos, pos->getLocalBounds()).toFloat().getCentre();
        ui.dragModulationForTest (lfo1, at);
        ui.dropModulationForTest (lfo1, at);
        bool routed = false;
        for (const auto& r : p.modulationRoutes())
            routed = routed || (r.route.dest == dest && r.route.source == osp::mod::Source::lfo1);
        CHECK (routed);
    }
}
