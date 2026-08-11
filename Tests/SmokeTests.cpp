#include "PluginProcessor.h"

#include <cmath>
#include <iostream>

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
}

int main()
{
    LAMEglitchAudioProcessor processor;
    if (auto* mode = processor.getAPVTS().getParameter("mode"))
        mode->setValueNotifyingHost(1.0f);

    processor.prepareToPlay(44100.0, 128);

    juce::AudioBuffer<float> buffer(2, 128);
    juce::MidiBuffer midi;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const float value = sample == 0 ? 0.5f : 0.0f;
        buffer.setSample(0, sample, value);
        buffer.setSample(1, sample, -value);
    }

    processor.processBlock(buffer, midi);

    if (!bufferIsFinite(buffer))
    {
        std::cerr << "LAMEglitch produced a non-finite sample\n";
        return 1;
    }

    processor.releaseResources();
    return 0;
}
