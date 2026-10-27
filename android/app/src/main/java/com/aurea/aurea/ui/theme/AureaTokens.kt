package com.aurea.aurea.ui.theme

import kotlin.reflect.KProperty
import kotlin.properties.ReadOnlyProperty
import java.util.Locale
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.text.style.TextDirection
import androidx.compose.animation.core.CubicBezierEasing
import androidx.compose.animation.core.Easing
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.PlatformTextStyle
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.LineHeightStyle
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp

// =============================================================================
//  O DESIGN SYSTEM DO AUREA (Fase 7.3 §8).
//
//  Um arquivo, uma fonte. Cor, tipografia, espaço, raio, ícone, sombra, vidro,
//  movimento e estados vivem AQUI — nenhuma tela escreve número ou cor solta.
//
//  A base numérica continua sendo a UI aprovada (Beta A.01 do Aurea antigo,
//  `aurea_colors.dart` / `app_theme.dart`, conferida pixel a pixel nos prints em
//  `docs/migration/ui_reference/`). O aparelho de referência tem densidade 2,625:
//  1 dp = 2,625 px. O que a A.01 tinha espalhado fora dos tokens entrou aqui com
//  nome semântico.
//
//  REGRA: se falta um token, ele entra aqui. Nunca no componente.
// =============================================================================

object AureaColors {
    /**
     * Tema escolhido nos Ajustes. É estado do Compose: trocar redesenha o app
     * inteiro na hora, sem reiniciar. Só os tons de fundo, superfície e a cor
     * de destaque mudam; estados (erro, aviso, sucesso) e o texto ficam.
     */
    // 0.3.3 tema: a paleta AUREA (azul) é o padrão; a Omega saiu.
    var palette by mutableStateOf(AureaPalette.Aurea)
    // --- Marca ---------------------------------------------------------------
    val BrandDeep: Color get() = palette.brandDeep
    val Brand: Color get() = palette.brand          // preenchimento de ação (Exportar, "+")
    val Accent: Color get() = palette.accent         // ação/estado fora do editor, destaque
    val Keyframe: Color get() = palette.keyframe       // keyframe, curva, valor, seleção em texto
    val Background: Color get() = palette.background
    val Surface: Color get() = palette.surface
    val SurfaceHigh: Color get() = palette.surfaceHigh
    val Chip: Color get() = palette.chip
    val ChipHigh: Color get() = palette.chipHigh       // campoAlto = lerp(chip, texto, 0,08)
    val Border: Color get() = palette.border
    val Text = Color(0xFFF7F9FB)
    val Muted: Color get() = palette.muted
    val Subtle: Color get() = palette.subtle         // texto terciário: nota, rodapé, ficha
    val OnAccent: Color get() = palette.onAccent
    val AccentDim: Color get() = palette.accentDim
    val KeyframeDim: Color get() = palette.keyframeDim
    val Danger = Color(0xFFFF6B6B)
    val Warning = Color(0xFFFFC978)
    val Success = Color(0xFF4CD08A)
    val Stage: Color get() = palette.stage          // fundo atrás da composição / timeline
    val Playhead = Color(0xFFFFFFFF)
    val Hairline: Color get() = palette.hairline       // #273442 @ 0,72

    // --- Cromo do editor (AmColors) -----------------------------------------
    val EditorTopBar: Color get() = palette.editorTopBar
    val EditorPanel: Color get() = palette.editorPanel
    val EditorPanelHigh: Color get() = palette.editorPanelHigh
    val Pill: Color get() = palette.pill
    val Action: Color get() = Brand
    /** Conteúdo sobre [Action] (a cor de marca): branco em todos os temas. */
    val OnAction: Color get() = palette.onBrand
    val ActionDim: Color get() = palette.actionDim
    val Selection: Color get() = BrandDeep
    val SelectionText: Color get() = Keyframe
    val Disabled = Color(0x40FFFFFF)       // branco 25 %
    val StatusBarVeil: Color get() = palette.statusBarVeil

    // --- Barra de sistema (a Home é edge-to-edge) ----------------------------
    /** Faixa sob a status bar: o fundo #0F141A sob o véu preto 25 % do sistema. */
    val SystemBarVeil: Color get() = palette.systemBarVeil
    /** Barra de gestos preta opaca (o app antigo não era edge-to-edge embaixo). */
    val NavigationBar = Color(0xFF000000)
    val Scrim = Color(0x8A000000)          // véu das folhas modais
    val SheetScrim = Color(0x590A0E13)     // véu das folhas de ajuste: palco a 35 %

    // --- Sobre imagem (cards com miniatura) ----------------------------------
    val OnImage = Color(0xFFFFFFFF)
    val OnImage70 = Color(0xB3FFFFFF)
    /** Fim do scrim do hero (preto 70 %). */
    val ImageScrim = Color(0xB3000000)

    // --- Aviso "beta" (Sobre) -------------------------------------------------
    val Beta = Color(0xFFFFB020)
    val BetaFill = Color(0x22FFB020)
    val BetaBorder = Color(0x55FFB020)

    // --- Realce de pressionar -------------------------------------------------
    /** Realce do botão cheio: não encolhe, só acende branco 5 %. */
    val PressHighlight = Color(0x0DFFFFFF)
    /** Realce neutro de uma linha de lista. */
    val RowHighlight = Color(0x0FFFFFFF)
    val OverlayPressed = Color(0x1AFFFFFF)

    // --- Controles nativos (Cupertino) ---------------------------------------
    val SegmentSeparator = Color(0x4D8E8E93)
    val SegmentThumbShadow = Color(0x1F000000)
    val SegmentTrack: Color get() = Background
    val SegmentThumb: Color get() = SurfaceHigh
    /** Trilho desligado do interruptor (secondarySystemFill escuro). */
    val SwitchOffTrack = Color(0x52787880)

    // --- Campos ---------------------------------------------------------------
    /** Campo de busca na barra da lista: CupertinoTextField padrão no escuro. */
    val Field = Color(0xFF000000)
    val FieldBorder = Color(0x33FFFFFF)
    val FieldPlaceholder = Color(0x4DEBEBF5)
    /** Campo preenchido (busca do navegador de efeitos, formulários). */
    val FieldFilled: Color get() = palette.fieldFilled

    // --- Editor redesenhado (mockup 2026-09-29) ----------------------------------
    /** Fundo do editor: topo, transporte e timeline (#161C2A no Aurea). */
    val EditorCanvas: Color get() = palette.editorCanvas
    /** Barras fixas (a de adicionar) e a faixa da navegação embaixo dela. */
    val EditorBar: Color get() = palette.editorBar
    /** Fio de 1 dp no alto das barras fixas. */
    val EditorBarLine: Color get() = palette.editorBarLine
    /** Pílula colada à esquerda de cada fileira da timeline (olho + miniatura). */
    val EditorRowPill: Color get() = palette.editorRowPill
    /** Quadradinho de 22 dp da miniatura/tipo dentro da pílula. */
    val EditorGlyphBox: Color get() = palette.editorGlyphBox
    /** Ícone do transporte sem ação (desfazer/refazer apagados). */
    val EditorIconDisabled = Color(0xFF4C5566)
    /** Fundo da caixa da prévia em volta do quadro. */
    val PreviewBackdrop = Color(0xFF0B0F16)
    /** Campo do diálogo de nome (o mesmo de `AureaNamePrompt`). */
    val FieldDialog = Color(0xFF1C1C1E)

    // --- Régua (AmTickRuler) --------------------------------------------------
    val TickWeak: Color get() = palette.tickWeak
    val TickStrong: Color get() = palette.tickStrong

    // --- Diálogos Cupertino ----------------------------------------------------
    val DestructiveCupertino = Color(0xFFFF453A)

    val BrandGradient: List<Color> get() = listOf(BrandDeep, Brand, Accent)

    // --- Painéis da A.01 (valores escritos à mão no Flutter, aqui viram token) ---
    val RailModeFill: Color get() = palette.railModeFill       // modo aceso do trilho, cartão de preset da curva
    val RailDisabled = Color(0xFF434956)       // losango/curva do trilho sem alvo
    val ControlButton = Color(0xFF434A60)      // corrente Largura/Altura, botão "Centro"
    val DialTrack = Color(0xFF2E3548)          // anel do dial de rotação
    val DialValueBox = Color(0xFF242436)       // caixa do ângulo no centro do dial
    val CurveGrid = Color(0xFF34405A)          // grade pontilhada do editor de curva
    val CurvePresetBorder = Color(0xFF333B4F)  // borda do preset de curva apagado
    val EffectPreviewTop = Color(0xFF232B3A)   // cartela genérica do catálogo (degradê)
    val EffectPreviewBottom = Color(0xFF606F98)
    val EffectPreviewDisc = Color(0xFFFF4D2D)
    val BlendThumbBottom = Color(0xFFFF8A3D)   // miniatura da mescla: disco de baixo
    val BlendThumbTop = Color(0xFF3D9BFF)      // miniatura da mescla: disco de cima

