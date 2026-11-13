// D / AUX (docs/aux): the product side - the strip under the cards (a view: shown and hidden
// from the main menu, kept with the session and never with a preset, the cards and the sound
// untouched), its modes, menu and rack, D's recording (loaded on the loader thread, kept in the
// sample store, missing handled), its MIDI lane and latency, and the screenshots.
#include "Binding.h"
#include "AnalogEngine.h"
#include "AnalogParameters.h"
#include "AuxUI.h"
#include "CeramicSkin.h"
#include "DropOwner.h"
#include "ShellEditor.h"
#include "ShellProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>

using andor::shell::ShellProcessor;
using andor::analog::AnalogEngine;
using andor::analog::AuxStripView;
namespace params = andor::analog::params;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int block = 512;

    void set (ShellProcessor& p, const juce::String& id, float v)
    {
        auto* param = p.parameters.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (v));
    }

    AnalogEngine& engineOf (ShellProcessor& p) { return dynamic_cast<AnalogEngine&> (p.productEngine()); }

    /** A held chord from the start, released at `releaseAt` seconds. */
    juce::AudioBuffer<float> render (ShellProcessor& p, double seconds, double releaseAt = 0.4)
    {
        p.prepareToPlay (rate, block);
        const int total = static_cast<int> (seconds * rate);
        const int off = static_cast<int> (releaseAt * rate);
        juce::AudioBuffer<float> out (2, total), buffer (2, block);
        for (int pos = 0; pos < total; pos += block)
        {
            juce::MidiBuffer midi;
            for (const int note : { 57, 64, 69 })
            {
                if (pos == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)), 0);
                if (off >= pos && off < pos + block)
                    midi.addEvent (juce::MidiMessage::noteOff (1, note), off - pos);
            }
            p.processBlock (buffer, midi);
            const int n = std::min (block, total - pos);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, buffer, ch, 0, n);
        }
        return out;
    }

    bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        if (a.getNumSamples() != b.getNumSamples())
            return false;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
                if (std::memcmp (a.getReadPointer (ch) + i, b.getReadPointer (ch) + i, sizeof (float)) != 0)
                    return false;
        return true;
    }

    double rms (const juce::AudioBuffer<float>& a, int from = 0, int to = -1)
    {
        to = to < 0 ? a.getNumSamples() : to;
        double sum = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = from; i < to; ++i)
                sum += static_cast<double> (a.getSample (ch, i)) * a.getSample (ch, i);
        return std::sqrt (sum / std::max (1, 2 * (to - from)));
    }

    /** A loop of ticks, 4 bars at 100 BPM, written as a WAV (the loader decodes and analyses it). */
    juce::File writeLoop (const juce::File& dir, const juce::String& name)
    {
        const auto file = dir.getChildFile (name);
        file.deleteFile();
        constexpr double beat = 60.0 / 100.0;
        const int length = static_cast<int> (std::lround (16 * beat * rate));
        juce::AudioBuffer<float> x (2, length);
        x.clear();
        for (int k = 0; k < 16; ++k)
        {
            const int first = static_cast<int> (std::lround (k * beat * rate));
            for (int i = 0; i < static_cast<int> (0.03 * rate) && first + i < length; ++i)
            {
                const double t = i / rate;
                const auto v = static_cast<float> ((k % 4 == 0 ? 0.8 : 0.5) * std::exp (-t / 0.006) * std::sin (2.0 * juce::MathConstants<double>::pi * 2000.0 * t));
                x.setSample (0, first + i, v);
                x.setSample (1, first + i, v);
            }
        }
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), rate, 2, 24, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();
        writer->writeFromAudioSampleBuffer (x, 0, length);
        return file;
    }

    struct Scratch
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("analog-aux-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        Scratch() { dir.createDirectory(); }
        ~Scratch() { dir.deleteRecursively(); }
    };

    andor::shell::ShellEditor& editorOf (std::unique_ptr<juce::AudioProcessorEditor>& e) { return dynamic_cast<andor::shell::ShellEditor&> (*e); }

    /** The strip's timer's work at once (no message loop in the tests). */
    void refreshStrip (andor::shell::ShellEditor& ui)
    {
        ui.refreshNow();
        if (auto* strip = ui.auxStripComponent())
            AuxStripView::refresh (*strip);
    }
}

TEST_CASE ("AUX strip: shown and hidden from the main menu; the cards stay, what is under them moves, the sound never changes", "[analog][aux][ui]")
{
    ShellProcessor p;
    REQUIRE (p.hasAuxStrip());
    CHECK_FALSE (p.auxStripShown());   // (hidden by default: the instrument as before)
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = editorOf (editor);
    ui.refreshNow();
    CHECK (ui.auxStripComponent() == nullptr);
    const auto cards = ui.engineCard (0).getBounds();
    const auto cardC = ui.engineCard (1).getBounds();
    const int modY = ui.modulationPanel().getY();
    const float height = ui.instrumentHeight();
    const auto before = render (p, 0.5);

    ui.setAuxStripShown (true);
    ui.refreshNow();
    REQUIRE (ui.auxStripComponent() != nullptr);
    CHECK (ui.auxStripComponent()->isVisible());
    CHECK (p.auxStripShown());
    const float shift = ui.auxShift();
    CHECK (shift == Catch::Approx (76.0f + 18.0f));
    CHECK (ui.engineCard (0).getBounds() == cards);   // (the cards keep their places and size)
    CHECK (ui.engineCard (1).getBounds() == cardC);
    CHECK (ui.modulationPanel().getY() == modY + juce::roundToInt (shift));
    CHECK (ui.instrumentHeight() == Catch::Approx (height + shift));
    const auto strip = ui.auxStripComponent()->getBounds();
    CHECK (strip.getY() >= ui.engineCard (0).getBottom() + 12);
    CHECK (strip.getBottom() <= ui.modulationPanel().getY());
    CHECK (strip.getHeight() == 76);
    CHECK (identical (before, render (p, 0.5)));   // (a view: nothing sounds different)

    // Its own ... menu: Hide AUX, Open / Close AUX Rack, Reset.
    CHECK (AuxStripView::menuItems (*ui.auxStripComponent()) == juce::StringArray ({ "Hide AUX", "Open AUX Rack", "Reset" }));
    AuxStripView::runMenuItem (*ui.auxStripComponent(), "Hide AUX");
    ui.refreshNow();
    CHECK_FALSE (p.auxStripShown());
    CHECK_FALSE (ui.auxStripComponent()->isVisible());
    CHECK (ui.modulationPanel().getY() == modY);   // (every place exactly back)
    CHECK (ui.instrumentHeight() == Catch::Approx (height));
    CHECK (identical (before, render (p, 0.5)));
}

TEST_CASE ("AUX strip: hiding it never mutes D or changes its sound", "[analog][aux][ui]")
{
    ShellProcessor p;
    set (p, params::auxEnabled, 1.0f);
    set (p, params::auxMode, 1.0f);   // NOISE
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = editorOf (editor);
    ui.setAuxStripShown (true);
    const auto shown = render (p, 0.4);
    ui.setAuxStripShown (false);
    const auto hidden = render (p, 0.4);
    CHECK (rms (shown) > 1.0e-3);
    CHECK (identical (shown, hidden));
    CHECK (p.parameters.getParameter (params::auxEnabled)->getValue() >= 0.5f);
}

TEST_CASE ("AUX strip: its visibility is kept with the session, never with a preset", "[analog][aux][ui]")
{
    Scratch scratch;
    ShellProcessor p;
    p.setAuxStripShown (true);
    juce::MemoryBlock session;
    p.getStateInformation (session);
    const auto preset = scratch.dir.getChildFile ("shown.andoranalog");
    REQUIRE (p.savePreset (preset));

    ShellProcessor q;
    q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
    CHECK (q.auxStripShown());   // (a session: the view as it was left)

    ShellProcessor r;
    CHECK_FALSE (r.auxStripShown());
    REQUIRE (r.loadPreset (preset));
    CHECK_FALSE (r.auxStripShown());   // (a preset never shows it ...)
    r.setAuxStripShown (true);
    p.setAuxStripShown (false);
    const auto hiddenPreset = scratch.dir.getChildFile ("hidden.andoranalog");
    REQUIRE (p.savePreset (hiddenPreset));
    REQUIRE (r.loadPreset (hiddenPreset));
    CHECK (r.auxStripShown());   // (... nor hides it)
}

