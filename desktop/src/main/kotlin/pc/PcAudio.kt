package com.aurea.pc

import java.io.File
import javax.sound.sampled.AudioFormat
import javax.sound.sampled.AudioSystem
import javax.sound.sampled.DataLine
import javax.sound.sampled.SourceDataLine
import javax.sound.sampled.TargetDataLine

/**
 * Prévia de áudio fora do projeto (biblioteca de sons, narração gerada): o
 * FFmpeg decodifica para PCM 48 kHz estéreo e o Java Sound toca.
 */
class PcAudioPreview {
    @Volatile var volume = 1f
    @Volatile var looping = false
    @Volatile var playing = false; private set
    var onEnd: (() -> Unit)? = null
    private var pcm: ByteArray = ByteArray(0)
    @Volatile private var pos = 0
    private var thread: Thread? = null
    private var line: SourceDataLine? = null
    private val fmt = AudioFormat(48000f, 16, 2, true, false)

    val durationMs: Long get() = pcm.size / 4L * 1000 / 48000
    val positionMs: Long get() = pos / 4L * 1000 / 48000

    fun open(f: File) {
        val p = ProcessBuilder(ffmpegPath(), "-v", "error", "-i", f.absolutePath, "-f", "s16le", "-ac", "2", "-ar", "48000", "-")
            .redirectError(ProcessBuilder.Redirect.DISCARD).start()
        pcm = p.inputStream.readBytes()
        p.waitFor()
        pos = 0
    }

    private fun ffmpegPath(): String = listOf(PcPaths.appDir, PcPaths.appDir.parentFile?.let { File(it, "windows") })
        .filterNotNull().map { File(it, "ffmpeg.exe") }.firstOrNull { it.isFile }?.absolutePath ?: "ffmpeg"

    @Synchronized fun play() {
        if (playing || pcm.isEmpty()) return
        playing = true
        thread = Thread({
            val l = (AudioSystem.getLine(DataLine.Info(SourceDataLine::class.java, fmt)) as SourceDataLine).also { line = it }
            l.open(fmt, 48000); l.start()
            val buf = ByteArray(4096)
            while (playing) {
                if (pos >= pcm.size) { if (looping) pos = 0 else break }
                val n = minOf(buf.size, pcm.size - pos)
                System.arraycopy(pcm, pos, buf, 0, n)
                if (volume < 0.999f) for (i in 0 until n step 2) {
                    val s = ((buf[i].toInt() and 0xFF) or (buf[i + 1].toInt() shl 8)) * volume
                    val v = s.toInt().coerceIn(-32768, 32767)
                    buf[i] = v.toByte(); buf[i + 1] = (v shr 8).toByte()
                }
                l.write(buf, 0, n)
                pos += n
            }
            l.drain(); l.close()
            val ended = pos >= pcm.size
            playing = false
            if (ended) onEnd?.invoke()
        }, "aurea-audio-preview").apply { isDaemon = true; start() }
    }

    fun pause() { playing = false }
    fun stop() { playing = false; pos = 0 }
    fun seek(ms: Long) { pos = ((ms * 48000 / 1000) * 4).toInt().coerceIn(0, pcm.size) and 3.inv() }
    fun release() { stop(); pcm = ByteArray(0) }
}

/** Gravação de voz pelo microfone padrão do Windows (AAC/M4A pelo FFmpeg no fim). */
class PcMicrophone {
    private var line: TargetDataLine? = null
    private var thread: Thread? = null
    @Volatile private var paused = false
    @Volatile private var peak = 0
    private val fmt = AudioFormat(48000f, 16, 1, true, false)

    fun start(out: File) {
        val l = AudioSystem.getTargetDataLine(fmt).also { line = it }
        l.open(fmt); l.start()
        val raw = File(out.path + ".pcm")
        thread = Thread({
            raw.outputStream().buffered().use { o ->
                val buf = ByteArray(4800)
                while (l.isOpen) {
                    val n = l.read(buf, 0, buf.size)
                    if (n <= 0) break
                    if (paused) continue
                    var m = 0
                    for (i in 0 until n - 1 step 2) m = maxOf(m, kotlin.math.abs(((buf[i].toInt() and 0xFF) or (buf[i + 1].toInt() shl 8)).toShort().toInt()))
                    peak = maxOf(peak, m)
                    o.write(buf, 0, n)
                }
            }
            val ff = listOf(PcPaths.appDir, PcPaths.appDir.parentFile?.let { File(it, "windows") }).filterNotNull()
                .map { File(it, "ffmpeg.exe") }.firstOrNull { it.isFile }?.absolutePath ?: "ffmpeg"
            ProcessBuilder(ff, "-y", "-v", "error", "-f", "s16le", "-ar", "48000", "-ac", "1", "-i", raw.absolutePath, "-c:a", "aac", "-b:a", "160k", out.absolutePath)
                .redirectErrorStream(true).start().waitFor()
            raw.delete()
        }, "aurea-mic").apply { isDaemon = true; start() }
    }

    fun pause() { paused = true }
    fun resume() { paused = false }
    fun stop() {
        line?.let { it.stop(); it.close() }
        line = null
        thread?.join(10_000)
        thread = null
    }
    fun maxAmplitude(): Int = peak.also { peak = 0 }
}
