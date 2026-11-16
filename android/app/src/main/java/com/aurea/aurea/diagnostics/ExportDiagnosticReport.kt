package com.aurea.aurea.diagnostics

import android.content.Context
import android.os.Debug
import android.os.SystemClock
import android.util.AtomicFile
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/** Private evidence for the existing, user-triggered problem report. No upload,
 * project name, media, URI or file path is added to this record. */
object ExportDiagnosticReport {
    private const val FILE = "export-diagnostic.json"
    /**
     * Teto do arquivo LOCAL. 0.5.3: 3000 → 6000 (entraram a ficha do aparelho, a
     * escada e o estado do serviço); acima dele o relato é cortado campo a campo
     * ([fit]), nunca descartado.
     */
    const val LIMIT = 6000
    /** O que cabe no "Relatar um problema" (o Worker aceita 4000 nos passos; ProblemReport.STEPS_MAX). */
    const val REPORT_LIMIT = 3000
    /** 0.5.3 (P2): sem mudança de etapa, o arquivo é regravado no máximo a cada isto. */
    private const val WRITE_EVERY_MS = 10_000L
    private const val LADDER_MAX = 12
    private const val NOTE_MAX = 200
    private var generation = 0L
    private var previousToken = ""
    private val attempts = mutableListOf<JSONObject>()
    // 0.5.3 (P1-3): o que só a tela sabe deste export — a ficha do aparelho, os
    // fatos avulsos (serviço, degrau de memória, caminho do encoder) e a escada.
    private var device = JSONObject()
    private var run = JSONObject()
    private val ladder = mutableListOf<JSONObject>()
    private var notesDirty = false
    private var lastSignature = ""
    private var lastWriteMs = 0L
    private val keys = listOf("sessionToken", "elapsedMs", "sessionStage", "sessionWaitMs", "producerStage", "producerWaitMs",
        "encoderStage", "encoderWaitMs", "encoder", "backend", "candidateCount", "requestedColor", "codedWidth", "codedHeight",
        "width", "height", "fpsNumerator", "fpsDenominator", "bitrate", "requestedProfile", "safeMode", "rendered", "submitted",
        "videoPackets", "audioPackets", "inputAgain", "outputAgain", "firstInputMs", "firstPacketMs", "hasResult", "result", "negotiatedColor",
        // 0.5.2 (E4/E1): onde a partida parou e o ponto do quadro incompleto (ExportDiagnostics.hpp).
        "checkpoint", "endedFrom", "endedFromMs", "incompleteSite", "unresolvedSites")

    /** [gpu]: "GPU · driver" do motor (AureaEngine.deviceSummary); vazio = não informado. */
    @Synchronized fun begin(context: Context, previous: String, gpu: String = ""): Long {
        generation++
        previousToken = runCatching { JSONObject(previous).optString("sessionToken") }.getOrDefault("")
        attempts.clear()
        ladder.clear()
        run = JSONObject()
        notesDirty = false
        lastSignature = ""
        lastWriteMs = 0L
        device = runCatching { ExportDeviceFacts.fixed(context, gpu) }.getOrDefault(JSONObject())
        runCatching { AtomicFile(File(context.noBackupFilesDir, FILE)).delete() }
        return generation
    }

    /**
     * 0.5.3 (P1-3): um fato deste export que o motor não vê (chave curta: "fgs"
     * serviço em primeiro plano, "step" degrau de memória, "enc" caminho do
     * encoder…). Texto passa pelo filtro de caminhos. Entra na próxima gravação.
     */
    @Synchronized fun note(ticket: Long, key: String, value: Any) {
        if (ticket != generation) return
        val clean: Any = if (value is String) ProblemReport.sanitize(value).take(NOTE_MAX) else value
        if (run.opt(key) == clean) return
        run.put(key, clean)
        notesDirty = true
    }

    /**
     * 0.5.3 (P1-3): uma tentativa da escada acabou — degrau, código (Errc; 0 =
     * pronto), motivo (ExportFailure), o quadro em que parou, os bits do progresso
     * (Engine::ExportFlag) e uma palavra de por quê. As [LADDER_MAX] mais recentes.
     */
    @Synchronized fun noteAttempt(ticket: Long, level: Int, code: Int, failure: Int, frame: Int, total: Int, flags: Int, why: String) {
        if (ticket != generation) return
        ladder.add(JSONObject().put("l", level).put("c", code).put("f", failure).put("at", frame).put("of", total)
            .put("fl", flags).put("w", why.take(24)))
        while (ladder.size > LADDER_MAX) ladder.removeAt(0)
        notesDirty = true
    }

