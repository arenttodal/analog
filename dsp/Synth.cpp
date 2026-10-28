#include "Synth.h"

#include "AnalogMath.h"
#include "Oscillator.h"

#include "engine/InstrumentEngine.h"

#include <algorithm>
#include <cmath>

namespace andor::analog
{

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

    for (int s = 0; s < maxVoices; ++s)
    {
        auto& v = voices[static_cast<std::size_t> (s)];
        v.slot = s;
        v.amp.setSampleRate (internalRate);
        v.env.setSampleRate (internalRate);
    }

    effects.prepare (hostRate, maxBlock, 1);
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
    lfo.reset (current.seed);
    lfoValue = 0.0f;
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
        // Free-running oscillators start where the slot's calibration puts them.
        osp::Prng start (osp::Prng::deriveSeed (current.seed, static_cast<std::uint64_t> (v.slot), 0x5048u));
        v.phase1 = start.nextDouble();
        v.phase2 = start.nextDouble();
        v.noise.reseed (osp::Prng::deriveSeed (current.seed, static_cast<std::uint64_t> (v.slot), 0x4e4fu));
    }
    for (auto* d : { &stage1L, &stage1R, &stage2L, &stage2R })
        d->reset();
    osp::Macros macros;
    macros.life = macros.motion = macros.reimagined = macros.drive = 0.0;
    macros.space = current.space;
    macros.echo = current.echo;
    effects.setShaping (current.effects);
    effects.setMacros (macros);
    effects.reset();
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
    if (current.seed != driftSeed)
    {
        driftSeed = current.seed;
        rebuildDrift();
    }
    osp::Macros macros;
    macros.life = macros.motion = macros.reimagined = macros.drive = 0.0;
    macros.space = std::clamp (current.space, 0.0, 1.0);
    macros.echo = std::clamp (current.echo, 0.0, 1.0);
    effects.setShaping (current.effects);
    effects.setMacros (macros);
}

void Synth::setTransport (const osp::HostTiming& t) noexcept
{
    timing = t;
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

void Synth::startVoice (Voice& v, int channel, int note, float velocity, bool retrigger) noexcept
{
    const bool wasActive = v.active;
    v.channel = channel;
    v.note = note;
    v.velocity = std::clamp (velocity, 0.0f, 1.0f);
    v.order = ++noteCounter;
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
        equalPower (static_cast<float> (current.panPercent / 100.0) + static_cast<float> (current.stereoSpread) * slotPan[static_cast<std::size_t> (v.slot)], v.panL, v.panR);
    }
    if (! wasActive)
    {
        v.filter.reset();
        v.line1.reset();
        v.line2.reset();
        v.osc2Out = 0.0f;
        v.outGain = 0.0f;
    }
    updateVoiceControl (v, lfoValue, static_cast<float> (std::clamp (current.movement + wheel * (1.0 - current.movement), 0.0, 1.0)));
    v.outGain = wasActive ? v.outGain : v.outGainTarget;
    if (current.phaseMode == PhaseMode::reset)
    {
        if (retrigger)
        {
            v.phase1 = 0.0;
            v.phase2 = 0.0;
        }
    }
    else if (! wasActive && v.hasPlayed)
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
    }
    v.active = true;
    v.hasPlayed = true;
}

