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
    symbolically; the test suite checks them against a numeric nodal solve).
    The bilinear transform turns it into a digital filter. For a linear
    passive network this is exactly what a wave digital filter computes, at
    a fraction of the cost, and the coefficients are cheap enough to update
    every few samples while a knob turns.

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

    void prepare (double sampleRate, const Components&) noexcept;
    void reset() noexcept;

    /** Real-time safe: no allocation. */
    void setControls (double treble, double bass, double mid) noexcept;

    float processSample (float x) noexcept
    {
        // Transposed direct form II, double precision: a third-order section
        // with poles near DC at a 4x oversampled rate needs the headroom.
        const auto in = (double) x;
        const auto y = b[0] * in + s1;
        s1 = b[1] * in - a[1] * y + s2;
        s2 = b[2] * in - a[2] * y + s3;
        s3 = b[3] * in - a[3] * y;
        return (float) y;
    }

private:
    Components parts;
    double twoFs = 96000.0;
    std::array<double, 4> b {}, a {};
    double s1 = 0.0, s2 = 0.0, s3 = 0.0;
};

} // namespace jmrig
