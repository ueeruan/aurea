"""Confere os catálogos de idioma (Fase 8.1) sem precisar do gradle.

Para cada arquivo `values/strings*.xml`:
  - o XML abre (o aapt2 recusa o arquivo inteiro por um `&` solto);
  - apóstrofo sem `\\'` e aspas sem `\\"` (o aapt2 recusa ou APAGA o caractere);
  - toda chave traduzível do padrão existe nos 6 idiomas, e nenhum idioma tem
    chave que o padrão não tem;
  - os argumentos de formato (`%1$s`, `%d`...) são OS MESMOS em todos — um `%s`
    a mais no árabe é crash na hora de formatar, não texto errado;
  - `plurals` existe nos 7, com `other` sempre.

Uso: `python tools/i18n_check.py` — sai com código 1 se algo falhar.
"""
import glob
import os
import re
import sys
import xml.etree.ElementTree as ET

RES = "android/app/src/main/res"
LANGS = ["en", "es", "ru", "hi", "id", "ar"]
FMT = re.compile(r"%(\d+\$)?[-#+ 0,(]*\d*(\.\d+)?[sdfxXeEgGc%]")


def args_of(text):
    out = []
    for m in FMT.finditer(text or ""):
        tok = m.group(0)
        if tok.endswith("%"):
            continue
        conv = tok[-1]
        conv = "d" if conv in "dxX" else ("f" if conv in "feEgG" else conv)
        out.append((m.group(1) or "", conv))
    return sorted(out)


def raw_texts(path):
    """Texto CRU de cada <string>, para achar apóstrofo sem escape."""
    src = open(path, encoding="utf-8").read()
    return re.findall(r'<string name="([^"]+)"[^>]*>(.*?)</string>', src, re.S)


def load(path):
    tree = ET.parse(path)
    strings, plurals, fixed = {}, {}, set()
    for el in tree.getroot():
        name = el.get("name")
        if el.tag == "string":
            if el.get("translatable") == "false":
                fixed.add(name)
            strings[name] = "".join(el.itertext())
        elif el.tag == "plurals":
            plurals[name] = {i.get("quantity"): "".join(i.itertext()) for i in el}
    return strings, plurals, fixed


def main():
    errors = []
    base_files = sorted(glob.glob(os.path.join(RES, "values", "strings*.xml")))
    total = {}
    for base in base_files:
        fname = os.path.basename(base)
        try:
            b_str, b_pl, b_fixed = load(base)
        except ET.ParseError as e:
            errors.append(f"{base}: XML invalido: {e}")
            continue
        for name, raw in raw_texts(base):
            if re.search(r"(?<!\\)'", raw):
                errors.append(f"{base}: {name}: apostrofo sem escape")
        # Chaves que ficam iguais em todo idioma (unidade, formato puro).
        want = {k for k in b_str if k not in b_fixed}
        total.setdefault("pt", 0)
        total["pt"] += len(b_str) + len(b_pl)
        for lang in LANGS:
            path = os.path.join(RES, f"values-{lang}", fname)
            if not os.path.exists(path):
                errors.append(f"{path}: falta o arquivo")
                continue
            try:
                l_str, l_pl, _ = load(path)
            except ET.ParseError as e:
                errors.append(f"{path}: XML invalido: {e}")
                continue
            for name, raw in raw_texts(path):
                if re.search(r"(?<!\\)'", raw):
                    errors.append(f"{path}: {name}: apostrofo sem escape")
            total.setdefault(lang, 0)
            total[lang] += len(l_str) + len(l_pl)
            for k in sorted(set(l_str) - set(b_str)):
                errors.append(f"{path}: chave que o padrao nao tem: {k}")
            for k in sorted(want):
                if k not in l_str:
                    # Unidade pura ("%1$d px") pode faltar de proposito: cai no padrao.
                    if re.fullmatch(r"[\s%0-9$sdf.,:·+\-−×]*(px|fps|ms|MB|GB|s|x|%%)?\s*", b_str[k] or ""):
                        continue
                    errors.append(f"{path}: falta {k}")
                elif args_of(l_str[k]) != args_of(b_str[k]):
                    errors.append(f"{path}: {k}: argumentos {args_of(l_str[k])} != {args_of(b_str[k])}")
            for k, forms in b_pl.items():
                if k not in l_pl:
                    errors.append(f"{path}: falta plurals {k}")
                    continue
                if "other" not in l_pl[k]:
                    errors.append(f"{path}: plurals {k} sem 'other'")
                base_args = args_of(forms.get("other", ""))
                for q, t in l_pl[k].items():
                    a = args_of(t)
                    # Forma sem número ("مشروع واحد") é permitida; número a mais não.
                    if a and a != base_args:
                        errors.append(f"{path}: plurals {k}[{q}]: argumentos {a} != {base_args}")
    for e in errors:
        print(e)
    print("chaves por idioma:", ", ".join(f"{k}={v}" for k, v in sorted(total.items())))
    print(f"{len(errors)} problema(s)")
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
