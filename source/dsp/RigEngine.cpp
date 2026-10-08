#include "RigEngine.h"

namespace jmrig
{

namespace
{
    constexpr double gainSmoothingSeconds   = 0.02;
    constexpr double bypassSmoothingSeconds = 0.02;

    // Multiplicative smoothing cannot reach zero, so -60 dB and below is
    // clamped to a tiny floor rather than true silence.
    float dbToGain (float db) noexcept
    {
        return juce::Decibels::decibelsToGain (db, -100.0f) + 1.0e-5f;
    }
}

void RigEngine::prepare (double sampleRate, int maxBlockSize)
{
    jassert (sampleRate > 0.0 && maxBlockSize > 0);
    maxBlock = juce::jmax (1, maxBlockSize);

    amp.prepare (sampleRate, maxBlock);
    cab.prepare (sampleRate, maxBlock);
    inputDcBlocker.prepare (sampleRate);

    inputGain.reset (sampleRate, gainSmoothingSeconds);
    outputGain.reset (sampleRate, gainSmoothingSeconds);
    wetMix.reset (sampleRate, bypassSmoothingSeconds);

    latencySamples = amp.getLatencySamples() + cab.getLatencySamples();

    dryDelay.setMaximumDelayInSamples (juce::jmax (1, latencySamples));
    dryDelay.prepare ({ sampleRate, (juce::uint32) maxBlock, 1 });
    dryDelay.setDelay ((float) latencySamples);

    dryScratch.allocate ((size_t) maxBlock, true);

    reset();
}

void RigEngine::reset() noexcept
{
    amp.reset();
    cab.reset();
    inputDcBlocker.reset();
    dryDelay.reset();

    inputGain.setCurrentAndTargetValue (inputGain.getTargetValue());
    outputGain.setCurrentAndTargetValue (outputGain.getTargetValue());
    wetMix.setCurrentAndTargetValue (wetMix.getTargetValue());
}

void RigEngine::setParameters (const RigParameters& p) noexcept
{
    inputGain.setTargetValue (dbToGain (p.inputGainDb));
    outputGain.setTargetValue (dbToGain (p.outputGainDb));
    wetMix.setTargetValue (p.bypass ? 0.0f : 1.0f);
    amp.setParameters (p);
}

void RigEngine::process (float* data, int numSamples) noexcept
{
    jassert (isPrepared());

    if (! isPrepared() || data == nullptr)
        return;

    // Hosts may hand us more than they promised in prepareToPlay; split rather
    // than overrun the pre-allocated buffers.
    for (int offset = 0; offset < numSamples; offset += maxBlock)
        processChunk (data + offset, juce::jmin (maxBlock, numSamples - offset));
}

void RigEngine::processChunk (float* data, int numSamples) noexcept
{
    // Dry copy, aligned with the wet path's latency.
    for (int i = 0; i < numSamples; ++i)
    {
        dryDelay.pushSample (0, data[i]);
        dryScratch[i] = dryDelay.popSample (0);
    }

    for (int i = 0; i < numSamples; ++i)
        data[i] = inputDcBlocker.processSample (data[i] * inputGain.getNextValue());

    amp.process (data, numSamples);
    cab.process (data, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto mix = wetMix.getNextValue();
        const auto wet = data[i] * outputGain.getNextValue();
        data[i] = dryScratch[i] + mix * (wet - dryScratch[i]);
    }
}

} // namespace jmrig
