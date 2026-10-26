package com.aurea.aurea.editor.panels

import androidx.compose.runtime.Composable

/**
 * COR E PREENCHIMENTO (`EditorPanel.Shape`; forma, texto, vetor e grupo).
 *
 * Omega aparência (área 05, P0-3): o painel virou o [ColorFillPanel] — barra de
 * tipo (Nenhum · Cor · Degradê / Cor / Original), seletor embutido e degradê.
 * O CONTORNO da forma saiu daqui: mora em Borda e sombra › Traço. Os parâmetros
 * da silhueta (tamanho, raio, pontas…) continuam em EDITAR FORMA ([ShapeEditPanel]).
 */
@Composable
internal fun ShapePanel(env: PanelEnv) {
    ColorFillPanel(env)
}
