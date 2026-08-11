/*
  ==============================================================================
    LAMEglitch - Real MP3 Encode/Decode with Data Corruption
    VST3/AU Plugin for macOS - JUCE 8
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "MP3Codec.h"

//==============================================================================
class LAMEglitchAudioProcessor : public juce::AudioProcessor
{
public:
    //==============================================================================
    LAMEglitchAudioProcessor();
    ~LAMEglitchAudioProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    
    bool isCodecAvailable() const { return codecAvailable; }
    bool isDecodeOk() const { return decodeOk.load(std::memory_order_relaxed); }
    bool isUsingRealtimeCodec() const { return useRealCodec; }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateParameters();
    void processChunk(float* leftChannel, float* rightChannel, int numChannels, int numSamples);
    
    juce::AudioProcessorValueTreeState apvts;
    
    // Real MP3 codec (when Shine is available)
    MP3Codec mp3Codec;
    
    // Fallback simulation codec
    MP3SimulationCodec simCodec;
    
    bool codecAvailable = false;
    bool useRealCodec = true;
    std::atomic<bool> decodeOk{false};
    
    // Parameters
    std::atomic<float>* corruptionParam = nullptr;
    std::atomic<float>* bitFlipParam = nullptr;
    std::atomic<float>* byteDropParam = nullptr;
    std::atomic<float>* frameRepeatParam = nullptr;
    std::atomic<float>* bitrateParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    
    // Dry buffer for mix
    juce::AudioBuffer<float> dryBuffer;
    int dryBufferCapacity = 0;

    static constexpr int maxRealtimeBlockSize = 8192;
    
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LAMEglitchAudioProcessor)
};
