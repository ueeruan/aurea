package com.aurea.aurea.editor

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.unit.IntSize
import com.aurea.aurea.R
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.GIZMO_LENGTH
import com.aurea.aurea.state.GIZMO_MOVE
import com.aurea.aurea.state.GIZMO_ROTATE
import com.aurea.aurea.state.GIZMO_SCALE
import com.aurea.aurea.ui.theme.LayerType
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.hypot
import kotlin.math.ln
import kotlin.math.roundToInt
import kotlin.math.sin

/**
 * A Cena 3D do Aurea PC na organização do Blender (pedido do dono, 12/11/2026:
 * "idêntico ao do Blender"), com as peças e as cores do Aurea. Aqui ficam o
 * estado da área de trabalho e as OPERAÇÕES (vista, seleção, mover/girar/
 * escalar pelo teclado, pais, chaves); a tela é o PcScene3DWorkspace.kt e as
 * teclas o PcScene3DKeys.kt. O Blender é só referência de comportamento
 * (manual e mapa de teclas públicos): nada dele foi copiado.
 *
 * Toda edição vai pelo motor, pelos mesmos comandos do celular: um mover pelo
 * teclado é UM passo de desfazer, com chave automática como o gizmo.
 */
internal object PcScene3D {
    // --- Viewport -----------------------------------------------------------------------
    /** O ponteiro está em cima da viewport: é quando as teclas do Blender valem. */
    var pointerInside by mutableStateOf(false)
    /** Último ponto do ponteiro (px do palco). */
    var mouse = Offset.Zero
    var viewSize = IntSize.Zero
    val mapper = StageMapper()

    /** T: a barra de ferramentas da esquerda. */
    var toolShelf by mutableStateOf(true)
    /** N: a lateral "Item" (posição, rotação, escala e dimensões). */
    var sidebar by mutableStateOf(false)
    /** 0 arame, 1 sólido, 2 prévia de material, 3 renderizado. */
    var shading by mutableIntStateOf(SHADING_MATERIAL)
    /** 0 ponto médio, 1 origens individuais, 2 elemento ativo, 3 cursor 3D. */
    var pivot by mutableIntStateOf(PIVOT_MEDIAN)
    /** Ímã ligado (Ctrl inverte durante o gesto). */
    var snap by mutableStateOf(false)
    /** Passo do ímã ao mover (px da composição); girar usa 5° e escalar 0,1. */
    var snapStep by mutableFloatStateOf(50f)
    /** Caixa de seleção sendo arrastada (px do palco). */
    var box by mutableStateOf<Rect?>(null)
    /** Menu aberto no ponteiro (Adicionar, chave, excluir, sombreamento…). */
    var menu by mutableStateOf<PcSceneMenu?>(null)
    /** F3: a busca de comandos do app. */
    var search by mutableStateOf(false)
    /** Aba do editor de propriedades. */
    var propsTab by mutableIntStateOf(PROP_OBJECT)
    /** A ortográfica foi ligada por uma vista de eixo (Numpad 1/3/7): orbitar volta à perspectiva. */
    var autoOrtho = false
    /** Shift+D: quando a cópia chegar (a seleção troca), ela já sai andando. */
    var pendingGrab by mutableStateOf(false)
    /** Quando o Shift+D foi pedido: passado 1,5 s sem a cópia chegar, o pedido caduca. */
    var pendingGrabAt = 0L
    /** Mover/girar/escalar pelo teclado em andamento. */
    var modal by mutableStateOf<PcSceneModal?>(null)
    /** Muda a cada passo do modal: a faixa de estado redesenha. */
    var modalTick by mutableIntStateOf(0)
    /**
     * Um campo de texto da cena (renomear, número) está com o foco: as teclas são dele, e
     * este é o jeito de sair do campo com Esc. Nulo = ninguém digitando.
     */
    var typingCancel: (() -> Unit)? = null
    /** Abrir o seletor de arquivo do "Importar modelo…" (a tela é quem tem o lançador). */
    var importRequest by mutableIntStateOf(0)

    const val SHADING_WIRE = 0
    const val SHADING_SOLID = 1
    const val SHADING_MATERIAL = 2
    const val SHADING_RENDERED = 3
    const val PIVOT_MEDIAN = 0
    const val PIVOT_INDIVIDUAL = 1
    const val PIVOT_ACTIVE = 2
    const val PIVOT_CURSOR = 3
    const val PROP_RENDER = 0
    const val PROP_WORLD = 1
    const val PROP_OBJECT = 2
    const val PROP_MODIFIERS = 3
    const val PROP_DATA = 4
    const val PROP_MATERIAL = 5
    const val PROP_RIG = 6
    const val PROP_BONE = 7
    const val PROP_CONSTRAINTS = 8

