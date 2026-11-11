// ANDOR ANALOG through the ANDOR Instrument Shell: its parameters and patches, its factory
// presets, MIDI at its own samples, state recall, and the rule that every control the
// interface enables moves the sound while every one it disables does not.
// (Compiled into andor_analog_tests with the shell's own tests, tests/shell/ShellTests.cpp.)

#include "core/PlatformTag.h"
#include "AnalogEngine.h"
#include "AnalogParameters.h"
#include "ArpSection.h"
#include "Binding.h"
#include "CeramicSkin.h"
#include "FactoryPatches.h"
#include "AnalogWellOverlays.h"
#include "MacroPopups.h"
#include "RackControls.h"
#include "ParameterRack.h"
#include "AnalogRacks.h"
#include "FxPresets.h"
#include "FxRack.h"
#include "FxRouting.h"
#include "FxIdentity.h"
#include "fx/Effects.h"
#include "OspLookAndFeel.h"
#include "ModulationPanel.h"
#include "SampleImport.h"
#include "SamplePatches.h"
#include "KontaktImport.h"
#include "SampleSourceUI.h"
#include "ShellEditor.h"
#include "SubPanel.h"
#include "ShellProcessor.h"

#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "engine/RhythmicShaper.h"
#include "io/AudioFileIO.h"

