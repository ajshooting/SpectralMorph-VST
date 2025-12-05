# SpectralFormantMorpher

A real-time VST3/AU audio plugin that allows for independent manipulation of vocal formants (F1, F2) without altering the pitch. This enables gender-bending effects, vocal tract resizing, and unique timbral transformations using Source-Filter theory.

## Features

*   **Source-Filter Separation:** Uses Cepstral Analysis to separate the spectral envelope (vocal tract) from the excitation (harmonics).
*   **Formant Warping:** Interactive dragging of F1 and F2 nodes to reshape the spectral envelope.
*   **Real-time Visualization:** High-performance (60fps) spectrum analyzer with overlaid envelope and control nodes.
*   **Parameters:**
    *   `F1 Shift`: Multiplier for the first formant frequency.
    *   `F2 Shift`: Multiplier for the second formant frequency.
    *   `Overall Scale`: Global frequency scaling factor (simulates Vocal Tract Length).

## Technical Details

The plugin is built with **JUCE 8** and **C++20**.

### DSP Pipeline
1.  **STFT Analysis:** The input signal is windowed (Hann) and transformed using FFT (1024 samples, 75% overlap).
2.  **Envelope Extraction:**
    *   Log Magnitude Spectrum -> Inverse FFT -> Cepstrum.
    *   Liftering (low-pass) to extract the smooth envelope.
    *   Forward FFT -> Exponentiation to get the Linear Envelope.
3.  **Warping:** A Piecewise Linear Warping map is generated based on the target F1/F2 shifts. This map interpolates the envelope indices.
4.  **Resynthesis:** The original "Excitation" (flattened spectrum) is multiplied by the new "Warped Envelope".
5.  **Reconstruction:** Inverse FFT and Overlap-Add method to reconstruct the time-domain signal.

## Build Instructions

### Prerequisites
*   **CMake** (3.20+)
*   **C++ Compiler** supporting C++20 (GCC 10+, Clang 10+, MSVC 2019+)
*   **Linux Dependencies** (Ubuntu/Debian):
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
