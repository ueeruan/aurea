package com.aurea.aurea.effects

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.provider.Settings
import android.util.LruCache
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaDims
import com.aurea.aurea.ui.theme.AureaShape
import com.aurea.aurea.ui.theme.AureaType
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import java.io.File

// =============================================================================
//  PRÉVIAS ANIMADAS DOS EFEITOS (docs/diagnostico/efeitos-imagem-feed.md §3.2).
//
//  O motor renderiza N quadros (12..16) do efeito real sobre uma cena-fonte
//  nossa (Engine::render_effect_preview_sequence). A interface só pede, guarda
//  e toca:
//
//   1. A GRADE CONTINUA PARADA. Só a FICHA do efeito anima, com seletor de
//      cena, e o cartão SEGURADO toca a animação que já estiver guardada.
//      Gerar é só ao abrir a ficha: a grade nunca pede sequência ao motor.
//   2. DISCO COM TETO. Uma tira WebP por (efeito, cena), na pasta versionada
//      das prévias estáticas (`previas/<versão>/animadas/`), até 24 MB; passou,
//      saem as menos usadas (LRU pela data de acesso).
//   3. BUSY NÃO É FALHA. Com o preview do editor usando a GPU, o motor devolve
//      Busy: tenta de novo com recuo exponencial. Sair da ficha cancela.
//      Falhou de vez? A ficha mostra a prévia parada.
//   4. "Remover animações" do sistema ou "Prévias de efeitos" desligado:
//      nada anima (e nada é pedido).
// =============================================================================

/** Cenas-fonte do motor (EffectPreviewScene). A ordem é contrato com o C++. */
enum class EffectPreviewScene(val id: Int, val label: Int) {
    PHOTO(0, R.string.fx_preview_scene_photo),
    PLATE(1, R.string.fx_preview_scene_plate),
    SHAPES(2, R.string.fx_preview_scene_shapes),
    LOW_LIGHT(3, R.string.fx_preview_scene_low_light),
}

/** O que o motor devolveu: 0 + quadros, ou o código de erro (Busy = tentar de novo). */
class EffectSequenceResult(val code: Int, val frames: List<ImageBitmap> = emptyList())

