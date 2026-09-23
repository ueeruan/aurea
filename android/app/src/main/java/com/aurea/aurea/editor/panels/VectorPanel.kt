package com.aurea.aurea.editor.panels

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.editor.PointTool
import com.aurea.aurea.editor.VectorPathOps
import com.aurea.aurea.editor.VectorStageState
import com.aurea.aurea.engine.VGroup
import com.aurea.aurea.engine.VPaint
import com.aurea.aurea.engine.VParam
import com.aurea.aurea.engine.VStop
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.ChoiceChips
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.PropertyCustomRow
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType

private val PATH_KINDS = listOf("Caminho livre", "Retângulo", "Elipse", "Polígono", "Estrela")
private val VECTOR_TABS = listOf("Forma", "Preenchimento", "Borda", "Caminho", "Transformar", "Operadores", "Efeitos")

/** Alvo do losango do trilho: um VParam (≥ 0), a forma do caminho, ou nada. */
private const val SEL_NONE = -1
private const val SEL_SHAPE = -2

/**
 * CAMADA VETORIAL (Fase 7D; UI da 7.2, Frente D).
 *
 * Fluxo: escolher o caminho (fileira de cima) → ferramenta de pontos na aba
 * Forma (Selecionar, Adicionar ponto, Remover ponto, Canto/Suave, Fechar
 * caminho) → arrastar no palco; as alças aparecem no ponto escolhido. As
 * propriedades ficam em abas (Forma, Preenchimento, Borda, Caminho,
 * Transformar, Operadores, Efeitos), cada uma com os principais à vista e o
 * resto em "Avançado ▾". O losango do trilho age na linha escolhida (o
 * chip dela acende), igual aos outros painéis. Tudo lê e escreve no motor.
 */
@Composable
internal fun VectorPanel(env: PanelEnv) {
    val store = env.store
    val doc = store.vectorDoc
    if (doc == null) {
        PanelNotice(stringResource(R.string.panel_escolha_camada_vetorial), Modifier.padding(horizontal = 18.dp))
        return
    }
    var tab by rememberSaveable { mutableIntStateOf(0) }
    var sel by remember { mutableIntStateOf(SEL_NONE) }
    val gi = store.vectorGroup
    val g = doc.groups.getOrNull(gi)
    val at = store.vectorPathAt
    val params = store.vectorParams
    val railLook = when {
        sel >= 0 && params != null -> paramLook(params, sel)
        sel == SEL_SHAPE && at != null -> when {
            at.keyHere -> KeyframeLook.KeyHere
            at.animated -> KeyframeLook.Animated
            else -> KeyframeLook.None
        }
        else -> KeyframeLook.None
    }
    Row(Modifier.fillMaxSize()) {
        LeftRail(
            onBack = env.onClose,
            keyframeLook = railLook,
            onKeyframe = when {
                sel >= 0 && params != null -> ({ store.toggleVectorParamKey(sel) })
                sel == SEL_SHAPE && at != null && at.free -> ({ store.toggleVectorPathKey() })
                else -> null
            },
            curveAnimated = false,
            onCurve = null,
        )
        Column(Modifier.weight(1f).fillMaxHeight()) {
            PathPicker(store)
            ScrollTabs(VECTOR_TABS, tab, onSelect = { tab = it; sel = SEL_NONE })
            Column(Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()).padding(start = 4.dp, end = 10.dp, bottom = 16.dp)) {
                if (g == null) {
                    KitHint(stringResource(R.string.panel_esta_camada_ainda_nao_tem_grupos))
                    return@Column
                }
                val s = Sel(sel) { sel = it }
                when (tab) {
                    0 -> ShapeTab(env, s)
                    1 -> FillTab(env, g, s)
                    2 -> StrokeTab(env, g, s)
                    3 -> PathOpsTab(env, g, s)
                    4 -> TransformTab(env, g, s)
                    5 -> OperatorsTab(env, g, s)
                    else -> {
                        Spacer(Modifier.height(8.dp))
                        ActionCard(stringResource(R.string.panel_abrir_efeitos_camada), stringResource(R.string.panel_desfoque_brilho_cor_outros_efeitos_aplicados)) {
                            env.onOpenPanel(EditorPanel.Effects)
                        }
                    }
                }
            }
        }
    }
}

