package com.aurea.aurea.editor.panels

import androidx.annotation.StringRes
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.collapse
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.expand
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDirection
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.engine.ExpressionLook
import com.aurea.aurea.engine.LayerDetail
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.ds.numeroPtBr
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.roundToLong

/**
 * As faces de Transformar (Omega 0.3.2, área 05, P0-2). Na tela: **Mover ⇄ Pivô**
 * (a mesma aba; o 2º toque troca), **Girar**, **Escala** e **Inclinar** (não na
 * câmera). Desfoque de movimento e Animadores (extras do AUREA) abrem pelo ⋯.
 * [Opacidade] saiu (fica em Mesclagem e opacidade) e [Lente] saiu para Opções da
 * câmera; os dois continuam aqui por compatibilidade (testes, pedidos antigos) e
 * a tela devolve para Mover se alguém pedir.
 *
 * [title] vai para o cabeçalho ("Transformar · Mover"); [props] é o grupo que o
 * losango crava no 2D (vazio = face sem keyframe); [id] é o id da aba na moldura.
 */
enum class TransformTab(@StringRes val title: Int, val props: IntArray, val id: String) {
    Mover(R.string.om_ap_tf_move, intArrayOf(TrackProperty.POSITION_X, TrackProperty.POSITION_Y), "move"),
    Escalar(R.string.om_ap_tf_scale, intArrayOf(TrackProperty.SCALE_X, TrackProperty.SCALE_Y), "scale"),
    Girar(R.string.om_ap_tf_rotate, intArrayOf(TrackProperty.ROTATION_Z), "rotate"),
    /** Fora da tela desde a Omega (Mesclagem e opacidade). */
    Opacidade(R.string.panel_opacidade, intArrayOf(TrackProperty.OPACITY), "opacity"),
    Pivo(R.string.om_ap_tf_pivot, intArrayOf(TrackProperty.ANCHOR_X, TrackProperty.ANCHOR_Y), "pivot"),
    Desfoque(R.string.panel_desfoque_movimento, intArrayOf(), "motionblur"),
    /** Animadores da camada (entrada/saída/wiggle; ◇ por linha, dentro do cartão). */
    Animadores(R.string.la_animators, intArrayOf(), "animators"),
    /** Fora da tela desde a Omega (Opções da câmera, [CameraOptionsPanel]). */
    Lente(R.string.lens_title, intArrayOf(), "lens"),
    /** Inclinação X/Y em graus (trilhas SkewX 13 / SkewY 14; core/LayerSkew.hpp). */
    Inclinar(R.string.om_ap_tf_skew, intArrayOf(TrackProperty.SKEW_X, TrackProperty.SKEW_Y), "skew");

    companion object {
        fun byId(id: String): TransformTab? = entries.firstOrNull { it.id == id }
    }
}

private val RotationProps = intArrayOf(TrackProperty.ROTATION_X, TrackProperty.ROTATION_Y, TrackProperty.ROTATION_Z)

/** The rail diamond keys every axis exposed by the current dimension mode. */
internal fun transformKeyProperties(tab: TransformTab, threeD: Boolean, axis: Int = 2): IntArray = when {
    tab == TransformTab.Girar -> if (threeD) RotationProps.copyOf() else intArrayOf(RotationProps[2])
    threeD && tab == TransformTab.Mover -> intArrayOf(TrackProperty.POSITION_X, TrackProperty.POSITION_Y, TrackProperty.POSITION_Z)
    threeD && tab == TransformTab.Escalar -> intArrayOf(TrackProperty.SCALE_X, TrackProperty.SCALE_Y, TrackProperty.SCALE_Z)
    threeD && tab == TransformTab.Pivo -> intArrayOf(TrackProperty.ANCHOR_X, TrackProperty.ANCHOR_Y, TrackProperty.ANCHOR_Z)
    else -> tab.props
}

/**
 * X/Y/Z SÓ QUANDO SE APLICA: camada 3D de verdade (objeto 3D, câmera, luz, nulo
 * 3D) ou 2D que JÁ usa a terceira dimensão (inclinada em X/Y, com profundidade
 * ou com esses keyframes).
 */
internal fun uses3D(d: com.aurea.aurea.engine.LayerDetail?): Boolean {
    if (d == null) return false
    if (d.kind == LayerType.Model3D.kind || d.kind == LayerType.Camera.kind || d.kind == LayerType.Light.kind) return true
    if ((d.flags and com.aurea.aurea.engine.PodLayout.FLAG_THREE_D) != 0) return true
    if (abs(d.rotation[0]) > 0.01f || abs(d.rotation[1]) > 0.01f || abs(d.position[2]) > 0.01f) return true
    return d.isAnimated(TrackProperty.ROTATION_X) || d.isAnimated(TrackProperty.ROTATION_Y) || d.isAnimated(TrackProperty.POSITION_Z)
}

internal fun animatorRailTrack(count: Int, focused: TrackKey?, saved: TrackKey?): TrackKey? =
    listOfNotNull(focused, saved).firstOrNull {
        it.property == TrackProperty.LAYER_ANIM_PARAM && it.effectIndex in 0 until count && it.paramIndex in 0..17
    } ?: if (count > 0) TrackKey(TrackProperty.LAYER_ANIM_PARAM, 0, 0) else null

