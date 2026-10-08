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
| Grid stopper + Miller capacitance | 1-pole low-pass, 18 kHz |
| V1A, V1B (12AX7, Rp 100k, Rk 1.5k bypassed, B+ 250 V) | `TriodeStage`: Koren plate current, load line solved by bisection for 4097 grid voltages at startup, linear interpolation at run time. Grid conduction soft-limits positive Vgk. Operating point 170 V plate, 1.2 V cathode, gain 61 |
| Coupling caps | DC blockers |
| Tone stack | `ToneStack`: exact third-order transfer function of the netlist (coefficients derived symbolically, verified in tests against a numeric nodal solve to 1e-9 dB), bilinear transform, recomputed every 32 samples while knobs move |
| Gain pot (1M audio taper) + 120 pF bright cap | Exact first-order divider; the bright lift fades as the pot opens, as on the amp |
| Phase inverter, output pair, transformer, feedback | `PowerAmp`: symmetric soft clip scaled by sag headroom (8 ms attack, 180 ms recovery, up to 2.5 dB), presence shelf (0 to +8 dB at 3.5 kHz), speaker resonance shelf (+2.5 dB at 90 Hz), 35 Hz transformer high-pass |

Coefficient math is allocation-free: JUCE's IIR coefficient factories
allocate, so `amp/Filters.h` has small first-order and shelf sections.

The tone stack values are an ODS-style stack as commonly published by clone
builders (250k treble, 1M bass, 25k mid, 100k slope, 250 pF, 22 nF, 22 nF),
voiced rather than measured. Step 4 compares the whole amp against SPICE.

On this CI-class x86 container, the whole rig (amp, both IR slots, filters)
runs at about 40x real time at 48 kHz with 64-sample blocks (`testCpuBudget`).

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