/** A linha escolhida do painel (alvo do losango do trilho). */
private class Sel(val value: Int, val set: (Int) -> Unit)

private fun paramLook(v: FloatArray, param: Int): KeyframeLook {
    val anim = v.getOrElse(VParam.COUNT) { 0f }.toInt()
    val keys = v.getOrElse(VParam.COUNT + 1) { 0f }.toInt()
    val bit = 1 shl param
    return when {
        keys and bit != 0 -> KeyframeLook.KeyHere
        anim and bit != 0 -> KeyframeLook.Animated
        else -> KeyframeLook.None
    }
}

/**
 * QUAL CAMINHO: um chip por caminho (com o grupo quando há mais de um) e
 * "+ Adicionar", que abre as formas de caminho logo abaixo.
 */
@Composable
private fun PathPicker(store: EditorStore) {
    val doc = store.vectorDoc ?: return
    var adding by remember { mutableStateOf(false) }
    val many = doc.groups.size > 1
    ChipRow {
        doc.groups.forEachIndexed { gi, grp ->
            grp.paths.forEachIndexed { pi, p ->
                val name = PATH_KINDS.getOrElse(p.kind) { "Caminho" } + if (grp.paths.size > 1) " ${pi + 1}" else ""
                KitChip(if (many) "${grp.name} · $name" else name, gi == store.vectorGroup && pi == store.vectorPath) {
                    store.selectVectorPath(gi, pi)
                }
            }
        }
        KitChip(if (adding) stringResource(R.string.panel_fechar) else stringResource(R.string.panel_adicionar), adding) { adding = !adding }
    }
    if (adding) {
        ChipRow {
            PATH_KINDS.forEachIndexed { k, name ->
                KitChip(name, false) {
                    if (k == 0) VectorStageState.pointTool = PointTool.ADD
                    store.addVectorPath(k)
                    adding = false
                }
            }
        }
    }
}

// =============================================================================
// Forma: ferramentas de pontos, tamanho do paramétrico, forma animada
// =============================================================================

