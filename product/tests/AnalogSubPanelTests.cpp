// SUB popup V7 (docs/SUB_POPUP_V7.md): the panel's composition, its display's response (exactly
// SubOsc::keyGain's), its gestures (one host gesture each, undoable, never crossing, inside
// 0..127), FLAT, the valve, automation, keyboard access, scaling, and the screenshots.
#include "AnalogParameters.h"
#include "CeramicSkin.h"
#include "ShellEditor.h"
#include "ShellProcessor.h"
#include "SubOsc.h"
#include "SubPanel.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using andor::analog::SubPanel;
using andor::analog::SubResponseDisplay;
using andor::shell::ShellProcessor;
namespace params = andor::analog::params;
namespace subview = andor::analog::subview;

namespace
{
    void set (ShellProcessor& p, const juce::String& id, float v)
    {
        auto* param = p.parameters.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (v));
    }
    float get (ShellProcessor& p, const juce::String& id)
    {
        auto* param = p.parameters.getParameter (id);
        return param->convertFrom0to1 (param->getValue());
    }
    juce::MouseEvent at (juce::Component& c, juce::Point<float> where, int clicks = 1)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (source, where, {}, juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, where, now, clicks, false);
    }
    /** Counts host gestures on parameters (begin / end), as a DAW sees them. */
    struct Gestures final : juce::AudioProcessorParameter::Listener
    {
        explicit Gestures (std::vector<juce::RangedAudioParameter*> ps) : watched (std::move (ps))
        {
            for (auto* p : watched)
                p->addListener (this);
        }
        ~Gestures() override
        {
            for (auto* p : watched)
                p->removeListener (this);
        }
        void parameterValueChanged (int, float) override {}
        void parameterGestureChanged (int, bool starting) override { (starting ? begins : ends)++; }
        std::vector<juce::RangedAudioParameter*> watched;
        int begins = 0, ends = 0;
    };
    struct Rig
    {
        ShellProcessor p;
        std::unique_ptr<SubPanel> panel;
        Rig()
        {
            panel = std::make_unique<SubPanel> (p);
            const auto size = panel->cardSize();
            panel->setSize (size.x + 2 * osp::plugin::MiniPanel::shadowMargin, size.y + 2 * osp::plugin::MiniPanel::shadowMargin);
        }
        SubResponseDisplay& d() { return panel->display(); }
        void drag (juce::Point<float> from, juce::Point<float> to)
        {
            auto& c = d();
            c.mouseDown (at (c, from));
            c.mouseDrag (at (c, to));
            c.mouseUp (at (c, to));
        }
    };
}

TEST_CASE ("SUB V7: the panel's composition, the same in FLAT and TAPER", "[v7][sub]")
{
    Rig r;
    auto& panel = *r.panel;
    CHECK (panel.cardSize().x == SubPanel::width);
    const auto taperSize = panel.cardSize();
    set (r.p, params::subResponse, 0.0f);
    panel.refreshContent();
    CHECK (panel.cardSize() == taperSize);   // no jump between FLAT and TAPER
    CHECK (panel.fromKnob().isVisible());
    CHECK (panel.slopeKnob().isVisible());
    CHECK (panel.fromKnob().getAlpha() < 1.0f);   // dimmed, still there
    set (r.p, params::subResponse, 1.0f);
    panel.refreshContent();
    CHECK (panel.fromKnob().getAlpha() == Catch::Approx (1.0f));
    // WAVE and OCT on one line; the display spans the panel; the controls below it.
    const auto wave = panel.waveControl().getBounds(), octave = panel.octaveControl().getBounds();
    CHECK (wave.getY() == octave.getY());
    CHECK (wave.getHeight() == octave.getHeight());
    CHECK (octave.getWidth() < wave.getWidth() / 2);
    const auto display = panel.display().getBounds();
    CHECK (display.getY() > wave.getBottom());
    CHECK (panel.gritKnob().getY() > display.getBottom());
    CHECK (panel.valve().getRight() <= panel.gritKnob().getX());
    CHECK (panel.responseControl().getY() > display.getBottom());
    CHECK (panel.fromKnob().getY() > panel.responseControl().getBottom());
    // Every control is an existing SUB parameter (no shadow state).
    CHECK (panel.gritKnob().slider.getProperties()["paramId"].toString() == params::subGrit);
    CHECK (panel.levelKnob().slider.getProperties()["paramId"].toString() == params::subLevel);
    CHECK (panel.fromKnob().slider.getProperties()["paramId"].toString() == params::subTaperStart);
    CHECK (panel.slopeKnob().slider.getProperties()["paramId"].toString() == params::subTaperSlope);
}