/** A face que a tela mostra para o pedido [tab] (as que saíram voltam para Mover). */
internal fun visibleTransformFace(tab: TransformTab, isCamera: Boolean): TransformTab = when (tab) {
    TransformTab.Opacidade, TransformTab.Lente -> TransformTab.Mover
    TransformTab.Inclinar -> if (isCamera) TransformTab.Mover else tab
    else -> tab
}

/**
 * MOVER E TRANSFORMAR na moldura comum ([PanelFrame], 200 dp): coluna esquerda
 * ‹ · ◆ · curva · = · ⋯, o conteúdo da face e as abas de ícone à direita.
 */
@Composable
internal fun TransformPanel(env: PanelEnv, tab: TransformTab, onTab: (TransformTab) -> Unit) {
    val store = env.store
    TransformPrefs.load(LocalContext.current)
    var wholeText by remember(store.primary) { mutableStateOf(false) }
    if (tab == TransformTab.Girar && store.text3d != null && !wholeText) {
        Column(Modifier.fillMaxSize()) {
            Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.t3d_letters), modifier = Modifier.weight(1f), color = AureaColors.Accent)
                androidx.compose.material3.TextButton(onClick = { wholeText = true }) { Text(stringResource(R.string.t3d_whole_object)) }
            }
            Box(Modifier.weight(1f)) { EffectsPanel(env, focusedType = effectTypeId("aurea.text3d.layout")) }
        }
        return
    }
    val isCamera = store.detail?.kind == LayerType.Camera.kind
    val face = visibleTransformFace(tab, isCamera)
    LaunchedEffect(tab, isCamera) {
        if (face != tab) {
            onTab(face)
            // Pedido antigo da Lente numa câmera: as opções moram no painel próprio.
            if (tab == TransformTab.Lente && isCamera) env.onOpenPanel(EditorPanel.Camera)
        }
    }
    val show3D by remember(store) { derivedStateOf { uses3D(store.detail) } }
    // Eixo da roda no 3D (padrão Z) e o modo do Mover (XY ou Z): por camada.
    var pickedAxis by rememberSaveable(store.primary) { mutableIntStateOf(2) }
    val axis = if (show3D) pickedAxis.coerceIn(0, 2) else 2
    var moveZ by rememberSaveable(store.primary) { mutableStateOf(false) }
    val cameraScale = isCamera && face == TransformTab.Escalar
    val keyProps = if (cameraScale) intArrayOf(TrackProperty.FOCAL_LENGTH) else transformKeyProperties(face, show3D, axis)
    val props = when {
        cameraScale -> intArrayOf()
        face == TransformTab.Girar -> intArrayOf(RotationProps[axis])
        face == TransformTab.Mover && moveZ -> intArrayOf(TrackProperty.POSITION_Z)
        else -> face.props
    }
    val animator = if (face == TransformTab.Animadores) animatorRailTrack(store.layerAnimators.size,
        store.timelineFocus?.singleOrNull(), store.transformAnimatorFocus?.takeIf { it.first == store.primary }?.second) else null
    DisposableEffect(store, store.projectGeneration, store.composition?.id, store.primary, face, show3D, axis, animator, cameraScale) {
        if (animator != null) store.focusLayerAnimator(animator)
        else store.timelineFocus = keyProps.map { TrackKey(it) }
        onDispose { store.timelineFocus = null }
    }
    val canKey = keyProps.isNotEmpty() || animator != null
    val look by remember(store, face, axis, show3D, animator, cameraScale) {
        derivedStateOf {
            when {
                animator != null -> {
                    val values = store.layerAnimators.getOrNull(animator.effectIndex)
                    val bit = 1 shl animator.paramIndex
                    when {
                        values != null && values[25].toInt() and bit != 0 -> KeyframeLook.KeyHere
                        values != null && values[24].toInt() and bit != 0 -> KeyframeLook.Animated
                        else -> KeyframeLook.None
                    }
                }
                cameraScale -> cameraTrackLook(store, TrackProperty.FOCAL_LENGTH)
                keyProps.isEmpty() -> KeyframeLook.None
                else -> transformLook(store.detail, keyProps)
            }
        }
    }
    val curveKeys by remember(store, face, axis, show3D, animator, moveZ, cameraScale) {
        derivedStateOf {
            if (animator != null) return@derivedStateOf store.primaryKeys().filter {
                it.property == animator.property && it.effectIndex == animator.effectIndex && it.paramIndex == animator.paramIndex
            }.sortedBy { it.time }
            val candidates = (props.toList() + keyProps.toList()).distinct()
            curveTrack(candidates.map { store.primaryKeys().transformTrack(it) })
        }
    }
    val exprKeys = props.map { TrackKey(it) }
    val exprLook by remember(store, face, axis, show3D, moveZ, cameraScale) { derivedStateOf { store.expressionLook(exprKeys) } }
    // Expressão na unidade da tela: escala em %, ângulos em °, o resto em px.
    val exprTitle = when (face) {
        TransformTab.Girar -> stringResource(R.string.edt_rotation_axis, "XYZ"[axis].toString())
        TransformTab.Mover -> if (moveZ) stringResource(R.string.panel_profundidade_z) else stringResource(face.title)
        else -> stringResource(face.title)
    }
    val exprScale = if (face == TransformTab.Escalar) 100f else 1f
    val exprUnit = when (face) {
        TransformTab.Escalar -> "%"
        TransformTab.Girar, TransformTab.Inclinar -> "°"
        else -> "px"
    }

    val onKeyframe: (() -> Unit)? = when {
        animator != null -> { { store.toggleLayerAnimKey(animator.effectIndex, animator.paramIndex) } }
        cameraScale -> { { store.toggleCameraLensKey(EditorStore.CAMERA_LENS_FOCAL) } }
        keyProps.isNotEmpty() -> { { store.toggleTransformKeyframe(keyProps) } }
        else -> null
    }
    // ◆ segurado: apaga as marcas da face num passo de desfazer (deleteAllKeyframes agrupa).
    val onDeleteAll: (() -> Unit)? = if (canKey) {
        {
            val tracks = if (animator != null) listOf(animator) else keyProps.map { TrackKey(it) }
            store.primary?.let { id -> store.deleteAllKeyframes(id, tracks) }
        }
    } else null
    val onCurve: (() -> Unit)? = if (canKey) {
        {
            val layer = store.primary
            val start = store.curveStart(curveKeys)
            if (layer != null && start != null) {
                store.selectKeyframe(layer, start)
                env.onOpenPanel(EditorPanel.Curve)
            }
        }
    } else null
    val onExpression: (() -> Unit)? = if (props.isNotEmpty()) ({ store.openExpression(exprTitle, exprKeys, exprScale, exprUnit) }) else null

    val more = transformMoreEntries(
        env = env, face = face, axis = axis, show3D = show3D, isCamera = isCamera,
        curveKeys = curveKeys, exprLook = exprLook, onExpression = onExpression, onTab = onTab,
    )

    val tabs = buildList {
        add(PanelTab(TransformTab.Mover.id, stringResource(R.string.om_ap_tf_move), glyph = CupertinoGlyph.Move,
            alt = PanelTab(TransformTab.Pivo.id, stringResource(R.string.om_ap_tf_pivot), glyph = CupertinoGlyph.SmallcircleCircle)))
        add(PanelTab(TransformTab.Girar.id, stringResource(R.string.om_ap_tf_rotate), glyph = CupertinoGlyph.ArrowCounterclockwise))
        add(PanelTab(TransformTab.Escalar.id, stringResource(R.string.om_ap_tf_scale), glyph = CupertinoGlyph.ArrowDownRightSquare))
        if (!isCamera) add(PanelTab(TransformTab.Inclinar.id, stringResource(R.string.om_ap_tf_skew), glyph = CupertinoGlyph.Rhombus))
    }
    // Desfoque de movimento e Animadores (⋯): nenhuma aba acesa.
    val selectedId = when (face) {
        TransformTab.Desfoque, TransformTab.Animadores -> null
        else -> face.id
    }
    Box(Modifier.fillMaxSize()) {
        PanelFrame(
            rail = PanelRail(
                onBack = env.onClose,
                keyframeLook = look,
                onKeyframe = onKeyframe,
                onDeleteAllKeyframes = onDeleteAll,
                onCurve = onCurve,
                curveReady = store.curveStart(curveKeys) != null,
                expression = exprLook,
                onExpression = onExpression,
                moreEntries = more,
                moreActive = face == TransformTab.Desfoque || face == TransformTab.Animadores,
            ),
            tabs = tabs,
            selectedTab = selectedId,
            onTab = { id -> TransformTab.byId(id)?.let(onTab) },
            scrollContent = false,
        ) {
            // O Mover não depende do 3D: arrastar o Z de uma camada 2D liga o "3D em uso"
            // no meio do gesto, e recriar a face cancelaria o arrasto.
            key(store.projectGeneration, store.composition?.id, store.primary, face, if (face == TransformTab.Mover) null else show3D) {
                when (face) {
                    TransformTab.Mover -> MoveFace(env, zMode = moveZ, onZMode = { moveZ = it })
                    TransformTab.Pivo -> PivotFace(env, depth = show3D)
                    TransformTab.Girar -> RotateFace(
                        env, depth = show3D, axis = axis, onAxis = { pickedAxis = it },
                        onLetters = if (store.text3d != null) ({ wholeText = false }) else null,
                    )
                    TransformTab.Escalar -> if (isCamera) CameraScaleFace(env) else ScaleFace(env, depth = show3D)
                    TransformTab.Inclinar -> SkewFace(env)
                    TransformTab.Desfoque -> MotionBlurFace(env)
                    TransformTab.Animadores -> AnimatorsFace(env)
                    TransformTab.Opacidade, TransformTab.Lente -> Unit
                }
            }
        }
        // Ids próprios das abas (além de `panel.tab.<i>` da moldura).
        PanelTabTags(buildList {
            add(if (face == TransformTab.Pivo) "transform.tab.pivot" else "transform.tab.move")
            add("transform.tab.rotate")
            add("transform.tab.scale")
            if (!isCamera) add("transform.tab.skew")
        })
    }
}

