package com.aurea.aurea.ui.platform

/**
 * Recursos que só o Aurea PC mostra. O desktop/ compila este código do Android
 * sem mudanças; o celular deixa tudo desligado e a tela fica igual. Pedido do
 * dono (12/11/2026): o trabalho do After Effects é para o PC, sem mudar a UI do
 * Android nem do iOS (exceto o animador de texto novo).
 */
object PlatformFeatures {
    /** Fonte da interface: nula = a do sistema (celular). O PC troca antes da primeira tela. */
    @JvmField @Volatile var uiFontFamily: androidx.compose.ui.text.font.FontFamily? = null

    /** Controles novos do AE (lotes de 12/11): ligado só pelo Main.kt do PC. */
    @JvmField @Volatile var desktopAe: Boolean = false

    /** Cena 3D no jeito do Blender (12/11): ligado só pelo Main.kt do PC. O celular fica igual. */
    @JvmField @Volatile var desktopScene3d: Boolean = false
}
