import SwiftUI

func preferredCurveTrack(_ tracks: [[KeyframeItem]]) -> [KeyframeItem] {
    tracks.first(where: { track in track.count >= 2 && track.contains { $0.value != track[0].value } })
        ?? tracks.first(where: { $0.count >= 2 })
        ?? tracks.first(where: { !$0.isEmpty }) ?? []
}
import UIKit

private var curveGreen: Color { AureaColors.accent }
private var curvePanelFill: Color { AureaColors.editorPanel }
private var curveRailFill: Color { AureaColors.editorPanelHigh }

// CurvePanel.kt: easing descriptions for drawing and editing the core's keys.
// These samples draw the control; project evaluation remains in the C++ core.
struct CurveEase: Equatable {
    var interpolation: UInt32
    var x1: Float
    var y1: Float
    var x2: Float
    var y2: Float
    /// Força da bézier 1..3 (`Keyframe::easePower`): a mesma curva repetida sobre o resultado.
    var power: Int = 1
    static let linear = CurveEase(interpolation: 1, x1: 0, y1: 0, x2: 1, y2: 1)
    var isBezier: Bool { interpolation == 2 || interpolation == 6 }
    var hasHandles: Bool { isBezier || interpolation == 1 || interpolation == 3 || interpolation == 4 || interpolation == 5 }
    var handles: [Float] {
        switch interpolation {
        // A reta mostra a bézier equivalente: nas pontas as alças ficavam em cima das marcas.
        case 1: return [1 / 3, 1 / 3, 2 / 3, 2 / 3]
        case 3: return [1 / 3, 0, 2 / 3, 1 / 3]
        case 4: return [1 / 3, 2 / 3, 2 / 3, 1]
        case 5: return [0.5, 0, 0.5, 1]
        default: return [x1, y1, x2, y2]
        }
    }
    func transform(_ t: Float) -> Float {
        switch interpolation {
        case 0: return t < 1 ? 0 : 1
        case 1: return t
        case 3: return t * t
        case 4: return 1 - (1 - t) * (1 - t)
        case 5: return t < 0.5 ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t)
        case 7:
            let u = min(1, max(0,t))
            if u < 0.5 { return 4*u*u }
            let segment: (Float,Float,Float) = u < 0.75 ? (0.5,0.25,0.25) : u < 0.9 ? (0.75,0.15,0.0625) : (0.9,0.1,0.015625)
            let p = (u-segment.0)/segment.1
            return 1-4*segment.2*p*(1-p)
        case 8:
            if t <= 0 { return 0 }; if t >= 1 { return 1 }
            return Float((1-exp(-6*Double(t))*cos(6*Double.pi*Double(t)))/(1-exp(-6)))
        case 9: return floor(min(1,max(0,t))*4)/4
        default:
            // `keyframe_ease` (Curve.hpp): a força repete a MESMA bézier sobre o resultado.
            var u = bezierOnce(t)
            for _ in 1..<min(3, max(1, power)) { u = bezierOnce(u) }
            return u
        }
    }
    private func bezierOnce(_ t: Float) -> Float {
            if t <= 0 { return 0 }
            if t >= 1 { return 1 }
            func bezier(_ p: Float, _ q: Float, _ m: Double) -> Double {
                3 * Double(p) * (1 - m) * (1 - m) * m + 3 * Double(q) * (1 - m) * m * m + m * m * m
            }
            var low = 0.0, high = 1.0, parameter = Double(t)
            for _ in 0..<28 {
                let error = bezier(x1, x2, parameter) - Double(t)
                if error == 0 { return Float(bezier(y1, y2, parameter)) }
                if error < 0 { low = parameter } else { high = parameter }
                let m = 1 - parameter
                let slope = 3 * m * m * Double(x1) + 6 * m * parameter * (Double(x2) - Double(x1)) + 3 * parameter * parameter * (1 - Double(x2))
                var next = (low + high) * 0.5
                if abs(slope) > 1e-12 {
                    let candidate = parameter - error / slope
                    if candidate > low && candidate < high { next = candidate }
                }
                if abs(next - parameter) < 1e-9 { return Float(bezier(y1, y2, next)) }
                parameter = next
            }
            return Float(bezier(y1, y2, parameter))
    }
    func same(_ other: CurveEase) -> Bool {
        interpolation == other.interpolation && (!isBezier || (power == other.power &&
            abs(x1 - other.x1) < 0.01 && abs(y1 - other.y1) < 0.01 && abs(x2 - other.x2) < 0.01 && abs(y2 - other.y2) < 0.01))
    }
    var inverted: CurveEase? {
        switch interpolation {
        case 3: var result = self; result.interpolation = 4; return result
        case 4: var result = self; result.interpolation = 3; return result
        case 2, 6:
            let result = CurveEase(interpolation: 2, x1: 1 - x2, y1: 1 - y2, x2: 1 - x1, y2: 1 - y1, power: power)
            if abs(result.x1 - x1) < 0.01 && abs(result.y1 - y1) < 0.01 && abs(result.x2 - x2) < 0.01 && abs(result.y2 - y2) < 0.01 { return nil }
            return result
        default: return nil
        }
    }
    var name: String {
        if let preset = CurvePresetItem.builtins.first(where: { same($0.ease) }) { return preset.name }
        let key: String
        switch interpolation {
        case 0: key = "pn_ease_hold"
        case 3: key = "pn_ease_in"
        case 4: key = "pn_ease_out"
        case 5: key = "pn_ease_in_out"
        case 7: key = "pn_textpreset_bounce"
        case 8: key = "pn_textpreset_elastic"
        case 9: key = "pn_curve_steps4"
        default: key = "pn_ease_bezier_custom"
        }
        return AureaText.t(key)
    }
}

private struct CurvePresetItem: Identifiable {
    let id: String
    let name: String
    let ease: CurveEase
    var stored = false
    static var builtins: [CurvePresetItem] {
        [CurvePresetItem(id: "linear", name: AureaText.t("panel_linear"), ease: .linear),
         CurvePresetItem(id: "in", name: AureaText.t("pn_ease_in"), ease: CurveEase(interpolation: 2, x1: 0.42, y1: 0, x2: 1, y2: 1)),
         CurvePresetItem(id: "out", name: AureaText.t("pn_ease_out"), ease: CurveEase(interpolation: 2, x1: 0, y1: 0, x2: 0.58, y2: 1)),
         CurvePresetItem(id: "inout", name: AureaText.t("pn_ease_in_out"), ease: CurveEase(interpolation: 2, x1: 0.42, y1: 0, x2: 0.58, y2: 1)),
         CurvePresetItem(id: "hold", name: AureaText.t("pn_ease_hold"), ease: CurveEase(interpolation: 0, x1: 0, y1: 0, x2: 1, y2: 1))]
    }
    static func read(_ object: [String: Any], id: String) -> CurvePresetItem? {
        guard object["kind"] as? String == "curve", let curve = object["curve"] as? [String: NSNumber],
              let interpolation = curve["interp"]?.uint32Value, interpolation <= 9 else { return nil }
        let h: [Float] = [curve["x1"]?.floatValue ?? 0.33, curve["y1"]?.floatValue ?? 0,
                 curve["x2"]?.floatValue ?? 0.67, curve["y2"]?.floatValue ?? 1]
        guard h.allSatisfy({ $0.isFinite }) else { return nil }
        return CurvePresetItem(id: id, name: object["name"] as? String ?? AureaText.t("panel_curva"),
            ease: CurveEase(interpolation: interpolation, x1: h[0], y1: h[1], x2: h[2], y2: h[3]), stored: true)
    }
}

/// Presets prontos do menu "Presets" (CurvePanel.kt `StandardCurves`): menu
/// compacto, não blocos. O Bounce segue no botão próprio.
let curveStandardPresets: [(key: String, ease: CurveEase)] = [
    ("pn_curve_std_smooth", CurveEase(interpolation: 2, x1: 0.33, y1: 0, x2: 0.66, y2: 1)),
    ("pn_curve_std_in", CurveEase(interpolation: 2, x1: 0.42, y1: 0, x2: 1, y2: 1)),
    ("pn_curve_std_out", CurveEase(interpolation: 2, x1: 0, y1: 0, x2: 0.58, y2: 1)),
    ("pn_curve_std_overshoot", CurveEase(interpolation: 2, x1: 0.34, y1: 1.56, x2: 0.64, y2: 1)),
    ("pn_curve_std_anticipate", CurveEase(interpolation: 2, x1: 0.36, y1: 0, x2: 0.66, y2: -0.56)),
]

/// [interp, x1, y1, x2, y2, força] do preset salvo (CurvePanel.kt `curvePresetValues`).
func curvePresetValues(_ ease: CurveEase) -> [Float] {
    let h = ease.handles
    return [Float(ease.interpolation), h[0], h[1], h[2], h[3], Float(ease.isBezier ? ease.power : 1)]
}

/// O inverso (PresetsPanel.kt `curvePresetEase`); preset antigo sem força = ×1.
func curvePresetEase(_ values: [Float]) -> CurveEase? {
    guard values.count >= 5, values.allSatisfy({ $0.isFinite }), values[0] >= 0, values[0] <= 9 else { return nil }
    let power = values.count >= 6 ? min(3, max(1, Int(values[5]))) : 1
    return CurveEase(interpolation: UInt32(values[0]), x1: values[1], y1: values[2], x2: values[3], y2: values[4], power: power)
}

/// Curva só existe entre 2 marcas (GraphCurve.kt `curveEditable`).
func curveEditable(_ track: [KeyframeItem]) -> Bool { track.count >= 2 }

/// "Aplicar a todos os keyframes desta propriedade" (GraphCurve.kt
/// `propertySegmentStarts`): o início de cada trecho de cada trilha do grupo
/// (X/Y/Z, componentes do parâmetro); [] = menos de 2 marcas.
func curvePropertySegmentStarts(_ keys: [KeyframeItem], _ track: [KeyframeItem]) -> [KeyframeItem] {
    guard curveEditable(track) else { return [] }
    var seen = Set<String>()
    return graphGroup(keys, track).flatMap { lane in lane.sorted { $0.time < $1.time }.dropLast() }
        .filter { seen.insert($0.id).inserted }
}

@MainActor
private enum CurveClipboard { static var ease: CurveEase? }

func curveSameTrack(_ a: KeyframeItem, _ b: KeyframeItem) -> Bool {
    a.property == b.property && (a.property < 31 ||
        (a.effectIndex == b.effectIndex && a.paramIndex == b.paramIndex))
}

/// Onde as duas alças são DESENHADAS e tocadas (CurveMath.kt `separatedHandles`):
/// as posições reais, afastadas quando ficam mais perto que `minimum` — a alça de
/// saída e a de chegada nunca viram um borrão só. Coincidentes, abrem na direção
/// primeira marca → segunda, cada uma para o lado da própria marca.
func curveSeparatedHandles(_ first: CGPoint, _ second: CGPoint, start: CGPoint, end: CGPoint, minimum: CGFloat) -> (CGPoint, CGPoint) {
    var dx = second.x - first.x, dy = second.y - first.y
    var distance = hypot(dx, dy)
    if distance >= minimum { return (first, second) }
    var gap = distance
    if distance < 0.5 {
        dx = end.x - start.x; dy = end.y - start.y
        distance = hypot(dx, dy)
        if distance < 0.001 { dx = 1; dy = 0; distance = 1 }
        gap = 0
    }
    let push = (minimum - gap) / 2, ux = dx / distance, uy = dy / distance
    return (CGPoint(x: first.x - ux * push, y: first.y - uy * push), CGPoint(x: second.x + ux * push, y: second.y + uy * push))
}

/// A alça sob o dedo: 0 (saída), 1 (chegada) ou −1; a mais perto vence, dentro de `radius`.
func curveNearestHandle(_ location: CGPoint, _ shown: (CGPoint, CGPoint), radius: CGFloat) -> Int {
    let d1 = pow(location.x - shown.0.x, 2) + pow(location.y - shown.0.y, 2)
    let d2 = pow(location.x - shown.1.x, 2) + pow(location.y - shown.1.y, 2)
    if min(d1, d2) > radius * radius { return -1 }
    return d1 <= d2 ? 0 : 1
}

/// A alça que o toque pega (CurveMath.kt `grabHandle`): a mais perto das duas, sempre uma.
func curveGrabHandle(_ location: CGPoint, _ shown: (CGPoint, CGPoint)) -> Int {
    let d1 = pow(location.x - shown.0.x, 2) + pow(location.y - shown.0.y, 2)
    let d2 = pow(location.x - shown.1.x, 2) + pow(location.y - shown.1.y, 2)
    return d1 <= d2 ? 0 : 1
}

/// Faixa vertical das alças (CurveMath.kt): além de 0..1 dá antecipação e overshoot.
let curveEaseYMin: Float = -2
let curveEaseYMax: Float = 3

/// Faixa vertical do gráfico (CurveMath.kt `easeRange`): curva e alças, contendo 0..1, com 8 % de folga.
func curveEaseRange(_ ease: CurveEase) -> (Float, Float) {
    var lo: Float = 0, hi: Float = 1
    for index in 0...40 {
        let v = ease.transform(Float(index) / 40)
        if v.isFinite { lo = min(lo, v); hi = max(hi, v) }
    }
    if ease.hasHandles {
        let h = ease.handles
        lo = min(lo, min(h[1], h[3])); hi = max(hi, max(h[1], h[3]))
    }
    let pad = (hi - lo) * 0.08
    return (lo - pad, hi + pad)
}

/// Encosta em 0 ou 1 quando está a menos de `tolerance` (CurveMath.kt `snapUnit`).
func curveSnapUnit(_ v: Float, _ tolerance: Float) -> Float {
    if abs(v) < tolerance { return 0 }
    if abs(v - 1) < tolerance { return 1 }
    return v
}

