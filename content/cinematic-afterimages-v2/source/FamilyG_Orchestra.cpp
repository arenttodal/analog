// FAMILY G - SYNTHETIC ORCHESTRA (061-070): brass, strings and ensembles as analog impressions -
// attack, bloom, vibrato onset and width, never pretending to be a sample library.

#include "Common.h"

namespace ca2
{

void addOrchestra (std::vector<Spec>& l)
{
    const auto F = Family::orchestra;

    l.push_back ({ 61, "Copper Choir", F,
                   "A massive synthetic brass section, warm and wide.",
                   "Play full chords; accent with velocity.",
                   "Brass ensemble chords", "Three saw voices (one an octave down on card C) blooming through the ladder on every chord.",
                   40, 80, Phrase::chords,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.8;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = 7.0;
                       p.osc2Level = 0.7;
                       unisonCard (p, 0, 3, 0.2, 0.55, 0.55);
                       osc3 (p, Osc2Wave::saw, -1, 0.0, -5.0, -8.0);
                       p.tone = hz (700.0);
                       p.resonance = 0.14;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.18, 1.2, 0.55, 1.0, 32.0);
                       ampEnv (p, 0.09, 0.8, 0.9, 1.0);
                       p.velocityToFilter = 0.6;
                       p.velocityRangeDb = 14.0;
                       p.polyphony = 8;
                       room (p, SpaceType::hall, 0.2, 3.5, 25.0, 6000.0);
                       p.analogAmount = 0.4;
                   },
                   nullptr });
}

} // namespace ca2
