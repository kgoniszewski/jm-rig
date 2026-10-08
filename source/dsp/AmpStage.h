#pragma once

#include <juce_dsp/juce_dsp.h>
#include "DcBlocker.h"
#include "RigParameters.h"

namespace jmrig
{

/** Amp slot: everything inside runs at the oversampled rate.

    In the skeleton the circuit is a placeholder (a smoothed drive into an
    asymmetric soft clipper, then master), so the chain makes sound and the
    oversampling, smoothing and latency plumbing can be verified. Step 3
    replaces the inside of processOversampled() with the circuit model:
    WDF tone stack plus fitted 12AX7 and power-amp stages. */
class AmpStage
{
public:
    /** 2^2 = 4x. Polyphase IIR half-band filters: a few samples of latency
        instead of the ~60 a linear-phase FIR cascade would add. */
    static constexpr size_t oversamplingOrder = 2;

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

private:
    void processOversampled (float* data, int numSamples) noexcept;

    juce::dsp::Oversampling<float> oversampling;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> drive { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> master { 1.0f };

    DcBlocker dcBlocker; // the asymmetric clipper creates DC; remove it before decimation
};

} // namespace jmrig
