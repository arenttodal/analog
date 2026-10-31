#pragma once

#include "SampleSource.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>

namespace andor::analog::import
{

/**
    The Sample Source importer's loading side (MVP A1): a Kontakt patch's manifest (made from
    its NKI by tools/kontakt: docs/sample-source/) and the patch's sample folder, into a
    sample source ANDOR ANALOG plays (SampleSource). Message or loader thread: it decodes audio.

    A zone's file is found in the folder by its name (exactly, else by its letters and digits,
    so a copy renamed by an upload or a file system still resolves). Anything that cannot be
    found or read is reported by name, never silently left out or replaced.
*/
struct GroupLoad
{
    std::shared_ptr<const SampleSource> source;   ///< null when nothing could be loaded
    juce::StringArray missing;                    ///< zones' files not found in the folder
    juce::StringArray unreadable;                 ///< found but not decodable
    int zones = 0;                                ///< zones in the group (loaded or not)
    bool complete() const noexcept { return source != nullptr && missing.isEmpty() && unreadable.isEmpty(); }
};

/** The manifest (JSON), or a void var when it cannot be read. */
juce::var readManifest (const juce::File& manifestFile);

/** The manifest's groups that have zones: their index and name, in lane order. */
juce::Array<std::pair<int, juce::String>> playableGroups (const juce::var& manifest);

/** One group of the manifest as a sample source, its audio from `sampleFolder`. Its level is
    the patch's: the group's volume times the instrument's (each zone keeps its own). */
GroupLoad loadGroup (const juce::var& manifest, int groupIndex, const juce::File& sampleFolder);

/** A zone's file in `folder`: by its exact name, else by its letters and digits. */
juce::File findSample (const juce::File& folder, const juce::String& name);

} // namespace andor::analog::import
