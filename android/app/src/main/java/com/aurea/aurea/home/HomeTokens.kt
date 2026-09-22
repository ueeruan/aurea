package com.aurea.aurea.home

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType

/**
 * Tokens que só a Home usa. São as "cores escritas à mão fora dos tokens" da
 * A.01 (spec §1.1.2) e as medidas próprias da Home 762dbfe (§5). Ficam aqui,
 * com nome, para nenhuma tela da Home escrever número solto — e para não
 * inchar os tokens globais com o que só existe nesta área.
 */
internal object HomeColors {
    /** Status bar dos prints: o fundo #0F141A sob o véu preto 25 % do sistema. */
    val StatusBarVeil = Color(0xFF0B0F13)
    /** Barra de gestos preta opaca dos prints (o app antigo não era edge-to-edge embaixo). */
    val NavigationBar = Color(0xFF000000)
    val White = Color(0xFFFFFFFF)
    val White70 = Color(0xB3FFFFFF)
    /** Fim do scrim do hero (preto 70 %). */
    val HeroScrim = Color(0xB3000000)
    /** Faixa "Versao beta para testes" do Sobre. */
    val Beta = Color(0xFFFFB020)
    val BetaFill = Color(0x22FFB020)
    val BetaBorder = Color(0x55FFB020)
    /** Separador entre opções do segmentado Cupertino (SDK). */
    val SegmentSeparator = Color(0x4D8E8E93)
    /** Sombra do polegar do segmentado Cupertino (SDK). */
    val SegmentThumbShadow = Color(0x1F000000)
    /** Trilho desligado do CupertinoSwitch escuro (secondarySystemFill). */
    val SwitchOffTrack = Color(0x52787880)
    /** Campo de busca: CupertinoTextField padrão no escuro. */
    val SearchField = Color(0xFF000000)
    val SearchFieldBorder = Color(0x33FFFFFF)
    val SearchPlaceholder = Color(0x4DEBEBF5)
    /** Campo do diálogo de nome (o mesmo de `AureaNamePrompt`). */
    val DialogField = Color(0xFF1C1C1E)
    /** Realce do FilledButton do tema (branco 5 %): o botão não encolhe, só acende. */
    val ButtonHighlight = Color(0x0DFFFFFF)
}

/**
 * Estilos de texto da Home 762dbfe. Todos fundem por cima de [AureaType.Base]
 * (15 sp, −0,1, altura 1,35): o Flutter herdava essa base mesmo quando o
 * widget só definia tamanho e peso — sem ela as alturas não batem com o print.
 * Estáticos: nenhum TextStyle é alocado por composição.
 */
internal object HomeType {
    private fun s(size: Float, weight: FontWeight = FontWeight.Normal, spacing: Float = -0.1f, color: Color = AureaColors.Text, lineHeight: Float? = null) =
        AureaType.Base.merge(
            TextStyle(
                fontSize = size.sp,
                fontWeight = weight,
                letterSpacing = spacing.sp,
                color = color,
                lineHeight = lineHeight?.em ?: 1.35.em,
            ),
        )

    // Cabeçalho e barras
    val HeaderTitle = s(30f, FontWeight.W800, -0.8f, lineHeight = 1.05f)
    val Greeting = s(13.5f, color = AureaColors.Muted)
    val CompactTitle = s(17f, FontWeight.W700, -0.4f)
    val TabLabel = s(10.5f, FontWeight.W500, 0.1f)
    val AvatarInitial = s(14f, FontWeight.W700, color = AureaColors.OnAccent)

    // Início
    val NewButton = s(17f, FontWeight.W600, -0.2f, AureaColors.OnAccent)
    val ShortcutLabel = s(12f, FontWeight.W600)
    val HeroKicker = s(11f, FontWeight.W700, 0.4f, AureaColors.Accent)
    val HeroTitle = s(18f, FontWeight.W700, -0.2f, HomeColors.White)
    val HeroSpec = s(11f, color = HomeColors.White70)
    val HeroPill = s(12.5f, FontWeight.W700, color = AureaColors.OnAccent)
    val ListCount = s(15f, FontWeight.W700)
    val CardTitle = s(14f, FontWeight.W600, -0.1f)
    val CardSpec = s(11f, color = AureaColors.Muted)
    val LinkRow = s(14.5f)
    val SectionTitle = s(21f, FontWeight.W700, -0.4f)
    val ModelDetail = s(11.5f, color = AureaColors.Muted)
    val FeatureTitle = s(15f, FontWeight.W600)
    val FeatureSubtitle = s(12.5f)
    val Empty = s(13.5f, color = AureaColors.Muted)
    val BatchCount = s(13f, color = AureaColors.Muted)
    val BatchAction = s(13f)
    val BatchDanger = s(13f, color = AureaColors.Danger)
    val SearchText = s(17f)
    val SearchPlaceholder = s(17f, color = HomeColors.SearchPlaceholder)

