#pragma once

#include <JuceHeader.h>

#include "MP3Codec.h"

#include <array>
#include <atomic>
#include <cstdint>

class RealtimeMP3Worker final : private juce::Thread
{
public:
    static constexpr int maxChannels = 2;
    static constexpr int maxFrameSamples = 1152;
    static constexpr int pipelineDelayFrames = 3;

    struct Parameters
    {
        float corruption = 0.0f;
        float bitFlip = 0.0f;
        float byteDrop = 0.0f;
        float frameRepeat = 0.0f;
        int bitrate = 128;
    };

    struct OutputFrame
    {
        std::array<float, maxChannels * maxFrameSamples> samples {};
        std::uint64_t sequence = 0;
        int numSamples = 0;
        int numChannels = 0;
        bool decodeOk = false;
    };

    struct Statistics
    {
        std::uint64_t submittedFrames = 0;
        std::uint64_t processedFrames = 0;
        std::uint64_t inputOverruns = 0;
        std::uint64_t outputOverruns = 0;
        std::uint64_t encodedFrames = 0;
        std::uint64_t decodedFrames = 0;
        std::uint64_t encodedBytes = 0;
        std::uint64_t bitFlips = 0;
        std::uint64_t zeroedBytes = 0;
        std::uint64_t repeatedBytes = 0;
        int activeBitrate = 0;
    };

    RealtimeMP3Worker();
    ~RealtimeMP3Worker() override;

    bool prepare(int sampleRate, int channels, int bitrate);
    void release();

    bool submitFrame(std::uint64_t sequence,
                     const float* left,
                     const float* right,
                     int numChannels,
                     const Parameters& parameters) noexcept;
    bool tryPopOutput(OutputFrame& output) noexcept;

    bool waitUntilOutputAvailable(std::uint64_t sequence, int timeoutMilliseconds) const;

    bool isCodecAvailable() const noexcept { return codecAvailable.load(std::memory_order_acquire); }
    int getFrameSamples() const noexcept { return frameSamples; }
    int getPipelineLatencySamples() const noexcept { return frameSamples * pipelineDelayFrames; }
    int getCodecLatencySamples() const noexcept { return codecLatencySamples; }
    Statistics getStatistics() const noexcept;

private:
    struct InputFrame
    {
        std::array<float, maxChannels * maxFrameSamples> samples {};
        Parameters parameters;
        std::uint64_t sequence = 0;
        int numSamples = 0;
        int numChannels = 0;
    };

    template <typename Frame, int capacity>
    class FrameQueue
    {
    public:
        bool push(const Frame& frame) noexcept
        {
            int start1 = 0;
            int size1 = 0;
            int start2 = 0;
            int size2 = 0;
            fifo.prepareToWrite(1, start1, size1, start2, size2);
            if (size1 == 0)
                return false;

            frames[static_cast<std::size_t>(start1)] = frame;
            fifo.finishedWrite(1);
            return true;
        }

        bool pop(Frame& frame) noexcept
        {
            int start1 = 0;
            int size1 = 0;
            int start2 = 0;
            int size2 = 0;
            fifo.prepareToRead(1, start1, size1, start2, size2);
            if (size1 == 0)
                return false;

            frame = frames[static_cast<std::size_t>(start1)];
            fifo.finishedRead(1);
            return true;
        }

        void reset() noexcept { fifo.reset(); }

    private:
        std::array<Frame, capacity> frames {};
        juce::AbstractFifo fifo { capacity };
    };

    void run() override;
    bool initialiseCodec(int bitrate);
    void publishCodecStatistics() noexcept;

    static constexpr int queueCapacity = 32;

    FrameQueue<InputFrame, queueCapacity> inputQueue;
    FrameQueue<OutputFrame, queueCapacity> outputQueue;
    MP3Codec codec;

    int configuredSampleRate = 44100;
    int configuredChannels = 2;
    int configuredBitrate = 128;
    int frameSamples = maxFrameSamples;
    int codecLatencySamples = maxFrameSamples * 2;

    std::atomic<bool> codecAvailable { false };
    std::atomic<std::uint64_t> lastDeliveredSequence { 0 };
    std::atomic<bool> hasDeliveredSequence { false };
    std::atomic<std::uint64_t> submittedFrames { 0 };
    std::atomic<std::uint64_t> processedFrames { 0 };
    std::atomic<std::uint64_t> inputOverruns { 0 };
    std::atomic<std::uint64_t> outputOverruns { 0 };
    std::atomic<std::uint64_t> encodedFrames { 0 };
    std::atomic<std::uint64_t> decodedFrames { 0 };
    std::atomic<std::uint64_t> encodedBytes { 0 };
    std::atomic<std::uint64_t> bitFlips { 0 };
    std::atomic<std::uint64_t> zeroedBytes { 0 };
    std::atomic<std::uint64_t> repeatedBytes { 0 };
    std::atomic<int> activeBitrate { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RealtimeMP3Worker)
};