    // --- Omega fundação: papéis das telas fora do editor (ver AureaLightRoles) ----
    // 0.3.3 tema: em todos os temas resolvem para os tons escuros da paleta
    // (fundo, superfície, texto claro, destaque). Home, gaveta, Novo projeto,
    // exportação, diálogos e menus popup pintam só com estes.
    /** Sempre falso desde a 0.3.3 (nenhuma paleta é clara); fica para quem ainda lê. */
    val LightIsLight: Boolean get() = palette.light.isLight
    val LightBackground: Color get() = palette.light.background
    val LightSheet: Color get() = palette.light.sheet
    val LightSurface: Color get() = palette.light.surface
    val LightDialog: Color get() = palette.light.dialog
    val LightSurfaceAlt: Color get() = palette.light.surfaceAlt
    val LightText: Color get() = palette.light.text
    val LightTextMuted: Color get() = palette.light.textMuted
    val LightTextFaint: Color get() = palette.light.textFaint
    val LightDivider: Color get() = palette.light.divider
    val LightInk: Color get() = palette.light.ink
    val LightOnInk: Color get() = palette.light.onInk
    val LightAccentInk: Color get() = palette.light.accentInk
    val LightDanger: Color get() = palette.light.danger
    val LightShadow: Color get() = palette.light.shadow

    // --- Omega home: papéis só da Home (ver AureaHomeRoles) -------------------
    val HomeDrawer: Color get() = palette.home.drawer
    val HomeThumb: Color get() = palette.home.thumb
    val HomeSelectionBar: Color get() = palette.home.selectionBar
    val HomeOnSelectionBar: Color get() = palette.home.onSelectionBar
    val HomeMenuScrim: Color get() = palette.home.menuScrim
    val HomeCover: Color get() = palette.home.cover
    val HomeAvatar: Color get() = palette.home.avatar
    val HomeOnAvatar: Color get() = palette.home.onAvatar
    val HomePillSelected: Color get() = palette.home.pillSelected
    val HomeOnPillSelected: Color get() = palette.home.onPillSelected
    val HomePillUnselected: Color get() = palette.home.pillUnselected
    val HomeOnPillUnselected: Color get() = palette.home.onPillUnselected
    val HomePillPressed: Color get() = palette.home.pillPressed
    val HomeOnPillPressed: Color get() = palette.home.onPillPressed
    val HomeShadowSoft: Color get() = palette.home.shadowSoft
    val HomeShadowStrong: Color get() = palette.home.shadowStrong
    val HomeBadge: Color get() = palette.home.badge
    val HomeOnBadge: Color get() = palette.home.onBadge

    // --- Omega fundação: moldura dos painéis de propriedade (F5, PanelFrame) ----
    /**
     * Fundo da moldura e das colunas (A9 da referência). 0.3.3 tema: o tom da
     * barra do editor, um degrau acima do fundo; no AUREA o `editorPanel` é
     * igual ao `background`, e as divisórias e a aba ativa sumiriam.
     */
    val PanelFrameBackground: Color get() = palette.editorBar
    /** Véu do conteúdo central: branco 10 % sobre a moldura. */
    val PanelFrameContent = Color(0x1AFFFFFF)
    /** Divisórias de 0,7 dp e fundo da aba ativa (A11). */
    val PanelFrameDivider: Color get() = palette.background
    /** Barra do painel de propriedade, mais clara que a da camada (S8). */
    val PanelBar: Color get() = palette.border

    // --- Omega adicionar (área 08): painel de adicionar, navegadores e exportação ---
    // Derivados dos tons que todo tema já tem (valem para os 7 temas sem campo novo).
    /** Fundo do painel de adicionar e dos navegadores (A9). */
    val AddPanel: Color get() = palette.editorPanel
    /** Prévia do navegador, divisores e células apertadas (A11). */
    val AddPanelDeep: Color get() = palette.background
    /** Fundo da miniatura sem imagem e das células de ação (S8). */
    val AddTile: Color get() = palette.border
    /** Barra de controle do navegador de mídia (S7). */
    val AddControlBar: Color get() = palette.chipHigh
    /** Véu leve sobre miniaturas (#11000000). */
    val AddScrimSoft = Color(0x11000000)
    /** Barra "+" do cartão de áudio (#32000000). */
    val AddScrimBar = Color(0x32000000)
    /** Faixa de título do cartão de preset/elemento: preto 27 %. */
    val AddTitleScrim = Color(0x45000000)
    /** Chip do nome na prévia: preto 40 %. */
    val AddChipScrim = Color(0x66000000)
    /** Selo "NOVO" (amarelo) e o texto escuro dele. */
    val AddNewBadge = Color(0xFFFFCD4B)
    val AddOnNewBadge = Color(0xFF1F2238)
    /** Página "Exportar e compartilhar" (tons da paleta). */
    val ExportPage: Color get() = palette.light.sheet
    val ExportCard: Color get() = palette.light.surface
    val ExportText: Color get() = palette.light.text
    val ExportHint: Color get() = palette.light.textMuted
    val ExportLine: Color get() = palette.light.divider
    val ExportPrimary: Color get() = palette.light.ink
    val ExportOnPrimary: Color get() = palette.light.onInk
    /** Seta e ⓘ das linhas. */
    val ExportAction: Color get() = palette.light.textFaint
    /** Página do resultado e os botões dela (S9). */
    val ExportResult: Color get() = palette.background
    val ExportResultButton: Color get() = palette.chipHigh
    /** Véu do progresso sobre a página (50 %). */
    val ExportScrim = Color(0x80000000)

    // Omega painel: (área 04) papéis do painel da camada da referência ----------
    /** Fundo do painel da camada (A9). 0.3.3 tema: o mesmo tom da moldura (ver [PanelFrameBackground]). */
    val LayerPanel: Color get() = palette.editorBar
    /** Pílula do trio de tempo e cartão do painel embutido (S8). */
    val LayerPanelButton: Color get() = palette.border
    /** Botão pressionado (A11). */
    val LayerPanelPressed: Color get() = palette.background
    /** Corpo dos painéis embutidos de velocidade e volume (A10). */
    val LayerPanelBody: Color get() = palette.surface
    /** Botões de retempo (S9). */
    val LayerPanelRetime: Color get() = palette.chipHigh
    /** Ícone e rótulo desabilitados (S1). */
    val LayerPanelDisabled: Color get() = palette.tickStrong
    /** Caixa de valor dos painéis embutidos (branco 5 %). */
    val LayerPanelValueBox = Color(0x0DFFFFFF)
    /** Fundo do gráfico do editor de curva (P10). */
    val CurveGraphBackground: Color get() = palette.curveGraph
    /** Coluna de presets e grupos do editor de curva (S9). */
    val CurveColumn: Color get() = palette.chipHigh

    // --- Omega efeitos (área 07): papéis do painel de efeitos e do navegador ----
    // Ver [AureaFxRoles]: os tons do tema em uso.
    val FxPanel: Color get() = palette.fx.panel
    val FxListSurface: Color get() = palette.fx.listSurface
    val FxLowSurface: Color get() = palette.fx.lowSurface
    val FxSunken: Color get() = palette.fx.sunken
    val FxDarkest: Color get() = palette.fx.darkest
    val FxSearch: Color get() = palette.fx.search
    val FxField: Color get() = palette.fx.field
    val FxSegment: Color get() = palette.fx.segment
    val FxSegmentOn: Color get() = palette.fx.segmentOn
    val FxLabel: Color get() = palette.fx.label
    val FxValueVeil: Color get() = palette.fx.valueVeil
    val FxAxisX: Color get() = palette.fx.axisX
    val FxAxisZ: Color get() = palette.fx.axisZ
    val FxNeedle: Color get() = palette.fx.needle
    val FxTick: Color get() = palette.fx.tick
    val FxTrack: Color get() = palette.fx.track
    val FxTrackDisabled: Color get() = palette.fx.trackDisabled
    val FxTipText: Color get() = palette.fx.tipText
    val FxDisabledText: Color get() = palette.fx.disabledText
    val FxColorText: Color get() = palette.fx.colorText
    val FxAxisLabel: Color get() = palette.fx.axisLabel
    val FxListIcon: Color get() = palette.fx.listIcon
    val FxTextSecondary: Color get() = palette.fx.textSecondary
    val FxScrim: Color get() = palette.fx.scrim

