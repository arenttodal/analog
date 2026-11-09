// RETRO (V2): one aging machine - TAPE, VINYL, DIGITAL 80s / 90s and CODEC - original DSP.
// Every stage is designed here, not copied: an antiderivative anti-aliased (ADAA) saturation,
// RBJ biquads for the medium's response, a Hermite-read modulated delay for wow and flutter,
// seeded Poisson events for dropouts and clicks, sample-and-hold converters, and an STFT
// (Hann, hop N/4, overlap-add) for the codec. The latency is one codec frame in every mode.
#include "Effects.h"

#include <cstring>

namespace andor::analog::fx
{

using namespace dsp;

RetroDerived deriveRetro (const RetroSettings& s) noexcept
{
    RetroDerived d {};
    const float c = std::clamp (s.character, 0.0f, 1.0f), w = std::clamp (s.wear, 0.0f, 1.0f);
    d.saturation = std::clamp (s.saturation + (1.0f - s.saturation) * 0.7f * c, 0.0f, 1.0f);
    d.tiltDb = std::clamp (s.toneDb - (s.mode == RetroSettings::tape ? 2.5f : (s.mode == RetroSettings::vinyl ? 2.0f : 0.0f)) * c, -14.0f, 12.0f);
    d.compAmount = s.compAmount > 0.0f ? std::min (1.0f, s.compAmount + 0.3f * c) : 0.0f;   // (never switched on by CHARACTER)
    d.wowDepth = std::clamp (s.wowDepth + (1.0f - s.wowDepth) * 0.5f * w, 0.0f, 1.0f);
    d.flutterDepth = std::clamp (s.flutterDepth + (1.0f - s.flutterDepth) * 0.4f * w, 0.0f, 1.0f);
    d.dropoutRate = s.dropoutRate + std::max (0.0f, w - 0.5f) * 6.0f;
    d.dropoutDepth = std::max (s.dropoutDepth, w > 0.5f ? (w - 0.5f) * 1.2f : 0.0f);
    d.lowHz = std::clamp (s.lowHz + 60.0f * w, 20.0f, 900.0f);
    d.highHz = std::clamp (s.highHz * std::pow (0.35f, w), 1500.0f, 20000.0f);
    d.bits = std::clamp (s.bits - 4.0f * c, 3.0f, 16.0f);
    d.rateKHz = std::clamp (s.rateKHz * (1.0f - 0.4f * w), 2.0f, 48.0f);
    // CODEC: CHARACTER deepens the loss, WEAR quickens the packets' changes.
    d.codecLoss = std::clamp (s.codecLoss + (1.0f - s.codecLoss) * 0.35f * c, 0.0f, 1.0f);
    d.codecSpeed = std::clamp (s.codecSpeed + (1.0f - s.codecSpeed) * 0.5f * w, 0.0f, 1.0f);
    return d;
}

namespace
{
    constexpr float twoPi = 6.283185307179586f;

    /** The medium's linear response: head bump (TAPE), band (high-pass, low-pass), tilt. */
    struct Response
    {
        Biquad bump, hp, lp, lowShelf, highShelf;
        void design (const RetroSettings& s, const RetroDerived& d, double rate) noexcept
        {
            const double nyq = 0.45 * rate;
            const bool reel = s.tapeModel == 1;
            if (s.mode == RetroSettings::tape)
                bump.design (Biquad::Shape::peak, rate, reel ? 45.0 : 62.0, 0.9, 7.0 * s.headBump);
            else if (s.mode == RetroSettings::vinyl)
                bump.design (Biquad::Shape::lowShelf, rate, 160.0, 0.707, 1.5 * s.character);   // (a little softness)
            else
                bump.identity();
            hp.design (Biquad::Shape::highPass, rate, std::max (s.mode == RetroSettings::vinyl ? 28.0 : 15.0, static_cast<double> (d.lowHz)), 0.707, 0.0);
            double high = d.highHz;
            if (s.mode == RetroSettings::tape && ! reel)
                high *= 0.78;   // (a cassette's narrower band)
            if (s.mode == RetroSettings::vinyl)
                high = std::min (high, 14000.0);
            if (s.mode == RetroSettings::digital)
                lp.identity();
            else
                lp.design (Biquad::Shape::lowPass, rate, std::min (nyq, high), s.mode == RetroSettings::vinyl ? 0.6 : 0.707, 0.0);
            lowShelf.design (Biquad::Shape::lowShelf, rate, 600.0, 0.707, -0.5 * d.tiltDb);
            highShelf.design (Biquad::Shape::highShelf, rate, 1800.0, 0.707, 0.5 * d.tiltDb);
        }
        double magnitude (double rate, double hz) const noexcept
        {
            return bump.magnitude (rate, hz) * hp.magnitude (rate, hz) * lp.magnitude (rate, hz) * lowShelf.magnitude (rate, hz) * highShelf.magnitude (rate, hz);
        }
        float process (float x) noexcept { return highShelf.process (lowShelf.process (lp.process (hp.process (bump.process (x))))); }
        void reset() noexcept
        {
            for (auto* b : { &bump, &hp, &lp, &lowShelf, &highShelf })
                b->reset();
        }
    };

    /** log cosh, overflow-safe (the antiderivative of tanh). */
    inline double logCosh (double u) noexcept
    {
        const double a = std::abs (u);
        return a > 12.0 ? a - 0.6931471805599453 : std::log (std::cosh (u));
    }
}

//==============================================================================
struct Retro::State
{
    double rate = 48000.0;
    int frame = 1024, hop = 256, latency = 1024, maxBlock = 512;
    int baseDelay = 288;   ///< the transport's centre delay (samples)

    // Shared.
    Glide inGain;
    float compEnvL = 0.0f, compEnvR = 0.0f, scLpL = 0.0f, scLpR = 0.0f;
    std::vector<float> inL, inR, outL, outR, oldL, oldR, envAt;   // (envAt: the input's envelope, per sample)
    int mode = 0, fadeFrom = -1, fadePos = 0, fadeLength = 960;
    RetroDerived d {};

    struct Channel
    {
        Response response;
        float split = 0.0f, adaaPrev = 0.0f, dcX = 0.0f, dcY = 0.0f;
        DelayLine transport, pad;
        float dropLp = 0.0f, lowShared = 0.0f;   // (lowShared: STEREO PHASE's low band, one transport)
        float n1 = 0.0f, n2 = 0.0f, nPrev = 0.0f;
        float clickEnv = 0.0f, popEnv = 0.0f, popLp = 0.0f;
        std::array<Biquad, 4> aa, rc;   // (CLEAN: the converter's elliptic filters, in and out)
        float held = 0.0f, error = 0.0f;
        std::vector<float> fifo, ola;
        DelayLine dry;
    } ch[2];

