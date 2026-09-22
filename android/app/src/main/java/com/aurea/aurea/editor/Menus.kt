package com.aurea.aurea.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaNamePrompt
import com.aurea.aurea.ui.ds.ColorPickerSheet
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import java.util.Locale
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

// =============================================================================
// ⋮ da camada — `menuDaCamada` (folha)
// =============================================================================

/**
 * Tudo o que se faz com UMA camada, com rótulo (A.01): camada, etiqueta,
 * recorte e grupo, na composição, mídia, tempo e o resto. O que o motor
 * novo já faz está ligado; o resto diz "em breve".
 */
@Composable
internal fun LayerMenuSheet(store: EditorStore, ui: EditorUi, onDismiss: () -> Unit) {
    val id = store.primary
    val index = store.layers.indexOfFirst { it.id == id }
    val row = store.layers.getOrNull(index)
    if (id == null || row == null) {
        onDismissLater(onDismiss)
        return
    }
    val type = LayerType.of(row.kind)
    val visual = type != LayerType.Audio
    val media = type == LayerType.Video || type == LayerType.Image
    val t = store.playhead
    val inside = t > row.startFrame && t < row.endFrame
    fun act(block: () -> Unit): () -> Unit = {
        onDismiss()
        block()
    }
    fun soon(feature: String) = act { store.comingSoon(feature) }
    fun timeAct(block: () -> Unit): () -> Unit = act {
        if (row.locked) store.showToast("Camada bloqueada: desbloqueie para editar") else block()
    }

    ShellMenuSheet(onDismiss) {
        MenuSection("Camada")
        // Bloqueada não se renomeia: o cadeado fecha a edição inteira.
        MenuItemRow(CupertinoGlyph.Pencil, "Renomear", if (row.locked) null else act { ui.sheet = ShellSheet.RenameLayer })
        // O cadeado não fecha a folha: o rótulo troca na hora.
        MenuItemRow(
            if (row.locked) ShellGlyph.LockOpenFill else CupertinoGlyph.LockFill,
            if (row.locked) "Desbloquear camada" else "Bloquear camada",
            { store.setLayerLocked(id, !row.locked) },
            detail = if (row.locked) "Volta a aceitar movimento e edição" else "Não aceita movimento, corte nem edição",
        )
        MenuItemRow(
            if (row.visible) CupertinoGlyph.EyeSlash else CupertinoGlyph.Eye,
            if (row.visible) "Ocultar camada" else "Mostrar camada",
            { store.setLayerVisible(id, !row.visible) },
        )
        MenuItemRow(CupertinoGlyph.PlusSquareOnSquare, "Duplicar", act { store.duplicateLayers(listOf(id)) })
        MenuItemRow(CupertinoGlyph.DocOnDoc, "Copiar camada", act { store.copyLayers(listOf(id)) })
        MenuItemRow(CupertinoGlyph.DocOnClipboard, "Colar camada no cabeçote", if (store.clipboard and 1 != 0) act { store.pasteLayers() } else null)
        MenuItemRow(CupertinoGlyph.Paintbrush, "Copiar estilo", act { store.select(id); store.copyStyle() })
        MenuItemRow(ShellGlyph.PaintbrushFill, "Colar estilo", if (store.clipboard and 2 != 0) act { store.pasteStyle(listOf(id)) } else null)
        MenuItemRow(CupertinoGlyph.ArrowUpToLine, "Trazer para a frente", if (index > 0) act { store.reorderLayer(id, index - 1) } else null)
        MenuItemRow(
            CupertinoGlyph.ArrowDownToLine,
            "Enviar para trás",
            if (index < store.layers.size - 1) act { store.reorderLayer(id, index + 1) } else null,
        )

        MenuSection("Etiqueta")
        LabelRow { store.comingSoon("Etiqueta") }

        if (visual) {
            MenuSection("Recorte e grupo")
            MenuItemRow(CupertinoGlyph.ArrowTurnLeftDown, "Recortar pela camada de baixo", soon("Recortar pela camada de baixo"))
            if (type != LayerType.Group) {
                MenuItemRow(CupertinoGlyph.RectangleStack, "Converter em grupo", act { store.precompose(listOf(id)) })
            } else {
                MenuItemRow(CupertinoGlyph.ArrowDownRightSquare, "Editar o grupo", act { store.openPrecomp(id) })
                MenuItemRow(ShellGlyph.SquareSplit2x2, "Desagrupar", act { store.ungroupPrecomp(id) })
                MenuItemRow(CupertinoGlyph.SquareStack3dDownRightFill, "Grupo de máscara", soon("Grupo de máscara"), detail = "A camada de cima mostra só o que cobre")
                MenuItemRow(CupertinoGlyph.SquareStack3dDownRight, "Grupo de recorte", soon("Grupo de recorte"), detail = "A camada de cima fura as de baixo")
            }
            MenuSection("Na composição")
            MenuItemRow(ShellGlyph.RectangleArrowUpRightArrowDownLeft, "Caber na composição", soon("Caber na composição"))
            MenuItemRow(CupertinoGlyph.Fullscreen, "Preencher a composição", soon("Preencher a composição"))
            MenuItemRow(ShellGlyph.ArrowUpLeftArrowDownRight, "Esticar até as bordas", soon("Esticar até as bordas"))
            MenuItemRow(CupertinoGlyph.ArrowLeftRightSquare, "Espelhar na horizontal", soon("Espelhar"))
            MenuItemRow(CupertinoGlyph.ArrowUpDownSquare, "Espelhar na vertical", soon("Espelhar"))
        }
        if (media || type == LayerType.Audio) {
            MenuSection("Mídia")
            MenuItemRow(CupertinoGlyph.InfoCircle, "Informações da mídia", soon("Informações da mídia"))
            if (type == LayerType.Video) {
                MenuItemRow(
                    CupertinoGlyph.MusicNote2,
                    "Extrair o áudio",
                    act { store.extractAudio(id) },
                    detail = "O som vira uma camada própria e o vídeo fica mudo",
                )
            }
            if (type == LayerType.Audio || type == LayerType.Video) {
                MenuItemRow(CupertinoGlyph.Speaker2, "Volume", act { openPanel(store, ui, EditorPanel.Audio) })
            }
        }

        MenuSection("Movimento")
        MenuItemRow(
            CupertinoGlyph.Speedometer,
            "Desfoque de movimento",
            act { store.setLayerMotionBlur(id, !(store.detail?.motionBlur ?: false)) },
            checked = store.detail?.motionBlur == true,
            detail = "Borra na direção do movimento (obturador nas configurações do projeto)",
        )
        if (type == LayerType.Video) {
            MenuItemRow(
                CupertinoGlyph.Speedometer,
                "Desfoque do movimento do vídeo",
                act { store.setVectorBlur(id, !(store.detail?.vectorBlur ?: false)) },
                checked = store.detail?.vectorBlur == true,
                detail = "Borra o que se mexe dentro do vídeo (pelos vetores de movimento)",
            )
        }

        MenuSection("Tempo")
        MenuItemRow(CupertinoGlyph.ArrowRightToLine, "Aparar o início no cabeçote", if (inside) timeAct { store.trimStart(id, t) } else null)
        MenuItemRow(CupertinoGlyph.Scissors, "Dividir no cabeçote", if (inside) timeAct { store.splitAtPlayhead(listOf(id)) } else null)
        MenuItemRow(CupertinoGlyph.ArrowLeftToLine, "Aparar o fim no cabeçote", if (inside) timeAct { store.trimEnd(id, t) } else null)
        if (type == LayerType.Video || type == LayerType.Audio) {
            MenuItemRow(CupertinoGlyph.Speedometer, "Velocidade e remapear o tempo", act { openPanel(store, ui, EditorPanel.Speed) })
        }
        if (type == LayerType.Video) {
            MenuItemRow(ShellGlyph.Snow, "Congelar quadro", if (inside) act { store.freezeFrame(id) } else null)
            MenuSection("Rastreio")
            MenuItemRow(ShellGlyph.Viewfinder, "Rastrear um ponto", act { store.select(id); store.beginPointPick(false) },
                detail = "Cria um Nulo que segue o ponto — ligue outras camadas a ele")
            MenuItemRow(ShellGlyph.Viewfinder, "Estabilizar pelo ponto", act { store.select(id); store.beginPointPick(true) },
                detail = "Move o vídeo para o ponto ficar parado na tela")
        }

        MenuSection("Mais")
        MenuItemRow(CupertinoGlyph.Link, "Seguir outra camada (parentesco)", act { openPanel(store, ui, EditorPanel.Parent) })
        MenuItemRow(CupertinoGlyph.SquareGrid2x2, "Todas as ações…", soon("Todas as ações"))
        MenuItemRow(CupertinoGlyph.Trash, "Excluir camada", act { LayerOps.delete(store, listOf(id)) }, danger = true)
    }
}

