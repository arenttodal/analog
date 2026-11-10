#include "AnalogPopovers.h"

#include "AnalogParameters.h"
#include "Binding.h"
#include "Design.h"
#include "MacroPopups.h"

#include <vector>

namespace andor::analog
{

using namespace osp::plugin;

namespace
{
    /**
        A macro popover: the compact card OSP's macro popovers use (small title, controls in
        rows), each row with a quiet caption as Advanced has. Only shared parts: MiniKnob,
        SegmentedControl, ValueSelector and the look-and-feel's toggle.
    */
    /** ANALOG's own parameters print their values themselves; ANDOR OSP's (SPACE, ECHO) are
        printed by their unit, with the popovers' own formats (as ANDOR OSP's popovers do). */
    MiniKnob::Formatter formatterFor (juce::RangedAudioParameter& p)
    {
        if (p.getParameterID().startsWith ("analog."))
            return [&p] (double v) { return p.getText (p.convertTo0to1 (static_cast<float> (v)), 0); };
        const auto label = p.getLabel();
        if (label == "%")
            return [] (double v) { return format::percent (v) + " %"; };
        if (label == "ms")
            return [] (double v) { return format::milliseconds (v); };
        if (label == "Hz")
            return [] (double v) { return format::hertz (v); };
        if (label == "s")
            return [] (double v) { return (v < 10.0 ? juce::String (v, 1) : juce::String (juce::roundToInt (v))) + " s"; };
        return [&p] (double v) { return p.getText (p.convertTo0to1 (static_cast<float> (v)), 0); };
    }

    /** A popover's pages (an interface choice, never a parameter): text tabs, the chosen one in
        the accent over a short rule, as the EQ's EQ | DAMPEN. Arrow keys move them. */
    class PageTabs final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        PageTabs (juce::StringArray names, juce::Colour accentColour, int& pageRef)
            : pages (std::move (names)), accent (accentColour), page (pageRef)
        {
            setWantsKeyboardFocus (true);
            setTitle ("Pages");
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
        std::function<void()> onChange;
        void select (int index)
        {
            index = juce::jlimit (0, pages.size() - 1, index);
            if (index == page)
                return;
            page = index;
            repaint();
            if (onChange != nullptr)
                onChange();
        }
        int selected() const noexcept { return page; }
        void paint (juce::Graphics& g) override
        {
            const auto w = static_cast<float> (getWidth()) / static_cast<float> (pages.size());
            g.setFont (fonts::label (11.0f));
            for (int i = 0; i < pages.size(); ++i)
            {
                const auto r = juce::Rectangle<float> (static_cast<float> (i) * w, 0.0f, w, static_cast<float> (getHeight()));
                const bool on = i == page;
                g.setColour (on ? palette::text : palette::textDim);
                g.drawText (pages[i], r.withTrimmedBottom (4.0f), juce::Justification::centred, false);
                if (on)
                {
                    g.setColour (accent);
                    g.fillRoundedRectangle (r.withSizeKeepingCentre (std::min (w - 16.0f, 54.0f), 2.0f).withY (r.getBottom() - 3.0f), 1.0f);
                }
            }
            g.setColour (palette::hairline);
            g.fillRect (0.0f, static_cast<float> (getHeight()) - 1.0f, static_cast<float> (getWidth()), 1.0f);
            if (hasKeyboardFocus (false))
            {
                g.setColour (accent.withAlpha (0.5f));
                g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 1.0f);
            }
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            select (juce::jlimit (0, pages.size() - 1, static_cast<int> (static_cast<float> (e.x) / (static_cast<float> (getWidth()) / static_cast<float> (pages.size())))));
        }
        bool keyPressed (const juce::KeyPress& k) override
        {
            if (k == juce::KeyPress::leftKey)
                select (page - 1);
            else if (k == juce::KeyPress::rightKey)
                select (page + 1);
            else
                return false;
            return true;
        }

    private:
        juce::StringArray pages;
        juce::Colour accent;
        int& page;
    };

    class AnalogPanel final : public MiniPanel
    {
    public:
        AnalogPanel (InstrumentHost& h, juce::String name, int width, int macro)
            : MiniPanel (std::move (name)), host (h), panelWidth (width), accent (design::colour::macro (macro))
        {
        }

