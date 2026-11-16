package com.aurea.pc

import android.content.Intent
import java.io.File
import java.io.PrintWriter
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter

/**
 * Onde o Aurea PC guarda as coisas (o equivalente das pastas do app no Android):
 *  - %APPDATA%\Aurea PC\        files/ (projetos, plugins), shared_prefs/, no_backup/
 *  - %LOCALAPPDATA%\Aurea PC\   cache/ (miniaturas, proxies, frames), logs/
 */
object PcPaths {
    private fun env(name: String, fallback: String) = System.getenv(name)?.takeIf { it.isNotBlank() } ?: fallback
    private val home = System.getProperty("user.home")
    private val portable = System.getProperty("aurea.dataDir")

    val data: File = File(portable ?: env("APPDATA", "$home\\AppData\\Roaming"), if (portable != null) "" else "Aurea PC").apply { mkdirs() }
    val local: File = File(portable ?: env("LOCALAPPDATA", "$home\\AppData\\Local"), if (portable != null) "local" else "Aurea PC").apply { mkdirs() }
    val files: File = File(data, "files").apply { mkdirs() }
    val cache: File = File(local, "cache").apply { mkdirs() }
    val noBackup: File = File(data, "no_backup").apply { mkdirs() }
    val external: File = File(data, "external").apply { mkdirs() }
    val logs: File = File(local, "logs").apply { mkdirs() }

    private fun userFolder(env: String, name: String): File =
        File(System.getenv(env) ?: File(home, name).path).let { if (it.isDirectory) it else File(home, name) }
    val videos: File get() = File(userFolder("AUREA_VIDEOS", "Videos"), "Aurea").apply { mkdirs() }
    val pictures: File get() = File(userFolder("AUREA_PICTURES", "Pictures"), "Aurea").apply { mkdirs() }
    val music: File get() = File(userFolder("AUREA_MUSIC", "Music"), "Aurea").apply { mkdirs() }

    /** A pasta de recursos do app instalado (assets/, engine-assets/, DLLs). */
    val appDir: File by lazy {
        System.getProperty("compose.application.resources.dir")?.let { File(it) }
            ?: File(System.getProperty("user.dir"), "build/appres/common")
    }
    val nativeDir: File get() = appDir

    private fun assetRoots(): List<File> = listOf(File(appDir, "assets"), File(appDir.parentFile ?: appDir, "common/assets"))
    fun asset(name: String): File? = assetRoots().map { File(it, name) }.firstOrNull { it.isFile }
    fun assetList(path: String): Array<String>? =
        assetRoots().map { File(it, path) }.firstOrNull { it.isDirectory }?.list()?.sortedArray() ?: (if (path.isEmpty()) emptyArray() else null)
    val engineAssets: File get() = listOf(File(appDir, "engine-assets"), File(appDir.parentFile ?: appDir, "common/engine-assets")).firstOrNull { it.isDirectory } ?: File(appDir, "engine-assets")
}

/** Log do app em %LOCALAPPDATA%\Aurea PC\logs\aurea.log (e no console). */
object PcLog {
    private val file = File(PcPaths.logs, "aurea.log")
    private val out: PrintWriter by lazy {
        if (file.length() > 8L shl 20) file.renameTo(File(PcPaths.logs, "aurea.old.log"))
        PrintWriter(java.io.FileWriter(file, true), true)
    }
    private val fmt = DateTimeFormatter.ofPattern("MM-dd HH:mm:ss.SSS")

    @Synchronized
    fun write(level: Char, tag: String, msg: String, tr: Throwable?) {
        if (level == 'V' || level == 'D') return
        val line = "${LocalDateTime.now().format(fmt)} $level/$tag: $msg"
        out.println(line)
        tr?.printStackTrace(out)
        if (level >= 'E' || level == 'W' || System.getProperty("aurea.verbose") != null) {
            System.err.println(line)
            tr?.printStackTrace()
        }
    }
}

/** Intents do Android → ações do Windows: abrir link, abrir/mostrar arquivo, "compartilhar". */
object PcIntents {
    fun start(intent: Intent) {
        Intent.chooserTarget(intent)?.let { return start(it) }
        val uri = intent.data
        when (intent.action) {
            Intent.ACTION_VIEW -> when (uri?.scheme) {
                "http", "https", "mailto" -> browse(uri.toString())
                "file" -> openFile(File(uri.path ?: return))
                "package", null -> {}
                else -> browse(uri.toString())
            }
            Intent.ACTION_SEND, Intent.ACTION_SEND_MULTIPLE -> {
                // "Compartilhar" no PC = mostrar o arquivo no Explorer.
                val stream = intent.getParcelableExtra<android.net.Uri>(Intent.EXTRA_STREAM)
                val text = intent.getStringExtra(Intent.EXTRA_TEXT)
                when {
                    stream?.scheme == "file" -> reveal(File(stream.path ?: return))
                    text != null -> { android.content.ClipboardManager.app.setPrimaryClip(android.content.ClipData.newPlainText("", text)); PcToasts.show(text.take(80), false) }
                }
            }
            else -> {}
        }
    }

    fun browse(url: String) {
        runCatching { java.awt.Desktop.getDesktop().browse(java.net.URI(url)) }
            .onFailure { ProcessBuilder("rundll32", "url.dll,FileProtocolHandler", url).start() }
    }

    fun openFile(f: File) {
        runCatching { java.awt.Desktop.getDesktop().open(f) }.onFailure { reveal(f) }
    }

    fun reveal(f: File) {
        runCatching { ProcessBuilder("explorer.exe", "/select,", f.absolutePath).start() }
    }
}

/** Um id estável deste PC (o ANDROID_ID do Aurea PC). */
object PcIdentity {
    val deviceId: String by lazy {
        val f = File(PcPaths.noBackup, "device-id")
        f.takeIf { it.isFile }?.readText()?.trim()?.takeIf { it.length >= 16 }
            ?: java.util.UUID.randomUUID().toString().replace("-", "").take(16).also { f.writeText(it) }
    }
}
