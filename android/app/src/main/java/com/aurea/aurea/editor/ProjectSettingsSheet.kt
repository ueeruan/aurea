package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.R
import com.aurea.aurea.state.CompositionSettings
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaDropdown
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.ds.ColorPickerSheet
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.ds.NumericKeypadSheet
import com.aurea.aurea.ui.i18n.AppText
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import java.util.Locale
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

// =============================================================================
// ⚙ Ajustes do projeto e do grupo (`project_settings_fragment`) — Omega chrome
// (área 02, P1-6): painel embaixo sobre véu de 30 %, fundo S9, raio 20 só em
// cima, entra subindo em 200 ms; ✕ e a lupa da busca no topo. Embaixo dos
// campos da referência, a seção "Mais do AUREA" com o que morava no menu da
// engrenagem (interface, prévia, edição, marcas, importar).
// =============================================================================

/** Proporções (largura / altura), na ordem da referência. */
private val Aspects = listOf("16:9" to 16f / 9f, "9:16" to 9f / 16f, "4:5" to 4f / 5f, "1:1" to 1f, "4:3" to 4f / 3f)

/** Resolução = o lado MENOR (o "1080p" vale para 16:9 e para 9:16). */
private val Resolutions = listOf(480 to "480p (SD)", 720 to "720p (HD)", 1080 to "1080p (FHD)", 1440 to "1440p (QHD)", 2160 to "2160p (4K)")
/** Atalhos de taxa; qualquer outra (1–240, 23,976, 29,97…) vem de "Personalizado…". */
private val FpsOptions = listOf(24, 25, 30, 50, 60, 90, 120, 144, 240)

/** Cores de fundo de um toque; "Outra cor…" abre o seletor. */
private val Backgrounds = listOf(R.string.sh_bg_black to floatArrayOf(0f, 0f, 0f), R.string.sh_bg_white to floatArrayOf(1f, 1f, 1f))

/** Ângulos do obturador do desfoque de movimento da composição. */
private val ShutterOptions = listOf(90, 180, 270, 360)

/**
 * O painel de ajustes. Fora de grupo (⚙ da barra do projeto): proporção,
 * tamanho livre, resolução, taxa de quadros, duração (extra do AUREA) e fundo.
 * Dentro de grupo (⚙ do grupo): largura × altura e fundo do GRUPO (a composição
 * aberta). Lê e escreve no motor (`store.composition`, a composição atual).
 */
