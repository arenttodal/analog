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

    l.push_back ({ 62, "Wooden Horizon", F,
                   "A soft, woody woodwind ensemble impression with a gentle reedy edge.",
                   "Play close-voiced chords or slow lines; the vibrato arrives on its own.",
                   "Woodwind section", "A pulse with a quick, shallow PWM wobble and an EQ bell for the wood, self-timed vibrato, a small hall.",
                   48, 84, Phrase::chords,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.3;
                       p.osc1Level = 0.85;
                       p.lfo.shape = osp::mod::LfoShape::sine;
                       p.lfo.rateHz = 5.5;
                       p.lfoAmount = 1.0;
                       p.lfoWidth1 = 0.05;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2FineCents = 3.0;
                       p.osc2Level = 0.5;
                       eqBell (p.eqA, 1200.0, 5.0, 1.5);
                       p.tone = hz (1500.0);
                       p.resonance = 0.12;
                       p.keyTrack = 0.6;
                       filterEnv (p, 0.08, 0.6, 0.7, 0.5, 8.0);
                       ampEnv (p, 0.07, 0.5, 0.9, 0.5);
                       p.velocityToFilter = 0.5;
                       p.velocityRangeDb = 14.0;
                       p.polyphony = 8;
                       vibratoAlways (p, 9.0, 5.3, 0.4, 0.6);
                       room (p, SpaceType::hall, 0.18, 2.5, 20.0, 6500.0);
                       p.analogAmount = 0.3;
                   },
                   nullptr });

    l.push_back ({ 63, "Winter Horns", F,
                   "A warm, distant French-horn section for slow chords.",
                   "Play soft, slow chords low in the middle register.",
                   "Horn section", "Two mellow saws through a dark ladder with a slow brassy rise, a breath of tube drive, a four-second VINTAGE hall.",
                   40, 76, Phrase::chords,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.8;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = 5.0;
                       p.osc2Level = 0.65;
                       p.tone = hz (450.0);
                       p.resonance = 0.12;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.18, 0.8, 0.5, 0.6, 18.0);
                       ampEnv (p, 0.12, 0.6, 0.9, 0.8);
                       p.velocityToFilter = 0.7;
                       p.velocityRangeDb = 14.0;
                       p.polyphony = 8;
                       vibratoAlways (p, 6.0, 5.0, 0.5, 0.8);
                       drive (p, DriveMode::tube, 0.12, 0.45, 0.55);
                       vintage (p, VintageVariant::hall, 0.3, 4.0, 30.0, 6000.0);
                       p.analogAmount = 0.35;
                   },
                   nullptr });

    l.push_back ({ 64, "Signal Brass", F,
                   "A bright, punchy brass stab for hits and fanfares.",
                   "Short chords, played hard; let them ring into the room.",
                   "Brass stab", "Two saws and a third an octave up on card C, a fast filter stab, a little crunch, a tight room.",
                   45, 81, Phrase::keys,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.8;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = 7.0;
                       p.osc2Level = 0.7;
                       osc3 (p, Osc2Wave::saw, 1, 0.0, 4.0, -10.0);
                       p.tone = hz (600.0);
                       p.resonance = 0.15;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.03, 0.35, 0.35, 0.35, 30.0);
                       ampEnv (p, 0.01, 0.5, 0.75, 0.4);
                       p.velocityToFilter = 0.8;
                       p.velocityRangeDb = 14.0;
                       p.polyphony = 8;
                       drive (p, DriveMode::crunch, 0.15, 0.5, 0.5);
                       room (p, SpaceType::room, 0.18, 1.4, 10.0, 7000.0);
                       p.analogAmount = 0.3;
                   },
                   nullptr });

    l.push_back ({ 65, "Amber Strings", F,
                   "A warm string-machine ensemble with the Juno's double chorus.",
                   "Hold chords across the middle of the keyboard.",
                   "String ensemble", "A DCO pulse with slow PWM and a DCO saw an octave up through the Juno filter, chorus I and II together.",
                   40, 88, Phrase::chords,
                   [] (Patch& p) {
                       p.osc1Model = OscModel::dco;
                       p.dco1.pulse = true;
                       p.dco1.saw = false;
                       p.dco1.pwmSource = PwmSource::lfo;
                       p.dco1.pwmDepth = 0.5;
                       p.osc1Level = 0.75;
                       p.osc2Model = OscModel::dco;
                       p.dco2.saw = true;
                       p.osc2Octave = 1;
                       p.osc2FineCents = 3.0;
                       p.osc2Level = 0.5;
                       p.filterModel = FilterModel::ota;
                       p.otaVoicing = OtaVoicing::juno;
                       p.highPass = 1;
                       p.tone = hz (2200.0);
                       p.resonance = 0.08;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.4, 1.5, 0.8, 1.5, 6.0);
                       ampEnv (p, 0.35, 1.5, 0.95, 1.8);
                       p.velocityRangeDb = 8.0;
                       p.lfo.shape = osp::mod::LfoShape::triangle;
                       p.lfo.rateHz = 0.6;
                       p.chorusMode = 3;
                       p.polyphony = 8;
                       room (p, SpaceType::hall, 0.22, 3.5, 25.0, 6000.0);
                       p.analogAmount = 0.3;
                   },
                   nullptr });

    l.push_back ({ 66, "Low Chamber", F,
                   "Dark, close low strings for tension and weight.",
                   "Play low, slow lines and open fifths.",
                   "Low string section", "Card A's three saws through a dark ladder with a slow bow-like attack; range kept low.",
                   28, 64, Phrase::chords,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.8;
                       unisonCard (p, 0, 3, 0.18, 0.55, 0.4);
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = 5.0;
                       p.osc2Level = 0.55;
                       p.tone = hz (500.0);
                       p.resonance = 0.12;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.35, 1.0, 0.7, 1.2, 10.0);
                       ampEnv (p, 0.3, 1.0, 0.95, 1.4);
                       p.velocityToFilter = 0.5;
                       p.velocityRangeDb = 10.0;
                       p.polyphony = 8;
                       vibratoAlways (p, 5.0, 4.8, 0.6, 0.8);
                       room (p, SpaceType::hall, 0.2, 3.5, 25.0, 4500.0);
                       p.analogAmount = 0.35;
                   },
                   nullptr });

    l.push_back ({ 67, "Fire Procession", F,
                   "A heroic brass ensemble that flares with every accent.",
                   "Accent with velocity; lean into held chords with aftertouch.",
                   "Heroic brass", "Two saws whose filter follows velocity fully, late vibrato, aftertouch opening the section.",
                   40, 80, Phrase::chords,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.8;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = 8.0;
                       p.osc2Level = 0.7;
                       p.tone = hz (500.0);
                       p.resonance = 0.15;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.08, 0.7, 0.5, 0.6, 26.0);
                       ampEnv (p, 0.05, 0.6, 0.9, 0.7);
                       p.velocityToFilter = 1.0;
                       p.velocityRangeDb = 14.0;
                       p.polyphony = 8;
                       vibratoAlways (p, 7.0, 5.2, 0.6, 0.8);
                       room (p, SpaceType::hall, 0.22, 3.5, 25.0, 6000.0);
                       p.analogAmount = 0.35;
                   },
                   [] (ShellProcessor& s) { route (s, 0, Source::pressure, Dest::cutoff, 35.0f); } });

    l.push_back ({ 68, "Hollow Reeds", F,
                   "A thin, hollow reed ensemble with a fluttering edge.",
                   "Play lines and simple chords; the flutter is built in.",
                   "Reed choir", "A narrow pulse and a triangle an octave up, a little noise, a fast LFO fluttering the filter.",
                   48, 84, Phrase::chords,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::pulse;
                       p.osc1Width = 0.25;
                       p.osc1Level = 0.8;
                       p.osc2Wave = Osc2Wave::triangle;
                       p.osc2Octave = 1;
                       p.osc2FineCents = 2.0;
                       p.osc2Level = 0.5;
                       p.noiseLevel = 0.06;
                       p.tone = hz (1300.0);
                       p.resonance = 0.2;
                       p.keyTrack = 0.6;
                       p.lfo.shape = osp::mod::LfoShape::sine;
                       p.lfo.rateHz = 4.5;
                       p.lfoAmount = 1.0;
                       p.lfoCutoff = 1.5;
                       filterEnv (p, 0.06, 0.5, 0.7, 0.5, 8.0);
                       ampEnv (p, 0.05, 0.5, 0.9, 0.5);
                       p.velocityToFilter = 0.5;
                       p.velocityRangeDb = 12.0;
                       p.polyphony = 8;
                       room (p, SpaceType::room, 0.16, 1.6, 12.0, 7000.0);
                       p.analogAmount = 0.3;
                   },
                   nullptr });

    l.push_back ({ 69, "Ashen Fanfare", F,
                   "A grand, darkened brass fanfare with low octave support.",
                   "Play bold chords; they are big enough on their own.",
                   "Grand fanfare", "Card A's three saws, a saw an octave down on card C, the filter held at 1.4 kHz, a VINTAGE hall.",
                   40, 80, Phrase::chords,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Level = 0.75;
                       unisonCard (p, 0, 3, 0.2, 0.55, 0.5);
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = 6.0;
                       p.osc2Level = 0.6;
                       osc3 (p, Osc2Wave::saw, -1, 0.0, -3.0, -9.0);
                       p.tone = hz (1400.0);
                       p.resonance = 0.12;
                       p.keyTrack = 0.5;
                       filterEnv (p, 0.1, 0.8, 0.6, 0.8, 14.0);
                       ampEnv (p, 0.06, 0.7, 0.9, 1.0);
                       p.velocityToFilter = 0.7;
                       p.velocityRangeDb = 12.0;
                       p.polyphony = 8;
                       vintage (p, VintageVariant::hall, 0.3, 5.0, 30.0, 5500.0);
                       p.analogAmount = 0.35;
                   },
                   nullptr });

    l.push_back ({ 70, "Frozen Ensemble", F,
                   "A glassy high ensemble, wide and cold, blooming into a wash.",
                   "Hold high chords; the wash keeps growing.",
                   "Glacial high strings", "A saw an octave up with card B's four copies spread wide, high-passed, through the Jupiter OTA, into BLOOM's wash.",
                   55, 96, Phrase::chords,
                   [] (Patch& p) {
                       p.osc1Wave = Osc1Wave::saw;
                       p.osc1Octave = 1;
                       p.osc1Level = 0.6;
                       p.osc2Wave = Osc2Wave::saw;
                       p.osc2FineCents = 5.0;
                       p.osc2Level = 0.7;
                       unisonCard (p, 1, 4, 0.25, 0.6, 0.9);
                       p.highPass = 2;
                       p.filterModel = FilterModel::ota;
                       p.otaVoicing = OtaVoicing::jupiter;
                       p.tone = hz (4500.0);
                       p.resonance = 0.1;
                       p.keyTrack = 0.4;
                       filterEnv (p, 0.6, 1.5, 0.85, 2.0, 4.0);
                       ampEnv (p, 0.5, 1.5, 0.95, 2.5);
                       p.velocityRangeDb = 8.0;
                       p.polyphony = 8;
                       bloom (p, BloomVariant::wash, 0.35, 8.0);
                       p.analogAmount = 0.25;
                   },
                   nullptr });
}

} // namespace ca2
