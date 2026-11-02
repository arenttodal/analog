#include "AnalogWellOverlays.h"

#include "AnalogParameters.h"
#include "SampleSourceUI.h"
#include "ShellProcessor.h"
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
    const juce::Colour cellFill { 0xff2b3031 };
    const juce::Colour cellEdge { 0xff3e4446 };
    const juce::Colour cellEdgeHot { 0xff59605f };
    const juce::Colour cream { 0xffeee8dd };

    /** A switch on the well (a parameter's, through its attachment): outlined text, lit in the
        card's accent with a small light at its left. */
    class WellSwitch final : public juce::Button
    {
    public:
        WellSwitch (const juce::String& name, juce::Colour accentColour) : juce::Button (name), accent (accentColour)
        {
            setTitle (name);
            setClickingTogglesState (true);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setWantsKeyboardFocus (true);
        }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto r = getLocalBounds().toFloat().reduced (1.0f);
            const float radius = std::min (6.0f, 0.24f * r.getHeight());
            const bool on = getToggleState();
            g.setColour (cellFill.withAlpha (over || down ? 1.0f : 0.9f));
            g.fillRoundedRectangle (r, radius);
            if (on)
            {
                g.setColour (accent.withAlpha (0.16f));
                g.fillRoundedRectangle (r, radius);
            }
            g.setColour (hasKeyboardFocus (false) ? accent : (on ? accent.withAlpha (0.85f) : (over ? cellEdgeHot : cellEdge)));
            g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
            auto content = r.reduced (7.0f, 0.0f);
            design::draw::led (g, { content.getX() + 3.0f, r.getCentreY() }, 5.0f, on ? accent : juce::Colour (0xff4a5050), on ? 0.6f : 0.0f);
            content.removeFromLeft (9.0f);
            g.setColour (on ? accent.interpolatedWith (juce::Colour (0xfff2eee6), 0.35f) : cream.withAlpha (over ? 0.95f : 0.72f));
            g.setFont (type::popupLabel (juce::jlimit (10.0f, 12.0f, 0.42f * r.getHeight())));
            g.drawText (getButtonText(), content, juce::Justification::centred, false);
        }

    private:
        juce::Colour accent;
    };

    /** Card C's type, short enough for its dropdown in a narrow well (its list says it in full). */
    juce::String shortTypeText (InstrumentHost& host)
    {
        return juce::roundToInt (host.parameterValue (params::hybridType)) == static_cast<int> (HybridType::instrumentPatch) ? juce::String ("Instrument") : juce::String();
    }

    /** An action key on the well (SOUNDS, IMPORT): graphite, cream text, the accent outline on
        hover or focus, as the well's other keys. */
    class WellAction final : public juce::Button
    {
    public:
        WellAction (const juce::String& name, juce::Colour accentColour) : juce::Button (name), accent (accentColour)
        {
            setTitle (name);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setWantsKeyboardFocus (true);
        }
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto r = getLocalBounds().toFloat().reduced (1.0f);
            const float radius = std::min (6.0f, 0.24f * r.getHeight());
            g.setColour (cellFill.withAlpha (over || down ? 1.0f : 0.92f));
            g.fillRoundedRectangle (r, radius);
            g.setColour (hasKeyboardFocus (false) || over ? accent : cellEdge);
            g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
            g.setColour (cream.withAlpha (over ? 1.0f : 0.85f));
            g.setFont (type::popupLabel (juce::jlimit (10.0f, 12.0f, 0.42f * r.getHeight())));
            g.drawText (getButtonText(), r.reduced (6.0f, 0.0f), juce::Justification::centred, false);
        }

    private:
        juce::Colour accent;
    };

    /** Whether card C shows a stand-in (empty: no recording, no sound chosen). */
    bool hybridEmpty (InstrumentHost& host)
    {
        InstrumentHost::GeneratedSource s;
        return host.generatedSource (1, s) && s.placeholder.isNotEmpty();
    }

    /** A transparent area over an empty card C's well: a click browses for a recording. */
    class BrowseArea final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit BrowseArea (InstrumentHost& h) : host (h)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setTooltip ("Choose a recording for card C (or drop one here)");
            setTitle ("Add sample");
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! getLocalBounds().contains (e.getPosition()))
                return;
            chooser = std::make_unique<juce::FileChooser> ("Add a sample to card C", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
            auto* h = &host;
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [h] (const juce::FileChooser& fc) {
                if (fc.getResult().existsAsFile())
                    if (auto* shell = dynamic_cast<andor::shell::ShellProcessor*> (h))
                        shell->loadFile (fc.getResult(), andor::shell::ShellProcessor::hybridLayer);
            });
        }

    private:
        InstrumentHost& host;
        std::unique_ptr<juce::FileChooser> chooser;
    };

    struct SlotIds
    {
        juce::String model, wave, width, lfoWidth, dcoSaw, dcoPulse, dcoSub, dcoSource, dcoDepth, wideWave, wideShape, wideSpread, keyTrack, lowFreq;
        bool fullWaves = true;   ///< OSC 2 / OSC 3's seven CLASSIC waves (OSC 1 has three)
    };

    SlotIds slotIds (int slot)
    {
        using namespace params;
        if (slot == 1)
            return { osc1Model, osc1Wave, osc1Width, lfoWidth1, osc1DcoSaw, osc1DcoPulse, osc1DcoSub, osc1DcoPwmSource, osc1DcoPwmDepth,
                     osc1WideWave, osc1WideShape, osc1WideSpread, {}, {}, false };
        if (slot == 2)
            return { osc2Model, osc2Wave, osc2Width, lfoWidth2, osc2DcoSaw, osc2DcoPulse, osc2DcoSub, osc2DcoPwmSource, osc2DcoPwmDepth,
                     osc2WideWave, osc2WideShape, osc2WideSpread, osc2KeyTrack, osc2LowFreq, true };
        return { osc3Model, osc3Wave, osc3Width, lfoWidth3, osc3DcoSaw, osc3DcoPulse, osc3DcoSub, osc3DcoPwmSource, osc3DcoPwmDepth,
                 osc3WideWave, osc3WideShape, osc3WideSpread, {}, {}, true };
    }

    int modelOf (InstrumentHost& host, int slot)
    {
        return juce::roundToInt (host.parameterValue (slotIds (slot).model));
    }

    class OscillatorOverlay final : public WellOverlay
    {
    public:
        OscillatorOverlay (InstrumentHost& h, int slotIndex, int layer)
            : host (h), slot (slotIndex), ids (slotIds (slotIndex)), model (modelOf (h, slotIndex)), accent (palette::layer (InstrumentHost::shownLayer (layer)))
        {
            if (slot == 3)
            {
                // Card C as OSC 3: its TYPE first (always shown), the model beside it.
                typeSelector = std::make_unique<ValueSelector> (*host.apvts().getParameter (params::hybridType), juce::String());
                typeSelector->setPlain (true);
                typeSelector->setOnDark (true);
                typeSelector->setAccent (accent);
                typeSelector->setTooltip ("What card C plays: a SAMPLE, a third OSCILLATOR, NOISE or an imported INSTRUMENT PATCH (the others are kept, silent)");
                typeSelector->setTitle ("Card C type");
                typeSelector->textOverride = [this] { return shortTypeText (host); };
                binding::tag (*typeSelector, params::hybridType);
                addAndMakeVisible (*typeSelector);
            }
            setTitle ("OSC " + juce::String (slot) + " sound design");
            // Always shown: the model, a compact dropdown at the top right (it names what plays).
            modelSelector = std::make_unique<ValueSelector> (*host.apvts().getParameter (ids.model), juce::String());
            modelSelector->setPlain (true);
            modelSelector->setOnDark (true);
            modelSelector->setAccent (accent);
            modelSelector->setTooltip ("OSC " + juce::String (slot) + "'s model: CLASSIC (the original VCO), DCO (Juno-style, SAW and PULSE together, SUB) or WIDE (more waves, SHAPE, SUPER SAW)");
            modelSelector->setTitle ("OSC " + juce::String (slot) + " model");
            binding::tag (*modelSelector, ids.model);
            addAndMakeVisible (*modelSelector);

            // Revealed: this model's timbre, five cells along the foot of the well.
            if (model == 1)   // DCO
            {
                cells = { pair (switchFor (ids.dcoSaw, "SAW", "OSC " + juce::String (slot) + "'s sawtooth (with PULSE: both sound)"),
                                switchFor (ids.dcoPulse, "PULSE", "OSC " + juce::String (slot) + "'s pulse (with SAW: both sound)")),
                          knob (ids.dcoSub, "SUB", "The square one octave down"),
                          width = knob (ids.width, "WIDTH", "The pulse's width"),
                          pwm = knob (ids.dcoDepth, "PWM", "How far the PWM source sweeps the width"),
                          pwmSource = selector (ids.dcoSource, "SOURCE", "What sweeps the width: MANUAL (none), LFO 1 or the filter envelope") };
            }
            else if (model == 2)   // WIDE
            {
                cells = { waveSelector = selector (ids.wideWave, "WAVE", "The wave: SAW, PULSE, TRIANGLE, SQUARE or SUPER SAW (seven saws)"),
                          shape = knob (ids.wideShape, "SHAPE", "The wave's edge: soft at 0, calibrated at the centre, raw at 100"),
                          spread = knob (ids.wideSpread, "SPREAD", "SUPER SAW's detune (its seven saws apart)"),
                          width = knob (ids.width, "WIDTH", "The pulse's width"),
                          lfoPw = knob (ids.lfoWidth, "LFO PW", "LFO 1's sweep of the pulse width") };
            }
            else   // CLASSIC
            {
                cells = { waveSelector = selector (ids.wave, "WAVE", ids.fullWaves ? "The wave or mix of waves" : "The wave: SAW, PULSE or both"),
                          width = knob (ids.width, "WIDTH", "The pulse's width"),
                          lfoPw = knob (ids.lfoWidth, "LFO PW", "LFO 1's sweep of the pulse width"), nullptr, nullptr };
            }
            if (ids.keyTrack.isNotEmpty())
            {
                keyTrack = switchFor (ids.keyTrack, "KEY", "OSC 2 follows the keys (off: a fixed pitch, a drone or an FM source)");
                lowFreq = switchFor (ids.lowFreq, "LOW", "OSC 2 far below the keys: a modulator for Poly Mod, not heard");
            }
            tick();
        }

        juce::Rectangle<float> revealBand() const override { return band().toFloat(); }

        void resized() override
        {
            WellOverlay::resized();
            const int w = getWidth();
            if (typeSelector != nullptr)
            {
                typeSelector->setBounds (w - 12 - 118, 6, 118, 22);
                modelSelector->setBounds (w - 12 - 118 - 8 - 92, 6, 92, 22);
            }
            else
                modelSelector->setBounds (w - 12 - 92, 6, 92, 22);
            const auto b = band().reduced (8, 4);
            const int cellWidth = b.getWidth() / static_cast<int> (cells.size());
            for (std::size_t i = 0; i < cells.size(); ++i)
                if (auto* c = cells[i])
                {
                    auto cell = juce::Rectangle<int> (b.getX() + static_cast<int> (i) * cellWidth, b.getY(), cellWidth, b.getHeight());
                    if (dynamic_cast<ValueSelector*> (c) != nullptr)
                        c->setBounds (cell.withSizeKeepingCentre (cellWidth - 6, 44));
                    else if (c == switchPair.get())
                        c->setBounds (cell.withSizeKeepingCentre (cellWidth - 8, 58));
                    else
                        c->setBounds (cell);
                }
            if (switchPair != nullptr)
            {
                auto r = switchPair->getLocalBounds();
                const int h = (r.getHeight() - 6) / 2;
                switchPair->getChildComponent (0)->setBounds (r.removeFromTop (h));
                r.removeFromTop (6);
                switchPair->getChildComponent (1)->setBounds (r.removeFromTop (h));
            }
            if (keyTrack != nullptr)
            {
                keyTrack->setBounds (12, 6, 58, 22);
                lowFreq->setBounds (74, 6, 58, 22);
            }
        }

        void tick() override
        {
            // A control the current wave leaves inactive is dimmed in its place.
            auto dim = [] (juce::Component* c, bool inactive) {
                if (c != nullptr)
                    c->setAlpha (inactive ? 0.38f : 1.0f);
            };
            const int wave = juce::roundToInt (host.parameterValue (model == 2 ? ids.wideWave : ids.wave));
            bool pulse = false;
            if (model == 1)
                pulse = host.parameterValue (ids.dcoPulse) >= 0.5f;
            else if (model == 2)
                pulse = wave == 1;   // (PULSE; SQUARE is fixed at half)
            else
                pulse = ids.fullWaves ? (wave == 2 || wave == 4 || wave == 5 || wave == 6) : (wave == 1 || wave == 2);
            dim (width, ! pulse);
            dim (lfoPw, ! pulse);
            dim (pwm, ! pulse);
            dim (pwmSource, ! pulse);
            if (model == 2)
                dim (spread, wave != 4);   // SUPER SAW's
        }

    private:
        juce::Rectangle<int> band() const { return getLocalBounds().withTrimmedTop (std::max (0, getHeight() - 92)); }

        juce::Component* knob (const juce::String& id, const char* caption, const char* tooltip)
        {
            auto* p = host.apvts().getParameter (id);
            jassert (p != nullptr);
            auto k = std::make_unique<MiniKnob> (host.apvts(), id, caption, [p] (double v) { return p->getText (p->convertTo0to1 (static_cast<float> (v)), 0); });
            k->setOnDark (true);
            k->slider.getProperties().set ("arc", static_cast<juce::int64> (accent.getARGB()));
            k->slider.getProperties().set ("graphite", true);
            k->slider.setTooltip (tooltip);
            return add (std::move (k));
        }

        juce::Component* selector (const juce::String& id, const char* caption, const char* tooltip)
        {
            auto s = std::make_unique<ValueSelector> (*host.apvts().getParameter (id), juce::String (caption));
            s->setOnDark (true);
            s->setTooltip (tooltip);
            s->setTitle (host.apvts().getParameter (id)->getName (64));
            binding::tag (*s, id);
            return add (std::move (s));
        }

        WellSwitch* switchFor (const juce::String& id, const char* text, const juce::String& tooltip)
        {
            auto t = std::make_unique<WellSwitch> (text, accent);
            t->setTooltip (tooltip);
            binding::tag (*t, id);
            attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (host.apvts(), id, *t));
            auto* raw = t.get();
            revealed().addAndMakeVisible (*raw);
            owned.push_back (std::move (t));
            return raw;
        }

        /** Two switches stacked in one cell (DCO's SAW and PULSE). */
        juce::Component* pair (WellSwitch* first, WellSwitch* second)
        {
            switchPair = std::make_unique<juce::Component>();
            switchPair->setInterceptsMouseClicks (false, true);
            revealed().addAndMakeVisible (*switchPair);
            switchPair->addAndMakeVisible (*first);
            switchPair->addAndMakeVisible (*second);
            return switchPair.get();
        }

        template <typename C>
        juce::Component* add (std::unique_ptr<C> c)
        {
            auto* raw = c.get();
            revealed().addAndMakeVisible (*raw);
            owned.push_back (std::move (c));
            return raw;
        }

        InstrumentHost& host;
        const int slot;
        const SlotIds ids;
        const int model;
        const juce::Colour accent;
        std::unique_ptr<ValueSelector> modelSelector, typeSelector;
        std::vector<std::unique_ptr<juce::Component>> owned;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> attachments;
        std::unique_ptr<juce::Component> switchPair;
        std::array<juce::Component*, 5> cells {};
        juce::Component *width = nullptr, *lfoPw = nullptr, *pwm = nullptr, *pwmSource = nullptr, *shape = nullptr, *spread = nullptr, *waveSelector = nullptr;
        WellSwitch *keyTrack = nullptr, *lowFreq = nullptr;
    };
}

