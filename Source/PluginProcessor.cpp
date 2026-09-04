#include "PluginProcessor.h"
#include "PluginEditor.h"

#if CIRCAT_HAS_ABOUT
 #include <CircatLog.h>
#endif

namespace { const char* kPluginLogName = "Circat Thought"; const char* kPluginVersion = "v0.8.3 beta"; }

void circatLog (const juce::String& line)
{
#if CIRCAT_HAS_ABOUT
    circat::Log::write (kPluginLogName, line);
#else
    juce::ignoreUnused (line);
#endif
}

CircatThoughtProcessor::CircatThoughtProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)), sampler (16), worker (sampler)
{
}

void CircatThoughtProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampler.prepare (sampleRate, samplesPerBlock);
    preview.prepare (sampleRate, samplesPerBlock);
#if CIRCAT_HAS_ABOUT
    circat::Log::beginSession (kPluginLogName, kPluginVersion, *this);
#endif
}
void CircatThoughtProcessor::releaseResources() { sampler.reset(); preview.releaseResources(); }

bool CircatThoughtProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void CircatThoughtProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    sampler.processBlock (buffer, midi);
    preview.processBlock (buffer); // browser audition, mixed on top
}

juce::AudioProcessorEditor* CircatThoughtProcessor::createEditor() { return new CircatThoughtEditor (*this); }

void CircatThoughtProcessor::setSampleRegion (float start, float end) noexcept
{
    sampler.setRegion (start, end);
}

void CircatThoughtProcessor::applyPitch() noexcept
{
    const float semis = (float) pitchOctave.load() * 12.0f
                      + (float) pitchSemitone.load()
                      + pitchFineCents.load() * 0.01f;
    sampler.setPitchTuning (semis);
}

void CircatThoughtProcessor::setPitchOctave (int octaves) noexcept
{
    pitchOctave.store (juce::jlimit (-4, 4, octaves)); applyPitch();
}

void CircatThoughtProcessor::setPitchSemitone (int semitones) noexcept
{
    pitchSemitone.store (juce::jlimit (-12, 12, semitones)); applyPitch();
}

void CircatThoughtProcessor::setPitchFineCents (float cents) noexcept
{
    pitchFineCents.store (juce::jlimit (-100.0f, 100.0f, cents)); applyPitch();
}

void CircatThoughtProcessor::setLoop (int mode, float start, float end, float fadeSeconds) noexcept
{
    loopMode.store (mode); loopStart.store (start); loopEnd.store (end); loopFade.store (fadeSeconds);
    sampler.setLoop (mode, start, end, fadeSeconds);
}

void CircatThoughtProcessor::setAmpEnvelope (float attack, float decay, float sustain, float release) noexcept
{
    ampAttack.store (attack); ampDecay.store (decay); ampSustain.store (sustain); ampRelease.store (release);
    sampler.setAmpEnvelope (attack, decay, sustain, release);
}

void CircatThoughtProcessor::setInputDriveDb (float db) noexcept
{
    drive.store (db);
    sampler.setInputDriveDb (db);
}

void CircatThoughtProcessor::setOutputGainDb (float db) noexcept
{
    outputGain.store (db);
    sampler.setOutputGainDb (db);
}

void CircatThoughtProcessor::setFilter (int mode, float cutoffHz, float resonance) noexcept
{
    filterMode.store (mode); filterCutoff.store (cutoffHz); filterResonance.store (resonance);
    sampler.setFilter (mode, cutoffHz, resonance);
}

void CircatThoughtProcessor::setFilterEnvelope (float attack, float decay, float sustain, float release, float amount) noexcept
{
    filterAttack.store (attack); filterDecay.store (decay); filterSustain.store (sustain);
    filterRelease.store (release); filterAmount.store (amount);
    sampler.setFilterEnvelope (attack, decay, sustain, release, amount);
}

