#include "TriodeStage.h"

#include <cmath>

namespace jmrig
{

double TriodeStage::plateCurrent (double vpk, double vgk, const Tube& t) noexcept
{
    if (vpk <= 0.0)
        return 0.0;

    // E1 = (Vpk / kP) * ln(1 + exp(kP * (1/mu + Vgk / sqrt(kVB + Vpk^2))))
    const auto x = t.kp * (1.0 / t.mu + vgk / std::sqrt (t.kvb + vpk * vpk));
    const auto softplus = x > 30.0 ? x : std::log1p (std::exp (x));
    const auto e1 = vpk / t.kp * softplus;

    // Koren: Ip = E1^Ex / Kg1 * (1 + sgn E1), in amps.
    return e1 > 0.0 ? 2.0 * std::pow (e1, t.ex) / t.kg1 : 0.0;
}

double TriodeStage::gridCurrent (double vgk, const Tube& t) noexcept
{
    if (vgk <= 0.0)
        return 0.0;

    // Diode (1 ohm series, as in Koren's model) behind gridOhms, at 27 C:
    // vgk = i * (R + 1) + Vt * ln(1 + i / Is). The right side rises with i.
    constexpr double thermalVolts = 0.025852;
    const auto r = t.gridOhms + 1.0;
    double lo = 0.0, hi = vgk / r + 1.0e-12;

    for (int i = 0; i < 80; ++i)
    {
        const auto mid = 0.5 * (lo + hi);
        const auto v = mid * r + thermalVolts * std::log1p (mid / t.gridDiodeAmps);
        (v > vgk ? hi : lo) = mid;
    }

    return 0.5 * (lo + hi);
}

double TriodeStage::solvePlateVolts (double vgk, double vk, const Design& d, const Tube& t) noexcept
{
    // The tube sees plate-to-cathode volts, Vpk = (B+ - Vk) - Rp * Ip(Vpk).
    // f(Vpk) = Vpk - (B+ - Vk - Rp * Ip(Vpk)) rises monotonically, so bisection
    // always converges. 60 halvings resolve B+ below 1e-15 V.
    const auto available = d.supplyVolts - vk;
    double lo = 0.0, hi = available;

    for (int i = 0; i < 60; ++i)
    {
        const auto mid = 0.5 * (lo + hi);
        const auto f = mid - (available - d.plateOhms * plateCurrent (mid, vgk, t));
        (f > 0.0 ? hi : lo) = mid;
    }

    return 0.5 * (lo + hi) + vk;
}

void TriodeStage::design (const Design& d, const Tube& t)
{
    // Quiescent point with the grid at 0 V: Vk = Rk * Ip(Vp, -Vk). Fixed-point
    // iteration converges quickly because Rk is small next to Rp.
    double vk = 1.0, vp = 0.0;

    for (int i = 0; i < 100; ++i)
    {
        vp = solvePlateVolts (-vk, vk, d, t);
        const auto next = d.cathodeOhms * plateCurrent (vp - vk, -vk, t);

        if (std::abs (next - vk) < 1.0e-9)
            break;

        vk = 0.5 * (vk + next);
    }

    tube = t;
    quiescentCathode = vk;
    quiescentPlate = solvePlateVolts (-vk, vk, d, t);

    // The bypass capacitor holds the cathode at its quiescent voltage for
    // audio frequencies, so Vgk = Vin - Vk.
    plateTable.resize ((size_t) tableSize);
    plateScale = (float) (tableSize - 1) / (inputMax - inputMin);

    for (int i = 0; i < tableSize; ++i)
    {
        const auto vin = (double) inputMin + (double) i / plateScale;
        plateTable[(size_t) i] = (float) (solvePlateVolts (vin - vk, vk, d, t) - quiescentPlate);
    }

    gridTable.resize ((size_t) tableSize);
    gridScale = (float) (tableSize - 1) / gridMax;

    for (int i = 0; i < tableSize; ++i)
        gridTable[(size_t) i] = (float) gridCurrent ((double) i / gridScale, t);

    const auto h = 1.0e-3;
    smallSignalGain = (float) ((solvePlateVolts (-vk - h, vk, d, t) - solvePlateVolts (-vk + h, vk, d, t)) / (2.0 * h));

    // Plate resistance rp = dVpk / dIp at the operating point, in parallel with Rp.
    const auto vpk = quiescentPlate - vk;
    const auto rp = 2.0 * h / (plateCurrent (vpk + h, -vk, t) - plateCurrent (vpk - h, -vk, t));
    outputOhms = rp * d.plateOhms / (rp + d.plateOhms);
}

} // namespace jmrig
