package com.aurea.aurea.editor.panels

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.graphics.drawscope.scale
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.presets.PresetEntry
import com.aurea.aurea.presets.PresetKind
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaAlert
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import org.json.JSONObject
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

/** Abas do navegador: favoritos, recentes e uma por tipo de preset. */
private enum class PresetTab(val label: String, val kind: PresetKind?) {
    Favorites("★ Favoritos", null),
    Recents("Recentes", null),
    Animation("Animação", PresetKind.Animation),
    Effects("Efeitos", PresetKind.Effects),
    Text("Texto", PresetKind.Text),
    Caption("Legenda", PresetKind.Caption),
    Curve("Curva", PresetKind.Curve),
}

/**
 * PRESETS: uma grade de cartões (prévia + nome) — tocar aplica (um passo de
 * desfazer). Em cima, a busca e as abas (favoritos, recentes e os tipos que
 * valem para ESTA camada: texto só em texto, legenda só onde há fala). O último
 * cartão guarda o que está na camada como preset novo.
 *
 * A PRÉVIA sai do próprio preset, nada de imagem inventada: animação anda um
 * quadradinho com os keyframes de verdade (posição na proporção da composição,
 * escala, giro, opacidade e as curvas), curva desenha a curva e passa um ponto
 * nela, legenda monta a linha com as opções dela (palavras, maiúsculas, destaque,
 * altura), efeito e texto mostram o ícone da categoria.
 */
@Composable
internal fun PresetsPanel(env: PanelEnv) {
    val store = env.store
    val lib = store.presets
    val kind = store.detail?.kind
    val isText = kind == LayerType.Text.kind
    val speaks = kind == LayerType.Video.kind || kind == LayerType.Audio.kind || isText
    val tabs = PresetTab.entries.filter { t ->
        when (t.kind) {
            PresetKind.Text -> isText
            PresetKind.Caption -> speaks
            else -> true
        }
    }
    var picked by rememberSaveable { mutableStateOf(if (isText) PresetTab.Text else PresetTab.Animation) }
    val tab = if (picked in tabs) picked else PresetTab.Animation
    var query by rememberSaveable { mutableStateOf("") }
    var stretch by rememberSaveable { mutableStateOf(false) }
    var saving by remember { mutableStateOf<PresetKind?>(null) }
    var deleting by remember { mutableStateOf<PresetEntry?>(null) }

    val q = query.trim()
    val list = when (tab) {
        PresetTab.Favorites -> lib.all().filter { it.key in lib.favorites }
        PresetTab.Recents -> lib.recents.mapNotNull { lib.find(it) }
        else -> lib.entries(tab.kind!!)
    }.filter { q.isEmpty() || it.name.contains(q, ignoreCase = true) }

    Column(Modifier.fillMaxSize().padding(horizontal = 12.dp)) {
        Row(
            Modifier.fillMaxWidth().padding(top = 6.dp).height(38.dp).clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip).padding(horizontal = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CupertinoIcon(CupertinoGlyph.Search, 15.dp, AureaColors.Muted)
            Spacer(Modifier.width(8.dp))
            Box(Modifier.weight(1f)) {
                if (query.isEmpty()) Text(stringResource(R.string.panel_buscar_preset), style = AureaType.Base.merge(TextStyle(fontSize = 13.5.sp, color = AureaColors.Muted)))
                BasicTextField(
                    query,
                    onValueChange = { query = it },
                    singleLine = true,
                    cursorBrush = SolidColor(AureaColors.Accent),
                    textStyle = AureaType.Base.merge(TextStyle(fontSize = 13.5.sp, color = AureaColors.Text)),
                    modifier = Modifier.fillMaxWidth(),
                )
            }
            if (query.isNotEmpty()) {
                Box(Modifier.size(34.dp).semantics { contentDescription = "Limpar busca" }.tocavel { query = "" }, contentAlignment = Alignment.Center) {
                    CupertinoIcon(CupertinoGlyph.XmarkCircleFill, 16.dp, AureaColors.Muted)
                }
            }
        }
        Row(
            Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).height(46.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            tabs.forEach { t -> Chip(t.label, t == tab) { picked = t } }
        }
        if (tab == PresetTab.Animation) {
            Row(Modifier.fillMaxWidth().height(40.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.panel_durar_ate_fim_camada), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
                AureaToggle(checked = stretch, onCheckedChange = { stretch = it })
            }
        }
        if (list.isEmpty() && tab.kind == null) {
            Box(Modifier.fillMaxWidth().weight(1f), contentAlignment = Alignment.Center) {
                Text(
                    when {
                        q.isNotEmpty() -> "Nenhum preset com \"$q\"."
                        tab == PresetTab.Favorites -> stringResource(R.string.panel_toque_preset_guardar_aqui)
                        else -> stringResource(R.string.panel_ultimos_10_presets_aplicados_aparecem_aqui)
                    },
                    textAlign = TextAlign.Center,
                    style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)),
                )
            }
        } else {
            LazyVerticalGrid(
                columns = GridCells.Adaptive(100.dp),
                modifier = Modifier.fillMaxWidth().weight(1f),
                contentPadding = PaddingValues(bottom = 12.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                items(list, key = { it.key }) { e ->
                    PresetCard(
                        store,
                        e,
                        showKind = tab.kind == null,
                        favorite = e.key in lib.favorites,
                        onApply = { apply(store, e, stretch) },
                        onFavorite = { lib.toggleFavorite(e) },
                        onDelete = if (e.builtin) null else ({ deleting = e }),
                    )
                }
                tab.kind?.let { k -> item(key = "salvar") { SaveCard { saving = k } } }
            }
        }
    }

    saving?.let { k -> SavePresetDialog(store, k, onDismiss = { saving = null }) }
    deleting?.let { e ->
        AureaAlert(
            title = "Apagar \"${e.name}\"?",
            message = stringResource(R.string.panel_preset_sai_deste_aparelho),
            confirmLabel = stringResource(R.string.panel_apagar),
            destructive = true,
            onConfirm = { store.deletePreset(e) },
            onDismiss = { deleting = null },
        )
    }
}

