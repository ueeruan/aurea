package com.aurea.aurea.presets

import android.content.res.AssetManager
import org.json.JSONArray

/**
 * Presets que vêm no app: assets/presets/<tipo>.json, uma LISTA de presets no
 * MESMO formato dos salvos pela pessoa (engine/include/aurea/project/Presets.hpp)
 * — o motor lê os dois pelo mesmo caminho, e o teste do motor
 * (Presets.BuiltinAppPresetsAreValid) aplica cada um destes arquivos.
 *
 * Os 11 de animação de texto são os presets nativos do motor (dependem do
 * texto: palavras, letras), então vão pelo número.
 */
class BuiltinPresets(private val assets: AssetManager) {
    /** Mesma ordem de `text::text_preset_name`. */
    private val textNames = listOf(
        "Pop", "Pulo", "Deslizar", "Escala", "Surgir", "Desfoque", "Destaque palavra", "Karaokê", "Máquina de escrever", "Onda", "Elástico",
    )

    private val byKind: Map<PresetKind, List<PresetEntry>> by lazy {
        PresetKind.entries.associateWith { kind ->
            if (kind == PresetKind.Text) {
                textNames.mapIndexed { i, n -> PresetEntry("b:texto:$i", kind, n, builtin = true, textPreset = i) }
            } else {
                load(kind)
            }
        }
    }

    private fun load(kind: PresetKind): List<PresetEntry> {
        val text = runCatching { assets.open("presets/${kind.dir}.json").use { it.readBytes().toString(Charsets.UTF_8) } }.getOrNull()
            ?: return emptyList()
        val arr = runCatching { JSONArray(text) }.getOrNull() ?: return emptyList()
        return (0 until arr.length()).mapNotNull { i ->
            val o = arr.optJSONObject(i) ?: return@mapNotNull null
            PresetEntry("b:${kind.dir}:$i", kind, o.optString("name", "Preset ${i + 1}"), builtin = true, json = o.toString())
        }
    }

    fun of(kind: PresetKind): List<PresetEntry> = byKind[kind].orEmpty()
}
