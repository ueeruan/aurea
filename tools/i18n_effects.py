"""Catálogo de texto dos EFEITOS nos 7 idiomas (Fase 8.1).

O motor publica nome de efeito, rótulo de parâmetro, opções de lista e unidade
em português — são literais C++ (`EffectInfo::name`, `ParamSpec::label`). A UI
não traduz o TEXTO que chega: traduz pela IDENTIDADE, que não muda:

  - efeito    → a chave estável (`aurea.blur.gaussian`);
  - parâmetro → chave do efeito + id do parâmetro (`aurea.blur.gaussian/blurriness`),
                que chega pela ponte em `EffectParamRow.idOffset`;
  - opções    → a mesma chave, uma entrada por índice da lista;
  - unidade   → o próprio texto ("quadros"), que é curto e fechado.

Se o motor ganhar um efeito ou parâmetro que a tabela ainda não tem, a UI mostra
o texto do motor (português) — nunca um vazio.

Entradas:
  - `tools/effect_catalog.json`: o catálogo do motor, gravado pelo teste
    `I18n.EffectCatalogHasStableIdsForEveryLabel` com `AUREA_CATALOG_JSON=<arq>`;
  - `tools/i18n_effects_kotlin.tsv`: textos do lado Kotlin (rótulos humanos do
    `EffectsHuman`, descrições do `EffectCatalogMeta`, texto das telas de
    efeito), `chave<TAB>pt`;
  - `tools/i18n_effects_tr/<idioma>.json`: `{texto pt: tradução}`.

Saídas (NÃO editar à mão — rodar de novo):
  - `res/values*/strings_effects.xml` (7 idiomas);
  - `effects/EffectStrings.kt` (chave estável → recurso).

Uso: `python tools/i18n_effects.py` (sai 1 se faltar tradução).
"""
import hashlib
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from i18n_build import slug  # noqa: E402

CATALOG = "tools/effect_catalog.json"
KOTLIN_TSV = "tools/i18n_effects_kotlin.tsv"
TR_DIR = "tools/i18n_effects_tr"
RES = "android/app/src/main/res"
KT = "android/app/src/main/java/com/aurea/aurea/effects/EffectStrings.kt"
LANGS = ["en", "es", "ru", "hi", "id", "ar"]
# Unidades que o motor publica e que são PALAVRA (px, %, fps e ° ficam).
ENGINE_UNITS = ["quadros", "por quadro", "oitavas", "stops", "voltas"]


def label_key(text, taken):
    """Rótulo curto: a chave sai do TEXTO — a mesma frase em dois efeitos é um recurso só."""
    base = "fx_" + slug(text)
    if base in taken and taken[base] != text:
        base = base + "_" + hashlib.md5(text.encode("utf-8")).hexdigest()[:4]
    taken[base] = text
    return base


def escape(text):
    out = text.replace("\\", "\\\\").replace("&", "&amp;").replace("<", "&lt;")
    out = out.replace("'", "\\'").replace('"', '\\"')
    if out[:1] in ("@", "?"):
        out = "\\" + out
    return out


def build_pool():
    catalog = json.load(open(CATALOG, encoding="utf-8"))
    taken = {}          # chave -> texto pt
    entries = {}        # chave -> texto pt (ordem de inserção)
    names, params, options, units = {}, {}, {}, {}

    def add(key, text):
        entries.setdefault(key, text)
        return key

    for e in catalog:
        tail = e["key"].split(".", 1)[1].replace(".", "_")
        names[e["key"]] = add(f"fx_name_{tail}", e["name"])
        for p in e["params"]:
            pk = f'{e["key"]}/{p["id"]}'
            params[pk] = add(label_key(p["label"], taken), p["label"])
            if p["enum"]:
                options[pk] = [add(label_key(o, taken), o) for o in p["enum"]]
    for u in ENGINE_UNITS:
        units[u] = add(label_key(u, taken), u)

    # Lado Kotlin: a chave já vem escolhida (é a que o código referencia).
    if os.path.exists(KOTLIN_TSV):
        for line in open(KOTLIN_TSV, encoding="utf-8"):
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            key, text = line.split("\t", 1)
            if key in entries and entries[key] != text:
                sys.exit(f"chave {key} com dois textos: {entries[key]!r} x {text!r}")
            entries[key] = text
    return entries, names, params, options, units