/** O ⋯ de Transformar: redefinir (da face e tudo), as ações de sempre e os extras do AUREA. */
@Composable
private fun transformMoreEntries(
    env: PanelEnv,
    face: TransformTab,
    axis: Int,
    show3D: Boolean,
    isCamera: Boolean,
    curveKeys: List<com.aurea.aurea.engine.KeyframeRow>,
    exprLook: ExpressionLook,
    onExpression: (() -> Unit)?,
    onTab: (TransformTab) -> Unit,
): List<MenuEntry> {
    val store = env.store
    val resetFaceLabel = when (face) {
        TransformTab.Mover -> stringResource(R.string.om_ap_tf_reset_position)
        TransformTab.Pivo -> stringResource(R.string.om_ap_tf_reset_pivot)
        TransformTab.Girar -> stringResource(R.string.om_ap_tf_reset_rotation)
        TransformTab.Escalar -> stringResource(R.string.om_ap_tf_reset_scale)
        TransformTab.Inclinar -> stringResource(R.string.om_ap_tf_reset_skew)
        else -> null
    }
    val resetAll = stringResource(R.string.om_ap_tf_reset_all)
    val copy = stringResource(R.string.transform_copy)
    val paste = stringResource(R.string.transform_paste)
    val turnsTitle = "${stringResource(R.string.edt_rotation_axis, "XYZ"[axis].toString())} · ${stringResource(R.string.edt_rotation_turns)}"
    val previous = stringResource(R.string.panel_keyframe_anterior)
    val next = stringResource(R.string.panel_proximo_keyframe)
    val expression = if (exprLook == ExpressionLook.None) stringResource(R.string.panel_adicionar_expressao) else stringResource(R.string.panel_editar_expressao)
    val rotate3D = stringResource(R.string.panel_girar_3d_x_y)
    val show3DLabel = stringResource(R.string.panel_mostrar_x_y_z_3d)
    val fill = stringResource(R.string.panel_preencher)
    val fit = stringResource(R.string.panel_ajustar)
    val compSize = stringResource(R.string.panel_tamanho_composicao)
    val snap = stringResource(R.string.om_ap_tf_snap)
    val accelerate = stringResource(R.string.om_ap_tf_accelerate)
    val motionBlur = stringResource(R.string.panel_desfoque_movimento)
    val animators = stringResource(R.string.la_animators)
    val cameraOptions = stringResource(R.string.om_ap_cam_title)
    val navigation = store.trackNavigation(curveKeys)
    val detail = store.detail
    val fitState = if (face == TransformTab.Escalar && !isCamera) mediaFitState(store) else null
    return buildList {
        if (resetFaceLabel != null) add(MenuEntry.Item(resetFaceLabel, tag = "transform.reset.${face.id}") {
            resetTransformFace(store, face, show3D, isCamera)
        })
        add(MenuEntry.Item(resetAll, tag = "transform.reset.all") { resetWholeTransform(store, show3D, isCamera) })
        add(MenuEntry.Divider)
        add(MenuEntry.Item(copy, glyph = CupertinoGlyph.DocOnDoc) { store.copyTransform() })
        if (store.clipboard and 16 != 0) add(MenuEntry.Item(paste, glyph = CupertinoGlyph.DocOnClipboard) { store.pasteTransform() })
        if (face == TransformTab.Girar) add(MenuEntry.Item(turnsTitle) {
            val angle = store.detail?.rotation?.get(axis) ?: 0f
            val (turns, rest) = rotationTurns(angle)
            val owner = store.primary
            val generation = store.projectGeneration
            val composition = store.composition?.id
            env.openKeypad(KeypadRequest(turnsTitle, turns.toFloat(), "x", Float.NEGATIVE_INFINITY, Float.POSITIVE_INFINITY, 0) {
                if (store.primary == owner && store.projectGeneration == generation && store.composition?.id == composition)
                    store.setTransform(RotationProps[axis], it.roundToInt() * 360f + rest, owner)
            })
        })
        add(MenuEntry.Item(previous, enabled = navigation?.hasPrevious == true) {
            navigation?.takeIf { it.hasPrevious }?.let { store.pause(); store.seek(it.previousFrame) }
        })
        add(MenuEntry.Item(next, enabled = navigation?.hasNext == true) {
            navigation?.takeIf { it.hasNext }?.let { store.pause(); store.seek(it.nextFrame) }
        })
        if (onExpression != null) add(MenuEntry.Item(expression) { onExpression() })
        if (face == TransformTab.Girar && !show3D) {
            // O antigo botão "Girar em 3D" da face (2D): liga X/Y/Z na camada.
            add(MenuEntry.Item(rotate3D, glyph = CupertinoGlyph.Cube, tag = "transform.enable3d") { store.enableLayer3D() })
        } else if (detail != null && detail.flags and com.aurea.aurea.engine.PodLayout.FLAG_THREE_D == 0 && detail.kind !in 8..10) {
            add(MenuEntry.Item(show3DLabel, glyph = CupertinoGlyph.Cube) { store.enableLayer3D() })
        }
        if (fitState != null) {
            // Preencher / Ajustar / Tamanho da composição (saíram da face Escala).
            add(MenuEntry.Divider)
            add(MenuEntry.Item(fill, checked = fitState == 0, tag = "transform.fit.fill") {
                store.primary?.let { com.aurea.aurea.editor.LayerOps.fitToCanvas(store, listOf(it), fill = true) }
            })
            add(MenuEntry.Item(fit, checked = fitState == 1, tag = "transform.fit.contain") {
                store.primary?.let { com.aurea.aurea.editor.LayerOps.fitToCanvas(store, listOf(it), fill = false) }
            })
            add(MenuEntry.Item(compSize, tag = "transform.fit.composition") {
                store.primary?.let { com.aurea.aurea.editor.LayerOps.makeCompositionSize(store, it) }
            })
        }
        if (face == TransformTab.Mover) {
            add(MenuEntry.Divider)
            add(MenuEntry.Item(snap, checked = TransformPrefs.snap, keepOpen = true, tag = "transform.more.snap") { TransformPrefs.toggleSnap() })
            add(MenuEntry.Item(accelerate, checked = TransformPrefs.accelerate, keepOpen = true, tag = "transform.more.accelerate") {
                TransformPrefs.toggleAccelerate()
            })
        }
        // Mais do AUREA: as faces extras e, na câmera, o painel das opções da lente.
        add(MenuEntry.Divider)
        add(MenuEntry.Item(motionBlur, glyph = CupertinoGlyph.MotionBlur, selected = face == TransformTab.Desfoque,
            tag = "transform.tab.motionblur") { onTab(TransformTab.Desfoque) })
        add(MenuEntry.Item(animators, glyph = CupertinoGlyph.Sparkles, selected = face == TransformTab.Animadores,
            tag = "transform.tab.animators") { onTab(TransformTab.Animadores) })
        if (isCamera) add(MenuEntry.Item(cameraOptions, glyph = CupertinoGlyph.CameraViewfinder, tag = "transform.camera") {
            env.onOpenPanel(EditorPanel.Camera)
        })
    }
}