    // Bits de SceneWorkspaceHost.hiddenGuides (tipos de linha do motor).
    const val GUIDE_FRAME = 1 shl 0
    const val GUIDE_CAMERA = 1 shl 1
    const val GUIDE_LIGHT = 1 shl 2
    const val GUIDE_FLOOR = 1 shl 3
    const val GUIDE_AXES = (1 shl 4) or (1 shl 5)

    fun text(id: Int, vararg args: Any): String = com.aurea.pc.PcApp.context.getString(id, *args)
    fun toast(id: Int, vararg args: Any) = com.aurea.pc.PcToasts.show(text(id, *args), false)

    /** O mapa px da composição ↔ px do palco, com a mesma conta do palco (Stage.kt). */
    fun map(store: EditorStore): StageMapper? {
        val p = store.project
        mapper.update(viewSize.width.toFloat(), viewSize.height.toFloat(), 0f, p.width, p.height, false,
            StageView.zoom, StageView.panX, StageView.panY)
        return mapper.takeIf { it.valid }
    }

    /** Entrar na cena: os limites do Blender (Topo/Base de verdade, zoom maior) e o sombreamento. */
    fun enter(store: EditorStore) {
        store.setSceneViewOptions(store.sceneOrtho, 89.5f)
        if (shading == SHADING_RENDERED) store.setPreviewScale(false, 1, 1)
    }

    /** Sair: a vista de navegação volta ao que o resto do app espera. */
    fun leave(store: EditorStore) {
        modal?.let { cancelModal(store) }
        menu = null; box = null; search = false; pendingGrab = false
        SceneWorkspaceHost.hiddenGuides = 0
        // Só desfaz o que o Renderizado pediu; a escala da prévia que a pessoa usa no editor não muda.
        if (shading == SHADING_RENDERED) store.setPreviewScale(true)
        store.setSceneViewOptions(false, 80f)
    }

    // --- Objetos da cena ------------------------------------------------------------------
    fun objects(store: EditorStore): List<LayerRow> = store.layers.filter { it.isThreeD }
    fun isObject(store: EditorStore, id: Long) = store.layers.any { it.id == id && it.isThreeD }

    /** Troca a seleção inteira; a primeira é a ATIVA (a que o gizmo e as propriedades mostram). */
    fun setSelection(store: EditorStore, ids: List<Long>) {
        val wanted = ids.distinct()
        if (wanted == store.selection.toList()) return
        store.clearSelection()
        wanted.forEach { store.select(it, additive = true, openOptions = false) }
    }

    /** Clique: escolhe só este. Shift: soma (ou tira) e o clicado vira o ativo. */
    fun click(store: EditorStore, id: Long?, extend: Boolean) {
        if (id == null) { if (!extend) store.clearSelection(); return }
        if (!extend) { setSelection(store, listOf(id)); return }
        val now = store.selection.toList()
        if (id in now && store.primary == id) setSelection(store, now - id)
        else setSelection(store, listOf(id) + (now - id))
    }

    fun selectAll(store: EditorStore) = setSelection(store, objects(store).filter { it.visible && !it.locked }.map { it.id })

    fun invert(store: EditorStore) {
        val now = store.selection
        setSelection(store, objects(store).filter { it.visible && !it.locked && it.id !in now }.map { it.id })
    }

    /** H esconde a seleção; Shift+H esconde o resto. Um passo de desfazer. */
    fun hide(store: EditorStore, unselected: Boolean) {
        val now = store.selection
        val targets = objects(store).filter { it.visible && ((it.id in now) != unselected) }
        if (targets.isEmpty()) return
        store.beginGesture("esconder")
        targets.forEach { store.setLayerVisible(it.id, false) }
        store.endGesture()
        if (!unselected) store.clearSelection()
    }

    /** Alt+H: tudo que estava escondido volta, já selecionado (como no Blender). */
    fun reveal(store: EditorStore) {
        val hidden = objects(store).filter { !it.visible }
        if (hidden.isEmpty()) return
        store.beginGesture("mostrar")
        hidden.forEach { store.setLayerVisible(it.id, true) }
        store.endGesture()
        setSelection(store, hidden.filter { !it.locked }.map { it.id })
    }

