package android.content

import com.aurea.pc.PcPaths
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors

/**
 * Preferências em JSON, uma por nome, em `shared_prefs/<nome>.json` — o mesmo
 * papel do XML do Android. `apply` grava numa thread própria; `commit` grava já.
 */
interface SharedPreferences {
    fun getAll(): Map<String, *>
    fun getString(key: String, defValue: String?): String?
    fun getStringSet(key: String, defValues: Set<String>?): Set<String>?
    fun getInt(key: String, defValue: Int): Int
    fun getLong(key: String, defValue: Long): Long
    fun getFloat(key: String, defValue: Float): Float
    fun getBoolean(key: String, defValue: Boolean): Boolean
    operator fun contains(key: String): Boolean
    fun edit(): Editor
    fun registerOnSharedPreferenceChangeListener(listener: OnSharedPreferenceChangeListener)
    fun unregisterOnSharedPreferenceChangeListener(listener: OnSharedPreferenceChangeListener)

    fun interface OnSharedPreferenceChangeListener {
        fun onSharedPreferenceChanged(sharedPreferences: SharedPreferences, key: String?)
    }

    interface Editor {
        fun putString(key: String, value: String?): Editor
        fun putStringSet(key: String, values: Set<String>?): Editor
        fun putInt(key: String, value: Int): Editor
        fun putLong(key: String, value: Long): Editor
        fun putFloat(key: String, value: Float): Editor
        fun putBoolean(key: String, value: Boolean): Editor
        fun remove(key: String): Editor
        fun clear(): Editor
        fun commit(): Boolean
        fun apply()
    }

    companion object {
        private val open = ConcurrentHashMap<String, JsonPreferences>()
        internal fun open(name: String): SharedPreferences = open.getOrPut(name) { JsonPreferences(name) }
        internal fun delete(name: String): Boolean {
            open.remove(name)
            return JsonPreferences.fileOf(name).delete()
        }
    }
}

internal class JsonPreferences(private val name: String) : SharedPreferences {
    private val lock = Any()
    private val map: MutableMap<String, Any?> = load()
    private val listeners = java.util.concurrent.CopyOnWriteArrayList<SharedPreferences.OnSharedPreferenceChangeListener>()

    companion object {
        private val writer = Executors.newSingleThreadExecutor { r -> Thread(r, "aurea-prefs").apply { isDaemon = true } }
        fun fileOf(name: String) = File(File(PcPaths.data, "shared_prefs").apply { mkdirs() }, "$name.json")
    }

    private fun load(): MutableMap<String, Any?> {
        val f = fileOf(name)
        val out = LinkedHashMap<String, Any?>()
        if (!f.isFile) return out
        runCatching {
            val o = JSONObject(f.readText())
            for (k in o.keys()) {
                val e = o.getJSONObject(k)
                out[k] = when (e.getString("t")) {
                    "s" -> e.getString("v")
                    "i" -> e.getInt("v")
                    "l" -> e.getLong("v")
                    "f" -> e.getDouble("v").toFloat()
                    "b" -> e.getBoolean("v")
                    "set" -> e.getJSONArray("v").let { a -> (0 until a.length()).map { a.getString(it) }.toSet() }
                    else -> null
                }
            }
        }
        return out
    }

    private fun snapshotJson(): String = synchronized(lock) {
        val o = JSONObject()
        for ((k, v) in map) {
            val e = JSONObject()
            when (v) {
                is String -> { e.put("t", "s"); e.put("v", v) }
                is Int -> { e.put("t", "i"); e.put("v", v) }
                is Long -> { e.put("t", "l"); e.put("v", v) }
                is Float -> { e.put("t", "f"); e.put("v", v.toDouble()) }
                is Boolean -> { e.put("t", "b"); e.put("v", v) }
                is Set<*> -> { e.put("t", "set"); e.put("v", JSONArray(v.map { it.toString() })) }
                else -> continue
            }
            o.put(k, e)
        }
        o.toString()
    }

    private fun write() {
        val f = fileOf(name)
        val tmp = File(f.parentFile, f.name + ".tmp")
        tmp.writeText(snapshotJson())
        java.nio.file.Files.move(tmp.toPath(), f.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING, java.nio.file.StandardCopyOption.ATOMIC_MOVE)
    }

    override fun getAll(): Map<String, *> = synchronized(lock) { HashMap(map) }
    override fun getString(key: String, defValue: String?): String? = synchronized(lock) { map[key] as? String ?: defValue }
    @Suppress("UNCHECKED_CAST")
    override fun getStringSet(key: String, defValues: Set<String>?): Set<String>? = synchronized(lock) { (map[key] as? Set<String>)?.toSet() ?: defValues }
    override fun getInt(key: String, defValue: Int): Int = synchronized(lock) { (map[key] as? Number)?.toInt() ?: defValue }
    override fun getLong(key: String, defValue: Long): Long = synchronized(lock) { (map[key] as? Number)?.toLong() ?: defValue }
    override fun getFloat(key: String, defValue: Float): Float = synchronized(lock) { (map[key] as? Number)?.toFloat() ?: defValue }
    override fun getBoolean(key: String, defValue: Boolean): Boolean = synchronized(lock) { map[key] as? Boolean ?: defValue }
    override fun contains(key: String): Boolean = synchronized(lock) { key in map }
    override fun registerOnSharedPreferenceChangeListener(listener: SharedPreferences.OnSharedPreferenceChangeListener) { listeners += listener }
    override fun unregisterOnSharedPreferenceChangeListener(listener: SharedPreferences.OnSharedPreferenceChangeListener) { listeners -= listener }

    override fun edit(): SharedPreferences.Editor = object : SharedPreferences.Editor {
        private val changes = LinkedHashMap<String, Any?>()
        private val removed = LinkedHashSet<String>()
        private var clear = false
        override fun putString(key: String, value: String?) = apply { if (value == null) removed += key else changes[key] = value }
        override fun putStringSet(key: String, values: Set<String>?) = apply { if (values == null) removed += key else changes[key] = values.toSet() }
        override fun putInt(key: String, value: Int) = apply { changes[key] = value }
        override fun putLong(key: String, value: Long) = apply { changes[key] = value }
        override fun putFloat(key: String, value: Float) = apply { changes[key] = value }
        override fun putBoolean(key: String, value: Boolean) = apply { changes[key] = value }
        override fun remove(key: String) = apply { removed += key }
        override fun clear() = apply { clear = true }

        private fun merge(): List<String> = synchronized(lock) {
            if (clear) map.clear()
            removed.forEach { map.remove(it); changes.remove(it) }
            map.putAll(changes)
            (removed + changes.keys).toList()
        }

        private fun notify(keys: List<String>) {
            if (listeners.isEmpty()) return
            com.aurea.pc.PcApp.runOnUi { keys.forEach { k -> listeners.forEach { it.onSharedPreferenceChanged(this@JsonPreferences, k) } } }
        }

        override fun commit(): Boolean {
            val keys = merge()
            val ok = runCatching { write() }.isSuccess
            notify(keys)
            return ok
        }

        override fun apply() {
            val keys = merge()
            writer.execute { runCatching { write() } }
            notify(keys)
        }
    }
}
