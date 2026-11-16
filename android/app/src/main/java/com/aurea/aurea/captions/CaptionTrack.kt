package com.aurea.aurea.captions
import org.json.JSONArray

/** [look]: estilo próprio do bloco (JSON das opções); nulo = o da faixa. */
data class CaptionBlock(val id: Long, val start: Int, val end: Int, val text: String, val look: String? = null)
/** [shift]: quadro local do bloco + shift = quadro da timeline (0.5.3). */
data class CaptionTrack(val layer: Long, val source: Long, val segments: List<CaptionBlock>, val shift: Int = 0)
fun parseCaptionTracks(json: String): List<CaptionTrack> = runCatching {
    val tracks = JSONArray(json)
    List(tracks.length()) { i ->
        val track = tracks.getJSONObject(i); val blocks = track.getJSONArray("segments")
        CaptionTrack(track.getLong("layer"), track.getLong("source"), List(blocks.length()) { j ->
            val b = blocks.getJSONObject(j); CaptionBlock(b.getLong("id"), b.getInt("start"), b.getInt("end"), b.getString("text"),
                b.optString("look").ifEmpty { null })
        }, track.optInt("shift", 0))
    }
}.getOrDefault(emptyList())