    // Transport.
    double wowPhase = 0.0, flutterPhase = 0.0, flutter2Phase = 0.0;
    float driftTarget = 0.0f, drift = 0.0f, lastDelayL = 0.0f;
    int driftCount = 0;
    // Dropouts.
    double dropIn = 1.0e12;
    int dropPos = 0, dropLength = 0;
    float dropDepth = 0.0f;
    // Vinyl events.
    double clickIn = 1.0e12, popIn = 1.0e12;
    // Noise and idle.
    float inputEnv = 0.0f, idleSeconds = 0.0f, idleGain = 1.0f;
    // Digital.
    double holdPhase = 0.0;
    float motionTarget = 0.0f, motion = 0.0f, motionTarget2 = 0.0f, motion2 = 0.0f;
    int motionCount = 0;
    // Codec.
    Fft fft;
    std::vector<float> window;
    std::vector<std::complex<float>> spec[2], held[2], work;
    std::vector<float> mag, smooth, prefix, mask[2];
    std::vector<float> jitter;
    int fifoPos = 0, readPos = 0, sinceFrame = 0, frameCount = 0, burst = 0, burstKind = 0;
    bool heldValid = false;
    DelayLine ambience[4];
    float ambienceFb = 0.0f;
    // Analysis (the graph's spectrum).
    Fft analysisFft;
    std::vector<float> analysisBuf;
    std::vector<std::complex<float>> analysisWork;
    int analysisPos = 0;
    double tickRemainder = 0.0;

    osp::Prng rng, noiseRng, eventRng, codecRng;   // (one stream each: the same samples whatever the block size)
};

namespace
{
    // The converter's filters (DIGITAL CLEAN): an 8th-order elliptic low-pass (0.1 dB ripple,
    // 65 dB stop band from 1.226 x the edge), the edge at 0.40 x the effective rate so the
    // stop band starts below its Nyquist. Analogue prototype (edge 1 rad/s) as four sections
    // (s^2 + wz^2) / (s^2 + b1 s + b0), designed offline; bilinear with the edge prewarped,
    // each section at unity DC.
    constexpr std::array<std::array<double, 3>, 4> ellipticPrototype { {
        { 1.535390694714, 0.822851517380, 0.251251161023 },
        { 1.884477373625, 0.527164820573, 0.590926982479 },
        { 3.475505419693, 0.245002381558, 0.897305627984 },
        { 24.222227988352, 0.067534555266, 1.040062886581 },
    } };

