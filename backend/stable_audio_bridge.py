"""Loopback bridge for Stable Audio Open one-shot generation."""

from __future__ import annotations

import contextlib
import io
import json
import os
import secrets
import threading
import time
import gc
import wave
from pathlib import Path
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HOST, PORT = "127.0.0.1", 8585

# CIRCAT_SAO_MODEL=small selects the distilled ~340 M Stable Audio Open small
# model (much less VRAM, a few steps, faster). Anything else = the full 1.0 DiT.
_MODEL_IDS = {
    "small": "stabilityai/stable-audio-open-small",
    "full": "stabilityai/stable-audio-open-1.0",
}
_MODEL_KEY = "small" if os.environ.get("CIRCAT_SAO_MODEL", "").lower() == "small" else "full"
_MODEL_ID = _MODEL_IDS[_MODEL_KEY]

# Idle watchdog: once at least one client (plugin) has been seen, exit if no
# request arrives for this long. Kills the orphan bridge after every DAW/plugin
# instance has closed without needing a cross-process reference count.
_IDLE_TIMEOUT = float(os.environ.get("CIRCAT_BRIDGE_IDLE_TIMEOUT", "150"))

_model = _config = None
_lock = threading.Lock()
_state, _load_started_at, _last_error = "unloaded", None, ""
_last_activity = time.monotonic()
_seen_client = False
_instance_lock_handle = None


def _touch() -> None:
    global _last_activity, _seen_client
    _last_activity = time.monotonic()
    _seen_client = True


def acquire_single_instance_lock():
    """Keep the lock handle alive for the bridge lifetime (Windows only)."""
    if os.name != "nt":
        return object()
    import msvcrt
    directory = Path(os.environ.get("LOCALAPPDATA", ".")) / "Circat" / "CircatThought"
    directory.mkdir(parents=True, exist_ok=True)
    lock_path = directory / "stable_audio_bridge.lock"
    handle = open(lock_path, "a+b")
    if lock_path.stat().st_size == 0:
        handle.write(b"0")
        handle.flush()
    handle.seek(0)
    try:
        msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
    except OSError:
        handle.close()
        return None
    return handle


def _release_instance_lock() -> None:
    global _instance_lock_handle
    handle = _instance_lock_handle
    _instance_lock_handle = None
    if handle is None or os.name != "nt":
        return
    try:
        import msvcrt
        handle.seek(0)
        msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
        handle.close()
    except Exception:
        pass


def _watchdog() -> None:
    while True:
        time.sleep(20.0)
        if _seen_client and (time.monotonic() - _last_activity) > _IDLE_TIMEOUT:
            try:
                unload_model()
            except Exception:
                pass
            _release_instance_lock()
            os._exit(0)


def get_model():
    global _model, _config, _state, _load_started_at, _last_error
    with _lock:
        if _model is None:
            _state, _load_started_at, _last_error = "loading", time.monotonic(), ""
            try:
                import torch
                from stable_audio_tools import get_pretrained_model
                # TF32 matmuls: large speedup on Ampere+ (RTX 3090) at no audible cost.
                torch.backends.cuda.matmul.allow_tf32 = True
                torch.backends.cudnn.allow_tf32 = True
                torch.backends.cudnn.benchmark = True
                _model, _config = get_pretrained_model(_MODEL_ID)
                device = "cuda" if torch.cuda.is_available() else "cpu"
                _model = _model.to(device).eval()
                if device == "cuda" and os.environ.get("CIRCAT_COMPILE") == "1":
                    try:
                        _model.model = torch.compile(_model.model, mode="reduce-overhead")
                    except Exception:
                        pass
                _state = "ready"
            except Exception as error:
                _model = _config = None; _state, _last_error = "error", str(error)
                raise
    return _model, _config


def load_async() -> None:
    if _state in ("loading", "ready"): return
    threading.Thread(target=get_model, name="stable-audio-model-loader", daemon=True).start()


def unload_model() -> None:
    global _model, _config, _state, _load_started_at
    with _lock:
        if _model is not None:
            import torch
            _model.to("cpu")
            _model = _config = None
            gc.collect()
            if torch.cuda.is_available(): torch.cuda.empty_cache()
        _state, _load_started_at = "unloaded", None


_NEGATIVE_PROMPT = (
    "drums, percussion, drum machine, beat, rhythm, groove, hi-hat, kick, snare, "
    "arpeggio, sequence, melody line, riff, bassline, vocals, singing, speech, "
    "reverb, delay, echo, room ambience"
)