/** A linha de etiquetas: "sem" e as doze cores (círculos 24 num alvo de 34). */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun LabelRow(onPick: () -> Unit) {
    FlowRow(
        Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 4.dp),
        horizontalArrangement = Arrangement.spacedBy(2.dp),
        verticalArrangement = Arrangement.spacedBy(2.dp),
    ) {
        Box(Modifier.size(34.dp).tocavel(haptic = true, onClick = onPick), contentAlignment = Alignment.Center) {
            Box(
                Modifier.size(24.dp).border(1.5.dp, AureaColors.Muted, CircleShape),
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(ShellGlyph.Nosign, 14.dp, AureaColors.Muted)
            }
        }
        ShellColors.LabelPalette.forEach { color ->
            Box(Modifier.size(34.dp).tocavel(haptic = true, onClick = onPick), contentAlignment = Alignment.Center) {
                Box(Modifier.size(24.dp).background(color, CircleShape))
            }
        }
    }
}

/** Fecha a folha fora da composição (a camada sumiu enquanto ela abria). */
@Composable
private fun onDismissLater(onDismiss: () -> Unit) {
    androidx.compose.runtime.LaunchedEffect(Unit) { onDismiss() }
}

// =============================================================================
// ⋮ da linha do tempo — `menuDaTimeline` (folha)
// =============================================================================

