package com.aurea.aurea.editor.panels

import com.aurea.aurea.ui.i18n.mirrorInRtl
import com.aurea.aurea.engine.ExpressionLook
import com.aurea.aurea.ui.ds.expressionColor
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Stable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import com.aurea.aurea.ui.ds.ParamRowColors
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.min
import androidx.compose.ui.unit.sp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.CurveRailIcon
import com.aurea.aurea.ui.ds.KeyframeDiamondIcon
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
// Omega tema (0.3.3): vetores do Material com o mesmo peso dos glifos.
import com.aurea.aurea.ui.theme.AureaLineIcons
import com.aurea.aurea.ui.theme.tocavel
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.selected
import com.aurea.aurea.editor.ShellDims
import com.aurea.aurea.ui.ds.AnchoredMenu
import com.aurea.aurea.ui.ds.AureaAlert
import com.aurea.aurea.ui.ds.MenuEntry

/**
 * Pedido do seletor de cor: cor de partida (RGBA 0..1), mudança viva e fim.
 * [finish] chama o fim UMA vez — pelo "Pronto" ou porque o painel saiu da tela
 * com a folha aberta (o passo de desfazer aberto na abertura tem de fechar).
 */
internal class ColorRequest(
    val initial: FloatArray,
    val onChange: (r: Float, g: Float, b: Float, a: Float) -> Unit,
    private val onDone: () -> Unit,
    /** Omega aparência (P0-5): retângulo da amostra tocada (px da janela). Com ele a cor abre no
     *  balão ancorado (seletor embutido); sem ele, no seletor em tela cheia. */
    val anchor: androidx.compose.ui.geometry.Rect? = null,
    /** Omega aparência: o campo aceita alfa (amostra com alfa num campo sem alfa fica indisponível). */
    val withAlpha: Boolean = true,
) {
    private var finished = false

    fun finish() {
        if (finished) return
        finished = true
        onDone()
    }
}

/**
 * O que todo painel recebe da moldura: o store, a navegação da casca e as duas
 * folhas compartilhadas (teclado e cor), que moram UMA vez em [PanelContent].
 * Estável (uma instância por store): passar isto não força recomposição.
 */
@Stable
internal class PanelEnv(
    val store: EditorStore,
    val onClose: () -> Unit,
    val onOpenPanel: (EditorPanel) -> Unit,
    val onOpenEffectsBrowser: () -> Unit,
    val openKeypad: (KeypadRequest) -> Unit,
    val openColor: (ColorRequest) -> Unit,
    /** Painel de onde o editor de curva foi aberto (o ‹ do trilho volta para ele). */
    val returnTo: () -> EditorPanel?,
    val onOpenAnimators: () -> Unit = { onOpenPanel(EditorPanel.Transform) },
)

/**
 * O CABEÇALHO [A] do painel (ContextSheet): borda superior 1 dp `#273442` e faixa
 * de 44 dp `#151C24` com `‹` (IconButton 48, chevron 26) e o título 14 sp w600.
 */
@Composable
internal fun PanelHeader(title: String, onBack: () -> Unit) {
    val backDesc = stringResource(R.string.pn_back_to_layer_tools)
    Column(Modifier.fillMaxWidth()) {
        Box(Modifier.fillMaxWidth().height(1.dp).background(AureaColors.Border))
        Row(
            Modifier
                .fillMaxWidth()
                .height(44.dp)
                .background(AureaColors.Surface),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(
                Modifier
                    .size(48.dp, 44.dp)
                    .semantics { contentDescription = backDesc }
                    .tocavel(onClick = onBack),
                contentAlignment = Alignment.Center,
            ) {
                Icon(AureaLineIcons.ChevronLeft, contentDescription = null, tint = AureaColors.Text, modifier = Modifier.size(26.dp).mirrorInRtl())
            }
            Text(
                title,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.weight(1f).padding(end = 12.dp),
                style = AureaType.EditorTitle,
            )
        }
    }
}

