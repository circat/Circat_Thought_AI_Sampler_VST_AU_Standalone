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

    void request (juce::String prompt);
    void setReferenceAudio (juce::File file);
    void loadModel();
    void unloadModel();
    Status getStatus() const noexcept;
    juce::String getStatusText() const;

private:
    void startLocalStack();
    void refreshHealth();
    void postModelCommand (const juce::String& path);
    static void trimToEvent (juce::AudioBuffer<float>& audio, double sampleRate);
    void run() override;
    bool generate (const juce::String& prompt, const juce::String& referencePath, juce::String& error);

    ThoughtSampler& sampler;
    std::unique_ptr<juce::ChildProcess> backendStarter;
    juce::CriticalSection requestLock, statusLock;
    juce::String pendingPrompt, pendingReferencePath, referenceAudioPath, statusText { "Ready — enter a prompt and press Generate" };
    std::atomic<Status> status { Status::idle };
    std::atomic<int> modelCommand { 0 };
};
