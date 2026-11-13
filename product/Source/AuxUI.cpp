#include "AuxUI.h"

#include "AnalogEngine.h"
#include "AnalogParameters.h"
#include "Binding.h"
#include "Design.h"
#include "DropOwner.h"
#include "EngineCard.h"
#include "FxIdentity.h"
#include "OspLookAndFeel.h"
#include "RackControls.h"
#include "ShapingPopups.h"
#include "ShellEditor.h"
#include "ShellProcessor.h"

#include <algorithm>
#include <cmath>

namespace andor::analog
{

using namespace osp::plugin;
using osp::plugin::rack::GraphFrame;
using osp::plugin::rack::ParameterRack;

namespace
{
    AnalogEngine* engineOf (InstrumentHost& host)
    {
        if (auto* shell = dynamic_cast<andor::shell::ShellProcessor*> (&host))
            return dynamic_cast<AnalogEngine*> (&shell->productEngine());
        return nullptr;
    }

    juce::File importFolder (InstrumentHost& host)
    {
        if (auto* shell = dynamic_cast<andor::shell::ShellProcessor*> (&host))
            return shell->sampleStoreFolder().getChildFile ("Imported");
        return {};
    }

    int modeOf (InstrumentHost& host) { return std::clamp (juce::roundToInt (host.parameterValue (params::auxMode)), 0, 3); }
    constexpr int liveMode = 3;   ///< LIVE INPUT (Addendum 05): the host's sidechain input
    bool poweredOf (InstrumentHost& host) { return host.parameterValue (params::auxEnabled) >= 0.5f; }

    const juce::Colour cream { 0xffe9e4da };
    /** " · " (a middle dot between two parts of a readout). */
    juce::String dot() { return juce::String::fromUTF8 (" \xc2\xb7 "); }
    const juce::Colour graphite { 0xff1c2021 };   // (the SUB / RHYTHM / SATELLITE displays')

    MiniKnob::Formatter textOf (InstrumentHost& host, const juce::String& id)
    {
        auto* p = host.apvts().getParameter (id);
        return [p] (double v) { return p != nullptr ? p->getText (p->convertTo0to1 (static_cast<float> (v)), 0) : juce::String (v, 1); };
    }