@Composable
private fun ShapeTab(env: PanelEnv, s: Sel) {
    val store = env.store
    val g = store.vectorDoc?.groups?.getOrNull(store.vectorGroup) ?: return
    val path = g.paths.getOrNull(store.vectorPath) ?: return
    val at = store.vectorPathAt
    val free = path.kind == 0
    val n = at?.path?.v?.size ?: 0
    val editing = store.vectorTool == 1
    val active = PointTool.effective(VectorStageState.pointTool, n)
    var adv by remember { mutableStateOf(false) }

    fun choose(tool: Int) {
        // Forma pronta vira caminho livre (mesma silhueta) antes de mexer nos pontos.
        if (!free) store.makeVectorPathEditable()
        VectorStageState.pointTool = tool
        store.chooseVectorTool(1)
    }
    Spacer(Modifier.height(4.dp))
    Row(Modifier.fillMaxWidth().padding(start = 4.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        ToolTile(stringResource(R.string.panel_selecionar), editing && active == PointTool.SELECT, icon = { drawSelectIcon(it) }) { choose(PointTool.SELECT) }
        ToolTile(stringResource(R.string.panel_adicionar_ponto), editing && active == PointTool.ADD, icon = { drawNodeIcon(it, plus = true) }) { choose(PointTool.ADD) }
        ToolTile(stringResource(R.string.panel_remover_ponto), editing && active == PointTool.REMOVE, enabled = !free || n > 0, icon = { drawNodeIcon(it, plus = false) }) { choose(PointTool.REMOVE) }
        ToolTile(stringResource(R.string.panel_canto_suave), editing && active == PointTool.CORNER, enabled = !free || n > 1, icon = { drawCornerIcon(it) }) { choose(PointTool.CORNER) }
        val closed = at?.path?.closed == true
        ToolTile(if (closed) stringResource(R.string.panel_abrir_caminho) else stringResource(R.string.panel_fechar_caminho), false, enabled = free && n >= 2, icon = { drawCloseIcon(it, closed) }) {
            VectorPathOps.toggleClosed(store)
        }
    }
    Spacer(Modifier.height(6.dp))
    if (!editing) {
        KitHint(if (free) stringResource(R.string.panel_escolha_ferramenta_toque_palco_editar_pontos) else stringResource(R.string.panel_ferramentas_pontos_transformam_forma_pronta_caminho))
    } else {
        KitHint(stringResource(R.string.panel_editando_pontos_palco_toque_concluir_palco))
    }
    if (free) {
        val look = when {
            at?.keyHere == true -> KeyframeLook.KeyHere
            at?.animated == true -> KeyframeLook.Animated
            else -> KeyframeLook.None
        }
        PropertyCustomRow(stringResource(R.string.panel_forma_animada), selected = s.value == SEL_SHAPE, onSelect = { s.set(SEL_SHAPE) }, keyframe = look) {
            Text(
                if (path.keys.isEmpty()) stringResource(R.string.panel_parada_toque_trilho_animar) else "${path.keys.size} keyframes — editar no cabeçote grava ali",
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)),
            )
        }
    } else {
        if (path.kind == 1 || path.kind == 2) {
            PathDimRow(env, stringResource(R.string.panel_largura), { it.w }, { p, x -> p.w = x }, 1f, 1f, 20000f, "px", 200f)
            PathDimRow(env, stringResource(R.string.panel_altura), { it.h }, { p, x -> p.h = x }, 1f, 1f, 20000f, "px", 200f)
            if (path.kind == 1) PathDimRow(env, stringResource(R.string.panel_raio), { it.roundness }, { p, x -> p.roundness = x }, 0.5f, 0f, 10000f, "px", 0f)
        } else {
            PathDimRow(env, if (path.kind == 3) stringResource(R.string.panel_lados) else stringResource(R.string.panel_pontas), { it.points }, { p, x -> p.points = kotlin.math.round(x) }, 0.06f, 3f, 100f, "", 5f)
            PathDimRow(env, if (path.kind == 3) stringResource(R.string.panel_raio) else stringResource(R.string.panel_raio_externo), { it.outerRadius }, { p, x -> p.outerRadius = x }, 1f, 1f, 20000f, "px", 100f)
            if (path.kind == 4) PathDimRow(env, stringResource(R.string.panel_raio_interno), { it.innerRadius }, { p, x -> p.innerRadius = x }, 1f, 1f, 20000f, "px", 50f)
        }
    }
    AdvancedSection(adv, { adv = !adv }) {
        if (!free && path.kind >= 3) {
            PathDimRow(env, stringResource(R.string.panel_arredondar_pontas), { it.outerRoundness }, { p, x -> p.outerRoundness = x }, 0.5f, -200f, 200f, "%", 0f)
            PathDimRow(env, stringResource(R.string.panel_giro), { it.rotation }, { p, x -> p.rotation = x }, 0.5f, -3600f, 3600f, "°", 0f)
        }
        ToggleLine(stringResource(R.string.panel_inverter_direcao_caminho), path.reversed) { on ->
            store.editVectorGroup { gr -> gr.paths.getOrNull(store.vectorPath)?.reversed = on }
        }
        if (!free) {
            ActionCard(stringResource(R.string.panel_converter_caminho_livre), stringResource(R.string.panel_mantem_forma_libera_pontos)) { store.makeVectorPathEditable() }
            Spacer(Modifier.height(6.dp))
        }
        KitTitle(stringResource(R.string.panel_novo_grupo_preenchimento_borda_proprios))
        ChipRow {
            PATH_KINDS.forEachIndexed { k, name ->
                KitChip("+ $name", false) {
                    if (k == 0) VectorStageState.pointTool = PointTool.ADD
                    store.addVectorGroup(k)
                }
            }
        }
        Spacer(Modifier.height(6.dp))
        if (g.paths.size > 1) {
            ActionCard(stringResource(R.string.panel_apagar_este_caminho), null, danger = true) { store.removeVectorPath(store.vectorPath) }
            Spacer(Modifier.height(6.dp))
        }
        if ((store.vectorDoc?.groups?.size ?: 0) > 1) {
            ActionCard("Apagar o grupo \"${g.name}\"", null, danger = true) { store.removeVectorGroup(store.vectorGroup) }
        }
    }
}

