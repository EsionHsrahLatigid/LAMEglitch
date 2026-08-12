/*
  ==============================================================================
    LAMEglitch - Plugin Editor Implementation
  ==============================================================================
*/

#include "PluginEditor.h"

namespace
{
float readNormalized(juce::AudioProcessorValueTreeState& apvts,
                     const char* id) noexcept
{
    if (auto* parameter = apvts.getParameter(id))
        return parameter->getValue();
    return 0.0f;
}
}

//==============================================================================
// CorruptionSlider Implementation
//==============================================================================
CorruptionSlider::CorruptionSlider(const juce::String& labelText,
                                   juce::AudioProcessorValueTreeState& apvts,
                                   const juce::String& paramID,
                                   const juce::String& suffix)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    ehl::juce_design::styleSlider(slider);
    slider.setTextValueSuffix(suffix);
    addAndMakeVisible(slider);
    
    label.setText(labelText, juce::dontSendNotification);
    ehl::juce_design::styleLabel(label);
    label.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(label);
    
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, paramID, slider);
}

void CorruptionSlider::paint(juce::Graphics&)
{
}

void CorruptionSlider::resized()
{
    auto bounds = getLocalBounds();
    label.setBounds(bounds.removeFromTop(ehl::juce_design::Metrics::labelHeight));
    bounds.removeFromTop(ehl::juce_design::Metrics::labelGap);
    slider.setBounds(bounds);
}

//==============================================================================
// LAMEglitchAudioProcessorEditor Implementation
//==============================================================================
LAMEglitchAudioProcessorEditor::LAMEglitchAudioProcessorEditor(LAMEglitchAudioProcessor& p)
    : AudioProcessorEditor(&p),
      audioProcessor(p),
      corruptionSlider("CORRUPTION", p.getAPVTS(), "corruption"),
      bitFlipSlider("BIT FLIP", p.getAPVTS(), "bitFlip"),
      byteDropSlider("BYTE DROP", p.getAPVTS(), "byteDrop"),
      frameRepeatSlider("REPEAT", p.getAPVTS(), "frameRepeat"),
      bitrateSlider("BITRATE", p.getAPVTS(), "bitrate", " kbps"),
      mixSlider("MIX", p.getAPVTS(), "mix")
{
    setLookAndFeel(&ehlLookAndFeel);
    setResizable(true, true);
    setResizeLimits(ehl::juce_design::Metrics::minimumWidth,
                    ehl::juce_design::Metrics::minimumHeight,
                    ehl::juce_design::Metrics::maximumWidth,
                    ehl::juce_design::Metrics::maximumHeight);

    addAndMakeVisible(corruptionSlider);
    addAndMakeVisible(bitFlipSlider);
    addAndMakeVisible(byteDropSlider);
    addAndMakeVisible(frameRepeatSlider);
    addAndMakeVisible(bitrateSlider);
    addAndMakeVisible(mixSlider);
    addAndMakeVisible(parameterDisplay);
    
    // Mode is attached directly to APVTS: off = real MP3 worker, on = simulation.
    modeButton.setButtonText("REAL MP3");
    ehl::juce_design::styleToggle(modeButton);
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.getAPVTS(), "mode", modeButton);
    addAndMakeVisible(modeButton);
    
    // Status label
    ehl::juce_design::styleLabel(statusLabel);
    statusLabel.setFont(juce::FontOptions(10.0f));
    statusLabel.setJustificationType(juce::Justification::centred);
    
    if (p.isCodecAvailable())
    {
        statusLabel.setText("Shine encoder ready - worker-isolated real MP3",
                           juce::dontSendNotification);
    }
    else
    {
        statusLabel.setText("MP3 encoder unavailable - simulation mode only",
                           juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, ehl::juce_design::Palette::paper());
    }
    addAndMakeVisible(statusLabel);

    ehl::juce_design::styleLabel(decodeStatusLabel);
    decodeStatusLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    decodeStatusLabel.setJustificationType(juce::Justification::centred);
    decodeStatusLabel.setText("DECODE STATUS: --", juce::dontSendNotification);
    decodeStatusLabel.setColour(juce::Label::textColourId, ehl::juce_design::Palette::mid());
    addAndMakeVisible(decodeStatusLabel);
    
    updateDisplayAndStatus();

    setSize(ehl::juce_design::Metrics::defaultWidth,
            ehl::juce_design::Metrics::defaultHeight);
    startTimerHz(15);
}

