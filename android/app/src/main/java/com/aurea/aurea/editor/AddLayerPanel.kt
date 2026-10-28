package com.aurea.aurea.editor

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.EnterExitState
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.SizeTransform
import androidx.compose.animation.core.AnimationSpec
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.snap
import androidx.compose.animation.core.spring
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.offset
import androidx.compose.runtime.DisposableEffect
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.TransformOrigin
import androidx.compose.ui.graphics.compositeOver
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.unit.LayoutDirection
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.StringRes
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyHorizontalGrid
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.itemsIndexed
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.presets.PresetEntry
import com.aurea.aurea.presets.PresetKind
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.ProjectEntry
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.AureaIconStyle
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.sin

/**
 * Destinos do adicionar. 0.3.3 barra: a pílula mostra 5 ([ADD_BAR_PILL]) e o
 * "+" abre a grade de 8 ([ADD_BAR_GRID]); o Vetor abre pelo Elemento.
 *
 * Categorias do adicionar (7.2): ícone grande + nome curto, cada recurso num
 * lugar só. O antigo "Mais" e o trilho da direita (mão livre, vetorial, texto)
 * foram distribuídos aqui: SVG em Vetor, ajuste/agrupar em
 * Elemento, marca e batidas em Áudio. `Shape` continua o 1º (padrão do `openAdd`).
 */
internal enum class AddTab(@StringRes val label: Int, val glyph: Char) {
    Shape(R.string.sh_add_tab_shape, ShellGlyph.SquareOnCircle),
    Media(R.string.sh_add_tab_media, CupertinoGlyph.PhotoOnRectangle),
    Audio(R.string.sh_add_tab_audio, CupertinoGlyph.MusicNote2),
    Text(R.string.sh_add_tab_text, CupertinoGlyph.Textformat),
    // Ao lado do Texto (que cria o texto direto): a faixa dos presets de texto.
    TextPresets(R.string.tp_add_tab, CupertinoGlyph.WandStars),
    Element(R.string.sh_add_tab_element, ShellGlyph.CircleGridHex),
    Model3D(R.string.sh_add_tab_3d, CupertinoGlyph.Cube),
    Draw(R.string.sh_add_tab_draw, ShellGlyph.Scribble),
    Vector(R.string.sh_add_tab_vector, CupertinoGlyph.PencilOutline),
}

/** Preferências de interface do adicionar (não são do projeto). */
internal fun omegaAddPrefs(context: Context) = context.getSharedPreferences("omega_add", Context.MODE_PRIVATE)

// =============================================================================
// O painel embutido (Omega, área 08, P0-1)
// =============================================================================

/**
 * O conteúdo do botão aberto na barra de adicionar, EMBUTIDO acima dela
 * (193 dp no retrato; ver [EditorLayout.ADD_CONTENT]). Sem cabeçalho: o ✕ de
 * 32 dp fica no canto (alvo de 48). Trocar de botão troca o conteúdo; tocar no
 * botão aceso, no palco, no Voltar ou adicionar algo fecha.
 */
@Composable
internal fun AddLayerPanel(store: EditorStore, ui: EditorUi, modifier: Modifier = Modifier.fillMaxSize()) {
    val close = { ui.adding = false }
    val closeLabel = stringResource(R.string.editor_fechar_adicionar)
    // 0.4.1 (mockup "interface 03"): a gaveta é um cartão flutuante — margem de 8, cantos
    // de 24, vidro escuro (#121214 a 96 %) e fio de 14 %.
    val card = RoundedCornerShape(24.dp)
    Box(
        modifier.padding(horizontal = 8.dp, vertical = 4.dp).clip(card)
            .background(Color(0xF5121214)).border(1.dp, Color.White.copy(alpha = 0.14f), card)
            .testTag("add.panel"),
    ) {
        Box(Modifier.fillMaxSize().testTag("editor.addPanel")) {
            when (ui.addTab) {
                AddTab.Shape -> ShapesPager(store, close)
                AddTab.Media -> MediaStrip(store, ui, close)
                AddTab.Audio -> AudioStrip(store, ui, close)
                AddTab.Text -> ObjectGrid(listOf(
                    ObjectCell(stringResource(R.string.sh_add_tab_text), "add.text", glyph = CupertinoGlyph.Textformat) {
                        if (store.addText() >= 0) close()
                    },
                ))
                AddTab.TextPresets -> TextPresetStrip(store, ui, close)
                AddTab.Element -> ElementGrid(store, ui, close)
                AddTab.Model3D -> Model3DGrid(store, close)
                AddTab.Draw -> ObjectGrid(listOf(
                    ObjectCell(stringResource(R.string.editor_mao_livre), "add.draw.freehand", glyph = ShellGlyph.Scribble) {
                        if (store.playing) store.pause()
                        close()
                        store.chooseVectorTool(2)
                    },
                ))
                AddTab.Vector -> VectorGrid(store, ui)
            }
        }
        Box(
            Modifier
                .align(Alignment.TopEnd)
                .size(48.dp)
                .testTag("add.close")
                .semantics { contentDescription = closeLabel }
                .tocavel(shrink = 1f, onClick = close),
            contentAlignment = Alignment.Center,
        ) {
            Box(
                Modifier.size(24.dp).clip(CircleShape).background(AureaColors.SurfaceHigh),
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(CupertinoGlyph.Xmark, 12.dp, AureaColors.Text)
            }
        }
    }
}

// =============================================================================
// Grade de objetos (Elemento, Vetor, 3D): células de 64 dp centradas
// =============================================================================

/** Uma célula: ícone (glifo ou desenho) e rótulo de 10 sp em até 2 linhas. */
internal class ObjectCell(
    val label: String,
    val tag: String,
    val glyph: Char? = null,
    val tint: Color? = null,
    val draw: (DrawScope.() -> Unit)? = null,
    val onClick: () -> Unit,
)

/**
 * Grade centrada da referência (Objeto): margem de 10 dp, células de 64 × 71
 * na primeira fileira e 64 × 64 nas outras; com mais células do que cabem,
 * rola na vertical dentro do painel. [footer] (dica) vai depois da grade.
 */
@Composable
internal fun ObjectGrid(cells: List<ObjectCell>, footer: String? = null, warning: String? = null) {
    // 0.4.1 (mockup "interface 03"): duas colunas de botões (ícone no destaque + nome),
    // fundo #1C1C1E, cantos de 12; rola na vertical dentro do cartão.
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 12.dp, end = 40.dp, top = 12.dp, bottom = 10.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        cells.chunked(2).forEach { row ->
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                row.forEach { cell -> ObjectCellView(cell, Modifier.weight(1f)) }
                if (row.size == 1) Spacer(Modifier.weight(1f))
            }
        }
        if (footer != null) {
            Text(
                footer,
                modifier = Modifier.padding(horizontal = 6.dp, vertical = 6.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, lineHeight = 15.sp, color = AureaColors.Muted)),
            )
        }
        if (warning != null) {
            Text(
                warning,
                modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, lineHeight = 15.sp, color = AureaColors.Warning)),
            )
        }
    }
}

@Composable
private fun ObjectCellView(cell: ObjectCell, modifier: Modifier) {
    val shape = RoundedCornerShape(12.dp)
    Row(
        modifier
            .height(40.dp)
            .clip(shape)
            .background(AureaColors.SurfaceHigh)
            .border(1.dp, Color.White.copy(alpha = 0.03f), shape)
            .semantics { contentDescription = cell.label }
            .testTag(cell.tag)
            .tocavel(onClick = cell.onClick)
            .padding(horizontal = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        when {
            cell.draw != null -> Canvas(Modifier.size(18.dp)) { cell.draw.invoke(this) }
            cell.glyph != null -> CupertinoIcon(cell.glyph, 17.dp, cell.tint ?: AureaColors.Accent)
        }
        Spacer(Modifier.width(8.dp))
        Text(
            cell.label,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, lineHeight = 13.sp, fontWeight = FontWeight.W500, color = AureaColors.Text)),
        )
    }
}

// =============================================================================
// Forma em páginas (P0-2)
// =============================================================================

/**
 * Presets de `Engine::add_shape`, na ordem de leitura da grade. Fora: o 9
 * (hexágono repetido) e o 13 (quase igual ao arredondado) — nada em dobro.
 */
