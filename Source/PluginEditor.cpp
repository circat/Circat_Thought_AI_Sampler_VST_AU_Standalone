#include "PluginEditor.h"

CircatThoughtEditor::CircatThoughtEditor (CircatThoughtProcessor& p) : AudioProcessorEditor (&p), processor (p)
{
    setSize (1280, 800);
    prompt.setText (processor.getPrompt());
    prompt.setMultiLine (true);
    prompt.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff17191d));
    prompt.setColour (juce::TextEditor::textColourId, juce::Colour (0xffd8e6d0));
    addAndMakeVisible (prompt);
    generate.onClick = [this] { processor.generate (prompt.getText()); };
    addAndMakeVisible (generate);
    loadModel.onClick = [this] { processor.loadAiModel(); };
    unloadModel.onClick = [this] { processor.unloadAiModel(); };
    addAndMakeVisible (loadModel); addAndMakeVisible (unloadModel);
    status.setJustificationType (juce::Justification::centredLeft);
    status.setColour (juce::Label::textColourId, juce::Colour (0xff9ed6b4));
    addAndMakeVisible (status);
    generationParameters.setText (
        "ONE-SHOT  ·  3.0 s target  ·  8 steps  ·  CFG 7.0  ·  random seed\n"
        "Lyrics: [hit] [silence]  ·  auto-slice: strongest hit, max. 2.5 s\n"
        "Auto tags: solo instrument · one isolated event · no loop · dry mix · zero reverb · direct input · mono compatible · no drums",
        juce::dontSendNotification);
    generationParameters.setJustificationType (juce::Justification::centredLeft);
    generationParameters.setColour (juce::Label::textColourId, juce::Colour (0xff858f96));
    generationParameters.setFont (juce::Font (13.0f));
    addAndMakeVisible (generationParameters);
    referenceStatus.setText ("ONE-SHOT MODE · Prompt templates: NOTE / CHORD / STAB / TEXTURE", juce::dontSendNotification);
    referenceStatus.setColour (juce::Label::textColourId, juce::Colour (0xffd7b76d));
    addAndMakeVisible (referenceStatus);
    for (auto* slider : { &start, &end })
    {
        slider->setRange (0.0, 1.0, 0.001);
        slider->setSliderStyle (juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
        slider->setColour (juce::Slider::trackColourId, juce::Colour (0xff9ed6b4));
        slider->setColour (juce::Slider::thumbColourId, juce::Colour (0xffd8e6d0));
        addAndMakeVisible (*slider);
    }
    start.setValue (processor.getSampleStart(), juce::dontSendNotification);
    end.setValue (processor.getSampleEnd(), juce::dontSendNotification);
    start.onValueChange = [this] { processor.setSampleRegion ((float) start.getValue(), (float) end.getValue()); };
    end.onValueChange = [this] { processor.setSampleRegion ((float) start.getValue(), (float) end.getValue()); };
    for (auto* label : { &startLabel, &endLabel })
    {
        label->setColour (juce::Label::textColourId, juce::Colour (0xff858f96));
        addAndMakeVisible (*label);
    }
    for (auto* slider : { &attack, &decay, &sustain, &release, &drive })
    {
        slider->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 18);
        slider->setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff9ed6b4));
        addAndMakeVisible (*slider);
    }
    attack.setRange (0.001, 5.0, 0.001); attack.setValue (0.005);
    decay.setRange (0.001, 5.0, 0.001); decay.setValue (0.15);
    sustain.setRange (0.0, 1.0, 0.001); sustain.setValue (0.85);
    release.setRange (0.001, 10.0, 0.001); release.setValue (0.25);
    drive.setRange (0.0, 24.0, 0.1); drive.setValue (0.0);
    const auto updateEnvelope = [this] { processor.setAmpEnvelope ((float) attack.getValue(), (float) decay.getValue(), (float) sustain.getValue(), (float) release.getValue()); };
    attack.onValueChange = updateEnvelope; decay.onValueChange = updateEnvelope; sustain.onValueChange = updateEnvelope; release.onValueChange = updateEnvelope;
    drive.onValueChange = [this] { processor.setInputDriveDb ((float) drive.getValue()); };
    for (auto* label : { &attackLabel, &decayLabel, &sustainLabel, &releaseLabel, &driveLabel })
    {
        label->setJustificationType (juce::Justification::centred);
        label->setColour (juce::Label::textColourId, juce::Colour (0xff858f96));
        addAndMakeVisible (*label);
    }
    timerCallback();
    startTimerHz (8);
}

void CircatThoughtEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.fillAll (juce::Colour (0xff1d0d12));
    juce::ColourGradient velvet (juce::Colour (0xff4a222d), 0.0f, 44.0f, juce::Colour (0xff2d121a), 0.0f, bounds.getBottom(), false);
    g.setGradientFill (velvet); g.fillRect (bounds);
    g.setColour (juce::Colour (0xffd9a557)); g.fillRect (0.0f, 43.0f, bounds.getWidth(), 1.0f);
    g.setFont (juce::Font (20.0f, juce::Font::bold));
    g.drawText ("CIRCAT THOUGHT", 24, 10, 310, 28, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xffcbd0d7)); g.setFont (12.0f);
    g.drawText ("AI SAMPLER  /  STABLE AUDIO OPEN", 340, 14, 320, 20, juce::Justification::centredLeft);

    const int top = 60, left = 24, gutter = 16, width = (getWidth() - left * 2 - gutter * 3) / 4;
    const auto card = [&g] (juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (juce::Colour (0x8f160a0f)); g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (juce::Colour (0x44ffffff)); g.drawRoundedRectangle (r.toFloat(), 6.0f, 1.0f);
        g.setColour (juce::Colour (0xffd9a557)); g.setFont (11.0f);
        g.drawText (title, r.reduced (16).removeFromTop (20), juce::Justification::centredLeft);
    };
    card ({ left, top, width, getHeight() - top - 24 }, "01 // AI / INPUT");
    card ({ left + (width + gutter), top, width * 2 + gutter, getHeight() - top - 24 }, "02 // SAMPLE / WAVEFORM");
    card ({ left + 3 * (width + gutter), top, width, getHeight() - top - 24 }, "03 // AMP / CHARACTER");

    const auto wave = juce::Rectangle<int> (left + width + gutter + 18, top + 48, width * 2 + gutter - 36, 190);
    g.setColour (juce::Colour (0xff10090c)); g.fillRoundedRectangle (wave.toFloat(), 4.0f);
    g.setColour (juce::Colour (0x33d9a557));
    for (int y = wave.getY() + 16; y < wave.getBottom(); y += 12) g.drawHorizontalLine (y, (float) wave.getX(), (float) wave.getRight());
    if (const auto* sample = processor.getSampleForDisplay())
    {
        const auto& audio = sample->audio;
        g.setColour (juce::Colour (0xffd9a557));
        for (int x = 0; x < wave.getWidth(); ++x)
        {
            const int a = x * audio.getNumSamples() / wave.getWidth();
            const int b = juce::jmin (audio.getNumSamples(), (x + 1) * audio.getNumSamples() / wave.getWidth());
            float peak = 0.0f;
            for (int n = a; n < b; ++n) peak = juce::jmax (peak, std::abs (audio.getSample (0, n)));
            const float half = peak * (float) wave.getHeight() * 0.42f;
            g.drawVerticalLine (wave.getX() + x, wave.getCentreY() - half, wave.getCentreY() + half);
        }
    }
    else { g.setColour (juce::Colour (0xff858f96)); g.drawFittedText ("GENERATE A SAMPLE", wave, juce::Justification::centred, 1); }
}

void CircatThoughtEditor::resized()
{
    const int top = 60, left = 24, gutter = 16, width = (getWidth() - left * 2 - gutter * 3) / 4;
    auto ai = juce::Rectangle<int> (left + 16, top + 44, width - 32, getHeight() - top - 68);
    status.setBounds (ai.removeFromTop (24)); ai.removeFromTop (8);
    generationParameters.setBounds (ai.removeFromTop (62));
    referenceStatus.setBounds (ai.removeFromTop (30)); ai.removeFromTop (10);
    loadModel.setBounds (ai.removeFromTop (28).removeFromLeft (110));
    unloadModel.setBounds (ai.removeFromTop (28).removeFromLeft (90));
    ai.removeFromTop (8);
    generate.setBounds (ai.removeFromBottom (36).removeFromRight (120)); ai.removeFromBottom (10);
    prompt.setBounds (ai);
    auto sampleControls = juce::Rectangle<int> (left + width + gutter + 18, top + 250, width * 2 + gutter - 36, 86);
    startLabel.setBounds (sampleControls.removeFromTop (22).removeFromLeft (62)); start.setBounds (sampleControls.removeFromTop (22)); sampleControls.removeFromTop (8);
    endLabel.setBounds (sampleControls.removeFromTop (22).removeFromLeft (62)); end.setBounds (sampleControls.removeFromTop (22));
    auto sound = juce::Rectangle<int> (left + 3 * (width + gutter) + 16, top + 44, width - 32, 180);
    auto placeKnob = [&sound] (juce::Label& label, juce::Slider& slider)
    {
        auto cell = sound.removeFromLeft (78);
        label.setBounds (cell.removeFromTop (18)); slider.setBounds (cell);
    };
    placeKnob (attackLabel, attack); placeKnob (decayLabel, decay); placeKnob (sustainLabel, sustain); placeKnob (releaseLabel, release);
    auto driveCell = sound.removeFromLeft (110); driveLabel.setBounds (driveCell.removeFromTop (18)); drive.setBounds (driveCell);
}

void CircatThoughtEditor::timerCallback()
{
    status.setText (processor.getGenerationStatusText(), juce::dontSendNotification);
    // Newly generated sample data is swapped on the worker thread. Repaint the
    // display on the message thread so the waveform visibly confirms the load.
    repaint();
}
