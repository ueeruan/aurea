package com.aurea.aurea.editor

import android.content.Context
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.editor.panels.*
import com.aurea.aurea.presets.PresetEntry
import com.aurea.aurea.presets.PresetKind
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import org.json.JSONArray

internal data class EditorCommand(val id: String, val title: String, val detail: String, val keywords: String, val requires: String)
private data class CommandHit(val id: String, val title: String, val detail: String, val search: String,
                              val category: String, val requires: String, val effect: Int? = null, val preset: PresetEntry? = null)

// Stable IDs and capabilities come from the same resource on both platforms.
internal fun readEditorCommands(context: Context): List<EditorCommand> {
    val array = JSONArray(context.assets.open("editor_commands.json").bufferedReader().use { it.readText() })
    return (0 until array.length()).map { i -> array.getJSONObject(i).let {
        EditorCommand(it.getString("id"), it.getString("title"), it.getString("detail"), it.getString("keywords"), it.getString("requires"))
    } }
}

private fun commandUnavailable(requirement: String, store: EditorStore): String? {
    val selected = store.layers.filter { it.id in store.selection }
    if (requirement == "any") return null
    if (requirement == "layers") return if (store.layers.isNotEmpty()) null else "Adicione uma camada"
    if (requirement == "undo") return if (store.project.canUndo) null else "Nenhuma edição para desfazer"
    if (requirement == "redo") return if (store.project.canRedo) null else "Nenhuma edição para refazer"
    if (selected.isEmpty()) return "Selecione uma camada"
    if (selected.any { it.locked }) return "Desbloqueie a seleção para editar"
    if (requirement == "selection") return null
    if (requirement == "inside") return if (selected.any { store.playhead > it.startFrame && store.playhead < it.endFrame }) null else "Posicione o cabeçote dentro do clipe"
    val layer = selected.singleOrNull() ?: return "Selecione apenas uma camada"
    return when (requirement) {
        "single" -> null
        "inside_single" -> if (store.playhead > layer.startFrame && store.playhead < layer.endFrame) null else "Posicione o cabeçote dentro do clipe"
        "video", "video_inside" -> if (layer.kind != 1) "Selecione um vídeo" else if (requirement == "video_inside" && store.playhead !in layer.startFrame until layer.endFrame) "Posicione o cabeçote dentro do vídeo" else null
        "media" -> if (layer.kind in listOf(1, 3)) null else "Selecione vídeo ou áudio"
        "text" -> if (layer.kind == 4) null else "Selecione um texto"
        "visual" -> if (layer.kind != 3) null else "Selecione uma camada visual"
        "text3d" -> if (store.text3d != null) null else "Selecione um texto 3D"
        "model3d" -> if (layer.kind == 10) null else "Selecione um modelo 3D"
        "particles" -> if (layer.kind == 11) null else "Selecione partículas"
        "vector" -> if (store.isVectorLayer) null else "Selecione um vetor"
        "precomp" -> if (layer.kind == 12) null else "Selecione uma pré-composição"
        else -> "Ação indisponível neste contexto"
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun CommandSearchSheet(store: EditorStore, ui: EditorUi, onDismiss: () -> Unit) {
    val context = LocalContext.current
    val commands = remember { readEditorCommands(context) }
    val preferences = remember { context.getSharedPreferences("aurea.commands", Context.MODE_PRIVATE) }
    var favorites by remember { mutableStateOf(preferences.getStringSet("favorites", emptySet()).orEmpty().toSet()) }
    var query by rememberSaveable { mutableStateOf("") }
    var category by rememberSaveable { mutableStateOf("Tudo") }
    val presets = store.presets.all()
    // Reuse existing catalogs and preference stores; no duplicate effect/preset system.
    val hits = commands.map { CommandHit(it.id, it.title, it.detail, normalizeSearch("${it.title} ${it.keywords} ${it.detail}"), "Ações", it.requires) } +
        store.catalog.map { CommandHit("effect:${it.typeId}", effectDisplayName(it.typeId, it.name), "Adicionar efeito · ${it.category}",
            effectSearchText(it.typeId, it.name, it.category), "Efeitos", if (it.typeId == effectTypeId("aurea.text3d.layout")) "text3d" else "selection", effect = it.typeId) } +
        presets.map { CommandHit("preset:${it.key}", it.name, "Abrir preset · ${it.kind.dir}", normalizeSearch("${it.name} ${it.kind.dir} preset"),
            "Presets", if (it.kind == PresetKind.Text) "text" else "single", preset = it) }
    fun isFavorite(hit: CommandHit) = hit.effect?.let { store.effectPrefs.isFavorite(it) }
        ?: hit.preset?.let { it.key in store.presets.favorites } ?: (hit.id in favorites)
    val terms = normalizeSearch(query).split(Regex("\\s+")).filter { it.isNotBlank() }
    val filtered = hits.filter { hit ->
        (category == "Tudo" || category == hit.category || category == "Favoritos" && isFavorite(hit)) &&
            terms.all { it in hit.search } &&
            (terms.isNotEmpty() || category != "Tudo" || isFavorite(hit) || hit.category == "Ações" && commandUnavailable(hit.requires, store) == null)
    }.sortedByDescending { isFavorite(it) }

    ModalBottomSheet(onDismissRequest = onDismiss, sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true), containerColor = AureaColors.EditorPanel) {
        Column(Modifier.fillMaxWidth().fillMaxHeight(0.88f).padding(horizontal = 16.dp).imePadding()) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Ferramentas", color = AureaColors.Text, fontSize = 20.sp, fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
                TextButton(onClick = onDismiss) { Text("Fechar") }
            }
            OutlinedTextField(query, { query = it }, modifier = Modifier.fillMaxWidth().semantics { contentDescription = "Buscar ferramentas" },
                singleLine = true, placeholder = { Text("Buscar ação, efeito ou preset") })
            androidx.compose.foundation.lazy.LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                items(listOf("Tudo", "Favoritos", "Ações", "Efeitos", "Presets")) { name -> FilterChip(selected = category == name, onClick = { category = name }, label = { Text(name) }) }
            }
            if (filtered.isEmpty()) Text(if (category == "Favoritos") "Toque na estrela de uma ferramenta para guardar aqui." else "Nenhuma ferramenta encontrada.", color = AureaColors.Muted, modifier = Modifier.padding(vertical = 20.dp))
            LazyColumn(Modifier.weight(1f)) {
                items(filtered, key = { it.id }) { hit ->
                    val reason = commandUnavailable(hit.requires, store)
                    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f).heightIn(min = 64.dp).clickable(enabled = reason == null) {
                            // Resolve against the current selection, never a captured layer ID.
                            if (commandUnavailable(hit.requires, store) == null) {
                                onDismiss()
                                when {
                                    hit.effect != null -> { store.addEffect(hit.effect); store.effectPrefs.addRecent(hit.effect); openPanel(store, ui, EditorPanel.Effects) }
                                    hit.preset != null -> { store.presetsOpenKind = hit.preset.kind; store.presetsOpenSearch = hit.preset.name; openPanel(store, ui, EditorPanel.Presets) }
                                    else -> executeEditorCommand(hit.id, store, ui)
                                }
                            }
                        }.padding(vertical = 10.dp, horizontal = 4.dp)) {
                            Text(hit.title, color = if (reason == null) AureaColors.Text else AureaColors.Muted, fontSize = 15.sp)
                            Text(reason ?: hit.detail, color = AureaColors.Muted, fontSize = 12.sp)
                        }
                        TextButton(onClick = {
                            when {
                                hit.effect != null -> store.effectPrefs.toggleFavorite(hit.effect)
                                hit.preset != null -> store.presets.toggleFavorite(hit.preset)
                                else -> { favorites = if (hit.id in favorites) favorites - hit.id else favorites + hit.id; preferences.edit().putStringSet("favorites", favorites).apply() }
                            }
                        }, modifier = Modifier.size(48.dp).semantics { contentDescription = "${if (isFavorite(hit)) "Remover dos" else "Adicionar aos"} favoritos: ${hit.title}" }) {
                            Text(if (isFavorite(hit)) "★" else "☆", color = AureaColors.Accent, fontSize = 24.sp)
                        }
                    }
                }
            }
        }
    }
}

