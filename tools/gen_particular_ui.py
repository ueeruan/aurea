"""Escreve as strings do painel do Aurea Particular nos 7 catalogos.

Le `tools/particular_strings.json` em vez de ter o texto embutido: assim a
traducao e revisavel sem mexer em codigo, e este script nao precisa de heredoc
com aspas de seis alfabetos dentro.
"""
import io
import json
import os
import re

RES = "android/app/src/main/res"
FOLDER = {"pt": "", "en": "-en", "es": "-es", "ru": "-ru", "hi": "-hi", "id": "-id", "ar": "-ar"}


def escape(text):
    out = text.replace("&", "&amp;").replace("<", "&lt;")
    return out.replace("'", "\\'")


def apply(lang, entries):
    path = os.path.join(RES, "values%s" % FOLDER[lang], "strings.xml")
    xml = io.open(path, encoding="utf-8").read()
    for key, text in entries.items():
        line = '    <string name="%s">%s</string>' % (key, escape(text))
        pat = re.compile(r'^    <string name="%s">.*?</string>$' % re.escape(key), re.M | re.S)
        if pat.search(xml):
            xml = pat.sub(lambda _m: line, xml, count=1)
        else:
            xml = xml.replace("</resources>", line + "\n</resources>", 1)
    io.open(path, "w", encoding="utf-8", newline="\n").write(xml)
    return len(entries)


def main():
    data = json.load(io.open("tools/particular_strings.json", encoding="utf-8"))
    base = len(data["pt"])
    for lang, entries in data.items():
        n = apply(lang, entries)
        assert n == base, "%s tem %d chaves, pt tem %d" % (lang, n, base)
    print("7 catalogos, %d chaves cada" % base)


if __name__ == "__main__":
    main()
