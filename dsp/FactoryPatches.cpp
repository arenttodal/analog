#include "FactoryPatches.h"

#include "AnalogMath.h"

namespace andor::analog
{

namespace
{
    using osp::EchoStereo;
    using osp::EchoType;
    using osp::SpaceType;

    /** Cutoff in Hz as TONE. */
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
    void room (Patch& p, double amount, SpaceType type, double decay)
    {
        p.space = amount;
        p.effects.spaceType = type;
        p.effects.spaceDecaySeconds = decay;
    }
    void delay (Patch& p, double amount, int division, double feedback, EchoType type = EchoType::tape)
    {
        p.echo = amount;
        p.effects.echoSync = true;
        p.effects.echoDivision = division;
        p.effects.echoFeedback = feedback;
        p.effects.echoType = type;
    }

    /** Every factory sound starts from INIT with the first version's LFO (TRIANGLE at 4 Hz:
        what the mod wheel brings in), so each sounds as it did. */
    Patch base()
    {
        Patch p;
        p.lfo.shape = osp::mod::LfoShape::triangle;
        p.lfo.rateHz = 4.0;
        return p;
    }

    const std::vector<int> chord { 48, 55, 60, 64, 67 };
    const std::vector<int> lowLine { 36, 36, 43, 39, 41, 36, 46, 43 };
    const std::vector<int> leadLine { 60, 63, 67, 70, 72, 70, 67, 65 };
    const std::vector<int> arpLine { 60, 64, 67, 72, 67, 64, 60, 55 };

