#include "AnalogRacks.h"
#include "AnalogEngine.h"
#include "AnalogMath.h"
#include "AnalogParameters.h"
#include "FxIdentity.h"
#include "Envelope.h"
#include "ShellProcessor.h"
#include "Design.h"
#include "JunoChorus.h"
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

    DriveCurve driveCurve (double drive, double bias) noexcept
    {
        // As the synth sets them: driveGain = 2^(4 DRIVE), driveBias = 0.6 BIAS.
        DriveCurve c;
        c.gain = std::exp2 (4.0 * std::clamp (drive, 0.0, 1.0));
        c.bias = 0.6 * std::clamp (bias, 0.0, 1.0);
        return c;
    }

    double DriveCurve::at (double x) const noexcept { return std::tanh (gain * x + bias) - std::tanh (bias); }
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

    //==========================================================================
    /** The graphs' type and lines: the approved ECHO / SPACE graph's, one scale for every
        page. `meta` what a graph shows and its values (10 px), `label` its axes and row names
        (9.5 px), the grid's lines white at 5 % and the axes at 7 %. */
    namespace gtype
    {
        juce::Font meta() { return fonts::make (10.0f, fonts::Weight::regular, 0.02f); }
        juce::Font label() { return fonts::make (9.5f, fonts::Weight::medium, 0.06f); }
        const juce::Colour metaColour { 0xd9c2c7c5 };    // #c2c7c5 at 85 %
        const juce::Colour labelColour { 0xcc9aa09e };   // #9aa09e at 80 %
        const juce::Colour grid { 0x0dffffff }, axis { 0x12ffffff }, track { 0x0dffffff };
        const juce::Colour strong { 0xffd5dcdc };        // an empty state's one line

        /** The top line: what it is (left), its values (right); as ECHO's. */
        void header (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& left, const juce::String& right, float alpha = 1.0f)
        {
            const juce::Rectangle<float> line (area.getX() + 9.0f, area.getY() + 4.0f, area.getWidth() - 18.0f, 12.0f);
            g.setFont (meta());
            g.setColour (metaColour.withMultipliedAlpha (alpha));
            g.drawText (left, line, juce::Justification::centredLeft, true);
            g.drawText (right, line, juce::Justification::centredRight, true);
        }

        /** The foot line (a short note on what the picture is, when it needs one). */
        void foot (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& left, float alpha = 0.8f)
        {
            g.setFont (meta());
            g.setColour (metaColour.withMultipliedAlpha (alpha));
            g.drawText (left, juce::Rectangle<float> (area.getX() + 9.0f, area.getBottom() - 15.0f, area.getWidth() - 18.0f, 12.0f), juce::Justification::centredLeft, true);
        }

        void small (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Justification j, float alpha = 1.0f)
        {
            g.setFont (label());
            g.setColour (labelColour.withMultipliedAlpha (alpha));
            g.drawText (text, r, j, false);
        }
    }

    juce::String middleDot() { return juce::String::fromUTF8 (" \xc2\xb7 "); }

    //==========================================================================
    /** FILTER's graph: what the shown page is about. FILTER: the low-pass's response (the
        engine's model linearised: no saturation, before the envelope and key tracking). VOICE:
        AMP the amplitude envelope (or the gate) as it is set; MOD LFO 1's swing on each target
        and its fade-in, from the settings; CHARACTER the drive stage's curve and the chorus'
        sweep, from the engine's own equations and figures. PAIR: what the two oscillators do to
        each other, as a schematic with the settings' values. */
    class FilterGraph final : public GraphFrame
    {
    public:
        FilterGraph (InstrumentHost& h, std::function<int()> p, std::function<int()> s) : host (h), page (std::move (p)), sub (std::move (s))
        {
            reader.attach (h.apvts());
            engine = dynamic_cast<AnalogEngine*> (&dynamic_cast<andor::shell::ShellProcessor&> (h).productEngine());
        }

        // V3: the FILTER page follows what the filter does (FX_RACK_V3 E). LIVE: the most recent
        // sounding voice's cutoff from the engine's telemetry (Synth::FilterTap). PREVIEW: when
        // an envelope, ENV, TONE or the link is edited with nothing playing, a silent notional
        // C4 at velocity 100 through the synth's own Envelope and mapping (never heard).
        enum class Mode { idle, live, preview };
        Mode mode() const noexcept { return shownMode; }
        /** Tests: the cutoffs shown (MIDI notes): base, now, the envelope's reach, its peak. */
        struct Shown
        {
            double baseNote = 0.0, nowNote = 0.0, reachNote = 0.0, peakNote = 0.0;
            int note = 60, velocity = 100, stage = 0;
        };
        const Shown& shownCutoffs() const noexcept { return shown3; }
        void replayPreview() { startPreview(); }

        void refresh() override
        {
            const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
            const double dt = lastRefresh > 0.0 ? std::clamp (now - lastRefresh, 0.0, 0.2) : 0.0;
            lastRefresh = now;
            if (page() == 0)
                advanceFilter (dt);
            juce::String sig (page() * 16 + sub());
            for (const auto& id : { params::tone, params::resonance, params::filterModel, params::otaCharacter, params::otaSlope, params::otaHpf, params::junoVariant,
                                    params::vcaMode, juce::String ("attack"), juce::String ("decay"), juce::String ("sustainLevel"), juce::String ("release"),
                                    params::polyphony, params::unison, params::sync, params::ringMix, params::blend, params::polyModEnv, params::polyModOsc2,
                                    params::polyModToPitch1, params::polyModToWidth1, params::polyModToCutoff, params::noise, params::noiseColor, params::envDepth,
                                    params::motion, params::lfoCutoff, params::lfoPitch1, params::lfoPitch2, params::lfoDelay, params::lfoRise, params::lfoWheel,
                                    params::analogAmount, params::phaseMode, params::driveBias, params::driveTrim, params::filterDrive, params::chorusMode,
                                    params::chorusNoise })
                sig << "|" << juce::String (host.parameterValue (id), 3);
            if (sig != shown)
            {
                shown = sig;
                repaint();
            }
            else if (page() == 0 && (shownMode != Mode::idle || settling))
                repaint();   // (moving: every frame; idle: only when something changed)
        }
        /** Tests: one refresh as if `seconds` had passed (no message loop). */
        void advanceBy (double seconds)
        {
            lastRefresh = juce::Time::getMillisecondCounterHiRes() * 0.001;
            advanceFilter (seconds);
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (page() == 0)
                startPreview();   // (a click replays the preview - never a sound)
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
        params::Reader reader;
        AnalogEngine* engine = nullptr;
        Mode shownMode = Mode::idle;
        Shown shown3;
        double lastRefresh = 0.0, drawnNote = -1000.0, previewTime = 0.0;
        static constexpr double previewRate = 48000.0;
        Envelope previewEnv;
        bool previewing = false, previewReleased = false, settling = false;
        int trackedSlot = -1, bestSlot = -1;
        std::uint64_t trackedOrder = 0;
        juce::String watched;
        juce::String text (const juce::String& id) const { return textOf (host, id) (plain (host, id)); }

        // The FILTER page's motion (message thread, ~30 Hz): which voice, its cutoffs; or the
        // preview's note through the same envelope; the drawn cutoff eases towards the truth
        // (drawing only), so a fast attack still reads as a jump to its peak.
        void startPreview()
        {
            previewing = true;
            previewTime = 0.0;
            previewEnv = {};
            previewEnv.setSampleRate (previewRate);
            previewEnv.noteOn();
            previewReleased = false;
        }
        static juce::String noteName (int note)
        {
            static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            return juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 1);
        }
        void advanceFilter (double dt)
        {
            const auto patch = reader.read();
            const double cap = engine != nullptr ? engine->cutoffNoteHigh() : 135.0;
            auto clampNote = [cap] (double n) { return std::clamp (n, -24.0, cap); };
            // A live voice: the most recent note-on that sounds. A newer note takes over; the one
            // followed is kept through its release; when it ends, the next most recent one.
            Synth::FilterTap::Snapshot tap, best, tracked;
            bool found = false, trackedAlive = false;
            if (engine != nullptr)
                for (int slot = 0; slot < AnalogEngine::voiceSlots; ++slot)
                    if (const auto* t = engine->filterTap (slot); t != nullptr && t->read (tap) && tap.active)
                    {
                        if (slot == trackedSlot && tap.order == trackedOrder)
                        {
                            tracked = tap;
                            trackedAlive = true;
                        }
                        if (! found || tap.order > best.order)
                        {
                            best = tap;
                            bestSlot = slot;
                            found = true;
                        }
                    }
            if (found && trackedAlive && best.order <= trackedOrder)
                best = tracked;   // (nothing newer: keep following it, released or not)
            else if (found)
            {
                trackedSlot = bestSlot;
                trackedOrder = best.order;
            }
            // What starts a preview (with nothing playing): the envelopes, ENV, TONE, the link.
            juce::String watch;
            for (const auto& id : { params::filterAttack, params::filterDecay, params::filterSustain, params::filterRelease, params::envDepth, params::tone,
                                    params::envLink, params::velocityToFilter, juce::String ("attack"), juce::String ("decay"), juce::String ("sustainLevel"),
                                    juce::String ("release") })
                watch << juce::String (host.parameterValue (id), 4) << "|";
            if (watch != watched)
            {
                const bool first = watched.isEmpty();
                watched = watch;
                if (! first && ! found)
                    startPreview();   // (restarts from its note-on: the new values, never the old run)
            }
            const double restBase = math::hzToNote (math::toneToHz (patch.tone));   // C4: KEYS add nothing
            if (found)
            {
                previewing = false;
                shownMode = Mode::live;
                shown3.baseNote = clampNote (best.baseNote);
                shown3.nowNote = clampNote (best.cutoffNote);
                shown3.reachNote = clampNote (best.baseNote + best.envDepth);
                shown3.peakNote = clampNote (best.baseNote + best.envPeak * best.envDepth);
                shown3.note = best.note;
                shown3.velocity = juce::roundToInt (best.velocity * 127.0f);
                shown3.stage = best.stage;
            }
            else if (previewing)
            {
                shownMode = Mode::preview;
                // ENV LINK: the AMP envelope's times shape the filter (as Synth::controlUpdate).
                if (patch.envLink)
                    previewEnv.setTimes (patch.ampAttack, patch.ampDecay, patch.ampSustain, patch.ampRelease);
                else
                    previewEnv.setTimes (patch.filterAttack, patch.filterDecay, patch.filterSustain, patch.filterRelease);
                const double attack = patch.envLink ? patch.ampAttack : patch.filterAttack;
                const double decay = patch.envLink ? patch.ampDecay : patch.filterDecay;
                const double hold = std::clamp (attack + decay, 0.35, 3.0) + 0.6;
                const int steps = static_cast<int> (dt * previewRate);
                for (int i = 0; i < steps; ++i)
                {
                    if (! previewReleased && previewTime + static_cast<double> (i) / previewRate >= hold)
                    {
                        previewEnv.noteOff();
                        previewReleased = true;
                    }
                    previewEnv.next();
                }
                previewTime += dt;
                const double velocity = 100.0 / 127.0;
                const double depth = patch.envDepthSemitones * (1.0 - patch.velocityToFilter + patch.velocityToFilter * velocity);
                shown3.baseNote = clampNote (restBase);
                shown3.nowNote = clampNote (restBase + previewEnv.value() * depth);
                shown3.reachNote = clampNote (restBase + depth);
                shown3.peakNote = shown3.reachNote;
                shown3.note = 60;
                shown3.velocity = 100;
                shown3.stage = static_cast<int> (previewEnv.currentStage());
                if ((previewReleased && ! previewEnv.isActive()) || previewTime > 12.0)
                    previewing = false;
            }
            else
            {
                shownMode = Mode::idle;
                const double depth = patch.envDepthSemitones * (1.0 - patch.velocityToFilter + patch.velocityToFilter * (100.0 / 127.0));
                shown3.baseNote = shown3.nowNote = clampNote (restBase);
                shown3.reachNote = shown3.peakNote = clampNote (restBase + depth);
                shown3.stage = 0;
            }
            // Drawing only: ease the drawn cutoff (about 45 ms); settle when it arrives.
            const double target = shown3.nowNote;
            if (drawnNote < -100.0 || dt <= 0.0)
                drawnNote = target;
            else
                drawnNote += (1.0 - std::exp (-dt / 0.045)) * (target - drawnNote);
            settling = std::abs (drawnNote - target) > 0.02;
            if (! settling)
                drawnNote = target;
        }

        void paintFilter (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
        {
            if (drawnNote < -100.0)
                advanceFilter (0.0);   // (a first paint: the state now)
            filtergraph::Settings s;
            s.resonance = 0.01 * plain (host, params::resonance);
            s.ota = choice (host, params::filterModel) == 1;
            s.jupiter = choice (host, params::otaCharacter) == 1;
            s.twoPole = choice (host, params::otaSlope) == 1;
            s.highPass = choice (host, params::otaHpf) - 1;
            s.junoVariant = choice (host, params::junoVariant);
            const auto plot = area.reduced (14.0f, 0.0f).withTrimmedTop (24.0f).withTrimmedBottom (34.0f);
            constexpr double lo = 20.0, hi = 20000.0, topDb = 24.0, bottomDb = -48.0;
            auto xOf = [&] (double hz) { return plot.getX() + plot.getWidth() * static_cast<float> (std::log (std::clamp (hz, lo, hi) / lo) / std::log (hi / lo)); };
            auto yOf = [&] (double db) { return plot.getY() + plot.getHeight() * static_cast<float> ((topDb - std::clamp (db, bottomDb, topDb)) / (topDb - bottomDb)); };
            // Grid: decades and every 12 dB (0 dB a step stronger).
            for (const double hz : { 100.0, 1000.0, 10000.0 })
            {
                g.setColour (gtype::grid);
                g.fillRect (juce::Rectangle<float> (xOf (hz) - 0.5f, plot.getY(), 1.0f, plot.getHeight()));
                gtype::small (g, hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + " kHz" : juce::String (hz, 0) + " Hz",
                              juce::Rectangle<float> (xOf (hz) - 30.0f, plot.getBottom() + 3.0f, 60.0f, 12.0f), juce::Justification::centred);
            }
            for (const double db : { 12.0, 0.0, -12.0, -24.0, -36.0 })
            {
                g.setColour (db == 0.0 ? gtype::axis : gtype::grid);
                g.fillRect (juce::Rectangle<float> (plot.getX(), yOf (db) - 0.5f, plot.getWidth(), 1.0f));
            }
            auto curveAt = [&] (double cutoffHz, juce::Path* fill) {
                auto c = s;
                c.cutoffHz = cutoffHz;
                juce::Path curve;
                for (int i = 0; i <= 240; ++i)
                {
                    const double hz = lo * std::pow (hi / lo, i / 240.0);
                    const juce::Point<float> p (xOf (hz), yOf (filtergraph::responseDb (c, hz)));
                    if (i == 0)
                    {
                        curve.startNewSubPath (p);
                        if (fill != nullptr)
                            fill->startNewSubPath (p.x, plot.getBottom());
                    }
                    else
                        curve.lineTo (p);
                    if (fill != nullptr)
                        fill->lineTo (p);
                }
                if (fill != nullptr)
                {
                    fill->lineTo (plot.getRight(), plot.getBottom());
                    fill->closeSubPath();
                }
                return curve;
            };
            auto hzOf = [] (double note) { return math::noteToHz (note); };
            auto hzText = [] (double hz) { return hz >= 1000.0 ? juce::String (hz / 1000.0, 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz"; };
            const double baseHz = hzOf (shown3.baseNote), nowHz = hzOf (drawnNote), reachHz = hzOf (shown3.reachNote);
            const bool moving = std::abs (shown3.reachNote - shown3.baseNote) > 0.05;
            const float dash[] { 3.0f, 3.0f };
            // 1. ENVELOPE REACH: a dashed ghost (where the envelope takes the cutoff at its top).
            if (moving)
            {
                const auto reach = curveAt (reachHz, nullptr);
                juce::Path dashed;
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, reach, dash, 2);
                g.setColour (colour.withAlpha (0.34f));
                g.fillPath (dashed);
                g.setColour (colour.withAlpha (0.28f));
                g.drawDashedLine (juce::Line<float> (xOf (reachHz), plot.getY(), xOf (reachHz), plot.getBottom()), dash, 2, 1.0f);
            }
            // 2. BASE: muted and thin under the moving one (at rest it IS the curve).
            if (shownMode != Mode::idle)
            {
                g.setColour (juce::Colour (0xffd9dcd8).withAlpha (0.3f));
                g.strokePath (curveAt (baseHz, nullptr), juce::PathStrokeType (1.0f));
                g.setColour (juce::Colour (0xffd9dcd8).withAlpha (0.22f));
                g.fillRect (juce::Rectangle<float> (xOf (baseHz) - 0.5f, plot.getY(), 1.0f, plot.getHeight()));
            }
            // 3. NOW: the response at the cutoff the filter uses (LIVE), or the preview's.
            juce::Path fill;
            const auto curve = curveAt (nowHz, &fill);
            const auto ink = shownMode == Mode::preview ? colour.withMultipliedSaturation (0.7f) : colour;
            g.setGradientFill (juce::ColourGradient (ink.withAlpha (0.22f), 0.0f, plot.getY(), ink.withAlpha (0.02f), 0.0f, plot.getBottom(), false));
            g.fillPath (fill);
            g.setColour (ink.withAlpha (0.25f));
            g.strokePath (curve, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (ink);
            g.strokePath (curve, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (ink.withAlpha (shownMode == Mode::idle ? 0.4f : 0.85f));
            g.fillRect (juce::Rectangle<float> (xOf (nowHz) - 0.75f, plot.getY(), 1.5f, plot.getHeight()));
            // Header: the filter's type, and what is shown (LIVE / PREVIEW / IDLE).
            const juce::String type = s.ota ? juce::String ("OTA ") + (s.jupiter ? "JUPITER" : "JUNO") + middleDot() + (s.twoPole ? "12 dB" : "24 dB")
                                            : "LADDER" + middleDot() + "24 dB";
            juce::String state;
            if (shownMode == Mode::live)
                state = "LIVE " + noteName (shown3.note) + middleDot() + "VEL " + juce::String (shown3.velocity);
            else if (shownMode == Mode::preview)
                state = "PREVIEW C4" + middleDot() + "VEL 100" + middleDot() + "SILENT";
            else
                state = "IDLE" + middleDot() + "RESO " + juce::String (juce::roundToInt (100.0 * s.resonance)) + " %";
            gtype::header (g, area, type + (s.highPass >= 0 ? middleDot() + "HPF " + juce::String (s.highPass) : juce::String()), state);
            // Foot: the cutoffs (the engine's mapping, clamped as the filter is) and the stage.
            juce::String figures = "BASE " + hzText (baseHz);
            if (shownMode != Mode::idle)
                figures << "   NOW " << hzText (nowHz);
            if (moving)
                figures << "   " << (shownMode == Mode::live ? "PEAK " + hzText (hzOf (shown3.peakNote)) : "REACH " + hzText (reachHz));
            const bool linked = plain (host, params::envLink) >= 0.5f;
            gtype::foot (g, area, figures + (linked ? middleDot() + "AMP ENV" : juce::String()));
            if (shownMode != Mode::idle)
            {
                // A / D / S / R: the envelope's stage now (Envelope::Stage 1..4), the current lit.
                static const char* letters[] { "A", "D", "S", "R" };
                const float w = 11.0f;
                float x = area.getRight() - 9.0f - 4.0f * w;
                g.setFont (fonts::make (8.5f, fonts::Weight::semibold, 0.04f));
                for (int i = 0; i < 4; ++i, x += w)
                {
                    const bool lit = shown3.stage == i + 1;
                    g.setColour (lit ? ink : juce::Colour (0xffd9dcd8).withAlpha (0.35f));
                    g.drawText (letters[i], juce::Rectangle<float> (x, area.getBottom() - 15.0f, w, 12.0f), juce::Justification::centred, false);
                }
            }
        }

        void paintVoice (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
        {
            // The amplitude as it is set: the AMP envelope (A D S R), or the gate.
            const bool gate = choice (host, params::vcaMode) == 1;
            const auto plot = area.reduced (16.0f, 0.0f).withTrimmedTop (28.0f).withTrimmedBottom (26.0f);
            g.setColour (gtype::axis);
            g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getBottom(), plot.getWidth(), 1.0f));
            g.setColour (gtype::grid);
            g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getY(), plot.getWidth(), 1.0f));
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
                // The stages' boundaries, faint.
                g.setColour (gtype::grid);
                for (const double t : { a, a + d, a + d + hold })
                    g.fillRect (juce::Rectangle<float> (x (t) - 0.5f, plot.getY(), 1.0f, plot.getHeight()));
            }
            juce::Path fill (p);
            fill.lineTo (plot.getRight(), plot.getBottom());
            fill.closeSubPath();
            g.setColour (colour.withAlpha (0.12f));
            g.fillPath (fill);
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            auto* voices = host.apvts().getParameter (params::polyphony);
            gtype::header (g, area, gate ? "AMP" + middleDot() + "GATE" : "AMP" + middleDot() + "ENVELOPE",
                           "VOICES " + (voices != nullptr ? voices->getCurrentValueAsText() : juce::String()) + (plain (host, params::unison) >= 0.5f ? middleDot() + "UNISON" : juce::String()));
            if (! gate)
                gtype::foot (g, area, "time on a square-root scale");
        }

        void paintMod (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
        {
            // LFO 1 as the engine applies it: each target swings by AMOUNT x its depth (the wheel,
            // when linked, brings in the rest up to the full depth); DELAY then RISE fade it in.
            using namespace params;
            const float amount = juce::jlimit (0.0f, 1.0f, 0.01f * plain (host, motion));
            const bool wheel = plain (host, lfoWheel) >= 0.5f;
            auto body = area.reduced (14.0f, 0.0f).withTrimmedTop (26.0f).withTrimmedBottom (wheel ? 24.0f : 12.0f);
            auto fade = body.removeFromRight (0.4f * body.getWidth());
            body.removeFromRight (16.0f);
            g.setColour (gtype::axis);
            g.fillRect (juce::Rectangle<float> (fade.getX() - 8.5f, body.getY() + 2.0f, 1.0f, body.getHeight() - 4.0f));
            struct Row
            {
                const juce::String& id;
                const char* name;
                float range;   // the parameter's span either way (st)
            };
            const Row rows[] { { lfoCutoff, "CUTOFF", 48.0f }, { lfoPitch1, "PITCH 1", 12.0f }, { lfoPitch2, "PITCH 2", 12.0f } };
            const float rowH = body.getHeight() / 3.0f;
            for (int i = 0; i < 3; ++i)
            {
                auto r = body.withHeight (rowH).translated (0.0f, rowH * static_cast<float> (i));
                const auto name = r.removeFromLeft (52.0f), value = r.removeFromRight (54.0f);
                r.removeFromRight (8.0f);
                const auto track = r.withSizeKeepingCentre (r.getWidth(), 6.0f);
                const float setting = plain (host, rows[i].id), now = amount * setting;
                const bool any = std::abs (setting) > 1.0e-3f;
                const float half = 0.5f * track.getWidth(), cx = track.getCentreX();
                // The track: its centre (no swing) and its halves marked, faintly.
                g.setColour (gtype::track);
                g.fillRoundedRectangle (track, 3.0f);
                g.setColour (gtype::axis);
                for (const float f : { -0.5f, 0.5f })
                    g.fillRect (juce::Rectangle<float> (cx + f * half - 0.5f, track.getY() - 2.0f, 1.0f, track.getHeight() + 4.0f));
                g.setColour (juce::Colour (0x40ffffff));
                g.fillRect (juce::Rectangle<float> (cx - 0.5f, track.getY() - 4.0f, 1.0f, track.getHeight() + 8.0f));
                auto span = [&] (float st) {
                    const float w = half * juce::jlimit (-1.0f, 1.0f, st / rows[i].range);
                    return juce::Rectangle<float> (std::min (cx, cx + w), track.getY(), std::abs (w), track.getHeight());
                };
                if (wheel && std::abs (setting) > std::abs (now))
                {
                    g.setColour (colour.withAlpha (0.5f));
                    g.drawRoundedRectangle (span (setting).reduced (0.5f), 2.5f, 1.0f);
                }
                if (std::abs (now) > 1.0e-4f)
                {
                    g.setColour (colour.withAlpha (0.9f));
                    g.fillRoundedRectangle (span (now), 2.5f);
                }
                gtype::small (g, rows[i].name, name, juce::Justification::centredLeft, any ? 1.0f : 0.5f);
                g.setFont (gtype::meta());
                g.setColour (gtype::metaColour.withMultipliedAlpha (any ? 1.0f : 0.45f));
                g.drawText (any ? (now >= 0.0f ? "+" : "") + juce::String (now, std::abs (now) < 10.0f ? 2 : 1) + " st" : juce::String ("off"), value,
                            juce::Justification::centredRight, false);
            }
            // The fade-in: silent for DELAY, then up over RISE (time to scale).
            const float delay = std::max (0.0f, plain (host, lfoDelay)), rise = std::max (0.0f, plain (host, lfoRise));
            const float span = std::max (0.5f, 1.3f * (delay + rise));
            gtype::small (g, "FADE-IN", fade.withHeight (12.0f), juce::Justification::centredLeft);
            auto plot = fade.withTrimmedTop (20.0f).withTrimmedBottom (18.0f);
            auto xOf = [&] (float t) { return plot.getX() + plot.getWidth() * std::min (1.0f, t / span); };
            g.setColour (gtype::axis);
            g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getBottom(), plot.getWidth(), 1.0f));
            g.setColour (gtype::grid);
            g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getY(), plot.getWidth(), 1.0f));
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
            g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
            auto seconds = [] (float t) { return juce::String (t, t < 1.0f ? 2 : 1) + " s"; };
            const auto times = fade.withTop (fade.getBottom() - 12.0f);
            gtype::small (g, "DELAY " + seconds (delay), times, juce::Justification::centredLeft);
            gtype::small (g, "RISE " + seconds (rise), times, juce::Justification::centredRight);
            gtype::header (g, area, "LFO 1" + middleDot() + "AMOUNT " + juce::String (juce::roundToInt (100.0f * amount)) + " %", wheel ? "WHEEL LINKED" : "WHEEL OFF");
            if (wheel)
            {
                // The legend: the swing now, and the outline the wheel can bring in.
                const float y = area.getBottom() - 15.0f;
                juce::Rectangle<float> swatch (area.getX() + 14.0f, y + 3.0f, 12.0f, 6.0f);
                g.setColour (colour.withAlpha (0.9f));
                g.fillRoundedRectangle (swatch, 2.0f);
                gtype::small (g, "NOW", { swatch.getRight() + 5.0f, y, 40.0f, 12.0f }, juce::Justification::centredLeft);
                swatch.setX (swatch.getRight() + 44.0f);
                g.setColour (colour.withAlpha (0.5f));
                g.drawRoundedRectangle (swatch.reduced (0.5f), 2.0f, 1.0f);
                gtype::small (g, "WITH THE WHEEL UP", { swatch.getRight() + 5.0f, y, 140.0f, 12.0f }, juce::Justification::centredLeft);
            }
        }

        void paintCharacter (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
        {
            // Two curves from the engine's own equations and figures (not measurements): the
            // ladder's drive stage (u = tanh (g x + b) - tanh (b), g = 2^(4 DRIVE), b = 0.6 BIAS)
            // and the chorus' two BBD taps sweeping in opposite phase (JunoChorus::spec).
            using namespace params;
            const bool ladder = choice (host, filterModel) == 0;
            const int chorus = choice (host, chorusMode);
            auto body = area.reduced (14.0f, 0.0f).withTrimmedTop (26.0f).withTrimmedBottom (12.0f);
            auto right = body.removeFromRight (0.46f * body.getWidth());
            body.removeFromRight (16.0f);
            g.setColour (gtype::axis);
            g.fillRect (juce::Rectangle<float> (right.getX() - 8.5f, body.getY() + 2.0f, 1.0f, body.getHeight() - 4.0f));

            // The drive stage.
            gtype::small (g, "DRIVE CURVE", body.withHeight (12.0f), juce::Justification::centredLeft);
            gtype::small (g, "BIAS " + text (driveBias), body.withHeight (12.0f), juce::Justification::centredRight, ladder ? 1.0f : 0.5f);
            const auto plot = body.withTrimmedTop (18.0f).withTrimmedBottom (2.0f);
            g.setColour (gtype::axis);
            g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getCentreY() - 0.5f, plot.getWidth(), 1.0f));
            g.fillRect (juce::Rectangle<float> (plot.getCentreX() - 0.5f, plot.getY(), 1.0f, plot.getHeight()));
            if (ladder)
            {
                const auto curve = filtergraph::driveCurve (0.01 * plain (host, filterDrive), 0.01 * plain (host, driveBias));
                constexpr float range = 1.6f;   // (output either way)
                auto at = [&] (float x, float y) {
                    return juce::Point<float> (plot.getCentreX() + 0.5f * plot.getWidth() * x, plot.getCentreY() - 0.5f * plot.getHeight() * juce::jlimit (-1.0f, 1.0f, y / range));
                };
                juce::Path line, linear;
                for (int i = 0; i <= 120; ++i)
                {
                    const float x = -1.0f + 2.0f * static_cast<float> (i) / 120.0f;
                    const auto p = at (x, static_cast<float> (curve.at (x)));
                    i == 0 ? line.startNewSubPath (p) : line.lineTo (p);
                }
                // Without the saturator (the stage's gain alone), faint, for comparison.
                const float lim = static_cast<float> (std::min (1.0, range / curve.gain));
                linear.startNewSubPath (at (-lim, static_cast<float> (-curve.gain * lim)));
                linear.lineTo (at (lim, static_cast<float> (curve.gain * lim)));
                g.setColour (juce::Colour (0x26ffffff));
                g.strokePath (linear, juce::PathStrokeType (1.0f));
                g.setColour (colour);
                g.strokePath (line, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            else
                gtype::small (g, "the OTA saturates in its own stages", plot, juce::Justification::centred, 0.7f);

            // The chorus: the two taps' delay over time.
            const auto spec = JunoChorus::spec (chorus);
            static const char* modes[] { "OFF", "I", "II", "I+II" };
            const bool hiss = plain (host, chorusNoise) >= 0.5f;
            gtype::small (g, juce::String ("CHORUS ") + modes[std::clamp (chorus, 0, 3)] + (chorus > 0 && hiss ? middleDot() + "HISS" : juce::String()),
                          right.withHeight (12.0f), juce::Justification::centredLeft, chorus > 0 ? 1.0f : 0.5f);
            if (chorus > 0)
                gtype::small (g, juce::String (spec.rateHz, spec.rateHz < 1.0 ? 2 : 1) + " Hz", right.withHeight (12.0f), juce::Justification::centredRight);
            const auto sweep = right.withTrimmedTop (18.0f).withTrimmedBottom (16.0f).withTrimmedLeft (12.0f);
            constexpr double loMs = 1.4, hiMs = 5.6, window = 2.4;   // (seconds shown)
            auto yOf = [&] (double ms) { return sweep.getBottom() - sweep.getHeight() * static_cast<float> ((ms - loMs) / (hiMs - loMs)); };
            g.setColour (gtype::grid);
            g.fillRect (juce::Rectangle<float> (sweep.getX(), yOf (3.5) - 0.5f, sweep.getWidth(), 1.0f));
            if (chorus > 0)
            {
                auto lfo = [&spec] (double t) {
                    const double ph = t * spec.rateHz;
                    return spec.sine ? std::sin (juce::MathConstants<double>::twoPi * ph) : 1.0 - 4.0 * std::abs (ph + 0.25 - std::floor (ph + 0.75));
                };
                for (const int tap : { -1, 1 })
                {
                    juce::Path p;
                    for (int i = 0; i <= 240; ++i)
                    {
                        const double t = window * i / 240.0;
                        const juce::Point<float> pt (sweep.getX() + sweep.getWidth() * static_cast<float> (i) / 240.0f,
                                                     yOf (spec.centreMs + static_cast<double> (tap) * spec.depthMs * lfo (t)));
                        i == 0 ? p.startNewSubPath (pt) : p.lineTo (pt);
                    }
                    g.setColour (colour.withAlpha (tap > 0 ? 0.9f : 0.45f));
                    g.strokePath (p, juce::PathStrokeType (tap > 0 ? 1.4f : 1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                gtype::small (g, "L", { right.getX(), yOf (spec.centreMs + spec.depthMs) - 6.0f, 10.0f, 12.0f }, juce::Justification::centredLeft);
                gtype::small (g, "R", { right.getX(), yOf (spec.centreMs - spec.depthMs) - 6.0f, 10.0f, 12.0f }, juce::Justification::centredLeft);
                gtype::small (g, juce::String (spec.centreMs - spec.depthMs, 2) + juce::String::fromUTF8 ("\xe2\x80\x93") + juce::String (spec.centreMs + spec.depthMs, 2) + " ms",
                              right.withTop (right.getBottom() - 12.0f), juce::Justification::centredLeft);
            }
            gtype::header (g, area, "ANALOG " + text (analogAmount) + middleDot() + "PHASE " + text (phaseMode).toUpperCase(),
                           "DRIVE " + text (filterDrive) + middleDot() + "TRIM " + text (driveTrim));
        }

        void paintPair (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
        {
            // A schematic (not an analysis): the two oscillators, what links them, their values.
            // The schematic on the left, the values on the right (the graph is 160 px high).
            auto inner = area.reduced (14.0f, 0.0f).withTrimmedTop (28.0f).withTrimmedBottom (16.0f);
            auto readouts = inner.removeFromRight (0.46f * inner.getWidth());
            inner.removeFromRight (12.0f);
            const float bw = std::min (64.0f, 0.36f * inner.getWidth()), bh = 24.0f;
            const juce::Rectangle<float> osc1 (inner.getX(), inner.getY() + 2.0f, bw, bh), osc2 (inner.getX(), inner.getBottom() - bh - 2.0f, bw, bh);
            const juce::Rectangle<float> mix (inner.getRight() - bw, inner.getCentreY() - 0.5f * bh, bw, bh);
            auto box = [&g] (juce::Rectangle<float> b, const juce::String& name, juce::Colour c) {
                g.setColour (c.withAlpha (0.14f));
                g.fillRoundedRectangle (b, 4.0f);
                g.setColour (c.withAlpha (0.75f));
                g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);
                g.setFont (gtype::label());
                g.setColour (c.withAlpha (0.95f));
                g.drawText (name, b, juce::Justification::centred, false);
            };
            box (osc1, "OSC 1", colour);
            box (osc2, "OSC 2", colour);
            box (mix, "MIX", colour);
            auto arrow = [&g] (juce::Point<float> a, juce::Point<float> b, juce::Colour c, bool lit, bool dashed) {
                g.setColour (c.withAlpha (lit ? 0.85f : 0.45f));
                juce::Path line;
                line.startNewSubPath (a);
                line.lineTo (b);
                if (dashed)
                {
                    juce::Path d;
                    const float dash[] { 4.0f, 3.0f };
                    juce::PathStrokeType (1.2f).createDashedStroke (d, line, dash, 2);
                    g.fillPath (d);
                }
                else
                    g.strokePath (line, juce::PathStrokeType (1.2f));
                g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (b));
            };
            arrow ({ osc1.getRight(), osc1.getCentreY() }, { mix.getX(), mix.getCentreY() - 6.0f }, colour, true, false);
            arrow ({ osc2.getRight(), osc2.getCentreY() }, { mix.getX(), mix.getCentreY() + 6.0f }, colour, true, false);
            // SYNC: OSC 2 restarts OSC 1 (dashed and quieter while off).
            const bool sync = plain (host, params::sync) >= 0.5f;
            arrow ({ osc2.getCentreX(), osc2.getY() }, { osc1.getCentreX(), osc1.getBottom() }, colour, sync, ! sync);
            gtype::small (g, sync ? "SYNC" : "SYNC OFF", juce::Rectangle<float> (osc1.getCentreX() + 7.0f, osc1.getBottom(), 80.0f, osc2.getY() - osc1.getBottom()),
                          juce::Justification::centredLeft, sync ? 1.0f : 0.75f);
            // The values, one line each.
            const auto pct = [this] (const juce::String& id) { return juce::String (juce::roundToInt (plain (host, id))) + " %"; };
            juce::StringArray to;
            if (plain (host, params::polyModToPitch1) >= 0.5f)
                to.add ("PITCH 1");
            if (plain (host, params::polyModToWidth1) >= 0.5f)
                to.add ("WIDTH 1");
            if (plain (host, params::polyModToCutoff) >= 0.5f)
                to.add ("FILTER");
            struct Line
            {
                juce::String name, value;
            };
            const Line lines[] { { "RING", pct (params::ringMix) },
                                 { "BLEND", pct (params::blend) },
                                 { "POLY MOD", "ENV " + pct (params::polyModEnv) + middleDot() + "OSC 2 " + pct (params::polyModOsc2) },
                                 { juce::String::fromUTF8 ("\xe2\x86\x92"), to.isEmpty() ? juce::String ("no destination") : to.joinIntoString (", ") },
                                 { "NOISE", pct (params::noise) + middleDot() + (choice (host, params::noiseColor) == 1 ? "JUNO" : "WHITE") } };
            const float step = std::min (19.0f, readouts.getHeight() / 5.0f);
            const float top = readouts.getY() + 0.5f * (readouts.getHeight() - 5.0f * step);
            for (int i = 0; i < 5; ++i)
            {
                auto r = juce::Rectangle<float> (readouts.getX(), top + step * static_cast<float> (i), readouts.getWidth(), step);
                gtype::small (g, lines[i].name, r.removeFromLeft (62.0f), juce::Justification::centredLeft);
                g.setFont (gtype::meta());
                g.setColour (gtype::metaColour);
                g.drawText (lines[i].value, r, juce::Justification::centredLeft, true);
            }
            gtype::header (g, area, "OSC 1 + OSC 2", {});
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
            // The synthesis racks wear the family's chassis, quieter (no FX branding): the voice
            // filter's own green; it is never an FX-chain module.
            // V5 (docs/VISUAL_IDENTITY_INDEX.md): the voice filter's own sage enamel and its
            // resonant motif - the same face as the family, never an FX-chain module.
            const auto& identity = synthIdentity (SynthRack::voiceFilter);
            auto f = faceplateFromIdentity (identity);
            setFaceplate (f);
            setAccent (identity.motifInk);
        }

    protected:
        int buildKey() const override
        {
            // FILTER's row follows the model, the high-pass (on or off), the voicing and the
            // envelope link; CHARACTER's the model (BIAS acts on the ladder); the others nothing.
            if (page() == 1 && subPage() == 2)
                return choice (host, params::filterModel);
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
                // which JUNO, whenever either acts; the high-pass - then two sections: the
                // filter (RESO ENV KEYS DRIVE) and its envelope (resting while ENV LINK lets the
                // AMP envelope shape it).
                const bool ota = choice (host, filterModel) == 1, linked = plain (host, envLink) >= 0.5f;
                const bool highPass = choice (host, otaHpf) > 0, jupiter = choice (host, otaCharacter) == 1;
                selectorSegment (filterModel, "TYPE", { { "LADDER", "Ladder", 0.0f }, { "OTA", "OTA", 1.0f } }, 112,
                                 "The low-pass: the original four-pole LADDER, or the OTA family (after the Juno and Jupiter-8)");
                if (ota)
                    selectorSegment (otaSlope, "SLOPE", { { "24 dB", "24 dB", 0.0f }, { "12 dB", "12 dB", 1.0f } }, 96,
                                     "The slope: four poles, or the Jupiter-8's two-pole tap (resonance stays)");
                if (ota || highPass)
                    selectorSegment (otaCharacter, "VOICING", { { "JUNO", "Juno", 0.0f }, { "JUPITER", "Jupiter", 1.0f } }, 120,
                                     ota ? "The OTA's voicing and the high-pass's curve: JUNO smoother, JUPITER brighter and more resonant"
                                         : "The high-pass's curve: the Juno's (as JUNO sets) or the Jupiter-8's");
                if ((ota || highPass) && ! jupiter)
                    selectorList (junoVariant, "JUNO", 64, "Which Juno: its high-pass positions (106: a bass boost) and the OTA's calibration");
                selectorList (otaHpf, "HPF", 64, "The high-pass before the filter: off, or its positions (Juno-106: 0 bass boost, 1 flat, 2 and 3 cut; the others: 0 flat, 1-3 cut)");
                knob (resonance, "RESO", textOf (host, resonance));
                knob (envDepth, "ENV", textOf (host, envDepth));
                knob (keyTrack, "KEYS", textOf (host, keyTrack));
                knob (filterDrive, "DRIVE", textOf (host, filterDrive));
                group ({});   // (the envelope: set apart by the gap and divider; linked, the graph's foot says so)
                for (const auto& [id, caption] : { std::pair { filterAttack, "ATTACK" }, std::pair { filterDecay, "DECAY" }, std::pair { filterSustain, "SUSTAIN" },
                                                   std::pair { filterRelease, "RELEASE" } })
                {
                    auto& k = knob (id, caption, textOf (host, id));
                    k.setEnabled (! linked);
                    k.setAlpha (linked ? 0.45f : 1.0f);
                    if (linked)
                        k.slider.setTooltip ("ENV LINK is on (VOICE, AMP): the AMP envelope shapes the filter, so these rest");
                }
                knob (velocityToFilter, "VELO", textOf (host, velocityToFilter));
            }
            else if (p == 1)
            {
                switch (subPage())
                {
                    case 0:   // AMP: the amplifier, the envelope link, the voices - one section
                        segmentCell (vcaMode, "AMP", { { "ENV", "Envelope", 0.0f }, { "GATE", "Gate", 1.0f } },
                                     "The amplitude: its envelope (AMP), or a gate that opens and closes with the key");
                        switchCell (envLink, "ENV LINK", { on, off }, "One envelope for both, as on a Juno-106: the AMP envelope shapes the filter too");
                        steppedKnob (polyphony, "VOICES", textOf (host, polyphony));
                        switchCell (unison, "UNISON", { on, off }, "Every voice on one note, detuned: a thick mono sound");
                        knob (unisonDetune, "DETUNE", textOf (host, unisonDetune));
                        knob (spread, "SPREAD", textOf (host, spread));
                        break;
                    case 1:   // MOD: LFO 1's depths and fade-in, WHEEL its last control (its shape and rate: the modulation panel)
                        knob (motion, "AMOUNT", textOf (host, motion));
                        knob (lfoCutoff, "CUTOFF", textOf (host, lfoCutoff));
                        knob (lfoPitch1, "PITCH 1", textOf (host, lfoPitch1));
                        knob (lfoPitch2, "PITCH 2", textOf (host, lfoPitch2));
                        knob (lfoDelay, "DELAY", textOf (host, lfoDelay));
                        knob (lfoRise, "RISE", textOf (host, lfoRise));
                        switchCell (lfoWheel, "WHEEL", { on, off },
                                    "The mod wheel brings in the rest of these depths (the classic link). Off: the wheel moves only what you route it to");
                        break;
                    default:   // CHARACTER: the analog character (ANALOG PHASE BIAS TRIM) | the ensemble and its noise (CHORUS HISS)
                    {
                        const bool ladder = choice (host, filterModel) == 0;
                        knob (analogAmount, "ANALOG", textOf (host, analogAmount));
                        segmentCell (phaseMode, "PHASE", { { "FREE", "Free", 0.0f }, { "RESET", "Reset", 1.0f } },
                                     "Oscillators free-running (each note starts anywhere) or restarted at every note");
                        auto& bias = knob (driveBias, "BIAS", textOf (host, driveBias));
                        bias.setAlpha (ladder ? 1.0f : 0.45f);
                        bias.slider.setTooltip (ladder ? "DC before the ladder's drive stage: asymmetric clipping, even harmonics"
                                                       : "BIAS acts on the LADDER's drive stage (the OTA saturates in its own stages)");
                        knob (driveTrim, "TRIM", textOf (host, driveTrim));
                        group ({});
                        segmentCell (chorusMode, "CHORUS", { { "OFF", "Off", 0.0f }, { "I", "I", 1.0f }, { "II", "II", 2.0f }, { "I+II", "I and II", 3.0f } },
                                     "A Juno-style bucket-brigade chorus on the whole sound");
                        switchCell (chorusNoise, "HISS", { on, off }, "The chorus' own faint hiss");
                        break;
                    }
                }
            }
            else
            {
                // The pair (SYNC RING BLEND) | POLY MOD's sources and its three destinations (each
                // its own switch: any of them at once) | the noise, its colour over it.
                selectorSegment (noiseColor, "NOISE COLOR", { { "WHITE", "White", 0.0f }, { "JUNO", "Juno", 1.0f } }, 104, "The noise's colour: white, or the Juno's darker hiss",
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
            const auto status = host.engineStatus ("morph");
            const float p = juce::jlimit (0.0f, 1.0f, 0.01f * plain (host, params::morphPosition));
            const int method = choice (host, params::morphMethod);
            static const char* methods[] { "IMPRINT", "FUSE", "WEAVE" };
            const auto title = juce::String (methods[std::clamp (method, 0, 2)]) + middleDot() + "MORPH " + juce::String (juce::roundToInt (100.0f * p)) + " %";
            if (status.isNotEmpty())
            {
                // The empty state: what to do, and why (the engine's words). No spectrum.
                gtype::header (g, area, title, {}, 0.45f);
                const auto message = area.withSizeKeepingCentre (area.getWidth() - 48.0f, 56.0f);
                g.setFont (fonts::make (12.0f, fonts::Weight::medium, 0.02f));
                g.setColour (gtype::strong.withAlpha (0.9f));
                g.drawText ("Load an eligible sample into C to morph", message.withHeight (18.0f), juce::Justification::centred, false);
                g.setFont (gtype::meta());
                g.setColour (gtype::metaColour.withMultipliedAlpha (0.8f));
                g.drawFittedText (status, message.withTrimmedTop (24.0f).toNearestInt(), juce::Justification::centredTop, 2, 1.0f);
                return;
            }
            const int partials = juce::jlimit (8, 48, juce::roundToInt (8.0f + 40.0f * 0.01f * plain (host, params::morphDetail)));
            const float formants = plain (host, params::morphFormants);   // semitones
            const float texture = 0.01f * plain (host, params::morphTexture);
            const auto plot = area.reduced (16.0f, 0.0f).withTrimmedTop (26.0f).withTrimmedBottom (32.0f);
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
            for (const float db : { -12.0f, -24.0f })
            {
                g.setColour (gtype::grid);
                g.fillRect (juce::Rectangle<float> (plot.getX(), yOf (db) - 0.5f, plot.getWidth(), 1.0f));
            }
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
                g.setColour (cool.withAlpha (0.2f));
                g.fillRect (juce::Rectangle<float> (x - 0.32f * slot, yOf (a), 0.2f * slot, plot.getBottom() - yOf (a)));
                g.setColour (copper.withAlpha (0.2f));
                g.fillRect (juce::Rectangle<float> (x + 0.12f * slot, yOf (b), 0.2f * slot, plot.getBottom() - yOf (b)));
                g.setColour (rc.withAlpha (on ? 0.95f : 0.4f));
                g.fillRect (juce::Rectangle<float> (x - 0.08f * slot, yOf (result), 0.16f * slot + 1.0f, plot.getBottom() - yOf (result)));
            }
            // TEXTURE: the residual's share, as a faint floor.
            g.setColour (copper.withAlpha (0.1f * texture));
            g.fillRect (plot.withTop (plot.getBottom() - 0.18f * plot.getHeight()));
            g.setColour (gtype::axis);
            g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getBottom(), plot.getWidth(), 1.0f));
            // The harmonics' numbers.
            for (const int n : { 1, 8, 16, 24, 32 })
                if (n <= shownPartials)
                    gtype::small (g, juce::String (n), { plot.getX() + slot * (static_cast<float> (n) - 0.5f) - 12.0f, plot.getBottom() + 3.0f, 24.0f, 12.0f },
                                  juce::Justification::centred, 0.8f);
            // The header: the method; the legend (the carrier cool, card C copper).
            gtype::header (g, area, title, {});
            static const char* carriers[] { "OSC 1", "OSC 2", "OSC 1+2" };
            const juce::String carrier = carriers[std::clamp (choice (host, params::morphCarrier), 0, 2)];
            g.setFont (gtype::meta());
            float x = area.getRight() - 9.0f;
            for (const auto& [name, c] : { std::pair { juce::String ("CARD C"), copper }, std::pair { carrier, cool } })
            {
                const float w = juce::GlyphArrangement::getStringWidth (gtype::meta(), name);
                g.setColour (c);
                g.drawText (name, juce::Rectangle<float> (x - w, area.getY() + 4.0f, w, 12.0f), juce::Justification::centredRight, false);
                g.fillRoundedRectangle (juce::Rectangle<float> (x - w - 13.0f, area.getY() + 7.0f, 8.0f, 6.0f), 1.5f);
                x -= w + 26.0f;
            }
            gtype::foot (g, area, on ? "concept view from the settings" + middleDot() + "not a measurement" : "MORPH is off: the sources play as they are");
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
            // Copper and rose (the V2 MORPH direction); a synthesis rack, never an FX module.
            // V5: copper-earth enamel and the woven spectral paths (a synthesis rack).
            const auto& identity = synthIdentity (SynthRack::morph);
            auto f = faceplateFromIdentity (identity);
            setFaceplate (f);
            setAccent (identity.motifInk);
        }

    protected:
        void build (int) override
        {
            // METHOD and CARRIER in the selector row; the five knobs that shape the morph; then,
            // a rank lower (its captions and values quieter, after the divider), the setup:
            // what comes out, the level match, card C's root. (MORPH 2.0's controls will take
            // pages - MORPH / IDENTITY / MOTION - once they exist, never a taller rack.)
            using namespace params;
            selectorSegment (morphMethod, "METHOD", { { "IMPRINT", "Imprint", 0.0f }, { "FUSE", "Fuse", 1.0f }, { "WEAVE", "Weave", 2.0f } }, 246,
                             "IMPRINT: card C's envelope on the oscillators. FUSE: their partials and pitch blended. WEAVE: their partials interleaved");
            selectorSegment (morphCarrier, "CARRIER", { { "OSC 1", "Oscillator 1", 0.0f }, { "OSC 2", "Oscillator 2", 1.0f }, { "BOTH", "Both", 2.0f } }, 216,
                             "The analog side: oscillator 1, oscillator 2, or the pair as levelled");
            knob (morphPosition, "MORPH", textOf (host, morphPosition));
            knob (morphTexture, "TEXTURE", textOf (host, morphTexture));
            knob (morphAttack, "ATTACK", textOf (host, morphAttack))
                .slider.setTooltip ("How much of card C's own attack (its onset transient) the morph keeps - not an envelope time");
            knob (morphFormants, "FORMANTS", textOf (host, morphFormants));
            knob (morphDetail, "DETAIL", textOf (host, morphDetail));
            group ({}, true);
            switchCell (morphOutput, "OUTPUT", { { "MORPH ONLY", "Morph only", 0.0f }, { "LAYERED", "Layered", 1.0f } },
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

bool filtergraph::liveView (juce::Component* graph, LiveView& out, double advanceSeconds)
{
    auto* g = dynamic_cast<FilterGraph*> (graph);
    if (g == nullptr)
        return false;
    if (advanceSeconds >= 0.0)
        g->advanceBy (advanceSeconds);
    const auto& s = g->shownCutoffs();
    out.mode = static_cast<int> (g->mode());
    out.baseNote = s.baseNote;
    out.nowNote = s.nowNote;
    out.reachNote = s.reachNote;
    out.peakNote = s.peakNote;
    out.note = s.note;
    out.velocity = s.velocity;
    out.stage = s.stage;
    return true;
}

} // namespace andor::analog