    // --- Omega conteúdo (área 06): editores de texto, fontes, desenho e pontos --
    // Ver [AureaContentRoles]: os tons do tema em uso.
    val ContentStyleBar: Color get() = palette.content.styleBar
    val ContentStyleButton: Color get() = palette.content.styleButton
    val ContentInputStrip: Color get() = palette.content.inputStrip
    val ContentEditPanel: Color get() = palette.content.editPanel
    val ContentBrowser: Color get() = palette.content.browser
    val ContentBrowserLine: Color get() = palette.content.browserLine
    val ContentDrawer: Color get() = palette.content.drawer
    val ContentDrawerHeader: Color get() = palette.content.drawerHeader
    val ContentTrackpad: Color get() = palette.content.trackpad
    val ContentGuide: Color get() = palette.content.guide
    val ContentStrongLine: Color get() = palette.content.strongLine
    val ContentPointMarker: Color get() = palette.content.pointMarker
    val ContentHint: Color get() = palette.content.hint
    val ContentValuePill: Color get() = palette.content.valuePill
}

/**
 * Omega conteúdo (área 06): os PAPÉIS dos editores de conteúdo (modo de texto,
 * navegador de fontes, desenho e Editar pontos; nomes entre «» em
 * `build/reference/am-omega-20261026/areas/06-content-editors.md`). Cada papel
 * cai no tom que o tema já tem ([mappedFrom]; 0.3.3: sem valores próprios da
 * Omega). Par exato de `AureaContentRoles` do Theme.swift.
 */
data class AureaContentRoles(
    /** «barra de ferramentas mais escura» (K2): a barra de estilo do modo de texto. */
    val styleBar: Color,
    /** «botão de estilo»: as peças da barra de estilo e o corpo dos popups. */
    val styleButton: Color,
    /** «superfície mais escura» (A1): a faixa de entrada do texto. */
    val inputStrip: Color,
    /** «fundo do painel de edição» (A3): o painel grande do modo de texto. */
    val editPanel: Color,
    /** «superfície do navegador» (S10): o navegador de fontes em tela cheia. */
    val browser: Color,
    /** «linha fina» (branco 10 %): fio da barra e borda da célula do navegador. */
    val browserLine: Color,
    /** «fundo do painel» (A9) e «superfície elevada» (A2): a gaveta de filtros e o cabeçalho dela. */
    val drawer: Color,
    val drawerHeader: Color,
    /** «superfície elevada» (S10): o trackpad do Editar pontos. */
    val trackpad: Color,
    /** «texto secundário» (P6): o texto-guia do trackpad. */
    val guide: Color,
    /** «linha forte» (A11): divisórias de 0,7 das colunas do desenho e dos pontos. */
    val strongLine: Color,
    /** Marcador da faixa de pontos (o escolhido usa o destaque). */
    val pointMarker: Color,
    /** «texto terciário»: dica do campo de texto. */
    val hint: Color,
    /** Pílula do valor (espessura do desenho). */
    val valuePill: Color,
) {
    companion object {
        /** Temas escuros: cada papel no tom que o tema já tem. */
        fun mappedFrom(p: AureaPalette) = AureaContentRoles(
            styleBar = p.stage, styleButton = p.chip, inputStrip = p.surface,
            editPanel = p.editorPanelHigh, browser = p.editorPanel, browserLine = p.border,
            drawer = p.surfaceHigh, drawerHeader = p.surface, trackpad = p.editorRowPill,
            guide = p.muted, strongLine = p.background, pointMarker = p.subtle,
            hint = p.subtle, valuePill = p.editorRowPill,
        )
    }
}

/**
 * Omega efeitos (área 07): os PAPÉIS do painel de efeitos, das linhas de
 * parâmetro e do navegador de efeitos (nomes entre «» em `build/reference/
 * am-omega-20261026/areas/07-effects.md`). Cada papel cai no tom que o tema já
 * tem ([mappedFrom]; 0.3.3: sem valores próprios da Omega). Par exato de
 * `AureaFxRoles` do Theme.swift (mesmos nomes e valores).
 */
data class AureaFxRoles(
    /** «painel» (A9): fundo do painel de efeitos. */
    val panel: Color,
    /** «superfície de lista» (S9): linha do efeito, cabeçalho e corpo dos controles, navegador. */
    val listSurface: Color,
    /** «superfície baixa» (S10): "+ Adicionar efeito", faixa da seção, recomendados. */
    val lowSurface: Color,
    /** «afundado» (A11): rótulo ativo, caixa pressionada, suspenso. */
    val sunken: Color,
    /** «mais escuro» (A12): grade de categorias, folha de detalhe, borda das etiquetas. */
    val darkest: Color,
    /** «busca» (S8): fundo da busca do navegador. */
    val search: Color,
    /** «campo» (S6): campo da busca, linha de variação. */
    val field: Color,
    /** «segmento» (P11) e «segmento ativo» (P10) da escolha segmentada. */
    val segment: Color,
    val segmentOn: Color,
    /** «rótulo» (P11 a 30 %): fundo do rótulo do parâmetro. */
    val label: Color,
    /** «véu 20 %»: fundo da caixa de valor. */
    val valueVeil: Color,
    /** «eixo X» (C1) e «eixo Z» (L3_A); o Y usa o texto. */
    val axisX: Color,
    val axisZ: Color,
    /** «agulha» da régua (C1). */
    val needle: Color,
    /** «risco» da régua (D1); o risco maior usa o texto. */
    val tick: Color,
    /** «trilho» do slider (P3) e o preenchimento desabilitado. */
    val track: Color,
    val trackDisabled: Color,
    /** «texto de dica» (S2) e «texto desabilitado» (S1). */
    val tipText: Color,
    val disabledText: Color,
    /** «texto de cor» (P6): o "R G B" da linha de cor. */
    val colorText: Color,
    /** «rótulo de eixo» (P3): X:/Y: e as letras das caixas. */
    val axisLabel: Color,
    /** «ícone de lista»: ⋮ e 🗑 do cabeçalho dos controles. */
    val listIcon: Color,
    /** «texto secundário» (P5): nome em inglês no navegador. */
    val textSecondary: Color,
    /** Véu da folha de detalhe (preto 30 %). */
    val scrim: Color,
) {
    companion object {
        /** Temas escuros: cada papel no tom que o tema já tem. */
        fun mappedFrom(p: AureaPalette) = AureaFxRoles(
            // 0.3.3 tema: o tom da moldura dos painéis (acima do `sunken`).
            panel = p.editorBar, listSurface = p.chipHigh, lowSurface = p.editorRowPill,
            sunken = p.background, darkest = p.stage, search = p.border,
            field = p.chip, segment = p.chip, segmentOn = p.chipHigh,
            label = p.chip, valueVeil = Color(0x33000000),
            axisX = p.keyframe, axisZ = p.accent, needle = p.accent,
            tick = p.tickStrong, track = p.muted, trackDisabled = p.chipHigh,
            tipText = p.subtle, disabledText = p.subtle, colorText = p.muted,
            axisLabel = p.muted, listIcon = Color(0xFFF7F9FB), textSecondary = p.muted,
            scrim = Color(0x4D000000),
        )
    }
}

/**
 * Paleta de um tema: só o que muda entre temas (fundos, superfícies,
 * marca e destaque). [id] é o que fica salvo nos Ajustes; nunca renomeie.
 */
