#include "PowerAmp.h"

#include <algorithm>

namespace jmrig
{

void PowerAmp::prepare (double sampleRate) noexcept
{
    rate = sampleRate;

    // One-pole envelope coefficients: ~8 ms attack, ~180 ms recovery,
    // roughly a tube-rectified supply with a few tens of microfarads.
    attack  = (float) (1.0 - std::exp (-1.0 / (0.008 * sampleRate)));
    release = (float) (1.0 - std::exp (-1.0 / (0.180 * sampleRate)));

    resonance.set (false, 90.0, 2.5, sampleRate);
    transformer.setHighPass (35.0, sampleRate);
    setPresence (0.5f);
    reset();
}

void PowerAmp::reset() noexcept
{
    env = 0.0f;
    resonance.reset();
    presenceShelf.reset();
    transformer.reset();
}

void PowerAmp::setPresence (float presence) noexcept
{
    presenceShelf.set (true, 3500.0, 8.0 * std::clamp ((double) presence, 0.0, 1.0), rate);
}

} // namespace jmrig