/**
 * O TRILHO ESQUERDO (redesenho 2026-09-29, Efeitos.dc.html): 58 dp com a
 * divisa fina #222B3B à direita; botões de 44 empilhados do topo (vão 6):
 * `‹` voltar · ◇+ keyframe · curva (e o `=` / `⋯` quando há). Sempre nesta
 * ordem — a mão aprende posição antes de ícone. O losango e a curva ficam
 * apagados quando não há o que marcar ou curvar. O `more` mora no pé.
 */
@Composable
internal fun LeftRail(
    onBack: () -> Unit,
    keyframeLook: KeyframeLook,
    onKeyframe: (() -> Unit)?,
    curveAnimated: Boolean,
    onCurve: (() -> Unit)?,
    modifier: Modifier = Modifier,
    more: (@Composable () -> Unit)? = null,
    expression: ExpressionLook = ExpressionLook.None,
    onExpression: (() -> Unit)? = null,
) {
    Column(
        modifier
            .width(58.dp)
            .fillMaxHeight()
            .drawBehind {
                // A divisa fica do lado do conteúdo: à direita em LTR, à esquerda em RTL.
                val w = 1.dp.toPx()
                val x = if (layoutDirection == androidx.compose.ui.unit.LayoutDirection.Rtl) 0f else size.width - w
                drawRect(ParamRowColors.RailLine, topLeft = Offset(x, 0f), size = Size(w, size.height))
            }
            .padding(top = 8.dp, bottom = 4.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        RailCell(onBack, stringResource(R.string.panel_voltar_ferramentas)) {
            Icon(AureaLineIcons.ChevronLeft, contentDescription = null, tint = AureaColors.Text, modifier = Modifier.size(PanelMetrics.RailIcon).mirrorInRtl())
        }
        RailCell(onKeyframe, if (keyframeLook == KeyframeLook.KeyHere) stringResource(R.string.panel_tirar_keyframe_daqui) else stringResource(R.string.panel_marcar_keyframe_aqui)) {
            KeyframeDiamondIcon(keyframeLook, enabled = onKeyframe != null, modifier = Modifier.size(PanelMetrics.RailIcon))
        }
        RailCell(onCurve, stringResource(R.string.panel_editar_curva_propriedade)) {
            CurveRailIcon(enabled = onCurve != null, animated = curveAnimated, modifier = Modifier.size(PanelMetrics.RailIcon))
        }
        if (onExpression != null) {
            // "=": o editor de expressão da propriedade (acende quando há uma).
            RailCell(onExpression, if (expression == ExpressionLook.None) stringResource(R.string.panel_adicionar_expressao) else stringResource(R.string.panel_editar_expressao)) {
                androidx.compose.material3.Text(
                    "=",
                    style = com.aurea.aurea.ui.theme.AureaType.Base.merge(
                        androidx.compose.ui.text.TextStyle(
                            fontSize = PanelMetrics.RailExpression,
                            fontWeight = androidx.compose.ui.text.font.FontWeight.W700,
                            color = if (expression == ExpressionLook.None) AureaColors.Text else expressionColor(expression),
                        ),
                    ),
                )
            }
        }
        if (more != null) {
            Spacer(Modifier.weight(1f))
            Box(Modifier.size(44.dp), contentAlignment = Alignment.Center) { more() }
        }
    }
}

@Composable
private fun RailCell(
    onClick: (() -> Unit)?,
    label: String,
    content: @Composable () -> Unit,
) {
    Box(
        Modifier
            .size(44.dp)
            .semantics { contentDescription = label }
            .then(if (onClick != null) Modifier.tocavel(shrink = 1f, onClick = onClick) else Modifier),
        contentAlignment = Alignment.Center,
    ) { content() }
}

/**
 * O `⋯` DO TRILHO que não esconde um modo (`AmMenuIcon`): aceso e com um ponto
 * quando há modo ligado lá dentro.
 */
@Composable
internal fun RailMoreButton(active: Boolean, onClick: () -> Unit) {
    Box(Modifier.size(44.dp).tocavel(onClick = onClick), contentAlignment = Alignment.Center) {
        CupertinoIcon(CupertinoGlyph.Ellipsis, PanelMetrics.RailIcon, if (active) AureaColors.Accent else AureaColors.Text)
        if (active) {
            Box(
                Modifier
                    .align(Alignment.Center)
                    .padding(start = 26.dp, bottom = 16.dp)
                    .size(6.dp)
                    .background(AureaColors.Accent, CircleShape),
            )
        }
    }
}

/** Um modo do trilho direito: ícone e rótulo de acessibilidade. */
internal class RailMode(val icon: ImageVector, val label: String)

/**
 * O TRILHO DIREITO [A] (`RailDireito`, 40 dp): os modos empilhados em
 * `spaceEvenly`; o vigente com fundo `#1E222D`, borda 1,5 `destaque` e ícone
 * aceso. O botão encolhe quando a coluna não cabe (cinco modos num painel baixo),
 * em vez de rolar e esconder justamente o último modo.
 */
@Composable
internal fun RightRail(modes: List<RailMode>, selected: Int, onSelect: (Int) -> Unit, modifier: Modifier = Modifier) {
    BoxWithConstraints(modifier.width(48.dp).fillMaxHeight()) {
        val cell = 44.dp
        Column(
            Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).heightIn(min = maxHeight),
            verticalArrangement = Arrangement.SpaceEvenly,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            modes.forEachIndexed { i, m ->
                val on = i == selected
                Box(
                    Modifier
                        .padding(vertical = 2.dp).size(cell)
                        .clip(RoundedCornerShape(8.dp))
                        .background(if (on) AureaColors.RailModeFill else Color.Transparent)
                        .then(if (on) Modifier.border(1.5.dp, AureaColors.Accent, RoundedCornerShape(8.dp)) else Modifier)
                        .semantics { contentDescription = m.label }
                        .tocavel(shrink = 1f) { onSelect(i) },
                    contentAlignment = Alignment.Center,
                ) {
                    Icon(m.icon, contentDescription = null, tint = if (on) AureaColors.Accent else AureaColors.Muted, modifier = Modifier.size(min(24.dp, cell * 0.6f)))
                }
            }
        }
    }
}

