/*
  ==============================================================================
    LAMEglitch - Plugin Editor Implementation
  ==============================================================================
*/

#include "PluginEditor.h"

//==============================================================================
// CorruptionSlider Implementation
//==============================================================================
CorruptionSlider::CorruptionSlider(const juce::String& labelText,
                                   juce::AudioProcessorValueTreeState& apvts,
                                   const juce::String& paramID,
                                   const juce::String& suffix)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
    slider.setTextValueSuffix(suffix);
    
    // Cyberpunk color scheme
    slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xffff3366));
    slider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff222233));
    slider.setColour(juce::Slider::thumbColourId, juce::Colour(0xff00ffcc));
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffff3366));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(slider);
    
    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    label.setFont(juce::Font(11.0f, juce::Font::bold));
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
    label.setBounds(bounds.removeFromTop(18));
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
    addAndMakeVisible(corruptionSlider);
    addAndMakeVisible(bitFlipSlider);
    addAndMakeVisible(byteDropSlider);
    addAndMakeVisible(frameRepeatSlider);
    addAndMakeVisible(bitrateSlider);
    addAndMakeVisible(mixSlider);
    
    // Mode is attached directly to APVTS: off = real MP3 worker, on = simulation.
    modeButton.setButtonText("REAL MP3");
    modeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333344));
    modeButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffff3366));
    modeButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff00ffcc));
    modeButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffffffff));
    modeButton.setClickingTogglesState(true);
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.getAPVTS(), "mode", modeButton);
    addAndMakeVisible(modeButton);
    
    // Status label
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff666677));
    statusLabel.setFont(juce::Font(10.0f));
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
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff6666));
    }
    addAndMakeVisible(statusLabel);

    decodeStatusLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    decodeStatusLabel.setJustificationType(juce::Justification::centred);
    decodeStatusLabel.setText("DECODE STATUS: --", juce::dontSendNotification);
    decodeStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff777777));
    addAndMakeVisible(decodeStatusLabel);
    
    // Initialize visualizer data
    waveformData.resize(256, 0.0f);
    corruptedBytesViz.resize(64, 0);
    
    setSize(650, 450);
    startTimerHz(30);
}

LAMEglitchAudioProcessorEditor::~LAMEglitchAudioProcessorEditor()
{
    stopTimer();
}

void LAMEglitchAudioProcessorEditor::timerCallback()
{
    vizPhase++;
    
    float targetIntensity = *audioProcessor.getAPVTS().getRawParameterValue("corruption");
    glitchIntensity += (targetIntensity - glitchIntensity) * 0.15f;
    
    // Update waveform visualization
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::uniform_int_distribution<int> byteDist(0, 255);
    
    for (size_t i = 0; i < waveformData.size(); ++i)
    {
        float wave = std::sin(vizPhase * 0.08f + i * 0.15f) * 0.7f;
        
        // Add glitch artifacts
        if (dist(rng) + 0.5f < glitchIntensity * 0.5f)
        {
            wave = dist(rng);
        }
        
        // Frame repeat simulation
        if (glitchIntensity > 0.3f && (vizPhase % 20 < static_cast<int>(glitchIntensity * 10)))
        {
            if (i > 0) wave = waveformData[i - 1];
        }
        
        waveformData[i] = wave;
    }
    
    // Update hex dump visualization
    for (size_t i = 0; i < corruptedBytesViz.size(); ++i)
    {
        if (dist(rng) + 0.5f < glitchIntensity * 0.3f)
        {
            corruptedBytesViz[i] = static_cast<uint8_t>(byteDist(rng));
        }
        else
        {
            // Slowly cycle through "normal" MP3 frame header bytes
            corruptedBytesViz[i] = static_cast<uint8_t>((0xFF - i * 4 + vizPhase) % 256);
        }
    }

    const bool realRequested = audioProcessor.isRealModeRequested();
    modeButton.setButtonText(realRequested ? "REAL MP3" : "SIMULATION");

    if (!realRequested)
    {
        statusLabel.setText("Simulation mode", juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff666677));
        decodeStatusLabel.setText("DECODE STATUS: SIM", juce::dontSendNotification);
        decodeStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff777777));
    }
    else if (!audioProcessor.isCodecAvailable())
    {
        statusLabel.setText("MP3 encoder unavailable - simulation fallback",
                            juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff6666));
        decodeStatusLabel.setText("DECODE STATUS: UNAVAILABLE", juce::dontSendNotification);
        decodeStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff3355));
    }
    else if (audioProcessor.isDecodeOk())
    {
        statusLabel.setText("Real MP3 worker active", juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff666677));
        decodeStatusLabel.setText("DECODE STATUS: OK", juce::dontSendNotification);
        decodeStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff33ff77));
    }
    else
    {
        decodeStatusLabel.setText("DECODE STATUS: FAIL", juce::dontSendNotification);
        decodeStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff3355));
    }
    
    repaint(0, 0, getWidth(), 170);
}

void LAMEglitchAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Background gradient
    juce::ColourGradient bgGradient(
        juce::Colour(0xff0a0a12), 0.0f, 0.0f,
        juce::Colour(0xff151520), 0.0f, static_cast<float>(getHeight()),
        false);
    g.setGradientFill(bgGradient);
    g.fillAll();
    
    // Grid pattern
    g.setColour(juce::Colour(0x15ffffff));
    for (int x = 0; x < getWidth(); x += 25)
    {
        g.drawVerticalLine(x, 0.0f, static_cast<float>(getHeight()));
    }
    for (int y = 0; y < getHeight(); y += 25)
    {
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(getWidth()));
    }
    
    // Title with glitch effect
    g.setFont(juce::Font(32.0f, juce::Font::bold));
    
    // Glitch shadow
    if (glitchIntensity > 0.3f && (vizPhase % 7 < 2))
    {
        g.setColour(juce::Colour(0xff00ffcc).withAlpha(0.5f));
        g.drawText("LAME GLITCH", 12 + (rng() % 3), 8, 250, 35, juce::Justification::left);
        
        g.setColour(juce::Colour(0xffff3366).withAlpha(0.5f));
        g.drawText("LAME GLITCH", 8 - (rng() % 3), 12, 250, 35, juce::Justification::left);
    }
    
    g.setColour(juce::Colour(0xffff3366));
    g.drawText("LAME GLITCH", 10, 10, 250, 35, juce::Justification::left);
    
    // Subtitle
    g.setFont(juce::Font(11.0f));
    g.setColour(juce::Colour(0xff00ffcc));
    g.drawText("WORKER-ISOLATED MP3 CORRUPTION", 10, 42, 300, 15, juce::Justification::left);
    
    // Waveform visualizer area
    juce::Rectangle<float> waveBounds(10.0f, 60.0f, getWidth() - 20.0f, 50.0f);
    
    // Background
    g.setColour(juce::Colour(0xff0a0a15));
    g.fillRoundedRectangle(waveBounds, 4.0f);
    
    // Waveform
    juce::Path wavePath;
    float centerY = waveBounds.getCentreY();
    float stepX = waveBounds.getWidth() / waveformData.size();
    
    wavePath.startNewSubPath(waveBounds.getX(), centerY);
    
    for (size_t i = 0; i < waveformData.size(); ++i)
    {
        float x = waveBounds.getX() + i * stepX;
        float y = centerY + waveformData[i] * waveBounds.getHeight() * 0.4f;
        wavePath.lineTo(x, y);
    }
    
    g.setColour(juce::Colour(0xffff3366));
    g.strokePath(wavePath, juce::PathStrokeType(1.5f));
    
    // Hex dump visualizer
    juce::Rectangle<float> hexBounds(10.0f, 115.0f, getWidth() - 20.0f, 45.0f);
    
    g.setColour(juce::Colour(0xff0a0a15));
    g.fillRoundedRectangle(hexBounds, 4.0f);
    
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
    
    float hexX = hexBounds.getX() + 5;
    float hexY = hexBounds.getY() + 12;
    
    for (size_t i = 0; i < corruptedBytesViz.size(); ++i)
    {
        uint8_t byte = corruptedBytesViz[i];
        
        // Color based on "corruption"
        if (byte == 0xFF || byte == 0xFB) // MP3 sync bytes
        {
            g.setColour(juce::Colour(0xff00ffcc));
        }
        else if (byte == 0x00)
        {
            g.setColour(juce::Colour(0xff666677));
        }
        else if (glitchIntensity > 0.5f && (i % 8 < static_cast<size_t>(glitchIntensity * 5)))
        {
            g.setColour(juce::Colour(0xffff3366)); // Corrupted
        }
        else
        {
            g.setColour(juce::Colour(0xff888899));
        }
        
        juce::String hexStr = juce::String::toHexString(byte).paddedLeft('0', 2).toUpperCase();
        g.drawText(hexStr, hexX, hexY, 20, 12, juce::Justification::left);
        
        hexX += 22;
        if ((i + 1) % 16 == 0)
        {
            hexX = hexBounds.getX() + 5;
            hexY += 14;
        }
        else if ((i + 1) % 8 == 0)
        {
            hexX += 8; // Extra space between groups
        }
    }
    
    // Glitch blocks
    if (glitchIntensity > 0.4f)
    {
        int numBlocks = static_cast<int>(glitchIntensity * 4);
        for (int i = 0; i < numBlocks; ++i)
        {
            if (rng() % 4 == 0)
            {
                int bx = rng() % getWidth();
                int by = rng() % 160;
                int bw = 20 + rng() % 60;
                int bh = 2 + rng() % 6;
                
                g.setColour(juce::Colour(
                    static_cast<uint8_t>(rng() % 255),
                    static_cast<uint8_t>(rng() % 255),
                    static_cast<uint8_t>(rng() % 255),
                    static_cast<uint8_t>(30 + rng() % 50)));
                g.fillRect(bx, by, bw, bh);
            }
        }
    }
    
    // Scanlines
    g.setColour(juce::Colour(0x08000000));
    for (int y = 0; y < getHeight(); y += 2)
    {
        g.fillRect(0, y, getWidth(), 1);
    }
    
    // Border
    g.setColour(juce::Colour(0xffff3366));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 4.0f, 2.0f);
}

void LAMEglitchAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    
    // Top area for visualizers (reserved in paint)
    bounds.removeFromTop(170);
    
    // Status bar at bottom
    auto statusArea = bounds.removeFromBottom(45);
    decodeStatusLabel.setBounds(statusArea.removeFromTop(20).reduced(10, 0));
    statusLabel.setBounds(statusArea.removeFromTop(20).reduced(10, 0));
    
    // Mode button
    auto modeArea = bounds.removeFromBottom(40);
    modeButton.setBounds(modeArea.withSizeKeepingCentre(150, 30));
    
    // Sliders area
    bounds.reduce(10, 10);
    
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
