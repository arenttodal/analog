// FAMILY B - CORROSION (011-020): distorted, aggressive, industrial basses. The fundamental stays
// stable (SUB, a clean oscillator); the midrange carries the aggression (sync, ring, drive); the top
// carries the bite.

#include "Common.h"

namespace ca2
{

void addCorrosion (std::vector<Spec>& l)
{
    const auto F = Family::corrosion;

    l.push_back ({ 11, "Furnace Teeth", F,
                   "A distorted sync bass with a ferocious harmonic bite.",
                   "Try pitch bends and fast mono lines; the wheel opens the throat.",
                   "Aggressive driving bass", "Hard sync swept by its own envelope, so every note rips open and closes; a clean SUB holds the floor.",
                   28, 55, Phrase::bassLine,
                   [] (Patch& p) {
                       p.sync = true;
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Coarse = 12.0;   // (the synced slave an octave up: the sweep's range)
                       p.osc1Level = 0.75;
                       p.osc2Wave = Osc2Wave::pulse;
                       p.osc2Width = 0.42;
                       p.osc2Level = 0.55;
                       p.polyModEnv = 0.55;
                       p.polyModToPitch1 = true;
                       sub (p, SubWave::sine, -1, -9.0, 24, 60);
                       p.tone = hz (1500.0);
                       p.resonance = 0.24;
                       p.keyTrack = 0.5;
                       p.filterDrive = 0.4;
                       filterEnv (p, 0.002, 0.32, 0.38, 0.2, 20.0);
                       ampEnv (p, 0.002, 0.5, 0.9, 0.16);
                       p.velocityToFilter = 0.5;
                       p.velocityRangeDb = 12.0;
                       mono (p, true, 0.02);
                       p.bendRangeSemitones = 7.0;
                       drive (p, DriveMode::crunch, 0.45, 0.45, 0.6);
                       p.analogAmount = 0.25;
                   },
                   [] (ShellProcessor& s) { route (s, 0, Source::cc1, Dest::cutoff, 40.0f); } });

    l.push_back ({ 12, "Rust Circuit", F,
                   "A metallic, growling bass with tuned dissonance over a steady floor.",
                   "Hold notes and move the wheel; low fifths growl best.",
                   "Industrial menace bass", "Ring modulation against a fourth gives the rusted metal; a clean SUB carries the pitch underneath.",
                   28, 55, Phrase::bassLine,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.6;
                       p.osc2Wave = Osc2Wave::pulse;
                       p.osc2Width = 0.45;
                       p.osc2Coarse = 5.0;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 0.55;
                       p.ringMix = 0.55;
                       sub (p, SubWave::sine, -1, -10.0, 28, 60);
                       p.tone = hz (1100.0);
                       p.resonance = 0.3;
                       p.keyTrack = 0.5;
                       p.filterDrive = 0.5;
                       filterEnv (p, 0.003, 0.4, 0.5, 0.2, 16.0);
                       ampEnv (p, 0.003, 0.6, 0.9, 0.18);
                       p.velocityRangeDb = 10.0;
                       mono (p, true, 0.02);
                       drive (p, DriveMode::crunch, 0.35, 0.4, 0.6);
                       p.analogAmount = 0.3;
                   },
                   [] (ShellProcessor& s) { route (s, 0, Source::cc1, Dest::cutoff, 35.0f); } });

