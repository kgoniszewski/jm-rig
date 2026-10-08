// Headless engine tests: real-time safety and edge cases. No test framework,
// so the runner builds anywhere JUCE does, including CI without a display.

#include <juce_dsp/juce_dsp.h>
#include "dsp/RigEngine.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

//==============================================================================
// Count every heap allocation, so tests can assert that process() makes none.
namespace
{
    std::atomic<long> allocationCount { 0 };
    std::atomic<bool> countingAllocations { false };
}

void* operator new (std::size_t size)
{
    if (countingAllocations.load (std::memory_order_relaxed))
        allocationCount.fetch_add (1, std::memory_order_relaxed);

    if (auto* p = std::malloc (size == 0 ? 1 : size))
        return p;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size) { return operator new (size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
    struct AllocationGuard
    {
        AllocationGuard()  { allocationCount = 0; countingAllocations = true; }
        ~AllocationGuard() { countingAllocations = false; }
        long count() const { return allocationCount.load(); }
    };

    int failures = 0;

    void check (bool condition, const char* what)
    {
        std::printf ("  [%s] %s\n", condition ? "pass" : "FAIL", what);

        if (! condition)
            ++failures;
    }

    bool allFinite (const std::vector<float>& v)
    {
        for (auto x : v)
            if (! std::isfinite (x))
                return false;

        return true;
    }

    float peak (const std::vector<float>& v, size_t from = 0)
    {
        float p = 0.0f;

        for (size_t i = from; i < v.size(); ++i)
            p = std::max (p, std::abs (v[i]));

        return p;
    }

    std::vector<float> sine (double sampleRate, double hz, int numSamples, float amplitude)
    {
        std::vector<float> v ((size_t) numSamples);

        for (int i = 0; i < numSamples; ++i)
            v[(size_t) i] = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sampleRate);

        return v;
    }

    /** Runs a whole signal through the engine in blocks of the given size. */
    void run (jmrig::RigEngine& engine, std::vector<float>& signal, int blockSize)
    {
        for (size_t offset = 0; offset < signal.size(); offset += (size_t) blockSize)
        {
            const auto n = (int) std::min ((size_t) blockSize, signal.size() - offset);
            engine.process (signal.data() + offset, n);
        }
    }

    //==========================================================================
    void testNoAllocationsInProcess()
    {
        std::puts ("process() is allocation-free");

        for (auto sr : { 44100.0, 48000.0, 96000.0 })
        {
            jmrig::RigEngine engine;
            engine.prepare (sr, 512);

            auto signal = sine (sr, 110.0, 8192, 0.5f);
            jmrig::RigParameters p;

            AllocationGuard guard;

            for (int i = 0; i < 16; ++i)
            {
                p.ampGain = (float) (i % 11);
                p.bypass = (i % 5) == 0;
                engine.setParameters (p);
                engine.process (signal.data() + i * 512, 512);
            }

            engine.reset();
            const auto allocations = guard.count();
            check (allocations == 0, juce::String ("no allocations at " + juce::String (sr) + " Hz").toRawUTF8());
        }
    }

    void testBlockSizeEdgeCases()
    {
        std::puts ("block size edge cases");

        jmrig::RigEngine engine;
        engine.prepare (48000.0, 256);

        std::vector<float> empty;
        engine.process (empty.data(), 0);
        check (true, "zero-length block does not crash");

        for (auto blockSize : { 1, 7, 255, 256, 1000, 4096 })
        {
            auto signal = sine (48000.0, 220.0, 9000, 0.3f);
            AllocationGuard guard;
            run (engine, signal, blockSize);
            const auto allocations = guard.count();

            const auto label = "block size " + juce::String (blockSize) + " stays finite, bounded and allocation-free";
            check (allFinite (signal) && peak (signal) < 4.0f && allocations == 0, label.toRawUTF8());
        }
    }

    void testBlockSizeIndependence()
    {
        std::puts ("output does not depend on how the host splits blocks");

        const auto reference = sine (48000.0, 196.0, 6000, 0.4f);

        jmrig::RigEngine a, b;
        a.prepare (48000.0, 512);
        b.prepare (48000.0, 512);

        auto x = reference, y = reference;
        run (a, x, 512);
        run (b, y, 37);

        float maxDiff = 0.0f;

        for (size_t i = 0; i < x.size(); ++i)
            maxDiff = std::max (maxDiff, std::abs (x[i] - y[i]));

        check (maxDiff < 1.0e-5f, "512 vs 37-sample blocks match");
    }

    void testSilenceAndDenormals()
    {
        std::puts ("silence and denormals");

        jmrig::RigEngine engine;
        engine.prepare (48000.0, 512);

        // A decaying tail into tiny values: output must settle to (near) zero
        // and contain no denormals once ScopedNoDenormals is in force.
        juce::ScopedNoDenormals noDenormals;

        std::vector<float> signal (48000, 0.0f);
        signal[0] = 1.0f;

        for (size_t i = 1; i < 2000; ++i)
            signal[i] = 1.0e-38f;

        run (engine, signal, 512);

        check (allFinite (signal), "impulse plus denormal-range input stays finite");
        check (peak (signal, 40000) < 1.0e-6f, "output decays to silence");
    }

    void testLatencyAndBypass()
    {
        std::puts ("latency reporting and bypass alignment");

        jmrig::RigEngine engine;
        engine.prepare (48000.0, 512);
        const auto latency = engine.getLatencySamples();
        check (latency >= 0 && latency < 64, ("latency is small: " + juce::String (latency) + " samples").toRawUTF8());

        jmrig::RigParameters p;
        p.bypass = true;
        engine.setParameters (p);
        engine.reset(); // snap the bypass crossfade

        std::vector<float> signal (2048, 0.0f);
        signal[100] = 1.0f;
        run (engine, signal, 512);

        const auto expected = (size_t) (100 + latency);
        check (std::abs (signal[expected] - 1.0f) < 1.0e-6f, "bypassed impulse arrives exactly at the reported latency");
    }

    void testWetPathAlignment()
    {
        std::puts ("wet path latency matches the reported value");

        jmrig::RigEngine engine;
        jmrig::RigParameters p;
        p.ampGain = 0.0f;    // nearly linear region of the placeholder curve
        p.ampMaster = 10.0f;
        engine.setParameters (p);
        engine.prepare (48000.0, 512);

        // A low-level sine passes through almost unchanged apart from the
        // delay, so cross-correlating against the input finds the wet latency.
        // 1 kHz keeps the DC blockers' phase lead well under a sample.
        const auto input = sine (48000.0, 1000.0, 8192, 0.05f);
        auto output = input;
        run (engine, output, 512);

        int bestLag = 0;
        double best = -1.0e9;

        for (int lag = 0; lag < 24; ++lag) // under half the 48-sample period
        {
            double sum = 0.0;

            for (size_t i = 4096; i < 8000; ++i)
                sum += (double) output[i] * input[i - (size_t) lag];

            if (sum > best)
            {
                best = sum;
                bestLag = lag;
            }
        }

        const auto reported = engine.getLatencySamples();
        check (std::abs (bestLag - reported) <= 1,
               ("measured wet latency " + juce::String (bestLag) + " vs reported " + juce::String (reported)).toRawUTF8());
    }
}

int main()
{
    testNoAllocationsInProcess();
    testBlockSizeEdgeCases();
    testBlockSizeIndependence();
    testSilenceAndDenormals();
    testLatencyAndBypass();
    testWetPathAlignment();

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
