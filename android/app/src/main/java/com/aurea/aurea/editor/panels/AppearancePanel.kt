package com.aurea.aurea.editor.panels

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.BlendMode
import androidx.compose.ui.graphics.CompositingStrategy
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.TickRuler
import com.aurea.aurea.ui.ds.valueDrag
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

/**
 * Um modo de mescla [A] (`ModoDeMescla`): rótulo, o `aurea::BlendMode` (nulo =
 * modo que o motor não tem) e o modo do Compose para a miniatura.
 */
private class BlendChoice(val label: String, val engine: Int?, val preview: BlendMode)

private class BlendCategory(val name: String, val modes: List<BlendChoice>)

/**
 * AS SETE CATEGORIAS DA MESCLAGEM [A] (`categoriasDeMescla`, v1.1.1). Os números
 * são `aurea::BlendMode` (Types.hpp): Normal 0, Add 1, Subtract 2, Multiply 3,
 * Screen 4, Overlay 5, Darken 6, Lighten 7, ColorDodge 8, ColorBurn 9, HardLight
 * 10, SoftLight 11, Difference 12, Exclusion 13, Hue 14, Saturation 15, Color 16,
 * Luminosity 17.
 */
private val BlendCategories = listOf(
    BlendCategory("Normal", listOf(BlendChoice("Normal", 0, BlendMode.SrcOver))),
    BlendCategory(
        "Escurecer",
        listOf(
            BlendChoice("Escurecer", 6, BlendMode.Darken), BlendChoice("Multiplicar", 3, BlendMode.Multiply),
            BlendChoice("Queimar cor", 9, BlendMode.ColorBurn),
        ),
    ),
    BlendCategory(
        "Clarear",
        listOf(
            BlendChoice("Clarear", 7, BlendMode.Lighten), BlendChoice("Tela", 4, BlendMode.Screen),
            BlendChoice("Subexpor cor", 8, BlendMode.ColorDodge), BlendChoice("Adicionar", 1, BlendMode.Plus),
        ),
    ),
    BlendCategory(
        "Contraste",
        listOf(
            BlendChoice("Sobrepor", 5, BlendMode.Overlay), BlendChoice("Luz suave", 11, BlendMode.Softlight),
            BlendChoice("Luz forte", 10, BlendMode.Hardlight),
        ),
    ),
    BlendCategory(
        "Diferença",
        listOf(
            BlendChoice("Diferença", 12, BlendMode.Difference), BlendChoice("Exclusão", 13, BlendMode.Exclusion),
            BlendChoice("Subtrair", 2, BlendMode.SrcOver),
        ),
    ),
    BlendCategory(
        "Cor",
        listOf(
            BlendChoice("Matiz", 14, BlendMode.Hue), BlendChoice("Saturação", 15, BlendMode.Saturation),
            BlendChoice("Cor", 16, BlendMode.Color), BlendChoice("Luminosidade", 17, BlendMode.Luminosity),
        ),
    ),
)

/**
 * Os modos que o renderer desenha: todos os de `aurea::BlendMode` (Normal no
 * blend de hardware; os outros num passe que lê o fundo). Só entra na lista o
 * modo que o motor desenha: nada de chip que não faz nada.
 */
private val RenderedBlendModes = (0..17).toSet()

/**
 * MESCLAGEM E OPACIDADE [A] (`BlendingPanel`): trilho com ◇ e curva da opacidade,
 * abas "Opacidade · Mistura · Máscara". Máscara abre o painel de máscara e
 * recorte (o mesmo da doca), onde mora também o recorte por outra camada.
 */
@Composable
internal fun AppearancePanel(env: PanelEnv) {
    val store = env.store
    var tab by rememberSaveable { mutableIntStateOf(0) }
    val look by remember(store) { derivedStateOf { transformLook(store.detail, intArrayOf(TrackProperty.OPACITY)) } }
    val curveReady by remember(store) { derivedStateOf { store.primaryKeys().transformTrack(TrackProperty.OPACITY).size >= 2 } }
    val exprLook by remember(store) { derivedStateOf { store.expressionLook(OpacityKeys) } }

    Row(Modifier.fillMaxSize()) {
        LeftRail(
            onBack = env.onClose,
            keyframeLook = look,
            onKeyframe = if (tab == 0) ({ store.toggleTransformKeyframe(intArrayOf(TrackProperty.OPACITY)) }) else null,
            curveAnimated = look != KeyframeLook.None,
            onCurve = if (tab == 0 && curveReady) {
                {
                    val layer = store.primary
                    val t = store.detail?.localPlayhead
                    if (layer != null && t != null) {
                        store.primaryKeys().transformTrack(TrackProperty.OPACITY).segmentStart(t)?.let {
                            store.selectKeyframe(layer, it)
                            env.onOpenPanel(EditorPanel.Curve)
                        }
                    }
                }
            } else {
                null
            },
            expression = exprLook,
            onExpression = if (tab == 0) ({ store.openExpression("Opacidade", OpacityKeys, 100f, "%") }) else null,
        )
        Column(Modifier.weight(1f).fillMaxHeight()) {
            ParamTabs(
                labels = listOf("Opacidade", "Mistura", "Máscara e recorte"),
                selected = tab,
                animated = { it == 0 && look != KeyframeLook.None },
                onSelect = { i ->
                    when (i) {
                        2 -> env.onOpenPanel(EditorPanel.Mask)
                        else -> tab = i
                    }
                },
            )
            Box(Modifier.weight(1f).fillMaxWidth()) {
                if (tab == 0) OpacityTab(env, look) else BlendTab(env)
            }
        }
    }
}

