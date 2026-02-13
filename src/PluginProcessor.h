/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

#include "PitchDetector.h"

// * Long / real silence ( silence > minNoteLen) detected
// * Short silence / but note still playing (silence < minNoteLen) detected
// * Started a note after silence
// * Started a note after another note
// * Ended a note
// * Note played for long enough to trigger note on
enum class InstrumentState{
  LongSilence, 
  ShortSilence,
  NoteAfterSilence,
  NoteAfterOtherNote, 
  NoteHeldNotLongEnoughYet,
  NoteLongEnoughSendNoteOn,
  NoteHeldNoteOnSent,
  NoteEndedNowSilentSendNoteOff, 
};
//==============================================================================
/**
*/
class TestPluginAudioProcessor  : public juce::AudioProcessor
                            #if JucePlugin_Enable_ARA
                             , public juce::AudioProcessorARAExtension
                            #endif
{
public:
    //==============================================================================
    TestPluginAudioProcessor();
    ~TestPluginAudioProcessor() override;

    //==============================================================================
    using juce::AudioProcessor::processBlock;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() { return parameters; }

    struct NoteEvent
    {
        int note = 0;
        float velocity = 0.0f;
        bool noteOn = false;
        double timeSeconds = 0.0;
    };

    int pullNoteEvents(NoteEvent* dest, int maxToRead);
    float getRmsLevel() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void pushNoteEventFromAudioThread(const NoteEvent& event);

    juce::AudioProcessorValueTreeState parameters;

    PitchDetector pitchDetector;
    PitchDetector::Settings pitchSettings;
    PitchDetector::Settings lastPitchSettings;
    std::vector<float> monoBuffer;
    std::vector<PitchDetector::Detection> detections;

    juce::AbstractFifo noteFifo { 1024 };
    std::vector<NoteEvent> noteEventBuffer { 1024 };

    std::atomic<float> rmsLevel { 0.0f };
    int64 sampleCounter = 0;
    double lastSampleRate = 44100.0;
    int lastBlockSize = 0;
    int64 logCounter = 0;

    int currentActiveNote = -1;

    int64 currentActiveNoteStartSample = -1;


    int64 lastDetectionSample = -1;
    int64 silenceForNSamples = -1;
    
    
    bool hasPendingNote = false;
    int pendingNote = -1;
    juce::uint8 pendingVelocity = 0;
    // int64 pendingDetectionSample = -1;

    std::array<bool, 127> noteOnRequested {};
    std::array<bool, 127> noteOffNeeded {};


    int lastNoteOff = -1;
    int64 lastNoteOffSeconds = -1;
    std::array<long, 127> noteOnTimestamps {};
    std::array<long, 127> noteOffTimestamps {};
    
    
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TestPluginAudioProcessor)
};

