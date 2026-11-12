#pragma once

#include "fx/FxGraph.h"

#include <juce_core/juce_core.h>

#include <memory>

namespace osp::plugin
{
class InstrumentHost;
}

namespace andor::analog
{

namespace fx
{
    class Processor;
    class Impulse;
}

/**
    What the FX library's rack reads of the instrument that plays, beyond its parameters (which
    it finds in the host's parameter state by `analog.fx.<type>.<n>.<name>`): the effect's
    running processor, whose own meters its graph draws, and the impulse a CONVOLUTION plays.

    The rack depends on this and on osp::plugin::InstrumentHost only, never on an instrument's
    shell or engine: ANDOR ANALOG's engine is one source (found through its shell,
    FxRackShell.cpp); another instrument that plays these processors gives its own, so the same
    rack edits one effect of its own.

    Message thread. A processor returned here must stay alive until the caller returns to the
    message loop (the rack asks again on every paint and never keeps the pointer).
*/
class FxRackSource
{
public:
    virtual ~FxRackSource() = default;

    /** The effect's processor (null: none playing, or one of the original four). */
    virtual const fx::Processor* fxProcessor (fx::InstanceId id) const = 0;
    /** The impulse CONVOLUTION `slot` plays (null: none yet). */
    virtual std::shared_ptr<const fx::Impulse> fxConvolutionImpulse (int slot) const = 0;

    /** The rack's heading for the effect. Empty (the default): the type's name, numbered where
        the pool holds two ("COMPRESSOR 1"). A host whose effects have names of their own gives
        the effect's. */
    virtual juce::String fxRackTitle (fx::InstanceId id) const
    {
        juce::ignoreUnused (id);
        return {};
    }

    /** What a rack remembers between openings: its page and the preset it shows. Null (the
        default): one memory per pool slot for the process, as ANDOR ANALOG's, where a slot is
        one effect. A host whose racks share a slot between several effects gives each its own. */
    struct Memory
    {
        int page = 0;
        juce::String preset;
    };
    virtual Memory* fxRackMemory (fx::InstanceId id)
    {
        juce::ignoreUnused (id);
        return nullptr;
    }
};

/** How the rack finds a host's source. A host that is itself an FxRackSource needs nothing; for
    any other, the instrument installs a finder once (it may return null: the rack then shows
    its settings without meters). */
using FxRackSourceFinder = FxRackSource* (*) (osp::plugin::InstrumentHost& host);
void setFxRackSourceFinder (FxRackSourceFinder finder);

} // namespace andor::analog
