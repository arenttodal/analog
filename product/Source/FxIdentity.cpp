#include "FxIdentity.h"

#include "RackFaceplate.h"

#include <cmath>

namespace andor::analog
{

namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    const juce::Colour bone { 0xfff5eddf }, carbon { 0xff241d17 };

    float luminance (juce::Colour c) noexcept
    {
        auto lin = [] (float v) { return v <= 0.03928f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f); };
        return 0.2126f * lin (c.getFloatRed()) + 0.7152f * lin (c.getFloatGreen()) + 0.0722f * lin (c.getFloatBlue());
    }

    /** One identity from its seeds (V5 §2.2): the name in whichever of bone and carbon reads
        better on the enamel; the rail a step deeper; the glass tinted by the enamel. */
    FxVisualIdentity make (juce::uint32 enamel, juce::uint32 ink, Motif motif, const char* tileName, const char* descriptor)
    {
        FxVisualIdentity id;
        id.enamel = juce::Colour (enamel);
        id.rail = id.enamel.darker (0.22f);
        id.edge = id.enamel.darker (0.45f);
        id.motifInk = juce::Colour (ink);
        id.text = contrastRatio (bone, id.enamel) >= contrastRatio (carbon, id.enamel) ? bone : carbon;
        id.mutedText = id.text.withAlpha (0.74f);
        id.statusActive = id.text == bone ? juce::Colour (0xfffff4dc) : juce::Colour (0xfffff8e8);
        id.glassTop = juce::Colour (0xff1d2325).interpolatedWith (id.enamel, 0.16f);
        id.glassBottom = juce::Colour (0xff161b1c).interpolatedWith (id.enamel, 0.10f);
        id.motif = motif;
        id.tileName = tileName;
        id.descriptor = descriptor;
        return id;
    }

    /** A small fixed-seed generator (the particle field: the same in every session). */
    struct Lcg
    {
        juce::uint32 state;
        float next() noexcept
        {
            state = state * 1664525u + 1013904223u;
            return static_cast<float> ((state >> 8) & 0xffffff) / static_cast<float> (0x1000000);
        }
    };

    struct Pen
    {
        juce::Graphics& g;
        juce::Colour ink;
        float alpha, width;
        void stroke (const juce::Path& p, float a = 1.0f, float w = 1.0f) const
        {
            g.setColour (ink.withMultipliedAlpha (alpha * a));
            g.strokePath (p, juce::PathStrokeType (width * w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        void dot (juce::Point<float> c, float r, float a = 1.0f) const
        {
            g.setColour (ink.withMultipliedAlpha (alpha * a));
            g.fillEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c));
        }
    };

    int countFor (MotifScale s, int thumb, int mini, int full) { return s == MotifScale::thumb ? thumb : s == MotifScale::mini ? mini : full; }
}

float contrastRatio (juce::Colour a, juce::Colour b) noexcept
{
    const float la = luminance (a), lb = luminance (b);
    return (std::max (la, lb) + 0.05f) / (std::min (la, lb) + 0.05f);
}

