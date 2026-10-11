package com.aurea.aurea.engine

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class BuiltinPropertySchemaTest {
    // Parser fixtures describe a complete binding table; integration tests read the native schema.
    private fun fixture(): JSONObject {
        fun row(id: String, bindings: List<Int>, tracks: List<Int>, type: Int = ParamType.FLOAT): JSONObject = JSONObject()
            .put("id", id).put("label", id).put("type", type).put("components", bindings.size)
            .put("bindingParams", JSONArray(bindings)).put("trackProperties", JSONArray(tracks))
            .put("defaultValue", JSONArray(bindings.map { 0f })).put("sliderMin", 0).put("sliderMax", 1)
            .put("typedMin", 0).put("typedMax", 10).put("unit", "").put("precision", 3)
            .put("group", "light").put("colorSpace", if (type == ParamType.COLOR) "linear" else "none")
            .put("defaultSource", "layer").put("conditions", JSONArray())
        val light = JSONArray().put(row("color", listOf(2,3,4), listOf(22,23,24), ParamType.COLOR))
        for (binding in listOf(0,1,5,6,7,8,9,10)) light.put(row("p$binding", listOf(binding), listOf(if (binding == 6) 25 else -1)))
        val material = JSONArray().put(row("baseColor", listOf(0,1,2,3), List(4) {37}, ParamType.COLOR))
            .put(row("metallic", listOf(4), listOf(37))).put(row("roughness", listOf(5), listOf(37)))
        return JSONObject().put("version", 1).put("panels", JSONArray()
            .put(JSONObject().put("domain", "light").put("properties", light))
            .put(JSONObject().put("domain", "material").put("properties", material)))
    }
    @Test fun componentTracksPreserveLightAndMaterialIdentity() {
        val schema = BuiltinPropertySchema.parse(fixture().toString())!!
        val light = schema.panels.getValue("light").first()
        assertEquals(TrackKey(23,-1,0), light.track("light", -1, 1))
        val material = schema.panels.getValue("material").first()
        assertEquals(TrackKey(37,7,3), material.track("material",7,3))
        assertEquals(3, light.components)
        assertEquals(4, material.components)
    }
    @Test fun staleOrIncompleteSchemaDoesNotCreateInventedControls() {
        assertNull(BuiltinPropertySchema.parse(fixture().put("version", 2).toString()))
        val missing = fixture()
        missing.getJSONArray("panels").getJSONObject(0).getJSONArray("properties").remove(1)
        assertNull(BuiltinPropertySchema.parse(missing.toString()))
        val duplicated = fixture()
        duplicated.getJSONArray("panels").getJSONObject(0).getJSONArray("properties").getJSONObject(1)
            .put("bindingParams", JSONArray(listOf(2)))
        assertNull(BuiltinPropertySchema.parse(duplicated.toString()))
    }
    @Test fun nativeConditionsFollowCurrentValueWithoutCachedUiState() {
        val json = fixture()
        val cone = json.getJSONArray("panels").getJSONObject(0).getJSONArray("properties").getJSONObject(4)
        cone.put("conditions", JSONArray().put(JSONObject().put("bindingParam",0).put("equals",JSONArray(listOf(2)))))
        val property = BuiltinPropertySchema.parse(json.toString())!!.panels.getValue("light")[4]
        val values = FloatArray(11)
        assertFalse(property.visible(values))
        values[0] = 2f
        assertTrue(property.visible(values))
        values[0] = 1f
        assertFalse(property.visible(values))
    }
}
