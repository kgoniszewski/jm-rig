// Headless engine tests: real-time safety and edge cases. No test framework,
// so the runner builds anywhere JUCE does, including CI without a display.

#include <juce_dsp/juce_dsp.h>
#include "dsp/ImpulseDecoder.h"
#include "dsp/RigEngine.h"

#include <atomic>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

//==============================================================================
// Count heap allocations made on the test's own thread, so tests can assert
// that process() makes none. The IR loader thread is allowed to allocate.
namespace
{
    std::atomic<long> allocationCount { 0 };
    thread_local bool countingAllocations = false;
}

void* operator new (std::size_t size)
{
    if (countingAllocations)
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

    /** IRs load on a background thread and are swapped in by process(), so
        feed silence until both slots hold an IR of the expected size. */
    bool waitForImpulses (jmrig::RigEngine& engine, int minimumSize = 2)
    {
        std::vector<float> silence (256, 0.0f);

        for (int attempt = 0; attempt < 400; ++attempt)
        {
            engine.process (silence.data(), (int) silence.size());

            if (engine.getCurrentImpulseSize (jmrig::CabStage::Slot::a) >= minimumSize
                && engine.getCurrentImpulseSize (jmrig::CabStage::Slot::b) >= minimumSize)
            {
                engine.reset();
                return true;
            }

            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        }

        return false;
    }

    float rms (const std::vector<float>& v, size_t from)
    {
        double sum = 0.0;

        for (size_t i = from; i < v.size(); ++i)
            sum += (double) v[i] * v[i];

        return (float) std::sqrt (sum / (double) (v.size() - from));
    }

    //==========================================================================
    void testNoAllocationsInProcess()
    {
        std::puts ("process() is allocation-free");

        for (auto sr : { 44100.0, 48000.0, 96000.0 })
        {
            jmrig::RigEngine engine;
            engine.prepare (sr, 512);
            check (waitForImpulses (engine), "default cab IRs load");

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
        check (waitForImpulses (a) && waitForImpulses (b), "default cab IRs load");

        auto x = reference, y = reference;
        run (a, x, 512);
        run (b, y, 37);

        float maxDiff = 0.0f;

        for (size_t i = 0; i < x.size(); ++i)
            maxDiff = std::max (maxDiff, std::abs (x[i] - y[i]));

        check (maxDiff < 1.0e-5f, ("512 vs 37-sample blocks match (max diff " + juce::String (maxDiff) + ")").toRawUTF8());
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
        p.cabOn = false;     // the cab's phase response would bias the measurement
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

    //==========================================================================
    void testCabIdentityWithUnitImpulse()
    {
        std::puts ("cab: unit-impulse IR is transparent");

        jmrig::CabStage cab;
        cab.prepare (48000.0, 512);

        juce::AudioBuffer<float> unit (1, 1);
        unit.setSample (0, 0, 1.0f);
        cab.loadImpulseResponse (jmrig::CabStage::Slot::a, unit, 48000.0, false);
        cab.loadImpulseResponse (jmrig::CabStage::Slot::b, unit, 48000.0, false);

        std::vector<float> silence (512, 0.0f);

        for (int i = 0; i < 400 && cab.getCurrentImpulseSize (jmrig::CabStage::Slot::a) != 1; ++i)
        {
            cab.process (silence.data(), 512);
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        }

        // Convolution crossfades a new IR in; let it finish.
        for (int i = 0; i < 40; ++i)
            cab.process (silence.data(), 512);

        check (cab.getCurrentImpulseSize (jmrig::CabStage::Slot::a) == 1, "unit IR loaded");

        jmrig::RigParameters p;
        p.cabLowCutHz = 10.0f;
        p.cabHighCutHz = 20000.0f;
        cab.setParameters (p);
        cab.reset();

        const auto input = sine (48000.0, 1000.0, 9600, 0.5f);
        auto output = input;

        for (size_t offset = 0; offset < output.size(); offset += 512)
            cab.process (output.data() + offset, (int) std::min<size_t> (512, output.size() - offset));

        const auto gainDb = juce::Decibels::gainToDecibels (rms (output, 4800) / rms (input, 4800));
        check (std::abs (gainDb) < 0.1f, ("1 kHz passes at " + juce::String (gainDb, 3) + " dB").toRawUTF8());
    }

    void testCabLowCut()
    {
        std::puts ("cab: low cut removes low end");

        jmrig::RigEngine engine;
        jmrig::RigParameters p;
        p.ampGain = 0.0f;
        p.ampMaster = 10.0f;
        engine.setParameters (p);
        engine.prepare (48000.0, 512);
        check (waitForImpulses (engine), "default cab IRs load");

        auto measure = [&] (float lowCutHz)
        {
            p.cabLowCutHz = lowCutHz;
            engine.setParameters (p);
            engine.reset();

            auto signal = sine (48000.0, 60.0, 24000, 0.05f);
            run (engine, signal, 512);
            return rms (signal, 12000);
        };

        const auto open = measure (20.0f);
        const auto cut  = measure (500.0f);
        const auto dB = juce::Decibels::gainToDecibels (cut / open);
        check (dB < -20.0f, ("60 Hz with a 500 Hz low cut: " + juce::String (dB, 1) + " dB").toRawUTF8());
    }

    void testIrSwapWhileProcessing()
    {
        std::puts ("cab: swapping an IR mid-stream is allocation-free and click-free");

        jmrig::RigEngine engine;
        engine.prepare (48000.0, 256);
        check (waitForImpulses (engine), "default cab IRs load");

        auto signal = sine (48000.0, 220.0, 48000, 0.3f);
        long allocations = 0;
        bool swapped = false;

        for (size_t offset = 0; offset < signal.size(); offset += 256)
        {
            if (offset == 256 * 20)
            {
                // A user IR arrives from the message thread while audio runs.
                std::thread loader ([&engine]
                {
                    auto ir = jmrig::CabStage::makeDefaultImpulseResponse (44100.0);
                    ir.applyGain (0.5f);
                    engine.loadImpulseResponse (jmrig::CabStage::Slot::a, std::move (ir), 44100.0);
                });
                loader.join();
                swapped = true;
            }

            const auto n = (int) std::min<size_t> (256, signal.size() - offset);
            AllocationGuard guard;
            engine.process (signal.data() + offset, n);
            allocations += guard.count();
        }

        float maxStep = 0.0f;

        for (size_t i = 24000; i < signal.size(); ++i)
            maxStep = std::max (maxStep, std::abs (signal[i] - signal[i - 1]));

        check (swapped && allocations == 0, "no allocations on the audio thread during the swap");
        check (allFinite (signal) && maxStep < 0.2f, ("no discontinuity (max step " + juce::String (maxStep, 3) + ")").toRawUTF8());
    }

    //==========================================================================
    void testDecoderRejectsBadFiles()
    {
        std::puts ("IR decoder rejects files it cannot use");

        jmrig::ImpulseDecoder decoder;
        check (! decoder.decode ({}).ok(), "empty file");

        juce::MemoryBlock garbage (4096);
        garbage.fillWith (0x5a);
        check (! decoder.decode (garbage).ok(), "not an audio file");
    }

    /** Runs every .wav in $JMRIG_IR_DIR through the real decode + cab path.
        The user's IRs are not in the repo, so CI skips this. */
    void testUserImpulseResponses()
    {
        const auto dir = juce::SystemStats::getEnvironmentVariable ("JMRIG_IR_DIR", {});

        if (dir.isEmpty())
        {
            std::puts ("user IRs: skipped (set JMRIG_IR_DIR to run)");
            return;
        }

        std::puts ("user IRs: decode, load and play each file");

        jmrig::ImpulseDecoder decoder;
        const auto files = juce::File (dir).findChildFiles (juce::File::findFiles, false, "*.wav");
        check (! files.isEmpty(), "found IR files");

        for (const auto& file : files)
        {
            juce::MemoryBlock data;
            file.loadFileAsData (data);
            auto decoded = decoder.decode (data);

            if (! decoded.ok())
            {
                check (false, (file.getFileName() + ": " + decoded.error).toRawUTF8());
                continue;
            }

            jmrig::RigEngine engine;
            jmrig::RigParameters p;
            p.ampGain = 0.0f;
            p.ampMaster = 10.0f;
            engine.setParameters (p);
            engine.prepare (48000.0, 128);
            check (waitForImpulses (engine), "default cab IRs load");
            const auto defaultSize = engine.getCurrentImpulseSize (jmrig::CabStage::Slot::a);

            engine.loadImpulseResponse (jmrig::CabStage::Slot::a, std::move (decoded.impulse), decoded.sampleRate);

            // Wait until slot A holds this IR rather than the default.
            std::vector<float> silence (128, 0.0f);

            for (int i = 0; i < 400 && engine.getCurrentImpulseSize (jmrig::CabStage::Slot::a) == defaultSize; ++i)
            {
                engine.process (silence.data(), 128);
                std::this_thread::sleep_for (std::chrono::milliseconds (5));
            }

            // Let the old-to-new IR crossfade finish.
            for (int i = 0; i < 100; ++i)
                engine.process (silence.data(), 128);

            engine.reset();

            auto signal = sine (48000.0, 1000.0, 24000, 0.05f);
            long allocations = 0;

            for (size_t offset = 0; offset < signal.size(); offset += 128)
            {
                AllocationGuard guard;
                engine.process (signal.data() + offset, (int) std::min<size_t> (128, signal.size() - offset));
                allocations += guard.count();
            }

            const auto levelDb = juce::Decibels::gainToDecibels (rms (signal, 12000) / (0.05f / std::sqrt (2.0f)), -120.0f);
            const auto label = file.getFileNameWithoutExtension() + " (" + juce::String (decoded.sampleRate / 1000.0, 1)
                             + " kHz, IR " + juce::String (engine.getCurrentImpulseSize (jmrig::CabStage::Slot::a))
                             + " samples): 1 kHz at " + juce::String (levelDb, 1) + " dB";

            check (allFinite (signal) && allocations == 0 && levelDb > -40.0f && levelDb < 20.0f, label.toRawUTF8());
        }
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
    testCabIdentityWithUnitImpulse();
    testCabLowCut();
    testIrSwapWhileProcessing();
    testDecoderRejectsBadFiles();
    testUserImpulseResponses();

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
