#include "AnalogRacks.h"
#include "AnalogParameters.h"
#include "Design.h"
#include "OspLookAndFeel.h"

#include <cmath>
#include <complex>

namespace andor::analog
{

using osp::plugin::InstrumentHost;
using osp::plugin::MiniKnob;
using osp::plugin::rack::GraphFrame;
using osp::plugin::rack::ParameterRack;
namespace rackui = osp::plugin::rackui;
namespace design = osp::plugin::design;
namespace fonts = osp::plugin::fonts;

//==============================================================================
namespace filtergraph
{
    double responseDb (const Settings& s, double hz) noexcept
    {
        using C = std::complex<double>;
        const C jw (0.0, hz / std::max (1.0, s.cutoffHz));
        const C G = 1.0 / (1.0 + jw);
        const C G2 = G * G, G4 = G2 * G2;
        const double r = std::clamp (s.resonance, 0.0, 1.0);
        C h;
        if (! s.ota)
            h = G4 / (1.0 + 4.15 * r * G4);   // LadderFilter at the synth's k = 4.15 RESO
        else
        {
            // OtaFilter's calibrations: k = max r (a + (1 - a) r), input 1 + comp k.
            const double maxK = s.jupiter ? 4.6 : 4.2, taper = s.jupiter ? 0.85 : 0.6, comp = s.jupiter ? 0.4 : 0.25;
            const double k = maxK * r * (taper + (1.0 - taper) * r);
            h = (1.0 + comp * k) * (s.twoPole ? G2 : G4) / (1.0 + k * G4);
        }
        // VoiceHighPass before it (its positions per voicing; -1 off).
        if (s.highPass >= 0)
        {
            const int p = std::clamp (s.highPass, 0, 3);
            double corner = 0.0;
            bool shelf = false;
            if (s.jupiter)
                corner = std::array<double, 4> { 0.0, 120.0, 350.0, 1000.0 }[static_cast<std::size_t> (p)];
            else if (s.junoVariant == 2)
            {
                shelf = p == 0;
                corner = std::array<double, 4> { 150.0, 0.0, 240.0, 720.0 }[static_cast<std::size_t> (p)];
            }
            else
                corner = std::array<double, 4> { 0.0, 225.0, 339.0, 720.0 }[static_cast<std::size_t> (p)];
            if (corner > 0.0)
            {
                const C lp = 1.0 / (1.0 + C (0.0, hz / corner));
                h *= shelf ? 1.0 + (2.0 - 1.0) * lp : 1.0 - lp;   // +6 dB low shelf, or the one-pole high-pass
            }
        }
        return 20.0 * std::log10 (std::max (1.0e-6, std::abs (h)));
    }
}

namespace
{
    float plain (InstrumentHost& h, const juce::String& id) { return h.parameterValue (id); }
    int choice (InstrumentHost& h, const juce::String& id) { return juce::roundToInt (h.parameterValue (id)); }

    /** The parameter's own text for a value (its unit added when the text lacks it). */
    MiniKnob::Formatter textOf (InstrumentHost& h, const juce::String& id)
    {
        auto* p = h.apvts().getParameter (id);
        return [p] (double v) {
            if (p == nullptr)
                return juce::String (v, 1);
            auto text = p->getText (p->convertTo0to1 (static_cast<float> (v)), 16);
            const auto label = p->getLabel();
            if (label.isNotEmpty() && ! text.containsIgnoreCase (label.trim()))
                text << " " << label;
            return text;
        };
    }

    juce::Font graphFont (float size) { return fonts::make (size, fonts::Weight::medium, 0.06f); }
    const juce::Colour graphText { 0xff94a8b0 }, graphFoot { 0xff89999e }, graphGrid { 0xff6c848d };

    //==========================================================================
    /** FILTER's graph: what the shown page is about. FILTER: the low-pass's response (the
        engine's model linearised: no saturation, before the envelope and key tracking). VOICE:
        AMP the amplitude envelope (or the gate) as it is set; MOD LFO 1's depth on each target
        and its fade-in, from the settings; CHARACTER no graph (nothing measured to draw), the
        settings' status where each acts in the voice. PAIR: what the two oscillators do to each
        other, as a schematic with the settings' values. */
    class FilterGraph final : public GraphFrame
    {
    public:
        FilterGraph (InstrumentHost& h, std::function<int()> p, std::function<int()> s) : host (h), page (std::move (p)), sub (std::move (s)) {}
        void refresh() override
        {
            juce::String sig (page() * 16 + sub());
            for (const auto& id : { params::tone, params::resonance, params::filterModel, params::otaCharacter, params::otaSlope, params::otaHpf, params::junoVariant,
                                    params::vcaMode, juce::String ("attack"), juce::String ("decay"), juce::String ("sustainLevel"), juce::String ("release"),
                                    params::polyphony, params::unison, params::sync, params::ringMix, params::blend, params::polyModEnv, params::polyModOsc2,
                                    params::polyModToPitch1, params::polyModToWidth1, params::polyModToCutoff, params::noise, params::noiseColor, params::envDepth,
                                    params::motion, params::lfoCutoff, params::lfoPitch1, params::lfoPitch2, params::lfoDelay, params::lfoRise, params::lfoWheel,
                                    params::analogAmount, params::phaseMode, params::driveBias, params::driveTrim, params::chorusMode, params::chorusNoise })
                sig << "|" << juce::String (host.parameterValue (id), 3);
            if (sig != shown)
            {
                shown = sig;
                repaint();
            }
        }

    protected:
        void paintGraph (juce::Graphics& g, juce::Rectangle<float> area) override
        {
            const auto colour = design::colour::macroOnDark (design::colour::Macro::character);
            switch (page())
            {
                case 1:
                    if (sub() == 1)
                        paintMod (g, area, colour);
                    else if (sub() == 2)
                        paintCharacter (g, area, colour);
                    else
                        paintVoice (g, area, colour);
                    break;
                case 2: paintPair (g, area, colour); break;
                default: paintFilter (g, area, colour); break;
            }
        }

