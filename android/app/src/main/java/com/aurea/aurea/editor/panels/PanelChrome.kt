package com.aurea.aurea.editor.panels

import com.aurea.aurea.engine.ExpressionLook
import com.aurea.aurea.ui.ds.expressionColor
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ChevronLeft
import androidx.compose.material.icons.rounded.ChevronLeft
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Stable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.clip
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
import com.aurea.aurea.ui.theme.tocavel

/**
 * Pedido do seletor de cor: cor de partida (RGBA 0..1), mudança viva e fim.
 * [finish] chama o fim UMA vez — pelo "Pronto" ou porque o painel saiu da tela
 * com a folha aberta (o passo de desfazer aberto na abertura tem de fechar).
 */
internal class ColorRequest(
    val initial: FloatArray,
    val onChange: (r: Float, g: Float, b: Float, a: Float) -> Unit,
    private val onDone: () -> Unit,
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
)

/**
 * O CABEÇALHO [A] do painel (ContextSheet): borda superior 1 dp `#273442` e faixa
 * de 44 dp `#151C24` com `‹` (IconButton 48, chevron 26) e o título 14 sp w600.
 */
@Composable
internal fun PanelHeader(title: String, onBack: () -> Unit) {
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
                    .semantics { contentDescription = "Voltar às ferramentas da camada" }
                    .tocavel(onClick = onBack),
                contentAlignment = Alignment.Center,
            ) {
                Icon(Icons.Filled.ChevronLeft, contentDescription = null, tint = AureaColors.Text, modifier = Modifier.size(26.dp))
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
 * O TRILHO ESQUERDO [A] (`RailEsquerdo`, 46 dp): `‹` voltar · ◇ keyframe · curva
 * (e o `⋯` quando há). Células de alturas iguais, sempre nesta ordem — a mão
 * aprende posição antes de ícone. O losango e a curva ficam apagados quando não
 * há o que marcar ou curvar.
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
    Column(modifier.width(46.dp).fillMaxHeight()) {
        RailCell(onBack, stringResource(R.string.panel_voltar_ferramentas)) {
            Icon(Icons.Rounded.ChevronLeft, contentDescription = null, tint = AureaColors.Text, modifier = Modifier.size(24.dp))
        }
        RailCell(onKeyframe, if (keyframeLook == KeyframeLook.KeyHere) stringResource(R.string.panel_tirar_keyframe_daqui) else stringResource(R.string.panel_marcar_keyframe_aqui)) {
            KeyframeDiamondIcon(keyframeLook, enabled = onKeyframe != null)
        }
        RailCell(onCurve, stringResource(R.string.panel_editar_curva_propriedade)) {
            CurveRailIcon(enabled = onCurve != null, animated = curveAnimated)
        }
        if (onExpression != null) {
            // "=": o editor de expressão da propriedade (acende quando há uma).
            RailCell(onExpression, if (expression == ExpressionLook.None) stringResource(R.string.panel_adicionar_expressao) else stringResource(R.string.panel_editar_expressao)) {
                androidx.compose.material3.Text(
                    "=",
                    style = com.aurea.aurea.ui.theme.AureaType.Base.merge(
                        androidx.compose.ui.text.TextStyle(
                            fontSize = 22.sp,
                            fontWeight = androidx.compose.ui.text.font.FontWeight.W700,
                            color = if (expression == ExpressionLook.None) AureaColors.Text else expressionColor(expression),
                        ),
                    ),
                )
            }
        }
        if (more != null) {
            Box(Modifier.weight(1f).fillMaxWidth(), contentAlignment = Alignment.Center) { more() }
        }
    }
}

@Composable
private fun androidx.compose.foundation.layout.ColumnScope.RailCell(
    onClick: (() -> Unit)?,
    label: String,
    content: @Composable () -> Unit,
) {
    Box(
        Modifier
            .weight(1f)
            .fillMaxWidth()
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
        CupertinoIcon(CupertinoGlyph.Ellipsis, 24.dp, if (active) AureaColors.Accent else AureaColors.Text)
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
    BoxWithConstraints(modifier.width(40.dp).fillMaxHeight()) {
        val cell: Dp = if (modes.isEmpty()) 36.dp else min(36.dp, maxHeight / modes.size)
        Column(
            Modifier.fillMaxHeight().fillMaxWidth(),
            verticalArrangement = Arrangement.SpaceEvenly,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            modes.forEachIndexed { i, m ->
                val on = i == selected
                Box(
                    Modifier
                        .size(cell)
                        .clip(RoundedCornerShape(8.dp))
                        .background(if (on) AureaColors.RailModeFill else Color.Transparent)
                        .then(if (on) Modifier.border(1.5.dp, AureaColors.Accent, RoundedCornerShape(8.dp)) else Modifier)
                        .semantics { contentDescription = m.label }
                        .tocavel(shrink = 1f) { onSelect(i) },
                    contentAlignment = Alignment.Center,
                ) {
                    Icon(m.icon, contentDescription = null, tint = if (on) AureaColors.Accent else AureaColors.Muted, modifier = Modifier.size(min(20.dp, cell * 0.6f)))
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
            .height(48.dp)
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
                            fontSize = 12.5.sp,
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