/** Curva vai no keyframe escolhido (o trecho que sai dele); o resto, pelo store. */
private fun apply(store: EditorStore, e: PresetEntry, stretch: Boolean) {
    if (e.kind != PresetKind.Curve) {
        store.applyPreset(e, stretch)
        return
    }
    val (layer, sel) = store.selectedKeyframe ?: run {
        store.showToast("Toque num keyframe da timeline para aplicar a curva")
        return
    }
    val v = store.curveOfPreset(e) ?: run {
        store.showToast("Preset de curva inválido")
        return
    }
    val track = (store.keyframes[layer] ?: emptyList()).track(sel)
    var i = track.indexOfFirst { it.time == sel.time }
    if (i < 0 || track.size < 2) {
        store.showToast("Crie pelo menos 2 keyframes para aplicar a curva")
        return
    }
    if (i == track.lastIndex) i--
    store.beginGesture("preset de curva")
    applyEase(store, layer, track[i], Ease(v[0].toInt(), v[1], v[2], v[3], v[4]))
    store.endGesture()
    store.presets.markUsed(e)
    store.showToast("Curva \"${e.name}\" aplicada")
}

/** Nome do preset (+ o que salvar, no texto). Curva: a do keyframe escolhido. */
@Composable
private fun SavePresetDialog(store: EditorStore, kind: PresetKind, onDismiss: () -> Unit) {
    var name by remember { mutableStateOf("") }
    var style by remember { mutableStateOf(true) }
    var anim by remember { mutableStateOf(true) }
    val exists = name.isNotBlank() && store.presets.exists(kind, name)
    AureaAlert(
        title = "Salvar preset de ${kind.label.lowercase()}",
        message = when (kind) {
            PresetKind.Caption -> stringResource(R.string.panel_guarda_opcoes_atuais_legenda)
            PresetKind.Curve -> stringResource(R.string.panel_guarda_curva_keyframe_escolhido)
            PresetKind.Animation -> stringResource(R.string.panel_guarda_keyframes_movimento_camada)
            PresetKind.Effects -> stringResource(R.string.panel_guarda_efeitos_camada_keyframes)
            PresetKind.Text -> stringResource(R.string.panel_guarda_estilo_ou_animacao_texto)
        },
        confirmLabel = stringResource(R.string.panel_salvar),
        onConfirm = {
            val n = name.trim()
            if (n.isEmpty()) {
                store.showToast("Dê um nome ao preset")
                return@AureaAlert
            }
            val json = when (kind) {
                PresetKind.Curve -> curveJson(store, n)
                PresetKind.Text -> {
                    val parts = (if (style) 1 else 0) or (if (anim) 2 else 0)
                    if (parts == 0) {
                        store.showToast("Escolha estilo e/ou animação")
                        return@AureaAlert
                    }
                    store.capturePreset(kind, n, parts)
                }
                else -> store.capturePreset(kind, n)
            }
            store.savePreset(kind, n, json)
        },
        onDismiss = onDismiss,
        extra = {
            BasicTextField(
                value = name,
                onValueChange = { name = it.take(60) },
                singleLine = true,
                textStyle = AureaType.Base.merge(TextStyle(fontSize = 15.sp)),
                cursorBrush = SolidColor(AureaColors.Accent),
                modifier = Modifier
                    .padding(top = 10.dp)
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(7.dp))
                    .background(androidx.compose.ui.graphics.Color(0xFF1C1C1E))
                    .padding(horizontal = 8.dp, vertical = 7.dp),
            )
            if (exists) Text(stringResource(R.string.panel_ja_existe_sera_substituido), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Danger)))
            if (kind == PresetKind.Text) {
                Row(Modifier.fillMaxWidth().height(40.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(R.string.panel_estilo), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
                    AureaToggle(checked = style, onCheckedChange = { style = it })
                }
                Row(Modifier.fillMaxWidth().height(40.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(R.string.panel_animacao), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
                    AureaToggle(checked = anim, onCheckedChange = { anim = it })
                }
            }
        },
    )
}