class EffectAnimatedPreviewStore(
    private val dir: () -> File,
    private val render: (typeId: Int, width: Int, height: Int, frames: Int, scene: Int) -> EffectSequenceResult,
) {
    /** Em memória: poucas sequências (16 quadros de 320×200 são 4 MB). */
    private val memory = object : LruCache<String, List<ImageBitmap>>(MAX_MEMORY_BYTES) {
        override fun sizeOf(key: String, value: List<ImageBitmap>) = value.sumOf { it.width * it.height * 4 }
    }
    @Volatile private var epoch = 0L

    fun trimMemory() { epoch++; memory.evictAll() }

    /**
     * A sequência do efeito na cena: memória → disco → (só com [generate]) GPU,
     * com recuo exponencial enquanto o motor responder Busy. Nulo = não há
     * (ainda) animação: quem chama mostra a prévia parada.
     */
    suspend fun load(typeId: Int, scene: Int, generate: Boolean): List<ImageBitmap>? {
        val key = key(typeId, scene)
        memory.get(key)?.let { return it }
        val started = epoch
        withContext(EffectPreviewDisk) { readStrip(key) }?.let { if (started == epoch) memory.put(key, it); return it }
        if (!generate) return null
        var wait = FIRST_RETRY_MS
        repeat(MAX_ATTEMPTS) { attempt ->
            val result = withContext(EffectPreviewGpu) {
                memory.get(key)?.let { EffectSequenceResult(0, it) }
                    ?: try { render(typeId, WIDTH, HEIGHT, FRAMES, scene) }
                    catch (_: OutOfMemoryError) { trimMemory(); EffectSequenceResult(-1) }
            }
            if (result.code == 0 && result.frames.isNotEmpty()) {
                if (started == epoch) memory.put(key, result.frames)
                withContext(EffectPreviewDisk) { writeStrip(key, result.frames) }
                return result.frames
            }
            if (result.code != AureaEngine.ERR_BUSY || attempt == MAX_ATTEMPTS - 1) return null
            delay(wait)   // cancelável: fechar a ficha para aqui
            wait = (wait * 2).coerceAtMost(MAX_RETRY_MS)
        }
        return null
    }

    /** Só o que já está pronto (memória ou disco), sem GPU: o cartão segurado. */
    suspend fun cached(typeId: Int, scene: Int): List<ImageBitmap>? = load(typeId, scene, generate = false)

    private fun key(typeId: Int, scene: Int) = "$typeId@${WIDTH}x$HEIGHT-c$scene"

    private fun folder(): File = File(dir(), "animadas")

    /** Tira: os quadros empilhados na vertical; o nome guarda quantos são. */
    private fun readStrip(key: String): List<ImageBitmap>? {
        val folder = folder()
        val file = folder.listFiles()?.firstOrNull { it.name.startsWith("$key-n") && it.name.endsWith(".webp") }
            ?: return null
        val count = file.name.removePrefix("$key-n").removeSuffix(".webp").toIntOrNull()?.takeIf { it in 1..64 }
            ?: run { file.delete(); return null }
        val strip = runCatching { BitmapFactory.decodeFile(file.path) }.getOrNull() ?: run { file.delete(); return null }
        return try {
            val h = strip.height / count
            if (h <= 0 || strip.width <= 0) { file.delete(); return null }
            file.setLastModified(System.currentTimeMillis())   // LRU: usado agora
            List(count) { i -> Bitmap.createBitmap(strip, 0, i * h, strip.width, h).asImageBitmap() }
        } catch (_: Throwable) {
            file.delete(); null
        } finally {
            strip.recycle()
        }
    }

    private fun writeStrip(key: String, frames: List<ImageBitmap>) {
        val first = frames.firstOrNull() ?: return
        val folder = folder()
        if (!folder.isDirectory && !folder.mkdirs()) return
        val file = File(folder, "$key-n${frames.size}.webp")
        val tmp = File(file.path + ".tmp")
        runCatching {
            val strip = Bitmap.createBitmap(first.width, first.height * frames.size, Bitmap.Config.ARGB_8888)
            try {
                val canvas = android.graphics.Canvas(strip)
                frames.forEachIndexed { i, f -> canvas.drawBitmap(f.asAndroidBitmap(), 0f, (i * first.height).toFloat(), null) }
                tmp.outputStream().use { strip.compress(WEBP_LOSSY, 80, it) }
            } finally { strip.recycle() }
            if (!tmp.renameTo(file)) tmp.delete()
            enforceCap(folder)
        }.onFailure { tmp.delete() }   // disco cheio: só não guarda
    }

    /** Teto de disco: apaga as tiras menos usadas até caber. */
    private fun enforceCap(folder: File) {
        val files = folder.listFiles()?.filter { it.isFile && it.name.endsWith(".webp") } ?: return
        var total = files.sumOf { it.length() }
        if (total <= MAX_DISK_BYTES) return
        for (f in files.sortedBy { it.lastModified() }) {
            if (total <= MAX_DISK_BYTES) break
            val bytes = f.length()
            if (f.delete()) total -= bytes
        }
    }

    internal companion object {
        /** Mesmo tamanho pedido da prévia estática; o motor reduz pela classe do aparelho. */
        const val WIDTH = 320
        const val HEIGHT = 200
        const val FRAMES = 16
        /** ~15 qps: 16 quadros, ~1 s em laço. */
        const val FRAME_MS = 66L
        const val MAX_DISK_BYTES = 24L shl 20
        const val FIRST_RETRY_MS = 150L
        const val MAX_RETRY_MS = 2400L
        const val MAX_ATTEMPTS = 7
        val MAX_MEMORY_BYTES = if (com.aurea.aurea.engine.DeviceMemoryClass.low) 4 shl 20 else 9 shl 20
        @Suppress("DEPRECATION")
        val WEBP_LOSSY: Bitmap.CompressFormat =
            if (android.os.Build.VERSION.SDK_INT >= 30) Bitmap.CompressFormat.WEBP_LOSSY else Bitmap.CompressFormat.WEBP
    }
}

