// =============================================================================
//  Aurea / platform / ios / app / Theme.swift
//
//  As cores, a tipografia e as medidas da casca. Tudo vem do MESMO lugar que o
//  Android: `ui/theme/AureaTokens.kt` (cores, tipografia, espaço, raio),
//  `editor/ShellTokens.kt` (peças só da casca do editor) e `editor/EditorLayout.kt`
//  (as alturas das zonas).
//
//  Por que copiar os números em vez de "desenhar bonito": o Aurea já tem uma
//  identidade aprovada (Beta A.01), e o iOS é a MESMA experiência — quem abre os
//  dois aparelhos tem que reconhecer o app. Onde o iOS pede outra coisa (um
//  gesto, uma folha modal), a diferença é do sistema, não do desenho.
//
//  DOIS NOMES PARA O MESMO TOKEN: o Kotlin escreve `AureaColors.Accent` e o
//  Swift já escrevia `AureaColors.accent`. Os dois nomes existem aqui, apontando
//  para o mesmo valor — quem porta uma tela linha a linha do Kotlin não precisa
//  traduzir nome, e quem lê o resto da casca Swift continua em paz. Nunca um
//  valor novo: é sempre o mesmo `static let` com um apelido.
//
//  E NADA DE NÚMERO SOLTO: o que a tela usa vem daqui. Medida de DESENHO (dentro
//  de um `Canvas`) pode ser número — é geometria, não layout.
// =============================================================================
import SwiftUI
import CoreText

extension Font {
    /// Android's default UI font, bundled from the reference system with its license.
    static func aurea(size: CGFloat, weight: Font.Weight = .regular, design: Font.Design = .default) -> Font {
        if design == .monospaced { return .system(size: size, weight: weight, design: .monospaced) }
        return .custom("Roboto-Regular", fixedSize: size).weight(weight)
    }
}


/// Paleta de um tema (par do `AureaPalette` do Android, mesmos ids e valores):
/// só fundos, superfícies, marca e destaque mudam entre temas.
struct AureaPalette: Identifiable {
    let id: String
    let brandDeep: Color
    let brand: Color
    let accent: Color
    let keyframe: Color
    let background: Color
    let surface: Color
    let surfaceHigh: Color
    let chip: Color
    let chipHigh: Color
    let border: Color
    let muted: Color
    let subtle: Color
    let onAccent: Color
    let accentDim: Color
    let keyframeDim: Color
    let stage: Color
    let hairline: Color
    let editorTopBar: Color
    let editorPanel: Color
    let editorPanelHigh: Color
    let pill: Color
    let actionDim: Color
    let statusBarVeil: Color
    let systemBarVeil: Color
    let tickWeak: Color
    let tickStrong: Color
    let railModeFill: Color
    let fieldFilled: Color
    static let aurea = AureaPalette(id: "aurea", brandDeep: Color(hex: 0xFF123A63), brand: Color(hex: 0xFF245D8C), accent: Color(hex: 0xFF6FAED9), keyframe: Color(hex: 0xFFA9D3EC), background: Color(hex: 0xFF0F141A), surface: Color(hex: 0xFF151C24), surfaceHigh: Color(hex: 0xFF1B2530), chip: Color(hex: 0xFF212D3A), chipHigh: Color(hex: 0xFF323D49), border: Color(hex: 0xFF273442), muted: Color(hex: 0xFFAAB6C3), subtle: Color(hex: 0xFF7C8A99), onAccent: Color(hex: 0xFF0B1117), accentDim: Color(hex: 0xFF1D3A55), keyframeDim: Color(hex: 0xFF22405A), stage: Color(hex: 0xFF0A0E13), hairline: Color(hex: 0xB8273442), editorTopBar: Color(hex: 0xFF0F141A), editorPanel: Color(hex: 0xFF0F141A), editorPanelHigh: Color(hex: 0xFF151C24), pill: Color(hex: 0xFF1B2530), actionDim: Color(hex: 0xFF16304A), statusBarVeil: Color(hex: 0xFF070A0E), systemBarVeil: Color(hex: 0xFF0B0F13), tickWeak: Color(hex: 0xFF43516A), tickStrong: Color(hex: 0xFF7485A3), railModeFill: Color(hex: 0xFF1E222D), fieldFilled: Color(hex: 0xFF272B33))
    static let midnight = AureaPalette(id: "midnight", brandDeep: Color(hex: 0xFF15325A), brand: Color(hex: 0xFF2C66A0), accent: Color(hex: 0xFF7DB8E6), keyframe: Color(hex: 0xFFB3D9F0), background: Color(hex: 0xFF000000), surface: Color(hex: 0xFF0B0D10), surfaceHigh: Color(hex: 0xFF14171C), chip: Color(hex: 0xFF1A1E24), chipHigh: Color(hex: 0xFF2A2F37), border: Color(hex: 0xFF1E232A), muted: Color(hex: 0xFFA7B0BA), subtle: Color(hex: 0xFF77818C), onAccent: Color(hex: 0xFF05080B), accentDim: Color(hex: 0xFF15283C), keyframeDim: Color(hex: 0xFF1A3247), stage: Color(hex: 0xFF000000), hairline: Color(hex: 0xB81E232A), editorTopBar: Color(hex: 0xFF000000), editorPanel: Color(hex: 0xFF000000), editorPanelHigh: Color(hex: 0xFF0B0D10), pill: Color(hex: 0xFF14171C), actionDim: Color(hex: 0xFF102438), statusBarVeil: Color(hex: 0xFF000000), systemBarVeil: Color(hex: 0xFF000000), tickWeak: Color(hex: 0xFF3A4250), tickStrong: Color(hex: 0xFF6C788C), railModeFill: Color(hex: 0xFF15181E), fieldFilled: Color(hex: 0xFF1A1D22))
    static let graphite = AureaPalette(id: "graphite", brandDeep: Color(hex: 0xFF3A4250), brand: Color(hex: 0xFF566273), accent: Color(hex: 0xFFB7C4D3), keyframe: Color(hex: 0xFFD6DEE8), background: Color(hex: 0xFF16181C), surface: Color(hex: 0xFF1D2025), surfaceHigh: Color(hex: 0xFF25292F), chip: Color(hex: 0xFF2B3037), chipHigh: Color(hex: 0xFF3A4048), border: Color(hex: 0xFF33383F), muted: Color(hex: 0xFFB0B6BE), subtle: Color(hex: 0xFF838A94), onAccent: Color(hex: 0xFF101215), accentDim: Color(hex: 0xFF2E343C), keyframeDim: Color(hex: 0xFF343B45), stage: Color(hex: 0xFF111316), hairline: Color(hex: 0xB833383F), editorTopBar: Color(hex: 0xFF16181C), editorPanel: Color(hex: 0xFF16181C), editorPanelHigh: Color(hex: 0xFF1D2025), pill: Color(hex: 0xFF25292F), actionDim: Color(hex: 0xFF2A3038), statusBarVeil: Color(hex: 0xFF0D0E10), systemBarVeil: Color(hex: 0xFF121417), tickWeak: Color(hex: 0xFF4A515C), tickStrong: Color(hex: 0xFF7F8896), railModeFill: Color(hex: 0xFF23272D), fieldFilled: Color(hex: 0xFF2A2E34))
    static let emerald = AureaPalette(id: "emerald", brandDeep: Color(hex: 0xFF0F4A3A), brand: Color(hex: 0xFF1C7A5E), accent: Color(hex: 0xFF5BD6A8), keyframe: Color(hex: 0xFFA6ECD2), background: Color(hex: 0xFF0B1411), surface: Color(hex: 0xFF111D19), surfaceHigh: Color(hex: 0xFF172722), chip: Color(hex: 0xFF1D302A), chipHigh: Color(hex: 0xFF2C403A), border: Color(hex: 0xFF223A33), muted: Color(hex: 0xFFA6BDB5), subtle: Color(hex: 0xFF789088), onAccent: Color(hex: 0xFF06110D), accentDim: Color(hex: 0xFF163A30), keyframeDim: Color(hex: 0xFF1D4539), stage: Color(hex: 0xFF080F0C), hairline: Color(hex: 0xB8223A33), editorTopBar: Color(hex: 0xFF0B1411), editorPanel: Color(hex: 0xFF0B1411), editorPanelHigh: Color(hex: 0xFF111D19), pill: Color(hex: 0xFF172722), actionDim: Color(hex: 0xFF123326), statusBarVeil: Color(hex: 0xFF060B09), systemBarVeil: Color(hex: 0xFF09100D), tickWeak: Color(hex: 0xFF3B5249), tickStrong: Color(hex: 0xFF6C8C80), railModeFill: Color(hex: 0xFF18251F), fieldFilled: Color(hex: 0xFF1F2B27))
    static let amethyst = AureaPalette(id: "amethyst", brandDeep: Color(hex: 0xFF3A2470), brand: Color(hex: 0xFF6246B8), accent: Color(hex: 0xFFB9A2FF), keyframe: Color(hex: 0xFFDCCFFF), background: Color(hex: 0xFF110E19), surface: Color(hex: 0xFF181423), surfaceHigh: Color(hex: 0xFF201B2E), chip: Color(hex: 0xFF282238), chipHigh: Color(hex: 0xFF383049), border: Color(hex: 0xFF2F2842), muted: Color(hex: 0xFFB5ADC6), subtle: Color(hex: 0xFF877E99), onAccent: Color(hex: 0xFF0D0A14), accentDim: Color(hex: 0xFF2C2248), keyframeDim: Color(hex: 0xFF362B55), stage: Color(hex: 0xFF0C0A12), hairline: Color(hex: 0xB82F2842), editorTopBar: Color(hex: 0xFF110E19), editorPanel: Color(hex: 0xFF110E19), editorPanelHigh: Color(hex: 0xFF181423), pill: Color(hex: 0xFF201B2E), actionDim: Color(hex: 0xFF261E42), statusBarVeil: Color(hex: 0xFF08070C), systemBarVeil: Color(hex: 0xFF0D0B13), tickWeak: Color(hex: 0xFF4A4260), tickStrong: Color(hex: 0xFF7D7399), railModeFill: Color(hex: 0xFF211C2D), fieldFilled: Color(hex: 0xFF272233))
    static let sunset = AureaPalette(id: "sunset", brandDeep: Color(hex: 0xFF6A3212), brand: Color(hex: 0xFFB45A1E), accent: Color(hex: 0xFFFFB060), keyframe: Color(hex: 0xFFFFD6A8), background: Color(hex: 0xFF15100C), surface: Color(hex: 0xFF1E1712), surfaceHigh: Color(hex: 0xFF281F18), chip: Color(hex: 0xFF30251D), chipHigh: Color(hex: 0xFF40342B), border: Color(hex: 0xFF3A2C22), muted: Color(hex: 0xFFC4B4A6), subtle: Color(hex: 0xFF96867A), onAccent: Color(hex: 0xFF140C06), accentDim: Color(hex: 0xFF45280F), keyframeDim: Color(hex: 0xFF4E3218), stage: Color(hex: 0xFF100C09), hairline: Color(hex: 0xB83A2C22), editorTopBar: Color(hex: 0xFF15100C), editorPanel: Color(hex: 0xFF15100C), editorPanelHigh: Color(hex: 0xFF1E1712), pill: Color(hex: 0xFF281F18), actionDim: Color(hex: 0xFF3E230D), statusBarVeil: Color(hex: 0xFF0B0806), systemBarVeil: Color(hex: 0xFF110D0A), tickWeak: Color(hex: 0xFF5A4838), tickStrong: Color(hex: 0xFF907A66), railModeFill: Color(hex: 0xFF271E17), fieldFilled: Color(hex: 0xFF2E251E))
    static let all: [AureaPalette] = [aurea, midnight, graphite, emerald, amethyst, sunset]
    static func of(_ id: String?) -> AureaPalette { all.first { $0.id == id } ?? aurea }
}