/** JSON da curva do trecho que sai do keyframe escolhido (nulo = sem trecho). */
private fun curveJson(store: EditorStore, name: String): String? {
    val (layer, sel) = store.selectedKeyframe ?: return null
    val track = (store.keyframes[layer] ?: emptyList()).track(sel)
    var i = track.indexOfFirst { it.time == sel.time }
    if (i < 0 || track.size < 2) return null
    if (i == track.lastIndex) i--
    val e = easeOf(layer, track[i])
    val h = e.handles()
    return store.curvePresetJson(name, e.interp, h[0], h[1], h[2], h[3])
}

/** Fundo da prévia: o palco (a composição fica escura atrás do objeto). */
private val PreviewBg = AureaColors.Stage

/** Cartão do preset: prévia (com ☆ e, nos seus, a lixeira), nome embaixo. Toque = aplicar. */
@Composable
private fun PresetCard(
    store: EditorStore,
    e: PresetEntry,
    showKind: Boolean,
    favorite: Boolean,
    onApply: () -> Unit,
    onFavorite: () -> Unit,
    onDelete: (() -> Unit)?,
) {
    Column(
        Modifier
            .clip(RoundedCornerShape(12.dp))
            .background(AureaColors.Chip)
            .semantics { contentDescription = "Aplicar ${e.name}" }
            .tocavel(onClick = onApply)
            .padding(6.dp),
    ) {
        Box(Modifier.fillMaxWidth().height(62.dp).clip(RoundedCornerShape(8.dp)).background(PreviewBg)) {
            PresetPreview(store, e, Modifier.fillMaxSize())
            Box(
                Modifier.align(Alignment.TopEnd).size(34.dp)
                    .semantics { contentDescription = if (favorite) "Tirar dos favoritos" else "Favoritar" }
                    .tocavel(onClick = onFavorite),
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(if (favorite) CupertinoGlyph.StarFill else CupertinoGlyph.Star, 15.dp, if (favorite) AureaColors.Warning else AureaColors.Muted)
            }
            if (onDelete != null) {
                Box(
                    Modifier.align(Alignment.TopStart).size(34.dp).semantics { contentDescription = "Apagar preset" }.tocavel(onClick = onDelete),
                    contentAlignment = Alignment.Center,
                ) {
                    CupertinoIcon(CupertinoGlyph.Trash, 14.dp, AureaColors.Danger)
                }
            }
        }
        Spacer(Modifier.height(5.dp))
        Text(
            e.name,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.height(30.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, lineHeight = 14.sp, fontWeight = FontWeight.W600)),
        )
        if (showKind) Text(e.kind.label, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 10.sp, color = AureaColors.Muted)))
    }
}