private val SHAPES = listOf(
    0 to R.string.sh_shape_circle, 10 to R.string.sh_shape_square, 1 to R.string.sh_shape_rounded,
    12 to R.string.sh_shape_capsule, 4 to R.string.sh_shape_triangle, 14 to R.string.sh_shape_right_triangle,
    6 to R.string.editor_poligono, 11 to R.string.editor_estrela, 2 to R.string.sh_shape_cross,
    3 to R.string.sh_shape_ring, 5 to R.string.sh_shape_slice, 7 to R.string.sh_shape_flower, 8 to R.string.sh_shape_arrow,
    // Formas vindas do app antigo (presets 15..22 do motor).
    16 to R.string.sh_shape_pentagon, 15 to R.string.sh_shape_octagon, 17 to R.string.sh_shape_trapezoid,
    18 to R.string.sh_shape_parallelogram, 19 to R.string.sh_shape_star4, 20 to R.string.sh_shape_star6,
    21 to R.string.sh_shape_gear, 22 to R.string.sh_shape_double_arrow,
    // Formas paramétricas (presets 23..32 do motor: explosão e os tipos 16..24).
    23 to R.string.sh_shape_burst, 24 to R.string.sh_shape_line, 25 to R.string.sh_shape_diamond,
    26 to R.string.sh_shape_heart, 27 to R.string.sh_shape_seal, 28 to R.string.sh_shape_arc,
    29 to R.string.sh_shape_bubble, 30 to R.string.sh_shape_bolt, 31 to R.string.sh_shape_wave,
    32 to R.string.sh_shape_blob,
)

/**
 * Páginas de 3 fileiras × `max(4, (largura − 18)/48)` colunas, células de
 * 48 dp com a miniatura de 40 (branco a 70 %), sobra repartida entre as
 * colunas, pontos de página embaixo. Sem rótulo visível (fica na
 * acessibilidade). Tocar põe a forma no centro, escolhida, e fecha.
 */
