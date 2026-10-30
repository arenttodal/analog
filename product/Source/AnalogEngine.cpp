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

//==============================================================================
// MORPH's analysis thread.

class AnalogEngine::Analyser final : public juce::Thread
{
public:
    explicit Analyser (AnalogEngine& e) : juce::Thread ("ANDOR Analog morph analysis"), engine (e) { startThread (juce::Thread::Priority::low); }
    ~Analyser() override
    {
        cancelling = true;
        signalThreadShouldExit();
        notify();
        stopThread (10000);
    }

    void request (std::shared_ptr<const osp::plugin::LoadedInstrument> source)
    {
        {
            const std::lock_guard<std::mutex> lock (mutex);
            wanted = std::move (source);
            hasRequest = true;
            state = wanted != nullptr ? "Analysing ORGANIC's recording for MORPH" : "Load a recording onto ORGANIC to morph";
        }
        cancelling = true;   // (a newer recording: the one being analysed is not needed)
        notify();
    }

    juce::String status() const
    {
        const std::lock_guard<std::mutex> lock (mutex);
        return state;
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            std::shared_ptr<const osp::plugin::LoadedInstrument> source;
            {
                const std::lock_guard<std::mutex> lock (mutex);
                if (hasRequest)
                {
                    source = wanted;
                    hasRequest = false;
                    cancelling = false;
                }
            }
            if (source != nullptr && source->model != nullptr && source->model->isValid())
                analyse (source);
            else if (source == nullptr)
                collect();
            wait (250);
            collect();
        }
    }

private:
    void analyse (const std::shared_ptr<const osp::plugin::LoadedInstrument>& source)
    {
        auto box = std::make_shared<ProfileBox>();
        box->model = source->model.get();
        box->source = source;
        const auto key = source->contentHash;
        std::shared_ptr<const ProfileBox> cached;
        {
            const std::lock_guard<std::mutex> lock (mutex);
            for (const auto& [k, p] : cache)
                if (k == key && ! key.empty())
                    cached = p;
        }
        if (cached != nullptr)
            box->profile = cached->profile;
        else
        {
            // The recording as ANDOR OSP prepared it, summed to mono (as the voices play it).
            const auto& src = *source->model->original.source;
            std::vector<float> mono (static_cast<std::size_t> (src.numFrames()), 0.0f);
            for (int ch = 0; ch < src.numChannels(); ++ch)
            {
                const float* d = src.channelData (ch);   // (the first frame: the padding is before it)
                for (std::size_t i = 0; i < mono.size(); ++i)
                    mono[i] += d[i] / static_cast<float> (src.numChannels());
            }
            box->profile = morph::analyse (mono.data(), static_cast<std::int64_t> (mono.size()), src.sampleRate(), {}, &cancelling);
            if (cancelling.load())
                return;
        }
        std::shared_ptr<const ProfileBox> published = box;
        {
            const std::lock_guard<std::mutex> lock (mutex);
            if (! key.empty() && cached == nullptr)
            {
                cache.emplace_back (key, published);
                if (cache.size() > 4)
                    cache.erase (cache.begin());
            }
            boxes.push_back (published);
            state = box->profile.kind == morph::MorphProfile::Kind::pitched
                        ? juce::String()
                        : "MORPH needs a pitched recording: " + juce::String (box->profile.status) + " (the sources play as they are)";
        }
        engine.pending.store (published.get(), std::memory_order_release);
    }

    /** Frees the profiles the audio thread no longer reads (never the newest, never the one in use). */
    void collect()
    {
        const std::lock_guard<std::mutex> lock (mutex);
        const auto* used = engine.inUse.load (std::memory_order_acquire);
        const auto* next = engine.pending.load (std::memory_order_acquire);
        for (std::size_t i = 0; i + 1 < boxes.size();)
        {
            if (boxes[i].get() != used && boxes[i].get() != next)
                boxes.erase (boxes.begin() + static_cast<long> (i));
            else
                ++i;
        }
    }

    AnalogEngine& engine;
    mutable std::mutex mutex;
    std::shared_ptr<const osp::plugin::LoadedInstrument> wanted;
    bool hasRequest = false;
    std::atomic<bool> cancelling { false };
    juce::String state = "Load a recording onto ORGANIC to morph";
    std::vector<std::shared_ptr<const ProfileBox>> boxes;
    std::vector<std::pair<std::string, std::shared_ptr<const ProfileBox>>> cache;
};

AnalogEngine::AnalogEngine (int oversampling) : oversamplingFactor (oversampling) {}

AnalogEngine::~AnalogEngine()
{
    analyser.reset();   // (before the synth that may read a profile goes)
}

void AnalogEngine::sourcePublished (int layer, std::shared_ptr<const osp::plugin::LoadedInstrument> source)
{
    if (layer != 1)
        return;
    if (analyser == nullptr)
        analyser = std::make_unique<Analyser> (*this);
    analyser->request (std::move (source));
}