data class AureaPalette(
    val id: String,
    val brandDeep: Color,
    val brand: Color,
    val accent: Color,
    val keyframe: Color,
    val background: Color,
    val surface: Color,
    val surfaceHigh: Color,
    val chip: Color,
    val chipHigh: Color,
    val border: Color,
    val muted: Color,
    val subtle: Color,
    val onAccent: Color,
    val accentDim: Color,
    val keyframeDim: Color,
    val stage: Color,
    val hairline: Color,
    val editorTopBar: Color,
    val editorPanel: Color,
    val editorPanelHigh: Color,
    val pill: Color,
    val actionDim: Color,
    val statusBarVeil: Color,
    val systemBarVeil: Color,
    val tickWeak: Color,
    val tickStrong: Color,
    val railModeFill: Color,
    val fieldFilled: Color,
    // --- Editor redesenhado (2026-09-29): fundo, barras e pílula das fileiras.
    // Os outros temas derivam dos tons que já têm; o Aurea usa os do mockup.
    val editorCanvas: Color = editorPanelHigh,
    val editorBar: Color = surfaceHigh,
    val editorBarLine: Color = border,
    val editorRowPill: Color = chip,
    val editorGlyphBox: Color = chipHigh,
    /** Ícone e texto sobre a cor de marca (`Brand`/`Action`). */
    val onBrand: Color = Color(0xFFF7F9FB),
    // Omega fundação: papéis das telas fora do editor. Nulo = derivados dos tons
    // deste tema ([AureaLightRoles.mappedFrom]); nenhuma paleta passa outro.
    val lightRoles: AureaLightRoles? = null,
    // Omega timeline (área 03): cabeçalho da linha, aba do tempo atual e fundo
    // da linha arrastada. Os outros temas derivam dos tons que já têm.
    val timelineHeader: Color = editorRowPill,
    val timelineTimeTab: Color = editorRowPill,
    val timelineDragRow: Color = chipHigh,
    // Omega home: papéis só da Home (miniatura, pílulas, barra de seleção...).
    // Nulo = derivados dos tons deste tema ([AureaHomeRoles.mappedFrom]); nenhuma paleta passa outro.
    val homeRoles: AureaHomeRoles? = null,
    // Omega painel: (área 04) fundo do gráfico do editor de curva (P10 da
    // referência). Os outros temas derivam do campo alto que já têm.
    val curveGraph: Color = chipHigh,
) {
    // Omega fundação: os papéis claros em uso (ver [AureaLightRoles]).
    val light: AureaLightRoles = lightRoles ?: AureaLightRoles.mappedFrom(this)
    // Omega efeitos (área 07): papéis do painel de efeitos, derivados dos tons
    // do tema ([AureaFxRoles]).
    val fx: AureaFxRoles = AureaFxRoles.mappedFrom(this)
    // Omega conteúdo (área 06): papéis dos editores de conteúdo ([AureaContentRoles]).
    val content: AureaContentRoles = AureaContentRoles.mappedFrom(this)
    // Omega home: os papéis da Home em uso (ver [AureaHomeRoles]).
    val home: AureaHomeRoles = homeRoles ?: AureaHomeRoles.mappedFrom(this)

    companion object {
        val Aurea = AureaPalette(
            id = "aurea",
            brandDeep = Color(0xFF123A63),
            brand = Color(0xFF245D8C),
            accent = Color(0xFF6FAED9),
            keyframe = Color(0xFFA9D3EC),
            background = Color(0xFF0F141A),
            surface = Color(0xFF151C24),
            surfaceHigh = Color(0xFF1B2530),
            chip = Color(0xFF212D3A),
            chipHigh = Color(0xFF323D49),
            border = Color(0xFF273442),
            muted = Color(0xFFAAB6C3),
            subtle = Color(0xFF7C8A99),
            onAccent = Color(0xFF0B1117),
            accentDim = Color(0xFF1D3A55),
            keyframeDim = Color(0xFF22405A),
            stage = Color(0xFF0A0E13),
            hairline = Color(0xB8273442),
            editorTopBar = Color(0xFF0F141A),
            editorPanel = Color(0xFF0F141A),
            editorPanelHigh = Color(0xFF151C24),
            pill = Color(0xFF1B2530),
            actionDim = Color(0xFF16304A),
            statusBarVeil = Color(0xFF070A0E),
            systemBarVeil = Color(0xFF0B0F13),
            tickWeak = Color(0xFF43516A),
            tickStrong = Color(0xFF7485A3),
            railModeFill = Color(0xFF1E222D),
            fieldFilled = Color(0xFF272B33),
            editorCanvas = Color(0xFF161C2A),
            editorBar = Color(0xFF1B2330),
            editorBarLine = Color(0xFF2A3444),
            editorRowPill = Color(0xFF222B3B),
            editorGlyphBox = Color(0xFF2E3B4E),
        )
        val Midnight = AureaPalette(
            id = "midnight",
            brandDeep = Color(0xFF15325A),
            brand = Color(0xFF2C66A0),
            accent = Color(0xFF7DB8E6),
            keyframe = Color(0xFFB3D9F0),
            background = Color(0xFF000000),
            surface = Color(0xFF0B0D10),
            surfaceHigh = Color(0xFF14171C),
            chip = Color(0xFF1A1E24),
            chipHigh = Color(0xFF2A2F37),
            border = Color(0xFF1E232A),
            muted = Color(0xFFA7B0BA),
            subtle = Color(0xFF77818C),
            onAccent = Color(0xFF05080B),
            accentDim = Color(0xFF15283C),
            keyframeDim = Color(0xFF1A3247),
            stage = Color(0xFF000000),
            hairline = Color(0xB81E232A),
            editorTopBar = Color(0xFF000000),
            editorPanel = Color(0xFF000000),
            editorPanelHigh = Color(0xFF0B0D10),
            pill = Color(0xFF14171C),
            actionDim = Color(0xFF102438),
            statusBarVeil = Color(0xFF000000),
            systemBarVeil = Color(0xFF000000),
            tickWeak = Color(0xFF3A4250),
            tickStrong = Color(0xFF6C788C),
            railModeFill = Color(0xFF15181E),
            fieldFilled = Color(0xFF1A1D22),
        )
        val Graphite = AureaPalette(
            id = "graphite",
            brandDeep = Color(0xFF3A4250),
            brand = Color(0xFF566273),
            accent = Color(0xFFB7C4D3),
            keyframe = Color(0xFFD6DEE8),
            background = Color(0xFF16181C),
            surface = Color(0xFF1D2025),
            surfaceHigh = Color(0xFF25292F),
            chip = Color(0xFF2B3037),
            chipHigh = Color(0xFF3A4048),
            border = Color(0xFF33383F),
            muted = Color(0xFFB0B6BE),
            subtle = Color(0xFF838A94),
            onAccent = Color(0xFF101215),
            accentDim = Color(0xFF2E343C),
            keyframeDim = Color(0xFF343B45),
            stage = Color(0xFF111316),
            hairline = Color(0xB833383F),
            editorTopBar = Color(0xFF16181C),
            editorPanel = Color(0xFF16181C),
            editorPanelHigh = Color(0xFF1D2025),
            pill = Color(0xFF25292F),
            actionDim = Color(0xFF2A3038),
            statusBarVeil = Color(0xFF0D0E10),
            systemBarVeil = Color(0xFF121417),
            tickWeak = Color(0xFF4A515C),
            tickStrong = Color(0xFF7F8896),
            railModeFill = Color(0xFF23272D),
            fieldFilled = Color(0xFF2A2E34),
        )
        val Emerald = AureaPalette(
            id = "emerald",
            brandDeep = Color(0xFF0F4A3A),
            brand = Color(0xFF1C7A5E),
            accent = Color(0xFF5BD6A8),
            keyframe = Color(0xFFA6ECD2),
            background = Color(0xFF0B1411),
            surface = Color(0xFF111D19),
            surfaceHigh = Color(0xFF172722),
            chip = Color(0xFF1D302A),
            chipHigh = Color(0xFF2C403A),
            border = Color(0xFF223A33),
            muted = Color(0xFFA6BDB5),
            subtle = Color(0xFF789088),
            onAccent = Color(0xFF06110D),
            accentDim = Color(0xFF163A30),
            keyframeDim = Color(0xFF1D4539),
            stage = Color(0xFF080F0C),
            hairline = Color(0xB8223A33),
            editorTopBar = Color(0xFF0B1411),
            editorPanel = Color(0xFF0B1411),
            editorPanelHigh = Color(0xFF111D19),
            pill = Color(0xFF172722),
            actionDim = Color(0xFF123326),
            statusBarVeil = Color(0xFF060B09),
            systemBarVeil = Color(0xFF09100D),
            tickWeak = Color(0xFF3B5249),
            tickStrong = Color(0xFF6C8C80),
            railModeFill = Color(0xFF18251F),
            fieldFilled = Color(0xFF1F2B27),
        )
        val Amethyst = AureaPalette(
            id = "amethyst",
            brandDeep = Color(0xFF3A2470),
            brand = Color(0xFF6246B8),
            accent = Color(0xFFB9A2FF),
            keyframe = Color(0xFFDCCFFF),
            background = Color(0xFF110E19),
            surface = Color(0xFF181423),
            surfaceHigh = Color(0xFF201B2E),
            chip = Color(0xFF282238),
            chipHigh = Color(0xFF383049),
            border = Color(0xFF2F2842),
            muted = Color(0xFFB5ADC6),
            subtle = Color(0xFF877E99),
            onAccent = Color(0xFF0D0A14),
            accentDim = Color(0xFF2C2248),
            keyframeDim = Color(0xFF362B55),
            stage = Color(0xFF0C0A12),
            hairline = Color(0xB82F2842),
            editorTopBar = Color(0xFF110E19),
            editorPanel = Color(0xFF110E19),
            editorPanelHigh = Color(0xFF181423),
            pill = Color(0xFF201B2E),
            actionDim = Color(0xFF261E42),
            statusBarVeil = Color(0xFF08070C),
            systemBarVeil = Color(0xFF0D0B13),
            tickWeak = Color(0xFF4A4260),
            tickStrong = Color(0xFF7D7399),
            railModeFill = Color(0xFF211C2D),
            fieldFilled = Color(0xFF272233),
        )
        val Sunset = AureaPalette(
            id = "sunset",
            brandDeep = Color(0xFF6A3212),
            brand = Color(0xFFB45A1E),
            accent = Color(0xFFFFB060),
            keyframe = Color(0xFFFFD6A8),
            background = Color(0xFF15100C),
            surface = Color(0xFF1E1712),
            surfaceHigh = Color(0xFF281F18),
            chip = Color(0xFF30251D),
            chipHigh = Color(0xFF40342B),
            border = Color(0xFF3A2C22),
            muted = Color(0xFFC4B4A6),
            subtle = Color(0xFF96867A),
            onAccent = Color(0xFF140C06),
            accentDim = Color(0xFF45280F),
            keyframeDim = Color(0xFF4E3218),
            stage = Color(0xFF100C09),
            hairline = Color(0xB83A2C22),
            editorTopBar = Color(0xFF15100C),
            editorPanel = Color(0xFF15100C),
            editorPanelHigh = Color(0xFF1E1712),
            pill = Color(0xFF281F18),
            actionDim = Color(0xFF3E230D),
            statusBarVeil = Color(0xFF0B0806),
            systemBarVeil = Color(0xFF110D0A),
            tickWeak = Color(0xFF5A4838),
            tickStrong = Color(0xFF907A66),
            railModeFill = Color(0xFF271E17),
            fieldFilled = Color(0xFF2E251E),
        )
        val all = listOf(Aurea, Midnight, Graphite, Emerald, Amethyst, Sunset)
        /** Id desconhecido (inclusive o "omega" salvo pela 0.3.2) cai na AUREA. */
        fun of(id: String?): AureaPalette = all.firstOrNull { it.id == id } ?: Aurea
    }
}

