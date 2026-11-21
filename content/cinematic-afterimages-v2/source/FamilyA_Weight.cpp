// FAMILY A - FUNDAMENTAL WEIGHT (001-010): fat, musical analog basses. A stable, mono-compatible
// fundamental (triangle, soft pulse, SUB), harmonics only where they help, no stereo below the
// bass, no reverb to hide in.

#include "Common.h"

namespace ca2
{

void addWeight (std::vector<Spec>& l)
{
    const auto F = Family::weight;

    l.push_back ({ 1, "Deep Current", F,
                   "Pure low-frequency weight with a soft harmonic edge.",
                   "Play single notes below C3; hold them.",
                   "Sustained cinematic bass notes", "The cleanest of the family: a triangle body, the saw only a whisper above it.",
                   28, 55, Phrase::bassLine,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.28;   // (the upper harmonic: reinforcement only)
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2Level = 1.0;
                       p.osc2FineCents = 0.0;
                       sub (p, SubWave::sine, -1, -12.0, 36, 60);
                       p.tone = hz (320.0);
                       p.resonance = 0.08;
                       p.keyTrack = 0.6;
                       filterEnv (p, 0.004, 0.6, 0.35, 0.4, 10.0);
                       ampEnv (p, 0.004, 0.9, 1.0, 0.35);
                       p.velocityToFilter = 0.35;
                       p.velocityRangeDb = 10.0;
                       mono (p, true, 0.03);
                       drive (p, DriveMode::tape, 0.18, 0.4, 0.65);
                       p.analogAmount = 0.2;
                   },
                   [] (ShellProcessor& s) { route (s, 0, Source::cc1, Dest::cutoff, 30.0f); } });

