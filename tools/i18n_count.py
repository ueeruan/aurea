"""Medidor de texto visível ainda inline no Kotlin (Fase 8.1).

O `count_strings.py` antigo usa uma regex que casa DE UMA ASPA A OUTRA — pega
o trecho de código ENTRE dois literais (`', suffix = '`) e conta como texto. Este
aqui LÊ o Kotlin: comentário (inclusive aninhado), string com `${...}` (que pode
ter outra string dentro), string crua `\"\"\"` e char `'x'`. Cada literal sai
inteiro, com as interpolações trocadas por `{}`.

Depois, decide se o literal é TEXTO DE INTERFACE. Fica de fora (com motivo):
  - identificador: snake_case, camelCase, pontuado (`aurea.blur.gaussian`), MIME;
  - nome técnico igual em todo idioma: proporção, resolução, codec, unidade;
  - argumento de log (`Log.d(TAG, "...")`), `error("...")`/`require(...)`/
    `check(...)`: mensagem de DESENVOLVEDOR, nunca chega à tela;
  - chave de mapa/JSON (`"x" to`, `getString("x")`, `put("x", ...)`);
  - `@Preview`, `TAG`, anotação;
  - rótulo de passo de desfazer (`group("...")`, `beginGesture("...")`): vai
    para o motor e nunca é exibido;
  - `keywords = "..."`: sinônimos de busca, não aparecem na tela.

Uso: `python tools/i18n_count.py [raiz] [--list]`.
"""
import os
import re
import sys

ROOT_DEFAULT = "android/app/src/main/java"


def lex(src):
    """Gera (inicio, fim, texto, tem_template) de cada literal de string."""
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
            continue
        if src.startswith("/*", i):
            depth, i = 1, i + 2
            while i < n and depth:
                if src.startswith("/*", i):
                    depth, i = depth + 1, i + 2
                elif src.startswith("*/", i):
                    depth, i = depth - 1, i + 2
                else:
                    i += 1
            continue
        if c == "'":
            # char literal: 'a', '\n', '\u0000'
            j = i + 1
            if j < n and src[j] == "\\":
                j += 2
                while j < n and src[j] != "'":
                    j += 1
            else:
                j += 1
            i = j + 1
            continue
        if c == '"':
            start = i
            text, i, tpl = read_string(src, i)
            yield start, i, text, tpl
            continue
        i += 1


def read_string(src, i):
    """Lê uma string a partir de `i` (na aspa). Devolve (texto, fim, template)."""
    n = len(src)
    raw = src.startswith('"""', i)
    i += 3 if raw else 1
    out, tpl = [], False
    while i < n:
        if raw and src.startswith('"""', i):
            # aspas extras no fim de string crua pertencem ao conteúdo
            while src.startswith('""""', i):
                out.append('"')
                i += 1
            return "".join(out), i + 3, tpl
        c = src[i]
        if not raw and c == '"':
            return "".join(out), i + 1, tpl
        if not raw and c == "\\":
            out.append(src[i:i + 2])
            i += 2
            continue
        if c == "$" and i + 1 < n and src[i + 1] == "{":
            tpl = True
            i = skip_block(src, i + 1)
            out.append("{}")
            continue
        if c == "$" and i + 1 < n and (src[i + 1].isalpha() or src[i + 1] == "_"):
            tpl = True
            i += 1
            while i < n and (src[i].isalnum() or src[i] == "_"):
                i += 1
            out.append("{}")
            continue
        out.append(c)
        i += 1
    return "".join(out), n, tpl


def skip_block(src, i):
    """`i` aponta para `{`; devolve o índice depois do `}` casado."""
    depth, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '"':
            _t, i, _ = read_string(src, i)
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return n


