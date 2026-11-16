@file:Suppress("unused", "UNUSED_PARAMETER")

package androidx.activity.compose

import androidx.activity.result.ManagedActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContract
import androidx.activity.result.newLauncher
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import com.aurea.pc.PcBack
import kotlinx.coroutines.launch

@Composable
fun <I, O> rememberLauncherForActivityResult(
    contract: ActivityResultContract<I, O>,
    onResult: (O) -> Unit,
): ManagedActivityResultLauncher<I, O> {
    val current = rememberUpdatedState(onResult)
    return remember(contract) { newLauncher(contract) { current.value } }
}

/**
 * Voltar do Android = Esc (ou o botão "voltar" do mouse) no PC. Como no
 * Android, vale o handler habilitado mais recente na composição.
 */
@Composable
fun BackHandler(enabled: Boolean = true, onBack: () -> Unit) {
    val current = rememberUpdatedState(onBack)
    val handler = remember { PcBack.Handler { current.value() } }
    SideEffect { handler.enabled = enabled }
    DisposableEffect(handler) {
        PcBack.add(handler)
        onDispose { PcBack.remove(handler) }
    }
}

/** Gesto preditivo do Android 14: no PC, o mesmo que [BackHandler]. */
@Composable
fun PredictiveBackHandler(enabled: Boolean = true, onBack: suspend (kotlinx.coroutines.flow.Flow<Any>) -> Unit) {
    BackHandler(enabled) { com.aurea.pc.PcApp.scope.launch { onBack(kotlinx.coroutines.flow.emptyFlow()) } }
}
