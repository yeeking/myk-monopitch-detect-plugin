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
    addAndMakeVisible(controlTabs);
    controlTabs.setTabBarDepth(26);
    controlTabs.addTab("Basic", juce::Colour(0xFF151C22), &basicControls, false);
    controlTabs.addTab("Advanced", juce::Colour(0xFF151C22), &advancedControls, false);

    auto& vts = audioProcessor.getValueTreeState();

    auto configureSlider = [&](juce::Component& parent, juce::Slider& slider, juce::Label& label, const juce::String& text)
    {
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
        slider.setColour(juce::Slider::trackColourId, juce::Colour(0xFF4B8BAE));
        slider.setColour(juce::Slider::thumbColourId, juce::Colour(0xFFE6F1FF));
        label.setText(text, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, juce::Colour(0xFFB7C6D9));
        label.setJustificationType(juce::Justification::centredLeft);
        parent.addAndMakeVisible(label);
        parent.addAndMakeVisible(slider);
    };

    configureSlider(advancedControls, initFreqSlider, initFreqLabel, "Init Freq");
    configureSlider(advancedControls, minFreqSlider, minFreqLabel, "Min Freq");
    configureSlider(advancedControls, maxFreqSlider, maxFreqLabel, "Max Freq");
    configureSlider(advancedControls, execFreqSlider, execFreqLabel, "Cycle time");
    configureSlider(advancedControls, maxBinsSlider, maxBinsLabel, "Max Bins/Oct");
    configureSlider(advancedControls, medianSlider, medianLabel, "Median");
    configureSlider(basicControls, ampThreshSlider, ampThreshLabel, "Amp Thresh");
    configureSlider(basicControls, ampScaleSlider, ampScaleLabel, "Gain");
    configureSlider(basicControls, minVelocitySlider, minVelocityLabel, "Min Velocity");
    configureSlider(basicControls, delaySlider, delayLabel, "Nin note len (ms)");
    configureSlider(advancedControls, peakThreshSlider, peakThreshLabel, "Peak Thresh");
    configureSlider(advancedControls, downSampleSlider, downSampleLabel, "Downsample");
    configureSlider(advancedControls, noteLengthSlider, noteLengthLabel, "Max Note Length (s)");
    configureSlider(advancedControls, decaySlider, decayLabel, "Decay (s)");
    scrollToggle.setButtonText("Scroll");
    scrollToggle.setToggleState(true, juce::dontSendNotification);
    clarityToggle.setButtonText("Clarity");
    midiThruToggle.setButtonText("MIDI Thru");
    freezeToggle.setButtonText("Freeze");
    freezeIndicator.setText("Frozen", juce::dontSendNotification);
    freezeIndicator.setColour(juce::Label::textColourId, juce::Colour(0xFFE8C35E));
    freezeIndicator.setJustificationType(juce::Justification::centredRight);

    basicControls.addAndMakeVisible(scrollToggle);
    basicControls.addAndMakeVisible(midiThruToggle);
    basicControls.addAndMakeVisible(freezeToggle);
    basicControls.addAndMakeVisible(freezeIndicator);
    advancedControls.addAndMakeVisible(clarityToggle);

    initFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "initFreq", initFreqSlider);
    minFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "minFreq", minFreqSlider);
    maxFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "maxFreq", maxFreqSlider);
    execFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "execFreq", execFreqSlider);
    maxBinsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "maxBins", maxBinsSlider);
    medianAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "median", medianSlider);
    ampThreshAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "ampThresh", ampThreshSlider);
    ampScaleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "ampScale", ampScaleSlider);
    minVelocityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "minVelocity", minVelocitySlider);
    delayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "delay", delaySlider);
    peakThreshAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "peakThresh", peakThreshSlider);
    downSampleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "downSample", downSampleSlider);
    noteLengthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "noteLengthMs", noteLengthSlider);
    decayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "decayTime", decaySlider);

    clarityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts, "clarity", clarityToggle);
    midiThruAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts, "midiThru", midiThruToggle);
    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts, "freeze", freezeToggle);
    scrollToggle.onClick = [this]
    {
        pianoRoll.setScrollEnabled(scrollToggle.getToggleState());
    };

    pianoRoll.setTimeWindowSeconds(8.0);
    pianoRoll.setScrollEnabled(true);
    levelMeter.setFrameRateHz(15);
    levelMeter.setDecaySeconds(1.5f);

    setSize (640,720);
    startTimerHz(10);
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
    const int pianoHeight = juce::jlimit(140, 260, area.getHeight() / 3);
    pianoRoll.setBounds(area.removeFromTop(pianoHeight));

    area.removeFromTop(10);
    controlTabs.setBounds(area);

    constexpr int labelWidth = 140;
    constexpr int basicRowHeight = 38;
    constexpr int advancedRowHeight = 22;

    auto basicArea = basicControls.getLocalBounds().reduced(10, 8);
    auto basicRow = [&](juce::Label& label, juce::Slider& slider, int height)
    {
        auto line = basicArea.removeFromTop(height);
        label.setBounds(line.removeFromLeft(labelWidth));
        slider.setBounds(line);
    };

    basicRow(ampThreshLabel, ampThreshSlider, basicRowHeight);
    basicRow(ampScaleLabel, ampScaleSlider, basicRowHeight);
    basicRow(minVelocityLabel, minVelocitySlider, basicRowHeight);
    basicRow(delayLabel, delaySlider, basicRowHeight);

    basicArea.removeFromTop(8);
    auto scrollRow = basicArea.removeFromTop(24);
    scrollToggle.setBounds(scrollRow.removeFromLeft(100));
    midiThruToggle.setBounds(scrollRow.removeFromLeft(110));
    freezeToggle.setBounds(scrollRow.removeFromLeft(80));
    freezeIndicator.setBounds(scrollRow);

    auto advancedArea = advancedControls.getLocalBounds().reduced(10, 8);
    const int columnGap = 12;
    auto leftColumn = advancedArea.removeFromLeft((advancedArea.getWidth() - columnGap) / 2);
    advancedArea.removeFromLeft(columnGap);
    auto rightColumn = advancedArea;

    auto advancedRow = [&](juce::Rectangle<int>& column, juce::Label& label, juce::Slider& slider)
    {
        auto line = column.removeFromTop(advancedRowHeight);
        label.setBounds(line.removeFromLeft(labelWidth));
        slider.setBounds(line);
    };

    advancedRow(leftColumn, execFreqLabel, execFreqSlider);
    advancedRow(leftColumn, initFreqLabel, initFreqSlider);
    advancedRow(leftColumn, minFreqLabel, minFreqSlider);
    advancedRow(leftColumn, maxFreqLabel, maxFreqSlider);
    advancedRow(leftColumn, maxBinsLabel, maxBinsSlider);

    advancedRow(rightColumn, medianLabel, medianSlider);
    advancedRow(rightColumn, peakThreshLabel, peakThreshSlider);
    advancedRow(rightColumn, downSampleLabel, downSampleSlider);
    advancedRow(rightColumn, noteLengthLabel, noteLengthSlider);
    advancedRow(rightColumn, decayLabel, decaySlider);

    rightColumn.removeFromTop(6);
    auto toggleRow = rightColumn.removeFromTop(24);
    clarityToggle.setBounds(toggleRow.removeFromLeft(90));
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
    const bool basicTabActive = (controlTabs.getCurrentTabIndex() == 0);
    freezeIndicator.setVisible(frozen && basicTabActive);
    pianoRoll.setFrozen(frozen);
}