    void setParameter (InstrumentHost& host, const juce::String& id, float value)
    {
        if (auto* p = host.apvts().getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    }

    andor::shell::ShellEditor* editorOf (juce::Component& c) { return c.findParentComponentOfClass<andor::shell::ShellEditor>(); }

    bool auxRackOpen (juce::Component& c)
    {
        auto* editor = editorOf (c);
        return editor != nullptr && editor->effectsRack().isShown (rack::Kind::aux);
    }

    void setAuxRackOpen (juce::Component& c, bool open)
    {
        if (auto* editor = editorOf (c))
            editor->setRackExpanded (rack::Kind::aux, open);
    }

    bool isAudioFile (const juce::String& path)
    {
        const auto ext = juce::File (path).getFileExtension().toLowerCase();
        return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac";
    }

    /** D's live picture, in a graphite well (the strip's, and the rack's graph): RHYTHM's loop
        (its level over its bars, the playhead where the audio thread reads it, D's envelope),
        NOISE's colour (its slope, TONE's corner, the gate's light) or KONTAKT's state and
        level. The engine's own telemetry, never an animation. */
    void paintAuxPicture (juce::Graphics& g, juce::Rectangle<float> area, InstrumentHost& host, bool compact, float& playheadX)
    {
        playheadX = -1.0f;
        const auto* engine = engineOf (host);
        const int mode = modeOf (host);
        const bool on = poweredOf (host);
        const float alpha = on ? 1.0f : 0.5f;
        const auto ink = aux::onDark();
        const float level = engine != nullptr ? std::clamp (engine->auxPeak(), 0.0f, 1.0f) : 0.0f;

        // The level meter at the right edge (what D plays now, after its LEVEL).
        auto meter = area.removeFromRight (compact ? 5.0f : 8.0f);
        area.removeFromRight (compact ? 8.0f : 12.0f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRoundedRectangle (meter, 2.5f);
        if (level > 0.0f)
        {
            const float h = std::sqrt (level) * meter.getHeight();
            g.setColour (ink.withAlpha (0.9f));
            g.fillRoundedRectangle (meter.withTop (meter.getBottom() - h), 2.5f);
        }

        if (mode == 0)
        {
            const auto loop = engine != nullptr ? engine->auxLoop() : nullptr;
            const auto state = engine != nullptr ? engine->auxRecordingState() : AnalogEngine::AuxRecordingState();
            if (loop == nullptr || loop->analysis.envelope.empty())
            {
                g.setColour (juce::Colours::white.withAlpha (0.1f));
                g.drawLine (area.getX(), area.getCentreY(), area.getRight(), area.getCentreY(), 1.0f);
                g.setColour (cream.withAlpha (0.55f));
                g.setFont (fonts::label (compact ? 10.0f : 12.0f));
                using S = AnalogEngine::AuxRecordingState::Status;
                const auto text = state.status == S::loading ? juce::String ("LOADING ") + state.name.toUpperCase()
                                  : state.status == S::missing || state.status == S::error ? state.message.toUpperCase()
                                                                                          : juce::String ("DROP A LOOP OR CLICK TO LOAD");
                g.drawText (text, area, juce::Justification::centred, true);
                return;
            }
            const auto& a = loop->analysis;
            const double from = loop->rhythmic() ? loop->loopStart / loop->rate : 0.0;
            const double span = loop->rhythmic() ? loop->loopFrames / loop->rate : std::max (1.0e-3, a.durationSeconds);
            auto xOf = [&] (double seconds) { return area.getX() + static_cast<float> ((seconds - from) / span) * area.getWidth(); };
            if (loop->rhythmic())
            {
                const double beat = a.beatSeconds();
                for (int b = 0; b <= a.beats; ++b)
                {
                    const float x = xOf (from + b * beat);
                    const bool bar = b % std::max (1, a.beatsPerBar) == 0;
                    g.setColour (juce::Colours::white.withAlpha ((bar ? 0.18f : 0.06f) * alpha));
                    g.drawLine (x, area.getY(), x, area.getBottom(), 1.0f);
                }
            }
            const float peak = std::max (a.envelopePeak, 1.0e-9f);
            juce::Path shape;
            const int points = std::max (8, static_cast<int> (area.getWidth() / 2.0f));
            shape.startNewSubPath (area.getX(), area.getBottom());
            for (int i = 0; i <= points; ++i)
            {
                const double t = from + span * i / points;
                const auto hop = static_cast<std::size_t> (std::clamp (t / a.hopSeconds, 0.0, static_cast<double> (a.envelope.size() - 1)));
                const float v = std::sqrt (std::clamp (a.envelope[hop] / peak, 0.0f, 1.0f));
                shape.lineTo (area.getX() + area.getWidth() * i / points, area.getBottom() - v * area.getHeight() * 0.92f);
            }
            shape.lineTo (area.getRight(), area.getBottom());
            shape.closeSubPath();
            g.setColour (ink.withAlpha (0.28f * alpha));
            g.fillPath (shape);
            g.setColour (ink.withAlpha (0.75f * alpha));
            g.strokePath (shape, juce::PathStrokeType (1.0f));
            const double head = engine != nullptr ? engine->auxPlayhead() : -1.0;
            if (on && head >= 0.0 && engine != nullptr && engine->auxRunning())
            {
                double t = head;
                if (loop->rhythmic())
                    t = from + std::fmod (std::max (0.0, head - from), span);
                playheadX = xOf (t);
                g.setColour (cream.withAlpha (0.9f));
                g.drawLine (playheadX, area.getY(), playheadX, area.getBottom(), 1.2f);
            }
            // D's envelope (its MOD output) as a thin bar along the foot.
            if (engine != nullptr && on)
            {
                const float env = std::clamp (engine->auxEnvelope(), 0.0f, 1.0f);
                g.setColour (ink.withAlpha (0.85f));
                g.fillRect (area.getX(), area.getBottom() - 2.0f, env * area.getWidth(), 2.0f);
            }
            return;
        }
        if (mode == 1)
        {
            // NOISE: its colour's slope over 20 Hz..20 kHz (white flat, pink -3 dB/oct, brown
            // -6 dB/oct) under TONE's low-pass corner; the gate's light in its top corner.
            const int colourIndex = std::clamp (juce::roundToInt (host.parameterValue (params::auxNoiseColor)), 0, 2);
            const float tone = host.parameterValue (params::auxNoiseTone) / 100.0f;
            const double corner = 200.0 * std::pow (2.0, tone * 6.64);
            const double slope = colourIndex == 0 ? 0.0 : (colourIndex == 1 ? -3.0 : -6.0);
            juce::Path curve;
            const int points = std::max (16, static_cast<int> (area.getWidth() / 3.0f));
            for (int i = 0; i <= points; ++i)
            {
                const double hz = 20.0 * std::pow (1000.0, static_cast<double> (i) / points);
                double db = slope * std::log2 (hz / 1000.0) - 10.0 * std::log10 (1.0 + std::pow (hz / corner, 2.0));
                db = std::clamp (db, -36.0, 18.0);
                const float y = area.getY() + static_cast<float> ((18.0 - db) / 54.0) * area.getHeight();
                const float x = area.getX() + area.getWidth() * i / points;
                if (i == 0)
                    curve.startNewSubPath (x, y);
                else
                    curve.lineTo (x, y);
            }
            auto fill = curve;
            fill.lineTo (area.getRight(), area.getBottom());
            fill.lineTo (area.getX(), area.getBottom());
            fill.closeSubPath();
            g.setColour (ink.withAlpha (0.22f * alpha));
            g.fillPath (fill);
            g.setColour (ink.withAlpha (0.8f * alpha));
            g.strokePath (curve, juce::PathStrokeType (1.2f));
            const bool free = host.parameterValue (params::auxNoiseGate) >= 0.5f;
            const float env = engine != nullptr && on ? std::clamp (engine->auxEnvelope(), 0.0f, 1.0f) : 0.0f;
            const float lit = free ? (on ? 1.0f : 0.0f) : env;
            const auto dot = juce::Rectangle<float> (compact ? 6.0f : 8.0f, compact ? 6.0f : 8.0f).withPosition (area.getRight() - (compact ? 6.0f : 8.0f), area.getY());
            g.setColour (juce::Colours::white.withAlpha (0.1f));
            g.fillEllipse (dot);
            if (lit > 0.01f)
            {
                g.setColour (ink.withAlpha (0.25f + 0.75f * lit));
                g.fillEllipse (dot);
            }
            return;
        }
        if (mode == liveMode)
        {
            // LIVE INPUT: the host's sidechain input - its level over the last moments (one bar a
            // block, mirrored), or why there is none. Telemetry of what arrives, before LEVEL.
            g.setFont (fonts::label (compact ? 10.0f : 12.0f));
            const auto view = engine != nullptr ? engine->liveInput() : AnalogEngine::LiveInputView();
            auto say = [&] (const juce::String& text, const juce::String& hint) {
                g.setColour (cream.withAlpha (0.6f * alpha));
                if (compact || hint.isEmpty())
                    g.drawText (text, area, juce::Justification::centred, true);
                else
                {
                    g.drawText (text, area.withTrimmedBottom (0.5f * area.getHeight()), juce::Justification::centredBottom, true);
                    g.setColour (cream.withAlpha (0.4f * alpha));
                    g.setFont (fonts::label (10.0f));
                    g.drawText (hint, area.withTrimmedTop (0.5f * area.getHeight() + 6.0f), juce::Justification::centredTop, true);
                }
            };
            const juce::String route ("Route an audio track to ANDOR Analog's AUX D sidechain input in your DAW");
            if (engine == nullptr || ! view.busExists)
            {
                say ("LIVE INPUT UNAVAILABLE IN THIS HOST", route);
                return;
            }
            if (! view.busEnabled)
            {
                say ("SIDECHAIN INPUT DISABLED", route);
                return;
            }
            std::array<float, AnalogEngine::liveHistory> history {};
            engine->liveInputHistory (history);
            float recent = 0.0f;
            for (std::size_t i = history.size() - 24; i < history.size(); ++i)
                recent = std::max (recent, history[i]);
            if (recent >= 1.0e-4f)
            {
                g.setColour (juce::Colours::white.withAlpha (0.1f));
                g.drawLine (area.getX(), area.getCentreY(), area.getRight(), area.getCentreY(), 1.0f);
            }
            const float w = area.getWidth() / static_cast<float> (history.size());
            g.setColour (ink.withAlpha (0.75f * alpha));
            for (std::size_t i = 0; i < history.size(); ++i)
            {
                // (a level's square root: quiet material stays visible, a full-scale peak fills it)
                const float h = std::sqrt (std::clamp (history[i], 0.0f, 1.0f)) * 0.5f * area.getHeight();
                if (h > 0.5f)
                    g.fillRect (area.getX() + w * static_cast<float> (i), area.getCentreY() - h, std::max (1.0f, w - 1.0f), 2.0f * h);
            }
            if (recent < 1.0e-4f)
                say ("NO INPUT SIGNAL", {});
            return;
        }
        // KONTAKT: its state and name, and its level over time as a bar.
        g.setFont (fonts::label (compact ? 10.0f : 12.0f));
        juce::String text = "KONTAKT NOT LOADED";   // (once: the readout says nothing more; a click loads it)
        if (engine != nullptr)
        {
            const auto& k = engine->auxInstrument();
            switch (k.state())
            {
                case ExternalInstrument::State::ready: text = k.displayName().toUpperCase() + (k.sounding() ? dot() + "PLAYING" : juce::String()); break;
                case ExternalInstrument::State::missing: text = k.displayName().toUpperCase() + ": NOT FOUND ON THIS COMPUTER"; break;
                case ExternalInstrument::State::failed: text = "KONTAKT WOULD NOT LOAD"; break;
                case ExternalInstrument::State::empty: break;
            }
        }
        const bool loaded = engine != nullptr && engine->auxInstrument().state() == ExternalInstrument::State::ready;
        g.setColour (cream.withAlpha (0.7f * alpha));
        if (! loaded)
        {
            g.drawText (text, area, juce::Justification::centred, true);   // (calm: one line, no level bar)
            return;
        }
        g.drawText (text, area.withTrimmedBottom (area.getHeight() * 0.35f), juce::Justification::centred, true);
        const auto bar = area.withTop (area.getBottom() - 4.0f).reduced (area.getWidth() * 0.1f, 0.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (ink.withAlpha (0.9f));
        g.fillRoundedRectangle (bar.withWidth (std::sqrt (level) * bar.getWidth()), 2.0f);
    }

    //==========================================================================
    /** What D does with files dropped on its strip or rack (only ever D: never card C). One
        WAV / AIFF / FLAC: D RHYTHM's loop (D switches to RHYTHM). Anything else: refused, with
        the reason, nothing changed. */
    juce::String loadAuxFiles (InstrumentHost& host, const juce::StringArray& files)
    {
        if (modeOf (host) == liveMode)
            return "LIVE INPUT PLAYS YOUR DAW'S ROUTED AUDIO - NOTHING TO LOAD";   // (no recording, no asset: RHYTHM's loop untouched)
        if (files.size() != 1)
            return "ONE RECORDING AT A TIME";
        if (! isAudioFile (files[0]))
            return "NOT A WAV, AIFF OR FLAC RECORDING";
        auto* engine = engineOf (host);
        if (engine == nullptr)
            return {};
        setParameter (host, params::auxMode, 0.0f);   // (a recording is D RHYTHM's)
        engine->loadAuxRecording (juce::File (files[0]), importFolder (host));
        return {};
    }

    /** A click on D's well (the strip's or the rack's): RHYTHM browses for a loop, KONTAKT loads
        it (or opens its window); NOISE has nothing to load. */
    void clickAuxSurface (InstrumentHost& host, juce::Component& near)
    {
        const int mode = modeOf (host);
        if (mode == liveMode)
            setAuxRackOpen (near, true);   // (no file to choose: the rack shows its input and how to route it)
        else if (mode == 0)
            aux::chooseRecording (host, near);
        else if (mode == 2)
            if (auto* engine = engineOf (host))
            {
                auto& k = engine->auxInstrument();
                if (k.state() == ExternalInstrument::State::ready)
                    k.openEditor();
                else
                    k.loadKontakt();
            }
    }

    /** The drop target and a refusal's reason, over a well (strip or rack). */
    void paintDropState (juce::Graphics& g, juce::Rectangle<float> well, bool over, const juce::String& notice, float radius)
    {
        if (over)
        {
            g.setColour (aux::onDark().withAlpha (0.14f));
            g.fillRoundedRectangle (well, radius);
            g.setColour (aux::onDark().withAlpha (0.9f));
            g.drawRoundedRectangle (well.reduced (1.0f), radius, 1.5f);
        }
        if (notice.isNotEmpty())
        {
            auto band = well.reduced (10.0f, 4.0f).removeFromBottom (14.0f);
            g.setColour (juce::Colour (0xffe9b4a4));
            g.setFont (fonts::label (10.0f));
            g.drawText (notice, band, juce::Justification::centredLeft, true);
        }
    }

    /** A plain text key on the ceramic (the rack's LOAD KONTAKT / OPEN / UNLOAD). */
    class TextKey final : public juce::Button
    {
    public:
        explicit TextKey (const juce::String& text) : juce::Button (text)
        {
            setWantsKeyboardFocus (true);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setTitle (text);
        }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            using namespace design;
            const auto r = getLocalBounds().toFloat().reduced (1.0f);
            draw::button (g, r, 6.0f, down, over, aux::colour());
            g.setColour (colour::textSecondary);
            g.setFont (fonts::make (11.0f, fonts::Weight::medium, 0.08f));
            g.drawText (getButtonText(), r, juce::Justification::centred, false);
            if (hasKeyboardFocus (false))
            {
                g.setColour (colour::text.withAlpha (0.35f));
                g.drawRoundedRectangle (r.expanded (1.0f), 7.0f, 1.0f);
            }
        }
    };

    /** RACK: the cards' utility key (EQ / NOTES: the same ceramic face, size and type), pressed
        in while the AUX rack is open. */
    class RackKey final : public juce::Button, public skin::KeyFace
    {
    public:
        RackKey() : juce::Button ("RACK")
        {
            setWantsKeyboardFocus (true);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setTitle ("D AUX rack");
            setTooltip ("D's rack: every setting of its mode (Escape or its X closes it)");
        }
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            using namespace design;
            const auto r = getLocalBounds().toFloat().reduced (1.0f);
            const bool open = getToggleState();
            if (! skin::drawnBySurface())
                draw::button (g, r, 7.0f, open || down, over, colour::accent);
            g.setFont (type::button (12.5f));
            g.setColour (colour::text.withAlpha (open ? 0.92f : 0.62f));
            g.drawText ("RACK", r, juce::Justification::centred, false);
            if (hasKeyboardFocus (false))
            {
                g.setColour (colour::text.withAlpha (0.35f));
                g.drawRoundedRectangle (r.expanded (1.0f), 8.0f, 1.0f);
            }
        }
        void buttonStateChanged() override { skin::repaintKey (*this); }
        skin::KeyRole keyRole() const override { return skin::KeyRole::eq; }
        skin::KeyState keyState() const override
        {
            return getToggleState() || isDown() ? skin::KeyState::pressed : isOver() ? skin::KeyState::hover : skin::KeyState::normal;
        }
        juce::Rectangle<float> keyArea (juce::Rectangle<float> bounds) const override { return bounds.reduced (1.0f); }
    };

    /** The cards' menu dots (three, vertical: the quietest control). */
    class MenuDots final : public juce::Button
    {
    public:
        MenuDots() : juce::Button ("D AUX menu")
        {
            setWantsKeyboardFocus (true);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setTitle ("D AUX menu");
        }
        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            const auto r = getLocalBounds().toFloat();
            if (over)
            {
                g.setColour (design::colour::text.withAlpha (0.06f));
                g.fillRoundedRectangle (r, 6.0f);
            }
            const float d = 4.2f * r.getHeight() / 36.0f, gap = 8.5f * r.getHeight() / 36.0f;
            g.setColour (design::colour::text.withAlpha (over ? 0.9f : 0.72f));
            for (int i = -1; i <= 1; ++i)
                g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (r.getCentre().translated (0.0f, static_cast<float> (i) * gap)));
        }
    };

