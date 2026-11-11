// ANDOR ANALOG: what makes it itself (identity, namespaces, macros, its own parameters and
// factory sounds) and its engine. Started by tools/create_andor_instrument.py from
// shared/andor-shell/template.

#include "AnalogEngine.h"
#include "AnalogMaterialsData.h"
#include "AnalogParameters.h"
#include "AnalogWellOverlays.h"
#include "AnalogPopovers.h"
#include "AnalogRacks.h"
#include "FxIdentity.h"
#include "FxRack.h"
#include "FxRouting.h"
#include "FxParameters.h"
#include "FactoryPatches.h"
#include "KeyDnaState.h"
#include "SamplePatches.h"
#include "SampleSourceUI.h"
#include "ShaperDnaPanel.h"
#include "UserMacroUI.h"
#include "SatellitePanel.h"
#include "ShellProcessor.h"
#include "ShellProduct.h"

#include <juce_cryptography/juce_cryptography.h>

namespace andor::shell
{

namespace
{
    /** Bumped whenever the factory sounds change: an installed set from another version is
        written again (the musician's own presets are never touched). */
    constexpr const char* factoryVersion = "analog-factory-9";   // (8: MORPH on, the wheel unlinked; 9: INIT with MORPH off)

    /** Version 2 moved settings (ANDOR OSP's DRIVE, MOVEMENT, REIMAGINED, GRANULAR and LFO 1
        joined): a version-1 session or preset is brought up to date so it sounds as it did. */
    void migrateFromVersion1 (osp::plugin::InstrumentHost& host)
    {
        namespace p = andor::analog::params;
        // DRIVE was the filter's input drive: that is FILTER DRIVE now, and the DRIVE macro is
        // ANDOR OSP's DRIVE, which version 1 did not have.
        host.setParameterValue (p::filterDrive, host.parameterValue ("drive"));
        host.setParameterValue ("drive", 0.0f);
        // The LFO is LFO 1 of the modulation panel now (S&H, which it lacks, becomes RANDOM).
        constexpr int shapeOf[] = { 0, 1, 2, 4, 5 };   // SINE, TRIANGLE, RAMP, SQUARE, S&H
        host.setParameterValue ("mod.lfo1.shape", static_cast<float> (shapeOf[std::clamp (juce::roundToInt (host.parameterValue (p::lfoWave)), 0, 4)]));
        host.setParameterValue ("mod.lfo1.rate", host.parameterValue (p::lfoRate));
        host.setParameterValue ("mod.lfo1.sync", host.parameterValue (p::lfoSync));
        host.setParameterValue ("mod.lfo1.division", static_cast<float> (andor::analog::syncDivisionOfLegacy (juce::roundToInt (host.parameterValue (p::lfoDivision)))));
        host.setParameterValue ("mod.lfo1.phase", 0.0f);
        host.setParameterValue ("mod.lfo1.polarity", 0.0f);
        host.setParameterValue ("mod.lfo1.mode", 0.0f);
        // ANDOR OSP's MOVEMENT, REIMAGINED and GRANULAR did nothing in version 1 (their
        // defaults were stored, unheard): they start off.
        host.setParameterValue ("motion", 0.0f);
        host.setParameterValue ("reimagined", 0.0f);
        host.setParameterValue ("layerA.sourceMode", 0.0f);
    }

    /** Version 3: the oscillators are mixed by their own LEVELs and BLEND left the interface.
        Its setting is folded into the two levels and it rests at its centre (the same sound). */
    void migrateFromVersion2 (osp::plugin::InstrumentHost& host)
    {
        namespace p = andor::analog::params;
        double level1 = 0.01 * host.parameterValue (p::osc1Level), level2 = 0.01 * host.parameterValue (p::osc2Level);
        andor::analog::foldBlend (0.01 * host.parameterValue (p::blend), level1, level2);
        host.setParameterValue (p::osc1Level, static_cast<float> (100.0 * level1));
        host.setParameterValue (p::osc2Level, static_cast<float> (100.0 * level2));
        host.setParameterValue (p::blend, 50.0f);
    }

    /** Version 4 (the oscillator models, the OTA filter family, the chorus, ORGANIC and MORPH,
        all in one step): an older session or preset has none of their parameters, so each is
        set to its default (CLASSIC + CLASSIC, LADDER, chorus off, ORGANIC off, MORPH off):
        exactly the sound it had. Nothing it stored is changed. */
    void migrateFromVersion3 (osp::plugin::InstrumentHost& host)
    {
        for (const auto& id : andor::analog::params::version4Ids())
            if (auto* parameter = host.apvts().getParameter (id))
                host.setParameterValue (id, parameter->convertFrom0to1 (parameter->getDefaultValue()));
    }

