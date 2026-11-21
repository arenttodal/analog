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
}

} // namespace ca2
