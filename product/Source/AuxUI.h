#pragma once

#include "InstrumentHost.h"
#include "ParameterRack.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace andor::analog
{

/**
    D / AUX (docs/aux): the compact strip under cards A / B / C (ShellProduct::auxStrip) and its
    full-width rack (a product rack, rack::Kind::aux). Both are views of D's parameters
    (analog.aux.*) and of the engine's telemetry; neither changes the sound by being shown.

    The strip: D's badge (dusty plum) and AUX, its power, M / S, a graphite well with D's mode
    (the card C type dropdown's look) and a live display, the mode's knobs (RHYTHM: RATE,
    MOD AMOUNT, LEVEL; NOISE: COLOR, TONE, LEVEL; KONTAKT: LEVEL), ROUTE and its `...` menu
    (Hide AUX, Open / Close AUX Rack, Reset). A double-click opens or closes the rack. A
    recording dropped on it becomes D RHYTHM's loop.
*/
namespace aux
{
    /** D's identity: a dusty plum (badge top, badge bottom, its light on graphite). */
    juce::Colour colour();
    juce::Colour badgeTop();
    juce::Colour badgeBottom();
    juce::Colour onDark();
    /** The mode's name ("RHYTHM", "NOISE", "KONTAKT"). */
    juce::String modeName (int mode);
    /** One line on what D plays now (the well's readout): the recording, the colour, Kontakt. */
    juce::String readout (osp::plugin::InstrumentHost& host);
    /** Resets every D parameter to its default (one undoable change); D's recording and
        Kontakt stay loaded. */
    void reset (osp::plugin::InstrumentHost& host);
    /** Opens a file chooser for D RHYTHM's recording (WAV, AIFF, FLAC). */
    void chooseRecording (osp::plugin::InstrumentHost& host, juce::Component& near);
}

std::unique_ptr<juce::Component> createAuxStrip (osp::plugin::InstrumentHost& host);
std::unique_ptr<osp::plugin::rack::Module> createAuxRack (osp::plugin::InstrumentHost& host);

/** Tests: the strip's parts by name ("power", "mute", "solo", "mode", "display", "route",
    "menu", and the knobs by caption), its shown mode, and the menu's items. */
struct AuxStripView
{
    static juce::Component* part (juce::Component& strip, const juce::String& name);
    static int shownMode (juce::Component& strip);
    static juce::StringArray knobCaptions (juce::Component& strip);
    static juce::StringArray menuItems (juce::Component& strip);
    static void runMenuItem (juce::Component& strip, const juce::String& item);
    static void doubleClick (juce::Component& strip);
    /** Its timer's work at once (tests: no message loop). */
    static void refresh (juce::Component& strip);
};

} // namespace andor::analog
