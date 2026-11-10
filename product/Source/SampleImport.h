#pragma once

#include "SampleSource.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>

namespace andor::analog::import
{

/**
    The Sample Source importer (MVP A1): a Kontakt patch's manifest (made from its NKI by
    tools/kontakt: docs/sample-source/) and the patch's sample folder, into an imported source
    ANDOR ANALOG plays and keeps.

    The persistent form is a *definition* (a `SampleSource` XML element saved with presets and
    sessions): its name and level, a link to where it came from (the NKI, each group's index,
    lane and name), and per zone its mapping and the recording it plays. The recordings are kept
    in the instrument's own sample store (named by their content), so a preset never depends on
    the Kontakt folder; a definition made without a store points at the files in place.

    A zone's file is found by its name (exactly, else by its letters and digits, so a copy
    renamed by an upload or a file system still resolves). Anything that cannot be found or read
    is reported by name, never silently left out or replaced. Message or loader thread (it reads
    files and decodes audio), never the audio thread.
*/

/** ANDOR ANALOG's ORGANIC path plays a recording this far below Kontakt's own gain chain
    (measured: [bukkehorn]); an imported source is raised by it so a patch plays at the level its
    Kontakt original did. */
constexpr double kontaktLevelMatchDb = 17.0;

/** What loading found: the source (null when nothing could be loaded), and what is missing. */
struct LoadResult
{
    std::shared_ptr<const SampleSource> source;
    juce::StringArray missing;      ///< zones' recordings not found
    juce::StringArray unreadable;   ///< found but not decodable
    int zones = 0;                  ///< zones in the definition (loaded or not)
    bool complete() const noexcept { return source != nullptr && missing.isEmpty() && unreadable.isEmpty(); }
};
using GroupLoad = LoadResult;

struct Definition
{
    std::unique_ptr<juce::XmlElement> xml;   ///< the `SampleSource` element
    juce::StringArray missing;               ///< recordings not found in the sample folder
};

/** The manifest (JSON), or a void var when it cannot be read. */
juce::var readManifest (const juce::File& manifestFile);

/** The manifest's groups that have zones: their index and name, in lane order. */
juce::Array<std::pair<int, juce::String>> playableGroups (const juce::var& manifest);

/** The manifest's saved instrument: each lane's selected group (A, B, C, D order). */
juce::Array<int> savedLaneGroups (const juce::var& manifest);

/** A definition for one or more of the manifest's groups (several: layered, each at its own
    level), its recordings found in `sampleFolder` and copied into `assetFolder` (none: referred
    to in place). Each zone's level is its own times its group's; the source's is the
    instrument's, raised by kontaktLevelMatchDb. */
Definition makeDefinition (const juce::var& manifest, const juce::Array<int>& groups, const juce::String& name, const juce::File& sampleFolder,
                           const juce::File& assetFolder);

/** One recording as a source (Cinematic V6 gate 3: card C's second layer's SAMPLE): one zone over
    every key, rooted where its pitch was found (else C4), copied into `assetFolder` by its content.
    Loader thread (it decodes and analyses). Null xml: the file could not be read or stored. */
Definition makeFileDefinition (const juce::File& recording, const juce::File& assetFolder);

/** A definition into a playable source, its recordings from `assetFolder` (or their paths). */
LoadResult loadDefinition (const juce::XmlElement& definition, const juce::File& assetFolder);

/** One group, straight from its folder (no store): makeDefinition then loadDefinition. */
LoadResult loadGroup (const juce::var& manifest, int groupIndex, const juce::File& sampleFolder);

/** Finds a definition's missing recordings in `folder` (by name), copies them into
    `assetFolder` and records them in `definition`. Returns how many were found. */
int relink (juce::XmlElement& definition, const juce::File& folder, const juce::File& assetFolder);

/** A zone's file in `folder`: by its exact name, else by its letters and digits. */
juce::File findSample (const juce::File& folder, const juce::String& name);

/** Copies a recording into the store, named by its content; returns that name (empty: failed). */
juce::String storeAsset (const juce::File& recording, const juce::File& assetFolder);

} // namespace andor::analog::import
