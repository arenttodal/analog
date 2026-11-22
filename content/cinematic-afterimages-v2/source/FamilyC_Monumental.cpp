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

    l.push_back ({ 22, "Titan Footfall", F,
                   "A slow, colossal swell that lands with force and keeps expanding.",
                   "Hold single low notes or fifths; give each one time.",
                   "Trailer-scale impact swell", "A forceful first half-second (its own envelope peak), then the filter keeps opening for seconds in a dark, short room.",
                   24, 52, Phrase::drone,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Octave = -1;
                       p.osc1Level = 0.85;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = 5.0;
                       p.osc2Level = 0.7;
                       sub (p, SubWave::sine, -1, -12.0, 36, 60);
                       p.tone = hz (240.0);
                       p.resonance = 0.18;
                       p.keyTrack = 0.4;
                       filterEnv (p, 0.8, 3.0, 0.5, 2.0, 30.0);
                       ampEnv (p, 0.35, 1.5, 0.9, 2.0);
                       p.velocityToFilter = 0.4;
                       p.velocityRangeDb = 10.0;
                       p.polyphony = 4;
                       drive (p, DriveMode::tube, 0.25, 0.35, 0.8);
                       room (p, SpaceType::room, 0.2, 1.6, 10.0, 2500.0, 80.0);
                       p.analogAmount = 0.3;
                   },
                   nullptr });

    l.push_back ({ 23, "Gravity Well", F,
                   "A sub-bass that seems to bend inward as it sinks, a distorted layer growing above.",
                   "Long single notes; feel it fall.",
                   "Ominous descent", "The filter's envelope runs downward over seconds while Poly Mod pulls the upper saw's pitch in; a CRUNCH stage roughens what is left.",
                   24, 50, Phrase::drone,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Octave = 1;
                       p.osc1Level = 0.35;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 1.0;
                       p.polyModEnv = 0.12;
                       p.polyModToPitch1 = true;
                       sub (p, SubWave::sine, -1, -10.0, 36, 60);
                       p.tone = hz (900.0);
                       p.resonance = 0.25;
                       p.keyTrack = 0.4;
                       filterEnv (p, 0.01, 5.0, 0.0, 2.0, -24.0);
                       ampEnv (p, 0.02, 2.0, 1.0, 1.5);
                       p.velocityRangeDb = 8.0;
                       mono (p, true, 0.05);
                       drive (p, DriveMode::crunch, 0.35, 0.35, 0.7);
                       p.analogAmount = 0.35;
                   },
                   nullptr });

    l.push_back ({ 24, "Cold Continent", F,
                   "A frozen, choir-like low synthesizer for long held chords.",
                   "Hold minor chords low on the keyboard.",
                   "Glacial low pad", "DCO saw and swept pulse an octave apart, a breath of noise on card C, the Juno chorus and a deep ABYSS.",
                   28, 64, Phrase::chords,
                   [] (Patch& p) {
                       p.osc1Model = OscModel::dco;
                       p.dco1.saw = true;
                       p.dco1.pulse = true;
                       p.dco1.pwmSource = PwmSource::lfo;
                       p.dco1.pwmDepth = 0.35;
                       p.osc1Level = 0.75;
                       p.osc2Model = OscModel::dco;
                       p.dco2.saw = true;
                       p.osc2Octave = -1;
                       p.osc2FineCents = 6.0;
                       p.osc2Level = 0.6;
                       noiseC (p, NoiseColor::juno, 0.5, 0.4, -26.0);
                       p.filterModel = FilterModel::ota;
                       p.otaVoicing = OtaVoicing::juno;
                       p.tone = hz (1200.0);
                       p.resonance = 0.15;
                       p.keyTrack = 0.4;
                       filterEnv (p, 1.5, 3.0, 0.7, 3.0, 8.0);
                       ampEnv (p, 1.4, 2.0, 0.9, 3.0);
                       p.velocityRangeDb = 6.0;
                       p.lfo.rateHz = 0.4;
                       p.chorusMode = 3;
                       p.polyphony = 8;
                       abyss (p, AbyssVariant::deep, 0.3, 8.0, 30.0, 4500.0);
                       p.analogAmount = 0.3;
                   },
                   nullptr });

    l.push_back ({ 25, "The Hollow Sun", F,
                   "A huge, warm bass that reveals a distorted edge the longer you hold it.",
                   "Hold notes for several seconds and listen to them change.",
                   "Slow-burn tension bass", "A four-second filter attack pushes an ever-brighter signal into the ladder's drive: the distortion arrives with time, not velocity.",
                   24, 52, Phrase::drone,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.85;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2Octave = -1;
                       p.osc2FineCents = 4.0;
                       p.osc2Level = 0.75;
                       sub (p, SubWave::sine, -1, -12.0, 36, 60);
                       p.tone = hz (220.0);
                       p.resonance = 0.2;
                       p.keyTrack = 0.4;
                       p.filterDrive = 0.6;
                       filterEnv (p, 4.0, 6.0, 0.85, 2.0, 22.0);
                       ampEnv (p, 0.05, 2.0, 1.0, 1.8);
                       p.velocityRangeDb = 8.0;
                       mono (p, true, 0.05);
                       drive (p, DriveMode::tube, 0.35, 0.45, 0.75);
                       room (p, SpaceType::hall, 0.15, 3.0, 20.0, 3500.0, 100.0);
                       p.analogAmount = 0.35;
                   },
                   nullptr });

    l.push_back ({ 26, "Abyssal Beacon", F,
                   "A dark, pulsing bass drone with a faint resonant tone far above it.",
                   "Hold a low note and let the beacon pulse.",
                   "Deep-sea suspense drone", "Very slow PWM on two square waves an octave apart, a PULSE movement, and card C's triangle two octaves up as the distant beacon.",
                   24, 55, Phrase::drone,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.5;
                       p.osc1Level = 0.8;
                       p.osc2Wave = Osc2Wave::pulse;
                       p.osc2Width = 0.5;
                       p.osc2Octave = -1;
                       p.osc2Level = 0.6;
                       p.lfo.shape = osp::mod::LfoShape::sine;
                       p.lfo.rateHz = 0.07;
                       p.lfoAmount = 1.0;
                       p.lfoWidth1 = 0.35;
                       p.lfoWidth2 = 0.3;
                       osc3 (p, Osc2Wave::triangle, 2, 0.0, 0.0, -24.0);
                       p.tone = hz (400.0);
                       p.resonance = 0.4;
                       p.keyTrack = 0.4;
                       filterEnv (p, 1.0, 3.0, 0.6, 3.0, 6.0);
                       ampEnv (p, 1.2, 2.0, 1.0, 4.0);
                       p.velocityRangeDb = 6.0;
                       p.polyphony = 4;
                       pulse (p, 0.25, 0.2, 0.5, 0.2);
                       abyss (p, AbyssVariant::deep, 0.35, 8.0, 40.0, 4000.0);
                       p.analogAmount = 0.35;
                   },
                   nullptr });

    l.push_back ({ 27, "Pressure Vault", F,
                   "A three-oscillator low instrument, compressed and dark, in a tight space.",
                   "Low chords and pedal tones; it stays controlled.",
                   "Dense low ensemble", "Saw, pulse and an octave-down saw packed into a driven ladder that breathes slowly; only short reflections around it.",
                   24, 55, Phrase::chords,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.75;
                       p.osc2Wave = Osc2Wave::pulse;
                       p.osc2Width = 0.35;
                       p.osc2FineCents = 5.0;
                       p.osc2Level = 0.6;
                       osc3 (p, Osc2Wave::saw, -1, 0.0, -4.0, -6.0);
                       p.tone = hz (500.0);
                       p.resonance = 0.2;
                       p.keyTrack = 0.4;
                       p.filterDrive = 0.35;
                       p.lfo.shape = osp::mod::LfoShape::sine;
                       p.lfo.rateHz = 0.2;
                       p.lfoAmount = 1.0;
                       p.lfoCutoff = 6.0;
                       filterEnv (p, 0.1, 1.0, 0.5, 0.8, 10.0);
                       ampEnv (p, 0.05, 1.0, 0.95, 1.0);
                       p.velocityRangeDb = 8.0;
                       p.polyphony = 4;
                       drive (p, DriveMode::tube, 0.3, 0.4, 0.8);
                       room (p, SpaceType::room, 0.12, 0.7, 4.0, 4000.0, 120.0);
                       p.analogAmount = 0.3;
                   },
                   nullptr });

    l.push_back ({ 28, "Black Ice Passage", F,
                   "Glass-like resonance ringing above a stable, dark bass.",
                   "Hold low notes; the glass sings on its own.",
                   "Cold atmospheric bass", "A whisper of ring modulation from a high fifth, a sharply resonant key-tracked ladder for the glass, a SUB for the floor.",
                   24, 55, Phrase::drone,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Octave = 1;
                       p.osc1Coarse = 7.0;
                       p.osc1Level = 0.2;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 1.0;
                       p.ringMix = 0.3;
                       sub (p, SubWave::sine, -1, -11.0, 36, 60);
                       p.tone = hz (1800.0);
                       p.resonance = 0.55;
                       p.keyTrack = 1.0;
                       filterEnv (p, 0.5, 2.0, 0.7, 2.0, 6.0);
                       ampEnv (p, 0.3, 1.5, 1.0, 2.5);
                       p.velocityRangeDb = 6.0;
                       p.polyphony = 4;
                       room (p, SpaceType::plate, 0.2, 3.5, 20.0, 7000.0);
                       p.analogAmount = 0.25;
                   },
                   nullptr });

    l.push_back ({ 29, "Tectonic Choir", F,
                   "Stacked low analog voices, deliberately detuned, for ominous chords.",
                   "Play slow, dark chords; avoid fast passages.",
                   "Ominous low chords", "Card A's four copies and card B's three spread the detune musically; the Jupiter OTA keeps the low end round; a vintage hall.",
                   28, 60, Phrase::chords,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.75;
                       unisonCard (p, 0, 4, 0.35, 0.65, 0.7);
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = -12.0;
                       p.osc2Level = 0.6;
                       unisonCard (p, 1, 3, 0.3, 0.6, 0.6);
                       p.filterModel = FilterModel::ota;
                       p.otaVoicing = OtaVoicing::jupiter;
                       p.tone = hz (900.0);
                       p.resonance = 0.2;
                       p.keyTrack = 0.4;
                       filterEnv (p, 1.2, 3.0, 0.6, 2.5, 12.0);
                       ampEnv (p, 0.6, 2.0, 0.95, 2.5);
                       p.velocityRangeDb = 8.0;
                       p.polyphony = 8;
                       vintage (p, VintageVariant::hall, 0.25, 5.0, 30.0, 5000.0);
                       p.analogAmount = 0.4;
                   },
                   nullptr });

    l.push_back ({ 30, "Heavy Weather", F,
                   "A wind-blown cinematic bass with storms moving through it.",
                   "Hold low notes; the weather keeps changing.",
                   "Storm underscore", "Card C's moving Juno noise is the wind; a slow LFO sweeps the resonant ladder through it; a deep, diffuse ABYSS tail.",
                   24, 55, Phrase::drone,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Octave = -1;
                       p.osc1Level = 0.7;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 0.0;
                       p.osc2Level = 0.8;
                       noiseC (p, NoiseColor::juno, 0.6, 0.8, -14.0);
                       p.tone = hz (350.0);
                       p.resonance = 0.35;
                       p.keyTrack = 0.35;
                       p.lfo.shape = osp::mod::LfoShape::sine;
                       p.lfo.rateHz = 0.13;
                       p.lfoAmount = 1.0;
                       p.lfoCutoff = 10.0;
                       filterEnv (p, 1.0, 3.0, 0.6, 3.0, 6.0);
                       ampEnv (p, 0.8, 2.0, 1.0, 3.0);
                       p.velocityRangeDb = 6.0;
                       p.polyphony = 4;
                       abyss (p, AbyssVariant::deep, 0.33, 8.0, 30.0, 4000.0);
                       p.analogAmount = 0.4;
                   },
                   nullptr });
}

} // namespace ca2
