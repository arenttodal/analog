#pragma once

#include <juce_core/juce_core.h>

namespace andor::analog::import
{

/**
    Reads a Kontakt 5+ instrument (.nki) into the importer's manifest (the same form as
    tools/kontakt/bukkehorn_manifest.py writes: docs/sample-source/), in the plugin, so a
    musician can import from their own NKI and sample folder.

    A bounded reader, for what the importer needs and nothing more: the NI container, its
    FastLZ-compressed sub-tree, and from Kontakt's preset chunks the program (name, volume),
    its groups (name, volume, pan, tune), zones (key and velocity range, root, tune, volume,
    start, length, rate, channels, loops), the sample file list and the first script with its
    saved persistent values. It executes nothing (a script is read as text). Written from the
    documented format (ConvertWithMoss's readers, LGPL, as documentation; no code copied).

    Lanes: when the script declares `$A_START` .. `$D_START` (an Evenant Engine's), each group
    is given its lane and slot and each lane its saved selection (`$ENGINE_x_SELECT`); otherwise
    the groups are listed as they are, without lanes.

    Encrypted or unexpected files are refused with a reason (never a guess, never a crash).
*/
struct NkiReadResult
{
    juce::var manifest;      ///< void when the file could not be read
    juce::String error;      ///< why not (empty when it was read)
};

NkiReadResult readNki (const juce::File& nki);

/** The folder the NKI's samples are expected in: beside the NKI, named as the file list's
    first path segment (e.g. "<instrument> Samples"); empty when it does not exist. */
juce::File expectedSampleFolder (const juce::File& nki, const juce::var& manifest);

} // namespace andor::analog::import
