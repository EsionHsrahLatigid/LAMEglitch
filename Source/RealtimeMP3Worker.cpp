#include "RealtimeMP3Worker.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace
{
float sanitizeSample(float sample) noexcept
{
    return std::isfinite(sample) ? std::clamp(sample, -1.0f, 1.0f) : 0.0f;
}
}

RealtimeMP3Worker::RealtimeMP3Worker()
    : juce::Thread("LAMEglitch MP3 worker")
{
}

RealtimeMP3Worker::~RealtimeMP3Worker()
{
    release();
}

bool RealtimeMP3Worker::prepare(int sampleRate, int channels, int bitrate)
{
    release();

    configuredSampleRate = sampleRate;
    configuredChannels = std::clamp(channels, 1, maxChannels);
    configuredBitrate = bitrate;
    inputQueue.reset();
    outputQueue.reset();

    submittedFrames.store(0, std::memory_order_relaxed);
    processedFrames.store(0, std::memory_order_relaxed);
    inputOverruns.store(0, std::memory_order_relaxed);
    outputOverruns.store(0, std::memory_order_relaxed);
    hasDeliveredSequence.store(false, std::memory_order_relaxed);

    const bool available = initialiseCodec(bitrate);
    startThread(juce::Thread::Priority::high);
    return available;
}

void RealtimeMP3Worker::release()
{
    if (isThreadRunning())
        stopThread(-1);

    codec.shutdown();
    codecAvailable.store(false, std::memory_order_release);
    inputQueue.reset();
    outputQueue.reset();
}

bool RealtimeMP3Worker::submitFrame(std::uint64_t sequence,
                                    const float* left,
                                    const float* right,
                                    int numChannels,
                                    const Parameters& parameters) noexcept
{
    InputFrame frame;
    frame.sequence = sequence;
    frame.numSamples = frameSamples;
    frame.numChannels = std::clamp(numChannels, 1, maxChannels);
    frame.parameters = parameters;

    for (int sample = 0; sample < frameSamples; ++sample)
    {
        frame.samples[static_cast<std::size_t>(sample)] = sanitizeSample(left[sample]);
        frame.samples[static_cast<std::size_t>(maxFrameSamples + sample)] =
            frame.numChannels > 1 && right != nullptr ? sanitizeSample(right[sample])
                                                     : frame.samples[static_cast<std::size_t>(sample)];
    }

    if (!inputQueue.push(frame))
    {
        inputOverruns.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    submittedFrames.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool RealtimeMP3Worker::tryPopOutput(OutputFrame& output) noexcept
{
    return outputQueue.pop(output);
}

bool RealtimeMP3Worker::waitUntilOutputAvailable(std::uint64_t sequence,
                                                  int timeoutMilliseconds) const
{
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(std::max(0, timeoutMilliseconds));

    while (std::chrono::steady_clock::now() < deadline)
    {
        if (hasDeliveredSequence.load(std::memory_order_acquire)
            && lastDeliveredSequence.load(std::memory_order_acquire) >= sequence)
            return true;

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return hasDeliveredSequence.load(std::memory_order_acquire)
        && lastDeliveredSequence.load(std::memory_order_acquire) >= sequence;
}

RealtimeMP3Worker::Statistics RealtimeMP3Worker::getStatistics() const noexcept
{
    return {
        submittedFrames.load(std::memory_order_relaxed),
        processedFrames.load(std::memory_order_relaxed),
        inputOverruns.load(std::memory_order_relaxed),
        outputOverruns.load(std::memory_order_relaxed),
        encodedFrames.load(std::memory_order_relaxed),
        decodedFrames.load(std::memory_order_relaxed),
        encodedBytes.load(std::memory_order_relaxed),
        bitFlips.load(std::memory_order_relaxed),
        zeroedBytes.load(std::memory_order_relaxed),
        repeatedBytes.load(std::memory_order_relaxed),
        activeBitrate.load(std::memory_order_relaxed)
    };
}

void RealtimeMP3Worker::run()
{
    InputFrame input;

    while (!threadShouldExit())
    {
        if (!inputQueue.pop(input))
        {
            wait(1);
            continue;
        }

        if (input.parameters.bitrate != configuredBitrate)
            initialiseCodec(input.parameters.bitrate);

        OutputFrame output;
        output.sequence = input.sequence;
        output.numSamples = input.numSamples;
        output.numChannels = input.numChannels;

        const float* inputLeft = input.samples.data();
        const float* inputRight = input.numChannels > 1
                                ? input.samples.data() + maxFrameSamples
                                : inputLeft;
        float* outputLeft = output.samples.data();
        float* outputRight = input.numChannels > 1
                           ? output.samples.data() + maxFrameSamples
                           : nullptr;

        if (codecAvailable.load(std::memory_order_acquire))
        {
            codec.setCorruptionAmount(input.parameters.corruption);
            codec.setBitFlipProbability(input.parameters.bitFlip);
            codec.setByteDropProbability(input.parameters.byteDrop);
            codec.setFrameRepeatProbability(input.parameters.frameRepeat);
            codec.processWithCorruption(inputLeft, inputRight, outputLeft, outputRight, input.numSamples);
            output.decodeOk = codec.getLastDecodeOk();
        }
        else
        {
            std::fill(output.samples.begin(), output.samples.end(), 0.0f);
            output.decodeOk = false;
        }

        if (!outputQueue.push(output))
            outputOverruns.fetch_add(1, std::memory_order_relaxed);
        else
        {
            lastDeliveredSequence.store(input.sequence, std::memory_order_release);
            hasDeliveredSequence.store(true, std::memory_order_release);
        }

        processedFrames.fetch_add(1, std::memory_order_relaxed);
        publishCodecStatistics();
    }
}

bool RealtimeMP3Worker::initialiseCodec(int bitrate)
{
    configuredBitrate = bitrate;
    const bool available = codec.initialize(configuredSampleRate, configuredChannels, bitrate);
    const int nextFrameSamples = std::clamp(
        available ? codec.getSamplesPerPass() : maxFrameSamples, 1, maxFrameSamples);
    const int nextCodecLatency = available ? codec.getLatencySamples() : 0;
    if (!isThreadRunning())
    {
        frameSamples = nextFrameSamples;
        codecLatencySamples = nextCodecLatency;
    }
    else
    {
        jassert(nextFrameSamples == frameSamples);
    }
    codecAvailable.store(available, std::memory_order_release);
    activeBitrate.store(available ? codec.getActiveBitrate() : 0, std::memory_order_relaxed);
    publishCodecStatistics();
    return available;
}

void RealtimeMP3Worker::publishCodecStatistics() noexcept
{
    const auto stats = codec.getProcessingStats();
    encodedFrames.store(stats.encodedFrames, std::memory_order_relaxed);
    decodedFrames.store(stats.decodedFrames, std::memory_order_relaxed);
    encodedBytes.store(stats.encodedBytes, std::memory_order_relaxed);
    bitFlips.store(stats.bitFlips, std::memory_order_relaxed);
    zeroedBytes.store(stats.zeroedBytes, std::memory_order_relaxed);
    repeatedBytes.store(stats.repeatedBytes, std::memory_order_relaxed);
    activeBitrate.store(stats.activeBitrate, std::memory_order_relaxed);
}
