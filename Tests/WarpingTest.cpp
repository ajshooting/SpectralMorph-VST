#include "../Source/DSP/EnvelopeExtractor.h"
#include "../Source/DSP/FormantWarper.h"
#include "../Source/DSP/SpectralProcessor.h"
#include "../Source/PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
bool testWarping()
{
    dsp::FormantWarper warper;
    constexpr int numBins = 100;

    // Test Case 1: Identity from empty points.
    std::vector<dsp::WarpingPoint> points;

    warper.calculateWarpMap(numBins, points);
    auto map = warper.getWarpMap();

    bool pass = true;
    for(int i=0; i<numBins; ++i)
    {
        const auto index = (size_t)i;
        if (std::abs(map[index] - (float)i) > 0.001f)
        {
            pass = false;
            std::cout << "Fail at " << i << ": expected " << i << " got " << map[index] << "\n";
            break;
        }
    }

    if (pass) std::cout << "Test 1 (Empty Identity) Passed\n";
    else return false;

    // Test Case 2: Explicit identity anchors.
    points.clear();
    points.push_back({0.0f, 0.0f});
    points.push_back({(float)numBins-1, (float)numBins-1});

    warper.calculateWarpMap(numBins, points);
    map = warper.getWarpMap();

    pass = true;
    for(int i=0; i<numBins; ++i)
    {
        const auto index = (size_t)i;
        if (std::abs(map[index] - (float)i) > 0.001f)
        {
            pass = false;
            std::cout << "Fail at " << i << ": expected " << i << " got " << map[index] << "\n";
            break;
        }
    }

    if (pass) std::cout << "Test 2 (Explicit Identity) Passed\n";
    else return false;

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
    else return false;

    return true;
}

bool testEnvelopeConstantGain()
{
    constexpr int fftSize = 1024;
    constexpr float expectedMagnitude = 4.0f;

    dsp::EnvelopeExtractor extractor;
    extractor.prepare(fftSize);

    std::vector<float> magnitude((size_t)fftSize / 2 + 1, expectedMagnitude);
    std::vector<float> envelope(magnitude.size(), 0.0f);
    extractor.process(magnitude, envelope);

    const auto [minimum, maximum] = std::minmax_element(envelope.begin(), envelope.end());
    const bool pass = std::abs(*minimum - expectedMagnitude) < 0.01f
                   && std::abs(*maximum - expectedMagnitude) < 0.01f;

    if (pass)
    {
        std::cout << "Test 4 (Envelope Constant Gain) Passed\n";
        return true;
    }

    std::cout << "Test 4 Fail: expected " << expectedMagnitude
              << ", got range [" << *minimum << ", " << *maximum << "]\n";
    return false;
}

bool testJuceFftRoundTrip()
{
    constexpr int fftOrder = 10;
    constexpr int fftSize = 1 << fftOrder;

    juce::dsp::FFT fft(fftOrder);
    std::vector<float> data((size_t)fftSize * 2, 0.0f);
    data[fftSize / 3] = 1.0f;

    fft.performRealOnlyForwardTransform(data.data());
    fft.performRealOnlyInverseTransform(data.data());

    const float reconstructed = data[fftSize / 3];
    const bool pass = std::abs(reconstructed - 1.0f) < 0.001f;
    std::cout << (pass ? "Test 5 (JUCE FFT Round Trip) Passed\n"
                       : "Test 5 Fail: JUCE FFT round trip returned ")
              << (pass ? "" : std::to_string(reconstructed) + "\n");
    return pass;
}

