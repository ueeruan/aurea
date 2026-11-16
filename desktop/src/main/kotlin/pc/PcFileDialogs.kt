package com.aurea.pc

import android.net.Uri
import androidx.activity.result.contract.ActivityResultContracts.PickVisualMedia
import java.awt.FileDialog
import java.awt.Frame
import java.io.File
import java.util.prefs.Preferences

/**
 * O seletor de arquivos do Windows no lugar da galeria/documentos do Android.
 * Lembra a última pasta usada (como o seletor do sistema faz no celular).
 */
object PcFileDialogs {
    class Filter(val label: String, val extensions: List<String>)

    private val prefs = Preferences.userRoot().node("aurea-pc/dialogs")
    private val VIDEO = listOf("mp4", "mov", "m4v", "mkv", "webm", "avi", "3gp", "mts", "m2ts", "wmv", "gif")
    private val IMAGE = listOf("png", "jpg", "jpeg", "webp", "gif", "bmp", "heic", "heif", "tif", "tiff", "psd", "svg")
    private val AUDIO = listOf("mp3", "wav", "m4a", "aac", "ogg", "opus", "flac", "wma")

    fun filterFor(type: PickVisualMedia.VisualMediaType): Filter = when (type) {
        PickVisualMedia.ImageOnly -> Filter("Imagens", IMAGE)
        PickVisualMedia.VideoOnly -> Filter("Vídeos", VIDEO)
        is PickVisualMedia.SingleMimeType -> filterForMimes(arrayOf(type.mimeType)) ?: Filter("Arquivos", IMAGE + VIDEO)
        else -> Filter("Fotos e vídeos", IMAGE + VIDEO)
    }

    fun filterForMimes(mimes: Array<String>): Filter? {
        if (mimes.isEmpty() || mimes.any { it == "*/*" }) return null
        val exts = mimes.flatMap { m ->
            when {
                m.startsWith("video/") && m.endsWith("*") -> VIDEO
                m.startsWith("image/") && m.endsWith("*") -> IMAGE
                m.startsWith("audio/") && m.endsWith("*") -> AUDIO
                m == "application/json" -> listOf("json", "lottie")
                m == "application/zip" -> listOf("zip", "aureapkg", "aureaproj")
                m.startsWith("font/") || m.contains("font") -> listOf("ttf", "otf", "ttc")
                m.startsWith("model/") -> listOf("glb", "gltf", "obj", "fbx")
                m == "application/octet-stream" -> return null
                else -> listOfNotNull(android.webkit.MimeTypeMap.getSingleton().getExtensionFromMimeType(m))
            }
        }.distinct()
        return if (exts.isEmpty()) null else Filter("Arquivos", exts)
    }

    private fun owner(): Frame? = PcApp.window as? Frame

    fun open(filter: Filter?, multiple: Boolean, onResult: (List<Uri>) -> Unit) = PcApp.runOnUi {
        val d = FileDialog(owner(), "Aurea", FileDialog.LOAD)
        d.isMultipleMode = multiple
        prefs.get("lastDir", null)?.let { d.directory = it }
        // No Windows o FilenameFilter não vale; o padrão "*.a;*.b" no nome filtra.
        if (filter != null) d.file = filter.extensions.joinToString(";") { "*.$it" }
        d.isVisible = true
        val files = d.files?.toList().orEmpty()
        files.firstOrNull()?.parentFile?.let { prefs.put("lastDir", it.absolutePath) }
        onResult(files.map { Uri.fromFile(it) })
    }

    fun save(suggestedName: String, mime: String, onResult: (Uri?) -> Unit) = PcApp.runOnUi {
        val d = FileDialog(owner(), "Aurea", FileDialog.SAVE)
        d.directory = prefs.get("lastSaveDir", PcPaths.videos.absolutePath)
        d.file = suggestedName
        d.isVisible = true
        val name = d.file
        if (name == null) { onResult(null); return@runOnUi }
        val dir = File(d.directory)
        prefs.put("lastSaveDir", dir.absolutePath)
        var f = File(dir, name)
        val ext = suggestedName.substringAfterLast('.', "")
        if (ext.isNotEmpty() && f.extension.isEmpty()) f = File(dir, "$name.$ext")
        onResult(Uri.fromFile(f))
    }

    fun folder(onResult: (Uri?) -> Unit) = PcApp.runOnUi {
        // FileDialog não escolhe pasta; o diálogo do Swing com o visual do Windows escolhe.
        runCatching { javax.swing.UIManager.setLookAndFeel(javax.swing.UIManager.getSystemLookAndFeelClassName()) }
        val c = javax.swing.JFileChooser(prefs.get("lastDir", null))
        c.fileSelectionMode = javax.swing.JFileChooser.DIRECTORIES_ONLY
        val ok = c.showOpenDialog(PcApp.window) == javax.swing.JFileChooser.APPROVE_OPTION
        onResult(if (ok) c.selectedFile?.let { Uri.fromFile(it) } else null)
    }
}