def write_xml(path, header, rows):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    out = ['<?xml version="1.0" encoding="utf-8"?>', header, "<resources>"]
    for key, text in rows:
        out.append(f'    <string name="{key}">{escape(text)}</string>')
    out.append("</resources>")
    open(path, "w", encoding="utf-8", newline="\n").write("\n".join(out) + "\n")


def write_kotlin(names, params, options, units):
    def kv(m):
        return ",\n".join(f'        "{k}" to R.string.{v}' for k, v in m.items())

    def kopt(m):
        return ",\n".join(
            f'        "{k}" to intArrayOf({", ".join("R.string." + x for x in v)})' for k, v in m.items())

    src = f'''package com.aurea.aurea.effects

import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.effectTypeId

// =============================================================================
//  GERADO por tools/i18n_effects.py — não editar à mão.
//
//  O motor publica nome de efeito, rótulo de parâmetro, opção de lista e
//  unidade em português. A tradução é pela IDENTIDADE: chave do efeito e id do
//  parâmetro (que chega pela ponte), nunca pelo texto. Fora da tabela, a UI
//  mostra o texto do motor.
// =============================================================================
internal object EffectStrings {{
    /** Chave estável do efeito → nome. */
    private val names: Map<String, Int> = mapOf(
{kv(names)},
    )

    /** "chave/idDoParametro" → rótulo. */
    private val params: Map<String, Int> = mapOf(
{kv(params)},
    )

    /** "chave/idDoParametro" → rótulo de cada opção, na ordem do motor. */
    private val options: Map<String, IntArray> = mapOf(
{kopt(options)},
    )

    /** Unidade por extenso que o motor publica ("quadros") → rótulo. */
    private val units: Map<String, Int> = mapOf(
{kv(units)},
    )

    /** O `typeId` é o FNV-1a da chave: a tabela é montada uma vez. */
    private val keyOf: Map<Int, String> by lazy {{ names.keys.associateBy {{ effectTypeId(it) }} }}

    fun name(typeId: Int): Int? = keyOf[typeId]?.let {{ names[it] }}

    fun param(typeId: Int, id: String): Int? =
        if (id.isEmpty()) null else keyOf[typeId]?.let {{ params["$it/$id"] }}

    fun options(typeId: Int, id: String): IntArray? =
        if (id.isEmpty()) null else keyOf[typeId]?.let {{ options["$it/$id"] }}

    fun unit(text: String): Int? = units[text]
}}
'''
    open(KT, "w", encoding="utf-8", newline="\n").write(src)


def main():
    entries, names, params, options, units = build_pool()
    rows = list(entries.items())
    write_xml(os.path.join(RES, "values", "strings_effects.xml"),
              "<!-- Efeitos: GERADO por tools/i18n_effects.py (catálogo do motor + tools/i18n_effects_kotlin.tsv). -->",
              rows)
    missing = 0
    for lang in LANGS:
        path = os.path.join(TR_DIR, f"{lang}.json")
        tr = json.load(open(path, encoding="utf-8")) if os.path.exists(path) else {}
        out = []
        for key, pt in rows:
            t = tr.get(pt)
            if not t:
                missing += 1
                print(f"{lang}: falta tradução de {pt!r}")
                continue
            out.append((key, t))
        write_xml(os.path.join(RES, f"values-{lang}", "strings_effects.xml"),
                  f"<!-- Efeitos ({lang}): GERADO por tools/i18n_effects.py. -->", out)
    write_kotlin(names, params, options, units)
    print(f"{len(rows)} recursos de efeito; {len(names)} efeitos, {len(params)} parametros, "
          f"{sum(len(v) for v in options.values())} opcoes, {len(units)} unidades; faltando {missing}")
    sys.exit(1 if missing else 0)


if __name__ == "__main__":
    main()