    fun delete(store: EditorStore) {
        if (store.selection.isEmpty()) return
        store.deleteLayers()
    }

    /** Shift+D: duplica e a cópia já sai no mover (o clique confirma, Esc deixa no lugar). */
    fun duplicateAndGrab(store: EditorStore) {
        if (store.selection.isEmpty()) return
        pendingGrab = true
        pendingGrabAt = System.currentTimeMillis()
        store.duplicateLayers()
    }

    /** Ctrl+P: os outros selecionados passam a seguir o ATIVO. */
    fun parentToActive(store: EditorStore) {
        val active = store.primary ?: return
        val children = store.selection.filter { it != active }
        if (children.isEmpty()) { toast(R.string.pc3d_parent_need_two); return }
        store.setParentMany(children, active)
    }

    /** Alt+P: solta o pai (o motor mantém o objeto onde está na tela). */
    fun clearParent(store: EditorStore) {
        if (store.selection.isEmpty()) return
        store.setParentMany(store.selection.toList(), 0L)
    }

    /** I: chave de Posição, Rotação e/ou Escala no cabeçote, em cada selecionado. */
    fun insertKey(store: EditorStore, location: Boolean, rotation: Boolean, scale: Boolean) {
        val ids = store.selection.toList()
        if (ids.isEmpty()) return
        val t = TrackProperty
        val groups = buildList {
            if (location) add(intArrayOf(t.POSITION_X, t.POSITION_X + 1, t.POSITION_X + 2))
            if (rotation) add(intArrayOf(t.ROTATION_X, t.ROTATION_X + 1, t.ROTATION_X + 2))
            if (scale) add(intArrayOf(t.SCALE_X, t.SCALE_X + 1, t.SCALE_X + 2))
        }
        var made = 0
        store.beginGesture("inserir keyframe")
        for (id in ids) {
            val d = store.queryDetail(id) ?: continue
            if (d.locked) continue
            for (group in groups) {
                // A função do app alterna: só chama quando AINDA não há chave aqui (inserir nunca apaga).
                if (group.any { d.hasKeyAtPlayhead(it) }) continue
                store.toggleTransformKeyframe(group, id)
                made++
            }
        }
        store.endGesture()
        toast(if (made > 0) R.string.pc3d_key_inserted else R.string.pc3d_key_already, store.playhead)
    }

    /** Alt+G / Alt+R / Alt+S: zera posição (centro da composição), rotação ou escala. */
    fun clearTransform(store: EditorStore, op: Int) {
        val ids = store.selection.toList()
        if (ids.isEmpty()) return
        val p = store.project
        store.beginGesture(when (op) { OP_ROTATE -> "zerar rotação"; OP_SCALE -> "zerar escala"; else -> "zerar posição" })
        for (id in ids) {
            if (!isObject(store, id)) continue
            when (op) {
                OP_ROTATE -> store.sceneSetComponents(id, TrackProperty.ROTATION_X, floatArrayOf(0f, 0f, 0f))
                OP_SCALE -> store.sceneSetComponents(id, TrackProperty.SCALE_X, floatArrayOf(1f, 1f, 1f))
                else -> store.sceneSetComponents(id, TrackProperty.POSITION_X, floatArrayOf(p.width / 2f, p.height / 2f, 0f))
            }
        }
        store.endGesture()
    }

    // --- Vista ----------------------------------------------------------------------------
    /** Numpad 1/3/7 (e os opostos): vista de eixo, ortográfica como no Blender. */
    fun axisView(store: EditorStore, yaw: Float, pitch: Float) {
        if (!store.sceneOrtho && store.setSceneViewOptions(true, 89.5f)) autoOrtho = true
        store.snapSceneView(yaw, pitch)
    }

    /** Numpad 5: alterna perspectiva e ortográfica. */
    fun toggleOrtho(store: EditorStore) {
        autoOrtho = false
        if (!store.setSceneViewOptions(!store.sceneOrtho, 89.5f)) toast(R.string.pc3d_need_engine_update)
    }

    /** Orbitar (botão do meio, Numpad 2/4/6/8, gizmo): sai da ortográfica automática. */
    fun orbit(store: EditorStore, dYaw: Float, dPitch: Float) {
        if (autoOrtho && store.sceneOrtho) { autoOrtho = false; store.setSceneViewOptions(false, 89.5f) }
        store.updateSceneView(store.sceneYaw + dYaw, store.scenePitch + dPitch, store.sceneDistance)
    }

