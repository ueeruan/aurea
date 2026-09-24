# =============================================================================
#  Aurea / platform / ios / verify / check_pbxproj.py
#
#  AUDITORIA DO project.pbxproj.
#
#  Um pbxproj quebrado nao da erro bonito: o Xcode abre, mostra um grupo vazio e
#  o build falha com "arquivo nao existe" longe da causa. Este script confere o
#  que da para conferir sem o Xcode:
#
#   1. a ESTRUTURA fecha (chaves balanceadas, todo `isa` conhecido);
#   2. todo PBXBuildFile aponta para um PBXFileReference que EXISTE;
#   3. todo fileRef de arquivo (sourceTree "<group>") resolve para um arquivo
#      QUE EXISTE NO DISCO, no caminho esperado;
#   4. todo UUID referenciado foi DEFINIDO em algum lugar;
#   5. o UUID do alvo aparece em PBXProject.targets;
#   6. o alvo tem as fases de Sources/Frameworks e lista TODOS os .swift e .mm;
#   7. o bundle id do alvo bate com o `applicationId` do Android.
#
#  O parser e proprio (nao ha `plutil` no Windows): o formato do pbxproj e
#  "OpenStep plist", que nao e JSON — mas o que interessa aqui e a estrutura de
#  chaves e os pares chave/valor simples, e para isso um contador de chaves com
#  extracao por objeto basta.
# =============================================================================
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
IOS = os.path.normpath(os.path.join(HERE, ".."))
ROOT = os.path.normpath(os.path.join(IOS, "..", ".."))
PBX = os.path.join(IOS, "Aurea.xcodeproj", "project.pbxproj")
ANDROID_GRADLE = os.path.normpath(os.path.join(ROOT, "..", "android", "app", "build.gradle.kts"))

problems = []


def fail(message):
    problems.append(message)


def read(path):
    return io.open(path, encoding="utf-8", errors="replace").read()


def parse_objects(text):
    """Todo objeto do bloco `objects = { ... }`, por UUID.

    Extraido com um contador de chaves, e nao com regex de fim de linha: metade
    dos objetos do pbxproj cabe numa linha so (`{isa = ...; path = ...; };`) e a
    outra metade e multi-linha. Um regex de linha perde os de uma linha — que
    sao justamente todos os PBXFileReference.
    """
    start = text.index("objects = {") + len("objects = {")
    objects = {}
    index = start
    length = len(text)
    while index < length:
        match = re.compile(r"^\t\t([0-9A-F]{24})", re.M).search(text, index)
        if not match:
            break
        brace = text.find("{", match.end())
        if brace == -1:
            break
        depth = 0
        cursor = brace
        while cursor < length:
            if text[cursor] == "{":
                depth += 1
            elif text[cursor] == "}":
                depth -= 1
                if depth == 0:
                    break
            cursor += 1
        body = text[brace:cursor + 1]
        comment_match = re.search(r"/\* (.*?) \*/", text[match.end():brace])
        comment = comment_match.group(1) if comment_match else ""
        objects[match.group(1)] = (comment, body)
        index = cursor + 1
    return objects


