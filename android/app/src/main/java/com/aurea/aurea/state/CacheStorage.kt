package com.aurea.aurea.state

import android.content.Context
import com.aurea.aurea.R
import com.aurea.aurea.ui.i18n.AppText
import java.io.File

// =============================================================================
//  ARMAZENAMENTO DO APP (Fase 8B §49–52).
//
//  Tudo o que o Aurea grava e que NÃO é projeto, num lugar só: onde fica,
//  quanto ocupa de verdade (bytes no disco, medidos agora), o teto e quem
//  limpa. Projetos (`filesDir/projetos/*.aurea`), presets, fontes e modelos
//  importados NUNCA entram aqui — são do usuário.
//
//  Limpeza automática: ao abrir o app e ao fim de cada export, cada tipo acima
//  do teto perde os arquivos mais antigos (LRU pela data de modificação).
//
//  0.5.2: o PreRender (`motor/pre-render`) é do MOTOR — a limpeza por idade
//  apagava quadros de dentro de uma pasta viva; agora o motor apaga as pastas
//  sem dono na abertura e a tela pede a ele ("Limpar PreRender"). A mídia
//  otimizada (`motor/preview-proxies`) tem linha própria. O resto do armazenamento
//  (mídia dos projetos, órfã, exportações guardadas) está em [AppStorage].
//
//  Temporários com dono e ciclo de vida (§52):
//   - export: `cacheDir/export/` — sucesso copia para a galeria e apaga;
//     falha e cancelamento apagam na hora; crash/force kill = sobra que a
//     limpeza da próxima abertura apaga (teto 0 fora de um export);
//   - legendas: `cacheDir/legendas/` — o áudio extraído é apagado no `finally`
//     da transcrição; sobra de crash, idem (teto 0).
// =============================================================================

/** Um tipo de arquivo regenerável, com o tamanho medido agora. */
data class CacheKind(
    val id: String,
    val title: String,
    val detail: String,
    val bytes: Long,
    val files: Int,
    /** Teto em bytes; acima dele a limpeza automática apaga os mais antigos. */
    val limitBytes: Long,
)

class CacheStorage(private val context: Context) {

    private fun text(id: Int) = AppText.get(context, id)
    private val motor get() = File(context.cacheDir, "motor")
    private val previews get() = File(motor, "previas")
    private val preRender get() = File(motor, "pre-render")
    private val proxies get() = File(motor, "preview-proxies")
    /** Pipelines e o resto do motor: fora prévias, PreRender e proxies (linhas próprias). */
    private fun pipeline(f: File) = !f.path.startsWith(previews.path) && !f.path.startsWith(preRender.path) &&
        !f.path.startsWith(proxies.path)
    private val export get() = File(context.cacheDir, "export")
    private val captions get() = File(context.cacheDir, "legendas")
    private val projects get() = File(context.filesDir, "projetos")
    private val projectThumbs get() = File(projects, ".miniaturas")

    /** Os tipos, com os tamanhos lidos do disco. Chamar fora da main thread. */
    fun scan(exporting: Boolean = false): List<CacheKind> {
        val (pipeBytes, pipeFiles) = measure(motor, ::pipeline)
        val (prevBytes, prevFiles) = measure(previews)
        val (preBytes, preFiles) = measure(preRender)
        val (proxyBytes, proxyFiles) = measure(proxies)
        val (expBytes, expFiles) = measure(export)
        val (capBytes, capFiles) = measure(captions)
        val orphans = orphanProjectThumbs()
        val known = setOf(motor.name, export.name, captions.name)
        val others = context.cacheDir.listFiles()?.filter { it.name !in known } ?: emptyList()
        var otherBytes = 0L
        var otherFiles = 0
        others.forEach { f -> measure(f).let { otherBytes += it.first; otherFiles += it.second } }
        return listOf(
            CacheKind(PIPELINES, text(R.string.app_cache_pipelines_title), text(R.string.app_cache_pipelines_detail),
                pipeBytes, pipeFiles, PIPELINE_LIMIT),
            CacheKind(PREVIEWS, text(R.string.app_cache_previews_title), text(R.string.app_cache_previews_detail),
                prevBytes, prevFiles, PREVIEW_LIMIT),
            // O motor limita e apaga (PreRenderCache: 4 GiB, metade do livre); aqui só a medida.
            CacheKind(PRERENDER, text(R.string.storage_prerender_title), text(R.string.storage_prerender_detail),
                preBytes, preFiles, Long.MAX_VALUE),
            CacheKind(PROXIES, text(R.string.storage_proxies_title), text(R.string.storage_proxies_detail),
                proxyBytes, proxyFiles, PROXY_LIMIT),
            CacheKind(EXPORT, text(R.string.app_cache_export_title), text(R.string.app_cache_export_detail),
                expBytes, expFiles, if (exporting) Long.MAX_VALUE else 0L),
            CacheKind(CAPTIONS, text(R.string.app_cache_captions_title), text(R.string.app_cache_captions_detail),
                capBytes, capFiles, 0L),
            CacheKind(ORPHAN_THUMBS, text(R.string.app_cache_orphans_title), text(R.string.app_cache_orphans_detail),
                orphans.sumOf { it.length() }, orphans.size, 0L),
            CacheKind(OTHER, text(R.string.app_cache_other_title), text(R.string.app_cache_other_detail),
                otherBytes, otherFiles, OTHER_LIMIT),
        )
    }