/**
 * AS ABAS DE PARÂMETRO [A] (`AmParamTabs`): 48 de altura; abas que cabem dividem a
 * linha por igual; chip de 36, raio 9, `campo` (acesa `destaqueApagado`), texto
 * 12,5 sp (acesa w700 `destaque`), ponto de 5 quando a propriedade anima.
 */
@Composable
internal fun ParamTabs(
    labels: List<String>,
    selected: Int,
    onSelect: (Int) -> Unit,
    animated: (Int) -> Boolean = { false },
) {
    Row(
        Modifier
            .fillMaxWidth()
            .height(PanelMetrics.ParamTabsHeight)
            .padding(horizontal = 8.dp, vertical = 6.dp),
    ) {
        labels.forEachIndexed { i, l ->
            val on = i == selected
            Row(
                Modifier
                    .weight(1f)
                    .fillMaxHeight()
                    .padding(horizontal = 3.dp)
                    .clip(RoundedCornerShape(9.dp))
                    .background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                    .tocavel(shrink = 1f) { onSelect(i) }
                    .padding(horizontal = 8.dp),
                horizontalArrangement = Arrangement.Center,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    l,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f, fill = false),
                    style = AureaType.Base.merge(
                        TextStyle(
                            fontSize = PanelMetrics.ParamTabsText,
                            fontWeight = if (on) FontWeight.W700 else FontWeight.W500,
                            color = if (on) AureaColors.Accent else AureaColors.Text,
                        ),
                    ),
                )
                if (animated(i)) {
                    Spacer(Modifier.width(5.dp))
                    Box(Modifier.size(5.dp).background(AureaColors.Accent, CircleShape))
                }
            }
        }
    }
}

/** Aviso de painel sem controle (texto 12,5 sp muted, várias linhas). */
@Composable
internal fun PanelNotice(text: String, modifier: Modifier = Modifier) {
    Text(text, modifier = modifier.padding(vertical = 10.dp), style = AureaType.Property)
}

