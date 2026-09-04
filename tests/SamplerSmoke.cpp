#include "ThoughtSampler.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

int main (int argc, char* argv[])
{
    if (argc != 2) return 2;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (argv[1])));
    if (reader == nullptr || reader->numChannels < 1 || reader->lengthInSamples < 2) return 3;

    juce::AudioBuffer<float> sample ((int) juce::jmin ((unsigned int) 2, reader->numChannels), (int) reader->lengthInSamples);
    if (! reader->read (&sample, 0, sample.getNumSamples(), 0, true, true)) return 4;

    ThoughtSampler sampler (4);
    sampler.prepare (48000.0, 512);
    sampler.setSampleData (std::make_shared<ThoughtSampleData> (sample, reader->sampleRate, 60));

    juce::AudioBuffer<float> output (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
    sampler.processBlock (output, midi);

    const auto magnitude = output.getMagnitude (0, output.getNumSamples());
    std::cout << "sampler_magnitude=" << magnitude << "\n";
    return magnitude > 0.00001f ? 0 : 5;
}