TEST_CASE ("AUX strip: the mode's knobs, the C-style mode dropdown, the rack from a double-click and Escape", "[analog][aux][ui]")
{
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = editorOf (editor);
    ui.setAuxStripShown (true);
    auto& strip = *ui.auxStripComponent();
    for (const auto& name : { "power", "mute", "solo", "mode", "LEVEL", "display", "rack", "menu" })
        CHECK (AuxStripView::part (strip, name) != nullptr);
    // (Addendum 04: LEVEL is permanent, not one of the mode's knobs)
    CHECK (AuxStripView::knobCaptions (strip) == juce::StringArray ({ "RATE", "PITCH", "OCT", "MOD AMOUNT" }));
    CHECK (AuxStripView::part (strip, "route") == nullptr);   // (Addendum 03: no ROUTE, no LOAD)
    CHECK (AuxStripView::part (strip, "load") == nullptr);
    set (p, params::auxMode, 1.0f);
    ui.refreshNow();
    refreshStrip (ui);
    CHECK (AuxStripView::shownMode (strip) == 1);
    CHECK (AuxStripView::knobCaptions (strip) == juce::StringArray ({ "COLOR", "TONE" }));
    set (p, params::auxMode, 2.0f);
    refreshStrip (ui);
    CHECK (AuxStripView::knobCaptions (strip).isEmpty());   // (KONTAKT: no placeholder knobs)

    const float height = ui.instrumentHeight();
    AuxStripView::doubleClick (strip);
    ui.refreshNow();
    CHECK (ui.effectsRack().isShown (osp::plugin::rack::Kind::aux));
    CHECK (ui.instrumentHeight() > height);
    CHECK (AuxStripView::menuItems (strip).contains ("Close AUX Rack"));
    CHECK (ui.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    ui.refreshNow();
    CHECK_FALSE (ui.effectsRack().isShown (osp::plugin::rack::Kind::aux));
    CHECK (ui.instrumentHeight() == Catch::Approx (height));
    CHECK (p.auxStripShown());   // (Escape closed one level: the rack, not the strip)

    // Hiding the strip takes its rack with it.
    AuxStripView::runMenuItem (strip, "Open AUX Rack");
    ui.refreshNow();
    CHECK (ui.effectsRack().isShown (osp::plugin::rack::Kind::aux));
    ui.setAuxStripShown (false);
    CHECK_FALSE (ui.effectsRack().isShown (osp::plugin::rack::Kind::aux));
}

TEST_CASE ("AUX strip: Reset returns D's parameters to their defaults (one undoable step)", "[analog][aux][ui]")
{
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = editorOf (editor);
    ui.setAuxStripShown (true);
    set (p, params::auxEnabled, 1.0f);
    set (p, params::auxMode, 1.0f);
    set (p, params::auxLevel, 0.0f);
    AuxStripView::runMenuItem (*ui.auxStripComponent(), "Reset");
    for (const auto& id : { params::auxEnabled, params::auxMode, params::auxLevel })
    {
        auto* param = p.parameters.getParameter (id);
        CHECK (param->getValue() == Catch::Approx (param->getDefaultValue()));
    }
}

TEST_CASE ("AUX: D RHYTHM's recording loads on the loader thread, is kept in the store, plays, and comes back with the session", "[analog][aux]")
{
    Scratch scratch;
    ShellProcessor p;
    auto& engine = engineOf (p);
    const auto file = writeLoop (scratch.dir, "ticks.wav");
    const auto store = p.sampleStoreFolder().getChildFile ("Imported");
    engine.loadAuxRecording (file, store);
    REQUIRE (engine.waitForAuxRecording (20000));
    const auto state = engine.auxRecordingState();
    CHECK (state.status == AnalogEngine::AuxRecordingState::Status::ready);
    CHECK (state.name == "ticks");
    REQUIRE (engine.auxLoop() != nullptr);
    CHECK (engine.auxLoop()->rhythmic());
    CHECK (engine.auxLoop()->analysis.bpm == Catch::Approx (100.0).margin (1.0));

    set (p, params::auxEnabled, 1.0f);
    set (p, params::auxMode, 0.0f);
    const auto out = render (p, 1.2);
    CHECK (rms (out) > 1.0e-3);

    // The session keeps the stored file; another instance plays it again.
    juce::MemoryBlock session;
    p.getStateInformation (session);
    ShellProcessor q;
    q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
    auto& other = engineOf (q);
    REQUIRE (other.waitForAuxRecording (20000));
    CHECK (other.auxRecordingState().status == AnalogEngine::AuxRecordingState::Status::ready);
    CHECK (other.auxLoop() != nullptr);
    CHECK (rms (render (q, 1.2)) > 1.0e-3);
}

TEST_CASE ("AUX: a missing or unreadable recording fails beautifully and is written back as it was", "[analog][aux]")
{
    Scratch scratch;
    ShellProcessor p;
    auto& engine = engineOf (p);
    // Not audio at all.
    const auto junk = scratch.dir.getChildFile ("junk.wav");
    junk.replaceWithText ("not audio");
    engine.loadAuxRecording (junk, scratch.dir.getChildFile ("store"));
    REQUIRE (engine.waitForAuxRecording (20000));
    // (Addendum 03: a failed load leaves D as it was - here without a loop - and says why)
    CHECK (engine.auxRecordingState().status == AnalogEngine::AuxRecordingState::Status::none);
    CHECK (engine.auxRecordingState().lastError.isNotEmpty());
    CHECK (engine.auxLoop() == nullptr);
    set (p, params::auxEnabled, 1.0f);
    CHECK (rms (render (p, 0.3)) >= 0.0);   // (plays on: D silent, nothing crashes)

    // A session whose stored file is gone.
    juce::XmlElement state ("STATE");
    auto* rec = state.createNewChildElement ("AuxRecording");
    rec->setAttribute ("asset", "0123456789abcdef01234567.wav");
    rec->setAttribute ("name", "gone");
    engine.restoreState (state, scratch.dir);
    REQUIRE (engine.waitForAuxRecording (20000));
    CHECK (engine.auxRecordingState().status == AnalogEngine::AuxRecordingState::Status::missing);
    juce::XmlElement saved ("STATE");
    engine.saveState (saved);
    const auto* back = saved.getChildByName ("AuxRecording");
    REQUIRE (back != nullptr);
    CHECK (back->getStringAttribute ("asset") == "0123456789abcdef01234567.wav");
    CHECK (back->getStringAttribute ("name") == "gone");
}

TEST_CASE ("AUX: D off and no Kontakt add no latency; the strip's and D's state are absent from a session without D", "[analog][aux]")
{
    ShellProcessor p;
    auto& engine = engineOf (p);
    p.prepareToPlay (rate, block);
    const int base = engine.latencySamples();
    set (p, params::auxEnabled, 1.0f);
    set (p, params::auxMode, 2.0f);   // KONTAKT, none loaded
    CHECK (engine.latencySamples() == base);
    CHECK (engine.auxInstrument().state() == andor::analog::ExternalInstrument::State::empty);
    juce::XmlElement state ("STATE");
    engine.saveState (state);
    CHECK (state.getChildByName ("AuxRecording") == nullptr);
    CHECK (state.getChildByName ("AuxKontakt") == nullptr);
}

TEST_CASE ("AUX: D NOISE follows the musician's keys before the arpeggiator", "[analog][aux]")
{
    ShellProcessor p;
    set (p, params::auxEnabled, 1.0f);
    set (p, params::auxMode, 1.0f);    // NOISE, gated by the keys
    set (p, params::auxLevel, 0.0f);
    // Only D heard: the cards muted.
    set (p, params::auxSolo, 1.0f);
    const auto out = render (p, 1.0, 0.4);
    const int at = static_cast<int> (0.2 * rate);
    const int late = static_cast<int> (0.9 * rate);
    const double held = rms (out, at, at + 4800);
    const double after = rms (out, late, late + 4800);
    CHECK (held > 1.0e-3);
    CHECK (after < 0.1 * held);   // (released: the gate closed, its release passed)
}

TEST_CASE ("AUX: screenshots - the strip in its three modes, its rack, hidden (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][aux]")
{
    const auto dir = juce::SystemStats::getEnvironmentVariable ("ANALOG_SCREENSHOT_DIR", {});
    REQUIRE (dir.isNotEmpty());
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    engineOf (p).setAuxLoopForTest ([] {
        constexpr double beat = 60.0 / 100.0;
        const auto length = static_cast<std::size_t> (std::lround (16 * beat * rate));
        std::vector<float> x (length, 0.0f);
        for (int k = 0; k < 16; ++k)
        {
            const auto first = static_cast<std::size_t> (std::lround (k * beat * rate));
            for (std::size_t i = 0; i < static_cast<std::size_t> (0.06 * rate) && first + i < length; ++i)
                x[first + i] += static_cast<float> ((k % 4 == 0 ? 0.8 : 0.45) * std::exp (-static_cast<double> (i) / rate / 0.02));
        }
        return andor::analog::rhythm::prepareLoop (x.data(), x.data(), x.size(), rate);
    }());
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = editorOf (editor);
    auto shoot = [&] (const juce::String& name, int width) {
        const float h = ui.instrumentHeight() * static_cast<float> (width) / osp::plugin::design::width;
        editor->setSize (width, juce::roundToInt (h));
        refreshStrip (ui);
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = out.getChildFile (name + "-" + juce::String (width) + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    for (const int w : { 869, 1448, 1810 })
        shoot ("aux-00-hidden", w);
    ui.setAuxStripShown (true);
    set (p, params::auxEnabled, 1.0f);
    const char* names[] = { "rhythm", "noise", "kontakt" };
    for (int mode = 0; mode < 3; ++mode)
    {
        set (p, params::auxMode, static_cast<float> (mode));
        refreshStrip (ui);
        for (const int w : { 869, 1448, 1810 })
            shoot (juce::String ("aux-0") + juce::String (mode + 1) + "-" + names[mode], w);
    }
    set (p, params::auxMode, 0.0f);
    refreshStrip (ui);
    ui.setRackExpanded (osp::plugin::rack::Kind::aux, true);
    for (const int w : { 869, 1448, 1810 })
        shoot ("aux-04-rack-rhythm", w);
    set (p, params::auxMode, 1.0f);
    refreshStrip (ui);
    shoot ("aux-05-rack-noise", 1448);
    set (p, params::auxMode, 2.0f);
    refreshStrip (ui);
    shoot ("aux-06-rack-kontakt", 1448);
    ui.setRackExpanded (osp::plugin::rack::Kind::aux, false);
    // A loop dragged over D's strip: the well shows where it will land (only D's).
    set (p, params::auxMode, 0.0f);
    if (auto* owner = dynamic_cast<osp::plugin::DropOwner*> (ui.auxStripComponent()))
    {
        owner->dropHover ({ "loop.wav" }, true);
        shoot ("aux-08-drop-hover", 1448);
        owner->dropHover ({ "loop.wav" }, false);
    }
    // Muted, and another card soloed (D not heard: the cards' veil and reasons).
    set (p, params::auxMute, 1.0f);
    shoot ("aux-09-muted", 1448);
    set (p, params::auxMute, 0.0f);
    set (p, "layerA.solo", 1.0f);
    shoot ("aux-10-not-soloed", 1448);
    set (p, "layerA.solo", 0.0f);
    set (p, params::auxSolo, 1.0f);
    shoot ("aux-11-soloed", 1448);
    set (p, params::auxSolo, 0.0f);
    set (p, params::auxEnabled, 0.0f);
    for (const int w : { 869, 1448, 1810 })
        shoot ("aux-07-off", w);
    // Addendum 05: LIVE INPUT - the sidechain off, routed and silent, routed with audio; its rack.
    set (p, params::auxEnabled, 1.0f);
    set (p, params::auxMode, 3.0f);
    auto feed = [&] (bool enabled, float amplitude) {
        std::vector<float> l (512), r (512);
        for (int b = 0; b < 120; ++b)
        {
            for (std::size_t i = 0; i < l.size(); ++i)
            {
                const double t = (b * 512 + static_cast<double> (i)) / rate;
                const float env = amplitude * static_cast<float> (0.35 + 0.65 * std::abs (std::sin (2.0 * 3.14159 * 1.7 * t)));
                l[i] = env * static_cast<float> (std::sin (2.0 * 3.14159 * 220.0 * t));
                r[i] = 0.8f * l[i];
            }
            AnalogEngine::ExternalInput in;
            in.busExists = true;
            in.busEnabled = enabled;
            in.channels = enabled ? 2 : 0;
            in.left = enabled ? l.data() : nullptr;
            in.right = enabled ? r.data() : nullptr;
            in.samples = enabled ? 512 : 0;
            engineOf (p).setExternalInput (in);
        }
    };
    feed (false, 0.0f);
    shoot ("aux-13-live-disabled", 1448);
    feed (true, 0.0f);
    shoot ("aux-14-live-no-signal", 1448);
    feed (true, 0.5f);
    for (const int w : { 869, 1448, 1810 })
        shoot ("aux-15-live-signal", w);
    ui.setRackExpanded (osp::plugin::rack::Kind::aux, true);
    shoot ("aux-16-rack-live", 1448);
    ui.setRackExpanded (osp::plugin::rack::Kind::aux, false);
    // MORPH TO in the MORPH rack: C (as ever) and D - the global stage's bands measured after a
    // held chord against D NOISE (always on) at MORPH 50 %.
    set (p, params::auxMode, 1.0f);
    set (p, params::auxNoiseGate, 1.0f);
    set (p, params::morphEnabled, 1.0f);
    set (p, params::morphPosition, 50.0f);
    ui.setRackExpanded (osp::plugin::rack::Kind::morph, true);
    shoot ("aux-17-morph-to-c", 1448);
    set (p, params::morphTarget, 1.0f);
    render (p, 1.0, 2.0);
    shoot ("aux-18-morph-to-d", 1448);
    set (p, params::auxMode, 3.0f);
    shoot ("aux-19-morph-to-d-live", 1448);
    ui.setRackExpanded (osp::plugin::rack::Kind::morph, false);
    set (p, params::morphTarget, 0.0f);
    editor.reset();

    // D off with no loop yet: the reason in the well's top row, clear of the picture's
    // "DROP A LOOP OR CLICK TO LOAD".
    ShellProcessor fresh;
    std::unique_ptr<juce::AudioProcessorEditor> second (fresh.createEditorIfNeeded());
    auto& ui2 = editorOf (second);
    ui2.setAuxStripShown (true);
    set (fresh, params::auxEnabled, 0.0f);
    for (const int w : { 869, 1448, 1810 })
    {
        const float h = ui2.instrumentHeight() * static_cast<float> (w) / osp::plugin::design::width;
        second->setSize (w, juce::roundToInt (h));
        refreshStrip (ui2);
        const auto image = second->createComponentSnapshot (second->getLocalBounds(), true, 1.0f);
        const auto file = out.getChildFile ("aux-12-off-no-loop-" + juce::String (w) + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    }
}

TEST_CASE ("AUX: renders - D under a FILTER sweep, through SPACE / ECHO, with card C, and D off nulling (ANALOG_RENDER_DIR)", "[.][renders][aux]")
{
    const auto dir = juce::SystemStats::getEnvironmentVariable ("ANALOG_RENDER_DIR", {});
    REQUIRE (dir.isNotEmpty());
    const juce::File out (dir);
    out.createDirectory();
    auto write = [&] (const juce::String& name, const juce::AudioBuffer<float>& audio) {
        const auto file = out.getChildFile (name + ".wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), rate, 2, 24, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();
        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    };
    auto peakDb = [] (const juce::AudioBuffer<float>& a) { return juce::Decibels::gainToDecibels (std::max (a.getMagnitude (0, a.getNumSamples()), 1.0e-9f)); };
    auto rmsDb = [] (const juce::AudioBuffer<float>& a, int from, int to) { return juce::Decibels::gainToDecibels (static_cast<float> (std::max (rms (a, from, to), 1.0e-9))); };
    const auto tickLoop = [] {
        constexpr double beat = 60.0 / 100.0;
        const auto length = static_cast<std::size_t> (std::lround (16 * beat * rate));
        std::vector<float> x (length, 0.0f);
        juce::Random noise (7);
        for (int k = 0; k < 16; ++k)
        {
            const auto first = static_cast<std::size_t> (std::lround (k * beat * rate));
            for (std::size_t i = 0; i < static_cast<std::size_t> (0.08 * rate) && first + i < length; ++i)
                x[first + i] += static_cast<float> ((k % 4 == 0 ? 0.7 : 0.4) * std::exp (-static_cast<double> (i) / rate / 0.015) * (noise.nextFloat() * 2.0f - 1.0f));
        }
        return andor::analog::rhythm::prepareLoop (x.data(), x.data(), x.size(), rate);
    }();

    // 1. D NOISE alone (soloed), held, the FILTER macro swept from closed to open over 4 s.
    {
        ShellProcessor p;
        set (p, params::auxEnabled, 1.0f);
        set (p, params::auxMode, 1.0f);
        set (p, params::auxNoiseGate, 1.0f);   // FREE
        set (p, params::auxSolo, 1.0f);
        set (p, params::auxLevel, 0.0f);
        p.prepareToPlay (rate, block);
        const int total = static_cast<int> (4.0 * rate);
        juce::AudioBuffer<float> audio (2, total), buffer (2, block);
        auto* tone = p.parameters.getParameter (params::tone);
        for (int pos = 0; pos < total; pos += block)
        {
            tone->setValueNotifyingHost (0.1f + 0.85f * static_cast<float> (pos) / static_cast<float> (total));
            juce::MidiBuffer none;
            p.processBlock (buffer, none);
            const int n = std::min (block, total - pos);
            for (int ch = 0; ch < 2; ++ch)
                audio.copyFrom (ch, pos, buffer, ch, 0, n);
        }
        write ("aux-01-noise-filter-sweep", audio);
        const float early = rmsDb (audio, static_cast<int> (0.2 * rate), static_cast<int> (0.7 * rate));
        const float late = rmsDb (audio, static_cast<int> (3.3 * rate), static_cast<int> (3.8 * rate));
        WARN ("FILTER sweep on D NOISE: " << early << " dB (closed) -> " << late << " dB (open), peak " << peakDb (audio) << " dBFS");
        CHECK (late > early + 6.0f);
    }
    // 2. D RHYTHM through SPACE and ECHO (macros up), soloed: the effects carry it.
    {
        ShellProcessor p;
        engineOf (p).setAuxLoopForTest (tickLoop);
        set (p, params::auxEnabled, 1.0f);
        set (p, params::auxMode, 0.0f);
        set (p, params::auxRhythmGate, 1.0f);   // (GATE FREE: on the grid, no keys needed)
        set (p, params::auxSolo, 1.0f);
        set (p, "space", 70.0f);
        set (p, "echo", 60.0f);
        const auto audio = render (p, 6.0, 5.0);
        write ("aux-02-rhythm-space-echo", audio);
        WARN ("D RHYTHM + SPACE / ECHO: " << rmsDb (audio, 0, audio.getNumSamples()) << " dB RMS, peak " << peakDb (audio) << " dBFS");
    }
    // 3. Cards A / B / C (C NOISE) and D RHYTHM together.
    {
        ShellProcessor p;
        engineOf (p).setAuxLoopForTest (tickLoop);
        set (p, params::hybridType, 2.0f);
        set (p, params::auxEnabled, 1.0f);
        set (p, params::auxMode, 0.0f);
        set (p, params::auxRhythmGate, 1.0f);   // (GATE FREE: on the grid, no keys needed)
        const auto audio = render (p, 6.0, 5.0);
        write ("aux-03-cards-and-d", audio);
        WARN ("A/B/C + D: peak " << peakDb (audio) << " dBFS");
    }
    // 4. D off (never powered) against the instrument with D merely configured but off: null.
    {
        ShellProcessor a, b;
        engineOf (b).setAuxLoopForTest (tickLoop);
        set (b, params::auxMode, 1.0f);
        set (b, params::auxLevel, 6.0f);
        const auto x = render (a, 2.0), y = render (b, 2.0);
        juce::AudioBuffer<float> diff (2, x.getNumSamples());
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < x.getNumSamples(); ++i)
                diff.setSample (ch, i, x.getSample (ch, i) - y.getSample (ch, i));
        write ("aux-04-legacy-null-difference", diff);
        WARN ("D off null: max difference " << diff.getMagnitude (0, diff.getNumSamples()));
        CHECK (identical (x, y));
    }
}


//==============================================================================
// AUX Refinement Addendum 03.

namespace
{
    /** A drop as the host delivers it: on the editor, at a point of `target` (its centre by
        default), through the editor's own drop handling (the overlays above the parts and all). */
    void dropOn (andor::shell::ShellEditor& ui, juce::Component& target, const juce::File& file, juce::Point<int> at = { -1, -1 })
    {
        if (at.x < 0)
            at = target.getLocalBounds().getCentre();
        const auto p = ui.getLocalPoint (&target, at);
        const juce::StringArray files { file.getFullPathName() };
        REQUIRE (ui.isInterestedInFileDrag (files));
        ui.fileDragEnter (files, p.x, p.y);
        ui.fileDragMove (files, p.x, p.y);
        ui.filesDropped (files, p.x, p.y);
    }

    juce::String auxName (ShellProcessor& p) { return engineOf (p).auxRecordingState().name; }

    struct DropRig
    {
        Scratch scratch;
        ShellProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor { p.createEditorIfNeeded() };
        andor::shell::ShellEditor& ui = editorOf (editor);
        juce::File loopA = writeLoop (scratch.dir, "aux-loop.wav"), loopB = writeLoop (scratch.dir, "card-c-loop.wav");
        DropRig()
        {
            editor->setSize (1448, 1086);
            ui.setAuxStripShown (true);
            ui.refreshNow();
        }
        bool cardC() { p.waitForLoads (20000); p.pollLoads(); return p.currentInstrument (1) != nullptr; }
    };
}

TEST_CASE ("AUX 03 gate 1: a file dropped on D (strip, its well, the rack) loads D only; one dropped on card C loads C only", "[analog][aux][drop]")
{
    DropRig rig;
    auto& strip = *rig.ui.auxStripComponent();
    auto& well = *AuxStripView::part (strip, "display");
    // 1. The strip's well: D, never C.
    dropOn (rig.ui, well, rig.loopA);
    REQUIRE (engineOf (rig.p).waitForAuxRecording (20000));
    CHECK (auxName (rig.p) == "aux-loop");
    CHECK_FALSE (rig.cardC());
    // 2. Elsewhere on the strip (beside its knobs): D's too.
    dropOn (rig.ui, strip, rig.loopB, { strip.getWidth() - 4, 4 });
    REQUIRE (engineOf (rig.p).waitForAuxRecording (20000));
    CHECK (auxName (rig.p) == "card-c-loop");
    CHECK_FALSE (rig.cardC());
    // 3. The AUX rack (its graph and the rest of it): D.
    rig.ui.setRackExpanded (osp::plugin::rack::Kind::aux, true);
    rig.ui.refreshNow();
    juce::Component* rack = nullptr;
    std::function<void (juce::Component&)> find = [&] (juce::Component& c) {
        for (auto* child : c.getChildren())
        {
            if (dynamic_cast<osp::plugin::DropOwner*> (child) != nullptr && child->isVisible())
                rack = child;
            find (*child);
        }
    };
    find (rig.ui.effectsRack());
    REQUIRE (rack != nullptr);
    dropOn (rig.ui, *rack, rig.loopA);
    REQUIRE (engineOf (rig.p).waitForAuxRecording (20000));
    CHECK (auxName (rig.p) == "aux-loop");
    CHECK_FALSE (rig.cardC());
    // 4. Card C's well: C, and D unchanged.
    rig.ui.setRackExpanded (osp::plugin::rack::Kind::aux, false);
    rig.ui.refreshNow();
    dropOn (rig.ui, rig.ui.engineCard (1).display(), rig.loopB);
    CHECK (rig.cardC());
    CHECK (auxName (rig.p) == "aux-loop");
    // 5. Hidden and shown again, the rack opened and closed: the same ownership.
    rig.ui.setAuxStripShown (false);
    rig.ui.setAuxStripShown (true);
    rig.ui.refreshNow();
    dropOn (rig.ui, *AuxStripView::part (*rig.ui.auxStripComponent(), "display"), rig.loopB);
    REQUIRE (engineOf (rig.p).waitForAuxRecording (20000));
    CHECK (auxName (rig.p) == "card-c-loop");
    // 6. Saved and reloaded: each its own.
    juce::MemoryBlock session;
    rig.p.getStateInformation (session);
    ShellProcessor q;
    q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
    REQUIRE (engineOf (q).waitForAuxRecording (20000));
    CHECK (auxName (q) == "card-c-loop");
    q.waitForLoads (20000);
    q.pollLoads();
    CHECK (q.currentInstrument (1) != nullptr);
}

TEST_CASE ("AUX 03: a file dropped on empty space (neither a card nor D) loads nothing", "[analog][aux][drop]")
{
    DropRig rig;
    // The macro panel, the header and the keyboard: none owns drops, none is a card.
    dropOn (rig.ui, rig.ui.modulationPanel(), rig.loopA);
    dropOn (rig.ui, rig.ui, rig.loopA, { rig.ui.getWidth() / 2, 12 });
    rig.p.waitForLoads (5000);
    rig.p.pollLoads();
    CHECK_FALSE (rig.cardC());
    CHECK (engineOf (rig.p).auxRecordingState().status == AnalogEngine::AuxRecordingState::Status::none);
    CHECK (engineOf (rig.p).auxLoop() == nullptr);
    // ... while card C's own well still takes one.
    dropOn (rig.ui, rig.ui.engineCard (1).display(), rig.loopB);
    CHECK (rig.cardC());
}

TEST_CASE ("MORPH TO (Addendum 05): C by default, MORPH as ever; D - the MORPH macro drives the global stage, the per-voice morph stands aside; D soloed: D itself", "[analog][aux][morph]")
{
    ShellProcessor p;
    auto* target = p.parameters.getParameter (params::morphTarget);
    REQUIRE (target != nullptr);
    CHECK (target->getDefaultValue() == 0.0f);   // (C HYBRID: every older session as it was)
    CHECK (params::engineParameterIds().contains (params::morphTarget));
    // The old D MORPH knob: kept for older experimental sessions, unread, no destination.
    CHECK_FALSE (params::engineParameterIds().contains (params::auxMorph));
    for (const auto& d : params::modulationDestinations())
        CHECK (juce::String (d.id) != params::auxMorph);
    // The reader: one processor at a time.
    auto patchOf = [&] {
        andor::analog::params::Reader::Values raw {};
        andor::analog::params::Reader reader;
        reader.attach (p.parameters);
        reader.snapshot (raw);
        return reader.read (raw);
    };
    set (p, params::morphEnabled, 1.0f);
    set (p, params::morphPosition, 40.0f);
    {
        const auto c = patchOf();
        CHECK_FALSE (c.morph.toAux);
        CHECK (c.morph.perVoiceOn());
        CHECK (c.aux.morph == 0.0);
    }
    set (p, params::morphTarget, 1.0f);
    {
        const auto d = patchOf();
        CHECK (d.morph.toAux);
        CHECK_FALSE (d.morph.perVoiceOn());   // (the per-voice morph stands aside...)
        CHECK (d.morph.enabled);              // (...its own settings untouched)
        CHECK (d.aux.morph == Catch::Approx (0.4));
    }
    set (p, params::morphEnabled, 0.0f);
    CHECK (patchOf().aux.morph == 0.0);   // (MORPH off: nothing)
    set (p, params::morphEnabled, 1.0f);
    set (p, params::auxEnabled, 1.0f);
    set (p, params::auxSolo, 1.0f);
    CHECK (patchOf().aux.morph == 0.0);   // (D soloed: D itself, not a morph of nothing)
    set (p, params::auxSolo, 0.0f);
    // Heard: D NOISE (always on) with a held chord - MORPH TO D at 100 % is not the layered sound;
    // back to C, the layered sound again (the global stage at 0 % never runs).
    set (p, params::morphTarget, 0.0f);
    set (p, params::morphEnabled, 0.0f);
    set (p, params::auxMode, 1.0f);
    set (p, params::auxNoiseGate, 1.0f);
    const auto layered = render (p, 0.6);
    set (p, params::morphEnabled, 1.0f);
    set (p, params::morphPosition, 100.0f);
    set (p, params::morphTarget, 1.0f);
    CHECK_FALSE (identical (layered, render (p, 0.6)));
    set (p, params::morphEnabled, 0.0f);
    CHECK (identical (layered, render (p, 0.6)));
}

TEST_CASE ("AUX 03 gate 5: an unsupported file dropped on D changes nothing and says why; a failed load keeps the loop in use", "[analog][aux][drop]")
{
    DropRig rig;
    auto& well = *AuxStripView::part (*rig.ui.auxStripComponent(), "display");
    dropOn (rig.ui, well, rig.loopA);
    REQUIRE (engineOf (rig.p).waitForAuxRecording (20000));
    const auto before = engineOf (rig.p).auxLoop();
    REQUIRE (before != nullptr);
    // Not audio at all (a text file): refused by D, never handed to C.
    const auto text = rig.scratch.dir.getChildFile ("notes.txt");
    text.replaceWithText ("hello");
    dropOn (rig.ui, well, text);
    CHECK (engineOf (rig.p).auxLoop() == before);
    CHECK (auxName (rig.p) == "aux-loop");
    CHECK_FALSE (rig.cardC());
    // A .wav that is not one: the load fails; the loop in use plays on, the reason is kept.
    const auto broken = rig.scratch.dir.getChildFile ("broken.wav");
    broken.replaceWithText ("not audio");
    dropOn (rig.ui, well, broken);
    REQUIRE (engineOf (rig.p).waitForAuxRecording (20000));
    CHECK (engineOf (rig.p).auxLoop() == before);
    CHECK (auxName (rig.p) == "aux-loop");
    CHECK (engineOf (rig.p).auxRecordingState().status == AnalogEngine::AuxRecordingState::Status::ready);
    CHECK (engineOf (rig.p).auxRecordingState().lastError.isNotEmpty());
    CHECK_FALSE (rig.cardC());
}

TEST_CASE ("AUX 03 gates 3-4 / Addendum 04: one fixed strip layout in every mode - SOURCE MODE and LEVEL after power, the display after LEVEL to B's well's right edge, RACK beside the menu", "[analog][aux][ui]")
{
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = editorOf (editor);
    editor->setSize (1448, 1086);
    ui.setAuxStripShown (true);
    ui.refreshNow();
    auto& strip = *ui.auxStripComponent();
    auto* well = AuxStripView::part (strip, "display");
    const auto wellB = ui.cardDisplayBounds (2);
    const auto wellD = well->getBounds() + strip.getPosition();
    // (Addendum 04: from just after LEVEL to B's well's right edge, aligned to the cards)
    CHECK (wellD.getRight() == wellB.getRight());
    CHECK (wellD.getX() <= wellB.getX());
    // M / S: the cards' own key (osp::plugin::LayerStateButton), the cards' size.
    auto* mute = dynamic_cast<osp::plugin::LayerStateButton*> (AuxStripView::part (strip, "mute"));
    auto* solo = dynamic_cast<osp::plugin::LayerStateButton*> (AuxStripView::part (strip, "solo"));
    REQUIRE (mute != nullptr);
    REQUIRE (solo != nullptr);
    CHECK (mute->getWidth() == 27);
    CHECK (mute->getHeight() == 21);
    set (p, params::auxMute, 1.0f);
    CHECK (mute->isOn());
    set (p, params::auxSolo, 1.0f);
    CHECK (solo->isOn());
    // RACK: at the right end, immediately left of the menu; not over the well.
    auto* rack = AuxStripView::part (strip, "rack");
    auto* menu = AuxStripView::part (strip, "menu");
    CHECK (rack->getRight() < menu->getX());
    CHECK (menu->getX() - rack->getRight() <= 10);
    CHECK (rack->getX() > well->getRight());
    CHECK_FALSE (well->getBounds().intersects (rack->getBounds()));
    // The knobs in C's zone, before RACK, none overlapping it.
    for (const auto& caption : AuxStripView::knobCaptions (strip))
    {
        auto* k = AuxStripView::part (strip, caption);
        CHECK (k->getX() >= well->getRight());
        CHECK (k->getRight() <= rack->getX());
    }
    // Addendum 04: D, M / S, power, SOURCE MODE, LEVEL, the display, RACK and the menu - every
    // permanent part - at exactly the same bounds in RHYTHM, NOISE and KONTAKT; only the mode's
    // knobs change. Left to right: power < SOURCE MODE < LEVEL < the display < the knobs < RACK.
    const char* permanent[] = { "mute", "solo", "power", "mode", "LEVEL", "display", "rack", "menu" };
    std::vector<std::vector<juce::Rectangle<int>>> placed;
    for (int mode = 0; mode < 3; ++mode)
    {
        set (p, params::auxMode, static_cast<float> (mode));
        refreshStrip (ui);
        std::vector<juce::Rectangle<int>> b;
        for (const auto* name : permanent)
            b.push_back (AuxStripView::part (strip, name)->getBounds());
        placed.push_back (b);
        auto* power = AuxStripView::part (strip, "power");
        auto* selector = AuxStripView::part (strip, "mode");
        auto* level = AuxStripView::part (strip, "LEVEL");
        INFO ("mode " << mode);
        CHECK (selector->getX() > power->getRight());
        CHECK (selector->getX() - power->getRight() <= 16);   // (directly after power)
        CHECK (level->getX() >= selector->getRight());
        CHECK (level->getX() - selector->getRight() <= 10);   // (directly after the mode)
        CHECK (well->getX() >= level->getRight());
        CHECK (well->getX() - level->getRight() <= 16);       // (the display directly after LEVEL)
        CHECK_FALSE (selector->getBounds().intersects (well->getBounds()));   // (never inside it)
        for (const auto& caption : AuxStripView::knobCaptions (strip))
        {
            auto* k = AuxStripView::part (strip, caption);
            CHECK (k->getX() >= well->getRight());
            CHECK (k->getRight() <= rack->getX());
        }
    }
    for (std::size_t i = 0; i < std::size (permanent); ++i)
    {
        INFO (permanent[i]);
        CHECK (placed[0][i] == placed[1][i]);
        CHECK (placed[1][i] == placed[2][i]);
    }
}

TEST_CASE ("AUX 03 gate 2: D always goes through the FILTER - the old switch is unread and hidden; ROUTE is gone", "[analog][aux]")
{
    ShellProcessor p;
    set (p, params::auxEnabled, 1.0f);
    set (p, params::auxMode, 1.0f);
    set (p, params::auxNoiseGate, 1.0f);
    set (p, params::auxSolo, 1.0f);
    set (p, params::tone, 10.0f);
    const auto closed = render (p, 0.5);
    set (p, params::tone, 95.0f);
    const auto open = render (p, 0.5);
    CHECK (rms (open, 12000, 24000) > 3.0 * rms (closed, 12000, 24000));   // (the FILTER macro moves D)
    // An older experimental session with AROUND: through the FILTER now, as every other.
    set (p, params::tone, 10.0f);
    set (p, params::auxFilterFollow, 0.0f);
    CHECK (identical (closed, render (p, 0.5)));
    CHECK_FALSE (params::engineParameterIds().contains (params::auxFilterFollow));   // (not offered to a host)
}

TEST_CASE ("AUX 03 gate 6: PITCH and OCT are host parameters at 0; OCT +1 is PITCH +12; they move D RHYTHM", "[analog][aux][pitch]")
{
    const auto loop = [] {
        std::vector<float> x (static_cast<std::size_t> (2.0 * rate), 0.0f);
        for (int k = 0; k < 4; ++k)
            for (std::size_t i = 0; i < static_cast<std::size_t> (0.03 * rate); ++i)
                x[static_cast<std::size_t> (k * 0.5 * rate) + i] = static_cast<float> (0.6 * std::exp (-static_cast<double> (i) / rate / 0.006)
                                                                                         * std::sin (2.0 * juce::MathConstants<double>::pi * 1500.0 * static_cast<double> (i) / rate));
        return andor::analog::rhythm::prepareLoop (x.data(), x.data(), x.size(), rate);
    }();
    ShellProcessor a, b, c;
    for (auto* p : { &a, &b, &c })
    {
        engineOf (*p).setAuxLoopForTest (loop);
        set (*p, params::auxEnabled, 1.0f);
        set (*p, params::auxMode, 0.0f);
        set (*p, params::auxSolo, 1.0f);
    }
    REQUIRE (a.parameters.getParameter (params::auxPitch) != nullptr);
    REQUIRE (a.parameters.getParameter (params::auxOctave) != nullptr);
    CHECK (a.parameters.getParameter (params::auxPitch)->getDefaultValue() == Catch::Approx (0.5f));   // (0 st)
    CHECK (a.parameters.getParameter (params::auxOctave)->getDefaultValue() == Catch::Approx (0.5f));  // (0 oct)
    set (a, params::auxPitch, 12.0f);
    set (b, params::auxOctave, 1.0f);
    const auto up = render (a, 1.2);
    CHECK (identical (up, render (b, 1.2)));   // (one transposition, however it is set)
    CHECK_FALSE (identical (up, render (c, 1.2)));
    CHECK (params::engineParameterIds().contains (params::auxPitch));
    CHECK (params::engineParameterIds().contains (params::auxOctave));
}

TEST_CASE ("AUX 03 renders: D RHYTHM at PITCH -12 / 0 / +12, OCT -1 / +1, RATE 2x with PITCH +5, and PITCH automated (ANALOG_RENDER_DIR)", "[.][renders][aux03]")
{
    const auto dir = juce::SystemStats::getEnvironmentVariable ("ANALOG_RENDER_DIR", {});
    REQUIRE (dir.isNotEmpty());
    const juce::File out (dir);
    out.createDirectory();
    // A 100 BPM loop: a tonal hit on each beat (a 440 Hz pluck), a noise tick on the off-beats.
    const auto loop = [] {
        constexpr double beat = 0.6;
        std::vector<float> x (static_cast<std::size_t> (8 * beat * rate), 0.0f);
        juce::Random noise (3);
        for (int k = 0; k < 16; ++k)
        {
            const auto first = static_cast<std::size_t> (k * beat * 0.5 * rate);
            for (std::size_t i = 0; i < static_cast<std::size_t> (0.25 * rate) && first + i < x.size(); ++i)
            {
                const double t = static_cast<double> (i) / rate;
                x[first + i] += static_cast<float> (k % 2 == 0 ? 0.5 * std::exp (-t / 0.08) * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * t)
                                                              : 0.25 * std::exp (-t / 0.01) * (noise.nextFloat() * 2.0f - 1.0f));
            }
        }
        return andor::analog::rhythm::prepareLoop (x.data(), x.data(), x.size(), rate);
    }();
    auto renderWith = [&] (const juce::String& name, float pitch, float octave, float rateIndex, bool automate) {
        ShellProcessor p;
        engineOf (p).setAuxLoopForTest (loop);
        set (p, params::auxEnabled, 1.0f);
        set (p, params::auxMode, 0.0f);
        set (p, params::auxRhythmGate, 1.0f);   // (GATE FREE: on the grid, no keys needed)
        set (p, params::auxSolo, 1.0f);
        set (p, params::auxLevel, 0.0f);
        set (p, params::tone, 95.0f);
        set (p, params::auxPitch, pitch);
        set (p, params::auxOctave, octave);
        set (p, params::auxRate, rateIndex);
        p.prepareToPlay (rate, block);
        const int total = static_cast<int> (6.0 * rate);
        juce::AudioBuffer<float> audio (2, total), buffer (2, block);
        for (int pos = 0; pos < total; pos += block)
        {
            if (automate)
                set (p, params::auxPitch, -12.0f + 24.0f * static_cast<float> (pos) / static_cast<float> (total));
            juce::MidiBuffer none;
            p.processBlock (buffer, none);
            const int n = std::min (block, total - pos);
            for (int ch = 0; ch < 2; ++ch)
                audio.copyFrom (ch, pos, buffer, ch, 0, n);
        }
        const auto file = out.getChildFile (name + ".wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), rate, 2, 24, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();
        writer->writeFromAudioSampleBuffer (audio, 0, total);
        // Where the beats start (above -30 dBFS after 40 ms of quiet): the grid, whatever the pitch.
        std::vector<double> starts;
        int quiet = 1 << 30;
        for (int i = 0; i < total; ++i)
        {
            const float v = std::abs (audio.getSample (0, i));
            if (v > 0.03f && quiet > static_cast<int> (0.04 * rate))
                starts.push_back (i / rate);
            quiet = v > 0.03f ? 0 : quiet + 1;
        }
        juce::String line;
        for (std::size_t k = 0; k < std::min<std::size_t> (6, starts.size()); ++k)
            line << juce::String (starts[k], 3) << " ";
        WARN (name << ": peak " << juce::Decibels::gainToDecibels (audio.getMagnitude (0, total)) << " dBFS; first hits at " << line << "s");
        return starts;
    };
    const auto plain = renderWith ("aux03-01-pitch-0", 0.0f, 0.0f, 2.0f, false);
    for (const auto& [name, pitch, octave] : { std::tuple<const char*, float, float> { "aux03-02-pitch-minus12", -12.0f, 0.0f },
                                               { "aux03-03-pitch-plus12", 12.0f, 0.0f },
                                               { "aux03-04-oct-minus1", 0.0f, -1.0f },
                                               { "aux03-05-oct-plus1", 0.0f, 1.0f } })
    {
        const auto starts = renderWith (name, pitch, octave, 2.0f, false);
        REQUIRE (starts.size() >= 3);
        // Every hit heard lands on the unpitched loop's grid (a slower slice's tail may hide the
        // quiet before the next hit from this detector - never move the hit).
        for (const double t : starts)
        {
            double nearest = 1.0e9;
            for (const double q : plain)
                nearest = std::min (nearest, std::abs (q - t));
            INFO (name << ": hit at " << t << " s");
            CHECK (nearest < 0.006);
        }
    }
    renderWith ("aux03-06-rate-2x-pitch-plus5", 5.0f, 0.0f, 3.0f, false);
    renderWith ("aux03-07-pitch-automated", 0.0f, 0.0f, 2.0f, true);
}

TEST_CASE ("AUX D MORPH renders: a chord on the cards x D RHYTHM at D MORPH 0 / 50 / 100 %, and swept (ANALOG_RENDER_DIR)", "[.][renders][dmorph]")
{
    const auto dir = juce::SystemStats::getEnvironmentVariable ("ANALOG_RENDER_DIR", {});
    REQUIRE (dir.isNotEmpty());
    const juce::File out (dir);
    out.createDirectory();
    const auto loop = [] {
        constexpr double beat = 0.5;
        std::vector<float> x (static_cast<std::size_t> (8 * beat * rate), 0.0f);
        juce::Random noise (5);
        for (int k = 0; k < 16; ++k)
        {
            const auto first = static_cast<std::size_t> (k * beat * 0.5 * rate);
            for (std::size_t i = 0; i < static_cast<std::size_t> (0.2 * rate) && first + i < x.size(); ++i)
                x[first + i] += static_cast<float> ((k % 4 == 0 ? 0.6 : 0.3) * std::exp (-static_cast<double> (i) / rate / (k % 2 == 0 ? 0.05 : 0.012))
                                                    * (noise.nextFloat() * 2.0f - 1.0f));
        }
        return andor::analog::rhythm::prepareLoop (x.data(), x.data(), x.size(), rate);
    }();
    auto renderWith = [&] (const juce::String& name, float morph, bool sweep) {
        ShellProcessor p;
        engineOf (p).setAuxLoopForTest (loop);
        set (p, params::auxEnabled, 1.0f);
        set (p, params::auxMode, 0.0f);
        set (p, params::auxRhythmGate, 1.0f);   // (GATE FREE: on the grid, no keys needed)
        set (p, params::auxLevel, -3.0f);
        set (p, params::morphTarget, 1.0f);
        set (p, params::morphEnabled, 1.0f);
        set (p, params::morphPosition, morph);
        p.prepareToPlay (rate, block);
        const int total = static_cast<int> (6.0 * rate);
        juce::AudioBuffer<float> audio (2, total), buffer (2, block);
        for (int pos = 0; pos < total; pos += block)
        {
            juce::MidiBuffer midi;
            if (pos == 0)
                for (const int note : { 48, 55, 60, 64 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)), 0);
            if (sweep)
                set (p, params::morphPosition, 100.0f * static_cast<float> (pos) / static_cast<float> (total));
            p.processBlock (buffer, midi);
            const int n = std::min (block, total - pos);
            for (int ch = 0; ch < 2; ++ch)
                audio.copyFrom (ch, pos, buffer, ch, 0, n);
        }
        const auto file = out.getChildFile (name + ".wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), rate, 2, 24, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();
        writer->writeFromAudioSampleBuffer (audio, 0, total);
        WARN (name << ": peak " << juce::Decibels::gainToDecibels (audio.getMagnitude (0, total)) << " dBFS, RMS "
                   << juce::Decibels::gainToDecibels (static_cast<float> (rms (audio))) << " dB");
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < total; ++i)
                REQUIRE (std::isfinite (audio.getSample (ch, i)));
    };
    renderWith ("dmorph-01-off", 0.0f, false);
    renderWith ("dmorph-02-half", 50.0f, false);
    renderWith ("dmorph-03-full", 100.0f, false);
    renderWith ("dmorph-04-sweep", 0.0f, true);
}

TEST_CASE ("AUX D RHYTHM GATE: KEYS by default - silent until a key, then the loop from its first beat; FREE always plays; RATE and the GATE are in the rack", "[analog][aux][keys]")
{
    const auto loop = [] {
        constexpr double beat = 0.5;
        std::vector<float> x (static_cast<std::size_t> (8 * beat * rate), 0.0f);
        for (int k = 0; k < 8; ++k)
            for (std::size_t i = 0; i < static_cast<std::size_t> (0.04 * rate); ++i)
                x[static_cast<std::size_t> (k * beat * rate) + i] += static_cast<float> (0.6 * std::exp (-static_cast<double> (i) / rate / 0.01));
        return andor::analog::rhythm::prepareLoop (x.data(), x.data(), x.size(), rate);
    }();
    auto play = [&] (ShellProcessor& p, double seconds, double keyAt) {
        p.prepareToPlay (rate, block);
        const int total = static_cast<int> (seconds * rate), key = keyAt < 0.0 ? -1 : static_cast<int> (keyAt * rate);
        juce::AudioBuffer<float> out (2, total), buffer (2, block);
        for (int pos = 0; pos < total; pos += block)
        {
            juce::MidiBuffer midi;
            if (key >= pos && key < pos + block)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), key - pos);
            p.processBlock (buffer, midi);
            const int n = std::min (block, total - pos);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, buffer, ch, 0, n);
        }
        return out;
    };
    auto setUp = [&] (ShellProcessor& p) {
        engineOf (p).setAuxLoopForTest (loop);
        set (p, params::auxEnabled, 1.0f);
        set (p, params::auxMode, 0.0f);
        set (p, params::auxSolo, 1.0f);   // (only D heard)
        set (p, params::auxLevel, 0.0f);
    };
    {
        ShellProcessor p;
        setUp (p);
        CHECK (p.parameters.getParameter (params::auxRhythmGate)->getValue() == 0.0f);   // KEYS
        const auto out = play (p, 1.4, 0.6);
        const int key = static_cast<int> (0.6 * rate);
        CHECK (out.getMagnitude (0, 0, key - block) == 0.0f);                       // no key: silent
        CHECK (out.getMagnitude (0, key, static_cast<int> (0.05 * rate)) > 0.05f);   // the first beat at the key
    }
    {
        ShellProcessor p;
        setUp (p);
        set (p, params::auxRhythmGate, 1.0f);   // FREE
        const auto out = play (p, 0.6, -1.0);
        CHECK (out.getMagnitude (0, 0, out.getNumSamples()) > 0.05f);   // plays with no key at all
    }
    // The rack shows the GATE beside the CLOCK and RATE, in RHYTHM.
    ShellProcessor p;
    setUp (p);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto rack = andor::analog::createAuxRack (dynamic_cast<osp::plugin::InstrumentHost&> (p));
    REQUIRE (rack != nullptr);
    rack->setSize (1400, 300);
    bool gate = false, rateKnob = false;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
        for (auto* child : c.getChildren())
        {
            const auto tags = osp::plugin::binding::tagsOf (*child);
            gate = gate || child->getTitle() == "GATE";   // (a segment switch: its caption)
            rateKnob = rateKnob || tags.contains (params::auxRate);
            walk (*child);
        }
    };
    walk (*rack);
    CHECK (gate);
    CHECK (rateKnob);
}

