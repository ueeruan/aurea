package com.aurea.aurea.ui.theme

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.LocalTextStyle
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.semantics.Role

/**
 * Tema da UI aprovada. Sem ripple em lugar nenhum (o app antigo desligava o
 * splash do Material): o feedback de toque é o `tocavel` — encolhe para
 * 0,965 e escurece para 0,82 enquanto o dedo está em cima.
 */
@Composable
fun AureaTheme(content: @Composable () -> Unit) {
    val scheme = darkColorScheme(
        primary = AureaColors.Accent,
        onPrimary = AureaColors.OnAccent,
        secondary = AureaColors.Keyframe,
        onSecondary = AureaColors.Text,
        error = AureaColors.Danger,
        onError = AureaColors.Text,
        background = AureaColors.Background,
        onBackground = AureaColors.Text,
        surface = AureaColors.Background,
        onSurface = AureaColors.Text,
        surfaceContainerHighest = AureaColors.SurfaceHigh,
        surfaceContainerHigh = AureaColors.SurfaceHigh,
        surfaceContainer = AureaColors.Surface,
        surfaceContainerLow = AureaColors.Surface,
        onSurfaceVariant = AureaColors.Muted,
        outline = AureaColors.Border,
        outlineVariant = AureaColors.Border,
    )
    MaterialTheme(colorScheme = scheme) {
        CompositionLocalProvider(LocalTextStyle provides AureaType.Base, content = content)
    }
}

/**
 * O "átomo de fluidez" do app antigo (`Tocavel`): encolhe e escurece enquanto
 * pressionado, sem ripple. Não muda o layout — só a pintura.
 *
 * @param shrink escala enquanto pressionado (1 = só escurece).
 * @param haptic toque leve no clique.
 */
@OptIn(ExperimentalFoundationApi::class)
fun Modifier.tocavel(
    enabled: Boolean = true,
    shrink: Float = AureaMotion.PRESS_SCALE,
    haptic: Boolean = false,
    role: Role? = Role.Button,
    onLongClick: (() -> Unit)? = null,
    onClick: () -> Unit,
): Modifier = composed {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    val hapticFeedback = LocalHapticFeedback.current
    val scale by animateFloatAsState(
        targetValue = if (pressed && enabled) shrink else 1f,
        animationSpec = tween(if (pressed) AureaMotion.PRESS_DOWN_MS else AureaMotion.PRESS_UP_MS),
        label = "tocavel-escala",
    )
    val alpha by animateFloatAsState(
        targetValue = if (pressed && enabled) AureaMotion.PRESS_ALPHA else 1f,
        animationSpec = tween(if (pressed) AureaMotion.PRESS_ALPHA_DOWN_MS else AureaMotion.PRESS_ALPHA_UP_MS),
        label = "tocavel-opacidade",
    )
    this
        .graphicsLayer {
            scaleX = scale
            scaleY = scale
            this.alpha = alpha
        }
        .combinedClickable(
            interactionSource = interaction,
            indication = null,
            enabled = enabled,
            role = role,
            onLongClick = onLongClick?.let { cb ->
                {
                    hapticFeedback.performHapticFeedback(HapticFeedbackType.LongPress)
                    cb()
                }
            },
            onClick = {
                if (haptic) hapticFeedback.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                onClick()
            },
        )
}
