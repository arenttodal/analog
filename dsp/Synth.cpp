#include "Synth.h"

#include "AnalogMath.h"
#include "Oscillator.h"

#include "engine/FastMath.h"
#include "engine/InstrumentEngine.h"
#include "engine/KaleidoscopeEngine.h"

#include <algorithm>
#include <cmath>

namespace andor::analog
{

namespace
{
    bool sameLfo (const osp::mod::LfoSettings& a, const osp::mod::LfoSettings& b) noexcept
    {
        return a.shape == b.shape && ! (a.rateHz < b.rateHz || a.rateHz > b.rateHz) && a.sync == b.sync && a.division == b.division && ! (a.phase < b.phase || a.phase > b.phase)
               && a.bipolar == b.bipolar && a.mode == b.mode && a.scope == b.scope;
    }
}

namespace
{
    constexpr double stealFadeSeconds = 0.003;
    /** The mixer's level into the filter: one oscillator at full sits at about -7 dBFS
        into the saturator, so DRIVE 0 is clean but not sterile. */
    constexpr float filterInputGain = 0.45f;
    /** A voice's level after the filter (headroom for a dense chord: see the INIT chord test). */
    constexpr float voiceGain = 0.32f;
    /** OSC 2's LOW FREQ range: seven octaves down (C4 becomes about 2 Hz). */
    constexpr double lowFrequencyOctaves = 7.0;
    /** Full Poly Mod at its destinations. */
    constexpr float polyModPitchSemitones = 48.0f;
    constexpr float polyModWidth = 0.45f;
    constexpr float polyModCutoffSemitones = 60.0f;

    /** Where each slot sits in the stereo field at full spread (poly mode): alternating
        sides, outer slots further out. */
    constexpr std::array<float, Synth::maxVoices> slotPan { -0.35f, 0.35f, -0.7f, 0.7f, -0.15f, 0.15f, -0.55f, 0.55f,
                                                           -0.9f, 0.9f, -0.25f, 0.25f, -0.8f, 0.8f, -0.45f, 0.45f };

    void equalPower (float pan, float& left, float& right) noexcept
    {
        const float angle = static_cast<float> (math::pi) * 0.25f * (std::clamp (pan, -1.0f, 1.0f) + 1.0f);
        left = 1.41421356f * std::cos (angle);
        right = 1.41421356f * std::sin (angle);
    }

    /** KALEIDOSCOPE's grains under the oscillators (ONE SHOT): close behind the sound, short,
        scattered, so the remapped octaves and fifths shimmer around the note. */
    osp::GranularParams cloudGrains() noexcept
    {
        osp::GranularParams g;
        g.position = 0.15;
        g.sizeSeconds = 0.12;
        g.density = 16.0;
        g.spread = 0.35;
        return g;
    }

