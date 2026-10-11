package com.aurea.aurea.state

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.MediaScannerConnection
import android.net.Uri
import android.os.Build
import android.provider.MediaStore
import java.io.File
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
