/*
  ==============================================================================
    MP3Codec.h - Shine Encoder / dr_mp3 Decoder Wrapper
  ==============================================================================
*/

#pragma once

#include <vector>
#include <cstdint>
#include <memory>
#include <random>
#include <atomic>

// Shine encoder
#ifdef __cplusplus
extern "C" {
#endif
#include "layer3.h"
#ifdef __cplusplus
}
#endif

// dr_mp3 decoder (float output)
#define DR_MP3_FLOAT_OUTPUT
#include "dr_mp3.h"

//==============================================================================
class MP3Codec
{
public:
    struct BufferCapacities
    {
        size_t mp3 = 0;
        size_t mp3Accum = 0;
        size_t inputLeft = 0;
        size_t inputRight = 0;
        size_t outputLeft = 0;
        size_t outputRight = 0;
    };

    MP3Codec();
    ~MP3Codec();
    
    bool initialize(int sampleRate, int channels, int bitrate);
    void shutdown();
    bool isInitialized() const { return initialized; }
    
    // Encode PCM to MP3, then decode back to PCM with optional corruption
    bool processWithCorruption(const float* inputL, const float* inputR, 
                                float* outputL, float* outputR,
                                int numSamples);
    
    // Corruption parameters
    void setCorruptionAmount(float amount) { corruptionAmount = amount; }
    void setBitFlipProbability(float prob) { bitFlipProb = prob; }
    void setByteDropProbability(float prob) { byteDropProb = prob; }
    void setFrameRepeatProbability(float prob) { frameRepeatProb = prob; }
    
    // Get latency in samples
    int getLatencySamples() const { return latencySamples; }
    bool getLastDecodeOk() const { return lastDecodeOk.load(std::memory_order_relaxed); }
    BufferCapacities getBufferCapacities() const;
    
private:
    void corruptMP3Data(uint8_t* data, int size);
    
    // Shine encoder
    shine_t shineEncoder = nullptr;
    int samplesPerPass = SHINE_MAX_SAMPLES;
    
    // dr_mp3 decoder
    drmp3dec mp3Decoder;
    
    // Buffers
    std::vector<uint8_t> mp3Buffer;
    std::vector<uint8_t> mp3AccumBuffer;
    int mp3AccumSize = 0;
    std::vector<int16_t> inputBufferL16;
    std::vector<int16_t> inputBufferR16;
    std::vector<float> outputBufferL;
    std::vector<float> outputBufferR;
    
    // State
    bool initialized = false;
    bool codecAvailable = false;
    int currentSampleRate = 44100;
    int currentChannels = 2;
    int currentBitrate = 128;
    int latencySamples = 1152 * 2;
    
    // Ring buffer positions
    int inputWritePos = 0;
    int outputWritePos = 0;
    int outputReadPos = 0;
    
    // Corruption parameters
    float corruptionAmount = 0.5f;
    float bitFlipProb = 0.01f;
    float byteDropProb = 0.005f;
    float frameRepeatProb = 0.05f;
    
    // Random
    std::mt19937 rng;
    std::uniform_real_distribution<float> uniformDist{0.0f, 1.0f};
    
    static constexpr int MP3_FRAME_SAMPLES = 1152;
    static constexpr int MP3_BUFFER_SIZE = 8192;
    static constexpr int MP3_ACCUM_BUFFER_SIZE = MP3_BUFFER_SIZE * 4;
    static constexpr int OUTPUT_BUFFER_SIZE = MP3_FRAME_SAMPLES * 16;

    float lastWetL = 0.0f;
    float lastWetR = 0.0f;
    std::atomic<bool> lastDecodeOk{false};
    std::atomic<int> consecutiveDecodeFails{0};
    int extraLatencySamples = 0;
};

//==============================================================================
// Fallback: Pure DSP simulation when encoder is not available
//==============================================================================
class MP3SimulationCodec
{
public:
    MP3SimulationCodec();
    
    void prepare(double sampleRate, int samplesPerBlock);
    void process(float* leftChannel, float* rightChannel, int numSamples);
    void reset();
    
    void setCorruptionAmount(float amount) { corruptionAmount = amount; }
    void setBitrate(int kbps);
    
private:
    void simulateMDCT(float* data, int size);
    void simulateIMDCT(float* data, int size);
    void applyQuantization(float* data, int size, int bits);
    void applyBandLimit(float* data, int size);
    
    double sampleRate = 44100.0;
    int bitrate = 128;
    float corruptionAmount = 0.5f;
    
    std::vector<float> frameBuffer;
    std::vector<float> mdctCoeffs;
    std::vector<float> windowBuffer;
    int framePosition = 0;
    
    std::mt19937 rng;
    std::uniform_real_distribution<float> uniformDist{0.0f, 1.0f};
    std::normal_distribution<float> normalDist{0.0f, 1.0f};
    
    static constexpr int FRAME_SIZE = 576;
};