// =============================================================================
// Preenchimento e Borda
// =============================================================================

@Composable
private fun FillTab(env: PanelEnv, g: VGroup, s: Sel) {
    val store = env.store
    var adv by remember { mutableStateOf(false) }
    ToggleLine(stringResource(R.string.panel_preencher), g.fillOn) { on -> store.editVectorGroup { it.fillOn = on } }
    if (!g.fillOn) return
    PaintEditor(env, g.fill, stringResource(R.string.panel_preenchimento_6e8f)) { (cont, change) -> store.editVectorGroup(cont) { gr -> change(gr.fill) } }
    ParamRow(env, s, stringResource(R.string.panel_opacidade), VParam.FILL_OPACITY, 0.5f, 0f, 100f, "%", 0, 100f)
    AdvancedSection(adv, { adv = !adv }) {
        KitTitle(stringResource(R.string.panel_onde_caminhos_cruzam))
        ChoiceChips(listOf(stringResource(R.string.panel_preencher_tudo), stringResource(R.string.panel_alternar_deixa_furos)), g.fillRule, onSelect = { r -> store.editVectorGroup { it.fillRule = r } })
        PaintAdvanced(env, g.fill) { (cont, change) -> store.editVectorGroup(cont) { gr -> change(gr.fill) } }
    }
}

@Composable
private fun StrokeTab(env: PanelEnv, g: VGroup, s: Sel) {
    val store = env.store
    var adv by remember { mutableStateOf(false) }
    ToggleLine(stringResource(R.string.panel_borda), g.strokeOn) { on -> store.editVectorGroup { it.strokeOn = on } }
    if (!g.strokeOn) return
    PaintEditor(env, g.stroke, "borda") { (cont, change) -> store.editVectorGroup(cont) { gr -> change(gr.stroke) } }
    ParamRow(env, s, stringResource(R.string.panel_largura), VParam.STROKE_WIDTH, 0.2f, 0f, 2000f, "px", 1, 6f)
    ParamRow(env, s, stringResource(R.string.panel_opacidade), VParam.STROKE_OPACITY, 0.5f, 0f, 100f, "%", 0, 100f)
    AdvancedSection(adv, { adv = !adv }) {
        KitTitle(stringResource(R.string.panel_pontas))
        ChoiceChips(listOf(stringResource(R.string.panel_retas), stringResource(R.string.panel_redondas), stringResource(R.string.panel_quadradas)), g.cap, onSelect = { c -> store.editVectorGroup { it.cap = c } })
        KitTitle(stringResource(R.string.panel_cantos))
        ChoiceChips(listOf(stringResource(R.string.panel_vivos), stringResource(R.string.panel_redondos), stringResource(R.string.panel_chanfrados)), g.join, onSelect = { j -> store.editVectorGroup { it.join = j } })
        if (g.join == 0) GroupRow(env, stringResource(R.string.panel_limite_canto), { it.miter }, { gr, x -> gr.miter = x }, 0.05f, 1f, 100f, "", 1, 4f)
        PaintAdvanced(env, g.stroke) { (cont, change) -> store.editVectorGroup(cont) { gr -> change(gr.stroke) } }
    }
}