    /** A amostra do motor que vale para este export, só com as chaves do relato; null = não serve. */
    private fun sampleOf(native: String): JSONObject? {
        val input = runCatching { JSONObject(native) }.getOrNull() ?: return null
        if (input.optInt("schema") != 1 || input.optBoolean("busy") || !input.has("elapsedMs")) return null
        val token = input.optString("sessionToken")
        if (token.isBlank() || token == previousToken) return null
        val sample = JSONObject()
        keys.forEach { key -> if (input.has(key)) sample.put(key, input.get(key)) }
        return sample
    }

    /**
     * Uma leitura do diagnóstico do motor. 0.5.3: (1) a amostra é opcional — uma
     * partida que falha antes de a sessão existir ainda grava a escada e a ficha
     * do aparelho; (2) a leitura continua a 1 Hz, mas o arquivo só é regravado
     * quando a etapa muda, quando a tela anotou algo, a cada [WRITE_EVERY_MS] ou
     * com [force] (o fim de cada tentativa). Fora da main thread.
     */
    @Synchronized fun record(context: Context, ticket: Long, native: String, force: Boolean = false) {
        if (ticket != generation) return
        runCatching {
            val sample = sampleOf(native)
            if (sample != null) {
                val token = sample.optString("sessionToken")
                val existing = attempts.indexOfFirst { it.optString("sessionToken") == token }
                if (existing >= 0) attempts[existing] = sample else attempts.add(sample)
                while (attempts.size > 3) attempts.removeAt(0)
            }
            if (attempts.isEmpty() && ladder.isEmpty()) return
            val now = SystemClock.elapsedRealtime()
            val signature = signatureOf(attempts.lastOrNull(), attempts.size)
            if (!shouldWrite(force, notesDirty || signature != lastSignature, now - lastWriteMs)) return
            val runtime = Runtime.getRuntime()
            val memory = ExportDeviceFacts.ram(context, JSONObject().put("javaMaxBytes", runtime.maxMemory())
                .put("javaUsedBytes", runtime.totalMemory() - runtime.freeMemory())
                .put("nativeHeapBytes", Debug.getNativeHeapAllocatedSize()))
            val text = compose(device, memory, ExportDeviceFacts.disk(context), ExportDeviceFacts.power(context),
                run, ladder, attempts, LIMIT)
            val file = AtomicFile(File(context.noBackupFilesDir, FILE))
            val stream = file.startWrite()
            try { stream.write(text.toByteArray(Charsets.UTF_8)); file.finishWrite(stream) }
            catch (error: Throwable) { file.failWrite(stream); throw error }
            lastSignature = signature
            lastWriteMs = now
            notesDirty = false
        }
    }

    /** 0.5.3 (P2): regrava com [force], quando algo mudou, ou passado o intervalo. */
    internal fun shouldWrite(force: Boolean, changed: Boolean, sinceLastWriteMs: Long): Boolean =
        force || changed || sinceLastWriteMs >= WRITE_EVERY_MS

    /** O que conta como "mudou": sessão, etapas, codec e resultado — não os contadores nem os relógios. */
    internal fun signatureOf(sample: JSONObject?, attemptCount: Int): String =
        signatureKeys.joinToString("|") { key -> sample?.opt(key)?.toString().orEmpty() } + "#" + attemptCount

    private val signatureKeys = listOf("sessionToken", "sessionStage", "producerStage", "encoderStage", "encoder", "backend",
        "safeMode", "hasResult", "result", "checkpoint", "endedFrom", "incompleteSite")

    /** Monta o relato e o faz caber em [limit] ([fit]). Só JSON: testável na JVM. */
    internal fun compose(device: JSONObject, memory: JSONObject, disk: JSONObject, power: JSONObject, run: JSONObject,
                         ladder: List<JSONObject>, attempts: List<JSONObject>, limit: Int): String {
        val report = JSONObject().put("schema", 1).put("device", device).put("memory", memory)
        if (disk.length() > 0) report.put("disk", disk)
        if (power.length() > 0) report.put("power", power)
        report.put("outputColor", "8-bit SDR BT.709 limited")
        if (run.length() > 0) report.put("run", run)
        if (ladder.isNotEmpty()) report.put("ladder", JSONArray(ladder))
        report.put("attempts", JSONArray(attempts))
        return fit(report, limit)
    }