/// A posição da alça para o dedo (CurveMath.kt `handleAt`): x 0..1, y −2..3, encaixe em 0 e 1.
func curveHandleAt(_ point: CGPoint, inset: CGFloat, size: CGSize, low: Float, high: Float, snap: CGFloat) -> (Float, Float) {
    let w = max(1, size.width - 2 * inset), h = max(1, size.height)
    let span = high - low > 0.001 ? high - low : 0.001
    let x = curveSnapUnit(Float((point.x - inset) / w).clamped(to: 0...1), Float(snap / w))
    let y = curveSnapUnit((low + Float((h - point.y) / h) * span).clamped(to: curveEaseYMin...curveEaseYMax), Float(snap / h) * span)
    return (x, y)
}

/// O texto do trecho (CurveMath.kt `cubicBezierLabel`).
func cubicBezierLabel(_ ease: CurveEase) -> String {
    let h = ease.handles
    let label = "cubic-bezier(" + h.map { String(format: "%.2f", locale: Locale(identifier: "en_US_POSIX"), $0) }.joined(separator: ", ") + ")"
    return ease.isBezier && ease.power > 1 ? label + " ×\(ease.power)" : label
}

/// s^power com o sinal de s (SpeedGraphHandles.kt `powered`).
func curvePowered(_ s: Double, _ power: Int) -> Double {
    var out = s
    for _ in 1..<min(3, max(1, power)) { out *= s }
    return out
}

/// A inversa: raiz `power` com sinal (par e negativo = o espelho positivo).
func curveRooted(_ v: Double, _ power: Int) -> Double {
    let p = min(3, max(1, power))
    if p == 1 { return v }
    let r = pow(abs(v), 1.0 / Double(p))
    return v < 0 && p % 2 == 1 ? -r : r
}

/// Próxima força ao tocar no texto (CurveMath.kt `nextPower`): ×1 → ×2 → ×3 → ×1.
func curveNextPower(_ ease: CurveEase) -> CurveEase {
    let h = ease.handles
    return CurveEase(interpolation: 2, x1: h[0], y1: h[1], x2: h[2], y2: h[3], power: ease.isBezier ? ease.power % 3 + 1 : 2)
}

private let curveInset: CGFloat = 28
/// Raio do halo da alça no dedo.
private let curveHandleHit: CGFloat = 28
/// Distância mínima entre as alças DESENHADAS (as bolas nunca se sobrepõem).
private let curveHandleSeparation: CGFloat = 30
/// Encaixe da alça nas linhas 0 e 1 (x e y).
private let curveHandleSnap: CGFloat = 10

private struct NativeCurveGraph: View {
    let ease: CurveEase
    let progress: Float?
    let onBegin: () -> Void
    let onChange: (CurveEase) -> Void
    let onEnd: () -> Void
    @State private var drag: HandleDrag?
    @GestureState private var touching = false
    private struct HandleDrag {
        var first: Bool
        var low: Float
        var high: Float
        var ease: CurveEase
        var began = false
    }
    // Faixa vertical ajustada à curva e às alças; parada enquanto o dedo arrasta.
    private var low: Float { drag?.low ?? curveEaseRange(ease).0 }
    private var high: Float { drag?.high ?? curveEaseRange(ease).1 }
    var body: some View {
        GeometryReader { geometry in
            ZStack {
                Canvas { context, size in draw(context, size) }
                Canvas { context, size in
                    if let progress {
                        // A linha do cabeçote e o ponto onde ele está NA curva.
                        let p = plot(progress, ease.transform(progress), size, low, high)
                        var line = Path(); line.move(to: CGPoint(x: p.x, y: 0)); line.addLine(to: CGPoint(x: p.x, y: size.height))
                        context.stroke(line, with: .color(.white.opacity(0.4)), style: StrokeStyle(lineWidth: 1, dash: [2, 4]))
                        context.fill(Path(ellipseIn: CGRect(x: p.x - 4, y: p.y - 4, width: 8, height: 8)), with: .color(.white))
                    }
                }
            }.contentShape(Rectangle()).accessibilityIdentifier("curve.easingGraph").gesture(DragGesture(minimumDistance: 0)
                .updating($touching) { _, active, _ in active = true }
                .onChanged { value in move(value, size: geometry.size) }
                .onEnded { _ in finish() })
        }
        .onChange(of: touching) { active in if !active { finish() } }
        .onDisappear { finish() }
    }
    private func plot(_ x: Float, _ y: Float, _ size: CGSize, _ lo: Float, _ hi: Float) -> CGPoint {
        CGPoint(x: curveInset + CGFloat(x) * max(1, size.width - 2 * curveInset), y: size.height - CGFloat((y - lo) / (hi - lo)) * size.height)
    }
    private func draw(_ context: GraphicsContext, _ size: CGSize) {
        func point(_ x: Float, _ y: Float) -> CGPoint { plot(x, y, size, low, high) }
        func line(_ a: CGPoint, _ b: CGPoint, _ color: Color, _ width: CGFloat, _ dash: [CGFloat] = []) {
            var path = Path(); path.move(to: a); path.addLine(to: b)
            context.stroke(path, with: .color(color), style: StrokeStyle(lineWidth: width, dash: dash))
        }
        func dot(_ p: CGPoint, radius: CGFloat, color: Color) {
            context.fill(Path(ellipseIn: CGRect(x: p.x - radius, y: p.y - radius, width: radius * 2, height: radius * 2)), with: .color(color))
        }
        for index in 1..<32 {
            let x = size.width * CGFloat(index) / 32, y = size.height * CGFloat(index) / 32
            line(CGPoint(x: x, y: 0), CGPoint(x: x, y: size.height), .white.opacity(0.045), 0.6)
            line(CGPoint(x: 0, y: y), CGPoint(x: size.width, y: y), .white.opacity(0.045), 0.6)
        }
        for y in [Float(0), Float(1)] { line(point(0, y), point(1, y), .white.opacity(0.24), 1, [4.5, 4.5]) }
        let base = point(0, 0).y
        var curve = Path(), area = Path(); area.move(to: CGPoint(x: 0, y: base))
        for index in 0...72 {
            let t = Float(index) / 72, p = point(t, ease.transform(t))
            if index == 0 { curve.move(to: p) } else { curve.addLine(to: p) }
            area.addLine(to: p)
        }
        area.addLine(to: CGPoint(x: size.width, y: base)); area.closeSubpath()
        context.fill(area, with: .color(curveGreen.opacity(0.025)))
        context.stroke(curve, with: .color(curveGreen), style: StrokeStyle(lineWidth: 3.5, lineCap: .round))
        let start = point(0, 0), end = point(1, 1)
        if ease.hasHandles {
            let h = ease.handles
            let (first, second) = curveSeparatedHandles(point(h[0], h[1]), point(h[2], h[3]), start: start, end: end, minimum: curveHandleSeparation)
            for p in [first, second] {
                line(CGPoint(x: p.x, y: min(p.y, base)), CGPoint(x: p.x, y: max(p.y, base)), .white.opacity(0.3), 1, [3, 3])
            }
            // Cada alça ligada à SUA marca: a de saída (cheia) à primeira, a de
            // chegada (anel) à segunda — dá para saber qual é qual.
            line(start, first, .white, 2.5); line(end, second, .white.opacity(0.7), 2.5)
            if let drag { dot(drag.first ? first : second, radius: curveHandleHit * 0.85, color: curveGreen.opacity(0.22)) }
            dot(first, radius: 11, color: .white)
            dot(second, radius: 9, color: curvePanelFill)
            context.stroke(Path(ellipseIn: CGRect(x: second.x - 9, y: second.y - 9, width: 18, height: 18)), with: .color(.white), lineWidth: 3)
        }
        dot(start, radius: 5, color: curveGreen); dot(end, radius: 5, color: curveGreen)
    }
    private func move(_ value: DragGesture.Value, size: CGSize) {
        guard size.width > 0, size.height > 0 else { return }
        if drag == nil {
            guard ease.hasHandles else { return }
            let h = ease.handles
            let a = plot(h[0], h[1], size, low, high), b = plot(h[2], h[3], size, low, high)
            // Qualquer toque no gráfico pega a alça mais perto de onde ela está
            // DESENHADA (afastadas se coincidem) — como no app antigo.
            let shown = curveSeparatedHandles(a, b, start: plot(0, 0, size, low, high), end: plot(1, 1, size, low, high), minimum: curveHandleSeparation)
            drag = HandleDrag(first: curveGrabHandle(value.startLocation, shown) == 0, low: low, high: high,
                ease: CurveEase(interpolation: 2, x1: h[0], y1: h[1], x2: h[2], y2: h[3], power: ease.isBezier ? ease.power : 1))
        }
        guard var current = drag else { return }
        // Um toque sem arrasto não mexe em nada.
        if !current.began { guard hypot(value.translation.width, value.translation.height) >= 3 else { return } }
        // A alça vai para o dedo (x 0..1, y −2..3), encaixando em 0 e 1.
        let (x, y) = curveHandleAt(value.location, inset: curveInset, size: size, low: current.low, high: current.high, snap: curveHandleSnap)
        guard x.isFinite, y.isFinite else { return }
        if !current.began { current.began = true; onBegin() }
        if current.first { current.ease.x1 = x; current.ease.y1 = y } else { current.ease.x2 = x; current.ease.y2 = y }
        drag = current; onChange(current.ease)
    }
    private func finish() {
        let began = drag?.began == true; drag = nil
        if began { onEnd() }
    }
}

