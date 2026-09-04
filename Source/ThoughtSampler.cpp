#include "ThoughtSampler.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    /** Find the nearest rising zero-crossing (sample[i] <= 0 && sample[i+1] > 0) within searchWindow samples.
        Returns the index of the zero-crossing, or targetSample if none found nearby. */
    static inline int findNearestZeroCrossing (const juce::AudioBuffer<float>& buffer, int targetSample, int searchWindow = 512)
    {
        const int numSamples = buffer.getNumSamples();
        if (numSamples < 2) return juce::jlimit (0, juce::jmax (0, numSamples - 1), targetSample);

        const float* channelData = buffer.getReadPointer (0);
        int start = juce::jlimit (0, numSamples - 2, targetSample - searchWindow);
        int end   = juce::jlimit (0, numSamples - 2, targetSample + searchWindow);

        int bestSample = targetSample;
        float minDistance = std::numeric_limits<float>::max();

        for (int i = start; i <= end; ++i)
        {
            if (channelData[i] <= 0.0f && channelData[i + 1] > 0.0f)
            {
                const float dist = (float) std::abs (i - targetSample);
                if (dist < minDistance) { minDistance = dist; bestSample = i; }
            }
        }
        return bestSample;
    }
}

ThoughtSampleData::ThoughtSampleData (const juce::AudioBuffer<float>& source, double rate, int rootNote)
    : audio (source), sampleRate (rate > 0.0 ? rate : 44100.0), rootMidiNote (juce::jlimit (0, 127, rootNote))
{
}

class ThoughtSampler::Voice
{
public:
    void noteOn (int midiNote, float velocity, const ThoughtSampleData* newSample,
                 double outputRate, float semitoneTuning, float regionStart, float regionEnd,
                 float attack, float decay, float sustain, float release,
                 int newFilterMode, float newCutoff, float newResonance,
                 float filterAttack, float filterDecay, float filterSustain, float filterRelease, float filterEnvAmount,
                 int newLoopMode, float newLoopStart, float newLoopEnd, float newLoopFadeSeconds) noexcept
    {
        sample = newSample;
        note = midiNote;
        gain = velocity;
        active = sample != nullptr && sample->audio.getNumSamples() > 1;

        if (active)
        {
            const auto lastIndex = static_cast<double> (sample->audio.getNumSamples() - 1);
            position = lastIndex * juce::jlimit (0.0f, 0.99f, regionStart);
            endPosition = lastIndex * juce::jlimit (0.01f, 1.0f, juce::jmax (regionEnd, regionStart + 0.01f));
            const auto ratio = std::pow (2.0, (static_cast<double> (midiNote - sample->rootMidiNote)
                                               + static_cast<double> (semitoneTuning)) / 12.0);
            increment = (sample->sampleRate / std::max (1.0, outputRate)) * ratio;
            attackStep = 1.0f / juce::jmax (1.0f, attack * (float) outputRate);
            decayStep = (1.0f - sustain) / juce::jmax (1.0f, decay * (float) outputRate);
            releaseStep = juce::jmax (sustain, 0.001f) / juce::jmax (1.0f, release * (float) outputRate);
            envelope = 0.0f; sustainLevel = sustain; stage = 0;
            loopMode = newLoopMode;
            loopStartPosition = lastIndex * juce::jlimit (regionStart, regionEnd - 0.001f, newLoopStart);
            const float minimumLoopEnd = (float) (loopStartPosition / lastIndex) + 0.001f;
            loopEndPosition = lastIndex * juce::jlimit (minimumLoopEnd, regionEnd, newLoopEnd);
            loopFadeSamples = juce::jmax (0, (int) std::round (newLoopFadeSeconds * outputRate));
            loopFadeSamples = juce::jmin (loopFadeSamples, (int) ((loopEndPosition - loopStartPosition) * 0.45));
            direction = 1.0;
        }
    }

    void noteOff (int midiNote) noexcept
    {
        if (active && (midiNote < 0 || note == midiNote))
        {
            stage = 3;
        }
    }

    bool isActive() const noexcept { return active; }

    // Lower = better candidate to steal. Releasing voices and quiet voices go first.
    float stealPriority() const noexcept
    {
        if (! active) return -1.0f;
        return stage == 3 ? envelope * 0.25f : envelope;
    }

