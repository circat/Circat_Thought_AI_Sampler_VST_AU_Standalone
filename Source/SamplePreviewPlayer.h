#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <memory>

// In-processor preview/audition player. Message-thread controls, lock-free
// hand-off to the audio callback. Inert until play() is called: processBlock()
// adds nothing to the buffer when no preview is active. Adapted from the S612
// sampler.
class SamplePreviewPlayer
{
public:
    SamplePreviewPlayer();
    ~SamplePreviewPlayer();

    void prepare (double sampleRate, int blockSize);
    void releaseResources();

    // Message-thread only.
    void play (const juce::File& file);
    void stop();

    bool isPlaying() const noexcept { return m_isPlaying.load(); }
    double getPositionSeconds() const noexcept { return m_positionSeconds.load(); }
    double getLengthSeconds() const noexcept { return m_lengthSeconds.load(); }
    float getGainDb() const noexcept { return m_gainDb.load(); }
    void setGainDb (float db) noexcept { m_gainDb.store (db); }

    // Audio thread. ADDs the preview (resampled to host rate) to every channel.
    void processBlock (juce::AudioBuffer<float>& buffer) noexcept;

private:
    struct Source
    {
        std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
        std::unique_ptr<juce::ResamplingAudioSource> resampler;
        double fileSampleRate = 0.0;
        juce::int64 lengthInSamples = 0;
    };

    void clearRetired();

    juce::AudioFormatManager m_formatManager;

    double m_hostSampleRate = 44100.0;
    int m_blockSize = 512;
    juce::AudioBuffer<float> m_scratch;

    juce::SpinLock m_handoffLock;
    std::unique_ptr<Source> m_pending;  // guarded by m_handoffLock
    std::unique_ptr<Source> m_retired;  // guarded by m_handoffLock
    std::unique_ptr<Source> m_active;   // audio thread only

    std::atomic<bool> m_isPlaying { false };
    std::atomic<bool> m_stopRequested { false };
    std::atomic<double> m_positionSeconds { 0.0 };
    std::atomic<double> m_lengthSeconds { 0.0 };
    std::atomic<float> m_gainDb { -6.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplePreviewPlayer)
};
