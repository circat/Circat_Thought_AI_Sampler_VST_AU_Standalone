#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

class CircatThoughtEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CircatThoughtEditor (CircatThoughtProcessor&);
    ~CircatThoughtEditor() override = default;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    CircatThoughtProcessor& processor;
    juce::TextEditor prompt;
    juce::TextButton generate { "GENERATE" };
    juce::TextButton loadModel { "LOAD MODEL" }, unloadModel { "UNLOAD" };
    juce::TextButton autoSlice { "AUTO SLICE" };
    juce::TextButton savePreset { "SAVE" }, loadPreset { "LOAD" };
    juce::TextButton saveSample { "EXPORT WAV" };
    juce::TextButton reference { "REFERENCE AUDIO" };
    juce::Label status;
    juce::Label generationParameters;
    juce::Label commandView;
    double generationProgress = 0.0;
    juce::ProgressBar generationProgressBar { generationProgress };
    juce::int64 generationStartedMs = 0;
    juce::ComboBox promptPreset;
    juce::Slider aiDuration, aiSteps, aiCfg, aiSeed;
    juce::Label durationLabel { {}, "SEC" }, stepsLabel { {}, "STEPS" }, cfgLabel { {}, "CFG" }, seedLabel { {}, "SEED" };
    juce::Label referenceStatus;
    juce::Label startLabel { {}, "START" }, endLabel { {}, "END" };
    juce::Label loopStartLabel { {}, "LOOP IN" }, loopEndLabel { {}, "LOOP OUT" }, loopFadeLabel { {}, "X-FADE" }, loopModeLabel { {}, "LOOP" };
    juce::Slider start, end, loopStart, loopEnd, loopFade;
    juce::ComboBox loopMode;
    juce::Slider attack, decay, sustain, release, drive;
    juce::Slider outputGain;
    double outputMeter = 0.0;
    juce::ProgressBar outputMeterBar { outputMeter };
    juce::ComboBox filterMode;
    juce::Slider cutoff, resonance, filterAttack, filterDecay, filterSustain, filterRelease, filterAmount;
    juce::Label attackLabel { {}, "A" }, decayLabel { {}, "D" }, sustainLabel { {}, "S" }, releaseLabel { {}, "R" }, driveLabel { {}, "WULF DRIVE" }, outputLabel { {}, "OUTPUT dB" };
    juce::Label filterLabel { {}, "FILTER" }, cutoffLabel { {}, "CUT" }, resonanceLabel { {}, "RES" }, filterAttackLabel { {}, "FA" }, filterDecayLabel { {}, "FD" }, filterSustainLabel { {}, "FS" }, filterReleaseLabel { {}, "FR" }, filterAmountLabel { {}, "ENV" };
    std::unique_ptr<juce::FileChooser> referenceChooser;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CircatThoughtEditor)
};
