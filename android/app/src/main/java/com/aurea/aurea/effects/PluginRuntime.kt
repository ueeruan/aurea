package com.aurea.aurea.effects

import android.app.Application
import android.net.Uri
import android.os.Handler
import android.os.Looper
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.concurrent.Executors

/**
 * 0.3.4 PLUGINS: o lado Kotlin dos painéis e scripts (docs/plugins/README.md).
 * O motor roda o script (AureaScript) e guarda o estado dos controles; aqui só
 * se chama o motor numa thread própria de pilha grande (o interpretador é
 * recursivo), se mostram as mensagens e se relê o estado. Par do iOS:
 * `PluginRuntime` em PluginsV2.swift.
 *
 * Uma chamada por vez (o motor serializa e um plugin ocupado responde "ocupado");
 * "Parar" cancela de qualquer thread.
 */
object PluginRuntime {
    private val worker = Executors.newSingleThreadExecutor { r -> Thread(null, r, "aurea-plugin", 8L shl 20).apply { isDaemon = true } }
    private val main = Handler(Looper.getMainLooper())

    @Volatile internal var app: Application? = null
        private set
    private var store: EditorStore? = null
    private var hostInstalled = false

    /** Estado dos controles por plugin (o JSON do motor: values, items, texts...). */
    val states = mutableStateMapOf<String, JSONObject>()
    /** Plugins com uma chamada rodando (mostra "Rodando…" e "Parar"). */
    val busy = mutableStateMapOf<String, Boolean>()
    /** Último erro do script por plugin (texto com arquivo:linha). */
    val errors = mutableStateMapOf<String, String>()
    /** Painel aberto no editor: id do plugin e do painel. */
    var editorPanel by mutableStateOf<Pair<String, String>?>(null)
    /** Aba de plugin escolhida na Home (id do plugin + painel). */
    var homePanel by mutableStateOf<Pair<String, String>?>(null)
    /** Alerta pedido pelo script (ui.alert / app.alert). */
    var alert by mutableStateOf<String?>(null)
    /** Console aberto (id do plugin). */
    var console by mutableStateOf<String?>(null)
    /** Instalação esperando consentimento: o caminho copiado e o que o pacote pede. */
    var pendingInstall by mutableStateOf<Pair<String, JSONObject>?>(null)

    /** Chamado quando o motor está pronto (EditorStore.refreshPlugins). */
    fun attach(store: EditorStore) {
        this.store = store
        app = store.getApplication<Application>()
        if (!hostInstalled) hostInstalled = store.pluginEngine.installPluginHost()
    }

    fun locale(): String = app?.resources?.configuration?.locales?.get(0)?.toLanguageTag() ?: "pt-BR"

    fun plugin(id: String): PluginTexts.Plugin? = PluginTexts.plugins.firstOrNull { it.id == id }

    /** Os plugins ligados que funcionam (a interface só mostra estes). */
    fun active(): List<PluginTexts.Plugin> = (store?.plugins ?: PluginTexts.plugins).filter { it.ok && it.enabled }

    // ------------------------------------------------------------------------
    // Chamadas
    // ------------------------------------------------------------------------
    fun openPanel(id: String, panel: String, inEditor: Boolean = true) {
        if (inEditor) editorPanel = id to panel else homePanel = id to panel
        run(id) { it.pluginOpenPanel(id, panel, locale()) }
    }

    fun closeEditorPanel() { editorPanel = null }

    /** Uma ação (menu, barra da camada): abre o painel ou chama a função. */
    fun runAction(id: String, action: PluginTexts.Action) {
        if (action.panel.isNotEmpty()) openPanel(id, action.panel) else call(id, action.call)
    }

    fun call(id: String, function: String, argsJson: String = "[]") = run(id) { it.pluginCall(id, function, argsJson, locale()) }

    /** A pessoa mexeu num controle: `kind` = click | change | select. */
    fun event(id: String, control: String, kind: String, valueJson: String = "") {
        // O valor aparece na hora (o motor confirma na resposta).
        if (kind == "change" && valueJson.isNotEmpty()) {
            states[id]?.let { s ->
                val copy = JSONObject(s.toString())
                runCatching { copy.optJSONObject("values")?.put(control, JSONArray("[$valueJson]").get(0)) }
                states[id] = copy
            }
        }
        run(id) { it.pluginEvent(id, control, kind, valueJson, locale()) }
    }

    fun cancel(id: String) {
        val engine = store?.pluginEngine ?: return
        engine.pluginCancel(id)
    }

    fun refreshState(id: String) {
        val engine = store?.pluginEngine ?: return
        worker.execute {
            val raw = engine.pluginState(id)
            main.post { runCatching { JSONObject(raw) }.getOrNull()?.takeIf { it.has("values") }?.let { states[id] = it } }
        }
    }

    private fun run(id: String, body: (com.aurea.aurea.engine.AureaEngine) -> String) {
        val engine = store?.pluginEngine ?: return
        busy[id] = true
        errors.remove(id)
        // Progresso (ui.progress) enquanto roda: o estado vale durante a chamada.
        val poll = object : Runnable {
            override fun run() {
                if (busy[id] != true) return
                Thread {
                    val raw = engine.pluginState(id)
                    main.post { if (busy[id] == true) runCatching { JSONObject(raw) }.getOrNull()?.takeIf { it.has("values") }?.let { states[id] = it } }
                }.start()
                main.postDelayed(this, 400)
            }
        }
        main.postDelayed(poll, 400)
        worker.execute {
            val raw = runCatching { body(engine) }.getOrElse { "{\"ok\":false,\"error\":{\"text\":\"${it.javaClass.simpleName}\"}}" }
            main.post { finish(id, raw) }
        }
    }

