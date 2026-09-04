# Circat Thought Developer Handoff

## Purpose

Circat Thought is a local-AI sampler instrument. A user enters a prompt, the local Stable Audio Open service generates a WAV, and the plugin loads it for chromatic MIDI playback. The repository currently targets a JUCE Standalone application and can be configured for VST3; AU is prepared for macOS builds.

## Repository layout

- `Source/` contains the JUCE processor, editor, asynchronous AI worker, and `ThoughtSampler` engine.
- `backend/` contains the Stable Audio Open loopback bridge and its Windows launcher.
- `tests/` contains the sampler smoke test.
- `tools/` contains development/integration utilities.
- `CMakeLists.txt` defines the JUCE fetch, plugin formats, and smoke-test target.
- `install_circat_thought.bat` installs the Windows local runtime and dependencies.
- `MODEL_LICENSES.md` and `THIRD_PARTY_NOTICES.md` document external licenses.

## Build targets

JUCE 7.0.9 is fetched by CMake. The default format is Standalone. Configure with `-DCIRCAT_BUILD_VST3=ON` for VST3. On macOS, `-DCIRCAT_BUILD_AU=ON` additionally enables AU.

Example Windows build:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCIRCAT_BUILD_VST3=ON
cmake --build build --config Release
```

The `CircatThoughtSamplerSmoke` executable exercises the sampler without the AI runtime:

```powershell
cmake --build build --config Release --target CircatThoughtSamplerSmoke
.
\build\Release\CircatThoughtSamplerSmoke.exe
```

## Stable Audio Open bridge

`backend/stable_audio_bridge.py` serves HTTP only on `127.0.0.1:8585`. The Windows runtime is installed below `%LOCALAPPDATA%\Circat\CircatThought\StableAudioOpen`; the model weights are downloaded to the user Hugging Face cache and are not stored in this repository.

The bridge binds its port before model work and uses a Windows file lock (`stable_audio_bridge.lock`) so only one bridge instance can run. A second launcher must exit instead of creating another model process. The plugin communicates with `/health`, `/v1/model/load`, `/v1/model/unload`, and `/v1/generate.wav`.

Model loading is automatic: `LocalAiWorker` requests a load on startup and, on the first `GENERATE`, `ensureModelReady()` polls `/health` (`unloaded`, `loading`, `ready`, `error`) until the model is ready before sending the generate request. There is no user-facing load/unload button. `/v1/model/unload` still exists on the bridge for tooling.

To start the service manually on Windows:

```powershell
backend\start_stable_audio.bat
```

## Sampler engine

`ThoughtSampler` currently provides polyphonic MIDI playback, normalized sample regions, start/end playback points, amplitude ADSR, an input drive stage, filter modes (bypass, low-pass, high-pass, band-pass), filter resonance, filter ADSR and envelope amount, and loop playback. Loop modes are forward loop and alternate/ping-pong loop, with configurable loop start, loop end, and crossfade time. Processing avoids allocations and locks on the audio thread.

The WULF input stage is an optional local development integration. CMake uses `D:/VSTPluginsDev/WULF-AD/Source/dsp/WulfAD.cpp` when that path exists; the project still builds without it.

## User interface

The editor uses the Circat/S612-inspired bordeaux velvet and brass visual language. It includes AI prompt/status controls, model load/unload controls, waveform display, sample start/end controls, loop controls, amplitude controls, filter controls, and WULF input drive. The waveform is drawn from the currently loaded sample.

## Installation and testing

Run `install_circat_thought.bat` as documented in `README.md`. The installer creates the Python virtual environment, installs Stable Audio dependencies, authenticates with Hugging Face, and keeps the large model outside the repository. Use the bridge health endpoint to diagnose runtime state:

```powershell
Invoke-WebRequest http://127.0.0.1:8585/health | Select-Object -Expand Content
```

For a real generation/integration check, use the documented integration smoke utility after the model is loaded. Keep generated audio and runtime caches out of Git.

## Licensing

Repository code is GPL-3.0-or-later. JUCE is used under its applicable GPL terms; a closed-source release requires a suitable JUCE commercial license. `stable-audio-tools` is MIT, but Stable Audio Open model weights are governed separately by the Stability AI Community License and AUP. The model is downloaded by each user and is not redistributed here. WULF-AD remains an external, optional source dependency.

## Known limitations and next work

Priority order:

1. Persist all sampler controls (loop, filter, ADSR, drive) in plugin state and restore them reliably.
2. Expose Stable Audio generation parameters in the UI and pass duration, steps, CFG, seed, and prompt building blocks end to end.
3. Improve lifecycle diagnostics and verify that all plugin close paths leave no orphan bridge process.
4. Add a sample browser and preset save/load workflow.
5. Add sample slicing and transient-based region editing later; slicing is intentionally deferred.
6. Complete and test VST3 packaging, then implement and validate the AU release on macOS.
7. Remove remaining legacy/mock/ACE terminology from documentation and development-only code where appropriate.

Do not place model weights, virtual environments, generated WAV files, or local build products in the repository. Keep real-time audio code allocation-free and free of network calls.
