package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.roundToInt

/**
 * 0.5.2: PREENCHIMENTO COM TEXTURA do texto (`Engine::set_text_texture`). Ligado, as letras
 * levam uma textura procedural do motor (ouro, cromo, madeira, fogo...) em vez da cor lisa.
 * A amostra de cada textura é um degradê das cores dela (o desenho real é da GPU).
 * Mesma ordem e mesmos textos do iOS (`NativeTextTextureSection`).
 */
internal val TEXT_TEXTURE_NAMES = intArrayOf(
    R.string.tex_preset_gold, R.string.tex_preset_silver, R.string.tex_preset_brushed, R.string.tex_preset_wood,
    R.string.tex_preset_marble, R.string.tex_preset_stone, R.string.tex_preset_concrete, R.string.tex_preset_glitter,
    R.string.tex_preset_holo, R.string.tex_preset_neon, R.string.tex_preset_fire, R.string.tex_preset_water,
    R.string.tex_preset_galaxy, R.string.tex_preset_paper, R.string.tex_preset_denim, R.string.tex_preset_leather,
    R.string.tex_preset_rust, R.string.tex_preset_ice,
)

/** Duas cores de amostra por textura (claro → escuro), na ordem de [TEXT_TEXTURE_NAMES]. */
private val TEXT_TEXTURE_SWATCH = listOf(
    0xFFFFEFA8 to 0xFF8A5A10, 0xFFF4F6FA to 0xFF3A3D44, 0xFFC9CCD2 to 0xFF6E7178, 0xFFC8915A to 0xFF6B3E1C,
    0xFFF2F1EE to 0xFF8C8D92, 0xFF9C978E to 0xFF4E4A44, 0xFFB3B0AA to 0xFF6F6D69, 0xFFF1E3F5 to 0xFF8E8A99,
    0xFF9FE6FF to 0xFFE9A2FF, 0xFFFF3DCB to 0xFF2CE6FF, 0xFFFFE07A to 0xFFB31A00, 0xFF8FE3F0 to 0xFF0B4C7A,
    0xFFB25BE0 to 0xFF0A0420, 0xFFF7F3EA to 0xFFD9D3C5, 0xFF5D7BA6 to 0xFF16264A, 0xFF9C6236 to 0xFF4A2610,
    0xFFD58A3E to 0xFF4A1C0A, 0xFFE8F7FF to 0xFF6DB3DD,
).map { (a, b) -> Color(a) to Color(b) }

