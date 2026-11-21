// FAMILY F - SLOW LIGHT (051-060): lush, evolving pads - each with harmonic evolution of its own,
// not two detuned oscillators and a long envelope.

#include "Common.h"

namespace ca2
{

void addSlowLight (std::vector<Spec>& l)
{
    const auto F = Family::slowLight;

    l.push_back ({ 51, "Slow Northern Light", F,
                   "Wide string-machine light, drifting open over long chords.",
                   "Hold wide chords for several bars.",
                   "Lush harmonic bed", "DCO saw and swept pulse through the Juno filter and its chorus; the filter slowly breathes upward.",
                   40, 88, Phrase::chords,
                   [] (Patch& p) {
                       p.osc1Model = OscModel::dco;
                       p.dco1.saw = true;
                       p.dco1.pulse = true;
                       p.dco1.pwmSource = PwmSource::lfo;
                       p.dco1.pwmDepth = 0.4;
                       p.osc1Level = 0.75;
                       p.osc2Model = OscModel::dco;
                       p.dco2.saw = true;
                       p.osc2Octave = 1;
                       p.osc2FineCents = 4.0;
                       p.osc2Level = 0.35;
                       p.filterModel = FilterModel::ota;
                       p.otaVoicing = OtaVoicing::juno;
                       p.highPass = 1;
                       p.tone = hz (2400.0);
                       p.resonance = 0.1;
                       p.keyTrack = 0.4;
                       filterEnv (p, 2.4, 4.0, 0.7, 2.5, 10.0);
                       ampEnv (p, 1.1, 2.0, 0.85, 2.6);
                       p.velocityRangeDb = 8.0;
                       p.lfo.shape = osp::mod::LfoShape::triangle;
                       p.lfo.rateHz = 0.35;
                       p.lfoAmount = 1.0;
                       p.lfoCutoff = 3.0;
                       p.chorusMode = 2;
                       p.polyphony = 8;
                       room (p, SpaceType::hall, 0.25, 4.5, 30.0, 6000.0);
                       p.analogAmount = 0.35;
                   },
                   nullptr });

    l.push_back ({ 52, "Weather Memory", F,
                   "Soft detuned triangles with a gentle tape wobble, like an old recording of weather.",
                   "Hold open chords; let the wobble do the moving.",
                   "Nostalgic pad", "Three triangles a few cents apart, a breath of noise, slow tape wow and flutter on the whole sound.",
                   40, 84, Phrase::chords,
                   [] (Patch& p) {
                       p.osc1Level = 0.0;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 6.0;
                       p.osc2Level = 0.9;
                       osc3 (p, Osc2Wave::triangle, 0, 0.0, -11.0, -4.0);
                       p.noiseLevel = 0.08;
                       p.tone = hz (1800.0);
                       p.resonance = 0.08;
                       p.keyTrack = 0.4;
                       filterEnv (p, 0.8, 2.0, 0.8, 2.0, 6.0);
                       ampEnv (p, 0.9, 2.0, 0.9, 2.5);
                       p.velocityRangeDb = 8.0;
                       p.polyphony = 8;
                       tape (p, 0.4, 0.6, 0.35, 0.4);
                       room (p, SpaceType::hall, 0.25, 4.5, 30.0, 5500.0);
                       p.analogAmount = 0.4;
                   },
                   nullptr });

    l.push_back ({ 53, "Submerged Chapel", F,
                   "A distant organ-like pad heard from under water.",
                   "Hold simple triads in the middle register.",
                   "Sacred underwater pad", "Square-ish pulses with a triangle two octaves up, the filter slowly rising and falling, a long VINTAGE hall.",
                   40, 79, Phrase::chords,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.5;
                       p.osc1Level = 0.7;
                       p.osc2Wave = Osc2Wave::pulse;
                       p.osc2Width = 0.5;
                       p.osc2Octave = 1;
                       p.osc2FineCents = 3.0;
                       p.osc2Level = 0.45;
                       osc3 (p, Osc2Wave::triangle, 2, 0.0, 0.0, -14.0);
                       p.tone = hz (900.0);
                       p.resonance = 0.15;
                       p.keyTrack = 0.4;
                       p.lfo.shape = osp::mod::LfoShape::sine;
                       p.lfo.rateHz = 0.09;
                       p.lfoAmount = 1.0;
                       p.lfoCutoff = 9.0;
                       filterEnv (p, 0.6, 2.0, 0.8, 2.5, 6.0);
                       ampEnv (p, 0.5, 1.5, 0.95, 3.0);
                       p.velocityRangeDb = 6.0;
                       p.polyphony = 8;
                       vintage (p, VintageVariant::hall, 0.35, 8.0, 40.0, 5000.0);
                       p.analogAmount = 0.3;
                   },
                   nullptr });

