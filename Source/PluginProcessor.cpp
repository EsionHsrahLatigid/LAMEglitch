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
    float corruption = *corruptionParam;
    float bitFlip = *bitFlipParam;
    float byteDrop = *byteDropParam;
    float frameRepeat = *frameRepeatParam;
    int bitrate = static_cast<int>(*bitrateParam);
    
    // The bundled MP3 codec can perform internal work that is not a hard
    // real-time contract. Keep the user-facing mode parameter, but constrain the
    // audio callback to deterministic simulation until a worker-thread codec path
    // exists.
    useRealCodec = false;
    
    // Always update both codecs so switching modes works
    mp3Codec.setCorruptionAmount(corruption);
    mp3Codec.setBitFlipProbability(bitFlip);
    mp3Codec.setByteDropProbability(byteDrop);
    mp3Codec.setFrameRepeatProbability(frameRepeat);
    
    simCodec.setCorruptionAmount(corruption);
    simCodec.setBitrate(bitrate);
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
    int bitrate = static_cast<int>(*bitrateParam);
    int channels = getTotalNumOutputChannels();
    channels = std::clamp(channels, 1, 2);
    
    // Try to initialize real MP3 codec
    codecAvailable = mp3Codec.initialize(static_cast<int>(sampleRate), channels, bitrate);
    
    // Always prepare simulation codec as fallback
    simCodec.prepare(sampleRate, samplesPerBlock);
    
    // Prepare dry buffer once. processBlock chunks larger host blocks through
    // this fixed capacity instead of reallocating on the audio thread.
    dryBufferCapacity = std::max(samplesPerBlock, maxRealtimeBlockSize);
    dryBuffer.setSize(channels, dryBufferCapacity, false, false, true);
}

void LAMEglitchAudioProcessor::releaseResources()
{
    mp3Codec.shutdown();
    simCodec.reset();
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

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* input = ch == 0 ? leftChannel : rightChannel;
        float* dry = dryBuffer.getWritePointer(ch);

        for (int i = 0; i < numSamples; ++i)
        {
            input[i] = sanitizeAudioSample(input[i]);
            dry[i] = input[i];
        }
    }

    simCodec.process(leftChannel, rightChannel, numSamples);
    decodeOk.store(false, std::memory_order_relaxed);

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
