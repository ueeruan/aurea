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
    static let scaleHandleTarget: CGFloat = 26
    static let rotateHandleTarget: CGFloat = 22

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
    static let dockRowHeight: CGFloat = 54   // ícone + nome curto (Android: 54 dp)
    static let dockRowPad: CGFloat = 10
    static let dockRowGap: CGFloat = 8
    static let dockTileMin: CGFloat = 64
    static let dockTileMax: CGFloat = 96
    static let dockTileIcon: CGFloat = 27
    static let dockTileIconSmall: CGFloat = 22
    static let batchRowHeight: CGFloat = 52
    static let batchRowShort: CGFloat = 48
    static let addCategories: CGFloat = 68
    static let addTabWidth: CGFloat = 64
    static let addCardHeight: CGFloat = 80
    static let addCardIcon: CGFloat = 28
    static let addShapeInset: CGFloat = 12
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
    @Published var resolutionAnchor: CGRect?
    @Published var timeInput = ""
    @Published var grabbedHandle = -1
    @Published var grabbedShapeHandle = -1
    @Published var snapX: Float?
    @Published var snapY: Float?
    func dismiss() { sheet = nil; linkAnchor = nil; resolutionAnchor = nil }
}

enum ShellStageGeometry {
    static let gizmoLength: Float = 320
    static func handles(_ corners: [CGPoint], size: CGSize) -> [CGPoint] {
        guard corners.count == 4 else { return [] }
        let center = CGPoint(x: corners.map(\.x).reduce(0, +) / 4, y: corners.map(\.y).reduce(0, +) / 4)
        var result = [1, 2, 0, 3].map { k -> CGPoint in
            var dx = corners[k].x - center.x, dy = corners[k].y - center.y, distance = hypot(corners[k].x - center.x, corners[k].y - center.y)
            if distance >= 30 { return corners[k] }
            if distance < 0.001 { dx = k == 1 || k == 2 ? 1 : -1; dy = k >= 2 ? 1 : -1; distance = hypot(dx, dy) }
            return CGPoint(x: center.x + dx / distance * 30, y: center.y + dy / distance * 30)
        }
        if hypot(result[0].x - result[1].x, result[0].y - result[1].y) < 60 {
            let mid = (result[0].y + result[1].y) / 2; result[0].y = mid - 30; result[1].y = mid + 30
        }
        // The Metal view already has the original eight-point stage inset.
        return result.map { CGPoint(x: $0.x.clamped(to: 14...max(14, size.width - 14)), y: $0.y.clamped(to: 14...max(14, size.height - 14))) }
    }
    static func gizmoTips(_ points: [CGPoint]) -> [CGPoint] {
        guard points.count == 4 else { return [] }; var result = points
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
            default:
                var stem = Path(); stem.move(to: p(0.04, 0.5)); stem.addLine(to: p(0.66, 0.5))
                context.stroke(stem, with: fill, lineWidth: s * 0.2)
                context.fill(polygon([0.6, 0.2, 0.98, 0.5, 0.6, 0.8]), with: fill)
            }
        }
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
