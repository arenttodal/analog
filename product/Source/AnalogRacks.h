#pragma once

#include "ParameterRack.h"

namespace andor::analog
{

/** FILTER's inline rack (macro FILTER: cutoff): its three pages FILTER, VOICE and PAIR, the
    controls the FILTER popover has, on the approved rack's parts, with a computed graph. */
std::unique_ptr<osp::plugin::rack::Module> createFilterRack (osp::plugin::InstrumentHost& host);
/** MORPH's inline rack (macro MORPH): ON, METHOD, CARRIER, its five knobs and its setup. */
std::unique_ptr<osp::plugin::rack::Module> createMorphRack (osp::plugin::InstrumentHost& host);

namespace filtergraph
{
    /** The low-pass's small-signal magnitude (dB) at `hz` for the FILTER page's graph: the
        engine's own model linearised (LadderFilter: H = G^4 / (1 + k G^4), k = 4.15 RESO; OTA:
        its calibration's k and pass-band restore, the 12 dB slope tapping the second stage), with
        the high-pass position before it (VoiceHighPass's corners). G = 1 / (1 + j f / fc). */
    struct Settings
    {
        double cutoffHz = 1000.0, resonance = 0.0;
        bool ota = false, jupiter = false, twoPole = false;
        int highPass = -1;      ///< -1 off, 0..3 the positions
        int junoVariant = 2;    ///< 0 JUNO-6, 1 JUNO-60, 2 JUNO-106
    };
    double responseDb (const Settings& s, double hz) noexcept;

    /** Tests: the FILTER page's live graph (FX Rack V3) - mode 0 idle, 1 LIVE (an engine
        voice's telemetry), 2 PREVIEW (silent); the cutoffs it shows as MIDI notes. With
        `advanceSeconds` >= 0 it first runs one refresh as if that much time had passed.
        False: `graph` is not FILTER's graph. */
    struct LiveView
    {
        int mode = 0;
        double baseNote = 0.0, nowNote = 0.0, reachNote = 0.0, peakNote = 0.0;
        int note = 0, velocity = 0, stage = 0;
    };
    bool liveView (juce::Component* graph, LiveView& out, double advanceSeconds = -1.0);

    /** The ladder's drive stage as LadderFilter applies it (its feedback aside): the input
        times the drive's gain, plus the bias, through the saturator, the bias's DC removed:
        u = tanh (gain x + bias) - tanh (bias). DRIVE and BIAS 0..1, as the parameters / 100. */
    struct DriveCurve
    {
        double gain = 1.0, bias = 0.0;
        double at (double x) const noexcept;
    };
    DriveCurve driveCurve (double drive, double bias) noexcept;
}

} // namespace andor::analog
