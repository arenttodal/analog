// FAMILY D - ENDLESS DISTANCE (031-040): huge, reverb-drenched leads. A strong, playable dry tone
// first; then an unusually large, tuned space around it.

#include "Common.h"

namespace ca2
{

void addDistance (std::vector<Spec>& l)
{
    const auto F = Family::distance;

    l.push_back ({ 31, "Long Afterlight", F,
                   "A warm, singing lead suspended in an enormous space.",
                   "Play slowly and use the mod wheel; strike harder to open it.",
                   "Emotional solo line", "Brass-like bloom from velocity, delayed vibrato on the wheel, a nine-second hall kept dark.",
                   48, 84, Phrase::lead,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.85;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = 6.0;
                       p.osc2Level = 0.7;
                       p.tone = hz (850.0);
                       p.resonance = 0.12;
                       p.keyTrack = 0.55;
                       filterEnv (p, 0.12, 1.1, 0.55, 1.5, 30.0);
                       ampEnv (p, 0.05, 0.9, 0.9, 2.0);
                       p.velocityToFilter = 0.7;
                       p.velocityRangeDb = 14.0;
                       mono (p, true, 0.06);
                       vibratoOnWheel (p, 24.0, 5.4, 0.2, 0.5);
                       room (p, SpaceType::hall, 0.36, 9.0, 60.0, 5200.0, 150.0);
                       echo (p, EchoType::tape, 0.12, div::eighthDotted, 0.35, 0.4);
                       drift (p, 0.12, 0.5, 0.25, 0.3);
                       p.analogAmount = 0.45;
                   },
                   nullptr });
}

} // namespace ca2
