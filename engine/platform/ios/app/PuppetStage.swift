import SwiftUI

// =============================================================================
// FANTOCHE no palco — port de editor/PuppetStage.kt (Android).
//
// "Editar pinos" no cartão do efeito aurea.distort.puppet liga o modo: a malha
// deformada (clara) e os pinos (pontos amarelos; o escolhido cheio) aparecem
// sobre a camada. Tocar na camada = pino novo; arrastar um pino = move ao vivo
// (auto-key do palco no cabeçote; o arrasto inteiro é UM passo de desfazer);
// segurar = apaga; "Concluir" sai. Mesmo padrão da Malha de deformação
// (MeshWarpStage.swift): o motor guarda e resolve (Engine::query_puppet e
// família) em fração da caixa da camada; aqui só dedo e desenho.
// =============================================================================
@MainActor final class PuppetStageState: ObservableObject {
    static let shared = PuppetStageState()
    static let pinFloats = 4
    static var type: UInt32 { fxEffectTypeId("aurea.distort.puppet") }

    @Published var layer: Int64 = 0
    @Published var effect: Int32 = -1
    @Published var editing = false
    /// Pino escolhido (índice do motor); −1 = nenhum.
    @Published var selected = -1
    @Published var revision = 0
    /// A doca (o antigo Rig) pediu o modo de pinos nesta camada.
    var pendingEdit: Int64 = 0

    func open(_ layerId: Int64, _ effectId: Int32) {
        if layer != layerId || effect != effectId { selected = -1; editing = false }
        layer = layerId
        effect = effectId
        if pendingEdit == layerId { editing = true; pendingEdit = 0 }
    }
    func close(_ effectId: Int32) {
        guard effect == effectId else { return }
        effect = -1
        editing = false
        selected = -1
    }
    func active(_ model: AureaModel) -> Bool {
        guard editing, effect >= 0, model.selection.count == 1, model.primarySelection == layer,
              let l = model.selectedLayer else { return false }
        return !l.locked
    }
}

// Os embrulhos do motor (puppetPins, puppetMovePin, openPuppetTool...) moram
// em AureaModel.swift, como os do resto do palco.

private let pinYellow = Color(red: 1.0, green: 0.84, blue: 0.04)

private func puppetToComp(_ c: [Float], _ u: Float, _ v: Float) -> CGPoint {
    CGPoint(x: CGFloat(c[0] + u * (c[2] - c[0]) + v * (c[6] - c[0])),
            y: CGFloat(c[1] + u * (c[3] - c[1]) + v * (c[7] - c[1])))
}

private func puppetToUv(_ c: [Float], _ x: Float, _ y: Float) -> (Float, Float)? {
    let ax = c[2] - c[0], ay = c[3] - c[1], bx = c[6] - c[0], by = c[7] - c[1]
    let det = ax * by - ay * bx
    guard abs(det) > 1e-6 else { return nil }
    let px = x - c[0], py = y - c[1]
    return ((px * by - py * bx) / det, (ax * py - ay * px) / det)
}

