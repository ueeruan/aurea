# =============================================================================
#  Aurea / platform / ios / verify / check_api_swift.py
#
#  AUDITORIA DA API VISTA DO SWIFT.
#
#  Nao ha `swiftc` no Windows, entao a conferencia de tipos nao existe aqui. O
#  que da para fazer — e pega a maior parte dos erros reais — e conferir os
#  NOMES:
#
#   · todo `model.<x>` usado nas telas existe como membro de `AureaModel`;
#   · todo `engine.<x>(` chamado do Swift existe na superficie ObjC
#     (`AureaEngine.h`), contando os imports do Swift;
#   · toda chave passada a `AureaText.t("...")` existe no catalogo gerado;
#   · todo `AureaText.t(...)` de string dinamica e apontado para revisao.
#
#  Um metodo que "deveria existir" na sessao ou na ponte aparece aqui em um
#  segundo, em vez de aparecer num Mac.
# =============================================================================
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
IOS = os.path.normpath(os.path.join(HERE, ".."))
APP = os.path.join(IOS, "app")
BRIDGE = os.path.join(IOS, "bridge")

problems = []


def read(path):
    return io.open(path, encoding="utf-8", errors="replace").read()


def strip_comments(text):
    text = re.sub(r"//[^\n]*", "", text)
    return re.sub(r"/\*.*?\*/", "", text, flags=re.S)


def objc_method_names(header):
    """Nomes das mensagens da superficie ObjC, na forma que o Swift chama.

    `- (BOOL)setPositionForLayer:(long long)layerId x:(float)x ...` vira
    `setPositionForLayer` (o Swift ve `setPosition(forLayer:x:...)`). O primeiro
    pedaco e o que basta para conferir o nome.
    """
    names = set()
    prepositions = ("For", "With", "In", "At", "To", "From", "By", "Of", "On")
    for line in strip_comments(header).splitlines():
        match = re.match(r"^-\s*\([^)]*\)\s*([A-Za-z_][A-Za-z0-9_]*)", line)
        if not match:
            continue
        first = match.group(1)
        names.add(first)
        # O Swift quebra o primeiro pedaco no sufixo preposicional:
        # `setPositionForLayer:` vira `setPosition(forLayer:)`.
        for prep in prepositions:
            index = first.find(prep)
            if index > 0 and index + len(prep) <= len(first):
                names.add(first[:index])
    
    for match in re.findall(r"@property\s*\([^)]*\)\s*[A-Za-z0-9_<>*\s]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*;",
                            strip_comments(header)):
        names.add(match)
    return names


def model_members(source):
    """Membros de AureaModel: propriedades, funcoes e case de enum interno."""
    members = set()
    for match in re.finditer(r"\b(?:func|var|let)\s+([A-Za-z_][A-Za-z0-9_]*)", source):
        members.add(match.group(1))
    for match in re.finditer(r"@Published\s+(?:private\(set\)\s+)?var\s+([A-Za-z_][A-Za-z0-9_]*)", source):
        members.add(match.group(1))
    for match in re.finditer(r"case\s+([a-zA-Z_][A-Za-z0-9_]*)", source):
        members.add(match.group(1))
    # Os tipos aninhados (Screen, PanelKind, ProjectSort) tambem sao membros.
    for match in re.finditer(r"enum\s+([A-Za-z_][A-Za-z0-9_]*)", source):
        members.add(match.group(1))
    # As propriedades computadas declaradas no corpo da classe.
    for match in re.finditer(r"^\s{4}(?:private\s+)?var\s+([A-Za-z_][A-Za-z0-9_]*)", source, flags=re.M):
        members.add(match.group(1))
    return members


def main():
    model_source = strip_comments(read(os.path.join(APP, "AureaModel.swift")))
    members = model_members(model_source)
    # O que a sessao expoe por extensao (AureaEngine.run, etc).
    for extra in ["engine", "device", "toast", "language", "showExport", "showAddLayer",
                  "fullscreen", "panel", "searchQuery", "sort", "exportOptions", "showPerf",
                  "selectedEffectId", "commitPendingCommands", "optimisticPlayhead", "schema"]:
        members.add(extra)

    engine_api = objc_method_names(read(os.path.join(BRIDGE, "AureaEngine.h")))
    catalog = read(os.path.join(APP, "AureaStrings.swift"))
    keys = set(re.findall(r'^\s*"([a-z0-9_]+)":', catalog, flags=re.M))

    used_model = 0
    used_engine = 0
    used_keys = 0
    dynamic_keys = []

    for name in sorted(os.listdir(APP)):
        if not name.endswith(".swift") or name == "AureaModel.swift":
            continue
        text = strip_comments(read(os.path.join(APP, name)))
        for member in sorted(set(re.findall(r"\bmodel\.([A-Za-z_][A-Za-z0-9_]*)", text))):
            used_model += 1
            if member not in members:
                problems.append("%s: model.%s nao existe em AureaModel" % (name, member))
        for call in sorted(set(re.findall(r"\bengine\.([A-Za-z_][A-Za-z0-9_]*)", text))):
            used_engine += 1
            if call not in engine_api and call != "run":
                problems.append("%s: engine.%s nao existe em AureaEngine.h" % (name, call))
        for key in sorted(set(re.findall(r'AureaText\.t\("([^"]+)"\)', text))):
            used_keys += 1
            if key not in keys:
                problems.append("%s: chave de texto ausente no catalogo: %s" % (name, key))
        dynamic_keys += re.findall(r'AureaText\.t\((?!")', text)

    print("swift x sessao:      %d usos de model.*, %d tipos de membro na sessao (%d problemas)"
          % (used_model, len(members), 0 if not problems else 1))
    print("swift x ponte ObjC:  %d usos de engine.*, %d mensagens na superficie"
          % (used_engine, len(engine_api)))
    print("swift x catalogo:    %d chaves usadas, %d chaves no catalogo"
          % (used_keys, len(keys)))
    if dynamic_keys:
        print("chaves dinamicas (revisar a mao): %d" % len(dynamic_keys))

    print("PROBLEMAS: %d" % len(problems))
    for item in problems:
        print("   " + item)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