        bool compact() const override { return true; }

        void row (juce::String caption)
        {
            Row r;
            r.caption = std::move (caption);
            rows.push_back (std::move (r));
        }

        /** The last row's caption follows `text` (a state that changes while it is open). */
        void captionFrom (std::function<juce::String()> text)
        {
            rows.back().captionText = std::move (text);
            rows.back().caption = rows.back().captionText();
        }

        /** The last row shows only while `predicate` holds (a model's own settings): the card
            follows the parameters, its size with it. */
        void showWhen (std::function<bool()> predicate)
        {
            rows.back().shown = predicate;
            applyVisibility();
        }

        /** A choice parameter's index now (for showWhen). */
        int choiceIndex (const juce::String& id) const
        {
            auto* parameter = host.apvts().getParameter (id);
            return parameter != nullptr ? juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue())) : 0;
        }

        void refreshContent() override
        {
            for (auto& r : rows)
                if (r.captionText != nullptr)
                    if (const auto text = r.captionText(); text != r.caption)
                    {
                        r.caption = text;
                        repaint();
                    }
            if (applyVisibility())
            {
                resized();
                if (onSizeChanged != nullptr)
                    onSizeChanged();
            }
        }

        void knob (const juce::String& id, const char* caption, float weight = 1.0f)
        {
            auto* parameter = host.apvts().getParameter (id);
            jassert (parameter != nullptr);
            auto k = std::make_unique<MiniKnob> (host.apvts(), id, caption, formatterFor (*parameter));
            k->setArcColour (accent);
            add (std::move (k), weight, knobHeight);
        }

        void segments (const juce::String& id, juce::StringArray items, float weight, const char* tooltip)
        {
            auto s = std::make_unique<SegmentedControl> (*host.apvts().getParameter (id), std::move (items));
            s->setAccent (accent);
            s->setTooltip (tooltip);
            binding::tag (*s, id);
            add (std::move (s), weight, segmentHeight);
        }

        void selector (const juce::String& id, const char* caption, float weight, const char* tooltip)
        {
            auto s = std::make_unique<ValueSelector> (*host.apvts().getParameter (id), caption);
            s->setAccent (accent);
            s->setTooltip (tooltip);
            binding::tag (*s, id);
            add (std::move (s), weight, selectorHeight);
        }

        /** The panel's pages: a tab row; rows after it shown on their page (onPage). */
        void tabs (juce::StringArray names, int& page)
        {
            row ({});
            auto t = std::make_unique<PageTabs> (std::move (names), accent, page);
            t->setTooltip ("The filter, the voice, what the two oscillators do together");
            t->onChange = [this] { refreshContent(); };
            pageTabs = t.get();
            add (std::move (t), 1.0f, tabHeight);
        }
        /** The last row belongs to `index`'s page (and, if given, shows only while `also` holds). */
        void onPage (int index, int& page, std::function<bool()> also = {})
        {
            auto* p = &page;
            showWhen ([p, index, also] { return *p == index && (also == nullptr || also()); });
        }
        PageTabs* tabsComponent() const noexcept { return pageTabs; }

        void toggle (const juce::String& id, const char* text, float weight, const char* tooltip)
        {
            auto t = std::make_unique<juce::ToggleButton> (text);
            t->setTooltip (tooltip);
            binding::tag (*t, id);
            attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (host.apvts(), id, *t));
            add (std::move (t), weight, toggleHeight);
        }

        /** One of the product's own controls in the last row (`h` high). */
        void custom (std::unique_ptr<juce::Component> c, float weight, int h) { add (std::move (c), weight, h); }
        juce::Colour accentColour() const noexcept { return accent; }

        juce::Point<int> cardSize() const override
        {
            int h = headerTop + bottom;
            for (const auto& r : rows)
                if (r.visible)
                    h += (r.caption.isNotEmpty() ? captionHeight : 0) + height (r) + rowGap;
            return { panelWidth, h - rowGap };
        }

        void paint (juce::Graphics& g) override
        {
            MiniPanel::paint (g);
            g.setColour (palette::textDim);
            g.setFont (fonts::label (9.5f));
            for (const auto& r : rows)
                if (r.visible && r.caption.isNotEmpty())
                    g.drawText (r.caption, r.captionArea, juce::Justification::centredLeft, false);
        }

    private:
        struct Cell
        {
            juce::Component* component = nullptr;
            float weight = 1.0f;
            int height = 0;
        };
        struct Row
        {
            juce::String caption;
            std::vector<Cell> cells;
            juce::Rectangle<int> captionArea;
            std::function<bool()> shown;   ///< null: always
            std::function<juce::String()> captionText;   ///< null: a fixed caption
            bool visible = true;
        };

        /** Shows each row as its predicate says; true when any changed. */
        bool applyVisibility()
        {
            bool changed = false;
            for (auto& r : rows)
            {
                const bool v = r.shown == nullptr || r.shown();
                if (v != r.visible)
                {
                    r.visible = v;
                    changed = true;
                }
                for (auto& c : r.cells)
                    c.component->setVisible (v);
            }
            return changed;
        }

        void add (std::unique_ptr<juce::Component> c, float weight, int h)
        {
            if (rows.empty())
                row ({});
            addAndMakeVisible (*c);
            c->setVisible (rows.back().visible);   // (a row hidden by its model starts hidden)
            rows.back().cells.push_back ({ c.get(), weight, h });
            owned.push_back (std::move (c));
        }

        static int height (const Row& r)
        {
            int h = 0;
            for (const auto& c : r.cells)
                h = std::max (h, c.height);
            return h;
        }

        void layoutContent (juce::Rectangle<int> area) override
        {
            for (auto& r : rows)
            {
                if (! r.visible)
                    continue;
                if (r.caption.isNotEmpty())
                    r.captionArea = area.removeFromTop (captionHeight).withTrimmedLeft (2);
                auto line = area.removeFromTop (height (r));
                area.removeFromTop (rowGap);
                float total = 0.0f;
                for (const auto& c : r.cells)
                    total += c.weight;
                const float unitWidth = static_cast<float> (line.getWidth()) / std::max (total, 1.0f);
                float x = static_cast<float> (line.getX());
                for (const auto& c : r.cells)
                {
                    const float w = c.weight * unitWidth;
                    const auto cell = juce::Rectangle<float> (x, static_cast<float> (line.getY()), w, static_cast<float> (line.getHeight())).toNearestInt();
                    // Knobs fill their cell (at most 80 wide, as OSP's popovers); keys sit centred.
                    if (c.height == knobHeight)
                        c.component->setBounds (cell.withSizeKeepingCentre (std::min (cell.getWidth(), 80), knobHeight));
                    else
                        c.component->setBounds (cell.reduced (3, 0).withSizeKeepingCentre (cell.getWidth() - 6, c.height));
                    x += w;
                }
            }
        }

        static constexpr int headerTop = 32, bottom = 10, captionHeight = 14, rowGap = 6;
        // A captioned selector is 34 high, as in ANDOR OSP's popovers (caption over the value key).
        static constexpr int knobHeight = 58, segmentHeight = 22, selectorHeight = 34, toggleHeight = 24, tabHeight = 26;

        InstrumentHost& host;
        int panelWidth;
        juce::Colour accent;
        std::vector<Row> rows;
        std::vector<std::unique_ptr<juce::Component>> owned;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> attachments;
        PageTabs* pageTabs = nullptr;
    };

    /** The page the FILTER popover opens on (interface only: the last one looked at). */
    int filterPage = 0;

    /** FILTER: the filter first and simplest; the voice and the oscillator pair on their own
        pages. FILTER: its type, its four main knobs, its envelope, its drive. VOICE: the
        amplifier, the voices, LFO 1's depths, the chorus, the analog character. PAIR: what the
        two oscillators do together (sync, ring, blend, Poly Mod, the noise mixer). */
    std::unique_ptr<MiniPanel> tonePopover (InstrumentHost& h)
    {
        using namespace params;
        auto p = std::make_unique<AnalogPanel> (h, "FILTER", 470, 2);
        auto* panel = p.get();
        auto& page = filterPage;
        p->tabs ({ "FILTER", "VOICE", "PAIR" }, page);

        // FILTER: what it is, then what it does.
        p->row ("TYPE");
        p->onPage (0, page);
        p->segments (filterModel, { "LADDER", "OTA" }, 2.0f, "The low-pass: the original four-pole LADDER, or the OTA family (after the Juno and Jupiter-8)");
        p->selector (otaHpf, "HIGH-PASS", 1.2f, "The high-pass before the filter: off, or its positions (Juno-106: 0 bass boost, 1 flat, 2 and 3 cut; the others: 0 flat, 1-3 cut)");
        p->row ("OTA");
        p->onPage (0, page, [panel] { return panel->choiceIndex (filterModel) == 1; });
        p->segments (otaCharacter, { "JUNO", "JUPITER" }, 2.0f, "The OTA's voicing (and the high-pass's curve): JUNO smoother, JUPITER brighter and more resonant");
        p->segments (otaSlope, { "24 dB", "12 dB" }, 2.0f, "The slope: four poles, or the Jupiter-8's two-pole tap (resonance stays)");
        p->selector (junoVariant, "JUNO", 1.0f, "Which Juno: its high-pass positions (106: a bass boost) and the OTA's calibration");
        p->row ({});
        p->onPage (0, page);
        p->knob (resonance, "RESO");
        p->knob (envDepth, "ENV");
        p->knob (keyTrack, "KEYS");
        p->knob (filterDrive, "DRIVE");
        p->row ("ENVELOPE");
        p->captionFrom ([panel] { return panel->choiceIndex (envLink) == 1 ? juce::String ("ENVELOPE  (ENV LINK: the AMP envelope shapes the filter)") : juce::String ("ENVELOPE"); });
        p->onPage (0, page);
        p->knob (filterAttack, "ATTACK");
        p->knob (filterDecay, "DECAY");
        p->knob (filterSustain, "SUSTAIN");
        p->knob (filterRelease, "RELEASE");
        p->knob (velocityToFilter, "VELO");

        // VOICE: how each note sounds and moves.
        p->row ("AMPLIFIER");
        p->onPage (1, page);
        p->segments (vcaMode, { "ENV", "GATE" }, 2.0f, "The amplitude: its envelope (AMP), or a gate that opens and closes with the key (editing the AMP envelope returns it to ENV)");
        p->toggle (envLink, "ENV LINK", 1.4f, "One envelope for both, as on a Juno-106: the AMP envelope shapes the filter too (the filter's envelope rests)");
        p->row ("VOICES");
        p->onPage (1, page);
        p->knob (polyphony, "VOICES");
        p->toggle (unison, "UNISON", 1.2f, "Every voice on one note, detuned: a thick mono sound");
        p->knob (unisonDetune, "DETUNE");
        p->knob (spread, "SPREAD");
        p->row ("LFO 1  (its shape and rate: the modulation panel)");
        p->onPage (1, page);
        p->knob (motion, "AMOUNT");
        p->toggle (lfoWheel, "WHEEL", 1.2f, "The mod wheel brings in the rest of these depths (the classic link). Off: the wheel moves only what you route it to");
        p->knob (lfoCutoff, "CUTOFF");
        p->knob (lfoPitch1, "PITCH 1");
        p->knob (lfoPitch2, "PITCH 2");
        p->knob (lfoDelay, "DELAY");
        p->knob (lfoRise, "RISE");
        p->row ("CHORUS");
        p->onPage (1, page);
        p->segments (chorusMode, { "OFF", "I", "II", "I+II" }, 3.0f, "A Juno-style bucket-brigade chorus on the whole sound");
        p->toggle (chorusNoise, "HISS", 1.0f, "The chorus' own faint hiss");
        p->row ("CHARACTER");
        p->onPage (1, page);
        p->knob (analogAmount, "ANALOG");
        p->segments (phaseMode, { "FREE", "RESET" }, 1.6f, "Oscillators free-running (each note starts anywhere) or restarted at every note");
        p->knob (driveBias, "BIAS");
        p->knob (driveTrim, "TRIM");

        // PAIR: what the two oscillators do together (each one's own sound is in its card).
        p->row ("OSC 1 + OSC 2");
        p->onPage (2, page);
        p->toggle (sync, "SYNC", 1.0f, "Hard sync: oscillator 2 restarts oscillator 1 every cycle (tune oscillator 1 up for the classic sweep)");
        p->knob (ringMix, "RING");
        p->knob (blend, "BLEND");
        p->row ("POLY MOD  (filter envelope and OSC 2, to:)");
        p->onPage (2, page);
        p->knob (polyModEnv, "FILT ENV");
        p->knob (polyModOsc2, "OSC 2");
        p->toggle (polyModToPitch1, "PITCH 1", 1.0f, "Poly Mod moves oscillator 1's pitch (FM with oscillator 2, sweeps with the envelope)");
        p->toggle (polyModToWidth1, "WIDTH 1", 1.0f, "Poly Mod moves oscillator 1's pulse width");
        p->toggle (polyModToCutoff, "FILTER", 1.0f, "Poly Mod moves the filter's cutoff");
        p->row ("NOISE  (mixed with the oscillators)");
        p->onPage (2, page);
        p->knob (noise, "LEVEL");
        p->segments (noiseColor, { "WHITE", "JUNO" }, 2.0f, "The noise's colour: white, or the Juno's darker hiss");
        return p;
    }
}

