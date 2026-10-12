package com.aurea.aurea.editor

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.content.pm.PackageManager
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.Handler
import android.os.Looper
import android.view.Surface
import androidx.core.content.ContextCompat
import com.aurea.aurea.state.EditorStore
import com.google.ar.core.ArCoreApk

internal enum class RealCameraTracking { Rotation, Position, Limited, Permission, Failed }

internal fun Context.realCameraActivity(): Activity? {
    var current = this
    while (current is ContextWrapper) {
        if (current is Activity) return current
        val next = current.baseContext
        if (next === current) break
        current = next
    }
    return current as? Activity
}

/** Platform measurements only. The engine owns reference pose, smoothing and keys. */
internal class RealCameraMotion(
    private val context: Context,
    private val store: EditorStore,
    private val layer: Long,
    private val requestCameraPermission: () -> Unit,
) : SensorEventListener {
    private val main = Handler(Looper.getMainLooper())
    private val manager = context.getSystemService(Context.SENSOR_SERVICE) as? SensorManager
    private val sensor = manager?.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR)
        ?: manager?.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR)
        ?: manager?.getDefaultSensor(Sensor.TYPE_GEOMAGNETIC_ROTATION_VECTOR)
    private val rotation = FloatArray(9)
    private val remapped = FloatArray(9)
    private val pose = FloatArray(16)
    private var registered = false
    private var permissionRequested = false
    private var installRequested = false
    private var transientChecks = 0
    private var ar: RealCameraArTracker? = null
    @Volatile private var active = false
    @Volatile private var closed = false
    @Volatile private var arOwnsPose = false

    fun resume() {
        if (closed || RealCamera.session?.layer != layer) return
        active = true
        if (arOwnsPose) RealCamera.tracking = RealCameraTracking.Limited
        if (!registered && sensor != null) {
            // 100 Hz: a 60 fps take gets a fresh measured pose for every frame
            // (SENSOR_DELAY_GAME, ~50 Hz, repeated poses on 60 fps comps).
            registered = manager?.registerListener(this, sensor, 10_000) == true
        }
        RealCamera.available = registered || arOwnsPose
        val activity = context.realCameraActivity()
        if (activity == null) { setTracking(RealCameraTracking.Rotation); return }
        // Google Play Services for AR omits 32-bit session libraries on some
        // 64-bit devices. A sideloaded 32-bit APK still records sensor rotation.
        if (!android.os.Process.is64Bit() && android.os.Build.SUPPORTED_64_BIT_ABIS.isNotEmpty()) {
            setTracking(RealCameraTracking.Rotation)
            return
        }
        val arCore = ArCoreApk.getInstance()
        try {
            val availability = arCore.checkAvailability(activity)
            if (availability.isTransient) {
                // Asynchronous device support query: orientation remains usable while checking.
                if (transientChecks++ < 30) main.postDelayed(retry, 200L)
                return
            }
            transientChecks = 0
            if (!availability.isSupported) { setTracking(RealCameraTracking.Rotation); return }
            if (arCore.requestInstall(activity, !installRequested) == ArCoreApk.InstallStatus.INSTALL_REQUESTED) {
                installRequested = true
                setTracking(RealCameraTracking.Rotation)
                return
            }
            if (ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
                setTracking(RealCameraTracking.Permission)
                if (!permissionRequested) {
                    permissionRequested = true
                    requestCameraPermission()
                }
                return
            }
            RealCamera.usesArCore = true
            arOwnsPose = true
            RealCamera.available = true
            RealCamera.tracking = RealCameraTracking.Limited
            val tracker = ar ?: RealCameraArTracker(context,
                onPose = { matrix, timestamp ->
                    if (!closed && active && arOwnsPose && RealCamera.session?.layer == layer)
                        store.realCameraSample(matrix, timestamp, true)
                },
                onHold = {
                    if (!closed && active && arOwnsPose && RealCamera.session?.layer == layer)
                        store.realCameraHold()
                },
                onState = { mode ->
                    if (!closed && active && RealCamera.session?.layer == layer) setTracking(mode)
                }).also { ar = it }
            updateGeometry(tracker)
            tracker.resume()
        } catch (e: Exception) {
            android.util.Log.w("AureaRealCamera", "AR setup unavailable: ${e.javaClass.simpleName}")
            setTracking(RealCameraTracking.Failed)
        }
    }

    private val retry = Runnable { if (active && !closed) resume() }

    fun permissionResult(granted: Boolean) {
        if (!active || closed) return
        if (granted) resume() else setTracking(RealCameraTracking.Permission)
    }

    fun pause() {
        active = false
        main.removeCallbacks(retry)
        manager?.unregisterListener(this)
        registered = false
        ar?.pause()
        // End the take before the preview lifecycle pauses; keys remain one undo group.
        if (RealCamera.session?.layer == layer) store.realCameraRecord(false)
    }

    fun close() {
        if (closed) return
        closed = true
        active = false
        main.removeCallbacks(retry)
        manager?.unregisterListener(this)
        registered = false
        ar?.close()
        ar = null
    }

    private fun setTracking(mode: RealCameraTracking) {
        arOwnsPose = mode == RealCameraTracking.Position || mode == RealCameraTracking.Limited
        RealCamera.tracking = mode
        RealCamera.available = registered || arOwnsPose
    }

    @Suppress("DEPRECATION")
    private fun currentRotation(): Int = context.realCameraActivity()?.windowManager?.defaultDisplay?.rotation
        ?: Surface.ROTATION_0

    fun updateDisplayGeometry() { ar?.let { updateGeometry(it) } }

    private fun updateGeometry(tracker: RealCameraArTracker) {
        val view = context.realCameraActivity()?.window?.decorView
        tracker.displayGeometry(currentRotation(), view?.width ?: context.resources.displayMetrics.widthPixels,
            view?.height ?: context.resources.displayMetrics.heightPixels)
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) = Unit

    override fun onSensorChanged(event: SensorEvent) {
        if (!active || closed || RealCamera.session?.layer != layer) return
        ar?.let { updateGeometry(it) }
        if (arOwnsPose) return
        SensorManager.getRotationMatrixFromVector(rotation, event.values)
        val screenRotation = currentRotation()
        val axes = when (screenRotation) {
            Surface.ROTATION_90 -> SensorManager.AXIS_Y to SensorManager.AXIS_MINUS_X
            Surface.ROTATION_180 -> SensorManager.AXIS_MINUS_X to SensorManager.AXIS_MINUS_Y
            Surface.ROTATION_270 -> SensorManager.AXIS_MINUS_Y to SensorManager.AXIS_X
            else -> SensorManager.AXIS_X to SensorManager.AXIS_Y
        }
        if (!SensorManager.remapCoordinateSystem(rotation, axes.first, axes.second, remapped)) return
        pose.fill(0f)
        // The rotation vector's world is Z-up (east, north, up); the engine's
        // is the AR one, Y-up: (x, y, z) -> (x, z, -y). Gravity has to be the
        // vertical the engine pans about, as when filming.
        for (column in 0..2) {
            pose[column * 4] = remapped[column]
            pose[column * 4 + 1] = remapped[6 + column]
            pose[column * 4 + 2] = -remapped[3 + column]
        }
        pose[15] = 1f
        // No acceleration integration: a rotation vector provides no position measurement.
        store.realCameraSample(pose, event.timestamp, false)
    }
}