/// O tema em uso. Trocar exige reconstruir as telas (o modelo muda `themeId`,
/// que é a identidade da raiz).
enum AureaTheme {
    static var palette: AureaPalette = .of(UserDefaults.standard.string(forKey: "aurea.theme"))
}

// =============================================================================
// Cores (AureaTokens.kt)
// =============================================================================
enum AureaColors {
    static let sheetGrab = Color(hex: 0xAAB6C3).opacity(0.5)
    static let menuScrim = Color.black.opacity(0.54)
    static let actionDivider = HomeColors.sheetDivider
    static let dialogBg = HomeColors.alertFill
    static let actionSheetText = HomeColors.sheetTitle
    static let actionSheetBg = HomeColors.sheetFill
    static let actionCancelBg = Color(hex: 0x2C2C2C)
    // --- Marca ---------------------------------------------------------------
    static var brandDeep: Color { AureaTheme.palette.brandDeep }
    static var brand: Color { AureaTheme.palette.brand }
    static var accent: Color { AureaTheme.palette.accent }
    static var keyframe: Color { AureaTheme.palette.keyframe }
    static var background: Color { AureaTheme.palette.background }
    static var surface: Color { AureaTheme.palette.surface }
    static var surfaceHigh: Color { AureaTheme.palette.surfaceHigh }
    static var chip: Color { AureaTheme.palette.chip }
    static var chipHigh: Color { AureaTheme.palette.chipHigh }
    static var border: Color { AureaTheme.palette.border }
    static let text        = Color(hex: 0xF7F9FB)
    static var muted: Color { AureaTheme.palette.muted }
    static var subtle: Color { AureaTheme.palette.subtle }
    static var onAccent: Color { AureaTheme.palette.onAccent }
    static var accentDim: Color { AureaTheme.palette.accentDim }
    static var keyframeDim: Color { AureaTheme.palette.keyframeDim }
    static let danger      = Color(hex: 0xFF6B6B)
    static let warning     = Color(hex: 0xFFC978)
    static let success     = Color(hex: 0x4CD08A)
    static var stage: Color { AureaTheme.palette.stage }
    static let playhead    = Color(hex: 0xFFFFFFFF)
    static var hairline: Color { AureaTheme.palette.hairline }

    // --- Cromo do editor (AmColors) -----------------------------------------
    static var editorTopBar: Color { AureaTheme.palette.editorTopBar }
    static var editorPanel: Color { AureaTheme.palette.editorPanel }
    static var editorPanelHigh: Color { AureaTheme.palette.editorPanelHigh }
    static var pill: Color { AureaTheme.palette.pill }
    static var action: Color { brand }
    static let onAction        = text
    static var actionDim: Color { AureaTheme.palette.actionDim }
    static var selection: Color { brandDeep }
    static var selectionText: Color { keyframe }
    static let disabled        = Color.white.opacity(0.25)
    static var statusBarVeil: Color { AureaTheme.palette.statusBarVeil }

    // --- Barra de sistema (a Home é edge-to-edge) ----------------------------
    static var systemBarVeil: Color { AureaTheme.palette.systemBarVeil }
    static let navigationBar = Color(hex: 0x000000)
    static let scrim         = Color(hex: 0x000000).opacity(0.54)
    static let sheetScrim    = Color(hex: 0x0A0E13).opacity(0.35)

    // --- Sobre imagem (cards com miniatura) ----------------------------------
    static let onImage   = Color(hex: 0xFFFFFFFF)
    static let onImage70 = Color(hex: 0xFFFFFFFF).opacity(0.70)
    static let imageScrim = Color(hex: 0x000000).opacity(0.70)

    // --- Aviso "beta" (Sobre) -------------------------------------------------
    static let beta       = Color(hex: 0xFFB020)
    static let betaFill   = Color(hex: 0xFFB020).opacity(0.13)
    static let betaBorder = Color(hex: 0xFFB020).opacity(0.33)

    // --- Realce de pressionar -------------------------------------------------
    static let pressHighlight = Color.white.opacity(0.05)
    static let rowHighlight   = Color.white.opacity(0.06)
    static let overlayPressed = Color.white.opacity(0.10)

    // --- Controles nativos (Cupertino) ---------------------------------------
    static let segmentSeparator   = Color(hex: 0x8E8E93).opacity(0.30)
    static let segmentThumbShadow = Color(hex: 0x000000).opacity(0.12)
    static var segmentTrack: Color { background }
    static var segmentThumb: Color { surfaceHigh }
    static let switchOffTrack     = Color(hex: 0x787880).opacity(0.32)

    // --- Campos ---------------------------------------------------------------
    static let field            = Color(hex: 0x000000)
    static let fieldBorder      = Color(hex: 0xFFFFFF).opacity(0.20)
    static let fieldPlaceholder = Color(hex: 0xEBEBF5).opacity(0.30)
    static var fieldFilled: Color { AureaTheme.palette.fieldFilled }
    static let fieldDialog      = Color(hex: 0x1C1C1E)

    // --- Régua (AmTickRuler) --------------------------------------------------
    static var tickWeak: Color { AureaTheme.palette.tickWeak }
    static var tickStrong: Color { AureaTheme.palette.tickStrong }

    // --- Diálogos Cupertino ----------------------------------------------------
    static let destructiveCupertino = Color(hex: 0xFF453A)
    /// O "=" com erro (`expressionColor` de `PropertyControls.kt`).
    static let expressionError = Color(hex: 0xFF6B5E)

    static var brandGradient: [Color] { [brandDeep, brand, accent] }

    // --- Painéis da A.01 (valores escritos à mão no Flutter, aqui viram token) ---
    static var railModeFill: Color { AureaTheme.palette.railModeFill }
    static let railDisabled        = Color(hex: 0x434956)
    static let controlButton       = Color(hex: 0x434A60)
    static let dialTrack           = Color(hex: 0x2E3548)
    static let dialValueBox        = Color(hex: 0x242436)
    static let curveGrid           = Color(hex: 0x34405A)
    static let curvePresetBorder   = Color(hex: 0x333B4F)
    static let effectPreviewTop    = Color(hex: 0x232B3A)
    static let effectPreviewBottom = Color(hex: 0x606F98)
    static let effectPreviewDisc   = Color(hex: 0xFF4D2D)
    static let blendThumbBottom    = Color(hex: 0xFF8A3D)
    static let blendThumbTop       = Color(hex: 0x3D9BFF)

    // --- Timeline da A.01 (AureaTimeline.kt) ---------------------------------
    static let tickMajor  = Color(hex: 0x8A97AD)
    static let tickMinor  = Color(hex: 0x5A6880)
    static let keyframeOn = Color(hex: 0xFFC107)
    static let trimHandle = Color(hex: 0xF2F5F9)
    /// Trilho da barra de tempo (branco 22 %, `ShellColors.Track22`).
    static let track = Color.white.opacity(0.22)

    /// Paleta das etiquetas de camada (`ShellColors.LabelPalette`).
    static let labelPalette: [Color] = [
        Color(hex: 0xE85B81), Color(hex: 0xFFB020), accent, Color(hex: 0x2BE3A0),
        Color(hex: 0x35C4E7), keyframe, Color(hex: 0x3D7BFF), Color(hex: 0xFF7A3D),
        Color(hex: 0xFF4D5E), Color(hex: 0xFFE14D), Color(hex: 0xB0B8C4), Color(hex: 0x4A5160),
    ]