/**
 * Preencher (0) / Ajustar (1) / nenhum (−1) para a escala atual; nulo quando a
 * camada não tem tamanho de mídia (os itens do ⋯ não aparecem).
 */
private fun mediaFitState(store: EditorStore): Int? {
    val d = store.detail ?: return null
    if (!com.aurea.aurea.editor.LayerGeometry.hasSize(d) || d.kind == LayerType.Audio.kind) return null
    val cw = store.project.width.toFloat()
    val ch = store.project.height.toFloat()
    val w = com.aurea.aurea.editor.LayerGeometry.width(d)
    val h = com.aurea.aurea.editor.LayerGeometry.height(d)
    if (w <= 0f || h <= 0f || cw <= 0f || ch <= 0f) return null
    val cover = max(cw / w, ch / h)
    val contain = min(cw / w, ch / h)
    val s = d.scale
    return when {
        abs(abs(s[0]) - cover) < 1e-3f && abs(abs(s[1]) - cover) < 1e-3f -> 0
        abs(abs(s[0]) - contain) < 1e-3f && abs(abs(s[1]) - contain) < 1e-3f -> 1
        else -> -1
    }
}

// =============================================================================
// Redefinir (⋯): cada um num passo de desfazer
// =============================================================================

private fun isCameraOrLight(d: LayerDetail) = d.kind == LayerType.Camera.kind || d.kind == LayerType.Light.kind