const FxVisualIdentity& visualIdentityFor (fx::Type type) noexcept
{
    // V5 §2.2's seeds (muted, no violet anywhere), one hue per device.
    static const FxVisualIdentity ids[fx::typeCount] {
        make (0xff68744c, 0xffccd0ab, Motif::branch, "DRIVE", "ANALOG SATURATION"),            // drive
        make (0xffa85e42, 0xffe8c9ad, Motif::flow, "MOVEMENT", "MODULATION RACK"),            // movement
        make (0xff597789, 0xffc1d4d9, Motif::rings, "ECHO", "DELAY PROCESSOR"),               // echo
        make (0xff4c7771, 0xffb5cec5, Motif::particles, "SPACE", "REVERB PROCESSOR"),         // space
        make (0xff766d62, 0xffd9d3c7, Motif::loops, "STEREO", "STEREO IMAGING / WIDENING"),   // spread
        make (0xff656d55, 0xffd7cbac, Motif::gainScale, "COMPRESSOR", "DYNAMICS PROCESSOR"),  // compressor
        make (0xff9d8051, 0xffe7d6ad, Motif::bands, "EQUALIZER", "PARAMETRIC EQUALIZER"),     // eq
        make (0xffb18336, 0xffead6a7, Motif::sweep, "FX FILTER", "FILTER PROCESSOR"),         // filter
        make (0xff617887, 0xffc5d6d9, Motif::notches, "PHASER", "ALL-PASS PHASE MACHINE"),    // phaser
        make (0xff547d7b, 0xffc1dad5, Motif::comb, "FLANGER", "COMB MODULATOR"),              // flanger
        make (0xff516c79, 0xffbbd0d2, Motif::hatch, "SHIFTER", "FREQUENCY OFFSET / BLUR"),    // shifter
        make (0xff74735b, 0xffd4c99f, Motif::impulse, "CONVOLUTION", "IMPULSE CABINET"),      // convolution
        make (0xff676a63, 0xffdbdbd2, Motif::graticule, "UTILITY", "METER BRIDGE"),           // utility
        make (0xff96543e, 0xffd5b17b, Motif::grooves, "RETRO", "TAPE / VINYL / DIGITAL CHARACTER"),   // retro
    };
    const int i = static_cast<int> (type);
    return ids[i >= 0 && i < fx::typeCount ? i : 0];
}

const FxVisualIdentity& synthIdentity (SynthRack rack) noexcept
{
    static const FxVisualIdentity filter = make (0xff5f7462, 0xffc9d6c2, Motif::resonance, "FILTER", "SYNTH  /  VOICE FILTER");
    static const FxVisualIdentity morph = make (0xffa66f58, 0xffe4c7b4, Motif::weave, "MORPH", "SYNTH  /  SPECTRAL MORPH");
    static const FxVisualIdentity aux = make (0xff6e5568, 0xffe2cfdc, Motif::grooves, "AUX", "SOURCE  /  D AUX");
    return rack == SynthRack::voiceFilter ? filter : rack == SynthRack::aux ? aux : morph;
}