#include <set>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>

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

    /** The mod wheel renderShell moves to at its start (CC 1, 0..127; below 0: untouched). */
    int renderModWheel = -1;

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
            if (renderModWheel >= 0 && pos == 0)
                midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, renderModWheel), 0);
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
        auto reimaginedModeB = [] (osp::ReimaginedMode m) {   // (card B's own, state version 14)
            return [m] (Patch& c) {
                c.reimagined2 = 0.8;
                c.reimaginedSettings2.mode = m;
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
            { "space.abyss.variant", abyss },
            { "space.vintage.variant", vintage },
            { "space.vintage.era", vintage },
            { "space.vintage.diffusion", vintage },
            { "space.bloom.variant", bloom },
            { "space.bloom.sync", bloom },
            { "space.bloom.division", bloom },
            { "space.bloom.time", bloomFree },
            { "space.bloom.diffusion", bloom },
            { "echo.ascend.interval", [] (Patch& c) { c.effects.echoType = osp::EchoType::ascend; } },
            { "echo.ascend.motion", [] (Patch& c) { c.effects.echoType = osp::EchoType::ascend; } },
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
            // Card B's own REIMAGINED (the shell's layer C): its mode settings, heard while it plays.
            { "layerC.reimagined", [] (Patch& c) { c.reimaginedSettings2.mode = osp::ReimaginedMode::kaleidoscope; } },
            { "layerC.reimagined.mode", reimaginedModeB (osp::ReimaginedMode::kaleidoscope) },
            { "layerC.reimagined.kaleidoscope.focus", reimaginedModeB (osp::ReimaginedMode::kaleidoscope) },
            { "layerC.reimagined.kaleidoscope.spread", reimaginedModeB (osp::ReimaginedMode::kaleidoscope) },
            { "layerC.reimagined.tapeFrame.age", reimaginedModeB (osp::ReimaginedMode::tapeFrame) },
            { "layerC.reimagined.tapeFrame.stability", reimaginedModeB (osp::ReimaginedMode::tapeFrame) },
            { "layerC.reimagined.tapeFrame.frame", [] (Patch& c) {
                  c.granular2 = true;   // FRAME is the tape's length: heard as card B's grains follow it
                  c.reimagined2 = 0.8;
                  c.reimaginedSettings2.mode = osp::ReimaginedMode::tapeFrame;
              } },
            { "layerC.reimagined.toybox.motion", reimaginedModeB (osp::ReimaginedMode::toybox) },
            { "layerC.reimagined.toybox.digital", reimaginedModeB (osp::ReimaginedMode::toybox) },
            { "layerC.reimagined.toybox.play", [] (Patch& c) {
                  c.granular2 = true;   // PLAY is how its heads move: heard as card B's grains follow them
                  c.reimagined2 = 0.8;
                  c.reimaginedSettings2.mode = osp::ReimaginedMode::toybox;
              } },
            { "layerC.reimagined.mirage.clock", reimaginedModeB (osp::ReimaginedMode::mirage) },
            { "layerC.reimagined.mirage.filter", reimaginedModeB (osp::ReimaginedMode::mirage) },
            { "layerC.reimagined.mirage.tone", reimaginedModeB (osp::ReimaginedMode::mirage) },
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
        // State version 11: MOVEMENT's lanes. Lane A's SYNC and CYCLE act on a PULSE, its PHASE
        // on a SHAPER; lane B's settings act with lane B on, in their mode.
        {
            using namespace params;
            auto laneA = [] (osp::MovementMode m, bool sync) { return [m, sync] (Patch& c) { c.effects.movementMode = m; c.effects.pulseSync = sync; }; };
            modes[movementLaneId (0, "sync")] = laneA (osp::MovementMode::pulse, false);
            modes[movementLaneId (0, "pulse.division")] = laneA (osp::MovementMode::pulse, true);
            modes[movementLaneId (0, "phase")] = laneA (osp::MovementMode::shaper, false);
            auto laneB = [] (osp::MovementMode m, bool sync) {
                return [m, sync] (Patch& c) {
                    c.movementBLane.on = true;
                    c.movementB.movementMode = m;
                    c.movementB.pulseSync = sync;
                };
            };
            for (const char* name : { "level", "mode", "drift.speed", "drift.pitch", "drift.tone" })
                modes[movementBPrefix + name] = laneB (osp::MovementMode::drift, false);
            modes[movementBPrefix + "mode"] = laneB (osp::MovementMode::tape, false);   // (from TAPE to another)
            for (const char* name : { "tape.wow", "tape.flutter", "tape.wear" })
                modes[movementBPrefix + name] = laneB (osp::MovementMode::tape, false);
            for (const char* name : { "chorus.rate", "chorus.width", "chorus.stereo" })
                modes[movementBPrefix + name] = laneB (osp::MovementMode::chorus, false);
            for (const char* name : { "pulse.rate", "pulse.shape", "pulse.stereo", "sync" })
                modes[movementBPrefix + name] = laneB (osp::MovementMode::pulse, false);
            modes[movementBPrefix + "pulse.division"] = laneB (osp::MovementMode::pulse, true);
            for (const char* name : { "phase", "shaper.pattern", "shaper.rate", "shaper.target", "shaper.smooth" })
                modes[movementBPrefix + name] = laneB (osp::MovementMode::shaper, false);
            // CUSTOM plays lane B's own steps (THREE's, by default) instead of another pattern.
            modes[movementBPrefix + "shaper.custom"] = [laneB] (Patch& c) {
                laneB (osp::MovementMode::shaper, false) (c);
                c.movementB.shaper.pattern = 7;
            };
        }
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
        // State version 16: card C's second layer (on, as an oscillator by default; each model's
        // and type's settings in that model or type).
        auto c2 = [] (HybridType type, OscModel m, std::function<void (Patch&)> more = {}) {
            return [type, m, more] (Patch& c) {
                c.hybrid2.enabled = true;
                c.hybrid2.type = type;
                c.hybrid2.model = m;
                c.hybrid2.wave = Osc2Wave::all;
                if (more != nullptr)
                    more (c);
            };
        };
        for (const auto& id : { params::hybrid2Type, params::hybrid2Level, params::hybrid2Octave, params::hybrid2Coarse, params::hybrid2Fine, params::hybrid2Velo,
                                params::hybrid2Model, params::hybrid2Wave, params::hybrid2Width })
            modes[id] = c2 (HybridType::oscillator, OscModel::classic);
        for (const auto& id : { params::hybrid2DcoSaw, params::hybrid2DcoPulse, params::hybrid2DcoSub })
            modes[id] = c2 (HybridType::oscillator, OscModel::dco);
        modes[params::hybrid2DcoPwmSource] = c2 (HybridType::oscillator, OscModel::dco, [] (Patch& c) { c.hybrid2.dco.pulse = true; c.hybrid2.dco.pwmDepth = 0.6; });
        modes[params::hybrid2DcoPwmDepth] = c2 (HybridType::oscillator, OscModel::dco, [] (Patch& c) { c.hybrid2.dco.pulse = true; c.hybrid2.dco.pwmSource = PwmSource::lfo; });
        for (const auto& id : { params::hybrid2WideWave, params::hybrid2WideShape })
            modes[id] = c2 (HybridType::oscillator, OscModel::wide);
        modes[params::hybrid2WideSpread] = c2 (HybridType::oscillator, OscModel::wide, [] (Patch& c) { c.hybrid2.wide.wave = WideWave::superSaw; });
        for (const auto& id : { params::hybrid2NoiseColor, params::hybrid2NoiseTone, params::hybrid2NoiseMovement })
            modes[id] = c2 (HybridType::noise, OscModel::classic);
        modes[params::hybrid2NoiseKeys] = c2 (HybridType::noise, OscModel::classic, [] (Patch& c) { c.hybrid2.noiseTone = 0.4; });
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
                if (id == params::morphDetail)
                    c.tone = 1.0;   // (DETAIL's upper partials heard through an open filter)
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
    // (Documents is the real one here: a file left by an earlier run is already listed.)
    const auto own = p.presetFolder().getChildFile ("Mine.andoranalog");
    const bool existed = own.existsAsFile();
    REQUIRE (p.savePreset (own));
    ShellProcessor q;
    CHECK (own.existsAsFile());
    CHECK (q.presetList().size() == list.size() + (existed ? 0 : 1));
    own.deleteFile();   // (the musician's folder as it was)
}

TEST_CASE ("analog: a factory update replaces only the factory files it wrote; the musician's stay", "[analog][presets]")
{
    // Its own Documents folder (ANDOR_DOCUMENTS_DIR), so the real factory set is not touched.
    Scratch scratch;
    struct Documents
    {
        explicit Documents (const juce::String& path)
        {
           #if JUCE_WINDOWS
            ::_putenv_s ("ANDOR_DOCUMENTS_DIR", path.toRawUTF8());
           #else
            ::setenv ("ANDOR_DOCUMENTS_DIR", path.toRawUTF8(), 1);
           #endif
        }
        ~Documents()
        {
           #if JUCE_WINDOWS
            ::_putenv_s ("ANDOR_DOCUMENTS_DIR", "");
           #else
            ::unsetenv ("ANDOR_DOCUMENTS_DIR");
           #endif
        }
    } documents (scratch.dir.getFullPathName());
    ShellProcessor p;
    const auto factory = p.presetFolder().getChildFile ("Factory");
    const auto manifest = factory.getChildFile (".manifest");
    REQUIRE (manifest.existsAsFile());
    juce::StringArray lines;
    manifest.readLines (lines);
    lines.removeEmptyStrings();
    CHECK (lines.size() == static_cast<int> (factoryPatches().size()));
    REQUIRE (lines.size() >= 2);
    // A musician's preset saved into Factory, and a factory preset they saved over (changed).
    const auto mine = factory.getChildFile ("Mine In Factory.andoranalog");
    REQUIRE (p.savePreset (mine));
    const auto overwritten = factory.getChildFile (lines[0].fromFirstOccurrenceOf (" ", false, false));
    const auto untouched = factory.getChildFile (lines[1].fromFirstOccurrenceOf (" ", false, false));
    REQUIRE (overwritten.existsAsFile());
    REQUIRE (untouched.existsAsFile());
    setValue (p, "gain", -12.0f);
    REQUIRE (p.savePreset (overwritten));
    const auto theirs = overwritten.loadFileAsString();
    const auto untouchedBefore = untouched.getLastModificationTime();
    // An update brings another factory set.
    factory.getChildFile (".version").replaceWithText ("analog-factory-older");
    juce::Thread::sleep (1100);   // (file times have a resolution of a second on some systems)
    ShellProcessor q;
    CHECK (mine.existsAsFile());
    CHECK (overwritten.loadFileAsString() == theirs);                       // their save kept
    CHECK (untouched.existsAsFile());
    CHECK (untouched.getLastModificationTime() > untouchedBefore);          // rewritten by the update
    CHECK (factory.getChildFile (".version").loadFileAsString().trim() != "analog-factory-older");
    // The new manifest lists what it wrote now (not the musician's files).
    juce::StringArray after;
    factory.getChildFile (".manifest").readLines (after);
    CHECK_FALSE (after.joinIntoString ("\n").contains ("Mine In Factory"));
    CHECK_FALSE (after.joinIntoString ("\n").contains (overwritten.getFileName()));
}

TEST_CASE ("analog: the plugin plays exactly what the engine renders from the same patch", "[analog][engine]")
{
    ShellProcessor p;
    apply (p, factoryPatches()[1].patch);
    const auto shell = renderShell (p, 0.6, phrase, false);

    params::Reader reader;
    reader.attach (p.parameters);
    auto synthOwned = std::make_unique<Synth>();   // (on the heap: a Synth is larger than some platforms' stacks)
    Synth& synth = *synthOwned;
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
    for (const char* id : { "arp.pattern", "arp.rate", "arp.gate", "arp.octaves", "arp.swing", "arp.velocity", "analog.arp.link", "analog.arp.cardA", "analog.arp.cardB", "analog.arp.cardC" })
        setups[id] = [] (ShellProcessor& q) {
            setValue (q, "arp.enabled", 1.0f);
            setValue (q, "arp.rate", 3.0f);
        };
    // The per-card arpeggiator's own settings act on their card while unlinked.
    for (const char* id : { "analog.arp.cardB.pattern", "analog.arp.cardB.rate", "analog.arp.cardB.octaves", "analog.arp.cardC.pattern", "analog.arp.cardC.rate", "analog.arp.cardC.octaves" })
        setups[id] = [] (ShellProcessor& q) {
            setValue (q, "arp.enabled", 1.0f);
            setValue (q, "analog.arp.link", 0.0f);
            setValue (q, "arp.rate", 3.0f);
            setValue (q, "analog.arp.cardB.rate", 3.0f);
            setValue (q, "analog.arp.cardC.rate", 3.0f);
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
    // SUB's settings act while SUB is on (Cinematic V6 gate 2; the phrase is in its range and
    // above TAPER's C4).
    for (const auto* id : { &params::subWave, &params::subOctave, &params::subGrit, &params::subLevel, &params::subLowKey, &params::subHighKey,
                            &params::subResponse, &params::subTaperStart, &params::subTaperSlope })
        setups[*id] = [] (ShellProcessor& q) { setValue (q, params::subEnabled, 1.0f); };
    // The cards' sends act in a MAIN + SEND route (Cinematic V6 gate 4), card C heard (its
    // second layer) and SPACE's room on SEND.
    for (const auto* id : { &params::sendA, &params::sendB, &params::sendC })
        setups[*id] = [] (ShellProcessor& q) {
            dynamic_cast<AnalogEngine&> (q.productEngine()).setFxRoute (andor::analog::fx::mainSendGraph (andor::analog::fx::classicGraph()));
            setValue (q, params::hybrid2Enabled, 1.0f);
            setValue (q, "space", 80.0f);
        };
    int checkedBound = 0, checkedUnbound = 0, checkedFx = 0;
    for (auto* base : p.getParameters())
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (base);
        REQUIRE (param != nullptr);
        const auto id = param->getParameterID();
        INFO (id);
        auto patch = context;
        if (const auto mode = modes.find (id); mode != modes.end())
            mode->second (patch);
        renderModWheel = id == params::lfoWheel ? 127 : -1;   // (WHEEL acts with the wheel up)
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
        if (id.startsWith ("analog.fx."))
        {
            // The routed effects' (V3: declared read so their racks stay live): heard only once
            // their effect is in a CUSTOM route ([fx] tests); in CLASSIC they change nothing.
            CHECK (isBound (id));
            CHECK (identical (reference, changed));
            ++checkedFx;
        }
        else if (isBound (id))
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
    renderModWheel = -1;
    // Every listed one (the two wheels are not parameters), the arpeggiator's seven ("arp.*")
    // and the per-card arpeggiator's ten (LINK, the three cards, B's and C's own three), and
    // the modulation's: LFO 2, ENV 1 and 2, the 16 routes' five ("mod.*"; LFO 1's are listed).
    CHECK (checkedBound == bound.size() - 6 + 7 + 10 + (8 + 2 * 7 + 5 * osp::mod::maxRoutes));   // (- 6: "analog.arp.*" and "analog.fx.*" are patterns too)
    CHECK (checkedFx > 300);   // (every routed effect's slot parameters: declared, silent in CLASSIC)
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
        // (MORPH, on in a new instrument since version 9, stays off in a session from before it.)
        CHECK (parameter->getValue() == Approx (id == params::morphEnabled ? 0.0f : parameter->getDefaultValue()).margin (1.0e-6));
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

TEST_CASE ("analog: taking the MORPH knob in hand switches MORPH on; the factory presets have it on", "[analog][morph]")
{
    ShellProcessor p;
    setValue (p, params::morphEnabled, 0.0f);   // (a preset or session with MORPH off)
    auto* position = p.parameters.getParameter (params::morphPosition);
    REQUIRE (position != nullptr);
    position->beginChangeGesture();
    position->setValueNotifyingHost (0.7f);
    position->endChangeGesture();
    CHECK (p.parameterValue (params::morphEnabled) >= 0.5f);
    // Automation (no gesture) leaves the switch as it is.
    setValue (p, params::morphEnabled, 0.0f);
    position->setValueNotifyingHost (0.3f);
    CHECK (p.parameterValue (params::morphEnabled) < 0.5f);
    for (const auto& f : andor::analog::factoryPatches())
    {
        INFO (f.name);
        CHECK (f.patch.morph.enabled);
        CHECK_FALSE (f.patch.wheelToLfo);
    }
}

TEST_CASE ("analog: the mod wheel moves nothing in a new instrument; a session from before keeps its wheel on LFO 1", "[analog][state][wheel]")
{
    {
        ShellProcessor p;
        CHECK (p.parameterValue (params::lfoWheel) < 0.5f);
    }
    juce::MemoryBlock session;
    {
        ShellProcessor old;
        old.getStateInformation (session);
    }
    auto xml = juce::AudioProcessor::getXmlFromBinary (session.getData(), static_cast<int> (session.getSize()));
    REQUIRE (xml != nullptr);
    xml->setAttribute ("productVersion", 9);
    juce::MemoryBlock v9;
    juce::AudioProcessor::copyXmlToBinary (*xml, v9);
    ShellProcessor p;
    p.setStateInformation (v9.getData(), static_cast<int> (v9.getSize()));
    CHECK (p.parameterValue (params::lfoWheel) >= 0.5f);   // (version 9 always linked the wheel to LFO 1)
    ShellProcessor q;
    q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
    CHECK (q.parameterValue (params::lfoWheel) < 0.5f);
}

TEST_CASE ("analog: a version-8 session keeps its MORPH: its position turned round, on or off as saved", "[analog][state][morph]")
{
    // Version 9 hears card C live, runs the position the other way (0 % the oscillators) and
    // is on in a new instrument. A session from before keeps what it had.
    for (const bool on : { false, true })
    {
        juce::MemoryBlock session;
        {
            ShellProcessor old;
            setValue (old, params::morphEnabled, on ? 1.0f : 0.0f);
            setValue (old, params::morphPosition, 30.0f);   // (version 8: 30 % of the way to the carrier)
            old.getStateInformation (session);
        }
        auto xml = juce::AudioProcessor::getXmlFromBinary (session.getData(), static_cast<int> (session.getSize()));
        REQUIRE (xml != nullptr);
        xml->setAttribute ("productVersion", 8);
        juce::MemoryBlock v8;
        juce::AudioProcessor::copyXmlToBinary (*xml, v8);
        ShellProcessor p;
        p.setStateInformation (v8.getData(), static_cast<int> (v8.getSize()));
        INFO ((on ? "on" : "off"));
        CHECK ((p.parameterValue (params::morphEnabled) >= 0.5f) == on);
        CHECK (p.parameterValue (params::morphPosition) == Approx (70.0f));
        // A version-9 session keeps its position as saved.
        ShellProcessor q;
        q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
        CHECK (q.parameterValue (params::morphPosition) == Approx (30.0f));
    }
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
    // (The layers as they are: MORPH, on in a new instrument, is MORPH's test.)
    Scratch scratch;
    const auto file = writeRecording (scratch.dir, "nyckelharpa stand-in A3.wav", 57, 2.5);
    ShellProcessor p;
    setValue (p, "space", 0.0f);
    setValue (p, params::morphEnabled, 0.0f);
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

TEST_CASE ("analog: MORPH is the first macro, on and half way in a new instrument; it fuses the oscillators with card C", "[analog][organic][morph]")
{
    Scratch scratch;
    ShellProcessor p;
    setValue (p, "space", 0.0f);
    CHECK (p.parameterValue (params::morphEnabled) >= 0.5f);
    CHECK (p.parameterValue (params::morphPosition) == Approx (50.0f));
    // Card C silent: said so; MORPH on changes nothing (the oscillators play as they are).
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
    // The ends: 0 % the oscillators, 100 % the recording (the oscillators replaced by it);
    // with LEVEL MATCH off, each exactly as it plays.
    setValue (p, params::morphLevelMatch, 0.0f);
    setValue (p, params::morphPosition, 0.0f);
    CHECK (differenceRms (renderShell (p, 0.8), synth) < 1.0e-3 * std::max (1.0e-6, rmsOf (synth)) + 1.0e-6);
    setValue (p, params::morphPosition, 100.0f);
    const auto recordingEnd = renderShell (p, 0.8);
    setValue (p, params::morphEnabled, 0.0f);
    setValue (p, params::osc1Enabled, 0.0f);
    setValue (p, params::osc2Enabled, 0.0f);
    CHECK (differenceRms (renderShell (p, 0.8), recordingEnd) < 1.0e-3 * rmsOf (recordingEnd) + 1.0e-6);
    setValue (p, params::osc1Enabled, 100.0f);
    setValue (p, params::osc2Enabled, 100.0f);
    setValue (p, params::morphEnabled, 1.0f);
    setValue (p, params::morphLevelMatch, 1.0f);
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
    {
        // The per-card arpeggiator: C alone plays the arpeggio (A and B hold the chord); then
        // unlinked, B's own pattern (DOWN) and RATE in the editor.
        std::function<osp::plugin::ArpInlinePanel* (juce::Component&)> find = [&] (juce::Component& c) -> osp::plugin::ArpInlinePanel* {
            if (auto* panel = dynamic_cast<osp::plugin::ArpInlinePanel*> (&c))
                return panel;
            for (auto* child : c.getChildren())
                if (auto* f = find (*child))
                    return f;
            return nullptr;
        };
        auto* panel = find (ui);
        REQUIRE (panel != nullptr);
        CHECK (panel->showsCards());
        setValue (p, "analog.arp.cardA", 0.0f);
        setValue (p, "analog.arp.cardB", 0.0f);
        renderShell (p, 0.8, { { 0, 57, 100 }, { 0, 64, 100 }, { 0, 69, 100 } }, false);
        panel->refresh();
        shoot ("analog-arp-card-c");
        setValue (p, "analog.arp.cardA", 1.0f);
        setValue (p, "analog.arp.cardB", 1.0f);
        setValue (p, "analog.arp.cardC", 0.0f);   // (C holds the chord)
        setValue (p, "analog.arp.link", 0.0f);
        setValue (p, "analog.arp.cardB.pattern", 1.0f);
        setValue (p, "analog.arp.cardB.rate", 2.0f);
        panel->setEditCard (1);
        CHECK (panel->editedCard() == 1);
        renderShell (p, 0.8, { { 0, 57, 100 }, { 0, 64, 100 }, { 0, 69, 100 } }, false);
        panel->refresh();
        shoot ("analog-arp-unlinked-b");
        panel->setEditCard (0);
        for (const char* id : { "analog.arp.cardA", "analog.arp.cardB", "analog.arp.cardC", "analog.arp.link", "analog.arp.cardB.pattern", "analog.arp.cardB.rate" })
            if (auto* q = p.parameters.getParameter (id))
                q->setValueNotifyingHost (q->getDefaultValue());
    }
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

    auto synthOwned = std::make_unique<Synth>();   // (on the heap: a Synth is larger than some platforms' stacks)
    Synth& synth = *synthOwned;
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

TEST_CASE ("analog: MIX sets the analog pair against whatever card C plays; MORPH fuses them, whatever C plays", "[analog][hybrid][morph]")
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
        // MORPH (on, half way, as a new instrument has it) hears card C live: it is ready, and
        // at the mix's centre the two are fused, not layered.
        CHECK (p.engineStatus ("morph").isEmpty());
        p.setParameterValue (params::morphEnabled, 0.0f);
        const auto layered = renderShell (p, 0.5, note, false);
        p.setParameterValue (params::morphEnabled, 1.0f);
        CHECK (differenceRms (both, layered) > 1.0e-3);
    }
    // Card C as an empty SAMPLE: MORPH says it has nothing to fuse; with a recording, ready.
    engine.setHybridType (HybridType::sample);
    CHECK (p.engineStatus ("morph").isNotEmpty());
    Scratch scratch;
    loadOrganic (p, writeRecording (scratch.dir, "morph.wav", 57, 2.5));
    CHECK (p.engineStatus ("morph").isEmpty());
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
        editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
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
            setValue (p, "space.abyss.variant", 2.0f);   // INVERSE: the swell at GRAVITY 0
            setValue (p, "space.abyss.gravity", 0.0f);
            shoot ("three-1448-10-space-abyss-inverse");
            setValue (p, "space.abyss.variant", 3.0f);   // ENDLESS
            shoot ("three-1448-10-space-abyss-endless");
            setValue (p, "space.abyss.variant", 1.0f);   // DARK, frozen, heavy
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
            for (const char* id : { "space.engine", "space.abyss.gravity", "space.abyss.resonance", "space.abyss.freeze", "space.abyss.variant", "space.vintage.variant",
                                    "space.bloom.variant", "space.bloom.diffusion" })
                if (auto* q = p.parameters.getParameter (id))
                    q->setValueNotifyingHost (q->getDefaultValue());
            ui.closePopup();
            // ECHO: ASCEND (its second row: INTERVAL, MOTION; the repeats' pitches in the picture).
            setValue (p, "echo.type", 2.0f);
            setValue (p, "echo.feedback", 60.0f);
            ui.openPopup (5);
            shoot ("three-1448-11-echo-ascend");
            setValue (p, "echo.ascend.interval", 1.0f);   // +7, STEADY
            setValue (p, "echo.ascend.motion", 1.0f);
            shoot ("three-1448-11-echo-ascend-steady-fifth");
            for (const char* id : { "echo.type", "echo.feedback", "echo.ascend.interval", "echo.ascend.motion" })
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

TEST_CASE ("analog: an imported Kontakt patch writes no presets; earlier imports' presets leave the list, the user's stay", "[analog][samplesource][presets]")
{
    Scratch scratch;
    const auto samples = scratch.dir.getChildFile ("Test Horn Samples");
    samples.createDirectory();
    const auto manifest = syntheticManifest (samples);
    ShellProcessor p;
    const auto store = scratch.dir.getChildFile ("Store").getChildFile ("Imported");
    const auto presets = scratch.dir.getChildFile ("Presets");

    // Today's import: the sounds in the record (card C's SOUNDS), no preset anywhere.
    const auto made = andor::analog::import::generatePatches (p, manifest, samples, store);
    CHECK (made.files.isEmpty());
    const auto record = andor::analog::import::readRecord (store, made.record.getFileName());
    REQUIRE (record != nullptr);
    juce::StringArray sounds;
    for (const auto* d : record->getChildWithTagNameIterator ("SampleSource"))
        sounds.add (d->getStringAttribute ("name"));
    CHECK (sounds == juce::StringArray { "A1 Horn", "B1 Texture", "Saved Lanes" });

    // An earlier import's presets (as it wrote them), a preset the user saved among them and
    // one elsewhere: the import's leave, the user's stay.
    const auto earlier = andor::analog::import::generatePatches (p, manifest, samples, store, presets);
    REQUIRE (earlier.files.size() == 4);
    const auto ext = andor::shell::productInfo().presetExtension;
    const auto mineBeside = presets.getChildFile ("Test Horn").getChildFile ("My Horn Pad" + ext);
    const auto mineElsewhere = presets.getChildFile ("Mine" + ext);
    REQUIRE (earlier.files[0].copyFileTo (mineBeside));
    REQUIRE (earlier.files[0].copyFileTo (mineElsewhere));
    CHECK (andor::analog::import::retireGeneratedPresets (presets, store) == 4);
    for (const auto& f : earlier.files)
        CHECK_FALSE (f.existsAsFile());
    CHECK (mineBeside.existsAsFile());
    CHECK (mineElsewhere.existsAsFile());
    CHECK (andor::analog::import::retireGeneratedPresets (presets, store) == 0);   // (nothing left to move)
    // ... and the import's sounds still play from the record.
    CHECK (andor::analog::import::readRecord (store, made.record.getFileName()) != nullptr);
}

namespace
{
    /** What the voices start, block by block: a voice's note beginning (held), with its cards. */
    struct LaneOnset
    {
        double seconds;
        int note;
        std::uint8_t cards;
    };

    struct LanePlay
    {
        std::vector<LaneOnset> onsets;
        int minHeldOn = 99, maxHeldOn = 0;   ///< held voices on `holdCards` while the keys are down (after 0.3 s)
        int heldAtEnd = 0, activeAtEnd = 0;
        int allCardVoices = 0;               ///< onsets on every card (the one arpeggiator's)
    };

    /** Holds C E G from the start until `release` seconds, renders `seconds`; `each` runs
        before every block (its start in seconds). */
    LanePlay playLanes (ShellProcessor& p, double seconds, double release, std::uint8_t holdCards,
                        const std::function<void (double)>& each = {})
    {
        p.prepareToPlay (rate, block);
        auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
        const auto& synth = engine.synthForTest();
        LanePlay result;
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
            int heldOn = 0;
            for (int s = 0; s < Synth::maxVoices; ++s)
            {
                const auto v = synth.voiceView (s);
                const auto& b = before[static_cast<std::size_t> (s)];
                if (v.active && v.held && (! b.active || ! b.held || b.note != v.note || b.cards != v.cards))
                {
                    result.onsets.push_back ({ t, v.note, v.cards });
                    if (v.cards == Synth::allCards)
                        ++result.allCardVoices;
                }
                if (v.active && v.held && v.cards == holdCards)
                    ++heldOn;
                before[static_cast<std::size_t> (s)] = v;
            }
            if (t > 0.3 && pos + block < releaseAt)
            {
                result.minHeldOn = std::min (result.minHeldOn, heldOn);
                result.maxHeldOn = std::max (result.maxHeldOn, heldOn);
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

    std::vector<int> firstNotes (const LanePlay& play, std::uint8_t cards, int count)
    {
        std::vector<int> notes;
        for (const auto& o : play.onsets)
            if (o.cards == cards && static_cast<int> (notes.size()) < count)
                notes.push_back (o.note);
        return notes;
    }
}

TEST_CASE ("analog: the arpeggiator on card C alone, while cards A and B hold the chord", "[analog][arp][cardarp]")
{
    ShellProcessor p;
    REQUIRE (p.cardArpeggiator());
    setValue (p, "arp.enabled", 1.0f);
    setValue (p, "analog.arp.cardA", 0.0f);
    setValue (p, "analog.arp.cardB", 0.0f);
    const auto play = playLanes (p, 4.0, 2.0, 3);   // (A and B: bits 0 and 1)
    INFO ("held on A+B: " << play.minHeldOn << " .. " << play.maxHeldOn);
    // The chord sustains on A and B, all the way through.
    CHECK (play.minHeldOn == 3);
    CHECK (play.maxHeldOn == 3);
    // C plays the arpeggio (UP, 1/8 at 120: about eight steps in two seconds), C E G in turn.
    const auto c = firstNotes (play, 4, 6);
    REQUIRE (c.size() >= 6);
    CHECK (c == std::vector<int> { 60, 64, 67, 60, 64, 67 });
    CHECK (play.allCardVoices == 0);
    // Released: nothing held, everything fades.
    CHECK (play.heldAtEnd == 0);
    CHECK (play.activeAtEnd == 0);
}

TEST_CASE ("analog: unlinked, each card plays its own pattern; LINK with every card is the one arpeggiator", "[analog][arp][cardarp]")
{
    {
        ShellProcessor p;
        setValue (p, "arp.enabled", 1.0f);
        setValue (p, "analog.arp.link", 0.0f);
        setValue (p, "arp.pattern", 0.0f);         // A: UP
        setValue (p, "analog.arp.cardB.pattern", 1.0f);   // B: DOWN
        setValue (p, "analog.arp.cardC", 0.0f);           // C holds
        const auto play = playLanes (p, 3.0, 2.0, 4);
        CHECK (firstNotes (play, 1, 4) == std::vector<int> { 60, 64, 67, 60 });
        CHECK (firstNotes (play, 2, 4) == std::vector<int> { 67, 64, 60, 67 });
        CHECK (play.minHeldOn == 3);   // the chord on C
        CHECK (play.allCardVoices == 0);
        CHECK (play.heldAtEnd == 0);
    }
    {
        // B on its own RATE: twice A's steps in the same time.
        ShellProcessor p;
        setValue (p, "arp.enabled", 1.0f);
        setValue (p, "analog.arp.link", 0.0f);
        setValue (p, "analog.arp.cardC", 0.0f);
        setValue (p, "analog.arp.cardB.rate", 2.0f);   // 1/16 against A's 1/8
        const auto play = playLanes (p, 2.2, 2.0, 4);
        int a = 0, b = 0;
        for (const auto& o : play.onsets)
        {
            a += o.cards == 1 ? 1 : 0;
            b += o.cards == 2 ? 1 : 0;
        }
        INFO ("A " << a << " B " << b);
        CHECK (b >= 2 * a - 2);
        CHECK (b <= 2 * a + 2);
    }
    {
        // LINK with every card (the default): the one arpeggiator, every card in each note.
        ShellProcessor p;
        setValue (p, "arp.enabled", 1.0f);
        const auto play = playLanes (p, 2.0, 1.5, 7);
        CHECK (firstNotes (play, 7, 4) == std::vector<int> { 60, 64, 67, 60 });
        CHECK (static_cast<int> (play.onsets.size()) == play.allCardVoices);
        CHECK (! dynamic_cast<AnalogEngine&> (p.productEngine()).synthForTest().cardRoutingOn());
    }
}

TEST_CASE ("analog: changing the arpeggiator's cards and LINK mid-chord leaves no note hanging", "[analog][arp][cardarp]")
{
    ShellProcessor p;
    setValue (p, "arp.enabled", 1.0f);
    setValue (p, "analog.arp.cardB.pattern", 1.0f);
    // A change every quarter second while the chord is held, then the keys come up.
    const std::vector<std::pair<const char*, float>> changes { { "analog.arp.cardC", 0.0f }, { "analog.arp.link", 0.0f }, { "analog.arp.cardA", 0.0f },
                                                                { "analog.arp.link", 1.0f }, { "analog.arp.cardB", 0.0f }, { "arp.enabled", 0.0f },
                                                                { "arp.enabled", 1.0f }, { "analog.arp.cardA", 1.0f }, { "analog.arp.cardB", 1.0f },
                                                                { "analog.arp.cardC", 1.0f }, { "analog.arp.link", 0.0f } };
    std::size_t next = 0;
    int heldAlong = 99;
    auto& synth = dynamic_cast<AnalogEngine&> (p.productEngine()).synthForTest();
    const auto play = playLanes (p, 6.0, 3.2, 0, [&] (double t) {
        if (next < changes.size() && t >= 0.25 * static_cast<double> (next + 1))
        {
            setValue (p, changes[next].first, changes[next].second);
            ++next;
        }
        if (t > 0.2 && t < 3.1)
        {
            // Something always sounds the held chord's notes (on whichever cards).
            int held = 0;
            for (int s = 0; s < Synth::maxVoices; ++s)
                held += synth.voiceView (s).held ? 1 : 0;
            if (t > 0.2 + 0.25 * static_cast<double> (next) + 0.05)   // (a step past the last change)
                heldAlong = std::min (heldAlong, held);
        }
    });
    CHECK (next == changes.size());
    CHECK (heldAlong >= 1);
    CHECK (play.heldAtEnd == 0);
    CHECK (play.activeAtEnd == 0);
}

//==============================================================================
// The effects rack (ANDOR OSP's shared rack host, opt-in): MOVEMENT expanded beside its macro.

namespace
{
    std::map<juce::String, float> allParameterValues (ShellProcessor& p)
    {
        std::map<juce::String, float> values;
        for (auto* param : p.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                values[ranged->getParameterID()] = ranged->getValue();
        return values;
    }

    int macroIndexOf (ShellProcessor& p, const juce::String& id)
    {
        for (int i = 0; i < osp::plugin::frame::macroCount; ++i)
            if (p.macroSpecs()[static_cast<std::size_t> (i)].parameterId == id)
                return i;
        return -1;
    }
}

TEST_CASE ("analog: MOVEMENT's key expands its rack under the macros, as a view: nothing sounds different", "[analog][rack]")
{
    using osp::plugin::rack::Kind;
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
    ui.refreshNow();
    // The cards keep nothing under their knobs (their modifiers in the well): the instrument
    // is shorter by their trim.
    CHECK (ui.instrumentHeight() == Approx (1086.0f - osp::plugin::EngineCard::footTrim));

    // MOVEMENT, ECHO and SPACE open racks (from their names: no key on the row).
    REQUIRE (ui.hasRack (Kind::movement));
    CHECK (ui.hasRack (Kind::echo));
    CHECK (ui.hasRack (Kind::space));
    CHECK_FALSE (ui.effectsRack().isVisible());
    const auto before = allParameterValues (p);
    const int heightBefore = editor->getHeight();
    const float instrumentBefore = ui.instrumentHeight();

    // A double-click on the name: the rack opens between the macros and the arpeggiator.
    ui.toggleRackFromLabel (Kind::movement);
    ui.refreshNow();
    REQUIRE (ui.effectsRack().isShown (Kind::movement));
    CHECK (ui.macroLabel (ui.rackMacro (Kind::movement))->isRackOpen());
    CHECK (ui.effectsRack().isVisible());
    const float rackHeight = ui.effectsRack().height();
    CHECK (rackHeight > 200.0f);
    CHECK (ui.instrumentHeight() == Approx (instrumentBefore + rackHeight + osp::plugin::design::layout::rackGap));
    CHECK (editor->getHeight() > heightBefore);
    CHECK (editor->getWidth() == 1448);
    CHECK (ui.effectsRack().getY() >= 900 - static_cast<int> (osp::plugin::EngineCard::footTrim));
    CHECK (ui.effectsRack().getHeight() <= 216);   // never taller than the macro panel
    CHECK (p.rackExpanded() == 1);

    // MOVEMENT's controls in the open, its knobs bound to the same parameters as the popover.
    auto* module = ui.effectsRack().module (Kind::movement);
    REQUIRE (module != nullptr);
    CHECK (osp::plugin::rackMovementCaptions (*module) == "INTENSITY SPEED PITCH TONE");
    setValue (p, "movement.mode", 4.0f);   // SHAPER (as automation would)
    ui.refreshNow();
    CHECK (osp::plugin::rackMovementCaptions (*module) == "PATTERN RATE TARGET DEPTH SMOOTH PHASE");
    setValue (p, "movement.mode", 1.0f);
    ui.refreshNow();
    CHECK (osp::plugin::rackMovementCaptions (*module) == "INTENSITY WOW FLUTTER WEAR");
    setValue (p, "movement.mode", 0.0f);
    ui.refreshNow();

    // The MOVEMENT name still opens its popover, with the rack open.
    const int movement = macroIndexOf (p, "motion");
    REQUIRE (movement >= 0);
    ui.openPopup (movement);
    CHECK (ui.openPopupPanel() != nullptr);
    CHECK (ui.effectsRack().isShown (Kind::movement));

    // The arpeggiator's editor opens under the rack (both shown, the keyboard under both).
    ui.setArpExpanded (true);
    ui.refreshNow();
    CHECK (ui.instrumentHeight() == Approx (instrumentBefore + rackHeight + osp::plugin::design::layout::rackGap + osp::plugin::design::layout::arpShift));
    ui.setArpExpanded (false);

    // Collapsing restores the window; no parameter moved at any point.
    ui.toggleRackFromLabel (Kind::movement);
    ui.refreshNow();
    CHECK_FALSE (ui.effectsRack().isShown (Kind::movement));
    CHECK_FALSE (ui.effectsRack().isVisible());
    CHECK (ui.instrumentHeight() == Approx (instrumentBefore));
    CHECK (editor->getHeight() == heightBefore);
    CHECK (allParameterValues (p) == before);
}

TEST_CASE ("analog: the rack's state is a view setting the session recalls", "[analog][rack]")
{
    using osp::plugin::rack::Kind;
    juce::MemoryBlock session;
    {
        ShellProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
        auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
        ui.setRackExpanded (Kind::movement, true);
        p.getStateInformation (session);
    }
    ShellProcessor q;
    q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
    CHECK (q.rackExpanded() == 1);
    std::unique_ptr<juce::AudioProcessorEditor> editor (q.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    CHECK (ui.effectsRack().isShown (Kind::movement));
    CHECK (ui.macroLabel (ui.rackMacro (Kind::movement))->isRackOpen());
    // A session recalled into an open editor closes it again.
    juce::MemoryBlock closed;
    {
        ShellProcessor r;
        r.getStateInformation (closed);
    }
    q.setStateInformation (closed.getData(), static_cast<int> (closed.getSize()));
    ui.refreshNow();
    CHECK_FALSE (ui.effectsRack().isShown (Kind::movement));
}

TEST_CASE ("analog: screenshots of the MOVEMENT rack at every width, ARP open and closed (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-rack]")
{
    using osp::plugin::rack::Kind;
    const auto* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    auto shoot = [&] (const juce::String& name) {
        ui.refreshNow();
        ui.effectsRack().settle();
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = out.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    setValue (p, "motion", 60.0f);
    for (const int width : { 1448, 869, 1810 })
    {
        const auto w = juce::String (width);
        for (const bool arp : { false, true })
        {
            const auto a = juce::String (arp ? "-arp" : "");
            ui.setRackExpanded (Kind::movement, false);
            ui.setArpExpanded (arp);
            editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
            shoot ("rack-" + w + a + "-0-collapsed");
            ui.setRackExpanded (Kind::movement, true);
            editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
            setValue (p, "movement.mode", 0.0f);
            shoot ("rack-" + w + a + "-1-drift");
            if (width != 1448)
                continue;
            setValue (p, "movement.mode", 1.0f);
            shoot ("rack-" + w + a + "-2-tape");
            setValue (p, "movement.mode", 2.0f);
            shoot ("rack-" + w + a + "-3-chorus");
            setValue (p, "movement.mode", 3.0f);
            shoot ("rack-" + w + a + "-4-pulse");
            setValue (p, "movement.mode", 4.0f);
            shoot ("rack-" + w + a + "-5-shaper");
            osp::plugin::setRackMovementWide (*ui.effectsRack().module (Kind::movement), true);
            shoot ("rack-" + w + a + "-6-shaper-wide");
            osp::plugin::setRackMovementWide (*ui.effectsRack().module (Kind::movement), false);
            ui.openPopup (macroIndexOf (p, "motion"));
            shoot ("rack-" + w + a + "-7-shaper-popover");
            ui.closePopup();
        }
    }
}

//==============================================================================
// MOVEMENT's two lanes (state version 11) in the product: parameters, presets, copy / swap,
// the second lane's SHAPER pattern with the session, and sessions from before lanes.

TEST_CASE ("analog: MOVEMENT lane B is off and lane A on at 100 % in a new instrument and in an older session", "[analog][rack][lanes]")
{
    using namespace params;
    ShellProcessor p;
    CHECK (p.movementLaneCount() == 2);
    CHECK (p.parameterValue (movementLaneId (0, "on")) >= 0.5f);
    CHECK (p.parameterValue (movementLaneId (0, "level")) == Approx (100.0f));
    CHECK (p.parameterValue (movementLaneId (1, "on")) < 0.5f);
    // A session from version 10 (no lanes): the same, whatever the instrument had before.
    juce::MemoryBlock session;
    {
        ShellProcessor old;
        old.getStateInformation (session);
    }
    auto xml = juce::AudioProcessor::getXmlFromBinary (session.getData(), static_cast<int> (session.getSize()));
    REQUIRE (xml != nullptr);
    xml->setAttribute ("productVersion", 10);
    juce::MemoryBlock v10;
    juce::AudioProcessor::copyXmlToBinary (*xml, v10);
    ShellProcessor q;
    setValue (q, movementLaneId (1, "on"), 1.0f);
    setValue (q, movementLaneId (0, "level"), 30.0f);
    q.setStateInformation (v10.getData(), static_cast<int> (v10.getSize()));
    CHECK (q.parameterValue (movementLaneId (1, "on")) < 0.5f);
    CHECK (q.parameterValue (movementLaneId (0, "level")) == Approx (100.0f));
}

TEST_CASE ("analog: a MOVEMENT preset saves one lane and loads into one lane, touching nothing else", "[analog][rack][lanes][presets]")
{
    using namespace params;
    Scratch scratch;
    ShellProcessor p;
    // Lane A: a SHAPER with a CUSTOM pattern, synced, moved.
    setValue (p, "movement.mode", 4.0f);
    setValue (p, "movement.shaper.rate", 1.0f);
    setValue (p, "movement.shaper.custom", 1.0f);
    setValue (p, movementLaneId (0, "phase"), 25.0f);
    setValue (p, movementLaneId (0, "level"), 70.0f);
    auto pattern = osp::RhythmicShaper::patternSteps (2);
    pattern[3] = { 0.2f, 0.9f, osp::StepShape::up };
    p.setLaneShaperPattern (0, pattern, false);
    const auto file = scratch.dir.getChildFile ("Gated" + juce::String (".andormovement"));
    REQUIRE (osp::plugin::writeMovementPreset (p, 0, file));

    // Into lane B of another instrument: lane B becomes it (and plays); nothing else moves.
    ShellProcessor q;
    setValue (q, osc2Level, 33.0f);
    setValue (q, "space", 41.0f);
    const auto before = allParameterValues (q);
    REQUIRE (osp::plugin::readMovementPreset (q, 1, file));
    const auto after = allParameterValues (q);
    for (const auto& [id, value] : after)
        if (value != before.at (id))
        {
            INFO (id);
            CHECK (id.startsWith (movementBPrefix));
        }
    CHECK (q.parameterValue (movementBPrefix + "mode") == Approx (4.0f));
    CHECK (q.parameterValue (movementBPrefix + "shaper.rate") == Approx (1.0f));
    CHECK (q.parameterValue (movementLaneId (1, "phase")) == Approx (25.0f).margin (0.1f));
    CHECK (q.parameterValue (movementLaneId (1, "level")) == Approx (70.0f).margin (0.1f));
    CHECK (q.parameterValue (movementLaneId (1, "on")) >= 0.5f);
    CHECK (q.laneShaperPattern (1) == pattern);
    CHECK (q.laneShaperPattern (0) == osp::RhythmicShaper::patternSteps (osp::ShaperParams().pattern));   // lane A untouched
    // Not a MOVEMENT preset: nothing happens.
    const auto other = scratch.dir.getChildFile ("not.andormovement");
    other.replaceWithText ("{\"kind\":\"something else\"}");
    CHECK_FALSE (osp::plugin::readMovementPreset (q, 0, other));
}

TEST_CASE ("analog: MOVEMENT's lanes copy and swap (settings and SHAPER pattern), and lane B's pattern comes back with the session", "[analog][rack][lanes]")
{
    using namespace params;
    ShellProcessor p;
    setValue (p, "movement.mode", 2.0f);
    setValue (p, "movement.chorus.rate", 80.0f);
    setValue (p, movementBPrefix + "mode", 3.0f);
    setValue (p, movementLaneId (1, "on"), 1.0f);
    auto patternB = osp::RhythmicShaper::patternSteps (7);
    patternB[0] = { 0.1f, 0.4f, osp::StepShape::dip };
    p.setLaneShaperPattern (1, patternB, false);

    osp::plugin::swapMovementLanes (p);
    CHECK (p.parameterValue ("movement.mode") == Approx (3.0f));
    CHECK (p.parameterValue (movementBPrefix + "mode") == Approx (2.0f));
    CHECK (p.parameterValue (movementBPrefix + "chorus.rate") == Approx (80.0f).margin (0.1f));
    CHECK (p.laneShaperPattern (0) == patternB);
    osp::plugin::swapMovementLanes (p);
    CHECK (p.parameterValue ("movement.mode") == Approx (2.0f));

    osp::plugin::copyMovementLane (p, 0, 1);
    CHECK (p.parameterValue (movementBPrefix + "mode") == Approx (2.0f));
    CHECK (p.parameterValue (movementBPrefix + "chorus.rate") == Approx (80.0f).margin (0.1f));
    CHECK (p.laneShaperPattern (1) == p.laneShaperPattern (0));

    // Lane B's pattern with the session.
    p.setLaneShaperPattern (1, patternB, false);
    juce::MemoryBlock session;
    p.getStateInformation (session);
    ShellProcessor q;
    q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
    CHECK (q.laneShaperPattern (1) == patternB);
    CHECK (q.parameterValue (movementLaneId (1, "on")) >= 0.5f);
}

TEST_CASE ("analog: the MOVEMENT rack shows each lane, its ON and its INTENSITY", "[analog][rack][lanes]")
{
    using osp::plugin::rack::Kind;
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    ui.setRackExpanded (Kind::movement, true);
    auto* module = ui.effectsRack().module (Kind::movement);
    REQUIRE (module != nullptr);
    CHECK (osp::plugin::rackMovementLane (*module) == 0);
    osp::plugin::showRackMovementLane (*module, 1);
    ui.refreshNow();
    CHECK (osp::plugin::rackMovementLane (*module) == 1);
    CHECK (osp::plugin::rackMovementCaptions (*module) == "INTENSITY SPEED PITCH TONE");   // lane B's DRIFT
    setValue (p, params::movementBPrefix + "mode", 3.0f);
    setValue (p, params::movementLaneId (1, "sync"), 1.0f);
    ui.refreshNow();
    CHECK (osp::plugin::rackMovementCaptions (*module) == "INTENSITY TIMING RATE SHAPE STEREO PHASE");   // PULSE synced: CYCLE as a key
}

TEST_CASE ("analog: screenshots of MOVEMENT's two lanes in the rack (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-lanes]")
{
    using osp::plugin::rack::Kind;
    using namespace params;
    const auto* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    auto shoot = [&] (const juce::String& name) {
        ui.refreshNow();
        ui.effectsRack().settle();
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = out.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    setValue (p, "motion", 60.0f);
    setValue (p, "movement.mode", 2.0f);
    setValue (p, movementBPrefix + "mode", 3.0f);
    setValue (p, movementLaneId (1, "on"), 1.0f);
    setValue (p, movementLaneId (1, "sync"), 1.0f);
    setValue (p, movementLaneId (1, "level"), 70.0f);
    ui.setRackExpanded (Kind::movement, true);
    for (const int width : { 1448, 869, 1810 })
        for (const bool arp : { false, true })
        {
            ui.setArpExpanded (arp);
            editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
            auto* module = ui.effectsRack().module (Kind::movement);
            const auto name = "lanes-" + juce::String (width) + (arp ? "-arp" : "");
            osp::plugin::showRackMovementLane (*module, 0);
            shoot (name + "-1-lane-a-chorus");
            osp::plugin::showRackMovementLane (*module, 1);
            shoot (name + "-2-lane-b-pulse-sync");
            if (width == 1448 && ! arp)
            {
                setValue (p, movementBPrefix + "mode", 4.0f);
                shoot (name + "-3-lane-b-shaper");
                setValue (p, movementLaneId (1, "on"), 0.0f);
                shoot (name + "-4-lane-b-off");
                setValue (p, movementLaneId (1, "on"), 1.0f);
                setValue (p, movementBPrefix + "mode", 3.0f);
            }
        }
}

//==============================================================================
// Effects rack, phase 3: ECHO and SPACE on the same host (one module at a time).

TEST_CASE ("analog: ECHO and SPACE expand into the rack, one module at a time, every control bound", "[analog][rack][rack-fx]")
{
    using osp::plugin::rack::Kind;
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, 1086);
    const auto before = allParameterValues (p);
    const float plain = ui.instrumentHeight();

    // One at a time: ECHO's key closes MOVEMENT's module, SPACE's closes ECHO's.
    ui.toggleRackFromLabel (Kind::movement);
    ui.toggleRackFromLabel (Kind::echo);
    ui.refreshNow();
    CHECK (ui.effectsRack().shownKinds() == std::vector<Kind> { Kind::echo });
    CHECK_FALSE (ui.macroLabel (ui.rackMacro (Kind::movement))->isRackOpen());
    CHECK (ui.macroLabel (ui.rackMacro (Kind::echo))->isRackOpen());
    CHECK (p.rackExpanded() == 2);
    auto* echo = ui.effectsRack().module (Kind::echo);
    REQUIRE (echo != nullptr);
    CHECK (osp::plugin::rackMovementCaptions (*echo) == "TIME FEEDBACK TONE AGE TIMING STEREO");
    setValue (p, "echo.type", 2.0f);   // ASCEND (as automation would)
    ui.refreshNow();
    CHECK (osp::plugin::rackMovementCaptions (*echo) == "TIME FEEDBACK TONE CLOCK INTERVAL MOTION TIMING STEREO");
    setValue (p, "echo.type", 0.0f);

    ui.toggleRackFromLabel (Kind::space);
    ui.refreshNow();
    CHECK (ui.effectsRack().shownKinds() == std::vector<Kind> { Kind::space });
    auto* space = ui.effectsRack().module (Kind::space);
    REQUIRE (space != nullptr);
    CHECK (osp::plugin::rackMovementCaptions (*space) == "PRE-DELAY SIZE DECAY DAMP MOD WIDTH LOW CUT HIGH CUT");
    for (const auto& [engine, captions] : { std::pair { 1.0f, "PRE-DELAY SIZE DECAY DAMP GRAVITY RESONANCE MOD WIDTH FREEZE" },
                                            std::pair { 2.0f, "PRE-DELAY SIZE DECAY DAMP DIFFUSION MOD WIDTH ERA" },
                                            std::pair { 3.0f, "TIME DIFFUSION DECAY DAMP TIMING SIZE MOD WIDTH" } })
    {
        setValue (p, "space.engine", engine);
        ui.refreshNow();
        CHECK (osp::plugin::rackMovementCaptions (*space) == juce::String (captions));
    }
    setValue (p, "space.engine", 0.0f);
    ui.refreshNow();
    // SPACE's module is taller (two rows of knobs): the window follows.
    CHECK (ui.instrumentHeight() == Approx (plain + ui.effectsRack().height() + osp::plugin::design::layout::rackGap));

    // The SPACE and ECHO names still open their popovers.
    for (const char* id : { "space", "echo" })
    {
        ui.openPopup (macroIndexOf (p, id));
        CHECK (ui.openPopupPanel() != nullptr);
        ui.closePopup();
    }
    ui.toggleRackFromLabel (Kind::space);
    ui.refreshNow();
    CHECK (ui.effectsRack().shownKinds().empty());
    CHECK (ui.instrumentHeight() == Approx (plain));
    setValue (p, "echo.type", before.at ("echo.type") > 0.5f ? 1.0f : 0.0f);
    setValue (p, "space.engine", 0.0f);
    for (const auto& [id, value] : allParameterValues (p))
    {
        INFO (id);
        CHECK (value == Approx (before.at (id)).margin (1.0e-6));
    }
}

TEST_CASE ("analog: expanding or collapsing ECHO and SPACE never touches their tails (a view only)", "[analog][rack][rack-fx]")
{
    using osp::plugin::rack::Kind;
    auto renderWith = [] (bool toggleRack) {
        ShellProcessor p;
        setValue (p, "space", 60.0f);
        setValue (p, "echo", 60.0f);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
        auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buffer (2, 256);
        std::vector<float> out;
        for (int block = 0; block < 48000 * 3 / 256; ++block)
        {
            juce::MidiBuffer midi;
            if (block == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 57, static_cast<juce::uint8> (100)), 0);
            if (block == 40)
                midi.addEvent (juce::MidiMessage::noteOff (1, 57), 0);
            if (toggleRack && block >= 60 && block % 30 == 0)
            {
                // In the tails: SPACE, ECHO and MOVEMENT open and close in turn.
                const Kind kinds[] { Kind::space, Kind::echo, Kind::movement };
                const auto kind = kinds[(block / 30) % 3];
                ui.setRackExpanded (kind, ! ui.effectsRack().isShown (kind));
                ui.refreshNow();
            }
            buffer.clear();
            p.processBlock (buffer, midi);
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 256);
        }
        return out;
    };
    const auto plain = renderWith (false), toggled = renderWith (true);
    REQUIRE (plain.size() == toggled.size());
    CHECK (plain == toggled);
    // (And the tails ring: SPACE and ECHO well after the note.)
    double tail = 0.0;
    for (std::size_t i = 48000 * 2; i < plain.size(); ++i)
        tail = std::max (tail, static_cast<double> (std::abs (plain[i])));
    CHECK (tail > 1.0e-4);
}

TEST_CASE ("analog: screenshots of the ECHO and SPACE racks at every width, ARP open and closed (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-rack-fx]")
{
    using osp::plugin::rack::Kind;
    const auto* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    auto shoot = [&] (const juce::String& name) {
        ui.refreshNow();
        ui.effectsRack().settle();
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = out.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    setValue (p, "space", 40.0f);
    setValue (p, "echo", 50.0f);
    for (const int width : { 1448, 869, 1810 })
        for (const bool arp : { false, true })
        {
            const auto name = "rackfx-" + juce::String (width) + (arp ? "-arp" : "");
            ui.setArpExpanded (arp);
            ui.setRackExpanded (Kind::echo, true);
            editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
            setValue (p, "echo.type", 0.0f);
            shoot (name + "-1-echo-tape");
            ui.setRackExpanded (Kind::space, true);
            editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
            setValue (p, "space.engine", 0.0f);
            shoot (name + "-2-space-rooms");
            if (width != 1448 || arp)
                continue;
            ui.setRackExpanded (Kind::echo, true);
            setValue (p, "echo.type", 2.0f);
            setValue (p, "echo.sync", 1.0f);
            shoot (name + "-3-echo-ascend-sync");
            setValue (p, "echo.type", 1.0f);
            shoot (name + "-4-echo-bbd");
            ui.setRackExpanded (Kind::space, true);
            for (const auto& [engine, label] : { std::pair { 1.0f, "abyss" }, std::pair { 2.0f, "vintage" }, std::pair { 3.0f, "bloom" } })
            {
                setValue (p, "space.engine", engine);
                shoot (name + "-5-space-" + label);
            }
            setValue (p, "space.engine", 0.0f);
            ui.openPopup (macroIndexOf (p, "space"));
            shoot (name + "-6-space-popover-over-rack");
            ui.closePopup();
        }
}

//==============================================================================
// Phase 4: card C's LINK, REVERSE, LOOP and FOLLOW as a compact row of icon keys in its well.

TEST_CASE ("analog: card C's modifiers sit in its well, only those that apply, bound as before", "[analog][rack][wellmods]")
{
    using andor::analog::HybridType;
    Scratch scratch;
    ShellProcessor p;
    loadOrganic (p, writeRecording (scratch.dir, "vowel.wav", 57, 2.0));
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, 1086);
    setValue (p, params::hybridType, static_cast<float> (HybridType::sample));
    ui.refreshNow();
    auto& card = ui.engineCard (ShellProcessor::hybridLayer);
    const auto well = card.display().getBounds();
    const char* ids[] { "layerB.link", "layerB.reverse", "layerB.loop", "layerB.follow" };

    // A SAMPLE: all four, small, at the well's top-left (no keys under the knobs any more).
    int right = well.getX();
    for (int i = 0; i < 4; ++i)
    {
        auto* key = card.modifierKey (i);
        REQUIRE (key != nullptr);
        INFO (ids[i]);
        CHECK (key->isVisible());
        CHECK (key->isInWell());
        CHECK (well.contains (key->getBounds()));
        CHECK (key->getY() < well.getY() + 40);
        CHECK (key->getHeight() <= 26);
        CHECK (key->getX() >= right);
        right = key->getRight();
        CHECK (osp::plugin::binding::tagsOf (*key).contains (ids[i]));   // the same parameter
    }
    CHECK (right < well.getCentreX());   // (the source's type stays at the top-right)

    // A click and the keyboard toggle the same parameter as before, one gesture each.
    auto* reverse = card.modifierKey (1);
    const float was = p.parameterValue ("layerB.reverse");
    reverse->toggle();
    CHECK (p.parameterValue ("layerB.reverse") != was);
    CHECK (reverse->getWantsKeyboardFocus());
    CHECK (reverse->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));
    CHECK (p.parameterValue ("layerB.reverse") == was);

    // An INSTRUMENT PATCH's zones do not play backwards: no REVERSE.
    setValue (p, params::hybridType, static_cast<float> (HybridType::instrumentPatch));
    ui.refreshNow();
    CHECK (card.modifierKey (0)->isVisible());
    CHECK_FALSE (card.modifierKey (1)->isVisible());
    CHECK (card.modifierKey (2)->isVisible());
    CHECK (card.modifierKey (3)->isVisible());
    CHECK (card.modifierKey (2)->getX() < card.modifierKey (0)->getRight() + 40);   // (the row closes up)

    // OSC 3 and NOISE: none.
    for (const auto type : { HybridType::oscillator, HybridType::noise })
    {
        setValue (p, params::hybridType, static_cast<float> (type));
        ui.refreshNow();
        for (int i = 0; i < 4; ++i)
            CHECK_FALSE (card.modifierKey (i)->isVisible());
    }

    // The EQ takes the well: the keys give way, and come back.
    setValue (p, params::hybridType, static_cast<float> (HybridType::sample));
    ui.refreshNow();
    card.setEqOpen (true);
    CHECK_FALSE (card.modifierKey (0)->isVisible());
    card.setEqOpen (false);
    ui.refreshNow();
    CHECK (card.modifierKey (0)->isVisible());

    // Cards A and B (the oscillators) show none, as before.
    for (int i = 0; i < 4; ++i)
        if (auto* key = ui.engineCard (0).modifierKey (i))
            CHECK_FALSE (key->isVisible());
}

//==============================================================================
// The unified rack: MOVEMENT, ECHO and SPACE share SPACE's structure and the same preset rules.

TEST_CASE ("analog: MOVEMENT, ECHO and SPACE share one layout - MODE, PRESET, the controls row, the picture", "[analog][rack][unified]")
{
    using osp::plugin::rack::Kind;
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
    std::map<Kind, std::array<juce::Rectangle<int>, 4>> slots;
    std::map<Kind, float> heights;
    setValue (p, "echo.type", 1.0f);   // (BBD: the approved layout; TAPE's deck is V4's own, [v4])
    for (const auto kind : { Kind::movement, Kind::echo, Kind::space })
    {
        ui.setRackExpanded (kind, true);
        ui.refreshNow();
        auto* module = ui.effectsRack().module (kind);
        REQUIRE (module != nullptr);
        for (int s = 2; s <= 5; ++s)
            slots[kind][static_cast<std::size_t> (s - 2)] = osp::plugin::rackSlot (*module, s);
        heights[kind] = ui.effectsRack().height();
    }
    for (const auto kind : { Kind::echo, Kind::space })
    {
        INFO (osp::plugin::rack::name (kind));
        CHECK (slots[kind][0] == slots[Kind::movement][0]);                   // MODE: same place and size
        CHECK (slots[kind][1] == slots[Kind::movement][1]);                   // PRESET: beside it, fixed width
        CHECK (slots[kind][2].getPosition() == slots[Kind::movement][2].getPosition());   // the first control: one origin
        CHECK (slots[kind][3].getRight() == slots[Kind::movement][3].getRight());   // the picture: its right edge fixed,
        CHECK (slots[kind][3].getY() == slots[Kind::movement][3].getY());           // its height the same
        CHECK (slots[kind][3].getHeight() == slots[Kind::movement][3].getHeight());
        CHECK (heights[kind] == heights[Kind::movement]);
    }
    CHECK (heights[Kind::space] <= 216.0f);   // never taller than the macro panel
    CHECK (slots[Kind::space][0].getWidth() == 165);   // approved MODE 165 x 27
    CHECK (slots[Kind::space][0].getHeight() == 27);
    // V2 faceplate: PRESET (236 x 24, never stretched) in the enamel rail, above the body.
    CHECK (slots[Kind::space][1].getWidth() == 236);
    CHECK (slots[Kind::space][1].getHeight() == 24);
    CHECK (slots[Kind::space][1].getBottom() <= 32);

    // SHAPER's precision editor (its magnifier): the one module that may be taller.
    ui.setRackExpanded (Kind::movement, true);
    setValue (p, "movement.mode", 4.0f);
    ui.refreshNow();
    auto* movement = ui.effectsRack().module (Kind::movement);
    CHECK (osp::plugin::rackMovementCaptions (*movement) == "PATTERN RATE TARGET DEPTH SMOOTH PHASE");
    osp::plugin::setRackMovementWide (*movement, true);
    ui.refreshNow();
    CHECK (ui.effectsRack().height() > 216.0f);
    CHECK (osp::plugin::rackSlot (*movement, 5).getWidth() > 2 * slots[Kind::space][3].getWidth());
    osp::plugin::setRackMovementWide (*movement, false);
    ui.refreshNow();
    CHECK (ui.effectsRack().height() == Approx (heights[Kind::movement]));
}

TEST_CASE ("analog: the approved rack layout - each module's graph column fixed, the slot families, nothing moves while editing", "[analog][rack][unified][density]")
{
    using osp::plugin::rack::Kind;
    Scratch scratch;
    osp::plugin::setRackPresetFolderForTest (scratch.dir);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
    struct Case
    {
        Kind kind;
        const char* id;
        int mode, dense;
    };
    // MOVEMENT's five modes, ECHO TAPE / BBD (six controls: standard), ASCEND (eight: dense),
    // SPACE (eight or nine: dense).
    const Case cases[] { { Kind::movement, "movement.mode", 0, 0 }, { Kind::movement, "movement.mode", 1, 0 }, { Kind::movement, "movement.mode", 2, 0 },
                         { Kind::movement, "movement.mode", 3, 0 }, { Kind::movement, "movement.mode", 4, 0 }, { Kind::echo, "echo.type", 0, 0 },
                         { Kind::echo, "echo.type", 1, 0 },         { Kind::echo, "echo.type", 2, 1 },         { Kind::space, "space.engine", 0, 1 },
                         { Kind::space, "space.engine", 1, 1 },     { Kind::space, "space.engine", 2, 1 },     { Kind::space, "space.engine", 3, 1 } };
    std::map<Kind, juce::Rectangle<int>> pictureOf;
    std::optional<int> pictureRight;
    for (const auto& c : cases)
    {
        INFO (osp::plugin::rack::name (c.kind) << " mode " << c.mode);
        ui.setRackExpanded (c.kind, true);
        setValue (p, c.id, static_cast<float> (c.mode));
        ui.refreshNow();
        auto* module = ui.effectsRack().module (c.kind);
        REQUIRE (module != nullptr);
        CHECK (osp::plugin::rackDensity (*module) == c.dense);
        if (osp::plugin::rackTapeDeckShown (*module))
        {
            // V4's TAPE deck (ECHO TAPE, MOVEMENT TAPE; its own layout, [v4]): the first control
            // on the same origin, the dense slots, the deck between the knobs and the display.
            const auto cells = osp::plugin::rackCellBounds (*module);
            REQUIRE (! cells.empty());
            CHECK (cells.front().getX() == osp::plugin::rack::hardware::bleedMeasures.contentLeft());
            for (std::size_t i = 1; i < cells.size(); ++i)
                CHECK (cells[i].getX() - cells[i - 1].getRight() == 5);
            CHECK (cells.back().getRight() < osp::plugin::rackTapeDeckBounds (*module).getX());
            CHECK (osp::plugin::rackTapeDeckBounds (*module).getRight() < osp::plugin::rackSlot (*module, 5).getX());
            continue;
        }
        // The graph: the module's column, whatever its mode; its right edge every module's.
        const auto picture = osp::plugin::rackSlot (*module, 5);
        if (pictureOf.count (c.kind) == 0)
            pictureOf[c.kind] = picture;
        CHECK (picture == pictureOf[c.kind]);
        if (! pictureRight)
            pictureRight = picture.getRight();
        CHECK (picture.getRight() == *pictureRight);
        // (V3 full-bleed face: the graph fills the body to the face's 16 px bottom margin.)
        CHECK (picture.getHeight() == module->getHeight() - module->titleOffset() - 7 - 16);
        // The cells: left-aligned from one origin on the approved slot widths (after the
        // faceplate's stripes), all before the divider; knobs never shrink (59 px in every family).
        const auto cells = osp::plugin::rackCellBounds (*module);
        REQUIRE (! cells.empty());
        CHECK (cells.front().getX() == osp::plugin::rack::hardware::bleedMeasures.contentLeft());
        CHECK (cells.back().getRight() + 17 < picture.getX() - 18);
        for (std::size_t i = 1; i < cells.size(); ++i)
            CHECK (cells[i].getX() - cells[i - 1].getRight() == (c.dense ? 5 : 7));
        // Presets of the mode, values, automation: nothing moves.
        std::array<juce::Rectangle<int>, 5> before;
        for (int s = 2; s <= 6; ++s)
            before[static_cast<std::size_t> (s - 2)] = osp::plugin::rackSlot (*module, s);
        osp::plugin::rackPresetStep (*module, 1);
        osp::plugin::rackPresetStep (*module, 1);
        for (const auto& [id, v] : allParameterValues (p))
            if (id.startsWith (c.kind == Kind::movement ? "movement." : (c.kind == Kind::echo ? "echo." : "space.")) && ! id.endsWith (".mode")
                && ! id.endsWith (".type") && ! id.endsWith (".engine") && ! id.endsWith (".sync"))
                if (auto* param = p.parameters.getParameter (id))
                    param->setValueNotifyingHost (0.37f);
        ui.refreshNow();
        CHECK (juce::roundToInt (p.parameterValue (c.id)) == c.mode);
        for (int s = 2; s <= 6; ++s)
        {
            INFO ("slot " << s);
            CHECK (osp::plugin::rackSlot (*module, s) == before[static_cast<std::size_t> (s - 2)]);
        }
        CHECK (osp::plugin::rackCellBounds (*module) == osp::plugin::rackCellBounds (*module));
    }
    // The approved columns: MOVEMENT 57 %, ECHO 61 %, SPACE 68 % of the content: SPACE's
    // graph the narrowest, MOVEMENT's the widest.
    CHECK (pictureOf[Kind::movement].getX() < pictureOf[Kind::echo].getX());
    CHECK (pictureOf[Kind::echo].getX() < pictureOf[Kind::space].getX());

    // FREE <-> SYNC: one RATE / TIME knob in one place; only its domain changes.
    for (const auto& [kind, modeId, mode, syncId, caption] :
         { std::tuple { Kind::movement, "movement.mode", 3, params::movementLaneId (0, "sync"), "RATE" }, std::tuple { Kind::echo, "echo.type", 0, juce::String ("echo.sync"), "TIME" } })
    {
        INFO (osp::plugin::rack::name (kind));
        ui.setRackExpanded (kind, true);
        setValue (p, modeId, static_cast<float> (mode));
        setValue (p, syncId, 0.0f);
        ui.refreshNow();
        auto* module = ui.effectsRack().module (kind);
        const auto freeCells = osp::plugin::rackCellBounds (*module);
        auto* freeKnob = dynamic_cast<osp::plugin::MiniKnob*> (osp::plugin::rackCell (*module, caption));
        REQUIRE (freeKnob != nullptr);
        const auto freeBounds = freeKnob->getBoundsInParent();
        CHECK (freeKnob->slider.getInterval() < 1.0);   // continuous when free (the parameter's own fine step)
        setValue (p, syncId, 1.0f);
        ui.refreshNow();
        auto* syncKnob = dynamic_cast<osp::plugin::MiniKnob*> (osp::plugin::rackCell (*module, caption));
        REQUIRE (syncKnob != nullptr);
        CHECK (syncKnob->getBoundsInParent() == freeBounds);
        CHECK (osp::plugin::rackCellBounds (*module) == freeCells);
        CHECK (syncKnob->slider.getInterval() == 1.0);   // detents when synced (the engine's note values)
    }
    osp::plugin::setRackPresetFolderForTest ({});
}

TEST_CASE ("analog: the racks' switches are their parameters - TIMING, STEREO, FREEZE, TARGET, the lane's power", "[analog][rack][unified][switches]")
{
    using osp::plugin::rack::Kind;
    using osp::plugin::rackui::VerticalSwitch;
    using osp::plugin::rackui::SegmentedSwitch;
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
    // ECHO: TIMING (FREE up, SYNC down), STEREO (MONO / PING / WIDE), both following the host.
    // (BBD: vertical switches; TAPE's deck has them as segmented keys, [v4].)
    setValue (p, "echo.type", 1.0f);
    ui.setRackExpanded (Kind::echo, true);
    ui.refreshNow();
    auto* echo = ui.effectsRack().module (Kind::echo);
    auto* timing = dynamic_cast<VerticalSwitch*> (osp::plugin::rackCell (*echo, "TIMING"));
    auto* stereo = dynamic_cast<VerticalSwitch*> (osp::plugin::rackCell (*echo, "STEREO"));
    REQUIRE (timing != nullptr);
    REQUIRE (stereo != nullptr);
    CHECK (timing->positionCount() == 2);
    CHECK (stereo->positionCount() == 3);
    timing->choose (0);
    CHECK (p.parameterValue ("echo.sync") < 0.5f);
    stereo->choose (2);
    CHECK (juce::roundToInt (p.parameterValue ("echo.stereo")) == 2);   // Wide
    setValue (p, "echo.stereo", 0.0f);
    CHECK (stereo->positionIndex() == 0);   // automation moves the actuator
    // SPACE ABYSS: FREEZE a real ON / OFF.
    ui.setRackExpanded (Kind::space, true);
    setValue (p, "space.engine", 1.0f);
    ui.refreshNow();
    auto* freeze = dynamic_cast<VerticalSwitch*> (osp::plugin::rackCell (*ui.effectsRack().module (Kind::space), "FREEZE"));
    REQUIRE (freeze != nullptr);
    freeze->choose (0);
    CHECK (p.parameterValue ("space.abyss.freeze") >= 0.5f);
    freeze->choose (1);
    CHECK (p.parameterValue ("space.abyss.freeze") < 0.5f);
    // MOVEMENT SHAPER: TARGET three-way segmented, not a list.
    ui.setRackExpanded (Kind::movement, true);
    setValue (p, "movement.mode", 4.0f);
    ui.refreshNow();
    auto* movement = ui.effectsRack().module (Kind::movement);
    auto* target = dynamic_cast<SegmentedSwitch*> (osp::plugin::rackCell (*movement, "TARGET"));
    REQUIRE (target != nullptr);
    target->choose (0);
    CHECK (juce::roundToInt (p.parameterValue ("movement.shaper.target")) == 0);
    auto* rate = dynamic_cast<osp::plugin::MiniKnob*> (osp::plugin::rackCell (*movement, "RATE"));
    REQUIRE (rate != nullptr);   // a stepped rotary, not a list or a stepper
    CHECK (rate->slider.getInterval() == 1.0);
    CHECK (rate->slider.getMaximum() == 5.0);   // the engine's six divisions, none invented
}

TEST_CASE ("analog: rack presets - by mode, the arrows, NAME * once edited, CUSTOM, your own, nothing else touched", "[analog][rack][unified][rackpresets]")
{
    using osp::plugin::rack::Kind;
    using namespace params;
    Scratch scratch;
    osp::plugin::setRackPresetFolderForTest (scratch.dir);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);

    // ECHO: the instrument's own settings are a preset of TAPE's; the list is TAPE's.
    ui.setRackExpanded (Kind::echo, true);
    ui.refreshNow();
    auto* echo = ui.effectsRack().module (Kind::echo);
    REQUIRE (echo != nullptr);
    CHECK (osp::plugin::rackPresetText (*echo) == "Dotted Eighth");
    CHECK (osp::plugin::rackPresetNames (*echo).joinIntoString ("|") == "Dotted Eighth|Slapback|Worn Loop|Dub Wash");
    setValue (p, osp::plugin::InstrumentHost::layerParameterId (0, "level"), 70.0f);
    setValue (p, "space.decay", 4.2f);
    const auto before = allParameterValues (p);
    REQUIRE (osp::plugin::rackPresetChoose (*echo, "Worn Loop"));
    CHECK (osp::plugin::rackPresetText (*echo) == "Worn Loop");
    CHECK (p.parameterValue ("echo.feedback") == Approx (68.0f).margin (0.1f));
    for (const auto& [id, v] : allParameterValues (p))
        if (v != before.at (id))
        {
            INFO (id);
            CHECK (id.startsWith ("echo."));   // ECHO's own settings only
        }
    // Edited: NAME *; the arrows step through TAPE's presets only.
    setValue (p, "echo.feedback", 20.0f);
    ui.refreshNow();
    CHECK (osp::plugin::rackPresetText (*echo) == "Worn Loop *");
    osp::plugin::rackPresetStep (*echo, 1);
    CHECK (osp::plugin::rackPresetText (*echo) == "Dub Wash");
    osp::plugin::rackPresetStep (*echo, 1);
    CHECK (osp::plugin::rackPresetText (*echo) == "Dotted Eighth");
    CHECK (p.parameterValue ("echo.type") == Approx (0.0f));
    // Another mode: its own presets; settings no preset has are CUSTOM.
    setValue (p, "echo.type", 1.0f);
    setValue (p, "echo.feedback", 33.0f);
    ui.refreshNow();
    CHECK (osp::plugin::rackPresetNames (*echo).contains ("Bucket Eighths"));
    CHECK_FALSE (osp::plugin::rackPresetNames (*echo).contains ("Worn Loop"));
    CHECK (osp::plugin::rackPresetText (*echo) == "CUSTOM");
    // Your own: saved, listed in its mode, recalled.
    REQUIRE (osp::plugin::rackPresetSave (*echo, "My BBD"));
    CHECK (osp::plugin::rackPresetText (*echo) == "My BBD");
    CHECK (osp::plugin::rackPresetNames (*echo).contains ("My BBD"));
    REQUIRE (osp::plugin::rackPresetChoose (*echo, "Bucket Slap"));
    REQUIRE (osp::plugin::rackPresetChoose (*echo, "My BBD"));
    CHECK (p.parameterValue ("echo.feedback") == Approx (33.0f).margin (0.1f));

    // SPACE: its presets as before (spacepresets), by engine.
    ui.setRackExpanded (Kind::space, true);
    ui.refreshNow();
    auto* space = ui.effectsRack().module (Kind::space);
    REQUIRE (space != nullptr);
    CHECK (osp::plugin::rackPresetNames (*space).size() > 10);
    const auto first = osp::plugin::rackPresetNames (*space)[0];
    REQUIRE (osp::plugin::rackPresetChoose (*space, first));
    CHECK (osp::plugin::rackPresetText (*space) == first);
    setValue (p, "space.damping", 77.0f);
    ui.refreshNow();
    CHECK (osp::plugin::rackPresetText (*space) == first + " *");

    // MOVEMENT: each lane its own mode and preset; lane A's never lane B's.
    ui.setRackExpanded (Kind::movement, true);
    ui.refreshNow();
    auto* movement = ui.effectsRack().module (Kind::movement);
    REQUIRE (movement != nullptr);
    CHECK (osp::plugin::rackPresetText (*movement) == "Gentle Drift");
    setValue (p, "movement.mode", 2.0f);
    ui.refreshNow();
    CHECK (osp::plugin::rackPresetText (*movement) == "Ensemble");
    const auto laneB = allParameterValues (p);
    REQUIRE (osp::plugin::rackPresetChoose (*movement, "Slow Sweep"));
    CHECK (p.parameterValue ("movement.chorus.rate") == Approx (20.0f).margin (0.1f));
    for (const auto& [id, v] : allParameterValues (p))
        if (id.startsWith (movementBPrefix))
        {
            INFO (id);
            CHECK (v == laneB.at (id));
        }
    osp::plugin::showRackMovementLane (*movement, 1);
    ui.refreshNow();
    CHECK (osp::plugin::rackPresetText (*movement) == "Gentle Drift");   // lane B: its own
    REQUIRE (osp::plugin::rackPresetSave (*movement, "B wander"));
    osp::plugin::showRackMovementLane (*movement, 0);
    ui.refreshNow();
    CHECK (osp::plugin::rackPresetText (*movement) == "Slow Sweep");     // lane A: still its own
    // Each mode keeps its settings when the mode changes back.
    setValue (p, "movement.mode", 1.0f);
    setValue (p, "movement.mode", 2.0f);
    CHECK (p.parameterValue ("movement.chorus.rate") == Approx (20.0f).margin (0.1f));
    osp::plugin::setRackPresetFolderForTest ({});
}

TEST_CASE ("analog: every rack's first control sits at one X, in every mode; the grid never moves", "[analog][rack][unified]")
{
    using osp::plugin::rack::Kind;
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
    std::optional<juce::Rectangle<int>> reference;
    auto check = [&] (Kind kind, const char* id, int choices)
    {
        ui.setRackExpanded (kind, true);
        for (int m = 0; m < choices; ++m)
        {
            setValue (p, id, static_cast<float> (m));
            ui.refreshNow();
            auto* module = ui.effectsRack().module (kind);
            REQUIRE (module != nullptr);
            const auto cell = osp::plugin::rackSlot (*module, 4);
            INFO (osp::plugin::rack::name (kind) << " mode " << m);
            if (! reference)
                reference = cell;
            CHECK (cell.getX() == reference->getX());           // left-aligned on one grid
            CHECK (cell.getY() == reference->getY());
            // A knob's slot in its family (V4's TAPE deck: the dense slots, its knobs left of the reels).
            CHECK (cell.getWidth() == (osp::plugin::rackDensity (*module) == 1 || osp::plugin::rackTapeDeckShown (*module) ? 89 : 104));
        }
    };
    check (Kind::space, "space.engine", 5);
    check (Kind::movement, "movement.mode", 5);
    check (Kind::echo, "echo.type", 3);
}

TEST_CASE ("analog: a rack preset's name, and its *, follow the settings through collapse, automation and the session", "[analog][rack][unified][rackpresets]")
{
    using osp::plugin::rack::Kind;
    Scratch scratch;
    osp::plugin::setRackPresetFolderForTest (scratch.dir);
    juce::MemoryBlock session;
    {
        ShellProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
        auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
        ui.setRackExpanded (Kind::echo, true);
        ui.refreshNow();
        REQUIRE (osp::plugin::rackPresetChoose (*ui.effectsRack().module (Kind::echo), "Worn Loop"));
        // A change from the host (automation, undo) is a change: NAME *; back to the preset's value, NAME.
        const float feedback = p.parameterValue ("echo.feedback");
        setValue (p, "echo.feedback", 20.0f);
        ui.refreshNow();
        CHECK (osp::plugin::rackPresetText (*ui.effectsRack().module (Kind::echo)) == "Worn Loop *");
        setValue (p, "echo.feedback", feedback);
        ui.refreshNow();
        CHECK (osp::plugin::rackPresetText (*ui.effectsRack().module (Kind::echo)) == "Worn Loop");
        setValue (p, "echo.feedback", 20.0f);
        // Collapsed and opened again (another module between): still the edited preset.
        ui.setRackExpanded (Kind::space, true);
        ui.setRackExpanded (Kind::echo, true);
        ui.refreshNow();
        CHECK (osp::plugin::rackPresetText (*ui.effectsRack().module (Kind::echo)) == "Worn Loop *");
        p.getStateInformation (session);
    }
    // A new editor on the restored session: the same name and *.
    ShellProcessor q;
    q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
    std::unique_ptr<juce::AudioProcessorEditor> editor (q.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    ui.setRackExpanded (Kind::echo, true);
    ui.refreshNow();
    CHECK (osp::plugin::rackPresetText (*ui.effectsRack().module (Kind::echo)) == "Worn Loop *");
    osp::plugin::setRackPresetFolderForTest ({});
}

TEST_CASE ("analog: SHAPER's rack presets are every pattern the popover offers, each loading its pattern", "[analog][rack][unified][rackpresets]")
{
    using osp::plugin::rack::Kind;
    Scratch scratch;
    osp::plugin::setRackPresetFolderForTest (scratch.dir);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    ui.setRackExpanded (Kind::movement, true);
    setValue (p, "movement.mode", 4.0f);
    ui.refreshNow();
    auto* movement = ui.effectsRack().module (Kind::movement);
    REQUIRE (movement != nullptr);
    const auto names = osp::plugin::rackPresetNames (*movement);
    CHECK (names.size() == osp::RhythmicShaper::patternCount);
    CHECK (names[0] == "Three");   // the instrument's own first
    for (int i = 0; i < osp::RhythmicShaper::patternCount; ++i)
    {
        const auto upper = juce::String (osp::RhythmicShaper::patternName (i));
        const auto name = upper.toLowerCase().substring (0, 1).toUpperCase() + upper.toLowerCase().substring (1);
        INFO (name);
        REQUIRE (names.contains (name));
        REQUIRE (osp::plugin::rackPresetChoose (*movement, name));
        CHECK (p.parameterValue ("movement.shaper.pattern") == Approx (static_cast<float> (i)));
        ui.refreshNow();
        CHECK (osp::plugin::rackPresetText (*movement) == name);
    }
    osp::plugin::setRackPresetFolderForTest ({});
}

TEST_CASE ("analog: screenshots of MOVEMENT, ECHO and SPACE in the unified rack (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-unified]")
{
    using osp::plugin::rack::Kind;
    const auto* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    auto shoot = [&] (const juce::String& name) {
        ui.refreshNow();
        ui.effectsRack().settle();
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = out.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    setValue (p, "motion", 60.0f);
    setValue (p, "space", 40.0f);
    setValue (p, "echo", 50.0f);
    for (const int width : { 1448, 869, 1810 })
        for (const bool arp : { false, true })
        {
            const auto name = "unified-" + juce::String (width) + (arp ? "-arp" : "");
            ui.setArpExpanded (arp);
            for (const auto& [kind, label] : { std::pair { Kind::movement, "1-movement" }, std::pair { Kind::echo, "2-echo" }, std::pair { Kind::space, "3-space" } })
            {
                ui.setRackExpanded (kind, true);
                editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
                shoot (name + "-" + label);
            }
            if (width != 1448 || arp)
                continue;
            ui.setRackExpanded (Kind::movement, true);
            for (const auto& [mode, label] : { std::pair { 0.0f, "drift" }, std::pair { 1.0f, "tape" }, std::pair { 2.0f, "chorus" }, std::pair { 3.0f, "pulse" },
                                               std::pair { 4.0f, "shaper" } })
            {
                setValue (p, "movement.mode", mode);
                shoot (name + "-4-movement-" + label);
            }
            setValue (p, params::movementLaneId (0, "sync"), 1.0f);
            setValue (p, "movement.mode", 3.0f);
            shoot (name + "-5-movement-pulse-sync");
            setValue (p, "movement.mode", 4.0f);
            osp::plugin::setRackMovementWide (*ui.effectsRack().module (Kind::movement), true);
            editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
            shoot (name + "-6-shaper-precision-editor");
            osp::plugin::setRackMovementWide (*ui.effectsRack().module (Kind::movement), false);
            ui.setRackExpanded (Kind::echo, true);
            setValue (p, "echo.type", 2.0f);
            shoot (name + "-7-echo-ascend");
            ui.setRackExpanded (Kind::space, true);
            setValue (p, "space.engine", 1.0f);
            shoot (name + "-8-space-abyss");
            setValue (p, "space.engine", 0.0f);
        }
}

//==============================================================================
// No mark on the macro row: a click on an effect's name opens its popover, a double-click its
// rack (the popover never flashing first), a right-click offers both, Enter opens the rack.

namespace
{
    juce::MouseEvent labelEvent (juce::Component& c, juce::ModifierKeys mods, int clicks)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        const auto at = c.getLocalBounds().getCentre().toFloat();
        return juce::MouseEvent (source, at, mods, juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, at, now, clicks, false);
    }
    void clickLabel (osp::plugin::MacroLabel& l)
    {
        const auto e = labelEvent (l, juce::ModifierKeys::leftButtonModifier, 1);
        l.mouseDown (e);
        l.mouseUp (e);
    }
    void doubleClickLabel (osp::plugin::MacroLabel& l)
    {
        clickLabel (l);
        const auto second = labelEvent (l, juce::ModifierKeys::leftButtonModifier, 2);
        l.mouseDown (second);
        l.mouseDoubleClick (second);
        l.mouseUp (second);
    }
}

TEST_CASE ("analog: an effect's name - click for settings, double-click for its rack, right-click for both, Enter", "[analog][rack][rackclicks]")
{
    // The label itself: a click waits for a possible double-click, which cancels it.
    {
        osp::plugin::MacroLabel label ("ECHO", true);
        label.setBounds (0, 0, 150, 22);
        int clicks = 0, racks = 0, menus = 0;
        label.onClick = [&] { ++clicks; };
        label.setRackActions ([&] { ++racks; }, [&] { ++menus; });
        CHECK (label.getTooltip() == juce::String::fromUTF8 ("Click for settings \xc2\xb7 Double-click for rack"));
        CHECK (label.getWantsKeyboardFocus());
        CHECK_FALSE (label.getMouseClickGrabsKeyboardFocus());

        clickLabel (label);
        CHECK (clicks == 0);   // not yet: a second click may follow
        CHECK (label.isClickPending());
        label.flushPendingClick();   // (the double-click interval has passed)
        CHECK (clicks == 1);
        CHECK (racks == 0);

        doubleClickLabel (label);
        CHECK (racks == 1);
        CHECK_FALSE (label.isClickPending());
        label.flushPendingClick();
        CHECK (clicks == 1);   // the popover never opened on the way

        const auto right = labelEvent (label, juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier), 1);
        label.mouseDown (right);
        label.mouseUp (right);
        CHECK (menus == 1);
        CHECK_FALSE (label.isClickPending());
        CHECK (clicks == 1);

        CHECK (label.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
        CHECK (racks == 2);
        CHECK (label.keyPressed (juce::KeyPress (juce::KeyPress::F10Key, juce::ModifierKeys::shiftModifier, 0)));
        CHECK (menus == 2);
    }
    // A name without a rack: a click acts at once, as before (no latency added).
    {
        osp::plugin::MacroLabel label ("DRIVE", true);
        label.setBounds (0, 0, 150, 22);
        int clicks = 0;
        label.onClick = [&] { ++clicks; };
        clickLabel (label);
        CHECK (clicks == 1);
        CHECK_FALSE (label.isClickPending());
        CHECK_FALSE (label.getWantsKeyboardFocus());
    }

    // In the instrument.
    using osp::plugin::rack::Kind;
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
    auto& movement = *ui.macroLabel (ui.rackMacro (Kind::movement));
    auto& echo = *ui.macroLabel (ui.rackMacro (Kind::echo));
    CHECK (ui.rackMacro (Kind::movement) == macroIndexOf (p, "motion"));
    CHECK (ui.macroLabel (macroIndexOf (p, "drive"))->getTooltip().isEmpty() == ui.macroLabel (macroIndexOf (p, "drive"))->getTooltip().isEmpty());   // (DRIVE: no rack)

    // Click: the popover (after the interval), no rack.
    clickLabel (movement);
    CHECK (ui.openPopupPanel() == nullptr);
    movement.flushPendingClick();
    CHECK (ui.openPopupIndex() == macroIndexOf (p, "motion"));
    CHECK (ui.effectsRack().shownKinds().empty());

    // Double-click with the popover open: the popover closes, the rack opens.
    doubleClickLabel (movement);
    ui.refreshNow();
    CHECK (ui.openPopupPanel() == nullptr);
    CHECK (ui.effectsRack().shownKinds() == std::vector<Kind> { Kind::movement });
    CHECK (movement.isRackOpen());
    // Another name: straight to its rack.
    doubleClickLabel (echo);
    ui.refreshNow();
    CHECK (ui.effectsRack().shownKinds() == std::vector<Kind> { Kind::echo });
    CHECK_FALSE (movement.isRackOpen());
    CHECK (echo.isRackOpen());
    // The same name again: closed.
    doubleClickLabel (echo);
    ui.refreshNow();
    CHECK (ui.effectsRack().shownKinds().empty());
    CHECK_FALSE (echo.isRackOpen());

    // Enter (with the keyboard's focus on the name).
    CHECK (movement.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
    ui.refreshNow();
    CHECK (ui.effectsRack().isShown (Kind::movement));

    // The context menu: Quick Settings, Close Rack (Open Rack when closed); each acts.
    auto items = [] (const juce::PopupMenu& menu) {
        std::vector<std::pair<juce::String, std::function<void()>>> list;
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            list.push_back ({ it.getItem().text, it.getItem().action });
        return list;
    };
    auto menu = items (ui.rackLabelMenu (ui.rackMacro (Kind::movement), Kind::movement));
    REQUIRE (menu.size() == 2);
    CHECK (menu[0].first == "Quick Settings");
    CHECK (menu[1].first == "Close Rack");
    menu[0].second();
    CHECK (ui.openPopupIndex() == macroIndexOf (p, "motion"));
    CHECK (ui.effectsRack().isShown (Kind::movement));   // (both: the popover over the open rack)
    menu[1].second();
    ui.refreshNow();
    CHECK_FALSE (ui.effectsRack().isShown (Kind::movement));
    CHECK (ui.openPopupPanel() == nullptr);
    menu = items (ui.rackLabelMenu (ui.rackMacro (Kind::space), Kind::space));
    CHECK (menu[1].first == "Open Rack");
    menu[1].second();
    ui.refreshNow();
    CHECK (ui.effectsRack().isShown (Kind::space));

    // Recalled with the session: the rack and its name's underline.
    juce::MemoryBlock session;
    p.getStateInformation (session);
    ShellProcessor q;
    q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
    std::unique_ptr<juce::AudioProcessorEditor> second (q.createEditorIfNeeded());
    auto& ui2 = dynamic_cast<andor::shell::ShellEditor&> (*second);
    CHECK (ui2.effectsRack().isShown (Kind::space));
    CHECK (ui2.macroLabel (ui2.rackMacro (Kind::space))->isRackOpen());
}

TEST_CASE ("analog: screenshots of the macro row - every rack closed, each open (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-macrorow]")
{
    using osp::plugin::rack::Kind;
    const auto* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    auto shoot = [&] (const juce::String& name) {
        editor->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
        ui.refreshNow();
        ui.effectsRack().settle();
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = out.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    shoot ("macrorow-0-closed");
    for (const auto& [kind, label] : { std::pair { Kind::movement, "1-movement" }, std::pair { Kind::echo, "2-echo" }, std::pair { Kind::space, "3-space" } })
    {
        ui.toggleRackFromLabel (kind);
        shoot ("macrorow-" + juce::String (label));
        ui.toggleRackFromLabel (kind);
    }
}

TEST_CASE ("analog: MOVEMENT's header is the title, A / B and the shown lane's power; copy / swap from A / B's menu", "[analog][rack][lanes]")
{
    using osp::plugin::rack::Kind;
    Scratch scratch;
    osp::plugin::setRackPresetFolderForTest (scratch.dir);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
    ui.setRackExpanded (Kind::movement, true);
    ui.refreshNow();
    auto* module = ui.effectsRack().module (Kind::movement);
    REQUIRE (module != nullptr);
    const auto onA = params::movementLaneId (0, "on"), onB = params::movementLaneId (1, "on");

    // The title row: the lane selector and the shown lane's power (no LANES, no COPY / SWAP);
    // with the V2 faceplate the preset browser and its actions are in the rail too.
    // (and the save key beside the browser, left of its actions)
    CHECK (osp::plugin::rackMovementTitleRow (*module).joinIntoString ("|") == "Presets|Preset actions|Lane A|Lane B|Save preset|Lane power");

    // Choosing a lane shows it and never switches either lane on or off.
    const float a0 = p.parameterValue (onA), b0 = p.parameterValue (onB);
    osp::plugin::showRackMovementLane (*module, 1);
    ui.refreshNow();
    CHECK (osp::plugin::rackMovementLane (*module) == 1);
    CHECK (p.parameterValue (onA) == a0);
    CHECK (p.parameterValue (onB) == b0);
    osp::plugin::showRackMovementLane (*module, 0);
    ui.refreshNow();
    CHECK (p.parameterValue (onA) == a0);
    CHECK (p.parameterValue (onB) == b0);

    // The shown lane's power: on the title row (right of A / B, by the collapse key), in every
    // mode; nothing of it among the controls.
    for (int mode = 0; mode < 5; ++mode)
    {
        setValue (p, "movement.mode", static_cast<float> (mode));
        ui.refreshNow();
        const auto power = osp::plugin::rackMovementLaneSwitch (*module);
        INFO ("mode " << mode);
        REQUIRE (! power.isEmpty());
        CHECK (power.getBottom() <= module->titleOffset() + 2);
        CHECK (power.getX() > module->getWidth() / 2);                      // at the rail's right (over the picture's side)
        CHECK (power.getRight() <= module->getWidth() - 22 - 26);          // before the collapse key
    }
    // It switches the shown lane only.
    osp::plugin::showRackMovementLane (*module, 1);
    ui.refreshNow();
    osp::plugin::clickRackMovementLaneSwitch (*module);
    CHECK (p.parameterValue (onB) != b0);
    CHECK (p.parameterValue (onA) == a0);
    osp::plugin::clickRackMovementLaneSwitch (*module);
    CHECK (p.parameterValue (onB) == b0);

    // Copy and swap: from the A / B selector's menu, and the same in the presets' menu.
    const auto actions = osp::plugin::rackMovementLaneActions (*module);
    CHECK (actions.size() == 3);
    CHECK (actions[2] == "Swap lanes A and B");
    CHECK (osp::plugin::rackMovementPresetMenuExtras (*module) == actions);
    setValue (p, juce::String ("movement.mode"), 2.0f);
    setValue (p, params::movementBPrefix + "mode", 0.0f);
    osp::plugin::runRackMovementLaneAction (*module, 0);   // copy A > B
    CHECK (p.parameterValue (params::movementBPrefix + "mode") == Approx (2.0f));
    setValue (p, params::movementBPrefix + "mode", 3.0f);
    osp::plugin::runRackMovementLaneAction (*module, 2);   // swap
    CHECK (p.parameterValue (juce::String ("movement.mode")) == Approx (3.0f));
    CHECK (p.parameterValue (params::movementBPrefix + "mode") == Approx (2.0f));
    osp::plugin::setRackPresetFolderForTest ({});
}

TEST_CASE ("analog: screenshots of MOVEMENT's header - A / B, each lane on or off, next to ECHO and SPACE (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-header]")
{
    using osp::plugin::rack::Kind;
    const auto* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    int width = 1448;
    auto shoot = [&] (const juce::String& name) {
        editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
        ui.refreshNow();
        ui.effectsRack().settle();
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = out.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    setValue (p, "motion", 60.0f);
    ui.setRackExpanded (Kind::movement, true);
    auto* module = ui.effectsRack().module (Kind::movement);
    REQUIRE (module != nullptr);
    shoot ("header-a-shown-b-off");
    setValue (p, params::movementLaneId (1, "on"), 1.0f);
    setValue (p, params::movementBPrefix + "mode", 2.0f);
    osp::plugin::showRackMovementLane (*module, 1);
    shoot ("header-b-shown-both-on");
    setValue (p, params::movementLaneId (0, "on"), 0.0f);
    shoot ("header-b-shown-a-off");
    setValue (p, params::movementLaneId (0, "on"), 1.0f);
    setValue (p, params::movementLaneId (1, "on"), 0.0f);
    shoot ("header-b-shown-b-off");
    setValue (p, params::movementBPrefix + "mode", 4.0f);
    osp::plugin::setRackMovementWide (*module, true);
    shoot ("header-shaper-wide");
    osp::plugin::setRackMovementWide (*module, false);
    osp::plugin::showRackMovementLane (*module, 0);
    for (const int w : { 869, 1810 })
    {
        width = w;
        ui.setArpExpanded (true);
        shoot ("header-" + juce::String (w) + "-arp");
        ui.setArpExpanded (false);
        shoot ("header-" + juce::String (w));
    }
    width = 1448;
    ui.setRackExpanded (Kind::echo, true);
    shoot ("header-echo");
    ui.setRackExpanded (Kind::space, true);
    shoot ("header-space");
}

namespace
{
    juce::MouseEvent eventAt (juce::Component& c, juce::Point<float> at, int clicks = 1)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (source, at, juce::ModifierKeys::leftButtonModifier, juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, at, now,
                                 clicks, false);
    }
}

TEST_CASE ("analog: the six modulation tabs never move - selection, routes, an editable curve", "[analog][modulation][modtabs]")
{
    using namespace osp::plugin;
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
    auto& panel = ui.modulationPanel();
    REQUIRE (panel.hasStableTabs());
    panel.showTab (0);
    ui.refreshNow();
    std::array<juce::Rectangle<float>, modui::tabCount> reference;
    for (int t = 0; t < modui::tabCount; ++t)
        reference[static_cast<std::size_t> (t)] = panel.tabArea (t);
    for (int t = 1; t < modui::tabCount; ++t)
        CHECK (reference[static_cast<std::size_t> (t)].getX() >= reference[static_cast<std::size_t> (t - 1)].getRight());   // in order, apart
    int checks = 0;
    auto unchanged = [&] (const char* when) {
        for (int shown = 0; shown < modui::tabCount; ++shown)
        {
            panel.showTab (shown);
            ui.refreshNow();
            for (int t = 0; t < modui::tabCount; ++t)
            {
                INFO (when << ": tab " << shown << " shown, tab " << t);
                CHECK (panel.tabArea (t) == reference[static_cast<std::size_t> (t)]);
                ++checks;
            }
            CHECK (panel.expandArea().isEmpty());   // no key in the tab row
            if (! panel.routesArea().isEmpty())
                CHECK (panel.routesArea().getX() > reference.back().getRight());   // in its reserved place
        }
    };
    unchanged ("no routes");
    // Routes (the count's text changes width), editable curves (LFO 1 CUSTOM, ENV 1 ONE SHOT).
    p.addModulationRoute (osp::mod::Source::vel, osp::mod::Dest::movement, 40.0f);
    p.addModulationRoute (osp::mod::Source::note, osp::mod::Dest::space, 30.0f);
    p.addModulationRoute (osp::mod::Source::lfo1, osp::mod::Dest::drive, 30.0f);
    p.addModulationRoute (osp::mod::Source::lfo1, osp::mod::Dest::space, 30.0f);
    unchanged ("routes");
    setValue (p, InstrumentHost::modLfoId (0, "shape"), static_cast<float> (osp::mod::LfoShape::custom));
    setValue (p, InstrumentHost::modEnvId (0, "mode"), 1.0f);
    unchanged ("editable LFO 1 and ENV 1");
    CHECK (checks == 3 * modui::tabCount * modui::tabCount);
}

TEST_CASE ("analog: VEL's and NOTE's larger curve editor opens from the display, edits that curve, and the session keeps it", "[analog][modulation][modtabs]")
{
    using namespace osp::plugin;
    juce::MemoryBlock session;
    std::map<int, osp::mod::Curve> edited;
    {
        ShellProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
        auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
        editor->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
        auto& panel = ui.modulationPanel();
        for (const int source : { osp::mod::slot::vel, osp::mod::slot::note })
        {
            INFO ("source " << source);
            panel.showTab (modui::tabOfSource (source));
            ui.refreshNow();
            auto& view = panel.curveView();
            REQUIRE (view.source() == source);
            const auto key = view.expandArea();
            REQUIRE (! key.isEmpty());
            CHECK (view.getLocalBounds().toFloat().contains (key));   // inside the display
            // A click on it opens the larger editor on this source's curve.
            const auto press = eventAt (view, key.getCentre());
            view.mouseDown (press);
            view.mouseUp (press);
            REQUIRE (ui.openPopupIndex() == andor::shell::ShellEditor::curvePopup);
            auto* popup = dynamic_cast<ModCurvePopup*> (ui.openPopupPanel());
            REQUIRE (popup != nullptr);
            auto& big = popup->curveView();
            CHECK (big.source() == source);
            CHECK (big.isEditable());
            if (big.getWidth() < 100)
                big.setBounds (0, 0, 600, 280);
            // An edit there: a double-click adds a point to this curve (and only this one).
            const int index = modui::keyCurveIndex (source);
            const auto before = p.modulationCurve (index);
            const auto other = p.modulationCurve (index == 4 ? 5 : 4);
            const auto plot = big.plotArea();
            const auto at = juce::Point<float> (plot.getX() + 0.37f * plot.getWidth(), plot.getY() + 0.2f * plot.getHeight());
            big.mouseDoubleClick (eventAt (big, at, 2));
            CHECK (p.modulationCurve (index).count == before.count + 1);
            CHECK (p.modulationCurve (index == 4 ? 5 : 4) == other);
            edited[index] = p.modulationCurve (index);
            ui.closePopup();
        }
        p.getStateInformation (session);
    }
    ShellProcessor q;
    q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
    for (const auto& [index, curve] : edited)
    {
        INFO ("curve " << index);
        CHECK (q.modulationCurve (index) == curve);
    }
}

TEST_CASE ("analog: screenshots of the modulation panel's tabs, fixed and as before (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-modtabs]")
{
    using namespace osp::plugin;
    const auto* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    skin::setLook (skin::Look::ceramic);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
    p.addModulationRoute (osp::mod::Source::vel, osp::mod::Dest::movement, 40.0f);
    p.addModulationRoute (osp::mod::Source::lfo1, osp::mod::Dest::drive, 30.0f);
    p.addModulationRoute (osp::mod::Source::lfo1, osp::mod::Dest::space, 30.0f);
    setValue (p, InstrumentHost::modLfoId (0, "shape"), static_cast<float> (osp::mod::LfoShape::custom));
    auto& panel = ui.modulationPanel();
    for (const bool stable : { false, true })
    {
        panel.setStableTabs (stable);
        for (int t = 0; t < modui::tabCount; ++t)
        {
            panel.showTab (t);
            ui.refreshNow();
            const auto image = editor->createComponentSnapshot (panel.getBoundsInParent().getUnion (panel.getBounds()).expanded (8), true, 1.0f);
            const auto bounds = editor->getLocalArea (&panel, panel.getLocalBounds()).expanded (8);
            const auto shot = editor->createComponentSnapshot (bounds, true, 1.0f);
            juce::ignoreUnused (image);
            const auto file = out.getChildFile (juce::String (stable ? "modtabs-fixed-" : "modtabs-before-") + juce::String (t) + ".png");
            file.deleteFile();
            juce::FileOutputStream stream (file);
            juce::PNGImageFormat().writeImageToStream (shot, stream);
        }
    }
}

TEST_CASE ("analog: screenshots of the rack's compact, medium and dense layouts at every width (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-adaptive]")
{
    using osp::plugin::rack::Kind;
    const auto* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    setValue (p, "motion", 60.0f);
    setValue (p, "space", 40.0f);
    setValue (p, "echo", 50.0f);
    setValue (p, params::movementLaneId (0, "sync"), 1.0f);
    struct Shot
    {
        const char* name;
        Kind kind;
        const char* id;
        int mode;
    };
    const Shot shots[] { { "movement-drift-4", Kind::movement, "movement.mode", 0 }, { "movement-pulse-6", Kind::movement, "movement.mode", 3 },
                         { "movement-shaper-6", Kind::movement, "movement.mode", 4 }, { "echo-tape-6", Kind::echo, "echo.type", 0 },
                         { "echo-ascend-8", Kind::echo, "echo.type", 2 },         { "space-abyss-9", Kind::space, "space.engine", 1 },
                         { "space-hall-8", Kind::space, "space.engine", 0 } };
    for (const int width : { 1448, 869, 1810 })
        for (const auto& shot : shots)
        {
            ui.setRackExpanded (shot.kind, true);
            setValue (p, shot.id, static_cast<float> (shot.mode));
            editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
            ui.refreshNow();
            ui.effectsRack().settle();
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            const auto file = out.getChildFile ("adaptive-" + juce::String (width) + "-" + shot.name + ".png");
            file.deleteFile();
            juce::FileOutputStream stream (file);
            juce::PNGImageFormat().writeImageToStream (image, stream);
        }
}

TEST_CASE ("analog: screenshots of every rack module in every mode, before / after a polish (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-polish]")
{
    using osp::plugin::rack::Kind;
    const auto* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    int width = 1448;
    auto shoot = [&] (const juce::String& name) {
        editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
        ui.refreshNow();
        ui.effectsRack().settle();
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = out.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    setValue (p, "motion", 60.0f);
    setValue (p, "space", 40.0f);
    setValue (p, "echo", 50.0f);
    ui.setRackExpanded (Kind::movement, true);
    const char* modes[] { "drift", "tape", "chorus", "pulse", "shaper" };
    for (int m = 0; m < 5; ++m)
    {
        setValue (p, "movement.mode", static_cast<float> (m));
        if (m == 3)
            setValue (p, params::movementLaneId (0, "sync"), 1.0f);
        shoot (juce::String ("polish-movement-") + modes[m]);
    }
    ui.setRackExpanded (Kind::echo, true);
    const char* echoes[] { "tape", "bbd", "ascend" };
    for (int m = 0; m < 3; ++m)
    {
        setValue (p, "echo.type", static_cast<float> (m));
        shoot (juce::String ("polish-echo-") + echoes[m]);
    }
    setValue (p, "echo.type", 0.0f);
    ui.setRackExpanded (Kind::space, true);
    setValue (p, "space.engine", 0.0f);
    setValue (p, "space.type", 1.0f);
    shoot ("polish-space-hall");
    setValue (p, "space.type", 2.0f);
    shoot ("polish-space-plate");
    for (const auto& [engine, label] : { std::pair { 1.0f, "abyss" }, std::pair { 2.0f, "vintage" }, std::pair { 3.0f, "bloom" } })
    {
        setValue (p, "space.engine", engine);
        shoot (juce::String ("polish-space-") + label);
    }
    setValue (p, "space.engine", 0.0f);
    setValue (p, "space.type", 1.0f);
    for (const int w : { 869, 1810 })
        for (const bool arp : { false, true })
        {
            width = w;
            ui.setArpExpanded (arp);
            for (const auto& [kind, label] : { std::pair { Kind::movement, "movement" }, std::pair { Kind::echo, "echo" }, std::pair { Kind::space, "space" } })
            {
                ui.setRackExpanded (kind, true);
                shoot ("polish-" + juce::String (w) + (arp ? "-arp-" : "-") + label);
            }
        }
}

//==============================================================================
// FILTER and MORPH as inline racks (the approved rack's parts), opened from their macros' names.

TEST_CASE ("analog: FILTER and MORPH open inline from their macros' names - every page's control bound, nothing sounds different", "[analog][rack][filtermorph]")
{
    using osp::plugin::rack::Kind;
    using osp::plugin::rack::ParameterRack;
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
    CHECK (ui.rackMacro (Kind::morph) == 0);
    CHECK (ui.rackMacro (Kind::filter) == 2);
    const auto before = allParameterValues (p);
    ui.setRackExpanded (Kind::movement, true);
    ui.refreshNow();
    const float standardHeight = ui.instrumentHeight();

    // A double-click on FILTER's name opens its rack (no popover); one module at a time; the
    // standard rack's height on every page and sub-page.
    ui.toggleRackFromLabel (Kind::filter);
    ui.refreshNow();
    CHECK (ui.effectsRack().shownKinds() == std::vector<Kind> { Kind::filter });
    auto* filter = dynamic_cast<ParameterRack*> (ui.effectsRack().module (Kind::filter));
    REQUIRE (filter != nullptr);
    struct View
    {
        int page, sub;
        const char* captions;
    };
    const View views[] { { 0, 0, "RESO ENV KEYS DRIVE ATTACK DECAY SUSTAIN RELEASE VELO" },
                         { 1, 0, "AMP ENV LINK VOICES UNISON DETUNE SPREAD" },
                         { 1, 1, "AMOUNT CUTOFF PITCH 1 PITCH 2 DELAY RISE WHEEL" },
                         { 1, 2, "ANALOG PHASE BIAS TRIM CHORUS HISS" },
                         { 2, 0, "SYNC RING BLEND FILT ENV OSC 2 PITCH 1 WIDTH 1 FILTER NOISE" } };
    const auto graph = filter->graphBounds();
    for (const auto& v : views)
    {
        INFO ("page " << v.page << "." << v.sub);
        filter->showPage (v.page);
        filter->showSubPage (v.sub);
        ui.refreshNow();
        CHECK (filter->page() == v.page);
        CHECK (filter->subPage() == v.sub);
        CHECK (filter->captions() == v.captions);
        CHECK (filter->graphBounds() == graph);   // one graph rectangle on every page
        CHECK (ui.instrumentHeight() == standardHeight);
        for (const auto& b : filter->cellBounds())
            CHECK (b.getRight() < graph.getX() - 17);
    }
    // FILTER's selector row: only the settings the chosen model uses, after the page words.
    using andor::analog::params::filterModel;
    using andor::analog::params::otaCharacter;
    using andor::analog::params::otaHpf;
    filter->showPage (0);
    ui.refreshNow();
    auto shown = [&] {
        juce::StringArray names;
        for (const char* name : { "TYPE", "SLOPE", "VOICING", "JUNO", "HPF" })
            if (auto* c = filter->cellNamed (name))
            {
                names.add (name);
                CHECK (c->getX() > 160);   // (right of FILTER VOICE PAIR and its rule)
                CHECK (c->getBottom() < filter->cellBounds().front().getY());
            }
        return names.joinIntoString (" ");
    };
    const auto savedModel = p.parameterValue (filterModel), savedHpf = p.parameterValue (otaHpf), savedVoicing = p.parameterValue (otaCharacter);
    setValue (p, filterModel, 0.0f);
    setValue (p, otaHpf, 0.0f);
    ui.refreshNow();
    CHECK (shown() == "TYPE HPF");   // the ladder, no high-pass: nothing else acts
    setValue (p, otaHpf, 2.0f);
    ui.refreshNow();
    CHECK (shown() == "TYPE VOICING JUNO HPF");   // the high-pass's curve: the voicing's and the Juno's
    setValue (p, filterModel, 1.0f);
    ui.refreshNow();
    CHECK (shown() == "TYPE SLOPE VOICING JUNO HPF");
    setValue (p, otaCharacter, 1.0f);
    ui.refreshNow();
    CHECK (shown() == "TYPE SLOPE VOICING HPF");   // JUPITER: no Juno to choose
    setValue (p, filterModel, savedModel);
    setValue (p, otaHpf, savedHpf);
    setValue (p, otaCharacter, savedVoicing);
    ui.refreshNow();
    // POLY MOD's destinations are three independent switches: any of them at once. The noise's
    // colour sits over the NOISE knob, not in the navigation.
    filter->showPage (2);
    ui.refreshNow();
    auto* noise = filter->cellNamed ("NOISE");
    auto* colour = filter->cellNamed ("NOISE COLOR");
    REQUIRE (noise != nullptr);
    REQUIRE (colour != nullptr);
    CHECK (colour->getX() < noise->getBounds().getCentreX());   // (its caption and field over the knob's column)
    CHECK (colour->getRight() > noise->getBounds().getCentreX());
    CHECK (colour->getX() > filter->cellNamed ("FILTER")->getRight());
    CHECK (colour->getBottom() < noise->getY());
    auto* pitch = dynamic_cast<osp::plugin::rackui::VerticalSwitch*> (filter->cellNamed ("PITCH 1"));
    auto* width = dynamic_cast<osp::plugin::rackui::VerticalSwitch*> (filter->cellNamed ("WIDTH 1"));
    auto* cutoff = dynamic_cast<osp::plugin::rackui::VerticalSwitch*> (filter->cellNamed ("FILTER"));
    REQUIRE (pitch != nullptr);
    REQUIRE (width != nullptr);
    REQUIRE (cutoff != nullptr);
    pitch->choose (0);
    width->choose (0);
    cutoff->choose (0);
    CHECK (p.parameterValue (andor::analog::params::polyModToPitch1) >= 0.5f);
    CHECK (p.parameterValue (andor::analog::params::polyModToWidth1) >= 0.5f);
    CHECK (p.parameterValue (andor::analog::params::polyModToCutoff) >= 0.5f);
    pitch->choose (1);
    width->choose (1);
    cutoff->choose (1);
    // TYPE: the two-way segmented switch; the slope only while OTA.
    filter->showPage (0);
    ui.refreshNow();
    auto* type = dynamic_cast<osp::plugin::rackui::SegmentedSwitch*> (filter->cellNamed ("TYPE"));
    REQUIRE (type != nullptr);
    type->choose (1);
    ui.refreshNow();
    CHECK (juce::roundToInt (p.parameterValue (andor::analog::params::filterModel)) == 1);
    type = dynamic_cast<osp::plugin::rackui::SegmentedSwitch*> (filter->cellNamed ("TYPE"));   // (rebuilt with the type)
    REQUIRE (type != nullptr);
    type->choose (0);
    ui.refreshNow();

    // MORPH: its rack replaces FILTER's; ON in its title row; one row; the standard height.
    ui.toggleRackFromLabel (Kind::morph);
    ui.refreshNow();
    CHECK (ui.effectsRack().shownKinds() == std::vector<Kind> { Kind::morph });
    CHECK (ui.instrumentHeight() == standardHeight);
    auto* morph = dynamic_cast<ParameterRack*> (ui.effectsRack().module (Kind::morph));
    REQUIRE (morph != nullptr);
    CHECK (morph->captions() == "MORPH TEXTURE ATTACK FORMANTS DETAIL OUTPUT LEVEL MATCH ROOT");
    REQUIRE (morph->powerKey() != nullptr);   // (its ON: the faceplate's power key, MORPH's enable)
    CHECK (morph->hasFaceplate());
    CHECK (morph->cellNamed ("METHOD") != nullptr);
    CHECK (morph->cellNamed ("CARRIER") != nullptr);
    CHECK (morph->status().isEmpty());   // (card C silent: the graph's empty state says so, not the header)
    // The setup a rank below the five that shape the morph: quieter captions and values.
    for (const char* name : { "MORPH", "TEXTURE", "ATTACK", "FORMANTS", "DETAIL" })
    {
        auto* k = dynamic_cast<osp::plugin::MiniKnob*> (morph->cellNamed (name));
        REQUIRE (k != nullptr);
        CHECK_FALSE (k->isQuiet());
    }
    for (const char* name : { "OUTPUT", "LEVEL MATCH" })
    {
        auto* key = morph->cellNamed (name);
        REQUIRE (key != nullptr);
        CHECK (dynamic_cast<osp::plugin::rackui::VerticalSwitch*> (key) != nullptr);
        auto* cell = dynamic_cast<osp::plugin::rackui::Cell*> (key->getParentComponent());
        REQUIRE (cell != nullptr);
        CHECK (cell->isQuiet());
    }
    auto* root = dynamic_cast<osp::plugin::MiniKnob*> (morph->cellNamed ("ROOT"));
    REQUIRE (root != nullptr);
    CHECK (root->isQuiet());
    ui.toggleRackFromLabel (Kind::morph);
    ui.refreshNow();
    CHECK (ui.effectsRack().shownKinds().empty());
    for (const auto& [id, v] : allParameterValues (p))
    {
        INFO (id);
        CHECK (v == before.at (id));
    }
}

namespace
{
    /** Where a rack cell's control sits: a knob's centre, a vertical switch's TRACK centre, a
        segmented or list key's centre (editor coordinates). */
    float controlCentreOf (juce::Component& editor, juce::Component& cellControl)
    {
        if (auto* k = dynamic_cast<osp::plugin::MiniKnob*> (&cellControl))
            return editor.getLocalArea (k, k->slider.getBounds()).toFloat().getCentreY();
        if (auto* v = dynamic_cast<osp::plugin::rackui::VerticalSwitch*> (&cellControl))
            return editor.getLocalArea (v, v->trackBounds()).getCentreY();
        return editor.getLocalArea (&cellControl, cellControl.getLocalBounds()).toFloat().getCentreY();
    }
}

TEST_CASE ("analog: every rack and page on the same lines - one height, one control centreline, the keyboard still", "[analog][rack][rackgeometry]")
{
    using osp::plugin::rack::Kind;
    using osp::plugin::rack::ParameterRack;
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    ui.setRackExpanded (Kind::movement, true);
    editor->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
    ui.refreshNow();
    const float height = ui.instrumentHeight();
    std::optional<float> centreline, rackTop, rackBottom;
    int checked = 0;
    auto check = [&] (const juce::String& what) {
        INFO (what);
        ui.refreshNow();
        CHECK (ui.instrumentHeight() == height);   // the keyboard never moves
        auto& rack = ui.effectsRack();
        const auto bounds = editor->getLocalArea (&rack, rack.getLocalBounds()).toFloat();
        if (! rackTop)
        {
            rackTop = bounds.getY();
            rackBottom = bounds.getBottom();
        }
        CHECK (bounds.getY() == *rackTop);
        CHECK (bounds.getBottom() == *rackBottom);
        auto* module = rack.module (rack.shownKinds().front());
        REQUIRE (module != nullptr);
        std::vector<juce::Component*> controls;
        for (auto* child : module->getChildren())
            if (auto* k = dynamic_cast<osp::plugin::MiniKnob*> (child); k != nullptr && k->isVisible())
                controls.push_back (k);
            else if (auto* c = dynamic_cast<osp::plugin::rackui::Cell*> (child); c != nullptr && c->isVisible())
                controls.push_back (&c->key());
        REQUIRE (! controls.empty());
        for (auto* c : controls)
        {
            const float y = controlCentreOf (*editor, *c);
            if (! centreline)
                centreline = y;
            CHECK (y == Catch::Approx (*centreline).margin (0.51));   // one centreline, whatever the control
            ++checked;
        }
    };
    ui.setRackExpanded (Kind::movement, true);
    for (int m = 0; m < 5; ++m)
    {
        setValue (p, "movement.mode", static_cast<float> (m));
        check ("MOVEMENT " + juce::String (m));
    }
    ui.setRackExpanded (Kind::echo, true);
    for (int m = 0; m < 3; ++m)
    {
        setValue (p, "echo.type", static_cast<float> (m));
        check ("ECHO " + juce::String (m));
    }
    ui.setRackExpanded (Kind::space, true);
    for (int m = 0; m < 4; ++m)
    {
        setValue (p, "space.engine", static_cast<float> (m));
        check ("SPACE " + juce::String (m));
    }
    ui.setRackExpanded (Kind::filter, true);
    auto* filter = dynamic_cast<ParameterRack*> (ui.effectsRack().module (Kind::filter));
    REQUIRE (filter != nullptr);
    for (const auto& [page, sub] : { std::pair { 0, 0 }, std::pair { 1, 0 }, std::pair { 1, 1 }, std::pair { 1, 2 }, std::pair { 2, 0 } })
    {
        filter->showPage (page);
        filter->showSubPage (sub);
        check ("FILTER " + juce::String (page) + "." + juce::String (sub));
    }
    ui.setRackExpanded (Kind::morph, true);
    check ("MORPH");
    CHECK (checked > 80);
}

TEST_CASE ("analog: screenshots of the racks with their guide lines (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-guides]")
{
    using osp::plugin::rack::Kind;
    using osp::plugin::rack::ParameterRack;
    const auto* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    setValue (p, andor::analog::params::resonance, 45.0f);
    int width = 1448;
    auto shoot = [&] (const juce::String& name, bool guides) {
        editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
        ui.refreshNow();
        ui.effectsRack().settle();
        auto& rack = ui.effectsRack();
        const auto r = editor->getLocalArea (&rack, rack.getLocalBounds()).toFloat();
        auto image = editor->createComponentSnapshot (r.getSmallestIntegerContainer().expanded (0, 6), true, 1.0f);
        if (guides)
        {
            // The rack's lines: top, header text, selector row, captions, control centres, values,
            // bottom (from the shared metrics, in this rack's coordinates).
            juce::Graphics g (image);
            auto* module = rack.module (rack.shownKinds().front());
            const float y0 = 6.0f, cells = y0 + static_cast<float> (module->titleOffset() + 39 + 8);
            const float lines[] { y0, y0 + 0.5f * static_cast<float> (module->titleOffset()) + 2.0f, y0 + static_cast<float> (module->titleOffset()) + 19.5f,
                                  cells + 0.5f * osp::plugin::rackui::captionHeight, cells + osp::plugin::rackui::controlCentre, cells + osp::plugin::rackui::valueTop + 9.0f,
                                  y0 + r.getHeight() - 1.0f };
            const juce::Colour colours[] { juce::Colours::red, juce::Colours::orange, juce::Colours::magenta, juce::Colours::blue, juce::Colours::green,
                                           juce::Colours::darkcyan, juce::Colours::red };
            for (int i = 0; i < 7; ++i)
            {
                g.setColour (colours[i].withAlpha (0.55f));
                g.fillRect (juce::Rectangle<float> (0.0f, lines[i], static_cast<float> (image.getWidth()), 1.0f));
            }
        }
        const auto file = out.getChildFile (name + (guides ? "-guides" : "") + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    auto both = [&] (const juce::String& name) {
        shoot (name, false);
        shoot (name, true);
    };
    ui.setRackExpanded (Kind::movement, true);
    setValue (p, "movement.mode", 4.0f);
    both ("rack-movement-shaper");
    ui.setRackExpanded (Kind::echo, true);
    setValue (p, "echo.type", 2.0f);
    both ("rack-echo-ascend");
    ui.setRackExpanded (Kind::space, true);
    setValue (p, "space.engine", 0.0f);
    both ("rack-space-hall");
    setValue (p, "space.engine", 1.0f);
    both ("rack-space-abyss");
    ui.setRackExpanded (Kind::filter, true);
    auto* filter = dynamic_cast<ParameterRack*> (ui.effectsRack().module (Kind::filter));
    REQUIRE (filter != nullptr);
    filter->showPage (0);
    both ("rack-filter-filter");
    // The OTA (its slope, voicing and Juno shown), and the ladder with the high-pass on (the
    // voicing and Juno shown for the high-pass's curve, no slope).
    setValue (p, andor::analog::params::filterModel, 1.0f);
    both ("rack-filter-filter-ota");
    setValue (p, andor::analog::params::filterModel, 0.0f);
    setValue (p, andor::analog::params::otaHpf, 3.0f);
    both ("rack-filter-filter-ladder-hpf");
    setValue (p, andor::analog::params::otaHpf, 0.0f);
    filter->showPage (1);
    const char* subs[] { "amp", "mod", "character" };
    for (int s = 0; s < 3; ++s)
    {
        filter->showSubPage (s);
        both (juce::String ("rack-filter-voice-") + subs[s]);
    }
    // MOD with LFO 1 at work: its depths, the wheel's share, the fade-in; CHARACTER's chorus on.
    filter->showSubPage (1);
    setValue (p, andor::analog::params::motion, 40.0f);
    setValue (p, andor::analog::params::lfoCutoff, 24.0f);
    setValue (p, andor::analog::params::lfoPitch1, -3.0f);
    setValue (p, andor::analog::params::lfoDelay, 0.4f);
    setValue (p, andor::analog::params::lfoRise, 1.2f);
    setValue (p, andor::analog::params::lfoWheel, 1.0f);
    both ("rack-filter-voice-mod-set");
    filter->showSubPage (2);
    setValue (p, andor::analog::params::chorusMode, 2.0f);
    both ("rack-filter-voice-character-chorus");
    filter->showPage (2);
    both ("rack-filter-pair");
    ui.setRackExpanded (Kind::morph, true);
    both ("rack-morph");
    setValue (p, andor::analog::params::hybridType, 1.0f);   // card C an oscillator: MORPH has a source
    both ("rack-morph-source");
    // The narrowest and widest: the whole window, ARP open.
    ui.setArpExpanded (true);
    for (const int w : { 869, 1810 })
    {
        width = w;
        for (const auto kind : { Kind::filter, Kind::morph, Kind::echo })
        {
            ui.setRackExpanded (kind, true);
            editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
            ui.refreshNow();
            ui.effectsRack().settle();
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            const auto file = out.getChildFile ("window-" + juce::String (w) + "-" + osp::plugin::rack::name (kind).toLowerCase() + ".png");
            file.deleteFile();
            juce::FileOutputStream stream (file);
            juce::PNGImageFormat().writeImageToStream (image, stream);
        }
    }
}

TEST_CASE ("analog: the review screens of FILTER and MORPH at 1448, 869 and 1810 px, with ECHO and SPACE (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-final]")
{
    using osp::plugin::rack::Kind;
    using osp::plugin::rack::ParameterRack;
    namespace ap = andor::analog::params;
    const auto* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    setValue (p, ap::resonance, 45.0f);
    struct View
    {
        const char* name;
        std::function<void()> set;
    };
    ParameterRack* filter = nullptr;
    auto openFilter = [&] (int page, int sub) {
        ui.setRackExpanded (Kind::filter, true);
        filter = dynamic_cast<ParameterRack*> (ui.effectsRack().module (Kind::filter));
        REQUIRE (filter != nullptr);
        filter->showPage (page);
        filter->showSubPage (sub);
    };
    const View views[] {
        { "echo-ascend", [&] { ui.setRackExpanded (Kind::echo, true); setValue (p, "echo.type", 2.0f); } },
        { "space-hall", [&] { ui.setRackExpanded (Kind::space, true); setValue (p, "space.engine", 0.0f); } },
        { "filter-ladder", [&] { setValue (p, ap::filterModel, 0.0f); openFilter (0, 0); } },
        { "filter-ota", [&] { setValue (p, ap::filterModel, 1.0f); openFilter (0, 0); } },
        { "voice-amp", [&] { setValue (p, ap::filterModel, 0.0f); openFilter (1, 0); } },
        { "voice-mod-zero", [&] {
             for (const auto& id : { ap::lfoCutoff, ap::lfoPitch1, ap::lfoPitch2, ap::lfoDelay, ap::lfoRise })
                 setValue (p, id, 0.0f);
             setValue (p, ap::lfoWheel, 0.0f);
             openFilter (1, 1);
         } },
        { "voice-mod-set", [&] {
             setValue (p, ap::motion, 40.0f);
             setValue (p, ap::lfoCutoff, 24.0f);
             setValue (p, ap::lfoPitch1, -3.0f);
             setValue (p, ap::lfoPitch2, 0.4f);
             setValue (p, ap::lfoDelay, 0.4f);
             setValue (p, ap::lfoRise, 1.2f);
             setValue (p, ap::lfoWheel, 1.0f);
             openFilter (1, 1);
         } },
        { "voice-character-off", [&] { setValue (p, ap::chorusMode, 0.0f); openFilter (1, 2); } },
        { "voice-character-chorus", [&] {
             setValue (p, ap::chorusMode, 2.0f);
             setValue (p, ap::chorusNoise, 1.0f);
             setValue (p, ap::filterDrive, 40.0f);
             setValue (p, ap::driveBias, 50.0f);
             openFilter (1, 2);
         } },
        { "pair", [&] { setValue (p, ap::polyModToPitch1, 1.0f); setValue (p, ap::polyModEnv, 30.0f); openFilter (2, 0); } },
        { "morph-no-source", [&] { setValue (p, ap::hybridType, 0.0f); ui.setRackExpanded (Kind::morph, true); } },
        { "morph-source", [&] { setValue (p, ap::hybridType, 1.0f); ui.setRackExpanded (Kind::morph, true); } },
    };
    for (const int width : { 1448, 869, 1810 })
        for (const auto& v : views)
        {
            v.set();
            editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
            ui.refreshNow();
            ui.effectsRack().settle();
            auto& rack = ui.effectsRack();
            const auto r = editor->getLocalArea (&rack, rack.getLocalBounds());
            const auto image = editor->createComponentSnapshot (r.expanded (0, 6).getIntersection (editor->getLocalBounds()), true, 1.0f);   // (real size)
            const auto file = out.getChildFile (juce::String (width) + "-" + v.name + ".png");
            file.deleteFile();
            juce::FileOutputStream stream (file);
            juce::PNGImageFormat().writeImageToStream (image, stream);
        }
}

TEST_CASE ("analog: FILTER's graph is the filter model's response - cutoff, resonance, type and high-pass move it", "[analog][rack][filtermorph]")
{
    using andor::analog::filtergraph::Settings;
    using andor::analog::filtergraph::responseDb;
    Settings s;
    s.cutoffHz = 1000.0;
    CHECK (responseDb (s, 20.0) == Approx (0.0).margin (0.1));               // pass band
    CHECK (responseDb (s, 1000.0) == Approx (-12.04).margin (0.1));          // four poles: -3 dB each at fc
    CHECK (responseDb (s, 8000.0) < responseDb (s, 4000.0) - 20.0);          // 24 dB/oct above
    s.resonance = 0.9;
    CHECK (responseDb (s, 1000.0) > responseDb (s, 20.0) + 10.0);            // the resonance's peak
    s.ota = true;
    s.twoPole = true;
    s.resonance = 0.0;
    CHECK (responseDb (s, 8000.0) > responseDb (Settings { 1000.0, 0.0 }, 8000.0) + 20.0);   // 12 dB slope
    s = Settings {};
    s.highPass = 3;   // JUNO-106 position 3: 720 Hz
    CHECK (responseDb (s, 50.0) < -20.0);
    s.highPass = 0;   // its bass boost
    CHECK (responseDb (s, 30.0) > 4.0);
}

TEST_CASE ("analog: CHARACTER's drive curve is the ladder's drive stage - DRIVE's gain, BIAS's DC removed", "[analog][rack][filtermorph]")
{
    using andor::analog::filtergraph::driveCurve;
    const auto plainCurve = driveCurve (0.0, 0.0);
    CHECK (plainCurve.gain == Approx (1.0));
    for (const double x : { -1.0, -0.3, 0.0, 0.5, 1.0 })
        CHECK (plainCurve.at (x) == Approx (std::tanh (x)));
    const auto driven = driveCurve (0.5, 0.0);
    CHECK (driven.gain == Approx (4.0));                       // 2^(4 x 0.5), as the synth sets it
    CHECK (driven.at (1.0) == Approx (std::tanh (4.0)));
    const auto biased = driveCurve (0.0, 1.0);
    CHECK (biased.bias == Approx (0.6));                       // 0.6 x BIAS
    CHECK (biased.at (0.0) == Approx (0.0).margin (1.0e-12));  // its DC removed
    CHECK (std::abs (biased.at (1.0)) < std::abs (biased.at (-1.0)));   // asymmetric: even harmonics
}

TEST_CASE ("analog: screenshots of the FILTER (FILTER, VOICE, PAIR) and MORPH racks (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-filtermorph]")
{
    using osp::plugin::rack::Kind;
    using osp::plugin::rack::ParameterRack;
    const auto* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    int width = 1448;
    auto shoot = [&] (const juce::String& name) {
        editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
        ui.refreshNow();
        ui.effectsRack().settle();
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = out.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    setValue (p, andor::analog::params::resonance, 45.0f);
    ui.setRackExpanded (Kind::filter, true);
    auto* filter = dynamic_cast<ParameterRack*> (ui.effectsRack().module (Kind::filter));
    REQUIRE (filter != nullptr);
    const char* pages[] { "filter", "voice", "pair" };
    for (int pg = 0; pg < 3; ++pg)
    {
        filter->showPage (pg);
        shoot (juce::String ("fm-1448-") + pages[pg]);
    }
    filter->showPage (0);
    setValue (p, andor::analog::params::filterModel, 1.0f);
    shoot ("fm-1448-filter-ota");
    setValue (p, andor::analog::params::filterModel, 0.0f);
    for (const int w : { 869, 1810 })
    {
        width = w;
        shoot ("fm-" + juce::String (w) + "-filter");
    }
    width = 1448;
    ui.setRackExpanded (Kind::morph, true);
    shoot ("fm-1448-morph");
    setValue (p, andor::analog::params::morphMethod, 2.0f);
    setValue (p, andor::analog::params::morphPosition, 60.0f);
    shoot ("fm-1448-morph-weave");
    for (const int w : { 869, 1810 })
    {
        width = w;
        ui.setArpExpanded (true);
        shoot ("fm-" + juce::String (w) + "-morph-arp");
        ui.setArpExpanded (false);
    }
}

namespace
{
    const juce::XmlElement* findElement (const juce::XmlElement& root, const juce::String& name)
    {
        if (root.hasTagName (name))
            return &root;
        for (const auto* c : root.getChildIterator())
            if (const auto* found = findElement (*c, name))
                return found;
        return nullptr;
    }
}

TEST_CASE ("analog: the FX route is saved with the session once CUSTOM; absent, old or unreadable, CLASSIC (state 12, now 13)", "[analog][fx][state]")
{
    namespace fx = andor::analog::fx;
    auto custom = fx::classicGraph();
    custom.custom = true;
    std::swap (custom.chain[0], custom.chain[1]);   // MOVEMENT before DRIVE
    juce::MemoryBlock classicSession, customSession;
    {
        ShellProcessor p;
        auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
        CHECK (engine.fxRoute() == fx::classicGraph());
        p.getStateInformation (classicSession);
        auto xml = juce::AudioProcessor::getXmlFromBinary (classicSession.getData(), static_cast<int> (classicSession.getSize()));
        REQUIRE (xml != nullptr);
        CHECK (xml->getIntAttribute ("productVersion") == 17);   // (13: RETRO's parameters appended; 14: card B's REIMAGINED; 15: SUB; 16: card C's second layer; 17: the cards' sends)
        CHECK (findElement (*xml, "FxRouting") == nullptr);   // (an untouched route writes nothing)

        // A graph this build cannot play is refused; the route stays.
        auto refused = fx::classicGraph();
        refused.custom = true;
        refused.chain.push_back (refused.chain[0]);   // DRIVE twice
        CHECK_FALSE (engine.setFxRoute (refused));
        CHECK (engine.fxRoute() == fx::classicGraph());

        int changed = 0;
        engine.onFxRouteChanged = [&changed] { ++changed; };
        REQUIRE (engine.setFxRoute (custom));
        CHECK (changed == 1);
        p.getStateInformation (customSession);
        engine.onFxRouteChanged = nullptr;
    }
    {
        ShellProcessor q;
        q.setStateInformation (customSession.getData(), static_cast<int> (customSession.getSize()));
        CHECK (dynamic_cast<AnalogEngine&> (q.productEngine()).fxRoute() == custom);
        // A session or preset without a route (every one from before) brings CLASSIC back.
        q.setStateInformation (classicSession.getData(), static_cast<int> (classicSession.getSize()));
        CHECK (dynamic_cast<AnalogEngine&> (q.productEngine()).fxRoute() == fx::classicGraph());
    }
    {
        // A state version 11 session (no route) and an unreadable route: CLASSIC.
        auto xml = juce::AudioProcessor::getXmlFromBinary (customSession.getData(), static_cast<int> (customSession.getSize()));
        REQUIRE (xml != nullptr);
        auto* routing = const_cast<juce::XmlElement*> (findElement (*xml, "FxRouting"));
        REQUIRE (routing != nullptr);
        routing->setAttribute ("graph", "v1 custom fx nonsense.4 on");
        juce::MemoryBlock broken;
        juce::AudioProcessor::copyXmlToBinary (*xml, broken);
        ShellProcessor r;
        r.setStateInformation (broken.getData(), static_cast<int> (broken.getSize()));
        CHECK (dynamic_cast<AnalogEngine&> (r.productEngine()).fxRoute() == fx::classicGraph());
    }
}

TEST_CASE ("analog: FX ROUTING - the icon beside SAVE on its centre line; the view takes only the cards' place; showing it changes nothing", "[analog][fx][routing][ui]")
{
    namespace fx = andor::analog::fx;
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    setValue (p, "echo", 40.0f);
    const auto before = renderShell (p, 0.6);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, juce::roundToInt (1448.0f * ui.instrumentHeight() / 1448.0f));
    ui.refreshNow();

    // The header without MIX: [ preset bar ][ SAVE ][ ROUTING ] as keys of one size on one
    // centre line, 8 px apart, 28 px clear of VOLUME; MIX hidden.
    auto& icon = ui.sourceViewToggle();
    REQUIRE (icon.isVisible());
    juce::Component* save = nullptr;
    juce::Component* mix = nullptr;
    juce::Component* bar = nullptr;
    for (auto* c : icon.getParentComponent()->getChildren())
    {
        if (dynamic_cast<osp::plugin::SavePresetButton*> (c) != nullptr)
            save = c;
        if (dynamic_cast<osp::plugin::HeaderMix*> (c) != nullptr)
            mix = c;
        if (dynamic_cast<osp::plugin::PresetBar*> (c) != nullptr)
            bar = c;
    }
    REQUIRE (save != nullptr);
    REQUIRE (bar != nullptr);
    CHECK ((mix == nullptr || ! mix->isVisible()));
    CHECK (icon.getBounds().getCentreY() == save->getBounds().getCentreY());
    CHECK (icon.getBounds().getCentreY() == bar->getBounds().getCentreY());
    CHECK (icon.getWidth() == save->getWidth());
    CHECK (icon.getHeight() == save->getHeight());
    CHECK (icon.getX() - save->getRight() == 8);
    CHECK (save->getX() - bar->getRight() == 8);

    // Opening it: the cards go, the view is in their place; macros and keyboard stay.
    std::vector<juce::Rectangle<int>> others;
    for (auto* c : icon.getParentComponent()->getChildren())
        if (dynamic_cast<osp::plugin::EngineCard*> (c) == nullptr && c->isVisible())
            others.push_back (c->getBounds());
    icon.onClick();   // (what a click does; triggerClick posts it)
    REQUIRE (ui.sourceViewShown());
    auto* view = dynamic_cast<andor::analog::FxRoutingView*> (ui.sourceViewComponent());
    REQUIRE (view != nullptr);
    CHECK (view->isVisible());
    for (auto* c : icon.getParentComponent()->getChildren())
        if (dynamic_cast<osp::plugin::EngineCard*> (c) != nullptr)
            CHECK_FALSE (c->isVisible());
    int unchanged = 0;
    for (auto* c : icon.getParentComponent()->getChildren())
        if (c != view && c->isVisible())
            for (const auto& b : others)
                unchanged += c->getBounds() == b ? 1 : 0;
    CHECK (unchanged >= static_cast<int> (others.size()) - 1);

    // CLASSIC drawn: the mixer at most a third; DRIVE, MOVEMENT, then ECHO over SPACE; one wire
    // from the mixer to the first tile and from the last to OUTPUT.
    CHECK (view->mixerBounds().getWidth() <= view->getWidth() / 3);
    REQUIRE (view->tiles().size() == 4);
    const auto& t = view->tiles();
    CHECK (t[0].id.type == fx::Type::drive);
    CHECK (t[1].id.type == fx::Type::movement);
    CHECK (t[2].id.type == fx::Type::echo);
    CHECK (t[3].id.type == fx::Type::space);
    CHECK (t[2].stacked);
    CHECK (t[2].bounds.getBottom() < t[3].bounds.getY());   // stacked: ECHO above SPACE
    CHECK (t[0].bounds.getCentreY() == view->outputBounds().getCentreY());
    CHECK (view->wireStart().getX() <= view->mixerBounds().getRight() + 6);
    CHECK (view->statusText().startsWith ("CLASSIC"));

    // Selecting, opening the library and closing the view never changed the sound.
    view->select (t[1].id);
    view->showLibrary (true);
    REQUIRE (view->libraryPopover() != nullptr);
    CHECK (view->libraryPopover()->isVisible());
    view->showLibrary (false);
    ui.setSourceViewShown (false);
    CHECK_FALSE (ui.sourceViewShown());
    CHECK (identical (before, renderShell (p, 0.6)));
    CHECK (dynamic_cast<AnalogEngine&> (p.productEngine()).fxRoute() == fx::classicGraph());
}

TEST_CASE ("analog: FX ROUTING - a drop in a gap goes in series, on a tile stacks; undo, Restore original, bypass, the library", "[analog][fx][routing][ui]")
{
    namespace fx = andor::analog::fx;
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    ui.setSourceViewShown (true);
    auto* view = dynamic_cast<andor::analog::FxRoutingView*> (ui.sourceViewComponent());
    REQUIRE (view != nullptr);
    auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
    const fx::InstanceId drive { fx::Type::drive, 0 }, echo { fx::Type::echo, 0 }, space { fx::Type::space, 0 };

    // ECHO into the first gap: series, first; CUSTOM.
    REQUIRE (view->moveTo (echo, {}, 0));
    CHECK (fx::toText (engine.fxRoute()) == "v1 custom fx echo.1 on fx drive.1 on fx movement.1 on fx space.1 on");
    CHECK (view->statusText().startsWith ("CUSTOM"));
    CHECK (view->tiles().front().id == echo);
    // SPACE onto DRIVE: stacked.
    REQUIRE (view->stackOn (space, drive));
    CHECK (fx::toText (engine.fxRoute()) == "v1 custom fx echo.1 on par linear 2 lane 0.5 { fx drive.1 on } lane 0.5 { fx space.1 on } fx movement.1 on");
    // Undo, step by step, back to CLASSIC.
    p.undo().undo();
    CHECK (fx::toText (engine.fxRoute()) == "v1 custom fx echo.1 on fx drive.1 on fx movement.1 on fx space.1 on");
    p.undo().undo();
    CHECK (engine.fxRoute() == fx::classicGraph());
    p.undo().redo();
    CHECK (engine.fxRoute().custom);
    // Bypass (the original four: the route's own), then Restore original: CLASSIC, settings kept.
    REQUIRE (view->toggleBypass (drive));
    CHECK (fx::toText (engine.fxRoute()).find ("fx drive.1 off") != std::string::npos);
    setValue (p, "drive", 33.0f);
    REQUIRE (view->restoreOriginal());
    CHECK (engine.fxRoute() == fx::classicGraph());
    CHECK (p.parameters.getRawParameterValue ("drive")->load() == Approx (33.0f));
    CHECK_FALSE (view->restoreOriginal());   // (CLASSIC already: nothing to restore)
    // The library: the original four IN CHAIN; one removed, it can come back.
    REQUIRE (view->remove (echo));
    CHECK_FALSE (fx::contains (engine.fxRoute(), echo));
    REQUIRE (view->insertNew (fx::Type::echo));
    CHECK (fx::contains (engine.fxRoute(), echo));
    CHECK_FALSE (view->insertNew (fx::Type::echo));   // (one ECHO)
}

TEST_CASE ("analog: screenshots of FX ROUTING (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-routing]")
{
    namespace fx = andor::analog::fx;
    const auto* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    setValue (p, "drive", 35.0f);
    setValue (p, "echo", 40.0f);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
    int width = 1448;
    auto shoot = [&] (const juce::String& name) {
        editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
        ui.refreshNow();
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = out.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    shoot ("routing-0-sound");
    ui.setSourceViewShown (true);
    auto* view = dynamic_cast<andor::analog::FxRoutingView*> (ui.sourceViewComponent());
    REQUIRE (view != nullptr);
    shoot ("routing-1-classic");
    view->moveTo ({ fx::Type::echo, 0 }, {}, 0);
    view->select (fx::InstanceId { fx::Type::drive, 0 });
    shoot ("routing-2-custom-echo-first");
    view->stackOn ({ fx::Type::space, 0 }, { fx::Type::movement, 0 });
    view->toggleBypass ({ fx::Type::drive, 0 });
    shoot ("routing-3-custom-parallel-bypass");
    view->showLibrary (true);
    shoot ("routing-4-library");
    view->showLibrary (false);
    for (const int w : { 869, 1810 })
    {
        width = w;
        shoot ("routing-" + juce::String (w) + "-custom");
    }
    width = 1448;
    engine.setFxRoute (fx::classicGraph());
    for (const int w : { 869, 1810 })
    {
        width = w;
        shoot ("routing-" + juce::String (w) + "-classic");
    }
}

TEST_CASE ("analog: the FX rack - every new effect opens in the one 214 px module, its quick settings expand to it; views change nothing", "[analog][fx][rack][ui]")
{
    namespace fx = andor::analog::fx;
    using osp::plugin::rack::Kind;
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, 1086);
    ui.setSourceViewShown (true);
    auto* view = dynamic_cast<andor::analog::FxRoutingView*> (ui.sourceViewComponent());
    REQUIRE (view != nullptr);
    view->setSize (1180, 330);
    for (int t = 4; t < fx::typeCount; ++t)
        REQUIRE (view->insertNew (static_cast<fx::Type> (t)));
    ui.refreshNow();
    const auto before = allParameterValues (p);

    std::optional<float> height;
    for (int t = 4; t < fx::typeCount; ++t)
    {
        const fx::InstanceId id { static_cast<fx::Type> (t), 0 };
        INFO (fx::typeName (id.type));
        view->openEditor (id);
        ui.refreshNow();
        CHECK (ui.effectsRack().shownKinds() == std::vector<Kind> { Kind::fx });
        CHECK (andor::analog::fxRackShown() == id);
        auto* module = dynamic_cast<osp::plugin::rack::ParameterRack*> (ui.effectsRack().module (Kind::fx));
        REQUIRE (module != nullptr);
        if (id.type == fx::Type::eq)
            CHECK (andor::analog::fxEqGraphic (*module) != nullptr);   // (the graphic is the module: no cells)
        else
            CHECK (module->captions().isNotEmpty());
        // The hardware faceplate: power key on the effect's ON, the preset browser and the
        // overflow key in the rail; every cell between the stripes and the graph's bezel.
        CHECK (module->hasFaceplate());
        REQUIRE (module->powerKey() != nullptr);
        CHECK (module->railComponent() != nullptr);
        CHECK (module->overflowKey() != nullptr);
        for (const auto& b : module->cellBounds())
        {
            CHECK (b.getX() >= module->contentLeft());
            CHECK (b.getRight() <= module->graphBounds().getX() - 4);
            CHECK (b.getBottom() <= module->chassisBounds().getBottom());
        }
        CHECK (module->chassisBounds().contains (module->graphBounds().expanded (4)));
        // Another effect swaps the module: the rack keeps its height.
        if (! height)
            height = ui.instrumentHeight();
        CHECK (ui.instrumentHeight() == Approx (*height));

        // A click's quick settings: anchored inside the view, the same parameters (2-4 knobs);
        // Return (EXPAND) closes them and shows the rack.
        ui.setRackExpanded (Kind::fx, false);
        view->showQuick (id);
        auto* quick = view->quickPopover();
        REQUIRE (quick != nullptr);
        CHECK (quick->isVisible());
        CHECK (view->getLocalBounds().contains (quick->getBounds()));
        CHECK (view->quickShown() == id);
        int knobs = 0;
        for (auto* c : quick->getChildren())
            knobs += dynamic_cast<osp::plugin::MiniKnob*> (c) != nullptr ? 1 : 0;
        CHECK (knobs >= 2);
        CHECK (knobs <= 4);
        CHECK (quick->keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
        CHECK (view->quickPopover() == nullptr);
        ui.refreshNow();
        CHECK (ui.effectsRack().shownKinds() == std::vector<Kind> { Kind::fx });
    }
    // Esc closes the quick settings first (the view stays).
    view->showQuick (fx::InstanceId { fx::Type::eq, 0 });
    REQUIRE (view->quickPopover() != nullptr);
    view->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    CHECK (view->quickPopover() == nullptr);
    CHECK (ui.sourceViewShown());
    // The original four open their own racks, never a copy; DRIVE's is the FX rack's DRIVE
    // faceplate on the instrument's own parameters (no ON of its own, no FX presets).
    view->openEditor ({ fx::Type::space, 0 });
    ui.refreshNow();
    CHECK (ui.effectsRack().shownKinds() == std::vector<Kind> { Kind::space });
    view->openEditor ({ fx::Type::drive, 0 });
    ui.refreshNow();
    CHECK (ui.effectsRack().shownKinds() == std::vector<Kind> { Kind::fx });
    if (auto* drive = dynamic_cast<osp::plugin::rack::ParameterRack*> (ui.effectsRack().module (Kind::fx)))
    {
        CHECK (drive->captions() == "DRIVE TONE BODY");
        CHECK (drive->cellNamed ("MODE") != nullptr);
        CHECK (drive->powerKey() == nullptr);
        CHECK (drive->railComponent() == nullptr);
    }
    else
        FAIL ("no DRIVE rack");
    // Opening, swapping and closing never moved a parameter.
    CHECK (allParameterValues (p) == before);

    // The power key is the effect's ON (a named gesture each way, as every rack switch).
    view->openEditor ({ fx::Type::compressor, 0 });
    ui.refreshNow();
    auto* comp = dynamic_cast<osp::plugin::rack::ParameterRack*> (ui.effectsRack().module (Kind::fx));
    REQUIRE (comp != nullptr);
    REQUIRE (comp->powerKey() != nullptr);
    CHECK (comp->powerKey()->isOn());
    comp->powerKey()->toggle();
    CHECK (p.parameters.getRawParameterValue ("analog.fx.comp.1.on")->load() < 0.5f);
    CHECK_FALSE (comp->powerKey()->isOn());
    comp->powerKey()->toggle();
    CHECK (p.parameters.getRawParameterValue ("analog.fx.comp.1.on")->load() > 0.5f);
    CHECK (comp->powerKey()->isOn());
}

TEST_CASE ("analog: RETRO - uninserted by default; its rack shows each personality's own controls on four pages, all inside the faceplate", "[analog][fx][rack][ui][retro]")
{
    namespace fx = andor::analog::fx;
    using osp::plugin::rack::Kind;
    ShellProcessor p;
    auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
    // An Init patch has no RETRO in its route (its parameters registered, at their defaults).
    for (const auto& n : engine.fxRoute().chain)
        CHECK (n.fx.type != fx::Type::retro);
    CHECK (p.parameters.getParameter ("analog.fx.retro.1.mode") != nullptr);
    CHECK (p.parameters.getParameter ("analog.fx.retro.2.seed") != nullptr);
    CHECK (p.parameters.getRawParameterValue ("analog.fx.retro.1.mix")->load() == Approx (65.0f));
    CHECK (p.parameters.getRawParameterValue ("analog.fx.retro.1.noise.level")->load() == Approx (-96.0f));   // (noise off)

    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, 1086);
    ui.setSourceViewShown (true);
    auto* view = dynamic_cast<andor::analog::FxRoutingView*> (ui.sourceViewComponent());
    REQUIRE (view != nullptr);
    view->setSize (1180, 330);
    REQUIRE (view->insertNew (fx::Type::retro));
    const fx::InstanceId id { fx::Type::retro, 0 };
    view->openEditor (id);
    ui.refreshNow();
    auto* module = dynamic_cast<osp::plugin::rack::ParameterRack*> (ui.effectsRack().module (Kind::fx));
    REQUIRE (module != nullptr);
    auto set = [&p, &ui] (const char* name, float v) {
        auto* q = p.parameters.getParameter (juce::String ("analog.fx.retro.1.") + name);
        REQUIRE (q != nullptr);
        q->setValueNotifyingHost (q->convertTo0to1 (v));
        ui.refreshNow();
    };
    struct Personality { int mode, era; const char* main; };
    const Personality personalities[] {
        { 0, 1, "CHARACTER WEAR WOW FLUTTER TONE NOISE MIX OUTPUT" },
        { 1, 1, "CHARACTER WEAR WOW CRACKLE POPS MIX OUTPUT" },
        { 2, 0, "CHARACTER WEAR BITS RATE ALIAS MIX OUTPUT" },
        { 2, 1, "CHARACTER WEAR BITS RATE ALIAS MIX OUTPUT" },
        { 2, 2, "CHARACTER WEAR LOSS PACKETS ARTIFACT MIX OUTPUT" },
    };
    for (const auto& pe : personalities)
    {
        INFO ("mode " << pe.mode << " era " << pe.era);
        set ("mode", static_cast<float> (pe.mode));
        set ("digital.era", static_cast<float> (pe.era));
        for (int page = 0; page < 4; ++page)
        {
            INFO ("page " << page);
            module->showPage (page);
            ui.refreshNow();
            CHECK (module->page() == page);
            CHECK (module->captions().isNotEmpty());
            if (page == 0)
                CHECK (module->captions() == pe.main);
            for (const auto& b : module->cellBounds())
            {
                CHECK (b.getX() >= module->contentLeft());
                CHECK (b.getRight() <= module->graphBounds().getX() - 4);
                CHECK (b.getBottom() <= module->chassisBounds().getBottom());
            }
        }
        module->showPage (0);
        ui.refreshNow();
    }
    // The quick settings: MODE, CHARACTER, WEAR, MIX.
    ui.setRackExpanded (Kind::fx, false);
    view->showQuick (id);
    REQUIRE (view->quickPopover() != nullptr);
    int knobs = 0;
    for (auto* c : view->quickPopover()->getChildren())
        knobs += dynamic_cast<osp::plugin::MiniKnob*> (c) != nullptr ? 1 : 0;
    CHECK (knobs == 3);
}

TEST_CASE ("analog: the host is told the route's latency - RETRO's frame once routed, nothing in CLASSIC", "[analog][fx][latency]")
{
    namespace fx = andor::analog::fx;
    ShellProcessor p;
    auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
    renderShell (p, 0.1);
    p.pollLoads();
    const int voices = p.getLatencySamples();   // (the oscillators' own, CLASSIC)
    auto route = fx::classicGraph();
    route.custom = true;
    route.chain.push_back (fx::Node::effect ({ fx::Type::retro, 0 }));
    REQUIRE (engine.setFxRoute (route));
    renderShell (p, 0.2);
    p.pollLoads();   // (the shell's timer: the message thread)
    CHECK (p.getLatencySamples() == voices + fx::Retro::frameFor (p.getSampleRate() > 0.0 ? p.getSampleRate() : 48000.0));
    REQUIRE (engine.setFxRoute (fx::classicGraph()));
    renderShell (p, 0.2);
    p.pollLoads();
    CHECK (p.getLatencySamples() == voices);
}

TEST_CASE ("analog: FX presets - every factory preset loads and reads as itself; a change marks it; a chain round-trips", "[analog][fx][presets]")
{
    namespace fx = andor::analog::fx;
    namespace fxpresets = andor::analog::fxpresets;
    ShellProcessor p;
    for (int t = 4; t < fx::typeCount; ++t)
    {
        const auto type = static_cast<fx::Type> (t);
        const fx::InstanceId id { type, 0 };
        INFO (fx::typeName (type));
        const auto& list = fxpresets::factory (type);
        CHECK (list.size() >= 3);
        for (const auto& preset : list)
        {
            INFO (preset.name);
            fxpresets::apply (p, id, preset);
            CHECK (fxpresets::matches (p, id, preset));
        }
        // Loading into slot 2 (where there is one) leaves slot 1 alone.
        if (fx::capacity[static_cast<std::size_t> (t)] > 1)
        {
            const auto one = fxpresets::capture (p, id, "one");
            fxpresets::apply (p, { type, 1 }, list.front());
            CHECK (fxpresets::matches (p, id, one));
        }
        // A capture is the settings: applying it elsewhere copies them.
        const auto here = fxpresets::capture (p, id, "here");
        fxpresets::apply (p, id, {});
        fxpresets::apply (p, id, here);
        CHECK (fxpresets::matches (p, id, here));
    }
    // ON is never a preset's.
    setValue (p, "analog.fx.eq.1.on", 0.0f);
    fxpresets::apply (p, { fx::Type::eq, 0 }, fxpresets::factory (fx::Type::eq).front());
    CHECK (p.parameters.getRawParameterValue ("analog.fx.eq.1.on")->load() < 0.5f);

    // A chain: the route and its new effects' settings, read back exactly.
    auto route = fx::classicGraph();
    route.custom = true;
    fx::insertEffect (route, {}, 0, fx::Node::effect ({ fx::Type::compressor, 0 }));
    fx::insertEffect (route, {}, 1, fx::Node::effect ({ fx::Type::eq, 0 }));
    fxpresets::apply (p, { fx::Type::compressor, 0 }, fxpresets::factory (fx::Type::compressor).back());
    setValue (p, "analog.fx.eq.1.band2.gain", 4.5f);
    const auto comp = fxpresets::capture (p, { fx::Type::compressor, 0 }, "c");
    const auto xml = fxpresets::chainToXml (p, route, "Test chain");
    REQUIRE (xml != nullptr);
    setValue (p, "analog.fx.eq.1.band2.gain", -3.0f);
    fxpresets::apply (p, { fx::Type::compressor, 0 }, {});
    fx::Graph read;
    REQUIRE (fxpresets::chainFromXml (p, *xml, read));
    CHECK (read == route);
    CHECK (p.parameters.getRawParameterValue ("analog.fx.eq.1.band2.gain")->load() == Approx (4.5f).margin (0.01));
    CHECK (fxpresets::matches (p, { fx::Type::compressor, 0 }, comp));
}

TEST_CASE ("analog: CONVOLUTION builds its impulse off the audio thread, follows IMPULSE and DECAY, and sounds in a CUSTOM route", "[analog][fx][conv]")
{
    namespace fx = andor::analog::fx;
    ShellProcessor p;
    auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
    const auto dry = renderShell (p, 0.6);
    auto ir = engine.convolutionImpulse (0);
    REQUIRE (ir != nullptr);
    const int chosen = juce::roundToInt (p.parameters.getRawParameterValue ("analog.fx.conv.1.impulse")->load());
    CHECK (ir->name == fx::Impulse::factoryName (chosen));
    CHECK (ir->lengthSamples > 0);

    setValue (p, "analog.fx.conv.1.impulse", static_cast<float> ((chosen + 1) % fx::Impulse::factoryCount));
    setValue (p, "analog.fx.conv.1.decay", 0.5f);
    engine.updateConvolutionImpulses();
    const auto next = engine.convolutionImpulse (0);
    REQUIRE (next != nullptr);
    CHECK (next->name == fx::Impulse::factoryName ((chosen + 1) % fx::Impulse::factoryCount));
    CHECK (next->lengthSamples <= static_cast<int> (0.5 * 48000.0) + 1);
    engine.updateConvolutionImpulses();
    CHECK (engine.convolutionImpulse (0) == next);   // (nothing changed: not rebuilt)

    // In a CUSTOM route after SPACE it is heard (and CLASSIC never hears it).
    auto route = fx::classicGraph();
    route.custom = true;
    fx::insertEffect (route, {}, static_cast<int> (route.chain.size()), fx::Node::effect ({ fx::Type::convolution, 0 }));
    setValue (p, "analog.fx.conv.1.mix", 60.0f);
    REQUIRE (engine.setFxRoute (route));
    const auto wet = renderShell (p, 0.6);
    CHECK (differenceRms (dry, wet) > 1.0e-4);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < wet.getNumSamples(); ++i)
            REQUIRE (std::isfinite (wet.getSample (ch, i)));
    REQUIRE (engine.setFxRoute (fx::classicGraph()));
    CHECK (identical (dry, renderShell (p, 0.6)));
}

TEST_CASE ("analog: screenshots of the FX racks and quick settings (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-fx-racks]")
{
    namespace fx = andor::analog::fx;
    const auto* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    renderShell (p, 0.3);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    int width = 1448;
    auto shoot = [&] (const juce::String& name) {
        editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
        ui.refreshNow();
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = out.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    ui.setSourceViewShown (true);
    auto* view = dynamic_cast<andor::analog::FxRoutingView*> (ui.sourceViewComponent());
    REQUIRE (view != nullptr);
    for (int t = 4; t < fx::typeCount; ++t)
        view->insertNew (static_cast<fx::Type> (t));
    // Play the phrase through the chain: the meters and live graphs show what was heard.
    renderShell (p, 0.5);
    for (int t = 4; t < fx::typeCount; ++t)
    {
        const fx::InstanceId id { static_cast<fx::Type> (t), 0 };
        const auto name = juce::String (fx::typeName (id.type)).toLowerCase().replaceCharacter (' ', '-');
        view->openEditor (id);
        shoot ("fx-rack-" + name);
        if (id.type == fx::Type::compressor || id.type == fx::Type::spread)
            for (const int w : { 869, 1810 })
            {
                width = w;
                shoot ("fx-rack-" + name + "-" + juce::String (w));
                width = 1448;
            }
        if (id.type == fx::Type::compressor || id.type == fx::Type::shifter || id.type == fx::Type::eq)
        {
            auto* rack = ui.effectsRack().module (osp::plugin::rack::Kind::fx);
            if (id.type == fx::Type::compressor)
                for (const int mode : { 1, 2 })
                {
                    setValue (p, "analog.fx.comp.1.mode", static_cast<float> (mode));
                    shoot ("fx-rack-" + name + "-mode" + juce::String (mode));
                }
            if (id.type == fx::Type::shifter)
            {
                setValue (p, "analog.fx.shift.1.mode", 1.0f);
                shoot ("fx-rack-" + name + "-blur");
            }
            juce::ignoreUnused (rack);
        }
        ui.setRackExpanded (osp::plugin::rack::Kind::fx, false);
        view->showQuick (id);
        shoot ("fx-quick-" + name);
        view->showQuick (std::nullopt);
    }
    view->openEditor ({ fx::Type::eq, 0 });
    for (const int w : { 869, 1810 })
    {
        width = w;
        shoot ("fx-rack-eq-" + juce::String (w));
    }
    width = 1448;
    // The instrument's own four on their faceplates (DRIVE's rack; MOVEMENT, ECHO, SPACE shared).
    setValue (p, "drive", 45.0f);
    setValue (p, "echo", 40.0f);
    setValue (p, "space", 30.0f);
    for (const auto t : { fx::Type::drive, fx::Type::movement, fx::Type::echo, fx::Type::space })
    {
        view->openEditor ({ t, 0 });
        shoot ("fx-rack-" + juce::String (fx::typeName (t)).toLowerCase());
    }
    for (const int mode : { 1, 2 })
    {
        setValue (p, "drive.mode", static_cast<float> (mode));
        view->openEditor ({ fx::Type::drive, 0 });
        shoot ("fx-rack-drive-mode" + juce::String (mode));
    }
    // RETRO: each personality with audio played through it (its spectrum, its events), each page.
    struct Look { const char* name; int mode, era; std::vector<std::pair<const char*, float>> values; };
    const Look looks[] {
        { "tape", 0, 1, { { "wear", 75.0f }, { "dropout.rate", 3.0f }, { "dropout.depth", 60.0f }, { "noise.level", -60.0f } } },
        { "vinyl", 1, 1, { { "crackle", 45.0f }, { "pops", 30.0f }, { "wear", 40.0f } } },
        { "digital-80s", 2, 0, { { "digital.bits", 9.0f }, { "digital.rate", 12.0f } } },
        { "digital-90s", 2, 1, { { "digital.mod", 60.0f } } },
        { "codec", 2, 2, { { "codec.loss", 55.0f }, { "codec.mode", 4.0f }, { "codec.speed", 60.0f } } },
    };
    const fx::InstanceId retro { fx::Type::retro, 0 };
    view->openEditor (retro);
    for (const auto& look : looks)
    {
        setValue (p, "analog.fx.retro.1.mode", static_cast<float> (look.mode));
        setValue (p, "analog.fx.retro.1.digital.era", static_cast<float> (look.era));
        for (const auto& [n, v] : look.values)
            setValue (p, juce::String ("analog.fx.retro.1.") + n, v);
        renderShell (p, 0.5);
        auto* rack = dynamic_cast<osp::plugin::rack::ParameterRack*> (ui.effectsRack().module (osp::plugin::rack::Kind::fx));
        REQUIRE (rack != nullptr);
        for (int page = 0; page < 4; ++page)
        {
            rack->showPage (page);
            shoot ("fx-rack-retro-" + juce::String (look.name) + (page == 0 ? juce::String() : "-page" + juce::String (page + 1)));
        }
        rack->showPage (0);
        if (look.mode == 0)
            for (const int w : { 869, 1810 })
            {
                width = w;
                shoot ("fx-rack-retro-tape-" + juce::String (w));
                width = 1448;
            }
    }
}

TEST_CASE ("analog: V3 review - every rack family in its modes, full window and rack, at 869, 1448 and 1810 px (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-v3]")
{
    using osp::plugin::rack::Kind;
    using osp::plugin::rack::ParameterRack;
    namespace ap = andor::analog::params;
    namespace fx = andor::analog::fx;
    const auto* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, 1086);
    // The new effects inserted (a CUSTOM route) so their racks open as they do for a musician.
    ui.setSourceViewShown (true);
    auto* view = dynamic_cast<andor::analog::FxRoutingView*> (ui.sourceViewComponent());
    REQUIRE (view != nullptr);
    for (const auto t : { fx::Type::compressor, fx::Type::spread, fx::Type::phaser, fx::Type::retro })
        view->insertNew (t);
    ui.setSourceViewShown (false);
    setValue (p, "echo", 40.0f);
    setValue (p, "space", 35.0f);
    setValue (p, ap::resonance, 40.0f);
    auto openFx = [&] (fx::Type t) { view->openEditor ({ t, 0 }); };
    auto openFilter = [&] {
        ui.setRackExpanded (Kind::filter, true);
        if (auto* f = dynamic_cast<ParameterRack*> (ui.effectsRack().module (Kind::filter)))
        {
            f->showPage (0);
            f->showSubPage (0);
        }
    };
    struct View
    {
        const char* name;
        std::function<void()> set;
    };
    const View views[] {
        { "filter-ladder", [&] { setValue (p, ap::filterModel, 0.0f); openFilter(); } },
        { "filter-ota", [&] { setValue (p, ap::filterModel, 1.0f); openFilter(); } },
        { "movement-drift", [&] { setValue (p, "movement.mode", 0.0f); ui.setRackExpanded (Kind::movement, true); } },
        { "movement-tape", [&] { setValue (p, "movement.mode", 1.0f); ui.setRackExpanded (Kind::movement, true); } },
        { "movement-chorus", [&] { setValue (p, "movement.mode", 2.0f); ui.setRackExpanded (Kind::movement, true); } },
        { "movement-pulse", [&] { setValue (p, "movement.mode", 3.0f); ui.setRackExpanded (Kind::movement, true); } },
        { "echo-tape", [&] { setValue (p, "echo.type", 0.0f); ui.setRackExpanded (Kind::echo, true); } },
        { "echo-bbd", [&] { setValue (p, "echo.type", 1.0f); ui.setRackExpanded (Kind::echo, true); } },
        { "space-hall", [&] { setValue (p, "space.engine", 0.0f); ui.setRackExpanded (Kind::space, true); } },
        { "space-abyss", [&] { setValue (p, "space.engine", 1.0f); ui.setRackExpanded (Kind::space, true); } },
        { "comp-clean", [&] { setValue (p, "analog.fx.comp.1.mode", 0.0f); openFx (fx::Type::compressor); } },
        { "comp-fet", [&] { setValue (p, "analog.fx.comp.1.mode", 1.0f); openFx (fx::Type::compressor); } },
        { "comp-ott", [&] { setValue (p, "analog.fx.comp.1.mode", 2.0f); openFx (fx::Type::compressor); } },
        { "retro", [&] { openFx (fx::Type::retro); } },
        { "spread", [&] { openFx (fx::Type::spread); } },
        { "phaser", [&] { openFx (fx::Type::phaser); } },
    };
    for (const int width : { 1448, 869, 1810 })
        for (const auto& v : views)
        {
            v.set();
            renderShell (p, 0.4);   // (the graphs show what was heard)
            editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
            ui.refreshNow();
            ui.effectsRack().settle();
            auto save = [&] (const juce::Image& image, const juce::String& name) {
                const auto file = out.getChildFile (name + ".png");
                file.deleteFile();
                juce::FileOutputStream stream (file);
                juce::PNGImageFormat().writeImageToStream (image, stream);
            };
            const auto prefix = juce::String (width) + "-" + v.name;
            auto& rack = ui.effectsRack();
            const auto r = editor->getLocalArea (&rack, rack.getLocalBounds());
            save (editor->createComponentSnapshot (r.expanded (0, 6).getIntersection (editor->getLocalBounds()), true, 1.0f), prefix + "-rack");
            if (width == 1448 || juce::String (v.name).startsWith ("space") || juce::String (v.name).startsWith ("filter"))
                save (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), prefix + "-window");
        }
}

TEST_CASE ("analog: FILTER's live graph shows the cutoff the engine's voice uses - ENV, KEYS, velocity, release, the most recent voice, PREVIEW when silent", "[analog][rack][filterlive]")
{
    using osp::plugin::rack::Kind;
    namespace ap = andor::analog::params;
    using andor::analog::filtergraph::LiveView;
    ShellProcessor p;
    auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, 1086);
    ui.setRackExpanded (Kind::filter, true);
    auto* module = ui.effectsRack().module (Kind::filter);
    REQUIRE (module != nullptr);
    juce::Component* graph = nullptr;
    LiveView v;
    for (auto* c : module->getChildren())
        if (andor::analog::filtergraph::liveView (c, v))
            graph = c;
    REQUIRE (graph != nullptr);
    setValue (p, ap::analogAmount, 0.0f);   // (no slot tolerance: the base is the mapping's)
    setValue (p, ap::envDepth, 24.0f);
    setValue (p, ap::keyTrack, 0.0f);
    setValue (p, ap::velocityToFilter, 0.0f);
    setValue (p, ap::envLink, 0.0f);
    setValue (p, ap::filterAttack, 0.008f);
    setValue (p, ap::filterDecay, 0.5f);
    setValue (p, ap::filterSustain, 20.0f);
    setValue (p, ap::filterRelease, 0.35f);
    p.prepareToPlay (rate, block);
    andor::analog::filtergraph::liveView (graph, v, 0.0);   // (the graph reads the settings once)
    auto run = [&p] (double seconds, std::vector<juce::MidiMessage> first = {}) {
        juce::AudioBuffer<float> buffer (2, block);
        const int blocks = std::max (1, static_cast<int> (seconds * rate / block));
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)
                for (auto& m : first)
                    midi.addEvent (m, 0);
            p.processBlock (buffer, midi);
        }
    };
    // The engine's own figures for the voice the graph follows (the truth to compare against).
    auto truth = [&engine] (int note, andor::analog::Synth::FilterTap::Snapshot& out) {
        andor::analog::Synth::FilterTap::Snapshot s;
        for (int slot = 0; slot < AnalogEngine::voiceSlots; ++slot)
            if (engine.filterTap (slot)->read (s) && s.active && s.note == note)
            {
                out = s;
                return true;
            }
        return false;
    };
    andor::analog::Synth::FilterTap::Snapshot t;
    REQUIRE (andor::analog::filtergraph::liveView (graph, v, 0.0));
    CHECK (v.mode == 0);   // IDLE: nothing playing, nothing edited

    // A C4 at velocity 100: LIVE, its base and cutoff exactly the engine's; ENV +24: the reach
    // two octaves (x4) above the base.
    run (0.3, { juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)) });
    andor::analog::filtergraph::liveView (graph, v, 0.0);
    REQUIRE (truth (60, t));
    CHECK (v.mode == 1);
    CHECK (v.note == 60);
    CHECK (v.velocity == 100);
    CHECK (v.baseNote == Approx (t.baseNote).margin (1.0e-4));
    CHECK (v.nowNote == Approx (std::min<double> (t.cutoffNote, engine.cutoffNoteHigh())).margin (1.0e-4));
    CHECK (v.reachNote - v.baseNote == Approx (24.0).margin (1.0e-3));
    CHECK (std::pow (2.0, (v.reachNote - v.baseNote) / 12.0) == Approx (4.0).margin (1.0e-3));
    CHECK (v.peakNote > v.baseNote + 23.0);   // (an 8 ms attack: the envelope reached its top)
    CHECK ((v.stage == 2 || v.stage == 3));    // DECAY or SUSTAIN
    // Held: the cutoff settles at the sustain (20 % of the travel).
    run (1.2);
    andor::analog::filtergraph::liveView (graph, v, 0.0);
    CHECK (v.nowNote - v.baseNote == Approx (0.2 * 24.0).margin (0.25));
    const double sustained = v.nowNote;
    // Released: RELEASE, falling towards the base; the voice ends, the graph rests.
    run (0.1, { juce::MidiMessage::noteOff (1, 60) });
    andor::analog::filtergraph::liveView (graph, v, 0.0);
    CHECK (v.stage == 4);
    CHECK (v.nowNote < sustained);
    run (3.0);
    andor::analog::filtergraph::liveView (graph, v, 0.0);
    CHECK (v.mode == 0);

    // Negative ENV sweeps down: the reach an octave below the base.
    setValue (p, ap::envDepth, -12.0f);
    andor::analog::filtergraph::liveView (graph, v, 0.0);   // (an edit while silent: a PREVIEW starts)
    CHECK (v.mode == 2);
    run (0.2, { juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)) });
    andor::analog::filtergraph::liveView (graph, v, 0.0);
    CHECK (v.mode == 1);   // (a real note replaces the preview at once)
    CHECK (v.reachNote - v.baseNote == Approx (-12.0).margin (1.0e-3));
    CHECK (v.nowNote < v.baseNote);
    run (0.05, { juce::MidiMessage::noteOff (1, 60) });
    run (3.0);

    // KEYS 100 %: C3 and C5 two octaves apart in the base, as the engine has them.
    setValue (p, ap::envDepth, 24.0f);
    setValue (p, ap::keyTrack, 100.0f);
    setValue (p, ap::lfoCutoff, 0.0f);   // (KEYS alone moves the base: no LFO on the cutoff)
    run (0.2, { juce::MidiMessage::noteOn (1, 48, static_cast<juce::uint8> (100)) });
    andor::analog::filtergraph::liveView (graph, v, 0.0);
    REQUIRE (truth (48, t));
    CHECK (v.note == 48);
    CHECK (v.baseNote == Approx (t.baseNote).margin (1.0e-4));
    const double c3 = v.baseNote;
    // The most recent note is followed: C5 now (C3 still sounds); its base as the engine has it,
    // about two octaves up (KEYS 100 %; MOVEMENT's drift wanders each voice a little).
    run (0.2, { juce::MidiMessage::noteOn (1, 72, static_cast<juce::uint8> (100)) });
    andor::analog::filtergraph::liveView (graph, v, 0.0);
    REQUIRE (truth (72, t));
    CHECK (v.note == 72);
    CHECK (v.baseNote == Approx (t.baseNote).margin (1.0e-4));
    CHECK (v.baseNote - c3 == Approx (24.0).margin (3.0));
    // C5 released and over: back to the one still held.
    run (0.05, { juce::MidiMessage::noteOff (1, 72) });
    andor::analog::filtergraph::liveView (graph, v, 0.0);
    CHECK (v.note == 72);   // (its release is still followed)
    run (3.0);
    andor::analog::filtergraph::liveView (graph, v, 0.0);
    CHECK (v.note == 48);
    run (0.05, { juce::MidiMessage::noteOff (1, 48) });
    run (3.0);

    // VEL TO FILTER 100 %: velocity 64 scales the depth to 64 / 127 of ENV, as the engine does.
    setValue (p, ap::keyTrack, 0.0f);
    setValue (p, ap::velocityToFilter, 100.0f);
    run (0.2, { juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (64)) });
    andor::analog::filtergraph::liveView (graph, v, 0.0);
    REQUIRE (truth (60, t));
    CHECK (v.reachNote - v.baseNote == Approx (24.0 * 64.0 / 127.0).margin (1.0e-3));
    CHECK (v.reachNote - v.baseNote == Approx (t.envDepth).margin (1.0e-4));
    run (0.05, { juce::MidiMessage::noteOff (1, 60) });
    run (3.0);
    setValue (p, ap::velocityToFilter, 0.0f);

    // PREVIEW (nothing playing): an edit runs the notional C4 through the same envelope -
    // ATTACK, DECAY, SUSTAIN, RELEASE - and ends at rest. Silent: nothing was rendered.
    setValue (p, ap::filterAttack, 0.2f);
    andor::analog::filtergraph::liveView (graph, v, 0.0);
    CHECK (v.mode == 2);
    andor::analog::filtergraph::liveView (graph, v, 0.1);
    CHECK (v.stage == 1);   // ATTACK (0.2 s)
    CHECK (v.nowNote > v.baseNote);
    CHECK (v.nowNote < v.reachNote);
    andor::analog::filtergraph::liveView (graph, v, 0.15);
    CHECK ((v.stage == 2 || v.stage == 3));
    for (int i = 0; i < 40 && v.mode == 2; ++i)
        andor::analog::filtergraph::liveView (graph, v, 0.1);
    CHECK (v.mode == 0);
    CHECK (v.nowNote == Approx (v.baseNote));
    // ENV LINK: the AMP envelope's times drive it - a 2 s AMP attack is still rising after 0.5 s.
    setValue (p, ap::envLink, 1.0f);
    setValue (p, "attack", 2000.0f);
    andor::analog::filtergraph::liveView (graph, v, 0.0);
    CHECK (v.mode == 2);
    andor::analog::filtergraph::liveView (graph, v, 0.5);
    CHECK (v.stage == 1);
    // ENV 0: the cutoff stays put while the envelope runs.
    setValue (p, ap::envLink, 0.0f);
    setValue (p, ap::envDepth, 0.0f);
    andor::analog::filtergraph::liveView (graph, v, 0.0);
    andor::analog::filtergraph::liveView (graph, v, 0.1);
    CHECK (v.nowNote == Approx (v.baseNote));
}

TEST_CASE ("analog: FILTER's live graph as frames - a played note's sweep, then a silent PREVIEW (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-filter-sweep]")
{
    using osp::plugin::rack::Kind;
    namespace ap = andor::analog::params;
    using andor::analog::filtergraph::LiveView;
    const auto* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, 1086);
    ui.setRackExpanded (Kind::filter, true);
    setValue (p, ap::envDepth, 30.0f);
    setValue (p, ap::resonance, 55.0f);
    setValue (p, ap::tone, 35.0f);
    setValue (p, ap::filterAttack, 0.35f);
    setValue (p, ap::filterDecay, 0.8f);
    setValue (p, ap::filterSustain, 35.0f);
    setValue (p, ap::filterRelease, 0.9f);
    editor->setSize (1448, juce::roundToInt (1448.0f * ui.instrumentHeight() / 1448.0f));
    ui.refreshNow();
    auto* module = ui.effectsRack().module (Kind::filter);
    REQUIRE (module != nullptr);
    juce::Component* graph = nullptr;
    LiveView v;
    for (auto* c : module->getChildren())
        if (andor::analog::filtergraph::liveView (c, v))
            graph = c;
    REQUIRE (graph != nullptr);
    p.prepareToPlay (rate, block);
    // (The setup's edits started a preview: let it end before recording.)
    for (int i = 0; i < 200 && (andor::analog::filtergraph::liveView (graph, v, 0.1), v.mode != 0); ++i) {}
    constexpr double frame = 1.0 / 30.0;
    int index = 0;
    auto shoot = [&] {
        auto& rack = ui.effectsRack();
        const auto r = editor->getLocalArea (&rack, rack.getLocalBounds());
        graph->repaint();
        const auto image = editor->createComponentSnapshot (r, true, 1.0f);
        const auto file = out.getChildFile ("sweep-" + juce::String (index++).paddedLeft ('0', 4) + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    juce::AudioBuffer<float> buffer (2, 1600);   // (one frame of audio at 48 kHz)
    auto play = [&] (double seconds, std::vector<juce::MidiMessage> first = {}) {
        for (int f = 0; f < static_cast<int> (seconds / frame); ++f)
        {
            juce::MidiBuffer midi;
            if (f == 0)
                for (auto& m : first)
                    midi.addEvent (m, 0);
            for (int done = 0; done < 1600; done += block)
            {
                juce::AudioBuffer<float> part (2, block);
                p.processBlock (part, midi);
                midi.clear();
            }
            andor::analog::filtergraph::liveView (graph, v, frame);
            shoot();
        }
    };
    play (0.3);
    play (2.0, { juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)) });   // LIVE: attack, decay, sustain
    play (1.6, { juce::MidiMessage::noteOff (1, 60) });                                     // release, then rest
    // Silent PREVIEW: an edit of DECAY with nothing playing.
    setValue (p, ap::filterDecay, 0.4f);
    for (int f = 0; f < 75; ++f)
    {
        andor::analog::filtergraph::liveView (graph, v, frame);
        shoot();
    }
}

