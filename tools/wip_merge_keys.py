"""Junta o rascunho de conversao com os catalogos que ja existem.

Para cada chave do rascunho:
  - se o TEXTO pt-BR ja existe como recurso (sob outra chave), a chave do
    rascunho e trocada pela existente no Kotlin — texto ja traduzido nos 7
    idiomas, sem chave nova e sem traducao repetida;
  - se nao existe, entra na lista de chaves novas (com o texto a traduzir).

Uso: python tools/wip_merge_keys.py [--write]
"""
import io
import json
import os
import re
import subprocess
import sys

RES = "android/app/src/main/res"
LOCALES = ["", "-en", "-es", "-ru", "-hi", "-id", "-ar"]
SRC = "android/app/src/main/java/com/aurea/aurea"


def recovered():
    out = subprocess.run([sys.executable, "tools/wip_recover_keys.py", "--json"],
                         capture_output=True, text=True, encoding="utf-8")
    return json.loads(out.stdout)


def catalog(path):
    xml = io.open(path, encoding="utf-8").read() if os.path.exists(path) else ""
    out = {}
    for m in re.finditer(r'<string name="([a-z0-9_]+)"[^>]*>(.*?)</string>', xml, re.S):
        out[m.group(1)] = m.group(2)
    return out


def main():
    rec = recovered()
    base = catalog(os.path.join(RES, "values", "strings.xml"))
    by_text = {}
    for key, text in base.items():
        by_text.setdefault(text, key)
    # Só vale reusar se a chave existente está traduzida em TODOS os idiomas.
    completo = set(base)
    for suf in LOCALES[1:]:
        completo &= set(catalog(os.path.join(RES, "values" + suf, "strings.xml")))

    reuse, fresh, blank = {}, {}, []
    for key, text in sorted(rec.items()):
        if not text:
            blank.append(key)
            continue
        existing = by_text.get(text)
        if existing and existing in completo and existing != key:
            reuse[key] = (existing, text)
        elif existing and existing == key:
            reuse[key] = (existing, text)
        else:
            fresh[key] = text

    if "--write" in sys.argv:
        json.dump({"reuse": reuse, "fresh": fresh, "blank": blank},
                  io.open("tools/wip_keys.json", "w", encoding="utf-8"),
                  ensure_ascii=False, indent=1)
        print("escrito tools/wip_keys.json")
        return
    print("chaves no rascunho : %d" % len(rec))
    print("reusam recurso     : %d" % len(reuse))
    print("chaves novas       : %d" % len(fresh))
    print("sem texto          : %d" % len(blank))
    print("--- sem texto ---")
    for k in blank:
        print("  " + k)


if __name__ == "__main__":
    main()
