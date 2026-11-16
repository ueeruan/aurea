package com.aurea.aurea.editor

import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEvent
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.isAltPressed
import androidx.compose.ui.input.key.isCtrlPressed
import androidx.compose.ui.input.key.isShiftPressed
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.type
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.Screen
import com.aurea.aurea.ui.theme.LayerType

/**
 * As teclas da Cena 3D do Aurea PC, no mapa padrão do Blender. Só valem com a
 * Cena 3D aberta E o ponteiro em cima da viewport (ou durante um mover/girar/
 * escalar, que segura o teclado até confirmar). Fora disso devolvem `false` e
 * os atalhos de After Effects/Premiere do editor (PcShortcuts) seguem iguais —
 * com o ponteiro na timeline de baixo, por exemplo, S continua sendo o ímã.
 *
 * O que NÃO é tratado aqui cai no PcShortcuts: Espaço (tocar), setas (quadro),
 * Ctrl+Z / Ctrl+Shift+Z, Ctrl+C/V, Ctrl+S.
 */
object PcScene3DKeys {
    private fun active(store: EditorStore) =
        store.screen == Screen.Editor && store.sceneEditor && store.textEditLayer == null && SceneWorkspaceHost.workspace != null

    /**
     * Antes de todo mundo (o Esc do app é "voltar", e o Tab é do foco): Esc cancela o
     * modal ou fecha o menu aberto; Tab é o modo de edição do Blender.
     */
    fun preview(e: KeyEvent, store: EditorStore): Boolean {
        if (e.type != KeyEventType.KeyDown || !active(store)) return false
        val s = PcScene3D
        // Digitando num campo da cena: Esc só sai do campo (não é "voltar"); o resto é do campo.
        s.typingCancel?.let { cancel ->
            if (e.key == Key.Escape) { cancel(); return true }
            return false
        }
        if (e.key == Key.Escape) {
            when {
                s.modal != null -> s.cancelModal(store)
                s.menu != null -> s.menu = null
                s.search -> s.search = false
                s.box != null -> s.box = null
                else -> return false
            }
            return true
        }
        if (e.key == Key.Tab && s.pointerInside && s.modal == null) {
            if (e.isCtrlPressed) togglePose(store)                   // Ctrl+Tab: modo Pose
            else if (e.isShiftPressed) s.snap = !s.snap               // Shift+Tab: ímã
            else s.toast(R.string.pc3d_edit_mode_unavailable)         // Tab: modo de edição (o motor ainda não edita malha)
            return true
        }
        return false
    }

    private fun togglePose(store: EditorStore) {
        val row = store.layers.firstOrNull { it.id == store.primary }
        val canPose = row?.kind == LayerType.Model3D.kind && store.poseBones != null
        when {
            store.rigPoseMode -> store.setRigPose(false)
            canPose -> store.setRigPose(true)
            else -> PcScene3D.toast(R.string.s3d_no_rig)
        }
    }

