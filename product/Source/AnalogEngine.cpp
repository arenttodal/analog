#include "AnalogEngine.h"

#include "Oscillator.h"

namespace andor::analog
{

andor::shell::Engine::Description AnalogEngine::describe() const
{
    Description d;
    d.name = "ANDOR ANALOG";
    d.diagnostic = false;
    return d;
}

void AnalogEngine::prepare (double sampleRate, int maximumBlockSize, juce::AudioProcessorValueTreeState& parameters)
{
    reader.attach (parameters);
    synth.prepare (sampleRate, std::max (1, maximumBlockSize), oversamplingFactor);
    auto patch = reader.read();
    patch.effects.shaper.customSteps = shaperPattern;
    synth.setPatch (patch);
    synth.setKeyDna (reader.keyDna (patch.seed));
    synth.reset();
    scratch.assign (static_cast<std::size_t> (std::max (1, maximumBlockSize)), 0.0f);
    prepared = true;
}

void AnalogEngine::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Performance& performance) noexcept
{
    const int total = buffer.getNumSamples();
    if (! prepared || total <= 0 || buffer.getNumChannels() == 0)
        return;
    auto patch = reader.read();
    patch.effects.shaper.customSteps = shaperPattern;   // MOVEMENT SHAPER's CUSTOM (the shell's)
    synth.setPatch (patch);
    synth.setKeyDna (reader.keyDna (patch.seed));       // recomputed only when it changes
    osp::HostTiming timing;
    timing.valid = performance.hasPosition;
    timing.playing = performance.playing;
    timing.bpm = performance.bpm;
    timing.ppq = performance.ppq;
    timing.numerator = performance.numerator;
    timing.denominator = performance.denominator;
    synth.setTransport (timing);

    // The on-screen wheels arrive here at the block's start; the host's (MIDI) wheels at
    // their own samples below. A wheel the MIDI moves in this block is the MIDI's.
    bool midiBend = false, midiMod = false;
    for (const auto m : midi)
    {
        const auto* d = m.data;
        midiBend = midiBend || (m.numBytes >= 3 && (d[0] & 0xf0) == 0xe0);
        midiMod = midiMod || (m.numBytes >= 3 && (d[0] & 0xf0) == 0xb0 && d[1] == 1);
    }
    if (! midiBend && ! juce::exactlyEqual (performance.pitchWheel, lastScreenPitch))
        synth.setPitchBend (performance.pitchWheel);
    if (! midiMod && ! juce::exactlyEqual (performance.modWheel, lastScreenMod))
        synth.setModWheel (performance.modWheel);
    lastScreenPitch = performance.pitchWheel;
    lastScreenMod = performance.modWheel;

    float* left = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : scratch.data();
    // Render up to each event, apply it there, go on: notes and controllers land on their
    // own samples (MidiBuffer iterates in time order).
    int done = 0;
    for (const auto m : midi)
    {
        const int at = std::clamp (m.samplePosition, 0, total);
        if (at > done)
        {
            synth.render (left + done, right + done, at - done);
            done = at;
        }
        synth.handleMidi (m.data, m.numBytes);
    }
    if (done < total)
        synth.render (left + done, right + done, total - done);
    if (buffer.getNumChannels() == 1)
        for (int i = 0; i < total; ++i)
            left[i] = 0.5f * (left[i] + scratch[static_cast<std::size_t> (i)]);
}

void AnalogEngine::setModulationCurve (int index, const osp::mod::Curve& curve) noexcept
{
    if (index == 0)
        synth.setLfoCurve (curve);   // LFO 1's CUSTOM shape (the synth's one LFO)
}

juce::String AnalogEngine::reimaginedModeUnavailable (int, int mode) const
{
    // MOSAIC rebuilds a recording from its harmonic analysis, made when it is loaded; the
    // oscillators' live sound has none, so the mode leaves it as it is.
    return mode == static_cast<int> (osp::ReimaginedMode::mosaic) ? juce::String ("NEEDS A RECORDING: PLAYS UNCHANGED") : juce::String();
}

bool AnalogEngine::sourcePicture (int, const juce::AudioProcessorValueTreeState& parameters, std::vector<float>& cycle) const
{
    params::Reader r;
    r.attach (const_cast<juce::AudioProcessorValueTreeState&> (parameters));   // reads only
    cycle = pictureOf (r.read());
    return true;
}

std::vector<float> AnalogEngine::pictureOf (const Patch& p, int points)
{
    // One period of OSC 1 (or, synced, of OSC 2, the master): the two oscillators as the
    // mixer blends them, without the filter. LOW FREQ OSC 2 is a modulator: not drawn.
    const auto w1 = osc::wavesOf (p.osc1Wave);
    const auto w2 = osc::wavesOf (p.osc2Wave);
    const double pitch1 = 12.0 * p.osc1Octave + p.osc1Coarse;
    const double pitch2 = 12.0 * p.osc2Octave + p.osc2Coarse + p.osc2FineCents / 100.0;
    const double ratio21 = std::exp2 ((pitch2 - pitch1) / 12.0);
    const double theta = p.blend * 1.5707963267948966;
    const auto g1 = static_cast<float> (p.osc1Level * std::min (1.0, 1.41421356 * std::cos (theta)));
    const auto g2 = static_cast<float> (p.osc2LowFrequency ? 0.0 : p.osc2Level * std::min (1.0, 1.41421356 * std::sin (theta)));
    std::vector<float> cycle (static_cast<std::size_t> (std::max (2, points)));
    for (std::size_t i = 0; i < cycle.size(); ++i)
    {
        const double t = static_cast<double> (i) / static_cast<double> (cycle.size());
        double phase1, phase2;
        if (p.sync && ! p.osc2LowFrequency)
        {
            phase2 = t;
            const double slave = t / ratio21;   // OSC 1's cycles within one of OSC 2's
            phase1 = slave - std::floor (slave);
        }
        else
        {
            phase1 = t;
            const double b = t * ratio21;
            phase2 = b - std::floor (b);
        }
        cycle[i] = g1 * osc::naive (w1, phase1, static_cast<float> (p.osc1Width)) + g2 * osc::naive (w2, phase2, static_cast<float> (p.osc2Width));
    }
    return cycle;
}

} // namespace andor::analog
