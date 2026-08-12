/*
  ==============================================================================
    LAMEglitch - Plugin Editor Header
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <ehl/juce_design/EhlDesign.h>

#include "PluginProcessor.h"

//==============================================================================
class CorruptionSlider : public juce::Component
{
public:
    CorruptionSlider(const juce::String& labelText, 
                     juce::AudioProcessorValueTreeState& apvts,
                     const juce::String& paramID,
                     const juce::String& suffix = "");
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
private:
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CorruptionSlider)
};

//==============================================================================
class LAMEglitchAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit LAMEglitchAudioProcessorEditor(LAMEglitchAudioProcessor&);
    ~LAMEglitchAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    
private:
    void timerCallback() override;
    
    LAMEglitchAudioProcessor& audioProcessor;
    ehl::juce_design::LookAndFeel ehlLookAndFeel;
    ehl::juce_design::ParameterDisplay parameterDisplay {
        ehl::juce_design::DisplayKind::bitcrusher
    };
    
    // Sliders
    CorruptionSlider corruptionSlider;
    CorruptionSlider bitFlipSlider;
    CorruptionSlider byteDropSlider;
    CorruptionSlider frameRepeatSlider;
    CorruptionSlider bitrateSlider;
    CorruptionSlider mixSlider;
    
    // Mode toggle
    juce::ToggleButton modeButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> modeAttachment;
    
    // Status display
    juce::Label statusLabel;
    juce::Label decodeStatusLabel;
    
    void updateDisplayAndStatus();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LAMEglitchAudioProcessorEditor)
};