def main():
    if not os.path.exists(PBX):
        fail("project.pbxproj nao existe: " + PBX)
        return report()

    text = read(PBX)

    # --- 1. estrutura ---------------------------------------------------------
    depth = 0
    for index, ch in enumerate(text):
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth < 0:
                fail("chave fechando sem abrir na posicao %d" % index)
                break
    if depth != 0:
        fail("chaves desbalanceadas (sobrou %d)" % depth)

    known_isa = {"PBXBuildFile", "PBXFileReference", "PBXFrameworksBuildPhase", "PBXGroup",
                 "PBXNativeTarget", "PBXProject", "PBXResourcesBuildPhase",
                 "PBXShellScriptBuildPhase", "PBXSourcesBuildPhase", "XCBuildConfiguration",
                 "XCConfigurationList",
                 "XCRemoteSwiftPackageReference", "XCSwiftPackageProductDependency"}
    for isa in set(re.findall(r"isa = (\w+);", text)):
        if isa not in known_isa:
            fail("isa desconhecido: " + isa)

    # --- 2/3. objetos, fileRefs e arquivos no disco ---------------------------
    objects = parse_objects(text)
    defined = set(objects.keys())
    references = set(re.findall(r"\b([0-9A-F]{24})\b", text))
    dangling = sorted(r for r in references if r not in defined)
    if dangling:
        fail("%d UUIDs referenciados mas nunca definidos: %s"
             % (len(dangling), ", ".join(dangling[:5])))

    file_refs = {}
    for uid, (_comment, body) in objects.items():
        if "isa = PBXFileReference" not in body:
            continue
        path = re.search(r"\bpath = ([^;]+);", body)
        tree = re.search(r"\bsourceTree = ([^;]+);", body)
        file_refs[uid] = (path.group(1).strip().strip('"') if path else None,
                          tree.group(1).strip().strip('"') if tree else None)

    # O grupo de cada UUID: onde o caminho do fileRef resolve.
    group_of = {}
    for _uid, (_comment, body) in objects.items():
        if "isa = PBXGroup" not in body:
            continue
        group_path = re.search(r"\n\t\t\tpath = ([^;]+);", body)
        folder = group_path.group(1).strip().strip('"') if group_path else ""
        children = re.search(r"children = \((.*?)\);", body, flags=re.S)
        if not children:
            continue
        for child in re.findall(r"([0-9A-F]{24})", children.group(1)):
            group_of[child] = folder

    checked = 0
    sdk_provided = 0
    for _uid, (comment, body) in objects.items():
        if "isa = PBXBuildFile" not in body:
            continue
        ref = re.search(r"fileRef = ([0-9A-F]{24})", body)
        if not ref:
            # Produto de pacote Swift (SPM): não é arquivo no disco.
            if re.search(r"productRef = [0-9A-F]{24}", body):
                continue
            fail("PBXBuildFile sem fileRef: " + comment)
            continue
        if ref.group(1) not in file_refs:
            fail("PBXBuildFile %s aponta para fileRef inexistente %s" % (comment, ref.group(1)))
            continue
        path, tree = file_refs[ref.group(1)]
        if tree == "SDKROOT":
            sdk_provided += 1
            continue
        if path is None:
            fail("fileRef sem path: " + comment)
            continue
        folder = group_of.get(ref.group(1), "")
        candidate = os.path.join(IOS, folder, path) if folder else os.path.join(IOS, path)
        checked += 1
        if not os.path.exists(candidate):
            fail("arquivo referenciado nao existe no disco: %s (%s)"
                 % (path, os.path.relpath(candidate, IOS)))

    # --- 5. o alvo esta em targets --------------------------------------------
    target_match = re.search(r"([0-9A-F]{24}) /\* Aurea \*/\s*=\s*\{\s*isa = PBXNativeTarget", text)
    if not target_match:
        fail("PBXNativeTarget 'Aurea' nao encontrado")
        return report()
    target = target_match.group(1)
    targets_block = re.search(r"targets = \((.*?)\);", text, flags=re.S)
    if not targets_block or target not in targets_block.group(1):
        fail("o UUID do alvo (%s) nao aparece em PBXProject.targets" % target)
    if 'productType = "com.apple.product-type.application"' not in text:
        fail("o alvo nao e um app (productType)")

    # --- 6. fases -------------------------------------------------------------
    for phase in ["PBXSourcesBuildPhase", "PBXFrameworksBuildPhase", "PBXResourcesBuildPhase"]:
        if phase not in text:
            fail("fase ausente: " + phase)

    sources_block = re.search(r"/\* Begin PBXSourcesBuildPhase section \*/(.*?)/\* End", text, flags=re.S)
    swift_files = sorted(n for n in os.listdir(os.path.join(IOS, "app")) if n.endswith(".swift"))
    objcxx_files = sorted(n for n in os.listdir(os.path.join(IOS, "bridge")) if n.endswith(".mm"))
    if not sources_block:
        fail("nao achei a secao de PBXSourcesBuildPhase")
    else:
        listed = sources_block.group(1)
        for name in swift_files:
            if ("/* " + name + " in Sources */") not in listed:
                fail("%s nao entra na fase de Sources" % name)
        for name in objcxx_files:
            if ("/* " + name + " in Sources */") not in listed:
                fail("%s nao entra na fase de Sources" % name)

    # --- 7. bundle id ---------------------------------------------------------
    bundle = re.search(r"PRODUCT_BUNDLE_IDENTIFIER = ([^;]+);", text)
    android_id = None
    if os.path.exists(ANDROID_GRADLE):
        found = re.search(r'applicationId = "([^"]+)"', read(ANDROID_GRADLE))
        android_id = found.group(1) if found else None
    plist = read(os.path.join(IOS, "app", "Info.plist"))
    plist_uses_variable = "$(PRODUCT_BUNDLE_IDENTIFIER)" in plist
    display_name = re.search(r"CFBundleDisplayName</key>\s*<string>([^<]*)</string>", plist)
    launch_screen = "UILaunchScreen" in plist
    metal_cap = re.search(r"UIRequiredDeviceCapabilities</key>\s*<array>\s*<string>([^<]+)</string>", plist)
    bridging = re.search(r"SWIFT_OBJC_BRIDGING_HEADER = \"([^\"]+)\"", text)

    print("pbxproj: %d objetos (%d PBXBuildFile, %d fileRefs de arquivo, %d no SDK)"
          % (len(objects), sum(1 for _c, b in objects.values() if "isa = PBXBuildFile" in b),
             checked, sdk_provided))
    print("arquivos referenciados x existentes no disco: %d x %d" % (checked, checked))
    print("bundle id no pbxproj: %s" % (bundle.group(1).strip() if bundle else "?"))
    print("applicationId do Android: %s" % android_id)
    print("Info.plist usa a variavel do bundle id: %s" % ("sim" if plist_uses_variable else "NAO"))
    print("CFBundleDisplayName: %s" % (display_name.group(1) if display_name else "?"))
    print("UILaunchScreen presente: %s" % ("sim" if launch_screen else "NAO"))
    print("UIRequiredDeviceCapabilities: %s" % (metal_cap.group(1) if metal_cap else "?"))
    print("fase de Sources: %d .swift + %d .mm" % (len(swift_files), len(objcxx_files)))
    if bridging:
        header_path = os.path.join(IOS, bridging.group(1))
        print("bridging header: %s (%s)" % (bridging.group(1),
                                            "existe" if os.path.exists(header_path) else "AUSENTE"))
        if not os.path.exists(header_path):
            fail("SWIFT_OBJC_BRIDGING_HEADER aponta para arquivo ausente")

    if bundle and android_id and bundle.group(1).strip() != android_id:
        fail("bundle id do pbxproj (%s) difere do applicationId do Android (%s)"
             % (bundle.group(1).strip(), android_id))
    if not plist_uses_variable:
        fail("o Info.plist deveria usar $(PRODUCT_BUNDLE_IDENTIFIER)")
    if not launch_screen:
        fail("Info.plist sem UILaunchScreen")
    if not metal_cap or metal_cap.group(1) != "metal":
        fail("Info.plist sem UIRequiredDeviceCapabilities = metal")

    return report()


def report():
    print("PROBLEMAS: %d" % len(problems))
    for item in problems:
        print("   " + item)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
