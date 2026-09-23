package com.aurea.aurea.editor.panels

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
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.ChoiceChips
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.PropertyCustomRow
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.LayerType

/**
 * MÁSCARA E RECORTE (UI da 7.2, Frente D).
 *
 * Duas abas no topo: MÁSCARAS (desenhadas na camada) e RECORTE POR OUTRA
 * CAMADA (track matte) — cada coisa no seu lugar. Nas máscaras o fluxo é o
 * da ordem de uso: Adicionar → 1 Caminho (editar no palco; ◇ do trilho anima
 * o caminho) → 2 Modo → 3 Borda (suavizar, expandir, opacidade) → 4 Rastrear
 * (só em vídeo). Poucos controles por vez.
 *
 * Com uma máscara escolhida o palco entra no modo de máscara: tocar põe
 * pontos (arrastar ao pôr curva o trecho), arrastar um ponto o move, as alças
 * do ponto escolhido mudam a curva. Tudo é rasterizado pelo motor (GPU) antes
 * dos efeitos.
 */
@Composable
internal fun MaskPanel(env: PanelEnv) {
    val store = env.store
    val st by remember(store) { derivedStateOf { store.masks } }
    val editId by remember(store) { derivedStateOf { store.maskEdit } }
    val drawing by remember(store) { derivedStateOf { store.maskDrawing } }
    var top by rememberSaveable { mutableIntStateOf(0) }
    val m = st?.find(editId)
    val look = when {
        m == null -> KeyframeLook.None
        m.keyHere -> KeyframeLook.KeyHere
        m.keyCount > 0 -> KeyframeLook.Animated
        else -> KeyframeLook.None
    }
    Row(Modifier.fillMaxSize()) {
        LeftRail(
            onBack = env.onClose,
            keyframeLook = look,
            // O que anima na máscara é o CAMINHO: o losango grava a forma no cabeçote.
            onKeyframe = if (top == 0 && m != null && m.closed && !drawing) ({ store.toggleMaskKey(m.id) }) else null,
            curveAnimated = false,
            onCurve = null,
        )
        Column(Modifier.weight(1f).fillMaxHeight()) {
            ParamTabs(listOf(stringResource(R.string.panel_mascaras), stringResource(R.string.panel_recorte_outra_camada)), top, onSelect = { top = it })
            Column(Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()).padding(start = 4.dp, end = 10.dp, bottom = 16.dp)) {
                if (top == 0) MasksTab(env, st, m, drawing) else TrackMatteTab(store)
            }
        }
    }
}

@Composable
private fun MasksTab(env: PanelEnv, state: EditorStore.MaskState?, m: EditorStore.MaskPath?, drawing: Boolean) {
    val store = env.store
    var adding by remember { mutableStateOf(false) }
    val masks = state?.masks.orEmpty()
    if (masks.isEmpty()) {
        Spacer(Modifier.height(6.dp))
        KitHint(stringResource(R.string.panel_mascara_mostra_so_parte_camada_escolha))
        Spacer(Modifier.height(8.dp))
        AddChoices(store)
        return
    }
    ChipRow {
        masks.forEachIndexed { i, mk ->
            KitChip("Máscara ${i + 1}" + if (!mk.closed) stringResource(R.string.panel_aberta) else "", m?.id == mk.id) {
                store.maskEdit = mk.id
                store.maskDrawing = !mk.closed
                store.maskPoint = -1
                adding = false
            }
        }
        KitChip(if (adding) stringResource(R.string.panel_fechar) else stringResource(R.string.panel_adicionar_mascara), adding) { adding = !adding }
    }
    if (adding) {
        Spacer(Modifier.height(4.dp))
        AddChoices(store) { adding = false }
        return
    }
    if (m == null) {
        Spacer(Modifier.height(6.dp))
        KitHint(stringResource(R.string.panel_escolha_mascara_acima_editar_palco))
        return
    }
    MaskSteps(env, m, drawing)
}