    std::vector<FactoryPatch> build()
    {
        std::vector<FactoryPatch> list;
        auto add = [&list] (const char* name, const char* category, Patch p, std::vector<int> notes, bool line = false, double keyDna = 0.0) {
            // (Version 3: the oscillators' mix is in their levels; BLEND rests at its centre.)
            foldBlend (p.blend, p.osc1Level, p.osc2Level);
            p.blend = 0.5;
            list.push_back ({ name, category, p, std::move (notes), line, keyDna });
        };

        {
            Patch p = base();   // INIT: two saws a few cents apart, the filter half open.
            add ("Init", "Basic", p, chord);
        }
        {
            Patch p = base();
            p.osc2FineCents = 6.0;
            p.tone = hz (1400.0);
            p.resonance = 0.18;
            p.envDepthSemitones = 20.0;
            p.keyTrack = 0.6;
            filterEnv (p, 0.012, 0.9, 0.35, 0.6);
            ampEnv (p, 0.012, 1.2, 0.9, 0.65);
            p.analogAmount = 0.45;
            room (p, 0.2, SpaceType::hall, 2.2);
            add ("Warm Poly", "Pad", p, chord);
        }
        {
            Patch p = base();
            p.osc2FineCents = 5.0;
            p.tone = hz (520.0);
            p.resonance = 0.12;
            p.envDepthSemitones = 34.0;
            p.velocityToFilter = 0.5;
            filterEnv (p, 0.07, 0.7, 0.45, 0.3);
            ampEnv (p, 0.04, 0.5, 0.85, 0.32);
            p.analogAmount = 0.4;
            room (p, 0.16, SpaceType::room, 1.2);
            add ("Soft Brass", "Brass", p, chord);
        }
        {
            Patch p = base();
            p.osc1Wave = Osc1Wave::pulse;
            p.osc1Width = 0.36;
            p.osc2FineCents = 9.0;
            p.tone = hz (2600.0);
            p.resonance = 0.08;
            p.envDepthSemitones = 6.0;
            filterEnv (p, 0.6, 1.5, 0.6, 1.5);
            ampEnv (p, 0.85, 1.0, 1.0, 1.9);
            p.lfoAmount = 0.35;
            p.lfo.rateHz = 0.35;
            p.lfoPitch1 = 0.0;
            p.lfoPitch2 = 0.0;
            p.lfoWidth1 = 0.2;
            p.analogAmount = 0.55;
            p.stereoSpread = 0.7;
            p.levelDb = -3.5;
            room (p, 0.34, SpaceType::hall, 3.4);
            add ("String Pad", "Pad", p, chord);
        }
        {
            Patch p = base();
            p.osc1Wave = Osc1Wave::pulse;
            p.osc1Width = 0.3;
            p.osc2Level = 0.0;
            p.blend = 0.0;
            p.tone = hz (700.0);
            p.resonance = 0.38;
            p.envDepthSemitones = 30.0;
            filterEnv (p, 0.001, 0.18, 0.0, 0.15);
            ampEnv (p, 0.001, 0.3, 0.25, 0.18);
            p.analogAmount = 0.3;
            delay (p, 0.32, 5, 0.42);
            room (p, 0.12, SpaceType::plate, 1.4);
            add ("Pulse Arp", "Sequence", p, arpLine, true);
        }
        {
            Patch p = base();
            p.voiceMode = VoiceMode::mono;
            p.osc1Octave = -1;
            p.osc2Wave = Osc2Wave::pulse;
            p.osc2Octave = -1;
            p.osc2FineCents = 3.0;
            p.osc2Level = 0.7;
            p.tone = hz (260.0);
            p.resonance = 0.25;
            p.keyTrack = 0.35;
            p.envDepthSemitones = 32.0;
            filterEnv (p, 0.001, 0.26, 0.12, 0.14);
            ampEnv (p, 0.002, 0.4, 0.8, 0.12);
            p.glideSeconds = 0.035;
            p.analogAmount = 0.3;
            p.space = 0.0;
            add ("Rounded Bass", "Bass", p, lowLine, true);
        }
        {
            Patch p = base();
            p.voiceMode = VoiceMode::mono;
            p.osc2FineCents = 8.0;
            p.tone = hz (1800.0);
            p.resonance = 0.3;
            p.envDepthSemitones = 16.0;
            filterEnv (p, 0.004, 0.5, 0.55, 0.25);
            ampEnv (p, 0.004, 0.3, 0.9, 0.22);
            p.glideSeconds = 0.08;
            p.filterDrive = 0.3;
            p.lfoPitch1 = 0.35;
            p.lfoPitch2 = 0.35;
            p.lfoWidth1 = 0.0;
            p.lfoWidth2 = 0.0;
            p.lfo.rateHz = 5.2;
            delay (p, 0.22, 5, 0.35);
            room (p, 0.14, SpaceType::plate, 1.6);
            add ("Mono Lead", "Lead", p, leadLine, true);
        }
        {
            Patch p = base();
            p.osc1Wave = Osc1Wave::pulse;
            p.osc1Width = 0.14;
            p.osc1Level = 0.35;
            p.osc2Wave = Osc2Wave::triangle;
            p.osc2FineCents = 2.0;
            p.osc2Level = 1.0;
            p.tone = hz (900.0);
            p.envDepthSemitones = 24.0;
            p.velocityToFilter = 0.7;
            p.keyTrack = 0.7;
            filterEnv (p, 0.001, 0.9, 0.0, 0.5);
            ampEnv (p, 0.002, 1.8, 0.0, 0.55);
            p.analogAmount = 0.35;
            room (p, 0.18, SpaceType::room, 1.4);
            add ("Soft Keys", "Keys", p, chord);
        }
        {
            Patch p = base();
            p.sync = true;
            p.osc1Coarse = 7.0;
            p.osc2Level = 0.25;
            p.blend = 0.3;
            p.polyModEnv = 0.62;
            p.polyModToPitch1 = true;
            p.tone = hz (3800.0);
            p.resonance = 0.15;
            p.envDepthSemitones = 10.0;
            filterEnv (p, 0.001, 0.45, 0.3, 0.3);
            ampEnv (p, 0.002, 0.6, 0.8, 0.25);
            p.filterDrive = 0.2;
            delay (p, 0.18, 3, 0.3);
            add ("Sync Bite", "Lead", p, leadLine, true);
        }
        {
            Patch p = base();
            p.osc1Level = 0.0;
            p.osc2Wave = Osc2Wave::triangle;
            p.osc2Level = 0.35;
            p.noiseLevel = 1.0;
            p.tone = hz (300.0);
            p.resonance = 0.62;
            p.envDepthSemitones = 40.0;
            p.keyTrack = 1.0;
            filterEnv (p, 2.4, 2.0, 0.25, 2.2);
            ampEnv (p, 1.8, 1.0, 1.0, 2.6);
            room (p, 0.5, SpaceType::hall, 4.5);
            add ("Noise Swell", "Texture", p, { 60, 67 });
        }
        {
            Patch p = base();
            p.osc2FineCents = 15.0;
            p.osc1Wave = Osc1Wave::sawPulse;
            p.osc1Width = 0.42;
            p.tone = hz (1900.0);
            p.resonance = 0.1;
            p.envDepthSemitones = 4.0;
            filterEnv (p, 1.2, 2.0, 0.6, 2.0);
            ampEnv (p, 1.1, 1.0, 1.0, 2.4);
            p.analogAmount = 1.0;
            p.lfoAmount = 0.25;
            p.lfo.rateHz = 0.18;
            p.lfoPitch1 = 0.12;
            p.lfoPitch2 = -0.12;
            p.lfoCutoff = 7.0;
            p.stereoSpread = 0.9;
            room (p, 0.42, SpaceType::hall, 5.0);
            delay (p, 0.14, 8, 0.4);
            add ("Detuned Haze", "Pad", p, chord);
        }
        {
            Patch p = base();
            p.osc2Wave = Osc2Wave::triangle;
            p.osc2Coarse = 7.0;
            p.osc2FineCents = 0.0;
            p.osc2Level = 0.15;
            p.blend = 0.35;
            p.polyModOsc2 = 0.55;
            p.polyModToPitch1 = true;
            p.tone = hz (5000.0);
            p.resonance = 0.05;
            p.envDepthSemitones = 12.0;
            filterEnv (p, 0.001, 1.6, 0.0, 1.0);
            ampEnv (p, 0.001, 2.6, 0.0, 1.3);
            p.analogAmount = 0.25;
            room (p, 0.3, SpaceType::plate, 2.8);
            add ("Metal Bells", "Keys", p, { 72, 76, 79, 84 });
        }
        {
            Patch p = base();
            p.unison = true;
            p.polyphony = 8;
            p.unisonDetune = 0.45;
            p.stereoSpread = 0.75;
            p.tone = hz (2400.0);
            p.resonance = 0.2;
            p.envDepthSemitones = 14.0;
            filterEnv (p, 0.003, 0.6, 0.5, 0.3);
            ampEnv (p, 0.003, 0.4, 0.9, 0.3);
            p.glideSeconds = 0.05;
            p.levelDb = -4.0;
            delay (p, 0.2, 5, 0.38);
            room (p, 0.16, SpaceType::plate, 1.8);
            add ("Unison Lead", "Lead", p, leadLine, true);
        }
        {
            Patch p = base();
            p.osc2FineCents = 4.0;
            p.tone = hz (380.0);
            p.resonance = 0.16;
            p.envDepthSemitones = 38.0;
            filterEnv (p, 0.002, 0.24, 0.2, 0.16);
            ampEnv (p, 0.002, 0.3, 0.55, 0.16);
            p.velocityToFilter = 0.6;
            p.filterDrive = 0.15;
            room (p, 0.14, SpaceType::room, 1.0);
            add ("Brass Stab", "Brass", p, chord);
        }
        {
            Patch p = base();
            p.osc1Wave = Osc1Wave::pulse;
            p.osc1Width = 0.2;
            p.osc2Level = 0.0;
            p.blend = 0.0;
            p.tone = hz (240.0);
            p.resonance = 0.55;
            p.envDepthSemitones = 42.0;
            p.velocityToFilter = 0.5;
            filterEnv (p, 0.001, 0.22, 0.0, 0.12);
            ampEnv (p, 0.001, 0.5, 0.0, 0.1);
            add ("Wah Clav", "Keys", p, { 52, 55, 59, 64 });
        }
        {
            Patch p = base();
            p.osc1Wave = Osc1Wave::pulse;
            p.osc1Octave = -1;
            p.osc1Width = 0.5;
            p.osc2Octave = -2;
            p.osc2Wave = Osc2Wave::triangle;
            p.osc2Level = 0.8;
            p.tone = hz (180.0);
            p.resonance = 0.2;
            p.envDepthSemitones = 36.0;
            filterEnv (p, 0.001, 0.16, 0.0, 0.1);
            ampEnv (p, 0.001, 0.55, 0.0, 0.14);
            p.space = 0.0;
            add ("Sub Pluck", "Bass", p, lowLine, true);
        }
        {
            Patch p = base();
            p.osc2FineCents = 10.0;
            p.tone = hz (420.0);
            p.resonance = 0.42;
            p.envDepthSemitones = 8.0;
            filterEnv (p, 1.0, 2.0, 0.5, 2.0);
            ampEnv (p, 0.7, 1.0, 1.0, 2.2);
            p.lfo.sync = true;
            p.lfo.division = 5;   // a bar
            p.lfo.shape = osp::mod::LfoShape::sine;
            p.lfoAmount = 0.55;
            p.lfoPitch1 = 0.0;
            p.lfoPitch2 = 0.0;
            p.lfoWidth1 = 0.0;
            p.lfoWidth2 = 0.0;
            p.lfoCutoff = 24.0;
            room (p, 0.38, SpaceType::hall, 4.0);
            add ("Sweep Pad", "Pad", p, chord);
        }
        {
            Patch p = base();
            p.osc1Wave = Osc1Wave::pulse;
            p.osc1Width = 0.25;
            p.osc1Octave = 1;
            p.osc1Level = 0.5;
            p.osc2Wave = Osc2Wave::triangle;
            p.osc2Level = 0.9;
            p.polyModEnv = 0.5;
            p.polyModToWidth1 = true;
            p.tone = hz (3000.0);
            p.envDepthSemitones = 10.0;
            filterEnv (p, 0.001, 1.2, 0.0, 0.8);
            ampEnv (p, 0.002, 2.2, 0.15, 0.9);
            room (p, 0.28, SpaceType::plate, 2.4);
            add ("Glass Keys", "Keys", p, chord);
        }
        {
            Patch p = base();
            p.osc1Wave = Osc1Wave::pulse;
            p.osc2Wave = Osc2Wave::pulse;
            p.osc2Octave = 1;
            p.osc2FineCents = 1.0;
            p.osc2Level = 0.6;
            p.tone = hz (4200.0);
            p.envDepthSemitones = 0.0;
            ampEnv (p, 0.004, 0.2, 1.0, 0.06);
            p.lfoAmount = 0.2;
            p.lfo.rateHz = 6.0;
            p.lfoPitch1 = 0.12;
            p.lfoPitch2 = 0.12;
            p.lfoWidth1 = 0.0;
            p.lfoWidth2 = 0.0;
            p.levelDb = -5.0;
            room (p, 0.2, SpaceType::room, 1.6);
            add ("Hollow Organ", "Keys", p, chord);
        }
        {
            Patch p = base();
            p.voiceMode = VoiceMode::mono;
            p.osc1Wave = Osc1Wave::sawPulse;
            p.osc1Width = 0.4;
            p.osc2FineCents = -7.0;
            p.tone = hz (1500.0);
            p.resonance = 0.35;
            p.envDepthSemitones = 20.0;
            filterEnv (p, 0.002, 0.35, 0.4, 0.3);
            ampEnv (p, 0.003, 0.3, 0.85, 0.3);
            p.glideSeconds = 0.06;
            p.filterDrive = 0.45;
            p.driveBias = 0.4;
            delay (p, 0.42, 6, 0.55);
            room (p, 0.18, SpaceType::plate, 2.0);
            add ("Tape Echo Lead", "Lead", p, leadLine, true);
        }
        {
            Patch p = base();
            p.osc2Wave = Osc2Wave::triangle;
            p.osc2LowFrequency = true;
            p.osc2KeyTrack = false;
            p.osc2Coarse = 5.0;
            p.osc2Level = 0.0;
            p.blend = 0.0;
            p.polyModOsc2 = 0.5;
            p.polyModToCutoff = true;
            p.polyModToWidth1 = true;
            p.osc1Wave = Osc1Wave::sawPulse;
            p.osc1Octave = -1;
            p.tone = hz (500.0);
            p.resonance = 0.5;
            p.envDepthSemitones = 6.0;
            filterEnv (p, 1.5, 2.0, 0.6, 2.0);
            ampEnv (p, 1.2, 1.0, 1.0, 2.6);
            room (p, 0.4, SpaceType::hall, 5.0);
            add ("Low Drone", "Texture", p, { 36, 43 });
        }
        {
            Patch p = base();
            p.osc1Wave = Osc1Wave::pulse;
            p.osc1Width = 0.5;
            p.osc2Wave = Osc2Wave::sawPulse;
            p.osc2Coarse = 12.0;
            p.osc2FineCents = 3.0;
            p.osc2Width = 0.3;
            p.osc2Level = 0.55;
            p.tone = hz (2200.0);
            p.resonance = 0.25;
            p.envDepthSemitones = 18.0;
            filterEnv (p, 0.002, 0.5, 0.3, 0.4);
            ampEnv (p, 0.002, 0.8, 0.6, 0.4);
            room (p, 0.15, SpaceType::room, 1.3);
            add ("Hollow Square", "Keys", p, chord);
        }
        {
            Patch p = base();
            p.osc1Octave = -1;
            p.osc2Octave = -1;
            p.osc2FineCents = 12.0;
            p.tone = hz (650.0);
            p.resonance = 0.3;
            p.envDepthSemitones = 26.0;
            filterEnv (p, 0.35, 1.4, 0.3, 1.2);
            ampEnv (p, 0.25, 1.0, 0.9, 1.4);
            p.lfoAmount = 0.3;
            p.lfo.shape = osp::mod::LfoShape::smoothRandom;   // (the first version stepped it)
            p.lfo.rateHz = 7.0;
            p.lfoPitch1 = 0.0;
            p.lfoPitch2 = 0.0;
            p.lfoWidth1 = 0.0;
            p.lfoWidth2 = 0.0;
            p.lfoCutoff = 14.0;
            delay (p, 0.25, 3, 0.45, EchoType::bbd);
            room (p, 0.3, SpaceType::plate, 2.6);
            add ("Random Steps", "Sequence", p, { 48, 55, 60 });
        }
        {
            Patch p = base();
            p.osc2Wave = Osc2Wave::sawTriangle;
            p.osc2FineCents = 6.0;
            p.osc1Width = 0.5;
            p.tone = hz (850.0);
            p.resonance = 0.2;
            p.envDepthSemitones = 22.0;
            p.velocityToFilter = 0.4;
            filterEnv (p, 0.15, 1.1, 0.4, 0.9);
            ampEnv (p, 0.12, 0.9, 0.85, 1.0);
            p.analogAmount = 0.6;
            p.stereoSpread = 0.6;
            room (p, 0.3, SpaceType::hall, 3.0);
            delay (p, 0.12, 9, 0.3);
            add ("Velvet Strings", "Pad", p, chord);
        }

        // Version 2: ANDOR OSP's engines on the oscillators' live sound.
        {
            Patch p = base();   // GRANULAR: a cloud from each voice's own last second, KEY DNA, KALEIDOSCOPE.
            p.osc2Wave = Osc2Wave::sawTriangle;
            p.osc2FineCents = 9.0;
            p.tone = hz (2200.0);
            p.resonance = 0.12;
            p.envDepthSemitones = 8.0;
            filterEnv (p, 0.8, 2.0, 0.6, 2.0);
            ampEnv (p, 0.6, 1.5, 0.9, 2.2);
            p.granular = true;
            p.grains = { 0.45, 0.28, 22.0, 0.0, 0.6 };
            p.reimagined = 0.45;
            p.lfoAmount = 0.35;
            p.lfo.rateHz = 0.3;
            p.lfoPitch1 = 0.25;
            p.lfoPitch2 = -0.25;
            p.levelDb = -2.0;
            room (p, 0.45, SpaceType::hall, 4.5);
            add ("Grain Cloud", "Texture", p, chord, false, 0.5);
        }
        {
            Patch p = base();   // KALEIDOSCOPE on strings: refracted, resonating, drifting.
            p.osc2FineCents = 8.0;
            p.tone = hz (1400.0);
            p.resonance = 0.15;
            p.envDepthSemitones = 14.0;
            filterEnv (p, 0.4, 1.2, 0.5, 1.4);
            ampEnv (p, 0.35, 1.0, 0.9, 1.6);
            p.reimagined = 0.65;
            p.reimaginedSettings.kaleidoscope = { 0.6, 0.7 };
            p.motion = 0.35;
            p.stereoSpread = 0.6;
            p.levelDb = 2.0;
            room (p, 0.35, SpaceType::hall, 3.5);
            add ("Kaleidoscope Pad", "Pad", p, chord);
        }
        {
            Patch p = base();   // TAPE FRAME: keys played back from a worn tape loop, MOVEMENT TAPE.
            p.osc1Wave = Osc1Wave::pulse;
            p.osc1Width = 0.35;
            p.osc2Wave = Osc2Wave::triangle;
            p.tone = hz (1800.0);
            p.envDepthSemitones = 20.0;
            filterEnv (p, 0.004, 0.8, 0.3, 0.6);
            ampEnv (p, 0.004, 1.6, 0.5, 0.8);
            p.granular = true;
            p.grains = { 0.2, 0.18, 18.0, 0.0, 0.15 };
            p.reimagined = 0.6;
            p.reimaginedSettings.mode = osp::ReimaginedMode::tapeFrame;
            p.reimaginedSettings.tapeFrame = { 0.5, 0.4, osp::TapeFrameLength::classic };
            p.motion = 0.3;
            p.effects.movementMode = osp::MovementMode::tape;
            p.levelDb = 3.0;
            room (p, 0.25, SpaceType::plate, 2.0);
            add ("Tape Frame Keys", "Keys", p, chord);
        }
        {
            Patch p = base();   // TOYBOX: a small digital keyboard turning its memory over.
            p.osc1Wave = Osc1Wave::pulse;
            p.osc1Width = 0.2;
            p.osc2Wave = Osc2Wave::pulse;
            p.osc2Octave = 1;
            p.osc2Width = 0.3;
            p.blend = 0.4;
            p.tone = hz (5000.0);
            p.envDepthSemitones = 12.0;
            filterEnv (p, 0.002, 0.5, 0.2, 0.4);
            ampEnv (p, 0.002, 1.2, 0.0, 1.0);
            p.reimagined = 0.7;
            p.reimaginedSettings.mode = osp::ReimaginedMode::toybox;
            p.reimaginedSettings.toybox = { 0.4, 0.6, osp::ToyboxPlay::turn };
            p.levelDb = -2.0;
            delay (p, 0.25, 5, 0.4, EchoType::bbd);
            room (p, 0.2, SpaceType::room, 1.2);
            add ("Toybox Chime", "Keys", p, { 72, 76, 79, 84 });
        }
        {
            Patch p = base();   // MIRAGE: an early sampler's heavy voice, then TUBE DRIVE.
            p.voiceMode = VoiceMode::mono;
            p.osc2Octave = -1;
            p.osc2FineCents = 4.0;
            p.tone = hz (900.0);
            p.resonance = 0.25;
            p.envDepthSemitones = 30.0;
            filterEnv (p, 0.002, 0.35, 0.2, 0.25);
            ampEnv (p, 0.002, 0.9, 0.7, 0.2);
            p.reimagined = 0.65;
            p.reimaginedSettings.mode = osp::ReimaginedMode::mirage;
            p.reimaginedSettings.mirage = { 0.55, 0.6, osp::MirageTone::dark };
            p.drive = 0.35;
            p.space = 0.0;
            p.levelDb = 5.0;
            add ("Mirage Bass", "Bass", p, lowLine, true);
        }
        {
            Patch p = base();   // MOVEMENT CHORUS: a wide string ensemble.
            p.osc1Wave = Osc1Wave::saw;
            p.osc2Wave = Osc2Wave::saw;
            p.osc2FineCents = 5.0;
            p.tone = hz (2600.0);
            p.envDepthSemitones = 6.0;
            filterEnv (p, 0.3, 1.0, 0.7, 1.2);
            ampEnv (p, 0.3, 1.0, 0.9, 1.3);
            p.motion = 0.6;
            p.effects.movementMode = osp::MovementMode::chorus;
            p.effects.chorusWidth = 0.7;
            p.effects.chorusStereo = 0.8;
            p.levelDb = 0.0;
            room (p, 0.3, SpaceType::hall, 3.0);
            add ("Chorus Ensemble", "Pad", p, chord);
        }
        {
            Patch p = base();   // MOVEMENT PULSE: an organ through a rotating tremolo.
            p.osc1Wave = Osc1Wave::pulse;
            p.osc1Width = 0.5;
            p.osc2Wave = Osc2Wave::pulse;
            p.osc2Octave = 1;
            p.osc2FineCents = 0.0;
            p.tone = hz (3200.0);
            p.envDepthSemitones = 0.0;
            ampEnv (p, 0.01, 0.2, 1.0, 0.12);
            p.motion = 0.5;
            p.effects.movementMode = osp::MovementMode::pulse;
            p.effects.pulseRate = 0.6;
            p.effects.pulseShape = 0.4;
            p.levelDb = -5.0;
            room (p, 0.2, SpaceType::room, 1.4);
            add ("Pulse Organ", "Keys", p, chord);
        }
        {
            Patch p = base();   // MOVEMENT SHAPER: a pad cut into a sixteenth-note rhythm.
            p.osc2FineCents = 10.0;
            p.tone = hz (1600.0);
            p.resonance = 0.3;
            p.envDepthSemitones = 10.0;
            ampEnv (p, 0.05, 1.0, 0.9, 0.6);
            p.motion = 0.85;
            p.effects.movementMode = osp::MovementMode::shaper;
            p.effects.shaper.pattern = 5;
            p.effects.shaper.target = osp::ShaperTarget::both;
            p.levelDb = 0.0;
            delay (p, 0.2, 5, 0.35);
            room (p, 0.25, SpaceType::plate, 2.2);
            add ("Shaper Gate", "Sequence", p, chord);
        }
        {
            Patch p = base();   // DRIVE CRUNCH: a synced lead pushed hard.
            p.voiceMode = VoiceMode::mono;
            p.glideSeconds = 0.04;
            p.osc1Coarse = 7.0;
            p.sync = true;
            p.tone = hz (2400.0);
            p.resonance = 0.2;
            p.envDepthSemitones = 16.0;
            filterEnv (p, 0.002, 0.4, 0.5, 0.3);
            ampEnv (p, 0.002, 0.5, 0.85, 0.25);
            p.drive = 0.55;
            p.effects.driveMode = osp::DriveMode::crunch;
            p.effects.driveTone = 0.6;
            p.levelDb = 2.0;
            delay (p, 0.2, 5, 0.35);
            add ("Crunch Lead", "Lead", p, leadLine, true);
        }
        {
            Patch p = base();   // GRANULAR an octave up, each key its own: frozen glass.
            p.osc1Wave = Osc1Wave::pulse;
            p.osc1Width = 0.3;
            p.osc2Wave = Osc2Wave::triangle;
            p.tone = hz (4000.0);
            p.envDepthSemitones = 0.0;
            ampEnv (p, 0.9, 1.5, 0.9, 3.0);
            p.granular = true;
            p.grains = { 0.85, 0.4, 12.0, 12.0, 0.3 };
            p.reimagined = 0.3;
            p.levelDb = -4.0;
            room (p, 0.55, SpaceType::hall, 6.0);
            delay (p, 0.2, 7, 0.5);
            add ("Frozen Glass", "Texture", p, { 60, 67, 72 }, false, 0.6);
        }

        // State version 4: the oscillator models, the OTA filters, the chorus, ring and SUPER
        // SAW (after the 34 above, which stay exactly as they were: legacyFactoryCount).
        auto juno = [] (Patch& p, JunoVariant variant, int highPass) {
            p.filterModel = FilterModel::ota;
            p.otaVoicing = OtaVoicing::juno;
            p.junoVariant = variant;
            p.highPass = highPass;
            p.osc1Model = p.osc2Model = OscModel::dco;
        };
        {
            auto p = base();
            juno (p, JunoVariant::juno60, 1);
            p.dco1 = { true, false, 0.0, PwmSource::manual, 0.0 };
            p.osc2Level = 0.0;
            p.tone = hz (2600.0);
            p.resonance = 0.1;
            p.envDepthSemitones = 6.0;
            ampEnv (p, 0.35, 1.0, 0.9, 1.2);
            p.chorusMode = 1;
            p.lfoAmount = 0.3;
            p.lfoDelaySeconds = 0.6;
            p.lfoRiseSeconds = 0.8;
            p.lfoPitch1 = 0.12;
            room (p, 0.3, SpaceType::hall, 3.5);
            add ("Juno Strings", "Pad", p, chord);
        }
        {
            auto p = base();
            juno (p, JunoVariant::juno106, 0);   // the 106's bass boost
            p.dco1 = { false, true, 0.8, PwmSource::manual, 0.0 };
            p.osc1Width = 0.42;
            p.osc2Level = 0.0;
            p.tone = hz (450.0);
            p.resonance = 0.35;
            p.envDepthSemitones = 30.0;
            filterEnv (p, 0.002, 0.22, 0.1, 0.12);
            p.vcaGate = true;
            p.space = 0.0;
            p.voiceMode = VoiceMode::mono;
            add ("DCO Bass 106", "Bass", p, lowLine, true);
        }
        {
            auto p = base();
            juno (p, JunoVariant::juno60, 1);
            p.dco1 = { false, true, 0.3, PwmSource::lfo, 0.75 };
            p.dco2 = { false, true, 0.0, PwmSource::lfo, 0.6 };
            p.osc2Coarse = 12.0;
            p.osc2FineCents = 0.0;
            p.osc2Level = 0.5;
            p.lfo.rateHz = 0.7;
            p.tone = hz (3000.0);
            p.envDepthSemitones = 0.0;
            ampEnv (p, 0.5, 1.0, 1.0, 1.5);
            p.chorusMode = 2;
            room (p, 0.35, SpaceType::hall, 4.5);
            add ("PWM Choir", "Pad", p, chord);
        }
        {
            auto p = base();
            juno (p, JunoVariant::juno106, 1);
            p.dco1 = { true, false, 0.0, PwmSource::manual, 0.0 };
            p.osc2Level = 0.0;
            p.tone = hz (700.0);
            p.resonance = 0.15;
            p.envDepthSemitones = 34.0;
            filterEnv (p, 0.09, 0.6, 0.45, 0.3);
            p.envLink = true;   // one envelope for both, as a 106
            p.chorusMode = 1;
            room (p, 0.25, SpaceType::room, 1.8);
            add ("Brass Section 106", "Brass", p, chord);
        }
        {
            auto p = base();
            p.osc1Model = p.osc2Model = OscModel::wide;
            p.wide1 = { WideWave::saw, 0.5, 0.0 };
            p.wide2 = { WideWave::saw, 0.5, 0.0 };
            p.osc2FineCents = 9.0;
            p.osc2Level = 0.9;
            p.filterModel = FilterModel::ota;
            p.otaVoicing = OtaVoicing::jupiter;
            p.tone = hz (900.0);
            p.resonance = 0.2;
            p.envDepthSemitones = 30.0;
            filterEnv (p, 0.06, 0.7, 0.5, 0.4);
            ampEnv (p, 0.03, 0.8, 0.85, 0.5);
            room (p, 0.25, SpaceType::hall, 2.5);
            add ("Jupiter Brass", "Brass", p, chord);
        }
        {
            auto p = base();
            p.osc1Model = OscModel::wide;
            p.wide1 = { WideWave::superSaw, 0.7, 0.45 };
            p.osc2Level = 0.0;
            p.filterModel = FilterModel::ota;
            p.otaVoicing = OtaVoicing::jupiter;
            p.tone = hz (5000.0);
            p.resonance = 0.15;
            p.envDepthSemitones = 8.0;
            p.voiceMode = VoiceMode::mono;
            p.glideSeconds = 0.04;
            p.lfoAmount = 0.15;
            delay (p, 0.25, 7, 0.35);
            room (p, 0.2, SpaceType::hall, 2.5);
            add ("Super Saw Lead", "Lead", p, leadLine, true);
        }
        {
            auto p = base();
            p.osc1Model = p.osc2Model = OscModel::wide;
            p.wide1 = { WideWave::superSaw, 0.6, 0.7 };
            p.wide2 = { WideWave::superSaw, 0.6, 0.5 };
            p.osc2Coarse = 12.0;
            p.osc2FineCents = 0.0;
            p.osc2Level = 0.45;
            p.filterModel = FilterModel::ota;
            p.otaVoicing = OtaVoicing::jupiter;
            p.otaTwoPole = true;
            p.tone = hz (4200.0);
            p.envDepthSemitones = 0.0;
            ampEnv (p, 0.25, 1.0, 1.0, 1.2);
            p.levelDb = -3.0;
            room (p, 0.35, SpaceType::hall, 5.0);
            add ("Trance Stack", "Pad", p, chord);
        }
        {
            auto p = base();
            p.osc1Model = OscModel::wide;
            p.wide1 = { WideWave::triangle, 0.6, 0.0 };
            p.osc2Wave = Osc2Wave::pulse;
            p.osc2Coarse = 7.0;
            p.osc2FineCents = 3.0;
            p.osc2Level = 0.8;
            p.ringMix = 0.8;
            p.tone = hz (6000.0);
            p.envDepthSemitones = 0.0;
            ampEnv (p, 0.002, 1.6, 0.0, 1.4);
            room (p, 0.3, SpaceType::hall, 3.0);
            delay (p, 0.15, 6, 0.3);
            add ("Ring Bell", "Keys", p, chord);
        }
        {
            auto p = base();
            p.osc1Model = OscModel::wide;
            p.wide1 = { WideWave::saw, 0.85, 0.0 };
            p.osc2Model = OscModel::dco;
            p.dco2 = { false, true, 0.0, PwmSource::manual, 0.0 };
            p.osc1Coarse = 7.0;
            p.osc2Level = 0.3;
            p.sync = true;
            p.polyModEnv = 0.55;
            p.polyModToPitch1 = true;
            p.tone = hz (3800.0);
            p.resonance = 0.2;
            filterEnv (p, 0.002, 0.5, 0.3, 0.3);
            p.voiceMode = VoiceMode::mono;
            delay (p, 0.2, 7, 0.3);
            add ("Sync Wide", "Lead", p, leadLine, true);
        }
        {
            auto p = base();
            p.osc2Model = OscModel::dco;
            p.dco2 = { true, false, 0.6, PwmSource::manual, 0.0 };
            p.osc2FineCents = 0.0;
            p.osc2Octave = -1;
            p.osc2Level = 0.5;
            p.filterModel = FilterModel::ota;
            p.tone = hz (1800.0);
            p.envDepthSemitones = 18.0;
            filterEnv (p, 0.002, 0.7, 0.2, 0.4);
            ampEnv (p, 0.003, 1.2, 0.4, 0.5);
            p.chorusMode = 3;
            p.noiseColor = NoiseColor::juno;
            p.noiseLevel = 0.06;
            room (p, 0.25, SpaceType::room, 1.5);
            add ("Hybrid Keys", "Keys", p, chord);
        }
        return list;
    }
}

osp::keydna::Settings keyDnaOf (const FactoryPatch& f)
{
    osp::keydna::Settings k;
    k.enabled = f.keyDna > 0.0;
    k.seed = f.patch.seed;
    using osp::keydna::Param;
    for (const auto param : { Param::position, Param::size, Param::density, Param::spread })
        k[param].variation = f.keyDna > 0.0 ? f.keyDna : 0.35;
    return k;
}

const std::vector<FactoryPatch>& factoryPatches()
{
    static const std::vector<FactoryPatch> list = build();
    return list;
}

} // namespace andor::analog