// =============================================================================
// Omega fundação · F5: MOLDURA COMUM DOS PAINÉIS DE PROPRIEDADE
// (`fragment_settings_base` + `fragment_setting_actionbar` da referência)
//
//   ┌60──┬────────── conteúdo (rola por dentro) ──────────┬60──┐  200 dp
//   │ ‹  │                                                │ ab1│  as células de
//   │ ◆± │                                                │ ab2│  cada coluna
//   │ ∿  │                                                │ ab3│  repartem a
//   │ ⋯  │                                                │ ab4│  altura
//   └────┴0,7─────────────────────────────────────────0,7┴────┘
//
// Coluna esquerda: ‹ Voltar · ◆± keyframe · Curva · (= expressão, extra do
// AUREA) · ⋯. Segurar ◆ com keyframe no quadro pede "apagar todos os keyframes"
// (confirmação); Curva com menos de 2 keyframes mostra o aviso. Coluna direita:
// até 4 abas de ícone (só com 2 ou mais; a aba com `alt` troca para a irmã no
// segundo toque). Em RTL a coluna esquerda vai para a direita (a Row espelha).
// Par de `PanelFrame`/`PanelBar` do DesignSystem.swift (mesmos ids).
//
//   PanelFrame(
//       rail = PanelRail(
//           onBack = env.onClose,
//           keyframeLook = look,
//           onKeyframe = { store.toggleTransformKeyframe(keys) },
//           onDeleteAllKeyframes = { /* um passo de desfazer */ },
//           onCurve = { env.onOpenPanel(EditorPanel.Curve) },
//           curveReady = curveReady,
//       ),
//       tabs = listOf(
//           PanelTab("move", label, glyph = CupertinoGlyph.Move, alt = PanelTab("pivot", label2, glyph = …)),
//           PanelTab("scale", label3, glyph = …),
//       ),
//       selectedTab = tab, onTab = { tab = it },
//   ) { /* conteúdo da aba */ }
// =============================================================================

/** Medidas da moldura (as da referência). */
internal object PanelFrameDefaults {
    val Height = 200.dp
    val RailWidth = PanelMetrics.RailWidth
    val Divider = 0.7.dp
    val TabInset = PanelMetrics.TabInset
    val TabRadius = PanelMetrics.TabRadius
    const val DISABLED_ALPHA = 0.15f
    const val MAX_TABS = 4
}

/**
 * 0.3.4 painéis: AS MEDIDAS CENTRAIS dos painéis de propriedade (moldura e
 * campos), ~25 % maiores que as da Omega ("está muito compacto"). Todo painel
 * (Transformar, Câmera, Cor e preenchimento, Borda e sombra, Mesclagem, Efeitos,
 * Velocidade/Som, Curva) lê daqui em vez de número solto. Par de
 * `PanelMetrics` do DesignSystem.swift: os valores são os mesmos (dp = pt, sp = pt).
 *
 * Largura: três campos de [FieldWidth] + o fio da pílula + [FieldGap] cabem
 * na coluna central de um telefone de 360 dp (360 − 2 × 60 − 2 × 0,7 − 12 = 226).
 * O número do campo encolhe (até [FieldValueMin]) antes de vazar, com fonte grande.
 */
internal object PanelMetrics {
    // --- Moldura -------------------------------------------------------------
    val RailWidth = 60.dp
    /** Ícones das colunas (‹, ◆, curva, ⋯ e as abas): antes 22. */
    val RailIcon = 26.dp
    /** O "=" da expressão na coluna esquerda (sp). */
    val RailExpression = 26.sp
    val TabInset = 5.dp
    val TabRadius = 7.dp
    /** Alvo de toque mínimo de qualquer controle do painel. */
    val Touch = 44.dp