void Synth::releaseVoice (Voice& v) noexcept
{
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
        equalPower (static_cast<float> (current.panPercent / 100.0 + current.stereoSpread * spreadPosition * (current.unison ? 1.0 : 0.0)), v.panL, v.panR);
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
void Synth::setModWheel (float value) noexcept { wheel = std::clamp (value, 0.0f, 1.0f); }

void Synth::setSustainPedal (bool down) noexcept
{
    pedal = down;
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
    wheel = 0.0f;
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
        case 0xb0:
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

void Synth::updateVoiceControl (Voice& v, float lfoNow, float depth) noexcept
{
    const auto& p = current;
    const auto& d = drift[static_cast<std::size_t> (v.slot)];
    const auto a = static_cast<float> (std::clamp (p.analogAmount, 0.0, 1.0));
    const float lfoD = lfoNow * depth;

    // Pitch: the note (gliding), TUNE and FINE, the wheel, unison, the slot's tolerance and
    // its wander (ANALOG: +-10 cents fixed and +-3 cents moving at 100 %).
    const double performed = p.tuneSemitones + p.fineTuneCents / 100.0 + bend * p.bendRangeSemitones;
    const double common = v.pitch + v.unisonOffset + performed;
    const double note1 = common + 12.0 * p.osc1Octave + p.osc1Coarse + a * (0.10 * d.pitch1 + 0.03 * d.wander) + lfoD * p.lfoPitch1;
    const double base2 = p.osc2KeyTrack ? common : 60.0 + v.unisonOffset + performed;
    const double note2 = base2 + 12.0 * p.osc2Octave + p.osc2Coarse + p.osc2FineCents / 100.0 + a * (0.10 * d.pitch2 - 0.03 * d.wander)
                         + lfoD * p.lfoPitch2 - (p.osc2LowFrequency ? 12.0 * lowFrequencyOctaves : 0.0);
    v.dt1 = std::clamp (math::noteToHz (note1) / internalRate, 1.0e-9, 0.45);
    v.dt2 = std::clamp (math::noteToHz (note2) / internalRate, 1.0e-9, 0.45);
    v.width1 = static_cast<float> (std::clamp (p.osc1Width + 0.04 * a * d.width1 + lfoD * p.lfoWidth1, 0.02, 0.98));
    v.width2 = static_cast<float> (std::clamp (p.osc2Width + 0.04 * a * d.width2 + lfoD * p.lfoWidth2, 0.02, 0.98));

    // Mixer: BLEND crossfades (equal power) between the two oscillators' own levels.
    const float theta = blendSmoothed * static_cast<float> (math::pi) * 0.5f;
    const float levelDrift = 0.17f * a;   // +-1.5 dB at 100 %
    v.gain1 = static_cast<float> (p.osc1Level) * std::min (1.0f, 1.41421356f * std::cos (theta)) * (1.0f + levelDrift * d.level1);
    v.gain2 = static_cast<float> (p.osc2Level) * std::min (1.0f, 1.41421356f * std::sin (theta)) * (1.0f + levelDrift * d.level2);
    v.gainNoise = static_cast<float> (p.noiseLevel) * 0.8f * (1.0f + levelDrift * d.noise);

    // Filter: TONE, keyboard tracking around C4, the slot's tolerance (+-1 semitone at 100 %)
    // and wander, the LFO; the envelope (scaled by velocity) is added per sample.
    v.cutoffNote = toneNote + static_cast<float> (p.keyTrack * (v.pitch - 60.0)) + a * (1.0f * d.cutoff + 0.2f * d.wander)
                   + lfoD * static_cast<float> (p.lfoCutoff);
    const double velocityScale = 1.0 - p.velocityToFilter + p.velocityToFilter * v.velocity;
    v.envDepth = static_cast<float> (p.envDepthSemitones * velocityScale);
    v.k = std::clamp (4.15f * resonanceSmoothed * (1.0f + 0.06f * a * d.resonance), 0.0f, 4.2f);
    v.makeUp = (1.0f + 0.35f * v.k) / std::sqrt (driveGain) * math::dbToGain (p.driveTrimDb);

    // Envelopes: the slot's tolerance on the times (+-20 % at 100 %).
    const double t = 0.2 * a;
    v.env.setTimes (p.filterAttack * (1.0 + t * d.attack), p.filterDecay * (1.0 + t * d.decay), p.filterSustain, p.filterRelease * (1.0 + t * d.release));
    v.amp.setTimes (p.ampAttack * (1.0 + t * d.attack), p.ampDecay * (1.0 + t * d.decay), p.ampSustain, p.ampRelease * (1.0 + t * d.release));

    // Level: velocity (VELOCITY RANGE dB between the hardest and softest note), LEVEL, MUTE.
    const double soft = 1.0 - v.velocity;
    const float velocityGain = math::dbToGain (-p.velocityRangeDb * soft * soft);
    // LEVEL at its bottom (ANDOR OSP's -48 dB) is silence.
    const float level = p.levelDb <= -47.99 ? 0.0f : math::dbToGain (p.levelDb);
    v.outGainTarget = p.mute ? 0.0f : velocityGain * level * voiceGain;
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
    driveSmoothed += c * (static_cast<float> (p.drive) - driveSmoothed);
    blendSmoothed += c * (static_cast<float> (p.blend) - blendSmoothed);
    smoothingPrimed = true;
    // DRIVE: up to +24 dB into the saturator (about half of it taken back after the filter,
    // per voice); BIAS shifts the saturator off centre for even harmonics.
    driveGain = std::exp2 (4.0f * driveSmoothed);
    driveBias = 0.6f * static_cast<float> (std::clamp (p.driveBias, 0.0, 1.0));
    driveBiasOffset = math::tanhFast (driveBias);

    // The LFO: locked to the host's beat while it plays (SYNC), else free.
    if (p.lfoSync && timing.valid && timing.playing)
    {
        const double ppq = ppqAtBlock + static_cast<double> (blockOffset) / hostRate * timing.bpm / 60.0;
        lfoValue = lfo.lockTo (p.lfoWave, ppq, lfoDivisionQuarters (p.lfoDivision));
    }
    else
    {
        const double rate = p.lfoSync ? std::max (timing.bpm, 1.0) / 60.0 / lfoDivisionQuarters (p.lfoDivision) : p.lfoRateHz;
        lfoValue = lfo.advance (p.lfoWave, rate, seconds);
    }
    const auto depth = static_cast<float> (std::clamp (p.movement + wheel * (1.0 - p.movement), 0.0, 1.0));

    // Glide: the time to cover ~95 % of the interval.
    const double glideCoef = p.glideSeconds > 0.0 ? 1.0 - std::exp (-seconds * 3.0 / p.glideSeconds) : 1.0;
    for (auto& d : drift)
        updateWander (d);
    for (auto& v : voices)
    {
        if (! v.active && ! v.pending)
            continue;
        v.pitch += (v.pitchTarget - v.pitch) * glideCoef;
        updateVoiceControl (v, lfoValue, depth);
    }
}

//==============================================================================
// Audio

float Synth::renderVoiceSample (Voice& v) noexcept
{
    const auto& p = current;
    const auto w1 = osc::wavesOf (p.osc1Wave);
    const auto w2 = osc::wavesOf (p.osc2Wave);
    const float envValue = v.env.next();

    // OSC 2 first: it is Poly Mod's source and hard sync's master.
    double wrap2 = -1.0;
    v.phase2 = osc::walk (v.phase2, v.dt2, v.width2, w2, 1.0, 0.0, v.line2, wrap2);
    v.osc2Out = v.line2.end (osc::naive (w2, v.phase2, v.width2));

    // Poly Mod: the filter envelope and OSC 2, at audio rate (amounts squared: fine near 0).
    const float pm = envValue * static_cast<float> (p.polyModEnv * p.polyModEnv) + v.osc2Out * static_cast<float> (p.polyModOsc2 * p.polyModOsc2);
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

    // The ladder: cutoff from the control-rate base, the envelope and Poly Mod.
    float cutoff = v.cutoffNote + envValue * v.envDepth;
    if (p.polyModToCutoff)
        cutoff += pm * polyModCutoffSemitones;
    const float g = cutoffG (cutoff);

    const float y = v.filter.process (mix * filterInputGain * driveGain, g, v.k, driveBias, driveBiasOffset);
    v.outGain += 0.004f * (v.outGainTarget - v.outGain);
    float out = y * v.makeUp * v.amp.next() * v.outGain;
    if (v.fade > 0)
        out *= static_cast<float> (v.fade) / static_cast<float> (std::max (1, static_cast<int> (stealFadeSeconds * internalRate)));
    return out;
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
        {
            if (! v.active)
                continue;
            float* l = mixL.data() + from;
            float* r = mixR.data() + from;
            for (int i = 0; i < count; ++i)
            {
                const float s = renderVoiceSample (v);
                l[i] += s * v.panL;
                r[i] += s * v.panR;
                if (v.fade > 0 && --v.fade == 0)
                {
                    // The stolen note has faded: the waiting one starts here.
                    v.amp.kill();
                    v.env.kill();
                    v.active = false;
                    startVoice (v, v.pendingChannel, v.pendingNote, v.pendingVelocity, v.pendingRetrigger);
                }
                if (! v.amp.isActive() && ! v.pending)
                {
                    v.active = false;
                    v.held = v.sustained = false;
                    v.stoppedAt = clock + static_cast<std::uint64_t> ((from + i) / oversampling);
                    break;
                }
            }
        }
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

    // SPACE and ECHO (OSP's own), then a DC blocker and the output guard.
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
}

Synth::VoiceView Synth::voiceView (int slot) const noexcept
{
    const auto& v = voices[static_cast<std::size_t> (std::clamp (slot, 0, maxVoices - 1))];
    return { v.active, v.held || v.sustained, v.note, v.amp.value() };
}

} // namespace andor::analog