void CircatThoughtProcessor::generate (const juce::String& newPrompt, float duration, int steps, float cfg, int seed)
{
    {
        const juce::ScopedLock lock (stateLock);
        prompt = newPrompt.substring (0, 512);
    }
    worker.request (newPrompt, duration, steps, cfg, seed);
}

juce::String CircatThoughtProcessor::getPrompt() const
{
    const juce::ScopedLock lock (stateLock);
    return prompt;
}

void CircatThoughtProcessor::getStateInformation (juce::MemoryBlock& target)
{
    juce::ValueTree state ("CircatThought");
    state.setProperty ("prompt", getPrompt(), nullptr);
    state.setProperty ("sampleStart", sampler.getRegionStart(), nullptr);
    state.setProperty ("sampleEnd", sampler.getRegionEnd(), nullptr);
    state.setProperty ("aiDuration", aiDuration.load(), nullptr);
    state.setProperty ("aiSteps", aiSteps.load(), nullptr);
    state.setProperty ("aiCfg", aiCfg.load(), nullptr);
    state.setProperty ("aiSeed", aiSeed.load(), nullptr);
    state.setProperty ("loopMode", loopMode.load(), nullptr);
    state.setProperty ("loopStart", loopStart.load(), nullptr);
    state.setProperty ("loopEnd", loopEnd.load(), nullptr);
    state.setProperty ("loopFade", loopFade.load(), nullptr);
    state.setProperty ("ampAttack", ampAttack.load(), nullptr);
    state.setProperty ("ampDecay", ampDecay.load(), nullptr);
    state.setProperty ("ampSustain", ampSustain.load(), nullptr);
    state.setProperty ("ampRelease", ampRelease.load(), nullptr);
    state.setProperty ("drive", drive.load(), nullptr);
    state.setProperty ("outputGain", outputGain.load(), nullptr);
    state.setProperty ("filterMode", filterMode.load(), nullptr);
    state.setProperty ("filterCutoff", filterCutoff.load(), nullptr);
    state.setProperty ("filterResonance", filterResonance.load(), nullptr);
    state.setProperty ("filterAttack", filterAttack.load(), nullptr);
    state.setProperty ("filterDecay", filterDecay.load(), nullptr);
    state.setProperty ("filterSustain", filterSustain.load(), nullptr);
    state.setProperty ("filterRelease", filterRelease.load(), nullptr);
    state.setProperty ("filterAmount", filterAmount.load(), nullptr);
    state.setProperty ("pitchOctave", pitchOctave.load(), nullptr);
    state.setProperty ("pitchSemitone", pitchSemitone.load(), nullptr);
    state.setProperty ("pitchFineCents", pitchFineCents.load(), nullptr);
    state.setProperty ("qualityMode", qualityMode.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, target);
}

void CircatThoughtProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName ("CircatThought"))
        {
            {
                const juce::ScopedLock lock (stateLock);
                prompt = xml->getStringAttribute ("prompt", prompt).substring (0, 512);
            }
            const auto d = [&xml] (const char* n, double fallback) { return xml->getDoubleAttribute (n, fallback); };
            const auto i = [&xml] (const char* n, int fallback) { return xml->getIntAttribute (n, fallback); };
            setSampleRegion ((float) d ("sampleStart", 0.0), (float) d ("sampleEnd", 1.0));
            aiDuration.store ((float) d ("aiDuration", aiDuration.load()));
            aiSteps.store (i ("aiSteps", aiSteps.load()));
            aiCfg.store ((float) d ("aiCfg", aiCfg.load()));
            aiSeed.store (i ("aiSeed", aiSeed.load()));
            setLoop (i ("loopMode", 0), (float) d ("loopStart", 0.0), (float) d ("loopEnd", 1.0), (float) d ("loopFade", 0.005));
            setAmpEnvelope ((float) d ("ampAttack", 0.005), (float) d ("ampDecay", 0.15),
                            (float) d ("ampSustain", 0.85), (float) d ("ampRelease", 0.25));
            setInputDriveDb ((float) d ("drive", 0.0));
            setOutputGainDb ((float) d ("outputGain", 0.0));
            setFilter (i ("filterMode", 1), (float) d ("filterCutoff", 8000.0), (float) d ("filterResonance", 0.12));
            setFilterEnvelope ((float) d ("filterAttack", 0.005), (float) d ("filterDecay", 0.20),
                               (float) d ("filterSustain", 0.0), (float) d ("filterRelease", 0.20),
                               (float) d ("filterAmount", 0.0));
            pitchOctave.store (i ("pitchOctave", 0));
            pitchSemitone.store (i ("pitchSemitone", 0));
            pitchFineCents.store ((float) d ("pitchFineCents", 0.0));
            applyPitch();
            qualityMode.store (juce::jlimit (0, 2, i ("qualityMode", 0)));
            worker.setSamplerType (qualityMode.load() == 0 ? "pingpong" : "dpmpp-3m-sde");
        }
}

