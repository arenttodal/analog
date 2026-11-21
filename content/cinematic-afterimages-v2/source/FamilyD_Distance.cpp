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

    l.push_back ({ 32, "Glass Horizon", F,
                   "A glassy, crystalline lead dissolving into long, diffuse reflections.",
                   "Play high, slow melodies; the wheel adds a shimmer of vibrato.",
                   "Ethereal top line", "A triangle an octave up and a touch of ring for the glass partials, a VINTAGE plate and a wide BBD echo.",
                   55, 91, Phrase::lead,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.25;
                       p.osc1Level = 0.55;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2Octave = 1;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 0.8;
                       p.ringMix = 0.2;
                       p.tone = hz (4000.0);
                       p.resonance = 0.2;
                       p.keyTrack = 0.6;
                       filterEnv (p, 0.02, 1.0, 0.6, 2.0, 14.0);
                       ampEnv (p, 0.02, 1.0, 0.8, 2.5);
                       p.velocityToFilter = 0.4;
                       p.velocityRangeDb = 12.0;
                       mono (p, true, 0.04);
                       vibratoOnWheel (p, 18.0, 5.8, 0.2, 0.4);
                       vintage (p, VintageVariant::plate, 0.35, 6.0, 30.0, 9000.0);
                       echo (p, EchoType::bbd, 0.12, div::eighth, 0.3, 0.55, EchoStereo::wide);
                       p.analogAmount = 0.3;
                   },
                   nullptr });

    l.push_back ({ 33, "Ghost Transmission", F,
                   "A lonely narrow-pulse lead disappearing into a huge echoing space.",
                   "Leave space between phrases; let the echoes answer.",
                   "Solitary signal melody", "A thin pulse alone, its pitch slightly unsteady, sent into long tape echoes and a dark ABYSS.",
                   52, 88, Phrase::lead,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.1;
                       p.osc1Level = 0.95;
                       p.osc2Level = 0.0;
                       p.tone = hz (1400.0);
                       p.resonance = 0.3;
                       p.keyTrack = 0.6;
                       p.lfo.shape = osp::mod::LfoShape::sine;
                       p.lfo.rateHz = 0.35;
                       p.lfoAmount = 1.0;
                       p.lfoPitch1 = 0.07;
                       filterEnv (p, 0.03, 0.8, 0.6, 1.5, 10.0);
                       ampEnv (p, 0.03, 0.8, 0.85, 2.0);
                       p.velocityRangeDb = 12.0;
                       mono (p, true, 0.05);
                       echo (p, EchoType::tape, 0.3, div::quarter, 0.55, 0.35);
                       abyss (p, AbyssVariant::dark, 0.3, 10.0, 30.0, 4500.0);
                       p.analogAmount = 0.45;
                   },
                   nullptr });

    l.push_back ({ 34, "The Last Beacon", F,
                   "A heroic, emotional mono lead whose saws slowly blossom.",
                   "Play long, slurred phrases; let each note bloom before moving.",
                   "Heroic theme lead", "Card A's three saws open over half a second, a long glide, delayed vibrato on the wheel, an eight-second hall.",
                   48, 84, Phrase::lead,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.8;
                       unisonCard (p, 0, 3, 0.15, 0.55, 0.4);
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = 5.0;
                       p.osc2Level = 0.65;
                       p.tone = hz (700.0);
                       p.resonance = 0.15;
                       p.keyTrack = 0.55;
                       filterEnv (p, 0.6, 2.0, 0.7, 2.0, 30.0);
                       ampEnv (p, 0.05, 1.0, 0.95, 3.0);
                       p.velocityToFilter = 0.5;
                       p.velocityRangeDb = 12.0;
                       mono (p, true, 0.09);
                       vibratoOnWheel (p, 22.0, 5.3, 0.4, 0.6);
                       room (p, SpaceType::hall, 0.38, 8.0, 50.0, 5500.0, 150.0);
                       echo (p, EchoType::tape, 0.1, div::eighthDotted, 0.3, 0.4);
                       p.analogAmount = 0.4;
                   },
                   nullptr });

    l.push_back ({ 35, "Halo Drift", F,
                   "A soft, pure, almost vocal lead floating in an exceptionally spacious tail.",
                   "Play slowly and softly; the vibrato arrives by itself.",
                   "Tender vocal-like line", "A triangle with only a breath of PWM pulse, self-timed vibrato, and BLOOM's wash for the halo.",
                   55, 88, Phrase::lead,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.45;
                       p.osc1Level = 0.3;
                       p.lfo.shape = osp::mod::LfoShape::sine;
                       p.lfo.rateHz = 0.5;
                       p.lfoAmount = 1.0;
                       p.lfoWidth1 = 0.15;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 1.0;
                       p.tone = hz (1200.0);
                       p.resonance = 0.1;
                       p.keyTrack = 0.6;
                       filterEnv (p, 0.1, 1.0, 0.8, 2.0, 6.0);
                       ampEnv (p, 0.12, 1.0, 0.95, 3.0);
                       p.velocityRangeDb = 10.0;
                       mono (p, true, 0.06);
                       vibratoAlways (p, 9.0, 5.0, 0.6, 0.8);
                       bloom (p, BloomVariant::wash, 0.4, 9.0);
                       p.analogAmount = 0.25;
                   },
                   nullptr });

    l.push_back ({ 36, "Burning Aurora", F,
                   "A bright, aggressive sync lead that turns ethereal in a vast space.",
                   "Play bold lines; the wheel opens the fire.",
                   "Soaring climax lead", "Poly Mod sweeps a synced saw into a driven ladder; a wide tape echo and an ENDLESS ABYSS make the aggression float.",
                   52, 88, Phrase::lead,
                   [] (Patch& p) {
                       p.sync = true;
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Coarse = 9.0;
                       p.osc1Level = 0.8;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2Level = 0.5;
                       p.polyModEnv = 0.3;
                       p.polyModToPitch1 = true;
                       p.tone = hz (3000.0);
                       p.resonance = 0.25;
                       p.keyTrack = 0.5;
                       p.filterDrive = 0.3;
                       filterEnv (p, 0.01, 0.6, 0.5, 0.8, 16.0);
                       ampEnv (p, 0.01, 0.6, 0.9, 1.5);
                       p.velocityRangeDb = 10.0;
                       mono (p, true, 0.03);
                       drive (p, DriveMode::crunch, 0.25, 0.55, 0.5);
                       echo (p, EchoType::tape, 0.22, div::eighthDotted, 0.45, 0.45, EchoStereo::wide);
                       abyss (p, AbyssVariant::endless, 0.32, 12.0, 30.0, 6000.0);
                       p.analogAmount = 0.3;
                   },
                   [] (ShellProcessor& s) { route (s, 0, Source::cc1, Dest::cutoff, 35.0f); } });

    l.push_back ({ 37, "Distant Signal", F,
                   "A small, intimate lead heard across an immense distance.",
                   "Simple melodies, softly; the long pre-delay sets the distance.",
                   "Far-away memory line", "A thin, dark, high-passed tone; the room arrives a sixth of a second late and very dark.",
                   55, 88, Phrase::lead,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.5;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 0.9;
                       p.highPass = 2;
                       p.tone = hz (800.0);
                       p.resonance = 0.15;
                       p.keyTrack = 0.6;
                       filterEnv (p, 0.02, 0.8, 0.6, 1.0, 8.0);
                       ampEnv (p, 0.02, 0.8, 0.9, 1.5);
                       p.velocityRangeDb = 10.0;
                       mono (p, true, 0.04);
                       room (p, SpaceType::hall, 0.45, 10.0, 160.0, 3000.0, 200.0);
                       p.analogAmount = 0.3;
                   },
                   nullptr });

    l.push_back ({ 38, "Pale Cathedral", F,
                   "A high, choir-like lead with slow vowel-like movement in a cathedral.",
                   "Long held notes high up; the vowel turns slowly.",
                   "Sacred high line", "Resonant Jupiter OTA moved by a slow LFO and an EQ bell make the vowel; three copies on card B make it a choir; a VINTAGE chamber.",
                   60, 91, Phrase::lead,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.7;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = 7.0;
                       p.osc2Level = 0.6;
                       unisonCard (p, 1, 3, 0.2, 0.6, 0.6);
                       eqBell (p.eqA, 1000.0, 6.0, 2.0);
                       p.filterModel = FilterModel::ota;
                       p.otaVoicing = OtaVoicing::jupiter;
                       p.tone = hz (2200.0);
                       p.resonance = 0.45;
                       p.keyTrack = 0.5;
                       p.lfo.shape = osp::mod::LfoShape::sine;
                       p.lfo.rateHz = 0.3;
                       p.lfoAmount = 1.0;
                       p.lfoCutoff = 7.0;
                       filterEnv (p, 0.3, 1.5, 0.8, 3.0, 6.0);
                       ampEnv (p, 0.25, 1.5, 0.9, 3.5);
                       p.velocityRangeDb = 10.0;
                       mono (p, true, 0.05);
                       vibratoAlways (p, 12.0, 5.2, 0.4, 0.8);
                       vintage (p, VintageVariant::chamber, 0.38, 7.0, 40.0, 6000.0);
                       p.analogAmount = 0.3;
                   },
                   nullptr });

    l.push_back ({ 39, "Blue Ember", F,
                   "A breathy, flute-like lead with a soft glow of ambience.",
                   "Play like a flute: gentle attacks, small bends.",
                   "Woodwind-like melody", "A pure triangle with white-noise breath through an open ladder, a modest attack, a deep hall and a quiet quarter-note echo.",
                   60, 96, Phrase::lead,
                   [] (Patch& p) {
                       p.osc1Level = 0.0;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 1.0;
                       p.noiseLevel = 0.12;
                       p.tone = hz (2200.0);
                       p.resonance = 0.05;
                       p.keyTrack = 0.8;
                       filterEnv (p, 0.06, 0.6, 0.8, 1.0, 6.0);
                       ampEnv (p, 0.08, 0.6, 0.9, 1.8);
                       p.velocityRangeDb = 14.0;
                       mono (p, true, 0.04);
                       vibratoOnWheel (p, 15.0, 5.0, 0.3, 0.5);
                       room (p, SpaceType::hall, 0.32, 6.0, 25.0, 6000.0, 150.0);
                       echo (p, EchoType::tape, 0.1, div::quarter, 0.35, 0.4);
                       p.analogAmount = 0.25;
                   },
                   nullptr });

    l.push_back ({ 40, "Endless Arrival", F,
                   "A powerful analog brass lead that blooms dramatically into a massive hall.",
                   "Strike harder for a bigger bloom; the wheel adds vibrato.",
                   "Grand arrival theme", "Three saws (one an octave down) with a big velocity-led filter bloom, unstable vintage drift, a nine-second VINTAGE hall.",
                   48, 84, Phrase::lead,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.8;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = 9.0;
                       p.osc2Level = 0.7;
                       osc3 (p, Osc2Wave::saw, -1, 0.0, -3.0, -10.0);
                       p.tone = hz (700.0);
                       p.resonance = 0.15;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.35, 1.5, 0.6, 2.0, 34.0);
                       ampEnv (p, 0.1, 1.0, 0.9, 3.0);
                       p.velocityToFilter = 0.7;
                       p.velocityRangeDb = 14.0;
                       mono (p, true, 0.07);
                       vibratoOnWheel (p, 20.0, 5.4, 0.3, 0.5);
                       drift (p, 0.25, 0.5, 0.4, 0.4);
                       vintage (p, VintageVariant::hall, 0.38, 9.0, 40.0, 6000.0);
                       p.analogAmount = 0.6;
                   },
                   nullptr });
}

} // namespace ca2
