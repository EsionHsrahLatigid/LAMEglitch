/*
  ==============================================================================
    MP3Codec.cpp - Real Shine Encode -> Corrupt -> dr_mp3 Decode
  ==============================================================================
*/

#define DR_MP3_IMPLEMENTATION

#include "MP3Codec.h"
#include <cmath>
#include <algorithm>
#include <cstring>

//==============================================================================
// MP3Codec Implementation
//==============================================================================
namespace
{
float sanitizeAudioSample(float value)
{
    if (!std::isfinite(value))
        return 0.0f;

    return std::clamp(value, -1.0f, 1.0f);
}

int findMp3SyncOffset(const uint8_t* data, int size)
{
    if (size < 2)
        return -1;
    
    for (int i = 0; i < size - 1; ++i)
    {
        if (data[i] == 0xFF && (data[i + 1] & 0xE0) == 0xE0)
            return i;
    }
    
    return -1;
}

int pickSupportedBitrate(int bitrate, int mpegVersion)
{
    static const int mpeg1Rates[] = { 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 };
    static const int mpeg2Rates[] = { 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 };
    
    const int* rates = mpegVersion == MPEG_I ? mpeg1Rates : mpeg2Rates;
    const int ratesCount = mpegVersion == MPEG_I ? (int)(sizeof(mpeg1Rates) / sizeof(mpeg1Rates[0]))
                                                 : (int)(sizeof(mpeg2Rates) / sizeof(mpeg2Rates[0]));
    int bestRate = rates[0];
    int bestDistance = std::abs(bitrate - rates[0]);
    
    for (int i = 1; i < ratesCount; ++i)
    {
        int distance = std::abs(bitrate - rates[i]);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestRate = rates[i];
        }
    }
    
    return bestRate;
}

int16_t floatToInt16(float value)
{
    float clipped = sanitizeAudioSample(value);
    return (int16_t)std::lrintf(clipped * 32767.0f);
}
}

MP3Codec::MP3Codec()
    : rng(std::random_device{}())
{
    drmp3dec_init(&mp3Decoder);
    mp3Buffer.resize(MP3_BUFFER_SIZE);
    outputBufferL.resize(OUTPUT_BUFFER_SIZE);
    outputBufferR.resize(OUTPUT_BUFFER_SIZE);
    
    // MP3 accumulation buffer for decoder
    mp3AccumBuffer.resize(MP3_ACCUM_BUFFER_SIZE);
    mp3AccumSize = 0;
}

MP3Codec::~MP3Codec()
{
    shutdown();
}

bool MP3Codec::initialize(int sampleRate, int channels, int bitrate)
{
    shutdown();
    
    currentSampleRate = sampleRate;
    currentChannels = channels;
    currentBitrate = bitrate;
    
    int samplerateIndex = shine_find_samplerate_index(sampleRate);
    if (samplerateIndex < 0)
    {
        codecAvailable = false;
        return false;
    }
    
    int mpegVersion = shine_mpeg_version(samplerateIndex);
    int targetBitrate = pickSupportedBitrate(bitrate, mpegVersion);
    if (shine_check_config(sampleRate, targetBitrate) < 0)
    {
        codecAvailable = false;
        return false;
    }
    
    shine_config_t config{};
    config.wave.channels = channels == 1 ? PCM_MONO : PCM_STEREO;
    config.wave.samplerate = sampleRate;
    shine_set_config_mpeg_defaults(&config.mpeg);
    config.mpeg.mode = channels == 1 ? MONO : STEREO;
    config.mpeg.bitr = targetBitrate;
    
    shineEncoder = shine_initialise(&config);
    codecAvailable = (shineEncoder != nullptr);
    if (!codecAvailable)
        return false;
    
    samplesPerPass = shine_samples_per_pass(shineEncoder);
    if (samplesPerPass <= 0 || samplesPerPass > MP3_FRAME_SAMPLES)
    {
        shutdown();
        return false;
    }

    drmp3dec_init(&mp3Decoder);
    
    inputBufferL16.resize(MP3_FRAME_SAMPLES);
    inputBufferR16.resize(MP3_FRAME_SAMPLES);
    std::fill(outputBufferL.begin(), outputBufferL.end(), 0.0f);
    std::fill(outputBufferR.begin(), outputBufferR.end(), 0.0f);
    
    inputWritePos = 0;
    outputWritePos = 0;
    outputReadPos = 0;
    mp3AccumSize = 0;
    lastWetL = 0.0f;
    lastWetR = 0.0f;
    lastDecodeOk.store(false, std::memory_order_relaxed);
    consecutiveDecodeFails.store(0, std::memory_order_relaxed);
    extraLatencySamples = std::max(0, static_cast<int>(sampleRate * 0.01f));
    
    initialized = true;
    return codecAvailable;
}

