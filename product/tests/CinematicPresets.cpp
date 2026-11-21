#include "CinematicPresets.h"

#include "AnalogMath.h"
#include "FactoryPatches.h"
#include "KeyDnaState.h"
#include "ShellProduct.h"
#include "engine/Modulation.h"

namespace cinematic
{

using namespace andor::analog;
using andor::shell::ShellProcessor;

namespace
{
    double hz (double cutoff) { return math::hzToTone (cutoff); }

    void filterEnv (Patch& p, double a, double d, double s, double r)
    {
        p.filterAttack = a;
        p.filterDecay = d;
        p.filterSustain = s;
        p.filterRelease = r;
    }
    void ampEnv (Patch& p, double a, double d, double s, double r)
    {
        p.ampAttack = a;
        p.ampDecay = d;
        p.ampSustain = s;
        p.ampRelease = r;
    }
    void hall (Patch& p, double amount, double decay, double preDelayMs = 20.0, double highCutHz = 6000.0)
    {
        p.space = amount;
        p.effects.spaceEngine = osp::SpaceEngine::rooms;
        p.effects.spaceType = osp::SpaceType::hall;
        p.effects.spaceDecaySeconds = decay;
        p.effects.spacePreDelayMs = preDelayMs;
        p.effects.spaceHighCutHz = highCutHz;
    }
    void plate (Patch& p, double amount, double decay)
    {
        p.space = amount;
        p.effects.spaceEngine = osp::SpaceEngine::rooms;
        p.effects.spaceType = osp::SpaceType::plate;
        p.effects.spaceDecaySeconds = decay;
    }
    void echo (Patch& p, double amount, int division, double feedback, osp::EchoType type = osp::EchoType::tape)
    {
        p.echo = amount;
        p.effects.echoSync = true;
        p.effects.echoDivision = division;   // 0 1/16, 1 1/8T, 2 1/16D, 3 1/8, 4 1/4T, 5 1/8D, 6 1/4 ...
        p.effects.echoFeedback = feedback;
        p.effects.echoType = type;
    }
    UnisonSettings unison (int voices, double detune, double blend, double width, UnisonSpread spread = UnisonSpread::linear)
    {
        UnisonSettings u;
        u.voices = voices;
        u.detune = detune;
        u.blend = blend;
        u.width = width;
        u.spread = spread;
        return u;
    }

    /** A modulation route: SOURCE onto DEST at DEPTH (%, -100..100). */
    void route (ShellProcessor& p, int slot, osp::mod::Source source, osp::mod::Dest dest, float depthPercent)
    {
        using Host = osp::plugin::InstrumentHost;
        p.setParameterValue (Host::modRouteId (slot, "source"), static_cast<float> (osp::mod::sourceIndex (source) + 1));
        p.setParameterValue (Host::modRouteId (slot, "dest"), static_cast<float> (osp::mod::choiceOf (dest)));
        p.setParameterValue (Host::modRouteId (slot, "depth"), depthPercent);
        p.setParameterValue (Host::modRouteId (slot, "enabled"), 1.0f);
    }

    void choose (ShellProcessor& p, const juce::String& id, const juce::String& choice)
    {
        auto* c = dynamic_cast<juce::AudioParameterChoice*> (p.apvts().getParameter (id));
        jassert (c != nullptr && c->choices.contains (choice));
        if (c != nullptr)
            c->setValueNotifyingHost (c->convertTo0to1 (static_cast<float> (c->choices.indexOf (choice))));
    }

    /** Every preset starts from a neutral, quiet canvas: two saws, no MORPH, no REIMAGINED,
        a little analog drift, SPACE and ECHO off (each preset sets its own). */
    Patch canvas()
    {
        Patch p;
        p.morph.enabled = false;
        p.space = 0.0;
        p.echo = 0.0;
        p.motion = 0.0;
        p.lfo.shape = osp::mod::LfoShape::triangle;
        p.lfo.rateHz = 5.0;
        p.lfoAmount = 0.0;
        p.lfoPitch1 = p.lfoPitch2 = 0.0;
        p.lfoWidth1 = p.lfoWidth2 = 0.0;
        return p;
    }

