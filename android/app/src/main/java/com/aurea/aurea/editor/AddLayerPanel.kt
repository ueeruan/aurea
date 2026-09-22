package com.aurea.aurea.editor

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.cos
import kotlin.math.sin

/** As abas do menu de adicionar (modelo AM da A.01). */
internal enum class AddTab(val label: String, val glyph: Char) {
    Shape("Forma", ShellGlyph.SquareOnCircle),
    Media("Mídia", CupertinoGlyph.PhotoOnRectangle),
    Audio("Áudio", CupertinoGlyph.MusicNote2),
    Object("Objeto / Elemento", ShellGlyph.CircleGridHex),
    More("Mais", CupertinoGlyph.SquareGrid2x2),
}

/**
 * O adicionar da A.01 é um painel EMBUTIDO na zona do painel contextual
 * (0,48 do espaço, pode cobrir a timeline): abas em cima e um trilho à
 * direita com os modos de criar (desenho livre, vetorial, texto).
 *
 * Mídia, formas, texto, desenho vetorial (modo de pontos), desenho à mão
 * livre e SVG existem no motor; o resto diz "em breve" em vez de fingir.
 */
@Composable
internal fun AddLayerPanel(store: EditorStore, ui: EditorUi) {
    val close = { ui.adding = false }
    Row(Modifier.fillMaxSize().background(AureaColors.EditorPanel)) {
        Column(Modifier.weight(1f).fillMaxHeight()) {
            AddTabs(ui)
            Box(Modifier.weight(1f).fillMaxWidth()) {
                when (ui.addTab) {
                    AddTab.Shape -> ShapesTab(store, Modifier.padding(horizontal = 6.dp, vertical = 2.dp), close)
                    AddTab.Media -> MediaTab(store, close)
                    AddTab.Audio -> AudioTab(store, close)
                    AddTab.Object -> ObjectsTab(store, close)
                    AddTab.More -> MoreTab(store, ui)
                }
            }
        }
        Column(Modifier.width(52.dp).fillMaxHeight()) {
            SideShortcut(ShellGlyph.Scribble, "Desenho à\nmão livre") { startFreehand(store, ui) }
            SideShortcut(CupertinoGlyph.PencilOutline, "Desenho\nvetorial") { startVector(store, ui) }
            SideShortcut(CupertinoGlyph.Textformat, "Texto") {
                if (store.addText() >= 0) openPanel(store, ui, com.aurea.aurea.editor.panels.EditorPanel.Text)
            }
            ChromeButton(CupertinoGlyph.Xmark, "Fechar adicionar", onClick = close, size = 20.dp, width = 52.dp, height = 44.dp)
        }
    }
}

/** "Desenho vetorial": camada vetorial vazia, modo de pontos e o painel do vetor. */
private fun startVector(store: EditorStore, ui: EditorUi) {
    if (store.addVectorLayer(0) >= 0) openPanel(store, ui, com.aurea.aurea.editor.panels.EditorPanel.Vector)
}

/** "Desenho à mão livre": o palco passa a receber traços (cada traço vira um caminho suave). */
private fun startFreehand(store: EditorStore, ui: EditorUi) {
    if (store.playing) store.pause()
    ui.adding = false
    store.chooseVectorTool(2)
}

/** Ícone em cima, rótulo embaixo (a aba se reconhece de relance). */
@Composable
private fun AddTabs(ui: EditorUi) {
    Row(Modifier.fillMaxWidth().height(54.dp)) {
        AddTab.entries.forEach { tab ->
            // Aba ativa em destaque: `acao` (#245D8C) no fundo escuro sumia (bug 27).
            val color = if (ui.addTab == tab) AureaColors.Accent else AureaColors.Text
            Column(
                Modifier
                    .weight(1f)
                    .fillMaxHeight()
                    .tocavel(shrink = 1f) { ui.addTab = tab },
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
            ) {
                CupertinoIcon(tab.glyph, 17.dp, color)
                Spacer(Modifier.height(2.dp))
                Text(
                    tab.label,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    style = AureaType.Base.merge(TextStyle(fontSize = 9.5.sp, fontWeight = FontWeight.W600, color = color)),
                )
            }
        }
    }
}

