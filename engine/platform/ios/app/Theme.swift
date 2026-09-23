// =============================================================================
//  Aurea / platform / ios / app / Theme.swift
//
//  As cores, a tipografia e as medidas da casca. Tudo vem do MESMO lugar que o
//  Android: `ui/theme/AureaTokens.kt` (cores), `editor/ShellTokens.kt` (peças só
//  da casca do editor) e `editor/EditorLayout.kt` (as alturas das zonas).
//
//  Por que copiar os números em vez de "desenhar bonito": o Aurea já tem uma
//  identidade aprovada (Beta A.01), e o iOS é a MESMA experiência — quem abre os
//  dois aparelhos tem que reconhecer o app. Onde o iOS pede outra coisa (um
//  gesto, uma folha modal), a diferença é do sistema, não do desenho.
// =============================================================================
import SwiftUI

// =============================================================================
// Cores (AureaTokens.kt)
// =============================================================================
enum AureaColors {
    static let brandDeep   = Color(hex: 0x123A63)
    static let brand       = Color(hex: 0x245D8C)
    static let accent      = Color(hex: 0x6FAED9)
    static let keyframe    = Color(hex: 0xA9D3EC)
    static let background  = Color(hex: 0x0F141A)
    static let surface     = Color(hex: 0x151C24)
    static let surfaceHigh = Color(hex: 0x1B2530)
    static let chip        = Color(hex: 0x212D3A)
    static let chipHigh    = Color(hex: 0x323D49)
    static let border      = Color(hex: 0x273442)
    static let text        = Color(hex: 0xF7F9FB)
    static let muted       = Color(hex: 0xAAB6C3)
    static let subtle      = Color(hex: 0x7C8A99)
    static let onAccent    = Color(hex: 0x0B1117)
    static let accentDim   = Color(hex: 0x1D3A55)
    static let danger      = Color(hex: 0xFF6B6B)
    static let warning     = Color(hex: 0xFFC978)
    static let success     = Color(hex: 0x4CD08A)
    static let stage       = Color(hex: 0x0A0E13)
    static let playhead    = Color.white
    static let hairline    = Color(hex: 0x273442).opacity(0.72)
    static let systemBarVeil = Color(hex: 0x0B0F13)
    static let navigationBar = Color.black
    static let scrim       = Color.black.opacity(0.54)
    static let sheetScrim  = Color(hex: 0x0A0E13).opacity(0.35)
    static let disabled    = Color.white.opacity(0.25)
    static let rowHighlight = Color.white.opacity(0.06)
    /// Trilho da barra de tempo (branco 22 %, `ShellColors.Track22`).
    static let track       = Color.white.opacity(0.22)
    static let tickMajor   = Color(hex: 0x8A97AD)
    static let tickMinor   = Color(hex: 0x5A6880)
    /// Losango escolhido na timeline (`TimelineTokens.KeyframeOn`).
    static let keyframeOn  = Color(hex: 0xFFC107)
    static let trimHandle  = Color(hex: 0xF2F5F9)

    /// Paleta das etiquetas de camada (`ShellColors.LabelPalette`).
    static let labelPalette: [Color] = [
        Color(hex: 0xE85B81), Color(hex: 0xFFB020), accent, Color(hex: 0x2BE3A0),
        Color(hex: 0x35C4E7), keyframe, Color(hex: 0x3D7BFF), Color(hex: 0xFF7A3D),
        Color(hex: 0xFF4D5E), Color(hex: 0xFFE14D), Color(hex: 0xB0B8C4), Color(hex: 0x4A5160),
    ]
}

extension Color {
    /// `0xFF151C24` → Color. O canal alfa vem junto (o token do Android é ARGB).
    init(hex: UInt32) {
        let a = Double((hex >> 24) & 0xFF) / 255.0
        let r = Double((hex >> 16) & 0xFF) / 255.0
        let g = Double((hex >> 8) & 0xFF) / 255.0
        let b = Double(hex & 0xFF) / 255.0
        self.init(.sRGB, red: r, green: g, blue: b, opacity: a)
    }
}

