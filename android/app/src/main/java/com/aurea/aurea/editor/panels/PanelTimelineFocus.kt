package com.aurea.aurea.editor.panels

import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.engine.TrackProperty

/** A size row edits two axes; other shape rows edit one native parameter. */
internal fun shapePanelTimelineFocus(param: Int): List<TrackKey> =
    (if (param == 5 || param == 6) listOf(5, 6) else listOf(param))
        .filter { it in 1..14 }.map { TrackKey(TrackProperty.SHAPE_PARAM, 0, it) }

/** Native queryPuppet contains only active pins: [index, x, y, keyHere]. */
internal fun puppetPanelTimelineFocus(effect: Int, selectedPin: Int, pins: FloatArray): List<TrackKey> {
    if (effect < 0) return emptyList()
    val active = pins.asList().chunked(4).filter { it.size == 4 }
        .mapNotNull { row -> row[0].takeIf { it.isFinite() && it == it.toInt().toFloat() }?.toInt() }
        .filter { it in 0..15 }.distinct()
    val chosen = if (selectedPin in active) listOf(selectedPin) else active
    return chosen.flatMap { pin ->
        val position = (5 + pin * 3) * 4
        listOf(TrackKey(TrackProperty.EFFECT_PARAM, effect, position), TrackKey(TrackProperty.EFFECT_PARAM, effect, position + 1))
    }
}