void paintFxMotif (juce::Graphics& g, Motif motif, juce::Rectangle<float> b, const FxVisualIdentity& id, MotifScale scale)
{
    // On the face the motif is quiet (a silk-screen etching, never a second graph): its lines
    // under 1.5 px, its ink at a low alpha - lower still on the big face behind the controls.
    const float alpha = scale == MotifScale::full ? 0.28f : scale == MotifScale::mini ? 0.62f : 0.85f;
    const float width = scale == MotifScale::full ? 1.0f : scale == MotifScale::mini ? 1.05f : 1.0f;
    const Pen pen { g, id.motifInk, alpha, width };
    const float x = b.getX(), y = b.getY(), w = b.getWidth(), h = b.getHeight();
    switch (motif)
    {
        case Motif::flow:
        {
            // Flowing contours from the lower left sweeping up and out, gently asymmetric, a
            // slow interference between neighbours (modulated depth).
            const int n = countFor (scale, 4, 6, 11);
            for (int i = 0; i < n; ++i)
            {
                const float t = static_cast<float> (i) / static_cast<float> (std::max (1, n - 1));
                juce::Path p;
                const float y0 = y + h * (1.05f - 0.12f * t), y1 = y + h * (0.05f + 0.5f * t);
                p.startNewSubPath (x - 0.05f * w, y0);
                const int steps = 24;
                for (int k = 1; k <= steps; ++k)
                {
                    const float u = static_cast<float> (k) / static_cast<float> (steps);
                    const float px = x - 0.05f * w + u * 1.1f * w;
                    const float base = y0 + (y1 - y0) * (1.0f - std::pow (1.0f - u, 2.2f));
                    const float wobble = 0.045f * h * std::sin (twoPi * (u * (1.1f + 0.35f * t) + 0.17f * static_cast<float> (i)));
                    p.lineTo (px, base + wobble * (0.4f + 0.6f * u));
                }
                pen.stroke (p, 0.55f + 0.45f * (1.0f - t));
            }
            break;
        }
        case Motif::branch:
        {
            // Saturation transfer curves from one origin: soft, then harder; they part and
            // converge toward the clipping knee at the upper right.
            const int n = countFor (scale, 3, 4, 7);
            const juce::Point<float> origin (x + 0.02f * w, y + 0.96f * h);
            for (int i = 0; i < n; ++i)
            {
                const float k = 0.8f + 4.5f * static_cast<float> (i) / static_cast<float> (std::max (1, n - 1));
                juce::Path p;
                p.startNewSubPath (origin);
                for (int s = 1; s <= 32; ++s)
                {
                    const float u = static_cast<float> (s) / 32.0f;
                    const float v = std::tanh (k * u) / std::tanh (k);
                    p.lineTo (origin.x + u * 0.96f * w, origin.y - v * 0.86f * h);
                }
                pen.stroke (p, 0.5f + 0.5f * static_cast<float> (i) / static_cast<float> (std::max (1, n - 1)));
            }
            // The branch's small offshoots where the curves part.
            if (scale != MotifScale::thumb)
                for (int i = 0; i < (scale == MotifScale::full ? 3 : 2); ++i)
                {
                    juce::Path p;
                    const float sx = x + (0.18f + 0.12f * static_cast<float> (i)) * w, sy = y + (0.55f - 0.12f * static_cast<float> (i)) * h;
                    p.startNewSubPath (sx, sy);
                    p.quadraticTo (sx + 0.10f * w, sy - 0.20f * h, sx + 0.28f * w, sy - 0.26f * h);
                    pen.stroke (p, 0.45f);
                }
            break;
        }
        case Motif::rings:
        {
            // Repeats: arcs about a source just off the left edge, spaced out and fading.
            const int n = countFor (scale, 4, 7, 12);
            const juce::Point<float> c (x - 0.08f * w, y + 0.5f * h);
            float r = 0.16f * std::max (w, h);
            for (int i = 0; i < n; ++i)
            {
                juce::Path p;
                p.addCentredArc (c.x, c.y, r, r, 0.0f, 0.15f * juce::MathConstants<float>::pi, 0.85f * juce::MathConstants<float>::pi, true);
                pen.stroke (p, 1.0f - 0.65f * static_cast<float> (i) / static_cast<float> (n));
                r += (scale == MotifScale::full ? 0.075f : 0.11f) * std::max (w, h) * (1.0f + 0.08f * static_cast<float> (i));
            }
            break;
        }
        case Motif::sweep:
        {
            // Low-pass responses at descending cutoffs: flat, a resonant knee, the slope.
            const int n = countFor (scale, 3, 4, 7);
            for (int i = 0; i < n; ++i)
            {
                const float t = static_cast<float> (i) / static_cast<float> (std::max (1, n - 1));
                const float fc = 0.78f - 0.48f * t;
                juce::Path p;
                for (int s = 0; s <= 40; ++s)
                {
                    const float u = static_cast<float> (s) / 40.0f;
                    const float rel = (u - fc) / 0.08f;
                    float v = 0.32f;                                                  // flat (from the top)
                    v -= 0.12f * std::exp (-rel * rel);                               // the resonance
                    if (u > fc)
                        v += 1.9f * (u - fc) * (u - fc) / (0.02f + (u - fc));         // the slope
                    const float py = y + std::min (1.08f, v + 0.06f * t) * h;
                    const float px = x + u * w;
                    if (s == 0)
                        p.startNewSubPath (px, py);
                    else
                        p.lineTo (px, py);
                }
                pen.stroke (p, 0.95f - 0.5f * t);
            }
            break;
        }
        case Motif::particles:
        {
            // A sound field: dense at its source (lower left), thinning along the decay.
            Lcg rng { 0x5ace0001u };
            const int n = countFor (scale, 40, 120, 340);
            for (int i = 0; i < n; ++i)
            {
                const float d = std::pow (rng.next(), 1.7f);                      // distance along the decay
                const float spread = 0.25f + 0.75f * d;
                const float px = x + (0.02f + 0.95f * d) * w + (rng.next() - 0.5f) * 0.15f * w * spread;
                const float py = y + h * (0.92f - 0.75f * d) + (rng.next() - 0.5f) * 0.9f * h * spread * 0.55f;
                const float r = (scale == MotifScale::full ? 0.9f : 0.75f) * (0.55f + 0.7f * rng.next()) * (scale == MotifScale::thumb ? 0.8f : 1.0f);
                pen.dot ({ px, py }, r, 1.0f - 0.7f * d);
            }
            break;
        }
        case Motif::loops:
        {
            // Two linked circles (left, right) and their lens; the full face repeats them
            // as wider phase arcs.
            const int n = countFor (scale, 1, 1, 4);
            const float base = 0.30f * std::min (h, 1.4f * w);
            for (int i = 0; i < n; ++i)
            {
                const float r = base * (1.0f + 0.55f * static_cast<float> (i));
                const float cy = y + 0.5f * h, cx = x + 0.5f * w;
                const float off = 0.62f * r;
                juce::Path p;
                p.addEllipse (cx - off - r, cy - r, 2.0f * r, 2.0f * r);
                p.addEllipse (cx + off - r, cy - r, 2.0f * r, 2.0f * r);
                pen.stroke (p, 1.0f - 0.22f * static_cast<float> (i));
            }
            break;
        }
        case Motif::gainScale:
        {
            // A calibrated scale (ticks along an arc) and the knee of a gain curve.
            const int ticks = countFor (scale, 9, 15, 31);
            const juce::Point<float> c (x + 0.05f * w, y + 1.05f * h);
            const float r = 0.92f * std::min (h, w * 1.2f);
            for (int i = 0; i < ticks; ++i)
            {
                const float a = -0.05f * juce::MathConstants<float>::pi + 0.55f * juce::MathConstants<float>::pi * static_cast<float> (i) / static_cast<float> (ticks - 1);
                const bool major = i % 5 == 0;
                const auto p0 = c.getPointOnCircumference (r, a), p1 = c.getPointOnCircumference (r * (major ? 0.86f : 0.92f), a);
                juce::Path p;
                p.startNewSubPath (p0);
                p.lineTo (p1);
                pen.stroke (p, major ? 1.0f : 0.6f);
            }
            juce::Path knee;
            knee.startNewSubPath (x + 0.05f * w, y + 0.95f * h);
            knee.lineTo (x + 0.48f * w, y + 0.45f * h);
            knee.quadraticTo (x + 0.58f * w, y + 0.34f * h, x + 0.98f * w, y + 0.26f * h);
            pen.stroke (knee, 0.9f, 1.15f);
            break;
        }
        case Motif::bands:
        {
            // Three low, smooth band arcs on one baseline.
            const int sets = countFor (scale, 1, 2, 4);
            for (int s = 0; s < sets; ++s)
            {
                const float base = y + h * (0.78f - 0.18f * static_cast<float> (s));
                juce::Path p;
                for (int k = 0; k <= 48; ++k)
                {
                    const float u = static_cast<float> (k) / 48.0f;
                    auto bell = [u] (float c, float wdt, float gain) { const float d = (u - c) / wdt; return gain * std::exp (-d * d); };
                    const float v = bell (0.18f, 0.12f, 0.30f) - bell (0.50f, 0.10f, 0.18f) + bell (0.82f, 0.14f, 0.26f);
                    const float px = x + u * w, py = base - v * h * 0.75f;
                    if (k == 0)
                        p.startNewSubPath (px, py);
                    else
                        p.lineTo (px, py);
                }
                pen.stroke (p, 1.0f - 0.25f * static_cast<float> (s));
            }
            break;
        }
        case Motif::notches:
        {
            // Nested partial arcs, each broken by its notches (the phase progression).
            const int n = countFor (scale, 3, 3, 6);
            const juce::Point<float> c (x + 0.02f * w, y + 0.5f * h);
            for (int i = 0; i < n; ++i)
            {
                const float r = (0.30f + 0.17f * static_cast<float> (i)) * std::max (w, h) * (scale == MotifScale::full ? 0.8f : 1.0f);
                const int notches = 3 + i;
                const float a0 = 0.18f * juce::MathConstants<float>::pi, a1 = 0.82f * juce::MathConstants<float>::pi, span = (a1 - a0) / static_cast<float> (notches);
                for (int k = 0; k < notches; ++k)
                {
                    juce::Path p;
                    p.addCentredArc (c.x, c.y, r, r, 0.0f, a0 + span * static_cast<float> (k) + 0.06f, a0 + span * static_cast<float> (k + 1) - 0.06f, true);
                    pen.stroke (p, 1.0f - 0.12f * static_cast<float> (i));
                }
            }
            break;
        }
        case Motif::comb:
        {
            // Close teeth whose heights follow a slow sweep (the comb's notches moving).
            const int n = countFor (scale, 9, 14, 34);
            for (int i = 0; i < n; ++i)
            {
                const float u = static_cast<float> (i) / static_cast<float> (n - 1);
                const float px = x + (0.04f + 0.92f * u) * w;
                const float amp = 0.25f + 0.65f * (0.5f + 0.5f * std::cos (twoPi * (u * 1.6f + 0.1f)));
                juce::Path p;
                p.startNewSubPath (px, y + h * (0.5f - 0.45f * amp));
                p.lineTo (px, y + h * (0.5f + 0.45f * amp));
                pen.stroke (p, 0.55f + 0.45f * amp);
            }
            break;
        }
        case Motif::hatch:
        {
            // Short wave strokes in staggered rows, each row offset (shifted) from the last.
            const int rows = countFor (scale, 4, 5, 9), cols = countFor (scale, 2, 2, 4);
            for (int r = 0; r < rows; ++r)
                for (int c = 0; c < cols; ++c)
                {
                    const float cy = y + h * (0.12f + 0.78f * static_cast<float> (r) / static_cast<float> (std::max (1, rows - 1)));
                    const float cx0 = x + w * (0.02f + 0.48f * static_cast<float> (c) + 0.09f * static_cast<float> (r % 3));
                    juce::Path p;
                    for (int k = 0; k <= 16; ++k)
                    {
                        const float u = static_cast<float> (k) / 16.0f;
                        const float px = cx0 + u * 0.36f * w, py = cy + 0.05f * h * std::sin (twoPi * (u * 2.0f + 0.25f * static_cast<float> (r)));
                        if (k == 0)
                            p.startNewSubPath (px, py);
                        else
                            p.lineTo (px, py);
                    }
                    pen.stroke (p, 1.0f - 0.08f * static_cast<float> (r));
                }
            break;
        }
        case Motif::impulse:
        {
            // An impulse and its decaying reflections, a fixed silhouette.
            Lcg rng { 0x1c0ffeeu };
            const int n = countFor (scale, 10, 18, 46);
            const float base = y + 0.82f * h;
            for (int i = 0; i < n; ++i)
            {
                const float u = static_cast<float> (i) / static_cast<float> (n - 1);
                const float px = x + (0.04f + 0.92f * u) * w;
                const float amp = (i == 0 ? 1.0f : std::exp (-3.2f * u) * (0.35f + 0.65f * rng.next()));
                juce::Path p;
                p.startNewSubPath (px, base);
                p.lineTo (px, base - amp * 0.74f * h);
                pen.stroke (p, 0.5f + 0.5f * amp);
            }
            juce::Path floor;
            floor.startNewSubPath (x + 0.02f * w, base);
            floor.lineTo (x + 0.98f * w, base);
            pen.stroke (floor, 0.6f);
            break;
        }
        case Motif::graticule:
        {
            // Meter bars of differing reach and a reference scale beside them.
            const int bars = countFor (scale, 3, 3, 5);
            for (int i = 0; i < bars; ++i)
            {
                const float bx = x + w * (0.10f + 0.16f * static_cast<float> (i));
                const float reach = 0.35f + 0.5f * (0.5f + 0.5f * std::sin (1.7f * static_cast<float> (i) + 0.6f));
                juce::Path p;
                p.startNewSubPath (bx, y + 0.88f * h);
                p.lineTo (bx, y + (0.88f - 0.76f * reach) * h);
                pen.stroke (p, 0.9f, scale == MotifScale::thumb ? 2.0f : 2.4f);
            }
            const int ticks = countFor (scale, 6, 9, 17);
            const float tx = x + w * (0.12f + 0.16f * static_cast<float> (bars));
            for (int i = 0; i < ticks; ++i)
            {
                const float ty = y + h * (0.12f + 0.76f * static_cast<float> (i) / static_cast<float> (ticks - 1));
                juce::Path p;
                p.startNewSubPath (tx, ty);
                p.lineTo (tx + (i % 4 == 0 ? 0.22f : 0.12f) * w, ty);
                pen.stroke (p, i % 4 == 0 ? 0.9f : 0.55f);
            }
            break;
        }
        case Motif::grooves:
        {
            // Offset concentric grooves (a record's, a tape pack's): two centres, close rings.
            const int n = countFor (scale, 4, 6, 13);
            for (int set = 0; set < (scale == MotifScale::thumb ? 1 : 2); ++set)
            {
                const juce::Point<float> c (x + (set == 0 ? -0.05f : 0.18f) * w, y + (set == 0 ? 1.0f : 1.15f) * h);
                for (int i = 0; i < n; ++i)
                {
                    const float r = (0.30f + 0.065f * static_cast<float> (i)) * std::max (w, h) * (set == 0 ? 1.0f : 1.25f);
                    juce::Path p;
                    p.addCentredArc (c.x, c.y, r, r, 0.0f, -0.05f * juce::MathConstants<float>::pi, 0.42f * juce::MathConstants<float>::pi, true);
                    pen.stroke (p, (set == 0 ? 1.0f : 0.5f) * (1.0f - 0.04f * static_cast<float> (i)));
                }
            }
            break;
        }
        case Motif::weave:
        {
            // Two spectral paths, each the other's offset, crossing as they travel.
            const int pairs = countFor (scale, 1, 2, 3);
            for (int pr = 0; pr < pairs; ++pr)
                for (int strand = 0; strand < 2; ++strand)
                {
                    juce::Path p;
                    for (int k = 0; k <= 40; ++k)
                    {
                        const float u = static_cast<float> (k) / 40.0f;
                        const float phase = strand == 0 ? 0.0f : juce::MathConstants<float>::pi;
                        const float py = y + h * (0.5f + (0.30f - 0.07f * static_cast<float> (pr)) * std::sin (twoPi * u * (1.2f + 0.3f * static_cast<float> (pr)) + phase));
                        if (k == 0)
                            p.startNewSubPath (x + u * w, py);
                        else
                            p.lineTo (x + u * w, py);
                    }
                    pen.stroke (p, (strand == 0 ? 1.0f : 0.7f) * (1.0f - 0.25f * static_cast<float> (pr)));
                }
            break;
        }
        case Motif::resonance:
        {
            // A resonant low-pass family: one strong peak, its quieter neighbours.
            const int n = countFor (scale, 2, 3, 5);
            for (int i = 0; i < n; ++i)
            {
                const float q = 0.45f - 0.08f * static_cast<float> (i);
                const float fc = 0.62f + 0.06f * static_cast<float> (i);
                juce::Path p;
                for (int s = 0; s <= 48; ++s)
                {
                    const float u = static_cast<float> (s) / 48.0f;
                    const float rel = (u - fc) / 0.05f;
                    float v = 0.55f - q * std::exp (-rel * rel);
                    if (u > fc)
                        v += 2.4f * (u - fc);
                    const float px = x + u * w, py = y + std::min (1.05f, v) * h;
                    if (s == 0)
                        p.startNewSubPath (px, py);
                    else
                        p.lineTo (px, py);
                }
                pen.stroke (p, 1.0f - 0.25f * static_cast<float> (i));
            }
            break;
        }
    }
}

osp::plugin::rack::Faceplate faceplateFromIdentity (const FxVisualIdentity& id)
{
    osp::plugin::rack::Faceplate f;
    f.fullBleed = true;
    f.enamelBody = true;
    f.enamel = id.rail;
    f.body = id.enamel;
    f.stripes = { id.enamel, id.enamel, id.enamel };   // (unused: the motif takes their place)
    f.glow = id.statusActive;
    f.glassTop = id.glassTop;
    f.glassBottom = id.glassBottom;
    f.subtitle = id.descriptor;
    f.ink = id.text.withAlpha (id.text.getPerceivedBrightness() > 0.5f ? 0.86f : 0.9f);
    f.inkQuiet = id.text.withAlpha (0.62f);
    f.arc = id.motifInk;
    f.motif = [id] (juce::Graphics& g, juce::Rectangle<float> zone) { paintFxMotif (g, id.motif, zone, id, MotifScale::full); };
    return f;
}

} // namespace andor::analog