TEST_CASE ("analog: V4 approval shots - ROUTING (classic, five, ten in two rows, parallel, 869) and the TAPE racks (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-v4]")
{
    namespace fx = andor::analog::fx;
    using osp::plugin::rack::Kind;
    const auto* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    setValue (p, "drive", 35.0f);
    setValue (p, "echo", 40.0f);
    setValue (p, "space", 30.0f);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
    int width = 1448;
    auto save = [&] (const juce::Image& image, const juce::String& name) {
        const auto file = out.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    auto shoot = [&] (const juce::String& name) {
        editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
        ui.refreshNow();
        ui.effectsRack().settle();
        save (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), name);
    };
    ui.setSourceViewShown (true);
    REQUIRE (dynamic_cast<andor::analog::FxRoutingView*> (ui.sourceViewComponent()) != nullptr);
    auto e = [] (fx::Type t, int slot = 0) { return fx::Node::effect ({ t, slot }); };
    auto custom = [] (std::vector<fx::Node> chain) {
        fx::Graph g;
        g.custom = true;
        g.chain = std::move (chain);
        return g;
    };
    // 1. The original CLASSIC path.
    shoot ("v4-1-routing-classic");
    // 2. Five in a row.
    REQUIRE (engine.setFxRoute (custom ({ e (fx::Type::drive), e (fx::Type::movement), e (fx::Type::echo), e (fx::Type::space), e (fx::Type::spread) })));
    shoot ("v4-2-routing-five");
    // 3. Ten: two rows.
    const auto ten = custom ({ e (fx::Type::drive), e (fx::Type::movement), e (fx::Type::compressor), e (fx::Type::eq), e (fx::Type::echo), e (fx::Type::space),
                               e (fx::Type::spread), e (fx::Type::phaser), e (fx::Type::retro), e (fx::Type::utility) });
    REQUIRE (engine.setFxRoute (ten));
    shoot ("v4-3-routing-ten");
    // 4. Two parallel branches (ECHO || SPACE then a serial SPREAD after the join).
    REQUIRE (engine.setFxRoute (custom ({ e (fx::Type::drive), e (fx::Type::movement), fx::Node::parallel ({ e (fx::Type::echo), e (fx::Type::compressor) }, { e (fx::Type::space) }),
                                          e (fx::Type::spread) })));
    shoot ("v4-4-routing-parallel");
    // 5. The ten at 869.
    REQUIRE (engine.setFxRoute (ten));
    width = 869;
    shoot ("v4-5-routing-ten-869");
    width = 1448;
    // 6. ECHO TAPE, 7. MOVEMENT TAPE (the whole instrument: the rack's height), 8. the rail.
    REQUIRE (engine.setFxRoute (fx::classicGraph()));
    ui.setSourceViewShown (false);
    setValue (p, "echo.type", 0.0f);
    ui.setRackExpanded (Kind::echo, true);
    renderShell (p, 0.6);
    shoot ("v4-6-echo-tape");
    setValue (p, "movement.mode", 1.0f);
    setValue (p, "motion", 45.0f);
    ui.setRackExpanded (Kind::movement, true);
    renderShell (p, 0.6);
    shoot ("v4-7-movement-tape");
    {
        // The rail: A / B, the horizontal ON, the preset bar, the actions, the borderless X.
        auto& rack = ui.effectsRack();
        const auto r = editor->getLocalArea (&rack, rack.getLocalBounds());
        save (editor->createComponentSnapshot (r.withLeft (r.getRight() - 620).withHeight (40), true, 2.0f), "v4-8-header-closeup");
    }
}

