#pragma once

#include <algorithm>
#include "Filters.h"

namespace jmrig
{

/** Phase inverter, push-pull output pair and output transformer, as a
    behavioural model fitted to how such a power section responds:

    - Push-pull clipping is symmetric (even harmonics cancel), with a soft
      knee: y = x / sqrt(1 + x^2), scaled by the available headroom.
    - Sag: the rectifier and filter caps droop under sustained load, so the
      headroom falls with a fast attack and slow recovery. This is what makes
      a high-headroom clean amp "bloom" and compress when pushed.
    - Global negative feedback flattens the response; the presence control
      reduces feedback at high frequencies, heard as a treble shelf. The
      speaker's impedance rise at its resonance does the same in the lows.
    - The output transformer rolls off below ~35 Hz.

    Input is normalised so that 1.0 is where the output pair starts to
    clip; output is normalised to the same scale. */
class PowerAmp
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /** presence 0..1. Allocation-free; call at most every few samples. */
    void setPresence (float presence) noexcept;

    float processSample (float x) noexcept
    {
        // Supply envelope: how hard the output pair is being driven.
        const auto level = std::abs (x);
        env += (level > env ? attack : release) * (level - env);

        const auto headroom = 1.0f - sagDepth * std::min (env, 1.5f) / 1.5f;
        const auto u = x / headroom;
        auto y = headroom * u / std::sqrt (1.0f + u * u);

        y = resonance.processSample (y);
        y = presenceShelf.processSample (y);
        return transformer.processSample (y);
    }

private:
    static constexpr float sagDepth = 0.25f; // up to -2.5 dB of headroom

    double rate = 192000.0;
    float attack = 0.0f, release = 0.0f, env = 0.0f;

    Shelf resonance, presenceShelf;
    FirstOrder transformer;
};

} // namespace jmrig