    /** Apaga um tipo inteiro. Devolve os bytes liberados. Nunca toca em projeto. */
    fun clear(id: String): Long = when (id) {
        PIPELINES -> deleteWhere(motor, ::pipeline)
        PREVIEWS -> deleteWhere(previews) { true }
        // Do motor: o EditorStore pede a ele (clearPreRenderCache / clearProxyCache).
        PRERENDER, PROXIES -> 0L
        EXPORT -> deleteWhere(export) { true }
        CAPTIONS -> deleteWhere(captions) { true }
        ORPHAN_THUMBS -> orphanProjectThumbs().sumOf { f -> f.length().also { f.delete() } }
        OTHER -> {
            val known = setOf(motor.name, export.name, captions.name)
            context.cacheDir.listFiles()?.filter { it.name !in known }?.sumOf { f -> deleteWhere(f) { true } } ?: 0L
        }
        else -> 0L
    }

    /**
     * Limpeza automática: cada tipo acima do teto perde os arquivos mais
     * ANTIGOS até caber. `exporting` = há um export em curso (a pasta dele
     * não é mexida). Devolve os bytes liberados.
     */
    fun enforceLimits(exporting: Boolean = false): Long {
        var freed = 0L
        freed += trimToLimit(listFiles(motor, ::pipeline), PIPELINE_LIMIT)
        freed += trimToLimit(listFiles(previews), PREVIEW_LIMIT)
        // A mídia otimizada seguia o teto do motor inteiro (64 MB) por estar dentro
        // dele: o mesmo teto, agora só dela. O PreRender nunca é aparado aqui.
        freed += trimToLimit(listFiles(proxies), PROXY_LIMIT)
        if (!exporting) freed += trimToLimit(listFiles(export), 0L)
        freed += trimToLimit(listFiles(captions), 0L)
        freed += orphanProjectThumbs().sumOf { f -> f.length().also { f.delete() } }
        val known = setOf(motor.name, export.name, captions.name)
        val others = context.cacheDir.listFiles()?.filter { it.name !in known }?.flatMap { listFiles(it) } ?: emptyList()
        freed += trimToLimit(others, OTHER_LIMIT)
        return freed
    }

    private fun orphanProjectThumbs(): List<File> {
        val alive = projects.listFiles { f -> f.extension == "aurea" }?.map { it.nameWithoutExtension }?.toSet()
            ?: return emptyList()
        return projectThumbs.listFiles()?.filter { it.isFile && it.nameWithoutExtension !in alive } ?: emptyList()
    }

    private fun listFiles(root: File, keep: (File) -> Boolean = { true }): List<File> =
        if (!root.exists()) emptyList()
        else if (root.isFile) listOf(root).filter(keep)
        else root.walkBottomUp().filter { it.isFile && keep(it) }.toList()

    private fun measure(root: File, keep: (File) -> Boolean = { true }): Pair<Long, Int> {
        val files = listFiles(root, keep)
        return files.sumOf { it.length() } to files.size
    }

    private fun deleteWhere(root: File, keep: (File) -> Boolean): Long {
        var freed = 0L
        listFiles(root, keep).forEach { f ->
            val n = f.length()
            if (f.delete()) freed += n
        }
        // Pastas vazias que sobraram (menos a raiz das conhecidas, que o app recria).
        if (root.isDirectory) root.walkBottomUp().filter { it.isDirectory && it != root }.forEach { it.delete() }
        else if (root.exists() && root.isFile) root.delete()
        return freed
    }

    private fun trimToLimit(files: List<File>, limit: Long): Long {
        var total = files.sumOf { it.length() }
        if (total <= limit) return 0L
        var freed = 0L
        for (f in files.sortedBy { it.lastModified() }) {
            if (total <= limit) break
            val n = f.length()
            if (f.delete()) {
                total -= n
                freed += n
            }
        }
        return freed
    }

    companion object {
        const val PIPELINES = "pipelines"
        const val PREVIEWS = "previas"
        const val EXPORT = "export"
        const val CAPTIONS = "legendas"
        const val ORPHAN_THUMBS = "capas"
        const val OTHER = "outros"
        const val PRERENDER = "prerender"
        const val PROXIES = "proxies"

        /** Cache de pipeline do Vulkan: poucos MB; o teto só pega algo anormal. */
        const val PIPELINE_LIMIT = 64L shl 20
        const val PREVIEW_LIMIT = 64L shl 20
        const val PROXY_LIMIT = 64L shl 20
        const val OTHER_LIMIT = 32L shl 20
    }
}
