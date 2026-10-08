#include "AmpStage.h"

namespace jmrig
{

namespace
{
    constexpr double smoothingSeconds = 0.03;

    /** Front-panel 0..10 to a linear factor between minDb and maxDb. */
    float knobToGain (float knob, float minDb, float maxDb) noexcept
    {
        const auto t = juce::jlimit (0.0f, 10.0f, knob) / 10.0f;
        return juce::Decibels::decibelsToGain (minDb + t * (maxDb - minDb));
    }

    /** Placeholder triode-ish curve: tanh with a small bias for even harmonics,
        offset so that f(0) = 0. Bounded, smooth, no lookup table. */
    inline float softClip (float x) noexcept
    {
        constexpr float bias = 0.15f;
        static const float offset = std::tanh (bias);
        return std::tanh (x + bias) - offset;
    }
}

AmpStage::AmpStage()
    : oversampling (1,
                    oversamplingOrder,
                    juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                    true,   // max quality
                    true)   // integer latency, so the dry path and the host stay aligned
{
}

void AmpStage::prepare (double sampleRate, int maxBlockSize)
{
    // All allocation happens here, never on the audio thread.
    oversampling.initProcessing ((size_t) maxBlockSize);

    const auto osRate = sampleRate * getOversamplingFactor();
    drive.reset (osRate, smoothingSeconds);
    master.reset (osRate, smoothingSeconds);
    dcBlocker.prepare (osRate);

    reset();
}

void AmpStage::reset() noexcept
{
    oversampling.reset();
    drive.setCurrentAndTargetValue (drive.getTargetValue());
    master.setCurrentAndTargetValue (master.getTargetValue());
    dcBlocker.reset();
}

void AmpStage::setParameters (const RigParameters& p) noexcept
{
    drive.setTargetValue (knobToGain (p.ampGain, -6.0f, 30.0f));
    master.setTargetValue (knobToGain (p.ampMaster, -30.0f, 0.0f));
}

int AmpStage::getLatencySamples() const noexcept
{
    return (int) std::lround (oversampling.getLatencyInSamples());
}

void AmpStage::process (float* data, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    float* channels[] = { data };
    juce::dsp::AudioBlock<float> block (channels, 1, (size_t) numSamples);

    auto up = oversampling.processSamplesUp (block);
    processOversampled (up.getChannelPointer (0), (int) up.getNumSamples());
    oversampling.processSamplesDown (block);
}

void AmpStage::processOversampled (float* data, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const auto shaped = softClip (data[i] * drive.getNextValue());
        data[i] = dcBlocker.processSample (shaped) * master.getNextValue();
    }
}

} // namespace jmrig