/**
 * Tinta (sólida ou degradê): tipo e cor(es). `edit(continuing, mudança)`.
 * Os pontos do degradê e as paradas extras ficam no Avançado ([PaintAdvanced]).
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
    ChoiceChips(listOf(stringResource(R.string.panel_cor_solida), stringResource(R.string.panel_degrade_reto), stringResource(R.string.panel_degrade_redondo)), paint.type, onSelect = { t ->
        edit(false to { p: VPaint ->
            p.type = t
            if (t != 0) ensureStops(p)
            // Pontos padrão sobre o caminho escolhido (largura da caixa dele).
            if (t != 0 && p.sx == -100f && p.ex == 100f) {
                val c = store.vectorDoc?.groups?.getOrNull(store.vectorGroup)?.paths?.getOrNull(store.vectorPath)
                if (c != null && c.kind != 0) { p.sx = c.cx - c.w / 2; p.sy = c.cy; p.ex = c.cx + c.w / 2; p.ey = c.cy }
            }
        })
    })
    if (paint.type == 0) {
        ColorLine("Cor da $what", Color(paint.r, paint.g, paint.b)) {
            val live = BooleanArray(1)
            env.openColor(ColorRequest(floatArrayOf(paint.r, paint.g, paint.b, paint.a),
                onChange = { r, g, b, a ->
                    edit(live[0] to { p: VPaint -> p.r = r; p.g = g; p.b = b; p.a = a })
                    live[0] = true
                },
                onDone = { }))
        }
    } else {
        paint.stops.forEachIndexed { i, st ->
            ColorLine(if (i == 0) stringResource(R.string.panel_cor_inicial) else if (i == paint.stops.size - 1) stringResource(R.string.panel_cor_final) else "Cor ${i + 1}", Color(st.r, st.g, st.b)) {
                val live = BooleanArray(1)
                env.openColor(ColorRequest(floatArrayOf(st.r, st.g, st.b, st.a),
                    onChange = { r, g, b, a ->
                        edit(live[0] to { p: VPaint -> p.stops.getOrNull(i)?.let { q -> q.r = r; q.g = g; q.b = b; q.a = a } })
                        live[0] = true
                    },
                    onDone = { }))
            }
        }
    }
}

@Composable
private fun PaintAdvanced(env: PanelEnv, paint: VPaint, edit: (Pair<Boolean, (VPaint) -> Unit>) -> Unit) {
    if (paint.type == 0) return
    KitTitle("Degradê")
    ChipRow {
        if (paint.stops.size < 8) KitChip(stringResource(R.string.panel_cor_meio), false) {
            edit(false to { p: VPaint ->
                val a = p.stops[p.stops.size - 2]
                val b = p.stops.last()
                p.stops.add(p.stops.size - 1, VStop((a.pos + b.pos) / 2, (a.r + b.r) / 2, (a.g + b.g) / 2, (a.b + b.b) / 2, (a.a + b.a) / 2))
            })
        }
        if (paint.stops.size > 2) KitChip(stringResource(R.string.panel_cor_meio_dd59), false) { edit(false to { p: VPaint -> p.stops.removeAt(p.stops.size - 2) }) }
    }
    PaintRow(env, paint, if (paint.type == 1) "Início X" else "Centro X", { it.sx }, { p, v -> p.sx = v }, -100f, edit)
    PaintRow(env, paint, if (paint.type == 1) "Início Y" else "Centro Y", { it.sy }, { p, v -> p.sy = v }, 0f, edit)
    PaintRow(env, paint, if (paint.type == 1) "Fim X" else "Raio", { it.ex }, { p, v -> p.ex = v }, 100f, edit)
    if (paint.type == 1) PaintRow(env, paint, "Fim Y", { it.ey }, { p, v -> p.ey = v }, 0f, edit)
}

@Composable
private fun PaintRow(
    env: PanelEnv,
    paint: VPaint,
    label: String,
    get: (VPaint) -> Float,
    set: (VPaint, Float) -> Unit,
    default: Float,
    edit: (Pair<Boolean, (VPaint) -> Unit>) -> Unit,
) {
    val live = remember { BooleanArray(1) }
    HumanRow(
        env, label, get(paint), 0.5f, -100000f, 100000f, "px", 0, default,
        onStart = { live[0] = false },
        onValue = { v -> edit(live[0] to { p: VPaint -> set(p, v) }); live[0] = true },
        onEnd = { },
        onCommit = { v -> edit(false to { p: VPaint -> set(p, v) }) },
    )
}

// =============================================================================
// Caminho (aparar e tracejado), Transformar e Operadores
// =============================================================================

@Composable
private fun PathOpsTab(env: PanelEnv, g: VGroup, s: Sel) {
    val store = env.store
    var adv by remember { mutableStateOf(false) }
    ToggleLine(stringResource(R.string.panel_aparar_desenhar_so_trecho), g.trimOn) { on -> store.editVectorGroup { it.trimOn = on } }
    if (g.trimOn) {
        ParamRow(env, s, stringResource(R.string.panel_inicio), VParam.TRIM_START, 0.3f, 0f, 100f, "%", 0, 0f)
        ParamRow(env, s, stringResource(R.string.panel_fim), VParam.TRIM_END, 0.3f, 0f, 100f, "%", 0, 100f)
    }
    ToggleLine(stringResource(R.string.panel_tracejado), g.dashes.isNotEmpty()) { on ->
        store.editVectorGroup { gr ->
            gr.dashes.clear()
            if (on) { gr.dashes += 24f; gr.dashes += 12f; gr.strokeOn = true }
        }
    }
    if (g.dashes.size >= 2) {
        if (!g.strokeOn) KitHint(stringResource(R.string.panel_tracejado_aparece_borda_ligue_borda))
        GroupRow(env, stringResource(R.string.panel_traco), { it.dashes.getOrElse(0) { 0f } }, { gr, x -> if (gr.dashes.size >= 2) gr.dashes[0] = x }, 0.3f, 0f, 5000f, "px", 0, 24f)
        GroupRow(env, stringResource(R.string.panel_espaco), { it.dashes.getOrElse(1) { 0f } }, { gr, x -> if (gr.dashes.size >= 2) gr.dashes[1] = x }, 0.3f, 0f, 5000f, "px", 0, 12f)
    }
    if (g.trimOn || g.dashes.size >= 2) {
        AdvancedSection(adv, { adv = !adv }) {
            if (g.trimOn) {
                ParamRow(env, s, stringResource(R.string.panel_deslocar_trecho), VParam.TRIM_OFFSET, 0.5f, -100000f, 100000f, "%", 0, 0f)
                KitTitle(stringResource(R.string.panel_varios_caminhos))
                ChoiceChips(listOf(stringResource(R.string.panel_cada_sozinho), stringResource(R.string.panel_depois_outro)), g.trimMode, onSelect = { m -> store.editVectorGroup { it.trimMode = m } })
            }
            if (g.dashes.size >= 2) ParamRow(env, s, stringResource(R.string.panel_deslocar_tracos), VParam.DASH_OFFSET, 0.5f, -100000f, 100000f, "px", 0, 0f)
        }
    }
}

@Composable
private fun TransformTab(env: PanelEnv, g: VGroup, s: Sel) {
    val store = env.store
    var adv by remember { mutableStateOf(false) }
    KitHint(stringResource(R.string.panel_move_so_este_grupo_dentro_camada))
    ParamRow(env, s, stringResource(R.string.panel_posicao_x), VParam.POS_X, 0.5f, -100000f, 100000f, "px", 0, 0f)
    ParamRow(env, s, stringResource(R.string.panel_posicao_y), VParam.POS_Y, 0.5f, -100000f, 100000f, "px", 0, 0f)
    ParamRow(env, s, stringResource(R.string.panel_rotacao), VParam.ROTATION, 0.5f, -3600f, 3600f, "°", 0, 0f)
    ParamRow(env, s, stringResource(R.string.panel_escala), VParam.SCALE, 0.3f, -10000f, 10000f, "%", 0, 100f)
    ParamRow(env, s, stringResource(R.string.panel_opacidade), VParam.OPACITY, 0.5f, 0f, 100f, "%", 0, 100f)
    AdvancedSection(adv, { adv = !adv }) {
        ToggleLine(stringResource(R.string.panel_grupo_visivel), g.visible) { on -> store.editVectorGroup { it.visible = on } }
    }
}

@Composable
private fun OperatorsTab(env: PanelEnv, g: VGroup, s: Sel) {
    val store = env.store
    var adv by remember { mutableStateOf(false) }
    KitTitle(stringResource(R.string.panel_juntar_caminhos_grupo))
    ChoiceChips(listOf(stringResource(R.string.panel_nao_juntar), stringResource(R.string.panel_unir), stringResource(R.string.panel_subtrair), stringResource(R.string.panel_intersecao), stringResource(R.string.panel_excluir_sobreposicao)), g.merge, onSelect = { m -> store.editVectorGroup { it.merge = m } })
    if (g.paths.size < 2) KitHint(stringResource(R.string.panel_juntar_precisa_2_caminhos_grupo_use))
    ToggleLine(stringResource(R.string.panel_repetir_copias), g.repOn) { on -> store.editVectorGroup { it.repOn = on } }
    if (!g.repOn) return
    ParamRow(env, s, stringResource(R.string.panel_copias), VParam.REP_COPIES, 0.05f, 0f, 500f, "", 0, 3f)
    ParamRow(env, s, stringResource(R.string.panel_distancia_x), VParam.REP_POS_X, 0.5f, -100000f, 100000f, "px", 0, 120f)
    ParamRow(env, s, stringResource(R.string.panel_distancia_y), VParam.REP_POS_Y, 0.5f, -100000f, 100000f, "px", 0, 0f)
    ParamRow(env, s, stringResource(R.string.panel_rotacao), VParam.REP_ROTATION, 0.5f, -3600f, 3600f, "°", 0, 0f)
    AdvancedSection(adv, { adv = !adv }) {
        ParamRow(env, s, stringResource(R.string.panel_escala), VParam.REP_SCALE, 0.3f, -10000f, 10000f, "%", 0, 100f)
        ParamRow(env, s, stringResource(R.string.panel_deslocamento), VParam.REP_OFFSET, 0.05f, -500f, 500f, "", 1, 0f)
        ParamRow(env, s, stringResource(R.string.panel_opacidade_inicial), VParam.REP_START_OPACITY, 0.5f, 0f, 100f, "%", 0, 100f)
        ParamRow(env, s, stringResource(R.string.panel_opacidade_final), VParam.REP_END_OPACITY, 0.5f, 0f, 100f, "%", 0, 100f)
        KitTitle(stringResource(R.string.panel_ordem_copias))
        ChoiceChips(listOf(stringResource(R.string.panel_copias_embaixo), stringResource(R.string.panel_copias_cima)), g.repAbove, onSelect = { a -> store.editVectorGroup { it.repAbove = a } })
    }
}

// =============================================================================
// Linhas
// =============================================================================

/** Valor animável do grupo (VectorParam): escolher a linha aponta o losango do trilho para ela. */
@Composable
private fun ParamRow(env: PanelEnv, s: Sel, label: String, param: Int, step: Float, min: Float, max: Float, unit: String, decimals: Int, default: Float) {
    val store = env.store
    val v = store.vectorParams ?: return
    val live = remember { BooleanArray(1) }
    HumanRow(
        env, label, v[param], step, min, max, unit, decimals, default,
        selected = s.value == param,
        onSelect = { s.set(param) },
        keyframe = paramLook(v, param),
        onStart = { live[0] = false },
        onValue = { x -> store.setVectorParam(param, x, continuing = live[0]); live[0] = true },
        onEnd = { },
        onCommit = { x -> store.setVectorParam(param, x, continuing = false) },
    )
}

