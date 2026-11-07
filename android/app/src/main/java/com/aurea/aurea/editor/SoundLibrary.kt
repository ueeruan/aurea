package com.aurea.aurea.editor

import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * A biblioteca de sons da pessoa (SoundLibrary): pastas e arquivos que ela importa
 * (efeitos, músicas) ficam COPIADOS em `files/sons`, para usar em qualquer projeto
 * mesmo que o original saia do aparelho. Uma pasta importada vira uma seção; cada
 * subpasta dela, outra ("SFX / Bells"). O índice guarda título e duração para a lista
 * abrir sem reler os arquivos. Par do iOS: `SoundLibrary` em AudioBrowserView.swift.
 */
internal object SoundLibrary {
    private val AUDIO = setOf("mp3", "wav", "m4a", "aac", "ogg", "oga", "opus", "flac", "aif", "aiff", "caf", "wma", "amr")
    /** Uma pasta com mais que isto vira várias importações: protege o armazenamento. */
    private const val MAX_FILES = 3000
    private const val MAX_DEPTH = 6

    data class Folder(val name: String, val sounds: List<AudioEntry>)

    private fun root(context: Context) = File(context.filesDir, "sons").apply { mkdirs() }
    private fun indexFile(context: Context) = File(root(context), "index.json")

    private data class Item(val category: String, val path: String, val title: String, val durationMs: Long)

    private fun read(context: Context): MutableList<Item> = runCatching {
        val array = JSONArray(indexFile(context).readText())
        MutableList(array.length()) { i ->
            val o = array.getJSONObject(i)
            Item(o.getString("c"), o.getString("p"), o.optString("t"), o.optLong("d"))
        }
    }.getOrDefault(mutableListOf())

    private fun write(context: Context, items: List<Item>) {
        val array = JSONArray()
        items.forEach { array.put(JSONObject().put("c", it.category).put("p", it.path).put("t", it.title).put("d", it.durationMs)) }
        val target = indexFile(context)
        val temp = File(target.parentFile, "index.json.tmp")
        temp.writeText(array.toString())
        if (!temp.renameTo(target)) { target.delete(); temp.renameTo(target) }
    }

    /** As seções, em ordem alfabética; arquivo apagado sai da lista. Fora da thread principal. */
    fun load(context: Context): List<Folder> {
        val items = read(context)
        val alive = items.filter { File(it.path).isFile }
        if (alive.size != items.size) write(context, alive)
        return alive.groupBy { it.category }.toSortedMap(String.CASE_INSENSITIVE_ORDER).map { (name, list) ->
            Folder(name, list.sortedBy { it.title.lowercase() }.map { AudioEntry(it.path, it.title, name, "", "", it.durationMs, 0L) })
        }
    }

    private fun safe(name: String) = name.replace(Regex("[\\\\/:*?\"<>|\\u0000-\\u001f]"), "_").trim().ifEmpty { "_" }.take(80)