namespace
{
    /** Card C as a SAMPLE, NOISE or INSTRUMENT PATCH (as an oscillator: OscillatorOverlay, slot 3). */
    class HybridOverlay final : public WellOverlay
    {
    public:
        explicit HybridOverlay (InstrumentHost& h)
            : host (h), type (static_cast<HybridType> (std::clamp (juce::roundToInt (h.parameterValue (params::hybridType)), 0, 3))),
              accent (palette::layer (InstrumentHost::shownLayer (1)))
        {
            setTitle ("Card C");
            typeSelector = std::make_unique<ValueSelector> (*host.apvts().getParameter (params::hybridType), juce::String());
            typeSelector->setPlain (true);
            typeSelector->setOnDark (true);
            typeSelector->setAccent (accent);
            typeSelector->setTooltip ("What card C plays: a SAMPLE, a third OSCILLATOR, NOISE or an imported INSTRUMENT PATCH (the others are kept, silent)");
            typeSelector->setTitle ("Card C type");
            typeSelector->textOverride = [this] { return shortTypeText (host); };
            binding::tag (*typeSelector, params::hybridType);
            addAndMakeVisible (*typeSelector);
            if (type == HybridType::noise)
            {
                // (TONE, MOVEMENT and KEYS are the card's own knobs, under the well.)
                cells = { selector (params::hybridNoiseColor, "COLOR", "The noise: WHITE, or the Juno's darker hiss"), nullptr, nullptr };
            }
            else if (type == HybridType::instrumentPatch)
            {
                soundKey = std::make_unique<WellAction> ("SOUNDS", accent);
                soundKey->setTooltip ("The imported sounds: pick one for card C, import a Kontakt patch, or remove it (OSC 1, OSC 2 and the effects stay)");
                soundKey->onClick = [this] { showSoundMenu (host, *soundKey); };
                // Empty (no sound chosen yet): the sounds always there, not only on hover.
                if (hybridEmpty (host))
                    addAndMakeVisible (*soundKey);
                else
                    revealed().addAndMakeVisible (*soundKey);   // (a key at the well's foot, as when empty: never a knob cell)
            }
            else if (type == HybridType::sample && hybridEmpty (host))
            {
                // Empty: the whole well is the invitation (click to browse, or drop a file on the
                // card); a quiet second way in at its foot.
                browse = std::make_unique<BrowseArea> (host);
                addAndMakeVisible (*browse);
                importKey = std::make_unique<WellAction> ("IMPORT KONTAKT PATCH", accent);
                importKey->setTooltip ("Turn a Kontakt instrument (its NKI and samples) into sounds for card C");
                importKey->onClick = [this] { runKontaktImport (host); };
                addAndMakeVisible (*importKey);
            }
            typeSelector->toFront (false);
        }

