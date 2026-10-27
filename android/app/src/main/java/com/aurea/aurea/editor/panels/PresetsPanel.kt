package com.aurea.aurea.editor.panels

import com.aurea.aurea.ui.i18n.AppText
import androidx.annotation.StringRes
import android.app.Application
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
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
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
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.toggleableState
import androidx.compose.ui.state.ToggleableState
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.graphicsLayer
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
import androidx.compose.ui.platform.testTag
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
private enum class PresetTab(@StringRes val label: Int, val kind: PresetKind?, val prefix: String = "") {
    Favorites(R.string.edt_cmd_favorites, null, "★ "),
    Recents(R.string.effect_recentes, null),
    Animation(R.string.panel_animacao, PresetKind.Animation),
    Effects(R.string.panel_efeitos, PresetKind.Effects),
    Text(R.string.panel_texto, PresetKind.Text),
    Caption(R.string.pn_caption, PresetKind.Caption),
    Curve(R.string.panel_curva, PresetKind.Curve),
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
    val isText = kind == LayerType.Text.kind || store.text3d != null
    val speaks = kind == LayerType.Video.kind || kind == LayerType.Audio.kind || kind == LayerType.Text.kind
    // Texto (2D ou 3D): o painel é o de PRESETS DE TEXTO (a ficha "Presets" da
    // doca do texto) — só a aba de texto e os favoritos/recentes de texto. Os
    // presets de vídeo/imagem saíram da interface (2026-10-02) e continuam fora.
    val tabs = if (isText) listOf(PresetTab.Text, PresetTab.Favorites, PresetTab.Recents) else PresetTab.entries.filter { t ->
        when (t.kind) {
            PresetKind.Text -> isText
            PresetKind.Caption -> speaks
            else -> true
        }
    }
    var picked by rememberSaveable { mutableStateOf(if (isText) PresetTab.Text else PresetTab.Animation) }
    // "Meus presets" (vindo dos efeitos) abre direto na aba pedida.
    LaunchedEffect(store.presetsOpenKind) {
        store.presetsOpenKind?.let { k -> PresetTab.entries.firstOrNull { it.kind == k }?.let { picked = it } }
        store.presetsOpenKind = null
    }
    val tab = if (picked in tabs) picked else tabs.first()
    val onlyText: (PresetEntry) -> Boolean = { !isText || it.kind == PresetKind.Text }
    var query by rememberSaveable { mutableStateOf(store.presetsOpenSearch.orEmpty()) }
    LaunchedEffect(Unit) { store.presetsOpenSearch = null }
    var stretch by rememberSaveable { mutableStateOf(false) }
    var saving by remember { mutableStateOf<PresetKind?>(null) }
    var deleting by remember { mutableStateOf<PresetEntry?>(null) }

    val q = query.trim()
    val list = when (tab) {
        PresetTab.Favorites -> lib.all().filter { it.key in lib.favorites }
        PresetTab.Recents -> lib.recents.mapNotNull { lib.find(it) }
        else -> lib.entries(tab.kind!!)
    }.filter { onlyText(it) && (q.isEmpty() || it.name.contains(q, ignoreCase = true)) }

    // 0.3.4 presets: o título "Presets" mora só na barra da seção (SectionTopBar).
    // Busca recolhida numa lupa ao lado das abas (uma fileira só); aberta, o campo
    // ocupa a fileira e o ✕ limpa e recolhe. Vindo de "Meus presets" com busca, já aberta.
    var searchOpen by rememberSaveable { mutableStateOf(query.isNotEmpty()) }
    val searchFocus = remember { FocusRequester() }
    LaunchedEffect(searchOpen) { if (searchOpen && query.isEmpty()) runCatching { searchFocus.requestFocus() } }