    // --- Campos de valor (X/Y/Z, Largura/Altura, Inclinação, ângulos) ----------
    val FieldWidth = 72.dp
    val FieldHeight = 36.dp
    val FieldRadius = 7.dp
    val FieldGap = 8.dp
    /** Número do campo (antes 10). */
    val FieldValue = 15.sp
    val FieldValueMin = 9.sp
    /** Rótulo embaixo do campo (antes 9). */
    val FieldLabel = 12.sp
    val FieldLabelGap = 3.dp
    /** Respiro em cima da fileira de campos (antes 6/15). */
    val FieldRowTop = 12.dp
    /** A fileira de campos inteira: respiro + campo + vão + rótulo (≈ 17). */
    val FieldBlock = 68.dp

    // --- Textos ---------------------------------------------------------------
    /** Título de cartão/seção dentro do painel (antes 13–13,5). */
    val Title = 15.sp
    /** Rótulo de linha / título de controle (antes 10–11). */
    val Label = 13.sp
    /** Texto secundário e notas (antes 10–11,5). */
    val Note = 12.sp
    /** A dica do trackpad e de áreas de gesto (antes 12). */
    val Hint = 14.sp
    /** Botões pequenos dentro do painel ("Centro", "Preciso", chips). */
    val ChipHeight = 36.dp
    val ChipText = 14.sp
    val ChipPad = 14.dp

    // --- Réguas ---------------------------------------------------------------
    /**
     * Régua de um par, no painel de 200 (antes 52). Com o painel mais alto as réguas
     * crescem até [RulerMax] ([fillRulers]); 68 + 8 + 56 + 8 + 56 + 4 = 200.
     */
    val Ruler = 56.dp
    /** Régua única, no painel de 200 (antes 72). */
    val RulerTall = 80.dp
    /** Teto ao crescer com o painel alto (as réguas repartem a sobra até aqui). */
    val RulerMax = 128.dp
    val RulerGap = 8.dp
    /** Respiro embaixo da última régua. */
    val RulerBottom = 4.dp

    /** A altura que sobra para as réguas abaixo de uma fileira de campos, num conteúdo de [height]. */
    fun rulerSpace(height: Dp): Dp = height - FieldBlock - RulerGap - RulerBottom

    // --- Abas de parâmetro (ParamTabs) -----------------------------------------
    val ParamTabsHeight = 54.dp
    val ParamTabsText = 14.sp

    /**
     * Altura de cada uma de [count] réguas para encher [available] (a altura que
     * sobra no painel), entre [min] e [RulerMax]. Com o painel alto as réguas
     * crescem em vez de tudo ficar espremido no topo; num painel baixo ficam em [min]
     * e o conteúdo rola.
     */
    fun fillRulers(available: Dp, count: Int, min: Dp): Dp {
        if (count <= 0) return min
        val gaps = RulerGap * (count - 1)
        val each = (available - gaps) / count.toFloat()
        return each.coerceIn(min, maxOf(min, RulerMax))
    }
}

/**
 * Uma aba de ícone da coluna direita. [id] é o que [PanelFrame] devolve em
 * `onTab`. [alt]: a aba irmã — tocar de novo na aba ativa troca para ela (outro
 * id e ícone) e o toque seguinte volta. Ícone: [glyph] (CupertinoGlyph) ou [icon].
 */
@Immutable
internal class PanelTab(
    val id: String,
    val label: String,
    val glyph: Char? = null,
    val icon: ImageVector? = null,
    val alt: PanelTab? = null,
)

/**
 * O que a coluna esquerda faz.
 * - [onKeyframe] nulo = o painel não tem parâmetro com keyframe: ◆ e Curva ficam a 15 %, sem toque.
 * - [onDeleteAllKeyframes]: segurar ◆ com keyframe no quadro ([keyframeLook] = KeyHere) pede
 *   confirmação e chama isto (o painel apaga as marcas da aba num passo de desfazer).
 * - [onCurve] + [curveReady]: com menos de 2 keyframes ([curveReady] falso) o toque mostra o aviso.
 * - [onExpression]: a célula "=" (extra do AUREA), só quando não nulo.
 * - [moreEntries] (menu ancorado) ou [onMore]: o ⋯; os dois nulos = sem ⋯.
 * - [supportsAnimation] falso: painel sem animação — só o ‹.
 */
