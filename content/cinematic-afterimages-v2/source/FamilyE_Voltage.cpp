// FAMILY E - HUMAN VOLTAGE (041-050): expressive analog leads that answer the player's touch, alive
// without leaning on effects.

#include "Common.h"

namespace ca2
{

void addVoltage (std::vector<Spec>& l)
{
    const auto F = Family::voltage;

    l.push_back ({ 41, "Amber Meridian", F,
                   "A warm reed with a brassy bite when you dig in.",
                   "Vary your velocity; aftertouch brightens a held note.",
                   "Expressive melodic lead", "Narrow pulse over a triangle: soft playing is a reed, hard playing a horn; vibrato arrives late on its own.",
                   48, 84, Phrase::lead,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.3;
                       p.osc1Level = 0.75;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 3.0;
                       p.osc2Level = 0.6;
                       p.tone = hz (1050.0);
                       p.resonance = 0.22;
                       p.keyTrack = 0.6;
                       filterEnv (p, 0.02, 0.5, 0.4, 0.3, 20.0);
                       ampEnv (p, 0.015, 0.3, 0.9, 0.25);
                       p.velocityToFilter = 0.85;
                       p.velocityRangeDb = 16.0;
                       mono (p, true, 0.035);
                       vibratoAlways (p, 11.0, 5.2, 0.55, 0.8);
                       room (p, SpaceType::room, 0.08, 0.9, 6.0, 7000.0);
                       p.analogAmount = 0.4;
                   },
                   [] (ShellProcessor& s) { route (s, 0, Source::pressure, Dest::cutoff, 40.0f); } });
}

} // namespace ca2
