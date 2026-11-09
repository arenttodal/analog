#include "FxRack.h"

#include <complex>
#include "FxIdentity.h"
#include "AnalogEngine.h"
#include "FxParameters.h"
#include "FxPresets.h"
#include "FxRouting.h"
#include "OspLookAndFeel.h"
#include "RackControls.h"
#include "ShellEditor.h"
#include "ShellProcessor.h"
#include "EqEditor.h"
#include "fx/Effects.h"
#include "engine/DriveProcessor.h"

namespace andor::analog
{

namespace fonts = osp::plugin::fonts;
namespace rackui = osp::plugin::rackui;
using fx::InstanceId;
using fx::Type;
using osp::plugin::MiniKnob;
using osp::plugin::rack::GraphFrame;
using osp::plugin::rack::ParameterRack;

namespace
{
    ParameterRack::Faceplate faceplateFor (Type type);
    InstanceId shownInstance { Type::eq, 0 };
    std::array<int, fx::totalInstances()> fxPageMemory {};
    std::array<juce::String, fx::totalInstances()> presetNames;

    AnalogEngine& engineOf (osp::plugin::InstrumentHost& host) { return dynamic_cast<AnalogEngine&> (dynamic_cast<andor::shell::ShellProcessor&> (host).productEngine()); }

    juce::String pid (InstanceId id, const char* name) { return fxparams::idOf (id, name); }
    float value (osp::plugin::InstrumentHost& host, InstanceId id, const char* name) { return host.parameterValue (pid (id, name)); }

    MiniKnob::Formatter textOf (osp::plugin::InstrumentHost& host, const juce::String& id)
    {
        auto* p = host.apvts().getParameter (id);
        return [p] (double v) { return p != nullptr ? p->getText (p->convertTo0to1 (static_cast<float> (v)), 16) : juce::String (v, 1); };
    }

    // The graphs' type and lines: the approved ECHO graph's (as AnalogRacks' graphs).
    juce::Font meta() { return fonts::make (10.0f, fonts::Weight::regular, 0.02f); }
    juce::Font label() { return fonts::make (9.5f, fonts::Weight::medium, 0.06f); }
    const juce::Colour metaColour { 0xd9c2c7c5 }, labelColour { 0xcc9aa09e }, grid { 0x0dffffff }, axis { 0x12ffffff };

    void header (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& left, const juce::String& right)
    {
        const juce::Rectangle<float> line (area.getX() + 9.0f, area.getY() + 4.0f, area.getWidth() - 18.0f, 12.0f);
        g.setFont (meta());
        g.setColour (metaColour);
        g.drawText (left, line, juce::Justification::centredLeft, true);
        g.drawText (right, line, juce::Justification::centredRight, true);
    }
    void foot (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text)
    {
        g.setFont (meta());
        g.setColour (metaColour.withMultipliedAlpha (0.8f));
        g.drawText (text, juce::Rectangle<float> (area.getX() + 9.0f, area.getBottom() - 15.0f, area.getWidth() - 18.0f, 12.0f), juce::Justification::centredLeft, true);
    }
    void small (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Justification j, float alpha = 1.0f)
    {
        g.setFont (label());
        g.setColour (labelColour.withMultipliedAlpha (alpha));
        g.drawText (text, r, j, false);
    }

    /** A log-frequency plot (20 Hz - 20 kHz) with its decades and a dB scale. */
    struct FreqPlot
    {
        juce::Rectangle<float> r;
        double lo = 20.0, hi = 20000.0, topDb = 18.0, bottomDb = -18.0;
        float x (double hz) const { return r.getX() + r.getWidth() * static_cast<float> (std::log (hz / lo) / std::log (hi / lo)); }
        float y (double db) const { return r.getY() + r.getHeight() * static_cast<float> ((topDb - std::clamp (db, bottomDb, topDb)) / (topDb - bottomDb)); }
        double hzAt (float px) const { return lo * std::pow (hi / lo, std::clamp ((px - r.getX()) / r.getWidth(), 0.0f, 1.0f)); }
        double dbAt (float py) const { return topDb - (topDb - bottomDb) * std::clamp ((py - r.getY()) / r.getHeight(), 0.0f, 1.0f); }
        void paintGrid (juce::Graphics& g) const
        {
            for (const double hz : { 100.0, 1000.0, 10000.0 })
            {
                g.setColour (grid);
                g.fillRect (juce::Rectangle<float> (x (hz) - 0.5f, r.getY(), 1.0f, r.getHeight()));
                small (g, hz >= 1000.0 ? juce::String (juce::roundToInt (hz / 1000.0)) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz", { x (hz) - 30.0f, r.getBottom() + 2.0f, 60.0f, 12.0f },
                       juce::Justification::centred, 0.8f);
            }
            g.setColour (axis);
            g.fillRect (juce::Rectangle<float> (r.getX(), y (0.0) - 0.5f, r.getWidth(), 1.0f));
        }
        template <typename F>
        void curve (juce::Graphics& g, F&& dbAtHz, juce::Colour colour) const
        {
            juce::Path p, fill;
            for (int i = 0; i <= 200; ++i)
            {
                const double hz = lo * std::pow (hi / lo, i / 200.0);
                const juce::Point<float> pt (x (hz), y (dbAtHz (hz)));
                if (i == 0)
                {
                    p.startNewSubPath (pt);
                    fill.startNewSubPath (pt.x, y (0.0));
                }
                else
                    p.lineTo (pt);
                fill.lineTo (pt);
            }
            fill.lineTo (r.getRight(), y (0.0));
            fill.closeSubPath();
            g.setColour (colour.withAlpha (0.12f));
            g.fillPath (fill);
            g.setColour (colour.withAlpha (0.22f));
            g.strokePath (p, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    };

    //==========================================================================
    /** The effect's graph: computed from its settings (a UI-side copy of its processor gives the
        exact curve its coefficients make) and metered from the engine's processor. */
    class FxGraph final : public GraphFrame
    {
    public:
        FxGraph (osp::plugin::InstrumentHost& h, InstanceId i) : host (h), id (i)
        {
            reader.attach (h.apvts());
            if (id.type == Type::eq)
                setInterceptsMouseClicks (true, false);   // (its band handles drag)
        }

        void refresh() override
        {
            juce::String sig;
            reader.read (settings);
            for (const char* legacy : { "drive", "drive.mode", "drive.tone", "drive.body" })
                if (id.type == Type::drive)
                    sig << host.parameterValue (legacy) << ",";
            for (const auto& spec : fxparams::specsOf (id.type))
                sig << host.parameterValue (pid (id, spec.name.toRawUTF8())) << ",";
            if (const auto* p = live())
            {
                if (const auto* c = dynamic_cast<const fx::Compressor*> (p))
                {
                    // The gain-reduction history (one point a refresh, ~4 s): what it did.
                    grHistory[static_cast<std::size_t> (grHead)] = c->reductionDb.load();
                    grHead = (grHead + 1) % static_cast<int> (grHistory.size());
                    sig << c->reductionDb.load() << c->inputDb.load() << c->bandReductionDb[0].load() << c->bandBoostDb[1].load() << c->bandReductionDb[2].load() << grHead;
                }
                if (const auto* s = dynamic_cast<const fx::StereoSpread*> (p))
                    sig << s->correlation.load() << s->scope.head.load();
                if (const auto* u = dynamic_cast<const fx::Utility*> (p))
                    sig << u->meterL.load() << u->meterR.load() << u->scope.head.load();
                if (const auto* ph = dynamic_cast<const fx::Phaser*> (p))
                    sig << ph->notchHz[0].load();
                if (const auto* fl = dynamic_cast<const fx::Flanger*> (p))
                    sig << fl->delayNowMs.load();
                if (const auto* r = dynamic_cast<const fx::Retro*> (p))
                    sig << r->spectrumDb[8].load() << r->spectrumDb[24].load() << r->spectrumDb[40].load() << r->eventHead.load() << r->effectiveBits.load()
                        << r->keptFraction.load() << r->wowCents.load() << static_cast<int> (r->ticks.load() / 32u);
            }
            if (sig != shown)
            {
                shown = sig;
                repaint();
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragging = -1;
            for (int b = 0; b < 4; ++b)
                if (handle (b).expanded (6.0f).contains (e.position))
                    dragging = b;
            if (dragging >= 0)
                for (const char* n : { "freq", "gain" })
                    if (auto* p = host.apvts().getParameter (pid (id, ("band" + std::to_string (dragging + 1) + "." + n).c_str())))
                        p->beginChangeGesture();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging < 0)
                return;
            const auto band = "band" + std::to_string (dragging + 1);
            auto set = [&] (const char* n, double v) {
                if (auto* p = host.apvts().getParameter (pid (id, (band + "." + n).c_str())))
                    p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (v)));
            };
            set ("freq", plot.hzAt (e.position.x));
            set ("gain", plot.dbAt (e.position.y));
            refresh();
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragging >= 0)
                for (const char* n : { "freq", "gain" })
                    if (auto* p = host.apvts().getParameter (pid (id, ("band" + std::to_string (dragging + 1) + "." + n).c_str())))
                        p->endChangeGesture();
            dragging = -1;
        }

    protected:
        void paintGraph (juce::Graphics& g, juce::Rectangle<float> area) override
        {
            reader.read (settings);
            const auto colour = visualIdentityFor (id.type).motifInk.brighter (0.15f);   // (V5: the device ink on its glass)
            plot.r = area.reduced (14.0f, 0.0f).withTrimmedTop (24.0f).withTrimmedBottom (32.0f);
            switch (id.type)
            {
                case Type::eq: paintEq (g, area, colour); break;
                case Type::filter: paintFilter (g, area, colour); break;
                case Type::compressor: paintComp (g, area, colour); break;
                case Type::spread: paintSpread (g, area, colour); break;
                case Type::phaser: paintPhaser (g, area, colour); break;
                case Type::flanger: paintFlanger (g, area, colour); break;
                case Type::shifter: paintShift (g, area, colour); break;
                case Type::convolution: paintConv (g, area, colour); break;
                case Type::utility: paintUtility (g, area, colour); break;
                case Type::drive: paintDrive (g, area, colour); break;
                case Type::retro: paintRetro (g, area, colour); break;
                default: break;
            }
        }

    private:
        const fx::Processor* live() const { return engineOf (host).fxProcessor (id); }

