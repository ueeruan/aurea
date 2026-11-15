package com.aurea.aurea.editor

import com.aurea.aurea.editor.panels.EditorPanel

/**
 * Onde a plataforma abre certos painéis. Tudo nulo (celular e tablet): os
 * painéis de sempre. O Aurea PC liga [curveHost]: o editor de curva abre na
 * janelinha de Curvas dele, e o painel de curva antigo não aparece.
 */
object PanelRouting {
    @Volatile var curveHost: (() -> Unit)? = null

    /**
     * Quem abre os outros painéis. Nulo (celular e tablet): `openPanel` só
     * marca o painel aberto, como sempre. Devolve `true` quando a plataforma
     * mostrou o painel noutro lugar (o painel aberto não muda); `false` segue o
     * caminho normal. O Aurea PC liga: Efeitos ficam no cartão da direita
     * (Controles de efeitos) e o resto abre na janela flutuante dele.
     */
    @Volatile var panelHost: ((EditorPanel) -> Boolean)? = null
}