TEST_CASE ("SUB V7: the drawn response is SubOsc::keyGain's, without LEVEL", "[v7][sub]")
{
    using andor::analog::SubOsc;
    using andor::analog::SubSettings;
    for (const bool taper : { false, true })
        for (const int start : { 40, 60, 90 })
            for (const double slope : { 0.0, 5.5, 12.0 })
            {
                SubSettings s;
                s.enabled = true;
                s.lowKey = 0;
                s.highKey = 127;
                s.taper = taper;
                s.taperStart = start;
                s.taperDbPerOctave = slope;
                s.levelDb = -18.0;
                SubOsc osc;
                osc.prepare (48000.0);
                osc.setSettings (s);
                const double ref = 20.0 * std::log10 (osc.keyGain (0));
                for (int key = 0; key <= 127; key += 7)
                {
                    const double dsp = ref - 20.0 * std::log10 (osc.keyGain (key));
                    CHECK (subview::relativeLossDb (key, taper, start, slope) == Catch::Approx (dsp).margin (1.0e-3));
                }
            }
    // The mock's example, honestly: C3..D4, FROM C4, 5.5 dB / oct: under 1 dB inside the range.
    CHECK (subview::relativeLossDb (62, true, 60, 5.5) == Catch::Approx (2.0 * 5.5 / 12.0));
    Rig r;
    set (r.p, params::subLowKey, 48.0f);
    set (r.p, params::subHighKey, 62.0f);
    set (r.p, params::subTaperStart, 60.0f);
    set (r.p, params::subTaperSlope, 5.5f);
    auto& d = r.d();
    CHECK (d.scaleDb() == Catch::Approx (std::ceil (subview::relativeLossDb (108, true, 60, 5.5) / 6.0) * 6.0));
    CHECK (d.curveY (48) == Catch::Approx (d.curveY (60)));
    CHECK (d.curveY (62) > d.curveY (60));
    CHECK (d.curveY (62) - d.curveY (60) < 0.05f * d.graphArea().getHeight());   // near flat, as it sounds
    // LEVEL never moves the curve.
    const float before = d.curveY (80);
    set (r.p, params::subLevel, -40.0f);
    CHECK (d.curveY (80) == Catch::Approx (before));
    // Slope 0 with TAPER: flat (the mode stays TAPER).
    set (r.p, params::subTaperSlope, 0.0f);
    CHECK (d.curveY (100) == Catch::Approx (d.curveY (30)));
    CHECK (d.taper());
}

