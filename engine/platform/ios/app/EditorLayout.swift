// =============================================================================
//  Aurea / platform / ios / app / EditorLayout.swift
//
//  As medidas e as cores da CASCA do editor que ainda não estão no Theme.swift
//  — o porte de `editor/EditorLayout.kt` (as zonas), das peças de
//  `editor/ShellTokens.kt` (a doca, o "+", o chip de resolução) e das
//  constantes privadas de `editor/Stage.kt` (o gizmo, as alças, a linha de
//  encaixe).
//
//  AS ZONAS já vivem no `Theme.swift` (`EditorLayout.solve`, `EditorMetrics`,
//  `SheetContent`) — aqui só entra o que a casca do editor usa e o tema global
//  não tem, com o MESMO valor do Android. Nenhum número solto: quem desenha lê
//  um token daqui, como no Kotlin lê `ShellDims`/`ShellColors`.
// =============================================================================
import SwiftUI

// =============================================================================
// Cores (ShellTokens.kt + as privadas de Stage.kt)
// =============================================================================
enum StageInk {
    /// `ShellColors.White40`: o tempo da barra do projeto.
    static let white40        = Color(hex: 0x66FFFFFF)
    static let dockRow        = Color(hex: 0xFF1E222D)
    static let dockTile       = Color(hex: 0xFF222634)
    static let dockTileContent = Color(hex: 0xFFD4D8E2)
    static let fab            = Color(hex: 0xFF1E2130)
    static let fabShadow      = Color.black.opacity(0.45)
    /// "Voltar ao editor" e o HUD.
    static let floatingDark   = Color(hex: 0xCC12151A)
    static let resolutionChip = Color(hex: 0xCC171D25)
    /// Traço escuro por baixo de todo contorno do palco.
    static let outlineUnder   = Color(hex: 0x8C0A0E13)
    static let snapLine       = Color(hex: 0xCCFF6B6B)
    static let busyVeil       = Color(hex: 0xDD17191D)
    static let menuScrim      = Color(hex: 0x8A000000)
    static let menuHandle     = Color(hex: 0x66AAB6C3)
    /// Item de menu apagado (muted 60 %).
    static let disabledMuted  = Color(hex: 0x99AAB6C3)
    /// Borda da faixa do cadeado e da faixa do vetor (destaque 50 %).
    static let accentHalf     = Color(hex: 0x806FAED9)
    static let snapHaptic     = Color.clear

    /// Rastreio de câmera (`Stage.kt`): amarelo entrou no solve, vermelho não.
    static let trackSolved    = Color(hex: 0xFFFFD34D)
    static let trackRejected  = Color(hex: 0xFFFF5A5A)
    /// Gizmo 3D: X vermelho, Y verde, Z azul.
    static let gizmoX         = Color(hex: 0xFFFF5A5A)
    static let gizmoY         = Color(hex: 0xFF5AD27A)
    static let gizmoZ         = Color(hex: 0xFF5AA8FF)
    /// Caminho da máscara: a editada em âmbar, as outras em branco 70 %.
    static let maskEdit       = Color(hex: 0xFFFFD34D)
    static let maskOther      = Color(hex: 0xB3FFFFFF)
}

// =============================================================================
// Medidas (ShellDims + as privadas de Stage.kt e AddLayerPanel.kt)
// =============================================================================
enum StageDim {
    static let stageInset: CGFloat = 8          // compositionRect: min(8, lado/4)
    static let sheetHandle: CGFloat = 12
    static let fullscreenTimeBar: CGFloat = 44
    static let fab: CGFloat = 52
    static let fabMargin: CGFloat = 18
    /// kTouchSlop do Flutter: tocar nunca move.
    static let touchSlop: CGFloat = 18
    static let handleSlop: CGFloat = 4
    static let snapTolerance: CGFloat = 10
    static let hitSlack: CGFloat = 12
    static let axisHandleTarget: CGFloat = 26

    /// Alvo dos botões das barras (N×44, sem enfeite próprio).
    static let barButtonWidth: CGFloat = 40
    static let barButtonHeight: CGFloat = 44

    // --- Palco (as privadas do Stage.kt) ---
    static let gizmoLength: Float = 320         // GIZMO_LENGTH do EditorStore
    static let gizmoTip: CGFloat = 44
    static let gizmoReach: CGFloat = 24
    static let handleRadius: CGFloat = 5
    static let handleRadiusGrabbed: CGFloat = 6
    static let handleEdge: CGFloat = 22
    static let handleMinDistance: CGFloat = 30
    static let handleSeparation: CGFloat = 60
    static let rotateRadius: CGFloat = 35 / 2
    static let snapStroke: CGFloat = 1.5
    static let outlineUnderWidth: CGFloat = 3.5
    static let outlineOverWidth: CGFloat = 2
    static let batchUnderWidth: CGFloat = 2.5
    static let batchOverWidth: CGFloat = 1.5
    static let maskPointHalf: CGFloat = 5.5
    static let lockMajor: CGFloat = 24
    static let lockMinor: CGFloat = 12
    static let minPivot: CGFloat = 8
    static let pinchDeadZone: CGFloat = 4       // graus

