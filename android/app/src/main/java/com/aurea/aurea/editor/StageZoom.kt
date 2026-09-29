package com.aurea.aurea.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.FitScreen
import androidx.compose.material.icons.rounded.ZoomIn
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.abs
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
    const val MIN = 1f
    const val MAX = 8f

    /** Zoom preso em [1×, 8×]; lixo (NaN/∞) volta ao encaixe. */
    fun clampZoom(z: Float): Float = if (z.isFinite()) z.coerceIn(MIN, MAX) else MIN

    /**
     * Pan máximo num eixo: a borda da composição pode chegar até onde ela fica
     * no encaixe (100 %). Em 1× não há pan. [baseExtent] = tamanho da
     * composição na tela em 1× (px).
     */
    fun maxPan(zoom: Float, baseExtent: Float): Float = ((zoom - 1f) * baseExtent / 2f).coerceAtLeast(0f)

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

    /** Está ampliado (o chip de % aparece)? */
    fun isZoomed(zoom: Float): Boolean = zoom > MIN + 0.005f

    /** "250" para 2,5×. */
    fun percent(zoom: Float): Int = (zoom * 100f).roundToInt()
}

/**
 * Estado da vista do palco (sessão do editor, nunca vai para o projeto). O
 * palco lê no desenho; o botão da lupa na barra de transporte liga
 * [zoomLock] (a pinça sempre amplia a vista, mesmo sobre a camada escolhida).
 */
internal object StageView {
    var zoom by mutableFloatStateOf(1f)
        private set
    var panX by mutableFloatStateOf(0f)
        private set
    var panY by mutableFloatStateOf(0f)
        private set
    var zoomLock by mutableStateOf(false)

    val zoomed: Boolean get() = StageZoomMath.isZoomed(zoom)

    /** Aplica (já preso pelo chamador) e manda ao motor só se mudou. */
    fun set(store: EditorStore, z: Float, px: Float, py: Float) {
        val nz = StageZoomMath.clampZoom(z)
        val nx = if (px.isFinite()) px else 0f
        val ny = if (py.isFinite()) py else 0f
        if (abs(nz - zoom) < 1e-5f && abs(nx - panX) < 0.01f && abs(ny - panY) < 0.01f) return
        zoom = nz
        panX = nx
        panY = ny
        store.setViewport(nz, nx, ny)
    }

    /** De volta ao encaixe (100 %). */
    fun reset(store: EditorStore) {
        zoom = 1f
        panX = 0f
        panY = 0f
        store.setViewport(1f, 0f, 0f)
    }
}

/**
 * Chip "250 %" no canto inf-dir do palco: só aparece com a vista ampliada;
 * tocar volta ao encaixe (100 %). Lê o zoom aqui dentro — só o chip recompõe.
 */
@Composable
internal fun StageZoomChip(store: EditorStore, modifier: Modifier) {
    val zoom = StageView.zoom
    if (!StageZoomMath.isZoomed(zoom)) return
    val pct = StageZoomMath.percent(zoom)
    val description = stringResource(R.string.stage_zoom_fit, pct)
    Row(
        modifier
            .heightIn(min = 36.dp)
            .clip(RoundedCornerShape(50))
            .background(ShellColors.ResolutionChip)
            .semantics { contentDescription = description }
            .testTag("stage.zoom.fit")
            .tocavel(haptic = true) { StageView.reset(store) }
            .padding(horizontal = 10.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(5.dp),
    ) {
        Icon(Icons.Rounded.FitScreen, contentDescription = null, tint = AureaColors.Accent, modifier = Modifier.size(15.dp))
        Text(
            stringResource(R.string.common_percent, pct),
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)),
        )
    }
}

/** Fundo do botão da lupa no canto da prévia (Efeitos.dc.html). */
private val StageZoomButtonFill = androidx.compose.ui.graphics.Color(0xFF2A3447)

/**
 * Botão da LUPA no canto sup-esq da prévia (redesenho 2026-09-29): 34×30,
 * colado à borda (raio 0/6/6/0), liga/desliga [StageView.zoomLock] — o mesmo
 * item "Lupa da prévia" do menu da timeline. Ligado, o ícone fica em destaque.
 * O alvo de toque é 48×44 (o desenho menor fica no canto dele).
 */
@Composable
internal fun StageZoomButton(modifier: Modifier) {
    val on = StageView.zoomLock
    val description = stringResource(if (on) R.string.stage_zoom_view_on else R.string.stage_zoom_view_off)
    androidx.compose.foundation.layout.Box(
        modifier
            .size(48.dp, 44.dp)
            .semantics {
                contentDescription = description
                role = androidx.compose.ui.semantics.Role.Switch
            }
            .testTag("stage.zoom.toggle")
            .tocavel(haptic = true) { StageView.zoomLock = !StageView.zoomLock },
    ) {
        androidx.compose.foundation.layout.Box(
            Modifier
                .padding(top = 8.dp)
                .size(34.dp, 30.dp)
                .background(StageZoomButtonFill, RoundedCornerShape(topStart = 0.dp, bottomStart = 0.dp, topEnd = 6.dp, bottomEnd = 6.dp)),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                androidx.compose.material.icons.Icons.Rounded.ZoomIn,
                contentDescription = null,
                tint = if (on) AureaColors.Accent else AureaColors.Text,
                modifier = Modifier.size(18.dp),
            )
        }
    }
}