    Column(Modifier.fillMaxSize().padding(horizontal = 12.dp)) {
        // Cabeçalho compacto: UMA fileira de 44 dp (lupa + abas, ou o campo de busca).
        Row(
            Modifier.fillMaxWidth().padding(top = 4.dp).height(44.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (searchOpen) {
                Row(
                    Modifier.weight(1f).height(34.dp).clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip).padding(start = 10.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    CupertinoIcon(CupertinoGlyph.Search, 15.dp, AureaColors.Muted)
                    Spacer(Modifier.width(8.dp))
                    Box(Modifier.weight(1f)) {
                        if (query.isEmpty()) Text(stringResource(R.string.panel_buscar_preset), maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 13.5.sp, color = AureaColors.Muted)))
                        BasicTextField(
                            query,
                            onValueChange = { query = it },
                            singleLine = true,
                            cursorBrush = SolidColor(AureaColors.Accent),
                            textStyle = AureaType.Base.merge(TextStyle(fontSize = 13.5.sp, color = AureaColors.Text)),
                            modifier = Modifier.fillMaxWidth().focusRequester(searchFocus).testTag("presets.search"),
                        )
                    }
                    val clearLabel = stringResource(R.string.panel_limpar_busca)
                    Box(
                        Modifier.size(44.dp, 34.dp).semantics { contentDescription = clearLabel }
                            .tocavel { query = ""; searchOpen = false },
                        contentAlignment = Alignment.Center,
                    ) {
                        CupertinoIcon(CupertinoGlyph.XmarkCircleFill, 16.dp, AureaColors.Muted)
                    }
                }
            } else {
                val searchLabel = stringResource(R.string.panel_buscar_preset)
                Box(
                    Modifier.size(44.dp).semantics { contentDescription = searchLabel }.testTag("presets.searchOpen")
                        .tocavel { searchOpen = true },
                    contentAlignment = Alignment.CenterStart,
                ) {
                    Box(Modifier.size(34.dp, 32.dp).clip(RoundedCornerShape(9.dp)).background(AureaColors.Chip), contentAlignment = Alignment.Center) {
                        CupertinoIcon(CupertinoGlyph.Search, 15.dp, AureaColors.Text)
                    }
                }
                Row(
                    Modifier.weight(1f).horizontalScroll(rememberScrollState()).testTag("presets.tabs"),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    tabs.forEach { t -> TabChip(t.prefix + stringResource(t.label), t == tab) { picked = t } }
                }
                // "Durar até o fim da camada" (antes uma linha inteira com interruptor):
                // pílula curta liga/desliga na ponta da fileira, só na aba Animação.
                if (tab == PresetTab.Animation) {
                    Spacer(Modifier.width(6.dp))
                    StretchChip(stretch) { stretch = !stretch }
                }
            }
        }
        if (list.isEmpty() && tab.kind == null) {
            Box(Modifier.fillMaxWidth().weight(1f), contentAlignment = Alignment.Center) {
                Text(
                    when {
                        q.isNotEmpty() -> stringResource(R.string.edt_presets_none_matching, q)
                        tab == PresetTab.Favorites -> stringResource(R.string.panel_toque_preset_guardar_aqui)
                        else -> stringResource(R.string.panel_ultimos_10_presets_aplicados_aparecem_aqui)
                    },
                    textAlign = TextAlign.Center,
                    style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)),
                )
            }
        } else {
            // A grade enche o resto do painel e rola por dentro (baixo ou alto, a
            // primeira fileira inteira e parte da segunda sempre aparecem).
            LazyVerticalGrid(
                columns = GridCells.Adaptive(100.dp),
                modifier = Modifier.fillMaxWidth().weight(1f).testTag("presets.grid"),
                contentPadding = PaddingValues(top = 4.dp, bottom = 12.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                // A dica do CC rola junto com os cartões (antes empurrava a grade para fora).
                if (tab == PresetTab.Effects) {
                    item(key = "dica", span = { GridItemSpan(maxLineSpan) }) {
                        Text(stringResource(R.string.cc_preset_hint),
                            style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, lineHeight = 15.sp, color = AureaColors.Muted)))
                    }
                }
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
            title = stringResource(R.string.edt_preset_delete_title, e.name),
            message = stringResource(R.string.panel_preset_sai_deste_aparelho),
            confirmLabel = stringResource(R.string.panel_apagar),
            destructive = true,
            onConfirm = { store.deletePreset(e) },
            onDismiss = { deleting = null },
        )
    }
}

/**
 * PRESETS DE TEXTO na barra de adicionar (ao lado do "Texto", que segue criando
 * o texto direto): escolher 2D ou 3D e tocar num cartão cria o texto já com o
 * preset ([EditorStore.addTextWithPreset]). Entram os 25 nativos do motor e os
 * de texto salvos ou baixados da comunidade (a mesma [PresetLibrary]).
 */