    std::vector<Spec> build()
    {
        std::vector<Spec> list;

        // 1. NEON REVERIE - expressive CS-80-style brass: two detuned saws through a soft ladder,
        // a slow brassy swell of the filter, brightness under the fingers (aftertouch), vibrato on
        // the wheel, a long hall.
        list.push_back ({ "Neon Reverie", "Blade Runner (1982): expressive CS-80 brass", 36, 84, 8, false,
                          [] (Patch& p) {
                              p = canvas();
                              p.osc1Wave = Osc1Wave::saw;
                              p.osc2Wave = Osc2Wave::saw;
                              p.osc1Level = 0.9;
                              p.osc2Level = 0.8;
                              p.osc2FineCents = 8.0;
                              p.oscUnison[0] = unison (3, 0.22, 0.55, 0.6);
                              p.tone = hz (780.0);
                              p.resonance = 0.12;
                              p.keyTrack = 0.55;
                              p.envDepthSemitones = 32.0;
                              p.velocityToFilter = 0.6;
                              p.highPass = 1;
                              filterEnv (p, 0.14, 1.1, 0.5, 1.2);
                              ampEnv (p, 0.08, 0.8, 0.88, 1.5);
                              p.velocityRangeDb = 16.0;
                              p.analogAmount = 0.55;
                              p.motion = 0.15;   // MOVEMENT's DRIFT: the slow living pitch
                              p.vibrato.enabled = true;
                              p.vibrato.depthCents = 22.0;
                              p.vibrato.rateHz = 5.6;
                              p.vibrato.delaySeconds = 0.3;
                              p.vibrato.riseSeconds = 0.6;
                              p.vibrato.control = osp::vibrato::Control::modWheel;
                              hall (p, 0.32, 5.5, 32.0, 6500.0);
                              echo (p, 0.10, 5, 0.32);
                              p.levelDb = 1.0;
                          },
                          [] (ShellProcessor& s) {
                              route (s, 0, osp::mod::Source::pressure, osp::mod::Dest::cutoff, 45.0f);   // the CS-80's brightness under the finger
                          } });

        // 2. SIGNAL FROM BELOW - a pulsing analog arpeggio: saw + pulse an octave down, a resonant
        // ladder plucked by a short envelope, the arpeggiator going UP in sixteenths, tape echo.
        list.push_back ({ "Signal From Below", "Stranger Things: analog pulsing arpeggio", 36, 79, 8, true,
                          [] (Patch& p) {
                              p = canvas();
                              p.osc1Wave = Osc1Wave::saw;
                              p.osc2Wave = Osc2Wave::pulse;
                              p.osc2Octave = -1;
                              p.osc2Width = 0.42;
                              p.osc2FineCents = 4.0;
                              p.osc1Level = 0.9;
                              p.osc2Level = 0.55;
                              p.tone = hz (520.0);
                              p.resonance = 0.38;
                              p.keyTrack = 0.45;
                              p.envDepthSemitones = 30.0;
                              p.velocityToFilter = 0.45;
                              filterEnv (p, 0.001, 0.24, 0.08, 0.16);
                              ampEnv (p, 0.001, 0.4, 0.55, 0.14);
                              p.velocityRangeDb = 14.0;
                              p.analogAmount = 0.4;
                              echo (p, 0.16, 5, 0.3);
                              plate (p, 0.12, 1.6);
                              p.levelDb = 6.0;
                          },
                          [] (ShellProcessor& s) {
                              s.setParameterValue (osp::plugin::ids::arpEnabled, 1.0f);
                              choose (s, osp::plugin::ids::arpPattern, "UP");
                              choose (s, osp::plugin::ids::arpRate, "1/16");
                              s.setParameterValue (osp::plugin::ids::arpOctaves, 2.0f);
                              s.setParameterValue (osp::plugin::ids::arpGate, 62.0f);
                              route (s, 0, osp::mod::Source::modWheel, osp::mod::Dest::cutoff, 35.0f);   // the wheel opens the sequence up
                          } });

        // 3. OBSIDIAN HORIZON - monumental synth brass: wide saws (unison on both cards, B an octave
        // down), a sub, the ladder driven hard and opening slowly, AMP 2 swelling notes played over
        // held ones even more slowly, a dark ABYSS space.
        list.push_back ({ "Obsidian Horizon", "Blade Runner 2049: monumental synth brass", 24, 67, 6, false,
                          [] (Patch& p) {
                              p = canvas();
                              p.osc1Wave = Osc1Wave::saw;
                              p.osc2Wave = Osc2Wave::saw;
                              p.osc2Octave = -1;
                              p.osc2FineCents = 5.0;
                              p.osc1Level = 0.85;
                              p.osc2Level = 0.75;
                              p.oscUnison[0] = unison (6, 0.32, 0.7, 0.9);
                              p.oscUnison[1] = unison (4, 0.25, 0.6, 0.7);
                              p.sub.enabled = true;
                              p.sub.wave = SubSettings::Wave::sine;
                              p.sub.octave = -1;
                              p.sub.levelDb = -18.5;
                              p.sub.lowKey = 24;
                              p.sub.highKey = 67;
                              p.tone = hz (360.0);
                              p.resonance = 0.14;
                              p.keyTrack = 0.4;
                              p.envDepthSemitones = 36.0;
                              p.velocityToFilter = 0.5;
                              p.filterDrive = 0.45;
                              filterEnv (p, 0.35, 2.4, 0.5, 2.6);
                              ampEnv (p, 0.3, 1.6, 0.95, 3.0);
                              p.amp2Enabled = true;
                              p.amp2Attack = 1.4;
                              p.velocityRangeDb = 14.0;
                              p.drive = 0.3;
                              p.analogAmount = 0.5;
                              p.motion = 0.12;
                              p.space = 0.36;
                              p.effects.spaceEngine = osp::SpaceEngine::abyss;
                              p.effects.abyssVariant = osp::AbyssVariant::dark;
                              p.effects.spaceDecaySeconds = 8.0;
                              p.effects.spacePreDelayMs = 40.0;
                              p.levelDb = -7.0;
                          },
                          [] (ShellProcessor& s) {
                              route (s, 0, osp::mod::Source::pressure, osp::mod::Dest::cutoff, 40.0f);
                          } });

        // 4. GRAVITY CATHEDRAL - an immense organ: square (flute 8'), saw an octave up (principal
        // 4'), OSC 3 two octaves up (a mixture), the SUB's triangle an octave down (16'). No
        // envelope sweep, no velocity (an organ's keys), a cathedral hall.
        list.push_back ({ "Gravity Cathedral", "Interstellar: immense organ-like sound", 24, 96, 12, false,
                          [] (Patch& p) {
                              p = canvas();
                              p.osc1Wave = Osc1Wave::pulse;
                              p.osc1Width = 0.5;
                              p.osc1Level = 0.85;
                              p.osc2Wave = Osc2Wave::saw;
                              p.osc2Octave = 1;
                              p.osc2FineCents = 1.5;
                              p.osc2Level = 0.5;
                              p.hybridType = HybridType::oscillator;
                              p.osc3Wave = Osc2Wave::pulse;
                              p.osc3Width = 0.5;
                              p.osc3Octave = 2;
                              p.sub.enabled = true;
                              p.sub.wave = SubSettings::Wave::triangle;
                              p.sub.octave = -1;
                              p.sub.levelDb = -27.0;
                              p.sub.lowKey = 24;
                              p.sub.highKey = 72;
                              p.tone = hz (5200.0);
                              p.resonance = 0.0;
                              p.keyTrack = 0.6;
                              p.envDepthSemitones = 0.0;
                              filterEnv (p, 0.01, 0.5, 1.0, 1.0);
                              ampEnv (p, 0.06, 0.2, 1.0, 1.8);
                              p.velocityRangeDb = 0.0;   // (an organ: every key alike)
                              p.analogAmount = 0.15;
                              p.vibrato.enabled = true;   // the wheel: a tremulant
                              p.vibrato.depthCents = 9.0;
                              p.vibrato.rateHz = 6.2;
                              p.vibrato.delaySeconds = 0.0;
                              p.vibrato.riseSeconds = 0.2;
                              hall (p, 0.38, 7.5, 45.0, 5500.0);
                              p.levelDb = -6.0;
                          },
                          [] (ShellProcessor& s) {
                              s.setParameterValue ("layerB.level", -14.0f);   // card C's LEVEL (the shell's: a Patch does not write it)
                          } });

        // 5. SANDBORNE - a distorted, organic desert horn: a saw an octave down against a narrow
        // pulse a fifth up, the ring modulator's grit, a resonant driven ladder over a high-pass
        // (a throat-like band), breath noise; MONO with LEGATO and glide, big bends, a throat
        // waver on the wheel.
        list.push_back ({ "Sandborne", "Dune (2021): distorted, organic desert horn", 28, 60, 1, false,
                          [] (Patch& p) {
                              p = canvas();
                              p.osc1Wave = Osc1Wave::saw;
                              p.osc1Octave = -1;
                              p.osc1Level = 1.0;
                              p.osc2Wave = Osc2Wave::pulse;
                              p.osc2Width = 0.28;
                              p.osc2Coarse = 7.0;
                              p.osc2Octave = -1;
                              p.osc2Level = 0.45;
                              p.ringMix = 0.22;
                              p.oscUnison[0] = unison (3, 0.18, 0.5, 0.5);
                              p.noiseLevel = 0.12;
                              p.noiseColor = NoiseColor::juno;   // (the darker of the two)
                              p.tone = hz (620.0);
                              p.resonance = 0.52;
                              p.keyTrack = 0.5;
                              p.envDepthSemitones = 16.0;
                              p.filterDrive = 0.7;
                              p.driveBias = 0.35;
                              p.highPass = 2;
                              filterEnv (p, 0.35, 1.4, 0.6, 1.0);
                              ampEnv (p, 0.22, 1.0, 0.9, 1.3);
                              p.velocityToFilter = 0.5;
                              p.velocityRangeDb = 12.0;
                              p.voiceMode = VoiceMode::mono;
                              p.legato = true;
                              p.glideSeconds = 0.12;
                              p.bendRangeSemitones = 7.0;
                              p.drive = 0.5;
                              p.analogAmount = 0.6;
                              p.vibrato.enabled = true;
                              p.vibrato.depthCents = 38.0;
                              p.vibrato.rateHz = 4.6;
                              p.vibrato.delaySeconds = 0.0;
                              p.vibrato.riseSeconds = 0.3;
                              hall (p, 0.28, 4.5, 25.0, 5000.0);
                              p.levelDb = 6.0;
                          },
                          [] (ShellProcessor& s) {
                              route (s, 0, osp::mod::Source::pressure, osp::mod::Dest::cutoff, 45.0f);
                          } });

        // 6. POLAR HEARTBEAT - a minimal, ominous bass: a triangle and a soft pulse low under a dark
        // ladder, a thump of filter envelope and a short body, MONO retriggered (LEGATO off), almost
        // dry - for slow repeated notes.
        list.push_back ({ "Polar Heartbeat", "The Thing (1982): minimalist ominous bass", 24, 52, 1, false,
                          [] (Patch& p) {
                              p = canvas();
                              p.osc1Wave = Osc1Wave::pulse;
                              p.osc1Width = 0.5;
                              p.osc1Level = 0.55;
                              p.osc2Wave = Osc2Wave::triangle;
                              p.osc2FineCents = 0.0;
                              p.osc2Level = 1.0;
                              p.sub.enabled = false;   // (an octave under these keys is felt, not heard: the heartbeat stays in the triangle)
                              p.sub.wave = SubSettings::Wave::sine;
                              p.sub.octave = -1;
                              p.sub.levelDb = -12.0;
                              p.sub.lowKey = 24;
                              p.sub.highKey = 52;
                              p.sub.taper = false;
                              p.tone = hz (240.0);
                              p.resonance = 0.18;
                              p.keyTrack = 0.3;
                              p.envDepthSemitones = 16.0;
                              p.velocityToFilter = 0.6;
                              filterEnv (p, 0.002, 0.32, 0.0, 0.2);
                              ampEnv (p, 0.003, 0.7, 0.35, 0.4);
                              p.velocityRangeDb = 18.0;
                              p.voiceMode = VoiceMode::mono;
                              p.legato = false;
                              p.analogAmount = 0.3;
                              p.space = 0.08;
                              p.effects.spaceEngine = osp::SpaceEngine::rooms;
                              p.effects.spaceType = osp::SpaceType::room;
                              p.effects.spaceDecaySeconds = 1.2;
                              p.levelDb = 6.0;
                          },
                          [] (ShellProcessor& s) {
                              route (s, 0, osp::mod::Source::modWheel, osp::mod::Dest::cutoff, 40.0f);
                          } });

        // 7. CIRCUIT CATHEDRAL - a futuristic synth/orchestral pulse: SUPER SAW on A, a pulse an
        // octave down on B, a bright OTA (Jupiter), MOVEMENT's SHAPER pulsing the held chord in
        // sixteenths (the wheel and MOVEMENT macro set how much), a plate.
        list.push_back ({ "Circuit Cathedral", "TRON: Legacy: futuristic synth/orchestral pulse", 36, 84, 8, false,
                          [] (Patch& p) {
                              p = canvas();
                              p.osc1Model = OscModel::wide;
                              p.wide1.wave = WideWave::superSaw;
                              p.wide1.spread = 0.42;
                              p.wide1.shape = 0.5;
                              p.osc1Level = 0.85;
                              p.osc2Wave = Osc2Wave::pulse;
                              p.osc2Octave = -1;
                              p.osc2Width = 0.45;
                              p.osc2Level = 0.5;
                              p.filterModel = FilterModel::ota;
                              p.otaVoicing = OtaVoicing::jupiter;
                              p.tone = hz (1700.0);
                              p.resonance = 0.2;
                              p.keyTrack = 0.5;
                              p.envDepthSemitones = 22.0;
                              p.velocityToFilter = 0.5;
                              filterEnv (p, 0.004, 0.28, 0.45, 0.3);
                              ampEnv (p, 0.003, 0.4, 0.85, 0.3);
                              p.velocityRangeDb = 14.0;
                              p.analogAmount = 0.25;
                              p.motion = 0.65;
                              p.effects.movementMode = osp::MovementMode::shaper;
                              p.effects.shaper.pattern = 0;   // PULSE
                              p.effects.shaper.rate = osp::ShaperRate::sixteenth;
                              p.effects.shaper.target = osp::ShaperTarget::both;
                              p.effects.shaper.smooth = 0.2;
                              plate (p, 0.18, 2.2);
                              echo (p, 0.10, 3, 0.25, osp::EchoType::bbd);
                              p.levelDb = 3.0;
                          },
                          nullptr });

        // 8. MACHINE HEART - metallic, mechanical synthesis: OSC 1 synced to OSC 2 and swept by
        // Poly Mod's envelope, the ring modulator for clang, short struck envelopes, a cold BBD
        // echo and plate.
        list.push_back ({ "Machine Heart", "The Terminator (1984): metallic mechanical synthesis", 36, 84, 8, false,
                          [] (Patch& p) {
                              p = canvas();
                              p.osc1Wave = Osc1Wave::saw;
                              p.osc1Coarse = 12.0;
                              p.osc1Level = 1.0;
                              p.osc2Wave = Osc2Wave::pulse;
                              p.osc2Width = 0.35;
                              p.osc2FineCents = 0.0;
                              p.osc2Level = 0.8;
                              p.sync = true;
                              p.ringMix = 0.45;
                              p.polyModEnv = 0.55;
                              p.polyModToPitch1 = true;
                              p.tone = hz (2200.0);
                              p.resonance = 0.3;
                              p.keyTrack = 0.6;
                              p.envDepthSemitones = 26.0;
                              p.velocityToFilter = 0.6;
                              filterEnv (p, 0.001, 0.5, 0.2, 0.4);
                              ampEnv (p, 0.002, 0.9, 0.3, 0.45);
                              p.velocityRangeDb = 18.0;
                              p.drive = 0.15;
                              p.analogAmount = 0.2;
                              echo (p, 0.14, 3, 0.28, osp::EchoType::bbd);
                              plate (p, 0.16, 1.8);
                              p.levelDb = 6.0;
                          },
                          [] (ShellProcessor& s) {
                              route (s, 0, osp::mod::Source::modWheel, osp::mod::Dest::cutoff, 30.0f);
                          } });

        // 9. VELVET PINES - a dreamy electric-piano / pad: a triangle with a quieter narrow pulse
        // for the tine, struck by a quick filter envelope and decaying like an electric piano;
        // the ensemble chorus (I+II), MOVEMENT's slow stereo pulse as a suitcase tremolo, a hall.
        list.push_back ({ "Velvet Pines", "Twin Peaks: dreamy electric piano / pad", 36, 88, 12, false,
                          [] (Patch& p) {
                              p = canvas();
                              p.osc1Wave = Osc1Wave::pulse;
                              p.osc1Width = 0.18;
                              p.osc1Level = 0.28;
                              p.osc2Wave = Osc2Wave::triangle;
                              p.osc2FineCents = 3.0;
                              p.osc2Level = 1.0;
                              p.tone = hz (1500.0);
                              p.resonance = 0.05;
                              p.keyTrack = 0.7;
                              p.envDepthSemitones = 20.0;
                              p.velocityToFilter = 0.75;
                              filterEnv (p, 0.001, 0.6, 0.15, 0.8);
                              ampEnv (p, 0.003, 3.2, 0.3, 1.1);
                              p.velocityRangeDb = 22.0;
                              p.analogAmount = 0.35;
                              p.chorusMode = 3;   // I + II
                              p.motion = 0.22;
                              p.effects.movementMode = osp::MovementMode::pulse;
                              p.effects.pulseRate = 0.35;
                              p.effects.pulseShape = 0.2;
                              p.effects.pulseStereo = 0.8;
                              hall (p, 0.28, 3.6, 18.0, 6000.0);
                              p.levelDb = 4.0;
                          },
                          nullptr });

        // 10. MIDNIGHT CHROME - a nostalgic analog polysynth: DCO saw + pulse with a little sub, the
        // Juno OTA, the chorus II, a dotted tape echo and a modest hall.
        list.push_back ({ "Midnight Chrome", "Drive (2011): nostalgic analog polysynth", 36, 88, 8, false,
                          [] (Patch& p) {
                              p = canvas();
                              p.osc1Model = OscModel::dco;
                              p.dco1.saw = true;
                              p.dco1.pulse = true;
                              p.dco1.subLevel = 0.3;
                              p.osc1Width = 0.5;
                              p.osc1Level = 0.9;
                              p.osc2Level = 0.0;
                              p.osc2Enabled = false;
                              p.filterModel = FilterModel::ota;
                              p.otaVoicing = OtaVoicing::juno;
                              p.tone = hz (1400.0);
                              p.resonance = 0.16;
                              p.keyTrack = 0.5;
                              p.envDepthSemitones = 18.0;
                              p.velocityToFilter = 0.45;
                              filterEnv (p, 0.01, 1.0, 0.5, 0.8);
                              ampEnv (p, 0.01, 1.0, 0.8, 0.8);
                              p.velocityRangeDb = 16.0;
                              p.analogAmount = 0.35;
                              p.chorusMode = 2;   // II
                              echo (p, 0.14, 5, 0.3);
                              hall (p, 0.2, 2.6, 15.0, 6500.0);
                              p.levelDb = 6.0;
                          },
                          nullptr });
        return list;
    }

