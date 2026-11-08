#pragma once

#include "FxGraph.h"
#include "FxSettings.h"

#include <memory>

namespace andor::analog::fx
{

/**
    A new effect's processor (one per pool slot, made in FxEngine::prepare). process() is the
    whole insert: the effect, its own MIX (dry delayed to match latency()) and OUTPUT. The engine
    adds the bypass glide around it.

    prepare() may allocate; everything else is real-time safe (no allocation, locks, I/O).
*/
class Processor
{
public:
    virtual ~Processor() = default;
    virtual void prepare (double sampleRate, int maximumBlockSize) = 0;
    virtual void reset() noexcept = 0;
    /** Reads its slot's settings (every block, before process). */
    virtual void set (const Settings& settings, int slot) noexcept = 0;
    virtual void process (float* left, float* right, int numSamples) noexcept = 0;
    /** Samples it delays its output by (its dry path included). */
    virtual int latency() const noexcept { return 0; }
    /** A wet tail (a delay, an impulse): bypassed, it stops hearing new input while the tail
        drains, rather than being cut. */
    virtual bool hasTail() const noexcept { return false; }
};

/** Makes the processor of a new effect type (null for the original four). */
std::unique_ptr<Processor> makeProcessor (Type type);

/** Whether a type has a working processor in this build (the original four always do). An
    effect that has none is not insertable (the library says "coming later"). */
bool implemented (Type type) noexcept;

/** A slot's ON (its own bypass) in the settings. */
bool isOn (const Settings& settings, InstanceId id) noexcept;

} // namespace andor::analog::fx
