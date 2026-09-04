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

void CircatThoughtProcessor::generate (const juce::String& newPrompt)
{
    {
        const juce::ScopedLock lock (stateLock);
        prompt = newPrompt.substring (0, 512);
    }
    worker.request (newPrompt);
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

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CircatThoughtProcessor(); }