//==============================================================================
// V4 (docs/FX_ROUTING_TAPE_V4.md): the routing's mixer knobs, its two-row projection of the
// route, the TAPE decks.

TEST_CASE ("analog: V4 ROUTING - the engine mixer's knobs are the cards' own levels (same parameters, same units)", "[analog][fx][routing][ui][v4]")
{
    namespace fx = andor::analog::fx;
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
    ui.setSourceViewShown (true);
    auto* view = dynamic_cast<andor::analog::FxRoutingView*> (ui.sourceViewComponent());
    REQUIRE (view != nullptr);
    // About a fifth of the panel (V4: 21 %, never more than a quarter).
    CHECK (view->mixerBounds().getWidth() <= view->getWidth() / 4);
    CHECK (view->mixerBounds().getWidth() >= view->getWidth() / 6);
    const juce::String ids[3] { "analog.osc1.level", "analog.osc2.level", "layerB.level" };
    for (int i = 0; i < 3; ++i)
    {
        INFO (ids[i]);
        auto* knob = view->levelKnob (i);
        REQUIRE (knob != nullptr);
        auto* param = p.apvts().getParameter (ids[i]);
        REQUIRE (param != nullptr);
        CHECK (knob->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag);
        CHECK (knob->getWidth() >= 36);
        CHECK (knob->getWidth() <= 56);
        CHECK (knob->getWidth() == knob->getHeight());
        CHECK (view->mixerBounds().contains (knob->getBounds()));
        // The knob moves the card's parameter; the parameter (a card, automation) moves the knob.
        const double target = knob->getMinimum() + 0.3 * (knob->getMaximum() - knob->getMinimum());
        knob->setValue (target, juce::sendNotificationSync);
        CHECK (p.parameterValue (ids[i]) == Catch::Approx (knob->getValue()).margin (1.0e-3));
        setValue (p, ids[i], param->convertFrom0to1 (param->getDefaultValue()));
        CHECK (knob->getValue() == Catch::Approx (param->convertFrom0to1 (param->getDefaultValue())).margin (1.0e-3));
    }
    // HYBRID's level is in dB at 0.0 dB (never turned into a percentage by the knob).
    CHECK (p.apvts().getParameter ("layerB.level")->getCurrentValueAsText() == "0.0 dB");
    // Nothing about the route moved.
    CHECK (dynamic_cast<AnalogEngine&> (p.productEngine()).fxRoute() == fx::classicGraph());
}

