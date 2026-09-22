package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.editor.VectorPathOps
import com.aurea.aurea.editor.isSmooth
import com.aurea.aurea.engine.VGroup
import com.aurea.aurea.engine.VPaint
import com.aurea.aurea.engine.VParam
import com.aurea.aurea.engine.VStop
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.ds.ChoiceChips
import com.aurea.aurea.ui.ds.ColorWell
import com.aurea.aurea.ui.ds.KeyframeDiamondIcon
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.PropertyCustomRow
import com.aurea.aurea.ui.ds.TickRuler
import com.aurea.aurea.ui.ds.ValueBox
import com.aurea.aurea.ui.ds.valueDrag
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.roundToInt

private val PATH_KINDS = listOf("Caminho", "Retângulo", "Elipse", "Polígono", "Estrela")

/**
 * CAMADA VETORIAL (Fase 7D): grupos e caminhos, modo de pontos, forma animada
 * (keyframe de forma no cabeçote), Mesclar, Preencher (sólido/degradê, regra),
 * Contorno (largura, pontas, juntas, tracejado), Aparar, Repetidor e o
 * transform do grupo. Os valores com losango são animáveis (gravam no
 * cabeçote quando já têm keyframe). Tudo lê do motor e escreve no motor.
 */
@Composable
internal fun VectorPanel(env: PanelEnv) {
    val store = env.store
    val doc = store.vectorDoc
    if (doc == null) {
        PanelNotice("Escolha uma camada vetorial.")
        return
    }
    val gi = store.vectorGroup
    val g = doc.groups.getOrNull(gi)
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 18.dp, top = 8.dp, end = 18.dp, bottom = 24.dp)) {
        VSection("Grupos")
        ChipLine {
            doc.groups.forEachIndexed { i, grp -> VChip(grp.name, i == gi) { store.selectVectorPath(i, 0) } }
        }
        ChipLine {
            Text("Novo:", style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
            PATH_KINDS.forEachIndexed { k, name -> VChip("+ $name", false) { store.addVectorGroup(k) } }
            if (g != null && doc.groups.size > 1) VChip("Apagar grupo", false) { store.removeVectorGroup(gi) }
        }
        if (g == null) return@Column

        VSection("Caminho")
        ChipLine {
            g.paths.forEachIndexed { i, p -> VChip("${i + 1}. ${PATH_KINDS.getOrElse(p.kind) { "?" }}", i == store.vectorPath) { store.selectVectorPath(gi, i) } }
            PATH_KINDS.forEachIndexed { k, name -> VChip("+ $name", false) { store.addVectorPath(k) } }
            if (g.paths.size > 1) VChip("Apagar caminho", false) { store.removeVectorPath(store.vectorPath) }
        }
        val path = g.paths.getOrNull(store.vectorPath)
        val at = store.vectorPathAt
        if (path != null) {
            if (path.kind == 0) {
                ChipLine {
                    VChip(if (store.vectorTool == 1) "Editando pontos" else "Editar pontos", store.vectorTool == 1) {
                        store.chooseVectorTool(if (store.vectorTool == 1) 0 else 1)
                    }
                    val sel = store.vectorPoint
                    val v = at?.path?.v
                    if (v != null && sel in v.indices) {
                        VChip("Apagar ponto", false) { VectorPathOps.deletePoint(store) }
                        VChip(if (isSmooth(v[sel]) || v[sel].inX != 0f || v[sel].outX != 0f || v[sel].inY != 0f || v[sel].outY != 0f) "Tornar canto" else "Tornar suave", false) {
                            VectorPathOps.toggleSmooth(store)
                        }
                    }
                    if ((at?.path?.v?.size ?: 0) >= 2) VChip(if (at?.path?.closed == true) "Abrir caminho" else "Fechar caminho", false) { VectorPathOps.toggleClosed(store) }
                }
                // Forma animada: keyframe de forma no cabeçote (morph).
                val look = when {
                    at?.keyHere == true -> KeyframeLook.KeyHere
                    at?.animated == true -> KeyframeLook.Animated
                    else -> KeyframeLook.None
                }
                PropertyCustomRow("Forma animada", selected = false, onSelect = {}, keyframe = look) {
                    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            if (path.keys.isEmpty()) "parada" else "${path.keys.size} keyframes de forma",
                            modifier = Modifier.weight(1f),
                            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)),
                        )
                        Box(Modifier.tocavel(onClick = { store.toggleVectorPathKey() }).padding(4.dp)) { KeyframeDiamondIcon(look, enabled = true) }
                    }
                }
            } else {
                GroupRuler(store, "Largura", { it.paths.getOrNull(store.vectorPath)?.w ?: 0f }, 1f, 1f, 20000f, "px") { gr, x -> gr.paths.getOrNull(store.vectorPath)?.w = x }
                GroupRuler(store, "Altura", { it.paths.getOrNull(store.vectorPath)?.h ?: 0f }, 1f, 1f, 20000f, "px") { gr, x -> gr.paths.getOrNull(store.vectorPath)?.h = x }
                if (path.kind == 1) {
                    GroupRuler(store, "Arredondar", { it.paths.getOrNull(store.vectorPath)?.roundness ?: 0f }, 0.5f, 0f, 10000f, "px") { gr, x -> gr.paths.getOrNull(store.vectorPath)?.roundness = x }
                }
                if (path.kind >= 3) {
                    GroupRuler(store, if (path.kind == 3) "Lados" else "Pontas", { it.paths.getOrNull(store.vectorPath)?.points ?: 5f }, 0.05f, 3f, 100f, "") { gr, x -> gr.paths.getOrNull(store.vectorPath)?.points = x.roundToInt().toFloat() }
                    GroupRuler(store, "Raio externo", { it.paths.getOrNull(store.vectorPath)?.outerRadius ?: 0f }, 1f, 1f, 20000f, "px") { gr, x -> gr.paths.getOrNull(store.vectorPath)?.outerRadius = x }
                    if (path.kind == 4) {
                        GroupRuler(store, "Raio interno", { it.paths.getOrNull(store.vectorPath)?.innerRadius ?: 0f }, 1f, 1f, 20000f, "px") { gr, x -> gr.paths.getOrNull(store.vectorPath)?.innerRadius = x }
                    }
                    GroupRuler(store, "Arredondar pontas", { it.paths.getOrNull(store.vectorPath)?.outerRoundness ?: 0f }, 0.5f, -200f, 200f, "%") { gr, x -> gr.paths.getOrNull(store.vectorPath)?.outerRoundness = x }
                    GroupRuler(store, "Giro", { it.paths.getOrNull(store.vectorPath)?.rotation ?: 0f }, 0.5f, -3600f, 3600f, "°") { gr, x -> gr.paths.getOrNull(store.vectorPath)?.rotation = x }
                }
                ChipLine { VChip("Converter em caminho editável", false) { store.makeVectorPathEditable() } }
            }
            ChipLine {
                VChip(if (path.reversed) "Direção invertida" else "Direção normal", path.reversed) {
                    store.editVectorGroup { gr -> gr.paths.getOrNull(store.vectorPath)?.let { it.reversed = !it.reversed } }
                }
            }
        }

        VSection("Mesclar caminhos")
        ChoiceChips(listOf("Nenhum", "Unir", "Subtrair", "Interseção", "Excluir"), g.merge, onSelect = { m -> store.editVectorGroup { it.merge = m } })

        VSection("Preencher")
        ToggleRow("Preencher", g.fillOn) { on -> store.editVectorGroup { it.fillOn = on } }
        if (g.fillOn) {
            PaintEditor(env, g.fill, "preenchimento") { (cont, change) -> store.editVectorGroup(cont) { gr -> change(gr.fill) } }
            ChoiceChips(listOf("Não-zero", "Par-ímpar"), g.fillRule, onSelect = { r -> store.editVectorGroup { it.fillRule = r } })
            ParamRuler(store, "Opacidade", VParam.FILL_OPACITY, 0.5f, 0f, 100f, "%")
        }

        VSection("Contorno")
        ToggleRow("Contorno", g.strokeOn) { on -> store.editVectorGroup { it.strokeOn = on } }
        if (g.strokeOn) {
            PaintEditor(env, g.stroke, "contorno") { (cont, change) -> store.editVectorGroup(cont) { gr -> change(gr.stroke) } }
            ParamRuler(store, "Largura", VParam.STROKE_WIDTH, 0.2f, 0f, 2000f, "px")
            ParamRuler(store, "Opacidade", VParam.STROKE_OPACITY, 0.5f, 0f, 100f, "%")
            VLabel("Pontas")
            ChoiceChips(listOf("Reta", "Redonda", "Quadrada"), g.cap, onSelect = { c -> store.editVectorGroup { it.cap = c } })
            VLabel("Juntas")
            ChoiceChips(listOf("Miter", "Redonda", "Chanfro"), g.join, onSelect = { j -> store.editVectorGroup { it.join = j } })
            if (g.join == 0) GroupRuler(store, "Limite do miter", { it.miter }, 0.05f, 1f, 100f, "") { gr, x -> gr.miter = x }
            ToggleRow("Tracejado", g.dashes.isNotEmpty()) { on ->
                store.editVectorGroup { gr ->
                    gr.dashes.clear()
                    if (on) { gr.dashes += 24f; gr.dashes += 12f }
                }
            }
            if (g.dashes.size >= 2) {
                GroupRuler(store, "Traço", { it.dashes.getOrElse(0) { 0f } }, 0.3f, 0f, 5000f, "px") { gr, x -> if (gr.dashes.size >= 2) gr.dashes[0] = x }
                GroupRuler(store, "Vão", { it.dashes.getOrElse(1) { 0f } }, 0.3f, 0f, 5000f, "px") { gr, x -> if (gr.dashes.size >= 2) gr.dashes[1] = x }
                ParamRuler(store, "Deslocar traço", VParam.DASH_OFFSET, 0.5f, -100000f, 100000f, "px")
            }
        }

        VSection("Aparar caminhos")
        ToggleRow("Aparar", g.trimOn) { on -> store.editVectorGroup { it.trimOn = on } }
        if (g.trimOn) {
            ParamRuler(store, "Início", VParam.TRIM_START, 0.3f, 0f, 100f, "%")
            ParamRuler(store, "Fim", VParam.TRIM_END, 0.3f, 0f, 100f, "%")
            ParamRuler(store, "Deslocamento", VParam.TRIM_OFFSET, 0.5f, -100000f, 100000f, "%")
            ChoiceChips(listOf("Cada caminho", "Em sequência"), g.trimMode, onSelect = { m -> store.editVectorGroup { it.trimMode = m } })
        }

        VSection("Repetidor")
        ToggleRow("Repetir", g.repOn) { on -> store.editVectorGroup { it.repOn = on } }
        if (g.repOn) {
            ParamRuler(store, "Cópias", VParam.REP_COPIES, 0.05f, 0f, 500f, "")
            ParamRuler(store, "Deslocamento", VParam.REP_OFFSET, 0.05f, -500f, 500f, "")
            ParamRuler(store, "Posição X", VParam.REP_POS_X, 0.5f, -100000f, 100000f, "px")
            ParamRuler(store, "Posição Y", VParam.REP_POS_Y, 0.5f, -100000f, 100000f, "px")
            ParamRuler(store, "Giro", VParam.REP_ROTATION, 0.5f, -3600f, 3600f, "°")
            ParamRuler(store, "Escala", VParam.REP_SCALE, 0.3f, -10000f, 10000f, "%")
            ParamRuler(store, "Opacidade inicial", VParam.REP_START_OPACITY, 0.5f, 0f, 100f, "%")
            ParamRuler(store, "Opacidade final", VParam.REP_END_OPACITY, 0.5f, 0f, 100f, "%")
            ChoiceChips(listOf("Cópias embaixo", "Cópias por cima"), g.repAbove, onSelect = { a -> store.editVectorGroup { it.repAbove = a } })
        }

        VSection("Transformar grupo")
        ParamRuler(store, "Posição X", VParam.POS_X, 0.5f, -100000f, 100000f, "px")
        ParamRuler(store, "Posição Y", VParam.POS_Y, 0.5f, -100000f, 100000f, "px")
        ParamRuler(store, "Giro", VParam.ROTATION, 0.5f, -3600f, 3600f, "°")
        ParamRuler(store, "Escala", VParam.SCALE, 0.3f, -10000f, 10000f, "%")
        ParamRuler(store, "Opacidade", VParam.OPACITY, 0.5f, 0f, 100f, "%")
        ToggleRow("Visível", g.visible) { on -> store.editVectorGroup { it.visible = on } }
    }
}