// =============================================================================
// Omega fundação: PAPÉIS DAS TELAS FORA DO EDITOR (o nome "Light" ficou da 0.3.2)
//
// Home, gaveta, folha "Novo projeto", página de exportação, diálogos e menus
// popup pintam SÓ com estes papéis (acessores `AureaColors.Light*`). 0.3.3: a
// paleta clara da Omega saiu; em todo tema cada papel cai no tom escuro que o
// tema já tem (texto claro, destaque do tema), então a mesma tela funciona em
// qualquer tema sem `if`.
// Par exato de `AureaLightRoles` em Theme.swift (mesmos nomes e valores).
//
// De onde vêm (análises 01, 02 e 08): background = homeBackground; sheet =
// homeSheet/exportPage (W4); surface = homeSurface/exportCard/menus (W1);
// dialog = dialogoClaro (W5); surfaceAlt = drawerGroup (G1); text =
// homeText/exportText/textoDialogo (HT); textMuted = exportHint (P5);
// textFaint = homeHint/dica (P3); divider = drawerDivider/exportLine/
// fioDialogo (G3); ink + onInk = segmentActive/exportPrimary (HT + Y1).
// =============================================================================
data class AureaLightRoles(
    /** Verdadeiro só se os papéis forem claros. Nenhuma paleta é clara desde a 0.3.3. */
    val isLight: Boolean,
    /** Fundo da página: abas da Home, gaveta, cabeçalhos. */
    val background: Color,
    /** Folha/página agrupada: "Novo projeto", página de exportação. */
    val sheet: Color,
    /** Superfície: cartões, menus popup, caixas suspensas, campos, barra de abas. */
    val surface: Color,
    /** Fundo dos diálogos. */
    val dialog: Color,
    /** Superfície alternativa: grupo da gaveta, linha pressionada ou escolhida. */
    val surfaceAlt: Color,
    /** Texto e ícone principais. */
    val text: Color,
    /** Texto secundário: rótulo, sublinha, valor à direita. */
    val textMuted: Color,
    /** Dica, placeholder e texto desabilitado. */
    val textFaint: Color,
    /** Fio divisório (0,5–1 dp). */
    val divider: Color,
    /** Preenchimento forte: segmento ativo, botão principal (Exportar). */
    val ink: Color,
    /** Texto e ícone sobre [ink]. */
    val onInk: Color,
    /** Destaque legível sobre estas superfícies: botão positivo, ✓, barra da linha escolhida. */
    val accentInk: Color,
    /** Ação destrutiva sobre estas superfícies. */
    val danger: Color,
    /** Sombra de cartões e menus. */
    val shadow: Color,
) {
    companion object {
        /** Cada papel no tom escuro que o tema já tem. */
        fun mappedFrom(p: AureaPalette) = AureaLightRoles(
            isLight = false,
            background = p.background,
            sheet = p.surface,
            surface = p.surfaceHigh,
            dialog = p.surfaceHigh,
            surfaceAlt = p.chip,
            text = Color(0xFFF7F9FB),
            textMuted = p.muted,
            textFaint = p.subtle,
            divider = p.border,
            ink = p.brand,
            onInk = p.onBrand,
            accentInk = p.accent,
            danger = Color(0xFFFF453A),
            shadow = Color(0x73000000),
        )
    }
}

// =============================================================================
// Omega home: PAPÉIS SÓ DA HOME (área 01)
//
// O que a Home pinta e que os papéis comuns (AureaLightRoles) não cobrem:
// fundo da gaveta, miniatura, barra de seleção, véu do menu rápido, cobertura
// atrás da folha, avatar sem login, pílulas das subabas, sombras e o selo de
// duração. 0.3.3: em todo tema, os tons escuros que o tema já tem.
// Par exato de `AureaHomeRoles` em Theme.swift (mesmos nomes e valores).
// =============================================================================
data class AureaHomeRoles(
    /** Fundo da gaveta de ajustes. */
    val drawer: Color,
    /** Fundo da miniatura do projeto (antes da imagem). */
    val thumb: Color,
    /** Barra inferior do modo de seleção. */
    val selectionBar: Color,
    /** Ícone e rótulo sobre [selectionBar]. */
    val onSelectionBar: Color,
    /** Véu claro atrás do menu rápido do projeto. */
    val menuScrim: Color,
    /** Cobertura atrás da folha "Novo projeto" e da gaveta. */
    val cover: Color,
    /** Avatar sem login. */
    val avatar: Color,
    /** Pessoa/inicial sobre [avatar]. */
    val onAvatar: Color,
    /** Pílula de subaba escolhida (fundo e texto). */
    val pillSelected: Color,
    val onPillSelected: Color,
    /** Pílula não escolhida (fundo e texto). */
    val pillUnselected: Color,
    val onPillUnselected: Color,
    /** Pílula não escolhida enquanto pressionada (fundo e texto). */
    val pillPressed: Color,
    val onPillPressed: Color,
    /** Sombra suave (cartões, menu rápido) e forte (pílula ativa, ordenação). */
    val shadowSoft: Color,
    val shadowStrong: Color,
    /** Selo de duração sobre a miniatura (fundo e texto). */
    val badge: Color,
    val onBadge: Color,
) {
    companion object {
        /** Temas escuros: cada papel no tom escuro que o tema já tem. */
        fun mappedFrom(p: AureaPalette) = AureaHomeRoles(
            drawer = p.surface,
            thumb = p.chip,
            selectionBar = p.chipHigh,
            onSelectionBar = Color(0xFFF7F9FB),
            menuScrim = p.background.copy(alpha = 0.8f),
            cover = Color(0xB3000000),
            avatar = p.chipHigh,
            onAvatar = Color(0xFFF7F9FB),
            pillSelected = p.brand,
            onPillSelected = p.onBrand,
            pillUnselected = p.surfaceHigh,
            onPillUnselected = p.muted,
            pillPressed = p.chip,
            onPillPressed = Color(0xFFF7F9FB),
            shadowSoft = Color(0x26000000),
            shadowStrong = Color(0x59000000),
            badge = Color(0xB3000000),
            onBadge = Color(0xFFFFFFFF),
        )
    }
}

