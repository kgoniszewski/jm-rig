# JM Rig

A circuit-modelled guitar amp and cab for macOS (AU, AUv3, Standalone) and iPadOS
(AUv3 + Standalone app), built with JUCE 9.0.3 and C++20.

An independent project, not affiliated with Neural DSP or John Mayer.

## Status

**0.1.0: skeleton.** The signal chain, parameters, oversampling, latency reporting
and builds are in place. The amp is a placeholder soft clipper and the cab is a
pass-through; both get replaced by real models (see the roadmap).

```
input gain -> DC block -> amp (4x oversampled) -> cab -> output gain
                                                         \
dry, delayed by the reported latency -------------- bypass crossfade
```

## Roadmap

1. **Skeleton** (this): CMake project, headless engine, AU/AUv3/Standalone, CI.
2. **Cab**: low-latency IR convolution for your own IRs, mic blend, low/high cut.
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

## Layout

- `source/dsp/`: headless engine. No GUI, no APVTS, nothing that may block.
- `source/plugin/`: processor, parameters, editor.
- `tests/`: engine tests (allocation-free `process()`, block-size edge cases,
  latency, denormals).
- `docs/architecture.md`: design notes and the real-time rules.
