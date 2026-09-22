package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
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

/** Abas do navegador: favoritos, recentes e uma por tipo de preset. */
private enum class PresetTab(val label: String, val kind: PresetKind?) {
    Favorites("★ Favoritos", null),
    Recents("Recentes", null),
    Effects("Efeitos", PresetKind.Effects),
    Text("Texto", PresetKind.Text),
    Animation("Animação", PresetKind.Animation),
    Caption("Legenda", PresetKind.Caption),
    Curve("Curva", PresetKind.Curve),
}

/**
 * PRESETS: navegar (abas por tipo, busca, favoritos, recentes), aplicar com um
 * toque (um passo de desfazer), salvar o que está na camada como preset e
 * apagar os próprios. Os arquivos moram em filesDir/presets/<tipo>/.
 */
@Composable
internal fun PresetsPanel(env: PanelEnv) {
    val store = env.store
    val lib = store.presets
    val isText = store.detail?.kind == LayerType.Text.kind
    var tab by rememberSaveable { mutableStateOf(if (isText) PresetTab.Text else PresetTab.Effects) }
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
            Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).height(42.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            PresetTab.entries.forEach { t -> Chip(t.label, t == tab) { tab = t } }
        }
        Row(Modifier.fillMaxWidth().height(40.dp), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(
                Modifier.weight(1f).height(34.dp).clip(RoundedCornerShape(8.dp)).background(AureaColors.Chip).padding(horizontal = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CupertinoIcon(CupertinoGlyph.Search, 14.dp, AureaColors.Muted)
                Spacer(Modifier.width(6.dp))
                Box(Modifier.weight(1f)) {
                    if (query.isEmpty()) Text("Buscar", style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
                    BasicTextField(
                        query,
                        onValueChange = { query = it },
                        singleLine = true,
                        cursorBrush = SolidColor(AureaColors.Accent),
                        textStyle = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Text)),
                        modifier = Modifier.fillMaxWidth(),
                    )
                }
            }
            tab.kind?.let { k -> Action("Salvar como preset") { saving = k } }
        }
        if (tab == PresetTab.Animation) {
            Row(Modifier.fillMaxWidth().height(36.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("Esticar até o fim da camada", modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
                AureaToggle(checked = stretch, onCheckedChange = { stretch = it })
            }
        }
        if (list.isEmpty()) {
            Box(Modifier.fillMaxWidth().weight(1f), contentAlignment = Alignment.Center) {
                Text(
                    when {
                        q.isNotEmpty() -> "Nenhum preset com \"$q\"."
                        tab == PresetTab.Favorites -> "Toque na ☆ de um preset para guardar aqui."
                        tab == PresetTab.Recents -> "Os últimos 10 presets aplicados aparecem aqui."
                        else -> "Nenhum preset."
                    },
                    style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)),
                )
            }
        } else {
            LazyColumn(Modifier.fillMaxWidth().weight(1f)) {
                items(list, key = { it.key }) { e ->
                    PresetRow(
                        e,
                        showKind = tab.kind == null,
                        favorite = e.key in lib.favorites,
                        onApply = { apply(store, e, stretch) },
                        onFavorite = { lib.toggleFavorite(e) },
                        onDelete = if (e.builtin) null else ({ deleting = e }),
                    )
                }
            }
        }
    }

    saving?.let { kind -> SavePresetDialog(store, kind, onDismiss = { saving = null }) }
    deleting?.let { e ->
        AureaAlert(
            title = "Apagar \"${e.name}\"?",
            message = "O arquivo do preset sai deste aparelho.",
            confirmLabel = "Apagar",
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
            PresetKind.Caption -> "Guarda as opções atuais da legenda."
            PresetKind.Curve -> "Guarda a curva do keyframe escolhido."
            PresetKind.Animation -> "Guarda os keyframes de movimento da camada."
            PresetKind.Effects -> "Guarda os efeitos da camada com os keyframes."
            PresetKind.Text -> "Guarda o estilo e/ou a animação do texto."
        },
        confirmLabel = "Salvar",
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
            if (exists) Text("Já existe: será substituído.", style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Danger)))
            if (kind == PresetKind.Text) {
                Row(Modifier.fillMaxWidth().height(40.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text("Estilo", modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
                    AureaToggle(checked = style, onCheckedChange = { style = it })
                }
                Row(Modifier.fillMaxWidth().height(40.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text("Animação", modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
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

@Composable
private fun PresetRow(
    e: PresetEntry,
    showKind: Boolean,
    favorite: Boolean,
    onApply: () -> Unit,
    onFavorite: () -> Unit,
    onDelete: (() -> Unit)?,
) {
    Row(
        Modifier.fillMaxWidth().height(46.dp).padding(vertical = 3.dp).clip(RoundedCornerShape(8.dp)).background(AureaColors.Chip)
            .semantics { contentDescription = "Aplicar ${e.name}" }
            .tocavel(onClick = onApply)
            .padding(start = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(e.name, maxLines = 1, overflow = TextOverflow.Ellipsis, style = AureaType.Base.merge(TextStyle(fontSize = 13.5.sp, fontWeight = FontWeight.W600)))
            val sub = listOfNotNull(if (showKind) e.kind.label else null, if (e.builtin) "Nativo" else "Meu").joinToString(" · ")
            Text(sub, style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = AureaColors.Muted)))
        }
        Box(
            Modifier.size(40.dp).semantics { contentDescription = if (favorite) "Tirar dos favoritos" else "Favoritar" }.tocavel(onClick = onFavorite),
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(if (favorite) CupertinoGlyph.StarFill else CupertinoGlyph.Star, 17.dp, if (favorite) AureaColors.Accent else AureaColors.Muted)
        }
        if (onDelete != null) {
            Box(Modifier.size(40.dp).semantics { contentDescription = "Apagar preset" }.tocavel(onClick = onDelete), contentAlignment = Alignment.Center) {
                CupertinoIcon(CupertinoGlyph.Trash, 16.dp, AureaColors.Danger)
            }
        }
    }
}

@Composable
private fun Chip(label: String, on: Boolean, onClick: () -> Unit) {
    Box(
        Modifier.clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
            .tocavel(onClick = onClick).padding(horizontal = 10.dp, vertical = 6.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
    }
}

@Composable
private fun Action(label: String, onClick: () -> Unit) {
    Box(
        Modifier.clip(RoundedCornerShape(8.dp)).background(AureaColors.AccentDim).tocavel(onClick = onClick).padding(horizontal = 10.dp, vertical = 8.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)))
    }
}
