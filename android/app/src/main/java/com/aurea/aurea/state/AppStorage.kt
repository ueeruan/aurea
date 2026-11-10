package com.aurea.aurea.state

import android.content.Context
import android.os.Environment
import android.util.Log
import com.aurea.aurea.R
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.ui.i18n.AppText
import java.io.File

// =============================================================================
//  ARMAZENAMENTO DOS PROJETOS (0.5.2) — par do StorageModel.swift do iOS.
//
//  O que o app guarda e que NÃO é cache: a mídia que os projetos usam, a que
//  nenhum projeto usa mais e as exportações que ficaram no app. A regra de o
//  que pode sair é do MOTOR (project/MediaStorage.cpp): aqui só as pastas do
//  app e a medida.
//
//  Pastas de mídia (as ÚNICAS de onde algo é apagado, e só pela porta do
//  motor, que confere de novo cada caminho):
//   - projetos/import-sources  cópias sem permissão persistente (copyIntoApp)
//   - projetos/midia/<nome>    mídia de um arquivo de projeto importado
//   - projetos/Media           PSD/GIF expandidos e sequências de imagens
//   - projetos/hyper           resultados do AureaHyper (viram projeto)
//   - projetos/voz             narrações gravadas
//   - aurea-ai                 vídeos do Aurea AI
//  Biblioteca (medida, nunca apagada daqui): LUTs, modelos 3D, fontes, formas
//  3D, sons e as fontes do Hyper.
// =============================================================================

/** Uma linha da tela de armazenamento. */
data class StorageLine(
    val id: String,
    val title: String,
    val detail: String,
    val bytes: Long,
    val files: Int,
    /** NONE = só informa; CACHE = limpa sem perguntar; CONFIRM = pergunta antes (não é cache). */
    val clean: StorageClean,
)

enum class StorageClean { NONE, CACHE, CONFIRM }