@Immutable
internal class PanelRail(
    val onBack: () -> Unit,
    val keyframeLook: KeyframeLook = KeyframeLook.None,
    val onKeyframe: (() -> Unit)? = null,
    val onDeleteAllKeyframes: (() -> Unit)? = null,
    val onCurve: (() -> Unit)? = null,
    val curveReady: Boolean = false,
    val expression: ExpressionLook = ExpressionLook.None,
    val onExpression: (() -> Unit)? = null,
    val moreEntries: List<MenuEntry>? = null,
    val onMore: (() -> Unit)? = null,
    val moreActive: Boolean = false,
    val supportsAnimation: Boolean = true,
    // Omega efeitos (área 07): o ⋯ a 15 % e sem toque (o ⋯ da lista de efeitos
    // fica inativo com os controles de um efeito abertos, como na referência).
    val moreEnabled: Boolean = true,
)

/**
 * A moldura comum dos painéis de propriedade (200 dp; quem hospeda dá a altura).
 *
 * @param rail a coluna esquerda ([PanelRail]).
 * @param tabs as abas de ícone (no máximo 4); com 0 ou 1 a coluna direita não existe.
 * @param selectedTab o [PanelTab.id] ativo (ou o id da `alt` dele).
 * @param onTab troca de aba (já resolve o segundo toque para a `alt`).
 * @param scrollContent o conteúdo rola por dentro (padrão). Falso para quem tem lista preguiçosa.
 */
@Composable
internal fun PanelFrame(
    rail: PanelRail,
    modifier: Modifier = Modifier,
    tabs: List<PanelTab> = emptyList(),
    selectedTab: String? = null,
    onTab: (String) -> Unit = {},
    scrollContent: Boolean = true,
    content: @Composable ColumnScope.() -> Unit,
) {
    var askDeleteAll by remember { mutableStateOf(false) }
    var curveWarning by remember { mutableStateOf(false) }
    Row(
        modifier
            .fillMaxSize()
            .background(AureaColors.PanelFrameBackground)
            .testTag("panel.frame"),
    ) {
        FrameLeftRail(rail, onAskDeleteAll = { askDeleteAll = true }, onCurveWarning = { curveWarning = true })
        FrameDivider()
        Column(
            Modifier
                .weight(1f)
                .fillMaxHeight()
                .background(AureaColors.PanelFrameContent)
                .testTag("panel.content")
                .then(if (scrollContent) Modifier.verticalScroll(rememberScrollState()) else Modifier),
            content = content,
        )
        if (tabs.size >= 2) {
            FrameDivider()
            FrameRightRail(tabs.take(PanelFrameDefaults.MAX_TABS), selectedTab, onTab)
        }
    }
    if (askDeleteAll) {
        AureaAlert(
            title = stringResource(R.string.om_panel_delete_all_keyframes_q),
            confirmLabel = stringResource(R.string.panel_apagar),
            destructive = true,
            onConfirm = { rail.onDeleteAllKeyframes?.invoke() },
            onDismiss = { askDeleteAll = false },
        )
    }
    if (curveWarning) {
        AureaAlert(
            title = stringResource(R.string.om_panel_keyframes_needed),
            message = stringResource(R.string.pn_curve_need_two_keys),
            showCancel = false,
            onConfirm = {},
            onDismiss = { curveWarning = false },
        )
    }
}

@Composable
private fun FrameDivider() {
    Box(Modifier.width(PanelFrameDefaults.Divider).fillMaxHeight().background(AureaColors.PanelFrameDivider))
}

