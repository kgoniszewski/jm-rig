// Headless engine tests: real-time safety and edge cases. No test framework,
// so the runner builds anywhere JUCE does, including CI without a display.

#include <juce_dsp/juce_dsp.h>
#include "dsp/ImpulseDecoder.h"
#include "dsp/RigEngine.h"

#include <atomic>
#include <complex>
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
                p.ampTreble = (float) ((i * 3) % 11);
                p.ampBass = (float) ((i * 7) % 11);
                p.ampMid = (float) ((i * 5) % 11);
                p.ampPresence = (float) ((i * 2) % 11);
                p.ampBright = (i % 3) == 0;
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

    void testAmpLatencyMatchesOversampler()
    {
        std::puts ("amp latency matches the oversampler round trip");

        jmrig::AmpStage amp;
        amp.prepare (48000.0, 512);

        // Run a low-level sine through the same oversampler with nothing in
        // between, then find the delay by cross-correlation.
        auto os = jmrig::AmpStage::makeOversampler();
        os->initProcessing (512);
        os->reset();

        const auto input = sine (48000.0, 1000.0, 8192, 0.05f);
        auto output = input;

        for (size_t offset = 0; offset < output.size(); offset += 512)
        {
            float* ch[] = { output.data() + offset };
            juce::dsp::AudioBlock<float> block (ch, 1, 512);
            os->processSamplesUp (block);
            os->processSamplesDown (block);
        }

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

        const auto reported = amp.getLatencySamples();
        check (std::abs (bestLag - reported) <= 1,
               ("measured latency " + juce::String (bestLag) + " vs reported " + juce::String (reported)).toRawUTF8());
    }

    //==========================================================================
    /** Independent check of the tone stack's closed-form coefficients: solve
        the same netlist numerically (complex nodal analysis, Gaussian
        elimination) and compare |H| and phase. */
    std::complex<double> solveToneStackNodal (const jmrig::ToneStack::Components& c,
                                              double t, double l, double m, double hz)
    {
        using cd = std::complex<double>;
        const cd s (0.0, 2.0 * juce::MathConstants<double>::pi * hz);
        const auto g = [] (double r) { return cd (1.0 / std::max (r, 1.0e-3), 0.0); };

        enum { T, O, X, S, M, N };
        cd Y[N][N] {};
        cd I[N] {};

        const auto between = [&] (int a, int b, cd y)
        {
            Y[a][a] += y; Y[b][b] += y; Y[a][b] -= y; Y[b][a] -= y;
        };
        const auto toGround = [&] (int a, cd y) { Y[a][a] += y; };
        const auto fromInput = [&] (int a, cd y) { Y[a][a] += y; I[a] += y; }; // Vin = 1

        fromInput (T, s * c.trebleCap);
        fromInput (S, g (c.slope));
        between (T, O, g ((1.0 - t) * c.treblePot));
        between (O, X, g (t * c.treblePot));
        toGround (O, g (c.load));
        between (S, X, s * c.bassCap);
        between (S, M, s * c.midCap);
        between (X, M, g (l * c.bassPot));
        toGround (M, g (m * c.midPot));

        for (int col = 0; col < N; ++col)
        {
            int pivot = col;

            for (int r = col + 1; r < N; ++r)
                if (std::abs (Y[r][col]) > std::abs (Y[pivot][col]))
                    pivot = r;

            std::swap (Y[col], Y[pivot]);
            std::swap (I[col], I[pivot]);

            for (int r = col + 1; r < N; ++r)
            {
                const auto f = Y[r][col] / Y[col][col];

                for (int k = col; k < N; ++k)
                    Y[r][k] -= f * Y[col][k];

                I[r] -= f * I[col];
            }
        }

        cd v[N];

        for (int r = N - 1; r >= 0; --r)
        {
            auto sum = I[r];

            for (int k = r + 1; k < N; ++k)
                sum -= Y[r][k] * v[k];

            v[r] = sum / Y[r][r];
        }

        return v[O];
    }

    void testToneStackMatchesCircuit()
    {
        std::puts ("tone stack: closed form matches a numeric circuit solve");

        const jmrig::ToneStack::Components parts;
        double worstDb = 0.0, worstPhase = 0.0;

        for (auto t : { 0.05, 0.5, 0.95 })
            for (auto l : { 0.05, 0.5, 0.95 })
                for (auto m : { 0.05, 0.5, 0.95 })
                {
                    const auto h = jmrig::ToneStack::analogCoefficients (parts, t, l, m);

                    for (auto hz : { 40.0, 150.0, 500.0, 1500.0, 5000.0, 15000.0 })
                    {
                        const std::complex<double> s (0.0, 2.0 * juce::MathConstants<double>::pi * hz);
                        std::complex<double> num = 0.0, den = 0.0, sk = 1.0;

                        for (size_t k = 0; k < 4; ++k, sk *= s)
                        {
                            num += h.b[k] * sk;
                            den += h.a[k] * sk;
                        }

                        const auto closed = num / den;
                        const auto nodal = solveToneStackNodal (parts, t, l, m, hz);

                        worstDb = std::max (worstDb, std::abs (20.0 * std::log10 (std::abs (closed) / std::abs (nodal))));
                        worstPhase = std::max (worstPhase, std::abs (std::arg (closed / nodal)));
                    }
                }

        check (worstDb < 1.0e-6 && worstPhase < 1.0e-6,
               ("27 settings x 6 frequencies, worst error " + juce::String (worstDb, 9) + " dB").toRawUTF8());

        // The digital filter at 4x oversampling follows the analog response.
        jmrig::ToneStack stack;
        stack.prepare (192000.0, parts);
        stack.setControls (0.3, 0.6, 0.4);
        const auto h = jmrig::ToneStack::analogCoefficients (parts, 0.3, 0.6, 0.4);

        for (auto hz : { 100.0, 1000.0, 5000.0 })
        {
            stack.reset();
            double peakOut = 0.0;

            for (int i = 0; i < 192000; ++i)
            {
                const auto y = stack.processSample ((float) std::sin (juce::MathConstants<double>::twoPi * hz * i / 192000.0));

                if (i > 96000)
                    peakOut = std::max (peakOut, (double) std::abs (y));
            }

            const std::complex<double> s (0.0, juce::MathConstants<double>::twoPi * hz);
            const auto analog = std::abs ((h.b[1] * s + h.b[2] * s * s + h.b[3] * s * s * s)
                                          / (h.a[0] + h.a[1] * s + h.a[2] * s * s + h.a[3] * s * s * s));
            const auto errDb = 20.0 * std::log10 (peakOut / analog);
            check (std::abs (errDb) < 0.05, ("digital vs analog at " + juce::String (hz, 0) + " Hz: "
                                             + juce::String (errDb, 3) + " dB").toRawUTF8());
        }
    }

    void testTriodeOperatingPoint()
    {
        std::puts ("12AX7 stage: operating point and gain");

        jmrig::TriodeStage v;
        v.design ({}, {});

        const auto vp = v.getQuiescentPlateVolts(), vk = v.getQuiescentCathodeVolts();
        const auto gain = v.getSmallSignalGain();
        check (vp > 130.0 && vp < 220.0 && vk > 0.7 && vk < 2.2,
               ("Vp " + juce::String (vp, 1) + " V, Vk " + juce::String (vk, 2) + " V").toRawUTF8());
        check (gain > 45.0f && gain < 80.0f, ("small-signal gain " + juce::String (gain, 1)).toRawUTF8());

        bool monotonic = true;

        for (float x = -15.0f; x < 15.0f; x += 0.01f)
            monotonic = monotonic && v.processSample (x + 0.01f) <= v.processSample (x);

        check (monotonic, "plate voltage falls as the grid rises (inverting, no foldback)");
        check (std::abs (v.processSample (0.0f)) < 1.0e-3f, "no offset at rest");
    }

    /** Harmonic distortion of a steady sine, from a DFT at each harmonic. */
    double thd (const std::vector<float>& v, size_t from, double hz, double sampleRate)
    {
        const auto power = [&] (double f)
        {
            double re = 0.0, im = 0.0;

            for (size_t i = from; i < v.size(); ++i)
            {
                const auto ph = juce::MathConstants<double>::twoPi * f * (double) i / sampleRate;
                re += v[i] * std::cos (ph);
                im += v[i] * std::sin (ph);
            }

            return re * re + im * im;
        };

        double harmonics = 0.0;

        for (int k = 2; k <= 9; ++k)
            harmonics += power (hz * k);

        return std::sqrt (harmonics / power (hz));
    }

    /** Amp only (cab off), steady sine at the given knob settings. */
    std::vector<float> playAmp (jmrig::RigParameters p, double hz, float amplitude)
    {
        p.cabOn = false;
        jmrig::RigEngine engine;
        engine.setParameters (p);
        engine.prepare (48000.0, 512);

        auto signal = sine (48000.0, hz, 48000, amplitude);
        run (engine, signal, 512);
        return signal;
    }

    void testBreakupFollowsGain()
    {
        std::puts ("amp: clean at low gain, breaks up as Gain rises");

        std::vector<double> distortion;

        for (auto g : { 2.0f, 5.0f, 8.0f, 10.0f })
        {
            jmrig::RigParameters p;
            p.ampGain = g;
            const auto out = playAmp (p, 220.0, 0.3f); // ~0.3 V, a typical single-coil peak
            distortion.push_back (thd (out, 24000, 220.0, 48000.0));
            std::printf ("    gain %.0f: THD %.2f %%, level %.1f dBFS\n", g, distortion.back() * 100.0,
                         juce::Decibels::gainToDecibels (rms (out, 24000)));
        }

        check (distortion[0] < 0.02, "gain 2 is clean (THD under 2 %)");
        check (distortion[3] > 0.08, "gain 10 breaks up (THD over 8 %)");
        check (distortion[0] < distortion[1] && distortion[1] < distortion[2] && distortion[2] < distortion[3],
               "distortion rises with every step of Gain");
    }

    float levelDb (jmrig::RigParameters p, double hz)
    {
        return juce::Decibels::gainToDecibels (rms (playAmp (p, hz, 0.05f), 24000));
    }

    void testToneControlsAndBright()
    {
        std::puts ("amp: tone controls and bright switch");

        jmrig::RigParameters p;
        p.ampGain = 3.0f;

        auto lowTreble = p, highTreble = p;
        lowTreble.ampTreble = 0.0f;
        highTreble.ampTreble = 10.0f;
        const auto trebleRange = levelDb (highTreble, 3000.0) - levelDb (lowTreble, 3000.0);
        check (trebleRange > 6.0f, ("Treble 0 to 10 moves 3 kHz by " + juce::String (trebleRange, 1) + " dB").toRawUTF8());

        auto lowBass = p, highBass = p;
        lowBass.ampBass = 0.0f;
        highBass.ampBass = 10.0f;
        const auto bassRange = levelDb (highBass, 100.0) - levelDb (lowBass, 100.0);
        check (bassRange > 6.0f, ("Bass 0 to 10 moves 100 Hz by " + juce::String (bassRange, 1) + " dB").toRawUTF8());

        auto lowMid = p, highMid = p;
        lowMid.ampMid = 0.0f;
        highMid.ampMid = 10.0f;
        const auto midRange = levelDb (highMid, 600.0) - levelDb (lowMid, 600.0);
        check (midRange > 3.0f, ("Mid 0 to 10 moves 600 Hz by " + juce::String (midRange, 1) + " dB").toRawUTF8());

        auto brightOn = p;
        brightOn.ampBright = true;
        const auto tilt = [&] (const jmrig::RigParameters& q) { return levelDb (q, 4000.0) - levelDb (q, 200.0); };
        const auto brightLift = tilt (brightOn) - tilt (p);
        check (brightLift > 3.0f, ("Bright at Gain 3 lifts 4 kHz vs 200 Hz by " + juce::String (brightLift, 1) + " dB").toRawUTF8());

        auto fullGain = p, fullGainBright = p;
        fullGain.ampGain = fullGainBright.ampGain = 10.0f;
        fullGainBright.ampBright = true;
        const auto brightAtFull = tilt (fullGainBright) - tilt (fullGain);
        check (std::abs (brightAtFull) < 1.0f, ("Bright fades out at Gain 10 (" + juce::String (brightAtFull, 2) + " dB)").toRawUTF8());
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
        p.ampGain = 3.0f; // clean, so the low cut is measured on a linear path
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

        // A click is a jump comparable to the signal's own peak; a 220 Hz
        // tone and its harmonics move far less than that between samples.
        float maxStep = 0.0f;
        const auto signalPeak = peak (signal, 24000);

        for (size_t i = 24000; i < signal.size(); ++i)
            maxStep = std::max (maxStep, std::abs (signal[i] - signal[i - 1]) / signalPeak);

        check (swapped && allocations == 0, "no allocations on the audio thread during the swap");
        check (allFinite (signal) && maxStep < 0.5f, ("no discontinuity (max step " + juce::String (maxStep, 3) + " of peak)").toRawUTF8());
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
            p.ampGain = 3.0f;
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

            check (allFinite (signal) && allocations == 0 && levelDb > -60.0f && levelDb < 20.0f, label.toRawUTF8());
        }
    }

    void testCpuBudget()
    {
        std::puts ("CPU: whole rig, 48 kHz, 64-sample blocks");

        jmrig::RigEngine engine;
        jmrig::RigParameters p;
        p.ampGain = 8.0f;
        engine.setParameters (p);
        engine.prepare (48000.0, 64);
        waitForImpulses (engine);

        auto signal = sine (48000.0, 196.0, 48000 * 10, 0.3f);
        const auto start = juce::Time::getMillisecondCounterHiRes();
        run (engine, signal, 64);
        const auto seconds = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
        const auto realtime = 10.0 / seconds;

        // Generous floor: CI runners are slow and shared. The figure printed
        // is what to watch.
        check (realtime > 10.0, ("runs at " + juce::String (juce::roundToInt (realtime)) + "x real time ("
                                 + juce::String (100.0 / realtime, 2) + " % of one core)").toRawUTF8());
    }
}

int main()
{
    testNoAllocationsInProcess();
    testBlockSizeEdgeCases();
    testBlockSizeIndependence();
    testSilenceAndDenormals();
    testLatencyAndBypass();
    testAmpLatencyMatchesOversampler();
    testToneStackMatchesCircuit();
    testTriodeOperatingPoint();
    testBreakupFollowsGain();
    testToneControlsAndBright();
    testCabIdentityWithUnitImpulse();
    testCabLowCut();
    testIrSwapWhileProcessing();
    testDecoderRejectsBadFiles();
    testUserImpulseResponses();
    testCpuBudget();

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