    // --- Espelho PascalCase dos nomes do Kotlin (o mesmo token, dois nomes) ---
    static var BrandDeep: Color { brandDeep }
    static var Brand: Color { brand }
    static var Accent: Color { accent }
    static var Keyframe: Color { keyframe }
    static var Background: Color { background }
    static var Surface: Color { surface }
    static var SurfaceHigh: Color { surfaceHigh }
    static let Text = text
    static var Chip: Color { chip }
    static var ChipHigh: Color { chipHigh }
    static var Border: Color { border }
    static var Muted: Color { muted }
    static var Subtle: Color { subtle }
    static var OnAccent: Color { onAccent }
    static var AccentDim: Color { accentDim }
    static let Danger = danger, Warning = warning, Success = success
    static var KeyframeDim: Color { keyframeDim }
    static let Playhead = playhead
    static var Stage: Color { stage }
    static var Hairline: Color { hairline }
    static var EditorTopBar: Color { editorTopBar }
    static var EditorPanel: Color { editorPanel }
    static var EditorPanelHigh: Color { editorPanelHigh }
    static let OnAction = onAction
    static var Pill: Color { pill }
    static var Action: Color { action }
    static var ActionDim: Color { actionDim }
    static let Disabled = disabled
    static var Selection: Color { selection }
    static var SelectionText: Color { selectionText }
    static let NavigationBar = navigationBar
    static var StatusBarVeil: Color { statusBarVeil }
    static var SystemBarVeil: Color { systemBarVeil }
    static let Scrim = scrim, SheetScrim = sheetScrim
    static let OnImage = onImage, OnImage70 = onImage70, ImageScrim = imageScrim
    static let Beta = beta, BetaFill = betaFill, BetaBorder = betaBorder
    static let PressHighlight = pressHighlight, RowHighlight = rowHighlight, OverlayPressed = overlayPressed
    static let SegmentSeparator = segmentSeparator, SegmentThumbShadow = segmentThumbShadow
    static let SwitchOffTrack = switchOffTrack
    static var SegmentTrack: Color { segmentTrack }
    static var SegmentThumb: Color { segmentThumb }
    static let Field = field, FieldBorder = fieldBorder, FieldPlaceholder = fieldPlaceholder
    static let FieldDialog = fieldDialog
    static var FieldFilled: Color { fieldFilled }
    static var TickWeak: Color { tickWeak }
    static var TickStrong: Color { tickStrong }
    static let DestructiveCupertino = destructiveCupertino
    static var BrandGradient: [Color] { brandGradient }
    static let RailDisabled = railDisabled, ControlButton = controlButton
    static var RailModeFill: Color { railModeFill }
    static let DialTrack = dialTrack, DialValueBox = dialValueBox, CurveGrid = curveGrid
    static let CurvePresetBorder = curvePresetBorder
    static let EffectPreviewTop = effectPreviewTop, EffectPreviewBottom = effectPreviewBottom
    static let EffectPreviewDisc = effectPreviewDisc
    static let BlendThumbBottom = blendThumbBottom, BlendThumbTop = blendThumbTop
    static let TickMajor = tickMajor, TickMinor = tickMinor, KeyframeOn = keyframeOn, TrimHandle = trimHandle
    static let Track = track, LabelPalette = labelPalette
}

extension Color {
    /// Tokens RGB de 6 digitos sao opacos; tokens ARGB de 8 digitos
    /// preservam o canal alfa explicito do Android.
    init(hex: UInt32) {
        let a = hex <= 0x00FF_FFFF ? 1.0 : Double((hex >> 24) & 0xFF) / 255.0
        let r = Double((hex >> 16) & 0xFF) / 255.0
        let g = Double((hex >> 8) & 0xFF) / 255.0
        let b = Double(hex & 0xFF) / 255.0
        self.init(.sRGB, red: r, green: g, blue: b, opacity: a)
    }
}

// =============================================================================
// Cores que só a casca do editor usa (ShellTokens.kt, `ShellColors`)
// =============================================================================
enum ShellColors {
    /// `CromoEditor.apagado`: o tempo da barra do projeto (branco 40 %).
    static let white40         = Color.white.opacity(0.40)
    static let dockRow         = Color(hex: 0x1E222D)
    static let dockTile        = Color(hex: 0x222634)
    static let dockTileContent = Color(hex: 0xD4D8E2)
    static let badgeNew        = Color(hex: 0xFFD600)
    static let fab             = Color(hex: 0x1E2130)
    static let fabShadow       = Color(hex: 0x000000).opacity(0.45)
    static let floatingDark    = Color(hex: 0x12151A).opacity(0.80)
    static let resolutionChip  = Color(hex: 0x171D25).opacity(0.80)
    static let outlineUnder    = Color(hex: 0x0A0E13).opacity(0.55)
    static let snapLine        = Color(hex: 0xFF6B6B).opacity(0.80)
    static let busyVeil        = Color(hex: 0x17191D).opacity(0.87)
    static let objectCard      = Color(hex: 0x0D0E12)
    static let shapeTile       = Color(hex: 0x000000)
    static let shapeFill       = Color(hex: 0x9E9E9E)
    static let pageDotOff      = Color(hex: 0x484E5C)
    static let camera3D        = Color(hex: 0x8BD5FF)
    static let text3D          = Color(hex: 0xFFD36B)
    static let phone3D         = Color(hex: 0xC9CDD4)
    static let track22         = Color.white.opacity(0.22)
    static let menuHandle      = Color(hex: 0xAAB6C3).opacity(0.40)
    static let sheetHandle     = Color(hex: 0xAAB6C3).opacity(0.50)
    static let menuScrim       = Color(hex: 0x000000).opacity(0.54)
    static let settingsScrim   = Color(hex: 0x000000).opacity(0.38)
    static let popupScrim      = Color(hex: 0x0A0E13).opacity(0.25)
    static let disabledMuted   = Color(hex: 0xAAB6C3).opacity(0.60)
    static let swatch          = Color.white.opacity(0.24)
    static let navigationBar   = Color(hex: 0x000000)
    static let accentHalf      = Color(hex: 0x6FAED9).opacity(0.50)
    static let labelPalette    = AureaColors.labelPalette

    static let White40 = white40, DockRow = dockRow, DockTile = dockTile
    static let DockTileContent = dockTileContent, BadgeNew = badgeNew, Fab = fab
    static let FabShadow = fabShadow, FloatingDark = floatingDark, ResolutionChip = resolutionChip
    static let OutlineUnder = outlineUnder, SnapLine = snapLine, BusyVeil = busyVeil
    static let ObjectCard = objectCard, ShapeTile = shapeTile, ShapeFill = shapeFill
    static let PageDotOff = pageDotOff, Camera3D = camera3D, Text3D = text3D, Phone3D = phone3D
    static let Track22 = track22, MenuHandle = menuHandle, SheetHandle = sheetHandle
    static let MenuScrim = menuScrim, SettingsScrim = settingsScrim, PopupScrim = popupScrim
    static let DisabledMuted = disabledMuted, Swatch = swatch, NavigationBar = navigationBar
    static let AccentHalf = accentHalf, LabelPalette = labelPalette
}

// =============================================================================
// Tipografia (AureaType.kt)
//
//  O Flutter do app antigo faz TODO texto herdar 15 sp, −0,1 de espaçamento e
//  altura de linha 1,35 (bodyMedium do M3). Sem essa base as alturas dos
//  componentes não batem com os prints.
//
//  `AureaType` guarda a FONTE (o que a casca já usava: `.font(AureaType.x)`).
//  `AureaTextSpec` guarda a fonte + cor + espaçamento + altura de linha, para
//  quem precisa do estilo inteiro de uma vez (`.aureaText(.Value)`).
// =============================================================================
enum AureaType {
    // A base de tudo (15 sp, −0,1, altura 1,35).
    static let base = AureaTextSpec.base

    // --- Escala (nomes do Kotlin) --------------------------------------------
    static let Display       = AureaTextSpec.display.font
    static let HeadlineLarge = AureaTextSpec.headlineLarge.font
    static let ScreenTitle   = AureaTextSpec.screenTitle.font
    static let TitleLarge    = AureaTextSpec.titleLarge.font
    static let TitleMedium   = AureaTextSpec.titleMedium.font
    static let TitleSmall    = AureaTextSpec.titleSmall.font
    static let BodyLarge     = AureaTextSpec.bodyLarge.font
    static let BodySmall     = AureaTextSpec.bodySmall.font
    static let LabelLarge    = AureaTextSpec.labelLarge.font

    // --- Home / telas fora do editor -----------------------------------------
    static let Greeting        = AureaTextSpec.greeting.font
    static let TabLabel        = AureaTextSpec.tabLabel.font
    static let Button          = AureaTextSpec.button.font
    static let ShortcutLabel   = AureaTextSpec.shortcutLabel.font
    static let HeroKicker      = AureaTextSpec.heroKicker.font
    static let HeroTitle       = AureaTextSpec.heroTitle.font
    static let HeroSpec        = AureaTextSpec.heroSpec.font
    static let HeroPill        = AureaTextSpec.heroPill.font
    static let ListCount       = AureaTextSpec.listCount.font
    static let CardTitle       = AureaTextSpec.cardTitle.font
    static let CardSpec        = AureaTextSpec.cardSpec.font
    static let LinkRow         = AureaTextSpec.linkRow.font
    static let FeatureTitle    = AureaTextSpec.featureTitle.font
    static let FeatureSubtitle = AureaTextSpec.featureSubtitle.font
    static let Empty           = AureaTextSpec.empty.font
    static let BatchCount      = AureaTextSpec.batchCount.font
    static let BatchAction     = AureaTextSpec.batchAction.font
    static let BatchDanger     = AureaTextSpec.batchDanger.font
    static let SearchText      = AureaTextSpec.searchText.font
    static let SearchPlaceholder = AureaTextSpec.searchPlaceholder.font
    static let Note            = AureaTextSpec.note.font
    static let Footer          = AureaTextSpec.footer.font
    static let Pill            = AureaTextSpec.pill.font
    static let ChipLabel       = AureaTextSpec.chipLabel.font
    static let Segment         = AureaTextSpec.segment.font
    static let DialogField     = AureaTextSpec.dialogField.font
    static let VersionPill     = AureaTextSpec.versionPill.font
    static let BetaTitle       = AureaTextSpec.betaTitle.font
    static let BetaBody        = AureaTextSpec.betaBody.font

    // --- Folha "Novo projeto" -------------------------------------------------
    static let SheetSpec       = AureaTextSpec.sheetSpec.font
    static let FrameLabel      = AureaTextSpec.frameLabel.font
    static let FrameHint       = AureaTextSpec.frameHint.font
    static let FormatLabel     = AureaTextSpec.formatLabel.font
    static let FormatHint      = AureaTextSpec.formatHint.font
    static let Caps            = AureaTextSpec.caps.font
    static let NameField       = AureaTextSpec.nameField.font
    static let NamePlaceholder = AureaTextSpec.namePlaceholder.font
    static let DimLabel        = AureaTextSpec.dimLabel.font
    static let DimField        = AureaTextSpec.dimField.font
    static let Times           = AureaTextSpec.times.font