void CircatThoughtProcessor::autoSlice() noexcept
{
    const auto* sample = sampler.getSample();
    if (sample == nullptr || sample->audio.getNumSamples() < 2)
        return;

    const auto& audio = sample->audio;
    const int length = audio.getNumSamples();
    const int window = juce::jmax (64, (int) (sample->sampleRate * 0.008));
    int strongest = 0;
    float strongestEnergy = -1.0f;
    for (int start = 0; start < length; start += window)
    {
        const int end = juce::jmin (length, start + window);
        float energy = 0.0f;
        for (int n = start; n < end; ++n)
            for (int channel = 0; channel < audio.getNumChannels(); ++channel)
                energy += std::abs (audio.getSample (channel, n));
        if (energy > strongestEnergy) { strongestEnergy = energy; strongest = start; }
    }
    const int tail = juce::jmin (length - 1, strongest + (int) (sample->sampleRate * 2.5));
    sampler.setRegion ((float) strongest / (float) length, (float) tail / (float) length);
}

bool CircatThoughtProcessor::loadSampleFile (const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->numChannels < 1 || reader->lengthInSamples < 2 || reader->lengthInSamples > 10000000)
    { error = "Unsupported or oversized audio file"; return false; }
    juce::AudioBuffer<float> audio ((int) juce::jmin ((unsigned int) 2, reader->numChannels), (int) reader->lengthInSamples);
    if (! reader->read (&audio, 0, audio.getNumSamples(), 0, true, true))
    { error = "Could not decode audio file"; return false; }
    sampler.setSampleData (std::make_shared<ThoughtSampleData> (audio, reader->sampleRate, 60));
    sampler.setRegion (0.0f, 1.0f);
    return true;
}

bool CircatThoughtProcessor::savePreset (const juce::File& file, juce::String& error)
{
    juce::MemoryBlock state;
    getStateInformation (state);
    if (! file.replaceWithData (state.getData(), state.getSize()))
    { error = "Could not save preset"; return false; }
    return true;
}

bool CircatThoughtProcessor::loadPreset (const juce::File& file, juce::String& error)
{
    juce::MemoryBlock state;
    if (! file.loadFileAsData (state) || state.getSize() == 0)
    { error = "Could not read preset"; return false; }
    setStateInformation (state.getData(), (int) state.getSize());
    return true;
}

bool CircatThoughtProcessor::saveSampleWav (const juce::File& file, juce::String& error)
{
    const auto* sample = sampler.getSample();
    if (sample == nullptr || sample->audio.getNumSamples() < 2) { error = "No sample loaded"; return false; }
    auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream());
    if (stream == nullptr) { error = "Could not create WAV file"; return false; }
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.release(), sample->sampleRate,
        (unsigned int) sample->audio.getNumChannels(), 16, {}, 0));
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (sample->audio, 0, sample->audio.getNumSamples()))
    { error = "Could not write WAV file"; return false; }
    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CircatThoughtProcessor(); }
