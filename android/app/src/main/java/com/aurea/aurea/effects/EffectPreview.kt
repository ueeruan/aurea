package com.aurea.aurea.effects

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.LruCache
import androidx.compose.runtime.Composable
import androidx.compose.runtime.produceState
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.graphics.asImageBitmap
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

// =============================================================================
//  AS PRÉVIAS DOS EFEITOS (Fase 7.3 §13–§15, §57; Fase 8I §59–63).
//
//  Cada efeito mostra o que ele FAZ, numa imagem. A prévia é o efeito real
//  rodando sobre a foto do app (assets/previa_efeitos.jpg) — não um desenho
//  genérico por categoria.
//
//  Quatro regras que a infraestrutura tem de cumprir:
//
//   1. NADA BLOQUEIA A LISTA. O navegador abre com a cartela genérica na tela;
//      a prévia entra quando fica pronta. Nenhuma geração no main thread.
//   2. SÓ O QUE ESTÁ NA TELA, UMA DE CADA VEZ. Cada cartão pede a sua ao entrar
//      na tela (produceState); o motor renderiza UMA prévia por vez numa fila
//      própria (limitedParallelism(1)) — antes eram até N núcleos do
//      Dispatchers.Default presos esperando o mesmo mutex de render do motor.
//      Cartão que saiu da tela antes da vez dele é cancelado sem custo.
//   3. NADA RECOMPUTA. Memória (LruCache por bytes) e disco
//      (`cache/motor/previas/<versão>/<tipo>@<l>x<a>.webp`; WebP 90 — é foto,
//      PNG seria várias vezes maior em disco). A versão é a da
//      instalação do app: app atualizado (efeito ou foto novos) → pasta nova,
//      a antiga é apagada. Da segunda abertura do navegador em diante, nenhuma
//      prévia volta à GPU (nem compila o pipeline do efeito).
//   4. FALHOU? MOSTRA A CARTELA. Efeito temporal, ou sem GPU, cai na cartela
//      genérica com o ícone da categoria — nunca num buraco.
// =============================================================================

/** Onde a prévia pronta de um efeito fica guardada. */
class EffectPreviewStore(
    cacheDir: File,
    private val version: String,
    private val render: (typeId: Int, width: Int, height: Int) -> ImageBitmap?,
) {
    private val root = File(cacheDir, "previas")
    private val dir = File(root, version)
    @Volatile private var dirReady = false

    /** Em memória: por BYTES, não por número — 128 prévias de 256² são 32 MB. */
    private val memory = object : LruCache<String, ImageBitmap>(MAX_MEMORY_BYTES) {
        override fun sizeOf(key: String, value: ImageBitmap) = value.width * value.height * 4
    }

    /** O que já está pronto agora, sem gerar nada. */
    fun peek(typeId: Int, width: Int, height: Int): ImageBitmap? = memory.get(key(typeId, width, height))

    /**
     * A prévia do efeito: memória → disco → GPU. Disco e GPU FORA do main
     * thread; a GPU numa fila de um só.
     */
    suspend fun load(typeId: Int, width: Int, height: Int): ImageBitmap? {
        val k = key(typeId, width, height)
        memory.get(k)?.let { return it }
        val file = File(dir, "$k.webp")
        withContext(Disk) { readPng(file) }?.let { memory.put(k, it); return it }
        val bmp = withContext(Gpu) {
            // Outro cartão com o mesmo efeito pode ter gerado enquanto este esperava.
            memory.get(k) ?: render(typeId, width, height)
        } ?: return null
        memory.put(k, bmp)
        withContext(Disk) { writePng(file, bmp) }
        return bmp
    }

    /** Quantas prévias já estão em memória (o painel de desempenho mostra). */
    fun residentCount(): Int = memory.size()

    /** Pressão de memória (Fase 8B): solta as prévias em memória; refeitas sob demanda. */
    fun trimMemory() = memory.evictAll()

    /** "Limpar cache" (main thread): renomeia na hora, apaga em segundo plano. */
    fun clear() {
        memory.evictAll()
        dirReady = false
        val trash = File(root.parentFile, "previas.apagar.${System.nanoTime()}")
        if (root.renameTo(trash)) kotlin.concurrent.thread(name = "aurea-previas-limpar") { trash.deleteRecursively() }
    }

    /** Cria a pasta da versão e apaga as de versões antigas (uma vez, no disco). */
    private fun ensureDir() {
        if (dirReady) return
        root.listFiles()?.forEach { if (it.name != version) it.deleteRecursively() }
        dir.mkdirs()
        dirReady = true
    }

    private fun readPng(file: File): ImageBitmap? {
        ensureDir()
        if (!file.isFile) return null
        return runCatching { BitmapFactory.decodeFile(file.path)?.asImageBitmap() }.getOrNull()
            ?: run { file.delete(); null }   // arquivo estragado: refaz
    }

    private fun writePng(file: File, bmp: ImageBitmap) {
        ensureDir()
        val tmp = File(file.path + ".tmp")
        runCatching {
            tmp.outputStream().use { bmp.asAndroidBitmap().compress(WEBP_LOSSY, 90, it) }
            if (!tmp.renameTo(file)) tmp.delete()
        }.onFailure { tmp.delete() }   // disco cheio: só não guarda
    }

    private fun key(typeId: Int, width: Int, height: Int) = "$typeId@${width}x$height"

    private companion object {
        const val MAX_MEMORY_BYTES = 16 * 1024 * 1024
        /** WebP com perda: o nome novo existe a partir do Android 11. */
        @Suppress("DEPRECATION")
        val WEBP_LOSSY: Bitmap.CompressFormat =
            if (android.os.Build.VERSION.SDK_INT >= 30) Bitmap.CompressFormat.WEBP_LOSSY else Bitmap.CompressFormat.WEBP
        /** O motor serializa o render: mais de uma aqui só prende threads. */
        @OptIn(kotlinx.coroutines.ExperimentalCoroutinesApi::class)
        val Gpu = Dispatchers.Default.limitedParallelism(1)
        @OptIn(kotlinx.coroutines.ExperimentalCoroutinesApi::class)
        val Disk = Dispatchers.IO.limitedParallelism(2)
    }
}

/**
 * A prévia de um efeito como estado do Compose.
 *
 * Devolve `null` enquanto ela não existe — e quem chama DESENHA a cartela
 * genérica nesse caso. É por isso que a lista nunca "pisca vazia": o cartão
 * tem sempre algo para mostrar.
 */
@Composable
fun rememberEffectPreview(
    store: EffectPreviewStore?,
    effectTypeId: Int,
    width: Int,
    height: Int,
): ImageBitmap? {
    if (store == null) return null
    val state = produceState(store.peek(effectTypeId, width, height), effectTypeId, width, height) {
        value = store.peek(effectTypeId, width, height) ?: store.load(effectTypeId, width, height)
    }
    return state.value
}
