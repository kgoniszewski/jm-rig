#pragma once

#include <juce_dsp/juce_dsp.h>
#include "RigParameters.h"

namespace jmrig
{

/** Cabinet: two impulse-response slots (A and B, e.g. two mics on the same
    speaker) blended together, then a low cut and a high cut.

        in -> conv A --\
                        blend -> low cut -> high cut -> cab on/off crossfade -> out
        in -> conv B --/

    Convolution is JUCE's non-uniform partitioned engine with a 256-sample
    zero-latency head, so the cab adds no latency at any block size.

    IR loading is wait-free: loadImpulseResponse() may be called from the
    message thread or a loader thread while audio runs. The engine resamples
    the IR to the current rate on a background thread and crossfades it in. */
class CabStage
{
public:
    enum class Slot { a, b };

    /** IRs longer than this are truncated with a short fade. Cab IRs are
        rarely over 200 ms; this keeps CPU bounded on the iPad. */
    static constexpr double maxImpulseSeconds = 0.5;

    CabStage();

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;

    /** Audio thread, once per block before process(). */
    void setParameters (const RigParameters& p) noexcept;

    /** Mono, in place. numSamples must be <= the prepared maxBlockSize. */
    void process (float* data, int numSamples) noexcept;

    int getLatencySamples() const noexcept { return 0; }

    /** Not the audio thread: the buffer is moved, never copied, but preparing
        it (truncation, mono fold-down) allocates. */
    void loadImpulseResponse (Slot slot, juce::AudioBuffer<float> impulse, double impulseSampleRate,
                              bool normalise = true);

    /** Reverts a slot to the built-in generic cab response. */
    void loadDefaultImpulseResponse (Slot slot);

    /** Built-in fallback so the rig sounds like an amp in a cab before any
        user IR is loaded: a generic closed-back 12" curve (low resonance,
        upper-mid presence peak, steep roll-off above ~5 kHz). Not a capture
        of any real cab. */
    static juce::AudioBuffer<float> makeDefaultImpulseResponse (double sampleRate);

    /** Number of samples of the IR currently in use in a slot (for tests). */
    int getCurrentImpulseSize (Slot slot) const;

private:
    juce::dsp::Convolution& convolution (Slot s) noexcept { return s == Slot::a ? convA : convB; }

    juce::dsp::ConvolutionMessageQueue loaderQueue; // must outlive both convolutions
    juce::dsp::Convolution convA { juce::dsp::Convolution::NonUniform { 256 }, loaderQueue };
    juce::dsp::Convolution convB { juce::dsp::Convolution::NonUniform { 256 }, loaderQueue };

    juce::dsp::StateVariableTPTFilter<float> lowCut, highCut;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> blend { 0.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetMix { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowCutHz { 20.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> highCutHz { 20000.0f };

    juce::HeapBlock<float> dryScratch, slotBScratch;
    double sampleRate = 48000.0;
    int maxBlock = 0;
};

} // namespace jmrig