/** Cor (fundo de barra) e glifo por tipo de camada — `aurea_tipo_da_camada.dart`. */
enum class LayerType(val kind: Int, val color: Color, val glyph: Char, val label: String) {
    Video(1, Color(0xFF6A52E0), CupertinoGlyph.VideocamFill, "Vídeo"),
    Image(2, Color(0xFF3D6FD9), CupertinoGlyph.PhotoFill, "Imagem"),
    Audio(3, Color(0xFF1F8C93), CupertinoGlyph.MusicNote, "Áudio"),
    Text(4, Color(0xFFB07A16), CupertinoGlyph.Textformat, "Texto"),
    // 0.3.4: Forma em contorno (os cheios das outras linhas viram contorno no AureaIconStyle).
    Shape(5, Color(0xFF2E9459), CupertinoGlyph.Circle, "Forma"),
    Null(6, Color(0xFF444C5C), CupertinoGlyph.SmallcircleCircle, "Nulo"),
    Adjustment(7, Color(0xFF5A4A7A), CupertinoGlyph.SliderHorizontal3, "Ajuste"),
    Camera(8, Color(0xFF2A7B9B), CupertinoGlyph.CameraFill, "Câmera"),
    Light(9, Color(0xFFC06A24), CupertinoGlyph.Lightbulb, "Luz"),
    Model3D(10, Color(0xFFC06A24), CupertinoGlyph.CubeFill, "Objeto 3D"),
    Particles(11, Color(0xFFB0417A), CupertinoGlyph.Sparkles, "Partículas"),
    Group(12, Color(0xFF4C5566), CupertinoGlyph.FolderFill, "Grupo");

    /** Rótulo no catálogo (o [label] em pt continua para quem já usa). */
    val labelRes: Int get() = when (this) {
        Video -> com.aurea.aurea.R.string.target_video
        Image -> com.aurea.aurea.R.string.target_image
        Audio -> com.aurea.aurea.R.string.target_audio
        Text -> com.aurea.aurea.R.string.target_text
        Shape -> com.aurea.aurea.R.string.target_shape
        Null -> com.aurea.aurea.R.string.sh_add_null
        Adjustment -> com.aurea.aurea.R.string.app_layer_type_adjust
        Camera -> com.aurea.aurea.R.string.app_layer_type_camera
        Light -> com.aurea.aurea.R.string.cat_light
        Model3D -> com.aurea.aurea.R.string.app_layer_type_model
        Particles -> com.aurea.aurea.R.string.panel_particulas
        Group -> com.aurea.aurea.R.string.editor_grupo
    }

    companion object {
        fun of(kind: Int): LayerType = entries.firstOrNull { it.kind == kind } ?: Null
    }
}

/**
 * Espaçamento entre letras (tracking) dos estilos do Aurea — ZERO em árabe.
 *
 * A escrita árabe é cursiva: o Android não aplica letter-spacing ao desenhar
 * árabe, mas aplica ao MEDIR a largura intrínseca do texto. Com o tracking
 * negativo do Aurea a medida saía menor que a linha real e o Compose quebrava
 * a última letra do título para a linha de baixo ("المشاري / ع"). Tipografia
 * árabe também não usa tracking. Lido a cada acesso: trocar de idioma no app
 * (Activity recriada, processo o mesmo) já pega o valor novo.
 */
fun aureaTracking(sp: Float): TextUnit = if (Locale.getDefault().language == "ar") 0.sp else sp.sp

/** Estilo guardado por estado de tracking (latino / árabe): monta uma vez cada. */
private class TrackedStyle(private val make: () -> TextStyle) : ReadOnlyProperty<Any?, TextStyle> {
    private var latin: TextStyle? = null
    private var arabic: TextStyle? = null
    override fun getValue(thisRef: Any?, property: KProperty<*>): TextStyle =
        if (Locale.getDefault().language == "ar") arabic ?: make().also { arabic = it }
        else latin ?: make().also { latin = it }
}

private fun styled(make: () -> TextStyle): ReadOnlyProperty<Any?, TextStyle> = TrackedStyle(make)

/**
 * Tipografia. O Flutter do app antigo faz TODO texto herdar 15 sp, −0,1 de
 * espaçamento e altura de linha 1,35 (bodyMedium do M3). Sem essa base as
 * alturas dos componentes não batem com os prints.
 *
 * Estáticos: nenhum TextStyle é alocado por composição.
 */
object AureaType {
    /** A base de tudo (15 sp, −0,1, altura 1,35). */
    val Base by styled { TextStyle(
        fontFamily = FontFamily.Default,
        fontSize = 15.sp,
        fontWeight = FontWeight.Normal,
        letterSpacing = aureaTracking(-0.1f),
        lineHeight = 1.35.em,
        color = AureaColors.Text,
        platformStyle = PlatformTextStyle(includeFontPadding = false),
        lineHeightStyle = LineHeightStyle(LineHeightStyle.Alignment.Center, LineHeightStyle.Trim.None),
    ) }

    /** Deriva um estilo da base (o `merge` que o Flutter herdava). */
    fun of(
        size: Float,
        weight: FontWeight = FontWeight.Normal,
        spacing: Float = -0.1f,
        color: Color = AureaColors.Text,
        lineHeight: Float? = null,
    ): TextStyle = Base.merge(
        TextStyle(
            fontSize = size.sp,
            fontWeight = weight,
            letterSpacing = aureaTracking(spacing),
            color = color,
            lineHeight = lineHeight?.em ?: 1.35.em,
        ),
    )