    private:
        void paintFilter (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
        {
            filtergraph::Settings s;
            s.cutoffHz = 20.0 * std::pow (900.0, std::clamp (0.01 * plain (host, params::tone), 0.0, 1.0));
            s.resonance = 0.01 * plain (host, params::resonance);
            s.ota = choice (host, params::filterModel) == 1;
            s.jupiter = choice (host, params::otaCharacter) == 1;
            s.twoPole = choice (host, params::otaSlope) == 1;
            s.highPass = choice (host, params::otaHpf) - 1;
            s.junoVariant = choice (host, params::junoVariant);
            const auto plot = area.reduced (14.0f, 0.0f).withTrimmedTop (26.0f).withTrimmedBottom (34.0f);
            constexpr double lo = 20.0, hi = 20000.0, topDb = 24.0, bottomDb = -48.0;
            auto xOf = [&] (double hz) { return plot.getX() + plot.getWidth() * static_cast<float> (std::log (hz / lo) / std::log (hi / lo)); };
            auto yOf = [&] (double db) { return plot.getY() + plot.getHeight() * static_cast<float> ((topDb - std::clamp (db, bottomDb, topDb)) / (topDb - bottomDb)); };
            // Grid: decades and 0 dB.
            g.setFont (graphFont (9.0f));
            for (const double hz : { 100.0, 1000.0, 10000.0 })
            {
                g.setColour (graphGrid.withAlpha (0.22f));
                g.fillRect (juce::Rectangle<float> (xOf (hz), plot.getY(), 1.0f, plot.getHeight()));
                g.setColour (graphFoot);
                g.drawText (hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + " kHz" : juce::String (hz, 0) + " Hz", juce::Rectangle<float> (xOf (hz) - 30.0f, plot.getBottom() + 3.0f, 60.0f, 12.0f),
                            juce::Justification::centred, false);
            }
            for (const double db : { 12.0, 0.0, -12.0, -24.0, -36.0 })
            {
                g.setColour (graphGrid.withAlpha (db == 0.0 ? 0.4f : 0.16f));
                g.fillRect (juce::Rectangle<float> (plot.getX(), yOf (db), plot.getWidth(), 1.0f));
            }
            juce::Path curve, fill;
            for (int i = 0; i <= 240; ++i)
            {
                const double hz = lo * std::pow (hi / lo, i / 240.0);
                const juce::Point<float> p (xOf (hz), yOf (filtergraph::responseDb (s, hz)));
                if (i == 0)
                {
                    curve.startNewSubPath (p);
                    fill.startNewSubPath (p.x, plot.getBottom());
                }
                else
                    curve.lineTo (p);
                fill.lineTo (p);
            }
            fill.lineTo (plot.getRight(), plot.getBottom());
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.22f), 0.0f, plot.getY(), colour.withAlpha (0.03f), 0.0f, plot.getBottom(), false));
            g.fillPath (fill);
            g.setColour (colour);
            g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            // The cutoff's mark.
            const float cx = xOf (s.cutoffHz);
            g.setColour (colour.withAlpha (0.5f));
            g.fillRect (juce::Rectangle<float> (cx - 0.5f, plot.getY(), 1.0f, plot.getHeight()));
            // Captions: what it is, where it is; how the curve was made (the foot).
            g.setFont (graphFont (10.0f));
            g.setColour (graphText);
            const juce::String type = s.ota ? juce::String ("OTA ") + (s.jupiter ? "JUPITER" : "JUNO") + (s.twoPole ? juce::String::fromUTF8 (" \xc2\xb7 12 dB") : juce::String::fromUTF8 (" \xc2\xb7 24 dB"))
                                            : juce::String::fromUTF8 ("LADDER \xc2\xb7 24 dB");
            g.drawText (type, area.reduced (10.0f, 7.0f).withHeight (14.0f), juce::Justification::centredLeft, false);
            const auto cutoffText = s.cutoffHz >= 1000.0 ? juce::String (s.cutoffHz / 1000.0, 2) + " kHz" : juce::String (juce::roundToInt (s.cutoffHz)) + " Hz";
            g.drawText ("CUTOFF " + cutoffText + juce::String::fromUTF8 (" \xc2\xb7 RESO ") + juce::String (juce::roundToInt (100.0 * s.resonance)) + " %",
                        area.reduced (10.0f, 7.0f).withHeight (14.0f), juce::Justification::centredRight, false);
            g.setFont (graphFont (8.5f));
            g.setColour (graphFoot.withAlpha (0.8f));
            g.drawText ("small-signal response: before the envelope and key tracking", area.reduced (10.0f, 4.0f).withTop (area.getBottom() - 12.0f), juce::Justification::centredLeft, false);
        }

