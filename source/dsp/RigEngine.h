#pragma once

#include <juce_dsp/juce_dsp.h>
#include "AmpStage.h"
#include "CabStage.h"
#include "DcBlocker.h"
#include "RigParameters.h"

namespace jmrig
{

/** The whole rig as one headless, mono processor:

        input gain -> DC block -> amp (oversampled) -> cab -> output gain
                                                              \
        dry (delayed by the rig latency) ------------------ bypass crossfade

    Real-time contract: prepare() allocates; process(), setParameters() and
    reset() never allocate, lock or make system calls. process() accepts any
    block length, including 0 and lengths above the prepared maximum (it
    splits them internally). */
class RigEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;

    /** Audio thread, once per block before process(). */
    void setParameters (const RigParameters& p) noexcept;

    /** Mono, in place. */
    void process (float* data, int numSamples) noexcept;

    /** Total latency to report to the host. Constant after prepare(). */
    int getLatencySamples() const noexcept { return latencySamples; }

    bool isPrepared() const noexcept { return maxBlock > 0; }

    /** Message or loader thread; wait-free with respect to process(). */
    void loadImpulseResponse (CabStage::Slot slot, juce::AudioBuffer<float> impulse, double impulseSampleRate,
                              bool normalise = true)
    {
        cab.loadImpulseResponse (slot, std::move (impulse), impulseSampleRate, normalise);
    }

    void loadDefaultImpulseResponse (CabStage::Slot slot) { cab.loadDefaultImpulseResponse (slot); }

    int getCurrentImpulseSize (CabStage::Slot slot) const { return cab.getCurrentImpulseSize (slot); }

private:
    void processChunk (float* data, int numSamples) noexcept;

    AmpStage amp;
    CabStage cab;
    DcBlocker inputDcBlocker;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGain { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetMix { 1.0f };

    // Dry path for latency-compensated bypass.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay;
    juce::HeapBlock<float> dryScratch;

    int maxBlock = 0;
    int latencySamples = 0;
};

} // namespace jmrig
