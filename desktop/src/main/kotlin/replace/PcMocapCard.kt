package com.aurea.aurea.editor

import android.net.Uri
import androidx.activity.result.contract.ActivityResultContracts.PickVisualMedia
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.draganddrop.dragAndDropTarget
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.Modifier
import androidx.compose.ui.draganddrop.DragAndDropEvent
import androidx.compose.ui.draganddrop.DragAndDropTarget
import androidx.compose.ui.draganddrop.DragData
import androidx.compose.ui.draganddrop.dragData
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.ActionCard
import com.aurea.aurea.editor.panels.HumanRow
import com.aurea.aurea.editor.panels.KitHint
import com.aurea.aurea.editor.panels.KitTitle
import com.aurea.aurea.editor.panels.MocapSectionHost
import com.aurea.aurea.editor.panels.PanelEnv
import com.aurea.aurea.editor.panels.ToggleLine
import com.aurea.aurea.mocap.MocapFlow
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import com.aurea.pc.PcFileDialogs
import kotlin.math.roundToInt

/**
 * MOTION CAPTURE na Cena 3D do Aurea PC: um cartão flutuante do Aurea, simples
 * — soltar (ou escolher) o vídeo → análise com progresso → prévia no modelo →
 * "Aplicar no modelo selecionado". Conduz o MESMO MocapFlow do painel do
 * celular (MocapSection.kt): as regras estão no motor (Engine::mocap_*), a
 * pose vem do BlazePose do motor no PC (replace/Replacements4.kt).
 *
 * Abre pelo menu Objeto ▸ Motion Capture… da cabeçalho e pela aba Rig das
 * Propriedades (que, na Cena 3D do PC, mostra só o botão: dois fluxos ao mesmo
 * tempo brigariam pela sessão única de mocap do motor).
 */
object PcMocap {
    var open by mutableStateOf(false)
    /** Canto superior esquerdo (dp); NaN = ainda não arrastado. */
    var x by mutableStateOf(Float.NaN)
    var y by mutableStateOf(Float.NaN)
    var adjust by mutableStateOf(false)

    private val VIDEO = setOf("mp4", "mov", "m4v", "mkv", "webm", "avi", "3gp", "mts", "m2ts", "wmv")
    fun isVideo(uri: Uri): Boolean = uri.lastPathSegment?.substringAfterLast('.', "")?.lowercase() in VIDEO
}

/** Liga o botão do Motion Capture no painel do rig enquanto a Cena 3D do PC está aberta. */
@Composable
internal fun PcMocapRigHook() {
    DisposableEffect(Unit) {
        val entry: @Composable (PanelEnv) -> Unit = { env -> PcMocapRigEntry(env) }
        MocapSectionHost.override = entry
        onDispose {
            MocapSectionHost.override = null
            PcMocap.open = false
        }
    }
}

@Composable
private fun PcMocapRigEntry(@Suppress("UNUSED_PARAMETER") env: PanelEnv) {
    Spacer(Modifier.height(12.dp))
    KitTitle(stringResource(R.string.mocap_title))
    KitHint(stringResource(R.string.pc_mocap_rig_hint))
    Spacer(Modifier.height(8.dp))
    ActionCard(stringResource(R.string.pc_mocap_open), null) { PcMocap.open = true }
}