@MainActor
struct NativeCurvePanel: View {
    @EnvironmentObject private var model: AureaModel
    @Environment(\.dismiss) private var dismissExpanded
    var expanded = false
    @State private var fullscreen = false
    @State private var ease = CurveEase.linear
    /// As curvas que a pessoa salvou (Presets › Curva).
    @State private var saved: [(name: String, ease: CurveEase)] = []
    private var graphMode: Int { model.curveGraphMode }
    private struct Segment {
        let start: KeyframeItem
        let end: KeyframeItem
        let index: Int
        var id: String { start.id }
    }
    private var layer: Int64 { model.primarySelection ?? 0 }
    private var track: [KeyframeItem] {
        (model.keyframes[layer] ?? []).filter {
            $0.property == model.curveProperty && (model.curveProperty < 31 ||
                ($0.effectIndex == model.curveEffect && $0.paramIndex == model.curveParam))
        }.sorted { $0.time < $1.time }
    }
    private var segment: Segment? {
        let keys = track
        guard keys.count >= 2, let selected = model.curveSelectedTime,
              let selectedIndex = keys.firstIndex(where: { $0.time == selected }) else { return nil }
        let index = min(selectedIndex, keys.count - 2)
        return Segment(start: keys[index], end: keys[index + 1], index: index)
    }
    private func progress(_ segment: Segment) -> Float? {
        let frame = model.localPlayhead
        guard frame >= segment.start.time, frame < segment.end.time, segment.end.time > segment.start.time else { return nil }
        return Float(Int64(frame) - Int64(segment.start.time)) / Float(Int64(segment.end.time) - Int64(segment.start.time))
    }
    var body: some View {
        VStack(spacing: 0) {
            if let segment {
                HStack(spacing: 0) {
                    ForEach(0..<3, id: \.self) { index in
                        Button { model.curveGraphMode = index } label: {
                            Text(AureaText.t(["panel_easing_curve", "particular_curve_value", "panel_velocidade"][index]))
                                .font(.aurea(size: 12)).lineLimit(1)
                                .foregroundStyle(graphMode == index ? AureaColors.accent : AureaColors.muted)
                                .frame(maxWidth: .infinity).frame(height: 48).contentShape(Rectangle())
                        }.buttonStyle(.plain).accessibilityIdentifier("curve.mode.\(index)")
                    }
                }.background(curveRailFill, ignoresSafeAreaEdges: [])
                HStack(spacing: 0) {
                    leftRail(segment)
                    VStack(spacing: 0) {
                        if graphMode == 0 {
                            NativeCurveGraph(ease: ease, progress: progress(segment),
                                onBegin: { model.beginGesture("curva") },
                                onChange: { apply($0, to: segment.start); model.refreshModel(force: true) },
                                onEnd: { model.endGesture() })
                                .id("\(layer):\(segment.id)")
                        } else {
                            NativeTrackGraph(layer: layer, keys: track, speed: graphMode == 2)
                        }
                        if graphMode == 0 { presetRow(segment) }
                        segmentNavigation(segment)
                    }.frame(maxWidth: .infinity, maxHeight: .infinity)
                }.frame(maxHeight: .infinity)
            } else {
                VStack(spacing: 12) {
                    Text(AureaText.t(model.curveSelectedTime == nil ? "panel_toque_num_keyframe_timeline_npara_editar" : "panel_crie_pelo_menos_2_keyframes_npara"))
                        .font(.aurea(size: 13)).foregroundStyle(AureaColors.muted).multilineTextAlignment(.center)
                    Button(action: back) {
                        Text(AureaText.t("panel_voltar")).font(.aurea(size: 15)).foregroundStyle(AureaColors.accent)
                            .padding(.horizontal, 16).padding(.vertical, 8)
                    }.buttonStyle(AureaPressStyle(shrink: 1))
                }.frame(maxWidth: .infinity, maxHeight: .infinity)
                // Propriedade com menos de 2 keyframes: avisa e volta, venha de onde vier.
                .onAppear {
                    if model.curveSelectedTime != nil && !curveEditable(track) {
                        model.toast = AureaText.t("pn_curve_need_two_keys"); back()
                    }
                }
            }
        }.clipped().contentShape(Rectangle())
        .background(curvePanelFill, ignoresSafeAreaEdges: [])
        .accessibilityElement(children: .contain).accessibilityIdentifier("curve.panel")
        .overlay {
            if expanded {
                ZStack {
                    if let request = model.actionSheet {
                        AureaActionSheet(title: request.title, actions: request.actions) { model.actionSheet = nil }
                    }
                    if let request = model.namePrompt {
                        AureaNamePrompt(title: request.title, initial: request.initial, onConfirm: request.onConfirm,
                            onDismiss: { model.namePrompt = nil }).id(request.id)
                    }
                }
            }
        }
        .fullScreenCover(isPresented: $fullscreen) { NativeCurvePanel(expanded: true).environmentObject(model).interactiveDismissDisabled() }
        .onAppear { load(); loadSaved() }
        .onChange(of: model.status.modelRevision) { _ in load() }
        .onChange(of: segment?.id) { _ in load() }
        .onChange(of: layer) { _ in load() }
        // O gráfico segue o que a pessoa olha, não o que estava aberto quando o
        // painel abriu: outra camada escolhida traz a trilha animada dela; o
        // cabeçote parado noutro trecho (scrub, rolar a timeline) escolhe esse trecho.
        .onChange(of: model.primarySelection) { _ in followLayer() }
        .onChange(of: model.status.playhead) { _ in if model.status.playing == 0 { followPlayhead() } }
        .onChange(of: model.status.playing) { playing in if playing == 0 { followPlayhead() } }
    }
    private func followLayer() {
        guard model.primarySelection != nil else { return }
        let current = track
        if current.count >= 2 {
            // A mesma trilha anima aqui: fica nela, no trecho do cabeçote (se a marca escolhida não é desta camada).
            if let time = model.curveSelectedTime, current.contains(where: { $0.time == time }) { return }
            model.curveSelectedTime = current[max(0, graphSegmentIndexAt(current, model.localPlayhead))].time
            return
        }
        let like = KeyframeItem(property: model.curveProperty, paramIndex: model.curveParam, effectIndex: model.curveEffect,
                                time: 0, value: 0, interpolation: 1)
        let next = graphTrackFor(model.keyframes[layer] ?? [], like: like)
        guard let head = next.first else { model.curveSelectedTime = nil; return }
        model.curveProperty = head.property; model.curveEffect = head.effectIndex; model.curveParam = head.paramIndex
        model.curveSelectedTime = next[max(0, graphSegmentIndexAt(next, model.localPlayhead))].time
    }
    private func followPlayhead() {
        let keys = track
        guard let time = model.curveSelectedTime, keys.count >= 2 else { return }
        let want = graphSegmentIndexAt(keys, model.localPlayhead)
        if want >= 0 && want != graphSegmentIndexOf(keys, time) { model.curveSelectedTime = keys[want].time }
    }
    private func back() { if expanded { dismissExpanded() } else { model.openPanel(model.curveReturnPanel == .curve ? .none : model.curveReturnPanel) } }
    private func load() {
        guard let key = segment?.start else { return }
        let h = model.engine.trackEasing(layer, property: key.property, effect: key.effectIndex, param: key.paramIndex, time: key.time).map(\.floatValue)
        ease = CurveEase(interpolation: key.interpolation, x1: h.count >= 4 ? h[0] : 0.33,
            y1: h.count >= 4 ? h[1] : 0, x2: h.count >= 4 ? h[2] : 0.67, y2: h.count >= 4 ? h[3] : 1,
            power: h.count >= 5 ? min(3, max(1, Int(h[4]))) : 1)
    }
    private func apply(_ value: CurveEase, to key: KeyframeItem) {
        // 3D transform axes share easing; effect components remain independent.
        for sibling in model.graphKeyGroup(layer, key) {
            model.engine.editTrackKey(layer, property: sibling.property, effect: sibling.effectIndex, param: sibling.paramIndex,
                time: sibling.time, action: 3, value: sibling.value, targetTime: sibling.time,
                interpolation: value.interpolation,
                handles: [value.x1, value.y1, value.x2, value.y2, Float(value.power)].map { NSNumber(value: $0) })
        }
        ease = value
    }
    private func set(_ value: CurveEase, to key: KeyframeItem, label: String = "curva") {
        model.beginGesture(label); apply(value, to: key); model.endGesture()
    }
    private func leftRail(_ segment: Segment) -> some View {
        VStack(spacing: 0) {
            Spacer().frame(height: 6)
            glyphButton(CupertinoGlyph.ChevronBack, size: 24, target: 44, label: AureaText.t("panel_voltar"), action: back)
            Spacer(minLength: 0)
            glyphButton(CupertinoGlyph.ArrowRightArrowLeft, size: 20, target: 44, label: AureaText.t("panel_inverter_curva")) {
                if let inverted = ease.inverted { set(inverted, to: segment.start, label: "inverter curva") }
                else { model.toast = AureaText.t("pn_curve_symmetric") }
            }.disabled(!(1...6).contains(ease.interpolation))
                .opacity((1...6).contains(ease.interpolation) ? 1 : 0.35)
            Spacer().frame(height: 4)
            RailMoreButton(active: false) { showMenu(segment) }
            Spacer().frame(height: 8)
        }.frame(width: 44).frame(maxHeight: .infinity).background(curveRailFill, ignoresSafeAreaEdges: [])
    }
    private func glyphButton(_ glyph: Character, size: CGFloat, target: CGFloat, label: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            CupertinoGlyph.text(glyph, size: size, color: .white)
                .frame(width: target, height: target).contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityLabel(label)
    }
    private func segmentNavigation(_ segment: Segment) -> some View {
        HStack(spacing: 4) {
            glyphButton(CupertinoGlyph.ChevronLeft, size: 16, target: 44, label: AureaText.t("panel_keyframe_anterior")) { jump(-1) }
            // A curva do trecho em números; tocar troca a força (×1 → ×2 → ×3).
            Text(graphMode == 0 ? (ease.hasHandles ? cubicBezierLabel(ease) : ease.name)
                 : AureaText.t(graphMode == 1 ? "particular_curve_value" : "panel_velocidade"))
                .font(.aurea(size: 10)).foregroundStyle(.white.opacity(0.6))
                .lineLimit(1).truncationMode(.tail).multilineTextAlignment(.center)
                .frame(minHeight: 44).contentShape(Rectangle())
                .onTapGesture { if graphMode == 0 && ease.hasHandles { set(curveNextPower(ease), to: segment.start, label: "força da curva") } }
                .accessibilityIdentifier("curve.power")
            glyphButton(CupertinoGlyph.ChevronRight, size: 16, target: 44, label: AureaText.t("panel_proximo_keyframe")) { jump(1) }
            if graphMode == 0 { bounceButton(segment) }
        }.frame(maxWidth: .infinity).frame(height: 44)
    }
    /// O único preset pronto que fica: Bounce (a bézier se faz nas alças).
    /// Tocar de novo volta a uma bézier suave, com alças.
    private func bounceButton(_ segment: Segment) -> some View {
        let bouncing = ease.interpolation == 7
        return Button {
            set(bouncing ? CurveEase(interpolation: 2, x1: 0.42, y1: 0, x2: 0.58, y2: 1)
                         : CurveEase(interpolation: 7, x1: 0, y1: 0, x2: 1, y2: 1), to: segment.start)
        } label: {
            Text(AureaText.t("pn_textpreset_bounce")).font(.aurea(size: 12, weight: .semibold)).lineLimit(1)
                .foregroundStyle(bouncing ? curveGreen : .white)
                .padding(.horizontal, 14).padding(.vertical, 7)
                .background(bouncing ? curveGreen.opacity(0.18) : curveRailFill, in: Capsule())
                .overlay(Capsule().stroke(bouncing ? curveGreen : .white.opacity(0.18), lineWidth: 1))
                .frame(minHeight: 44).contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle(shrink: 1)).padding(.leading, 4).padding(.trailing, 8)
            .accessibilityLabel(AureaText.t("pn_textpreset_bounce")).accessibilityIdentifier("curve.preset.bounce")
    }
    private func jump(_ direction: Int) {
        guard let segment else { return }
        let keys = track, target = (segment.index + direction).clamped(to: 0...max(0, track.count - 2))
        guard target != segment.index, target + 1 < keys.count else { return }
        model.curveSelectedTime = keys[target].time
        if model.status.playing != 0 { model.playPause() }
        let local = Int64(keys[target].time) + (Int64(keys[target + 1].time) - Int64(keys[target].time)) / 2
        model.seek(toFrame: local + Int64(model.selectedLayer?.startFrame ?? 0) - Int64(model.selectedLayer?.offsetFrames ?? 0))
        load()
    }
    private func showMenu(_ segment: Segment) {
        let value = ease
        let copied = CurveClipboard.ease
        let actions: [SheetAction] = [
            SheetAction(AureaText.t("panel_curva")) { model.curveGraphMode = 0 },
            SheetAction(AureaText.t("particular_curve_value")) { model.curveGraphMode = 1 },
            SheetAction(AureaText.t("panel_velocidade")) { model.curveGraphMode = 2 },
            SheetAction(AureaText.t(expanded ? "editor_sair_tela_cheia" : "panel_expandir")) { if expanded { dismissExpanded() } else { fullscreen = true } },
            SheetAction(AureaText.t("panel_copiar_curva")) { CurveClipboard.ease = value },
            SheetAction(AureaText.t("panel_salvar_curva_como_preset")) {
                model.namePrompt = NamePromptRequest(title: AureaText.t("panel_salvar_curva_como_preset"), initial: value.name) { savePreset(name: $0, ease: value) }
            },
            SheetAction(AureaText.t("panel_colar_curva"), enabled: copied != nil) {
                if let copied { set(copied, to: segment.start, label: "colar curva") }
            },
            // Todos os trechos da propriedade (e dos eixos do grupo): um desfazer.
            SheetAction(AureaText.t("panel_curve_apply_property")) {
                let starts = curvePropertySegmentStarts(model.keyframes[layer] ?? [], track)
                guard !starts.isEmpty else { model.toast = AureaText.t("pn_curve_need_two_keys"); return }
                model.beginGesture("curva na propriedade")
                for key in starts { apply(value, to: key) }
                model.endGesture()
            }
        ]
        model.actionSheet = ActionSheetRequest(title: AureaText.t("panel_curva"), actions: actions)
    }
    /// Presets prontos (menu), salvar a curva e as curvas salvas numa fileira
    /// fina que rola: o painel não cresce.
    private func presetRow(_ segment: Segment) -> some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 6) {
                presetChip(AureaText.t("panel_presets") + " ▾", id: "curve.presets") { showStandardPresets(segment) }
                presetChip("+ " + AureaText.t("pn_curve_save"), id: "curve.save") {
                    let value = ease
                    model.namePrompt = NamePromptRequest(title: AureaText.t("panel_salvar_curva_como_preset"), initial: value.name) { savePreset(name: $0, ease: value) }
                }
                ForEach(Array(saved.enumerated()), id: \.offset) { _, item in
                    presetChip(item.name, active: ease.same(item.ease), id: "curve.saved") { set(item.ease, to: segment.start, label: "preset de curva") }
                }
            }.padding(.horizontal, 8)
        }.frame(height: 40)
    }
    private func presetChip(_ label: String, active: Bool = false, id: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(label).font(.aurea(size: 12, weight: .semibold)).lineLimit(1).truncationMode(.tail)
                .foregroundStyle(active ? curveGreen : .white)
                .padding(.horizontal, 12).padding(.vertical, 6)
                .frame(maxWidth: 140)
                .background(active ? curveGreen.opacity(0.18) : curveRailFill, in: Capsule())
                .overlay(Capsule().stroke(active ? curveGreen : .white.opacity(0.18), lineWidth: 1))
                .frame(minHeight: 40).contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityIdentifier(id)
    }
    private func showStandardPresets(_ segment: Segment) {
        let actions = curveStandardPresets.map { item in
            SheetAction(AureaText.t(item.key)) { set(item.ease, to: segment.start, label: "preset de curva") }
        }
        model.actionSheet = ActionSheetRequest(title: AureaText.t("panel_presets"), actions: actions)
    }
    /// Lê as curvas salvas (arquivos de Presets › Curva, validados pelo motor).
    private func loadSaved() {
        let directory = PanelPresetKind.curve.directory
        let files = ((try? FileManager.default.contentsOfDirectory(at: directory, includingPropertiesForKeys: nil)) ?? [])
            .filter { $0.pathExtension == "json" }
            .sorted { $0.lastPathComponent.lowercased() < $1.lastPathComponent.lowercased() }
        saved = files.compactMap { url in
            guard let source = try? String(contentsOf: url, encoding: .utf8),
                  let value = curvePresetEase(model.engine.parseCurvePreset(source).map(\.floatValue)) else { return nil }
            return (url.deletingPathExtension().lastPathComponent, value)
        }
    }
    private func savePreset(name: String, ease: CurveEase) {
        let title = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !title.isEmpty else { return }
        defer { loadSaved() }
        let h = ease.handles
        // O JSON do motor leva a força da bézier; sem ele, o formato antigo (×1).
        let engineJSON = model.engine.makeCurvePreset(title, interpolation: ease.interpolation,
            handles: curvePresetValues(ease).dropFirst().map { NSNumber(value: $0) })
        let object: [String: Any] = ["aurea_preset": 1, "kind": "curve", "name": title,
            "curve": ["interp": NSNumber(value: ease.interpolation), "x1": NSNumber(value: h[0]), "y1": NSNumber(value: h[1]),
                      "x2": NSNumber(value: h[2]), "y2": NSNumber(value: h[3])]]
        do {
            let directory = AureaPaths.documents.appendingPathComponent("presets/curva", isDirectory: true)
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            let safe = title.components(separatedBy: CharacterSet(charactersIn: "/\\:")).joined(separator: "-")
            var url = directory.appendingPathComponent(safe + ".json"), counter = 2
            while FileManager.default.fileExists(atPath: url.path) { url = directory.appendingPathComponent("\(safe) \(counter).json"); counter += 1 }
            // Sem dado nao se grava ARQUIVO VAZIO: um preset ilegivel e pior
            // que nenhum, porque aparece na lista e nao abre. Ver AureaJSON.h.
            let data: Data
            if !engineJSON.isEmpty, let bytes = engineJSON.data(using: .utf8) { data = bytes }
            else {
                guard let fallback = AureaJSONData(object, false) else { throw CocoaError(.fileWriteInvalidFileName) }
                data = fallback
            }
            try data.write(to: url, options: .atomic)
        } catch { model.toast = error.localizedDescription }
    }
}

