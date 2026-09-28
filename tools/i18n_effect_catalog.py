"""Tradução dos textos que o MOTOR publica nos efeitos: rótulo de parâmetro e
opções de lista (enum). O motor fala pt-BR e continua assim; a interface traduz
pela IDENTIDADE — chave do efeito + id do parâmetro (+ índice da opção) — e só
cai no texto do motor se a identidade não estiver na tabela.

Entrada:
  - o catálogo do motor (JSON gravado pelo teste
    `I18n.EffectCatalogHasStableIdsForEveryLabel` com `AUREA_CATALOG_JSON=<arquivo>`);
  - `tools/effect_i18n_texts.json`: {texto pt: {en, es, ru, hi, id, ar}} para os
    textos que ainda não têm recurso;
  - os catálogos do Android e a tabela humana (`EffectsHuman.kt`), cujos rótulos
    escritos à mão têm precedência e são reaproveitados.

Saída (tudo gerado, não editar à mão):
  - `tools/effect_i18n.tsv` — chave, índice, id, opção (-1 = rótulo), recurso.
    É o que o teste do motor `I18n.EveryEffectTextHasEnglish` confere;
  - `android/.../effects/EffectI18nTable.kt` e `engine/platform/ios/app/EffectI18nTable.swift`;
  - recursos `fxl_*` (rótulo) e `fxo_*` (opção) novos nos 7 catálogos.

Uso: `python tools/i18n_effect_catalog.py <catalogo.json>`
     `python tools/i18n_effect_catalog.py <catalogo.json> --todo`  (lista textos sem tradução)
"""
import json
import os
import re
import sys
import unicodedata
import xml.etree.ElementTree as ET

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import i18n_add  # noqa: E402

RES = os.path.join(ROOT, "android", "app", "src", "main", "res")
HUMAN_KT = os.path.join(ROOT, "android/app/src/main/java/com/aurea/aurea/editor/panels/EffectsHuman.kt")
TEXTS = os.path.join(ROOT, "tools", "effect_i18n_texts.json")
TSV = os.path.join(ROOT, "tools", "effect_i18n.tsv")
OUT_KT = os.path.join(ROOT, "android/app/src/main/java/com/aurea/aurea/effects/EffectI18nTable.kt")
OUT_SWIFT = os.path.join(ROOT, "engine/platform/ios/app/EffectI18nTable.swift")
LANGS = ["en", "es", "ru", "hi", "id", "ar"]
REUSE_PREFIXES = ("fx_", "afx_", "fxl_", "fxo_")


def load_strings(folder):
    out = {}
    for el in ET.parse(os.path.join(RES, folder, "strings.xml")).getroot():
        if el.tag == "string":
            text = "".join(el.itertext())
            out[el.get("name")] = text.replace("\\'", "'").replace('\\"', '"')
    return out


def hand_labels():
    """{chave: {índice: recurso}} dos rótulos escritos à mão em EffectsHuman.kt."""
    src = open(HUMAN_KT, encoding="utf-8").read()
    matrix = re.search(r"MatrixLabels = listOf\((.*?)\)", src, re.S)
    matrix = re.findall(r"R\.string\.(\w+)", matrix.group(1)) if matrix else []
    parts = re.split(r'put\(\s*"(aurea\.[a-z0-9_.]+)"', src)
    out = {}
    for i in range(1, len(parts), 2):
        body = parts[i + 1]
        m = {int(a): b for a, b in re.findall(r"(\d+) to ParamHuman\(label = R\.string\.(\w+)", body)}
        if parts[i] == "aurea.color.matrix":
            m.update({k: v for k, v in enumerate(matrix)})
        out.setdefault(parts[i], {}).update(m)
    return out


def slug(text, limit=40):
    s = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode().lower()
    s = re.sub(r"[^a-z0-9]+", "_", s).strip("_")
    return (s[:limit].rstrip("_")) or "x"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    todo_only = "--todo" in sys.argv
    catalog = json.load(open(args[0], encoding="utf-8"))
    texts = json.load(open(TEXTS, encoding="utf-8")) if os.path.exists(TEXTS) else {}
    pt = load_strings("values")
    en = load_strings("values-en")
    hand = hand_labels()

    by_pt = {}
    for key in sorted(pt, key=lambda k: (not k.startswith(("fxl_", "fxo_")), len(k), k)):
        if key.startswith(REUSE_PREFIXES) and key in en:
            by_pt.setdefault(pt[key].strip(), key)

    rows, new, todo = [], {}, []

    def resource(text, prefix):
        text = text.strip()
        if text in by_pt:
            return by_pt[text]
        tr = texts.get(text)
        if not tr or not tr.get("en"):
            todo.append(text)
            return None
        # Texto do motor em inglês pode ter um pt próprio ("Seed" → "Semente").
        want = tr.get("pt", text)
        base = prefix + slug(tr["en"])
        name, n = base, 2
        while (name in pt and pt[name].strip() != want) or (name in new and new[name]["pt"] != want):
            name, n = f"{base}_{n}", n + 1
        if name not in pt:
            new[name] = {"pt": want, **{l: tr[l] for l in LANGS if tr.get(l)}}
        by_pt[text] = name
        return name

    for e in catalog:
        for p in e["params"]:
            if p["hidden"]:
                continue
            res = hand.get(e["key"], {}).get(p["index"])
            if res is None or res not in en:
                res = resource(p["label"], "fxl_")
            rows.append((e["key"], p["index"], p["id"], -1, res))
            for k, opt in enumerate(p["enum"]):
                rows.append((e["key"], p["index"], p["id"], k, resource(opt, "fxo_")))

    if todo or todo_only:
        for t in dict.fromkeys(todo):
            print(t)
        print(f"{len(set(todo))} texto(s) sem tradução em {os.path.relpath(TEXTS, ROOT)}", file=sys.stderr)
        sys.exit(1 if todo else 0)

    if new:
        per_lang = {}
        for name, tr in new.items():
            for lang, text in tr.items():
                per_lang.setdefault(lang, {})[name] = text
        for lang, entries in per_lang.items():
            i18n_add.apply(lang, entries, only_missing=True)
        print(f"{len(new)} recurso(s) novo(s)")

    with open(TSV, "w", encoding="utf-8", newline="\n") as f:
        f.write("# GERADO por tools/i18n_effect_catalog.py. chave\tindice\tid\topcao(-1=rotulo)\trecurso\n")
        for r in rows:
            f.write("\t".join(str(x) for x in r) + "\n")
    write_kotlin(rows)
    write_swift(rows)
    print(f"{len(rows)} linha(s): {sum(1 for r in rows if r[3] < 0)} rótulos, {sum(1 for r in rows if r[3] >= 0)} opções")