        juce::Rectangle<float> handle (int b) const
        {
            const auto& band = settings.eq[static_cast<std::size_t> (id.slot)].bands[static_cast<std::size_t> (b)];
            return juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ plot.x (band.hz), plot.y (band.gainDb) });
        }

        /** RETRO: the medium's response (its own filter designs) over the output's live spectrum;
            DIGITAL: the converter's effective Nyquist and quantisation floor (live, with MOTION);
            CODEC: its band limit and the share of bins kept; below, the last seconds' events
            (dropouts, crackle, pops, packets) where they happened. Nothing drawn is invented. */
        void paintRetro (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
        {
            const auto& s = settings.retro[static_cast<std::size_t> (id.slot)];
            const auto* r = dynamic_cast<const fx::Retro*> (live());
            const auto d = fx::deriveRetro (s);
            const bool digital = s.mode == fx::RetroSettings::digital, codec = digital && s.era == fx::RetroSettings::codec;
            plot.r = plot.r.withTrimmedBottom (12.0f);
            plot.topDb = 12.0;
            plot.bottomDb = -36.0;
            plot.paintGrid (g);
            // The live spectrum (the output, 30 Hz - 20 kHz): its own scale, -90..0 dBFS.
            auto specY = [this] (float db) { return plot.r.getBottom() - plot.r.getHeight() * std::clamp ((db + 90.0f) / 90.0f, 0.0f, 1.0f); };
            if (r != nullptr)
            {
                juce::Path line, fill;
                for (int b = 0; b < fx::Retro::bands; ++b)
                {
                    const double hz = 30.0 * std::pow (20000.0 / 30.0, (b + 0.5) / fx::Retro::bands);
                    const juce::Point<float> pt (plot.x (hz), specY (r->spectrumDb[static_cast<std::size_t> (b)].load()));
                    if (b == 0)
                    {
                        line.startNewSubPath (pt);
                        fill.startNewSubPath (pt.x, plot.r.getBottom());
                    }
                    else
                        line.lineTo (pt);
                    fill.lineTo (pt);
                }
                fill.lineTo (plot.x (20000.0 * 0.99), plot.r.getBottom());
                fill.closeSubPath();
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xffff8a3d).withAlpha (0.38f), 0.0f, plot.r.getY(), juce::Colour (0xffff8a3d).withAlpha (0.04f), 0.0f, plot.r.getBottom(), false));
                g.fillPath (fill);
                g.setColour (juce::Colour (0xffff8a3d).withAlpha (0.8f));
                g.strokePath (line, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            juce::String left, right;
            if (codec)
            {
                // The band it keeps (LOSS), and what share of the bins survived the last frame.
                if (d.codecLoss > 0.0f)
                {
                    const float cut = plot.x (std::min (19999.0, 20000.0 - 15000.0 * d.codecLoss));
                    g.setColour (colour.withAlpha (0.10f));
                    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (cut, plot.r.getY(), plot.r.getRight(), plot.r.getBottom()));
                    g.setColour (colour.withAlpha (0.8f));
                    g.fillRect (juce::Rectangle<float> (cut - 0.5f, plot.r.getY(), 1.0f, plot.r.getHeight()));
                }
                const float kept = r != nullptr ? r->keptFraction.load() : 1.0f;
                auto bar = juce::Rectangle<float> (plot.r.getRight() - 112.0f, plot.r.getY() + 4.0f, 100.0f, 5.0f);
                g.setColour (grid);
                g.fillRoundedRectangle (bar, 2.0f);
                g.setColour (colour);
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * std::clamp (kept, 0.0f, 1.0f)), 2.0f);
                small (g, "BINS KEPT " + juce::String (juce::roundToInt (100.0f * kept)) + " %", bar.translated (0.0f, 6.0f).withHeight (12.0f), juce::Justification::centredRight, 0.85f);
                left = "CODEC";
                right = "loss " + juce::String (juce::roundToInt (100.0f * d.codecLoss)) + juce::String::fromUTF8 (" % \xc2\xb7 STFT ") + juce::String (fx::Retro::frameFor (48000.0));
            }
            else if (digital)
            {
                // The converter now (MOTION moves it): its Nyquist, its quantisation floor.
                const float bits = r != nullptr ? r->effectiveBits.load() : d.bits;
                const float rateK = r != nullptr ? r->effectiveRateKHz.load() : d.rateKHz;
                const float nyq = plot.x (std::clamp (500.0 * rateK, 20.0, 20000.0));
                g.setColour (colour.withAlpha (0.10f));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (nyq, plot.r.getY(), plot.r.getRight(), plot.r.getBottom()));
                g.setColour (colour.withAlpha (0.85f));
                g.fillRect (juce::Rectangle<float> (nyq - 0.5f, plot.r.getY(), 1.0f, plot.r.getHeight()));
                const float floorY = specY (-6.02f * bits - 1.76f);
                const float dash[] { 4.0f, 3.0f };
                g.drawDashedLine (juce::Line<float> (plot.r.getX(), floorY, nyq, floorY), dash, 2, 1.0f);
                small (g, s.aliasHard ? "ALIASES" : "FILTERED", juce::Rectangle<float> (nyq + 4.0f, plot.r.getY() + 2.0f, 70.0f, 12.0f), juce::Justification::centredLeft, 0.85f);
                left = s.era == fx::RetroSettings::eighties ? "80s CONVERTER" : "90s CONVERTER";
                right = juce::String (bits, 1) + juce::String::fromUTF8 (" bit \xc2\xb7 ") + juce::String (rateK, 1) + " kHz";
            }
            else
            {
                // The medium's response: the same filter designs the audio plays.
                plot.curve (g, [&s] (double hz) { return fx::Retro::responseDb (s, 48000.0, hz); }, colour);
                const float cents = r != nullptr ? r->wowCents.load() : 0.0f;
                left = s.mode == fx::RetroSettings::tape ? (s.tapeModel == 1 ? "REEL" : "CASSETTE") : "VINYL";
                if (s.mode == fx::RetroSettings::vinyl)
                {
                    static const char* rpm[] { "33\xe2\x85\x93", "45", "78" };
                    left << "  " << juce::String::fromUTF8 (rpm[std::clamp (s.rpm, 0, 2)]) << " RPM";
                }
                right = juce::String::fromUTF8 ("pitch ") + (cents >= 0.0f ? "+" : "") + juce::String (cents, 1) + " ct";
            }
            // The events of the last four seconds (newest at the right).
            auto strip = juce::Rectangle<float> (plot.r.getX(), plot.r.getBottom() + 16.0f, plot.r.getWidth(), 8.0f);
            g.setColour (grid);
            g.fillRect (strip.withHeight (1.0f).withY (strip.getCentreY()));
            if (r != nullptr)
            {
                const auto now = r->ticks.load();
                const float window = 4.0f * r->ticksPerSecond.load();
                static const juce::Colour kinds[] { juce::Colour (0xffe9c46a), juce::Colour (0xfff1e4cf), juce::Colour (0xffff8a3d), juce::Colour (0xff8fd3c4) };
                for (int e = 0; e < static_cast<int> (r->eventTicks.size()); ++e)
                {
                    const auto at = r->eventTicks[static_cast<std::size_t> (e)].load();
                    if (at == 0u)
                        continue;
                    const float age = static_cast<float> (now - at);
                    if (age < 0.0f || age > window)
                        continue;
                    const int kind = std::clamp (r->eventKinds[static_cast<std::size_t> (e)].load(), 0, 3);
                    if (codec ? kind != 3 : (digital || kind == 3))
                        continue;   // (this personality's events only: dropouts, crackle, pops; or packets)
                    const float x = strip.getRight() - strip.getWidth() * age / window;
                    g.setColour (kinds[kind].withAlpha (1.0f - 0.7f * age / window));
                    g.fillEllipse (juce::Rectangle<float> (kind == 2 ? 7.0f : 5.0f, kind == 2 ? 7.0f : 5.0f).withCentre ({ x, strip.getCentreY() }));
                }
            }
            header (g, area, left, right);
        }

        void paintEq (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
        {
            fx::Equalizer view;
            view.prepare (48000.0, 64);
            view.set (settings, id.slot);
            view.reset();
            plot.topDb = 18.0;
            plot.bottomDb = -18.0;
            plot.paintGrid (g);
            plot.curve (g, [&view] (double hz) { return view.responseDb (hz); }, colour);
            const auto& s = settings.eq[static_cast<std::size_t> (id.slot)];
            for (int b = 0; b < 4; ++b)
            {
                const auto& band = s.bands[static_cast<std::size_t> (b)];
                const auto h = handle (b);
                g.setColour (band.on ? colour : labelColour.withMultipliedAlpha (0.5f));
                g.drawEllipse (h, 1.4f);
                g.setFont (label());
                g.drawText (juce::String (b + 1), h, juce::Justification::centred, false);
            }
            header (g, area, "PARAMETRIC", juce::String::fromUTF8 ("drag a band: frequency \xc2\xb7 gain"));
        }

        void paintFilter (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
        {
            fx::FxFilter view;
            view.prepare (48000.0, 64);
            view.set (settings, id.slot);
            view.reset();
            plot.topDb = 24.0;
            plot.bottomDb = -48.0;
            plot.paintGrid (g);
            plot.curve (g, [&view] (double hz) { return view.responseDb (hz); }, colour);
            const auto& s = settings.filter[static_cast<std::size_t> (id.slot)];
            auto* type = host.apvts().getParameter (pid (id, "type"));
            header (g, area, type != nullptr ? type->getCurrentValueAsText().toUpperCase() : juce::String(),
                    "FREQ " + textOf (host, pid (id, "freq")) (s.hz) + juce::String::fromUTF8 (" \xc2\xb7 RESO ") + juce::String (juce::roundToInt (100.0f * s.resonance)) + " %");
        }

        void paintComp (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
        {
            const auto& s = settings.comp[static_cast<std::size_t> (id.slot)];
            const auto* c = dynamic_cast<const fx::Compressor*> (live());
            const float gr = c != nullptr ? c->reductionDb.load() : 0.0f;
            auto body = area.reduced (14.0f, 0.0f).withTrimmedTop (24.0f).withTrimmedBottom (20.0f);
            if (s.mode == fx::CompSettings::multiband)
            {
                // Three bands where they are on a frequency axis (the crossovers at their real
                // frequencies); down (copper) above the line, up (the glow) below - live.
                plot.paintGrid (g);
                const char* names[3] { "LOW", "MID", "HIGH" };
                const float xs[4] { plot.r.getX(), plot.x (s.lowHz), plot.x (s.highHz), plot.r.getRight() };
                const float mid = plot.r.getCentreY();
                for (int b = 0; b < 3; ++b)
                {
                    auto col = juce::Rectangle<float>::leftTopRightBottom (xs[b], plot.r.getY(), xs[b + 1], plot.r.getBottom());
                    g.setColour (colour.withAlpha (b == 1 ? 0.06f : 0.10f));
                    g.fillRect (col);
                    if (b > 0)
                    {
                        g.setColour (colour.withAlpha (0.55f));
                        g.fillRect (juce::Rectangle<float> (col.getX() - 0.5f, col.getY(), 1.0f, col.getHeight()));
                    }
                    const float down = c != nullptr ? c->bandReductionDb[static_cast<std::size_t> (b)].load() : 0.0f;
                    const float up = c != nullptr ? c->bandBoostDb[static_cast<std::size_t> (b)].load() : 0.0f;
                    const float scale = 0.5f * col.getHeight() / 18.0f;
                    auto barCol = col.withSizeKeepingCentre (std::min (26.0f, col.getWidth() * 0.5f), col.getHeight());
                    g.setColour (juce::Colour (0xffd99a6a));
                    g.fillRect (barCol.withY (mid - std::min (18.0f, down) * scale).withHeight (std::min (18.0f, down) * scale));
                    g.setColour (colour);
                    g.fillRect (barCol.withY (mid + 1.0f).withHeight (std::min (18.0f, up) * scale));
                    small (g, names[b], col.withHeight (12.0f).withY (col.getY() + 2.0f), juce::Justification::centred);
                }
                g.setColour (axis.withMultipliedAlpha (2.0f));
                g.fillRect (plot.r.withHeight (1.0f).withY (mid));
                header (g, area, "MULTIBAND " + juce::String (juce::roundToInt (100.0f * s.depth)) + " %",
                        textOf (host, pid (id, "multi.low")) (s.lowHz) + " / " + textOf (host, pid (id, "multi.high")) (s.highHz));
                foot (g, area, "gain reduction above, upward gain below (live)");
                return;
            }
            // Left: the gain reduction it applied over the last seconds (live). Right: the
            // transfer curve (CLEAN's static curve; FET's fixed reference and ratio) with where
            // the input is now on it.
            auto history = body.removeFromLeft (body.getWidth() * 0.46f).reduced (0.0f, 2.0f);
            body.removeFromLeft (16.0f);
            {
                auto plotArea = history.withTrimmedLeft (22.0f).withTrimmedBottom (14.0f);
                g.setColour (grid);
                for (const float d : { 6.0f, 12.0f, 18.0f })
                    g.fillRect (plotArea.withHeight (1.0f).withY (plotArea.getY() + plotArea.getHeight() * d / 24.0f));
                for (const float d : { 0.0f, 12.0f, 24.0f })
                    small (g, d == 0.0f ? juce::String ("0") : "-" + juce::String (juce::roundToInt (d)),
                           juce::Rectangle<float> (history.getX(), plotArea.getY() + plotArea.getHeight() * d / 24.0f - 6.0f, 18.0f, 12.0f), juce::Justification::centredRight, 0.8f);
                juce::Path fill;
                const int count = static_cast<int> (grHistory.size());
                fill.startNewSubPath (plotArea.getX(), plotArea.getY());
                for (int i = 0; i < count; ++i)
                {
                    const float v = std::clamp (grHistory[static_cast<std::size_t> ((grHead + i) % count)], 0.0f, 24.0f);
                    fill.lineTo (plotArea.getX() + plotArea.getWidth() * static_cast<float> (i) / static_cast<float> (count - 1), plotArea.getY() + plotArea.getHeight() * v / 24.0f);
                }
                fill.lineTo (plotArea.getRight(), plotArea.getY());
                fill.closeSubPath();
                g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.65f), 0.0f, plotArea.getY(), colour.withAlpha (0.12f), 0.0f, plotArea.getBottom(), false));
                g.fillPath (fill);
                small (g, "GAIN REDUCTION", history.withY (history.getBottom() - 12.0f).withHeight (12.0f).withTrimmedLeft (22.0f), juce::Justification::centredLeft, 0.85f);
            }
            auto box = body.withTrimmedBottom (14.0f).withTrimmedLeft (18.0f);
            box = box.withSizeKeepingCentre (std::min (box.getWidth(), box.getHeight() * 1.25f), box.getHeight());
            auto xy = [&] (float inDb, float outDb) {
                return juce::Point<float> (box.getX() + box.getWidth() * (std::clamp (inDb, -60.0f, 0.0f) + 60.0f) / 60.0f,
                                           box.getBottom() - box.getHeight() * (std::clamp (outDb, -60.0f, 0.0f) + 60.0f) / 60.0f);
            };
            g.setColour (grid);
            for (const float d : { -48.0f, -36.0f, -24.0f, -12.0f })
            {
                g.fillRect (juce::Rectangle<float> (xy (d, d).x - 0.5f, box.getY(), 1.0f, box.getHeight()));
                g.fillRect (juce::Rectangle<float> (box.getX(), xy (d, d).y - 0.5f, box.getWidth(), 1.0f));
            }
            g.setColour (axis.withMultipliedAlpha (2.0f));
            g.drawRect (box, 1.0f);
            const float dashes[] { 3.0f, 3.0f };
            g.drawDashedLine (juce::Line<float> (xy (-60.0f, -60.0f), xy (0.0f, 0.0f)), dashes, 2, 1.0f);
            auto transfer = [&s] (float in) {
                if (s.mode == fx::CompSettings::clean)
                    return fx::Compressor::cleanCurveDb (s, in) + s.makeupDb;
                constexpr std::array<float, 5> ratios { 4.0f, 8.0f, 12.0f, 20.0f, 40.0f };
                const float drive = in + s.fetInputDb, ratio = ratios[static_cast<std::size_t> (std::clamp (s.fetRatio, 0, 4))];
                const float over = drive + 18.0f;
                return (s.fetColorOnly || over <= 0.0f ? drive : drive - (1.0f - 1.0f / ratio) * over) + s.fetOutputDb;
            };
            juce::Path p;
            for (int i = 0; i <= 120; ++i)
            {
                const float in = -60.0f + 0.5f * static_cast<float> (i);
                const auto pt = xy (in, transfer (in));
                i == 0 ? p.startNewSubPath (pt) : p.lineTo (pt);
            }
            g.setColour (colour.withAlpha (0.25f));
            g.strokePath (p, juce::PathStrokeType (4.0f));
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (1.6f));
            if (c != nullptr)
                if (const float in = c->inputDb.load(); in > -60.0f)
                {
                    const auto dot = xy (in, transfer (in));
                    g.setColour (colour.withAlpha (0.3f));
                    g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (dot));
                    g.setColour (colour.brighter (0.3f));
                    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (dot));
                }
            for (const float d : { -48.0f, -24.0f, 0.0f })
                small (g, juce::String (juce::roundToInt (d)), juce::Rectangle<float> (xy (d, d).x - 14.0f, box.getBottom() + 1.0f, 28.0f, 12.0f), juce::Justification::centred, 0.8f);
            header (g, area, s.mode == fx::CompSettings::clean ? "CLEAN" : "FET 76", "GR " + juce::String (gr, 1) + " dB");
        }

        void paintSpread (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
        {
            const auto& s = settings.spread[static_cast<std::size_t> (id.slot)];
            const auto* sp = dynamic_cast<const fx::StereoSpread*> (live());
            auto body = area.reduced (14.0f, 0.0f).withTrimmedTop (26.0f).withTrimmedBottom (22.0f);
            constexpr std::array<int, 4> counts { 2, 4, 6, 8 };
            const int voices = counts[static_cast<std::size_t> (std::clamp (s.voices, 0, 3))];
            // A goniometer of what it plays (mid up, side across; measured, decimated), and the
            // voices it spreads (from the settings) along its horizon.
            auto field = body.removeFromTop (body.getHeight() * 0.68f);
            const auto c = field.getCentre();
            const float rx = field.getWidth() * 0.46f, ry = field.getHeight() * 0.48f;
            g.setColour (grid);
            for (const float k : { 0.35f, 0.7f, 1.0f })
                g.drawEllipse (juce::Rectangle<float> (2.0f * rx * k, 2.0f * ry * k).withCentre (c), 1.0f);
            g.setColour (axis.withMultipliedAlpha (1.6f));
            g.fillRect (juce::Rectangle<float> (field.getX(), c.y - 0.5f, field.getWidth(), 1.0f));
            g.fillRect (juce::Rectangle<float> (c.x - 0.5f, field.getY(), 1.0f, field.getHeight()));
            small (g, "L", field.withWidth (12.0f), juce::Justification::centredLeft);
            small (g, "R", field.withX (field.getRight() - 12.0f).withWidth (12.0f), juce::Justification::centredRight);
            small (g, "M", juce::Rectangle<float> (c.x + 3.0f, field.getY(), 12.0f, 12.0f), juce::Justification::centredLeft);
            small (g, "S", juce::Rectangle<float> (c.x + 3.0f, field.getBottom() - 12.0f, 12.0f, 12.0f), juce::Justification::centredLeft);
            if (sp != nullptr)
            {
                std::array<juce::Point<float>, fx::ScopeTap::size> pts;
                float peak = 1.0e-4f;
                for (int i = 0; i < fx::ScopeTap::size; ++i)
                {
                    const float l = sp->scope.l[static_cast<std::size_t> (i)].load (std::memory_order_relaxed);
                    const float r = sp->scope.r[static_cast<std::size_t> (i)].load (std::memory_order_relaxed);
                    pts[static_cast<std::size_t> (i)] = { (r - l) * 0.70710678f, (l + r) * 0.70710678f };
                    peak = std::max ({ peak, std::abs (l), std::abs (r) });
                }
                const float k = 0.92f / (peak * 1.41421356f);
                for (const auto& q : pts)
                {
                    const juce::Point<float> at (c.x + q.x * k * rx, c.y - q.y * k * ry);
                    g.setColour (colour.withAlpha (0.16f));
                    g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (at));
                    g.setColour (colour.withAlpha (0.75f));
                    g.fillEllipse (juce::Rectangle<float> (1.8f, 1.8f).withCentre (at));
                }
            }
            if (s.mode != fx::SpreadSettings::dimension)
                for (int v = 0; v < voices; ++v)
                {
                    const float spread = voices > 1 ? 2.0f * static_cast<float> (v) / static_cast<float> (voices - 1) - 1.0f : 0.0f;
                    const juce::Point<float> at (c.x + rx * 0.98f * spread * std::min (1.0f, s.width), field.getBottom() + 4.0f);
                    g.setColour (colour.withAlpha (0.8f));
                    g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (at));
                }
            body.removeFromTop (6.0f);
            // Measured: correlation and side level.
            const float corr = sp != nullptr ? sp->correlation.load() : 1.0f;
            auto meter = body.withTrimmedTop (8.0f).withHeight (8.0f);
            g.setColour (axis);
            g.fillRoundedRectangle (meter, 3.0f);
            const float x = meter.getX() + meter.getWidth() * 0.5f * (corr + 1.0f);
            g.setColour (corr < 0.0f ? juce::Colour (0xffc99470) : colour);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ x, meter.getCentreY() }));
            small (g, "-1", meter.withY (meter.getBottom() + 2.0f).withHeight (12.0f), juce::Justification::centredLeft, 0.8f);
            small (g, "CORRELATION " + juce::String (corr, 2), meter.withY (meter.getBottom() + 2.0f).withHeight (12.0f), juce::Justification::centred);
            small (g, "+1", meter.withY (meter.getBottom() + 2.0f).withHeight (12.0f), juce::Justification::centredRight, 0.8f);
            static const char* modes[3] { "HYPER", "DIMENSION", "BOTH" };
            header (g, area, juce::String (modes[std::clamp (s.mode, 0, 2)]) + (s.mode != 1 ? juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (voices) + juce::String::fromUTF8 (" voices \xc2\xb1") + juce::String (s.detuneCents, 1) + " ct" : juce::String()),
                    "WIDTH " + juce::String (juce::roundToInt (100.0f * s.width)) + " %");
        }

        void paintPhaser (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
        {
            const auto& s = settings.phaser[static_cast<std::size_t> (id.slot)];
            const auto* ph = dynamic_cast<const fx::Phaser*> (live());
            plot.topDb = 6.0;
            plot.bottomDb = -30.0;
            plot.paintGrid (g);
            // The sweep's range (from the settings), and where the notches are now (live).
            const double octaves = (s.mode == fx::PhaserSettings::deep ? 4.0 : 3.0) * s.depth;
            g.setColour (colour.withAlpha (0.12f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (plot.x (std::max (20.0, s.centreHz * std::exp2 (-octaves))), plot.r.getY(),
                                                                     plot.x (std::min (20000.0, s.centreHz * std::exp2 (octaves))), plot.r.getBottom()));
            if (ph != nullptr)
                for (const auto& n : ph->notchHz)
                    if (const float hz = n.load(); hz > 20.0f)
                    {
                        g.setColour (colour);
                        g.fillRect (juce::Rectangle<float> (plot.x (hz) - 0.75f, plot.r.getY(), 1.5f, plot.r.getHeight()));
                    }
            header (g, area, s.mode == fx::PhaserSettings::deep ? "DEEP" : "CLASSIC", s.sync ? juce::String ("SYNC") : juce::String (s.rateHz, 2) + " Hz");
            foot (g, area, "the notches now (live) within the sweep's range");
        }

        void paintFlanger (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
        {
            const auto& s = settings.flanger[static_cast<std::size_t> (id.slot)];
            const auto* fl = dynamic_cast<const fx::Flanger*> (live());
            const double d = 0.001 * (fl != nullptr ? fl->delayNowMs.load() : s.delayMs);
            const double fb = std::clamp (static_cast<double> (s.feedback), -0.92, 0.92), mix = std::clamp (static_cast<double> (s.mix), 0.0, 1.0);
            plot.topDb = 12.0;
            plot.bottomDb = -30.0;
            plot.paintGrid (g);
            plot.curve (g, [&] (double hz) {
                const auto z = std::polar (1.0, -2.0 * fx::dsp::pi * hz * d);
                const auto wet = z / (1.0 - fb * z);
                return 20.0 * std::log10 (std::max (1.0e-6, std::abs ((1.0 - mix) + mix * wet)));
            }, colour);
            header (g, area, s.mode == fx::FlangerSettings::throughZero ? "THROUGH-ZERO" : "CLASSIC", "DELAY " + juce::String (1000.0 * d, 2) + " ms");
            foot (g, area, "its comb at the delay now (live)");
        }

        void paintShift (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
        {
            const auto& s = settings.shifter[static_cast<std::size_t> (id.slot)];
            auto body = area.reduced (18.0f, 0.0f).withTrimmedTop (28.0f).withTrimmedBottom (28.0f);
            if (s.mode == fx::ShifterSettings::shift)
            {
                // Input frequency -> output: every partial moves by the same Hz (not a ratio).
                const double shift = s.down ? -s.shiftHz : s.shiftHz;
                auto xy = [&] (double in, double out) {
                    return juce::Point<float> (body.getX() + body.getWidth() * static_cast<float> (in / 2000.0), body.getBottom() - body.getHeight() * static_cast<float> (std::clamp (out, 0.0, 2000.0) / 2000.0));
                };
                g.setColour (axis);
                g.drawLine (juce::Line<float> (xy (0, 0), xy (2000, 2000)), 1.0f);
                g.setColour (colour);
                g.drawLine (juce::Line<float> (xy (std::max (0.0, -shift), std::max (0.0, shift)), xy (std::min (2000.0, 2000.0 - shift), std::min (2000.0, 2000.0 + shift))), 1.6f);
                small (g, "IN 0 - 2 kHz", body.withY (body.getBottom() + 2.0f).withHeight (12.0f), juce::Justification::centredRight, 0.8f);
                header (g, area, "SHIFT", (shift >= 0 ? "+" : "") + juce::String (shift, 1) + " Hz");
                foot (g, area, "every partial moves by the same Hz (inharmonic)");
                return;
            }
            // BLUR: each bin's magnitude settles over BLUR TIME (its smoothing, drawn).
            juce::Path p;
            for (int i = 0; i <= 100; ++i)
            {
                const double t = 0.5 * i / 100.0;
                const double y = 1.0 - std::exp (-t / (std::max (10.0f, s.blurMs) * 0.001));
                const juce::Point<float> pt (body.getX() + body.getWidth() * static_cast<float> (i) / 100.0f, body.getBottom() - body.getHeight() * static_cast<float> (y));
                i == 0 ? p.startNewSubPath (pt) : p.lineTo (pt);
            }
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (1.6f));
            small (g, "0 - 500 ms", body.withY (body.getBottom() + 2.0f).withHeight (12.0f), juce::Justification::centredRight, 0.8f);
            header (g, area, "BLUR " + juce::String (juce::roundToInt (100.0f * s.blurAmount)) + " %", juce::String (juce::roundToInt (s.blurMs)) + " ms");
            foot (g, area, "how fast each frequency's level follows (window 43 ms)");
        }

        void paintConv (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
        {
            const auto& s = settings.conv[static_cast<std::size_t> (id.slot)];
            const auto ir = engineOf (host).convolutionImpulse (id.slot);
            auto body = area.reduced (14.0f, 0.0f).withTrimmedTop (26.0f).withTrimmedBottom (24.0f);
            if (ir != nullptr && ! ir->envelope.empty())
            {
                float peak = 1.0e-6f;
                for (const float e : ir->envelope)
                    peak = std::max (peak, e);
                juce::Path p;
                const float w = body.getWidth() / static_cast<float> (ir->envelope.size());
                for (std::size_t i = 0; i < ir->envelope.size(); ++i)
                {
                    const float db = 20.0f * std::log10 (std::max (1.0e-5f, ir->envelope[i] / peak));
                    const float h = body.getHeight() * std::clamp ((db + 60.0f) / 60.0f, 0.0f, 1.0f);
                    p.addRectangle (body.getX() + w * static_cast<float> (i), body.getBottom() - h, std::max (1.0f, w - 0.5f), h);
                }
                g.setColour (colour.withAlpha (0.75f));
                g.fillPath (p);
                small (g, juce::String (ir->lengthSamples / ir->sampleRate, 2) + " s", body.withY (body.getBottom() + 2.0f).withHeight (12.0f), juce::Justification::centredRight, 0.8f);
            }
            header (g, area, ir != nullptr ? juce::String (ir->name).toUpperCase() : juce::String ("NO IMPULSE"), "DECAY " + juce::String (s.decaySeconds, 1) + " s");
            foot (g, area, "the impulse's level over its length (-60 dB at the floor)");
        }

        void paintDrive (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
        {
            // DRIVE's own circuit (osp::DriveProcessor::transfer): its steady-state curve, and
            // the harmonics it makes of a sine at its reference level (computed from the curve).
            osp::DriveProcessor::Settings d;
            d.amount = std::clamp (host.parameterValue ("drive") / 100.0, 0.0, 1.0);
            d.mode = static_cast<osp::DriveMode> (std::clamp (juce::roundToInt (host.parameterValue ("drive.mode")), 0, 2));
            d.tone = std::clamp (host.parameterValue ("drive.tone") / 100.0, 0.0, 1.0);
            d.body = std::clamp (host.parameterValue ("drive.body") / 100.0, 0.0, 1.0);
            auto body = area.reduced (14.0f, 0.0f).withTrimmedTop (26.0f).withTrimmedBottom (22.0f);
            auto curveBox = body.removeFromLeft (body.getWidth() * 0.46f);
            curveBox = curveBox.withSizeKeepingCentre (std::min (curveBox.getWidth(), curveBox.getHeight() * 1.15f), curveBox.getHeight());
            body.removeFromLeft (18.0f);
            g.setColour (grid);
            g.fillRect (curveBox.withHeight (1.0f).withY (curveBox.getCentreY()));
            g.fillRect (juce::Rectangle<float> (curveBox.getCentreX() - 0.5f, curveBox.getY(), 1.0f, curveBox.getHeight()));
            g.setColour (axis.withMultipliedAlpha (2.0f));
            g.drawRect (curveBox, 1.0f);
            const float dashes[] { 3.0f, 3.0f };
            g.drawDashedLine ({ curveBox.getBottomLeft(), curveBox.getTopRight() }, dashes, 2, 1.0f);
            juce::Path p;
            for (int i = 0; i <= 160; ++i)
            {
                const double x = -2.0 + 4.0 * i / 160.0;
                const double y = std::clamp (osp::DriveProcessor::transfer (d, x), -2.0, 2.0);
                const juce::Point<float> pt (curveBox.getX() + curveBox.getWidth() * static_cast<float> ((x + 2.0) / 4.0),
                                             curveBox.getBottom() - curveBox.getHeight() * static_cast<float> ((y + 2.0) / 4.0));
                i == 0 ? p.startNewSubPath (pt) : p.lineTo (pt);
            }
            g.setColour (colour.withAlpha (0.25f));
            g.strokePath (p, juce::PathStrokeType (4.0f));
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (1.6f));
            small (g, "TRANSFER", curveBox.withY (curveBox.getBottom() + 2.0f).withHeight (12.0f), juce::Justification::centredLeft, 0.85f);
            // Harmonics 1-9 of a sine through the curve (a 256-point DFT of one period).
            constexpr int n = 256, count = 9;
            std::array<double, count> mag {};
            for (int k = 1; k <= count; ++k)
            {
                double re = 0.0, im = 0.0;
                for (int t = 0; t < n; ++t)
                {
                    const double ph = juce::MathConstants<double>::twoPi * t / n;
                    const double y = osp::DriveProcessor::transfer (d, std::sin (ph));
                    re += y * std::cos (k * ph);
                    im += y * std::sin (k * ph);
                }
                mag[static_cast<std::size_t> (k - 1)] = std::sqrt (re * re + im * im) * 2.0 / n;
            }
            auto bars = body.withTrimmedBottom (14.0f);
            const float w = bars.getWidth() / static_cast<float> (count);
            for (int k = 0; k < count; ++k)
            {
                const double db = 20.0 * std::log10 (std::max (1.0e-6, mag[static_cast<std::size_t> (k)] / std::max (1.0e-9, mag[0])));
                const float h = bars.getHeight() * static_cast<float> (std::clamp ((db + 72.0) / 72.0, 0.0, 1.0));
                const auto bar = juce::Rectangle<float> (bars.getX() + w * static_cast<float> (k) + w * 0.25f, bars.getBottom() - h, w * 0.5f, h);
                g.setGradientFill (juce::ColourGradient (colour, 0.0f, bar.getY(), colour.withAlpha (0.35f), 0.0f, bar.getBottom(), false));
                g.fillRect (bar);
                small (g, juce::String (k + 1), juce::Rectangle<float> (bars.getX() + w * static_cast<float> (k), bars.getBottom() + 1.0f, w, 12.0f), juce::Justification::centred, 0.8f);
            }
            static const char* modes[3] { "TUBE", "TAPE", "CRUNCH" };
            header (g, area, juce::String (modes[static_cast<int> (d.mode)]) + juce::String::fromUTF8 (" \xc2\xb7 DRIVE ") + juce::String (juce::roundToInt (100.0 * d.amount)) + " %",
                    "HARMONICS (dB, -72 .. 0)");
        }

        void paintUtility (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
        {
            const auto* u = dynamic_cast<const fx::Utility*> (live());
            const auto& s = settings.utility[static_cast<std::size_t> (id.slot)];
            auto body = area.reduced (18.0f, 0.0f).withTrimmedTop (30.0f).withTrimmedBottom (24.0f);
            auto scopeArea = body.removeFromRight (body.getWidth() * 0.42f);
            body.removeFromRight (14.0f);
            // The levels after it (live RMS), each channel's polarity marked when inverted.
            const float levels[2] { u != nullptr ? u->meterL.load() : 0.0f, u != nullptr ? u->meterR.load() : 0.0f };
            const bool inverted[2] { s.invertLeft, s.invertRight };
            const char* names[2] { "L", "R" };
            for (int c = 0; c < 2; ++c)
            {
                auto row = body.removeFromTop (22.0f);
                body.removeFromTop (8.0f);
                small (g, names[c], row.removeFromLeft (14.0f), juce::Justification::centredLeft);
                auto mark = row.removeFromRight (34.0f);
                if (inverted[c])
                {
                    g.setColour (juce::Colour (0xffe0a46a));
                    g.drawRoundedRectangle (mark.reduced (2.0f, 4.0f), 3.0f, 1.0f);
                    small (g, juce::String::fromUTF8 ("\xc3\x98 INV"), mark, juce::Justification::centred, 1.0f);
                }
                auto bar = row.withSizeKeepingCentre (row.getWidth() - 6.0f, 8.0f);
                g.setColour (axis);
                g.fillRoundedRectangle (bar, 3.0f);
                const float db = 20.0f * std::log10 (std::max (1.0e-6f, levels[c]));
                g.setColour (colour);
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * std::clamp ((db + 60.0f) / 60.0f, 0.0f, 1.0f)), 3.0f);
                for (const float tick : { -48.0f, -36.0f, -24.0f, -12.0f, -6.0f })
                {
                    g.setColour (juce::Colours::white.withAlpha (0.18f));
                    g.fillRect (juce::Rectangle<float> (bar.getX() + bar.getWidth() * (tick + 60.0f) / 60.0f, bar.getBottom() + 2.0f, 1.0f, 3.0f));
                }
            }
            const float corr = u != nullptr ? u->correlation.load() : 1.0f;
            auto meter = body.removeFromTop (8.0f).withTrimmedLeft (14.0f).withTrimmedRight (40.0f);
            g.setColour (axis);
            g.fillRoundedRectangle (meter, 3.0f);
            g.setColour (corr < 0.0f ? juce::Colour (0xffe0a46a) : colour);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ meter.getX() + meter.getWidth() * 0.5f * (corr + 1.0f), meter.getCentreY() }));
            small (g, "CORRELATION " + juce::String (corr, 2), body.withHeight (14.0f).translated (14.0f, 2.0f), juce::Justification::centredLeft);
            // A goniometer of its output.
            const auto c = scopeArea.getCentre();
            const float rx = scopeArea.getWidth() * 0.46f, ry = scopeArea.getHeight() * 0.46f;
            g.setColour (grid);
            g.drawEllipse (juce::Rectangle<float> (2.0f * rx, 2.0f * ry).withCentre (c), 1.0f);
            g.setColour (axis.withMultipliedAlpha (1.6f));
            g.fillRect (juce::Rectangle<float> (c.x - rx, c.y - 0.5f, 2.0f * rx, 1.0f));
            g.fillRect (juce::Rectangle<float> (c.x - 0.5f, c.y - ry, 1.0f, 2.0f * ry));
            if (u != nullptr)
            {
                float peak = 1.0e-4f;
                for (int i = 0; i < fx::ScopeTap::size; ++i)
                    peak = std::max ({ peak, std::abs (u->scope.l[static_cast<std::size_t> (i)].load (std::memory_order_relaxed)),
                                       std::abs (u->scope.r[static_cast<std::size_t> (i)].load (std::memory_order_relaxed)) });
                const float k = 0.92f / (peak * 1.41421356f);
                for (int i = 0; i < fx::ScopeTap::size; ++i)
                {
                    const float l = u->scope.l[static_cast<std::size_t> (i)].load (std::memory_order_relaxed);
                    const float r = u->scope.r[static_cast<std::size_t> (i)].load (std::memory_order_relaxed);
                    const juce::Point<float> at (c.x + (r - l) * 0.70710678f * k * rx, c.y - (l + r) * 0.70710678f * k * ry);
                    g.setColour (colour.withAlpha (0.65f));
                    g.fillEllipse (juce::Rectangle<float> (1.8f, 1.8f).withCentre (at));
                }
            }
            header (g, area, "GAIN " + textOf (host, pid (id, "gain")) (s.gainDb), "WIDTH " + juce::String (juce::roundToInt (100.0f * s.width)) + " %");
            foot (g, area, "levels and image after it (live)");
        }

        osp::plugin::InstrumentHost& host;
        InstanceId id;
        fxparams::Reader reader;
        fx::Settings settings;
        FreqPlot plot;
        std::array<float, 120> grHistory {};
        int grHead = 0;
        juce::String shown;
        int dragging = -1;
    };

    //==========================================================================
    /**
        EQUALIZER as one graphic: the card EQ's design at the rack's full width (the graph IS the
        module, its controls are overlays). A log grid 20 Hz - 20 kHz and +-18 dB; the spectrum
        of what plays through it - the output filled, the input a quiet line (Pro-Q / EQ Eight
        style; the processor's taps, the FFT here on the message thread); the selected band's
        own shape; the EQ's exact response (a UI-side copy of its processor); the bands as
        nodes in their type's colour, HP and LP as their own nodes. Drag a node (frequency, and
        gain where it has one; an off node comes on), wheel for Q, double-click to reset. The
        top row: the band chips (1-4, HP, LP), the selected band's ON, TYPE, FREQ, GAIN and Q
        (or the pass filter's FREQ and 12 / 24 slope), MIX and OUTPUT - the same parameters.
    */
    class EqGraphic final : public GraphFrame, private juce::Timer
    {
    public:
        static constexpr int hp = 4, lp = 5;   ///< the pass filters' chips (after the four bands)
        static constexpr int fftSize = 2048;
        static constexpr float floorDb = -96.0f;

        EqGraphic (osp::plugin::InstrumentHost& h, InstanceId i) : host (h), id (i)
        {
            setInterceptsMouseClicks (true, true);
            reader.attach (h.apvts());
            reader.read (settings);
            buffer.resize (static_cast<std::size_t> (fftSize));
            for (int n = 0; n < fftSize; ++n)
                window[static_cast<std::size_t> (n)] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * static_cast<float> (n) / static_cast<float> (fftSize - 1));
            postDb.fill (floorDb);
            preDb.fill (floorDb);
            mixField = std::make_unique<osp::plugin::EqValueField> (host, pid (id, "mix"));
            outField = std::make_unique<osp::plugin::EqValueField> (host, pid (id, "output"));
            for (auto* f : { mixField.get(), outField.get() })
                addAndMakeVisible (*f);
            selectBand (0);
            startTimerHz (30);
        }
        ~EqGraphic() override { stopTimer(); }

        // -- what tests and the screenshots read ---------------------------------------------
        int selectedBand() const noexcept { return selected; }
        void selectBand (int band)
        {
            selected = juce::jlimit (0, lp, band);
            rebuildFields();
            repaint();
        }
        juce::Rectangle<float> plotArea() const
        {
            const auto r = getLocalBounds().toFloat().reduced (1.0f);
            return juce::Rectangle<float>::leftTopRightBottom (r.getX() + 36.0f, r.getY() + 36.0f, r.getRight() - 12.0f, r.getBottom() - 20.0f);
        }
        juce::Point<float> nodePosition (int band) const
        {
            const auto& e = eq();
            if (band == hp)
                return { xForHz (e.hpHz), yForDb (0.0) };
            if (band == lp)
                return { xForHz (e.lpHz), yForDb (0.0) };
            const auto& b = e.bands[static_cast<std::size_t> (band)];
            return { xForHz (b.hz), hasGain (b.type) ? yForDb (b.gainDb) : yForDb (0.0) };
        }
        /** The output's spectrum now (dB per bin; the floor when nothing has played). */
        float spectrumPeakDb() const noexcept { return *std::max_element (postDb.begin(), postDb.end()); }
        juce::Component* field (const juce::String& name) const
        {
            if (name == "mix")
                return mixField.get();
            if (name == "output")
                return outField.get();
            if (name == "freq")
                return freqField.get();
            if (name == "gain")
                return gainField.get();
            if (name == "q")
                return qField.get();
            return nullptr;
        }
        /** One analysis step (the timer's; tests call it after playing). */
        void analyse()
        {
            const auto* p = dynamic_cast<const fx::Equalizer*> (engineOf (host).fxProcessor (id));
            if (p == nullptr)
                return;
            const int head = p->spectrumOut.head.load (std::memory_order_acquire);
            const bool fresh = head != lastHead;
            lastHead = head;
            const bool moved = transform (p->spectrumOut, postDb, fresh) | transform (p->spectrumIn, preDb, fresh);
            if (moved)
                repaint (plotArea().expanded (2.0f).getSmallestIntegerContainer());
        }

        void refresh() override
        {
            reader.read (settings);
            juce::String sig;
            for (const auto& spec : fxparams::specsOf (id.type))
                sig << host.parameterValue (pid (id, spec.name.toRawUTF8())) << ",";
            if (sig != shown)
            {
                const bool typeChanged = selected < 4 && shownType != eq().bands[static_cast<std::size_t> (selected)].type;
                shown = sig;
                if (typeChanged)
                    rebuildFields();   // (a type with or without gain: its fields)
                repaint();
            }
        }

        void resized() override { placeFields(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto pos = e.position;
            for (int c = 0; c <= lp; ++c)
                if (chipArea (c).contains (pos))
                {
                    selectBand (c);
                    return;
                }
            if (powerArea().contains (pos))
            {
                toggle (selected == hp ? "hp.on" : selected == lp ? "lp.on" : band (selected, "on"));
                return;
            }
            if (selected < 4 && typeArea().contains (pos))
            {
                showTypeMenu();
                return;
            }
            if (selected >= 4)
                for (int k = 0; k < 2; ++k)
                    if (slopeArea (k).contains (pos))
                    {
                        set1 (selected == hp ? "hp.steep" : "lp.steep", static_cast<float> (k));
                        return;
                    }
            dragBand = bandAt (pos);
            if (dragBand < 0)
                return;
            if (dragBand != selected)
                selectBand (dragBand);
            // A node that is off comes on as it is taken (the card EQ's way).
            if (! isOn (dragBand))
                set1 (onId (dragBand), 1.0f);
            host.undo().beginNewTransaction ("EQ band");   // (the drag: one undoable step)
            for (const auto& n : dragIds (dragBand))
                if (auto* p = host.apvts().getParameter (n))
                    p->beginChangeGesture();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragBand < 0)
                return;
            const auto ids = dragIds (dragBand);
            setRaw (ids[0], static_cast<float> (hzForX (e.position.x)));
            if (ids.size() > 1)
                setRaw (ids[1], static_cast<float> (dbForY (e.position.y)));
            refresh();
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragBand >= 0)
                for (const auto& n : dragIds (dragBand))
                    if (auto* p = host.apvts().getParameter (n))
                        p->endChangeGesture();
            dragBand = -1;
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            // A band's gain back to 0 dB; a pass filter off.
            const int b = bandAt (e.position);
            if (b == hp || b == lp)
                set1 (onId (b), 0.0f);
            else if (b >= 0 && hasGain (eq().bands[static_cast<std::size_t> (b)].type))
                set1 (band (b, "gain"), 0.0f);
        }
        void mouseMove (const juce::MouseEvent& e) override
        {
            const int now = bandAt (e.position);
            setMouseCursor (now >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
            if (now != hover)
            {
                hover = now;
                repaint();
            }
        }
        void mouseExit (const juce::MouseEvent&) override
        {
            hover = -1;
            repaint();
        }
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            // Q: the band under the pointer, else the selected band.
            int b = bandAt (e.position);
            if (b < 0)
                b = selected;
            if (b < 0 || b >= 4)
                return;
            if (auto* p = host.apvts().getParameter (band (b, "q")))
            {
                const float q = host.parameterValue (band (b, "q")) * std::exp (0.6f * w.deltaY);
                host.undo().beginNewTransaction ("EQ Q");
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (q));
                p->endChangeGesture();
            }
        }

    protected:
        void paintGraph (juce::Graphics& g, juce::Rectangle<float>) override
        {
            reader.read (settings);
            const auto& e = eq();
            const auto plot = plotArea();
            const auto& identity = visualIdentityFor (Type::eq);
            const auto text = juce::Colour (0xff8d8c86), gridColour = juce::Colour (0xff2e3434);
            const double rate = sampleRate();

            // Grid (the card EQ's): decades stronger, 0 dB stronger; quiet labels inside.
            g.setFont (fonts::make (10.5f, fonts::Weight::regular, 0.02f));
            for (double hz : { 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 100.0, 200.0, 300.0, 400.0, 500.0, 600.0, 700.0, 800.0, 900.0, 1000.0, 2000.0,
                               3000.0, 4000.0, 5000.0, 6000.0, 7000.0, 8000.0, 9000.0, 10000.0, 20000.0 })
            {
                const int whole = juce::roundToInt (hz);
                const bool decade = whole == 100 || whole == 1000 || whole == 10000;
                g.setColour (gridColour.withAlpha (decade ? 0.95f : 0.45f));
                g.fillRect (juce::Rectangle<float> (std::round (xForHz (hz)), plot.getY(), 1.0f, plot.getHeight()));
            }
            g.setColour (text.withAlpha (0.85f));
            for (const auto& [hz, label] : { std::pair { 20.0, "20" }, { 50.0, "50" }, { 100.0, "100" }, { 200.0, "200" }, { 500.0, "500" }, { 1000.0, "1k" }, { 2000.0, "2k" },
                                             { 5000.0, "5k" }, { 10000.0, "10k" }, { 20000.0, "20k" } })
            {
                const float x = xForHz (hz);
                auto r = juce::Rectangle<float> (x - 18.0f, plot.getBottom() + 3.0f, 36.0f, 14.0f);
                auto just = juce::Justification::centred;
                if (juce::roundToInt (hz) == 20)
                {
                    r = r.withX (x);
                    just = juce::Justification::centredLeft;
                }
                else if (juce::roundToInt (hz) == 20000)
                {
                    r = r.withRightX (x);
                    just = juce::Justification::centredRight;
                }
                g.drawText (label, r, just, false);
            }
            for (int db = -18; db <= 18; db += 6)
            {
                const float y = std::round (yForDb (db));
                g.setColour (db == 0 ? text.withAlpha (0.45f) : gridColour.withAlpha (0.7f));
                g.fillRect (juce::Rectangle<float> (plot.getX(), y, plot.getWidth(), 1.0f));
                if (db % 6 == 0 && db != 18 && db != -18)
                {
                    g.setColour (text.withAlpha (0.85f));
                    g.drawText (db > 0 ? "+" + juce::String (db) : juce::String (db), juce::Rectangle<float> (plot.getX() - 34.0f, y - 7.0f, 28.0f, 14.0f), juce::Justification::centredRight, false);
                }
            }

            // The spectrum: the output filled (the device's ink), the input a quiet line.
            {
                juce::Graphics::ScopedSaveState clip (g);
                g.reduceClipRegion (plot.toNearestInt());
                auto spectrumPath = [&] (const std::array<float, fftSize / 2>& db, bool closed) {
                    juce::Path path;
                    bool any = false;
                    const int x0 = static_cast<int> (plot.getX()), x1 = static_cast<int> (plot.getRight());
                    for (int x = x0; x <= x1; x += 2)
                    {
                        const double f0 = hzForX (static_cast<float> (x)), f1 = hzForX (static_cast<float> (x + 2));
                        const double kf0 = f0 * fftSize / rate, kf1 = f1 * fftSize / rate;
                        float v = floorDb;
                        if (kf1 - kf0 < 1.0)
                        {
                            // (the low end: bins wider than pixels - between two bins, a smooth
                            // cosine blend, not steps)
                            const double kf = juce::jlimit (1.0, static_cast<double> (fftSize / 2 - 2), 0.5 * (kf0 + kf1));
                            const int k = static_cast<int> (kf);
                            const float t = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * static_cast<float> (kf - k));
                            v = db[static_cast<std::size_t> (k)] + t * (db[static_cast<std::size_t> (k + 1)] - db[static_cast<std::size_t> (k)]);
                        }
                        else
                        {
                            const int k0 = juce::jlimit (1, fftSize / 2 - 1, static_cast<int> (kf0));
                            const int k1 = juce::jlimit (k0, fftSize / 2 - 1, static_cast<int> (kf1));
                            for (int k = k0; k <= k1; ++k)
                                v = std::max (v, db[static_cast<std::size_t> (k)]);
                        }
                        any = any || v > floorDb + 1.0f;
                        const float y = plot.getBottom() - plot.getHeight() * (v - floorDb) / (0.0f - floorDb);
                        if (x == x0)
                            path.startNewSubPath (static_cast<float> (x), closed ? plot.getBottom() : y);
                        path.lineTo (static_cast<float> (x), y);
                    }
                    if (closed)
                    {
                        path.lineTo (plot.getRight(), plot.getBottom());
                        path.closeSubPath();
                    }
                    return any ? path : juce::Path();
                };
                const auto pre = spectrumPath (preDb, false);
                g.setColour (text.withAlpha (0.35f));
                g.strokePath (pre, juce::PathStrokeType (1.0f));
                const auto post = spectrumPath (postDb, true);
                g.setGradientFill (juce::ColourGradient (identity.motifInk.withAlpha (0.24f), 0.0f, plot.getY(), identity.motifInk.withAlpha (0.04f), 0.0f, plot.getBottom(), false));
                g.fillPath (post);
                g.setColour (identity.motifInk.withAlpha (0.45f));
                g.strokePath (post, juce::PathStrokeType (0.9f));
            }

            // The selected band's own shape, faint, in its colour.
            auto responseOf = [&] (const fx::Settings& st) {
                fx::Equalizer view;
                view.prepare (rate, 64);
                view.set (st, id.slot);
                view.reset();
                juce::Path path;
                for (int i = 0; i <= 300; ++i)
                {
                    const float x = plot.getX() + plot.getWidth() * static_cast<float> (i) / 300.0f;
                    const float y = juce::jlimit (plot.getY() - 4.0f, plot.getBottom() + 4.0f, yForDb (view.responseDb (hzForX (x))));
                    i == 0 ? path.startNewSubPath (x, y) : path.lineTo (x, y);
                }
                return path;
            };
            {
                auto own = settings;
                auto& oe = own.eq[static_cast<std::size_t> (id.slot)];
                for (int b = 0; b < 4; ++b)
                    oe.bands[static_cast<std::size_t> (b)].on = b == selected && oe.bands[static_cast<std::size_t> (b)].on;
                oe.hpOn = selected == hp && oe.hpOn;
                oe.lpOn = selected == lp && oe.lpOn;
                if (isOn (selected))
                {
                    const auto c = bandColour (selected);
                    auto path = responseOf (own);
                    auto fill = path;
                    fill.lineTo (plot.getRight(), yForDb (0.0));
                    fill.lineTo (plot.getX(), yForDb (0.0));
                    fill.closeSubPath();
                    g.setColour (c.withAlpha (0.12f));
                    g.fillPath (fill);
                    g.setColour (c.withAlpha (0.55f));
                    g.strokePath (path, juce::PathStrokeType (1.0f));
                }
            }
            // The EQ as heard: one crisp warm line.
            {
                const auto warm = juce::Colour (0xffeedfc8);
                auto curve = responseOf (settings);
                auto fill = curve;
                fill.lineTo (plot.getRight(), yForDb (0.0));
                fill.lineTo (plot.getX(), yForDb (0.0));
                fill.closeSubPath();
                g.setColour (warm.withAlpha (0.06f));
                g.fillPath (fill);
                g.setColour (warm);
                g.strokePath (curve, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            // The nodes: hollow markers while off, filled in their type's colour while on.
            for (int b = 0; b <= lp; ++b)
            {
                const auto at = nodePosition (b);
                const auto c = bandColour (b);
                const bool hot = b == hover || b == dragBand;
                if (! isOn (b))
                {
                    const float r = hot ? 5.5f : 4.5f;
                    g.setColour (c.withAlpha (hot ? 0.95f : 0.5f));
                    g.drawEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (at), 1.2f);
                    continue;
                }
                const float r = hot || b == selected ? 7.0f : 6.0f;
                if (b == selected)
                {
                    g.setColour (c.withAlpha (0.45f));
                    g.drawEllipse (juce::Rectangle<float> (2.0f * r + 9.0f, 2.0f * r + 9.0f).withCentre (at), 1.2f);
                }
                g.setColour (juce::Colour (0xff1f2324));
                g.fillEllipse (juce::Rectangle<float> (2.0f * r + 3.0f, 2.0f * r + 3.0f).withCentre (at));
                g.setColour (c);
                g.fillEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (at));
                g.setColour (juce::Colour (0xff1f2324));
                g.setFont (fonts::make (8.5f, fonts::Weight::bold));
                g.drawText (chipName (b).substring (0, b >= 4 ? 1 : 2), juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (at), juce::Justification::centred, false);
            }

            // The top row: the chips, the selected band's switch, type and slope; the fields'
            // captions (the fields are children over this).
            g.setFont (fonts::make (10.5f, fonts::Weight::semibold, 0.06f));
            for (int c = 0; c <= lp; ++c)
            {
                const auto r = chipArea (c);
                const bool on = c == selected;
                g.setColour (on ? juce::Colour (0xff2c3233) : juce::Colour (0xff232829));
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (on ? bandColour (c) : juce::Colour (0xff3a4142));
                g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
                g.setColour (bandColour (c).withAlpha (isOn (c) ? 1.0f : 0.35f));
                g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ r.getX() + 8.0f, r.getCentreY() }));
                g.setColour (on ? juce::Colour (0xffeee6d8) : text);
                g.drawText (chipName (c), r.withTrimmedLeft (12.0f), juce::Justification::centred, false);
            }
            // ON: the selected band's switch (a power glyph, lit in its colour).
            {
                const auto r = powerArea();
                const bool on = isOn (selected);
                const auto c = on ? bandColour (selected) : text.withAlpha (0.6f);
                juce::Path arc;
                arc.addCentredArc (r.getCentreX(), r.getCentreY() + 0.5f, 5.5f, 5.5f, 0.0f, 0.75f, juce::MathConstants<float>::twoPi - 0.75f, true);
                g.setColour (c);
                g.strokePath (arc, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.drawLine (r.getCentreX(), r.getCentreY() - 7.0f, r.getCentreX(), r.getCentreY() - 1.0f, 1.5f);
            }
            if (selected < 4)
            {
                const auto r = typeArea();
                g.setColour (juce::Colour (0xff232829));
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (juce::Colour (0xff3a4142));
                g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
                g.setColour (juce::Colour (0xffe9dcc8));
                g.setFont (fonts::make (10.5f, fonts::Weight::semibold, 0.05f));
                g.drawText (typeName (e.bands[static_cast<std::size_t> (selected)].type).toUpperCase(), r.withTrimmedRight (14.0f), juce::Justification::centred, false);
                juce::Path chevron;
                chevron.startNewSubPath (r.getRight() - 13.0f, r.getCentreY() - 1.5f);
                chevron.lineTo (r.getRight() - 10.0f, r.getCentreY() + 1.5f);
                chevron.lineTo (r.getRight() - 7.0f, r.getCentreY() - 1.5f);
                g.setColour (text);
                g.strokePath (chevron, juce::PathStrokeType (1.2f));
            }
            else
            {
                const bool steep = selected == hp ? e.hpSteep : e.lpSteep;
                for (int k = 0; k < 2; ++k)
                {
                    const auto r = slopeArea (k);
                    const bool on = (k == 1) == steep;
                    g.setColour (on ? juce::Colour (0xff3a4344) : juce::Colour (0xff232829));
                    g.fillRoundedRectangle (r, 3.0f);
                    g.setColour (on ? juce::Colour (0xffeee6d8) : text);
                    g.setFont (fonts::make (10.5f, fonts::Weight::semibold, 0.04f));
                    g.drawText (k == 0 ? "12" : "24", r, juce::Justification::centred, false);
                }
                g.setColour (juce::Colour (0xff3a4142));
                g.drawRoundedRectangle (slopeArea (0).getUnion (slopeArea (1)).reduced (0.5f), 3.0f, 1.0f);
            }
            g.setFont (fonts::make (9.5f, fonts::Weight::semibold, 0.08f));
            g.setColour (text);
            for (const auto& [f, caption] : { std::pair<juce::Component*, const char*> { freqField.get(), "FREQ" }, { gainField.get(), "GAIN" }, { qField.get(), "Q" },
                                              { mixField.get(), "MIX" }, { outField.get(), "OUT" } })
                if (f != nullptr && f->isVisible())
                    g.drawText (caption, juce::Rectangle<int> (f->getX() - 40, f->getY(), 36, f->getHeight()), juce::Justification::centredRight, false);
        }

    private:
        void timerCallback() override { analyse(); }

        const fx::EqSettings& eq() const { return settings.eq[static_cast<std::size_t> (id.slot)]; }
        double sampleRate() const { return host.hostSampleRate() > 0.0 ? host.hostSampleRate() : 48000.0; }
        static bool hasGain (int type) { return type == fx::EqBand::peak || type == fx::EqBand::lowShelf || type == fx::EqBand::highShelf; }
        static juce::String typeName (int type)
        {
            const char* names[] { "Peak", "Low Shelf", "High Shelf", "High Pass", "Low Pass" };
            return names[juce::jlimit (0, 4, type)];
        }
        static juce::String chipName (int c) { return c == hp ? "HP" : c == lp ? "LP" : juce::String (c + 1); }
        juce::Colour bandColour (int c) const
        {
            // The card EQ's restrained colours by kind (no violet: the low pass a teal grey).
            const auto kind = c == hp ? fx::EqBand::highPass : c == lp ? fx::EqBand::lowPass : eq().bands[static_cast<std::size_t> (c)].type;
            switch (kind)
            {
                case fx::EqBand::lowShelf: return juce::Colour (0xffc9a052);
                case fx::EqBand::highShelf: return juce::Colour (0xffc07a5c);
                case fx::EqBand::highPass: return juce::Colour (0xff7f97b2);
                case fx::EqBand::lowPass: return juce::Colour (0xff86a6a4);
                default: return juce::Colour (0xff8fae8a);
            }
        }
        static juce::String band (int b, const char* name) { return juce::String ("band") + juce::String (b + 1) + "." + name; }
        juce::String onId (int b) const { return b == hp ? "hp.on" : b == lp ? "lp.on" : band (b, "on"); }
        bool isOn (int b) const
        {
            const auto& e = eq();
            return b == hp ? e.hpOn : b == lp ? e.lpOn : (b >= 0 && b < 4 && e.bands[static_cast<std::size_t> (b)].on);
        }
        std::vector<juce::String> dragIds (int b) const
        {
            if (b == hp)
                return { pid (id, "hp.freq") };
            if (b == lp)
                return { pid (id, "lp.freq") };
            if (hasGain (eq().bands[static_cast<std::size_t> (b)].type))
                return { pid (id, band (b, "freq").toRawUTF8()), pid (id, band (b, "gain").toRawUTF8()) };
            return { pid (id, band (b, "freq").toRawUTF8()) };
        }
        void setRaw (const juce::String& fullId, float plain)
        {
            if (auto* p = host.apvts().getParameter (fullId))
                p->setValueNotifyingHost (p->convertTo0to1 (plain));
        }
        /** One edit as one gesture (a click: a switch, a type, a slope). */
        void set1 (const juce::String& name, float plain)
        {
            if (auto* p = host.apvts().getParameter (pid (id, name.toRawUTF8())))
            {
                host.undo().beginNewTransaction ("EQ " + name);
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (plain));
                p->endChangeGesture();
            }
            refresh();
        }
        void toggle (const juce::String& name) { set1 (name, host.parameterValue (pid (id, name.toRawUTF8())) >= 0.5f ? 0.0f : 1.0f); }
        void showTypeMenu()
        {
            juce::PopupMenu m;
            const int now = eq().bands[static_cast<std::size_t> (selected)].type;
            for (int t = 0; t < 5; ++t)
                m.addItem (t + 1, typeName (t), true, t == now);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (typeArea().toNearestInt())),
                             [safe = juce::Component::SafePointer<EqGraphic> (this), b = selected] (int r) {
                                 if (safe != nullptr && r > 0)
                                     safe->set1 (band (b, "type"), static_cast<float> (r - 1));
                             });
        }
        int bandAt (juce::Point<float> p) const
        {
            if (! plotArea().expanded (8.0f).contains (p))
                return -1;
            int best = -1;
            float bestD = 11.0f;
            for (int b = 0; b <= lp; ++b)
                if (const float d = nodePosition (b).getDistanceFrom (p); d < bestD)
                {
                    bestD = d;
                    best = b;
                }
            return best;
        }

        // -- geometry ----------------------------------------------------------------------
        float xForHz (double hz) const
        {
            const auto plot = plotArea();
            return plot.getX() + plot.getWidth() * static_cast<float> (std::log (std::clamp (hz, 20.0, 20000.0) / 20.0) / std::log (1000.0));
        }
        double hzForX (float x) const
        {
            const auto plot = plotArea();
            return 20.0 * std::pow (1000.0, std::clamp (static_cast<double> ((x - plot.getX()) / plot.getWidth()), 0.0, 1.0));
        }
        float yForDb (double db) const
        {
            const auto plot = plotArea();
            return plot.getCentreY() - plot.getHeight() * 0.5f * static_cast<float> (db / 18.0);
        }
        double dbForY (float y) const
        {
            const auto plot = plotArea();
            return std::clamp (static_cast<double> ((plot.getCentreY() - y) / (plot.getHeight() * 0.5f)) * 18.0, -18.0, 18.0);
        }
        juce::Rectangle<float> chipArea (int c) const
        {
            float x = 10.0f;
            for (int k = 0; k < c; ++k)
                x += (k >= 4 ? 40.0f : 34.0f) + 4.0f;
            return { x, 7.0f, c >= 4 ? 40.0f : 34.0f, 22.0f };
        }
        juce::Rectangle<float> powerArea() const { return { chipArea (lp).getRight() + 18.0f, 7.0f, 22.0f, 22.0f }; }
        juce::Rectangle<float> typeArea() const { return { powerArea().getRight() + 8.0f, 7.0f, 104.0f, 22.0f }; }
        juce::Rectangle<float> slopeArea (int k) const { return { powerArea().getRight() + 8.0f + 30.0f * static_cast<float> (k), 7.0f, 30.0f, 22.0f }; }

        void rebuildFields()
        {
            for (auto* f : { freqField.get(), gainField.get(), qField.get() })
                if (f != nullptr)
                    removeChildComponent (f);
            freqField.reset();
            gainField.reset();
            qField.reset();
            const auto accent = bandColour (selected);
            if (selected == hp || selected == lp)
                freqField = std::make_unique<osp::plugin::EqValueField> (host, pid (id, selected == hp ? "hp.freq" : "lp.freq"));
            else
            {
                const int type = eq().bands[static_cast<std::size_t> (selected)].type;
                shownType = type;
                freqField = std::make_unique<osp::plugin::EqValueField> (host, pid (id, band (selected, "freq").toRawUTF8()));
                if (hasGain (type))
                    gainField = std::make_unique<osp::plugin::EqValueField> (host, pid (id, band (selected, "gain").toRawUTF8()));
                qField = std::make_unique<osp::plugin::EqValueField> (host, pid (id, band (selected, "q").toRawUTF8()));
            }
            for (auto* f : { freqField.get(), gainField.get(), qField.get() })
                if (f != nullptr)
                {
                    f->setColour (accent);
                    addAndMakeVisible (*f);
                }
            placeFields();
        }
        void placeFields()
        {
            const float left = (selected < 4 ? typeArea().getRight() : slopeArea (1).getRight()) + 52.0f;
            float x = left;
            for (auto* f : { freqField.get(), gainField.get(), qField.get() })
                if (f != nullptr)
                {
                    const int w = f == qField.get() ? 54 : 76;
                    f->setBounds (juce::roundToInt (x), 8, w, 20);
                    x += static_cast<float> (w) + 52.0f;
                }
            const int right = getWidth() - 12;
            if (outField != nullptr)
                outField->setBounds (right - 72, 8, 72, 20);
            if (mixField != nullptr)
                mixField->setBounds (right - 72 - 44 - 64, 8, 64, 20);
        }

        // -- the spectrum (message thread) ------------------------------------------------
        /** The tap's last fftSize samples, windowed, through the FFT, into dB (fast up, slow
            down - a readable fall); with nothing new, the shown spectrum falls to the floor. */
        bool transform (const fx::SpectrumTap& tap, std::array<float, fftSize / 2>& out, bool fresh)
        {
            bool moved = false;
            if (fresh)
            {
                const int head = tap.head.load (std::memory_order_acquire);
                for (int n = 0; n < fftSize; ++n)
                {
                    const int at = (head - fftSize + n + fx::SpectrumTap::size) & (fx::SpectrumTap::size - 1);
                    buffer[static_cast<std::size_t> (n)] = { tap.x[static_cast<std::size_t> (at)].load (std::memory_order_relaxed) * window[static_cast<std::size_t> (n)], 0.0f };
                }
                fft (buffer);
            }
            for (int k = 1; k < fftSize / 2; ++k)
            {
                auto& v = out[static_cast<std::size_t> (k)];
                float db = floorDb;
                if (fresh)
                {
                    const float mag = std::abs (buffer[static_cast<std::size_t> (k)]) * 4.0f / static_cast<float> (fftSize);   // (Hann: x2, one-sided: x2)
                    db = std::max (floorDb, 20.0f * std::log10 (mag + 1.0e-9f));
                }
                const float next = db > v ? db : std::max (db, v - 1.5f);   // (up at once, down 45 dB/s)
                if (std::abs (next - v) > 0.01f)
                    moved = true;
                v = next;
            }
            return moved;
        }
        static void fft (std::vector<std::complex<float>>& a)
        {
            const int n = static_cast<int> (a.size());
            for (int i = 1, j = 0; i < n; ++i)
            {
                int bit = n >> 1;
                for (; j & bit; bit >>= 1)
                    j ^= bit;
                j ^= bit;
                if (i < j)
                    std::swap (a[static_cast<std::size_t> (i)], a[static_cast<std::size_t> (j)]);
            }
            for (int len = 2; len <= n; len <<= 1)
            {
                const float angle = -juce::MathConstants<float>::twoPi / static_cast<float> (len);
                const std::complex<float> wl (std::cos (angle), std::sin (angle));
                for (int i = 0; i < n; i += len)
                {
                    std::complex<float> w (1.0f, 0.0f);
                    for (int k = 0; k < len / 2; ++k)
                    {
                        const auto u = a[static_cast<std::size_t> (i + k)], v = a[static_cast<std::size_t> (i + k + len / 2)] * w;
                        a[static_cast<std::size_t> (i + k)] = u + v;
                        a[static_cast<std::size_t> (i + k + len / 2)] = u - v;
                        w *= wl;
                    }
                }
            }
        }

        osp::plugin::InstrumentHost& host;
        InstanceId id;
        fxparams::Reader reader;
        fx::Settings settings;
        juce::String shown;
        int selected = 0, hover = -1, dragBand = -1, shownType = -1, lastHead = -1;
        std::unique_ptr<osp::plugin::EqValueField> freqField, gainField, qField, mixField, outField;
        std::vector<std::complex<float>> buffer;
        std::array<float, fftSize> window {};
        std::array<float, fftSize / 2> postDb {}, preDb {};
    };

    //==========================================================================
    /** PRESET: < name > - a click lists the factory and user presets, saves and deletes; "*" when
        the settings have moved from it. */
    class PresetBar final : public juce::Component
    {
    public:
        PresetBar (osp::plugin::InstrumentHost& h, InstanceId i) : host (h), id (i) { setTitle ("Preset"); }

        juce::String current() const
        {
            auto& name = presetNames[static_cast<std::size_t> (id.flatIndex())];
            return name.isEmpty() ? juce::String ("Default") : name;
        }

        bool modified() const
        {
            const auto name = presetNames[static_cast<std::size_t> (id.flatIndex())];
            for (const auto& list : { fxpresets::factory (id.type), fxpresets::user (id.type) })
                for (const auto& p : list)
                    if (p.name == name)
                        return ! fxpresets::matches (host, id, p);
            return ! fxpresets::matches (host, id, {});
        }

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (rackui::palette::segmentEdge);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (rackui::palette::segmentFill);
            g.fillRoundedRectangle (r.reduced (1.0f), 3.0f);
            g.setColour (rackui::palette::segmentDivider);
            g.fillRect (juce::Rectangle<float> (26.0f, 4.0f, 1.0f, r.getHeight() - 8.0f));
            g.fillRect (juce::Rectangle<float> (r.getRight() - 27.0f, 4.0f, 1.0f, r.getHeight() - 8.0f));
            g.setColour (rackui::palette::segmentText);
            g.setFont (fonts::make (11.0f, fonts::Weight::regular));
            g.drawText ("<", juce::Rectangle<float> (0.0f, 0.0f, 26.0f, r.getHeight()), juce::Justification::centred, false);
            g.drawText (">", juce::Rectangle<float> (r.getRight() - 26.0f, 0.0f, 26.0f, r.getHeight()), juce::Justification::centred, false);
            g.setColour (juce::Colour (0xff4b4945));
            g.setFont (fonts::make (10.5f, fonts::Weight::medium, 0.02f));
            g.drawText (current() + (modified() ? " *" : ""), r.reduced (30.0f, 0.0f), juce::Justification::centred, true);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.position.x < 26.0f)
                step (-1);
            else if (e.position.x > static_cast<float> (getWidth()) - 26.0f)
                step (1);
            else
                menu();
        }

        /** Save as...: a name for these settings, then saved (the rail's save key, the menu). */
        void promptSave()
        {
            auto* w = new juce::AlertWindow ("Save FX preset", "A name for these settings:", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", current() == "Default" ? juce::String ("My ") + fx::typeName (id.type) : current());
            w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<PresetBar> (this), w] (int result) {
                if (result == 1 && safe != nullptr)
                    if (const auto name = w->getTextEditorContents ("name").trim(); name.isNotEmpty())
                    {
                        fxpresets::saveUser (safe->id.type, fxpresets::capture (safe->host, safe->id, name));
                        presetNames[static_cast<std::size_t> (safe->id.flatIndex())] = name;
                        safe->repaint();
                    }
            }), true);
        }

        /** The overflow key's menu: save, delete, the folder, back to the defaults. */
        void showActions (juce::Component& target)
        {
            juce::PopupMenu m;
            addActions (m);
            m.addSeparator();
            m.addItem ("Reset to defaults", [this] { load ({}); presetNames[static_cast<std::size_t> (id.flatIndex())] = {}; repaint(); });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
        }

    private:
        void addActions (juce::PopupMenu& m)
        {
            const auto mine = fxpresets::user (id.type);
            m.addItem ("Save as...", [safe = juce::Component::SafePointer<PresetBar> (this)] {
                if (safe != nullptr)
                    safe->promptSave();
            });
            bool isUser = false;
            for (const auto& p : mine)
                isUser = isUser || p.name == current();
            m.addItem ("Delete " + current(), isUser, false, [this] {
                fxpresets::removeUser (id.type, current());
                presetNames[static_cast<std::size_t> (id.flatIndex())] = {};
                repaint();
            });
            m.addItem ("Show presets folder", [this] {
                auto folder = fxpresets::userFolder (id.type);
                folder.createDirectory();
                folder.revealToUser();
            });
        }
        std::vector<fxpresets::Preset> all() const
        {
            auto list = fxpresets::factory (id.type);
            for (auto& p : fxpresets::user (id.type))
                list.push_back (std::move (p));
            return list;
        }
        void load (const fxpresets::Preset& p)
        {
            fxpresets::apply (host, id, p);
            presetNames[static_cast<std::size_t> (id.flatIndex())] = p.name;
            repaint();
        }
        void step (int delta)
        {
            const auto list = all();
            if (list.empty())
                return;
            int at = -1;
            for (int i = 0; i < static_cast<int> (list.size()); ++i)
                if (list[static_cast<std::size_t> (i)].name == current())
                    at = i;
            at = (at + delta + static_cast<int> (list.size())) % static_cast<int> (list.size());
            load (list[static_cast<std::size_t> (at)]);
        }
        void menu()
        {
            juce::PopupMenu m;
            m.addSectionHeader ("FACTORY");
            for (const auto& p : fxpresets::factory (id.type))
                m.addItem (p.name, true, p.name == current(), [this, p] { load (p); });
            const auto mine = fxpresets::user (id.type);
            if (! mine.empty())
            {
                m.addSectionHeader ("YOURS");
                for (const auto& p : mine)
                    m.addItem (p.name, true, p.name == current(), [this, p] { load (p); });
            }
            m.addSeparator();
            addActions (m);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        }

        osp::plugin::InstrumentHost& host;
        InstanceId id;
    };

    //==========================================================================
    /** Each effect's faceplate (docs/FX_EFFECTS_LIBRARY.md): its enamel, stripes, glow and glass,
        tuned to the approved V2 renders; interface only. */
    ParameterRack::Faceplate faceplateFor (Type type)
    {
        // V5 PATTERNED HARDWARE: the device's identity (docs/VISUAL_IDENTITY_INDEX.md) - the same
        // enamel and motif as its routing tile; DRIVE keeps its V2 subtitle.
        auto f = faceplateFromIdentity (visualIdentityFor (type));
        if (type == Type::drive)
            f.subtitle = "ANALOG SATURATION / HARMONICS";
        if (type == Type::eq)
        {
            // The graphic EQ: the body a neutral graphite, one with the graph's glass (the card
            // EQ's well), so the graphic reads edge to edge; the brass stays in the title rail.
            f.body = juce::Colour (0xff222627);
            f.glassTop = juce::Colour (0xff1f2324);
            f.glassBottom = juce::Colour (0xff1c2021);
            f.motif = nullptr;
        }
        return f;
    }

    /** MAIN / DETAIL only where one row cannot hold every control: STEREO SPREAD (11 in BOTH),
        COMPRESSOR (10-14 per mode), CONVOLUTION (10), UTILITY (12). FREQ SHIFTER, PHASER,
        FLANGER and FX FILTER show everything in one view. */
    bool pagedType (Type type)
    {
        switch (type)
        {
            case Type::spread:
            case Type::compressor:
            case Type::convolution:
            case Type::utility: return true;
            default: return false;
        }
    }

    /** The controls' share of the body: where the graph is the instrument (EQ, the filter, the
        shifter's map, an impulse), it takes more of the rack. */
    float shareFor (Type type)
    {
        switch (type)
        {
            case Type::phaser:
            case Type::flanger: return 0.645f;   // (one view: nine controls)
            case Type::filter: return 0.58f;     // (one view: up to eight)
            case Type::eq: return 0.52f;
            case Type::shifter:
            case Type::convolution: return 0.55f;
            case Type::drive: return 0.5f;
            default: return 0.62f;
        }
    }

    /** FX FILTER's type family: 0 the plain responses, 1 the combs (COMB DAMP acts), 2 the
        formants (FORMANT acts). */
    int filterFamily (int type) { return type >= 9 ? 2 : (type >= 7 ? 1 : 0); }

    class FxRack final : public ParameterRack
    {
    public:
        explicit FxRack (osp::plugin::InstrumentHost& h) : ParameterRack (h, "FX", visualIdentityFor (shownInstance.type).motifInk, shareFor (shownInstance.type)), id (shownInstance)
        {
            juce::String title = fx::typeName (id.type);
            if (fx::capacity[static_cast<std::size_t> (id.type)] > 1)
                title << " " << (id.slot + 1);
            setHeading (title);
            if (! fx::isLegacy (id.type))
                setHeaderSwitch (pid (id, "on"), "ON", juce::String (fx::typeName (id.type)) + " on (its own bypass: the route keeps it in place)");
            if (id.type == Type::drive || id.type == Type::eq)
                ;   // (one page: DRIVE's own controls; EQ: the graphic is the module)
            else if (id.type == Type::retro)
                setPages ({ "MAIN", "MACHINE", "MOTION", "DYNAMICS" }, &fxPageMemory[static_cast<std::size_t> (id.flatIndex())]);
            else if (pagedType (id.type))
                setPages ({ "MAIN", "DETAIL" }, &fxPageMemory[static_cast<std::size_t> (id.flatIndex())]);
            // (FREQ SHIFTER, PHASER, FLANGER, FX FILTER: everything in one row - no pages)
            if (id.type == Type::eq)
            {
                // EQUALIZER: the card EQ's design at full width, its controls overlays.
                setGraph (std::make_unique<EqGraphic> (h, id));
                setFullWidthGraph (true);
            }
            else
                setGraph (std::make_unique<FxGraph> (h, id));
            // The hardware faceplate: the preset browser and the overflow key in the rail.
            setFaceplate (faceplateFor (id.type));
            if (! fx::isLegacy (id.type))
            {
                auto bar = std::make_unique<PresetBar> (h, id);
                presets = bar.get();
                setRailComponent (std::move (bar), 230);
                setOverflow ([this] (juce::Component& key) { presets->showActions (key); });
                setSaveAction ([this] { presets->promptSave(); });   // (the rail's save key, beside the browser)
            }
        }

    protected:
        bool faceplateLit() const override
        {
            // DRIVE (the instrument's own, no ON of its own): lit while it drives.
            if (id.type == Type::drive)
                return host.parameterValue ("drive") > 0.0f;
            return ParameterRack::faceplateLit();
        }
        int buildKey() const override
        {
            // A mode or a sync switch changes the cells.
            int k = 0;
            if (host.apvts().getParameter (pid (id, "mode")) != nullptr)
                k = juce::roundToInt (value (host, id, "mode")) + 1;
            if (host.apvts().getParameter (pid (id, "sync")) != nullptr)
                k += 10 * (value (host, id, "sync") >= 0.5f ? 1 : 0);
            if (id.type == Type::retro)
                k += 100 * (juce::roundToInt (value (host, id, "digital.era")) + 1);   // (DIGITAL's 80s / 90s / CODEC: other cells)
            if (id.type == Type::filter)
                k += 1000 * filterFamily (juce::roundToInt (value (host, id, "type")));   // (COMB DAMP / FORMANT where they act)
            return k;
        }

        void build (int page) override
        {
            using P = rackui::Position;
            const P on { "ON", "On", 1.0f }, off { "OFF", "Off", 0.0f };
            auto k = [this] (const char* name, const char* caption) -> MiniKnob& { return knob (pid (id, name), caption, textOf (host, pid (id, name))); };
            auto stepped = [this] (const char* name, const char* caption) -> MiniKnob& { return steppedKnob (pid (id, name), caption, textOf (host, pid (id, name))); };
            auto sw = [this, on, off] (const char* name, const char* caption, const char* tip) { switchCell (pid (id, name), caption, { on, off }, tip); };
            auto mode = [this] (std::vector<P> positions, int width, const char* tip) { selectorSegment (pid (id, "mode"), "MODE", std::move (positions), width, tip); };
            const int m = juce::roundToInt (value (host, id, "mode"));
            switch (id.type)
            {
                case Type::drive:
                    // The instrument's own DRIVE (ANDOR OSP's; its macro and its popover's
                    // parameters - never the voice filter's DRIVE / BIAS).
                    selectorSegment ("drive.mode", "MODE", { { "TUBE", "Tube", 0.0f }, { "TAPE", "Tape", 1.0f }, { "CRUNCH", "Crunch", 2.0f } }, 240,
                                     "TUBE: warm, even; TAPE: soft, compressing; CRUNCH: a firmer edge");
                    for (const auto& [pidName, caption] : { std::pair { "drive", "DRIVE" }, std::pair { "drive.tone", "TONE" }, std::pair { "drive.body", "BODY" } })
                        knob (pidName, caption, [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; });
                    break;
                case Type::spread:
                    mode ({ { "HYPER", "Hyper", 0 }, { "DIMENSION", "Dimension", 1 }, { "BOTH", "Both", 2 } }, 240, "HYPER: detuned voices; DIMENSION: a short ensemble; BOTH: one then the other");
                    if (page == 0)
                    {
                        k ("width", "WIDTH");
                        k ("detune", "DETUNE");
                        stepped ("voices", "VOICES");
                        k ("dimSize", "SIZE");
                        k ("dimAmount", "AMOUNT");
                        k ("mix", "MIX");
                        group ({});
                        sw ("lowMono", "LOW MONO", "Below the corner, the sound stays mono (no phasey bass)");
                    }
                    else
                    {
                        k ("hyperRate", "HYPER RATE");
                        k ("dimRate", "DIM RATE");
                        k ("lowMonoHz", "MONO BELOW");
                        k ("output", "OUTPUT");
                    }
                    break;
                case Type::compressor:
                    mode ({ { "CLEAN", "Clean", 0 }, { "FET 76", "FET 76", 1 }, { "MULTIBAND", "Multiband", 2 } }, 260, "CLEAN: transparent; FET 76: fast feedback colour; MULTIBAND: three bands, up and down");
                    if (m == 0)
                    {
                        if (page == 0)
                        {
                            k ("threshold", "THRESHOLD");
                            k ("ratio", "RATIO");
                            k ("attack", "ATTACK");
                            k ("release", "RELEASE");
                            k ("knee", "KNEE");
                            k ("makeup", "MAKEUP");
                            k ("mix", "MIX");
                        }
                        else
                        {
                            segmentCell (pid (id, "detector"), "DETECTOR", { { "RMS", "RMS", 0 }, { "PEAK", "Peak", 1 } }, "What the detector follows");
                            k ("link", "LINK");
                            k ("sidechain", "SC HP");
                            sw ("autoMakeup", "AUTO GAIN", "Makes up half of the reduction at 0 dBFS");
                            k ("output", "OUTPUT");
                        }
                    }
                    else if (m == 1)
                    {
                        if (page == 0)
                        {
                            k ("fet.input", "INPUT");
                            k ("fet.output", "OUTPUT");
                            k ("fet.attack", "ATTACK");
                            k ("fet.release", "RELEASE");
                            stepped ("fet.ratio", "RATIO");
                            k ("fet.color", "COLOR");
                            k ("mix", "MIX");
                        }
                        else
                        {
                            sw ("fet.link", "LINK", "One detector for both sides (the image stays put)");
                            sw ("fet.colorOnly", "COLOR ONLY", "The colour without any gain reduction");
                            k ("output", "TRIM");
                        }
                    }
                    else
                    {
                        if (page == 0)
                        {
                            k ("multi.depth", "DEPTH");
                            k ("multi.time", "TIME");
                            k ("multi.tone", "TONE");
                            k ("multi.low", "LOW X");
                            k ("multi.high", "HIGH X");
                            k ("mix", "MIX");
                            group ({});
                            sw ("multi.match", "MATCH", "Holds the loudness steady as DEPTH changes");
                        }
                        else
                        {
                            k ("multi.threshold", "THRESHOLD");
                            k ("multi.down", "DOWN");
                            k ("multi.up", "UP");
                            k ("multi.deadband", "DEADBAND");
                            k ("multi.lowGain", "LOW");
                            k ("multi.midGain", "MID");
                            k ("multi.highGain", "HIGH");
                        }
                    }
                    break;
                case Type::eq:
                    break;   // (no cells: the graphic's overlays are its controls)
                case Type::filter:
                {
                    // One view; COMB DAMP and FORMANT only with the types they act on.
                    const int family = filterFamily (juce::roundToInt (value (host, id, "type")));
                    listCell (pid (id, "type"), "TYPE", "The filter's response");
                    k ("freq", "FREQ");
                    k ("reso", "RESO");
                    if (family == 1)
                        k ("combDamp", "COMB DAMP");
                    if (family == 2)
                        k ("formantShift", "FORMANT");
                    k ("drive", "DRIVE");
                    k ("pan", "PAN");
                    k ("mix", "MIX");
                    k ("output", "OUTPUT");
                    break;
                }
                case Type::phaser:
                case Type::flanger:
                {
                    const bool phaser = id.type == Type::phaser;
                    if (phaser)
                        mode ({ { "CLASSIC", "Classic", 0 }, { "DEEP", "Deep", 1 } }, 180, "CLASSIC; DEEP: more stages and feedback, a wider sweep");
                    else
                        mode ({ { "CLASSIC", "Classic", 0 }, { "THRU-ZERO", "Through-zero", 1 } }, 200, "THROUGH-ZERO: the sweep passes the dry (6 ms latency)");
                    const bool sync = value (host, id, "sync") >= 0.5f;
                    // One view (nine controls at most).
                    if (sync)
                        stepped ("division", "RATE");
                    else
                        k ("rate", "RATE");
                    switchCell (pid (id, "sync"), "TIMING", { { "FREE", "Free", 0 }, { "SYNC", "Sync", 1 } }, "Its own rate or the host's tempo");
                    k ("depth", "DEPTH");
                    if (phaser)
                    {
                        k ("center", "CENTER");
                        k ("feedback", "FEEDBACK");
                        stepped ("stages", "STAGES");
                    }
                    else
                    {
                        k ("delay", "DELAY");
                        k ("feedback", "FEEDBACK");
                        k ("width", "WIDTH");
                    }
                    k ("stereo", "STEREO");
                    k ("mix", "MIX");
                    k ("output", "OUTPUT");
                    break;
                }
                case Type::shifter:
                    mode ({ { "SHIFT", "Shift", 0 }, { "BLUR", "Blur", 1 } }, 160, "SHIFT: every partial by the same Hz; BLUR: a spectral smear");
                    // One view: the mode's controls, then OUTPUT (no DETAIL page).
                    if (m == 0)
                    {
                        k ("hz", "SHIFT");
                        switchCell (pid (id, "direction"), "DIR", { { "UP", "Up", 0 }, { "DOWN", "Down", 1 } }, "Upwards or downwards");
                        k ("width", "WIDTH");
                        k ("feedback", "FEEDBACK");
                    }
                    else
                    {
                        k ("blurAmount", "AMOUNT");
                        k ("blurTime", "TIME");
                    }
                    k ("mix", "MIX");
                    k ("output", "OUTPUT");
                    break;
                case Type::convolution:
                    mode ({ { "REALISTIC", "Realistic", 0 }, { "CREATIVE", "Creative", 1 } }, 200, "REALISTIC: the impulse as it is; CREATIVE: reverse and size freely");
                    selectorList (pid (id, "impulse"), "IR", 140, "The impulse response it plays");
                    if (page == 0)
                    {
                        k ("predelay", "PRE-DELAY");
                        k ("decay", "DECAY");
                        k ("size", "SIZE");
                        k ("width", "WIDTH");
                        k ("mix", "MIX");
                        k ("output", "OUTPUT");
                    }
                    else
                    {
                        k ("lowCut", "LOW CUT");
                        k ("highCut", "HIGH CUT");
                        sw ("normalize", "NORMALIZE", "Every impulse at the same level");
                        sw ("reverse", "REVERSE", "The impulse played backwards (prepared, not live)");
                    }
                    break;
                case Type::retro:
                    buildRetro (page, m);
                    break;
                case Type::utility:
                    if (page == 0)
                    {
                        k ("gain", "GAIN");
                        k ("pan", "BALANCE");
                        k ("width", "WIDTH");
                        sw ("monoBass", "MONO BASS", "Below the corner, mono");
                        k ("monoBassHz", "BELOW");
                        group ({});
                        sw ("invertL", "INV L", "The left side's polarity");
                        sw ("invertR", "INV R", "The right side's polarity");
                    }
                    else
                    {
                        sw ("hp.on", "HIGH PASS", "A high pass");
                        k ("hp.freq", "HP FREQ");
                        sw ("lp.on", "LOW PASS", "A low pass");
                        k ("lp.freq", "LP FREQ");
                        k ("output", "OUTPUT");
                    }
                    break;
                default: break;
            }
        }

    private:
        /** RETRO (docs/FX_EFFECTS_LIBRARY.md): the personality on MAIN with its own controls
            only (no idle knobs); MACHINE (the medium, its tone, its noise), MOTION (the
            transport and its wear), DYNAMICS (the internal compressor, the noise's behaviour). */
        void buildRetro (int page, int mode)
        {
            auto k = [this] (const char* name, const char* caption) -> MiniKnob& { return knob (pid (id, name), caption, textOf (host, pid (id, name))); };
            const int era = juce::roundToInt (value (host, id, "digital.era"));
            const bool tape = mode == fx::RetroSettings::tape, vinyl = mode == fx::RetroSettings::vinyl;
            const bool codec = mode == fx::RetroSettings::digital && era == fx::RetroSettings::codec;
            const bool converter = mode == fx::RetroSettings::digital && ! codec;
            if (page == 0)
            {
                selectorSegment (pid (id, "mode"), "MODE", { { "TAPE", "Tape", 0 }, { "VINYL", "Vinyl", 1 }, { "DIGITAL", "Digital", 2 } }, 230,
                                 "The medium: TAPE, VINYL or DIGITAL (80s, 90s converters or a CODEC)");
                if (tape)
                    selectorSegment (pid (id, "tape.model"), "MODEL", { { "CASSETTE", "Cassette", 0 }, { "REEL", "Studio reel", 1 } }, 170,
                                     "CASSETTE: a narrower band, more hiss; REEL: wider, a deeper head bump");
                else if (vinyl)
                    selectorSegment (pid (id, "vinyl.rpm"), "RPM", { { juce::String::fromUTF8 ("33\xe2\x85\x93"), juce::String::fromUTF8 ("33\xe2\x85\x93 RPM"), 0 }, { "45", "45 RPM", 1 }, { "78", "78 RPM", 2 } }, 150,
                                     "The platter's speed: its wow turns at this rate");
                else
                    selectorSegment (pid (id, "digital.era"), "TYPE", { { "80s", "80s", 0 }, { "90s", "90s", 1 }, { "CODEC", "Codec", 2 } }, 170,
                                     "80s: truncated, raw; 90s: rounded, dithered, shaped; CODEC: a spectral loss (an STFT)");
                group ("MEDIUM");
                k ("character", "CHARACTER");
                k ("wear", "WEAR");
                if (tape)
                {
                    group ("TRANSPORT");
                    k ("wow.depth", "WOW");
                    k ("flutter.depth", "FLUTTER");
                    group ("TEXTURE");
                    k ("tone", "TONE");
                    k ("noise.level", "NOISE");
                }
                else if (vinyl)
                {
                    group ("GROOVE");
                    k ("wow.depth", "WOW");
                    k ("crackle", "CRACKLE");
                    k ("pops", "POPS");
                }
                else if (converter)
                {
                    group ("CONVERTER");
                    k ("digital.bits", "BITS");
                    k ("digital.rate", "RATE");
                    switchCell (pid (id, "digital.alias"), "ALIAS", { { "CLEAN", "Clean", 0 }, { "HARD", "Hard", 1 } },
                                "CLEAN: the converter's filters (no aliasing); HARD: the raw staircase, aliasing on purpose");
                }
                else
                {
                    group ("CODEC");
                    k ("codec.loss", "LOSS");
                    listCell (pid (id, "codec.mode"), "PACKETS", "STANDARD: masking only; DROP / REPEAT: whole frames; JITTER: phases; COMBINED");
                    k ("codec.artifact", "ARTIFACT");
                }
                group ("LEVEL");
                k ("mix", "MIX");
                k ("output", "OUTPUT");
                return;
            }
            if (page == 1)
            {
                if (codec)
                {
                    group ("CODEC");
                    listCell (pid (id, "codec.stereo"), "STEREO", "STEREO: each side its own mask; JOINT: one mask; MONO: one channel coded");
                    switchCell (pid (id, "codec.artifactOnly"), "ARTIFACT ONLY", { { "ON", "On", 1 }, { "OFF", "Off", 0 } },
                                "Only what the codec changed: processed minus the aligned dry");
                    k ("codec.ambience", "AMBIENCE");
                    group ("TONE");
                    k ("input", "INPUT");
                    k ("tone", "TONE");
                    k ("bandwidth.low", "LOW EDGE");
                    k ("noise.level", "NOISE");
                    return;
                }
                group ("TONE");
                k ("input", "INPUT");
                if (! converter)
                {
                    k ("saturation", "SATURATION");
                    switchCell (pid (id, "preserve"), "PRESERVE", { { "LOW", "Keep the lows clean", 0 }, { "MID", "Neutral", 1 }, { "HIGH", "Keep the highs clean", 2 } },
                                "Which band the saturation leaves alone");
                }
                if (tape)
                    k ("head.bump", "HEAD BUMP");
                if (! tape)
                    k ("tone", "TONE");
                group ("BAND");
                k ("bandwidth.low", "LOW EDGE");
                if (! converter)
                    k ("bandwidth.high", "HIGH EDGE");
                if (! tape)
                {
                    group ("NOISE");
                    k ("noise.level", "LEVEL");   // (TAPE's is on MAIN)
                }
                return;
            }
            if (page == 2)
            {
                if (tape || vinyl)
                {
                    group ("TRANSPORT");
                    if (tape)
                        k ("wow.rate", "WOW RATE");
                    else
                        k ("flutter.depth", "FLUTTER");
                    k ("flutter.rate", "FLUTTER RATE");
                    k ("transport.random", "RANDOM");
                    k ("stereo.phase", "STEREO");
                    group ("DROPOUTS");
                    k ("dropout.rate", "RATE");
                    k ("dropout.depth", "DEPTH");
                    k ("dropout.length", "LENGTH");
                    return;
                }
                group ("MOTION");
                if (converter)
                    k ("digital.mod", "MOTION");
                else
                    k ("codec.speed", "SPEED");
                steppedKnob (pid (id, "seed"), "SEED", textOf (host, pid (id, "seed")));
                return;
            }
            group ("COMPRESSOR");
            k ("compress.amount", "COMPRESS");
            k ("compress.attack", "ATTACK");
            k ("compress.release", "RELEASE");
            k ("compress.tilt", "SC TILT");
            switchCell (pid (id, "compress.link"), "LINK", { { "ON", "On", 1 }, { "OFF", "Off", 0 } }, "One detector for both sides");
            group ("NOISE", true);
            listCell (pid (id, "noise.type"), "NOISE TYPE", "AUTO: the medium's own; or hiss, groove, a room, a converter's");
            k ("noise.follow", "FOLLOW");
            switchCell (pid (id, "noise.automute"), "AUTO MUTE", { { "ON", "On", 1 }, { "OFF", "Off", 0 } }, "250 ms of silence fades the noise away");
        }

        InstanceId id;
        PresetBar* presets = nullptr;
    };
}

