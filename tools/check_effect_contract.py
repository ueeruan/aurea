# -*- coding: utf-8 -*-
"""Contrato da linha de parâmetro de efeito (`bridge::EffectParamRow`).

Omega 0.3.2, área 07 (P0-1): a linha passou de 136 para 160 bytes com a
apresentação resolvida pelo motor (controle, passo, origem, estilo da escolha,
habilitação). Os três lados leem o MESMO mapa de bytes:

  - C++    engine/include/aurea/bridge/BridgePods.hpp  (struct + static_assert)
  - Kotlin android/.../engine/EnginePods.kt           (EffectParam.ROW_BYTES + read())
  - ObjC++ engine/platform/ios/bridge/AureaEngine.mm  (param_row_dict lê os campos)

Este script confere, sem compilar nada, que o tamanho e os deslocamentos
batem nos três lados. Saída 0 = ok; 1 = divergência (mensagens na saída).
Uso: python tools/check_effect_contract.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PODS = ROOT / "engine/include/aurea/bridge/BridgePods.hpp"
KOTLIN = ROOT / "android/app/src/main/java/com/aurea/aurea/engine/EnginePods.kt"
OBJC = ROOT / "engine/platform/ios/bridge/AureaEngine.mm"
SWIFT = ROOT / "engine/platform/ios/app/AureaModel.swift"

# Campos da Omega (área 07) e o deslocamento esperado.
OMEGA_FIELDS = {"control": 136, "step": 140, "origin": 144, "enumStyle": 148, "rowFlags": 152}
ROW_BYTES = 160


def fail(problems, msg):
    problems.append(msg)


def struct_block(text):
    m = re.search(r"struct EffectParamRow \{(.*?)\n\};", text, re.S)
    return m.group(1) if m else ""


def main():
    problems = []
    pods = PODS.read_text(encoding="utf-8")
    block = struct_block(pods)
    if not block:
        fail(problems, "BridgePods.hpp: struct EffectParamRow não encontrada")
    # Deslocamentos anotados "// +NNN" na struct.
    offsets = {}
    for line in block.splitlines():
        m = re.match(r"\s*(?:u32|i32|f32)\s+(\w+)(\[\d+\])?\s*(?:\{\})?\s*(?:=[^;]*)?;\s*//\s*\+(\d+)", line)
        if m:
            offsets[m.group(1)] = int(m.group(3))
    for name, off in OMEGA_FIELDS.items():
        if offsets.get(name) != off:
            fail(problems, f"BridgePods.hpp: {name} deveria estar em +{off} (achei {offsets.get(name)})")
    size = re.search(r"static_assert\(sizeof\(EffectParamRow\) == (\d+)", pods)
    if not size or int(size.group(1)) != ROW_BYTES:
        fail(problems, f"BridgePods.hpp: sizeof(EffectParamRow) deveria ser {ROW_BYTES}")
    for name, off in OMEGA_FIELDS.items():
        if f"offsetof(EffectParamRow, {name}) == {off}" not in pods:
            fail(problems, f"BridgePods.hpp: falta static_assert de offsetof({name}) == {off}")

    kt = KOTLIN.read_text(encoding="utf-8")
    m = re.search(r"const val ROW_BYTES = (\d+)", kt[kt.find("class EffectParam("):])
    if not m or int(m.group(1)) != ROW_BYTES:
        fail(problems, f"EnginePods.kt: EffectParam.ROW_BYTES deveria ser {ROW_BYTES}")
    kotlin_reads = {"control": "control = rows.getInt(b + 136)", "step": "rows.getFloat(b + 140)",
                    "origin": "rows.getFloat(b + 144)", "enumStyle": "enumStyle = rows.getInt(b + 148)",
                    "rowFlags": "rowFlags = rows.getInt(b + 152)"}
    for name, snippet in kotlin_reads.items():
        if snippet not in kt:
            fail(problems, f"EnginePods.kt: leitura de {name} fora do contrato ({snippet!r})")

    mm = OBJC.read_text(encoding="utf-8")
    for name in OMEGA_FIELDS:
        if f"p.{name}" not in mm:
            fail(problems, f"AureaEngine.mm: param_row_dict não lê p.{name}")
    swift = SWIFT.read_text(encoding="utf-8")
    for key in OMEGA_FIELDS:
        if f'row["{key}"]' not in swift:
            fail(problems, f"AureaModel.swift: loadParams não lê row[\"{key}\"]")

    if problems:
        print("check_effect_contract: FALHOU")
        for p in problems:
            print("  -", p)
        return 1
    print(f"check_effect_contract: ok (EffectParamRow = {ROW_BYTES} bytes; C++, Kotlin, ObjC++ e Swift batem)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