/// Entry point for older curve panels; the actual editor is the shared sheet.
struct ExpressionEditor: View {
    @EnvironmentObject private var model: AureaModel
    var body: some View {
        Button {
            guard let layer = model.primarySelection else { return }
            model.expressionSheet = ExpressionRequest(layer: layer, label: AureaText.t("panel_expressao"),
                tracks: [ExpressionTrack(property: model.curveProperty, effect: model.curveEffect, param: model.curveParam)])
        } label: {
            HStack(spacing: 8) {
                Text("=").font(.aurea(size: 22, weight: .bold))
                Text(AureaText.t("panel_expressao")).font(.aurea(size: 14, weight: .semibold))
                Spacer()
            }.foregroundStyle(AureaColors.accent).frame(height: 44)
        }.buttonStyle(AureaPressStyle(shrink: 1)).padding(.horizontal, 14)
    }
}

/// REMAPEAR TEMPO em lista (par do TimeRemapEffectEditor do Android; UX
/// inspirada no Node Video, com o tema do Aurea). Três linhas e o Ao contrário:
///  1. Manter o tom do áudio.
///  2. Remapear tempo — o MOMENTO DA FONTE no cabeçote em timecode: arrastar o
///     valor para os lados (1 quadro a cada 4 pt) ou tocar para digitar grava a
///     chave ali. O ◇ marca/tira a chave; a faixa fina mostra as chaves e o
///     cabeçote (tocar numa chave vai até ela e a escolhe) e a curva abre o
///     editor de curva NORMAL do Aurea para aquele trecho.
///  3. Interpolação do tempo — Desligado / Mistura de quadros / Optical flow.
/// O dado é a MESMA curva da camada (prévia, exportação e som).
struct TimeRemapEffectEditor: View {
    @EnvironmentObject private var model: AureaModel
    let effectId: UInt32
    @State private var values: [Float] = []
    @State private var drag: Float?
    @State private var dragStart: Float?
    @State private var selectedKey: Int32?
    private var id: Int64 { model.primarySelection ?? 0 }
    private var fps: Float { Float(model.compositionFps > 0 ? model.compositionFps : 30) }
    private var flags: UInt32 { (model.detail["timeFlags"] as? NSNumber)?.uint32Value ?? 0 }
    private var isVideo: Bool { model.selectedLayer?.kind == 1 }
    private var keyTimes: [Int32] {
        guard values.count >= 5 else { return [] }
        let count = min(max(0, Int(values[0])), (values.count - 5) / 7)
        return (0..<count).map { Int32(values[5 + $0 * 7]) }
    }
    private var lo: Int32 { values.count >= 5 ? Int32(values[1]) : 0 }
    private var hi: Int32 { values.count >= 5 ? Int32(values[2]) : 1 }
    private var lastFrame: Float {
        guard values.count >= 5 else { return 1 }
        return max(1, values[3] > 0 ? values[3] : values[2])
    }
    private var inside: Bool { values.count >= 5 && model.localPlayhead >= lo && model.localPlayhead <= hi }
    private var seconds: Float { model.effectParams.first { $0.index == 0 }?.scalar ?? 0 }
    private var keyHere: Int? { keyTimes.firstIndex(of: model.localPlayhead) }
    private var shownFrames: Float { drag ?? seconds * fps }
    private func load() { values = model.engine.timeRemap(id).map(\.floatValue) }
    private func refresh() { model.refreshModel(force: true); load() }
    private func seekLocal(_ local: Int32) {
        guard let layer = model.selectedLayer else { return }
        model.seek(toFrame: Int64(local) + Int64(layer.startFrame) - Int64(layer.offsetFrames))
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            toggleRow("remap_manter_tom", checked: flags & 128 != 0) { on in
                model.mutate { $0.setKeepPitch(forLayer: id, on: on) }
                refresh()
            }
            if values.count < 5 {
                PanelNotice(AureaText.t("remap_ligue_efeito"))
            } else {
                PropertyCustomRow(AureaText.t("remap_linha_tempo"), selected: keyHere != nil, onSelect: toggleKey,
                                  keyframe: keyHere != nil ? .keyHere : .animated) {
                    HStack(spacing: 0) {
                        ValueBox(Self.timecode(Int(shownFrames.rounded()), fps: fps), enabled: inside, width: 112) { openKeypad() }
                            .highPriorityGesture(DragGesture(minimumDistance: 6).onChanged { value in
                                guard inside else { return }
                                if dragStart == nil { dragStart = seconds * fps; model.beginGesture("tempo do vídeo") }
                                let frames = min(lastFrame, max(0, (dragStart ?? 0) + Float(value.translation.width) * 0.25))
                                drag = frames
                                model.engine.setEffect(effectId, forLayer: id, paramIndex: 0, value: frames.rounded() / fps)
                            }.onEnded { _ in finishDrag() })
                        Spacer(minLength: 4)
                        Button(action: openCurve) {
                            CurveRailIcon(enabled: keyTimes.count >= 2, animated: selectedKey != nil || keyHere != nil)
                                .frame(width: 44, height: 44).contentShape(Rectangle())
                        }.buttonStyle(AureaPressStyle()).disabled(keyTimes.count < 2)
                            .accessibilityLabel(AureaText.t("remap_editar_curva"))
                    }
                }
                keyStrip.padding(.leading, 102).padding(.trailing, 4)
                if !inside {
                    Text(AureaText.t("remap_fora_clipe")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                        .fixedSize(horizontal: false, vertical: true)
                }
                if isVideo {
                    Text(AureaText.t("remap_interpolacao")).font(.aurea(size: 13)).padding(.top, 6)
                    ScrollView(.horizontal, showsIndicators: false) {
                        HStack(spacing: 6) {
                            ForEach([(0, "remap_interp_off"), (1, "remap_interp_blend"), (2, "remap_interp_flow")], id: \.0) { mode, label in
                                chip(label, on: (flags & 16 != 0 ? 2 : flags & 8 != 0 ? 1 : 0) == mode) {
                                    model.mutate { $0.setFrameBlendForLayer(id, mode: UInt32(mode)) }
                                    refresh()
                                }
                            }
                        }
                    }
                }
                toggleRow("remap_ao_contrario", checked: flags & 256 != 0) { _ in
                    model.mutate { _ = $0.reverseTimeRemap(forLayer: id) }
                    refresh()
                }
                Text(AureaText.t("remap_dica_lista")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                    .fixedSize(horizontal: false, vertical: true).padding(.top, 2)
            }
        }.frame(maxWidth: .infinity, alignment: .leading)
            .onAppear(perform: load)
            .onChange(of: model.status.modelRevision) { _ in load() }
            .onChange(of: model.status.playhead) { _ in load() }
            .onChange(of: id) { _ in load(); selectedKey = nil }
            .onDisappear { finishDrag() }
    }

    /// Timecode da fonte, H:MM:SS:QQ (quadros na taxa do projeto).
    static func timecode(_ frames: Int, fps: Float) -> String {
        let rate = max(1, Int(fps.rounded()))
        let f = max(0, frames)
        let total = f / rate
        return String(format: "%d:%02d:%02d:%02d", total / 3600, (total / 60) % 60, total % 60, f % rate)
    }

    private func finishDrag() {
        guard dragStart != nil else { return }
        dragStart = nil; drag = nil
        model.endGesture()
        load()
    }

    private func openKeypad() {
        guard inside else { return }
        let top = lastFrame / fps
        model.numericKeypad = KeypadRequest(title: AureaText.t("remap_linha_tempo"), value: shownFrames / fps, unit: "s",
                                            min: 0, max: top, decimals: 2) { s in
            model.engine.setEffect(effectId, forLayer: id, paramIndex: 0, value: min(top, max(0, s)))
            refresh()
        }
    }

    /// Editor de curva NORMAL: a chave escolhida na faixa, senão a que abre o
    /// trecho sob o cabeçote (TrackProperty::TimeRemap = 30).
    private func openCurve() {
        let keys = keyTimes
        guard keys.count >= 2 else { return }
        let time: Int32
        if let s = selectedKey, keys.contains(s), s != keys.last { time = s }
        else if let s = selectedKey, s == keys.last { time = keys[keys.count - 2] }
        else { time = keys[max(0, min(keys.count - 2, keys.lastIndex { $0 <= model.localPlayhead } ?? 0))] }
        model.openCurve(property: 30, effect: UInt32.max, param: 0, time: time)
    }

    private func toggleKey() {
        guard inside else { return }
        if let index = keyHere {
            if keyTimes.count > 2 { model.engine.removeTimeRemap(id, index: UInt32(index)) }
        } else {
            _ = model.engine.editTimeRemap(id, index: -1, time: Int64(model.localPlayhead), value: 0, interpolation: -1)
        }
        refresh()
    }

    /// Faixa fina das chaves: losangos, o escolhido aceso e o cabeçote. Tocar
    /// perto de um losango vai até ele e o escolhe; o resto move o cabeçote.
    private var keyStrip: some View {
        GeometryReader { geometry in
            let pad: CGFloat = 8
            let width = max(1, geometry.size.width - pad * 2)
            let span = CGFloat(max(1, hi - lo))
            let xOf: (Int32) -> CGFloat = { t in pad + width * CGFloat(min(max(t, lo), hi) - lo) / span }
            let frameAt: (CGFloat) -> Int32 = { x in lo + Int32((min(1, max(0, (x - pad) / width)) * span).rounded()) }
            Canvas { ctx, size in
                let y = size.height / 2
                ctx.fill(Path(roundedRect: CGRect(x: pad, y: y - 1.5, width: width, height: 3), cornerRadius: 1.5), with: .color(AureaColors.chip))
                let r: CGFloat = 5
                for t in keyTimes where t >= lo - 1 && t <= hi + 1 {
                    let x = xOf(t)
                    var p = Path()
                    p.move(to: CGPoint(x: x, y: y - r)); p.addLine(to: CGPoint(x: x + r, y: y))
                    p.addLine(to: CGPoint(x: x, y: y + r)); p.addLine(to: CGPoint(x: x - r, y: y)); p.closeSubpath()
                    ctx.fill(p, with: .color(t == selectedKey || t == model.localPlayhead ? AureaColors.accent : AureaColors.text))
                }
                let px = xOf(model.localPlayhead)
                var line = Path()
                line.move(to: CGPoint(x: px, y: 2)); line.addLine(to: CGPoint(x: px, y: size.height - 2))
                ctx.stroke(line, with: .color(AureaColors.playhead), lineWidth: 2)
            }.contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0).onChanged { value in
                    let start = value.startLocation.x
                    if let hit = keyTimes.min(by: { abs(xOf($0) - start) < abs(xOf($1) - start) }), abs(xOf(hit) - start) <= 14 {
                        if selectedKey != hit { selectedKey = hit; seekLocal(hit) }
                    } else {
                        selectedKey = nil
                        seekLocal(min(max(frameAt(value.location.x), lo), max(lo, hi - 1)))
                    }
                })
        }.frame(height: 32).accessibilityLabel(AureaText.t("remap_faixa_chaves"))
    }

    private func toggleRow(_ key: String, checked: Bool, _ onChange: @escaping (Bool) -> Void) -> some View {
        HStack {
            Text(AureaText.t(key)).font(.aurea(size: 13)).frame(maxWidth: .infinity, alignment: .leading)
            AureaToggle(checked: checked, onCheckedChange: onChange)
        }.frame(height: 48)
    }

    private func chip(_ label: String, on: Bool, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(AureaText.t(label)).font(.aurea(size: 12)).foregroundStyle(on ? AureaColors.accent : AureaColors.text)
                .padding(.horizontal, 12).frame(height: 36)
                .background(on ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
        }.buttonStyle(AureaPressStyle())
    }
}

