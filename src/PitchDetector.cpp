#include "PitchDetector.h"

#include <algorithm>
#include <cmath>
#include <JuceHeader.h>

int PitchDetector::log2ceil(int x)
{
    int v = 0;
    int p = 1;
    while (p < x)
    {
        p <<= 1;
        ++v;
    }
    return v;
}

float PitchDetector::insertMedian(float* values, int* ages, int size, float value)
{
    int pos = -1;
    const int last = size - 1;

    for (int i = 0; i < size; ++i)
    {
        if (ages[i] == last)
            pos = i;
        else
            ages[i]++;
    }

    while (pos != 0 && value < values[pos - 1])
    {
        values[pos] = values[pos - 1];
        ages[pos] = ages[pos - 1];
        pos--;
    }

    while (pos != last && value > values[pos + 1])
    {
        values[pos] = values[pos + 1];
        ages[pos] = ages[pos + 1];
        pos++;
    }

    values[pos] = value;
    ages[pos] = 0;
    return values[size >> 1];
}

void PitchDetector::initMedian(float* values, int* ages, int size, float value)
{
    for (int i = 0; i < size; ++i)
    {
        values[i] = value;
        ages[i] = i;
    }
}

void PitchDetector::prepare(double sr, int /*samplesPerBlock*/, const Settings& settings)
{
    sampleRate = static_cast<float>(sr);
    downSample = std::max(1, settings.downSample);
    analysisRate = sampleRate / static_cast<float>(downSample);

    minFreq = settings.minFreq;
    maxFreq = settings.maxFreq;
    freq = settings.initFreq;
    ampThreshold = settings.ampThreshold;
    peakThreshold = settings.peakThreshold;
    getClarity = settings.clarity;

    const float execFreq = std::clamp(settings.execFreq, minFreq, maxFreq);
    maxLog2Bins = log2ceil(std::max(1, settings.maxBinsPerOctave));

    medianSize = std::clamp(settings.medianSize, 1, kMaxMedianSize);
    medianValues.assign(static_cast<size_t>(medianSize), freq);
    medianAges.assign(static_cast<size_t>(medianSize), 0);
    initMedian(medianValues.data(), medianAges.data(), medianSize, freq);

    minPeriod = static_cast<int>(analysisRate / std::max(1.0f, maxFreq));
    maxPeriod = static_cast<int>(analysisRate / std::max(1.0f, minFreq));

    execPeriod = static_cast<int>(analysisRate / std::max(1.0f, execFreq));
    execPeriod = std::max(execPeriod, 1);

    size = std::max(maxPeriod << 1, execPeriod);
    buffer.assign(static_cast<size_t>(size), 0.0f);

    index = 0;
    downSampleCounter = 0;
    hasFreq = 0.0f;
}

void PitchDetector::reset()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    initMedian(medianValues.data(), medianAges.data(), medianSize, freq);
    index = 0;
    downSampleCounter = 0;
    hasFreq = 0.0f;
}

void PitchDetector::processBlock(const float* input, int numSamples, std::vector<Detection>& detections)
{
    detections.clear();

    if (buffer.empty() || numSamples <= 0)
        return;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        if (downSampleCounter == 0)
        {
            buffer[static_cast<size_t>(index++)] = input[sample];

            if (index >= size)
            {
                float outFreq = freq;
                float outAmp = amp; 
                float outClarity = hasFreq;
                const bool gotPitch = analyse(outFreq, outAmp, outClarity);
                freq = outFreq;
                amp = outAmp; 
                hasFreq = outClarity;

                if (gotPitch && outClarity > 0.0f)
                {
                    Detection detection;
                    detection.freq = outFreq;
                    detection.amp = outAmp;
                    
                    detection.clarity = outClarity;
                    detection.sampleOffset = sample;
                    detections.push_back(detection);
                }

                const int interval = size - execPeriod;
                for (int i = 0; i < interval; ++i)
                    buffer[static_cast<size_t>(i)] = buffer[static_cast<size_t>(i + execPeriod)];

                index = interval;
            }
        }

        downSampleCounter++;
        if (downSampleCounter >= downSample)
            downSampleCounter = 0;
    }
}

