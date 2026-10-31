#pragma once

#include "engine/Dampen.h"
#include "engine/LayerEq.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace andor::analog
{

/**
    A card's EQ inside one voice, before the voice's filter: ANDOR OSP's layer EQ (the same
    five bands, the same designs, osp::eq::design, and the same trapezoidal state-variable
    sections), run on one voice's mono signal sample by sample. In ANDOR ANALOG each voice
    carries both cards (the oscillators and ORGANIC) into one analog filter, so a card's EQ
    has to act on its own part of the voice, before the filter.

    As ANDOR OSP's: frequency, gain and Q glide (about 20 ms); the sections are redesigned at
    control rate (only when a setting moved); a band or the whole EQ switched crossfades over
    10 ms. Off (every band faded out) the signal is not touched at all. Real-time safe.
*/
class VoiceEq
{
public:
    void prepare (double internalRate) noexcept
    {
        sampleRate = internalRate;
        reset();
    }

    void reset() noexcept
    {
        bands = {};
        started = false;
        engaged = false;
    }

    /** Control rate: move towards `target` over the coming `samples`. */
    void control (const osp::eq::Settings& target, int samples) noexcept
    {
        const double k = started ? 1.0 - std::exp (-static_cast<double> (samples) / (glideSeconds * sampleRate)) : 1.0;
        const double step = 1.0 / (fadeSeconds * sampleRate);
        engaged = false;
        for (int i = 0; i < osp::eq::bandCount; ++i)
        {
            auto& b = bands[static_cast<std::size_t> (i)];
            const auto& t = target.bands[static_cast<std::size_t> (i)];
            b.goal = target.enabled && t.enabled ? 1.0 : 0.0;
            b.step = step;
            if (b.mix <= 0.0 && b.goal <= 0.0)
                continue;
            engaged = true;
            const double logHz = std::log2 (std::max (1.0, t.frequencyHz));
            const double before[3] { b.logHz, b.gainDb, b.q };
            if (b.mix <= 0.0 || ! b.designed)
            {
                // Switched on from silence: at the target (nothing to glide from).
                b.logHz = logHz;
                b.gainDb = t.gainDb;
                b.q = t.q;
            }
            else
            {
                b.logHz += (logHz - b.logHz) * k;
                b.gainDb += (t.gainDb - b.gainDb) * k;
                b.q += (t.q - b.q) * k;
            }
            if (t.steep != b.steep)
            {
                b.steep = t.steep;
                b.state[1] = {};
                b.designed = false;
            }
            if (! b.designed || std::abs (before[0] - b.logHz) > 0.0 || std::abs (before[1] - b.gainDb) > 0.0 || std::abs (before[2] - b.q) > 0.0)
            {
                osp::eq::BandSettings now;
                now.frequencyHz = std::exp2 (b.logHz);
                now.gainDb = b.gainDb;
                now.q = b.q;
                now.steep = b.steep;
                b.sectionCount = osp::eq::design (static_cast<osp::eq::Band> (i), now, sampleRate, b.sections);
                b.designed = true;
            }
        }
        started = true;
    }

    /** Engaged: a band on, or still fading (the part goes through it). */
    bool active() const noexcept { return engaged; }

    float process (float x) noexcept
    {
        double in = x;
        for (auto& b : bands)
        {
            if (b.mix <= 0.0 && b.goal <= 0.0)
                continue;
            b.mix = b.goal > b.mix ? std::min (b.goal, b.mix + b.step) : std::max (b.goal, b.mix - b.step);
            double y = in;
            for (int s = 0; s < b.sectionCount; ++s)
            {
                const auto& c = b.sections[static_cast<std::size_t> (s)];
                auto& z = b.state[static_cast<std::size_t> (s)];
                const double a1 = 1.0 / (1.0 + c.g * (c.g + c.k));
                const double a2 = c.g * a1, a3 = c.g * a2;
                const double v3 = y - z[1];
                const double v1 = a1 * z[0] + a2 * v3;
                const double v2 = z[1] + a2 * z[0] + a3 * v3;
                z[0] = 2.0 * v1 - z[0];
                z[1] = 2.0 * v2 - z[1];
                y = c.m0 * y + c.m1 * v1 + c.m2 * v2;
            }
            in += b.mix * (y - in);
            if (b.mix <= 0.0)
                b.state = {};   // off: the next switch-on starts from rest
            for (auto& z : b.state)
                for (auto& v : z)
                    if (std::abs (v) < 1.0e-25)
                        v = 0.0;
        }
        return static_cast<float> (in);
    }

private:
    static constexpr double glideSeconds = 0.02, fadeSeconds = 0.01;

    struct Band
    {
        double logHz = 0.0, gainDb = 0.0, q = 0.707;
        double mix = 0.0, goal = 0.0, step = 0.0;
        bool steep = false, designed = false;
        int sectionCount = 0;
        std::array<osp::eq::Section, 2> sections {};
        std::array<std::array<double, 2>, 2> state {};
    };

    double sampleRate = 192000.0;
    std::array<Band, osp::eq::bandCount> bands {};
    bool started = false, engaged = false;
};

/**
    A card's DAMPEN inside one voice, before the filter: ANDOR OSP's per-note damping (the
    curve over the keys times AMOUNT, osp::dampen), its filter set up at control rate only when
    the damping or the settings moved, glided over about 10 ms. At no damping the filter is
    skipped (the part untouched).
*/
class VoiceDampen
{
public:
    void reset() noexcept
    {
        filter.reset();
        now = 0.0;
        applied = -1.0;
    }

    /** Control rate: `curve` the card's damping over the keys (the settings' own is not used). */
    void control (const osp::dampen::Settings& s, const osp::mod::CurveTable& curve, int note, double internalRate, double smoothing, bool immediate) noexcept
    {
        const double atKey = std::clamp (osp::mod::curveAt (curve, std::clamp (note, 0, 127) / 127.0), 0.0, 1.0);
        const double target = s.enabled ? atKey * std::clamp (s.amount, 0.0, 1.0) : 0.0;
        now = immediate ? target : now + (target - now) * smoothing;
        if (target <= 0.0 && now < 1.0e-3)
            now = 0.0;
        const double shelf = std::clamp (s.shelfHz, osp::dampen::minShelfHz, osp::dampen::maxShelfHz);
        const double cutoff = std::clamp (s.minCutoffHz, osp::dampen::minCutoffLowHz, osp::dampen::minCutoffHighHz);
        const bool moved = std::abs (now - applied) > 1.0e-3 || (now <= 0.0 && applied > 0.0) || s.mode != mode
                           || std::abs (shelf - shelfApplied) > 0.002 * shelf || std::abs (cutoff - cutoffApplied) > 0.002 * cutoff;
        if (! moved)
            return;
        if (applied <= 0.0 && now > 0.0)
            filter.reset();   // (from rest: a damping that starts now has no history)
        applied = now;
        mode = s.mode;
        shelfApplied = shelf;
        cutoffApplied = cutoff;
        filter.setup (mode, internalRate, now, shelf, cutoff);
    }

    bool active() const noexcept { return applied > 0.0 && filter.active(); }

    float process (float x) noexcept
    {
        float r = 0.0f;   // (OSP's filter is stereo; a voice here is mono)
        filter.process (x, r);
        return x;
    }

private:
    osp::dampen::Filter filter;
    osp::dampen::Mode mode = osp::dampen::Mode::softShelf;
    double now = 0.0, applied = -1.0, shelfApplied = 0.0, cutoffApplied = 0.0;
};

} // namespace andor::analog