MP3Codec::BufferCapacities MP3Codec::getBufferCapacities() const
{
    return {
        mp3Buffer.capacity(),
        mp3AccumBuffer.capacity(),
        inputBufferL16.capacity(),
        inputBufferR16.capacity(),
        outputBufferL.capacity(),
        outputBufferR.capacity()
    };
}

void MP3Codec::shutdown()
{
    if (shineEncoder)
    {
        shine_close(shineEncoder);
        shineEncoder = nullptr;
    }
    codecAvailable = false;
    initialized = false;
}

bool MP3Codec::processWithCorruption(const float* inputL, const float* inputR,
                                      float* outputL, float* outputR,
                                      int numSamples)
{
    if (!initialized || !codecAvailable || !shineEncoder)
    {
        // Passthrough if codec not available
        lastDecodeOk.store(false, std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            outputL[i] = inputL[i];
            if (outputR)
                outputR[i] = inputR ? inputR[i] : inputL[i];
        }
        return false;
    }
    
    float clampedCorruption = std::max(0.0f, std::min(1.0f, corruptionAmount));
    bool decodedThisBlock = false;

    // === 1. Accumulate input samples ===
    for (int i = 0; i < numSamples; ++i)
    {
        inputBufferL16[inputWritePos] = floatToInt16(inputL[i]);
        if (currentChannels > 1)
            inputBufferR16[inputWritePos] = floatToInt16(inputR ? inputR[i] : inputL[i]);
        inputWritePos++;
        
        // === 2. Encode when we have enough samples ===
        if (inputWritePos >= samplesPerPass)
        {
            int mp3Bytes = 0;
            int16_t* channels[2] = { inputBufferL16.data(), nullptr };
            if (currentChannels > 1)
                channels[1] = inputBufferR16.data();
            unsigned char* encoded = shine_encode_buffer(
                shineEncoder,
                channels,
                &mp3Bytes
            );
            
            // === 3. If we got MP3 data ===
            if (mp3Bytes > 0 && encoded != nullptr)
            {
                if (mp3Bytes > static_cast<int>(mp3Buffer.size()))
                {
                    lastDecodeOk.store(false, std::memory_order_relaxed);
                    return false;
                }
                
                memcpy(mp3Buffer.data(), encoded, mp3Bytes);
                if (clampedCorruption > 0.0f)
                    corruptMP3Data(mp3Buffer.data(), mp3Bytes);
                
                if (mp3AccumSize + mp3Bytes >= static_cast<int>(mp3AccumBuffer.size()))
                    mp3AccumSize = 0;
                
                memcpy(mp3AccumBuffer.data() + mp3AccumSize, mp3Buffer.data(), mp3Bytes);
                mp3AccumSize += mp3Bytes;
            }
            
            inputWritePos = 0;
        }
    }
    
    // === 4. Decode accumulated MP3 data ===
    int syncOffset = findMp3SyncOffset(mp3AccumBuffer.data(), mp3AccumSize);
    if (syncOffset > 0)
    {
        memmove(mp3AccumBuffer.data(), mp3AccumBuffer.data() + syncOffset, mp3AccumSize - syncOffset);
        mp3AccumSize -= syncOffset;
    }
    else if (syncOffset < 0 && clampedCorruption > 0.0f && mp3AccumSize > 4096)
    {
        int keepBytes = 4;
        memmove(mp3AccumBuffer.data(), mp3AccumBuffer.data() + mp3AccumSize - keepBytes, keepBytes);
        mp3AccumSize = keepBytes;
    }
    
    int decodeAttempts = 0;
    int minDecodeBytes = clampedCorruption > 0.0f ? 128 : 512;
    while (mp3AccumSize > minDecodeBytes && decodeAttempts < 10)
    {
        decodeAttempts++;
        drmp3dec_frame_info info{};
        float pcm[DRMP3_MAX_SAMPLES_PER_FRAME * 2];
        
        int samples = drmp3dec_decode_frame(
            &mp3Decoder,
            mp3AccumBuffer.data(),
            mp3AccumSize,
            pcm,
            &info
        );
        
        if (info.frame_bytes == 0)
        {
            // No valid frame found - just wait for more data, don't skip
            if (clampedCorruption > 0.3f && mp3AccumSize > 4)
            {
                int skipBytes = 1 + static_cast<int>(uniformDist(rng) * 4.0f);
                memmove(mp3AccumBuffer.data(), mp3AccumBuffer.data() + skipBytes, mp3AccumSize - skipBytes);
                mp3AccumSize -= skipBytes;
            }
            break;
        }
        
        // Remove consumed bytes
        int consumed = info.frame_bytes;
        if (consumed > 0 && consumed <= mp3AccumSize)
        {
            memmove(mp3AccumBuffer.data(), mp3AccumBuffer.data() + consumed, mp3AccumSize - consumed);
            mp3AccumSize -= consumed;
        }
        
        // Store decoded samples
        if (samples > 0)
        {
            decodedThisBlock = true;
            for (int s = 0; s < samples; ++s)
            {
                int idx = outputWritePos % static_cast<int>(outputBufferL.size());
                if (info.channels >= 2)
                {
                    outputBufferL[idx] = pcm[s * info.channels];
                    outputBufferR[idx] = pcm[s * info.channels + 1];
                }
                else if (info.channels == 1)
                {
                    outputBufferL[idx] = pcm[s];
                    outputBufferR[idx] = pcm[s];
                }
                else
                {
                    // channels == 0 means decoder needs more frames to determine
                    outputBufferL[idx] = pcm[s];
                    outputBufferR[idx] = pcm[s];
                }
                outputWritePos++;
            }
        }
    }
    
    // === 6. Output samples ===
    int available = outputWritePos - outputReadPos;
    int effectiveAvailable = available;
    if (clampedCorruption > 0.0f)
        effectiveAvailable = std::max(0, available - extraLatencySamples);
    bool decodeOk = decodedThisBlock || available > 0;
    lastDecodeOk.store(decodeOk, std::memory_order_relaxed);
    if (decodeOk)
        consecutiveDecodeFails.store(0, std::memory_order_relaxed);
    else
        consecutiveDecodeFails.fetch_add(1, std::memory_order_relaxed);
    
    for (int i = 0; i < numSamples; ++i)
    {
        // Use available decoded samples if we have any
        if (effectiveAvailable > 0)
        {
            int idx = outputReadPos % static_cast<int>(outputBufferL.size());
            
            // Output decoded audio + 220Hz tone to confirm
            outputL[i] = outputBufferL[idx];
            if (outputR)
                outputR[i] = outputBufferR[idx];
            lastWetL = outputL[i];
            lastWetR = outputR ? outputR[i] : outputL[i];
            
            outputReadPos++;
            available--;
            effectiveAvailable--;
        }
        else
        {
            if (clampedCorruption <= 0.0f)
            {
                outputL[i] = inputL[i];
                if (outputR)
                    outputR[i] = inputR ? inputR[i] : inputL[i];
            }
            else
            {
                outputL[i] = 0.0f;
                if (outputR)
                    outputR[i] = 0.0f;
            }
        }
    }
    
    if (outputReadPos > 100000)
    {
        outputWritePos -= outputReadPos;
        outputReadPos = 0;
    }
    
    return true;
}

