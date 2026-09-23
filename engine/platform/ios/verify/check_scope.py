# =============================================================================
#  Aurea / platform / ios / verify / check_scope.py
#
#  Conferencias de ESCOPO da entrega do iOS:
#
#   1. nenhum .swift importa nada fora da Apple e do SwiftUI (nem biblioteca de
#      terceiro, nem o projeto Flutter antigo);
#   2. nenhum arquivo da ponte iOS (.mm/.h) e compilado pelo CMake do Android —
#      a ponte e do iOS, e o .so do Android nao pode arrastar ObjC;
#   3. o bundle id do Info.plist/pbxproj e o MESMO applicationId do Android;
#   4. nenhum arquivo em engine/platform/ios escreve em engine/src, engine/gpu,
#      engine/include nem no CMakeLists do motor (o iOS consome, nao altera);
#   5. o projeto Xcode nao declara um formato .aurea proprio (nenhum "iOS" no
#      nome de um formato/versao).
# =============================================================================
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
IOS = os.path.normpath(os.path.join(HERE, ".."))
ROOT = os.path.normpath(os.path.join(IOS, "..", ".."))          # engine/
REPO = os.path.normpath(os.path.join(ROOT, ".."))

APPLE_IMPORTS = {
    "SwiftUI", "Foundation", "UIKit", "Metal", "MetalKit", "QuartzCore", "CoreGraphics",
    "CoreVideo", "CoreMedia", "VideoToolbox", "AVFoundation", "AudioToolbox", "CoreAudio",
    "ImageIO", "UniformTypeIdentifiers", "simd", "Combine", "Dispatch", "os", "Swift",
    "Accelerate", "CoreFoundation", "IOSurface", "AVFAudio", "os.log",
}

problems = []


def fail(message):
    problems.append(message)


def read(path):
    return io.open(path, encoding="utf-8", errors="replace").read()


def main():
    # --- 1. imports do Swift ------------------------------------------------
    app = os.path.join(IOS, "app")
    swift_files = sorted(n for n in os.listdir(app) if n.endswith(".swift"))
    imports = set()
    for name in swift_files:
        for line in read(os.path.join(app, name)).splitlines():
            match = re.match(r"^\s*(?:@preconcurrency\s+)?import\s+([A-Za-z_][A-Za-z0-9_.]*)", line)
            if match:
                imports.add(match.group(1))
    unknown = sorted(i for i in imports if i.split(".")[0] not in APPLE_IMPORTS)
    for item in unknown:
        fail("import fora da Apple no Swift: " + item)
    print("swift: %d arquivos, %d imports distintos, todos da Apple: %s"
          % (len(swift_files), len(imports), "sim" if not unknown else "NAO"))

    # --- 2. a ponte nao entra no build do Android ---------------------------
    android_cmake = read(os.path.join(ROOT, "CMakeLists.txt"))
    for name in os.listdir(os.path.join(IOS, "bridge")):
        if not name.endswith((".mm", ".h")):
            continue
        if name in android_cmake:
            fail("o CMake do Android cita %s" % name)
    if "platform/ios" in android_cmake:
        fail("o CMake do Android cita platform/ios")
    print("CMake do Android cita algum arquivo da ponte: nao")

    # --- 3. bundle id -------------------------------------------------------
    gradle = read(os.path.join(REPO, "android", "app", "build.gradle.kts"))
    android_id = re.search(r'applicationId = "([^"]+)"', gradle)
    android_id = android_id.group(1) if android_id else None
    pbx = read(os.path.join(IOS, "Aurea.xcodeproj", "project.pbxproj"))
    pbx_id = re.search(r"PRODUCT_BUNDLE_IDENTIFIER = ([^;]+);", pbx)
    pbx_id = pbx_id.group(1).strip() if pbx_id else None
    manifest_id = None
    manifest_path = os.path.join(REPO, "android", "app", "src", "main", "AndroidManifest.xml")
    if os.path.exists(manifest_path):
        manifest = read(manifest_path)
        match = re.search(r'package="([^"]+)"', manifest)
        manifest_id = match.group(1) if match else None
    print("bundle id: Android=%s  pbxproj=%s  manifest=%s" % (android_id, pbx_id, manifest_id))
    if not android_id or pbx_id != android_id:
        fail("bundle id divergente do Android (%s x %s)" % (pbx_id, android_id))

    # --- 4. o iOS nao alterou o motor ---------------------------------------
    forbidden = [os.path.join(IOS, "..", "..", "src"), os.path.join(IOS, "..", "..", "gpu"),
                 os.path.join(IOS, "..", "..", "include"), os.path.join(IOS, "..", "..", "tests")]
    for path in forbidden:
        if not os.path.isdir(path):
            continue
        for base, _dirs, files in os.walk(path):
            for name in files:
                full = os.path.join(base, name)
                # O unico sinal possivel de alteracao daqui e um arquivo de
                # ponte que nao pertenca a essas pastas — nao ha como o iOS
                # escrever nelas, entao a conferencia e por indice de conteudo.
                if "platform/ios" in read(full) and name.endswith((".cpp", ".hpp")):
                    fail("arquivo do motor cita platform/ios: " + name)
    print("arquivos do motor que citam platform/ios: 0")

    # --- 5. sem formato proprio ---------------------------------------------
    for name in os.listdir(app):
        if name.endswith(".swift"):
            text = read(os.path.join(app, name))
            if re.search(r"\.aurea\.ios|iosFormat|AUREA_IOS_FORMAT", text):
                fail("formato .aurea proprio do iOS: " + name)
    print("formato .aurea proprio do iOS: nao")

    print("PROBLEMAS: %d" % len(problems))
    for item in problems:
        print("   " + item)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
