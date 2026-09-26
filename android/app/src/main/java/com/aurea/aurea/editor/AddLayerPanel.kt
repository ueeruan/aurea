package com.aurea.aurea.editor

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.annotation.StringRes
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.cos
import kotlin.math.sin

/**
 * Categorias do adicionar (7.2): ícone grande + nome curto, cada recurso num
 * lugar só. O antigo "Mais" e o trilho da direita (mão livre, vetorial, texto)
 * foram distribuídos aqui: SVG em Vetor, legendas em Texto, ajuste/agrupar em
 * Elemento, marca e batidas em Áudio. `Shape` continua o 1º (padrão do `openAdd`).
 */
internal enum class AddTab(@StringRes val label: Int, val glyph: Char) {
    Shape(R.string.sh_add_tab_shape, ShellGlyph.SquareOnCircle),
    Media(R.string.sh_add_tab_media, CupertinoGlyph.PhotoOnRectangle),
    Audio(R.string.sh_add_tab_audio, CupertinoGlyph.MusicNote2),
    Text(R.string.sh_add_tab_text, CupertinoGlyph.Textformat),
    Element(R.string.sh_add_tab_element, ShellGlyph.CircleGridHex),
    Model3D(R.string.sh_add_tab_3d, CupertinoGlyph.Cube),
    Draw(R.string.sh_add_tab_draw, ShellGlyph.Scribble),
    Vector(R.string.sh_add_tab_vector, CupertinoGlyph.PencilOutline),
}

/**
 * O adicionar é um painel EMBUTIDO na zona do painel contextual (ADD_BODY no
 * EditorLayout): fileira de categorias em cima (✕ fixo à esquerda, o resto rola
 * de lado) e o conteúdo da categoria logo abaixo, em fichas grandes.
 * Só recurso que existe no motor; nada de "em breve".
 */
@Composable
internal fun AddLayerPanel(store: EditorStore, ui: EditorUi) {
    val close = { ui.adding = false }
    Column(Modifier.fillMaxSize().background(AureaColors.EditorPanel)) {
        AddCategories(ui, close)
        Box(Modifier.weight(1f).fillMaxWidth()) {
            when (ui.addTab) {
                AddTab.Shape -> ShapesTab(store, close)
                AddTab.Media -> MediaTab(store, ui, close)
                AddTab.Audio -> AudioTab(store, close)
                AddTab.Text -> TextTab(store, ui)
                AddTab.Element -> ElementTab(store, close)
                AddTab.Model3D -> Model3DTab(store, close)
                AddTab.Draw -> DrawTab(store, ui)
                AddTab.Vector -> VectorTab(store, ui)
            }
        }
    }
}

// =============================================================================
// Categorias
// =============================================================================

/**
 * ✕ fixo + 8 categorias de 64 dp que rolam de lado (em 360 dp cabem ~5 e a
 * seguinte aparece cortada: dá para ver que há mais). A escolhida ganha fundo
 * e cor de destaque, e é trazida para a vista ao abrir.
 */
@Composable
private fun AddCategories(ui: EditorUi, close: () -> Unit) {
    val scroll = rememberScrollState()
    val itemPx = with(LocalDensity.current) { 64.dp.toPx() }
    LaunchedEffect(Unit) {
        val i = ui.addTab.ordinal
        if (i > 3) scroll.scrollTo(((i - 3) * itemPx).toInt())
    }
    Row(Modifier.fillMaxWidth().height(68.dp), verticalAlignment = Alignment.CenterVertically) {
        ChromeButton(CupertinoGlyph.Xmark, stringResource(R.string.editor_fechar_adicionar), onClick = close, size = 20.dp, width = 44.dp, height = 68.dp)
        Row(
            Modifier.weight(1f).fillMaxHeight().horizontalScroll(scroll).padding(end = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            AddTab.entries.forEach { tab ->
                val on = ui.addTab == tab
                val tabLabel = stringResource(tab.label)
                val color = if (on) AureaColors.Accent else AureaColors.Text
                Column(
                    Modifier
                        .width(64.dp)
                        .fillMaxHeight()
                        .padding(horizontal = 2.dp, vertical = 5.dp)
                        .clip(RoundedCornerShape(12.dp))
                        .background(if (on) AureaColors.Chip else Color.Transparent)
                        .semantics { contentDescription = tabLabel }
                        .tocavel(shrink = 1f) { ui.addTab = tab },
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.Center,
                ) {
                    CupertinoIcon(tab.glyph, 24.dp, color)
                    Spacer(Modifier.height(4.dp))
                    Text(
                        tabLabel,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, fontWeight = FontWeight.W600, color = color)),
                    )
                }
            }
        }
    }
    Box(Modifier.fillMaxWidth().height(1.dp).background(AureaColors.Hairline))
}