@Composable
private fun ShapesPager(store: EditorStore, close: () -> Unit) {
    BoxWithConstraints(Modifier.fillMaxSize().testTag("add.shape.pager")) {
        val usable = maxWidth - 18.dp
        val cols = max(4, (usable / 48.dp).toInt())
        val perPage = cols * 3
        val pages = (SHAPES.size + perPage - 1) / perPage
        val state = rememberPagerState { pages }
        val cellWidth = usable / cols
        Column(Modifier.fillMaxSize().padding(vertical = 5.dp)) {
            HorizontalPager(state, Modifier.weight(1f).fillMaxWidth()) { page ->
                val slice = SHAPES.drop(page * perPage).take(perPage)
                Column(
                    Modifier.fillMaxSize().padding(horizontal = 9.dp),
                    verticalArrangement = Arrangement.Center,
                ) {
                    for (row in 0 until 3) {
                        Row {
                            for (col in 0 until cols) {
                                val shape = slice.getOrNull(row * cols + col)
                                Box(Modifier.width(cellWidth).height(48.dp), contentAlignment = Alignment.Center) {
                                    if (shape != null) {
                                        ShapeCell(shape.first, stringResource(shape.second)) {
                                            store.addShape(shape.first)
                                            close()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            PageDots(pages, state.currentPage, Modifier.align(Alignment.CenterHorizontally).testTag("add.shape.dots"))
        }
    }
}

@Composable
private fun ShapeCell(preset: Int, label: String, onClick: () -> Unit) {
    val ink = AureaColors.Text.copy(alpha = 0.7f)
    Box(
        Modifier
            .size(48.dp)
            .semantics { contentDescription = label }
            .testTag("add.shape.$preset")
            .tocavel(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Canvas(Modifier.size(40.dp).padding(2.5.dp)) { drawShapePreset(preset, ink) }
    }
}

/** Pontos de página (10 dp de altura), o atual em destaque. */
@Composable
internal fun PageDots(count: Int, current: Int, modifier: Modifier = Modifier) {
    val description = stringResource(R.string.om_add_shape_page, current + 1, count)
    Row(
        modifier.height(10.dp).semantics { contentDescription = description },
        horizontalArrangement = Arrangement.spacedBy(6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        repeat(count) { i ->
            Box(Modifier.size(6.dp).clip(CircleShape).background(if (i == current) AureaColors.Accent else AureaColors.Text.copy(alpha = 0.3f)))
        }
    }
}

/** Silhueta de cada preset do motor (mesmo tipo e proporção que a camada nasce). */
private fun DrawScope.drawShapePreset(preset: Int, fill: Color = ShellColors.DockTileContent) {
    val s = size.minDimension
    val o = Offset((size.width - s) / 2, (size.height - s) / 2)
    fun p(x: Float, y: Float) = Offset(o.x + x * s, o.y + y * s)
    fun poly(vararg pts: Float): Path = Path().apply {
        moveTo(p(pts[0], pts[1]).x, p(pts[0], pts[1]).y)
        var i = 2
        while (i < pts.size) {
            lineTo(p(pts[i], pts[i + 1]).x, p(pts[i], pts[i + 1]).y)
            i += 2
        }
        close()
    }
    fun regular(n: Int, r: Float, cy: Float = 0.5f): Path = Path().apply {
        for (k in 0 until n) {
            val a = -Math.PI / 2 + k * 2 * Math.PI / n
            val q = p(0.5f + r * cos(a).toFloat(), cy + r * sin(a).toFloat())
            if (k == 0) moveTo(q.x, q.y) else lineTo(q.x, q.y)
        }
        close()
    }
    when (preset) {
        0 -> drawCircle(fill, s / 2, p(0.5f, 0.5f))
        10 -> drawRect(fill, p(0.04f, 0.04f), Size(s * 0.92f, s * 0.92f))
        1 -> drawRoundRect(fill, p(0.04f, 0.04f), Size(s * 0.92f, s * 0.92f), CornerRadius(s * 0.2f))
        12 -> drawRoundRect(fill, p(0f, 0.34f), Size(s, s * 0.32f), CornerRadius(s * 0.16f))
        4 -> drawPath(poly(0.5f, 0.04f, 0.98f, 0.9f, 0.02f, 0.9f), fill)
        14 -> drawPath(poly(0.06f, 0.06f, 0.94f, 0.94f, 0.06f, 0.94f), fill)
        6 -> drawPath(regular(6, 0.5f), fill)
        11 -> {
            val star = Path()
            for (k in 0 until 10) {
                val r = if (k % 2 == 0) 0.52f else 0.23f
                val a = -Math.PI / 2 + k * Math.PI / 5
                val q = p(0.5f + r * cos(a).toFloat(), 0.55f + r * sin(a).toFloat())
                if (k == 0) star.moveTo(q.x, q.y) else star.lineTo(q.x, q.y)
            }
            star.close()
            drawPath(star, fill)
        }
        2 -> {
            drawRect(fill, p(0.33f, 0f), Size(s * 0.34f, s))
            drawRect(fill, p(0f, 0.33f), Size(s, s * 0.34f))
        }
        3 -> drawCircle(fill, s * 0.4f, p(0.5f, 0.5f), style = Stroke(s * 0.2f))
        5 -> drawArc(fill, 0f, 270f, true, p(0f, 0f), Size(s, s))
        7 -> for (k in 0 until 6) {
            val a = -Math.PI / 2 + k * Math.PI / 3
            val c = p(0.5f + 0.25f * cos(a).toFloat(), 0.5f + 0.25f * sin(a).toFloat())
            val r = Rect(c.x - s * 0.12f, c.y - s * 0.26f, c.x + s * 0.12f, c.y + s * 0.26f)
            val deg = Math.toDegrees(a).toFloat() + 90f
            drawContext.transform.rotate(deg, c)
            drawOval(fill, r.topLeft, r.size)
            drawContext.transform.rotate(-deg, c)
        }
        15 -> drawPath(regular(8, 0.5f), fill)
        16 -> drawPath(regular(5, 0.5f, 0.53f), fill)
        17 -> drawPath(poly(0.2f, 0.2f, 0.8f, 0.2f, 1f, 0.8f, 0f, 0.8f), fill)
        18 -> drawPath(poly(0.25f, 0.25f, 1f, 0.25f, 0.75f, 0.75f, 0f, 0.75f), fill)
        19, 20 -> {
            val n = if (preset == 19) 4 else 6
            val inner = if (preset == 19) 0.2f else 0.28f
            val star = Path()
            for (k in 0 until n * 2) {
                val r = if (k % 2 == 0) 0.5f else inner
                val a = -Math.PI / 2 + k * Math.PI / n
                val q = p(0.5f + r * cos(a).toFloat(), 0.5f + r * sin(a).toFloat())
                if (k == 0) star.moveTo(q.x, q.y) else star.lineTo(q.x, q.y)
            }
            star.close()
            drawPath(star, fill)
        }
        21 -> drawPath(com.aurea.aurea.editor.panels.gearGlyph(p(0.5f, 0.5f), s * 0.5f, 10, 0.3f), fill)
        22 -> drawPath(com.aurea.aurea.editor.panels.doubleArrowGlyph(p(0.5f, 0.5f), s * 0.5f), fill)
        23 -> {
            // Explosão: estrela de 14 pontas rasas.
            val burst = Path()
            for (k in 0 until 28) {
                val r = if (k % 2 == 0) 0.5f else 0.36f
                val a = -Math.PI / 2 + k * Math.PI / 14
                val q = p(0.5f + r * cos(a).toFloat(), 0.5f + r * sin(a).toFloat())
                if (k == 0) burst.moveTo(q.x, q.y) else burst.lineTo(q.x, q.y)
            }
            burst.close()
            drawPath(burst, fill)
        }
        24 -> drawRoundRect(fill, p(0f, 0.44f), Size(s, s * 0.12f), CornerRadius(s * 0.03f))
        25 -> drawPath(com.aurea.aurea.editor.panels.diamondGlyph(p(0.5f, 0.5f), s * 0.5f), fill)
        26 -> drawPath(com.aurea.aurea.editor.panels.heartGlyph(p(0.5f, 0.5f), s * 0.5f), fill)
        27 -> drawPath(com.aurea.aurea.editor.panels.sealGlyph(p(0.5f, 0.5f), s * 0.5f, 14), fill)
        28 -> drawArc(fill, 0f, 270f, false, p(0.1f, 0.1f), Size(s * 0.8f, s * 0.8f), style = Stroke(s * 0.2f))
        29 -> drawPath(com.aurea.aurea.editor.panels.bubbleGlyph(p(0.5f, 0.5f), s * 0.5f), fill)
        30 -> drawPath(com.aurea.aurea.editor.panels.boltGlyph(p(0.5f, 0.5f), s * 0.5f), fill)
        31 -> drawPath(com.aurea.aurea.editor.panels.waveGlyph(p(0.5f, 0.5f), s * 0.5f, 3), fill, style = Stroke(s * 0.12f))
        32 -> drawPath(com.aurea.aurea.editor.panels.blobGlyph(p(0.5f, 0.5f), s * 0.5f), fill)
        else -> {   // 8: seta (camada nasce 1,6 : 1)
            drawLine(fill, p(0.04f, 0.5f), p(0.66f, 0.5f), s * 0.2f, StrokeCap.Butt)
            drawPath(poly(0.6f, 0.2f, 0.98f, 0.5f, 0.6f, 0.8f), fill)
        }
    }
}

// =============================================================================
// Elemento: grade de objetos (P0-5) e a lista de projetos (P1-6)
// =============================================================================

/**
 * Câmera, Nulo, Grupo vazio e Elemento/Projeto primeiro (a ordem da
 * referência), depois os extras do AUREA. Elemento/Projeto troca o conteúdo
 * pela faixa dos outros projetos.
 */
@Composable
private fun ElementGrid(store: EditorStore, ui: EditorUi, close: () -> Unit) {
    var projects by rememberSaveable { mutableStateOf(false) }
    if (projects) {
        ProjectStrip(store, onBack = { projects = false }, close = close)
        return
    }
    val pickToGroup = stringResource(R.string.sh_add_pick_layers_to_group)
    val emptyGroupName = stringResource(R.string.om_add_empty_group)
    ObjectGrid(
        listOf(
            // D6: Câmera aqui (referência) e também na 3D.
            ObjectCell(stringResource(R.string.om_add_camera), "add.element.camera", glyph = CupertinoGlyph.CameraFill) {
                store.addCamera(); close()
            },
            ObjectCell(stringResource(R.string.sh_add_null), "add.element.null", draw = { drawNullIcon(AureaColors.Text) }) {
                store.addNull(false); close()
            },
            ObjectCell(emptyGroupName, "add.element.emptyGroup", glyph = CupertinoGlyph.Folder) {
                if (store.addEmptyGroup(emptyGroupName) >= 0) close()
            },
            ObjectCell(stringResource(R.string.om_add_element_project), "add.element.project", glyph = CupertinoGlyph.SquareStack3dUp) {
                projects = true
            },
            // Extras do AUREA.
            ObjectCell(stringResource(R.string.particular_title), "add.element.particles", glyph = CupertinoGlyph.Sparkles, tint = ShellColors.Text3D) {
                close(); store.addParticles(20)   // 20 = Particular (preset Padrão)
            },
            // 0.4.1 (mockup "interface 03"): a aurora boreal pronta, numa camada de ajuste.
            ObjectCell(stringResource(R.string.add_element_aurora), "add.element.aurora", glyph = CupertinoGlyph.Sparkles, tint = ShellColors.Text3D) {
                close(); store.addAurora()
            },
            ObjectCell(stringResource(R.string.editor_camada_ajuste), "add.element.adjust", glyph = CupertinoGlyph.WandStars) {
                close(); store.addAdjustmentLayer()
            },
            ObjectCell(stringResource(R.string.scene_flare3d), "add.element.flare", glyph = CupertinoGlyph.Sparkles, tint = ShellColors.Text3D) {
                store.addLight(3); close()
            },
            ObjectCell(stringResource(R.string.grid_builder), "add.element.grid", glyph = ShellGlyph.CircleGridHex) {
                store.createGrid(); close()
            },
            ObjectCell(stringResource(R.string.sh_add_group_selection), "add.element.group", glyph = CupertinoGlyph.FolderFill) {
                if (store.selection.isEmpty()) store.showToast(pickToGroup) else { close(); store.precompose() }
            },
            // 0.3.3 barra: o Vetor (pontos, SVG, PSD) não tem vaga na barra nova; mora aqui.
            ObjectCell(stringResource(R.string.sh_add_tab_vector), "add.element.vector", glyph = CupertinoGlyph.PencilOutline) {
                ui.addTab = AddTab.Vector
            },
        ),
    )
}

/**
 * Faixa de 2 fileiras: [Ver tudo] [Voltar] e os outros projetos do mais
 * recente para o mais antigo (cartão de 75 dp com miniatura sobre xadrez e
 * faixa de título). Tocar insere o projeto como camada de grupo.
 */
@Composable
private fun ProjectStrip(store: EditorStore, onBack: () -> Unit, close: () -> Unit) {
    var all by remember { mutableStateOf(false) }
    val current = store.project.path
    val list = remember(store.projects, current) { store.projects.filter { it.path != current }.sortedByDescending { it.modifiedMs } }
    val insert: (ProjectEntry) -> Unit = { entry -> store.insertProjectAsLayer(entry.path, entry.title); close() }
    BoxWithConstraints(Modifier.fillMaxSize().testTag("add.element.list")) {
        val cell = maxHeight / 2
        LazyHorizontalGrid(
            rows = GridCells.Fixed(2),
            modifier = Modifier.fillMaxSize(),
        ) {
            item {
                StripActionCell(cell, stringResource(R.string.om_add_view_all), CupertinoGlyph.SquareGrid2x2, "add.element.list.viewAll") { all = true }
            }
            item {
                StripActionCell(cell, stringResource(R.string.om_add_back), CupertinoGlyph.ChevronLeft, "add.element.list.back", onClick = onBack)
            }
            itemsIndexed(list, key = { _, e -> e.path }) { _, entry ->
                ProjectCard(cell, entry, Modifier.testTag("add.element.list.item.${File(entry.path).nameWithoutExtension}")) { insert(entry) }
            }
            if (list.isEmpty()) {
                item(span = { GridItemSpan(maxLineSpan) }) {
                    Box(Modifier.width(180.dp).fillMaxHeight(), contentAlignment = Alignment.Center) {
                        Text(stringResource(R.string.om_add_project_none), textAlign = TextAlign.Center,
                            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
                    }
                }
            }
        }
    }
    if (all) ProjectPickerDialog(list, onPick = { all = false; insert(it) }) { all = false }
}

/** Célula de ação das faixas (Ver tudo, Voltar): 75 dp, ícone + texto de 12. */
@Composable
internal fun StripActionCell(cell: Dp, label: String, glyph: Char, tag: String, badge: String? = null, onClick: () -> Unit) {
    Box(Modifier.size(cell), contentAlignment = Alignment.Center) {
        Column(
            Modifier
                .size(75.dp)
                .clip(RoundedCornerShape(2.5.dp))
                .background(AureaColors.AddTile)
                .semantics { contentDescription = label }
                .testTag(tag)
                .tocavel(onClick = onClick)
                .padding(4.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
        ) {
            CupertinoIcon(glyph, 22.dp, AureaColors.Text)
            Spacer(Modifier.height(4.dp))
            Text(label, maxLines = 2, overflow = TextOverflow.Ellipsis, textAlign = TextAlign.Center,
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 13.sp, color = AureaColors.Text)))
        }
        if (badge != null) {
            Text(
                badge,
                modifier = Modifier.align(Alignment.TopEnd).padding(top = 6.dp, end = 4.dp)
                    .clip(RoundedCornerShape(3.dp)).background(AureaColors.AddNewBadge).padding(horizontal = 3.dp, vertical = 1.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 7.sp, fontWeight = FontWeight.W700, color = AureaColors.AddOnNewBadge)),
            )
        }
    }
}

@Composable
private fun ProjectCard(cell: Dp, entry: ProjectEntry, modifier: Modifier = Modifier, onClick: () -> Unit) {
    val label = stringResource(R.string.om_add_project_named, entry.title)
    val thumb by produceState<Bitmap?>(null, entry.thumbnailPath) {
        value = withContext(Dispatchers.IO) {
            entry.thumbnailPath?.let { path -> runCatching { BitmapFactory.decodeFile(path, BitmapFactory.Options().apply { inSampleSize = 2 }) }.getOrNull() }
        }
    }
    Box(Modifier.size(cell), contentAlignment = Alignment.Center) {
        Box(
            modifier
                .size(75.dp)
                .clip(RoundedCornerShape(2.5.dp))
                .drawBehind { drawChecker(AureaColors.AddPanelDeep, AureaColors.AddTile) }
                .semantics { contentDescription = label }
                .tocavel(onClick = onClick),
        ) {
            thumb?.let { Image(it.asImageBitmap(), null, Modifier.fillMaxSize(), contentScale = ContentScale.Fit) }
            TitleStrip(entry.title, Modifier.align(Alignment.BottomCenter))
        }
    }
}

/** Faixa de título dos cartões: véu preto a 27 %, nome 10 sp em negrito. */
@Composable
internal fun TitleStrip(title: String, modifier: Modifier = Modifier) {
    Box(modifier.fillMaxWidth().background(AureaColors.AddTitleScrim).padding(horizontal = 4.dp, vertical = 2.dp)) {
        Text(title, maxLines = 1, overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 10.sp, fontWeight = FontWeight.W700, color = AureaColors.OnImage)))
    }
}

/** Xadrez de 6 dp (fundo transparente dos cartões). */
internal fun DrawScope.drawChecker(a: Color, b: Color) {
    drawRect(a)
    val cell = 6.dp.toPx()
    var y = 0f
    var row = 0
    while (y < size.height) {
        var x = if (row % 2 == 0) 0f else cell
        while (x < size.width) {
            drawRect(b, Offset(x, y), Size(cell, cell))
            x += cell * 2
        }
        y += cell
        row++
    }
}

/** "Ver tudo" dos projetos: a lista inteira em tela cheia, só para escolher. */
@Composable
private fun ProjectPickerDialog(list: List<ProjectEntry>, onPick: (ProjectEntry) -> Unit, onDismiss: () -> Unit) {
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Column(Modifier.fillMaxSize().background(AureaColors.AddPanel).statusBarsPadding().navigationBarsPadding()) {
            BrowserTopBar(stringResource(R.string.om_add_element_project), CupertinoGlyph.Xmark, stringResource(R.string.editor_fechar), "add.element.list.close", onDismiss)
            if (list.isEmpty()) {
                Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    Text(stringResource(R.string.om_add_project_none), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
                }
            } else {
                LazyVerticalGrid(GridCells.Adaptive(96.dp), Modifier.fillMaxSize().padding(6.dp)) {
                    itemsIndexed(list, key = { _, e -> e.path }) { _, entry -> ProjectCard(96.dp, entry) { onPick(entry) } }
                }
            }
        }
    }
}

/** Barra de 42 dp dos navegadores: botão à esquerda (✕ ou ←) e o título centrado. */
@Composable
internal fun BrowserTopBar(title: String, glyph: Char, description: String, tag: String, onClick: () -> Unit, trailing: @Composable () -> Unit = {}) {
    Box(Modifier.fillMaxWidth().height(42.dp).background(AureaColors.AddPanel)) {
        Box(
            Modifier.align(Alignment.CenterStart).size(44.dp).testTag(tag).semantics { contentDescription = description }.tocavel(onClick = onClick),
            contentAlignment = Alignment.Center,
        ) { CupertinoIcon(glyph, 18.dp, AureaColors.Text) }
        Text(title, modifier = Modifier.align(Alignment.Center).padding(horizontal = 56.dp), maxLines = 1, overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)))
        Row(Modifier.align(Alignment.CenterEnd), verticalAlignment = Alignment.CenterVertically) { trailing() }
    }
    Box(Modifier.fillMaxWidth().height(0.5.dp).background(AureaColors.AddPanelDeep))
}

/** Quadrado com a diagonal: o ícone do nulo (camada invisível que serve de pai). */
private fun DrawScope.drawNullIcon(c: Color = ShellColors.DockTileContent) {
    val k = size.minDimension / 30f
    val r = Rect(center.x - 13 * k, center.y - 13 * k, center.x + 13 * k, center.y + 13 * k)
    // 0.3.4: o traço fino do estilo dos ícones (AureaIconStyle.Line na caixa de 24 → × 30/24).
    val w = AureaIconStyle.Line * 30f / 24f * k
    drawRoundRect(c, r.topLeft, r.size, CornerRadius(4 * k), style = Stroke(w))
    drawLine(c, Offset(r.left + 3 * k, r.bottom - 3 * k), Offset(r.right - 3 * k, r.top + 3 * k), w, StrokeCap.Round)
}

// =============================================================================
// 3D (só no AUREA): a mesma grade de objetos
// =============================================================================

@Composable
private fun Model3DGrid(store: EditorStore, close: () -> Unit) {
    // glTF/GLB/FBX/OBJ do aparelho. O tipo MIME de modelo 3D varia por
    // gerenciador de arquivos; o filtro real é a extensão, no store.
    // Seleção múltipla: o FBX/OBJ vem com as texturas e o .mtl (ou um .zip).
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris: List<Uri> ->
        if (uris.isNotEmpty()) {
            store.importModel(uris)
            close()
        }
    }
    // "Formas 3D": a grade das 10 formas prontas (motor: scene3d/Shape3D.hpp).
    var shapes by rememberSaveable { mutableStateOf(false) }
    if (shapes) {
        ObjectGrid(
            listOf(ObjectCell(stringResource(R.string.shape3d_back), "add.model3d.back", glyph = CupertinoGlyph.ChevronLeft) { shapes = false }) +
                (0 until com.aurea.aurea.state.Shape3DCatalog.COUNT).map { k ->
                    ObjectCell(stringResource(com.aurea.aurea.state.Shape3DCatalog.names[k]), "shape3d.add.$k", draw = { drawShape3DIcon(k) }) {
                        store.addShape3D(k)
                        close()
                    }
                },
            footer = stringResource(R.string.shape3d_hint),
        )
        return
    }
    ObjectGrid(
        listOf(
            ObjectCell(stringResource(R.string.shape3d_title), "add.model3d.shapes", draw = { drawShape3DIcon(0) }) { shapes = true },
            ObjectCell(stringResource(R.string.sh_add_model_3d), "add.model3d.file", glyph = CupertinoGlyph.Cube, tint = AureaColors.Accent) {
                // Keep the broad fallback for providers that assign FBX/OBJ a
                // generic or vendor-specific MIME type. The shared importer validates it.
                picker.launch(arrayOf("model/gltf-binary", "model/gltf+json", "model/obj", "model/mtl",
                    "application/x-fbx", "application/zip", "text/plain", "image/*", "application/octet-stream", "*/*"))
            },
            ObjectCell(stringResource(R.string.sh_add_text_3d), "add.model3d.text", glyph = ShellGlyph.TextformatAlt, tint = ShellColors.Text3D) { store.addText3D(); close() },
            ObjectCell(stringResource(R.string.scene_workspace), "add.model3d.scene", glyph = CupertinoGlyph.Cube) { close(); store.enterSceneEditor() },
            ObjectCell(stringResource(R.string.panel_camera_3d), "add.model3d.camera", glyph = CupertinoGlyph.CameraFill) { store.addCamera(); close() },
            ObjectCell(stringResource(R.string.real_camera_title), "add.model3d.realCamera", glyph = CupertinoGlyph.CameraViewfinder) { store.startRealCamera(); close() },
            ObjectCell(stringResource(R.string.sh_add_null_3d), "add.model3d.null", draw = { drawNullIcon(AureaColors.Text) }) { store.addNull(true); close() },
        ),
        footer = stringResource(R.string.sh_add_model_3d_hint),
        warning = stringResource(R.string.model3d_risk_warning),
    )
}

// =============================================================================
// Vetor (P0-7): as 7 fichas na grade de objetos
// =============================================================================

/**
 * Vetor: caminhos de pontos editáveis. "Desenhar com pontos" é o preset 0
 * (entra no modo de pontos); as formas 1..4 do `default_group` já nascem como
 * caminho; SVG é lido e convertido no motor. Todos abrem o painel Vetor.
 */
@Composable
private fun VectorGrid(store: EditorStore, ui: EditorUi) {
    val psdPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) { ui.adding = false; store.importPsd(uri) }
    }
    val svgPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) {
            ui.adding = false
            store.importSvg(uri)
        }
    }
    fun start(preset: Int) {
        if (store.addVectorLayer(preset) >= 0) openPanel(store, ui, EditorPanel.Vector)
    }
    ObjectGrid(
        listOf(
            ObjectCell(stringResource(R.string.editor_desenhar_pontos), "add.vector.points", draw = { drawVectorIcon(0) }) { start(0) },
            ObjectCell(stringResource(R.string.editor_retangulo), "add.vector.rect", draw = { drawVectorIcon(1) }) { start(1) },
            ObjectCell(stringResource(R.string.editor_elipse), "add.vector.ellipse", draw = { drawVectorIcon(2) }) { start(2) },
            ObjectCell(stringResource(R.string.editor_poligono), "add.vector.polygon", draw = { drawVectorIcon(3) }) { start(3) },
            ObjectCell(stringResource(R.string.editor_estrela), "add.vector.star", draw = { drawVectorIcon(4) }) { start(4) },
            ObjectCell(stringResource(R.string.editor_importar_svg), "add.vector.svg", glyph = CupertinoGlyph.DocText) {
                svgPicker.launch(arrayOf("image/svg+xml", "text/xml", "application/xml", "application/octet-stream"))
            },
            ObjectCell(stringResource(R.string.psd_import), "add.vector.psd", glyph = CupertinoGlyph.PhotoOnRectangle) { psdPicker.launch(arrayOf("*/*")) },
        ),
        footer = stringResource(R.string.editor_vetor_contorno_pontos_voce_arrasta_curva),
    )
}