@Composable
internal fun ProjectSettingsSheet(store: EditorStore, ui: EditorUi, onDismiss: () -> Unit) {
    val comp = store.composition
    val p = store.project
    val group = store.precompDepth > 0
    val w = comp?.width ?: p.width
    val h = comp?.height ?: p.height
    val aspect = if (h > 0) w.toFloat() / h else 0f
    val preset = Aspects.firstOrNull { abs(it.second - aspect) < 0.01f }?.first
    var free by remember { mutableStateOf(preset == null) }
    val shortSide = min(w, h)
    val fpsValue = comp?.fps ?: p.fps.toDouble()
    val bg = comp?.background ?: listOf(0f, 0f, 0f, 1f)
    // Alfa 0 = "Transparente" (regra do motor: Composition::set_background_choice).
    val bgTransparent = bg.getOrElse(3) { 1f } < 0.5f
    val bgName = if (bgTransparent) stringResource(R.string.sh_bg_transparent)
        else Backgrounds.firstOrNull { (_, c) -> (0..2).all { abs(c[it] - bg[it]) < 0.01f } }?.first
            ?.let { stringResource(it) } ?: stringResource(R.string.editor_personalizada)
    val widthLabel = stringResource(R.string.editor_largura)
    val heightLabel = stringResource(R.string.editor_altura)
    val durationLabel = stringResource(R.string.editor_duracao)
    val customFpsLabel = stringResource(R.string.project_fps_custom)
    val customFpsTitle = stringResource(R.string.project_fps_custom_title)

    var keypad by remember { mutableStateOf<KeypadRequest?>(null) }
    var pickingBackground by remember { mutableStateOf(false) }

    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        val enter = remember { Animatable(1f) }
        LaunchedEffect(Unit) { enter.animateTo(0f, tween(200)) }
        BoxWithConstraints(
            Modifier
                .fillMaxSize()
                .background(ShellColors.SettingsScrim)
                .testTag("projectSettings.scrim")
                .pointerInput(Unit) { detectTapGestures { onDismiss() } },
            contentAlignment = Alignment.BottomCenter,
        ) {
            val maxPanel = maxHeight * 0.85f
            Column(
                Modifier
                    .fillMaxWidth()
                    .heightIn(max = maxPanel)
                    .graphicsLayer { translationY = enter.value * size.height }
                    .clip(RoundedCornerShape(topStart = 20.dp, topEnd = 20.dp))
                    .background(OmegaChrome.Column)
                    .testTag("projectSettings")
                    // O toque dentro do painel não fecha.
                    .pointerInput(Unit) { detectTapGestures { } }
                    .windowInsetsPadding(WindowInsets.navigationBars),
            ) {
                // Cabeçalho: título, a lupa da busca de ferramentas e o ✕.
                Row(Modifier.fillMaxWidth().padding(start = 18.dp, end = 6.dp, top = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        stringResource(if (group) R.string.om_tb_group_settings else R.string.editor_projeto_cbe9),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)),
                        modifier = Modifier.weight(1f),
                    )
                    ChromeButton(
                        CupertinoGlyph.Search, stringResource(R.string.edt_cmd_search_desc),
                        onClick = { ui.sheet = ShellSheet.CommandSearch },
                        modifier = Modifier.testTag("commandSearchOpen"), size = 19.dp,
                    )
                    ChromeButton(
                        CupertinoGlyph.Xmark, stringResource(R.string.editor_fechar),
                        onClick = onDismiss, modifier = Modifier.testTag("projectSettings.close"), size = 18.dp,
                    )
                }
                Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState())) {
                    if (!group) {
                        // Proporção: no formato de cada uma; ✎ = tamanho livre. Os vãos também tocam.
                        Row(
                            Modifier.fillMaxWidth().height(56.dp).padding(horizontal = 30.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Aspects.forEach { (label, ratio) ->
                                AspectBox(label, ratio, on = !free && preset == label) {
                                    free = false
                                    if (comp != null && preset != label) applyAspect(store, comp, ratio, shortSide)
                                }
                            }
                            val freeLabel = stringResource(R.string.editor_tamanho_livre)
                            Box(
                                Modifier
                                    .size(40.dp)
                                    .clip(RoundedCornerShape(6.dp))
                                    .border(1.5.dp, if (free) AureaColors.Accent else AureaColors.Text, RoundedCornerShape(6.dp))
                                    .testTag("projectSettings.aspect.free")
                                    .semantics {
                                        contentDescription = freeLabel
                                        selected = free
                                    }
                                    .tocavel(haptic = true) { free = true },
                                contentAlignment = Alignment.Center,
                            ) {
                                CupertinoIcon(CupertinoGlyph.Pencil, 18.dp, if (free) AureaColors.Accent else AureaColors.Text)
                            }
                        }
                    }

                    if ((free || group) && comp != null) {
                        SettingLine(stringResource(if (group) R.string.om_gs_size else R.string.editor_tamanho)) {
                            Row(Modifier.width(200.dp), verticalAlignment = Alignment.CenterVertically) {
                                SizeBox("$w", Modifier.weight(1f).testTag(if (group) "groupSettings.size.w" else "projectSettings.size.w")) {
                                    keypad = KeypadRequest(widthLabel, w.toFloat(), "px", 16f, 8192f, 0) { v ->
                                        setSize(store, comp, v.roundToInt(), h)
                                    }
                                }
                                Text("×", modifier = Modifier.padding(horizontal = 8.dp), style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Muted)))
                                SizeBox("$h", Modifier.weight(1f).testTag(if (group) "groupSettings.size.h" else "projectSettings.size.h")) {
                                    keypad = KeypadRequest(heightLabel, h.toFloat(), "px", 16f, 8192f, 0) { v ->
                                        setSize(store, comp, w, v.roundToInt())
                                    }
                                }
                            }
                        }
                    }

                    if (!group) {
                        SettingLine(stringResource(R.string.editor_resolucao)) {
                            AureaDropdown(
                                Resolutions.firstOrNull { it.first == shortSide }?.second ?: "${shortSide}p",
                                tag = "projectSettings.resolution",
                                contentDescription = stringResource(R.string.editor_resolucao),
                                entries = Resolutions.map { (short, label) ->
                                    // Bug 6b (0.3.5): ids das opções (= iOS), para o teste de interface.
                                    MenuEntry.Item(label, selected = short == shortSide, tag = "projectSettings.resolution.$short") {
                                        val c = comp ?: return@Item
                                        val (nw, nh) = sizeFor(short, if (aspect > 0f) aspect else 16f / 9f)
                                        if (c.fits(nw, nh)) store.setCompositionSize(nw, nh)
                                        else store.showToast(capMessage(store, c))
                                    }
                                },
                            )
                        }

                        SettingLine(stringResource(R.string.editor_quadros_segundo)) {
                            val rounded = fpsValue.roundToInt()
                            val label = if (abs(fpsValue - rounded) < 0.01) "$rounded fps"
                            else String.format(Locale.ROOT, stringResource(R.string.editor_2f_fps), fpsValue).replace('.', ',')
                            AureaDropdown(
                                label,
                                tag = "projectSettings.fps",
                                contentDescription = stringResource(R.string.editor_quadros_segundo),
                                entries = FpsOptions.map { f ->
                                    // Omega 0.3.4: ids das opções (= iOS), para o teste de interface.
                                    MenuEntry.Item("$f fps", selected = abs(fpsValue - f) < 0.01, tag = "projectSettings.fps.$f") { store.setCompositionFps(f.toDouble()) }
                                } + MenuEntry.Item(customFpsLabel, selected = FpsOptions.none { abs(fpsValue - it) < 0.01 }, tag = "projectSettings.fps.custom") {
                                    // fps livre: o teclado limita a 1–240 e aceita decimais (29,97).
                                    keypad = KeypadRequest(customFpsTitle, fpsValue.toFloat(), "fps", 1f, 240f, 3) { v ->
                                        store.setCompositionFps(v.toDouble())
                                    }
                                },
                            )
                        }

                        // Extra do AUREA (sem par na referência): a duração da composição.
                        SettingLine(durationLabel) {
                            val seconds = (comp?.durationFrames ?: p.durationFrames) / fpsValue
                            SizeBox(String.format(Locale.getDefault(), "%.3f s", seconds), Modifier.width(180.dp).testTag("projectSettings.duration")) {
                                keypad = KeypadRequest(durationLabel, seconds.toFloat(), "s", (1.0 / fpsValue).toFloat(), 86400f, 3) { value ->
                                    store.setCompositionDuration(max(1, (value * fpsValue).roundToInt()))
                                }
                            }
                        }
                    }

                    SettingLine(stringResource(R.string.editor_plano_fundo)) {
                        AureaDropdown(
                            bgName,
                            tag = if (group) "groupSettings.background" else "projectSettings.background",
                            contentDescription = stringResource(R.string.editor_plano_fundo),
                            swatch = if (bgTransparent) Color.Transparent else Color(bg[0], bg[1], bg[2]),
                            checkerSwatch = bgTransparent,
                            entries = Backgrounds.map { (res, c) ->
                                val name = stringResource(res)
                                MenuEntry.Item(name, selected = name == bgName) { store.setCompositionBackground(c[0], c[1], c[2], 1f) }
                            } + MenuEntry.Item(stringResource(R.string.sh_bg_transparent), selected = bgTransparent) {
                                // Sem fundo: o motor guarda a cor e exporta PNG/GIF com alfa.
                                store.setCompositionBackground(bg[0], bg[1], bg[2], 0f)
                            } + MenuEntry.Item(stringResource(R.string.editor_outra_cor), selected = bgName == stringResource(R.string.editor_personalizada)) {
                                if (comp != null) pickingBackground = true
                            },
                        )
                    }

                    MoreInAurea(store, ui, onDismiss)
                    Spacer(Modifier.height(16.dp))
                }
            }
        }
    }

    keypad?.let { r -> NumericKeypadSheet(r, onDismiss = { keypad = null }) }
    if (pickingBackground && comp != null) {
        // Um passo de desfazer para o seletor inteiro; fecha também se ele
        // sair da tela sem o "Pronto". O fundo já é sRGB no motor.
        // Vindo do "Transparente", o seletor abre com a cor guardada, opaca.
        val initial = remember { comp.background.toFloatArray().also { if (it.size > 3) it[3] = 1f } }
        DisposableEffect(Unit) {
            store.beginGesture("fundo da composição")
            onDispose { store.endGesture() }
        }
        ColorPickerSheet(
            initial = initial,
            withAlpha = false,
            pickFromPreview = { store.captureBitmap(720) },
            previewIdentity = store.projectGeneration to comp.id,
            onChange = { r, g, b, _ -> store.setCompositionBackground(r, g, b, 1f) },
            onDone = { pickingBackground = false },
        )
    }
}

