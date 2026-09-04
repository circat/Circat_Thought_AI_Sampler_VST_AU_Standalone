#include "LocalAiWorker.h"

#include <cmath>

void circatLog (const juce::String& line); // defined in PluginProcessor.cpp

juce::File LocalAiWorker::generatedDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("CIRCAT").getChildFile ("CircatThought").getChildFile ("Generated");
    dir.createDirectory();
    return dir;
}

LocalAiWorker::LocalAiWorker (ThoughtSampler& target) : juce::Thread ("Circat Thought AI"), sampler (target)
{
    startLocalStack();
    modelCommand.store (1); // auto-load: the user never presses a "load model" button
    startThread();
}

LocalAiWorker::~LocalAiWorker()
{
    signalThreadShouldExit();
    notify();
    stopThread (3000);
}

void LocalAiWorker::request (juce::String prompt, float duration, int steps, float cfg, int seed)
{
    prompt = prompt.trim();
    if (prompt.isEmpty()) return;
    {
        const juce::ScopedLock lock (requestLock);
        pendingPrompt = std::move (prompt);
        pendingReferencePath = referenceAudioPath;
        pendingDuration = juce::jlimit (1.0f, 6.0f, duration);
        pendingSteps = juce::jlimit (4, 250, steps);
        pendingCfg = juce::jlimit (1.0f, 12.0f, cfg);
        pendingSeed = seed < 0 ? -1 : juce::jlimit (0, 2147483646, seed);
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

void LocalAiWorker::setSamplerType (juce::String type)
{
    const juce::ScopedLock lock (requestLock);
    samplerType = type.isEmpty() ? juce::String ("pingpong") : std::move (type);
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
        juce::String sampler;
        float duration = 3.0f, cfg = 6.0f;
        int steps = 100, seed = -1;
        {
            const juce::ScopedLock lock (requestLock);
            prompt = pendingPrompt;
            referencePath = pendingReferencePath;
            sampler = samplerType;
            pendingPrompt.clear();
            pendingReferencePath.clear();
            duration = pendingDuration; steps = pendingSteps; cfg = pendingCfg; seed = pendingSeed;
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
        circatLog ("generate request: \"" + prompt.substring (0, 120) + "\"");
        {
            const juce::ScopedLock lock (statusLock);
            statusText = "Preparing Stable Audio Open…";
        }
        bool ok = ensureModelReady (error);
        if (ok)
        {
            const juce::ScopedLock lock (statusLock);
            statusText = "Generating locally…";
        }
        if (ok)
            ok = generate (prompt, referencePath, sampler, duration, steps, cfg, seed, error);
        status.store (ok ? Status::ready : Status::error, std::memory_order_release);
        {
            const juce::ScopedLock lock (statusLock);
            statusText = ok ? "Sample ready — play MIDI" : "AI error: " + error;
        }
        circatLog (ok ? "generate ok" : "generate failed: " + error);
    }
}

bool LocalAiWorker::ensureModelReady (juce::String& error)
{
    for (int attempt = 0; attempt < 240 && ! threadShouldExit(); ++attempt)
    {
        int httpStatus = 0;
        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs (2000).withStatusCode (&httpStatus);
        auto stream = std::unique_ptr<juce::InputStream> (
            juce::URL ("http://127.0.0.1:8585/health").createInputStream (options));
        if (stream != nullptr && httpStatus == 200)
        {
            juce::MemoryBlock response;
            stream->readIntoMemoryBlock (response, 64 * 1024);
            const auto parsed = juce::JSON::parse (response.toString());
            if (auto* object = parsed.getDynamicObject())
            {
                const auto state = object->getProperty ("status").toString();
                if ((bool) object->getProperty ("model_ready") || state == "ready")
                    return true;
                if (state == "error")
                {
                    error = object->getProperty ("error").toString();
                    if (error.isEmpty()) error = "model load error";
                    return false;
                }
                if (state == "unloaded" && attempt == 0)
                    postModelCommand ("/v1/model/load");
                const juce::ScopedLock lock (statusLock);
                statusText = state == "loading" ? "Loading Stable Audio Open…" : "Waiting for model…";
            }
        }
        wait (1000);
    }
    error = "model did not become ready — is backend/start_stable_audio.bat running?";
    return false;
}

void LocalAiWorker::pruneGenerated()
{
    auto files = generatedDirectory().findChildFiles (juce::File::findFiles, false, "*.wav");
    if (files.size() <= 60) return;
    files.sort(); // name is timestamped, so lexical == chronological
    for (int i = 0; i < files.size() - 60; ++i)
        files.getReference (i).deleteFile();
}

juce::File LocalAiWorker::getLastGeneratedFile() const
{
    const juce::ScopedLock lock (statusLock);
    return lastGenerated;
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

bool LocalAiWorker::generate (const juce::String& prompt, const juce::String& referencePath, const juce::String& samplerTypeName, float duration, int steps, float cfg, int seed, juce::String& error)
{
    auto body = juce::JSON::toString (juce::var (new juce::DynamicObject()));
    auto payload = juce::DynamicObject::Ptr (new juce::DynamicObject());
    payload->setProperty ("prompt", prompt.substring (0, 512));
    payload->setProperty ("duration", duration);
    payload->setProperty ("steps", steps);
    payload->setProperty ("cfg", cfg);
    payload->setProperty ("seed", seed);
    payload->setProperty ("sample_rate", 44100);
    payload->setProperty ("sampler_type", samplerTypeName.isNotEmpty() ? samplerTypeName : juce::String ("pingpong"));
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
    if (stream == nullptr) { error = "bridge connection failed (start backend/start_stable_audio.bat)"; return false; }
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
    const double outRate = reader->sampleRate;
    sampler.setSampleData (std::make_shared<ThoughtSampleData> (audio, outRate, 60));

    // Keep every generation on disk until the user exports it: the sample
    // browser recalls them and nothing is lost on plugin close.
    auto file = generatedDirectory().getChildFile (
        "thought_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S")
        + "_" + juce::String (juce::Random::getSystemRandom().nextInt (9000) + 1000) + ".wav");
    if (auto out = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
    {
        juce::WavAudioFormat wav;
        if (auto* writer = wav.createWriterFor (out.get(), outRate,
                                                (unsigned int) audio.getNumChannels(), 24, {}, 0))
        {
            out.release();
            writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
            delete writer;
            const juce::ScopedLock lock (statusLock);
            lastGenerated = file;
        }
    }
    pruneGenerated();
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
