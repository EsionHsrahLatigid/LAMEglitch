/*
  ==============================================================================
    LAMEglitch - Plugin Processor Implementation
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
float sanitizeAudioSample(float value)
{
    if (!std::isfinite(value))
        return 0.0f;

    return std::clamp(value, -1.0f, 1.0f);
}
}

//==============================================================================
LAMEglitchAudioProcessor::LAMEglitchAudioProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    corruptionParam = apvts.getRawParameterValue("corruption");
    bitFlipParam = apvts.getRawParameterValue("bitFlip");
    byteDropParam = apvts.getRawParameterValue("byteDrop");
    frameRepeatParam = apvts.getRawParameterValue("frameRepeat");
    bitrateParam = apvts.getRawParameterValue("bitrate");
    mixParam = apvts.getRawParameterValue("mix");
    modeParam = apvts.getRawParameterValue("mode");
}

LAMEglitchAudioProcessor::~LAMEglitchAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout LAMEglitchAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    // Corruption amount (master)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"corruption", 1}, "Corruption",
        juce::NormalisableRange<float>(0.0f, 0.5f, 0.01f), 0.25f));
    
    // Bit flip probability
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"bitFlip", 1}, "Bit Flip",
        juce::NormalisableRange<float>(0.0f, 0.2f, 0.001f), 0.01f));
    
    // Byte drop probability
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"byteDrop", 1}, "Byte Drop",
        juce::NormalisableRange<float>(0.0f, 0.1f, 0.001f), 0.005f));
    
    // Frame repeat probability
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"frameRepeat", 1}, "Frame Repeat",
        juce::NormalisableRange<float>(0.0f, 0.5f, 0.01f), 0.05f));
    
    // MP3 bitrate (32-320 kbps)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"bitrate", 1}, "Bitrate",
        juce::NormalisableRange<float>(32.0f, 320.0f, 8.0f), 128.0f));
    
    // Dry/Wet mix
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mix", 1}, "Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));
    
    // Mode: 0 = Real MP3 (if available), 1 = Simulation
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mode", 1}, "Mode",
        juce::NormalisableRange<float>(0.0f, 1.0f, 1.0f), 0.0f));
    
    return { params.begin(), params.end() };
}

void LAMEglitchAudioProcessor::updateParameters()
{
    const float corruption = std::clamp(corruptionParam->load(), 0.0f, 1.0f);
    const float bitFlip = std::clamp(bitFlipParam->load(), 0.0f, 1.0f);
    const float byteDrop = std::clamp(byteDropParam->load(), 0.0f, 1.0f);
    const float frameRepeat = std::clamp(frameRepeatParam->load(), 0.0f, 1.0f);
    const int bitrate = std::clamp(static_cast<int>(bitrateParam->load()), 8, 320);
    const bool wantsRealCodec = modeParam->load() < 0.5f;

    workerParameters = { corruption, bitFlip, byteDrop, frameRepeat, bitrate };
    const bool previousRealRequest = realModeRequested.exchange(wantsRealCodec,
                                                                std::memory_order_relaxed);
    if (previousRealRequest != wantsRealCodec && inputFrameFill > 0)
        inputFrameEligibleForReal = false;
    useRealCodec.store(wantsRealCodec && realWorker.isCodecAvailable(), std::memory_order_relaxed);

    simCodec.setCorruptionAmount(corruption);
}

//==============================================================================
const juce::String LAMEglitchAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool LAMEglitchAudioProcessor::acceptsMidi() const { return false; }
bool LAMEglitchAudioProcessor::producesMidi() const { return false; }
bool LAMEglitchAudioProcessor::isMidiEffect() const { return false; }
double LAMEglitchAudioProcessor::getTailLengthSeconds() const { return 0.1; }

int LAMEglitchAudioProcessor::getNumPrograms() { return 1; }
int LAMEglitchAudioProcessor::getCurrentProgram() { return 0; }
void LAMEglitchAudioProcessor::setCurrentProgram(int) {}
const juce::String LAMEglitchAudioProcessor::getProgramName(int) { return {}; }
void LAMEglitchAudioProcessor::changeProgramName(int, const juce::String&) {}

//==============================================================================
void LAMEglitchAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const int bitrate = static_cast<int>(bitrateParam->load());
    int channels = getTotalNumOutputChannels();
    channels = std::clamp(channels, 1, 2);

    realWorker.prepare(static_cast<int>(sampleRate), channels, bitrate);
    workerFrameSamples = realWorker.getFrameSamples();
    pipelineLatencySamples = realWorker.getPipelineLatencySamples();
    totalLatencySamples = pipelineLatencySamples + realWorker.getCodecLatencySamples();
    setLatencySamples(totalLatencySamples);

    dryBufferCapacity = std::max(samplesPerBlock, maxRealtimeBlockSize);
    dryBuffer.setSize(channels, dryBufferCapacity, false, false, true);
    dryDelayBuffer.setSize(channels, std::max(1, totalLatencySamples), false, true, true);
    dryDelayBuffer.clear();

    inputFrame.fill(0.0f);
    inputFrameFill = 0;
    inputFrameEligibleForReal = false;
    nextInputFrameSequence = 0;
    hasPendingOutputFrame = false;
    hasActiveOutputFrame = false;
    dryDelayPosition = 0;
    streamSamplePosition = 0;
    decodeOk.store(false, std::memory_order_relaxed);
    updateParameters();
}

void LAMEglitchAudioProcessor::releaseResources()
{
    realWorker.release();
    useRealCodec.store(false, std::memory_order_relaxed);
    decodeOk.store(false, std::memory_order_relaxed);
}

bool LAMEglitchAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void LAMEglitchAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    
    if (numChannels < 1 || numSamples < 1)
        return;
    
    updateParameters();

    if (dryBufferCapacity <= 0)
        return;

    for (int offset = 0; offset < numSamples; offset += dryBufferCapacity)
    {
        const int chunkSamples = std::min(dryBufferCapacity, numSamples - offset);
        float* leftChannel = buffer.getWritePointer(0) + offset;
        float* rightChannel = numChannels > 1 ? buffer.getWritePointer(1) + offset : nullptr;
        processChunk(leftChannel, rightChannel, numChannels, chunkSamples);
    }
}

void LAMEglitchAudioProcessor::processChunk(float* leftChannel, float* rightChannel,
                                            int numChannels, int numSamples)
{
    const float rawMix = mixParam != nullptr ? mixParam->load() : 1.0f;
    const float mix = std::clamp(std::isfinite(rawMix) ? rawMix : 1.0f, 0.0f, 1.0f);

    const bool processReal = useRealCodec.load(std::memory_order_relaxed);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        if (inputFrameFill == 0)
            inputFrameEligibleForReal = realModeRequested.load(std::memory_order_relaxed)
                                     && realWorker.isCodecAvailable();

        const float inputLeft = sanitizeAudioSample(leftChannel[sample]);
        const float inputRight = numChannels > 1 ? sanitizeAudioSample(rightChannel[sample]) : inputLeft;
        leftChannel[sample] = inputLeft;
        if (rightChannel != nullptr)
            rightChannel[sample] = inputRight;

        inputFrame[static_cast<std::size_t>(inputFrameFill)] = inputLeft;
        inputFrame[static_cast<std::size_t>(RealtimeMP3Worker::maxFrameSamples + inputFrameFill)] = inputRight;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float current = channel == 0 ? inputLeft : inputRight;
            float delayed = current;
            if (totalLatencySamples > 0)
            {
                delayed = dryDelayBuffer.getSample(channel, dryDelayPosition);
                dryDelayBuffer.setSample(channel, dryDelayPosition, current);
            }
            dryBuffer.setSample(channel, sample, delayed);
        }

        inputFrameFill++;
        if (inputFrameFill == workerFrameSamples)
            submitCompletedInputFrame();

        float wetLeft = 0.0f;
        float wetRight = 0.0f;
        if (streamSamplePosition >= static_cast<std::uint64_t>(pipelineLatencySamples))
        {
            const auto targetSample = streamSamplePosition
                                    - static_cast<std::uint64_t>(pipelineLatencySamples);
            const auto targetSequence = targetSample / static_cast<std::uint64_t>(workerFrameSamples);
            const int targetOffset = static_cast<int>(targetSample
                                   % static_cast<std::uint64_t>(workerFrameSamples));

            if (targetOffset == 0)
                loadOutputFrame(targetSequence);

            if (hasActiveOutputFrame && activeOutputFrame.sequence == targetSequence)
            {
                wetLeft = activeOutputFrame.samples[static_cast<std::size_t>(targetOffset)];
                wetRight = numChannels > 1
                         ? activeOutputFrame.samples[static_cast<std::size_t>(
                               RealtimeMP3Worker::maxFrameSamples + targetOffset)]
                         : wetLeft;
            }
            else
            {
                // A late worker frame must not shift the stream. Use the
                // latency-aligned dry signal for this frame and discard stale
                // worker output at the next frame boundary.
                wetLeft = dryBuffer.getSample(0, sample);
                wetRight = numChannels > 1 ? dryBuffer.getSample(1, sample) : wetLeft;
            }
        }

        if (processReal)
        {
            leftChannel[sample] = sanitizeAudioSample(wetLeft);
            if (rightChannel != nullptr)
                rightChannel[sample] = sanitizeAudioSample(wetRight);
        }

        if (totalLatencySamples > 0)
            dryDelayPosition = (dryDelayPosition + 1) % totalLatencySamples;
        streamSamplePosition++;
    }

    if (!processReal)
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            leftChannel[sample] = dryBuffer.getSample(0, sample);
            if (rightChannel != nullptr)
                rightChannel[sample] = dryBuffer.getSample(1, sample);
        }
        simCodec.process(leftChannel, rightChannel, numSamples);
        decodeOk.store(false, std::memory_order_relaxed);
    }

    if (mix < 1.0f)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* wet = ch == 0 ? leftChannel : rightChannel;
            const float* dry = dryBuffer.getReadPointer(ch);

            for (int i = 0; i < numSamples; ++i)
                wet[i] = sanitizeAudioSample(dry[i] * (1.0f - mix) + sanitizeAudioSample(wet[i]) * mix);
        }
    }
    else
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* wet = ch == 0 ? leftChannel : rightChannel;
            for (int i = 0; i < numSamples; ++i)
                wet[i] = sanitizeAudioSample(wet[i]);
        }
    }
}

void LAMEglitchAudioProcessor::submitCompletedInputFrame()
{
    const auto sequence = nextInputFrameSequence++;
    const bool shouldSubmit = inputFrameEligibleForReal
                           && realModeRequested.load(std::memory_order_relaxed)
                           && realWorker.isCodecAvailable();

    if (shouldSubmit)
    {
        const bool submitted = realWorker.submitFrame(
            sequence,
            inputFrame.data(),
            inputFrame.data() + RealtimeMP3Worker::maxFrameSamples,
            getTotalNumOutputChannels(),
            workerParameters);

        if (submitted && isNonRealtime())
            realWorker.waitUntilOutputAvailable(sequence, 5000);
    }

    inputFrameFill = 0;
    inputFrameEligibleForReal = false;
}

void LAMEglitchAudioProcessor::loadOutputFrame(std::uint64_t sequence) noexcept
{
    hasActiveOutputFrame = false;

    for (;;)
    {
        if (!hasPendingOutputFrame)
            hasPendingOutputFrame = realWorker.tryPopOutput(pendingOutputFrame);

        if (!hasPendingOutputFrame)
            break;

        if (pendingOutputFrame.sequence < sequence)
        {
            hasPendingOutputFrame = false;
            continue;
        }

        if (pendingOutputFrame.sequence == sequence)
        {
            activeOutputFrame = pendingOutputFrame;
            hasActiveOutputFrame = true;
            hasPendingOutputFrame = false;
            decodeOk.store(activeOutputFrame.decodeOk, std::memory_order_relaxed);
        }

        break;
    }

    if (!hasActiveOutputFrame)
        decodeOk.store(false, std::memory_order_relaxed);
}

//==============================================================================
bool LAMEglitchAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* LAMEglitchAudioProcessor::createEditor()
{
    return new LAMEglitchAudioProcessorEditor(*this);
}

//==============================================================================
void LAMEglitchAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void LAMEglitchAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LAMEglitchAudioProcessor();
}