namespace
{
    std::unique_ptr<MiniPanel> morphPopover (InstrumentHost& h)
    {
        using namespace params;
        auto p = std::make_unique<AnalogPanel> (h, "MORPH", 520, 0);
        auto* host = &h;
        // Its state, as it is: ready, being analysed, or why it cannot be (then the sources
        // play as they are: nothing pretends to morph).
        p->row ({});
        p->captionFrom ([host] {
            const auto status = host->engineStatus ("morph");
            return status.isEmpty() ? juce::String ("LIVE: THE OSCILLATORS AND CARD C, FUSED") : status.toUpperCase();
        });
        p->toggle (morphEnabled, "ON", 0.8f, "MORPH: the oscillators and card C become one sound (at 0 % the oscillators, at 100 % card C, between them a fusion)");
        p->segments (morphMethod, { "IMPRINT", "FUSE", "WEAVE" }, 3.0f,
                     "IMPRINT: card C's envelope on the oscillators. FUSE: their partials and pitch blended. WEAVE: their partials interleaved");
        p->segments (morphCarrier, { "OSC 1", "OSC 2", "BOTH" }, 2.4f, "The analog side: oscillator 1, oscillator 2, or the pair as levelled");
        p->row ({});
        p->knob (morphPosition, "MORPH");
        p->knob (morphTexture, "TEXTURE");
        p->knob (morphAttack, "ATTACK");
        p->knob (morphFormants, "FORMANTS");
        p->knob (morphDetail, "DETAIL");
        p->row ("OUTPUT");
        p->segments (morphOutput, { "MORPH ONLY", "LAYERED" }, 2.6f, "MORPH ONLY: the morph replaces the oscillators and card C. LAYERED: it joins them");
        p->toggle (morphLevelMatch, "LEVEL MATCH", 1.4f, "Holds the loudness steady between the oscillators' and card C's");
        p->knob (organicRoot, "ROOT");
        return p;
    }
}

