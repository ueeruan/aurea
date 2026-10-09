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
    "Preset Entrada rápida", "Preset Salto elástico", "Preset Salto por palavra", "Preset Movimento suave", "Onda de contorno", "Digitação reversa", "Palavras em sequência", "Cintilação", "Legenda suave", "Legenda luminosa")

/** Nomes 0..7 de [ExtraTextPresetNames] no catálogo (o texto em pt acima é só referência). */
internal val ExtraTextPresetLabels = listOf(R.string.app_text_preset_bounce, R.string.app_text_preset_soft_in, R.string.app_text_preset_reveal,
    R.string.app_text_preset_slide, R.string.app_text_preset_fast_in, R.string.app_text_preset_elastic, R.string.app_text_preset_word_jump,
    R.string.app_text_preset_smooth)

internal val PackTextPresetLabels = listOf(R.string.pack_text_0, R.string.pack_text_1, R.string.pack_text_2, R.string.pack_text_3, R.string.pack_text_4, R.string.pack_text_5)

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
            PresetEntry("b:texto:${i + 11}", PresetKind.Text, if (i >= 8) AppText.get(context, PackTextPresetLabels[i - 8]) else ExtraTextPresetLabels.getOrNull(i)?.let { res.getString(it) } ?: name, builtin = true, textPreset = i + 11)
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

    /** Nome no idioma ATUAL do app (o JSON fala pt-BR); sem recurso, o do arquivo. */
    fun of(kind: PresetKind): List<PresetEntry> = if (kind == PresetKind.Text) textEntries() else {
        val names = BuiltinPresetNames[kind].orEmpty()
        byKind[kind].orEmpty().mapIndexed { i, e -> if (i < names.size) e.copy(name = AppText.get(context, names[i])) else e }
    }
}

/**
 * Nome traduzido dos presets embutidos (assets/presets/<tipo>.json), na ordem
 * do arquivo — mesma tabela do iOS (`builtinPresetNameKeys`). Os de marca
 * ("CC · Detail", "Omino · Diffusion") ficam com o nome do arquivo.
 */
internal val BuiltinPresetNames: Map<PresetKind, List<Int>> = mapOf(
    PresetKind.Animation to listOf(R.string.app_preset_anim_appear, R.string.app_preset_anim_vanish, R.string.app_preset_anim_enter_left,
        R.string.app_preset_anim_rise, R.string.app_preset_anim_zoom_in, R.string.app_preset_anim_pulse, R.string.app_preset_anim_spin_in),
    PresetKind.Curve to listOf(R.string.app_preset_curve_cubic, R.string.app_preset_curve_strong_inout, R.string.app_preset_curve_expo_out,
        R.string.app_preset_curve_expo_in, R.string.app_preset_curve_back_out, R.string.app_preset_curve_back_in),
    PresetKind.Effects to listOf(R.string.app_preset_fx_soft_blur, R.string.app_preset_fx_focus_in, R.string.app_preset_fx_neon_glow,
        R.string.app_preset_fx_bw, R.string.app_preset_fx_strong_contrast, R.string.app_preset_fx_sepia, R.string.app_preset_fx_dream,
        R.string.app_preset_fx_turbulence),
    PresetKind.Caption to listOf(R.string.app_preset_caption_viral, R.string.app_preset_caption_karaoke, R.string.app_preset_caption_subtle,
        R.string.pack_text_4, R.string.pack_text_5),
)
