#include "CabStage.h"

namespace jmrig
{

namespace
{
    constexpr double smoothingSeconds = 0.03;

    // Filter cutoffs are recomputed every few samples rather than every
    // sample: setCutoffFrequency() costs a tan(), and 16 samples at 44.1 kHz
    // is far below anything audible as zipper noise on a smoothed sweep.
    constexpr int cutoffUpdateInterval = 16;

    /** Keeps channel 0 only (a stereo IR file is usually two mics or two
        speakers; summing them would comb-filter), truncates to the maximum
        length and fades the last 5 ms so the cut does not click.

        Normalising scales the IR to unit energy, so white noise passes at the
        same RMS through any IR. JUCE's own option leaves a fixed -18 dB on top
        of that, which made every cab sound much quieter than the amp alone.
        IRs captured at different levels (Celestion's Lo-Gn and Hi-Gn sets)
        then come out at the same loudness. */
    juce::AudioBuffer<float> prepareImpulse (juce::AudioBuffer<float> in, double sampleRate, bool normalise)
    {
        const auto maxSamples = (int) (CabStage::maxImpulseSeconds * sampleRate);
        const auto length = juce::jmin (in.getNumSamples(), maxSamples);

        juce::AudioBuffer<float> out (1, juce::jmax (1, length));
        out.clear();

        if (length > 0 && in.getNumChannels() > 0)
            out.copyFrom (0, 0, in, 0, 0, length);

        if (in.getNumSamples() > maxSamples)
        {
            const auto fade = juce::jmin (length, (int) (0.005 * sampleRate));
            out.applyGainRamp (0, length - fade, fade, 1.0f, 0.0f);
        }

        if (normalise)
        {
            const auto* d = out.getReadPointer (0);
            double energy = 0.0;

            for (int i = 0; i < out.getNumSamples(); ++i)
                energy += (double) d[i] * d[i];

            if (energy > 1.0e-12)
                out.applyGain ((float) (1.0 / std::sqrt (energy)));
        }

        return out;
    }
}

CabStage::CabStage()
{
    lowCut.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    highCut.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

    // Butterworth-like response, no resonant bump at the cutoff.
    lowCut.setResonance (1.0f / juce::MathConstants<float>::sqrt2);
    highCut.setResonance (1.0f / juce::MathConstants<float>::sqrt2);

    loadDefaultImpulseResponse (Slot::a);
    loadDefaultImpulseResponse (Slot::b);
}

void CabStage::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    maxBlock = juce::jmax (1, maxBlockSize);

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlock, 1 };
    convA.prepare (spec);
    convB.prepare (spec);
    lowCut.prepare (spec);
    highCut.prepare (spec);

    blend.reset (sampleRate, smoothingSeconds);
    wetMix.reset (sampleRate, smoothingSeconds);
    lowCutHz.reset (sampleRate, smoothingSeconds);
    highCutHz.reset (sampleRate, smoothingSeconds);

    dryScratch.allocate ((size_t) maxBlock, true);
    slotBScratch.allocate ((size_t) maxBlock, true);

    reset();
}

void CabStage::reset() noexcept
{
    convA.reset();
    convB.reset();

    blend.setCurrentAndTargetValue (blend.getTargetValue());
    wetMix.setCurrentAndTargetValue (wetMix.getTargetValue());
    lowCutHz.setCurrentAndTargetValue (lowCutHz.getTargetValue());
    highCutHz.setCurrentAndTargetValue (highCutHz.getTargetValue());

    lowCut.setCutoffFrequency (lowCutHz.getCurrentValue());
    highCut.setCutoffFrequency (highCutHz.getCurrentValue());
    lowCut.reset();
    highCut.reset();
}

void CabStage::setParameters (const RigParameters& p) noexcept
{
    blend.setTargetValue (juce::jlimit (0.0f, 1.0f, p.cabBlend));
    wetMix.setTargetValue (p.cabOn ? 1.0f : 0.0f);

    // Keep the high cut below Nyquist at any host rate.
    const auto nyquistLimit = (float) (sampleRate * 0.45);
    lowCutHz.setTargetValue (juce::jlimit (10.0f, 1000.0f, p.cabLowCutHz));
    highCutHz.setTargetValue (juce::jlimit (1000.0f, juce::jmin (20000.0f, nyquistLimit), p.cabHighCutHz));
}