bool testSpectralImpulseGainAndLatency(float impulseAmplitude = 1.0f)
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 128;
    constexpr int totalSamples = 4096;
    constexpr int inputImpulseIndex = 1024;
    constexpr int expectedLatency = 1024;

    dsp::SpectralProcessor processor;
    processor.prepare({sampleRate, (juce::uint32)blockSize, 2});

    std::vector<float> leftOutput((size_t)totalSamples, 0.0f);
    std::vector<float> rightOutput((size_t)totalSamples, 0.0f);

    for (int blockStart = 0; blockStart < totalSamples; blockStart += blockSize)
    {
        juce::AudioBuffer<float> buffer(2, blockSize);
        buffer.clear();

        if (inputImpulseIndex >= blockStart && inputImpulseIndex < blockStart + blockSize)
            buffer.setSample(0, inputImpulseIndex - blockStart, impulseAmplitude);

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        processor.process(context);

        std::copy(buffer.getReadPointer(0),
                  buffer.getReadPointer(0) + blockSize,
                  leftOutput.begin() + blockStart);
        std::copy(buffer.getReadPointer(1),
                  buffer.getReadPointer(1) + blockSize,
                  rightOutput.begin() + blockStart);
    }

    const auto peak = std::max_element(leftOutput.begin(), leftOutput.end(), [](float a, float b)
                                       { return std::abs(a) < std::abs(b); });
    const int peakIndex = (int)std::distance(leftOutput.begin(), peak);
    const float peakMagnitude = std::abs(*peak) / impulseAmplitude;
    const bool allFinite = std::all_of(leftOutput.begin(), leftOutput.end(), [](float sample)
                                       { return std::isfinite(sample); })
                        && std::all_of(rightOutput.begin(), rightOutput.end(), [](float sample)
                                       { return std::isfinite(sample); });
    const float rightPeak = std::abs(*std::max_element(
        rightOutput.begin(), rightOutput.end(), [](float a, float b)
        { return std::abs(a) < std::abs(b); }));

    const bool pass = allFinite
                   && dsp::SpectralProcessor::getLatencySamples() == expectedLatency
                   && peakIndex == inputImpulseIndex + expectedLatency
                   && peakMagnitude > 0.98f
                   && peakMagnitude < 1.02f
                   && rightPeak < 1.0e-7f;

    if (pass)
    {
        std::cout << "Test 6 (Spectral Impulse Gain/Latency, input="
                  << impulseAmplitude << ") Passed\n";
        return true;
    }

    std::cout << "Test 6 Fail: expected peak near 1.0 at "
              << inputImpulseIndex + expectedLatency
              << ", got " << peakMagnitude << " at " << peakIndex
              << ", right peak=" << rightPeak
              << ", finite=" << allFinite << "\n";
    return false;
}

