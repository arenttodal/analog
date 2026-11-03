#include "AnalogEngine.h"

#include "InstrumentHost.h"
#include "SampleImport.h"

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

void AnalogEngine::attachParameters (juce::AudioProcessorValueTreeState& parameters)
{
    state = &parameters;
    hybridTypeValue = parameters.getRawParameterValue (params::hybridType);
}

HybridType AnalogEngine::hybridType() const noexcept
{
    return static_cast<HybridType> (std::clamp (juce::roundToInt (hybridTypeValue != nullptr ? hybridTypeValue->load() : 0.0f), 0, 3));
}

void AnalogEngine::setHybridType (HybridType type)
{
    if (state == nullptr)
        return;
    if (auto* p = state->getParameter (params::hybridType))
        p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (type)));
}

void AnalogEngine::prepare (double sampleRate, int maximumBlockSize, juce::AudioProcessorValueTreeState& parameters)
{
    if (state == nullptr)
        attachParameters (parameters);
    reader.attach (parameters);
    synth.prepare (sampleRate, std::max (1, maximumBlockSize), oversamplingFactor);
    synth.setControlModulator (this);
    reader.snapshot (raw);
    auto patch = reader.read (raw);
    patch.effects.shaper.customSteps = shaperPattern;
    synth.setPatch (patch);
    synth.setKeyDnaAmount (reader.keyDnaCardAmount());
    synth.reset();
    scratch.assign (static_cast<std::size_t> (std::max (1, maximumBlockSize)), 0.0f);
    prepared = true;
}

AnalogEngine::AnalogEngine (int oversampling) : oversamplingFactor (oversampling) {}

AnalogEngine::~AnalogEngine()
{
    sourceLoader.removeAllJobs (true, 30000);
}

//==============================================================================
// The imported sample source.

void AnalogEngine::saveState (juce::XmlElement& state) const
{
    const std::lock_guard<std::mutex> lock (sourceMutex);
    if (sourceDefinition != nullptr)
        state.addChildElement (new juce::XmlElement (*sourceDefinition));
}

void AnalogEngine::restoreState (const juce::XmlElement& state, const juce::File& assetFolder)
{
    const auto* definition = state.getChildByName ("SampleSource");
    setSampleSourceDefinition (definition != nullptr ? std::make_unique<juce::XmlElement> (*definition) : nullptr, assetFolder.getChildFile ("Imported"));
    // State version 8: before it, an imported source played whenever it was there; now card C
    // plays it as its INSTRUMENT PATCH (a session or preset from before keeps its sound).
    if (definition != nullptr && state.getIntAttribute ("productVersion", 1) < 8)
        setHybridType (HybridType::instrumentPatch);
}

void AnalogEngine::setSampleSourceDefinition (std::unique_ptr<juce::XmlElement> definition, const juce::File& assetFolder)
{
    std::uint64_t generation = 0;
    {
        const std::lock_guard<std::mutex> lock (sourceMutex);
        const bool same = (definition == nullptr && sourceDefinition == nullptr)
                          || (definition != nullptr && sourceDefinition != nullptr && definition->isEquivalentTo (sourceDefinition.get(), false));
        if (same && ! (sourceState.loaded && (! sourceState.missing.isEmpty() || ! sourceState.unreadable.isEmpty())))
            return;   // (the same one, already loading or loaded complete: nothing to do)
        sourceDefinition = definition != nullptr ? std::make_unique<juce::XmlElement> (*definition) : nullptr;
        generation = ++sourceGeneration;
        sourceState = {};
        sourceLoaded.reset();
        if (sourceDefinition != nullptr)
        {
            sourceState.name = sourceDefinition->getStringAttribute ("name");
            sourceState.loading = true;
        }
    }
    if (definition == nullptr)
    {
        // None: an empty source (the synth plays ORGANIC's recording, or nothing).
        auto none = std::make_shared<LoadedSource>();
        none->generation = generation;
        const std::lock_guard<std::mutex> lock (sourceMutex);
        sourceExchange.publish (std::move (none));
        sourceExchange.collectGarbage();
        return;
    }
    sourceLoader.addJob ([this, d = std::make_shared<juce::XmlElement> (*definition), assetFolder, generation] {
        loadSampleSource (std::make_unique<juce::XmlElement> (*d), assetFolder, generation);
    });
}

void AnalogEngine::loadSampleSource (std::unique_ptr<juce::XmlElement> definition, juce::File assetFolder, std::uint64_t generation)
{
    auto result = import::loadDefinition (*definition, assetFolder);
    auto loaded = std::make_shared<LoadedSource>();
    loaded->generation = generation;
    loaded->source = result.source;
    const std::lock_guard<std::mutex> lock (sourceMutex);
    if (generation != sourceGeneration)
        return;   // (another definition was asked for meanwhile)
    sourceState.loading = false;
    sourceState.loaded = true;
    sourceState.missing = result.missing;
    sourceState.unreadable = result.unreadable;
    sourceState.zones = result.zones;
    sourceLoaded = result.source;
    sourceExchange.publish (std::move (loaded));
    sourceExchange.collectGarbage();
}

