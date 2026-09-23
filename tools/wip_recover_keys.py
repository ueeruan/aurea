"""Recupera (chave -> texto pt-BR) do rascunho de conversao Kotlin.

O `wip_kotlin_i18n.patch` troca cada literal por `R.string.<chave>`, mas os
catalogos com essas chaves nunca foram gerados — o texto de origem de cada chave
so existe no diff.

Alinhar por LINHA nao serve: um hunk que troca varios literais de uma vez deixa
a correspondencia ambigua. O alinhamento aqui e por PALAVRA, que e exato — o
diff vira `\"Texto\"` -> `R.string.chave` e os dois lados casam um a um. Quando
o par nao fecha (chave sem literal do lado antigo, ou literal com interpolacao),
a chave sai marcada para revisao a mao.

Uso:
  python tools/wip_recover_keys.py            # resumo, faltantes e ambiguas
  python tools/wip_recover_keys.py --json     # {chave: texto} para consumo
"""
import difflib
import io
import json
import os
import re
import subprocess
import sys

FILES = [
    "android/app/src/main/java/com/aurea/aurea/editor/AddLayerPanel.kt",
    "android/app/src/main/java/com/aurea/aurea/editor/BottomArea.kt",
    "android/app/src/main/java/com/aurea/aurea/editor/EditorScreen.kt",
    "android/app/src/main/java/com/aurea/aurea/editor/ExportScreen.kt",
    "android/app/src/main/java/com/aurea/aurea/editor/Menus.kt",
    "android/app/src/main/java/com/aurea/aurea/editor/ProjectSettingsSheet.kt",
    "android/app/src/main/java/com/aurea/aurea/editor/Stage.kt",
    "android/app/src/main/java/com/aurea/aurea/editor/panels/AppearancePanel.kt",
    "android/app/src/main/java/com/aurea/aurea/editor/panels/CaptionsPanel.kt",
    "android/app/src/main/java/com/aurea/aurea/editor/panels/CurvePanel.kt",
    "android/app/src/main/java/com/aurea/aurea/editor/panels/EffectsHuman.kt",
    "android/app/src/main/java/com/aurea/aurea/editor/panels/Element3DPanel.kt",
    "android/app/src/main/java/com/aurea/aurea/editor/panels/ExpressionSheet.kt",
    "android/app/src/main/java/com/aurea/aurea/editor/panels/FormaKit.kt",
    "android/app/src/main/java/com/aurea/aurea/editor/panels/MaskPanel.kt",
    "android/app/src/main/java/com/aurea/aurea/editor/panels/PanelChrome.kt",
    "android/app/src/main/java/com/aurea/aurea/editor/panels/Panels.kt",
    "android/app/src/main/java/com/aurea/aurea/effects/EffectCatalogMeta.kt",
    "android/app/src/main/java/com/aurea/aurea/presets/BuiltinPresets.kt",
    "android/app/src/main/java/com/aurea/aurea/presets/PresetLibrary.kt",
]

KEY = re.compile(r"R\.string\.([a-z0-9_]+)")
STRING_LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')


def old_text(path):
    out = subprocess.run(["git", "show", "HEAD:" + path], capture_output=True, text=True, encoding="utf-8")
    return out.stdout


def new_text(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def align(old_src, new_src):
    """Casa cada `R.string.<chave>` do arquivo novo com o literal que ele trocou.

    Primeiro um diff por LINHA (barato) para achar os trechos que mudaram; so
    dentro deles o alinhamento e por palavra (caro, mas o trecho e curto).
    """
    a, b = old_src.splitlines(), new_src.splitlines()
    sm = difflib.SequenceMatcher(a=a, b=b, autojunk=False)
    found = {}
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            continue
        wa, wb = tokens("\n".join(a[i1:i2])), tokens("\n".join(b[j1:j2]))
        ws = difflib.SequenceMatcher(a=wa, b=wb, autojunk=False)
        for wtag, k1, k2, l1, l2 in ws.get_opcodes():
            if wtag == "equal":
                continue
            left = [t for t in wa[k1:k2] if STRING_LIT.fullmatch(t)]
            keys = [KEY.fullmatch(t).group(1) for t in wb[l1:l2] if KEY.fullmatch(t)]
            for idx, key in enumerate(keys):
                if idx < len(left):
                    src = left[idx]
                elif len(left) == 1:
                    src = left[0]
                else:
                    src = ""
                found.setdefault(key, set()).add(src.strip('"'))
    return found


def tokens(src):
    """Palavras e literais como tokens separados (o diff fica alinhado)."""
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '"':
            j = i + 1
            while j < n and src[j] != '"':
                j += 2 if src[j] == "\\" else 1
            out.append(src[i:j + 1])
            i = j + 1
        elif c.isalnum() or c in "._":
            j = i
            while j < n and (src[j].isalnum() or src[j] in "._"):
                j += 1
            out.append(src[i:j])
            i = j
        else:
            out.append(c)
            i += 1
    return out


def main():
    found = {}
    for path in FILES:
        if not os.path.exists(path):
            continue
        for key, texts in align(old_text(path), new_text(path)).items():
            found.setdefault(key, set()).update(texts)
    flat, ambiguous, missing = {}, [], []
    for key, texts in found.items():
        real = sorted(t for t in texts if t)
        if len(set(real)) > 1:
            ambiguous.append((key, sorted(set(real))))
        if not real:
            missing.append(key)
        flat[key] = real[0] if real else ""
    if "--json" in sys.argv:
        io.open("tools/wip_keys_recovered.json", "w", encoding="utf-8").write(
            json.dumps(flat, ensure_ascii=False, indent=1))
        print("escrito tools/wip_keys_recovered.json (%d chaves)" % len(flat))
        return
    print("chaves no rascunho : %d" % len(found))
    print("com texto          : %d" % sum(1 for v in flat.values() if v))
    print("sem texto          : %d" % len(missing))
    print("ambiguas           : %d" % len(ambiguous))
    for key in sorted(missing):
        print("   SEM TEXTO  %s" % key)
    for key, texts in ambiguous:
        print("   AMBIGUA    %-30s %s" % (key, " | ".join(texts)))


if __name__ == "__main__":
    main()
