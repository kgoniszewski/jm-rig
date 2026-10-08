#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace jmrig
{

/** Parameter IDs are part of saved sessions and host automation: never rename
    one. To change a parameter's meaning, add a new ID and bump its version. */
namespace ParamIDs
{
    inline constexpr auto inputGain   = "inputGain";
    inline constexpr auto outputGain  = "outputGain";
    inline constexpr auto ampGain     = "ampGain";
    inline constexpr auto ampBass     = "ampBass";
    inline constexpr auto ampMid      = "ampMid";
    inline constexpr auto ampTreble   = "ampTreble";
    inline constexpr auto ampPresence = "ampPresence";
    inline constexpr auto ampMaster   = "ampMaster";
    inline constexpr auto ampBright   = "ampBright";
    inline constexpr auto cabOn       = "cabOn";
    inline constexpr auto cabBlend    = "cabBlend";
    inline constexpr auto cabLowCut   = "cabLowCut";
    inline constexpr auto cabHighCut  = "cabHighCut";
    inline constexpr auto bypass      = "bypass";
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace jmrig
