#pragma once

#include "Patch.h"

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
};

/** Every factory sound (synthesized only: no samples, no outside files). */
const std::vector<FactoryPatch>& factoryPatches();

} // namespace andor::analog