    fun zoom(store: EditorStore, factor: Float) =
        store.updateSceneView(store.sceneYaw, store.scenePitch, store.sceneDistance * factor)

    /** Numpad 9: o lado oposto. */
    fun oppositeView(store: EditorStore) = store.snapSceneView(store.sceneYaw + 180f, -store.scenePitch)

    /** Numpad . : a órbita passa a girar em volta da seleção, que vem para o meio. */
    fun frameSelected(store: EditorStore) {
        val ids = store.selection.filter { isObject(store, it) }
        if (ids.isEmpty()) { frameAll(store); return }
        // Pela caixa dos objetos (alvo no centro, distância para caber); motor antigo: só o alvo.
        if (PcSceneOps.frame(store, ids)) return
        if (store.focusSceneView(ids)) {
            store.updateSceneView(store.sceneYaw, store.scenePitch, minOf(store.sceneDistance, 1.6f))
            return
        }
        // Motor antigo (sem focus_scene_view): leva a origem do ativo ao meio do quadro.
        val g = store.primary?.let { store.sceneGizmoOf(it) } ?: return
        store.panSceneView(store.project.width / 2f - g[0], store.project.height / 2f - g[1])
    }

    /** Home: tudo à vista. */
    fun frameAll(store: EditorStore) {
        val all = objects(store).filter { it.visible }.map { it.id }
        if (all.isNotEmpty() && PcSceneOps.frame(store, emptyList())) return
        if (all.isNotEmpty() && store.focusSceneView(all)) {
            store.updateSceneView(store.sceneYaw, store.scenePitch, 3f)
        } else sceneFrameAll(store)
    }

    /** A câmera ativa da composição (a de cima na pilha), ou nulo. */
    fun activeCamera(store: EditorStore): LayerRow? =
        store.layers.firstOrNull { it.kind == LayerType.Camera.kind && it.visible }

    /**
     * Ctrl+Alt+Numpad 0: a câmera ativa vai para onde a vista está. Posição = o olho da
     * órbita; ponto de interesse = o alvo (câmera de dois nós, sem rolagem). A lente
     * da câmera não muda.
     */
    fun alignCameraToView(store: EditorStore) {
        val camera = activeCamera(store) ?: run { toast(R.string.pc3d_no_camera); return }
        // O motor faz tudo num comando (um passo de desfazer); o caminho de baixo é só para o DLL antigo.
        if (PcSceneOps.alignCamera(store, camera.id)) {
            setSelection(store, listOf(camera.id))
            if (!store.sceneCameraView) store.toggleSceneCameraView()
            toast(R.string.pc3d_camera_aligned)
            return
        }
        val v = store.querySceneView() ?: run { toast(R.string.pc3d_need_engine_update); return }
        val p = store.project
        val yaw = Math.toRadians(v[0].toDouble()); val pitch = Math.toRadians(v[1].toDouble())
        val distance = p.height.coerceAtLeast(1) * v[2]
        val tx = p.width / 2f + v[4]; val ty = p.height / 2f + v[5]; val tz = v[6]
        val eye = floatArrayOf(
            tx + (sin(yaw) * cos(pitch)).toFloat() * distance,
            ty - sin(pitch).toFloat() * distance,
            tz - (cos(yaw) * cos(pitch)).toFloat() * distance,
        )
        setSelection(store, listOf(camera.id))
        // Três comandos do motor (dois nós, posição, ponto de interesse): cada um é um passo de desfazer.
        store.setCameraTwoNode(true)
        store.sceneSetComponents(camera.id, TrackProperty.POSITION_X, eye)
        store.setCameraPoi(tx, ty, tz)
        if (!store.sceneCameraView) store.toggleSceneCameraView()
        toast(R.string.pc3d_camera_aligned)
    }

    // --- Sombreamento e sobreposições -----------------------------------------------------
    /**
     * Renderizado = a prévia na resolução inteira; Prévia de material = a prévia
     * automática do app. Sólido e Arame pedem um modo de sombreamento da viewport que o
     * motor ainda não tem (build/reference/blender/gap_scene3d.md, fase 1).
     */
    fun chooseShading(store: EditorStore, mode: Int) {
        if (mode == SHADING_WIRE || mode == SHADING_SOLID) { toast(R.string.pc3d_shading_need_engine); return }
        if (shading == mode) return
        shading = mode
        if (mode == SHADING_RENDERED) store.setPreviewScale(false, 1, 1) else store.setPreviewScale(true)
    }

