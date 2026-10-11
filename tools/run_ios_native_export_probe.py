#!/usr/bin/env python3
"""Compile and execute the real shared FFmpeg export tests on an iOS simulator.

This harness requires macOS/Xcode and the native simulator dependency prefix.
It uses the shipping C++ sink and CPU decoders without depending on Swift UI.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import platform
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def capture(args: list[str]) -> str:
    return subprocess.check_output(args, text=True).strip()


def run_logged(args: list[str], log: Path, timeout: int = 300) -> None:
    print("+ " + shlex.join(args), flush=True)
    with log.open("w", encoding="utf-8") as output:
        result = subprocess.run(args, stdout=output, stderr=subprocess.STDOUT, timeout=timeout)
    if result.returncode:
        print(log.read_text(encoding="utf-8", errors="replace"), flush=True)
        raise RuntimeError(f"Native export command failed ({result.returncode}); evidence: {log}")


def simulator(requested: str | None) -> str:
    available = json.loads(capture(["xcrun", "simctl", "list", "devices", "available", "--json"]))
    devices = [(runtime, device) for runtime, group in available["devices"].items()
               if ".iOS-" in runtime for device in group if device.get("isAvailable")]
    if requested:
        selected = next((device for _, device in devices if device["udid"] == requested), None)
        if selected is None:
            raise RuntimeError(f"Requested available iOS simulator not found: {requested}")
    else:
        if not devices:
            raise RuntimeError("Xcode has no available iOS simulator runtime")
        _, selected = max(devices, key=lambda item: (item[1]["state"] == "Booted", item[0], item[1]["name"]))
    identifier = selected["udid"]
    if selected["state"] != "Booted":
        subprocess.run(["xcrun", "simctl", "boot", identifier], check=True, timeout=60)
    subprocess.run(["xcrun", "simctl", "bootstatus", identifier, "-b"], check=True, timeout=180)
    return identifier


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "build/reference/astra-ios-native")
    parser.add_argument("--arch", choices=("arm64", "x86_64"), default=platform.machine())
    parser.add_argument("--ios-min", default="16.0")
    parser.add_argument("--device", help="Available iOS simulator UDID; otherwise choose and boot automatically")
    args = parser.parse_args()
    if platform.system() != "Darwin":
        raise RuntimeError("A real macOS host with Xcode is required for native iOS simulator execution")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    run_directory = output / "run"
    run_directory.mkdir(exist_ok=True)
    prefix = args.ffmpeg_root.resolve()
    archives = [prefix / f"lib/lib{component}.a" for component in
                ("avformat", "avcodec", "swscale", "swresample", "avutil", "openh264", "kvazaar")]
    for archive in archives:
        if not archive.is_file() or not archive.stat().st_size:
            raise RuntimeError(f"Native iOS export dependency missing: {archive}")
    manifest = prefix / "aurea-ffmpeg-build.json"
    shutil.copy2(manifest, output / "native-dependencies.json")
    sdk = capture(["xcrun", "--sdk", "iphonesimulator", "--show-sdk-path"])
    compiler = capture(["xcrun", "--sdk", "iphonesimulator", "--find", "clang++"])
    source = output / "main.cpp"
    source.write_text(r'''#include "TestFramework.hpp"
#include "NativeExportVerifier.hpp"
#include <cerrno>
#include <cstring>
#include <unistd.h>
extern "C" {
#include <libavutil/log.h>
}
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2 || chdir(argv[1]) != 0) {
        std::fprintf(stderr, "Owned native export directory unavailable: %s\n", std::strerror(errno));
        return 2;
    }
    av_log_set_level(AV_LOG_ERROR);
    int result = aurea::test::run_all(argc > 2 ? argv[2] : nullptr);
    if (!result && argc == 2) {
        if (!aurea::test::verify_native_export("build/reference/astra-native/cfr-stereo.mp4", 90, 192, 96, 30, true).ok) return 1;
        if (!aurea::test::verify_native_export("build/reference/astra-native/fractional-2997.mp4", 300, 192, 96, 30000.0 / 1001, true).ok) return 1;
    }
    return result;
}
''', encoding="utf-8")
    executable = output / "astra_ios_native_export_probe"
    sources = [source, ROOT / "engine/src/export/FFmpegExportSink.cpp",
               ROOT / "engine/src/export/OfflineFrameQueue.cpp", ROOT / "engine/src/core/Time.cpp",
               ROOT / "engine/src/core/Log.cpp", ROOT / "engine/src/project/FileIO.cpp",
               ROOT / "engine/tests/test_ffmpeg_export.cpp", ROOT / "engine/tests/test_offline_frame_queue.cpp"]
    compile_command = [compiler, "-target", f"{args.arch}-apple-ios{args.ios_min}-simulator", "-isysroot", sdk,
                       "-std=c++23", "-O2", "-gline-tables-only", "-pthread", "-DAUREA_HAS_FFMPEG=1",
                       "-DAUREA_PLATFORM_IOS=1", "-I" + str(ROOT / "engine/include"),
                       "-I" + str(ROOT / "engine/tests"), "-I" + str(prefix / "include"),
                       *map(str, sources), *map(str, archives), "-lz", "-lbz2", "-liconv", "-lm",
                       "-framework", "VideoToolbox", "-framework", "CoreMedia", "-framework", "CoreVideo",
                       "-framework", "AudioToolbox", "-framework", "Security", "-framework", "Foundation",
                       "-o", str(executable)]
    (output / "compile-command.json").write_text(json.dumps(compile_command, indent=2)+"\n", encoding="utf-8")
    run_logged(compile_command, output / "compile.log")
    run_logged(["codesign", "--force", "--sign", "-", str(executable)], output / "codesign.log")
    run_logged(["xcrun", "vtool", "-show-build", str(executable)], output / "mach-o-platform.log")
    identifier = simulator(args.device)
    evidence = {"host": platform.system(), "host_arch": platform.machine(), "target_arch": args.arch,
                "sdk": sdk, "device_udid": identifier,
                "devices": json.loads(capture(["xcrun", "simctl", "list", "devices", "available", "--json"])),
                "xcode": capture(["xcodebuild", "-version"]), "prefix": str(prefix),
                "command": ["xcrun", "simctl", "spawn", identifier, str(executable), str(run_directory)]}
    (output / "execution-environment.json").write_text(json.dumps(evidence, indent=2)+"\n", encoding="utf-8")
    run_logged(evidence["command"], output / "native-tests.log", timeout=600)
    print((output / "native-tests.log").read_text(encoding="utf-8", errors="replace"), flush=True)
    print(f"Native iOS simulator export tests completed; movies and raw evidence: {output}", flush=True)


if __name__ == "__main__":
    main()
