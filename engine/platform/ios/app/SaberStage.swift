import SwiftUI

// =============================================================================
// SABER no palco — port de editor/SaberStage.kt (Android).
//
// Com o cartão do efeito aurea.light.saber aberto e o núcleo "Único", o Início
// e o Fim do núcleo aparecem como dois pontos sobre a camada, ligados por uma
// linha tracejada. Arrastar um ponto o move — o valor é a fração da caixa da
// camada, o mesmo dos campos X/Y do cartão; o arrasto inteiro é UM passo de
// desfazer e, com o ponto animado, vira keyframe no cabeçote. Só os pontos
// recebem toque: fora deles, o palco segue normal.
// =============================================================================
@MainActor final class SaberStageState: ObservableObject {
    static let shared = SaberStageState()
    static let typeParam: UInt32 = 6
    static let start: UInt32 = 7
    static let end: UInt32 = 8

    @Published var layer: Int64 = 0
    @Published var effect: Int32 = -1
    @Published var revision = 0

    func open(_ layerId: Int64, _ effectId: Int32) {
        layer = layerId
        effect = effectId
    }
    func close(_ effectId: Int32) {
        if effect == effectId { effect = -1 }
    }
}

/// Cartão do Saber aberto: liga os pontos no palco enquanto ele estiver à vista.
@MainActor struct SaberCardTools: View {
    @EnvironmentObject private var model: AureaModel
    let effectId: UInt32
    private var id32: Int32 { Int32(truncatingIfNeeded: effectId) }

    var body: some View {
        Color.clear
            .frame(width: 0, height: 0)
            .accessibilityHidden(true)
            .onAppear { if let layer = model.primarySelection { SaberStageState.shared.open(layer, id32) } }
            .onDisappear { SaberStageState.shared.close(id32) }
    }
}

private let saberInk = Color(red: 0.44, green: 0.76, blue: 1.0)

/// Homografia do quadrado unitário da camada (u, v) para os 4 cantos na
/// composição — (0,0) (1,0) (1,1) (0,1). Exata também com a camada girada em
/// 3D (cantos em perspectiva); com um paralelogramo é a conta afim de antes.
private func saberHomography(_ c: [Float]) -> [Float] {
    let x0 = c[0], y0 = c[1], x1 = c[2], y1 = c[3]
    let x2 = c[4], y2 = c[5], x3 = c[6], y3 = c[7]
    let sx = x0 - x1 + x2 - x3
    let sy = y0 - y1 + y2 - y3
    var g: Float = 0
    var h: Float = 0
    if abs(sx) > 1e-4 || abs(sy) > 1e-4 {
        let dx1 = x1 - x2, dx2 = x3 - x2
        let dy1 = y1 - y2, dy2 = y3 - y2
        let den = dx1 * dy2 - dx2 * dy1
        if abs(den) > 1e-9 {
            g = (sx * dy2 - dx2 * sy) / den
            h = (dx1 * sy - sx * dy1) / den
        }
    }
    return [x1 - x0 + g * x1, x3 - x0 + h * x3, x0,
            y1 - y0 + g * y1, y3 - y0 + h * y3, y0, g, h]
}

private func saberToComp(_ c: [Float], _ u: Float, _ v: Float) -> CGPoint {
    let m = saberHomography(c)
    var w = m[6] * u + m[7] * v + 1
    if abs(w) < 1e-6 { w = 1e-6 }
    return CGPoint(x: CGFloat((m[0] * u + m[1] * v + m[2]) / w),
                   y: CGFloat((m[3] * u + m[4] * v + m[5]) / w))
}

private func saberToUv(_ c: [Float], _ x: Float, _ y: Float) -> (Float, Float)? {
    // x·(g u + h v + 1) = a u + b v + c  →  sistema 2×2 em (u, v).
    let m = saberHomography(c)
    let a11 = m[0] - m[6] * x, a12 = m[1] - m[7] * x
    let a21 = m[3] - m[6] * y, a22 = m[4] - m[7] * y
    let det = a11 * a22 - a12 * a21
    guard abs(det) > 1e-6 else { return nil }
    let r1 = x - m[2], r2 = y - m[5]
    return ((r1 * a22 - a12 * r2) / det, (a11 * r2 - r1 * a21) / det)
}

