package com.aurea.aurea.editor

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import com.aurea.aurea.R
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.LayerType

/**
 * Cena 3D do PC, fases 1 e 2 (12/11/2026): o que depende das consultas novas do
 * motor (`EditorStore.sceneOp`) — caixas e dimensões, enquadrar, pivôs no mundo,
 * cursor 3D, medir, alinhar a câmera — e as operações de objeto que usam o que o
 * motor JÁ tem: espelhar (escala negativa), coleções (um Vazio como pai, com olho
 * e cadeado em cascata) e restrições (expressões do Aurea nas trilhas de transformação).
 *
 * Com o aurea.dll antigo `sceneOp` devolve falso e cada função cai no caminho de antes.
 */
internal object PcSceneOps {
    // Códigos de `nativeSceneOp` (aurea_jni.cpp).
    private const val OP_BOUNDS = 0
    private const val OP_SCENE_BOUNDS = 1
    private const val OP_FRAME = 2
    private const val OP_ORIGIN = 3
    private const val OP_TO_PARENT = 4
    private const val OP_ROTATE = 5
    private const val OP_POINT = 6
    private const val OP_PROJECT = 7
    private const val OP_ALIGN_CAMERA = 8

    /** O cursor 3D (mundo, px). Nulo = ainda no centro da composição (a "origem do mundo" do Aurea). */
    var cursor by mutableStateOf<FloatArray?>(null)
    /** Ferramenta extra da barra T: 0 nenhuma (as do Aurea), 1 cursor, 2 medir. */
    var tool by mutableIntStateOf(TOOL_NONE)
    /** A medida à vista: dois pontos do mundo. */
    var measure by mutableStateOf<Pair<FloatArray, FloatArray>?>(null)
    const val TOOL_NONE = 0
    const val TOOL_CURSOR = 1
    const val TOOL_MEASURE = 2

    fun worldOrigin(store: EditorStore): FloatArray = floatArrayOf(store.project.width / 2f, store.project.height / 2f, 0f)
    fun cursorPoint(store: EditorStore): FloatArray = cursor ?: worldOrigin(store)

    // --- Consultas ------------------------------------------------------------------------
    /** Caixa da camada (Engine::query_layer_bounds, 26 valores) ou nulo. */
    fun bounds(store: EditorStore, id: Long): FloatArray? = FloatArray(26).takeIf { store.sceneOp(OP_BOUNDS, id, output = it) }

    /** Retângulo (px da composição) que a caixa da camada ocupa na tela, ou nulo (atrás da câmera / motor antigo). */
    fun screenRect(store: EditorStore, id: Long): Rect? {
        val b = bounds(store, id)?.takeIf { it[25] > 0.5f } ?: return null
        var x0 = Float.MAX_VALUE; var y0 = Float.MAX_VALUE; var x1 = -Float.MAX_VALUE; var y1 = -Float.MAX_VALUE
        for (i in 0 until 8) {
            x0 = minOf(x0, b[9 + i * 2]); x1 = maxOf(x1, b[9 + i * 2])
            y0 = minOf(y0, b[10 + i * 2]); y1 = maxOf(y1, b[10 + i * 2])
        }
        return Rect(x0, y0, x1, y1)
    }

    fun originWorld(store: EditorStore, id: Long): FloatArray? = FloatArray(3).takeIf { store.sceneOp(OP_ORIGIN, id, output = it) }

    /** Um passo do mundo no espaço do pai da camada (o que a Posição soma). Sem a função: vale só para quem não tem pai. */
    fun toParent(store: EditorStore, id: Long, world: FloatArray, parented: Boolean): FloatArray? {
        val out = FloatArray(3)
        if (store.sceneOp(OP_TO_PARENT, id, input = world, output = out)) return out
        return if (parented) null else world.copyOf()
    }

    /** Engine::scene_rotate_about: {posição 3, rotação 3} novas, ou nulo. `pivot` nulo = gira no lugar. */
    fun rotateAbout(store: EditorStore, id: Long, axis: Int, degrees: Float, follow: Boolean, pivot: FloatArray?,
                    basePos: FloatArray, baseRot: FloatArray): FloatArray? {
        val input = FloatArray(13)
        input[0] = axis.toFloat(); input[1] = degrees; input[2] = if (follow) 1f else 0f; input[3] = if (pivot != null) 1f else 0f
        pivot?.copyInto(input, 4, 0, 3)
        basePos.copyInto(input, 7, 0, 3); baseRot.copyInto(input, 10, 0, 3)
        return FloatArray(6).takeIf { store.sceneOp(OP_ROTATE, id, input = input, output = it) }
    }

