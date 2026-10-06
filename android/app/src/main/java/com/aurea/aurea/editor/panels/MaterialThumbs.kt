package com.aurea.aurea.editor.panels

import android.util.LruCache
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

// =============================================================================
//  MINIATURAS DE MATERIAL das listas do painel 3D (materiais prontos do texto
//  3D, materiais do modelo importado, partes da forma 3D): a bola de estúdio
//  que o MOTOR calcula (Engine::material_preview, scene3d/MaterialPreview.hpp)
//  com os fatores e a imagem do mapa de cor — a mesma bola no iOS
//  (Panel3DView.swift, MaterialThumbStore).
//
//  Nada no main thread: a bola é pedida numa fila de um só no Default. O motor
//  guarda as prontas por receita (pedir de novo depois de uma edição só
//  recalcula o material que mudou); aqui fica a ÚLTIMA de cada item, para a
//  lista reabrir já com as bolas e não piscar enquanto a nova chega.
// =============================================================================
internal object MaterialThumbs : com.aurea.aurea.engine.TrimmableImageCache {
    /** Lado da bola em px (nítida em 30 dp até 3,2×). */
    const val SIZE = 96

    init { com.aurea.aurea.engine.UiImageCaches.register(this) }

    private val memory = object : LruCache<String, ImageBitmap>(4 * 1024 * 1024) {
        override fun sizeOf(key: String, value: ImageBitmap) = value.width * value.height * 4
    }

    @OptIn(kotlinx.coroutines.ExperimentalCoroutinesApi::class)
    private val worker = Dispatchers.Default.limitedParallelism(1)

    fun peek(key: String): ImageBitmap? = memory.get(key)

    suspend fun load(key: String, render: () -> ImageBitmap?): ImageBitmap? {
        val bmp = withContext(worker) { runCatching { render() }.getOrNull() } ?: return null
        memory.put(key, bmp)
        return bmp
    }

    override fun releaseImages() = memory.evictAll()
}

/**
 * A bola do material [material] da camada 3D [layer]. [revision] = o que muda
 * quando o material muda (revisão do modelo, valores): a bola é pedida de novo
 * e a anterior fica na tela até a nova chegar. Nulo = ainda não há bola.
 */
@Composable
internal fun rememberMaterialThumb(store: EditorStore, layer: Long, material: Int, revision: Any?): ImageBitmap? {
    val key = "m:$layer:$material"
    var shown by remember(key) { mutableStateOf(MaterialThumbs.peek(key)) }
    LaunchedEffect(key, revision) {
        MaterialThumbs.load(key) { store.renderMaterialThumb(layer, material, MaterialThumbs.SIZE) }?.let { shown = it }
    }
    return shown
}

/** A bola de um material pronto do texto 3D (não depende do projeto). */
@Composable
internal fun rememberPresetThumb(store: EditorStore, preset: Int): ImageBitmap? {
    val key = "p:$preset"
    var shown by remember(key) { mutableStateOf(MaterialThumbs.peek(key)) }
    LaunchedEffect(key) {
        if (shown == null) MaterialThumbs.load(key) { store.renderText3DPresetThumb(preset, MaterialThumbs.SIZE) }?.let { shown = it }
    }
    return shown
}

/**
 * A bola desenhada: a miniatura do motor ou, enquanto ela não chega, a
 * bolinha da cor do material. Decorativa para a acessibilidade — o nome ao
 * lado já diz o que é.
 */
@Composable
internal fun MaterialBall(thumb: ImageBitmap?, fallback: Color, size: Dp, modifier: Modifier = Modifier) {
    if (thumb != null) {
        Image(thumb, contentDescription = null, modifier = modifier.size(size))
    } else {
        Box(modifier.size(size).padding(size * 0.08f).clip(CircleShape).background(fallback))
    }
}

/** Ficha de material: bola + nome; acesa = destaque. */
@Composable
internal fun MaterialChip(label: String, on: Boolean, thumb: ImageBitmap?, fallback: Color, modifier: Modifier = Modifier, onClick: () -> Unit) {
    Row(
        modifier.height(40.dp).clip(RoundedCornerShape(9.dp))
            .background(if (on) AureaColors.AccentDim else AureaColors.Chip)
            .semantics { selected = on }
            .tocavel(shrink = 1f, onClick = onClick).padding(start = 5.dp, end = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        MaterialBall(thumb, fallback, 30.dp)
        Spacer(Modifier.width(7.dp))
        Text(label, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 12.5.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
    }
}
