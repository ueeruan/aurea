package com.aurea.aurea.state

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.MediaScannerConnection
import android.net.Uri
import android.os.Build
import android.provider.MediaStore
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.io.OutputStream
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/**
 * Como o arquivo exportado entra na galeria ("o vídeo exportado não aparece na
 * galeria", beta 07/10). As regras puras ficam aqui (testáveis na JVM); o
 * Exporter faz o IO.
 *
 * - Android 10+: MediaStore com IS_PENDING=1 → cópia → IS_PENDING=0, MIME
 *   certo e DATE_TAKEN/DATE_ADDED/DATE_MODIFIED de AGORA — sem a data, app de
 *   galeria que ordena por "data da foto" podia pôr o vídeo fora da linha do
 *   tempo (ou no fim dela) até o scanner terminar.
 * - Android 8–9: a galeria só indexa a pasta PÚBLICA (Filmes/Aurea…); antes o
 *   export ficava em Android/data do app, que nenhuma galeria mostra. Com
 *   WRITE_EXTERNAL_STORAGE (pedida na tela Exportar, só nesses sistemas) vai
 *   para a pasta pública e passa pelo MediaScanner; sem ela, continua na
 *   pasta do app (e a mensagem diz o caminho).
 */
@android.annotation.SuppressLint("InlinedApi")   // colunas do Android 10 só são usadas nele
object GalleryPublish {
    /** Colunas do MediaStore para o item novo (pendente até a cópia terminar). */
    fun pendingColumns(name: String, mime: String, relativePath: String, nowMs: Long,
                       dateTaken: Boolean = true): List<Pair<String, Any>> {
        val seconds = nowMs / 1000
        return listOfNotNull(
            MediaStore.MediaColumns.DISPLAY_NAME to name,
            MediaStore.MediaColumns.MIME_TYPE to mime,
            MediaStore.MediaColumns.RELATIVE_PATH to relativePath,
            MediaStore.MediaColumns.IS_PENDING to 1,
            // DATE_TAKEN existe para vídeo e imagem (VideoColumns/ImageColumns: "datetaken", ms).
            // Downloads (.zip) não é mídia: sem "data da foto".
            if (dateTaken) MediaStore.Video.VideoColumns.DATE_TAKEN to nowMs else null,
            MediaStore.MediaColumns.DATE_ADDED to seconds,
            MediaStore.MediaColumns.DATE_MODIFIED to seconds,
        )
    }

    /** Colunas que publicam o item (fim da cópia): visível para todas as galerias. */
    fun publishColumns(nowMs: Long): List<Pair<String, Any>> = listOf(
        MediaStore.MediaColumns.IS_PENDING to 0,
        MediaStore.MediaColumns.DATE_MODIFIED to nowMs / 1000,
    )

    /** Pasta relativa ao armazenamento compartilhado (RELATIVE_PATH / pasta pública). */
    fun relativeFolder(format: ExportFormat): String = ImageExportRules.galleryFolder(format)

    /** Android 8–9 sem a permissão de escrita: a tela pede antes de exportar. */
    fun needsLegacyWritePermission(context: Context): Boolean =
        legacyStorage(Build.VERSION.SDK_INT) &&
            context.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED

    /** Até o Android 9 (API 28) não há MediaStore com RELATIVE_PATH/IS_PENDING. */
    fun legacyStorage(sdk: Int): Boolean = sdk < Build.VERSION_CODES.Q

    fun canWritePublic(context: Context): Boolean =
        legacyStorage(Build.VERSION.SDK_INT) &&
            context.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED

    // --- 0.5.3 (P0-12): a cópia para a galeria cabe? ---

    /** Folga além do próprio arquivo (o MediaStore e o sistema de arquivos também escrevem). */
    const val COPY_MARGIN_BYTES = 32L shl 20

    /**
     * A cópia de [size] bytes cabe nos [room] bytes livres do armazenamento
     * compartilhado? Sem medida (`room` < 0) tenta, como antes. Não cabe: nem a
     * galeria nem a pasta externa do app são tentadas (a cópia morreria no meio,
     * depois de encher o aparelho) e o vídeo é resgatado sem cópia ([rescue]).
     */
    fun copyFits(size: Long, room: Long): Boolean = room < 0L || size <= 0L || room >= size + COPY_MARGIN_BYTES

