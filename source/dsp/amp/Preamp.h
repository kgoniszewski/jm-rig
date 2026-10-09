#pragma once

#include "../DcBlocker.h"
#include "Interstage.h"
#include "TriodeStage.h"

namespace jmrig
{

/** The preamp, from the guitar input to V1B's plate, in volts:

        grid stopper + V1A Miller cap    1-node circuit, with V1A's grid current
        V1A  12AX7, Rp 100k, Rk 1.5k     load-line table (TriodeStage)
        tone stack, Gain pot, bright     one nodal circuit (Interstage),
          cap, V1B's Miller cap          driven by V1A's output resistance,
                                         with V1B's grid current
        V1B  12AX7                       load-line table
        coupling cap into the next grid  DC blocker, plus V1B's loading

    Runs at whatever rate prepare() is given (the amp runs it oversampled).
    Kept separate from AmpStage so the SPICE null tests can drive it directly;
    tools/spice/generate.py simulates the same values. */
class Preamp
{
public:
    static constexpr double gridStopperOhms = 68.0e3;
    static constexpr double couplingFarads = 22.0e-9;
    static constexpr double nextGridOhms = 1.0e6;

    Preamp();

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /** Pot positions are electrical fractions 0..1 (after the taper); bright 0..1.
        Allocation-free; call every few samples while knobs move. */
    void setControls (double gain, double treble, double bass, double mid, double bright) noexcept;

    float processSample (float inputVolts) noexcept
    {
        // Grid stopper into V1A's input capacitance (trapezoidal), with grid
        // current solved against the node's resistance.
        const auto open = (inputVolts * stopperSiemens + inputHistory) * inputOhms;
        const auto grid = v1a.solveGrid (open, inputOhms);
        inputHistory = 2.0f * inputCapSiemens * grid - inputHistory;

        auto v = v1a.processSample (grid);
        v = v1b.processSample (interstage.processSample (v, v1b));
        return coupling.processSample (v * outputLoading);
    }

private:
    TriodeStage v1a, v1b;
    float stopperSiemens = 0.0f, inputCapSiemens = 0.0f, inputOhms = 0.0f, inputHistory = 0.0f;
    Interstage interstage;
    DcBlocker coupling;
    float outputLoading = 1.0f;
};

} // namespace jmrig