@Composable
private fun FrameLeftRail(rail: PanelRail, onAskDeleteAll: () -> Unit, onCurveWarning: () -> Unit) {
    var moreOpen by remember { mutableStateOf(false) }
    Column(Modifier.width(PanelFrameDefaults.RailWidth).fillMaxHeight()) {
        FrameCell("panel.rail.back", stringResource(R.string.panel_voltar_ferramentas), enabled = true, onClick = rail.onBack) {
            CupertinoIcon(CupertinoGlyph.ChevronLeft, PanelMetrics.RailIcon, AureaColors.Text)
        }
        if (!rail.supportsAnimation) {
            Spacer(Modifier.weight(3f))
            return@Column
        }
        FrameCell(
            "panel.rail.keyframe",
            stringResource(if (rail.keyframeLook == KeyframeLook.KeyHere) R.string.panel_tirar_keyframe_daqui else R.string.panel_marcar_keyframe_aqui),
            enabled = rail.onKeyframe != null,
            onClick = rail.onKeyframe,
            onLongClick = if (rail.keyframeLook == KeyframeLook.KeyHere && rail.onDeleteAllKeyframes != null) onAskDeleteAll else null,
        ) {
            KeyframeDiamondIcon(rail.keyframeLook, enabled = true, modifier = Modifier.size(PanelMetrics.RailIcon))
        }
        val curve = rail.onCurve
        FrameCell(
            "panel.rail.curve",
            stringResource(R.string.panel_editar_curva_propriedade),
            enabled = curve != null,
            onClick = if (curve != null) ({ if (rail.curveReady) curve() else onCurveWarning() }) else null,
        ) {
            CurveRailIcon(enabled = true, animated = rail.keyframeLook != KeyframeLook.None, modifier = Modifier.size(PanelMetrics.RailIcon))
        }
        val onExpression = rail.onExpression
        if (onExpression != null) {
            FrameCell(
                "panel.rail.expression",
                stringResource(if (rail.expression == ExpressionLook.None) R.string.panel_adicionar_expressao else R.string.panel_editar_expressao),
                enabled = true,
                onClick = onExpression,
            ) {
                Text(
                    "=",
                    style = AureaType.Base.merge(
                        TextStyle(
                            fontSize = PanelMetrics.RailExpression,
                            fontWeight = FontWeight.W700,
                            color = if (rail.expression == ExpressionLook.None) AureaColors.Text else expressionColor(rail.expression),
                        ),
                    ),
                )
            }
        }
        val entries = rail.moreEntries
        val onMore = rail.onMore
        if (entries != null || onMore != null) {
            Box(Modifier.weight(1f).fillMaxWidth()) {
                FrameCellBody(
                    "panel.rail.more",
                    stringResource(R.string.editor_mais),
                    enabled = rail.moreEnabled,   // Omega efeitos (área 07)
                    onClick = { if (entries != null) moreOpen = true else onMore?.invoke() },
                ) {
                    CupertinoIcon(CupertinoGlyph.Ellipsis, PanelMetrics.RailIcon, if (rail.moreActive) AureaColors.Accent else AureaColors.Text)
                }
                if (entries != null) {
                    AnchoredMenu(
                        expanded = moreOpen,
                        onDismiss = { moreOpen = false },
                        entries = entries,
                        tag = "panel.rail.more.menu",
                    )
                }
            }
        } else {
            Spacer(Modifier.weight(1f))
        }
    }
}

/** Uma célula da coluna esquerda: reparte a altura com as outras. */
@Composable
private fun ColumnScope.FrameCell(
    tag: String,
    label: String,
    enabled: Boolean,
    onClick: (() -> Unit)?,
    onLongClick: (() -> Unit)? = null,
    content: @Composable () -> Unit,
) {
    Box(Modifier.weight(1f).fillMaxWidth()) {
        FrameCellBody(tag, label, enabled, onClick, onLongClick, content)
    }
}

@Composable
private fun FrameCellBody(
    tag: String,
    label: String,
    enabled: Boolean,
    onClick: (() -> Unit)?,
    onLongClick: (() -> Unit)? = null,
    content: @Composable () -> Unit,
) {
    Box(
        Modifier
            .fillMaxSize()
            .testTag(tag)
            .semantics {
                contentDescription = label
                if (!enabled) disabled()
            }
            .then(if (enabled && onClick != null) Modifier.tocavel(shrink = 1f, onLongClick = onLongClick, onClick = onClick) else Modifier)
            .alpha(if (enabled) 1f else PanelFrameDefaults.DISABLED_ALPHA),
        contentAlignment = Alignment.Center,
    ) { content() }
}