/**
 * O que vale para o projeto inteiro ou para a linha do tempo: seleção,
 * reprodução, modo de prévia, miniatura, marcas, cronômetro, agrupar e guia.
 */
@Composable
internal fun TimelineMenuSheet(store: EditorStore, ui: EditorUi, onDismiss: () -> Unit) {
    val count = store.layers.size
    val now = ShellTime.short(store.playhead, store.project.fps)
    fun act(block: () -> Unit): () -> Unit = {
        onDismiss()
        block()
    }
    fun soon(feature: String) = act { store.comingSoon(feature) }
    ShellMenuSheet(onDismiss, maxHeightFraction = 0.78f) {
        MenuSection("Seleção")
        MenuItemRow(CupertinoGlyph.CheckmarkSquare, "Selecionar todas as camadas", if (count >= 2) act { store.selectAll() } else null)
        MenuItemRow(CupertinoGlyph.Square, "Limpar seleção", act { store.clearSelection() })

        MenuSection("Reprodução e prévia")
        MenuItemRow(CupertinoGlyph.Repeat, "Reprodução em loop", act { store.setLoop(!store.looping) }, checked = store.looping)
        MenuItemRow(
            CupertinoGlyph.Fullscreen,
            if (ui.fullscreen) "Sair da tela cheia" else "Tela cheia",
            act { ui.fullscreen = !ui.fullscreen },
        )
        MenuItemRow(CupertinoGlyph.Sparkles, "Prévia: Resultado final", act {}, checked = true, radio = true)
        MenuItemRow(ShellGlyph.WandRaysInverse, "Prévia: Sem efeitos", soon("Prévia sem efeitos"), checked = false, radio = true)
        MenuItemRow(CupertinoGlyph.CircleLefthalfFill, "Prévia: Selecionada a 50%", soon("Prévia da selecionada a 50%"), checked = false, radio = true)

        MenuSection("Edição")
        MenuItemRow(
            CupertinoGlyph.Link,
            "Timeline magnética (modo Edição)",
            act { store.toggleEditMode() },
            checked = store.editMode,
            detail = "Aparar empurra as camadas seguintes e excluir fecha o espaço",
        )
        MenuItemRow(ShellGlyph.ScissorsAlt, "Remover espaços vazios", act { store.removeGaps() })

        MenuSection("Projeto")
        MenuItemRow(
            ShellGlyph.ScissorsAlt,
            "Aparar o projeto no cabeçote",
            if (store.playhead > 0) act { store.trimProjectAtPlayhead() } else null,
            detail = "Corta tudo o que passa de $now",
        )
        MenuItemRow(CupertinoGlyph.Photo, "Usar este quadro como miniatura", soon("Miniatura do projeto"))
        MenuItemRow(
            CupertinoGlyph.ArrowRightToLine,
            "Marcar aqui o fim da introdução",
            soon("Marca da introdução"),
            detail = "Esticado noutro projeto, a introdução toca intacta",
        )
        MenuItemRow(
            CupertinoGlyph.ArrowLeftToLine,
            "Marcar aqui o começo do final",
            soon("Marca do final"),
            detail = "Esticado noutro projeto, o final toca intacto",
        )

        MenuSection("Marcas e ritmo")
        MenuItemRow(CupertinoGlyph.Bookmark, "Marcar (ou desmarcar) este instante", act { store.toggleMarker() })
        MenuItemRow(ShellGlyph.BookmarkSolid, "Ir para a próxima marca", if (store.markers.size > 0) act { store.seekToNextMarker() } else null)
        MenuItemRow(CupertinoGlyph.MusicNote2, "Detectar batidas da camada escolhida", act { store.detectBeats() })

        MenuSection("Cronômetro de edição")
        MenuItemRow(
            CupertinoGlyph.Timer,
            "Iniciar o cronômetro",
            soon("Cronômetro de edição"),
            detail = "Conta o tempo que você passa editando este projeto",
        )

        MenuSection("Mais")
        MenuItemRow(
            CupertinoGlyph.RectangleStack, "Agrupar as camadas escolhidas",
            if (store.selection.isNotEmpty()) act { store.precompose() } else null,
        )
        MenuItemRow(CupertinoGlyph.Book, "Guia rápido", soon("Guia rápido"))
    }
}