    //==========================================================================
    /** D's dark well in the strip (B's width and place): the loading surface - a click loads (by
        mode), the readout at its top, the live picture under it. */
    class AuxDisplay final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit AuxDisplay (InstrumentHost& h) : host (h)
        {
            setTitle ("D AUX display");
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
        std::function<juce::String()> notice;   ///< (a refused drop, a failed load)
        bool dropOver = false;
        std::function<juce::String()> unheard;   ///< why D is not heard (MUTED, NOT SOLOED), or empty
        void paint (juce::Graphics& g) override
        {
            const auto b = getLocalBounds().toFloat();
            g.setColour (graphite);
            g.fillRoundedRectangle (b, 9.0f);
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.drawRoundedRectangle (b.reduced (0.5f), 9.0f, 1.0f);
            auto inner = b.reduced (12.0f, 6.0f);
            auto top = inner.removeFromTop (22.0f);
            // (Addendum 04: the mode is chosen outside the well; its top row holds what D plays,
            // discreetly at the right, and - muted or another source soloed - why it is not
            // heard, at the left. OFF is never drawn here: the strip's AUX ON / OFF says it.)
            if (const auto why = unheard != nullptr ? unheard() : juce::String(); why.isNotEmpty())
            {
                const auto font = fonts::make (10.0f, fonts::Weight::medium, 0.12f);
                const float width = juce::GlyphArrangement::getStringWidth (font, why) + 4.0f;
                g.setColour (juce::Colour (0xffd9dcd8).withAlpha (0.6f));
                g.setFont (font);
                g.drawText (why, top.withWidth (width), juce::Justification::centredLeft, false);
                top.removeFromLeft (width + 14.0f);
            }
            g.setColour (cream.withAlpha (0.6f));
            g.setFont (fonts::label (10.0f));
            if (top.getWidth() > 8.0f)
                g.drawText (aux::readout (host), top, juce::Justification::centredRight, true);
            inner.removeFromTop (3.0f);
            paintAuxPicture (g, inner, host, true, shownPlayheadX);
            paintDropState (g, b, dropOver, notice != nullptr ? notice() : juce::String(), 9.0f);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mouseWasClicked() && e.getNumberOfClicks() == 1 && getLocalBounds().contains (e.getPosition()))
                clickAuxSurface (host, *this);
        }
        float playheadX() const noexcept { return shownPlayheadX; }