/// Os dois pontos do núcleo único. Só eles recebem toque.
@MainActor struct SaberStageOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var saber = SaberStageState.shared
    @ObservedObject private var viewZoom = StageViewZoom.shared
    @State private var dragging: UInt32? = nil

    var body: some View {
        if let points = activePoints(), let c = corners() {
            GeometryReader { geo in
                let placed = StageZoomMath.fit(size: geo.size,
                                               composition: CGSize(width: CGFloat(model.compositionWidth), height: CGFloat(model.compositionHeight)),
                                               zoom: viewZoom.zoom, pan: viewZoom.pan)
                let a = screen(c, points.0, placed.scale, placed.origin)
                let b = screen(c, points.1, placed.scale, placed.origin)
                ZStack {
                    Path { p in p.move(to: a); p.addLine(to: b) }
                        .stroke(StageInk.outlineUnder, lineWidth: 3)
                        .allowsHitTesting(false)
                    Path { p in p.move(to: a); p.addLine(to: b) }
                        .stroke(Color.white.opacity(0.85), style: StrokeStyle(lineWidth: 1.5, dash: [6, 4]))
                        .allowsHitTesting(false)
                    handle(SaberStageState.start, at: a, filled: false, fit: placed.scale, origin: placed.origin)
                    handle(SaberStageState.end, at: b, filled: true, fit: placed.scale, origin: placed.origin)
                }
                .coordinateSpace(name: "saberStage")
            }
        }
    }

    /// (início, fim) em fração da camada, ou nil fora do modo.
    private func activePoints() -> (CGPoint, CGPoint)? {
        guard saber.effect >= 0, model.selection.count == 1, model.primarySelection == saber.layer,
              let l = model.selectedLayer, !l.locked else { return nil }
        _ = saber.revision
        _ = model.status.playhead
        let rows = model.engine.effectParams(forLayer: saber.layer, effectId: UInt32(truncatingIfNeeded: saber.effect))
        func value(_ index: UInt32) -> [Float]? {
            guard let row = rows.first(where: { ($0["index"] as? NSNumber)?.uint32Value == index }) else { return nil }
            return (row["value"] as? [NSNumber])?.map(\.floatValue)
        }
        guard let type = value(SaberStageState.typeParam)?.first, Int(type.rounded()) == 0,
              let s = value(SaberStageState.start), let e = value(SaberStageState.end), s.count >= 2, e.count >= 2 else { return nil }
        return (CGPoint(x: CGFloat(s[0]), y: CGFloat(s[1])), CGPoint(x: CGFloat(e[0]), y: CGFloat(e[1])))
    }

    /// Cantos da caixa da camada na composição; a camada de ajuste cobre a composição.
    private func corners() -> [Float]? {
        if model.selectedLayer?.adjustment == true {
            let w = Float(model.compositionWidth), h = Float(model.compositionHeight)
            return [0, 0, w, 0, w, h, 0, h]
        }
        var c: [Float] = []
        return StageGeom.corners(model.detail, &c) && c.count == 8 ? c : nil
    }

    private func screen(_ c: [Float], _ uv: CGPoint, _ fit: CGFloat, _ origin: CGPoint) -> CGPoint {
        let q = saberToComp(c, Float(uv.x), Float(uv.y))
        return CGPoint(x: origin.x + q.x * fit, y: origin.y + q.y * fit)
    }

    private func handle(_ index: UInt32, at p: CGPoint, filled: Bool, fit: CGFloat, origin: CGPoint) -> some View {
        let isStart = index == SaberStageState.start
        return ZStack {
            Circle().fill(StageInk.outlineUnder).frame(width: 20, height: 20)
            if filled {
                Circle().fill(saberInk).frame(width: 16, height: 16)
            } else {
                Circle().stroke(saberInk, lineWidth: 2.5).frame(width: 13, height: 13)
            }
        }
        .frame(width: 44, height: 44)
        .contentShape(Circle())
        .position(p)
        .gesture(DragGesture(minimumDistance: 0, coordinateSpace: .named("saberStage"))
            .onChanged { v in drag(index, v.location, fit: fit, origin: origin) }
            .onEnded { _ in end() })
        .accessibilityElement()
        .accessibilityLabel(AureaText.t(isStart ? "saber_point_start" : "saber_point_end"))
        .accessibilityHint(AureaText.t("saber_points_hint"))
        .accessibilityIdentifier(isStart ? "stage.saber.start" : "stage.saber.end")
    }

    private func drag(_ index: UInt32, _ location: CGPoint, fit: CGFloat, origin: CGPoint) {
        guard fit > 0, let c = corners(), let layer = model.primarySelection, saber.effect >= 0 else { return }
        guard let uv = saberToUv(c, Float((location.x - origin.x) / fit), Float((location.y - origin.y) / fit)),
              uv.0.isFinite, uv.1.isFinite else { return }
        let effect = UInt32(truncatingIfNeeded: saber.effect)
        let rows = model.engine.effectParams(forLayer: layer, effectId: effect)
        guard let row = rows.first(where: { ($0["index"] as? NSNumber)?.uint32Value == index }) else { return }
        if dragging == nil {
            dragging = index
            model.beginGesture("mover ponto do Saber")
        }
        let animated = row["animated"] as? Bool ?? false
        if animated {
            model.engine.keyParameter(layer, property: 31, effect: effect, param: index * 4, time: model.localPlayhead, value: uv.0)
            model.engine.keyParameter(layer, property: 31, effect: effect, param: index * 4 + 1, time: model.localPlayhead, value: uv.1)
        } else {
            // O vetor inteiro num comando só (como o writeVector do cartão).
            let old = (row["value"] as? [NSNumber])?.map(\.floatValue) ?? []
            model.engine.setEffectColor(effect, forLayer: layer, paramIndex: index, r: uv.0, g: uv.1,
                                        b: old.count > 2 ? old[2] : 0, a: old.count > 3 ? old[3] : 0)
        }
        model.commitPendingCommands()
        saber.revision += 1
    }

    private func end() {
        guard dragging != nil else { return }
        dragging = nil
        model.endGesture()
    }
}