/** Opacidade: a linha [A] e, embaixo, uma régua grande (as duas mexem no mesmo valor). */
@Composable
private fun OpacityTab(env: PanelEnv, look: KeyframeLook) {
    val store = env.store
    val opacity by remember(store) { derivedStateOf { (store.detail?.opacity ?: 1f) * 100f } }
    Column(Modifier.fillMaxSize().padding(start = 2.dp, top = 6.dp, end = 10.dp, bottom = 10.dp)) {
        OpacityRow(env, opacity, selected = true, keyframe = look)
        Spacer(Modifier.height(6.dp))
        TickRuler(
            value = { opacity },
            unitsPerDp = 0.35f,
            active = true,
            modifier = Modifier
                .weight(1f)
                .fillMaxWidth()
                .valueDrag(
                    enabled = true,
                    start = { (store.detail?.opacity ?: 1f) * 100f },
                    unitsPerDp = { 0.35f },
                    min = 0f,
                    max = 100f,
                    onStart = { store.beginGesture("opacidade") },
                    onValue = { store.setTransform(TrackProperty.OPACITY, it / 100f) },
                    onEnd = { store.endGesture() },
                ),
        )
    }
}

/**
 * A MESCLAGEM EM CATEGORIAS [A]: cada cabeçalho diz qual modo está ligado ali
 * dentro; a categoria do modo atual nasce aberta. Chip com a miniatura de dois
 * discos (o de cima composto com o modo — vê-se o que o modo faz antes de tocar).
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun BlendTab(env: PanelEnv) {
    val store = env.store
    val mode by remember(store) { derivedStateOf { store.detail?.blendMode ?: 0 } }
    val currentCategory = BlendCategories.firstOrNull { c -> c.modes.any { it.engine == mode } }
    var open by remember { mutableStateOf(setOfNotNull(currentCategory?.name)) }
    LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(start = 8.dp, top = 4.dp, end = 12.dp, bottom = 12.dp)) {
        BlendCategories.forEach { cat ->
            item(key = cat.name) {
                val isOpen = cat.name in open
                Column {
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .height(40.dp)
                            .tocavel(shrink = 1f) { open = if (isOpen) open - cat.name else open + cat.name },
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        CupertinoIcon(if (isOpen) CupertinoGlyph.ChevronDown else CupertinoGlyph.ChevronRight, 13.dp, AureaColors.Muted)
                        Spacer(Modifier.width(8.dp))
                        Text(cat.name, style = AureaType.Base.merge(TextStyle(fontSize = 13.5.sp, fontWeight = FontWeight.W600)))
                        Spacer(Modifier.weight(1f))
                        val on = cat.modes.firstOrNull { it.engine == mode }
                        if (on != null) {
                            Text(on.label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Accent)))
                            Spacer(Modifier.width(6.dp))
                            CupertinoIcon(CupertinoGlyph.CheckmarkCircleFill, 16.dp, AureaColors.Accent)
                        }
                    }
                    if (isOpen) {
                        FlowRow(
                            Modifier.padding(bottom = 8.dp),
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            verticalArrangement = Arrangement.spacedBy(8.dp),
                        ) {
                            cat.modes.forEach { m ->
                                BlendChip(m, lit = m.engine != null && m.engine == mode) {
                                    val e = m.engine
                                    if (e != null && e in RenderedBlendModes) store.setBlendMode(e)
                                }
                            }
                        }
                    }
                }
            }
        }
        item(key = "ajuda") {
            Text(
                "A mistura combina esta camada com as camadas abaixo. Branco em Clarear cobre a imagem; em Escurecer deixa a imagem aparecer. Ajuste também a opacidade para reduzir a intensidade.",
                maxLines = 3,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.padding(top = 4.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, lineHeight = 14.3.sp, color = AureaColors.Muted)),
            )
        }
    }
}

/** Um chip de mescla [A] (`_BlendChip`): 74 de largura, miniatura 34 × 22, rótulo 9,5 sp. */
@Composable
private fun BlendChip(m: BlendChoice, lit: Boolean, onClick: () -> Unit) {
    Column(
        Modifier
            .width(74.dp)
            .clip(RoundedCornerShape(10.dp))
            .background(AureaColors.Chip)
            .then(if (lit) Modifier.border(2.dp, AureaColors.Accent, RoundedCornerShape(10.dp)) else Modifier)
            .tocavel(onClick = onClick)
            .padding(vertical = 8.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        // Camada própria (offscreen): o modo do disco de cima compõe só com o de baixo.
        Canvas(Modifier.size(34.dp, 22.dp).graphicsLayer { compositingStrategy = CompositingStrategy.Offscreen }) {
            val r = size.height * 0.46f
            drawCircle(AureaColors.BlendThumbBottom, r, Offset(size.width * 0.38f, size.height / 2f))
            drawCircle(AureaColors.BlendThumbTop, r, Offset(size.width * 0.62f, size.height / 2f), blendMode = m.preview)
        }
        Spacer(Modifier.height(4.dp))
        Text(
            m.label,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.padding(horizontal = 4.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 9.5.sp, color = if (lit) AureaColors.Accent else AureaColors.Text)),
        )
    }
}