/**
 * "Mais do AUREA": o que o menu da engrenagem tinha e a referência não tem,
 * em linhas compactas de 40 dp — interface, prévia (qualidade, repetição, tela
 * cheia, desfoque da composição, diagnóstico), edição (ímã, keyframes de todas
 * as camadas, timeline magnética, remover vãos, aparar no cabeçote), marcas e
 * importar do After Effects.
 */
@Composable
private fun MoreInAurea(store: EditorStore, ui: EditorUi, onDismiss: () -> Unit) {
    fun act(block: () -> Unit): () -> Unit = {
        onDismiss()
        block()
    }
    Box(Modifier.padding(top = 12.dp).fillMaxWidth().height(1.dp).background(AureaColors.Border))
    MoreHeader(stringResource(R.string.om_settings_more), tag = "projectSettings.more")

    // Interface do projeto (0.2.5): Composição ou Edição, trocável a qualquer momento.
    Column(Modifier.fillMaxWidth().padding(start = 18.dp, end = 18.dp, top = 4.dp, bottom = 4.dp)) {
        Text(stringResource(R.string.ui_mode_label), style = AureaType.of(12f, FontWeight.W600, color = AureaColors.Muted))
        Spacer(Modifier.height(8.dp))
        com.aurea.aurea.home.UiModeChoice(store.uiMode) { store.setUiMode(it) }
    }

    MoreSubheader(stringResource(R.string.om_settings_preview))
    // Qualidade da prévia (D3): não muda a exportação.
    PreviewQualityLine(store)
    MoreToggle(stringResource(R.string.editor_reproducao_loop), store.looping, "projectSettings.more.loop") { store.setLoop(!store.looping) }
    MoreAction(
        stringResource(if (ui.fullscreen) R.string.editor_sair_tela_cheia else R.string.editor_tela_cheia),
        "projectSettings.more.fullscreen",
        act { ui.fullscreen = !ui.fullscreen },
    )
    MoreToggle(
        stringResource(R.string.editor_desfoque_movimento_composicao), store.compMotionBlur, "projectSettings.more.motionBlur",
        detail = stringResource(R.string.editor_camadas_desfoque_movimento_so_borram_isto),
    ) { store.setCompositionMotionBlur(!store.compMotionBlur) }
    if (store.compMotionBlur) {
        val shutter = store.shutterAngle.roundToInt()
        SettingLine(stringResource(R.string.sh_menu_shutter, shutter)) {
            AureaDropdown(
                "$shutter°",
                tag = "projectSettings.more.shutter",
                contentDescription = stringResource(R.string.sh_menu_shutter, shutter),
                entries = ShutterOptions.map { a -> MenuEntry.Item("$a°", selected = a == shutter) { store.changeShutterAngle(a.toFloat()) } },
            )
        }
    }
    MoreToggle(
        stringResource(R.string.editor_diagnostico_tela), store.hudVisible, "projectSettings.more.hud",
        detail = stringResource(R.string.editor_quadros_segundo_tempos_gpu_memoria_decodificador),
    ) { store.toggleHud() }

    MoreSubheader(stringResource(R.string.om_settings_editing))
    MoreToggle(stringResource(R.string.i18n_snapping), store.snapping, "projectSettings.more.snapping") { store.snapping = !store.snapping }
    MoreToggle(
        stringResource(R.string.editor_keyframes_todas_camadas), store.showAllKeyframes, "projectSettings.more.allKeys",
        detail = stringResource(R.string.editor_keyframes_todas_camadas_detalhe),
    ) { store.toggleShowAllKeyframes() }
    MoreToggle(
        stringResource(R.string.editor_timeline_magnetica_modo_edicao), store.editMode, "projectSettings.more.magnetic",
        detail = stringResource(R.string.editor_aparar_empurra_camadas_seguintes_excluir_fecha),
    ) { store.toggleEditMode() }
    MoreAction(stringResource(R.string.editor_remover_espacos_vazios), "projectSettings.more.removeGaps", act { store.removeGaps() })
    MoreAction(
        stringResource(R.string.editor_aparar_projeto_cabecote), "projectSettings.more.trimProject",
        if (store.playhead > 0) act { store.trimProjectAtPlayhead() } else null,
        detail = stringResource(R.string.sh_menu_cut_after, ShellTime.short(store.playhead, store.project.fps)),
    )

    MoreSubheader(stringResource(R.string.editor_marcas_ritmo))
    val marked = store.markers.frames.binarySearch(store.playhead) >= 0
    MoreToggle(stringResource(R.string.editor_marcar_ou_desmarcar_este_instante), marked, "projectSettings.more.marker") { store.toggleMarker() }
    MoreAction(stringResource(R.string.marker_edit), "projectSettings.more.editMarker", if (marked) act { store.editMarkerAtPlayhead() } else null)
    MoreAction(stringResource(R.string.editor_ir_proxima_marca), "projectSettings.more.nextMarker", if (store.markers.size > 0) act { store.seekToNextMarker() } else null)

    MoreSubheader(stringResource(R.string.om_settings_import))
    MoreAction(
        stringResource(R.string.ae_import_menu), "menu.aeImport",
        act { store.requestAeImport() },
        detail = stringResource(if (store.primary != null) R.string.ae_import_layer_detail else R.string.ae_import_new_layer_detail),
    )
}

