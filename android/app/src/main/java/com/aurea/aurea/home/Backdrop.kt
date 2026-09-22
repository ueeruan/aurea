package com.aurea.aurea.home

import android.os.Build
import androidx.compose.foundation.background
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.BlurEffect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.TileMode
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.graphics.layer.GraphicsLayer
import androidx.compose.ui.graphics.layer.drawLayer
import androidx.compose.ui.graphics.rememberGraphicsLayer
import androidx.compose.ui.layout.onPlaced
import androidx.compose.ui.layout.positionInRoot
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.toIntSize

/**
 * Vidro da Home (barra de abas, barra compacta, barra de lote): o app antigo
 * usava `BackdropFilter` — o que passa POR BAIXO da barra aparece borrado.
 *
 * Compose não tem backdrop; o jeito sem biblioteca é o conteúdo se gravar
 * numa [GraphicsLayer] (a fonte) e a barra desenhar ESSA camada, recortada
 * no próprio retângulo e com blur de RenderEffect (API 31+). A camada é
 * desenhada por referência (RenderNode): rolar a lista regrava a fonte e a
 * barra acompanha sem recompor nem regravar nada.
 *
 * Abaixo da API 31 não há RenderEffect: cor sólida mais opaca, como pede a
 * spec (§1.5). A fonte nem grava camada nesse caso.
 *
 * REGRA: a barra nunca pode estar DENTRO da própria fonte (o RenderNode
 * desenharia a si mesmo). Fonte e barra são irmãs.
 */
@Stable
internal class Backdrop(val layer: GraphicsLayer) {
    /** Onde a fonte está na raiz — a barra desloca a camada por isso. */
    var origin by mutableStateOf(Offset.Zero)
}

internal val BlurSupported = Build.VERSION.SDK_INT >= Build.VERSION_CODES.S

@Composable
internal fun rememberBackdrop(): Backdrop {
    val layer = rememberGraphicsLayer()
    return remember(layer) { Backdrop(layer) }
}

/** Marca o conteúdo que as barras de vidro vão borrar. */
internal fun Modifier.backdropSource(backdrop: Backdrop): Modifier {
    if (!BlurSupported) return this
    return this
        .onPlaced { backdrop.origin = it.positionInRoot() }
        .drawWithContent {
            backdrop.layer.record { this@drawWithContent.drawContent() }
            drawLayer(backdrop.layer)
        }
}

/**
 * Fundo de vidro: o que está atrás, borrado com o `sigma` do Flutter, e a
 * tinta por cima. [fallback] é a cor sólida sem blur (API < 31).
 *
 * O [fallback] também é desenhado ANTES do borrão no caminho com blur: se o
 * `RenderEffect` não render nada (SwiftShader, driver sem o efeito), a camada
 * borrada fica vazia e sobraria só a tinta de 72% — o texto de trás passaria
 * legível por baixo da barra. Com o piso, ou o borrão cobre o piso inteiro,
 * ou a barra fica sólida. Nunca "meio transparente sem blur".
 */
@Composable
internal fun Modifier.glass(backdrop: Backdrop, sigma: Dp, tint: Color, fallback: Color): Modifier {
    if (!BlurSupported) return background(fallback)
    val self = remember { mutableStateOf(Offset.Zero) }
    // O Flutter recebe sigma; o RenderEffect recebe raio (o Skia converte
    // raio → sigma por 0,57735·r + 0,5). Sem a conversão o blur sai ~1,7× mais forte.
    val radius = with(LocalDensity.current) { ((sigma.toPx() - 0.5f) / 0.57735f).coerceAtLeast(0f) }
    return this
        .onPlaced { self.value = it.positionInRoot() }
        .drawWithCache {
            val blurred = obtainGraphicsLayer().apply {
                renderEffect = BlurEffect(radius, radius, TileMode.Clamp)
                clip = true
            }
            onDrawBehind {
                drawRect(fallback)
                val offset = self.value - backdrop.origin
                blurred.record(size.toIntSize()) {
                    translate(-offset.x, -offset.y) { drawLayer(backdrop.layer) }
                }
                drawLayer(blurred)
                drawRect(tint)
            }
        }
}
