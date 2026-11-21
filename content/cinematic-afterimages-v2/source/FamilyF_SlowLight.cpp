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
}

} // namespace ca2
