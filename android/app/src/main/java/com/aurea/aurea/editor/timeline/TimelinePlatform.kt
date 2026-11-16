package com.aurea.aurea.editor.timeline

/**
 * Diferenças de entrada por plataforma na timeline. No celular e no tablet
 * (toque) tudo fica falso: arrastar o clipe rola/varre, e mover pede segurar.
 * O Aurea PC liga [mouseDragMovesClips]: com o MOUSE, arrastar o corpo do
 * clipe já o move, como nos editores de computador (o toque não muda).
 */
object TimelinePlatform {
    @Volatile var mouseDragMovesClips: Boolean = false

    /**
     * Tempo ancorado no INÍCIO, como no After Effects (Aurea PC): o 0 fica à
     * esquerda, a vista rola por conta própria e o cabeçote anda pela régua.
     * Falso (celular e tablet): o cabeçote fixo no meio e o conteúdo corre.
     */
    @Volatile var anchorAtStart: Boolean = false
}
