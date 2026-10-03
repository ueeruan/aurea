package com.aurea.aurea.ui.i18n

import androidx.compose.ui.platform.LocalConfiguration
import android.view.View
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.graphics.drawscope.scale
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.unit.LayoutDirection

/**
 * O QUE NÃO ESPELHA (Fase 8.1).
 *
 * Em árabe o app inteiro corre da direita para a esquerda — menos as superfícies
 * onde o eixo X **é** o dado. O tempo corre para a direita em qualquer idioma:
 * a régua, o playhead, a waveform, o editor de curvas, os keyframes, o canvas
 * do preview, os eixos X/Y/Z e os gizmos 3D continuam LTR, senão o quadro 0
 * apareceria na direita e arrastar para "avançar" andaria para trás.
 *
 * [KeepLtr] força o LTR de LAYOUT (Row/Column/start-end), não só do texto.
 * `Canvas` já desenha em coordenadas absolutas e não é espelhado; o que quebra
 * com RTL é o layout em volta dele — a coluna de nomes de camada indo para a
 * direita enquanto o painter desenha as barras a partir da esquerda.
 */
@Composable
fun KeepLtr(content: @Composable () -> Unit) {
    CompositionLocalProvider(LocalLayoutDirection provides LayoutDirection.Ltr, content = content)
}

/**
 * Um trecho LTR dentro de um texto que corre em RTL.
 *
 * Um timecode, uma coordenada ou um nome de arquivo colados numa frase árabe
 * são reordenados pelo algoritmo bidirecional: "00:02:03" pode virar
 * "03:02:00". O isolamento (U+2066 … U+2069) prende o trecho no sentido dele.
 *
 * Usar SEMPRE que um número com separador ou um identificador entrar no meio de
 * texto que pode ser RTL.
 */
fun ltr(text: String): AnnotatedString = buildAnnotatedString {
    append('⁦')   // LRI — Left-to-Right Isolate
    append(text)
    append('⁩')   // PDI — Pop Directional Isolate
}

/**
 * O mesmo, para quem compõe o texto à mão e só quer embrulhar uma parte.
 */
fun AnnotatedString.Builder.ltrIsolated(text: String) {
    append('⁦')
    append(text)
    append('⁩')
}

/**
 * O mesmo isolamento LTR, para quem precisa de `String` (rótulo de `Text`
 * simples, argumento de `stringResource`): "1920 × 1080" numa linha RTL vira
 * "1080 × 1920" sem ele — o algoritmo bidirecional trata o número como RTL
 * na hora de resolver o " × " entre dois números.
 */
fun ltrPlain(text: String): String = "\u2066$text\u2069"

/**
 * Espelha o DESENHO na horizontal quando o layout está em RTL (árabe).
 *
 * Para ícones que apontam um sentido de leitura — voltar ‹, avançar ›,
 * "abre submenu", desfazer/refazer (Material espelha). NÃO usar em play,
 * transporte, relógio, marca de visto, logotipo nem em nada da timeline: lá o
 * sentido é o do tempo, que não espelha (ver [KeepLtr]). Dentro de [KeepLtr]
 * a direção já é LTR, então o ícone fica como está — é o que se quer.
 *
 * Só o desenho vira: o toque, a medida e a semântica não mudam.
 */
fun Modifier.mirrorInRtl(): Modifier = drawWithContent {
    if (layoutDirection == LayoutDirection.Rtl) {
        scale(scaleX = -1f, scaleY = 1f) { this@drawWithContent.drawContent() }
    } else {
        drawContent()
    }
}

/**
 * O contrário de [KeepLtr]: volta à direção do IDIOMA (RTL em árabe) dentro de
 * uma superfície mantida em LTR. Diálogos e menus abertos de dentro do palco ou
 * da timeline herdariam o LTR do palco — texto árabe alinhado à esquerda e
 * botões na ordem trocada. Envolva o diálogo, não o palco.
 */
@Composable
fun LocaleDirection(content: @Composable () -> Unit) {
    val rtl = LocalConfiguration.current.layoutDirection == View.LAYOUT_DIRECTION_RTL
    CompositionLocalProvider(LocalLayoutDirection provides if (rtl) LayoutDirection.Rtl else LayoutDirection.Ltr, content = content)
}