    fun handle(e: KeyEvent, store: EditorStore): Boolean {
        if (e.type != KeyEventType.KeyDown || !active(store)) return false
        val s = PcScene3D
        val ctrl = e.isCtrlPressed
        val shift = e.isShiftPressed
        val alt = e.isAltPressed
        val k = e.key
        // Digitando num campo da cena: nenhuma tecla solta vira atalho (nem os do editor).
        if (s.typingCancel != null) return !ctrl

        // --- Durante mover/girar/escalar: o teclado é do modal --------------------------
        if (s.modal != null) {
            when (k) {
                Key.X -> s.modalAxis(store, 0, shift)
                Key.Y -> s.modalAxis(store, 1, shift)
                Key.Z -> s.modalAxis(store, 2, shift)
                Key.Enter, Key.NumPadEnter, Key.Spacebar -> s.confirmModal(store)
                Key.Backspace -> s.modalType(store, '\b')
                Key.Minus, Key.NumPadSubtract -> s.modalType(store, '-')
                Key.Period, Key.NumPadDot, Key.Comma -> s.modalType(store, '.')
                else -> digit(k)?.let { s.modalType(store, it) }
            }
            return true
        }
        if (!s.pointerInside) return false

        when {
            // --- Transformar -----------------------------------------------------------
            k == Key.G && !ctrl -> if (alt) s.clearTransform(store, s.OP_MOVE) else s.startModal(store, s.OP_MOVE)
            k == Key.R && !ctrl -> if (alt) s.clearTransform(store, s.OP_ROTATE) else s.startModal(store, s.OP_ROTATE)
            k == Key.S && shift && !ctrl -> s.menu = PcSceneMenu(PcSceneMenu.SNAP, s.mouse)          // cursor 3D
            k == Key.S && !ctrl -> if (alt) s.clearTransform(store, s.OP_SCALE) else s.startModal(store, s.OP_SCALE)

            // --- Seleção ---------------------------------------------------------------
            k == Key.A && shift && !ctrl -> s.menu = PcSceneMenu(PcSceneMenu.ADD, s.mouse)
            k == Key.A && alt -> store.clearSelection()
            k == Key.A && !ctrl -> s.selectAll(store)
            k == Key.I && ctrl -> s.invert(store)
            k == Key.H && alt -> s.reveal(store)
            k == Key.H && !ctrl -> s.hide(store, unselected = shift)

            // --- Objeto ----------------------------------------------------------------
            k == Key.I && !ctrl -> if (store.selection.isNotEmpty()) s.menu = PcSceneMenu(PcSceneMenu.KEY, s.mouse)
            k == Key.X && !ctrl -> if (store.selection.isNotEmpty()) s.menu = PcSceneMenu(PcSceneMenu.DELETE, s.mouse)
            k == Key.Delete -> s.delete(store)
            k == Key.D && shift && !ctrl -> s.duplicateAndGrab(store)
            // Alt+D: no Aurea a cópia de um modelo JÁ divide a malha com o original (o asset é o mesmo).
            k == Key.D && alt && !ctrl -> { s.duplicateAndGrab(store); s.toast(R.string.pc3d_linked_duplicate) }
            k == Key.P && ctrl -> if (store.selection.size >= 2) s.parentToActive(store) else s.menu = PcSceneMenu(PcSceneMenu.PARENT, s.mouse)
            k == Key.P && alt -> s.clearParent(store)
            k == Key.M && ctrl -> if (store.selection.isNotEmpty()) s.menu = PcSceneMenu(PcSceneMenu.MIRROR, s.mouse)
            k == Key.M && !ctrl -> if (store.selection.isNotEmpty()) s.menu = PcSceneMenu(PcSceneMenu.COLLECTION, s.mouse)
            k == Key.C && shift && !ctrl -> { PcSceneOps.cursorToWorldOrigin(); s.frameAll(store) }

            // --- Viewport --------------------------------------------------------------
            k == Key.Z && !ctrl && shift -> s.chooseShading(store, if (s.shading == s.SHADING_RENDERED) s.SHADING_MATERIAL else s.SHADING_RENDERED)
            k == Key.Z && !ctrl && alt -> ViewModes.hideOverlay = !ViewModes.hideOverlay
            k == Key.Z && !ctrl -> s.menu = PcSceneMenu(PcSceneMenu.SHADING, s.mouse)
            k == Key.N && !ctrl -> s.sidebar = !s.sidebar
            k == Key.T && !ctrl -> s.toolShelf = !s.toolShelf
            k == Key.F3 -> s.search = true
            k == Key.Period && !ctrl -> s.menu = PcSceneMenu(PcSceneMenu.PIVOT, s.mouse)
            k == Key.Comma && !ctrl -> s.menu = PcSceneMenu(PcSceneMenu.ORIENTATION, s.mouse)
            k == Key.W && !ctrl -> store.chooseSceneTool(com.aurea.aurea.state.SCENE_TOOL_SELECT)

            // --- Navegação (teclado numérico) ------------------------------------------
            k == Key.NumPad1 -> s.axisView(store, if (ctrl) 180f else 0f, 0f)
            k == Key.NumPad3 -> s.axisView(store, if (ctrl) -90f else 90f, 0f)
            k == Key.NumPad7 -> s.axisView(store, 0f, if (ctrl) -89.5f else 89.5f)
            k == Key.NumPad9 -> s.oppositeView(store)
            k == Key.NumPad5 -> s.toggleOrtho(store)
            k == Key.NumPad0 && ctrl && alt -> s.alignCameraToView(store)
            k == Key.NumPad0 -> if (s.activeCamera(store) != null || store.sceneCameraView) store.toggleSceneCameraView() else s.toast(R.string.pc3d_no_camera)
            k == Key.NumPadDot -> s.frameSelected(store)
            k == Key.MoveHome -> s.frameAll(store)
            k == Key.NumPad4 -> s.orbit(store, 15f, 0f)
            k == Key.NumPad6 -> s.orbit(store, -15f, 0f)
            k == Key.NumPad8 -> s.orbit(store, 0f, 15f)
            k == Key.NumPad2 -> s.orbit(store, 0f, -15f)
            k == Key.NumPadAdd -> s.zoom(store, 1f / 1.2f)
            k == Key.NumPadSubtract -> s.zoom(store, 1.2f)
            else -> return false
        }
        return true
    }

    private fun digit(k: Key): Char? = when (k) {
        Key.Zero, Key.NumPad0 -> '0'; Key.One, Key.NumPad1 -> '1'; Key.Two, Key.NumPad2 -> '2'
        Key.Three, Key.NumPad3 -> '3'; Key.Four, Key.NumPad4 -> '4'; Key.Five, Key.NumPad5 -> '5'
        Key.Six, Key.NumPad6 -> '6'; Key.Seven, Key.NumPad7 -> '7'; Key.Eight, Key.NumPad8 -> '8'
        Key.Nine, Key.NumPad9 -> '9'
        else -> null
    }
}
