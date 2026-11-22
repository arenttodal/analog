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

    l.push_back ({ 92, "Hollow Felt", F,
                   "A soft, felted electric-piano impression, warm and slightly hollow.",
                   "Play chords and voicings as on an electric piano; velocity sets the bark.",
                   "Intimate keys", "A triangle and soft pulse with a quick velocity-led filter strike and a gentle decay, the Juno chorus I.",
                   36, 91, Phrase::keys,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.4;
                       p.osc1Level = 0.5;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 2.0;
                       p.osc2Level = 0.9;
                       p.tone = hz (900.0);
                       p.resonance = 0.08;
                       p.keyTrack = 0.6;
                       filterEnv (p, 0.002, 0.6, 0.2, 0.5, 20.0);
                       ampEnv (p, 0.002, 2.2, 0.0, 0.5);
                       p.velocityToFilter = 0.8;
                       p.velocityRangeDb = 18.0;
                       p.chorusMode = 1;
                       p.polyphony = 16;
                       room (p, SpaceType::room, 0.14, 1.4, 10.0, 6500.0);
                       p.analogAmount = 0.3;
                   },
                   nullptr });

    l.push_back ({ 93, "After Rain", F,
                   "A soft mallet whose notes drip into a long, dark space.",
                   "Play sparse notes and leave room for the tail.",
                   "Reflective mallet", "A triangle with a quiet sine-like octave, a fast mallet strike, a twelve-second dark ABYSS.",
                   48, 91, Phrase::keys,
                   [] (Patch& p) {
                       p.osc1Level = 0.0;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 1.0;
                       osc3 (p, Osc2Wave::triangle, 2, 0.0, 0.0, -14.0);
                       p.tone = hz (2200.0);
                       p.resonance = 0.08;
                       p.keyTrack = 0.7;
                       filterEnv (p, 0.001, 0.3, 0.1, 0.5, 18.0);
                       ampEnv (p, 0.001, 1.4, 0.0, 1.0);
                       p.velocityToFilter = 0.6;
                       p.velocityRangeDb = 18.0;
                       p.polyphony = 16;
                       abyss (p, AbyssVariant::dark, 0.35, 12.0, 30.0, 4500.0);
                       p.analogAmount = 0.25;
                   },
                   nullptr });

    l.push_back ({ 94, "Antique Circuit", F,
                   "A worn little square-wave organ from a forgotten room.",
                   "Play chords and simple melodies; it is meant to sound old.",
                   "Vintage toy organ", "Square pulses an octave apart, the Juno chorus II, and tape wear for the age.",
                   36, 88, Phrase::keys,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.5;
                       p.osc1Level = 0.75;
                       p.osc2Wave = Osc2Wave::pulse;
                       p.osc2Width = 0.5;
                       p.osc2Octave = 1;
                       p.osc2FineCents = 2.0;
                       p.osc2Level = 0.45;
                       p.tone = hz (2000.0);
                       p.resonance = 0.08;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.005, 0.5, 0.9, 0.2, 4.0);
                       ampEnv (p, 0.005, 0.5, 0.95, 0.15);
                       p.velocityRangeDb = 6.0;
                       p.chorusMode = 2;
                       p.polyphony = 8;
                       tape (p, 0.4, 0.5, 0.4, 0.7);
                       room (p, SpaceType::room, 0.14, 1.3);
                       p.analogAmount = 0.35;
                   },
                   nullptr });

    l.push_back ({ 95, "Snowglass", F,
                   "A clear, ringing bell that decays over three seconds.",
                   "Play single notes and open intervals high on the keyboard.",
                   "Crystal bell", "Strong ring modulation of two pure tones a ninth apart; a struck envelope that rings away.",
                   55, 96, Phrase::keys,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.5;
                       p.osc1Level = 0.4;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2Coarse = 14.0;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 1.0;
                       p.ringMix = 0.7;
                       p.levelDb = 4.0;
                       p.tone = hz (4000.0);
                       p.resonance = 0.05;
                       p.keyTrack = 0.7;
                       filterEnv (p, 0.001, 0.8, 0.2, 1.0, 10.0);
                       ampEnv (p, 0.001, 3.0, 0.0, 2.0);
                       p.velocityRangeDb = 18.0;
                       p.polyphony = 16;
                       room (p, SpaceType::plate, 0.25, 3.0, 15.0, 9000.0);
                       p.analogAmount = 0.15;
                   },
                   nullptr });

    l.push_back ({ 96, "Iron Rain", F,
                   "A hard, metallic percussive hit like rain on iron.",
                   "Play rhythms; short, hard notes work best.",
                   "Metallic percussion", "Ring modulation and a lot of noise, an envelope only a seventh of a second long.",
                   36, 84, Phrase::keys,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.3;
                       p.osc1Level = 1.0;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2Coarse = 11.0;
                       p.osc2FineCents = 30.0;
                       p.osc2Level = 1.0;
                       p.ringMix = 0.6;
                       p.noiseLevel = 0.6;
                       p.tone = hz (3000.0);
                       p.resonance = 0.25;
                       p.keyTrack = 0.6;
                       filterEnv (p, 0.001, 0.12, 0.0, 0.12, 24.0);
                       ampEnv (p, 0.001, 0.14, 0.0, 0.14);
                       p.velocityToFilter = 0.6;
                       p.velocityRangeDb = 20.0;
                       p.polyphony = 16;
                       room (p, SpaceType::room, 0.2, 1.4, 8.0, 8000.0);
                       p.analogAmount = 0.2;
                   },
                   nullptr });

    l.push_back ({ 97, "Falling Embers", F,
                   "A pluck whose pitch falls a little as it strikes, echoing away.",
                   "Play broken chords; let the echo carry them.",
                   "Glowing pluck", "Poly Mod drops OSC 1's pitch from the filter envelope on each strike; a dotted tape echo carries the embers.",
                   48, 88, Phrase::keys,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.8;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 4.0;
                       p.osc2Level = 0.6;
                       p.polyModEnv = 0.25;
                       p.polyModToPitch1 = true;
                       p.tone = hz (1200.0);
                       p.resonance = 0.2;
                       p.keyTrack = 0.6;
                       filterEnv (p, 0.001, 0.3, 0.0, 0.3, 24.0);
                       ampEnv (p, 0.001, 0.8, 0.0, 0.6);
                       p.velocityToFilter = 0.6;
                       p.velocityRangeDb = 16.0;
                       p.polyphony = 16;
                       echo (p, EchoType::tape, 0.22, div::eighthDotted, 0.45, 0.45, EchoStereo::pingPong);
                       room (p, SpaceType::hall, 0.18, 3.0);
                       p.analogAmount = 0.3;
                   },
                   nullptr });

    l.push_back ({ 98, "Black Door", F,
                   "A heavy, dark stab for doors slamming in a thriller.",
                   "Short, low chords, hard.",
                   "Thriller stab", "Saw and a pulse on card C, the SUB underneath, tube drive, a dark room.",
                   28, 64, Phrase::keys,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.85;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = 10.0;
                       p.osc2Level = 0.6;
                       osc3 (p, Osc2Wave::pulse, 0, 0.0, -6.0, -8.0);
                       sub (p, SubWave::sine, -1, -8.0, 24, 60);
                       p.tone = hz (500.0);
                       p.resonance = 0.2;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.002, 0.35, 0.2, 0.5, 28.0);
                       ampEnv (p, 0.002, 0.7, 0.3, 0.6);
                       p.velocityToFilter = 0.7;
                       p.velocityRangeDb = 12.0;
                       p.polyphony = 8;
                       drive (p, DriveMode::tube, 0.3, 0.4, 0.6);
                       room (p, SpaceType::hall, 0.2, 2.5, 15.0, 4000.0);
                       p.analogAmount = 0.35;
                   },
                   nullptr });

    l.push_back ({ 99, "Reverse Signal", F,
                   "A sound that seems to play backwards, rising out of nothing.",
                   "Hold notes for four seconds or more; release before the next.",
                   "Reverse swell", "A narrow pulse and a saw rising over four seconds as the filter opens; the ASCEND echo climbs out of the swell and keeps rising after the note stops.",
                   36, 76, Phrase::drone,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.15;
                       p.osc1Level = 0.8;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = 7.0;
                       p.osc2Level = 0.65;
                       p.tone = hz (350.0);
                       p.resonance = 0.2;
                       p.keyTrack = 0.4;
                       filterEnv (p, 4.5, 1.0, 1.0, 0.3, 40.0);
                       ampEnv (p, 4.0, 1.0, 1.0, 0.25);
                       p.velocityRangeDb = 6.0;
                       p.polyphony = 8;
                       echo (p, EchoType::ascend, 0.25, div::quarter, 0.5, 0.45, EchoStereo::wide);
                       room (p, SpaceType::plate, 0.18, 2.5, 20.0, 6000.0);
                       p.analogAmount = 0.3;
                   },
                   nullptr });

    l.push_back ({ 100, "Final Exposure", F,
                   "The last image: a wide, slow blooming chord that fades into a wash.",
                   "Play one chord and add notes to it; let it fade completely.",
                   "Closing-credits pad", "A triangle and card A's five wide saws; later keys swell in over a second and a half; a six-second release into BLOOM's wash.",
                   40, 88, Phrase::chords,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.65;
                       unisonCard (p, 0, 5, 0.25, 0.6, 0.9);
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 3.0;
                       p.osc2Level = 0.8;
                       p.tone = hz (900.0);
                       p.resonance = 0.1;
                       p.keyTrack = 0.4;
                       filterEnv (p, 1.5, 3.0, 0.85, 6.0, 12.0);
                       ampEnv (p, 0.8, 2.0, 0.95, 6.0);
                       p.velocityRangeDb = 6.0;
                       p.polyphony = 8;
                       amp2 (p, 1.5);
                       bloom (p, BloomVariant::wash, 0.4, 10.0);
                       p.analogAmount = 0.3;
                   },
                   nullptr });
}

} // namespace ca2