namespace
{
    /**
        SUB's RANGE: a small keybed of the played keys (C1 .. C8) with the range lit and two
        handles, its low and high keys. A press takes the nearer handle and drags it; arrow
        keys move the low key (with Shift, the high one). The two never cross.
    */
    class RangeKeybed final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        static constexpr int firstKey = 24, lastKey = 108;   // C1 .. C8

        RangeKeybed (InstrumentHost& h, juce::Colour accentColour)
            : host (h), accent (accentColour),
              low (*h.apvts().getParameter (params::subLowKey), [this] (float) { repaint(); }, &h.undo()),
              high (*h.apvts().getParameter (params::subHighKey), [this] (float) { repaint(); }, &h.undo())
        {
            setTitle ("Sub range");
            setTooltip ("The keys SUB answers: drag either end (arrow keys: the low end; with Shift, the high end)");
            setWantsKeyboardFocus (true);
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            binding::tag (*this, params::subLowKey);
            low.sendInitialUpdate();
            high.sendInitialUpdate();
        }

        int lowKey() const { return juce::roundToInt (host.parameterValue (params::subLowKey)); }
        int highKey() const { return juce::roundToInt (host.parameterValue (params::subHighKey)); }

        void paint (juce::Graphics& g) override
        {
            const auto bed = keybed();
            const int lo = lowKey(), hi = highKey();
            // The white keys, those in the range lit; then the black keys over them.
            const auto whites = whiteCount();
            const float w = bed.getWidth() / static_cast<float> (whites);
            int index = 0;
            for (int k = firstKey; k <= lastKey; ++k)
            {
                if (isBlack (k))
                    continue;
                const auto r = juce::Rectangle<float> (bed.getX() + static_cast<float> (index++) * w, bed.getY(), w, bed.getHeight()).reduced (0.5f, 0.0f);
                const bool in = k >= lo && k <= hi;
                g.setColour (in ? accent.interpolatedWith (juce::Colours::white, 0.72f) : juce::Colour (0xfff7f4ee));
                g.fillRoundedRectangle (r, 1.5f);
                g.setColour (palette::hairline);
                g.drawRoundedRectangle (r, 1.5f, 0.6f);
            }
            for (int k = firstKey; k <= lastKey; ++k)
                if (isBlack (k))
                {
                    const auto r = keyArea (k);
                    const bool in = k >= lo && k <= hi;
                    g.setColour (in ? accent.darker (0.55f) : juce::Colour (0xff3a3835));
                    g.fillRoundedRectangle (r, 1.0f);
                }
            // C labels under the bed, quietly.
            g.setFont (fonts::label (8.5f));
            g.setColour (palette::textDim);
            for (int k = firstKey; k <= lastKey; k += 12)
                g.drawText ("C" + juce::String (k / 12 - 1), keyArea (k).withY (bed.getBottom() + 1.0f).withHeight (10.0f).expanded (6.0f, 0.0f),
                            juce::Justification::centred, false);
            // The two handles: a bar over each end of the range, a cap above.
            for (const int which : { 0, 1 })
            {
                const int key = std::clamp (which == 0 ? lo : hi, firstKey, lastKey);
                const auto k = keyArea (key);
                const float x = which == 0 ? k.getX() : k.getRight();
                const bool active = dragging == which || (dragging < 0 && hovered == which);
                g.setColour (accent.withAlpha (active ? 1.0f : 0.85f));
                g.fillRoundedRectangle (juce::Rectangle<float> (x - 1.25f, bed.getY() - 3.0f, 2.5f, bed.getHeight() + 3.0f), 1.0f);
                g.fillRoundedRectangle (juce::Rectangle<float> (which == 0 ? x - 1.25f : x - 7.25f, bed.getY() - 7.0f, 8.5f, 5.0f), 1.5f);
            }
            if (hasKeyboardFocus (false))
            {
                g.setColour (accent.withAlpha (0.5f));
                g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 3.0f, 1.0f);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const int now = nearerHandle (e.position.x);
            if (now != hovered)
            {
                hovered = now;
                repaint();
            }
        }
        void mouseExit (const juce::MouseEvent&) override
        {
            hovered = -1;
            repaint();
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            dragging = nearerHandle (e.position.x);
            (dragging == 0 ? low : high).beginGesture();
            dragTo (e.position.x);
        }
        void mouseDrag (const juce::MouseEvent& e) override { dragTo (e.position.x); }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragging >= 0)
                (dragging == 0 ? low : high).endGesture();
            dragging = -1;
            repaint();
        }
        bool keyPressed (const juce::KeyPress& k) override
        {
            const int step = k == juce::KeyPress::leftKey ? -1 : k == juce::KeyPress::rightKey ? 1 : 0;
            if (step == 0)
                return false;
            if (k.getModifiers().isShiftDown())
                high.setValueAsCompleteGesture (static_cast<float> (std::clamp (highKey() + step, lowKey(), 127)));
            else
                low.setValueAsCompleteGesture (static_cast<float> (std::clamp (lowKey() + step, 0, highKey())));
            return true;
        }

        /** Tests: where a key is drawn (local coordinates). */
        juce::Rectangle<float> keyArea (int key) const
        {
            const auto bed = keybed();
            const float w = bed.getWidth() / static_cast<float> (whiteCount());
            int whitesBefore = 0;
            for (int k = firstKey; k < key; ++k)
                whitesBefore += isBlack (k) ? 0 : 1;
            if (! isBlack (key))
                return { bed.getX() + static_cast<float> (whitesBefore) * w, bed.getY(), w, bed.getHeight() };
            return { bed.getX() + static_cast<float> (whitesBefore) * w - 0.32f * w, bed.getY(), 0.64f * w, 0.6f * bed.getHeight() };
        }

    private:
        static bool isBlack (int key) noexcept
        {
            const int n = key % 12;
            return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
        }
        static int whiteCount() noexcept
        {
            int n = 0;
            for (int k = firstKey; k <= lastKey; ++k)
                n += isBlack (k) ? 0 : 1;
            return n;
        }
        juce::Rectangle<float> keybed() const { return getLocalBounds().toFloat().reduced (4.0f, 0.0f).withTrimmedTop (9.0f).withTrimmedBottom (12.0f); }

        /** The key under `x` (a black key where one is drawn, else the white key's column). */
        int keyAt (float x) const
        {
            for (int k = firstKey; k <= lastKey; ++k)
                if (isBlack (k) && x >= keyArea (k).getX() && x < keyArea (k).getRight())
                    return k;
            for (int k = firstKey; k <= lastKey; ++k)
                if (! isBlack (k) && x < keyArea (k).getRight())
                    return k;
            return lastKey;
        }
        int nearerHandle (float x) const
        {
            const auto l = keyArea (std::clamp (lowKey(), firstKey, lastKey)).getX();
            const auto h = keyArea (std::clamp (highKey(), firstKey, lastKey)).getRight();
            return std::abs (x - l) <= std::abs (x - h) ? 0 : 1;
        }
        void dragTo (float x)
        {
            const int key = keyAt (x);
            if (dragging == 0)
                low.setValueAsPartOfGesture (static_cast<float> (std::min (key, highKey())));
            else if (dragging == 1)
                high.setValueAsPartOfGesture (static_cast<float> (std::max (key, lowKey())));
        }

        InstrumentHost& host;
        juce::Colour accent;
        juce::ParameterAttachment low, high;
        int dragging = -1, hovered = -1;
    };
}

