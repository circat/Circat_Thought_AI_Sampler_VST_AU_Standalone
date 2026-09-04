#pragma once

#include "LocalAiWorker.h"
#include <juce_audio_processors/juce_audio_processors.h>

class CircatThoughtProcessor final : public juce::AudioProcessor
{
public:
    CircatThoughtProcessor();
    ~CircatThoughtProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Circat Thought"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Init"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    void generate (const juce::String& prompt);
    void setReferenceAudio (juce::File file) { worker.setReferenceAudio (std::move (file)); }
    void loadAiModel() { worker.loadModel(); }
    void unloadAiModel() { worker.unloadModel(); }
    void setSampleRegion (float start, float end) noexcept { sampler.setRegion (start, end); }
    void setAmpEnvelope (float attack, float decay, float sustain, float release) noexcept { sampler.setAmpEnvelope (attack, decay, sustain, release); }
    void setInputDriveDb (float db) noexcept { sampler.setInputDriveDb (db); }
    float getSampleStart() const noexcept { return sampler.getRegionStart(); }
    float getSampleEnd() const noexcept { return sampler.getRegionEnd(); }
    const ThoughtSampleData* getSampleForDisplay() const noexcept { return sampler.getSample(); }
    LocalAiWorker::Status getGenerationStatus() const noexcept { return worker.getStatus(); }
    juce::String getGenerationStatusText() const { return worker.getStatusText(); }
    juce::String getPrompt() const;

private:
    ThoughtSampler sampler;
    LocalAiWorker worker;
    mutable juce::CriticalSection stateLock;
    juce::String prompt { "Brass, D-minor chord, rising" };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CircatThoughtProcessor)
};