/** Contorno com os pontos à mostra: diz "isto se edita ponto a ponto". */
private fun DrawScope.drawVectorIcon(kind: Int) {
    val s = size.minDimension
    val o = Offset((size.width - s) / 2, (size.height - s) / 2)
    fun p(x: Float, y: Float) = Offset(o.x + x * s, o.y + y * s)
    val line = AureaColors.Text
    val stroke = Stroke(s * 0.06f)
    val pts = mutableListOf<Offset>()
    fun ring(n: Int, rOuter: Float, rInner: Float?): Path = Path().apply {
        val m = if (rInner != null) n * 2 else n
        for (k in 0 until m) {
            val r = if (rInner != null && k % 2 == 1) rInner else rOuter
            val a = -Math.PI / 2 + k * 2 * Math.PI / m
            val q = p(0.5f + r * cos(a).toFloat(), 0.52f + r * sin(a).toFloat())
            pts += q
            if (k == 0) moveTo(q.x, q.y) else lineTo(q.x, q.y)
        }
        close()
    }
    when (kind) {
        0 -> {
            val a = p(0.08f, 0.8f)
            val b = p(0.92f, 0.3f)
            val path = Path().apply {
                moveTo(a.x, a.y)
                cubicTo(p(0.3f, 0.05f).x, p(0.3f, 0.05f).y, p(0.62f, 1.0f).x, p(0.62f, 1.0f).y, b.x, b.y)
            }
            drawPath(path, line, style = stroke)
            // Alça do ponto final: o traço fino até a bolinha.
            drawLine(AureaColors.Accent, b, p(0.98f, 0.06f), s * 0.03f)
            drawCircle(AureaColors.Accent, s * 0.06f, p(0.98f, 0.06f))
            pts += a; pts += b
        }
        1 -> {
            drawRect(line, p(0.1f, 0.18f), Size(s * 0.8f, s * 0.64f), style = stroke)
            pts += listOf(p(0.1f, 0.18f), p(0.9f, 0.18f), p(0.9f, 0.82f), p(0.1f, 0.82f))
        }
        2 -> {
            drawOval(line, p(0.06f, 0.18f), Size(s * 0.88f, s * 0.64f), style = stroke)
            pts += listOf(p(0.5f, 0.18f), p(0.94f, 0.5f), p(0.5f, 0.82f), p(0.06f, 0.5f))
        }
        3 -> drawPath(ring(6, 0.44f, null), line, style = stroke)
        else -> drawPath(ring(5, 0.46f, 0.2f), line, style = stroke)
    }
    val h = s * 0.13f
    for (q in pts) {
        drawRect(Color.White, Offset(q.x - h / 2, q.y - h / 2), Size(h, h))
        drawRect(AureaColors.Accent, Offset(q.x - h / 2, q.y - h / 2), Size(h, h), style = Stroke(s * 0.03f))
    }
}

