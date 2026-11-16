package com.aurea.pc

import android.content.Context
import android.content.Intent
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.aurea.aurea.AureaApplication
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.swing.Swing
import java.awt.EventQueue

/** O processo do Aurea PC: o contexto único, a thread da UI e a janela. */
object PcApp {
    val application: AureaApplication by lazy { AureaApplication() }
    val context: Context get() = application

    val scope = CoroutineScope(SupervisorJob() + Dispatchers.Swing)

    @Volatile var launchIntent: Intent = Intent(Intent.ACTION_MAIN)
    @Volatile var closing = false
    @Volatile var refreshRate: Float = 60f

    /** Pedidos para a janela (Main.kt observa). */
    var closeRequested by mutableStateOf(false)
    /** Trocar o idioma "recria a Activity": a árvore do Compose recomeça. */
    var generation by mutableIntStateOf(0)
    var minimizeRequested by mutableIntStateOf(0)
    @Volatile var window: java.awt.Window? = null

    fun runOnUi(block: () -> Unit) {
        if (EventQueue.isDispatchThread()) block() else EventQueue.invokeLater(block)
    }

    fun requestClose() = runOnUi { closeRequested = true }
    fun recreate() = runOnUi {
        android.content.res.Configuration.current = android.content.res.Configuration(android.content.res.Configuration.current).also {
            com.aurea.aurea.ui.i18n.AppLanguage.override(context, it)?.let { fixed -> it.setTo(fixed) }
        }
        generation++
    }
    fun minimize() = runOnUi { minimizeRequested++ }

    /** Progresso na barra de tarefas do Windows (exportação); < 0 limpa. */
    fun taskbarProgress(fraction: Float) = runOnUi {
        val w = window ?: return@runOnUi
        runCatching {
            val tb = java.awt.Taskbar.getTaskbar()
            if (fraction < 0f) tb.setWindowProgressState(w, java.awt.Taskbar.State.OFF)
            else { tb.setWindowProgressState(w, java.awt.Taskbar.State.NORMAL); tb.setWindowProgressValue(w, (fraction * 100).toInt().coerceIn(0, 100)) }
        }
    }
}

/** Toast do Android = aviso flutuante no rodapé da janela. */
object PcToasts {
    data class Toast(val id: Long, val text: String, val long: Boolean)
    val visible = mutableStateListOf<Toast>()
    private var next = 0L
    fun show(text: String, long: Boolean) = PcApp.runOnUi {
        val t = Toast(next++, text, long)
        visible += t
        if (visible.size > 3) visible.removeAt(0)
        android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({ visible.remove(t) }, if (long) 3500 else 2000)
    }
}

/** A pilha de "voltar" (BackHandler): Esc e o botão voltar do mouse chamam o de cima. */
object PcBack {
    class Handler(val onBack: () -> Unit) { @Volatile var enabled = true }
    private val stack = java.util.concurrent.CopyOnWriteArrayList<Handler>()
    fun add(h: Handler) { stack += h }
    fun remove(h: Handler) { stack -= h }
    /** `true` = alguém tratou (senão, o "voltar" na raiz não fecha o app no PC). */
    fun dispatch(): Boolean {
        val h = stack.lastOrNull { it.enabled } ?: return false
        h.onBack()
        return true
    }
}
