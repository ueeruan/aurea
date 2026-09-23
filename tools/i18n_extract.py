"""Extrator de texto de interface do Kotlin (Fase 8.1).

Não é um tradutor nem um reescritor: é o MAPA. Ele diz, para cada literal,
ONDE ele está e QUAL é o contexto — e é o contexto que decide se aquilo é texto
de interface ou um identificador técnico.

O que ele NUNCA marca como texto (e o motivo):
  - `key = "..."`            → chave de item de lista; some no build, não é lido
  - `label = "..."` dentro de animateFloatAsState → rótulo de ferramenta de
    depuração do Compose
  - `"aurea/..."`, `"video/..."`, `"16:9"`, `"4K"` → id, MIME e nome técnico
  - `%1$s`-only, `"%d"`, nomes de arquivo/pasta
  - qualquer coisa sem duas letras seguidas

Saída: TSV `arquivo<TAB>linha<TAB>contexto<TAB>literal`, para revisão.
"""
import os
import re
import sys

LINE_COMMENT = re.compile(r"//[^\n]*")
BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.S)
STRING = re.compile(r'"((?:[^"\\]|\\.)*)"')
WORDY = re.compile(r"[A-Za-zÀ-ÿ]{2}")

# Parâmetros cujo valor é TEXTO de interface.
UI_PARAMS = (
    "Text(", "title =", "subtitle =", "label =", "hint =", "placeholder =",
    "message =", "confirmLabel =", "cancelLabel =", "actionLabel =", "text =",
    "contentDescription =", "description =", "note =", "kicker =", "caption =",
    "header =", "emptyMessage =", "summary =", "unit =", "suffix =",
)

# Parâmetros cujo valor NUNCA é texto de interface.
TECH_PARAMS = (
    "key =", "tag =", "path =", "id =", "source =", "uri =", "mime =",
    "mimeType =", "type =", "name =", "scheme =", "action =", "fontFamily =",
    "family =", "extension =",
)

# Literais que são técnica, não texto, mesmo soltos.
TECH_PATTERNS = (
    re.compile(r"^[a-z][a-z0-9]*([./:_-][a-zA-Z0-9]+)+$"),   # aurea/x, video/avc
    re.compile(r"^[0-9]+(:[0-9]+)+$"),                        # 16:9
    re.compile(r"^[0-9]+[a-zA-Z]*$"),                         # 4K, 1080p
    re.compile(r"^[A-Z_]+$"),                                 # GDK, FPS
    re.compile(r"^[\W_]+$"),
    re.compile(r"^%[0-9$]*[a-z]$"),                           # "%s", "%1$d"
    re.compile(r"^[a-z]+_[a-z_]+$"),                          # snake_case
)


def strip_source(src: str) -> str:
    src = BLOCK_COMMENT.sub("", src)
    return LINE_COMMENT.sub("", src)


def classify(before: str) -> str:
    tail = before[-80:]
    if any(t in tail for t in TECH_PARAMS):
        return "tech"
    if any(u in tail for u in UI_PARAMS):
        return "ui"
    return "?"


def looks_like_text(s: str) -> bool:
    if len(s) < 2 or not WORDY.search(s):
        return False
    if "\n" in s:
        return False
    for pat in TECH_PATTERNS:
        if pat.match(s):
            return False
    # Uma única palavra minúscula é quase sempre id, não rótulo.
    if " " not in s and s[:1].islower() and len(s) < 12:
        return False
    return True


def scan(path: str):
    src = strip_source(open(path, encoding="utf-8").read())
    out = []
    for m in STRING.finditer(src):
        value = m.group(1)
        if not looks_like_text(value):
            continue
        line = src.count("\n", 0, m.start()) + 1
        out.append((path, line, classify(src[max(0, m.start() - 80):m.start()]), value))
    return out


if __name__ == "__main__":
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    rows = []
    for base, _, files in os.walk(root):
        for name in sorted(files):
            if name.endswith(".kt"):
                rows.extend(scan(os.path.join(base, name)))
    for path, line, ctx, value in rows:
        print(f"{path}\t{line}\t{ctx}\t{value}")
    print(f"# {len(rows)} candidatos", file=sys.stderr)
