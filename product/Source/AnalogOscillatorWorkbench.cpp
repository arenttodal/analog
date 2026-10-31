#include "AnalogOscillatorWorkbench.h"

#include "AnalogEngine.h"
#include "AnalogParameters.h"
#include "Binding.h"
#include "Design.h"
#include "EngineCard.h"
#include "OspLookAndFeel.h"
#include "ShapingPopups.h"

#include <array>
#include <vector>

namespace andor::analog
{

using namespace osp::plugin;

namespace
{
    // The well's own palette: the EQ / NOTE MAP pages' graphite cells and cream type, layer
    // A's warm accent (the card's badge, its grain knobs' arcs).
    const juce::Colour cellFill { 0xff2b3031 };
    const juce::Colour cellEdge { 0xff3e4446 };
    const juce::Colour cellEdgeHot { 0xff59605f };
    const juce::Colour cream { 0xffeee8dd };
    juce::Colour accent() { return palette::layer (0); }

    constexpr int toolbarHeight = 30, margin = 12, keyHeight = 28;

    /** A key on the well: text (and an optional glyph), outlined; lit (toggled) it is filled
        faintly and outlined in the accent, a small light at its left when `led`. A parameter's
        switch (ButtonAttachment) or a view toggle (PIN). */
    class WellKey final : public juce::Button
    {
    public:
        enum class Glyph { none, wave, pin };

        explicit WellKey (const juce::String& name, Glyph g = Glyph::none, bool withLed = false)
            : juce::Button (name), glyph (g), led (withLed)
        {
            setTitle (name);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setWantsKeyboardFocus (true);
        }

        bool showText = true;

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto r = getLocalBounds().toFloat().reduced (1.0f);
            const float radius = std::min (6.0f, 0.24f * r.getHeight());
            const bool on = getToggleState();
            const auto a = accent();
            g.setColour (cellFill.withAlpha (over || down ? 1.0f : 0.9f));
            g.fillRoundedRectangle (r, radius);
            if (on)
            {
                g.setColour (a.withAlpha (0.16f));
                g.fillRoundedRectangle (r, radius);
            }
            g.setColour (hasKeyboardFocus (false) ? a : (on ? a.withAlpha (0.85f) : (over ? cellEdgeHot : cellEdge)));
            g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);

            auto content = r.reduced (8.0f, 0.0f);
            const auto ink = on ? a.interpolatedWith (juce::Colour (0xfff2eee6), 0.35f) : cream.withAlpha (over ? 0.95f : 0.72f);
            if (led)
            {
                design::draw::led (g, { content.getX() + 3.0f, r.getCentreY() }, 5.0f, on ? a : juce::Colour (0xff4a5050), on ? 0.6f : 0.0f);
                content.removeFromLeft (10.0f);
            }
            if (glyph != Glyph::none)
            {
                const float side = std::min (16.0f, r.getHeight() - 10.0f);
                auto box = showText ? content.removeFromLeft (side + 4.0f).withSizeKeepingCentre (side, side) : content.withSizeKeepingCentre (side, side);
                drawGlyph (g, box, ink);
            }
            if (showText)
            {
                g.setColour (ink);
                g.setFont (type::popupLabel (juce::jlimit (10.0f, 12.5f, 0.42f * r.getHeight())));
                g.drawText (getButtonText(), content, juce::Justification::centred, false);
            }
        }

    private:
        void drawGlyph (juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c) const
        {
            juce::Path p;
            if (glyph == Glyph::wave)
            {
                // One period of a sine: the waveform view.
                for (int i = 0; i <= 24; ++i)
                {
                    const float t = static_cast<float> (i) / 24.0f;
                    const juce::Point<float> pt (b.getX() + t * b.getWidth(), b.getCentreY() - 0.36f * b.getHeight() * std::sin (juce::MathConstants<float>::twoPi * t));
                    i == 0 ? p.startNewSubPath (pt) : p.lineTo (pt);
                }
                g.setColour (c);
                g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                return;
            }
            // A pin: its round head, the collar, the needle (tilted when not pinned).
            const auto centre = b.getCentre();
            const float s = b.getWidth();
            p.addEllipse (centre.x - 0.22f * s, b.getY() + 0.02f * s, 0.44f * s, 0.44f * s);
            p.addRectangle (centre.x - 0.3f * s, b.getY() + 0.44f * s, 0.6f * s, 0.1f * s);
            g.setColour (c);
            const auto tilt = getToggleState() ? juce::AffineTransform() : juce::AffineTransform::rotation (0.6f, centre.x, centre.y);
            g.fillPath (p, tilt);
            juce::Path needle;
            needle.startNewSubPath (centre.x, b.getY() + 0.54f * s);
            needle.lineTo (centre.x, b.getBottom());
            g.strokePath (needle, juce::PathStrokeType (1.3f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded), tilt);
        }

