#include "PluginProcessor.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace
{
bool bufferIsFinite(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* samples = buffer.getReadPointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            if (!std::isfinite(samples[sample]))
                return false;
        }
    }
    return true;
}

bool setParameter(juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float normalizedValue)
{
    if (auto* parameter = apvts.getParameter(id))
    {
        parameter->setValueNotifyingHost(normalizedValue);
        return true;
    }

    return false;
}

bool runBlock(LAMEglitchAudioProcessor& processor, int channels, int samples, float scale)
{
    juce::AudioBuffer<float> buffer(channels, samples);
    juce::MidiBuffer midi;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const float value = sample == 0 ? scale : std::sin(static_cast<float>(sample) * 0.03f) * scale;
        for (int channel = 0; channel < channels; ++channel)
            buffer.setSample(channel, sample, channel == 0 ? value : -value);
    }

    processor.processBlock(buffer, midi);
    return bufferIsFinite(buffer);
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    LAMEglitchAudioProcessor processor;
    processor.setNonRealtime(true);

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    if (editor == nullptr
        || editor->getWidth() != 640
        || editor->getHeight() != 360
        || !editor->isResizable())
    {
        std::cerr << "LAMEglitch editor did not use the compact resizable EHL layout\n";
        return 1;
    }

    if (!setParameter(processor.getAPVTS(), "mode", 1.0f)
        || !setParameter(processor.getAPVTS(), "corruption", 1.0f)
        || !setParameter(processor.getAPVTS(), "bitFlip", 1.0f)
        || !setParameter(processor.getAPVTS(), "byteDrop", 1.0f)
        || !setParameter(processor.getAPVTS(), "frameRepeat", 1.0f)
        || !setParameter(processor.getAPVTS(), "mix", 0.5f))
    {
        std::cerr << "LAMEglitch is missing an expected parameter\n";
        return 1;
    }

    processor.prepareToPlay(44100.0, 128);

    if (processor.getLatencySamples() <= 0)
    {
        std::cerr << "LAMEglitch did not report worker/codec latency\n";
        return 1;
    }

    for (int samples : { 1, 64, 128, 257, 1024, 9000 })
    {
        if (!runBlock(processor, 2, samples, 0.5f))
        {
            std::cerr << "LAMEglitch variable block produced a non-finite sample\n";
            return 1;
        }
    }

    juce::AudioBuffer<float> buffer(2, 16);
    juce::MidiBuffer midi;
    const float badValues[] = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max()
    };

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const float value = badValues[sample % 5];
        buffer.setSample(0, sample, value);
        buffer.setSample(1, sample, -value);
    }

    processor.processBlock(buffer, midi);

    if (!bufferIsFinite(buffer))
    {
        std::cerr << "LAMEglitch non-finite/extreme regression failed\n";
        return 1;
    }

    setParameter(processor.getAPVTS(), "mode", 0.0f);
    setParameter(processor.getAPVTS(), "corruption", 0.0f);
    setParameter(processor.getAPVTS(), "bitFlip", 0.0f);
    setParameter(processor.getAPVTS(), "byteDrop", 0.0f);
    setParameter(processor.getAPVTS(), "frameRepeat", 0.0f);
    setParameter(processor.getAPVTS(), "mix", 1.0f);

    for (int samples : { 1, 64, 128, 257, 1024, 8192, 9000 })
    {
        if (!runBlock(processor, 2, samples, 0.25f))
        {
            std::cerr << "LAMEglitch real MP3 variable block emitted non-finite output\n";
            return 1;
        }
    }

    for (int block = 0; block < 32; ++block)
        runBlock(processor, 2, 128, 0.25f);

    const auto realStats = processor.getRealCodecStatistics();
    if (!processor.isUsingRealtimeCodec()
        || realStats.submittedFrames == 0
        || realStats.processedFrames == 0
        || realStats.encodedFrames == 0
        || realStats.decodedFrames == 0
        || realStats.encodedBytes == 0)
    {
        std::cerr << "LAMEglitch real mode did not encode and decode actual MP3 frames\n";
        return 1;
    }

    setParameter(processor.getAPVTS(), "bitrate", 1.0f);
    for (int block = 0; block < 20; ++block)
        runBlock(processor, 2, 128, 0.25f);
    const auto highBitrateStats = processor.getRealCodecStatistics();
    if (highBitrateStats.activeBitrate != 320
        || highBitrateStats.encodedFrames == 0
        || highBitrateStats.encodedBytes == 0)
    {
        std::cerr << "LAMEglitch Bitrate did not encode with the reconfigured Shine encoder\n";
        return 1;
    }

    const auto submissionsBeforeSimulation = highBitrateStats.submittedFrames;
    setParameter(processor.getAPVTS(), "mode", 1.0f);
    for (int block = 0; block < 20; ++block)
        runBlock(processor, 2, 128, 0.25f);
    if (processor.isUsingRealtimeCodec()
        || processor.getRealCodecStatistics().submittedFrames != submissionsBeforeSimulation)
    {
        std::cerr << "LAMEglitch simulation mode still submitted real MP3 work\n";
        return 1;
    }

    setParameter(processor.getAPVTS(), "mode", 0.0f);
    for (int block = 0; block < 20; ++block)
        runBlock(processor, 2, 128, 0.25f);
    if (processor.getRealCodecStatistics().submittedFrames <= submissionsBeforeSimulation)
    {
        std::cerr << "LAMEglitch did not resume real MP3 work after a mode switch\n";
        return 1;
    }

    juce::MemoryBlock state;
    processor.getStateInformation(state);

    LAMEglitchAudioProcessor restored;
    restored.setNonRealtime(true);
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    restored.prepareToPlay(48000.0, 64);

    if (!runBlock(restored, 1, 64, 0.25f))
    {
        std::cerr << "LAMEglitch state round-trip processor failed\n";
        return 1;
    }

    LAMEglitchAudioProcessor monoProcessor;
    monoProcessor.setNonRealtime(true);
    juce::AudioProcessor::BusesLayout monoLayout;
    monoLayout.inputBuses.add(juce::AudioChannelSet::mono());
    monoLayout.outputBuses.add(juce::AudioChannelSet::mono());
    if (!monoProcessor.setBusesLayout(monoLayout))
    {
        std::cerr << "LAMEglitch rejected its declared mono layout\n";
        return 1;
    }
    setParameter(monoProcessor.getAPVTS(), "mode", 0.0f);
    setParameter(monoProcessor.getAPVTS(), "corruption", 0.0f);
    monoProcessor.prepareToPlay(44100.0, 128);
    for (int block = 0; block < 64; ++block)
        runBlock(monoProcessor, 1, 128, 0.25f);
    const auto monoStats = monoProcessor.getRealCodecStatistics();
    if (monoStats.encodedFrames == 0 || monoStats.decodedFrames == 0)
    {
        std::cerr << "LAMEglitch mono mode did not encode and decode real MP3 frames\n";
        return 1;
    }

    LAMEglitchAudioProcessor fallbackProcessor;
    fallbackProcessor.setNonRealtime(true);
    setParameter(fallbackProcessor.getAPVTS(), "mode", 0.0f);
    fallbackProcessor.prepareToPlay(96000.0, 128);
    for (int block = 0; block < 16; ++block)
    {
        if (!runBlock(fallbackProcessor, 2, 128, 0.25f))
        {
            std::cerr << "LAMEglitch unsupported-rate fallback emitted non-finite output\n";
            return 1;
        }
    }
    if (fallbackProcessor.isCodecAvailable()
        || fallbackProcessor.isUsingRealtimeCodec()
        || fallbackProcessor.getRealCodecStatistics().submittedFrames != 0)
    {
        std::cerr << "LAMEglitch unsupported-rate fallback submitted real MP3 work\n";
        return 1;
    }

    processor.releaseResources();
    restored.releaseResources();
    monoProcessor.releaseResources();
    fallbackProcessor.releaseResources();
    return 0;
}