    fun guideHidden(bits: Int) = SceneWorkspaceHost.hiddenGuides and bits == bits
    fun toggleGuide(bits: Int) {
        val now = SceneWorkspaceHost.hiddenGuides
        SceneWorkspaceHost.hiddenGuides = if (now and bits == bits) now and bits.inv() else now or bits
    }

    // --- Mover / girar / escalar pelo teclado (G, R, S) -----------------------------------
    const val OP_MOVE = 0
    const val OP_ROTATE = 1
    const val OP_SCALE = 2

    fun startModal(store: EditorStore, op: Int) {
        if (modal != null) return
        if (rigPoseActive(store) || rigFixActive(store)) { toast(R.string.pc3d_pose_use_gizmo); return }
        val m = map(store) ?: return
        val primary = store.primary
        if (primary != null && store.shapePartOf(primary) >= 0) { toast(R.string.pc3d_part_use_gizmo); return }
        // Filho de um selecionado segue o pai (como no Blender): só o de cima entra.
        val chosen = store.selection
        val rows = store.layers
        fun underSelected(row: com.aurea.aurea.engine.LayerRow): Boolean {
            var cur = row.parentIndex; var steps = 0
            while (cur in rows.indices && steps < 64) { if (rows[cur].id in chosen) return true; cur = rows[cur].parentIndex; steps++ }
            return false
        }
        val items = store.selection.mapNotNull { id ->
            val row = rows.firstOrNull { it.id == id } ?: return@mapNotNull null
            if (row.locked || underSelected(row)) return@mapNotNull null
            val d = store.queryDetail(id) ?: return@mapNotNull null
            val g = store.sceneGizmoOf(id) ?: return@mapNotNull null
            PcSceneModal.Item(id, d.kind, d.position.toFloatArray(), d.rotation.toFloatArray(), d.scale.toFloatArray(),
                store.previewGestureBasis(id), row.hasParent, Offset(g[0], g[1]),
                if (op == OP_ROTATE) store.queryTrackball(id) else null,
                PcSceneOps.originWorld(store, id) ?: (if (row.hasParent) null else d.position.toFloatArray()))
        }
        if (items.isEmpty()) return
        if (store.playing) store.pause()
        val active = items.firstOrNull { it.id == primary } ?: items.first()
        val worlds = items.mapNotNull { it.world }
        // O pivô no MUNDO: cursor 3D, o ativo ou o ponto médio das origens (nulo = cada um na sua).
        val pivotWorld: FloatArray? = when {
            pivot == PIVOT_INDIVIDUAL -> null
            pivot == PIVOT_CURSOR -> PcSceneOps.cursorPoint(store)
            pivot == PIVOT_ACTIVE -> active.world
            worlds.isNotEmpty() -> FloatArray(3) { k -> worlds.map { it[k] }.average().toFloat() }
            else -> null
        }
        // Na tela: o pivô projetado (o ângulo e a distância do ponteiro medem em volta dele).
        val pivotComp = pivotWorld?.let { PcSceneOps.project(store, it) }
            ?: if (pivot == PIVOT_ACTIVE) active.origin
            else Offset(items.map { it.origin.x }.average().toFloat(), items.map { it.origin.y }.average().toFloat())
        val modalState = PcSceneModal(op, items, store.sceneGizmoOf(active.id), Offset(m.sx(pivotComp.x), m.sy(pivotComp.y)),
            pivotComp, pivotWorld, mouse)
        store.beginGesture(when (op) { OP_ROTATE -> "girar"; OP_SCALE -> "escalar"; else -> "mover" })
        // A ferramenta do gizmo acompanha a operação (como o Blender mostra o gizmo dela).
        store.chooseSceneTool(when (op) { OP_ROTATE -> GIZMO_ROTATE; OP_SCALE -> GIZMO_SCALE; else -> GIZMO_MOVE })
        modal = modalState
        modalTick++
    }