/** As três maneiras de começar uma máscara (cartões grandes). */
@Composable
private fun AddChoices(store: EditorStore, done: () -> Unit = {}) {
    ActionCard(stringResource(R.string.panel_desenhar_mao), stringResource(R.string.panel_toque_palco_pontos_arraste_curvar)) { store.startMaskDrawing(); done() }
    Spacer(Modifier.height(6.dp))
    ActionCard(stringResource(R.string.panel_retangulo), stringResource(R.string.panel_retangulo_meio_camada_pronto_ajustar)) { store.addMaskPreset(0); done() }
    Spacer(Modifier.height(6.dp))
    ActionCard(stringResource(R.string.panel_elipse), stringResource(R.string.panel_elipse_meio_camada_pronta_ajustar)) { store.addMaskPreset(1); done() }
}

/** Os passos da máscara escolhida, na ordem de uso. */
@Composable
private fun MaskSteps(env: PanelEnv, m: EditorStore.MaskPath, drawing: Boolean) {
    val store = env.store
    val isVideo = store.layers.firstOrNull { it.id == store.primary }?.kind == LayerType.Video.kind
    val steps = buildList {
        add(stringResource(R.string.panel_1_caminho)); add(stringResource(R.string.panel_2_modo)); add(stringResource(R.string.panel_3_borda))
        if (isVideo) add(stringResource(R.string.panel_4_rastrear))
    }
    var step by rememberSaveable { mutableIntStateOf(0) }
    if (step >= steps.size) step = 0
    ScrollTabs(steps, step, onSelect = { step = it })
    when (step) {
        0 -> {
            if (drawing) {
                KitHint(stringResource(R.string.panel_toque_palco_pontos_arraste_curvar_toque))
                Spacer(Modifier.height(8.dp))
                ActionCard(stringResource(R.string.panel_fechar_caminho), "${m.count} ponto(s) até agora") { store.closeMaskPath() }
            } else {
                KitHint(stringResource(R.string.panel_arraste_pontos_palco_toque_num_ponto))
                val look = when {
                    m.keyHere -> KeyframeLook.KeyHere
                    m.keyCount > 0 -> KeyframeLook.Animated
                    else -> KeyframeLook.None
                }
                PropertyCustomRow(stringResource(R.string.panel_caminho), selected = true, onSelect = {}, keyframe = look) {
                    Text(
                        if (m.keyCount == 0) stringResource(R.string.panel_parado_toque_trilho_animar) else "${m.keyCount} keyframe(s) — editar no cabeçote grava ali",
                        style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)),
                    )
                }
            }
            Spacer(Modifier.height(10.dp))
            ActionCard(stringResource(R.string.panel_apagar_mascara), null, danger = true) { store.deleteMask(m.id) }
        }
        1 -> {
            KitTitle(stringResource(R.string.panel_como_esta_mascara_combina_outras))
            val modes = listOf(0 to stringResource(R.string.panel_somar), 1 to stringResource(R.string.panel_subtrair), 2 to stringResource(R.string.panel_intersecao), 3 to stringResource(R.string.panel_diferenca), 4 to stringResource(R.string.panel_desligada))
            ChoiceChips(modes.map { it.second }, modes.indexOfFirst { it.first == m.op }, onSelect = { i ->
                store.setMaskProps(m.id, modes[i].first, m.inverted, m.feather, m.expansion, m.opacity)
            })
            ToggleLine(stringResource(R.string.panel_inverter_mostrar_lado_fora), m.inverted) { on ->
                store.setMaskProps(m.id, m.op, on, m.feather, m.expansion, m.opacity)
            }
        }
        2 -> {
            MaskRow(env, stringResource(R.string.panel_suavizar), m.id, 0, m.feather, 0.5f, 0f, 500f, "px", 0f)
            MaskRow(env, stringResource(R.string.panel_expandir), m.id, 1, m.expansion, 0.5f, -500f, 500f, "px", 0f)
            MaskRow(env, stringResource(R.string.panel_opacidade), m.id, 2, m.opacity * 100f, 0.5f, 0f, 100f, "%", 100f)
        }
        else -> {
            if (store.maskTracking) {
                KitHint(stringResource(R.string.panel_rastreando))
            } else {
                KitHint(stringResource(R.string.panel_mascara_segue_esta_embaixo_dela_cabecote))
                Spacer(Modifier.height(8.dp))
                ActionCard(stringResource(R.string.panel_seguir_posicao), stringResource(R.string.panel_objetos_so_andam_pela_tela)) { store.trackMask(m.id, 0) }
                Spacer(Modifier.height(6.dp))
                ActionCard(stringResource(R.string.panel_seguir_posicao_tamanho_giro), stringResource(R.string.panel_objetos_aproximam_ou_giram)) { store.trackMask(m.id, 1) }
            }
        }
    }
}