// TimeRemapGraph.kt: source frame vertically, layer-local time horizontally.
struct TimeRemapEditor: View {
    @EnvironmentObject private var model: AureaModel
    var expanded = false
    var expandedHeight: CGFloat = 240
    @State private var fullscreen = false
    @State private var viewport: TrackGraphViewport?
    @State private var panInitial: TrackGraphViewport?
    @State private var grabOffset = CGPoint.zero
    @State private var values: [Float] = []
    @State private var selected = -1
    @State private var editingPoint: Int?
    private var id: Int64 { model.primarySelection ?? 0 }
    private var points: [[Float]] {
        guard values.count >= 5 else { return [] }
        let count = min(max(0, Int(values[0])), (values.count - 5) / 7)
        return (0..<count).map { index in Array(values[(5 + index * 7)..<(12 + index * 7)]) }
    }
    private var fitLow: Float { values.count >= 5 ? values[1] : 0 }
    private var low: Float { viewport.map { Float($0.from) } ?? fitLow }
    private var fitHigh: Float { values.count >= 5 ? max(values[2], fitLow + 1) : 1 }
    private var high: Float { viewport.map { Float($0.to) } ?? fitHigh }
    private var fitSourceMax: Float {
        if values.count >= 5 && values[3] > 0 { return values[3] }
        return max(1, (points.map { $0[1] }.max() ?? 0) * 1.2)
    }
    private var sourceLow: Float { viewport.map { Float($0.low) } ?? 0 }
    private var sourceMax: Float { viewport.map { Float($0.high) } ?? fitSourceMax }
    private var view: TrackGraphViewport { viewport ?? TrackGraphViewport(from: Double(fitLow), to: Double(fitHigh), low: 0, high: Double(fitSourceMax)) }
    private var speedLabel: String {
        let speed = values.count >= 5 ? values[4] : 0
        if abs(speed) < 0.005 { return AureaText.t("panel_congelado_cabecote") }
        let number = String(format: "%.2f", abs(speed))
        return AureaText.t(speed < 0 ? "ios_speed_at_playhead_reversed" : "ios_speed_at_playhead", number)
    }
    private func load() {
        values = model.engine.timeRemap(id).map(\.floatValue)
        if !points.indices.contains(selected) { selected = -1 }
    }
    private func refresh() { model.refreshModel(force: true); load() }
    private func finish() {
        guard editingPoint != nil else { return }
        editingPoint = nil
        model.engine.endUndoGroup()
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            if !points.isEmpty {
                HStack {
                    Button("−") { viewport = view.transformed(zoom: 1/1.5) }.frame(width: 44, height: 44)
                    Button("+") { viewport = view.transformed(zoom: 1.5) }.frame(width: 44, height: 44)
                    Button(AureaText.t("panel_ajustar")) { viewport = nil }.frame(minWidth: 44, minHeight: 44)
                    Spacer()
                    if !expanded { Button(AureaText.t("panel_expandir")) { fullscreen = true }.frame(minHeight: 44) }
                }
                GeometryReader { geometry in
                    ZStack {
                        Canvas { context, size in draw(context, size: size) }
                        TimeRemapTouchSurface(
                            hit: { hit($0, size: geometry.size) },
                            onTap: { location in tap(location, size: geometry.size) },
                            onLongPress: { location in remove(location, size: geometry.size) },
                            onBegin: { index, location in
                                finish(); selected = index; editingPoint = index
                                let key = position(points[index], size: geometry.size)
                                grabOffset = CGPoint(x: key.x-location.x, y: key.y-location.y)
                                model.engine.beginUndoGroup()
                            },
                            onMove: { index, location in move(index, location: location, size: geometry.size) },
                            onEnd: finish,
                            onPan: { translation, ended in
                                if panInitial == nil { panInitial = view }
                                if let initial = panInitial {
                                    viewport = initial.transformed(zoom: 1, dx: Double(translation.x / max(1, geometry.size.width - 48)), dy: Double(translation.y / max(1, geometry.size.height - 48)))
                                }
                                if ended { panInitial = nil }
                            })
                    }
                }.frame(height: expanded ? expandedHeight : 240).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
                    .clipShape(RoundedRectangle(cornerRadius: 10))
                Text(speedLabel).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                    .frame(maxWidth: .infinity, alignment: .leading).padding(.top, 6)
                if points.indices.contains(selected) {
                    HStack(spacing: 6) {
                        Text("Ponto \(selected + 1)").font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                        interpolationChip(1, "panel_linear")
                        interpolationChip(5, "panel_suave")
                        interpolationChip(0, "panel_congelar")
                    }.padding(.top, 6)
                }
                Text(AureaText.t("panel_toque_curva_criar_ponto_arraste_mudar"))
                    .font(.aurea(size: 11)).foregroundStyle(AureaColors.muted)
                    .fixedSize(horizontal: false, vertical: true).padding(.top, 4)
            }
        }.frame(maxWidth: .infinity, alignment: .leading)
            .onAppear(perform: load)
            .onChange(of: model.status.modelRevision) { _ in load() }
            .onChange(of: model.status.playhead) { _ in load() }
            .onChange(of: id) { _ in finish(); selected = -1; load() }
            .onDisappear(perform: finish)
            .fullScreenCover(isPresented: $fullscreen) {
                GeometryReader { bounds in
                    VStack {
                        Button(AureaText.t("editor_sair_tela_cheia")) { fullscreen = false }.frame(minHeight: 44)
                        TimeRemapEditor(expanded: true, expandedHeight: max(96, bounds.size.height - 220)).environmentObject(model)
                        Spacer(minLength: 0)
                    }.padding(16).background(AureaColors.background)
                }.interactiveDismissDisabled()
            }
    }
    private func interpolationChip(_ interpolation: Int32, _ label: String) -> some View {
        let active = points.indices.contains(selected) && Int32(points[selected][2]) == interpolation
        return Button {
            guard points.indices.contains(selected) else { return }
            let point = points[selected]
            _ = model.engine.editTimeRemap(id, index: Int32(selected), time: Int64(point[0]), value: point[1], interpolation: interpolation)
            refresh()
        } label: {
            Text(AureaText.t(label)).font(.aurea(size: 12)).foregroundStyle(active ? AureaColors.accent : AureaColors.text)
                .padding(.horizontal, 10).padding(.vertical, 6)
                .background(active ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
        }.buttonStyle(AureaPressStyle())
    }
    private func position(_ point: [Float], size: CGSize) -> CGPoint {
        CGPoint(x: 24 + CGFloat((point[0] - low) / (high - low)) * max(1, size.width - 48),
                y: size.height - 24 - CGFloat((point[1] - sourceLow) / max(0.0001, sourceMax-sourceLow)) * max(1, size.height - 48))
    }
    private func frame(at x: CGFloat, size: CGSize) -> Int64 {
        let value = low + Float((x - 24) / max(1, size.width - 48)) * (high - low)
        return Int64(floor(Double(value) + 0.5))
    }
    private func hit(_ location: CGPoint, size: CGSize) -> Int? {
        var nearest: Int?, distance: CGFloat = 28
        for (index, point) in points.enumerated() {
            let p = position(point, size: size), d = hypot(p.x - location.x, p.y - location.y)
            if d < distance { nearest = index; distance = d }
        }
        return nearest
    }
    private func tap(_ location: CGPoint, size: CGSize) {
        if let index = hit(location, size: size) { selected = index; return }
        // index=-1 samples the existing C++ curve: inserting alone changes no motion.
        selected = Int(model.engine.editTimeRemap(id, index: -1, time: frame(at: location.x, size: size), value: 0, interpolation: -1))
        refresh()
    }
    private func remove(_ location: CGPoint, size: CGSize) {
        guard points.count > 2, let index = hit(location, size: size) else { return }
        model.engine.removeTimeRemap(id, index: UInt32(index))
        selected = -1; refresh()
    }
    private func move(_ index: Int, location: CGPoint, size: CGSize) {
        guard let index = editingPoint, points.indices.contains(index) else { return }
        let location = CGPoint(x: location.x+grabOffset.x, y: location.y+grabOffset.y)
        let value = max(0, sourceLow + Float((size.height - 24 - location.y) / max(1, size.height - 48)) * (sourceMax-sourceLow))
        let moved = model.engine.editTimeRemap(id, index: Int32(index), time: frame(at: location.x, size: size), value: value, interpolation: -1)
        if moved >= 0 { selected = Int(moved); editingPoint = Int(moved) }
        refresh()
    }
    private func draw(_ context: GraphicsContext, size: CGSize) {
        let pad: CGFloat = 24, width = max(1, size.width - 48), height = max(1, size.height - 48)
        func line(_ start: CGPoint, _ end: CGPoint, _ color: Color, _ stroke: CGFloat) {
            var path = Path(); path.move(to: start); path.addLine(to: end)
            context.stroke(path, with: .color(color), lineWidth: stroke)
        }
        for index in 1...3 {
            let x = pad + width * CGFloat(index) / 4, y = pad + height * CGFloat(index) / 4
            line(CGPoint(x: x, y: pad), CGPoint(x: x, y: size.height - pad), .white.opacity(0.06), 1)
            line(CGPoint(x: pad, y: y), CGPoint(x: size.width - pad, y: y), .white.opacity(0.06), 1)
        }
        let keys = points
        if let first = keys.first {
            var path = Path(); path.move(to: position(first, size: size))
            for index in 0..<max(0, keys.count - 1) {
                let a = keys[index], b = keys[index + 1]
                let ease = CurveEase(interpolation: UInt32(a[2]), x1: a[3], y1: a[4], x2: a[5], y2: a[6])
                for sample in 1...32 {
                    let u = Float(sample) / 32
                    path.addLine(to: position([a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * ease.transform(u)], size: size))
                }
            }
            context.stroke(path, with: .color(AureaColors.accent), lineWidth: 2.5)
        }
        let playhead = Float(model.localPlayhead)
        if playhead >= low && playhead <= high {
            let x = position([playhead, 0], size: size).x
            line(CGPoint(x: x, y: pad * 0.5), CGPoint(x: x, y: size.height - pad * 0.5), Color(red: 1, green: 90 / 255, blue: 90 / 255), 1.5)
        }
        for (index, point) in keys.enumerated() {
            let center = position(point, size: size)
            context.fill(Path(ellipseIn: CGRect(x: center.x - 8, y: center.y - 8, width: 16, height: 16)), with: .color(.black.opacity(0.5)))
            context.fill(Path(ellipseIn: CGRect(x: center.x - 6, y: center.y - 6, width: 12, height: 12)), with: .color(index == selected ? .white : AureaColors.accent))
        }
    }
}