class AppStorage(
    private val context: Context,
    private val engine: AureaEngine,
    /** Projetos em quarentena (ProjectGuard): já derrubaram o app ao abrir, o motor não os lê. */
    private val quarantined: () -> Collection<String> = { emptyList() },
) {

    private fun text(id: Int) = AppText.get(context, id)
    private val files get() = context.filesDir
    val projectsDir: File get() = File(files, "projetos")

    /** As pastas de mídia do app (ver o cabeçalho). */
    fun managedRoots(): Array<String> = listOf(
        File(projectsDir, "import-sources"), File(projectsDir, "midia"), File(projectsDir, "Media"),
        File(projectsDir, "hyper"), File(projectsDir, "voz"), File(files, "aurea-ai"),
    ).map { it.absolutePath }.toTypedArray()

    /** Nenhuma biblioteca mora dentro das pastas de mídia no Android. */
    private val keepRoots = emptyArray<String>()

    private fun libraryDirs() = listOf(
        File(projectsDir, "luts"), File(projectsDir, "modelos"), File(projectsDir, "fontes"),
        File(projectsDir, "formas3d"), File(files, "hyper"), File(files, "sons"),
    )

    /** Onde ficam as cópias que a galeria recusou (Exporter.keepInAppFolder) e as do Aurea AI. */
    private fun exportDirs(): List<File> =
        listOf(Environment.DIRECTORY_MOVIES, Environment.DIRECTORY_PICTURES, Environment.DIRECTORY_DOWNLOADS)
            .mapNotNull { context.getExternalFilesDir(it)?.let { dir -> File(dir, "Aurea") } }

    /** O que apagar projetos libera: os caminhos (só os do motor) e a estimativa em bytes. */
    class Release(val paths: Array<String>, val bytes: Long)

    /**
     * Antes de apagar os arquivos de projeto: o motor lê TODOS (com .bak e cópias)
     * e devolve a mídia que só estes usavam. null = nada sai (projeto ilegível,
     * export em andamento, motor parado). Bloqueia: chame fora da main.
     */
    fun planRelease(projects: Collection<String>, editorOpen: Boolean, neverRead: Collection<String> = emptyList()): Release? {
        if (projects.isEmpty()) return null
        val r = engine.mediaReleasePlan(projectsDir.absolutePath, projects.toTypedArray(), managedRoots(), keepRoots,
            (quarantined() + neverRead).toTypedArray(), editorOpen) ?: return null
        if (r.size < 3 || r[0] != "0") {
            Log.i(TAG, "apagar projeto: midia mantida (projeto ilegivel ou export em andamento; ilegiveis=${r.getOrNull(2)})")
            return null
        }
        return Release(r.copyOfRange(3, r.size), r[1].toLongOrNull() ?: 0L)
    }

    /** Depois de apagar os arquivos de projeto: a mídia do plano, pela porta do motor. */
    fun release(plan: Release): Long {
        if (plan.paths.isEmpty()) return 0L
        val r = engine.removeManagedMedia(plan.paths, managedRoots(), keepRoots)
        Log.i(TAG, "apagar projeto: ${r[1]} arquivo(s) de midia liberado(s), ${r[0] shr 20} MB")
        return r[0]
    }

    private class Scan(val usedBytes: Long, val usedFiles: Int, val orphanBytes: Long, val orphanFiles: Int,
                       val orphans: Array<String>, val blocked: Boolean)

    private fun scanMedia(): Scan {
        val r = engine.mediaStorageScan(projectsDir.absolutePath, managedRoots(), keepRoots, quarantined().toTypedArray(),
            ORPHAN_MIN_AGE_S, false)
        if (r == null || r.size < 6) {
            val (b, n) = measure(managedRoots().map(::File))
            return Scan(b, n, 0L, 0, emptyArray(), true)
        }
        val blocked = r[0] != "0"
        return Scan(r[1].toLongOrNull() ?: 0L, r[2].toIntOrNull() ?: 0, r[3].toLongOrNull() ?: 0L, r[4].toIntOrNull() ?: 0,
            if (blocked) emptyArray() else r.copyOfRange(6, r.size), blocked)
    }

    /**
     * As linhas que não são cache: mídia dos projetos, mídia sem projeto,
     * exportações guardadas no app. Bloqueia (lê todos os projetos): fora da main.
     */
    fun scan(): List<StorageLine> {
        val media = scanMedia()
        val (libBytes, libFiles) = measure(libraryDirs())
        val (expBytes, expFiles) = measure(exportDirs())
        return listOf(
            StorageLine(PROJECT_MEDIA, text(R.string.storage_media_title), text(R.string.storage_media_detail),
                media.usedBytes + libBytes, media.usedFiles + libFiles, StorageClean.NONE),
            StorageLine(ORPHAN_MEDIA, text(R.string.storage_orphans_title),
                text(if (media.blocked) R.string.storage_orphans_blocked else R.string.storage_orphans_detail),
                media.orphanBytes, media.orphanFiles, if (media.blocked) StorageClean.NONE else StorageClean.CONFIRM),
            StorageLine(APP_EXPORTS, text(R.string.storage_exports_title), text(R.string.storage_exports_detail),
                expBytes, expFiles, StorageClean.CONFIRM),
        )
    }

    /** Limpa uma linha que não é cache (a pessoa já confirmou). Devolve os bytes liberados. */
    fun clean(id: String): Long = when (id) {
        // Varre de novo: apaga o que é órfão AGORA, não o que era quando a tela abriu.
        ORPHAN_MEDIA -> scanMedia().let { s ->
            if (s.blocked || s.orphans.isEmpty()) 0L else engine.removeManagedMedia(s.orphans, managedRoots(), keepRoots)[0]
        }
        APP_EXPORTS -> {
            var freed = 0L
            exportDirs().forEach { dir ->
                dir.walkBottomUp().filter { it.isFile }.forEach { f ->
                    val n = f.length()
                    if (f.delete()) freed += n
                }
            }
            freed
        }
        else -> 0L
    }

    private fun measure(roots: List<File>): Pair<Long, Int> {
        var bytes = 0L
        var count = 0
        roots.forEach { root ->
            if (root.exists()) root.walkTopDown().filter { it.isFile }.forEach { bytes += it.length(); count++ }
        }
        return bytes to count
    }

    companion object {
        private const val TAG = "AureaStorage"
        const val PROJECT_MEDIA = "midia-projetos"
        const val ORPHAN_MEDIA = "midia-orfa"
        const val APP_EXPORTS = "exportacoes"
        /** Importação mais nova que isto nunca é órfã (pode não ter chegado ao .aurea ainda). */
        const val ORPHAN_MIN_AGE_S = 24L * 3600
    }
}
