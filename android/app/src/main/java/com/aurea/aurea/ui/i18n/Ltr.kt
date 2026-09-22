package com.aurea.aurea.ui.i18n

import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
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