    // --- Doca e adicionar (BottomArea.kt / AddLayerPanel.kt) ---
    static let dockRowPad: CGFloat = 10
    static let dockRowGap: CGFloat = 8
    static let dockTileIcon: CGFloat = 27
    static let dockTileIconSmall: CGFloat = 22
    static let batchRowHeight: CGFloat = 52
    static let batchRowShort: CGFloat = 48
    static let addCategories: CGFloat = 68
    static let addTabWidth: CGFloat = 64
    static let addCardHeight: CGFloat = 80
    static let addCardIcon: CGFloat = 28
    static let addShapeInset: CGFloat = 12
    /// A barra fixa de adicionar (no lugar do "+"): `ShellDims.AddBar*` do Android.
    static let addBar: CGFloat = EditorLayout.addBar
    static let addBarItem: CGFloat = 64
    static let addBarItemMin: CGFloat = 56
    static let addBarIcon: CGFloat = 23
    static let addBarItemInset: CGFloat = 4
    static let hairline: CGFloat = 1
}

// =============================================================================
// Tempo — porte literal de `ShellTime` (ChromeKit.kt)
// =============================================================================
enum ShellClock {
    private static func millis(_ frame: Int64, _ fps: Float) -> Int64 {
        let f = fps > 0 ? Double(fps) : 30
        return Int64(Double(frame) / f * 1000.0)
    }

    private static func pad2(_ v: Int64) -> String { v < 10 ? "0\(v)" : "\(v)" }

    /// "m:ss.cc" — o relógio da barra do projeto (`_tempoCurto`).
    static func short(_ frame: Int64, _ fps: Float) -> String {
        let ms = millis(frame, fps)
        return "\(ms / 60000):\(pad2((ms / 1000) % 60)).\(pad2((ms % 1000) / 10))"
    }

    /// "m:ss.d" — a barra de tempo da tela cheia (`formatTime`).
    static func tenths(_ frame: Int64, _ fps: Float) -> String {
        let ms = millis(frame, fps)
        return "\(ms / 60000):\(pad2((ms % 60000) / 1000)).\((ms % 1000) / 100)"
    }

    /// Lê "12.5", "1:02.5", "1:02:03.5" e "00:01:02:15" (o último campo em
    /// quadros quando há três separadores) — `parseTimecodeInput` da A.01.
    static func parseToFrame(_ text: String, _ fps: Float) -> Int64? {
        let f = Double(fps > 0 ? fps : 30)
        let s = text.trimmingCharacters(in: .whitespacesAndNewlines).replacingOccurrences(of: ",", with: ".")
        if s.isEmpty { return nil }
        let parts = s.split(separator: ":", omittingEmptySubsequences: false).map(String.init)
        func d(_ i: Int) -> Double? { Double(parts[i]) }
        let seconds: Double
        switch parts.count {
        case 1: guard let a = d(0) else { return nil }; seconds = a
        case 2: guard let a = d(0), let b = d(1) else { return nil }; seconds = a * 60 + b
        case 3: guard let a = d(0), let b = d(1), let c = d(2) else { return nil }; seconds = a * 3600 + b * 60 + c
        case 4:
            guard let a = d(0), let b = d(1), let c = d(2), let e = d(3) else { return nil }
            seconds = a * 3600 + b * 60 + c + e / f
        default: return nil
        }
        return Int64((seconds * f).rounded())
    }
}

// =============================================================================
// As folhas da casca (um por vez, como no `ShellSheet` do Android)
// =============================================================================
enum ShellSheet: String, Identifiable {
    case layerMenu, renameLayer, timelineMenu, projectSettings, copyPaste, goToTime, searchLayers
    var id: String { rawValue }
}

/// O que é de APRESENTAÇÃO da casca: nada aqui é do projeto. O motor não sabe
/// que existe uma folha aberta, um dedo arrastando ou uma linha de encaixe no
/// ar — o mesmo recorte do `EditorUi` do Android.
@MainActor final class EditorShellUi: ObservableObject {
    @Published var sheet: ShellSheet?
    /// Um dedo manipula algo no palco: o transporte vira a barra de informações.
    @Published var manipulating = false
    /// Tela do "adicionar camada": a aba escolhida.
    /// Linha de encaixe ativa (px da composição; nil = nenhuma). Lida no desenho.
    @Published var snapX: Float?
    @Published var snapY: Float?
    /// Alça pega (0 = giro, 1..3 = escala; −1 = nenhuma).
    @Published var grabbedHandle = -1

