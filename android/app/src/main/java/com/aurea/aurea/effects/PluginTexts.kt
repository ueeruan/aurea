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
        // 0.3.4 plugins: interface, script e permissões (formato 2).
        val enabled: Boolean = true,
        val hasScript: Boolean = false,
        val network: List<String> = emptyList(),
        val wantsClipboard: Boolean = false,
        val wantsFiles: Boolean = false,
        /** Concedidas: 1 rede, 2 área de transferência, 4 arquivos. */
        val granted: Int = 0,
        val panels: List<Panel> = emptyList(),
        val actions: List<Action> = emptyList(),
    ) {
        /** Bits que o plugin pede (o motor só aceita conceder estes). */
        val requested: Int get() = (if (network.isNotEmpty()) 1 else 0) or (if (wantsClipboard) 2 else 0) or (if (wantsFiles) 4 else 0)
    }

    // 0.3.4 plugins: o que o plugin.json declara para a interface (PluginUi.hpp).
    class Control(
        val id: String,
        val type: String,
        val label: Map<String, String>,
        val style: String,
        val min: Double,
        val max: Double,
        val step: Double,
        val unit: String,
        val options: List<Map<String, String>>,
        val placeholder: Map<String, String>,
        val multiline: Boolean,
        val maxLength: Int,
        val aspect: Float,
        val text: Map<String, String>,
    )
    class Panel(val id: String, val title: Map<String, String>, val icon: String, val placement: Set<String>, val controls: List<Control>)
    class Action(
        val id: String,
        val title: Map<String, String>,
        val icon: String,
        val placement: Set<String>,
        val panel: String,
        val call: String,
        val needsLayer: Boolean,
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
                // Omega efeitos (P0-1): a apresentação passa intacta.
                control = p.control, step = p.step, origin = p.origin, enumStyle = p.enumStyle, rowFlags = p.rowFlags,
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
            enabled = o.optBoolean("enabled", true),
            hasScript = o.optBoolean("hasScript"),
            network = o.optJSONObject("permissions")?.optJSONArray("network")?.let { a -> List(a.length()) { a.optString(it) } } ?: emptyList(),
            wantsClipboard = o.optJSONObject("permissions")?.optBoolean("clipboard") == true,
            wantsFiles = o.optJSONObject("permissions")?.optBoolean("files") == true,
            granted = o.optJSONObject("granted")?.let { g ->
                (if (g.optBoolean("network")) 1 else 0) or (if (g.optBoolean("clipboard")) 2 else 0) or (if (g.optBoolean("files")) 4 else 0)
            } ?: 0,
            panels = o.optJSONObject("ui")?.optJSONArray("panels")?.let { a -> List(a.length()) { panel(a.getJSONObject(it)) } } ?: emptyList(),
            actions = o.optJSONObject("ui")?.optJSONArray("actions")?.let { a -> List(a.length()) { action(a.getJSONObject(it)) } } ?: emptyList(),
        )
    }

    private fun places(a: JSONArray?): Set<String> = if (a == null) emptySet() else (0 until a.length()).map { a.optString(it) }.toSet()

    private fun panel(o: JSONObject): Panel {
        val controls = o.optJSONArray("controls") ?: JSONArray()
        return Panel(
            id = o.optString("id"),
            title = texts(o.optJSONObject("title")),
            icon = o.optString("icon", "wand"),
            placement = places(o.optJSONArray("placement")),
            controls = List(controls.length()) { k ->
                val c = controls.getJSONObject(k)
                val opts = c.optJSONArray("options") ?: JSONArray()
                Control(
                    id = c.optString("id"),
                    type = c.optString("type"),
                    label = texts(c.optJSONObject("label")),
                    style = c.optString("style", "primary"),
                    min = c.optDouble("min", 0.0),
                    max = c.optDouble("max", 1.0),
                    step = c.optDouble("step", 0.0),
                    unit = c.optString("unit"),
                    options = List(opts.length()) { texts(opts.optJSONObject(it)) },
                    placeholder = texts(c.optJSONObject("placeholder")),
                    multiline = c.optBoolean("multiline"),
                    maxLength = c.optInt("maxLength", 2000),
                    aspect = c.optDouble("aspect", 0.0).toFloat(),
                    text = texts(c.optJSONObject("text")),
                )
            },
        )
    }

    private fun action(o: JSONObject) = Action(
        id = o.optString("id"),
        title = texts(o.optJSONObject("title")),
        icon = o.optString("icon", "wand"),
        placement = places(o.optJSONArray("placement")),
        panel = o.optString("panel"),
        call = o.optString("call"),
        needsLayer = o.optBoolean("needsLayer"),
    )
}
