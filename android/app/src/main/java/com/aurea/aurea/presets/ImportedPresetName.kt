package com.aurea.aurea.presets

/** Reserve room for the suffix so a truncated duplicate always makes progress. */
internal fun importedPresetName(base: String, exists: (String) -> Boolean): String {
    val stem = "AM · $base"
    var name = stem.take(60)
    var index = 2
    while (exists(name)) {
        val suffix = " ${index++}"
        name = stem.take(60 - suffix.length) + suffix
    }
    return name
}