    // --- Editor (AureaEstilos) ------------------------------------------------
    static let EditorTitle = AureaTextSpec.editorTitle.font
    static let Property    = AureaTextSpec.property.font
    static let Value       = AureaTextSpec.value.font
    static let Label       = AureaTextSpec.labelStyle.font
    static let Section     = AureaTextSpec.section.font
    static let Body        = AureaTextSpec.bodyStyle.font

    // --- Apelidos em camelCase que a casca Swift já usava ---------------------
    static let title     = Font.aurea(size: 22, weight: .semibold, design: .rounded)
    static let section   = AureaTextSpec.editorSection.font
    static let body      = AureaTextSpec.bodyStyle.font
    static let label     = AureaTextSpec.rowLabel.font
    static let value     = Font.aurea(size: 13, weight: .medium, design: .monospaced)
    static let tiny      = Font.aurea(size: 11, weight: .medium)
    static let tabLabel  = AureaTextSpec.tabLabel.font

    /// Número tabular (tnum do Kotlin): algarismos de largura fixa.
    static func tabular(_ size: CGFloat = 14, weight: Font.Weight = .bold) -> Font {
        .aurea(size: size, weight: weight, design: .monospaced)
    }
}

/// Fonte + cor + espaçamento + altura de linha de um estilo do Kotlin.
struct AureaTextSpec {
    let font: Font
    let size: CGFloat
    let color: Color
    let tracking: CGFloat
    let lineHeight: CGFloat

    /// Deriva um estilo da BASE (o `merge` que o Flutter herdava): espaçamento
    /// −0,1 e altura de linha 1,35 quando não dito.
    static func of(_ size: CGFloat, _ weight: Font.Weight = .regular,
                   tracking: CGFloat = -0.1, color: Color = AureaColors.text,
                   lineHeight: CGFloat = 1.35) -> AureaTextSpec {
        AureaTextSpec(font: .aurea(size: size, weight: weight), size: size,
                      color: color, tracking: tracking, lineHeight: lineHeight)
    }

    // --- Escala ---------------------------------------------------------------
    static let base          = AureaTextSpec.of(15)
    static let display       = AureaTextSpec.of(30, .heavy, tracking: -0.8, lineHeight: 1.05)
    static let headlineLarge = AureaTextSpec.of(34, .bold, tracking: -0.8, lineHeight: 1.1)
    static let screenTitle   = AureaTextSpec.of(21, .bold, tracking: -0.4)
    static let titleLarge    = AureaTextSpec.of(22, .bold, tracking: -0.5, lineHeight: 1.27)
    static let titleMedium   = AureaTextSpec.of(17, .semibold, tracking: -0.3, lineHeight: 1.5)
    static let titleSmall    = AureaTextSpec.of(17, .bold, tracking: -0.4)
    static let bodyLarge     = AureaTextSpec.of(17, tracking: -0.2, lineHeight: 1.5)
    static let bodySmall     = AureaTextSpec.of(13, tracking: 0, color: AureaColors.muted, lineHeight: 1.33)
    static let labelLarge    = AureaTextSpec.of(17, .semibold, tracking: -0.2, lineHeight: 1.43)

    // --- Home / telas fora do editor -----------------------------------------
    static let greeting        = AureaTextSpec.of(13.5, color: AureaColors.muted)
    static let tabLabel        = AureaTextSpec.of(10.5, .medium, tracking: 0.1)
    static let button          = AureaTextSpec.of(17, .semibold, tracking: -0.2, color: AureaColors.onAccent)
    static let shortcutLabel   = AureaTextSpec.of(12, .semibold)
    static let heroKicker      = AureaTextSpec.of(11, .bold, tracking: 0.4, color: AureaColors.accent)
    static let heroTitle       = AureaTextSpec.of(18, .bold, tracking: -0.2, color: AureaColors.onImage)
    static let heroSpec        = AureaTextSpec.of(11, color: AureaColors.onImage70)
    static let heroPill        = AureaTextSpec.of(12.5, .bold, color: AureaColors.onAccent)
    static let listCount       = AureaTextSpec.of(15, .bold)
    static let cardTitle       = AureaTextSpec.of(14, .semibold, tracking: -0.1)
    static let cardSpec        = AureaTextSpec.of(11, color: AureaColors.muted)
    static let linkRow         = AureaTextSpec.of(14.5)
    static let featureTitle    = AureaTextSpec.of(15, .semibold)
    static let featureSubtitle = AureaTextSpec.of(12.5, color: AureaColors.muted)
    static let empty           = AureaTextSpec.of(13.5, color: AureaColors.muted)
    static let batchCount      = AureaTextSpec.of(13, color: AureaColors.muted)
    static let batchAction     = AureaTextSpec.of(13)
    static let batchDanger     = AureaTextSpec.of(13, color: AureaColors.danger)
    static let searchText      = AureaTextSpec.of(17)
    static let searchPlaceholder = AureaTextSpec.of(17, color: AureaColors.fieldPlaceholder)
    static let note            = AureaTextSpec.of(12.5, color: AureaColors.muted)
    static let footer          = AureaTextSpec.of(11, color: AureaColors.subtle)
    static let pill            = AureaTextSpec.of(12, .semibold, color: AureaColors.accent)
    static let chipLabel       = AureaTextSpec.of(12, .semibold)
    static let segment         = AureaTextSpec.of(13, .semibold, tracking: -0.1)
    static let dialogField     = AureaTextSpec.of(15)
    static let versionPill     = AureaTextSpec.of(12, .semibold, color: AureaColors.accent)
    static let betaTitle       = AureaTextSpec.of(13, .bold, color: AureaColors.beta)
    static let betaBody        = AureaTextSpec.of(11, color: AureaColors.muted)

    // --- Folha "Novo projeto" -------------------------------------------------
    static let sheetSpec       = AureaTextSpec.of(12.5, color: AureaColors.muted)
    static let frameLabel      = AureaTextSpec.of(22, .bold, tracking: -0.3)
    static let frameHint       = AureaTextSpec.of(12, color: AureaColors.muted)
    static let formatLabel     = AureaTextSpec.of(12.5, .semibold, tracking: -0.1)
    static let formatHint      = AureaTextSpec.of(10, color: AureaColors.muted)
    static let caps            = AureaTextSpec.of(12, .medium, tracking: 0.6, color: AureaColors.muted)
    static let nameField       = AureaTextSpec.of(17, tracking: -0.2)
    static let namePlaceholder = AureaTextSpec.of(17, tracking: -0.2, color: AureaColors.muted)
    static let dimLabel        = AureaTextSpec.of(11, color: AureaColors.muted)
    static let dimField        = AureaTextSpec.of(16)
    static let times           = AureaTextSpec.of(16)

    // --- Editor (AureaEstilos) ------------------------------------------------
    static let editorTitle = AureaTextSpec.of(14, .semibold)
    static let property    = AureaTextSpec.of(12.5, color: AureaColors.muted)
    static let value       = AureaTextSpec(font: .aurea(size: 14, weight: .bold).monospacedDigit(),
                                           size: 14, color: AureaColors.keyframe,
                                           tracking: 0, lineHeight: 1.35)
    static let labelStyle  = AureaTextSpec.of(10, color: AureaColors.muted)
    static let section     = AureaTextSpec.of(11, .semibold, tracking: 0.3, color: AureaColors.muted)
    static let bodyStyle   = AureaTextSpec.of(13)

    // --- Peças da casca -------------------------------------------------------
    /// Nome do parâmetro de uma linha de propriedade (12 sp w600 muted).
    static let rowLabel = AureaTextSpec.of(12, .semibold, color: AureaColors.muted)
    /// Título de seção de painel (13 sp w700 muted).
    static let editorSection = AureaTextSpec.of(13, .bold, color: AureaColors.muted)
}

extension View {
    /// Aplica um estilo do catálogo (fonte, cor, espaçamento e altura de linha).
    func aureaText(_ spec: AureaTextSpec) -> some View {
        self.font(spec.font)
            .foregroundStyle(spec.color)
            .tracking(spec.tracking)
            .lineSpacing(max(0, spec.lineHeight * spec.size - (UIFont(name: "Roboto-Regular", size: spec.size)?.lineHeight ?? spec.size)))
    }
    /// Same typography metrics without overriding a selected/disabled color.
    func aureaFont(_ spec: AureaTextSpec) -> some View {
        let fontHeight = UIFont(name: "Roboto-Regular", size: spec.size)?.lineHeight ?? spec.size
        let extra = max(0, spec.lineHeight * spec.size - fontHeight)
        return self.font(spec.font).tracking(spec.tracking).lineSpacing(extra).padding(.vertical, extra / 2)
    }
}

// =============================================================================
// Medidas (AureaDims + ShellDims)
// =============================================================================
enum AureaDims {
    // --- Espaço ---------------------------------------------------------------
    static let s1: CGFloat = 4
    static let s2: CGFloat = 8
    static let s3: CGFloat = 12
    static let s4: CGFloat = 16
    static let s5: CGFloat = 24
    static let s6: CGFloat = 32
    static let minTap: CGFloat = 44
    /// Recuo lateral das telas fora do editor.
    static let gutter: CGFloat = 20
    static let roundTarget: CGFloat = 44
    static let roundCircle: CGFloat = 36
    static let listEndSpace: CGFloat = 120
    static let pad: CGFloat = 14

    // --- Raios ----------------------------------------------------------------
    static let radiusXs: CGFloat = 5
    static let radiusSm: CGFloat = 8
    static let radiusChip: CGFloat = 10
    static let radiusCard: CGFloat = 12
    static let radiusMd: CGFloat = 14
    static let radiusLg: CGFloat = 16
    static let radiusSheet: CGFloat = 18
    static let radiusXl: CGFloat = 20
    static let radiusClip: CGFloat = 8
    static let radiusPill: CGFloat = 999
    static let corner: CGFloat = 10

    // --- Ícones ---------------------------------------------------------------
    static let iconXs: CGFloat = 13
    static let iconSm: CGFloat = 16
    static let iconMd: CGFloat = 20
    static let iconLg: CGFloat = 24
    static let iconXl: CGFloat = 32