/** Posição no centro da composição (no espaço do pai); Z volta a 0 (câmera e luz mantêm o Z). */
private fun resetPosition(store: EditorStore, d: LayerDetail, show3D: Boolean) {
    val c = FloatArray(2)
    d.compToParent(store.project.width / 2f, store.project.height / 2f, c)
    store.setTransform2(TrackProperty.POSITION_X, c[0], TrackProperty.POSITION_Y, c[1])
    if (show3D && !isCameraOrLight(d) && d.position[2] != 0f) store.setTransform(TrackProperty.POSITION_Z, 0f)
}

/** Pivô no centro da mídia (o objeto 3D centrado tem o centro no zero). */
private fun resetPivot(store: EditorStore, d: LayerDetail, show3D: Boolean) {
    val centered = d.kind == LayerType.Model3D.kind
    val ax = if (centered) 0f else com.aurea.aurea.editor.LayerGeometry.width(d) / 2f
    val ay = if (centered) 0f else com.aurea.aurea.editor.LayerGeometry.height(d) / 2f
    store.setTransform2(TrackProperty.ANCHOR_X, ax, TrackProperty.ANCHOR_Y, ay)
    if (show3D && d.anchor[2] != 0f) store.setTransform(TrackProperty.ANCHOR_Z, 0f)
}

