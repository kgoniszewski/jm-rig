// Null tests: the amp's models against SPICE simulations of the same circuit.
// The reference data comes from tools/spice/generate.py (ngspice), committed
// under tests/data/spice so these run without ngspice installed.

#include "dsp/amp/Preamp.h"
#include "dsp/amp/ToneStack.h"
#include "dsp/amp/TriodeStage.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#ifndef JMRIG_SPICE_DATA_DIR
 #error "JMRIG_SPICE_DATA_DIR must point at tests/data/spice"
#endif

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double rate = 192000.0;   // the amp's oversampled rate at 48 kHz

    int failures = 0;

    void check (bool condition, const char* what)
    {
        std::printf ("  [%s] %s\n", condition ? "pass" : "FAIL", what);

        if (! condition)
            ++failures;
    }

    /** Numeric CSV rows, skipping '#' comments and the header line. */
    std::vector<std::vector<double>> readCsv (const std::string& name, std::string* comments = nullptr)
    {
        std::ifstream in (std::string (JMRIG_SPICE_DATA_DIR) + "/" + name);
        std::vector<std::vector<double>> rows;
        std::string line;
        bool header = true;

        while (std::getline (in, line))
        {
            if (line.empty())
                continue;

            if (line[0] == '#')
            {
                if (comments != nullptr)
                    *comments += line + "\n";
                continue;
            }

            if (header)
            {
                header = false;
                continue;
            }

            std::vector<double> row;
            std::stringstream ss (line);
            std::string cell;

            while (std::getline (ss, cell, ','))
                row.push_back (std::stod (cell));

            rows.push_back (row);
        }

        if (rows.empty())
            std::printf ("  could not read %s\n", name.c_str());

        return rows;
    }

    double audioTaper (double knob)
    {
        const auto x = std::clamp (knob, 0.0, 10.0) / 10.0;
        return (std::pow (10.0, 2.0 * x) - 1.0) / 99.0;
    }

    void setKnobs (jmrig::Preamp& p, double gain, double treble, double bass, double mid, double bright)
    {
        p.setControls (audioTaper (gain), audioTaper (treble), audioTaper (bass),
                       std::clamp (mid, 0.0, 10.0) / 10.0, bright);
    }

    /** Complex amplitude of one frequency over a whole number of cycles. */
    std::complex<double> dft (const std::vector<double>& x, size_t from, size_t count, double hz)
    {
        std::complex<double> acc;

        for (size_t i = 0; i < count; ++i)
        {
            const auto n = (double) (from + i);
            acc += x[from + i] * std::polar (1.0, -2.0 * pi * hz * n / rate);
        }

        return acc * (2.0 / (double) count);
    }

    //==========================================================================
    void testTriode()
    {
        std::puts ("triode stage vs SPICE (Koren 12AX7, DC transfer)");

        std::string comments;
        const auto rows = readCsv ("triode_dc.csv", &comments);
        double spiceVp = 0.0, spiceVk = 0.0;
        std::sscanf (comments.c_str() + comments.find ("vp="), "vp=%lf vk=%lf", &spiceVp, &spiceVk);

        jmrig::TriodeStage stage;
        const jmrig::TriodeStage::Design design;
        stage.design (design, jmrig::TriodeStage::Tube {});

        std::printf ("  quiescent plate %.3f V (SPICE %.3f), cathode %.4f V (SPICE %.4f)\n",
                     stage.getQuiescentPlateVolts(), spiceVp, stage.getQuiescentCathodeVolts(), spiceVk);
        check (std::abs (stage.getQuiescentPlateVolts() - spiceVp) < 0.005, "quiescent plate within 5 mV");
        check (std::abs (stage.getQuiescentCathodeVolts() - spiceVk) < 0.001, "quiescent cathode within 1 mV");

        // vin is the grid voltage; the cathode sits at its quiescent value.
        double worst = 0.0;

        for (const auto& r : rows)
        {
            const auto ours = stage.processSample ((float) r[0]);
            worst = std::max (worst, std::abs ((double) ours - r[1]));
        }

        std::printf ("  plate swing, -8..+1 V grid: worst error %.4f V of up to %.1f V\n", worst, rows.front()[1]);
        check (worst < 0.01, "transfer curve within 10 mV");
    }

    void testToneStack()
    {
        std::puts ("tone stack vs SPICE AC (ideal source, 1M load)");

        const auto rows = readCsv ("tonestack_ac.csv");
        double worstDb = 0.0, worstPhase = 0.0;
        int compared = 0;

        for (const auto& r : rows)
        {
            if (r[4] < -60.0) // all knobs at zero: the stack is nearly a short, below any useful level
                continue;

            const auto h = jmrig::ToneStack::analogCoefficients (jmrig::ToneStack::Components {}, r[0], r[1], r[2]);
            const std::complex<double> s (0.0, 2.0 * pi * r[3]);
            std::complex<double> num, den, sk (1.0);

            for (size_t k = 0; k < 4; ++k, sk *= s)
            {
                num += h.b[k] * sk;
                den += h.a[k] * sk;
            }

            const auto ours = num / den;
            worstDb = std::max (worstDb, std::abs (20.0 * std::log10 (std::abs (ours)) - r[4]));
            worstPhase = std::max (worstPhase, std::abs (std::remainder (std::arg (ours) - r[5], 2.0 * pi)));
            ++compared;
        }

        std::printf ("  %d points: worst %.4f dB, %.5f rad\n", compared, worstDb, worstPhase);
        check (compared > 250, "compared the whole sweep");
        check (worstDb < 0.01 && worstPhase < 0.001, "matches within 0.01 dB and 0.001 rad");
    }

    /** Small-signal gain of the whole preamp at one frequency, in dB. */
    double preampGainDb (double gain, double treble, double bass, double mid, double bright, double hz)
    {
        jmrig::Preamp p;
        p.prepare (rate);
        setKnobs (p, gain, treble, bass, mid, bright);
        p.reset();

        const auto amplitude = 1.0e-3;
        const auto settle = (size_t) rate / 2;
        const auto count = (size_t) std::lround (rate / hz * std::ceil (hz / 20.0)); // whole cycles, >= 50 ms
        std::vector<double> in (settle + count), out (in.size());

        for (size_t n = 0; n < in.size(); ++n)
        {
            in[n] = amplitude * std::sin (2.0 * pi * hz * (double) n / rate);
            out[n] = p.processSample ((float) in[n]);
        }

        return 20.0 * std::log10 (std::abs (dft (out, settle, count, hz)) / amplitude);
    }

    void testPreampSmallSignal (double toleranceDb)
    {
        std::puts ("preamp small-signal response vs SPICE (jack to V1B plate)");

        const auto rows = readCsv ("preamp_ac.csv");
        double worst = 0.0;
        std::tuple<double, double, double, double, double> current { -1, -1, -1, -1, -1 };

        for (const auto& r : rows)
        {
            const std::tuple<double, double, double, double, double> setting { r[0], r[1], r[2], r[3], r[4] };

            if (setting != current)
            {
                current = setting;
                std::printf ("  gain %g treble %g bass %g mid %g bright %g:", r[0], r[1], r[2], r[3], r[4]);
            }

            const auto ours = preampGainDb (r[0], r[1], r[2], r[3], r[4], r[5]);
            const auto err = ours - r[6];
            worst = std::max (worst, std::abs (err));
            std::printf (" %+.1f", err);

            if (r[5] > 6000.0)
                std::printf (" dB (at 100 Hz..6.4 kHz; SPICE %.1f dB at 1.6 kHz)\n", r[6 - 0]);
        }

        std::printf ("  worst error %.2f dB\n", worst);
        char what[96];
        std::snprintf (what, sizeof (what), "within %.1f dB at every setting and frequency", toleranceDb);
        check (worst < toleranceDb, what);
    }

    /** Null depth limits: -20 dB up to 1 V peak at the jack (a hot humbucker
        dug in hard), -12 dB beyond, where V1A's own clipping against its
        real (tone stack) load line, which the model treats as a Thevenin
        source, starts to show. Harmonics within 40 dB of the fundamental,
        the ones that shape the tone, must land within 7 dB. */
    void testPreampLargeSignal()
    {
        std::puts ("preamp large-signal null vs SPICE (200 Hz, tone at noon)");

        const auto rows = readCsv ("preamp_tran.csv");
        std::map<std::pair<double, double>, std::vector<std::pair<size_t, double>>> cases;

        for (const auto& r : rows)
            cases[{ r[0], r[1] }].push_back ({ (size_t) r[2], r[3] });

        double worstNull = -200.0, worstHarmonic = 0.0;
        bool nullsOk = true;

        for (const auto& [key, ref] : cases)
        {
            const auto [amplitude, gainKnob] = key;
            jmrig::Preamp p;
            p.prepare (rate);
            setKnobs (p, gainKnob, 5, 5, 5, 0);
            p.reset();

            const auto last = ref.back().first;
            std::vector<double> ours (last + 1), spice (last + 1);

            for (size_t n = 0; n <= last; ++n)
                ours[n] = p.processSample ((float) (amplitude * std::sin (2.0 * pi * 200.0 * (double) n / rate)));

            const auto from = ref.front().first, count = ref.size();
            double diff = 0.0, energy = 0.0;

            for (const auto& [n, v] : ref)
            {
                spice[n] = v;
                diff += (ours[n] - v) * (ours[n] - v);
                energy += v * v;
            }

            const auto nullDb = 10.0 * std::log10 (diff / energy);
            worstNull = std::max (worstNull, nullDb);
            nullsOk = nullsOk && nullDb < (amplitude <= 1.0 ? -20.0 : -12.0);

            std::printf ("  %.1f V in, gain %g: null %.1f dB; harmonics (ours/SPICE, dBV):", amplitude, gainKnob, nullDb);

            const auto fundamental = std::abs (dft (spice, from, count, 200.0));

            for (int h = 1; h <= 5; ++h)
            {
                const auto a = std::abs (dft (ours, from, count, 200.0 * h));
                const auto b = std::abs (dft (spice, from, count, 200.0 * h));
                const auto adb = 20.0 * std::log10 (a + 1e-9), bdb = 20.0 * std::log10 (b + 1e-9);
                std::printf (" %.1f/%.1f", adb, bdb);

                if (b > fundamental * 0.01)
                    worstHarmonic = std::max (worstHarmonic, std::abs (adb - bdb));
            }

            std::puts ("");
        }

        std::printf ("  worst null %.1f dB, worst harmonic error %.1f dB\n", worstNull, worstHarmonic);
        check (nullsOk, "nulls below -20 dB up to 1 V in, -12 dB beyond");
        check (worstHarmonic < 7.0, "harmonics within 40 dB of the fundamental match within 7 dB");
    }
}

int main()
{
    testTriode();
    testToneStack();
    testPreampSmallSignal (0.5);
    testPreampLargeSignal();

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
