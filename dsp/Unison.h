#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace andor::analog
{

/** How UNISON spaces its copies between the outermost ones (which always sit at +-1). */
enum class UnisonSpread { linear, super, exp, inv, random };

/**
    UNISON (state version 24): a card's oscillator played as up to eight detuned copies inside
    each voice (so the voice keeps its own envelopes and the instrument stays polyphonic). One
    copy: the original oscillator, untouched.
*/
struct UnisonSettings
{
    int voices = 1;              ///< 1..8 (1: off)
    double detune = 0.45;        ///< 0..1: the outer copies at detune^2.5 x range semitones
    double blend = 0.75;         ///< 0..1: the outer copies' level against the centre one(s)
    double width = 1.0;          ///< 0..1: the copies spread left / right (0: mono)
    UnisonSpread spread = UnisonSpread::linear;
    double rangeSemitones = 1.0; ///< the outer copies' detune at DETUNE 100 %

    bool on() const noexcept { return voices > 1; }
    bool operator== (const UnisonSettings&) const = default;
};

namespace unisonmath
{
    constexpr int maxVoices = 8;

    /** Copy i of n (0 .. n-1) on an even grid from -1 to +1: the outer copies exactly at the
        ends whether n is odd or even (no copy at 0 when n is even). */
    inline double position (int i, int n) noexcept
    {
        if (n < 2)
            return 0.0;
        const int half = n / 2;
        const bool even = n % 2 == 0;
        const double offset = even ? 0.5 : 0.0;
        const double scale = even ? static_cast<double> (n) / (n - 1) : 1.0;
        return scale * ((i - half) + offset) / half;
    }

    /** The middle copy (odd n) or the two middle copies (even n). */
    inline bool isCentre (int i, int n) noexcept
    {
        const int half = n / 2;
        return i == half || (n % 2 == 0 && i == half - 1);
    }

    /** The spread curve: every curve keeps the ends at +-1 and only moves the inner copies.
        `random01` (RANDOM): the copy's own draw for this note, 0..1. */
    inline double curve (UnisonSpread s, double x, double random01) noexcept
    {
        switch (s)
        {
            case UnisonSpread::linear: return x;
            case UnisonSpread::super:  return std::tan (x) / std::tan (1.0);           // inner copies drawn towards the centre
            case UnisonSpread::exp:    return x * std::abs (x);                        // drawn in further
            case UnisonSpread::inv:    return std::sin (x * 1.5707963267948966);       // pushed outwards
            case UnisonSpread::random: return (x < 0.0 ? -1.0 : (x > 0.0 ? 1.0 : 0.0)) * std::clamp (random01, 0.0, 1.0);
        }
        return x;
    }

    /** The outermost copies' detune (semitones): most of the knob's travel is fine detune. */
    inline double detuneSemitones (double knob, double range) noexcept
    {
        const double d = std::clamp (knob, 0.0, 1.0);
        return d * d * std::sqrt (d) * std::max (0.0, range);
    }

    /** The outer copies' weight against the centre's (1) at BLEND b: faint at 0, louder than
        the centre near the top. An original law. */
    inline double sideWeight (double blend) noexcept
    {
        const double b = std::clamp (blend, 0.0, 1.0);
        return 0.05 + 1.25 * b * b;
    }

    /** One note's copies of one oscillator: each copy's detune (semitones), gain, and place
        (0 left .. 0.5 centre .. 1 right). The gains keep the sum's loudness that of one
        oscillator whatever BLEND and the count (the copies' phases are independent: an
        incoherent sum), so turning BLEND up never makes a patch louder. */
    struct Layout
    {
        int count = 1;
        int centre = 0;   ///< the copy that is the oscillator's own phase (sync and Poly Mod act on it)
        std::array<double, maxVoices> semitones {};
        std::array<float, maxVoices> gain {};
        std::array<float, maxVoices> place {};
    };

    inline Layout layout (const UnisonSettings& s, const std::array<float, maxVoices>& random01) noexcept
    {
        Layout l;
        const int n = std::clamp (s.voices, 1, maxVoices);
        l.count = n;
        l.centre = n / 2;
        if (n == 1)
        {
            l.gain[0] = 1.0f;
            l.place[0] = 0.5f;
            return l;
        }
        const double amount = detuneSemitones (s.detune, s.rangeSemitones);
        const double w = n == 2 ? 1.0 : sideWeight (s.blend);   // (two copies: both are the centre)
        const int centres = n % 2 == 0 ? 2 : 1;
        const double norm = 1.0 / std::sqrt (centres + (n == 2 ? 0 : n - centres) * w * w);
        const double width = std::clamp (s.width, 0.0, 1.0);
        bool flip = false;
        for (int i = 0; i < n; ++i)
        {
            const auto k = static_cast<std::size_t> (i);
            const bool centre = isCentre (i, n);
            l.gain[k] = static_cast<float> ((centre || n == 2 ? 1.0 : w) * norm);
            // (The odd count's middle copy: no detune, in the middle.)
            if (centre && n % 2 == 1)
            {
                l.semitones[k] = 0.0;
                l.place[k] = 0.5f;
                continue;
            }
            l.semitones[k] = curve (s.spread, position (i, n), static_cast<double> (random01[k])) * amount;
            // Left and right by turns along the copies (not sharp one side, flat the other).
            l.place[k] = static_cast<float> (0.5 + (flip ? -0.5 : 0.5) * width);
            flip = ! flip;
        }
        return l;
    }
}

} // namespace andor::analog