private fun resetRotation(store: EditorStore, d: LayerDetail, show3D: Boolean) {
    if (show3D) {
        if (d.rotation[0] != 0f) store.setTransform(TrackProperty.ROTATION_X, 0f)
        if (d.rotation[1] != 0f) store.setTransform(TrackProperty.ROTATION_Y, 0f)
    }
    store.setTransform(TrackProperty.ROTATION_Z, 0f)
}

private fun resetScale(store: EditorStore) {
    if (store.layers.firstOrNull { it.id == store.primary }?.isThreeD == true) store.setScale3(floatArrayOf(1f, 1f, 1f))
    else store.setTransform2(TrackProperty.SCALE_X, 1f, TrackProperty.SCALE_Y, 1f)
}

private fun resetSkew(store: EditorStore) {
    store.setTransform2(TrackProperty.SKEW_X, 0f, TrackProperty.SKEW_Y, 0f)
}

/** Câmera: a "escala" é o ângulo de visão; volta ao padrão do motor (50°). */
private fun resetCameraView(store: EditorStore) {
    store.setCameraLens(EditorStore.CAMERA_LENS_FOCAL, CameraLens.focalFromFov(CameraLens.DEFAULT_FOV))
}

internal fun resetTransformFace(store: EditorStore, face: TransformTab, show3D: Boolean, isCamera: Boolean) {
    val d = store.detail ?: return
    store.beginGesture("redefinir")
    when (face) {
        TransformTab.Mover -> resetPosition(store, d, show3D)
        TransformTab.Pivo -> resetPivot(store, d, show3D)
        TransformTab.Girar -> resetRotation(store, d, show3D)
        TransformTab.Escalar -> if (isCamera) resetCameraView(store) else resetScale(store)
        TransformTab.Inclinar -> resetSkew(store)
        else -> Unit
    }
    store.endGesture()
}

/** "Redefinir transformação": posição, pivô, rotação, escala e inclinação num passo só. */
internal fun resetWholeTransform(store: EditorStore, show3D: Boolean, isCamera: Boolean) {
    val d = store.detail ?: return
    store.beginGesture("redefinir transformação")
    resetPosition(store, d, show3D)
    store.detail?.let { resetPivot(store, it, show3D) }
    store.detail?.let { resetRotation(store, it, show3D) }
    if (!isCamera) {
        resetScale(store)
        resetSkew(store)
    }
    store.endGesture()
}

// =============================================================================
// Peças comuns das faces: campo 63 × 25 e ids das abas
// =============================================================================

/** Medidas do campo da referência (pílula X | Y, Z, Largura/Altura, Inclinação). */
internal object TfFieldDims {
    val Width = 63.dp
    val Height = 25.dp
    val Radius = 5.dp
    val Gap = 7.dp
}

internal val TfPillStart: Shape = RoundedCornerShape(topStart = TfFieldDims.Radius, bottomStart = TfFieldDims.Radius)
internal val TfPillEnd: Shape = RoundedCornerShape(topEnd = TfFieldDims.Radius, bottomEnd = TfFieldDims.Radius)
internal val TfPillAlone: Shape = RoundedCornerShape(TfFieldDims.Radius)

/**
 * Um campo 63 × 25 (fundo `campo`) com o valor em 10 sp (ativo no destaque) e
 * o rótulo de 9 sp embaixo. A área de toque tem 44 dp de altura.
 */
@Composable
internal fun TfField(
    text: String,
    label: String,
    description: String,
    active: Boolean,
    tag: String,
    modifier: Modifier = Modifier,
    shape: Shape = TfPillAlone,
    enabled: Boolean = true,
    activeColor: Color = AureaColors.Accent,
    idleColor: Color = AureaColors.Text,
    onLongClick: (() -> Unit)? = null,
    onClick: () -> Unit,
) {
    Column(
        modifier
            .width(TfFieldDims.Width)
            .heightIn(min = 44.dp)
            .testTag(tag)
            .semantics(mergeDescendants = true) {
                contentDescription = description
                selected = active
            }
            .tocavel(enabled = enabled, shrink = 1f, onLongClick = onLongClick, onClick = onClick),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(
            Modifier.fillMaxWidth().height(TfFieldDims.Height).clip(shape).background(AureaColors.Chip).padding(horizontal = 3.dp),
            contentAlignment = Alignment.Center,
        ) {
            Text(
                text,
                maxLines = 1,
                overflow = TextOverflow.Clip,
                style = AureaType.Base.merge(TextStyle(
                    fontSize = 10.sp,
                    fontWeight = FontWeight.W600,
                    color = if (!enabled) AureaColors.Muted else if (active) activeColor else idleColor,
                    textAlign = TextAlign.Center,
                    fontFeatureSettings = "tnum",
                    textDirection = TextDirection.Ltr,
                )),
            )
        }
        Text(
            label,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.padding(top = 2.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 9.sp, color = AureaColors.Muted, textAlign = TextAlign.Center)),
        )
    }
}

/** O fio de 1 dp entre X e Y da pílula. */
@Composable
internal fun TfPillGap() {
    Spacer(Modifier.width(1.dp))
}

