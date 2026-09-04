"""Small, dependency-free local AI bridge used by the standalone MVP.

This is deliberately an HTTP process boundary.  A future ACE-Step/MiniMax
adapter can keep the same API while the plugin remains independent of the
model runtime.
"""

from __future__ import annotations

import argparse
import base64
import io
import json
import math
import re
import wave
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any

HOST = "127.0.0.1"
PORT = 8585
MAX_BODY = 64 * 1024
MAX_PROMPT = 1000
MIN_DURATION = 0.05
MAX_DURATION = 10.0
MIN_RATE = 8000
MAX_RATE = 48000


def _error(message: str, status: int = HTTPStatus.BAD_REQUEST) -> tuple[int, dict[str, Any]]:
    return int(status), {"error": message}


def _request_fields(body: bytes) -> tuple[int, dict[str, Any]]:
    if len(body) > MAX_BODY:
        return _error("request body is too large")
    try:
        value = json.loads(body.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return _error("body must be valid UTF-8 JSON")
    if not isinstance(value, dict):
        return _error("body must be a JSON object")
    prompt = value.get("prompt")
    if not isinstance(prompt, str) or not prompt.strip():
        return _error("prompt must be a non-empty string")
    if len(prompt) > MAX_PROMPT:
        return _error("prompt is too long")
    duration = value.get("duration", 1.0)
    # bool is an int in Python, but is not a meaningful duration.
    if isinstance(duration, bool) or not isinstance(duration, (int, float)):
        return _error("duration must be a number")
    if not math.isfinite(float(duration)) or not MIN_DURATION <= float(duration) <= MAX_DURATION:
        return _error(f"duration must be between {MIN_DURATION} and {MAX_DURATION} seconds")
    sample_rate = value.get("sample_rate", 44100)
    if isinstance(sample_rate, bool) or not isinstance(sample_rate, int):
        return _error("sample_rate must be an integer")
    if not MIN_RATE <= sample_rate <= MAX_RATE:
        return _error(f"sample_rate must be between {MIN_RATE} and {MAX_RATE}")
    return 200, {"prompt": prompt.strip(), "duration": float(duration), "sample_rate": sample_rate}


def _frequencies(prompt: str) -> list[float]:
    """Return a simple chord approximation from common prompt notation."""
    names = {"c": 261.63, "d": 293.66, "e": 329.63, "f": 349.23,
             "g": 392.00, "a": 440.00, "b": 493.88}
    match = re.search(r"\b([a-g])\s*[- ]?(minor|major|m|maj|min)?\b", prompt.lower())
    root = names.get(match.group(1), 261.63) if match else 261.63
    quality = match.group(2) if match and match.group(2) else "major"
    intervals = (0, 3, 7) if quality in ("minor", "min", "m") else (0, 4, 7)
    return [root * 2.0 ** (interval / 12.0) for interval in intervals]


def make_wav(prompt: str, duration: float, sample_rate: int) -> bytes:
    frequencies = _frequencies(prompt)
    rising = "rising" in prompt.lower() or "upward" in prompt.lower()
    frames = int(duration * sample_rate)
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        samples = bytearray()
        for index in range(frames):
            t = index / sample_rate
            phase_scale = 1.0 + (0.35 * t / duration if rising else 0.0)
            value = sum(math.sin(2.0 * math.pi * freq * phase_scale * t) for freq in frequencies)
            # short fades avoid clicks when the generated sample is swapped.
            fade = min(1.0, index / max(1, int(0.02 * sample_rate)),
                       (frames - index) / max(1, int(0.03 * sample_rate)))
            pcm = int(max(-1.0, min(1.0, value / len(frequencies) * 0.8 * fade)) * 32767)
            samples += int(pcm).to_bytes(2, "little", signed=True)
        wav.writeframes(samples)
    return output.getvalue()


class BridgeHandler(BaseHTTPRequestHandler):
    server_version = "CircatThoughtMockBridge/0.1"

    def _send_json(self, status: int, payload: dict[str, Any]) -> None:
        encoded = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(encoded)

    def _send_wav(self, wav_data: bytes) -> None:
        self.send_response(200)
        self.send_header("Content-Type", "audio/wav")
        self.send_header("Content-Length", str(len(wav_data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(wav_data)

    def do_GET(self) -> None:  # noqa: N802 (stdlib handler API)
        if self.path == "/health":
            self._send_json(200, {"status": "ok", "service": "mock-bridge", "version": "0.1"})
        else:
            self._send_json(404, {"error": "not found"})

    def do_POST(self) -> None:  # noqa: N802
        if self.path not in ("/v1/generate", "/v1/generate.wav"):
            self._send_json(404, {"error": "not found"})
            return
        try:
            length = int(self.headers.get("Content-Length", "-1"))
        except ValueError:
            length = -1
        if length < 0:
            self._send_json(411, {"error": "Content-Length is required"})
            return
        status, fields = _request_fields(self.rfile.read(min(length, MAX_BODY + 1)))
        if status != 200:
            self._send_json(status, fields)
            return
        wav_data = make_wav(fields["prompt"], fields["duration"], fields["sample_rate"])
        if self.path == "/v1/generate.wav":
            self._send_wav(wav_data)
            return
        encoded = base64.b64encode(wav_data).decode("ascii")
        self._send_json(200, {"wav_base64": encoded, "audio_base64": encoded,
                              "mime_type": "audio/wav", "sample_rate": fields["sample_rate"],
                              "duration": fields["duration"]})

    def log_message(self, fmt: str, *args: Any) -> None:
        print(f"[mock-bridge] {self.address_string()} - {fmt % args}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Circat Thought local mock AI bridge")
    parser.add_argument("--port", type=int, default=PORT)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("port must be between 1 and 65535")
    server = ThreadingHTTPServer((HOST, args.port), BridgeHandler)
    print(f"Circat Thought mock bridge listening on http://{HOST}:{args.port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