// =============================================================================
// Tipografia
// =============================================================================
enum AureaType {
    static let title     = Font.system(size: 22, weight: .semibold, design: .rounded)
    static let section   = Font.system(size: 15, weight: .semibold)
    static let body      = Font.system(size: 14, weight: .regular)
    static let label     = Font.system(size: 12.5, weight: .medium)
    static let value     = Font.system(size: 13, weight: .medium, design: .monospaced)
    static let tiny      = Font.system(size: 11, weight: .medium)
    /// `AureaType.TabLabel`: 10,5 w500 — o rótulo da barra de abas.
    static let tabLabel  = Font.system(size: 10.5, weight: .medium)
}

// =============================================================================
// Medidas (AureaDims + ShellDims)
// =============================================================================
enum AureaDims {
    static let hairline: CGFloat = 1
    static let tabBarHeight: CGFloat = 54
    static let iconLg: CGFloat = 24
    static let iconSm: CGFloat = 16
    static let topBar: CGFloat = 44
    static let transport: CGFloat = 46
    static let strip: CGFloat = 8
    static let corner: CGFloat = 10
    static let pad: CGFloat = 14
    static let sheetHandle: CGFloat = 12
    static let fab: CGFloat = 52
}

/// As alturas das zonas do editor — porte literal de `EditorLayout` do Android.
///
/// A regra que a casca A.01 cobrava: abrir painel, adicionar ou trocar de aba
/// NUNCA move o preview. O painel tira espaço da TIMELINE, e a timeline nunca
/// fica abaixo do piso. No iOS isso também é o que mantém o CAMetalLayer com o
/// mesmo tamanho durante um arrasto (redimensionar o drawable a cada gesto
/// custaria uma recriação de swapchain).
enum SheetContent { case none, hint, dock, panel, adding }

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
        let fraction: CGFloat
        if total <= 0 {
            fraction = previewFractionMax
        } else {
            let reserve = min(max(250, ws * 0.42), 320)
            fraction = min(previewFractionMax, max(previewMin, ws - 90 - reserve) / total)
        }
        let preview = (total * min(max(fraction, 0.14), 0.60))
            .clamped(to: previewMin...(max(previewMin, ws - timelineMin)))

        var sheetFraction: CGFloat = 0
        switch content {
        case .none: sheetFraction = 0
        case .hint: sheetFraction = ws > 0 ? (sheetHandle + hintBody) / ws : 0
        case .dock: sheetFraction = dockFraction
        case .panel: sheetFraction = panelFraction
        case .adding: sheetFraction = ws > 0 ? (sheetHandle + addBody) / ws : 0
        }
        var sheet = content == .none ? 0 : ws * min(max(sheetFraction, 0), 0.60)

        let floor: CGFloat
        switch content {
        case .adding, .dock, .panel: floor = 90
        default: floor = 120
        }
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
}

extension Comparable {
    func clamped(to range: ClosedRange<Self>) -> Self {
        min(max(self, range.lowerBound), range.upperBound)
    }
}

// =============================================================================
// Peças reutilizadas
// =============================================================================
/// Cabeçalho de seção dos painéis.
struct AureaSectionHeader: View {
    let title: String
    var trailing: AnyView? = nil

    var body: some View {
        HStack {
            Text(title)
                .font(AureaType.section)
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
    var size: CGFloat = 20
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Image(systemName: systemName)
                .font(.system(size: size, weight: .medium))
                .foregroundStyle(enabled ? (active ? AureaColors.accent : AureaColors.text) : AureaColors.disabled)
                .frame(width: 38, height: 38)
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
                .frame(width: 86, alignment: .leading)
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
            .frame(minWidth: 62)
            .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 7))
            .focused($focused)
            .onAppear { text = String(format: format, value) }
            // `onChange(of:perform:)` de um parâmetro: o de dois parâmetros é
            // iOS 17, e o piso do app é 16.
            .onChange(of: value) { newValue in
                if !focused { text = String(format: format, newValue) }
            }
            .onSubmit {
                if let parsed = Float(text.replacingOccurrences(of: ",", with: ".")) { onChange(parsed) }
                else { text = String(format: format, value) }
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
                .font(.system(size: 12, weight: .semibold))
                .foregroundStyle(on ? AureaColors.keyframeOn : AureaColors.subtle)
                .frame(width: 30, height: 30)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}
