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

// =============================================================================
// PONTO DE EFEITO no palco (P1-5) — port de editor/EffectPointStage.kt.
//
// O parâmetro Point2D (ou os X/Y de um Point3D) em fração da camada que a
// pessoa tocou no cartão aparece como uma alça sobre a camada. Arrastar a alça
// escreve o ponto (o mesmo valor dos campos X/Y) num passo de desfazer; com o
// ponto animado, vira keyframe no cabeçote. Só aparece com o parâmetro ATIVO
// (cartão aberto e o ponto escolhido por toque): o palco 3D continua seco no
// resto do tempo. Mesma geometria do Saber (homografia da caixa da camada).
// =============================================================================
// Omega efeitos (área 07, P1-2): o parâmetro ATIVO no preview. Tocar no rótulo
// de um Point/Xyz/Orientação no painel o ativa: o guia "deslize para ajustar"
// aparece no centro do preview e QUALQUER arrasto no preview ajusta o valor de
// forma relativa (`EffectParamDragOverlay`); a alça do ponto em fração da camada
// fica como marcador (arrastar nela continua absoluto). Toque sem arrasto no
// preview, fechar os controles ou trocar de efeito desativa.
@MainActor final class EffectPointStageState: ObservableObject {
    static let shared = EffectPointStageState()

    /// Camada, efeito e índice do parâmetro ativo (−1 = nenhum).
    @Published private(set) var layer: Int64 = 0
    @Published private(set) var effect: UInt32 = 0
    @Published private(set) var param: Int32 = -1
    /// O controle do parâmetro ativo (`FxParamControl.point/xyz/orientation`).
    @Published private(set) var control = 0
    @Published var revision = 0
    /// O guia: nome do parâmetro, tempos e um contador que muda a cada exibição.
    @Published private(set) var guideLabel = ""
    @Published private(set) var guide: FxParamMath.Guide = .short
    @Published private(set) var guideToken = 0
    /// O último guia já mostrado (o overlay recriado não repete o mesmo).
    var guideShown = 0

    var active: Bool { param >= 0 }

    /// Ativa o parâmetro; `guide` não nulo mostra o guia de novo.
    func activate(_ layerId: Int64, effect effectId: UInt32, param index: UInt32, control kind: Int,
                  label: String, guide timing: FxParamMath.Guide?) {
        if layer != layerId { layer = layerId }
        if effect != effectId { effect = effectId }
        if param != Int32(index) { param = Int32(index) }
        if control != kind { control = kind }
        if guideLabel != label { guideLabel = label }
        if let timing { guide = timing; guideToken += 1 }
    }
    /// Sai do modo e apaga o guia que ainda estivesse na tela.
    func deactivate() {
        if param != -1 { param = -1 }
        if !guideLabel.isEmpty { guideLabel = "" }
    }
    func isActive(_ layerId: Int64?, effect effectId: UInt32, param index: UInt32) -> Bool {
        param >= 0 && layerId == layer && effect == effectId && UInt32(param) == index
    }
}

/// Parâmetro que pode ser ativado no preview: ponto 2D/3D visível (par do
/// `effectParamDraggable` do EffectPointStage.kt).
func fxEffectParamDraggable(type: Int, flags: UInt32) -> Bool {
    (type == fxParamPoint2D || type == fxParamPoint3D) && flags & 16 == 0
}

/// Escreve um parâmetro de efeito (escalar ou vetor) num comando só; com a
/// trilha animada, keyframe no cabeçote só nos componentes `changed` (o mesmo
/// contrato do `writeVector` do painel). Quem chama abre/fecha o passo de desfazer.
@MainActor func fxWriteEffectParam(_ model: AureaModel, layer: Int64, effect: UInt32, param: EffectParamItem,
                                   values: [Float], changed: [Int]) {
    let count = Swift.min(fxComponentCount(Int(param.type)), 4)
    guard count > 0, model.selectedLayer?.locked != true, param.flags & 16 == 0 else { return }
    var vector = Array((param.value + [0, 0, 0, 1]).prefix(4))
    for component in changed where component >= 0 && component < count && component < values.count {
        guard values[component].isFinite else { return }
        vector[component] = values[component]
    }
    if param.animated {
        for component in changed where component >= 0 && component < count {
            model.engine.keyParameter(layer, property: 31, effect: effect, param: param.index * 4 + UInt32(component),
                                      time: model.localPlayhead, value: vector[component])
        }
    } else if count == 1 {
        model.engine.setEffect(effect, forLayer: layer, paramIndex: param.index, value: vector[0])
    } else {
        model.engine.setEffectColor(effect, forLayer: layer, paramIndex: param.index,
                                    r: vector[0], g: vector[1], b: vector[2], a: vector[3])
    }
    model.commitPendingCommands()
}

