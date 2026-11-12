#include "AuxUI.h"

#include "AnalogEngine.h"
#include "AnalogParameters.h"
#include "Binding.h"
#include "Design.h"
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

    int modeOf (InstrumentHost& host) { return std::clamp (juce::roundToInt (host.parameterValue (params::auxMode)), 0, 2); }
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
                                                                                          : juce::String ("DROP A LOOP OR CLICK LOAD");
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
        // KONTAKT: its state and name, and its level over time as a bar.
        g.setFont (fonts::label (compact ? 10.0f : 12.0f));
        juce::String text = "KONTAKT NOT LOADED" + dot() + "LOAD HOSTS IT FOR D";
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
        g.setColour (cream.withAlpha (0.7f * alpha));
        g.drawText (text, area.withTrimmedBottom (area.getHeight() * 0.35f), juce::Justification::centred, true);
        const auto bar = area.withTop (area.getBottom() - 4.0f).reduced (area.getWidth() * 0.1f, 0.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (ink.withAlpha (0.9f));
        g.fillRoundedRectangle (bar.withWidth (std::sqrt (level) * bar.getWidth()), 2.0f);
    }

    //==========================================================================
    /** A small on / off key bound to one of D's switches (M, S): a ceramic key, its letter lit
        while on (SOLO in D's plum, MUTE in graphite), as the cards' M / S. */
    class AuxKey final : public juce::Button
    {
    public:
        AuxKey (InstrumentHost& h, const juce::String& id, const juce::String& letter, juce::Colour litColour)
            : juce::Button (letter), host (h), lit (litColour),
              attachment (h.apvts(), id, *this)
        {
            setClickingTogglesState (true);
            setWantsKeyboardFocus (true);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            binding::tag (*this, id);
        }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            using namespace design;
            const auto r = getLocalBounds().toFloat().reduced (1.0f);
            const bool on = getToggleState();
            draw::button (g, r, 6.0f, on, over || down, lit);
            g.setColour (on ? juce::Colours::white : colour::textSecondary);
            g.setFont (fonts::make (12.0f, fonts::Weight::medium, 0.04f));
            g.drawText (getButtonText(), r, juce::Justification::centred, false);
            if (hasKeyboardFocus (false))
            {
                g.setColour (colour::text.withAlpha (0.35f));
                g.drawRoundedRectangle (r.expanded (1.0f), 7.0f, 1.0f);
            }
        }

    private:
        InstrumentHost& host;
        juce::Colour lit;
        juce::AudioProcessorValueTreeState::ButtonAttachment attachment;
    };

    /** A plain text key on the ceramic (ROUTE, ...): its word, a quiet chevron or dots. */
    class TextKey final : public juce::Button
    {
    public:
        explicit TextKey (const juce::String& text, bool dotsOnly = false, bool menu = false) : juce::Button (text), dots (dotsOnly), chevron (menu)
        {
            setWantsKeyboardFocus (true);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setTitle (dots ? juce::String ("D AUX menu") : text);
        }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            using namespace design;
            const auto r = getLocalBounds().toFloat().reduced (1.0f);
            draw::button (g, r, 6.0f, down, over, aux::colour());
            g.setColour (colour::textSecondary);
            if (dots)
            {
                for (int i = -1; i <= 1; ++i)
                    g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (r.getCentre().translated (6.0f * static_cast<float> (i), 0.0f)));
            }
            else
            {
                g.setFont (fonts::make (11.0f, fonts::Weight::medium, 0.08f));
                g.drawText (getButtonText(), chevron ? r.withTrimmedRight (12.0f) : r, juce::Justification::centred, false);
                if (chevron)   // (a menu: ROUTE)
                {
                    juce::Path p;
                    const auto c = juce::Point<float> (r.getRight() - 11.0f, r.getCentreY());
                    p.startNewSubPath (c.x - 3.0f, c.y - 1.5f);
                    p.lineTo (c.x, c.y + 1.5f);
                    p.lineTo (c.x + 3.0f, c.y - 1.5f);
                    g.strokePath (p, juce::PathStrokeType (1.2f));
                }
            }
            if (hasKeyboardFocus (false))
            {
                g.setColour (colour::text.withAlpha (0.35f));
                g.drawRoundedRectangle (r.expanded (1.0f), 7.0f, 1.0f);
            }
        }

    private:
        bool dots, chevron;
    };

    //==========================================================================
    /** The well's live picture (the strip's). */
    class AuxDisplay final : public juce::Component
    {
    public:
        explicit AuxDisplay (InstrumentHost& h) : host (h)
        {
            setInterceptsMouseClicks (false, false);
            setTitle ("D AUX display");
        }
        void paint (juce::Graphics& g) override { paintAuxPicture (g, getLocalBounds().toFloat(), host, true, shownPlayheadX); }
        float playheadX() const noexcept { return shownPlayheadX; }

    private:
        InstrumentHost& host;
        float shownPlayheadX = -1.0f;
    };

    //==========================================================================
    class AuxStrip final : public juce::Component, public juce::SettableTooltipClient, public juce::FileDragAndDropTarget, private juce::Timer
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
            mute = std::make_unique<AuxKey> (h, params::auxMute, "M", juce::Colour (0xff4a4f50));
            mute->setTooltip ("Mute D (its level glides down, it keeps running)");
            addAndMakeVisible (*mute);
            solo = std::make_unique<AuxKey> (h, params::auxSolo, "S", aux::colour());
            solo->setTooltip ("Solo D (with the cards' SOLO: only the soloed sources are heard; only while D is on)");
            addAndMakeVisible (*solo);

            modeSelector = std::make_unique<ValueSelector> (*h.apvts().getParameter (params::auxMode), juce::String());
            modeSelector->setPlain (true);
            modeSelector->setOnDark (true);
            modeSelector->setAccent (aux::onDark());
            modeSelector->setTooltip ("What D plays: a RHYTHM loop (host-synced), NOISE (white, pink, brown), or KONTAKT (its own instance, your keys)");
            modeSelector->setTitle ("D mode");
            binding::tag (*modeSelector, params::auxMode);
            addAndMakeVisible (*modeSelector);
            display = std::make_unique<AuxDisplay> (h);
            addAndMakeVisible (*display);
            load = std::make_unique<TextKey> ("LOAD");
            load->onClick = [this] { loadForMode(); };
            addAndMakeVisible (*load);

            route = std::make_unique<TextKey> ("ROUTE", false, true);
            route->setTooltip ("Where D goes: through the cards' FILTER (it follows the FILTER macro and rack) or around it, then the shared effects");
            route->onClick = [this] { showRouteMenu(); };
            addAndMakeVisible (*route);
            menu = std::make_unique<TextKey> ("...", true);
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
            if (name == "route") return route.get();
            if (name == "menu") return menu.get();
            if (name == "load") return load.get();
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

        void resized() override
        {
            // [ D AUX ][ power ][ M S ] | [ well: MODE  display  LOAD ] | knobs ... | ROUTE ...
            auto r = getLocalBounds().reduced (16, 0);
            const int cy = getHeight() / 2;
            badge = juce::Rectangle<int> (44, 44).withPosition (r.getX(), cy - 22);
            r.removeFromLeft (44 + 10);
            nameArea = juce::Rectangle<int> (44, 44).withPosition (r.getX(), cy - 22);
            r.removeFromLeft (44 + 8);
            power->setBounds (juce::Rectangle<int> (30, 30).withPosition (r.getX(), cy - 15));
            r.removeFromLeft (30 + 12);
            mute->setBounds (juce::Rectangle<int> (28, 24).withPosition (r.getX(), cy - 12));
            solo->setBounds (juce::Rectangle<int> (28, 24).withPosition (r.getX() + 32, cy - 12));
            r.removeFromLeft (60 + 18);

            auto right = r.removeFromRight (30 + 8 + 86);
            menu->setBounds (juce::Rectangle<int> (30, 26).withPosition (right.getRight() - 30, cy - 13));
            route->setBounds (juce::Rectangle<int> (86, 26).withPosition (right.getX(), cy - 13));
            r.removeFromRight (16);

            // Three knob cells (the most any mode shows), right-aligned before ROUTE.
            const int cellWidth = 92;
            auto knobArea = r.removeFromRight (3 * cellWidth);
            r.removeFromRight (16);
            for (std::size_t i = 0; i < knobs.size(); ++i)
            {
                const int slot = 3 - static_cast<int> (knobs.size()) + static_cast<int> (i);
                knobs[i]->setBounds (knobArea.getX() + slot * cellWidth, 4, cellWidth, getHeight() - 8);
            }

            well = r.withTrimmedTop (8).withTrimmedBottom (8);
            auto inner = well.reduced (10, 6);
            auto top = inner.removeFromTop (22);
            modeSelector->setBounds (top.removeFromLeft (124));
            load->setVisible (builtMode != 2 || true);
            load->setBounds (top.removeFromRight (64).withSizeKeepingCentre (64, 20));
            inner.removeFromTop (3);
            display->setBounds (inner);
        }

        void paint (juce::Graphics& g) override
        {
            using namespace design;
            const auto bounds = getLocalBounds().toFloat();
            draw::raised (g, bounds, layout::cardRadius, colour::cardTop, colour::cardBottom);
            // D's badge, as the cards' A / B / C.
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
            // The graphite well (the cards' displays).
            const auto w = well.toFloat();
            g.setColour (graphite);
            g.fillRoundedRectangle (w, 9.0f);
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.drawRoundedRectangle (w.reduced (0.5f), 9.0f, 1.0f);
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            // Not heard (muted, or another source soloed) or off: a veil over the well, as a card's.
            const bool on = poweredOf (host);
            const bool muted = host.parameterValue (params::auxMute) >= 0.5f;
            if (on && ! muted)
                return;
            const auto w = well.toFloat();
            g.setColour (juce::Colour (0xff15191a).withAlpha (0.45f));
            g.fillRoundedRectangle (w, 9.0f);
            g.setColour (juce::Colour (0xffd9dcd8).withAlpha (0.6f));
            g.setFont (fonts::make (11.0f, fonts::Weight::medium, 0.12f));
            g.drawText (on ? "MUTED" : "OFF", w.withTrimmedLeft (140.0f).withTrimmedRight (80.0f).removeFromTop (34.0f), juce::Justification::centred, false);
        }

        void mouseDoubleClick (const juce::MouseEvent&) override { setAuxRackOpen (*this, ! auxRackOpen (*this)); }

        bool isInterestedInFileDrag (const juce::StringArray& files) override { return files.size() == 1 && isAudioFile (files[0]); }
        void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
        void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
        void filesDropped (const juce::StringArray& files, int, int) override
        {
            dragOver = false;
            if (files.size() == 1 && isAudioFile (files[0]))
                if (auto* engine = engineOf (host))
                {
                    setParameter (host, params::auxMode, 0.0f);   // (a recording is D RHYTHM's)
                    engine->loadAuxRecording (juce::File (files[0]), importFolder (host));
                }
            repaint();
        }

    private:
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
                add (params::auxModAmount, "MOD AMOUNT", "How far D's rhythm envelope moves its target (the rack's MOD TARGET; 0 by default)");
            }
            else if (builtMode == 1)
            {
                add (params::auxNoiseColor, "COLOR", "WHITE, PINK or BROWN");
                add (params::auxNoiseTone, "TONE", "Its own low-pass, before the FILTER");
            }
            add (params::auxLevel, "LEVEL", "D's level (before the FILTER and the effects)");
            resized();
        }

        void loadForMode()
        {
            if (modeOf (host) == 2)
            {
                if (auto* engine = engineOf (host))
                {
                    auto& k = engine->auxInstrument();
                    if (k.state() == ExternalInstrument::State::ready)
                        k.openEditor();
                    else
                        k.loadKontakt();
                }
                return;
            }
            if (modeOf (host) == 1)
            {
                setAuxRackOpen (*this, true);   // (NOISE has nothing to load: its rack)
                return;
            }
            aux::chooseRecording (host, *this);
        }

        void showRouteMenu()
        {
            juce::PopupMenu m;
            const bool follow = host.parameterValue (params::auxFilterFollow) >= 0.5f;
            m.addSectionHeader ("D ROUTE");
            m.addItem ("Through the FILTER (follows the FILTER macro and rack)", true, follow, [this] { setParameter (host, params::auxFilterFollow, 1.0f); });
            m.addItem ("Around the FILTER", true, ! follow, [this] { setParameter (host, params::auxFilterFollow, 0.0f); });
            m.addSeparator();
            m.addItem ("Then DRIVE, MOVEMENT, ECHO, SPACE and the FX route", false, true, [] {});
            m.addItem ("SEND / DIRECT: not in this version", false, false, [] {});
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (route.get()));
        }

    public:
        void refreshNow() { timerCallback(); }

    private:
        void showMenu() { createMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menu.get())); }

        void timerCallback() override
        {
            if (modeOf (host) != builtMode)
                buildKnobs();
            load->setButtonText (builtMode == 2 ? (engineOf (host) != nullptr && engineOf (host)->auxInstrument().state() == ExternalInstrument::State::ready ? "OPEN" : "LOAD")
                                                : (builtMode == 1 ? "RACK" : "LOAD"));
            load->setTooltip (builtMode == 0 ? "Load a loop (WAV, AIFF or FLAC): it plays on the host's grid"
                              : builtMode == 1 ? "NOISE's rack: its gate, envelope, movement and width"
                                               : "Load Kontakt for D (its own instance; your keys, before the arpeggiator), or open its window");
            display->repaint();
            const bool on = poweredOf (host), muted = host.parameterValue (params::auxMute) >= 0.5f;
            if (on != shownOn || muted != shownMuted)
            {
                shownOn = on;
                shownMuted = muted;
                repaint();
            }
        }

        InstrumentHost& host;
        std::unique_ptr<rackui::PowerKey> power;
        std::unique_ptr<AuxKey> mute, solo;
        std::unique_ptr<ValueSelector> modeSelector;
        std::unique_ptr<AuxDisplay> display;
        std::unique_ptr<TextKey> load, route, menu;
        std::vector<std::unique_ptr<MiniKnob>> knobs;
        juce::Rectangle<int> badge, nameArea, well;
        int builtMode = -1;
        bool shownOn = false, shownMuted = false, dragOver = false;
        friend struct andor::analog::AuxStripView;
    };

    //==========================================================================
    class AuxGraph final : public GraphFrame
    {
    public:
        explicit AuxGraph (InstrumentHost& h) : host (h) {}
        void refresh() override { repaint(); }

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
        }

    private:
        InstrumentHost& host;
    };

    class AuxRack final : public ParameterRack
    {
    public:
        explicit AuxRack (InstrumentHost& h) : ParameterRack (h, "AUX", aux::colour(), 0.62f)
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
            selectorSegment (auxMode, "MODE", { { "RHYTHM", "Rhythm", 0.0f }, { "NOISE", "Noise", 1.0f }, { "KONTAKT", "Kontakt", 2.0f } }, 246,
                             "What D plays: a host-synced loop, NOISE, or its own Kontakt");
            const int mode = modeOf (host);
            if (mode == 0)
            {
                selectorSegment (auxSync, "CLOCK", { { "SYNC", "Sync", 0.0f }, { "FREE", "Free", 1.0f } }, 140,
                                 "SYNC: on the host's grid (its transport). FREE: its own clock at the loop's tempo");
                selectorComponent (makeKey ("LOAD LOOP", [this] { aux::chooseRecording (host, *this); }), "RECORDING", 120);
                steppedKnob (auxRate, "RATE", textOf (host, auxRate));
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
            group ({}, true);
            switchCell (auxFilterFollow, "FILTER", { { "FOLLOW", "Follow", 1.0f }, { "AROUND", "Around", 0.0f } },
                        "FOLLOW: D goes through the cards' FILTER (the FILTER macro and rack move it). AROUND: it does not");
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
    return mode == 1 ? "NOISE" : mode == 2 ? "KONTAKT" : "RHYTHM";
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
        return engine != nullptr && engine->auxInstrument().state() == ExternalInstrument::State::ready ? engine->auxInstrument().displayName().toUpperCase()
                                                                                                         : "KONTAKT" + dot() + "NOT LOADED";
    if (engine == nullptr)
        return "RHYTHM";
    const auto state = engine->auxRecordingState();
    const auto loop = engine->auxLoop();
    if (loop == nullptr)
        return state.name.isNotEmpty() ? state.name.toUpperCase() : juce::String ("NO LOOP");
    auto text = state.name.toUpperCase();
    if (loop->rhythmic())
        text << dot() << juce::String (loop->analysis.bpm, 1) << " BPM" << dot() << loop->analysis.beats << " BEATS";
    text << dot() << (host.parameterValue (params::auxSync) >= 0.5f ? "FREE" : "SYNC");
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
