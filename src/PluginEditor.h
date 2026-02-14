/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "OpenGLPianoRollComponent.h"
#include "LevelMeterComp.h"

//==============================================================================
/**
*/
class TestPluginAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    TestPluginAudioProcessorEditor (TestPluginAudioProcessor&);
    ~TestPluginAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    TestPluginAudioProcessor& audioProcessor;

    OpenGLPianoRollComponent pianoRoll;
    LevelMeterComp levelMeter;

    juce::Slider minFreqSlider;
    juce::Slider initFreqSlider;
    juce::Slider maxFreqSlider;
    juce::Slider execFreqSlider;
    juce::Slider maxBinsSlider;
    juce::Slider medianSlider;
    juce::Slider ampThreshSlider;
    juce::Slider ampScaleSlider;
    juce::Slider minVelocitySlider;
    juce::Slider delaySlider;
    juce::Slider peakThreshSlider;
    juce::Slider downSampleSlider;
    juce::Slider noteLengthSlider;
    juce::Slider decaySlider;

    juce::TabbedComponent controlTabs { juce::TabbedButtonBar::TabsAtTop };
    juce::Component basicControls;
    juce::Component advancedControls;

    juce::Label minFreqLabel;
    juce::Label initFreqLabel;
    juce::Label maxFreqLabel;
    juce::Label execFreqLabel;
    juce::Label maxBinsLabel;
    juce::Label medianLabel;
    juce::Label ampThreshLabel;
    juce::Label ampScaleLabel;
    juce::Label minVelocityLabel;
    juce::Label delayLabel;
    juce::Label peakThreshLabel;
    juce::Label downSampleLabel;
    juce::Label noteLengthLabel;
    juce::Label decayLabel;

    juce::ToggleButton scrollToggle;
    juce::ToggleButton clarityToggle;
    juce::ToggleButton midiThruToggle;
    juce::ToggleButton freezeToggle;
    juce::Label freezeIndicator;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> minFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> initFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> maxFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> execFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> maxBinsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> medianAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ampThreshAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ampScaleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> minVelocityAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> delayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> peakThreshAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> downSampleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noteLengthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> decayAttachment;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> clarityAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> midiThruAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TestPluginAudioProcessorEditor)
};
