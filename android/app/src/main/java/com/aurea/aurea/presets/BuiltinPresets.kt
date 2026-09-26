package com.aurea.aurea.presets

import android.content.Context
import android.content.res.AssetManager
import android.content.res.Resources
import com.aurea.aurea.R
import com.aurea.aurea.ui.i18n.AppText
import org.json.JSONArray

/**
 * Nomes dos 11 presets de animação de texto do motor, na ordem de
 * `text::text_preset_name` (o índice é o que vai para o motor).
 */
internal val ExtraTextPresetNames = listOf("Preset Bounce", "Preset Entrada suave", "Preset Revelar", "Preset Deslizar",
    "Preset Entrada rápida", "Preset Salto elástico", "Preset Salto por palavra", "Preset Movimento suave")

internal val TextPresetNames = listOf(
    R.string.pn_pop, R.string.pn_textpreset_bounce, R.string.pn_textpreset_slide, R.string.panel_escala,
    R.string.pn_textpreset_appear, R.string.panel_desfoque, R.string.pn_textpreset_word_highlight, R.string.pn_karaoke,
    R.string.pn_textpreset_typewriter, R.string.pn_textpreset_wave, R.string.pn_textpreset_elastic,
)

/**
 * Presets que vêm no app: assets/presets/<tipo>.json, uma LISTA de presets no
 * MESMO formato dos salvos pela pessoa (engine/include/aurea/project/Presets.hpp)
 * — o motor lê os dois pelo mesmo caminho, e o teste do motor
 * (Presets.BuiltinAppPresetsAreValid) aplica cada um destes arquivos.
 *
 * Os 11 de animação de texto são os presets nativos do motor (dependem do
 * texto: palavras, letras), então vão pelo número.
 */
class BuiltinPresets(private val context: Context) {
    private val assets: AssetManager = context.assets

    private val byKind: Map<PresetKind, List<PresetEntry>> by lazy {
        PresetKind.entries.filter { it != PresetKind.Text }.associateWith { load(it) }
    }

    /** Os de texto levam o nome no idioma do app: refeitos só quando o idioma muda. */
    @Volatile private var text: Pair<Resources, List<PresetEntry>>? = null

    private fun textEntries(): List<PresetEntry> {
        val res = AppText.resources(context)
        text?.let { (r, list) -> if (r === res) return list }
        val list = TextPresetNames.mapIndexed { i, id ->
            PresetEntry("b:texto:$i", PresetKind.Text, res.getString(id), builtin = true, textPreset = i)
        } + ExtraTextPresetNames.mapIndexed { i, name ->
            PresetEntry("b:texto:${i + 11}", PresetKind.Text, name, builtin = true, textPreset = i + 11)
        }
        text = res to list
        return list
    }

    private fun load(kind: PresetKind): List<PresetEntry> {
        val text = runCatching { assets.open("presets/${kind.dir}.json").use { it.readBytes().toString(Charsets.UTF_8) } }.getOrNull()
            ?: return emptyList()
        val arr = runCatching { JSONArray(text) }.getOrNull() ?: return emptyList()
        return (0 until arr.length()).mapNotNull { i ->
            val o = arr.optJSONObject(i) ?: return@mapNotNull null
            val name = o.optString("name").ifEmpty { AppText.get(context, R.string.pn_preset_n, i + 1) }
            PresetEntry("b:${kind.dir}:$i", kind, name, builtin = true, json = o.toString())
        }
    }

    fun of(kind: PresetKind): List<PresetEntry> = if (kind == PresetKind.Text) textEntries() else byKind[kind].orEmpty()
}
