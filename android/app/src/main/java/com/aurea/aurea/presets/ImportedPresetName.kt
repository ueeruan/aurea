package com.aurea.aurea.presets

/**
 * Name of an imported effects preset: [label] wraps the base name (the app passes the
 * localized "Importado · %1$s"). Reserve room for the suffix so a truncated duplicate
 * always makes progress.
 */
internal fun importedPresetName(
    base: String,
    label: (String) -> String = { "Importado · $it" },
    exists: (String) -> Boolean,
): String {
    val stem = label(base)
    var name = stem.take(60)
    var index = 2
    while (exists(name)) {
        val suffix = " ${index++}"
        name = stem.take(60 - suffix.length) + suffix
    }
    return name
}