def generate(prompt: str, duration: float, steps: int = 14, cfg: float = 6.0, seed: int = -1) -> bytes:
    import torch
    from stable_audio_tools.inference.generation import generate_diffusion_cond

    model, config = get_model()
    rate = int(config["sample_rate"])
    device = "cuda" if torch.cuda.is_available() else "cpu"
    seed = secrets.randbelow(2**31 - 1) if seed < 0 else seed

    # The prompt is passed through as authored (templates and the UI already
    # carry the "dry studio / one-shot" wording). Everything we want to keep OUT
    # of the render goes through real negative conditioning instead of being
    # appended as positive text.
    conditioning = [{"prompt": prompt, "seconds_start": 0, "seconds_total": duration}]
    negative = [{"prompt": _NEGATIVE_PROMPT, "seconds_start": 0, "seconds_total": duration}]

    if steps > 12 and _MODEL_KEY == "small":
        steps = 12  # the distilled model needs only a handful of pingpong steps

    autocast = (torch.autocast("cuda", dtype=torch.float16)
                if device == "cuda" else contextlib.nullcontext())
    with torch.inference_mode(), autocast:
        audio = generate_diffusion_cond(
            model, conditioning=conditioning, negative_conditioning=negative,
            steps=steps, cfg_scale=cfg, seed=seed,
            sample_size=int(config["sample_size"]),
            sampler_type="pingpong", sigma_min=0.03, sigma_max=500, device=device)[0]
    audio = audio[:, :int(rate * duration)].detach().float().cpu().clamp(-1, 1)
    pcm = (audio * 32767).to(torch.int16).transpose(0, 1).numpy().tobytes()
    result = io.BytesIO()
    with wave.open(result, "wb") as wav:
        wav.setnchannels(audio.shape[0]); wav.setsampwidth(2); wav.setframerate(rate); wav.writeframes(pcm)
    return result.getvalue()


class Handler(BaseHTTPRequestHandler):
    def send_json(self, status: int, data: dict) -> None:
        blob = json.dumps(data).encode(); self.send_response(status)
        self.send_header("Content-Type", "application/json"); self.send_header("Content-Length", str(len(blob)))
        self.end_headers(); self.wfile.write(blob)

    def do_GET(self) -> None:
        if self.path == "/health":
            _touch()
            elapsed = round(time.monotonic() - _load_started_at, 1) if _load_started_at else 0.0
            self.send_json(200, {"status": _state, "provider": "stable-audio-open",
                                 "model": _MODEL_KEY, "model_id": _MODEL_ID,
                                 "model_ready": _model is not None, "load_seconds": elapsed,
                                 "error": _last_error})
        else: self.send_json(404, {"error": "not found"})

    def do_POST(self) -> None:
        _touch()
        if self.path == "/v1/model/load": load_async(); self.send_json(202, {"status": _state}); return
        if self.path == "/v1/model/unload": unload_model(); self.send_json(200, {"status": _state}); return
        if self.path == "/v1/shutdown":
            self.send_json(200, {"status": "stopping"})
            try: unload_model()
            except Exception: pass
            _release_instance_lock()
            threading.Thread(target=lambda: (time.sleep(0.2), os._exit(0)), daemon=True).start()
            return
        if self.path != "/v1/generate.wav": self.send_json(404, {"error": "not found"}); return
        try:
            if _state != "ready": raise RuntimeError("model is not loaded")
            body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", "0"))))
            prompt, duration = body.get("prompt", ""), float(body.get("duration", 3.0))
            steps = int(body.get("steps", 14)); cfg = float(body.get("cfg", 6.0)); seed = int(body.get("seed", -1))
            if not isinstance(prompt, str) or not prompt.strip() or not 1 <= duration <= 6: raise ValueError("prompt and 1-6 second duration required")
            if not 4 <= steps <= 250 or not 1.0 <= cfg <= 12.0 or not -1 <= seed < 2**31: raise ValueError("invalid steps, cfg, or seed")
            data = generate(prompt.strip(), duration, steps, cfg, seed)
            _touch()
            self.send_response(200); self.send_header("Content-Type", "audio/wav"); self.send_header("Content-Length", str(len(data)))
            self.end_headers(); self.wfile.write(data)
        except Exception as error:
            self.send_json(503, {"error": str(error)})

    def log_message(self, *_): pass


if __name__ == "__main__":
    # The batch file can be invoked by several DAW/plugin instances at once.
    # A file lock is acquired before binding or loading any model, so exactly
    # one bridge process may continue.
    _instance_lock_handle = acquire_single_instance_lock()
    if _instance_lock_handle is None:
        raise SystemExit(0)
    threading.Thread(target=_watchdog, name="bridge-idle-watchdog", daemon=True).start()
    # Warm the model straight away so the first GENERATE in the plugin does not
    # have to wait for the full load; the plugin also triggers this over HTTP.
    load_async()
    ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()