namespace
{
    //==========================================================================
    /** The quick settings a tile's click shows (docs/FX_EFFECTS_LIBRARY.md): the effect's name,
        EXPAND (its full rack) and a close; MODE and PRESET; up to four knobs. The same
        parameters as the rack (no copy): automation, undo and presets are theirs. */
    class QuickPopover final : public juce::Component, private juce::Timer
    {
    public:
        QuickPopover (osp::plugin::InstrumentHost& h, InstanceId i, std::function<void()> expandFn, std::function<void()> dismissFn)
            : host (h), id (i), onExpand (std::move (expandFn)), onDismiss (std::move (dismissFn))
        {
            setTitle (juce::String (fx::typeName (id.type)) + " quick settings");
            setWantsKeyboardFocus (true);
            build();
            startTimerHz (8);
        }

        static constexpr int pad = 12, titleH = 30, rowH = 27, knobW = 66, knobH = rackui::cellHeight;

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0x38291d23));
            g.fillRoundedRectangle (r.translated (0.0f, 3.0f), 8.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffbf8f2), 0.0f, 0.0f, juce::Colour (0xfff1eae0), 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r.reduced (0.5f), 8.0f);
            g.setColour (juce::Colour (0xffd2c7b9));
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
            // V5: the title band is the device's enamel and motif (its tile and its rack).
            const auto& identity = visualIdentityFor (id.type);
            {
                juce::Graphics::ScopedSaveState clip (g);
                juce::Path rounded;
                rounded.addRoundedRectangle (r.reduced (0.5f), 8.0f);
                g.reduceClipRegion (rounded);
                const auto band = juce::Rectangle<float> (0.0f, 0.0f, r.getWidth(), static_cast<float> (titleH));
                g.setColour (identity.enamel);
                g.fillRect (band);
                paintFxMotif (g, identity.motif, band.withWidth (70.0f).reduced (2.0f, 3.0f), identity, MotifScale::mini);
                g.setColour (identity.edge.withAlpha (0.6f));
                g.fillRect (band.withTop (band.getBottom() - 1.0f));
            }
            g.setColour (identity.text);
            g.setFont (fonts::make (12.5f, fonts::Weight::bold, 0.07f));
            g.drawText (title(), juce::Rectangle<int> (pad + 52, 5, getWidth() - 170, 20), juce::Justification::centredLeft, true);
            g.setFont (fonts::make (8.5f, fonts::Weight::semibold, 0.08f));
            g.setColour (identity.text.withAlpha (hoverExpand ? 1.0f : 0.78f));
            g.drawRoundedRectangle (expandArea().toFloat().reduced (0.5f), 4.0f, 1.0f);
            g.drawText ("EXPAND", expandArea(), juce::Justification::centred, false);
            g.setColour (identity.text.withAlpha (0.85f));
            g.setFont (fonts::make (13.0f));
            g.drawText (juce::String::fromUTF8 ("\xc3\x97"), closeArea(), juce::Justification::centred, false);
        }

        void resized() override
        {
            int y = titleH + 7;
            if (mode != nullptr || presets != nullptr)
            {
                auto row = juce::Rectangle<int> (pad, y, getWidth() - 2 * pad, rowH);
                if (mode != nullptr)
                    mode->setBounds (row.removeFromLeft (presets != nullptr ? 118 : row.getWidth()));
                if (presets != nullptr)
                    presets->setBounds (row.withTrimmedLeft (mode != nullptr ? 8 : 0));
                y += rowH + 4;
            }
            const int total = static_cast<int> (knobs.size()) * knobW;
            int x = (getWidth() - total) / 2;
            for (auto& k : knobs)
            {
                k->setBounds (x, y, knobW, knobH);
                x += knobW;
            }
        }

        juce::Rectangle<int> preferredSize() const
        {
            const int w = std::max (272, 2 * pad + static_cast<int> (knobs.size()) * knobW);
            const int h = titleH + 7 + (mode != nullptr || presets != nullptr ? rowH + 4 : 0) + knobH + 6;
            return { w, h };
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            if (const bool over = expandArea().contains (e.getPosition()); over != hoverExpand)
            {
                hoverExpand = over;
                repaint();
            }
            setMouseCursor (expandArea().contains (e.getPosition()) || closeArea().contains (e.getPosition()) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        }
        void mouseExit (const juce::MouseEvent&) override
        {
            hoverExpand = false;
            repaint();
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            // (Copies: either one may delete this popover.)
            if (expandArea().contains (e.getPosition()))
                run (onExpand);
            else if (closeArea().contains (e.getPosition()))
                run (onDismiss);
        }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::escapeKey)
                return run (onDismiss);
            if (key == juce::KeyPress::returnKey)
                return run (onExpand);
            return false;
        }

        /** Calls a copy: the callback may delete this popover. */
        static bool run (const std::function<void()>& f)
        {
            if (f == nullptr)
                return false;
            const auto copy = f;
            copy();
            return true;
        }

        juce::Rectangle<int> expandArea() const { return { getWidth() - 96, 7, 58, 18 }; }
        juce::Rectangle<int> closeArea() const { return { getWidth() - 32, 4, 24, 24 }; }
        int knobCount() const noexcept { return static_cast<int> (knobs.size()); }
        bool hasMode() const noexcept { return mode != nullptr; }
        bool hasPresets() const noexcept { return presets != nullptr; }

    private:
        juce::String title() const
        {
            juce::String t = fx::typeName (id.type);
            if (fx::capacity[static_cast<std::size_t> (id.type)] > 1)
                t << " " << (id.slot + 1);
            return t;
        }

        juce::String controlsKey() const
        {
            juce::StringArray ids;
            for (const auto& c : quickControls (host, id))
                ids.add (c.id);
            return ids.joinIntoString ("|");
        }

        void build()
        {
            knobs.clear();
            mode.reset();
            presets.reset();
            if (const auto m = quickModeId (id); m.isNotEmpty())
                if (auto* p = host.apvts().getParameter (m))
                {
                    auto selector = std::make_unique<osp::plugin::ValueSelector> (*p, juce::String());
                    selector->setRackStyle (osp::plugin::ValueSelector::RackStyle::mode);
                    selector->setTooltip ("MODE");
                    mode = std::move (selector);
                    addAndMakeVisible (*mode);
                }
            if (! fx::isLegacy (id.type))
            {
                presets = std::make_unique<PresetBar> (host, id);
                addAndMakeVisible (*presets);
            }
            for (const auto& c : quickControls (host, id))
            {
                if (host.apvts().getParameter (c.id) == nullptr)
                    continue;
                auto k = std::make_unique<MiniKnob> (host.apvts(), c.id, c.caption, textOf (host, c.id));
                k->setArcColour (visualIdentityFor (id.type).enamel);
                k->setRackMetrics (true);
                addAndMakeVisible (*k);
                knobs.push_back (std::move (k));
                if (knobs.size() == 4)
                    break;
            }
            builtKey = controlsKey();
        }

        void timerCallback() override
        {
            // A MODE change can change which controls are the useful ones (COMPRESSOR, SHIFT).
            if (controlsKey() != builtKey)
            {
                build();
                if (auto* parent = getParentComponent())
                    setBounds (getBounds().withSize (preferredSize().getWidth(), preferredSize().getHeight()).constrainedWithin (parent->getLocalBounds()));
                resized();
            }
            if (presets != nullptr)
                presets->repaint();   // (the "*" follows the settings)
        }

        osp::plugin::InstrumentHost& host;
        InstanceId id;
        std::function<void()> onExpand, onDismiss;
        std::unique_ptr<juce::Component> mode, presets;
        std::vector<std::unique_ptr<MiniKnob>> knobs;
        juce::String builtKey;
        bool hoverExpand = false;
    };
}