    func open(_ next: ShellSheet, _ model: AureaModel) {
        if model.status.playing != 0 { model.playPause() }
        sheet = next
    }
}

/// Voltar (o gesto do sistema e o "‹" das barras) — a ordem da A.01, uma coisa
/// por toque. É o `editorBack` da sessão, que já tem essa ordem.
@MainActor func shellBack(_ model: AureaModel) {
    model.editorBack()
}

// =============================================================================
// Botão das barras (`_BotaoDoCromo`): alvo N×44, ícone 21, sem enfeite próprio
// =============================================================================
struct ShellBarButton: View {
    let glyph: Character
    let description: String
    var size: CGFloat = 21
    var width: CGFloat = StageDim.barButtonWidth
    var height: CGFloat = StageDim.barButtonHeight
    var tint: Color?
    var enabled = true
    var mirror = false
    var onLongPress: (() -> Void)?
    var action: () -> Void

    var body: some View {
        CupertinoGlyph.text(glyph, size: size, color: enabled ? (tint ?? AureaColors.text) : AureaColors.disabled)
            .scaleEffect(x: mirror ? -1 : 1, y: 1)
            .frame(width: width, height: height)
            .contentShape(Rectangle())
            .onTapGesture { if enabled { action() } }
            .onLongPressGesture(minimumDuration: 0.45) { if enabled { onLongPress?() } }
            .accessibilityLabel(description)
            .accessibilityAddTraits(.isButton)
    }
}

/// Shell popups live above the entire editor, matching Android's window overlays.
final class ShellPresentation: ObservableObject {
    @Published var sheet: ShellSheet?
    @Published var linkAnchor: CGRect?
    @Published var linkIds: [Int64] = []
    /// Categoria aberta pela barra de adicionar (índice das `ShellAddCategories`).
    @Published var addCategory = 0
    @Published var resolutionAnchor: CGRect?
    @Published var timeInput = ""
    @Published var grabbedHandle = -1
    @Published var grabbedShapeHandle = -1
    @Published var snapX: Float?
    @Published var snapY: Float?
    /// Seletor pedido pelo diálogo de adicionar. Quem APRESENTA é a raiz do
    /// editor (`AddLayerPickers`), nunca o diálogo: ele some ao escolher, e um
    /// seletor cujo dono sai da tela no meio do fechamento deixava a próxima
    /// apresentação presa e invisível (foto/vídeo e depois modelo 3D: nada
    /// entrava e nenhum toque respondia).
    @Published var addPicker: ShellAddPicker?
    func dismiss() { sheet = nil; linkAnchor = nil; resolutionAnchor = nil }
}

/// Os seletores do diálogo de adicionar (galeria/foto/vídeo pelo seletor de
/// fotos; áudio, modelo 3D e SVG pelo de arquivos).
enum ShellAddPicker: Equatable {
    case gallery, photo, video, audioFromVideo, audioFile, model, svg, modelTextures
    var isFile: Bool { self == .audioFile || self == .model || self == .svg || self == .modelTextures }
}

enum ShellStageGeometry {
    static let gizmoLength: Float = 320
    /// Setas do 2D a partir do centro (Stage.kt placeAxisHandles): 0 = X à
    /// direita, 1 = Y para cima, 56 pt. Perto da borda do palco a seta vira
    /// para o outro lado (continua tocável). A Metal view já tem o recuo de 8 pt.
    static func axisHandles(_ center: CGPoint, size: CGSize) -> [CGPoint] {
        let length: CGFloat = 56, edge: CGFloat = 14
        return [CGPoint(x: center.x + length > size.width - edge ? center.x - length : center.x + length, y: center.y),
                CGPoint(x: center.x, y: center.y - length < edge ? center.y + length : center.y - length)]
    }
    static func gizmoTips(_ points: [CGPoint]) -> [CGPoint] {
        guard points.count == 4 else { return [] }; var result = points
        let extent = (1...3).map { hypot(points[$0].x - points[0].x, points[$0].y - points[0].y) }.max() ?? 0
        if extent > 0.0001 {
            let scale = 80 / extent
            for i in 1...3 {
                result[i] = CGPoint(x: points[0].x + (points[i].x - points[0].x) * scale,
                                    y: points[0].y + (points[i].y - points[0].y) * scale)
            }
        }
        if hypot(result[3].x - result[0].x, result[3].y - result[0].y) < 44 * 0.6 { result[3] = CGPoint(x: result[0].x + 44 * 0.7, y: result[0].y - 44 * 0.7) }
        return result
    }
}