    l.push_back ({ 54, "Dust Bloom", F,
                   "A worn, grainy unison pad that blooms like dust in light.",
                   "Hold chords and let them bloom over a couple of seconds.",
                   "Lo-fi blooming pad", "Card A's three saws, heavy tape wear and wobble, and tape drive for the dust.",
                   40, 84, Phrase::chords,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.75;
                       unisonCard (p, 0, 3, 0.25, 0.6, 0.6);
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = 8.0;
                       p.osc2Level = 0.5;
                       p.tone = hz (1100.0);
                       p.resonance = 0.1;
                       p.keyTrack = 0.4;
                       filterEnv (p, 1.6, 2.5, 0.75, 2.5, 12.0);
                       ampEnv (p, 1.2, 2.0, 0.9, 2.8);
                       p.velocityRangeDb = 8.0;
                       p.polyphony = 8;
                       tape (p, 0.6, 0.55, 0.4, 0.75);
                       drive (p, DriveMode::tape, 0.35, 0.45, 0.55);
                       room (p, SpaceType::hall, 0.22, 4.0, 25.0, 5000.0);
                       p.analogAmount = 0.45;
                   },
                   nullptr });

    l.push_back ({ 55, "Sleepwalk Circuit", F,
                   "Two pulses breathing against each other, waking over six seconds.",
                   "Hold one chord a long time; it opens very slowly.",
                   "Slow-reveal pad", "Two pulses with their widths swept in opposite directions by the LFO and envelope, chorus movement, a six-second filter rise.",
                   40, 84, Phrase::chords,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.4;
                       p.osc1Level = 0.75;
                       p.osc2Wave = Osc2Wave::pulse;
                       p.osc2Width = 0.6;
                       p.osc2FineCents = 5.0;
                       p.osc2Level = 0.7;
                       p.lfo.shape = osp::mod::LfoShape::triangle;
                       p.lfo.rateHz = 0.3;
                       p.lfoAmount = 1.0;
                       p.lfoWidth1 = 0.2;
                       p.lfoWidth2 = -0.2;
                       p.tone = hz (500.0);
                       p.resonance = 0.15;
                       p.keyTrack = 0.4;
                       filterEnv (p, 6.0, 4.0, 0.85, 3.0, 26.0);
                       ampEnv (p, 1.0, 2.0, 0.95, 3.0);
                       p.velocityRangeDb = 6.0;
                       p.polyphony = 8;
                       chorusMovement (p, 0.35, 0.35, 0.6, 0.8);
                       room (p, SpaceType::hall, 0.25, 5.0, 35.0, 5500.0);
                       p.analogAmount = 0.35;
                   },
                   nullptr });

    l.push_back ({ 56, "The Blue Archive", F,
                   "A clean, classic polysynth pad with the Juno's chorus.",
                   "Play chords in any register; it sits easily under other parts.",
                   "Classic poly bed", "A DCO saw with its square sub, the Juno filter with its first high-pass step, chorus I.",
                   36, 88, Phrase::chords,
                   [] (Patch& p) {
                       p.osc1Model = OscModel::dco;
                       p.dco1.saw = true;
                       p.dco1.subLevel = 0.2;
                       p.osc1Level = 0.85;
                       p.osc2Level = 0.0;
                       p.filterModel = FilterModel::ota;
                       p.otaVoicing = OtaVoicing::juno;
                       p.highPass = 1;
                       p.tone = hz (1600.0);
                       p.resonance = 0.12;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.4, 1.5, 0.6, 1.5, 12.0);
                       ampEnv (p, 0.35, 1.5, 0.9, 1.8);
                       p.velocityRangeDb = 8.0;
                       p.chorusMode = 1;
                       p.polyphony = 8;
                       room (p, SpaceType::hall, 0.18, 3.0, 20.0, 6000.0);
                       p.analogAmount = 0.3;
                   },
                   nullptr });

    l.push_back ({ 57, "Pale Moss", F,
                   "A soft, rustling pad that pulses gently across the stereo field.",
                   "Hold chords; the pulse moves by itself left and right.",
                   "Living textural pad", "Card A's wide three saws an octave up, a bed of noise on card C, a soft stereo pulse.",
                   48, 88, Phrase::chords,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Octave = 1;
                       p.osc1Level = 0.6;
                       unisonCard (p, 0, 3, 0.2, 0.6, 0.9);
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 4.0;
                       p.osc2Level = 0.8;
                       noiseC (p, NoiseColor::white, 0.45, 0.5, -18.0);
                       p.tone = hz (1500.0);
                       p.resonance = 0.1;
                       p.keyTrack = 0.4;
                       filterEnv (p, 1.0, 2.0, 0.8, 2.0, 6.0);
                       ampEnv (p, 0.8, 2.0, 0.9, 2.5);
                       p.velocityRangeDb = 6.0;
                       p.polyphony = 8;
                       pulse (p, 0.3, 0.5, 0.3, 0.9);
                       room (p, SpaceType::hall, 0.25, 4.5, 30.0, 6000.0);
                       p.analogAmount = 0.3;
                   },
                   nullptr });

    l.push_back ({ 58, "Silent Meridian", F,
                   "A hollow pad of stacked fifths and fourths, open and unresolved.",
                   "Play single notes or simple shapes; the intervals are built in.",
                   "Open-interval pad", "A saw, a fifth above and a fourth above that on card C, a dark ABYSS.",
                   36, 76, Phrase::drone,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.75;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2Coarse = 7.0;
                       p.osc2FineCents = 3.0;
                       p.osc2Level = 0.55;
                       osc3 (p, Osc2Wave::triangle, 1, 0.0, -4.0, -8.0);
                       p.tone = hz (800.0);
                       p.resonance = 0.1;
                       p.keyTrack = 0.4;
                       filterEnv (p, 1.2, 3.0, 0.8, 3.0, 8.0);
                       ampEnv (p, 1.4, 2.0, 0.95, 3.5);
                       p.velocityRangeDb = 6.0;
                       p.polyphony = 4;
                       abyss (p, AbyssVariant::dark, 0.32, 11.0, 40.0, 4000.0);
                       p.analogAmount = 0.35;
                   },
                   nullptr });

    l.push_back ({ 59, "Glass Fog", F,
                   "A misty, shimmering pad with faint metallic overtones.",
                   "Hold chords high in the middle register.",
                   "Shimmering haze", "A pulse an octave up over a triangle with a little ring for the glass, chorus movement, a long VINTAGE plate.",
                   48, 88, Phrase::chords,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Octave = 1;
                       p.osc1Width = 0.35;
                       p.osc1Level = 0.5;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 0.85;
                       p.ringMix = 0.18;
                       p.tone = hz (2600.0);
                       p.resonance = 0.12;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.9, 2.0, 0.8, 2.5, 6.0);
                       ampEnv (p, 0.8, 2.0, 0.9, 3.0);
                       p.velocityRangeDb = 6.0;
                       p.polyphony = 8;
                       chorusMovement (p, 0.35, 0.4, 0.7, 0.8);
                       vintage (p, VintageVariant::plate, 0.32, 6.0, 25.0, 8000.0);
                       p.analogAmount = 0.25;
                   },
                   nullptr });

    l.push_back ({ 60, "Dawn Without Sun", F,
                   "A wide unison pad whose held notes swell in more slowly than the first chord.",
                   "Play a chord, then add notes over it; the added notes rise in like a dawn.",
                   "Evolving dawn pad", "Card B's three saws, a two-and-a-half-second filter rise, and AMP 2 giving later keys a two-second swell.",
                   40, 84, Phrase::chords,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.7;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = 6.0;
                       p.osc2Level = 0.65;
                       unisonCard (p, 1, 3, 0.22, 0.6, 0.7);
                       p.tone = hz (650.0);
                       p.resonance = 0.12;
                       p.keyTrack = 0.4;
                       filterEnv (p, 2.5, 3.0, 0.8, 3.0, 18.0);
                       ampEnv (p, 0.6, 2.0, 0.95, 3.0);
                       p.velocityRangeDb = 6.0;
                       p.polyphony = 8;
                       amp2 (p, 2.0);
                       room (p, SpaceType::hall, 0.28, 6.0, 40.0, 5500.0);
                       p.analogAmount = 0.35;
                   },
                   nullptr });
}

} // namespace ca2
