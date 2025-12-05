#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

#if __has_include(<juce_core/juce_core.h>)
 #include <juce_core/juce_core.h>
#else
 #include <cassert>
 #ifndef jassert
  #define jassert assert
 #endif
#endif

namespace dsp
{

/**
 * Handles the frequency warping logic.
 * Generates a mapping from output frequency bin to input frequency bin.
 */
class FormantWarper
{
public:
    FormantWarper() = default;

    /**
     * Prepares the warp map based on control points.
     *
     * @param numBins The number of frequency bins in the half-spectrum.
     * @param shiftFactor A simple factor for testing. 1.0 = no change. > 1.0 = stretch (shift up), < 1.0 = compress (shift down).
     *                    In a full implementation, this would take a list of nodes (src_freq -> dst_freq).
     */
    void calculateWarpMap(int numBins, float shiftFactor)
    {
        if (warpMap.size() != (size_t)numBins)
            warpMap.resize((size_t)numBins);

        // Simple linear warping for now, centered around 0Hz?
        // Or maybe preserving 0 and shifting the rest.
        // Prompt says "shifting F1 from 500Hz to 700Hz".
        // If we apply a constant factor, it's a pitch shift of the formant.
        // warpMap[out_bin] = in_bin
        // If we shift UP (500 -> 700), the envelope at 700 (out) should come from 500 (in).
        // So in_bin = out_bin * (500/700) = out_bin * (1/shiftFactor).

        float invFactor = 1.0f / std::max(0.1f, shiftFactor);

        for (int i = 0; i < numBins; ++i)
        {
            float srcIndex = i * invFactor;
            warpMap[(size_t)i] = std::min(srcIndex, (float)(numBins - 1));
        }
    }

    /**
     * Warps the spectral envelope.
     *
     * @param srcEnvelope The original spectral envelope (magnitudes).
     * @param dstEnvelope The destination buffer (must be same size).
     */
    void process(const std::vector<float>& srcEnvelope, std::vector<float>& dstEnvelope)
    {
        jassert(srcEnvelope.size() == dstEnvelope.size());
        jassert(warpMap.size() == srcEnvelope.size());

        size_t size = srcEnvelope.size();

        for (size_t i = 0; i < size; ++i)
        {
            float srcIdx = warpMap[i];

            // Linear Interpolation
            int idx0 = (int)srcIdx;
            int idx1 = std::min(idx0 + 1, (int)size - 1);
            float frac = srcIdx - idx0;

            float val0 = srcEnvelope[(size_t)idx0];
            float val1 = srcEnvelope[(size_t)idx1];

            dstEnvelope[i] = val0 + frac * (val1 - val0);
        }
    }

    const std::vector<float>& getWarpMap() const { return warpMap; }

private:
    std::vector<float> warpMap;
};

} // namespace dsp