/// `drawShapePreset` de AddLayerPanel.kt. O número é o preset real do motor,
/// não o índice da ficha na grade (círculo 0, quadrado 10, arredondado 1...).
struct ShellShapeGlyph: View {
    let preset: Int
    var body: some View {
        Canvas { context, size in
            let s = min(size.width, size.height)
            let o = CGPoint(x: (size.width - s) / 2, y: (size.height - s) / 2)
            func p(_ x: CGFloat, _ y: CGFloat) -> CGPoint { CGPoint(x: o.x + x * s, y: o.y + y * s) }
            func rect(_ x: CGFloat, _ y: CGFloat, _ w: CGFloat, _ h: CGFloat) -> CGRect {
                CGRect(origin: p(x, y), size: CGSize(width: w * s, height: h * s))
            }
            func polygon(_ values: [CGFloat]) -> Path {
                var path = Path(); path.move(to: p(values[0], values[1]))
                for i in stride(from: 2, to: values.count, by: 2) { path.addLine(to: p(values[i], values[i + 1])) }
                path.closeSubpath(); return path
            }
            func radial(_ count: Int, _ outer: CGFloat, _ inner: CGFloat? = nil, cy: CGFloat = 0.5) -> Path {
                var values: [CGFloat] = []
                for k in 0..<count {
                    let a = -CGFloat.pi / 2 + CGFloat(k) * 2 * .pi / CGFloat(count)
                    let r = k % 2 == 1 ? (inner ?? outer) : outer
                    values += [0.5 + r * cos(a), cy + r * sin(a)]
                }
                return polygon(values)
            }
            let fill = GraphicsContext.Shading.color(StageInk.dockTileContent)
            switch preset {
            case 0: context.fill(Path(ellipseIn: rect(0, 0, 1, 1)), with: fill)
            case 10: context.fill(Path(rect(0.04, 0.04, 0.92, 0.92)), with: fill)
            case 1: context.fill(Path(roundedRect: rect(0.04, 0.04, 0.92, 0.92), cornerRadius: s * 0.2), with: fill)
            case 12: context.fill(Path(roundedRect: rect(0, 0.34, 1, 0.32), cornerRadius: s * 0.16), with: fill)
            case 4: context.fill(polygon([0.5, 0.04, 0.98, 0.9, 0.02, 0.9]), with: fill)
            case 14: context.fill(polygon([0.06, 0.06, 0.94, 0.94, 0.06, 0.94]), with: fill)
            case 6: context.fill(radial(6, 0.5), with: fill)
            case 11: context.fill(radial(10, 0.52, 0.23, cy: 0.55), with: fill)
            case 2:
                context.fill(Path(rect(0.33, 0, 0.34, 1)), with: fill)
                context.fill(Path(rect(0, 0.33, 1, 0.34)), with: fill)
            case 3: context.stroke(Path(ellipseIn: rect(0.1, 0.1, 0.8, 0.8)), with: fill, lineWidth: s * 0.2)
            case 5:
                var path = Path(); path.move(to: p(0.5, 0.5)); path.addLine(to: p(1, 0.5))
                path.addArc(center: p(0.5, 0.5), radius: s / 2, startAngle: .degrees(0), endAngle: .degrees(270), clockwise: false)
                path.closeSubpath(); context.fill(path, with: fill)
            case 7:
                for k in 0..<6 {
                    let a = -CGFloat.pi / 2 + CGFloat(k) * .pi / 3
                    let center = p(0.5 + 0.25 * cos(a), 0.5 + 0.25 * sin(a))
                    var petal = context
                    petal.translateBy(x: center.x, y: center.y)
                    petal.rotate(by: .radians(Double(a + .pi / 2)))
                    petal.fill(Path(ellipseIn: CGRect(x: -s * 0.12, y: -s * 0.26, width: s * 0.24, height: s * 0.52)), with: fill)
                }
            // Formas vindas do app antigo (presets 15..22 do motor).
            case 15: context.fill(radial(8, 0.5), with: fill)
            case 16: context.fill(radial(5, 0.5, cy: 0.53), with: fill)
            case 17: context.fill(polygon([0.2, 0.2, 0.8, 0.2, 1, 0.8, 0, 0.8]), with: fill)
            case 18: context.fill(polygon([0.25, 0.25, 1, 0.25, 0.75, 0.75, 0, 0.75]), with: fill)
            case 19: context.fill(radial(8, 0.5, 0.2), with: fill)
            case 20: context.fill(radial(12, 0.5, 0.28), with: fill)
            case 21: context.fill(ShapeGlyphPaths.gear(center: p(0.5, 0.5), radius: s / 2, teeth: 10, hub: 0.3), with: fill, style: FillStyle(eoFill: true))
            case 22: context.fill(ShapeGlyphPaths.doubleArrow(center: p(0.5, 0.5), radius: s / 2), with: fill)
            default:
                var stem = Path(); stem.move(to: p(0.04, 0.5)); stem.addLine(to: p(0.66, 0.5))
                context.stroke(stem, with: fill, lineWidth: s * 0.2)
                context.fill(polygon([0.6, 0.2, 0.98, 0.5, 0.6, 0.8]), with: fill)
            }
        }
    }
}

