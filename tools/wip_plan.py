"""Lista o que falta de recurso depois de aplicar o rascunho.

Le todos os `R.string.<chave>` do Kotlin, tira os que ja existem nos catalogos e
cruza o que sobra com o mapa recuperado do diff (tools/wip_keys_recovered.json).
O que nao aparecer em nenhum dos dois sai como PENDENTE (texto a mao).

Uso: python tools/wip_plan.py [--write]
"""
import io
import json
import os
import re
import sys

RAIZ = "android/app/src/main/java"
RES = "android/app/src/main/res"
KEY = re.compile(r"R\.string\.([a-z0-9_]+)")


def chaves_do_kotlin():
    out = {}
    for base, _, files in os.walk(RAIZ):
        for f in files:
            if not f.endswith(".kt"):
                continue
            p = os.path.join(base, f)
            for i, line in enumerate(io.open(p, encoding="utf-8").read().splitlines()):
                for k in KEY.findall(line):
                    out.setdefault(k, []).append("%s:%d" % (os.path.relpath(p, RAIZ).replace("\\", "/"), i + 1))
    return out


def catalogo(lang=""):
    p = os.path.join(RES, "values" + lang, "strings.xml")
    if not os.path.exists(p):
        return set()
    return set(re.findall(r'<string name="([a-z0-9_]+)"', io.open(p, encoding="utf-8").read()))


def main():
    kotlin = chaves_do_kotlin()
    existem = catalogo()
    for suf in ("-en", "-es", "-ru", "-hi", "-id", "-ar"):
        existem |= catalogo(suf)
    faltando = sorted(k for k in kotlin if k not in existem)
    rec = json.load(io.open("tools/wip_keys_recovered.json", encoding="utf-8"))
    pend = [k for k in faltando if not rec.get(k)]
    if "--write" in sys.argv:
        json.dump({"faltando": faltando, "pendentes": pend, "usos": {k: kotlin[k] for k in faltando}},
                  io.open("tools/wip_plan.json", "w", encoding="utf-8"), ensure_ascii=False, indent=1)
        print("escrito tools/wip_plan.json")
    print("chaves no Kotlin   : %d" % len(kotlin))
    print("ja nos catalogos   : %d" % (len(kotlin) - len(faltando)))
    print("FALTANDO recurso   : %d" % len(faltando))
    print("  com texto do diff: %d" % (len(faltando) - len(pend)))
    print("  PENDENTES (a mao) : %d" % len(pend))
    for k in pend:
        usos = kotlin[k][:2]
        print("   %-40s %s" % (k, " ".join(usos)))


if __name__ == "__main__":
    main()
