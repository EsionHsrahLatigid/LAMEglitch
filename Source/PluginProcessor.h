/*
  ==============================================================================
    LAMEglitch - Real MP3 Encode/Decode with Data Corruption
    VST3/AU Plugin for macOS - JUCE 8
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "MP3Codec.h"
#include "RealtimeMP3Worker.h"

#include <array>
#include <cstdint>

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
    
    bool isCodecAvailable() const { return realWorker.isCodecAvailable(); }
    bool isDecodeOk() const { return decodeOk.load(std::memory_order_relaxed); }
    bool isUsingRealtimeCodec() const { return useRealCodec.load(std::memory_order_relaxed); }
    bool isRealModeRequested() const { return realModeRequested.load(std::memory_order_relaxed); }
    RealtimeMP3Worker::Statistics getRealCodecStatistics() const { return realWorker.getStatistics(); }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateParameters();
    void processChunk(float* leftChannel, float* rightChannel, int numChannels, int numSamples);
    void submitCompletedInputFrame();
    void loadOutputFrame(std::uint64_t sequence) noexcept;
    
    juce::AudioProcessorValueTreeState apvts;
    
    // Shine/dr_mp3 live exclusively on this worker. The audio callback only
    // exchanges fixed-size frames through its SPSC queues.
    RealtimeMP3Worker realWorker;
    
    // Fallback simulation codec
    MP3SimulationCodec simCodec;
    
    std::atomic<bool> realModeRequested { true };
    std::atomic<bool> useRealCodec { false };
    std::atomic<bool> decodeOk{false};
    RealtimeMP3Worker::Parameters workerParameters;
    
    // Parameters
    std::atomic<float>* corruptionParam = nullptr;
    std::atomic<float>* bitFlipParam = nullptr;
    std::atomic<float>* byteDropParam = nullptr;
    std::atomic<float>* frameRepeatParam = nullptr;
    std::atomic<float>* bitrateParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    
    std::array<float, RealtimeMP3Worker::maxChannels * RealtimeMP3Worker::maxFrameSamples>
        inputFrame {};
    int inputFrameFill = 0;
    bool inputFrameEligibleForReal = false;
    int workerFrameSamples = RealtimeMP3Worker::maxFrameSamples;
    std::uint64_t nextInputFrameSequence = 0;

    RealtimeMP3Worker::OutputFrame pendingOutputFrame;
    RealtimeMP3Worker::OutputFrame activeOutputFrame;
    bool hasPendingOutputFrame = false;
    bool hasActiveOutputFrame = false;

    // Audio-thread-owned delay and mix storage.
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> dryDelayBuffer;
    int dryBufferCapacity = 0;
    int dryDelayPosition = 0;
    int pipelineLatencySamples = 0;
    int totalLatencySamples = 0;
    std::uint64_t streamSamplePosition = 0;

    static constexpr int maxRealtimeBlockSize = 8192;
    
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LAMEglitchAudioProcessor)
};
