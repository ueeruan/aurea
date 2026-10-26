package com.aurea.aurea.editor

import android.content.Context
import android.content.SharedPreferences
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.FastOutLinearInEasing
import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectVerticalDragGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.setProgress
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntRect
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Popup
import androidx.compose.ui.window.PopupPositionProvider
import androidx.compose.ui.window.PopupProperties
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.delay
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * Zoom da VISTA do palco (lupa da prévia): só muda como a composição aparece
 * na tela, nunca o projeto, o render nem o export. O motor aplica o mesmo
 * zoom/pan no passe de saída da prévia (`viewportZoom`/`viewportPan`, px da
 * superfície) e o [StageMapper] usa os mesmos números para desenhar alças e
 * converter o toque — mover um objeto com zoom continua exato.
 *
 * Contas puras (testadas na JVM): o encaixe base (zoom 1) é o da prévia; o
 * zoom multiplica esse encaixe em torno do centro do palco e o pan desloca em
 * px de tela.
 */
internal object StageZoomMath {
    // Omega chrome (área 02, P1-2): de 1 % a 6400 %, como a referência.
    const val MIN = 0.01f
    const val MAX = 64f

    /** Degraus do + e do − da coluna de modos (1 · 5 · 10 · 25 · 50 · 100 · 150 · 200 · 300 · 400 · 800 · 1600 · 3200 · 6400 %). */
    val STEPS = floatArrayOf(0.01f, 0.05f, 0.1f, 0.25f, 0.5f, 1f, 1.5f, 2f, 3f, 4f, 8f, 16f, 32f, 64f)

    /** O primeiro degrau ACIMA do zoom atual (no teto, fica). */
    fun stepUp(z: Float): Float = STEPS.firstOrNull { it > z * 1.0001f } ?: MAX

    /** O último degrau ABAIXO do zoom atual (no piso, fica). */
    fun stepDown(z: Float): Float = STEPS.lastOrNull { it < z * 0.9999f } ?: MIN

    /** Zoom preso em [1 %, 6400 %]; lixo (NaN/∞) volta ao encaixe (100 %). */
    fun clampZoom(z: Float): Float = if (z.isFinite()) z.coerceIn(MIN, MAX) else 1f

    /**
     * Pan máximo num eixo: a borda da composição pode chegar até onde ela fica
     * no encaixe (100 %). Em 1× não há pan. [baseExtent] = tamanho da
     * composição na tela em 1× (px).
     */
    fun maxPan(zoom: Float, baseExtent: Float): Float =
        if (baseExtent.isFinite() && baseExtent > 0f) ((clampZoom(zoom) - 1f) * baseExtent / 2f).coerceAtLeast(0f) else 0f

    fun clampPan(pan: Float, zoom: Float, baseExtent: Float): Float {
        if (!pan.isFinite()) return 0f
        val lim = maxPan(zoom, baseExtent)
        return pan.coerceIn(-lim, lim)
    }

    /**
     * Pinça da vista: o ponto da composição que estava sob [mid0] (meio dos
     * dedos no começo, com [zoom0]/[pan0]) fica sob [mid] com [zoom1]. Com
     * `mid == mid0` é o zoom em torno do ponto focal; mudando só o meio, é o
     * pan de dois dedos. [centre] = centro do palco no eixo.
     */
    fun pinchPan(pan0: Float, zoom0: Float, zoom1: Float, mid0: Float, mid: Float, centre: Float): Float {
        val z0 = if (zoom0 > 0f) zoom0 else 1f
        return (mid - centre) - (mid0 - centre - pan0) * (zoom1 / z0)
    }

    /** Zoom em torno de um ponto fixo da tela ([focus]). */
    fun zoomAround(pan0: Float, zoom0: Float, zoom1: Float, focus: Float, centre: Float): Float =
        pinchPan(pan0, zoom0, zoom1, focus, focus, centre)

