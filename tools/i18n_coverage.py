"""Cobertura dos catálogos de idioma (es, ru, id e os demais) contra o pt-BR.

Para cada idioma conta:
  - chaves que faltam (o Android cai no pt-BR: português vazando na tela);
  - chaves a mais (o padrão não tem);
  - argumentos de formato diferentes do pt-BR (`%1$s` x `%d`: crash ao formatar);
  - texto IGUAL ao pt-BR ou ao inglês — suspeita de texto não traduzido.

Texto igual nem sempre é erro ("Normal", "Zoom", "Instagram", "%1$d fps"): os
iguais já revisados à mão ficam em `tools/i18n_reviewed_identical.json`
(idioma -> chaves). Só os NÃO revisados contam como problema.

Uso: `python tools/i18n_coverage.py [--update-reviewed]` — sai com 1 se algum
idioma de LANGS_REQUIRED tiver problema.
"""
import glob
import json
import os
import re
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "android", "app", "src", "main", "res")
REVIEWED = os.path.join(ROOT, "tools", "i18n_reviewed_identical.json")
LANGS = ["en", "es", "ru", "id", "hi", "ar"]
LANGS_REQUIRED = ["en", "es", "ru", "id"]
FMT = re.compile(r"%(\d+\$)?[-#+ 0,(]*\d*(\.\d+)?[sdfxXeEgGc%]")
WORD = re.compile(r"[^\W\d_]{3,}")


def load(folder):
    out = {}
    for path in sorted(glob.glob(os.path.join(RES, folder, "strings*.xml"))):
        for node in ET.parse(path).getroot():
            if node.tag == "string" and node.get("translatable") != "false":
                out[node.get("name")] = "".join(node.itertext())
    return out


def args(text):
    found = []
    for m in FMT.finditer(text):
        if m.group(0).endswith("%"):
            continue
        conv = m.group(0)[-1]
        found.append((m.group(1) or "", "d" if conv in "dxX" else "f" if conv in "feEgG" else conv))
    return sorted(found)


def main():
    pt, en = load("values"), load("values-en")
    reviewed = json.load(open(REVIEWED, encoding="utf-8")) if os.path.exists(REVIEWED) else {}
    update = "--update-reviewed" in sys.argv
    failed = False
    print(f"pt-BR: {len(pt)} chaves")
    for lang in LANGS:
        cat = load("values-" + lang)
        missing = [k for k in pt if k not in cat]
        extra = [k for k in cat if k not in pt]
        fmt = [k for k in pt if k in cat and args(cat[k]) != args(pt[k])]
        same = []
        if lang != "en":
            for k in pt:
                if k not in cat or not WORD.search(cat[k]):
                    continue
                if cat[k] == pt[k] or (cat[k] == en.get(k) and en.get(k) != pt[k]):
                    same.append(k)
        ok = set(reviewed.get(lang, []))
        unreviewed = [k for k in same if k not in ok]
        if update and lang != "en":
            reviewed[lang] = sorted(same)
            unreviewed = []
        problems = len(missing) + len(extra) + len(fmt) + len(unreviewed)
        if problems and lang in LANGS_REQUIRED:
            failed = True
        print(f"{lang}: {len(cat)} chaves | faltam {len(missing)} | a mais {len(extra)} | formato diferente {len(fmt)} | "
              f"iguais ao pt/en {len(same)} (revisados {len(same) - len(unreviewed)}, NÃO revisados {len(unreviewed)})")
        for label, keys in (("falta", missing), ("a mais", extra), ("formato", fmt), ("não revisado", unreviewed)):
            for k in keys[:15]:
                print(f"    {label}: {k} = {cat.get(k, pt.get(k))!r}")
            if len(keys) > 15:
                print(f"    ... +{len(keys) - 15}")
    if update:
        json.dump(reviewed, open(REVIEWED, "w", encoding="utf-8"), ensure_ascii=False, indent=1, sort_keys=True)
        print("revisados gravados em", REVIEWED)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
