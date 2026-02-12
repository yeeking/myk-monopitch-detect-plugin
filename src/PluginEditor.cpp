/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <array>

//==============================================================================
TestPluginAudioProcessorEditor::TestPluginAudioProcessorEditor (TestPluginAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    addAndMakeVisible(pianoRoll);
    addAndMakeVisible(levelMeter);

    auto& vts = audioProcessor.getValueTreeState();

    auto configureSlider = [&](juce::Slider& slider, juce::Label& label, const juce::String& text)
    {
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
        slider.setColour(juce::Slider::trackColourId, juce::Colour(0xFF4B8BAE));
        slider.setColour(juce::Slider::thumbColourId, juce::Colour(0xFFE6F1FF));
        label.setText(text, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, juce::Colour(0xFFB7C6D9));
        label.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(label);
        addAndMakeVisible(slider);
    };

    configureSlider(initFreqSlider, initFreqLabel, "Init Freq");
    configureSlider(minFreqSlider, minFreqLabel, "Min Freq");
    configureSlider(maxFreqSlider, maxFreqLabel, "Max Freq");
    configureSlider(execFreqSlider, execFreqLabel, "Block repeats");
    configureSlider(maxBinsSlider, maxBinsLabel, "Max Bins/Oct");
    configureSlider(medianSlider, medianLabel, "Median");
    configureSlider(ampThreshSlider, ampThreshLabel, "Amp Thresh");
    configureSlider(ampScaleSlider, ampScaleLabel, "Amp Scale");
    configureSlider(minVelocitySlider, minVelocityLabel, "Min Velocity");
    configureSlider(peakThreshSlider, peakThreshLabel, "Peak Thresh");
    configureSlider(downSampleSlider, downSampleLabel, "Downsample");
    configureSlider(noteLengthSlider, noteLengthLabel, "Note Length (ms)");
    configureSlider(decaySlider, decayLabel, "Decay (s)");

    advancedToggle.setButtonText("Advanced");
    clarityToggle.setButtonText("Clarity");
    midiThruToggle.setButtonText("MIDI Thru");
    freezeToggle.setButtonText("Freeze");
    freezeIndicator.setText("Frozen", juce::dontSendNotification);
    freezeIndicator.setColour(juce::Label::textColourId, juce::Colour(0xFFE8C35E));
    freezeIndicator.setJustificationType(juce::Justification::centredRight);

    addAndMakeVisible(advancedToggle);
    addAndMakeVisible(clarityToggle);
    addAndMakeVisible(midiThruToggle);
    addAndMakeVisible(freezeToggle);
    addAndMakeVisible(freezeIndicator);

    initFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "initFreq", initFreqSlider);
    minFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "minFreq", minFreqSlider);
    maxFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "maxFreq", maxFreqSlider);
    execFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "execFreq", execFreqSlider);
    maxBinsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "maxBins", maxBinsSlider);
    medianAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "median", medianSlider);
    ampThreshAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "ampThresh", ampThreshSlider);
    ampScaleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "ampScale", ampScaleSlider);
    minVelocityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "minVelocity", minVelocitySlider);
    peakThreshAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "peakThresh", peakThreshSlider);
    downSampleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "downSample", downSampleSlider);
    noteLengthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "noteLengthMs", noteLengthSlider);
    decayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "decayTime", decaySlider);

    clarityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts, "clarity", clarityToggle);
    midiThruAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts, "midiThru", midiThruToggle);
    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts, "freeze", freezeToggle);
    advancedToggle.onClick = [this]
    {
        setAdvancedVisible(advancedToggle.getToggleState());
        resized();
    };

    pianoRoll.setTimeWindowSeconds(8.0);
    levelMeter.setFrameRateHz(30);
    levelMeter.setDecaySeconds(1.5f);
    setAdvancedVisible(false);

    setSize (960, 560);
    startTimerHz(60);
}

TestPluginAudioProcessorEditor::~TestPluginAudioProcessorEditor()
{
}

//==============================================================================
void TestPluginAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (juce::Colour(0xFF0E1419));

    g.setColour (juce::Colour(0xFFE6F1FF));
    juce::Font titleFont(juce::FontOptions(18.0f, juce::Font::bold));
    g.setFont(titleFont);
    g.drawText ("Pitch Tracker", getLocalBounds().removeFromTop(28).reduced(12, 2),
                juce::Justification::centredLeft);
}

void TestPluginAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
    auto area = getLocalBounds().reduced(12);
    auto header = area.removeFromTop(32);
    levelMeter.setBounds(header.removeFromRight(240));

    area.removeFromTop(6);
    auto controls = area.removeFromLeft(280);
    constexpr int rowHeight = 30;
    auto row = [&](juce::Label& label, juce::Slider& slider)
    {
        auto line = controls.removeFromTop(rowHeight);
        label.setBounds(line.removeFromLeft(120));
        slider.setBounds(line);
    };

    row(ampThreshLabel, ampThreshSlider);
    row(ampScaleLabel, ampScaleSlider);
    row(minVelocityLabel, minVelocitySlider);
    row(execFreqLabel, execFreqSlider);

    controls.removeFromTop(6);
    auto advancedToggleRow = controls.removeFromTop(24);
    advancedToggle.setBounds(advancedToggleRow);

    if (advancedVisible)
    {
        controls.removeFromTop(6);
        row(initFreqLabel, initFreqSlider);
        row(minFreqLabel, minFreqSlider);
        row(maxFreqLabel, maxFreqSlider);
        row(maxBinsLabel, maxBinsSlider);
        row(medianLabel, medianSlider);
        row(peakThreshLabel, peakThreshSlider);
        row(downSampleLabel, downSampleSlider);
        row(noteLengthLabel, noteLengthSlider);
        row(decayLabel, decaySlider);

        controls.removeFromTop(6);
        auto toggleRow = controls.removeFromTop(24);
        clarityToggle.setBounds(toggleRow.removeFromLeft(90));
        midiThruToggle.setBounds(toggleRow.removeFromLeft(110));
        freezeToggle.setBounds(toggleRow.removeFromLeft(80));
        freezeIndicator.setBounds(toggleRow);
    }
    else
    {
        clarityToggle.setBounds({});
        midiThruToggle.setBounds({});
        freezeToggle.setBounds({});
        freezeIndicator.setBounds({});
    }

    area.removeFromLeft(12);
    pianoRoll.setBounds(area);
}

void TestPluginAudioProcessorEditor::setAdvancedVisible(bool shouldShow)
{
    advancedVisible = shouldShow;
    advancedToggle.setToggleState(advancedVisible, juce::dontSendNotification);

    initFreqLabel.setVisible(advancedVisible);
    initFreqSlider.setVisible(advancedVisible);
    minFreqLabel.setVisible(advancedVisible);
    minFreqSlider.setVisible(advancedVisible);
    maxFreqLabel.setVisible(advancedVisible);
    maxFreqSlider.setVisible(advancedVisible);
    maxBinsLabel.setVisible(advancedVisible);
    maxBinsSlider.setVisible(advancedVisible);
    medianLabel.setVisible(advancedVisible);
    medianSlider.setVisible(advancedVisible);
    peakThreshLabel.setVisible(advancedVisible);
    peakThreshSlider.setVisible(advancedVisible);
    downSampleLabel.setVisible(advancedVisible);
    downSampleSlider.setVisible(advancedVisible);
    noteLengthLabel.setVisible(advancedVisible);
    noteLengthSlider.setVisible(advancedVisible);
    decayLabel.setVisible(advancedVisible);
    decaySlider.setVisible(advancedVisible);
    clarityToggle.setVisible(advancedVisible);
    midiThruToggle.setVisible(advancedVisible);
    freezeToggle.setVisible(advancedVisible);
    freezeIndicator.setVisible(advancedVisible);
}

void TestPluginAudioProcessorEditor::timerCallback()
{
    std::array<TestPluginAudioProcessor::NoteEvent, 128> events {};
    const int count = audioProcessor.pullNoteEvents(events.data(), static_cast<int>(events.size()));
    for (int i = 0; i < count; ++i)
    {
        const auto& event = events[static_cast<size_t>(i)];
        if (event.noteOn)
            pianoRoll.noteOn(event.note, event.velocity, event.timeSeconds);
        else
            pianoRoll.noteOff(event.note, event.timeSeconds);
    }

    levelMeter.setRMS(audioProcessor.getRmsLevel());

    const bool frozen = audioProcessor.getValueTreeState().getRawParameterValue("freeze")->load() > 0.5f;
    freezeIndicator.setVisible(frozen && advancedVisible);
    pianoRoll.setFrozen(frozen);
}
