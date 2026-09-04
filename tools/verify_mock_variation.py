"""Verify exactly which prompt fields affect the current mock bridge."""

from __future__ import annotations

import hashlib
import json
import urllib.request


PROMPTS = [
    "Brass, D-minor chord, rising",
    "Piano, D-minor chord, rising",
    "Brass, C-major chord",
    "Brass, D-minor chord",
]


def request_wav(prompt: str) -> bytes:
    payload = json.dumps({"prompt": prompt, "duration": 0.2, "sample_rate": 44100}).encode()
    request = urllib.request.Request(
        "http://127.0.0.1:8585/v1/generate.wav",
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=10) as response:
        return response.read()


def main() -> None:
    for prompt in PROMPTS:
        wav = request_wav(prompt)
        print(json.dumps({"prompt": prompt, "sha256": hashlib.sha256(wav).hexdigest(), "bytes": len(wav)}))


if __name__ == "__main__":
    main()
