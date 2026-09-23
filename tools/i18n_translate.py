"""Acrescenta traduções aos catálogos de idioma.

Recebe um dicionário `{idioma: {chave: texto}}` e escreve em
`res/values-<idioma>/strings.xml`, escapando o que o `aapt2` exige:

  - `&` e `<` como entidade (senão o XML não fecha);
  - apóstrofo como `\\'` — sem isso o aapt2 recusa o arquivo inteiro com
    "Invalid unicode escape sequence", que não diz qual linha é.

Chave já presente é SUBSTITUÍDA, para corrigir uma tradução sem duplicar.
"""
import os
import re

RES = "android/app/src/main/res"


def escape(text: str) -> str:
    out = text.replace("&", "&amp;").replace("<", "&lt;")
    return out.replace("'", "\\'")


def apply(lang: str, entries: dict) -> int:
    """`lang` = sufixo da pasta (`en`, `es`, `ru`, `hi`, `id`, `ar`)."""
    path = os.path.join(RES, f"values-{lang}", "strings.xml")
    xml = open(path, encoding="utf-8").read()
    added = 0
    for key, text in entries.items():
        value = escape(text)
        line = f'    <string name="{key}">{value}</string>'
        pattern = re.compile(rf'^    <string name="{re.escape(key)}">.*?</string>$',
                             re.M | re.S)
        if pattern.search(xml):
            xml = pattern.sub(lambda _m: line, xml, count=1)
        else:
            xml = xml.replace("</resources>", line + "\n</resources>", 1)
        added += 1
    open(path, "w", encoding="utf-8", newline="\n").write(xml)
    return added


def run(batches: dict) -> None:
    for lang, entries in batches.items():
        n = apply(lang, entries)
        print(f"{lang}: {n} chaves")
