package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.os.SystemClock
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.effects.effectToolOf
import com.aurea.aurea.effects.pickableEffects
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.hyper.EntitlementCheck
import com.aurea.aurea.hyper.EntitlementProvider
import com.aurea.aurea.hyper.HyperAccess
import com.aurea.aurea.hyper.HyperEntitlements
import com.aurea.aurea.hyper.ServerEntitlementProvider
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.After
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import java.io.File
import kotlin.math.abs

/**
 * Varredura de TODOS os efeitos do navegador (pedido do dono, 17/11/2026): para
 * cada efeito escolhível numa camada de forma, adiciona, renderiza o quadro do
 * motor, compara com o quadro sem efeito e remove. O relatório vai para
 * `files/effects-smoke/report.tsv` linha a linha (se o processo cair, a última
 * linha diz em qual efeito), e cada quadro fica em PNG ao lado.
 *
 * "sem mudança" não é falha por si: transição com conclusão 0 e efeito de tempo
 * numa forma parada não mudam o quadro. Falha é: não entrar na pilha, quadro
 * nulo, quadro vazio ou o processo cair.
 */
class AllEffectsSmokeDeviceTest {
    @get:Rule val compose = createComposeRule()
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private lateinit var store: EditorStore

    private class AllAccess : EntitlementProvider {
        override suspend fun fetch(sessionToken: String): EntitlementCheck =
            EntitlementCheck.Granted(setOf("aurea_hyper_mensal", HyperEntitlements.PRODUCT), 1)
    }

    @After fun tearDown() {
        com.aurea.aurea.config.AppConfigStore.overrideForTest(null)
        HyperEntitlements.useProvider(ServerEntitlementProvider())
        compose.runOnUiThread { HyperEntitlements.clear() }
    }

    private fun capture(): Bitmap? {
        compose.waitForIdle()
        SystemClock.sleep(250)
        compose.waitForIdle()
        return store.captureBitmap(360)
    }

    /** Diferença média por canal (0..255) e fração de pixels não transparentes/não pretos. */
    private fun stats(a: Bitmap, b: Bitmap?): Pair<Double, Double> {
        val w = a.width; val h = a.height
        val pa = IntArray(w * h); a.getPixels(pa, 0, w, 0, 0, w, h)
        val pb = if (b != null && b.width == w && b.height == h) IntArray(w * h).also { b.getPixels(it, 0, w, 0, 0, w, h) } else null
        var diff = 0L; var lit = 0
        for (i in pa.indices) {
            val c = pa[i]
            val r = (c shr 16) and 255; val g = (c shr 8) and 255; val bl = c and 255
            if (r + g + bl > 24) lit++
            if (pb != null) {
                val d = pb[i]
                diff += abs(r - ((d shr 16) and 255)) + abs(g - ((d shr 8) and 255)) + abs(bl - (d and 255))
            }
        }
        return (if (pb == null) -1.0 else diff / (3.0 * pa.size)) to lit.toDouble() / pa.size
    }

    /**
     * Efeito que não mudou nada com o padrão (intensidade 0, conclusão 0%...):
     * leva cada parâmetro numérico a 60% da faixa, um de cada vez, até o quadro
     * mudar. Devolve "OK_PARAM:<rótulo>" ou nulo se nenhum mexeu.
     */
    private fun pushParams(typeId: Int, base: Bitmap): String? {
        val fx = store.effects.firstOrNull { it.typeId == typeId } ?: return null
        val specs = store.effectSpecs(typeId).filter {
            it.type == com.aurea.aurea.engine.ParamType.FLOAT || it.type == com.aurea.aurea.engine.ParamType.INT ||
                it.type == com.aurea.aurea.engine.ParamType.ANGLE
        }.take(10)
        for (p in specs) {
            if (!(p.max > p.min)) continue
            val d = p.defaultValue.firstOrNull() ?: p.min
            var v = p.min + 0.6f * (p.max - p.min)
            if (abs(v - d) < 1e-3f * (p.max - p.min)) v = p.min + 0.9f * (p.max - p.min)
            compose.runOnIdle { store.setEffectParam(fx.effectId, p, v) }
            compose.runOnIdle { store.seek(15) }
            val bmp = capture() ?: continue
            val diff = stats(bmp, base).first
            bmp.recycle()
            if (diff >= 0.05) return "OK_PARAM:${p.label}=${"%.2f".format(v)}"
        }
        return null
    }

    @Test fun psykoEffectsRenderAndTwitchIsFree() {
        check(context.packageName.endsWith(".uitest"))
        compose.runOnUiThread { HyperEntitlements.clear() }
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30_000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(360, 360, 30f, "Psyko e Twitch") }
        compose.waitUntil(15_000) { store.project.title == "Psyko e Twitch" && !store.projectOperationBusy }
        compose.runOnIdle { store.addShape(1) }
        compose.waitUntil(10_000) { store.detail != null && store.catalog.isNotEmpty() }