TEST_CASE ("analog: V4 ROUTING - the board is a projection of the route: one row, two rows in order, parallel stages, summaries, never scrolled", "[analog][fx][routing][ui][v4]")
{
    namespace fx = andor::analog::fx;
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
    auto sizeTo = [&] (int width) {
        editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
        ui.refreshNow();
        editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);   // (paint: the board follows the scale)
    };
    ui.setSourceViewShown (true);
    auto* view = dynamic_cast<andor::analog::FxRoutingView*> (ui.sourceViewComponent());
    REQUIRE (view != nullptr);
    sizeTo (1448);
    auto e = [] (fx::Type t, int slot = 0) { return fx::Node::effect ({ t, slot }); };
    auto custom = [] (std::vector<fx::Node> chain) {
        fx::Graph g;
        g.custom = true;
        g.chain = std::move (chain);
        return g;
    };
    auto relayout = [&] {
        view->resized();
        editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    };
    auto noOverlaps = [&] {
        const auto& t = view->tiles();
        for (std::size_t i = 0; i < t.size(); ++i)
        {
            CHECK (view->chainBounds().contains (t[i].bounds));
            CHECK_FALSE (t[i].bounds.intersects (view->outputBounds()));
            for (std::size_t j = i + 1; j < t.size(); ++j)
                CHECK_FALSE (t[i].bounds.intersects (t[j].bounds));
            for (const auto& s : view->summaries())
                CHECK_FALSE (t[i].bounds.intersects (s.bounds));
        }
    };

    // (V5 PATTERNED HARDWARE supersedes V4's tile size: miniature hardware 178-204 x 70-84 at
    // the canonical width, four a row; the topology rules are V4's.)
    // Four: one row, centred on one line.
    REQUIRE (engine.setFxRoute (custom ({ e (fx::Type::drive), e (fx::Type::movement), e (fx::Type::echo), e (fx::Type::space) })));
    relayout();
    CHECK (view->rowCount() == 1);
    REQUIRE (view->tiles().size() == 4);
    for (std::size_t i = 0; i < 4; ++i)
    {
        const auto& t = view->tiles()[i];
        CHECK (t.bounds.getWidth() >= 178);
        CHECK (t.bounds.getWidth() <= 204);
        CHECK (t.bounds.getHeight() >= 70);
        CHECK (t.bounds.getHeight() <= 84);
        CHECK (t.bounds.getCentreY() == view->outputBounds().getCentreY());
        CHECK (t.light.getWidth() >= 24);   // (the bypass target)
        if (i > 0)
            CHECK (t.bounds.getX() > view->tiles()[i - 1].bounds.getRight());
    }
    CHECK (view->summaries().empty());
    noOverlaps();

    // Eight: two rows, four each; row 2 continues row 1 left to right on one column grid;
    // CONT. 2 / FROM 1; OUTPUT on the last tile's line; the route itself untouched.
    const auto eight = custom ({ e (fx::Type::drive), e (fx::Type::movement), e (fx::Type::compressor), e (fx::Type::eq), e (fx::Type::echo), e (fx::Type::space),
                                 e (fx::Type::spread), e (fx::Type::phaser) });
    REQUIRE (engine.setFxRoute (eight));
    const auto text = fx::toText (engine.fxRoute());
    relayout();
    CHECK (fx::toText (engine.fxRoute()) == text);
    CHECK (view->rowCount() == 2);
    REQUIRE (view->tiles().size() == 8);
    const auto order = fx::instancesOf (engine.fxRoute());
    for (std::size_t i = 0; i < 8; ++i)
        CHECK (view->tiles()[i].id == order[i]);
    const int row1 = view->tiles().front().bounds.getCentreY(), row2 = view->tiles().back().bounds.getCentreY();
    CHECK (row2 > row1);
    for (std::size_t i = 0; i < 8; ++i)
    {
        const auto& t = view->tiles()[i];
        CHECK (t.bounds.getCentreY() == (i < 4 ? row1 : row2));
        if (i % 4 > 0)
            CHECK (t.bounds.getX() > view->tiles()[i - 1].bounds.getRight());   // (never reversed)
        if (i >= 4)
            CHECK (t.bounds.getX() == view->tiles()[i - 4].bounds.getX());     // (one column grid)
    }
    CHECK (! view->continuationOut().isEmpty());
    CHECK (! view->continuationIn().isEmpty());
    CHECK (view->continuationOut().getCentreY() == row1);
    CHECK (view->continuationIn().getCentreY() == row2);
    CHECK (view->outputBounds().getCentreY() == row2);
    CHECK (view->wireStart().getCentreY() == row1);
    noOverlaps();
    // A drop into row 2's first gap goes between the rows' effects (index 4), as drawn.
    const auto gap = std::find_if (view->gaps().begin(), view->gaps().end(), [&] (const auto& g) {
        return g.path.empty() && g.index == 4 && g.bounds.getCentreY() == row2;
    });
    REQUIRE (gap != view->gaps().end());
    REQUIRE (view->moveTo ({ fx::Type::phaser, 0 }, gap->path, gap->index));
    CHECK (fx::instancesOf (engine.fxRoute())[4] == fx::InstanceId { fx::Type::phaser, 0 });
    p.undo().undo();
    CHECK (fx::toText (engine.fxRoute()) == text);

    // Ten at the canonical width: eight tiles' room - what does not fit is a labelled summary;
    // every effect is on the board, in the route's order.
    const auto ten = custom ({ e (fx::Type::drive), e (fx::Type::movement), e (fx::Type::compressor), e (fx::Type::eq), e (fx::Type::echo), e (fx::Type::space),
                               e (fx::Type::spread), e (fx::Type::phaser), e (fx::Type::retro), e (fx::Type::utility) });
    REQUIRE (engine.setFxRoute (ten));
    relayout();
    CHECK (view->rowCount() == 2);
    CHECK (! view->summaries().empty());
    {
        std::vector<fx::InstanceId> shown;
        for (const auto& tile : view->tiles())
            shown.push_back (tile.id);
        for (const auto& sum : view->summaries())
            for (const auto& id : sum.members)
                shown.push_back (id);
        CHECK (shown == fx::instancesOf (engine.fxRoute()));
    }
    noOverlaps();

    // A parallel stage: its lanes in one stage (split before, join after), on the row's line.
    REQUIRE (engine.setFxRoute (custom ({ e (fx::Type::drive), fx::Node::parallel ({ e (fx::Type::echo) }, { e (fx::Type::space) }), e (fx::Type::spread) })));
    relayout();
    CHECK (view->rowCount() == 1);
    REQUIRE (view->tiles().size() == 4);
    const auto& t = view->tiles();
    CHECK (t[1].stacked);
    CHECK (t[2].stacked);   // (SPACE: the lower lane)
    CHECK (t[1].bounds.getX() == t[2].bounds.getX());
    CHECK (t[2].bounds.getY() > t[1].bounds.getBottom());
    CHECK (t[0].bounds.getRight() < std::min (t[1].bounds.getX(), t[2].bounds.getX()));
    CHECK (t[3].bounds.getX() > std::max (t[1].bounds.getRight(), t[2].bounds.getRight()));
    CHECK (t[3].bounds.getCentreY() == view->outputBounds().getCentreY());
    CHECK (t[0].bounds.getCentreY() == t[3].bounds.getCentreY());
    noOverlaps();
    // The wider stage wraps as a whole: its lanes stay together on row 2, after the split.
    REQUIRE (engine.setFxRoute (custom ({ e (fx::Type::drive), e (fx::Type::movement), fx::Node::parallel ({ e (fx::Type::echo), e (fx::Type::compressor) }, { e (fx::Type::space) }),
                                          e (fx::Type::spread) })));
    relayout();
    REQUIRE (view->tiles().size() == 6);
    CHECK (view->tiles()[2].bounds.getCentreY() == view->tiles()[3].bounds.getCentreY());
    CHECK (view->tiles()[4].bounds.getY() > view->tiles()[2].bounds.getBottom());
    CHECK (view->tiles()[5].bounds.getX() > std::max (view->tiles()[3].bounds.getRight(), view->tiles()[4].bounds.getRight()));
    noOverlaps();

    // 869: larger tiles in design units (legible), four a row at most; what does not fit is a
    // labelled summary - every effect is on the board, as a tile or in a summary.
    REQUIRE (engine.setFxRoute (ten));
    sizeTo (869);
    CHECK (view->tileScale() > 1.1f);
    std::map<int, int> perRow;
    for (const auto& tile : view->tiles())
        ++perRow[tile.bounds.getCentreY()];
    for (const auto& s : view->summaries())
        ++perRow[s.bounds.getCentreY()];
    CHECK (perRow.size() <= 2);
    for (const auto& [y, count] : perRow)
        CHECK (count <= 4);
    std::vector<fx::InstanceId> shown;
    for (const auto& tile : view->tiles())
        shown.push_back (tile.id);
    for (const auto& s : view->summaries())
        for (const auto& id : s.members)
            shown.push_back (id);
    CHECK (shown == fx::instancesOf (engine.fxRoute()));   // (all of them, in the route's order)
    noOverlaps();
    CHECK (fx::toText (engine.fxRoute()) == fx::toText (ten));
    // Back to the canonical width: eight fit as tiles again.
    sizeTo (1448);
    REQUIRE (engine.setFxRoute (eight));
    relayout();
    CHECK (view->summaries().empty());
    CHECK (view->tiles().size() == 8);
}

