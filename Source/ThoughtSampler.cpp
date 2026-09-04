#include "ThoughtSampler.h"

#include <algorithm>
#include <cmath>

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
            filterMode = newFilterMode;
            filterCutoff = newCutoff;
            filterResonance = newResonance;
            filterAttackStep = 1.0f / juce::jmax (1.0f, filterAttack * (float) outputRate);
            filterDecayStep = (1.0f - filterSustain) / juce::jmax (1.0f, filterDecay * (float) outputRate);
            filterReleaseStep = juce::jmax (filterSustain, 0.001f) / juce::jmax (1.0f, filterRelease * (float) outputRate);
            filterEnvelope = 0.0f; filterSustainLevel = filterSustain; filterStage = 0;
            filterAmount = filterEnvAmount; filterSampleRate = outputRate;
            filterLow[0] = filterLow[1] = filterBand[0] = filterBand[1] = 0.0f;
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
            stage = 3;
    }

    bool isActive() const noexcept { return active; }

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
            if (filterStage == 0) { filterEnvelope += filterAttackStep; if (filterEnvelope >= 1.0f) { filterEnvelope = 1.0f; filterStage = 1; } }
            else if (filterStage == 1) { filterEnvelope -= filterDecayStep; if (filterEnvelope <= filterSustainLevel) { filterEnvelope = filterSustainLevel; filterStage = 2; } }
            else if (stage == 3) { filterEnvelope -= filterReleaseStep; if (filterEnvelope < 0.0f) filterEnvelope = 0.0f; }
            const float cutoff = juce::jlimit (20.0f, 20000.0f, filterCutoff * std::pow (2.0f, filterEnvelope * filterAmount));
            const float f = juce::jlimit (0.001f, 0.99f, 2.0f * std::sin (juce::MathConstants<float>::pi * cutoff / (float) filterSampleRate));
            const float damping = juce::jlimit (0.08f, 1.95f, 1.8f - filterResonance * 1.65f);
            for (int channel = 0; channel < outputChannels; ++channel)
            {
                const int sourceChannel = std::min (channel, sourceChannels - 1);
                const auto* data = sample->audio.getReadPointer (sourceChannel);
                float value = data[index] + fraction * (data[index + 1] - data[index]);
                if (loopMode == 1 && loopFadeSamples > 1 && position >= loopEndPosition - loopFadeSamples)
                {
                    const double loopPosition = loopStartPosition + (position - (loopEndPosition - loopFadeSamples));
                    const int loopIndex = juce::jlimit (0, length - 2, (int) loopPosition);
                    const float loopFraction = (float) (loopPosition - loopIndex);
                    const float loopValue = data[loopIndex] + loopFraction * (data[loopIndex + 1] - data[loopIndex]);
                    const float blend = (float) ((position - (loopEndPosition - loopFadeSamples)) / loopFadeSamples);
                    value = value + (loopValue - value) * juce::jlimit (0.0f, 1.0f, blend);
                }
                if (filterMode != 0)
                {
                    const int stateChannel = juce::jmin (channel, 1);
                    filterLow[stateChannel] += f * filterBand[stateChannel];
                    const float high = value - filterLow[stateChannel] - damping * filterBand[stateChannel];
                    filterBand[stateChannel] += f * high;
                    value = filterMode == 1 ? filterLow[stateChannel] : (filterMode == 2 ? high : filterBand[stateChannel]);
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
    float filterLow[2] {}, filterBand[2] {};
    float filterCutoff = 8000.0f, filterResonance = 0.12f, filterAmount = 0.0f;
    float filterEnvelope = 0.0f, filterAttackStep = 1.0f, filterDecayStep = 0.0f, filterReleaseStep = 1.0f, filterSustainLevel = 0.0f;
    double filterSampleRate = 44100.0;
    int note = -1;
    int stage = 0, filterStage = 0, filterMode = 0, loopMode = 0;
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
            Voice* selected = nullptr;
            for (auto& voice : voices)
                if (! voice->isActive()) { selected = voice.get(); break; }
            if (selected == nullptr)
                selected = voices.front().get();
            selected->noteOn (message.getNoteNumber(), message.getFloatVelocity(), getSample(), sampleRate, getPitchTuning(),
                              getRegionStart(), getRegionEnd(), ampAttack.load(), ampDecay.load(), ampSustain.load(), ampRelease.load(),
                              filterMode.load(), filterCutoff.load(), filterResonance.load(), filterAttack.load(), filterDecay.load(),
                              filterSustain.load(), filterRelease.load(), filterAmount.load(), loopMode.load(), loopStart.load(),
                              loopEnd.load(), loopFadeSeconds.load());
        }
        else if (message.isNoteOff())
            for (auto& voice : voices)
                voice->noteOff (message.getNoteNumber());
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