// =============================================================================
// Presets como faixa de modelos (P0-6)
// =============================================================================

/**
 * Faixa de 2 fileiras: [Ver tudo] (o seletor completo de hoje, em tela
 * cheia), [Descobrir mais] (os presets da comunidade, com o selo NOVO até o
 * primeiro toque) e os presets de texto, os recentes primeiro. Cartão de
 * 75 dp com a prévia animada e a faixa de título. Tocar cria o texto já com o
 * preset e fecha.
 */
@Composable
private fun TextPresetStrip(store: EditorStore, ui: EditorUi, close: () -> Unit) {
    val context = LocalContext.current
    val lib = store.presets
    val prefs = remember { omegaAddPrefs(context) }
    var discoverSeen by remember { mutableStateOf(prefs.getBoolean("presets.discover.seen", false)) }
    var discover by remember { mutableStateOf(false) }
    val entries = remember(lib.user, lib.recents) {
        val all = lib.entries(PresetKind.Text)
        val recent = lib.recents.mapNotNull { key -> all.firstOrNull { it.key == key } }
        recent + all.filter { it !in recent }
    }
    BoxWithConstraints(Modifier.fillMaxSize().testTag("add.presets.strip")) {
        val cell = maxHeight / 2
        LazyHorizontalGrid(rows = GridCells.Fixed(2), modifier = Modifier.fillMaxSize()) {
            item {
                StripActionCell(cell, stringResource(R.string.om_add_view_all), CupertinoGlyph.SquareGrid2x2, "add.presets.viewAll") {
                    ui.presetsBrowser = true
                }
            }
            item {
                StripActionCell(cell, stringResource(R.string.om_add_discover_presets), CupertinoGlyph.Sparkles, "add.presets.discover",
                    badge = if (discoverSeen) null else stringResource(R.string.om_badge_new)) {
                    if (!discoverSeen) { discoverSeen = true; prefs.edit().putBoolean("presets.discover.seen", true).apply() }
                    discover = true
                }
            }
            itemsIndexed(entries, key = { _, e -> e.key }) { _, e ->
                PresetStripCard(cell, e) {
                    if (store.addTextWithPreset(e, false) >= 0) close()
                }
            }
        }
    }
    if (discover) {
        Dialog(onDismissRequest = { discover = false }, properties = DialogProperties(usePlatformDefaultWidth = false)) {
            Column(Modifier.fillMaxSize().background(AureaColors.LightBackground).statusBarsPadding().navigationBarsPadding()) {
                BrowserTopBar(stringResource(R.string.om_add_discover_presets), CupertinoGlyph.Xmark, stringResource(R.string.editor_fechar),
                    "add.presets.discover.close", { discover = false })
                Box(Modifier.weight(1f).fillMaxWidth()) { com.aurea.aurea.home.CommunityPresetsTab() }
            }
        }
    }
}