    /** Version 5: the A/B mix (ANDOR OSP's blend) sets the oscillators against ORGANIC, which
        version 4 played at full level beside them whatever it showed. Its centre is that
        sound (both at full level), so a session or preset from before rests there. */
    void migrateFromVersion4 (osp::plugin::InstrumentHost& host)
    {
        host.setParameterValue ("ab.blend", 0.5f);
    }

    /** Version 6: KEY DNA is ANDOR OSP's own (its editor; its settings in the session's KeyDna
        tree, its AMOUNT layerA.keyDna.amount). The analog.keyDna.* parameters an older session
        stored move there, seeded as they were (the patch's seed): the same profile, the same
        sound. The old parameters stay, unread. */
    void migrateFromVersion5 (osp::plugin::InstrumentHost& host)
    {
        namespace p = andor::analog::params;
        auto percent = [&host] (const juce::String& id) { return 0.01 * static_cast<double> (host.parameterValue (id)); };
        osp::keydna::Settings k;
        k.enabled = host.parameterValue (p::keyDna) >= 0.5f;
        k.individuality = percent (p::keyDnaIndividuality);
        k.seed = static_cast<std::uint64_t> (std::max (1L, std::lround (host.parameterValue ("seed"))));
        using osp::keydna::Param;
        k[Param::position].variation = percent (p::keyDnaPosition);
        k[Param::size].variation = percent (p::keyDnaSize);
        k[Param::density].variation = percent (p::keyDnaDensity);
        k[Param::spread].variation = percent (p::keyDnaSpread);
        k[Param::tune].variation = percent (p::keyDnaTune);
        host.setKeyDnaSettings (0, k, {}, false);
        host.setParameterValue (osp::plugin::InstrumentHost::keyDnaAmountId (0), host.parameterValue (p::keyDnaAmount));
    }

    /** Version 7: ENV LINK's one envelope is the AMP envelope (the one on screen), no longer
        the filter's. A linked session or preset from before played the filter envelope on
        both: it is copied into AMP, so it sounds as it did. */
    void migrateFromVersion6 (osp::plugin::InstrumentHost& host)
    {
        namespace p = andor::analog::params;
        if (host.parameterValue (p::envLink) < 0.5f)
            return;
        host.setParameterValue ("attack", 1000.0f * host.parameterValue (p::filterAttack));
        host.setParameterValue ("decay", 1000.0f * host.parameterValue (p::filterDecay));
        host.setParameterValue ("sustainLevel", host.parameterValue (p::filterSustain));
        host.setParameterValue ("release", 1000.0f * host.parameterValue (p::filterRelease));
    }

    /** Version 8: three engines. Card A was the oscillator pair; now OSC 1 alone, and card B
        (the shell's layer C) is OSC 2. A session or preset from before had one M / S and one EQ
        and DAMPEN for the pair: they are copied to card B, so both oscillators are muted,
        soloed, equalised and damped exactly as before (the curves are linear: EQ (1) + EQ (2)
        = EQ (1 + 2), nothing processed twice). ORGANIC (layer B) is untouched. */
    /** Version 9: MORPH hears card C live, is on by default at 50 %, and its position runs the
        other way (0 % the oscillators, 100 % card C). A session or preset from before keeps
        its MORPH exactly: its position is turned round (0 % the recording before is 100 %
        now), and one from before MORPH existed (version 3 and older) keeps it off. */
    void migrateFromVersion8 (osp::plugin::InstrumentHost& host, int from)
    {
        namespace p = andor::analog::params;
        host.setParameterValue (p::morphPosition, 100.0f - host.parameterValue (p::morphPosition));
        if (from < 4)
            host.setParameterValue (p::morphEnabled, 0.0f);
    }

    /** Version 14 (Cinematic V6): each oscillator card has its own REIMAGINED. Before, one
        amount and mode transformed the analog pair together: both cards now get that amount
        and mode, each on its own (card C is no longer transformed). */
    void migrateFromVersion13 (osp::plugin::InstrumentHost& host)
    {
        using Host = osp::plugin::InstrumentHost;
        host.setParameterValue (Host::reimaginedParameterId (2), host.parameterValue (Host::reimaginedParameterId (0)));
        for (const char* name : { "mode", "kaleidoscope.focus", "kaleidoscope.spread", "tapeFrame.age", "tapeFrame.stability", "tapeFrame.frame",
                                  "toybox.motion", "toybox.digital", "toybox.play", "mirage.clock", "mirage.filter", "mirage.tone" })
            host.setParameterValue (Host::reimaginedModeParameterId (2, name), host.parameterValue (Host::reimaginedModeParameterId (0, name)));
    }

