#pragma once

#include "ShellProduct.h"

namespace andor::analog
{

/**
    ANDOR Learn for ANDOR Analog (docs/LEARN_V1.md): the lesson catalog (Quick Start, the ARP
    lesson, MORPH, FX Routing), the Learn mode help of the instrument's main regions, the
    product's own targets and its capabilities. Interface only: no parameter, no state.
*/
andor::shell::LearnDescriptor learnDescriptor();

/** The catalog alone (tests and docs). */
andor::learn::Catalog learnCatalog();

} // namespace andor::analog
