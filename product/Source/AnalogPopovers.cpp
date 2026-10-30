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

        void toggle (const juce::String& id, const char* text, float weight, const char* tooltip)
        {
            auto t = std::make_unique<juce::ToggleButton> (text);
            t->setTooltip (tooltip);
            binding::tag (*t, id);
            attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (host.apvts(), id, *t));
            add (std::move (t), weight, toggleHeight);
        }

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
        static constexpr int knobHeight = 58, segmentHeight = 22, selectorHeight = 34, toggleHeight = 24;

        InstrumentHost& host;
        int panelWidth;
        juce::Colour accent;
        std::vector<Row> rows;
        std::vector<std::unique_ptr<juce::Component>> owned;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> attachments;
    };

    std::unique_ptr<MiniPanel> blendPopover (InstrumentHost& h)
    {
        using namespace params;
        auto p = std::make_unique<AnalogPanel> (h, "OSCILLATORS", 640, 0);
        auto* panel = p.get();
        // Each slot: its model (CLASSIC, DCO, WIDE) and ON, the model's own settings (only its
        // own are shown), then what every model shares (tuning, width, level, LFO depths).
        for (int slot = 1; slot <= 2; ++slot)
        {
            const bool first = slot == 1;
            const auto modelId = first ? osc1Model : osc2Model;
            p->row (first ? "OSCILLATOR 1" : "OSCILLATOR 2");
            p->segments (modelId, { "CLASSIC", "DCO", "WIDE" }, 3.0f,
                         "The oscillator's model: CLASSIC (the original), DCO (stable, after the Juno), WIDE (after the Jupiter-8, with SUPER SAW)");
            p->toggle (first ? osc1Enabled : osc2Enabled, "ON", 0.8f, "The oscillator sounds (off: silent; as a sync or Poly Mod source it keeps running)");
            p->row ({});
            p->showWhen ([panel, modelId] { return panel->choiceIndex (modelId) == 0; });
            if (first)
                p->segments (osc1Wave, { "SAW", "PULSE", "BOTH" }, 2.4f, "Oscillator 1's waveform: saw, pulse, or both");
            else
                p->selector (osc2Wave, "WAVE", 1.4f, "Oscillator 2's waveforms: saw, triangle and pulse, alone or together");
            p->row ({});
            p->showWhen ([panel, modelId] { return panel->choiceIndex (modelId) == 1; });
            p->toggle (first ? osc1DcoSaw : osc2DcoSaw, "SAW", 0.9f, "The DCO's sawtooth");
            p->toggle (first ? osc1DcoPulse : osc2DcoPulse, "PULSE", 0.9f, "The DCO's pulse (its width: WIDTH, swept by PWM)");
            p->knob (first ? osc1DcoSub : osc2DcoSub, "SUB");
            p->segments (first ? osc1DcoPwmSource : osc2DcoPwmSource, { "MAN", "LFO", "ENV" }, 2.0f,
                         "What sweeps the pulse width: the WIDTH knob alone, LFO 1, or the filter envelope");
            p->knob (first ? osc1DcoPwmDepth : osc2DcoPwmDepth, "PWM");
            p->row ({});
            p->showWhen ([panel, modelId] { return panel->choiceIndex (modelId) == 2; });
            p->selector (first ? osc1WideWave : osc2WideWave, "WAVE", 1.6f, "WIDE's wave: saw, pulse, triangle, square, or SUPER SAW (seven detuned saws, a modern addition)");
            p->knob (first ? osc1WideShape : osc2WideShape, "SHAPE");
            p->knob (first ? osc1WideSpread : osc2WideSpread, "SPREAD");
            p->row ({});
            p->knob (first ? osc1Octave : osc2Octave, "OCT");
            p->knob (first ? osc1Coarse : osc2Coarse, "SEMI");
            if (! first)
                p->knob (osc2Fine, "DETUNE");   // (the card's DETUNE)
            p->knob (first ? osc1Width : osc2Width, "WIDTH");
            p->knob (first ? osc1Level : osc2Level, "LEVEL");
            p->knob (first ? lfoPitch1 : lfoPitch2, "LFO PITCH");
            p->knob (first ? lfoWidth1 : lfoWidth2, "LFO PW");
        }
        p->row ("THE PAIR");
        p->toggle (sync, "SYNC", 1.0f, "Hard sync: oscillator 2 restarts oscillator 1 every cycle (tune oscillator 1 up for the classic sweep)");
        p->toggle (osc2KeyTrack, "KEYBOARD", 1.2f, "Oscillator 2 follows the keyboard (off: a fixed pitch, for drones and Poly Mod)");
        p->toggle (osc2LowFreq, "LO FREQ", 1.1f, "Oscillator 2 seven octaves down: a slow modulator for Poly Mod");
        p->knob (ringMix, "RING");
        p->row ("POLY MOD");
        p->knob (polyModEnv, "FILT ENV");
        p->knob (polyModOsc2, "OSC 2");
        p->toggle (polyModToPitch1, "PITCH 1", 1.1f, "Poly Mod moves oscillator 1's pitch (FM with oscillator 2, sweeps with the envelope)");
        p->toggle (polyModToWidth1, "WIDTH 1", 1.1f, "Poly Mod moves oscillator 1's pulse width");
        p->toggle (polyModToCutoff, "FILTER", 1.1f, "Poly Mod moves the filter's cutoff");
        p->row ("NOISE & VOICES");
        p->knob (noise, "NOISE");
        p->segments (noiseColor, { "WHITE", "JUNO" }, 1.6f, "The noise's colour: white, or the Juno's darker hiss");
        p->segments (polyphony, { "1", "2", "4", "8", "16" }, 2.6f, "How many voices play (with UNISON: how many stack on one note)");
        p->toggle (unison, "UNISON", 1.1f, "Every voice on one note, detuned: a thick mono sound");
        p->knob (unisonDetune, "UNI DETUNE");
        p->knob (spread, "SPREAD");
        p->row ("KEY DNA  (GRANULAR: each key its own grains)");
        p->toggle (keyDna, "ON", 0.8f, "KEY DNA: every key plays its own grains (where, how long, how dense, how wide), the same each time");
        p->knob (keyDnaAmount, "AMOUNT");
        p->knob (keyDnaIndividuality, "INDIVIDUAL");
        p->knob (keyDnaPosition, "POS");
        p->knob (keyDnaSize, "SIZE");
        p->knob (keyDnaDensity, "DENS");
        p->knob (keyDnaSpread, "SPREAD");
        p->knob (keyDnaTune, "TUNE");
        return p;
    }

    std::unique_ptr<MiniPanel> tonePopover (InstrumentHost& h)
    {
        using namespace params;
        auto p = std::make_unique<AnalogPanel> (h, "FILTER", 470, 2);
        auto* panel = p.get();
        p->row ("MODEL  (and the high-pass before it)");
        p->segments (filterModel, { "LADDER", "OTA" }, 2.0f, "The low-pass: the original four-pole LADDER, or the OTA family (after the Juno and Jupiter-8)");
        p->segments (otaHpf, { "OFF", "0", "1", "2", "3" }, 2.6f,
                     "The high-pass before the filter: off, or its positions (Juno-106: 0 bass boost, 1 flat, 2 and 3 cut; the others: 0 flat, 1-3 cut)");
        p->row ("OTA");
        p->showWhen ([panel] { return panel->choiceIndex (filterModel) == 1; });
        p->segments (otaCharacter, { "JUNO", "JUPITER" }, 2.0f, "The OTA's voicing (and the high-pass's curve): JUNO smoother, JUPITER brighter and more resonant");
        p->segments (otaSlope, { "24 dB", "12 dB" }, 2.0f, "The slope: four poles, or the Jupiter-8's two-pole tap (resonance stays)");
        p->row ("JUNO VARIANT");
        p->showWhen ([panel] { return panel->choiceIndex (otaCharacter) == 0 && (panel->choiceIndex (filterModel) == 1 || panel->choiceIndex (otaHpf) > 0); });
        p->segments (junoVariant, { "6", "60", "106" }, 2.0f, "Which Juno: its high-pass positions (106: a bass boost) and the OTA's calibration");
        p->row ({});
        p->knob (resonance, "RESO");
        p->knob (envDepth, "ENV");
        p->knob (keyTrack, "KEYS");
        p->knob (velocityToFilter, "VELO");
        p->row ("FILTER ENVELOPE");
        p->knob (filterAttack, "ATTACK");
        p->knob (filterDecay, "DECAY");
        p->knob (filterSustain, "SUSTAIN");
        p->knob (filterRelease, "RELEASE");
        p->row ("AMPLIFIER");
        p->segments (vcaMode, { "ENV", "GATE" }, 2.0f, "The amplitude: its envelope (AMP), or a gate that opens and closes with the key");
        p->toggle (envLink, "ENV LINK", 1.4f, "The amplitude follows the filter envelope's ADSR (one envelope for both, as on a Juno-106)");
        p->row ("FILTER DRIVE");
        p->knob (filterDrive, "DRIVE");
        p->knob (driveBias, "BIAS");
        p->knob (driveTrim, "TRIM");
        p->row ("LFO  (LFO 1 of the modulation panel; the mod wheel adds the rest)");
        p->knob (motion, "AMOUNT");
        p->knob (lfoCutoff, "CUTOFF");
        p->knob (lfoDelay, "DELAY");
        p->knob (lfoRise, "RISE");
        p->row ("CHORUS  (after the voices)");
        p->segments (chorusMode, { "OFF", "I", "II", "I+II" }, 3.0f, "A Juno-style bucket-brigade chorus on the whole sound");
        p->toggle (chorusNoise, "HISS", 1.0f, "The chorus' own faint hiss");
        p->row ("ANALOG");
        p->knob (analogAmount, "AMOUNT");
        p->segments (phaseMode, { "FREE", "RESET" }, 2.0f, "Oscillators free-running (each note starts anywhere) or restarted at every note");
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
            return status.isEmpty() ? juce::String ("READY: ORGANIC'S RECORDING INTO THE OSCILLATORS") : status.toUpperCase();
        });
        p->toggle (morphEnabled, "ON", 0.8f, "MORPH: the recording and the carrier become one sound (at 0 % the recording, at 100 % the carrier)");
        p->segments (morphMethod, { "IMPRINT", "FUSE", "WEAVE" }, 3.0f,
                     "IMPRINT: the recording's envelope on the carrier. FUSE: their partials blended. WEAVE: their partials interleaved");
        p->segments (morphCarrier, { "OSC 1", "OSC 2", "BOTH" }, 2.4f, "The analog side: oscillator 1, oscillator 2, or the pair as levelled");
        p->row ({});
        p->knob (morphPosition, "MORPH");
        p->knob (morphTexture, "TEXTURE");
        p->knob (morphAttack, "ATTACK");
        p->knob (morphFormants, "FORMANTS");
        p->knob (morphDetail, "DETAIL");
        p->row ("OUTPUT");
        p->segments (morphOutput, { "MORPH ONLY", "LAYERED" }, 2.6f, "MORPH ONLY: the morph replaces the recording and the carrier. LAYERED: it joins them");
        p->toggle (morphLevelMatch, "LEVEL MATCH", 1.4f, "Holds the loudness steady between the recording's and the carrier's");
        p->knob (organicRoot, "ROOT");
        return p;
    }
}

std::unique_ptr<MiniPanel> createOscillatorPopover (InstrumentHost& host)
{
    return blendPopover (host);
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