    /** O ponteiro andou durante o modal. Shift = precisão (um décimo); Ctrl inverte o ímã. */
    fun modalMove(store: EditorStore, at: Offset, shift: Boolean, ctrl: Boolean) {
        val s = modal ?: return
        val fine = if (shift && s.axisPending == 0) 0.1f else 1f
        val d = at - s.last
        s.accX += d.x * fine; s.accY += d.y * fine
        val a0 = atan2(s.last.y - s.pivotScreen.y, s.last.x - s.pivotScreen.x)
        val a1 = atan2(at.y - s.pivotScreen.y, at.x - s.pivotScreen.x)
        var turn = a1 - a0
        while (turn > Math.PI) turn -= (2 * Math.PI).toFloat()
        while (turn < -Math.PI) turn += (2 * Math.PI).toFloat()
        s.accAngle += Math.toDegrees(turn.toDouble()).toFloat() * fine
        val r0 = hypot(s.last.x - s.pivotScreen.x, s.last.y - s.pivotScreen.y).coerceAtLeast(4f)
        val r1 = hypot(at.x - s.pivotScreen.x, at.y - s.pivotScreen.y).coerceAtLeast(4f)
        s.accLog += ln(r1 / r0) * fine
        s.last = at
        applyModal(store, shift, ctrl)
    }

    /** X/Y/Z no modal: trava o eixo; a mesma tecla de novo solta. Shift+eixo = o plano dos outros dois. */
    fun modalAxis(store: EditorStore, axis: Int, plane: Boolean) {
        val s = modal ?: return
        if (s.axis == axis && s.plane == plane) { s.axis = -1; s.plane = false } else { s.axis = axis; s.plane = plane }
        applyModal(store, false, com.aurea.pc.PcKeys.ctrl)
    }

    /** Número digitado no modal (px, graus ou fator). */
    fun modalType(store: EditorStore, ch: Char) {
        val s = modal ?: return
        s.typed = when {
            ch == '\b' -> s.typed.dropLast(1)
            ch == '-' -> if (s.typed.startsWith("-")) s.typed.drop(1) else "-" + s.typed
            ch == '.' -> if ('.' in s.typed) s.typed else s.typed + "."
            ch.isDigit() -> s.typed + ch
            else -> s.typed
        }
        applyModal(store, false, com.aurea.pc.PcKeys.ctrl)
    }

    fun confirmModal(store: EditorStore) {
        if (modal == null) return
        modal = null
        store.endGesture()
        modalTick++
    }

    /** Esc / botão direito: tudo volta ao começo (o passo aberto é desfeito). */
    fun cancelModal(store: EditorStore) {
        val s = modal ?: return
        modal = null
        store.endGesture()
        if (s.changed) store.undo()
        modalTick++
    }

    private fun snapTo(v: Float, step: Float) = if (step > 0f) (v / step).roundToInt() * step else v

