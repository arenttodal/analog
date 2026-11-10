#include "SubOsc.h"

#include <algorithm>
#include <cmath>

namespace andor::analog
{

namespace
{
    constexpr double twoPi = 6.283185307179586;

    /** Two-point PolyBLEP: the step's residual at `t` (0..1) for a phase increment `dt`. */
    inline float polyBlep (double t, double dt) noexcept
    {
        if (t < dt)
        {
            t /= dt;
            return static_cast<float> (t + t - t * t - 1.0);
        }
        if (t > 1.0 - dt)
        {
            t = (t - 1.0) / dt;
            return static_cast<float> (t * t + t + t + 1.0);
        }
        return 0.0f;
    }

    inline float dbToGain (double db) noexcept { return static_cast<float> (std::pow (10.0, db / 20.0)); }
}

void SubOsc::prepare (double sampleRate) noexcept
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    attackStep = static_cast<float> (1.0 / (0.012 * rate));             // a 12 ms anti-click attack
    releaseCoef = static_cast<float> (std::exp (-6.9 / (0.22 * rate)));   // -60 dB in 220 ms
    rangeCoef = static_cast<float> (1.0 - std::exp (-1.0 / (0.012 * rate)));   // range / response / level glide
    quickCoef = static_cast<float> (std::exp (-6.9 / (0.005 * rate)));     // ALL SOUND OFF: 5 ms
    const double rc = 1.0 / (twoPi * 25.0);                                // the subsonic guard at 25 Hz
    hpCoef = static_cast<float> (rc / (rc + 1.0 / rate));
    setSettings (settings);
    reset();
}

void SubOsc::reset() noexcept
{
    for (auto& v : voices)
        v = Voice {};
    held.fill (0);
    sustained.fill (false);
    pedal = false;
    killing = false;
    bendSemitones = 0.0;
    toneState = hp1x = hp1y = hp2x = hp2y = 0.0f;
}

void SubOsc::setSettings (const SubSettings& s) noexcept
{
    settings = s;
    settings.octave = s.octave <= -2 ? -2 : -1;
    settings.lowKey = std::clamp (s.lowKey, 0, 127);
    settings.highKey = std::clamp (s.highKey, settings.lowKey, 127);
    // GRIT's tone: the harmonics it adds tamed from 9 kHz (a touch) to 2.2 kHz (all of it).
    const double toneHz = 9000.0 * std::pow (2200.0 / 9000.0, std::clamp (settings.grit, 0.0, 1.0));
    toneCoef = static_cast<float> (1.0 - std::exp (-twoPi * toneHz / rate));
    reconcile();
}

float SubOsc::keyGain (int key) const noexcept
{
    if (! inRange (key))
        return 0.0f;
    float g = dbToGain (settings.levelDb);
    if (settings.taper && key > settings.taperStart)
        g *= dbToGain (-settings.taperDbPerOctave * static_cast<double> (key - settings.taperStart) / 12.0);
    return g;
}

void SubOsc::startKey (int key, float velocity) noexcept
{
    // A voice already on this key (still sounding or fading) takes it again: no new attack.
    Voice* chosen = nullptr;
    for (auto& v : voices)
        if (v.key == key)
            chosen = &v;
    if (chosen == nullptr)
        for (auto& v : voices)
            if (v.key < 0)
            {
                chosen = &v;
                break;
            }
    // None free: the oldest released voice, else the oldest held one (it glides to the new key).
    if (chosen == nullptr)
        for (auto& v : voices)
            if (v.releasing && (chosen == nullptr || v.age < chosen->age))
                chosen = &v;
    if (chosen == nullptr)
        for (auto& v : voices)
            if (chosen == nullptr || v.age < chosen->age)
                chosen = &v;
    const bool fresh = chosen->key < 0;
    chosen->key = key;
    chosen->releasing = false;
    chosen->velocity = velocity;
    chosen->age = ++clock;
    chosen->gainTarget = keyGain (key) * velocityGain (velocity);
    if (fresh)
    {
        chosen->gain = chosen->gainTarget;
        chosen->env = 0.0f;
        chosen->phase = 0.0;
    }
}

void SubOsc::releaseKey (int key) noexcept
{
    for (auto& v : voices)
        if (v.key == key)
            v.releasing = true;
}

void SubOsc::releaseAll() noexcept
{
    for (auto& v : voices)
        if (v.key >= 0)
            v.releasing = true;
}

void SubOsc::reconcile() noexcept
{
    // Off, or a key out of the range now: its voice fades (the release). On, a held key in the
    // range without a voice: it starts (turning SUB on, or widening the range, while playing).
    for (auto& v : voices)
        if (v.key >= 0)
        {
            if (! settings.enabled || ! inRange (v.key))
                v.releasing = true;
            else
                v.gainTarget = keyGain (v.key) * velocityGain (v.velocity);
        }
    if (! settings.enabled)
        return;
    for (int key = 0; key < 128; ++key)
    {
        const auto k = static_cast<std::size_t> (key);
        if ((held[k] == 0 && ! sustained[k]) || ! inRange (key))
            continue;
        bool playing = false;
        for (const auto& v : voices)
            playing = playing || (v.key == key && ! v.releasing);
        if (! playing)
            startKey (key, heldVelocity[k]);
    }
}

