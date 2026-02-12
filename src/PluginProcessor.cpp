/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
    constexpr const char* paramInitFreq = "initFreq";
    constexpr const char* paramMinFreq = "minFreq";
    constexpr const char* paramMaxFreq = "maxFreq";
    constexpr const char* paramExecFreq = "execFreq";
    constexpr const char* paramMaxBins = "maxBins";
    constexpr const char* paramMedian = "median";
    constexpr const char* paramAmpThresh = "ampThresh";
    constexpr const char* paramPeakThresh = "peakThresh";
    constexpr const char* paramDownSample = "downSample";
    constexpr const char* paramClarity = "clarity";
    constexpr const char* paramNoteLengthMs = "noteLengthMs";
    constexpr const char* paramDecayTime = "decayTime";
    constexpr const char* paramMidiThru = "midiThru";
    constexpr const char* paramFreeze = "freeze";

    PitchDetector::Settings readSettings(juce::AudioProcessorValueTreeState& params)
    {
        PitchDetector::Settings settings;
        settings.initFreq = params.getRawParameterValue(paramInitFreq)->load();
        settings.minFreq = params.getRawParameterValue(paramMinFreq)->load();
        settings.maxFreq = params.getRawParameterValue(paramMaxFreq)->load();
        settings.execFreq = params.getRawParameterValue(paramExecFreq)->load();
        settings.maxBinsPerOctave = static_cast<int>(params.getRawParameterValue(paramMaxBins)->load());
        settings.medianSize = static_cast<int>(params.getRawParameterValue(paramMedian)->load());
        settings.ampThreshold = params.getRawParameterValue(paramAmpThresh)->load();
        settings.peakThreshold = params.getRawParameterValue(paramPeakThresh)->load();
        settings.downSample = static_cast<int>(params.getRawParameterValue(paramDownSample)->load());
        settings.clarity = params.getRawParameterValue(paramClarity)->load() > 0.5f;
        return settings;
    }

    bool nearlyEqual(float a, float b, float epsilon = 1.0e-4f)
    {
        return std::fabs(a - b) <= epsilon;
    }

    bool settingsEqual(const PitchDetector::Settings& a, const PitchDetector::Settings& b)
    {
        return nearlyEqual(a.initFreq, b.initFreq)
            && nearlyEqual(a.minFreq, b.minFreq)
            && nearlyEqual(a.maxFreq, b.maxFreq)
            && nearlyEqual(a.execFreq, b.execFreq)
            && a.maxBinsPerOctave == b.maxBinsPerOctave
            && a.medianSize == b.medianSize
            && nearlyEqual(a.ampThreshold, b.ampThreshold)
            && nearlyEqual(a.peakThreshold, b.peakThreshold)
            && a.downSample == b.downSample
            && a.clarity == b.clarity;
    }
}

//==============================================================================
TestPluginAudioProcessor::TestPluginAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
    , parameters(*this, nullptr, "PARAMS", createParameterLayout())
{
}

TestPluginAudioProcessor::~TestPluginAudioProcessor()
{
}

//==============================================================================
const juce::String TestPluginAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool TestPluginAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool TestPluginAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool TestPluginAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double TestPluginAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int TestPluginAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int TestPluginAudioProcessor::getCurrentProgram()
{
    return 0;
}

void TestPluginAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused(index);
}

const juce::String TestPluginAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused(index);
    return {};
}

void TestPluginAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

//==============================================================================
void TestPluginAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    lastSampleRate = sampleRate;
    lastBlockSize = samplesPerBlock;
    pitchSettings = readSettings(parameters);
    pitchDetector.prepare(sampleRate, samplesPerBlock, pitchSettings);
    lastPitchSettings = pitchSettings;
    monoBuffer.assign(static_cast<size_t>(samplesPerBlock), 0.0f);
    detections.reserve(128);
    sampleCounter = 0;
    logCounter = 0;
    currentActiveNote = -1;
    lastDetectionSample = -1;
}

void TestPluginAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool TestPluginAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void TestPluginAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0 || totalNumInputChannels <= 0)
        return;

    pitchSettings = readSettings(parameters);

    const bool midiThru = parameters.getRawParameterValue(paramMidiThru)->load() > 0.5f;
    const bool freeze = parameters.getRawParameterValue(paramFreeze)->load() > 0.5f;

    const float decayTimeSec = parameters.getRawParameterValue(paramDecayTime)->load();
    const int64 decaySamples = static_cast<int64>(std::max(0.0f, decayTimeSec) * static_cast<float>(lastSampleRate));

    if (!settingsEqual(pitchSettings, lastPitchSettings))
    {
        pitchDetector.prepare(lastSampleRate, lastBlockSize, pitchSettings);
        lastPitchSettings = pitchSettings;
    }

    if (!midiThru)
        midiMessages.clear();

    if (freeze)
    {
        sampleCounter += numSamples;
        return;
    }

    if (static_cast<int>(monoBuffer.size()) < numSamples)
        monoBuffer.assign(static_cast<size_t>(numSamples), 0.0f);

    const float channelScale = 1.0f / static_cast<float>(totalNumInputChannels);
    float rmsSum = 0.0f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float mixed = 0.0f;
        for (int channel = 0; channel < totalNumInputChannels; ++channel)
            mixed += buffer.getReadPointer(channel)[sample];

        mixed *= channelScale;
        monoBuffer[static_cast<size_t>(sample)] = mixed;
        rmsSum += mixed * mixed;
    }

    const float rms = std::sqrt(rmsSum / static_cast<float>(numSamples));
    rmsLevel.store(rms, std::memory_order_relaxed);

    const int64 blockStartSample = sampleCounter;
    const int64 blockEndSample = sampleCounter + numSamples;

    pitchDetector.processBlock(monoBuffer.data(), numSamples, detections);
    bool detectedThisBlock = false;
    for (const auto& detection : detections)
    {
        if (detection.freq <= 0.0f)
            continue;

        detectedThisBlock = true;
        lastDetectionSample = blockStartSample + detection.sampleOffset;

       #if JUCE_DEBUG
        const int64 logInterval = static_cast<int64>(lastSampleRate * 0.5);
        logCounter += numSamples;
        if (logCounter >= logInterval)
        {
            logCounter = 0;
            juce::Logger::writeToLog("Detected pitch: " + juce::String(detection.freq, 2) + " Hz");
        }
       #endif

        const double midiNoteDouble = 69.0 + 12.0 * std::log2(detection.freq / 440.0);
        const int note = juce::jlimit(0, 127, static_cast<int>(std::lround(midiNoteDouble)));
        const float velocityFloat = juce::jlimit(0.1f, 1.0f, detection.clarity);
        const int velocity = juce::jlimit(1, 127, static_cast<int>(std::lround(velocityFloat * 127.0f)));

        const int offset = detection.sampleOffset;

        if (note != currentActiveNote)
        {
            if (currentActiveNote >= 0)
            {
                midiMessages.addEvent(juce::MidiMessage::noteOff(1, currentActiveNote), offset);
                NoteEvent eventOff;
                eventOff.note = currentActiveNote;
                eventOff.velocity = velocityFloat;
                eventOff.noteOn = false;
                eventOff.timeSeconds = static_cast<double>(blockStartSample + offset) / getSampleRate();
                pushNoteEventFromAudioThread(eventOff);
            }

            midiMessages.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8)velocity), offset);
            NoteEvent eventOn;
            eventOn.note = note;
            eventOn.velocity = velocityFloat;
            eventOn.noteOn = true;
            eventOn.timeSeconds = static_cast<double>(blockStartSample + offset) / getSampleRate();
            pushNoteEventFromAudioThread(eventOn);

            currentActiveNote = note;
        }
    }

    if (!detectedThisBlock && currentActiveNote >= 0)
    {
        bool shouldTurnOff = decaySamples == 0;
        int offOffset = 0;

        if (decaySamples > 0 && lastDetectionSample >= 0)
        {
            const int64 offSampleTime = lastDetectionSample + decaySamples;
            if (offSampleTime <= blockStartSample)
            {
                shouldTurnOff = true;
                offOffset = 0;
            }
            else if (offSampleTime < blockEndSample)
            {
                shouldTurnOff = true;
                offOffset = static_cast<int>(offSampleTime - blockStartSample);
            }
        }

        if (shouldTurnOff)
        {
            midiMessages.addEvent(juce::MidiMessage::noteOff(1, currentActiveNote), offOffset);
            NoteEvent eventOff;
            eventOff.note = currentActiveNote;
            eventOff.velocity = 0.0f;
            eventOff.noteOn = false;
            eventOff.timeSeconds = static_cast<double>(blockStartSample + offOffset) / getSampleRate();
            pushNoteEventFromAudioThread(eventOff);
            currentActiveNote = -1;
        }
    }

    sampleCounter += numSamples;
}

