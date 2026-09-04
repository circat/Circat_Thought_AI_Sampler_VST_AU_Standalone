"""End-to-end test for Circat bridge -> ACE-Step -> sampler-compatible WAV."""

from __future__ import annotations

import json
from pathlib import Path
import struct
import sys
from urllib.error import HTTPError
import urllib.request


payload = json.dumps({
    "prompt": "warm brass D-minor chord, rising, cinematic, dry",
    "duration": 3.0,
    "steps": 10,
    "cfg": 6.0,
    "seed": 12345,
    "sample_rate": 44100,
}).encode("utf-8")
request = urllib.request.Request(
    "http://127.0.0.1:8585/v1/generate.wav",
    data=payload,
    headers={"Content-Type": "application/json"},
    method="POST",
)

try:
    with urllib.request.urlopen(request, timeout=900) as response:
        wav_data = response.read()
    fmt_offset = wav_data.index(b"fmt ")
    data_offset = wav_data.index(b"data")
    audio_format, channels, sample_rate, _, _, sample_width = struct.unpack_from("<HHIIHH", wav_data, fmt_offset + 8)
    data_bytes = struct.unpack_from("<I", wav_data, data_offset + 4)[0]
    frames = data_bytes // (channels * (sample_width // 8))
    info = {
        "riff": wav_data[:4].decode("ascii", errors="replace"),
        "bytes": len(wav_data),
        "audio_format": audio_format,
        "channels": channels,
        "sample_rate": sample_rate,
        "frames": frames,
        "duration_seconds": round(frames / sample_rate, 3),
        "sample_width_bits": sample_width,
    }
    output = Path(__file__).resolve().parents[1] / "runtime" / "integration_generated.wav"
    output.write_bytes(wav_data)
    if info["riff"] != "RIFF" or info["audio_format"] not in (1, 3) or not 1 <= info["channels"] <= 2 or not 2 <= info["frames"] <= 600000:
        raise RuntimeError(f"sampler validation failed: {info}")
    print(json.dumps({"ok": True, **info}))
except HTTPError as error:
    print(json.dumps({"ok": False, "http_status": error.code, "error": error.read().decode("utf-8", errors="replace")}))
    sys.exit(1)
except Exception as error:
    print(json.dumps({"ok": False, "error": str(error)}))
    sys.exit(1)
