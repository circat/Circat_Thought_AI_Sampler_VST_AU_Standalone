# Stable Audio bridge

`stable_audio_bridge.py` exposes a loopback HTTP service on `127.0.0.1:8585`.
It loads `stabilityai/stable-audio-open-1.0` only after `POST /v1/model/load` (the UI's **LOAD MODEL** button), reports state and `load_seconds` through `/health`, and releases the model with `POST /v1/model/unload`.

`mock_bridge.py` is a deterministic CPU development substitute and does not require model weights.