/// Silhuetas compartilhadas (grade de formas e troca de forma): engrenagem com
/// raiz 0,78 e furo do cubo (preencher em par-ímpar), seta dupla.
enum ShapeGlyphPaths {
    static func gear(center c: CGPoint, radius r: CGFloat, teeth: Int, hub: CGFloat) -> Path {
        var path = Path()
        let root = r * 0.78, steps = teeth * 4
        for i in 0..<steps {
            let a0 = -CGFloat.pi / 2 + CGFloat(i) * 2 * .pi / CGFloat(steps), a1 = a0 + 2 * .pi / CGFloat(steps)
            let rr = (i % 4 == 1 || i % 4 == 2) ? r : root
            let p0 = CGPoint(x: c.x + rr * cos(a0), y: c.y + rr * sin(a0))
            if i == 0 { path.move(to: p0) } else { path.addLine(to: p0) }
            path.addLine(to: CGPoint(x: c.x + rr * cos(a1), y: c.y + rr * sin(a1)))
        }
        path.closeSubpath()
        path.addEllipse(in: CGRect(x: c.x - root * hub, y: c.y - root * hub, width: 2 * root * hub, height: 2 * root * hub))
        return path
    }
    static func doubleArrow(center c: CGPoint, radius r: CGFloat) -> Path {
        let pts: [(CGFloat, CGFloat)] = [(-1, 0), (-0.45, -0.8), (-0.45, -0.22), (0.45, -0.22), (0.45, -0.8), (1, 0), (0.45, 0.8), (0.45, 0.22), (-0.45, 0.22), (-0.45, 0.8)]
        var path = Path()
        for (i, q) in pts.enumerated() {
            let point = CGPoint(x: c.x + r * q.0, y: c.y + r * q.1)
            if i == 0 { path.move(to: point) } else { path.addLine(to: point) }
        }
        path.closeSubpath()
        return path
    }
    static func quad(center c: CGPoint, radius r: CGFloat, _ xy: [CGFloat]) -> Path {
        var path = Path()
        for i in stride(from: 0, to: xy.count - 1, by: 2) {
            let point = CGPoint(x: c.x + xy[i] * r, y: c.y + xy[i + 1] * r)
            if i == 0 { path.move(to: point) } else { path.addLine(to: point) }
        }
        path.closeSubpath()
        return path
    }
}

struct ShellNullGlyph: View {
    var body: some View {
        Canvas { context, size in
            let k = min(size.width, size.height) / 30
            let r = CGRect(x: size.width / 2 - 13 * k, y: size.height / 2 - 13 * k, width: 26 * k, height: 26 * k)
            context.stroke(Path(roundedRect: r, cornerRadius: 4 * k), with: .color(StageInk.dockTileContent), lineWidth: 1.8 * k)
            var line = Path(); line.move(to: CGPoint(x: r.minX + 3 * k, y: r.maxY - 3 * k)); line.addLine(to: CGPoint(x: r.maxX - 3 * k, y: r.minY + 3 * k))
            context.stroke(line, with: .color(StageInk.dockTileContent), lineWidth: 1.8 * k)
        }
    }
}

/// Contorno e pontos da aba Vetor, portados de `drawVectorIcon`.
struct ShellVectorGlyph: View {
    let kind: Int
    var body: some View {
        Canvas { context, size in
            let s = min(size.width, size.height)
            func p(_ x: CGFloat, _ y: CGFloat) -> CGPoint { CGPoint(x: (size.width - s) / 2 + x * s, y: (size.height - s) / 2 + y * s) }
            var path = Path(), points: [CGPoint] = []
            switch kind {
            case 0:
                let a = p(0.08, 0.8), b = p(0.92, 0.3)
                path.move(to: a); path.addCurve(to: b, control1: p(0.3, 0.05), control2: p(0.62, 1))
                points = [a, b]
                var handle = Path(); handle.move(to: b); handle.addLine(to: p(0.98, 0.06))
                context.stroke(handle, with: .color(AureaColors.accent), lineWidth: s * 0.03)
                let c = p(0.98, 0.06)
                context.fill(Path(ellipseIn: CGRect(x: c.x - s * 0.06, y: c.y - s * 0.06, width: s * 0.12, height: s * 0.12)), with: .color(AureaColors.accent))
            case 1:
                points = [p(0.1, 0.18), p(0.9, 0.18), p(0.9, 0.82), p(0.1, 0.82)]
                path.addRect(CGRect(origin: p(0.1, 0.18), size: CGSize(width: s * 0.8, height: s * 0.64)))
            case 2:
                points = [p(0.5, 0.18), p(0.94, 0.5), p(0.5, 0.82), p(0.06, 0.5)]
                path.addEllipse(in: CGRect(origin: p(0.06, 0.18), size: CGSize(width: s * 0.88, height: s * 0.64)))
            default:
                let count = kind == 3 ? 6 : 10
                for k in 0..<count {
                    let a = -CGFloat.pi / 2 + CGFloat(k) * 2 * .pi / CGFloat(count)
                    let r: CGFloat = kind == 3 ? 0.44 : (k % 2 == 0 ? 0.46 : 0.2)
                    let q = p(0.5 + r * cos(a), 0.52 + r * sin(a)); points.append(q)
                    if k == 0 { path.move(to: q) } else { path.addLine(to: q) }
                }
                path.closeSubpath()
            }
            context.stroke(path, with: .color(StageInk.dockTileContent), lineWidth: s * 0.06)
            for q in points {
                let box = Path(CGRect(x: q.x - s * 0.065, y: q.y - s * 0.065, width: s * 0.13, height: s * 0.13))
                context.fill(box, with: .color(.white)); context.stroke(box, with: .color(AureaColors.accent), lineWidth: s * 0.03)
            }
        }
    }
}

