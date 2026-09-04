#include "LocalAiWorker.h"

#include <cmath>

LocalAiWorker::LocalAiWorker (ThoughtSampler& target) : juce::Thread ("Circat Thought AI"), sampler (target)
{
    startLocalStack();
    startThread();
}

LocalAiWorker::~LocalAiWorker()
{
    signalThreadShouldExit();
    notify();
    stopThread (3000);
}

void LocalAiWorker::request (juce::String prompt)
{
    prompt = prompt.trim();
    if (prompt.isEmpty()) return;
    {
        const juce::ScopedLock lock (requestLock);
        pendingPrompt = std::move (prompt);
        pendingReferencePath = referenceAudioPath;
    }
    status.store (Status::generating, std::memory_order_release);
    {
        const juce::ScopedLock lock (statusLock);
        statusText = "Generating locally…";
    }
    notify();
}

void LocalAiWorker::setReferenceAudio (juce::File file)
{
    const juce::ScopedLock lock (requestLock);
    referenceAudioPath = file.existsAsFile() ? file.getFullPathName() : juce::String();
}
void LocalAiWorker::loadModel() { modelCommand.store (1); notify(); }
void LocalAiWorker::unloadModel() { modelCommand.store (2); notify(); }

LocalAiWorker::Status LocalAiWorker::getStatus() const noexcept { return status.load (std::memory_order_acquire); }

juce::String LocalAiWorker::getStatusText() const
{
    const juce::ScopedLock lock (statusLock);
    return statusText;
}

void LocalAiWorker::startLocalStack()
{
   #if JUCE_WINDOWS
    auto root = juce::File (juce::SystemStats::getEnvironmentVariable ("CIRCAT_THOUGHT_HOME", {}));
    if (! root.isDirectory())
    {
        root = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
        for (int i = 0; i < 8; ++i)
        {
            if (root.getChildFile ("backend/start_stable_audio.bat").existsAsFile()) break;
            root = root.getParentDirectory();
        }
    }

    const auto script = root.getChildFile ("backend/start_stable_audio.bat");
    if (! script.existsAsFile())
    {
        const juce::ScopedLock lock (statusLock);
        statusText = "Local AI setup missing — run install_circat_thought.bat";
        return;
    }

    backendStarter = std::make_unique<juce::ChildProcess>();
    if (backendStarter->start ({ "cmd.exe", "/c", script.getFullPathName() }))
    {
        const juce::ScopedLock lock (statusLock);
        statusText = "Starting Stable Audio Open…";
    }
   #endif
}

void LocalAiWorker::run()
{
    int healthTicks = 0;
    while (! threadShouldExit())
    {
        wait (250);
        const int command = modelCommand.exchange (0);
        if (command != 0) { postModelCommand (command == 1 ? "/v1/model/load" : "/v1/model/unload"); continue; }
        juce::String prompt;
        juce::String referencePath;
        {
            const juce::ScopedLock lock (requestLock);
            prompt = pendingPrompt;
            referencePath = pendingReferencePath;
            pendingPrompt.clear();
            pendingReferencePath.clear();
        }
        if (prompt.isEmpty())
        {
            if (++healthTicks >= 8)
            {
                healthTicks = 0;
                if (getStatus() != Status::generating) refreshHealth();
            }
            continue;
        }

        juce::String error;
        const bool ok = generate (prompt, referencePath, error);
        status.store (ok ? Status::ready : Status::error, std::memory_order_release);
        const juce::ScopedLock lock (statusLock);
        statusText = ok ? "Sample ready — play MIDI" : "AI error: " + error;
    }
}

void LocalAiWorker::postModelCommand (const juce::String& path)
{
    int httpStatus = 0;
    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
        .withConnectionTimeoutMs (5000).withStatusCode (&httpStatus);
    auto stream = std::unique_ptr<juce::InputStream> (juce::URL ("http://127.0.0.1:8585" + path).withPOSTData ("{}").createInputStream (options));
    const juce::ScopedLock lock (statusLock);
    statusText = (stream != nullptr && httpStatus < 300) ? "Stable Audio model command accepted" : "Model command failed";
}

void LocalAiWorker::refreshHealth()
{
    int httpStatus = 0;
    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
        .withConnectionTimeoutMs (1500)
        .withStatusCode (&httpStatus);
    auto stream = std::unique_ptr<juce::InputStream> (juce::URL ("http://127.0.0.1:8585/health").createInputStream (options));
    if (stream == nullptr || httpStatus != 200)
    {
        const juce::ScopedLock lock (statusLock);
        statusText = "Local AI starting — waiting for bridge";
        return;
    }

    juce::MemoryBlock response;
    stream->readIntoMemoryBlock (response, 64 * 1024);
    const auto parsed = juce::JSON::parse (response.toString());
    const auto* object = parsed.getDynamicObject();
    if (object == nullptr)
        return;

    const bool modelReady = (bool) object->getProperty ("model_ready");
    const auto state = object->getProperty ("status").toString();
    const double loadSeconds = (double) object->getProperty ("load_seconds");
    const juce::ScopedLock lock (statusLock);
    if (modelReady)
        statusText = "Stable Audio Open ready";
    else if (state == "unloaded")
        statusText = "Model unloaded — GPU memory free";
    else if (state == "loading")
        statusText = "Loading Stable Audio Open… " + juce::String (loadSeconds, 1) + " s";
    else
        statusText = "Stable Audio Open: " + state;
}

