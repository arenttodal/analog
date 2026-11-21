// FAMILY J - FRAGILE MACHINES (091-100): keys, bells, plucks and cinematic effects - the unusual
// final sounds.

#include "Common.h"

namespace ca2
{

void addFragile (std::vector<Spec>& l)
{
    const auto F = Family::fragile;

    l.push_back ({ 91, "Glass Raindrops", F,
                   "Delicate metallic drops falling into a soft, cascading space.",
                   "Play sparse high notes and broken chords.",
                   "Glittering keys / pluck", "Ring modulation against a fifth gives the metal; a fast pluck and a long plate let each drop fall.",
                   60, 96, Phrase::keys,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.5;
                       p.osc1Level = 0.8;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2Coarse = 7.0;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 1.0;
                       p.ringMix = 0.5;
                       p.levelDb = 6.0;
                       p.tone = hz (3000.0);
                       p.resonance = 0.15;
                       p.keyTrack = 0.7;
                       filterEnv (p, 0.001, 0.25, 0.1, 0.6, 30.0);
                       ampEnv (p, 0.001, 0.9, 0.0, 0.8);
                       p.velocityToFilter = 0.5;
                       p.velocityRangeDb = 18.0;
                       p.polyphony = 16;
                       room (p, SpaceType::plate, 0.28, 2.8, 20.0, 8000.0);
                       echo (p, EchoType::tape, 0.15, div::eighth, 0.4, 0.55);
                       p.analogAmount = 0.2;
                   },
                   nullptr });
}

} // namespace ca2
