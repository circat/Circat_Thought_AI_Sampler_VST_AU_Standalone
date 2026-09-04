from __future__ import annotations

from pathlib import Path

root = Path(r"D:\VSTPluginsDev\WULF-AD")
for suffix in ("*.cpp", "*.h"):
    for path in root.rglob(suffix):
        if "build" not in path.parts:
            print(path)
