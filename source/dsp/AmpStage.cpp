#include "AmpStage.h"

namespace jmrig
{

namespace
{
    constexpr double smoothingSeconds = 0.03;

    // V1B's plate swing (volts) that drives the output pair to the onset of
    // clipping with Master at full; and the final output scaling.
    constexpr float powerAmpVoltsAtClip = 12.0f;
    constexpr float outputScale = 0.9f;

    float knob01 (float knob) noexcept { return juce::jlimit (0.0f, 10.0f, knob) / 10.0f; }
}

std::unique_ptr<juce::dsp::Oversampling<float>> AmpStage::makeOversampler()
{
    return std::make_unique<juce::dsp::Oversampling<float>> (
        1, oversamplingOrder,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true,   // max quality
        true);  // integer latency, so the dry path and the host stay aligned
}

double AmpStage::audioTaper (double knob) noexcept
{
    // Log ("A") taper: about 10% of the resistance at half rotation.
    const auto x = juce::jlimit (0.0, 10.0, knob) / 10.0;
    return (std::pow (10.0, 2.0 * x) - 1.0) / 99.0;
}

AmpStage::AmpStage() : oversampling (makeOversampler()) {}

void AmpStage::prepare (double sampleRate, int maxBlockSize)
{
    // All allocation happens here, never on the audio thread.
    oversampling->initProcessing ((size_t) maxBlockSize);
    osRate = sampleRate * getOversamplingFactor();

    preamp.prepare (osRate);
    powerAmp.prepare (osRate);

    for (auto* v : { &gain, &treble, &bass, &mid, &presence, &bright })
        v->reset (osRate, smoothingSeconds);

    master.reset (osRate, smoothingSeconds);

    reset();
}

void AmpStage::reset() noexcept
{
    oversampling->reset();

    for (auto* v : { &gain, &treble, &bass, &mid, &presence, &bright })
        v->setCurrentAndTargetValue (v->getTargetValue());

    master.setCurrentAndTargetValue (master.getTargetValue());

    preamp.reset();
    powerAmp.reset();

    samplesUntilUpdate = 0;
    updateControls();
}

void AmpStage::setParameters (const RigParameters& p) noexcept
{
    gain.setTargetValue ((float) audioTaper (p.ampGain));
    treble.setTargetValue ((float) audioTaper (p.ampTreble));
    bass.setTargetValue ((float) audioTaper (p.ampBass));
    mid.setTargetValue (knob01 (p.ampMid)); // linear pot
    presence.setTargetValue (knob01 (p.ampPresence));
    bright.setTargetValue (p.ampBright ? 1.0f : 0.0f);
    master.setTargetValue ((float) audioTaper (p.ampMaster) + 1.0e-4f);
}

int AmpStage::getLatencySamples() const noexcept
{
    return (int) std::lround (oversampling->getLatencyInSamples());
}

void AmpStage::process (float* data, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    float* channels[] = { data };
    juce::dsp::AudioBlock<float> block (channels, 1, (size_t) numSamples);

    auto up = oversampling->processSamplesUp (block);
    processOversampled (up.getChannelPointer (0), (int) up.getNumSamples());
    oversampling->processSamplesDown (block);
}

void AmpStage::updateControls() noexcept
{
    preamp.setControls (gain.getCurrentValue(), treble.getCurrentValue(), bass.getCurrentValue(),
                        mid.getCurrentValue(), bright.getCurrentValue());
    powerAmp.setPresence (presence.getCurrentValue());
}

void AmpStage::processOversampled (float* data, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        if (--samplesUntilUpdate <= 0)
        {
            for (auto* v : { &gain, &treble, &bass, &mid, &presence, &bright })
                v->skip (controlInterval);

            updateControls();
            samplesUntilUpdate = controlInterval;
        }

        const auto v = preamp.processSample (data[i]); // volts at V1B's plate
        const auto drive = v * master.getNextValue() / powerAmpVoltsAtClip;
        data[i] = powerAmp.processSample (drive) * outputScale;
    }
}

} // namespace jmrig
