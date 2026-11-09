#pragma once

#include "fx/FxGraph.h"
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

/** Shows `id`'s rack (the original four: their own racks, DRIVE its popover). */
void openFxRack (andor::shell::ShellEditor& editor, fx::InstanceId id);
/** The effect the FX rack shows (interface state). */
fx::InstanceId fxRackShown();

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