    /**
     * Origem da composição na tela num eixo: encaixe centrado em [box]
     * (menos [inset] de cada lado) com a escala `baseFit·zoom`, mais o pan. A
     * mesma conta do motor (Renderer.cpp, "saida").
     */
    fun origin(box: Float, inset: Float, comp: Float, baseFit: Float, zoom: Float, pan: Float): Float =
        inset + (box - 2f * inset - comp * baseFit * zoom) / 2f + pan

    /** px da composição → px de tela. */
    fun toScreen(c: Float, box: Float, inset: Float, comp: Float, baseFit: Float, zoom: Float, pan: Float): Float =
        origin(box, inset, comp, baseFit, zoom, pan) + c * baseFit * zoom

    /** px de tela → px da composição (inverso exato de [toScreen]). */
    fun toComp(s: Float, box: Float, inset: Float, comp: Float, baseFit: Float, zoom: Float, pan: Float): Float {
        val f = baseFit * zoom
        return if (f > 0f) (s - origin(box, inset, comp, baseFit, zoom, pan)) / f else 0f
    }

    /** Fora do encaixe (ampliado ou reduzido)? */
    fun isZoomed(zoom: Float): Boolean = abs(zoom - 1f) > 0.005f

    /** "250" para 2,5×. */
    fun percent(zoom: Float): Int = (zoom * 100f).roundToInt()
}

/** Tracks a stable pair of pointers. Crossing fingers must not flip the layer
 * by 180 degrees; reversing at a limit must react on the very next event. */
internal class StagePinchTracker {
    var factor = 1f
        private set
    var degrees = 0f
        private set
    private var span = 0f
    private var angle = 0f
    private var minimumSpan = 16f
    private var ready = false

    fun start(ax: Float, ay: Float, bx: Float, by: Float, minimum: Float) {
        factor = 1f; degrees = 0f; minimumSpan = maxOf(1f, minimum)
        span = kotlin.math.hypot(ax - bx, ay - by)
        angle = kotlin.math.atan2(by - ay, bx - ax)
        ready = span.isFinite() && span >= minimumSpan
    }

    fun update(ax: Float, ay: Float, bx: Float, by: Float, clamp: (Float) -> Float): Boolean {
        if (!ax.isFinite() || !ay.isFinite() || !bx.isFinite() || !by.isFinite()) return false
        val nextSpan = kotlin.math.hypot(ax - bx, ay - by)
        val nextAngle = kotlin.math.atan2(by - ay, bx - ax)
        if (!nextSpan.isFinite() || !nextAngle.isFinite()) return false
        if (!ready || nextSpan < minimumSpan) {
            span = nextSpan; angle = nextAngle; ready = nextSpan >= minimumSpan
            return false
        }
        var delta = nextAngle - angle
        val pi = Math.PI.toFloat()
        if (delta > pi) delta -= 2 * pi
        if (delta < -pi) delta += 2 * pi
        val next = clamp(factor * (nextSpan / span))
        if (!next.isFinite() || next <= 0f) return false
        val changed = abs(next - factor) > 1e-6f || abs(delta) > 1e-6f
        factor = next; degrees += Math.toDegrees(delta.toDouble()).toFloat()
        span = nextSpan; angle = nextAngle
        return changed
    }
}


/**
 * Estado da vista do palco (sessão do editor, nunca vai para o projeto). O
 * palco lê no desenho; o botão de modo zoom no canto da prévia liga
 * [zoomLock] (a pinça amplia a vista, mesmo sobre a camada escolhida).
 */
internal object StageView {
    var zoom by mutableFloatStateOf(1f)
        private set
    var panX by mutableFloatStateOf(0f)
        private set
    var panY by mutableFloatStateOf(0f)
        private set
    var zoomLock by mutableStateOf(false)

    /** Omega chrome (área 02): muda a cada zoom/pan da vista — o rótulo "NN%" aparece e some em 2 s. */
    var viewTouch by mutableIntStateOf(0)
        private set