// =============================================================================
// Fichas (todas as categorias menos Forma)
// =============================================================================

/** Uma ficha: ícone da fonte OU desenho próprio, nome curto, ação real. */
private class AddItem(
    val label: String,
    val glyph: Char? = null,
    val tint: Color = ShellColors.DockTileContent,
    val draw: (DrawScope.() -> Unit)? = null,
    val onClick: () -> Unit,
)

/**
 * Grade de fichas grandes no estilo da doca da camada (ref15): 3 por fileira,
 * 80 dp de altura, ícone 28 e nome em até 2 linhas. Rola se passar de 2 fileiras.
 */
@Composable
private fun CardGrid(items: List<AddItem>, hint: String? = null) {
    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 12.dp, vertical = 10.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        items.chunked(3).forEach { row ->
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                for (k in 0 until 3) {
                    val item = row.getOrNull(k)
                    if (item == null) Spacer(Modifier.weight(1f)) else AddCard(item)
                }
            }
        }
        if (hint != null) {
            Text(
                hint,
                modifier = Modifier.padding(horizontal = 4.dp, vertical = 2.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted)),
            )
        }
    }
}

@Composable
private fun RowScope.AddCard(item: AddItem) {
    Column(
        Modifier
            .weight(1f)
            .height(80.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(ShellColors.DockTile)
            .semantics { contentDescription = item.label }
            .tocavel(onClick = item.onClick)
            .padding(horizontal = 4.dp, vertical = 8.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        when {
            item.draw != null -> Canvas(Modifier.size(30.dp)) { item.draw.invoke(this) }
            item.glyph != null -> CupertinoIcon(item.glyph, 28.dp, item.tint)
        }
        Spacer(Modifier.height(6.dp))
        Text(
            item.label,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
            style = AureaType.Base.merge(
                TextStyle(fontSize = 12.sp, lineHeight = 14.sp, fontWeight = FontWeight.W500, color = ShellColors.DockTileContent),
            ),
        )
    }
}

// =============================================================================
// Forma: grade visual das formas do motor (Engine::add_shape)
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
)

/** Tocar põe a forma no centro da cena, já escolhida (e fecha o adicionar). */
@Composable
private fun ShapesTab(store: EditorStore, close: () -> Unit) {
    BoxWithConstraints(Modifier.fillMaxSize()) {
        // Ladrilho de ~64 dp: 5 colunas num telefone, mais num tablet.
        val cols = ((maxWidth.value - 24f) / 68f).toInt().coerceIn(5, 9)
        Column(
            Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 12.dp, vertical = 10.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            SHAPES.chunked(cols).forEach { row ->
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    for (k in 0 until cols) {
                        val shape = row.getOrNull(k)
                        if (shape == null) {
                            Spacer(Modifier.weight(1f))
                        } else {
                            ShapeTile(shape.first, stringResource(shape.second)) {
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

@Composable
private fun RowScope.ShapeTile(preset: Int, label: String, onClick: () -> Unit) {
    Column(
        Modifier
            .weight(1f)
            .semantics { contentDescription = label }
            .tocavel(onClick = onClick),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Canvas(
            Modifier
                .fillMaxWidth()
                .aspectRatio(1f)
                .clip(RoundedCornerShape(12.dp))
                .background(ShellColors.DockTile)
                .padding(12.dp),
        ) { drawShapePreset(preset) }
        Spacer(Modifier.height(4.dp))
        Text(
            label,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = AureaColors.Muted)),
        )
    }
}

/** Silhueta de cada preset do motor (mesmo tipo e proporção que a camada nasce). */
private fun DrawScope.drawShapePreset(preset: Int) {
    val s = size.minDimension
    val o = Offset((size.width - s) / 2, (size.height - s) / 2)
    fun p(x: Float, y: Float) = Offset(o.x + x * s, o.y + y * s)
    val fill = ShellColors.DockTileContent
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
        else -> {   // 8: seta (camada nasce 1,6 : 1)
            drawLine(fill, p(0.04f, 0.5f), p(0.66f, 0.5f), s * 0.2f, StrokeCap.Butt)
            drawPath(poly(0.6f, 0.2f, 0.98f, 0.5f, 0.6f, 0.8f), fill)
        }
    }
}

// =============================================================================
// Mídia e áudio (fluxos do seletor do sistema, sem mudança)
// =============================================================================

/**
 * Mídia pelo seletor do sistema (Photo Picker): vídeo vai para
 * `importVideo`, imagem para `importImage`, pelo tipo MIME.
 */
@Composable
private fun MediaTab(store: EditorStore, ui: EditorUi, close: () -> Unit) {
    val context = LocalContext.current
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri: Uri? ->
        if (uri != null) {
            val mime = context.contentResolver.getType(uri).orEmpty()
            if (mime.startsWith("video/")) store.importVideo(uri) else store.importImage(uri)
            close()
        }
    }
    CardGrid(
        listOf(
            AddItem(stringResource(R.string.editor_galeria), CupertinoGlyph.PhotoOnRectangle, AureaColors.Accent) {
                picker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageAndVideo))
            },
            AddItem(stringResource(R.string.editor_foto), CupertinoGlyph.Photo) {
                picker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly))
            },
            AddItem(stringResource(R.string.editor_video), CupertinoGlyph.Videocam) {
                picker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.VideoOnly))
            },
            // Aurea AI: o video nao vem do aparelho, vem de um servidor. Entra
            // aqui porque, para quem usa, e mais um jeito de conseguir um video.
            AddItem(stringResource(R.string.sh_add_ai_video), CupertinoGlyph.WandStars) {
                openPanel(store, ui, EditorPanel.AiVideo)
            },
        ),
    )
}

