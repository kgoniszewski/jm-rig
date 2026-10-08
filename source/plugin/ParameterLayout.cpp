#include "ParameterLayout.h"

namespace jmrig
{

namespace
{
    constexpr int parameterVersion = 1;

    juce::ParameterID pid (const char* id) { return { id, parameterVersion }; }

    std::unique_ptr<juce::AudioParameterFloat> decibels (const char* id, const juce::String& name,
                                                         float min, float max, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            pid (id), name, juce::NormalisableRange<float> (min, max, 0.1f), def,
            juce::AudioParameterFloatAttributes().withLabel ("dB"));
    }

    /** Log-skewed frequency, so the knob's middle sits at the geometric mean. */
    std::unique_ptr<juce::AudioParameterFloat> frequency (const char* id, const juce::String& name,
                                                          float min, float max, float def)
    {
        juce::NormalisableRange<float> range (min, max, 1.0f);
        range.setSkewForCentre (std::sqrt (min * max));

        return std::make_unique<juce::AudioParameterFloat> (
            pid (id), name, range, def,
            juce::AudioParameterFloatAttributes()
                .withLabel ("Hz")
                .withStringFromValueFunction ([] (float v, int)
                {
                    return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz"
                                        : juce::String (juce::roundToInt (v)) + " Hz";
                }));
    }

    /** 0..10 amp knob, shown with one decimal like a numbered pot. */
    std::unique_ptr<juce::AudioParameterFloat> knob (const char* id, const juce::String& name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            pid (id), name, juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), def,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1); }));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (decibels (ParamIDs::inputGain,  "Input",  -24.0f, 24.0f, 0.0f),
                decibels (ParamIDs::outputGain, "Output", -60.0f, 12.0f, 0.0f));

    layout.add (std::make_unique<juce::AudioProcessorParameterGroup> (
        "amp", "Amp", "|",
        knob (ParamIDs::ampGain,     "Gain",     5.0f),
        knob (ParamIDs::ampBass,     "Bass",     5.0f),
        knob (ParamIDs::ampMid,      "Mid",      5.0f),
        knob (ParamIDs::ampTreble,   "Treble",   5.0f),
        knob (ParamIDs::ampPresence, "Presence", 5.0f),
        knob (ParamIDs::ampMaster,   "Master",   5.0f),
        std::make_unique<juce::AudioParameterBool> (pid (ParamIDs::ampBright), "Bright", false)));

    layout.add (std::make_unique<juce::AudioProcessorParameterGroup> (
        "cab", "Cab", "|",
        std::make_unique<juce::AudioParameterBool> (pid (ParamIDs::cabOn), "Cab On", true),
        std::make_unique<juce::AudioParameterFloat> (
            pid (ParamIDs::cabBlend), "IR Blend", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            {
                const auto b = juce::roundToInt (v * 100.0f);
                return "A " + juce::String (100 - b) + " / B " + juce::String (b);
            })),
        frequency (ParamIDs::cabLowCut,  "Low Cut",  20.0f, 500.0f, 20.0f),
        frequency (ParamIDs::cabHighCut, "High Cut", 2000.0f, 20000.0f, 20000.0f)));

    layout.add (std::make_unique<juce::AudioParameterBool> (pid (ParamIDs::bypass), "Bypass", false));

    return layout;
}

} // namespace jmrig