    // --- Controles ------------------------------------------------------------
    static let buttonHeight: CGFloat = 54
    static let controlRow: CGFloat = 54
    static let searchField: CGFloat = 40
    static let chipHeight: CGFloat = 34
    static let tabBarHeight: CGFloat = 54
    static let compactBarHeight: CGFloat = 52
    static let compactBarThreshold: CGFloat = 64
    static let switchWidth: CGFloat = 59
    static let switchHeight: CGFloat = 39

    // --- Casca do editor (ShellDims + EditorLayout) ---------------------------
    static let hairline: CGFloat = 1
    static let hairlineThin: CGFloat = 0.5
    static let selectionStroke: CGFloat = 2
    static let multiSelectionStroke: CGFloat = 1.5
    static let topBar: CGFloat = 44
    static let transport: CGFloat = 46
    static let strip: CGFloat = 8
    static let panelHeader: CGFloat = 44
    static let sheetHandle: CGFloat = 12
    static let fullscreenTimeBar: CGFloat = 44
    static let stageInset: CGFloat = 8
    static let fab: CGFloat = 52
    static let fabMargin: CGFloat = 18
    static let touchSlop: CGFloat = 18
    static let handleSlop: CGFloat = 4
    static let snapTolerance: CGFloat = 10
    static let hitSlack: CGFloat = 12
    static let scaleHandleTarget: CGFloat = 26
    static let rotateHandleTarget: CGFloat = 22

    // --- Régua de riscos (AmTickRuler) ----------------------------------------
    static let tickStep: CGFloat = 9
    static let tickFade: CGFloat = 24
    static let tickRulerHeight: CGFloat = 40
    static let tickRulerPad: CGFloat = 8

    // --- Linha de propriedade -------------------------------------------------
    static let propertyRowHeight: CGFloat = 48
    static let labelChipW: CGFloat = 94
    static let labelChipH: CGFloat = 32
    static let labelChipPadH: CGFloat = 6
    static let valueBoxH: CGFloat = 24
    static let valueBoxW: CGFloat = 60
    static let valueBoxRadius: CGFloat = 8
    static let diamondIcon: CGFloat = 22
    static let curveIcon: CGFloat = 20
    static let colorWell: CGFloat = 30
    static let colorWellRadius: CGFloat = 6

    // --- Interruptor e chips --------------------------------------------------
    static let toggleW: CGFloat = 51
    static let toggleH: CGFloat = 31
    static let toggleKnob: CGFloat = 27
    static let toggleTravel: CGFloat = 22
    static let togglePad: CGFloat = 2
    static let toggleRadius: CGFloat = 15.5
    static let chipRadius: CGFloat = 8
    static let chipPadH: CGFloat = 11
    static let chipPadV: CGFloat = 6

    // --- Cromo de painel ------------------------------------------------------
    static let railW: CGFloat = 46
    static let railModeW: CGFloat = 40
    static let railCell: CGFloat = 36
    static let railModeRadius: CGFloat = 8
    static let railModeStroke: CGFloat = 1.5
    static let railMoreDot: CGFloat = 6
    /// O ponto do `⋯` aceso, medido da referência (o `padding(start: 26,
    /// bottom: 16)` do Android sobre um alvo de 44).
    static let railMoreDotX: CGFloat = 13
    static let railMoreDotY: CGFloat = -9
    static let chromeButtonW: CGFloat = 40
    static let chromeButtonH: CGFloat = 44
    static let chromeGlyph: CGFloat = 21
    static let chromeGlyphSm: CGFloat = 19
    static let panelBackIcon: CGFloat = 26
    static let panelBackTarget: CGFloat = 48
    static let paramTabH: CGFloat = 48
    static let paramTabRadius: CGFloat = 9
    static let paramTabPadH: CGFloat = 8
    static let paramTabPadV: CGFloat = 6
    static let paramTabGap: CGFloat = 3
    static let paramTabDot: CGFloat = 5
    static let noticePadV: CGFloat = 10

    // --- Folhas, diálogos e menus ---------------------------------------------
    static let sheetRadius: CGFloat = 18
    static let adjustRadius: CGFloat = 13.5
    static let sheetGrabW: CGFloat = 36
    static let sheetGrabMenu: CGFloat = 5
    static let sheetGrabAdjust: CGFloat = 4
    static let sheetGrabRadius: CGFloat = 3
    static let actionSheetRadius: CGFloat = 14
    static let actionSheetMargin: CGFloat = 8
    static let actionRowMinH: CGFloat = 57
    static let dialogW: CGFloat = 270
    static let dialogRadius: CGFloat = 14
    static let dialogRowH: CGFloat = 45
    static let dialogDivider: CGFloat = 0.3
    static let dialogPadH: CGFloat = 16
    static let dialogPadTop: CGFloat = 19
    static let dialogPadBottom: CGFloat = 16
    static let nameFieldRadius: CGFloat = 7
    static let timeFieldRadius: CGFloat = 7
    static let menuRowMinH: CGFloat = 48
    static let menuIcon: CGFloat = 20
    static let menuIconGap: CGFloat = 14
    static let menuCheck: CGFloat = 18
    static let menuSectionPadH: CGFloat = 20
    static let menuSectionPadTop: CGFloat = 14
    static let menuSectionPadBottom: CGFloat = 4
    static let popupW: CGFloat = 250
    static let popupRowH: CGFloat = 40
    static let popupRadius: CGFloat = 8
    static let popupPadV: CGFloat = 4
    static let popupBarW: CGFloat = 4
    static let popupBarH: CGFloat = 24
    static let popupBarGap: CGFloat = 10
    static let popupMargin: CGFloat = 8

    // --- Teclado numérico e seletor de cor ------------------------------------
    static let keypadCellH: CGFloat = 48
    static let keypadRadius: CGFloat = 10
    static let keypadFont: CGFloat = 21
    static let keypadGap: CGFloat = 3
    static let keypadRowGap: CGFloat = 6
    static let keypadDisplayRadius: CGFloat = 12
    static let keypadDisplayFont: CGFloat = 26
    static let keypadDisplayPadH: CGFloat = 14
    static let keypadDisplayPadV: CGFloat = 12
    static let keypadHintH: CGFloat = 22
    static let keypadButtonPad: CGFloat = 12
    static let colorBoardH: CGFloat = 170
    static let colorStripH: CGFloat = 26
    static let colorStripTarget: CGFloat = 30
    static let colorStripHandle: CGFloat = 7
    static let colorStripStroke: CGFloat = 2.5
    static let colorSampleW: CGFloat = 76
    static let colorSampleH: CGFloat = 28
    static let colorCell: CGFloat = 30
    static let colorHexBoxW: CGFloat = 96
    static let colorAlphaBoxW: CGFloat = 52
    static let checkerCell: CGFloat = 6

    // --- Indicador de atividade -----------------------------------------------
    static let activitySize: CGFloat = 22

    // --- Espelho PascalCase (o mesmo valor, o nome do Kotlin) -----------------
    static let S1 = s1, S2 = s2, S3 = s3, S4 = s4, S5 = s5, S6 = s6
    static let MinTap = minTap, Gutter = gutter, RoundTarget = roundTarget, RoundCircle = roundCircle
    static let ListEndSpace = listEndSpace, Pad = pad
    static let RadiusXs = radiusXs, RadiusSm = radiusSm, RadiusChip = radiusChip
    static let RadiusCard = radiusCard, RadiusMd = radiusMd, RadiusLg = radiusLg
    static let RadiusSheet = radiusSheet, RadiusXl = radiusXl, RadiusClip = radiusClip
    static let RadiusPill = radiusPill
    static let IconXs = iconXs, IconSm = iconSm, IconMd = iconMd, IconLg = iconLg, IconXl = iconXl
    static let ButtonHeight = buttonHeight, ControlRow = controlRow, SearchField = searchField
    static let ChipHeight = chipHeight, TabBarHeight = tabBarHeight
    static let CompactBarHeight = compactBarHeight, CompactBarThreshold = compactBarThreshold
    static let SwitchWidth = switchWidth, SwitchHeight = switchHeight
    static let Hairline = hairline, HairlineThin = hairlineThin
    static let SelectionStroke = selectionStroke, MultiSelectionStroke = multiSelectionStroke
    static let EditorTopBar = topBar, EditorTransport = transport, PreviewResizeStrip = strip
    static let PanelHeader = panelHeader
    // ShellDims (Kotlin `ShellDims`)
    static let TopBar = topBar, Transport = transport, Strip = strip, SheetHandle = sheetHandle
    static let FullscreenTimeBar = fullscreenTimeBar, StageInset = stageInset
    static let Fab = fab, FabMargin = fabMargin, TouchSlop = touchSlop, HandleSlop = handleSlop
    static let SnapTolerance = snapTolerance, HitSlack = hitSlack
    static let ScaleHandleTarget = scaleHandleTarget, RotateHandleTarget = rotateHandleTarget
    // Régua e linha de propriedade
    static let TickStep = tickStep, TickFade = tickFade
    static let PropertyRowHeight = propertyRowHeight, LabelChipW = labelChipW, LabelChipH = labelChipH
    static let ValueBoxH = valueBoxH, ValueBoxW = valueBoxW
    // Cromo
    static let RailW = railW, RailModeW = railModeW, RailCell = railCell
    static let ChromeButtonW = chromeButtonW, ChromeButtonH = chromeButtonH, ChromeGlyph = chromeGlyph
    static let ParamTabH = paramTabH
    // Folhas
    static let SheetRadius = sheetRadius, AdjustRadius = adjustRadius, DialogW = dialogW
    static let DialogRadius = dialogRadius, DialogRowH = dialogRowH
    static let PopupW = popupW, PopupRowH = popupRowH, PopupRadius = popupRadius
    static let MenuRowMinH = menuRowMinH, MenuIcon = menuIcon
    // Teclado e cor
    static let KeypadCellH = keypadCellH, KeypadRadius = keypadRadius
    static let ColorBoardH = colorBoardH, ColorStripH = colorStripH
}

