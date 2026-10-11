"""Build a factual APK inventory against a freshly exported Aurea registry.

Matching an import rule is evidence of a conversion path, never proof of
functional or visual equivalence. Outputs contain metadata, not third-party code.
"""
import argparse
import collections
import json
from pathlib import Path
import re


def normalize(value):
    return re.sub(r"[^a-z0-9]", "", value.lower())


def short_name(value):
    return re.sub(r"\d+$", "", normalize(value.rsplit(".", 1)[-1]))


def aliases_match(aliases, value):
    return any((alias[1:] in value if alias.startswith("~") else alias == value)
               for alias in aliases.split("|"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("catalog", type=Path, help="Export from I18n.EffectCatalogHasStableIdsForEveryLabel")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    reference = args.reference
    summary = json.loads((reference / "summary.json").read_text(encoding="utf-8"))
    effects = json.loads((reference / "effects.json").read_text(encoding="utf-8"))
    catalog = json.loads(args.catalog.read_text(encoding="utf-8"))
    catalog_by_key = {e["key"]: e for e in catalog}
    resource_text = (reference / "resources.txt").read_text(encoding="utf-8-sig")
    resources = {}
    for match in re.finditer(r"^    resource (0x[\da-f]+) (\S+) PUBLIC\r?\n(.*?)(?=^    resource |^  type |\Z)", resource_text, re.M | re.S):
        value = re.search(r'^      \(\) "(.*)"$', match[3], re.M)
        resources[match[2]] = {"id": match[1], "default": value[1] if value else ""}
    source = (repo / "engine/src/project/AlightMotion.cpp").read_text(encoding="utf-8")
    header = (repo / "engine/include/aurea/effects/EffectRegistry.hpp").read_text(encoding="utf-8")
    keys = dict(re.findall(r'const char\*\s+(\w+)\s*=\s*"([^"]+)"', header))
    rules_block = source.split("constexpr EffectRule kRules[] = {", 1)[1].split("\n};", 1)[0]
    rules = re.findall(r'\{"([^"]+)", effect_keys::(\w+), (\w+)', rules_block)
    rows = []
    for effect in effects:
        matching = next((r for r in rules if aliases_match(r[0], short_name(effect["id"]))), None)
        name_key = effect.get("name", "").replace("@am:", "")
        controls = [{k: p[k] for k in ("kind", "id", "default", "min", "max", "step", "type") if k in p}
                    for p in effect["controls"]]
        row = {"id": effect["id"], "name": resources.get(name_key, {}).get("default") or effect["id"],
               "reference_path": effect["path"], "tags": [t.strip() for t in effect.get("tags", "").split(",") if t.strip()],
               "controls": controls, "import_rule": None,
               "android_validation": "pending", "ios_validation": "pending", "visual_equivalence": "unverified"}
        if matching:
            key = keys[matching[1]]
            row["import_rule"] = {"aurea_key": key, "registered": key in catalog_by_key,
                                  "aliases": matching[0], "parameter_rules": matching[2]}
        # Exact name is only a search aid. It is deliberately not a parity status.
        row["name_candidates"] = [e["key"] for e in catalog if normalize(e["name"]) == normalize(row["name"])]
        rows.append(row)
    actions = sorted({key.split("/", 1)[1] for key in resources if key.startswith("id/action_")})
    layouts = json.loads((reference / "layouts.json").read_text(encoding="utf-8"))
    result = {"source": {k: summary[k] for k in ("apk_name", "apk_sha256", "entry_count", "uncompressed_bytes")},
              "summary": {"effect_definitions": len(rows), "effect_controls": sum(len(e["controls"]) for e in rows),
                          "aurea_registered_effects": len(catalog), "with_import_rule": sum(e["import_rule"] is not None for e in rows),
                          "without_import_rule": sum(e["import_rule"] is None for e in rows),
                          "action_ids": len(actions), "layout_files": len(layouts)},
              "interpretation": {"without_import_rule": "No match in the current XML effect importer; does not prove the feature is absent.",
                                 "with_import_rule": "Conversion candidate; parameters, defaults, animation and rendered output require review.",
                                 "layout_files": "Includes libraries, alternate resources and internal screens.",
                                 "effect_definitions": "Includes legacy versions, internal definitions and non-user-facing effects."},
              "effects": rows, "action_ids": actions, "layout_files": layouts,
              "aurea_registry": [{"key": e["key"], "name": e["name"], "parameters": [p["id"] for p in e["params"]]} for e in catalog]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result["summary"], ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
