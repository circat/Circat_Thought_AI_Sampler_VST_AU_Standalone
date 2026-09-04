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
    juce::TextButton reference { "REFERENCE AUDIO" };
    juce::Label status;
    juce::Label generationParameters;
    juce::Label referenceStatus;
    juce::Label startLabel { {}, "START" }, endLabel { {}, "END" };
    juce::Slider start, end;
    juce::Slider attack, decay, sustain, release, drive;
    juce::Label attackLabel { {}, "A" }, decayLabel { {}, "D" }, sustainLabel { {}, "S" }, releaseLabel { {}, "R" }, driveLabel { {}, "WULF DRIVE" };
    std::unique_ptr<juce::FileChooser> referenceChooser;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CircatThoughtEditor)
};