/** Valor NÃO animável do grupo (traço, limite do canto): régua que edita o documento. */
@Composable
private fun GroupRow(env: PanelEnv, label: String, get: (VGroup) -> Float, set: (VGroup, Float) -> Unit, step: Float, min: Float, max: Float, unit: String, decimals: Int, default: Float) {
    val store = env.store
    val g = store.vectorDoc?.groups?.getOrNull(store.vectorGroup) ?: return
    val live = remember { BooleanArray(1) }
    HumanRow(
        env, label, get(g), step, min, max, unit, decimals, default,
        onStart = { live[0] = false },
        onValue = { x -> store.editVectorGroup(continuing = live[0]) { gr -> set(gr, x) }; live[0] = true },
        onEnd = { },
        onCommit = { x -> store.editVectorGroup { gr -> set(gr, x) } },
    )
}

/** Medida da forma pronta do caminho escolhido. */
@Composable
private fun PathDimRow(
    env: PanelEnv,
    label: String,
    get: (com.aurea.aurea.engine.VPath) -> Float,
    set: (com.aurea.aurea.engine.VPath, Float) -> Unit,
    step: Float,
    min: Float,
    max: Float,
    unit: String,
    default: Float,
) {
    val store = env.store
    GroupRow(env, label, { gr -> gr.paths.getOrNull(store.vectorPath)?.let(get) ?: 0f }, { gr, x -> gr.paths.getOrNull(store.vectorPath)?.let { set(it, x) } },
        step, min, max, unit, 0, default)
}

