#include "MP3Codec.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

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

struct CodecRun
{
    MP3Codec::ProcessingStats stats;
    MP3Codec::BufferCapacities capacitiesBefore;
    MP3Codec::BufferCapacities capacitiesAfter;
    bool finiteOutput = true;
    bool audibleOutput = false;
};

CodecRun runCodec(int bitrate,
                  float corruption,
                  float bitFlip,
                  float byteDrop,
                  float frameRepeat,
                  int frames)
{
    MP3Codec codec;
    codec.setRandomSeed(0x4c414d45u);
    require(codec.initialize(44100, 2, bitrate), "Shine codec did not initialize");
    codec.setCorruptionAmount(corruption);
    codec.setBitFlipProbability(bitFlip);
    codec.setByteDropProbability(byteDrop);
    codec.setFrameRepeatProbability(frameRepeat);

    const int frameSamples = codec.getSamplesPerPass();
    require(frameSamples > 0 && frameSamples <= 1152, "invalid Shine frame size");

    std::array<float, 1152> left {};
    std::array<float, 1152> right {};
    std::array<float, 1152> outputLeft {};
    std::array<float, 1152> outputRight {};
    CodecRun result;
    result.capacitiesBefore = codec.getBufferCapacities();

    std::uint64_t samplePosition = 0;
    for (int frame = 0; frame < frames; ++frame)
    {
        for (int sample = 0; sample < frameSamples; ++sample, ++samplePosition)
        {
            const float phase = static_cast<float>(samplePosition) * 0.017f;
            left[static_cast<std::size_t>(sample)] = std::sin(phase) * 0.7f;
            right[static_cast<std::size_t>(sample)] = std::cos(phase * 0.73f) * 0.6f;
        }

        codec.processWithCorruption(left.data(), right.data(),
                                    outputLeft.data(), outputRight.data(), frameSamples);

        for (int sample = 0; sample < frameSamples; ++sample)
        {
            const float l = outputLeft[static_cast<std::size_t>(sample)];
            const float r = outputRight[static_cast<std::size_t>(sample)];
            result.finiteOutput = result.finiteOutput && std::isfinite(l) && std::isfinite(r);
            result.audibleOutput = result.audibleOutput || std::abs(l) > 1.0e-6f || std::abs(r) > 1.0e-6f;
        }
    }

    result.stats = codec.getProcessingStats();
    result.capacitiesAfter = codec.getBufferCapacities();
    return result;
}

bool sameCapacities(const MP3Codec::BufferCapacities& a,
                    const MP3Codec::BufferCapacities& b)
{
    return a.mp3 == b.mp3
        && a.mp3Accum == b.mp3Accum
        && a.inputLeft == b.inputLeft
        && a.inputRight == b.inputRight
        && a.outputLeft == b.outputLeft
        && a.outputRight == b.outputRight;
}
}

int main()
{
    {
        MP3Codec latencyProbe;
        require(latencyProbe.initialize(44100, 2, 128), "latency probe codec did not initialize");
        latencyProbe.setCorruptionAmount(0.0f);
        std::array<float, 1152> probeInput {};
        std::array<float, 1152> probeOutput {};
        int firstNonZero = -1;
        for (int frame = 0; frame < 8; ++frame)
        {
            for (int sample = 0; sample < 1152; ++sample)
                probeInput[static_cast<std::size_t>(sample)] =
                    std::sin(static_cast<float>(frame * 1152 + sample) * 0.017f) * 0.7f;
            latencyProbe.processWithCorruption(probeInput.data(), probeInput.data(),
                                               probeOutput.data(), probeOutput.data(), 1152);
            for (int sample = 0; sample < 1152 && firstNonZero < 0; ++sample)
                if (std::abs(probeOutput[static_cast<std::size_t>(sample)]) > 1.0e-7f)
                    firstNonZero = frame * 1152 + sample;
        }
        if (!(firstNonZero >= latencyProbe.getLatencySamples()
              && firstNonZero < latencyProbe.getLatencySamples() + 1152))
        {
            std::cerr << "reported codec latency does not bracket first decoded PCM: first="
                      << firstNonZero << " reported=" << latencyProbe.getLatencySamples() << '\n';
            std::exit(1);
        }
    }

    const auto clean = runCodec(128, 0.0f, 0.0f, 0.0f, 0.0f, 12);
    require(clean.stats.encodedFrames >= 12, "real path did not produce MP3 frames");
    require(clean.stats.encodedBytes > 0, "real path produced no MP3 bytes");
    require(clean.stats.decodedFrames > 0, "dr_mp3 decoded no real MP3 frames");
    require(clean.finiteOutput, "clean real MP3 path emitted non-finite output");
    require(clean.audibleOutput, "clean real MP3 path stayed silent");
    require(sameCapacities(clean.capacitiesBefore, clean.capacitiesAfter),
            "real MP3 buffers changed capacity while processing");

    const auto bitFlip = runCodec(128, 0.5f, 0.2f, 0.0f, 0.0f, 64);
    require(bitFlip.stats.bitFlips > 0, "Bit Flip did not alter real MP3 payload bytes");
    require(bitFlip.finiteOutput, "Bit Flip produced non-finite decoded output");

    const auto byteDrop = runCodec(128, 0.5f, 0.0f, 0.1f, 0.0f, 64);
    require(byteDrop.stats.zeroedBytes > 0, "Byte Drop did not alter real MP3 payload bytes");
    require(byteDrop.finiteOutput, "Byte Drop produced non-finite decoded output");

    const auto frameRepeat = runCodec(128, 0.5f, 0.0f, 0.0f, 0.5f, 64);
    require(frameRepeat.stats.repeatedBytes > 0, "Frame Repeat did not alter real MP3 payload bytes");
    require(frameRepeat.finiteOutput, "Frame Repeat produced non-finite decoded output");

    const auto lowBitrate = runCodec(32, 0.0f, 0.0f, 0.0f, 0.0f, 8);
    const auto highBitrate = runCodec(320, 0.0f, 0.0f, 0.0f, 0.0f, 8);
    require(lowBitrate.stats.activeBitrate == 32, "low bitrate was not applied to Shine");
    require(highBitrate.stats.activeBitrate == 320, "high bitrate was not applied to Shine");
    require(lowBitrate.stats.encodedBytes < highBitrate.stats.encodedBytes,
            "Bitrate did not change real MP3 frame size");

    return 0;
}