    /** De uma tentativa antiga fica a fronteira da falha e o codec escolhido. */
    private val boundaryKeys = listOf("safeMode", "encoder", "backend", "requestedColor", "elapsedMs", "encoderStage", "result",
        "firstPacketMs")
    /** Campos avulsos na ordem em que saem: do que menos ajuda a triagem ao que mais ajuda. */
    private val fitOrder = listOf("device" to "buildId", "device" to "androidVersion", "device" to "abis", "device" to "board",
        "memory" to "javaMaxBytes", "memory" to "javaUsedBytes", "device" to "cpuHw", "device" to "roHw", "device" to "roSoc",
        "device" to "memClass", "memory" to "nativeHeapBytes", "device" to "plat", "device" to "hardware", "run" to "enc")
    private val blockOrder = listOf("outputColor", "disk", "power", "memory", "run", "ladder", "device", "attempts")

    private fun boundary(item: JSONObject): JSONObject {
        val old = JSONObject()
        boundaryKeys.forEach { key -> if (item.has(key)) old.put(key, item.get(key)) }
        return old
    }

    /**
     * 0.5.3 (P1-3): faz o relato caber em [limit] cortando CAMPO A CAMPO. Antes,
     * um relato acima do teto simplesmente não era gravado (sumia calado). A
     * ordem: tentativas antigas resumidas → campos avulsos → tentativas antigas
     * → degraus antigos da escada → a última tentativa resumida → blocos
     * inteiros. O que saiu fica listado em "cut". Não mexe no [report] recebido.
     */
    internal fun fit(report: JSONObject, limit: Int): String {
        var text = report.toString()
        if (text.length <= limit) return text
        val r = JSONObject(text)
        // Um relato já cortado uma vez (o arquivo local) mantém a lista do que saiu.
        val cut = r.optJSONArray("cut") ?: JSONArray()
        r.put("cut", cut)
        fun fits(what: String): Boolean { cut.put(what); text = r.toString(); return text.length <= limit }
        val attempts = r.optJSONArray("attempts") ?: JSONArray()
        if (attempts.length() > 1) {
            for (i in 0 until attempts.length() - 1) attempts.put(i, boundary(attempts.getJSONObject(i)))
            if (fits("attempts.old")) return text
        }
        for ((owner, key) in fitOrder) {
            val o = r.optJSONObject(owner) ?: continue
            if (!o.has(key)) continue
            o.remove(key)
            if (fits("$owner.$key")) return text
        }
        if (attempts.length() > 1) {
            while (attempts.length() > 1) attempts.remove(0)
            if (fits("attempts.older")) return text
        }
        val ladder = r.optJSONArray("ladder")
        if (ladder != null && ladder.length() > 1) {
            while (ladder.length() > 1) ladder.remove(0)
            if (fits("ladder.older")) return text
        }
        if (attempts.length() == 1) {
            attempts.put(0, boundary(attempts.getJSONObject(0)))
            if (fits("attempts.last")) return text
        }
        for (block in blockOrder) {
            if (!r.has(block)) continue
            r.remove(block)
            if (fits(block)) return text
        }
        return JSONObject().put("schema", 1).put("cut", JSONArray().put("all")).toString()
    }

    /** O relato guardado dentro de [limit] (o corpo do "Relatar um problema"); o que já cabe passa intacto. */
    fun fitText(text: String, limit: Int): String =
        if (text.length <= limit) text else runCatching { fit(JSONObject(text), limit) }.getOrElse { text.take(limit) }

    @Synchronized fun last(context: Context): String? = runCatching {
        AtomicFile(File(context.noBackupFilesDir, FILE)).openRead().use {
            if (it.channel.size() > LIMIT * 4L) return@runCatching null
            it.bufferedReader().readText()
        }
            .takeIf { it.length <= LIMIT && JSONObject(it).optInt("schema") == 1 }
    }.getOrNull()
}