bool PitchDetector::analyse(float& outFreq, float& outAmp, float& outClarity)
{
    bool foundPeak = false;
    bool ampOk = false;

    if (maxPeriod <= 0 || minPeriod <= 0)
    {
        outClarity = 0.0f;
        return false;
    }

    for (int j = 0; j < maxPeriod; ++j)
    {
        if (std::fabs(buffer[static_cast<size_t>(j)]) >= ampThreshold)
        {
            ampOk = true;
            break;
        }
    }

    if (!ampOk)
    {
        outClarity = 0.0f;
        return false;
    }

    float zeroLagVal = 0.0f;
    for (int j = 0; j < maxPeriod; ++j)
        zeroLagVal += buffer[static_cast<size_t>(j)] * buffer[static_cast<size_t>(j)];

    if (zeroLagVal <= 0.0f)
    {
        outClarity = 0.0f;
        return false;
    }

    const float threshold = zeroLagVal * peakThreshold;

    int binstep = 1;
    int i = 0;

    for (i = 1; i <= maxPeriod; i += binstep)
    {
        float ampSum = 0.0f;
        for (int j = 0; j < maxPeriod; ++j)
            ampSum += buffer[static_cast<size_t>(i + j)] * buffer[static_cast<size_t>(j)];

        if (ampSum < threshold)
            break;

        const int octave = log2ceil(i);
        if (octave <= maxLog2Bins)
            binstep = 1;
        else
            binstep = 1 << (octave - maxLog2Bins);
    }

    const int startPeriod = i;
    int period = startPeriod;
    float maxSum = threshold;

    for (i = startPeriod; i <= maxPeriod; i += binstep)
    {
        if (i >= minPeriod)
        {
            float ampSum = 0.0f;
            for (int j = 0; j < maxPeriod; ++j)
                ampSum += buffer[static_cast<size_t>(i + j)] * buffer[static_cast<size_t>(j)];

            if (ampSum > threshold)
            {
                if (ampSum > maxSum)
                {
                    foundPeak = true;
                    maxSum = ampSum;
                    period = i;
                }
            }
            else if (foundPeak)
            {
                break;
            }
        }

        const int octave = log2ceil(i);
        if (octave <= maxLog2Bins)
            binstep = 1;
        else
            binstep = 1 << (octave - maxLog2Bins);
    }

    if (!foundPeak)
    {
        outClarity = 0.0f;
        return false;
    }

    float prevAmpSum = 0.0f;
    float nextAmpSum = 0.0f;

    if (period > 0)
    {
        const int idx = period - 1;
        for (int j = 0; j < maxPeriod; ++j)
            prevAmpSum += buffer[static_cast<size_t>(idx + j)] * buffer[static_cast<size_t>(j)];
    }

    if (period < maxPeriod)
    {
        const int idx = period + 1;
        for (int j = 0; j < maxPeriod; ++j)
            nextAmpSum += buffer[static_cast<size_t>(idx + j)] * buffer[static_cast<size_t>(j)];
    }

    
    while (prevAmpSum > maxSum && period > 0)
    {
        nextAmpSum = maxSum;
        maxSum = prevAmpSum;
        period--;
        const int idx = period - 1;
        prevAmpSum = 0.0f;
        for (int j = 0; j < maxPeriod; ++j)
            prevAmpSum += buffer[static_cast<size_t>(idx + j)] * buffer[static_cast<size_t>(j)];
    }

    
    while (nextAmpSum > maxSum && period < maxPeriod)
    {
        prevAmpSum = maxSum;
        maxSum = nextAmpSum;
        period++;
        const int idx = period + 1;
        nextAmpSum = 0.0f;
        for (int j = 0; j < maxPeriod; ++j)
            nextAmpSum += buffer[static_cast<size_t>(idx + j)] * buffer[static_cast<size_t>(j)];
    }

    const float beta = 0.5f * (nextAmpSum - prevAmpSum);
    const float gamma = 2.0f * maxSum - nextAmpSum - prevAmpSum;
    float fPeriod = static_cast<float>(period);
    if (std::fabs(gamma) > 1.0e-6f)
        fPeriod += (beta / gamma);

    const float tempFreq = analysisRate / fPeriod;

    if (tempFreq < minFreq || tempFreq > maxFreq)
    {
        outClarity = 0.0f;
        return false;
    }

    outFreq = tempFreq;

    if (medianSize > 1)
        outFreq = insertMedian(medianValues.data(), medianAges.data(), medianSize, outFreq);

    if (getClarity)
        outClarity = maxSum / zeroLagVal;
    else
        outClarity = 1.0f;


    // Map raw autocorrelation amplitude to a log-like curve:
    // fast rise for low input, then compression toward 1.0 at the top.
    const float rawAmp = (period > 0) ? (prevAmpSum / static_cast<float>(period)) : 0.0f;
    constexpr float ampCurve = 20.0f;
    const float safeAmp = std::max(0.0f, rawAmp);
    const float mappedAmp = std::log1p(ampCurve * safeAmp) / std::log1p(ampCurve);
    outAmp = juce::jlimit(0.0f, 1.0f, mappedAmp);
    
    
    return true;
}
