#pragma once

#include "ThoughtSampler.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <atomic>

class LocalAiWorker final : private juce::Thread
{
public:
    enum class Status { idle, generating, ready, error };

    explicit LocalAiWorker (ThoughtSampler& sampler);
    ~LocalAiWorker() override;

    void request (juce::String prompt, float duration, int steps, float cfg, int seed);
    void setReferenceAudio (juce::File file);
    void loadModel();
    void unloadModel();
    Status getStatus() const noexcept;
    juce::String getStatusText() const;

    // Every generated one-shot is written here until the user exports it.
    static juce::File generatedDirectory();
    juce::File getLastGeneratedFile() const;

private:
    void startLocalStack();
    void refreshHealth();
    bool ensureModelReady (juce::String& error);
    void pruneGenerated();
    void postModelCommand (const juce::String& path);
    static void trimToEvent (juce::AudioBuffer<float>& audio, double sampleRate);
    void run() override;
    bool generate (const juce::String& prompt, const juce::String& referencePath, float duration, int steps, float cfg, int seed, juce::String& error);

    ThoughtSampler& sampler;
    std::unique_ptr<juce::ChildProcess> backendStarter;
    juce::CriticalSection requestLock, statusLock;
    juce::String pendingPrompt, pendingReferencePath, referenceAudioPath, statusText { "Ready — enter a prompt and press Generate" };
    juce::File lastGenerated;
    float pendingDuration = 3.0f, pendingCfg = 6.0f;
    int pendingSteps = 100, pendingSeed = -1;
    std::atomic<Status> status { Status::idle };
    std::atomic<int> modelCommand { 0 };
};