std::unique_ptr<juce::XmlElement> AnalogEngine::sampleSourceDefinition() const
{
    const std::lock_guard<std::mutex> lock (sourceMutex);
    return sourceDefinition != nullptr ? std::make_unique<juce::XmlElement> (*sourceDefinition) : nullptr;
}

AnalogEngine::SampleSourceState AnalogEngine::sampleSourceState() const
{
    const std::lock_guard<std::mutex> lock (sourceMutex);
    return sourceState;
}

bool AnalogEngine::waitForSampleSource (int timeoutMs) const
{
    const auto until = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);
    while (juce::Time::getMillisecondCounter() < until)
    {
        {
            const std::lock_guard<std::mutex> lock (sourceMutex);
            if (! sourceState.loading)
                return true;
        }
        juce::Thread::sleep (5);
    }
    return false;
}

void AnalogEngine::sourcePublished (int layer, std::shared_ptr<const osp::plugin::LoadedInstrument> source)
{
    if (layer != 1)
        return;
    recordingPublished.store (source != nullptr);
    if (source != nullptr)
        setHybridType (HybridType::sample);   // (a recording dropped on card C plays: an imported source stays, dormant)
}

juce::String AnalogEngine::status (const juce::String& topic) const
{
    if (topic == "sample")
    {
        // The imported source in words (empty: none, or playing complete).
        const auto st = sampleSourceState();
        if (st.loading)
            return "Loading " + st.name + juce::String::fromUTF8 ("\xe2\x80\xa6");
        if (! st.missing.isEmpty() || ! st.unreadable.isEmpty())
            return st.name + ": " + juce::String (st.missing.size() + st.unreadable.size()) + " of " + juce::String (st.zones)
                   + " recordings missing - relink to play them";
        return {};
    }
    if (topic != "morph")
        return {};
    // MORPH hears card C live: it acts whenever C sounds (a recording, OSC 3, NOISE or an
    // instrument patch's zones); only a silent card C leaves it nothing to fuse.
    switch (hybridType())
    {
        case HybridType::oscillator:
        case HybridType::noise: return {};
        case HybridType::instrumentPatch: return sampleSourceState().loaded ? juce::String() : juce::String ("Card C has no instrument patch loaded: nothing to morph with");
        case HybridType::sample: break;
    }
    return recordingPublished.load() ? juce::String() : juce::String ("Card C is silent: load a recording (or choose OSC 3, NOISE or an instrument patch) to morph");
}

void AnalogEngine::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Performance& performance) noexcept
{
    // The imported sample source: a newly loaded one replaces the old (the synth stops reading
    // the old before this returns; it is freed on the message side once newer ones are in use).
    if (const auto* next = sourceExchange.takePending())
        sourcePlaying = next;
    // Card C plays only its selected type: the recording as a SAMPLE, the imported zones as an
    // INSTRUMENT PATCH (OSC 3 and NOISE are the voices' own). The others stay loaded, silent.
    {
        const auto type = static_cast<HybridType> (std::clamp (juce::roundToInt (hybridTypeValue != nullptr ? hybridTypeValue->load() : 0.0f), 0, 3));
        const auto* recording = type == HybridType::sample ? shellRecording : nullptr;
        const SampleSource* zones = type == HybridType::instrumentPatch && sourcePlaying != nullptr ? sourcePlaying->source.get() : nullptr;
        if (recording != recordingApplied)
        {
            synth.setOrganic (recording);
            recordingApplied = recording;
        }
        if (zones != zonesApplied)
        {
            synth.setSampleSource (zones);
            zonesApplied = zones;
        }
    }
    if (sourcePlaying != nullptr)
        sourceExchange.publishOldestInUse (sourcePlaying->generation);
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
        // ... and the header's MIX (ANDOR OSP's MIX destination): the A/B mix between the
        // oscillators and ORGANIC (the layer gains ramp towards it: a moving source is click-free).
        for (const auto& [dest, id] : { std::pair<D, const char*> { D::drive, "drive" }, { D::movement, "motion" }, { D::space, "space" }, { D::echo, "echo" },
                                        { D::mixPosition, "ab.blend" } })
            if (synth.sharedOffset (dest, offset))
                reader.moveById (raw, id, offset);
    }
    auto patch = reader.read (raw);
    patch.effects.shaper.customSteps = shaperPattern;   // MOVEMENT SHAPER's CUSTOM (the shell's)
    synth.setPatch (patch);
    synth.setKeyDnaAmount (reader.keyDnaCardAmount());  // (the profile is the shell's: setKeyDnaTable)
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
    // Each oscillator card's own grains (A: OSC 1, B: OSC 2; state version 8).
    for (int card = 0; card < 2; ++card)
    {
        auto& snapshot = card == 0 ? grains : grains2;
        const int n = synth.collectGrains (card, dots.data(), Snapshot::capacity);
        for (int i = 0; i < n; ++i)
        {
            const auto k = static_cast<std::size_t> (i);
            snapshot.position[k].store (dots[k].position, std::memory_order_relaxed);
            snapshot.level[k].store (dots[k].level, std::memory_order_relaxed);
            snapshot.lane[k].store (dots[k].lane, std::memory_order_relaxed);
        }
        snapshot.count.store (n, std::memory_order_release);
    }

    // Card B (ORGANIC): one read head per voice playing the recording.
    std::array<float, Snapshot::playheadCapacity> where {}, level {};
    const int heads = synth.collectPlayheads (where.data(), level.data(), Snapshot::playheadCapacity);
    for (int i = 0; i < heads; ++i)
    {
        const auto k = static_cast<std::size_t> (i);
        organicHeads.playheadPosition[k].store (where[k], std::memory_order_relaxed);
        organicHeads.playheadLevel[k].store (level[k], std::memory_order_relaxed);
    }
    organicHeads.playheads.store (heads, std::memory_order_release);
}