@Composable
private fun ColumnScope.SideShortcut(glyph: Char, label: String, onClick: () -> Unit) {
    Column(
        Modifier
            .weight(1f)
            .fillMaxWidth()
            .tocavel(shrink = 1f, onClick = onClick),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        CupertinoIcon(glyph, 18.dp, AureaColors.Text)
        Spacer(Modifier.height(2.dp))
        Text(
            label,
            maxLines = 3,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
            style = AureaType.Base.merge(TextStyle(fontSize = 9.sp)),
        )
    }
}

// =============================================================================
// Forma: grade 5 × 3 de silhuetas + pontos de página
// =============================================================================

@Composable
private fun ShapesTab(store: EditorStore, modifier: Modifier, close: () -> Unit) {
    Column(modifier.fillMaxSize()) {
        Column(Modifier.weight(1f).fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            for (r in 0 until 3) {
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    for (c in 0 until 5) {
                        val index = r * 5 + c
                        ShapeTile(index) {
                            store.addShape(index)
                            close()
                        }
                    }
                }
            }
        }
        Spacer(Modifier.height(6.dp))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.Center) {
            for (i in 0 until 4) {
                Box(
                    Modifier
                        .padding(horizontal = 4.dp)
                        .size(6.dp)
                        .background(if (i == 0) Color.White else ShellColors.PageDotOff, CircleShape),
                )
            }
        }
        Spacer(Modifier.height(4.dp))
    }
}

@Composable
private fun RowScope.ShapeTile(index: Int, onClick: () -> Unit) {
    Canvas(
        Modifier
            .weight(1f)
            .aspectRatio(1f)
            .background(ShellColors.ShapeTile)
            .tocavel(shrink = 1f, onClick = onClick)
            .padding(7.dp),
    ) { drawLibraryShape(index) }
}

/**
 * As 15 silhuetas da 1ª página da biblioteca (01_add_sheet_formas.png), num
 * quadrado de 82 % do ladrilho, cinza #9E9E9E; as geométricas levam os
 * pontinhos brancos nos vértices.
 */
private fun DrawScope.drawLibraryShape(index: Int) {
    val s = size.minDimension * 0.82f
    val o = Offset((size.width - s) / 2, (size.height - s) / 2)
    fun p(x: Float, y: Float) = Offset(o.x + x * s, o.y + y * s)
    val fill = ShellColors.ShapeFill
    val dotR = s * 0.045f
    fun dot(x: Float, y: Float) = drawCircle(Color.White, dotR, p(x, y))
    fun poly(vararg pts: Float): Path = Path().apply {
        moveTo(p(pts[0], pts[1]).x, p(pts[0], pts[1]).y)
        var i = 2
        while (i < pts.size) {
            lineTo(p(pts[i], pts[i + 1]).x, p(pts[i], pts[i + 1]).y)
            i += 2
        }
        close()
    }
    fun regular(n: Int, r: Float, rot: Double): Path = Path().apply {
        for (k in 0 until n) {
            val a = rot + k * 2 * Math.PI / n
            val q = p(0.5f + r * cos(a).toFloat(), 0.5f + r * sin(a).toFloat())
            if (k == 0) moveTo(q.x, q.y) else lineTo(q.x, q.y)
        }
        close()
    }
    val top = -Math.PI / 2
    when (index) {
        0 -> drawCircle(fill, s / 2, p(0.5f, 0.5f))
        1 -> drawRoundRect(fill, p(0f, 0f), Size(s, s), androidx.compose.ui.geometry.CornerRadius(s * 0.12f))
        2 -> {
            drawRect(fill, p(0.36f, 0f), Size(s * 0.28f, s))
            drawRect(fill, p(0f, 0.36f), Size(s, s * 0.28f))
        }
        3 -> drawCircle(fill, s * 0.41f, p(0.5f, 0.5f), style = Stroke(s * 0.17f))
        4 -> {
            drawPath(poly(0.5f, 0.05f, 0.95f, 0.9f, 0.05f, 0.9f), fill)
            dot(0.5f, 0.05f); dot(0.5f, 0.9f)
        }
        5 -> drawArc(fill, 0f, 270f, true, p(0f, 0f), Size(s, s))
        6 -> drawPath(regular(6, 0.5f, top), fill)
        7 -> for (k in 0 until 6) {
            val a = (top + k * Math.PI / 3)
            val c = p(0.5f + 0.25f * cos(a).toFloat(), 0.5f + 0.25f * sin(a).toFloat())
            rotateEllipse(c, s * 0.11f, s * 0.27f, Math.toDegrees(a).toFloat() + 90f, fill)
        }
        8 -> {
            drawLine(fill, p(0.05f, 0.5f), p(0.72f, 0.5f), s * 0.07f, StrokeCap.Round)
            drawPath(poly(0.66f, 0.36f, 0.97f, 0.5f, 0.66f, 0.64f), fill)
        }
        9 -> {
            drawPath(regular(6, 0.5f, top), fill)
            dot(0.5f, 0f); dot(0.5f, 1f)
        }
        10 -> drawRect(fill, p(0f, 0f), Size(s, s))
        11 -> {
            val star = Path()
            for (k in 0 until 10) {
                val r = if (k % 2 == 0) 0.5f else 0.21f
                val a = top + k * Math.PI / 5
                val q = p(0.5f + r * cos(a).toFloat(), 0.55f + r * sin(a).toFloat())
                if (k == 0) star.moveTo(q.x, q.y) else star.lineTo(q.x, q.y)
            }
            star.close()
            drawPath(star, fill)
        }
        12 -> {
            drawLine(fill, p(0.1f, 0.5f), p(0.9f, 0.5f), s * 0.24f, StrokeCap.Round)
            dot(0.1f, 0.5f); dot(0.9f, 0.5f)
        }
        13 -> drawRoundRect(fill, p(0.06f, 0.06f), Size(s * 0.88f, s * 0.88f), androidx.compose.ui.geometry.CornerRadius(s * 0.08f))
        else -> {
            drawPath(poly(0.05f, 0.05f, 0.95f, 0.95f, 0.05f, 0.95f), fill)
            dot(0.5f, 0.5f); dot(0.05f, 0.95f)
        }
    }
}

