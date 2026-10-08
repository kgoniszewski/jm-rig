#pragma once

namespace jmrig
{

/** Plain parameter snapshot handed from the plugin layer to the engine once per
    block. The engine never touches APVTS or any other GUI-side object, so it
    stays headless and testable. All values are in user units. */
struct RigParameters
{
    float inputGainDb  = 0.0f;   // -24 .. +24 dB
    float outputGainDb = 0.0f;   // -60 .. +12 dB

    // Amp controls, 0 .. 10 like the front panel.
    float ampGain     = 5.0f;
    float ampBass     = 5.0f;
    float ampMid      = 5.0f;
    float ampTreble   = 5.0f;
    float ampPresence = 5.0f;
    float ampMaster   = 5.0f;
    bool  ampBright   = false;

    bool bypass = false;
};

} // namespace jmrig