/// Malha + pinos. Cobre o preview só no modo de pinos.
@MainActor struct PuppetStageOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var puppet = PuppetStageState.shared
    @ObservedObject private var viewZoom = StageViewZoom.shared
    @State private var started = false
    @State private var hit = -1
    @State private var moved = false
    @State private var sent = false
    @State private var removed = false
    @State private var hold: Task<Void, Never>? = nil

    var body: some View {
        if puppet.active(model) {
            GeometryReader { geo in
                let placed = StageZoomMath.fit(size: geo.size,
                                               composition: CGSize(width: CGFloat(model.compositionWidth), height: CGFloat(model.compositionHeight)),
                                               zoom: viewZoom.zoom, pan: viewZoom.pan)
                let pins = model.puppetPins(puppet.layer, effect: puppet.effect)
                Canvas { context, _ in
                    _ = puppet.revision
                    _ = model.status.playhead
                    draw(&context, pins, placed.scale, placed.origin)
                }
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { v in changed(v, placed.scale, placed.origin) }
                    .onEnded { v in ended(v, placed.scale, placed.origin) })
                .accessibilityElement(children: .contain)
                .accessibilityLabel(AureaText.t("fx_name_puppet"))
                .accessibilityHint(AureaText.t("puppet_hint"))
                .accessibilityIdentifier("stage.puppet")
                .overlay {
                    // Cada pino é um elemento de acessibilidade (VoiceOver: escolher / apagar).
                    ForEach(Array(stride(from: 0, to: pins.count - PuppetStageState.pinFloats + 1, by: PuppetStageState.pinFloats)), id: \.self) { k in
                        let index = Int(pins[k])
                        let p = screen(pins[k + 1], pins[k + 2], placed.scale, placed.origin)
                        Color.clear.frame(width: 28, height: 28).position(p)
                            .accessibilityElement()
                            .accessibilityLabel(AureaText.t("puppet_pin_a11y", index + 1))
                            .accessibilityAddTraits(index == puppet.selected ? [.isButton, .isSelected] : .isButton)
                            .accessibilityAction { puppet.selected = index }
                            .accessibilityAction(named: Text(AureaText.t("common_delete"))) {
                                model.puppetRemovePin(puppet.layer, effect: puppet.effect, pin: Int32(index))
                                puppet.revision += 1
                            }
                            .allowsHitTesting(false)
                    }
                }
            }
        }
    }

    private func corners() -> [Float]? {
        var c: [Float] = []
        return StageGeom.corners(model.detail, &c) && c.count == 8 ? c : nil
    }

    private func screen(_ u: Float, _ v: Float, _ fit: CGFloat, _ origin: CGPoint) -> CGPoint {
        guard let c = corners() else { return .zero }
        let q = puppetToComp(c, u, v)
        return CGPoint(x: origin.x + q.x * fit, y: origin.y + q.y * fit)
    }

    private func draw(_ context: inout GraphicsContext, _ pins: [Float], _ fit: CGFloat, _ origin: CGPoint) {
        guard corners() != nil else { return }
        // Malha deformada: triângulos em traço fino e claro.
        let mesh = model.puppetMesh(puppet.layer, effect: puppet.effect)
        if mesh.count >= 6 {
            var path = Path()
            var i = 0
            while i + 5 < mesh.count {
                path.move(to: screen(mesh[i], mesh[i + 1], fit, origin))
                path.addLine(to: screen(mesh[i + 2], mesh[i + 3], fit, origin))
                path.addLine(to: screen(mesh[i + 4], mesh[i + 5], fit, origin))
                path.closeSubpath()
                i += 6
            }
            context.stroke(path, with: .color(.white.opacity(0.55)), lineWidth: 1)
        }
        var k = 0
        while k + PuppetStageState.pinFloats <= pins.count {
            let o = screen(pins[k + 1], pins[k + 2], fit, origin)
            let chosen = Int(pins[k]) == puppet.selected
            let r: CGFloat = 7
            context.fill(Path(ellipseIn: CGRect(x: o.x - r - 2, y: o.y - r - 2, width: 2 * r + 4, height: 2 * r + 4)), with: .color(StageInk.outlineUnder))
            if chosen {
                context.fill(Path(ellipseIn: CGRect(x: o.x - r, y: o.y - r, width: 2 * r, height: 2 * r)), with: .color(pinYellow))
            } else {
                context.stroke(Path(ellipseIn: CGRect(x: o.x - r + 1, y: o.y - r + 1, width: 2 * r - 2, height: 2 * r - 2)), with: .color(pinYellow), lineWidth: 2.5)
            }
            if pins[k + 3] > 0.5 {   // key do pino no cabeçote
                context.stroke(Path(ellipseIn: CGRect(x: o.x - r - 5, y: o.y - r - 5, width: 2 * r + 10, height: 2 * r + 10)), with: .color(pinYellow), lineWidth: 1.5)
            }
            k += PuppetStageState.pinFloats
        }
    }

    /// Pino sob o dedo (índice do motor) ou −1.
    private func pick(_ p: CGPoint, _ fit: CGFloat, _ origin: CGPoint) -> Int {
        let pins = model.puppetPins(puppet.layer, effect: puppet.effect)
        var best: CGFloat = 24
        var found = -1
        var k = 0
        while k + PuppetStageState.pinFloats <= pins.count {
            let q = screen(pins[k + 1], pins[k + 2], fit, origin)
            let d = hypot(p.x - q.x, p.y - q.y)
            if d < best { best = d; found = Int(pins[k]) }
            k += PuppetStageState.pinFloats
        }
        return found
    }

    private func uv(_ p: CGPoint, _ fit: CGFloat, _ origin: CGPoint) -> (Float, Float)? {
        guard fit > 0, let c = corners() else { return nil }
        return puppetToUv(c, Float((p.x - origin.x) / fit), Float((p.y - origin.y) / fit))
    }

    private func changed(_ v: DragGesture.Value, _ fit: CGFloat, _ origin: CGPoint) {
        if !started {
            started = true
            moved = false
            sent = false
            removed = false
            hit = pick(v.startLocation, fit, origin)
            hold?.cancel()
            if hit >= 0 {
                // Segurar parado sobre o pino (0,5 s) apaga.
                let pin = hit
                hold = Task { @MainActor in
                    try? await Task.sleep(nanoseconds: 500_000_000)
                    guard !Task.isCancelled, started, !moved else { return }
                    removed = true
                    model.puppetRemovePin(puppet.layer, effect: puppet.effect, pin: Int32(pin))
                    if puppet.selected == pin { puppet.selected = -1 }
                    puppet.revision += 1
                }
            }
        }
        if removed { return }
        if !moved && hypot(v.translation.width, v.translation.height) < 6 { return }
        moved = true
        hold?.cancel()
        guard hit >= 0, let q = uv(v.location, fit, origin) else { return }
        if model.puppetMovePin(puppet.layer, effect: puppet.effect, pin: Int32(hit), u: q.0, v: q.1, continuing: sent) {
            puppet.revision += 1
        }
        sent = true
    }

    private func ended(_ v: DragGesture.Value, _ fit: CGFloat, _ origin: CGPoint) {
        defer { started = false; hold?.cancel(); hold = nil }
        if removed { return }
        if !started { hit = pick(v.startLocation, fit, origin); moved = false; sent = false }
        if sent {
            puppet.selected = hit
            puppet.revision += 1
            model.refreshModel(force: true)
            return
        }
        guard !moved else { return }
        if hit >= 0 { puppet.selected = hit; return }
        // Toque fora de pino, em cima da camada: pino novo.
        guard let q = uv(v.startLocation, fit, origin) else { return }
        guard q.0 >= -0.02, q.0 <= 1.02, q.1 >= -0.02, q.1 <= 1.02 else { puppet.selected = -1; return }
        let pin = model.puppetAddPin(puppet.layer, effect: puppet.effect, u: q.0, v: q.1)
        if pin >= 0 { puppet.selected = Int(pin); puppet.revision += 1 }
    }
}