/// The graph owns drags inside its bounds: keys take priority, empty space pans. Scroll outside the graph remains available.
private struct TimeRemapTouchSurface: UIViewRepresentable {
    var hit: (CGPoint) -> Int?
    var onTap: (CGPoint) -> Void
    var onLongPress: (CGPoint) -> Void
    var onBegin: (Int, CGPoint) -> Void
    var onMove: (Int, CGPoint) -> Void
    var onEnd: () -> Void
    var onPan: (CGPoint, Bool) -> Void
    func makeCoordinator() -> Coordinator { Coordinator(self) }
    func makeUIView(context: Context) -> UIView {
        let view = UIView(); view.backgroundColor = .clear
        let pan = UIPanGestureRecognizer(target: context.coordinator, action: #selector(Coordinator.pan(_:)))
        pan.maximumNumberOfTouches = 1; pan.delegate = context.coordinator
        let hold = UILongPressGestureRecognizer(target: context.coordinator, action: #selector(Coordinator.hold(_:)))
        hold.minimumPressDuration = 0.5
        let tap = UITapGestureRecognizer(target: context.coordinator, action: #selector(Coordinator.tap(_:)))
        tap.require(toFail: hold); tap.require(toFail: pan)
        view.addGestureRecognizer(pan); view.addGestureRecognizer(hold); view.addGestureRecognizer(tap)
        return view
    }
    func updateUIView(_ uiView: UIView, context: Context) { context.coordinator.owner = self }
    static func dismantleUIView(_ uiView: UIView, coordinator: Coordinator) { coordinator.finish() }
    final class Coordinator: NSObject, UIGestureRecognizerDelegate {
        var owner: TimeRemapTouchSurface
        private var index: Int?
        init(_ owner: TimeRemapTouchSurface) { self.owner = owner }
        func finish() {
            guard index != nil else { return }
            index = nil; owner.onEnd()
        }
        func gestureRecognizerShouldBegin(_ gestureRecognizer: UIGestureRecognizer) -> Bool {
            return true
        }
        @objc func tap(_ recognizer: UITapGestureRecognizer) {
            guard recognizer.state == .ended, let view = recognizer.view else { return }
            owner.onTap(recognizer.location(in: view))
        }
        @objc func hold(_ recognizer: UILongPressGestureRecognizer) {
            guard recognizer.state == .began, let view = recognizer.view else { return }
            owner.onLongPress(recognizer.location(in: view))
        }
        private func panTranslation(_ recognizer: UIPanGestureRecognizer, _ view: UIView) -> CGPoint { recognizer.translation(in: view) }
        @objc func pan(_ recognizer: UIPanGestureRecognizer) {
            guard let view = recognizer.view else { finish(); return }
            let location = recognizer.location(in: view)
            switch recognizer.state {
            case .began:
                let translation = recognizer.translation(in: view)
                index = owner.hit(CGPoint(x: location.x - translation.x, y: location.y - translation.y))
                if let index { owner.onBegin(index, CGPoint(x: location.x-translation.x, y: location.y-translation.y)) }
            case .changed:
                if let index { owner.onMove(index, location) } else { owner.onPan(panTranslation(recognizer, view), false) }
            case .ended, .cancelled, .failed:
                if index == nil { owner.onPan(panTranslation(recognizer, view), true) }; finish()
            default: break
            }
        }
    }
}

// ExpressionSheet.kt: shared expression target, including vector properties.
struct ExpressionTrack: Equatable {
    let property: UInt32
    let effect: UInt32
    let param: UInt32
    init(property: UInt32, effect: UInt32 = .max, param: UInt32 = 0) {
        self.property = property; self.effect = effect; self.param = param
    }
}
struct ExpressionRequest: Identifiable {
    let id = UUID()
    let layer: Int64
    let label: String
    let tracks: [ExpressionTrack]
    let scale: Float
    let unit: String
    init(layer: Int64, label: String, tracks: [ExpressionTrack], scale: Float = 1, unit: String = "") {
        self.layer = layer; self.label = label; self.tracks = tracks; self.scale = scale; self.unit = unit
    }
    var packedTracks: [NSNumber] { tracks.flatMap { [NSNumber(value: $0.property), NSNumber(value: $0.effect), NSNumber(value: $0.param)] } }
}
private struct ExpressionDiagnostic {
    var ok = true
    var message = ""
    var line = 0
    var column = 0
    init() {}
    init(_ info: [String: Any]) {
        ok = (info["ok"] as? NSNumber)?.boolValue ?? true
        message = info["message"] as? String ?? ""
        line = (info["line"] as? NSNumber)?.intValue ?? 0
        column = (info["column"] as? NSNumber)?.intValue ?? 0
    }
}

@MainActor
struct ExpressionSheet: View {
    @EnvironmentObject private var model: AureaModel
    let request: ExpressionRequest
    let onDismiss: () -> Void
    @State private var source = ""
    @State private var selection = NSRange(location: 0, length: 0)
    @State private var info: [String: Any] = [:]
    @State private var syntax = ExpressionDiagnostic()
    @State private var values: [Float] = []
    @State private var refused = false
    @State private var loaded = false
    @State private var codeScroll: CGFloat = 0
    private let snippets = ["wiggle(2, 30)", "shake(30, 8)", "shake(80, 14, 0.6, 0.6, inPoint)", "ease(timeSinceMarker(), 0, 0.3, 120, 100)", "posterizeTime(12); wiggle(2, 30)", "loopOut(\"cycle\")", "time * 90", "value", "loopOut(\"pingpong\")", "linear(time, 0, 1, 0, 100)", "effect(\"Slider Control\")(\"Slider\")", "thisComp.layer(1).transform.position", "random(0, 100)"]
    private var applied: Bool { (info["exists"] as? NSNumber)?.boolValue == true }
    private var enabled: Bool { (info["enabled"] as? NSNumber)?.boolValue == true }
    private var dirty: Bool { source != (info["source"] as? String ?? "") }
    private var shownError: ExpressionDiagnostic? {
        if !syntax.ok { return syntax }
        if !dirty && applied, let error = info["error"] as? String, !error.isEmpty {
            var diagnostic = ExpressionDiagnostic(); diagnostic.ok = false; diagnostic.message = error
            diagnostic.line = (info["line"] as? NSNumber)?.intValue ?? 0
            diagnostic.column = (info["column"] as? NSNumber)?.intValue ?? 0
            return diagnostic
        }
        return nil
    }
    private var status: (String, Color) {
        if refused { return (AureaText.t("panel_motor_recusou_esta_propriedade"), AureaColors.danger) }
        if let error = shownError {
            let message = error.line > 0 ? AureaText.t("pn_expr_error_at", error.line, error.column, error.message) : error.message
            return (message + (!dirty && applied && syntax.ok ? AureaText.t("panel_usando_valor_keyframes") : ""), AureaColors.danger)
        }
        if dirty { return (AureaText.t(source.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty ? "panel_aplicar_sem_texto_remove_expressao" : "panel_sintaxe_ok_toque_aplicar"), AureaColors.muted) }
        if applied && !enabled { return (AureaText.t("panel_desligada_propriedade_usa_keyframes"), AureaColors.muted) }
        if applied { return (AureaText.t("panel_resultado_agora") + values.map { numeroPtBr($0, casas: 2) + request.unit }.joined(separator: " · "), AureaColors.keyframe) }
        return (AureaText.t("panel_escreva_expressao_ou_toque_num_atalho"), AureaColors.muted)
    }
    var body: some View {
        AureaBottomOverlay(modal: true, onDismiss: onDismiss) {
            VStack(alignment: .leading, spacing: 0) {
                HStack(spacing: 8) {
                    VStack(alignment: .leading, spacing: 0) {
                        Text(AureaText.t("panel_expressao")).font(.aurea(size: 17, weight: .bold))
                        Text(request.label).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                    }.frame(maxWidth: .infinity, alignment: .leading)
                    if applied {
                        Text(AureaText.t(enabled ? "panel_ligada" : "panel_desligada")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                        AureaToggle(checked: enabled) { on in
                            _ = model.engine.enableExpressions(request.layer, tracks: request.packedTracks, enabled: on)
                            model.refreshModel(force: true); reread()
                        }
                    }
                }.padding(.bottom, 12)
                codeEditor
                Text(status.0).font(.aurea(size: 12)).foregroundStyle(status.1).fixedSize(horizontal: false, vertical: true).padding(.top, 8)
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 6) {
                        ForEach(snippets, id: \.self) { snippet in
                            Button { insert(snippet) } label: {
                                Text(snippet).font(.aurea(size: 12, design: .monospaced)).foregroundStyle(AureaColors.keyframe)
                                    .padding(.horizontal, 10).padding(.vertical, 7).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                            }.buttonStyle(AureaPressStyle())
                        }
                    }
                }.padding(.top, 10)
                HStack(spacing: 8) {
                    if applied { sheetButton(AureaText.t("panel_remover"), color: AureaColors.danger) { apply(""); source = ""; selection = NSRange(location: 0, length: 0) } }
                    sheetButton(AureaText.t("panel_fechar"), color: AureaColors.text, action: onDismiss)
                    sheetButton(AureaText.t("panel_aplicar"), color: AureaColors.accent, enabled: dirty) { apply(source) }
                }.padding(.top, 14).padding(.bottom, 12)
            }.padding(.horizontal, 16).foregroundStyle(AureaColors.text)
        }
        .onAppear {
            reread(); source = info["source"] as? String ?? ""; selection = NSRange(location: (source as NSString).length, length: 0); loaded = true
        }
        .onChange(of: model.status.playhead) { _ in reread() }
        .onChange(of: model.status.modelRevision) { _ in reread() }
        .task(id: source) {
            guard loaded else { return }
            do { try await Task.sleep(nanoseconds: 250_000_000) } catch { return }
            guard !Task.isCancelled else { return }
            syntax = source.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty ? ExpressionDiagnostic() : ExpressionDiagnostic(model.engine.checkExpressionSyntax(source))
        }
    }
    private var codeEditor: some View {
        let lineCount = source.filter { $0 == "\n" }.count + 1
        let height = min(220, max(120, CGFloat(lineCount) * 20 + 20))
        return HStack(spacing: 0) {
            Canvas { context, size in
                for line in 1...lineCount {
                    let y = CGFloat(line - 1) * 20 + 10 - codeScroll
                    guard y >= -20 && y <= size.height else { continue }
                    let error = shownError?.line == line
                    context.draw(Text(String(line)).font(.aurea(size: 14, weight: error ? .bold : .regular, design: .monospaced)).foregroundColor(error ? AureaColors.danger : AureaColors.muted.opacity(0.6)), at: CGPoint(x: 28, y: y), anchor: .topTrailing)
                }
            }.frame(width: 34)
            ExpressionTextInput(text: $source, selection: $selection, scroll: $codeScroll).padding(.trailing, 10)
        }.frame(height: height).background(Color(hex: 0x0B1016), in: RoundedRectangle(cornerRadius: 10)).clipped()
    }
    private func sheetButton(_ label: String, color: Color, enabled: Bool = true, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(label).font(.aurea(size: 15, weight: .semibold)).foregroundStyle(enabled ? color : AureaColors.disabled)
                .frame(maxWidth: .infinity).frame(height: 44).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 12))
        }.buttonStyle(AureaPressStyle()).disabled(!enabled)
    }
    private func reread() {
        guard let first = request.tracks.first else { info = [:]; values = []; return }
        info = model.engine.expression(request.layer, property: first.property, effect: first.effect, param: first.param)
        values = request.tracks.map { track in
            let value = model.engine.expression(request.layer, property: track.property, effect: track.effect, param: track.param)
            return ((value["value"] as? NSNumber)?.floatValue ?? 0) * request.scale
        }
    }
    private func apply(_ text: String) {
        let result = model.engine.setExpressions(request.layer, tracks: request.packedTracks, source: text)
        refused = (result["accepted"] as? NSNumber)?.boolValue != true
        if !refused { syntax = ExpressionDiagnostic(result) }
        model.refreshModel(force: true); reread()
    }
    private func insert(_ snippet: String) {
        let text = source as NSString
        let start = min(selection.location, text.length), count = min(selection.length, text.length - min(selection.location, text.length))
        source = text.replacingCharacters(in: NSRange(location: start, length: count), with: snippet)
        selection = NSRange(location: start + (snippet as NSString).length, length: 0)
    }
}

@MainActor
private struct ExpressionTextInput: UIViewRepresentable {
    @Binding var text: String
    @Binding var selection: NSRange
    @Binding var scroll: CGFloat
    func makeCoordinator() -> Coordinator { Coordinator(self) }
    func makeUIView(context: Context) -> UITextView {
        let view = UITextView(); view.delegate = context.coordinator
        view.backgroundColor = .clear; view.textColor = UIColor(AureaColors.text); view.tintColor = UIColor(AureaColors.accent)
        view.font = UIFont.monospacedSystemFont(ofSize: 14, weight: .regular)
        view.textContainerInset = UIEdgeInsets(top: 10, left: 0, bottom: 10, right: 0); view.textContainer.lineFragmentPadding = 0
        view.autocorrectionType = .no; view.autocapitalizationType = .none; view.spellCheckingType = .no; view.keyboardType = .asciiCapable
        view.smartQuotesType = .no; view.smartDashesType = .no; view.isScrollEnabled = true
        let paragraph = NSMutableParagraphStyle(); paragraph.minimumLineHeight = 20; paragraph.maximumLineHeight = 20
        view.typingAttributes = [.font: UIFont.monospacedSystemFont(ofSize: 14, weight: .regular), .foregroundColor: UIColor(AureaColors.text), .paragraphStyle: paragraph]
        return view
    }
    func updateUIView(_ view: UITextView, context: Context) {
        context.coordinator.parent = self
        if view.text != text {
            let paragraph = NSMutableParagraphStyle(); paragraph.minimumLineHeight = 20; paragraph.maximumLineHeight = 20
            view.attributedText = NSAttributedString(string: text, attributes: [.font: UIFont.monospacedSystemFont(ofSize: 14, weight: .regular), .foregroundColor: UIColor(AureaColors.text), .paragraphStyle: paragraph])
        }
        let length = (text as NSString).length
        let range = NSRange(location: min(selection.location, length), length: min(selection.length, length - min(selection.location, length)))
        if view.selectedRange != range { view.selectedRange = range }
    }
    final class Coordinator: NSObject, UITextViewDelegate {
        var parent: ExpressionTextInput
        init(_ parent: ExpressionTextInput) { self.parent = parent }
        func textViewDidChange(_ textView: UITextView) { parent.text = textView.text; parent.selection = textView.selectedRange }
        func textViewDidChangeSelection(_ textView: UITextView) { if parent.selection != textView.selectedRange { parent.selection = textView.selectedRange } }
        func scrollViewDidScroll(_ scrollView: UIScrollView) { parent.scroll = scrollView.contentOffset.y }
    }
}

// Value/speed graph uses the core track evaluator, not the easing thumbnail.
// Par do GraphCurve.kt: o gráfico de valor/velocidade desenha a MESMA conta do
// motor (`Track::sample_keys` + `apply_easing`), não os 160 valores de
// `trackCurve` — numa trilha longa eles pulavam frames (o "manter" virava
// rampa) e a velocidade saía de diferenças entre frames, longe das alças.
// Nos frames inteiros (os únicos que o motor pede) o traço passa pelo valor
// renderizado; os frames da janela entram como vértices.
struct CurveKey { let time: Int32; let value: Float; let ease: CurveEase }

func graphCurveKeys(_ track: [KeyframeItem], ease: (KeyframeItem) -> CurveEase) -> [CurveKey] {
    track.sorted { $0.time < $1.time }.map { CurveKey(time: $0.time, value: $0.value, ease: ease($0)) }
}

private func curveBefore(_ keys: [CurveKey], _ frame: Double) -> Int {
    var lo = 0, hi = keys.count - 1, best = -1
    while lo <= hi {
        let mid = (lo + hi) / 2
        if Double(keys[mid].time) <= frame { best = mid; lo = mid + 1 } else { hi = mid - 1 }
    }
    return best
}

private func curveSegmentValue(_ a: CurveKey, _ b: CurveKey, _ frame: Double) -> Float {
    if a.ease.interpolation == 0 { return a.value }
    let span = Int64(b.time) - Int64(a.time)
    if span <= 0 { return b.value }
    let t01 = Float((frame - Double(a.time)) / Double(span))
    return a.value + (b.value - a.value) * a.ease.transform(t01)
}

private func curveSegmentVelocity(_ a: CurveKey, _ b: CurveKey, _ frame: Double, fps: Float) -> Float {
    let span = Double(Int64(b.time) - Int64(a.time))
    if span <= 0 || a.ease.interpolation == 0 || a.ease.interpolation == 9 { return 0 }
    let t = Float(min(1, max(0, (frame - Double(a.time)) / span)))
    let h: Float = 1e-3
    let lo = max(0, t - h), hi = min(1, t + h)
    let slope = (a.ease.transform(hi) - a.ease.transform(lo)) / (hi - lo)
    return Float(Double((b.value - a.value) * slope * fps) / span)
}

/// `Track::sample_keys` num instante qualquer (nos inteiros, o valor do motor).
func sampleCurve(_ keys: [CurveKey], _ frame: Double) -> Float {
    guard let first = keys.first else { return 0 }
    if keys.count == 1 { return first.value }
    let i = curveBefore(keys, frame)
    if i < 0 { return first.value }
    if i >= keys.count - 1 { return keys[keys.count - 1].value }
    return curveSegmentValue(keys[i], keys[i + 1], frame)
}

/// Velocidade (unidades/s): a derivada da mesma conta; fora da animação, zero.
func sampleCurveVelocity(_ keys: [CurveKey], _ frame: Double, fps: Float) -> Float {
    guard keys.count >= 2 else { return 0 }
    let i = curveBefore(keys, frame)
    if i < 0 || i >= keys.count - 1 { return 0 }
    return curveSegmentVelocity(keys[i], keys[i + 1], frame, fps: fps)
}

/// O traço em from...to (frames): subdividido pela tela e SEMPRE com os frames
/// inteiros como vértices; manter/degraus viram degraus retos (dois vértices no
/// mesmo x). Par exato do `graphCurve` do Android.
func graphCurve(_ keys: [CurveKey], from: Double, to: Double, pointsPerFrame: Double, speed: Bool, fps: Float) -> [CGPoint] {
    guard !keys.isEmpty, from.isFinite, to.isFinite, to > from else { return [] }
    var out: [CGPoint] = []
    func add(_ frame: Double, _ value: Float) { if value.isFinite { out.append(CGPoint(x: frame, y: Double(value))) } }
    let density: Double = pointsPerFrame.isFinite && pointsPerFrame > 0 ? pointsPerFrame : 1
    add(from, speed ? 0 : sampleCurve(keys, from))
    for i in 0..<(keys.count - 1) {
        let a = keys[i], b = keys[i + 1]
        let t0 = Double(a.time), t1 = Double(b.time)
        if t1 <= from || t0 >= to || t1 <= t0 { continue }
        let lo = max(t0, from), hi = min(t1, to)
        if speed && i == 0 && t0 > from { add(t0, 0) }
        switch a.ease.interpolation {
        case 0:
            add(lo, speed ? 0 : a.value); add(hi, speed ? 0 : a.value)
        case 9:
            for k in 0..<4 {
                let s = t0 + (t1 - t0) * Double(k) / 4, e = t0 + (t1 - t0) * Double(k + 1) / 4
                if e <= lo || s >= hi { continue }
                let v: Float = speed ? 0 : a.value + (b.value - a.value) * Float(k) / 4
                add(max(s, lo), v); add(min(e, hi), v)
            }
        default:
            let n = min(2048, max(8, Int(ceil((hi - lo) * density))))
            var frames: [Double] = (0...n).map { lo + (hi - lo) * Double($0) / Double(n) }
            let first = Int64(ceil(lo)), last = Int64(floor(hi))
            if last >= first && last - first <= 4096 { frames += (first...last).map { Double($0) } }
            frames.sort()
            var previous = Double.nan
            for f in frames where f != previous {
                previous = f
                add(f, speed ? curveSegmentVelocity(a, b, f, fps: fps) : curveSegmentValue(a, b, f))
            }
        }
        if !speed && t1 <= to { add(t1, b.value) }
        if speed && i + 1 == keys.count - 1 && t1 < to { add(t1, 0) }
    }
    add(to, speed ? 0 : sampleCurve(keys, to))
    return out
}