/**
 * Tinta (sólida ou degradê): tipo, cor (sólida) ou as duas pontas do degradê
 * (cor de cada parada) e os pontos inicial/final no espaço do grupo.
 * `edit(continuing, mudança)`.
 */
@Composable
private fun PaintEditor(env: PanelEnv, paint: VPaint, what: String, edit: (Pair<Boolean, (VPaint) -> Unit>) -> Unit) {
    val store = env.store
    fun ensureStops(p: VPaint) {
        if (p.stops.size < 2) {
            p.stops.clear()
            p.stops += VStop(0f, p.r, p.g, p.b, p.a)
            p.stops += VStop(1f, 0f, 0f, 0f, 1f)
        }
    }
    ChoiceChips(listOf("Sólido", "Degradê linear", "Degradê radial"), paint.type, onSelect = { t ->
        edit(false to { p: VPaint ->
            p.type = t
            if (t != 0) ensureStops(p)
            // Pontos padrão sobre o caminho escolhido (largura da caixa do grupo).
            if (t != 0 && p.sx == -100f && p.ex == 100f) {
                val c = store.vectorDoc?.groups?.getOrNull(store.vectorGroup)?.paths?.getOrNull(store.vectorPath)
                if (c != null && c.kind != 0) { p.sx = c.cx - c.w / 2; p.sy = c.cy; p.ex = c.cx + c.w / 2; p.ey = c.cy }
            }
        })
    })
    if (paint.type == 0) {
        ColorRow("Cor do $what", Color(paint.r, paint.g, paint.b)) {
            val live = BooleanArray(1)
            env.openColor(ColorRequest(floatArrayOf(paint.r, paint.g, paint.b, paint.a),
                onChange = { r, g, b, a ->
                    edit(live[0] to { p: VPaint -> p.r = r; p.g = g; p.b = b; p.a = a })
                    live[0] = true
                },
                onDone = { }))
        }
    } else {
        paint.stops.forEachIndexed { i, s ->
            ColorRow(if (i == 0) "Cor inicial" else if (i == paint.stops.size - 1) "Cor final" else "Cor ${i + 1}", Color(s.r, s.g, s.b)) {
                val live = BooleanArray(1)
                env.openColor(ColorRequest(floatArrayOf(s.r, s.g, s.b, s.a),
                    onChange = { r, g, b, a ->
                        edit(live[0] to { p: VPaint -> p.stops.getOrNull(i)?.let { st -> st.r = r; st.g = g; st.b = b; st.a = a } })
                        live[0] = true
                    },
                    onDone = { }))
            }
        }
        ChipLine {
            if (paint.stops.size < 8) VChip("+ parada", false) {
                edit(false to { p: VPaint ->
                    val a = p.stops[p.stops.size - 2]
                    val b = p.stops.last()
                    p.stops.add(p.stops.size - 1, VStop((a.pos + b.pos) / 2, (a.r + b.r) / 2, (a.g + b.g) / 2, (a.b + b.b) / 2, (a.a + b.a) / 2))
                })
            }
            if (paint.stops.size > 2) VChip("− parada", false) { edit(false to { p: VPaint -> p.stops.removeAt(p.stops.size - 2) }) }
        }
        PaintRuler(store, paint, if (paint.type == 1) "Início X" else "Centro X", { it.sx }, { p, v -> p.sx = v }, edit)
        PaintRuler(store, paint, if (paint.type == 1) "Início Y" else "Centro Y", { it.sy }, { p, v -> p.sy = v }, edit)
        PaintRuler(store, paint, if (paint.type == 1) "Fim X" else "Raio (X)", { it.ex }, { p, v -> p.ex = v }, edit)
        if (paint.type == 1) PaintRuler(store, paint, "Fim Y", { it.ey }, { p, v -> p.ey = v }, edit)
    }
}