    private fun finish(id: String, raw: String) {
        busy[id] = false
        val result = runCatching { JSONObject(raw) }.getOrNull() ?: return
        result.optJSONObject("state")?.let { states[id] = it }
        if (!result.optBoolean("ok")) {
            val e = result.optJSONObject("error")
            errors[id] = e?.optString("text").orEmpty().ifEmpty { e?.optString("message").orEmpty() }
        }
        val messages = result.optJSONArray("messages") ?: JSONArray()
        for (i in 0 until messages.length()) {
            val m = messages.optJSONObject(i) ?: continue
            val text = m.optString("text")
            when (m.optString("kind")) {
                "toast" -> store?.showToast(text)
                "alert" -> alert = text
                "select" -> runCatching {
                    val ids = JSONArray(text)
                    store?.selectFromPlugin(List(ids.length()) { ids.optString(it).toULong().toLong() })
                }
            }
        }
        result.optString("openPanel").takeIf { it.isNotEmpty() }?.let { editorPanel = id to it }
        store?.afterPluginScript()
    }

    // ------------------------------------------------------------------------
    // Instalação com consentimento
    // ------------------------------------------------------------------------
    /** Copia o arquivo escolhido e lê o que ele pede; a folha de consentimento decide. */
    fun beginInstall(uri: Uri) {
        val s = store ?: return
        val context = app ?: return
        s.pluginBusy(context.getString(R.string.pv2_reading))
        worker.execute {
            val dir = File(context.cacheDir, "plugin-install").apply { mkdirs() }
            val display = runCatching {
                context.contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
                    if (c.moveToFirst()) c.getString(0) else null
                }
            }.getOrNull() ?: uri.lastPathSegment.orEmpty()
            val base = File(display).name.replace(Regex("[^A-Za-z0-9._-]"), "_").ifEmpty { "plugin.aureaplugin" }
            val ext = base.substringAfterLast('.', "").lowercase(java.util.Locale.ROOT)
            val temp = File(dir, if (ext == "fs" || ext == "aureaplugin" || ext == "zip") base else "$base.aureaplugin")
            val copied = runCatching {
                context.contentResolver.openInputStream(uri)?.use { input -> temp.outputStream().use { input.copyTo(it) } } != null
            }.getOrDefault(false)
            val info = if (copied) runCatching { JSONObject(s.pluginEngine.inspectPlugin(temp.absolutePath)) }.getOrNull() else null
            main.post {
                s.pluginBusy(null)
                if (info == null || !info.optBoolean("ok")) {
                    temp.delete()
                    s.pluginError(context.getString(R.string.pv2_inspect_failed, info?.optString("error").orEmpty()))
                } else {
                    pendingInstall = temp.absolutePath to info
                }
            }
        }
    }

    /** A pessoa aceitou: instala e grava as permissões que ela deixou ligadas. */
    fun confirmInstall(grantedMask: Int) {
        val (path, info) = pendingInstall ?: return
        pendingInstall = null
        val s = store ?: return
        val context = app ?: return
        s.pluginBusy(context.getString(R.string.plug_installing))
        worker.execute {
            val result = s.pluginEngine.installPlugin(path)
            if (result.ok) s.pluginEngine.setPluginPermissions(result.id, grantedMask)
            File(path).delete()
            main.post {
                s.pluginBusy(null)
                s.afterPluginInstall()
                if (result.ok) {
                    val names = PluginTexts.plugins.firstOrNull { it.id == result.id }?.names ?: emptyMap()
                    s.showToast(context.getString(R.string.plug_installed, PluginTexts.pick(names, locale()) ?: result.id))
                } else {
                    s.pluginError(context.getString(R.string.plug_failed, result.error))
                }
                info.optString("id").let { states.remove(it); errors.remove(it) }
            }
        }
    }

    fun cancelInstall() {
        pendingInstall?.first?.let { File(it).delete() }
        pendingInstall = null
    }

    fun setEnabled(id: String, on: Boolean) {
        val s = store ?: return
        if (s.pluginEngine.setPluginEnabled(id, on)) {
            if (!on && editorPanel?.first == id) editorPanel = null
            if (!on && homePanel?.first == id) homePanel = null
            s.refreshPlugins()
        }
    }

    fun setPermission(id: String, bit: Int, on: Boolean) {
        val s = store ?: return
        val p = plugin(id) ?: return
        val mask = if (on) p.granted or bit else p.granted and bit.inv()
        if (s.pluginEngine.setPluginPermissions(id, mask)) s.refreshPlugins()
    }

    fun consoleLines(id: String): List<Pair<Int, String>> {
        val raw = store?.pluginEngine?.pluginConsole(id) ?: return emptyList()
        val a = runCatching { JSONArray(raw) }.getOrNull() ?: return emptyList()
        return List(a.length()) { a.getJSONObject(it).let { o -> o.optInt("level") to o.optString("text") } }
    }

    fun clearConsole(id: String) { store?.pluginEngine?.pluginClearConsole(id) }

    fun reload(id: String) {
        store?.pluginEngine?.pluginReload(id)
        errors.remove(id)
    }
}
