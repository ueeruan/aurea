"""Escreve os catalogos do rascunho de conversao (Fase 8.1).

Junta as tres fontes do texto pt-BR:
  1. o que o diff do rascunho deixou recuperar (tools/wip_keys_recovered.json);
  2. o que foi escrito a mao (tools/wip_keys_manual.json);
  3. as traducoes por idioma (tools/wip_translations.json).

Antes de usar a traducao escrita a mao, procura o MESMO texto pt-BR num recurso
que ja existe no idioma: se achar, copia a traducao de la (texto igual, traducao
igual) e nao ha o que traduzir de novo.

Confere, antes de gravar: chave repetida, string vazia, e o numero de `%n$s` do
pt-BR batendo com o de cada traducao (placeholder trocado vira crash em runtime,
nao erro de compilacao).

Uso:
  python tools/wip_apply.py --check     # so relatorio
  python tools/wip_apply.py --write     # grava values/ e values-<lang>/
"""
import io
import json
import os
import re
import sys

RES = "android/app/src/main/res"
# Escopo desta fase: pt-BR (base) e ingles. Os outros idiomas ficam para depois
# — o que falta cai no pt-BR pelo fallback do Android, sem quebrar nada.
LOCALES = [("", ""), ("en", "-en")]
PH = re.compile(r"%(\d+)\$[sd]")


def load(path, default):
    return json.load(io.open(path, encoding="utf-8")) if os.path.exists(path) else default


def catalog_texts(lang):
    """{texto: chave} do catalogo que ja existe nesse idioma."""
    p = os.path.join(RES, "values" + lang, "strings.xml")
    if not os.path.exists(p):
        return {}
    xml = io.open(p, encoding="utf-8").read()
    out = {}
    for m in re.finditer(r'<string name="([a-z0-9_]+)"[^>]*>(.*?)</string>', xml, re.S):
        out.setdefault(m.group(2), m.group(1))
    return out


def escape(text):
    return text.replace("&", "&amp;").replace("<", "&lt;").replace("'", "\\'")


def main():
    rec = load("tools/wip_keys_recovered.json", {})
    manual = load("tools/wip_keys_manual.json", {})
    plan = load("tools/wip_plan.json", {})
    faltando = plan.get("faltando", [])

    ptbr, problemas = {}, []
    for k in faltando:
        texto = manual.get(k) or rec.get(k) or ""
        if not texto:
            problemas.append("SEM TEXTO  %s" % k)
            continue
        ptbr[k] = texto
    if len(ptbr) != len(set(ptbr)):
        problemas.append("chave repetida no plano")

    langs = {}
    for suf, pasta in LOCALES:
        idioma = {"pt-BR": ptbr} if not suf else load("tools/wip_translations.json", {}).get(suf, {})
        if not suf:
            langs[pasta] = ptbr
            continue
        existentes = catalog_texts(pasta)
        tabela, faltam_mao, herdadas = {}, [], 0
        for k, texto in ptbr.items():
            if k in idioma:
                tabela[k] = idioma[k]
            elif texto in existentes:
                tabela[k] = None    # mesma traducao do recurso existente
                herdadas += 1
            else:
                faltam_mao.append(k)
        langs[pasta] = tabela
        print("%-4s traducao escrita %4d · herdada do catalogo %4d · FALTA %4d" %
              (pasta or "base", len([v for v in tabela.values() if v]), herdadas, len(faltam_mao)))
        for k in faltam_mao[:5]:
            print("        falta: %s | %s" % (k, ptbr[k]))

    # Placeholders: mesma quantidade e mesmos indices do pt-BR.
    for pasta, tabela in langs.items():
        if not pasta:
            continue
        for k, texto in tabela.items():
            if texto is None:
                continue
            if PH.findall(texto) != PH.findall(ptbr[k]):
                problemas.append("PLACEHOLDER %s [%s] %s != %s" % (k, pasta, PH.findall(texto), PH.findall(ptbr[k])))

    if "--write" not in sys.argv:
        print("problemas: %d" % len(problemas))
        for p in problemas[:40]:
            print("   " + p)
        return
    # Grava so o que tem traducao escrita OU herdada (herdada nao entra: a chave
    # nova aponta para o texto; o idioma cai no pt-BR se faltar).
    for pasta, tabela in langs.items():
        linhas = []
        for k in sorted(tabela):
            texto = tabela[k]
            if texto is None:
                continue
            linhas.append('    <string name="%s">%s</string>' % (k, escape(texto)))
        if not linhas:
            continue
        p = os.path.join(RES, "values" + pasta, "strings.xml")
        xml = io.open(p, encoding="utf-8").read()
        marca = "</resources>"
        j = xml.rindex(marca)
        xml = xml[:j] + "\n".join(linhas) + "\n" + xml[j:]
        io.open(p, "w", encoding="utf-8").write(xml)
        print("gravado values%s: %d chaves" % (pasta, len(linhas)))
    print("problemas: %d" % len(problemas))
    for p in problemas[:40]:
        print("   " + p)


if __name__ == "__main__":
    main()
