package com.aurea.aurea.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

/**
 * Identidade visual do Aurea.
 *
 * O fundo `#0F141A` é o MESMO que pinta o ícone adaptativo e a tela de
 * abertura (`res/values/colors.xml`). Se divergissem, abrir o app piscaria
 * entre o ícone e a interface.
 *
 * O editor é escuro por decisão, não por moda: quem edita vídeo olha para a
 * imagem, e uma interface clara em volta altera a percepção de cor e de
 * exposição do que está sendo editado. O escuro é o que o olho espera.
 */
object AureaColors {
    /** O fundo da marca. Bate com `@color/aurea_background`. */
    val Background = Color(0xFF0F141A)

    /** Superfícies elevadas: painéis, cartões, gavetas. */
    val Surface = Color(0xFF161C24)
    val SurfaceHigh = Color(0xFF1D242E)
    val SurfaceHighest = Color(0xFF252D39)

    /** A borda que separa painéis sem usar sombra — sombra em fundo escuro
     *  vira mancha cinzenta; uma linha de 1 px define melhor. */
    val Outline = Color(0xFF2C3542)

    /** Texto. `OnSurfaceMuted` é para rótulos secundários. */
    val OnSurface = Color(0xFFE6EAF0)
    val OnSurfaceMuted = Color(0xFF9AA5B4)
    val OnSurfaceFaint = Color(0xFF6B7686)

    /** Cor de destaque: o ciano da marca. */
    val Accent = Color(0xFF35C8E8)
    val AccentDim = Color(0xFF1E7A8C)

    /** Playhead e seleção na timeline. */
    val Playhead = Color(0xFFFF5A5F)
    val Selection = Color(0x2235C8E8)
    val SelectionBorder = Color(0xFF35C8E8)

    /** Cores das barras de camada na timeline, por tipo. */
    val LayerVideo = Color(0xFF2E5C8A)
    val LayerImage = Color(0xFF2E7D6B)
    val LayerAudio = Color(0xFF6B5B95)
    val LayerText = Color(0xFF8A6B2E)
    val LayerShape = Color(0xFF8A3E5C)
    val LayerNull = Color(0xFF4A5260)
    val LayerCamera = Color(0xFF3E6B8A)
    val LayerLight = Color(0xFF8A7A2E)
    val Layer3D = Color(0xFF4A6B8A)
    val LayerParticles = Color(0xFF6B8A3E)
    val LayerComp = Color(0xFF5A4A8A)

    /** Faixa de keyframes na barra da camada. */
    val Keyframe = Color(0xFFF0C040)
    val KeyframeSelected = Color(0xFFFFFFFF)

    val Danger = Color(0xFFE5484D)
    val Warning = Color(0xFFE8A33D)
    val Success = Color(0xFF46A758)

    /** Fundo do preview quando não há composição (transparência). */
    val CheckerLight = Color(0xFF2A3340)
    val CheckerDark = Color(0xFF212934)
}

private val AureaDarkScheme = darkColorScheme(
    primary = AureaColors.Accent,
    onPrimary = Color(0xFF00212B),
    primaryContainer = AureaColors.AccentDim,
    onPrimaryContainer = AureaColors.OnSurface,
    secondary = AureaColors.OnSurfaceMuted,
    onSecondary = AureaColors.Background,
    background = AureaColors.Background,
    onBackground = AureaColors.OnSurface,
    surface = AureaColors.Surface,
    onSurface = AureaColors.OnSurface,
    surfaceVariant = AureaColors.SurfaceHigh,
    onSurfaceVariant = AureaColors.OnSurfaceMuted,
    outline = AureaColors.Outline,
    error = AureaColors.Danger,
    onError = Color.White,
)

/**
 * Tipografia do editor.
 *
 * Menor que o padrão do Material de propósito: numa timeline cabem mais nomes
 * de camada em 12sp do que em 14sp, e o usuário lê o NOME, não o estilo. Os
 * números usam espaçamento fixo para a régua e o contador de frames não
 * "pularem" ao trocar de dígito.
 */
private val AureaTypography = Typography(
    titleLarge = TextStyle(fontSize = 20.sp, fontWeight = FontWeight.SemiBold, lineHeight = 26.sp),
    titleMedium = TextStyle(fontSize = 16.sp, fontWeight = FontWeight.SemiBold, lineHeight = 22.sp),
    titleSmall = TextStyle(fontSize = 14.sp, fontWeight = FontWeight.Medium, lineHeight = 20.sp),
    bodyLarge = TextStyle(fontSize = 14.sp, lineHeight = 20.sp),
    bodyMedium = TextStyle(fontSize = 13.sp, lineHeight = 18.sp),
    bodySmall = TextStyle(fontSize = 12.sp, lineHeight = 16.sp),
    labelLarge = TextStyle(fontSize = 13.sp, fontWeight = FontWeight.Medium, lineHeight = 16.sp),
    labelMedium = TextStyle(fontSize = 11.sp, fontWeight = FontWeight.Medium, lineHeight = 14.sp),
    labelSmall = TextStyle(fontSize = 10.sp, fontWeight = FontWeight.Medium, lineHeight = 12.sp),
)

@Composable
fun AureaTheme(content: @Composable () -> Unit) {
    // Sem opção clara. Ver o comentário de AureaColors: o editor é escuro por
    // decisão de produto, e `isSystemInDarkTheme` é ignorado de propósito para
    // não existir um caminho de interface que ninguém testa.
    @Suppress("UNUSED_EXPRESSION")
    isSystemInDarkTheme()

    MaterialTheme(
        colorScheme = AureaDarkScheme,
        typography = AureaTypography,
        content = content,
    )
}

/** Cor da barra de camada por tipo. */
fun layerColor(kind: Int): Color = when (kind) {
    1 -> AureaColors.LayerVideo
    2 -> AureaColors.LayerImage
    3 -> AureaColors.LayerAudio
    4 -> AureaColors.LayerText
    5 -> AureaColors.LayerShape
    6 -> AureaColors.LayerNull
    8 -> AureaColors.LayerCamera
    9 -> AureaColors.LayerLight
    10 -> AureaColors.Layer3D
    11 -> AureaColors.LayerParticles
    12 -> AureaColors.LayerComp
    else -> AureaColors.LayerNull
}

/** Nome do tipo, para a UI. Os valores vêm de `aurea::LayerKind`. */
fun layerKindName(kind: Int): String = when (kind) {
    1 -> "Vídeo"
    2 -> "Imagem"
    3 -> "Áudio"
    4 -> "Texto"
    5 -> "Forma"
    6 -> "Nulo"
    7 -> "Ajuste"
    8 -> "Câmera"
    9 -> "Luz"
    10 -> "Modelo 3D"
    11 -> "Partículas"
    12 -> "Composição"
    else -> "Camada"
}
