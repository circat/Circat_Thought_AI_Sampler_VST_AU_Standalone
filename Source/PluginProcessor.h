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

    void generate (const juce::String& prompt, float duration = 3.0f, int steps = 14, float cfg = 6.0f, int seed = -1);
    void setReferenceAudio (juce::File file) { worker.setReferenceAudio (std::move (file)); }
    void loadAiModel() { worker.loadModel(); }
    void unloadAiModel() { worker.unloadModel(); }
    void setSampleRegion (float start, float end) noexcept;
    void setLoop (int mode, float start, float end, float fadeSeconds) noexcept;
    void setAmpEnvelope (float attack, float decay, float sustain, float release) noexcept;
    void setInputDriveDb (float db) noexcept;
    void setOutputGainDb (float db) noexcept;
    float getAndClearOutputPeak() noexcept { return sampler.getAndClearOutputPeak(); }
    void setFilter (int mode, float cutoffHz, float resonance) noexcept;
    void setFilterEnvelope (float attack, float decay, float sustain, float release, float amount) noexcept;
    void autoSlice() noexcept;
    bool loadSampleFile (const juce::File& file, juce::String& error);
    bool savePreset (const juce::File& file, juce::String& error);
    bool loadPreset (const juce::File& file, juce::String& error);
    bool saveSampleWav (const juce::File& file, juce::String& error);
    float getSampleStart() const noexcept { return sampler.getRegionStart(); }
    float getSampleEnd() const noexcept { return sampler.getRegionEnd(); }
    float getLoopStart() const noexcept { return sampler.getLoopStart(); }
    float getLoopEnd() const noexcept { return sampler.getLoopEnd(); }
    int getLoopMode() const noexcept { return loopMode.load(); }
    float getLoopFade() const noexcept { return loopFade.load(); }
    const ThoughtSampleData* getSampleForDisplay() const noexcept { return sampler.getSample(); }
    LocalAiWorker::Status getGenerationStatus() const noexcept { return worker.getStatus(); }
    juce::String getGenerationStatusText() const { return worker.getStatusText(); }
    juce::String getPrompt() const;
    float getAiDuration() const noexcept { return aiDuration.load(); } int getAiSteps() const noexcept { return aiSteps.load(); }
    float getAiCfg() const noexcept { return aiCfg.load(); } int getAiSeed() const noexcept { return aiSeed.load(); }
    float getAmpAttack() const noexcept { return ampAttack.load(); } float getAmpDecay() const noexcept { return ampDecay.load(); }
    float getAmpSustain() const noexcept { return ampSustain.load(); } float getAmpRelease() const noexcept { return ampRelease.load(); }
    float getDrive() const noexcept { return drive.load(); } float getOutputGain() const noexcept { return outputGain.load(); }
    int getFilterMode() const noexcept { return filterMode.load(); } float getFilterCutoff() const noexcept { return filterCutoff.load(); }
    float getFilterResonance() const noexcept { return filterResonance.load(); }
    float getFilterAttack() const noexcept { return filterAttack.load(); } float getFilterDecay() const noexcept { return filterDecay.load(); }
    float getFilterSustain() const noexcept { return filterSustain.load(); } float getFilterRelease() const noexcept { return filterRelease.load(); }
    float getFilterAmount() const noexcept { return filterAmount.load(); }

private:
    ThoughtSampler sampler;
    LocalAiWorker worker;
    mutable juce::CriticalSection stateLock;
    juce::String prompt { "Brass, D-minor chord, rising" };
    std::atomic<float> aiDuration { 3.0f }, aiCfg { 6.0f }, ampAttack { 0.005f }, ampDecay { 0.15f }, ampSustain { 0.85f }, ampRelease { 0.25f }, drive { 0.0f }, outputGain { 0.0f }, filterCutoff { 8000.0f }, filterResonance { 0.12f }, filterAttack { 0.005f }, filterDecay { 0.2f }, filterSustain { 0.0f }, filterRelease { 0.2f }, filterAmount { 0.0f };
    std::atomic<int> aiSteps { 14 }, aiSeed { -1 }, filterMode { 1 }, loopMode { 0 };
    std::atomic<float> loopStart { 0.0f }, loopEnd { 1.0f }, loopFade { 0.005f };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CircatThoughtProcessor)
};
