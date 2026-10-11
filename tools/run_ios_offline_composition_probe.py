#!/usr/bin/env python3
"""Build and execute shipping Engine -> offscreen Metal -> FFmpeg on iOS Simulator."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import urllib.request

from run_ios_native_export_probe import ROOT, capture, run_logged, simulator

KHT_COMMIT = "edc7c9e67c639d230715049ee31f9a96a6babbbe"
MODELS = (
    ("DamagedHelmet", "a1e3b04de97b11de564ce6e53b95f02954a297f0008183ac63a4f5974f6b32d8"),
    ("Fox", "d97044e701822bac5a62696459b27d7b375aada5de8574ed4362edbba94771f7"),
)


def prepare_models(output: Path) -> Path:
    """Reference fixtures only; they are never placed in product resources."""
    models = output / "models"
    models.mkdir(exist_ok=True)
    evidence = []
    for name, digest in MODELS:
        base = f"https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/{KHT_COMMIT}/Models/{name}"
        url = f"{base}/glTF-Binary/{name}.glb"
        destination = models / f"{name}.glb"
        if not destination.exists():
            with urllib.request.urlopen(url, timeout=60) as response:
                destination.write_bytes(response.read())
        actual = hashlib.sha256(destination.read_bytes()).hexdigest()
        if actual != digest:
            raise RuntimeError(f"Official 3D export fixture hash mismatch: {destination}: {actual}")
        license_file = models / f"LICENSE_{name}.md"
        if not license_file.exists():
            with urllib.request.urlopen(f"{base}/LICENSE.md", timeout=60) as response:
                license_file.write_bytes(response.read())
        evidence.append({"name": name, "url": url, "sha256": actual, "bytes": destination.stat().st_size,
                         "license_url": f"{base}/LICENSE.md"})
    (output / "model-sources.json").write_text(json.dumps(evidence, indent=2)+"\n", encoding="utf-8")
    return models


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "build/reference/astra-ios-offline")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "engine/platform/ios/build/native-export-probe")
    parser.add_argument("--arch", choices=("arm64", "x86_64"), default=platform.machine())
    parser.add_argument("--ios-min", default="16.3")
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--device", help="Available iOS simulator UDID; otherwise select and boot")
    args = parser.parse_args()
    if platform.system() != "Darwin":
        raise RuntimeError("Actual macOS/Xcode and an iOS simulator are required")
    prefix = args.ffmpeg_root.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    owned_run = output / "run"
    owned_run.mkdir(exist_ok=True)
    build = args.build_dir.resolve()
    manifest = prefix / "aurea-ffmpeg-build.json"
    shutil.copy2(manifest, output / "native-dependencies.json")
    shutil.copytree(prefix / "licenses", output / "licenses", dirs_exist_ok=True)
    models = prepare_models(output)
    configure = ["cmake", "-S", str(ROOT / "engine/platform/ios"), "-B", str(build), "-G", "Ninja",
                 "-DCMAKE_TOOLCHAIN_FILE=" + str(ROOT / "engine/platform/ios/ios_toolchain.cmake"),
                 "-DCMAKE_BUILD_TYPE=Release", "-DAUREA_IOS_SIMULATOR=ON",
                 "-DCMAKE_OSX_ARCHITECTURES=" + args.arch, "-DCMAKE_OSX_DEPLOYMENT_TARGET=" + args.ios_min,
                 "-DAUREA_FFMPEG_ROOT=" + str(prefix), "-DAUREA_BUILD_NATIVE_EXPORT_PROBE=ON",
                 "-DCMAKE_C_COMPILER_LAUNCHER=ccache", "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache",
                 "-DCMAKE_OBJCXX_COMPILER_LAUNCHER=ccache"]
    run_logged(configure, output / "configure.log", timeout=600)
    run_logged(["cmake", "--build", str(build), "--target", "aurea_ios_composition_export_probe",
                "--parallel", str(args.jobs)], output / "build.log", timeout=1800)
    executable = build / "aurea_ios_composition_export_probe"
    run_logged(["codesign", "--force", "--sign", "-", str(executable)], output / "codesign.log")
    run_logged(["xcrun", "vtool", "-show-build", str(executable)], output / "mach-o-platform.log")
    identifier = simulator(args.device)
    command = ["xcrun", "simctl", "spawn", identifier, str(executable), str(owned_run), str(models)]
    environment = {"target": "iOS Simulator", "arch": args.arch, "device_udid": identifier,
                   "xcode": capture(["xcodebuild", "-version"]),
                   "sdk": capture(["xcrun", "--sdk", "iphonesimulator", "--show-sdk-path"]),
                   "devices": json.loads(capture(["xcrun", "simctl", "list", "devices", "available", "--json"])),
                   "configure": configure, "command": command}
    (output / "execution-environment.json").write_text(json.dumps(environment, indent=2)+"\n", encoding="utf-8")
    run_logged(command, output / "native-tests.log", timeout=900)
    print((output / "native-tests.log").read_text(encoding="utf-8", errors="replace"), flush=True)
    print(f"Actual iOS offscreen composition/native export evidence: {output}", flush=True)


if __name__ == "__main__":
    main()