private fun executeEditorCommand(id: String, store: EditorStore, ui: EditorUi) {
    val layer = store.primary
    fun panel(value: EditorPanel) = openPanel(store, ui, value)
    when (id) {
        "split" -> store.splitAtPlayhead()
        "duplicate" -> store.duplicateLayers()
        "ripple_delete" -> store.deleteLayers(ripple = true)
        "trim_start" -> layer?.let { store.trimStart(it, store.playhead) }
        "trim_end" -> layer?.let { store.trimEnd(it, store.playhead) }
        "speed" -> panel(EditorPanel.Speed)
        "clip_edit" -> panel(EditorPanel.ClipEdit)
        "freeze" -> store.freezeFrame()
        "extract_audio" -> store.extractAudio()
        "audio" -> panel(EditorPanel.Audio)
        "beats" -> store.detectBeats()
        "transform" -> panel(EditorPanel.Transform)
        "text" -> panel(EditorPanel.Text)
        "mask" -> panel(EditorPanel.Mask)
        "effects" -> panel(EditorPanel.Effects)
        "appearance" -> panel(EditorPanel.Appearance)
        "tracking" -> panel(EditorPanel.Tracking)
        "environment" -> panel(EditorPanel.Element3D)
        "particles" -> panel(EditorPanel.Particles)
        "vector" -> panel(EditorPanel.Vector)
        "presets" -> panel(EditorPanel.Presets)
        "precompose" -> store.precompose()
        "enter_precomp" -> layer?.let { store.openPrecomp(it) }
        "marker" -> store.toggleMarker()
        "magnetic" -> store.toggleEditMode()
        "remove_gaps" -> store.removeGaps()
        "previous_frame" -> store.step(-1)
        "next_frame" -> store.step(1)
        "add_text" -> store.addText()
        "add_null" -> store.addNull(false)
        "add_null3d" -> store.addNull(true)
        "add_camera" -> store.addCamera()
        "captions" -> panel(EditorPanel.Captions)
        "project_settings" -> openSheet(store, ui, ShellSheet.ProjectSettings)
        "search_layers" -> openSheet(store, ui, ShellSheet.SearchLayers)
        "undo" -> store.undo()
        "redo" -> store.redo()
    }
}