std::unique_ptr<juce::Component> createFxQuickPopover (osp::plugin::InstrumentHost& host, InstanceId id, std::function<void()> expand, std::function<void()> dismiss,
                                                      juce::Rectangle<int>& size)
{
    auto q = std::make_unique<QuickPopover> (host, id, std::move (expand), std::move (dismiss));
    size = q->preferredSize();
    return q;
}

std::unique_ptr<osp::plugin::rack::Module> createFxRack (osp::plugin::InstrumentHost& host) { return std::make_unique<FxRack> (host); }

InstanceId fxRackShown() { return shownInstance; }

namespace
{
    EqGraphic* eqGraphicOf (osp::plugin::rack::Module& module)
    {
        for (auto* c : module.getChildren())
            if (auto* e = dynamic_cast<EqGraphic*> (c))
                return e;
        return nullptr;
    }
}

float analyseFxEqSpectrum (osp::plugin::rack::Module& module)
{
    if (auto* e = eqGraphicOf (module))
    {
        for (int i = 0; i < 3; ++i)
            e->analyse();
        return e->spectrumPeakDb();
    }
    return -1000.0f;
}

int fxEqSelectedBand (osp::plugin::rack::Module& module)
{
    auto* e = eqGraphicOf (module);
    return e != nullptr ? e->selectedBand() : -1;
}