@Composable
internal fun TextPresetPicker(store: EditorStore, onAdded: () -> Unit) {
    val lib = store.presets
    var threeD by rememberSaveable { mutableStateOf(false) }
    val list = lib.entries(PresetKind.Text)
    Column(Modifier.fillMaxSize().padding(horizontal = 12.dp)) {
        Row(
            Modifier.fillMaxWidth().height(46.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            Chip(stringResource(R.string.sh_add_tab_text), !threeD) { threeD = false }
            Chip(stringResource(R.string.sh_add_text_3d), threeD) { threeD = true }
        }
        Text(stringResource(R.string.tp_hint), modifier = Modifier.padding(bottom = 8.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
        LazyVerticalGrid(
            columns = GridCells.Adaptive(100.dp),
            modifier = Modifier.fillMaxWidth().weight(1f).testTag("textPresets.grid"),
            contentPadding = PaddingValues(bottom = 12.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            items(list, key = { it.key }) { e ->
                PresetCard(
                    store,
                    e,
                    showKind = false,
                    favorite = e.key in lib.favorites,
                    onApply = { if (store.addTextWithPreset(e, threeD) >= 0) onAdded() },
                    onFavorite = { lib.toggleFavorite(e) },
                    onDelete = null,
                    applyDescription = stringResource(R.string.tp_add_named, e.name),
                )
            }
        }
    }
}

/** Curva vai no keyframe escolhido (o trecho que sai dele); o resto, pelo store. */
private fun apply(store: EditorStore, e: PresetEntry, stretch: Boolean) {
    if (e.kind != PresetKind.Curve) {
        store.applyPreset(e, stretch)
        return
    }
    val (layer, sel) = store.selectedKeyframe ?: run {
        store.showToast(AppText.get(store.getApplication<Application>(), R.string.edt_curve_tap_keyframe))
        return
    }
    val v = store.curveOfPreset(e) ?: run {
        store.showToast(AppText.get(store.getApplication<Application>(), R.string.edt_curve_invalid))
        return
    }
    val track = (store.keyframes[layer] ?: emptyList()).track(sel)
    var i = track.indexOfFirst { it.time == sel.time }
    if (i < 0 || track.size < 2) {
        store.showToast(AppText.get(store.getApplication<Application>(), R.string.edt_curve_need_two))
        return
    }
    if (i == track.lastIndex) i--
    store.beginGesture("preset de curva")
    applyEase(store, layer, track[i], curvePresetEase(v))
    store.endGesture()
    store.presets.markUsed(e)
    store.showToast(AppText.get(store.getApplication<Application>(), R.string.edt_curve_applied, e.name))
}

/** Nome do preset (+ o que salvar, no texto). Curva: a do keyframe escolhido. */
@Composable
private fun SavePresetDialog(store: EditorStore, kind: PresetKind, onDismiss: () -> Unit) {
    var name by remember { mutableStateOf("") }
    var style by remember { mutableStateOf(true) }
    var anim by remember { mutableStateOf(true) }
    val exists = name.isNotBlank() && store.presets.exists(kind, name)
    AureaAlert(
        title = stringResource(R.string.pn_save_preset_of, stringResource(kind.label).lowercase()),
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
                store.showToast(AppText.get(store.getApplication<Application>(), R.string.panel_nome_preset))
                return@AureaAlert
            }
            val json = when (kind) {
                PresetKind.Curve -> curveJson(store, n)
                PresetKind.Text -> {
                    val parts = (if (style) 1 else 0) or (if (anim) 2 else 0)
                    if (parts == 0) {
                        store.showToast(AppText.get(store.getApplication<Application>(), R.string.edt_preset_pick_parts))
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
            // Omega fundação: campo e textos do diálogo claro (F4).
            com.aurea.aurea.ui.ds.AureaDialogField(
                value = name,
                onValueChange = { name = it.take(60) },
            )
            if (exists) Text(stringResource(R.string.panel_ja_existe_sera_substituido), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.LightDanger)))
            if (kind == PresetKind.Text) {
                Row(Modifier.fillMaxWidth().height(40.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(R.string.panel_estilo), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.LightText)))
                    AureaToggle(checked = style, onCheckedChange = { style = it })
                }
                Row(Modifier.fillMaxWidth().height(40.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(R.string.panel_animacao), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.LightText)))
                    AureaToggle(checked = anim, onCheckedChange = { anim = it })
                }
            }
        },
    )
}

