package com.aurea.aurea.presets

import android.app.Application
import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import java.io.File

/**
 * Tipos de preset (o `kind` do JSON do motor, `presets::PresetKind`). O
 * número é o do motor; `dir` é a pasta em filesDir/presets/.
 */
enum class PresetKind(val id: Int, val dir: String, val label: String) {
    Effects(0, "efeitos", "Efeitos"),
    Text(1, "texto", "Texto"),
    Animation(2, "animacao", "Animação"),
    Caption(3, "legenda", "Legenda"),
    Curve(4, "curva", "Curva"),
}

/**
 * Um preset da biblioteca. Nativo: vem no app (JSON embutido, ou um preset de
 * texto do motor pelo número em [textPreset]). Do usuário: arquivo [file].
 */
data class PresetEntry(
    val key: String,
    val kind: PresetKind,
    val name: String,
    val builtin: Boolean,
    val json: String? = null,
    val textPreset: Int? = null,
    val file: File? = null,
)

/**
 * A biblioteca: presets nativos (assets + motor) + os da pessoa (um arquivo JSON por
 * preset em filesDir/presets/<tipo>/<nome>.json), favoritos e recentes
 * (SharedPreferences). O conteúdo dos arquivos é o JSON do motor — lido e
 * validado por ele na hora de aplicar.
 */
class PresetLibrary(private val app: Application) {
    private val prefs = app.getSharedPreferences("presets", Context.MODE_PRIVATE)
    private val root = File(app.filesDir, "presets")
    private val builtins = BuiltinPresets(app.assets)

    var user by mutableStateOf<Map<PresetKind, List<PresetEntry>>>(emptyMap())
        private set
    var favorites by mutableStateOf(prefs.getStringSet(FAVS, emptySet())?.toSet() ?: emptySet())
        private set
    var recents by mutableStateOf(prefs.getString(RECENTS, "")!!.split('\n').filter { it.isNotEmpty() })
        private set

    init {
        reload()
    }

    fun reload() {
        user = PresetKind.entries.associateWith { kind ->
            File(root, kind.dir).listFiles { f -> f.isFile && f.name.endsWith(".json") }
                ?.sortedBy { it.name.lowercase() }
                ?.map { f -> PresetEntry("u:${kind.dir}:${f.name}", kind, f.name.removeSuffix(".json"), builtin = false, file = f) }
                .orEmpty()
        }
    }

    fun entries(kind: PresetKind): List<PresetEntry> = builtins.of(kind) + user[kind].orEmpty()

    fun all(): List<PresetEntry> = PresetKind.entries.flatMap { entries(it) }

    fun find(key: String): PresetEntry? = all().firstOrNull { it.key == key }

    /** O JSON do preset (arquivo lido na hora; nulo = arquivo sumiu/ilegível). */
    fun jsonOf(e: PresetEntry): String? = e.json ?: e.file?.let { f -> runCatching { f.readText(Charsets.UTF_8) }.getOrNull() }

    fun exists(kind: PresetKind, name: String): Boolean = File(File(root, kind.dir), fileName(name)).exists()

    /** Grava (ou substitui) o preset da pessoa. Falso = nome inválido ou erro de disco. */
    fun save(kind: PresetKind, name: String, json: String): PresetEntry? {
        val fn = fileName(name)
        if (fn == ".json") return null
        val dir = File(root, kind.dir).apply { mkdirs() }
        val target = File(dir, fn)
        val tmp = File(dir, "$fn.tmp")
        val ok = runCatching {
            tmp.writeText(json, Charsets.UTF_8)
            if (target.exists()) target.delete()
            tmp.renameTo(target)
        }.getOrDefault(false)
        if (!ok) {
            tmp.delete()
            return null
        }
        reload()
        return find("u:${kind.dir}:$fn")
    }

    /** Apaga um preset criado pela pessoa (os nativos não têm arquivo). */
    fun delete(e: PresetEntry): Boolean {
        val f = e.file ?: return false
        if (e.builtin || !f.canonicalPath.startsWith(root.canonicalPath)) return false
        val ok = f.delete()
        if (ok) {
            if (e.key in favorites) toggleFavorite(e)
            recents = recents.filter { it != e.key }
            prefs.edit().putString(RECENTS, recents.joinToString("\n")).apply()
            reload()
        }
        return ok
    }

    fun toggleFavorite(e: PresetEntry) {
        favorites = if (e.key in favorites) favorites - e.key else favorites + e.key
        prefs.edit().putStringSet(FAVS, favorites).apply()
    }

    /** Aplicado agora: vai para o topo dos recentes (no máximo [MAX_RECENTS]). */
    fun markUsed(e: PresetEntry) {
        recents = (listOf(e.key) + recents.filter { it != e.key }).take(MAX_RECENTS)
        prefs.edit().putString(RECENTS, recents.joinToString("\n")).apply()
    }

    companion object {
        private const val FAVS = "favoritos"
        private const val RECENTS = "recentes"
        const val MAX_RECENTS = 10

        /** Nome → arquivo: sem separador de pasta nem caractere proibido, até 60. */
        fun fileName(name: String): String {
            val clean = name.trim()
                .map { c -> if (c.code < 32 || c in "/\\:*?\"<>|") '_' else c }
                .joinToString("")
                .trim('.', ' ')
                .take(60)
            return "$clean.json"
        }
    }
}
