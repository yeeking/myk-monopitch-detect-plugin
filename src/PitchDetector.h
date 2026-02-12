#pragma once

#include <vector>

class PitchDetector
{
public:
    struct Settings
    {
        float initFreq = 440.0f;
        float minFreq = 60.0f;
        float maxFreq = 2000.0f;
        float execFreq = 100.0f;
        int maxBinsPerOctave = 16;
        int medianSize = 1;
        float ampThreshold = 0.02f;
        float peakThreshold = 0.5f;
        int downSample = 1;
        bool clarity = false;
    };

    struct Detection
    {
        float freq = 0.0f;
        float clarity = 0.0f;
        int sampleOffset = 0;
    };

    void prepare(double sampleRate, int samplesPerBlock, const Settings& settings);
    void reset();
    void processBlock(const float* input, int numSamples, std::vector<Detection>& detections);

private:
    static constexpr int kMaxMedianSize = 31;

    static int log2ceil(int x);
    static float insertMedian(float* values, int* ages, int size, float value);
    static void initMedian(float* values, int* ages, int size, float value);

    bool analyse(float& outFreq, float& outClarity);

    std::vector<float> buffer;
    std::vector<float> medianValues;
    std::vector<int> medianAges;

    float freq = 440.0f;
    float minFreq = 60.0f;
    float maxFreq = 2000.0f;
    float hasFreq = 0.0f;
    float sampleRate = 44100.0f;
    float analysisRate = 44100.0f;
    float ampThreshold = 0.02f;
    float peakThreshold = 0.5f;

    int minPeriod = 0;
    int maxPeriod = 0;
    int execPeriod = 0;
    int index = 0;
    int size = 0;
    int downSample = 1;
    int maxLog2Bins = 0;
    int medianSize = 1;
    int downSampleCounter = 0;

    bool getClarity = false;
};
