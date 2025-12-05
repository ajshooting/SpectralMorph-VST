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

struct WarpingPoint
{
    float srcBin;
    float dstBin;
};

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
     * The points must be sorted by dstBin.
     *
     * The map is generated such that for a given output bin index 'i',
     * warpMap[i] gives the fractional input bin index to sample from.
     *
     * @param numBins The number of frequency bins in the half-spectrum.
     * @param points A list of control points mapping Source Bin -> Destination Bin.
     */
    void calculateWarpMap(int numBins, std::vector<WarpingPoint> points)
    {
        if (warpMap.size() != (size_t)numBins)
            warpMap.resize((size_t)numBins);

        // Ensure we cover the full range
        // If the first point isn't 0->0, add it.
        if (points.empty() || points.front().dstBin > 0.001f)
        {
            points.insert(points.begin(), {0.0f, 0.0f});
        }

        // If the last point isn't Nyquist->Nyquist (approx), add it.
        // Or assume linear extension? Usually better to anchor Nyquist.
        if (points.back().dstBin < (float)(numBins - 1))
        {
            points.push_back({(float)(numBins - 1), (float)(numBins - 1)});
        }

        // Sort by destination bin just in case
        std::sort(points.begin(), points.end(), [](const WarpingPoint& a, const WarpingPoint& b) {
            return a.dstBin < b.dstBin;
        });

        // Generate Map
        // We iterate through output bins 'i'. We find which segment of 'dstBin' 'i' falls into.
        // Then interpolate the corresponding 'srcBin'.

        int currentSegment = 0;

        for (int i = 0; i < numBins; ++i)
        {
            float outBin = (float)i;

            // Advance segment if needed
            while (currentSegment < (int)points.size() - 1 && outBin > points[(size_t)currentSegment + 1].dstBin)
            {
                currentSegment++;
            }

            if (currentSegment >= (int)points.size() - 1)
            {
                // Beyond last point, clamp or extrapolate?
                // Since we added Nyquist anchor, we should be fine, but let's clamp.
                warpMap[(size_t)i] = points.back().srcBin;
            }
            else
            {
                const auto& p0 = points[(size_t)currentSegment];
                const auto& p1 = points[(size_t)currentSegment + 1];

                float range = p1.dstBin - p0.dstBin;
                float frac = 0.0f;
                if (range > 0.0001f)
                    frac = (outBin - p0.dstBin) / range;

                // Interpolate Source
                float srcVal = p0.srcBin + frac * (p1.srcBin - p0.srcBin);
                warpMap[(size_t)i] = std::max(0.0f, std::min(srcVal, (float)(numBins - 1)));
            }
        }
    }

    /**
     * Legacy/Simple interface for compatibility during refactor.
     * Equivalent to a single stretch point at Nyquist/2 or similar,
     * but actually the previous logic was linear scaling: src = dst * (1/factor).
     * This corresponds to mapping:
     *   0 -> 0
     *   Nyquist -> Nyquist * (1/factor)  (if factor > 1, we map full output to partial input)
     *
     *   Wait, if we stretch (factor > 1), we want Output(F) to match Input(F/factor).
     *   So Output(Nyquist) maps to Input(Nyquist/factor).
     *   Control Point: Src=Nyquist/Factor -> Dst=Nyquist.
     */
    void calculateWarpMapLegacy(int numBins, float shiftFactor)
    {
        std::vector<WarpingPoint> points;
        points.push_back({0.0f, 0.0f});

        float nyquist = (float)(numBins - 1);
        float srcAtNyquist = nyquist * (1.0f / std::max(0.1f, shiftFactor));

        points.push_back({srcAtNyquist, nyquist});

        calculateWarpMap(numBins, points);
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
        size_t maxIdx = size - 1;

        for (size_t i = 0; i < size; ++i)
        {
            float srcIdx = warpMap[i];

            // Linear Interpolation
            int idx0 = (int)srcIdx;
            int idx1 = std::min(idx0 + 1, (int)maxIdx);
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
