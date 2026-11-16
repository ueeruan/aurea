@file:Suppress("unused", "UNUSED_PARAMETER")

package androidx.activity.result.contract

import android.net.Uri
import androidx.activity.result.PickVisualMediaRequest
import com.aurea.pc.PcFileDialogs

/**
 * Os contratos de resultado do Android, resolvidos no PC pelo seletor de
 * arquivos do Windows. Galeria = arquivos de imagem/vídeo; documento =
 * arquivo; pasta = pasta; permissão = concedida (o Windows não pergunta).
 */
abstract class ActivityResultContract<I, O> {
    internal abstract fun launchPc(input: I, onResult: (O) -> Unit)
}

class ActivityResultContracts private constructor() {

    open class PickVisualMedia : ActivityResultContract<PickVisualMediaRequest, Uri?>() {
        sealed interface VisualMediaType
        object ImageOnly : VisualMediaType
        object VideoOnly : VisualMediaType
        object ImageAndVideo : VisualMediaType
        class SingleMimeType(val mimeType: String) : VisualMediaType

        override fun launchPc(input: PickVisualMediaRequest, onResult: (Uri?) -> Unit) =
            PcFileDialogs.open(PcFileDialogs.filterFor(input.mediaType), multiple = false) { onResult(it.firstOrNull()) }

        companion object {
            @JvmStatic fun isPhotoPickerAvailable(): Boolean = true
            @JvmStatic fun isPhotoPickerAvailable(context: android.content.Context): Boolean = true
        }
    }

    open class PickMultipleVisualMedia(private val maxItems: Int = 100) : ActivityResultContract<PickVisualMediaRequest, List<Uri>>() {
        override fun launchPc(input: PickVisualMediaRequest, onResult: (List<Uri>) -> Unit) =
            PcFileDialogs.open(PcFileDialogs.filterFor(input.mediaType), multiple = true) { onResult(it.take(maxItems)) }
    }

    open class OpenDocument : ActivityResultContract<Array<String>, Uri?>() {
        override fun launchPc(input: Array<String>, onResult: (Uri?) -> Unit) =
            PcFileDialogs.open(PcFileDialogs.filterForMimes(input), multiple = false) { onResult(it.firstOrNull()) }
    }

    open class OpenMultipleDocuments : ActivityResultContract<Array<String>, List<Uri>>() {
        override fun launchPc(input: Array<String>, onResult: (List<Uri>) -> Unit) =
            PcFileDialogs.open(PcFileDialogs.filterForMimes(input), multiple = true, onResult)
    }

    open class OpenDocumentTree : ActivityResultContract<Uri?, Uri?>() {
        override fun launchPc(input: Uri?, onResult: (Uri?) -> Unit) = PcFileDialogs.folder(onResult)
    }

    open class CreateDocument(private val mimeType: String) : ActivityResultContract<String, Uri?>() {
        override fun launchPc(input: String, onResult: (Uri?) -> Unit) = PcFileDialogs.save(input, mimeType, onResult)
    }

    open class GetContent : ActivityResultContract<String, Uri?>() {
        override fun launchPc(input: String, onResult: (Uri?) -> Unit) =
            PcFileDialogs.open(PcFileDialogs.filterForMimes(arrayOf(input)), multiple = false) { onResult(it.firstOrNull()) }
    }

    open class GetMultipleContents : ActivityResultContract<String, List<Uri>>() {
        override fun launchPc(input: String, onResult: (List<Uri>) -> Unit) =
            PcFileDialogs.open(PcFileDialogs.filterForMimes(arrayOf(input)), multiple = true, onResult)
    }

    /** Câmera do aparelho: no PC não há gravação pela câmera (volta "não gravou"). */
    open class CaptureVideo : ActivityResultContract<Uri, Boolean>() {
        override fun launchPc(input: Uri, onResult: (Boolean) -> Unit) = onResult(false)
    }

    open class TakePicture : ActivityResultContract<Uri, Boolean>() {
        override fun launchPc(input: Uri, onResult: (Boolean) -> Unit) = onResult(false)
    }

    open class RequestPermission : ActivityResultContract<String, Boolean>() {
        override fun launchPc(input: String, onResult: (Boolean) -> Unit) = onResult(true)
    }

    open class RequestMultiplePermissions : ActivityResultContract<Array<String>, Map<String, Boolean>>() {
        override fun launchPc(input: Array<String>, onResult: (Map<String, Boolean>) -> Unit) = onResult(input.associateWith { true })
    }

    open class StartActivityForResult : ActivityResultContract<android.content.Intent, androidx.activity.result.ActivityResult>() {
        override fun launchPc(input: android.content.Intent, onResult: (androidx.activity.result.ActivityResult) -> Unit) {
            com.aurea.pc.PcIntents.start(input)
            onResult(androidx.activity.result.ActivityResult(0, null))
        }
    }
}
