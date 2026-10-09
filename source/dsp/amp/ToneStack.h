#pragma once

#include <array>

namespace jmrig
{

/** Passive treble/bass/mid tone stack, solved exactly from its circuit.
    Netlist (pots as their two resistor sections):

        C1        in  -> T        treble cap
        Rslope    in  -> S        slope resistor
        (1-t)Rt   T   -> out      treble pot, top to wiper
        t·Rt      out -> X        treble pot, wiper to bottom
        Rload     out -> ground   next stage's volume pot
        C2        S   -> X        bass cap
        C3        S   -> M        mid cap
        l·Rb      X   -> M        bass pot, wired as a rheostat
        m·Rm      M   -> ground   mid pot, wired as a rheostat

    Nodal analysis of this network gives a third-order transfer function
    whose coefficients are polynomials in the pot resistances (derived
    symbolically; the test suite checks them against a numeric nodal solve
    and against SPICE).

    The amp itself runs the stack inside Interstage, together with the parts
    that load it; this closed form is the reference that network is tested
    against.

    Default values are a Dumble ODS-style stack as commonly published by
    clone builders, voiced rather than measured from a specific amp. */
class ToneStack
{
public:
    struct Components
    {
        double treblePot = 250.0e3;
        double bassPot   = 1.0e6;
        double midPot    = 25.0e3;
        double slope     = 100.0e3;
        double trebleCap = 250.0e-12;
        double bassCap   = 22.0e-9;
        double midCap    = 22.0e-9;
        double load      = 1.0e6;
    };

    /** H(s) = (b1 s + b2 s^2 + b3 s^3) / (a0 + a1 s + a2 s^2 + a3 s^3). */
    struct Analog
    {
        std::array<double, 4> b {}, a {};
    };

    /** Pot positions are electrical fractions, 0..1 (after the pot's taper). */
    static Analog analogCoefficients (const Components&, double treble, double bass, double mid) noexcept;
};

} // namespace jmrig