juce::Point<float> fxEqNode (osp::plugin::rack::Module& module, int band)
{
    auto* e = eqGraphicOf (module);
    return e != nullptr ? e->nodePosition (band) : juce::Point<float>();
}

juce::Component* fxEqGraphic (osp::plugin::rack::Module& module) { return eqGraphicOf (module); }

void openFxRack (andor::shell::ShellEditor& editor, InstanceId id)
{
    using osp::plugin::rack::Kind;
    switch (id.type)
    {
        case Type::movement: editor.setRackExpanded (Kind::movement, true); return;
        case Type::echo: editor.setRackExpanded (Kind::echo, true); return;
        case Type::space: editor.setRackExpanded (Kind::space, true); return;
        case Type::drive: break;   // (V2: DRIVE's hardware rack - its macro's popover stays the quick view)
        default: break;
    }
    // One FX rack at a time: another effect swaps the module (the rack keeps its height).
    shownInstance = id;
    editor.setRackExpanded (Kind::fx, false);
    editor.setRackExpanded (Kind::fx, true);
}

juce::String quickModeId (InstanceId id)
{
    switch (id.type)
    {
        case Type::drive: return "drive.mode";
        case Type::echo: return "echo.type";
        case Type::space: return "space.engine";
        case Type::spread:
        case Type::compressor:
        case Type::phaser:
        case Type::flanger:
        case Type::shifter:
        case Type::convolution:
        case Type::retro: return pid (id, "mode");
        default: return {};
    }
}

