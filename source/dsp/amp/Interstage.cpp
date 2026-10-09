#include "Interstage.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace jmrig
{

namespace
{
    // A pot section at the end of its travel is a wire; keep it a small
    // resistor so the node matrix stays well conditioned.
    constexpr double minOhms = 1.0;

    double ohms (double r) noexcept { return std::max (r, minOhms); }
}

void Interstage::prepare (double rate, const Parts& p) noexcept
{
    sampleRate = rate;
    parts = p;

    const auto& t = parts.toneStack;
    caps[c1]      = { plate,  trebleCap,  t.trebleCap,            0.0, 0.0 };
    caps[c2]      = { slope,  bassBottom, t.bassCap,              0.0, 0.0 };
    caps[c3]      = { slope,  midTop,     t.midCap,               0.0, 0.0 };
    caps[cBright] = { potTop, wiper,      0.0,                    0.0, 0.0 };
    caps[cGrid]   = { wiper,  -1,         parts.gridFarads,       0.0, 0.0 };

    setControls (0.5, 0.5, 0.5, 0.5, 0.0);
    reset();
}

void Interstage::reset() noexcept
{
    for (auto& c : caps)
        c.history = 0.0;

    v.fill (0.0);
}

void Interstage::stamp (int a, int b, double siemens) noexcept
{
    if (a >= 0) g[(size_t) (a * numNodes + a)] += siemens;
    if (b >= 0) g[(size_t) (b * numNodes + b)] += siemens;

    if (a >= 0 && b >= 0)
    {
        g[(size_t) (a * numNodes + b)] -= siemens;
        g[(size_t) (b * numNodes + a)] -= siemens;
    }
}

void Interstage::setControls (double gain, double treble, double bass, double mid, double bright) noexcept
{
    gain   = std::clamp (gain,   0.0, 1.0);
    treble = std::clamp (treble, 0.0, 1.0);
    bass   = std::clamp (bass,   0.0, 1.0);
    mid    = std::clamp (mid,    0.0, 1.0);

    const auto& t = parts.toneStack;
    g.fill (0.0);

    stamp (plate,      -1,         1.0 / parts.sourceOhms);
    stamp (plate,      slope,      1.0 / t.slope);
    stamp (trebleCap,  potTop,     1.0 / ohms ((1.0 - treble) * t.treblePot));
    stamp (potTop,     bassBottom, 1.0 / ohms (treble * t.treblePot));
    stamp (bassBottom, midTop,     1.0 / ohms (bass * t.bassPot));
    stamp (midTop,     -1,         1.0 / ohms (mid * t.midPot));
    stamp (potTop,     wiper,      1.0 / ohms ((1.0 - gain) * parts.gainPotOhms));
    stamp (wiper,      -1,         1.0 / ohms (gain * parts.gainPotOhms));

    caps[cBright].farads = std::clamp (bright, 0.0, 1.0) * parts.brightCapFarads;

    for (auto& c : caps)
    {
        c.conductance = 2.0 * sampleRate * c.farads;
        stamp (c.a, c.b, c.conductance);
    }

    // Gauss-Jordan inversion with partial pivoting, in place on a copy.
    auto m = g;
    constexpr auto n = (size_t) numNodes;

    for (size_t r = 0; r < n; ++r)
        for (size_t c = 0; c < n; ++c)
            inverse[r * n + c] = r == c ? 1.0 : 0.0;

    for (size_t col = 0; col < n; ++col)
    {
        auto pivot = col;

        for (size_t r = col + 1; r < n; ++r)
            if (std::abs (m[r * n + col]) > std::abs (m[pivot * n + col]))
                pivot = r;

        if (pivot != col)
            for (size_t c = 0; c < n; ++c)
            {
                std::swap (m[col * n + c], m[pivot * n + c]);
                std::swap (inverse[col * n + c], inverse[pivot * n + c]);
            }

        const auto scale = 1.0 / m[col * n + col];

        for (size_t c = 0; c < n; ++c)
        {
            m[col * n + c] *= scale;
            inverse[col * n + c] *= scale;
        }

        for (size_t r = 0; r < n; ++r)
        {
            if (r == col)
                continue;

            const auto f = m[r * n + col];

            for (size_t c = 0; c < n; ++c)
            {
                m[r * n + c] -= f * m[col * n + c];
                inverse[r * n + c] -= f * inverse[col * n + c];
            }
        }
    }
}

float Interstage::processSample (float sourceVolts, const TriodeStage& v1b) noexcept
{
    // Current injections: the source through its resistance, and each
    // capacitor's trapezoidal history current.
    std::array<double, numNodes> rhs {};
    rhs[plate] = (double) sourceVolts / parts.sourceOhms;

    for (const auto& c : caps)
    {
        if (c.a >= 0) rhs[(size_t) c.a] += c.history;
        if (c.b >= 0) rhs[(size_t) c.b] -= c.history;
    }

    constexpr auto n = (size_t) numNodes;

    for (size_t r = 0; r < n; ++r)
    {
        double acc = 0.0;

        for (size_t c = 0; c < n; ++c)
            acc += inverse[r * n + c] * rhs[c];

        v[r] = acc;
    }

    // Grid current leaves the wiper node. Column `wiper` of the inverse is
    // every node's response to current there; its diagonal entry is the
    // source resistance the grid sees.
    const auto sourceOhms = inverse[wiper * n + wiper];
    const auto open = v[wiper];
    const auto grid = (double) v1b.solveGrid ((float) open, (float) sourceOhms);

    if (grid < open)
    {
        const auto gridAmps = (open - grid) / sourceOhms;

        for (size_t r = 0; r < n; ++r)
            v[r] -= inverse[r * n + wiper] * gridAmps;
    }

    // i = G (va - vb) - history; next history = G (va - vb) + i.
    for (auto& c : caps)
    {
        const auto va = c.a >= 0 ? v[(size_t) c.a] : 0.0;
        const auto vb = c.b >= 0 ? v[(size_t) c.b] : 0.0;
        c.history = 2.0 * c.conductance * (va - vb) - c.history;
    }

    return (float) v[wiper];
}

} // namespace jmrig
