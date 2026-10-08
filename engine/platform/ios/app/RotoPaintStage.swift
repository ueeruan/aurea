// Roto Brush do Rotobrush IA (referência: Roto Brush do After Effects).
// "Pintar recorte" no cartão liga o modo: o palco recebe traços de Objeto
// (verde) ou Fundo (vermelho); o motor (EngineRoto.cpp) leva os pontos da
// composição para a camada, guarda no efeito e refaz os recortes.
import SwiftUI

@MainActor final class RotoPaintState: ObservableObject {
    static let shared = RotoPaintState()

    @Published var layer: Int64 = 0
    @Published var effect: UInt32 = 0
    @Published var active = false
    var previousView: UInt32 = 0
    @Published var background = false
    /// Raio do pincel em px da composição.
    @Published var radius: CGFloat = 24
    /// Traço em curso, em px da composição (desenho ao vivo).
    @Published var live: [CGPoint] = []
    @Published var status: [Int64] = [0, 0, 0, 0, 0, 0]

    func painting(_ model: AureaModel) -> Bool { active && model.primarySelection == layer }

    func end(_ model: AureaModel) {
        guard active else { return }
        _ = model.engine.rotoSetView(previousView, effect: effect, layer: layer)
        active = false
        live = []
        model.refreshModel(force: true)
    }

    func refresh(_ model: AureaModel) {
        status = model.engine.rotoStatus(effect: effect, layer: layer).map { $0.int64Value }
        if status.count < 6 { status = [0, 0, 0, 0, 0, 0] }
    }
}

@MainActor struct RotoPaintStageOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var roto = RotoPaintState.shared
    @ObservedObject private var viewZoom = StageViewZoom.shared

    var body: some View {
        if roto.painting(model) {
            GeometryReader { geo in
                let placed = StageZoomMath.fit(size: geo.size,
                                               composition: CGSize(width: CGFloat(model.compositionWidth), height: CGFloat(model.compositionHeight)),
                                               zoom: viewZoom.zoom, pan: viewZoom.pan)
                Canvas { context, _ in
                    let color = roto.background ? Color(red: 0.9, green: 0.28, blue: 0.3).opacity(0.8)
                                                : Color(red: 0.19, green: 0.77, blue: 0.42).opacity(0.8)
                    let pts = roto.live.map { CGPoint(x: placed.origin.x + $0.x * placed.scale, y: placed.origin.y + $0.y * placed.scale) }
                    let r = roto.radius * placed.scale
                    if pts.count > 1 {
                        var path = Path()
                        path.addLines(pts)
                        context.stroke(path, with: .color(color), style: StrokeStyle(lineWidth: r * 2, lineCap: .round, lineJoin: .round))
                    }
                    if let last = pts.last {
                        context.fill(Path(ellipseIn: CGRect(x: last.x - r, y: last.y - r, width: r * 2, height: r * 2)), with: .color(color))
                    }
                }
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { v in
                        let p = CGPoint(x: (v.location.x - placed.origin.x) / max(placed.scale, 0.0001),
                                        y: (v.location.y - placed.origin.y) / max(placed.scale, 0.0001))
                        roto.live.append(p)
                    }
                    .onEnded { _ in finish() })
                .accessibilityElement()
                .accessibilityLabel(AureaText.t("roto_paint"))
                .accessibilityHint(AureaText.t("roto_hint"))
                .accessibilityIdentifier("stage.roto_paint")
            }
        }
    }

    /// Soltar manda o traço inteiro ao motor (um passo de desfazer).
    private func finish() {
        let pts = roto.live
        roto.live = []
        guard !pts.isEmpty else { return }
        var xy: [NSNumber] = []
        xy.reserveCapacity(pts.count * 2)
        for p in pts { xy.append(NSNumber(value: Float(p.x))); xy.append(NSNumber(value: Float(p.y))) }
        if model.engine.rotoAddStroke(xy, background: roto.background, radius: Float(roto.radius), effect: roto.effect, forLayer: roto.layer) {
            model.refreshModel(force: true)
            roto.refresh(model)
        }
    }
}