        Glyph glyph;
        bool led;
    };

    /** View tabs for an interface choice that is not a parameter: the pages (the dark
        segmented strip, the chosen page lit in copper; WAVE with its glyph) or, quieter, the
        depth (QUICK | DETAILED: text only, the chosen one in copper over a short rule).
        Arrow keys move them. */
    class ViewTabs final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit ViewTabs (juce::StringArray names, bool quietStyle = false, bool waveGlyphFirst = false)
            : items (std::move (names)), quiet (quietStyle), waveGlyph (waveGlyphFirst)
        {
            setWantsKeyboardFocus (true);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        std::function<void (int)> onChange;
        int selected() const noexcept { return index; }
        void setSelected (int i)
        {
            i = juce::jlimit (0, items.size() - 1, i);
            if (i != index)
            {
                index = i;
                repaint();
            }
        }
        void choose (int i)
        {
            i = juce::jlimit (0, items.size() - 1, i);
            if (i == index)
                return;
            index = i;
            repaint();
            if (onChange != nullptr)
                onChange (index);
        }

        void paint (juce::Graphics& g) override
        {
            if (quiet)
            {
                paintQuiet (g);
                return;
            }
            const auto r = getLocalBounds().toFloat().reduced (0.5f, 1.0f);
            const float h = r.getHeight();
            const float w = r.getWidth() / static_cast<float> (std::max (1, items.size()));
            const float radius = std::max (3.0f, 0.2f * h);
            g.setColour (cellFill.withAlpha (0.9f));
            g.fillRoundedRectangle (r, radius);
            g.setColour (hasKeyboardFocus (false) ? accent().withAlpha (0.8f) : cellEdge);
            g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
            g.setFont (type::popupLabel (juce::jlimit (10.0f, 12.0f, 0.42f * h)));
            for (int i = 0; i < items.size(); ++i)
            {
                const auto cell = juce::Rectangle<float> (r.getX() + w * static_cast<float> (i), r.getY(), w, h);
                if (i > 0 && i != index && i != index + 1)
                {
                    g.setColour (cellEdge);
                    g.drawVerticalLine (juce::roundToInt (cell.getX()), cell.getY() + 4.0f, cell.getBottom() - 4.0f);
                }
                if (i == index)
                {
                    // The chosen view: lit as the segmented controls' chosen key (the accent's
                    // dark tone, its keyline, its text).
                    const auto key = cell.reduced (1.5f);
                    const auto lit = design::colour::accent;
                    const auto deep = lit.interpolatedWith (juce::Colour (0xff1e2222), 0.62f);
                    g.setGradientFill (juce::ColourGradient (deep.brighter (0.25f), 0.0f, key.getY(), deep.darker (0.15f), 0.0f, key.getBottom(), false));
                    g.fillRoundedRectangle (key, radius - 1.0f);
                    g.setColour (lit.withAlpha (0.9f));
                    g.drawRoundedRectangle (key.reduced (0.5f), radius - 1.0f, 1.0f);
                    g.setColour (lit.interpolatedWith (juce::Colour (0xfffff0e4), 0.3f));
                }
                else
                    g.setColour (cream.withAlpha (i == hover ? 0.92f : 0.62f));
                auto text = cell;
                if (waveGlyph && i == 0)
                {
                    // WAVE: a period of a sine before its name (the waveform page).
                    const float textWidth = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), items[i]);
                    const float glyphWidth = 15.0f;
                    auto both = cell.withSizeKeepingCentre (glyphWidth + 6.0f + textWidth, cell.getHeight());
                    drawSine (g, both.removeFromLeft (glyphWidth).withSizeKeepingCentre (glyphWidth, 10.0f));
                    both.removeFromLeft (6.0f);
                    text = both;
                }
                g.drawText (items[i], text, juce::Justification::centred, false);
            }
        }

        void paintQuiet (juce::Graphics& g)
        {
            // Quieter than the pages: no strip, the chosen depth in copper over a short rule,
            // the other receding; a dot between them.
            const auto r = getLocalBounds().toFloat();
            const float w = r.getWidth() / static_cast<float> (std::max (1, items.size()));
            g.setFont (type::popupLabel (juce::jlimit (10.0f, 11.5f, 0.46f * r.getHeight())));
            for (int i = 0; i < items.size(); ++i)
            {
                const auto cell = juce::Rectangle<float> (r.getX() + w * static_cast<float> (i), r.getY(), w, r.getHeight());
                const bool chosen = i == index;
                g.setColour (chosen ? design::colour::accent.interpolatedWith (juce::Colour (0xfffff0e4), 0.3f)
                                    : cream.withAlpha (i == hover ? 0.85f : 0.48f));
                g.drawText (items[i], cell, juce::Justification::centred, false);
                if (chosen)
                {
                    const float textWidth = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), items[i]);
                    g.setColour (design::colour::accent.withAlpha (0.9f));
                    g.fillRect (juce::Rectangle<float> (cell.getCentreX() - 0.5f * textWidth, cell.getBottom() - 2.5f, textWidth, 1.5f));
                }
                if (i > 0)
                {
                    g.setColour (cream.withAlpha (0.3f));
                    g.fillEllipse (juce::Rectangle<float> (2.0f, 2.0f).withCentre ({ cell.getX(), cell.getCentreY() }));
                }
            }
            if (hasKeyboardFocus (false))
            {
                g.setColour (design::colour::accent.withAlpha (0.6f));
                g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
            }
        }

        void drawSine (juce::Graphics& g, juce::Rectangle<float> b) const
        {
            juce::Path p;
            for (int i = 0; i <= 24; ++i)
            {
                const float t = static_cast<float> (i) / 24.0f;
                const juce::Point<float> pt (b.getX() + t * b.getWidth(), b.getCentreY() - 0.5f * b.getHeight() * std::sin (juce::MathConstants<float>::twoPi * t));
                i == 0 ? p.startNewSubPath (pt) : p.lineTo (pt);
            }
            g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        void mouseDown (const juce::MouseEvent& e) override { choose (cellAt (e.position.x)); }
        void mouseMove (const juce::MouseEvent& e) override { setHover (cellAt (e.position.x)); }
        void mouseExit (const juce::MouseEvent&) override { setHover (-1); }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key.isKeyCode (juce::KeyPress::leftKey))
                choose (index - 1);
            else if (key.isKeyCode (juce::KeyPress::rightKey))
                choose (index + 1);
            else
                return false;
            return true;
        }
        void focusGained (FocusChangeType) override { repaint(); }
        void focusLost (FocusChangeType) override { repaint(); }

    private:
        int cellAt (float x) const
        {
            return juce::jlimit (0, items.size() - 1, static_cast<int> (x / (static_cast<float> (getWidth()) / static_cast<float> (std::max (1, items.size())))));
        }
        void setHover (int h)
        {
            if (h != hover)
            {
                hover = h;
                repaint();
            }
        }

        juce::StringArray items;
        bool quiet, waveGlyph;
        int index = 0, hover = -1;
    };

    /** One oscillator's own cycle, drawn as the card's display draws the pair (a body from
        the centre line, the line over it), dimmer while the oscillator is off. */
    class WaveThumb final : public juce::Component
    {
    public:
        /** `repeats`: the cycle drawn this many times across; `framed`: in its own small panel
            (a thumbnail) rather than straight on the well with a quiet grid (the WAVE page). */
        explicit WaveThumb (int repeats = 1, bool isFramed = true) : times (repeats), framed (isFramed) {}

        void setCycle (std::vector<float> c, bool on)
        {
            cycle = std::move (c);
            sounding = on;
            repaint();
        }
        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat().reduced (0.5f);
            auto inner = r.reduced (8.0f, 7.0f);
            if (framed)
            {
                g.setColour (juce::Colour (0xff252a2b));
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (cellEdge.withAlpha (0.7f));
                g.drawRoundedRectangle (r, 6.0f, 1.0f);
            }
            else
            {
                // On the well itself: the display's quiet grid (rows, and a mark per period).
                inner = r.reduced (4.0f, 14.0f);
                g.setColour (design::colour::wellGrid.withAlpha (0.55f));
                for (int k = 1; k < 4; ++k)
                    g.fillRect (juce::Rectangle<float> (inner.getX(), inner.getY() + inner.getHeight() * static_cast<float> (k) / 4.0f - 0.5f, inner.getWidth(), 1.0f));
                for (int k = 1; k < times; ++k)
                    g.fillRect (juce::Rectangle<float> (inner.getX() + inner.getWidth() * static_cast<float> (k) / static_cast<float> (times) - 0.5f, inner.getY(), 1.0f, inner.getHeight()));
            }
            g.setColour (design::colour::wellGrid);
            g.fillRect (juce::Rectangle<float> (inner.getX(), inner.getCentreY() - 0.5f, inner.getWidth(), 1.0f));
            if (cycle.size() < 2)
                return;
            float peak = 1.0e-3f;
            for (auto v : cycle)
                peak = std::max (peak, std::abs (v));
            const float scale = (framed ? 0.5f : 0.46f) * inner.getHeight() / std::max (1.0f, peak);
            juce::Path line;
            const auto n = cycle.size();
            const auto total = static_cast<std::size_t> (times) * (n - 1);
            for (std::size_t i = 0; i <= total; ++i)
            {
                const float x = inner.getX() + inner.getWidth() * static_cast<float> (i) / static_cast<float> (total);
                const float y = inner.getCentreY() - scale * cycle[i % (n - 1)];
                i == 0 ? line.startNewSubPath (x, y) : line.lineTo (x, y);
            }
            auto body = line;
            body.lineTo (inner.getRight(), inner.getCentreY());
            body.lineTo (inner.getX(), inner.getCentreY());
            body.closeSubPath();
            const auto a = accent();
            g.setColour (a.withAlpha (sounding ? 0.16f : 0.06f));
            g.fillPath (body);
            g.setColour (a.withAlpha (sounding ? 0.95f : 0.35f));
            g.strokePath (line, juce::PathStrokeType (framed ? 1.5f : 1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

    private:
        std::vector<float> cycle;
        bool sounding = true;
        int times;
        bool framed;
    };

    /** A choice parameter's index now. */
    int choiceOf (InstrumentHost& host, const juce::String& id)
    {
        auto* p = host.apvts().getParameter (id);
        return p != nullptr ? juce::roundToInt (p->convertFrom0to1 (p->getValue())) : 0;
    }
    bool onOf (InstrumentHost& host, const juce::String& id) { return host.parameterValue (id) >= 0.5f; }

    /**
        A page of the well: the controls it owns (each bound to its parameter, made once) and
        the rules that show, fade or arrange them. Subclasses lay them out (resized) and say,
        on each refresh, which are shown and which only faded (inactive: their setting is
        kept, it simply does nothing in the current state).
    */
    class Page : public juce::Component
    {
    public:
        explicit Page (InstrumentHost& h) : host (h) {}
        virtual void refresh()
        {
            if (updateStates())
                resized();
        }

    protected:
        /** Visibility / fading from the parameters; true when the layout must follow. */
        virtual bool updateStates() { return false; }

        MiniKnob* knob (const juce::String& id, const char* caption, const char* tooltip = nullptr)
        {
            auto* p = host.apvts().getParameter (id);
            jassert (p != nullptr);
            auto k = std::make_unique<MiniKnob> (host.apvts(), id, caption, [p] (double v) { return p->getText (p->convertTo0to1 (static_cast<float> (v)), 0); });
            k->setOnDark (true);
            k->slider.getProperties().set ("arc", static_cast<juce::int64> (accent().getARGB()));
            k->slider.getProperties().set ("graphite", true);   // (the knob's graphite cap: it sits on the well)
            if (tooltip != nullptr)
                k->slider.setTooltip (tooltip);
            return add (std::move (k));
        }
        SegmentedControl* segments (const juce::String& id, juce::StringArray items, const char* tooltip)
        {
            auto s = std::make_unique<SegmentedControl> (*host.apvts().getParameter (id), std::move (items));
            s->setOnDark (true);
            s->setAccent (design::colour::accent);   // (the chosen segment: the instrument's accent key)
            s->setTooltip (tooltip);
            s->setTitle (host.apvts().getParameter (id)->getName (64));
            binding::tag (*s, id);
            return add (std::move (s));
        }
        ValueSelector* selector (const juce::String& id, const char* tooltip, const char* caption = "")
        {
            auto s = std::make_unique<ValueSelector> (*host.apvts().getParameter (id), juce::String (caption));
            s->setOnDark (true);
            s->setTooltip (tooltip);
            s->setTitle (host.apvts().getParameter (id)->getName (64));
            binding::tag (*s, id);
            return add (std::move (s));
        }
        WellKey* toggle (const juce::String& id, const char* text, const char* tooltip)
        {
            auto t = std::make_unique<WellKey> (text, WellKey::Glyph::none, true);
            t->setClickingTogglesState (true);
            t->setTooltip (tooltip);
            binding::tag (*t, id);
            attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (host.apvts(), id, *t));
            return add (std::move (t));
        }
        void note (juce::Rectangle<int> area, juce::String text)
        {
            notes.push_back ({ area, std::move (text) });
        }

        template <typename C>
        C* add (std::unique_ptr<C> c)
        {
            auto* raw = c.get();
            addAndMakeVisible (*raw);
            owned.push_back (std::move (c));
            return raw;
        }

        /** Faded: a setting kept but inactive now (it can still be set). */
        static void fade (juce::Component* c, bool inactive)
        {
            if (c != nullptr)
                c->setAlpha (inactive ? 0.38f : 1.0f);
        }
        /** Shows `c` (true when it changed). */
        static bool show (juce::Component* c, bool visible)
        {
            if (c == nullptr || c->isVisible() == visible)
                return false;
            c->setVisible (visible);
            return true;
        }

        /** Knobs in rows across `area` (as many per row as fit at `minWidth`, each at most
            `maxWidth` wide, centred), each row at most `maxHeight` high. */
        static void flowKnobs (const std::vector<juce::Component*>& knobs, juce::Rectangle<int> area, int minWidth = 74, int maxWidth = 112, int maxHeight = 78,
                               bool fromLeft = false)
        {
            std::vector<juce::Component*> shown;
            for (auto* k : knobs)
                if (k != nullptr && k->isVisible())
                    shown.push_back (k);
            if (shown.empty() || area.isEmpty())
                return;
            const int perRow = std::max (1, std::min (static_cast<int> (shown.size()), area.getWidth() / minWidth));
            const int rows = (static_cast<int> (shown.size()) + perRow - 1) / perRow;
            const int rowHeight = std::min (maxHeight, area.getHeight() / rows);
            const int cellWidth = std::min (maxWidth, area.getWidth() / perRow);
            for (int r = 0; r < rows; ++r)
            {
                const int first = r * perRow;
                const int count = std::min (perRow, static_cast<int> (shown.size()) - first);
                const int rowWidth = count * cellWidth;
                int x = fromLeft ? area.getX() : area.getCentreX() - rowWidth / 2;
                const int y = area.getY() + r * rowHeight + (area.getHeight() - rows * rowHeight) / 2;
                for (int i = 0; i < count; ++i, x += cellWidth)
                    shown[static_cast<std::size_t> (first + i)]->setBounds (x, y, cellWidth, rowHeight);
            }
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            g.setFont (type::popupLabel (10.5f));
            for (const auto& n : notes)
            {
                g.setColour (design::colour::wellText.brighter (0.25f));
                g.drawText (n.text, n.area, juce::Justification::centredLeft, true);
            }
        }

        InstrumentHost& host;
        struct Note
        {
            juce::Rectangle<int> area;
            juce::String text;
        };
        std::vector<Note> notes;   ///< captions, placed by resized()

    private:
        std::vector<std::unique_ptr<juce::Component>> owned;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> attachments;
    };

    //==========================================================================
    /** The well's one grid: wide (one card) or narrow (two cards), a cell per knob, the gaps
        between families, the heights of a caption and a band of knobs. Every page lays out on
        it, so the same things sit in the same places page after page. */
    struct Grid
    {
        explicit Grid (juce::Rectangle<int> area) : narrow (area.getWidth() < 900)
        {
            gap = narrow ? 14 : 30;
            caption = 14;
            band = narrow ? 62 : 86;
            rowGap = narrow ? 6 : 10;
        }
        bool narrow;
        int gap, caption, band, rowGap, cell = 0;
    };

    /** The settings a cycle's picture shows, read raw (a picture is redrawn only when they move). */
    std::vector<float> pictureSignature (InstrumentHost& host)
    {
        using namespace params;
        std::vector<float> v;
        for (const auto& id : { osc1Model, osc2Model, osc1Enabled, osc2Enabled, osc1Wave, osc2Wave, osc1DcoSaw, osc1DcoPulse, osc1DcoSub, osc2DcoSaw,
                                osc2DcoPulse, osc2DcoSub, osc1WideWave, osc2WideWave, osc1WideShape, osc2WideShape, osc1WideSpread, osc2WideSpread,
                                osc1Octave, osc1Coarse, osc2Octave, osc2Coarse, osc2Fine, osc1Width, osc2Width, osc1Level, osc2Level, sync, osc2LowFreq,
                                ringMix })
            v.push_back (host.parameterValue (id));
        return v;
    }

    /** The engine's own picture of one oscillator alone (AnalogEngine::pictureOf): at full
        level, the other silent, OSC 2 on its own period (no detune against OSC 1), OSC 1 still
        synced to OSC 2 when SYNC is on (its synced shape). `sounding`: it is switched on. */
    std::vector<float> oscillatorPicture (const params::Reader& reader, int slot, bool& sounding, int points)
    {
        auto p = reader.read();
        sounding = slot == 1 ? p.osc1Enabled : p.osc2Enabled;
        p.blend = 0.5;
        p.ringMix = 0.0;
        if (slot == 1)
        {
            p.osc1Enabled = true;
            p.osc1Level = 1.0;
            p.osc2Enabled = false;
        }
        else
        {
            p.osc2Enabled = true;
            p.osc2Level = 1.0;
            p.osc2LowFrequency = false;
            p.osc1Enabled = false;
            p.sync = false;
            p.osc1Octave = p.osc2Octave;
            p.osc1Coarse = p.osc2Coarse;
            p.osc2FineCents = 0.0;
        }
        return AnalogEngine::pictureOf (p, points);
    }

    /**
        OSC 1 / OSC 2: one oscillator in focus, on the same grid for both (so changing the page
        changes which oscillator is in focus, nothing moves). The heading: ON, the model, its
        waves. Then the families, left to right, each under its caption: WAVE (the oscillator's
        own cycle) -> PITCH -> SHAPE -> OUTPUT. QUICK shows the first row (the controls that
        tell most for the model); DETAILED adds a second row under the same families (the LFO
        depths, the DCO's PWM and its source, WIDE's pulse width): nothing above it moves.
        A model's controls the others lack are hidden, never reset.
    */
    class OscillatorPage final : public Page
    {
    public:
        OscillatorPage (InstrumentHost& h, int slotNumber, bool detailedView)
            : Page (h), slot (slotNumber), detailed (detailedView)
        {
            using namespace params;
            const bool first = slot == 1;
            ids.model = first ? osc1Model : osc2Model;
            ids.enabled = first ? osc1Enabled : osc2Enabled;
            ids.dcoPulse = first ? osc1DcoPulse : osc2DcoPulse;
            ids.dcoSource = first ? osc1DcoPwmSource : osc2DcoPwmSource;
            ids.wideWave = first ? osc1WideWave : osc2WideWave;

            on = toggle (ids.enabled, "ON", "The oscillator sounds (off: silent; as a sync or Poly Mod source it keeps running)");
            model = segments (ids.model, { "CLASSIC", "DCO", "WIDE" },
                              "The oscillator's model: CLASSIC (the original), DCO (stable, after the Juno), WIDE (after the Jupiter-8, with SUPER SAW)");
            if (first)
                classicWave = segments (osc1Wave, { "SAW", "PULSE", "BOTH" }, "Oscillator 1's waveform: saw, pulse, or both");
            else
                classicWaveSelector = selector (osc2Wave, "Oscillator 2's waveforms: saw, triangle and pulse, alone or together");
            dcoSaw = toggle (first ? osc1DcoSaw : osc2DcoSaw, "SAW", "The DCO's sawtooth");
            dcoPulse = toggle (ids.dcoPulse, "PULSE", "The DCO's pulse (its width: WIDTH, swept by PWM)");
            wideWave = selector (ids.wideWave, "WIDE's wave: saw, pulse, triangle, square, or SUPER SAW (seven detuned saws, a modern addition)");
            thumb = add (std::make_unique<WaveThumb>());

            octave = knob (first ? osc1Octave : osc2Octave, "OCT", "Octave");
            semi = knob (first ? osc1Coarse : osc2Coarse, "SEMI", "Semitones");
            if (! first)
                detune = knob (osc2Fine, "DETUNE", "Oscillator 2 against oscillator 1, in cents (the card's DETUNE)");
            width = knob (first ? osc1Width : osc2Width, "WIDTH", "Pulse width (it acts on the pulse wave)");
            sub = knob (first ? osc1DcoSub : osc2DcoSub, "SUB", "The DCO's sub-oscillator: a square one octave down");
            shape = knob (first ? osc1WideShape : osc2WideShape, "SHAPE", "The wave's edge: soft .. calibrated (centre) .. raw");
            spread = knob (first ? osc1WideSpread : osc2WideSpread, "SPREAD", "SUPER SAW's detune (it acts on SUPER SAW)");
            level = knob (first ? osc1Level : osc2Level, "LEVEL", "The oscillator's level in the mixer");
            if (detailed)
            {
                lfoPitch = knob (first ? lfoPitch1 : lfoPitch2, "LFO PITCH", "How far LFO 1 moves its pitch");
                lfoWidth = knob (first ? lfoWidth1 : lfoWidth2, "LFO PW", "How far LFO 1 moves its pulse width");
                pwm = knob (first ? osc1DcoPwmDepth : osc2DcoPwmDepth, "PWM", "How far the PWM source sweeps the pulse width");
                dcoSource = selector (ids.dcoSource, "What sweeps the DCO's pulse width: the WIDTH knob alone (MAN), LFO 1, or the filter envelope", "PWM BY");
            }
            setTitle (first ? "Oscillator 1" : "Oscillator 2");
            reader.attach (h.apvts());
            updateStates();
        }

        void refresh() override
        {
            Page::refresh();
            if (auto now = pictureSignature (host); now != pictured)
            {
                pictured = std::move (now);
                updatePicture();
            }
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (cellEdge.withAlpha (0.8f));
            for (const auto& d : dividers)
                g.fillRect (d);
        }

        void resized() override
        {
            notes.clear();
            dividers.clear();
            const auto area = getLocalBounds();
            Grid grid (area);
            const int thumbWidth = grid.narrow ? 96 : 236;
            grid.cell = juce::jlimit (62, 112, (area.getWidth() - thumbWidth - 3 * grid.gap) / 7);
            const int blockWidth = thumbWidth + 7 * grid.cell + 3 * grid.gap;
            // Across, every position is the same at both depths (DETAILED adds a row under the
            // same families); each depth's block is centred in the well's height.
            const int blockHeight = keyHeight + (grid.narrow ? 10 : 16) + grid.caption + 4 + grid.band + (detailed ? grid.rowGap + grid.band : 0);
            const int x0 = area.getX() + std::max (0, (area.getWidth() - blockWidth) / 2);
            const int y0 = area.getY() + std::max (0, (area.getHeight() - blockHeight) / 2);
            const int m = choiceOf (host, ids.model);

            // The heading: ON, the model, its waves (the oscillator's identity).
            auto head = juce::Rectangle<int> (x0, y0, blockWidth, keyHeight);
            on->setBounds (head.removeFromLeft (64));
            head.removeFromLeft (10);
            model->setBounds (head.removeFromLeft (grid.narrow ? 186 : 268));
            head.removeFromLeft (grid.narrow ? 12 : 20);
            layoutWaves (head, m);

            const int captionY = y0 + keyHeight + (grid.narrow ? 10 : 16);
            const int band1 = captionY + grid.caption + 4, band2 = band1 + grid.band + grid.rowGap;
            const int pitchX = x0 + thumbWidth + grid.gap, shapeX = pitchX + 3 * grid.cell + grid.gap, outX = shapeX + 2 * grid.cell + grid.gap;
            auto cell = [&] (int columnX, int index, int bandY) { return juce::Rectangle<int> (columnX + index * grid.cell, bandY, grid.cell, grid.band); };

            thumb->setBounds (x0, band1, thumbWidth, grid.band);
            if (dcoSource != nullptr)
                dcoSource->setBounds (juce::Rectangle<int> (x0, band2, std::min (thumbWidth, 170), grid.band).withSizeKeepingCentre (std::min (thumbWidth, 170), 44));
            note ({ x0, captionY, thumbWidth, grid.caption }, "CYCLE");
            note ({ pitchX, captionY, 3 * grid.cell, grid.caption }, "PITCH");
            note ({ shapeX, captionY, 2 * grid.cell, grid.caption }, "SHAPE");
            note ({ outX, captionY, grid.cell, grid.caption }, "OUTPUT");
            const int ruleBottom = (detailed ? band2 : band1) + grid.band;
            for (const int x : { pitchX, shapeX, outX })
                dividers.push_back ({ x - grid.gap / 2, captionY, 1, ruleBottom - captionY });

            // PITCH: OCT SEMI (DETUNE) | LFO PITCH.
            octave->setBounds (cell (pitchX, 0, band1));
            semi->setBounds (cell (pitchX, 1, band1));
            if (detune != nullptr)
                detune->setBounds (cell (pitchX, 2, band1));
            if (lfoPitch != nullptr)
                lfoPitch->setBounds (cell (pitchX, 0, band2));
            // SHAPE, by model: CLASSIC WIDTH | LFO PW; DCO WIDTH SUB | PWM LFO PW; WIDE SHAPE
            // SPREAD | WIDTH LFO PW.
            if (m == 0)
            {
                width->setBounds (cell (shapeX, 0, band1));
                if (lfoWidth != nullptr)
                    lfoWidth->setBounds (cell (shapeX, 0, band2));
            }
            else if (m == 1)
            {
                width->setBounds (cell (shapeX, 0, band1));
                sub->setBounds (cell (shapeX, 1, band1));
                if (pwm != nullptr)
                    pwm->setBounds (cell (shapeX, 0, band2));
                if (lfoWidth != nullptr)
                    lfoWidth->setBounds (cell (shapeX, 1, band2));
            }
            else
            {
                shape->setBounds (cell (shapeX, 0, band1));
                spread->setBounds (cell (shapeX, 1, band1));
                width->setBounds (cell (shapeX, 0, band2));
                if (lfoWidth != nullptr)
                    lfoWidth->setBounds (cell (shapeX, 1, band2));
            }
            level->setBounds (cell (outX, 0, band1));
        }

    private:
        void layoutWaves (juce::Rectangle<int> row, int m)
        {
            if (m == 0)
            {
                if (classicWave != nullptr)
                    classicWave->setBounds (row.removeFromLeft (std::min (264, row.getWidth())));
                if (classicWaveSelector != nullptr)
                    classicWaveSelector->setBounds (row.removeFromLeft (std::min (210, row.getWidth())));
            }
            else if (m == 1)
            {
                const int w = std::min (88, (row.getWidth() - 8) / 2);
                dcoSaw->setBounds (row.removeFromLeft (w));
                row.removeFromLeft (8);
                dcoPulse->setBounds (row.removeFromLeft (w));
            }
            else
                wideWave->setBounds (row.removeFromLeft (std::min (210, row.getWidth())));
        }

        bool updateStates() override
        {
            const int m = choiceOf (host, ids.model);
            bool changed = m != shownModel;
            shownModel = m;
            changed |= show (classicWave, m == 0);
            changed |= show (classicWaveSelector, m == 0);
            changed |= show (dcoSaw, m == 1);
            changed |= show (dcoPulse, m == 1);
            changed |= show (dcoSource, m == 1);
            changed |= show (wideWave, m == 2);
            changed |= show (width, m != 2 || detailed);
            changed |= show (sub, m == 1);
            changed |= show (pwm, m == 1);
            changed |= show (shape, m == 2);
            changed |= show (spread, m == 2);

            // Faded: kept, but doing nothing in this state.
            const bool hasPulse = m == 0 ? (slot == 1 ? choiceOf (host, params::osc1Wave) != 0 : pulseInClassic2())
                                         : (m == 1 ? onOf (host, ids.dcoPulse) : choiceOf (host, ids.wideWave) == 1);
            fade (width, ! hasPulse);
            fade (lfoWidth, ! hasPulse);
            fade (pwm, m == 1 && choiceOf (host, ids.dcoSource) == 0);
            fade (spread, m == 2 && choiceOf (host, ids.wideWave) != 4);
            // An oscillator switched off: its settings stay editable, quieter.
            const bool sounding = onOf (host, ids.enabled);
            for (auto* c : std::initializer_list<juce::Component*> { model, classicWave, classicWaveSelector, dcoSaw, dcoPulse, wideWave })
                if (c != nullptr && (c->getAlpha() > 0.5f) != sounding)
                    c->setAlpha (sounding ? 1.0f : 0.55f);
            return changed;
        }

        bool pulseInClassic2() const
        {
            // OSC 2's classic waves: Saw, Triangle, Pulse, Saw+Tri, Saw+Pulse, Tri+Pulse, All.
            const int w = choiceOf (host, params::osc2Wave);
            return w == 2 || w == 4 || w == 5 || w == 6;
        }

        void updatePicture()
        {
            bool sounding = true;
            auto cycle = oscillatorPicture (reader, slot, sounding, 192);
            thumb->setCycle (std::move (cycle), sounding);
        }

        struct Ids
        {
            juce::String model, enabled, dcoPulse, dcoSource, wideWave;
        } ids;
        int slot;
        bool detailed;
        int shownModel = -1;
        params::Reader reader;
        std::vector<float> pictured;
        std::vector<juce::Rectangle<int>> dividers;

        WellKey* on = nullptr;
        SegmentedControl* model = nullptr;
        SegmentedControl* classicWave = nullptr;
        ValueSelector* classicWaveSelector = nullptr;
        WellKey* dcoSaw = nullptr;
        WellKey* dcoPulse = nullptr;
        ValueSelector* dcoSource = nullptr;
        ValueSelector* wideWave = nullptr;
        WaveThumb* thumb = nullptr;
        MiniKnob* octave = nullptr;
        MiniKnob* semi = nullptr;
        MiniKnob* detune = nullptr;
        MiniKnob* width = nullptr;
        MiniKnob* pwm = nullptr;
        MiniKnob* sub = nullptr;
        MiniKnob* shape = nullptr;
        MiniKnob* spread = nullptr;
        MiniKnob* level = nullptr;
        MiniKnob* lfoPitch = nullptr;
        MiniKnob* lfoWidth = nullptr;
    };

    //==========================================================================
    /** An oscillator's name on BOTH ("OSC 1", its light): it opens the oscillator's own page. */
    class OscillatorName final : public juce::Button
    {
    public:
        explicit OscillatorName (int slot) : juce::Button (slot == 1 ? "OSC 1" : "OSC 2")
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setWantsKeyboardFocus (true);
            setTitle (getButtonText());
            setTooltip (juce::String ("Oscillator ") + juce::String (slot) + ": open its own page (every setting)");
        }
        bool sounding = true;

        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            auto r = getLocalBounds().toFloat();
            design::draw::led (g, { r.getX() + 4.0f, r.getCentreY() }, 6.0f, sounding ? accent() : juce::Colour (0xff4a5050), sounding ? 0.6f : 0.0f);
            r.removeFromLeft (14.0f);
            g.setFont (fonts::make (13.0f, fonts::Weight::semibold, 0.08f));
            g.setColour (cream.withAlpha (sounding ? (over ? 1.0f : 0.94f) : 0.55f));
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), getButtonText());
            g.drawText (getButtonText(), r.removeFromLeft (w + 2.0f), juce::Justification::centredLeft, false);
            icons::draw (g, icons::Kind::chevronRight, r.removeFromLeft (14.0f).withSizeKeepingCentre (9.0f, 9.0f), cream.withAlpha (over ? 0.8f : 0.4f), 1.3f);
            if (hasKeyboardFocus (false))
            {
                g.setColour (accent().withAlpha (0.6f));
                g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
            }
        }
    };

    /**
        One oscillator on BOTH, the same component for each (so the two are equal by
        construction). Its zones, top to bottom: identity (name, model, ON), the wave and its
        character (the oscillator's own picture under its wave choice; the model's character
        controls beside it), pitch and output (OCT, SEMI, LEVEL). Compact (a two-card well):
        one strip (identity and wave choice on a line; under it a small picture, the
        character, LEVEL). A model's controls the others lack are hidden, never reset; a
        setting that does nothing now is faded, never moved.
    */
    class OscillatorSection final : public Page
    {
    public:
        OscillatorSection (InstrumentHost& h, int slotNumber, std::function<void (int)> openPage) : Page (h), slot (slotNumber), name (slotNumber)
        {
            using namespace params;
            const bool first = slot == 1;
            ids.model = first ? osc1Model : osc2Model;
            ids.enabled = first ? osc1Enabled : osc2Enabled;
            ids.dcoPulse = first ? osc1DcoPulse : osc2DcoPulse;
            ids.dcoSource = first ? osc1DcoPwmSource : osc2DcoPwmSource;
            ids.wideWave = first ? osc1WideWave : osc2WideWave;

            addAndMakeVisible (name);
            name.onClick = [openPage, this] { openPage (slot); };
            on = toggle (ids.enabled, "ON", "The oscillator sounds (off: silent; as a sync or Poly Mod source it keeps running)");
            model = segments (ids.model, { "CLASSIC", "DCO", "WIDE" },
                              "The oscillator's model: CLASSIC (the original), DCO (stable, after the Juno), WIDE (after the Jupiter-8, with SUPER SAW)");
            if (first)
                classicWave = segments (osc1Wave, { "SAW", "PULSE", "BOTH" }, "Oscillator 1's waveform: saw, pulse, or both");
            else
                classicWaveSelector = selector (osc2Wave, "Oscillator 2's waveforms: saw, triangle and pulse, alone or together");
            dcoSaw = toggle (first ? osc1DcoSaw : osc2DcoSaw, "SAW", "The DCO's sawtooth");
            dcoPulse = toggle (ids.dcoPulse, "PULSE", "The DCO's pulse (its width: WIDTH, swept by PWM)");
            wideWave = selector (ids.wideWave, "WIDE's wave: saw, pulse, triangle, square, or SUPER SAW (seven detuned saws, a modern addition)");
            thumb = add (std::make_unique<WaveThumb> (2, false));

            width = knob (first ? osc1Width : osc2Width, "WIDTH", "Pulse width (it acts on the pulse wave)");
            lfoWidth = knob (first ? lfoWidth1 : lfoWidth2, "LFO PW", "How far LFO 1 moves its pulse width");
            sub = knob (first ? osc1DcoSub : osc2DcoSub, "SUB", "The DCO's sub-oscillator: a square one octave down");
            pwm = knob (first ? osc1DcoPwmDepth : osc2DcoPwmDepth, "PWM", "How far the PWM source sweeps the pulse width");
            dcoSource = segments (ids.dcoSource, { "MAN", "LFO", "ENV" }, "What sweeps the DCO's pulse width: the WIDTH knob alone (MAN), LFO 1, or the filter envelope");
            shape = knob (first ? osc1WideShape : osc2WideShape, "SHAPE", "The wave's edge: soft .. calibrated (centre) .. raw");
            spread = knob (first ? osc1WideSpread : osc2WideSpread, "SPREAD", "SUPER SAW's detune (it acts on SUPER SAW)");
            octave = knob (first ? osc1Octave : osc2Octave, "OCT", "Octave");
            semi = knob (first ? osc1Coarse : osc2Coarse, "SEMI", "Semitones");
            level = knob (first ? osc1Level : osc2Level, "LEVEL", "The oscillator's level in the mixer");
            setTitle (first ? "Oscillator 1" : "Oscillator 2");
            reader.attach (h.apvts());
            updateStates();
        }

        /** Compact: one strip (a two-card well). */
        void setCompact (bool c)
        {
            if (c == compact)
                return;
            compact = c;
            updateStates();
            resized();
        }

        void refresh() override
        {
            Page::refresh();
            if (auto now = pictureSignature (host); now != pictured)
            {
                pictured = std::move (now);
                bool sounding = true;
                auto cycle = oscillatorPicture (reader, slot, sounding, 192);
                thumb->setCycle (std::move (cycle), sounding);
            }
        }

        void resized() override
        {
            notes.clear();
            if (compact)
                layoutStrip();
            else
                layoutWide();
        }

    private:
        static constexpr int nameWidth = 76, onWidth = 58, caption = 14;

        void layoutIdentity (juce::Rectangle<int> row, int modelWidth)
        {
            name.setBounds (row.removeFromLeft (nameWidth));
            on->setBounds (row.removeFromRight (onWidth));
            row.removeFromLeft (8);
            model->setBounds (row.removeFromLeft (std::min (modelWidth, row.getWidth() - 8)));
        }

        /** The wave choice for the model in `row`. */
        void layoutWaves (juce::Rectangle<int> row, int m)
        {
            if (m == 0)
            {
                if (classicWave != nullptr)
                    classicWave->setBounds (row);
                if (classicWaveSelector != nullptr)
                    classicWaveSelector->setBounds (row);
            }
            else if (m == 1)
            {
                const int w = (row.getWidth() - 8) / 2;
                dcoSaw->setBounds (row.removeFromLeft (w));
                row.removeFromLeft (8);
                dcoPulse->setBounds (row);
            }
            else
                wideWave->setBounds (row);
        }

        /** The model's character controls, two to a row, from `area`'s left (cell wide). */
        void layoutCharacter (juce::Rectangle<int> area, int cell, int band, int m, bool withSource)
        {
            auto at = [&] (int column) { return juce::Rectangle<int> (area.getX() + column * cell, area.getY(), cell, band); };
            if (m == 0)
            {
                width->setBounds (at (0));
                lfoWidth->setBounds (at (1));
            }
            else if (m == 1)
            {
                sub->setBounds (at (0));
                pwm->setBounds (at (1));
                if (withSource)
                    dcoSource->setBounds (juce::Rectangle<int> (area.getX() + 6, area.getY() + band + 2, 2 * cell - 12, 24));
            }
            else
            {
                shape->setBounds (at (0));
                spread->setBounds (at (1));
            }
        }

        void layoutWide()
        {
            const auto area = getLocalBounds();
            const int m = choiceOf (host, ids.model);
            // One knob band for every model and both rows (the same knob size everywhere);
            // the row under CHARACTER is kept for the DCO's PWM source in every model, so
            // nothing moves when the model changes.
            const int cap = 12, sourceRow = 28, rowGap = 6;
            const int avail = area.getHeight() - keyHeight - 8;
            const int band = juce::jlimit (60, 84, (avail - 2 * (cap + 2) - sourceRow - rowGap) / 2);
            const int zoneB = cap + 2 + band + sourceRow;
            const int waveWidth = area.getWidth() * 56 / 100, gutter = 14;
            const int charWidth = area.getWidth() - waveWidth - gutter;
            const int cell = std::min (112, charWidth / 2);
            const int charX = area.getX() + waveWidth + gutter + (charWidth - 2 * cell) / 2;

            // Zone A: identity.
            layoutIdentity (area.withHeight (keyHeight), 268);

            // Zone B: the wave choice over its picture; CHARACTER beside it.
            const int yB = area.getY() + keyHeight + 8;
            const int choice = 24;
            layoutWaves (juce::Rectangle<int> (area.getX(), yB, waveWidth, choice), m);
            thumb->setBounds (area.getX(), yB + choice + 2, waveWidth, zoneB - choice - 2);
            note ({ charX, yB, 2 * cell, cap }, "CHARACTER");
            layoutCharacter ({ charX, yB + cap + 2, 2 * cell, band }, cell, band, m, true);

            // Zone C: PITCH (OCT, SEMI) under the picture, OUTPUT (LEVEL) under the character.
            const int yC = yB + zoneB + rowGap;
            note ({ area.getX(), yC, 2 * cell, cap }, "PITCH");
            note ({ charX, yC, cell, cap }, "OUTPUT");
            octave->setBounds (area.getX(), yC + cap + 2, cell, band);
            semi->setBounds (area.getX() + cell, yC + cap + 2, cell, band);
            level->setBounds (charX, yC + cap + 2, cell, band);
        }

        void layoutStrip()
        {
            const auto area = getLocalBounds();
            const int m = choiceOf (host, ids.model);
            // The line: name, model, wave choice, ON.
            auto line = area.withHeight (keyHeight);
            name.setBounds (line.removeFromLeft (nameWidth));
            on->setBounds (line.removeFromRight (onWidth));
            line.removeFromRight (8);
            line.removeFromLeft (4);
            const int modelWidth = std::min (204, line.getWidth() * 52 / 100);
            model->setBounds (line.removeFromLeft (modelWidth));
            line.removeFromLeft (8);
            layoutWaves (line, m);
            // Under it: a small picture, the character, LEVEL.
            auto row = area.withTrimmedTop (keyHeight + 4);
            const int cell = juce::jlimit (64, 92, (row.getWidth() * 60 / 100) / 3);
            const int band = std::min (80, row.getHeight());
            auto knobs = row.removeFromRight (3 * cell);
            row.removeFromRight (10);
            thumb->setBounds (row);
            const int y = knobs.getY() + (knobs.getHeight() - band) / 2;
            layoutCharacter ({ knobs.getX(), y, 2 * cell, band }, cell, band, m, false);
            level->setBounds (knobs.getX() + 2 * cell, y, cell, band);
        }

        bool updateStates() override
        {
            const int m = choiceOf (host, ids.model);
            bool changed = m != shownModel;
            shownModel = m;
            changed |= show (classicWave, m == 0);
            changed |= show (classicWaveSelector, m == 0);
            changed |= show (dcoSaw, m == 1);
            changed |= show (dcoPulse, m == 1);
            changed |= show (wideWave, m == 2);
            changed |= show (width, m == 0);
            changed |= show (lfoWidth, m == 0);
            changed |= show (sub, m == 1);
            changed |= show (pwm, m == 1);
            changed |= show (dcoSource, m == 1 && ! compact);
            changed |= show (shape, m == 2);
            changed |= show (spread, m == 2);
            changed |= show (octave, ! compact);
            changed |= show (semi, ! compact);

            const bool hasPulse = m == 0 ? (slot == 1 ? choiceOf (host, params::osc1Wave) != 0 : pulseInClassic2()) : onOf (host, ids.dcoPulse);
            fade (width, ! hasPulse);
            fade (lfoWidth, ! hasPulse);
            fade (pwm, m == 1 && (! hasPulse || choiceOf (host, ids.dcoSource) == 0));
            fade (spread, m == 2 && choiceOf (host, ids.wideWave) != 4);
            // Switched off: its settings stay editable, quieter (nothing moves or hides).
            const bool sounding = onOf (host, ids.enabled);
            if (name.sounding != sounding)
            {
                name.sounding = sounding;
                name.repaint();
            }
            for (auto* c : std::initializer_list<juce::Component*> { model, classicWave, classicWaveSelector, dcoSaw, dcoPulse, wideWave })
                if (c != nullptr && (c->getAlpha() > 0.5f) != sounding)
                    c->setAlpha (sounding ? 1.0f : 0.55f);
            return changed;
        }

        bool pulseInClassic2() const
        {
            // OSC 2's classic waves: Saw, Triangle, Pulse, Saw+Tri, Saw+Pulse, Tri+Pulse, All.
            const int w = choiceOf (host, params::osc2Wave);
            return w == 2 || w == 4 || w == 5 || w == 6;
        }

        struct Ids
        {
            juce::String model, enabled, dcoPulse, dcoSource, wideWave;
        } ids;
        int slot;
        bool compact = false;
        int shownModel = -1;
        params::Reader reader;
        std::vector<float> pictured;

        OscillatorName name;
        WellKey* on = nullptr;
        SegmentedControl* model = nullptr;
        SegmentedControl* classicWave = nullptr;
        ValueSelector* classicWaveSelector = nullptr;
        WellKey* dcoSaw = nullptr;
        WellKey* dcoPulse = nullptr;
        SegmentedControl* dcoSource = nullptr;
        ValueSelector* wideWave = nullptr;
        WaveThumb* thumb = nullptr;
        MiniKnob* width = nullptr;
        MiniKnob* lfoWidth = nullptr;
        MiniKnob* sub = nullptr;
        MiniKnob* pwm = nullptr;
        MiniKnob* shape = nullptr;
        MiniKnob* spread = nullptr;
        MiniKnob* octave = nullptr;
        MiniKnob* semi = nullptr;
        MiniKnob* level = nullptr;
    };

    /**
        BOTH (the first page): the two oscillators at once, each its own equal section.
        Wide (one card, or beside the ORGANIC dock): side by side about one quiet hairline.
        Compact (beside a loaded ORGANIC card): two strips, one above the other, each name
        opening that oscillator's own page.
    */
    class BothPage final : public Page
    {
    public:
        BothPage (InstrumentHost& h, std::function<void (int)> openPage) : Page (h), one (h, 1, openPage), two (h, 2, openPage)
        {
            addAndMakeVisible (one);
            addAndMakeVisible (two);
            setTitle ("Both oscillators");
        }

        /** The well width from which the two sit side by side. */
        static constexpr int wideFrom = 900;

        void refresh() override
        {
            one.refresh();
            two.refresh();
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (cellEdge.withAlpha (0.8f));
            g.fillRect (divider);
        }

        void resized() override
        {
            const auto area = getLocalBounds();
            const bool wide = area.getWidth() >= wideFrom;
            one.setCompact (! wide);
            two.setCompact (! wide);
            if (wide)
            {
                // Two equal halves about a hairline, at most 620 px each (centred in a wider well).
                const int gutter = 32;
                const int half = std::min (620, (area.getWidth() - gutter) / 2);
                const int x0 = area.getX() + (area.getWidth() - 2 * half - gutter) / 2;
                const int height = std::min (area.getHeight(), 262);
                const int y0 = area.getY() + (area.getHeight() - height) / 2;
                one.setBounds (x0, y0, half, height);
                two.setBounds (x0 + half + gutter, y0, half, height);
                divider = { x0 + half + gutter / 2, y0 + 4, 1, height - 8 };
            }
            else
            {
                const int gap = 14;
                const int strip = std::min (122, (area.getHeight() - gap) / 2);
                const int y0 = area.getY() + (area.getHeight() - 2 * strip - gap) / 2;
                one.setBounds (area.getX(), y0, area.getWidth(), strip);
                two.setBounds (area.getX(), y0 + strip + gap, area.getWidth(), strip);
                divider = { area.getX() + 8, y0 + strip + gap / 2, area.getWidth() - 16, 1 };
            }
        }

    private:
        OscillatorSection one, two;
        juce::Rectangle<int> divider;
    };

    //==========================================================================
    /**
        PAIR: what the two oscillators do together, in two groups under one grid. INTERACTION
        (SYNC, KEYBOARD, LO FREQ, RING) on the left, POLY MOD (its two amounts, its three
        destinations) on the right. DETAILED adds VOICES under INTERACTION (polyphony, unison
        and its detune, the stereo spread) and NOISE under POLY MOD (its level and colour). (KEY DNA is ANDOR OSP's own editor, opened from the
        card's KEY DNA key in GRANULAR.)
    */
    class PairPage final : public Page
    {
    public:
        PairPage (InstrumentHost& h, bool detailedView) : Page (h), detailed (detailedView)
        {
            using namespace params;
            syncKey = toggle (sync, "SYNC", "Hard sync: oscillator 2 restarts oscillator 1 every cycle (tune oscillator 1 up for the classic sweep)");
            keyboardKey = toggle (osc2KeyTrack, "KEYBOARD", "Oscillator 2 follows the keyboard (off: a fixed pitch, for drones and Poly Mod)");
            lowKey = toggle (osc2LowFreq, "LO FREQ", "Oscillator 2 seven octaves down: a slow modulator for Poly Mod (it is not heard then)");
            ring = knob (ringMix, "RING", "Ring modulation: the two oscillators multiplied, mixed in");
            polyEnv = knob (polyModEnv, "FILT ENV", "Poly Mod: how far the filter envelope moves the destinations");
            polyOsc = knob (polyModOsc2, "OSC 2", "Poly Mod: how far oscillator 2 moves the destinations (FM with PITCH 1)");
            toPitch = toggle (polyModToPitch1, "PITCH 1", "Poly Mod moves oscillator 1's pitch (FM with oscillator 2, sweeps with the envelope)");
            toWidth = toggle (polyModToWidth1, "WIDTH 1", "Poly Mod moves oscillator 1's pulse width");
            toFilter = toggle (polyModToCutoff, "FILTER", "Poly Mod moves the filter's cutoff");
            if (detailed)
            {
                voices = segments (polyphony, { "1", "2", "4", "8", "16" }, "How many voices play (with UNISON: how many stack on one note)");
                unisonKey = toggle (unison, "UNISON", "Every voice on one note, detuned: a thick mono sound");
                uniDetune = knob (unisonDetune, "UNI DETUNE", "How far the stacked voices are detuned");
                spreadKnob = knob (spread, "SPREAD", "How wide the voices are spread in the stereo field");
                noiseLevel = knob (noise, "NOISE", "The noise source's level in the mixer");
                noiseColour = segments (noiseColor, { "WHITE", "JUNO" }, "The noise's colour: white, or the Juno's darker hiss");
            }
            setTitle ("Pair");
        }

        /** The height its layout needs at a width (a narrow well scrolls DETAILED). */
        int preferredHeight (int width) const
        {
            const Grid grid (juce::Rectangle<int> (width, 1000));
            const int row1 = grid.caption + 4 + stackHeight();
            const int row = grid.caption + 4 + grid.band;
            if (! detailed)
                return row1;
            return row1 + grid.rowGap + 12 + row;
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (cellEdge.withAlpha (0.8f));
            for (const auto& d : dividers)
                g.fillRect (d);
        }

        void resized() override
        {
            notes.clear();
            dividers.clear();
            const auto area = getLocalBounds();
            Grid grid (area);
            grid.cell = grid.narrow ? 64 : 88;
            const int keysWidth = grid.narrow ? 104 : 128, segWidth = grid.narrow ? 150 : 172;
            // Two columns, the two groups packed and centred about the hairline between them;
            // DETAILED adds VOICES under INTERACTION (the same columns).
            const int interaction = keysWidth + 10 + grid.cell, polyMod = 2 * grid.cell + 10 + keysWidth;
            const int voicesWidth = segWidth + 10 + 2 * grid.cell;
            const int left = detailed ? std::max (interaction, voicesWidth) : interaction, right = polyMod;
            const int blockWidth = left + grid.gap + right;
            const int x0 = area.getX() + std::max (0, (area.getWidth() - blockWidth) / 2);
            const int rx = x0 + left + grid.gap;
            const int y0 = area.getY() + std::max (0, (area.getHeight() - preferredHeight (area.getWidth())) / 2);

            // Row 1: INTERACTION | POLY MOD.
            const int stack = stackHeight();
            const int band1 = y0 + grid.caption + 4;
            note ({ x0, y0, left, grid.caption }, "INTERACTION");
            note ({ rx, y0, right, grid.caption }, "POLY MOD");
            placeStack ({ syncKey, keyboardKey, lowKey }, { x0, band1, keysWidth, stack });
            ring->setBounds (juce::Rectangle<int> (x0 + keysWidth + 10, band1, grid.cell, stack).withSizeKeepingCentre (grid.cell, grid.band));
            polyEnv->setBounds (juce::Rectangle<int> (rx, band1, grid.cell, stack).withSizeKeepingCentre (grid.cell, grid.band));
            polyOsc->setBounds (juce::Rectangle<int> (rx + grid.cell, band1, grid.cell, stack).withSizeKeepingCentre (grid.cell, grid.band));
            placeStack ({ toPitch, toWidth, toFilter }, { rx + 2 * grid.cell + 10, band1, keysWidth, stack });
            int bottom = band1 + stack;
            if (detailed)
            {
                // Row 2: VOICES (polyphony over UNISON, then its detune and the stereo spread).
                const int top2 = bottom + grid.rowGap + 12;
                auto voicesArea = juce::Rectangle<int> (x0, top2, left, grid.caption + 4 + grid.band);
                note (voicesArea.removeFromTop (grid.caption), "VOICES");
                voicesArea.removeFromTop (4);
                auto column = voicesArea.removeFromLeft (segWidth);
                const int key = std::min (keyHeight, (grid.band - 8) / 2);
                voices->setBounds (column.removeFromTop (key));
                column.removeFromTop (grid.band - 2 * key);
                unisonKey->setBounds (column.removeFromTop (key));
                voicesArea.removeFromLeft (10);
                uniDetune->setBounds (voicesArea.removeFromLeft (grid.cell));
                spreadKnob->setBounds (voicesArea.removeFromLeft (grid.cell));
                // NOISE (the third source, beside the oscillators) under POLY MOD.
                auto noiseArea = juce::Rectangle<int> (rx, top2, right, grid.caption + 4 + grid.band);
                note (noiseArea.removeFromTop (grid.caption), "NOISE");
                noiseArea.removeFromTop (4);
                noiseLevel->setBounds (noiseArea.removeFromLeft (grid.cell));
                noiseArea.removeFromLeft (10);
                noiseColour->setBounds (noiseArea.removeFromLeft (std::min (noiseArea.getWidth(), segWidth)).withSizeKeepingCentre (std::min (noiseArea.getWidth(), segWidth), keyHeight));
                bottom = top2 + grid.caption + 4 + grid.band;
            }
            dividers.push_back ({ rx - grid.gap / 2, y0, 1, bottom - y0 });
        }

    private:
        static int stackHeight() { return 3 * keyHeight + 2 * 8; }
        static void placeStack (std::initializer_list<juce::Component*> keys, juce::Rectangle<int> area)
        {
            for (auto* k : keys)
            {
                k->setBounds (area.removeFromTop (keyHeight));
                area.removeFromTop (8);
            }
        }

        bool detailed;
        std::vector<juce::Rectangle<int>> dividers;
        WellKey* syncKey = nullptr;
        WellKey* keyboardKey = nullptr;
        WellKey* lowKey = nullptr;
        MiniKnob* ring = nullptr;
        MiniKnob* polyEnv = nullptr;
        MiniKnob* polyOsc = nullptr;
        WellKey* toPitch = nullptr;
        WellKey* toWidth = nullptr;
        WellKey* toFilter = nullptr;
        SegmentedControl* voices = nullptr;
        WellKey* unisonKey = nullptr;
        MiniKnob* uniDetune = nullptr;
        MiniKnob* spreadKnob = nullptr;
        MiniKnob* noiseLevel = nullptr;
        SegmentedControl* noiseColour = nullptr;
    };

    //==========================================================================
    /** The workbench: its header (the pages left, the depth and the pin right, on one line)
        over the page in focus. */
    class OscillatorWorkbench final : public CardWellEditor
    {
    public:
        explicit OscillatorWorkbench (InstrumentHost& h)
            : host (h), pageTabs ({ "BOTH", "OSC 1", "OSC 2", "PAIR" }), depthTabs ({ "QUICK", "DETAILED" }, true)
        {
            setTitle ("Oscillator workbench");
            setWantsKeyboardFocus (false);
            pageTabs.setComponentID ("pages");
            pageTabs.setTitle ("Page");
            pageTabs.setTooltip ("BOTH: the two oscillators at once. OSC 1, OSC 2: one oscillator in focus, every setting. PAIR: what they do together");
            pageTabs.onChange = [this] (int i) {
                auto v = viewState();
                v.section = i;
                setView (v);
            };
            addAndMakeVisible (pageTabs);
            depthTabs.setComponentID ("detail");
            depthTabs.setTitle ("Quick or detailed");
            depthTabs.setTooltip ("QUICK: the controls that matter most. DETAILED: every setting of the page");
            depthTabs.onChange = [this] (int i) {
                auto v = viewState();
                v.detailed = i == 1;
                setView (v);
            };
            addAndMakeVisible (depthTabs);
            waveKey.setComponentID ("waveform");
            waveKey.showText = false;
            waveKey.setTitle ("Show the waveform");
            waveKey.setTooltip ("Back to the waveform (as OSCILLATORS)");
            waveKey.onClick = [this] {
                if (onShowWaveform != nullptr)
                    onShowWaveform();
            };
            addAndMakeVisible (waveKey);
            pinKey.setComponentID ("pin");
            pinKey.showText = false;
            pinKey.setTitle ("Pin the oscillator editor");
            pinKey.setTooltip ("Pin: keep the oscillators open (after the EQ, and when the window opens)");
            pinKey.setClickingTogglesState (true);
            pinKey.onClick = [this] {
                auto v = viewState();
                v.pinned = pinKey.getToggleState();
                setView (v);
            };
            addAndMakeVisible (pinKey);
            viewport.setScrollBarsShown (true, false);
            viewport.setScrollBarThickness (6);
            addAndMakeVisible (viewport);
            viewStateChanged();
        }

        void refresh() override
        {
            if (page != nullptr)
                page->refresh();
        }

        void paint (juce::Graphics& g) override
        {
            // The display's own well (its colour, corners and rim): the editor is the well.
            design::draw::well (g, getLocalBounds().toFloat().reduced (0.5f), 9.0f, design::colour::wellA);
            g.setColour (cellEdge.withAlpha (0.8f));
            g.fillRect (separator);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (margin, 10);
            auto bar = r.removeFromTop (toolbarHeight);
            const bool narrow = bar.getWidth() < 700;
            // One line, one centre: the pages at the left; the depth (quieter, smaller) and the
            // pin at the right, a hairline between them.
            pageTabs.setBounds (bar.removeFromLeft (narrow ? 292 : 360));
            pinKey.setBounds (bar.removeFromRight (toolbarHeight));
            bar.removeFromRight (6);
            waveKey.setBounds (bar.removeFromRight (toolbarHeight));
            bar.removeFromRight (10);
            separator = bar.removeFromRight (1).withSizeKeepingCentre (1, 16);
            bar.removeFromRight (10);
            // The depth belongs to the focused pages (BOTH has one depth).
            depthTabs.setBounds (bar.removeFromRight (narrow ? 140 : 160).withSizeKeepingCentre (narrow ? 140 : 160, 24));
            if (! depthTabs.isVisible())
                separator = {};
            r.removeFromTop (narrow ? 8 : 12);
            viewport.setBounds (r);
            layoutPage();
        }

    protected:
        void viewStateChanged() override
        {
            const auto& v = viewState();
            pinKey.setToggleState (v.pinned, juce::dontSendNotification);
            depthTabs.setSelected (v.detailed ? 1 : 0);
            const int section = juce::jlimit (0, static_cast<int> (WorkbenchSection::count) - 1, v.section);
            pageTabs.setSelected (section);
            if (const bool depth = section != static_cast<int> (WorkbenchSection::both); depth != depthTabs.isVisible())
            {
                depthTabs.setVisible (depth);
                resized();
            }
            const int wanted = section == static_cast<int> (WorkbenchSection::both) ? 0 : 2 * section + (v.detailed ? 1 : 0);
            if (page == nullptr || wanted != shownPage)
            {
                // Only the page shown exists: its controls are attached, no others.
                viewport.setViewedComponent (nullptr, false);
                page.reset();
                shownPage = wanted;
                juce::Component::SafePointer<OscillatorWorkbench> safe (this);
                auto open = [safe] (int s) {
                    if (safe == nullptr)
                        return;
                    auto next = safe->viewState();
                    next.section = s;
                    safe->setView (next);
                };
                switch (static_cast<WorkbenchSection> (section))
                {
                    case WorkbenchSection::both: page = std::make_unique<BothPage> (host, open); break;
                    case WorkbenchSection::osc1: page = std::make_unique<OscillatorPage> (host, 1, v.detailed); break;
                    case WorkbenchSection::osc2: page = std::make_unique<OscillatorPage> (host, 2, v.detailed); break;
                    case WorkbenchSection::pair:
                    case WorkbenchSection::count:
                    default: page = std::make_unique<PairPage> (host, v.detailed); break;
                }
                viewport.setViewedComponent (page.get(), false);
                layoutPage();
                page->refresh();
            }
        }

    private:
        void layoutPage()
        {
            if (page == nullptr)
                return;
            // The page fills the well under the header; a narrow PAIR in DETAILED, taller than
            // the well, scrolls (vertically only, inside the well).
            const auto shown = viewport.getLocalBounds();
            int height = shown.getHeight();
            if (auto* pair = dynamic_cast<PairPage*> (page.get()))
                height = std::max (height, pair->preferredHeight (shown.getWidth()));
            const bool scrolls = height > shown.getHeight();
            page->setBounds (0, 0, shown.getWidth() - (scrolls ? viewport.getScrollBarThickness() + 4 : 0), height);
        }

        InstrumentHost& host;
        ViewTabs pageTabs, depthTabs;
        WellKey waveKey { "WAVE", WellKey::Glyph::wave };
        WellKey pinKey { "PIN", WellKey::Glyph::pin };
        juce::Viewport viewport;
        juce::Rectangle<int> separator;
        std::unique_ptr<Page> page;
        int shownPage = -1;
    };
}

std::unique_ptr<CardWellEditor> createOscillatorWorkbench (InstrumentHost& host)
{
    return std::make_unique<OscillatorWorkbench> (host);
}

} // namespace andor::analog
