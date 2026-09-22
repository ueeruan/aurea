package com.aurea.aurea.ui

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.core.tween
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.ui.theme.AureaType
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
            store.toast?.let { msg ->
                Box(
                    Modifier
                        .align(Alignment.BottomCenter)
                        .navigationBarsPadding()
                        .padding(bottom = 96.dp, start = 24.dp, end = 24.dp)
                        .clip(RoundedCornerShape(12.dp))
                        .background(AureaColors.SurfaceHigh)
                        .padding(horizontal = 16.dp, vertical = 10.dp),
                ) {
                    Text(msg, style = AureaType.Body)
                }
            }
            store.errorMessage?.let { msg ->
                AureaAlert(
                    title = "Aurea",
                    message = msg,
                    confirmLabel = stringResource(R.string.common_ok),
                    showCancel = false,
                    onConfirm = {},
                    onDismiss = { store.dismissError() },
                )
            }
        }
    }
}