/** Qualidade da prévia (Auto · Cheia · 1/2 · 1/4 · 1/8); segurar não precisa: o HUD tem linha própria. */
@Composable
private fun PreviewQualityLine(store: EditorStore) {
    val current = store.preview.scaleLabel
    val label = if (store.rawPlayback) "RAW" else when (current) {
        "FULL" -> stringResource(R.string.i18n_preview_full)
        "AUTO" -> "Auto"
        else -> current
    }
    // A reprodução RAW é ferramenta de teste (texto fixo em inglês): só no build de depuração.
    val testeRaw = com.aurea.aurea.BuildConfig.DEBUG
    val proxy = store.proxies.summary
    val title = stringResource(R.string.om_settings_preview_quality)
    SettingLine(title) {
        AureaDropdown(
            label,
            tag = "projectSettings.previewQuality",
            contentDescription = title,
            entries = listOfNotNull(
                if (proxy.generating > 0) MenuEntry.Item(stringResource(R.string.proxy_optimizing, proxy.percent), enabled = false) else null,
                if (testeRaw || store.rawPlayback) MenuEntry.Item(
                    if (store.rawPlayback) stringResource(R.string.edt_back_to_compositor) else "AUREA RAW PLAYBACK TEST",
                    selected = store.rawPlayback,
                ) { store.toggleRawPlayback() } else null,
                MenuEntry.Item("Auto", selected = current == "AUTO") { store.setPreviewScale(true) },
                MenuEntry.Item(stringResource(R.string.i18n_preview_full), selected = current == "FULL") { store.setPreviewScale(false, 1, 1) },
                MenuEntry.Item("1/2", selected = current == "1/2") { store.setPreviewScale(false, 1, 2) },
                MenuEntry.Item("1/4", selected = current == "1/4") { store.setPreviewScale(false, 1, 4) },
                MenuEntry.Item("1/8", selected = current == "1/8") { store.setPreviewScale(false, 1, 8) },
            ),
        )
    }
}

