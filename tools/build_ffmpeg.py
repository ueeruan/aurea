#!/usr/bin/env python3
"""Build the pinned export-only FFmpeg/OpenH264/Kvazaar libraries for mobile or host.

Android Windows uses Git Bash plus the NDK's GNU make and clang; Linux/macOS use
their POSIX shell. iOS always requires a real macOS host with the selected SDK.
Archives are SHA-256 pinned; versioned dependency safety patches are recorded
with before/after hashes in each installed build manifest.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
FFMPEG_VERSION = "9.0.2"
OPENH264_VERSION = "2.6.0"
KVAZAAR_VERSION = "2.3.2"
X265_VERSION = "4.1"
X265_SOURCE = ("x265_4.1.tar.gz", "https://github.com/Multicorewareinc/x265/releases/download/4.1/x265_4.1.tar.gz",
               "a31699c6a89806b74b0151e5e6a7df65de4b49050482fe5ebf8a4379d7af8f29")
SOURCES = (
    (f"ffmpeg-{FFMPEG_VERSION}.tar.xz", f"https://ffmpeg.org/releases/ffmpeg-{FFMPEG_VERSION}.tar.xz",
     "8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e"),
    (f"openh264-{OPENH264_VERSION}.tar.gz", f"https://github.com/cisco/openh264/archive/refs/tags/v{OPENH264_VERSION}.tar.gz",
     "558544ad358283a7ab2930d69a9ceddf913f4a51ee9bf1bfb9e377322af81a69"),
    (f"kvazaar-{KVAZAAR_VERSION}.tar.gz", f"https://github.com/ultravideo/kvazaar/releases/download/v{KVAZAAR_VERSION}/kvazaar-{KVAZAAR_VERSION}.tar.gz",
     "b95d2e20f2b0d8d7ed320055740be2e7a730abe28b153b5a788cfca371cc38b2"),
)

def run(args: list[str], cwd: Path | None = None, env: dict | None = None) -> None:
    print("+ " + shlex.join(args), flush=True)
    subprocess.run(args, cwd=cwd, env=env, check=True)

def checked_sources(cache: Path, with_x265: bool = False) -> tuple[Path, Path]:
    download = cache / "downloads"
    source = cache / "source"
    download.mkdir(parents=True, exist_ok=True)
    source.mkdir(parents=True, exist_ok=True)
    for name, url, digest in (*SOURCES, *((X265_SOURCE,) if with_x265 else ())):
        archive = download / name
        if not archive.exists():
            print(f"Downloading {name}", flush=True)
            with urllib.request.urlopen(url) as response, archive.open("wb") as out:
                shutil.copyfileobj(response, out)
        actual = hashlib.sha256(archive.read_bytes()).hexdigest()
        if actual != digest:
            raise RuntimeError(f"Source checksum mismatch for {archive}: {actual}")
        directory = source / name.split(".tar.")[0]
        if not directory.exists():
            with tarfile.open(archive) as package:
                package.extractall(source, filter="data")
    return source / f"ffmpeg-{FFMPEG_VERSION}", source / f"openh264-{OPENH264_VERSION}"

def windows_native_compiler_environment(env: dict[str, str]) -> dict[str, str]:
    """FFmpeg's configure validates a real host C compiler for table tools."""
    roots = [Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio",
             Path("C:/Program Files (x86)/Microsoft Visual Studio"), Path("D:/Program Files (x86)/Microsoft Visual Studio")]
    install = ""
    for root in roots:
        locator = root / "Installer/vswhere.exe"
        if locator.exists():
            install = subprocess.check_output([str(locator), "-latest", "-products", "*", "-requires",
                                              "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                                             text=True).strip()
        if install:
            break
    devcmd = Path(install) / "Common7/Tools/VsDevCmd.bat"
    if not install:
        # vswhere does not always list a Build Tools instance installed on
        # another drive; its VsDevCmd.bat is in the usual <version>/<edition> layout.
        found = sorted(p for root in roots for p in root.glob("*/*/Common7/Tools/VsDevCmd.bat")
                       if (p.parents[2] / "VC/Tools/MSVC").is_dir())
        if found:
            devcmd, install = found[-1], str(found[-1].parents[2])
    if not install or not devcmd.exists():
        raise RuntimeError("Visual Studio C++ Build Tools not found for FFmpeg host checks")
    # cmd uses its own quote rules; list2cmdline backslash-quotes are invalid
    # around a batch file. The installation path comes from vswhere itself.
    output = subprocess.check_output(f'cmd.exe /d /c call "{devcmd}" -no_logo -arch=x64 >nul && set', text=True, env=env)
    for line in output.splitlines():
        key, sep, value = line.partition("=")
        if sep and key.upper() in ("INCLUDE", "LIB", "LIBPATH", "PATH", "VCTOOLSINSTALLDIR", "WINDOWSSDKDIR", "WINDOWSSDKVERSION"):
            env[key.upper() if key.upper() != "PATH" else "PATH"] = value
    # VsDevCmd only finds a Windows SDK registered on the system drive; with the
    # SDK on another drive the host compiler had no UCRT headers (ctype.h).
    if "\\ucrt" not in env.get("INCLUDE", "").lower():
        kits = sorted(p for root in roots for p in (root.parent / "Windows Kits/10/Include").glob("10.*") if (p / "ucrt").is_dir())
        if kits:
            include, version = kits[-1], kits[-1].name
            lib = include.parents[1] / "Lib" / version
            env["INCLUDE"] = os.pathsep.join([env.get("INCLUDE", "")] + [str(include / d) for d in ("ucrt", "um", "shared")])
            env["LIB"] = os.pathsep.join([env.get("LIB", "")] + [str(lib / d / "x64") for d in ("ucrt", "um")])
    return env

def public_header_list(source: Path) -> list[tuple[str, bool]]:
    """Read the complete public header set from the pinned upstream makefiles."""
    headers: list[tuple[str, bool]] = []
    for component in ("avcodec", "avformat", "avutil", "swscale", "swresample"):
        directory = "lib" + component
        declarations = (source / directory / "Makefile").read_text(encoding="utf-8").replace("\\\n", "")
        for variable in ("HEADERS", "BUILT_HEADERS"):
            match = re.search(r"^" + variable + r"\s*=\s*([^\n]*)", declarations, re.MULTILINE)
            if not match:
                if variable == "HEADERS":
                    raise RuntimeError(f"Upstream public header list missing for {component}")
                continue
            for name in match.group(1).split():
                if not re.fullmatch(r"[A-Za-z0-9_]+\.h", name):
                    raise RuntimeError(f"Unexpected upstream public header expression: {name}")
                headers.append((f"{directory}/{name}", variable == "BUILT_HEADERS"))
    return headers

def installed_headers_match(source: Path, prefix: Path, hashes: dict[str, str]) -> bool:
    """Validate a restored native prefix even when its object tree is absent."""
    headers = public_header_list(source)
    if set(hashes) != {name for name, _ in headers}:
        return False
    for name, generated in headers:
        installed = prefix / "include" / name
        if not installed.is_file() or hashlib.sha256(installed.read_bytes()).hexdigest() != hashes[name]:
            return False
        # Non-generated bytes must also agree with the verified release source.
        if not generated and hashlib.sha256((source / name).read_bytes()).hexdigest() != hashes[name]:
            return False
    return True

def install_public_headers(source: Path, configured: Path, prefix: Path) -> dict[str, str]:
    """Install upstream public headers individually, including generated ABI data.

    Windows make/install can exceed cmd.exe's command length for libavutil's
    public header list. Use the checksum-pinned upstream HEADERS definitions,
    keeping each file operation independent and verifying the installed bytes.
    """
    installed: dict[str, str] = {}
    for name, generated in public_header_list(source):
        original = (configured if generated else source) / name
        destination = prefix / "include" / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_name(destination.name + f".{os.getpid()}.tmp")
        shutil.copy2(original, temporary)
        digest = hashlib.sha256(original.read_bytes()).hexdigest()
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != digest:
            raise RuntimeError(f"Installed public header mismatch: {destination}")
        os.replace(temporary, destination)
        installed[name] = digest
    return installed

def write_manifest(path: Path, manifest: dict) -> None:
    temporary = path.with_name(path.name + f".{os.getpid()}.tmp")
    temporary.write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8")
    os.replace(temporary, path)


def fix_ffmpeg_mediacodec_buffers(source: Path) -> dict[str, str]:
    """Apply exactly one reviewed LGPL patch to the checksum-pinned release.

    Reject another source version and any locally divergent file rather than
    silently adapting it. This narrow parser only accepts the one relative
    target above; patch paths cannot select any other workspace file.
    """
    target = source / "libavcodec/mediacodecenc.c"
    # 9.0.2: mediacodecenc.c igual ao 8.0.3 sem patch (o mesmo hash "before"):
    # nenhuma destas correções entrou no upstream.
    patch = ROOT / "tools/patches/ffmpeg-mediacodec-safety.patch"
    before = "d65e6f3b4e9d60633ad8f80ec917d77ecab354675497a61c39fbab7f78f222c1"
    after = "36b1887bb54b63b9981b43bbc9f2c35fa3348b2a14ed22c2bf05afaaa353e8d0"
    actual = hashlib.sha256(target.read_bytes()).hexdigest()
    if actual not in (before, after):
        raise RuntimeError(f"Unexpected FFmpeg {FFMPEG_VERSION} MediaCodec source hash: {actual}")
    if actual == before:
        original = target.read_text(encoding="utf-8").splitlines(keepends=True)
        lines = patch.read_text(encoding="utf-8").splitlines(keepends=True)
        if lines[:2] != ["--- a/libavcodec/mediacodecenc.c\n", "+++ b/libavcodec/mediacodecenc.c\n"]:
            raise RuntimeError("MediaCodec safety patch target is not the reviewed file")
        output: list[str] = []
        index = cursor = 0
        for line in lines[2:]:
            if line.startswith("@@ "):
                match = re.match(r"@@ -(\d+)(?:,\d+)? \+\d+(?:,\d+)? @@", line)
                if not match:
                    raise RuntimeError("Invalid MediaCodec safety patch hunk")
                index = int(match.group(1)) - 1
                if index < cursor:
                    raise RuntimeError("Overlapping MediaCodec safety patch hunks")
                output.extend(original[cursor:index])
                cursor = index
            elif line[:1] in (" ", "-"):
                if cursor >= len(original) or original[cursor] != line[1:]:
                    raise RuntimeError("MediaCodec safety patch context does not match")
                if line[0] == " ":
                    output.append(line[1:])
                cursor += 1
            elif line.startswith("+"):
                output.append(line[1:])
            else:
                raise RuntimeError("Invalid MediaCodec safety patch line")
        output.extend(original[cursor:])
        data = "".join(output).encode("utf-8")
        if hashlib.sha256(data).hexdigest() != after:
            raise RuntimeError("MediaCodec safety patch result does not match the reviewed hash")
        target.write_bytes(data)
    return {"patch": patch.name, "patch_sha256": hashlib.sha256(patch.read_bytes()).hexdigest(),
            "before_sha256": before, "after_sha256": after}

def write_dependency_notice(directory: Path, args: argparse.Namespace, arch: str, patch: str,
                            ffmpeg_patch: dict[str, str]) -> None:
    """Ship the dependency notices together with reproducible source/build data."""
    dependencies = [
        f"FFmpeg {FFMPEG_VERSION}: {'GPL-2.0-or-later' if args.with_x265 else 'LGPL-2.1-or-later'}",
        f"OpenH264 {OPENH264_VERSION}: BSD-2-Clause",
        f"Kvazaar {KVAZAAR_VERSION}: BSD-3-Clause",
    ]
    if args.with_x265:
        dependencies.append(f"x265 {X265_VERSION}: GPL-2.0-or-later (explicit optional build)")
    recipe = f"python3 tools/build_ffmpeg.py --platform {args.platform} --arch {arch}"
    if args.platform == "android":
        recipe += f" --ndk <Android-NDK> --api {args.api}"
    elif args.platform.startswith("ios"):
        recipe += f" --ios-min {args.ios_min}"
    if args.with_x265:
        recipe += " --with-x265"
    sources = "\n\n".join(f"{url}\nSHA-256: {digest}" for _, url, digest in
                           (*SOURCES, *((X265_SOURCE,) if args.with_x265 else ())))
    text = ("AUREA native export dependencies\n\n" + "\n".join(dependencies) +
        "\n\nFull license texts are included beside this notice.\n\n" +
        "Corresponding checksum-pinned upstream source archives:\n" + sources +
        "\n\nNative build recipe from the AUREA source repository:\n" + recipe +
        "\nBuild integration: tools/build_ffmpeg.py and engine/cmake/AureaFFmpeg.cmake.\n" +
        "Libraries are built statically with position-independent code. FFmpeg uses\n" +
        "libavcodec, libavformat, libswscale and libswresample; OpenH264 supplies AVC\n" +
        "software encoding and Kvazaar supplies HEVC software encoding. Native hardware\n" +
        "encoding uses Android MediaCodec or Apple VideoToolbox with software fallback.\n" +
        "Default source builds do not enable GPL FFmpeg components.\n\n" +
        "Kvazaar source adaptation: debug-file cleanup releases mutexes only for\n" +
        "actually opened RD training files, preserving successive export sessions.\n" +
        "The exact patch is in fix_kvazaar_debug_file_lifetime in tools/build_ffmpeg.py.\n" +
        f"Patched src/rdo.c SHA-256: {patch}\n" +
        "FFmpeg MediaCodec source adaptation (LGPL-2.1-or-later): validate input/output\n" +
        "buffer bounds, propagate queue errors, preserve/release final packets with EOS,\n" +
        "initialize coded padding, and restart the encoder after the header probe\n" +
        "by retiring the probe instance and creating a fresh one with the same\n" +
        "codec/format instead of undefined encoder flush.\n" +
        "The exact versioned patch is included here.\n" +
        f"MediaCodec patch SHA-256: {ffmpeg_patch['patch_sha256']}\n" +
        f"Patched mediacodecenc.c SHA-256: {ffmpeg_patch['after_sha256']}\n" +
        "OpenH264 and other FFmpeg codec sources are unmodified. Optional x265\n" +
        "builds adapt obsolete CMake policy settings; codec algorithms remain intact.\n")
    (directory / "NativeExport-DEPENDENCIES.txt").write_text(text, encoding="utf-8")

def fix_kvazaar_debug_file_lifetime(source: Path) -> str:
    """Fix upstream's destruction of unused/already destroyed debug mutexes.

    Kvazaar 2.3.2 unconditionally destroys static RD-training file mutexes when
    closing every encoder. Those mutexes are initialized only when training
    files are opened; Android Bionic aborts on the second ordinary export.
    Keep the encoder algorithms intact and release only actual debug resources.
    """
    file = source / "src/rdo.c"
    content = file.read_text(encoding="utf-8")
    old = """    if (curr != NULL) {
      fclose(curr);
    }
    if (curr_mtx != NULL) {
      pthread_mutex_destroy(curr_mtx);
    }
"""
    new = """    if (curr != NULL) {
      fclose(curr);
      fastrd_learning_outfile[i] = NULL;
      pthread_mutex_destroy(curr_mtx);
    }
"""
    if content.count(old) == 1:
        file.write_text(content.replace(old, new), encoding="utf-8")
    elif content.count(new) != 1:
        raise RuntimeError("Unexpected pinned Kvazaar RD debug cleanup implementation")
    return hashlib.sha256(file.read_bytes()).hexdigest()

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=("android", "ios", "ios-simulator", "host"), required=True)
    parser.add_argument("--arch", default="arm64-v8a")
    parser.add_argument("--ndk", type=Path)
    parser.add_argument("--api", type=int, default=26)
    parser.add_argument("--ios-min", default="16.0")
    parser.add_argument("--prefix", type=Path)
    parser.add_argument("--cache", type=Path, default=ROOT / "build/astra-ffmpeg")
    parser.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 2))
    parser.add_argument("--with-x265", action="store_true",
        help="Build software HEVC with GPL x265 for internal QA or a separately licensed product; default dependencies remain LGPL/BSD")
    parser.add_argument("--force", action="store_true", help="Reconfigure and rebuild even if the native manifest matches")
    args = parser.parse_args()
    ffmpeg, openh264 = checked_sources(args.cache.resolve(), args.with_x265)
    ffmpeg_patch = fix_ffmpeg_mediacodec_buffers(ffmpeg)
    native_arch = args.arch
    if args.platform.startswith("ios") and native_arch == "arm64-v8a":
        native_arch = "arm64"
    family = "ffmpeg-gpl" if args.with_x265 else "ffmpeg"
    prefix = (args.prefix or ROOT / "build" / family / args.platform / native_arch).resolve()
    if args.platform == "host" and not args.prefix:
        prefix = ROOT / "build" / family / "host"
    work = args.cache.resolve() / "objects" / args.platform / native_arch
    if args.with_x265:
        work = work / "gpl"
    # Keep the newly enabled BSD HEVC build separate from archives currently
    # linked or being exercised by an Android/iOS app build.
    work = work / "bsd-hevc"
    work.mkdir(parents=True, exist_ok=True)
    prefix.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    windows = os.name == "nt"
    bash = "bash"
    make = "make"
    if windows:
        if args.platform != "android":
            raise RuntimeError("Native host/iOS source builds require POSIX; use a native dev prefix on Windows.")
        bash = str(Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Git/bin/bash.exe")
        env = windows_native_compiler_environment(env)
        env["PATH"] = str(Path(bash).parents[1] / "usr/bin") + os.pathsep + env["PATH"]
    q = lambda p: str(p).replace("\\", "/")
    cc = "clang"
    cxx = "clang++"
    ar = "ar"
    nm = "nm"
    ranlib = "ranlib"
    # 9.0.2: sem --disable-unstable. Ele parece economizar o backend novo do
    # swscale, mas o 9.0.2 não linka assim: format.c só define
    # ff_sws_chroma_pos dentro de #if CONFIG_UNSTABLE e graph.c chama sempre
    # ("undefined symbol: ff_sws_chroma_pos" no libaurea.so). Fica a configuração
    # padrão, a que o FFmpeg testa.
    common = ["--disable-programs", "--disable-doc", "--disable-network", "--disable-autodetect",
              "--disable-avdevice", "--disable-avfilter", "--disable-everything",
              "--enable-avcodec", "--enable-avformat", "--enable-swscale", "--enable-swresample",
              "--enable-static", "--disable-shared", "--enable-pic", "--enable-pthreads",
              "--enable-libopenh264", "--enable-libkvazaar", "--enable-encoder=libopenh264,libkvazaar,aac,mpeg4,mjpeg,png,ffv1",
              "--enable-decoder=h264,hevc,mpeg4,aac,mjpeg,png,ffv1",
              "--enable-parser=h264,hevc,mpeg4video,aac", "--enable-muxer=mp4,mov,matroska,image2",
              "--enable-demuxer=mov,matroska,image2", "--enable-protocol=file",
              "--enable-bsf=aac_adtstoasc,extract_extradata,h264_metadata,hevc_metadata"]
    oh = []
    flags = ""
    if args.platform == "android":
        if not args.ndk:
            raise RuntimeError("--ndk is required for Android")
        host = "windows-x86_64" if windows else "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
        ndk = args.ndk.resolve()
        tool = ndk / f"toolchains/llvm/prebuilt/{host}/bin"
        suffix = ".exe" if windows else ""
        cc, cxx, ar, nm, ranlib = (q(tool / (n + suffix)) for n in ("clang", "clang++", "llvm-ar", "llvm-nm", "llvm-ranlib"))
        arch, triple, oh_arch = {"arm64-v8a": ("aarch64", "aarch64-linux-android", "arm64"),
            "armeabi-v7a": ("arm", "armv7a-linux-androideabi", "arm"),
            "x86_64": ("x86_64", "x86_64-linux-android", "x86_64")}[native_arch]
        flags = f"--target={triple}{args.api} -fPIC -O2"
        common += ["--target-os=android", f"--arch={arch}", "--enable-cross-compile",
                   "--enable-jni", "--enable-mediacodec", "--enable-encoder=h264_mediacodec,hevc_mediacodec",
                   f"--extra-cflags={flags}", f"--extra-ldflags=--target={triple}{args.api}"]
        if arch == "x86_64":
            common += ["--disable-x86asm"]
        # Bypass upstream Android wrappers (.cmd cannot be run by POSIX sh).
        oh = ["OS=android", f"ARCH={oh_arch}", f"NDKROOT={q(ndk)}", f"TARGET=android-{args.api}",
              f"CC={cc}", f"CXX={cxx}", f"AR={ar}",
              f"CFLAGS={flags} -DANDROID_NDK -MMD -MP -fstack-protector-all"]
        if windows:
            make = q(ndk / "prebuilt/windows-x86_64/bin/make.exe")
            common += [f"--host-cc={cc}", "--host-cflags=--target=x86_64-pc-windows-msvc",
                       "--disable-hardcoded-tables"]
    elif args.platform.startswith("ios"):
        if platform.system() != "Darwin":
            raise RuntimeError("iOS compilation requires macOS and Xcode")
        sdk = "iphonesimulator" if args.platform.endswith("simulator") else "iphoneos"
        sysroot = subprocess.check_output(["xcrun", "--sdk", sdk, "--show-sdk-path"], text=True).strip()
        cc = subprocess.check_output(["xcrun", "--sdk", sdk, "--find", "clang"], text=True).strip()
        cxx = subprocess.check_output(["xcrun", "--sdk", sdk, "--find", "clang++"], text=True).strip()
        minimum = "-mios-simulator-version-min" if sdk == "iphonesimulator" else "-miphoneos-version-min"
        flags = f"-arch {native_arch} -isysroot {shlex.quote(sysroot)} {minimum}={args.ios_min} -fPIC -O2"
        common += ["--target-os=darwin", f"--arch={native_arch}", "--enable-cross-compile",
                   "--enable-videotoolbox", "--enable-encoder=h264_videotoolbox,hevc_videotoolbox",
                   f"--sysroot={sysroot}", f"--extra-cflags={flags}", f"--extra-ldflags={flags}"]
        # Darwin target with explicit SDK avoids deprecated iOS bitcode flags.
        oh = ["OS=darwin", f"ARCH={native_arch}", f"CC={cc}", f"CXX={cxx}", f"CFLAGS={flags}"]
    else:
        oh = ["OS=darwin" if platform.system() == "Darwin" else "OS=linux", f"ARCH={platform.machine()}"]
        common += ["--disable-x86asm"]
    kvazaar_original = args.cache.resolve() / f"source/kvazaar-{KVAZAAR_VERSION}"
    # Upstream CMake generates version/pkg-config files inside its source tree.
    # Each ABI therefore receives a private copy of the verified release source.
    kvazaar_source = work / f"kvazaar-source-{KVAZAAR_VERSION}"
    if not kvazaar_source.exists():
        shutil.copytree(kvazaar_original, kvazaar_source)
    kvazaar_patch = fix_kvazaar_debug_file_lifetime(kvazaar_source)
    kvazaar_work = work / "kvazaar"
    kvazaar_args = ["cmake", "-S", q(kvazaar_source), "-B", q(kvazaar_work), "-G", "Ninja",
                    f"-DCMAKE_INSTALL_PREFIX={q(prefix)}", "-DCMAKE_INSTALL_LIBDIR=lib", "-DCMAKE_BUILD_TYPE=Release",
                    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON", "-DBUILD_SHARED_LIBS=OFF",
                    "-DBUILD_TESTS=OFF", "-DBUILD_KVAZAAR_BINARY=OFF", "-DUSE_CRYPTO=OFF"]
    if args.platform == "android":
        kvazaar_args += [f"-DCMAKE_TOOLCHAIN_FILE={q(ndk / 'build/cmake/android.toolchain.cmake')}",
                        f"-DANDROID_ABI={native_arch}", f"-DANDROID_PLATFORM=android-{args.api}", "-DANDROID_STL=c++_shared"]
    elif args.platform.startswith("ios"):
        kvazaar_args += ["-DCMAKE_SYSTEM_NAME=iOS", f"-DCMAKE_OSX_SYSROOT={sysroot}",
                        f"-DCMAKE_SYSTEM_PROCESSOR={native_arch}",
                        f"-DCMAKE_OSX_ARCHITECTURES={native_arch}", f"-DCMAKE_OSX_DEPLOYMENT_TARGET={args.ios_min}"]
    run(kvazaar_args, env=env)
    run(["cmake", "--build", q(kvazaar_work), "--parallel", str(args.jobs)], env=env)
    run(["cmake", "--install", q(kvazaar_work)], env=env)
    if args.with_x265:
        common += ["--enable-gpl", "--enable-libx265", "--enable-encoder=libx265"]
        x265_source = args.cache.resolve() / f"source/x265_{X265_VERSION}/source"
        # Source archive has obsolete CMake policy modes. These are build-system
        # adaptations only; preserve the checksum-pinned codec source itself.
        cmakelists = x265_source / "CMakeLists.txt"
        content = cmakelists.read_text(encoding="utf-8")
        content = content.replace("cmake_minimum_required (VERSION 2.8.8)", "cmake_minimum_required (VERSION 3.5)")
        for policy in ("CMP0025", "CMP0054"):
            content = content.replace(f"cmake_policy(SET {policy} OLD)", f"cmake_policy(SET {policy} NEW)")
        cmakelists.write_text(content, encoding="utf-8")
        x265_work = work / "x265"
        x265_args = ["cmake", "-S", q(x265_source), "-B", q(x265_work), "-G", "Ninja",
                     f"-DCMAKE_INSTALL_PREFIX={q(prefix)}", "-DCMAKE_BUILD_TYPE=Release",
                     "-DENABLE_SHARED=OFF", "-DENABLE_CLI=OFF", "-DENABLE_ASSEMBLY=OFF",
                     "-DENABLE_NEON=OFF", "-DENABLE_NEON_DOTPROD=OFF", "-DENABLE_NEON_I8MM=OFF",
                     "-DENABLE_SVE=OFF", "-DENABLE_SVE2=OFF", "-DENABLE_PIC=ON",
                     "-DENABLE_LIBNUMA=OFF", "-DENABLE_LIBVMAF=OFF", "-DENABLE_HDR10_PLUS=OFF",
                     "-DHIGH_BIT_DEPTH=OFF", "-DGIT_ARCHETYPE=1", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"]
        if args.platform == "android":
            x265_args += [f"-DCMAKE_TOOLCHAIN_FILE={q(ndk / 'build/cmake/android.toolchain.cmake')}",
                          f"-DANDROID_ABI={native_arch}", f"-DANDROID_PLATFORM=android-{args.api}", "-DANDROID_STL=c++_shared"]
        elif args.platform.startswith("ios"):
            x265_args += ["-DCMAKE_SYSTEM_NAME=iOS", f"-DCMAKE_OSX_SYSROOT={sysroot}",
                          f"-DCMAKE_SYSTEM_PROCESSOR={native_arch}",
                          f"-DCMAKE_OSX_ARCHITECTURES={native_arch}", f"-DCMAKE_OSX_DEPLOYMENT_TARGET={args.ios_min}"]
        run(x265_args, env=env)
        run(["cmake", "--build", q(x265_work), "--parallel", str(args.jobs)], env=env)
        run(["cmake", "--install", q(x265_work)], env=env)
    # Only these exact pinned codec packages are visible to this cross build.
    pkg = work / "aurea-pkg-config.sh"
    cpp_runtime = "-lc++_shared" if args.platform == "android" else "-lc++" if platform.system() == "Darwin" else "-lstdc++"
    x265_pkg = ""
    if args.with_x265:
        x265_pkg = " *--modversion*x265*) echo 4.1;;\n *--cflags*x265*) echo '-I"+q(prefix)+"/include';;\n *--libs*x265*) echo '-L"+q(prefix)+"/lib -lx265 "+cpp_runtime+" -lm';;\n *x265*) exit 0;;\n"
    kvazaar_pkg = " *--modversion*kvazaar*) echo 2.3.2;;\n *--cflags*kvazaar*) echo '-I"+q(prefix)+"/include';;\n *--libs*kvazaar*) echo '-L"+q(prefix)+"/lib -lkvazaar -lm -pthread';;\n *kvazaar*) exit 0;;\n"
    pkg.write_text("#!/bin/sh\ncase \"$*\" in\n *--version*) echo 1.8.0;;\n"+x265_pkg+kvazaar_pkg+" *--modversion*) echo 2.6.0;;\n *--cflags*) echo '-I"+q(prefix)+"/include';;\n *--libs*) echo '-L"+q(prefix)+"/lib -lopenh264 "+cpp_runtime+" -lm';;\n *openh264*) exit 0;;\n *) exit 1;;\nesac\n", encoding="utf-8")
    pkg.chmod(0o755)
    openh_work = work / "openh264"
    # Objetos por versão: configurar o 9.x por cima dos objetos do 8.x misturava
    # arquivos .d/.o de duas árvores de fonte.
    ff_work = work / f"ffmpeg-{FFMPEG_VERSION}"
    openh_work.mkdir(exist_ok=True)
    ff_work.mkdir(exist_ok=True)
    steps = [
        [make, "-f", q(openh264 / "Makefile"), f"-j{args.jobs}", "USE_ASM=No", "V=No", f"PREFIX={q(prefix)}", *oh, "install-static"],
        ["sh", q(ffmpeg / "configure"), f"--prefix={q(prefix)}", f"--cc={cc}", f"--cxx={cxx}", f"--ar={ar}", f"--nm={nm}", f"--ranlib={ranlib}", f"--pkg-config={q(pkg)}", *common],
        [make, f"-j{args.jobs}"], [make, "install"],
    ]
    manifest_path = prefix / "aurea-ffmpeg-build.json"
    license_dir = prefix / "licenses"
    license_dir.mkdir(exist_ok=True)
    shutil.copy2(ffmpeg / "COPYING.LGPLv2.1", license_dir / "FFmpeg-LGPL-2.1.txt")
    shutil.copy2(openh264 / "LICENSE", license_dir / "OpenH264-BSD-2-Clause.txt")
    shutil.copy2(kvazaar_original / "LICENSE", license_dir / "Kvazaar-BSD-3-Clause.txt")
    if args.with_x265:
        shutil.copy2(ffmpeg / "COPYING.GPLv2", license_dir / "FFmpeg-GPL-2.0.txt")
        shutil.copy2(x265_source.parent / "COPYING", license_dir / "x265-GPL-2.0.txt")
    else:
        # An explicit prefix can be reused for a different licensed variant.
        for name in ("FFmpeg-GPL-2.0.txt", "x265-GPL-2.0.txt"):
            (license_dir / name).unlink(missing_ok=True)
    write_dependency_notice(license_dir, args, native_arch, kvazaar_patch, ffmpeg_patch)
    shutil.copy2(ROOT / "tools/patches" / ffmpeg_patch["patch"], license_dir / ffmpeg_patch["patch"])
    expected_archives = ("avformat", "avcodec", "swscale", "swresample", "avutil", "openh264", "kvazaar") + (("x265",) if args.with_x265 else ())
    if manifest_path.exists() and not args.force:
        previous = json.loads(manifest_path.read_text(encoding="utf-8"))
        if (previous.get("configure") == steps[1] and previous.get("ffmpeg") == FFMPEG_VERSION
                and previous.get("openh264") == OPENH264_VERSION
                and previous.get("kvazaar") == KVAZAAR_VERSION
                and previous.get("x265") == (X265_VERSION if args.with_x265 else None)
                and previous.get("ffmpeg_patches", {}).get("mediacodec_safety") == ffmpeg_patch
                and (prefix / "include/libavcodec/avcodec.h").exists()
                and all((prefix / f"lib/lib{name}.a").is_file() and (prefix / f"lib/lib{name}.a").stat().st_size > 0 for name in expected_archives)):
            # CI restores installed prefixes without their object trees. Verify
            # all bytes, including ABI-generated headers, before reusing them.
            headers_valid = installed_headers_match(ffmpeg, prefix, previous.get("public_headers", {}))
            generated_headers_available = all((ff_work / name).is_file()
                for name, generated in public_header_list(ffmpeg) if generated)
            if headers_valid or generated_headers_available:
                # Repair incomplete Windows make installs only when this exact
                # native build still supplies its generated ABI headers.
                if not headers_valid:
                    previous["public_headers"] = install_public_headers(ffmpeg, ff_work, prefix)
                previous["kvazaar_patches"] = {"rdcost_debug_cleanup": kvazaar_patch}
                write_manifest(manifest_path, previous)
                print(f"Native export dependency manifest matches: {prefix}", flush=True)
                return
            print("Cached public headers are incomplete; rebuilding native FFmpeg dependencies", flush=True)
    for index, step in enumerate(steps):
        cwd = openh_work if index == 0 else ff_work
        run([bash, "-c", shlex.join(step)], cwd=cwd, env=env)
        if windows and index == 1:
            # Native NDK make consumes Win32 paths, while configure uses POSIX
            # pwd. Convert only path strings in generated build files.
            for file in (ff_work / "Makefile", ff_work / "ffbuild/config.mak"):
                content = file.read_text(encoding="utf-8")
                for drive in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
                    content = content.replace(f"/{drive.lower()}/", f"{drive}:/")
                file.write_text(content, encoding="utf-8")
    installed_headers = install_public_headers(ffmpeg, ff_work, prefix)
    manifest = {"platform": args.platform, "arch": native_arch, "api": args.api if args.platform == "android" else None,
                "ffmpeg": FFMPEG_VERSION, "openh264": OPENH264_VERSION, "kvazaar": KVAZAAR_VERSION, "x265": X265_VERSION if args.with_x265 else None,
                "sources": [{"url": u, "sha256": h} for _, u, h in (*SOURCES, *((X265_SOURCE,) if args.with_x265 else ()))], "configure": steps[1],
                "public_headers": installed_headers, "kvazaar_patches": {"rdcost_debug_cleanup": kvazaar_patch},
                "ffmpeg_patches": {"mediacodec_safety": ffmpeg_patch}}
    write_manifest(manifest_path, manifest)
    print(f"Built native export libraries at {prefix}", flush=True)

if __name__ == "__main__":
    main()
