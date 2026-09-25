package com.aurea.aurea.ads

import android.os.Handler
import android.os.Looper
import android.util.Log

/**
 * A tela cheia do anúncio (Unity/LevelPlay) roda um WebView do Chromium no
 * processo do app. No emulador API 35 (WebView 124) a sessão de mídia dele
 * lança `JniAndroid$UncaughtExceptionException` (NPE em
 * `MediaSessionImpl.mediaSessionPositionChanged`) na thread principal logo
 * depois da exportação — o vídeo já está salvo, mas o app inteiro morria
 * (P10_EXPORT_AD_WEBVIEW_2026-09-25.md). A exceção é assíncrona, fora do
 * try/catch do `showAd`.
 *
 * O guarda reentra no laço da thread principal e engole SÓ essa falha do
 * Chromium dentro do anúncio (o Android já encerra a Activity do anúncio);
 * qualquer outra exceção sobe exatamente como antes e derruba o app.
 */
object AdWebViewCrashGuard {
    private const val TAG = "AureaAdGuard"
    private var installed = false

    fun install() {
        if (installed) return
        installed = true
        Handler(Looper.getMainLooper()).post {
            while (true) {
                try {
                    Looper.loop()
                    return@post   // o laço terminou por quit(): processo encerrando
                } catch (t: Throwable) {
                    if (!isAdWebViewMediaFault(t)) throw t
                    Log.w(TAG, "falha do WebView do anúncio ignorada (app segue vivo)", t)
                }
            }
        }
    }

    /** A falha conhecida: exceção JNI do Chromium vinda da sessão de mídia do WebView. */
    fun isAdWebViewMediaFault(t: Throwable): Boolean {
        var chromium = false
        var media = false
        var cur: Throwable? = t
        var depth = 0
        while (cur != null && depth < 8) {
            if (cur.javaClass.name.startsWith("org.chromium.")) chromium = true
            for (frame in cur.stackTrace) {
                if (frame.className.startsWith("org.chromium.")) chromium = true
                if (frame.className.endsWith("MediaSessionImpl")) media = true
            }
            cur = cur.cause
            depth++
        }
        return chromium && media
    }
}