TEST_CASE ("SUB V7: the range on the keyboard - one gesture, never crossing, inside 0..127", "[v7][sub]")
{
    Rig r;
    auto& d = r.d();
    auto& low = *r.p.parameters.getParameter (params::subLowKey);
    auto& high = *r.p.parameters.getParameter (params::subHighKey);
    Gestures gestures ({ &low, &high });
    // The low end dragged past the high end stops at it.
    r.drag (d.handlePosition (SubResponseDisplay::Handle::low).translated (0.0f, 4.0f), { d.keyArea (96).getCentreX(), d.keybedArea().getCentreY() });
    CHECK (juce::roundToInt (get (r.p, params::subLowKey)) == 84);
    CHECK (juce::roundToInt (get (r.p, params::subHighKey)) == 84);
    CHECK (gestures.begins == 1);
    CHECK (gestures.ends == 1);
    // Both ends on one key: the drag's direction picks which moves.
    r.drag (d.handlePosition (SubResponseDisplay::Handle::high).translated (0.0f, 4.0f), { d.keyArea (91).getCentreX(), d.keybedArea().getCentreY() });
    CHECK (juce::roundToInt (get (r.p, params::subLowKey)) == 84);
    CHECK (juce::roundToInt (get (r.p, params::subHighKey)) == 91);
    r.drag (d.handlePosition (SubResponseDisplay::Handle::low).translated (0.0f, 4.0f), { d.keyArea (72).getCentreX(), d.keybedArea().getCentreY() });
    CHECK (juce::roundToInt (get (r.p, params::subLowKey)) == 72);
    CHECK (gestures.begins == gestures.ends);
    // The lit span moves as a whole (both ends, one gesture each), its width kept.
    const int width = juce::roundToInt (get (r.p, params::subHighKey)) - juce::roundToInt (get (r.p, params::subLowKey));
    const auto inside = juce::Point<float> (0.5f * (d.handlePosition (SubResponseDisplay::Handle::low).x + d.handlePosition (SubResponseDisplay::Handle::high).x),
                                            d.keybedArea().getCentreY());
    REQUIRE (d.handleAt (inside) == SubResponseDisplay::Handle::span);
    r.p.apvts().copyState();   // (the parameters into the undo state, as the message loop's timer does)
    const int lowBefore = juce::roundToInt (get (r.p, params::subLowKey)), highBefore = juce::roundToInt (get (r.p, params::subHighKey));
    r.drag (inside, inside.translated (-6.0f * d.keyArea (60).getWidth(), 0.0f));
    r.p.apvts().copyState();
    CHECK (juce::roundToInt (get (r.p, params::subHighKey)) - juce::roundToInt (get (r.p, params::subLowKey)) == width);
    CHECK (juce::roundToInt (get (r.p, params::subLowKey)) < 72);
    CHECK (gestures.begins == gestures.ends);
    // Undo takes the whole move back in one step.
    r.p.undo().undo();
    CHECK (juce::roundToInt (get (r.p, params::subLowKey)) == lowBefore);
    CHECK (juce::roundToInt (get (r.p, params::subHighKey)) == highBefore);
    // MIDI 0 and 127: the keyboard widens to show them; nothing is clamped to what it shows.
    set (r.p, params::subLowKey, 0.0f);
    set (r.p, params::subHighKey, 127.0f);
    CHECK (d.viewport() == subview::Viewport { 0, 127 });
    CHECK (d.keybedArea().contains (d.keyArea (0).getCentre()));
    CHECK (d.keybedArea().contains (d.keyArea (127).getCentre()));
    CHECK (juce::roundToInt (get (r.p, params::subLowKey)) == 0);
    // Keys beyond C1..C8 with a narrow range: shifted by octaves, not the whole MIDI range.
    set (r.p, params::subLowKey, 10.0f);
    set (r.p, params::subHighKey, 30.0f);
    CHECK (d.viewport() == subview::Viewport { 0, 84 });
    set (r.p, params::subHighKey, 127.0f);
    set (r.p, params::subLowKey, 100.0f);
    CHECK (d.viewport() == subview::Viewport { 43, 127 });
    set (r.p, params::subLowKey, 48.0f);
    set (r.p, params::subHighKey, 84.0f);
    CHECK (d.viewport() == subview::Viewport { 24, 108 });
    // Keyboard access: Left / Right the low end, with Shift the high end; never crossing.
    CHECK (d.keyPressed (juce::KeyPress (juce::KeyPress::leftKey)));
    CHECK (juce::roundToInt (get (r.p, params::subLowKey)) == 47);
    CHECK (d.keyPressed (juce::KeyPress (juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier, 0)));
    CHECK (juce::roundToInt (get (r.p, params::subHighKey)) == 85);
    set (r.p, params::subLowKey, 60.0f);
    set (r.p, params::subHighKey, 60.0f);
    CHECK (d.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (juce::roundToInt (get (r.p, params::subLowKey)) == 60);   // (the low end never passes the high one)
}

TEST_CASE ("SUB V7: FROM and the slope on the curve, synchronised with their knobs", "[v7][sub]")
{
    Rig r;
    auto& d = r.d();
    auto& start = *r.p.parameters.getParameter (params::subTaperStart);
    auto& slope = *r.p.parameters.getParameter (params::subTaperSlope);
    Gestures gestures ({ &start, &slope });
    // FROM: horizontal, whole semitones; the FROM knob follows (it is the same parameter).
    REQUIRE (d.handleAt (d.handlePosition (SubResponseDisplay::Handle::from)) == SubResponseDisplay::Handle::from);
    r.drag (d.handlePosition (SubResponseDisplay::Handle::from), { d.keyArea (67).getCentreX(), d.handlePosition (SubResponseDisplay::Handle::from).y });
    CHECK (juce::roundToInt (get (r.p, params::subTaperStart)) == 67);
    CHECK (r.panel->fromKnob().slider.getValue() == Catch::Approx (67.0));
    CHECK (gestures.begins == 1);
    CHECK (gestures.ends == 1);
    // Note names: the keyboard, the readout and the knob agree (MIDI 60 = C4).
    CHECK (subview::noteName (60) == "C4");
    CHECK (start.getText (start.convertTo0to1 (60.0f), 0) == "C4");
    // The slope: down is steeper, up gentler; 0 and 12 at the ends.
    const auto s = d.handlePosition (SubResponseDisplay::Handle::slope);
    REQUIRE (d.handleAt (s) == SubResponseDisplay::Handle::slope);
    r.drag (s, s.translated (0.0f, 1000.0f));
    CHECK (get (r.p, params::subTaperSlope) == Catch::Approx (12.0f));
    r.drag (d.handlePosition (SubResponseDisplay::Handle::slope), d.handlePosition (SubResponseDisplay::Handle::slope).translated (0.0f, -1000.0f));
    CHECK (get (r.p, params::subTaperSlope) == Catch::Approx (0.0f));
    CHECK (r.panel->slopeKnob().slider.getValue() == Catch::Approx (0.0));
    CHECK (gestures.begins == 3);
    CHECK (gestures.ends == 3);
    // A knob or automation moves the handle at once (no gesture from the display).
    set (r.p, params::subTaperStart, 72.0f);
    CHECK (d.handlePosition (SubResponseDisplay::Handle::from).x == Catch::Approx (d.keyArea (72).getCentreX()));
    CHECK (gestures.begins == 3);
    // A handle's double-click resets it.
    d.mouseDoubleClick (at (d, d.handlePosition (SubResponseDisplay::Handle::from), 2));
    CHECK (juce::roundToInt (get (r.p, params::subTaperStart)) == 60);
    // FLAT: the curve is level, its handles neither drawn nor hit; values kept for TAPER.
    set (r.p, params::subTaperSlope, 8.0f);
    set (r.p, params::subResponse, 0.0f);
    CHECK (d.curveY (100) == Catch::Approx (d.curveY (40)));
    CHECK (d.handleAt (d.handlePosition (SubResponseDisplay::Handle::from)) != SubResponseDisplay::Handle::from);
    CHECK (get (r.p, params::subTaperSlope) == Catch::Approx (8.0f));
    set (r.p, params::subResponse, 1.0f);
    CHECK (d.curveY (100) > d.curveY (40));
    // A popover closed mid-drag still ends its gesture.
    const int begun = gestures.begins;
    d.mouseDown (at (d, d.handlePosition (SubResponseDisplay::Handle::from)));
    CHECK (gestures.begins == begun + 1);
    r.panel.reset();
    CHECK (gestures.ends == gestures.begins);
}

TEST_CASE ("SUB V7: the wave picture and the valve follow the parameters", "[v7][sub]")
{
    Rig r;
    for (int w = 0; w < 4; ++w)
    {
        set (r.p, params::subWave, static_cast<float> (w));   // (as automation would)
        CHECK (r.d().wave() == w);
        CHECK (r.panel->waveControl().selected() == w);
    }
    set (r.p, params::subOctave, 0.0f);
    CHECK (r.panel->octaveControl().selected() == 0);
    // The valve: brighter with GRIT, dimmed with SUB off, never by itself.
    set (r.p, params::subEnabled, 1.0f);
    float last = -1.0f;
    for (const float grit : { 0.0f, 25.0f, 50.0f, 79.0f, 100.0f })
    {
        set (r.p, params::subGrit, grit);
        r.panel->refreshContent();
        CHECK (r.panel->valve().glow() > last);
        last = r.panel->valve().glow();
    }
    CHECK (r.panel->valve().glow() == Catch::Approx (1.0f));
    set (r.p, params::subEnabled, 0.0f);
    r.panel->refreshContent();
    CHECK (r.panel->valve().dimmed());
    CHECK (r.panel->valve().glow() < 0.5f);
    CHECK (get (r.p, params::subGrit) == Catch::Approx (100.0f));   // the setting itself untouched
}

TEST_CASE ("SUB V7: the popover inside the instrument at every size", "[v7][sub]")
{
    for (const int width : { 869, 1086, 1448, 1810, 2172 })
    {
        ShellProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
        auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
        editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
        ui.refreshNow();
        ui.openPopup (andor::shell::ShellEditor::arpCompanionPopup);
        ui.refreshNow();
        auto* panel = dynamic_cast<SubPanel*> (ui.openPopupPanel());
        REQUIRE (panel != nullptr);
        const auto card = ui.contentForTest().getLocalArea (panel, panel->card());
        INFO (width << ": " << card.toString());
        CHECK (ui.contentForTest().getLocalBounds().contains (card));
        // Opening it does not switch SUB.
        CHECK (get (p, params::subEnabled) == Catch::Approx (0.0f));
    }
}

TEST_CASE ("SUB V7: screenshots (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][screenshots-sub-v7]")
{
    const char* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    for (const int width : { 1448, 869 })
    {
        ShellProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
        auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
        editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
        auto save = [&] (const juce::Image& image, const juce::String& name) {
            const auto file = out.getChildFile (name + "-" + juce::String (width) + ".png");
            file.deleteFile();
            juce::FileOutputStream stream (file);
            juce::PNGImageFormat().writeImageToStream (image, stream);
        };
        auto shoot = [&] (const juce::String& name, bool full = false) {
            ui.closePopup();
            ui.openPopup (andor::shell::ShellEditor::arpCompanionPopup);
            ui.refreshNow();
            if (full)
                save (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), name + "-full");
            auto* panel = ui.openPopupPanel();
            REQUIRE (panel != nullptr);
            const auto area = ui.getLocalArea (panel, panel->card().expanded (8));
            save (editor->createComponentSnapshot (area, true, 2.0f), name);
        };
        // 1. The instance defaults (SUB off), and on.
        shoot ("v7-01-defaults-off");
        set (p, params::subEnabled, 1.0f);
        shoot ("v7-02-defaults", true);
        // 2. The approved mock's values (its response drawn as it sounds).
        set (p, params::subLowKey, 48.0f);
        set (p, params::subHighKey, 62.0f);
        set (p, params::subOctave, 0.0f);
        set (p, params::subGrit, 79.0f);
        set (p, params::subLevel, -18.0f);
        set (p, params::subTaperStart, 60.0f);
        set (p, params::subTaperSlope, 5.5f);
        shoot ("v7-03-mock-values", true);
        set (p, params::subResponse, 0.0f);
        shoot ("v7-04-flat");
        set (p, params::subResponse, 1.0f);
        // 3. The waves.
        for (const auto& [w, name] : { std::pair { 1, "tri" }, std::pair { 2, "square" }, std::pair { 3, "saw" } })
        {
            set (p, params::subWave, static_cast<float> (w));
            shoot (juce::String ("v7-05-wave-") + name);
        }
        set (p, params::subWave, 0.0f);
        // 4. GRIT 0 / 50 / 100.
        for (const float grit : { 0.0f, 50.0f, 100.0f })
        {
            set (p, params::subGrit, grit);
            shoot ("v7-06-grit-" + juce::String (juce::roundToInt (grit)));
        }
        // 5. The edges: 0..127, and one key; a steep taper.
        set (p, params::subLowKey, 0.0f);
        set (p, params::subHighKey, 127.0f);
        shoot ("v7-07-range-0-127");
        set (p, params::subLowKey, 60.0f);
        set (p, params::subHighKey, 60.0f);
        shoot ("v7-08-range-one-key");
        set (p, params::subLowKey, 36.0f);
        set (p, params::subHighKey, 96.0f);
        set (p, params::subTaperStart, 48.0f);
        set (p, params::subTaperSlope, 12.0f);
        shoot ("v7-09-steep-taper", true);
        ui.closePopup();
    }
}
