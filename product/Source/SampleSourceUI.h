#pragma once

#include "InstrumentHost.h"
#include "ShapingPopups.h"

#include <memory>

namespace andor::analog
{

/**
    The Sample Source importer's interface (MVP A1.4), in ANDOR's own pieces:

    - ORGANIC's heading, while an imported source plays, opens a small popover: the import's
      sounds (a click plays one in ORGANIC's place; the patch's other settings stay), and the
      ways in and out (import another Kontakt patch, remove).
    - IMPORT KONTAKT PATCH (on the empty ORGANIC dock and in its card's menu): choose the NKI,
      then its sample folder (found beside it when it is there), review what was found, and
      import: the recordings are copied into the instrument's store, a preset is written per
      sound (Presets/<instrument>/), and the instrument's saved sound starts playing.
*/
std::unique_ptr<osp::plugin::MiniPanel> createSoundPopover (osp::plugin::InstrumentHost& host);

void runKontaktImport (osp::plugin::InstrumentHost& host);
/** Card C as an INSTRUMENT PATCH: plays this imported sound (its definition, from the import's
    record). Only the hybrid changes: OSC 1, OSC 2, the filter and the effects stay as they are. */
void chooseSound (osp::plugin::InstrumentHost& host, const juce::XmlElement& definition, int layer = 1);
/** The imported sounds as a menu under `target` (card C's well): pick one, import a Kontakt
    patch, or remove it. */
/** (Cinematic V6 gate 3: `layer` 2 lists and sets card C's second layer's own sound.) */
void showSoundMenu (osp::plugin::InstrumentHost& host, juce::Component& target, int layer = 1);
/** What card C plays as an INSTRUMENT PATCH, in words (its name, or how to choose one). */
juce::String instrumentPatchName (osp::plugin::InstrumentHost& host);

/** The review of an NKI and its folder (the window's content; tests build it directly). */
class ImportReview final : public juce::Component, private juce::ListBoxModel
{
public:
    ImportReview (juce::var manifest, juce::File sampleFolder);
    ~ImportReview() override;

    /** The musician imported: the sound to start with (a group index, or -1: the saved lanes). */
    std::function<void (int startGroup)> onImport;
    std::function<void()> onCancel;

    int groupsFound() const noexcept { return playable; }
    int recordingsMissing() const noexcept { return missing; }
    int recordingsTotal() const noexcept { return total; }
    juce::String summary() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;

    struct Row
    {
        int group = -1;              ///< -1: the saved lanes
        juce::String label, detail;
        int zones = 0, found = 0;
    };
    juce::var manifest;
    juce::File folder;
    std::vector<Row> rows;
    int playable = 0, missing = 0, total = 0;
    juce::ListBox list;
    juce::TextButton importButton { "Import" }, cancelButton { "Cancel" };
};

} // namespace andor::analog
