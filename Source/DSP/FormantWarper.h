#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <juce_core/juce_core.h>

namespace dsp
{

/**
 * Represents a control point for frequency warping.
 * Maps a Source Frequency Bin to a Destination Frequency Bin.
 */
struct WarpingPoint
{
    float srcBin;
    float dstBin;
};

/**
 * Handles the frequency warping logic using Piecewise Linear Interpolation.
 *
 * The goal is to reshape the spectral envelope by mapping output frequency bins
 * to input frequency bins.
 *
 * Example: To shift a formant from 500Hz (src) to 700Hz (dst), we want the
 * output envelope at 700Hz to take the value from the input envelope at 500Hz.
 * Thus, the map should be: map[dst] = src.
 */
class FormantWarper
{
public:
    FormantWarper() = default;

    /**
     * Prepares the warp map based on a list of control points.
     *
     * @param numBins Number of frequency bins in the envelope.
     * @param points User-defined control points (e.g., F1->NewF1, F2->NewF2).
     */
    void calculateWarpMap(int numBins, std::vector<WarpingPoint> points)
    {
        if (warpMap.size() != (size_t)numBins)
            warpMap.resize((size_t)numBins);

        // Ensure we cover the full range [0, Nyquist]
        // Anchor 0Hz -> 0Hz
        if (points.empty() || points.front().dstBin > 0.001f)
            points.insert(points.begin(), {0.0f, 0.0f});

        // Anchor Nyquist -> Nyquist
        if (points.back().dstBin < (float)(numBins - 1))
            points.push_back({(float)(numBins - 1), (float)(numBins - 1)});

        // Sort points by Destination bin to allow binary search or linear scan
        std::sort(points.begin(), points.end(), [](const WarpingPoint& a, const WarpingPoint& b) {
            return a.dstBin < b.dstBin;
        });

        // Generate the Map: For every output bin 'i', find the corresponding source bin.
        int currentSegment = 0;
        for (int i = 0; i < numBins; ++i)
        {
            float outBin = (float)i;

            // Find the segment [p0, p1] that contains outBin
            while (currentSegment < (int)points.size() - 1 && outBin > points[(size_t)currentSegment + 1].dstBin)
                currentSegment++;

            if (currentSegment >= (int)points.size() - 1)
            {
                // Safety: Clamp to last known source point
                warpMap[(size_t)i] = points.back().srcBin;
            }
            else
            {
                // Linear Interpolation
                const auto& p0 = points[(size_t)currentSegment];
                const auto& p1 = points[(size_t)currentSegment + 1];

                float range = p1.dstBin - p0.dstBin;
                float frac = (range > 0.0001f) ? (outBin - p0.dstBin) / range : 0.0f;

                float srcVal = p0.srcBin + frac * (p1.srcBin - p0.srcBin);

                // Clamp source index to valid range
                warpMap[(size_t)i] = std::max(0.0f, std::min(srcVal, (float)(numBins - 1)));
            }
        }
    }

    /**
     * Applies the warping to the spectral envelope.
     *
     * @param srcEnvelope The original extracted envelope.
     * @param dstEnvelope The destination buffer for the warped envelope.
     */
    void process(const std::vector<float>& srcEnvelope, std::vector<float>& dstEnvelope)
    {
        jassert(srcEnvelope.size() == dstEnvelope.size());
        jassert(warpMap.size() == srcEnvelope.size());

        size_t size = srcEnvelope.size();
        size_t maxIdx = size - 1;

        for (size_t i = 0; i < size; ++i)
        {
            // Get the source index from the map
            float srcIdx = warpMap[i];

            // Linear Interpolation for smooth envelope resampling
            int idx0 = (int)srcIdx;
            int idx1 = std::min(idx0 + 1, (int)maxIdx);
            float frac = srcIdx - idx0;

            dstEnvelope[i] = srcEnvelope[(size_t)idx0] + frac * (srcEnvelope[(size_t)idx1] - srcEnvelope[(size_t)idx0]);
        }
    }

    const std::vector<float>& getWarpMap() const { return warpMap; }

private:
    std::vector<float> warpMap;
};

} // namespace dsp