void AnalogEngine::setModulationCurve (int index, const osp::mod::Curve& curve) noexcept
{
    if (index == 0)
        synth.setLfoCurve (curve);   // LFO 1's CUSTOM shape (the synth's one LFO)
    else if (index >= osp::plugin::InstrumentHost::dampenCurveIndex (0) && index <= osp::plugin::InstrumentHost::dampenCurveIndex (2))
        synth.setDampenCurve (index - osp::plugin::InstrumentHost::dampenCurveIndex (0), curve);   // each card's DAMPEN over the keys (layer C: OSC 2)
}

juce::String AnalogEngine::reimaginedModeUnavailable (int, int mode) const
{
    // MOSAIC rebuilds a recording from its harmonic analysis, made when it is loaded; the
    // oscillators' live sound has none, so the mode leaves it as it is.
    return mode == static_cast<int> (osp::ReimaginedMode::mosaic) ? juce::String ("NEEDS A RECORDING: PLAYS UNCHANGED") : juce::String();
}

juce::String AnalogEngine::engineSourceName (int layer) const
{
    if (layer != 1)
        return {};
    switch (hybridType())
    {
        case HybridType::oscillator: return "OSC 3";
        case HybridType::noise: return "NOISE";
        case HybridType::sample: return recordingPublished.load() ? juce::String() : juce::String ("HYBRID");   // (empty: its stand-in)
        case HybridType::instrumentPatch: break;
    }
    const std::lock_guard<std::mutex> lock (sourceMutex);
    if (sourceDefinition == nullptr)
        return "INSTRUMENT PATCH";   // (none chosen yet: the card says so; its well offers the sounds)
    const auto collection = sourceDefinition->getStringAttribute ("collection");
    const auto name = sourceDefinition->getStringAttribute ("name");
    return collection.isNotEmpty() ? collection + juce::String::fromUTF8 (" \xc2\xb7 ") + name : name;
}

juce::String AnalogEngine::engineSourcePlaceholder (int layer) const
{
    if (layer != 1)
        return {};
    if (hybridType() == HybridType::sample && ! recordingPublished.load())
        return "ADD SAMPLE\nDROP OR BROWSE";
    if (hybridType() == HybridType::instrumentPatch)
    {
        const std::lock_guard<std::mutex> lock (sourceMutex);
        if (sourceDefinition == nullptr)
            return "CHOOSE A SOUND\nSOUNDS, OR IMPORT A KONTAKT PATCH";
    }
    return {};
}

void AnalogEngine::removeEngineSource (int layer)
{
    if (layer != 1)
        return;
    if (hybridType() == HybridType::instrumentPatch)
        setSampleSourceDefinition (nullptr, {});
    setHybridType (HybridType::sample);   // (card C back to its SAMPLE: a recording, or the empty card)
}

