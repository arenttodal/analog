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

    l.push_back ({ 42, "Wounded Voltage", F,
                   "A slightly unstable sync lead that cries when pushed with aftertouch.",
                   "Lean into held notes; the wheel adds a wide, aching vibrato.",
                   "Wounded hero line", "Two synced saws a fifth apart with a slow glide and a loose analog feel; aftertouch opens the filter onto the sync edge.",
                   48, 84, Phrase::lead,
                   [] (Patch& p) {
                       p.sync = true;
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Coarse = 5.0;
                       p.osc1Level = 0.8;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 0.45;
                       p.tone = hz (900.0);
                       p.resonance = 0.2;
                       p.keyTrack = 0.55;
                       filterEnv (p, 0.04, 0.8, 0.5, 0.6, 18.0);
                       ampEnv (p, 0.02, 0.6, 0.9, 0.6);
                       p.velocityToFilter = 0.6;
                       p.velocityRangeDb = 14.0;
                       mono (p, true, 0.1);
                       vibratoOnWheel (p, 25.0, 5.6, 0.15, 0.4);
                       room (p, SpaceType::hall, 0.18, 3.0, 25.0, 6000.0);
                       p.analogAmount = 0.6;
                   },
                   [] (ShellProcessor& s) { route (s, 0, Source::pressure, Dest::cutoff, 45.0f); } });

    l.push_back ({ 43, "Night Swimmer", F,
                   "A smooth, haunting saw lead with a slow underwater vibrato.",
                   "Play long, legato lines in the middle of the keyboard.",
                   "Nocturnal melody", "Two saws a few cents apart through a soft ladder, a slow vibrato always present, a dotted tape echo.",
                   48, 84, Phrase::lead,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.8;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = -5.0;
                       p.osc2Level = 0.7;
                       p.tone = hz (750.0);
                       p.resonance = 0.12;
                       p.keyTrack = 0.6;
                       filterEnv (p, 0.08, 1.2, 0.6, 1.0, 10.0);
                       ampEnv (p, 0.06, 1.0, 0.9, 1.2);
                       p.velocityToFilter = 0.4;
                       p.velocityRangeDb = 12.0;
                       mono (p, true, 0.06);
                       vibratoAlways (p, 8.0, 3.2, 0.3, 1.0);
                       echo (p, EchoType::tape, 0.2, div::eighthDotted, 0.4, 0.4);
                       room (p, SpaceType::hall, 0.2, 3.5, 25.0, 5000.0);
                       p.analogAmount = 0.35;
                   },
                   nullptr });

    l.push_back ({ 44, "Silk Current", F,
                   "A soft, vocal lead with a moving formant-like character.",
                   "Slow phrases with gentle attacks; let the vowel move by itself.",
                   "Singing lead", "A triangle and a pulse, a strong EQ bell for the vowel, a slow LFO sweeping the filter around it.",
                   52, 88, Phrase::lead,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.4;
                       p.osc1Level = 0.6;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 2.0;
                       p.osc2Level = 0.8;
                       eqBell (p.eqA, 900.0, 7.0, 1.8);
                       eqBell (p.eqC, 900.0, 5.0, 1.8);
                       p.tone = hz (1400.0);
                       p.resonance = 0.3;
                       p.keyTrack = 0.5;
                       p.lfo.shape = osp::mod::LfoShape::triangle;
                       p.lfo.rateHz = 0.6;
                       p.lfoAmount = 1.0;
                       p.lfoCutoff = 6.0;
                       filterEnv (p, 0.1, 1.0, 0.7, 1.0, 8.0);
                       ampEnv (p, 0.09, 1.0, 0.9, 1.2);
                       p.velocityRangeDb = 12.0;
                       mono (p, true, 0.05);
                       vibratoOnWheel (p, 15.0, 5.2);
                       room (p, SpaceType::plate, 0.2, 2.5, 20.0, 7000.0);
                       p.analogAmount = 0.3;
                   },
                   nullptr });

    l.push_back ({ 45, "Glass Needle", F,
                   "A thin, piercing resonant lead that cuts through any arrangement.",
                   "Velocity sets the sting; aftertouch pushes it brighter.",
                   "Cutting top line", "A very narrow pulse through a resonant ladder that follows velocity hard; aftertouch opens it further.",
                   55, 91, Phrase::lead,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.08;
                       p.osc1Level = 1.0;
                       p.osc2Level = 0.0;
                       p.tone = hz (1100.0);
                       p.resonance = 0.4;
                       p.keyTrack = 0.7;
                       filterEnv (p, 0.005, 0.4, 0.4, 0.4, 24.0);
                       ampEnv (p, 0.005, 0.4, 0.85, 0.5);
                       p.velocityToFilter = 0.9;
                       p.velocityRangeDb = 14.0;
                       mono (p, false, 0.02);
                       vibratoOnWheel (p, 18.0, 6.0);
                       echo (p, EchoType::bbd, 0.12, div::eighth, 0.3, 0.5);
                       room (p, SpaceType::room, 0.12, 1.2);
                       p.analogAmount = 0.3;
                   },
                   [] (ShellProcessor& s) { route (s, 0, Source::pressure, Dest::cutoff, 50.0f); } });

    l.push_back ({ 46, "Horizon Runner", F,
                   "A propulsive, fast-modulated sync lead built for chase themes.",
                   "Fast lines and repeated notes; the wheel opens it up.",
                   "Chase-scene lead", "A synced saw an octave up, Poly Mod's quick sweep on every note, a little crunch.",
                   48, 84, Phrase::lead,
                   [] (Patch& p) {
                       p.sync = true;
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Coarse = 12.0;
                       p.osc1Level = 0.75;
                       p.osc2Wave = Osc2Wave::pulse;
                       p.osc2Width = 0.4;
                       p.osc2Level = 0.5;
                       p.polyModEnv = 0.25;
                       p.polyModToPitch1 = true;
                       p.tone = hz (2200.0);
                       p.resonance = 0.2;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.002, 0.25, 0.4, 0.25, 18.0);
                       ampEnv (p, 0.002, 0.3, 0.85, 0.2);
                       p.velocityToFilter = 0.5;
                       p.velocityRangeDb = 10.0;
                       mono (p, false, 0.015);
                       drive (p, DriveMode::crunch, 0.15, 0.55, 0.5);
                       echo (p, EchoType::tape, 0.1, div::sixteenthDotted, 0.3, 0.5);
                       room (p, SpaceType::room, 0.1, 1.0);
                       p.analogAmount = 0.3;
                   },
                   [] (ShellProcessor& s) { route (s, 0, Source::cc1, Dest::cutoff, 30.0f); } });

    l.push_back ({ 47, "False Sunrise", F,
                   "A dynamic lead that opens wide with velocity and aftertouch.",
                   "Play soft for shadow, hard for light; aftertouch and the wheel both open it.",
                   "Hopeful rising theme", "A slowly swept PWM pulse whose filter follows velocity fully; aftertouch and the mod wheel open it further.",
                   48, 84, Phrase::lead,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.35;
                       p.osc1Level = 0.85;
                       p.lfo.shape = osp::mod::LfoShape::triangle;
                       p.lfo.rateHz = 0.4;
                       p.lfoAmount = 1.0;
                       p.lfoWidth1 = 0.2;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = 6.0;
                       p.osc2Level = 0.5;
                       p.tone = hz (600.0);
                       p.resonance = 0.2;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.05, 1.0, 0.5, 0.8, 28.0);
                       ampEnv (p, 0.03, 0.8, 0.9, 0.9);
                       p.velocityToFilter = 1.0;
                       p.velocityRangeDb = 16.0;
                       mono (p, true, 0.05);
                       room (p, SpaceType::hall, 0.2, 3.5, 30.0, 6000.0);
                       p.analogAmount = 0.35;
                   },
                   [] (ShellProcessor& s) {
                       route (s, 0, Source::pressure, Dest::cutoff, 60.0f);
                       route (s, 1, Source::cc1, Dest::cutoff, 40.0f);
                   } });

    l.push_back ({ 48, "Copper Thread", F,
                   "A focused, thin analog lead with a late, expressive vibrato.",
                   "Hold notes long; the vibrato blooms after half a second.",
                   "Intimate solo line", "Two saws, high-passed and narrowed by the ladder, with vibrato that waits before it arrives.",
                   52, 88, Phrase::lead,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.8;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = 3.0;
                       p.osc2Level = 0.6;
                       p.highPass = 2;
                       p.tone = hz (2400.0);
                       p.resonance = 0.18;
                       p.keyTrack = 0.6;
                       filterEnv (p, 0.02, 0.6, 0.6, 0.6, 10.0);
                       ampEnv (p, 0.02, 0.6, 0.9, 0.8);
                       p.velocityToFilter = 0.4;
                       p.velocityRangeDb = 12.0;
                       mono (p, true, 0.04);
                       vibratoAlways (p, 16.0, 5.5, 0.45, 0.7);
                       room (p, SpaceType::plate, 0.18, 2.2, 15.0, 7000.0);
                       p.analogAmount = 0.35;
                   },
                   nullptr });

    l.push_back ({ 49, "Arc of Breath", F,
                   "An airy lead mixing triangle tone and soft breath noise.",
                   "Soft playing is almost all breath; dig in for the tone.",
                   "Breathing melody", "A triangle with Juno-coloured noise beneath it; a wide velocity range lets the breath lead at low velocities.",
                   55, 91, Phrase::lead,
                   [] (Patch& p) {
                       p.osc1Level = 0.0;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 1.0;
                       p.noiseLevel = 0.25;
                       p.noiseColor = NoiseColor::juno;
                       p.tone = hz (1800.0);
                       p.resonance = 0.1;
                       p.keyTrack = 0.7;
                       filterEnv (p, 0.08, 0.8, 0.7, 0.8, 8.0);
                       ampEnv (p, 0.07, 0.8, 0.9, 1.0);
                       p.velocityToFilter = 0.6;
                       p.velocityRangeDb = 20.0;
                       mono (p, true, 0.04);
                       vibratoOnWheel (p, 14.0, 5.0);
                       room (p, SpaceType::hall, 0.25, 3.0, 25.0, 6000.0);
                       p.analogAmount = 0.3;
                   },
                   nullptr });

    l.push_back ({ 50, "Soft Defiance", F,
                   "A restrained, dark lead that becomes intense only when needed.",
                   "Stay quiet and low; strike hard or press in for the defiance.",
                   "Restrained resolve", "A pulse and triangle kept dark by a resonant ladder at 650 Hz; velocity and aftertouch are the only way out.",
                   45, 81, Phrase::lead,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.35;
                       p.osc1Level = 0.8;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 4.0;
                       p.osc2Level = 0.7;
                       p.tone = hz (650.0);
                       p.resonance = 0.35;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.03, 0.7, 0.3, 0.6, 22.0);
                       ampEnv (p, 0.02, 0.6, 0.9, 0.8);
                       p.velocityToFilter = 0.8;
                       p.velocityRangeDb = 14.0;
                       mono (p, true, 0.05);
                       vibratoAlways (p, 6.0, 5.0, 0.5, 0.8);
                       room (p, SpaceType::room, 0.14, 1.5);
                       p.analogAmount = 0.4;
                   },
                   [] (ShellProcessor& s) { route (s, 0, Source::pressure, Dest::cutoff, 35.0f); } });
}

} // namespace ca2