/** A curva de um preset lido ([interp, x1, y1, x2, y2, força]); preset antigo = força 1. */
internal fun curvePresetEase(v: FloatArray): Ease =
    Ease(v[0].toInt(), v[1], v[2], v[3], v[4], (v.getOrNull(5) ?: 1f).toInt().coerceIn(1, 3))

/** JSON da curva do trecho que sai do keyframe escolhido (nulo = sem trecho). */
private fun curveJson(store: EditorStore, name: String): String? {
    val (layer, sel) = store.selectedKeyframe ?: return null
    val track = (store.keyframes[layer] ?: emptyList()).track(sel)
    var i = track.indexOfFirst { it.time == sel.time }
    if (i < 0 || track.size < 2) return null
    if (i == track.lastIndex) i--
    val e = easeOf(store, layer, track[i])
    val h = e.handles()
    return store.curvePresetJson(name, e.interp, h[0], h[1], h[2], h[3], if (e.isBezier) e.power else 1)
}

/** Fundo da prévia: o palco (a composição fica escura atrás do objeto). */
private val PreviewBg = AureaColors.Stage

/** 0.3.4 presets: medidas do cartão (par do `PresetCardView` do iOS). */
private val CardPreview = 50.dp
private val CardName = 26.dp

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
    applyDescription: String? = null,
) {
    val applyLabel = applyDescription ?: stringResource(R.string.edt_apply_named, e.name)
    val favoriteLabel = stringResource(R.string.panel_favoritar)
    val unfavoriteLabel = stringResource(R.string.panel_tirar_favoritos)
    val deleteLabel = stringResource(R.string.panel_apagar_preset)
    Column(
        Modifier
            .clip(RoundedCornerShape(12.dp))
            .background(AureaColors.Chip)
            .semantics { contentDescription = applyLabel }
            .tocavel(onClick = onApply)
            .padding(5.dp),
    ) {
        // 0.3.4 presets: cartão mais baixo (prévia 50 + nome em 2 linhas = 90 dp) para
        // a grade mostrar 1,5+ fileiras no painel baixo; o tipo vira selo na prévia.
        Box(Modifier.fillMaxWidth().height(CardPreview).clip(RoundedCornerShape(8.dp)).background(PreviewBg)) {
            PresetPreview(store, e, Modifier.fillMaxSize())
            if (showKind) {
                Text(
                    stringResource(e.kind.label),
                    maxLines = 1,
                    modifier = Modifier.align(Alignment.BottomStart).padding(4.dp)
                        .clip(RoundedCornerShape(4.dp)).background(AureaColors.Chip).padding(horizontal = 4.dp, vertical = 1.dp),
                    style = AureaType.Base.merge(TextStyle(fontSize = 9.5.sp, color = AureaColors.Muted)),
                )
            }
            Box(
                Modifier.align(Alignment.TopEnd).size(34.dp)
                    .semantics { contentDescription = if (favorite) unfavoriteLabel else favoriteLabel }
                    .tocavel(onClick = onFavorite),
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(if (favorite) CupertinoGlyph.StarFill else CupertinoGlyph.Star, 15.dp, if (favorite) AureaColors.Warning else AureaColors.Muted)
            }
            if (onDelete != null) {
                Box(
                    Modifier.align(Alignment.TopStart).size(34.dp).semantics { contentDescription = deleteLabel }.tocavel(onClick = onDelete),
                    contentAlignment = Alignment.Center,
                ) {
                    CupertinoIcon(CupertinoGlyph.Trash, 14.dp, AureaColors.Danger)
                }
            }
        }
        Spacer(Modifier.height(4.dp))
        Text(
            e.name,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.fillMaxWidth().height(CardName),
            style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, lineHeight = 13.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)),
        )
    }
}

