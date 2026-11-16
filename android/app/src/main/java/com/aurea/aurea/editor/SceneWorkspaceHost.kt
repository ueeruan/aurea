package com.aurea.aurea.editor

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerInputChange
import com.aurea.aurea.state.EditorStore

/**
 * O ponteiro no palco da Cena 3D, quando a plataforma tem mouse (Aurea PC). São
 * funções de extensão do escopo do gesto: rodam DENTRO do gesto do palco, com o
 * mesmo mapa de coordenadas.
 */
internal interface ScenePointerHost {
    /**
     * Primeiro a ver o ponteiro que desceu. `true` = a plataforma ficou com o
     * gesto inteiro (no PC: botão do meio, Alt+arrasto, botão direito e o clique
     * que confirma mover/girar/escalar pelo teclado).
     */
    suspend fun AwaitPointerEventScope.press(store: EditorStore, m: StageMapper, down: PointerInputChange): Boolean

    /**
     * No lugar do gesto de toque da cena (um dedo orbita, tocar escolhe) quando
     * o ponteiro não pegou alça, trackball nem osso. No PC: clique escolhe,
     * Shift soma e arrastar no vazio abre a caixa de seleção.
     */
    suspend fun AwaitPointerEventScope.gesture(store: EditorStore, m: StageMapper, down: PointerInputChange)
}

/**
 * Onde a plataforma troca a área de trabalho da Cena 3D. Tudo nulo (celular e
 * tablet): a [SceneLayoutWorkspace] e os gestos de toque de sempre. O Aurea PC
 * liga tudo no `desktop/src/main/kotlin/pc/Main.kt` e a cena vira a do
 * `replace/PcScene3D*.kt` (organização e mouse/teclado do Blender, com as peças
 * do Aurea). Pedido do dono em 12/11/2026: só no PC.
 */
internal object SceneWorkspaceHost {
    /** A área de trabalho inteira da Cena 3D. Nulo = [SceneLayoutWorkspace]. */
    @Volatile var workspace: (@Composable (EditorStore, EditorUi, @Composable (Modifier) -> Unit) -> Unit)? = null

    /** Quem fica com o ponteiro no palco da Cena 3D ([ScenePointerHost]). Nulo = os gestos de toque de sempre. */
    @Volatile var pointer: ScenePointerHost? = null

    /**
     * Linhas-guia da cena que o palco NÃO desenha: um bit por tipo do motor
     * (`Engine::query_scene_guides`: 0 quadro da composição, 1 câmera, 2 nulo e
     * luz, 3 chão, 4 eixo X, 5 eixo Z). 0 = todas à vista (o celular nunca muda).
     */
    var hiddenGuides by mutableIntStateOf(0)
}