@Composable
private fun PresetStripCard(cell: Dp, e: PresetEntry, onClick: () -> Unit) {
    val label = stringResource(R.string.tp_add_named, e.name)
    Box(Modifier.size(cell), contentAlignment = Alignment.Center) {
        Box(
            Modifier
                .size(75.dp)
                .clip(RoundedCornerShape(2.5.dp))
                .background(AureaColors.AddPanelDeep)
                .semantics { contentDescription = label }
                .testTag("add.presets.item.${e.key}")
                .tocavel(onClick = onClick),
        ) {
            PresetMotionThumb(e.textPreset, Modifier.fillMaxSize().padding(bottom = 16.dp))
            TitleStrip(e.name, Modifier.align(Alignment.BottomCenter))
        }
    }
}

/** "Aa" animado pelo tipo do preset (a mesma ideia da prévia do seletor). */
@Composable
private fun PresetMotionThumb(preset: Int?, modifier: Modifier) {
    val transition = rememberInfiniteTransition(label = "preset strip")
    val phase by transition.animateFloat(0f, 1f, infiniteRepeatable(tween(2400, easing = LinearEasing)), label = "phase")
    Box(modifier, contentAlignment = Alignment.Center) {
        Row {
            "Aa".forEachIndexed { index, glyph ->
                val progress = ((phase * 2f).coerceAtMost(2f) - index * .12f).coerceIn(0f, 1f)
                val opacity = when (preset) {
                    20, 21, 23, 24 -> (progress * 3f).coerceIn(0f, 1f) * ((1f - phase) * 5f).coerceIn(0f, 1f)
                    22 -> .3f + .7f * kotlin.math.abs(kotlin.math.sin(phase * 60f + index * 4f))
                    null -> 1f
                    else -> (progress * 2f).coerceIn(0.35f, 1f)
                }
                val size = if (preset == 19) 1f - .8f * kotlin.math.sin(progress * Math.PI).toFloat() else 1f
                val lift = if (preset == 2 || preset == 9) (1f - progress) * 8f else 0f
                Text(glyph.toString(), Modifier.graphicsLayer { alpha = opacity; scaleX = size; scaleY = size; translationY = lift },
                    style = AureaType.Base.merge(TextStyle(fontSize = 22.sp, fontWeight = FontWeight.W700, color = AureaColors.Text)))
            }
        }
    }
}

/** "Ver tudo" dos presets: o seletor completo de hoje (Texto | Texto 3D), em tela cheia. */
@Composable
internal fun TextPresetsBrowser(store: EditorStore, onAdded: () -> Unit, onDismiss: () -> Unit) {
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Column(Modifier.fillMaxSize().background(AureaColors.AddPanel).statusBarsPadding().navigationBarsPadding().testTag("presets.browser")) {
            BrowserTopBar(stringResource(R.string.tp_title), CupertinoGlyph.Xmark, stringResource(R.string.editor_fechar), "presets.browser.close", onDismiss)
            Box(Modifier.weight(1f).fillMaxWidth().padding(top = 6.dp)) {
                com.aurea.aurea.editor.panels.TextPresetPicker(store) { onAdded(); onDismiss() }
            }
        }
    }
}

// =============================================================================
// Interface de Edição: a janela de antes (a Edição não tem a barra de adicionar)
// =============================================================================

/**
 * O painel da categoria escolhida numa janela no meio da tela. Só a interface
 * de Edição usa (as figurinhas e os modelos de texto do Dock dela); a
 * Composição tem o painel embutido acima da barra.
 */
@Composable
internal fun AddLayerOverlay(store: EditorStore, ui: EditorUi) {
    val close = { ui.adding = false }
    Dialog(onDismissRequest = close, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        BoxWithConstraints(Modifier.fillMaxSize().padding(24.dp), contentAlignment = Alignment.Center) {
            Box(Modifier.widthIn(max = 380.dp).fillMaxWidth().heightIn(max = minOf(maxHeight, 300.dp)).height(EditorLayout.ADD_CONTENT.dp + 40.dp)
                .clip(RoundedCornerShape(20.dp)).border(1.dp, AureaColors.Action, RoundedCornerShape(20.dp))) {
                AddLayerPanel(store, ui, Modifier.fillMaxSize())
            }
        }
    }
}

// =============================================================================
// 0.3.3 barra: a nova barra de adicionar (desenho do dono)
// =============================================================================

/** As 5 categorias da pílula, na ordem. Ids `addBar.<AddTab>` (iguais no iOS). */
internal val ADD_BAR_PILL = listOf(AddTab.Shape, AddTab.Media, AddTab.Audio, AddTab.Text, AddTab.Element)

/** A grade 4 × 2 do "+", na ordem de leitura. Ids `addBar.grid.<AddTab>`. */
internal val ADD_BAR_GRID = listOf(
    AddTab.Shape, AddTab.Media, AddTab.Audio, AddTab.Text,
    AddTab.TextPresets, AddTab.Element, AddTab.Model3D, AddTab.Draw,
)

/**
 * A vaga da pílula que acende para o conteúdo aberto. Os destinos sem vaga
 * própria acendem a vizinha de assunto (Presets no Texto; 3D e Vetor no
 * Elemento), que mostra o ícone e o nome do que está aberto.
 */
internal fun addBarSlot(tab: AddTab): AddTab = when (tab) {
    AddTab.TextPresets -> AddTab.Text
    AddTab.Model3D, AddTab.Vector -> AddTab.Element
    AddTab.Draw -> AddTab.Shape
    else -> tab
}

/** Estado só da barra (não é do projeto): a grade do "+" aberta. */
internal object AddBarState {
    var more by mutableStateOf(false)
}

/** Abre um destino pela pílula ou pela grade (a grade fecha antes). */
internal fun openFromAddBar(store: EditorStore, ui: EditorUi, tab: AddTab) {
    AddBarState.more = false
    openAdd(store, ui, tab)
}

/** Toque numa vaga da pílula: a acesa fecha o conteúdo; as outras abrem o seu destino. */
internal fun tapAddBarSlot(store: EditorStore, ui: EditorUi, tab: AddTab, on: Boolean) {
    if (on) {
        AddBarState.more = false
        ui.adding = false
        return
    }
    openFromAddBar(store, ui, tab)
}

/** O "+" abre a grade (pausa e fecha o conteúdo aberto); o "X" fecha. */
internal fun toggleAddBarMore(store: EditorStore, ui: EditorUi) {
    if (AddBarState.more) {
        AddBarState.more = false
        return
    }
    if (store.playing) store.pause()
    ui.adding = false
    AddBarState.more = true
}

/** Medidas da barra (dp). A barra continua com [EditorLayout.ADD_BAR] de altura. */
private object AddBarDims {
    // 0.4.1 (mockup "interface 03"): pílula de 50 com cantos de 26, "+" redondo de 50,
    // fio de 1 a 12 %, cartão da grade com cantos de 28 e ladrilhos de 14.
    val SideMargin = 10.dp
    val VerticalMargin = 4.dp
    val BlockPadding = 6.dp
    val BlockRadius = 26.dp
    val Gap = 8.dp
    /** O "+" e o "X" redondos (alvo de 50). */
    val Round = 50.dp
    val Icon = 19.dp
    val Line = 1.dp
    val CardRadius = 28.dp
    val TileHeight = 46.dp
    val TileRadius = 14.dp
    /** Peso da vaga acesa na pílula (as outras pesam 1; ficam com 44+ dp a partir de 360 dp de tela). */
    const val ACTIVE_WEIGHT = 2f
}

/** Cores da barra: só papéis da paleta AUREA. */
private object AddBarColors {
    /** O bloco flutuante: o tom mais fundo do tema. */
    val Block get() = AureaColors.Background
    /** A pílula, os botões redondos e o cartão da grade (vidro escuro do mockup). */
    val Pill get() = AureaColors.Surface
    /** O fio fino em volta da pílula, dos botões e do cartão (branco a 12 %). */
    val Raised get() = Color.White.copy(alpha = 0.12f)
    /** Os ladrilhos da grade (branco a 5 % sobre o cartão). */
    val Tile get() = Color.White.copy(alpha = 0.05f).compositeOver(AureaColors.Surface)
    /** A pílula interna da categoria aberta: o brilho do destaque (22 %). */
    val Active get() = AureaColors.Accent.copy(alpha = 0.22f).compositeOver(AureaColors.Surface)
}

private val AddBarPillShape = RoundedCornerShape(percent = 50)

