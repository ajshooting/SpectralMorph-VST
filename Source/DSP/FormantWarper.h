#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <juce_core/juce_core.h>

namespace dsp
{

struct WarpingPoint
{
    float srcBin;
    float dstBin;
};

class FormantWarper
{
public:
    FormantWarper() = default;

    void calculateWarpMap(int numBins, std::vector<WarpingPoint> points)
    {
        if (warpMap.size() != (size_t)numBins)
            warpMap.resize((size_t)numBins);

        if (points.empty() || points.front().dstBin > 0.001f)
            points.insert(points.begin(), {0.0f, 0.0f});

        if (points.back().dstBin < (float)(numBins - 1))
            points.push_back({(float)(numBins - 1), (float)(numBins - 1)});

        std::sort(points.begin(), points.end(), [](const WarpingPoint& a, const WarpingPoint& b) {
            return a.dstBin < b.dstBin;
        });

        int currentSegment = 0;
        for (int i = 0; i < numBins; ++i)
        {
            float outBin = (float)i;
            while (currentSegment < (int)points.size() - 1 && outBin > points[(size_t)currentSegment + 1].dstBin)
                currentSegment++;

            if (currentSegment >= (int)points.size() - 1)
            {
                warpMap[(size_t)i] = points.back().srcBin;
            }
            else
            {
                const auto& p0 = points[(size_t)currentSegment];
                const auto& p1 = points[(size_t)currentSegment + 1];
                float range = p1.dstBin - p0.dstBin;
                float frac = (range > 0.0001f) ? (outBin - p0.dstBin) / range : 0.0f;
                float srcVal = p0.srcBin + frac * (p1.srcBin - p0.srcBin);
                warpMap[(size_t)i] = std::max(0.0f, std::min(srcVal, (float)(numBins - 1)));
            }
        }
    }

    void process(const std::vector<float>& srcEnvelope, std::vector<float>& dstEnvelope)
    {
        jassert(srcEnvelope.size() == dstEnvelope.size());
        jassert(warpMap.size() == srcEnvelope.size());

        size_t size = srcEnvelope.size();
        size_t maxIdx = size - 1;

        for (size_t i = 0; i < size; ++i)
        {
            float srcIdx = warpMap[i];
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