/// A alça do ponto ativo. Só ela recebe toque: fora dela, o palco segue normal.
@MainActor struct EffectPointStageOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var point = EffectPointStageState.shared
    @ObservedObject private var saber = SaberStageState.shared
    @ObservedObject private var viewZoom = StageViewZoom.shared
    @State private var dragging = false

    var body: some View {
        if let active = activePoint(), let c = corners() {
            GeometryReader { geo in
                let placed = StageZoomMath.fit(size: geo.size,
                                               composition: CGSize(width: CGFloat(model.compositionWidth), height: CGFloat(model.compositionHeight)),
                                               zoom: viewZoom.zoom, pan: viewZoom.pan)
                let q = saberToComp(c, Float(active.uv.x), Float(active.uv.y))
                let p = CGPoint(x: placed.origin.x + q.x * placed.scale, y: placed.origin.y + q.y * placed.scale)
                ZStack {
                    handle(at: p, label: active.label, fit: placed.scale, origin: placed.origin)
                }
                .coordinateSpace(name: "effectPointStage")
            }
        }
    }

    /// (ponto em fração da camada, nome do parâmetro), ou nil fora do modo.
    private func activePoint() -> (uv: CGPoint, label: String)? {
        // Omega efeitos (P1-2): o marcador do parâmetro ATIVO (a orientação não tem ponto).
        guard point.param >= 0, point.control != FxParamControl.orientation, model.selection.count == 1,
              model.primarySelection == point.layer, let l = model.selectedLayer, !l.locked else { return nil }
        _ = point.revision
        _ = model.status.playhead
        guard let row = model.effectParams.first(where: { $0.index == UInt32(point.param) }), model.selectedEffectId == point.effect,
              fxStagePointEligible(type: Int(row.type), flags: row.flags), row.enabled, row.value.count >= 2,
              row.value[0].isFinite, row.value[1].isFinite else { return nil }
        // O Saber (núcleo único) já desenha os dois pontos dele.
        if saber.effect >= 0, UInt32(truncatingIfNeeded: saber.effect) == point.effect,
           UInt32(point.param) == SaberStageState.start || UInt32(point.param) == SaberStageState.end { return nil }
        return (CGPoint(x: CGFloat(row.value[0]), y: CGFloat(row.value[1])), row.label)
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

    /// Anel com miolo e cruz fina (lê como "posição", não como o anel do Saber).
    private func handle(at p: CGPoint, label: String, fit: CGFloat, origin: CGPoint) -> some View {
        ZStack {
            Path { path in
                path.move(to: CGPoint(x: 0, y: 22)); path.addLine(to: CGPoint(x: 44, y: 22))
                path.move(to: CGPoint(x: 22, y: 0)); path.addLine(to: CGPoint(x: 22, y: 44))
            }.stroke(StageInk.outlineUnder, lineWidth: 3)
            Path { path in
                path.move(to: CGPoint(x: 7, y: 22)); path.addLine(to: CGPoint(x: 13, y: 22))
                path.move(to: CGPoint(x: 31, y: 22)); path.addLine(to: CGPoint(x: 37, y: 22))
                path.move(to: CGPoint(x: 22, y: 7)); path.addLine(to: CGPoint(x: 22, y: 13))
                path.move(to: CGPoint(x: 22, y: 31)); path.addLine(to: CGPoint(x: 22, y: 37))
            }.stroke(Color.white, lineWidth: 1.5)
            Circle().fill(StageInk.outlineUnder).frame(width: 22, height: 22)
            Circle().stroke(saberInk, lineWidth: 2.5).frame(width: 15, height: 15)
            Circle().fill(saberInk).frame(width: 6, height: 6)
        }
        .frame(width: 44, height: 44)
        .contentShape(Circle())
        .position(p)
        .gesture(DragGesture(minimumDistance: 0, coordinateSpace: .named("effectPointStage"))
            .onChanged { v in drag(v.location, fit: fit, origin: origin) }
            .onEnded { _ in end() })
        .accessibilityElement()
        .accessibilityLabel(AureaText.t("fxui_stage_point", label))
        .accessibilityHint(AureaText.t("fxui_stage_point_hint"))
        .accessibilityIdentifier("stage.effect_point")
    }

    private func drag(_ location: CGPoint, fit: CGFloat, origin: CGPoint) {
        guard fit > 0, let c = corners(), let layer = model.primarySelection, point.param >= 0 else { return }
        guard let uv = saberToUv(c, Float((location.x - origin.x) / fit), Float((location.y - origin.y) / fit)),
              uv.0.isFinite, uv.1.isFinite else { return }
        let effect = point.effect, index = UInt32(point.param)
        let rows = model.engine.effectParams(forLayer: layer, effectId: effect)
        guard let row = rows.first(where: { ($0["index"] as? NSNumber)?.uint32Value == index }) else { return }
        if !dragging {
            dragging = true
            model.beginGesture("mover ponto do efeito")
        }
        if row["animated"] as? Bool ?? false {
            model.engine.keyParameter(layer, property: 31, effect: effect, param: index * 4, time: model.localPlayhead, value: uv.0)
            model.engine.keyParameter(layer, property: 31, effect: effect, param: index * 4 + 1, time: model.localPlayhead, value: uv.1)
        } else {
            // O vetor inteiro num comando só (como o writeVector do cartão).
            let old = (row["value"] as? [NSNumber])?.map(\.floatValue) ?? []
            model.engine.setEffectColor(effect, forLayer: layer, paramIndex: index, r: uv.0, g: uv.1,
                                        b: old.count > 2 ? old[2] : 0, a: old.count > 3 ? old[3] : 0)
        }
        model.commitPendingCommands()
        model.loadParams(layerId: layer, effectId: effect)
        point.revision += 1
    }

    private func end() {
        guard dragging else { return }
        dragging = false
        model.endGesture()
    }
}