// =============================================================================
// ⚙ Projeto — `showProjectSettingsSheet` (A.01: `folhaDoEstudio`)
// =============================================================================

private val Aspects = listOf("16:9" to 16f / 9f, "9:16" to 9f / 16f, "1:1" to 1f, "4:5" to 4f / 5f, "4:3" to 4f / 3f)
private val Resolutions = listOf(720 to "HD 720p", 1080 to "Full HD 1080p", 1440 to "QHD 1440p", 2160 to "4K 2160p")
private val FpsOptions = listOf(24, 30, 60)
private val ShutterOptions = listOf(90, 180, 270, 360)

/**
 * Os ajustes do projeto, lidos e escritos no motor (`store.composition`).
 * Proporção mantém o lado menor; resolução mantém a proporção; trocar a taxa
 * preserva os segundos (o motor reescala os tempos). O fundo abre o seletor
 * de cor num passo só de desfazer. O nome renomeia de verdade, e
 * "Diagnóstico na tela" liga o HUD de desempenho.
 */
@Composable
internal fun ProjectSettingsSheet(store: EditorStore, ui: EditorUi, onDismiss: () -> Unit) {
    val p = store.project
    val comp = store.composition
    var renaming by remember { mutableStateOf(false) }
    var pickingBackground by remember { mutableStateOf(false) }
    val w = comp?.width ?: p.width
    val h = comp?.height ?: p.height
    val aspect = if (h > 0) w.toFloat() / h else 0f
    val currentAspect = Aspects.firstOrNull { abs(it.second - aspect) < 0.01f }?.first
    val shortSide = min(w, h)
    val fps = (comp?.fps ?: p.fps.toDouble()).roundToInt()
    val seconds = if (fps > 0) (comp?.durationFrames ?: p.durationFrames).toFloat() / (comp?.fps?.toFloat() ?: p.fps) else 0f
    ShellMenuSheet(onDismiss, maxHeightFraction = 0.82f, scrim = ShellColors.SettingsScrim, handle = ShellColors.SheetHandle) {
        Text(
            "Projeto",
            style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W700)),
            modifier = Modifier.padding(start = 18.dp, top = 12.dp, end = 18.dp, bottom = 4.dp),
        )
        SettingRow(CupertinoGlyph.Pencil, p.title.ifBlank { "(Sem título)" }, "Toque para renomear", chevron = true) { renaming = true }

        SettingSection("Composição")
        ChipsRow("Proporção", Aspects.map { it.first }, currentAspect) { label ->
            val c = comp ?: return@ChipsRow
            val ratio = Aspects.first { it.first == label }.second
            var (nw, nh) = sizeFor(shortSide, ratio)
            if (!c.fits(nw, nh)) {
                // Não cabe mantendo o lado menor: encolhe até caber, na proporção pedida.
                val k = min(c.capLong.toFloat() / max(nw, nh), c.capShort.toFloat() / min(nw, nh))
                nw = even(nw * k)
                nh = even(nh * k)
            }
            store.setCompositionSize(nw, nh)
        }
        ChipsRow("Resolução", Resolutions.map { it.second }, Resolutions.firstOrNull { it.first == shortSide }?.second) { label ->
            val c = comp ?: return@ChipsRow
            val short = Resolutions.first { it.second == label }.first
            val (nw, nh) = sizeFor(short, if (aspect > 0f) aspect else 16f / 9f)
            if (c.fits(nw, nh)) {
                store.setCompositionSize(nw, nh)
            } else {
                store.showToast("Este aparelho exporta até ${c.capLong} × ${c.capShort}")
            }
        }
        ChipsRow("Quadros", FpsOptions.map { it.toString() }, FpsOptions.firstOrNull { it == fps }?.toString()) { label ->
            store.setCompositionFps(label.toDouble())
        }
        SettingRow(
            ShellGlyph.SquareFill,
            "Fundo da composição",
            "$w × $h · ${String.format(Locale.ROOT, "%.1f", seconds).replace('.', ',')} s",
            onClick = if (comp != null) ({ pickingBackground = true }) else null,
        )

        SettingSection("Preview")
        SettingRow(ShellGlyph.SquareStack3dDownDottedline, "Casca de cebola", "Desligada") { store.comingSoon("Casca de cebola") }

        SettingSection("Guias")
        SettingRow(ShellGlyph.RectangleDock, "Áreas seguras", "Margens de título e ação no preview", switch = false) { store.comingSoon("Áreas seguras") }
        ChipsRow("Colunas", listOf("Sem", "2", "3", "4", "6", "12"), "Sem") { store.comingSoon("Colunas") }
        SettingRow(CupertinoGlyph.LineHorizontal3, "Adicionar guia vertical", "0 vertical, 0 horizontal") { store.comingSoon("Guias") }
        SettingRow(CupertinoGlyph.LineHorizontal3, "Adicionar guia horizontal") { store.comingSoon("Guias") }

        SettingSection("Motion blur da composição")
        SettingRow(
            CupertinoGlyph.Speedometer,
            "Motion blur",
            if (store.compMotionBlur) "Ligado · obturador de ${store.shutterAngle.roundToInt()}°"
            else "Desligado (as camadas com motion blur só borram com isto ligado)",
            switch = store.compMotionBlur,
        ) { store.setCompositionMotionBlur(!store.compMotionBlur) }
        ChipsRow("Obturador", ShutterOptions.map { "$it°" }, ShutterOptions.firstOrNull { it == store.shutterAngle.roundToInt() }?.let { "$it°" }) { label ->
            store.changeShutterAngle(label.removeSuffix("°").toFloat())
        }

        SettingSection("Paleta do projeto")
        SettingRow(CupertinoGlyph.AddCircled, "Adicionar cor à paleta") { store.comingSoon("Paleta do projeto") }

        SettingSection("Propriedades expostas (template)")
        SettingRow(
            CupertinoGlyph.SliderHorizontal3,
            "Nenhuma propriedade exposta",
            "Exponha um parâmetro para quem usar este projeto como template.",
            onClick = null,
        )

        SettingSection("Dados (CSV)")
        SettingRow(
            ShellGlyph.Table,
            "Carregar CSV",
            "Colunas viram fontes para textos (vincular no painel do texto)",
            chevron = true,
        ) { store.comingSoon("Dados (CSV)") }

        SettingSection("Ajuda")
        SettingRow(CupertinoGlyph.QuestionCircle, "Como usar o editor", "Guia rápido, com busca", chevron = true) { store.comingSoon("Guia rápido") }
        SettingRow(CupertinoGlyph.Lightbulb, "Ver as dicas de novo", "As quatro dicas de primeiro uso voltam ao abrir o editor") {
            store.comingSoon("Dicas de primeiro uso")
        }
        SettingRow(
            ShellGlyph.WaveformPathEcg,
            "Diagnóstico na tela",
            "Quadros por segundo, tempos da GPU, memória e o decodificador.",
            switch = store.hudVisible,
        ) { store.toggleHud() }
        Spacer(Modifier.height(12.dp))
    }
    if (renaming) {
        AureaNamePrompt(
            title = "Nome do projeto",
            initial = p.title,
            onConfirm = { store.renameProject(it) },
            onDismiss = { renaming = false },
        )
    }
    if (pickingBackground && comp != null) {
        // Um passo de desfazer para a folha inteira; fecha também se ela
        // sair da tela sem o "Pronto".
        // O fundo já é sRGB no motor (ao contrário das cores de efeito).
        val initial = remember { comp.background.toFloatArray() }
        DisposableEffect(Unit) {
            store.beginGesture("fundo da composição")
            onDispose { store.endGesture() }
        }
        ColorPickerSheet(
            initial = initial,
            withAlpha = false,
            onChange = { r, g, b, _ -> store.setCompositionBackground(r, g, b, 1f) },
            onDone = { pickingBackground = false },
        )
    }
}

