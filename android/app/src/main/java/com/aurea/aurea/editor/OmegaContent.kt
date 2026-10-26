package com.aurea.aurea.editor

import android.app.Application
import android.content.Context
import android.content.SharedPreferences
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.i18n.AppText
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.CupertinoIconBoldStroke
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel

// =============================================================================
// Omega conteúdo (área 06): as peças comuns dos editores de conteúdo — abrir o
// editor do tipo da camada (toque duplo no palco e depois de criar), as ações
// da barra da seção (Editar pontos e Editar forma), as preferências do
// aparelho e os ícones próprios que o CupertinoIcons não tem.
// =============================================================================

/** Ícones desenhados pelo AUREA (sem par no CupertinoIcons). Mesmos traços do Swift. */
internal enum class ContentIconKind { AlignLeft, AlignCenter, AlignRight, Eraser, PointCorner, PointCurve, AngleLock, LengthLock, FromCenter }

@Composable
internal fun ContentIcon(kind: ContentIconKind, size: Dp, tint: Color, modifier: Modifier = Modifier) {
    Canvas(modifier.size(size)) {
        val u = this.size.minDimension / 24f
        // 0.3.3: traço 1,8 + o extra dos glifos bold (CupertinoIconBoldStroke), como o desfoque de movimento.
        val sw = (1.8f + 24f * CupertinoIconBoldStroke) * u
        val stroke = Stroke(width = sw, cap = StrokeCap.Round, join = StrokeJoin.Round)
        fun line(x0: Float, y0: Float, x1: Float, y1: Float) =
            drawLine(tint, Offset(x0 * u, y0 * u), Offset(x1 * u, y1 * u), strokeWidth = sw, cap = StrokeCap.Round)
        when (kind) {
            ContentIconKind.AlignLeft, ContentIconKind.AlignCenter, ContentIconKind.AlignRight -> {
                // Quatro linhas: longa, curta, longa, curta — encostadas pelo lado do alinhamento.
                val widths = floatArrayOf(16f, 10f, 16f, 10f)
                for (i in 0 until 4) {
                    val w = widths[i]
                    val x0 = when (kind) {
                        ContentIconKind.AlignLeft -> 4f
                        ContentIconKind.AlignRight -> 20f - w
                        else -> 12f - w / 2f
                    }
                    val y = 6f + i * 4f
                    line(x0, y, x0 + w, y)
                }
            }
            ContentIconKind.Eraser -> {
                // A borracha inclinada: o corpo e a ponta, e a linha do chão.
                rotate(-40f, Offset(12f * u, 12f * u)) {
                    drawRoundRect(tint, Offset(5f * u, 8f * u), Size(14f * u, 8f * u), CornerRadius(1.6f * u), style = stroke)
                    drawLine(tint, Offset(10f * u, 8f * u), Offset(10f * u, 16f * u), strokeWidth = sw)
                }
                line(4f, 20.5f, 20f, 20.5f)
            }
            ContentIconKind.PointCorner -> {
                // Canto: duas retas que se encontram num ponto.
                val p = Path().apply { moveTo(4f * u, 19f * u); lineTo(12f * u, 6f * u); lineTo(20f * u, 19f * u) }
                drawPath(p, tint, style = stroke)
                drawRect(tint, Offset(10f * u, 4f * u), Size(4f * u, 4f * u))
            }
            ContentIconKind.PointCurve -> {
                // Curva: o arco com o ponto e as duas alças.
                val p = Path().apply { moveTo(4f * u, 19f * u); cubicTo(6f * u, 6f * u, 18f * u, 6f * u, 20f * u, 19f * u) }
                drawPath(p, tint, style = stroke)
                line(5f, 9.5f, 19f, 9.5f)
                drawCircle(tint, 2f * u, Offset(12f * u, 9.5f * u))
                drawCircle(tint, 1.4f * u, Offset(5f * u, 9.5f * u))
                drawCircle(tint, 1.4f * u, Offset(19f * u, 9.5f * u))
            }
            ContentIconKind.AngleLock -> {
                // Alça presa na reta: duas alças opostas e um cadeado pequeno.
                line(3f, 15f, 21f, 15f)
                drawCircle(tint, 1.8f * u, Offset(12f * u, 15f * u))
                drawRoundRect(tint, Offset(8.5f * u, 4.5f * u), Size(7f * u, 5.5f * u), CornerRadius(1f * u))
                val arc = Path().apply { moveTo(10f * u, 4.5f * u); cubicTo(10f * u, 1.5f * u, 14f * u, 1.5f * u, 14f * u, 4.5f * u) }
                drawPath(arc, tint, style = Stroke((1.4f + 24f * CupertinoIconBoldStroke) * u))
            }
            ContentIconKind.LengthLock -> {
                // Comprimento igual: a régua com as duas pontas.
                line(4f, 12f, 20f, 12f)
                line(4f, 8f, 4f, 16f)
                line(20f, 8f, 20f, 16f)
                line(7f, 9f, 4f, 12f); line(7f, 15f, 4f, 12f)
                line(17f, 9f, 20f, 12f); line(17f, 15f, 20f, 12f)
            }
            ContentIconKind.FromCenter -> {
                // Quatro setas saindo do centro.
                drawCircle(tint, 1.8f * u, Offset(12f * u, 12f * u))
                line(9f, 9f, 4f, 4f); line(4f, 4f, 8f, 4f); line(4f, 4f, 4f, 8f)
                line(15f, 9f, 20f, 4f); line(20f, 4f, 16f, 4f); line(20f, 4f, 20f, 8f)
                line(9f, 15f, 4f, 20f); line(4f, 20f, 8f, 20f); line(4f, 20f, 4f, 16f)
                line(15f, 15f, 20f, 20f); line(20f, 20f, 16f, 20f); line(20f, 20f, 20f, 16f)
            }
        }
    }
}