/// Ferramentas do Fantoche no cartão do efeito: "Editar pinos" liga o modo de
/// pinos no palco; "Concluir" sai. Fechar o cartão também sai.
@MainActor struct PuppetCardTools: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var puppet = PuppetStageState.shared
    let effectId: UInt32
    private var id32: Int32 { Int32(truncatingIfNeeded: effectId) }

    var body: some View {
        let editing = puppet.editing && puppet.effect == id32 && puppet.layer == model.primarySelection
        let label = AureaText.t(editing ? "puppet_done" : "puppet_edit_pins")
        VStack(alignment: .leading, spacing: 6) {
            Text(AureaText.t("puppet_hint"))
                .font(.system(size: 11.5))
                .foregroundStyle(AureaColors.muted)
                .fixedSize(horizontal: false, vertical: true)
            Button {
                guard let layer = model.primarySelection else { return }
                if puppet.effect != id32 || puppet.layer != layer { puppet.open(layer, id32) }
                puppet.editing = !editing
                if !puppet.editing { puppet.selected = -1 }
            } label: {
                Text(label)
                    .font(.system(size: 12, weight: .semibold))
                    .foregroundStyle(editing ? Color.black : AureaColors.text)
                    .padding(.horizontal, 12)
                    .frame(minHeight: 36)
                    .background(RoundedRectangle(cornerRadius: 8).fill(editing ? AureaColors.accent : AureaColors.chip))
            }
            .buttonStyle(.plain)
            .accessibilityLabel(label)
            .accessibilityIdentifier("fx.puppet.edit")
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.bottom, 6)
        .onAppear { if let layer = model.primarySelection { puppet.open(layer, id32) } }
        .onDisappear { puppet.close(id32) }
    }
}