bool AnalogEngine::sourcePicture (int layer, const juce::AudioProcessorValueTreeState& parameters, std::vector<float>& cycle) const
{
    if (layer == 1 && engineSourcePlaceholder (1).isNotEmpty())
    {
        // A stand-in: a recording's outline in general (swells, a long tail), drawn faint.
        cycle.resize (256);
        for (std::size_t i = 0; i < cycle.size(); ++i)
        {
            const double t = static_cast<double> (i) / 255.0;
            const double envelope = std::sin (math::pi * std::pow (t, 0.6)) * (0.55 + 0.45 * std::sin (7.0 * t + 1.0));
            cycle[i] = static_cast<float> ((i % 2 == 0 ? 1.0 : -1.0) * envelope * (0.7 + 0.3 * std::sin (37.0 * t)));
        }
        return true;
    }
    if (layer == 1 && hybridType() != HybridType::instrumentPatch)
    {
        // OSC 3 alone (OSC 2's picture with OSC 3's settings), or NOISE's.
        params::Reader r;
        r.attach (const_cast<juce::AudioProcessorValueTreeState&> (parameters));
        auto p = r.read();
        if (p.hybridType == HybridType::noise)
        {
            cycle = noisePictureOf (p);
            return true;
        }
        p.osc2Model = p.osc3Model;
        p.osc2Wave = p.osc3Wave;
        p.dco2 = p.dco3;
        p.wide2 = p.wide3;
        p.osc2Width = p.osc3Width;
        p.osc2Octave = p.osc3Octave;
        p.osc2Coarse = p.osc3Coarse;
        cycle = oscillatorPictureOf (p, 2);
        return true;
    }
    if (layer == 1)
    {
        // The imported source at a glance: its longest recording's outline (its peaks, drawn as
        // a waveform), not a period (it is a recording, not an oscillator).
        std::shared_ptr<const SampleSource> source;
        {
            const std::lock_guard<std::mutex> lock (sourceMutex);
            source = sourceLoaded;
        }
        cycle.clear();
        if (source == nullptr)
            return true;
        const osp::PlaybackSource* longest = nullptr;
        for (const auto& z : source->zones)
            if (z.audio != nullptr && (longest == nullptr || z.audio->numFrames() > longest->numFrames()))
                longest = z.audio.get();
        if (longest == nullptr)
            return true;
        constexpr int points = 256;
        const auto frames = longest->numFrames();
        for (int i = 0; i < points; ++i)
        {
            float peak = 0.0f;
            const auto a = frames * i / points, b = std::max (a + 1, frames * (i + 1) / points);
            for (auto f = a; f < b; f += std::max<std::int64_t> (1, (b - a) / 64))
                for (int ch = 0; ch < longest->numChannels(); ++ch)
                    peak = std::max (peak, std::abs (longest->channelData (ch)[f]));
            cycle.push_back (i % 2 == 0 ? peak : -peak);
        }
        // (Shown full height: the outline's shape, not its loudness.)
        float top = 1.0e-6f;
        for (const float v : cycle)
            top = std::max (top, std::abs (v));
        for (auto& v : cycle)
            v /= top;
        return true;
    }
    params::Reader r;
    r.attach (const_cast<juce::AudioProcessorValueTreeState&> (parameters));   // reads only
    // Three engines: each oscillator's card pictures that oscillator alone.
    cycle = oscillatorPictureOf (r.read(), layer == 2 ? 2 : 1);
    return true;
}

std::vector<float> AnalogEngine::noisePictureOf (const Patch& p, int points)
{
    // A fixed seeded stretch of the noise, through its COLOR and TONE (what the card shows; the
    // voices play their own sequences).
    osp::Prng noise (0x484eu);
    const double rate = 48000.0;
    const double toneHz = 200.0 * std::pow (90.0, std::clamp (p.hybridNoiseTone, 0.0, 1.0));
    const float coef = p.hybridNoiseTone >= 0.999 ? 1.0f : static_cast<float> (1.0 - std::exp (-2.0 * math::pi * toneHz / rate));
    const float colourCoef = static_cast<float> (1.0 - std::exp (-2.0 * math::pi * 5000.0 / rate));
    float colour = 0.0f, tone = 0.0f, top = 1.0e-6f;
    std::vector<float> cycle (static_cast<std::size_t> (std::max (2, points)));
    for (auto& v : cycle)
    {
        float n = static_cast<float> (noise.bipolar());
        if (p.hybridNoiseColor == NoiseColor::juno)
        {
            colour += colourCoef * (n - colour);
            n = colour * 1.36f;
        }
        tone += coef * (n - tone);
        v = tone;
        top = std::max (top, std::abs (v));
    }
    for (auto& v : cycle)
        v /= top;
    return cycle;
}

std::vector<float> AnalogEngine::oscillatorPictureOf (Patch p, int slot, int points)
{
    p.blend = 0.5;
    p.ringMix = 0.0;
    if (slot == 1)
    {
        p.osc1Enabled = true;
        p.osc1Level = 1.0;
        p.osc2Enabled = false;
    }
    else
    {
        p.osc2Enabled = true;
        p.osc2Level = 1.0;
        p.osc2LowFrequency = false;
        p.osc1Enabled = false;
        p.sync = false;
        p.osc1Octave = p.osc2Octave;
        p.osc1Coarse = p.osc2Coarse;
        p.osc2FineCents = 0.0;
    }
    return pictureOf (p, points);
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