    private:
        InstrumentHost& host;
        float shownPlayheadX = -1.0f;
    };

    //==========================================================================
    class AuxStrip final : public juce::Component, public juce::SettableTooltipClient, public osp::plugin::DropOwner, private juce::Timer
    {
    public:
        explicit AuxStrip (InstrumentHost& h) : host (h)
        {
            setTitle ("D AUX");
            setDescription ("D / AUX: a fourth source (RHYTHM, NOISE or KONTAKT) through the FILTER and the effects");
            setTooltip ("D / AUX: double-click for its rack");
            power = std::make_unique<rackui::PowerKey> (*h.apvts().getParameter (params::auxEnabled), &h.undo(), aux::colour());
            power->setTooltip ("D's power: off, D plays nothing at all (its bus is idle)");
            power->setTitle ("D power");
            binding::tag (*power, params::auxEnabled);
            addAndMakeVisible (*power);
            // M / S: the cards' own keys (sizes, faces, states), bound to D's parameters.
            mute = std::make_unique<LayerStateButton> (h, params::auxMute, false, aux::colour(), "D");
            addAndMakeVisible (*mute);
            solo = std::make_unique<LayerStateButton> (h, params::auxSolo, true, aux::onDark(), "D");
            addAndMakeVisible (*solo);

            display = std::make_unique<AuxDisplay> (h);
            display->notice = [this] { return currentNotice(); };
            display->unheard = [this] { const auto why = unheardReason(); return why == "OFF" ? juce::String() : why; };
            addAndMakeVisible (*display);
            // SOURCE MODE (Addendum 04): a compact ceramic dropdown on the strip itself, right of
            // the power key - the rack's own list style - never inside the well.
            modeSelector = std::make_unique<ValueSelector> (*h.apvts().getParameter (params::auxMode), juce::String());
            modeSelector->setRackStyle (ValueSelector::RackStyle::option);
            modeSelector->setTooltip ("What D plays: a RHYTHM loop (host-synced), NOISE (white, pink, brown), or KONTAKT (its own instance, your keys)");
            modeSelector->setTitle ("D mode");
            binding::tag (*modeSelector, params::auxMode);
            addAndMakeVisible (*modeSelector);
            // LEVEL: permanent, right of the mode, the same place in every mode.
            level = std::make_unique<MiniKnob> (h.apvts(), params::auxLevel, "LEVEL", textOf (h, params::auxLevel));
            level->slider.setTooltip ("D's level (before the FILTER and the effects)");
            level->slider.getProperties().set ("arc", static_cast<juce::int64> (aux::colour().getARGB()));
            binding::tag (level->slider, params::auxLevel);
            addAndMakeVisible (*level);

            rackKey = std::make_unique<RackKey>();
            rackKey->onClick = [this] { setAuxRackOpen (*this, ! auxRackOpen (*this)); };
            addAndMakeVisible (*rackKey);
            menu = std::make_unique<MenuDots>();
            menu->setTooltip ("D's menu: hide the strip, its rack, reset");
            menu->onClick = [this] { showMenu(); };
            addAndMakeVisible (*menu);
            buildKnobs();
            startTimerHz (30);
        }

        int shownMode() const noexcept { return builtMode; }
        juce::StringArray knobCaptions() const
        {
            juce::StringArray out;
            for (const auto& k : knobs)
                out.add (k->captionText());
            return out;
        }
        juce::Component* part (const juce::String& name) const
        {
            if (name == "power") return power.get();
            if (name == "mute") return mute.get();
            if (name == "solo") return solo.get();
            if (name == "mode") return modeSelector.get();
            if (name == "display") return display.get();
            if (name == "rack") return rackKey.get();
            if (name == "menu") return menu.get();
            if (name == "LEVEL") return level.get();
            for (const auto& k : knobs)
                if (k->captionText() == name)
                    return k.get();
            return nullptr;
        }

        juce::PopupMenu createMenu()
        {
            juce::PopupMenu m;
            juce::Component::SafePointer<AuxStrip> safe (this);
            m.addItem ("Hide AUX", [safe] {
                if (safe != nullptr)
                    if (auto* editor = editorOf (*safe))
                        editor->setAuxStripShown (false);   // (a view change: D plays on)
            });
            const bool open = auxRackOpen (*this);
            m.addItem (open ? "Close AUX Rack" : "Open AUX Rack", [safe, open] {
                if (safe != nullptr)
                    setAuxRackOpen (*safe, ! open);
            });
            m.addSeparator();
            m.addItem ("Reset", [safe] {
                if (safe != nullptr)
                    aux::reset (safe->host);
            });
            return m;
        }

        void runMenuItem (const juce::String& item)
        {
            auto m = createMenu();
            for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
                if (it.getItem().text == item && it.getItem().action != nullptr)
                {
                    it.getItem().action();
                    return;
                }
        }

        /** The three zones, aligned to the cards above (Addendum 04: one fixed layout for every
            mode). A's: D, M / S, AUX and its state, power, the SOURCE MODE dropdown and LEVEL -
            never moving. The display: from just after LEVEL to B's dark well's right edge. C's:
            the mode's own knobs only, then RACK and the menu anchored at the right. */
        void resized() override
        {
            const int h = getHeight();
            juce::Rectangle<int> wellA, wellB, wellC, cardA, cardC;
            if (auto* editor = editorOf (*this))
            {
                const auto origin = getPosition();
                wellA = editor->cardDisplayBounds (0) - origin;
                wellB = editor->cardDisplayBounds (2) - origin;   // (B: OSC 2, the shell's layer C)
                wellC = editor->cardDisplayBounds (1) - origin;
                cardA = editor->engineCard (0).getBounds() - origin;
                cardC = editor->engineCard (1).getBounds() - origin;
            }
            if (wellB.isEmpty())
            {
                // (no cards to align to: thirds of the strip)
                const int third = getWidth() / 3;
                wellA = { 14, 0, third - 28, h };
                wellB = { third + 14, 0, third - 28, h };
                wellC = { 2 * third + 14, 0, third - 28, h };
                cardA = { 0, 0, third, h };
                cardC = { 2 * third, 0, third, h };
            }
            // A's zone: the badge where the cards' badges are, M / S beside it, AUX, power, then
            // SOURCE MODE (as wide as KONTAKT, whatever is chosen) and LEVEL.
            const int top = (h - 43) / 2;
            badge = { cardA.getX() + 21, top, 47, 43 };
            mute->setBounds (badge.getRight() + 9, top, 27, 21);
            solo->setBounds (badge.getRight() + 9, top + 22, 27, 21);
            nameArea = { mute->getRight() + 14, top, 46, 43 };
            power->setBounds (juce::Rectangle<int> (30, 30).withCentre ({ nameArea.getRight() + 15, h / 2 }));
            const int levelWidth = 58;
            const int modeLeft = power->getRight() + 12;
            const int modeWidth = std::max (60, std::min (104, wellB.getX() - 14 - levelWidth - 6 - modeLeft));
            modeSelector->setBounds (modeLeft, h / 2 - 13, modeWidth, 27);
            level->setBounds (modeSelector->getRight() + 6, 4, levelWidth, h - 8);
            // The well: immediately after LEVEL (no empty gap on the left), its right edge on B's
            // dark well's right edge (aligned to the cards above); the same in every mode.
            const int wellLeft = std::min (level->getRight() + 14, wellB.getX());
            display->setBounds (wellLeft, 8, wellB.getRight() - wellLeft, h - 16);
            // C's zone: the menu at the cards' menu's place, RACK beside it, the mode's knobs
            // before them from C's well's left - RHYTHM's four fill it (MOD AMOUNT, a long
            // caption, a double cell); NOISE's two keep the same cells, from the left.
            menu->setBounds (cardC.getRight() - 51, h / 2 - 18, 30, 36);
            rackKey->setBounds (menu->getX() - 8 - 60, h / 2 - 16, 60, 33);
            const int knobLeft = wellC.getX(), knobRight = rackKey->getX() - 10;
            auto weight = [] (const juce::String& caption) { return caption.length() > 6 ? 2.0 : 1.0; };
            const double unit = (knobRight - knobLeft) / 5.0;   // (RATE PITCH OCT MOD-AMOUNT: 1 + 1 + 1 + 2)
            double x = knobLeft;
            for (auto& k : knobs)
            {
                const double w = unit * weight (k->captionText());
                k->setBounds (juce::roundToInt (x), 4, juce::roundToInt (w), h - 8);
                x += w;
            }
        }

        void paint (juce::Graphics& g) override
        {
            using namespace design;
            const auto bounds = getLocalBounds().toFloat();
            draw::raised (g, bounds, layout::cardRadius, colour::cardTop, colour::cardBottom);
            skin::drawKeys (g, *this);   // (M / S and RACK: the ceramic keys, as on the cards)
            {
                const auto b = badge.toFloat();
                juce::Path shape;
                shape.addRoundedRectangle (b, 6.0f);
                design::CachedShadow (juce::Colour (0x40302418), 4, { 0, 2 }).drawForPath (g, shape);
                g.setGradientFill (juce::ColourGradient (aux::badgeTop(), 0.0f, b.getY(), aux::badgeBottom(), 0.0f, b.getBottom(), false));
                g.fillPath (shape);
                g.setColour (juce::Colours::white.withAlpha (0.35f));
                g.strokePath (shape, juce::PathStrokeType (1.0f), juce::AffineTransform::translation (0.0f, 0.5f));
                g.setColour (juce::Colour (0xfffff6ec));
                g.setFont (fonts::make (29.0f, fonts::Weight::regular));
                g.drawText ("D", b.translated (0.0f, 0.5f), juce::Justification::centred, false);
            }
            {
                auto n = nameArea.toFloat();
                g.setColour (colour::text.withAlpha (0.92f));
                g.setFont (fonts::make (15.0f, fonts::Weight::medium, 0.06f));
                g.drawText ("AUX", n.removeFromTop (24.0f), juce::Justification::bottomLeft, false);
                g.setColour (colour::textSecondary);
                g.setFont (fonts::label (10.0f));
                g.drawText (poweredOf (host) ? juce::String ("ON") : juce::String ("OFF"), n, juce::Justification::topLeft, false);
            }
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            // Not heard (off, muted, or another source soloed): a veil over the well's picture, as a
            // card's display. The well draws the reason in its top row (AuxDisplay::unheard), so it
            // never lands on the picture's own text ("DROP A LOOP OR CLICK TO LOAD").
            const auto why = unheardReason();
            if (why.isEmpty())
                return;
            const auto w = display->getBounds().toFloat();
            g.setColour (juce::Colour (0xff15191a).withAlpha (0.45f));
            g.fillRoundedRectangle (w.withTrimmedTop (30.0f), 9.0f);
        }

        void mouseDoubleClick (const juce::MouseEvent&) override { setAuxRackOpen (*this, ! auxRackOpen (*this)); }

        // osp::plugin::DropOwner: a file dropped anywhere on the strip is D's (never card C's).
        void dropHover (const juce::StringArray&, bool over) override
        {
            display->dropOver = over;
            display->repaint();
        }
        void dropFiles (const juce::StringArray& files, juce::Point<int>) override
        {
            display->dropOver = false;
            showNotice (loadAuxFiles (host, files));
        }

        void refreshNow() { timerCallback(); }

    private:
        juce::String unheardReason() const
        {
            if (! poweredOf (host))
                return "OFF";
            if (host.parameterValue (params::auxMute) >= 0.5f && host.parameterValue (params::auxSolo) < 0.5f)
                return "MUTED";
            const bool anySolo = host.parameterValue (params::auxSolo) >= 0.5f || host.parameterValue ("layerA.solo") >= 0.5f
                                 || host.parameterValue ("layerB.solo") >= 0.5f || host.parameterValue ("layerC.solo") >= 0.5f;
            if (anySolo && host.parameterValue (params::auxSolo) < 0.5f)
                return "NOT SOLOED";
            return {};
        }

        juce::String currentNotice() const
        {
            if (noticeTicks > 0)
                return notice;
            if (auto* engine = engineOf (host))
                return engine->auxRecordingState().lastError.toUpperCase();
            return {};
        }

        void showNotice (const juce::String& text)
        {
            notice = text;
            noticeTicks = text.isEmpty() ? 0 : 120;   // (4 s)
            display->repaint();
        }

        void buildKnobs()
        {
            knobs.clear();
            builtMode = modeOf (host);
            auto add = [this] (const juce::String& id, const char* caption, const char* tip) {
                auto k = std::make_unique<MiniKnob> (host.apvts(), id, caption, textOf (host, id));
                k->slider.setTooltip (tip);
                k->slider.getProperties().set ("arc", static_cast<juce::int64> (aux::colour().getARGB()));
                binding::tag (k->slider, id);
                addAndMakeVisible (*k);
                knobs.push_back (std::move (k));
            };
            if (builtMode == 0)
            {
                add (params::auxRate, "RATE", "The loop's speed against the host's tempo (1x: as detected), without changing its pitch");
                add (params::auxPitch, "PITCH", "D RHYTHM's pitch (semitones): the loop on the same grid, as long - only its pitch moves");
                add (params::auxOctave, "OCT", "D RHYTHM's octave: the loop on the same grid, as long - only its pitch moves");
                add (params::auxModAmount, "MOD AMOUNT", "How far D's rhythm envelope moves its target (the rack's MOD TARGET; 0 by default)");
            }
            else if (builtMode == 1)
            {
                add (params::auxNoiseColor, "COLOR", "WHITE, PINK or BROWN");
                add (params::auxNoiseTone, "TONE", "Its own low-pass, before the FILTER");
            }
            resized();
        }

        void showMenu() { createMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menu.get())); }

        void timerCallback() override
        {
            if (modeOf (host) != builtMode)
                buildKnobs();   // (only the mode's knobs: every other part stays where it is)
            display->setTooltip (builtMode == 0 ? "Click to load a loop (WAV, AIFF or FLAC), or drop one here: it plays on the host's grid"
                                 : builtMode == 1 ? "NOISE: its settings are in the rack"
                                 : builtMode == liveMode ? "LIVE INPUT: the audio your DAW routes to ANDOR Analog's AUX D sidechain input (choose the source track in the DAW's sidechain / input routing). Click for the rack"
                                                  : "Click to load Kontakt for D (its own instance; your keys, before the arpeggiator), or to open its window");
            const bool open = auxRackOpen (*this);
            if (rackKey->getToggleState() != open)
                rackKey->setToggleState (open, juce::dontSendNotification);
            if (noticeTicks > 0 && --noticeTicks == 0)
                display->repaint();
            display->repaint();
            const auto why = unheardReason();
            if (why != shownReason)
            {
                shownReason = why;
                repaint();
            }
        }

        InstrumentHost& host;
        std::unique_ptr<rackui::PowerKey> power;
        std::unique_ptr<LayerStateButton> mute, solo;
        std::unique_ptr<ValueSelector> modeSelector;
        std::unique_ptr<MiniKnob> level;   ///< (permanent: not one of the mode's knobs)
        std::unique_ptr<AuxDisplay> display;
        std::unique_ptr<RackKey> rackKey;
        std::unique_ptr<MenuDots> menu;
        std::vector<std::unique_ptr<MiniKnob>> knobs;
        juce::Rectangle<int> badge, nameArea;
        juce::String notice, shownReason = "-";
        int noticeTicks = 0;
        int builtMode = -1;
        friend struct andor::analog::AuxStripView;
    };

    //==========================================================================
    /** LIVE INPUT's meter in the rack: the left and right peaks of what arrives (before LEVEL),
        and its state - DISABLED, NO SIGNAL, MONO / STEREO. */
    class LiveInputMeter final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
    {
    public:
        explicit LiveInputMeter (InstrumentHost& h) : host (h)
        {
            setTitle ("D live input level");
            setTooltip ("Route an audio track to ANDOR Analog's AUX D sidechain input in your DAW (the track's sidechain or input routing). "
                        "D plays it live, through the FILTER and the effects; no MIDI needed");
            startTimerHz (30);
        }
        void paint (juce::Graphics& g) override
        {
            using namespace design;
            const auto* engine = engineOf (host);
            const auto v = engine != nullptr ? engine->liveInput() : AnalogEngine::LiveInputView();
            auto r = getLocalBounds().toFloat().reduced (2.0f, 3.0f);
            auto bars = r.removeFromLeft (120.0f);
            const float h = 0.5f * (bars.getHeight() - 3.0f);
            // (on the rack's plum faceplate: light inks)
            auto bar = [&] (juce::Rectangle<float> b, float peak) {
                g.setColour (juce::Colours::white.withAlpha (0.14f));
                g.fillRoundedRectangle (b, 2.0f);
                // (-60 .. 0 dB across the bar)
                const float db = peak > 1.0e-6f ? juce::Decibels::gainToDecibels (peak) : -100.0f;
                const float fill = std::clamp ((db + 60.0f) / 60.0f, 0.0f, 1.0f);
                if (fill > 0.0f)
                {
                    g.setColour ((db > -1.0f ? juce::Colour (0xffe07a62) : aux::onDark()).withAlpha (0.95f));
                    g.fillRoundedRectangle (b.withWidth (fill * b.getWidth()), 2.0f);
                }
            };
            bar (bars.removeFromTop (h), v.peakLeft);
            bars.removeFromTop (3.0f);
            bar (bars.removeFromTop (h), v.channels > 1 ? v.peakRight : v.peakLeft);
            r.removeFromLeft (10.0f);
            g.setColour (cream.withAlpha (0.8f));
            g.setFont (fonts::label (10.0f));
            const juce::String state = ! v.busExists    ? "UNAVAILABLE IN THIS HOST"
                                       : ! v.busEnabled ? "SIDECHAIN DISABLED"
                                       : std::max (v.peakLeft, v.peakRight) < 1.0e-4f ? "NO INPUT SIGNAL"
                                       : juce::String (v.channels > 1 ? "STEREO" : "MONO") + dot() + juce::String (juce::Decibels::gainToDecibels (std::max (v.peakLeft, v.peakRight)), 1) + " dB";
            g.drawText (state, r, juce::Justification::centredLeft, true);
        }

    private:
        void timerCallback() override { repaint(); }
        InstrumentHost& host;
    };

    //==========================================================================
    /** The rack's graph: D's picture, and its loading surface too (a click loads by mode). */
    class AuxGraph final : public GraphFrame, public juce::SettableTooltipClient
    {
    public:
        explicit AuxGraph (InstrumentHost& h) : host (h)
        {
            setInterceptsMouseClicks (true, false);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setTooltip ("Click to load (a loop in RHYTHM, Kontakt in KONTAKT), or drop a loop here");
        }
        void refresh() override { repaint(); }
        bool dropOver = false;
        juce::String notice;
        int noticeTicks = 0;
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mouseWasClicked() && getLocalBounds().contains (e.getPosition()))
                clickAuxSurface (host, *this);
        }

    protected:
        void paintGraph (juce::Graphics& g, juce::Rectangle<float> area) override
        {
            auto inner = area.reduced (14.0f, 12.0f);
            auto top = inner.removeFromTop (16.0f);
            g.setFont (fonts::label (11.0f));
            g.setColour (cream.withAlpha (0.82f));
            g.drawText (aux::readout (host), top.withTrimmedRight (80.0f), juce::Justification::centredLeft, true);
            g.setColour (cream.withAlpha (0.45f));
            g.drawText ("D " + aux::modeName (modeOf (host)), top, juce::Justification::centredRight, false);
            inner.removeFromTop (8.0f);
            float x = -1.0f;
            paintAuxPicture (g, inner, host, false, x);
            juce::String shown = noticeTicks > 0 ? notice : juce::String();
            if (shown.isEmpty())
                if (auto* engine = engineOf (host))
                    shown = engine->auxRecordingState().lastError.toUpperCase();
            paintDropState (g, area, dropOver, shown, 6.0f);
        }

    private:
        InstrumentHost& host;
    };

    class AuxRack final : public ParameterRack, public osp::plugin::DropOwner
    {
    public:
        // osp::plugin::DropOwner: a file dropped anywhere on the AUX rack is D's (never card C's).
        void dropHover (const juce::StringArray&, bool over) override
        {
            if (auto* g = dynamic_cast<AuxGraph*> (graph()))
            {
                g->dropOver = over;
                g->repaint();
            }
        }
        void dropFiles (const juce::StringArray& files, juce::Point<int>) override
        {
            const auto why = loadAuxFiles (host, files);
            if (auto* g = dynamic_cast<AuxGraph*> (graph()))
            {
                g->dropOver = false;
                g->notice = why;
                g->noticeTicks = why.isEmpty() ? 0 : 120;
                g->repaint();
            }
        }
        void refresh() override
        {
            ParameterRack::refresh();
            if (auto* g = dynamic_cast<AuxGraph*> (graph()); g != nullptr && g->noticeTicks > 0)
                --g->noticeTicks;
        }

        explicit AuxRack (InstrumentHost& h) : ParameterRack (h, "AUX", aux::colour(), 0.72f)
        {
            setHeaderSwitch (params::auxEnabled, "ON", "D / AUX: a fourth source through the FILTER and the shared effects (off: idle, nothing at all)");
            setGraph (std::make_unique<AuxGraph> (h));
            // ANALOG's racks' hardware: a plum enamel faceplate (a source rack, never an FX module).
            const auto& identity = synthIdentity (SynthRack::aux);
            setFaceplate (faceplateFromIdentity (identity));
            setAccent (identity.motifInk);
        }

    protected:
        int buildKey() const override { return modeOf (host); }

        void build (int) override
        {
            using namespace params;
            selectorSegment (auxMode, "MODE", { { "RHYTHM", "Rhythm", 0.0f }, { "NOISE", "Noise", 1.0f }, { "KONTAKT", "Kontakt", 2.0f }, { "LIVE", "Live input", 3.0f } }, 320,
                             "What D plays: a host-synced loop, NOISE, or its own Kontakt");
            const int mode = modeOf (host);
            if (mode == 0)
            {
                selectorSegment (auxRhythmGate, "GATE", { { "KEYS", "Keys", 0.0f }, { "FREE", "Free", 1.0f } }, 140,
                                 "KEYS: a key starts the loop from its first beat; it plays while you hold a key, then fades with the AMP's release. FREE: always playing, on the host's grid");
                selectorSegment (auxSync, "CLOCK", { { "SYNC", "Sync", 0.0f }, { "FREE", "Free", 1.0f } }, 140,
                                 "SYNC: the host's tempo (with GATE FREE, its grid too). FREE: the loop's own tempo");
                steppedKnob (auxRate, "RATE", textOf (host, auxRate));
                knob (auxPitch, "PITCH", textOf (host, auxPitch));
                steppedKnob (auxOctave, "OCT", textOf (host, auxOctave));
                knob (auxLevel, "LEVEL", textOf (host, auxLevel));
                group ("MOD");
                listCell (auxModTarget, "TARGET", "What D's rhythm envelope moves: the cards' FILTER, D's own filter, or D's level");
                knob (auxModAmount, "AMOUNT", textOf (host, auxModAmount));
                knob (auxSensitivity, "SENS", textOf (host, auxSensitivity));
                knob (auxAttack, "ATTACK", textOf (host, auxAttack));
                knob (auxRelease, "RELEASE", textOf (host, auxRelease));
            }
            else if (mode == 1)
            {
                selectorSegment (auxNoiseColor, "COLOR", { { "WHITE", "White", 0.0f }, { "PINK", "Pink", 1.0f }, { "BROWN", "Brown", 2.0f } }, 210,
                                 "WHITE, PINK (-3 dB per octave) or BROWN (-6 dB per octave)");
                selectorSegment (auxNoiseGate, "GATE", { { "KEYS", "Keys", 0.0f }, { "FREE", "Free", 1.0f } }, 140,
                                 "KEYS: your keys open it (legato: one gate). FREE: always on while D is on");
                knob (auxNoiseTone, "TONE", textOf (host, auxNoiseTone));
                knob (auxLevel, "LEVEL", textOf (host, auxLevel));
                group ("SHAPE");
                knob (auxNoiseAttack, "ATTACK", textOf (host, auxNoiseAttack));
                knob (auxNoiseRelease, "RELEASE", textOf (host, auxNoiseRelease));
                knob (auxNoiseMovement, "MOVEMENT", textOf (host, auxNoiseMovement));
                knob (auxNoiseWidth, "WIDTH", textOf (host, auxNoiseWidth));
            }
            else if (mode == liveMode)
            {
                // LIVE INPUT: what arrives (its meters and state) and how to route it; LEVEL. No
                // timing, pitch or file controls: it is the DAW's audio, live.
                selectorComponent (std::make_unique<LiveInputMeter> (host), "INPUT", 300);
                knob (auxLevel, "LEVEL", textOf (host, auxLevel));
            }
            else
            {
                selectorComponent (makeKey ("LOAD KONTAKT", [this] {
                                       if (auto* e = engineOf (host))
                                           e->auxInstrument().loadKontakt();
                                   }),
                                   "INSTRUMENT", 130);
                selectorComponent (makeKey ("OPEN", [this] {
                                       if (auto* e = engineOf (host))
                                           e->auxInstrument().openEditor();
                                   }),
                                   {}, 70);
                selectorComponent (makeKey ("UNLOAD", [this] {
                                       if (auto* e = engineOf (host))
                                           e->auxInstrument().unload();
                                   }),
                                   {}, 80);
                knob (auxLevel, "LEVEL", textOf (host, auxLevel));
            }
            // (Addendum 05: D's MORPH is MORPH TO D in the MORPH rack - the MORPH macro drives it.)
        }

    private:
        std::unique_ptr<juce::Component> makeKey (const juce::String& text, std::function<void()> action)
        {
            auto key = std::make_unique<TextKey> (text);
            key->onClick = std::move (action);
            return key;
        }
    };
}