/**
 * Ids próprios das abas por cima da coluna direita do [PanelFrame] (que só
 * marca `panel.tab.<i>`). Caixas sem toque: o toque e o leitor de tela continuam
 * na aba de baixo; o id serve aos testes de UI, iguais ao iOS.
 */
@Composable
internal fun BoxScope.PanelTabTags(tags: List<String>) {
    if (tags.size < 2) return
    Row(Modifier.matchParentSize()) {
        Spacer(Modifier.weight(1f))
        Column(Modifier.width(PanelFrameDefaults.RailWidth).fillMaxHeight()) {
            tags.take(PanelFrameDefaults.MAX_TABS).forEach { Box(Modifier.weight(1f).fillMaxWidth().testTag(it)) }
        }
    }
}

/**
 * Rotação em voltas + resto, como o AE mostra ("2x +30°"): as voltas truncam
 * para o zero e o resto tem o MESMO sinal (−405° = "-1x -45°"). Conta em
 * décimos de grau para 719,99° não virar "1x +360°".
 */
internal fun rotationTurns(angle: Float): Pair<Int, Float> {
    val tenths = (angle.toDouble() * 10.0).roundToLong()
    val turns = tenths / 3600L
    return turns.toInt() to ((tenths - turns * 3600L) / 10.0).toFloat()
}

// =============================================================================
// Extras do AUREA (pelo ⋯): desfoque de movimento e animadores
// =============================================================================

@Composable
private fun ColumnScope.AnimatorsFace(env: PanelEnv) {
    val store = env.store
    Column(
        Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()).padding(start = 8.dp, top = 6.dp, end = 12.dp, bottom = 10.dp),
    ) {
        LayerAnimSection(env)
        if (store.detail?.kind == LayerType.Text.kind || store.text3d != null) {
            TextAnimSection(env, showAnimatorEffect = false)
        }
    }
}

/**
 * DESFOQUE DE MOVIMENTO: liga na camada (e no projeto, se estava desligado lá —
 * o motor só borra com os dois ligados). A composição controla ângulo e fase
 * em graus, além das amostras. O rastro local e o fluxo óptico do vídeo são
 * controles separados.
 */
@Composable
private fun ColumnScope.MotionBlurFace(env: PanelEnv) {
    val store = env.store
    val d = store.detail ?: return
    val on = d.motionBlur && store.compMotionBlur
    var advanced by rememberSaveable { mutableStateOf(false) }
    Column(
        Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()).padding(start = 8.dp, top = 6.dp, end = 12.dp, bottom = 10.dp),
    ) {
        BlurToggleRow(stringResource(R.string.panel_desfoque_movimento), stringResource(R.string.panel_borra_camada_direcao_ela_move), on) { v ->
            if (v) {
                if (!store.compMotionBlur) store.setCompositionMotionBlur(true)
                if (!d.motionBlur) store.setLayerMotionBlur(d.id, true)
            } else {
                store.setLayerMotionBlur(d.id, false)
            }
        }
        if (on) {
            MotionBlurSetting(env, stringResource(R.string.mb_shutter_angle), store.shutterAngle,
                "°", 0f, 720f, 1, "motionblur.shutter", store::changeShutterAngle)
            Text(
                stringResource(R.string.mb_comp_settings_hint),
                modifier = Modifier.padding(top = 4.dp, start = 4.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, lineHeight = 15.sp, color = AureaColors.Muted)),
            )
            // Comprimento do rastro SÓ desta camada (× o obturador do projeto).
            val lengthLabel = stringResource(R.string.la_motion_blur_length)
            val length = store.layerMotionBlurLength * 100f
            com.aurea.aurea.ui.ds.PropertyRow(
                label = lengthLabel,
                value = length,
                unitsPerDp = 0.5f,
                min = 0f,
                max = 400f,
                format = { "${numeroPtBr(it, 0)}%" },
                selected = false,
                onSelect = {},
                onGestureStart = { store.beginGesture("comprimento do desfoque") },
                onValue = { store.changeLayerMotionBlurLength(it.coerceIn(0f, 400f) / 100f) },
                onGestureEnd = { store.endGesture() },
                onTapValue = {
                    env.openKeypad(KeypadRequest(lengthLabel, length, "%", 0f, 400f, 0) { store.changeLayerMotionBlurLength(it / 100f) })
                },
            )
            Text(
                stringResource(R.string.la_motion_blur_length_hint),
                modifier = Modifier.padding(top = 4.dp, start = 4.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, lineHeight = 15.sp, color = AureaColors.Muted)),
            )
            val advancedLabel = stringResource(R.string.mb_advanced)
            com.aurea.aurea.ui.ds.AdvancedToggle(advanced, 3, { advanced = !advanced },
                Modifier.heightIn(min = 48.dp).testTag("motionblur.advanced").semantics {
                    contentDescription = advancedLabel
                    if (advanced) collapse { advanced = false; true }
                    else expand { advanced = true; true }
                })
            if (advanced) {
                MotionBlurSetting(env, stringResource(R.string.mb_shutter_phase), store.shutterPhase,
                    "°", -360f, 360f, 1, "motionblur.phase", store::changeShutterPhase)
                androidx.compose.material3.TextButton(onClick = store::centerMotionBlurExposure,
                    modifier = Modifier.heightIn(min = 48.dp).testTag("motionblur.center")) {
                    Text(stringResource(R.string.mb_center_exposure))
                }
                Text(stringResource(R.string.mb_phase_hint), style = AureaType.BodySmall, color = AureaColors.Muted)
                MotionBlurSetting(env, stringResource(R.string.mb_samples_per_frame), store.motionBlurSamples.toFloat(),
                    "", 2f, minOf(64, store.motionBlurAdaptiveLimit).toFloat(), 0, "motionblur.samples") {
                    store.changeMotionBlurSamples(it.roundToInt())
                }
                MotionBlurSetting(env, stringResource(R.string.mb_adaptive_limit), store.motionBlurAdaptiveLimit.toFloat(),
                    "", maxOf(2, store.motionBlurSamples).toFloat(), 256f, 0, "motionblur.adaptive") {
                    store.changeMotionBlurAdaptiveLimit(it.roundToInt())
                }
                Text(stringResource(R.string.mb_quality_hint), style = AureaType.BodySmall, color = AureaColors.Muted)
                // 0.3.2: motion blur 3D rápido (vetores) × alta qualidade (amostras).
                val vectorLabel = stringResource(R.string.mb_vector_3d)
                val vectorHint = stringResource(R.string.mb_vector_3d_hint)
                Row(
                    Modifier.fillMaxWidth().heightIn(min = 48.dp).padding(vertical = 6.dp).padding(start = 4.dp)
                        .testTag("motionblur.vector3d")
                        .toggleable(value = store.motionBlurVector, role = androidx.compose.ui.semantics.Role.Switch,
                            onValueChange = store::changeMotionBlurVector)
                        .semantics(mergeDescendants = true) { contentDescription = "$vectorLabel. $vectorHint" },
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Column(Modifier.weight(1f)) {
                        Text(vectorLabel, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)))
                        Text(vectorHint, style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, lineHeight = 15.sp, color = AureaColors.Muted)))
                    }
                    Spacer(Modifier.width(10.dp))
                    com.aurea.aurea.ui.ds.AureaToggle(checked = store.motionBlurVector, onCheckedChange = store::changeMotionBlurVector)
                }
            }
        }
        if (d.kind == LayerType.Video.kind) {
            Spacer(Modifier.height(8.dp))
            BlurToggleRow(stringResource(R.string.panel_desfoque_movimento_video), stringResource(R.string.panel_borra_mexe_dentro_video), d.vectorBlur) { store.setVectorBlur(d.id, it) }
        }
    }
}