//==============================================================================
// Addendum 05: D LIVE INPUT - the host's sidechain input.

namespace
{
    /** One block through the processor with `input` on the sidechain (the bus enabled when
        `enabled`), the output's first `n` samples returned. */
    juce::AudioBuffer<float> sidechainBlocks (juce::AudioProcessor& p, double seconds, const std::function<float (int)>& input, bool enabled,
                                              float junk = 0.0f)
    {
        auto layout = p.getBusesLayout();
        if (layout.inputBuses.size() > 0)
            layout.inputBuses.getReference (0) = enabled ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
        p.setBusesLayout (layout);
        p.prepareToPlay (rate, block);
        const int total = static_cast<int> (seconds * rate);
        const int channels = std::max (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
        juce::AudioBuffer<float> out (2, total), buffer (channels, block);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            buffer.setSize (channels, n, false, false, true);
            // What the host leaves in the buffer: the sidechain's audio on the input channels
            // when routed; otherwise whatever was there (junk: never to be heard as input).
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < n; ++i)
                    buffer.setSample (ch, i, enabled ? input (pos + i) : junk);
            p.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, buffer, ch, 0, n);
        }
        return out;
    }
}

TEST_CASE ("AUX LIVE INPUT: the plugin keeps one optional sidechain bus (off by default), stays an instrument, and plays it in D with no MIDI", "[analog][aux][live]")
{
    ShellProcessor p;
    // The bus: one input, named for D, off until the host routes to it; stereo output as ever.
    REQUIRE (p.getBusCount (true) == 1);
    CHECK (p.getBus (true, 0)->getName() == "AUX D Live Input");
    CHECK_FALSE (p.getBus (true, 0)->isEnabledByDefault());
    CHECK (p.getBusCount (false) == 1);
    CHECK (p.acceptsMidi());
    CHECK (p.hasExternalInput());
    // VST3: an auxiliary (sidechain) bus, never the main input - the plugin stays an instrument.
    auto* vst3 = p.getVST3ClientExtensions();
    REQUIRE (vst3 != nullptr);
    CHECK_FALSE (vst3->getPluginHasMainInput());
    // The layouts it takes: input off, mono or stereo; never a stereo output lost.
    auto layout = p.getBusesLayout();
    for (const auto& in : { juce::AudioChannelSet::disabled(), juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo() })
    {
        layout.inputBuses.getReference (0) = in;
        CHECK (p.checkBusesLayoutSupported (layout));
    }
    layout.inputBuses.getReference (0) = juce::AudioChannelSet::create5point1();
    CHECK_FALSE (p.checkBusesLayoutSupported (layout));

    // D on in LIVE INPUT, only D heard: the sidechain's 330 Hz arrives with no note played.
    set (p, params::auxEnabled, 1.0f);
    set (p, params::auxMode, 3.0f);
    set (p, params::auxSolo, 1.0f);
    set (p, params::auxLevel, 0.0f);
    set (p, params::tone, 100.0f);
    auto sine = [] (int i) { return 0.3f * static_cast<float> (std::sin (2.0 * 3.141592653589793 * 330.0 * i / rate)); };
    const auto routed = sidechainBlocks (p, 1.0, sine, true);
    CHECK (rms (routed, static_cast<int> (0.4 * rate)) > 0.05);
    auto* engine = &engineOf (p);
    CHECK (engine->liveInput().busEnabled);
    CHECK (engine->liveInput().channels == 2);
    CHECK (engine->liveInput().peakLeft > 0.2f);
    // The bus off with junk left in the buffer: D hears none of it (the stale output is never input).
    const auto off = sidechainBlocks (p, 0.6, sine, false, 0.5f);
    CHECK (rms (off, static_cast<int> (0.2 * rate)) < 1.0e-5);
    CHECK_FALSE (engine->liveInput().busEnabled);
    // Another mode: the sidechain is not D's source (RHYTHM with no loop: silent).
    set (p, params::auxMode, 0.0f);
    CHECK (rms (sidechainBlocks (p, 0.6, sine, true), static_cast<int> (0.2 * rate)) < 1.0e-5);
}