LAMEglitchAudioProcessorEditor::~LAMEglitchAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void LAMEglitchAudioProcessorEditor::timerCallback()
{
    updateDisplayAndStatus();
}

void LAMEglitchAudioProcessorEditor::updateDisplayAndStatus()
{
    auto& apvts = audioProcessor.getAPVTS();
    parameterDisplay.setValues({ readNormalized(apvts, "corruption"),
                                 readNormalized(apvts, "bitFlip"),
                                 readNormalized(apvts, "byteDrop"),
                                 readNormalized(apvts, "frameRepeat") });

    const bool realRequested = audioProcessor.isRealModeRequested();
    modeButton.setButtonText(realRequested ? "REAL MP3" : "SIMULATION");
    const auto stats = audioProcessor.getRealCodecStatistics();

    if (!realRequested)
    {
        statusLabel.setText("Simulation mode", juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, ehl::juce_design::Palette::mid());
        decodeStatusLabel.setText("DECODE STATUS: SIM", juce::dontSendNotification);
        decodeStatusLabel.setColour(juce::Label::textColourId, ehl::juce_design::Palette::mid());
    }
    else if (!audioProcessor.isCodecAvailable())
    {
        statusLabel.setText("MP3 encoder unavailable - simulation fallback",
                            juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, ehl::juce_design::Palette::paper());
        decodeStatusLabel.setText("DECODE STATUS: UNAVAILABLE", juce::dontSendNotification);
        decodeStatusLabel.setColour(juce::Label::textColourId, ehl::juce_design::Palette::paper());
    }
    else if (audioProcessor.isDecodeOk())
    {
        statusLabel.setText("frames "
                                + juce::String(stats.processedFrames)
                                + " / bytes "
                                + juce::String(stats.encodedBytes)
                                + " / "
                                + juce::String(stats.activeBitrate)
                                + " kbps",
                            juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, ehl::juce_design::Palette::mid());
        decodeStatusLabel.setText("DECODE STATUS: OK", juce::dontSendNotification);
        decodeStatusLabel.setColour(juce::Label::textColourId, ehl::juce_design::Palette::paper());
    }
    else
    {
        decodeStatusLabel.setText("DECODE STATUS: FAIL", juce::dontSendNotification);
        decodeStatusLabel.setColour(juce::Label::textColourId, ehl::juce_design::Palette::paper());
        statusLabel.setText("frames "
                                + juce::String(stats.processedFrames)
                                + " / drops "
                                + juce::String(stats.outputOverruns)
                                + " / "
                                + juce::String(stats.activeBitrate)
                                + " kbps",
                            juce::dontSendNotification);
    }
}

void LAMEglitchAudioProcessorEditor::paint(juce::Graphics& g)
{
    ehl::juce_design::paintEditorChrome(g, getLocalBounds(), "LAMEGLITCH",
                                        "WORKER-ISOLATED MP3 CORRUPTION");
}

void LAMEglitchAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    parameterDisplay.setBounds(ehl::juce_design::parameterDisplayArea(bounds));

    // Status bar at bottom
    auto statusArea = bounds.removeFromBottom(40).reduced(ehl::juce_design::Metrics::margin, 0);
    decodeStatusLabel.setBounds(statusArea.removeFromLeft(176));
    statusLabel.setBounds(statusArea);
    
    // Mode button
    auto modeArea = bounds.removeFromBottom(36);
    modeButton.setBounds(modeArea.withSizeKeepingCentre(144, 28));
    
    // Sliders area
    bounds.removeFromTop(ehl::juce_design::Metrics::controlsTop);
    bounds.reduce(ehl::juce_design::Metrics::margin, 8);
    
    const int sliderWidth = bounds.getWidth() / 3;
    const int sliderHeight = bounds.getHeight() / 2;
    
    auto row1 = bounds.removeFromTop(sliderHeight);
    corruptionSlider.setBounds(row1.removeFromLeft(sliderWidth));
    bitFlipSlider.setBounds(row1.removeFromLeft(sliderWidth));
    byteDropSlider.setBounds(row1.removeFromLeft(sliderWidth));
    
    auto row2 = bounds;
    frameRepeatSlider.setBounds(row2.removeFromLeft(sliderWidth));
    bitrateSlider.setBounds(row2.removeFromLeft(sliderWidth));
    mixSlider.setBounds(row2.removeFromLeft(sliderWidth));
}