//==============================================================================

juce::Colour aux::colour() { return juce::Colour (0xff8a6d84); }
juce::Colour aux::badgeTop() { return juce::Colour (0xffa0849a); }
juce::Colour aux::badgeBottom() { return juce::Colour (0xff5e4459); }
juce::Colour aux::onDark() { return juce::Colour (0xffc7a6bf); }

juce::String aux::modeName (int mode)
{
    return mode == 1 ? "NOISE" : mode == 2 ? "KONTAKT" : mode == 3 ? "LIVE INPUT" : "RHYTHM";
}

juce::String aux::readout (InstrumentHost& host)
{
    const auto* engine = engineOf (host);
    const int mode = modeOf (host);
    if (mode == 1)
    {
        static const char* colours[] = { "WHITE", "PINK", "BROWN" };
        const int c = std::clamp (juce::roundToInt (host.parameterValue (params::auxNoiseColor)), 0, 2);
        return juce::String (colours[c]) + " NOISE" + dot() + (host.parameterValue (params::auxNoiseGate) >= 0.5f ? "FREE" : "KEYS");
    }
    if (mode == 2)
        return {};   // (KONTAKT: the picture names it - or says it is not loaded - once)
    if (mode == liveMode)
    {
        // LIVE INPUT: its channels and its level, discreetly (the picture says when there is none).
        juce::String text ("LIVE INPUT");
        if (engine != nullptr)
            if (const auto v = engine->liveInput(); v.busEnabled)
            {
                text << dot() << (v.channels > 1 ? "STEREO" : "MONO");
                const float peak = std::max (v.peakLeft, v.peakRight);
                if (peak > 1.0e-4f)
                    text << dot() << juce::String (juce::Decibels::gainToDecibels (peak), 1) << " dB PEAK";
            }
        return text;
    }
    if (engine == nullptr)
        return "RHYTHM";
    const auto state = engine->auxRecordingState();
    const auto loop = engine->auxLoop();
    if (loop == nullptr)
        return state.name.isNotEmpty() ? state.name.toUpperCase() : juce::String ("NO LOOP");
    auto text = state.name.toUpperCase();
    if (loop->rhythmic())
        text << dot() << juce::String (loop->analysis.bpm, 1) << " BPM" << dot() << loop->analysis.beats << " BEATS";
    text << dot() << (host.parameterValue (params::auxRhythmGate) >= 0.5f ? "FREE" : "KEYS");
    return text;
}