    void render (juce::AudioBuffer<float>& output, int start, int count) noexcept
    {
        if (! active || sample == nullptr || count <= 0)
            return;

        const int length = sample->audio.getNumSamples();
        const int sourceChannels = sample->audio.getNumChannels();
        const int outputChannels = output.getNumChannels();
        if (length < 1 || sourceChannels < 1 || outputChannels < 1)
        {
            active = false;
            return;
        }

        for (int i = 0; i < count && active; ++i)
        {
            if (loopMode == 1 && position >= loopEndPosition)
                position = loopStartPosition + (position - loopEndPosition);
            else if (loopMode == 2 && position >= loopEndPosition)
            {
                direction = -1.0;
                position = loopEndPosition - (position - loopEndPosition);
            }
            else if (loopMode == 2 && position <= loopStartPosition)
            {
                direction = 1.0;
                position = loopStartPosition + (loopStartPosition - position);
            }

            const auto index = static_cast<int> (position);
            if (index >= length - 1 || index < 0 || (loopMode == 0 && position >= endPosition))
            {
                active = false;
                break;
            }

            const float fraction = static_cast<float> (position - static_cast<double> (index));
            if (stage == 0) { envelope += attackStep; if (envelope >= 1.0f) { envelope = 1.0f; stage = 1; } }
            else if (stage == 1) { envelope -= decayStep; if (envelope <= sustainLevel) { envelope = sustainLevel; stage = 2; } }
            else if (stage == 3) { envelope -= releaseStep; if (envelope <= 0.0f) { active = false; break; } }
            for (int channel = 0; channel < outputChannels; ++channel)
            {
                const int sourceChannel = std::min (channel, sourceChannels - 1);
                const auto* data = sample->audio.getReadPointer (sourceChannel);
                // 4-point Catmull-Rom / Hermite interpolation. Much less aliasing
                // than linear when a note is transposed up.
                const float xm1 = data[index > 0 ? index - 1 : 0];
                const float x0  = data[index];
                const float x1  = data[index + 1];
                const float x2  = data[index + 2 < length ? index + 2 : length - 1];
                const float c1 = 0.5f * (x1 - xm1);
                const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
                const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                float value = ((c3 * fraction + c2) * fraction + c1) * fraction + x0;

                // Forward loop (mode 1) with equal-power crossfade.
                if (loopMode == 1 && loopFadeSamples > 1 && position >= loopEndPosition - loopFadeSamples)
                {
                    const double loopPosition = loopStartPosition + (position - (loopEndPosition - loopFadeSamples));
                    const int loopIndex = juce::jlimit (0, length - 2, (int) loopPosition);
                    const float loopFraction = (float) (loopPosition - loopIndex);
                    const float loopValue = data[loopIndex] + loopFraction * (data[loopIndex + 1] - data[loopIndex]);
                    const float t = juce::jlimit (0.0f, 1.0f, (float) ((position - (loopEndPosition - loopFadeSamples)) / loopFadeSamples));
                    const float gainOut = std::cos (t * juce::MathConstants<float>::halfPi);
                    const float gainIn  = std::sin (t * juce::MathConstants<float>::halfPi);
                    value = value * gainOut + loopValue * gainIn;
                }
                // Ping-pong loop (mode 2) with equal-power crossfade at reversals.
                else if (loopMode == 2 && loopFadeSamples > 1)
                {
                    // Crossfade when approaching loop end (going forward).
                    if (direction > 0.0 && position >= loopEndPosition - loopFadeSamples)
                    {
                        const double mirroredPos = loopEndPosition - (position - (loopEndPosition - loopFadeSamples));
                        const int mirrorIndex = juce::jlimit (0, length - 2, (int) mirroredPos);
                        const float mirrorFraction = (float) (mirroredPos - mirrorIndex);
                        const float mirrorValue = data[mirrorIndex] + mirrorFraction * (data[mirrorIndex + 1] - data[mirrorIndex]);
                        const float t = juce::jlimit (0.0f, 1.0f, (float) ((position - (loopEndPosition - loopFadeSamples)) / loopFadeSamples));
                        const float gainOut = std::cos (t * juce::MathConstants<float>::halfPi);
                        const float gainIn  = std::sin (t * juce::MathConstants<float>::halfPi);
                        value = value * gainOut + mirrorValue * gainIn;
                    }
                    // Crossfade when approaching loop start (going backward).
                    else if (direction < 0.0 && position <= loopStartPosition + loopFadeSamples)
                    {
                        const double mirroredPos = loopStartPosition + (loopStartPosition + loopFadeSamples - position);
                        const int mirrorIndex = juce::jlimit (0, length - 2, (int) mirroredPos);
                        const float mirrorFraction = (float) (mirroredPos - mirrorIndex);
                        const float mirrorValue = data[mirrorIndex] + mirrorFraction * (data[mirrorIndex + 1] - data[mirrorIndex]);
                        const float t = juce::jlimit (0.0f, 1.0f, (float) ((loopStartPosition + loopFadeSamples - position) / loopFadeSamples));
                        const float gainOut = std::cos (t * juce::MathConstants<float>::halfPi);
                        const float gainIn  = std::sin (t * juce::MathConstants<float>::halfPi);
                        value = value * gainOut + mirrorValue * gainIn;
                    }
                }

                output.addSample (channel, start + i, value * gain * envelope);
            }
            position += increment * direction;
        }
    }

private:
    const ThoughtSampleData* sample = nullptr;
    double position = 0.0, increment = 1.0, endPosition = 0.0;
    double loopStartPosition = 0.0, loopEndPosition = 0.0, direction = 1.0;
    int loopFadeSamples = 0;
    float gain = 0.0f;
    float envelope = 0.0f, attackStep = 1.0f, decayStep = 0.0f, releaseStep = 1.0f, sustainLevel = 1.0f;
    int note = -1;
    int stage = 0, loopMode = 0;
    bool active = false;
};