    float softGuard (float x) noexcept
    {
        // Linear to 0.8 (-1.9 dBFS), then a tanh knee that never passes 1.0.
        constexpr float knee = 0.8f, room = 0.2f;
        const float a = std::abs (x);
        if (a <= knee)
            return x;
        return std::copysign (knee + room * math::tanhFast ((a - knee) / room), x);
    }
}

//==============================================================================
void Synth::prepare (double sampleRate, int maximumBlockSize, int oversamplingFactor)
{
    hostRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    oversampling = oversamplingFactor >= 4 ? 4 : (oversamplingFactor >= 2 ? 2 : 1);
    internalRate = hostRate * oversampling;
    maxBlock = std::max (1, maximumBlockSize);

    const auto internalBlock = static_cast<std::size_t> (maxBlock * oversampling);
    mixL.assign (internalBlock, 0.0f);
    mixR.assign (internalBlock, 0.0f);
    halfL.assign (static_cast<std::size_t> (maxBlock * 2), 0.0f);
    halfR.assign (static_cast<std::size_t> (maxBlock * 2), 0.0f);

    // 2x -> 1x: flat to 0.42 fs, -96 dB from 0.58 fs (what folds back lands above 0.42 fs).
    stage2L.design (0.08, 96.0);
    stage2R.design (0.08, 96.0);
    // 4x -> 2x: only what would fold below 0.42 fs needs rejecting.
    stage1L.design (0.29, 96.0);
    stage1R.design (0.29, 96.0);
    double delay = 0.0;
    if (oversampling >= 2)
        delay += stage2L.latency() / 2.0;
    if (oversampling == 4)
        delay += stage1L.latency() / 4.0;
    latency = static_cast<int> (std::lround (delay));

    // tan (pi f / fs) by note, up to 0.45 fs or 40 kHz, whichever is lower.
    const double topHz = std::min (0.45 * internalRate, 40000.0);
    tableHighNote = static_cast<float> (math::hzToNote (topHz));
    const int entries = static_cast<int> ((tableHighNote - tableLowNote) * tableStepsPerNote) + 2;
    gTable.resize (static_cast<std::size_t> (entries));
    for (int i = 0; i < entries; ++i)
    {
        const double note = std::min (static_cast<double> (tableLowNote) + static_cast<double> (i) / tableStepsPerNote, static_cast<double> (tableHighNote));
        gTable[static_cast<std::size_t> (i)] = static_cast<float> (std::tan (math::pi * math::noteToHz (note) / internalRate));
    }

    const int recordLength = static_cast<int> (std::ceil (recordingSeconds * internalRate));
    for (int s = 0; s < maxVoices; ++s)
    {
        auto& v = voices[static_cast<std::size_t> (s)];
        v.slot = s;
        v.amp.setSampleRate (internalRate);
        v.env.setSampleRate (internalRate);
        v.recording.prepare (recordLength);
        v.granulator.prepare (internalRate);
        v.tapeFrame.prepare (internalRate);
        v.toybox.prepare (internalRate);
        v.mirage.prepare (internalRate);
        v.highPass.prepare (internalRate);
        v.organic.prepare (internalRate);
    }
    // The mode engines' "recording" on a live voice: only its length, rate and root are read.
    auto standIn = [] (double rate, double seconds) {
        osp::AudioData blank;
        blank.sampleRate = rate;
        blank.channels.assign (1, std::vector<float> (static_cast<std::size_t> (std::ceil (rate * seconds)), 0.0f));
        return osp::PlaybackSource (blank, 60.0, 4);
    };
    tapeSource = standIn (1000.0, 12.0);
    toyboxSource = standIn (1000.0, recordingSeconds);
    mirageSource = standIn (internalRate, 0.001);
    liveModel = osp::InstrumentModel();
    liveModel.rootMidi = 60.0;
    for (auto& m : resonanceModels)
    {
        m.rootMidi = 60.0;
        m.resonanceHz.assign (static_cast<std::size_t> (osp::ReimaginedStage::resonators), 261.6);
    }
    kaleidoscopeStage.prepare (hostRate, 0x6b616c65ull);
    lfoCurve = osp::mod::compileCurve (osp::mod::defaultLfoCurve());

    effects.prepare (hostRate, maxBlock, 1);
    chorus.prepare (hostRate, maxBlock);
    dcCoef = static_cast<float> (1.0 - 2.0 * math::pi * 5.0 / hostRate);
    reset();
}

void Synth::reset() noexcept
{
    noteCounter = 0;
    clock = 0;
    nextSlot = 0;
    keyCount = 0;
    monoGate = false;
    lastMonoPitch = -1.0;
    bend = 0.0f;
    wheel = 0.0f;
    pedal = false;
    blockOffset = 0;
    controlCountdown = 0;
    smoothingPrimed = false;
    driftSeed = current.seed;
    rebuildDrift();
    // The modulation starts over too (its LFOs, envelopes, controllers): the same seed and
    // events give the same sound.
    {
        const auto settings = modRuntime.settings;
        modRuntime = osp::mod::Runtime {};
        modRuntime.setSettings (settings);
        for (std::size_t i = 0; i < 2; ++i)
            modRuntime.globalLfo[i].start (settings.lfo[i], settings.lfo[i].phase);
    }
    lfoValue = 0.0f;
    sharedDriftCents = 0.0;
    stageCountdown = 0;
    for (auto& v : voices)
    {
        v.active = v.held = v.sustained = v.pending = v.hasPlayed = false;
        v.note = -1;
        v.fade = 0;
        v.amp.kill();
        v.env.kill();
        v.filter.reset();
        v.line1.reset();
        v.line2.reset();
        v.osc2Out = 0.0f;
        v.outGain = 0.0f;
        v.recording.restart();
        v.granulator.stop();
        v.modeEngine = nullptr;
        v.modeFactor = 1.0;
        v.grainMix = current.granular ? 1.0f : 0.0f;
        v.driftNow = v.driftFrom = v.driftTo = {};
        v.mod = {};
        v.modulated = false;
        v.panMod = 0.0f;
        v.ampAttackSeconds = v.ampDecaySeconds = v.ampReleaseSeconds = -1.0;
        v.driveGain = 1.0f;
        // Free-running oscillators start where the slot's calibration puts them.
        osp::Prng start (osp::Prng::deriveSeed (current.seed, static_cast<std::uint64_t> (v.slot), 0x5048u));
        v.phase1 = start.nextDouble();
        v.phase2 = start.nextDouble();
        v.noise.reseed (osp::Prng::deriveSeed (current.seed, static_cast<std::uint64_t> (v.slot), 0x4e4fu));
    }
    for (auto* d : { &stage1L, &stage1R, &stage2L, &stage2R })
        d->reset();
    osp::Macros macros;
    macros.life = macros.reimagined = 0.0;
    macros.motion = current.motion;
    macros.drive = current.drive;
    macros.space = current.space;
    macros.echo = current.echo;
    effects.setShaping (current.effects);
    effects.setMacros (macros);
    effects.reset();
    chorus.reset();
    kaleidoscopeStage.reset();
    dcL = dcR = 0.0f;
    dcStateL = dcStateR = 0.0f;
    sounding.store (0, std::memory_order_relaxed);
}

void Synth::setPatch (const Patch& patch) noexcept
{
    const bool wasMono = monophonic();
    const int oldGroup = groupSize();
    current = patch;
    current.polyphony = std::clamp (current.polyphony, 1, maxVoices);
    if (wasMono != monophonic() || (monophonic() && oldGroup != groupSize()))
    {
        // Another way of playing: what sounds is released, the held keys forgotten.
        allNotesOff();
        keyCount = 0;
        monoGate = false;
    }
    applyPolyphonyLimit();
    // LFO 1 is the synth's LFO: the modulation runs the patch's.
    if (! sameLfo (current.lfo, modSettings.lfo[0]) || modSettings.seed != current.seed)
    {
        modSettings.lfo[0] = current.lfo;
        modSettings.seed = current.seed;
        rebuildModulation();
    }
    ++patchGeneration;   // the voices' patches follow
    if (current.seed != driftSeed)
    {
        driftSeed = current.seed;
        rebuildDrift();
    }
    osp::Macros macros;
    macros.life = macros.reimagined = 0.0;   // (KALEIDOSCOPE's stage is the synth's own)
    macros.motion = std::clamp (current.motion, 0.0, 1.0);
    macros.drive = std::clamp (current.drive, 0.0, 1.0);
    macros.space = std::clamp (current.space, 0.0, 1.0);
    macros.echo = std::clamp (current.echo, 0.0, 1.0);
    effects.setShaping (current.effects);
    effects.setMacros (macros);
    const auto& rs = current.reimaginedSettings;
    kaleidoscopeStage.setAmount (rs.mode == osp::ReimaginedMode::kaleidoscope ? current.reimagined : 0.0);
    kaleidoscopeStage.setShape (rs.kaleidoscope.focus, rs.kaleidoscope.spread);
}

void Synth::setLfoCurve (const osp::mod::Curve& curve) noexcept
{
    lfoCurve = osp::mod::compileCurve (curve);
    modSettings.lfoCurve[0] = lfoCurve;
    rebuildModulation();
}

void Synth::setModulation (const osp::mod::Settings& settings, const std::array<int, osp::mod::ccSlots>& controllers) noexcept
{
    modSettings = settings;
    modSettings.lfo[0] = current.lfo;   // (the same parameters: LFO 1 is the patch's)
    modSettings.seed = current.seed;
    modControllers = controllers;
    rebuildModulation();
}

void Synth::rebuildModulation() noexcept
{
    modRuntime.setSettings (modSettings);
    const auto& compiled = modRuntime.compiled;
    voiceRoutes = compiled.anyVoiceDest;
    voiceControlCount = 0;
    for (int k = 0; k < osp::mod::productDestCount(); ++k)
        if (const auto d = osp::mod::productDest (k); compiled.has (d) && osp::mod::destInfo (d).owner == osp::mod::Owner::voice)
            voiceControls[static_cast<std::size_t> (voiceControlCount++)] = k;
    ++patchGeneration;   // a route gone leaves nothing behind in the voices' patches
}

bool Synth::sharedOffset (osp::mod::Dest d, double& offset) const noexcept
{
    const auto& compiled = modRuntime.compiled;
    if (! compiled.has (d))
        return false;
    offset = osp::mod::destInfo (d).span * compiled.sum (d, modRuntime.sharedValue);
    return true;
}

const osp::mod::VoiceState* Synth::newestModulation() const noexcept
{
    const Voice* newest = nullptr;
    for (const auto& v : voices)
        if (v.active && (newest == nullptr || v.order > newest->order))
            newest = &v;
    return newest != nullptr ? &newest->mod : nullptr;
}

int Synth::collectGrains (LiveGranulator::GrainDot* out, int max) const noexcept
{
    if (! current.granular)
        return 0;
    int n = 0;
    for (const auto& v : voices)
        if (v.active && n < max)
            n += v.granulator.collect (v.recording, out + n, max - n);
    return n;
}

void Synth::setKeyDna (const osp::keydna::Settings& settings) noexcept
{
    // AMOUNT scales the profile live (KeyDna's apply): only the rest recomputes the table.
    auto shape = settings;
    shape.amount = keyDna.amount;
    const bool same = shape == keyDna;
    keyDna = settings;
    if (! same)
        keyDnaTable.compute (keyDna);
}

void Synth::setTransport (const osp::HostTiming& t) noexcept
{
    timing = t;
    modRuntime.setTiming (t);
    ppqAtBlock = t.ppq;
    blockOffset = 0;
    effects.setTiming (t);
}

//==============================================================================
// Variation

void Synth::rebuildDrift() noexcept
{
    for (int s = 0; s < maxVoices; ++s)
    {
        osp::Prng r (osp::Prng::deriveSeed (driftSeed, static_cast<std::uint64_t> (s), 0xd15u));
        auto& d = drift[static_cast<std::size_t> (s)];
        auto next = [&r] { return static_cast<float> (r.bipolar()); };
        d.pitch1 = next();
        d.pitch2 = next();
        d.width1 = next();
        d.width2 = next();
        d.cutoff = next();
        d.resonance = next();
        d.attack = next();
        d.decay = next();
        d.release = next();
        d.level1 = next();
        d.level2 = next();
        d.noise = next();
        d.wander = d.wanderFrom = d.wanderTo = 0.0f;
        d.wanderStep = 0;
        d.wanderLength = 1;
        d.wanderCount = 0;
    }
}

void Synth::updateWander (Drift& d) noexcept
{
    // A new target every 0.6..1.8 s, reached along a smooth S-curve. Drawn from the slot's
    // own generator, seeded from the variation seed and the step count: reproducible.
    if (++d.wanderStep >= d.wanderLength)
    {
        const auto slot = static_cast<std::uint64_t> (&d - drift.data());
        osp::Prng r (osp::Prng::deriveSeed (driftSeed ^ 0x57414e44u, slot, ++d.wanderCount));
        d.wanderFrom = d.wanderTo;
        d.wanderTo = static_cast<float> (r.bipolar());
        const double seconds = 0.6 + 1.2 * r.nextDouble();
        d.wanderLength = std::max (1, static_cast<int> (seconds * hostRate / controlPeriod));
        d.wanderStep = 0;
    }
    const float x = static_cast<float> (d.wanderStep) / static_cast<float> (d.wanderLength);
    const float s = x * x * (3.0f - 2.0f * x);
    d.wander = d.wanderFrom + (d.wanderTo - d.wanderFrom) * s;
}

double Synth::slotPitchOffsetCents (int slot) const noexcept
{
    return 10.0 * drift[static_cast<std::size_t> (std::clamp (slot, 0, maxVoices - 1))].pitch1;
}

//==============================================================================
// Notes

int Synth::groupSize() const noexcept
{
    return current.unison ? std::clamp (current.polyphony, 1, maxVoices) : 1;
}

void Synth::applyPolyphonyLimit() noexcept
{
    const int limit = monophonic() ? groupSize() : current.polyphony;
    for (auto& v : voices)
        if (v.slot >= limit && v.active && ! v.amp.isReleasing())
        {
            v.held = v.sustained = false;
            releaseVoice (v);
        }
}

Synth::Voice* Synth::allocate() noexcept
{
    const int limit = current.polyphony;
    // A free slot, taking turns (so every slot's character is heard, as on the original).
    for (int i = 0; i < limit; ++i)
    {
        auto& v = voices[static_cast<std::size_t> ((nextSlot + i) % limit)];
        if (! v.active && ! v.pending)
        {
            nextSlot = (v.slot + 1) % limit;
            return &v;
        }
    }
    // Else the quietest released voice, else the oldest held one.
    Voice* best = nullptr;
    for (int i = 0; i < limit; ++i)
    {
        auto& v = voices[static_cast<std::size_t> (i)];
        if (v.pending || v.held)
            continue;
        if (best == nullptr || v.amp.value() < best->amp.value())
            best = &v;
    }
    if (best != nullptr)
        return best;
    for (int i = 0; i < limit; ++i)
    {
        auto& v = voices[static_cast<std::size_t> (i)];
        if (! v.pending && (best == nullptr || v.order < best->order))
            best = &v;
    }
    return best;
}

bool Synth::anyKeyHeld (const Voice* except) const noexcept
{
    for (const auto& v : voices)
        if (&v != except && (v.held || v.sustained || v.pending))
            return true;
    return false;
}

void Synth::startVoice (Voice& v, int channel, int note, float velocity, bool retrigger) noexcept
{
    const bool wasActive = v.active;
    // The first note of a phrase (nothing else held): the LFO restarts (RETRIGGER, ONE SHOT)
    // and KALEIDOSCOPE's resonators tune to this note's harmonics.
    if (retrigger && ! anyKeyHeld (&v))
    {
        // (LFO 1 restarts in the modulation, which follows the keys: noteOn.)
        resonanceModel ^= 1;
        auto& model = resonanceModels[static_cast<std::size_t> (resonanceModel)];
        const double f0 = math::noteToHz (note + current.tuneSemitones);
        for (std::size_t k = 0; k < model.resonanceHz.size(); ++k)
            model.resonanceHz[k] = std::min (f0 * static_cast<double> (k + 1), 0.45 * hostRate);
        kaleidoscopeStage.setModel (&model);
    }
    if (retrigger)
        effects.noteStarted();
    v.channel = channel;
    v.note = note;
    v.velocity = std::clamp (velocity, 0.0f, 1.0f);
    v.order = ++noteCounter;
    v.seed = osp::Prng::deriveSeed (current.seed, v.order, static_cast<std::uint64_t> (note));
    v.held = true;
    v.sustained = false;
    v.pending = false;
    v.fade = 0;
    v.pitchTarget = note;
    if (! monophonic())
    {
        // Glide (poly): each voice slides from the last note it played.
        if (! (current.glideSeconds > 0.0 && v.hasPlayed))
            v.pitch = note;
        v.unisonOffset = 0.0;
        v.pan = static_cast<float> (current.panPercent / 100.0) + static_cast<float> (current.stereoSpread) * slotPan[static_cast<std::size_t> (v.slot)];
        equalPower (v.pan, v.panL, v.panR);
    }
    if (! wasActive)
    {
        v.filter.reset();
        v.line1.reset();
        v.line2.reset();
        // State version 4: the models' extras from the note's seed (SUPER SAW's side phases
        // start spread, as free-running oscillators would be), the filters from rest.
        osp::Prng sidePhases (osp::Prng::deriveSeed (v.seed, 0x5353u, 0));
        for (auto* x : { &v.extra1, &v.extra2 })
        {
            x->subSign = 1.0f;
            x->tilt = 0.0f;
            for (auto& side : x->sides)
                side = sidePhases.nextDouble();
        }
        v.noiseColour = 0.0f;
        v.ringNow = static_cast<float> (current.ringMix);
        v.ota.reset();
        v.otaMix = v.otaTarget = current.filterModel == FilterModel::ota ? 1.0f : 0.0f;
        v.highPass.reset();
        v.highPassTail = 0;
        v.osc2Out = 0.0f;
        v.outGain = 0.0f;
        // A new sound: its own recording from silence, its grains, its drift, its mode.
        v.noise.reseed (osp::Prng::deriveSeed (v.seed, 0x4e4fu, 0));
        v.doublingPhase = 0.0;
        v.doublingDelay = v.doublingTarget = 0.018 * internalRate;
        v.recording.restart();
        v.grainMix = current.granular ? 1.0f : 0.0f;
        v.granulator.start (current.granular ? current.grains : cloudGrains(), v.seed);
        v.driftRng.reseed (osp::Prng::deriveSeed (v.seed, 0x6472696674ull, 0));
        v.driftNow = v.driftFrom = v.driftTo = {};
        v.driftCountdown = 0;
        v.driftSegment = 1;
        updateDrift (v);
        // It begins part-way through a wander, not at its centre (as ANDOR OSP's voices).
        v.driftFrom = v.driftNow = v.driftTo;
        v.driftCountdown = 0;
        v.notePitch = note;
        startModeEngine (v);
    }
    // LFO 1 with POLY voices: each note its own - from PHASE (RETRIGGER, ONE SHOT) or where
    // the free-running LFO is (FREE), as ANDOR OSP's voice LFOs.
    if (current.lfo.scope == osp::mod::Scope::poly && (retrigger || ! wasActive))
    {
        if (current.lfo.mode == osp::mod::LfoMode::free)
            v.lfo = modRuntime.globalLfo[0];
        else
            v.lfo.start (current.lfo, current.lfo.phase);
        v.lfo.evaluate (current.lfo, lfoCurve, v.seed);
    }
    // The note's modulation: its VEL and NOTE, its envelopes and POLY LFOs start with it (a
    // legato note keeps them running); the amplitude's attack and decay are taken now.
    v.mod.note = note;
    v.mod.velocity = std::clamp (static_cast<int> (std::lround (v.velocity * 127.0f)), 1, 127);
    if (retrigger || ! wasActive)
    {
        v.mod.start (modRuntime);
        if (current.lfo.scope == osp::mod::Scope::poly)
            v.mod.values[0] = static_cast<float> (v.lfo.value);   // (LFO 1: the synth's own, this note's)
        using D = osp::mod::Dest;
        const auto& compiled = modRuntime.compiled;
        v.ampAttackSeconds = compiled.has (D::ampAttack) ? osp::mod::modulatedSeconds (D::ampAttack, current.ampAttack, offsetOf (v, D::ampAttack)) : -1.0;
        v.ampDecaySeconds = compiled.has (D::ampDecay) ? osp::mod::modulatedSeconds (D::ampDecay, current.ampDecay, offsetOf (v, D::ampDecay)) : -1.0;
        v.ampReleaseSeconds = -1.0;
    }
    applyVoiceModulation (v);
    updateVoiceControl (v, current.lfo.scope == osp::mod::Scope::poly ? static_cast<float> (v.lfo.value) : lfoValue,
                        static_cast<float> (std::clamp (current.lfoAmount + wheel * (1.0 - current.lfoAmount), 0.0, 1.0)));
    v.outGain = wasActive ? v.outGain : v.outGainTarget;
    if (current.phaseMode == PhaseMode::reset)
    {
        if (retrigger)
        {
            v.phase1 = 0.0;
            v.phase2 = 0.0;
        }
    }
    else if (! wasActive && v.hasPlayed && v.stoppedAt != stillRunning)
    {
        // Free-running: the oscillators kept turning while the voice was silent.
        const double elapsed = static_cast<double> (clock - v.stoppedAt) * oversampling;
        const double p1 = v.phase1 + v.dt1 * elapsed, p2 = v.phase2 + v.dt2 * elapsed;
        v.phase1 = p1 - std::floor (p1);
        v.phase2 = p2 - std::floor (p2);
    }
    if (retrigger || ! wasActive)
    {
        v.amp.noteOn();
        v.env.noteOn();
        v.ageSeconds = 0.0;   // (LFO DELAY counts from here)
        v.releaseWhenPlayedOut = false;
        if (organicSource != nullptr)
            v.organic.start (organicSource, current.organic.start, current.organic.reverse);
        else
            v.organic.stop();
    }
    v.active = true;
    v.hasPlayed = true;
}

void Synth::setOrganic (const osp::InstrumentModel* model) noexcept
{
    if (model == organicSource)
        return;
    for (auto& v : voices)
        if (v.organic.playing())
            v.organic.captureTail (v.organicStep, (v.held || v.sustained) && current.organic.loop);
    organicSource = model;
}

void Synth::releaseVoice (Voice& v) noexcept
{
    // FOLLOW off: a note playing a recording lets it play out before it is released.
    if (! current.organic.follow && v.organic.playing() && v.organicGain > 0.0f)
    {
        v.releaseWhenPlayedOut = true;
        return;
    }
    v.releaseWhenPlayedOut = false;
    // The release time is taken as the note is released (as ANDOR OSP's voices).
    using D = osp::mod::Dest;
    v.mod.release (modRuntime);
    if (modRuntime.compiled.has (D::ampRelease))
    {
        v.ampReleaseSeconds = osp::mod::modulatedSeconds (D::ampRelease, current.ampRelease, offsetOf (v, D::ampRelease));
        const auto& p = patchOf (v);
        v.amp.setTimes (v.ampAttackSeconds >= 0.0 ? v.ampAttackSeconds : p.ampAttack, v.ampDecaySeconds >= 0.0 ? v.ampDecaySeconds : p.ampDecay,
                        p.ampSustain, v.ampReleaseSeconds);
    }
    v.amp.noteOff();
    v.env.noteOff();
}

void Synth::noteOn (int channel, int note, float velocity) noexcept
{
    if (note < 0 || note > 127)
        return;
    if (velocity <= 0.0f)
    {
        noteOff (channel, note);
        return;
    }
    // The modulation follows the keys: a global LFO 1 / 2 in RETRIGGER restarts with the first,
    // the global envelopes start, VEL and NOTE on the shared stages take this key.
    modRuntime.noteStarted (note, channel);
    if (! playedKeysOnly)
        modRuntime.setInputNote (note, std::clamp (static_cast<int> (std::lround (velocity * 127.0f)), 1, 127));
    if (monophonic())
    {
        monoNoteOn (channel, note, velocity);
        return;
    }
    // The same key again while its last note still rings out: that voice plays it again.
    for (int i = 0; i < current.polyphony; ++i)
    {
        auto& v = voices[static_cast<std::size_t> (i)];
        if (v.active && ! v.pending && ! v.held && v.channel == channel && v.note == note)
        {
            startVoice (v, channel, note, velocity, true);
            return;
        }
    }
    auto* v = allocate();
    if (v == nullptr)
        return;
    if (v->active && v->amp.value() > 1.0e-4f)
    {
        // Taken from another note: a 3 ms fade first, then the new note.
        v->pending = true;
        v->pendingChannel = channel;
        v->pendingNote = note;
        v->pendingVelocity = velocity;
        v->pendingRetrigger = true;
        v->held = false;
        v->sustained = false;
        v->fade = std::max (1, static_cast<int> (stealFadeSeconds * internalRate));
        v->order = ++noteCounter;
        return;
    }
    startVoice (*v, channel, note, velocity, true);
}

void Synth::noteOff (int channel, int note) noexcept
{
    modRuntime.noteEnded (note, channel);
    if (monophonic())
    {
        monoNoteOff (channel, note);
        return;
    }
    // A note waiting for its voice to fade is cancelled.
    for (auto& v : voices)
        if (v.pending && v.pendingChannel == channel && v.pendingNote == note)
        {
            v.pending = false;
            v.fade = 0;
            v.held = false;
            v.sustained = false;
            releaseVoice (v);
            return;
        }
    // The oldest held voice of this key (a key pressed twice keeps its newer note).
    Voice* oldest = nullptr;
    for (auto& v : voices)
        if (v.active && v.held && v.channel == channel && v.note == note && (oldest == nullptr || v.order < oldest->order))
            oldest = &v;
    if (oldest == nullptr)
        return;
    oldest->held = false;
    if (pedal)
        oldest->sustained = true;
    else
        releaseVoice (*oldest);
}

void Synth::monoNoteOn (int channel, int note, float velocity) noexcept
{
    // The newest key plays (last-note priority); a key already down moves to the top.
    for (int i = 0; i < keyCount; ++i)
        if (keys[static_cast<std::size_t> (i)].channel == channel && keys[static_cast<std::size_t> (i)].note == note)
        {
            for (int j = i; j + 1 < keyCount; ++j)
                keys[static_cast<std::size_t> (j)] = keys[static_cast<std::size_t> (j + 1)];
            --keyCount;
            break;
        }
    if (keyCount == static_cast<int> (keys.size()))
    {
        for (int j = 0; j + 1 < keyCount; ++j)
            keys[static_cast<std::size_t> (j)] = keys[static_cast<std::size_t> (j + 1)];
        --keyCount;
    }
    keys[static_cast<std::size_t> (keyCount++)] = { channel, note, velocity };
    // Legato: a key pressed while another holds slides there without a new attack.
    const bool legato = monoGate;
    monoGoTo (note, ! legato, velocity, channel);
    monoGate = true;
}

void Synth::monoNoteOff (int channel, int note) noexcept
{
    int found = -1;
    for (int i = 0; i < keyCount; ++i)
        if (keys[static_cast<std::size_t> (i)].channel == channel && keys[static_cast<std::size_t> (i)].note == note)
            found = i;
    if (found < 0)
        return;
    const bool wasTop = found == keyCount - 1;
    for (int j = found; j + 1 < keyCount; ++j)
        keys[static_cast<std::size_t> (j)] = keys[static_cast<std::size_t> (j + 1)];
    --keyCount;
    if (! wasTop)
        return;
    if (keyCount > 0)
    {
        // Back to the key still held (legato).
        const auto& k = keys[static_cast<std::size_t> (keyCount - 1)];
        monoGoTo (k.note, false, k.velocity, k.channel);
        return;
    }
    if (pedal)
    {
        for (int i = 0; i < groupSize(); ++i)
        {
            voices[static_cast<std::size_t> (i)].held = false;
            voices[static_cast<std::size_t> (i)].sustained = true;
        }
        return;
    }
    monoGate = false;
    for (int i = 0; i < groupSize(); ++i)
    {
        auto& v = voices[static_cast<std::size_t> (i)];
        v.held = false;
        releaseVoice (v);
    }
}

void Synth::monoGoTo (int note, bool retrigger, float velocity, int channel) noexcept
{
    const int count = groupSize();
    const bool glide = current.glideSeconds > 0.0 && lastMonoPitch >= 0.0;
    for (int i = 0; i < count; ++i)
    {
        auto& v = voices[static_cast<std::size_t> (i)];
        const double spreadPosition = count > 1 ? 2.0 * i / (count - 1) - 1.0 : 0.0;
        v.unisonOffset = current.unison ? 0.25 * current.unisonDetune * spreadPosition : 0.0;
        v.pan = static_cast<float> (current.panPercent / 100.0 + current.stereoSpread * spreadPosition * (current.unison ? 1.0 : 0.0));
        equalPower (v.pan, v.panL, v.panR);
        if (! v.active || ! glide)
            v.pitch = glide ? lastMonoPitch : note;
        if (v.active && ! retrigger)
        {
            v.note = note;
            v.channel = channel;
            v.pitchTarget = note;
            v.held = true;
            v.sustained = false;
            if (! glide)
                v.pitch = note;
            continue;
        }
        startVoice (v, channel, note, velocity, true);
    }
    lastMonoPitch = note;
}

void Synth::setPitchBend (float value) noexcept { bend = std::clamp (value, -1.0f, 1.0f); }
void Synth::setModWheel (float value) noexcept
{
    wheel = std::clamp (value, 0.0f, 1.0f);
    modRuntime.setModWheel (wheel);
}

void Synth::setSustainPedal (bool down) noexcept
{
    pedal = down;
    modRuntime.setPedal (down);
    if (down)
        return;
    if (monophonic())
    {
        if (keyCount == 0 && monoGate)
        {
            monoGate = false;
            for (int i = 0; i < groupSize(); ++i)
            {
                auto& v = voices[static_cast<std::size_t> (i)];
                v.sustained = false;
                v.held = false;
                releaseVoice (v);
            }
        }
        return;
    }
    for (auto& v : voices)
        if (v.sustained)
        {
            v.sustained = false;
            releaseVoice (v);
        }
}

void Synth::allNotesOff() noexcept
{
    modRuntime.allNotesOff();
    keyCount = 0;
    monoGate = false;
    for (auto& v : voices)
    {
        if (v.pending)
        {
            v.pending = false;
            v.fade = 0;
        }
        v.held = v.sustained = false;
        if (v.active)
            releaseVoice (v);
    }
}

void Synth::allSoundOff() noexcept
{
    modRuntime.allNotesOff();
    keyCount = 0;
    monoGate = false;
    for (auto& v : voices)
    {
        v.active = v.held = v.sustained = v.pending = false;
        v.fade = 0;
        v.amp.kill();
        v.env.kill();
        v.filter.reset();
        v.line1.reset();
        v.line2.reset();
        v.outGain = 0.0f;
        v.stoppedAt = clock;
    }
    // What is still in the decimators and the DC blocker goes too (the effects' tails stay).
    for (auto* d : { &stage1L, &stage1R, &stage2L, &stage2R })
        d->reset();
    std::fill (mixL.begin(), mixL.end(), 0.0f);
    std::fill (mixR.begin(), mixR.end(), 0.0f);
    dcL = dcR = dcStateL = dcStateR = 0.0f;
}

void Synth::resetControllers() noexcept
{
    bend = 0.0f;
    setModWheel (0.0f);
    setSustainPedal (false);
}

void Synth::handleMidi (const std::uint8_t* data, int size) noexcept
{
    if (data == nullptr || size < 1)
        return;
    const int status = data[0] & 0xf0;
    const int channel = (data[0] & 0x0f) + 1;
    const int a = size > 1 ? data[1] & 0x7f : 0;
    const int b = size > 2 ? data[2] & 0x7f : 0;
    switch (status)
    {
        case 0x90:
            if (b > 0)
                noteOn (channel, a, static_cast<float> (b) / 127.0f);
            else
                noteOff (channel, a);
            break;
        case 0x80: noteOff (channel, a); break;
        case 0xe0: setPitchBend (static_cast<float> ((b << 7 | a) - 8192) / 8192.0f); break;
        case 0xd0: modRuntime.setPressure (static_cast<float> (a) / 127.0f); break;   // PRESSURE
        case 0xa0: modRuntime.setPressure (static_cast<float> (b) / 127.0f); break;
        case 0xb0:
            // CC 1-4: the slots following this controller.
            for (std::size_t slot = 0; slot < modControllers.size(); ++slot)
                if (modControllers[slot] == a)
                    modRuntime.setController (static_cast<int> (slot), static_cast<float> (b) / 127.0f);
            switch (a)
            {
                case 1: setModWheel (static_cast<float> (b) / 127.0f); break;
                case 64: setSustainPedal (b >= 64); break;
                case 120: allSoundOff(); break;
                case 121: resetControllers(); break;
                case 123: case 124: case 125: case 126: case 127: allNotesOff(); break;
                default: break;
            }
            break;
        default: break;
    }
}

//==============================================================================
// Control rate

float Synth::cutoffG (float note) const noexcept
{
    const float position = (std::clamp (note, tableLowNote, tableHighNote) - tableLowNote) * static_cast<float> (tableStepsPerNote);
    const auto i = static_cast<int> (position);
    const float f = position - static_cast<float> (i);
    const auto* t = gTable.data() + i;
    return t[0] + f * (t[1] - t[0]);
}

void Synth::applyVoiceModulation (Voice& v) noexcept
{
    v.panMod = 0.0f;
    if (! voiceRoutes)
    {
        v.modulated = false;
        return;
    }
    // From the knobs as they are (`current`), each modulated control moved for this note.
    if (! v.modulated || v.patchGeneration != patchGeneration)
    {
        v.patch = current;
        v.patchGeneration = patchGeneration;
    }
    v.modulated = true;
    auto& p = v.patch;
    if (modulator != nullptr)
        for (int n = 0; n < voiceControlCount; ++n)
        {
            const int k = voiceControls[static_cast<std::size_t> (n)];
            modulator->moveControl (p, k, offsetOf (v, osp::mod::productDest (k)));
        }
    // ANDOR OSP's destinations the synth plays, in their own units (as its voices take them).
    using D = osp::mod::Dest;
    const auto& compiled = modRuntime.compiled;
    if (compiled.has (D::levelA))
    {
        const double total = std::max (current.levelDb, osp::mod::Runtime::silentLevelDb) + offsetOf (v, D::levelA);
        p.levelDb = total <= osp::mod::Runtime::silentLevelDb ? -96.0 : std::min (total, osp::mod::Runtime::maxLevelDb);
    }
    if (compiled.has (D::panA))
        v.panMod = static_cast<float> (offsetOf (v, D::panA));
    if (compiled.has (D::fineTuneA))
        p.tuneSemitones = std::clamp (current.tuneSemitones + offsetOf (v, D::fineTuneA) / 100.0, -osp::mod::Runtime::maxTuneSemitones,
                                      osp::mod::Runtime::maxTuneSemitones);
    if (compiled.has (D::reimaginedA))
        p.reimagined = std::clamp (current.reimagined + offsetOf (v, D::reimaginedA), 0.0, 1.0);
    if (compiled.has (D::grainPositionA))
        p.grains.position = std::clamp (current.grains.position + offsetOf (v, D::grainPositionA), 0.0, 1.0);
    if (compiled.has (D::grainSpreadA))
        p.grains.spread = std::clamp (current.grains.spread + offsetOf (v, D::grainSpreadA), 0.0, 1.0);
    if (compiled.has (D::grainDensityA))
        p.grains.density = std::clamp (current.grains.density * std::exp2 (offsetOf (v, D::grainDensityA)), 4.0, 40.0);
    if (compiled.has (D::grainSizeA))
        p.grains.sizeSeconds = std::clamp (current.grains.sizeSeconds * std::exp2 (offsetOf (v, D::grainSizeA)), 0.02, 0.4);
    if (compiled.has (D::ampSustain))
        p.ampSustain = std::clamp (current.ampSustain + offsetOf (v, D::ampSustain), 0.0, 1.0);
}

void Synth::updateVoiceControl (Voice& v, float lfoNow, float depth) noexcept
{
    const auto& p = patchOf (v);
    if (v.modulated)   // (LFO AMOUNT may be among the moved controls)
        depth = static_cast<float> (std::clamp (p.lfoAmount + wheel * (1.0 - p.lfoAmount), 0.0, 1.0));
    const auto& d = drift[static_cast<std::size_t> (v.slot)];
    const auto a = static_cast<float> (std::clamp (p.analogAmount, 0.0, 1.0));
    // LFO DELAY / RISE (state version 4): each note's LFO depths wait, then fade in. Off (both
    // 0): exactly 1, the original.
    v.lfoFade = 1.0f;
    if (p.lfoDelaySeconds > 0.0 || p.lfoRiseSeconds > 0.0)
    {
        const double after = v.ageSeconds - p.lfoDelaySeconds;
        v.lfoFade = after <= 0.0 ? 0.0f : (p.lfoRiseSeconds > 0.0 ? static_cast<float> (std::min (1.0, after / p.lfoRiseSeconds)) : 1.0f);
    }
    const float lfoD = lfoNow * depth * v.lfoFade;

    // Pitch: the note (gliding), TUNE and FINE, the wheel, unison, the slot's tolerance and
    // its wander (ANALOG: +-10 cents fixed and +-3 cents moving at 100 %).
    const double performed = p.tuneSemitones + p.fineTuneCents / 100.0 + bend * p.bendRangeSemitones;
    // MOVEMENT DRIFT (the voice's own wander and the shared one); REIMAGINED's read speed
    // (wow, self-modulation) moves the oscillators themselves while they are the source.
    const double drifted = (v.driftNow[0] + sharedDriftCents) / 100.0
                           + (v.modeEngine != nullptr ? 12.0 * std::log2 (std::max (1.0e-3, v.modeFactor)) * (1.0 - v.grainMix) : 0.0);
    const double common = v.pitch + v.unisonOffset + performed + drifted;
    v.notePitch = common;
    // A DCO is pitch-stable: a fifth of the tolerance and a tenth of the wander (its clock is
    // a crystal; what is left is the divider's and the waveshaper's own small spread).
    const double tolerance1 = p.osc1Model == OscModel::dco ? 0.2 : 1.0, wander1 = p.osc1Model == OscModel::dco ? 0.1 : 1.0;
    const double tolerance2 = p.osc2Model == OscModel::dco ? 0.2 : 1.0, wander2 = p.osc2Model == OscModel::dco ? 0.1 : 1.0;
    const double note1 = common + 12.0 * p.osc1Octave + p.osc1Coarse + a * (0.10 * tolerance1 * d.pitch1 + 0.03 * wander1 * d.wander) + lfoD * p.lfoPitch1;
    const double base2 = p.osc2KeyTrack ? common : 60.0 + v.unisonOffset + performed;
    const double note2 = base2 + 12.0 * p.osc2Octave + p.osc2Coarse + p.osc2FineCents / 100.0 + a * (0.10 * tolerance2 * d.pitch2 - 0.03 * wander2 * d.wander)
                         + lfoD * p.lfoPitch2 - (p.osc2LowFrequency ? 12.0 * lowFrequencyOctaves : 0.0);
    v.dt1 = std::clamp (math::noteToHz (note1) / internalRate, 1.0e-9, 0.45);
    v.dt2 = std::clamp (math::noteToHz (note2) / internalRate, 1.0e-9, 0.45);
    v.width1 = static_cast<float> (std::clamp (p.osc1Width + 0.04 * a * d.width1 + lfoD * p.lfoWidth1, 0.02, 0.98));
    v.width2 = static_cast<float> (std::clamp (p.osc2Width + 0.04 * a * d.width2 + lfoD * p.lfoWidth2, 0.02, 0.98));

    // Mixer: BLEND crossfades (equal power) between the two oscillators' own levels.
    // (A control moved for this note moves its smoothed value by as much.)
    const auto moved = [&v, this] (double now, double knob) { return v.modulated ? static_cast<float> (now - knob) : 0.0f; };
    const float blendNow = std::clamp (blendSmoothed + moved (p.blend, current.blend), 0.0f, 1.0f);
    const float theta = blendNow * static_cast<float> (math::pi) * 0.5f;
    const float levelDrift = 0.17f * a;   // +-1.5 dB at 100 %
    v.gain1 = static_cast<float> (p.osc1Level) * std::min (1.0f, 1.41421356f * std::cos (theta)) * (1.0f + levelDrift * d.level1);
    v.gain2 = static_cast<float> (p.osc2Level) * std::min (1.0f, 1.41421356f * std::sin (theta)) * (1.0f + levelDrift * d.level2);
    v.gainNoise = static_cast<float> (p.noiseLevel) * 0.8f * (1.0f + levelDrift * d.noise);
    if (! p.osc1Enabled)
        v.gain1 = 0.0f;
    if (! p.osc2Enabled)
        v.gain2 = 0.0f;

    // State version 4: the slots' models (CLASSIC + CLASSIC, no ring, white noise: the
    // original path, untouched), DCO PWM, RING MOD glided.
    v.ringNow += 0.25f * (static_cast<float> (p.ringMix) - v.ringNow);
    if (v.ringNow < 1.0e-4f && p.ringMix <= 0.0)
        v.ringNow = 0.0f;
    // ORGANIC: the note's pitch (its TUNE) against the recording's root (or the override).
    if (v.organic.playing())
        v.organicStep = v.organic.stepFor (common + p.organic.tuneSemitones, static_cast<double> (p.organic.rootOverride));
    v.organicGain = p.organic.heard && p.organic.levelDb > -47.99 ? math::dbToGain (static_cast<float> (p.organic.levelDb)) : 0.0f;
    v.organicOn = ! v.organic.finished();
    v.classicPair = p.osc1Model == OscModel::classic && p.osc2Model == OscModel::classic && v.ringNow == 0.0f && p.noiseColor == NoiseColor::white
                    && ! v.organicOn;
    if (! v.classicPair)
    {
        planSlot (v, 1, p, v.dt1);
        planSlot (v, 2, p, v.dt2);
        const float lfoRaw = lfoNow * v.lfoFade;   // (PWM's own depth: LFO AMOUNT does not scale it)
        auto pwm = [&] (const DcoSettings& c, float& lfoOffset, float& envDepth) {
            const float depthNow = 0.45f * static_cast<float> (std::clamp (c.pwmDepth, 0.0, 1.0));
            lfoOffset = c.pwmSource == PwmSource::lfo ? depthNow * lfoRaw : 0.0f;
            envDepth = c.pwmSource == PwmSource::env ? depthNow : 0.0f;
        };
        pwm (p.dco1, v.pwm1, v.pwmEnv1);
        pwm (p.dco2, v.pwm2, v.pwmEnv2);
        if (p.osc1Model != OscModel::dco)
            v.pwm1 = v.pwmEnv1 = 0.0f;
        if (p.osc2Model != OscModel::dco)
            v.pwm2 = v.pwmEnv2 = 0.0f;
    }

    // Filter: TONE, keyboard tracking around C4, the slot's tolerance (+-1 semitone at 100 %)
    // and wander, the LFO; the envelope (scaled by velocity) is added per sample.
    float toneMoved = 0.0f;
    if (v.modulated && (p.tone < current.tone || p.tone > current.tone))
        toneMoved = static_cast<float> (math::hzToNote (math::toneToHz (p.tone)) - math::hzToNote (math::toneToHz (current.tone)));
    v.cutoffNote = toneNote + toneMoved + static_cast<float> (p.keyTrack * (v.pitch - 60.0)) + a * (1.0f * d.cutoff + 0.2f * d.wander)
                   + lfoD * static_cast<float> (p.lfoCutoff) + static_cast<float> (12.0 * v.driftNow[1]);
    const double velocityScale = 1.0 - p.velocityToFilter + p.velocityToFilter * v.velocity;
    v.envDepth = static_cast<float> (p.envDepthSemitones * velocityScale);
    const float resonanceNow = std::clamp (resonanceSmoothed + moved (p.resonance, current.resonance), 0.0f, 1.0f);
    v.k = std::clamp (4.15f * resonanceNow * (1.0f + 0.06f * a * d.resonance), 0.0f, 4.2f);
    v.resonance01 = std::clamp (resonanceNow * (1.0f + 0.06f * a * d.resonance), 0.0f, 1.0f);
    // The Juno variants are calibrations, not engines. The 6 and the 60 share one voice
    // circuit (no source tells their filters apart), so ANDOR voices the 6 a semitone darker
    // and 5 % less resonant: a voicing choice (by ear), not a measurement. The 106 differs by
    // its high-pass switch.
    v.otaCutoffOffset = 0.0f;
    if (p.otaVoicing == OtaVoicing::juno && p.junoVariant == JunoVariant::juno6)
    {
        v.otaCutoffOffset = -1.0f;
        v.resonance01 *= 0.95f;
    }
    v.otaTarget = p.filterModel == FilterModel::ota ? 1.0f : 0.0f;
    if (v.otaTarget > 0.0f && v.otaMix <= 0.0f)
        v.ota.reset();   // (fading in from rest, never from a stale state)
    // The high-pass before the low-pass: its curve the OTA voicing's (JUPITER) or the Juno
    // variant's; switched off, it glides to FLAT and then sleeps (no click either way).
    {
        const auto variant = p.otaVoicing == OtaVoicing::jupiter ? HpfVariant::jupiter
                                                                 : (p.junoVariant == JunoVariant::juno6 ? HpfVariant::juno6 : p.junoVariant == JunoVariant::juno60 ? HpfVariant::juno60 : HpfVariant::juno106);
        const int flat = variant == HpfVariant::juno106 ? 1 : 0;
        if (p.highPass >= 0)
        {
            v.highPass.setMode (variant, std::clamp (p.highPass, 0, 3));
            v.highPassTail = std::max (1, static_cast<int> (VoiceHighPass::rampSeconds * internalRate) + controlPeriod * oversampling);
        }
        else if (v.highPassTail > 0)
            v.highPass.setMode (variant, flat);
    }
    equalPower (v.pan + static_cast<float> (v.driftNow[3]) + v.panMod, v.panL, v.panR);   // its place, as DRIFT and PAN's routes move it
    const float driveMoved = moved (p.filterDrive, current.filterDrive);
    v.driveGain = driveMoved != 0.0f ? std::exp2 (4.0f * std::clamp (driveSmoothed + driveMoved, 0.0f, 1.0f)) : driveGain;
    v.makeUp = (1.0f + 0.35f * v.k) / std::sqrt (v.driveGain) * math::dbToGain (p.driveTrimDb);
    v.otaMakeUp = 1.0f / std::sqrt (v.driveGain) * math::dbToGain (p.driveTrimDb);   // (the OTA compensates its own resonance loss)

    // Envelopes: the slot's tolerance on the times (+-20 % at 100 %).
    const double t = 0.2 * a;
    v.env.setTimes (p.filterAttack * (1.0 + t * d.attack), p.filterDecay * (1.0 + t * d.decay), p.filterSustain, p.filterRelease * (1.0 + t * d.release));
    if (p.vcaGate)   // GATE: the amplitude opens and closes with the key (a few ms against clicks)
        v.amp.setTimes (0.002, 0.001, 1.0, 0.006);
    else if (p.envLink)   // ENV LINK: the amplitude follows the filter envelope's ADSR (one envelope, as on a Juno-106)
        v.amp.setTimes (p.filterAttack * (1.0 + t * d.attack), p.filterDecay * (1.0 + t * d.decay), p.filterSustain, p.filterRelease * (1.0 + t * d.release));
    else
        v.amp.setTimes ((v.ampAttackSeconds >= 0.0 ? v.ampAttackSeconds : p.ampAttack) * (1.0 + t * d.attack),
                        (v.ampDecaySeconds >= 0.0 ? v.ampDecaySeconds : p.ampDecay) * (1.0 + t * d.decay), p.ampSustain,
                        (v.ampReleaseSeconds >= 0.0 ? v.ampReleaseSeconds : p.ampRelease) * (1.0 + t * d.release));

    // Level: velocity (VELOCITY RANGE dB between the hardest and softest note), LEVEL, MUTE.
    // APPLY TO AMP: VEL's curve shapes the velocity the loudness follows (a straight curve,
    // the default, leaves it exactly as played).
    double velocity = v.velocity;
    if (modRuntime.settings.velocityToAmp)
        if (const double shaped = modRuntime.velocityValue (v.mod.velocity); std::abs (shaped - velocity) > 1.0e-4)
            velocity = shaped;
    const double soft = 1.0 - velocity;
    const float velocityGain = math::dbToGain (-p.velocityRangeDb * soft * soft);
    // LEVEL at its bottom (ANDOR OSP's -48 dB) is silence.
    const float level = p.levelDb <= -47.99 ? 0.0f : math::dbToGain (p.levelDb);
    // MUTE (the oscillators' card, or ORGANIC's solo): with a recording playing, only the
    // oscillators and the noise fall silent (above); without one, the voice as before.
    if (p.mute && v.organicOn)
        v.gain1 = v.gain2 = v.gainNoise = 0.0f;
    v.outGainTarget = p.mute && ! v.organicOn ? 0.0f : velocityGain * level * voiceGain * math::dbToGain (static_cast<float> (v.driftNow[2]));

    // The source: ONE SHOT <-> GRANULAR glides (about 8 ms); the grains' settings, moved by
    // the REIMAGINED mode's travel and the key's KEY DNA; KALEIDOSCOPE's per-voice parts.
    v.grainMix += 0.05f * ((p.granular ? 1.0f : 0.0f) - v.grainMix);
    const auto& rs = p.reimaginedSettings;
    const double r = rs.mode == osp::ReimaginedMode::kaleidoscope ? std::clamp (p.reimagined, 0.0, 1.0) : 0.0;
    osp::NoteShape shape;
    osp::kaleidoscope::shapeNote (shape, r, p.motion, rs.kaleidoscope);
    v.saturationDrive = 1.0f + 4.0f * std::clamp (shape.saturation, 0.0f, 1.0f);
    v.doubling = shape.doubling;
    v.doublingPhase += 0.13 * static_cast<double> (controlPeriod) / hostRate;
    v.doublingPhase -= std::floor (v.doublingPhase);
    v.doublingTarget = (0.018 + 0.006 * std::sin (2.0 * math::pi * v.doublingPhase)) * internalRate;
    v.cloud = (1.0f - v.grainMix) * shape.granular;
    v.granulator.setRemap (shape.granular);
    v.grainStep = v.modeEngine != nullptr ? v.modeFactor : 1.0;
    if (p.granular)
    {
        auto g = p.grains;
        if (v.modeEngine != nullptr)
            g.position = std::clamp (g.position + v.modeEngine->grainPositionOffset(), 0.0, 1.0);
        if (keyDnaTable.active())
            osp::keydna::apply (keyDnaTable.at (v.note), keyDna.amount, g);
        v.granulator.setParams (g);
    }
    else
        v.granulator.setParams (cloudGrains());
}

void Synth::updateDrift (Voice& v) noexcept
{
    // ANDOR OSP's per-voice DRIFT (InstrumentEngine's shape, InstrumentVoice's wander), with
    // KALEIDOSCOPE's own instability: depths in cents, octaves of tone, dB and place.
    const auto& sh = current.effects;
    const bool driftMode = sh.movementMode == osp::MovementMode::drift;
    const double m = driftMode ? std::pow (std::clamp (current.motion, 0.0, 1.0), 0.75) : 0.0;
    const auto& rs = current.reimaginedSettings;
    const double r = rs.mode == osp::ReimaginedMode::kaleidoscope ? std::clamp (current.reimagined, 0.0, 1.0) : 0.0;
    const double rr = 0.5 * r * r;
    const std::array<double, 4> depth {
        (m * osp::shaping::driftPitchCents (sh.driftPitch) + 12.0 * rr) * (1.0 + 2.0 * std::max (0.0, r - 0.6)),
        m * osp::shaping::driftToneOctaves (sh.driftTone) + 0.6 * rr,
        1.5 * m + 1.0 * rr,
        0.3 * m + 0.2 * rr * static_cast<double> (osp::kaleidoscope::width (rs.kaleidoscope.spread)),
    };
    if (depth[0] <= 0.0 && depth[1] <= 0.0 && depth[2] <= 0.0 && depth[3] <= 0.0)
    {
        v.driftNow = v.driftFrom = v.driftTo = {};
        v.driftCountdown = 0;
        return;
    }
    if (--v.driftCountdown <= 0)
    {
        // A new glide from wherever the drift is now to fresh random values.
        v.driftFrom = v.driftNow;
        for (std::size_t i = 0; i < depth.size(); ++i)
        {
            const double magnitude = v.driftRng.uniform (0.35, 1.0);
            v.driftTo[i] = depth[i] * (v.driftRng.nextDouble() < 0.5 ? -magnitude : magnitude);
        }
        const double rateHz = driftMode ? osp::shaping::driftSpeedHz (sh.driftSpeed) : 0.15;
        const double seconds = v.driftRng.uniform (0.6, 1.4) / std::max (0.01, rateHz);
        v.driftCountdown = v.driftSegment = std::max (1, static_cast<int> (seconds * hostRate / controlPeriod));
    }
    const double f = 1.0 - static_cast<double> (v.driftCountdown) / static_cast<double> (v.driftSegment);
    const double glide = f * f * (3.0 - 2.0 * f);
    for (std::size_t i = 0; i < depth.size(); ++i)
        v.driftNow[i] = v.driftFrom[i] + (v.driftTo[i] - v.driftFrom[i]) * glide;
}

osp::ReimaginedControl Synth::modeControl (const Voice& v) const noexcept
{
    osp::ReimaginedControl c;
    c.amount = std::clamp (patchOf (v).reimagined, 0.0, 1.0);
    c.settings = &current.reimaginedSettings;
    c.step = std::exp2 ((v.notePitch - 60.0) / 12.0);   // the "recording" is rooted at C4
    return c;
}

void Synth::startModeEngine (Voice& v) noexcept
{
    // As on an ANDOR OSP layer: a note uses the mode its layer named when it started, and a
    // note started at 0 % plays without one. KALEIDOSCOPE is the voice's own path; MOSAIC
    // rebuilds a recording from its harmonic analysis, which a live sound does not have.
    v.modeEngine = nullptr;
    v.modeFactor = 1.0;
    v.modeCountdown = 0;
    if (! (current.reimagined > 0.0))
        return;
    osp::ReimaginedVoiceEngine* engine = nullptr;
    const osp::PlaybackSource* source = nullptr;
    switch (current.reimaginedSettings.mode)
    {
        case osp::ReimaginedMode::tapeFrame: engine = &v.tapeFrame; source = &tapeSource; break;
        case osp::ReimaginedMode::toybox:    engine = &v.toybox; source = &toyboxSource; break;
        case osp::ReimaginedMode::mirage:    engine = &v.mirage; source = &mirageSource; break;
        case osp::ReimaginedMode::kaleidoscope:
        case osp::ReimaginedMode::mosaic:    return;
    }
    osp::ReimaginedNote n;
    n.model = &liveModel;
    n.source = source;
    n.outputRate = internalRate;
    n.rootStep = 1.0;
    n.loop = true;          // a held note sustains
    n.granular = true;      // it transforms the live sound (as an ANDOR OSP granular layer's grains)
    n.velocity = v.velocity;
    n.note = v.note;
    n.seed = v.seed;
    if (engine->start (n, modeControl (v)))
        v.modeEngine = engine;
}

void Synth::controlUpdate() noexcept
{
    const auto& p = current;
    const double seconds = static_cast<double> (controlPeriod) / hostRate;

    // Smoothed knobs (about 15 ms), jumping to their values the first time.
    const auto wantedTone = static_cast<float> (math::hzToNote (math::toneToHz (p.tone)));
    const float c = smoothingPrimed ? math::smoothingCoefficient (0.015, hostRate / controlPeriod) : 1.0f;
    toneNote += c * (wantedTone - toneNote);
    resonanceSmoothed += c * (static_cast<float> (p.resonance) - resonanceSmoothed);
    driveSmoothed += c * (static_cast<float> (p.filterDrive) - driveSmoothed);
    blendSmoothed += c * (static_cast<float> (p.blend) - blendSmoothed);
    smoothingPrimed = true;
    // DRIVE: up to +24 dB into the saturator (about half of it taken back after the filter,
    // per voice); BIAS shifts the saturator off centre for even harmonics.
    driveGain = std::exp2 (4.0f * driveSmoothed);
    driveBias = 0.6f * static_cast<float> (std::clamp (p.driveBias, 0.0, 1.0));
    driveBiasOffset = math::tanhFast (driveBias);

    // The modulation's shared sources; among them the LFO (ANDOR OSP's LFO 1): on the host's
    // beat while it plays (SYNC, FREE), else free.
    modRuntime.advanceGlobal (controlPeriod, hostRate);
    lfoValue = static_cast<float> (modRuntime.globalLfo[0].value);

    // MOVEMENT DRIFT's shared part: the whole instrument wanders a little together.
    sharedDriftCents = 0.0;
    if (p.effects.movementMode == osp::MovementMode::drift && p.motion > 0.0)
        sharedDriftCents = 0.3 * p.motion * osp::shaping::driftPitchCents (p.effects.driftPitch)
                           * osp::shaping::sharedWander (p.seed, static_cast<double> (clock) / hostRate, osp::shaping::driftSpeedHz (p.effects.driftSpeed));
    const auto depth = static_cast<float> (std::clamp (p.lfoAmount + wheel * (1.0 - p.lfoAmount), 0.0, 1.0));

    // Glide: the time to cover ~95 % of the interval.
    const double glideCoef = p.glideSeconds > 0.0 ? 1.0 - std::exp (-seconds * 3.0 / p.glideSeconds) : 1.0;
    for (auto& d : drift)
        updateWander (d);
    for (auto& v : voices)
    {
        if (! v.active && ! v.pending)
            continue;
        v.pitch += (v.pitchTarget - v.pitch) * glideCoef;
        v.ageSeconds += seconds;
        if (v.releaseWhenPlayedOut && ! v.organic.playing())
            releaseVoice (v);   // (FOLLOW off: the recording has played out)
        updateDrift (v);
        float lfoNow = lfoValue;
        if (p.lfo.scope == osp::mod::Scope::poly)
        {
            if (p.lfo.sync && timing.valid && timing.playing)
                v.lfo.follow (p.lfo, ppqAtBlock + static_cast<double> (blockOffset) / hostRate * timing.bpm / 60.0);
            else
                v.lfo.advance (p.lfo, seconds, timing.valid ? timing.bpm : 120.0);
            v.lfo.evaluate (p.lfo, lfoCurve, v.seed);   // (RANDOM: each note its own values)
            lfoNow = static_cast<float> (v.lfo.value);
        }
        // This note's sources (LFO 1 POLY: the synth's own, as the classic depths hear it), and
        // what its routes move.
        v.mod.advance (modRuntime, seconds);
        if (p.lfo.scope == osp::mod::Scope::poly)
            v.mod.values[0] = lfoNow;
        applyVoiceModulation (v);
        updateVoiceControl (v, lfoNow, depth);
    }
}

//==============================================================================
// Audio

void Synth::planSlot (Voice& v, int slot, const Patch& p, double dt) noexcept
{
    auto& plan = slot == 1 ? v.plan1 : v.plan2;
    auto& x = slot == 1 ? v.extra1 : v.extra2;
    const auto model = slot == 1 ? p.osc1Model : p.osc2Model;
    plan = {};
    plan.model = model;
    if (model == OscModel::classic)
    {
        plan.waves = slot == 1 ? osc::wavesOf (p.osc1Wave) : osc::wavesOf (p.osc2Wave);
        return;
    }
    if (model == OscModel::dco)
    {
        const auto& c = slot == 1 ? p.dco1 : p.dco2;
        plan.waves = osc::wavesOf (c);
        plan.subLevel = static_cast<float> (std::clamp (c.subLevel, 0.0, 1.0));
        return;
    }
    const auto& w = slot == 1 ? p.wide1 : p.wide2;
    plan.waves = osc::wavesOf (w.wave);
    plan.squareWidth = w.wave == WideWave::square;
    plan.superSaw = w.wave == WideWave::superSaw;
    if (plan.superSaw)
    {
        const double cents = osc::superSawCents (w.spread);
        for (int i = 0; i < osc::superSawSides; ++i)
            x.sideRatio[static_cast<std::size_t> (i)] = std::exp2 (osc::superSawOffsets[static_cast<std::size_t> (i)] * cents / 1200.0);
        x.sideLevel = osc::superSawSideLevel (w.spread);
        x.sawNormal = osc::superSawNormal (x.sideLevel);
    }
    // SHAPE: a one-pole low-pass that follows the pitch (a fixed number of harmonics: the
    // wave's edge, not a filter sweep): 4 (soft) .. 32 (calibrated) .. 256 harmonics; raw at
    // the top. Each voice slot's own tolerance moves it a little (+-10 % at ANALOG 100 %).
    const double shape = std::clamp (w.shape, 0.0, 1.0);
    plan.tilt = shape < 0.999;
    if (plan.tilt)
    {
        const auto& d = drift[static_cast<std::size_t> (v.slot)];
        const double spread = 1.0 + 0.1 * std::clamp (p.analogAmount, 0.0, 1.0) * (slot == 1 ? d.width1 : d.width2);
        const double corner = std::min (0.45, dt * 4.0 * std::pow (64.0, shape) * spread);
        x.tiltCoef = static_cast<float> (1.0 - std::exp (-2.0 * math::pi * corner));
    }
}

float Synth::modelPairSample (Voice& v, const Patch& p, float envValue, float& polyMod) noexcept
{
    // As the CLASSIC pair below, slot by slot: OSC 2 first (Poly Mod's source, sync's master).
    double wrap2 = -1.0;
    const float width2 = std::clamp (v.width2 + v.pwm2 + v.pwmEnv2 * envValue, 0.02f, 0.98f);
    if (v.plan2.model == OscModel::classic)
    {
        v.phase2 = osc::walk (v.phase2, v.dt2, width2, v.plan2.waves, 1.0, 0.0, v.line2, wrap2);
        v.osc2Out = v.line2.end (osc::naive (v.plan2.waves, v.phase2, width2));
    }
    else
        v.osc2Out = osc::renderModel (v.phase2, v.line2, v.extra2, v.plan2, v.dt2, width2, -1.0, wrap2);

    const float pm = envValue * static_cast<float> (p.polyModEnv * p.polyModEnv) + v.osc2Out * static_cast<float> (p.polyModOsc2 * p.polyModOsc2);
    polyMod = pm;
    double dt1 = v.dt1;
    float width1 = std::clamp (v.width1 + v.pwm1 + v.pwmEnv1 * envValue, 0.02f, 0.98f);
    if (p.polyModToPitch1)
        dt1 = std::clamp (dt1 * static_cast<double> (math::exp2Fast (pm * polyModPitchSemitones / 12.0f)), 1.0e-9, 0.45);
    if (p.polyModToWidth1)
        width1 = std::clamp (width1 + polyModWidth * pm, 0.02f, 0.98f);

    double wrap1 = -1.0;
    const double syncAt = p.sync && wrap2 >= 0.0 ? wrap2 : -1.0;
    float o1;
    if (v.plan1.model == OscModel::classic)
    {
        const auto& w1 = v.plan1.waves;
        if (syncAt >= 0.0)
        {
            const double atReset = osc::walk (v.phase1, dt1, width1, w1, 1.0 - syncAt, syncAt, v.line1, wrap1);
            osc::syncReset (atReset, dt1, width1, w1, static_cast<float> (syncAt), v.line1);
            v.phase1 = osc::walk (0.0, dt1, width1, w1, syncAt, 0.0, v.line1, wrap1);
        }
        else
            v.phase1 = osc::walk (v.phase1, dt1, width1, w1, 1.0, 0.0, v.line1, wrap1);
        o1 = v.line1.end (osc::naive (w1, v.phase1, width1));
    }
    else
        o1 = osc::renderModel (v.phase1, v.line1, v.extra1, v.plan1, dt1, width1, syncAt, wrap1);

    float mix = o1 * v.gain1 + v.osc2Out * v.gain2;
    // RING MOD: OSC 1 x OSC 2, crossfaded in (never a loud third voice): the product's RMS of
    // two full-scale saws is a third against a saw's 0.58, hence the 1.7; the slots' levels set it.
    if (v.ringNow > 0.0f)
        mix += v.ringNow * (1.7f * o1 * v.osc2Out * 0.5f * (v.gain1 + v.gain2) - mix);
    if (v.gainNoise > 0.0f)
    {
        float n = static_cast<float> (v.noise.bipolar());
        if (p.noiseColor == NoiseColor::juno)
        {
            // JUNO: a darker hiss (one pole near 5 kHz at any internal rate), its level kept.
            const float c = static_cast<float> (1.0 - std::exp (-2.0 * math::pi * 5000.0 / internalRate));
            v.noiseColour += c * (n - v.noiseColour);
            n = v.noiseColour * 1.36f;   // (the same loudness as white through the decimator: measured)
        }
        mix += v.gainNoise * n;
    }
    if (v.organicOn)
        mix += v.organicGain * v.organic.next (v.organicStep, (v.held || v.sustained) && p.organic.loop);
    return mix;
}

float Synth::sourceSample (Voice& v, float envValue, float& polyMod) noexcept
{
    const auto& p = patchOf (v);
    if (! v.classicPair)
        return liveStages (v, modelPairSample (v, p, envValue, polyMod) * filterInputGain);
    const auto w1 = osc::wavesOf (p.osc1Wave);
    const auto w2 = osc::wavesOf (p.osc2Wave);

    // OSC 2 first: it is Poly Mod's source and hard sync's master.
    double wrap2 = -1.0;
    v.phase2 = osc::walk (v.phase2, v.dt2, v.width2, w2, 1.0, 0.0, v.line2, wrap2);
    v.osc2Out = v.line2.end (osc::naive (w2, v.phase2, v.width2));

    // Poly Mod: the filter envelope and OSC 2, at audio rate (amounts squared: fine near 0).
    const float pm = envValue * static_cast<float> (p.polyModEnv * p.polyModEnv) + v.osc2Out * static_cast<float> (p.polyModOsc2 * p.polyModOsc2);
    polyMod = pm;
    double dt1 = v.dt1;
    float width1 = v.width1;
    if (p.polyModToPitch1)
        dt1 = std::clamp (dt1 * static_cast<double> (math::exp2Fast (pm * polyModPitchSemitones / 12.0f)), 1.0e-9, 0.45);
    if (p.polyModToWidth1)
        width1 = std::clamp (width1 + polyModWidth * pm, 0.02f, 0.98f);

    // OSC 1, restarted by OSC 2's wrap when synced.
    double wrap1 = -1.0;
    if (p.sync && wrap2 >= 0.0)
    {
        const double before = v.phase1;
        double atReset = osc::walk (before, dt1, width1, w1, 1.0 - wrap2, wrap2, v.line1, wrap1);
        osc::syncReset (atReset, dt1, width1, w1, static_cast<float> (wrap2), v.line1);
        v.phase1 = osc::walk (0.0, dt1, width1, w1, wrap2, 0.0, v.line1, wrap1);
    }
    else
        v.phase1 = osc::walk (v.phase1, dt1, width1, w1, 1.0, 0.0, v.line1, wrap1);
    const float o1 = v.line1.end (osc::naive (w1, v.phase1, width1));

    float mix = o1 * v.gain1 + v.osc2Out * v.gain2;
    if (v.gainNoise > 0.0f)
        mix += v.gainNoise * static_cast<float> (v.noise.bipolar());
    return liveStages (v, mix * filterInputGain);
}

float Synth::liveStages (Voice& v, float x) noexcept
{

    // The voice's own recording, and what GRANULAR and KALEIDOSCOPE take from it.
    v.recording.write (x);
    if (v.grainMix > 1.0e-4f || v.cloud > 1.0e-4f)
    {
        const float grains = v.granulator.render (v.recording, v.grainStep);
        x = x * (1.0f - v.grainMix) * (1.0f - 0.5f * v.cloud) + grains * (v.grainMix + v.cloud);
    }
    if (v.doubling > 0.0f)
    {
        // KALEIDOSCOPE's doubling: a second head 12-24 ms behind, slowly moving (its delay
        // glides sample by sample towards the control rate's value).
        v.doublingDelay += 0.03 * (v.doublingTarget - v.doublingDelay);
        const double delay = v.doublingDelay;
        if (static_cast<double> (v.recording.history()) > delay + 4.0)
        {
            const double at = static_cast<double> (v.recording.now()) - delay;
            const auto i = static_cast<std::int64_t> (std::floor (at));
            const float* t = v.recording.taps (i - 1);
            const auto wts = osp::hermiteWeights (static_cast<float> (at - static_cast<double> (i)));
            const float doubled = (wts * osp::simd::Float4::load (t)).sum();
            x = (x + v.doubling * doubled) / (1.0f + 0.5f * v.doubling);
        }
    }
    if (v.saturationDrive > 1.0f)
        x = osp::fastTanh (v.saturationDrive * x) / v.saturationDrive;   // KALEIDOSCOPE's harmonic saturation
    return x;
}

void Synth::renderVoice (Voice& v, float* left, float* right, int count) noexcept
{
    const auto& p = current;
    const int fadeLength = std::max (1, static_cast<int> (stealFadeSeconds * internalRate));
    for (int done = 0; done < count && v.active;)
    {
        // The mode engine (TAPE FRAME, TOYBOX, MIRAGE) works in intervals of up to 32 samples,
        // its controls updated every 32 (as in an ANDOR OSP voice).
        int n = std::min (count - done, modeInterval);
        if (v.modeEngine != nullptr)
        {
            if (v.modeCountdown <= 0)
            {
                v.modeEngine->control (modeControl (v));
                v.modeFactor = v.modeEngine->stepFactor();
                v.modeCountdown = modeInterval;
            }
            n = std::min (n, v.modeCountdown);
        }
        // The source runs ahead of the filter by up to one interval, but never past the
        // voice's end or the end of a stolen note's fade: nothing is computed for a note
        // that is over (so a render never depends on where blocks or intervals fall).
        if (v.fade > 0)
            n = std::min (n, v.fade);
        bool ends = false;
        for (int k = 0; k < n; ++k)
        {
            const auto ks = static_cast<std::size_t> (k);
            const float env = v.env.next();
            scratchEnv[ks] = env;
            scratchSource[ks] = sourceSample (v, env, scratchPolyMod[ks]);
            scratchAmp[ks] = v.amp.next();
            if (! v.amp.isActive() && ! v.pending)
            {
                n = k + 1;
                ends = true;
                break;
            }
        }
        if (v.modeEngine != nullptr)
        {
            v.modeEngine->render (scratchSource.data(), scratchSource.data(), scratchL.data(), scratchR.data(), n);
            for (int k = 0; k < n; ++k)
                scratchSource[static_cast<std::size_t> (k)] = 0.5f * (scratchL[static_cast<std::size_t> (k)] + scratchR[static_cast<std::size_t> (k)]);
            v.modeCountdown -= n;
        }

        int used = n;
        for (int k = 0; k < n; ++k)
        {
            // The ladder: cutoff from the control-rate base, the envelope and Poly Mod.
            const auto ks = static_cast<std::size_t> (k);
            float cutoff = v.cutoffNote + scratchEnv[ks] * v.envDepth;
            if (p.polyModToCutoff)
                cutoff += scratchPolyMod[ks] * polyModCutoffSemitones;
            float in = scratchSource[ks] * v.driveGain;
            if (v.highPassTail > 0)
            {
                in = v.highPass.process (in);
                if (current.highPass < 0)
                    --v.highPassTail;
            }
            float y;
            if (v.otaMix <= 0.0f && v.otaTarget <= 0.0f)   // LADDER: the original path
                y = v.filter.process (in, cutoffG (cutoff), v.k, driveBias, driveBiasOffset) * v.makeUp;
            else
            {
                // OTA (or the crossfade between the models, about 30 ms: both run meanwhile).
                const float g = cutoffG (cutoff);
                const float ota = v.ota.process (in, v.otaCutoffOffset != 0.0f ? cutoffG (cutoff + v.otaCutoffOffset) : g, v.resonance01, current.otaVoicing == OtaVoicing::jupiter ? OtaCharacter::jupiter : OtaCharacter::juno,
                                                 current.otaTwoPole) * v.otaMakeUp;
                if (v.otaMix >= 1.0f && v.otaTarget >= 1.0f)
                    y = ota;
                else
                {
                    const float ladder = v.filter.process (in, g, v.k, driveBias, driveBiasOffset) * v.makeUp;
                    const float step = 1.0f / (0.03f * static_cast<float> (internalRate));
                    v.otaMix = std::clamp (v.otaMix + (v.otaTarget > v.otaMix ? step : -step), 0.0f, 1.0f);
                    y = ladder + v.otaMix * (ota - ladder);
                }
            }
            v.outGain += 0.004f * (v.outGainTarget - v.outGain);
            float out = y * scratchAmp[ks] * v.outGain;
            if (v.fade > 0)
                out *= static_cast<float> (v.fade) / static_cast<float> (fadeLength);
            const int at = done + k;
            left[at] += out * v.panL;
            right[at] += out * v.panR;
            if (v.fade > 0 && --v.fade == 0)
            {
                // The stolen note has faded: the waiting one starts with the next sample. (Its
                // oscillators never stopped: their phases are already where they should be.)
                v.amp.kill();
                v.env.kill();
                v.active = false;
                v.stoppedAt = stillRunning;
                startVoice (v, v.pendingChannel, v.pendingNote, v.pendingVelocity, v.pendingRetrigger);
                used = k + 1;
                break;
            }
            if (ends && k == n - 1)
            {
                v.active = false;
                v.held = v.sustained = false;
                // (`clock` is this control period's first host sample.)
                v.stoppedAt = clock + static_cast<std::uint64_t> (at / oversampling);
                if (v.modeEngine != nullptr)
                    v.modeEngine->release();
                return;
            }
        }
        done += used;
    }
}

void Synth::render (float* left, float* right, int numSamples) noexcept
{
    // A host may hand over more than it announced: render it in prepared-size pieces.
    for (int done = 0; done < numSamples;)
    {
        const int n = std::min (maxBlock, numSamples - done);
        renderChunk (left + done, right + done, n);
        done += n;
    }
}

void Synth::renderChunk (float* left, float* right, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;
    const int total = numSamples * oversampling;
    std::fill (mixL.begin(), mixL.begin() + total, 0.0f);
    std::fill (mixR.begin(), mixR.begin() + total, 0.0f);

    int pos = 0;
    while (pos < numSamples)
    {
        if (controlCountdown <= 0)
        {
            controlUpdate();
            controlCountdown = controlPeriod;
        }
        const int chunk = std::min (controlCountdown, numSamples - pos);
        const int from = pos * oversampling, count = chunk * oversampling;
        for (auto& v : voices)
            if (v.active)
                renderVoice (v, mixL.data() + from, mixR.data() + from, count);
        controlCountdown -= chunk;
        pos += chunk;
        blockOffset += chunk;
        clock += static_cast<std::uint64_t> (chunk);
    }

    // Down to the host rate.
    if (oversampling == 4)
    {
        for (int i = 0; i < 2 * numSamples; ++i)
        {
            halfL[static_cast<std::size_t> (i)] = stage1L.process (mixL[static_cast<std::size_t> (2 * i)], mixL[static_cast<std::size_t> (2 * i + 1)]);
            halfR[static_cast<std::size_t> (i)] = stage1R.process (mixR[static_cast<std::size_t> (2 * i)], mixR[static_cast<std::size_t> (2 * i + 1)]);
        }
    }
    const float* srcL = oversampling == 4 ? halfL.data() : mixL.data();
    const float* srcR = oversampling == 4 ? halfR.data() : mixR.data();
    for (int i = 0; i < numSamples; ++i)
    {
        if (oversampling == 1)
        {
            left[i] = srcL[i];
            right[i] = srcR[i];
        }
        else
        {
            left[i] = stage2L.process (srcL[2 * i], srcL[2 * i + 1]);
            right[i] = stage2R.process (srcR[2 * i], srcR[2 * i + 1]);
        }
    }

    // The chorus (state version 4; off: the buffers untouched), on the voices' sum.
    chorus.setMode (current.chorusMode);
    chorus.setNoise (current.chorusNoise, current.seed);
    if (chorus.isActive())
        chorus.process (left, right, numSamples);

    // KALEIDOSCOPE's resonators and formants on the mix (ANDOR OSP's stage), then ANDOR OSP's
    // DRIVE, MOVEMENT, ECHO and SPACE, then a DC blocker and the output guard.
    if (kaleidoscopeStage.active())
        for (int i = 0; i < numSamples; ++i)
        {
            if (--stageCountdown <= 0)
            {
                kaleidoscopeStage.update();
                stageCountdown = osp::ReimaginedStage::controlInterval;
            }
            kaleidoscopeStage.process (left[i], right[i]);
        }
    effects.process (left, right, numSamples);
    const float out = math::dbToGain (current.outputDb);
    for (int i = 0; i < numSamples; ++i)
    {
        const float l = left[i], r = right[i];
        const float yl = l - dcL + dcCoef * dcStateL;
        const float yr = r - dcR + dcCoef * dcStateR;
        dcL = l;
        dcR = r;
        dcStateL = yl;
        dcStateR = yr;
        left[i] = softGuard (yl * out);
        right[i] = softGuard (yr * out);
    }

    int count = 0;
    for (const auto& v : voices)
        count += v.active ? 1 : 0;
    sounding.store (count, std::memory_order_relaxed);
    effects.setVoicesActive (count > 0);
}

Synth::VoiceView Synth::voiceView (int slot) const noexcept
{
    const auto& v = voices[static_cast<std::size_t> (std::clamp (slot, 0, maxVoices - 1))];
    return { v.active, v.held || v.sustained, v.note, v.amp.value() };
}

} // namespace andor::analog
