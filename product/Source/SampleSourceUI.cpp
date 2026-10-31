#include "SampleSourceUI.h"

#include "AnalogEngine.h"
#include "Design.h"
#include "OspLookAndFeel.h"
#include "KontaktImport.h"
#include "SamplePatches.h"
#include "ShellProcessor.h"

namespace andor::analog
{

using namespace osp::plugin;

namespace
{
    AnalogEngine* engineOf (InstrumentHost& host)
    {
        if (auto* shell = dynamic_cast<andor::shell::ShellProcessor*> (&host))
            return dynamic_cast<AnalogEngine*> (&shell->productEngine());
        return nullptr;
    }

    juce::File importFolder (InstrumentHost& host)
    {
        if (auto* shell = dynamic_cast<andor::shell::ShellProcessor*> (&host))
            return shell->sampleStoreFolder().getChildFile ("Imported");
        return {};
    }

    /** "Select sound": the imported collection's sounds, as keys; a click plays one. */
    class SoundPopover final : public MiniPanel
    {
    public:
        explicit SoundPopover (InstrumentHost& h) : MiniPanel ("SOUND", "Imported sample source"), host (h)
        {
            if (auto* engine = engineOf (host))
                if (const auto definition = engine->sampleSourceDefinition())
                {
                    current = definition->getStringAttribute ("name");
                    title = definition->getStringAttribute ("collection", "SOUND").toUpperCase();
                    if (const auto record = import::readRecord (importFolder (host), definition->getStringAttribute ("import")))
                        for (const auto* d : record->getChildWithTagNameIterator ("SampleSource"))
                            sounds.push_back (std::make_unique<juce::XmlElement> (*d));
                }
            for (const auto& d : sounds)
            {
                auto b = std::make_unique<juce::TextButton> (d->getStringAttribute ("name"));
                b->setClickingTogglesState (false);
                b->setToggleState (d->getStringAttribute ("name") == current, juce::dontSendNotification);
                b->setColour (juce::TextButton::buttonOnColourId, design::colour::accent.withAlpha (0.25f));
                const auto* def = d.get();
                b->onClick = [this, def] { choose (*def); };
                addAndMakeVisible (*b);
                keys.push_back (std::move (b));
            }
            importKey.onClick = [this] { runKontaktImport (host); };
            removeKey.onClick = [this] {
                if (auto* engine = engineOf (host))
                    engine->setSampleSourceDefinition (nullptr, {});
                if (onClose != nullptr)
                    onClose();
            };
            addAndMakeVisible (importKey);
            addAndMakeVisible (removeKey);
        }

        juce::Point<int> cardSize() const override
        {
            const int rowsOfKeys = (static_cast<int> (keys.size()) + 2) / 3;
            return { 520, headerHeight() + 18 + rowsOfKeys * 34 + 20 + 34 + 22 };
        }

        void layoutContent (juce::Rectangle<int> area) override
        {
            area = area.reduced (22, 0);
            area.removeFromTop (6);
            const int w = (area.getWidth() - 2 * 8) / 3;
            int i = 0;
            juce::Rectangle<int> rowArea;
            for (auto& k : keys)
            {
                if (i % 3 == 0)
                {
                    rowArea = area.removeFromTop (28);
                    area.removeFromTop (6);
                }
                k->setBounds (rowArea.removeFromLeft (w));
                rowArea.removeFromLeft (8);
                ++i;
            }
            area.removeFromTop (14);
            auto footer = area.removeFromTop (30);
            importKey.setBounds (footer.removeFromLeft (230));
            removeKey.setBounds (footer.removeFromRight (110));
        }

        void refreshContent() override
        {
            if (auto* engine = engineOf (host))
                if (const auto definition = engine->sampleSourceDefinition())
                    for (auto& k : keys)
                        k->setToggleState (k->getButtonText() == definition->getStringAttribute ("name"), juce::dontSendNotification);
        }

    private:
        void choose (const juce::XmlElement& definition)
        {
            if (auto* engine = engineOf (host))
                engine->setSampleSourceDefinition (std::make_unique<juce::XmlElement> (definition), importFolder (host));
        }

        InstrumentHost& host;
        juce::String current;
        std::vector<std::unique_ptr<juce::XmlElement>> sounds;
        std::vector<std::unique_ptr<juce::TextButton>> keys;
        juce::TextButton importKey { juce::String::fromUTF8 ("Import Kontakt patch\xe2\x80\xa6") }, removeKey { "Remove" };
    };

    std::unique_ptr<juce::FileChooser> chooser;   // (message thread: one chooser at a time)
    juce::Component::SafePointer<juce::DialogWindow> reviewWindow;