/** O último cartão: guardar o que está na camada como preset deste tipo. */
@Composable
private fun SaveCard(onClick: () -> Unit) {
    Column(
        Modifier
            .clip(RoundedCornerShape(12.dp))
            .background(AureaColors.AccentDim)
            .semantics { contentDescription = "Salvar o da camada como preset" }
            .tocavel(onClick = onClick)
            .padding(6.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(Modifier.fillMaxWidth().height(62.dp), contentAlignment = Alignment.Center) {
            CupertinoIcon(CupertinoGlyph.Plus, 24.dp, AureaColors.Accent)
        }
        Spacer(Modifier.height(5.dp))
        Text(
            stringResource(R.string.panel_salvar_desta_camada),
            maxLines = 2,
            textAlign = TextAlign.Center,
            modifier = Modifier.height(30.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, lineHeight = 14.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)),
        )
    }
}

@Composable
private fun PresetPreview(store: EditorStore, e: PresetEntry, modifier: Modifier) {
    // O JSON do preset (dos seus, o arquivo) é lido uma vez por cartão.
    val json = remember(e.key) { store.presets.jsonOf(e) }
    when (e.kind) {
        PresetKind.Animation -> {
            val tracks = remember(json) { json?.let { parseAnim(it) } }
            if (tracks.isNullOrEmpty()) GlyphPreview(CupertinoGlyph.Move, modifier)
            else AnimPreview(tracks, animFps(json), store.project.width.coerceAtLeast(1).toFloat(), modifier)
        }
        PresetKind.Curve -> {
            val v = remember(json) { json?.let { store.curveOfPreset(e) } }
            if (v == null || v.size < 5) GlyphPreview(CupertinoGlyph.Scribble, modifier)
            else CurvePreview(Ease(v[0].toInt(), v[1], v[2], v[3], v[4]), modifier)
        }
        PresetKind.Caption -> {
            val c = remember(json) { json?.let { runCatching { JSONObject(it).optJSONObject("caption") }.getOrNull() } }
            CaptionPreview(c, modifier)
        }
        PresetKind.Effects -> {
            val keys = remember(json) { json?.let { effectKeys(it) }.orEmpty() }
            GlyphPreview(effectGlyph(keys.firstOrNull()), modifier, badge = if (keys.size > 1) "${keys.size} efeitos" else null)
        }
        PresetKind.Text -> Box(modifier, contentAlignment = Alignment.Center) {
            Text(stringResource(R.string.panel_aa), style = AureaType.Base.merge(TextStyle(fontSize = 24.sp, fontWeight = FontWeight.W700, color = AureaColors.Text)))
        }
    }
}

@Composable
private fun GlyphPreview(glyph: Char, modifier: Modifier, badge: String? = null) {
    Box(modifier, contentAlignment = Alignment.Center) {
        CupertinoIcon(glyph, 26.dp, AureaColors.Accent)
        if (badge != null) {
            Text(
                badge,
                modifier = Modifier.align(Alignment.BottomCenter).padding(bottom = 3.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 9.5.sp, color = AureaColors.Muted)),
            )
        }
    }
}

// --- Animação ----------------------------------------------------------------------

/** Uma trilha do preset: `keys` = [quadro, valor, interpolação, x1, y1, x2, y2] (o formato do motor). */
private class AnimTrack(val prop: String, val keys: List<FloatArray>) {
    fun at(f: Float): Float {
        if (keys.isEmpty()) return 0f
        if (f <= keys[0][0]) return keys[0][1]
        for (i in 0 until keys.size - 1) {
            val a = keys[i]
            val b = keys[i + 1]
            if (f < b[0]) {
                val u = (f - a[0]) / max(1e-3f, b[0] - a[0])
                return a[1] + (b[1] - a[1]) * Ease(a[2].toInt(), a[3], a[4], a[5], a[6]).transform(u)
            }
        }
        return keys.last()[1]
    }