void SubOsc::handleMidi (const std::uint8_t* data, int size) noexcept
{
    if (size < 1)
        return;
    const int status = data[0] & 0xf0;
    if ((status == 0x90 || status == 0x80) && size >= 3)
    {
        const int key = data[1] & 0x7f;
        const auto k = static_cast<std::size_t> (key);
        if (status == 0x90 && data[2] > 0)
        {
            held[k] = static_cast<std::uint8_t> (std::min (255, held[k] + 1));
            sustained[k] = false;
            heldVelocity[k] = static_cast<float> (data[2]) / 127.0f;
            if (settings.enabled && inRange (key))
                startKey (key, heldVelocity[k]);
        }
        else if (held[k] > 0)
        {
            if (--held[k] == 0)
            {
                if (pedal)
                    sustained[k] = true;
                else
                    releaseKey (key);
            }
        }
        return;
    }
    if (status == 0xb0 && size >= 3)
    {
        const int controller = data[1], value = data[2];
        if (controller == 64)
        {
            pedal = value >= 64;
            if (! pedal)
                for (int key = 0; key < 128; ++key)
                    if (sustained[static_cast<std::size_t> (key)])
                    {
                        sustained[static_cast<std::size_t> (key)] = false;
                        if (held[static_cast<std::size_t> (key)] == 0)
                            releaseKey (key);
                    }
        }
        else if (controller == 120 || controller == 123)   // ALL SOUND OFF (at once) / ALL NOTES OFF
        {
            held.fill (0);
            sustained.fill (false);
            releaseAll();
            killing = killing || controller == 120;
        }
        else if (controller == 121)   // RESET ALL CONTROLLERS
        {
            bendSemitones = 0.0;
            if (pedal)
            {
                const std::uint8_t off[3] { 0xb0, 64, 0 };
                handleMidi (off, 3);
            }
        }
        return;
    }
    if (status == 0xe0 && size >= 3)
    {
        const int bend = ((data[2] & 0x7f) << 7 | (data[1] & 0x7f)) - 8192;
        bendSemitones = bendRange * static_cast<double> (bend) / 8192.0;
    }
}

float SubOsc::sample (Voice& v, double increment) noexcept
{
    float x;
    switch (settings.wave)
    {
        case SubSettings::Wave::sine:
            x = static_cast<float> (std::sin (twoPi * v.phase));
            break;
        case SubSettings::Wave::triangle:
            x = static_cast<float> (4.0 * std::abs (v.phase - 0.5) - 1.0);
            break;
        case SubSettings::Wave::square:
        {
            double half = v.phase + 0.5;
            if (half >= 1.0)
                half -= 1.0;
            x = (v.phase < 0.5 ? 1.0f : -1.0f) + polyBlep (v.phase, increment) - polyBlep (half, increment);
            x *= 0.7f;   // (as loud as the sine, near enough)
            break;
        }
        case SubSettings::Wave::saw:
        default:
            x = static_cast<float> (2.0 * v.phase - 1.0) - polyBlep (v.phase, increment);
            x *= 0.8f;
            break;
    }
    v.phase += increment;
    if (v.phase >= 1.0)
        v.phase -= 1.0;
    return x;
}

bool SubOsc::sounding() const noexcept
{
    for (const auto& v : voices)
        if (v.key >= 0)
            return true;
    return std::abs (hp2y) > 1.0e-7f || std::abs (hp1y) > 1.0e-7f;
}

int SubOsc::activeVoices() const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        n += v.key >= 0 && ! v.releasing ? 1 : 0;
    return n;
}

void SubOsc::render (float* left, float* right, int n) noexcept
{
    if (! sounding())
    {
        killing = false;
        hp1x = hp1y = hp2x = hp2y = toneState = 0.0f;
        return;
    }
    const float level = std::max (1.0e-6f, dbToGain (settings.levelDb));
    const double grit = std::clamp (settings.grit, 0.0, 1.0);
    const float drive = static_cast<float> (1.0 + 7.0 * grit);
    std::array<double, maxVoices> increments {};
    for (std::size_t i = 0; i < voices.size(); ++i)
        if (voices[i].key >= 0)
        {
            const double note = static_cast<double> (voices[i].key) + 12.0 * settings.octave + bendSemitones;
            increments[i] = std::min (0.45, 440.0 * std::exp2 ((note - 69.0) / 12.0) / rate);
        }
    for (int s = 0; s < n; ++s)
    {
        float sum = 0.0f;
        for (std::size_t i = 0; i < voices.size(); ++i)
        {
            auto& v = voices[i];
            if (v.key < 0)
                continue;
            if (killing)
                v.env *= quickCoef;
            else if (v.releasing)
                v.env *= releaseCoef;
            else
                v.env = std::min (1.0f, v.env + attackStep);
            v.gain += rangeCoef * (v.gainTarget - v.gain);
            sum += sample (v, increments[i]) * v.env * v.gain;
            if ((v.releasing || killing) && v.env < 1.0e-5f)
                v = Voice {};
        }
        if (grit > 0.0)
        {
            // GRIT: soft saturation measured against the level (a single key at full is driven
            // as hard as GRIT says, a quiet one hardly), its new harmonics tamed.
            const float u = sum / level;
            sum = level * std::tanh (drive * u) / drive * (1.0f + 0.35f * static_cast<float> (grit));
            toneState += toneCoef * (sum - toneState);
            sum = toneState;
        }
        // The subsonic guard: two one-pole high-passes at 25 Hz (and no DC).
        hp1y = hpCoef * (hp1y + sum - hp1x);
        hp1x = sum;
        hp2y = hpCoef * (hp2y + hp1y - hp2x);
        hp2x = hp1y;
        if (std::abs (hp2y) < 1.0e-20f)
            hp2y = 0.0f;   // (no denormals)
        left[s] += hp2y;
        right[s] += hp2y;
    }
    bool any = false;
    for (const auto& v : voices)
        any = any || v.key >= 0;
    if (! any)
        killing = false;
}

} // namespace andor::analog
