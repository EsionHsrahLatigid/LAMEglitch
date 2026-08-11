/*
  ==============================================================================
    LAMEglitch - Plugin Editor Header
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
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
    
    // Sliders
    CorruptionSlider corruptionSlider;
    CorruptionSlider bitFlipSlider;
    CorruptionSlider byteDropSlider;
    CorruptionSlider frameRepeatSlider;
    CorruptionSlider bitrateSlider;
    CorruptionSlider mixSlider;
    
    // Mode toggle
    juce::TextButton modeButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> modeAttachment;
    
    // Status display
    juce::Label statusLabel;
    juce::Label decodeStatusLabel;
    
    // Visualizer
    std::vector<float> waveformData;
    std::vector<uint8_t> corruptedBytesViz;
    int vizPhase = 0;
    float glitchIntensity = 0.0f;
    
    std::mt19937 rng{std::random_device{}()};
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LAMEglitchAudioProcessorEditor)
};