/// Medidas que só a casca do editor usa (espelho de `ShellDims`).
enum ShellDims {
    static let topBar: CGFloat = AureaDims.topBar
    static let transport: CGFloat = AureaDims.transport
    static let strip: CGFloat = AureaDims.strip
    static let sheetHandle: CGFloat = AureaDims.sheetHandle
    static let fullscreenTimeBar: CGFloat = AureaDims.fullscreenTimeBar
    static let stageInset: CGFloat = AureaDims.stageInset
    static let fab: CGFloat = AureaDims.fab
    static let fabMargin: CGFloat = AureaDims.fabMargin
    static let touchSlop: CGFloat = AureaDims.touchSlop
    static let handleSlop: CGFloat = AureaDims.handleSlop
    static let snapTolerance: CGFloat = AureaDims.snapTolerance
    static let hitSlack: CGFloat = AureaDims.hitSlack
    static let scaleHandleTarget: CGFloat = AureaDims.scaleHandleTarget
    static let rotateHandleTarget: CGFloat = AureaDims.rotateHandleTarget

    static let TopBar = topBar, Transport = transport, Strip = strip, SheetHandle = sheetHandle
    static let FullscreenTimeBar = fullscreenTimeBar, StageInset = stageInset
    static let Fab = fab, FabMargin = fabMargin, TouchSlop = touchSlop, HandleSlop = handleSlop
    static let SnapTolerance = snapTolerance, HitSlack = hitSlack
    static let ScaleHandleTarget = scaleHandleTarget, RotateHandleTarget = rotateHandleTarget
}

/// Raios prontos (AureaShape.kt).
enum AureaShape {
    static let Xs = RoundedRectangle(cornerRadius: AureaDims.radiusXs)
    static let Sm = RoundedRectangle(cornerRadius: AureaDims.radiusSm)
    static let Chip = RoundedRectangle(cornerRadius: AureaDims.radiusChip)
    static let Card = RoundedRectangle(cornerRadius: AureaDims.radiusCard)
    static let Md = RoundedRectangle(cornerRadius: AureaDims.radiusMd)
    static let Lg = RoundedRectangle(cornerRadius: AureaDims.radiusLg)
    static let Sheet = RoundedRectangle(cornerRadius: AureaDims.radiusSheet)
    static let Xl = RoundedRectangle(cornerRadius: AureaDims.radiusXl)
    static let Pill = RoundedRectangle(cornerRadius: AureaDims.radiusPill)
    static let Circle = SwiftUI.Circle()
}

/// Movimento: 100/200/300 ms; entrada desacelera, saída acelera (t²). Sem molas.
enum AureaMotion {
    static let fast: Double = 0.10
    static let normal: Double = 0.20
    static let slow: Double = 0.30
    static let FAST: Double = fast
    static let NORMAL: Double = normal
    static let SLOW: Double = slow

    /// Entrada ≈ Curves.decelerate; saída acelera (t²).
    static let enter = Animation.timingCurve(0.0, 0.0, 0.58, 1.0, duration: normal)
    static let exit = Animation.timingCurve(1.0 / 3.0, 0.0, 2.0 / 3.0, 1.0 / 3.0, duration: fast)
    static let Enter = enter
    static let Exit = exit

    /// Tocável: escala 0,965 e opacidade 0,82 enquanto pressionado.
    static let pressScale: CGFloat = 0.965
    static let pressAlpha: Double = 0.82
    static let PRESS_SCALE = pressScale
    static let PRESS_ALPHA = pressAlpha
}

/// Elevação: as sombras do Material e os vidros das barras.
enum AureaElevation {
    static let card: CGFloat = 1
    static let sheet: CGFloat = 8
    static let raised: CGFloat = 16
    static let Card = card, Sheet = sheet, Raised = raised

    static let tabBarBlur: CGFloat = 24
    static let compactBarBlur: CGFloat = 18
    static let batchBarBlur: CGFloat = 20

    static var tabBarTint: Color { AureaColors.background.opacity(0.72) }
    static var tabBarFallback: Color { AureaColors.background.opacity(0.97) }
    static var batchBarTint: Color { AureaColors.surface.opacity(0.88) }
    static var batchBarFallback: Color { AureaColors.surface }
}

/// Timeline da A.01 (`am_timeline.dart@aba36bb`). Só a timeline usa.
enum AureaTimeline {
    // --- Cores ---------------------------------------------------------------
    static let tickMajor  = Color(hex: 0x8A97AD)
    static let tickMinor  = Color(hex: 0x5A6880)
    static let headerPill = Color(hex: 0x1E222D)
    static let swatch     = Color(hex: 0xFFE899)
    static let swatchGlyph = Color(hex: 0x0F141A)
    static let keyframeOn = Color(hex: 0xFFC107)
    static let trimHandle = Color(hex: 0xF2F5F9)

    static let TickMajor = tickMajor, TickMinor = tickMinor, HeaderPill = headerPill
    static let Swatch = swatch, SwatchGlyph = swatchGlyph, KeyframeOn = keyframeOn
    static let TrimHandle = trimHandle

    // --- Medidas (dp) --------------------------------------------------------
    static let rulerTicks: CGFloat = 20
    static let rulerGap: CGFloat = 18
    static let row: CGFloat = 36
    static let bar: CGFloat = 30
    static let barRadius: CGFloat = 8
    static let barMinWidth: CGFloat = 40
    static let keyframeTrack: CGFloat = 11
    static let headerColumn: CGFloat = 66
    static let pillWidth: CGFloat = 58
    static let pillHeight: CGFloat = 24
    static let playhead: CGFloat = 1.6
    static let playheadKnob: CGFloat = 8

    static let RulerTicks = rulerTicks, RulerGap = rulerGap, Row = row, Bar = bar
    static let BarRadius = barRadius, BarMinWidth = barMinWidth, KeyframeTrack = keyframeTrack
    static let HeaderColumn = headerColumn, PillWidth = pillWidth, PillHeight = pillHeight
    static let Playhead = playhead, PlayheadKnob = playheadKnob
}

// =============================================================================
// As alturas das zonas do editor — porte literal de `EditorLayout` do Android.
//
//  A regra que a casca A.01 cobrava: abrir painel, adicionar ou trocar de aba
//  NUNCA move o preview. O painel tira espaço da TIMELINE, e a timeline nunca
//  fica abaixo do piso. No iOS isso também é o que mantém o CAMetalLayer com o
//  mesmo tamanho durante um arrasto (redimensionar o drawable a cada gesto
//  custaria uma recriação de swapchain).
// =============================================================================
enum SheetContent { case none, hint, dock, panel, curve, batch, adding }

struct EditorMetrics {
    var topBar: CGFloat
    var preview: CGFloat
    var strip: CGFloat
    var transport: CGFloat
    var timeline: CGFloat
    var sheet: CGFloat
}

enum EditorLayout {
    static let topBar: CGFloat = 44
    static let transport: CGFloat = 46
    static let strip: CGFloat = 8
    static let timelineMin: CGFloat = 110
    static let previewMin: CGFloat = 96
    private static let previewFractionMax: CGFloat = 0.50
    private static let dockFraction: CGFloat = 0.40
    private static let panelFraction: CGFloat = 0.46
    private static let addBody: CGFloat = 280
    private static let sheetHandle: CGFloat = 12
    private static let batchBody: CGFloat = 124
    private static let hintBody: CGFloat = 30

    static func workspace(_ totalHeight: CGFloat) -> CGFloat {
        max(0, totalHeight - topBar - transport - strip)
    }

    static func solve(total: CGFloat, content: SheetContent, fullscreen: Bool) -> EditorMetrics {
        if fullscreen {
            return EditorMetrics(topBar: 0, preview: max(0, total - transport), strip: 0,
                                 transport: transport, timeline: 0, sheet: 0)
        }
        let ws = workspace(total)
        var preview = (total * 0.54)
            .clamped(to: previewMin...(max(previewMin, ws - timelineMin)))

        var sheetFraction: CGFloat = 0
        switch content {
        case .none: sheetFraction = 0
        case .hint: sheetFraction = ws > 0 ? (sheetHandle + hintBody) / ws : 0
        case .batch: sheetFraction = ws > 0 ? (sheetHandle + batchBody) / ws : 0
        case .dock: sheetFraction = dockFraction
        case .panel: sheetFraction = panelFraction
        case .curve: sheetFraction = ws > 0 ? 280 / ws : 0
        case .adding: sheetFraction = ws > 0 ? (sheetHandle + addBody) / ws : 0
        }
        var sheet = content == .none ? 0 : ws * min(max(sheetFraction, 0), 0.60)

        let floor: CGFloat
        switch content {
        case .adding, .dock, .panel, .curve, .batch: floor = 90
        default: floor = 120
        }
        // Make room for editing controls instead of compressing their targets.
        let requiredSheet: CGFloat = content == .panel ? 336 : (content == .dock ? 240 : 0)
        sheet = min(max(sheet, requiredSheet), max(0, ws - previewMin - floor))
        preview = min(preview, max(previewMin, ws - sheet - floor))
        var timeline = ws - preview - sheet
        if timeline < floor {
            sheet = max(0, sheet - (floor - timeline))
            timeline = ws - preview - sheet
        }
        return EditorMetrics(topBar: topBar, preview: preview, strip: strip,
                             transport: transport, timeline: max(0, timeline), sheet: max(0, sheet))
    }

    /// Layout largo: largura ≥ 600 em paisagem, ou ≥ 900 (o mesmo do Android).
    static func isWide(_ width: CGFloat, _ height: CGFloat) -> Bool {
        (width >= 600 && width > height) || width >= 900
    }

    static func wideTimeline(_ totalHeight: CGFloat) -> CGFloat {
        min(max((totalHeight - topBar - transport - strip) * 0.34, 88), 280)
    }

    /// A largura da folha no layout largo (`wideSheetWidth`): 40 %, presa a 280–380.
    static func wideSheetWidth(_ width: CGFloat) -> CGFloat {
        min(max(width * 0.4, 280), 380)
    }
}