std::unique_ptr<MiniPanel> createSubPopover (InstrumentHost& host)
{
    // SUB (Cinematic V6 gate 2): what it plays (WAVE, OCT), how it sounds (GRIT, LEVEL), which
    // keys (RANGE) and how the high ones speak (FLAT / TAPER).
    using namespace params;
    auto p = std::make_unique<AnalogPanel> (host, "SUB", 360, 2);
    auto* panel = p.get();
    p->row ("WAVE");
    p->segments (subWave, { "SINE", "TRI", "SQUARE", "SAW" }, 1.0f, "SUB's wave (band-limited)");
    p->row ({});
    p->segments (subOctave, { "-1", "-2" }, 1.4f, "One or two octaves below the key");
    p->knob (subGrit, "GRIT");
    p->knob (subLevel, "LEVEL");
    p->row ("RANGE");
    p->captionFrom ([&host] {
        auto name = [&host] (const juce::String& id) { return juce::MidiMessage::getMidiNoteName (juce::roundToInt (host.parameterValue (id)), true, true, 4); };
        return "RANGE  " + name (subLowKey) + " - " + name (subHighKey);
    });
    p->custom (std::make_unique<RangeKeybed> (host, panel->accentColour()), 1.0f, 48);
    p->row ({});
    p->segments (subResponse, { "FLAT", "TAPER" }, 1.0f, "FLAT: every key the same. TAPER: quieter above its start (the low end stays full)");
    p->row ("TAPER");
    p->showWhen ([panel] { return panel->choiceIndex (subResponse) == 1; });
    p->knob (subTaperStart, "FROM");
    p->knob (subTaperSlope, "dB / OCT");
    return p;
}

std::unique_ptr<MiniPanel> createMacroPopover (int macro, InstrumentHost& host)
{
    switch (macro)
    {
        case 0: return morphPopover (host);
        case 1: return createMacroPopup (MacroPopup::drive, host);      // ANDOR OSP's own
        case 2: return tonePopover (host);
        case 3: return createMacroPopup (MacroPopup::movement, host);
        case 4: return createMacroPopup (MacroPopup::space, host);
        case 5: return createMacroPopup (MacroPopup::echo, host);
        default: return nullptr;
    }
}

} // namespace andor::analog
