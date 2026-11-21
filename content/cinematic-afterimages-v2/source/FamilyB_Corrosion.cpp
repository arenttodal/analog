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
}

} // namespace ca2
