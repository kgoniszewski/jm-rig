#pragma once

#include <juce_dsp/juce_dsp.h>
#include "amp/PowerAmp.h"
#include "amp/Preamp.h"
#include "RigParameters.h"

namespace jmrig
{

/** The amp: a high-headroom, Dumble/Two-Rock-style clean channel, modelled
    stage by stage from the circuit. Everything inside runs 4x oversampled.

        Preamp (V1A, tone stack, Gain    see Preamp.h; checked against
          pot + bright, V1B)             SPICE in tests/SpiceNullTests.cpp
        Master                           attenuation
        PI + push-pull + OT + NFB        PowerAmp (sag, presence, resonance)

    Signal levels are in volts from the guitar input to V1B's plate, so the
    breakup point falls where it does in the circuit: V1A stays clean for
    normal pickups, and V1B breaks up as the Gain control rises. */
class AmpStage
{
public:
    /** 2^2 = 4x. Polyphase IIR half-band filters: a few samples of latency
        instead of the ~60 a linear-phase FIR cascade would add. */
    static constexpr size_t oversamplingOrder = 2;

    /** The oversampler configuration the amp uses (tests measure its latency). */
    static std::unique_ptr<juce::dsp::Oversampling<float>> makeOversampler();

    AmpStage();

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;

    /** Call once per block, before process(). Only sets smoothing targets. */
    void setParameters (const RigParameters& p) noexcept;

    /** Mono, in place. numSamples must be <= the prepared maxBlockSize. */
    void process (float* data, int numSamples) noexcept;

    /** Integer latency of the oversampling round trip, at the base rate. */
    int getLatencySamples() const noexcept;

    int getOversamplingFactor() const noexcept { return 1 << oversamplingOrder; }

    /** Front-panel 0..10 to an audio-taper (log) pot's electrical position. */
    static double audioTaper (double knob) noexcept;

private:
    void processOversampled (float* data, int numSamples) noexcept;
    void updateControls() noexcept;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    double osRate = 192000.0;

    Preamp preamp;
    PowerAmp powerAmp;

    // Controls that recompute coefficients are updated every controlInterval
    // oversampled samples from these smoothed values.
    static constexpr int controlInterval = 32;
    int samplesUntilUpdate = 0;

    using Linear = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;
    Linear gain { 0.5f }, treble { 0.5f }, bass { 0.5f }, mid { 0.5f }, presence { 0.5f }, bright { 0.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> master { 1.0f };
};

} // namespace jmrig