    private fun displayName(context: Context, uri: Uri): String? = runCatching {
        context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) c.getString(0) else null
        }
    }.getOrNull()

    private fun isAudio(name: String, mime: String?) =
        name.substringAfterLast('.', "").lowercase() in AUDIO || mime?.startsWith("audio/") == true

    /** Uma pasta inteira do aparelho (com as subpastas). Devolve quantos sons entraram. */
    fun importTree(context: Context, tree: Uri, progress: (Int, Int) -> Unit): Int {
        val rootId = DocumentsContract.getTreeDocumentId(tree)
        val rootName = displayName(context, DocumentsContract.buildDocumentUriUsingTree(tree, rootId)) ?: "Sons"
        val found = ArrayList<Triple<Uri, String, String>>()
        fun walk(docId: String, rel: String, depth: Int) {
            if (depth > MAX_DEPTH || found.size >= MAX_FILES) return
            val children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId)
            context.contentResolver.query(children, arrayOf(DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE), null, null, null)?.use { c ->
                while (c.moveToNext() && found.size < MAX_FILES) {
                    val id = c.getString(0) ?: continue
                    val name = c.getString(1) ?: continue
                    val mime = c.getString(2)
                    if (mime == DocumentsContract.Document.MIME_TYPE_DIR) walk(id, if (rel.isEmpty()) name else "$rel / $name", depth + 1)
                    else if (isAudio(name, mime)) found += Triple(DocumentsContract.buildDocumentUriUsingTree(tree, id), rel, name)
                }
            }
        }
        walk(rootId, "", 0)
        return copy(context, found.map { (uri, rel, name) -> Triple(uri, if (rel.isEmpty()) rootName else "$rootName / $rel", name) }, progress)
    }

    /** Arquivos soltos: entram na seção [category]. */
    fun importFiles(context: Context, uris: List<Uri>, category: String, progress: (Int, Int) -> Unit): Int =
        copy(context, uris.mapNotNull { uri ->
            val name = displayName(context, uri) ?: uri.lastPathSegment ?: return@mapNotNull null
            if (isAudio(name, context.contentResolver.getType(uri))) Triple(uri, category, name) else null
        }, progress)

    private fun copy(context: Context, files: List<Triple<Uri, String, String>>, progress: (Int, Int) -> Unit): Int {
        val items = read(context)
        val known = items.mapTo(HashSet()) { it.path }
        var added = 0
        files.forEachIndexed { index, (uri, category, name) ->
            progress(index, files.size)
            val dir = File(root(context), category.split(" / ").joinToString(File.separator) { safe(it) }).apply { mkdirs() }
            val target = File(dir, safe(name))
            if (target.absolutePath in known) return@forEachIndexed   // a mesma pasta importada de novo
            try {
                context.contentResolver.openInputStream(uri)?.use { input -> target.outputStream().use { input.copyTo(it) } } ?: return@forEachIndexed
            } catch (_: Exception) { target.delete(); return@forEachIndexed }
            // Só entra o que o aparelho consegue tocar (o mesmo teste da biblioteca de áudio).
            val probe = OmegaAudioLibrary.probe(context, target.absolutePath, name.substringBeforeLast('.'))
            if (probe == null) { target.delete(); return@forEachIndexed }
            items += Item(category, target.absolutePath, name.substringBeforeLast('.'), probe.durationMs)
            known += target.absolutePath
            added++
        }
        progress(files.size, files.size)
        write(context, items)
        return added
    }

    /** O som está na biblioteca (e não num projeto)? */
    fun owns(context: Context, path: String): Boolean = path.startsWith(root(context).absolutePath + File.separator)

    /**
     * Uma cópia do som para os projetos (a mesma pasta das cópias de importação): tirar
     * a seção da biblioteca depois não deixa o projeto sem áudio. Mesmo nome e tamanho =
     * a mesma cópia, sem duplicar. Fora da thread principal.
     */
    fun projectCopy(context: Context, path: String): String {
        val source = File(path)
        val dir = File(context.filesDir, "projetos/import-sources").apply { mkdirs() }
        val target = File(dir, "som-${source.length()}-${safe(source.name)}")
        if (!(target.isFile && target.length() == source.length())) {
            val temp = File(dir, target.name + ".tmp")
            source.copyTo(temp, overwrite = true)
            if (!temp.renameTo(target)) { target.delete(); temp.renameTo(target) }
        }
        return target.absolutePath
    }

    /** Tira a seção da biblioteca e apaga as cópias dela. */
    fun remove(context: Context, category: String) {
        val items = read(context)
        items.filter { it.category == category }.forEach { File(it.path).delete() }
        write(context, items.filterNot { it.category == category })
        // Pastas que ficaram vazias.
        root(context).walkBottomUp().filter { it.isDirectory && it != root(context) && it.listFiles().isNullOrEmpty() }.forEach { it.delete() }
    }
}