ThoughtSampler::ThoughtSampler (int maximumVoices)
{
    const int count = juce::jmax (1, maximumVoices);
    voices.reserve (static_cast<size_t> (count));
    for (int i = 0; i < count; ++i)
        voices.emplace_back (std::make_unique<Voice>());
}

ThoughtSampler::~ThoughtSampler() = default;

void ThoughtSampler::prepare (double outputSampleRate, int) 
{
    sampleRate = outputSampleRate > 0.0 ? outputSampleRate : 44100.0;
    reset();
#if CIRCAT_HAS_WULF_INPUT
    for (auto& stage : inputStage) stage.prepare (sampleRate);
#endif
}

void ThoughtSampler::setAmpEnvelope (float attack, float decay, float sustain, float release) noexcept
{
    ampAttack.store (juce::jlimit (0.001f, 10.0f, attack), std::memory_order_relaxed);
    ampDecay.store (juce::jlimit (0.001f, 10.0f, decay), std::memory_order_relaxed);
    ampSustain.store (juce::jlimit (0.0f, 1.0f, sustain), std::memory_order_relaxed);
    ampRelease.store (juce::jlimit (0.001f, 20.0f, release), std::memory_order_relaxed);
}

void ThoughtSampler::setFilter (int mode, float cutoffHz, float resonance) noexcept
{
    filterMode.store (juce::jlimit (0, 3, mode), std::memory_order_relaxed);
    filterCutoff.store (juce::jlimit (20.0f, 20000.0f, cutoffHz), std::memory_order_relaxed);
    filterResonance.store (juce::jlimit (0.0f, 1.0f, resonance), std::memory_order_relaxed);
}

void ThoughtSampler::setFilterEnvelope (float attack, float decay, float sustain, float release, float amountOctaves) noexcept
{
    filterAttack.store (juce::jlimit (0.001f, 10.0f, attack), std::memory_order_relaxed);
    filterDecay.store (juce::jlimit (0.001f, 10.0f, decay), std::memory_order_relaxed);
    filterSustain.store (juce::jlimit (0.0f, 1.0f, sustain), std::memory_order_relaxed);
    filterRelease.store (juce::jlimit (0.001f, 20.0f, release), std::memory_order_relaxed);
    filterAmount.store (juce::jlimit (-6.0f, 6.0f, amountOctaves), std::memory_order_relaxed);
}

void ThoughtSampler::reset() noexcept
{
    for (auto& voice : voices)
        voice->noteOff (-1);
}

void ThoughtSampler::setSample (std::shared_ptr<const ThoughtSampleData> sample)
{
    const auto raw = sample.get();
    const juce::ScopedLock lock (ownerLock);
    if (sample != nullptr)
        sampleOwners.push_back (std::move (sample));
    currentSample.store (raw, std::memory_order_release);
}

