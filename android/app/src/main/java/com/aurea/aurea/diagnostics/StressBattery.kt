package com.aurea.aurea.diagnostics

import android.app.Application
import android.net.Uri
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.engine.DeviceProfile
import com.aurea.aurea.engine.PerfStats
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import kotlinx.coroutines.delay
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.max

/**
 * Bateria de estresse — o teste que o beta tester roda no aparelho DELE.
 *
 * Por que existe: os defeitos de P0 (playback travando, 3D piscando, texto 3D
 * escuro) não se reproduzem no host nem no emulador. O que dá para fazer sem
 * aparelho é montar uma bateria roteirizada, medir com o que o motor JÁ mede
 * (`PerfPOD`: decode, cpu, gpu, present, acquire, pacing p50/p95/p99, frames
 * perdidos, `hardwareDecoder`, `zeroCopy`, `staleFrames`, `audioUnderruns`,
 * texturas criadas em regime, escala adaptativa) e devolver TEXTO que o tester
 * copia e manda de volta.
 *
 * Duas coisas aqui não saem do `PerfPOD`, e são medidas por pixel, que é o
 * único jeito de provar:
 *
 *  - o BRILHO da face do texto 3D (foi assim que apareceu o 140 no iOS contra
 *    242 no Android e 244 no host); e
 *  - a PISCADA: mover a camada quadro a quadro e olhar cada quadro capturado.
 *    Quadro idêntico ao anterior com o objeto em movimento é quadro repetido;
 *    contagem de pixels acesos despencando é o objeto sumindo.
 *
 * Nada aqui salva por cima de projeto do tester: todo trabalho acontece em
 * projeto NOVO, e o projeto anterior é reaberto no fim.
 */