/** Suavizar / Expandir / Opacidade: um arrasto = um desfazer. */
@Composable
private fun MaskRow(env: PanelEnv, label: String, mask: Int, prop: Int, value: Float, step: Float, min: Float, max: Float, unit: String, default: Float) {
    val store = env.store
    fun write(v: Float) {
        val m = store.masks?.find(mask) ?: return
        when (prop) {
            0 -> store.setMaskProps(mask, m.op, m.inverted, v, m.expansion, m.opacity)
            1 -> store.setMaskProps(mask, m.op, m.inverted, m.feather, v, m.opacity)
            else -> store.setMaskProps(mask, m.op, m.inverted, m.feather, m.expansion, v / 100f)
        }
    }
    HumanRow(
        env, label, value, step, min, max, unit, 0, default,
        onStart = { store.beginGesture("máscara") },
        onValue = { write(it) },
        onEnd = { store.endGesture() },
        onCommit = { v ->
            store.beginGesture("máscara")
            write(v)
            store.endGesture()
        },
    )
}

/**
 * RECORTE POR OUTRA CAMADA (track matte): a camada só aparece através da
 * forma (alfa) ou do brilho (luma) de outra; a camada usada some da tela
 * enquanto recorta — como no AE.
 */
@Composable
private fun TrackMatteTab(store: EditorStore) {
    val tm by remember(store) { derivedStateOf { store.trackMatte } }
    val self = store.primary ?: return
    val matte = tm?.getOrNull(0) ?: 0L
    val mode = (tm?.getOrNull(1) ?: 0L).toInt()
    val rows = store.layers
    val me = rows.firstOrNull { it.id == self }
    val candidates = rows.filter { it.id != self && it.kind != LayerType.Audio.kind && it.kind != LayerType.Camera.kind && it.kind != LayerType.Light.kind }
    KitTitle(stringResource(R.string.panel_1_recortar_pelo))
    val modes = listOf(0 to stringResource(R.string.panel_nao_recortar), 1 to stringResource(R.string.panel_pela_forma), 2 to stringResource(R.string.panel_pela_forma_invertido), 3 to stringResource(R.string.panel_pelo_brilho), 4 to stringResource(R.string.panel_pelo_brilho_invertido))
    ChoiceChips(modes.map { it.second }, modes.indexOfFirst { it.first == mode }, onSelect = { i ->
        val value = modes[i].first
        if (value == 0) {
            store.setTrackMatte(0L, 0)
        } else {
            // Sem camada escolhida: a logo acima na pilha (AE).
            val target = if (matte != 0L) matte
            else candidates.filter { me != null && it.zIndex > me.zIndex }.minByOrNull { it.zIndex }?.id ?: 0L
            if (target == 0L) store.showToast("Escolha a camada abaixo") else store.setTrackMatte(target, value)
        }
    })
    KitTitle(stringResource(R.string.panel_2_qual_camada_recorta))
    if (candidates.isEmpty()) {
        KitHint(stringResource(R.string.panel_nao_ha_outra_camada_possa_recortar))
    } else {
        ChipRow {
            candidates.forEach { r ->
                KitChip(r.name.ifEmpty { LayerType.of(r.kind).label }, matte == r.id) { store.setTrackMatte(r.id, if (mode == 0) 1 else mode) }
            }
        }
    }
    Spacer(Modifier.height(4.dp))
    KitHint(if (matte != 0L) stringResource(R.string.panel_camada_escolhida_recorta_esta_some_tela) else stringResource(R.string.panel_pela_forma_aparece_onde_outra_camada))
}