    /** Bytes livres onde a galeria e a pasta externa do app escrevem (-1 = sem medida). */
    fun sharedRoom(context: Context): Long = runCatching {
        val dir = context.getExternalFilesDir(null) ?: return@runCatching -1L
        android.os.StatFs(dir.absolutePath).availableBytes
    }.getOrDefault(-1L)

    // --- 0.5.3 (P0-2): o temporário só sai quando existe uma cópia confirmada ---

    /** O destino do arquivo temporário do export depois da tentativa de publicar. */
    enum class TempFate { Delete, Rescue }

    /**
     * Galeria ou a cópia de reserva (pasta do app) deram certo: o temporário pode
     * sair. As DUAS falharam (ex.: disco cheio no armazenamento compartilhado):
     * ele é o único vídeo que existe — antes era apagado mesmo assim.
     */
    fun tempFate(galleryOk: Boolean, fallbackOk: Boolean): TempFate =
        if (galleryOk || fallbackOk) TempFate.Delete else TempFate.Rescue

    /**
     * Tira o temporário de `cacheDir/export` (apagada na abertura do app) para
     * [safeDir], pasta privada no MESMO sistema de arquivos: `renameTo` não copia
     * nem precisa de espaço. Devolve o arquivo no lugar novo; null = não deu, e o
     * temporário fica onde está (quem chama não o apaga).
     */
    fun rescue(temp: File, safeDir: File): File? = runCatching {
        if (!temp.isFile || temp.length() <= 0L) return@runCatching null
        if (!safeDir.isDirectory && !safeDir.mkdirs() && !safeDir.isDirectory) return@runCatching null
        val dst = File(safeDir, temp.name)
        if (temp.renameTo(dst)) return@runCatching dst
        // Sem rename (outro volume): cópia conferida pelo tamanho; só então o temporário sai.
        val size = temp.length()
        temp.copyTo(dst, overwrite = true)
        if (dst.length() == size) { temp.delete(); dst } else { dst.delete(); null }
    }.getOrNull()

    // --- 0.5.3 (P2): a cópia para a galeria ---

    /**
     * Copia [file] para [out]. Android 10+: `android.os.FileUtils.copy` — o
     * kernel copia de descritor para descritor, sem passar o vídeo inteiro pela
     * JVM em blocos de 1 MiB. Se ela falhar ou copiar menos que o arquivo, volta
     * ao começo do destino e copia do jeito de sempre; destino que não dá para
     * voltar ao começo repassa o erro (quem chama desfaz o item pendente).
     * Devolve os bytes copiados.
     */
    @android.annotation.SuppressLint("NewApi")   // só é chamada com sdk >= 29
    fun copyFile(file: File, out: OutputStream, sdk: Int = Build.VERSION.SDK_INT,
                 kernelCopy: (FileInputStream, OutputStream) -> Long = { input, output -> android.os.FileUtils.copy(input, output) }): Long {
        if (sdk >= Build.VERSION_CODES.Q) {
            val size = file.length()
            val fast = runCatching { FileInputStream(file).use { kernelCopy(it, out) } }
            if (fast.getOrNull() == size) return size
            val error = fast.exceptionOrNull() ?: java.io.IOException("short copy: ${fast.getOrNull()} of $size")
            val channel = (out as? FileOutputStream)?.channel ?: throw error
            runCatching { channel.truncate(0L); channel.position(0L) }.onFailure { throw error }
        }
        return file.inputStream().use { it.copyTo(out, 1 shl 20) }
    }

    /**
     * Android 8–9: pede ao MediaScanner para indexar o arquivo (é o que faz a
     * galeria vê-lo). Devolve a Uri do MediaStore, ou null se não indexou em
     * [timeoutMs] — o arquivo continua lá e a galeria o acha no próximo scan.
     */
    fun scan(context: Context, file: File, mime: String, timeoutMs: Long = 10_000): Uri? {
        val done = CountDownLatch(1)
        var result: Uri? = null
        MediaScannerConnection.scanFile(context, arrayOf(file.absolutePath), arrayOf(mime)) { _, uri ->
            result = uri
            done.countDown()
        }
        done.await(timeoutMs, TimeUnit.MILLISECONDS)
        return result
    }
}
