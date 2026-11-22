// FAMILY I - DARK MATTER (081-090): drones, tension and noir atmospheres - evolving, musical
// instruments even when one note is held for minutes.

#include "Common.h"

namespace ca2
{

void addDarkMatter (std::vector<Spec>& l)
{
    const auto F = Family::darkMatter;

    l.push_back ({ 81, "Signal Grave", F,
                   "A deep, ominous drone with faint radio static and slow, uneasy pitch.",
                   "Hold one low note or a fifth; let it evolve.",
                   "Tension underscore", "Low saw and detuned triangle wander in pitch under a breath of filtered noise, in a dark endless space.",
                   24, 60, Phrase::drone,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Octave = -1;
                       p.osc1Level = 0.7;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2Octave = -1;
                       p.osc2FineCents = -9.0;
                       p.osc2Level = 0.8;
                       noiseC (p, NoiseColor::juno, 0.35, 0.55, -24.0);
                       p.tone = hz (380.0);
                       p.resonance = 0.3;
                       p.keyTrack = 0.3;
                       filterEnv (p, 2.0, 6.0, 0.6, 4.0, 8.0);
                       ampEnv (p, 2.5, 3.0, 1.0, 4.0);
                       p.velocityRangeDb = 6.0;
                       p.lfo.shape = osp::mod::LfoShape::sine;
                       p.lfo.rateHz = 0.17;
                       p.lfoAmount = 1.0;
                       p.lfoPitch1 = 0.08;
                       p.lfoCutoff = 6.0;
                       p.polyphony = 4;
                       drift (p, 0.35, 0.4, 0.5, 0.5);
                       abyss (p, AbyssVariant::dark, 0.38, 8.0, 40.0, 4000.0);
                       p.analogAmount = 0.6;
                   },
                   nullptr });

    l.push_back ({ 82, "Frosted Air", F,
                   "Cold, high air moving through an endless frozen space.",
                   "Hold one or two notes; the air keeps shifting on its own.",
                   "Icy air drone", "White noise on card C over a quiet triangle, high-passed, through the Jupiter OTA into an ENDLESS ABYSS.",
                   48, 84, Phrase::drone,
                   [] (Patch& p) {
                       p.osc1Level = 0.0;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 0.9;
                       noiseC (p, NoiseColor::white, 0.7, 0.7, 0.0);
                       p.highPass = 2;
                       p.filterModel = FilterModel::ota;
                       p.otaVoicing = OtaVoicing::jupiter;
                       p.tone = hz (5000.0);
                       p.resonance = 0.3;
                       p.keyTrack = 0.5;
                       p.lfo.shape = osp::mod::LfoShape::sine;
                       p.lfo.rateHz = 0.08;
                       p.lfoAmount = 1.0;
                       p.lfoCutoff = 8.0;
                       filterEnv (p, 2.0, 3.0, 0.9, 4.0, 4.0);
                       ampEnv (p, 2.0, 3.0, 0.95, 5.0);
                       p.velocityRangeDb = 6.0;
                       p.polyphony = 4;
                       abyss (p, AbyssVariant::endless, 0.4, 3.0, 40.0, 7000.0);
                       p.analogAmount = 0.3;
                   },
                   nullptr });

