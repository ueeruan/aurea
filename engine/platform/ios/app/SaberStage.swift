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

private func saberToComp(_ c: [Float], _ u: Float, _ v: Float) -> CGPoint {
    CGPoint(x: CGFloat(c[0] + u * (c[2] - c[0]) + v * (c[6] - c[0])),
            y: CGFloat(c[1] + u * (c[3] - c[1]) + v * (c[7] - c[1])))
}

private func saberToUv(_ c: [Float], _ x: Float, _ y: Float) -> (Float, Float)? {
    let ax = c[2] - c[0], ay = c[3] - c[1], bx = c[6] - c[0], by = c[7] - c[1]
    let det = ax * by - ay * bx
    guard abs(det) > 1e-6 else { return nil }
    let px = x - c[0], py = y - c[1]
    return ((px * by - py * bx) / det, (ax * py - ay * px) / det)
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