    private fun applyModal(store: EditorStore, shift: Boolean, ctrl: Boolean) {
        val s = modal ?: return
        val m = map(store) ?: return
        val snapping = snap != ctrl
        val typed = s.typed.toFloatOrNull()
        val t = TrackProperty
        when (s.op) {
            OP_MOVE -> {
                var dx = s.accX / m.fit; var dy = s.accY / m.fit
                val step = if (shift) snapStep / 10f else snapStep
                val g = s.gizmo
                fun axisOnScreen(k: Int) = if (g == null) Offset.Zero else Offset(g[(k + 1) * 2] - g[0], g[(k + 1) * 2 + 1] - g[1])
                val local = if (store.gizmoLocalSpace) 3 else 0
                if (s.axis < 0) {
                    if (snapping) { dx = snapTo(dx, step); dy = snapTo(dy, step) }
                    s.value = hypot(dx, dy); s.value2 = Float.NaN
                    for (it in s.items) {
                        val out = it.basis?.let { b -> AureaEngine.previewGestureValue(b, dx, dy, false) } ?: continue
                        if (out.size >= 3) write(store, s, it, t.POSITION_X, floatArrayOf(out[0], out[1], out[2]))
                    }
                } else if (!s.plane) {
                    val a = axisOnScreen(s.axis)
                    val len2 = a.x * a.x + a.y * a.y
                    var amount = typed ?: if (len2 > 1f) (dx * a.x + dy * a.y) / len2 * GIZMO_LENGTH else -dy * 2f
                    if (typed == null && snapping) amount = snapTo(amount, step)
                    s.value = amount; s.value2 = Float.NaN
                    for (it in s.items) {
                        val dir = it.direction(store, s.axis + local) ?: continue
                        write(store, s, it, t.POSITION_X, FloatArray(3) { k -> it.pos[k] + dir[k] * amount })
                    }
                } else {
                    val i = (s.axis + 1) % 3; val j = (s.axis + 2) % 3
                    val a = axisOnScreen(i); val b = axisOnScreen(j)
                    val det = a.x * b.y - a.y * b.x
                    if (abs(det) < 1e-3f) return
                    var u = (dx * b.y - dy * b.x) / det * GIZMO_LENGTH
                    var v = (a.x * dy - a.y * dx) / det * GIZMO_LENGTH
                    if (snapping) { u = snapTo(u, step); v = snapTo(v, step) }
                    s.value = u; s.value2 = v
                    for (it in s.items) {
                        val du = it.direction(store, i + local) ?: continue
                        val dv = it.direction(store, j + local) ?: continue
                        write(store, s, it, t.POSITION_X, FloatArray(3) { k -> it.pos[k] + du[k] * u + dv[k] * v })
                    }
                }
            }
            OP_ROTATE -> {
                var deg = typed ?: s.accAngle
                if (typed == null && snapping) deg = snapTo(deg, if (shift) 1f else 5f)
                s.value = deg; s.value2 = Float.NaN
                // Eixo do motor: 0..2 mundo, 3..5 local do objeto, 6 = o olhar da vista (R sem eixo).
                val engineAxis = if (s.axis < 0) 6 else s.axis + (if (store.gizmoLocalSpace) 3 else 0)
                val pivotWorld = s.pivotWorld
                val rad = Math.toRadians(deg.toDouble())
                val c = cos(rad).toFloat(); val sn = sin(rad).toFloat()
                for (it in s.items) {
                    // O motor compõe a rotação (quaternions) e leva a origem em volta do pivô; devolve Posição e Rotação.
                    val turned = PcSceneOps.rotateAbout(store, it.id, engineAxis, deg, follow = typed == null && s.axis >= 0,
                        pivot = pivotWorld, basePos = it.pos, baseRot = it.rot)
                    if (turned != null) {
                        write(store, s, it, t.ROTATION_X, floatArrayOf(turned[3], turned[4], turned[5]))
                        if (pivotWorld != null) write(store, s, it, t.POSITION_X, floatArrayOf(turned[0], turned[1], turned[2]))
                        continue
                    }
                    // Motor antigo: soma no componente (eixo) ou o anel da vista do trackball.
                    val rot = it.rot.copyOf()
                    if (s.axis >= 0) rot[s.axis] = it.rot[s.axis] + deg
                    else viewRotation(it, deg)?.copyInto(rot) ?: run { rot[2] = it.rot[2] + deg }
                    write(store, s, it, t.ROTATION_X, rot)
                    if (pivot != PIVOT_INDIVIDUAL && s.items.size > 1 && s.axis < 0) {
                        val o = it.origin - s.pivotComp
                        val moved = Offset(o.x * c - o.y * sn, o.x * sn + o.y * c) - o
                        val out = it.basis?.let { b -> AureaEngine.previewGestureValue(b, moved.x, moved.y, false) }
                        if (out != null && out.size >= 3) write(store, s, it, t.POSITION_X, floatArrayOf(out[0], out[1], out[2]))
                    }
                }
            }
            else -> {
                var f = typed ?: exp(s.accLog)
                if (typed == null && snapping) f = snapTo(f, if (shift) 0.01f else 0.1f)
                if (typed == null) f = f.coerceIn(0.001f, 1000f)
                if (!f.isFinite() || f == 0f) return
                s.value = f; s.value2 = Float.NaN
                val pivotWorld = s.pivotWorld
                for (it in s.items) {
                    var out = it.scl.copyOf()
                    when {
                        s.axis < 0 -> out = AureaEngine.gestureScale3D(it.kind, out[0], out[1], out[2], 3, f)
                        !s.plane -> out = AureaEngine.gestureScale3D(it.kind, out[0], out[1], out[2], s.axis, f)
                        else -> for (k in 0..2) if (k != s.axis) out = AureaEngine.gestureScale3D(it.kind, out[0], out[1], out[2], k, f)
                    }
                    if (out.size >= 3) write(store, s, it, t.SCALE_X, floatArrayOf(out[0], out[1], out[2]))
                    val world = it.world
                    if (pivotWorld != null && world != null) {
                        // A distância ao pivô escala junto (eixos do mundo); o passo volta para o espaço do pai.
                        val step = FloatArray(3) { k ->
                            val scaled = if (s.axis < 0) true else if (s.plane) k != s.axis else k == s.axis
                            if (scaled) (world[k] - pivotWorld[k]) * (f - 1f) else 0f
                        }
                        val local = PcSceneOps.toParent(store, it.id, step, it.parented)
                        if (local != null) write(store, s, it, t.POSITION_X, FloatArray(3) { k -> it.pos[k] + local[k] })
                    }
                }
            }
        }
        modalTick++
    }

