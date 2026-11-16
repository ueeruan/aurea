package com.aurea.aurea.editor

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.aurea.aurea.editor.panels.Ease
import com.aurea.aurea.editor.timeline.KeyRef

/**
 * A janelinha de CURVAS do Aurea PC: aberta pelo duplo clique num keyframe da
 * timeline ou pelo símbolo de curva dos painéis (o editor de curva antigo não
 * aparece no PC: `PanelRouting.curveHost` desvia para cá). Flutua por cima do
 * editor; a posição fica onde a pessoa arrastou.
 */
object PcCurvePopup {
    var open by mutableStateOf(false)
    /** Canto superior esquerdo (dp); NaN = ainda não arrastada (abre perto do reprodutor). */
    var x by mutableStateOf(Float.NaN)
    var y by mutableStateOf(Float.NaN)

    /**
     * O último clique foi dentro da janelinha: Ctrl+C / Ctrl+V copiam e colam a
     * CURVA (e não camadas ou keyframes). O `Main.kt` zera a cada clique e a
     * janelinha religa no mesmo clique (passagem Initial: pai antes do filho).
     */
    var hot = false

    /** Prévia de um preset (um clique): a curva mostrada até aplicar ou trocar de keyframe. */
    internal var edited by mutableStateOf<Ease?>(null)
    internal var editedFor by mutableStateOf<Pair<Long, KeyRef>?>(null)

    /** O que o gráfico mostra agora (Copiar curva / Ctrl+C); nulo = sem trecho. */
    internal var shown: Ease? = null

    fun close() {
        open = false
        hot = false
        edited = null
    }
}