void ThoughtSampler::clearSample()
{
    currentSample.store (nullptr, std::memory_order_release);
}

void ThoughtSampler::setRegion (float start, float end) noexcept
{
    start = juce::jlimit (0.0f, 0.99f, start);
    end = juce::jlimit (start + 0.01f, 1.0f, end);
    regionStart.store (start, std::memory_order_relaxed);
    regionEnd.store (end, std::memory_order_relaxed);
}

void ThoughtSampler::setLoop (int mode, float start, float end, float fadeSeconds) noexcept
{
    start = juce::jlimit (0.0f, 0.99f, start);
    end = juce::jlimit (start + 0.001f, 1.0f, end);

    // Snap loop points to nearest zero-crossing if a sample is loaded.
    const ThoughtSampleData* sample = currentSample.load (std::memory_order_acquire);
    if (sample != nullptr && sample->audio.getNumSamples() > 1)
    {
        const int numSamples = sample->audio.getNumSamples();
        const int targetStartSample = (int) (start * (float) (numSamples - 1));
        const int targetEndSample   = (int) (end * (float) (numSamples - 1));

        const int snappedStartSample = findNearestZeroCrossing (sample->audio, targetStartSample, 512);
        const int snappedEndSample   = findNearestZeroCrossing (sample->audio, targetEndSample, 512);

        // Clamp snapped values to valid range and convert back to normalized.
        const int clampedStart = juce::jlimit (0, numSamples - 2, snappedStartSample);
        const int clampedEnd   = juce::jlimit (clampedStart + 1, numSamples - 1, snappedEndSample);

        start = (float) clampedStart / (float) (numSamples - 1);
        end   = (float) clampedEnd / (float) (numSamples - 1);
    }

    loopMode.store (juce::jlimit (0, 2, mode), std::memory_order_relaxed);
    loopStart.store (start, std::memory_order_relaxed);
    loopEnd.store (end, std::memory_order_relaxed);
    loopFadeSeconds.store (juce::jlimit (0.0f, 0.5f, fadeSeconds), std::memory_order_relaxed);
}

