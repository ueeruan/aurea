package com.aurea.aurea.effects

import com.aurea.aurea.engine.EffectParam
import org.json.JSONArray
import org.json.JSONObject

/**
 * PLUGINS DE EFEITO (0.3.0, docs/plugins/README.md): o motor publica o nome e os
 * parâmetros de cada plugin em pt-BR no catálogo, e os textos de TODOS os
 * idiomas que o plugin.json trouxe em `pluginsJson()`. Este objeto guarda essa
 * lista e escolhe o idioma do app — o par do iOS é `PluginTexts` (PluginsView.swift).
 *
 * Escolha do texto: a etiqueta exata (pt-BR), o idioma (pt), "*" (vale para
 * todos), inglês, pt-BR e, por fim, o primeiro que houver.
 */
object PluginTexts {
    class Param(val id: String, val labels: Map<String, String>, val options: List<Map<String, String>>)
    class Plugin(
        val id: String,
        val version: String,
        val author: String,
        val kind: String,
        val effectTypeId: Int,
        val names: Map<String, String>,
        val descriptions: Map<String, String>,
        val ok: Boolean,
        val error: String,
        val testRenderMs: Double,
        val params: List<Param>,
        /** 0.3.1: "kind": "shader3d" (câmera da cena e camadas ligadas). */
        val scene3d: Boolean = false,
    )

    @Volatile var plugins: List<Plugin> = emptyList()
        private set
    @Volatile private var byType: Map<Int, Plugin> = emptyMap()

    /** A lista do motor (`AureaEngine.pluginsJson`). JSON inválido = nenhum plugin. */
    fun update(json: String) {
        val list = runCatching { parse(JSONArray(json)) }.getOrDefault(emptyList())
        plugins = list
        byType = list.associateBy { it.effectTypeId }
    }

    fun isPlugin(typeId: Int): Boolean = byType.containsKey(typeId)

    fun pick(texts: Map<String, String>, tag: String): String? {
        if (texts.isEmpty()) return null
        val lang = tag.substringBefore('-').lowercase()
        return texts[tag]
            ?: texts.entries.firstOrNull { it.key.equals(tag, ignoreCase = true) }?.value
            ?: texts.entries.firstOrNull { it.key.substringBefore('-').lowercase() == lang }?.value
            ?: texts["*"] ?: texts["en"] ?: texts["pt-BR"] ?: texts.values.first()
    }

    fun name(typeId: Int, tag: String): String? = byType[typeId]?.let { pick(it.names, tag) }
    fun description(typeId: Int, tag: String): String? = byType[typeId]?.let { pick(it.descriptions, tag) }

    /** Rótulos e opções do plugin no idioma [tag]; nulo = não é plugin (a tabela do app traduz). */
    fun localize(typeId: Int, params: List<EffectParam>, tag: String): List<EffectParam>? {
        val plugin = byType[typeId] ?: return null
        return params.map { p ->
            val decl = plugin.params.getOrNull(p.index) ?: return@map p
            val label = pick(decl.labels, tag) ?: p.label
            val options = if (decl.options.size == p.enumLabels.size) decl.options.mapIndexed { i, o -> pick(o, tag) ?: p.enumLabels[i] } else p.enumLabels
            EffectParam(
                index = p.index, type = p.type, flags = p.flags, min = p.min, max = p.max,
                value = p.value, defaultValue = p.defaultValue, label = label, unit = p.unit,
                enumLabels = options, animated = p.animated, hardMin = p.hardMin, hardMax = p.hardMax,
                section = p.section, sectionId = p.sectionId, sectionLabel = p.sectionLabel, sectionToggle = p.sectionToggle,
                sectionOpen = p.sectionOpen, tip = p.tip,
            )
        }
    }

    private fun texts(o: JSONObject?): Map<String, String> {
        if (o == null) return emptyMap()
        val out = LinkedHashMap<String, String>()
        o.keys().forEach { k -> out[k] = o.optString(k) }
        return out
    }

    private fun parse(array: JSONArray): List<Plugin> = List(array.length()) { i ->
        val o = array.getJSONObject(i)
        val params = o.optJSONArray("params") ?: JSONArray()
        Plugin(
            id = o.optString("id"),
            version = o.optString("version"),
            author = o.optString("author"),
            kind = o.optString("kind"),
            effectTypeId = o.optLong("effectTypeId").toInt(),
            names = texts(o.optJSONObject("name")),
            descriptions = texts(o.optJSONObject("description")),
            ok = o.optBoolean("ok"),
            error = o.optString("error"),
            testRenderMs = o.optDouble("testRenderMs", 0.0),
            params = List(params.length()) { k ->
                val q = params.getJSONObject(k)
                val opts = q.optJSONArray("options") ?: JSONArray()
                Param(q.optString("id"), texts(q.optJSONObject("label")), List(opts.length()) { texts(opts.optJSONObject(it)) })
            },
            scene3d = o.optBoolean("scene3d"),
        )
    }
}