    /** Ponto do mundo sob o ponteiro (px da composição): superfície de modelo ou o plano de frente por `through`. */
    fun pointAt(store: EditorStore, compX: Float, compY: Float, through: FloatArray?, surfaces: Boolean): FloatArray? {
        val input = FloatArray(7)
        input[0] = compX; input[1] = compY; input[2] = if (surfaces) 1f else 0f; input[3] = if (through != null) 1f else 0f
        through?.copyInto(input, 4, 0, 3)
        return FloatArray(3).takeIf { store.sceneOp(OP_POINT, input = input, output = it) }
    }

    /** Ponto do mundo → px da composição pela vista atual (nulo = atrás da câmera ou motor antigo). */
    fun project(store: EditorStore, world: FloatArray): Offset? {
        val out = FloatArray(2)
        if (!store.sceneOp(OP_PROJECT, input = world, output = out) || out[0].isNaN() || out[1].isNaN()) return null
        return Offset(out[0], out[1])
    }

    // --- Vista ------------------------------------------------------------------------------
    /** Home / Numpad . pela CAIXA: alvo no centro e distância para caber. Falso = motor antigo. */
    fun frame(store: EditorStore, ids: List<Long>): Boolean {
        if (!store.sceneOp(OP_FRAME, layers = ids.toLongArray().takeIf { it.isNotEmpty() }, input = floatArrayOf(1.2f))) return false
        store.syncSceneViewFromEngine()
        return true
    }

    /** Ctrl+Alt+Numpad 0 num passo de desfazer. Falso = motor antigo (o chamador usa o caminho de três comandos). */
    fun alignCamera(store: EditorStore, camera: Long): Boolean {
        if (!store.sceneOp(OP_ALIGN_CAMERA, camera)) return false
        store.sceneProjectChanged()
        return true
    }

    // --- Cursor 3D --------------------------------------------------------------------------
    /** Shift+botão direito (ou a ferramenta Cursor): o cursor vai para a superfície sob o ponteiro, ou fica na mesma profundidade. */
    fun placeCursor(store: EditorStore, compX: Float, compY: Float) {
        val p = pointAt(store, compX, compY, cursorPoint(store), surfaces = true)
        if (p == null) { PcScene3D.toast(R.string.pc3d_need_engine_update); return }
        cursor = p
    }

    /** Shift+S › Cursor para a seleção (o ponto médio das origens) ou para o ativo. */
    fun cursorToSelected(store: EditorStore, activeOnly: Boolean) {
        val ids = if (activeOnly) listOfNotNull(store.primary) else store.selection.toList()
        val origins = ids.mapNotNull { originWorld(store, it) }
        if (origins.isEmpty()) { if (ids.isNotEmpty()) PcScene3D.toast(R.string.pc3d_need_engine_update); return }
        cursor = FloatArray(3) { k -> origins.map { it[k] }.average().toFloat() }
    }

    fun cursorToWorldOrigin() { cursor = null }

    /** Shift+S › Seleção para o cursor: cada origem vai ao cursor (um passo de desfazer). */
    fun selectionToCursor(store: EditorStore, ids: List<Long> = store.selection.toList()) {
        if (ids.isEmpty()) return
        val target = cursorPoint(store)
        store.beginGesture("seleção para o cursor")
        for (id in ids) moveOriginTo(store, id, target)
        store.endGesture()
    }

    private fun moveOriginTo(store: EditorStore, id: Long, target: FloatArray) {
        val row = store.layers.firstOrNull { it.id == id } ?: return
        val d = store.queryDetail(id) ?: return
        val origin = originWorld(store, id) ?: (if (row.hasParent) return else d.position.toFloatArray())
        val step = toParent(store, id, FloatArray(3) { target[it] - origin[it] }, row.hasParent) ?: return
        store.sceneSetComponents(id, TrackProperty.POSITION_X, FloatArray(3) { d.position[it] + step[it] })
    }

    /** Objeto novo nasce no cursor 3D (como no Blender). Sem cursor posto, fica onde o motor o criou. */
    fun added(store: EditorStore) {
        if (cursor == null) return
        val id = store.primary ?: return
        moveOriginTo(store, id, cursorPoint(store))
    }