private fun DrawScope.rotateEllipse(center: Offset, rx: Float, ry: Float, degrees: Float, color: Color) {
    val r = Rect(center.x - rx, center.y - ry, center.x + rx, center.y + ry)
    drawContext.transform.rotate(degrees, center)
    drawOval(color, r.topLeft, r.size)
    drawContext.transform.rotate(-degrees, center)
}

// =============================================================================
// Mídia e áudio
// =============================================================================

/**
 * Mídia pelo seletor do sistema (Photo Picker): vídeo vai para
 * `importVideo`, imagem para `importImage`, pelo tipo MIME.
 */
@Composable
private fun MediaTab(store: EditorStore, close: () -> Unit) {
    val context = LocalContext.current
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri: Uri? ->
        if (uri != null) {
            val mime = context.contentResolver.getType(uri).orEmpty()
            if (mime.startsWith("video/")) store.importVideo(uri) else store.importImage(uri)
            close()
        }
    }
    Row(Modifier.fillMaxWidth().padding(6.dp), horizontalArrangement = Arrangement.spacedBy(6.dp), verticalAlignment = Alignment.Top) {
        AddOption(CupertinoGlyph.PhotoOnRectangle, "Galeria") {
            picker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageAndVideo))
        }
        AddOption(CupertinoGlyph.Photo, "Fotos do sistema") {
            picker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly))
        }
        AddOption(CupertinoGlyph.Videocam, "Vídeos do sistema") {
            picker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.VideoOnly))
        }
    }
}

/**
 * Áudio: arquivo de som (m4a, mp3, wav, aac, ogg, flac…) ou o som de um vídeo
 * da galeria — os dois viram camada de áudio pelo mesmo `importAudio`.
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
    Row(Modifier.fillMaxWidth().padding(6.dp), horizontalArrangement = Arrangement.spacedBy(6.dp), verticalAlignment = Alignment.Top) {
        AddOption(CupertinoGlyph.MusicNote, "Arquivo de áudio") { files.launch(arrayOf("audio/*")) }
        AddOption(CupertinoGlyph.Film, "Extrair de vídeo") {
            videos.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.VideoOnly))
        }
    }
}

/**
 * Bloco 60 × 60 (#212D3A, raio 18, ícone 28 em destaque) com rótulo 12 em até
 * duas linhas. Largura FIXA de 84: com peso, três blocos + espaçador davam
 * 47 dp por bloco de 60 — os quadrados se tocavam e o rótulo cortava.
 */
