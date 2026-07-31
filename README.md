# Spectral Formant Morpher

Spectral Formant Morpher is a real-time VST3/AU/Standalone audio effect for reshaping vocal timbre. It detects the input spectral envelope and warps its formant peaks toward user-controlled F1-F15 targets while leaving the harmonic spacing—and therefore the perceived pitch—unchanged.

This is a spectral-envelope processor, not a neural voice-conversion or pitch-shifting system. A Voice Profile is a lightweight formant recipe rather than a learned speaker identity.

## Features

- **F1/F2 target pad** with mouse and keyboard control
- **Independent F3-F15 faders** with a monotonic DSP mapping
- **Reference audio analysis** from WAV, AIFF, FLAC, or Ogg files
- **Voice Profile sharing** through clipboard JSON or `.sfmprofile` files
- **Latency-aligned dry/wet mix** and smoothed output gain
- **Live logarithmic analyzer** for the input spectrum and morphed envelope
- **Resizable, accessible dark UI** with named controls and keyboard focus states

## Signal flow

1. A 1024-sample Hann-windowed STFT runs at 75% overlap.
2. Cepstral liftering extracts a smooth spectral envelope.
3. Up to 15 envelope peaks are detected as the current formants.
4. A monotonic piecewise-linear map moves those peaks toward F1-F15.
5. The warped envelope is applied to the original spectral fine structure.
6. Inverse STFT and overlap-add reconstruct the signal.

The wet path has a fixed latency of 1024 samples. The plugin reports this latency to the host and delays the dry path by the same amount, so intermediate dry/wet settings remain phase-aligned.

## Reference audio and Voice Profiles

`Analyze reference` reads up to the first six seconds of a file on a background thread. It combines as many as eight energetic, non-overlapping analysis frames in the log-envelope domain, rejects silence, isolated transients, and low-confidence results, then applies only the envelope peaks that were actually detected. Higher targets are left unchanged instead of being replaced with synthetic estimates. Clear, sustained vocal audio gives the most useful result.

A Voice Profile contains:

- F1-F15 target frequencies
- dry/wet mix
- output gain
- a format type and version

Imported values are validated before any parameter is changed. Crossing formants are corrected into ascending order so the saved/UI values match the DSP target.

## Build

### Requirements

- CMake 3.20 or newer
- A C++20 compiler
- Git, used by CMake to fetch JUCE 8.0.12

Ubuntu/Debian packages:

```bash
sudo apt-get install libasound2-dev libjack-jackd2-dev ladspa-sdk \
    libcurl4-openssl-dev libfreetype-dev pkg-config \
    libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
    libxinerama-dev libxrandr-dev libxrender-dev \
    libwebkit2gtk-4.1-dev libgtk-3-dev \
    libglu1-mesa-dev mesa-common-dev
```

Configure and build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Artifacts are written under:

```text
build/SpectralFormantMorpher_artefacts/Release/
```

The default build does not copy plugins into system plugin folders. For a local development build that should install the result automatically:

```bash
cmake -S . -B build -DSFM_COPY_PLUGIN_AFTER_BUILD=ON
```

The default macOS deployment target is macOS 11.0 and can be overridden with `CMAKE_OSX_DEPLOYMENT_TARGET`. Local builds use the host architecture by default; CI packages macOS artifacts as Universal 2 (`arm64` and `x86_64`). Local and CI artifacts are not Developer ID signed or notarized.

## Tests

```bash
ctest --test-dir build -C Release --output-on-failure
```

The regression runner covers:

- empty and explicit identity warp maps
- piecewise formant mapping
- cepstral-envelope gain preservation
- JUCE FFT round-trip normalization
- STFT impulse gain and exact latency
- stereo channel isolation
- silent-reference rejection
- isolated-transient reference rejection
- sustained formant-shaped reference acceptance
- cooperative reference-analysis cancellation
- low-sample-rate finite-output safety
- actual processor bypass/dry-path latency alignment across mode changes

## CI and releases

`.github/workflows/build.yml` builds and tests Ubuntu, macOS, and Windows on every push and pull request.

Tags must match the version in `CMakeLists.txt` (for example, project version `0.1.0` requires tag `v0.1.0`). The release workflow publishes native artifacts only after all three platform builds and the regression suite succeed.

Platform outputs:

- macOS: VST3, AU, Standalone
- Windows: VST3, Standalone
- Linux: VST3, Standalone