// =============================================================================
// Geometria da camada — porte de `LayerGeometry` (LayerOps.kt)
//
// A MESMA conta do motor (`layer_matrix` em Renderer.cpp); quando a camada tem
// pais, o motor já resolveu tudo e o detalhe traz os cantos do mundo.
// =============================================================================
enum StageGeom {

    static func floats(_ value: Any?) -> [Float] {
        guard let numbers = value as? [NSNumber] else { return [] }
        return numbers.map { $0.floatValue }
    }

    static func layerKind(_ d: [String: Any]) -> UInt32 {
        (d["kind"] as? NSNumber)?.uint32Value ?? 0
    }

    /// Largura/altura da mídia (a imagem usa o dobro da âncora quando o motor
    /// ainda não gravou o tamanho — o mesmo remendo do Android).
    static func width(_ d: [String: Any]) -> Float {
        let source = floats(d["sourceSize"])
        if source.count > 0 && source[0] > 0 { return source[0] }
        if layerKind(d) == 2 {
            let anchor = floats(d["anchor"])
            if anchor.count > 0 && anchor[0] > 0 { return anchor[0] * 2 }
        }
        return 0
    }

    static func height(_ d: [String: Any]) -> Float {
        let source = floats(d["sourceSize"])
        if source.count > 1 && source[1] > 0 { return source[1] }
        if layerKind(d) == 2 {
            let anchor = floats(d["anchor"])
            if anchor.count > 1 && anchor[1] > 0 { return anchor[1] * 2 }
        }
        return 0
    }

    static func hasSize(_ d: [String: Any]) -> Bool { width(d) > 0 && height(d) > 0 }

    /// EnginePods.LayerDetail.hasWorldCorners: an eight-element buffer exists
    /// even when the core did not produce projected corners (e.g. Model3D).
    static func hasWorldCorners(_ d: [String: Any]) -> Bool {
        ((d["geomFlags"] as? NSNumber)?.uint32Value ?? 0) & 1 != 0
    }

    /// Cantos TL, TR, BR, BL (x,y intercalados) em px da composição.
    static func corners(_ d: [String: Any], _ out: inout [Float]) -> Bool {
        let world = floats(d["corners"])
        if hasWorldCorners(d) && world.count == 8 {
            out = world
            return true
        }
        if !hasSize(d) { return false }
        let w = width(d)
        let h = height(d)
        let rad = Double(floats(d["rotation"]).count > 2 ? floats(d["rotation"])[2] : 0) * .pi / 180
        let c = Float(cos(rad))
        let s = Float(sin(rad))
        let scale = floats(d["scale"])
        let sx = scale.count > 0 ? scale[0] : 1
        let sy = scale.count > 1 ? scale[1] : 1
        let position = floats(d["position"])
        let anchor = floats(d["anchor"])
        let px = position.count > 0 ? position[0] : 0
        let py = position.count > 1 ? position[1] : 0
        // 3D: a âncora é o CENTRO da silhueta (o pivô do modelo).
        let centered = layerKind(d) == 10
        let ax = (anchor.count > 0 ? anchor[0] : 0) + (centered ? w * 0.5 : 0)
        let ay = (anchor.count > 1 ? anchor[1] : 0) + (centered ? h * 0.5 : 0)
        var result = [Float](repeating: 0, count: 8)
        for i in 0..<4 {
            let lx: Float = (i == 1 || i == 2) ? w : 0
            let ly: Float = i >= 2 ? h : 0
            let dx = (lx - ax) * sx
            let dy = (ly - ay) * sy
            result[i * 2] = px + dx * c - dy * s
            result[i * 2 + 1] = py + dx * s + dy * c
        }
        out = result
        return true
    }