void aux::reset (InstrumentHost& host)
{
    host.undo().beginNewTransaction ("Reset D / AUX");
    for (const auto& id : params::engineParameterIds())
        if (id.startsWith ("analog.aux."))
            if (auto* p = host.apvts().getParameter (id))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->getDefaultValue());
                p->endChangeGesture();
            }
}

void aux::chooseRecording (InstrumentHost& host, juce::Component& near)
{
    auto chooser = std::make_shared<juce::FileChooser> ("Load a loop for D", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
    juce::Component::SafePointer<juce::Component> safe (&near);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [&host, chooser, safe] (const juce::FileChooser& fc) {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;
        if (auto* engine = engineOf (host))
        {
            setParameter (host, params::auxMode, 0.0f);
            engine->loadAuxRecording (fc.getResult(), importFolder (host));
        }
    });
}

std::unique_ptr<juce::Component> createAuxStrip (InstrumentHost& host) { return std::make_unique<AuxStrip> (host); }
std::unique_ptr<rack::Module> createAuxRack (InstrumentHost& host) { return std::make_unique<AuxRack> (host); }

juce::Component* AuxStripView::part (juce::Component& strip, const juce::String& name)
{
    auto* s = dynamic_cast<AuxStrip*> (&strip);
    return s != nullptr ? s->part (name) : nullptr;
}