TEST_CASE ("analog: V4 TAPE - ECHO and MOVEMENT TAPE as a reel deck on the same parameters; the reels rest in silence", "[analog][rack][ui][v4]")
{
    using osp::plugin::rack::Kind;
    using osp::plugin::rackui::SegmentedSwitch;
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
    setValue (p, "echo", 40.0f);
    setValue (p, "echo.type", 0.0f);   // TAPE
    ui.setRackExpanded (Kind::echo, true);
    ui.refreshNow();
    auto* echo = ui.effectsRack().module (Kind::echo);
    REQUIRE (echo != nullptr);
    const int height = echo->getHeight();
    REQUIRE (osp::plugin::rackTapeDeckShown (*echo));
    CHECK (osp::plugin::rackMovementCaptions (*echo) == "TIME FEEDBACK TONE AGE TIMING STEREO");
    // The regions: knobs left, the deck in the middle, the display right - its whole column,
    // as tall as SPACE's graph (to the face's bottom margin), no caption over it.
    const auto deck = osp::plugin::rackTapeDeckBounds (*echo);
    const auto display = osp::plugin::rackSlot (*echo, 5);
    const auto cells = osp::plugin::rackCellBounds (*echo);
    REQUIRE (cells.size() == 4);
    CHECK (cells.back().getRight() < deck.getX());
    CHECK (deck.getRight() < display.getX());
    CHECK (display.getWidth() >= 250);
    CHECK (display.getRight() <= echo->getWidth());
    CHECK (display.getHeight() >= 140);
    CHECK (display.getBottom() == echo->getHeight() - 16);
    CHECK (deck.getBottom() <= echo->getHeight());
    // TIMING and STEREO: the same parameters, as small segmented keys.
    auto* timing = dynamic_cast<SegmentedSwitch*> (osp::plugin::rackCell (*echo, "TIMING"));
    auto* stereo = dynamic_cast<SegmentedSwitch*> (osp::plugin::rackCell (*echo, "STEREO"));
    REQUIRE (timing != nullptr);
    REQUIRE (stereo != nullptr);
    timing->choose (0);
    CHECK (p.parameterValue ("echo.sync") < 0.5f);
    timing->choose (1);
    CHECK (p.parameterValue ("echo.sync") >= 0.5f);
    stereo->choose (2);
    CHECK (juce::roundToInt (p.parameterValue ("echo.stereo")) == 2);
    setValue (p, "echo.stereo", 1.0f);
    CHECK (stereo->positionIndex() == 1);   // (automation moves it)

    // The reels: still in silence; turning while the echo sounds; easing to rest when it stops.
    CHECK (osp::plugin::rackTapeDeckSpeed (*echo) == 0.0);
    osp::plugin::advanceRackTapeDeck (*echo, 0.5);
    CHECK (osp::plugin::rackTapeDeckSpeed (*echo) == 0.0);   // (nothing has sounded)
    renderShell (p, 0.3);   // (the phrase: notes still sounding at its end)
    REQUIRE (p.outputPeak() > 1.0e-3f);
    osp::plugin::advanceRackTapeDeck (*echo, 1.0);
    CHECK (osp::plugin::rackTapeDeckSpeed (*echo) > 0.5);
    setValue (p, "echo", 0.0f);   // (the echo off: the module no longer plays)
    for (int i = 0; i < 90; ++i)
        osp::plugin::advanceRackTapeDeck (*echo, 1.0 / 30.0);
    CHECK (osp::plugin::rackTapeDeckSpeed (*echo) == 0.0);
    setValue (p, "echo", 40.0f);

    // Other ECHO modes: as approved (no deck); the rack keeps its height.
    setValue (p, "echo.type", 1.0f);   // BBD
    ui.refreshNow();
    CHECK_FALSE (osp::plugin::rackTapeDeckShown (*echo));
    CHECK (osp::plugin::rackMovementCaptions (*echo) == "TIME FEEDBACK TONE AGE TIMING STEREO");
    CHECK (echo->getHeight() == height);
    setValue (p, "echo.type", 0.0f);
    ui.refreshNow();
    CHECK (osp::plugin::rackTapeDeckShown (*echo));

    // MOVEMENT TAPE: INTENSITY, WOW, FLUTTER, WEAR (never delay controls); its own deck.
    setValue (p, "motion", 45.0f);
    setValue (p, "movement.mode", 1.0f);   // TAPE
    ui.setRackExpanded (Kind::movement, true);
    ui.refreshNow();
    auto* movement = ui.effectsRack().module (Kind::movement);
    REQUIRE (movement != nullptr);
    CHECK (osp::plugin::rackTapeDeckShown (*movement));
    CHECK (osp::plugin::rackMovementCaptions (*movement) == "INTENSITY WOW FLUTTER WEAR");
    CHECK (movement->getHeight() == height);
    renderShell (p, 0.3);
    osp::plugin::advanceRackTapeDeck (*movement, 1.0);
    CHECK (osp::plugin::rackTapeDeckSpeed (*movement) > 0.5);
    setValue (p, "movement.mode", 0.0f);   // DRIFT: as approved
    ui.refreshNow();
    CHECK_FALSE (osp::plugin::rackTapeDeckShown (*movement));
}

TEST_CASE ("analog: V5 review matrix - the route board (six, classic, parallel, five, 869) and every rack (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-v5]")
{
    namespace fx = andor::analog::fx;
    namespace ap = andor::analog::params;
    using osp::plugin::rack::Kind;
    const auto* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    setValue (p, "drive", 35.0f);
    setValue (p, "echo", 40.0f);
    setValue (p, "space", 30.0f);
    setValue (p, "motion", 45.0f);
    setValue (p, "movement.mode", 2.0f);   // (CHORUS: the reference's MOVEMENT, and its rack shot's)
    setValue (p, ap::resonance, 40.0f);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
    int width = 1448;
    auto save = [&] (const juce::Image& image, const juce::String& name) {
        const auto file = out.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    auto size = [&] {
        editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
        ui.refreshNow();
        ui.effectsRack().settle();
    };
    auto shoot = [&] (const juce::String& name) {
        size();
        save (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), name);
    };
    ui.setSourceViewShown (true);
    auto* view = dynamic_cast<andor::analog::FxRoutingView*> (ui.sourceViewComponent());
    REQUIRE (view != nullptr);
    auto e = [] (fx::Type t, int slot = 0) { return fx::Node::effect ({ t, slot }); };
    auto custom = [] (std::vector<fx::Node> chain) {
        fx::Graph g;
        g.custom = true;
        g.chain = std::move (chain);
        return g;
    };
    auto routeShot = [&] (const juce::String& name) {
        size();
        const auto r = editor->getLocalArea (view, view->getLocalBounds());
        save (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), name);
        save (editor->createComponentSnapshot (r.expanded (4).getIntersection (editor->getLocalBounds()), true, width < 1000 ? 2.0f : 1.0f), name + "-board");
    };
    // The six of the reference (MOVEMENT, DRIVE, ECHO, FX FILTER, SPACE, STEREO): a serial
    // chain, its own order; CLASSIC; a true parallel stage; five; the six at 869.
    const auto six = custom ({ e (fx::Type::movement), e (fx::Type::drive), e (fx::Type::echo), e (fx::Type::filter), e (fx::Type::space), e (fx::Type::spread) });
    REQUIRE (engine.setFxRoute (six));
    routeShot ("v5-01-routing-six");
    // Each tile alone (2x): the miniature of its rack (the pairs).
    auto tileShots = [&] {
        for (const auto& t : view->tiles())
        {
            const auto r = editor->getLocalArea (view, t.bounds.expanded (6));
            save (editor->createComponentSnapshot (r, true, 2.0f), juce::String ("v5-tile-") + fx::typeKey (t.id.type));
        }
    };
    tileShots();
    REQUIRE (engine.setFxRoute (fx::classicGraph()));
    routeShot ("v5-02-routing-classic");
    REQUIRE (engine.setFxRoute (custom ({ e (fx::Type::drive), e (fx::Type::movement), fx::Node::parallel ({ e (fx::Type::echo), e (fx::Type::compressor) }, { e (fx::Type::space) }),
                                          e (fx::Type::spread) })));
    routeShot ("v5-03-routing-parallel");
    REQUIRE (engine.setFxRoute (custom ({ e (fx::Type::compressor), e (fx::Type::eq), e (fx::Type::phaser), e (fx::Type::flanger), e (fx::Type::shifter), e (fx::Type::convolution),
                                          e (fx::Type::utility), e (fx::Type::retro) })));
    routeShot ("v5-04-routing-eight-others");
    tileShots();
    REQUIRE (engine.setFxRoute (six));
    width = 869;
    routeShot ("v5-05-routing-six-869");
    width = 1448;
    // The library (its entries' identities).
    view->showLibrary (true);
    routeShot ("v5-06-library");
    view->showLibrary (false);

    // Every rack, full window and the rack alone.
    ui.setSourceViewShown (false);
    REQUIRE (engine.setFxRoute (fx::classicGraph()));
    struct Rack
    {
        const char* name;
        std::function<void()> open;
    };
    auto openFx = [&] (fx::Type t) { andor::analog::openFxRack (ui, { t, 0 }); };
    const Rack racks[] {
        { "movement-chorus", [&] { setValue (p, "movement.mode", 2.0f); ui.setRackExpanded (Kind::movement, true); } },
        { "movement-tape", [&] { setValue (p, "movement.mode", 1.0f); ui.setRackExpanded (Kind::movement, true); } },
        { "drive", [&] { openFx (fx::Type::drive); } },
        { "echo-tape", [&] { setValue (p, "echo.type", 0.0f); ui.setRackExpanded (Kind::echo, true); } },
        { "echo-bbd", [&] { setValue (p, "echo.type", 1.0f); ui.setRackExpanded (Kind::echo, true); } },
        { "fxfilter", [&] { openFx (fx::Type::filter); } },
        { "space-hall", [&] { setValue (p, "space.engine", 0.0f); ui.setRackExpanded (Kind::space, true); } },
        { "spread", [&] { openFx (fx::Type::spread); } },
        { "compressor", [&] { openFx (fx::Type::compressor); } },
        { "eq", [&] { openFx (fx::Type::eq); } },
        { "phaser", [&] { openFx (fx::Type::phaser); } },
        { "flanger", [&] { openFx (fx::Type::flanger); } },
        { "shifter", [&] { openFx (fx::Type::shifter); } },
        { "convolution", [&] { openFx (fx::Type::convolution); } },
        { "utility", [&] { openFx (fx::Type::utility); } },
        { "retro", [&] { openFx (fx::Type::retro); } },
        { "voice-filter", [&] { ui.setRackExpanded (Kind::filter, true); } },
        { "morph", [&] { ui.setRackExpanded (Kind::morph, true); } },
    };
    for (const int w : { 1448, 869 })
        for (const auto& r : racks)
        {
            if (w == 869 && ! juce::String (r.name).startsWith ("movement-chorus") && ! juce::String (r.name).startsWith ("echo-tape") && ! juce::String (r.name).startsWith ("compressor"))
                continue;
            width = w;
            r.open();
            renderShell (p, 0.4);
            size();
            const auto prefix = "v5-rack-" + juce::String (w) + "-" + r.name;
            auto& rack = ui.effectsRack();
            const auto area = editor->getLocalArea (&rack, rack.getLocalBounds());
            save (editor->createComponentSnapshot (area.expanded (0, 6).getIntersection (editor->getLocalBounds()), true, w < 1000 ? 2.0f : 1.0f), prefix);
            if (w == 1448 && (juce::String (r.name) == "echo-tape" || juce::String (r.name) == "movement-chorus"))
                save (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), prefix + "-window");
        }
    width = 1448;
}

