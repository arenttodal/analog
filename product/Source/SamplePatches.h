#pragma once

#include "InstrumentHost.h"
#include "Patch.h"
#include "SampleImport.h"

namespace andor::analog::import
{

/**
    An imported Kontakt patch's sounds (the Sample Source importer, milestone A1.2): the
    recordings copied into `assetFolder` and the import's record beside them, every sound's
    definition, which card C's INSTRUMENT PATCH offers under SOUNDS. That is where the sounds
    are played from: the import writes no presets.

    Only when `presetFolder` is given (tests, a later "export as presets") are they also
    written as native presets into `<presetFolder>/<patch name>/`:

    - one per playable group ("A8 Voxx": lane and slot, then the group's name; Kontakt repeats
      names across lanes): the group alone, the oscillators off;
    - "Saved Lanes": the instrument as it was saved (each lane's selected group, layered at its
      own level), the oscillators off;
    - one starting point that layers the saved lane A with the oscillators ("Hybrid ...",
      explicitly generated: no Kontakt preset).

    Each carries its source's definition (its recordings copied into `assetFolder`, the link to
    the NKI group(s) they came from). The patch takes from the Kontakt instrument only what ANDOR
    ANALOG plays alike (see makePatch); the rest of the Kontakt settings stay in the manifest.
    The 16 user presets of the Kontakt instrument are empty and are not invented.
*/
struct GeneratedPatches
{
    juce::Array<juce::File> files;
    juce::File record;           ///< the import's record (its sounds' definitions: the sound list)
    juce::StringArray missing;   ///< recordings not found in the sample folder (relink later)
    juce::StringArray skipped;   ///< groups with none of their recordings found
};

GeneratedPatches generatePatches (osp::plugin::InstrumentHost& host, const juce::var& manifest, const juce::File& sampleFolder, const juce::File& assetFolder,
                                  const juce::File& presetFolder = {});

/**
    The presets earlier imports wrote (before the sounds were reached only through card C's
    SOUNDS), moved to the system's trash (deleted where it has none): in a folder named after
    an import's collection, playing a sound of that same import, and named as the importer
    named them (a sound's name, "Saved Lanes", "Hybrid - ..."). Anything else, a preset saved
    by the user among them included, stays. Returns how many were moved. Message thread.
*/
int retireGeneratedPresets (const juce::File& presetFolder, const juce::File& assetFolder);

/** The patch an imported source plays in: ORGANIC alone (or with the oscillators, `hybrid`),
    the Kontakt instrument's shared envelope, filter and send where ANDOR ANALOG has them. */
Patch makePatch (bool hybrid);

/** A preset's state: every parameter (the patch's, the defaults elsewhere) and the source's
    definition. */
std::unique_ptr<juce::XmlElement> presetState (osp::plugin::InstrumentHost& host, const Patch& patch, const juce::XmlElement& definition);

} // namespace andor::analog::import

namespace andor::analog::import
{
/** An import's record (`<assetFolder>/<name>.import.xml`): every sound's definition, so a
    playing imported source can offer its siblings (ORGANIC's "Select sound"). */
std::unique_ptr<juce::XmlElement> readRecord (const juce::File& assetFolder, const juce::String& recordName);
} // namespace andor::analog::import