        void paintVoice (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
        {
            // The amplitude as it is set: the AMP envelope (A D S R), or the gate.
            const bool gate = choice (host, params::vcaMode) == 1;
            const auto plot = area.reduced (16.0f, 0.0f).withTrimmedTop (30.0f).withTrimmedBottom (26.0f);
            g.setColour (graphGrid.withAlpha (0.25f));
            g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getBottom(), plot.getWidth(), 1.0f));
            juce::Path p;
            if (gate)
            {
                const float x0 = plot.getX() + 0.08f * plot.getWidth(), x1 = plot.getX() + 0.78f * plot.getWidth();
                p.startNewSubPath (plot.getX(), plot.getBottom());
                p.lineTo (x0, plot.getBottom());
                p.lineTo (x0, plot.getY());
                p.lineTo (x1, plot.getY());
                p.lineTo (x1, plot.getBottom());
                p.lineTo (plot.getRight(), plot.getBottom());
            }
            else
            {
                // Each stage's width by its time (square-root scale: short times stay visible).
                auto seconds = [this] (const char* id) {
                    auto* param = host.apvts().getParameter (id);
                    return param != nullptr ? std::max (0.0, static_cast<double> (param->convertFrom0to1 (param->getValue()))) : 0.1;
                };
                const double a = std::sqrt (seconds ("attack")), d = std::sqrt (seconds ("decay")), r = std::sqrt (seconds ("release"));
                const double hold = 0.6 * (a + d + r) + 1.0e-3;
                const double total = a + d + hold + r + 1.0e-6;
                const float sustain = juce::jlimit (0.0f, 1.0f, 0.01f * plain (host, "sustainLevel"));
                auto x = [&] (double t) { return plot.getX() + plot.getWidth() * static_cast<float> (t / total); };
                const float ys = plot.getBottom() - sustain * plot.getHeight();
                p.startNewSubPath (plot.getX(), plot.getBottom());
                p.lineTo (x (a), plot.getY());
                p.quadraticTo (x (a + 0.3 * d), ys, x (a + d), ys);
                p.lineTo (x (a + d + hold), ys);
                p.quadraticTo (x (a + d + hold + 0.3 * r), plot.getBottom(), x (total), plot.getBottom());
            }
            juce::Path fill (p);
            fill.lineTo (plot.getRight(), plot.getBottom());
            fill.closeSubPath();
            g.setColour (colour.withAlpha (0.12f));
            g.fillPath (fill);
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setFont (graphFont (10.0f));
            g.setColour (graphText);
            g.drawText (gate ? juce::String::fromUTF8 ("AMP \xc2\xb7 GATE") : juce::String::fromUTF8 ("AMP \xc2\xb7 ENVELOPE"), area.reduced (10.0f, 7.0f).withHeight (14.0f),
                        juce::Justification::centredLeft, false);
            auto* voices = host.apvts().getParameter (params::polyphony);
            const juce::String count = voices != nullptr ? voices->getCurrentValueAsText() : juce::String();
            g.drawText ("VOICES " + count + (plain (host, params::unison) >= 0.5f ? juce::String::fromUTF8 (" \xc2\xb7 UNISON") : juce::String()),
                        area.reduced (10.0f, 7.0f).withHeight (14.0f), juce::Justification::centredRight, false);
            if (! gate)
            {
                g.setFont (graphFont (8.5f));
                g.setColour (graphFoot.withAlpha (0.8f));
                g.drawText ("the AMP panel's envelope (time on a square-root scale)", area.reduced (10.0f, 4.0f).withTop (area.getBottom() - 12.0f), juce::Justification::centredLeft, false);
            }
        }

        void paintPair (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
        {
            // A schematic (not an analysis): the two oscillators, what links them, their values.
            // The schematic on the left, the values on the right (the graph is 160 px high).
            auto inner = area.reduced (14.0f, 0.0f).withTrimmedTop (28.0f).withTrimmedBottom (20.0f);
            auto readouts = inner.removeFromRight (0.46f * inner.getWidth());
            inner.removeFromRight (10.0f);
            const float bw = std::min (64.0f, 0.36f * inner.getWidth()), bh = 24.0f;
            const juce::Rectangle<float> osc1 (inner.getX(), inner.getY() + 2.0f, bw, bh), osc2 (inner.getX(), inner.getBottom() - bh - 2.0f, bw, bh);
            const juce::Rectangle<float> mix (inner.getRight() - bw, inner.getCentreY() - 0.5f * bh, bw, bh);
            auto box = [&g] (juce::Rectangle<float> b, const juce::String& text, juce::Colour c) {
                g.setColour (c.withAlpha (0.16f));
                g.fillRoundedRectangle (b, 4.0f);
                g.setColour (c.withAlpha (0.85f));
                g.drawRoundedRectangle (b, 4.0f, 1.2f);
                g.setFont (graphFont (10.0f));
                g.drawText (text, b, juce::Justification::centred, false);
            };
            box (osc1, "OSC 1", colour);
            box (osc2, "OSC 2", colour);
            box (mix, "MIX", colour);
            auto arrow = [&g] (juce::Point<float> a, juce::Point<float> b, juce::Colour c, bool lit, bool dashed) {
                g.setColour (c.withAlpha (lit ? 0.9f : 0.3f));
                juce::Path line;
                line.startNewSubPath (a);
                line.lineTo (b);
                if (dashed)
                {
                    juce::Path d;
                    const float dash[] { 4.0f, 3.0f };
                    juce::PathStrokeType (1.3f).createDashedStroke (d, line, dash, 2);
                    g.fillPath (d);
                }
                else
                    g.strokePath (line, juce::PathStrokeType (1.3f));
                g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (b));
            };
            arrow ({ osc1.getRight(), osc1.getCentreY() }, { mix.getX(), mix.getCentreY() - 6.0f }, colour, true, false);
            arrow ({ osc2.getRight(), osc2.getCentreY() }, { mix.getX(), mix.getCentreY() + 6.0f }, colour, true, false);
            // SYNC: OSC 2 restarts OSC 1 (dashed and quiet while off).
            const bool sync = plain (host, params::sync) >= 0.5f;
            arrow ({ osc2.getCentreX(), osc2.getY() }, { osc1.getCentreX(), osc1.getBottom() }, colour, sync, ! sync);
            g.setFont (graphFont (9.0f));
            g.setColour (graphText.withAlpha (sync ? 1.0f : 0.5f));
            g.drawText (sync ? "SYNC" : "SYNC OFF", juce::Rectangle<float> (osc1.getCentreX() + 6.0f, osc1.getBottom(), 80.0f, osc2.getY() - osc1.getBottom()), juce::Justification::centredLeft, false);
            // The values, one line each.
            const auto pct = [this] (const juce::String& id) { return juce::String (juce::roundToInt (plain (host, id))) + " %"; };
            juce::StringArray to;
            if (plain (host, params::polyModToPitch1) >= 0.5f)
                to.add ("PITCH 1");
            if (plain (host, params::polyModToWidth1) >= 0.5f)
                to.add ("WIDTH 1");
            if (plain (host, params::polyModToCutoff) >= 0.5f)
                to.add ("FILTER");
            const juce::StringArray lines { "RING " + pct (params::ringMix) + "   BLEND " + pct (params::blend),
                                            "POLY MOD  ENV " + pct (params::polyModEnv) + "  OSC 2 " + pct (params::polyModOsc2),
                                            juce::String::fromUTF8 ("\xe2\x86\x92 ") + (to.isEmpty() ? juce::String ("no destination") : to.joinIntoString (", ")),
                                            "NOISE " + pct (params::noise) + "  " + (choice (host, params::noiseColor) == 1 ? "JUNO" : "WHITE") };
            g.setFont (graphFont (9.0f));
            g.setColour (graphText);
            const float step = std::min (18.0f, readouts.getHeight() / 4.0f);
            for (int i = 0; i < lines.size(); ++i)
                g.drawText (lines[i], readouts.withHeight (step).translated (0.0f, step * static_cast<float> (i) + 0.5f * (readouts.getHeight() - 4.0f * step)),
                            juce::Justification::centredLeft, true);
            g.setFont (graphFont (10.0f));
            g.drawText ("OSC 1 + OSC 2", area.reduced (10.0f, 7.0f).withHeight (14.0f), juce::Justification::centredLeft, false);
            g.setFont (graphFont (8.5f));
            g.setColour (graphFoot.withAlpha (0.8f));
            g.drawText ("a schematic of the pair's links, with their settings", area.reduced (10.0f, 4.0f).withTop (area.getBottom() - 12.0f), juce::Justification::centredLeft, false);
        }

        void paintMod (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
        {
            // LFO 1 as the engine applies it: each target swings by AMOUNT x its depth (the wheel,
            // when linked, brings in the rest up to the full depth); DELAY then RISE fade it in.
            using namespace params;
            const float amount = juce::jlimit (0.0f, 1.0f, 0.01f * plain (host, motion));
            const bool wheel = plain (host, lfoWheel) >= 0.5f;
            auto body = area.reduced (14.0f, 0.0f).withTrimmedTop (30.0f).withTrimmedBottom (24.0f);
            auto fade = body.removeFromRight (0.36f * body.getWidth());
            body.removeFromRight (18.0f);
            struct Row
            {
                const juce::String& id;
                const char* name;
                float range;   // the parameter's span either way (st)
            };
            const Row rows[] { { lfoCutoff, "CUTOFF", 48.0f }, { lfoPitch1, "PITCH 1", 12.0f }, { lfoPitch2, "PITCH 2", 12.0f } };
            const float rowH = body.getHeight() / 3.0f;
            g.setFont (graphFont (9.0f));
            for (int i = 0; i < 3; ++i)
            {
                auto r = body.withHeight (rowH).translated (0.0f, rowH * static_cast<float> (i));
                const auto label = r.removeFromLeft (48.0f), value = r.removeFromRight (52.0f);
                const auto track = r.withSizeKeepingCentre (r.getWidth() - 8.0f, 8.0f);
                const float setting = plain (host, rows[i].id), now = amount * setting;
                const float half = 0.5f * track.getWidth(), cx = track.getCentreX();
                g.setColour (graphGrid.withAlpha (0.14f));
                g.fillRoundedRectangle (track, 2.0f);
                g.setColour (graphGrid.withAlpha (0.5f));
                g.fillRect (juce::Rectangle<float> (cx - 0.5f, track.getY() - 3.0f, 1.0f, track.getHeight() + 6.0f));
                auto span = [&] (float st) {
                    const float w = half * juce::jlimit (-1.0f, 1.0f, st / rows[i].range);
                    return juce::Rectangle<float> (std::min (cx, cx + w), track.getY(), std::abs (w), track.getHeight());
                };
                if (wheel && std::abs (setting) > std::abs (now))
                {
                    g.setColour (colour.withAlpha (0.55f));
                    g.drawRoundedRectangle (span (setting).reduced (0.5f), 2.0f, 1.0f);
                }
                g.setColour (colour.withAlpha (now != 0.0f ? 0.9f : 0.0f));
                g.fillRoundedRectangle (span (now), 2.0f);
                const bool any = std::abs (setting) > 1.0e-3f;
                g.setColour (graphText.withAlpha (any ? 1.0f : 0.55f));
                g.drawText (rows[i].name, label, juce::Justification::centredLeft, false);
                g.drawText (any ? (now >= 0.0f ? "+" : "") + juce::String (now, std::abs (now) < 10.0f ? 2 : 1) + " st" : juce::String ("off"), value, juce::Justification::centredRight, false);
            }
            // The fade-in: silent for DELAY, then up over RISE (time to scale).
            const float delay = std::max (0.0f, plain (host, lfoDelay)), rise = std::max (0.0f, plain (host, lfoRise));
            const float span = std::max (0.5f, 1.3f * (delay + rise));
            auto plot = fade.withTrimmedTop (14.0f).withTrimmedBottom (12.0f);
            auto xOf = [&] (float t) { return plot.getX() + plot.getWidth() * std::min (1.0f, t / span); };
            g.setColour (graphGrid.withAlpha (0.3f));
            g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getBottom(), plot.getWidth(), 1.0f));
            juce::Path p;
            p.startNewSubPath (plot.getX(), plot.getBottom());
            p.lineTo (xOf (delay), plot.getBottom());
            p.lineTo (xOf (delay + rise), plot.getY());
            p.lineTo (plot.getRight(), plot.getY());
            juce::Path fill (p);
            fill.lineTo (plot.getRight(), plot.getBottom());
            fill.closeSubPath();
            g.setColour (colour.withAlpha (0.12f));
            g.fillPath (fill);
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
            g.setFont (graphFont (8.5f));
            g.setColour (graphText);
            g.drawText ("FADE-IN", fade.withHeight (12.0f), juce::Justification::centredLeft, false);
            auto seconds = [] (float t) { return juce::String (t, t < 1.0f ? 2 : 1) + " s"; };
            g.setColour (graphFoot);
            g.drawText ("DELAY " + seconds (delay) + juce::String::fromUTF8 (" \xc2\xb7 RISE ") + seconds (rise), fade.withTop (fade.getBottom() - 11.0f),
                        juce::Justification::centredLeft, false);
            g.setFont (graphFont (10.0f));
            g.setColour (graphText);
            g.drawText (juce::String::fromUTF8 ("LFO 1 \xc2\xb7 AMOUNT ") + juce::String (juce::roundToInt (100.0f * amount)) + " %", area.reduced (10.0f, 7.0f).withHeight (14.0f),
                        juce::Justification::centredLeft, false);
            g.drawText (wheel ? "WHEEL LINKED" : "WHEEL OFF", area.reduced (10.0f, 7.0f).withHeight (14.0f), juce::Justification::centredRight, false);
            g.setFont (graphFont (8.5f));
            g.setColour (graphFoot.withAlpha (0.8f));
            g.drawText (wheel ? "solid: the swing now (AMOUNT x depth) - outline: with the wheel up" : "the swing on each target: AMOUNT x its depth",
                        area.reduced (10.0f, 4.0f).withTop (area.getBottom() - 12.0f), juce::Justification::centredLeft, false);
        }

        void paintCharacter (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
        {
            // Nothing here is measured, so no graph: where each setting acts in the voice, and
            // its value (the chorus quiet while off).
            using namespace params;
            auto text = [this] (const juce::String& id) { return textOf (host, id) (plain (host, id)).toUpperCase().replace ("DB", "dB"); };
            const bool chorus = choice (host, chorusMode) > 0;
            struct Stage
            {
                const char* title;
                juce::StringArray lines;
                bool active;
            };
            const Stage stages[] { { "VOICE", { "ANALOG " + text (analogAmount), "PHASE " + text (phaseMode) }, true },
                                   { "FILTER DRIVE", { "BIAS " + text (driveBias), "TRIM " + text (driveTrim) }, true },
                                   { "CHORUS", { chorus ? text (chorusMode) : juce::String ("OFF"), "HISS " + text (chorusNoise) }, chorus } };
            auto row = area.reduced (14.0f, 0.0f).withTrimmedTop (34.0f).withTrimmedBottom (30.0f);
            const float arrow = 18.0f, w = (row.getWidth() - 2.0f * arrow) / 3.0f;
            for (int i = 0; i < 3; ++i)
            {
                const auto& st = stages[i];
                const auto box = juce::Rectangle<float> (row.getX() + static_cast<float> (i) * (w + arrow), row.getY(), w, row.getHeight());
                g.setColour (colour.withAlpha (st.active ? 0.1f : 0.03f));
                g.fillRoundedRectangle (box, 4.0f);
                g.setColour (colour.withAlpha (st.active ? 0.6f : 0.25f));
                g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.0f);
                g.setFont (graphFont (9.0f));
                g.setColour (graphText.withAlpha (st.active ? 1.0f : 0.5f));
                g.drawText (st.title, box.reduced (8.0f, 6.0f).withHeight (12.0f), juce::Justification::centredLeft, false);
                g.setFont (graphFont (9.5f));
                g.setColour ((st.active ? juce::Colour (0xffd5e0e3) : graphText.withAlpha (0.5f)));
                for (int l = 0; l < st.lines.size(); ++l)
                    g.drawText (st.lines[l], box.reduced (8.0f, 0.0f).withY (box.getY() + 30.0f + 16.0f * static_cast<float> (l)).withHeight (14.0f),
                                juce::Justification::centredLeft, true);
                if (i < 2)
                {
                    const float y = box.getCentreY(), x0 = box.getRight() + 4.0f, x1 = box.getRight() + arrow - 4.0f;
                    g.setColour (graphGrid.withAlpha (0.8f));
                    g.fillRect (juce::Rectangle<float> (x0, y - 0.5f, x1 - x0 - 3.0f, 1.0f));
                    juce::Path head;
                    head.addTriangle (x1, y, x1 - 4.0f, y - 3.0f, x1 - 4.0f, y + 3.0f);
                    g.fillPath (head);
                }
            }
            g.setFont (graphFont (10.0f));
            g.setColour (graphText);
            g.drawText ("CHARACTER", area.reduced (10.0f, 7.0f).withHeight (14.0f), juce::Justification::centredLeft, false);
            g.setFont (graphFont (8.5f));
            g.setColour (graphFoot.withAlpha (0.8f));
            g.drawText ("where each setting acts in the voice, as set (nothing measured)", area.reduced (10.0f, 4.0f).withTop (area.getBottom() - 12.0f),
                        juce::Justification::centredLeft, false);
        }

        InstrumentHost& host;
        std::function<int()> page, sub;
        juce::String shown;
    };

    //==========================================================================
    int filterPageShown = 0, voicePageShown = 0;   // (interface only: the last pages looked at)

    class FilterRack final : public ParameterRack
    {
    public:
        explicit FilterRack (InstrumentHost& h) : ParameterRack (h, "FILTER", design::colour::macro (design::colour::Macro::character), 0.68f)
        {
            setPages ({ "FILTER", "VOICE", "PAIR" }, &filterPageShown);
            setSubPages (1, { "AMP", "MOD", "CHARACTER" }, &voicePageShown);
            setGraph (std::make_unique<FilterGraph> (h, [this] { return page(); }, [this] { return subPage(); }));
        }

    protected:
        int buildKey() const override
        {
            // FILTER's row follows the model, the high-pass (on or off), the voicing and the
            // envelope link; the other pages, nothing.
            if (page() != 0)
                return 0;
            return choice (host, params::filterModel) * 8 + (choice (host, params::otaHpf) > 0 ? 4 : 0) + choice (host, params::otaCharacter) * 2
                 + (plain (host, params::envLink) >= 0.5f ? 1 : 0);
        }
        void build (int p) override
        {
            using namespace params;
            const rackui::Position on { "ON", "On", 1.0f }, off { "OFF", "Off", 0.0f };
            if (p == 0)
            {
                // The filter: what it is in the selector row, only what the chosen model uses -
                // TYPE; the OTA's SLOPE; the VOICING (the OTA's, and the high-pass's curve) and
                // which JUNO, whenever either acts; the high-pass - then one row: RESO ENV KEYS
                // DRIVE and the envelope (resting while ENV LINK lets the AMP envelope shape it).
                const bool ota = choice (host, filterModel) == 1, linked = plain (host, envLink) >= 0.5f;
                const bool highPass = choice (host, otaHpf) > 0, jupiter = choice (host, otaCharacter) == 1;
                selectorSegment (filterModel, "TYPE", { { "LADDER", "Ladder", 0.0f }, { "OTA", "OTA", 1.0f } }, 104,
                                 "The low-pass: the original four-pole LADDER, or the OTA family (after the Juno and Jupiter-8)");
                if (ota)
                    selectorSegment (otaSlope, "SLOPE", { { "24 dB", "24 dB", 0.0f }, { "12 dB", "12 dB", 1.0f } }, 88,
                                     "The slope: four poles, or the Jupiter-8's two-pole tap (resonance stays)");
                if (ota || highPass)
                    selectorSegment (otaCharacter, "VOICING", { { "JUNO", "Juno", 0.0f }, { "JUPITER", "Jupiter", 1.0f } }, 108,
                                     ota ? "The OTA's voicing and the high-pass's curve: JUNO smoother, JUPITER brighter and more resonant"
                                         : "The high-pass's curve: the Juno's (as JUNO sets) or the Jupiter-8's");
                if ((ota || highPass) && ! jupiter)
                    selectorList (junoVariant, "JUNO", 60, "Which Juno: its high-pass positions (106: a bass boost) and the OTA's calibration");
                selectorList (otaHpf, "HPF", 60, "The high-pass before the filter: off, or its positions (Juno-106: 0 bass boost, 1 flat, 2 and 3 cut; the others: 0 flat, 1-3 cut)");
                knob (resonance, "RESO", textOf (host, resonance));
                knob (envDepth, "ENV", textOf (host, envDepth));
                knob (keyTrack, "KEYS", textOf (host, keyTrack));
                knob (filterDrive, "DRIVE", textOf (host, filterDrive));
                group (linked ? juce::String::fromUTF8 ("ENVELOPE \xc2\xb7 LINKED: THE AMP ENVELOPE SHAPES IT") : juce::String ("ENVELOPE"));
                for (const auto& [id, caption] : { std::pair { filterAttack, "ATTACK" }, std::pair { filterDecay, "DECAY" }, std::pair { filterSustain, "SUSTAIN" },
                                                   std::pair { filterRelease, "RELEASE" } })
                {
                    auto& k = knob (id, caption, textOf (host, id));
                    k.setEnabled (! linked);
                    k.setAlpha (linked ? 0.45f : 1.0f);
                }
                knob (velocityToFilter, "VELO", textOf (host, velocityToFilter));
            }
            else if (p == 1)
            {
                switch (subPage())
                {
                    case 0:   // AMP: the amplifier, the envelope link, the voices
                        segmentCell (vcaMode, "AMP", { { "ENV", "Envelope", 0.0f }, { "GATE", "Gate", 1.0f } },
                                     "The amplitude: its envelope (AMP), or a gate that opens and closes with the key");
                        switchCell (envLink, "ENV LINK", { on, off }, "One envelope for both, as on a Juno-106: the AMP envelope shapes the filter too");
                        group ({});
                        steppedKnob (polyphony, "VOICES", textOf (host, polyphony));
                        switchCell (unison, "UNISON", { on, off }, "Every voice on one note, detuned: a thick mono sound");
                        knob (unisonDetune, "DETUNE", textOf (host, unisonDetune));
                        knob (spread, "SPREAD", textOf (host, spread));
                        break;
                    case 1:   // MOD: LFO 1's depths (its shape and rate: the modulation panel)
                        group ("LFO 1");
                        knob (motion, "AMOUNT", textOf (host, motion));
                        knob (lfoCutoff, "CUTOFF", textOf (host, lfoCutoff));
                        knob (lfoPitch1, "PITCH 1", textOf (host, lfoPitch1));
                        knob (lfoPitch2, "PITCH 2", textOf (host, lfoPitch2));
                        knob (lfoDelay, "DELAY", textOf (host, lfoDelay));
                        knob (lfoRise, "RISE", textOf (host, lfoRise));
                        group ({});
                        switchCell (lfoWheel, "WHEEL", { on, off },
                                    "The mod wheel brings in the rest of these depths (the classic link). Off: the wheel moves only what you route it to");
                        break;
                    default:   // CHARACTER: the analog character, phase, bias, trim; the chorus and its hiss
                        knob (analogAmount, "ANALOG", textOf (host, analogAmount));
                        segmentCell (phaseMode, "PHASE", { { "FREE", "Free", 0.0f }, { "RESET", "Reset", 1.0f } },
                                     "Oscillators free-running (each note starts anywhere) or restarted at every note");
                        knob (driveBias, "BIAS", textOf (host, driveBias));
                        knob (driveTrim, "TRIM", textOf (host, driveTrim));
                        group ({});
                        segmentCell (chorusMode, "CHORUS", { { "OFF", "Off", 0.0f }, { "I", "I", 1.0f }, { "II", "II", 2.0f }, { "I+II", "I and II", 3.0f } },
                                     "A Juno-style bucket-brigade chorus on the whole sound");
                        switchCell (chorusNoise, "HISS", { on, off }, "The chorus' own faint hiss");
                        break;
                }
            }
            else
            {
                // The pair: sync, ring, blend; Poly Mod's sources and its three destinations
                // (each its own switch: any of them at once); the noise, its colour over it.
                selectorSegment (noiseColor, "COLOR", { { "WHITE", "White", 0.0f }, { "JUNO", "Juno", 1.0f } }, 104, "The noise's colour: white, or the Juno's darker hiss",
                                 "NOISE");
                switchCell (sync, "SYNC", { on, off }, "Hard sync: oscillator 2 restarts oscillator 1 every cycle (tune oscillator 1 up for the classic sweep)");
                knob (ringMix, "RING", textOf (host, ringMix));
                knob (blend, "BLEND", textOf (host, blend));
                group ("POLY MOD");
                knob (polyModEnv, "FILT ENV", textOf (host, polyModEnv));
                knob (polyModOsc2, "OSC 2", textOf (host, polyModOsc2));
                switchCell (polyModToPitch1, "PITCH 1", { on, off }, "Poly Mod moves oscillator 1's pitch (FM with oscillator 2, sweeps with the envelope)");
                switchCell (polyModToWidth1, "WIDTH 1", { on, off }, "Poly Mod moves oscillator 1's pulse width");
                switchCell (polyModToCutoff, "FILTER", { on, off }, "Poly Mod moves the filter's cutoff");
                group ({});
                knob (noise, "NOISE", textOf (host, noise));
            }
        }
    };

    //==========================================================================
    /** MORPH's graph: a concept view of the method at the MORPH position, drawn from the
        settings alone (the engine does not publish its analysis to the interface yet), and
        labelled as such: the carrier's harmonics (cool), card C's (copper), the result. With
        nothing in card C to morph with, it is the module's empty state instead: what to do, and
        the engine's own words for why. */
    class MorphGraph final : public GraphFrame
    {
    public:
        explicit MorphGraph (InstrumentHost& h) : host (h) {}
        void refresh() override
        {
            juce::String sig;
            for (const auto& id : { params::morphEnabled, params::morphPosition, params::morphMethod, params::morphCarrier, params::morphTexture, params::morphDetail,
                                    params::morphFormants, params::morphOutput })
                sig << "|" << juce::String (host.parameterValue (id), 3);
            sig << host.engineStatus ("morph");
            if (sig != shown)
            {
                shown = sig;
                repaint();
            }
        }

    protected:
        void paintGraph (juce::Graphics& g, juce::Rectangle<float> area) override
        {
            const auto cool = juce::Colour (0xff9dbdd0), copper = design::colour::macroOnDark (design::colour::Macro::life);
            const bool on = plain (host, params::morphEnabled) >= 0.5f;
            const bool silent = host.engineStatus ("morph").isNotEmpty();
            const float p = juce::jlimit (0.0f, 1.0f, 0.01f * plain (host, params::morphPosition));
            const int method = choice (host, params::morphMethod);
            const int partials = juce::jlimit (8, 48, juce::roundToInt (8.0f + 40.0f * 0.01f * plain (host, params::morphDetail)));
            const float formants = plain (host, params::morphFormants);   // semitones
            const float texture = 0.01f * plain (host, params::morphTexture);
            const auto plot = area.reduced (16.0f, 0.0f).withTrimmedTop (30.0f).withTrimmedBottom (26.0f);
            static const char* methods[] { "IMPRINT", "FUSE", "WEAVE" };
            const auto title = juce::String (methods[std::clamp (method, 0, 2)]) + juce::String::fromUTF8 (" \xc2\xb7 MORPH ") + juce::String (juce::roundToInt (100.0f * p)) + " %";
            if (silent)
            {
                g.setFont (graphFont (10.0f));
                g.setColour (graphText.withAlpha (0.45f));
                g.drawText (title, area.reduced (10.0f, 7.0f).withHeight (14.0f), juce::Justification::centredLeft, false);
                g.setColour (graphGrid.withAlpha (0.25f));
                g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getBottom(), plot.getWidth(), 1.0f));
                const auto message = area.reduced (28.0f, 0.0f).withSizeKeepingCentre (area.getWidth() - 56.0f, 52.0f);
                g.setFont (graphFont (11.0f));
                g.setColour (juce::Colour (0xffd5e0e3));
                g.drawText ("Load an eligible sample into C to morph", message.withHeight (16.0f), juce::Justification::centred, false);
                g.setFont (graphFont (9.0f));
                g.setColour (graphFoot);
                g.drawFittedText (host.engineStatus ("morph"), message.withTrimmedTop (22.0f).toNearestInt(), juce::Justification::centredTop, 2, 1.0f);
                return;
            }
            const int shownPartials = std::min (partials, 32);
            const float slot = plot.getWidth() / static_cast<float> (shownPartials);
            // Two illustrative spectra: the carrier's (1/n) and card C's (a formant hump,
            // moved by FORMANTS), and the result by the method at this position.
            auto carrierDb = [] (int n) { return -20.0f * std::log10 (static_cast<float> (n)); };
            auto sourceDb = [formants] (int n) {
                const float centre = 4.0f * std::pow (2.0f, formants / 12.0f);
                const float d = std::log2 (static_cast<float> (n) / centre);
                return -6.0f * d * d - 2.0f * std::log10 (static_cast<float> (n));
            };
            auto yOf = [&] (float db) { return plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 36.0f) / 36.0f); };
            for (int n = 1; n <= shownPartials; ++n)
            {
                const float x = plot.getX() + slot * (static_cast<float> (n) - 0.5f);
                const float a = carrierDb (n), b = sourceDb (n);
                float result = a;
                juce::Colour rc = cool;
                if (method == 0)   // IMPRINT: the carrier's partials under C's envelope
                {
                    result = a + p * (b - a);
                    rc = cool.interpolatedWith (copper, 0.5f * p);
                }
                else if (method == 1)   // FUSE: levels blended
                {
                    result = (1.0f - p) * a + p * b;
                    rc = cool.interpolatedWith (copper, p);
                }
                else   // WEAVE: groups from one or the other
                {
                    const bool fromC = ((n - 1) / 3) % 2 == 1 ? p > 0.25f : p > 0.75f;
                    result = fromC ? b : a;
                    rc = fromC ? copper : cool;
                }
                g.setColour (cool.withAlpha (0.18f));
                g.fillRect (juce::Rectangle<float> (x - 0.32f * slot, yOf (a), 0.2f * slot, plot.getBottom() - yOf (a)));
                g.setColour (copper.withAlpha (0.18f));
                g.fillRect (juce::Rectangle<float> (x + 0.12f * slot, yOf (b), 0.2f * slot, plot.getBottom() - yOf (b)));
                g.setColour (rc.withAlpha (on ? 0.95f : 0.4f));
                g.fillRect (juce::Rectangle<float> (x - 0.08f * slot, yOf (result), 0.16f * slot + 1.0f, plot.getBottom() - yOf (result)));
            }
            // TEXTURE: the residual's share, as a faint floor.
            g.setColour (copper.withAlpha (0.12f * texture));
            g.fillRect (plot.withTop (plot.getBottom() - 0.18f * plot.getHeight()));
            g.setColour (graphGrid.withAlpha (0.3f));
            g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getBottom(), plot.getWidth(), 1.0f));
            g.setFont (graphFont (10.0f));
            g.setColour (graphText);
            g.drawText (title, area.reduced (10.0f, 7.0f).withHeight (14.0f), juce::Justification::centredLeft, false);
            g.setColour (cool);
            g.drawText ("OSC", area.reduced (10.0f, 7.0f).withHeight (14.0f).withTrimmedRight (40.0f), juce::Justification::centredRight, false);
            g.setColour (copper);
            g.drawText ("C", area.reduced (10.0f, 7.0f).withHeight (14.0f), juce::Justification::centredRight, false);
            g.setFont (graphFont (8.5f));
            g.setColour (graphFoot.withAlpha (0.85f));
            g.drawText (on ? "concept view from the settings - not an analysis of the sound" : "MORPH is off: the sources play as they are",
                        area.reduced (10.0f, 4.0f).withTop (area.getBottom() - 12.0f), juce::Justification::centredLeft, false);
        }

    private:
        InstrumentHost& host;
        juce::String shown;
    };

    class MorphRack final : public ParameterRack
    {
    public:
        explicit MorphRack (InstrumentHost& h) : ParameterRack (h, "MORPH", design::colour::macro (design::colour::Macro::life), 0.68f)
        {
            using namespace params;
            setHeaderSwitch (morphEnabled, "ON", "MORPH: the oscillators and card C become one sound (at 0 % the oscillators, at 100 % card C, between them a fusion)");
            setGraph (std::make_unique<MorphGraph> (h));
        }

    protected:
        void build (int) override
        {
            // METHOD and CARRIER in the selector row; the five knobs that shape the morph in one
            // row; then, a rank lower (lighter captions and values, its own caption), the setup:
            // what comes out, the level match, card C's root. (MORPH 2.0's controls will take
            // pages - MORPH / IDENTITY / MOTION - once they exist, never a taller rack.)
            using namespace params;
            selectorSegment (morphMethod, "METHOD", { { "IMPRINT", "Imprint", 0.0f }, { "FUSE", "Fuse", 1.0f }, { "WEAVE", "Weave", 2.0f } }, 210,
                             "IMPRINT: card C's envelope on the oscillators. FUSE: their partials and pitch blended. WEAVE: their partials interleaved");
            selectorSegment (morphCarrier, "CARRIER", { { "OSC 1", "Oscillator 1", 0.0f }, { "OSC 2", "Oscillator 2", 1.0f }, { "BOTH", "Both", 2.0f } }, 180,
                             "The analog side: oscillator 1, oscillator 2, or the pair as levelled");
            knob (morphPosition, "MORPH", textOf (host, morphPosition));
            knob (morphTexture, "TEXTURE", textOf (host, morphTexture));
            knob (morphAttack, "ATTACK", textOf (host, morphAttack))
                .slider.setTooltip ("How much of card C's own attack (its onset transient) the morph keeps - not an envelope time");
            knob (morphFormants, "FORMANTS", textOf (host, morphFormants));
            knob (morphDetail, "DETAIL", textOf (host, morphDetail));
            group ("SETUP", true);
            switchCell (morphOutput, "OUTPUT", { { "MORPH", "Morph only", 0.0f }, { "LAYERED", "Layered", 1.0f } },
                        "MORPH ONLY: the morph replaces the oscillators and card C. LAYERED: it joins them");
            switchCell (morphLevelMatch, "LEVEL MATCH", { { "ON", "On", 1.0f }, { "OFF", "Off", 0.0f } }, "Holds the loudness steady between the oscillators' and card C's");
            knob (organicRoot, "ROOT", [] (double v) {
                const int note = juce::roundToInt (v);
                return note < 0 ? juce::String ("AUTO") : juce::MidiMessage::getMidiNoteName (note, true, true, 4);
            });
        }
    };
}

std::unique_ptr<osp::plugin::rack::Module> createFilterRack (InstrumentHost& host) { return std::make_unique<FilterRack> (host); }
std::unique_ptr<osp::plugin::rack::Module> createMorphRack (InstrumentHost& host) { return std::make_unique<MorphRack> (host); }

} // namespace andor::analog
