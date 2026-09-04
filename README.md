# Circat Thought VSTi

**Status: Beta 0.8.3** — feature-complete for testing; APIs and preset format may still change.

Local AI sampler for VST3/AU and Standalone. Type a prompt, generate a one-shot with **Stable Audio Open**, and play it chromatically over MIDI. Audio and prompts stay local.

https://huggingface.co/stabilityai/stable-audio-open-1.0

## Licence

Circat Thought source code is licensed under **GPL-3.0-or-later**. Stable Audio
Open weights are downloaded separately and remain subject to the Stability AI
Community License; see [MODEL_LICENSES.md](MODEL_LICENSES.md) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## System requirements

| | |
| --- | --- |
| OS | Windows 10/11 x64 (64-bit) |
| GPU | NVIDIA with current CUDA driver, **8 GB VRAM minimum**, 12 GB+ recommended. CPU fallback works but is very slow. |
| CPU / RAM | any modern x64 CPU, 16 GB system RAM |
| Disk — during install | **~25–30 GB free.** CUDA PyTorch wheels ~5–7 GB, pip/uv download cache ~3–5 GB, model weights ~9 GB, virtualenv ~2 GB. |
| Disk — steady state | **~13–16 GB.** `%LOCALAPPDATA%\Circat\CircatThought\StableAudioOpen\.venv` ~8–10 GB + Hugging Face model cache ~9 GB (`%USERPROFILE%\.cache\huggingface`), minus the freed download caches. |
| Tools | Git, [`uv`](https://docs.astral.sh/uv/). No Administrator rights needed. |

### Model

Stable Audio Open 1.0 (`stabilityai/stable-audio-open-1.0`): DiT audio-diffusion
model (~1.2 B parameters) plus a T5 text encoder. Total download ≈ **9 GB**
(`model.safetensors` ≈ 4.9 GB, T5 encoder ≈ 3.5 GB, configs). Weights are pulled
into the standard Hugging Face cache and are **not** bundled with this repo.

Inference uses ≈ 6–8 GB VRAM at fp16. The plugin's **QUALITY** switch picks the
trade-off: **FAST** (14 pingpong steps, a few seconds on a 24 GB NVIDIA card),
**BALANCED** (40 steps, dpmpp-3m-sde), **QUALITY** (110 steps). The `STEPS`
slider still overrides the count. The first generation after the bridge starts
is slower (kernel warm-up).

### Reference test system

Verified on: Windows 11 x64, NVIDIA GeForce RTX 3090 (24 GB), CUDA 12.x driver,
Python 3.11, 64 GB RAM. Standalone + VST3 built with Visual Studio 2022 / CMake,
JUCE 7.0.9.

## Windows / NVIDIA CUDA installation

1. Accept the license for [`stabilityai/stable-audio-open-1.0`](https://huggingface.co/stabilityai/stable-audio-open-1.0) and create a Hugging Face read token.
2. Run `install_circat_thought.bat` from this repository.
3. Enter the token when asked. The installer creates `%LOCALAPPDATA%\Circat\CircatThought\StableAudioOpen\.venv`, installs CUDA PyTorch and Stable Audio Open, and logs in to Hugging Face locally. The token is handed straight to `hf auth login`; it is never written to this repository or to any tracked file.
4. Run `backend\start_stable_audio.bat`, then launch the Standalone/plugin.

The bridge loads the model on its own the first time you press **GENERATE** —
there is no separate load step. Only one bridge uses port 8585.

If `uv` is missing: `winget install --id Astral-sh.uv --exact`.

### Bridge environment variables (optional)

| Variable | Default | Effect |
| --- | --- | --- |
| `CIRCAT_SAO_MODEL` | `full` | `small` loads **Stable Audio Open small** (~340 M, ~4 GB VRAM, a few steps, much faster, slightly lower fidelity). |
| `CIRCAT_COMPILE` | off | `1` enables `torch.compile` (slower first run, faster after). |
| `CIRCAT_BRIDGE_IDLE_TIMEOUT` | `150` | Seconds without any plugin request before the bridge unloads the model and exits, so it never lingers after the DAW closes. |

The bridge exits on its own once every plugin instance is gone; nothing is left
running or holding VRAM.

## macOS (Apple Silicon / CPU)

Use Python 3.11 and `uv`; install platform-appropriate PyTorch, `stable-audio-tools`, and `huggingface_hub`, accept the model license, then run `hf auth login`. MPS may work on Apple Silicon; CPU generation is substantially slower. AU distribution also requires Xcode signing and notarization.

## Troubleshooting

| Symptom | Fix |
| --- | --- |
| Bridge connection failed | Run `backend\start_stable_audio.bat` and wait for health status. |
| 401/403 during model load | Accept the model license and repeat `hf auth login`. |
| CUDA out of memory | Close other GPU applications, or run the bridge on CPU/MPS. |
| First generation hangs for a minute | Expected — the model loads on first GENERATE and CUDA kernels warm up. |

## Developer build

Requires CMake 3.22+, Visual Studio 2022 Desktop C++, and Git.

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target CircatThought_Standalone
```

VST3: add `-DCIRCAT_BUILD_VST3=ON`. AU on macOS: add `-DCIRCAT_BUILD_AU=ON`.


## Support

Circat Media // Erich Lesovsky  
hello@circat.media  
www.circat.media

buymeacoffee.com/MigraineBoy

## Legal

© 2026 Circat Media // Erich Lesovsky. All rights reserved.