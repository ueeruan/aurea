package com.aurea.aurea.diagnostics

import android.content.Context
import android.os.Build
import android.os.Debug
import android.os.Process
import android.util.AtomicFile
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/** Private evidence for the existing, user-triggered problem report. No upload,
 * project name, media, URI or file path is added to this record. */
object ExportDiagnosticReport {
    private const val FILE = "export-diagnostic.json"
    private const val LIMIT = 3000
    private var generation = 0L
    private var previousToken = ""
    private val attempts = mutableListOf<JSONObject>()
    private val keys = listOf("sessionToken", "elapsedMs", "sessionStage", "sessionWaitMs", "producerStage", "producerWaitMs",
        "encoderStage", "encoderWaitMs", "encoder", "backend", "candidateCount", "requestedColor", "codedWidth", "codedHeight",
        "width", "height", "fpsNumerator", "fpsDenominator", "bitrate", "requestedProfile", "safeMode", "rendered", "submitted",
        "videoPackets", "audioPackets", "inputAgain", "outputAgain", "firstInputMs", "firstPacketMs", "hasResult", "result", "negotiatedColor")

    @Synchronized fun begin(context: Context, previous: String): Long {
        generation++
        previousToken = runCatching { JSONObject(previous).optString("sessionToken") }.getOrDefault("")
        attempts.clear()
        runCatching { AtomicFile(File(context.noBackupFilesDir, FILE)).delete() }
        return generation
    }

    @Synchronized fun record(context: Context, ticket: Long, native: String) {
        if (ticket != generation) return
        runCatching {
            val input = JSONObject(native)
            if (input.optInt("schema") != 1 || input.optBoolean("busy") || !input.has("elapsedMs")) return
            val token = input.optString("sessionToken")
            if (token.isBlank() || token == previousToken) return
            val sample = JSONObject()
            keys.forEach { key -> if (input.has(key)) sample.put(key, input.get(key)) }
            val existing = attempts.indexOfFirst { it.optString("sessionToken") == token }
            if (existing >= 0) attempts[existing] = sample else attempts.add(sample)
            while (attempts.size > 3) attempts.removeAt(0)
            val runtime = Runtime.getRuntime()
            val device = JSONObject().put("manufacturer", Build.MANUFACTURER.take(80)).put("model", Build.MODEL.take(120))
                .put("androidSdk", Build.VERSION.SDK_INT).put("androidVersion", Build.VERSION.RELEASE.take(40))
                .put("buildId", Build.ID.take(80)).put("hardware", Build.HARDWARE.take(80)).put("board", Build.BOARD.take(80))
                .put("process64Bit", Process.is64Bit()).put("abis", JSONArray(Build.SUPPORTED_ABIS.toList()))
                .put("socManufacturer", if (Build.VERSION.SDK_INT >= 31) Build.SOC_MANUFACTURER.take(80) else JSONObject.NULL)
                .put("socModel", if (Build.VERSION.SDK_INT >= 31) Build.SOC_MODEL.take(80) else JSONObject.NULL)
            val report = JSONObject().put("schema", 1).put("device", device)
                .put("memory", JSONObject().put("javaMaxBytes", runtime.maxMemory())
                    .put("javaUsedBytes", runtime.totalMemory() - runtime.freeMemory())
                    .put("nativeHeapBytes", Debug.getNativeHeapAllocatedSize()))
                .put("outputColor", "8-bit SDR BT.709 limited")
                .put("attempts", JSONArray(attempts))
            var text = report.toString()
            // Keep the most recent attempt complete; older attempts retain the
            // failure boundary and selected codec, even when space is tight.
            if (text.length > LIMIT) {
                val reduced = JSONArray()
                attempts.forEachIndexed { index, item ->
                    if (index == attempts.lastIndex) reduced.put(item) else {
                        val old = JSONObject()
                        listOf("safeMode", "encoder", "backend", "requestedColor", "elapsedMs", "encoderStage", "result", "firstPacketMs")
                            .forEach { key -> if (item.has(key)) old.put(key, item.get(key)) }
                        reduced.put(old)
                    }
                }
                report.put("attempts", reduced); text = report.toString()
            }
            if (text.length > LIMIT) return
            val file = AtomicFile(File(context.noBackupFilesDir, FILE))
            val stream = file.startWrite()
            try { stream.write(text.toByteArray(Charsets.UTF_8)); file.finishWrite(stream) }
            catch (error: Throwable) { file.failWrite(stream); throw error }
        }
    }

    @Synchronized fun last(context: Context): String? = runCatching {
        AtomicFile(File(context.noBackupFilesDir, FILE)).openRead().use {
            if (it.channel.size() > LIMIT * 4L) return@runCatching null
            it.bufferedReader().readText()
        }
            .takeIf { it.length <= LIMIT && JSONObject(it).optInt("schema") == 1 }
    }.getOrNull()
}
