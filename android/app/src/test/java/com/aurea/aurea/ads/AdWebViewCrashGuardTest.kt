package com.aurea.aurea.ads

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** O guarda só engole a falha do Chromium na sessão de mídia do anúncio. */
class AdWebViewCrashGuardTest {

    private fun frame(cls: String, method: String) = StackTraceElement(cls, method, "x.java", 1)

    @Test
    fun chromiumMediaSessionFaultIsRecognized() {
        val cause = NullPointerException("Attempt to invoke virtual method 'void WV.GE.b()'").apply {
            stackTrace = arrayOf(frame("org.chromium.content.browser.MediaSessionImpl", "mediaSessionPositionChanged"))
        }
        val jni = RuntimeException("uncaught", cause).apply {
            stackTrace = arrayOf(frame("org.chromium.base.JniAndroid", "handleException"))
        }
        assertTrue(AdWebViewCrashGuard.isAdWebViewMediaFault(jni))
    }

    @Test
    fun appAndOtherChromiumFaultsStillCrash() {
        val app = IllegalStateException("bug do app").apply {
            stackTrace = arrayOf(frame("com.aurea.aurea.state.EditorStore", "seek"))
        }
        assertFalse(AdWebViewCrashGuard.isAdWebViewMediaFault(app))
        val otherChromium = RuntimeException("gpu").apply {
            stackTrace = arrayOf(frame("org.chromium.gpu.GpuProcess", "run"))
        }
        assertFalse(AdWebViewCrashGuard.isAdWebViewMediaFault(otherChromium))
        // Nome parecido fora do Chromium não conta.
        val lookalike = RuntimeException("x").apply {
            stackTrace = arrayOf(frame("com.example.MediaSessionImpl", "run"))
        }
        assertFalse(AdWebViewCrashGuard.isAdWebViewMediaFault(lookalike))
    }
}