/// Trilhas IRMÃS (par do `sameGroup` do Android): X/Y/Z de uma propriedade,
/// componentes de um parâmetro de efeito, largura/altura da forma.
func graphSameGroup(_ a: KeyframeItem, _ b: KeyframeItem) -> Bool {
    if a.property == 31 || b.property == 31 {
        return a.property == b.property && a.effectIndex == b.effectIndex && a.paramIndex / 4 == b.paramIndex / 4
    }
    if a.property == 35 && b.property == 35 {
        return a.paramIndex == b.paramIndex || ((5...6).contains(a.paramIndex) && (5...6).contains(b.paramIndex))
    }
    if a.property > 31 || b.property > 31 {
        return a.property == b.property && a.effectIndex == b.effectIndex && a.paramIndex == b.paramIndex
    }
    func group(_ p: UInt32) -> UInt32 { p < 12 ? p / 3 : (p == 13 || p == 14 ? 4 : 100 + p) }
    return group(a.property) == group(b.property)
}

/// Trilhas animadas (2+ marcas) da camada, em ordem estável.
func graphAnimatedTracks(_ keys: [KeyframeItem]) -> [[KeyframeItem]] {
    var tracks: [[KeyframeItem]] = []
    for key in keys {
        if let i = tracks.firstIndex(where: { curveSameTrack($0[0], key) }) { tracks[i].append(key) } else { tracks.append([key]) }
    }
    func order(_ k: KeyframeItem) -> (UInt32, UInt32, UInt32) { (k.property, k.property < 31 ? 0 : k.effectIndex, k.paramIndex) }
    return tracks.map { track in track.sorted { $0.time < $1.time } }
        .filter { $0.count >= 2 }
        .sorted { order($0[0]) < order($1[0]) }
}

/// O que o gráfico mostra numa camada: a mesma trilha de `like` se ela anima
/// ali, senão uma irmã, senão a primeira animada ([] = nada para mostrar).
func graphTrackFor(_ keys: [KeyframeItem], like: KeyframeItem?) -> [KeyframeItem] {
    let tracks = graphAnimatedTracks(keys)
    if let like {
        if let same = tracks.first(where: { curveSameTrack($0[0], like) }) { return same }
        let sibling = preferredCurveTrack(tracks.filter { graphSameGroup($0[0], like) })
        if sibling.count >= 2 { return sibling }
    }
    return preferredCurveTrack(tracks)
}

/// O grupo inteiro de `track` na camada (a escolhida sempre entra).
func graphGroup(_ keys: [KeyframeItem], _ track: [KeyframeItem]) -> [[KeyframeItem]] {
    guard let head = track.first else { return [] }
    let group = graphAnimatedTracks(keys).filter { graphSameGroup($0[0], head) }
    return group.contains(where: { curveSameTrack($0[0], head) }) ? group : [track] + group
}

/// O trecho sob o cabeçote (a última marca abre o trecho que chega nela); −1 = nenhum.
func graphSegmentIndexAt(_ track: [KeyframeItem], _ local: Int32) -> Int {
    guard track.count >= 2 else { return -1 }
    return min(track.count - 2, max(0, track.lastIndex(where: { $0.time <= local }) ?? 0))
}

/// O trecho que o painel mostra para a marca escolhida em `time`.
func graphSegmentIndexOf(_ track: [KeyframeItem], _ time: Int32) -> Int {
    guard track.count >= 2 else { return -1 }
    let i = track.firstIndex(where: { $0.time == time }) ?? max(0, track.lastIndex(where: { $0.time <= time }) ?? 0)
    return min(i, track.count - 2)
}

private struct TrackGraphViewport: Equatable {
    var from: Double, to: Double, low: Double, high: Double
    var duration: Double { max(1, to - from) }
    var range: Double { max(0.0001, high - low) }
    func transformed(zoom: Double, dx: Double = 0, dy: Double = 0) -> TrackGraphViewport {
        let duration = min(1e9, max(1, self.duration / min(4, max(0.25, zoom))))
        let range = min(1e12, max(0.0001, self.range / min(4, max(0.25, zoom))))
        let from = min(1e9 - duration, max(-1e9, self.from + (self.duration - duration) * 0.5 - dx * self.duration))
        let low = self.low + (self.range - range) * 0.5 + dy * self.range
        return TrackGraphViewport(from: from, to: from + duration, low: low, high: low + range)
    }
}

