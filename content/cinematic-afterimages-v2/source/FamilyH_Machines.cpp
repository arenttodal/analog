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

    l.push_back ({ 72, "Hourglass Motor", F,
                   "A triplet pulse that climbs and falls like sand turning in an hourglass.",
                   "Hold a chord; try two octaves and change the chord every bar.",
                   "Rolling triplet motor", "UP/DOWN in eighth-note triplets over two octaves; a pulse and saw plucked short through a warm ladder.",
                   45, 76, Phrase::arpChord,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.35;
                       p.osc1Level = 0.9;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = 5.0;
                       p.osc2Level = 0.55;
                       p.tone = hz (800.0);
                       p.resonance = 0.3;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.001, 0.25, 0.1, 0.2, 24.0);
                       ampEnv (p, 0.002, 0.35, 0.2, 0.2);
                       p.velocityRangeDb = 10.0;
                       p.polyphony = 8;
                       echo (p, EchoType::tape, 0.14, div::quarterTriplet, 0.3, 0.45);
                       room (p, SpaceType::room, 0.12, 1.5);
                       p.analogAmount = 0.35;
                   },
                   [] (ShellProcessor& s) { arp (s, "UP/DOWN", "1/8T", 2, 60.0f); } });

    l.push_back ({ 73, "Spindle Light", F,
                   "A fast, sparkling three-octave arpeggio dissolving into light.",
                   "Hold a simple triad; it spins up through three octaves.",
                   "Sparkling cascade", "UP in sixteenths over three octaves; a bright triangle and pulse pluck, into BLOOM's clean shimmer.",
                   52, 79, Phrase::arpChord,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.3;
                       p.osc1Level = 0.6;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2Octave = 1;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 0.9;
                       p.tone = hz (2500.0);
                       p.resonance = 0.15;
                       p.keyTrack = 0.6;
                       filterEnv (p, 0.001, 0.2, 0.2, 0.2, 18.0);
                       ampEnv (p, 0.001, 0.3, 0.1, 0.25);
                       p.velocityRangeDb = 10.0;
                       p.polyphony = 8;
                       bloom (p, BloomVariant::clean, 0.3, 5.0);
                       p.analogAmount = 0.25;
                   },
                   [] (ShellProcessor& s) { arp (s, "UP", "1/16", 3, 45.0f); } });

    l.push_back ({ 74, "Counting Shadows", F,
                   "A dark, descending eighth-note pattern for suspense.",
                   "Hold low minor chords; it walks downward through them.",
                   "Suspense ostinato", "DOWN in eighths, one octave; a dark saw and sub-octave pulse through a closed ladder.",
                   36, 64, Phrase::arpChord,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.85;
                       p.osc2Wave = Osc2Wave::pulse;
                       p.osc2Width = 0.45;
                       p.osc2Octave = -1;
                       p.osc2Level = 0.5;
                       p.tone = hz (450.0);
                       p.resonance = 0.25;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.002, 0.3, 0.15, 0.25, 22.0);
                       ampEnv (p, 0.002, 0.4, 0.3, 0.25);
                       p.velocityRangeDb = 10.0;
                       p.polyphony = 8;
                       echo (p, EchoType::tape, 0.12, div::quarter, 0.35, 0.35);
                       room (p, SpaceType::hall, 0.15, 2.5, 20.0, 4500.0);
                       p.analogAmount = 0.35;
                   },
                   [] (ShellProcessor& s) { arp (s, "DOWN", "1/8", 1, 70.0f); } });

    l.push_back ({ 75, "Circuit Rain", F,
                   "Random metallic droplets falling through a chord.",
                   "Hold a wide chord; the order changes every time.",
                   "Random glitter", "RANDOM in sixteenths over two octaves; ring modulation against a fifth makes each drop metallic.",
                   52, 79, Phrase::arpChord,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.5;
                       p.osc1Level = 0.7;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2Coarse = 7.0;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 0.9;
                       p.ringMix = 0.4;
                       p.tone = hz (2800.0);
                       p.resonance = 0.15;
                       p.keyTrack = 0.6;
                       filterEnv (p, 0.001, 0.15, 0.1, 0.15, 20.0);
                       ampEnv (p, 0.001, 0.25, 0.0, 0.2);
                       p.velocityRangeDb = 12.0;
                       p.polyphony = 8;
                       echo (p, EchoType::bbd, 0.15, div::eighthDotted, 0.35, 0.5, EchoStereo::pingPong);
                       room (p, SpaceType::plate, 0.18, 2.0);
                       p.analogAmount = 0.25;
                   },
                   [] (ShellProcessor& s) { arp (s, "RANDOM", "1/16", 2, 40.0f); } });

    l.push_back ({ 76, "Moss Clock", F,
                   "An organic, swung pattern that keeps returning to the lowest note.",
                   "Hold a chord of three or four notes; the swing gives it a loping walk.",
                   "Organic swung pulse", "THUMB UP in dotted sixteenths with swing; a soft triangle and narrow pulse, gently filtered.",
                   45, 76, Phrase::arpChord,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.2;
                       p.osc1Level = 0.55;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 3.0;
                       p.osc2Level = 0.9;
                       p.tone = hz (1300.0);
                       p.resonance = 0.18;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.002, 0.3, 0.3, 0.25, 12.0);
                       ampEnv (p, 0.003, 0.35, 0.3, 0.25);
                       p.velocityRangeDb = 10.0;
                       p.polyphony = 8;
                       echo (p, EchoType::tape, 0.1, div::eighth, 0.3, 0.4);
                       room (p, SpaceType::room, 0.15, 1.6);
                       p.analogAmount = 0.4;
                   },
                   [] (ShellProcessor& s) {
                       arp (s, "THUMB UP", "1/16D", 1, 55.0f);
                       set (s, osp::plugin::ids::arpSwing, 30.0f);
                   } });

    l.push_back ({ 77, "Broken Compass", F,
                   "A gritty, driven up-and-down pattern that loses its bearings.",
                   "Hold a dissonant chord; the grit grows with velocity.",
                   "Gritty motor", "UP & DOWN in sixteenths; two saws through a driven, biased ladder for an edge that never settles.",
                   40, 72, Phrase::arpChord,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.85;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = 12.0;
                       p.osc2Level = 0.6;
                       p.tone = hz (900.0);
                       p.resonance = 0.3;
                       p.keyTrack = 0.5;
                       p.filterDrive = 0.45;
                       p.driveBias = 0.4;
                       filterEnv (p, 0.001, 0.2, 0.15, 0.15, 22.0);
                       ampEnv (p, 0.002, 0.25, 0.2, 0.15);
                       p.velocityToFilter = 0.6;
                       p.velocityRangeDb = 10.0;
                       p.polyphony = 8;
                       room (p, SpaceType::room, 0.12, 1.2);
                       p.analogAmount = 0.45;
                   },
                   [] (ShellProcessor& s) { arp (s, "UP & DOWN", "1/16", 1, 50.0f); } });

    l.push_back ({ 78, "Neon Telegraph", F,
                   "A short, clipped sync pattern tapping out messages in sixteenths.",
                   "Hold a chord; shorten or lengthen the gate to change the code.",
                   "Staccato signal", "UP in sixteenths with a short gate; a synced saw clicks against a BBD echo at the same rate.",
                   48, 76, Phrase::arpChord,
                   [] (Patch& p) {
                       p.sync = true;
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Coarse = 10.0;
                       p.osc1Level = 1.0;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2Level = 0.6;
                       p.tone = hz (1800.0);
                       p.resonance = 0.2;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.001, 0.12, 0.2, 0.1, 18.0);
                       ampEnv (p, 0.001, 0.15, 0.4, 0.08);
                       p.velocityRangeDb = 10.0;
                       p.polyphony = 8;
                       echo (p, EchoType::bbd, 0.16, div::sixteenth, 0.35, 0.5, EchoStereo::wide);
                       room (p, SpaceType::room, 0.1, 1.0);
                       p.analogAmount = 0.3;
                   },
                   [] (ShellProcessor& s) { arp (s, "UP", "1/16", 1, 35.0f); } });

    l.push_back ({ 79, "Paper Satellites", F,
                   "A soft, floating pattern that folds inward, blurred into a wash.",
                   "Hold wide chords; the outer notes meet in the middle.",
                   "Floating ostinato", "CONVERGE in dotted eighths with a long gate; triangle and soft pulse into BLOOM's wash.",
                   48, 84, Phrase::arpChord,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.4;
                       p.osc1Level = 0.5;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 0.9;
                       p.tone = hz (1600.0);
                       p.resonance = 0.1;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.01, 0.5, 0.5, 0.6, 8.0);
                       ampEnv (p, 0.01, 0.6, 0.6, 0.8);
                       p.velocityRangeDb = 8.0;
                       p.polyphony = 8;
                       bloom (p, BloomVariant::wash, 0.35, 7.0);
                       p.analogAmount = 0.3;
                   },
                   [] (ShellProcessor& s) { arp (s, "CONVERGE", "1/8D", 1, 80.0f); } });

    l.push_back ({ 80, "Binary Aurora", F,
                   "A wide, shimmering three-octave pattern whose filter rises and falls over several seconds.",
                   "Hold one chord for several bars; the light comes and goes by itself.",
                   "Evolving climax arpeggio", "UP/DOWN in sixteenths over three octaves; card A's four saws, a slow LFO on the filter, Juno chorus II.",
                   48, 76, Phrase::arpChord,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.75;
                       unisonCard (p, 0, 4, 0.2, 0.55, 0.7);
                       p.osc2Wave = Osc2Wave::pulse;
                       p.osc2Width = 0.4;
                       p.osc2FineCents = 5.0;
                       p.osc2Level = 0.45;
                       p.tone = hz (900.0);
                       p.resonance = 0.25;
                       p.keyTrack = 0.5;
                       p.lfo.shape = osp::mod::LfoShape::triangle;
                       p.lfo.rateHz = 0.15;
                       p.lfoAmount = 1.0;
                       p.lfoCutoff = 14.0;
                       filterEnv (p, 0.001, 0.25, 0.25, 0.2, 14.0);
                       ampEnv (p, 0.002, 0.3, 0.3, 0.2);
                       p.velocityRangeDb = 10.0;
                       p.chorusMode = 2;
                       p.polyphony = 8;
                       echo (p, EchoType::tape, 0.12, div::eighthDotted, 0.35, 0.45, EchoStereo::wide);
                       p.analogAmount = 0.3;
                   },
                   [] (ShellProcessor& s) { arp (s, "UP/DOWN", "1/16", 3, 50.0f); } });
}

} // namespace ca2