    // --- Escala --------------------------------------------------------------
    val Display by styled { of(30f, FontWeight.W800, -0.8f, lineHeight = 1.05f) }
    val HeadlineLarge by styled { Base.merge(TextStyle(fontSize = 34.sp, fontWeight = FontWeight.W700, letterSpacing = aureaTracking(-0.8f), lineHeight = 1.1.em)) }
    /** Título de tela dentro de uma aba (21–22 w700). */
    val ScreenTitle by styled { of(21f, FontWeight.W700, -0.4f) }
    /** Cabeçalho de tela grande (28 w800): o título das abas da Home. */
    val TitleLarge by styled { Base.merge(TextStyle(fontSize = 22.sp, fontWeight = FontWeight.W700, letterSpacing = aureaTracking(-0.5f), lineHeight = 1.27.em)) }
    val TitleMedium by styled { Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W600, letterSpacing = aureaTracking(-0.3f), lineHeight = 1.5.em)) }
    val TitleSmall by styled { of(17f, FontWeight.W700, -0.4f) }
    val BodyLarge by styled { Base.merge(TextStyle(fontSize = 17.sp, letterSpacing = aureaTracking(-0.2f), lineHeight = 1.5.em)) }
    val BodySmall by styled { Base.merge(TextStyle(fontSize = 13.sp, letterSpacing = aureaTracking(0f), lineHeight = 1.33.em, color = AureaColors.Muted)) }
    val LabelLarge by styled { Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W600, letterSpacing = aureaTracking(-0.2f), lineHeight = 1.43.em)) }

    // --- Home / telas fora do editor -----------------------------------------
    val Greeting by styled { of(13.5f, color = AureaColors.Muted) }
    val TabLabel by styled { of(10.5f, FontWeight.W500, 0.1f) }
    val Button by styled { of(17f, FontWeight.W600, -0.2f, AureaColors.OnAccent) }
    val ShortcutLabel by styled { of(12f, FontWeight.W600) }
    val HeroKicker by styled { of(11f, FontWeight.W700, 0.4f, AureaColors.Accent) }
    val HeroTitle by styled { of(18f, FontWeight.W700, -0.2f, AureaColors.OnImage) }
    val HeroSpec by styled { of(11f, color = AureaColors.OnImage70) }
    val HeroPill by styled { of(12.5f, FontWeight.W700, color = AureaColors.OnAccent) }
    val ListCount by styled { of(15f, FontWeight.W700) }
    val CardTitle by styled { of(14f, FontWeight.W600, -0.1f) }
    val CardSpec by styled { of(11f, color = AureaColors.Muted) }
    val LinkRow by styled { of(14.5f) }
    val FeatureTitle by styled { of(15f, FontWeight.W600) }
    val FeatureSubtitle by styled { of(12.5f, color = AureaColors.Muted) }
    val Empty by styled { of(13.5f, color = AureaColors.Muted) }
    val BatchCount by styled { of(13f, color = AureaColors.Muted) }
    val BatchAction by styled { of(13f) }
    val BatchDanger by styled { of(13f, color = AureaColors.Danger) }
    val SearchText by styled { of(17f) }
    val SearchPlaceholder by styled { of(17f, color = AureaColors.FieldPlaceholder) }
    val Note by styled { of(12.5f, color = AureaColors.Muted) }
    val Footer by styled { of(11f, color = AureaColors.Subtle) }
    val Pill by styled { of(12f, FontWeight.W600, color = AureaColors.Accent) }
    val ChipLabel by styled { of(12f, FontWeight.W600) }
    val Segment by styled { of(13f, FontWeight.W600, -0.1f) }
    val DialogField by styled { of(15f) }
    val VersionPill by styled { of(12f, FontWeight.W600, color = AureaColors.Accent) }
    val BetaTitle by styled { of(13f, FontWeight.W700, color = AureaColors.Beta) }
    val BetaBody by styled { of(11f, color = AureaColors.Muted) }

    // --- Folha "Novo projeto" -------------------------------------------------
    val SheetSpec by styled { of(12.5f, color = AureaColors.Muted) }
    val FrameLabel by styled { of(22f, FontWeight.W700, -0.3f) }
    val FrameHint by styled { of(12f, color = AureaColors.Muted) }
    val FormatLabel by styled { of(12.5f, FontWeight.W600, -0.1f) }
    val FormatHint by styled { of(10f, color = AureaColors.Muted) }
    /** Rótulo em caixa-alta de um campo ou seção. */
    val Caps by styled { of(12f, FontWeight.W500, 0.6f, AureaColors.Muted) }
    val NameField by styled { of(17f, spacing = -0.2f) }
    val NamePlaceholder by styled { of(17f, spacing = -0.2f, color = AureaColors.Muted) }
    val DimLabel by styled { of(11f, color = AureaColors.Muted) }
    val DimField by styled { of(16f) }
    val Times by styled { of(16f) }

    // --- Editor (AureaEstilos) ------------------------------------------------
    val EditorTitle by styled { Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600)) }
    val Property by styled { Base.merge(TextStyle(fontSize = 12.5.sp, color = AureaColors.Muted)) }
    val Value by styled { Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W700, color = AureaColors.Keyframe, fontFeatureSettings = "tnum", textDirection = TextDirection.Ltr)) }
    val Label by styled { Base.merge(TextStyle(fontSize = 10.sp, color = AureaColors.Muted)) }
    val Section by styled { Base.merge(TextStyle(fontSize = 11.sp, fontWeight = FontWeight.W600, letterSpacing = aureaTracking(0.3f), color = AureaColors.Muted)) }
    val Body by styled { Base.merge(TextStyle(fontSize = 13.sp)) }
    /** Números (tabulares e sempre LTR: em árabe "-1,5" não vira "1,5-"). */
    val Tabular by styled { TextStyle(fontFeatureSettings = "tnum", textDirection = TextDirection.Ltr) }
}

/** Espaços, raios, alturas e ícones. */
object AureaDims {
    // --- Espaço ---------------------------------------------------------------
    val S1 = 4.dp
    val S2 = 8.dp
    val S3 = 12.dp
    val S4 = 16.dp
    val S5 = 24.dp
    val S6 = 32.dp
    val MinTap = 44.dp
    /** Recuo lateral das telas fora do editor. */
    val Gutter = 20.dp
    /** Alvo redondo padrão (44) e o círculo interno (36). */
    val RoundTarget = 44.dp
    val RoundCircle = 36.dp
    /** Folga no fim das listas para passar da barra de abas translúcida. */
    val ListEndSpace = 120.dp

    // --- Raios ----------------------------------------------------------------
    val RadiusXs = 5.dp
    val RadiusSm = 8.dp
    val RadiusChip = 10.dp
    val RadiusCard = 12.dp
    val RadiusMd = 14.dp
    val RadiusLg = 16.dp
    val RadiusSheet = 18.dp
    val RadiusXl = 20.dp
    val RadiusClip = 8.dp
    val RadiusPill = 999.dp

    // --- Ícones ---------------------------------------------------------------
    val IconXs = 13.dp
    val IconSm = 16.dp
    val IconMd = 20.dp
    val IconLg = 24.dp
    val IconXl = 32.dp

    // --- Controles ------------------------------------------------------------
    val ButtonHeight = 54.dp
    val ControlRow = 54.dp
    val SearchField = 40.dp
    val ChipHeight = 34.dp
    val TabBarHeight = 54.dp
    val CompactBarHeight = 52.dp
    /** Scroll a partir do qual a barra compacta aparece. */
    val CompactBarThreshold = 64.dp
    val SwitchWidth = 59.dp
    val SwitchHeight = 39.dp

    // --- Editor A.01 ----------------------------------------------------------
    val EditorTopBar = 44.dp
    val EditorTransport = 46.dp
    val PreviewResizeStrip = 8.dp
    val PanelHeader = 44.dp

    val Hairline = 0.5.dp
    val SelectionStroke = 2.dp
    val MultiSelectionStroke = 1.5.dp
}

/** Raios prontos. Uma instância por valor — nada de `RoundedCornerShape` por composição. */
object AureaShape {
    val Xs = RoundedCornerShape(AureaDims.RadiusXs)
    val Sm = RoundedCornerShape(AureaDims.RadiusSm)
    val Chip = RoundedCornerShape(AureaDims.RadiusChip)
    val Card = RoundedCornerShape(AureaDims.RadiusCard)
    val Md = RoundedCornerShape(AureaDims.RadiusMd)
    val Lg = RoundedCornerShape(AureaDims.RadiusLg)
    val Sheet = RoundedCornerShape(AureaDims.RadiusSheet)
    val Xl = RoundedCornerShape(AureaDims.RadiusXl)
    val SheetTop = RoundedCornerShape(topStart = AureaDims.RadiusXl, topEnd = AureaDims.RadiusXl)
    val Pill = RoundedCornerShape(AureaDims.RadiusPill)
    val Circle = CircleShape
}

/** Movimento: 100/200/300 ms; entrada desacelera, saída acelera (t²). Sem molas. */
object AureaMotion {
    const val FAST = 100
    const val NORMAL = 200
    const val SLOW = 300
    val Enter: Easing = CubicBezierEasing(0.0f, 0.0f, 0.58f, 1.0f)      // ≈ Curves.decelerate
    val Exit: Easing = CubicBezierEasing(1f / 3f, 0f, 2f / 3f, 1f / 3f)  // t²

    // Tocavel: escala 0,965 e opacidade 0,82 enquanto pressionado.
    const val PRESS_SCALE = 0.965f
    const val PRESS_ALPHA = 0.82f
    const val PRESS_DOWN_MS = 90
    const val PRESS_UP_MS = 220
    const val PRESS_ALPHA_DOWN_MS = 60
    const val PRESS_ALPHA_UP_MS = 180
}

/** Elevação: as sombras do Material e os vidros das barras. */
object AureaElevation {
    val Card = 1.dp
    val Sheet = 8.dp
    val Raised = 16.dp

    /** Sigma do blur (no vocabulário do Flutter) de cada vidro. */
    val TabBarBlur = 24.dp
    val CompactBarBlur = 18.dp
    val BatchBarBlur = 20.dp

    /** Tinta e cor sólida de recuo de cada vidro. */
    fun tabBarTint() = AureaColors.Background.copy(alpha = 0.72f)
    fun tabBarFallback() = AureaColors.Background.copy(alpha = 0.97f)
    fun batchBarTint() = AureaColors.Surface.copy(alpha = 0.88f)
    fun batchBarFallback() = AureaColors.Surface
}