    void designConverter (std::array<Biquad, 4>& sections, float rateKHz, double rate) noexcept
    {
        const double edge = std::min (0.40 * 1000.0 * static_cast<double> (rateKHz), 0.45 * rate);
        const double c = 1.0 / std::tan (3.141592653589793 * edge / rate), c2 = c * c;
        for (std::size_t k = 0; k < sections.size(); ++k)
        {
            const auto [wz2, b1, b0] = ellipticPrototype[k];
            const double a0 = c2 + b1 * c + b0, g = b0 / wz2;
            auto& q = sections[k];
            q.b0 = g * (c2 + wz2) / a0;
            q.b1 = g * (2.0 * wz2 - 2.0 * c2) / a0;
            q.b2 = q.b0;
            q.a1 = (2.0 * b0 - 2.0 * c2) / a0;
            q.a2 = (c2 - b1 * c + b0) / a0;
        }
    }
}

Retro::Retro() : st (std::make_unique<State>()) {}
Retro::~Retro() = default;

int Retro::frameFor (double rate) noexcept
{
    int n = 1024;
    while (n < static_cast<int> (1024.0 * rate / 48000.0 + 0.5))
        n <<= 1;
    return n;
}

double Retro::responseDb (const RetroSettings& s, double rate, double hz) noexcept
{
    Response r;
    r.design (s, deriveRetro (s), rate);
    return 20.0 * std::log10 (std::max (1.0e-9, r.magnitude (rate, hz)));
}

void Retro::prepareEffect()
{
    auto& s = *st;
    s.rate = rate;
    s.maxBlock = maxBlock;
    s.frame = frameFor (rate);
    s.hop = s.frame / 4;
    s.latency = s.frame;
    s.baseDelay = static_cast<int> (0.006 * rate);
    s.fadeLength = std::max (1, static_cast<int> (0.02 * rate));
    for (auto* v : { &s.inL, &s.inR, &s.outL, &s.outR, &s.oldL, &s.oldR, &s.envAt })
        v->assign (static_cast<std::size_t> (maxBlock), 0.0f);
    for (auto& c : s.ch)
    {
        c.transport.prepare (s.baseDelay * 2 + 64);
        c.pad.prepare (s.latency + 8);
        c.fifo.assign (static_cast<std::size_t> (s.frame), 0.0f);
        c.ola.assign (static_cast<std::size_t> (s.frame), 0.0f);
        c.dry.prepare (s.latency + 8);
    }
    s.fft.prepare (s.frame);
    s.window.resize (static_cast<std::size_t> (s.frame));
    for (int i = 0; i < s.frame; ++i)
        s.window[static_cast<std::size_t> (i)] = 0.5f - 0.5f * std::cos (twoPi * static_cast<float> (i) / static_cast<float> (s.frame));
    for (int c = 0; c < 2; ++c)
    {
        s.spec[c].assign (static_cast<std::size_t> (s.frame), {});
        s.held[c].assign (static_cast<std::size_t> (s.frame), {});
        s.mask[c].assign (static_cast<std::size_t> (s.frame / 2 + 1), 1.0f);
    }
    s.work.assign (static_cast<std::size_t> (s.frame), {});
    s.mag.assign (static_cast<std::size_t> (s.frame / 2 + 1), 0.0f);
    s.smooth.assign (s.mag.size(), 0.0f);
    s.prefix.assign (s.mag.size() + 1, 0.0f);
    s.jitter.assign (s.mag.size(), 0.0f);
    const std::array<double, 4> ms { 0.0051, 0.0073, 0.0117, 0.029 };
    for (int i = 0; i < 4; ++i)
        s.ambience[i].prepare (static_cast<int> (ms[static_cast<std::size_t> (i)] * rate) + 8);
    s.analysisFft.prepare (s.frame);
    s.analysisBuf.assign (static_cast<std::size_t> (s.frame), 0.0f);
    s.analysisWork.assign (static_cast<std::size_t> (s.frame), {});
    s.inGain.prepare (rate, 0.02);
    setLatency (s.latency);
    ticksPerSecond.store (static_cast<float> (rate / tickSamples));
}

void Retro::resetEffect() noexcept
{
    auto& s = *st;
    // Deterministic: every random stream restarts from the instance's seed.
    const auto base = osp::Prng::deriveSeed (0x5245545246ull, static_cast<std::uint64_t> (p.seed), static_cast<std::uint64_t> (slot));
    s.rng.reseed (base);
    s.noiseRng.reseed (osp::Prng::deriveSeed (base, 1));
    s.codecRng.reseed (osp::Prng::deriveSeed (base, 2));
    s.eventRng.reseed (osp::Prng::deriveSeed (base, 3));
    s.inGain.jump (dbToGain (p.inputDb));
    s.compEnvL = s.compEnvR = s.scLpL = s.scLpR = 0.0f;
    for (auto& c : s.ch)
    {
        c.response.reset();
        c.split = c.adaaPrev = c.dcX = c.dcY = c.dropLp = c.lowShared = 0.0f;
        c.n1 = c.n2 = c.nPrev = c.clickEnv = c.popEnv = c.popLp = 0.0f;
        c.transport.reset();
        c.pad.reset();
        for (auto& b : c.aa)
            b.reset();
        for (auto& b : c.rc)
            b.reset();
        c.held = c.error = 0.0f;
        std::fill (c.fifo.begin(), c.fifo.end(), 0.0f);
        std::fill (c.ola.begin(), c.ola.end(), 0.0f);
        c.dry.reset();
    }
    s.wowPhase = s.flutterPhase = s.flutter2Phase = 0.0;
    s.driftTarget = s.drift = 0.0f;
    s.lastDelayL = static_cast<float> (s.baseDelay);
    s.driftCount = 0;
    s.dropIn = s.clickIn = s.popIn = 1.0e12;
    s.dropPos = s.dropLength = 0;
    s.inputEnv = s.idleSeconds = 0.0f;
    s.idleGain = 1.0f;
    s.holdPhase = 0.0;
    s.motion = s.motionTarget = s.motion2 = s.motionTarget2 = 0.0f;
    s.motionCount = 0;
    s.fifoPos = s.readPos = s.sinceFrame = s.frameCount = s.burst = 0;
    s.heldValid = false;
    for (auto& a : s.ambience)
        a.reset();
    s.ambienceFb = 0.0f;
    std::fill (s.analysisBuf.begin(), s.analysisBuf.end(), 0.0f);
    s.analysisPos = 0;
    s.mode = personality (p);
    s.fadeFrom = -1;
    for (auto& v : spectrumDb)
        v.store (-120.0f, std::memory_order_relaxed);
    design();
}

void Retro::design() noexcept
{
    auto& s = *st;
    s.d = deriveRetro (p);
    for (auto& c : s.ch)
    {
        c.response.design (p, s.d, s.rate);
        designConverter (c.aa, s.d.rateKHz, s.rate);
        c.rc = c.aa;
    }
}

void Retro::set (const Settings& all, int whichSlot) noexcept
{
    const auto& next = all.retro[static_cast<std::size_t> (whichSlot)];
    const bool reseed = next.seed != p.seed || whichSlot != slot;
    const int before = personality (p);
    p = next;
    slot = whichSlot;
    setCommon (p);
    st->inGain.set (dbToGain (std::clamp (p.inputDb, -18.0f, 18.0f)));
    if (reseed)
    {
        const auto base = osp::Prng::deriveSeed (0x5245545246ull, static_cast<std::uint64_t> (p.seed), static_cast<std::uint64_t> (slot));
        st->rng.reseed (base);
        st->noiseRng.reseed (osp::Prng::deriveSeed (base, 1));
        st->codecRng.reseed (osp::Prng::deriveSeed (base, 2));
        st->eventRng.reseed (osp::Prng::deriveSeed (base, 3));
    }
    // The filters keep their state; only their coefficients move (cheap: a few per block).
    auto keepState = [] (Biquad& b, const Biquad& designed) {
        b.b0 = designed.b0;
        b.b1 = designed.b1;
        b.b2 = designed.b2;
        b.a1 = designed.a1;
        b.a2 = designed.a2;
    };
    const auto d = deriveRetro (p);
    st->d = d;
    for (auto& c : st->ch)
    {
        Response r;
        r.design (p, d, st->rate);
        keepState (c.response.bump, r.bump);
        keepState (c.response.hp, r.hp);
        keepState (c.response.lp, r.lp);
        keepState (c.response.lowShelf, r.lowShelf);
        keepState (c.response.highShelf, r.highShelf);
        std::array<Biquad, 4> conv;
        designConverter (conv, d.rateKHz, st->rate);
        for (std::size_t k = 0; k < conv.size(); ++k)
        {
            keepState (c.aa[k], conv[k]);
            keepState (c.rc[k], conv[k]);
        }
    }
    const int now = personality (p);
    if (now != before)
    {
        // A new personality: crossfade 20 ms from the old one; the new one starts clean.
        auto& s = *st;
        s.fadeFrom = s.mode;
        s.mode = now;
        s.fadePos = s.fadeLength;
        for (auto& c : s.ch)
        {
            c.response.reset();
            c.transport.reset();
            c.pad.reset();
            c.adaaPrev = c.split = c.dcX = c.dcY = 0.0f;
            for (auto& b : c.aa)
                b.reset();
            for (auto& b : c.rc)
                b.reset();
        }
        s.holdPhase = 0.0;
    }
}

void Retro::event (int kind) noexcept
{
    const int h = eventHead.load (std::memory_order_relaxed);
    eventTicks[static_cast<std::size_t> (h)].store (ticks.load (std::memory_order_relaxed), std::memory_order_relaxed);
    eventKinds[static_cast<std::size_t> (h)].store (kind, std::memory_order_relaxed);
    eventHead.store ((h + 1) % static_cast<int> (eventTicks.size()), std::memory_order_release);
}

//==============================================================================
void Retro::processWet (float* left, float* right, int n) noexcept
{
    auto& s = *st;
    // Input gain, the input's envelope (noise follow, idle), the optional compressor.
    const float envRelease = std::exp (-1.0f / (0.05f * static_cast<float> (s.rate)));
    const float comp = s.d.compAmount;
    const float threshold = -6.0f - 30.0f * comp, ratio = 1.0f + 7.0f * comp;
    const float makeup = comp > 0.0f ? 0.5f * (-threshold) * (1.0f - 1.0f / ratio) * comp : 0.0f;
    const float attack = std::exp (-1.0f / (0.005f * std::clamp (p.compAttack, 0.25f, 4.0f) * static_cast<float> (s.rate)));
    const float release = std::exp (-1.0f / (0.15f * std::clamp (p.compRelease, 0.25f, 8.0f) * static_cast<float> (s.rate)));
    const float scA = 1.0f - std::exp (-twoPi * 1000.0f / static_cast<float> (s.rate));
    const float tilt = 0.9f * std::clamp (p.compTilt, -1.0f, 1.0f);
    for (int i = 0; i < n; ++i)
    {
        const float g = s.inGain.next();
        float l = left[i] * g, r = right[i] * g;
        const float peak = std::max (std::abs (l), std::abs (r));
        s.inputEnv = peak > s.inputEnv ? peak : s.inputEnv * envRelease;
        s.envAt[static_cast<std::size_t> (i)] = s.inputEnv;
        if (comp > 0.0f)
        {
            s.scLpL += scA * (l - s.scLpL);
            s.scLpR += scA * (r - s.scLpR);
            const float vl = s.scLpL * (1.0f - tilt) + (l - s.scLpL) * (1.0f + tilt);
            const float vr = s.scLpR * (1.0f - tilt) + (r - s.scLpR) * (1.0f + tilt);
            auto follow = [attack, release] (float& env, float v) {
                const float a = std::abs (v);
                env = a > env ? attack * env + (1.0f - attack) * a : release * env + (1.0f - release) * a;
            };
            auto gainFor = [threshold, ratio, makeup] (float env) {
                const float db = gainToDb (env), over = db - threshold;
                float gr = 0.0f;
                if (over > 3.0f)
                    gr = over * (1.0f - 1.0f / ratio);
                else if (over > -3.0f)
                    gr = (1.0f - 1.0f / ratio) * (over + 3.0f) * (over + 3.0f) / 12.0f;   // (a 6 dB soft knee)
                return dbToGain (makeup - gr);
            };
            if (p.compLink)
            {
                follow (s.compEnvL, std::max (std::abs (vl), std::abs (vr)));
                const float k = gainFor (s.compEnvL);
                l *= k;
                r *= k;
            }
            else
            {
                follow (s.compEnvL, vl);
                follow (s.compEnvR, vr);
                l *= gainFor (s.compEnvL);
                r *= gainFor (s.compEnvR);
            }
        }
        s.inL[static_cast<std::size_t> (i)] = l;
        s.inR[static_cast<std::size_t> (i)] = r;
    }

    runMode (s.mode, s.inL.data(), s.inR.data(), s.outL.data(), s.outR.data(), n);
    if (s.fadeFrom >= 0)
    {
        runMode (s.fadeFrom, s.inL.data(), s.inR.data(), s.oldL.data(), s.oldR.data(), n);
        for (int i = 0; i < n && s.fadePos > 0; ++i, --s.fadePos)
        {
            const float a = 1.0f - static_cast<float> (s.fadePos) / static_cast<float> (s.fadeLength);
            const auto k = static_cast<std::size_t> (i);
            s.outL[k] = s.oldL[k] + a * (s.outL[k] - s.oldL[k]);
            s.outR[k] = s.oldR[k] + a * (s.outR[k] - s.oldR[k]);
        }
        if (s.fadePos <= 0)
            s.fadeFrom = -1;
    }
    // Noise (every personality): the medium's own, under the music. Auto-mute: 250 ms of
    // silence at the input fades it out over 150 ms (back in 50 ms); per sample, so a bounce
    // is the same whatever the host's block size.
    const float noiseLevel = p.noiseDb > -95.5f ? dbToGain (std::clamp (p.noiseDb, -96.0f, -24.0f)) : 0.0f;
    if (noiseLevel > 0.0f)
    {
        int type = p.noiseType;
        if (type == RetroSettings::autoNoise)
            type = p.mode == RetroSettings::tape ? RetroSettings::hiss : (p.mode == RetroSettings::vinyl ? RetroSettings::vinylNoise : RetroSettings::digitalNoise);
        const float follow = std::clamp (p.noiseFollow, 0.0f, 1.0f);
        const float fs = static_cast<float> (s.rate), dt = 1.0f / fs;
        const float hpA = std::exp (-twoPi * 400.0f / fs);
        const float roomA = 1.0f - std::exp (-twoPi * 300.0f / fs);
        const float surfA = 1.0f - std::exp (-twoPi * 3000.0f / fs);
        const float rumbleA = 1.0f - std::exp (-twoPi * 25.0f / fs);
        const float upStep = dt / 0.05f, downStep = dt / 0.15f;
        for (int i = 0; i < n; ++i)
        {
            const float env = s.envAt[static_cast<std::size_t> (i)];
            s.idleSeconds = env < 2.5e-4f ? s.idleSeconds + dt : 0.0f;
            const float idleTarget = (! p.noiseAutoMute || s.idleSeconds < 0.25f) ? 1.0f : 0.0f;
            s.idleGain = idleTarget > s.idleGain ? std::min (idleTarget, s.idleGain + upStep) : std::max (idleTarget, s.idleGain - downStep);
            const float level = noiseLevel * ((1.0f - follow) + follow * std::min (1.0f, env * 4.0f)) * s.idleGain;
            for (int c = 0; c < 2; ++c)
            {
                auto& chn = s.ch[c];
                const float w = static_cast<float> (s.noiseRng.bipolar());
                float v = 0.0f;
                switch (type)
                {
                    case RetroSettings::hiss: chn.n1 = hpA * (chn.n1 + w - chn.nPrev); chn.nPrev = w; v = chn.n1; break;
                    case RetroSettings::room: chn.n1 += roomA * (w - chn.n1); v = 3.0f * chn.n1; break;
                    case RetroSettings::vinylNoise:
                        chn.n1 += surfA * (w - chn.n1);
                        chn.n2 += rumbleA * (w - chn.n2);
                        v = 0.6f * chn.n1 + 9.0f * chn.n2;
                        break;
                    default: v = 0.5f * w; break;
                }
                (c == 0 ? s.outL : s.outR)[static_cast<std::size_t> (i)] += level * v;
            }
        }
    }
    std::memcpy (left, s.outL.data(), sizeof (float) * static_cast<std::size_t> (n));
    std::memcpy (right, s.outR.data(), sizeof (float) * static_cast<std::size_t> (n));
    analyse (left, right, n);
    s.tickRemainder += n;
    while (s.tickRemainder >= tickSamples)
    {
        s.tickRemainder -= tickSamples;
        ticks.fetch_add (1, std::memory_order_relaxed);
    }
}

void Retro::runMode (int mode, const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
{
    switch (mode)
    {
        case 3: runCodec (inL, inR, outL, outR, n); break;
        case 2: runDigital (inL, inR, outL, outR, n); break;
        default: runAnalog (mode, inL, inR, outL, outR, n); break;
    }
}

//==============================================================================
// TAPE and VINYL.
void Retro::runAnalog (int mode, const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
{
    auto& s = *st;
    const auto& d = s.d;
    const bool vinyl = mode == 1;
    const float fs = static_cast<float> (s.rate);
    // The saturation: an asymmetric tanh (its bias makes even harmonics), ADAA, and a dry-wet
    // blend so 0 is the input untouched; frequency preservation splits around it.
    const float sat = vinyl ? 0.5f * d.saturation : d.saturation;
    const double k = 1.0 + 8.0 * sat, b = (vinyl ? 0.05 : 0.12) * sat;
    const double tb = std::tanh (b), norm = 1.0 / (k * (1.0 - tb * tb));
    const float wet = std::min (1.0f, sat * 3.0f);
    auto shape = [k, b, tb, norm] (double x) { return (std::tanh (k * x + b) - tb) * norm; };
    auto anti = [k, b, tb, norm] (double x) { return (logCosh (k * x + b) / k - tb * x) * norm; };
    const float splitA = 1.0f - std::exp (-twoPi * (p.preserve == RetroSettings::keepHigh ? 3000.0f : 200.0f) / fs);
    const float dcA = std::exp (-twoPi * 8.0f / fs);
    // The transport: wow (the platter's turn on VINYL), flutter, a slow random drift.
    const double wowHz = vinyl ? std::array<double, 3> { 100.0 / 3.0 / 60.0, 45.0 / 60.0, 78.0 / 60.0 }[static_cast<std::size_t> (std::clamp (p.rpm, 0, 2))]
                               : std::clamp (static_cast<double> (p.wowHz), 0.1, 3.0);
    const double flutterHz = std::clamp (static_cast<double> (p.flutterHz), 3.0, 20.0);
    const float wowA = d.wowDepth * 0.0022f * fs, flutterA = d.flutterDepth * 0.00018f * fs;
    const float stereo = std::clamp (p.stereoPhaseDeg, 0.0f, 180.0f) * 3.14159265f / 180.0f;
    const float randomness = std::clamp (p.randomness, 0.0f, 1.0f);
    const int driftEvery = std::max (1, static_cast<int> (0.25 * s.rate));
    const float driftA = 1.0f / (0.3f * fs);
    // Dropouts: Poisson events.
    const double dropRate = d.dropoutRate;
    if (dropRate <= 0.0)
        s.dropIn = 1.0e12;
    else if (s.dropIn > 1.0e11)
        s.dropIn = -std::log (1.0 - s.rng.nextDouble()) / dropRate * s.rate;
    const float dropLpA = 1.0f - std::exp (-twoPi * 1800.0f / fs);
    const float lowA = 1.0f - std::exp (-twoPi * 400.0f / fs);
    // Vinyl events.
    const double clickRate = vinyl ? 50.0 * p.crackle : 0.0, popRate = vinyl ? 1.5 * p.pops : 0.0;
    if (clickRate <= 0.0)
        s.clickIn = 1.0e12;
    else if (s.clickIn > 1.0e11)
        s.clickIn = -std::log (1.0 - s.rng.nextDouble()) / clickRate * s.rate;
    if (popRate <= 0.0)
        s.popIn = 1.0e12;
    else if (s.popIn > 1.0e11)
        s.popIn = -std::log (1.0 - s.rng.nextDouble()) / popRate * s.rate;
    const float clickDecay = std::exp (-1.0f / (0.00015f * fs)), popDecay = std::exp (-1.0f / (0.0015f * fs));
    const float popLpA = 1.0f - std::exp (-twoPi * 600.0f / fs);
    const int pad = s.latency - (s.baseDelay - 1);
    float lastWow = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        // Transport phases (shared; the right channel offset by STEREO PHASE).
        s.wowPhase += wowHz / s.rate;
        s.flutterPhase += flutterHz / s.rate;
        s.flutter2Phase += 1.37 * flutterHz / s.rate;
        for (double* ph : { &s.wowPhase, &s.flutterPhase, &s.flutter2Phase })
            if (*ph >= 1.0)
                *ph -= 1.0;
        if (++s.driftCount >= driftEvery)
        {
            s.driftCount = 0;
            s.driftTarget = static_cast<float> (s.rng.bipolar());
        }
        s.drift += driftA * (s.driftTarget - s.drift);
        // Dropout envelope.
        float dropEnv = 0.0f;
        if (s.dropLength > 0)
        {
            const float x = static_cast<float> (s.dropPos) / static_cast<float> (s.dropLength);
            dropEnv = 0.5f - 0.5f * std::cos (twoPi * x);
            if (++s.dropPos >= s.dropLength)
                s.dropLength = 0;
        }
        else if ((s.dropIn -= 1.0) <= 0.0)
        {
            s.dropLength = std::max (8, static_cast<int> (std::clamp (p.dropoutMs, 2.0f, 250.0f) * 0.001f * fs * static_cast<float> (s.rng.uniform (0.5, 1.5))));
            s.dropPos = 0;
            s.dropDepth = std::clamp (d.dropoutDepth, 0.0f, 1.0f) * static_cast<float> (s.rng.uniform (0.4, 1.0));
            s.dropIn = dropRate > 0.0 ? -std::log (1.0 - s.rng.nextDouble()) / dropRate * s.rate : 1.0e12;
            event (0);
        }
        // Clicks and pops (VINYL).
        if ((s.clickIn -= 1.0) <= 0.0)
        {
            const float u = static_cast<float> (s.rng.nextDouble());
            const float a = 0.25f * p.crackle * (0.15f + 0.85f * u * u * u) * (s.rng.nextDouble() < 0.5 ? -1.0f : 1.0f);
            const int side = static_cast<int> (s.rng.nextBelow (3));
            if (side != 1)
                s.ch[0].clickEnv = a;
            if (side != 0)
                s.ch[1].clickEnv = a * (side == 2 ? 0.7f : 1.0f);
            s.clickIn = -std::log (1.0 - s.rng.nextDouble()) / clickRate * s.rate;
            event (1);
        }
        if ((s.popIn -= 1.0) <= 0.0)
        {
            const float a = 0.35f * p.pops * static_cast<float> (s.rng.uniform (0.3, 1.0));
            s.ch[0].popEnv = a;
            s.ch[1].popEnv = a * 0.9f;
            s.popIn = -std::log (1.0 - s.rng.nextDouble()) / popRate * s.rate;
            event (2);
        }
        for (int c = 0; c < 2; ++c)
        {
            auto& chn = s.ch[c];
            float x = (c == 0 ? inL : inR)[i];
            // Saturation (with the preserved band around it).
            if (wet > 0.0f)
            {
                chn.split += splitA * (x - chn.split);
                const float low = chn.split, high = x - low;
                const float into = p.preserve == RetroSettings::keepLow ? high : (p.preserve == RetroSettings::keepHigh ? low : x);
                const double dx = static_cast<double> (into) - chn.adaaPrev;
                const double y = std::abs (dx) > 1.0e-5 ? (anti (into) - anti (chn.adaaPrev)) / dx : shape (0.5 * (into + chn.adaaPrev));
                chn.adaaPrev = into;
                // The difference the circuit makes, its DC removed (the bias), blended in.
                const float diff = static_cast<float> (y) - into;
                chn.dcY = dcA * (chn.dcY + diff - chn.dcX);
                chn.dcX = diff;
                x += wet * chn.dcY;
            }
            x = chn.response.process (x);
            // Transport.
            chn.transport.push (x);
            auto delayFor = [&] (float off) {
                const float dl = static_cast<float> (s.baseDelay) + wowA * std::sin (twoPi * static_cast<float> (s.wowPhase) + off)
                               + randomness * wowA * 0.8f * s.drift
                               + flutterA * (1.0f + 0.3f * randomness * s.drift) * (std::sin (twoPi * static_cast<float> (s.flutterPhase) + off) + 0.5f * std::sin (twoPi * static_cast<float> (s.flutter2Phase) + off));
                return std::clamp (dl, 2.0f, static_cast<float> (2 * s.baseDelay));
            };
            const float delay = delayFor (0.0f);
            if (c == 0)
            {
                lastWow = s.lastDelayL - delay;   // (samples per sample: the pitch's deviation)
                s.lastDelayL = delay;
            }
            float y = chn.transport.read (delay);
            if (c == 1 && stereo > 0.0f)
            {
                // STEREO PHASE: the right transport runs offset above ~400 Hz; below it both
                // channels share one transport, so the low end stays centred in mono.
                const float offset = chn.transport.read (delayFor (stereo));
                chn.lowShared += lowA * ((y - offset) - chn.lowShared);
                y = offset + chn.lowShared;
            }
            // Dropout: a dip, its overtones muted first.
            chn.dropLp += dropLpA * (y - chn.dropLp);
            if (dropEnv > 0.0f)
            {
                const float depth = s.dropDepth * dropEnv;
                y = (y + 0.8f * depth * (chn.dropLp - y)) * (1.0f - 0.7f * depth);
            }
            // Clicks and pops.
            if (chn.clickEnv != 0.0f)
            {
                y += chn.clickEnv * static_cast<float> (s.eventRng.bipolar());
                chn.clickEnv = std::abs (chn.clickEnv) < 1.0e-5f ? 0.0f : chn.clickEnv * clickDecay;
            }
            if (chn.popEnv != 0.0f)
            {
                chn.popLp += popLpA * (chn.popEnv * static_cast<float> (s.eventRng.bipolar()) - chn.popLp);
                y += std::clamp (chn.popLp * 6.0f, -0.6f, 0.6f);
                chn.popEnv = chn.popEnv < 1.0e-5f ? 0.0f : chn.popEnv * popDecay;
            }
            // Pad to the constant latency.
            chn.pad.push (y);
            (c == 0 ? outL : outR)[i] = chn.pad.readInteger (pad + 1);
        }
    }
    wowCents.store (1200.0f * std::log2 (std::max (0.5f, 1.0f + lastWow)), std::memory_order_relaxed);
}

//==============================================================================
// DIGITAL 80s / 90s: sample and hold at the effective rate, quantisation at the effective
// bits; CLEAN: anti-alias and reconstruction filters (8th-order elliptic) and dither (90s with
// first-order noise shaping); HARD: the raw staircase. 80s truncates toward zero (a cheap
// converter's grain); 90s rounds. MOTION drifts the rate and bits slowly (seeded).
void Retro::runDigital (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
{
    auto& s = *st;
    const bool eighties = p.era == RetroSettings::eighties;
    const float motionAmount = std::clamp (p.digitalMotion, 0.0f, 1.0f);
    // A converter at (or above) the host's rate reduces nothing: no hold, no filters - only bits.
    const bool reduces = 1000.0 * s.d.rateKHz * (1.0 - 0.15 * motionAmount) < s.rate * 0.999;
    const bool smooth = ! p.aliasHard, clean = smooth && reduces;
    const int motionEvery = std::max (1, static_cast<int> (0.5 * s.rate));
    const float motionA = 1.0f / (0.4f * static_cast<float> (s.rate));
    float shownBits = s.d.bits, shownRate = s.d.rateKHz;
    for (int i = 0; i < n; ++i)
    {
        if (++s.motionCount >= motionEvery)
        {
            s.motionCount = 0;
            s.motionTarget = static_cast<float> (s.rng.bipolar());
            s.motionTarget2 = static_cast<float> (s.rng.bipolar());
        }
        s.motion += motionA * (s.motionTarget - s.motion);
        s.motion2 += motionA * (s.motionTarget2 - s.motion2);
        const double rateEff = std::min (s.rate, 1000.0 * s.d.rateKHz * (1.0 + 0.15 * motionAmount * s.motion));
        const float bits = std::clamp (s.d.bits + 1.5f * motionAmount * s.motion2, 3.0f, 16.0f);
        const float step = std::exp2 (1.0f - bits);
        shownBits = bits;
        shownRate = static_cast<float> (rateEff * 0.001);
        s.holdPhase += rateEff / s.rate;
        const bool take = s.holdPhase >= 1.0 || rateEff >= s.rate * 0.999;
        if (s.holdPhase >= 1.0)
            s.holdPhase -= std::floor (s.holdPhase);
        for (int c = 0; c < 2; ++c)
        {
            auto& chn = s.ch[c];
            float x = (c == 0 ? inL : inR)[i];
            if (clean)
                for (auto& f : chn.aa)
                    x = f.process (x);
            if (take)
            {
                float q;
                if (eighties)
                    q = std::trunc (x / step) * step;
                else
                {
                    const float v = x - chn.error;
                    const float dither = (smooth && s.envAt[static_cast<std::size_t> (i)] > 1.0e-5f) ? (static_cast<float> (s.eventRng.nextDouble() + s.eventRng.nextDouble()) - 1.0f) * step : 0.0f;
                    q = std::round ((v + dither) / step) * step;
                    chn.error = smooth ? std::clamp (q - v, -step, step) : 0.0f;
                }
                chn.held = std::clamp (q, -1.5f, 1.5f);
            }
            float y = chn.held;
            if (clean)
                for (auto& f : chn.rc)
                    y = f.process (y);
            y = chn.response.process (y);
            chn.pad.push (y);
            (c == 0 ? outL : outR)[i] = chn.pad.readInteger (s.latency + 1);
        }
    }
    effectiveBits.store (shownBits, std::memory_order_relaxed);
    effectiveRateKHz.store (shownRate, std::memory_order_relaxed);
}

//==============================================================================
// CODEC: an STFT (Hann analysis and synthesis, hop N/4, normalised overlap-add). Its latency
// is the frame. Per frame: a band limit, a mask of weak bins (relative to their neighbours'
// mean and to the frame's peak, held for some frames by SPEED), log-domain quantisation of
// what stays; DROP / REPEAT whole frames (crossfaded by the windows), JITTER the phases. The
// spectrum of a real signal stays Hermitian (bins 1..N/2-1 mirrored, DC and Nyquist real).
void Retro::runCodec (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
{
    auto& s = *st;
    const float artifact = std::clamp (p.codecArtifact, 0.0f, 1.0f), ambience = std::clamp (p.codecAmbience, 0.0f, 1.0f);
    const std::array<int, 4> taps { static_cast<int> (0.0051 * s.rate), static_cast<int> (0.0073 * s.rate), static_cast<int> (0.0117 * s.rate), static_cast<int> (0.029 * s.rate) };
    for (int i = 0; i < n; ++i)
    {
        const float xl = inL[i], xr = inR[i];
        s.ch[0].fifo[static_cast<std::size_t> (s.fifoPos)] = xl;
        s.ch[1].fifo[static_cast<std::size_t> (s.fifoPos)] = xr;
        s.ch[0].dry.push (xl);
        s.ch[1].dry.push (xr);
        s.fifoPos = (s.fifoPos + 1) % s.frame;
        float y[2];
        for (int c = 0; c < 2; ++c)
        {
            auto& chn = s.ch[c];
            y[c] = chn.ola[static_cast<std::size_t> (s.readPos)];
            chn.ola[static_cast<std::size_t> (s.readPos)] = 0.0f;
        }
        s.readPos = (s.readPos + 1) % s.frame;
        if (++s.sinceFrame >= s.hop)
        {
            s.sinceFrame = 0;
            codecFrame();
        }
        for (int c = 0; c < 2; ++c)
        {
            const float dry = s.ch[c].dry.readInteger (s.latency + 1);
            const float art = y[c] - dry;
            float out = p.artifactOnly ? std::tanh (art * 1.5f) / 1.5f : y[c] + artifact * art;
            if (ambience > 0.0f && c == 0)
            {
                // A small diffusion of the artifact (its own, bounded; never the instrument's SPACE).
                float v = art + 0.55f * s.ambienceFb;
                for (int a = 0; a < 3; ++a)
                {
                    const float delayed = s.ambience[a].readInteger (taps[static_cast<std::size_t> (a)]);
                    const float w = v + 0.6f * delayed;
                    s.ambience[a].push (w);
                    v = delayed - 0.6f * w;
                }
                s.ambience[3].push (v);
                s.ambienceFb = std::clamp (s.ambience[3].readInteger (taps[3]), -1.0f, 1.0f);
            }
            if (ambience > 0.0f)
                out += ambience * (c == 0 ? s.ambienceFb : -s.ambienceFb * 0.8f);
            (c == 0 ? outL : outR)[i] = out;
        }
    }
}

void Retro::codecFrame() noexcept
{
    auto& s = *st;
    const int N = s.frame, half = N / 2;
    const float loss = s.d.codecLoss, speed = s.d.codecSpeed;
    const int channels = p.codecStereo == RetroSettings::mono ? 1 : 2;
    // Analysis.
    for (int c = 0; c < 2; ++c)
    {
        auto& spec = s.spec[c];
        for (int k = 0; k < N; ++k)
        {
            const int at = (s.fifoPos + k) % N;   // (the oldest sample first)
            float v = s.ch[c].fifo[static_cast<std::size_t> (at)];
            if (channels == 1)
                v = 0.5f * (s.ch[0].fifo[static_cast<std::size_t> (at)] + s.ch[1].fifo[static_cast<std::size_t> (at)]);
            spec[static_cast<std::size_t> (k)] = { v * s.window[static_cast<std::size_t> (k)], 0.0f };
        }
        s.fft.transform (spec.data(), false);
    }
    // Packets: DROP / REPEAT bursts (their length shorter as SPEED rises).
    const int mode = p.codecMode;
    const bool packets = mode == RetroSettings::drop || mode == RetroSettings::repeat || mode == RetroSettings::combined;
    int packet = 0;   // 0 none, 1 drop, 2 repeat
    if (s.burst > 0)
    {
        packet = s.burstKind;
        --s.burst;
    }
    else if (packets && s.codecRng.nextDouble() < (mode == RetroSettings::combined ? 0.2 : 0.4) * loss)
    {
        packet = mode == RetroSettings::drop ? 1 : (mode == RetroSettings::repeat ? 2 : (s.codecRng.nextDouble() < 0.5 ? 1 : 2));
        if (packet == 2 && ! s.heldValid)
            packet = 1;
        s.burstKind = packet;
        s.burst = static_cast<int> ((1.0f - speed) * 5.0f * static_cast<float> (s.codecRng.nextDouble()));
        event (3);
    }
    // The mask: weak bins (against their neighbours and the frame's peak) and the band limit,
    // recomputed every few frames (SPEED).
    const int holdFrames = 1 + static_cast<int> (std::round ((1.0f - speed) * 10.0f));
    const bool spectral = mode == RetroSettings::standard || mode == RetroSettings::combined;
    const bool remask = (s.frameCount % holdFrames) == 0;
    const float cutoffHz = loss > 0.0f ? 20000.0f - 15000.0f * loss : 1.0e9f;
    const int cutBin = std::clamp (static_cast<int> (cutoffHz / static_cast<float> (s.rate) * static_cast<float> (N)), 8, half);
    if (spectral && remask)
        for (int c = 0; c < channels; ++c)
        {
            const bool joint = p.codecStereo == RetroSettings::joint;
            float peak = 1.0e-9f;
            for (int k = 0; k <= half; ++k)
            {
                const auto v = joint ? 0.5f * (s.spec[0][static_cast<std::size_t> (k)] + s.spec[1][static_cast<std::size_t> (k)]) : s.spec[c][static_cast<std::size_t> (k)];
                s.mag[static_cast<std::size_t> (k)] = std::abs (v);
                peak = std::max (peak, s.mag[static_cast<std::size_t> (k)]);
            }
            s.prefix[0] = 0.0f;
            for (int k = 0; k <= half; ++k)
                s.prefix[static_cast<std::size_t> (k + 1)] = s.prefix[static_cast<std::size_t> (k)] + s.mag[static_cast<std::size_t> (k)];
            // (LOSS 0: nothing masked - the codec is then transparent but for its latency.)
            const float relative = 1.2f * loss, floorLevel = loss > 0.0f ? peak * std::pow (10.0f, -(100.0f - 70.0f * loss) / 20.0f) : 0.0f;
            for (int k = 0; k <= half; ++k)
            {
                const int a = std::max (0, k - 6), b = std::min (half, k + 6);
                const float mean = (s.prefix[static_cast<std::size_t> (b + 1)] - s.prefix[static_cast<std::size_t> (a)]) / static_cast<float> (b - a + 1);
                const float m = s.mag[static_cast<std::size_t> (k)];
                float keep = (m >= relative * mean && m >= floorLevel) ? 1.0f : 0.0f;
                if (k > cutBin)
                    keep = 0.0f;
                else if (k > cutBin - 4)
                    keep *= static_cast<float> (cutBin - k) / 4.0f;
                s.mask[c][static_cast<std::size_t> (k)] = keep;
            }
            if (joint)
                s.mask[1] = s.mask[0];
        }
    // The phase jitter's angles (redrawn every frame at full SPEED, held longer below).
    if (mode == RetroSettings::jitter && remask)
        for (int k = 0; k <= half; ++k)
            s.jitter[static_cast<std::size_t> (k)] = static_cast<float> (s.codecRng.bipolar()) * 3.14159265f * loss;
    float keptSum = 0.0f;
    const float q = 1.75f * loss;   // (log2 units: up to 10.5 dB steps; none at LOSS 0)
    for (int c = 0; c < 2; ++c)
    {
        auto& spec = s.spec[c];
        const int src = channels == 1 ? 0 : c;
        if (packet == 1)
            std::fill (spec.begin(), spec.end(), std::complex<float> {});
        else if (packet == 2)
            spec = s.held[src];
        else
        {
            if (channels == 1 && c == 1)
                spec = s.spec[0];
            else
            {
                for (int k = 0; k <= half; ++k)
                {
                    auto& v = spec[static_cast<std::size_t> (k)];
                    if (spectral)
                    {
                        const float keep = s.mask[c][static_cast<std::size_t> (k)];
                        const float m = std::abs (v);
                        if (keep <= 0.0f || m < 1.0e-12f)
                            v = {};
                        else
                        {
                            const float mq = q > 0.02f ? std::exp2 (std::round (std::log2 (m) / q) * q) : m;
                            v *= keep * mq / m;
                            keptSum += keep;
                        }
                    }
                    else if (mode == RetroSettings::jitter && k > 0 && k < half)
                        v *= std::polar (1.0f, s.jitter[static_cast<std::size_t> (k)]);
                }
                for (int k = 1; k < half; ++k)
                    spec[static_cast<std::size_t> (N - k)] = std::conj (spec[static_cast<std::size_t> (k)]);
                spec[0] = { spec[0].real(), 0.0f };
                spec[static_cast<std::size_t> (half)] = { spec[static_cast<std::size_t> (half)].real(), 0.0f };
                if (packet == 0)
                    s.held[c] = spec;
            }
        }
    }
    if (packet == 0)
        s.heldValid = true;
    keptFraction.store (spectral ? keptSum / static_cast<float> ((half + 1) * channels) : 1.0f, std::memory_order_relaxed);
    // Synthesis: inverse, window, overlap-add (Hann^2 at hop N/4 sums to 1.5).
    for (int c = 0; c < 2; ++c)
    {
        auto& spec = s.spec[c];
        s.fft.transform (spec.data(), true);
        auto& chn = s.ch[c];
        for (int k = 0; k < N; ++k)
        {
            const auto at = static_cast<std::size_t> ((s.readPos + k) % N);
            chn.ola[at] += spec[static_cast<std::size_t> (k)].real() * s.window[static_cast<std::size_t> (k)] * (2.0f / 3.0f);
        }
    }
    ++s.frameCount;
}

//==============================================================================
// The graph's spectrum: a Hann-windowed FFT of the output every frame, 48 log bands.
void Retro::analyse (const float* l, const float* r, int n) noexcept
{
    auto& s = *st;
    const int N = s.frame;
    for (int i = 0; i < n; ++i)
    {
        s.analysisBuf[static_cast<std::size_t> (s.analysisPos)] = 0.5f * (l[i] + r[i]);
        if (++s.analysisPos < N)
            continue;
        s.analysisPos = 0;
        for (int k = 0; k < N; ++k)
            s.analysisWork[static_cast<std::size_t> (k)] = { s.analysisBuf[static_cast<std::size_t> (k)] * s.window[static_cast<std::size_t> (k)], 0.0f };
        s.analysisFft.transform (s.analysisWork.data(), false);
        for (int b = 0; b < bands; ++b)
        {
            const double lo = 30.0 * std::pow (20000.0 / 30.0, static_cast<double> (b) / bands);
            const double hi = 30.0 * std::pow (20000.0 / 30.0, static_cast<double> (b + 1) / bands);
            const int k0 = std::max (1, static_cast<int> (lo / s.rate * N)), k1 = std::max (k0, std::min (N / 2, static_cast<int> (hi / s.rate * N)));
            float sum = 0.0f;
            for (int k = k0; k <= k1; ++k)
                sum += std::norm (s.analysisWork[static_cast<std::size_t> (k)]);
            const float db = 10.0f * std::log10 (sum / static_cast<float> (k1 - k0 + 1) + 1.0e-14f) - 20.0f * std::log10 (0.25f * static_cast<float> (N));
            auto& v = spectrumDb[static_cast<std::size_t> (b)];
            const float was = v.load (std::memory_order_relaxed);
            v.store (db > was ? db : was + 0.35f * (db - was), std::memory_order_relaxed);
        }
    }
}

} // namespace andor::analog::fx
