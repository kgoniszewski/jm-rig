# JM Rig

A circuit-modelled guitar amp and cab for macOS (AU, AUv3, Standalone) and iPadOS
(AUv3 + Standalone app), built with JUCE 9.0.3 and C++20.

An independent project, not affiliated with Neural DSP or John Mayer.

## Status

**0.2.0: cab.** The signal chain, parameters, oversampling, latency reporting
and builds are in place, and the cab plays your own impulse responses. The amp is
still a placeholder soft clipper until step 3.

The cab has two IR slots (A and B) with a blend, a low cut, a high cut and an
on/off switch. Load WAV, AIFF or FLAC files up to 0.5 s at any sample rate. Each IR
is normalised to the same loudness, and the file itself is saved inside your
session, so it comes back on another computer or inside the iPad's AUv3 sandbox.
With no IR loaded, a built-in generic 1x12 response is used.

```
input gain -> DC block -> amp (4x oversampled) -> cab -> output gain
                                                         \
dry, delayed by the reported latency -------------- bypass crossfade
```

## Roadmap

1. **Skeleton** (done): CMake project, headless engine, AU/AUv3/Standalone, CI.
2. **Cab** (this): zero-latency IR convolution for your own IRs, A/B blend, low/high cut.
3. **Amp**: Dumble/Two-Rock-style clean amp. WDF tone stack, fitted 12AX7 and
   power-amp stages. Gain, Bass, Mid, Treble, Presence, Master, Bright.
4. **Tune and test**: null tests against SPICE renders, iPad CPU profiling, UI.

## Building

Clone with the JUCE submodule:

```sh
git clone --recursive https://github.com/kgoniszewski/jm-rig.git
```

macOS (Xcode project; AUv3 needs the Xcode generator):

```sh
cmake -S . -B build -G Xcode
open build/JMRig.xcodeproj
```

iPadOS (pick your team under Signing & Capabilities for both the app and the
AUv3 extension, then run on the iPad):

```sh
cmake -S . -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0
open build-ios/JMRig.xcodeproj
```

Engine tests (any platform):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target JMRigTests && ctest --test-dir build
```

To also run every `.wav` in a folder through the real IR path (decode, load,
play, check for allocations), point `JMRIG_IR_DIR` at it. IRs are not kept in
this repo.

```sh
JMRIG_IR_DIR=~/IRs ./build/JMRigTests_artefacts/Release/JMRigTests
```

## Layout

- `source/dsp/`: headless engine. No GUI, no APVTS, nothing that may block.
- `source/plugin/`: processor, parameters, editor.
- `tests/`: engine tests (allocation-free `process()`, block-size edge cases,
  latency, denormals).
- `docs/architecture.md`: design notes and the real-time rules.