    /** O valor em repouso (último keyframe): a posição mede o deslocamento a partir dele. */
    val rest: Float get() = keys.lastOrNull()?.get(1) ?: 0f
    val span: Float get() = keys.lastOrNull()?.get(0) ?: 0f
}

private fun parseAnim(json: String): List<AnimTrack>? = runCatching {
    val arr = JSONObject(json).optJSONArray("tracks") ?: return null
    (0 until arr.length()).mapNotNull { i ->
        val t = arr.optJSONObject(i) ?: return@mapNotNull null
        val ks = t.optJSONArray("keys") ?: return@mapNotNull null
        // Os padrões do leitor do motor: sem interpolação = reta; alças 0,33/0/0,67/1.
        val keys = (0 until ks.length()).mapNotNull { j ->
            val a = ks.optJSONArray(j) ?: return@mapNotNull null
            val d = floatArrayOf(0f, 0f, 1f, 0.33f, 0f, 0.67f, 1f)
            for (k in 0 until min(a.length(), 7)) d[k] = a.optDouble(k, d[k].toDouble()).toFloat()
            d
        }.sortedBy { it[0] }
        if (keys.isEmpty()) null else AnimTrack(t.optString("prop"), keys)
    }
}.getOrNull()

private fun animFps(json: String?): Float = json?.let { runCatching { JSONObject(it).optDouble("fps", 30.0).toFloat() }.getOrNull() }?.takeIf { it > 0f } ?: 30f

/**
 * O quadradinho segue as trilhas do preset em loop (com uma pausa no fim). A
 * posição vale na proporção da composição do projeto: 600 px numa tela de 1080
 * andam mais de meio cartão.
 */
@Composable
private fun AnimPreview(tracks: List<AnimTrack>, fps: Float, compWidth: Float, modifier: Modifier) {
    val span = max(1f, tracks.maxOf { it.span })
    val loop = span + fps * 0.6f
    val t = rememberInfiniteTransition(label = "preset")
    val frame by t.animateFloat(
        0f, loop,
        infiniteRepeatable(tween(((loop / fps) * 1000f).toInt().coerceAtLeast(300), easing = LinearEasing), RepeatMode.Restart),
        label = "quadro",
    )
    val byProp = remember(tracks) { tracks.associateBy { it.prop } }
    Canvas(modifier) {
        val f = min(frame, span)
        fun v(p: String, def: Float) = byProp[p]?.at(f) ?: def
        fun off(p: String) = byProp[p]?.let { it.at(f) - it.rest } ?: 0f
        val side = size.height * 0.4f
        val dx = off("positionX") / compWidth * size.width
        val dy = off("positionY") / compWidth * size.width
        val sx = v("scaleX", 1f)
        val sy = v("scaleY", 1f)
        val rot = v("rotationZ", 0f)
        val alpha = v("opacity", 1f).coerceIn(0f, 1f)
        val c = Offset(size.width / 2f + dx, size.height / 2f + dy)
        translate(c.x, c.y) {
            rotate(rot, Offset.Zero) {
                scale(sx, sy, Offset.Zero) {
                    drawRoundRect(
                        AureaColors.Accent.copy(alpha = alpha),
                        topLeft = Offset(-side / 2f, -side / 2f),
                        size = Size(side, side),
                        cornerRadius = CornerRadius(side * 0.18f),
                    )
                }
            }
        }
    }
}

// --- Curva ---------------------------------------------------------------------------