// =============================================================================
// Ícones da barra de pontos (desenhados: dizem a função sem fonte de ícones)
// =============================================================================

private fun DrawScope.drawSelectIcon(c: Color) {
    val w = size.width
    val p = Path().apply {
        moveTo(w * 0.22f, w * 0.08f)
        lineTo(w * 0.22f, w * 0.84f)
        lineTo(w * 0.42f, w * 0.64f)
        lineTo(w * 0.56f, w * 0.94f)
        lineTo(w * 0.68f, w * 0.88f)
        lineTo(w * 0.54f, w * 0.58f)
        lineTo(w * 0.82f, w * 0.58f)
        close()
    }
    drawPath(p, c)
}

private fun DrawScope.drawNodeIcon(c: Color, plus: Boolean) {
    val w = size.width
    val sw = 1.6.dp.toPx()
    // Linha curva com um nó no meio.
    val p = Path().apply {
        moveTo(w * 0.02f, w * 0.78f)
        cubicTo(w * 0.25f, w * 0.30f, w * 0.45f, w * 0.30f, w * 0.5f, w * 0.52f)
    }
    drawPath(p, c, style = Stroke(sw, cap = StrokeCap.Round))
    val r = w * 0.13f
    drawRect(c, Offset(w * 0.5f - r, w * 0.52f - r), Size(2 * r, 2 * r))
    val cx = w * 0.78f
    val cy = w * 0.26f
    val arm = w * 0.17f
    drawLine(c, Offset(cx - arm, cy), Offset(cx + arm, cy), 2.dp.toPx(), cap = StrokeCap.Round)
    if (plus) drawLine(c, Offset(cx, cy - arm), Offset(cx, cy + arm), 2.dp.toPx(), cap = StrokeCap.Round)
}

