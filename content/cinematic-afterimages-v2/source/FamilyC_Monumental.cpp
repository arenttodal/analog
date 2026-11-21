// FAMILY C - MONUMENTAL LOW END (021-030): cinematic bass drones and colossal foundations -
// physically large, still pitched instruments.

#include "Common.h"

namespace ca2
{

void addMonumental (std::vector<Spec>& l)
{
    const auto F = Family::monumental;

    l.push_back ({ 21, "Cathedral Below", F,
                   "A low organ of octaves and fifths, breathing slowly in a vast stone room.",
                   "Hold low chords and pedal notes; let the room answer.",
                   "Architectural low foundation", "Organ registration (square, fifth, sub-octave) with a slow tidal filter - no detune, no grit.",
                   24, 55, Phrase::drone,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.5;
                       p.osc1Level = 0.8;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2Coarse = 7.0;   // (the quint)
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 0.45;
                       osc3 (p, Osc2Wave::triangle, -1, 0.0, 0.0, -9.0);
                       p.tone = hz (1100.0);
                       p.resonance = 0.06;
                       p.keyTrack = 0.4;
                       filterEnv (p, 0.4, 2.0, 0.8, 1.2, 4.0);
                       ampEnv (p, 0.09, 1.0, 1.0, 1.6);
                       p.velocityRangeDb = 4.0;
                       p.lfo.shape = osp::mod::LfoShape::sine;
                       p.lfo.rateHz = 0.11;
                       p.lfoAmount = 1.0;
                       p.lfoCutoff = 5.0;   // (the slow tide of harmonics)
                       p.polyphony = 4;
                       room (p, SpaceType::hall, 0.3, 6.5, 45.0, 3600.0, 60.0);
                       p.analogAmount = 0.15;
                   },
                   nullptr });
}

} // namespace ca2