// =============================================================================
// Preferências do aparelho (P0-4, P0-6): ficam entre projetos.
// =============================================================================
internal object ContentPrefs {
    private const val FILE_SHAPE = "aurea.shape"
    private const val FILE_DRAW = "aurea.draw"
    private var shape: SharedPreferences? = null
    private var draw: SharedPreferences? = null

    /** Forma: manter proporção / mudar a partir do centro (padrão = o de sempre: centro). */
    var lockAspect by mutableStateOf(false)
        private set
    var fromCenter by mutableStateOf(true)
        private set

    /** Desenho: ferramenta (0 caneta…3 borracha), espessura (px) e cor (ARGB). */
    var drawTool by mutableStateOf(0)
        private set
    var drawWidth by mutableStateOf(12)
        private set
    var drawColor by mutableStateOf(0xFFFFFFFF.toInt())
        private set

    fun load(context: Context) {
        if (shape == null) {
            val p = context.applicationContext.getSharedPreferences(FILE_SHAPE, Context.MODE_PRIVATE)
            shape = p
            lockAspect = p.getBoolean("lockAspect", false)
            fromCenter = p.getBoolean("fromCenter", true)
        }
        if (draw == null) {
            val p = context.applicationContext.getSharedPreferences(FILE_DRAW, Context.MODE_PRIVATE)
            draw = p
            drawTool = p.getInt("tool", 0).coerceIn(0, 3)
            drawWidth = p.getInt("width", 12).coerceIn(1, 300)
            drawColor = p.getInt("color", 0xFFFFFFFF.toInt())
        }
    }

    @JvmName("saveLockAspect") fun setLockAspect(on: Boolean) { lockAspect = on; shape?.edit()?.putBoolean("lockAspect", on)?.apply() }
    @JvmName("saveFromCenter") fun setFromCenter(on: Boolean) { fromCenter = on; shape?.edit()?.putBoolean("fromCenter", on)?.apply() }
    @JvmName("saveDrawTool") fun setDrawTool(tool: Int) { drawTool = tool.coerceIn(0, 3); draw?.edit()?.putInt("tool", drawTool)?.apply() }
    @JvmName("saveDrawWidth") fun setDrawWidth(width: Int) { drawWidth = width.coerceIn(1, 300); draw?.edit()?.putInt("width", drawWidth)?.apply() }
    @JvmName("saveDrawColor") fun setDrawColor(argb: Int) { drawColor = argb; draw?.edit()?.putInt("color", argb)?.apply() }
}

// =============================================================================
// Abrir o editor do tipo da camada (P0-7)
// =============================================================================

/**
 * O editor de conteúdo da camada: texto (2D/3D) → modo de texto; desenho →
 * editor de desenho; vetor → Editar pontos; forma → Editar forma; grupo →
 * entra no grupo. Devolve falso quando o tipo não tem editor.
 */
internal fun openContentEditorFor(store: EditorStore, ui: EditorUi, layerId: Long): Boolean {
    if (store.primary != layerId) return false
    val d = store.detail ?: return false
    if (d.locked) return false
    return when {
        d.kind == LayerType.Text.kind || store.text3d != null -> store.openTextEdit(selectAll = false)
        d.kind == LayerType.Shape.kind && store.isDrawingLayer -> { openPanel(store, ui, EditorPanel.Drawing); true }
        d.kind == LayerType.Shape.kind && store.isVectorLayer -> { openPanel(store, ui, EditorPanel.EditPoints); true }
        d.kind == LayerType.Shape.kind -> { openPanel(store, ui, EditorPanel.ShapeEdit); true }
        d.kind == LayerType.Group.kind -> { store.openPrecomp(layerId); true }
        else -> false
    }
}