TEST_CASE ("AUX LIVE INPUT: a drop is refused (nothing loaded, RHYTHM's loop kept); a click opens the rack; the rack shows the input", "[analog][aux][live][ui]")
{
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = editorOf (editor);
    ui.setAuxStripShown (true);
    set (p, params::auxEnabled, 1.0f);
    set (p, params::auxMode, 3.0f);
    refreshStrip (ui);
    auto& strip = *ui.auxStripComponent();
    CHECK (AuxStripView::shownMode (strip) == 3);
    CHECK (AuxStripView::knobCaptions (strip).isEmpty());   // (no timing / pitch controls for live audio)
    auto* owner = dynamic_cast<osp::plugin::DropOwner*> (&strip);
    REQUIRE (owner != nullptr);
    owner->dropFiles ({ "/tmp/some-loop.wav" }, {});
    CHECK (juce::roundToInt (p.parameters.getParameter (params::auxMode)->convertFrom0to1 (p.parameters.getParameter (params::auxMode)->getValue())) == 3);
    CHECK (engineOf (p).auxRecordingState().status == AnalogEngine::AuxRecordingState::Status::none);
    // A click on the display: the rack (its input and how to route it), never a file chooser.
    auto* display = AuxStripView::part (strip, "display");
    REQUIRE (display != nullptr);
    const auto at = display->getLocalBounds().getCentre().toFloat();
    display->mouseUp (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, display, display,
                                        juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false));
    CHECK (ui.effectsRack().isShown (osp::plugin::rack::Kind::aux));
}