@Composable
private fun MoreHeader(title: String, tag: String) {
    Text(
        title,
        style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Text)),
        modifier = Modifier.testTag(tag).padding(start = 18.dp, end = 18.dp, top = 12.dp, bottom = 2.dp),
    )
}

@Composable
private fun MoreSubheader(title: String) {
    Text(
        title,
        style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = AureaColors.Muted)),
        modifier = Modifier.padding(start = 18.dp, end = 18.dp, top = 12.dp, bottom = 2.dp),
    )
}

/** Linha compacta de 40 dp com o interruptor à direita. */
@Composable
private fun MoreToggle(label: String, on: Boolean, tag: String, detail: String? = null, onToggle: () -> Unit) {
    val state = stringResource(if (on) R.string.common_on else R.string.common_off)
    Row(
        Modifier
            .fillMaxWidth()
            .heightIn(min = 40.dp)
            .testTag(tag)
            .semantics {
                role = Role.Switch
                stateDescription = state
            }
            .tocavel(shrink = 1f, haptic = true, onClick = onToggle)
            .padding(horizontal = 18.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Text)))
            if (detail != null) Text(detail, style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = AureaColors.Muted)))
        }
        Spacer(Modifier.width(10.dp))
        AureaToggle(checked = on, onCheckedChange = { onToggle() })
    }
}