void ThoughtSampler::processBlock (juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) noexcept
{
    output.clear();
    int cursor = 0;
    for (const auto metadata : midi)
    {
        const int eventSample = juce::jlimit (0, output.getNumSamples(), metadata.samplePosition);
        if (eventSample > cursor)
            for (auto& voice : voices)
                voice->render (output, cursor, eventSample - cursor);

        const auto message = metadata.getMessage();
        if (message.isNoteOn())
        {
            // Retrigger master filter envelope on note-on.
            m_fStage = 0;
            m_fEnv = 0.0f;

            Voice* selected = nullptr;
            for (auto& voice : voices)
                if (! voice->isActive()) { selected = voice.get(); break; }
            if (selected == nullptr)
            {
                float lowest = std::numeric_limits<float>::max();
                for (auto& voice : voices)
                {
                    const float priority = voice->stealPriority();
                    if (priority < lowest) { lowest = priority; selected = voice.get(); }
                }
            }
            selected->noteOn (message.getNoteNumber(), message.getFloatVelocity(), getSample(), sampleRate, getPitchTuning(),
                              getRegionStart(), getRegionEnd(), ampAttack.load(), ampDecay.load(), ampSustain.load(), ampRelease.load(),
                              0, 8000.0f, 0.12f, 0.005f, 0.20f, 0.0f, 0.20f, 0.0f, loopMode.load(), loopStart.load(),
                              loopEnd.load(), loopFadeSeconds.load());
        }
        else if (message.isNoteOff())
        {
            // Set master filter to release stage on any note-off.
            m_fStage = 3;
            for (auto& voice : voices)
                voice->noteOff (message.getNoteNumber());
        }
        cursor = eventSample;
    }
    if (cursor < output.getNumSamples())
        for (auto& voice : voices)
            voice->render (output, cursor, output.getNumSamples() - cursor);
#if CIRCAT_HAS_WULF_INPUT
    const float drive = std::pow (10.0f, inputDriveDb.load (std::memory_order_relaxed) / 20.0f);
    if (drive > 1.0001f)
        for (int channel = 0; channel < output.getNumChannels(); ++channel)
        {
            inputStage[juce::jmin (channel, 1)].setGain (drive);
            for (int sample = 0; sample < output.getNumSamples(); ++sample)
                output.setSample (channel, sample, inputStage[juce::jmin (channel, 1)].processSample (output.getSample (channel, sample)));
        }
#endif
    // Master bus filter (TPT SVF). Apply after voice render + WULF drive, before output gain.
    const int masterFilterMode = filterMode.load (std::memory_order_relaxed);
    if (masterFilterMode != 0)
    {
        const float masterFilterCutoff = filterCutoff.load (std::memory_order_relaxed);
        const float masterFilterResonance = filterResonance.load (std::memory_order_relaxed);
        const float masterFilterAttack = filterAttack.load (std::memory_order_relaxed);
        const float masterFilterDecay = filterDecay.load (std::memory_order_relaxed);
        const float masterFilterSustain = filterSustain.load (std::memory_order_relaxed);
        const float masterFilterRelease = filterRelease.load (std::memory_order_relaxed);
        const float masterFilterAmount = filterAmount.load (std::memory_order_relaxed);

        // Advance filter envelope per sample.
        const float fAttackStep = 1.0f / juce::jmax (1.0f, masterFilterAttack * (float) sampleRate);
        const float fDecayStep = (1.0f - masterFilterSustain) / juce::jmax (1.0f, masterFilterDecay * (float) sampleRate);
        const float fReleaseStep = juce::jmax (masterFilterSustain, 0.001f) / juce::jmax (1.0f, masterFilterRelease * (float) sampleRate);

        for (int sample = 0; sample < output.getNumSamples(); ++sample)
        {
            // Advance filter envelope.
            if (m_fStage == 0) { m_fEnv += fAttackStep; if (m_fEnv >= 1.0f) { m_fEnv = 1.0f; m_fStage = 1; } }
            else if (m_fStage == 1) { m_fEnv -= fDecayStep; if (m_fEnv <= masterFilterSustain) { m_fEnv = masterFilterSustain; m_fStage = 2; } }
            else if (m_fStage == 3) { m_fEnv -= fReleaseStep; if (m_fEnv <= 0.0f) m_fEnv = 0.0f; }

            // Compute cutoff from envelope.
            const float cutoffHz = juce::jlimit (20.0f, (float) sampleRate * 0.45f,
                                                 masterFilterCutoff * std::pow (2.0f, m_fEnv * masterFilterAmount));

            // TPT SVF coefficients.
            const float g = std::tan (juce::MathConstants<float>::pi * cutoffHz / (float) sampleRate);
            const float k = 2.0f - 1.9f * juce::jlimit (0.0f, 1.0f, masterFilterResonance);
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1;
            const float a3 = g * a2;

            // Apply filter to each channel.
            for (int channel = 0; channel < output.getNumChannels(); ++channel)
            {
                const int sc = juce::jmin (channel, 1);
                const float value = output.getSample (channel, sample);
                const float v3 = value - m_svfIc2[sc];
                const float v1 = a1 * m_svfIc1[sc] + a2 * v3;
                const float v2 = m_svfIc2[sc] + a2 * m_svfIc1[sc] + a3 * v3;
                m_svfIc1[sc] = 2.0f * v1 - m_svfIc1[sc];
                m_svfIc2[sc] = 2.0f * v2 - m_svfIc2[sc];

                const float low = v2;
                const float band = v1;
                const float high = value - k * v1 - v2;
                float filtered = masterFilterMode == 1 ? low : (masterFilterMode == 2 ? high : band);

                // Non-finite guard.
                if (! std::isfinite (filtered)) { m_svfIc1[sc] = m_svfIc2[sc] = 0.0f; filtered = 0.0f; }
                filtered = juce::jlimit (-4.0f, 4.0f, filtered);

                output.setSample (channel, sample, filtered);
            }
        }
    }
    const float outputGain = std::pow (10.0f, outputGainDb.load (std::memory_order_relaxed) / 20.0f);
    float peak = 0.0f;
    for (int channel = 0; channel < output.getNumChannels(); ++channel)
        for (int sample = 0; sample < output.getNumSamples(); ++sample)
        {
            const float value = output.getSample (channel, sample) * outputGain;
            output.setSample (channel, sample, value);
            peak = juce::jmax (peak, std::abs (value));
        }
    float previous = outputPeak.load (std::memory_order_relaxed);
    while (previous < peak && ! outputPeak.compare_exchange_weak (previous, peak, std::memory_order_relaxed)) {}
}