/**
 * Áudio: arquivo de som (m4a, mp3, wav, aac, ogg, flac…) ou o som de um vídeo
 * da galeria — os dois viram camada de áudio pelo mesmo `importAudio`. Marca e
 * batidas moram aqui porque servem para sincronizar com a música.
 */
@Composable
private fun AudioTab(store: EditorStore, close: () -> Unit) {
    val files = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) {
            store.importAudio(uri)
            close()
        }
    }
    val videos = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri: Uri? ->
        if (uri != null) {
            store.importAudio(uri)
            close()
        }
    }
    CardGrid(
        listOf(
            AddItem(stringResource(R.string.editor_musica_ou_som), CupertinoGlyph.MusicNote, AureaColors.Accent) { files.launch(arrayOf("audio/*")) },
            AddItem(stringResource(R.string.sh_add_video_sound), CupertinoGlyph.Film) {
                videos.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.VideoOnly))
            },
            AddItem(stringResource(R.string.sh_add_detect_beats), ShellGlyph.Metronome) {
                close()
                store.detectBeats()
            },
            AddItem(stringResource(R.string.sh_add_marker_at_playhead), CupertinoGlyph.Bookmark) {
                close()
                store.toggleMarker()
            },
        ),
    )
}

// =============================================================================
// Texto, elemento e 3D
// =============================================================================

@Composable
private fun TextTab(store: EditorStore, ui: EditorUi) {
    val needsSpeech = stringResource(R.string.sh_add_captions_need_speech)
    CardGrid(
        listOf(
            AddItem(stringResource(R.string.sh_add_tab_text), CupertinoGlyph.Textformat, AureaColors.Accent) {
                if (store.addText() >= 0) openPanel(store, ui, EditorPanel.Text)
            },
            AddItem(stringResource(R.string.sh_add_speech_captions), CupertinoGlyph.CaptionsBubble) {
                // Legendas saem da fala de um vídeo/áudio: abre o painel dele.
                val kind = store.detail?.kind
                if (kind == LayerType.Video.kind || kind == LayerType.Audio.kind) {
                    openPanel(store, ui, EditorPanel.Captions)
                } else {
                    store.showToast(needsSpeech)
                }
            },
        ),
    )
}

/** Peças que não são mídia nem desenho: nulo, partículas (3 receitas), ajuste e grupo. */
@Composable
private fun ElementTab(store: EditorStore, close: () -> Unit) {
    val pickToGroup = stringResource(R.string.sh_add_pick_layers_to_group)
    CardGrid(
        listOf(
            AddItem(stringResource(R.string.sh_add_null), draw = { drawNullIcon() }) { store.addNull(false); close() },
            // UM sistema, nao tres. Faiscas/Neve/Poeira de luz viraram preset
            // do mesmo motor — listar os tres aqui prometia tres motores.
            AddItem(stringResource(R.string.particular_title), CupertinoGlyph.Sparkles, ShellColors.Text3D) {
                close(); store.addParticles(10)
            },
            AddItem(stringResource(R.string.editor_camada_ajuste), CupertinoGlyph.WandStars) { close(); store.addAdjustmentLayer() },
            AddItem(stringResource(R.string.sh_add_group_selection), CupertinoGlyph.Folder) {
                if (store.selection.isEmpty()) {
                    store.showToast(pickToGroup)
                } else {
                    close()
                    store.precompose()
                }
            },
        ),
    )
}

