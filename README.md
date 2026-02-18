# SpectralFormantMorpher

A real-time VST3/AU audio plugin for vocal timbre morphing with direct control of `F1〜F15` formants while preserving pitch.

## Features

- **F1/F2 XY Pad:** Move one point in XY space to control `F1` (Y axis) and `F2` (X axis) in Hz.
- **F3〜F15 Mixer-style Sliders:** Each higher formant can be controlled independently with vertical sliders.
- **Source Audio Import:** Load a source file (`wav/aiff/flac/mp3`) and auto-estimate/apply `F1〜F15` as the target template.
- **Real-time Morphing:** During playback, the current input envelope is warped toward the configured `F1〜F15` targets.
- **Real-time Visualization:** Spectrum + warped envelope preview while processing.

## Technical Details

The plugin is built with **JUCE 8** and **C++20**.

### DSP Pipeline

1.  **STFT Analysis:** The input signal is windowed (Hann) and transformed using FFT (1024 samples, 75% overlap).
2.  **Envelope Extraction:**
    - Log Magnitude Spectrum -> Inverse FFT -> Cepstrum.
    - Liftering (low-pass) to extract the smooth envelope.
    - Forward FFT -> Exponentiation to get the Linear Envelope.
3.  **Formant Detection:** Detect up to 15 envelope peaks as the current input formants.
4.  **Warping:** Build a piecewise-linear mapping from detected formants to target `F1〜F15` bins.
5.  **Resynthesis:** Apply warped envelope to the source spectral fine structure.
6.  **Reconstruction:** Inverse FFT and overlap-add synthesis.

## Build Instructions

### Prerequisites

- **CMake** (3.20+)
- **C++ Compiler** supporting C++20 (GCC 10+, Clang 10+, MSVC 2019+)
- **Linux Dependencies** (Ubuntu/Debian):
  ```bash
  sudo apt-get install libasound2-dev libjack-jackd2-dev ladspa-sdk \
      libcurl4-openssl-dev libfreetype-dev libx11-dev libxcomposite-dev \
      libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev \
      libwebkit2gtk-4.1-dev libgtk-3-dev
  ```

### Compilation

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release
```

The compiled plugin will be located in `build/SpectralFormantMorpher_artefacts/Release/`.

## Testing

Unit tests for the warping logic are included.

```bash
# Run tests
./build/Runner_artefacts/Release/Runner
```

## CI/CD

Automated builds for Ubuntu, macOS, and Windows are handled via GitHub Actions (`.github/workflows/build.yml`).