/** O cartão (na área de trabalho da Cena 3D, por cima da viewport). */
@Composable
internal fun PcMocapCardHost(store: EditorStore, maxW: Float, maxH: Float) {
    if (!PcMocap.open) return
    val density = LocalDensity.current
    val w = 340f
    if (PcMocap.x.isNaN()) {
        PcMocap.x = 64f
        PcMocap.y = 54f
    }
    val x = PcMocap.x.coerceIn(0f, (maxW - 80f).coerceAtLeast(0f))
    val y = PcMocap.y.coerceIn(0f, (maxH - 40f).coerceAtLeast(0f))
    val shape = RoundedCornerShape(12.dp)
    val layerName = store.layers.firstOrNull { it.id == store.primary }?.name.orEmpty()
    Column(
        Modifier.offset { IntOffset((x * density.density).roundToInt(), (y * density.density).roundToInt()) }
            .width(w.dp).heightIn(max = (maxH - y - 8f).coerceAtLeast(120f).dp)
            .shadow(18.dp, shape).clip(shape)
            .background(AureaColors.EditorPanel).border(1.dp, AureaColors.Hairline, shape),
    ) {
        // Título (arrasta o cartão): ícone, nome, camada, ✕.
        Row(
            Modifier.fillMaxWidth().height(42.dp)
                .pointerInput(Unit) {
                    detectDragGestures { c, d ->
                        c.consume()
                        PcMocap.x = (PcMocap.x + d.x / density.density).coerceIn(0f, (maxW - 80f).coerceAtLeast(0f))
                        PcMocap.y = (PcMocap.y + d.y / density.density).coerceIn(0f, (maxH - 40f).coerceAtLeast(0f))
                    }
                }
                .padding(horizontal = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(Modifier.size(28.dp), contentAlignment = Alignment.Center) { CupertinoIcon(CupertinoGlyph.Person3Fill, 16.dp, AureaColors.Accent) }
            Spacer(Modifier.width(4.dp))
            Column(Modifier.weight(1f)) {
                Text(stringResource(R.string.mocap_title), color = AureaColors.Text, fontSize = 13.sp, fontWeight = FontWeight.Medium,
                    maxLines = 1, overflow = TextOverflow.Ellipsis)
                if (layerName.isNotEmpty()) Text(layerName, color = AureaColors.Subtle, fontSize = 10.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
            val close = stringResource(R.string.pc_close)
            Box(Modifier.size(28.dp).clip(RoundedCornerShape(6.dp)).semantics { contentDescription = close }
                .tocavel(shrink = 1f) { PcMocap.open = false }, contentAlignment = Alignment.Center) {
                CupertinoIcon(CupertinoGlyph.Xmark, 13.dp, AureaColors.Muted)
            }
        }
        Box(Modifier.fillMaxWidth().height(1.dp).background(AureaColors.Hairline))
        Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(horizontal = 14.dp, vertical = 12.dp)) {
            // Nada (ou outra coisa) selecionado: pega sozinho o modelo 3D da cena, de preferência
            // um com esqueleto. Pedido do dono: abriu, soltou o vídeo, pronto.
            LaunchedEffect(store.primary, store.layers.size) {
                val cur = store.layers.firstOrNull { it.id == store.primary }
                if (cur?.kind != LayerType.Model3D.kind) {
                    store.layers.firstOrNull { it.kind == LayerType.Model3D.kind }?.let { store.select(it.id, openOptions = false) }
                }
            }
            val layer = store.primary
            val row = store.layers.firstOrNull { it.id == layer }
            // Modelo importado (forma, texto e imagem 3D não têm esqueleto).
            val model = layer != null && row?.kind == LayerType.Model3D.kind && store.shape3d == null && store.text3d == null && store.image3d == null
            if (!model || layer == null) {
                KitHint(stringResource(R.string.pc_mocap_select_model))
            } else {
                key(layer, store.projectGeneration) { MocapCardBody(store, layer) }
            }
        }
    }
}

@Composable
private fun MocapCardBody(store: EditorStore, layer: Long) {
    val scope = rememberCoroutineScope()
    val context = LocalContext.current
    val flow = remember(layer) { MocapFlow(store, layer, scope, context) }
    DisposableEffect(flow) { onDispose { flow.dispose() } }
    // Depois do AutoRig 3D: o rig apareceu?
    LaunchedEffect(flow, store.curveRevision, store.humanoidStatus, store.autoRigState) { flow.recheckRig() }
    // Pedido do dono: soltou o vídeo, rastreou, já aplica em todos os ossos (sem prévia nem botão).
    // `applied` volta a -1 a cada vídeo novo, então aplica uma vez por análise.
    LaunchedEffect(flow.step, flow.applied, flow.error) {
        if (flow.step == MocapFlow.Step.Ready && flow.applied < 0 && flow.error == 0) flow.apply()
    }
    val notVideo = stringResource(R.string.pc_mocap_not_video)
    val take: (Uri) -> Unit = { uri -> if (PcMocap.isVideo(uri)) flow.analyze(uri) else store.showToast(notVideo) }
    val pick: () -> Unit = {
        PcFileDialogs.open(PcFileDialogs.filterFor(PickVisualMedia.VideoOnly), multiple = false) { uris -> uris.firstOrNull()?.let(take) }
    }
    when (flow.step) {
        // Esqueleto do arquivo que o Aurea não reconheceu sozinho: no PC a lista para associar
        // os ossos fica na aba Rig das Propriedades (não "acima", como no celular).
        MocapFlow.Step.NeedRig -> if (flow.status == 1) {
            KitHint(stringResource(R.string.pc_mocap_need_map))
            Spacer(Modifier.height(10.dp))
            ActionCard(stringResource(R.string.rig3d_fix), stringResource(R.string.rig3d_fix_desc)) {
                PcScene3D.propsTab = PcScene3D.PROP_RIG
            }
        } else {
            KitHint(stringResource(R.string.mocap_need_rig))
            Spacer(Modifier.height(10.dp))
            if (store.autoRigState == 1) {
                LinearProgressIndicator(progress = { store.autoRigProgress.coerceIn(0f, 1f) }, modifier = Modifier.fillMaxWidth(),
                    color = AureaColors.Accent, trackColor = AureaColors.Chip)
            } else {
                ActionCard(stringResource(R.string.mocap_autorig), null) { store.startAutoRig() }
            }
        }
        MocapFlow.Step.Choose -> {
            VideoDrop(tall = true, onPick = pick, onDrop = take)
            Spacer(Modifier.height(4.dp))
            // No PC o padrão é a qualidade máxima (BlazePose heavy, vídeo inteiro, 3D do
            // modelo); desligada = o modo reduzido do celular (mais rápido).
            ToggleLine(stringResource(R.string.pc_mocap_max_quality), !flow.reduced) { flow.reduced = !it }
            MocapCardError(flow.error)
        }
        MocapFlow.Step.Analyzing -> {
            val pct = (flow.progress * 100f).roundToInt().coerceIn(0, 100)
            val label = stringResource(R.string.mocap_analyzing, pct)
            Text(label, color = AureaColors.Text, fontSize = 13.sp, modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
            Spacer(Modifier.height(10.dp))
            LinearProgressIndicator(
                progress = { flow.progress.coerceIn(0f, 1f) },
                modifier = Modifier.fillMaxWidth().semantics {
                    contentDescription = label
                    progressBarRangeInfo = ProgressBarRangeInfo(flow.progress.coerceIn(0f, 1f), 0f..1f)
                },
                color = AureaColors.Accent, trackColor = AureaColors.Chip,
            )
            Spacer(Modifier.height(10.dp))
            ActionCard(stringResource(R.string.mocap_cancel), null) { flow.cancel() }
        }
        MocapFlow.Step.Ready -> {
            if (flow.applied >= 0) {
                Text(stringResource(R.string.mocap_applied, flow.applied), color = AureaColors.Accent, fontSize = 13.sp,
                    fontWeight = FontWeight.Medium, modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
                Spacer(Modifier.height(6.dp))
            } else if (flow.error == 0) {
                Text(stringResource(R.string.mocap_analyzing, 100), color = AureaColors.Text, fontSize = 13.sp)
                Spacer(Modifier.height(6.dp))
            }
            flow.report?.let { r -> KitHint(stringResource(R.string.mocap_summary, r[0].roundToInt(), r[1])) }
            val limbs = flow.uncertainLimbs()
            if (limbs.isNotEmpty()) {
                val names = limbs.map { stringResource(limbName(it)) }.joinToString(", ")
                Spacer(Modifier.height(6.dp))
                Text(stringResource(R.string.mocap_low_conf, names), color = AureaColors.Warning, fontSize = 12.sp, lineHeight = 16.sp,
                    modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
            }
            MocapCardError(flow.error)
            Spacer(Modifier.height(8.dp))
            // Ajustes (os mesmos do painel do celular), recolhidos por padrão.
            Row(Modifier.fillMaxWidth().height(36.dp).tocavel(shrink = 1f) { PcMocap.adjust = !PcMocap.adjust },
                verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.pc_mocap_adjust), color = AureaColors.Muted, fontSize = 12.5.sp, fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.weight(1f))
                CupertinoIcon(if (PcMocap.adjust) CupertinoGlyph.ChevronDown else CupertinoGlyph.ChevronRight, 10.dp, AureaColors.Muted)
            }
            if (PcMocap.adjust) {
                ScenePanelHost(store, {}) { env -> MocapAdjustments(env, flow) }
                // Mexeu nos ajustes: aplica de novo por cima dos keyframes do vídeo.
                Spacer(Modifier.height(6.dp))
                PrimaryButton(stringResource(R.string.pc_mocap_apply)) { flow.apply() }
            }
            Spacer(Modifier.height(8.dp))
            VideoDrop(tall = false, onPick = pick, onDrop = take)
        }
    }
    Spacer(Modifier.height(8.dp))
    KitHint(stringResource(R.string.mocap_limits))
}

@Composable
private fun MocapAdjustments(env: PanelEnv, flow: MocapFlow) {
    @Composable
    fun line(label: String, value: Float, min: Float, max: Float, step: Float, unit: String, decimals: Int, default: Float, set: (Float) -> Unit) =
        HumanRow(env, label, value, step, min, max, unit, decimals, default,
            onStart = {}, onValue = { set(it.coerceIn(min, max)) }, onEnd = { flow.retune() },
            onCommit = { set(it.coerceIn(min, max)); flow.retune() }, keyTag = "pcmocap.$label")
    Column {
        line(stringResource(R.string.mocap_intensity), flow.intensity * 100f, 0f, 150f, 1f, "%", 0, 100f) { flow.intensity = it / 100f }
        line(stringResource(R.string.mocap_smoothing), flow.smoothing * 100f, 0f, 100f, 1f, "%", 0, 50f) { flow.smoothing = it / 100f }
        line(stringResource(R.string.mocap_speed), flow.speed, 0.25f, 4f, 0.05f, "×", 2, 1f) { flow.speed = it }
        line(stringResource(R.string.mocap_yaw), flow.yaw, -180f, 180f, 1f, "°", 0, 0f) { flow.yaw = it }
        line(stringResource(R.string.mocap_floor), flow.floor * 100f, -50f, 50f, 1f, "%", 0, 0f) { flow.floor = it / 100f }
        ToggleLine(stringResource(R.string.mocap_foot_lock), flow.footLock) { flow.footLock = it; flow.retune() }
    }
}

/** A área de soltar o vídeo (clique abre o seletor de arquivos do Windows). */
@OptIn(ExperimentalFoundationApi::class, ExperimentalComposeUiApi::class)
@Composable
private fun VideoDrop(tall: Boolean, onPick: () -> Unit, onDrop: (Uri) -> Unit) {
    var hovering by remember { mutableStateOf(false) }
    val drop by androidx.compose.runtime.rememberUpdatedState(onDrop)
    val target = remember {
        object : DragAndDropTarget {
            override fun onEntered(event: DragAndDropEvent) { hovering = true }
            override fun onExited(event: DragAndDropEvent) { hovering = false }
            override fun onEnded(event: DragAndDropEvent) { hovering = false }
            override fun onDrop(event: DragAndDropEvent): Boolean {
                hovering = false
                val files = (event.dragData() as? DragData.FilesList)?.readFiles().orEmpty()
                val uri = files.map { Uri.parse(it) }.firstOrNull { it.scheme == "file" } ?: return false
                drop(uri)
                return true
            }
        }
    }
    val ink = if (hovering) AureaColors.Accent else AureaColors.Muted
    val title = stringResource(if (tall) R.string.pc_mocap_drop else R.string.mocap_new_video)
    Column(
        Modifier.fillMaxWidth().height(if (tall) 132.dp else 56.dp).clip(RoundedCornerShape(12.dp))
            .background(if (hovering) AureaColors.Accent.copy(alpha = 0.12f) else AureaColors.Chip)
            .drawBehind {
                drawRoundRect(ink.copy(alpha = 0.7f), cornerRadius = CornerRadius(12.dp.toPx()),
                    style = Stroke(1.5.dp.toPx(), pathEffect = PathEffect.dashPathEffect(floatArrayOf(6.dp.toPx(), 4.dp.toPx()))))
            }
            .dragAndDropTarget(shouldStartDragAndDrop = { it.dragData() is DragData.FilesList }, target = target)
            .semantics { contentDescription = title }
            .tocavel(shrink = 1f, onClick = onPick)
            .padding(horizontal = 14.dp, vertical = 10.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        if (tall) {
            CupertinoIcon(CupertinoGlyph.Film, 26.dp, ink)
            Spacer(Modifier.height(8.dp))
            Text(title, color = AureaColors.Text, fontSize = 14.sp, fontWeight = FontWeight.SemiBold, textAlign = TextAlign.Center)
            Spacer(Modifier.height(3.dp))
            Text(stringResource(R.string.pc_mocap_drop_hint), color = AureaColors.Muted, fontSize = 11.5.sp, lineHeight = 15.sp, textAlign = TextAlign.Center)
        } else {
            Row(verticalAlignment = Alignment.CenterVertically) {
                CupertinoIcon(CupertinoGlyph.Film, 16.dp, ink)
                Spacer(Modifier.width(8.dp))
                Text(title, color = AureaColors.Text, fontSize = 13.sp, fontWeight = FontWeight.Medium)
            }
        }
    }
}

/** O botão de ação do Aurea (preenchido na cor de marca). */
@Composable
private fun PrimaryButton(label: String, onClick: () -> Unit) {
    Box(
        Modifier.fillMaxWidth().height(44.dp).clip(RoundedCornerShape(12.dp)).background(AureaColors.Brand)
            .semantics { contentDescription = label }.tocavel(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, color = AureaColors.OnAction, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
    }
}

@Composable
private fun MocapCardError(code: Int) {
    val id = when (code) {
        1 -> R.string.mocap_err_no_person
        2 -> R.string.mocap_err_video
        3 -> R.string.mocap_err_apply
        else -> return
    }
    Spacer(Modifier.height(6.dp))
    Text(stringResource(id), color = AureaColors.Warning, fontSize = 12.sp, modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
}

private fun limbName(i: Int): Int = when (i) {
    0 -> R.string.mocap_limb_torso
    1 -> R.string.mocap_limb_head
    2 -> R.string.mocap_limb_left_arm
    3 -> R.string.mocap_limb_right_arm
    4 -> R.string.mocap_limb_left_leg
    else -> R.string.mocap_limb_right_leg
}
