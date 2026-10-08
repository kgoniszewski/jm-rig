#pragma once

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

    The plate current comes from Koren's triode equations. prepare() finds the
    quiescent point, then solves the load line Vp = B+ - Rp * Ip(Vp, Vgk) for a
    dense grid of input voltages and stores the plate swing in a table, so the
    audio thread does one interpolated lookup per sample.

    Grid conduction: once the grid goes above the cathode, the grid draws
    current through the grid-stopper and source impedance, which compresses
    positive peaks. That is modelled as a soft limit on positive Vgk.

    Input is in volts at the grid; output is the change in plate voltage from
    its quiescent value, in volts (inverted, as a real stage is). */
class TriodeStage
{
public:
    struct Design
    {
        double supplyVolts     = 250.0;    // B+ at the plate resistor
        double plateOhms       = 100.0e3;  // Rp
        double cathodeOhms     = 1.5e3;    // Rk (bypassed for audio)
        double gridLimitVolts  = 0.6;      // where grid conduction flattens positive swing
    };

    /** Koren 12AX7 constants. */
    struct Tube
    {
        double mu = 100.0, ex = 1.4, kg1 = 1060.0, kp = 600.0, kvb = 300.0;
    };

    /** Allocates; call from prepare only. */
    void design (const Design&, const Tube&);

    float processSample (float gridVolts) const noexcept
    {
        const auto pos = (gridVolts - inputMin) * tableScale;

        if (pos <= 0.0f)
            return table.front();

        if (pos >= (float) (table.size() - 1))
            return table.back();

        const auto i = (int) pos;
        const auto frac = pos - (float) i;
        return table[(size_t) i] + frac * (table[(size_t) i + 1] - table[(size_t) i]);
    }

    double getQuiescentPlateVolts() const noexcept { return quiescentPlate; }
    double getQuiescentCathodeVolts() const noexcept { return quiescentCathode; }

    /** Small-signal gain around the operating point (volts out per volt in). */
    float getSmallSignalGain() const noexcept { return smallSignalGain; }

    /** Plate current in amps (Koren). Exposed for tests. */
    static double plateCurrent (double plateVolts, double gridCathodeVolts, const Tube&) noexcept;

    /** Solves the load line for one grid-to-cathode voltage. Exposed for tests. */
    static double solvePlateVolts (double gridCathodeVolts, const Design&, const Tube&) noexcept;

private:
    static constexpr float inputMin = -16.0f, inputMax = 16.0f;
    static constexpr int tableSize = 4097;

    std::vector<float> table;
    float tableScale = 0.0f;
    float smallSignalGain = 0.0f;
    double quiescentPlate = 0.0, quiescentCathode = 0.0;
};

} // namespace jmrig
