package com.aurea.aurea.captions
import org.json.JSONArray

data class CaptionBlock(val id: Long, val start: Int, val end: Int, val text: String)
data class CaptionTrack(val layer: Long, val source: Long, val segments: List<CaptionBlock>)
fun parseCaptionTracks(json: String): List<CaptionTrack> = runCatching {
    val tracks = JSONArray(json)
    List(tracks.length()) { i ->
        val track = tracks.getJSONObject(i); val blocks = track.getJSONArray("segments")
        CaptionTrack(track.getLong("layer"), track.getLong("source"), List(blocks.length()) { j ->
            val b = blocks.getJSONObject(j); CaptionBlock(b.getLong("id"), b.getInt("start"), b.getInt("end"), b.getString("text"))
        })
    }
}.getOrDefault(emptyList())