bool testOnlyDetectedFormantsAreWarped(size_t expectedCount, int impulseSpacing)
{
    constexpr int fftSize = 1024;
    constexpr double sampleRate = 48000.0;
    constexpr float hzPerBin = (float)sampleRate / (float)fftSize;

    // A single impulse has a flat envelope. Adding a second impulse L samples
    // away gives envelope maxima at multiples of sampleRate / L. L=8 and L=12
    // therefore give exactly one and two peaks in the 150-9000 Hz search band.
    // Compensate the analysis window so the final frame has that exact spectrum.
    std::vector<float> hann((size_t)fftSize, 1.0f);
    juce::dsp::WindowingFunction<float> window(
        fftSize, juce::dsp::WindowingFunction<float>::hann, false);
    window.multiplyWithWindowingTable(hann.data(), fftSize);

    juce::AudioBuffer<float> source(1, fftSize);
    source.clear();
    source.setSample(0, fftSize / 2, 1.0f / hann[(size_t)fftSize / 2]);
    if (impulseSpacing > 0)
    {
        const int secondIndex = fftSize / 2 - impulseSpacing;
        source.setSample(0, secondIndex, 0.25f / hann[(size_t)secondIndex]);
    }

    std::array<float, dsp::SpectralProcessor::numFormants> targets{};
    for (size_t i = 0; i < targets.size(); ++i)
        targets[i] = 750.0f * (float)(i + 1);

    struct Snapshot
    {
        std::vector<float> spectrum;
        std::vector<float> envelope;
        float f1 = 0.0f;
        float f2 = 0.0f;
    };

    const auto process = [&source, &targets]
    {
        dsp::SpectralProcessor processor;
        processor.prepare({sampleRate, (juce::uint32)fftSize, 1});
        processor.setTargetFormantsHz(targets);
        juce::AudioBuffer<float> buffer;
        buffer.makeCopyOf(source);
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        processor.process(context);

        Snapshot result;
        processor.getLatestVisualizationData(
            result.spectrum, result.envelope, result.f1, result.f2);
        return result;
    };

    const auto baseline = process();
    for (size_t i = expectedCount; i < targets.size(); ++i)
        targets[i] = 10000.0f + 100.0f * (float)i;
    const auto changedUnusedTargets = process();

    if (baseline.envelope.size() != (size_t)fftSize / 2 + 1
        || baseline.envelope.size() != baseline.spectrum.size()
        || baseline.envelope.size() != changedUnusedTargets.envelope.size())
    {
        std::cout << "Detected-peak warping: unexpected visualization size\n";
        return false;
    }

    bool pass = std::abs(baseline.f1 - (expectedCount > 0 ? 750.0f / hzPerBin : 0.0f)) < 1.0e-5f
             && std::abs(baseline.f2 - (expectedCount > 1 ? 1500.0f / hzPerBin : 0.0f)) < 1.0e-5f
             && std::abs(baseline.f1 - changedUnusedTargets.f1) < 1.0e-5f
             && std::abs(baseline.f2 - changedUnusedTargets.f2) < 1.0e-5f;

    float unusedTargetError = 0.0f;
    for (size_t i = 0; i < baseline.envelope.size(); ++i)
    {
        pass = pass && std::isfinite(baseline.envelope[i])
                    && std::isfinite(changedUnusedTargets.envelope[i]);
        unusedTargetError = std::max(unusedTargetError,
            std::abs(baseline.envelope[i] - changedUnusedTargets.envelope[i]));
        if (expectedCount == 0)
            pass = pass && std::abs(baseline.envelope[i] - baseline.spectrum[i]) < 1.0e-7f;
    }
    pass = pass && unusedTargetError < 1.0e-7f;

    // A detected target must still move a real envelope peak, not merely leave
    // every frame untouched to satisfy the unused-target comparison above.
    if (expectedCount > 0)
    {
        targets[0] = 1125.0f;
        const auto movedFirstTarget = process();
        const size_t originalTargetBin = (size_t)(750.0f / hzPerBin);
        const size_t movedTargetBin = (size_t)(1125.0f / hzPerBin);
        pass = pass
            && movedFirstTarget.envelope.size() == baseline.envelope.size()
            && std::abs(movedFirstTarget.f1 - (float)movedTargetBin) < 1.0e-5f
            && std::abs(movedFirstTarget.envelope[movedTargetBin]
                        - baseline.envelope[originalTargetBin]) < 1.0e-7f
            && movedFirstTarget.envelope[movedTargetBin]
                 > movedFirstTarget.envelope[movedTargetBin - 1]
            && movedFirstTarget.envelope[movedTargetBin]
                 > movedFirstTarget.envelope[movedTargetBin + 1];
    }

    std::cout << "Detected-peak warping (" << expectedCount << " peaks): "
              << (pass ? "Passed" : "FAILED")
              << ", unused-target error=" << unusedTargetError
              << ", markers=" << baseline.f1 << ", " << baseline.f2 << "\n";
    return pass;
}

bool testSilentReferenceIsRejected()
{
    dsp::SpectralProcessor processor;
    juce::AudioBuffer<float> silence(1, 4096);
    silence.clear();

    std::array<float, dsp::SpectralProcessor::numFormants> estimated{};
    const bool pass = !processor.estimateFormantsFromBuffer(silence, 48000.0, estimated);

    std::cout << (pass ? "Test 7 (Silent Reference Rejected) Passed\n"
                       : "Test 7 Fail: silent audio was accepted as a reference\n");
    return pass;
}

bool testTransientReferenceIsRejected()
{
    juce::AudioBuffer<float> transient(1, 48000);
    transient.clear();
    transient.setSample(0, 24000, 1.0f);

    dsp::SpectralProcessor processor;
    std::array<float, dsp::SpectralProcessor::numFormants> estimated{};
    size_t detectedCount = 99;
    const bool pass = !processor.estimateFormantsFromBuffer(
                          transient, 48000.0, estimated, &detectedCount)
                   && detectedCount == 0;

    std::cout << (pass ? "Test 8 (Transient Reference Rejected) Passed\n"
                       : "Test 8 Fail: an isolated transient was accepted as a reference\n");
    return pass;
}

