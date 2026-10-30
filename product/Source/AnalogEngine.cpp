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
    synth.setControlModulator (this);
    reader.snapshot (raw);
    auto patch = reader.read (raw);
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
    // The knobs as they are, the instrument's own controls moved by their routes (global
    // sources: one value for the whole instrument), and ANDOR OSP's macros likewise.
    reader.snapshot (raw);
    if (modulated)
    {
        double offset = 0.0;
        for (int k = 0; k < osp::mod::productDestCount(); ++k)
            if (const auto d = osp::mod::productDest (k); osp::mod::destInfo (d).owner == osp::mod::Owner::global && synth.sharedOffset (d, offset))
                reader.move (raw, k, offset);
        using D = osp::mod::Dest;
        for (const auto& [dest, id] : { std::pair<D, const char*> { D::drive, "drive" }, { D::movement, "motion" }, { D::space, "space" }, { D::echo, "echo" } })
            if (synth.sharedOffset (dest, offset))
                reader.moveById (raw, id, offset);
    }
    auto patch = reader.read (raw);
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
    publishGrains();
    publishModulationView();
}

void AnalogEngine::setModulation (const osp::mod::Settings& settings, const std::array<int, osp::mod::ccSlots>& controllers) noexcept
{
    synth.setModulation (settings, controllers);
    modulated = synth.modulation().compiled.any;
}

void AnalogEngine::setPlayedKeys (bool arpeggiating, const juce::MidiBuffer& played) noexcept
{
    // VEL and NOTE on the shared stages follow the musician's keys, not the arpeggiator's.
    synth.setPlayedKeysOnly (arpeggiating);
    if (arpeggiating)
        for (const auto m : played)
            if (m.numBytes >= 3 && (m.data[0] & 0xf0) == 0x90 && m.data[2] > 0)
                synth.setPlayedKey (m.data[1], m.data[2]);
}

void AnalogEngine::publishModulationView() noexcept
{
    // As ANDOR OSP's engine shows its sources: the LFOs (POLY: the newest note's), the newest
    // note's envelopes, VEL and NOTE, and the shared controllers.
    const auto& rt = synth.modulation();
    const auto* newest = synth.newestModulation();
    for (std::size_t i = 0; i < 2; ++i)
    {
        const bool poly = rt.settings.lfo[i].scope == osp::mod::Scope::poly;
        const auto& lfo = poly && newest != nullptr ? newest->lfo[i] : rt.globalLfo[i];
        viewPhase[i].store (static_cast<float> (lfo.phase), std::memory_order_relaxed);
        viewValue[i].store (static_cast<float> (poly && newest != nullptr ? newest->values[i] : lfo.value), std::memory_order_relaxed);
        const auto* env = newest != nullptr ? &newest->env[i] : nullptr;
        viewValue[2 + i].store (env != nullptr ? static_cast<float> (env->level) : 0.0f, std::memory_order_relaxed);
        const double time = env == nullptr ? 0.0 : (rt.settings.env[i].oneShotCurve ? env->elapsed : env->t);
        viewEnvTime[i].store (static_cast<float> (time), std::memory_order_relaxed);
        viewEnvStage[i].store (env != nullptr ? static_cast<int> (env->stage) : 0, std::memory_order_relaxed);
        viewGlobalEnv[i].store (static_cast<float> (rt.globalEnv[i].level), std::memory_order_relaxed);
    }
    for (std::size_t i = osp::mod::slot::modWheel; i < viewValue.size(); ++i)
        viewValue[i].store (rt.sharedValue[i], std::memory_order_relaxed);
    if (newest != nullptr)
        for (const auto i : { osp::mod::slot::vel, osp::mod::slot::note })
            viewValue[static_cast<std::size_t> (i)].store (newest->values[static_cast<std::size_t> (i)], std::memory_order_relaxed);
    viewVoice.store (newest != nullptr, std::memory_order_release);
}

osp::InstrumentEngine::ModView AnalogEngine::modulationView() const noexcept
{
    osp::InstrumentEngine::ModView view;
    view.voice = viewVoice.load (std::memory_order_acquire);
    for (std::size_t i = 0; i < 2; ++i)
    {
        view.lfoPhase[i] = viewPhase[i].load (std::memory_order_relaxed);
        view.envTime[i] = viewEnvTime[i].load (std::memory_order_relaxed);
        view.envStage[i] = viewEnvStage[i].load (std::memory_order_relaxed);
        view.globalEnv[i] = viewGlobalEnv[i].load (std::memory_order_relaxed);
    }
    for (std::size_t i = 0; i < view.value.size(); ++i)
        view.value[i] = viewValue[i].load (std::memory_order_relaxed);
    return view;
}

void AnalogEngine::publishGrains() noexcept
{
    using Snapshot = osp::InstrumentEngine::GrainSnapshot;
    std::array<LiveGranulator::GrainDot, Snapshot::capacity> dots;
    const int n = synth.collectGrains (dots.data(), Snapshot::capacity);
    for (int i = 0; i < n; ++i)
    {
        const auto k = static_cast<std::size_t> (i);
        grains.position[k].store (dots[k].position, std::memory_order_relaxed);
        grains.level[k].store (dots[k].level, std::memory_order_relaxed);
        grains.lane[k].store (dots[k].lane, std::memory_order_relaxed);
    }
    grains.count.store (n, std::memory_order_release);
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
