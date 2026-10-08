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

    layout.add (std::make_unique<juce::AudioParameterBool> (pid (ParamIDs::bypass), "Bypass", false));

    return layout;
}

} // namespace jmrig