@Composable
private fun PaintRuler(
    store: EditorStore,
    paint: VPaint,
    label: String,
    get: (VPaint) -> Float,
    set: (VPaint, Float) -> Unit,
    edit: (Pair<Boolean, (VPaint) -> Unit>) -> Unit,
) {
    val live = remember { BooleanArray(1) }
    RulerRow(label, { get(paint) }, "${get(paint).roundToInt()} px", 0.5f, -100000f, 100000f, KeyframeLook.None, null,
        onStart = { live[0] = false },
        onValue = { v -> edit(live[0] to { p: VPaint -> set(p, v) }); live[0] = true })
    // `store` mantém a régua recompondo quando o documento muda.
    if (store.vectorDoc == null) return
}

/** Valor animável do grupo (VectorParam): régua + losango (grava no cabeçote se animado). */
@Composable
private fun ParamRuler(store: EditorStore, label: String, param: Int, step: Float, min: Float, max: Float, unit: String) {
    val v = store.vectorParams ?: return
    val anim = v.getOrElse(VParam.COUNT) { 0f }.toInt()
    val keys = v.getOrElse(VParam.COUNT + 1) { 0f }.toInt()
    val bit = 1 shl param
    val look = when {
        keys and bit != 0 -> KeyframeLook.KeyHere
        anim and bit != 0 -> KeyframeLook.Animated
        else -> KeyframeLook.None
    }
    val live = remember { BooleanArray(1) }
    val shown = v[param]
    RulerRow(label, { store.vectorParams?.getOrNull(param) ?: shown },
        if (step < 0.2f) "${"%.2f".format(shown)} $unit".trim() else "${shown.roundToInt()} $unit".trim(),
        step, min, max, look, { store.toggleVectorParamKey(param) },
        onStart = { live[0] = false },
        onValue = { x -> store.setVectorParam(param, x, continuing = live[0]); live[0] = true })
}

