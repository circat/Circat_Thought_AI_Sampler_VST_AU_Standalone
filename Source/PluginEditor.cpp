#include "PluginEditor.h"

#include <cmath>

CircatThoughtEditor::CircatThoughtEditor (CircatThoughtProcessor& p) : AudioProcessorEditor (&p), processor (p)
{
    setSize (1280, 800);
    setLookAndFeel (&lookAndFeel);
    prompt.setText (processor.getPrompt());
    prompt.setMultiLine (true);
    prompt.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff17191d));
    prompt.setColour (juce::TextEditor::textColourId, juce::Colour (0xffd8e6d0));
    addAndMakeVisible (prompt);
    generate.onClick = [this]
    {
        generationStartedMs = juce::Time::currentTimeMillis(); generationProgress = 0.02;
        processor.generate (prompt.getText(), (float) aiDuration.getValue(), (int) aiSteps.getValue(), (float) aiCfg.getValue(), (int) aiSeed.getValue());
    };
    addAndMakeVisible (generate);
    loadModel.onClick = [this] { processor.loadAiModel(); };
    unloadModel.onClick = [this] { processor.unloadAiModel(); };
    addAndMakeVisible (loadModel); addAndMakeVisible (unloadModel);
    status.setJustificationType (juce::Justification::centredLeft);
    status.setColour (juce::Label::textColourId, juce::Colour (0xff9ed6b4));
    addAndMakeVisible (status);
    commandView.setJustificationType (juce::Justification::centredLeft);
    commandView.setColour (juce::Label::backgroundColourId, juce::Colour (0xff0b0d0f));
    commandView.setColour (juce::Label::textColourId, juce::Colour (0xff9ed6b4));
    commandView.setFont (juce::Font (11.0f)); addAndMakeVisible (commandView);
    generationProgressBar.setColour (juce::ProgressBar::foregroundColourId, juce::Colour (0xffd9a557));
    generationProgressBar.setColour (juce::ProgressBar::backgroundColourId, juce::Colour (0xff17191d)); addAndMakeVisible (generationProgressBar);
    for (int i = 0; i < numPromptTemplates; ++i)
        promptPreset.addItem (promptTemplates[i].name, i + 1);
    promptPreset.setSelectedId (1);
    promptPreset.onChange = [this]
    {
        const int index = promptPreset.getSelectedId() - 1;
        if (index > 0 && index < numPromptTemplates)
            prompt.setText (promptTemplates[index].text, false);
    };
    addAndMakeVisible (promptPreset);
    for (auto* slider : { &aiDuration, &aiSteps, &aiCfg, &aiSeed })
    { slider->setSliderStyle (juce::Slider::LinearBar); slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, 20); slider->setColour (juce::Slider::trackColourId, juce::Colour (0xffd9a557)); addAndMakeVisible (*slider); }
    aiDuration.setRange (1.0, 6.0, 0.1); aiDuration.setValue (processor.getAiDuration(), juce::dontSendNotification);
    aiSteps.setRange (4, 250, 1); aiSteps.setValue (processor.getAiSteps(), juce::dontSendNotification);
    aiCfg.setRange (1.0, 12.0, 0.1); aiCfg.setValue (processor.getAiCfg(), juce::dontSendNotification);
    aiSeed.setRange (-1, 2147483646.0, 1); aiSeed.setSkewFactorFromMidPoint (1000.0);
    aiSeed.setValue (processor.getAiSeed(), juce::dontSendNotification);
    for (auto* label : { &durationLabel, &stepsLabel, &cfgLabel, &seedLabel }) { label->setColour (juce::Label::textColourId, juce::Colour (0xff858f96)); addAndMakeVisible (*label); }
    generationParameters.setText (
        "ONE-SHOT  ·  3.0 s target  ·  14 steps (pingpong)  ·  CFG 6.0  ·  random seed\n"
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
    autoSlice.onClick = [this] { processor.autoSlice(); start.setValue (processor.getSampleStart()); end.setValue (processor.getSampleEnd()); };
    addAndMakeVisible (autoSlice);
    savePreset.onClick = [this]
    {
        referenceChooser = std::make_unique<juce::FileChooser> ("Save Circat Thought preset", juce::File(), "*.ctpreset");
        referenceChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& chooser)
        { if (chooser.getResult() != juce::File()) { juce::String error; if (! processor.savePreset (chooser.getResult().withFileExtension ("ctpreset"), error)) status.setText (error, juce::dontSendNotification); } });
    };
    loadPreset.onClick = [this]
    {
        referenceChooser = std::make_unique<juce::FileChooser> ("Load Circat Thought preset", juce::File(), "*.ctpreset");
        referenceChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& chooser)
        { if (chooser.getResult().existsAsFile()) { juce::String error; if (! processor.loadPreset (chooser.getResult(), error)) status.setText (error, juce::dontSendNotification); else { prompt.setText (processor.getPrompt()); start.setValue (processor.getSampleStart()); end.setValue (processor.getSampleEnd()); } } });
    };
    saveSample.onClick = [this]
    {
        referenceChooser = std::make_unique<juce::FileChooser> ("Export sample as WAV", juce::File(), "*.wav");
        referenceChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& chooser)
        { if (chooser.getResult() != juce::File()) { juce::String error; if (! processor.saveSampleWav (chooser.getResult().withFileExtension ("wav"), error)) status.setText (error, juce::dontSendNotification); } });
    };
    addAndMakeVisible (savePreset); addAndMakeVisible (loadPreset); addAndMakeVisible (saveSample);
    for (auto* slider : { &start, &end })
    {
        slider->setRange (0.0, 1.0, 0.001);
        slider->setSliderStyle (juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
        slider->setColour (juce::Slider::trackColourId, juce::Colour (0xff9ed6b4));
        slider->setColour (juce::Slider::thumbColourId, juce::Colour (0xffd8e6d0));
        addAndMakeVisible (*slider);
    }
    for (auto* slider : { &loopStart, &loopEnd })
    {
        slider->setRange (0.0, 1.0, 0.001);
        slider->setSliderStyle (juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
        slider->setColour (juce::Slider::trackColourId, juce::Colour (0xffd9a557));
        slider->setColour (juce::Slider::thumbColourId, juce::Colour (0xffd9a557));
        addAndMakeVisible (*slider);
    }
    start.setValue (processor.getSampleStart(), juce::dontSendNotification);
    end.setValue (processor.getSampleEnd(), juce::dontSendNotification);
    loopStart.setValue (processor.getLoopStart(), juce::dontSendNotification);
    loopEnd.setValue (processor.getLoopEnd(), juce::dontSendNotification);
    start.onValueChange = [this] { processor.setSampleRegion ((float) start.getValue(), (float) end.getValue()); };
    end.onValueChange = [this] { processor.setSampleRegion ((float) start.getValue(), (float) end.getValue()); };
    loopMode.addItem ("OFF", 1); loopMode.addItem ("LOOP", 2); loopMode.addItem ("ALTERNATE", 3);
    loopMode.setSelectedId (processor.getLoopMode() + 1, juce::dontSendNotification);
    loopMode.onChange = [this] { processor.setLoop (loopMode.getSelectedId() - 1, (float) loopStart.getValue(), (float) loopEnd.getValue(), (float) loopFade.getValue()); };
    addAndMakeVisible (loopMode);
    loopFade.setRange (0.0, 0.5, 0.001); loopFade.setValue (processor.getLoopFade(), juce::dontSendNotification);
    loopFade.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); loopFade.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 18);
    loopFade.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffd9a557)); addAndMakeVisible (loopFade);
    const auto updateLoop = [this] { processor.setLoop (loopMode.getSelectedId() - 1, (float) loopStart.getValue(), (float) loopEnd.getValue(), (float) loopFade.getValue()); };
    loopStart.onValueChange = updateLoop; loopEnd.onValueChange = updateLoop; loopFade.onValueChange = updateLoop;
    for (auto* label : { &startLabel, &endLabel, &loopStartLabel, &loopEndLabel, &loopFadeLabel, &loopModeLabel })
    {
        label->setColour (juce::Label::textColourId, juce::Colour (0xff858f96));
        addAndMakeVisible (*label);
    }
    for (auto* slider : { &attack, &decay, &sustain, &release, &drive, &outputGain })
    {
        slider->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 18);
        slider->setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff9ed6b4));
        addAndMakeVisible (*slider);
    }
    attack.setRange (0.001, 5.0, 0.001); attack.setValue (processor.getAmpAttack(), juce::dontSendNotification);
    decay.setRange (0.001, 5.0, 0.001); decay.setValue (processor.getAmpDecay(), juce::dontSendNotification);
    sustain.setRange (0.0, 1.0, 0.001); sustain.setValue (processor.getAmpSustain(), juce::dontSendNotification);
    release.setRange (0.001, 10.0, 0.001); release.setValue (processor.getAmpRelease(), juce::dontSendNotification);
    drive.setRange (0.0, 24.0, 0.1); drive.setValue (processor.getDrive(), juce::dontSendNotification);
    outputGain.setRange (-60.0, 12.0, 0.1); outputGain.setValue (processor.getOutputGain(), juce::dontSendNotification);
    const auto updateEnvelope = [this] { processor.setAmpEnvelope ((float) attack.getValue(), (float) decay.getValue(), (float) sustain.getValue(), (float) release.getValue()); };
    attack.onValueChange = updateEnvelope; decay.onValueChange = updateEnvelope; sustain.onValueChange = updateEnvelope; release.onValueChange = updateEnvelope;
    drive.onValueChange = [this] { processor.setInputDriveDb ((float) drive.getValue()); };
    outputGain.onValueChange = [this] { processor.setOutputGainDb ((float) outputGain.getValue()); };
    addAndMakeVisible (outputMeterBar);
    for (auto* label : { &attackLabel, &decayLabel, &sustainLabel, &releaseLabel, &driveLabel, &outputLabel })
    {
        label->setJustificationType (juce::Justification::centred);
        label->setColour (juce::Label::textColourId, juce::Colour (0xff858f96));
        addAndMakeVisible (*label);
    }
    filterMode.addItem ("BYPASS", 1); filterMode.addItem ("LOW-PASS", 2); filterMode.addItem ("HIGH-PASS", 3); filterMode.addItem ("BAND-PASS", 4);
    filterMode.setSelectedId (processor.getFilterMode() + 1, juce::dontSendNotification);
    filterMode.onChange = [this] { processor.setFilter (filterMode.getSelectedId() - 1, (float) cutoff.getValue(), (float) resonance.getValue()); };
    addAndMakeVisible (filterMode); addAndMakeVisible (filterLabel);
    for (auto* slider : { &cutoff, &resonance, &filterAttack, &filterDecay, &filterSustain, &filterRelease, &filterAmount })
    {
        slider->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 48, 16);
        slider->setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffd9a557));
        addAndMakeVisible (*slider);
    }
    cutoff.setRange (20.0, 20000.0, 1.0); cutoff.setSkewFactorFromMidPoint (1000.0);
    cutoff.setValue (processor.getFilterCutoff(), juce::dontSendNotification);
    resonance.setRange (0.0, 1.0, 0.01); resonance.setValue (processor.getFilterResonance(), juce::dontSendNotification);
    filterAttack.setRange (0.001, 5.0, 0.001); filterAttack.setValue (processor.getFilterAttack(), juce::dontSendNotification);
    filterDecay.setRange (0.001, 5.0, 0.001); filterDecay.setValue (processor.getFilterDecay(), juce::dontSendNotification);
    filterSustain.setRange (0.0, 1.0, 0.001); filterSustain.setValue (processor.getFilterSustain(), juce::dontSendNotification);
    filterRelease.setRange (0.001, 10.0, 0.001); filterRelease.setValue (processor.getFilterRelease(), juce::dontSendNotification);
    filterAmount.setRange (-6.0, 6.0, 0.01); filterAmount.setValue (processor.getFilterAmount(), juce::dontSendNotification);
    cutoff.onValueChange = [this] { processor.setFilter (filterMode.getSelectedId() - 1, (float) cutoff.getValue(), (float) resonance.getValue()); };
    resonance.onValueChange = cutoff.onValueChange;
    const auto updateFilterEnvelope = [this] { processor.setFilterEnvelope ((float) filterAttack.getValue(), (float) filterDecay.getValue(), (float) filterSustain.getValue(), (float) filterRelease.getValue(), (float) filterAmount.getValue()); };
    filterAttack.onValueChange = updateFilterEnvelope; filterDecay.onValueChange = updateFilterEnvelope; filterSustain.onValueChange = updateFilterEnvelope; filterRelease.onValueChange = updateFilterEnvelope; filterAmount.onValueChange = updateFilterEnvelope;
    for (auto* label : { &filterLabel, &cutoffLabel, &resonanceLabel, &filterAttackLabel, &filterDecayLabel, &filterSustainLabel, &filterReleaseLabel, &filterAmountLabel })
    { label->setJustificationType (juce::Justification::centred); label->setColour (juce::Label::textColourId, juce::Colour (0xff858f96)); addAndMakeVisible (*label); }
    timerCallback();
    startTimerHz (30);
}