/** "Remover animações" (Opções do desenvolvedor / Acessibilidade): escala de animação 0. */
@Composable
fun rememberReduceMotion(): Boolean {
    val context = LocalContext.current
    return remember {
        runCatching { Settings.Global.getFloat(context.contentResolver, Settings.Global.ANIMATOR_DURATION_SCALE, 1f) == 0f }
            .getOrDefault(false)
    }
}

data class AnimatedPreviewState(
    val frames: List<ImageBitmap> = emptyList(),
    val loading: Boolean = false,
    val failed: Boolean = false,
)

/**
 * A sequência como estado do Compose. [store] nulo = não anima (desligado,
 * reduzir movimento, cartão solto): devolve vazio sem pedir nada. Sair da
 * composição cancela a espera do recuo.
 */
@Composable
fun rememberAnimatedEffectPreview(
    store: EffectAnimatedPreviewStore?,
    typeId: Int,
    scene: Int,
    generate: Boolean,
): AnimatedPreviewState {
    val state = produceState(AnimatedPreviewState(loading = store != null && generate), store, typeId, scene, generate) {
        if (store == null) { value = AnimatedPreviewState(); return@produceState }
        value = AnimatedPreviewState(loading = generate)
        val frames = store.load(typeId, scene, generate)
        value = if (frames.isNullOrEmpty()) AnimatedPreviewState(failed = generate) else AnimatedPreviewState(frames)
    }
    return state.value
}

/** Toca os quadros em laço (~15 qps) enquanto [playing]; parado, o primeiro. */
@Composable
fun AnimatedPreviewFrames(
    frames: List<ImageBitmap>,
    playing: Boolean,
    modifier: Modifier = Modifier,
    contentScale: ContentScale = ContentScale.Crop,
) {
    var index by remember(frames) { mutableIntStateOf(0) }
    if (playing && frames.size > 1) {
        LaunchedEffect(frames) {
            while (true) {
                delay(EffectAnimatedPreviewStore.FRAME_MS)
                index = (index + 1) % frames.size
            }
        }
    }
    val frame = frames.getOrNull(if (playing) index else 0) ?: return
    Image(frame, null, modifier.fillMaxSize(), contentScale = contentScale, filterQuality = FilterQuality.Low)
}

/** Seletor de cena da ficha: grupo de opções (um toque escolhe), com estado lido pelo leitor de tela. */
@Composable
fun EffectPreviewScenePicker(selected: Int, onSelect: (Int) -> Unit, modifier: Modifier = Modifier) {
    val groupLabel = stringResource(R.string.fx_preview_scene_label)
    Row(
        modifier
            .fillMaxWidth()
            .semantics { contentDescription = groupLabel }
            .selectableGroup(),
        horizontalArrangement = Arrangement.spacedBy(AureaDims.S2),
    ) {
        EffectPreviewScene.entries.forEach { scene ->
            val on = scene.id == selected
            Box(
                Modifier
                    .weight(1f)
                    .heightIn(min = AureaDims.MinTap)
                    .clip(AureaShape.Chip)
                    .background(if (on) AureaColors.Accent else AureaColors.Chip)
                    .selectable(selected = on, role = Role.RadioButton, onClick = { onSelect(scene.id) })
                    .padding(horizontal = 4.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    stringResource(scene.label),
                    style = AureaType.of(12f, FontWeight.W600, color = if (on) AureaColors.EditorPanel else AureaColors.Text),
                    textAlign = TextAlign.Center,
                    maxLines = 2,
                )
            }
        }
    }
}
