@file:Suppress("unused", "UNUSED_PARAMETER")

package com.aurea.aurea.editor

import android.app.Activity
import android.content.Context
import com.aurea.aurea.state.EditorStore

internal enum class RealCameraTracking { Rotation, Position, Limited, Permission, Failed }

internal fun Context.realCameraActivity(): Activity? = com.aurea.pc.PcApp.context as? Activity

/** Câmera real pelos sensores do aparelho: um PC não tem giroscópio. Fica "limitado". */
internal class RealCameraMotion(
    private val context: Context,
    private val store: EditorStore,
    private val layer: Long,
    private val requestCameraPermission: () -> Unit,
) {
    fun resume() { RealCamera.tracking = RealCameraTracking.Limited }
    fun permissionResult(granted: Boolean) {}
    fun pause() {}
    fun close() {}
    fun updateDisplayGeometry() {}
}