    /** R sem eixo: gira em volta do OLHAR pela conta do trackball do motor (anel da vista). */
    private fun viewRotation(item: PcSceneModal.Item, degrees: Float): FloatArray? {
        val t = item.track ?: return null
        if (t.size < 23) return null
        val radius = 100f
        val args = FloatArray(27)
        t.copyInto(args, 0, 11, 20)        // frame da vista
        t.copyInto(args, 9, 20, 23)        // rotação no início
        t.copyInto(args, 12, 20, 23)       // anterior = início
        args[18] = 1f                      // quaternion acumulado = identidade
        args[19] = 3f                      // anel da vista
        args[20] = radius; args[21] = 0f
        args[26] = radius
        var from = 0f
        var out: FloatArray? = null
        val steps = (abs(degrees) / 30f).toInt() + 1
        for (i in 1..steps) {
            val to = degrees * i / steps
            val a = Math.toRadians(from.toDouble()); val b = Math.toRadians(to.toDouble())
            args[22] = radius * cos(a).toFloat(); args[23] = radius * sin(a).toFloat()
            args[24] = radius * cos(b).toFloat(); args[25] = radius * sin(b).toFloat()
            val step = AureaEngine.trackballDrag(args)
            if (step.size != 7) return out
            step.copyInto(args, 12, 0, 3)
            step.copyInto(args, 15, 3, 7)
            out = floatArrayOf(step[0], step[1], step[2])
            from = to
        }
        return out
    }

    private fun write(store: EditorStore, s: PcSceneModal, item: PcSceneModal.Item, base: Int, values: FloatArray) {
        if (values.any { !it.isFinite() }) return
        val slot = when (base) { TrackProperty.POSITION_X -> 0; TrackProperty.ROTATION_X -> 1; else -> 2 }
        val previous = item.written[slot] ?: when (slot) { 0 -> item.pos; 1 -> item.rot; else -> item.scl }
        if ((0..2).all { abs(values[it] - previous[it]) < 1e-5f }) return
        store.sceneSetComponents(item.id, base, values, previous)
        item.written[slot] = values.copyOf()
        s.changed = true
    }
}

/** Menus que abrem no ponteiro. */
internal class PcSceneMenu(val kind: Int, val at: Offset) {
    companion object {
        const val ADD = 0
        const val KEY = 1
        const val DELETE = 2
        const val SHADING = 3
        const val PIVOT = 4
        const val ORIENTATION = 5
        const val CONTEXT = 6
        const val PARENT = 7
        const val SNAP = 8
        const val MIRROR = 9
        const val COLLECTION = 10
    }
}

/** Um mover/girar/escalar pelo teclado: o que foi pego no começo e o quanto o ponteiro andou. */
internal class PcSceneModal(
    val op: Int,
    val items: List<Item>,
    /** Setas do ativo no começo (px da composição): como cada eixo aparece na tela. */
    val gizmo: FloatArray?,
    val pivotScreen: Offset,
    val pivotComp: Offset,
    val pivotWorld: FloatArray?,
    start: Offset,
) {
    class Item(
        val id: Long, val kind: Int, val pos: FloatArray, val rot: FloatArray, val scl: FloatArray,
        val basis: FloatArray?, val parented: Boolean, val origin: Offset, val track: FloatArray?,
        /** Origem no mundo no começo do gesto (nulo = desconhecida: tem pai e o motor é antigo). */
        val world: FloatArray? = null,
    ) {
        /** Último valor gravado de posição, rotação e escala. */
        val written = arrayOfNulls<FloatArray>(3)
        private val directions = arrayOfNulls<FloatArray>(6)

        /** Quanto a posição (espaço do pai) anda por unidade no eixo (0..2 mundo, 3..5 local). */
        fun direction(store: EditorStore, axis: Int): FloatArray? {
            directions[axis]?.let { return it }
            val zero = store.sceneAxisMove(id, axis, 0f) ?: return null
            val one = store.sceneAxisMove(id, axis, 1f) ?: return null
            return FloatArray(3) { one[it] - zero[it] }.also { directions[axis] = it }
        }
    }

    var axis by mutableIntStateOf(-1)
    var plane by mutableStateOf(false)
    var typed by mutableStateOf("")
    var value = 0f
    var value2 = Float.NaN
    var last = start
    var accX = 0f
    var accY = 0f
    var accAngle = 0f
    var accLog = 0f
    var changed = false
    /** Reservado: Shift apertado só para escolher o plano não conta como precisão. */
    var axisPending = 0
}