    // Folha "Novo projeto"
    val SheetSpec = s(12.5f, color = AureaColors.Muted)
    val FrameLabel = s(22f, FontWeight.W700, -0.3f)
    val FrameHint = s(12f, color = AureaColors.Muted)
    val FormatLabel = s(12.5f, FontWeight.W600, -0.1f)
    val FormatHint = s(10f, color = AureaColors.Muted)
    val SectionLabel = s(12f, FontWeight.W500, 0.6f, AureaColors.Muted)
    val NameField = s(17f, spacing = -0.2f)
    val NamePlaceholder = s(17f, spacing = -0.2f, color = AureaColors.Muted)
    val DimLabel = s(11f, color = AureaColors.Muted)
    val DimField = s(16f)
    val Times = s(16f)
    val Segment = s(13f, FontWeight.W600, -0.1f)
    val CreateButton = s(17f, FontWeight.W600, -0.2f, AureaColors.OnAccent)
    val DialogField = s(15f)

    // Ajustes / Sobre / Comunidade / Perfil
    val Note = s(12.5f, color = AureaColors.Muted)
    val TileSubtitle = s(12f, color = AureaColors.Muted)
    /** Subtítulo padrão do ListTile M3 (bodyMedium em onSurfaceVariant). */
    val TileSubtitleMedium = s(15f, color = AureaColors.Muted)
    val AboutName = AureaType.HeadlineLarge.merge(TextStyle(fontSize = 28.sp))
    val VersionPill = s(12f, FontWeight.W600, color = AureaColors.Accent)
    val BetaTitle = s(13f, FontWeight.W700, color = HomeColors.Beta)
    val BetaBody = s(11f, color = AureaColors.Muted)
    val Footer = AureaType.BodySmall.merge(TextStyle(fontSize = 11.sp))
    val CommunityTitle = s(28f, FontWeight.W800, -0.7f)
    val CreatorLabel = s(11.5f, color = AureaColors.Muted)
    val CommunityAvatar = s(13.6f, FontWeight.W700, color = AureaColors.OnAccent)
    val CreatorAvatar = s(22f, FontWeight.W700, color = AureaColors.OnAccent)
    val EmptyTitle = s(15f, FontWeight.W700)
    val EmptyBody = s(13f, color = AureaColors.Muted, lineHeight = 1.4f)
    val BannerTitle = s(14f, FontWeight.W700)
    val BannerBody = s(12f, color = AureaColors.Muted)
    val ToggleLabel = s(14f, FontWeight.W600)
    val ProfileInitial = s(28f, FontWeight.W700, color = AureaColors.Accent)
    val FieldPlaceholder = s(15f, color = AureaColors.Muted)
    val FieldNote = s(11.5f, color = AureaColors.Muted)
    val ProfileButton = s(15f, FontWeight.W700, color = AureaColors.OnAccent)
}

internal object HomeDims {
    val Gutter = 20.dp
    val TabBarHeight = 54.dp
    val CompactBarHeight = 52.dp
    /** Scroll a partir do qual a barra compacta aparece. */
    val CompactBarThreshold = 64.dp
    /** Folga no fim das listas para passar da barra de abas translúcida. */
    val ListEndSpace = 120.dp
    val RoundTarget = 44.dp
    val RoundCircle = 36.dp
    val ShortcutCircle = 56.dp
    val HeroRadius = 20.dp
    val CardRadius = 14.dp
    val ModelRadius = 16.dp
    val NewButtonHeight = 54.dp
    val NewButtonRadius = 16.dp
    val GroupRadius = 16.dp
    val SheetTopRadius = 24.dp

    /** Sigma do blur (Flutter) de cada vidro da Home. */
    val TabBarBlur = 24.dp
    val CompactBarBlur = 18.dp
    val BatchBarBlur = 20.dp

    /** Quantos projetos a Inicio mostra antes do "Mostrar todos". */
    const val RECENT_ON_HOME = 6
    /** Grade: largura/altura do cartão (miniatura + duas linhas). */
    const val CARD_ASPECT = 1.12f
}