    void configure (ShellProcessor& p, const Spec& spec)
    {
        Patch patch;
        spec.patch (patch);
        // (As the factory set: the oscillators' mix in their levels, BLEND at its centre.)
        foldBlend (patch.blend, patch.osc1Level, patch.osc2Level);
        patch.blend = 0.5;
        const auto values = params::valuesOf (patch);
        for (auto* base : p.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (base))
            {
                const auto it = values.find (ranged->getParameterID());
                const float value = it != values.end() ? it->second : ranged->convertFrom0to1 (ranged->getDefaultValue());
                ranged->setValueNotifyingHost (ranged->convertTo0to1 (value));
            }
        if (spec.extra != nullptr)
            spec.extra (p);
    }
}

const std::vector<Spec>& collection()
{
    static const auto list = build();
    return list;
}

std::unique_ptr<juce::XmlElement> presetXml (const Spec& spec)
{
    ShellProcessor p;
    configure (p, spec);
    auto tree = p.apvts().copyState();
    for (int i = tree.getNumChildren(); --i >= 0;)
        if (! tree.getChild (i).hasType ("PARAM"))
            tree.removeChild (i, nullptr);
    const auto& info = andor::shell::productInfo();
    auto xml = std::make_unique<juce::XmlElement> (info.stateType);
    xml->setAttribute ("schemaVersion", 1);
    xml->setAttribute ("product", info.slug);
    xml->setAttribute ("productVersion", info.stateVersion);
    if (auto params = tree.createXml())
        xml->addChildElement (params.release());
    std::array<osp::keydna::Settings, osp::plugin::keyDnaState::layerCount> keyDna {};
    if (auto dna = osp::plugin::keyDnaState::toTree (keyDna, {}).createXml())
        xml->addChildElement (dna.release());
    return xml;
}

juce::File collectionFolder()
{
    return juce::File (__FILE__).getParentDirectory().getParentDirectory().getParentDirectory().getChildFile ("presets/Cinematic Afterimages Vol. 1");
}

juce::File fileFor (const Spec& spec)
{
    return collectionFolder().getChildFile (juce::File::createLegalFileName (juce::String (spec.name)) + ".andoranalog");
}

} // namespace cinematic
