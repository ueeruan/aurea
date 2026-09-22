package com.aurea.aurea.ui.ds

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.core.MutableTransitionState
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.compose.ui.window.DialogWindowProvider
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaMotion

/**
 * Controle da folha de ajuste: [dismiss] toca a animação de saída e SÓ ENTÃO
 * avisa o dono (que tira a folha da árvore).
 */
class AdjustSheetState internal constructor(internal val visible: MutableTransitionState<Boolean>) {
    fun dismiss() {
        visible.targetState = false
    }
}

/**
 * A FOLHA DE AJUSTE (teclado numérico, seletor de cor): sobe de baixo, fundo
 * `painel`, topo 13,5, véu `palco` a 35 %, e NÃO FECHA ARRASTANDO — dentro dela
 * quase tudo se ajusta arrastando (bug B-14: o teclado fechava com o dedo). Toque
 * no véu ou voltar do sistema fecham.
 *
 * É um `Dialog` e não um `ModalBottomSheet` porque o M3 1.3 não desliga o arrasto
 * da folha sem desligar também o toque no véu.
 */
@Composable
fun AureaAdjustSheet(
    onDismiss: () -> Unit,
    topRadius: Dp = 13.5.dp,
    enterMs: Int = AureaMotion.NORMAL,
    content: @Composable (AdjustSheetState) -> Unit,
) {
    val visible = remember { MutableTransitionState(false).apply { targetState = true } }
    val state = remember { AdjustSheetState(visible) }
    // Saída concluída → o dono tira a folha.
    LaunchedEffect(visible.currentState, visible.targetState) {
        if (!visible.targetState && !visible.currentState && visible.isIdle) onDismiss()
    }
    Dialog(
        onDismissRequest = { state.dismiss() },
        properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false),
    ) {
        // O véu é nosso (cor do palco); o escurecimento padrão da janela sai.
        val window = (LocalView.current.parent as? DialogWindowProvider)?.window
        SideEffect { window?.setDimAmount(0f) }
        // UMA transição para véu e folha (um MutableTransitionState não pode
        // alimentar duas); cada peça anima a sua entrada/saída dentro dela.
        AnimatedVisibility(
            visibleState = visible,
            modifier = Modifier.fillMaxSize(),
            enter = EnterTransition.None,
            exit = ExitTransition.None,
        ) {
            Box(Modifier.fillMaxSize()) {
                Box(
                    Modifier
                        .fillMaxSize()
                        .animateEnterExit(enter = fadeIn(tween(enterMs)), exit = fadeOut(tween(AureaMotion.FAST)))
                        .background(AureaColors.SheetScrim)
                        .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null) { state.dismiss() },
                )
                Column(
                    Modifier
                        .align(Alignment.BottomCenter)
                        .animateEnterExit(
                            enter = slideInVertically(tween(enterMs, easing = AureaMotion.Enter)) { it },
                            exit = slideOutVertically(tween(AureaMotion.FAST, easing = AureaMotion.Exit)) { it },
                        )
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(topStart = topRadius, topEnd = topRadius))
                        .background(AureaColors.Surface)
                        // Engole o toque para não atravessar até o véu.
                        .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null) {}
                        .navigationBarsPadding(),
                ) {
                    content(state)
                }
            }
        }
    }
}
