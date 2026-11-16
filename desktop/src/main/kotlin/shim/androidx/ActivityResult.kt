@file:Suppress("unused", "UNUSED_PARAMETER")

package androidx.activity.result

import androidx.activity.result.contract.ActivityResultContract
import androidx.activity.result.contract.ActivityResultContracts

class PickVisualMediaRequest(val mediaType: ActivityResultContracts.PickVisualMedia.VisualMediaType = ActivityResultContracts.PickVisualMedia.ImageAndVideo) {
    class Builder {
        private var type: ActivityResultContracts.PickVisualMedia.VisualMediaType = ActivityResultContracts.PickVisualMedia.ImageAndVideo
        fun setMediaType(t: ActivityResultContracts.PickVisualMedia.VisualMediaType) = apply { type = t }
        fun build() = PickVisualMediaRequest(type)
    }
}

/** `PickVisualMediaRequest(ImageOnly)` como função, igual ao androidx. */
fun PickVisualMediaRequest(mediaType: ActivityResultContracts.PickVisualMedia.VisualMediaType, maxItems: Int): PickVisualMediaRequest =
    PickVisualMediaRequest(mediaType)

class ActivityResult(val resultCode: Int, val data: android.content.Intent?)

abstract class ActivityResultLauncher<I> {
    abstract fun launch(input: I)
    fun launch(input: I, options: Any?) = launch(input)
    open fun unregister() {}
}

class ManagedActivityResultLauncher<I, O> internal constructor(
    private val contract: ActivityResultContract<I, O>,
    private val onResult: () -> ((O) -> Unit),
) : ActivityResultLauncher<I>() {
    override fun launch(input: I) = contract.launchPc(input) { o -> com.aurea.pc.PcApp.runOnUi { onResult()(o) } }
}

/** `launch()` sem argumento para contratos de entrada `Unit`/`Void?`. */
fun ActivityResultLauncher<Unit>.launch() = launch(Unit)

internal fun <I, O> newLauncher(contract: ActivityResultContract<I, O>, onResult: () -> ((O) -> Unit)) =
    ManagedActivityResultLauncher(contract, onResult)