@MainActor
private struct NativeTrackGraph: View {
    @EnvironmentObject private var model: AureaModel
    let layer: Int64
    let keys: [KeyframeItem]
    let speed: Bool
    @State private var viewport = TrackGraphViewport(from: 0, to: 100, low: 0, high: 1)
    /// Um traço por trilha do grupo; a escolhida (editável) é a última.
    @State private var samples: [[CGPoint]] = []
    @State private var width: CGFloat = 0
    @State private var drag: GraphDrag?
    @State private var multi = false
    @State private var picked = Set<Int32>()
    @GestureState private var touching = false
    /// Pinça em curso: a janela do começo dela e o arrasto do dedo naquele instante
    /// (o arrasto que segue move a janela junto: zoom e pan de dois dedos).
    @State private var pinchBase: TrackGraphViewport?
    @State private var pinchTranslation: CGSize = .zero
    @State private var lastTranslation: CGSize = .zero
    /// Alça de velocidade (SpeedGraphHandles.kt). Com a força ×power a
    /// inclinação nas pontas vira inclinação^power; o arrasto desfaz a potência.
    private struct SpeedHandle {
        let key: KeyframeItem
        let end: KeyframeItem
        let incoming: Bool
        let handles: [Float]
        let base: Double
        var power: Int = 1
        var influence: Double { Double(incoming ? 1 - handles[2] : handles[0]) }
        private var slope: Double { Double(incoming ? 1 - handles[3] : handles[1]) / influence }
        var velocity: Double { base * curvePowered(slope, power) }
        var frame: Double { Double(key.time) + (Double(end.time) - Double(key.time)) * Double(incoming ? handles[2] : handles[0]) }
        func changed(frame: Double, velocity: Double) -> [Float] {
            var h = handles
            let fraction = min(0.99, max(0.01, (frame - Double(key.time)) / (Double(end.time) - Double(key.time))))
            let s = curveRooted(velocity / base, power)
            if incoming { h[2] = Float(fraction); h[3] = Float(1 - s * (1 - fraction)) }
            else { h[0] = Float(fraction); h[1] = Float(s * fraction) }
            return h
        }
    }
    private var speedHandles: [SpeedHandle] {
        guard speed, keys.count >= 2 else { return [] }
        var result: [SpeedHandle] = []
        let fps: Double = max(1, Double(model.status.compFps))
        for index in 0..<(keys.count - 1) {
            let a: KeyframeItem = keys[index]
            let b: KeyframeItem = keys[index + 1]
            let duration: Double = Double(b.time) - Double(a.time)
            let difference: Double = Double(b.value) - Double(a.value)
            let base: Double = difference * fps / duration
            let raw = model.engine.trackEasing(layer, property: a.property, effect: a.effectIndex, param: a.paramIndex, time: a.time).map(\.floatValue)
            guard raw.count >= 4, base.isFinite, abs(base) > 0.000001 else { continue }
            let ease = CurveEase(interpolation: a.interpolation, x1: raw[0], y1: raw[1], x2: raw[2], y2: raw[3], power: raw.count >= 5 ? Int(raw[4]) : 1)
            guard ease.hasHandles else { continue }
            var h: [Float] = ease.handles
            if a.interpolation == 1 { h = [0.33333334,0.33333334,0.6666667,0.6666667] }
            h[0] = min(0.99,max(0.01,h[0])); h[2] = min(0.99,max(0.01,h[2]))
            let power = a.interpolation == 1 ? 1 : min(3, max(1, ease.power))
            result.append(SpeedHandle(key:a,end:b,incoming:false,handles:h,base:base,power:power))
            result.append(SpeedHandle(key:a,end:b,incoming:true,handles:h,base:base,power:power))
        }
        return result
    }
    private struct GraphDrag {
        let initial: TrackGraphViewport
        let key: KeyframeItem?
        var currentTime: Int32 = 0
        var previous: Int32?
        var next: Int32?
        var began = false
        var group: [KeyframeItem] = []
        var originalPeers: [KeyframeItem] = []
        var groupDelta: Int32 = 0
        var handle: SpeedHandle?
        /// Tocou num losango de trilha irmã: o toque só troca a trilha editável.
        var picked = false
    }
    private var start: Int32 { Int32(floor(viewport.from)) }
    private var end: Int32 { max(start + 1, Int32(ceil(viewport.to))) }
    private var signature: String {
        guard let first = keys.first else { return "" }
        return "\(layer):\(first.property):\(first.effectIndex):\(first.paramIndex):\(speed)"
    }
    /// O grupo inteiro (X/Y/Z, componentes) com a trilha escolhida por último.
    private var group: [[KeyframeItem]] {
        guard let first = keys.first else { return [] }
        let all = graphGroup(model.keyframes[layer] ?? [], keys)
        return all.filter { !curveSameTrack($0[0], first) } + all.filter { curveSameTrack($0[0], first) }
    }
    private func ease(_ key: KeyframeItem) -> CurveEase {
        let h = model.engine.trackEasing(layer, property: key.property, effect: key.effectIndex, param: key.paramIndex, time: key.time).map(\.floatValue)
        return CurveEase(interpolation: key.interpolation, x1: h.count >= 4 ? h[0] : 0.33, y1: h.count >= 4 ? h[1] : 0,
            x2: h.count >= 4 ? h[2] : 0.67, y2: h.count >= 4 ? h[3] : 1, power: h.count >= 5 ? Int(h[4]) : 1)
    }
    /// A mesma conta do motor (`graphCurve`), não uma amostra de frames.
    private func read(from: Double, to: Double, perFrame: Double? = nil) -> [[CGPoint]] {
        guard to > from else { return [] }
        let fps = Float(max(1, Double(model.status.compFps)))
        let density = perFrame ?? (width > 0 ? Double(width) / max(1, to - from) / 2 : 1)
        return group.map { graphCurve(graphCurveKeys($0, ease: ease), from: from, to: to, pointsPerFrame: density, speed: speed, fps: fps) }
    }
    private func reload() { samples = read(from: viewport.from, to: viewport.to) }
    private func fit() {
        let tracks = group
        guard let low = tracks.compactMap({ $0.first?.time }).min(), let high = tracks.compactMap({ $0.last?.time }).max() else { return }
        let to = max(low + 1, high)
        let full = read(from: Double(low), to: Double(to), perFrame: 2).flatMap { $0 }
        let values = full.map { Double($0.y) } + (speed ? speedHandles.map(\.velocity) + [0] : tracks.flatMap { $0 }.map { Double($0.value) })
        let bottom = values.min() ?? 0, top = values.max() ?? 1
        let margin = max(0.1, max(top - bottom, abs(top) * 0.05) * 0.12)
        let timeMargin = max(1, (Double(to) - Double(low)) * 0.06)
        viewport = TrackGraphViewport(from: Double(low) - timeMargin, to: Double(to) + timeMargin, low: bottom - margin, high: top + margin)
        reload()
    }
    /// Cor de uma trilha irmã: X vermelho, Y verde, Z azul.
    private func axisColor(_ key: KeyframeItem) -> Color {
        let axis = key.property == 31 ? Int(key.paramIndex % 4) : (key.property < 12 ? Int(key.property % 3) : 3)
        return [Color(red: 1, green: 0.42, blue: 0.42), Color(red: 0.42, green: 0.84, blue: 0.48), Color(red: 0.36, green: 0.61, blue: 1), AureaColors.muted][axis]
    }
    private func point(_ frame: Double, _ value: Double, _ size: CGSize, _ view: TrackGraphViewport) -> CGPoint {
        CGPoint(x: (frame - view.from) / view.duration * Double(size.width),
            y: Double(size.height) - (value - view.low) / view.range * Double(size.height))
    }
    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 6) {
                if !speed {
                    Button(multi ? "Done (\(picked.count))" : "Select") { multi.toggle(); if !multi { picked.removeAll() } }
                        .font(.aurea(size: 11)).frame(minHeight: 44).accessibilityIdentifier("curve.multi")
                }
                Text(String(format: "%.3g%@", viewport.high, speed ? " /s" : ""))
                    .font(.aurea(size: 10)).foregroundStyle(AureaColors.muted)
                Spacer(minLength: 0)
                Button("−") { viewport = viewport.transformed(zoom: 1 / 1.5) }.frame(width: 44, height: 44)
                Button("+") { viewport = viewport.transformed(zoom: 1.5) }.frame(width: 44, height: 44)
                Button(AureaText.t("panel_ajustar")) { fit() }.font(.aurea(size: 11))
                    .frame(minWidth: 44, minHeight: 44).contentShape(Rectangle()).accessibilityIdentifier("curve.fit")
            }.foregroundStyle(AureaColors.accent)
            if multi && !speed {
                ScrollView(.horizontal, showsIndicators: false) { HStack(spacing: 16) {
                    Button("All") { picked = Set(keys.map(\.time)) }
                    Spacer(minLength: 0)
                    Button("Copy") { _ = model.engine.keyframeSelection(layer, references: references(keys.filter { picked.contains($0.time) }), action: 0, delta: 0) }.disabled(picked.isEmpty)
                    Button("Duplicate") {
                        guard let row = model.layers.first(where: { $0.id == layer }), let last = keys.last else { return }
                        let target = Int64(last.time) + 1 + Int64(row.startFrame) - Int64(row.offsetFrames)
                        guard target >= Int64(Int32.min), target <= Int64(Int32.max) else { return }
                        if model.engine.keyframeSelection(layer, references: references(keys.filter { picked.contains($0.time) }), action: 0, delta: 0) > 0 {
                            model.engine.pasteKeyframes([NSNumber(value: layer)], atFrame: Int32(target)); model.refreshModel(force: true)
                        }
                    }.disabled(picked.isEmpty)
                    Spacer(minLength: 0)
                    Button("Paste") { model.engine.pasteKeyframes([NSNumber(value: layer)], atFrame: Int32(clamping: model.status.playhead)); model.refreshModel(force: true) }
                    Spacer(minLength: 0)
                    Button("Delete") {
                        if model.engine.keyframeSelection(layer, references: references(keys.filter { picked.contains($0.time) }), action: 2, delta: 0) > 0 {
                            picked.removeAll(); model.refreshModel(force: true)
                        }
                    }.disabled(picked.isEmpty)
                }.font(.aurea(size: 11)).frame(minHeight: 44).foregroundStyle(AureaColors.accent) }
            }
            GeometryReader { geometry in
                Canvas { context, size in
                    let view = viewport
                    func plot(_ frame: Double, _ value: Double) -> CGPoint {
                        CGPoint(x: (frame - view.from) / view.duration * Double(size.width),
                            y: Double(size.height) - (value - view.low) / view.range * Double(size.height))
                    }
                    func line(_ a: CGPoint, _ b: CGPoint, color: Color, width: CGFloat = 1) {
                        var path = Path(); path.move(to: a); path.addLine(to: b)
                        context.stroke(path, with: .color(color), lineWidth: width)
                    }
                    for index in 1...3 {
                        let x = size.width * CGFloat(index) / 4, y = size.height * CGFloat(index) / 4
                        line(CGPoint(x: x, y: 0), CGPoint(x: x, y: size.height), color: .white.opacity(0.1))
                        line(CGPoint(x: 0, y: y), CGPoint(x: size.width, y: y), color: .white.opacity(0.1))
                    }
                    let zero = plot(viewport.from, 0).y
                    if zero >= 0 && zero <= size.height { line(CGPoint(x: 0, y: zero), CGPoint(x: size.width, y: zero), color: .white.opacity(0.3)) }
                    let tracks = group
                    for (curveIndex, curve) in samples.enumerated() {
                        let active = curveIndex == samples.count - 1
                        var path = Path()
                        for (index, sample) in curve.enumerated() {
                            let p = plot(Double(sample.x), Double(sample.y))
                            if index == 0 { path.move(to: p) } else { path.addLine(to: p) }
                        }
                        let tint = active || !tracks.indices.contains(curveIndex) ? AureaColors.accent : axisColor(tracks[curveIndex][0]).opacity(0.55)
                        context.stroke(path, with: .color(tint), lineWidth: active ? 2 : 1.5)
                        if !active && !speed && tracks.indices.contains(curveIndex) {
                            for key in tracks[curveIndex] {
                                let p = plot(Double(key.time), Double(key.value))
                                context.fill(Path(ellipseIn: CGRect(x: p.x - 4, y: p.y - 4, width: 8, height: 8)), with: .color(tint))
                            }
                        }
                    }
                    for handle in speedHandles {
                        let p = plot(handle.frame, handle.velocity)
                        line(plot(Double(handle.incoming ? handle.end.time : handle.key.time), handle.velocity), p, color: AureaColors.muted)
                        let dot = Path(ellipseIn: CGRect(x:p.x-9,y:p.y-9,width:18,height:18))
                        context.stroke(dot, with:.color(AureaColors.accent),lineWidth:2.5)
                    }
                    if !speed {
                        for key in keys {
                            let p = plot(Double(key.time), Double(key.value))
                            let dot = Path(ellipseIn: CGRect(x: p.x - 8, y: p.y - 8, width: 16, height: 16))
                            context.fill(dot, with: .color((multi ? picked.contains(key.time) : key.time == model.curveSelectedTime) ? .white : AureaColors.accent))
                        }
                    }
                    let x = plot(Double(model.localPlayhead), 0).x
                    if x >= 0 && x <= size.width { line(CGPoint(x: x, y: 0), CGPoint(x: x, y: size.height), color: AureaColors.danger) }
                }.clipped().contentShape(Rectangle()).accessibilityIdentifier("curve.trackGraph")
                .gesture(DragGesture(minimumDistance: 0)
                    .updating($touching) { _, active, _ in active = true }
                    .onChanged { move($0, size: geometry.size) }
                    .onEnded { _ in finish(toggle: true) })
                // Pinça: zoom no tempo e no valor; o dedo que anda leva a janela junto.
                .simultaneousGesture(MagnificationGesture()
                    .onChanged { scale in pinch(Double(scale), size: geometry.size) }
                    .onEnded { _ in pinchBase = nil })
                .onAppear { width = geometry.size.width }
                .onChange(of: geometry.size.width) { value in width = value; reload() }
            }
            HStack {
                Text(String(format: "%.3g%@", viewport.low, speed ? " /s" : ""))
                Spacer()
                Text("\(start)–\(end) f")
            }.font(.aurea(size: 10)).foregroundStyle(AureaColors.muted)
        }
        .onAppear { fit() }
        .onChange(of: signature) { _ in finish(); multi = false; picked.removeAll(); fit() }
        .onChange(of: viewport) { _ in reload() }
        // O cabeçote saiu da janela (scrub, play, outro trecho): a janela vai atrás dele.
        .onChange(of: model.localPlayhead) { frame in
            let f = Double(frame)
            if drag == nil && (f < viewport.from || f > viewport.to) {
                let half = viewport.duration / 2
                viewport = TrackGraphViewport(from: f - half, to: f + half, low: viewport.low, high: viewport.high)
            }
        }
        .onChange(of: model.status.modelRevision) { _ in reload(); picked.formIntersection(Set(keys.map(\.time))) }
        .onChange(of: touching) { active in if !active && drag?.began == true { finish() } }
        .onDisappear { finish() }
    }
    private func pinch(_ scale: Double, size: CGSize) {
        guard size.width > 0, size.height > 0, scale.isFinite, scale > 0 else { return }
        if pinchBase == nil {
            // O segundo dedo transforma o gesto em zoom: a edição da marca (se
            // começou) fecha aqui e o resto do arrasto não agarra nada.
            if drag?.began == true { finish() }
            pinchBase = drag?.initial ?? viewport
            pinchTranslation = lastTranslation
            drag = GraphDrag(initial: viewport, key: nil, picked: true)
        }
        guard let base = pinchBase else { return }
        viewport = base.transformed(zoom: scale,
            dx: Double((lastTranslation.width - pinchTranslation.width) / size.width),
            dy: Double((lastTranslation.height - pinchTranslation.height) / size.height))
    }
    private func move(_ value: DragGesture.Value, size: CGSize) {
        guard size.width > 0, size.height > 0 else { return }
        lastTranslation = value.translation
        if let base = pinchBase {
            viewport = base.transformed(zoom: max(0.25, min(4, base.duration / viewport.duration)),
                dx: Double((lastTranslation.width - pinchTranslation.width) / size.width),
                dy: Double((lastTranslation.height - pinchTranslation.height) / size.height))
            return
        }
        if drag == nil {
            var nearest: KeyframeItem?
            var distance = CGFloat(28 * 28) // 56 pt de alvo: o dedo acerta a marca sem mirar
            if !speed {
                for key in keys {
                    let p = point(Double(key.time), Double(key.value), size, viewport)
                    let dx = p.x - value.startLocation.x, dy = p.y - value.startLocation.y
                    let d = dx * dx + dy * dy
                    if d <= distance { nearest = key; distance = d }
                }
            }
            drag = GraphDrag(initial: viewport, key: nearest, currentTime: nearest?.time ?? 0,
                previous: nearest.flatMap { selected in keys.last(where: { $0.time < selected.time })?.time },
                next: nearest.flatMap { selected in keys.first(where: { $0.time > selected.time })?.time })
            if let nearest {
                let peers = model.graphKeyGroup(layer, nearest)
                drag?.originalPeers = peers
                let tracks = (model.keyframes[layer] ?? []).filter { candidate in peers.contains { curveSameTrack($0, candidate) } }
                drag?.previous = tracks.filter { $0.time < nearest.time }.map(\.time).max()
                drag?.next = tracks.filter { $0.time > nearest.time }.map(\.time).min()
            }
            if speed {
                for handle in speedHandles {
                    let p = point(handle.frame,handle.velocity,size,viewport)
                    let dx = p.x-value.startLocation.x, dy = p.y-value.startLocation.y
                    let d = dx*dx+dy*dy
                    if d <= distance { drag?.handle = handle; distance = d }
                }
            }
            // Losango de uma trilha IRMÃ: tocar troca a trilha editável (sem arrastar).
            if nearest == nil && !speed, let first = keys.first {
                var sibling: KeyframeItem?
                var best = CGFloat(28 * 28)
                for key in group.flatMap({ $0 }) where !curveSameTrack(key, first) {
                    let p = point(Double(key.time), Double(key.value), size, viewport)
                    let dx = p.x - value.startLocation.x, dy = p.y - value.startLocation.y
                    if dx * dx + dy * dy <= best { sibling = key; best = dx * dx + dy * dy }
                }
                if let sibling {
                    drag?.picked = true
                    model.curveProperty = sibling.property; model.curveEffect = sibling.effectIndex
                    model.curveParam = sibling.paramIndex; model.curveSelectedTime = sibling.time
                    return
                }
            }
            if multi, let nearest { drag?.group = picked.contains(nearest.time) ? keys.filter { picked.contains($0.time) } : [nearest] }
            if let nearest { model.curveSelectedTime = nearest.time }
        }
        guard var current = drag, !current.picked else { return }
        if let handle = current.handle {
            guard current.began || hypot(value.translation.width,value.translation.height) >= 4 else { return }
            let h = handle.changed(frame:handle.frame+Double(value.translation.width/size.width)*current.initial.duration,
                velocity:handle.velocity-Double(value.translation.height/size.height)*current.initial.range)
            guard h.allSatisfy({ $0.isFinite }) else { return }
            if !current.began { model.beginGesture("editar velocidade do intervalo");current.began = true }
            for peer in model.graphKeyGroup(layer, handle.key) {
                model.engine.editTrackKey(layer,property:peer.property,effect:peer.effectIndex,param:peer.paramIndex,
                    time:peer.time,action:3,value:peer.value,targetTime:peer.time,interpolation:2,handles:h.map { NSNumber(value:$0) })
            }
            drag = current; model.refreshModel(force:true)
        } else if let key = current.key {
            guard current.began || hypot(value.translation.width, value.translation.height) >= 4 else { return }
            let changed = Double(key.value) - Double(value.translation.height / size.height) * current.initial.range
            guard changed.isFinite, abs(changed) <= Double(Float.greatestFiniteMagnitude) else { return }
            if !current.began { model.beginGesture("editar keyframe no gráfico"); current.began = true }
            if multi && !current.group.isEmpty {
                let desired = Double(value.translation.width / size.width) * current.initial.duration
                guard desired.isFinite else { return }
                let delta = Int32(clamping: Int64(min(Double(Int32.max), max(Double(Int32.min), desired.rounded()))))
                let step = Int64(delta) - Int64(current.groupDelta)
                if step >= Int64(Int32.min) && step <= Int64(Int32.max) && step != 0,
                   model.engine.keyframeSelection(layer, references: references(current.group, offset: current.groupDelta), action: 1, delta: Int32(step)) > 0 {
                    current.groupDelta = delta
                    picked = Set(current.group.map { Int32(clamping: Int64($0.time) + Int64(delta)) })
                    model.refreshModel(force: true)
                }
                drag = current
                return
            }
            let low = current.previous.map { Int64($0) + 1 } ?? Int64(Int32.min)
            let high = current.next.map { Int64($0) - 1 } ?? Int64(Int32.max)
            let desired = (Double(key.time) + Double(value.translation.width / size.width) * current.initial.duration).rounded()
            guard desired.isFinite, low <= high else { return }
            let target = Int32(min(Double(high), max(Double(low), desired)))
            let live = key
            let peers = current.originalPeers.isEmpty ? [key] : current.originalPeers
            for peer in peers {
                if target != current.currentTime {
                    model.engine.editTrackKey(layer, property: peer.property, effect: peer.effectIndex, param: peer.paramIndex,
                        time: current.currentTime, action: 2, value: peer.value, targetTime: target, interpolation: peer.interpolation, handles: [])
                }
                if peer.property == key.property || (model.scaleAxesLinked && (3...5).contains(key.property)) {
                    let amount = peer.property == key.property || live.value == 0 ? Float(changed) : peer.value * (Float(changed) / live.value)
                    model.engine.editTrackKey(layer, property: peer.property, effect: peer.effectIndex, param: peer.paramIndex,
                        time: target, action: 0, value: amount, targetTime: target, interpolation: peer.interpolation, handles: [])
                }
            }
            current.currentTime = target
            drag = current
            model.curveSelectedTime = target
            model.refreshModel(force: true)
        } else {
            viewport = current.initial.transformed(zoom: 1,
                dx: Double(value.translation.width / size.width), dy: Double(value.translation.height / size.height))
        }
    }
    private func references(_ selected: [KeyframeItem], offset: Int32 = 0) -> [NSNumber] {
        selected.flatMap { key in
            [NSNumber(value: key.property), NSNumber(value: key.effectIndex), NSNumber(value: key.paramIndex), NSNumber(value: Int64(key.time) + Int64(offset))]
        }
    }
    private func finish(toggle: Bool = false) {
        if toggle && multi, let current = drag, !current.began, let key = current.key {
            if picked.contains(key.time) { picked.remove(key.time) } else { picked.insert(key.time) }
        }
        let began = drag?.began == true; drag = nil; pinchBase = nil
        if began { model.endGesture() }
    }
}
