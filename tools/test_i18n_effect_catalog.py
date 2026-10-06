"""Regressions for effect labels shared by Android and iOS."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

import i18n_effect_catalog as catalog


class EffectCatalogTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.res = self.root / "res"
        for folder in catalog.i18n_add.FOLDERS.values():
            (self.res / folder).mkdir(parents=True)
            (self.res / folder / "strings.xml").write_text("<resources>\n</resources>\n", encoding="utf-8")
        self.human = self.root / "EffectsHuman.kt"
        self.human.write_text("", encoding="utf-8")
        self.texts = self.root / "texts.json"
        self.texts.write_text("{}", encoding="utf-8")
        self.input = self.root / "catalog.json"
        self.outputs = [self.root / name for name in ("table.tsv", "Table.kt", "Table.swift")]
        bindings = dict(RES=str(self.res), HUMAN_KT=str(self.human), TEXTS=str(self.texts),
                        TSV=str(self.outputs[0]), OUT_KT=str(self.outputs[1]), OUT_SWIFT=str(self.outputs[2]))
        self.addCleanup(patch.stopall)
        patch.multiple(catalog, **bindings).start()
        patch.object(catalog.i18n_add, "RES", str(self.res)).start()
        patch.object(catalog.sys, "argv", ["generator", str(self.input)]).start()

    def strings(self, lang, name, entries):
        path = self.res / catalog.i18n_add.FOLDERS[lang] / name
        root = ET.Element("resources")
        for key, value in entries.items():
            ET.SubElement(root, "string", name=key).text = value
        path.write_text(ET.tostring(root, encoding="unicode"), encoding="utf-8")
        return path

    def source(self, label="Novo", options=()):
        self.input.write_text(json.dumps([{"key": "aurea.test", "params": [
            {"index": 0, "id": "amount", "label": label, "hidden": False, "enum": list(options)}
        ]}], ensure_ascii=False), encoding="utf-8")

    def run_generator(self):
        with contextlib.redirect_stdout(io.StringIO()):
            catalog.main()

    def test_split_resources_preserve_hand_labels_and_reuse_enum_without_rewrite(self):
        self.human.write_text('put("aurea.test", mapOf(0 to ParamHuman(label = R.string.fx3_amount)))', encoding="utf-8")
        originals = {}
        for lang in catalog.i18n_add.FOLDERS:
            path = self.strings(lang, "strings_area.xml", {"fx3_amount": "Valor", "fx3o_repeat": "Repetir"})
            originals[path] = path.read_bytes()
        self.source("Rótulo do motor", ["Repetir"])
        self.run_generator()
        tsv = self.outputs[0].read_text(encoding="utf-8")
        self.assertIn("aurea.test\t0\tamount\t-1\tfx3_amount", tsv)
        self.assertIn("aurea.test\t0\tamount\t0\tfx3o_repeat", tsv)
        self.assertIn("R.string.fx3_amount, R.string.fx3o_repeat", self.outputs[1].read_text(encoding="utf-8"))
        self.assertIn("aurea.test|0|amount|fx3_amount|fx3o_repeat", self.outputs[2].read_text(encoding="utf-8"))
        self.assertTrue(all(path.read_bytes() == data for path, data in originals.items()))

    def test_duplicate_key_across_catalog_parts_is_rejected(self):
        self.strings("pt", "strings.xml", {"fxl_new": "Novo"})
        self.strings("pt", "strings_area.xml", {"fxl_new": "Outro"})
        with self.assertRaisesRegex(ValueError, "duplicado"):
            catalog.load_strings("values")

    def test_existing_portuguese_key_gets_missing_languages_without_duplicates(self):
        self.source()
        self.strings("pt", "strings_area.xml", {"fxl_new": "Novo"})
        spanish = self.strings("es", "strings_area.xml", {"fxl_new": "Traducción revisada"})
        prior_spanish = spanish.read_bytes()
        self.texts.write_text(json.dumps({"Novo": {lang: "New" for lang in catalog.LANGS}}), encoding="utf-8")
        self.run_generator()
        self.assertEqual(catalog.load_strings("values")["fxl_new"], "Novo")
        self.assertNotIn("fxl_new", (self.res / "values" / "strings.xml").read_text(encoding="utf-8"))
        self.assertEqual(catalog.load_strings("values-en")["fxl_new"], "New")
        self.assertEqual(spanish.read_bytes(), prior_spanish)
        self.assertEqual(catalog.load_strings("values-es")["fxl_new"], "Traducción revisada")

    def test_generated_tables_and_resources_are_idempotent(self):
        self.source()
        self.texts.write_text(json.dumps({"Novo": {lang: "New" for lang in catalog.LANGS}}), encoding="utf-8")
        self.run_generator()
        files = self.outputs + list(self.res.rglob("*.xml"))
        before = {path: path.read_bytes() for path in files}
        self.run_generator()
        self.assertTrue(all(path.read_bytes() == data for path, data in before.items()))


if __name__ == "__main__":
    unittest.main()
