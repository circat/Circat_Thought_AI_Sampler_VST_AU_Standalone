#include "SamplePreviewPlayer.h"

SamplePreviewPlayer::SamplePreviewPlayer()
{
    m_formatManager.registerBasicFormats();
}

SamplePreviewPlayer::~SamplePreviewPlayer() = default;

void SamplePreviewPlayer::prepare (double sampleRate, int blockSize)
{
    m_hostSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    m_blockSize = juce::jmax (1, blockSize);
    m_scratch.setSize (2, m_blockSize, false, false, true);

    juce::SpinLock::ScopedLockType sl (m_handoffLock);
    if (m_active && m_active->resampler)
        m_active->resampler->prepareToPlay (m_blockSize, m_hostSampleRate);
}

void SamplePreviewPlayer::releaseResources()
{
    stop();
    clearRetired();
    juce::SpinLock::ScopedLockType sl (m_handoffLock);
    m_active.reset();
    m_pending.reset();
    m_scratch.setSize (2, 0);
}

void SamplePreviewPlayer::clearRetired()
{
    std::unique_ptr<Source> dead;
    {
        juce::SpinLock::ScopedLockType sl (m_handoffLock);
        dead = std::move (m_retired);
    }
    dead.reset(); // freed here, on the message thread
}

void SamplePreviewPlayer::play (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (m_formatManager.createReaderFor (file));
    if (reader == nullptr)
        return;

    auto src = std::make_unique<Source>();
    src->fileSampleRate = reader->sampleRate;
    src->lengthInSamples = reader->lengthInSamples;

    src->readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
    src->readerSource->setNextReadPosition (0);

    const double ratio = src->fileSampleRate > 0.0 ? src->fileSampleRate / m_hostSampleRate : 1.0;
    src->resampler = std::make_unique<juce::ResamplingAudioSource> (src->readerSource.get(), false, 2);
    src->resampler->setResamplingRatio (ratio);
    src->resampler->prepareToPlay (m_blockSize, m_hostSampleRate);

    m_lengthSeconds.store (src->fileSampleRate > 0.0
                               ? (double) src->lengthInSamples / src->fileSampleRate : 0.0);
    m_positionSeconds.store (0.0);
    m_stopRequested.store (false);

    std::unique_ptr<Source> displaced;
    {
        juce::SpinLock::ScopedLockType sl (m_handoffLock);
        if (m_pending)
            displaced = std::move (m_pending);
        m_pending = std::move (src);
    }
    displaced.reset();
    clearRetired();

    m_isPlaying.store (true);
}

void SamplePreviewPlayer::stop()
{
    m_stopRequested.store (true);
    m_isPlaying.store (false);
    m_positionSeconds.store (0.0);
    clearRetired();
}

void SamplePreviewPlayer::processBlock (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numOut = buffer.getNumChannels();
    if (numSamples <= 0 || numOut <= 0)
        return;

    if (m_handoffLock.tryEnter())
    {
        if (m_pending)
        {
            if (m_active)
                m_retired = std::move (m_active);
            m_active = std::move (m_pending);
        }
        if (m_stopRequested.exchange (false))
        {
            if (m_active)
                m_retired = std::move (m_active);
        }
        m_handoffLock.exit();
    }

    if (m_active == nullptr || m_active->resampler == nullptr || ! m_isPlaying.load())
        return;

    const int n = juce::jmin (numSamples, m_scratch.getNumSamples());
    if (n <= 0)
        return;

    m_scratch.clear();
    juce::AudioSourceChannelInfo info (&m_scratch, 0, n);
    m_active->resampler->getNextAudioBlock (info);

    const float gain = juce::Decibels::decibelsToGain (m_gainDb.load());
    for (int ch = 0; ch < numOut; ++ch)
    {
        const int srcCh = juce::jmin (ch, m_scratch.getNumChannels() - 1);
        buffer.addFrom (ch, 0, m_scratch, srcCh, 0, n, gain);
    }

    if (m_active->readerSource)
    {
        const juce::int64 pos = m_active->readerSource->getNextReadPosition();
        m_positionSeconds.store (m_active->fileSampleRate > 0.0
                                     ? (double) pos / m_active->fileSampleRate : 0.0);
        if (! m_active->readerSource->isLooping() && pos >= m_active->lengthInSamples)
        {
            m_isPlaying.store (false);
            m_stopRequested.store (true);
        }
    }
}
