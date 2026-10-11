package com.aurea.aurea.editor

import android.content.Context
import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLSurface
import android.opengl.GLES11Ext
import android.opengl.GLES20
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.util.Log
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong
import com.google.ar.core.Config
import com.google.ar.core.Session
import com.google.ar.core.TrackingState

/** Owns only a private pbuffer/context. It never attaches to the editor preview. */
internal class RealCameraArTracker(
    context: Context,
    private val onPose: (FloatArray, Long) -> Unit,
    private val onHold: () -> Unit,
    private val onState: (RealCameraTracking) -> Unit,
) {
    private val app = context.applicationContext
    private val thread = HandlerThread("AureaRealCameraAR").apply { start() }
    private val worker = Handler(thread.looper)
    private val main = Handler(Looper.getMainLooper())
    @Volatile private var closed = false
    @Volatile private var wanted = false
    private val generation = AtomicLong(0L)
    private var resumedGeneration = 0L
    @Volatile private var rotation = 0
    @Volatile private var width = 1
    @Volatile private var height = 1
    private var session: Session? = null
    private var resumed = false
    private var display: EGLDisplay = EGL14.EGL_NO_DISPLAY
    private var eglContext: EGLContext = EGL14.EGL_NO_CONTEXT
    private var surface: EGLSurface = EGL14.EGL_NO_SURFACE
    private val texture = IntArray(1)
    private val pose = FloatArray(16)
    private var lastTimestamp = 0L
    private var lastState: RealCameraTracking? = null
    @Volatile private var latestState: RealCameraTracking? = null
    private val statePending = AtomicBoolean(false)
    private val deliverState = Runnable {
        statePending.set(false)
        val latest = latestState
        if (!closed && wanted && latest != null) onState(latest)
    }

    fun displayGeometry(displayRotation: Int, widthPx: Int, heightPx: Int) {
        rotation = displayRotation
        width = widthPx.coerceAtLeast(1)
        height = heightPx.coerceAtLeast(1)
    }

    fun resume() {
        if (closed) return
        wanted = true
        val requestedGeneration = generation.get()
        worker.post {
            if (closed || !wanted || resumed || generation.get() != requestedGeneration) return@post
            try {
                if (session == null) {
                    createContext()
                    session = Session(app).also { s ->
                        s.configure(Config(s).apply {
                            updateMode = Config.UpdateMode.LATEST_CAMERA_IMAGE
                            focusMode = Config.FocusMode.AUTO
                            planeFindingMode = Config.PlaneFindingMode.DISABLED
                            lightEstimationMode = Config.LightEstimationMode.DISABLED
                            depthMode = Config.DepthMode.DISABLED
                        })
                        s.setCameraTextureName(texture[0])
                    }
                }
                if (closed || !wanted || generation.get() != requestedGeneration) return@post
                session!!.setDisplayGeometry(rotation, width, height)
                session!!.resume()
                resumed = true
                resumedGeneration = requestedGeneration
                report(RealCameraTracking.Limited)
                worker.removeCallbacks(pump)
                worker.post(pump)
            } catch (e: Exception) {
                failed(e)
            }
        }
    }

    fun pause() {
        wanted = false
        generation.incrementAndGet()
        worker.post {
            worker.removeCallbacks(pump)
            if (resumed) {
                try { session?.pause() } catch (e: Exception) { Log.w(TAG, "AR pause: ${e.javaClass.simpleName}") }
                resumed = false
            }
        }
    }

    fun close() {
        if (closed) return
        closed = true
        wanted = false
        generation.incrementAndGet()
        worker.post {
            worker.removeCallbacks(pump)
            release()
            thread.quitSafely()
        }
    }

    private val pump = object : Runnable {
        override fun run() {
            if (closed || !wanted || !resumed || generation.get() != resumedGeneration) return
            try {
                val s = session ?: return
                s.setDisplayGeometry(rotation, width, height)
                val frame = s.update()
                if (closed || !wanted || generation.get() != resumedGeneration) return
                if (frame.camera.trackingState == TrackingState.TRACKING) {
                    if (frame.timestamp > lastTimestamp) {
                        frame.camera.displayOrientedPose.toMatrix(pose, 0)
                        lastTimestamp = frame.timestamp
                        onPose(pose, frame.timestamp)
                    }
                    report(RealCameraTracking.Position)
                } else {
                    // Write the already filtered native pose as a held key. No invalid AR pose
                    // enters the model, and exported interpolation cannot invent lost motion.
                    if (lastTimestamp > 0L) onHold()
                    report(RealCameraTracking.Limited)
                }
                worker.postDelayed(this, 16L)
            } catch (e: Exception) {
                if (!closed && wanted && generation.get() == resumedGeneration) failed(e)
            }
        }
    }

    private fun report(state: RealCameraTracking) {
        if (lastState == state) return
        lastState = state
        latestState = state
        if (statePending.compareAndSet(false, true)) main.post(deliverState)
    }

    private fun failed(e: Exception) {
        Log.w(TAG, "AR tracking unavailable: ${e.javaClass.simpleName}")
        worker.removeCallbacks(pump)
        release()
        lastState = null
        report(RealCameraTracking.Failed)
    }

    private fun createContext() {
        display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
        val version = IntArray(2)
        check(display != EGL14.EGL_NO_DISPLAY && EGL14.eglInitialize(display, version, 0, version, 1))
        val configs = arrayOfNulls<EGLConfig>(1)
        val count = IntArray(1)
        check(EGL14.eglChooseConfig(display, intArrayOf(
            EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
            EGL14.EGL_SURFACE_TYPE, EGL14.EGL_PBUFFER_BIT,
            EGL14.EGL_RED_SIZE, 8, EGL14.EGL_GREEN_SIZE, 8, EGL14.EGL_BLUE_SIZE, 8,
            EGL14.EGL_NONE), 0, configs, 0, 1, count, 0) && count[0] > 0)
        val config = checkNotNull(configs[0])
        eglContext = EGL14.eglCreateContext(display, config, EGL14.EGL_NO_CONTEXT,
            intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE), 0)
        check(eglContext != EGL14.EGL_NO_CONTEXT)
        surface = EGL14.eglCreatePbufferSurface(display, config,
            intArrayOf(EGL14.EGL_WIDTH, 1, EGL14.EGL_HEIGHT, 1, EGL14.EGL_NONE), 0)
        check(surface != EGL14.EGL_NO_SURFACE && EGL14.eglMakeCurrent(display, surface, surface, eglContext))
        GLES20.glGenTextures(1, texture, 0)
        check(texture[0] != 0)
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, texture[0])
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
        check(GLES20.glGetError() == GLES20.GL_NO_ERROR)
    }

    private fun release() {
        try { if (resumed) session?.pause() } catch (_: Exception) { }
        resumed = false
        try { session?.close() } catch (e: Exception) { Log.w(TAG, "AR close: ${e.javaClass.simpleName}") }
        session = null
        lastTimestamp = 0L
        if (display != EGL14.EGL_NO_DISPLAY) {
            if (texture[0] != 0 && eglContext != EGL14.EGL_NO_CONTEXT) {
                EGL14.eglMakeCurrent(display, surface, surface, eglContext)
                GLES20.glDeleteTextures(1, texture, 0)
                texture[0] = 0
            }
            EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT)
            if (surface != EGL14.EGL_NO_SURFACE) EGL14.eglDestroySurface(display, surface)
            if (eglContext != EGL14.EGL_NO_CONTEXT) EGL14.eglDestroyContext(display, eglContext)
            // Never terminate the process-wide default EGL display: GLES preview may share it.
            EGL14.eglReleaseThread()
        }
        display = EGL14.EGL_NO_DISPLAY
        eglContext = EGL14.EGL_NO_CONTEXT
        surface = EGL14.EGL_NO_SURFACE
    }

    companion object { private const val TAG = "AureaRealCamera" }
}
