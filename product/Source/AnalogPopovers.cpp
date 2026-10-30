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

        void row (juce::String caption) { rows.push_back ({ std::move (caption), {}, {} }); }

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
            add (std::move (s), weight, segmentHeight);
        }

        void selector (const juce::String& id, const char* caption, float weight, const char* tooltip)
        {
            auto s = std::make_unique<ValueSelector> (*host.apvts().getParameter (id), caption);
            s->setAccent (accent);
            s->setTooltip (tooltip);
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
                h += (r.caption.isNotEmpty() ? captionHeight : 0) + height (r) + rowGap;
            return { panelWidth, h - rowGap };
        }

        void paint (juce::Graphics& g) override
        {
            MiniPanel::paint (g);
            g.setColour (palette::textDim);
            g.setFont (fonts::label (9.5f));
            for (const auto& r : rows)
                if (r.caption.isNotEmpty())
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
        };

        void add (std::unique_ptr<juce::Component> c, float weight, int h)
        {
            if (rows.empty())
                row ({});
            addAndMakeVisible (*c);
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
        p->row ("OSCILLATOR 1");
        p->segments (osc1Wave, { "SAW", "PULSE", "BOTH" }, 2.4f, "Oscillator 1's waveform: saw, pulse, or both");
        p->knob (osc1Octave, "OCT");
        p->knob (osc1Coarse, "SEMI");
        p->knob (osc1Width, "WIDTH");
        p->knob (osc1Level, "LEVEL");
        p->knob (lfoPitch1, "LFO PITCH");
        p->knob (lfoWidth1, "LFO PW");
        p->row ("OSCILLATOR 2");
        p->selector (osc2Wave, "WAVE", 1.4f, "Oscillator 2's waveforms: saw, triangle and pulse, alone or together");
        p->knob (osc2Octave, "OCT");
        p->knob (osc2Coarse, "SEMI");
        p->knob (osc2Fine, "DETUNE");   // (the card's DETUNE)
        p->knob (osc2Width, "WIDTH");
        p->knob (osc2Level, "LEVEL");
        p->knob (lfoPitch2, "LFO PITCH");
        p->knob (lfoWidth2, "LFO PW");
        p->row ({});
        p->toggle (sync, "SYNC", 1.0f, "Hard sync: oscillator 2 restarts oscillator 1 every cycle (tune oscillator 1 up for the classic sweep)");
        p->toggle (osc2KeyTrack, "KEYBOARD", 1.2f, "Oscillator 2 follows the keyboard (off: a fixed pitch, for drones and Poly Mod)");
        p->toggle (osc2LowFreq, "LO FREQ", 1.1f, "Oscillator 2 seven octaves down: a slow modulator for Poly Mod");
        p->row ("POLY MOD");
        p->knob (polyModEnv, "FILT ENV");
        p->knob (polyModOsc2, "OSC 2");
        p->toggle (polyModToPitch1, "PITCH 1", 1.1f, "Poly Mod moves oscillator 1's pitch (FM with oscillator 2, sweeps with the envelope)");
        p->toggle (polyModToWidth1, "WIDTH 1", 1.1f, "Poly Mod moves oscillator 1's pulse width");
        p->toggle (polyModToCutoff, "FILTER", 1.1f, "Poly Mod moves the filter's cutoff");
        p->row ("NOISE & VOICES");
        p->knob (noise, "NOISE");
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
        auto p = std::make_unique<AnalogPanel> (h, "FILTER", 400, 2);
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
        p->row ("FILTER DRIVE");
        p->knob (filterDrive, "DRIVE");
        p->knob (driveBias, "BIAS");
        p->knob (driveTrim, "TRIM");
        p->row ("LFO  (LFO 1 of the modulation panel; the mod wheel adds the rest)");
        p->knob (motion, "AMOUNT");
        p->knob (lfoCutoff, "CUTOFF");
        p->row ("ANALOG");
        p->knob (analogAmount, "AMOUNT");
        p->segments (phaseMode, { "FREE", "RESET" }, 2.0f, "Oscillators free-running (each note starts anywhere) or restarted at every note");
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
        case 0: return nullptr;   // (not assigned yet: shown disabled)
        case 1: return createMacroPopup (MacroPopup::drive, host);      // ANDOR OSP's own
        case 2: return tonePopover (host);
        case 3: return createMacroPopup (MacroPopup::movement, host);
        case 4: return createMacroPopup (MacroPopup::space, host);
        case 5: return createMacroPopup (MacroPopup::echo, host);
        default: return nullptr;
    }
}

} // namespace andor::analog