/** Tamanho par com lado menor `short` na proporção `ratio` (largura / altura). */
private fun sizeFor(short: Int, ratio: Float): Pair<Int, Int> =
    if (ratio >= 1f) even(short * ratio) to even(short.toFloat()) else even(short.toFloat()) to even(short / ratio)

private fun even(v: Float): Int = max(2, (v / 2f).roundToInt() * 2)

@Composable
private fun SettingSection(title: String) {
    Text(
        title.uppercase(Locale.ROOT),
        style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, letterSpacing = 0.6.sp, fontWeight = FontWeight.W600, color = AureaColors.Muted)),
        modifier = Modifier.padding(start = 18.dp, top = 14.dp, end = 18.dp, bottom = 4.dp),
    )
}

/** `LinhaDoEstudio`: ícone 20, título 15, subtítulo 11,5; fim = switch ou seta. */
@Composable
private fun SettingRow(
    glyph: Char,
    title: String,
    subtitle: String? = null,
    chevron: Boolean = false,
    switch: Boolean? = null,
    onClick: (() -> Unit)?,
) {
    val color = if (onClick == null && switch == null) AureaColors.Muted else AureaColors.Text
    Row(
        Modifier
            .fillMaxWidth()
            .tocavel(enabled = onClick != null, shrink = 1f) { onClick?.invoke() }
            .padding(horizontal = 18.dp, vertical = 11.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CupertinoIcon(glyph, 20.dp, color)
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            Text(title, maxLines = 1, overflow = TextOverflow.Ellipsis, style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = color)))
            if (subtitle != null) {
                Text(
                    subtitle,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                    style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, lineHeight = 15.sp, color = AureaColors.Muted)),
                )
            }
        }
        when {
            switch != null -> ShellSwitch(switch)
            chevron -> CupertinoIcon(CupertinoGlyph.ChevronRight, 15.dp, AureaColors.Muted)
        }
    }
}

/** `_Chips`: rótulo na coluna de 84, pílulas que quebram linha. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ChipsRow(label: String, options: List<String>, selected: String?, onPick: (String) -> Unit) {
    Row(Modifier.fillMaxWidth().padding(horizontal = 18.dp, vertical = 6.dp)) {
        Text(
            label,
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)),
            modifier = Modifier.width(84.dp).padding(top = 7.dp),
        )
        FlowRow(
            Modifier.weight(1f),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            options.forEach { o ->
                val on = o == selected
                Text(
                    o,
                    style = AureaType.Base.merge(
                        TextStyle(fontSize = 12.5.sp, fontWeight = FontWeight.W600, color = if (on) AureaColors.Accent else AureaColors.Text),
                    ),
                    modifier = Modifier
                        .clip(RoundedCornerShape(9.dp))
                        .background(if (on) AureaColors.ActionDim else AureaColors.Chip)
                        .then(if (on) Modifier.border(1.dp, AureaColors.Action, RoundedCornerShape(9.dp)) else Modifier)
                        .tocavel(shrink = 1f) { if (!on) onPick(o) }
                        .padding(horizontal = 11.dp, vertical = 7.dp),
                )
            }
        }
    }
}