class StressBattery(
    private val app: Application,
    private val store: EditorStore,
    private val engine: AureaEngine,
    private val version: Pair<String, Long>,
    /** Vídeo escolhido pelo tester no seletor. Nulo = usa o que o próprio teste exporta. */
    private val videoDoUsuario: Uri? = null,
    private val onProgress: (String) -> Unit,
) {
    private val out = StringBuilder()
    private val capturasMs = mutableListOf<Float>()
    private val medidas = mutableListOf<Medida>()
    private val medidaBuf = DoubleArray(20)
    private val falhas = mutableListOf<String>()

    /**
     * O relatório vai para o arquivo LINHA A LINHA.
     *
     * É a parte que faz o teste servir para o que ele existe: se o app travar ou
     * fechar no meio, o arquivo já tem tudo até o passo anterior — e é o passo
     * anterior que diz onde travou. Um relatório montado só em memória morre
     * junto com o processo e não sobra nada para consertar.
     */
    val arquivo: File = File(File(app.filesDir, "diagnostico").apply { mkdirs() },
                             "estresse-" + SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(Date()) + ".txt")
    private val escritor = arquivo.bufferedWriter()
    private var passoAtual = "(inicio)"
    private var handlerAnterior: Thread.UncaughtExceptionHandler? = null

    private fun line(text: String = "") {
        out.append(text).append('\n')
        runCatching { escritor.write(text); escritor.newLine(); escritor.flush() }
    }

    private fun step(text: String) {
        passoAtual = text
        line("[passo] $text")
        onProgress(text)
    }

    /// Marca o passo que estava em curso quando o app cair.
    private fun instalarRedeDeQueda() {
        handlerAnterior = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { t, e ->
            runCatching {
                escritor.write("!!! O APP CAIU DURANTE: $passoAtual\n")
                escritor.write("${t.name}: ${e::class.java.name}: ${e.message}\n")
                e.stackTrace.forEach { escritor.write("    at $it\n") }
                escritor.flush()
                escritor.close()
            }
            handlerAnterior?.uncaughtException(t, e)
        }
    }

    private fun verdict(nome: String, ok: Boolean, detalhe: String) {
        line("JULGAMENTO: ${if (ok) "OK" else "FALHA"} — $detalhe")
        if (!ok) falhas += "$nome: $detalhe"
    }

    /**
     * O `PerfPOD` só é escrito pelo laço de prévia ao vivo (`update_perf`), que
     * precisa de superfície. Rodando de Ajustes ele sai todo zero — e zero aqui
     * significa "não medido", não "ruim". Julgar FALHA em cima disso acusava o
     * aparelho de um defeito que ninguém mediu.
     */
    private fun semMedicao(): Boolean {
        val p = perf()
        return p.previewWidth == 0 && p.pacingSamples == 0 && p.gpuName.isBlank()
    }

    private fun verdictMedido(nome: String, ok: Boolean, detalhe: String) {
        if (semMedicao()) {
            line("JULGAMENTO: NAO MEDIDO — $nome (sem previa viva, o POD saiu zero)")
            return
        }
        verdict(nome, ok, detalhe)
    }

    suspend fun run(): String {
        val textoAnterior = store.project.path
        instalarRedeDeQueda()
        engine.setOffscreenTimers(true)
        try {
            cabecalho()
            playback2d()
            videoReal()
            texto3d()
            particulas()
            memoria()
            pesado()
            bruto()
        } catch (t: Throwable) {
            line()
            line("A BATERIA PAROU: ${t::class.java.simpleName}: ${t.message}")
            falhas += "bateria interrompida: ${t.message}"
        } finally {
            if (!textoAnterior.isNullOrBlank()) runCatching { store.openProjectHeadless(textoAnterior) }
            Thread.setDefaultUncaughtExceptionHandler(handlerAnterior)
            runCatching { engine.setOffscreenTimers(false) }
        }
        rodape()
        runCatching { escritor.flush() }
        return out.toString()
    }

    // --- 0) o aparelho, medido ---------------------------------------------------

    private fun cabecalho() {
        line("AUREA — TESTE DE ESTRESSE")
        line("gerado: " + SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.ROOT).format(Date()))
        line("app: ${version.first} (build ${version.second})")
        val d = engine.deviceReport()
        if (d == null) {
            line("aparelho: (o motor ainda não reportou)")
        } else {
            line("aparelho: ${d.totalCores} núcleos (${d.performanceCores} fortes + ${d.efficiencyCores} fracos)")
            line("RAM do aparelho: ${d.totalMemoryMb} MB, disponível ${d.availableMemoryMb} MB")
        }
        line("resumo do motor: ${engine.deviceSummary().ifBlank { "(vazio)" }}")
        line("gpu: ${perf().gpuName.ifBlank { "(nao medido: o POD so enche com a previa viva)" }}")
        line(
            "decoder: ${perf().decoder.ifBlank { "?" }}  " +
                "hardware=${sim(perf().hardwareDecoder)}  zeroCopy=${sim(perf().zeroCopy)}"
        )
        line("termal: ${perf().thermal} (0 = frio)")
        line("orcamento de memoria do motor: ${perf().memoryBudgetMB} MB")
        codecs()
        line()
    }

    /**
     * O que o aparelho OFERECE para decodificar.
     *
     * É o dado que responde à pergunta "caiu para software?" sem depender do
     * `PerfPOD`, que fora do editor sai zero. A tabela vem da sondagem que o app
     * já faz na primeira abertura — sete inteiros por linha: tag, é_encoder,
     * é_hardware, largura, altura, bits, instâncias.
     */
    private fun codecs() {
        val probe = runCatching { DeviceProfile.probe(app) }.getOrNull()
        if (probe == null) {
            line("decodificadores: a sondagem não rodou")
            return
        }
        val c = probe.codecs
        val avc = StringBuilder()
        val hevc = StringBuilder()
        var at = 0
        while (at + 7 <= c.size) {
            val tag = c[at]
            val encoder = c[at + 1] == 1
            val hardware = c[at + 2] == 1
            val w = c[at + 3]
            val h = c[at + 4]
            val nome = when (tag) {
                0x61766331 -> "avc1"
                0x68766331 -> "hvc1"
                0x61763031 -> "av01"
                0x76703039 -> "vp09"
                else -> null
            }
            if (!encoder && nome != null) {
                val destino = if (nome == "avc1") avc else hevc
                destino.append(nome)
                    .append(if (hardware) " HARDWARE" else " software")
                    .append(" ${w}x$h; ")
            }
            at += 7
        }
        line("decodificadores do aparelho: ${if (avc.isEmpty()) "NENHUM H.264" else avc}")
        if (hevc.isNotEmpty()) line("  HEVC: $hevc")
    }

    // --- 1) playback 2D com efeitos ---------------------------------------------

    private suspend fun playback2d() {
        step("1/5 playback 2D com efeitos")
        line("--- 1) PLAYBACK 2D + EFEITOS (1080p30, 6 s)")
        if (!store.newProjectHeadless(1920, 1080, 30f, "Estresse 2D")) { line("nao consegui criar o projeto de teste; parando"); falhas += "newProject falhou"; return }
        delay(200)
        store.addText()
        delay(150)
        val ids = doisEfeitosPesados()
        for (id in ids) {
            store.addEffect(id)
            delay(60)
        }
        line("efeitos aplicados: ${ids.size}")
        val amostras = toca(segundos = 6)
        relataAmostras(amostras)
        // 33,3 ms por quadro a 30 fps; p99 acima do dobro disso é engasgo visível.
        val p99 = percentil(amostras.map { it.gpuFrameMs + it.cpuFrameMs }, 99)
        verdictMedido(
            "playback 2D", p99 in 0.01f..(2f * 33.3f),
            "cpu+gpu p99 = %.1f ms por quadro (orcamento 33,3 ms a 30 fps)".format(p99)
        )
        val queda = if (amostras.size >= 2) amostras.last().droppedFrames - amostras.first().droppedFrames else 0
        verdictMedido("frames perdidos", queda <= 0, "frames perdidos na janela = $queda")
    }

    // --- 2) vídeo de verdade: exporta e reproduz --------------------------------

    /**
     * Sem mídia do tester na mão, o vídeo sai do próprio motor: exporta 3 s em
     * H.264 e importa de volta. É o único jeito de exercitar MediaCodec/
     * VideoToolbox, o caminho de decodificação por hardware e o zero-copy sem
     * pedir arquivo emprestado.
     */
    private suspend fun videoReal() {
        step("2/5 vídeo real: exporta e reproduz")
        line()
        line("--- 2) VÍDEO REAL: EXPORTA E REPRODUZ")
        val dir = File(app.cacheDir, "estresse").apply { mkdirs() }
        dir.listFiles()?.forEach { it.delete() }
        val file = File(dir, "estresse.mp4")
        val code = engine.startExport(file.absolutePath, 1080, 0.0, 0, 0)
        if (code != 0) {
            line("não consegui exportar (código $code); pulando a fase de vídeo")
            falhas += "export do próprio teste falhou com código $code"
            return
        }
        val prog = ExportProgress()
        val espera = System.currentTimeMillis()
        while (System.currentTimeMillis() - espera < 120_000) {
            delay(120)
            if (!engine.exportProgress(progresso)) continue
            prog.readFrom(progresso)
            if (prog.finished) break
        }
        if (!file.exists() || file.length() < 1024) {
            line("o export não gerou arquivo utilizável; pulando")
            falhas += "export do próprio teste não gerou MP4"
            return
        }
        val segundos = (System.currentTimeMillis() - espera) / 1000.0
        // O encoder muda de aparelho para aparelho, e o tamanho do arquivo muda
        // junto: 50 KB num e 2604 KB noutro é encoder diferente, não cena
        // diferente. Sem este campo o relatório não distingue os dois.
        line(
            "exportado: ${file.length() / 1024} KB em H.264 1080p em %.1f s".format(segundos) +
                if (prog.hardwareEncoder) " (encoder de HARDWARE)" else if (prog.softwareEncoder) " (encoder de SOFTWARE)" else " (encoder nao informado)"
        )
        if (prog.thermalReduced) line("!! o export reduziu por calor (thermalReduced)")

        // Projeto novo com o vídeo de volta, para o decode ser exercitado de verdade.
        if (!store.newProjectHeadless(1920, 1080, 30f, "Estresse video")) { line("nao consegui criar o projeto de video; parando"); falhas += "newProject de video falhou"; return }
        delay(200)
        store.importVideo(android.net.Uri.fromFile(file))
        delay(600)
        line("decoder em uso: ${perf().decoder.ifBlank { "?" }}")
        val amostras = toca(segundos = 6)
        relataAmostras(amostras)
        val dec = percentil(amostras.map { it.decodeMs }, 95)
        verdictMedido(
            "decodificação por hardware", perf().hardwareDecoder,
            "decoder = ${perf().decoder.ifBlank { "sem nome" }}"
        )
        verdictMedido(
            "zero-copy do quadro decodificado", perf().zeroCopy,
            if (perf().zeroCopy) "sem passar pela CPU" else "o quadro está passando por RGBA na CPU"
        )
        verdictMedido("decode", dec < 33.3f, "decode p95 = %.1f ms".format(dec))
        val velhos = amostras.sumOf { it.staleFrames }
        verdictMedido("frames velhos", velhos <= 0, "quadros apresentados fora do tempo = $velhos")
        val coalescidos = amostras.lastOrNull()?.coalesced ?: 0
        line("seeks coalescidos: $coalescidos")
        val under = amostras.sumOf { it.audioUnderruns }
        verdictMedido("áudio contínuo", under <= 0, "underruns = $under")

        // O azul do vídeo aparece "do nada" — durante playback parado ninguém vê.
        // Aqui o vídeo é scrubado com força e cada quadro capturado é medido:
        // quanto o azul domina. É a única forma de flagrar sem aparelho na mão.
        step("2/5 vídeo: procurando o azul")
        var azuis = 0
        var pior = 0.0
        var primeiroAzul = -1
        var i = 0
        val tAzul = System.currentTimeMillis()
        while (System.currentTimeMillis() - tAzul < 15_000) {
            store.seek((i * 7) % 60)
            if (i % 5 == 0) delay(16)
            val q = captura(160) ?: continue
            val c = cores(q.bytes)
            val azul = indiceAzul(c)
            if (azul > 0.12) {
                if (primeiroAzul < 0) primeiroAzul = i
                azuis++
            }
            if (azul > pior) pior = azul
            i++
        }
        line("quadros olhados no scrub: $i, azuis = $azuis, pior indice = %.3f".format(pior))
        if (primeiroAzul >= 0) line("    o primeiro quadro azul foi o de indice $primeiroAzul")
        verdict("vídeo não pode ficar azul", azuis == 0, "quadros azuis = $azuis, pior = %.3f".format(pior))
    }

    // --- 3) texto 3D: brilho e piscada ------------------------------------------

    /**
     * Duas provas, quadro a quadro:
     *
     *  1. BRILHO: o pixel mais claro do texto. Foi essa medida que mostrou 140 no
     *     iOS contra 242 no Android e 244 no Vulkan do host, com a MESMA cena.
     *  2. PISCADA: mover a camada e olhar CADA quadro. Quadro idêntico ao
     *     anterior com o objeto andando = quadro repetido/velho; contagem de
     *     pixels acesos caindo a quase zero = o objeto sumiu.
     */
    private suspend fun texto3d() {
        step("3/5 texto 3D: brilho e piscada")
        line()
        line("--- 3) TEXTO 3D: BRILHO E PISCADA")
        if (!store.newProjectHeadless(1920, 1080, 30f, "Estresse 3D")) { line("nao consegui criar o projeto 3D; parando"); falhas += "newProject 3D falhou"; return }
        delay(200)
        val id = store.addText3D()
        store.seek(0)
        delay(400)

        val quadro = captura(320) ?: run {
            line("não consegui capturar o quadro do texto 3D")
            falhas += "captura de quadro falhou no texto 3D"
            return
        }
        val brilho = maiorCanal(quadro.bytes)
        val acesos = contaAcesos(quadro.bytes)
        line("face do texto 3D: pixel mais claro = $brilho, pixels acesos = $acesos")
        line("referencia medida nesta mesma cena: 244 no Vulkan do host, 242 no Android")
        verdict(
            "brilho do texto 3D", brilho >= 200 && acesos >= 200,
            "maior canal = $brilho; abaixo de 200 a face está sendo sombreada a menos que o devido"
        )

        if (id == 0L) return
        // Move de verdade: 60 quadros empurrando a posição X, capturando cada um.
        var identicos = 0
        var sumidos = 0
        var anterior: ByteArray? = null
        val acesosPorQuadro = mutableListOf<Int>()
        for (f in 0 until 60) {
            val x = (f - 30) * 6f
            store.setTransform(TrackProperty.POSITION_X, x, id)
            store.seek(f)
            delay(34)
            val q = captura(160) ?: continue
            val n = contaAcesos(q.bytes)
            acesosPorQuadro += n
            val antes = anterior
            if (antes != null && antes.size == q.bytes.size && antes.contentEquals(q.bytes)) identicos++
            anterior = q.bytes
        }
        val mediana = acesosPorQuadro.sorted().getOrNull(acesosPorQuadro.size / 2) ?: 0
        sumidos = acesosPorQuadro.count { it < max(1, mediana / 5) }
        line("quadros capturados ao mover: ${acesosPorQuadro.size}")
        line("quadros idênticos ao anterior: $identicos")
        line("quadros em que o objeto quase sumiu: $sumidos (mediana de pixels acesos = $mediana)")
        verdict("piscada do 3D", identicos == 0 && sumidos == 0, "repetidos = $identicos, sumidos = $sumidos")
    }

    // --- 4) partículas -----------------------------------------------------------

    private suspend fun particulas() {
        step("4/5 partículas")
        line()
        line("--- 4) PARTÍCULAS (6 s)")
        if (!store.newProjectHeadless(1920, 1080, 30f, "Estresse particulas")) { line("nao consegui criar o projeto de particulas; parando"); falhas += "newProject de particulas falhou"; return }
        delay(200)
        store.addParticles(0)
        delay(300)
        val amostras = toca(segundos = 6)
        relataAmostras(amostras)
        val p99 = percentil(amostras.map { it.cpuFrameMs + it.gpuFrameMs }, 99)
        verdictMedido("partículas", p99 <= 2f * 33.3f, "cpu+gpu p99 = %.1f ms".format(p99))
    }

    // --- 5) memória --------------------------------------------------------------

    private suspend fun memoria() {
        step("5/5 memória")
        line()
        line("--- 5) MEMÓRIA")
        line("RAM do processo: ${perf().ramBytes / MB} MB")
        line("cache de quadros decodificados: ${perf().decodedCacheBytes / MB} MB (${perf().decodedCacheFrames} quadros)")
        line("VRAM reservada: ${perf().gpuReservedBytes / MB} MB em ${perf().gpuAllocations} blocos")
        line("geometria e texturas 3D residentes: ${perf().scene3dBytes / MB} MB")
        // Em regime, criar textura e compilar pipeline por quadro é vazamento ou
        // recompilação — os dois aparecem como engasgo.
        verdictMedido(
            "recursos criados em regime", perf().texturesCreated == 0,
            "texturas criadas no último quadro = ${perf().texturesCreated}"
        )
        verdictMedido(
            "pipeline compilado durante o uso", perf().pipelineCompilesLive == 0,
            "pipelines compilados ao vivo = ${perf().pipelineCompilesLive}"
        )
        line("escala de renderização: ${perf().renderScaleNum}/${perf().renderScaleDen} (auto=${sim(perf().renderAuto)})")
        line("fator de calor (heavyScale): ${perf().heavyScale}")
    }

    // --- 6) composição pesada, com o vídeo do tester -----------------------------

    /**
     * O caso real: o vídeo do tester, com MUITA coisa em cima.
     *
     * Quatro cópias de vídeo (quatro decodificações ao mesmo tempo), dois textos,
     * dois textos 3D, duas camadas de partículas, duas formas, um ajuste e efeitos
     * espalhados. É assim que o editor de verdade fica depois de uma sessão de
     * trabalho — e é onde o app trava. Mede o custo de quadro pela captura, porque
     * fora do editor o `PerfPOD` sai zero.
     */
    private suspend fun pesado() {
        step("6/7 composição pesada")
        line()
        line("--- 6) COMPOSIÇÃO PESADA (o caso real)")
        if (videoDoUsuario == null) {
            line("nenhum vídeo escolhido pelo tester; a fase pesada usa só camadas geradas")
        } else {
            line("vídeo do tester: ${videoDoUsuario}")
        }
        if (!store.newProjectHeadless(1920, 1080, 30f, "Estresse pesado")) {
            line("nao consegui criar o projeto pesado; parando")
            falhas += "newProject pesado falhou"
            return
        }
        delay(250)

        val base = mutableListOf<Long>()
        if (videoDoUsuario != null) {
            // O import do store é assíncrono e no fim seleciona a camada nova;
            // é por isso que a espera é pela seleção, e não por um retorno.
            store.clearSelection()
            store.importVideo(videoDoUsuario)
            var espera = 0
            while (store.primary == null && espera < 40) { delay(200); espera++ }
            val v = store.primary
            if (v != null) {
                base.add(v)
                // Quatro cópias = quatro decodificações concorrentes da mesma fonte.
                store.duplicateLayers(listOf(v)); delay(200)
                store.duplicateLayers(store.layers.map { it.id }); delay(250)
                line("camadas de vídeo: ${store.layers.size} no total")
            } else {
                line("!! o vídeo do tester não importou em 8 s")
                falhas += "importVideo do vídeo do tester falhou"
            }
        }
        repeat(2) { val id = store.addText(); if (id > 0) base.add(id); delay(80) }
        repeat(2) { val id = store.addText3D(); if (id > 0) base.add(id); delay(150) }
        var id3d = base.lastOrNull() ?: 0L
        repeat(2) { val id = store.addParticles(0); if (id > 0) base.add(id); delay(120) }
        repeat(2) { store.addShape(1); delay(80) }
        store.addAdjustmentLayer(); delay(80)
        store.addVectorLayer(1); delay(120)

        // Efeitos em cima de quem já existe — vários por camada.
        val efeitos = doisEfeitosPesados()
        var aplicados = 0
        for (id in base.take(8)) {
            for (e in efeitos) {
                store.addEffect(e, listOf(id))
                aplicados++
                delay(30)
            }
        }
        delay(500)
        line("camadas criadas: ${base.size}, efeitos aplicados: $aplicados")
        line("(a lista do store fica vazia fora do editor: a contagem acima e' dos ids devolvidos)")
        line("geometria e texturas 3D residentes: ${perf().scene3dBytes / MB} MB")
        line("VRAM reservada: ${perf().gpuReservedBytes / MB} MB em ${perf().gpuAllocations} blocos")
        line("RAM do processo: ${perf().ramBytes / MB} MB")

        step("6/7 pesada: playback")
        capturasMs.clear()
        val amostras = toca(segundos = 20)
        relataAmostras(amostras)

        step("6/7 pesada: scrub com tudo ligado")
        val rnd = java.util.Random(11)
        var i = 0
        var azuis = 0
        var pior = 0.0
        var repetidos = 0
        var anterior: ByteArray? = null
        val t0 = System.currentTimeMillis()
        while (System.currentTimeMillis() - t0 < 15_000) {
            store.seek(rnd.nextInt(120))
            if (i % 4 == 0) delay(16)
            val q = captura(160) ?: continue
            val c = cores(q.bytes)
            if (indiceAzul(c) > 0.12) azuis++
            pior = maxOf(pior, indiceAzul(c))
            val antes = anterior
            if (antes != null && antes.size == q.bytes.size && antes.contentEquals(q.bytes)) repetidos++
            anterior = q.bytes
            i++
        }
        val segs = (System.currentTimeMillis() - t0) / 1000.0
        line("quadros olhados: $i em %.1f s (%.1f por segundo)".format(segs, i / segs))
        line("repetidos = $repetidos, azuis = $azuis, pior indice de azul = %.3f".format(pior))
        relataCapturas()
        verdict("pesada não pode ficar azul", azuis == 0, "quadros azuis = $azuis")

        step("6/7 pesada: mexer no 3D com tudo ligado")
        var sumidos = 0
        var f = 0
        val t3 = System.currentTimeMillis()
        while (System.currentTimeMillis() - t3 < 20_000) {
            if (id3d > 0) {
                store.setTransform(TrackProperty.POSITION_X, ((f % 100) - 50) * 5f, id3d)
                store.setTransform(TrackProperty.ROTATION_Y, (f % 360).toFloat(), id3d)
                store.setTransform(TrackProperty.SCALE_X, 1f + (f % 20) / 40f, id3d)
                store.setTransform(TrackProperty.SCALE_Y, 1f + (f % 20) / 40f, id3d)
            }
            delay(33)
            val q = captura(160) ?: continue
            if (cores(q.bytes).acesos in 1 until 40) sumidos++
            f++
        }
        line("3D com tudo ligado: $f quadros, sem o objeto = $sumidos")
        verdict("3D some na composição pesada", sumidos == 0, "quadros sem o objeto = $sumidos")

        if (id3d == 0L) line("(sem texto 3D nesta fase: nada a mover)")
    }

    /** Percentis do custo de um quadro medido pela captura. */
    private fun relataCapturas() {
        if (capturasMs.isEmpty()) {
            line("nenhuma captura cronometrada")
            return
        }
        line(
            "custo de quadro (captura: render + leitura) p50/p95/p99: %.1f / %.1f / %.1f ms em %d quadros".format(
                percentil(capturasMs, 50), percentil(capturasMs, 95), percentil(capturasMs, 99), capturasMs.size
            )
        )
        val p95 = percentil(capturasMs, 95)
        verdict(
            "custo de quadro na composição pesada", p95 in 0.01f..(2f * 33.3f),
            "p95 = %.1f ms (orcamento 33,3 ms a 30 fps)".format(p95)
        )
        if (medidas.isEmpty()) return
        fun col(f: (Medida) -> Float) = medidas.map(f)
        line("    onde o tempo foi (p95 de cada etapa, ms):")
        line("      CPU prepare ......... %.1f   (preso sob o lock do modelo)".format(percentil(col { it.prepareMs }, 95)))
        line("      espera do decoder ... %.1f   (parado esperando quadro exato de vídeo)".format(percentil(col { it.mediaWaitMs }, 95)))
        line("      gravar o grafo ...... %.1f   (montagem do FrameGraph)".format(percentil(col { it.recordMs }, 95)))
        line("      submeter ............ %.1f".format(percentil(col { it.submitMs }, 95)))
        line("      esperar a GPU ....... %.1f   (wait_idle: pipeline entupido)".format(percentil(col { it.gpuWaitMs }, 95)))
        line("      GPU medida .......... %.1f   (%d de %d quadros com timestamp)".format(
            percentil(col { it.gpuMs }, 95), medidas.count { it.gpuMedido }, medidas.size))
        line("    trabalho do quadro: %d passes, %d camadas, %d draws 3D, %d triangulos, %d particulas, %d efeitos".format(
            medidas.last().passesExecutados, medidas.last().camadas, medidas.last().draws3d,
            medidas.last().triangulos, medidas.last().particulas, medidas.last().efeitos))
        line("    transitorias do FrameGraph: %.1f MB   alocador do backend: %.1f MB".format(
            medidas.last().transitorio / 1048576.0, medidas.last().gpuUsado / 1048576.0))
        line("    prepares por quadro (1 = o decoder ja tinha o quadro): %d".format(medidas.last().tentativas))
    }

    /// Uma amostra do `OffscreenMeasure` do motor — o quadro que a captura renderiza.
    private class Medida(
        val prepareMs: Float, val mediaWaitMs: Float, val tentativas: Int, val recordMs: Float,
        val submitMs: Float, val gpuWaitMs: Float, val gpuMs: Float, val gpuMedido: Boolean,
        val passesExecutados: Int, val passesCortados: Int, val draws: Int, val camadas: Int,
        val draws3d: Int, val triangulos: Int, val cortados3d: Int, val particulas: Int,
        val efeitos: Int, val transitorio: Double, val gpuUsado: Double,
    ) {
        companion object {
            fun de(v: DoubleArray) = Medida(
                v[0].toFloat(), v[1].toFloat(), v[2].toInt(), v[3].toFloat(), v[4].toFloat(),
                v[5].toFloat(), v[6].toFloat(), v[7] > 0.5, v[9].toInt(), v[10].toInt(),
                v[11].toInt(), v[12].toInt(), v[13].toInt(), v[14].toInt(), v[15].toInt(),
                v[16].toInt(), v[17].toInt(), v[18], v[19],
            )
        }
    }

    // --- 7) modo bruto: forçar o travamento --------------------------------------

    /**
     * Aqui não se mede nada de fino: tenta-se QUEBRAR. Os testadores relatam
     * travar, engasgar e fechar; isso acontece em uso encadeado e rápido, não em
     * playback limpo de seis segundos. Cada sub-passo cronometra a si mesmo, e o
     * relatório é escrito no arquivo linha a linha — então um passo que não
     * termina deixa registrado até onde chegou.
     */
    private suspend fun bruto() {
        if (!store.newProjectHeadless(1920, 1080, 30f, "Estresse bruto")) {
            line("nao consegui criar o projeto bruto; parando")
            falhas += "newProject do modo bruto falhou"
            return
        }
        delay(250)
        store.addText()
        delay(120)
        for (id in doisEfeitosPesados()) { store.addEffect(id); delay(50) }
        val id3d = store.addText3D()
        delay(250)

        step("6/6 modo bruto: play/pause")
        line()
        line("--- 6) MODO BRUTO")
        var ciclos = 0
        var t0 = System.currentTimeMillis()
        while (System.currentTimeMillis() - t0 < 15_000) {
            store.play(); delay(300); store.pause(); delay(120)
            ciclos++
        }
        line("6.1 play/pause encadeado: $ciclos ciclos em %.1f s".format((System.currentTimeMillis() - t0) / 1000.0))

        step("6/6 modo bruto: scrub agressivo")
        val rnd = java.util.Random(7)
        t0 = System.currentTimeMillis()
        var seeks = 0
        while (System.currentTimeMillis() - t0 < 15_000) {
            store.seek(rnd.nextInt(300))
            seeks++
            if (seeks % 25 == 0) delay(16)
        }
        line("6.2 scrub agressivo: $seeks seeks em %.1f s".format((System.currentTimeMillis() - t0) / 1000.0))

        step("6/6 modo bruto: seek em rajada")
        t0 = System.currentTimeMillis()
        repeat(40) {
            store.seek(10); store.seek(40); store.seek(70); store.seek(100)
        }
        store.seek(100)
        delay(300)
        line("6.3 rajada 10/40/70/100 x40: %.1f s (o 100 tem de ganhar)".format((System.currentTimeMillis() - t0) / 1000.0))
        line("    playhead pedido = 100, playhead real = ${store.playhead}")
        verdict("seek em rajada", store.playhead == 100, "playhead ficou em ${store.playhead}, esperado 100")

        step("6/6 modo bruto: 3D por 30 s")
        var repetidos = 0
        var sumidos = 0
        var azuis = 0
        var piorAzul = 0.0
        var anterior: ByteArray? = null
        val t3d = System.currentTimeMillis()
        var f = 0
        while (System.currentTimeMillis() - t3d < 30_000) {
            if (id3d > 0) {
                store.setTransform(TrackProperty.POSITION_X, ((f % 120) - 60) * 4f, id3d)
                store.setTransform(TrackProperty.ROTATION_Y, (f % 360).toFloat(), id3d)
            }
            store.seek(f % 300)
            delay(33)
            val q = captura(160) ?: continue
            val c = cores(q.bytes)
            if (c.acesos in 1 until 40) sumidos++
            val antes = anterior
            if (antes != null && antes.size == q.bytes.size && antes.contentEquals(q.bytes)) repetidos++
            anterior = q.bytes
            val azul = indiceAzul(c)
            if (azul > 0.12) azuis++
            if (azul > piorAzul) piorAzul = azul
            f++
        }
        val segundos3d = (System.currentTimeMillis() - t3d) / 1000.0
        line("6.4 3D continuo: $f quadros em %.1f s (%.1f fps de captura)".format(segundos3d, f / segundos3d))
        line("    quadros repetidos = $repetidos, quadros sem o objeto = $sumidos")
        line("    quadros azuis = $azuis (pior indice de azul = %.3f)".format(piorAzul))
        verdict("3D nao pode piscar", repetidos == 0 && sumidos == 0, "repetidos = $repetidos, sumidos = $sumidos")
        verdict("video/3D nao pode ficar azul", azuis == 0, "quadros azuis = $azuis, pior = %.3f".format(piorAzul))

        step("6/6 modo bruto: abrir e fechar projetos")
        t0 = System.currentTimeMillis()
        var voltas = 0
        while (System.currentTimeMillis() - t0 < 10_000) {
            store.newProjectHeadless(1080, 1920, 60f, "Estresse ciclo")
            delay(60)
            store.newProjectHeadless(1920, 1080, 24f, "Estresse ciclo")
            delay(60)
            voltas++
        }
        line("6.5 trocar de projeto (inclusive 1080x1920 60fps): $voltas voltas em %.1f s"
            .format((System.currentTimeMillis() - t0) / 1000.0))
    }

    /**
     * Índice de azul: quanto o canal azul domina a média de vermelho e verde,
     * normalizado. Um quadro normal fica perto de 0; o defeito relatado
     * ("o vídeo fica azul do nada") joga isso para cima de 0,3 de uma vez.
     */
    private fun indiceAzul(c: Cores): Double {
        if (c.acesos < 200) return 0.0
        val rg = (c.r + c.g) / 2.0
        if (rg <= 1.0) return 0.0
        return ((c.b - rg) / (rg + c.b)).coerceAtLeast(0.0)
    }

    private class Cores(val r: Double, val g: Double, val b: Double, val acesos: Int)

    private fun cores(b: ByteArray): Cores {
        var r = 0L; var g = 0L; var bl = 0L; var n = 0
        var i = 0
        while (i + 2 < b.size) {
            val cr = b[i].toInt() and 0xFF
            val cg = b[i + 1].toInt() and 0xFF
            val cb = b[i + 2].toInt() and 0xFF
            if (cr > 8 || cg > 8 || cb > 8) { r += cr; g += cg; bl += cb; n++ }
            i += 4
        }
        if (n == 0) return Cores(0.0, 0.0, 0.0, 0)
        return Cores(r.toDouble() / n, g.toDouble() / n, bl.toDouble() / n, n)
    }

    private fun rodape() {
        line()
        line("--- ERROS REPORTADOS PELO APP")
        val e = store.errorMessage
        if (e.isNullOrBlank()) line("nenhum durante a bateria") else line("  $e")
        line()
        line("--- VEREDITO")
        if (falhas.isEmpty()) {
            line("nenhum P0 nesta bateria")
        } else {
            falhas.forEach { line("  FALHA — $it") }
        }
        line()
        line("FIM. Copie daqui para cima e mande inteiro.")
    }

    // --- utilidades --------------------------------------------------------------

    private suspend fun toca(segundos: Int): List<PerfStats> {
        val amostras = mutableListOf<PerfStats>()
        store.seek(0)
        delay(120)
        store.play()
        val fim = System.currentTimeMillis() + segundos * 1000L
        while (System.currentTimeMillis() < fim) {
            delay(100)
            amostras += perf()
        }
        store.pause()
        delay(150)
        return amostras
    }

    private fun relataAmostras(a: List<PerfStats>) {
        // O teste roda a partir de Ajustes, onde não há superfície de prévia. Se
        // o motor não estiver apresentando quadro nenhum, estes números não valem
        // e é melhor dizer isso do que entregar um relatório bonito e vazio.
        if (a.isNotEmpty() && a.all { it.previewWidth == 0 } && a.all { it.previewFps == 0f }) {
            line("!! sem superfície de prévia: o motor não apresentou quadro, os tempos abaixo NAO valem")
        }
        if (a.isEmpty()) {
            line("nenhuma amostra")
            return
        }
        val fps = a.map { it.previewFps }.filter { it > 0f }
        line("fps medio: ${if (fps.isEmpty()) "?" else "%.1f".format(fps.average())}")
        line(
            "pacing p50/p95/p99: %.1f / %.1f / %.1f ms (${a.last().pacingSamples} amostras)".format(
                percentil(a.map { it.pacingP50Ms }.filter { it > 0f }, 50),
                percentil(a.map { it.pacingP95Ms }.filter { it > 0f }, 95),
                percentil(a.map { it.pacingP99Ms }.filter { it > 0f }, 99)
            )
        )
        line(
            "cpu p50/p99: %.1f / %.1f ms   gpu p50/p99: %.1f / %.1f ms".format(
                percentil(a.map { it.cpuFrameMs }, 50), percentil(a.map { it.cpuFrameMs }, 99),
                percentil(a.map { it.gpuFrameMs }, 50), percentil(a.map { it.gpuFrameMs }, 99)
            )
        )
        line(
            "present p99: %.1f ms   acquire p99: %.1f ms   decode p95: %.1f ms".format(
                percentil(a.map { it.presentMs }, 99), percentil(a.map { it.acquireMs }, 99),
                percentil(a.map { it.decodeMs }, 95)
            )
        )
        val perdidos = a.last().droppedFrames - a.first().droppedFrames
        line("frames perdidos na janela: $perdidos (recente: ${a.last().droppedRecent})")
        line("undo/redo, seeks: ${a.last().seeks}   coalescidos: ${a.last().coalesced}   velhos: ${a.last().staleFrames}")
        line("audio: underruns ${a.last().audioUnderruns}  faltando ${a.last().audioMissingBlocks}  fila ${a.last().audioQueuedMs} ms")
    }

    private fun percentil(v: List<Float>, p: Int): Float {
        if (v.isEmpty()) return 0f
        val s = v.sorted()
        val i = ((p / 100f) * (s.size - 1)).toInt().coerceIn(0, s.size - 1)
        return s[i]
    }

    private fun doisEfeitosPesados(): List<Int> {
        val cat = store.catalog
        val queridos = listOf("glow", "blur", "borr", "brilho")
        val achados = queridos.mapNotNull { q ->
            cat.firstOrNull { it.name.lowercase(Locale.ROOT).contains(q) }?.typeId
        }
        return achados.distinct().ifEmpty { cat.take(1).map { it.typeId } }
    }

    /**
     * RGBA8 do quadro do playhead, em `maxDim`; nulo se o motor não devolveu.
     *
     * O tempo desta chamada é a ÚNICA medida por quadro que funciona fora do
     * editor: `render_offscreen` renderiza de verdade e o motor espera os frames
     * exatos de vídeo, então o número inclui render e leitura. Não é o tempo de
     * apresentação, mas é o custo real de um quadro desta composição.
     */
    private fun captura(maxDim: Int): Captura? {
        val tam = maxDim * maxDim * 4
        val buf = ByteBuffer.allocateDirect(tam).order(ByteOrder.nativeOrder())
        val wh = IntArray(2)
        val t0 = System.currentTimeMillis()
        val n = engine.captureFrame(maxDim, buf, wh)
        capturasMs += (System.currentTimeMillis() - t0).toFloat()
        if (n > 0 && engine.readOffscreenMeasure(medidaBuf)) medidas += Medida.de(medidaBuf)
        if (n <= 0) return null
        val bytes = ByteArray(n)
        buf.rewind()
        buf.get(bytes)
        return Captura(bytes, wh[0], wh[1])
    }

    private class Captura(val bytes: ByteArray, val w: Int, val h: Int)

    private fun contaAcesos(b: ByteArray): Int {
        var n = 0
        var i = 0
        while (i + 2 < b.size) {
            val v = max(b[i].toInt() and 0xFF, max(b[i + 1].toInt() and 0xFF, b[i + 2].toInt() and 0xFF))
            if (v > 8) n++
            i += 4
        }
        return n
    }

    private fun maiorCanal(b: ByteArray): Int {
        var m = 0
        var i = 0
        while (i + 2 < b.size) {
            m = max(m, max(b[i].toInt() and 0xFF, max(b[i + 1].toInt() and 0xFF, b[i + 2].toInt() and 0xFF)))
            i += 4
        }
        return m
    }

    /**
     * O `PerfPOD` lido direto do motor.
     *
     * Não dá para usar `store.perf`: aquele campo só é preenchido quando o HUD de
     * desenvolvimento está ligado (`hudVisible`), e era por isso que os quatro
     * relatórios dos testadores vieram com `gpu: ?`, `decoder: ?` e orçamento 0.
     * Aqui a leitura é sempre feita, com ou sem HUD.
     */
    private fun perf(): PerfStats {
        if (!engine.readPerf(perfBuffer)) return PerfStats()
        perfBuffer.rewind()
        return PerfStats.read(perfBuffer)
    }

    private val perfBuffer: ByteBuffer =
        ByteBuffer.allocateDirect(PerfStats.BYTES).order(ByteOrder.nativeOrder())

    private fun sim(v: Boolean) = if (v) "sim" else "NAO"

    private val progresso: ByteBuffer = ByteBuffer.allocateDirect(256).order(ByteOrder.nativeOrder())

    private companion object {
        const val MB = 1024L * 1024L
    }
}
