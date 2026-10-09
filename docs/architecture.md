# Architecture

## Layers

| Layer | Path | Depends on | Runs on |
|---|---|---|---|
| Engine | `source/dsp` | `juce_dsp` only | audio thread |
| Plugin | `source/plugin` | engine, `juce_audio_processors`, GUI | host threads |
| Tests | `tests` | engine | CI |

The engine knows nothing about APVTS. Once per block the processor reads each
parameter's atomic (`getRawParameterValue`, relaxed load) into a plain
`RigParameters` struct and hands it to `RigEngine::setParameters()`, which only
sets smoothing targets. This keeps the engine headless, testable and reusable
for the iPad app and the AUv3 extension alike.

`jmrig_dsp` is a CMake INTERFACE library: JUCE modules compile into each final
target, so a static library would compile them twice and fail to link.

## Real-time rules

Inside `processBlock()` and everything it calls:

- No allocation. Buffers are sized in `prepareToPlay()`. The test runner
  replaces global `operator new` and fails if `process()` allocates.
- No locks, no I/O, no logging, no Objective-C messaging, no system calls.
- Any block length works: 0, 1, odd sizes, and blocks larger than the size
  promised in `prepareToPlay()` (split into chunks, never overrun).
- `juce::ScopedNoDenormals` at the top of the render; DC blockers after any
  asymmetric non-linearity.
- Every gain-like parameter goes through `juce::SmoothedValue`; the amp's
  smoothers run at the oversampled rate.

## Signal flow and latency

- Mono internally. The guitar is input 1; input 2 of a stereo bus is ignored
  rather than summed (summing costs 6 dB and mixes in whatever else is there).
  The result is copied to every output channel.
- The amp runs at 4x through `juce::dsp::Oversampling` with polyphase IIR
  half-band filters and integer latency: 6 samples at the base rate, against
  roughly 60 for a linear-phase FIR cascade. That is 0.13 ms at 48 kHz, which
  matters for live playing on iPad.
- Latency is fixed after `prepareToPlay()` and reported with
  `setLatencySamples()`. It never changes while playing.
- Bypass is the host's bypass parameter (`getBypassParameter()`), implemented as
  a 20 ms crossfade to a dry signal delayed by the same latency, so toggling it
  neither clicks nor shifts timing. `processBlockBypassed()` keeps the engine
  running for the same reason.

## Amp

All of it runs inside the 4x oversampler, in volts from the guitar input to
V1B's plate, so clipping starts where it does in the circuit.

| Stage | Model |
|---|---|
| Grid stopper (68k) + V1A's Miller capacitance | One trapezoidal node, with V1A's grid current solved against it |
| V1A, V1B (12AX7, Rp 100k, Rk 1.5k bypassed, B+ 250 V) | `TriodeStage`: Koren plate current from plate-to-cathode volts, load line solved by bisection for 4097 grid voltages at startup, linear interpolation at run time. Operating point 170.1 V plate, 1.20 V cathode, gain 61 |
| Grid conduction | Koren's diode behind 2k, tabulated; `solveGrid()` finds the grid voltage against whatever drives it, so the driving circuit takes the current |
| Tone stack, Gain pot (1M audio) + 120 pF bright cap, V1B's Miller capacitance | `Interstage`: one nodal circuit driven by V1A's output resistance (rp ‖ Rp), capacitors discretised with the trapezoidal rule, matrix inverse recomputed every 32 samples while knobs move. V1B's grid current is a one-variable solve against the inverse |
| Coupling cap into the next grid | DC blocker at the RC corner, plus V1B's loading |
| Phase inverter, output pair, transformer, feedback | `PowerAmp`: symmetric soft clip scaled by sag headroom (8 ms attack, 180 ms recovery, up to 2.5 dB), presence shelf (0 to +8 dB at 3.5 kHz), speaker resonance shelf (+2.5 dB at 90 Hz), 35 Hz transformer high-pass |

Coefficient math is allocation-free: JUCE's IIR coefficient factories
allocate, so `amp/Filters.h` has small first-order and shelf sections, and
`Interstage` inverts its 7x7 matrix in place.

The tone stack values are an ODS-style stack as commonly published by clone
builders (250k treble, 1M bass, 25k mid, 100k slope, 250 pF, 22 nF, 22 nF),
voiced rather than measured. `ToneStack` keeps the stack's exact closed-form
transfer function as a reference the nodal network is tested against.

On this CI-class x86 container, the whole rig (amp, both IR slots, filters)
runs at about 27x real time at 48 kHz with 64-sample blocks, and 21x in the
worst case (Gain 10, two 0.5 s IRs, 32-sample blocks); see `testCpuBudget`.

### SPICE null tests

`tools/spice/generate.py` renders the preamp in ngspice as it is wired on the
chassis: Koren's 12AX7 models with grid diode and interelectrode capacitances,
self-biased stages with 22 µF bypass caps, the tone stack, Gain pot and bright
cap. `tests/SpiceNullTests.cpp` (`JMRigSpiceTests`) drives the plugin's
`Preamp` with the same signals.

| Check | Result |
|---|---|
| Triode operating point and DC transfer, -8 to +1 V grid | within 7 mV |
| Tone stack alone, 329 points | within 0.0001 dB |
| Whole preamp, small signal, 8 knob settings, 100 Hz to 6.4 kHz | within 0.35 dB |
| Whole preamp, 200 Hz, up to 1 V at the jack | nulls -21 to -25 dB, harmonics within a few dB |
| Whole preamp, 2 V at the jack, Gain 10 | null -13 dB |

What the first version got wrong, and these tests caught: the load line used
plate-to-ground instead of plate-to-cathode volts (0.4 V off at the operating
point); the stages were treated as unloaded, which read 2.5 dB loud and up to
8 dB too bright at Gain 8 (V1B's Miller capacitance against the Gain pot's
resistance); and grid conduction was a fixed soft limit, which left heavy
breakup 15 dB short on even harmonics.

What is still simplified: V1A drives the tone stack as a Thevenin source
(its open-circuit swing behind its small-signal output resistance), so its
own clipping against the real load shows in the 2 V case; the cathodes are
held at their quiescent voltage (SPICE says bias shift barely changes the
harmonics here); and Miller capacitance uses the small-signal gain.

## Cab

- Two `juce::dsp::Convolution` engines (slots A and B) sharing one background
  loader queue, non-uniform partitioned with a 256-sample head: zero latency
  at any host block size, and long IRs cost little more than short ones.
- Loading is wait-free for the audio thread. The decoder (`ImpulseDecoder`)
  runs on the message thread, the convolution engine resamples the IR to the
  session rate on its own thread, then crossfades it in during `process()`.
  A test swaps an IR mid-stream and checks for allocations and clicks.
- IRs are trimmed to 0.5 s and normalised to unit energy (white noise in,
  same RMS out), so captures made at different levels sound equally loud.
  Only channel 0 of a stereo file is used.
- The processor keeps each loaded file's bytes and writes them into the
  plugin state (`CabIRs` child, binary as base64). Sessions never depend on
  a file path, which matters on iPadOS where an AUv3 can't reopen a path
  from a previous launch.
- Blend, low cut and high cut are smoothed; the filter cutoffs are
  recomputed every 16 samples (TPT state-variable filters, Butterworth Q).

## Platforms

- macOS: universal binary (arm64 + x86_64), deployment target 13.0.
- iPadOS: arm64, deployment target 17.0, landscape only, background audio and
  microphone permission for the standalone app. The AUv3 extension is embedded
  in the standalone app, which is how iPadOS distributes AUv3s.
