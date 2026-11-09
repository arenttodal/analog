#pragma once

#include "fx/FxGraph.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace osp::plugin::rack
{
struct Faceplate;
}

namespace andor::analog
{

/**
    V5 PATTERNED HARDWARE (docs/VISUAL_IDENTITY_INDEX.md): one visual identity per device -
    its muted enamel, its original motif, its inks and its graph glass - shared by the routing
    tile, the FX library, the quick settings and the full 214 px rack, so the miniature and the
    rack read as the same instrument at two scales.

    Interface only: derived from the effect's type (never saved, never a parameter), painted on
    the message thread from cached-free vector geometry with fixed seeds (the same picture in
    every session and screenshot).
*/
enum class Motif
{
    flow,        ///< MOVEMENT: flowing modulated contour lines
    branch,      ///< DRIVE: saturation transfer curves branching and converging
    rings,       ///< ECHO: concentric delayed repeats fading from the left
    sweep,       ///< FX FILTER: low-pass responses, knee and resonance, descending
    particles,   ///< SPACE: a diffuse particle field decaying away from its source
    loops,       ///< STEREO SPREAD: two linked circles and their lens
    gainScale,   ///< COMPRESSOR: a calibrated gain scale and a knee
    bands,       ///< EQ: three low smooth band arcs
    notches,     ///< PHASER: nested phase arcs with notches
    comb,        ///< FLANGER: close comb teeth swept by an interference
    hatch,       ///< FREQ SHIFTER: staggered, offset wave hatch
    impulse,     ///< CONVOLUTION: an impulse response silhouette
    graticule,   ///< UTILITY: meter bars and reference ticks
    grooves,     ///< RETRO: offset concentric grooves (tape pack / record)
    weave,       ///< MORPH: two intertwined spectral paths
    resonance    ///< voice FILTER: a resonant response family
};

/** Where a motif is drawn: the library thumbnail, the routing tile, the full rack's face. */
enum class MotifScale
{
    thumb,
    mini,
    full
};

struct FxVisualIdentity
{
    juce::Colour enamel;       ///< the face (tile and rack body)
    juce::Colour rail;         ///< the rack's title rail: the same enamel, a step deeper
    juce::Colour edge;         ///< the 1 px edge
    juce::Colour text;         ///< the name (bone on dark enamel, carbon on light)
    juce::Colour mutedText;    ///< the mode / captions
    juce::Colour motifInk;     ///< the motif's lines; knob arcs on the face
    juce::Colour statusActive; ///< the status light, lit
    juce::Colour glassTop, glassBottom;   ///< the graph's glass
    Motif motif = Motif::flow;
    const char* tileName = "";     ///< the routing tile's title
    const char* descriptor = "";   ///< the rack's subtitle
};

/** The identity of an effect type (the post-FX library). */
const FxVisualIdentity& visualIdentityFor (fx::Type type) noexcept;

/** The synthesis racks (upstream, never routing tiles). */
enum class SynthRack
{
    voiceFilter,
    morph
};
const FxVisualIdentity& synthIdentity (SynthRack rack) noexcept;

/** The device's motif in `bounds` (clipped by the caller), in its ink, at a scale. */
void paintFxMotif (juce::Graphics&, Motif, juce::Rectangle<float> bounds, const FxVisualIdentity&, MotifScale);

/** The full rack's faceplate from an identity: the enamel body, the inks, the motif painter. */
osp::plugin::rack::Faceplate faceplateFromIdentity (const FxVisualIdentity&);

/** WCAG contrast ratio of two colours (tests: every name readable on its enamel). */
float contrastRatio (juce::Colour a, juce::Colour b) noexcept;

} // namespace andor::analog