/**
 * Altura nominal do bloco da grade do "+" (dp, fonte 1×): margem 4 + bloco 7 +
 * cartão 8 + 2 fileiras de (ladrilho 46 + 4 + nome 15) + 6 entre elas + 8 + 7 + 4.
 * Cabe na zona mínima da divisa ([EditorLayout.ZONE_MIN]). O iOS tem o par
 * `EditorLayout.addBarGridHeight`.
 */
internal const val ADD_BAR_GRID_HEIGHT = 4f + 7f + 8f + 2f * (46f + 4f + 15f) + 6f + 8f + 7f + 4f

/**
 * Traço dos ícones desenhados da barra, na caixa de 24 (o mesmo no iOS): 0.3.4, o
 * traço fino do estilo dos ícones ([AureaIconStyle.Line]), o mesmo peso dos glifos
 * da fonte ao lado.
 */
private const val ADD_BAR_STROKE = AureaIconStyle.Line

/**
 * Ícones da barra e da grade: os do AUREA (fonte CupertinoIcons) e, onde a
 * fonte não tem o desenho pedido, três desenhados aqui (Forma: quadrado com
 * nós; Elemento: flor; Desenho: imagem com lápis), com a mesma geometria no iOS.
 */
@Composable
internal fun AddBarIcon(tab: AddTab, size: Dp, tint: Color, modifier: Modifier = Modifier) {
    when (tab) {
        AddTab.Shape -> Canvas(modifier.size(size)) { drawAddBarShape(tint) }
        AddTab.Element -> Canvas(modifier.size(size)) { drawAddBarFlower(tint) }
        AddTab.Draw -> Canvas(modifier.size(size)) { drawAddBarDrawing(tint) }
        AddTab.Media -> CupertinoIcon(CupertinoGlyph.Photo, size, tint, modifier)
        else -> CupertinoIcon(tab.glyph, size, tint, modifier)
    }
}

private fun DrawScope.addBarUnit(): Pair<Float, Offset> {
    val u = size.minDimension / 24f
    return u to Offset((size.width - 24f * u) / 2f, (size.height - 24f * u) / 2f)
}

/** Forma: quadrado com um nó em cada canto. */
private fun DrawScope.drawAddBarShape(c: Color) {
    val (u, o) = addBarUnit()
    fun p(x: Float, y: Float) = Offset(o.x + x * u, o.y + y * u)
    val w = ADD_BAR_STROKE * u
    val lo = 5f
    val hi = 19f
    val r = 2.3f
    for ((x, y) in listOf(lo to lo, hi to lo, hi to hi, lo to hi)) drawCircle(c, r * u, p(x, y), style = Stroke(w))
    drawLine(c, p(lo + r, lo), p(hi - r, lo), w, StrokeCap.Round)
    drawLine(c, p(hi, lo + r), p(hi, hi - r), w, StrokeCap.Round)
    drawLine(c, p(lo + r, hi), p(hi - r, hi), w, StrokeCap.Round)
    drawLine(c, p(lo, lo + r), p(lo, hi - r), w, StrokeCap.Round)
}

/** Elemento: flor de 8 pétalas (só o contorno de fora de cada uma) e o miolo. */
private fun DrawScope.drawAddBarFlower(c: Color) {
    val (u, o) = addBarUnit()
    val w = ADD_BAR_STROKE * u
    val cx = o.x + 12f * u
    val cy = o.y + 12f * u
    val ring = 5.4f * u
    val petal = 3.1f * u
    for (k in 0 until 8) {
        val a = Math.toRadians(k * 45.0)
        val px = cx + ring * cos(a).toFloat()
        val py = cy + ring * sin(a).toFloat()
        // ±64,3° = até onde a pétala encosta nas vizinhas.
        drawArc(
            c, startAngle = k * 45f - 64.3f, sweepAngle = 128.6f, useCenter = false,
            topLeft = Offset(px - petal, py - petal), size = Size(petal * 2f, petal * 2f),
            style = Stroke(w, cap = StrokeCap.Round),
        )
    }
    drawCircle(c, 2.1f * u, Offset(cx, cy), style = Stroke(w))
}

/** Desenho: moldura de imagem (aberta no canto) com um lápis na diagonal. */
private fun DrawScope.drawAddBarDrawing(c: Color) {
    val (u, o) = addBarUnit()
    fun Path.m(x: Float, y: Float) = moveTo(o.x + x * u, o.y + y * u)
    fun Path.l(x: Float, y: Float) = lineTo(o.x + x * u, o.y + y * u)
    fun Path.q(x1: Float, y1: Float, x2: Float, y2: Float) = quadraticTo(o.x + x1 * u, o.y + y1 * u, o.x + x2 * u, o.y + y2 * u)
    val stroke = Stroke(ADD_BAR_STROKE * u, cap = StrokeCap.Round, join = StrokeJoin.Round)
    drawPath(Path().apply {
        m(18f, 10f); l(18f, 6f); q(18f, 3f, 15f, 3f); l(6f, 3f); q(3f, 3f, 3f, 6f)
        l(3f, 15f); q(3f, 18f, 6f, 18f); l(10f, 18f)
    }, c, style = stroke)
    drawPath(Path().apply { m(3.5f, 15f); l(8f, 10.5f); l(11.5f, 14f) }, c, style = stroke)
    drawCircle(c, 1.5f * u, Offset(o.x + 13f * u, o.y + 7.5f * u), style = Stroke(ADD_BAR_STROKE * u))
    drawPath(Path().apply {
        m(11.2f, 20.8f); l(14.45f, 20.09f); l(21.87f, 12.67f); l(19.33f, 10.13f); l(11.91f, 17.55f); close()
    }, c, style = stroke)
}

/**
 * A barra de adicionar, na base do editor, sem camada escolhida (e com o
 * adicionar aberto): um bloco flutuante com a pílula das 5 categorias e o "+"
 * redondo à direita. Sem nada aberto, cada categoria tem ícone e nome; com uma
 * aberta, a pílula mostra só os ícones e a aberta vira a pílula interna acesa
 * (ícone e nome lado a lado), que desliza e cresce até ela. Tocar na acesa
 * fecha. O "+" abre a grade de todas as categorias ([AddBarMore]).
 */
@Composable
internal fun AddBar(store: EditorStore, ui: EditorUi, modifier: Modifier = Modifier) {
    val reduce = com.aurea.aurea.effects.rememberReduceMotion()
    // A barra saiu (camada escolhida, outra interface): a grade do "+" fecha junto.
    DisposableEffect(Unit) { onDispose { AddBarState.more = false } }
    val moreLabel = stringResource(R.string.bar33_more)
    Box(
        modifier
            .background(AureaColors.EditorBar)
            .testTag("editor.addBar")
            .padding(horizontal = AddBarDims.SideMargin, vertical = AddBarDims.VerticalMargin),
    ) {
        Row(
            Modifier
                .fillMaxSize()
                .clip(RoundedCornerShape(AddBarDims.BlockRadius))
                .background(AddBarColors.Block)
                .padding(AddBarDims.BlockPadding),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            AddBarPill(store, ui, reduce, Modifier.weight(1f).fillMaxHeight())
            Spacer(Modifier.width(AddBarDims.Gap))
            AddBarRoundButton(moreLabel, "addBar.more", rotation = 0f) { toggleAddBarMore(store, ui) }
        }
    }
}

/** Como uma vaga da pílula se mostra. */
private enum class AddBarLook { Stacked, IconOnly, Inline }

