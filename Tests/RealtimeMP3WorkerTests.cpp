#include "RealtimeMP3Worker.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <thread>

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

bool waitForProcessedFrames(const RealtimeMP3Worker& worker,
                            std::uint64_t expected,
                            int timeoutMilliseconds)
{
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(timeoutMilliseconds);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (worker.getStatistics().processedFrames >= expected)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return worker.getStatistics().processedFrames >= expected;
}
}

int main()
{
    RealtimeMP3Worker worker;
    require(worker.prepare(44100, 2, 128), "worker failed to initialize Shine");

    const int frameSamples = worker.getFrameSamples();
    std::array<float, RealtimeMP3Worker::maxFrameSamples> left {};
    std::array<float, RealtimeMP3Worker::maxFrameSamples> right {};
    for (int sample = 0; sample < frameSamples; ++sample)
    {
        left[static_cast<std::size_t>(sample)] =
            std::sin(static_cast<float>(sample) * 0.017f) * 0.6f;
        right[static_cast<std::size_t>(sample)] = -left[static_cast<std::size_t>(sample)];
    }

    RealtimeMP3Worker::Parameters parameters;
    constexpr std::uint64_t framesToSubmit = 64;
    for (std::uint64_t sequence = 0; sequence < framesToSubmit; ++sequence)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!worker.submitFrame(sequence, left.data(), right.data(), 2, parameters))
        {
            require(std::chrono::steady_clock::now() < deadline,
                    "worker input queue did not recover from backpressure");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    require(waitForProcessedFrames(worker, framesToSubmit, 5000),
            "worker did not process the submitted frames");
    const auto overrunStats = worker.getStatistics();
    require(overrunStats.outputOverruns > 0,
            "worker output-overrun path was not exercised");
    require(!worker.waitUntilOutputAvailable(framesToSubmit - 1, 20),
            "dropped output was incorrectly reported as delivered");

    worker.release();
    require(!worker.isCodecAvailable(), "release left the worker codec available");

    require(worker.prepare(44100, 2, 128), "worker did not restart after release");
    parameters.bitrate = 320;
    constexpr std::uint64_t restartedSequence = 100;
    require(worker.submitFrame(restartedSequence, left.data(), right.data(), 2, parameters),
            "restarted worker rejected a frame");
    require(worker.waitUntilOutputAvailable(restartedSequence, 5000),
            "restarted worker did not deliver its frame");

    RealtimeMP3Worker::OutputFrame output;
    require(worker.tryPopOutput(output), "restarted worker output queue was empty");
    require(output.sequence == restartedSequence, "restarted worker returned stale output");
    require(worker.getStatistics().activeBitrate == 320,
            "worker did not reinitialize Shine at the requested bitrate");

    worker.release();
    return 0;
}
