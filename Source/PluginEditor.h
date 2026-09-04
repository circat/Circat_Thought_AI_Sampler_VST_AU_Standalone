#pragma once

#include "PluginProcessor.h"
#include "GeneratedBrowser.h"
#include "PromptMixer.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

#if CIRCAT_HAS_ABOUT
 #include <CircatAboutPanel.h>
#endif

/** Minimal horizontal level bar. Value is 0..1 already mapped to a dB scale. */
class BarMeter final : public juce::Component
{
public:
    void setValue (double v) { value = juce::jlimit (0.0, 1.0, v); repaint(); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff17191d));
        g.fillRoundedRectangle (r, 2.0f);
        auto fill = r.reduced (1.0f);
        fill = fill.withWidth (fill.getWidth() * (float) value);
        juce::Colour c = value > 0.92 ? juce::Colour (0xffe1554f)
                       : value > 0.78 ? juce::Colour (0xfff3a340)
                                      : juce::Colour (0xff9ed6b4);
        g.setColour (c);
        g.fillRoundedRectangle (fill, 2.0f);
    }
private:
    double value = 0.0;
};

// Shared Circat palette, aligned with the S612 / AKAK master (CircatTokens.h).
namespace ct
{
    inline const juce::Colour shell      { 0xff121417 };
    inline const juce::Colour velvetTop  { 0xff37302a };
    inline const juce::Colour velvetBot  { 0xff201914 };
    inline const juce::Colour card       { 0xff1a1d24 };
    inline const juce::Colour cardBorder { juce::Colour::fromRGBA (255, 255, 255, 26) };
    inline const juce::Colour accent     { 0xffd9a557 };
    inline const juce::Colour accentDim  { 0xff9c6a38 };
    // S612 master section tints (colour-coded headers + trailing rules).
    inline const juce::Colour secInput   { 0xffb07e28 };
    inline const juce::Colour secSample  { 0xff1e8c80 };
    inline const juce::Colour secShape   { 0xff5442a0 };
    inline const juce::Colour secOut     { 0xff4fa868 };
    inline const juce::Colour textPrimary{ 0xffe8eaed };
    inline const juce::Colour textLabel  { 0xffcbd0d7 };
    inline const juce::Colour textDim    { 0xff9ba1ab };
    inline const juce::Colour control    { 0xff232732 };
    inline const juce::Colour filterArc  { 0xff63b4a6 };
}

// Knob / button / combo styling to match the S612 AKAK master look.
class CircatThoughtLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    CircatThoughtLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, ct::textLabel);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ComboBox::backgroundColourId, ct::control);
        setColour (juce::ComboBox::textColourId, ct::textPrimary);
        setColour (juce::ComboBox::outlineColourId, ct::cardBorder);
        setColour (juce::ComboBox::arrowColourId, ct::accent);
        setColour (juce::PopupMenu::backgroundColourId, ct::card);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, ct::accentDim);
        setColour (juce::TextButton::buttonColourId, ct::control);
        setColour (juce::TextButton::textColourOnId, ct::accent);
        setColour (juce::TextButton::textColourOffId, ct::textLabel);
        setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff17191d));
        setColour (juce::TextEditor::textColourId, ct::textPrimary);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float pos, float startAngle, float endAngle,
                           juce::Slider& s) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto angle = startAngle + pos * (endAngle - startAngle);
        const auto arc = s.findColour (juce::Slider::rotarySliderFillColourId, true);
        const auto fill = arc == juce::Colour() ? ct::accent : arc;

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius - 3.0f, radius - 3.0f, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 20));
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius - 3.0f, radius - 3.0f, 0.0f, startAngle, angle, true);
        g.setColour (fill);
        g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (ct::control);
        g.fillEllipse (juce::Rectangle<float> (radius, radius).withCentre (centre).reduced (2.0f));
        g.setColour (ct::cardBorder);
        g.drawEllipse (juce::Rectangle<float> (radius, radius).withCentre (centre).reduced (2.0f), 1.0f);

        juce::Path pointer;
        pointer.addRoundedRectangle (-1.2f, -radius + 4.0f, 2.4f, radius * 0.5f, 1.0f);
        g.setColour (ct::textPrimary);
        g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
    }
};

class CircatThoughtEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CircatThoughtEditor (CircatThoughtProcessor&);
    ~CircatThoughtEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void openBrowser();
    void openMixer();
    CircatThoughtLookAndFeel lookAndFeel;
    CircatThoughtProcessor& processor;
    juce::TextEditor prompt;
    juce::TextButton generate { "GENERATE" };
    juce::TextButton browseSamples { "SAMPLE BROWSER" };
    juce::TextButton promptBuilder { "PROMPT BUILDER" };
    juce::TextButton autoSlice { "AUTO SLICE" };
    juce::TextButton savePreset { "SAVE" }, loadPreset { "LOAD" };
    juce::TextButton saveSample { "EXPORT WAV" };
    juce::TextButton reference { "REFERENCE AUDIO" };
    juce::Label status;
    juce::Label commandView;
    double generationProgress = 0.0;
    juce::ProgressBar generationProgressBar { generationProgress };
    juce::int64 generationStartedMs = 0;
    juce::ComboBox qualityMode;
    juce::Label qualityLabel { {}, "QUALITY" };
    juce::Slider aiDuration, aiSteps, aiCfg, aiSeed;
    juce::Label durationLabel { {}, "SEC" }, stepsLabel { {}, "STEPS" }, cfgLabel { {}, "CFG" }, seedLabel { {}, "SEED" };
    juce::Label referenceStatus;
    juce::Label startLabel { {}, "START" }, endLabel { {}, "END" };
    juce::Label loopStartLabel { {}, "LOOP IN" }, loopEndLabel { {}, "LOOP OUT" }, loopFadeLabel { {}, "X-FADE" }, loopModeLabel { {}, "LOOP" };
    juce::Slider start, end, loopStart, loopEnd, loopFade;
    juce::ComboBox loopMode;
    juce::Slider fineTune, semitone;
    juce::TextButton octaveDown { juce::CharPointer_UTF8 ("OCT \xE2\x80\x93") }, octaveUp { "OCT +" };
    juce::Label fineTuneLabel { {}, "FINE" }, semitoneLabel { {}, "SEMI" }, octaveLabel { {}, "OCTAVE" }, octaveValue;
    juce::Slider attack, decay, sustain, release, drive;
    juce::Slider outputGain;
    double outputMeter = 0.0;
    BarMeter outputMeterBar;
    juce::ComboBox filterMode;
    juce::Slider cutoff, resonance, filterAttack, filterDecay, filterSustain, filterRelease, filterAmount;
    juce::Label attackLabel { {}, "A" }, decayLabel { {}, "D" }, sustainLabel { {}, "S" }, releaseLabel { {}, "R" }, driveLabel { {}, "WULF DRIVE" }, outputLabel { {}, "OUTPUT dB" };
    juce::Label filterLabel { {}, "FILTER" }, cutoffLabel { {}, "CUT" }, resonanceLabel { {}, "RES" }, filterAttackLabel { {}, "FA" }, filterDecayLabel { {}, "FD" }, filterSustainLabel { {}, "FS" }, filterReleaseLabel { {}, "FR" }, filterAmountLabel { {}, "ENV" };
    std::unique_ptr<juce::FileChooser> referenceChooser;
    std::unique_ptr<GeneratedBrowser> browser;
    std::unique_ptr<PromptMixer> mixer;
    juce::Rectangle<int> logoHit;
    double animPhase = 0.0;
#if CIRCAT_HAS_ABOUT
    circat::AboutPanel about;
    std::unique_ptr<juce::Drawable> cornerLogo;
#endif
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CircatThoughtEditor)
};