/// Dentro do cartão do Rotobrush: o botão "Pintar recorte" e, no modo, os controles.
@MainActor struct RotoPaintCardTools: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var roto = RotoPaintState.shared
    let effectId: UInt32
    let previousView: UInt32
    private let timer = Timer.publish(every: 0.3, on: .main, in: .common).autoconnect()

    private func chip(_ key: String, _ id: String, _ on: Bool, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(AureaText.t(key))
                .font(.system(size: 12, weight: .semibold))
                .foregroundStyle(AureaColors.text)
                .padding(.horizontal, 12)
                .frame(minHeight: 36)
                .background(RoundedRectangle(cornerRadius: 8).fill(on ? AureaColors.text.opacity(0.18) : AureaColors.chip))
        }
        .buttonStyle(.plain)
        .accessibilityLabel(AureaText.t(key))
        .accessibilityAddTraits(on ? .isSelected : [])
        .accessibilityIdentifier(id)
    }

    var body: some View {
        let layer = model.primarySelection ?? 0
        let mine = roto.active && roto.layer == layer && roto.effect == effectId
        VStack(alignment: .leading, spacing: 6) {
            if !mine {
                chip("roto_paint", "fx.roto.paint", false) {
                    // Outra sessão aberta: devolve a visualização dela antes.
                    roto.end(model)
                    model.pause()
                    roto.background = false
                    roto.layer = layer
                    roto.effect = effectId
                    roto.previousView = previousView
                    roto.active = true
                    _ = model.engine.rotoSetView(2, effect: effectId, layer: layer)
                    roto.refresh(model)
                    model.refreshModel(force: true)
                }
            } else {
                Text(AureaText.t("roto_hint"))
                    .font(.system(size: 11.5))
                    .foregroundStyle(AureaColors.muted)
                    .fixedSize(horizontal: false, vertical: true)
                HStack(spacing: 6) {
                    chip("roto_object", "fx.roto.object", !roto.background) { roto.background = false }
                    chip("roto_background", "fx.roto.background", roto.background) { roto.background = true }
                    chip("roto_undo", "fx.roto.undo", false) {
                        if model.engine.rotoUndoStroke(effect: effectId, layer: layer) {
                            model.refreshModel(force: true)
                            roto.refresh(model)
                        }
                    }
                }
                Text(AureaText.t("roto_brush_size"))
                    .font(.system(size: 11.5))
                    .foregroundStyle(AureaColors.muted)
                Slider(value: $roto.radius, in: 4...160)
                    .accessibilityLabel(AureaText.t("roto_brush_size"))
                    .accessibilityIdentifier("fx.roto.size")
                HStack(spacing: 6) {
                    chip("roto_propagate", "fx.roto.propagate", roto.status[2] != 0) {
                        if roto.status[2] != 0 { model.engine.rotoCancel() }
                        else { _ = model.engine.rotoPropagate(effect: effectId, layer: layer) }
                        roto.refresh(model)
                    }
                    chip("roto_done", "fx.roto.done", false) {
                        roto.end(model)
                    }
                }
                if roto.status[2] != 0 && roto.status[1] > 0 {
                    Text(String(format: AureaText.t("roto_progress"), Int(roto.status[0]), Int(roto.status[1])))
                        .font(.system(size: 11.5))
                        .foregroundStyle(AureaColors.muted)
                        .accessibilityIdentifier("fx.roto.progress")
                }
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.bottom, 6)
        .onReceive(timer) { _ in if mine { roto.refresh(model) } }
        // Fechar o painel encerra o modo pintar (Stage Android: DisposableEffect):
        // o palco deixa de comer toques e a sobreposição não fica no preview.
        .onDisappear {
            if roto.active && roto.layer == layer && roto.effect == effectId { roto.end(model) }
        }
    }
}