        val twitch = effectTypeId("aurea.motion.twitch_reference")
        assertTrue(!AureaEngine.nativeEffectRequiresHyper(twitch))
        assertTrue(!AureaEngine.nativeEffectRequiresHyper(effectTypeId("aurea.motion.twitch")))
        compose.runOnIdle { store.addEffect(twitch) }
        compose.waitUntil(10_000) { store.effects.any { it.typeId == twitch } }
        compose.runOnIdle { store.removeEffect(store.effects.first { it.typeId == twitch }.effectId) }
        compose.waitUntil(10_000) { store.effects.isEmpty() }

        val original = checkNotNull(capture())
        try {
            for (key in listOf("aurea.stylize.psyko_blobs", "aurea.stylize.psyko_stripes")) {
                val id = effectTypeId(key)
                assertTrue("$key belongs to the catalog", store.catalog.any { it.typeId == id })
                compose.runOnIdle { store.seek(0); store.addEffect(id) }
                compose.waitUntil(10_000) { store.effects.any { it.typeId == id } }
                val still = checkNotNull(capture())
                try {
                    assertTrue("$key recolors the layer", stats(still, original).first > 0.5)
                    compose.runOnIdle { store.seek(24) }
                    val advanced = checkNotNull(capture())
                    try {
                        assertTrue("$key animates its phase", stats(advanced, still).first > 0.5)
                    } finally {
                        advanced.recycle()
                    }
                } finally {
                    still.recycle()
                }
                compose.runOnIdle { store.removeEffect(store.effects.first { it.typeId == id }.effectId) }
                compose.waitUntil(10_000) { store.effects.isEmpty() }
            }
        } finally {
            original.recycle()
        }
    }

    @Test fun everyBrowserEffectRenders() {
        check(context.packageName.endsWith(".uitest"))
        val now = System.currentTimeMillis() / 1000
        com.aurea.aurea.config.AppConfigStore.overrideForTest(com.aurea.aurea.config.AppConfig("android", 0, 0, "",
            com.aurea.aurea.config.HyperMode.Entitled, emptyMap(), now - 60, now + 86400, "", ""))
        HyperEntitlements.useProvider(AllAccess())
        compose.runOnUiThread { HyperEntitlements.refresh("t".repeat(43), "teste@exemplo.com") }

        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.waitUntil(5000) { HyperEntitlements.access == HyperAccess.Unlocked }
        compose.runOnIdle { store.newProject(540, 540, 30f, "Efeitos smoke") }
        compose.waitUntil(15000) { store.project.title == "Efeitos smoke" && !store.projectOperationBusy }
        // Fundo cinza claro: sombra preta em fundo preto não aparece.
        compose.runOnIdle { store.setCompositionBackground(0.62f, 0.64f, 0.68f) }
        compose.runOnIdle { store.seek(15) }
        val bgOnly = checkNotNull(capture()) { "quadro do fundo nulo" }
        // Imagem colorida (gradiente de matiz, faixas e círculos): forma branca
        // não mostra efeito de cor, e efeito de borda precisa de detalhe.
        val src = File(context.filesDir, "effects-smoke-src.png")
        val img = Bitmap.createBitmap(540, 540, Bitmap.Config.ARGB_8888)
        for (y in 0 until 540) for (x in 0 until 540) {
            val hsv = floatArrayOf(360f * x / 540f, 0.85f, 0.25f + 0.75f * (1f - y / 540f))
            var c = android.graphics.Color.HSVToColor(hsv)
            if ((x / 45 + y / 45) % 2 == 0 && y > 360) c = android.graphics.Color.rgb(250, 250, 250)
            val dx = x - 270; val dy = y - 220
            if (dx * dx + dy * dy < 90 * 90) c = android.graphics.Color.rgb(230, 60, 40)
            img.setPixel(x, y, c)
        }
        src.outputStream().use { img.compress(Bitmap.CompressFormat.PNG, 100, it) }
        compose.runOnIdle { store.importImage(android.net.Uri.fromFile(src)) }
        compose.waitUntil(20000) { store.layers.size == 1 && store.detail != null }
        compose.waitUntil(10000) { store.catalog.isNotEmpty() }
        compose.runOnIdle { store.seek(15) }

        val dir = File(context.filesDir, "effects-smoke").apply { deleteRecursively(); mkdirs() }
        val report = File(dir, "report.tsv")
        report.writeText("status\tnome\tcategoria\tchave\tdiff\taceso\tms\n")
        val base = checkNotNull(capture()) { "quadro base nulo" }
        base.compress(Bitmap.CompressFormat.PNG, 100, File(dir, "000-base.png").outputStream())
        val (_, baseLit) = stats(base, null)
        val baseVsBg = stats(base, bgOnly).first
        report.appendText("BASE\t-\t-\t-\t0\t${"%.3f".format(baseLit)}\t0\n")

        val kind = store.layers.first().kind
        val entries = pickableEffects(store.catalog, false, kind).filter { effectToolOf(it.typeId) == null }.distinctBy { it.typeId }
            // IA pesada que roda fora da pilha (Rotobrush, Remover fundo): não cabe nesta varredura.
            .filter { it.typeId != effectTypeId("aurea.key.rotobrush") && Integer.toUnsignedString(it.typeId) != "1993981751" }
        var failures = 0
        entries.forEachIndexed { i, e ->
            val key = Integer.toUnsignedString(e.typeId)
            report.appendText("INICIO\t${e.name}\t${e.category}\t$key\t\t\t\n")
            val t0 = SystemClock.elapsedRealtime()
            compose.runOnIdle { store.addEffect(e.typeId) }
            val added = runCatching { compose.waitUntil(8000) { store.effects.any { it.typeId == e.typeId } } }.isSuccess
            if (!added) {
                failures++
                report.appendText("NAO_ENTROU\t${e.name}\t${e.category}\t$key\t\t\t${SystemClock.elapsedRealtime() - t0}\n")
                return@forEachIndexed
            }
            compose.runOnIdle { store.seek(15) }
            val bmp = capture()
            val ms = SystemClock.elapsedRealtime() - t0
            val status: String
            var diff = -1.0; var lit = 0.0
            if (bmp == null) { status = "QUADRO_NULO"; failures++ }
            else {
                val s = stats(bmp, base); diff = s.first; lit = s.second
                val vsBg = stats(bmp, bgOnly).first
                var st = when {
                    vsBg < 0.05 && baseVsBg > 0.5 -> "VAZIO"
                    diff < 0.05 -> "SEM_MUDANCA"
                    else -> "OK"
                }
                if (st == "SEM_MUDANCA") st = pushParams(e.typeId, base) ?: st
                status = st
                if (status == "VAZIO") failures++
                bmp.compress(Bitmap.CompressFormat.PNG, 100,
                    File(dir, "%03d-%s.png".format(i + 1, e.name.replace(Regex("[^A-Za-z0-9]+"), "_"))).outputStream())
                bmp.recycle()
            }
            val known = store.effects.firstOrNull { it.typeId == e.typeId }?.known
            report.appendText("$status\t${e.name}\t${e.category}\t$key\t${"%.2f".format(diff)}\t${"%.3f".format(lit)}\t$ms${if (known == false) "\tDESCONHECIDO" else ""}\n")
            compose.runOnIdle { store.effects.filter { it.typeId == e.typeId }.forEach { store.removeEffect(it.effectId) } }
            runCatching { compose.waitUntil(8000) { store.effects.none { it.typeId == e.typeId } } }
        }
        report.appendText("FIM\t${entries.size} efeitos\t$failures falhas\t\t\t\t\n")
        assertTrue("$failures efeitos falharam (veja report.tsv)", failures == 0)
    }

    /**
     * Efeitos pagos que só aparecem com MOVIMENTO (Twitch, Cartoon Moblur) e os de
     * sombra/3D, numa forma vermelha pequena que anda da esquerda para a direita
     * sobre fundo cinza. Monta uma folha: cada linha é um efeito, colunas = quadros
     * 4, 10, 16, 22, 28; a 1ª linha é sem efeito. Também mede quantos quadros mudam.
     */
    @Test fun paidEffectsWithMotion() {
        check(context.packageName.endsWith(".uitest"))
        val now = System.currentTimeMillis() / 1000
        com.aurea.aurea.config.AppConfigStore.overrideForTest(com.aurea.aurea.config.AppConfig("android", 0, 0, "",
            com.aurea.aurea.config.HyperMode.Entitled, emptyMap(), now - 60, now + 86400, "", ""))
        HyperEntitlements.useProvider(AllAccess())
        compose.runOnUiThread { HyperEntitlements.refresh("t".repeat(43), "teste@exemplo.com") }
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.waitUntil(5000) { HyperEntitlements.access == HyperAccess.Unlocked }
        compose.runOnIdle { store.newProject(540, 540, 30f, "Efeitos movimento") }
        compose.waitUntil(15000) { store.project.title == "Efeitos movimento" && !store.projectOperationBusy }
        compose.runOnIdle { store.setCompositionBackground(0.62f, 0.64f, 0.68f) }
        compose.runOnIdle { store.addShape(1) }
        compose.waitUntil(10000) { store.layers.size == 1 && store.detail != null }
        val engine = EditorStore::class.java.getDeclaredField("engine").apply { isAccessible = true }.get(store) as com.aurea.aurea.engine.AureaEngine
        val shape = store.layers.first().id
        compose.runOnIdle {
            engine.beginCommandBatch()
            val c = com.aurea.aurea.engine.CommandBatch(engine)
            c.setScale(shape, .55f, .55f, 1f)
            c.setShapeFill(shape, .95f, .12f, .15f, 1f)
            engine.submitCommands()
        }
        // Posição X animada pelo caminho do app (cronômetro): 90 px no quadro 0, 450 px no 29.
        val px = com.aurea.aurea.engine.TrackProperty.POSITION_X
        compose.runOnIdle { store.seek(0) }
        compose.runOnIdle { store.setTransform(px, 90f) }
        compose.runOnIdle { store.toggleTransformKeyframe(intArrayOf(px)) }
        compose.runOnIdle { store.seek(29) }
        compose.runOnIdle { store.toggleTransformKeyframe(intArrayOf(px)) }
        compose.runOnIdle { store.setTransform(px, 450f) }
        compose.waitForIdle(); SystemClock.sleep(500)
        val frames = intArrayOf(4, 10, 16, 22, 28)
        // Efeito temporal pede os quadros vizinhos: renderiza, espera e captura de novo.
        fun row(): List<Bitmap> = frames.map { f -> compose.runOnIdle { store.seek(f) }; capture()?.recycle(); SystemClock.sleep(400); checkNotNull(capture()) }
        val effects = listOf(
            "Sem efeito" to 0,
            "Twitch" to effectTypeId("aurea.motion.twitch_reference"),
            "Cartoon Moblur" to effectTypeId("aurea.motion.cartoon_moblur"),
            "Drop Shadow" to effectTypeId("aurea.stylize.drop_shadow_reference"),
            "Extrude" to effectTypeId("aurea.generate.extrude_reference"),
            "Signal" to effectTypeId("aurea.glitch.signal_reference"),
            "Glitchify" to effectTypeId("aurea.glitch.glitchify_reference"),
        )
        val dir = File(context.filesDir, "effects-smoke").apply { mkdirs() }
        val report = File(dir, "motion.tsv").apply { writeText("efeito;quadros_mudados;diffs" + System.lineSeparator()) }
        val rows = ArrayList<List<Bitmap>>()
        var baseRow: List<Bitmap>? = null
        for ((name, type) in effects) {
            if (type != 0) {
                compose.runOnIdle { store.addEffect(type) }
                compose.waitUntil(8000) { store.effects.any { it.typeId == type } }
                // Twitch vem com todos os modos desligados (igual ao original): liga Slide e Scale.
                if (name == "Twitch") {
                    val fx = store.effects.first { it.typeId == type }
                    store.effectSpecs(type).filter { it.label == "Slide" || it.label == "Scale" || it.label == "Blur" }
                        .forEach { p -> compose.runOnIdle { store.setEffectParam(fx.effectId, p, 1f) } }
                }
            }
            val r = row()
            if (baseRow == null) baseRow = r
            val diffs = r.indices.map { stats(r[it], baseRow!![it]).first }
            report.appendText("$name;${diffs.count { it >= 0.05 }}/${diffs.size};${diffs.joinToString(",") { "%.2f".format(it) }}" + System.lineSeparator())
            rows += r
            if (type != 0) {
                compose.runOnIdle { store.effects.filter { it.typeId == type }.forEach { store.removeEffect(it.effectId) } }
                compose.waitUntil(8000) { store.effects.none { it.typeId == type } }
            }
        }
        val cell = 180
        val sheet = Bitmap.createBitmap(cell * frames.size, cell * rows.size, Bitmap.Config.ARGB_8888)
        val canvas = android.graphics.Canvas(sheet)
        val paint = android.graphics.Paint().apply { color = android.graphics.Color.WHITE; textSize = 18f; isAntiAlias = true
            setShadowLayer(3f, 0f, 0f, android.graphics.Color.BLACK) }
        rows.forEachIndexed { y, r -> r.forEachIndexed { x, b ->
            canvas.drawBitmap(b, null, android.graphics.Rect(x * cell, y * cell, (x + 1) * cell, (y + 1) * cell), null)
        }; canvas.drawText(effects[y].first, 6f, y * cell + 22f, paint) }
        File(dir, "motion-sheet.png").outputStream().use { sheet.compress(Bitmap.CompressFormat.PNG, 100, it) }
    }
}