        int rulerReserve() const override { return 118 + 24; }
        /** Its TYPE dropdown stays while card C plays Granular (the grains sit along the foot). */
        bool shownWhileGranular() const override { return true; }

        juce::Rectangle<float> revealBand() const override
        {
            if (type == HybridType::sample)
                return {};
            const int band = type == HybridType::instrumentPatch ? 52 : 92;   // (SOUNDS: one key's height)
            return getLocalBounds().withTrimmedTop (std::max (0, getHeight() - band)).toFloat();
        }

        void resized() override
        {
            WellOverlay::resized();
            typeSelector->setBounds (getWidth() - 12 - 118, 6, 118, 22);
            if (browse != nullptr)
                browse->setBounds (getLocalBounds().withTrimmedTop (32));
            if (importKey != nullptr)
                importKey->setBounds (getLocalBounds().withSizeKeepingCentre (std::min (getWidth() - 24, 190), 26).withBottomY (getHeight() - 12));
            if (soundKey != nullptr)
                soundKey->setBounds (getLocalBounds().withSizeKeepingCentre (std::min (getWidth() - 24, 150), 28).withBottomY (getHeight() - 14));
            const auto b = getLocalBounds().withTrimmedTop (std::max (0, getHeight() - 92)).reduced (8, 4);
            const int cellWidth = b.getWidth() / 3;
            for (std::size_t i = 0; i < cells.size(); ++i)
                if (auto* c = cells[i])
                {
                    auto cell = juce::Rectangle<int> (b.getX() + static_cast<int> (i) * cellWidth, b.getY(), cellWidth, b.getHeight());
                    if (dynamic_cast<ValueSelector*> (c) != nullptr)
                        c->setBounds (cell.withSizeKeepingCentre (cellWidth - 10, 44));
                    else if (dynamic_cast<juce::TextButton*> (c) != nullptr)
                        c->setBounds (cell.withSizeKeepingCentre (cellWidth - 10, 28));
                    else
                        c->setBounds (cell.withSizeKeepingCentre (std::min (cellWidth, 100), b.getHeight()));
                }
        }