@Composable
private fun MotionBlurSetting(env: PanelEnv, label: String, value: Float, unit: String,
                              min: Float, max: Float, decimals: Int, tag: String, onValue: (Float) -> Unit) {
    val edit = { env.openKeypad(KeypadRequest(label, value, unit, min, max, decimals, onConfirm = onValue)) }
    com.aurea.aurea.ui.ds.PropertyRow(
        label = label, value = value, unitsPerDp = if (decimals == 0) .1f else .5f,
        min = min, max = max, format = { "${numeroPtBr(it, decimals)}$unit" },
        selected = false, onSelect = {}, modifier = Modifier.testTag(tag).semantics(mergeDescendants = true) {
            contentDescription = label
            stateDescription = "${numeroPtBr(value, decimals)}$unit"
            onClick { edit(); true }
        },
        onGestureStart = { env.store.beginGesture(label) }, onValue = onValue,
        onGestureEnd = { env.store.endGesture() },
        onTapValue = edit,
    )
}

/** Linha com título, dica e interruptor (desfoque de movimento e opções da câmera). */
@Composable
internal fun BlurToggleRow(label: String, hint: String, checked: Boolean, modifier: Modifier = Modifier, onChange: (Boolean) -> Unit) {
    Row(modifier.fillMaxWidth().padding(vertical = 6.dp).padding(start = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)))
            if (hint.isNotEmpty()) Text(hint, style = AureaType.Base.merge(TextStyle(fontSize = 11.5.sp, lineHeight = 15.sp, color = AureaColors.Muted)))
        }
        Spacer(Modifier.width(10.dp))
        com.aurea.aurea.ui.ds.AureaToggle(checked = checked, onCheckedChange = onChange)
    }
}

/**
 * Centraliza uma fileira de campos no topo da face. [scroll]: rola de lado se não
 * couber (só fora do trackpad — dentro dele o arrasto é do trackpad).
 */
@Composable
internal fun TfFieldRow(
    top: androidx.compose.ui.unit.Dp,
    modifier: Modifier = Modifier,
    scroll: Boolean = false,
    content: @Composable () -> Unit,
) {
    Box(modifier.fillMaxWidth().padding(top = top), contentAlignment = Alignment.TopCenter) {
        Row(
            (if (scroll) Modifier.horizontalScroll(rememberScrollState()) else Modifier).padding(horizontal = 6.dp),
            verticalAlignment = Alignment.Top,
            horizontalArrangement = Arrangement.Center,
        ) { content() }
    }
}
