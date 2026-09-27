"""Verify the exact Android-produced project and media used by native iOS tests."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1] / "engine/tests/data/manual-editing"
manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
for name, expected in manifest["files"].items():
    if Path(name).name != name:
        raise SystemExit("Fixture name must be a single file name")
    data = (root / name).read_bytes()
    if len(data) != expected["bytes"] or hashlib.sha256(data).hexdigest() != expected["sha256"]:
        raise SystemExit(f"Android editing fixture changed: {name}")
print("Android editing fixture: project and two media files verified")