/** Último toque numa camada do palco (o toque duplo abre o editor do tipo). */
private object ContentTap { var at = 0L; var layer = 0L }

/**
 * Chamado a cada toque que acerta uma camada (Stage.kt). Um segundo toque na
 * MESMA camada, já escolhida e com a doca ou o painel da camada à vista, dentro
 * de [timeoutMs], abre o editor do tipo dela. Verdadeiro = o toque foi o duplo.
 */
internal fun omegaContentDoubleTap(store: EditorStore, ui: EditorUi, hit: Long, now: Long, timeoutMs: Long): Boolean {
    val again = ContentTap.layer == hit && now - ContentTap.at in 0..timeoutMs
    ContentTap.at = if (again) 0L else now
    ContentTap.layer = hit
    if (!again || store.primary != hit || store.selection.size != 1 || ui.adding || store.textEditLayer != null) return false
    return openContentEditorFor(store, ui, hit)
}

// =============================================================================
// Ações da barra da seção (P0-6)
// =============================================================================

/** Botão de 40 dp da barra da seção (alvo de 44 com a folga do Compose). */
@Composable
internal fun RowScope.SectionBarButton(
    label: String,
    tag: String,
    on: Boolean,
    enabled: Boolean = true,
    onClick: () -> Unit,
    icon: @Composable (Color) -> Unit,
) {
    val tint = if (on) AureaColors.Accent else AureaColors.Text
    Box(
        Modifier
            .size(44.dp)
            .testTag(tag)
            .semantics { contentDescription = label; selected = on }
            .alpha(if (enabled) 1f else 0.3f)
            .tocavel(enabled = enabled, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { icon(tint) }
}

/**
 * O que vai à direita da barra da seção: o segmento ◆/• do Editar pontos e as
 * duas chaves do Editar forma (manter proporção, a partir do centro).
 */
@Composable
internal fun RowScope.ContentSectionTrailing(store: EditorStore, panel: EditorPanel) {
    when (panel) {
        EditorPanel.EditPoints -> {
            // Segmento de dois botões: ◆ keyframes da forma | • pontos.
            Row(
                Modifier.padding(end = 4.dp).clip(RoundedCornerShape(8.dp)).background(AureaColors.Chip),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                val keys = EditPointsState.keysMode
                SectionBarButton(stringResource(R.string.om_pts_mode_keys_cd), "points.mode.keys", on = keys,
                    onClick = { EditPointsState.keysMode = true }) { CupertinoIcon(CupertinoGlyph.RhombusFill, 18.dp, it) }
                SectionBarButton(stringResource(R.string.om_pts_mode_points_cd), "points.mode.points", on = !keys,
                    onClick = { EditPointsState.keysMode = false }) { CupertinoIcon(CupertinoGlyph.CircleFill, 12.dp, it) }
            }
        }
        EditorPanel.ShapeEdit -> {
            val ratioOk = shapeAcceptsRatio(store)
            val lockText = AppText.get(store.getApplication<Application>(), if (!ContentPrefs.lockAspect) R.string.om_shp_ratio_locked else R.string.om_shp_ratio_free)
            val centerText = AppText.get(store.getApplication<Application>(), if (!ContentPrefs.fromCenter) R.string.om_shp_from_center else R.string.om_shp_from_edge)
            SectionBarButton(stringResource(R.string.om_shp_lock_ratio_cd), "shape.lockAspect", on = ContentPrefs.lockAspect, enabled = ratioOk,
                onClick = { ContentPrefs.setLockAspect(!ContentPrefs.lockAspect); store.showToast(lockText) }) {
                CupertinoIcon(if (ContentPrefs.lockAspect) CupertinoGlyph.LockFill else CupertinoGlyph.LockOpen, 20.dp, it)
            }
            SectionBarButton(stringResource(R.string.om_shp_from_center_cd), "shape.fromCenter", on = ContentPrefs.fromCenter, enabled = ratioOk,
                onClick = { ContentPrefs.setFromCenter(!ContentPrefs.fromCenter); store.showToast(centerText) }) {
                ContentIcon(ContentIconKind.FromCenter, 22.dp, it)
            }
            Box(Modifier.width(4.dp).fillMaxHeight())
        }
        else -> {}
    }
}

/**
 * A forma aceita as chaves de proporção e de centro? Só as formas com caixa de
 * largura × altura (o `StB` da referência); a camada vetorial não.
 */
internal fun shapeAcceptsRatio(store: EditorStore): Boolean {
    val d = store.detail ?: return false
    return d.kind == LayerType.Shape.kind && !store.isVectorLayer && !d.locked
}