@Composable
private fun FrameRightRail(tabs: List<PanelTab>, selectedId: String?, onTab: (String) -> Unit) {
    Column(Modifier.width(PanelFrameDefaults.RailWidth).fillMaxHeight()) {
        tabs.forEachIndexed { i, tab ->
            val alt = tab.alt
            val altOn = alt != null && selectedId == alt.id
            val on = selectedId == tab.id || altOn
            val shown = if (altOn && alt != null) alt else tab
            val tint = if (on) AureaColors.Accent else AureaColors.Text
            Box(
                Modifier
                    .weight(1f)
                    .fillMaxWidth()
                    .testTag("panel.tab.$i")
                    .semantics {
                        contentDescription = shown.label
                        selected = on
                    }
                    .tocavel(shrink = 1f) {
                        if (alt != null && selectedId == tab.id) onTab(alt.id) else onTab(tab.id)
                    }
                    .padding(PanelFrameDefaults.TabInset)
                    .clip(RoundedCornerShape(PanelFrameDefaults.TabRadius))
                    .background(if (on) AureaColors.PanelFrameDivider else Color.Transparent),
                contentAlignment = Alignment.Center,
            ) {
                val glyph = shown.glyph
                val icon = shown.icon
                when {
                    glyph != null -> CupertinoIcon(glyph, PanelMetrics.RailIcon, tint)
                    icon != null -> Icon(icon, contentDescription = null, tint = tint, modifier = Modifier.size(PanelMetrics.RailIcon))
                }
            }
        }
    }
}

/**
 * A BARRA DO PAINEL DE PROPRIEDADE (P0-6 da área 04 / P0-6 da área 06): fundo
 * `PanelBar`, ‹ de 44 dp e o título À ESQUERDA logo depois, com reticências;
 * [trailing] à direita (ex.: o segmento ◆/• de Editar pontos) e um ⋮ opcional
 * ([moreEntries], menu ancorado). Altura = a da barra superior (42 dp).
 * Ids: `panel.bar.back`, `panel.bar.title`, `panel.bar.more` (+ o [tag] da barra).
 */
@Composable
internal fun PanelBar(
    title: String,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
    tag: String? = null,
    moreEntries: List<MenuEntry>? = null,
    trailing: @Composable RowScope.() -> Unit = {},
) {
    val back = stringResource(R.string.pn_back_to_layer_tools)
    var moreOpen by remember { mutableStateOf(false) }
    Row(
        modifier
            .fillMaxWidth()
            .height(ShellDims.TopBar)
            .background(AureaColors.PanelBar)
            .then(tag?.let { Modifier.testTag(it) } ?: Modifier),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            Modifier
                .size(44.dp)
                .testTag("panel.bar.back")
                .semantics { contentDescription = back }
                .tocavel(onClick = onBack),
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(CupertinoGlyph.ChevronLeft, 22.dp, AureaColors.Text)
        }
        Text(
            title,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.weight(1f).testTag("panel.bar.title").semantics { heading() },
            style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)),
        )
        trailing()
        if (moreEntries != null) {
            val more = stringResource(R.string.editor_mais)
            Box {
                Box(
                    Modifier
                        .size(44.dp)
                        .testTag("panel.bar.more")
                        .semantics { contentDescription = more }
                        .tocavel { moreOpen = true },
                    contentAlignment = Alignment.Center,
                ) {
                    CupertinoIcon(CupertinoGlyph.Ellipsis, 22.dp, AureaColors.Text)
                }
                AnchoredMenu(
                    expanded = moreOpen,
                    onDismiss = { moreOpen = false },
                    entries = moreEntries,
                    alignEnd = true,
                    tag = "panel.bar.more.menu",
                )
            }
        } else {
            Spacer(Modifier.width(6.dp))
        }
    }
}
