#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>
#include <utility>
#include <vector>
#if CIRCAT_HAS_WULF_INPUT
#include "dsp/WulfAD.h"
#endif

/** Immutable audio held by ThoughtSampler. Build this off the audio thread. */
struct ThoughtSampleData
{
    ThoughtSampleData() = default;
    ThoughtSampleData (const juce::AudioBuffer<float>& source, double rate, int rootNote = 60);

    juce::AudioBuffer<float> audio;
    double sampleRate = 44100.0;
    int rootMidiNote = 60;
};

/** A small, allocation-free sampler core intended to be used by both app and plug-in. */
class ThoughtSampler final
{
public:
    explicit ThoughtSampler (int maximumVoices = 16);
    ~ThoughtSampler();

    ThoughtSampler (const ThoughtSampler&) = delete;
    ThoughtSampler& operator= (const ThoughtSampler&) = delete;

    void prepare (double outputSampleRate, int maximumBlockSize);
    void reset() noexcept;

    // Ownership is retained by the sampler, so the audio thread only reads an atomic raw pointer.
    // Construct/prepare the data off the audio thread.
    void setSample (std::shared_ptr<const ThoughtSampleData> sample);
    void setSampleData (std::shared_ptr<const ThoughtSampleData> sample) { setSample (std::move (sample)); }
    void clearSample();
    const ThoughtSampleData* getSample() const noexcept { return currentSample.load (std::memory_order_acquire); }

    void setPitchTuning (float semitones) noexcept { tuning.store (semitones, std::memory_order_relaxed); }
    float getPitchTuning() const noexcept { return tuning.load (std::memory_order_relaxed); }
    void setAmpEnvelope (float attack, float decay, float sustain, float release) noexcept;
    void setInputDriveDb (float db) noexcept { inputDriveDb.store (juce::jlimit (0.0f, 24.0f, db), std::memory_order_relaxed); }
    void setRegion (float start, float end) noexcept;
    float getRegionStart() const noexcept { return regionStart.load (std::memory_order_relaxed); }
    float getRegionEnd() const noexcept { return regionEnd.load (std::memory_order_relaxed); }

    // Renders into the supplied buffer. The buffer is cleared and MIDI is consumed in timestamp order.
    // No locks or allocations are taken by this method.
    void processBlock (juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) noexcept;

private:
    class Voice;
    std::vector<std::unique_ptr<Voice>> voices;
    double sampleRate = 44100.0;
    std::atomic<float> tuning { 0.0f };
    std::atomic<float> ampAttack { 0.005f }, ampDecay { 0.15f }, ampSustain { 0.85f }, ampRelease { 0.25f };
    std::atomic<float> inputDriveDb { 0.0f };
    std::atomic<float> regionStart { 0.0f }, regionEnd { 1.0f };
    std::atomic<const ThoughtSampleData*> currentSample { nullptr };

    // Old samples are intentionally retained until destruction. This makes a swap safe even when a
    // voice is rendering the old immutable buffer on another callback.
    std::vector<std::shared_ptr<const ThoughtSampleData>> sampleOwners;
    juce::CriticalSection ownerLock; // control-thread only; never touched by processBlock
#if CIRCAT_HAS_WULF_INPUT
    wulf::WulfAD inputStage[2];
#endif
};