    /** Tamanho da composição na tela em 1× (px), anotado pelo palco: prende o pan dos degraus. */
    private var extentX = 0f
    private var extentY = 0f

    val zoomed: Boolean get() = StageZoomMath.isZoomed(zoom)

    fun noteExtent(x: Float, y: Float) {
        extentX = x
        extentY = y
    }

    /** Aplica (já preso pelo chamador) e manda ao motor só se mudou. */
    fun set(store: EditorStore, z: Float, px: Float, py: Float) {
        val nz = StageZoomMath.clampZoom(z)
        val nx = if (px.isFinite()) px else 0f
        val ny = if (py.isFinite()) py else 0f
        if (abs(nz - zoom) < 1e-5f && abs(nx - panX) < 0.01f && abs(ny - panY) < 0.01f) return
        zoom = nz
        panX = nx
        panY = ny
        viewTouch++
        store.setViewport(nz, nx, ny)
    }

    /** De volta ao encaixe (100 %), sem mostrar o rótulo (troca de projeto). */
    fun reset(store: EditorStore) {
        zoom = 1f
        panX = 0f
        panY = 0f
        store.setViewport(1f, 0f, 0f)
    }

    /** Os degraus da coluna (+ / − / 100 %): zoom em torno do centro, pan preso. */
    fun zoomTo(store: EditorStore, z: Float) {
        val nz = StageZoomMath.clampZoom(z)
        val k = if (zoom > 0f) nz / zoom else 1f
        set(
            store, nz,
            StageZoomMath.clampPan(panX * k, nz, extentX),
            StageZoomMath.clampPan(panY * k, nz, extentY),
        )
        viewTouch++
    }
}

// =============================================================================
// Omega chrome (área 02, P0-5/P1-2): modos de visão da prévia
// =============================================================================

/**
 * A coluna de modos à direita da prévia (referência: pixels reais · grade com
 * espaçamento · destaque da camada escolhida · visão pela câmera · zoom em
 * degraus). Tudo é da SESSÃO e só da prévia: nada vai para o projeto nem para
 * o export. O espaçamento da grade fica guardado no aparelho.
 */
internal object ViewModes {
    const val GRID_MIN = 5
    const val GRID_MAX = 500
    /** A marca brilhante do girador (a referência começa nela). */
    const val GRID_MARK = 40

    var columnOpen by mutableStateOf(false)
    var pixel by mutableStateOf(false)
    var grid by mutableStateOf(false)
    var gridSpacing by mutableIntStateOf(GRID_MARK)
        private set
    /** 0 normal, 1 só a(s) escolhida(s), 2 a(s) escolhida(s) a 50 % (só na prévia). */
    var solo by mutableIntStateOf(0)
    var camera by mutableStateOf(false)
    var spacingOpen by mutableStateOf(false)

    private var prefs: SharedPreferences? = null

    fun load(context: Context) {
        if (prefs != null) return
        val p = context.applicationContext.getSharedPreferences("aurea.omega.chrome", Context.MODE_PRIVATE)
        prefs = p
        gridSpacing = p.getInt("gridSpacing", GRID_MARK).coerceIn(GRID_MIN, GRID_MAX)
    }

    fun changeGridSpacing(value: Int) {
        val v = value.coerceIn(GRID_MIN, GRID_MAX)
        if (v == gridSpacing) return
        gridSpacing = v
        prefs?.edit()?.putInt("gridSpacing", v)?.apply()
    }

    /** O toque no destaque cicla NORMAL → SÓ A ESCOLHIDA → ESCOLHIDA A 50 % → NORMAL. */
    fun cycleSolo() {
        solo = (solo + 1) % 3
    }

    /** Cada projeto abre com a prévia normal (a grade e o espaçamento ficam). */
    fun resetForProject() {
        columnOpen = false
        pixel = false
        solo = 0
        camera = false
        spacingOpen = false
    }
}

