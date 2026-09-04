#include "ThoughtSampler.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

int main (int argc, char* argv[])
{
    juce::AudioBuffer<float> sample;
    double sourceRate = 48000.0;
    if (argc == 2)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (argv[1])));
        if (reader == nullptr || reader->numChannels < 1 || reader->lengthInSamples < 2) return 3;
        sample.setSize ((int) juce::jmin ((unsigned int) 2, reader->numChannels), (int) reader->lengthInSamples);
        if (! reader->read (&sample, 0, sample.getNumSamples(), 0, true, true)) return 4;
        sourceRate = reader->sampleRate;
    }
    else
    {
        sample.setSize (2, 48000);
        for (int n = 0; n < sample.getNumSamples(); ++n)
            sample.setSample (0, n, std::sin (juce::MathConstants<float>::twoPi * 440.0f * (float) n / 48000.0f));
        sample.copyFrom (1, 0, sample, 0, 0, sample.getNumSamples());
    }

    ThoughtSampler sampler (4);
    sampler.prepare (48000.0, 512);
    sampler.setSampleData (std::make_shared<ThoughtSampleData> (sample, sourceRate, 60));
    sampler.setFilter (1, 1200.0f, 0.3f);
    sampler.setFilterEnvelope (0.001f, 0.1f, 0.0f, 0.1f, 1.0f);

    juce::AudioBuffer<float> output (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
    sampler.processBlock (output, midi);

    const auto magnitude = output.getMagnitude (0, output.getNumSamples());
    std::cout << "sampler_magnitude=" << magnitude << "\n";
    return magnitude > 0.00001f ? 0 : 5;
}
