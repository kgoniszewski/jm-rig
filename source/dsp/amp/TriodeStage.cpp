#include "TriodeStage.h"

#include <cmath>

namespace jmrig
{

double TriodeStage::plateCurrent (double vp, double vgk, const Tube& t) noexcept
{
    if (vp <= 0.0)
        return 0.0;

    // E1 = (Vp / kP) * ln(1 + exp(kP * (1/mu + Vgk / sqrt(kVB + Vp^2))))
    const auto x = t.kp * (1.0 / t.mu + vgk / std::sqrt (t.kvb + vp * vp));
    const auto softplus = x > 30.0 ? x : std::log1p (std::exp (x));
    const auto e1 = vp / t.kp * softplus;

    // Koren: Ip = E1^Ex / Kg1 * (1 + sgn E1), in amps.
    return e1 > 0.0 ? 2.0 * std::pow (e1, t.ex) / t.kg1 : 0.0;
}

double TriodeStage::solvePlateVolts (double vgk, const Design& d, const Tube& t) noexcept
{
    // f(Vp) = Vp - (B+ - Rp * Ip(Vp)) rises monotonically from 0 to B+,
    // so bisection always converges. 60 halvings resolve B+ below 1e-15 V.
    double lo = 0.0, hi = d.supplyVolts;

    for (int i = 0; i < 60; ++i)
    {
        const auto mid = 0.5 * (lo + hi);
        const auto f = mid - (d.supplyVolts - d.plateOhms * plateCurrent (mid, vgk, t));
        (f > 0.0 ? hi : lo) = mid;
    }

    return 0.5 * (lo + hi);
}

void TriodeStage::design (const Design& d, const Tube& t)
{
    // Quiescent point with the grid at 0 V: Vk = Rk * Ip(Vp, -Vk). Fixed-point
    // iteration converges quickly because Rk is small next to Rp.
    double vk = 1.0, vp = 0.0;

    for (int i = 0; i < 100; ++i)
    {
        vp = solvePlateVolts (-vk, d, t);
        const auto next = d.cathodeOhms * plateCurrent (vp, -vk, t);

        if (std::abs (next - vk) < 1.0e-9)
            break;

        vk = 0.5 * (vk + next);
    }

    quiescentCathode = vk;
    quiescentPlate = solvePlateVolts (-vk, d, t);

    // The bypass capacitor holds the cathode at its quiescent voltage for
    // audio frequencies, so Vgk = Vin - Vk.
    table.resize ((size_t) tableSize);
    tableScale = (float) (tableSize - 1) / (inputMax - inputMin);

    for (int i = 0; i < tableSize; ++i)
    {
        const auto vin = (double) inputMin + (double) i / tableScale;
        auto vgk = vin - vk;

        if (vgk > 0.0)
            vgk = d.gridLimitVolts * std::tanh (vgk / d.gridLimitVolts);

        table[(size_t) i] = (float) (solvePlateVolts (vgk, d, t) - quiescentPlate);
    }

    const auto h = 1.0e-3;
    smallSignalGain = (float) ((solvePlateVolts (-vk - h, d, t) - solvePlateVolts (-vk + h, d, t)) / (2.0 * h));
}

} // namespace jmrig
