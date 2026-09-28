"""Acrescenta (ou corrige) strings nos SETE catálogos de uma vez.

Entrada: um JSON `{chave: {"pt": ..., "en": ..., "es": ..., "ru": ..., "hi": ...,
"id": ..., "ar": ...}}` com texto CRU (sem escape de XML/aapt2). Idioma ausente
no objeto = não mexe naquele catálogo.

Cada arquivo é relido imediatamente antes de ser gravado e só as linhas das
chaves dadas mudam — outras pessoas acrescentando strings ao mesmo tempo não
perdem nada. Chave já presente é SUBSTITUÍDA no lugar; nova entra no fim.

Uso: `python tools/i18n_add.py novos.json [--only-missing]`
  --only-missing  não substitui chave que o catálogo já tem.
"""
import json
import os
import re
import sys

RES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "android", "app", "src", "main", "res")
FOLDERS = {"pt": "values", "en": "values-en", "es": "values-es", "ru": "values-ru",
           "hi": "values-hi", "id": "values-id", "ar": "values-ar"}


def escape(text: str) -> str:
    out = text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    out = re.sub(r"(?<!\\)'", r"\\'", out)
    out = re.sub(r'(?<!\\)"', r'\\"', out)
    out = out.replace("\n", "\\n")
    if out[:1] in ("@", "?"):
        out = "\\" + out
    return out


def apply(lang: str, entries: dict, only_missing: bool) -> int:
    path = os.path.join(RES, FOLDERS[lang], "strings.xml")
    with open(path, encoding="utf-8") as f:
        xml = f.read()
    changed = 0
    for key, text in entries.items():
        line = f'    <string name="{key}">{escape(text)}</string>'
        pattern = re.compile(rf'^[ \t]*<string name="{re.escape(key)}"(?: [^>]*)?>.*?</string>[ \t]*$', re.M | re.S)
        if pattern.search(xml):
            if only_missing:
                continue
            xml = pattern.sub(lambda _m: line, xml, count=1)
        else:
            i = xml.rfind("</resources>")
            xml = xml[:i] + line + "\n" + xml[i:]
        changed += 1
    if changed:
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(xml)
    return changed


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    only_missing = "--only-missing" in sys.argv
    data = json.load(open(args[0], encoding="utf-8"))
    per_lang = {lang: {} for lang in FOLDERS}
    for key, texts in data.items():
        if not re.fullmatch(r"[a-z0-9_]+", key):
            sys.exit(f"chave invalida: {key}")
        for lang, text in texts.items():
            if lang not in FOLDERS:
                sys.exit(f"idioma desconhecido em {key}: {lang}")
            per_lang[lang][key] = text
    for lang, entries in per_lang.items():
        if entries:
            print(f"{lang}: {apply(lang, entries, only_missing)} gravada(s)")


if __name__ == "__main__":
    main()