TEST_CASE ("analog: V5 audio baseline - representative factory presets and routes render to recorded hashes (ANALOG_AUDIO_HASHES)", "[.][v5-audio]")
{
    // A skin pass must be audibly null: these renders (factory presets, CLASSIC and a CUSTOM
    // route through every effect) are hashed before the pass and compared after it.
    namespace fx = andor::analog::fx;
    const auto* path = std::getenv ("ANALOG_AUDIO_HASHES");
    REQUIRE (path != nullptr);
    const juce::File file (path);
    auto hashOf = [] (const juce::AudioBuffer<float>& b) {
        std::uint64_t h = 1469598103934665603ull;   // (FNV-1a over the samples' bytes)
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const auto* bytes = reinterpret_cast<const unsigned char*> (b.getReadPointer (ch));
            for (std::size_t i = 0; i < sizeof (float) * static_cast<std::size_t> (b.getNumSamples()); ++i)
                h = (h ^ bytes[i]) * 1099511628211ull;
        }
        return juce::String::toHexString (static_cast<juce::int64> (h));
    };
    juce::StringArray lines;
    {
        ShellProcessor p;
        const auto list = p.presetList();
        REQUIRE (list.size() >= 5);
        for (std::size_t i = 0; i < 5; ++i)
        {
            ShellProcessor q;
            q.openPresetEntry (list[i * (list.size() - 1) / 4]);
            lines.add (list[i * (list.size() - 1) / 4].name + " " + hashOf (renderShell (q, 1.0)));
        }
    }
    {
        ShellProcessor q;
        setValue (q, "drive", 35.0f);
        setValue (q, "echo", 40.0f);
        setValue (q, "space", 30.0f);
        setValue (q, "motion", 45.0f);
        lines.add ("CLASSIC " + hashOf (renderShell (q, 1.0)));
        fx::Graph g;
        g.custom = true;
        for (int t = 0; t < fx::typeCount; ++t)
            g.chain.push_back (fx::Node::effect ({ static_cast<fx::Type> (t), 0 }));
        REQUIRE (dynamic_cast<AnalogEngine&> (q.productEngine()).setFxRoute (g));
        lines.add ("CUSTOM-ALL " + hashOf (renderShell (q, 1.0)));
    }
    const auto text = lines.joinIntoString ("\n");
    if (file.existsAsFile())
        CHECK (file.loadFileAsString().replace ("\r\n", "\n").trim() == text.trim());   // (the second run: the same sound)
    else
        file.replaceWithText (text, false, false, "\n");
}

TEST_CASE ("analog: V5 PATTERNED HARDWARE - one identity per device, legible, no violet; the tiles' knobs are the racks' parameters", "[analog][fx][routing][ui][v5]")
{
    namespace fx = andor::analog::fx;
    using andor::analog::visualIdentityFor;
    // Every device: its own enamel and motif, its name readable on it, no violet hue.
    std::set<juce::uint32> enamels;
    std::set<int> motifs;
    for (int i = 0; i < fx::typeCount; ++i)
    {
        const auto type = static_cast<fx::Type> (i);
        const auto& id = visualIdentityFor (type);
        INFO (fx::typeName (type));
        enamels.insert (id.enamel.getARGB());
        motifs.insert (static_cast<int> (id.motif));
        CHECK (andor::analog::contrastRatio (id.text, id.enamel) >= 3.0f);   // (bold 13.5 px names: large-text contrast)
        const float hue = id.enamel.getHue() * 360.0f;
        CHECK_FALSE ((hue > 245.0f && hue < 330.0f && id.enamel.getSaturation() > 0.12f));   // (no violet / purple)
        CHECK (juce::String (id.tileName).isNotEmpty());
    }
    CHECK (static_cast<int> (enamels.size()) == fx::typeCount);
    CHECK (static_cast<int> (motifs.size()) == fx::typeCount);
    for (const auto rack : { andor::analog::SynthRack::voiceFilter, andor::analog::SynthRack::morph })
    {
        const auto& id = andor::analog::synthIdentity (rack);
        CHECK (andor::analog::contrastRatio (id.text, id.enamel) >= 3.0f);
        CHECK (motifs.count (static_cast<int> (id.motif)) == 0);   // (their own motifs)
    }
    // The faceplates: the whole face is the enamel, with the device's motif.
    const auto f = andor::analog::faceplateFromIdentity (visualIdentityFor (fx::Type::echo));
    CHECK (f.enamelBody);
    CHECK (f.fullBleed);
    CHECK (f.motif != nullptr);
    CHECK (f.body == visualIdentityFor (fx::Type::echo).enamel);

    // The tiles' knobs: real attachments to the racks' parameters, both ways; none on a
    // parallel lane's compact tile.
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
    ui.setSourceViewShown (true);
    auto* view = dynamic_cast<andor::analog::FxRoutingView*> (ui.sourceViewComponent());
    REQUIRE (view != nullptr);
    auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
    CHECK (view->tileKnobParameters ({ fx::Type::drive, 0 }) == juce::StringArray { "drive", "drive.tone", "drive.body" });
    CHECK (view->tileKnobParameters ({ fx::Type::echo, 0 }).isEmpty());   // (CLASSIC: ECHO and SPACE are a parallel pair)
    fx::Graph g;
    g.custom = true;
    for (const auto t : { fx::Type::movement, fx::Type::drive, fx::Type::echo, fx::Type::filter, fx::Type::space, fx::Type::spread })
        g.chain.push_back (fx::Node::effect ({ t, 0 }));
    REQUIRE (engine.setFxRoute (g));
    view->resized();
    const auto before = fx::toText (engine.fxRoute());
    CHECK (view->tileKnobParameters ({ fx::Type::echo, 0 }) == juce::StringArray { "echo.division", "echo.feedback", "echo" });
    CHECK (view->tileKnobParameters ({ fx::Type::space, 0 }) == juce::StringArray { "space.size", "space.decay", "space" });
    const fx::InstanceId filter { fx::Type::filter, 0 };
    const auto filterKnobs = view->tileKnobParameters (filter);
    REQUIRE (filterKnobs.size() == 3);
    for (const auto& t : view->tiles())
        for (const auto& pidName : view->tileKnobParameters (t.id))
        {
            INFO (pidName.toStdString());
            auto* k = view->tileKnob (t.id, pidName);
            REQUIRE (k != nullptr);
            CHECK (k->getParentComponent() == view);
            CHECK (t.bounds.contains (k->getBounds()));
            CHECK (k->getWidth() >= 28);   // (the hit area)
            CHECK_FALSE (k->getBounds().intersects (t.light));
            auto* param = p.apvts().getParameter (pidName);
            REQUIRE (param != nullptr);
            const double target = k->getMinimum() + 0.37 * (k->getMaximum() - k->getMinimum());
            k->setValue (target, juce::sendNotificationSync);
            CHECK (p.parameterValue (pidName) == Catch::Approx (k->getValue()).margin (1.0e-3));
            setValue (p, pidName, param->convertFrom0to1 (param->getDefaultValue()));
            CHECK (k->getValue() == Catch::Approx (param->convertFrom0to1 (param->getDefaultValue())).margin (1.0e-3));
        }
    // Turning knobs, laying out again, resizing: the route itself never moved.
    view->resized();
    CHECK (fx::toText (engine.fxRoute()) == before);
    // A mode change changes which knobs a tile shows, never a parameter.
    const auto values = allParameterValues (p);
    setValue (p, "movement.mode", 2.0f);   // CHORUS
    view->resized();
    CHECK (view->tileKnobParameters ({ fx::Type::movement, 0 }).contains ("motion"));
    setValue (p, "movement.mode", values.at ("movement.mode"));
}

TEST_CASE ("analog: every rack with presets has a save key beside its browser, left of the actions", "[analog][rack][ui][savekey]")
{
    namespace fx = andor::analog::fx;
    using osp::plugin::rack::Kind;
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
    // MOVEMENT, ECHO, SPACE: the browser (slot 3), the save key (7), the actions (6), in a row.
    for (const auto kind : { Kind::movement, Kind::echo, Kind::space })
    {
        INFO (osp::plugin::rack::name (kind));
        ui.setRackExpanded (kind, true);
        ui.refreshNow();
        auto* module = ui.effectsRack().module (kind);
        REQUIRE (module != nullptr);
        const auto browser = osp::plugin::rackSlot (*module, 3), save = osp::plugin::rackSlot (*module, 7), actions = osp::plugin::rackSlot (*module, 6);
        REQUIRE (! save.isEmpty());
        CHECK (save.getX() >= browser.getRight());
        CHECK (save.getRight() <= actions.getX());
        CHECK (save.getX() - browser.getRight() <= 8);
        CHECK (save.getBottom() <= module->titleOffset());
        CHECK (std::abs (save.getCentreY() - browser.getCentreY()) <= 1);
    }
    // An FX rack: the same order (its browser, the save key, the overflow key).
    andor::analog::openFxRack (ui, { fx::Type::compressor, 0 });
    ui.refreshNow();
    auto* rack = dynamic_cast<osp::plugin::rack::ParameterRack*> (ui.effectsRack().module (Kind::fx));
    REQUIRE (rack != nullptr);
    REQUIRE (rack->saveKey() != nullptr);
    REQUIRE (rack->railComponent() != nullptr);
    REQUIRE (rack->overflowKey() != nullptr);
    CHECK (rack->saveKey()->getX() >= rack->railComponent()->getRight());
    CHECK (rack->saveKey()->getRight() <= rack->overflowKey()->getX());
    CHECK (rack->saveKey()->getTitle() == "Save preset");
}

TEST_CASE ("analog: EQ review - the graphic EQ at full width with a live spectrum, 1448 and 869 (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-eq]")
{
    namespace fx = andor::analog::fx;
    using osp::plugin::rack::Kind;
    const auto* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
    fx::Graph g;
    g.custom = true;
    g.chain = { fx::Node::effect ({ fx::Type::eq, 0 }) };
    REQUIRE (engine.setFxRoute (g));
    // A musical curve: a low shelf up, a cut at 350 Hz, a presence lift, the high pass on.
    setValue (p, "analog.fx.eq.1.band1.gain", 4.0f);
    setValue (p, "analog.fx.eq.1.band2.gain", -4.5f);
    setValue (p, "analog.fx.eq.1.band3.gain", 5.0f);
    setValue (p, "analog.fx.eq.1.band3.q", 1.4f);
    setValue (p, "analog.fx.eq.1.hp.on", 1.0f);
    setValue (p, "analog.fx.eq.1.hp.freq", 60.0f);
    andor::analog::openFxRack (ui, { fx::Type::eq, 0 });
    for (const int width : { 1448, 869 })
    {
        renderShell (p, 0.35);   // (notes still sounding: the taps hold what played)
        editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
        ui.refreshNow();
        ui.effectsRack().settle();
        auto* module = ui.effectsRack().module (Kind::fx);
        REQUIRE (module != nullptr);
        CHECK (andor::analog::analyseFxEqSpectrum (*module) > -80.0f);
        auto save = [&] (const juce::Image& image, const juce::String& name) {
            const auto file = out.getChildFile (name + ".png");
            file.deleteFile();
            juce::FileOutputStream stream (file);
            juce::PNGImageFormat().writeImageToStream (image, stream);
        };
        auto& rack = ui.effectsRack();
        const auto r = editor->getLocalArea (&rack, rack.getLocalBounds());
        save (editor->createComponentSnapshot (r.expanded (0, 6).getIntersection (editor->getLocalBounds()), true, width < 1000 ? 2.0f : 1.0f), "eq-rack-" + juce::String (width));
        save (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), "eq-window-" + juce::String (width));
    }
}

TEST_CASE ("analog: the EQ rack is one full-width graphic - nodes drag the real parameters, overlays are bound, a live spectrum", "[analog][fx][rack][ui][eqgraphic]")
{
    namespace fx = andor::analog::fx;
    using osp::plugin::rack::Kind;
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
    auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
    fx::Graph g;
    g.custom = true;
    g.chain = { fx::Node::effect ({ fx::Type::eq, 0 }) };
    REQUIRE (engine.setFxRoute (g));
    andor::analog::openFxRack (ui, { fx::Type::eq, 0 });
    ui.refreshNow();
    auto* module = dynamic_cast<osp::plugin::rack::ParameterRack*> (ui.effectsRack().module (Kind::fx));
    REQUIRE (module != nullptr);
    auto* graphic = andor::analog::fxEqGraphic (*module);
    REQUIRE (graphic != nullptr);
    // The graphic IS the module: no knob cells, no pages; it spans the body under the rail.
    CHECK (module->captions().isEmpty());
    CHECK (graphic->getX() <= 20);
    CHECK (graphic->getRight() >= module->getWidth() - 20);
    CHECK (graphic->getY() >= module->titleOffset());
    CHECK (graphic->getBottom() <= module->getHeight());
    CHECK (module->saveKey() != nullptr);

    // Dragging band 3's node: its frequency and gain, one undoable gesture.
    p.apvts().copyState();   // (the parameters into the undo state, as the message loop's timer does)
    const auto before = allParameterValues (p);
    const auto from = andor::analog::fxEqNode (*module, 2);
    const auto to = from.translated (60.0f, -20.0f);
    graphic->mouseDown (mouseEventFor (*graphic, from));
    graphic->mouseDrag (mouseEventFor (*graphic, to));
    graphic->mouseUp (mouseEventFor (*graphic, to));
    p.apvts().copyState();
    CHECK (andor::analog::fxEqSelectedBand (*module) == 2);
    CHECK (p.apvts().getParameter ("analog.fx.eq.1.band3.freq")->getValue() > before.at ("analog.fx.eq.1.band3.freq"));
    CHECK (p.parameterValue ("analog.fx.eq.1.band3.gain") > 1.0f);
    const auto node = andor::analog::fxEqNode (*module, 2);
    CHECK (std::abs (node.x - to.x) < 2.0f);
    CHECK (std::abs (node.y - to.y) < 2.0f);
    p.undo().undo();   // (the drag: one step)
    CHECK (p.apvts().getParameter ("analog.fx.eq.1.band3.gain")->getValue() == Catch::Approx (before.at ("analog.fx.eq.1.band3.gain")).margin (1.0e-4));
    CHECK (p.apvts().getParameter ("analog.fx.eq.1.band3.freq")->getValue() == Catch::Approx (before.at ("analog.fx.eq.1.band3.freq")).margin (1.0e-4));

    // The high pass: off, taken by its node it comes on and moves (frequency only).
    REQUIRE (p.parameterValue ("analog.fx.eq.1.hp.on") < 0.5f);
    const auto hpFrom = andor::analog::fxEqNode (*module, 4);
    graphic->mouseDown (mouseEventFor (*graphic, hpFrom));
    graphic->mouseDrag (mouseEventFor (*graphic, hpFrom.translated (40.0f, 30.0f)));
    graphic->mouseUp (mouseEventFor (*graphic, hpFrom.translated (40.0f, 30.0f)));
    CHECK (p.parameterValue ("analog.fx.eq.1.hp.on") >= 0.5f);
    CHECK (p.apvts().getParameter ("analog.fx.eq.1.hp.freq")->getValue() > before.at ("analog.fx.eq.1.hp.freq"));
    // A double-click turns a pass filter off; on a band, its gain back to 0 dB.
    graphic->mouseDoubleClick (mouseEventFor (*graphic, andor::analog::fxEqNode (*module, 4)));
    CHECK (p.parameterValue ("analog.fx.eq.1.hp.on") < 0.5f);
    setValue (p, "analog.fx.eq.1.band2.gain", -6.0f);
    graphic->mouseDoubleClick (mouseEventFor (*graphic, andor::analog::fxEqNode (*module, 1)));
    CHECK (p.parameterValue ("analog.fx.eq.1.band2.gain") == Catch::Approx (0.0f).margin (1.0e-3));

    // The overlay fields are the same parameters (both ways): band 1 chosen (a click on its node).
    graphic->mouseDown (mouseEventFor (*graphic, andor::analog::fxEqNode (*module, 0)));
    graphic->mouseUp (mouseEventFor (*graphic, andor::analog::fxEqNode (*module, 0)));
    REQUIRE (andor::analog::fxEqSelectedBand (*module) == 0);
    int bound = 0;
    for (auto* c : graphic->getChildren())
        if (auto* f = dynamic_cast<juce::Slider*> (c))
        {
            const auto id = f->getProperties()["paramId"].toString();
            INFO (id.toStdString());
            REQUIRE (id.startsWith ("analog.fx.eq.1."));
            auto* param = p.apvts().getParameter (id);
            REQUIRE (param != nullptr);
            const double target = f->getMinimum() + 0.4 * (f->getMaximum() - f->getMinimum());
            f->setValue (target, juce::sendNotificationSync);
            CHECK (p.parameterValue (id) == Catch::Approx (f->getValue()).margin (1.0e-3));
            ++bound;
        }
    CHECK (bound == 5);   // (FREQ, GAIN, Q of the selected band; MIX; OUTPUT)

    // The spectrum: silent before anything plays; what plays through the EQ shows.
    CHECK (andor::analog::analyseFxEqSpectrum (*module) < -80.0f);
    renderShell (p, 0.35);
    CHECK (andor::analog::analyseFxEqSpectrum (*module) > -60.0f);
}

TEST_CASE ("analog: GRANULAR on cards A and B is an on / off key (the grain cloud); card C keeps its mode menu", "[analog][editor][granular][granular-toggle]")
{
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, 1086);
    ui.refreshNow();
    int toggles = 0, menus = 0;
    for (int layer = 0; layer < 3; ++layer)
    {
        auto* key = ui.engineCard (layer).modeSelector();
        INFO ("layer " << layer);
        REQUIRE (key != nullptr);
        if (! p.isGeneratorLayer (layer))
        {
            // Card C: ANDOR OSP's mode menu (One Shot / Granular as symbols).
            CHECK_FALSE (key->isToggle());
            CHECK (key->iconOnly());
            ++menus;
            continue;
        }
        ++toggles;
        REQUIRE (key->isToggle());
        CHECK_FALSE (key->iconOnly());
        CHECK (key->getWidth() <= 44);   // (a small key, as EQ beside it)
        const auto id = juce::String ("layer") + osp::plugin::InstrumentHost::layerName (layer) + ".sourceMode";
        auto* param = p.apvts().getParameter (id);
        REQUIRE (param != nullptr);
        CHECK (param->getValue() < 0.5f);
        CHECK (key->getTooltip().startsWith ("Granular: off"));
        // A click: on (one undoable gesture); again: off.
        p.undoManager.beginNewTransaction();
        key->mouseUp (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), key->getLocalBounds().getCentre().toFloat(), {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                        key, key, juce::Time::getCurrentTime(), key->getLocalBounds().getCentre().toFloat(), juce::Time::getCurrentTime(), 1, false));
        CHECK (param->getValue() >= 0.5f);
        CHECK (key->mode() == 1);
        CHECK (key->getTooltip().startsWith ("Granular: on"));
        CHECK (key->keyState() == osp::plugin::skin::KeyState::pressed);
        CHECK (key->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));
        CHECK (param->getValue() < 0.5f);
    }
    CHECK (toggles == 2);
    CHECK (menus == 1);
    if (const char* dir = std::getenv ("ANALOG_SCREENSHOT_DIR"))
    {
        const juce::File out (dir);
        out.createDirectory();
        auto shoot = [&] (const juce::String& name) {
            ui.refreshNow();
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            const auto file = out.getChildFile (name + ".png");
            file.deleteFile();
            juce::FileOutputStream stream (file);
            juce::PNGImageFormat().writeImageToStream (image, stream);
        };
        shoot ("granular-toggle-off");
        setValue (p, "layerA.sourceMode", 1.0f);
        shoot ("granular-toggle-a-on");
    }
}

//==============================================================================
// Release contracts (1.0): what a DAW sees and what a saved project holds.

namespace
{
    juce::File productSourceDir()
    {
        return juce::File (ANDOR_PRODUCT_SCHEMA_GOLDEN).getParentDirectory().getParentDirectory();
    }

    bool updatingGoldens()
    {
        const char* update = std::getenv ("OSP_UPDATE_GOLDEN");
        return update != nullptr && juce::String (update) == "1";
    }
}

TEST_CASE ("analog: a DAW may automate exactly the parameters the engine reads", "[analog][host][automation]")
{
    // The others (ANDOR OSP's schema has more than ANALOG plays) are proved inert by the
    // bindings test: hosts leave them out of their automation lists. IDs and order unchanged.
    ShellProcessor p;
    const auto bound = params::engineParameterIds();
    int automatable = 0, hidden = 0;
    for (auto* base : p.getParameters())
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (base);
        REQUIRE (param != nullptr);
        const auto id = param->getParameterID();
        INFO (id);
        const bool isBound = osp::plugin::binding::anyBound ({ id }, bound);
        CHECK (param->isAutomatable() == isBound);
        (isBound ? automatable : hidden) += 1;
    }
    CHECK (hidden > 100);
    CHECK (automatable > 500);
    WARN ("host parameters: " << automatable << " automatable, " << hidden << " not offered for automation");
}

TEST_CASE ("analog: the modulation destinations keep their places (sessions store the index)", "[analog][host][golden]")
{
    // A route's destination is saved as its index in the choice list: ANDOR OSP's own, then
    // ANALOG's in this order. Inserting or reordering one would re-route saved modulation.
    juce::StringArray lines;
    const auto& dests = andor::shell::productInfo().modulationDestinations;
    for (std::size_t k = 0; k < dests.size(); ++k)
        lines.add (juce::String (osp::mod::hostDestCount + static_cast<int> (k)) + " " + juce::String (dests[k].id) + " \""
                   + juce::String (dests[k].name) + "\" " + (dests[k].owner == osp::mod::Owner::global ? "global" : "voice"));
    const auto listing = lines.joinIntoString ("\n") + "\n";
    const auto golden = productSourceDir().getChildFile ("golden/modulation-destinations.txt");
    if (updatingGoldens())
        REQUIRE (golden.replaceWithText (listing, false, false, "\n"));
    REQUIRE (golden.existsAsFile());
    const auto expected = juce::StringArray::fromLines (golden.loadFileAsString().trimEnd());
    // Appending is allowed (a new destination at the end); nothing before it may change.
    REQUIRE (lines.size() >= expected.size());
    for (int i = 0; i < expected.size(); ++i)
    {
        INFO ("destination " << i);
        CHECK (lines[i] == expected[i]);
    }
}

namespace
{
    /** 16 windows' RMS of a render (both channels): a sound's shape, platform-tolerant. */
    std::vector<double> contourOf (const juce::AudioBuffer<float>& b)
    {
        std::vector<double> c;
        const int n = b.getNumSamples(), w = n / 16;
        for (int i = 0; i < 16; ++i)
        {
            double s = 0.0;
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                for (int k = i * w; k < (i + 1) * w; ++k)
                    s += static_cast<double> (b.getSample (ch, k)) * b.getSample (ch, k);
            c.push_back (std::sqrt (s / std::max (1, w * b.getNumChannels())));
        }
        return c;
    }

    juce::String valuesOf (ShellProcessor& p)
    {
        juce::StringArray lines;
        for (auto* base : p.getParameters())
            if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (base))
                lines.add (param->getParameterID() + " " + juce::String (param->getValue(), 6));
        return lines.joinIntoString ("\n") + "\n";
    }

    const juce::File fixtureDir() { return productSourceDir().getChildFile ("tests/fixtures/state-1.0"); }

    /** The sessions a 1.0 customer can have: a fresh instance, one factory sound per category,
        a modulated one (routes, the arpeggiator, KEY DNA) and a CUSTOM FX route. */
    std::vector<std::pair<juce::String, std::function<void (ShellProcessor&)>>> fixtureSessions()
    {
        std::vector<std::pair<juce::String, std::function<void (ShellProcessor&)>>> list;
        list.emplace_back ("default", [] (ShellProcessor&) {});
        std::set<std::string> categories;
        for (const auto& f : factoryPatches())
            if (categories.insert (f.category).second)
            {
                const juce::String name (f.name);
                list.emplace_back ("factory-" + juce::String (f.category).toLowerCase(), [name] (ShellProcessor& p) {
                    for (const auto& e : p.presetList())
                        if (e.name == name)
                            p.openPresetEntry (e);
                });
            }
        list.emplace_back ("modulated", [] (ShellProcessor& p) {
            const auto toneChoice = static_cast<float> (osp::mod::hostDestCount + 1);
            setValue (p, osp::plugin::InstrumentHost::modRouteId (0, "source"), 1.0f);
            setValue (p, osp::plugin::InstrumentHost::modRouteId (0, "dest"), toneChoice);
            setValue (p, osp::plugin::InstrumentHost::modRouteId (0, "depth"), 40.0f);
            setValue (p, "mod.lfo1.rate", 3.0f);
            setValue (p, "arp.enabled", 1.0f);
            setValue (p, "arp.rate", 3.0f);
            osp::keydna::Settings dna;
            dna.enabled = true;
            dna[osp::keydna::Param::position].variation = 0.5;
            p.setKeyDnaSettings (0, dna, "KEY DNA", false);
        });
        list.emplace_back ("fx-custom", [] (ShellProcessor& p) {
            namespace fx = andor::analog::fx;
            auto route = fx::classicGraph();
            route.custom = true;
            std::swap (route.chain[0], route.chain[1]);
            route.chain.push_back (fx::Node::effect ({ fx::Type::retro, 0 }));
            REQUIRE (dynamic_cast<AnalogEngine&> (p.productEngine()).setFxRoute (route));
            setValue (p, "space", 40.0f);
        });
        return list;
    }

    juce::AudioBuffer<float> renderLoaded (const juce::MemoryBlock& state)
    {
        ShellProcessor q;
        q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        q.pollLoads();
        return renderShell (q, 1.0);
    }
}

TEST_CASE ("analog: write the 1.0 session fixtures (run once, by hand, for a release)", "[.analog-write-fixtures]")
{
    fixtureDir().createDirectory();
    for (const auto& [name, make] : fixtureSessions())
    {
        ShellProcessor p;
        make (p);
        p.pollLoads();
        juce::MemoryBlock state;
        p.getStateInformation (state);
        REQUIRE (fixtureDir().getChildFile (name + ".state").replaceWithData (state.getData(), state.getSize()));
        ShellProcessor q;
        q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        REQUIRE (fixtureDir().getChildFile (name + ".values.txt").replaceWithText (valuesOf (q), false, false, "\n"));
        juce::StringArray contour;
        for (const double v : contourOf (renderLoaded (state)))
            contour.add (juce::String (v, 8));
        REQUIRE (fixtureDir().getChildFile (name + ".contour.txt").replaceWithText (contour.joinIntoString ("\n") + "\n", false, false, "\n"));
    }
}

TEST_CASE ("analog: projects saved by 1.0 open with the same settings and the same sound", "[analog][state][fixtures]")
{
    // Real state saved by the 1.0 build (tests/fixtures/state-1.0, written by the hidden test
    // above), not state made by today's build: a change in how sessions are read shows here.
    const auto sessions = fixtureSessions();
    REQUIRE (fixtureDir().isDirectory());
    for (const auto& [name, make] : sessions)
    {
        INFO (name);
        juce::MemoryBlock state;
        REQUIRE (fixtureDir().getChildFile (name + ".state").loadFileAsData (state));
        ShellProcessor q;
        q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        // Every parameter as saved (a parameter appended later must be absent from the file:
        // then it rests at its default, which is what the listing holds for it).
        const auto expected = juce::StringArray::fromLines (fixtureDir().getChildFile (name + ".values.txt").loadFileAsString().trimEnd());
        const auto actual = juce::StringArray::fromLines (valuesOf (q).trimEnd());
        REQUIRE (actual.size() >= expected.size());
        for (int i = 0; i < expected.size(); ++i)
        {
            INFO (expected[i]);
            CHECK (actual[i].upToFirstOccurrenceOf (" ", false, false) == expected[i].upToFirstOccurrenceOf (" ", false, false));
            CHECK (actual[i].fromFirstOccurrenceOf (" ", false, false).getDoubleValue()
                   == Approx (expected[i].fromFirstOccurrenceOf (" ", false, false).getDoubleValue()).margin (2.0e-6));
        }
        if (name == "fx-custom")
            CHECK (dynamic_cast<AnalogEngine&> (q.productEngine()).fxRoute().custom);
        // The same sound (16 windows' loudness within 0.5 %: platform-tolerant).
        const auto contour = contourOf (renderLoaded (state));
        const auto wanted = juce::StringArray::fromLines (fixtureDir().getChildFile (name + ".contour.txt").loadFileAsString().trimEnd());
        REQUIRE (wanted.size() == static_cast<int> (contour.size()));
        for (std::size_t i = 0; i < contour.size(); ++i)
            CHECK (contour[i] == Approx (wanted[static_cast<int> (i)].getDoubleValue()).epsilon (0.005).margin (1.0e-6));
    }
}

//==============================================================================
// V6 gate 0: the 1.0 baseline, frozen. Every factory sound through the whole instrument (voice,
// CLASSIC FX, output), a macro-heavy CLASSIC state and a CUSTOM route through every effect, at
// 44.1 and 48 kHz: an exact hash per platform and a 16-window RMS contour for every other. The
// Cinematic V6 work (independent REIMAGINED, SUB, C2, MAIN / SEND) must leave every one of these
// renders as it is while its own features are off. OSP_UPDATE_GOLDEN=1 records (1.0 only).

namespace
{
    juce::File v6BaselinePath() { return productSourceDir().getChildFile ("golden/v6-baseline-renders.txt"); }

    std::vector<std::pair<juce::String, std::function<void (ShellProcessor&)>>> v6BaselineStates()
    {
        namespace fx = andor::analog::fx;
        std::vector<std::pair<juce::String, std::function<void (ShellProcessor&)>>> states;
        ShellProcessor probe;
        for (const auto& entry : probe.presetList())   // (the factory sounds only: not a musician's own)
            if (std::any_of (factoryPatches().begin(), factoryPatches().end(), [&entry] (const auto& f) { return entry.name == juce::String (f.name); }))
                states.push_back ({ "preset/" + entry.name, [entry] (ShellProcessor& q) { q.openPresetEntry (entry); } });
        states.push_back ({ "classic-macros", [] (ShellProcessor& q) {
                               setValue (q, "drive", 35.0f);
                               setValue (q, "echo", 40.0f);
                               setValue (q, "space", 30.0f);
                               setValue (q, "motion", 45.0f);
                           } });
        states.push_back ({ "custom-all", [] (ShellProcessor& q) {
                               setValue (q, "echo", 40.0f);
                               setValue (q, "space", 30.0f);
                               fx::Graph g;
                               g.custom = true;
                               for (int t = 0; t < fx::typeCount; ++t)
                                   g.chain.push_back (fx::Node::effect ({ static_cast<fx::Type> (t), 0 }));
                               REQUIRE (dynamic_cast<AnalogEngine&> (q.productEngine()).setFxRoute (g));
                           } });
        return states;
    }

    /** renderShell at a sample rate of its own (the phrase's times scaled from 48 kHz). */
    juce::AudioBuffer<float> renderShellAt (ShellProcessor& p, double sampleRate, double seconds)
    {
        p.prepareToPlay (sampleRate, block);
        const int total = static_cast<int> (seconds * sampleRate);
        const auto at = [sampleRate] (int sample48) { return static_cast<int> (std::llround (sample48 * sampleRate / 48000.0)); };
        juce::AudioBuffer<float> out (2, total), buffer (2, block);
        for (int pos = 0; pos < total; pos += block)
        {
            juce::MidiBuffer midi;
            for (const auto& n : phrase)
                if (const int s = at (n.sample); s >= pos && s < pos + block)
                    midi.addEvent (n.velocity > 0 ? juce::MidiMessage::noteOn (1, n.note, static_cast<juce::uint8> (n.velocity))
                                                  : juce::MidiMessage::noteOff (1, n.note),
                                   s - pos);
            if (const int s = at (6000); s >= pos && s < pos + block)
                midi.addEvent (juce::MidiMessage::pitchWheel (1, 12000), s - pos);
            p.processBlock (buffer, midi);
            const int n = std::min (block, total - pos);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, buffer, ch, 0, n);
        }
        return out;
    }

    juce::String v6HashOf (const juce::AudioBuffer<float>& b)
    {
        std::uint64_t h = 1469598103934665603ull;   // (FNV-1a over the samples' bits)
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float x = b.getSample (ch, i);
                std::uint32_t bits;
                std::memcpy (&bits, &x, sizeof bits);
                h = (h ^ bits) * 1099511628211ull;
            }
        return juce::String::toHexString (static_cast<juce::int64> (h)).paddedLeft ('0', 16);
    }
}