    void migrateFromVersion7 (osp::plugin::InstrumentHost& host)
    {
        using Host = osp::plugin::InstrumentHost;
        for (const char* name : { "mute", "solo" })
            host.setParameterValue (Host::layerParameterId (2, name), host.parameterValue (Host::layerParameterId (0, name)));
        for (const auto& name : Host::eqParameterNames())
            host.setParameterValue (Host::eqParameterId (2, name), host.parameterValue (Host::eqParameterId (0, name)));
        for (const auto& name : Host::dampenParameterNames())
            host.setParameterValue (Host::dampenParameterId (2, name), host.parameterValue (Host::dampenParameterId (0, name)));
        // GRANULAR was the whole voice's: card B (OSC 2) keeps playing as it did, its own
        // switch and grains copied from card A's.
        for (const char* name : { "sourceMode", "granular.position", "granular.size", "granular.density", "granular.tune", "granular.spread" })
            host.setParameterValue (Host::layerParameterId (2, name), host.parameterValue (Host::layerParameterId (0, name)));
    }

    /** ANDOR ANALOG's materials: aged copper for ANDOR OSP's coral, layer A's amber trace,
        the walnut housing and copper badge (tools/make_walnut_skin.py). The ceramic is the
        family's. */
    osp::plugin::design::Materials walnutMaterials()
    {
        auto m = osp::plugin::design::Materials::osp();
        m.accent = juce::Colour (0xffc8703f);
        m.accentTop = juce::Colour (0xffd57e48);
        m.accentBottom = juce::Colour (0xffa9542b);
        m.accentSoft = juce::Colour (0xfff1cbb2);
        m.paletteAccent = juce::Colour (0xffcc7a48);
        m.layerA = juce::Colour (0xffd78b50);
        m.identityA = { juce::Colour (0xffd9824c), juce::Colour (0xff9c4a22), juce::Colour (0xfff0a060), juce::Colour (0xffc8703f), juce::Colour (0xffd57e48),
                        juce::Colour (0xffe4b07a) };
        m.graphiteOnWell = true;
        // The macros' identities: ANDOR OSP's mineral palette, SPACE in spore (no mauve).
        m.macros = { juce::Colour (0xffbe7552), juce::Colour (0xffb5843a), juce::Colour (0xff7b8d69),
                     juce::Colour (0xff73899c), juce::Colour (0xff8b9180), juce::Colour (0xff5c9490) };
        m.pack = AnalogMaterialsData::walnutskin_zip;
        m.packSize = static_cast<size_t> (AnalogMaterialsData::walnutskin_zipSize);
        return m;
    }

    juce::String safeFileName (const std::string& name)
    {
        return juce::File::createLegalFileName (juce::String (name));
    }

    /** A factory patch as the shell's saved state: every parameter, the patch's values where
        it has them, the defaults elsewhere (so a preset never inherits what played before). */
    std::unique_ptr<juce::XmlElement> presetXml (osp::plugin::InstrumentHost& host, const andor::analog::FactoryPatch& factory)
    {
        namespace p = andor::analog::params;
        auto values = p::valuesOf (factory.patch);
        auto tree = host.apvts().copyState();
        for (auto* p : host.apvts().processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const auto it = values.find (ranged->getParameterID());
                const float value = it != values.end() ? it->second : ranged->convertFrom0to1 (ranged->getDefaultValue());
                auto child = tree.getChildWithProperty ("id", ranged->getParameterID());
                if (child.isValid())
                    child.setProperty ("value", ranged->convertFrom0to1 (ranged->convertTo0to1 (value)), nullptr);
            }
        // Modulation links and other trees belong to the musician's own work: none here.
        for (int i = tree.getNumChildren(); --i >= 0;)
            if (! tree.getChild (i).hasType ("PARAM"))
                tree.removeChild (i, nullptr);
        auto xml = std::make_unique<juce::XmlElement> (productInfo().stateType);
        xml->setAttribute ("schemaVersion", 1);
        xml->setAttribute ("product", productInfo().slug);
        xml->setAttribute ("productVersion", productInfo().stateVersion);
        if (auto params = tree.createXml())
            xml->addChildElement (params.release());
        // KEY DNA (ANDOR OSP's tree): the sound's own profile on the oscillators' card.
        std::array<osp::keydna::Settings, osp::plugin::keyDnaState::layerCount> keyDna {};
        if (factory.keyDna > 0.0)
            keyDna[0] = andor::analog::keyDnaOf (factory);
        if (auto dna = osp::plugin::keyDnaState::toTree (keyDna, {}).createXml())
            xml->addChildElement (dna.release());
        return xml;
    }

