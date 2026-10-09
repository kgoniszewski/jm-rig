#include "ToneStack.h"

#include <algorithm>

namespace jmrig
{

ToneStack::Analog ToneStack::analogCoefficients (const Components& c, double treble, double bass, double mid) noexcept
{
    treble = std::clamp (treble, 0.0, 1.0);
    bass   = std::clamp (bass,   0.0, 1.0);
    mid    = std::clamp (mid,    0.0, 1.0);

    // Pot sections, named as in the circuit diagram in the header.
    const auto Rt = treble * c.treblePot;           // wiper to bottom
    const auto Ru = (1.0 - treble) * c.treblePot;   // top to wiper
    const auto Rb = bass * c.bassPot;
    const auto Rm = mid * c.midPot;
    const auto Rs = c.slope;
    const auto RL = c.load;
    const auto C1 = c.trebleCap, C2 = c.bassCap, C3 = c.midCap;

    Analog h;

    h.b[0] = 0.0;
    h.b[1] = RL * (C1 * (Rb + Rm + Rt) + C2 * (Rb + Rm) + C3 * Rm);
    h.b[2] = RL * (C1 * C2 * (Rb * Rs + Rb * Rt + Rb * Ru + Rm * Rs + Rm * Rt + Rm * Ru + Rs * Rt)
                 + C1 * C3 * (Rb * Rm + Rb * Rs + Rm * Rs + Rm * Rt + Rm * Ru + Rs * Rt)
                 + C2 * C3 * Rb * Rm);
    h.b[3] = C1 * C2 * C3 * RL * Rb * (Rm * Rs + Rm * Rt + Rm * Ru + Rs * Rt);

    h.a[0] = RL + Rb + Rm + Rt;
    h.a[1] = C1 * (RL * Rb + RL * Rm + RL * Rt + RL * Ru + Rb * Ru + Rm * Ru + Rt * Ru)
           + C2 * (RL * Rb + RL * Rm + RL * Rs + Rb * Rs + Rb * Rt + Rm * Rs + Rm * Rt + Rs * Rt)
           + C3 * (RL * Rm + RL * Rs + Rb * Rm + Rb * Rs + Rm * Rs + Rm * Rt + Rs * Rt);
    h.a[2] = C1 * C2 * (RL * Rb * Rs + RL * Rb * Rt + RL * Rb * Ru + RL * Rm * Rs + RL * Rm * Rt + RL * Rm * Ru
                        + RL * Rs * Rt + RL * Rs * Ru + Rb * Rs * Ru + Rb * Rt * Ru + Rm * Rs * Ru + Rm * Rt * Ru
                        + Rs * Rt * Ru)
           + C1 * C3 * (RL * Rb * Rm + RL * Rb * Rs + RL * Rm * Rs + RL * Rm * Rt + RL * Rm * Ru + RL * Rs * Rt
                        + RL * Rs * Ru + Rb * Rm * Ru + Rb * Rs * Ru + Rm * Rs * Ru + Rm * Rt * Ru + Rs * Rt * Ru)
           + C2 * C3 * (RL * Rb * Rm + RL * Rb * Rs + Rb * Rm * Rs + Rb * Rm * Rt + Rb * Rs * Rt);
    h.a[3] = C1 * C2 * C3 * Rb * (RL * Rm * Rs + RL * Rm * Rt + RL * Rm * Ru + RL * Rs * Rt + RL * Rs * Ru
                                  + Rm * Rs * Ru + Rm * Rt * Ru + Rs * Rt * Ru);
    return h;
}

} // namespace jmrig
