#pragma once

#include <array>
#include "ToneStack.h"
#include "TriodeStage.h"

namespace jmrig
{

/** Everything between V1A's plate and V1B's grid, solved as one circuit:

        V1A (Thevenin: open-circuit swing behind its plate resistance Ro)
          -> tone stack (the netlist in ToneStack.h)
          -> Gain pot, top to wiper Ru, wiper to ground Rl, bright cap across Ru
          -> V1B grid, with its Miller input capacitance to ground

    The parts load each other: V1A's output resistance against the tone
    stack, the tone stack's output resistance against the Gain pot, and the
    pot's wiper resistance against V1B's Miller capacitance (the treble loss
    as Gain comes up from zero). Treating them as separate stages, as the
    first version did, read about 2.5 dB loud and up to 8 dB bright against
    SPICE.

    Solved with modified nodal analysis, each capacitor discretised with the
    trapezoidal rule (a conductance plus a history current, the same
    bilinear map a wave digital filter uses). The node matrix only changes
    with the knobs, so its inverse is recomputed in setControls() and each
    sample is one 7x7 matrix-vector product. V1B's grid current is the one
    nonlinear element: the inverse gives the network's resistance seen from
    the grid, so it is a one-variable solve against that. */
class Interstage
{
public:
    struct Parts
    {
        ToneStack::Components toneStack;
        double sourceOhms     = 38.0e3;    // V1A's plate output resistance, rp || Rp
        double gainPotOhms    = 1.0e6;
        double brightCapFarads = 120.0e-12;
        double gridFarads     = 150.0e-12; // V1B's Miller input capacitance
    };

    void prepare (double sampleRate, const Parts&) noexcept;
    void reset() noexcept;

    /** Electrical pot fractions 0..1 (after the taper), bright 0..1.
        Allocation-free; inverts a 7x7 matrix, so call every few samples. */
    void setControls (double gain, double treble, double bass, double mid, double bright) noexcept;

    /** Open-circuit plate swing of V1A in, V1B grid volts out. V1B's grid
        current is solved together with the network, so the tone stack and
        Gain pot see it, as they do on the chassis. */
    float processSample (float sourceVolts, const TriodeStage& v1b) noexcept;

private:
    enum Node { plate, trebleCap, slope, bassBottom, midTop, potTop, wiper, numNodes };
    enum Cap { c1, c2, c3, cBright, cGrid, numCaps };

    struct Capacitor
    {
        int a = -1, b = -1;   // nodes; -1 is ground
        double farads = 0.0, conductance = 0.0, history = 0.0;
    };

    void stamp (int a, int b, double siemens) noexcept;

    Parts parts;
    double sampleRate = 192000.0;

    std::array<Capacitor, numCaps> caps;
    std::array<double, numNodes * numNodes> g {}, inverse {};
    std::array<double, numNodes> v {};
};

} // namespace jmrig