/** Linha compacta de 40 dp que faz uma ação (apagada quando não vale agora). */
@Composable
private fun MoreAction(label: String, tag: String, onClick: (() -> Unit)?, detail: String? = null) {
    Row(
        Modifier
            .fillMaxWidth()
            .heightIn(min = 40.dp)
            .testTag(tag)
            .tocavel(enabled = onClick != null, shrink = 1f, haptic = true) { onClick?.invoke() }
            .padding(horizontal = 18.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = if (onClick != null) AureaColors.Text else ShellColors.DisabledMuted)))
            if (detail != null) Text(detail, style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = AureaColors.Muted)))
        }
        CupertinoIcon(CupertinoGlyph.ChevronRight, 14.dp, if (onClick != null) AureaColors.Muted else ShellColors.DisabledMuted)
    }
}

/** Troca a proporção mantendo o lado menor; não cabendo no teto, encolhe na proporção pedida. */
private fun applyAspect(store: EditorStore, c: CompositionSettings, ratio: Float, shortSide: Int) {
    var (nw, nh) = sizeFor(shortSide, ratio)
    if (!c.fits(nw, nh)) {
        val k = min(c.capLong.toFloat() / max(nw, nh), c.capShort.toFloat() / min(nw, nh))
        nw = even(nw * k)
        nh = even(nh * k)
    }
    store.setCompositionSize(nw, nh)
}

/** Tamanho livre: par e dentro do teto do aparelho (senão diz o teto). */
private fun setSize(store: EditorStore, c: CompositionSettings, width: Int, height: Int) {
    val nw = even(width.toFloat())
    val nh = even(height.toFloat())
    if (c.fits(nw, nh)) store.setCompositionSize(nw, nh)
    else store.showToast(capMessage(store, c))
}

/** O teto do aparelho, no idioma escolhido no app (fora do Compose). */
private fun capMessage(store: EditorStore, c: CompositionSettings): String =
    AppText.get(store.getApplication<Application>(), R.string.sh_device_exports_up_to, c.capLong, c.capShort)

/** Tamanho par com lado menor `short` na proporção `ratio` (largura / altura). */
private fun sizeFor(short: Int, ratio: Float): Pair<Int, Int> =
    if (ratio >= 1f) even(short * ratio) to even(short.toFloat()) else even(short.toFloat()) to even(short / ratio)

private fun even(v: Float): Int = max(2, (v / 2f).roundToInt() * 2)

/** Botão no formato da proporção (lado maior 40): borda branca, aceso no destaque. */
@Composable
private fun AspectBox(label: String, ratio: Float, on: Boolean, onClick: () -> Unit) {
    val description = stringResource(R.string.sh_aspect_ratio_desc, label)
    val long = 40f
    val bw = if (ratio >= 1f) long else long * ratio.coerceAtLeast(0.5f) + 6f
    val bh = if (ratio >= 1f) long / ratio + 6f else long
    val ink = if (on) AureaColors.Accent else AureaColors.Text
    // O alvo de toque inclui o vão em volta (os espaçadores também tocam).
    Box(
        Modifier
            .size(48.dp, 52.dp)
            .testTag("projectSettings.aspect.$label")
            .semantics {
                contentDescription = description
                selected = on
            }
            .tocavel(haptic = true, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier
                .size(bw.dp, bh.dp)
                .clip(RoundedCornerShape(4.dp))
                .border(1.5.dp, ink, RoundedCornerShape(4.dp)),
            contentAlignment = Alignment.Center,
        ) {
            Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 10.sp, fontWeight = FontWeight.W700, color = ink)))
        }
    }
}

/** Rótulo de 15 sp à esquerda e o controle (caixa de 180) à direita. */
@Composable
private fun SettingLine(label: String, content: @Composable () -> Unit) {
    Row(Modifier.fillMaxWidth().padding(horizontal = 18.dp, vertical = 5.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(
            label,
            maxLines = 2,
            style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Text)),
            modifier = Modifier.weight(1f).padding(end = 8.dp),
        )
        Box { content() }
    }
}

@Composable
private fun SizeBox(value: String, modifier: Modifier, onClick: () -> Unit) {
    Box(
        modifier
            .height(40.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(AureaColors.LightSurface)
            .tocavel(shrink = 1f, haptic = true, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(value, style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600, color = AureaColors.LightText)).merge(AureaType.Tabular))
    }
}
