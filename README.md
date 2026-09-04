# Circat Thought

Local AI sampler for VST3/AU and Standalone. Type a prompt, generate a one-shot with **Stable Audio Open**, and play it chromatically over MIDI. Audio and prompts stay local.

## Licence

Circat Thought source code is licensed under **GPL-3.0-or-later**. Stable Audio
Open weights are downloaded separately and remain subject to the Stability AI
Community License; see [MODEL_LICENSES.md](MODEL_LICENSES.md) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Windows / NVIDIA CUDA installation

Requirements: Windows 10/11 x64, current NVIDIA CUDA driver (24 GB VRAM recommended), Git, [`uv`](https://docs.astral.sh/uv/), and 20–30 GB free disk space. No Administrator rights are needed.

1. Accept the license for [`stabilityai/stable-audio-open-1.0`](https://huggingface.co/stabilityai/stable-audio-open-1.0) and create a Hugging Face read token.
2. Run `install_circat_thought.bat` from this repository.
3. Enter the token when asked. The installer creates `%LOCALAPPDATA%\Circat\CircatThought\StableAudioOpen\.venv`, installs CUDA PyTorch and Stable Audio Open, and logs in to Hugging Face locally.
4. Run `backend\start_stable_audio.bat`, launch the Standalone/plugin, then press **LOAD MODEL**.

The model is not loaded automatically. The UI shows loading time and provides **UNLOAD MODEL** to release VRAM. Only one bridge uses port 8585.

If `uv` is missing: `winget install --id Astral-sh.uv --exact`.

## macOS (Apple Silicon / CPU)

Use Python 3.11 and `uv`; install platform-appropriate PyTorch, `stable-audio-tools`, and `huggingface_hub`, accept the model license, then run `hf auth login`. MPS may work on Apple Silicon; CPU generation is substantially slower. AU distribution also requires Xcode signing and notarization.

## Troubleshooting

| Symptom | Fix |
| --- | --- |
| Bridge connection failed | Run `backend\start_stable_audio.bat` and wait for health status. |
| 401/403 during model load | Accept the model license and repeat `hf auth login`. |
| CUDA out of memory | Press **UNLOAD MODEL**, close GPU applications, or use CPU/MPS. |

## Developer build

Requires CMake 3.22+, Visual Studio 2022 Desktop C++, and Git.

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target CircatThought_Standalone
```

VST3: add `-DCIRCAT_BUILD_VST3=ON`. AU on macOS: add `-DCIRCAT_BUILD_AU=ON`.
