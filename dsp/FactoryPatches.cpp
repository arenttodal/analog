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

    const std::vector<int> chord { 48, 55, 60, 64, 67 };
    const std::vector<int> lowLine { 36, 36, 43, 39, 41, 36, 46, 43 };
    const std::vector<int> leadLine { 60, 63, 67, 70, 72, 70, 67, 65 };
    const std::vector<int> arpLine { 60, 64, 67, 72, 67, 64, 60, 55 };

    std::vector<FactoryPatch> build()
    {
        std::vector<FactoryPatch> list;
        auto add = [&list] (const char* name, const char* category, Patch p, std::vector<int> notes, bool line = false) {
            list.push_back ({ name, category, p, std::move (notes), line });
        };

        {
            Patch p;   // INIT: two saws a few cents apart, the filter half open.
            add ("Init", "Basic", p, chord);
        }
        {
            Patch p;
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
            Patch p;
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
            Patch p;
            p.osc1Wave = Osc1Wave::pulse;
            p.osc1Width = 0.36;
            p.osc2FineCents = 9.0;
            p.tone = hz (2600.0);
            p.resonance = 0.08;
            p.envDepthSemitones = 6.0;
            filterEnv (p, 0.6, 1.5, 0.6, 1.5);
            ampEnv (p, 0.85, 1.0, 1.0, 1.9);
            p.movement = 0.35;
            p.lfoRateHz = 0.35;
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
            Patch p;
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
            Patch p;
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
            Patch p;
            p.voiceMode = VoiceMode::mono;
            p.osc2FineCents = 8.0;
            p.tone = hz (1800.0);
            p.resonance = 0.3;
            p.envDepthSemitones = 16.0;
            filterEnv (p, 0.004, 0.5, 0.55, 0.25);
            ampEnv (p, 0.004, 0.3, 0.9, 0.22);
            p.glideSeconds = 0.08;
            p.drive = 0.3;
            p.lfoPitch1 = 0.35;
            p.lfoPitch2 = 0.35;
            p.lfoWidth1 = 0.0;
            p.lfoWidth2 = 0.0;
            p.lfoRateHz = 5.2;
            delay (p, 0.22, 5, 0.35);
            room (p, 0.14, SpaceType::plate, 1.6);
            add ("Mono Lead", "Lead", p, leadLine, true);
        }
        {
            Patch p;
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
            Patch p;
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
            p.drive = 0.2;
            delay (p, 0.18, 3, 0.3);
            add ("Sync Bite", "Lead", p, leadLine, true);
        }
        {
            Patch p;
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
            Patch p;
            p.osc2FineCents = 15.0;
            p.osc1Wave = Osc1Wave::sawPulse;
            p.osc1Width = 0.42;
            p.tone = hz (1900.0);
            p.resonance = 0.1;
            p.envDepthSemitones = 4.0;
            filterEnv (p, 1.2, 2.0, 0.6, 2.0);
            ampEnv (p, 1.1, 1.0, 1.0, 2.4);
            p.analogAmount = 1.0;
            p.movement = 0.25;
            p.lfoRateHz = 0.18;
            p.lfoPitch1 = 0.12;
            p.lfoPitch2 = -0.12;
            p.lfoCutoff = 7.0;
            p.stereoSpread = 0.9;
            room (p, 0.42, SpaceType::hall, 5.0);
            delay (p, 0.14, 8, 0.4);
            add ("Detuned Haze", "Pad", p, chord);
        }
        {
            Patch p;
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
            Patch p;
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
            Patch p;
            p.osc2FineCents = 4.0;
            p.tone = hz (380.0);
            p.resonance = 0.16;
            p.envDepthSemitones = 38.0;
            filterEnv (p, 0.002, 0.24, 0.2, 0.16);
            ampEnv (p, 0.002, 0.3, 0.55, 0.16);
            p.velocityToFilter = 0.6;
            p.drive = 0.15;
            room (p, 0.14, SpaceType::room, 1.0);
            add ("Brass Stab", "Brass", p, chord);
        }
        {
            Patch p;
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
            Patch p;
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
            Patch p;
            p.osc2FineCents = 10.0;
            p.tone = hz (420.0);
            p.resonance = 0.42;
            p.envDepthSemitones = 8.0;
            filterEnv (p, 1.0, 2.0, 0.5, 2.0);
            ampEnv (p, 0.7, 1.0, 1.0, 2.2);
            p.lfoSync = true;
            p.lfoDivision = 3;   // a bar
            p.lfoWave = LfoWave::sine;
            p.movement = 0.55;
            p.lfoPitch1 = 0.0;
            p.lfoPitch2 = 0.0;
            p.lfoWidth1 = 0.0;
            p.lfoWidth2 = 0.0;
            p.lfoCutoff = 24.0;
            room (p, 0.38, SpaceType::hall, 4.0);
            add ("Sweep Pad", "Pad", p, chord);
        }
        {
            Patch p;
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
            Patch p;
            p.osc1Wave = Osc1Wave::pulse;
            p.osc2Wave = Osc2Wave::pulse;
            p.osc2Octave = 1;
            p.osc2FineCents = 1.0;
            p.osc2Level = 0.6;
            p.tone = hz (4200.0);
            p.envDepthSemitones = 0.0;
            ampEnv (p, 0.004, 0.2, 1.0, 0.06);
            p.movement = 0.2;
            p.lfoRateHz = 6.0;
            p.lfoPitch1 = 0.12;
            p.lfoPitch2 = 0.12;
            p.lfoWidth1 = 0.0;
            p.lfoWidth2 = 0.0;
            p.levelDb = -5.0;
            room (p, 0.2, SpaceType::room, 1.6);
            add ("Hollow Organ", "Keys", p, chord);
        }
        {
            Patch p;
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
            p.drive = 0.45;
            p.driveBias = 0.4;
            delay (p, 0.42, 6, 0.55);
            room (p, 0.18, SpaceType::plate, 2.0);
            add ("Tape Echo Lead", "Lead", p, leadLine, true);
        }
        {
            Patch p;
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
            Patch p;
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
            Patch p;
            p.osc1Octave = -1;
            p.osc2Octave = -1;
            p.osc2FineCents = 12.0;
            p.tone = hz (650.0);
            p.resonance = 0.3;
            p.envDepthSemitones = 26.0;
            filterEnv (p, 0.35, 1.4, 0.3, 1.2);
            ampEnv (p, 0.25, 1.0, 0.9, 1.4);
            p.movement = 0.3;
            p.lfoWave = LfoWave::sampleHold;
            p.lfoRateHz = 7.0;
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
            Patch p;
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
        return list;
    }
}

const std::vector<FactoryPatch>& factoryPatches()
{
    static const std::vector<FactoryPatch> list = build();
    return list;
}

} // namespace andor::analog