    l.push_back ({ 2, "Stone Current", F,
                   "A heavy, rounded pulse bass with a short resonant snap at the start of every note.",
                   "Play detached notes; every note restarts its snap.",
                   "Rhythmic foundation bass", "A pulse with a true square sub, retriggered with LEGATO off: the resonant blip is its signature, the release tight.",
                   28, 52, Phrase::bassLine,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.4;
                       p.osc1Level = 1.0;
                       p.osc2Level = 0.0;
                       sub (p, SubWave::square, -1, -13.0, 28, 60);
                       p.tone = hz (420.0);
                       p.resonance = 0.36;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.001, 0.12, 0.2, 0.1, 22.0);
                       ampEnv (p, 0.002, 0.4, 0.85, 0.12);
                       p.velocityToFilter = 0.4;
                       p.velocityRangeDb = 10.0;
                       mono (p, false);
                       p.analogAmount = 0.45;
                   },
                   [] (ShellProcessor& s) { route (s, 0, Source::cc1, Dest::cutoff, 35.0f); } });

    l.push_back ({ 3, "Velvet Gravity", F,
                   "A creamy, expensive mono bass for singing low melodies.",
                   "Play legato lines; the glide connects them. The wheel opens it.",
                   "Melodic low lead-bass", "Three gently detuned saws (one an octave down) through a warmly driven ladder: the classic three-oscillator mono, smooth not aggressive.",
                   28, 60, Phrase::bassLine,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.8;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = -6.0;
                       p.osc2Level = 0.7;
                       osc3 (p, Osc2Wave::saw, -1, 0.0, 4.0, -10.0);
                       p.tone = hz (520.0);
                       p.resonance = 0.28;
                       p.keyTrack = 0.55;
                       p.filterDrive = 0.25;
                       filterEnv (p, 0.01, 0.7, 0.45, 0.3, 18.0);
                       ampEnv (p, 0.004, 1.0, 0.9, 0.25);
                       p.velocityToFilter = 0.45;
                       p.velocityRangeDb = 12.0;
                       mono (p, true, 0.07);
                       p.analogAmount = 0.4;
                   },
                   [] (ShellProcessor& s) { route (s, 0, Source::cc1, Dest::cutoff, 40.0f); } });

    l.push_back ({ 4, "Blackwater Monolith", F,
                   "A dense, dark mono bass with tremendous body.",
                   "Hold long low notes; play softly for darkness, harder for a little edge.",
                   "Weight under a cue", "Saw, pulse and triangle all at the fundamental, closely tuned, behind a nearly closed filter and a round tube stage.",
                   24, 50, Phrase::bassLine,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.7;
                       p.osc2Wave = Osc2Wave::pulse;
                       p.osc2Width = 0.5;
                       p.osc2FineCents = 4.0;
                       p.osc2Level = 0.7;
                       osc3 (p, Osc2Wave::triangle, 0, 0.0, -5.0, -6.0);
                       sub (p, SubWave::sine, -1, -14.0, 36, 60);
                       p.tone = hz (260.0);
                       p.resonance = 0.12;
                       p.keyTrack = 0.45;
                       p.filterDrive = 0.3;
                       filterEnv (p, 0.006, 0.5, 0.4, 0.3, 10.0);
                       ampEnv (p, 0.006, 0.9, 1.0, 0.3);
                       p.velocityToFilter = 0.5;
                       p.velocityRangeDb = 9.0;
                       mono (p, true, 0.02);
                       drive (p, DriveMode::tube, 0.2, 0.35, 0.8);
                       p.analogAmount = 0.3;
                   },
                   nullptr });

    l.push_back ({ 5, "Ironwood", F,
                   "A woody, plucked bass, almost acoustic in its response.",
                   "Play short notes with varied velocity, like a picked upright.",
                   "Plucked walking bass", "A triangle body plucked by a very fast filter decay; an asymmetric drive adds the wood.",
                   28, 60, Phrase::bassLine,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.3;
                       p.osc1Level = 0.3;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 1.0;
                       p.tone = hz (600.0);
                       p.resonance = 0.2;
                       p.keyTrack = 0.7;
                       p.filterDrive = 0.25;
                       p.driveBias = 0.35;
                       filterEnv (p, 0.001, 0.14, 0.0, 0.1, 30.0);
                       ampEnv (p, 0.001, 0.55, 0.0, 0.2);
                       p.velocityToFilter = 0.6;
                       p.velocityRangeDb = 16.0;
                       mono (p, false);
                       p.analogAmount = 0.35;
                   },
                   nullptr });

    l.push_back ({ 6, "Low Orbit", F,
                   "An extremely clean modern sub with a faint saw hovering above it.",
                   "Soft notes stay pure; dig in to bring out the articulation.",
                   "Modern sub foundation", "Nearly a sine at low velocity; velocity alone opens the hovering saw, which drifts very slightly.",
                   24, 52, Phrase::bassLine,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.18;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 1.0;
                       p.tone = hz (170.0);
                       p.resonance = 0.05;
                       p.keyTrack = 0.6;
                       filterEnv (p, 0.003, 0.35, 0.15, 0.3, 24.0);
                       p.velocityToFilter = 1.0;
                       ampEnv (p, 0.003, 1.0, 1.0, 0.4);
                       p.velocityRangeDb = 8.0;
                       p.lfo.shape = osp::mod::LfoShape::sine;
                       p.lfo.rateHz = 0.3;
                       p.lfoAmount = 1.0;
                       p.lfoPitch1 = 0.06;
                       mono (p, true, 0.04);
                       p.analogAmount = 0.1;
                   },
                   nullptr });

    l.push_back ({ 7, "Earthmover", F,
                   "A massive, punchy cinematic bass that hits like a wall.",
                   "Short, strong notes; let the punch land.",
                   "Impact bass for action cues", "Octave-stacked saws and a square sub, a pronounced filter strike and an amp that clamps to 80 % at once - built-in compression feel.",
                   24, 52, Phrase::bassLine,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.9;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2Octave = -1;
                       p.osc2FineCents = 3.0;
                       p.osc2Level = 0.8;
                       unisonCard (p, 0, 3, 0.15, 0.5, 0.0);   // (thick, not wide: mono-safe)
                       sub (p, SubWave::square, -1, -14.0, 28, 60);
                       p.tone = hz (350.0);
                       p.resonance = 0.2;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.001, 0.22, 0.25, 0.15, 34.0);
                       ampEnv (p, 0.001, 0.3, 0.8, 0.2);
                       p.velocityToFilter = 0.45;
                       p.velocityRangeDb = 10.0;
                       mono (p, false);
                       drive (p, DriveMode::tube, 0.3, 0.45, 0.7);
                       p.analogAmount = 0.3;
                   },
                   nullptr });

    l.push_back ({ 8, "Coal Engine", F,
                   "A slow, rolling bass with a pulse moving inside it.",
                   "Hold notes and let the engine roll; slow lines work best.",
                   "Moving ostinato bass", "Narrow pulses whose width and the dark ladder both breathe with the LFO, worn by a little tape instability.",
                   24, 55, Phrase::bassLine,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.2;
                       p.osc1Level = 0.9;
                       p.osc2Wave = Osc2Wave::pulse;
                       p.osc2Width = 0.25;
                       p.osc2FineCents = -5.0;
                       p.osc2Level = 0.6;
                       p.lfo.shape = osp::mod::LfoShape::triangle;
                       p.lfo.rateHz = 3.0;
                       p.lfoAmount = 1.0;
                       p.lfoWidth1 = 0.18;
                       p.lfoWidth2 = -0.15;
                       p.lfoCutoff = 7.0;
                       p.tone = hz (300.0);
                       p.resonance = 0.3;
                       p.keyTrack = 0.45;
                       filterEnv (p, 0.004, 0.6, 0.4, 0.3, 12.0);
                       ampEnv (p, 0.004, 0.8, 0.9, 0.3);
                       p.velocityRangeDb = 10.0;
                       mono (p, true, 0.03);
                       tape (p, 0.3, 0.45, 0.3, 0.5);
                       p.analogAmount = 0.45;
                   },
                   nullptr });

    l.push_back ({ 9, "Lunar Pressure", F,
                   "A focused bass core surrounded by a wide halo of upper harmonics.",
                   "Hold notes; the centre stays put while the halo spreads.",
                   "Wide modern bass for scores", "Only the upper octave is wide (card A's five copies); the triangle core and the SUB stay mono.",
                   28, 55, Phrase::bassLine,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Octave = 1;
                       p.osc1Level = 0.38;
                       unisonCard (p, 0, 5, 0.3, 0.7, 1.0);
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 1.0;
                       sub (p, SubWave::sine, -1, -13.0, 36, 60);
                       p.tone = hz (900.0);
                       p.resonance = 0.1;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.01, 1.0, 0.6, 0.5, 8.0);
                       ampEnv (p, 0.01, 1.0, 1.0, 0.5);
                       p.velocityRangeDb = 9.0;
                       mono (p, true, 0.03);
                       room (p, SpaceType::room, 0.06, 0.8, 4.0, 6000.0, 300.0);
                       p.analogAmount = 0.25;
                   },
                   nullptr });

    l.push_back ({ 10, "Deepest Thread", F,
                   "A soft, almost sine-like bass that slowly opens and lingers.",
                   "Hold long notes or low chords under strings and pads.",
                   "Ambient / orchestral underpinning", "Starts as a pure tone; a seconds-long filter attack reveals its harmonics; a long release lets it fade like a held breath.",
                   24, 55, Phrase::drone,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.2;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 1.0;
                       p.tone = hz (150.0);
                       p.resonance = 0.08;
                       p.keyTrack = 0.5;
                       filterEnv (p, 2.5, 4.0, 0.8, 2.5, 14.0);
                       ampEnv (p, 0.03, 1.0, 1.0, 2.5);
                       p.velocityRangeDb = 6.0;
                       p.polyphony = 4;
                       room (p, SpaceType::room, 0.08, 2.0, 10.0, 3000.0, 200.0);
                       p.analogAmount = 0.2;
                   },
                   nullptr });
}

} // namespace ca2