/** O último cartão: guardar o que está na camada como preset deste tipo. */
@Composable
private fun SaveCard(onClick: () -> Unit) {
    val saveLabel = stringResource(R.string.edt_preset_save_from_layer)
    Column(
        Modifier
            .clip(RoundedCornerShape(12.dp))
            .background(AureaColors.AccentDim)
            .semantics { contentDescription = saveLabel }
            .tocavel(onClick = onClick)
            .padding(5.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(Modifier.fillMaxWidth().height(CardPreview), contentAlignment = Alignment.Center) {
            CupertinoIcon(CupertinoGlyph.Plus, 22.dp, AureaColors.Accent)
        }
        Spacer(Modifier.height(4.dp))
        Text(
            stringResource(R.string.panel_salvar_desta_camada),
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
            modifier = Modifier.height(CardName),
            style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, lineHeight = 13.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)),
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
            else CurvePreview(curvePresetEase(v), modifier)
        }
        PresetKind.Caption -> {
            val c = remember(json) { json?.let { runCatching { JSONObject(it).optJSONObject("caption") }.getOrNull() } }
            CaptionPreview(c, modifier)
        }
        PresetKind.Effects -> {
            val keys = remember(json) { json?.let { effectKeys(it) }.orEmpty() }
            GlyphPreview(effectGlyph(keys.firstOrNull()), modifier, badge = if (keys.size > 1) stringResource(R.string.edt_effects_count, keys.size) else null)
        }
        PresetKind.Text -> TextMotionPreview(e.textPreset, modifier)
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

/** 0.3.4 presets: o "Durar até o fim da camada" compacto (pílula liga/desliga, alvo de 44 dp). */
@Composable
private fun StretchChip(on: Boolean, onToggle: () -> Unit) {
    val full = stringResource(R.string.panel_durar_ate_fim_camada)
    Box(
        Modifier.height(44.dp)
            .semantics { contentDescription = full; toggleableState = ToggleableState(on) }
            .testTag("presets.stretch")
            .tocavel(role = Role.Switch, onClick = onToggle),
        contentAlignment = Alignment.Center,
    ) {
        Row(
            Modifier.height(32.dp).clip(RoundedCornerShape(9.dp))
                .background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                .padding(horizontal = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CupertinoIcon(CupertinoGlyph.ArrowRightToLine, 13.dp, if (on) AureaColors.Accent else AureaColors.Muted)
            Spacer(Modifier.width(5.dp))
            Text(stringResource(R.string.p34_presets_to_end), maxLines = 1,
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = if (on) FontWeight.W700 else FontWeight.W500, color = if (on) AureaColors.Accent else AureaColors.Muted)))
        }
    }
}

/** 0.3.4 presets: aba do cabeçalho — pílula de 32 dp num alvo de 44 dp (fileira única). */
@Composable
private fun TabChip(label: String, on: Boolean, onClick: () -> Unit) {
    Box(
        Modifier.height(44.dp).semantics { selected = on }.tocavel(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier.height(32.dp).clip(RoundedCornerShape(9.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                .padding(horizontal = 12.dp),
            contentAlignment = Alignment.Center,
        ) {
            Text(label, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 12.5.sp, fontWeight = if (on) FontWeight.W700 else FontWeight.W500, color = if (on) AureaColors.Accent else AureaColors.Text)))
        }
    }
}

@Composable
private fun TextMotionPreview(preset: Int?, modifier: Modifier) {
    val transition = rememberInfiniteTransition(label = "text preview")
    val phase by transition.animateFloat(0f, 1f, infiniteRepeatable(tween(2400, easing = LinearEasing)), label = "phase")
    Box(modifier, contentAlignment = Alignment.Center) {
        Row {
            "Aurea".forEachIndexed { index, glyph ->
                val progress = ((phase * 2f).coerceAtMost(2f) - index * .12f).coerceIn(0f, 1f)
                val opacity = when (preset) {
                    20, 21, 23, 24 -> (progress * 3f).coerceIn(0f, 1f) * ((1f - phase) * 5f).coerceIn(0f, 1f)
                    22 -> .3f + .7f * kotlin.math.abs(kotlin.math.sin(phase * 60f + index * 4f))
                    else -> 1f
                }
                val size = if (preset == 19) 1f - .8f * kotlin.math.sin(progress * Math.PI).toFloat() else 1f
                Text(glyph.toString(), Modifier.graphicsLayer { alpha = opacity; scaleX = size; scaleY = size },
                    style = AureaType.Base.merge(TextStyle(fontSize = 24.sp, fontWeight = FontWeight.W700, color = AureaColors.Text)))
            }
        }
    }
}