    l.push_back ({ 13, "Broken Reactor", F,
                   "An extremely aggressive bass: crunchy mids over compressed weight.",
                   "Short riffs and held power notes; keep the lines simple.",
                   "Heavy hybrid score bass", "Detuned stacked saws hit a hard-driven ladder and the CRUNCH stage; a gritty square SUB keeps the weight.",
                   26, 52, Phrase::bassLine,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.85;
                       unisonCard (p, 0, 3, 0.25, 0.6, 0.2);
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2Octave = -1;
                       p.osc2Level = 0.7;
                       sub (p, SubWave::square, -1, -11.0, 26, 60, 0.4);
                       p.tone = hz (800.0);
                       p.resonance = 0.2;
                       p.keyTrack = 0.45;
                       p.filterDrive = 0.7;
                       p.driveBias = 0.2;
                       filterEnv (p, 0.002, 0.3, 0.5, 0.15, 22.0);
                       ampEnv (p, 0.002, 0.4, 0.92, 0.15);
                       p.velocityRangeDb = 8.0;
                       mono (p, true, 0.015);
                       drive (p, DriveMode::crunch, 0.7, 0.55, 0.7);
                       p.analogAmount = 0.3;
                   },
                   nullptr });

    l.push_back ({ 14, "Corrosion Field", F,
                   "A grainy, toxic bass with a noise component that moves.",
                   "Hold notes and let the grain crawl; works for slow pulses.",
                   "Dystopian texture bass", "Filtered noise crawls through a low-passed saw; strong bias makes the drive asymmetric and dirty.",
                   26, 55, Phrase::bassLine,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.8;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2Level = 0.6;
                       p.noiseLevel = 0.32;
                       p.noiseColor = NoiseColor::juno;
                       sub (p, SubWave::sine, -1, -12.0, 26, 60);
                       p.tone = hz (450.0);
                       p.resonance = 0.35;
                       p.keyTrack = 0.4;
                       p.filterDrive = 0.45;
                       p.driveBias = 0.6;
                       p.lfo.shape = osp::mod::LfoShape::rampDown;
                       p.lfo.rateHz = 0.8;
                       p.lfoAmount = 1.0;
                       p.lfoCutoff = 6.0;
                       filterEnv (p, 0.004, 0.5, 0.5, 0.3, 12.0);
                       ampEnv (p, 0.004, 0.7, 0.95, 0.25);
                       p.velocityRangeDb = 8.0;
                       mono (p, true, 0.02);
                       drive (p, DriveMode::tube, 0.4, 0.4, 0.6);
                       p.analogAmount = 0.4;
                   },
                   nullptr });

    l.push_back ({ 15, "Blackcoil", F,
                   "A narrow-pulse bass with resonant teeth and an unstable vintage edge.",
                   "Medium-length notes; the wheel bares the teeth.",
                   "Gritty retro bass", "Two narrow pulses swept in opposite directions under a resonant ladder, aged by worn tape.",
                   28, 55, Phrase::bassLine,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.12;
                       p.osc1Level = 0.9;
                       p.osc2Wave = Osc2Wave::pulse;
                       p.osc2Width = 0.2;
                       p.osc2FineCents = -8.0;
                       p.osc2Level = 0.5;
                       p.lfo.shape = osp::mod::LfoShape::triangle;
                       p.lfo.rateHz = 0.6;
                       p.lfoAmount = 1.0;
                       p.lfoWidth1 = 0.25;
                       p.lfoWidth2 = -0.2;
                       sub (p, SubWave::sine, -1, -12.0, 28, 60);
                       p.tone = hz (500.0);
                       p.resonance = 0.55;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.002, 0.25, 0.3, 0.2, 20.0);
                       ampEnv (p, 0.002, 0.5, 0.9, 0.2);
                       p.velocityRangeDb = 10.0;
                       mono (p, true, 0.02);
                       tape (p, 0.35, 0.4, 0.4, 0.6);
                       drive (p, DriveMode::tape, 0.3, 0.45, 0.6);
                       p.analogAmount = 0.65;
                   },
                   [] (ShellProcessor& s) { route (s, 0, Source::cc1, Dest::cutoff, 35.0f); } });

    l.push_back ({ 16, "Voltage Wound", F,
                   "A screaming sync bass whose every note tears slowly upward.",
                   "Hold notes to let the scream climb; bend a full octave for leaps.",
                   "Climactic bass scream", "Sync whose slave is swept by Poly Mod's envelope over almost a second (Furnace Teeth's sweep is a snap); a twelve-semitone bend for leaps; a pulse and SUB keep the note clear.",
                   28, 60, Phrase::bassLine,
                   [] (Patch& p) {
                       p.sync = true;
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Coarse = 7.0;
                       p.osc1Level = 0.7;
                       p.osc2Wave = Osc2Wave::pulse;
                       p.osc2Width = 0.5;
                       p.osc2Level = 0.7;
                       p.polyModEnv = 0.65;
                       p.polyModToPitch1 = true;
                       sub (p, SubWave::sine, -1, -10.0, 28, 60);
                       p.tone = hz (2000.0);
                       p.resonance = 0.35;
                       p.keyTrack = 0.5;
                       p.filterDrive = 0.4;
                       filterEnv (p, 0.6, 0.9, 0.6, 0.3, 14.0);   // (a slow rise: the slave climbs as it opens)
                       ampEnv (p, 0.003, 0.6, 0.95, 0.2);
                       p.velocityRangeDb = 10.0;
                       mono (p, true, 0.03);
                       p.bendRangeSemitones = 12.0;
                       drive (p, DriveMode::crunch, 0.4, 0.5, 0.55);
                       p.analogAmount = 0.3;
                   },
                   [] (ShellProcessor& s) { route (s, 0, Source::cc1, Dest::cutoff, 30.0f); } });

    l.push_back ({ 17, "Charred Glass", F,
                   "Hard, brittle distorted harmonics shattering over a soft sub.",
                   "Short stabs and staccato lines.",
                   "Percussive distorted bass", "The ring-modulated octave is high-passed into glass and decays fast; only the sine SUB sustains under it.",
                   28, 60, Phrase::bassLine,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.3;
                       p.osc1Level = 0.8;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2Octave = 1;
                       p.osc2Level = 0.7;
                       p.ringMix = 0.6;
                       p.highPass = 2;
                       sub (p, SubWave::sine, -1, -8.0, 28, 64);
                       p.tone = hz (3500.0);
                       p.resonance = 0.2;
                       p.keyTrack = 0.6;
                       filterEnv (p, 0.001, 0.2, 0.1, 0.1, 18.0);
                       ampEnv (p, 0.001, 0.35, 0.25, 0.15);
                       p.velocityToFilter = 0.4;
                       p.velocityRangeDb = 12.0;
                       mono (p, false);
                       drive (p, DriveMode::crunch, 0.5, 0.7, 0.4);
                       p.analogAmount = 0.25;
                   },
                   nullptr });

    l.push_back ({ 18, "Slagline", F,
                   "A fast, punchy industrial bass for rhythmic playing, bone dry.",
                   "Sixteenth-note riffs; mute between notes.",
                   "Driving action ostinato", "Saw over an octave-down square, a very short filter blip and heavy CRUNCH - no space at all, so it sits in a busy mix.",
                   26, 55, Phrase::bassLine,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.85;
                       p.osc2Wave = Osc2Wave::pulse;
                       p.osc2Width = 0.5;
                       p.osc2Octave = -1;
                       p.osc2Level = 0.7;
                       sub (p, SubWave::square, -1, -14.0, 26, 60);
                       p.tone = hz (700.0);
                       p.resonance = 0.25;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.001, 0.12, 0.15, 0.06, 26.0);
                       ampEnv (p, 0.001, 0.18, 0.55, 0.06);
                       p.velocityToFilter = 0.4;
                       p.velocityRangeDb = 10.0;
                       mono (p, false);
                       drive (p, DriveMode::crunch, 0.6, 0.5, 0.5);
                       p.analogAmount = 0.2;
                   },
                   nullptr });

    l.push_back ({ 19, "Motor Ghost", F,
                   "A mechanical, pulsating bass with an unsteady motor inside.",
                   "Hold notes; the motor keeps turning.",
                   "Machine-room tension bass", "Beating oscillators and a PULSE movement throb in amplitude; a slow pitch wobble makes the motor uneven.",
                   26, 55, Phrase::bassLine,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.8;
                       p.osc2Wave = Osc2Wave::pulse;
                       p.osc2Width = 0.4;
                       p.osc2FineCents = 12.0;
                       p.osc2Level = 0.7;
                       sub (p, SubWave::sine, -1, -12.0, 26, 60);
                       p.lfo.shape = osp::mod::LfoShape::sine;
                       p.lfo.rateHz = 0.4;
                       p.lfoAmount = 1.0;
                       p.lfoPitch1 = 0.05;
                       p.tone = hz (600.0);
                       p.resonance = 0.3;
                       p.keyTrack = 0.45;
                       filterEnv (p, 0.004, 0.5, 0.5, 0.25, 12.0);
                       ampEnv (p, 0.004, 0.6, 0.95, 0.25);
                       p.velocityRangeDb = 8.0;
                       mono (p, true, 0.02);
                       pulse (p, 0.55, 0.6, 0.6, 0.1);
                       drive (p, DriveMode::tape, 0.35, 0.45, 0.6);
                       p.analogAmount = 0.55;
                   },
                   nullptr });

    l.push_back ({ 20, "Gravel Choir", F,
                   "A dark, vocal-like distorted bass that seems to sing through gravel.",
                   "Slow legato lines; the vowel moves on its own.",
                   "Ominous vocal bass", "Two resonant peaks - the ladder and card A's EQ bell - make a vowel that the LFO slowly moves; a tube stage roughens it.",
                   28, 55, Phrase::bassLine,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.85;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = -9.0;
                       p.osc2Level = 0.7;
                       eqBell (p.eqA, 700.0, 9.0, 3.0);
                       eqBell (p.eqC, 1100.0, 6.0, 2.5);
                       sub (p, SubWave::sine, -1, -11.0, 28, 60);
                       p.tone = hz (900.0);
                       p.resonance = 0.5;
                       p.keyTrack = 0.4;
                       p.lfo.shape = osp::mod::LfoShape::sine;
                       p.lfo.rateHz = 0.25;
                       p.lfoAmount = 1.0;
                       p.lfoCutoff = 5.0;
                       filterEnv (p, 0.02, 0.8, 0.6, 0.3, 8.0);
                       ampEnv (p, 0.01, 0.8, 0.95, 0.3);
                       p.velocityRangeDb = 8.0;
                       mono (p, true, 0.05);
                       drive (p, DriveMode::tube, 0.45, 0.4, 0.65);
                       p.analogAmount = 0.35;
                   },
                   nullptr });
}

} // namespace ca2
