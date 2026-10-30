#include "AnalogOscillatorWorkbench.h"

#include "AnalogEngine.h"
#include "AnalogParameters.h"
#include "Binding.h"
#include "Design.h"
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

    /** View tabs (QUICK | DETAILED, the Detailed sections): the dark segmented strip, for an
        interface choice that is not a parameter. Arrow keys move it. */
    class ViewTabs final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit ViewTabs (juce::StringArray names) : items (std::move (names))
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
                    const auto key = cell.reduced (1.5f);
                    g.setColour (accent().withAlpha (0.16f));
                    g.fillRoundedRectangle (key, radius - 1.0f);
                    g.setColour (accent().withAlpha (0.85f));
                    g.drawRoundedRectangle (key.reduced (0.5f), radius - 1.0f, 1.0f);
                    g.setColour (accent().interpolatedWith (juce::Colour (0xfff2eee6), 0.35f));
                }
                else
                    g.setColour (cream.withAlpha (i == hover ? 0.92f : 0.66f));
                g.drawText (items[i], cell, juce::Justification::centred, false);
            }
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
        int index = 0, hover = -1;
    };

    /** One oscillator's own cycle, drawn as the card's display draws the pair (a body from
        the centre line, the line over it), dimmer while the oscillator is off. */
    class WaveThumb final : public juce::Component
    {
    public:
        void setCycle (std::vector<float> c, bool on)
        {
            cycle = std::move (c);
            sounding = on;
            repaint();
        }
        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (juce::Colour (0xff252a2b));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (cellEdge.withAlpha (0.7f));
            g.drawRoundedRectangle (r, 6.0f, 1.0f);
            const auto inner = r.reduced (8.0f, 7.0f);
            g.setColour (design::colour::wellGrid);
            g.fillRect (juce::Rectangle<float> (inner.getX(), inner.getCentreY() - 0.5f, inner.getWidth(), 1.0f));
            if (cycle.size() < 2)
                return;
            float peak = 1.0e-3f;
            for (auto v : cycle)
                peak = std::max (peak, std::abs (v));
            const float scale = 0.5f * inner.getHeight() / std::max (1.0f, peak);
            juce::Path line;
            for (std::size_t i = 0; i < cycle.size(); ++i)
            {
                const float x = inner.getX() + inner.getWidth() * static_cast<float> (i) / static_cast<float> (cycle.size() - 1);
                const float y = inner.getCentreY() - scale * cycle[i];
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
            g.strokePath (line, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

    private:
        std::vector<float> cycle;
        bool sounding = true;
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
            if (tooltip != nullptr)
                k->slider.setTooltip (tooltip);
            return add (std::move (k));
        }
        SegmentedControl* segments (const juce::String& id, juce::StringArray items, const char* tooltip)
        {
            auto s = std::make_unique<SegmentedControl> (*host.apvts().getParameter (id), std::move (items));
            s->setOnDark (true);
            s->setAccent (accent());
            s->setTooltip (tooltip);
            s->setTitle (host.apvts().getParameter (id)->getName (64));
            binding::tag (*s, id);
            return add (std::move (s));
        }
        ValueSelector* selector (const juce::String& id, const char* tooltip)
        {
            auto s = std::make_unique<ValueSelector> (*host.apvts().getParameter (id), juce::String());
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

    /**
        One oscillator: its name, ON, its model (CLASSIC / DCO / WIDE), the model's own waves,
        a picture of its cycle and its controls. QUICK shows the three or four that tell most
        for its model; DETAILED every one the engine plays for it (tuning, the model's own,
        LFO depths). A model's controls the other models do not have are hidden, never reset.
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
            ids.classicWave = first ? osc1Wave : osc2Wave;
            ids.dcoPulse = first ? osc1DcoPulse : osc2DcoPulse;
            ids.dcoSaw = first ? osc1DcoSaw : osc2DcoSaw;
            ids.dcoSource = first ? osc1DcoPwmSource : osc2DcoPwmSource;
            ids.wideWave = first ? osc1WideWave : osc2WideWave;

            on = toggle (ids.enabled, "ON", "The oscillator sounds (off: silent; as a sync or Poly Mod source it keeps running)");
            model = segments (ids.model, { "CLASSIC", "DCO", "WIDE" },
                              "The oscillator's model: CLASSIC (the original), DCO (stable, after the Juno), WIDE (after the Jupiter-8, with SUPER SAW)");
            if (first)
                classicWave = segments (osc1Wave, { "SAW", "PULSE", "BOTH" }, "Oscillator 1's waveform: saw, pulse, or both");
            else
                classicWaveSelector = selector (osc2Wave, "Oscillator 2's waveforms: saw, triangle and pulse, alone or together");
            dcoSaw = toggle (ids.dcoSaw, "SAW", "The DCO's sawtooth");
            dcoPulse = toggle (ids.dcoPulse, "PULSE", "The DCO's pulse (its width: WIDTH, swept by PWM)");
            wideWave = selector (ids.wideWave, "WIDE's wave: saw, pulse, triangle, square, or SUPER SAW (seven detuned saws, a modern addition)");
            if (detailed)
                dcoSource = segments (ids.dcoSource, { "MAN", "LFO", "ENV" }, "What sweeps the DCO's pulse width: the WIDTH knob alone, LFO 1, or the filter envelope");
            thumb = add (std::make_unique<WaveThumb>());

            octave = knob (first ? osc1Octave : osc2Octave, "OCT", "Octave");
            semi = knob (first ? osc1Coarse : osc2Coarse, "SEMI", "Semitones");
            if (detailed && ! first)
                detune = knob (osc2Fine, "DETUNE", "Oscillator 2 against oscillator 1, in cents (the card's DETUNE)");
            width = knob (first ? osc1Width : osc2Width, "WIDTH", "Pulse width (it acts on the pulse wave)");
            pwm = knob (first ? osc1DcoPwmDepth : osc2DcoPwmDepth, "PWM", "How far LFO 1 or the filter envelope sweeps the pulse width");
            sub = knob (first ? osc1DcoSub : osc2DcoSub, "SUB", "The DCO's sub-oscillator: a square one octave down");
            shape = knob (first ? osc1WideShape : osc2WideShape, "SHAPE", "The wave's edge: soft .. calibrated (centre) .. raw");
            spread = knob (first ? osc1WideSpread : osc2WideSpread, "SPREAD", "SUPER SAW's detune (it acts on SUPER SAW)");
            level = knob (first ? osc1Level : osc2Level, "LEVEL", "The oscillator's level in the mixer");
            if (detailed)
            {
                lfoPitch = knob (first ? lfoPitch1 : lfoPitch2, "LFO PITCH", "How far LFO 1 moves its pitch");
                lfoWidth = knob (first ? lfoWidth1 : lfoWidth2, "LFO PW", "How far LFO 1 moves its pulse width");
            }
            setTitle (first ? "Oscillator 1" : "Oscillator 2");
            reader.attach (h.apvts());
            updateStates();
            updatePicture();
        }

        void refresh() override
        {
            Page::refresh();
            updatePicture();
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (cream.withAlpha (0.92f));
            g.setFont (fonts::make (detailed ? 13.0f : 12.5f, fonts::Weight::medium, 0.08f));
            g.drawText (slot == 1 ? "OSC 1" : "OSC 2", nameArea, juce::Justification::centredLeft, false);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            notes.clear();
            const bool narrow = r.getWidth() < 520;
            const int m = choiceOf (host, ids.model);
            // The heading: name, ON, the model (Detailed, with room: the model's waves too).
            auto head = r.removeFromTop (keyHeight);
            nameArea = head.removeFromLeft (narrow ? 46 : 54);
            on->setBounds (head.removeFromLeft (56));
            head.removeFromLeft (8);
            juce::Rectangle<int> waveRow;
            if (detailed && ! narrow)
            {
                model->setBounds (head.removeFromLeft (std::min (300, head.getWidth() / 2)));
                head.removeFromLeft (16);
                waveRow = head;
            }
            else
                model->setBounds (head.removeFromLeft (std::min (300, head.getWidth())));
            r.removeFromTop (12);

            if (detailed)
            {
                if (waveRow.isEmpty())
                {
                    waveRow = r.removeFromTop (keyHeight);
                    r.removeFromTop (10);
                }
                layoutWaves (waveRow, m);
                // With room, the picture at the left and the knobs level with it, the pair
                // centred in the well; otherwise the knobs alone, in as many rows as they need.
                const bool picture = r.getWidth() >= 900 && r.getHeight() >= 90;
                thumb->setVisible (picture);
                if (picture)
                {
                    const int h = std::min (r.getHeight(), 132);
                    auto block = r.withSizeKeepingCentre (r.getWidth(), h);
                    thumb->setBounds (block.removeFromLeft (232));
                    block.removeFromLeft (28);
                    flowKnobs (knobOrder(), block, 74, 108, 84, true);
                    return;
                }
                flowKnobs (knobOrder(), r, 70, 108, 84);
                return;
            }

            if (r.getWidth() >= 480)
            {
                // QUICK, a wide half: the picture at the left; the waves over the knobs at its
                // right; the block centred in the well.
                const int h = std::min (r.getHeight(), 136);
                auto block = r.withSizeKeepingCentre (r.getWidth(), h);
                thumb->setVisible (true);
                const int w = juce::jlimit (120, 230, juce::roundToInt (0.34f * static_cast<float> (block.getWidth())));
                thumb->setBounds (block.removeFromLeft (w));
                block.removeFromLeft (18);
                layoutWaves (block.removeFromTop (keyHeight), m);
                block.removeFromTop (10);
                flowKnobs (knobOrder(), block, 64, 104, 92);
                return;
            }
            // QUICK, a narrow half (two cards): the picture beside the waves, the knobs under.
            const int knobRow = juce::jlimit (56, 80, juce::roundToInt (0.44f * static_cast<float> (r.getHeight())));
            auto knobs = r.removeFromBottom (knobRow);
            r.removeFromBottom (6);
            const bool picture = r.getHeight() >= 34;
            thumb->setVisible (picture);
            auto mid = r;
            if (picture)
            {
                const int w = juce::jlimit (90, 220, juce::roundToInt (0.36f * static_cast<float> (mid.getWidth())));
                thumb->setBounds (mid.removeFromLeft (w).withSizeKeepingCentre (w, std::min (mid.getHeight(), 92)));
                mid.removeFromLeft (12);
            }
            layoutWaves (mid.withSizeKeepingCentre (mid.getWidth(), std::min (mid.getHeight(), keyHeight)), m);
            flowKnobs (knobOrder(), knobs, 64, 104, knobRow);
        }

    private:
        void layoutWaves (juce::Rectangle<int> row, int m)
        {
            if (row.isEmpty())
                return;
            if (m == 0)
            {
                if (classicWave != nullptr)
                    classicWave->setBounds (row.removeFromLeft (std::min (300, row.getWidth())));
                if (classicWaveSelector != nullptr)
                    classicWaveSelector->setBounds (row.removeFromLeft (std::min (220, row.getWidth())));
            }
            else if (m == 1)
            {
                const int w = std::min (86, (row.getWidth() - 8) / (dcoSource != nullptr ? 4 : 2));
                dcoSaw->setBounds (row.removeFromLeft (w));
                row.removeFromLeft (6);
                dcoPulse->setBounds (row.removeFromLeft (w));
                row.removeFromLeft (14);
                if (dcoSource != nullptr)
                    dcoSource->setBounds (row.removeFromLeft (std::min (210, row.getWidth())));
            }
            else
                wideWave->setBounds (row.removeFromLeft (std::min (220, row.getWidth())));
        }

        std::vector<juce::Component*> knobOrder() const
        {
            return { octave, semi, detune, width, pwm, sub, shape, spread, level, lfoPitch, lfoWidth };
        }

        bool updateStates() override
        {
            const int m = choiceOf (host, ids.model);
            bool changed = m != shownModel;
            shownModel = m;
            // The model's own waves.
            changed |= show (classicWave, m == 0);
            changed |= show (classicWaveSelector, m == 0);
            changed |= show (dcoSaw, m == 1);
            changed |= show (dcoPulse, m == 1);
            changed |= show (dcoSource, m == 1);
            changed |= show (wideWave, m == 2);
            // Its controls: QUICK the three or four that tell most, DETAILED all it plays.
            changed |= show (semi, detailed || m == 0);
            changed |= show (width, detailed ? true : m != 2);
            changed |= show (pwm, m == 1 && detailed);
            changed |= show (sub, m == 1);
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
            for (auto* c : std::initializer_list<juce::Component*> { model, classicWave, classicWaveSelector, dcoSaw, dcoPulse, dcoSource, wideWave })
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
            // Only when one of the settings the picture shows has moved (a few floats).
            std::array<float, 24> now {};
            const char* const shared[] = { "analog.sync", "analog.osc2.lowFreq" };
            std::size_t n = 0;
            for (const auto& id : { ids.model, ids.enabled, ids.classicWave, ids.dcoSaw, ids.dcoPulse, ids.wideWave })
                now[n++] = host.parameterValue (id);
            for (const auto* id : shared)
                now[n++] = host.parameterValue (id);
            using namespace params;
            for (const auto& id : { osc1Octave, osc1Coarse, osc2Octave, osc2Coarse, osc1Width, osc2Width, osc1DcoSub, osc2DcoSub, osc1WideShape,
                                    osc2WideShape, osc1WideSpread, osc2WideSpread, osc1Wave, osc2Wave })
                if (n < now.size())
                    now[n++] = host.parameterValue (id);
            if (now == pictured && ! thumbStale)
                return;
            pictured = now;
            thumbStale = false;
            // The engine's own picture (AnalogEngine::pictureOf), of this oscillator alone: at
            // full level, the other silent, OSC 2 on its own period (no detune against OSC 1),
            // OSC 1 still synced to OSC 2 when SYNC is on (its synced shape).
            auto p = reader.read();
            const bool sounding = slot == 1 ? p.osc1Enabled : p.osc2Enabled;
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
            thumb->setCycle (AnalogEngine::pictureOf (p, 192), sounding);
        }

        struct Ids
        {
            juce::String model, enabled, classicWave, dcoSaw, dcoPulse, dcoSource, wideWave;
        } ids;
        int slot;
        bool detailed;
        int shownModel = -1;
        juce::Rectangle<int> nameArea;
        params::Reader reader;
        std::array<float, 24> pictured {};
        bool thumbStale = true;

        WellKey* on = nullptr;
        SegmentedControl* model = nullptr;
        SegmentedControl* classicWave = nullptr;
        ValueSelector* classicWaveSelector = nullptr;
        WellKey* dcoSaw = nullptr;
        WellKey* dcoPulse = nullptr;
        SegmentedControl* dcoSource = nullptr;
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

    /** QUICK: both oscillators, side by side, a hairline between them. */
    class QuickPage final : public Page
    {
    public:
        explicit QuickPage (InstrumentHost& h) : Page (h), one (h, 1, false), two (h, 2, false)
        {
            addAndMakeVisible (one);
            addAndMakeVisible (two);
        }
        void refresh() override
        {
            one.refresh();
            two.refresh();
        }
        void resized() override
        {
            auto r = getLocalBounds();
            const int gap = r.getWidth() >= 900 ? 40 : 24;
            const int w = (r.getWidth() - gap) / 2;
            one.setBounds (r.removeFromLeft (w));
            divider = r.removeFromLeft (gap).withSizeKeepingCentre (1, r.getHeight() - 8);
            two.setBounds (r);
        }
        void paint (juce::Graphics& g) override
        {
            g.setColour (cellEdge);
            g.fillRect (divider);
        }

    private:
        OscillatorPage one, two;
        juce::Rectangle<int> divider;
    };

    /**
        A page of rows (PAIR, MORE, KEY DNA): each row a caption over its controls, the
        controls across the row by weight (knobs at most a knob's width, keys centred).
    */
    class RowsPage : public Page
    {
    public:
        using Page::Page;

        void resized() override
        {
            notes.clear();
            dividers.clear();
            auto r = getLocalBounds();
            float all = 0.0f;
            for (const auto& row : rows)
                all += total (row);
            constexpr int captionH = 16, gap = 32;
            // A wide well: the groups side by side (each as wide as its controls ask), a
            // hairline between; a narrow one: stacked, a caption over each.
            const bool columns = rows.size() > 1 && all * 100.0f + static_cast<float> (gap * static_cast<int> (rows.size() - 1)) <= static_cast<float> (r.getWidth());
            if (columns)
            {
                const float avail = static_cast<float> (r.getWidth() - gap * static_cast<int> (rows.size() - 1));
                const int h = std::min (r.getHeight(), captionH + 96);
                auto band = r.withSizeKeepingCentre (r.getWidth(), h);
                for (std::size_t i = 0; i < rows.size(); ++i)
                {
                    auto area = band.removeFromLeft (juce::roundToInt (avail * total (rows[i]) / all));
                    if (i + 1 < rows.size())
                    {
                        dividers.push_back (band.removeFromLeft (gap).withSizeKeepingCentre (1, h));
                    }
                    note (area.removeFromTop (captionH), rows[i].caption);
                    placeCells (rows[i], area.withSizeKeepingCentre (area.getWidth(), std::min (area.getHeight(), 84)));
                }
                return;
            }
            const int count = static_cast<int> (rows.size());
            const int rowH = std::min (captionH + 84, count > 0 ? (r.getHeight() - (count - 1) * 6) / count : r.getHeight());
            r = r.withSizeKeepingCentre (r.getWidth(), std::min (r.getHeight(), count * rowH + (count - 1) * 6));
            for (const auto& row : rows)
            {
                auto line = r.removeFromTop (rowH);
                r.removeFromTop (6);
                note (line.removeFromTop (captionH), row.caption);
                placeCells (row, line);
            }
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (cellEdge);
            for (const auto& d : dividers)
                g.fillRect (d);
        }

    protected:
        void row (juce::String caption) { rows.push_back ({ std::move (caption), {} }); }
        template <typename C>
        C* cell (C* c, float weight)
        {
            rows.back().cells.push_back ({ c, weight });
            return c;
        }

    private:
        struct Cell
        {
            juce::Component* component;
            float weight;
        };
        struct Row
        {
            juce::String caption;
            std::vector<Cell> cells;
        };

        static float total (const Row& row)
        {
            float t = 0.0f;
            for (const auto& c : row.cells)
                t += c.weight;
            return t;
        }

        /** A row's controls across `line` by weight: knobs at most a knob's size, keys centred. */
        static void placeCells (const Row& row, juce::Rectangle<int> line)
        {
            const float unit = std::min (static_cast<float> (line.getWidth()) / std::max (1.0f, total (row)), 150.0f);
            float x = static_cast<float> (line.getX());
            for (const auto& c : row.cells)
            {
                const float w = c.weight * unit;
                const auto cell = juce::Rectangle<float> (x, static_cast<float> (line.getY()), w, static_cast<float> (line.getHeight())).toNearestInt();
                if (dynamic_cast<MiniKnob*> (c.component) != nullptr)
                    c.component->setBounds (cell.withSizeKeepingCentre (std::min (cell.getWidth(), 104), std::min (cell.getHeight(), 76)));
                else
                    c.component->setBounds (cell.reduced (5, 0).withSizeKeepingCentre (std::min (cell.getWidth() - 10, 150), keyHeight));
                x += w;
            }
        }

        std::vector<Row> rows;
        std::vector<juce::Rectangle<int>> dividers;
    };

    /** PAIR: what the two oscillators do together. */
    class PairPage final : public RowsPage
    {
    public:
        explicit PairPage (InstrumentHost& h) : RowsPage (h)
        {
            using namespace params;
            row ("SYNC, KEYBOARD AND RING  (oscillator 2 is sync's master)");
            cell (toggle (sync, "SYNC", "Hard sync: oscillator 2 restarts oscillator 1 every cycle (tune oscillator 1 up for the classic sweep)"), 1.0f);
            cell (toggle (osc2KeyTrack, "KEYBOARD", "Oscillator 2 follows the keyboard (off: a fixed pitch, for drones and Poly Mod)"), 1.2f);
            cell (toggle (osc2LowFreq, "LO FREQ", "Oscillator 2 seven octaves down: a slow modulator for Poly Mod (it is not heard then)"), 1.1f);
            cell (knob (ringMix, "RING", "Ring modulation: the two oscillators multiplied, mixed in"), 1.0f);
            row ("POLY MOD  (the filter envelope and oscillator 2 as sources, to these destinations)");
            cell (knob (polyModEnv, "FILT ENV", "How far the filter envelope moves the destinations"), 1.0f);
            cell (knob (polyModOsc2, "OSC 2", "How far oscillator 2 moves the destinations (FM with PITCH 1)"), 1.0f);
            cell (toggle (polyModToPitch1, "PITCH 1", "Poly Mod moves oscillator 1's pitch (FM with oscillator 2, sweeps with the envelope)"), 1.1f);
            cell (toggle (polyModToWidth1, "WIDTH 1", "Poly Mod moves oscillator 1's pulse width"), 1.1f);
            cell (toggle (polyModToCutoff, "FILTER", "Poly Mod moves the filter's cutoff"), 1.1f);
        }
    };

    /** MORE: the noise and the voices. */
    class MorePage final : public RowsPage
    {
    public:
        explicit MorePage (InstrumentHost& h) : RowsPage (h)
        {
            using namespace params;
            row ("NOISE");
            cell (knob (noise, "NOISE", "The noise source's level in the mixer"), 1.0f);
            cell (segments (noiseColor, { "WHITE", "JUNO" }, "The noise's colour: white, or the Juno's darker hiss"), 1.8f);
            row ("VOICES");
            cell (segments (polyphony, { "1", "2", "4", "8", "16" }, "How many voices play (with UNISON: how many stack on one note)"), 2.6f);
            cell (toggle (unison, "UNISON", "Every voice on one note, detuned: a thick mono sound"), 1.1f);
            cell (knob (unisonDetune, "UNI DETUNE", "How far the stacked voices are detuned"), 1.0f);
            cell (knob (spread, "SPREAD", "How wide the voices are spread in the stereo field"), 1.0f);
        }
    };

    /** KEY DNA: GRANULAR's own grains per key. */
    class KeyDnaPage final : public RowsPage
    {
    public:
        explicit KeyDnaPage (InstrumentHost& h) : RowsPage (h)
        {
            using namespace params;
            row ("KEY DNA  (each key its own grains)");
            cell (toggle (keyDna, "ON", "KEY DNA: every key plays its own grains (where, how long, how dense, how wide), the same each time"), 0.9f);
            cell (knob (keyDnaAmount, "AMOUNT"), 1.0f);
            cell (knob (keyDnaIndividuality, "INDIVIDUAL"), 1.0f);
            cell (knob (keyDnaPosition, "POS"), 1.0f);
            cell (knob (keyDnaSize, "SIZE"), 1.0f);
            cell (knob (keyDnaDensity, "DENS"), 1.0f);
            cell (knob (keyDnaSpread, "SPREAD"), 1.0f);
            cell (knob (keyDnaTune, "TUNE"), 1.0f);
        }
        void resized() override
        {
            RowsPage::resized();
            // It acts while the card plays GRANULAR: said under the row.
            granularNote = getLocalBounds().removeFromBottom (18);
        }
        void paint (juce::Graphics& g) override
        {
            const bool granular = host.parameterValue ("layerA.sourceMode") >= 0.5f;
            g.setFont (type::popupLabel (10.5f));
            g.setColour (granular ? accent().withAlpha (0.85f) : design::colour::wellText.brighter (0.25f));
            g.drawText (granular ? "Playing: the card is in GRANULAR" : "Heard in GRANULAR (the card's mode, top right)", granularNote, juce::Justification::centredLeft, false);
        }
        void refresh() override
        {
            const bool granular = host.parameterValue ("layerA.sourceMode") >= 0.5f;
            if (granular != shownGranular)
            {
                shownGranular = granular;
                repaint (granularNote);
            }
        }

    private:
        juce::Rectangle<int> granularNote;
        bool shownGranular = false;
    };

    /** The workbench: its toolbar over one page. */
    class OscillatorWorkbench final : public CardWellEditor
    {
    public:
        explicit OscillatorWorkbench (InstrumentHost& h)
            : host (h), detailTabs ({ "QUICK", "DETAILED" }), sectionTabs ({ "OSC 1", "OSC 2", "PAIR", "MORE", "KEY DNA" })
        {
            setTitle ("Oscillator workbench");
            setWantsKeyboardFocus (false);
            waveKey.setComponentID ("wave");
            waveKey.setTooltip ("Back to the waveform (or click OSCILLATORS)");
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
            detailTabs.setComponentID ("detail");
            detailTabs.setTitle ("Quick or detailed");
            detailTabs.setTooltip ("QUICK: both oscillators' main controls. DETAILED: every setting, a section at a time");
            detailTabs.onChange = [this] (int i) {
                auto v = viewState();
                v.detailed = i == 1;
                setView (v);
            };
            addAndMakeVisible (detailTabs);
            sectionTabs.setComponentID ("sections");
            sectionTabs.setTitle ("Section");
            sectionTabs.onChange = [this] (int i) {
                auto v = viewState();
                v.section = i;
                setView (v);
            };
            addChildComponent (sectionTabs);
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
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (margin, 10);
            auto bar = r.removeFromTop (toolbarHeight);
            const bool narrow = bar.getWidth() < 700;
            waveKey.setBounds (bar.removeFromLeft (narrow ? 74 : 86));
            pinKey.setBounds (bar.removeFromRight (34));
            bar.removeFromRight (8);
            detailTabs.setBounds (bar.removeFromRight (narrow ? 150 : 184));
            bar.removeFromRight (12);
            bar.removeFromLeft (12);
            sectionTabs.setBounds (bar.removeFromLeft (std::min (narrow ? bar.getWidth() : 440, bar.getWidth())));
            r.removeFromTop (10);
            if (page != nullptr)
                page->setBounds (r);
        }

    protected:
        void viewStateChanged() override
        {
            const auto& v = viewState();
            pinKey.setToggleState (v.pinned, juce::dontSendNotification);
            detailTabs.setSelected (v.detailed ? 1 : 0);
            const int section = juce::jlimit (0, static_cast<int> (WorkbenchSection::count) - 1, v.section);
            sectionTabs.setSelected (section);
            sectionTabs.setVisible (v.detailed);
            const int wanted = v.detailed ? section : -1;
            if (page == nullptr || wanted != shownPage)
            {
                // Only the page shown exists: its controls are attached, no others.
                page.reset();
                shownPage = wanted;
                switch (wanted)
                {
                    case -1: page = std::make_unique<QuickPage> (host); break;
                    case 0: page = std::make_unique<OscillatorPage> (host, 1, true); break;
                    case 1: page = std::make_unique<OscillatorPage> (host, 2, true); break;
                    case 2: page = std::make_unique<PairPage> (host); break;
                    case 3: page = std::make_unique<MorePage> (host); break;
                    default: page = std::make_unique<KeyDnaPage> (host); break;
                }
                addAndMakeVisible (*page);
                resized();
            }
        }

    private:
        InstrumentHost& host;
        WellKey waveKey { "WAVE", WellKey::Glyph::wave };
        WellKey pinKey { "PIN", WellKey::Glyph::pin };
        ViewTabs detailTabs, sectionTabs;
        std::unique_ptr<Page> page;
        int shownPage = -2;
    };
}

std::unique_ptr<CardWellEditor> createOscillatorWorkbench (InstrumentHost& host)
{
    return std::make_unique<OscillatorWorkbench> (host);
}

} // namespace andor::analog