juce::String AnalogEngine::status (const juce::String& topic) const
{
    if (topic != "morph")
        return {};
    return analyser != nullptr ? analyser->status() : juce::String ("Load a recording onto ORGANIC to morph");
}

void AnalogEngine::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Performance& performance) noexcept
{
    // MORPH: a profile finished on the analysis thread (it describes the model it names: the
    // synth uses it only while that model plays).
    if (const auto* box = pending.exchange (nullptr, std::memory_order_acq_rel))
    {
        synth.setMorphProfile (&box->profile, box->model);
        inUse.store (box, std::memory_order_release);
    }
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
    // mixer blends them, each in its model, without the filter. LOW FREQ OSC 2 is a
    // modulator: not drawn. A DCO's sub needs two periods to show its octave: then two are
    // drawn.
    auto plan = [&p] (int slot) {
        osc::SlotPlan s;
        s.model = slot == 1 ? p.osc1Model : p.osc2Model;
        if (s.model == OscModel::classic)
            s.waves = slot == 1 ? osc::wavesOf (p.osc1Wave) : osc::wavesOf (p.osc2Wave);
        else if (s.model == OscModel::dco)
        {
            const auto& c = slot == 1 ? p.dco1 : p.dco2;
            s.waves = osc::wavesOf (c);
            s.subLevel = static_cast<float> (c.subLevel);
        }
        else
        {
            const auto& w = slot == 1 ? p.wide1 : p.wide2;
            s.waves = osc::wavesOf (w.wave);
            s.squareWidth = w.wave == WideWave::square;
            s.superSaw = w.wave == WideWave::superSaw;
        }
        return s;
    };
    const auto plan1 = plan (1), plan2 = plan (2);
    const double spread1 = p.wide1.spread, spread2 = p.wide2.spread;
    // A slot's naive value at `phase` (cycle `n` of its own, for the sub's octave).
    auto value = [] (const osc::SlotPlan& s, double phase, int n, float width, double spread) {
        if (s.squareWidth)
            width = 0.5f;
        float v = osc::naive (s.waves, phase, width);
        if (s.subLevel > 0.0f)
            v += (n % 2 == 0 ? 1.0f : -1.0f) * s.subLevel;
        if (s.superSaw && spread > 0.0)
        {
            // The side saws drawn where their detune has carried them a little way (a still
            // picture of a moving sound), at the level the engine gives them.
            const float side = osc::superSawSideLevel (spread);
            float sum = v;
            for (int i = 0; i < osc::superSawSides; ++i)
            {
                double q = phase + 0.11 * osc::superSawOffsets[static_cast<std::size_t> (i)] * (0.3 + spread);
                q -= std::floor (q);
                sum += side * static_cast<float> (2.0 * q - 1.0);
            }
            v = sum * osc::superSawNormal (side);
        }
        return v;
    };
    const double pitch1 = 12.0 * p.osc1Octave + p.osc1Coarse;
    const double pitch2 = 12.0 * p.osc2Octave + p.osc2Coarse + p.osc2FineCents / 100.0;
    const double ratio21 = std::exp2 ((pitch2 - pitch1) / 12.0);
    const double theta = p.blend * 1.5707963267948966;
    const auto g1 = static_cast<float> (p.osc1Enabled ? p.osc1Level * std::min (1.0, 1.41421356 * std::cos (theta)) : 0.0);
    const auto g2 = static_cast<float> (p.osc2LowFrequency || ! p.osc2Enabled ? 0.0 : p.osc2Level * std::min (1.0, 1.41421356 * std::sin (theta)));
    const auto ring = static_cast<float> (std::clamp (p.ringMix, 0.0, 1.0));
    const double periods = plan1.subLevel > 0.0f || plan2.subLevel > 0.0f ? 2.0 : 1.0;
    std::vector<float> cycle (static_cast<std::size_t> (std::max (2, points)));
    for (std::size_t i = 0; i < cycle.size(); ++i)
    {
        const double t = periods * static_cast<double> (i) / static_cast<double> (cycle.size());
        double cycles1, cycles2;
        if (p.sync && ! p.osc2LowFrequency)
        {
            cycles2 = t;
            const double inMaster = t - std::floor (t);
            cycles1 = std::floor (t) * 1.0e3 + inMaster / ratio21;   // (restarted every master cycle)
        }
        else
        {
            cycles1 = t;
            cycles2 = t * ratio21;
        }
        const double phase1 = cycles1 - std::floor (cycles1), phase2 = cycles2 - std::floor (cycles2);
        const float o1 = value (plan1, phase1, static_cast<int> (std::floor (cycles1)), static_cast<float> (p.osc1Width), spread1);
        const float o2 = value (plan2, phase2, static_cast<int> (std::floor (cycles2)), static_cast<float> (p.osc2Width), spread2);
        float mix = g1 * o1 + g2 * o2;
        if (ring > 0.0f)
            mix += ring * (1.7f * o1 * o2 * 0.5f * (g1 + g2) - mix);
        cycle[i] = mix;
    }
    return cycle;
}

} // namespace andor::analog