    void installFactoryPresets (osp::plugin::InstrumentHost& host, const juce::File& folder)
    {
        // Imported Kontakt patches are reached through card C's SOUNDS only: the presets an
        // earlier import wrote leave the preset list (to the trash).
        if (auto* shell = dynamic_cast<ShellProcessor*> (&host))
            andor::analog::import::retireGeneratedPresets (folder.getParentDirectory(), shell->sampleStoreFolder().getChildFile ("Imported"));
        const auto marker = folder.getChildFile (".version");
        if (marker.loadFileAsString().trim() == factoryVersion)
            return;
        // Replace the previous version's set, and only that: ".manifest" lists every file the
        // installer wrote with the SHA-256 of what it wrote. A file the musician saved into
        // Factory, or a factory file they saved over, is never deleted. (A folder from before
        // the manifest: its preset files go to the trash, recoverable, instead of being
        // deleted.)
        const auto manifest = folder.getChildFile (".manifest");
        auto hashOf = [] (const juce::File& f) { return juce::SHA256 (f).toHexString(); };
        if (manifest.existsAsFile())
        {
            juce::StringArray lines;
            manifest.readLines (lines);
            for (const auto& line : lines)
            {
                const auto hash = line.upToFirstOccurrenceOf (" ", false, false);
                const auto relative = line.fromFirstOccurrenceOf (" ", false, false);
                if (relative.isEmpty() || relative.contains ("..") || juce::File::isAbsolutePath (relative))
                    continue;
                const auto f = folder.getChildFile (relative);
                if (f.existsAsFile() && hashOf (f) == hash)
                    f.deleteFile();
            }
        }
        else if (folder.isDirectory())
        {
            for (const auto& f : folder.findChildFiles (juce::File::findFiles, true, "*" + productInfo().presetExtension))
                if (! f.moveToTrash())
                    f.moveFileTo (f.withFileExtension (productInfo().presetExtension + ".old"));
        }
        if (! folder.createDirectory())
            return;
        juce::StringArray written;
        for (const auto& factory : andor::analog::factoryPatches())
        {
            const auto file = folder.getChildFile (juce::String (factory.category)).getChildFile (safeFileName (factory.name) + productInfo().presetExtension);
            file.getParentDirectory().createDirectory();
            // A musician's own file where a factory preset would go keeps its place: the
            // factory one is skipped (it is still in the instrument; renaming theirs gets it back).
            if (file.existsAsFile())
                continue;
            if (auto xml = presetXml (host, factory); xml != nullptr && xml->writeTo (file))
                written.add (hashOf (file) + " " + file.getRelativePathFrom (folder).replaceCharacter ('\\', '/'));
        }
        // Empty category folders left by the previous set go.
        for (const auto& d : folder.findChildFiles (juce::File::findDirectories, false))
            if (d.findChildFiles (juce::File::findFilesAndDirectories, true).isEmpty())
                d.deleteFile();
        manifest.replaceWithText (written.joinIntoString ("\n") + "\n");
        marker.replaceWithText (factoryVersion);
    }
}

const ProductInfo& productInfo()
{
    static const ProductInfo info = [] {
        ProductInfo p;
        p.name = "ANDOR Analog";
        p.slug = "analog";
        // The header: ANDOR in the display face's bold, the product in its light, the tagline under them.
        p.identity = { "ANDOR", "/ANALOG", "A living analog instrument" };
        p.presetExtension = ".andoranalog";
        p.presetFolder = "ANDOR Analog";
        p.stateType = "ANDOR_ANALOG";
        // The macro row, in data order (ECHO's knob sits before SPACE's on screen, as in
        // ANDOR OSP). DRIVE, MOVEMENT, SPACE and ECHO are ANDOR OSP's own (parameters,
        // popovers and sound); FILTER is the instrument's. The first is not assigned yet: it
        // shows ANDOR OSP's LIFE knob, which the engine does not read, so it is disabled.
        p.macros = { { { andor::analog::params::morphPosition, "MORPH", "Morph: the oscillators (0 %) and card C (100 %) fused into one sound between them" },
                       { "drive", "DRIVE", "Drive: saturation of the whole sound (tube, tape or crunch)" },
                       { andor::analog::params::tone, "FILTER", "Filter: the four-pole filter's cutoff (resonance, envelope, filter drive, LFO amount behind it)" },
                       { "motion", "MOVEMENT", "Movement: drift, tape, chorus, pulse or a rhythmic shaper" },
                       { "space", "SPACE", "Space: the room it plays in" },
                       { "echo", "ECHO", "Echo: repeats of the sound, from a tape echo or a bucket-brigade delay" } } };
        p.macroPopover = [] (int macro, osp::plugin::InstrumentHost& host) { return andor::analog::createMacroPopover (macro, host); };
        p.addParameters = [] (juce::AudioProcessorValueTreeState::ParameterLayout& layout) {
            andor::analog::params::addTo (layout);
            andor::analog::fxparams::addTo (layout);   // (state version 12: the FX library's slots, appended)
            andor::analog::params::addSubTo (layout);   // (state version 15: SUB, appended)
            andor::analog::params::addHybrid2To (layout);   // (state version 16: card C's second layer, appended)
            andor::analog::params::addSendsTo (layout);   // (state version 17: the cards' sends, appended)
            andor::analog::params::addShaperDnaTo (layout);   // (state version 18: SHAPER DNA, appended)
            andor::analog::params::addUserMacrosTo (layout);   // (state version 19: the USER macros, appended)
            andor::analog::params::addSatelliteTo (layout);    // (state version 20: SATELLITE, appended, every card off)
            andor::analog::params::addRhythmTo (layout);       // (state version 21: RHYTHMIC MORPH, appended, off)
        };
        p.installFactoryPresets = installFactoryPresets;
        // Every control a modulation route can move: ANDOR OSP's (the macros, the amplitude
        // envelope, the generator's level, pan, tune, REIMAGINED and grains) and these.
        p.modulationDestinations = andor::analog::params::modulationDestinations();
        // The card's first knob is DETUNE (OSC 2 against OSC 1), a plain control; the card's
        // heading, OSCILLATORS beside M and S, opens the OSCILLATOR WORKBENCH in the
        // waveform's dark well (the shell's in-card editor; its view is an interface
        // preference, never a parameter).
        // Three engines (docs/THREE_ENGINE_ARCHITECTURE.md): A OSC 1 (the shell's layer A), B OSC 2
        // (its layer C), C the hybrid (its layer B: ORGANIC's IDs, recordings and imports stay
        // where they are). Each oscillator's card: DETUNE, TUNE, OCT, LEVEL of its own.
        p.threeEngines.enabled = true;
        p.threeEngines.order = { 0, 2, 1 };
        p.cardArpeggiator = true;   // each card arpeggiated or holding the keys, its own pattern unlinked
        // The effects rack: MOVEMENT, ECHO and SPACE each expanded beside its macro (one at a time).
        p.rackModules = { osp::plugin::rack::Kind::movement, osp::plugin::rack::Kind::echo, osp::plugin::rack::Kind::space };
        // FILTER and MORPH: the product's own inline racks (the same rack, the same parts),
        // opened from their macro's name as the others (their popovers stay the quick view).
        p.productRacks = { { andor::analog::params::tone, osp::plugin::rack::Kind::filter, andor::analog::createFilterRack },
                           { andor::analog::params::morphPosition, osp::plugin::rack::Kind::morph, andor::analog::createMorphRack },
                           // The FX library's rack: no macro of its own; FX ROUTING opens it.
                           { {}, osp::plugin::rack::Kind::fx, andor::analog::createFxRack } };
        // MOVEMENT's two lanes (state version 11): A is MOVEMENT itself, B a second one.
        {
            using namespace andor::analog::params;
            osp::plugin::InstrumentHost::MovementLane a, b;
            a.prefix = "movement.";
            b.prefix = movementBPrefix;
            for (auto* lane : { &a, &b })
            {
                const int index = lane == &a ? 0 : 1;
                lane->onId = movementLaneId (index, "on");
                lane->levelId = movementLaneId (index, "level");
                lane->syncId = movementLaneId (index, "sync");
                lane->pulseDivisionId = movementLaneId (index, "pulse.division");
                lane->phaseId = movementLaneId (index, "phase");
            }
            p.movementLanes = { a, b };
        }
        p.mixEndLabels = { "A+B", "C" };   // (the header MIX: the analog pair against card C)
        // No header MIX for now (the cards' own levels and the routing view's balance them): the
        // preset bar takes its room, SAVE and ROUTING after it as keys of one size.
        p.headerMix = false;
        p.crossCloseKeys = true;   // (the ARPEGGIATOR closes with the racks' X)
        // MORPH's own on / off beside its name (off on a new instance; a preset or a session keeps its own).
        p.macroSwitches = { { 0, andor::analog::params::morphEnabled, "Enable / bypass Morph." } };
        // LINK, REVERSE, LOOP and FOLLOW belong to a recording (or an imported patch): card C as
        // a SAMPLE or INSTRUMENT PATCH only.
        p.cardModifiersShown = [] (int layer, osp::plugin::InstrumentHost& host) {
            if (layer != 1)
                return false;
            // (Card C's modifiers are both layers': LOOP and FOLLOW act on the second's zones too.)
            const bool second = andor::analog::hybridLayerShown (host) == 2;
            const int type = juce::roundToInt (host.parameterValue (second ? andor::analog::params::hybrid2Type : andor::analog::params::hybridType));
            return type == static_cast<int> (andor::analog::HybridType::sample) || type == static_cast<int> (andor::analog::HybridType::instrumentPatch);
        };
        // ... as a compact row of icon keys at the top-left of card C's dark well (its foot free,
        // as cards A and B), only the ones that act on what card C plays: a SAMPLE all four; an
        // INSTRUMENT PATCH's zones LINK, LOOP and FOLLOW (they do not play backwards).
        p.cardModifiersInWell = true;
        p.stableModulationTabs = true;
        // FX ROUTING (docs/FX_ROUTING_IMPLEMENTATION.md): its icon beside SAVE, its view in the
        // cards' place.
        // The shared racks' hardware faceplates (V2: docs/FX_RACK_V2_PROGRESS.md) - interface only.
        {
            using osp::plugin::rack::Kind;
            // V5 PATTERNED HARDWARE (docs/VISUAL_IDENTITY_INDEX.md): each rack wears its device's
            // identity - the same enamel and motif as its routing tile.
            namespace an = andor::analog;
            auto movement = an::faceplateFromIdentity (an::visualIdentityFor (an::fx::Type::movement));
            auto echo = an::faceplateFromIdentity (an::visualIdentityFor (an::fx::Type::echo));
            auto space = an::faceplateFromIdentity (an::visualIdentityFor (an::fx::Type::space));
            // V4: the TAPE modes (ECHO's tape echo, MOVEMENT's wow and flutter) as a light reel
            // deck (docs/FX_ROUTING_TAPE_V4.md).
            movement.tapeDeck = echo.tapeDeck = true;
            p.rackFaceplates = { { Kind::movement, movement }, { Kind::echo, echo }, { Kind::space, space } };
            p.fullBleedRacks = true;
        }
        p.sourceAreaView = { "FX Routing", [] (osp::plugin::InstrumentHost& host) { return andor::analog::createFxRoutingView (host); } };
        p.cardModifiersApplicable = [] (int layer, osp::plugin::InstrumentHost& host) {
            if (layer != 1)
                return 0xf;
            if (andor::analog::hybridLayerShown (host) == 2)
                return 1 | 4 | 8;   // (the second layer's zones: LINK, LOOP, FOLLOW - they never play backwards)
            const int type = juce::roundToInt (host.parameterValue (andor::analog::params::hybridType));
            return type == static_cast<int> (andor::analog::HybridType::instrumentPatch) ? (1 | 4 | 8) : 0xf;
        };
        p.cardControls = [] (int layer, osp::plugin::InstrumentHost& host) {
            namespace ap = andor::analog::params;
            using K = andor::shell::ProductInfo::CardKnob;
            std::array<K, 5> k {};
            // Every card the same first four (DETUNE, TUNE, OCT, LEVEL), its fifth its own.
            if (layer == 0)
                k = { K { ap::osc1Fine, "DETUNE" }, K { ap::osc1Coarse, "TUNE" }, K { ap::osc1Octave, "OCT" }, K { ap::osc1Level, "LEVEL" }, K {} };
            else if (layer == 2)   // (Cinematic V6: its own REIMAGINED fifth, as card A's; its VELO in its well)
                k = { K { ap::osc2Fine, "DETUNE" }, K { ap::osc2Coarse, "TUNE" }, K { ap::osc2Octave, "OCT" }, K { ap::osc2Level, "LEVEL" }, K {} };
            else if (andor::analog::hybridLayerShown (host) == 2)
            {
                // Card C's second layer (its keys in the well choose it): its own, by its type.
                using andor::analog::HybridType;
                const auto type = static_cast<HybridType> (std::clamp (juce::roundToInt (host.parameterValue (ap::hybrid2Type)), 0, 3));
                const K level { ap::hybrid2Level, "LEVEL" }, velo { ap::hybrid2Velo, "VELO" };
                if (type == HybridType::noise)
                    k = { K { ap::hybrid2NoiseTone, "TONE" }, K { ap::hybrid2NoiseMovement, "MOVEMENT" }, K { ap::hybrid2NoiseKeys, "KEYS" }, level, velo };
                else
                    k = { K { ap::hybrid2Fine, "DETUNE" }, K { ap::hybrid2Coarse, "TUNE" }, K { ap::hybrid2Octave, "OCT" }, level, velo };
            }
            else
            {
                // Card C by its type.
                using andor::analog::HybridType;
                const auto type = static_cast<HybridType> (std::clamp (juce::roundToInt (host.parameterValue (ap::hybridType)), 0, 4));
                const K level { "layerB.level", "LEVEL" };
                if (type == HybridType::external)
                {
                    K none;
                    none.hidden = true;
                    k = { none, none, none, level, none };   // (EXPERIMENT: the hosted instrument's own controls are in its editor)
                }
                else if (type == HybridType::oscillator)
                    k = { K { ap::osc3Fine, "DETUNE" }, K { ap::osc3Coarse, "TUNE" }, K { ap::osc3Octave, "OCT" }, level, K { ap::hybridVelo, "VELO" } };
                else if (type == HybridType::noise)
                    k = { K { ap::hybridNoiseTone, "TONE" }, K { ap::hybridNoiseMovement, "MOVEMENT" }, K { ap::hybridNoiseKeys, "KEYS" }, level, K { ap::hybridVelo, "VELO" } };
                else   // a recording or a patch: its pitch (the notes it is played with), START
                    k = { K { ap::hybridFine, "DETUNE" }, K { "layerB.tune", "TUNE" }, K { ap::hybridOctave, "OCT" }, level, K { "layerB.start", "START" } };
            }
            return k;
        };
        // Each oscillator's sound design in its card's well, revealed by the pointer (no
        // workbench to open): the selected model's own controls.
        p.wellOverlay = [] (int layer, osp::plugin::InstrumentHost& host) -> std::unique_ptr<osp::plugin::WellOverlay> {
            if (layer == 0)
                return andor::analog::createOscillatorOverlay (host, 1, layer);
            if (layer == 2)
                return andor::analog::createOscillatorOverlay (host, 2, layer);
            return andor::analog::createHybridOverlay (host);   // card C: its type and that type's controls
        };
        p.wellOverlayKey = [] (int layer, osp::plugin::InstrumentHost& host) {
            return layer == 1 ? andor::analog::hybridOverlayKey (host) : andor::analog::oscillatorOverlayKey (host, layer == 0 ? 1 : 2);
        };
        // ORGANIC: one recording beside the oscillators (the shell's layer B: ANDOR OSP's loader,
        // sample store, card and recall).
        p.hybridSampleName = "HYBRID";
        // Empty, ORGANIC is a compact ADD SAMPLE dock beside the oscillators' card (about a
        // fifth of the area); a recording opens its own card, the two then sharing equally.
        p.hybridSampleDock = false;   // (three engines: the hybrid keeps its full column)
        // The Sample Source importer: a Kontakt patch (its NKI and samples) into native sounds that
        // play in ORGANIC's place; ORGANIC's heading then picks among them.
        p.hybridAction = { "Import Kontakt patch", [] (osp::plugin::InstrumentHost& host) { andor::analog::runKontaktImport (host); } };
        p.hybridHeaderPopover = [] (osp::plugin::InstrumentHost& host) { return andor::analog::createSoundPopover (host); };
        // The oscillators' card plays them as they are (STANDARD) or as grains (GRANULAR).
        p.generatorModeName = "Regular";
        // ...shown as a GRANULAR on / off key (the grain cloud, lit in the card's colour): the
        // oscillator plays as it is unless lit. Card C keeps its mode menu (ANDOR OSP's symbols).
        p.generatorGranularToggle = true;
        // Cinematic V6 gate 2: SUB in the arpeggiator's housing ("arp" above, "sub" below).
        p.arpCompanion = { "sub", andor::analog::params::subEnabled, [] (osp::plugin::InstrumentHost& host) { return andor::analog::createSubPopover (host); } };
        // USER MACROS V1.1: the CORE | USER tab and six USER macros in the knobs' places.
        p.userMacros = andor::analog::usermacros::descriptor();
        p.rhythmSource = true;   // (C RHYTHM in the routes' source list: RHYTHMIC MORPH)
        // SATELLITE: switched and opened from the per-card arpeggiator's title row.
        p.satellite = andor::shell::ProductInfo::Satellite {
            [] (int card) { return andor::analog::params::satelliteId (juce::jlimit (0, 2, card), "enabled"); },
            [] (osp::plugin::InstrumentHost& host, int card) -> std::unique_ptr<osp::plugin::MiniPanel> {
                return std::make_unique<andor::analog::SatellitePanel> (host, card);
            }
        };
        // SHAPER DNA V1: the key on the MOVEMENT rack's SHAPER and its panel.
        p.shaperDna = andor::shell::ProductInfo::ShaperDna {
            andor::analog::params::dnaEnabled,
            [] (osp::plugin::InstrumentHost& host) -> std::unique_ptr<osp::plugin::MiniPanel> { return std::make_unique<andor::analog::ShaperDnaPanel> (host); },
            [] (osp::plugin::InstrumentHost& host) -> juce::String {
                // Why it cannot play just now (the key's tooltip): MOVEMENT not heard on MAIN, or lane A off.
                if (host.parameterValue ("analog.movement.a.on") < 0.5f)
                    return "Switch MOVEMENT lane A on for Shaper DNA";
                auto& engine = dynamic_cast<andor::analog::AnalogEngine&> (dynamic_cast<andor::shell::ShellProcessor&> (host).productEngine());
                if (! andor::analog::fx::movementOnMain (engine.fxRoute()))
                    return "Place MOVEMENT on MAIN for Shaper DNA";
                return {};
            } };
        // The walnut sibling of ANDOR OSP's teak (docs/WALNUT_VISUAL_DESIGN.md): its own
        // finish, interface only.
        p.materials = walnutMaterials();
        // The AMP envelope says when it is not what plays (VCA GATE), and editing it gives the
        // amplitude back to it.
        p.ampEnvelopeNote = [] (osp::plugin::InstrumentHost& host) {
            return host.parameterValue (andor::analog::params::vcaMode) >= 0.5f ? juce::String ("GATE") : juce::String();
        };
        p.ampEnvelopeTouched = [] (osp::plugin::InstrumentHost& host) {
            if (host.parameterValue (andor::analog::params::vcaMode) >= 0.5f)
                host.setParameterValue (andor::analog::params::vcaMode, 0.0f);
        };
        p.stateVersion = 22;   // (22: EXPERIMENT - card C's EXTERNAL type appended (indices kept) and its ExternalInstrument engine child: absent = none - nothing to migrate; 21: RHYTHMIC MORPH appended, off; C RHYTHM a 13th route source - nothing to migrate; 20: SATELLITE appended, every card off - nothing to migrate; 19: the USER macros appended, no assignment - nothing to migrate; 18: SHAPER DNA appended, OFF - nothing to migrate; 12: the FX route, an engine child: absent = CLASSIC; 13: RETRO's parameters appended - nothing to migrate; 14: card B's own REIMAGINED; 15: SUB appended - nothing to migrate; 16: card C's second layer appended, its source an engine child - nothing to migrate; 17: the cards' sends appended, MAIN + SEND's route schema 2 - nothing to migrate)
        p.migrateState = [] (osp::plugin::InstrumentHost& host, int from) {
            if (from < 2)
                migrateFromVersion1 (host);
            if (from < 3)
                migrateFromVersion2 (host);
            if (from < 4)
                migrateFromVersion3 (host);
            if (from < 5)
                migrateFromVersion4 (host);
            if (from < 6)
                migrateFromVersion5 (host);
            if (from < 7)
                migrateFromVersion6 (host);
            if (from < 8)
                migrateFromVersion7 (host);
            if (from < 9)
                migrateFromVersion8 (host, from);
            if (from < 10)
                host.setParameterValue (andor::analog::params::lfoWheel, 1.0f);   // (version 10: the wheel's link to LFO 1 is a switch, off when new; before, always on)
            if (from < 11)
            {
                // Version 11: MOVEMENT's lanes - lane A on at 100 %, lane B off: as before.
                using namespace andor::analog::params;
                juce::StringArray lanes;
                for (const int lane : { 0, 1 })
                    for (const char* name : { "on", "level", "sync", "pulse.division", "phase" })
                        lanes.add (movementLaneId (lane, name));
                for (const auto& name : movementSettingNames())
                    lanes.add (movementBPrefix + name);
                for (const auto& id : lanes)
                    if (auto* parameter = host.apvts().getParameter (id))
                        parameter->setValueNotifyingHost (parameter->getDefaultValue());
            }
            if (from < 14)
                migrateFromVersion13 (host);
        };
        return p;
    }();
    return info;
}

std::unique_ptr<Engine> createEngine()
{
    return std::make_unique<andor::analog::AnalogEngine>();
}

} // namespace andor::shell
