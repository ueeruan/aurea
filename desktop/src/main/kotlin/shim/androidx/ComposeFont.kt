@file:Suppress("unused", "UNUSED_PARAMETER")

package androidx.compose.ui.text.font

/** `Font(R.font.x)`: a fonte de `res/font`, do classpath. */
fun Font(resId: Int, weight: FontWeight = FontWeight.Normal, style: FontStyle = FontStyle.Normal): Font {
    val (kind, name) = com.aurea.aurea.RNames.byId[resId] ?: error("fonte $resId")
    val bytes = android.content.res.Catalog.openResource(kind, name)?.use { it.readBytes() } ?: error("fonte $kind/$name")
    return androidx.compose.ui.text.platform.Font("res:$kind/$name", bytes, weight, style)
}

/** `FontFamily(Typeface)` do Android: a fonte do arquivo de onde o Typeface veio. */
fun FontFamily(typeface: android.graphics.Typeface): FontFamily {
    val path = typeface.sourcePath ?: return FontFamily.Default
    return FontFamily(androidx.compose.ui.text.platform.Font(java.io.File(path)))
}
