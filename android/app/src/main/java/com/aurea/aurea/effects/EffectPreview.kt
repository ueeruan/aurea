package com.aurea.aurea.effects

import android.util.LruCache
import androidx.compose.runtime.Composable
import androidx.compose.runtime.produceState
import androidx.compose.ui.graphics.ImageBitmap

// =============================================================================
//  AS PRÉVIAS DOS EFEITOS (Fase 7.3 §13–§15, §57).
//
//  Cada efeito mostra o que ele FAZ, numa imagem. A prévia é o efeito real
//  rodando sobre uma cartela de demonstração — não um desenho genérico por
//  categoria.
//
//  Três regras que a infraestrutura tem de cumprir:
//
//   1. NADA BLOQUEIA A LISTA. O navegador abre com a cartela genérica na tela;
//      a prévia entra quando fica pronta. Nenhuma geração no main thread.
//   2. NADA RECOMPUTA. O resultado é guardado em memória (LruCache por bytes) e
//      em disco (`filesDir/previas/<typeId>.png`), e vale para sempre: o
//      resultado de um efeito sobre a cartela não depende do projeto.
//   3. FALHOU? MOSTRA A CARTELA. Efeito temporal, ou sem GPU, cai na cartela
//      genérica com o ícone da categoria — nunca num buraco.
// =============================================================================

/** Onde a prévia pronta de um efeito fica guardada. */
class EffectPreviewStore(
    cacheDir: java.io.File,
    private val render: (typeId: Int, width: Int, height: Int) -> ImageBitmap?,
) {
    private val dir = java.io.File(cacheDir, "previas").apply { mkdirs() }

    /** Em memória: por BYTES, não por número — 128 prévias de 256² são 32 MB. */
    private val memory = object : LruCache<String, ImageBitmap>(MAX_MEMORY_BYTES) {
        override fun sizeOf(key: String, value: ImageBitmap) = value.width * value.height * 4
    }

    /** O que já está pronto agora, sem gerar nada. */
    fun peek(typeId: Int, width: Int, height: Int): ImageBitmap? = memory.get(key(typeId, width, height))

    /**
     * A prévia do efeito, gerando se preciso. Roda FORA do main thread quando
     * gera; com a prévia em memória, devolve na hora.
     */
    suspend fun load(typeId: Int, width: Int, height: Int): ImageBitmap? {
        val k = key(typeId, width, height)
        memory.get(k)?.let { return it }
        val bmp = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.Default) {
            render(typeId, width, height)
        } ?: return null
        memory.put(k, bmp)
        return bmp
    }

    /** Quantas prévias já estão em memória (o painel de desempenho mostra). */
    fun residentCount(): Int = memory.size()

    /** Pressão de memória (Fase 8B): solta as prévias em memória; refeitas sob demanda. */
    fun trimMemory() = memory.evictAll()

    fun clear() {
        memory.evictAll()
        dir.listFiles()?.forEach { it.delete() }
    }

    private fun key(typeId: Int, width: Int, height: Int) = "$typeId@${width}x$height"

    private companion object {
        const val MAX_MEMORY_BYTES = 16 * 1024 * 1024
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