#ifdef ANDOR_ANALOG_VST3
TEST_CASE ("AUX LIVE INPUT through ANDOR Analog's own VST3 (hosted here): an instrument with MIDI in, its sidechain bus, audio in -> D -> out", "[analog][aux][live][vst3]")
{
    // The real plugin binary through JUCE's VST3 hosting: what a DAW's scan and routing see.
    // (A DAW - Ableton, Logic, REAPER - is the owner's test; this is the closest proof here.)
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, ANDOR_ANALOG_VST3);
    REQUIRE (types.size() == 1);
    CHECK (types[0]->isInstrument);
    juce::String error;
    auto plugin = format.createInstanceFromDescription (*types[0], rate, block, error);
    INFO (error);
    REQUIRE (plugin != nullptr);
    CHECK (plugin->acceptsMidi());
    CHECK (plugin->getBusCount (false) == 1);
    REQUIRE (plugin->getBusCount (true) == 1);
    CHECK (plugin->getBus (true, 0)->getName() == "AUX D Live Input");
    auto param = [&] (const juce::String& name) -> juce::AudioProcessorParameter* {
        for (auto* q : plugin->getParameters())
            if (q->getName (64) == name)
                return q;
        return nullptr;
    };
    auto* power = param ("AUX Power");
    auto* mode = param ("AUX Mode");
    auto* solo = param ("AUX Solo");
    REQUIRE (power != nullptr);
    REQUIRE (mode != nullptr);
    REQUIRE (solo != nullptr);
    power->setValueNotifyingHost (1.0f);
    mode->setValueNotifyingHost (1.0f);   // (LIVE INPUT: the last choice)
    solo->setValueNotifyingHost (1.0f);
    auto sine = [] (int i) { return 0.3f * static_cast<float> (std::sin (2.0 * 3.141592653589793 * 330.0 * i / rate)); };
    const auto routed = sidechainBlocks (*plugin, 1.0, sine, true);
    CHECK (rms (routed, static_cast<int> (0.4 * rate)) > 0.01);
    plugin->releaseResources();
    // The bus off: whatever the host leaves in the buffer is never heard as input - a fresh
    // instance with junk there plays exactly what one with zeros does (the effects' own tail
    // from the routed sine above is why a fresh instance is used).
    auto offWith = [&] (float junk) {
        auto fresh = format.createInstanceFromDescription (*types[0], rate, block, error);
        REQUIRE (fresh != nullptr);
        for (auto* q : fresh->getParameters())
        {
            const auto name = q->getName (64);
            if (name == "AUX Power" || name == "AUX Mode" || name == "AUX Solo")
                q->setValueNotifyingHost (1.0f);
        }
        auto out = sidechainBlocks (*fresh, 0.6, sine, false, junk);
        fresh->releaseResources();
        return out;
    };
    const auto junked = offWith (0.5f), zeroed = offWith (0.0f);
    CHECK (identical (junked, zeroed));
    CHECK (rms (junked, static_cast<int> (0.2 * rate)) < 1.0e-5);
}
#endif