CircatThoughtEditor::~CircatThoughtEditor() { setLookAndFeel (nullptr); }

void CircatThoughtEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.fillAll (ct::shell);
    juce::ColourGradient velvet (ct::velvetTop, 0.0f, 44.0f, ct::velvetBot, 0.0f, bounds.getBottom(), false);
    g.setGradientFill (velvet); g.fillRect (bounds);
    g.setColour (ct::accent); g.fillRect (0.0f, 43.0f, bounds.getWidth(), 1.0f);
    g.setColour (ct::textPrimary);
    g.setFont (juce::Font (20.0f, juce::Font::bold));
    g.drawText ("CIRCAT THOUGHT", 24, 10, 310, 28, juce::Justification::centredLeft);
    g.setColour (ct::textLabel); g.setFont (12.0f);
    g.drawText ("AI SAMPLER  /  STABLE AUDIO OPEN", 340, 14, 320, 20, juce::Justification::centredLeft);

    const int top = 60, left = 24, gutter = 16, width = (getWidth() - left * 2 - gutter * 3) / 4;
    const auto card = [&g] (juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (ct::card.withAlpha (0.92f)); g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (ct::cardBorder); g.drawRoundedRectangle (r.toFloat(), 6.0f, 1.0f);
        g.setColour (ct::accent); g.setFont (11.0f);
        g.drawText (title, r.reduced (16).removeFromTop (20), juce::Justification::centredLeft);
    };
    card ({ left, top, width, getHeight() - top - 24 }, "01 // AI / INPUT");
    card ({ left + (width + gutter), top, width * 2 + gutter, getHeight() - top - 24 }, "02 // SAMPLE / WAVEFORM");
    card ({ left + 3 * (width + gutter), top, width, getHeight() - top - 24 }, "03 // AMP / FILTER");

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
    status.setBounds (ai.removeFromTop (22));
    commandView.setBounds (ai.removeFromTop (22));
    generationProgressBar.setBounds (ai.removeFromTop (8)); ai.removeFromTop (5);
    promptPreset.setBounds (ai.removeFromTop (24)); ai.removeFromTop (4);
    auto aiParam = [&ai] (juce::Label& label, juce::Slider& slider)
    { label.setBounds (ai.removeFromTop (20).removeFromLeft (52)); slider.setBounds (ai.removeFromTop (20)); };
    aiParam (durationLabel, aiDuration); aiParam (stepsLabel, aiSteps); aiParam (cfgLabel, aiCfg); aiParam (seedLabel, aiSeed);
    generationParameters.setBounds (ai.removeFromTop (42));
    referenceStatus.setBounds (ai.removeFromTop (22)); ai.removeFromTop (5);
    loadModel.setBounds (ai.removeFromTop (28).removeFromLeft (110));
    unloadModel.setBounds (ai.removeFromTop (28).removeFromLeft (90));
    ai.removeFromTop (8);
    generate.setBounds (ai.removeFromBottom (36).removeFromRight (120)); ai.removeFromBottom (8);
    prompt.setBounds (ai);
    auto sampleControls = juce::Rectangle<int> (left + width + gutter + 18, top + 250, width * 2 + gutter - 36, 160);
    startLabel.setBounds (sampleControls.removeFromTop (22).removeFromLeft (62)); start.setBounds (sampleControls.removeFromTop (22)); sampleControls.removeFromTop (8);
    endLabel.setBounds (sampleControls.removeFromTop (22).removeFromLeft (62)); end.setBounds (sampleControls.removeFromTop (22));
    sampleControls.removeFromTop (8);
    loopStartLabel.setBounds (sampleControls.removeFromTop (22).removeFromLeft (62)); loopStart.setBounds (sampleControls.removeFromTop (22));
    loopEndLabel.setBounds (sampleControls.removeFromTop (22).removeFromLeft (62)); loopEnd.setBounds (sampleControls.removeFromTop (22));
    sampleControls.removeFromTop (6);
    loopModeLabel.setBounds (sampleControls.removeFromLeft (50).removeFromTop (22)); loopMode.setBounds (sampleControls.removeFromLeft (115).removeFromTop (24));
    loopFadeLabel.setBounds (sampleControls.removeFromLeft (62).removeFromTop (20)); loopFade.setBounds (sampleControls.removeFromLeft (70).removeFromTop (70));
    autoSlice.setBounds (left + width + gutter + 18, top + 430, 110, 28);
    savePreset.setBounds (left + width + gutter + 138, top + 430, 72, 28);
    loadPreset.setBounds (left + width + gutter + 218, top + 430, 72, 28);
    saveSample.setBounds (left + width + gutter + 300, top + 430, 112, 28);
    auto sound = juce::Rectangle<int> (left + 3 * (width + gutter) + 16, top + 44, width - 32, 180);
    auto placeKnob = [] (juce::Rectangle<int> cell, juce::Label& label, juce::Slider& slider)
    {
        label.setBounds (cell.removeFromTop (18)); slider.setBounds (cell);
    };
    const int knobWidth = sound.getWidth() / 2;
    placeKnob ({ sound.getX(), sound.getY(), knobWidth, 80 }, attackLabel, attack);
    placeKnob ({ sound.getX() + knobWidth, sound.getY(), knobWidth, 80 }, decayLabel, decay);
    placeKnob ({ sound.getX(), sound.getY() + 84, knobWidth, 80 }, sustainLabel, sustain);
    placeKnob ({ sound.getX() + knobWidth, sound.getY() + 84, knobWidth, 80 }, releaseLabel, release);
    auto outputArea = juce::Rectangle<int> (left + 3 * (width + gutter) + 16, top + 225, width - 32, 78);
    auto driveCell = outputArea.removeFromLeft (112); driveLabel.setBounds (driveCell.removeFromTop (18)); drive.setBounds (driveCell);
    auto outCell = outputArea.removeFromLeft (112); outputLabel.setBounds (outCell.removeFromTop (18)); outputGain.setBounds (outCell);
    outputMeterBar.setBounds (left + 3 * (width + gutter) + 16, top + 310, width - 32, 10);
    auto filterArea = juce::Rectangle<int> (left + 3 * (width + gutter) + 16, top + 335, width - 32, 260);
    filterLabel.setBounds (filterArea.removeFromTop (18).removeFromLeft (68)); filterMode.setBounds (filterArea.removeFromTop (24)); filterArea.removeFromTop (4);
    auto placeFilterKnob = [&filterArea] (juce::Label& label, juce::Slider& slider)
    { auto cell = filterArea.removeFromLeft (70); label.setBounds (cell.removeFromTop (16)); slider.setBounds (cell.removeFromTop (76)); };
    placeFilterKnob (cutoffLabel, cutoff); placeFilterKnob (resonanceLabel, resonance);
    filterArea.removeFromTop (4);
    placeFilterKnob (filterAttackLabel, filterAttack); placeFilterKnob (filterDecayLabel, filterDecay); placeFilterKnob (filterSustainLabel, filterSustain); placeFilterKnob (filterReleaseLabel, filterRelease);
    filterArea.removeFromTop (4); placeFilterKnob (filterAmountLabel, filterAmount);
}

