package com.aurea.pc

import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.platform.Font
import java.io.File

/**
 * Fonte da interface do Aurea PC: Segoe UI um degrau mais grossa que o pedido
 * (normal → seminegrito, seminegrito → negrito). No Windows a fonte padrão do
 * Skia sai fina demais na tela escura (pedido do dono, 12/11).
 */
object PcFonts {
    private val dir = File(System.getenv("WINDIR") ?: "C:\\Windows", "Fonts")

    fun uiFamily(): FontFamily? {
        val regular = file("segoeui.ttf") ?: return null
        val semibold = file("seguisb.ttf") ?: return null
        val bold = file("segoeuib.ttf") ?: return null
        val black = file("seguibl.ttf") ?: bold
        val italic = file("segoeuii.ttf") ?: regular
        val semiboldItalic = file("seguisbi.ttf") ?: semibold
        val boldItalic = file("segoeuiz.ttf") ?: bold
        val shift = listOf(
            FontWeight.Thin to regular, FontWeight.ExtraLight to regular, FontWeight.Light to regular,
            FontWeight.Normal to semibold, FontWeight.Medium to semibold,
            FontWeight.SemiBold to bold, FontWeight.Bold to bold,
            FontWeight.ExtraBold to black, FontWeight.Black to black,
        )
        val shiftItalic = listOf(
            FontWeight.Light to italic, FontWeight.Normal to semiboldItalic, FontWeight.Medium to semiboldItalic,
            FontWeight.SemiBold to boldItalic, FontWeight.Bold to boldItalic,
        )
        return FontFamily(
            shift.map { (w, f) -> Font(f, w, FontStyle.Normal) } + shiftItalic.map { (w, f) -> Font(f, w, FontStyle.Italic) },
        )
    }

    private fun file(name: String): File? = File(dir, name).takeIf { it.isFile }
}
