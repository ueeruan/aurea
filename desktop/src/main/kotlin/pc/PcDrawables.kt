package com.aurea.pc

import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.painter.BitmapPainter
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.graphics.toComposeImageBitmap
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.loadXmlImageVector
import androidx.compose.ui.unit.Density
import com.aurea.aurea.RNames
import org.jetbrains.skia.Image
import org.xml.sax.InputSource
import java.util.concurrent.ConcurrentHashMap

/** `res/drawable*` e `res/mipmap*` do Android: vetores XML e imagens. */
object PcDrawables {
    private val vectors = ConcurrentHashMap<Int, ImageVector>()
    private val bitmaps = ConcurrentHashMap<Int, ImageBitmap>()

    private fun name(id: Int) = RNames.byId[id] ?: error("drawable $id")

    fun isVector(id: Int): Boolean {
        val (kind, n) = name(id)
        return openFirst(kind, n)?.second?.endsWith(".xml") == true
    }

    /** O arquivo de maior densidade (xxxhdpi primeiro), como um tablet escolheria. */
    private fun openFirst(kind: String, n: String): Pair<java.io.InputStream, String>? {
        val dirs = listOf("$kind-xxxhdpi", "$kind-xxhdpi", "$kind-xhdpi", "$kind-hdpi", "$kind-mdpi", kind, "$kind-anydpi-v26")
        for (d in dirs) for (ext in listOf(".png", ".webp", ".jpg", ".xml")) {
            val path = "/aurea-res/$d/$n$ext"
            PcDrawables::class.java.getResourceAsStream(path)?.let { return it to path }
        }
        return null
    }

    fun vector(id: Int): ImageVector = vectors.getOrPut(id) {
        val (kind, n) = name(id)
        val (input, _) = openFirst(kind, n) ?: error("$kind/$n")
        input.use { loadXmlImageVector(InputSource(it), Density(1f)) }
    }

    fun bitmap(id: Int): ImageBitmap = bitmaps.getOrPut(id) {
        val (kind, n) = name(id)
        val (input, _) = openFirst(kind, n) ?: error("$kind/$n")
        Image.makeFromEncoded(input.use { it.readBytes() }).toComposeImageBitmap()
    }

    fun painter(id: Int): Painter = BitmapPainter(bitmap(id))
}
