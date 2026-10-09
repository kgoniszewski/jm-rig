#include "Preamp.h"

namespace jmrig
{

namespace
{
    constexpr double twoPi = 2.0 * 3.14159265358979323846;
}

Preamp::Preamp()
{
    // Tube stages are designed once; they don't depend on the sample rate.
    TriodeStage::Design stage;
    v1a.design (stage, TriodeStage::Tube {});
    v1b.design (stage, TriodeStage::Tube {});

    // V1B's plate output resistance against the next grid resistor.
    outputLoading = (float) (nextGridOhms / (nextGridOhms + v1b.getOutputOhms()));
}

void Preamp::prepare (double sampleRate) noexcept
{
    // V1A's Miller capacitance behind the grid stopper. Its plate drives the
    // tone stack (around 100k at mid frequencies).
    stopperSiemens = (float) (1.0 / gridStopperOhms);
    inputCapSiemens = (float) (2.0 * sampleRate * v1a.getInputFarads (100.0e3));
    inputOhms = 1.0f / (stopperSiemens + inputCapSiemens);

    Interstage::Parts parts;
    parts.sourceOhms = v1a.getOutputOhms();
    parts.gridFarads = v1b.getInputFarads (nextGridOhms);
    interstage.prepare (sampleRate, parts);

    coupling.prepare (sampleRate, 1.0 / (twoPi * (nextGridOhms + v1b.getOutputOhms()) * couplingFarads));

    setControls (0.5, 0.5, 0.5, 0.5, 0.0);
    reset();
}

void Preamp::reset() noexcept
{
    inputHistory = 0.0f;
    interstage.reset();
    coupling.reset();
}

void Preamp::setControls (double gain, double treble, double bass, double mid, double bright) noexcept
{
    interstage.setControls (gain, treble, bass, mid, bright);
}

} // namespace jmrig