    l.push_back ({ 83, "Void Bending", F,
                   "A resonant drone whose centre slowly bends and wavers in a reversed space.",
                   "Hold a low note; let the resonance roam.",
                   "Warping tension drone", "Two saws through a resonant ladder swept very slowly by the LFO, chorus movement, the INVERSE ABYSS.",
                   28, 64, Phrase::drone,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.8;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = 9.0;
                       p.osc2Level = 0.7;
                       p.tone = hz (600.0);
                       p.resonance = 0.6;
                       p.keyTrack = 0.4;
                       p.lfo.shape = osp::mod::LfoShape::triangle;
                       p.lfo.rateHz = 0.07;
                       p.lfoAmount = 1.0;
                       p.lfoCutoff = 18.0;
                       filterEnv (p, 1.5, 3.0, 0.8, 4.0, 6.0);
                       ampEnv (p, 1.5, 2.0, 0.95, 4.0);
                       p.velocityRangeDb = 6.0;
                       p.polyphony = 4;
                       chorusMovement (p, 0.4, 0.3, 0.6, 0.8);
                       abyss (p, AbyssVariant::inverse, 0.32, 8.0, 30.0, 4500.0);
                       p.analogAmount = 0.4;
                   },
                   nullptr });

    l.push_back ({ 84, "Inverted Chapel", F,
                   "A reversed-feeling swell: slow to arrive, quick to vanish.",
                   "Play chords and lift your hands before they fully bloom.",
                   "Reverse-swell pad", "A three-and-a-half-second amplitude rise with a short release, the filter rising over three seconds, a dark ABYSS.",
                   36, 76, Phrase::chords,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.75;
                       p.osc2Wave = Osc2Wave::pulse;
                       p.osc2Width = 0.4;
                       p.osc2FineCents = 6.0;
                       p.osc2Level = 0.6;
                       p.tone = hz (500.0);
                       p.resonance = 0.15;
                       p.keyTrack = 0.4;
                       filterEnv (p, 3.0, 2.0, 0.8, 0.5, 22.0);
                       ampEnv (p, 3.5, 1.0, 1.0, 0.4);
                       p.velocityRangeDb = 6.0;
                       p.polyphony = 8;
                       abyss (p, AbyssVariant::dark, 0.32, 8.0, 30.0, 4000.0);
                       p.analogAmount = 0.35;
                   },
                   nullptr });

    l.push_back ({ 85, "Blue Static", F,
                   "A cold triangle drone under a worn layer of static.",
                   "Hold a note or two; the static wears and wavers.",
                   "Noir static bed", "A triangle and white noise on card C through heavy tape wear and slow wow.",
                   36, 76, Phrase::drone,
                   [] (Patch& p) {
                       p.osc1Level = 0.0;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 0.9;
                       noiseC (p, NoiseColor::white, 0.55, 0.4, -16.0);
                       p.tone = hz (2200.0);
                       p.resonance = 0.1;
                       p.keyTrack = 0.5;
                       filterEnv (p, 1.0, 2.0, 0.9, 3.0, 4.0);
                       ampEnv (p, 1.2, 2.0, 0.95, 3.5);
                       p.velocityRangeDb = 6.0;
                       p.polyphony = 4;
                       tape (p, 0.5, 0.6, 0.4, 0.8);
                       room (p, SpaceType::hall, 0.25, 5.0, 30.0, 4500.0);
                       p.analogAmount = 0.35;
                   },
                   nullptr });

    l.push_back ({ 86, "Coal Dust Atmosphere", F,
                   "A dark, slowly pulsing industrial atmosphere.",
                   "Hold low notes; the pulse breathes slowly underneath.",
                   "Industrial dread", "Two dark saws with Juno noise, a slow pulse movement, tape drive for the dust.",
                   28, 60, Phrase::drone,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.8;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2Octave = -1;
                       p.osc2FineCents = 4.0;
                       p.osc2Level = 0.6;
                       p.noiseLevel = 0.15;
                       p.noiseColor = NoiseColor::juno;
                       p.tone = hz (450.0);
                       p.resonance = 0.2;
                       p.keyTrack = 0.4;
                       filterEnv (p, 1.0, 2.0, 0.8, 3.0, 8.0);
                       ampEnv (p, 1.2, 2.0, 0.95, 3.5);
                       p.velocityRangeDb = 6.0;
                       p.polyphony = 4;
                       pulse (p, 0.35, 0.25, 0.4, 0.4);
                       drive (p, DriveMode::tape, 0.4, 0.4, 0.6);
                       room (p, SpaceType::hall, 0.22, 4.5, 25.0, 4000.0);
                       p.analogAmount = 0.45;
                   },
                   nullptr });

    l.push_back ({ 87, "Ring of Ash", F,
                   "A clangorous, inharmonic drone, metallic and unsettled.",
                   "Hold single notes; each key gives a different metal.",
                   "Inharmonic tension", "Ring modulation of a triangle against a saw a tritone-ish six semitones and thirteen cents above it.",
                   36, 72, Phrase::drone,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Coarse = 6.0;
                       p.osc1Level = 0.4;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 13.0;
                       p.osc2Level = 0.9;
                       p.ringMix = 0.45;
                       p.tone = hz (1500.0);
                       p.resonance = 0.15;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.8, 2.0, 0.85, 3.0, 6.0);
                       ampEnv (p, 1.0, 2.0, 0.95, 3.5);
                       p.velocityRangeDb = 6.0;
                       p.polyphony = 4;
                       abyss (p, AbyssVariant::deep, 0.3, 8.0, 30.0, 4500.0);
                       p.analogAmount = 0.35;
                   },
                   nullptr });

    l.push_back ({ 88, "Deep Stairwell", F,
                   "A dissonant cluster sinking into an endless space.",
                   "Hold one low note; the cluster is built in.",
                   "Dissonant abyss", "A saw, a minor second above it, and a tritone on card C, into an ENDLESS ABYSS that fades over about twenty-five seconds.",
                   28, 60, Phrase::drone,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.75;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2Coarse = 1.0;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 0.5;
                       osc3 (p, Osc2Wave::triangle, 0, 6.0, 0.0, -9.0);
                       p.tone = hz (550.0);
                       p.resonance = 0.15;
                       p.keyTrack = 0.4;
                       filterEnv (p, 1.5, 3.0, 0.8, 4.0, 8.0);
                       ampEnv (p, 1.6, 2.0, 0.95, 4.0);
                       p.velocityRangeDb = 6.0;
                       p.polyphony = 4;
                       abyss (p, AbyssVariant::endless, 0.38, 3.0, 40.0, 4000.0);
                       p.analogAmount = 0.4;
                   },
                   nullptr });

    l.push_back ({ 89, "Forgotten Machine", F,
                   "An old, failing machine left running in an empty room.",
                   "Hold notes; the pitch sags and recovers like worn motors.",
                   "Decaying machinery", "A pulse and saw through tape wow and wear, the LFO letting the pitch drift slightly.",
                   36, 72, Phrase::drone,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.3;
                       p.osc1Level = 0.75;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = -8.0;
                       p.osc2Level = 0.55;
                       p.lfo.shape = osp::mod::LfoShape::sine;
                       p.lfo.rateHz = 0.2;
                       p.lfoAmount = 1.0;
                       p.lfoPitch1 = 0.04;
                       p.lfoPitch2 = 0.04;
                       p.tone = hz (900.0);
                       p.resonance = 0.2;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.6, 2.0, 0.8, 2.5, 8.0);
                       ampEnv (p, 0.8, 2.0, 0.95, 3.0);
                       p.velocityRangeDb = 6.0;
                       p.polyphony = 4;
                       tape (p, 0.55, 0.7, 0.45, 0.7);
                       room (p, SpaceType::room, 0.2, 2.0, 15.0, 4500.0);
                       p.analogAmount = 0.6;
                   },
                   nullptr });

    l.push_back ({ 90, "Rusted Sea", F,
                   "A slow, heaving ocean of noise and low tone.",
                   "Hold low notes for a long time; the waves roll on their own.",
                   "Oceanic drone", "Juno noise on card C with a wandering tone, a low saw, the filter heaved by a slow LFO.",
                   24, 60, Phrase::drone,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.7;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2Octave = -1;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 0.6;
                       noiseC (p, NoiseColor::juno, 0.5, 0.9, -14.0);
                       p.lfo.shape = osp::mod::LfoShape::sine;
                       p.lfo.rateHz = 0.12;
                       p.lfoAmount = 1.0;
                       p.lfoPitch1 = 0.2;
                       p.lfoPitch2 = 0.0;
                       p.lfoCutoff = 14.0;
                       p.tone = hz (500.0);
                       p.resonance = 0.15;
                       p.keyTrack = 0.4;
                       filterEnv (p, 1.5, 3.0, 0.8, 4.0, 6.0);
                       ampEnv (p, 1.8, 2.0, 0.95, 4.5);
                       p.velocityRangeDb = 6.0;
                       p.polyphony = 4;
                       abyss (p, AbyssVariant::deep, 0.32, 8.0, 30.0, 3500.0);
                       p.analogAmount = 0.4;
                   },
                   nullptr });
}

} // namespace ca2