    /** The import has finished: its chosen sound starts playing in ORGANIC's place. */
    void finish (InstrumentHost& host, const juce::var& manifest, const juce::File& assets, const import::GeneratedPatches& made, int startGroup)
    {
        auto* shell = dynamic_cast<andor::shell::ShellProcessor*> (&host);
        auto* engine = engineOf (host);
        if (shell == nullptr || engine == nullptr)
            return;
        if (const auto record = import::readRecord (assets, made.record.getFileName()))
        {
            const auto groups = manifest.getProperty ("groups", {});
            const auto wanted = startGroup < 0 ? juce::String ("Saved Lanes")
                                               : groups[startGroup].getProperty ("lane", {}).toString() + juce::String (static_cast<int> (groups[startGroup].getProperty ("slot", 0)) + 1)
                                                     + " " + groups[startGroup].getProperty ("name", {}).toString();
            for (const auto* d : record->getChildWithTagNameIterator ("SampleSource"))
                if (d->getStringAttribute ("name") == wanted)
                {
                    shell->removeLayer (andor::shell::ShellProcessor::hybridLayer);   // (a recording on ORGANIC gives way)
                    engine->setSampleSourceDefinition (std::make_unique<juce::XmlElement> (*d), assets);
                    if (shell->parameterValue ("ab.blend") < 0.01f)
                        shell->setParameterValue ("ab.blend", 0.5f);   // (heard at once)
                }
        }
        if (! made.missing.isEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Some recordings were not found",
                                                    juce::String (made.missing.size()) + " recordings of this instrument were not in the folder. Their zones are silent until you relink them.");
    }

    void showReview (InstrumentHost& host, juce::var manifest, juce::File nki, juce::File folder)
    {
        auto review = std::make_unique<ImportReview> (manifest, folder);
        auto* raw = review.get();
        raw->onCancel = [] {
            if (reviewWindow != nullptr)
                reviewWindow->exitModalState (0);
        };
        raw->onImport = [&host, manifest, folder] (int startGroup) {
            if (reviewWindow != nullptr)
                reviewWindow->exitModalState (1);
            auto* shell = dynamic_cast<andor::shell::ShellProcessor*> (&host);
            if (shell == nullptr)
                return;
            // Copying the recordings and writing the presets takes a while for a large
            // instrument: off the message thread, with a progress window; the result is taken
            // back on the message thread when it has finished.
            struct Job final : juce::ThreadWithProgressWindow
            {
                Job (InstrumentHost& h, juce::var m, juce::File f, juce::File store, juce::File presets, int start)
                    : juce::ThreadWithProgressWindow ("Importing the Kontakt patch", true, false), host (h), manifest (std::move (m)), folder (std::move (f)),
                      assets (std::move (store)), presetFolder (std::move (presets)), startGroup (start)
                {
                }
                void run() override
                {
                    setStatusMessage ("Copying the recordings and writing the presets");
                    made = import::generatePatches (host, manifest, folder, assets, presetFolder);
                }
                void threadComplete (bool) override
                {
                    finish (host, manifest, assets, made, startGroup);
                    delete this;
                }
                InstrumentHost& host;
                juce::var manifest;
                juce::File folder, assets, presetFolder;
                int startGroup;
                import::GeneratedPatches made;
            };
            (new Job (host, manifest, folder, shell->sampleStoreFolder().getChildFile ("Imported"), shell->presetFolder(), startGroup))->launchThread();
        };
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (review.release());
        options.dialogTitle = "Import Kontakt patch - " + nki.getFileNameWithoutExtension();
        options.dialogBackgroundColour = design::colour::housingTop;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        reviewWindow = options.launchAsync();
    }
}

std::unique_ptr<MiniPanel> createSoundPopover (InstrumentHost& host)
{
    return std::make_unique<SoundPopover> (host);
}

void runKontaktImport (InstrumentHost& host)
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a Kontakt instrument (.nki)", juce::File(), "*.nki");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [&host] (const juce::FileChooser& fc) {
        const auto nki = fc.getResult();
        if (nki == juce::File())
            return;
        const auto read = import::readNki (nki);
        if (read.manifest.isVoid())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Cannot import this instrument", read.error);
            return;
        }
        if (const auto folder = import::expectedSampleFolder (nki, read.manifest); folder != juce::File())
        {
            showReview (host, read.manifest, nki, folder);
            return;
        }
        // Its folder is not beside it: the musician points to it.
        const auto expected = read.manifest.getProperty ("source", {}).getProperty ("sampleFolder", {}).toString();
        chooser = std::make_unique<juce::FileChooser> ("Choose its sample folder (" + expected + ")", nki.getParentDirectory());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [&host, manifest = read.manifest, nki] (const juce::FileChooser& f) {
            if (f.getResult().isDirectory())
                showReview (host, manifest, nki, f.getResult());
        });
    });
}

