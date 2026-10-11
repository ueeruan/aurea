package com.aurea.aurea.engine

import org.json.JSONObject

/** Descriptions of existing native bindings, never a second source of property values. */
data class BuiltinPropertyCondition(val bindingParam: Int, val equals: List<Float>)
data class BuiltinPropertyOption(val value: Int, val id: String, val label: String)
data class BuiltinPropertySpec(
    val id: String, val label: String, val type: Int, val components: Int,
    val bindingParams: List<Int>, val trackProperties: List<Int>, val defaultValue: List<Float>,
    val sliderMin: Float, val sliderMax: Float, val typedMin: Float, val typedMax: Float,
    val unit: String, val precision: Int, val group: String, val colorSpace: String,
    val defaultSource: String, val conditions: List<BuiltinPropertyCondition>,
    val options: List<BuiltinPropertyOption>,
) {
    fun visible(values: FloatArray): Boolean = conditions.all { condition ->
        values.getOrNull(condition.bindingParam)?.let { value -> condition.equals.any { it == value } } == true
    }

    fun value(values: FloatArray, component: Int = 0): Float = values.getOrElse(bindingParams[component]) { defaultValue[component] }
    fun track(domain: String, material: Int, component: Int): TrackKey? {
        val property = trackProperties[component]
        return if (property < 0) null else TrackKey(property,
            if (domain == "material") material else -1,
            if (domain == "material") bindingParams[component] else 0)
    }
}

class BuiltinPropertySchema private constructor(val panels: Map<String, List<BuiltinPropertySpec>>) {
    companion object {
        const val VERSION = 1

        /** A stale or invalid native contract produces an explicit unavailable panel. */
        fun parse(json: String): BuiltinPropertySchema? = runCatching {
            val root = JSONObject(json)
            require(root.getInt("version") == VERSION)
            val panels = root.getJSONArray("panels")
            val result = linkedMapOf<String, List<BuiltinPropertySpec>>()
            for (p in 0 until panels.length()) {
                val panel = panels.getJSONObject(p)
                val domain = panel.getString("domain")
                require(domain == "light" || domain == "material")
                require(domain !in result)
                val nativeCount = if (domain == "light") 11 else 6
                val used = hashSetOf<Int>()
                val ids = hashSetOf<String>()
                val properties = panel.getJSONArray("properties")
                result[domain] = List(properties.length()) { index ->
                    val row = properties.getJSONObject(index)
                    val id = row.getString("id")
                    require(id.isNotBlank() && ids.add(id))
                    val type = row.getInt("type")
                    require(type in listOf(ParamType.FLOAT, ParamType.BOOL, ParamType.COLOR, ParamType.ANGLE, ParamType.ENUM))
                    val components = row.getInt("components")
                    require(if (type == ParamType.COLOR) components in 3..4 else components == 1)
                    fun ints(name: String): List<Int> {
                        val array = row.getJSONArray(name)
                        require(array.length() == components)
                        return List(components) { array.getInt(it) }
                    }
                    val bindings = ints("bindingParams")
                    require(bindings.all { it in 0 until nativeCount && used.add(it) })
                    val tracks = ints("trackProperties")
                    require(tracks.all { it >= -1 })
                    val defaults = row.getJSONArray("defaultValue")
                    require(defaults.length() == components)
                    fun finite(name: String): Float = row.getDouble(name).toFloat().also { require(it.isFinite()) }
                    val sliderMin = finite("sliderMin"); val sliderMax = finite("sliderMax")
                    val typedMin = finite("typedMin"); val typedMax = finite("typedMax")
                    require(typedMin <= sliderMin && sliderMin <= sliderMax && sliderMax <= typedMax)
                    val precision = row.getInt("precision").also { require(it in 0..6) }
                    val conditions = row.getJSONArray("conditions")
                    val options = row.optJSONArray("options")
                    val spec = BuiltinPropertySpec(id, row.getString("label"), type, components, bindings, tracks,
                        List(components) { defaults.getDouble(it).toFloat().also { value -> require(value.isFinite()) } },
                        sliderMin, sliderMax, typedMin, typedMax, row.getString("unit"), precision,
                        row.getString("group"), row.getString("colorSpace"), row.getString("defaultSource"),
                        List(conditions.length()) { c ->
                            val condition = conditions.getJSONObject(c)
                            val binding = condition.getInt("bindingParam").also { require(it in 0 until nativeCount) }
                            val equals = condition.getJSONArray("equals")
                            require(equals.length() > 0)
                            BuiltinPropertyCondition(binding, List(equals.length()) { equals.getDouble(it).toFloat().also { v -> require(v.isFinite()) } })
                        }, List(options?.length() ?: 0) { o ->
                            val option = options!!.getJSONObject(o)
                            BuiltinPropertyOption(option.getInt("value"), option.getString("id"), option.getString("label"))
                        })
                    require(type != ParamType.ENUM || spec.options.isNotEmpty())
                    require(domain != "light" || type != ParamType.COLOR || components == 3)
                    spec
                }
                require(used.size == nativeCount)
            }
            require(result.keys == setOf("light", "material"))
            BuiltinPropertySchema(result)
        }.getOrNull()
    }
}