/** Valor NÃO animável do grupo (forma paramétrica, traço, miter): régua que edita o documento. */
@Composable
private fun GroupRuler(store: EditorStore, label: String, get: (VGroup) -> Float, step: Float, min: Float, max: Float, unit: String, set: (VGroup, Float) -> Unit) {
    val g = store.vectorDoc?.groups?.getOrNull(store.vectorGroup) ?: return
    val live = remember { BooleanArray(1) }
    val shown = get(g)
    RulerRow(label, { store.vectorDoc?.groups?.getOrNull(store.vectorGroup)?.let(get) ?: shown },
        if (step < 0.2f) "${"%.2f".format(shown)} $unit".trim() else "${shown.roundToInt()} $unit".trim(),
        step, min, max, KeyframeLook.None, null,
        onStart = { live[0] = false },
        onValue = { x -> store.editVectorGroup(continuing = live[0]) { gr -> set(gr, x) }; live[0] = true })
}

@Composable
private fun RulerRow(
    label: String,
    value: () -> Float,
    text: String,
    step: Float,
    min: Float,
    max: Float,
    look: KeyframeLook,
    onKey: (() -> Unit)?,
    onStart: () -> Unit,
    onValue: (Float) -> Unit,
) {
    PropertyCustomRow(label, selected = false, onSelect = {}, keyframe = look) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f).height(40.dp)) {
                TickRuler(
                    value = value,
                    unitsPerDp = step,
                    active = true,
                    modifier = Modifier.fillMaxSize().valueDrag(
                        enabled = true,
                        start = value,
                        unitsPerDp = { step },
                        min = min,
                        max = max,
                        onStart = onStart,
                        onValue = onValue,
                        onEnd = { },
                    ),
                )
            }
            Spacer(Modifier.width(8.dp))
            ValueBox(text, onTap = null)
            if (onKey != null) {
                Spacer(Modifier.width(4.dp))
                Box(Modifier.tocavel(onClick = onKey).padding(4.dp)) { KeyframeDiamondIcon(look, enabled = true) }
            }
        }
    }
}

@Composable
private fun ToggleRow(label: String, on: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
        AureaToggle(checked = on, onCheckedChange = onChange)
    }
}

@Composable
private fun ColorRow(label: String, color: Color, onClick: () -> Unit) {
    Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
        ColorWell(color, onClick = onClick)
    }
}

@Composable
private fun VSection(title: String) {
    Spacer(Modifier.height(10.dp))
    Text(title, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
    Spacer(Modifier.height(2.dp))
}

@Composable
private fun VLabel(text: String) {
    Text(text, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
}

@Composable
private fun ChipLine(content: @Composable () -> Unit) {
    Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).height(42.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) { content() }
}

@Composable
private fun VChip(label: String, on: Boolean, onClick: () -> Unit) {
    Box(
        Modifier.clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
            .tocavel(onClick = onClick).padding(horizontal = 10.dp, vertical = 6.dp),
    ) {
        Text(label, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
    }
}