/** Largura da coluna = a de uma célula do transporte de 7 (tela ÷ 7). */
internal fun viewModeColumnWidth(screenWidthDp: Int): Dp = (screenWidthDp / 7f).dp.coerceIn(44.dp, 72.dp)

/**
 * A coluna de modos (`viewmode.column`). Abre pelo último botão do transporte:
 * o recorte vai da metade de baixo à coluna inteira em 100 ms (acelerando) e
 * volta desacelerando. Consome todo toque (nada passa para o palco embaixo).
 */
@Composable
internal fun ViewModeColumn(store: EditorStore, width: Dp, modifier: Modifier) {
    val context = LocalContext.current
    remember(context) {
        ViewModes.load(context)
        true
    }
    // Cada projeto abre com a prévia normal.
    val path = store.project.path
    LaunchedEffect(path) { ViewModes.resetForProject() }
    // Os modos vão ao motor sempre que mudam ou que a seleção muda.
    val selection = store.selection
    LaunchedEffect(ViewModes.solo, ViewModes.pixel, selection) {
        store.setPreviewView(if (selection.isEmpty()) 0 else ViewModes.solo, ViewModes.pixel)
    }
    val open = ViewModes.columnOpen
    val progress by animateFloatAsState(
        targetValue = if (open) 1f else 0f,
        animationSpec = tween(100, easing = if (open) FastOutLinearInEasing else LinearOutSlowInEasing),
        label = "viewmode-column",
    )
    if (!open && progress <= 0.001f) return
    val hasSelection = selection.isNotEmpty()
    val hasCamera = store.layers.any { it.kind == LayerType.Camera.kind }
    val onLabel = stringResource(R.string.common_on)
    val offLabel = stringResource(R.string.common_off)
    Column(
        modifier
            .width(width)
            .fillMaxHeight()
            .drawWithContent {
                // Da metade de baixo até a coluna inteira.
                clipRect(top = size.height * 0.5f * (1f - progress)) { this@drawWithContent.drawContent() }
            }
            .background(OmegaChrome.Column)
            .testTag("viewmode.column")
            .pointerInput(Unit) {
                awaitEachGesture {
                    val down = awaitFirstDown(requireUnconsumed = false)
                    down.consume()
                    while (true) {
                        val event = awaitPointerEvent()
                        event.changes.forEach { it.consume() }
                        if (event.changes.none { it.pressed }) break
                    }
                }
            },
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        ViewModeCell(
            glyph = ShellGlyph.PixelGrid,
            description = stringResource(R.string.om_vm_pixel),
            state = if (ViewModes.pixel) onLabel else offLabel,
            on = ViewModes.pixel,
            tag = "viewmode.pixel",
            modifier = Modifier.weight(1f),
        ) { ViewModes.pixel = !ViewModes.pixel }
        Box(Modifier.weight(1f).fillMaxWidth()) {
            ViewModeCell(
                glyph = CupertinoGlyph.Grid,
                description = stringResource(R.string.om_vm_grid),
                state = if (ViewModes.grid) onLabel else offLabel,
                on = ViewModes.grid,
                tag = "viewmode.grid",
                modifier = Modifier.fillMaxHeight(),
            ) {
                // Ligar abre o girador do espaçamento; desligar fecha os dois.
                ViewModes.grid = !ViewModes.grid
                ViewModes.spacingOpen = ViewModes.grid
            }
            if (ViewModes.spacingOpen && ViewModes.grid) {
                val density = LocalDensity.current
                val provider = remember(density) { with(density) { SideOfAnchor(8.dp.roundToPx(), 6.dp.roundToPx()) } }
                Popup(
                    popupPositionProvider = provider,
                    onDismissRequest = { ViewModes.spacingOpen = false },
                    properties = PopupProperties(focusable = true),
                ) {
                    GridSpacingSpinner(ViewModes.gridSpacing) { ViewModes.changeGridSpacing(it) }
                }
            }
        }
        val soloState = when (ViewModes.solo) {
            1 -> stringResource(R.string.om_vm_solo_only)
            2 -> stringResource(R.string.om_vm_solo_dim)
            else -> stringResource(R.string.om_vm_solo_off)
        }
        ViewModeCell(
            glyph = when (ViewModes.solo) {
                1 -> ShellGlyph.SoloOn
                2 -> CupertinoGlyph.CircleLefthalfFill
                else -> ShellGlyph.SoloOff
            },
            description = stringResource(R.string.om_vm_solo),
            state = soloState,
            on = ViewModes.solo != 0,
            enabled = hasSelection,
            tag = "viewmode.solo",
            modifier = Modifier.weight(1f),
        ) { ViewModes.cycleSolo() }
        // 0.3.3 recursos: esconder/mostrar a caixa da seleção no palco (a 0.3.2 tinha
        // o botão na barra da camada; ela tampa o que está por baixo). Guardado no aparelho.
        ViewModeCell(
            glyph = if (StagePrefs.hideSelectionBox) CupertinoGlyph.EyeSlash else CupertinoGlyph.Square,
            description = stringResource(
                if (StagePrefs.hideSelectionBox) R.string.editor_mostrar_caixa_selecao else R.string.editor_esconder_caixa_selecao,
            ),
            state = if (StagePrefs.hideSelectionBox) offLabel else onLabel,
            on = StagePrefs.hideSelectionBox,
            tag = "stage.selectionBox",
            modifier = Modifier.weight(1f),
        ) { StagePrefs.toggleSelectionBox() }
        // Visão pela câmera ativa (D5): existe na coluna, desligada. A prévia do
        // AUREA ainda olha sempre pela câmera ativa; desligar essa visão no motor
        // sem desalinhar as alças do palco fica para a próxima onda.
        ViewModeCell(
            glyph = if (ViewModes.camera) CupertinoGlyph.VideocamFill else CupertinoGlyph.Videocam,
            description = stringResource(R.string.om_vm_camera),
            state = if (hasCamera) stringResource(R.string.om_vm_camera_unavailable) else offLabel,
            on = false,
            enabled = false,
            tag = "viewmode.camera",
            modifier = Modifier.weight(1f),
        ) {}
        ZoomStepsCell(store)
    }
}