bool testSustainedReferenceIsAccepted()
{
    constexpr double sampleRate = 48000.0;
    constexpr int numSamples = 48000;
    constexpr double fundamentalHz = 100.0;

    juce::AudioBuffer<float> voiced(1, numSamples);
    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        const double time = (double)sampleIndex / sampleRate;
        double sample = 0.0;

        for (int harmonic = 1; harmonic <= 90; ++harmonic)
        {
            const double frequency = fundamentalHz * (double)harmonic;
            const auto resonance = [frequency](double centre, double width)
            {
                const double distance = (frequency - centre) / width;
                return std::exp(-0.5 * distance * distance);
            };

            const double amplitude = 0.01
                                   + resonance(500.0, 120.0)
                                   + 0.85 * resonance(1500.0, 170.0)
                                   + 0.65 * resonance(2600.0, 230.0);
            sample += amplitude
                    * std::sin(juce::MathConstants<double>::twoPi * frequency * time
                               + 0.37 * (double)harmonic);
        }

        voiced.setSample(0, sampleIndex, (float)(sample * 0.025));
    }

    dsp::SpectralProcessor processor;
    std::array<float, dsp::SpectralProcessor::numFormants> estimated{};
    size_t detectedCount = 0;
    const bool accepted = processor.estimateFormantsFromBuffer(
        voiced, sampleRate, estimated, &detectedCount);
    const bool finiteAndAscending = std::all_of(
        estimated.begin(),
        estimated.begin() + (std::ptrdiff_t)detectedCount,
        [](float value)
        {
            return std::isfinite(value);
        })
        && std::is_sorted(
            estimated.begin(),
            estimated.begin() + (std::ptrdiff_t)detectedCount);
    const bool pass = accepted && detectedCount >= 3 && finiteAndAscending;

    std::cout << (pass ? "Test 9 (Sustained Reference Accepted) Passed\n"
                       : "Test 9 Fail: a sustained formant-shaped source was rejected\n");
    return pass;
}

bool testLowSampleRateStaysFinite()
{
    constexpr double sampleRate = 16000.0;
    constexpr int blockSize = 64;
    constexpr int totalSamples = 4096;

    dsp::SpectralProcessor processor;
    processor.prepare({sampleRate, (juce::uint32)blockSize, 1});

    std::array<float, dsp::SpectralProcessor::numFormants> targets{};
    targets.fill(12000.0f);
    processor.setTargetFormantsHz(targets);

    bool allFinite = true;
    float maximumMagnitude = 0.0f;
    int sampleIndex = 0;

    for (int blockStart = 0; blockStart < totalSamples; blockStart += blockSize)
    {
        juce::AudioBuffer<float> buffer(1, blockSize);
        for (int i = 0; i < blockSize; ++i, ++sampleIndex)
            buffer.setSample(0, i, (float)(0.1 * std::sin(juce::MathConstants<double>::twoPi
                                                          * 220.0 * (double)sampleIndex / sampleRate)));

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        processor.process(context);

        for (int i = 0; i < blockSize; ++i)
        {
            const float sample = buffer.getSample(0, i);
            allFinite = allFinite && std::isfinite(sample);
            maximumMagnitude = std::max(maximumMagnitude, std::abs(sample));
        }
    }

    const bool pass = allFinite && maximumMagnitude < 10.0f;
    std::cout << (pass ? "Test 10 (Low Sample Rate Safety) Passed\n"
                       : "Test 10 Fail: low sample rate output became invalid\n");
    return pass;
}