void CircatThoughtEditor::timerCallback()
{
    const auto text = processor.getGenerationStatusText();
    status.setText (text, juce::dontSendNotification);
    commandView.setText ("> " + text, juce::dontSendNotification);
    if (processor.getGenerationStatus() == LocalAiWorker::Status::generating)
    {
        const double elapsed = generationStartedMs == 0 ? 0.0 : (juce::Time::currentTimeMillis() - generationStartedMs) / 1000.0;
        generationProgress = juce::jmin (0.95, 0.04 + elapsed / 90.0 * 0.91);
    }
    else if (processor.getGenerationStatus() == LocalAiWorker::Status::ready)
        generationProgress = 1.0;
    else if (processor.getGenerationStatus() == LocalAiWorker::Status::error)
        generationProgress = 0.0;
    // Output meter: map the linear inter-block peak onto a -60..+6 dB scale so
    // the bar tracks perceived level instead of sitting near zero for most signals.
    const float peakLinear = processor.getAndClearOutputPeak();
    const double peakDb = peakLinear > 1.0e-6f ? 20.0 * std::log10 ((double) peakLinear) : -120.0;
    const double meterTarget = juce::jlimit (0.0, 1.0, (peakDb + 60.0) / 66.0);
    outputMeter = juce::jmax (meterTarget, outputMeter - 0.06); // ~0.5 s fall at 30 Hz
    outputMeterBar.setValue (outputMeter);
    // Newly generated sample data is swapped on the worker thread. Repaint the
    // display on the message thread so the waveform visibly confirms the load.
    repaint();
}
