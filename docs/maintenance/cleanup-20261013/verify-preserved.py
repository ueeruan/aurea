import hashlib
import json
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[3]
report = Path(__file__).resolve().parent
before = json.loads((report / "protected-files-before.json").read_text(encoding="utf-8"))
problems = []
checked = 0
for rel, prior in before.items():
    path = root / rel
    if prior.get("state") == "already-missing":
        continue
    if not path.is_file():
        problems.append({"file": rel, "problem": "missing"})
        continue
    h = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            h.update(block)
    checked += 1
    if h.hexdigest() != prior["sha256"]:
        problems.append({"file": rel, "problem": "content changed"})
status = subprocess.run(["git", "status", "--porcelain=v1", "-z"], cwd=root, capture_output=True, check=True).stdout
(report / "git-status-after.bin").write_bytes(status)
new_sources = [
    "android/app/src/androidTest/java/com/aurea/aurea/editor/ReferenceEditingDeviceTest.kt",
    "android/app/src/main/java/com/aurea/aurea/engine/KeyframeNavigation.kt",
    "engine/tests/test_editor_reference.cpp",
    "engine/platform/ios/verify/check_timeline_reference.py",
    "docs/parity/alight-motion-5.0.280.md",
]
for rel in new_sources:
    if not (root / rel).is_file():
        problems.append({"file": rel, "problem": "new source missing"})
result = {"protected_files_verified": checked, "problems": problems, "new_sources_present": len(new_sources), "git_status_unchanged": status == (report / "git-status-before.bin").read_bytes()}
(report / "preservation-result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(json.dumps(result, ensure_ascii=False))
if problems:
    raise SystemExit(1)