/** Uma célula da coluna (peso 1): o ícone em destaque quando ligada; apagada a 25 %. */
@Composable
private fun ViewModeCell(
    glyph: Char,
    description: String,
    state: String,
    on: Boolean,
    tag: String,
    /** O peso na coluna (`Modifier.weight(1f)`) ou a altura do Box da grade. */
    modifier: Modifier,
    enabled: Boolean = true,
    onClick: () -> Unit,
) {
    Box(
        modifier
            .fillMaxWidth()
            .alpha(if (enabled) 1f else 0.25f)
            .testTag(tag)
            .semantics {
                contentDescription = description
                stateDescription = state
                role = Role.Button
                if (!enabled) disabled()
            }
            .tocavel(enabled = enabled, haptic = true, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        CupertinoIcon(glyph, 20.dp, if (on) AureaColors.Accent else AureaColors.Text)
    }
}

/** A célula do zoom (peso 1,8): + · "NN%" · −, em degraus; tocar no número volta a 100 %. */
@Composable
private fun ColumnScope.ZoomStepsCell(store: EditorStore) {
    val zoom = StageView.zoom
    val pct = StageZoomMath.percent(zoom)
    val fit = stringResource(R.string.stage_zoom_fit, pct)
    val zoomIn = stringResource(R.string.om_vm_zoom_in)
    val zoomOut = stringResource(R.string.om_vm_zoom_out)
    Column(
        Modifier
            .weight(1.8f)
            .fillMaxWidth()
            .padding(5.dp)
            .clip(RoundedCornerShape(3.dp))
            .background(OmegaChrome.ZoomChip),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(
            Modifier
                .weight(1f)
                .fillMaxWidth()
                .testTag("viewmode.zoomIn")
                .semantics {
                    contentDescription = zoomIn
                    role = Role.Button
                }
                .tocavel(enabled = zoom < StageZoomMath.MAX, haptic = true) {
                    StageView.zoomTo(store, StageZoomMath.stepUp(StageView.zoom))
                },
            contentAlignment = Alignment.Center,
        ) { CupertinoIcon(CupertinoGlyph.Plus, 16.dp, AureaColors.Text) }
        Text(
            stringResource(R.string.common_percent, pct),
            maxLines = 1,
            textAlign = TextAlign.Center,
            style = AureaType.Base.merge(TextStyle(fontSize = 10.sp, color = AureaColors.Text)).merge(AureaType.Tabular),
            modifier = Modifier
                .fillMaxWidth()
                .height(20.dp)
                .testTag("viewmode.zoomLabel")
                .semantics {
                    contentDescription = fit
                    role = Role.Button
                }
                .tocavel(haptic = true) { StageView.zoomTo(store, 1f) },
        )
        Box(
            Modifier
                .weight(1f)
                .fillMaxWidth()
                .testTag("viewmode.zoomOut")
                .semantics {
                    contentDescription = zoomOut
                    role = Role.Button
                }
                .tocavel(enabled = zoom > StageZoomMath.MIN, haptic = true) {
                    StageView.zoomTo(store, StageZoomMath.stepDown(StageView.zoom))
                },
            contentAlignment = Alignment.Center,
        ) { CupertinoIcon(CupertinoGlyph.Minus, 16.dp, AureaColors.Text) }
    }
}

/** O girador fica À ESQUERDA da âncora (à direita em RTL), centrado nela na vertical. */
private class SideOfAnchor(private val margin: Int, private val gap: Int) : PopupPositionProvider {
    override fun calculatePosition(
        anchorBounds: IntRect,
        windowSize: IntSize,
        layoutDirection: LayoutDirection,
        popupContentSize: IntSize,
    ): IntOffset {
        val x = if (layoutDirection == LayoutDirection.Rtl) anchorBounds.right + gap
        else anchorBounds.left - gap - popupContentSize.width
        val y = anchorBounds.top + anchorBounds.height / 2 - popupContentSize.height / 2
        return IntOffset(
            x.coerceIn(margin, max(margin, windowSize.width - popupContentSize.width - margin)),
            y.coerceIn(margin, max(margin, windowSize.height - popupContentSize.height - margin)),
        )
    }
}

/**
 * O girador vertical do espaçamento da grade (45 × 179, a referência): o
 * número em cima e a régua embaixo, com a agulha no destaque e a marca
 * brilhante em 40. Arrastar para cima aumenta; 5 a 500 px da composição.
 */
@Composable
private fun GridSpacingSpinner(value: Int, onValue: (Int) -> Unit) {
    val tickPx = with(LocalDensity.current) { 4.dp.toPx() }
    val current = rememberUpdatedState(value)
    val change = rememberUpdatedState(onValue)
    var carry by remember { mutableFloatStateOf(0f) }
    val label = stringResource(R.string.om_vm_grid_spacing)
    val accent = AureaColors.Accent
    val ink = AureaColors.Text
    Column(
        Modifier
            .size(45.dp, 179.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(OmegaChrome.Column)
            .testTag("viewmode.grid.spacing")
            .semantics {
                contentDescription = label
                stateDescription = "$value"
                progressBarRangeInfo = ProgressBarRangeInfo(
                    value.toFloat(), ViewModes.GRID_MIN.toFloat()..ViewModes.GRID_MAX.toFloat(),
                )
                setProgress { v ->
                    change.value(v.roundToInt())
                    true
                }
            },
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            "$value",
            maxLines = 1,
            style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, fontWeight = FontWeight.W600, color = AureaColors.Text))
                .merge(AureaType.Tabular),
            modifier = Modifier.padding(top = 6.dp, bottom = 4.dp),
        )
        Canvas(
            Modifier
                .width(36.dp)
                .weight(1f)
                .padding(bottom = 6.dp)
                .pointerInput(Unit) {
                    detectVerticalDragGestures(onDragStart = { carry = 0f }) { pointer, dy ->
                        pointer.consume()
                        // Para cima aumenta: a régua anda junto com o dedo.
                        carry -= dy
                        val steps = (carry / tickPx).toInt()
                        if (steps != 0) {
                            carry -= steps * tickPx
                            change.value(current.value + steps)
                        }
                    }
                },
        ) {
            val centre = size.height / 2f
            val reach = (centre / tickPx).toInt() + 1
            val v0 = current.value
            for (k in -reach..reach) {
                val v = v0 + k
                if (v < ViewModes.GRID_MIN || v > ViewModes.GRID_MAX) continue
                val y = centre - k * tickPx
                val mark = v == ViewModes.GRID_MARK
                val length = when {
                    mark || v % 10 == 0 -> size.width * 0.8f
                    v % 5 == 0 -> size.width * 0.55f
                    else -> size.width * 0.3f
                }
                val color = if (mark) ink else ink.copy(alpha = 0.4f)
                drawLine(
                    color,
                    Offset((size.width - length) / 2f, y),
                    Offset((size.width + length) / 2f, y),
                    strokeWidth = (if (mark) 2.dp else 1.dp).toPx(),
                )
            }
            // A agulha.
            drawLine(accent, Offset(0f, centre), Offset(size.width, centre), strokeWidth = 2.dp.toPx())
        }
    }
}

