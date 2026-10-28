#pragma once

#include "AnalogParameters.h"
#include "ShellEngine.h"
#include "Synth.h"

#include <atomic>

namespace andor::analog
{

/**
    ANDOR ANALOG's engine at the ANDOR Instrument Shell's boundary: it reads the parameters
    (raw values cached in prepare()), turns the block's MIDI into the synth's events at their
    own sample positions, and renders. Everything the synth does is in dsp/ (pure C++).

    Threads: prepare() on the message thread (allocates); process() on the audio thread
    (no allocation, no locks); sourcePicture() on the message thread (parameters only).
*/
class AnalogEngine final : public andor::shell::Engine
{
public:
    /** 4x is the shipping quality; tests may ask for less. */
    explicit AnalogEngine (int oversampling = 4) : oversamplingFactor (oversampling) {}

    Description describe() const override;
    juce::String generatorName() const override { return "OSCILLATORS"; }
    juce::StringArray boundParameters() const override { return params::engineParameterIds(); }
    void prepare (double sampleRate, int maximumBlockSize, juce::AudioProcessorValueTreeState& parameters) override;
    int latencySamples() const override { return synth.latencySamples(); }
    void setSource (int, const osp::plugin::LoadedInstrument*) noexcept override {}
    void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Performance& performance) noexcept override;
    int activeNotes() const noexcept override { return synth.activeVoices(); }
    bool sourcePicture (int layer, const juce::AudioProcessorValueTreeState& parameters, std::vector<float>& cycle) const override;

    /** One period of the oscillators' mix for a patch (the card's picture). */
    static std::vector<float> pictureOf (const Patch& patch, int points = 256);

private:
    int oversamplingFactor;
    Synth synth;
    params::Reader reader;
    std::vector<float> scratch;
    float lastScreenPitch = 0.0f, lastScreenMod = 0.0f;
    bool prepared = false;
};

} // namespace andor::analog