private fun DrawScope.drawCornerIcon(c: Color) {
    val w = size.width
    val sw = 1.6.dp.toPx()
    // Canto (esquerda) e curva suave (direita).
    val a = Path().apply {
        moveTo(w * 0.04f, w * 0.85f)
        lineTo(w * 0.24f, w * 0.2f)
        lineTo(w * 0.44f, w * 0.85f)
    }
    drawPath(a, c, style = Stroke(sw, cap = StrokeCap.Round))
    val b = Path().apply {
        moveTo(w * 0.56f, w * 0.85f)
        cubicTo(w * 0.62f, w * 0.1f, w * 0.9f, w * 0.1f, w * 0.96f, w * 0.85f)
    }
    drawPath(b, c, style = Stroke(sw, cap = StrokeCap.Round))
    drawCircle(c, w * 0.08f, Offset(w * 0.24f, w * 0.2f))
    drawCircle(c, w * 0.08f, Offset(w * 0.76f, w * 0.30f))
}

private fun DrawScope.drawCloseIcon(c: Color, closed: Boolean) {
    val w = size.width
    val sw = 1.7.dp.toPx()
    val r = w * 0.36f
    val ctr = Offset(w / 2f, w / 2f)
    if (closed) {
        drawArc(c, 20f, 300f, false, Offset(ctr.x - r, ctr.y - r), Size(2 * r, 2 * r), style = Stroke(sw, cap = StrokeCap.Round))
    } else {
        drawCircle(c, r, ctr, style = Stroke(sw))
    }
    drawCircle(c, w * 0.09f, Offset(ctr.x + r, ctr.y))
}