/**
 * O botão de modo zoom do canvas (canto sup-esq da prévia, 10 dp abaixo do
 * topo) e o rótulo "NN%" logo abaixo. Com camada escolhida o botão fica e liga
 * ou desliga [StageView.zoomLock] (a pinça mexe na vista em vez da camada).
 * Sem camada, a pinça sempre mexe na vista: o botão só aparece junto com o
 * rótulo, apagado e sem efeito. O rótulo aparece a cada zoom/pan da vista e
 * some com alfa em 2000 ms.
 */
@Composable
internal fun CanvasZoomMode(store: EditorStore, modifier: Modifier) {
    val touch = StageView.viewTouch
    val fade = remember { Animatable(0f) }
    LaunchedEffect(touch) {
        if (touch == 0) return@LaunchedEffect
        fade.snapTo(1f)
        delay(300)
        fade.animateTo(0f, tween(2000))
    }
    val hasSelection = store.selection.isNotEmpty()
    val shown = fade.value
    Column(modifier.padding(top = 10.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        if (hasSelection || shown > 0f) {
            val on = StageView.zoomLock
            val description = stringResource(if (on) R.string.stage_zoom_view_on else R.string.stage_zoom_view_off)
            Box(
                Modifier
                    .size(48.dp)
                    .alpha(if (hasSelection) 1f else shown)
                    .testTag("stage.zoom.toggle")
                    .semantics {
                        contentDescription = description
                        role = Role.Switch
                        if (!hasSelection) disabled()
                    }
                    .tocavel(enabled = hasSelection, haptic = true) { StageView.zoomLock = !StageView.zoomLock },
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(
                    ShellGlyph.ZoomIn, 22.dp,
                    when {
                        !hasSelection -> OmegaChrome.Secondary
                        on -> AureaColors.Accent
                        else -> AureaColors.Text
                    },
                )
            }
        }
        if (shown > 0f) {
            Text(
                stringResource(R.string.common_percent, StageZoomMath.percent(StageView.zoom)),
                maxLines = 1,
                textAlign = TextAlign.Center,
                style = AureaType.Base.merge(
                    TextStyle(
                        fontSize = 12.sp,
                        color = OmegaChrome.Secondary,
                        shadow = Shadow(AureaColors.ImageScrim, Offset(0f, 1f), 3f),
                    ),
                ).merge(AureaType.Tabular),
                modifier = Modifier.width(50.dp).alpha(shown).testTag("stage.zoom.label"),
            )
        }
    }
}