/**
 * Timeline da A.01 (`am_timeline.dart@aba36bb`), conferida no print t2
 * (`docs/migration/ui_spec/03_timeline.md` §1.B). Só a timeline usa.
 */
object AureaTimeline {
    val Background = Color(0xFF1A1C28)
    // --- Cores ---------------------------------------------------------------
    // Redesenho 2026-09-29 (mockup `docs/design/redesenho-2026-09-29/Editor.dc.html`).
    val TickMajor = Color(0xFF6E7A8C)      // risco de segundo (e rótulo)
    val TickMinor = Color(0xFF4C5566)      // risco de décimo / quadro
    val HeaderPill = Color(0xFF292D3D)     // pílula do olho + quadradinho
    val Swatch = Color(0xFFFFE899)         // quadradinho da camada sem etiqueta
    val SwatchGlyph = Color(0xFF0F141A)    // cadeado / visto dentro do quadradinho
    val KeyframeOn = Color(0xFFFFC107)     // losango escolhido (âmbar)
    val TrimHandle = Color(0xFFF2F5F9)     // alça de trim (dentro das pontas)

    // --- Fileiras (pílula à esquerda + clipe no tom do tipo) -------------------
    /** Glifo do tipo na pílula (tom muted). */
    val GutterIcon = Color(0xFF8B97A5)
    /** O olho da pílula. */
    val GutterEye = Color(0xFFF7F9FB)
    /** Contorno do clipe escolhido e as alças de aparar. */
    val ClipSelected = Color(0xFFFFFFFF)
    /** O "T" da camada de texto no quadradinho da pílula. */
    val GlyphText = Color(0xFFF7F9FB)
    /** Ponto da forma no quadradinho da pílula. */
    val GlyphShape = Color(0xFFE8A33D)
    /** Nota do áudio no quadradinho (o mesmo tom da onda). */
    val GlyphAudio = Color(0xFF9FD3B8)
    /** O ≡ da ponta do clipe (tinta escura sobre o tom do tipo). */
    val ClipGrip = Color(0x73000000)
    /** Divisas da tira de miniaturas. */
    val FilmDivider = Color(0x40000000)
    /** Clipes de camada oculta: a fileira inteira a 40 %. */
    const val HiddenAlpha = 0.4f

    val Amber = ClipTone(Color(0xFF7A4A24), Color(0xFFEF9F27), Color(0xFFF7E4D2), Color(0xFFEF9F27))
    val Teal = ClipTone(Color(0xFF245247), Color(0xFF9FD3B8), Color(0xFFD6F0E3), Color(0xFF9FD3B8))
    val Purple = ClipTone(Color(0xFF5E3470), Color(0xFFC18BD6), Color(0xFFF0E2F6), Color(0xFF9D95F0))
    val Gray = ClipTone(Color(0xFF3A4456), Color(0xFF8B97A5), Color(0xFFE1E6EE), Color(0xFF8B97A5))
    val Blue = ClipTone(Color(0xFF3B4E7A), Color(0xFF7F9AD6), Color(0xFFE8EEF8), Color(0xFF7F9AD6))
    val Green = ClipTone(Color(0xFF4A7C3F), Color(0xFF8FC77F), Color(0xFFE4F0DF), Color(0xFFC9E8BF))
    val Brown = ClipTone(Color(0xFF7A5A2B), Color(0xFFE8A33D), Color(0xFFF7ECD9), Color(0xFFE8A33D))

    /** Tom do clipe por tipo (mockup): texto azul, forma marrom, imagem/vídeo verde (tira de miniaturas), áudio verde-água, 3D âmbar, partículas roxo, o resto cinza. */
    fun tone(type: LayerType): ClipTone = when (type) {
        LayerType.Model3D -> Amber
        LayerType.Audio -> Teal
        LayerType.Particles -> Purple
        LayerType.Video, LayerType.Image -> Green
        LayerType.Text -> Blue
        LayerType.Shape -> Brown
        LayerType.Adjustment, LayerType.Camera, LayerType.Light, LayerType.Null, LayerType.Group -> Gray
    }

    // --- Medidas (dp) --------------------------------------------------------
    val RulerTicks = 14.dp                 // faixa dos riscos (fortes 12, finos 6)
    val RulerGap = 30.dp                   // relógio + respiro até a primeira barra em 44
    val Row = 32.dp                        // pílula 28 + 4 de vão (antes 36: camadas grandes demais)
    val Bar = 26.dp                        // barra do clipe
    val BarRadius = 0.dp
    val KeyframeTrack = 11.dp              // faixa de baixo da barra, dos losangos
    val HeaderColumn = 70.dp               // a pílula colada à esquerda (as barras passam por baixo)
    val RowPillHeight = 28.dp
    val RowPillInset = 2.dp                // a pílula começa 2 dp acima da barra
    val GutterIconSize = 16.dp             // o olho da pílula
    val GutterEyeSize = 20.dp
    val GlyphBox = 20.dp
    val ClipStripe = 3.dp                  // faixa da cor da etiqueta na borda esquerda
    val ClipSelStroke = 2.dp
    val ClipIcon = 12.dp
    val Playhead = 1.dp
    val PlayheadKnob = 8.dp

    // --- Omega 0.3.2 (área 03): papéis da timeline ------------------------------
    // Valores medidos na referência (build/reference/am-omega-20261026/areas/03-timeline.md
    // §4, "Papéis de cor"); os do tema vêm da paleta. Desenho e nomes são do AUREA.
    /** Pílula do cabeçalho da linha (fora da seleção múltipla). */
    val OmegaHeader: Color get() = AureaColors.palette.timelineHeader
    /** Fundo da aba do tempo atual na régua. */
    val OmegaTimeTab: Color get() = AureaColors.palette.timelineTimeTab
    /** Fundo da linha que está sendo reordenada. */
    val OmegaDragRow: Color get() = AureaColors.palette.timelineDragRow
    /** Traço de 0,5 dp da pílula do cabeçalho (branco 10 %). */
    val OmegaHeaderBorder = Color(0x1AFFFFFF)
    /** Tiques da régua (branco 15 %). */
    val OmegaTick = Color(0x26FFFFFF)
    /** As três barrinhas da alça ≡ da linha e a sombra delas. */
    val OmegaGrip = Color(0x4DFFFFFF)
    val OmegaGripShadow = Color(0xB31B1F32)
    /** Marcadores da régua, pela aparência (índice mod 4). */
    val Bookmarks = listOf(Color(0xFFF16586), Color(0xFFFAD3A5), Color(0xFF4BE29D), Color(0xFF3C8CE7))
    /** Fundo da linha da câmera principal (vermelho 10 %). */
    val MainCameraRow = Color(0x1AD13665)
    /**
     * 0.3.4 (bugs-034, cores): rótulo do clipe em branco sobre a cor sólida do tipo
     * (`ClipColors.solid`), com uma sombra curta preta a 50 %.
     */
    val ClipLabel = Color(0xFFFFFFFF)
    val ClipLabelShadow = Color(0x80000000)
    /** Realce no topo do cartão: brilho branco 14 % que some em [ClipSheenDepth] da altura, e fio branco 20 %. */
    val ClipSheen = Color(0x24FFFFFF)
    val ClipSheenLine = Color(0x33FFFFFF)
    const val ClipSheenDepth = 0.45f
    /** Forma de onda do clipe. */
    val ClipWave = Color(0xFFEAEAEA)
    /** Zebra de quadros (branco 16/255). */
    val ClipZebra = Color(0x10FFFFFF)
    /** Sombra leve do cartão e a sombra do clipe levantado. */
    val ClipShadow = Color(0x40000000)
    val ClipLiftShadow = Color(0x4D000000)
    /** Losango em edição: branco com traço preto; dos outros parâmetros: branco 160/255. */
    val KeyFill = Color(0xFFFFFFFF)
    val KeyStroke = Color(0xFF000000)
    val KeyAlt = Color(0xA0FFFFFF)
    val KeyAltStroke = Color(0xFFFFFFFF)
    /** Losango de baixo quando só parte dos parâmetros tem keyframe no instante. */
    val KeyPartial = Color(0xFF9D9FA5)
    /** Alças de corte e de velocidade do modo foco (o alfa varia com o gesto). */
    val TrimGrip = Color(0xFFFFFFFF)
}

/** Fundo escuro, faixa sólida, texto claro e forma de onda de um tipo de clipe. */
data class ClipTone(val body: Color, val stripe: Color, val text: Color, val wave: Color)