//==============================================================================
bool TestPluginAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* TestPluginAudioProcessor::createEditor()
{
    return new TestPluginAudioProcessorEditor (*this);
}

//==============================================================================
void TestPluginAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
    juce::MemoryOutputStream stream(destData, true);
    parameters.state.writeToStream(stream);
}

void TestPluginAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    auto tree = juce::ValueTree::readFromData(data, static_cast<size_t>(sizeInBytes));
    if (tree.isValid())
        parameters.replaceState(tree);
}

int TestPluginAudioProcessor::pullNoteEvents(NoteEvent* dest, int maxToRead)
{
    if (dest == nullptr || maxToRead <= 0)
        return 0;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    noteFifo.prepareToRead(maxToRead, start1, size1, start2, size2);

    int copied = 0;
    if (size1 > 0)
    {
        std::copy(noteEventBuffer.begin() + start1, noteEventBuffer.begin() + start1 + size1, dest);
        copied += size1;
    }
    if (size2 > 0)
    {
        std::copy(noteEventBuffer.begin() + start2, noteEventBuffer.begin() + start2 + size2, dest + copied);
        copied += size2;
    }

    noteFifo.finishedRead(copied);
    return copied;
}

float TestPluginAudioProcessor::getRmsLevel() const
{
    return rmsLevel.load(std::memory_order_relaxed);
}

void TestPluginAudioProcessor::pushNoteEventFromAudioThread(const NoteEvent& event)
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    noteFifo.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 > 0)
    {
        noteEventBuffer[static_cast<size_t>(start1)] = event;
        noteFifo.finishedWrite(1);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout TestPluginAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramInitFreq, "Init Freq", juce::NormalisableRange<float>(20.0f, 2000.0f, 0.01f, 0.5f), 440.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramMinFreq, "Min Freq", juce::NormalisableRange<float>(20.0f, 1000.0f, 0.01f, 0.5f), 60.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramMaxFreq, "Max Freq", juce::NormalisableRange<float>(100.0f, 8000.0f, 0.01f, 0.5f), 2000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramExecFreq, "Exec Freq", juce::NormalisableRange<float>(10.0f, 500.0f, 0.01f, 0.5f), 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(paramMaxBins, "Max Bins/Oct", 1, 32, 16));
    params.push_back(std::make_unique<juce::AudioParameterInt>(paramMedian, "Median", 1, 31, 7));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramAmpThresh, "Amp Thresh", juce::NormalisableRange<float>(0.0f, 0.2f, 0.0001f), 0.02f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramPeakThresh, "Peak Thresh", juce::NormalisableRange<float>(0.1f, 1.0f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(paramDownSample, "Downsample", 1, 32, 1));
    params.push_back(std::make_unique<juce::AudioParameterBool>(paramClarity, "Clarity", true));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramNoteLengthMs, "Note Length (ms)", juce::NormalisableRange<float>(10.0f, 500.0f, 1.0f), 60.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramDecayTime, "Decay Time (s)", juce::NormalisableRange<float>(0.0f, 0.5f, 0.001f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(paramMidiThru, "MIDI Thru", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(paramFreeze, "Freeze", false));

    return { params.begin(), params.end() };
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TestPluginAudioProcessor();
}