bool LocalAiWorker::generate (const juce::String& prompt, const juce::String& referencePath, juce::String& error)
{
    auto body = juce::JSON::toString (juce::var (new juce::DynamicObject()));
    auto payload = juce::DynamicObject::Ptr (new juce::DynamicObject());
    payload->setProperty ("prompt", prompt.substring (0, 512));
    payload->setProperty ("duration", 3.0);
    payload->setProperty ("sample_rate", 44100);
    if (referencePath.isNotEmpty()) payload->setProperty ("reference_audio_path", referencePath);
    body = juce::JSON::toString (juce::var (payload.get()));

    juce::URL endpoint ("http://127.0.0.1:8585/v1/generate.wav");
    int httpStatus = 0;
    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
        .withExtraHeaders ("Content-Type: application/json\r\n")
        // Local generation can take longer while Stable Audio loads weights.
        // This is a worker-thread wait only; the audio thread remains untouched.
        .withConnectionTimeoutMs (600000)
        .withStatusCode (&httpStatus);
    auto stream = std::unique_ptr<juce::InputStream> (endpoint.withPOSTData (body).createInputStream (options));
    if (stream == nullptr) { error = "bridge connection failed (start backend/mock_bridge.py)"; return false; }
    if (httpStatus < 200 || httpStatus >= 300) { error = "bridge HTTP " + juce::String (httpStatus); return false; }

    juce::MemoryBlock response;
    if (stream->readIntoMemoryBlock (response, 8 * 1024 * 1024) <= 0) { error = "empty response"; return false; }
    std::unique_ptr<juce::InputStream> input = std::make_unique<juce::MemoryInputStream> (response, false);
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (std::move (input)));
    if (reader == nullptr || reader->lengthInSamples < 2 || reader->lengthInSamples > 600000 || reader->numChannels < 1)
    { error = "unsupported WAV from bridge"; return false; }

    juce::AudioBuffer<float> audio ((int) juce::jmin ((unsigned int) 2, reader->numChannels), (int) reader->lengthInSamples);
    if (! reader->read (&audio, 0, audio.getNumSamples(), 0, true, true)) { error = "WAV decode failed"; return false; }
    trimToEvent (audio, reader->sampleRate);
    sampler.setSampleData (std::make_shared<ThoughtSampleData> (audio, reader->sampleRate, 60));
    return true;
}

void LocalAiWorker::trimToEvent (juce::AudioBuffer<float>& audio, double sampleRate)
{
    if (audio.getNumSamples() < 128 || audio.getNumChannels() < 1)
        return;

    float peak = 0.0f;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            peak = juce::jmax (peak, std::abs (audio.getSample (channel, sample)));
    if (peak < 0.0001f)
        return;

    const auto crosses = [&audio] (int sample, float threshold)
    {
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            if (std::abs (audio.getSample (channel, sample)) >= threshold)
                return true;
        return false;
    };

    int first = 0, last = audio.getNumSamples() - 1;
    while (first < last && ! crosses (first, peak * 0.08f)) ++first;
    while (last > first && ! crosses (last, peak * 0.012f)) --last;
    const int preroll = (int) std::round (sampleRate * 0.012);
    const int postroll = (int) std::round (sampleRate * 0.040);
    first = juce::jmax (0, first - preroll);
    last = juce::jmin (audio.getNumSamples() - 1, last + postroll);
    // ACE may return a musically valid loop even when asked for a one-shot.
    // The sampler deliberately keeps only the attack and its short decay.
    const int oneShotLimit = (int) std::round (sampleRate * 2.5);
    last = juce::jmin (last, first + oneShotLimit);
    if (last - first < 128)
        return;

    juce::AudioBuffer<float> trimmed (audio.getNumChannels(), last - first + 1);
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        trimmed.copyFrom (channel, 0, audio, channel, first, trimmed.getNumSamples());
    const int fadeSamples = juce::jmin ((int) std::round (sampleRate * 0.005), trimmed.getNumSamples() / 2);
    for (int channel = 0; channel < trimmed.getNumChannels(); ++channel)
        for (int sample = 0; sample < fadeSamples; ++sample)
        {
            const float gain = (float) sample / (float) fadeSamples;
            trimmed.setSample (channel, sample, trimmed.getSample (channel, sample) * gain);
            const int tail = trimmed.getNumSamples() - 1 - sample;
            trimmed.setSample (channel, tail, trimmed.getSample (channel, tail) * gain);
        }
    audio.makeCopyOf (trimmed, true);
}
