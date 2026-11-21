// FAMILY H - MUSICAL MACHINES (071-080): arpeggios and rhythmic instruments - hold a chord and the
// intended performance plays; rate, gate, octaves and pattern invite play. They follow the host's
// tempo.

#include "Common.h"

namespace ca2
{

void addMachines (std::vector<Spec>& l)
{
    const auto F = Family::machines;

    l.push_back ({ 71, "Lost Telemetry", F,
                   "A retro sixteenth-note pulse, narrow and filtered, with stereo echoes.",
                   "Hold a chord; change the arpeggiator's rate and octaves.",
                   "Driving underscore pulse", "A narrow pulse through a resonant ladder, plucked short, echoed across the stereo field by a BBD.",
                   48, 76, Phrase::arpChord,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.22;
                       p.osc1Level = 1.0;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2Level = 0.4;
                       p.levelDb = 6.0;
                       p.tone = hz (900.0);
                       p.resonance = 0.45;
                       p.keyTrack = 0.55;
                       filterEnv (p, 0.001, 0.18, 0.05, 0.12, 26.0);
                       ampEnv (p, 0.001, 0.25, 0.0, 0.12);
                       p.velocityRangeDb = 10.0;
                       p.polyphony = 8;
                       echo (p, EchoType::bbd, 0.18, div::eighthDotted, 0.35, 0.45);
                       room (p, SpaceType::plate, 0.1, 1.6);
                       p.analogAmount = 0.3;
                   },
                   [] (ShellProcessor& s) { arp (s, "UP", "1/16", 1, 50.0f); } });
}

} // namespace ca2