// =============================================================================
// Omega efeitos (área 07, P1-2): ARRASTO RELATIVO no preview com o parâmetro
// ativo. Fica ABAIXO das alças das ferramentas (rig, malha, fantoche, sabre, a
// alça do ponto, roto), que continuam com prioridade; acima do resto do palco.
// - Ponto em fração da camada (`kParamRelative`): o valor segue o dedo (a
//   diferença das posições do dedo, convertidas para a camada).
// - Outros pontos e XYZ (dois primeiros componentes): Δ px de tela × passo.
// - Orientação: trackball (`FxParamMath.trackball`), posições normalizadas.
// Um passo de desfazer por gesto. Toque sem arrasto desativa o parâmetro.
// =============================================================================
@MainActor struct EffectParamDragOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var point = EffectPointStageState.shared
    @ObservedObject private var viewZoom = StageViewZoom.shared
    @Environment(\.displayScale) private var displayScale
    @State private var dragging = false
    @State private var startValues: [Float] = []
    @State private var liveValues: [Float] = []
    @State private var startUv: CGPoint?
    @State private var last: CGPoint = .zero

    var body: some View {
        if let row = activeRow() {
            GeometryReader { geo in
                ZStack {
                    Color.clear
                        .contentShape(Rectangle())
                        .gesture(DragGesture(minimumDistance: 0, coordinateSpace: .local)
                            .onChanged { value in changed(value, row: row, size: geo.size) }
                            .onEnded { _ in ended() })
                        .accessibilityHidden(true)
                    EffectParamGuide()
                        .allowsHitTesting(false)
                }
            }
            .onDisappear { if dragging { dragging = false; model.endGesture() } }
        }
    }

    /// O parâmetro ativo, quando o palco pode editá-lo.
    private func activeRow() -> EffectParamItem? {
        guard point.active, model.selection.count == 1, model.primarySelection == point.layer,
              let layer = model.selectedLayer, !layer.locked, model.selectedEffectId == point.effect,
              let row = model.effectParams.first(where: { $0.index == UInt32(point.param) }),
              row.enabled, fxEffectParamDraggable(type: Int(row.type), flags: row.flags) else { return nil }
        return row
    }

    private func changed(_ value: DragGesture.Value, row: EffectParamItem, size: CGSize) {
        if !dragging {
            guard hypot(value.translation.width, value.translation.height) >= FxParamMath.TOUCH_SLOP else { return }
            dragging = true
            model.beginGesture("ajustar " + row.label)
            startValues = Array((row.value + [0, 0, 0, 1]).prefix(4))
            liveValues = startValues
            last = value.startLocation
            startUv = uv(value.startLocation, size: size)
        }
        guard let layer = model.primarySelection else { return }
        let control = FxParamMath.controlFor(row.control, Int(row.type))
        let typedMin = Swift.min(row.hardMin, row.minValue)
        let typedMax = Swift.max(row.hardMax, row.maxValue)
        func bounded(_ v: Float, _ component: Int) -> Float {
            let (lo, hi) = FxParamRowMath.dragBounds(min: typedMin, max: typedMax, from: startValues[component])
            return Swift.min(Swift.max(v, lo), hi)
        }
        var next = liveValues
        var changedComponents: [Int] = [0, 1]
        if control == FxParamControl.orientation && Int(row.type) == fxParamPoint3D {
            // Trackball entre a posição anterior e a atual, normalizadas em [−1, 1]
            // na composição (centro = 0).
            guard let a = normalized(last, size: size), let b = normalized(value.location, size: size) else { return }
            let turned = FxParamMath.trackball(alpha: next[0], beta: next[1], gamma: next[2], x0: a.x, y0: a.y, x1: b.x, y1: b.y)
            for component in 0..<3 { next[component] = bounded(turned[component], component) }
            changedComponents = [0, 1, 2]
        } else if row.flags & 8 != 0, let from = startUv, let now = uv(value.location, size: size) {
            // Em fração da camada: segue o dedo.
            next[0] = bounded(startValues[0] + Float(now.x - from.x), 0)
            next[1] = bounded(startValues[1] + Float(now.y - from.y), 1)
        } else {
            // Referência: valor + (dx, dy em px de tela) × passo do motor.
            let step: Float = row.step > 0 && row.step.isFinite ? row.step : 1
            let dx = Float(value.translation.width * displayScale)
            let dy = Float(value.translation.height * displayScale)
            next[0] = bounded(startValues[0] + dx * step, 0)
            next[1] = bounded(startValues[1] + dy * step, 1)
        }
        last = value.location
        guard next != liveValues else { return }
        liveValues = next
        fxWriteEffectParam(model, layer: layer, effect: point.effect, param: row, values: next, changed: changedComponents)
        model.loadParams(layerId: layer, effectId: point.effect)
        point.revision += 1
    }

    private func ended() {
        if dragging {
            dragging = false
            model.endGesture()
        } else {
            // Toque sem arrasto: sai do modo e não faz mais nada.
            point.deactivate()
        }
        startUv = nil
    }

    /// Posição na tela → composição normalizada em [−1, 1] (para o trackball).
    private func normalized(_ location: CGPoint, size: CGSize) -> (x: Float, y: Float)? {
        let w = CGFloat(model.compositionWidth), h = CGFloat(model.compositionHeight)
        let placed = StageZoomMath.fit(size: size, composition: CGSize(width: w, height: h), zoom: viewZoom.zoom, pan: viewZoom.pan)
        guard placed.scale > 0, w > 0, h > 0 else { return nil }
        let cx = (location.x - placed.origin.x) / placed.scale
        let cy = (location.y - placed.origin.y) / placed.scale
        return (Float(cx / w * 2 - 1), Float(cy / h * 2 - 1))
    }

    /// Posição na tela → fração da camada (a mesma homografia da alça).
    private func uv(_ location: CGPoint, size: CGSize) -> CGPoint? {
        let placed = StageZoomMath.fit(size: size,
                                       composition: CGSize(width: CGFloat(model.compositionWidth), height: CGFloat(model.compositionHeight)),
                                       zoom: viewZoom.zoom, pan: viewZoom.pan)
        guard placed.scale > 0, let c = corners(),
              let uv = saberToUv(c, Float((location.x - placed.origin.x) / placed.scale), Float((location.y - placed.origin.y) / placed.scale)),
              uv.0.isFinite, uv.1.isFinite else { return nil }
        return CGPoint(x: CGFloat(uv.0), y: CGFloat(uv.1))
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
}

