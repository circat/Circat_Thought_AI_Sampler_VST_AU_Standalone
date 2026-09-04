"""Loopback bridge for Stable Audio Open one-shot generation."""

from __future__ import annotations

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
_model = _config = None
_lock = threading.Lock()
_state, _load_started_at, _last_error = "unloaded", None, ""


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


def get_model():
    global _model, _config, _state, _load_started_at, _last_error
    with _lock:
        if _model is None:
            _state, _load_started_at, _last_error = "loading", time.monotonic(), ""
            try:
                import torch
                from stable_audio_tools import get_pretrained_model
                _model, _config = get_pretrained_model("stabilityai/stable-audio-open-1.0")
                _model = _model.to("cuda" if torch.cuda.is_available() else "cpu").eval()
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


def generate(prompt: str, duration: float, steps: int = 100, cfg: float = 6.0, seed: int = -1) -> bytes:
    import torch
    from stable_audio_tools.inference.generation import generate_diffusion_cond

    model, config = get_model()
    rate = int(config["sample_rate"])
    device = "cuda" if torch.cuda.is_available() else "cpu"
    seed = secrets.randbelow(2**31 - 1) if seed < 0 else seed
    conditioning = [{"prompt": (
        prompt + ", isolated sampler one-shot, single sustained note or chord, dry studio, "
        "no drums, no rhythm, no melody, no sequence, no loop, no vocals, zero reverb"
    ), "seconds_start": 0, "seconds_total": duration}]
    with torch.inference_mode():
        audio = generate_diffusion_cond(model, conditioning=conditioning, steps=steps, cfg_scale=cfg,
                                       seed=seed, sample_size=int(config["sample_size"]), device=device)[0]
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
            elapsed = round(time.monotonic() - _load_started_at, 1) if _load_started_at else 0.0
            self.send_json(200, {"status": _state, "provider": "stable-audio-open",
                                 "model_ready": _model is not None, "load_seconds": elapsed, "error": _last_error})
        else: self.send_json(404, {"error": "not found"})

    def do_POST(self) -> None:
        if self.path == "/v1/model/load": load_async(); self.send_json(202, {"status": _state}); return
        if self.path == "/v1/model/unload": unload_model(); self.send_json(200, {"status": _state}); return
        if self.path != "/v1/generate.wav": self.send_json(404, {"error": "not found"}); return
        try:
            if _state != "ready": raise RuntimeError("model is not loaded — press LOAD MODEL")
            body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", "0"))))
            prompt, duration = body.get("prompt", ""), float(body.get("duration", 3.0))
            steps = int(body.get("steps", 100)); cfg = float(body.get("cfg", 6.0)); seed = int(body.get("seed", -1))
            if not isinstance(prompt, str) or not prompt.strip() or not 1 <= duration <= 6: raise ValueError("prompt and 1-6 second duration required")
            if not 10 <= steps <= 250 or not 1.0 <= cfg <= 12.0 or not -1 <= seed < 2**31: raise ValueError("invalid steps, cfg, or seed")
            data = generate(prompt.strip(), duration, steps, cfg, seed)
            self.send_response(200); self.send_header("Content-Type", "audio/wav"); self.send_header("Content-Length", str(len(data)))
            self.end_headers(); self.wfile.write(data)
        except Exception as error:
            self.send_json(503, {"error": str(error)})

    def log_message(self, *_): pass


if __name__ == "__main__":
    # The batch file can be invoked by several DAW/plugin instances at once.
    # A file lock is acquired before binding or loading any model, so exactly
    # one bridge process may continue.
    instance_lock = acquire_single_instance_lock()
    if instance_lock is None:
        raise SystemExit(0)
    ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()