@Composable
internal fun TextTextureSection(env: PanelEnv) {
    val store = env.store
    val v = store.textTexture ?: return
    if (v.size < 17) return
    val kind = v[0].roundToInt()
    val on = kind in 1..18
    PanelSection(stringResource(R.string.tex_section), com.aurea.aurea.ui.theme.CupertinoGlyph.Paintbrush, "text.sec.texture", startOpen = on,
        trailing = { AureaToggle(checked = on, onCheckedChange = { store.setTextTextureValues(mapOf(0 to if (it) 1f else 0f)) }) }) {
        // Grade de texturas, três por linha.
        TEXT_TEXTURE_NAMES.toList().chunked(3).forEachIndexed { row, names ->
            Row(Modifier.fillMaxWidth().padding(top = 6.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                names.forEachIndexed { col, name ->
                    val n = row * 3 + col + 1
                    TexturePresetCell(stringResource(name), TEXT_TEXTURE_SWATCH[n - 1], selected = kind == n, tag = "text.texture.$n",
                        modifier = Modifier.weight(1f)) { store.setTextTextureValues(mapOf(0 to n.toFloat())) }
                }
                repeat(3 - names.size) { Spacer(Modifier.weight(1f)) }
            }
        }
        if (!on) return@PanelSection
        Spacer(Modifier.height(6.dp))
        val maps = listOf(R.string.tex_map_block, R.string.tex_map_letter)
        ChipRow(stringResource(R.string.tex_mapping), maps.map { stringResource(it) }, v[1].roundToInt()) {
            store.setTextTextureValues(mapOf(1 to it.toFloat()))
        }
        ToggleLine(stringResource(R.string.tex_stroke_too), v[2] > .5f) { store.setTextTextureValues(mapOf(2 to if (it) 1f else 0f)) }
        TextRuler(env, stringResource(R.string.tex_scale), { (store.textTexture?.get(3) ?: 1f) * 100 }, "${(v[3] * 100).roundToInt()}%",
            1f, 5f, 2000f, "escala da textura") { store.setTextTextureValues(mapOf(3 to it / 100)) }
        TextRuler(env, stringResource(R.string.tex_rotation), { store.textTexture?.get(4) ?: 0f }, "${v[4].roundToInt()}°",
            0.5f, -360f, 360f, "rotacao da textura") { store.setTextTextureValues(mapOf(4 to it)) }
        TextRuler(env, stringResource(R.string.tex_offset_x), { (store.textTexture?.get(5) ?: 0f) * 100 }, "${(v[5] * 100).roundToInt()}%",
            1f, -1000f, 1000f, "deslocamento da textura") { store.setTextTextureValues(mapOf(5 to it / 100)) }
        TextRuler(env, stringResource(R.string.tex_offset_y), { (store.textTexture?.get(6) ?: 0f) * 100 }, "${(v[6] * 100).roundToInt()}%",
            1f, -1000f, 1000f, "deslocamento da textura") { store.setTextTextureValues(mapOf(6 to it / 100)) }
        TextRuler(env, stringResource(R.string.tex_contrast), { (store.textTexture?.get(7) ?: 0f) * 100 }, "${(v[7] * 100).roundToInt()}%",
            0.5f, -100f, 100f, "contraste da textura") { store.setTextTextureValues(mapOf(7 to it / 100)) }
        TextRuler(env, stringResource(R.string.tex_brightness), { (store.textTexture?.get(8) ?: 0f) * 100 }, "${(v[8] * 100).roundToInt()}%",
            0.5f, -100f, 100f, "brilho da textura") { store.setTextTextureValues(mapOf(8 to it / 100)) }
        ColorLine(stringResource(R.string.tex_tint), Color(v[9], v[10], v[11])) {
            store.beginGesture("tinta da textura")
            env.openColor(ColorRequest(floatArrayOf(v[9], v[10], v[11], 1f),
                onChange = { r, g, b, _ -> store.setTextTextureValues(mapOf(9 to r, 10 to g, 11 to b, 12 to 1f)) },
                onDone = { store.endGesture() }))
        }
        TextRuler(env, stringResource(R.string.tex_speed), { (store.textTexture?.get(13) ?: 1f) * 100 }, "${(v[13] * 100).roundToInt()}%",
            1f, 0f, 1000f, "velocidade da textura") { store.setTextTextureValues(mapOf(13 to it / 100)) }
        TextRuler(env, stringResource(R.string.tex_relief), { (store.textTexture?.get(14) ?: .5f) * 100 }, "${(v[14] * 100).roundToInt()}%",
            0.5f, 0f, 100f, "relevo da textura") { store.setTextTextureValues(mapOf(14 to it / 100)) }
        TextRuler(env, stringResource(R.string.tex_light_angle), { store.textTexture?.get(15) ?: 135f }, "${v[15].roundToInt()}°",
            0.5f, -360f, 360f, "luz do relevo") { store.setTextTextureValues(mapOf(15 to it)) }
        TextRuler(env, stringResource(R.string.tex_variation), { store.textTexture?.get(16) ?: 0f }, "${v[16].roundToInt()}",
            0.1f, 0f, 9999f, "variacao da textura") { store.setTextTextureValues(mapOf(16 to it.roundToInt().toFloat())) }
    }
}

@Composable
private fun TexturePresetCell(label: String, colors: Pair<Color, Color>, selected: Boolean, tag: String, modifier: Modifier, onClick: () -> Unit) {
    val desc = stringResource(R.string.tex_preset_desc, label)
    Column(
        modifier
            .clip(RoundedCornerShape(8.dp))
            .then(if (selected) Modifier.border(1.5.dp, AureaColors.Accent, RoundedCornerShape(8.dp)) else Modifier)
            .tocavel(onClick = onClick)
            .semantics { contentDescription = desc; role = Role.Button; this.selected = selected }
            .testTag(tag)
            .padding(4.dp),
    ) {
        Box(Modifier.fillMaxWidth().height(34.dp).clip(RoundedCornerShape(6.dp))
            .background(Brush.linearGradient(listOf(colors.first, colors.second))))
        Text(label, maxLines = 1, overflow = TextOverflow.Ellipsis, textAlign = TextAlign.Center,
            modifier = Modifier.fillMaxWidth().padding(top = 3.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = if (selected) AureaColors.Accent else AureaColors.Text)))
    }
}