//==============================================================================
// Addendum 05: the six listening demos (ANALOG_RENDER_DIR). The external tracks are SYNTHESIZED
// stand-ins - a plucked string, a formant vowel voice, a bowed string, a drum kit - since no real
// recordings live in the repositories; each reaches ANDOR Analog through its sidechain bus exactly
// as a DAW track would (processBlock with the input bus enabled).
namespace
{
    struct Biquad
    {
        double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        static Biquad bandPass (double hz, double q)
        {
            Biquad f;
            const double w = 2.0 * 3.141592653589793 * hz / rate, alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
            f.b0 = alpha / a0;
            f.b2 = -alpha / a0;
            f.a1 = -2.0 * std::cos (w) / a0;
            f.a2 = (1.0 - alpha) / a0;
            return f;
        }
        double process (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    std::vector<float> pluckedString (double seconds)
    {
        // Karplus-Strong strums (E2 A2 D3 G3 B3, a little apart), one every 1.6 s.
        std::vector<float> x (static_cast<std::size_t> (seconds * rate), 0.0f);
        juce::Random r (7);
        const double notes[] { 82.41, 110.0, 146.83, 196.0, 246.94 };
        for (double at = 0.0; at < seconds; at += 1.6)
            for (int n = 0; n < 5; ++n)
            {
                const auto period = static_cast<std::size_t> (rate / notes[n]);
                std::vector<double> line (period);
                for (auto& v : line)
                    v = r.nextDouble() * 2.0 - 1.0;
                const auto start = static_cast<std::size_t> ((at + 0.025 * n) * rate);
                for (std::size_t i = 0, k = 0; start + i < x.size() && i < static_cast<std::size_t> (1.6 * rate); ++i)
                {
                    const std::size_t k1 = (k + 1) % period;
                    const double v = 0.4975 * (line[k] + line[k1]);
                    line[k] = v;
                    k = k1;
                    x[start + i] += static_cast<float> (0.12 * v);
                }
            }
        return x;
    }

    std::vector<float> vowelVoice (double seconds)
    {
        // A glottal pulse train (140 Hz, 5 Hz vibrato) through three formants, the vowel moving
        // A - E - I - O every 0.8 s (glided).
        const double formants[4][3] { { 800, 1150, 2900 }, { 400, 2100, 2800 }, { 300, 2300, 3000 }, { 450, 800, 2830 } };
        std::vector<float> x (static_cast<std::size_t> (seconds * rate));
        double phase = 0.0;
        Biquad f[3];
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            const double t = static_cast<double> (i) / rate;
            if (i % 64 == 0)
            {
                const double pos = std::fmod (t / 0.8, 4.0);
                const int a = static_cast<int> (pos), b = (a + 1) % 4;
                const double mix = std::clamp ((pos - a - 0.7) / 0.3, 0.0, 1.0);
                for (int k = 0; k < 3; ++k)
                {
                    const auto zf = f[k];
                    f[k] = Biquad::bandPass (formants[a][k] + mix * (formants[b][k] - formants[a][k]), 8.0);
                    f[k].z1 = zf.z1;
                    f[k].z2 = zf.z2;
                }
            }
            phase += (140.0 * (1.0 + 0.012 * std::sin (2.0 * 3.141592653589793 * 5.0 * t))) / rate;
            const double pulse = phase - std::floor (phase) < 0.08 ? 1.0 : 0.0;
            if (phase > 1.0)
                phase -= 1.0;
            x[i] = static_cast<float> (2.0 * (f[0].process (pulse) + 0.6 * f[1].process (pulse) + 0.3 * f[2].process (pulse)));   // (about the synth's level, as a sending track would be set)
        }
        return x;
    }

