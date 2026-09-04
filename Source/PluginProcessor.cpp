#include "PluginProcessor.h"
#include "PluginEditor.h"

CircatThoughtProcessor::CircatThoughtProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)), sampler (16), worker (sampler)
{
}

void CircatThoughtProcessor::prepareToPlay (double sampleRate, int samplesPerBlock) { sampler.prepare (sampleRate, samplesPerBlock); }
void CircatThoughtProcessor::releaseResources() { sampler.reset(); }

bool CircatThoughtProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void CircatThoughtProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    sampler.processBlock (buffer, midi);
}

juce::AudioProcessorEditor* CircatThoughtProcessor::createEditor() { return new CircatThoughtEditor (*this); }

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
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, target);
}

void CircatThoughtProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName ("CircatThought"))
        {
            const juce::ScopedLock lock (stateLock);
            prompt = xml->getStringAttribute ("prompt", prompt).substring (0, 512);
            sampler.setRegion ((float) xml->getDoubleAttribute ("sampleStart", 0.0),
                               (float) xml->getDoubleAttribute ("sampleEnd", 1.0));
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