@Composable
private fun AddBarPill(store: EditorStore, ui: EditorUi, reduce: Boolean, modifier: Modifier) {
    val open = if (ui.adding) ui.addTab else null
    val slot = open?.let { addBarSlot(it) }?.takeIf { it in ADD_BAR_PILL }
    val spec: AnimationSpec<Float> = if (reduce) snap() else spring(dampingRatio = 0.84f, stiffness = 600f)
    val weights = ADD_BAR_PILL.map { tab ->
        animateFloatAsState(if (slot == tab) AddBarDims.ACTIVE_WEIGHT else 1f, spec, label = "addBar.weight").value
    }
    val glow by animateFloatAsState(if (slot != null) 1f else 0f, spec, label = "addBar.glow")
    // Ao fechar, a pílula acesa some no lugar da última aberta.
    val last = remember { arrayOf(AddTab.Shape) }
    if (slot != null) last[0] = slot
    val glowTab = slot ?: last[0]
    BoxWithConstraints(
        modifier
            .clip(AddBarPillShape)
            .background(AddBarColors.Pill)
            .border(AddBarDims.Line, AddBarColors.Raised, AddBarPillShape)
            .testTag("addBar.pill"),
    ) {
        val total = weights.sum()
        val index = ADD_BAR_PILL.indexOf(glowTab)
        val start = maxWidth * (weights.take(index).sum() / total)
        val width = maxWidth * (weights[index] / total)
        if (glow > 0.001f) {
            Box(
                Modifier
                    .offset(x = start)
                    .width(width)
                    .fillMaxHeight()
                    .padding(4.dp)
                    .graphicsLayer { alpha = glow }
                    .clip(AddBarPillShape)
                    .background(AddBarColors.Active),
            )
        }
        Row(Modifier.fillMaxSize()) {
            ADD_BAR_PILL.forEachIndexed { k, tab ->
                val on = slot == tab
                val look = when {
                    slot == null -> AddBarLook.Stacked
                    on -> AddBarLook.Inline
                    else -> AddBarLook.IconOnly
                }
                AddBarPillItem(store, ui, tab, if (on && open != null) open else tab, look, on, reduce, Modifier.weight(weights[k]).fillMaxHeight())
            }
        }
    }
}

@Composable
private fun AddBarPillItem(
    store: EditorStore,
    ui: EditorUi,
    tab: AddTab,
    shown: AddTab,
    look: AddBarLook,
    on: Boolean,
    reduce: Boolean,
    modifier: Modifier,
) {
    val label = stringResource(shown.label)
    val fade = if (reduce) 0 else 160
    Box(
        modifier
            .semantics { contentDescription = label; selected = on }
            .testTag("addBar.${tab.name}")
            .tocavel(shrink = 1f, haptic = true) { tapAddBarSlot(store, ui, tab, on) },
        contentAlignment = Alignment.Center,
    ) {
        AnimatedContent(
            targetState = look to shown,
            transitionSpec = { (fadeIn(tween(fade)) togetherWith fadeOut(tween(fade))).using(SizeTransform(clip = false)) },
            contentAlignment = Alignment.Center,
            label = "addBar.item",
        ) { (mode, item) ->
            val text = stringResource(item.label)
            when (mode) {
                AddBarLook.Stacked -> Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    AddBarIcon(item, AddBarDims.Icon, AureaColors.Muted)
                    Spacer(Modifier.height(2.dp))
                    AddBarLabel(text, 10, color = AureaColors.Muted)
                }
                AddBarLook.IconOnly -> AddBarIcon(item, AddBarDims.Icon, AureaColors.Muted)
                AddBarLook.Inline -> Row(Modifier.padding(horizontal = 10.dp), verticalAlignment = Alignment.CenterVertically) {
                    AddBarIcon(item, AddBarDims.Icon, AureaColors.Text)
                    Spacer(Modifier.width(6.dp))
                    AddBarLabel(text, 13, Modifier.weight(1f, fill = false))
                }
            }
        }
    }
}

@Composable
private fun AddBarLabel(text: String, sizeSp: Int, modifier: Modifier = Modifier, color: Color = AureaColors.Text) {
    Text(
        text,
        modifier = modifier,
        maxLines = 1,
        overflow = TextOverflow.Ellipsis,
        style = AureaType.Base.merge(TextStyle(fontSize = sizeSp.sp, lineHeight = (sizeSp + 3).sp, fontWeight = FontWeight.W500, color = color)),
    )
}

/** O "+" redondo (e o "X", que é o mesmo "+" girado 45°). */
@Composable
private fun AddBarRoundButton(label: String, tag: String, rotation: Float, onClick: () -> Unit) {
    Box(
        Modifier
            .size(AddBarDims.Round)
            .clip(CircleShape)
            .background(AddBarColors.Pill)
            .border(AddBarDims.Line, AddBarColors.Raised, CircleShape)
            .semantics { contentDescription = label }
            .testTag(tag)
            .tocavel(haptic = true, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        CupertinoIcon(CupertinoGlyph.Plus, 20.dp, AureaColors.Text, Modifier.graphicsLayer { rotationZ = rotation })
    }
}

/**
 * A grade do "+": o bloco cresce para cima, por cima da timeline e da própria
 * barra, com o cartão 4 × 2 das categorias e o "X" redondo no lugar do "+".
 * [modifier] dá a área que o véu transparente cobre (tocar fora do cartão
 * fecha, e nada passa para a timeline). Abre e fecha em ~200 ms com mola
 * suave; com "remover animações" troca na hora. O Voltar fecha.
 */
@Composable
internal fun AddBarMore(store: EditorStore, ui: EditorUi, modifier: Modifier = Modifier) {
    val reduce = com.aurea.aurea.effects.rememberReduceMotion()
    val open = AddBarState.more
    BackHandler(enabled = open) { AddBarState.more = false }
    val origin = if (LocalLayoutDirection.current == LayoutDirection.Rtl) TransformOrigin(0f, 1f) else TransformOrigin(1f, 1f)
    Box(modifier) {
        if (open) {
            // 0.4.1 (mockup "interface 03"): o véu escurece (preto a 45 %).
            Box(Modifier.matchParentSize().background(Color.Black.copy(alpha = 0.45f)).pointerInput(Unit) { detectTapGestures { AddBarState.more = false } })
        }
        AnimatedVisibility(
            visible = open,
            modifier = Modifier.align(Alignment.BottomCenter).fillMaxWidth(),
            enter = if (reduce) EnterTransition.None else
                fadeIn(tween(120)) + scaleIn(spring(dampingRatio = 0.86f, stiffness = 700f), initialScale = 0.86f, transformOrigin = origin),
            exit = if (reduce) ExitTransition.None else
                fadeOut(tween(140)) + scaleOut(tween(160), targetScale = 0.9f, transformOrigin = origin),
            label = "addBar.more",
        ) {
            val rotation by transition.animateFloat(
                transitionSpec = { if (reduce) snap() else spring(dampingRatio = 0.8f, stiffness = 600f) },
                label = "addBar.x",
            ) { if (it == EnterExitState.Visible) 45f else 0f }
            AddBarGridBlock(store, ui, rotation)
        }
    }
}

@Composable
private fun AddBarGridBlock(store: EditorStore, ui: EditorUi, rotation: Float) {
    val closeLabel = stringResource(R.string.bar33_close)
    val gridLabel = stringResource(R.string.bar33_grid)
    val cardShape = RoundedCornerShape(AddBarDims.CardRadius)
    Box(
        Modifier
            .fillMaxWidth()
            .padding(horizontal = AddBarDims.SideMargin, vertical = AddBarDims.VerticalMargin)
            .clip(RoundedCornerShape(AddBarDims.BlockRadius))
            .background(AddBarColors.Block)
            // O fundo do bloco segura o toque: não fecha nem passa para a timeline.
            .pointerInput(Unit) { detectTapGestures { } }
            .padding(AddBarDims.BlockPadding),
    ) {
        Row(verticalAlignment = Alignment.Bottom) {
            Column(
                Modifier
                    .weight(1f)
                    .clip(cardShape)
                    .background(AddBarColors.Pill)
                    .border(AddBarDims.Line, AddBarColors.Raised, cardShape)
                    .semantics { contentDescription = gridLabel }
                    .testTag("addBar.grid")
                    .padding(8.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                ADD_BAR_GRID.chunked(4).forEach { row ->
                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        row.forEach { tab -> AddBarGridTile(store, ui, tab, Modifier.weight(1f)) }
                    }
                }
            }
            Spacer(Modifier.width(AddBarDims.Gap))
            AddBarRoundButton(closeLabel, "addBar.close", rotation) { AddBarState.more = false }
        }
    }
}

@Composable
private fun AddBarGridTile(store: EditorStore, ui: EditorUi, tab: AddTab, modifier: Modifier) {
    val label = stringResource(tab.label)
    Column(
        modifier
            .semantics { contentDescription = label }
            .testTag("addBar.grid.${tab.name}")
            .tocavel(haptic = true) { openFromAddBar(store, ui, tab) },
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(
            Modifier
                .fillMaxWidth()
                .height(AddBarDims.TileHeight)
                .clip(RoundedCornerShape(AddBarDims.TileRadius))
                .background(AddBarColors.Tile),
            contentAlignment = Alignment.Center,
        ) {
            AddBarIcon(tab, 20.dp, AureaColors.Text)
        }
        Spacer(Modifier.height(4.dp))
        AddBarLabel(label, 10, color = AureaColors.Muted)
    }
}
