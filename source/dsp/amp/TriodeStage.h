#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace jmrig
{

/** One common-cathode 12AX7 gain stage with a fully bypassed cathode
    resistor, solved from its circuit:

        B+ --- Rp ---+--- plate (output)
                     |
             grid ---|< 12AX7
                     |
                     +--- Rk || Ck --- ground

    The plate current comes from Koren's triode equations. design() finds the
    quiescent point, then solves the load line Vpk = (B+ - Vk) - Rp * Ip(Vpk, Vgk)
    for a dense grid of input voltages and stores the plate swing in a table, so
    the audio thread does one interpolated lookup per sample.

    Grid conduction: once the grid rises above the cathode it draws current
    (Koren's model: a diode behind 2k). That current flows back through
    whatever drives the grid, which holds the grid down and flattens the
    positive peaks. solveGrid() finds the grid voltage given the driving
    circuit's open-circuit voltage and source resistance, from a second table.

    Input is in volts at the grid; output is the change in plate voltage from
    its quiescent value, in volts (inverted, as a real stage is). */
class TriodeStage
{
public:
    struct Design
    {
        double supplyVolts = 250.0;    // B+ at the plate resistor
        double plateOhms   = 100.0e3;  // Rp
        double cathodeOhms = 1.5e3;    // Rk (bypassed for audio)
    };

    /** Koren 12AX7 constants, grid conduction and interelectrode capacitances. */
    struct Tube
    {
        double mu = 100.0, ex = 1.4, kg1 = 1060.0, kp = 600.0, kvb = 300.0;
        double gridOhms = 2000.0, gridDiodeAmps = 1.0e-9;   // Koren's RGI and diode IS
        double gridCathodeFarads = 2.3e-12, gridPlateFarads = 2.4e-12;
    };

    /** Allocates; call from prepare only. */
    void design (const Design&, const Tube&);

    float processSample (float gridVolts) const noexcept
    {
        return lookup (plateTable, (gridVolts - inputMin) * plateScale);
    }

    /** The grid voltage when a source of openVolts behind sourceOhms drives
        the grid, with grid current flowing once the grid passes the cathode. */
    float solveGrid (float openVolts, float sourceOhms) const noexcept
    {
        const auto vk = (float) quiescentCathode;

        if (openVolts <= vk)
            return openVolts;

        // f(vg) = vg - open + R * Ig(vg - vk) is increasing, negative at vk and
        // positive at open. Newton steps, kept inside the bracket.
        auto lo = vk, hi = openVolts, vg = openVolts;

        for (int i = 0; i < 12; ++i)
        {
            const auto pos = (vg - vk) * gridScale;
            const auto ig = lookup (gridTable, pos);
            const auto f = vg - openVolts + sourceOhms * ig;

            (f > 0.0f ? hi : lo) = vg;

            if (std::abs (f) < 1.0e-6f)
                break;

            const auto slope = 1.0f + sourceOhms * gridSlope (pos);
            auto next = vg - f / slope;

            if (! (next > lo && next < hi))
                next = 0.5f * (lo + hi);

            vg = next;
        }

        return vg;
    }

    double getQuiescentPlateVolts() const noexcept { return quiescentPlate; }
    double getQuiescentCathodeVolts() const noexcept { return quiescentCathode; }

    /** Small-signal gain around the operating point (volts out per volt in),
        with only the plate resistor as load. */
    float getSmallSignalGain() const noexcept { return smallSignalGain; }

    /** Output resistance at the plate around the operating point: rp || Rp. */
    double getOutputOhms() const noexcept { return outputOhms; }

    /** Input capacitance seen at the grid when the plate drives loadOhms (AC):
        Cgk plus the Miller-multiplied Cgp. */
    double getInputFarads (double loadOhms) const noexcept
    {
        const auto gain = smallSignalGain * loadOhms / (loadOhms + outputOhms);
        return tube.gridCathodeFarads + tube.gridPlateFarads * (1.0 + gain);
    }

    /** Plate current in amps (Koren), from plate-to-cathode and grid-to-cathode volts. */
    static double plateCurrent (double plateCathodeVolts, double gridCathodeVolts, const Tube&) noexcept;

    /** Grid current in amps for a grid-to-cathode voltage (diode behind gridOhms). */
    static double gridCurrent (double gridCathodeVolts, const Tube&) noexcept;

    /** Solves the load line for one grid-to-cathode voltage with the cathode
        held at cathodeVolts; returns the plate voltage to ground. */
    static double solvePlateVolts (double gridCathodeVolts, double cathodeVolts, const Design&, const Tube&) noexcept;

private:
    static constexpr float inputMin = -16.0f, inputMax = 16.0f;
    static constexpr float gridMax = 16.0f;
    static constexpr int tableSize = 4097;

    static float lookup (const std::vector<float>& table, float pos) noexcept
    {
        if (pos <= 0.0f)
            return table.front();

        if (pos >= (float) (table.size() - 1))
            return table.back();

        const auto i = (size_t) pos;
        const auto frac = pos - (float) i;
        return table[i] + frac * (table[i + 1] - table[i]);
    }

    /** dIg/dVgk at a grid table position. */
    float gridSlope (float pos) const noexcept
    {
        const auto last = (float) (gridTable.size() - 2);
        const auto i = (size_t) std::max (0.0f, std::min (pos, last));
        return (gridTable[i + 1] - gridTable[i]) * gridScale;
    }

    std::vector<float> plateTable, gridTable;
    float plateScale = 0.0f, gridScale = 0.0f;
    float smallSignalGain = 0.0f;
    double quiescentPlate = 0.0, quiescentCathode = 0.0, outputOhms = 0.0;
    Tube tube;
};

} // namespace jmrig