    /// Caixa alinhada aos eixos (left, top, right, bottom) na composição.
    static func bounds(_ d: [String: Any]) -> (Float, Float, Float, Float)? {
        var pts = [Float](repeating: 0, count: 8)
        if !corners(d, &pts) { return nil }
        var l = Float.greatestFiniteMagnitude
        var t = Float.greatestFiniteMagnitude
        var r = -Float.greatestFiniteMagnitude
        var b = -Float.greatestFiniteMagnitude
        for i in 0..<4 {
            l = min(l, pts[i * 2]); r = max(r, pts[i * 2])
            t = min(t, pts[i * 2 + 1]); b = max(b, pts[i * 2 + 1])
        }
        return (l, t, r, b)
    }

    /// O ponto (composição) cai dentro da camada, com `slack` px de folga?
    static func contains(_ d: [String: Any], _ x: Float, _ y: Float, slack: Float) -> Bool {
        let world = floats(d["corners"])
        if hasWorldCorners(d) && world.count == 8 { return quadContains(world, x, y, slack: slack) }
        if !hasSize(d) { return false }
        let scale = floats(d["scale"])
        let sx = scale.count > 0 ? scale[0] : 1
        let sy = scale.count > 1 ? scale[1] : 1
        if abs(sx) < 1e-6 || abs(sy) < 1e-6 { return false }
        let rotation = floats(d["rotation"])
        let rad = Double(rotation.count > 2 ? rotation[2] : 0) * .pi / 180
        let c = Float(cos(rad))
        let s = Float(sin(rad))
        let position = floats(d["position"])
        let anchor = floats(d["anchor"])
        let dx = x - (position.count > 0 ? position[0] : 0)
        let dy = y - (position.count > 1 ? position[1] : 0)
        let ux = dx * c + dy * s
        let uy = -dx * s + dy * c
        let lx = ux / sx + (anchor.count > 0 ? anchor[0] : 0)
        let ly = uy / sy + (anchor.count > 1 ? anchor[1] : 0)
        let tx = slack / abs(sx)
        let ty = slack / abs(sy)
        return lx >= -tx && lx <= width(d) + tx && ly >= -ty && ly <= height(d) + ty
    }

    /// Ponto dentro do quadrilátero convexo (ou a menos de `slack` da borda).
    static func quadContains(_ q: [Float], _ x: Float, _ y: Float, slack: Float) -> Bool {
        var pos = 0
        var neg = 0
        var near = false
        for i in 0..<4 {
            let ax = q[i * 2], ay = q[i * 2 + 1]
            let bx = q[((i + 1) % 4) * 2], by = q[((i + 1) % 4) * 2 + 1]
            let ex = bx - ax, ey = by - ay
            let cr = ex * (y - ay) - ey * (x - ax)
            if cr > 0 { pos += 1 } else if cr < 0 { neg += 1 }
            let len2 = ex * ex + ey * ey
            let t = len2 > 0 ? min(max(((x - ax) * ex + (y - ay) * ey) / len2, 0), 1) : 0
            let px = ax + ex * t - x, py = ay + ey * t - y
            if px * px + py * py <= slack * slack { near = true }
        }
        return near || pos == 0 || neg == 0
    }

    /// A camada está no tempo do cabeçote?
    static func activeAt(_ row: LayerItem, _ frame: Int64) -> Bool {
        frame >= Int64(row.startFrame) && frame < Int64(row.endFrame)
    }

    /// Afim `a b c d tx ty`: parent → composição.
    static func applyAffine(_ a: [Float], _ x: Float, _ y: Float) -> (Float, Float) {
        if a.count < 6 { return (x, y) }
        return (a[0] * x + a[2] * y + a[4], a[1] * x + a[3] * y + a[5])
    }

    /// O caminho de volta (composição → parent), ou nil quando não é inversível.
    static func invertAffine(_ a: [Float], _ x: Float, _ y: Float) -> (Float, Float)? {
        if a.count < 6 { return nil }
        let det = a[0] * a[3] - a[1] * a[2]
        if abs(det) < 1e-6 { return nil }
        let dx = x - a[4], dy = y - a[5]
        return ((a[3] * dx - a[2] * dy) / det, (-a[1] * dx + a[0] * dy) / det)
    }
}

