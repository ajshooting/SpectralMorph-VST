#include "../Source/DSP/FormantWarper.h"
#include <vector>
#include <iostream>

// Simple test harness
int main()
{
    dsp::FormantWarper warper;
    int numBins = 100;

    // Test Case 1: No Shift
    warper.calculateWarpMap(numBins, 1.0f);
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

    if (pass) std::cout << "Test 1 (Identity) Passed\n";
    else return 1;

    // Test Case 2: Shift Up (Stretch)
    // Shift factor 2.0. in_bin = out_bin * 0.5.
    warper.calculateWarpMap(numBins, 2.0f);
    map = warper.getWarpMap();

    pass = true;
    // index 50 (out) should map to 25 (in)
    if (std::abs(map[50] - 25.0f) > 0.001f)
    {
        pass = false;
        std::cout << "Test 2 Fail: map[50] expected 25.0, got " << map[50] << "\n";
    }

    if (pass) std::cout << "Test 2 (Stretch) Passed\n";
    else return 1;

    return 0;
}
