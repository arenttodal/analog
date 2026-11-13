#pragma once

#include "fx/FxGraph.h"
#include "FxRackSource.h"
#include "InstrumentHost.h"
#include "ParameterRack.h"

namespace andor::shell
{
class ShellEditor;
}

namespace andor::analog
{

/**
    The FX library's full rack (rack::Kind::fx; docs/FX_EFFECTS_LIBRARY.md): the standard 214 px
    module, one effect at a time - ON in its title row, its pages (MAIN / DETAIL, or EQ's bands),
    MODE where it has modes and its PRESET browser in the selector row, one row of controls on the
    shared lines, and a graph drawn from what plays (the coefficients of its settings; meters from
    the engine's processor). Opening another effect swaps the module, not the rack's height.
*/
std::unique_ptr<osp::plugin::rack::Module> createFxRack (osp::plugin::InstrumentHost& host);
/** The same rack for one named effect of `host`, whatever fxRackShown() says: for an instrument
    that shows several hosts' effects in one process (each rack bound to its own). The rack
    reads the instrument through FxRackSource (FxRackSource.h) and nothing else. (A name of its
    own: createFxRack stays the plain function a product lists among its racks.) */
std::unique_ptr<osp::plugin::rack::Module> createFxRackFor (osp::plugin::InstrumentHost& host, fx::InstanceId id);

/** Shows `id`'s rack (the original four: their own racks, DRIVE its popover). ANDOR ANALOG's
    shell only (FxRackShell.cpp). */
void openFxRack (andor::shell::ShellEditor& editor, fx::InstanceId id);
/** ANDOR ANALOG: its engine becomes the rack's source (once, when the product is described). */
void installShellFxRack();
/** The effect createFxRack (host) shows (interface state), and choosing it. */
fx::InstanceId fxRackShown();
void setFxRackShown (fx::InstanceId id);

/** Tests and screenshots: the EQ rack's graphic (if `module` shows one) analyses its spectrum
    now - the timer's step - and returns the output's loudest bin (dB); -1000 without one. */
float analyseFxEqSpectrum (osp::plugin::rack::Module& module);
/** Tests: the EQ graphic's selected band (0-3, 4 HP, 5 LP; -1 without one) and a node's centre. */
int fxEqSelectedBand (osp::plugin::rack::Module& module);
juce::Point<float> fxEqNode (osp::plugin::rack::Module& module, int band);
juce::Component* fxEqGraphic (osp::plugin::rack::Module& module);

/** The quick settings a tile's single click shows: MODE (when the effect has modes) and up to
    four controls - the same parameters as the rack. Each pair: parameter ID and caption. */
struct QuickControl
{
    juce::String id, caption;
};
std::vector<QuickControl> quickControls (osp::plugin::InstrumentHost& host, fx::InstanceId id);
juce::String quickModeId (fx::InstanceId id);
/** The quick-settings popover for `id` (MODE, PRESET, those controls, EXPAND); `size` receives
    the size it wants. `expand`: its EXPAND (or Return); `dismiss`: its close (or Esc). */
std::unique_ptr<juce::Component> createFxQuickPopover (osp::plugin::InstrumentHost& host, fx::InstanceId id, std::function<void()> expand,
                                                      std::function<void()> dismiss, juce::Rectangle<int>& size);

} // namespace andor::analog