int AuxStripView::shownMode (juce::Component& strip)
{
    auto* s = dynamic_cast<AuxStrip*> (&strip);
    return s != nullptr ? s->shownMode() : -1;
}

juce::StringArray AuxStripView::knobCaptions (juce::Component& strip)
{
    auto* s = dynamic_cast<AuxStrip*> (&strip);
    return s != nullptr ? s->knobCaptions() : juce::StringArray();
}

juce::StringArray AuxStripView::menuItems (juce::Component& strip)
{
    juce::StringArray out;
    if (auto* s = dynamic_cast<AuxStrip*> (&strip))
    {
        auto m = s->createMenu();
        for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
            if (! it.getItem().isSeparator)
                out.add (it.getItem().text);
    }
    return out;
}

void AuxStripView::runMenuItem (juce::Component& strip, const juce::String& item)
{
    if (auto* s = dynamic_cast<AuxStrip*> (&strip))
        s->runMenuItem (item);
}

void AuxStripView::refresh (juce::Component& strip)
{
    if (auto* s = dynamic_cast<AuxStrip*> (&strip))
        s->refreshNow();
}

void AuxStripView::doubleClick (juce::Component& strip)
{
    if (auto* s = dynamic_cast<AuxStrip*> (&strip))
        setAuxRackOpen (*s, ! auxRackOpen (*s));
}

} // namespace andor::analog
