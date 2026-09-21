package com.aurea.aurea.ui

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.core.tween
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.aurea.aurea.editor.EditorScreen
import com.aurea.aurea.home.HomeScreen
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.Screen
import com.aurea.aurea.ui.ds.AureaAlert
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaTheme

/**
 * Raiz da UI. Duas telas (Home e Editor), trocadas com a transição de página
 * do app antigo (a nova entra da direita, a de baixo recua um terço, 500 ms).
 */
@Composable
fun AureaApp(store: EditorStore) {
    AureaTheme {
        Box(Modifier.fillMaxSize().background(AureaColors.Background)) {
            AnimatedContent(
                targetState = store.screen,
                transitionSpec = {
                    if (targetState == Screen.Editor) {
                        slideInHorizontally(tween(500)) { it } togetherWith slideOutHorizontally(tween(500)) { -it / 3 }
                    } else {
                        slideInHorizontally(tween(500)) { -it / 3 } togetherWith slideOutHorizontally(tween(500)) { it }
                    }
                },
                label = "tela",
            ) { screen ->
                when (screen) {
                    Screen.Home -> HomeScreen(store)
                    Screen.Editor -> EditorScreen(store)
                }
            }
            store.errorMessage?.let { msg ->
                AureaAlert(
                    title = "Aurea",
                    message = msg,
                    confirmLabel = "Entendi",
                    cancelLabel = null,
                    onConfirm = {},
                    onDismiss = { store.dismissError() },
                )
            }
        }
    }
}