    // --- Espelhar (Ctrl+M + eixo) -----------------------------------------------------------
    /**
     * Espelha a seleção no eixo (0 X, 1 Y, 2 Z): a Escala do eixo troca de sinal e, com mais de um
     * objeto (ou pivô no cursor), a origem reflete em volta do pivô no eixo do mundo. Um passo de desfazer.
     */
    fun mirror(store: EditorStore, axis: Int) {
        val ids = store.selection.toList()
        if (ids.isEmpty()) return
        val origins = ids.associateWith { originWorld(store, it) }
        val known = origins.values.filterNotNull()
        val pivot: FloatArray? = when {
            PcScene3D.pivot == PcScene3D.PIVOT_INDIVIDUAL -> null
            PcScene3D.pivot == PcScene3D.PIVOT_CURSOR -> cursorPoint(store)
            PcScene3D.pivot == PcScene3D.PIVOT_ACTIVE -> store.primary?.let { origins[it] }
            known.size > 1 -> FloatArray(3) { k -> known.map { it[k] }.average().toFloat() }
            else -> null
        }
        store.beginGesture("espelhar")
        for (id in ids) {
            val row = store.layers.firstOrNull { it.id == id } ?: continue
            val d = store.queryDetail(id) ?: continue
            if (d.locked) continue
            val scale = d.scale.toFloatArray()
            scale[axis] = -scale[axis]
            store.sceneSetComponents(id, TrackProperty.SCALE_X, scale)
            val origin = origins[id]
            if (pivot != null && origin != null) {
                val world = FloatArray(3)
                world[axis] = 2f * (pivot[axis] - origin[axis])
                val step = toParent(store, id, world, row.hasParent) ?: continue
                store.sceneSetComponents(id, TrackProperty.POSITION_X, FloatArray(3) { d.position[it] + step[it] })
            }
        }
        store.endGesture()
    }

    // --- Coleções ---------------------------------------------------------------------------
    // O motor não tem "coleção". O que ele tem, salvo no projeto e com desfazer, é o parentesco:
    // uma coleção aqui é um VAZIO 3D que serve de pai. O Outliner mostra os membros dentro dele e o
    // olho/cadeado do vazio vale para todos (em cascata). Diferenças para o Blender: a coleção tem
    // transformação própria (mover o vazio move os membros) e um objeto fica numa coleção só.
    fun collections(store: EditorStore): List<LayerRow> =
        store.layers.filter { it.kind == LayerType.Null.kind && it.isThreeD }

    /** M › Nova coleção: cria o vazio (com o nome dado) e põe a seleção dentro. */
    fun newCollection(store: EditorStore, name: String) {
        val members = store.selection.toList()
        store.addNull(true)
        val id = store.primary ?: return
        if (id in members) return
        store.renameLayer(id, name)
        if (members.isNotEmpty()) store.setParentMany(members, id)
    }

    fun moveToCollection(store: EditorStore, collection: Long) {
        val members = store.selection.filter { it != collection }
        if (members.isNotEmpty()) store.setParentMany(members, collection)
    }

    /** A camada e todos os descendentes dela (pela cadeia de pais da lista de camadas). */
    fun family(store: EditorStore, root: Long): List<LayerRow> {
        val rows = store.layers
        val index = rows.indexOfFirst { it.id == root }
        if (index < 0) return emptyList()
        return rows.filterIndexed { i, _ ->
            var cur = i; var steps = 0
            while (cur in rows.indices && steps < 64) {
                if (cur == index) return@filterIndexed true
                cur = rows[cur].parentIndex; steps++
            }
            false
        }
    }

    /** Olho ou cadeado em cascata (a coleção e os membros), num passo de desfazer. */
    fun setFamily(store: EditorStore, root: Long, visible: Boolean? = null, locked: Boolean? = null) {
        val rows = family(store, root)
        if (rows.isEmpty()) return
        store.beginGesture(if (visible != null) "visibilidade da coleção" else "bloqueio da coleção")
        for (r in rows) {
            if (visible != null && r.visible != visible) store.setLayerVisible(r.id, visible)
            if (locked != null && r.locked != locked) store.setLayerLocked(r.id, locked)
        }
        store.endGesture()
    }