    private:
        juce::Component* knob (const juce::String& id, const char* caption, const char* tooltip)
        {
            auto* p = host.apvts().getParameter (id);
            auto k = std::make_unique<MiniKnob> (host.apvts(), id, caption, [p] (double v) { return p->getText (p->convertTo0to1 (static_cast<float> (v)), 0); });
            k->setOnDark (true);
            k->slider.getProperties().set ("arc", static_cast<juce::int64> (accent.getARGB()));
            k->slider.getProperties().set ("graphite", true);
            k->slider.setTooltip (tooltip);
            auto* raw = k.get();
            revealed().addAndMakeVisible (*raw);
            owned.push_back (std::move (k));
            return raw;
        }

        juce::Component* selector (const juce::String& id, const char* caption, const char* tooltip)
        {
            auto s = std::make_unique<ValueSelector> (*host.apvts().getParameter (id), juce::String (caption));
            s->setOnDark (true);
            s->setTooltip (tooltip);
            s->setTitle (host.apvts().getParameter (id)->getName (64));
            binding::tag (*s, id);
            auto* raw = s.get();
            revealed().addAndMakeVisible (*raw);
            owned.push_back (std::move (s));
            return raw;
        }

        InstrumentHost& host;
        const HybridType type;
        const juce::Colour accent;
        std::unique_ptr<ValueSelector> typeSelector;
        std::unique_ptr<WellAction> soundKey, importKey;
        std::unique_ptr<BrowseArea> browse;
        std::vector<std::unique_ptr<juce::Component>> owned;
        std::array<juce::Component*, 3> cells {};
    };
}

std::unique_ptr<WellOverlay> createHybridOverlay (InstrumentHost& host)
{
    if (juce::roundToInt (host.parameterValue (params::hybridType)) == static_cast<int> (HybridType::oscillator))
        return std::make_unique<OscillatorOverlay> (host, 3, 1);
    return std::make_unique<HybridOverlay> (host);
}

juce::String hybridOverlayKey (InstrumentHost& host)
{
    const int type = juce::roundToInt (host.parameterValue (params::hybridType));
    return "hybrid/" + juce::String (type) + (type == static_cast<int> (HybridType::oscillator) ? "/" + juce::String (modelOf (host, 3)) : juce::String())
           + (hybridEmpty (host) ? "/empty" : "");
}

std::unique_ptr<WellOverlay> createOscillatorOverlay (InstrumentHost& host, int slot, int layer)
{
    return std::make_unique<OscillatorOverlay> (host, slot, layer);
}

juce::String oscillatorOverlayKey (InstrumentHost& host, int slot)
{
    return "osc" + juce::String (slot) + "/" + juce::String (modelOf (host, slot));
}

} // namespace andor::analog