void MP3Codec::corruptMP3Data(uint8_t* data, int size)
{
    if (size < 10)
        return;
    
    // Find frame start (sync word 0xFF 0xFB/0xFA/0xF3/etc)
    int startOffset = 0;
    for (int i = 0; i < size - 1; ++i)
    {
        if (data[i] == 0xFF && (data[i + 1] & 0xE0) == 0xE0)
        {
            startOffset = i + 4; // Skip sync + header
            break;
        }
    }
    
    if (startOffset >= size)
        startOffset = 4;
    
    float clampedCorruption = std::max(0.0f, std::min(1.0f, corruptionAmount));
    float bitFlipChance = clampedCorruption * bitFlipProb * 0.08f;
    float byteDropChance = clampedCorruption * byteDropProb * 0.03f;
    float repeatChance = clampedCorruption * frameRepeatProb * 0.06f;
    float randomChance = clampedCorruption * 0.02f;
    float substituteChance = clampedCorruption * 0.5f;
    
    // Corrupt the data portion (not header)
    for (int i = startOffset; i < size; ++i)
    {
        float roll = uniformDist(rng);
        
        // Byte substitution - gentle corruption (0x13 -> 0x22)
        if (data[i] == 0x13 && roll < substituteChance)
        {
            data[i] = 0x22;
        }
        
        // Bit flip - light touch
        if (roll < bitFlipChance)
        {
            int bitCount = 1 + static_cast<int>(uniformDist(rng) * (1.0f + clampedCorruption * 3.0f));
            for (int bit = 0; bit < bitCount; ++bit)
            {
                int bitPos = static_cast<int>(uniformDist(rng) * 8.0f);
                data[i] ^= (1 << bitPos);
            }
        }
        
        // Zero byte - creates dropouts
        if (roll < byteDropChance)
        {
            data[i] = 0x00;
        }
        
        // Random byte - harsh digital noise
        if (roll < randomChance)
        {
            data[i] = static_cast<uint8_t>(uniformDist(rng) * 256.0f);
        }
        
        // Duplicate previous byte - creates stutter
        if (i > startOffset && roll < repeatChance)
        {
            data[i] = data[i - 1];
        }
    }

}