    // --- Restrições (expressões do Aurea) ---------------------------------------------------
    // O motor avalia expressões nas trilhas de transformação (expr/Expression.cpp). Cada restrição
    // daqui É uma expressão gravada na camada: salva no projeto, com desfazer, igual no export.
    const val CONSTRAINT_COPY_LOCATION = 0
    const val CONSTRAINT_COPY_ROTATION = 1
    const val CONSTRAINT_COPY_SCALE = 2
    const val CONSTRAINT_TRACK_TO = 3

    private fun quoted(name: String) = "\"" + name.replace("\\", "\\\\").replace("\"", "\\\"") + "\""

    /** As trilhas e o texto de cada restrição: (rótulo, trilhas, expressão). */
    fun constraintParts(kind: Int, targetName: String): List<Triple<String, List<TrackKey>, String>> {
        val t = TrackProperty
        val target = "thisComp.layer(${quoted(targetName)})"
        fun xyz(base: Int) = listOf(TrackKey(base), TrackKey(base + 1), TrackKey(base + 2))
        return when (kind) {
            CONSTRAINT_COPY_LOCATION -> listOf(Triple("position", xyz(t.POSITION_X), "$target.toWorld($target.transform.anchorPoint)"))
            CONSTRAINT_COPY_SCALE -> listOf(Triple("scale", xyz(t.SCALE_X), "$target.transform.scale"))
            CONSTRAINT_COPY_ROTATION -> listOf(
                Triple("xRotation", listOf(TrackKey(t.ROTATION_X)), "$target.transform.xRotation"),
                Triple("yRotation", listOf(TrackKey(t.ROTATION_X + 1)), "$target.transform.yRotation"),
                Triple("zRotation", listOf(TrackKey(t.ROTATION_X + 2)), "$target.transform.zRotation"),
            )
            // Mirar: a Rotação X/Y que aponta o Z da camada para o alvo (R = Rz·Ry·Rx, a ordem do motor). Usa a própria
            // POSIÇÃO: ler o mundo da camada dependeria da rotação que a expressão escreve. X e Y são trilhas escalares: uma expressão
            // para cada; a Rotação Z fica livre (o rolamento).
            else -> {
                val aim = "var d = sub($target.toWorld($target.transform.anchorPoint), thisLayer.transform.position); "
                listOf(
                    Triple("xRotation", listOf(TrackKey(t.ROTATION_X)), aim + "radiansToDegrees(Math.asin(-d[1] / length(d)))"),
                    Triple("yRotation", listOf(TrackKey(t.ROTATION_X + 1)), aim + "radiansToDegrees(Math.atan2(d[0], d[2]))"),
                )
            }
        }
    }

    /** Liga a restrição do ativo ao alvo. Devolve o erro do motor, se houver. */
    fun addConstraint(store: EditorStore, kind: Int, target: LayerRow): String? {
        val id = store.primary ?: return null
        if (id == target.id) return null
        var error: String? = null
        for ((label, tracks, source) in constraintParts(kind, target.name)) {
            val diag = store.applyExpression(EditorStore.ExpressionTarget(id, label, tracks), source)
            if (diag == null) error = PcScene3D.text(R.string.pc3d_constraint_refused)
            else if (!diag.ok) error = diag.message
        }
        return error
    }

    /** Tira as expressões de Posição, Rotação ou Escala do ativo (0, 1, 2). */
    fun clearConstraint(store: EditorStore, group: Int) {
        val id = store.primary ?: return
        val t = TrackProperty
        val base = when (group) { 0 -> t.POSITION_X; 1 -> t.ROTATION_X; else -> t.SCALE_X }
        val all = listOf(TrackKey(base), TrackKey(base + 1), TrackKey(base + 2))
        store.applyExpression(EditorStore.ExpressionTarget(id, "", all), "")
        if (group == 1) all.forEach { store.applyExpression(EditorStore.ExpressionTarget(id, "", listOf(it)), "") }
    }

    /** Posição / Rotação / Escala do ativo têm expressão agora? */
    fun constrained(store: EditorStore, group: Int): Boolean {
        val id = store.primary ?: return false
        val t = TrackProperty
        val base = when (group) { 0 -> t.POSITION_X; 1 -> t.ROTATION_X; else -> t.SCALE_X }
        return (0..2).any { store.expressionInfo(EditorStore.ExpressionTarget(id, "", listOf(TrackKey(base + it))))?.exists == true }
    }
}
