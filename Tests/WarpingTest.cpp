#include "../Source/DSP/FormantWarper.h"
#include <vector>
#include <iostream>

// Simple test harness
int main()
{
    dsp::FormantWarper warper;
    int numBins = 100;

    // Test Case 1: Identity from empty points.
    std::vector<dsp::WarpingPoint> points;

    warper.calculateWarpMap(numBins, points);
    auto map = warper.getWarpMap();

    bool pass = true;
    for(int i=0; i<numBins; ++i)
    {
        if (std::abs(map[i] - (float)i) > 0.001f)
        {
            pass = false;
            std::cout << "Fail at " << i << ": expected " << i << " got " << map[i] << "\n";
            break;
        }
    }

    if (pass) std::cout << "Test 1 (Empty Identity) Passed\n";
    else return 1;

    // Test Case 2: Explicit identity anchors.
    points.clear();
    points.push_back({0.0f, 0.0f});
    points.push_back({(float)numBins-1, (float)numBins-1});

    warper.calculateWarpMap(numBins, points);
    map = warper.getWarpMap();

    pass = true;
    for(int i=0; i<numBins; ++i)
    {
        if (std::abs(map[i] - (float)i) > 0.001f)
        {
            pass = false;
            std::cout << "Fail at " << i << ": expected " << i << " got " << map[i] << "\n";
            break;
        }
    }

    if (pass) std::cout << "Test 2 (Explicit Identity) Passed\n";
    else return 1;

    // Test Case 3: Shift specific point
    // Shift input bin 50 to output bin 70.
    // 0->0
    // 50->70
    // 99->99

    points.clear();
    points.push_back({0.0f, 0.0f});
    points.push_back({50.0f, 70.0f});
    points.push_back({99.0f, 99.0f});

    warper.calculateWarpMap(numBins, points);
    map = warper.getWarpMap();

    pass = true;

    // Check output bin 70. Should map to 50.
    if (std::abs(map[70] - 50.0f) > 0.1f)
    {
        pass = false;
        std::cout << "Test 3 Fail: map[70] expected 50.0, got " << map[70] << "\n";
    }

    // Check output bin 35 (halfway to 70). Should map to 25 (halfway to 50).
    if (std::abs(map[35] - 25.0f) > 0.1f)
    {
        pass = false;
        std::cout << "Test 3 Fail: map[35] expected 25.0, got " << map[35] << "\n";
    }

    if (pass) std::cout << "Test 3 (Piecewise) Passed\n";
    else return 1;

    return 0;
}
