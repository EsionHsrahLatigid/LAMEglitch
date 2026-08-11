#include "PluginProcessor.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace
{
bool operator==(const MP3Codec::BufferCapacities& lhs, const MP3Codec::BufferCapacities& rhs)
{
    return lhs.mp3 == rhs.mp3
        && lhs.mp3Accum == rhs.mp3Accum
        && lhs.inputLeft == rhs.inputLeft
        && lhs.inputRight == rhs.inputRight
        && lhs.outputLeft == rhs.outputLeft
        && lhs.outputRight == rhs.outputRight;
}

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

void setParameter(juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float normalizedValue)
{
    if (auto* parameter = apvts.getParameter(id))
        parameter->setValueNotifyingHost(normalizedValue);
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
    LAMEglitchAudioProcessor processor;
    setParameter(processor.getAPVTS(), "mode", 1.0f);
    setParameter(processor.getAPVTS(), "corruption", 1.0f);
    setParameter(processor.getAPVTS(), "bitFlip", 1.0f);
    setParameter(processor.getAPVTS(), "byteDrop", 1.0f);
    setParameter(processor.getAPVTS(), "frameRepeat", 1.0f);
    setParameter(processor.getAPVTS(), "mix", 0.5f);

    processor.prepareToPlay(44100.0, 128);

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
    if (!runBlock(processor, 2, 128, 0.25f) || processor.isUsingRealtimeCodec())
    {
        std::cerr << "LAMEglitch real mode was not constrained to realtime-safe simulation\n";
        return 1;
    }

    juce::MemoryBlock state;
    processor.getStateInformation(state);

    LAMEglitchAudioProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    restored.prepareToPlay(48000.0, 64);

    if (!runBlock(restored, 1, 64, 0.25f))
    {
        std::cerr << "LAMEglitch state round-trip processor failed\n";
        return 1;
    }

    MP3Codec codec;
    if (codec.initialize(44100, 2, 128))
    {
        auto before = codec.getBufferCapacities();
        float left[128] {};
        float right[128] {};

        for (int block = 0; block < 64; ++block)
        {
            for (int sample = 0; sample < 128; ++sample)
            {
                left[sample] = std::sin(static_cast<float>(sample + block) * 0.02f) * 0.2f;
                right[sample] = -left[sample];
            }

            codec.processWithCorruption(left, right, left, right, 128);
        }

        auto after = codec.getBufferCapacities();
        if (!(before == after))
        {
            std::cerr << "MP3Codec buffer capacities changed during processing\n";
            return 1;
        }
    }

    processor.releaseResources();
    restored.releaseResources();
    return 0;
}