TEST_CASE ("analog: V6 gate 0 - every factory sound and FX route renders as at 1.0 (hash per platform, contour elsewhere)", "[analog][legacy][v6-baseline]")
{
    const auto platform = juce::String (osp::platformTag());
    const bool update = updatingGoldens();
    std::map<juce::String, juce::String> stored;   // "kind key" -> line
    for (const auto& line : juce::StringArray::fromLines (v6BaselinePath().loadFileAsString()))
    {
        const auto parts = juce::StringArray::fromTokens (line, " ", "");
        if (parts.size() >= 2)
            stored[parts[0] + " " + parts[1]] = line;
    }
    juce::StringArray written;
    int compared = 0, hashed = 0;
    for (const double r : { 44100.0, 48000.0 })
    {
        for (const auto& [name, setUp] : v6BaselineStates())
        {
            const auto key = (name + "@" + juce::String (static_cast<int> (r))).replaceCharacter (' ', '_');
            INFO (key);
            ShellProcessor q;
            setUp (q);
            const auto out = renderShellAt (q, r, 1.0);
            const auto contour = contourOf (out);
            const auto hash = v6HashOf (out);
            if (update)
            {
                juce::String line = "rms " + key;
                for (const double v : contour)
                    line << " " << juce::String (v, 9);
                written.add (line);
                for (const auto& [k, l] : stored)   // (other platforms' hashes kept)
                    if (k.startsWith ("hash:") && k.endsWith (" " + key) && ! k.startsWith ("hash:" + platform + " "))
                        written.add (l);
                written.add ("hash:" + platform + " " + key + " " + hash);
                continue;
            }
            const auto rms = stored.find ("rms " + key);
            REQUIRE (rms != stored.end());
            const auto wanted = juce::StringArray::fromTokens (rms->second, " ", "");
            REQUIRE (wanted.size() == 2 + static_cast<int> (contour.size()));
            for (std::size_t i = 0; i < contour.size(); ++i)
                CHECK (contour[i] == Approx (wanted[2 + static_cast<int> (i)].getDoubleValue()).epsilon (0.005).margin (1.0e-6));
            ++compared;
            if (const auto h = stored.find ("hash:" + platform + " " + key); h != stored.end())
            {
                CHECK (h->second.endsWith (" " + hash));   // (the same platform: bit for bit)
                ++hashed;
            }
        }
    }
    if (update)
    {
        REQUIRE (v6BaselinePath().replaceWithText (written.joinIntoString ("\n") + "\n", false, false, "\n"));
        return;
    }
    CHECK (compared == 2 * (static_cast<int> (factoryPatches().size()) + 2));   // (every factory sound + 2 routes, at two rates)
    // Exact hashes where this platform's were recorded (all of them, then); elsewhere the
    // contours alone, as the legacy goldens do (a different compiler rounds differently).
    INFO ("hashes compared on " << platform << ": " << hashed);
    if (hashed == 0)
        WARN ("no exact hashes stored for " << platform << ": loudness contours compared only");
    else
        CHECK (hashed == compared);
}

TEST_CASE ("analog: V6 gate 1 - card B has its own REIMAGINED (layerC.reimagined*); older states give both cards theirs; VELO in B's well", "[analog][v6][reimagined]")
{
    using Host = osp::plugin::InstrumentHost;
    const auto ampB = Host::reimaginedParameterId (2), modeB = Host::reimaginedModeParameterId (2, "mode");
    REQUIRE (ampB == "layerC.reimagined");

    // The engine reads card B's own amount and mode (bound: automatable, its popover opens).
    {
        ShellProcessor p;
        CHECK (osp::plugin::binding::anyBound ({ ampB }, p.boundParameters()));
        CHECK (osp::plugin::binding::anyBound ({ modeB }, p.boundParameters()));
        CHECK (osp::plugin::binding::anyBound ({ Host::reimaginedModeParameterId (2, "toybox.motion") }, p.boundParameters()));
    }

    // A state from before (version 13): one REIMAGINED for the pair becomes both cards'.
    juce::MemoryBlock session;
    {
        ShellProcessor old;
        setValue (old, "reimagined", 40.0f);
        setValue (old, Host::reimaginedModeParameterId (0, "mode"), 2.0f);   // TOYBOX
        setValue (old, Host::reimaginedModeParameterId (0, "toybox.motion"), 70.0f);
        setValue (old, ampB, 0.0f);
        old.getStateInformation (session);
    }
    auto xml = juce::AudioProcessor::getXmlFromBinary (session.getData(), static_cast<int> (session.getSize()));
    REQUIRE (xml != nullptr);
    CHECK (xml->getIntAttribute ("productVersion") == 17);
    xml->setAttribute ("productVersion", 13);   // as 1.0 wrote it
    juce::MemoryBlock v13;
    juce::AudioProcessor::copyXmlToBinary (*xml, v13);
    {
        ShellProcessor q;
        q.setStateInformation (v13.getData(), static_cast<int> (v13.getSize()));
        CHECK (q.parameterValue ("reimagined") == Approx (40.0f).margin (0.01f));
        CHECK (q.parameterValue (ampB) == Approx (40.0f).margin (0.01f));
        CHECK (juce::roundToInt (q.parameterValue (modeB)) == 2);
        CHECK (q.parameterValue (Host::reimaginedModeParameterId (2, "toybox.motion")) == Approx (70.0f).margin (0.01f));
    }
    // A version 14 state keeps the cards apart.
    {
        ShellProcessor q;
        q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
        CHECK (q.parameterValue ("reimagined") == Approx (40.0f).margin (0.01f));
        CHECK (q.parameterValue (ampB) == Approx (0.0f).margin (0.01f));
    }

    // The cards: B's fifth knob is its REIMAGINED; its VELO is in its well, every model.
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, 1086);
    ui.refreshNow();
    auto hasControlFor = [] (juce::Component& root, const juce::String& id, bool outsideWell = false) {
        bool found = false;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
            if (outsideWell && &c != &root && dynamic_cast<osp::plugin::WellOverlay*> (&c) != nullptr)
                return;   // (the well's own controls)
            if (osp::plugin::binding::tagsOf (c).contains (id) || c.getProperties()["paramId"].toString() == id)
                found = true;
            for (auto* child : c.getChildren())
                walk (*child);
        };
        walk (root);
        return found;
    };
    CHECK (hasControlFor (ui.engineCard (2), ampB, true));
    CHECK_FALSE (hasControlFor (ui.engineCard (2), params::osc2Velo, true));   // (not in the knob row: in the well)
    CHECK (hasControlFor (ui.engineCard (0), "reimagined"));
    for (const float model : { 0.0f, 1.0f, 2.0f })   // CLASSIC, DCO, WIDE
    {
        INFO ("OSC 2 model " << model);
        setValue (p, params::osc2Model, model);
        auto overlay = andor::analog::createOscillatorOverlay (p, 2, 2);
        overlay->setBounds (0, 0, 420, 260);
        CHECK (hasControlFor (*overlay, params::osc2Velo));
        auto overlayA = andor::analog::createOscillatorOverlay (p, 1, 0);
        CHECK_FALSE (hasControlFor (*overlayA, params::osc2Velo));
    }
}

TEST_CASE ("analog: V6 gate 1 screenshots - each oscillator card's own REIMAGINED, B's popover, B's well with VELO (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-v6-g1]")
{
    using Host = osp::plugin::InstrumentHost;
    const char* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    for (const int width : { 1448, 869 })
    {
        ShellProcessor p;
        setValue (p, "reimagined", 40.0f);   // A: KALEIDOSCOPE 40 %
        setValue (p, Host::reimaginedParameterId (2), 70.0f);   // B: TAPE FRAME 70 %
        setValue (p, Host::reimaginedModeParameterId (2, "mode"), 1.0f);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
        auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
        editor->setSize (width, juce::roundToInt (static_cast<float> (width) * 1086.0f / 1448.0f));
        auto shoot = [&] (const juce::String& name) {
            ui.refreshNow();
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            const auto file = out.getChildFile (name + "-" + juce::String (width) + ".png");
            file.deleteFile();
            juce::FileOutputStream stream (file);
            juce::PNGImageFormat().writeImageToStream (image, stream);
        };
        shoot ("g1-1-cards");
        ui.openPopup (andor::shell::ShellEditor::reimaginedPopup + 2);   // B's modes, above its REIMAGINED name
        shoot ("g1-2-b-popover");
        ui.closePopup();
        if (auto* overlay = ui.engineCard (2).wellOverlayComponent())
        {
            overlay->setRevealedForTest (true);
            shoot ("g1-3-b-well-velo");
            setValue (p, params::osc2Model, 1.0f);   // DCO: a full row, VELO the sixth
            ui.refreshNow();
            if (auto* dco = ui.engineCard (2).wellOverlayComponent())
                dco->setRevealedForTest (true);
            shoot ("g1-4-b-well-dco-velo");
        }
    }
}

//==============================================================================
// Cinematic V6 gate 2: SUB in the arpeggiator's housing (docs/CINEMATIC_V6_GATE2.md).

TEST_CASE ("analog: SUB plays the keys held under the arpeggiator, not its notes (SUB-01, through the plugin)", "[v6][sub]")
{
    ShellProcessor p;
    setValue (p, "arp.enabled", 1.0f);
    setValue (p, params::subEnabled, 1.0f);
    p.prepareToPlay (rate, block);
    juce::AudioBuffer<float> buffer (2, block);
    const auto& synth = dynamic_cast<AnalogEngine&> (p.productEngine()).synthForTest();
    int fewest = 99, most = 0;
    for (int b = 0; b < 60; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0)
            for (const int key : { 48, 52, 55 })
                midi.addEvent (juce::MidiMessage::noteOn (1, key, static_cast<juce::uint8> (100)), 0);
        p.processBlock (buffer, midi);
        if (b > 4)
        {
            fewest = std::min (fewest, synth.sub().activeVoices());
            most = std::max (most, synth.sub().activeVoices());
        }
    }
    // The arpeggiator plays one note at a time; SUB holds the three keys the whole while.
    CHECK (fewest == 3);
    CHECK (most == 3);
    std::vector<int> keys;
    for (int i = 0; i < SubOsc::maxVoices; ++i)
        if (synth.sub().voiceKey (i) >= 0)
            keys.push_back (synth.sub().voiceKey (i));
    std::sort (keys.begin(), keys.end());
    CHECK (keys == std::vector<int> { 48, 52, 55 });
    int sounding = 0;
    for (int s = 0; s < Synth::maxVoices; ++s)
        sounding += synth.voiceView (s).held ? 1 : 0;
    CHECK (sounding <= 1);   // (the arpeggiator's one note)

    // The keys come up: SUB lets go.
    juce::MidiBuffer up;
    for (const int key : { 48, 52, 55 })
        up.addEvent (juce::MidiMessage::noteOff (1, key), 0);
    p.processBlock (buffer, up);
    CHECK (synth.sub().activeVoices() == 0);
}

TEST_CASE ("analog: SUB's row in the arpeggiator's housing: its light switches SUB, the row opens its popover; ANDOR OSP's housing unchanged", "[v6][sub]")
{
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, 1086);
    auto& housing = ui.arpHousing();
    REQUIRE (housing.hasSecondRow());
    // Two rows of the same height, no inset; the housing keeps its size (105 x 64 at 1448).
    CHECK (std::abs (housing.secondRowArea().getHeight() - 0.5f * static_cast<float> (housing.getHeight())) < 0.51f);
    CHECK (housing.getWidth() <= 110);

    auto click = [&] (juce::Point<float> at) {
        const auto pos = at;
        juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &housing, &housing,
                            juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
        housing.mouseDown (e);
    };
    // The lower light: SUB on, then off (arp untouched).
    CHECK (p.parameterValue (params::subEnabled) < 0.5f);
    click (housing.secondLightArea().getCentre());
    CHECK (p.parameterValue (params::subEnabled) > 0.5f);
    CHECK (housing.isSecondRowOn());
    CHECK (p.parameterValue ("arp.enabled") < 0.5f);
    click (housing.secondLightArea().getCentre());
    CHECK (p.parameterValue (params::subEnabled) < 0.5f);
    // The upper light is still the arpeggiator's.
    click (housing.lightArea().getCentre());
    CHECK (p.parameterValue ("arp.enabled") > 0.5f);
    CHECK (p.parameterValue (params::subEnabled) < 0.5f);

    // The rest of the lower row: SUB's popover, above the housing; the same press as the
    // outside-click watch sees it keeps it open; again: closed.
    const auto rowPoint = housing.secondRowArea().getCentre();
    click (rowPoint);
    REQUIRE (ui.openPopupIndex() == andor::shell::ShellEditor::arpCompanionPopup);
    REQUIRE (ui.openPopupPanel() != nullptr);
    CHECK (ui.openPopupPanel()->getBoundsInParent().getBottom() - osp::plugin::MiniPanel::shadowMargin <= housing.getY());
    click (rowPoint);
    CHECK (ui.openPopupIndex() == -1);

    // The popover's RANGE: dragging the low handle moves analog.sub.lowKey (never past the high).
    click (rowPoint);
    REQUIRE (ui.openPopupPanel() != nullptr);
    // (V7: the range lives on the display's keyboard - SubPanel.h.)
    auto* subPanel = dynamic_cast<andor::analog::SubPanel*> (ui.openPopupPanel());
    REQUIRE (subPanel != nullptr);
    auto& bed = subPanel->display();
    CHECK (juce::roundToInt (p.parameterValue (params::subLowKey)) == 48);
    auto drag = [&] (juce::Point<float> from, float toX) {
        const float y = bed.keybedArea().getCentreY();
        auto event = [&] (juce::Point<float> at) {
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &bed, &bed,
                                     juce::Time::getCurrentTime(), from, juce::Time::getCurrentTime(), 1, false);
        };
        bed.mouseDown (event (from));
        bed.mouseDrag (event ({ toX, y }));
        bed.mouseUp (event ({ toX, y }));
    };
    using Handle = andor::analog::SubResponseDisplay::Handle;
    drag (bed.handlePosition (Handle::low).translated (0.0f, 6.0f), bed.keyArea (36).getCentreX());   // to C2
    CHECK (juce::roundToInt (p.parameterValue (params::subLowKey)) == 36);
    drag (bed.handlePosition (Handle::low).translated (0.0f, 6.0f), bed.keyArea (107).getCentreX());   // past the high key: stops at it
    CHECK (juce::roundToInt (p.parameterValue (params::subLowKey)) == juce::roundToInt (p.parameterValue (params::subHighKey)));
    ui.closePopup();

    // The reference rendering (ANDOR OSP's own housing): no second row.
    {
        ShellProcessor reference;
        reference.showReferenceMacros();
        std::unique_ptr<juce::AudioProcessorEditor> plain (reference.createEditorIfNeeded());
        CHECK (! dynamic_cast<andor::shell::ShellEditor&> (*plain).arpHousing().hasSecondRow());
    }
}

TEST_CASE ("analog: V6 gate 2 screenshots - the arp / sub housing and SUB's popover (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-v6-g2]")
{
    const char* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    for (const int width : { 1448, 869 })
    {
        ShellProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
        auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
        editor->setSize (width, juce::roundToInt (static_cast<float> (width) * 1086.0f / 1448.0f));
        auto shoot = [&] (const juce::String& name) {
            ui.refreshNow();
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            const auto file = out.getChildFile (name + "-" + juce::String (width) + ".png");
            file.deleteFile();
            juce::FileOutputStream stream (file);
            juce::PNGImageFormat().writeImageToStream (image, stream);
            // The housing alone, larger.
            const auto housing = ui.getLocalArea (&ui.arpHousing(), ui.arpHousing().getLocalBounds()).expanded (24);
            const auto crop = editor->createComponentSnapshot (housing, true, 3.0f);
            const auto cropFile = out.getChildFile (name + "-housing-" + juce::String (width) + ".png");
            cropFile.deleteFile();
            juce::FileOutputStream cropStream (cropFile);
            juce::PNGImageFormat().writeImageToStream (crop, cropStream);
        };
        shoot ("g2-1-housing-off");
        setValue (p, "arp.enabled", 1.0f);
        setValue (p, params::subEnabled, 1.0f);
        shoot ("g2-2-housing-on");
        ui.openPopup (andor::shell::ShellEditor::arpCompanionPopup);
        shoot ("g2-3-sub-popover");
        ui.closePopup();
        setValue (p, params::subResponse, 0.0f);   // FLAT: the TAPER row folds away
        setValue (p, params::subWave, 3.0f);
        setValue (p, params::subLowKey, 36.0f);
        setValue (p, params::subHighKey, 72.0f);
        ui.openPopup (andor::shell::ShellEditor::arpCompanionPopup);
        shoot ("g2-4-sub-popover-flat");
        ui.closePopup();
    }
}

//==============================================================================
// Cinematic V6 gate 3: card C's second layer (C2), inside card C (docs/CINEMATIC_V6_GATE3.md).

namespace
{
    juce::Component* findTitled (juce::Component& root, const juce::String& title)
    {
        if (root.getTitle() == title)
            return &root;
        for (auto* c : root.getChildren())
            if (auto* found = findTitled (*c, title))
                return found;
        return nullptr;
    }

    void clickAt (juce::Component& c, juce::Point<float> at)
    {
        juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c,
                            juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false);
        c.mouseUp (e);
    }

    juce::StringArray cardCKnobs (ShellProcessor& p)
    {
        juce::StringArray ids;
        for (const auto& k : p.product().cardControls (1, p))
            ids.add (k.parameterId);
        return ids;
    }
}

TEST_CASE ("analog: card C's second layer - off by default, + adds it, 1 / 2 rebind the well and the knob row, x removes it (C2-03, UI)", "[v6][hybrid2]")
{
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    const auto oneLayerKnobs = cardCKnobs (p);
    CHECK (andor::analog::hybridLayerShown (p) == 1);
    CHECK (andor::analog::hybridOverlayKey (p) == "hybrid/0/empty");   // (exactly the key it always had)
    CHECK (p.parameterValue (params::hybrid2Enabled) < 0.5f);

    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, 1086);
    ui.refreshNow();
    auto layerKeys = [&ui]() -> juce::Component* {
        auto* overlay = ui.engineCard (1).wellOverlayComponent();
        return overlay != nullptr ? findTitled (*overlay, "Hybrid layers") : nullptr;
    };
    REQUIRE (layerKeys() != nullptr);
    CHECK (layerKeys()->getWidth() == 22);   // one key: +
    CHECK (layerKeys()->getParentComponent() != ui.engineCard (1).wellOverlayComponent());   // (in the revealed layer: shown on hover only)

    // + : the second layer on, shown; the knob row its own.
    clickAt (*layerKeys(), { 11.0f, 11.0f });
    CHECK (p.parameterValue (params::hybrid2Enabled) > 0.5f);
    CHECK (andor::analog::hybridLayerShown (p) == 2);
    CHECK (cardCKnobs (p) == juce::StringArray { params::hybrid2Fine, params::hybrid2Coarse, params::hybrid2Octave, params::hybrid2Level, params::hybrid2Velo });
    ui.refreshNow();
    REQUIRE (layerKeys() != nullptr);
    CHECK (layerKeys()->getWidth() == 3 * 22 + 2 * 4);   // 1, 2, x
    CHECK (layerKeys()->getParentComponent() == ui.engineCard (1).wellOverlayComponent());   // (always shown with two layers)

    // 1 : the first layer's, exactly as before.
    clickAt (*layerKeys(), { 11.0f, 11.0f });
    CHECK (andor::analog::hybridLayerShown (p) == 1);
    CHECK (cardCKnobs (p) == oneLayerKnobs);
    ui.refreshNow();
    CHECK (layerKeys()->getWidth() == 2 * 22 + 4);   // 1, 2
    // 2 then x : removed, back to one layer (its settings kept for next time).
    clickAt (*layerKeys(), { 37.0f, 11.0f });
    CHECK (andor::analog::hybridLayerShown (p) == 2);
    setValue (p, params::hybrid2Type, 2.0f);   // NOISE: its knob row
    CHECK (cardCKnobs (p)[0] == params::hybrid2NoiseTone);
    ui.refreshNow();
    clickAt (*layerKeys(), { 63.0f, 11.0f });
    CHECK (p.parameterValue (params::hybrid2Enabled) < 0.5f);
    CHECK (andor::analog::hybridLayerShown (p) == 1);
    CHECK (cardCKnobs (p) == oneLayerKnobs);
    CHECK (juce::roundToInt (p.parameterValue (params::hybrid2Type)) == 2);
}

TEST_CASE ("analog: card C's second layer's own recording - rooted at its pitch, kept apart from C1's, saved and recalled, heard", "[v6][hybrid2]")
{
    Scratch scratch;
    const auto file = writeRecording (scratch.dir, "layer2.wav", 57, 1.5);
    const auto store = scratch.dir.getChildFile ("Imported");
    const auto made = andor::analog::import::makeFileDefinition (file, store);
    REQUIRE (made.xml != nullptr);
    const auto* zone = made.xml->getChildByName ("Zone");
    REQUIRE (zone != nullptr);
    CHECK (std::abs (zone->getDoubleAttribute ("root") - 57.0) < 0.3);   // (A3: found by its pitch)
    CHECK (store.getChildFile (zone->getStringAttribute ("asset")).existsAsFile());

    juce::MemoryBlock session;
    {
        ShellProcessor p;
        auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
        engine.setHybrid2Definition (std::make_unique<juce::XmlElement> (*made.xml), store);
        REQUIRE (engine.waitForHybrid2 (10000));
        CHECK (engine.hybrid2State().loaded);
        CHECK (engine.hybrid2State().missing.isEmpty());
        CHECK (engine.sampleSourceDefinition() == nullptr);   // (C1's own untouched)
        setValue (p, params::hybrid2Enabled, 1.0f);
        setValue (p, params::hybrid2Type, 0.0f);   // SAMPLE
        setValue (p, "layerA.level", -48.0f);   // (the oscillators out: card C heard alone)
        const auto heard = renderShell (p, 0.5, { { 0, 57, 100 } }, false);
        CHECK (rmsOf (heard) > 1.0e-3);
        setValue (p, params::hybrid2Enabled, 0.0f);
        CHECK (rmsOf (renderShell (p, 0.5, { { 0, 57, 100 } }, false)) < 0.2 * rmsOf (heard));
        setValue (p, params::hybrid2Enabled, 1.0f);
        p.getStateInformation (session);
    }
    {
        ShellProcessor q;
        q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
        auto& engine = dynamic_cast<AnalogEngine&> (q.productEngine());
        const auto recalled = engine.hybrid2Definition();
        REQUIRE (recalled != nullptr);
        CHECK (recalled->isEquivalentTo (made.xml.get(), false));
        CHECK (q.parameterValue (params::hybrid2Enabled) > 0.5f);
    }
}

TEST_CASE ("analog: V6 gate 3 screenshots - card C with one layer, two, and its second shown (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-v6-g3]")
{
    const char* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    for (const int width : { 1448, 869 })
    {
        ShellProcessor p;
        setValue (p, params::hybridType, 1.0f);   // C1: OSC 3
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
        auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
        editor->setSize (width, juce::roundToInt (static_cast<float> (width) * 1086.0f / 1448.0f));
        auto shoot = [&] (const juce::String& name, bool revealed) {
            ui.refreshNow();
            if (auto* overlay = ui.engineCard (1).wellOverlayComponent())
                overlay->setRevealedForTest (revealed);
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            const auto file = out.getChildFile (name + "-" + juce::String (width) + ".png");
            file.deleteFile();
            juce::FileOutputStream stream (file);
            juce::PNGImageFormat().writeImageToStream (image, stream);
            const auto card = ui.getLocalArea (&ui.engineCard (1), ui.engineCard (1).getLocalBounds());
            const auto crop = editor->createComponentSnapshot (card, true, 2.0f);
            const auto cropFile = out.getChildFile (name + "-card-" + juce::String (width) + ".png");
            cropFile.deleteFile();
            juce::FileOutputStream cropStream (cropFile);
            juce::PNGImageFormat().writeImageToStream (crop, cropStream);
        };
        shoot ("g3-1-one-layer", false);
        shoot ("g3-2-one-layer-hover", true);
        setValue (p, params::hybrid2Enabled, 1.0f);
        andor::analog::showHybridLayer (p, 2);
        setValue (p, params::hybrid2Type, 2.0f);   // C2: NOISE
        shoot ("g3-3-layer2-noise", true);
        setValue (p, params::hybrid2Type, 1.0f);   // C2: an oscillator, WIDE
        setValue (p, params::hybrid2Model, 2.0f);
        shoot ("g3-4-layer2-osc", true);
        andor::analog::showHybridLayer (p, 1);
        shoot ("g3-5-two-layers-1-shown", false);
        andor::analog::showHybridLayer (p, 2);
        setValue (p, params::hybrid2Type, 0.0f);   // C2: SAMPLE, empty
        shoot ("g3-6-layer2-sample-empty", false);
    }
}

//==============================================================================
// Cinematic V6 gate 4: MAIN + SEND (docs/CINEMATIC_V6_GATE4.md) - opt-in only.

TEST_CASE ("analog: MAIN + SEND - only by the musician's choice; tabs, the mixer's sends, SEND for spatial effects, back to one path", "[v6][mainsend][ui]")
{
    namespace fx = andor::analog::fx;
    ShellProcessor p;
    auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, 1086);
    ui.setSourceViewShown (true);
    auto* view = dynamic_cast<andor::analog::FxRoutingView*> (ui.sourceViewComponent());
    REQUIRE (view != nullptr);
    CHECK (! engine.fxRoute().custom);   // (CLASSIC: opening the view changes nothing)
    view->setTab (1);
    CHECK (view->tab() == 0);   // (no tabs on one path)

    // The explicit conversion: CLASSIC's inserts on MAIN, ECHO || SPACE on SEND, card C at 25 %.
    REQUIRE (view->useMainSend());
    const auto route = engine.fxRoute();
    CHECK (route.mainSend);
    CHECK (fx::mainText (route) == "v1 custom fx drive.1 on fx movement.1 on");
    CHECK (std::abs (p.parameterValue (params::sendC) - 25.0f) < 0.01f);
    CHECK (p.parameterValue (params::sendA) == 0.0f);
    CHECK (view->tiles().size() == 2);   // MAIN's two

    // SEND: its chain on the board, the mixer's knobs the cards' sends.
    view->setTab (1);
    CHECK (view->tab() == 1);
    CHECK (view->levelKnob (2)->getTitle() == "Card C send");
    CHECK (view->tiles().size() == 2);   // ECHO || SPACE
    view->setTab (0);
    CHECK (view->levelKnob (2)->getTitle() == "Card C level");

    // A spatial effect added on MAIN goes on SEND; an insert stays on MAIN.
    REQUIRE (view->insertNew (fx::Type::convolution));
    CHECK (view->tab() == 1);
    CHECK (fx::contains (fx::Graph { true, engine.fxRoute().send, false, {} }, { fx::Type::convolution, 0 }));
    view->setTab (0);
    REQUIRE (view->insertNew (fx::Type::eq));
    CHECK (fx::contains (fx::Graph { true, engine.fxRoute().chain, false, {} }, { fx::Type::eq, 0 }));
    // OVERVIEW edits nothing.
    view->setTab (2);
    const auto before = engine.fxRoute();
    CHECK (! view->moveTo ({ fx::Type::drive, 0 }, {}, 0));
    CHECK (engine.fxRoute() == before);

    // Back to one path: MAIN then SEND in series, CUSTOM; the tabs gone.
    REQUIRE (view->useSinglePath());
    CHECK (! engine.fxRoute().mainSend);
    CHECK (engine.fxRoute().custom);
    CHECK (view->tab() == 0);
    // ... undo returns MAIN + SEND.
    p.undo().undo();
    CHECK (engine.fxRoute().mainSend);
    ui.setSourceViewShown (false);
}

TEST_CASE ("analog: MAIN + SEND - saved as schema 2 and recalled; CLASSIC and CUSTOM sessions stay schema 1, never upgraded", "[v6][mainsend][state]")
{
    namespace fx = andor::analog::fx;
    auto stateOf = [] (ShellProcessor& p) {
        juce::MemoryBlock block;
        p.getStateInformation (block);
        return block;
    };
    // A CUSTOM route (schema 1) stays one path.
    juce::MemoryBlock custom;
    {
        ShellProcessor p;
        auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
        auto g = fx::classicGraph();
        g.custom = true;
        REQUIRE (engine.setFxRoute (g));
        setValue (p, params::sendC, 60.0f);   // (a send set, heard nowhere: one path)
        custom = stateOf (p);
        auto xml = juce::AudioProcessor::getXmlFromBinary (custom.getData(), static_cast<int> (custom.getSize()));
        const auto* routing = findElement (*xml, "FxRouting");
        REQUIRE (routing != nullptr);
        CHECK (routing->getIntAttribute ("schema") == 1);
    }
    {
        ShellProcessor q;
        q.setStateInformation (custom.getData(), static_cast<int> (custom.getSize()));
        const auto g = dynamic_cast<AnalogEngine&> (q.productEngine()).fxRoute();
        CHECK (g.custom);
        CHECK (! g.mainSend);
    }
    // MAIN + SEND: schema 2, mode, its two chains; recalled exactly.
    juce::MemoryBlock both;
    fx::Graph saved;
    {
        ShellProcessor p;
        auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
        saved = fx::mainSendGraph (fx::classicGraph());
        saved.send.insert (saved.send.begin(), fx::Node::effect ({ fx::Type::eq, 1 }));
        REQUIRE (engine.setFxRoute (saved));
        setValue (p, params::sendA, 40.0f);
        both = stateOf (p);
        auto xml = juce::AudioProcessor::getXmlFromBinary (both.getData(), static_cast<int> (both.getSize()));
        const auto* routing = findElement (*xml, "FxRouting");
        REQUIRE (routing != nullptr);
        CHECK (routing->getIntAttribute ("schema") == 2);
        CHECK (routing->getStringAttribute ("mode") == "mainSend");
        CHECK (routing->getStringAttribute ("main").isNotEmpty());
        CHECK (routing->getStringAttribute ("send").contains ("space.1"));
    }
    {
        ShellProcessor q;
        q.setStateInformation (both.getData(), static_cast<int> (both.getSize()));
        CHECK (dynamic_cast<AnalogEngine&> (q.productEngine()).fxRoute() == saved);
        CHECK (std::abs (q.parameterValue (params::sendA) - 40.0f) < 0.01f);
    }
}

TEST_CASE ("analog: MAIN + SEND - through the plugin, card C's send reaches SEND's room and card A's level stays its own", "[v6][mainsend]")
{
    namespace fx = andor::analog::fx;
    ShellProcessor p;
    auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
    REQUIRE (engine.setFxRoute (fx::mainSendGraph (fx::classicGraph())));
    setValue (p, "space", 80.0f);
    setValue (p, "echo", 0.0f);
    setValue (p, params::hybrid2Enabled, 1.0f);   // (card C sounds: its second layer, an oscillator)
    const std::vector<Note> shortNote { { 0, 60, 100 }, { 4800, 60, 0 } };
    const auto dry = renderShell (p, 1.5, shortNote, false);
    setValue (p, params::sendC, 100.0f);
    const auto wet = renderShell (p, 1.5, shortNote, false);
    // The room after the note: louder with card C sent (its tail is SEND's alone).
    auto tail = [] (const juce::AudioBuffer<float>& b) {
        double sum = 0.0;
        for (int i = 48000; i < b.getNumSamples(); ++i)
            sum += static_cast<double> (b.getSample (0, i)) * b.getSample (0, i);
        return std::sqrt (sum / (b.getNumSamples() - 48000));
    };
    CHECK (tail (wet) > 2.0 * tail (dry) + 1.0e-5);
}

TEST_CASE ("analog: V6 gate 4 screenshots - FX ROUTING's MAIN, SEND and OVERVIEW (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-v6-g4]")
{
    namespace fx = andor::analog::fx;
    const char* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    for (const int width : { 1448, 869 })
    {
        ShellProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
        auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
        editor->setSize (width, juce::roundToInt (static_cast<float> (width) * 1086.0f / 1448.0f));
        ui.setSourceViewShown (true);
        auto* view = dynamic_cast<andor::analog::FxRoutingView*> (ui.sourceViewComponent());
        REQUIRE (view != nullptr);
        auto shoot = [&] (const juce::String& name) {
            ui.refreshNow();
            view->resized();
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            const auto file = out.getChildFile (name + "-" + juce::String (width) + ".png");
            file.deleteFile();
            juce::FileOutputStream stream (file);
            juce::PNGImageFormat().writeImageToStream (image, stream);
        };
        shoot ("g4-1-classic");
        REQUIRE (view->useMainSend());
        view->insertNew (fx::Type::eq);   // (an EQ on MAIN after MOVEMENT)
        view->setTab (1);
        view->select (std::nullopt);
        shoot ("g4-3-send");
        view->setTab (0);
        view->select (std::nullopt);
        shoot ("g4-2-main");
        view->setTab (2);
        shoot ("g4-4-overview");
        ui.setSourceViewShown (false);
    }
}

TEST_CASE ("analog: MAIN + SEND (review fixes) - a chain that does not fit its tab is refused before a setting moves; the same C2 source again stays loaded", "[v6][mainsend][hybrid2]")
{
    namespace fx = andor::analog::fx;
    ShellProcessor p;
    auto& engine = dynamic_cast<AnalogEngine&> (p.productEngine());
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    editor->setSize (1448, 1086);
    ui.setSourceViewShown (true);
    auto* view = dynamic_cast<andor::analog::FxRoutingView*> (ui.sourceViewComponent());
    REQUIRE (view != nullptr);

    // A saved chain holding CONVOLUTION at a MIX of its own.
    Scratch scratch;
    fx::Graph withConv;
    withConv.custom = true;
    withConv.chain = { fx::Node::effect ({ fx::Type::convolution, 0 }) };
    setValue (p, "analog.fx.conv.1.mix", 80.0f);
    const auto xml = andor::analog::fxpresets::chainToXml (p, withConv, "conv");
    REQUIRE (xml != nullptr);
    const auto file = scratch.dir.getChildFile ("conv.xml");
    REQUIRE (xml->writeTo (file));
    setValue (p, "analog.fx.conv.1.mix", 30.0f);

    // On MAIN it does not fit (CONVOLUTION belongs on SEND): refused, the MIX untouched.
    REQUIRE (view->useMainSend());
    const auto before = engine.fxRoute();
    CHECK (! view->loadChain (file));
    CHECK (engine.fxRoute() == before);
    CHECK (std::abs (p.parameterValue ("analog.fx.conv.1.mix") - 30.0f) < 0.01f);
    // On SEND it fits.
    view->setTab (1);
    CHECK (view->loadChain (file));
    CHECK (std::abs (p.parameterValue ("analog.fx.conv.1.mix") - 80.0f) < 0.01f);
    ui.setSourceViewShown (false);

    // C2's source set again to the same definition: stays loaded, never "loading".
    const auto rec = writeRecording (scratch.dir, "again.wav", 60, 1.0);
    const auto made = andor::analog::import::makeFileDefinition (rec, scratch.dir.getChildFile ("Imported"));
    REQUIRE (made.xml != nullptr);
    engine.setHybrid2Definition (std::make_unique<juce::XmlElement> (*made.xml), scratch.dir.getChildFile ("Imported"));
    REQUIRE (engine.waitForHybrid2 (10000));
    engine.setHybrid2Definition (std::make_unique<juce::XmlElement> (*made.xml), scratch.dir.getChildFile ("Imported"));
    CHECK (! engine.hybrid2State().loading);
    CHECK (engine.hybrid2State().loaded);
}