/// Os mesmos números com os nomes do Kotlin (`EditorLayoutMetrics`).
enum EditorLayoutMetrics {
    static let topBar: CGFloat = EditorLayout.topBar
    static let transport: CGFloat = EditorLayout.transport
    static let strip: CGFloat = EditorLayout.strip
    static let timelineMin: CGFloat = EditorLayout.timelineMin
    static let previewMin: CGFloat = EditorLayout.previewMin
    static let TOP_BAR = topBar, TRANSPORT = transport, STRIP = strip
    static let TIMELINE_MIN = timelineMin, PREVIEW_MIN = previewMin

    static func workspace(_ totalHeight: CGFloat) -> CGFloat { EditorLayout.workspace(totalHeight) }
    static func solve(total: CGFloat, content: SheetContent, fullscreen: Bool) -> EditorMetrics {
        EditorLayout.solve(total: total, content: content, fullscreen: fullscreen)
    }
    static func isWide(_ width: CGFloat, _ height: CGFloat) -> Bool { EditorLayout.isWide(width, height) }
    static func wideTimeline(_ totalHeight: CGFloat) -> CGFloat { EditorLayout.wideTimeline(totalHeight) }
    static func wideSheetWidth(_ width: CGFloat) -> CGFloat { EditorLayout.wideSheetWidth(width) }
}

extension Comparable {
    func clamped(to range: ClosedRange<Self>) -> Self {
        min(max(self, range.lowerBound), range.upperBound)
    }
}

// =============================================================================
// Glifos Cupertino
//
//  O Android desenha cada ícone como um glifo da fonte `CupertinoIcons.ttf`
//  (MIT, pacote cupertino_icons 1.0.9 — licença em docs/licenses) numa caixa
//  N×N com `fontSize = N`. A MESMA fonte viaja para o iOS (o .ttf está em
//  `engine/platform/ios/app/CupertinoIcons.ttf`, registrado em `UIAppFonts` e
//  em `registerCupertinoFonts()`): o glifo é o mesmo desenho, no mesmo lugar.
//
//  Se o registro falhar (arquivo fora do bundle), o glifo sai vazio: é o sinal
//  honesto de que a fonte não chegou, em vez de um ícone parecido que não é o
//  aprovado.
// =============================================================================
enum CupertinoFont {
    /// O nome de família do .ttf (tabela `name`, id 1/4/6).
    static let family = "CupertinoIcons"
    static let fileName = "CupertinoIcons.ttf"

    private static var registered = false

    /// Registra a fonte uma vez. O `UIAppFonts` do Info.plist já a carrega no
    /// início do app; esta chamada cobre o caso de o arquivo ter entrado no
    /// bundle por outro caminho.
    static func register() {
        if registered { return }
        registered = true
        guard let url = Bundle.main.url(forResource: "CupertinoIcons", withExtension: "ttf") else { return }
        CTFontManagerRegisterFontsForURL(url as CFURL, .process, nil)
    }

    /// A fonte no tamanho pedido.
    static func font(_ size: CGFloat) -> Font {
        register()
        return .custom(family, fixedSize: size)
    }
}

/// Os codepoints usados pela UI (nomes do pacote cupertino_icons, os MESMOS do
/// `CupertinoGlyph`/`ShellGlyph` do Kotlin).
enum CupertinoGlyph {
    // --- Navegação e ações ----------------------------------------------------
    static let AddCircled: Character = "\u{F48A}"
    static let Arrow2Squarepath: Character = "\u{F4E6}"
    static let ArrowDownRightSquare: Character = "\u{F4F7}"
    static let ArrowDownToLine: Character = "\u{F4FB}"
    static let ArrowDownCircleFill: Character = "\u{F4EC}"
    static let ArrowLeftRight: Character = "\u{F500}"
    static let ArrowLeftRightSquare: Character = "\u{F503}"
    static let ArrowLeftToLine: Character = "\u{F507}"
    static let ArrowRightArrowLeft: Character = "\u{F50B}"
    static let ArrowRightToLine: Character = "\u{F514}"
    static let ArrowTurnLeftDown: Character = "\u{F519}"
    static let ArrowTurnUpRight: Character = "\u{F51E}"
    static let ArrowUpArrowDown: Character = "\u{F51F}"
    static let ArrowUpDownSquare: Character = "\u{F52D}"
    static let ArrowUpToLine: Character = "\u{F53D}"
    static let ArrowUpDoc: Character = "\u{F528}"
    static let ArrowUturnLeft: Character = "\u{F544}"
    static let ArrowUturnRight: Character = "\u{F549}"
    static let ArrowCounterclockwise: Character = "\u{F21C}"
    static let BackwardEnd: Character = "\u{F578}"
    static let BackwardEndAlt: Character = "\u{F579}"
    static let Bolt: Character = "\u{F593}"
    static let Book: Character = "\u{F3E7}"
    static let Bookmark: Character = "\u{F3E9}"
    static let Camera: Character = "\u{F3F5}"
    static let CameraFill: Character = "\u{F3F6}"
    static let CameraViewfinder: Character = "\u{F5B9}"
    static let CaptionsBubble: Character = "\u{F5BE}"
    static let CaptionsBubbleFill: Character = "\u{F5BF}"
    static let ChartBarAltFill: Character = "\u{F8B7}"
    static let CheckmarkAlt: Character = "\u{F8C1}"
    static let CheckmarkCircle: Character = "\u{F3FE}"
    static let CheckmarkCircleFill: Character = "\u{F3FF}"
    static let CheckmarkSeal: Character = "\u{F5CB}"
    static let CheckmarkSquare: Character = "\u{F5CF}"
    static let CheckmarkSquareFill: Character = "\u{F5D0}"
    static let ChevronDown: Character = "\u{F5D5}"
    static let ChevronLeft: Character = "\u{F3D2}"
    static let ChevronRight: Character = "\u{F3D3}"
    static let ChevronUp: Character = "\u{F5E5}"
    static let Circle: Character = "\u{F401}"
    static let CircleFill: Character = "\u{F400}"
    static let CircleLefthalfFill: Character = "\u{F5F0}"
    static let Clock: Character = "\u{F4BE}"
    static let CloudDownload: Character = "\u{F8C4}"
    static let ColorFilter: Character = "\u{F8C8}"
    static let Crop: Character = "\u{F618}"
    static let Cube: Character = "\u{F61A}"
    static let CubeBox: Character = "\u{F61B}"
    static let CubeBoxFill: Character = "\u{F61C}"
    static let CubeFill: Character = "\u{F61D}"
    static let Trash: Character = "\u{F4C4}"
    static let DeleteLeft: Character = "\u{F621}"
    static let DevicePhonePortrait: Character = "\u{F8CF}"
    static let DocOnClipboard: Character = "\u{F632}"
    static let DocOnDoc: Character = "\u{F634}"
    static let DocText: Character = "\u{F638}"
    static let DropFill: Character = "\u{F8D9}"
    static let Ellipsis: Character = "\u{F46A}"
    static let ExclamationmarkBubble: Character = "\u{F656}"
    static let ExclamationmarkTriangle: Character = "\u{F660}"
    static let Eye: Character = "\u{F424}"
    static let EyeFill: Character = "\u{F425}"
    static let EyeSlash: Character = "\u{F662}"
    static let Eyedropper: Character = "\u{F664}"
    static let Film: Character = "\u{F66B}"
    static let Flag: Character = "\u{F42C}"
    static let Folder: Character = "\u{F434}"
    static let FolderFill: Character = "\u{F435}"
    static let ForwardEnd: Character = "\u{F67F}"
    static let ForwardEndAlt: Character = "\u{F680}"
    static let Fullscreen: Character = "\u{F386}"
    static let FullscreenExit: Character = "\u{F37D}"
    static let Gear: Character = "\u{F43C}"
    static let GearAltFill: Character = "\u{F43D}"
    static let Grid: Character = "\u{F6A5}"
    static let House: Character = "\u{F447}"
    static let HouseFill: Character = "\u{F6CA}"
    static let InfoCircle: Character = "\u{F44C}"
    static let InfoCircleFill: Character = "\u{F6CF}"
    static let Lightbulb: Character = "\u{F6DD}"
    static let LineHorizontal3: Character = "\u{F6E1}"
    static let Link: Character = "\u{F6E5}"
    static let LinkCircleFill: Character = "\u{F6E7}"
    static let Lock: Character = "\u{F4C8}"
    static let LockFill: Character = "\u{F4C9}"
    static let LockOpen: Character = "\u{F6FA}"
    static let Minus: Character = "\u{F70F}"
    static let MinusCircle: Character = "\u{F463}"
    static let Move: Character = "\u{F8F8}"
    static let MusicNote: Character = "\u{F46B}"
    static let MusicNote2: Character = "\u{F46C}"
    static let Paintbrush: Character = "\u{F72E}"
    static let PauseFill: Character = "\u{F478}"
    static let Pencil: Character = "\u{F37E}"
    static let PencilOutline: Character = "\u{F73D}"
    static let PersonCropCircle: Character = "\u{F419}"
    static let Photo: Character = "\u{F767}"
    static let PhotoFill: Character = "\u{F768}"
    static let PhotoOnRectangle: Character = "\u{F76A}"
    static let Play: Character = "\u{F487}"
    static let PlayFill: Character = "\u{F488}"
    static let PlayRectangle: Character = "\u{F771}"
    static let Plus: Character = "\u{F489}"
    static let PlusSquare: Character = "\u{F77E}"
    static let PlusSquareOnSquare: Character = "\u{F781}"
    static let QuestionCircle: Character = "\u{F78F}"
    static let RectangleStack: Character = "\u{F3C9}"
    static let Repeat: Character = "\u{F7BF}"
    static let Rhombus: Character = "\u{F7C2}"
    static let RhombusFill: Character = "\u{F7C3}"
    static let Scissors: Character = "\u{F7C9}"
    static let Search: Character = "\u{F4A5}"
    static let SliderHorizontal3: Character = "\u{F7DC}"
    static let SmallcircleCircle: Character = "\u{F7DF}"
    static let Sparkles: Character = "\u{F7E8}"
    static let Speaker2: Character = "\u{F7EB}"
    static let SpeakerSlash: Character = "\u{F7EE}"
    static let Speedometer: Character = "\u{F7F5}"
    static let Square: Character = "\u{F7F8}"
    static let SquareArrowUp: Character = "\u{F4CA}"
    static let SquareGrid2x2: Character = "\u{F804}"
    static let SquareOnSquare: Character = "\u{F80D}"
    static let SquareStack3dDownRight: Character = "\u{F817}"
    static let SquareStack3dDownRightFill: Character = "\u{F818}"
    static let SquareStack3dUp: Character = "\u{F819}"
    static let SuitDiamond: Character = "\u{F831}"
    static let SuitDiamondFill: Character = "\u{F832}"
    static let Textformat: Character = "\u{F85C}"
    static let Timer: Character = "\u{F868}"
    static let Tv: Character = "\u{F881}"
    static let Videocam: Character = "\u{F4CC}"
    static let VideocamFill: Character = "\u{F4CD}"
    static let WandStars: Character = "\u{F892}"
    static let Waveform: Character = "\u{F894}"
    static let Wrench: Character = "\u{F8A0}"
    static let Xmark: Character = "\u{F404}"
    static let XmarkCircle: Character = "\u{F405}"
    static let XmarkCircleFill: Character = "\u{F36E}"