    std::vector<float> bowedString (double seconds)
    {
        // A bowed G2 (98 Hz): a sawtooth with vibrato and bow noise, slow attack, through a body.
        std::vector<float> x (static_cast<std::size_t> (seconds * rate));
        juce::Random r (9);
        double phase = 0.0;
        Biquad body[3] { Biquad::bandPass (280.0, 2.0), Biquad::bandPass (1050.0, 2.5), Biquad::bandPass (2600.0, 3.0) };
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            const double t = static_cast<double> (i) / rate;
            phase += 98.0 * (1.0 + 0.006 * std::sin (2.0 * 3.141592653589793 * 5.5 * t)) / rate;
            phase -= std::floor (phase);
            const double s = (2.0 * phase - 1.0) + 0.05 * (r.nextDouble() * 2.0 - 1.0);
            const double env = std::min (1.0, t / 0.4);
            x[i] = static_cast<float> (0.7 * env * (body[0].process (s) + body[1].process (s) + 0.5 * body[2].process (s)));
        }
        return x;
    }

    std::vector<float> drumKit (double seconds)
    {
        // 110 BPM: kick on 1 and 3, snare on 2 and 4, hats on eighths.
        std::vector<float> x (static_cast<std::size_t> (seconds * rate), 0.0f);
        juce::Random r (11);
        const double eighth = 60.0 / 110.0 / 2.0;
        for (int e = 0; e * eighth < seconds; ++e)
        {
            const auto at = static_cast<std::size_t> (e * eighth * rate);
            for (std::size_t i = 0; at + i < x.size() && i < static_cast<std::size_t> (0.35 * rate); ++i)
            {
                const double t = static_cast<double> (i) / rate;
                double v = 0.08 * (r.nextDouble() * 2.0 - 1.0) * std::exp (-t / 0.012);   // hat
                if (e % 4 == 0)
                    v += 0.7 * std::sin (2.0 * 3.141592653589793 * (50.0 * t + 70.0 * 0.03 * (1.0 - std::exp (-t / 0.03)))) * std::exp (-t / 0.18);   // kick
                if (e % 4 == 2)
                    v += (0.35 * (r.nextDouble() * 2.0 - 1.0) + 0.25 * std::sin (2.0 * 3.141592653589793 * 190.0 * t)) * std::exp (-t / 0.07);   // snare
                x[at + i] += static_cast<float> (v);
            }
        }
        return x;
    }
}