//==============================================================================
ImportReview::ImportReview (juce::var m, juce::File f) : manifest (std::move (m)), folder (std::move (f))
{
    const auto groups = manifest.getProperty ("groups", {});
    std::map<juce::String, bool> seen;
    auto addRow = [&] (Row r) { rows.push_back (std::move (r)); };
    for (const auto& [index, name] : import::playableGroups (manifest))
    {
        Row r;
        r.group = index;
        const auto g = groups[index];
        r.label = g.getProperty ("lane", {}).toString() + juce::String (static_cast<int> (g.getProperty ("slot", 0)) + 1) + "  " + name;
        if (auto* zones = g.getProperty ("zones", {}).getArray())
            for (const auto& z : *zones)
            {
                ++r.zones;
                const auto file = z.getProperty ("file", {}).toString();
                const bool here = import::findSample (folder, file) != juce::File();
                r.found += here ? 1 : 0;
                if (seen.find (file) == seen.end())
                {
                    seen[file] = here;
                    ++total;
                    missing += here ? 0 : 1;
                }
            }
        r.detail = juce::String (r.zones) + " zones" + (r.found == r.zones ? juce::String() : ", " + juce::String (r.zones - r.found) + " missing");
        playable += r.found > 0 ? 1 : 0;
        addRow (std::move (r));
    }
    if (! import::savedLaneGroups (manifest).isEmpty())
    {
        Row saved;
        saved.label = "Saved Lanes";
        saved.detail = "the instrument as it was saved (four lanes layered)";
        rows.insert (rows.begin(), std::move (saved));
    }
    list.setModel (this);
    list.setRowHeight (24);
    list.setColour (juce::ListBox::backgroundColourId, design::colour::cardTop);
    list.setColour (juce::ListBox::outlineColourId, palette::hairline);
    list.setOutlineThickness (1);
    list.selectRow (0);
    addAndMakeVisible (list);
    importButton.onClick = [this] {
        const int row = list.getSelectedRow();
        if (onImport != nullptr)
            onImport (juce::isPositiveAndBelow (row, static_cast<int> (rows.size())) ? rows[static_cast<std::size_t> (row)].group : -1);
    };
    cancelButton.onClick = [this] {
        if (onCancel != nullptr)
            onCancel();
    };
    importButton.setEnabled (playable > 0);
    addAndMakeVisible (importButton);
    addAndMakeVisible (cancelButton);
    setSize (560, 520);
}

ImportReview::~ImportReview()
{
    list.setModel (nullptr);
}

juce::String ImportReview::summary() const
{
    return juce::String (playable) + " sounds  " + juce::String::fromUTF8 ("\xc2\xb7") + "  " + juce::String (total) + " recordings"
           + (missing > 0 ? "  " + juce::String::fromUTF8 ("\xc2\xb7") + "  " + juce::String (missing) + " not in the folder" : juce::String());
}

int ImportReview::getNumRows()
{
    return static_cast<int> (rows.size());
}

void ImportReview::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, static_cast<int> (rows.size())))
        return;
    const auto& r = rows[static_cast<std::size_t> (row)];
    if (selected)
    {
        g.setColour (design::colour::accent.withAlpha (0.18f));
        g.fillRect (0, 0, width, height);
    }
    const bool incomplete = r.group >= 0 && r.found < r.zones;
    g.setColour (r.group >= 0 && r.found == 0 ? design::colour::textMicro : design::colour::text);
    g.setFont (type::popupLabel (12.5f));
    g.drawText (r.label, 10, 0, width / 2, height, juce::Justification::centredLeft, true);
    g.setColour (incomplete ? juce::Colour (0xffb5532c) : design::colour::textMicro);
    g.drawText (r.detail, width / 2, 0, width / 2 - 10, height, juce::Justification::centredRight, true);
}

void ImportReview::paint (juce::Graphics& g)
{
    g.fillAll (design::colour::housingTop);
    auto r = getLocalBounds().reduced (18);
    g.setColour (design::colour::text);
    g.setFont (fonts::make (20.0f, fonts::Weight::semibold, 0.02f));
    g.drawText (manifest.getProperty ("source", {}).getProperty ("program", {}).toString(), r.removeFromTop (28), juce::Justification::centredLeft, true);
    g.setColour (design::colour::textMicro);
    g.setFont (type::popupLabel (12.0f));
    g.drawText (summary(), r.removeFromTop (20), juce::Justification::centredLeft, true);
    // What is not imported, said plainly.
    auto foot = getLocalBounds().reduced (18).removeFromBottom (86).removeFromTop (44);
    g.drawFittedText ("Kontakt's effects (its reverb, Replika, SuperGT) are not imitated: their settings are kept with the import. "
                      "Each sound becomes a preset; its recordings are copied into ANDOR Analog's own store.",
                      foot, juce::Justification::topLeft, 3);
}

void ImportReview::resized()
{
    auto r = getLocalBounds().reduced (18);
    r.removeFromTop (56);
    auto buttons = r.removeFromBottom (32);
    r.removeFromBottom (54);
    list.setBounds (r);
    cancelButton.setBounds (buttons.removeFromRight (100));
    buttons.removeFromRight (10);
    importButton.setBounds (buttons.removeFromRight (120));
}

} // namespace andor::analog