/** A curva do preset (tempo → valor) e um ponto que corre nela. */
@Composable
private fun CurvePreview(ease: Ease, modifier: Modifier) {
    val t = rememberInfiniteTransition(label = "curva")
    val u by t.animateFloat(0f, 1.4f, infiniteRepeatable(tween(1600, easing = LinearEasing), RepeatMode.Restart), label = "u")
    val ys = remember(ease) { FloatArray(41) { ease.transform(it / 40f) } }
    val lo = min(0f, ys.min())
    val hi = max(1f, ys.max())
    Canvas(modifier.padding(horizontal = 14.dp, vertical = 10.dp)) {
        fun p(x: Float, y: Float) = Offset(x * size.width, size.height - (y - lo) / (hi - lo) * size.height)
        drawLine(AureaColors.CurveGrid, p(0f, 0f), p(1f, 0f), 1.dp.toPx())
        drawLine(AureaColors.CurveGrid, p(0f, 1f), p(1f, 1f), 1.dp.toPx())
        val path = Path()
        ys.forEachIndexed { i, y -> val o = p(i / 40f, y); if (i == 0) path.moveTo(o.x, o.y) else path.lineTo(o.x, o.y) }
        drawPath(path, AureaColors.Keyframe, style = Stroke(2.dp.toPx(), cap = StrokeCap.Round))
        val x = min(u, 1f)
        drawCircle(AureaColors.Accent, 3.5.dp.toPx(), p(x, ease.transform(x)))
    }
}

// --- Legenda -------------------------------------------------------------------------

/** A linha da legenda com as opções do preset: palavras por vez, maiúsculas, destaque, altura e tamanho. */
@Composable
private fun CaptionPreview(c: JSONObject?, modifier: Modifier) {
    val words = listOf(stringResource(R.string.panel_sua), "legenda", "aparece", "assim", "aqui", "hoje")
    val one = (c?.optInt("mode", 0) ?: 0) == 1
    val n = if (one) 1 else (c?.optInt("maxWords", 4) ?: 4).coerceIn(1, 4)
    val upper = c?.optBoolean("uppercase", false) ?: false
    val highlight = c?.optBoolean("highlight", false) ?: false
    val posY = (c?.optDouble("posY", 0.8) ?: 0.8).toFloat().coerceIn(0.1f, 0.9f)
    val size = (c?.optDouble("sizeFrac", 0.06) ?: 0.06).toFloat()
    val fontSp = (8f + size * 90f).coerceIn(8f, 16f)
    Box(modifier) {
        val line = androidx.compose.ui.text.buildAnnotatedString {
            words.take(n).forEachIndexed { i, w ->
                if (i > 0) append(' ')
                val s = if (upper) w.uppercase() else w
                if (highlight && i == (if (one) 0 else 1)) {
                    pushStyle(androidx.compose.ui.text.SpanStyle(color = AureaColors.Warning))
                    append(s)
                    pop()
                } else {
                    append(s)
                }
            }
        }
        Text(
            line,
            maxLines = 1,
            overflow = TextOverflow.Clip,
            textAlign = TextAlign.Center,
            modifier = Modifier
                .fillMaxWidth()
                .align(androidx.compose.ui.BiasAlignment(0f, posY * 2f - 1f))
                .padding(horizontal = 4.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = fontSp.sp, fontWeight = FontWeight.W800, color = Color.White)),
        )
    }
}

// --- Efeitos -------------------------------------------------------------------------

private fun effectKeys(json: String): List<String> = runCatching {
    val arr = JSONObject(json).optJSONArray("effects") ?: return emptyList()
    (0 until arr.length()).mapNotNull { arr.optJSONObject(it)?.optString("key") }
}.getOrDefault(emptyList())

/** O ícone da categoria do efeito (pela chave `aurea.<categoria>.<efeito>`). */
private fun effectGlyph(key: String?): Char {
    val k = key ?: return CupertinoGlyph.WandStars
    return when {
        ".blur." in k -> CupertinoGlyph.DropFill
        ".light." in k -> CupertinoGlyph.Sparkles
        ".color." in k -> CupertinoGlyph.ColorFilter
        ".distort." in k -> CupertinoGlyph.Scribble
        else -> CupertinoGlyph.WandStars
    }
}

@Composable
private fun Chip(label: String, on: Boolean, onClick: () -> Unit) {
    Box(
        Modifier.height(34.dp).clip(RoundedCornerShape(9.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
            .tocavel(onClick = onClick).padding(horizontal = 12.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.5.sp, fontWeight = if (on) FontWeight.W700 else FontWeight.W500, color = if (on) AureaColors.Accent else AureaColors.Text)))
    }
}