@Composable
private fun Model3DTab(store: EditorStore, close: () -> Unit) {
    // glTF/GLB/FBX/OBJ do aparelho. O tipo MIME de modelo 3D varia por
    // gerenciador de arquivos; o filtro real é a extensão, no store.
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) {
            store.importModel(uri)
            close()
        }
    }
    CardGrid(
        listOf(
            AddItem(stringResource(R.string.sh_add_model_3d), CupertinoGlyph.Cube, AureaColors.Accent) {
                picker.launch(arrayOf("model/gltf-binary", "model/gltf+json", "model/obj", "application/octet-stream", "*/*"))
            },
            AddItem(stringResource(R.string.sh_add_text_3d), ShellGlyph.TextformatAlt, ShellColors.Text3D) { store.addText3D(); close() },
            AddItem(stringResource(R.string.scene_workspace), CupertinoGlyph.Cube, AureaColors.Accent) { close(); store.enterSceneEditor() },
            AddItem(stringResource(R.string.panel_camera_3d), CupertinoGlyph.CameraFill, AureaColors.Accent) { store.addCamera(); close() },
            AddItem(stringResource(R.string.sh_add_null_3d), draw = { drawNullIcon() }) { store.addNull(true); close() },
        ),
        hint = stringResource(R.string.sh_add_model_3d_hint),
    )
}

/** Quadrado com a diagonal: o ícone do nulo (camada invisível que serve de pai). */
private fun DrawScope.drawNullIcon() {
    val k = size.minDimension / 30f
    val r = Rect(center.x - 13 * k, center.y - 13 * k, center.x + 13 * k, center.y + 13 * k)
    val c = ShellColors.DockTileContent
    drawRoundRect(c, r.topLeft, r.size, CornerRadius(4 * k), style = Stroke(1.8f * k))
    drawLine(c, Offset(r.left + 3 * k, r.bottom - 3 * k), Offset(r.right - 3 * k, r.top + 3 * k), 1.8f * k)
}

// =============================================================================
// Desenho e vetor
// =============================================================================

/** "Mão livre": o palco passa a receber traços (cada traço vira um caminho suave). */
@Composable
private fun DrawTab(store: EditorStore, ui: EditorUi) {
    CardGrid(
        listOf(
            AddItem(stringResource(R.string.editor_mao_livre), ShellGlyph.Scribble, AureaColors.Accent) {
                if (store.playing) store.pause()
                ui.adding = false
                store.chooseVectorTool(2)
            },
        ),
        hint = stringResource(R.string.editor_desenhe_dedo_direto_palco_cada_traco),
    )
}

/**
 * Vetor: caminhos de pontos editáveis. "Desenhar com pontos" é o preset 0
 * (entra no modo de pontos); as formas 1..4 do `default_group` já nascem como
 * caminho; SVG é lido e convertido no motor. Todos abrem o painel Vetor.
 */
@Composable
private fun VectorTab(store: EditorStore, ui: EditorUi) {
    val svgPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) {
            ui.adding = false
            store.importSvg(uri)
        }
    }
    fun start(preset: Int) {
        if (store.addVectorLayer(preset) >= 0) openPanel(store, ui, EditorPanel.Vector)
    }
    CardGrid(
        listOf(
            AddItem(stringResource(R.string.editor_desenhar_pontos), draw = { drawVectorIcon(0) }) { start(0) },
            AddItem(stringResource(R.string.editor_retangulo), draw = { drawVectorIcon(1) }) { start(1) },
            AddItem(stringResource(R.string.editor_elipse), draw = { drawVectorIcon(2) }) { start(2) },
            AddItem(stringResource(R.string.editor_poligono), draw = { drawVectorIcon(3) }) { start(3) },
            AddItem(stringResource(R.string.editor_estrela), draw = { drawVectorIcon(4) }) { start(4) },
            AddItem(stringResource(R.string.editor_importar_svg), CupertinoGlyph.DocText) { svgPicker.launch(arrayOf("image/svg+xml")) },
        ),
        hint = stringResource(R.string.editor_vetor_contorno_pontos_voce_arrasta_curva),
    )
}

/** Contorno com os pontos à mostra: diz "isto se edita ponto a ponto". */
private fun DrawScope.drawVectorIcon(kind: Int) {
    val s = size.minDimension
    val o = Offset((size.width - s) / 2, (size.height - s) / 2)
    fun p(x: Float, y: Float) = Offset(o.x + x * s, o.y + y * s)
    val line = ShellColors.DockTileContent
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
