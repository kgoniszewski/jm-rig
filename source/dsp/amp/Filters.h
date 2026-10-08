#pragma once

#include <cmath>

namespace jmrig
{

/** Allocation-free filters for the amp's inner loop. JUCE's IIR coefficient
    factories return reference-counted objects (a heap allocation), so they
    cannot be recomputed on the audio thread. */

/** First-order section from analog H(s) = (b0 + b1 s) / (a0 + a1 s), via
    the bilinear transform. */
struct FirstOrder
{
    void setAnalog (double b0, double b1, double a0, double a1, double sampleRate) noexcept
    {
        const auto K = 2.0 * sampleRate;
        const auto n = 1.0 / (a0 + a1 * K);
        bz0 = (float) ((b0 + b1 * K) * n);
        bz1 = (float) ((b0 - b1 * K) * n);
        az1 = (float) ((a0 - a1 * K) * n);
    }

    void setLowPass (double hz, double sampleRate) noexcept
    {
        const auto w = 2.0 * 3.14159265358979323846 * hz;
        setAnalog (w, 0.0, w, 1.0, sampleRate);
    }

    void setHighPass (double hz, double sampleRate) noexcept
    {
        const auto w = 2.0 * 3.14159265358979323846 * hz;
        setAnalog (0.0, 1.0, w, 1.0, sampleRate);
    }

    void reset() noexcept { s = 0.0f; }

    float processSample (float x) noexcept
    {
        const auto y = bz0 * x + s;
        s = bz1 * x - az1 * y;
        return y;
    }

    float bz0 = 1.0f, bz1 = 0.0f, az1 = 0.0f, s = 0.0f;
};

/** RBJ cookbook shelving biquad. */
struct Shelf
{
    void set (bool high, double hz, double gainDb, double sampleRate) noexcept
    {
        const auto A = std::pow (10.0, gainDb / 40.0);
        const auto w0 = 2.0 * 3.14159265358979323846 * hz / sampleRate;
        const auto cw = std::cos (w0), sw = std::sin (w0);
        const auto alpha = sw / 2.0 * std::sqrt (2.0); // shelf slope S = 1
        const auto sq = 2.0 * std::sqrt (A) * alpha;
        const auto sign = high ? 1.0 : -1.0;

        const auto b0 = A * ((A + 1) + sign * (A - 1) * cw + sq);
        const auto b1 = -2.0 * sign * A * ((A - 1) + sign * (A + 1) * cw);
        const auto b2 = A * ((A + 1) + sign * (A - 1) * cw - sq);
        const auto a0 = (A + 1) - sign * (A - 1) * cw + sq;
        const auto a1 = 2.0 * sign * ((A - 1) - sign * (A + 1) * cw);
        const auto a2 = (A + 1) - sign * (A - 1) * cw - sq;

        c0 = (float) (b0 / a0); c1 = (float) (b1 / a0); c2 = (float) (b2 / a0);
        d1 = (float) (a1 / a0); d2 = (float) (a2 / a0);
    }

    void reset() noexcept { z1 = z2 = 0.0f; }

    float processSample (float x) noexcept
    {
        const auto y = c0 * x + z1;
        z1 = c1 * x - d1 * y + z2;
        z2 = c2 * x - d2 * y;
        return y;
    }

    float c0 = 1.0f, c1 = 0.0f, c2 = 0.0f, d1 = 0.0f, d2 = 0.0f, z1 = 0.0f, z2 = 0.0f;
};

} // namespace jmrig
