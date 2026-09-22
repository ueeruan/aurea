package com.aurea.aurea.editor.timeline

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Waveform em grade fixa (fase 8D): quantas vezes a timeline pergunta ao motor
 * tocando e na pinça. ANTES = uma pergunta por linha de som visível A CADA
 * quadro (a conta do pintor antigo); DEPOIS = [WaveStrip].
 */
class TimelineWaveTest {
    private val fps = 30f
    private val density = 3f
    private val width = 1080f
    private val cx = width / 2f

    /** O motor falso: o valor do balde é função do frame (confere o conteúdo). */
    private val source = WaveSource { _, start, fpb, count, out ->
        for (i in 0 until count) out.put(i, ((start / fpb + i).toLong() and 0x7F).toByte())
        count
    }

    private fun visible(view: Double, ppf: Float, fpb: Double): LongArray =
        longArrayOf(
            WaveGrid.bucketAt(TimeAxis.frameAt(0f, view, ppf, cx), fpb),
            WaveGrid.bucketAt(TimeAxis.frameAt(width, view, ppf, cx), fpb),
        )

    @Test
    fun `tocando 60 s a 30 fps a janela serve varias telas`() {
        val strip = WaveStrip(2048)
        val ppf = TimeAxis.pxPerFrame(Zoom.DEFAULT_PPS, density, fps)
        val fpb = WaveGrid.framesPerBucket(density * 1.5f, ppf)
        val model = Any()
        val frames = 60 * 30
        for (f in 0 until frames) {
            val v = visible(f.toDouble(), ppf, fpb)
            val e = strip.get(source, 7L, model, 1, fpb, v[0], v[1])!!
            // O conteúdo é o do balde certo (grade absoluta).
            assertEquals((v[0] and 0x7F).toInt(), e.at(v[0]))
        }
        println("BENCH waveform tocando 60 s (1 linha): antes $frames consultas ao motor, depois ${strip.queries}")
        assertTrue(strip.queries * 20 < frames)
    }

    @Test
    fun `pinca so repergunta ao passar de degrau`() {
        val strip = WaveStrip(2048)
        val model = Any()
        var pps = 80f
        var draws = 0
        while (pps < 320f) {
            val ppf = TimeAxis.pxPerFrame(pps, density, fps)
            val fpb = WaveGrid.framesPerBucket(density * 1.5f, ppf)
            val v = visible(300.0, ppf, fpb)
            strip.get(source, 7L, model, 1, fpb, v[0], v[1])
            pps *= 1.01f
            draws++
        }
        println("BENCH waveform pinça 4x (1 linha): antes $draws consultas, depois ${strip.queries}")
        assertTrue(strip.queries <= 6)
    }

    @Test
    fun `modelo ou geracao novos repedem`() {
        val strip = WaveStrip(2048)
        val m1 = Any()
        strip.get(source, 1L, m1, 1, 2.0, 0, 100)
        strip.get(source, 1L, m1, 1, 2.0, 10, 110)
        assertEquals(1, strip.queries)
        strip.get(source, 1L, m1, 2, 2.0, 10, 110)
        assertEquals(2, strip.queries)
        strip.get(source, 1L, Any(), 2, 2.0, 10, 110)
        assertEquals(3, strip.queries)
        // Degraus de √2 e grade: 1,5 dp a 3x e 80 dp/s = 0,5625 frame → 0,5.
        assertEquals(0.5, WaveGrid.framesPerBucket(4.5f, 8f), 1e-9)
        assertEquals(-1L, WaveGrid.bucketAt(-0.1, 0.5))
    }
}
