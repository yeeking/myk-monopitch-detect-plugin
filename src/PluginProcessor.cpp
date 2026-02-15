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
    constexpr const char* paramAmpScale = "ampScale";
    constexpr const char* paramMinVelocity = "minVelocity";
    constexpr const char* paramDelay = "delay";
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
    hasPendingNote = false;
    pendingNote = -1;
    pendingVelocity = 0;
    // pendingDetectionSample = -1;
    noteOnTimestamps.fill(0);
    noteOffTimestamps.fill(0);
    noteDetected.fill(false);
    noteOffNeeded.fill(false);

    
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

    const float decayTimeSec = parameters.getRawParameterValue(paramDecayTime)->load();
    const float maxNoteLengthSec = parameters.getRawParameterValue(paramNoteLengthMs)->load();
    const float ampScale = parameters.getRawParameterValue(paramAmpScale)->load();
    const int minVelocityParam = static_cast<int>(parameters.getRawParameterValue(paramMinVelocity)->load());
    const float minAllowedNoteLenSecs = parameters.getRawParameterValue(paramDelay)->load();
    const int64 decaySamples = static_cast<int64>(std::max(0.0f, decayTimeSec) * static_cast<float>(lastSampleRate));
    const int64 maxNoteLengthSamples = static_cast<int64>(std::max(0.0f, maxNoteLengthSec) * static_cast<float>(lastSampleRate));
    const int64 noteDelaySamples = static_cast<int64>(std::max(0.0f, minAllowedNoteLenSecs) * static_cast<float>(lastSampleRate));

    if (!settingsEqual(pitchSettings, lastPitchSettings))
    {
        pitchDetector.prepare(lastSampleRate, lastBlockSize, pitchSettings);
        lastPitchSettings = pitchSettings;
    }

    if (!midiThru)
        midiMessages.clear();

    if (static_cast<int>(monoBuffer.size()) < numSamples)
        monoBuffer.assign(static_cast<size_t>(numSamples), 0.0f);

    const float channelScale = 1.0f / static_cast<float>(totalNumInputChannels);
    float rmsSum = 0.0f;

    constexpr int maxCachedInputChannels = 64;
    std::array<const float*, maxCachedInputChannels> readPointers {};
    const int numCachedInputChannels = juce::jmin(totalNumInputChannels, maxCachedInputChannels);
    jassert(totalNumInputChannels <= maxCachedInputChannels);
    for (int channel = 0; channel < numCachedInputChannels; ++channel)
        readPointers[static_cast<size_t>(channel)] = buffer.getReadPointer(channel);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float mixed = 0.0f;
        for (int channel = 0; channel < numCachedInputChannels; ++channel)
            mixed += readPointers[static_cast<size_t>(channel)][sample];

        mixed *= channelScale;
        mixed *= ampScale; 
        monoBuffer[static_cast<size_t>(sample)] = mixed;
        rmsSum += mixed * mixed;
    }

    const float rms = std::sqrt(rmsSum / static_cast<float>(numSamples));
    rmsLevel.store(rms, std::memory_order_relaxed);
    

    const int64 blockStartSample = sampleCounter;
    const int64 blockEndSample = sampleCounter + numSamples;

    pitchDetector.processBlock(monoBuffer.data(), numSamples, detections);

    

    bool sendNoteOn = false;
    bool sendNoteOff = false;
    int noteOnToSend = -1;
    int noteOffToSend = -1;
    int noteOffSampleOffset = 0;
    int noteOnSampleOffset = 0;
    

    int velToPlay = -1;
    int64 durToPlaySamples = -1;
    const int64 minAllowedNoteLenSamples = minAllowedNoteLenSecs * getSampleRate();
    int64 timeSinceNoteStartSamples = 0;

    InstrumentState playerState;

    // first detect silence 
    if (detections.size() == 0){// silence 
        silenceForNSamples += getBlockSize();

        // if (silenceForNSamples > minAllowedNoteLenSamples){
        //     playerState = InstrumentState::ShortSilence;        
        // } 

        // if (silenceForNSamples > minAllowedNoteLenSamples && 
        //     currentActiveNote == -1){ // just a long silence
            
        //         playerState = InstrumentState::LongSilence;        
        // }
        if (silenceForNSamples > minAllowedNoteLenSamples && // long silence
            currentActiveNote != -1){ // was playing a note
                playerState = InstrumentState::NoteEndedNowSilentSendNoteOff;  
                // reset it 
                noteDetected[currentActiveNote] = false; 
            
                noteOffToSend = currentActiveNote; 
                noteOffSampleOffset = getBlockSize() - 1;// give it some grace at the end 
                currentActiveNote = -1;
            }
    }
    if (detections.size() > 0){// notes
        silenceForNSamples = 0;// reset that one 
        PitchDetector::Detection newNoteData = detections[0];// only ever one actually
        if (currentActiveNote != -1){// we are playing 
            timeSinceNoteStartSamples = (blockStartSample + newNoteData.sampleOffset) - currentActiveNoteStartSample;
        }
        else{
            timeSinceNoteStartSamples = 0;
        }
        int newNoteMidiNum = static_cast<int>(std::lround(69.0 + 12.0 * std::log2(newNoteData.freq / 440.0)));
        // DBG("Got note " << newNoteMidiNum);
        if (currentActiveNote == -1 &&// was not playing
            newNoteMidiNum > 0){// is playing now 
                playerState = InstrumentState::NoteAfterSilence;   
                // start the timer 
                currentActiveNoteStartSample = blockStartSample + newNoteData.sampleOffset; 
        }
        if (currentActiveNote != -1 &&// was playing
            newNoteMidiNum != currentActiveNote){// note changed 
                // DBG("Note switch from " << currentActiveNote << " to " << newNoteMidiNum );
                noteDetected[currentActiveNote] = false; // reset this so i can request it next time i get this note
                playerState = InstrumentState::NoteAfterOtherNote;  
                // reset note start time for new note 
                currentActiveNoteStartSample = blockStartSample + newNoteData.sampleOffset; 
                // prepare to send a note off for the old note 
                noteOffToSend = currentActiveNote; 
                noteOffSampleOffset = newNoteData.sampleOffset;
                
        } 

        if (currentActiveNote != -1 &&// was playing
            newNoteMidiNum == currentActiveNote){// still playing same note 
                playerState = InstrumentState::NoteHeldNotLongEnoughYet;// assume its too short
                
                // is the held note held long enough to 
                // allow a note on message? 
                // DBG("held note "<< currentActiveNote << "  length so far " << timeSinceNoteStartSamples << " of " << minAllowedNoteLenSamples);

                if (timeSinceNoteStartSamples > minAllowedNoteLenSamples){
                    if (noteDetected[currentActiveNote]){// already requested a note on
                        playerState = InstrumentState::NoteHeldNoteOnSent; 
                        // DBG("I think I sent note " << currentActiveNote);
                    }
                    else{// its long enough and we've not requested a note on yet 
                        // DBG("Time to send a note on " << currentActiveNote << " set request note to true");
                        playerState = InstrumentState::NoteLongEnoughSendNoteOn; 
                        noteDetected[currentActiveNote] = true; 
                        noteOnToSend = currentActiveNote;
                        noteOnSampleOffset = newNoteData.sampleOffset;
                        DBG("RMS " << rms);
                        velToPlay = juce::jlimit(minVelocityParam, 127, static_cast<int>(rms  * 127.0f));

                    }
                }
        } 
        currentActiveNote = newNoteMidiNum;
    }

    const double blockStartSeconds = static_cast<double>(blockStartSample) / getSampleRate();


    switch (playerState)
    {
        // case InstrumentState::LongSilence:       DBG("LongSilence"); break;
        // case InstrumentState::ShortSilence:      DBG("ShortSilence"); break;
        case InstrumentState::NoteAfterSilence:  {
            // DBG("NoteAfterSilence"); 
            break;
        }
        case InstrumentState::NoteAfterOtherNote:{
            if (!noteOffNeeded[noteOffToSend]){// note is not on
                break; 
            }
            // DBG("NoteAfterOtherNote"); 
            // only if we actually sent an on for this note
            // if (noteOffNeeded[noteOffToSend]){
            // DBG("OFF " << noteOffToSend << "\n\n");
            midiMessages.addEvent(juce::MidiMessage::noteOff(1, noteOffToSend), noteOffSampleOffset);
            NoteEvent eventOff;
            eventOff.note = noteOffToSend;
            eventOff.velocity = 0.0f;
            eventOff.noteOn = false;
            eventOff.timeSeconds = blockStartSeconds + static_cast<double>(noteOffSampleOffset) / getSampleRate();
            pushNoteEventFromAudioThread(eventOff);
            lastNoteOff = noteOffToSend;
            noteOffNeeded[noteOffToSend] = false; 

            // }            
            break;
        }
 
        case InstrumentState::NoteHeldNotLongEnoughYet:{
            // DBG("NoteHeldNotLongEnoughYet  min " << minAllowedNoteLenSamples << " is: " << timeSinceNoteStartSamples); 
            // DBG("ON " << noteOnToSend << " rms " << rmsSum);
            // midiMessages.addEvent(juce::MidiMessage::noteOn(1, noteOnToSend, static_cast<juce::uint8>(64)), noteOnSampleOffset);
            break;
        }
        case InstrumentState::NoteLongEnoughSendNoteOn:{
            // DBG("NoteLongEnoughSendNoteOn");
            // DBG("ON " << noteOnToSend << " rms " << rmsSum);
            juce::uint8 vel = static_cast<juce::uint8>(velToPlay);

            midiMessages.addEvent(juce::MidiMessage::noteOn(1, noteOnToSend, vel), noteOnSampleOffset);
            // tell the piano roll
            NoteEvent eventOn;
            eventOn.note = noteOnToSend;
            eventOn.velocity = static_cast<float>(vel) / 127.0f;
            eventOn.noteOn = true;
            eventOn.timeSeconds = blockStartSeconds + static_cast<double>(noteOnSampleOffset) / getSampleRate();
            pushNoteEventFromAudioThread(eventOn);
            noteOffNeeded[noteOnToSend] = true; 

            break;
        }
        case InstrumentState::NoteHeldNoteOnSent:{
            // DBG("NoteHeldNoteOnSent"); 
            break;
        }
        case InstrumentState::NoteEndedNowSilentSendNoteOff:{
            // DBG("NoteEndedNowSilent"); 
            // if (lastNoteOff == noteOffToSend){
                // break; 
            // }
            if (!noteOffNeeded[noteOffToSend]){// not on - don't need note off
                break; 
            }
            // DBG("OFF " << noteOffToSend << "\n\n");

            midiMessages.addEvent(juce::MidiMessage::noteOff(1, noteOffToSend), noteOffSampleOffset);
            // tell the piano roll
            NoteEvent eventOff;
            eventOff.note = noteOffToSend;
            eventOff.velocity = 0.0f;
            eventOff.noteOn = false;
            eventOff.timeSeconds = blockStartSeconds + static_cast<double>(noteOffSampleOffset) / getSampleRate();
            pushNoteEventFromAudioThread(eventOff);
            noteOffNeeded[noteOffToSend] = false; 

            break;
        }
    }

    

    // // managing the detections
    // if (detections.size() > 0){// we saw a note. 
    //     silenceForNSamples = 0;

    //     PitchDetector::Detection newNoteData = detections[0];

    //     int newNoteMidiNum = static_cast<int>(std::lround(69.0 + 12.0 * std::log2(newNoteData.freq / 440.0)));
    //     // is it a different note from last time? 
    //     if (newNoteMidiNum != currentActiveNote){// YES it changed
    //         // wantNoteOn[newNoteMidiNum] = true; // we've not sent note on for this one yet 

    //         // note changed - did the previous note last long enough for us to send it out?
    //         const int64 newNoteStartSamples = blockStartSample + newNoteData.sampleOffset;
    //         const int64 oldNoteDurationSamples = newNoteStartSamples - currentActiveNoteStartSample;
    //         const double oldNoteDurationMs = 1000.0 * (static_cast<double>(oldNoteDurationSamples) / getSampleRate());

    //         if (currentActiveNote != -1 &&// previous note was not silence
    //             oldNoteDurationMs > minAllowedNoteLenSecs * 1000.0){// previous note was long enough
    //             // have we sent a note off for the previous note yet?

    //             DBG("Notechanged from valid note - do updates ");
    //             // sendNoteOff = true; 
    //             wantNoteOff[currentActiveNote] = true; 
    //             noteToPlay = currentActiveNote;
    //             durToPlaySamples = oldNoteDurationSamples; 
    //         }
 
    //         wantNoteOn[currentActiveNote] = false;// re-prime the note on requester for the 'old' note 
            
    //         // reset the note on timestamp 
    //         currentActiveNoteStartSample = newNoteData.sampleOffset + blockStartSample;
    //         currentActiveNote = newNoteMidiNum; 

    //     }
    //     else{// we saw a note but it is the same as last time. 
    //         // if the time since we first saw the note is sufficiently long (>minNoteLength)
    //         // then we should send a note on
    //         const int64 noteDurationSoFarSamples = blockStartSample - currentActiveNoteStartSample;
    //         const double noteDurationSoFarMs = 1000.0 * (static_cast<double>(noteDurationSoFarSamples) / getSampleRate());
    //         // is the note long enough, and have we not yet sent a note? 
    //         if (wantNoteOn[currentActiveNote] == false &&// we've not already sent note on 
    //             noteDurationSoFarMs > minAllowedNoteLenSecs * 1000.0){// we have held this note for long enough to send a note on
    //             sendNoteOn = true; 
    //             noteToPlay = currentActiveNote;
    //             DBG("Note held long enough - send note on " << noteToPlay);
    //             // somehow remember that we've sent the note on 
    //             wantNoteOn[currentActiveNote] = true; 
    //         }

    //         // const double note = 1000.0 * (static_cast<double>(oldNoteDurationSamples) / getSampleRate());

    //     }
    // }
    // if (detections.size() == 0){
    //     // remember how long since we last saw a detection 
    //     // because we can get a no detection event even when a note is still playing
    //     // so we 'latch' the note for a bit of time before we consider it to have really ended 

    //     silenceForNSamples += getBlockSize();
    //     const double noDetectionsForNMs = 1000.0 * (static_cast<double>(silenceForNSamples) / getSampleRate());

    //     if (noDetectionsForNMs > (minAllowedNoteLenSecs * 1000.0) && 
    //         currentActiveNote != -1) {
    //         // ok that's a valid 'note off' style silence 
    //         // send a note off 
    //         const int64 currentNoteDurationSamples = blockStartSample - currentActiveNoteStartSample;
    //         const double currentNoteDurationMs = 1000.0 * (static_cast<double>(currentNoteDurationSamples) / getSampleRate());
    //         if (currentNoteDurationMs > minAllowedNoteLenSecs * 1000.0){
    //             DBG("Note ended : send note off " << currentActiveNote << " dur " << currentNoteDurationMs);
    //             wantNoteOff[currentActiveNote] = true;
    //             sendNoteOff = true; 
    //             noteToPlay = currentActiveNote;
    //             durToPlaySamples = currentNoteDurationSamples; 
    //         }
    //         // now reset things!
    //         currentActiveNote = -1; 
    //     }   
    // }

    // if (sendNoteOn){
    //     // we have noteToPlay and durToPlaySamples
    //     // work out the velocity
    //     DBG("ON " << noteToPlay << " amp " << velToPlay << " dur " << durToPlaySamples);

    // }

    // if (sendNoteOff){
    //     // we have noteToPlay and durToPlaySamples
    //     // work out the velocity
    //     DBG("OFF " << noteToPlay << " amp " << velToPlay << " dur " << durToPlaySamples);

    // }

    if (!midiMessages.isEmpty())
    {
        for (const auto metadata : midiMessages)
        {
            const auto& msg = metadata.getMessage();
            if (msg.isNoteOn())
            {
                DBG("MIDI ON " << msg.getNoteNumber() << " vel " << static_cast<int>(msg.getVelocity())
                               << " ch " << msg.getChannel() << " at " << metadata.samplePosition);
            }
            else if (msg.isNoteOff())
            {
                DBG("MIDI OFF " << msg.getNoteNumber() << " ch " << msg.getChannel()
                                << " at " << metadata.samplePosition);
            }
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
    // return new GenericAudioProcessorEditor (*this);
    
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
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramExecFreq, "Cycle Time", juce::NormalisableRange<float>(10.0f, 500.0f, 0.01f, 0.5f), 10.0f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(paramMaxBins, "Max Bins/Oct", 1, 32, 16));
    params.push_back(std::make_unique<juce::AudioParameterInt>(paramMedian, "Median", 1, 31, 7));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramAmpThresh, "Amp Thresh", juce::NormalisableRange<float>(0.0f, 0.02f, 0.0001f), 0.05f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramAmpScale, "Gain", juce::NormalisableRange<float>(0.0f, 10.0f, 0.001f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(paramMinVelocity, "Min Velocity", 0, 64, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramDelay, "Min note len", juce::NormalisableRange<float>(0.0f, 0.25f, 0.001f), 0.001f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramPeakThresh, "Peak Thresh", juce::NormalisableRange<float>(0.1f, 1.0f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(paramDownSample, "Downsample", 1, 32, 1));
    params.push_back(std::make_unique<juce::AudioParameterBool>(paramClarity, "Clarity", true));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramNoteLengthMs, "Max Note Length (s)", juce::NormalisableRange<float>(3.0f, 10.0f, 0.1f), 5.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramDecayTime, "Decay Time (s)", juce::NormalisableRange<float>(0.0f, 0.5f, 0.001f), 0.001f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(paramMidiThru, "MIDI Thru", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(paramFreeze, "GUI Freeze", false));

    return { params.begin(), params.end() };
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TestPluginAudioProcessor();
}