WORD = re.compile(r"[A-Za-zÀ-ÿ]{2,}")
TECH = (
    re.compile(r"^[a-z][a-zA-Z0-9]*$"),                       # id, camelCase
    re.compile(r"^[A-Za-z0-9_]+(\.[A-Za-z0-9_{}]+)+$"),       # aurea.blur.x
    re.compile(r"^[a-z0-9_{}]+$"),                            # snake_case
    re.compile(r"^[A-Z0-9_]+$"),                              # CONST, FPS
    re.compile(r"^[a-z]+/[a-z0-9.+*-]+$"),                    # MIME
    re.compile(r"^[0-9]+(:[0-9]+)+$"),                        # 16:9
    re.compile(r"^[0-9.,]+ ?[a-zA-Z%°]{0,4}$"),               # 4K, 1080p, 30 fps
    re.compile(r"^\W*(\{\}\W*)+$"),                           # só template
    re.compile(r"^%[-0-9.$]*[a-zA-Z]"),                       # formato
    re.compile(r"^[.a-z0-9_-]*\.[a-z0-9]{2,5}$"),             # arquivo.ext
    re.compile(r"^(OMX|c2)\."),
)
# Nomes técnicos que ficam latinos em qualquer idioma (Fase 8.1 fe565a3).
TECH_WORDS = {
    "Aurea", "AureaEngine", "AureaStore", "AndroidKeyStore", "SDR", "HDR",
    "H.264", "H.265", "HEVC", "AVC", "AV1", "VP9", "AAC", "Opus", "MP4", "WebM",
    "GIF", "PNG", "JPEG", "WAV", "Full HD", "Ultra HD", "px", "fps", "ms", "dB",
    "RGB", "RGBA", "HSV", "HSL", "Hex", "HEX", "sRGB", "Rec.709", "BT.709",
    "Vulkan", "GPU", "CPU", "RAM", "SVG", "Lottie", "glTF", "GLB", "FBX", "OBJ",
    "English", "Español", "Português", "Русский", "हिन्दी", "Bahasa Indonesia",
    "العربية", "Português (Brasil)", "X", "Y", "Z", "R", "G", "B", "A", "OK",
    "Auto", "Full", "Bézier", "Bezier", "Aurea Particular",
}
DEV_CALL = re.compile(
    r"(Log\.[a-z]\s*\(|error\s*\(|require\w*\s*\(|check\w*\s*\(|"
    r"IllegalStateException\s*\(|IllegalArgumentException\s*\(|"
    r"RuntimeException\s*\(|println\s*\(|Exception\s*\(|"
    r"TAG\s*,|Trace\.\w+\s*\(|traceSection\s*\(|beginSection\s*\(|"
    r"@Preview\s*\(|@Suppress\s*\(|@JvmName\s*\(|@OptIn|"
    r"getString\s*\(|getInt\s*\(|getFloat\s*\(|getLong\s*\(|getBoolean\s*\(|"
    r"putString\s*\(|putInt\s*\(|putFloat\s*\(|putLong\s*\(|putBoolean\s*\(|"
    r"optString\s*\(|optInt\s*\(|optDouble\s*\(|optJSONArray\s*\(|optJSONObject\s*\(|"
    r"getJSONArray\s*\(|getJSONObject\s*\(|has\s*\(|"
    r"\.put\s*\(|Regex\s*\(|toRegex|split\s*\(|startsWith\s*\(|endsWith\s*\(|"
    r"System\.loadLibrary\s*\(|getSystemService|File\s*\(|resolve\s*\(|"
    r"key\s*=\s*|tag\s*=\s*|animate\w*\(|Transition\s*\(|"
    # rótulo de passo de desfazer: vai para o motor, nunca é exibido
    r"group\s*\(|beginGesture\s*\(|beginUndoGroup\s*\(|"
    # sinônimos de busca (não aparecem; a busca também indexa o nome traduzido)
    r"keywords\s*=\s*|searchKeywords\s*=\s*)\s*$"
)
UNIT_CTX = re.compile(r"(suffix|unit)\s*=\s*$")
MAP_KEY_AFTER = re.compile(r"^\s*(to\b|->|in\s)")
# Na MESMA linha, antes do literal: `group(if (x) "a" else "b")` também é desfazer.
LINE_TECH = re.compile(r"\b(group|beginGesture|beginUndoGroup|Log\.[a-z])\s*\(|\bkeywords\s*=")


def is_visible(text, before, after):
    s = text.strip()
    if not WORD.search(s.replace("{}", "")):
        return False
    if s in TECH_WORDS:
        return False
    # Unidade exibida ao lado do número ("quadros") é texto, mesmo minúscula.
    if UNIT_CTX.search(before[-20:]) and not DEV_CALL.search(before[-70:]):
        return True
    for pat in TECH:
        if pat.match(s):
            return False
    if DEV_CALL.search(before[-70:]):
        return False
    if LINE_TECH.search(before.rsplit("\n", 1)[-1]):
        return False
    if MAP_KEY_AFTER.match(after) and " " not in s:
        return False
    # String com barra/ponto e sem espaço é caminho, chave ou pacote.
    if " " not in s and any(ch in s for ch in "/\\#@=<>[]"):
        return False
    return True


def scan_file(path):
    src = open(path, encoding="utf-8").read()
    for start, end, text, _tpl in lex(src):
        before = src[max(0, start - 120):start]
        after = src[end:end + 12]
        if is_visible(text, before, after):
            line = src.count("\n", 0, start) + 1
            yield line, text


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    root = args[0] if args else ROOT_DEFAULT
    listing = "--list" in sys.argv
    distinct, total, per_file = set(), 0, {}
    for base, _, files in os.walk(root):
        for name in sorted(files):
            if not name.endswith(".kt"):
                continue
            path = os.path.join(base, name)
            rows = list(scan_file(path))
            if not rows:
                continue
            per_file[path] = len(rows)
            for line, text in rows:
                distinct.add(text)
                total += 1
                if listing:
                    print(f"{path.replace(os.sep, '/')}:{line}\t{text}")
    if listing:
        return
    print("literais visiveis distintos:", len(distinct))
    print("ocorrencias               :", total)
    print("arquivos com texto        :", len(per_file))
    for path, count in sorted(per_file.items(), key=lambda kv: -kv[1])[:40]:
        print(f"{count:5d}  {path}")


if __name__ == "__main__":
    main()