void CabStage::process (float* data, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    jassert (numSamples <= maxBlock);

    juce::FloatVectorOperations::copy (dryScratch.get(), data, numSamples);
    juce::FloatVectorOperations::copy (slotBScratch.get(), data, numSamples);

    {
        float* a[] = { data };
        float* b[] = { slotBScratch.get() };
        juce::dsp::AudioBlock<float> blockA (a, 1, (size_t) numSamples);
        juce::dsp::AudioBlock<float> blockB (b, 1, (size_t) numSamples);
        convA.process (juce::dsp::ProcessContextReplacing<float> (blockA));
        convB.process (juce::dsp::ProcessContextReplacing<float> (blockB));
    }

    for (int i = 0; i < numSamples; ++i)
    {
        if (i % cutoffUpdateInterval == 0)
        {
            lowCut.setCutoffFrequency (lowCutHz.skip (juce::jmin (cutoffUpdateInterval, numSamples - i)));
            highCut.setCutoffFrequency (highCutHz.skip (juce::jmin (cutoffUpdateInterval, numSamples - i)));
        }

        const auto mixB = blend.getNextValue();
        auto y = data[i] + mixB * (slotBScratch[i] - data[i]);
        y = highCut.processSample (0, lowCut.processSample (0, y));

        const auto wet = wetMix.getNextValue();
        data[i] = dryScratch[i] + wet * (y - dryScratch[i]);
    }

    lowCut.snapToZero();
    highCut.snapToZero();
}

void CabStage::loadImpulseResponse (Slot slot, juce::AudioBuffer<float> impulse, double impulseSampleRate,
                                    bool normalise)
{
    convolution (slot).loadImpulseResponse (prepareImpulse (std::move (impulse), impulseSampleRate, normalise),
                                            impulseSampleRate,
                                            juce::dsp::Convolution::Stereo::no,
                                            juce::dsp::Convolution::Trim::yes,
                                            juce::dsp::Convolution::Normalise::no);
}

void CabStage::loadDefaultImpulseResponse (Slot slot)
{
    constexpr double rate = 48000.0;
    loadImpulseResponse (slot, makeDefaultImpulseResponse (rate), rate);
}

int CabStage::getCurrentImpulseSize (Slot slot) const
{
    return (slot == Slot::a ? convA : convB).getCurrentIRSize();
}

juce::AudioBuffer<float> CabStage::makeDefaultImpulseResponse (double rate)
{
    using Coeffs = juce::dsp::IIR::Coefficients<float>;

    // Shaped by filtering a unit impulse; each stage is one feature of a
    // typical closed-back 1x12 response.
    const std::array<Coeffs::Ptr, 7> stages {
        Coeffs::makeHighPass  (rate,   75.0, 0.8f),          // sealed-box low-end roll-off
        Coeffs::makePeakFilter (rate, 110.0, 1.4f, 1.6f),    // cone resonance
        Coeffs::makePeakFilter (rate, 450.0, 0.9f, 0.8f),    // low-mid dip
        Coeffs::makePeakFilter (rate, 2500.0, 1.1f, 1.9f),   // presence peak
        Coeffs::makePeakFilter (rate, 4000.0, 2.5f, 0.7f),   // cone break-up notch
        Coeffs::makeLowPass   (rate, 5200.0, 0.7f),          // speaker roll-off, 24 dB/oct
        Coeffs::makeLowPass   (rate, 5200.0, 0.7f),          // as two 2nd-order stages
    };

    const auto length = (int) (0.085 * rate);
    juce::AudioBuffer<float> ir (1, length);
    ir.clear();
    ir.setSample (0, 0, 1.0f);

    for (auto& c : stages)
    {
        juce::dsp::IIR::Filter<float> f (c);
        auto* d = ir.getWritePointer (0);

        for (int i = 0; i < length; ++i)
            d[i] = f.processSample (d[i]);
    }

    const auto fade = (int) (0.01 * rate);
    ir.applyGainRamp (0, length - fade, fade, 1.0f, 0.0f);
    return ir;
}

} // namespace jmrig
