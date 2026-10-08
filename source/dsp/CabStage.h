#pragma once

namespace jmrig
{

/** Cabinet slot. Pass-through in the skeleton; step 2 replaces this with the
    low-latency IR convolver (user Celestion IRs, mic blend, low/high cut).
    The interface is the one the convolver will keep. */
class CabStage
{
public:
    void prepare (double /*sampleRate*/, int /*maxBlockSize*/) noexcept {}
    void reset() noexcept {}
    void process (float* /*data*/, int /*numSamples*/) noexcept {}
    int getLatencySamples() const noexcept { return 0; }
};

} // namespace jmrig
