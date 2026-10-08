#pragma once

#include <cmath>

namespace jmrig
{

/** One-pole/one-zero DC blocker, y[n] = x[n] - x[n-1] + R * y[n-1].
    The corner sits at ~10 Hz regardless of sample rate, well under a low E (82 Hz). */
class DcBlocker
{
public:
    void prepare (double sampleRate, double cornerHz = 10.0) noexcept
    {
        r = (float) std::exp (-2.0 * 3.14159265358979323846 * cornerHz / sampleRate);
        reset();
    }

    void reset() noexcept { x1 = y1 = 0.0f; }

    float processSample (float x) noexcept
    {
        const auto y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }

    void process (float* data, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            data[i] = processSample (data[i]);
    }

private:
    float r = 0.999f, x1 = 0.0f, y1 = 0.0f;
};

} // namespace jmrig