@Composable
private fun AddOption(glyph: Char, label: String, onClick: () -> Unit) {
    Column(
        Modifier
            .width(84.dp)
            .semantics { contentDescription = label }
            .tocavel(onClick = onClick),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(
            Modifier.size(60.dp).clip(RoundedCornerShape(18.dp)).background(AureaColors.Chip),
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(glyph, 28.dp, AureaColors.Accent)
        }
        Spacer(Modifier.height(6.dp))
        Text(
            label,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 14.sp)),
        )
    }
}

// =============================================================================
// Objeto / Elemento: cartões 3 colunas
// =============================================================================

private enum class ObjectCard(val label: String) {
    Scene3D("Cena 3D"), EmptyGroup("Grupo Vazio"), Null("Nulo"), Camera3D("Câmera 3D"),
    Element("Elemento / Projeto"), Particles("Partículas"), Text3D("Texto 3D"), Phone3D("iPhone 3D"),
}

@Composable
private fun ObjectsTab(store: EditorStore, close: () -> Unit) {
    // Cena 3D: glTF/GLB do aparelho. O tipo MIME de modelo 3D varia por
    // gerenciador de arquivos; o filtro real é a extensão, no store.
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) {
            store.importModel(uri)
            close()
        }
    }
    BoxWithConstraints(Modifier.fillMaxSize().padding(horizontal = 6.dp, vertical = 2.dp)) {
        val rows = 3
        val cardH = ((maxHeight.value - 8f - 8f * (rows - 1)) / rows).coerceIn(52f, 118f)
        Column(
            Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 8.dp, vertical = 4.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            ObjectCard.entries.chunked(3).forEach { row ->
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    for (k in 0 until 3) {
                        val card = row.getOrNull(k)
                        if (card == null) Spacer(Modifier.weight(1f))
                        else ObjectCardTile(card, cardH) {
                            when (card) {
                                ObjectCard.Scene3D -> picker.launch(arrayOf("model/gltf-binary", "model/gltf+json", "model/obj", "application/octet-stream", "*/*"))
                                ObjectCard.Null -> { store.addNull(false); close() }
                                ObjectCard.Particles -> { store.addParticles(0); close() }
                                ObjectCard.Text3D -> { store.addText3D(); close() }
                                else -> store.comingSoon(card.label)
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun RowScope.ObjectCardTile(card: ObjectCard, height: Float, onClick: () -> Unit) {
    Box(
        Modifier
            .weight(1f)
            .height(height.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(ShellColors.ObjectCard)
            .tocavel(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        if (card == ObjectCard.Scene3D) {
            Text(
                "PROVAR",
                style = AureaType.Base.merge(TextStyle(fontSize = 8.5.sp, fontWeight = FontWeight.W900, color = Color.Black)),
                modifier = Modifier
                    .align(Alignment.TopCenter)
                    .padding(top = 6.dp)
                    .clip(RoundedCornerShape(4.dp))
                    .background(AureaColors.Accent)
                    .padding(horizontal = 4.dp, vertical = 1.dp),
            )
        }
        Column(Modifier.padding(4.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            if (card == ObjectCard.Scene3D) Spacer(Modifier.height(10.dp))
            val iconSize = if (height < 80f) 26.dp else 36.dp
            when (card) {
                ObjectCard.Scene3D -> CupertinoIcon(CupertinoGlyph.Videocam, iconSize, Color.White)
                ObjectCard.Camera3D -> CupertinoIcon(CupertinoGlyph.VideocamFill, iconSize, ShellColors.Camera3D)
                ObjectCard.Particles -> CupertinoIcon(CupertinoGlyph.Sparkles, iconSize, Color.White)
                ObjectCard.Text3D -> CupertinoIcon(ShellGlyph.TextformatAlt, iconSize, ShellColors.Text3D)
                ObjectCard.Phone3D -> CupertinoIcon(CupertinoGlyph.DevicePhonePortrait, iconSize, ShellColors.Phone3D)
                else -> Canvas(Modifier.size(iconSize)) { drawObjectIcon(card) }
            }
            Spacer(Modifier.height(6.dp))
            Text(
                card.label,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                textAlign = TextAlign.Center,
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = Color.White)),
            )
        }
    }
}

/** Os três ícones desenhados da A.01: grupo tracejado, nulo e elemento/projeto. */
private fun DrawScope.drawObjectIcon(card: ObjectCard) {
    val k = size.minDimension / 36f
    val stroke = Stroke(1.8f * k)
    when (card) {
        ObjectCard.EmptyGroup -> {
            val r = Rect(center.x - 17 * k, center.y - 14 * k, center.x + 17 * k, center.y + 14 * k)
            drawRect(
                Color.White, r.topLeft, r.size,
                style = Stroke(1.6f * k, pathEffect = PathEffect.dashPathEffect(floatArrayOf(3.5f * k, 2.5f * k))),
            )
            val h = 4f * k
            for (q in listOf(r.topLeft, r.topCenter, r.topRight, r.centerRight, r.bottomRight, r.bottomCenter, r.bottomLeft, r.centerLeft)) {
                drawRect(Color.White, Offset(q.x - h / 2, q.y - h / 2), Size(h, h))
            }
        }
        ObjectCard.Null -> {
            val r = Rect(center.x - 16 * k, center.y - 16 * k, center.x + 16 * k, center.y + 16 * k)
            drawRoundRect(Color.White, r.topLeft, r.size, androidx.compose.ui.geometry.CornerRadius(5 * k), style = stroke)
            drawLine(Color.White, Offset(r.left + 3 * k, r.bottom - 3 * k), Offset(r.right - 3 * k, r.top + 3 * k), 2f * k)
        }
        else -> {
            val tri = Path().apply {
                moveTo(center.x - 6 * k, center.y - 12 * k)
                lineTo(center.x - 16 * k, center.y + 8 * k)
                lineTo(center.x + 4 * k, center.y + 8 * k)
                close()
            }
            drawPath(tri, Color.White, style = stroke)
            drawCircle(Color.White, 9 * k, Offset(center.x + 6 * k, center.y + 2 * k), style = stroke)
        }
    }
}

// =============================================================================
// Mais
// =============================================================================

@Composable
private fun MoreTab(store: EditorStore, ui: EditorUi) {
    // SVG pelo seletor de documentos do sistema (o arquivo é lido e convertido no motor).
    val svgPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) {
            ui.adding = false
            store.importSvg(uri)
        }
    }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(6.dp)) {
        MoreItem(ShellGlyph.Scribble, "Desenho livre") { startFreehand(store, ui) }
        MoreItem(CupertinoGlyph.PencilOutline, "Desenho vetorial") { startVector(store, ui) }
        MoreItem(CupertinoGlyph.DocText, "Importar SVG") { svgPicker.launch(arrayOf("image/svg+xml")) }
        MoreItem(CupertinoGlyph.CaptionsBubble, "Legendas") { store.comingSoon("Legendas") }
        MoreItem(CupertinoGlyph.WandStars, "Camada de ajuste") { store.comingSoon("Camada de ajuste") }
        MoreItem(CupertinoGlyph.Folder, "Agrupar camadas") { store.comingSoon("Agrupar camadas") }
        MoreItem(CupertinoGlyph.Bookmark, "Marca no cabeçote") { store.toggleMarker() }
        MoreItem(ShellGlyph.Metronome, "Detectar batidas") { store.detectBeats() }
        MoreItem(CupertinoGlyph.QuestionCircle, "Como editar") { store.comingSoon("Guia rápido") }
    }
}

@Composable
private fun MoreItem(glyph: Char, label: String, onClick: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .heightIn(min = 48.dp)
            .tocavel(shrink = 1f, onClick = onClick)
            .padding(horizontal = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CupertinoIcon(glyph, 21.dp, AureaColors.Text)
        Spacer(Modifier.width(32.dp))
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp)))
    }
}