    // --- Painéis e controles de propriedade ----------------------------------
    static let ArrowtriangleDownFill: Character = "\u{F55D}"
    static let ArrowtriangleRightFill: Character = "\u{F569}"
    static let ArrowUp: Character = "\u{F366}"
    static let ArrowDown: Character = "\u{F35D}"
    static let ChevronBack: Character = "\u{F3CF}"
    static let Star: Character = "\u{F81F}"
    static let StarFill: Character = "\u{F822}"
    static let Scribble: Character = "\u{F7CB}"
    static let Tortoise: Character = "\u{F86A}"
    static let Hare: Character = "\u{F6B9}"

    // --- Glifos que só a casca usa (ShellGlyph.kt) ---------------------------
    static let SquareOnCircle: Character = "\u{F80C}"
    static let CircleGridHex: Character = "\u{F5EE}"
    static let SliderHorizontalBelowRectangle: Character = "\u{F7DD}"
    static let CircleGrid3x3: Character = "\u{F5EC}"
    static let Viewfinder: Character = "\u{F88D}"
    static let SquareSplit2x2: Character = "\u{F813}"
    static let PaintbrushFill: Character = "\u{F72F}"
    static let RectangleArrowUpRightArrowDownLeft: Character = "\u{F79E}"
    static let ArrowUpLeftArrowDownRight: Character = "\u{F386}"
    static let Nosign: Character = "\u{F727}"
    static let FolderBadgePlus: Character = "\u{F678}"
    static let SquareGrid3x2: Character = "\u{F806}"
    static let LockOpenFill: Character = "\u{F6FB}"
    static let Snow: Character = "\u{F7E7}"
    static let BookmarkSolid: Character = "\u{F3EA}"
    static let Metronome: Character = "\u{F70B}"
    static let ScissorsAlt: Character = "\u{F905}"
    static let WandRaysInverse: Character = "\u{F891}"
    static let PauseCircle: Character = "\u{F736}"
    static let PlayCircle: Character = "\u{F76F}"
    static let SquareFill: Character = "\u{F7FF}"
    static let SquareStack3dDownDottedline: Character = "\u{F816}"
    static let RectangleDock: Character = "\u{F7A3}"
    static let WaveformPathEcg: Character = "\u{F89A}"
    static let Table: Character = "\u{F844}"
    static let TextformatAlt: Character = "\u{F860}"

    /// O glifo desenhado no tamanho pedido — a mesma conta do Android (caixa
    /// N×N com `fontSize = N`). É o helper que qualquer tela usa quando não
    /// quer um `CupertinoIcon` inteiro.
    static func text(_ codepoint: Character, size: CGFloat,
                     color: Color = AureaColors.text) -> Text {
        Text(String(codepoint))
            .font(CupertinoFont.font(size))
            .foregroundColor(color)
    }
}

/// Espelho de `ShellGlyph` (os mesmos codepoints, o mesmo nome).
enum ShellGlyph {
    static let SquareOnCircle = CupertinoGlyph.SquareOnCircle
    static let CircleGridHex = CupertinoGlyph.CircleGridHex
    static let Scribble = CupertinoGlyph.Scribble
    static let SliderHorizontalBelowRectangle = CupertinoGlyph.SliderHorizontalBelowRectangle
    static let CircleGrid3x3 = CupertinoGlyph.CircleGrid3x3
    static let Viewfinder = CupertinoGlyph.Viewfinder
    static let SquareSplit2x2 = CupertinoGlyph.SquareSplit2x2
    static let PaintbrushFill = CupertinoGlyph.PaintbrushFill
    static let RectangleArrowUpRightArrowDownLeft = CupertinoGlyph.RectangleArrowUpRightArrowDownLeft
    static let ArrowUpLeftArrowDownRight = CupertinoGlyph.ArrowUpLeftArrowDownRight
    static let Nosign = CupertinoGlyph.Nosign
    static let FolderBadgePlus = CupertinoGlyph.FolderBadgePlus
    static let SquareGrid3x2 = CupertinoGlyph.SquareGrid3x2
    static let LockOpenFill = CupertinoGlyph.LockOpenFill
    static let Snow = CupertinoGlyph.Snow
    static let BookmarkSolid = CupertinoGlyph.BookmarkSolid
    static let Metronome = CupertinoGlyph.Metronome
    static let ScissorsAlt = CupertinoGlyph.ScissorsAlt
    static let WandRaysInverse = CupertinoGlyph.WandRaysInverse
    static let PauseCircle = CupertinoGlyph.PauseCircle
    static let PlayCircle = CupertinoGlyph.PlayCircle
    static let SquareFill = CupertinoGlyph.SquareFill
    static let SquareStack3dDownDottedline = CupertinoGlyph.SquareStack3dDownDottedline
    static let RectangleDock = CupertinoGlyph.RectangleDock
    static let WaveformPathEcg = CupertinoGlyph.WaveformPathEcg
    static let Table = CupertinoGlyph.Table
    static let TextformatAlt = CupertinoGlyph.TextformatAlt
}

// =============================================================================
// Peças reutilizadas da casca (a base que a casca já usava)
// =============================================================================
/// Cabeçalho de seção dos painéis.
struct AureaSectionHeader: View {
    let title: String
    var trailing: AnyView? = nil

    var body: some View {
        HStack {
            Text(title)
                .font(AureaType.Section)
                .foregroundStyle(AureaColors.text)
            Spacer()
            if let trailing { trailing }
        }
        .padding(.horizontal, AureaDims.pad)
        .padding(.top, 10)
        .padding(.bottom, 4)
    }
}

/// Botão de ícone da casca (topo, transporte, linhas de camada).
struct AureaIconButton: View {
    let systemName: String
    var active: Bool = false
    var enabled: Bool = true
    var size: CGFloat = AureaDims.iconMd
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Image(systemName: systemName)
                .font(.aurea(size: size, weight: .medium))
                .foregroundStyle(enabled ? (active ? AureaColors.accent : AureaColors.text) : AureaColors.disabled)
                .frame(width: AureaDims.chromeButtonW, height: AureaDims.chromeButtonW)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .disabled(!enabled)
    }
}

/// Rótulo + valor de uma linha de propriedade.
struct AureaPropertyRow<Content: View>: View {
    let title: String
    @ViewBuilder var content: Content

    var body: some View {
        HStack(spacing: 10) {
            Text(title)
                .font(AureaType.label)
                .foregroundStyle(AureaColors.muted)
                .frame(width: AureaDims.labelChipW, alignment: .leading)
            content
        }
        .padding(.horizontal, AureaDims.pad)
        .padding(.vertical, 5)
    }
}

/// Campo numérico do inspetor. Escreve o valor no motor pelo mesmo caminho dos
/// sliders — nunca guarda estado próprio além do texto em edição.
struct AureaNumberField: View {
    let value: Float
    var format: String = "%.1f"
    let onChange: (Float) -> Void

    @State private var text: String = ""
    @FocusState private var focused: Bool

    var body: some View {
        TextField("", text: $text)
            .keyboardType(.numbersAndPunctuation)
            .multilineTextAlignment(.center)
            .font(AureaType.value)
            .foregroundStyle(AureaColors.text)
            .padding(.vertical, 6)
            .frame(minWidth: AureaDims.valueBoxW + AureaDims.s1)
            .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: AureaDims.nameFieldRadius))
            .focused($focused)
            .onAppear { text = String(format: format, value) }
            // `onChange(of:perform:)` de um parâmetro: o de dois parâmetros é
            // iOS 17, e o piso do app é 16.
            .onChange(of: value) { newValue in
                if !focused { text = String(format: format, newValue) }
            }
            .onSubmit {
                if let parsed = Float(text.replacingOccurrences(of: ",", with: ".")), parsed.isFinite { onChange(parsed) }
                else { text = String(format: format, value) }
            }
            .onChange(of: focused) { editing in
                if !editing, let parsed = Float(text.replacingOccurrences(of: ",", with: ".")), parsed.isFinite, parsed != value { onChange(parsed) }
            }
    }
}

/// Losango de keyframe: aceso quando há keyframe NESTE instante.
struct AureaKeyframeDiamond: View {
    let on: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Image(systemName: on ? "diamond.fill" : "diamond")
                .font(.aurea(size: 12, weight: .semibold))
                .foregroundStyle(on ? AureaColors.keyframeOn : AureaColors.subtle)
                .frame(width: AureaDims.colorWell, height: AureaDims.colorWell)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

/// Exact vector paths from the Compose 1.7.8 dependency used by Android.
struct MaterialGlyph: View {
    let name: String
    var size: CGFloat
    var color: Color
    init(_ name: String, size: CGFloat = 24, color: Color = AureaColors.text) {
        self.name = name; self.size = size; self.color = color
    }
    var body: some View {
        Image("Material-" + name.replacingOccurrences(of: ".", with: "-"))
            .resizable().renderingMode(.template).scaledToFit()
            .frame(width: size, height: size).foregroundStyle(color)
    }
}
