package com.aurea.aurea.state

import android.content.Context
import android.net.Uri
import android.os.ParcelFileDescriptor
import com.aurea.aurea.BuildConfig
import com.aurea.aurea.diagnostics.ProjectGuard
import com.aurea.aurea.engine.AureaEngine
import java.io.File

/**
 * "Exportar arquivo do projeto" / "Importar arquivo do projeto" no Android.
 * O formato e a religação da mídia são do motor (engine/src/project/
 * ProjectPackage.cpp); aqui só o que é do sistema: abrir as URIs `content://`
 * (o motor lê por `/proc/self/fd/N`) e copiar de/para o documento escolhido.
 */
internal object ProjectFile {
    /** Extensão do pacote (um ZIP sem compressão com o `.aurea` e a mídia). */
    const val EXTENSION = "aureaproj"

    /** Códigos de `aurea::Errc` que a tela diferencia. */
    const val ERR_IO = 10
    const val ERR_CORRUPT = 11
    const val ERR_NEWER = 12
    const val ERR_CHECKSUM = 13
    const val ERR_NOT_PROJECT = 17
    const val ERR_STORAGE_FULL = 28

    data class ExportOutcome(val code: Int, val included: Int, val skipped: Int)

    /** Grava o pacote do projeto em `target`. Roda em IO. */
    fun export(context: Context, engine: AureaEngine, projectPath: String, title: String, includeMedia: Boolean, target: Uri): ExportOutcome {
        val tmp = try { File.createTempFile("saida-", ".$EXTENSION", context.cacheDir) }
        catch (_: Exception) { return ExportOutcome(ERR_IO, 0, 0) }
        val opened = mutableListOf<ParcelFileDescriptor>()
        try {
            val media = mutableListOf<String>()
            if (includeMedia) {
                val refs = engine.projectFileMedia(projectPath) ?: return ExportOutcome(ERR_CORRUPT, 0, 0)
                for (i in 0 until refs.size / 4) {
                    val stored = refs[i * 4]
                    val resolved = refs[i * 4 + 1]
                    val readable = if (resolved.startsWith("content://")) {
                        // O motor lê o descritor pelo /proc; ele fica aberto até o fim da escrita.
                        runCatching { context.contentResolver.openFileDescriptor(Uri.parse(resolved), "r") }.getOrNull()
                            ?.also { opened += it }?.let { "/proc/self/fd/${it.fd}" } ?: ""
                    } else resolved.removePrefix("file://")
                    media += listOf(stored, readable, refs[i * 4 + 2].ifBlank { stored.substringAfterLast('/') })
                }
            }
            // O motor carrega o projeto inteiro para empacotar: etapa vigiada.
            ProjectGuard.begin(context, ProjectGuard.Stage.EXPORT_FILE, projectPath)
            val r = try {
                engine.exportProjectPackage(projectPath, tmp.absolutePath, title, BuildConfig.VERSION_NAME, media.toTypedArray())
            } finally {
                ProjectGuard.end(context, ProjectGuard.Stage.EXPORT_FILE)
            }
            if (r.isEmpty() || r[0] != 0) return ExportOutcome(r.getOrElse(0) { ERR_IO }, 0, 0)
            val out = context.contentResolver.openOutputStream(target, "wt") ?: return ExportOutcome(ERR_IO, 0, 0)
            out.use { o -> tmp.inputStream().use { it.copyTo(o, 1 shl 20) } }
            return ExportOutcome(0, r.getOrElse(1) { 0 }, r.getOrElse(2) { 0 })
        } catch (e: java.io.IOException) {
            return ExportOutcome(if (e.message?.contains("ENOSPC") == true) ERR_STORAGE_FULL else ERR_IO, 0, 0)
        } catch (_: RuntimeException) {
            // SecurityException, provedor que lança IllegalArgument/IllegalState...:
            // um documento ruim é erro na tela, nunca o app fechando.
            return ExportOutcome(ERR_IO, 0, 0)
        } finally {
            opened.forEach { runCatching { it.close() } }
            tmp.delete()
        }
    }

    data class ImportOutcome(val code: Int, val path: String = "", val title: String = "", val missing: Int = 0)

    /**
     * Lê o pacote de `source` para um projeto NOVO em `projectsDir` (nome livre
     * dado por `uniqueName`; nunca por cima de outro) e a mídia em
     * `projectsDir/midia/<nome>/`. Roda em IO.
     */
    fun import(
        context: Context, engine: AureaEngine, source: Uri, projectsDir: File, fallbackTitle: String,
        uniqueName: (String) -> String,
    ): ImportOutcome {
        val tmp = try { File.createTempFile("entrada-", ".$EXTENSION", context.cacheDir) }
        catch (_: Exception) { return ImportOutcome(ERR_IO) }
        try {
            val input = context.contentResolver.openInputStream(source) ?: return ImportOutcome(ERR_IO)
            input.use { i -> tmp.outputStream().use { i.copyTo(it, 1 shl 20) } }
            // O nome vem do próprio arquivo até o motor ler o título do pacote.
            val name = uniqueName(fallbackTitle)
            val project = File(projectsDir, "$name.aurea")
            val mediaDir = File(File(projectsDir, "midia"), name)
            // Pacote de outro aparelho: se o motor cair lendo, o projeto que
            // sobrar fica em quarentena na próxima abertura (ProjectGuard).
            ProjectGuard.begin(context, ProjectGuard.Stage.IMPORT, project.absolutePath)
            val r = try {
                engine.importProjectPackage(tmp.absolutePath, project.absolutePath, mediaDir.absolutePath)
            } finally {
                ProjectGuard.end(context, ProjectGuard.Stage.IMPORT)
            }
            val code = r.getOrNull(0)?.toIntOrNull() ?: ERR_IO
            if (code != 0) return ImportOutcome(code)
            return ImportOutcome(0, project.absolutePath, r.getOrNull(1).orEmpty().ifBlank { fallbackTitle }, r.getOrNull(4)?.toIntOrNull() ?: 0)
        } catch (e: java.io.IOException) {
            return ImportOutcome(if (e.message?.contains("ENOSPC") == true) ERR_STORAGE_FULL else ERR_IO)
        } catch (_: RuntimeException) {
            return ImportOutcome(ERR_IO)
        } finally {
            tmp.delete()
        }
    }
}