bool testProcessorBypassAndDryMixStayAligned()
{
    constexpr int blockSize = 64;
    constexpr int latency = dsp::SpectralProcessor::getLatencySamples();
    constexpr int totalSamples = latency + blockSize * 4;

    SpectralFormantMorpherAudioProcessor processor;
    if (auto *mix = processor.getAPVTS().getParameter("MIX"))
        mix->setValueNotifyingHost(mix->convertTo0to1(0.0f));
    processor.prepareToPlay(48000.0, blockSize);

    std::vector<float> input((size_t)totalSamples, 0.0f);
    input[0] = 0.75f;
    input[(size_t)blockSize] = -0.5f;

    std::vector<float> leftOutput((size_t)totalSamples, 0.0f);
    std::vector<float> rightOutput((size_t)totalSamples, 0.0f);
    juce::MidiBuffer midi;
    for (int blockStart = 0; blockStart < totalSamples; blockStart += blockSize)
    {
        juce::AudioBuffer<float> buffer(2, blockSize);
        buffer.clear();
        std::copy(input.begin() + blockStart,
                  input.begin() + blockStart + blockSize,
                  buffer.getWritePointer(0));

        if ((blockStart / blockSize) % 2 == 0)
            processor.processBlock(buffer, midi);
        else
            processor.processBlockBypassed(buffer, midi);

        std::copy_n(buffer.getReadPointer(0),
                    blockSize,
                    leftOutput.begin() + blockStart);
        std::copy_n(buffer.getReadPointer(1),
                    blockSize,
                    rightOutput.begin() + blockStart);
    }

    processor.releaseResources();

    bool pass = processor.getLatencySamples() == latency;
    for (int sample = 0; sample < totalSamples; ++sample)
    {
        const float expected = sample >= latency ? input[(size_t)(sample - latency)] : 0.0f;
        pass = pass
            && std::abs(leftOutput[(size_t)sample] - expected) < 1.0e-6f
            && std::abs(rightOutput[(size_t)sample]) < 1.0e-7f;
    }

    std::cout << (pass ? "Test 11 (Processor Bypass/Dry Alignment) Passed\n"
                       : "Test 11 Fail: normal and bypass dry paths did not share the reported latency\n");
    return pass;
}

bool testReferenceAnalysisCanBeCancelled()
{
    juce::AudioBuffer<float> source(1, 48000);
    source.clear();
    source.setSample(0, 0, 0.5f);

    dsp::SpectralProcessor processor;
    std::array<float, dsp::SpectralProcessor::numFormants> estimated{};
    size_t detectedCount = 99;
    int cancellationChecks = 0;
    const bool accepted = processor.estimateFormantsFromBuffer(
        source,
        48000.0,
        estimated,
        &detectedCount,
        [&cancellationChecks]
        {
            return ++cancellationChecks >= 4;
        });

    const bool pass = !accepted
                   && detectedCount == 0
                   && cancellationChecks >= 4;
    std::cout << (pass ? "Test 12 (Reference Analysis Cancellation) Passed\n"
                       : "Test 12 Fail: reference analysis ignored cancellation\n");
    return pass;
}
}

// Simple test harness
int main()
{
    if (!testWarping())
        return 1;

    if (!testEnvelopeConstantGain())
        return 1;

    if (!testJuceFftRoundTrip())
        return 1;

    if (!testSpectralImpulseGainAndLatency())
        return 1;

    if (!testSilentReferenceIsRejected())
        return 1;

    if (!testTransientReferenceIsRejected())
        return 1;

    if (!testSustainedReferenceIsAccepted())
        return 1;

    if (!testLowSampleRateStaysFinite())
        return 1;

    if (!testProcessorBypassAndDryMixStayAligned())
        return 1;

    if (!testReferenceAnalysisCanBeCancelled())
        return 1;

    bool detectedPeakTestsPassed = true;
    detectedPeakTestsPassed &= testOnlyDetectedFormantsAreWarped(0, 0);
    detectedPeakTestsPassed &= testOnlyDetectedFormantsAreWarped(1, 8);
    detectedPeakTestsPassed &= testOnlyDetectedFormantsAreWarped(2, 12);
    detectedPeakTestsPassed &= testSpectralImpulseGainAndLatency(1.0e-8f);
    if (!detectedPeakTestsPassed)
        return 1;

    return 0;
}
