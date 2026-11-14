#pragma once

#include "fx/Effects.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <complex>
#include <functional>
#include <vector>

namespace andor::analog
{

/**
    The card EQ's spectrum analyser (ported from ANDOR OSP's EqSpectrum; the routing EQUALIZER rack's
    behind its curve): what plays through an EQ, from the processor's two SpectrumTaps (input
    and output), windowed and through an FFT on the message thread, in dB per bin - fast up, a
    readable fall (45 dB/s), down to the floor when nothing new arrives. Drawn as the output
    filled in the device's ink with the input a quiet line (Pro-Q / EQ Eight style), on its own
    scale (floor to 0 dBFS) under the EQ's curve. Message thread only.
*/
class EqSpectrum
{
public:
    static constexpr int fftSize = 2048;
    static constexpr float floorDb = -96.0f;

    EqSpectrum();
    /** One analysis step (an editor timer's): true when what is shown moved. */
    bool analyse (const fx::SpectrumTap& input, const fx::SpectrumTap& output);
    /** The output's spectrum now (dB per bin; the floor when nothing has played). */
    float peakDb() const noexcept;
    /** The spectrum in `plot` (clipped to it): `hzForX` the graph's own frequency axis, `rate`
        the processor's sample rate; the input a line in `lineColour`, the output filled in `ink`. */
    void paint (juce::Graphics& g, juce::Rectangle<float> plot, const std::function<double (float)>& hzForX, double rate, juce::Colour lineColour,
                juce::Colour ink) const;

private:
    bool transform (const fx::SpectrumTap& tap, std::array<float, fftSize / 2>& out, bool fresh);
    static void fft (std::vector<std::complex<float>>& a);

    std::vector<std::complex<float>> buffer;
    std::array<float, fftSize> window {};
    std::array<float, fftSize / 2> postDb {}, preDb {};
    int lastHead = -1;
};

} // namespace andor::analog
