#pragma once

#include "Patch.h"

#include "engine/KeyDna.h"

#include <string>
#include <vector>

namespace andor::analog
{

/** A factory sound: its name, a category for the preset folder, and the patch. */
struct FactoryPatch
{
    std::string name;
    std::string category;
    Patch patch;
    /** The notes its demo phrase plays (analog-render --all): a chord or a line. */
    std::vector<int> demoNotes;
    bool demoAsLine = false;
    /** GRANULAR's KEY DNA: 0 off; otherwise on, each key's grains (where, how long, how
        dense, how wide) differing by this much (analog.keyDna.*). */
    double keyDna = 0.0;
};

/** A factory sound's KEY DNA as the engine takes it (seeded from the patch's variation seed). */
osp::keydna::Settings keyDnaOf (const FactoryPatch& patch);

/** Every factory sound (synthesized only: no samples, no outside files). */
const std::vector<FactoryPatch>& factoryPatches();

} // namespace andor::analog
