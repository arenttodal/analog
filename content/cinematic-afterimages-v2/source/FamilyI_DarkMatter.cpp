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
                       abyss (p, AbyssVariant::dark, 0.38, 12.0, 40.0, 4000.0);
                       p.analogAmount = 0.6;
                   },
                   nullptr });
}

} // namespace ca2