TEST_CASE ("AUX LIVE INPUT renders (Addendum 05): six demos through the sidechain - layering, FILTER, vocal imprint, bowed fusion, rhythm, mode switching (ANALOG_RENDER_DIR)", "[.][renders][live05]")
{
    const auto dir = juce::SystemStats::getEnvironmentVariable ("ANALOG_RENDER_DIR", {});
    REQUIRE (dir.isNotEmpty());
    const juce::File out (dir);
    out.createDirectory();
    juce::StringArray notes;
    // Renders `seconds` with `input` on the sidechain; `chord` held from the start (none: no
    // MIDI at all); `each` runs before every block (automation).
    auto renderLive = [&] (const juce::String& name, const juce::String& what, double seconds, const std::vector<float>& input,
                           std::vector<int> chord, std::function<void (ShellProcessor&)> setup, std::function<void (ShellProcessor&, double)> each) {
        ShellProcessor p;
        set (p, params::auxEnabled, 1.0f);
        set (p, params::auxMode, 3.0f);
        set (p, params::auxLevel, -3.0f);
        if (setup)
            setup (p);
        auto layout = p.getBusesLayout();
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        REQUIRE (p.setBusesLayout (layout));
        p.prepareToPlay (rate, block);
        const int total = static_cast<int> (seconds * rate);
        juce::AudioBuffer<float> audio (2, total), buffer (2, block);
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            buffer.setSize (2, n, false, false, true);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                    buffer.setSample (ch, i, static_cast<std::size_t> (pos + i) < input.size() ? input[static_cast<std::size_t> (pos + i)] : 0.0f);
            juce::MidiBuffer midi;
            if (pos == 0)
                for (const int note : chord)
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (96)), 0);
            if (each)
                each (p, static_cast<double> (pos) / rate);
            p.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                audio.copyFrom (ch, pos, buffer, ch, 0, n);
        }
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < total; ++i)
                REQUIRE (std::isfinite (audio.getSample (ch, i)));
        const auto file = out.getChildFile (name + ".wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), rate, 2, 16, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();
        writer->writeFromAudioSampleBuffer (audio, 0, total);
        const auto line = name + ".wav: " + what + " - peak " + juce::String (juce::Decibels::gainToDecibels (audio.getMagnitude (0, total)), 1) + " dBFS, RMS "
                          + juce::String (juce::Decibels::gainToDecibels (static_cast<float> (rms (audio))), 1) + " dB";
        notes.add (line);
        WARN (line);
        return audio;
    };
    const std::vector<int> pad { 48, 55, 60, 64 };
    auto padSound = [] (ShellProcessor& p) {
        set (p, params::tone, 70.0f);   // (the FILTER part open: the pad and D through it)
    };
    // 1. Basic layering: the pad and a plucked string together, D layered (MORPH TO C, MORPH off).
    renderLive ("live05-1-layer-guitar", "pad chord + plucked-string stand-in on the sidechain, layered", 8.0, pluckedString (8.0), pad, padSound, {});
    // 2. FILTER: the input alone (no MIDI), the FILTER macro swept closed and open again.
    renderLive ("live05-2-filter-sweep", "plucked-string stand-in alone, the FILTER macro 100 % -> 8 % -> 100 %", 8.0, pluckedString (8.0), {}, {}, [] (ShellProcessor& p, double t) {
        set (p, params::tone, static_cast<float> (100.0 - 92.0 * std::sin (3.141592653589793 * std::clamp (t / 8.0, 0.0, 1.0))));
    });
    // 3. Vocal imprint: a held saw chord morphing towards the voice (MORPH TO D, 0 -> 100 %).
    renderLive ("live05-3-vocal-imprint", "saw chord -> formant-voice stand-in, MORPH TO D swept 0 -> 100 %", 9.0, vowelVoice (9.0), { 48, 55, 60 },
                [&] (ShellProcessor& p) {
                    padSound (p);
                    set (p, params::morphTarget, 1.0f);
                    set (p, params::morphEnabled, 1.0f);
                    set (p, params::morphPosition, 0.0f);
                },
                [] (ShellProcessor& p, double t) { set (p, params::morphPosition, static_cast<float> (std::clamp (100.0 * (t - 1.0) / 7.0, 0.0, 100.0))); });
    // 4. Bowed fusion: the oscillators towards a bowed string - 0 %, 50 % held, then 100 %.
    renderLive ("live05-4-bowed-fusion", "pad chord with a bowed-string stand-in, MORPH TO D 0 % (2 s), 50 % (4 s), 100 % (2 s)", 8.0, bowedString (8.0), pad,
                [&] (ShellProcessor& p) {
                    padSound (p);
                    set (p, params::morphTarget, 1.0f);
                    set (p, params::morphEnabled, 1.0f);
                },
                [] (ShellProcessor& p, double t) { set (p, params::morphPosition, t < 2.0 ? 0.0f : (t < 6.0 ? 50.0f : 100.0f)); });
    // 5. Rhythmic audio layered with a sustained pad.
    renderLive ("live05-5-rhythm-layer", "pad chord + drum-kit stand-in on the sidechain, layered", 8.0, drumKit (8.0), pad, padSound, {});
    // 6. Source replacement: LIVE INPUT, RHYTHM, NOISE, KONTAKT (none loaded: silent), LIVE INPUT
    //    again - each mode keeps its own state (RHYTHM's loop stays loaded).
    const auto loop = [] {
        std::vector<float> x (static_cast<std::size_t> (4.0 * rate), 0.0f);
        for (int k = 0; k < 8; ++k)
            for (std::size_t i = 0; i < static_cast<std::size_t> (0.05 * rate); ++i)
                x[static_cast<std::size_t> (k * 0.5 * rate) + i] = static_cast<float> (0.5 * std::exp (-static_cast<double> (i) / rate / 0.01));
        return andor::analog::rhythm::prepareLoop (x.data(), x.data(), x.size(), rate);
    }();
    ShellProcessor* used = nullptr;
    renderLive ("live05-6-mode-switch", "pad chord; D: LIVE INPUT (drums) 0-2 s, RHYTHM 2-4 s, NOISE 4-6 s, KONTAKT (not loaded) 6-7 s, LIVE INPUT 7-9 s", 9.0,
                drumKit (9.0), pad,
                [&] (ShellProcessor& p) {
                    padSound (p);
                    engineOf (p).setAuxLoopForTest (loop);
                    set (p, params::auxRhythmGate, 1.0f);
                    set (p, params::auxNoiseGate, 1.0f);
                    used = &p;
                },
                [] (ShellProcessor& p, double t) { set (p, params::auxMode, t < 2.0 ? 3.0f : t < 4.0 ? 0.0f : t < 6.0 ? 1.0f : t < 7.0 ? 2.0f : 3.0f); });
    juce::ignoreUnused (used);
    const auto notesFile = out.getChildFile ("live05-renders.txt");
    notesFile.replaceWithText ("ANDOR Analog - AUX D LIVE INPUT (Addendum 05) demos, rendered by the hidden test [live05] through the plugin's sidechain bus.\n"
                               "The external tracks are SYNTHESIZED stand-ins (no real recordings are in the repository); listen for the routing and the morph,\n"
                               "then try your own tracks in a DAW.\n\n"
                               + notes.joinIntoString ("\n") + "\n");
}

TEST_CASE ("AUX LIVE INPUT: switching D's source keeps every other mode's state - RHYTHM's loop, NOISE's settings - and LIVE INPUT stores no audio", "[analog][aux][live]")
{
    ShellProcessor p;
    const auto loop = [] {
        std::vector<float> x (static_cast<std::size_t> (2.0 * rate), 0.0f);
        x[0] = 0.5f;
        return andor::analog::rhythm::prepareLoop (x.data(), x.data(), x.size(), rate);
    }();
    engineOf (p).setAuxLoopForTest (loop);
    set (p, params::auxNoiseColor, 2.0f);
    set (p, params::auxNoiseTone, 31.0f);
    for (const float mode : { 3.0f, 1.0f, 2.0f, 3.0f, 0.0f })
        set (p, params::auxMode, mode);
    CHECK (engineOf (p).auxLoop() == loop);
    CHECK (juce::roundToInt (p.parameters.getParameter (params::auxNoiseColor)->convertFrom0to1 (p.parameters.getParameter (params::auxNoiseColor)->getValue())) == 2);
    CHECK (p.parameters.getParameter (params::auxNoiseTone)->convertFrom0to1 (p.parameters.getParameter (params::auxNoiseTone)->getValue()) == Catch::Approx (31.0f).margin (0.1f));
    // The session with LIVE INPUT: the mode only - no audio, no source track.
    set (p, params::auxMode, 3.0f);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    ShellProcessor q;
    q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (juce::roundToInt (q.parameters.getParameter (params::auxMode)->convertFrom0to1 (q.parameters.getParameter (params::auxMode)->getValue())) == 3);
    CHECK (state.getSize() < 2 * 1024 * 1024);   // (no recorded input in it)
}