/// Mover o pivô sem a imagem pular (par do PivotDrag.kt). O motor desenha
/// `posição · R · S · (p − âncora)` (R = Rz no 2D, Rz·Ry·Rx no 3D): a posição
/// anda Δ e a âncora anda `(R·S)⁻¹·Δ`, e todo pixel fica onde estava.
enum PivotMath {
    /// Δâncora XYZ que compensa a posição andar (dx, dy, 0) no espaço do pai;
    /// nil quando a escala zera um eixo.
    static func anchorDelta(dx: Float, dy: Float, rotation: [Float], scale: [Float], threeD: Bool, depthFollowsWidth: Bool) -> [Float]? {
        func at(_ v: [Float], _ i: Int, _ fallback: Float) -> Float { v.count > i ? v[i] : fallback }
        let sx = at(scale, 0, 1), sy = at(scale, 1, 1)
        let sz: Float = !threeD ? 1 : depthFollowsWidth ? at(scale, 2, 1) * sx : at(scale, 2, 1)
        let rad = Double.pi / 180
        let x = threeD ? Double(at(rotation, 0, 0)) * rad : 0, y = threeD ? Double(at(rotation, 1, 0)) * rad : 0
        let z = Double(at(rotation, 2, 0)) * rad
        let (cx, sxr, cy, syr, cz, szr) = (cos(x), sin(x), cos(y), sin(y), cos(z), sin(z))
        // Linhas 0 e 1 de Rz·Ry·Rx; Rᵀ·(dx, dy, 0).
        let r0 = [cz * cy, cz * syr * sxr - szr * cx, cz * syr * cx + szr * sxr]
        let r1 = [szr * cy, szr * syr * sxr + cz * cx, szr * syr * cx - cz * sxr]
        var out = (0..<3).map { Float(r0[$0] * Double(dx) + r1[$0] * Double(dy)) }
        let s = [sx, sy, sz]
        for i in 0..<3 {
            if abs(out[i]) < 1e-9 { out[i] = 0; continue }
            if abs(s[i]) < 1e-6 { return nil }
            out[i] /= s[i]
        }
        if !threeD { out[2] = 0 }
        return out
    }
}

/// Um arrasto do pivô: valores do COMEÇO e escrita absoluta (nada acumula).
@MainActor struct PivotDragSession {
    let layer: Int64
    let start: SIMD2<Float>
    private let position: [Float], anchor: [Float], rotation: [Float], scale: [Float], affine: [Float]
    private let threeD: Bool, depthFollowsWidth: Bool
    private let parentStart: (Float, Float)

    init?(model: AureaModel, pivot: SIMD2<Float>) {
        guard let id = model.primarySelection, let row = model.selectedLayer, row.id == id else { return nil }
        let d = model.detail
        position = StageGeom.floats(d["position"]); anchor = StageGeom.floats(d["anchor"])
        rotation = StageGeom.floats(d["rotation"]); scale = StageGeom.floats(d["scale"])
        affine = StageGeom.floats(d["parentAffine"])
        guard position.count >= 2, anchor.count >= 2 else { return nil }
        layer = id; start = pivot
        threeD = row.threeD || [UInt32(8), 9, 10].contains(row.kind)
        depthFollowsWidth = row.kind != 8 && row.kind != 9
        parentStart = StageGeom.invertAffine(affine, pivot.x, pivot.y) ?? (pivot.x, pivot.y)
    }

    /// Onde o pivô está na composição: a origem do gizmo (3D) ou a posição
    /// levada pelo pai (2D — a âncora cai exatamente na posição).
    static func pivotPoint(_ model: AureaModel) -> SIMD2<Float>? {
        guard let id = model.primarySelection else { return nil }
        let g = model.engine.gizmo(id, length: ShellStageGeometry.gizmoLength).map(\.floatValue)
        if g.count == 8, g[0].isFinite, g[1].isFinite { return SIMD2(g[0], g[1]) }
        let p = StageGeom.floats(model.detail["position"])
        guard p.count >= 2 else { return nil }
        let c = StageGeom.applyAffine(StageGeom.floats(model.detail["parentAffine"]), p[0], p[1])
        return c.0.isFinite && c.1.isFinite ? SIMD2(c.0, c.1) : nil
    }

    /// Leva o pivô ao ponto da composição `to`; a posição compensa a âncora.
    func move(to target: SIMD2<Float>, model: AureaModel) {
        let p = StageGeom.invertAffine(affine, target.x, target.y) ?? (target.x, target.y)
        let dx = p.0 - parentStart.0, dy = p.1 - parentStart.1
        guard let da = PivotMath.anchorDelta(dx: dx, dy: dy, rotation: rotation, scale: scale,
                                             threeD: threeD, depthFollowsWidth: depthFollowsWidth) else { return }
        func at(_ v: [Float], _ i: Int) -> Float { v.count > i ? v[i] : 0 }
        model.setPivot(layer, anchor: (0..<3).map { at(anchor, $0) + da[$0] },
                       position: [at(position, 0) + dx, at(position, 1) + dy, at(position, 2)])
    }
}