std::vector<QuickControl> quickControls (osp::plugin::InstrumentHost& host, InstanceId id)
{
    switch (id.type)
    {
        case Type::drive: return { { "drive", "DRIVE" }, { "drive.tone", "TONE" }, { "drive.body", "BODY" } };
        case Type::movement: return { { "motion", "MOVEMENT" } };
        case Type::echo: return { { "echo", "ECHO" }, { "echo.feedback", "FEEDBACK" }, { "echo.tone", "TONE" } };
        case Type::space: return { { "space", "SPACE" }, { "space.decay", "DECAY" }, { "space.size", "SIZE" } };
        case Type::spread: return { { pid (id, "width"), "WIDTH" }, { pid (id, "detune"), "DETUNE" }, { pid (id, "mix"), "MIX" } };
        case Type::compressor:
        {
            const int m = juce::roundToInt (value (host, id, "mode"));
            if (m == 1)
                return { { pid (id, "fet.input"), "INPUT" }, { pid (id, "fet.output"), "OUTPUT" }, { pid (id, "mix"), "MIX" } };
            if (m == 2)
                return { { pid (id, "multi.depth"), "DEPTH" }, { pid (id, "multi.time"), "TIME" }, { pid (id, "mix"), "MIX" } };
            return { { pid (id, "threshold"), "THRESHOLD" }, { pid (id, "ratio"), "RATIO" }, { pid (id, "mix"), "MIX" } };
        }
        case Type::eq: return { { pid (id, "band1.gain"), "LOW" }, { pid (id, "band2.gain"), "LOW MID" }, { pid (id, "band3.gain"), "HIGH MID" }, { pid (id, "band4.gain"), "HIGH" } };
        case Type::filter: return { { pid (id, "freq"), "FREQ" }, { pid (id, "reso"), "RESO" }, { pid (id, "mix"), "MIX" } };
        case Type::phaser: return { { pid (id, "rate"), "RATE" }, { pid (id, "depth"), "DEPTH" }, { pid (id, "feedback"), "FEEDBACK" }, { pid (id, "mix"), "MIX" } };
        case Type::flanger: return { { pid (id, "rate"), "RATE" }, { pid (id, "depth"), "DEPTH" }, { pid (id, "feedback"), "FEEDBACK" }, { pid (id, "mix"), "MIX" } };
        case Type::shifter:
            return juce::roundToInt (value (host, id, "mode")) == 0 ? std::vector<QuickControl> { { pid (id, "hz"), "SHIFT" }, { pid (id, "feedback"), "FEEDBACK" }, { pid (id, "mix"), "MIX" } }
                                                                    : std::vector<QuickControl> { { pid (id, "blurAmount"), "AMOUNT" }, { pid (id, "blurTime"), "TIME" }, { pid (id, "mix"), "MIX" } };
        case Type::convolution: return { { pid (id, "decay"), "DECAY" }, { pid (id, "size"), "SIZE" }, { pid (id, "mix"), "MIX" } };
        case Type::utility: return { { pid (id, "gain"), "GAIN" }, { pid (id, "pan"), "BALANCE" }, { pid (id, "width"), "WIDTH" } };
        case Type::retro: return { { pid (id, "character"), "CHARACTER" }, { pid (id, "wear"), "WEAR" }, { pid (id, "mix"), "MIX" } };
        default: return {};
    }
}

} // namespace andor::analog
