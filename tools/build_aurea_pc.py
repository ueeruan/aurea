"""Build and package the native AUREA PC beta in one reusable Release directory.

No dependency, project, or existing delivery directory is deleted by this script.
Requires Windows x64, Visual Studio C++ Build Tools, CMake, Ninja, an Android NDK
shader compiler, and a native FFmpeg development prefix (see docs/pc-beta001.md).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "engine/build/pc-beta001"
VALIDATION = ROOT / "build/validation/pc-beta001"
DIST = ROOT / "output/aurea-pc-beta-0.0.1"


def development_environment() -> dict[str, str]:
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if not vswhere.is_file():
        raise RuntimeError("Visual Studio Installer/vswhere.exe is required")
    installation = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], text=True).strip()
    vcvars = Path(installation) / "VC/Auxiliary/Build/vcvars64.bat"
    if not vcvars.is_file():
        raise RuntimeError("Visual Studio x64 C++ Build Tools were not found")
    # cmd.exe interprets its command tail directly; list2cmdline would turn
    # the embedded quotes into backslash-quotes, invalid for a batch path.
    output = subprocess.check_output(f'cmd.exe /d /s /c ""{vcvars}" >nul && set"', text=True)
    environment = os.environ.copy()
    for line in output.splitlines():
        name, separator, value = line.partition("=")
        if separator and name:
            environment[name.upper()] = value
    environment["VSLANG"] = "1033"
    return environment


def run(command: list[str], environment: dict[str, str], log_name: str) -> None:
    VALIDATION.mkdir(parents=True, exist_ok=True)
    with (VALIDATION / log_name).open("w", encoding="utf-8") as log:
        log.write(subprocess.list2cmdline(command) + "\n")
        with subprocess.Popen(command, cwd=ROOT, env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace") as process:
            assert process.stdout is not None
            for line in process.stdout:
                log.write(line)
                log.flush()
                # Keep full diagnostics in the log; localized MSVC include
                # traces can otherwise overwhelm an interactive build console.
                if "incluindo arquivo:" not in line and "including file:" not in line:
                    print(line, end="", flush=True)
            result = process.wait()
        if result:
            raise RuntimeError(f"Command exited {result}; see {VALIDATION / log_name}")


def include_prefix(environment: dict[str, str]) -> str:
    VALIDATION.mkdir(parents=True, exist_ok=True)
    source = VALIDATION / "msvc-include-probe.c"
    source.write_text("#include <stdio.h>\n", encoding="utf-8")
    compiler = shutil.which("cl.exe", path=environment["PATH"])
    if not compiler:
        raise RuntimeError("The Visual C++ development environment has no cl.exe")
    result = subprocess.run([compiler, "/nologo", "/showIncludes", "/EP", str(source)], cwd=VALIDATION, env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)
    (VALIDATION / "msvc-showincludes-probe.log").write_bytes(result.stdout)
    for line in result.stdout.splitlines():
        match = re.match(rb"^(.*?: )\s*[A-Za-z]:[\\/]", line)
        if match:
            try:
                return match.group(1).decode("utf-8")
            except UnicodeDecodeError:
                return match.group(1).decode("mbcs")
    raise RuntimeError("Cannot detect the localized MSVC /showIncludes prefix")


def package(prefix: Path, environment: dict[str, str]) -> None:
    executable = BUILD / "platform/windows/AUREA-PC.exe"
    if not executable.is_file():
        raise RuntimeError("Build AUREA-PC.exe before packaging")
    DIST.mkdir(parents=True, exist_ok=True)
    shutil.copy2(executable, DIST / executable.name)
    # The five linked FFmpeg libraries form a closed dependency set. Encoding
    # libraries in this development distribution are linked into avcodec.dll.
    for component in ("avcodec", "avformat", "avutil", "swresample", "swscale"):
        libraries = list((prefix / "bin").glob(component + "-*.dll"))
        if len(libraries) != 1:
            raise RuntimeError(f"Expected one {component} runtime DLL in {prefix / 'bin'}")
        shutil.copy2(libraries[0], DIST / libraries[0].name)
    redist = Path(environment["VCTOOLSREDISTDIR"]) / "x64"
    crt_directories = list(redist.glob("Microsoft.VC*.CRT"))
    if len(crt_directories) != 1:
        raise RuntimeError("Cannot identify the Visual C++ x64 application-local runtime")
    for library in crt_directories[0].glob("*.dll"):
        shutil.copy2(library, DIST / library.name)
    license_directory = DIST / "licenses"
    license_directory.mkdir(exist_ok=True)
    shutil.copy2(prefix / "LICENSE", license_directory / "FFmpeg-distribution-LICENSE.txt")
    for name, source in (("ncnn", BUILD / "_deps/aurea_ncnn-src/LICENSE.txt"), ("glslang", BUILD / "_deps/aurea_ncnn-src/glslang/LICENSE.txt"), ("whisper", BUILD / "_deps/aurea_whisper-src/LICENSE"), ("ggml", BUILD / "_deps/aurea_whisper-src/ggml/LICENSE")):
        if source.is_file():
            shutil.copy2(source, license_directory / (name + "-LICENSE.txt"))
    for name in ("ai", "rotobrush"):
        directory = ROOT / "engine/assets" / name
        for source in directory.glob("*LICENSE*"):
            if source.is_file():
                shutil.copy2(source, license_directory / (name + "-" + source.name))
        readme = directory / "README.md"
        if readme.is_file():
            shutil.copy2(readme, license_directory / (name + "-provenance.md"))
    for source in (ROOT / "engine/third_party").rglob("*LICENSE*"):
        if source.is_file():
            relative = source.relative_to(ROOT / "engine/third_party")
            target = license_directory / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    shutil.copy2(ROOT / "docs/pc-beta001.md", DIST / "LEIA-ME.md")
    records = []
    for file in sorted(DIST.rglob("*")):
        if file.is_file() and file.name != "SHA256.json":
            records.append({"path": file.relative_to(DIST).as_posix(), "bytes": file.stat().st_size, "sha256": hashlib.sha256(file.read_bytes()).hexdigest()})
    (DIST / "SHA256.json").write_text(json.dumps({"version": "0.0.1", "architecture": "windows-x64", "files": records}, indent=2), encoding="utf-8")
    archive = DIST.parent / (DIST.name + ".zip")
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
        for file in sorted(DIST.rglob("*")):
            if file.is_file():
                output.write(file, file.relative_to(DIST.parent))
    print(f"Portable beta: {archive} ({archive.stat().st_size:,} bytes)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg-prefix", type=Path, default=ROOT / "build/astra-ffmpeg/host-dev/ffmpeg-8.1.2-full_build-shared")
    parser.add_argument("--configure-only", action="store_true")
    parser.add_argument("--core-only", action="store_true")
    parser.add_argument("--skip-configure", action="store_true")
    parser.add_argument("--test", action="store_true")
    parser.add_argument("--package", action="store_true")
    parser.add_argument("--package-only", action="store_true")
    parser.add_argument("--package-test", action="store_true", help="Run the packaged executable headlessly with no development DLL directories in PATH")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("AUREA PC beta 0.0.1 currently targets Windows x64")
    prefix = args.ffmpeg_prefix.resolve()
    environment = development_environment()
    environment["PATH"] = str(prefix / "bin") + os.pathsep + environment["PATH"]
    if not args.skip_configure and not args.package_only:
        run(["cmake", "-S", str(ROOT / "engine"), "-B", str(BUILD), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_C_FLAGS=/utf-8 /diagnostics:column", "-DCMAKE_CXX_FLAGS=/utf-8 /diagnostics:column", "-DAUREA_BUILD_DESKTOP=ON", "-DAUREA_BUILD_TESTS=OFF", f"-DAUREA_MSVC_INCLUDE_PREFIX={include_prefix(environment)}", f"-DCMAKE_PROJECT_INCLUDE={(ROOT / 'engine/platform/windows/WindowsBuildEnvironment.cmake').as_posix()}", f"-DAUREA_FFMPEG_ROOT={prefix.as_posix()}"], environment, "configure.log")
    if args.configure_only:
        return 0
    if not args.package_only:
        targets = ["aurea_core", "aurea_vulkan", "aurea_windows_media"] if args.core_only else ["aurea_desktop"]
        run(["cmake", "--build", str(BUILD), "--parallel", str(max(1, args.jobs)), "--target", *targets], environment, "core-build.log" if args.core_only else "desktop-build.log")
    if args.test:
        run([str(BUILD / "platform/windows/AUREA-PC.exe"), "--self-test", str(VALIDATION / "self-test")], environment, "self-test-process.log")
        # wWinMain self-test communicates through its file because the executable
        # uses the Windows subsystem rather than attaching to a terminal.
        print((VALIDATION / "self-test/desktop-tests.txt").read_text(encoding="utf-8"))
    if args.package or args.package_only:
        package(prefix, environment)
    if args.package_test:
        portable_environment = os.environ.copy()
        windows_directory = Path(os.environ["SystemRoot"])
        portable_environment["PATH"] = os.pathsep.join(str(path) for path in (windows_directory / "System32", windows_directory, DIST))
        run([str(DIST / "AUREA-PC.exe"), "--self-test", str(VALIDATION / "package-self-test")], portable_environment, "package-self-test-process.log")
        print((VALIDATION / "package-self-test/desktop-tests.txt").read_text(encoding="utf-8"))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"AUREA PC build failed: {error}", file=sys.stderr)
        raise SystemExit(1)