/// O GUIA "deslize para ajustar" no centro do preview: glifo de deslizar, o
/// texto (10, `text`) e o nome do parâmetro (10 negrito, `accent`); entra, espera
/// e sai nos tempos de `FxParamMath.Guide` (curto; longo no 1º efeito da camada).
private struct EffectParamGuide: View {
    @ObservedObject private var point = EffectPointStageState.shared
    @State private var alpha: Double = 0

    var body: some View {
        let text = AureaText.t("om_fx_swipe_to_adjust")
        VStack(spacing: 2) {
            CupertinoGlyph.text(CupertinoGlyph.ArrowLeftRight, size: 22, color: AureaColors.text)
            Text(text)
                .font(.aurea(size: 10))
                .foregroundStyle(AureaColors.text)
                .multilineTextAlignment(.center)
            Text(point.guideLabel)
                .font(.aurea(size: 10, weight: .bold))
                .foregroundStyle(AureaColors.accent)
                .lineLimit(1)
        }
        .frame(width: 100)
        .opacity(point.guideLabel.isEmpty ? 0 : alpha)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(text + " · " + point.guideLabel)
        .accessibilityHidden(alpha <= 0 || point.guideLabel.isEmpty)
        .accessibilityIdentifier("stage.effect.guide")
        .task(id: point.guideToken) { await run() }
    }

    private func run() async {
        let token = point.guideToken
        guard token > 0, token != point.guideShown else { return }
        point.guideShown = token
        let timing = point.guide
        alpha = 0
        withAnimation(.linear(duration: Double(timing.fadeIn) / 1000)) { alpha = 1 }
        try? await Task.sleep(nanoseconds: UInt64(timing.fadeIn + timing.hold) * 1_000_000)
        guard !Task.isCancelled else { return }
        withAnimation(.linear(duration: Double(timing.fadeOut) / 1000)) { alpha = 0 }
    }
}
