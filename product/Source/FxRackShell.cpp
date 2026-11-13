#include "FxRack.h"

#include "AnalogEngine.h"
#include "ShellEditor.h"
#include "ShellProcessor.h"

// The FX rack in ANDOR ANALOG's shell: the only part of it that knows the shell and the engine
// (FxRack.cpp itself depends on osp::plugin::InstrumentHost and FxRackSource alone, so another
// instrument can show the same rack for an effect of its own).

namespace andor::analog
{

namespace
{
    /** ANALOG's source: the engine behind the shell's processor (null for any other host). */
    FxRackSource* engineBehind (osp::plugin::InstrumentHost& host)
    {
        if (auto* processor = dynamic_cast<andor::shell::ShellProcessor*> (&host))
            return dynamic_cast<AnalogEngine*> (&processor->productEngine());
        return nullptr;
    }
}

void installShellFxRack() { setFxRackSourceFinder (engineBehind); }

void openFxRack (andor::shell::ShellEditor& editor, fx::InstanceId id)
{
    using fx::Type;
    using osp::plugin::rack::Kind;
    switch (id.type)
    {
        case Type::movement: editor.setRackExpanded (Kind::movement, true); return;
        case Type::echo: editor.setRackExpanded (Kind::echo, true); return;
        case Type::space: editor.setRackExpanded (Kind::space, true); return;
        case Type::drive: break;   // (V2: DRIVE's hardware rack - its macro's popover stays the quick view)
        default: break;
    }
    // One FX rack at a time: another effect swaps the module (the rack keeps its height).
    setFxRackShown (id);
    editor.setRackExpanded (Kind::fx, false);
    editor.setRackExpanded (Kind::fx, true);
}

} // namespace andor::analog
