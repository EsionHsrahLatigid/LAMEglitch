#include "PluginProcessor.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>

namespace
{
thread_local bool countAllocations = false;
std::atomic<std::size_t> audioThreadAllocations { 0 };
}

void* operator new(std::size_t size)
{
    if (countAllocations)
        audioThreadAllocations.fetch_add(1, std::memory_order_relaxed);
    if (void* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(size);
    }
    catch (...)
    {
        return nullptr;
    }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return ::operator new(size, std::nothrow);
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    if (countAllocations)
        audioThreadAllocations.fetch_add(1, std::memory_order_relaxed);

    void* memory = nullptr;
    if (posix_memalign(&memory, static_cast<std::size_t>(alignment), size) == 0)
        return memory;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return ::operator new(size, alignment);
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete(void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete[](void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete(void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept { std::free(memory); }

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void setParameter(LAMEglitchAudioProcessor& processor, const char* id, float normalized)
{
    auto* parameter = processor.getAPVTS().getParameter(id);
    require(parameter != nullptr, "missing expected parameter");
    parameter->setValueNotifyingHost(normalized);
}
}

int main()
{
    LAMEglitchAudioProcessor processor;
    processor.setNonRealtime(false);
    setParameter(processor, "mode", 0.0f);
    setParameter(processor, "mix", 1.0f);
    processor.prepareToPlay(44100.0, 257);

    juce::AudioBuffer<float> buffer(2, 257);
    juce::MidiBuffer midi;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const float value = std::sin(static_cast<float>(sample) * 0.03f) * 0.5f;
        buffer.setSample(0, sample, value);
        buffer.setSample(1, sample, -value);
    }

    audioThreadAllocations.store(0, std::memory_order_relaxed);
    countAllocations = true;
    for (int block = 0; block < 24; ++block)
        processor.processBlock(buffer, midi);
    countAllocations = false;

    require(audioThreadAllocations.load(std::memory_order_relaxed) == 0,
            "processBlock allocated on the audio thread");
    require(processor.getRealCodecStatistics().submittedFrames > 0,
            "allocation test never crossed a real MP3 worker-frame boundary");

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            require(std::isfinite(buffer.getSample(channel, sample)),
                    "realtime worker underrun produced non-finite output");

    processor.releaseResources();
    return 0;
}