//==============================================================================
// MP3SimulationCodec Implementation
//==============================================================================
MP3SimulationCodec::MP3SimulationCodec()
    : rng(std::random_device{}())
{
    frameBuffer.resize(FRAME_SIZE * 2);
    mdctCoeffs.resize(FRAME_SIZE);
    windowBuffer.resize(FRAME_SIZE);
    
    for (int i = 0; i < FRAME_SIZE; ++i)
    {
        windowBuffer[i] = std::sin(M_PI / FRAME_SIZE * (i + 0.5f));
    }
}

void MP3SimulationCodec::prepare(double sr, int)
{
    sampleRate = sr;
    reset();
}

void MP3SimulationCodec::reset()
{
    std::fill(frameBuffer.begin(), frameBuffer.end(), 0.0f);
    framePosition = 0;
}

void MP3SimulationCodec::setBitrate(int kbps)
{
    bitrate = kbps;
}

void MP3SimulationCodec::process(float* leftChannel, float* rightChannel, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        float L = sanitizeAudioSample(leftChannel[i]);
        float R = rightChannel ? sanitizeAudioSample(rightChannel[i]) : L;
        
        if (corruptionAmount > 0.001f)
        {
            // Bit crushing
            float bits = 16.0f - corruptionAmount * 12.0f;
            float scale = std::pow(2.0f, bits);
            L = std::round(L * scale) / scale;
            R = std::round(R * scale) / scale;
            
            // Random glitch
            if (uniformDist(rng) < corruptionAmount * 0.02f)
            {
                L = sanitizeAudioSample((uniformDist(rng) - 0.5f) * 2.0f);
                R = sanitizeAudioSample((uniformDist(rng) - 0.5f) * 2.0f);
            }
        }
        
        leftChannel[i] = sanitizeAudioSample(L);
        if (rightChannel)
            rightChannel[i] = sanitizeAudioSample(R);
    }
}

void MP3SimulationCodec::simulateMDCT(float*, int) {}
void MP3SimulationCodec::simulateIMDCT(float*, int) {}
void MP3SimulationCodec::applyQuantization(float*, int, int) {}
void MP3SimulationCodec::applyBandLimit(float*, int) {}