def grouped(rows):
    """[(chave, índice, id, rótulo, [opções])] na ordem do catálogo."""
    out, cur = [], None
    for key, index, pid, opt, res in rows:
        if opt < 0:
            cur = [key, index, pid, res, []]
            out.append(cur)
        else:
            cur[4].append(res)
    return out


def write_kotlin(rows):
    groups = grouped(rows)
    lines = [
        "// GERADO por tools/i18n_effect_catalog.py a partir do catálogo do motor — não editar.",
        "// Rótulo e opções de cada parâmetro de efeito pela IDENTIDADE (chave + id do parâmetro).",
        "package com.aurea.aurea.effects",
        "",
        "import com.aurea.aurea.R",
        "",
        "internal object EffectI18nTable {",
        "    /** (typeId << 16 | índice do parâmetro) → [rótulo, opção 0, opção 1, ...] (recursos). */",
        "    val entries: Map<Long, IntArray> by lazy {",
        "        HashMap<Long, IntArray>(" + str(len(groups) * 2) + ").apply {",
    ]
    chunks = [groups[i:i + 120] for i in range(0, len(groups), 120)]
    for n in range(len(chunks)):
        lines.append(f"            part{n}(this)")
    lines += ["        }", "    }", ""]
    for n, chunk in enumerate(chunks):
        lines.append(f"    private fun part{n}(m: HashMap<Long, IntArray>) {{")
        for key, index, pid, label, opts in chunk:
            ids = ", ".join(f"R.string.{r}" for r in [label] + opts)
            lines.append(f'        m.put("{key}", {index}, /* {pid} */ intArrayOf({ids}))')
        lines += ["    }", ""]
    lines += [
        "    private fun HashMap<Long, IntArray>.put(key: String, index: Int, ids: IntArray) {",
        "        put(slot(com.aurea.aurea.editor.panels.effectTypeId(key), index), ids)",
        "    }",
        "",
        "    fun slot(typeId: Int, index: Int): Long = (typeId.toLong() shl 16) or (index.toLong() and 0xFFFF)",
        "}",
        "",
    ]
    open(OUT_KT, "w", encoding="utf-8", newline="\n").write("\n".join(lines))


def write_swift(rows):
    groups = grouped(rows)
    lines = [
        "// GERADO por tools/i18n_effect_catalog.py a partir do catálogo do motor — não editar.",
        "// Rótulo e opções de cada parâmetro de efeito pela IDENTIDADE (chave + id do parâmetro).",
        "// Uma linha por parâmetro: chave|índice|id|rótulo|opção 0,opção 1,...",
        "import Foundation",
        "",
        "enum EffectI18nTable {",
        "    /// \"typeId#índice\" → (rótulo, opções) como chaves do AureaText.",
        "    static let entries: [String: (label: String, options: [String])] = {",
        "        var out: [String: (label: String, options: [String])] = [:]",
        "        for line in table.split(separator: \"\\n\") {",
        "            let f = line.split(separator: \"|\", omittingEmptySubsequences: false).map(String.init)",
        "            guard f.count == 5, let index = Int(f[1]) else { continue }",
        "            let options = f[4].isEmpty ? [] : f[4].split(separator: \",\").map(String.init)",
        "            out[\"\\(fxEffectTypeId(f[0]))#\\(index)\"] = (f[3], options)",
        "        }",
        "        return out",
        "    }()",
        "",
        "    private static let table = \"\"\"",
    ]
    for key, index, pid, label, opts in groups:
        lines.append(f"    {key}|{index}|{pid}|{label}|{','.join(opts)}")
    lines += ['    """', "}", ""]
    open(OUT_SWIFT, "w", encoding="utf-8", newline="\n").write("\n".join(lines))


if __name__ == "__main__":
    main()
