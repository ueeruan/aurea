import SwiftUI

// =============================================================================
// RIG 2D DE ROSTO no palco — port de editor/FaceRigStage.kt (Android).
//
// Os controles (virar, acenar, olhos, boca...) são parâmetros comuns do cartão
// do efeito aurea.rig.face, com keyframe. "Ajustar pontos" liga as alças sobre
// a camada: centro e canto do rosto, de cada olho e da boca, o queixo e o topo
// do cabelo. Arrastar uma alça move o ponto (o gesto inteiro é UM passo de
// desfazer). O motor guarda e resolve (Engine::query_face_rig e família) em
// fração da caixa da camada; aqui só dedo e desenho.
// =============================================================================
@MainActor final class FaceRigStageState: ObservableObject {
    static let shared = FaceRigStageState()
    static let handles = 10
    static var type: UInt32 { fxEffectTypeId("aurea.rig.face") }
    /// Nomes das alças (ordem de face_rig::Handle) — acessibilidade e dica.
    static let handleNames = ["face_rig_h_face", "face_rig_h_face_size", "face_rig_h_eye_left", "face_rig_h_eye_left_size",
                              "face_rig_h_eye_right", "face_rig_h_eye_right_size", "face_rig_h_mouth", "face_rig_h_mouth_size",
                              "face_rig_h_neck", "face_rig_h_hair"]
    /// Alcance do toque numa alça (pt).
    static let reach: CGFloat = 32

    @Published var layer: Int64 = 0
    @Published var effect: Int32 = -1
    @Published var editing = false
    /// Alça escolhida; −1 = nenhuma.
    @Published var selected = -1
    @Published var revision = 0

    func open(_ layerId: Int64, _ effectId: Int32) {
        if layer != layerId || effect != effectId { selected = -1; editing = false }
        layer = layerId
        effect = effectId
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

private func faceToComp(_ c: [Float], _ u: Float, _ v: Float) -> CGPoint {
    CGPoint(x: CGFloat(c[0] + u * (c[2] - c[0]) + v * (c[6] - c[0])),
            y: CGFloat(c[1] + u * (c[3] - c[1]) + v * (c[7] - c[1])))
}

private func faceToUv(_ c: [Float], _ x: Float, _ y: Float) -> (Float, Float)? {
    let ax = c[2] - c[0], ay = c[3] - c[1], bx = c[6] - c[0], by = c[7] - c[1]
    let det = ax * by - ay * bx
    guard abs(det) > 1e-6 else { return nil }
    let px = x - c[0], py = y - c[1]
    return ((px * by - py * bx) / det, (ax * py - ay * px) / det)
}

private func faceHandleInk(_ handle: Int) -> Color {
    switch handle {
    case 0, 1: return .white
    case 2, 3, 4, 5: return Color(red: 1.0, green: 0.84, blue: 0.04)
    case 6, 7: return Color(red: 1.0, green: 0.44, blue: 0.66)
    default: return Color(red: 0.31, green: 0.76, blue: 1.0)
    }
}

/// Elipses e alças. Cobre o preview só no modo "Ajustar pontos".
@MainActor struct FaceRigStageOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var rig = FaceRigStageState.shared
    @ObservedObject private var viewZoom = StageViewZoom.shared
    @State private var started = false
    @State private var hit = -1
    @State private var sent = false
    @State private var grab: (Float, Float) = (0, 0)
    @State private var startUv: (Float, Float) = (0, 0)

    var body: some View {
        if rig.active(model) {
            GeometryReader { geo in
                let placed = StageZoomMath.fit(size: geo.size,
                                               composition: CGSize(width: CGFloat(model.compositionWidth), height: CGFloat(model.compositionHeight)),
                                               zoom: viewZoom.zoom, pan: viewZoom.pan)
                let handles = model.faceRigHandles(rig.layer, effect: rig.effect)
                Canvas { context, _ in
                    _ = rig.revision
                    _ = model.status.playhead
                    draw(&context, handles, placed.scale, placed.origin)
                }
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { v in changed(v, placed.scale, placed.origin) }
                    .onEnded { _ in ended() })
                .accessibilityElement(children: .contain)
                .accessibilityLabel(AureaText.t("fx_name_face_rig"))
                .accessibilityHint(AureaText.t("face_rig_hint"))
                .accessibilityIdentifier("stage.faceRig")
                .overlay { handleElements(handles, placed.scale, placed.origin) }
            }
        }
    }

    /// Cada alça é um elemento de acessibilidade (VoiceOver: escolher).
    @ViewBuilder
    private func handleElements(_ h: [Float], _ fit: CGFloat, _ origin: CGPoint) -> some View {
        if h.count >= FaceRigStageState.handles * 2 {
            ForEach(0..<FaceRigStageState.handles, id: \.self) { i in
                handleElement(h, i, fit, origin)
            }
        }
    }

    private func handleElement(_ h: [Float], _ i: Int, _ fit: CGFloat, _ origin: CGPoint) -> some View {
        let traits: AccessibilityTraits = i == rig.selected ? [.isButton, .isSelected] : .isButton
        return Color.clear.frame(width: 28, height: 28).position(screen(h[i * 2], h[i * 2 + 1], fit, origin))
            .accessibilityElement()
            .accessibilityLabel(AureaText.t("face_rig_handle_a11y", AureaText.t(FaceRigStageState.handleNames[i])))
            .accessibilityAddTraits(traits)
            .accessibilityAction { rig.selected = i }
            .allowsHitTesting(false)
    }

    private func corners() -> [Float]? {
        var c: [Float] = []
        return StageGeom.corners(model.detail, &c) && c.count == 8 ? c : nil
    }

    private func screen(_ u: Float, _ v: Float, _ fit: CGFloat, _ origin: CGPoint) -> CGPoint {
        guard let c = corners() else { return .zero }
        let q = faceToComp(c, u, v)
        return CGPoint(x: origin.x + q.x * fit, y: origin.y + q.y * fit)
    }

    private func draw(_ context: inout GraphicsContext, _ h: [Float], _ fit: CGFloat, _ origin: CGPoint) {
        guard corners() != nil, h.count >= FaceRigStageState.handles * 2 else { return }
        // Elipses do rosto, dos olhos e da boca (centro + raios, em fração da camada).
        for part in [0, 2, 4, 6] {
            let cu = h[part * 2], cv = h[part * 2 + 1]
            let ru = h[(part + 1) * 2] - cu, rv = h[(part + 1) * 2 + 1] - cv
            var path = Path()
            for i in 0...48 {
                let t = Float(i) / 48 * 2 * Float.pi
                let o = screen(cu + ru * cos(t), cv + rv * sin(t), fit, origin)
                if i == 0 { path.move(to: o) } else { path.addLine(to: o) }
            }
            context.stroke(path, with: .color(StageInk.outlineUnder), lineWidth: 3)
            context.stroke(path, with: .color(faceHandleInk(part)), lineWidth: 1.5)
        }
        for i in 0..<FaceRigStageState.handles {
            let o = screen(h[i * 2], h[i * 2 + 1], fit, origin)
            let edge = i == 1 || i == 3 || i == 5 || i == 7
            let r: CGFloat = edge ? 5 : 7
            context.fill(Path(ellipseIn: CGRect(x: o.x - r - 2, y: o.y - r - 2, width: 2 * r + 4, height: 2 * r + 4)),
                         with: .color(StageInk.outlineUnder))
            if i == rig.selected || !edge {
                context.fill(Path(ellipseIn: CGRect(x: o.x - r, y: o.y - r, width: 2 * r, height: 2 * r)), with: .color(faceHandleInk(i)))
            } else {
                context.stroke(Path(ellipseIn: CGRect(x: o.x - r + 1, y: o.y - r + 1, width: 2 * r - 2, height: 2 * r - 2)),
                               with: .color(faceHandleInk(i)), lineWidth: 2)
            }
        }
    }

    private func uv(_ p: CGPoint, _ fit: CGFloat, _ origin: CGPoint) -> (Float, Float)? {
        guard fit > 0, let c = corners() else { return nil }
        return faceToUv(c, Float((p.x - origin.x) / fit), Float((p.y - origin.y) / fit))
    }

    /// Alça sob o dedo (a mais perto), ou −1.
    private func pick(_ p: CGPoint, _ h: [Float], _ fit: CGFloat, _ origin: CGPoint) -> Int {
        var best = -1
        var bestD = FaceRigStageState.reach
        for i in 0..<FaceRigStageState.handles {
            let q = screen(h[i * 2], h[i * 2 + 1], fit, origin)
            let d = hypot(p.x - q.x, p.y - q.y)
            if d < bestD { bestD = d; best = i }
        }
        return best
    }

    private func changed(_ v: DragGesture.Value, _ fit: CGFloat, _ origin: CGPoint) {
        if !started {
            started = true
            sent = false
            let h = model.faceRigHandles(rig.layer, effect: rig.effect)
            hit = h.count >= FaceRigStageState.handles * 2 ? pick(v.startLocation, h, fit, origin) : -1
            if hit >= 0, let s = uv(v.startLocation, fit, origin) {
                grab = (h[hit * 2], h[hit * 2 + 1])
                startUv = s
            } else {
                hit = -1
            }
            rig.selected = hit
            rig.revision += 1
        }
        guard hit >= 0, hypot(v.translation.width, v.translation.height) >= 6 || sent,
              let q = uv(v.location, fit, origin) else { return }
        // A alça anda o quanto o dedo andou (sem pular para debaixo dele).
        if model.faceRigMoveHandle(rig.layer, effect: rig.effect, handle: Int32(hit),
                                   u: grab.0 + q.0 - startUv.0, v: grab.1 + q.1 - startUv.1, continuing: sent) {
            sent = true
            rig.revision += 1
        }
    }

    private func ended() {
        defer { started = false }
        if sent {
            rig.revision += 1
            model.refreshModel(force: true)
        }
    }
}

/// Ferramentas do Rig 2D de rosto no cartão do efeito: o estado dos pontos
/// achados, "Ajustar pontos"/"Concluir" e "Detectar rosto de novo". Fechar o
/// cartão sai do modo de pontos.
@MainActor struct FaceRigCardTools: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var rig = FaceRigStageState.shared
    let effectId: UInt32
    private var id32: Int32 { Int32(truncatingIfNeeded: effectId) }

    var body: some View {
        // Rig 2D v2: no "Controle do rig" (o nulo das partes separadas) os
        // controles movem todas as partes; pontos e separar ficam na imagem.
        if model.selectedLayer?.kind == 6 {
            Text(AureaText.t("face_rig_control_hint"))
                .font(.system(size: 11.5))
                .foregroundStyle(AureaColors.muted)
                .fixedSize(horizontal: false, vertical: true)
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(.bottom, 6)
                .accessibilityIdentifier("fx.faceRig.control")
        } else {
            tools
        }
    }

    @ViewBuilder private var tools: some View {
        let layer = model.primarySelection ?? 0
        let editing = rig.editing && rig.effect == id32 && rig.layer == layer
        let locked = model.selectedLayer?.locked ?? true
        let handles = model.faceRigHandles(layer, effect: id32)
        let placed = handles.count > FaceRigStageState.handles * 2 ? Int(handles[FaceRigStageState.handles * 2]) : -1
        VStack(alignment: .leading, spacing: 6) {
            if let status = statusKey(placed) {
                Text(AureaText.t(status))
                    .font(.system(size: 12))
                    .foregroundStyle(placed == 0 ? AureaColors.text : AureaColors.muted)
                    .fixedSize(horizontal: false, vertical: true)
                    .accessibilityIdentifier("fx.faceRig.status")
            }
            Text(AureaText.t("face_rig_hint"))
                .font(.system(size: 11.5))
                .foregroundStyle(AureaColors.muted)
                .fixedSize(horizontal: false, vertical: true)
            HStack(spacing: 6) {
                chip(AureaText.t(editing ? "puppet_done" : "face_rig_adjust"), on: editing) {
                    guard !locked, let layer = model.primarySelection else { return }
                    if rig.effect != id32 || rig.layer != layer { rig.open(layer, id32) }
                    rig.editing = !editing
                    if !rig.editing { rig.selected = -1 }
                }
                .accessibilityIdentifier("fx.faceRig.adjust")
                chip(AureaText.t("face_rig_detect"), on: false) {
                    guard !locked, let layer = model.primarySelection else { return }
                    model.faceRigAutoPlace(layer, effect: id32)
                    rig.revision += 1
                }
                .accessibilityIdentifier("fx.faceRig.detect")
            }
            // v2: cada parte numa camada, com o que fica atrás preenchido e o nulo
            // "Controle do rig". Portão AureaHyper.
            chip(AureaText.t("face_rig_split"), on: false) {
                guard !locked, !model.importingMedia, let layer = model.primarySelection,
                      model.requireHyperSubscription() else { return }
                rig.editing = false
                model.faceRigSplitParts(layer, effect: id32)
            }
            .accessibilityIdentifier("fx.faceRig.split")
            .accessibilityHint(AureaText.t("face_rig_split_hint"))
            Text(AureaText.t("face_rig_split_hint"))
                .font(.system(size: 11.5))
                .foregroundStyle(AureaColors.muted)
                .fixedSize(horizontal: false, vertical: true)
            if editing, rig.selected >= 0, rig.selected < FaceRigStageState.handleNames.count {
                Text(AureaText.t("face_rig_handle_a11y", AureaText.t(FaceRigStageState.handleNames[rig.selected])))
                    .font(.system(size: 11.5))
                    .foregroundStyle(AureaColors.muted)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.bottom, 6)
        .onAppear { if let layer = model.primarySelection { rig.open(layer, id32) } }
        .onDisappear { rig.close(id32) }
    }

    private func statusKey(_ placed: Int) -> String? {
        switch placed {
        case 2: return "face_rig_found"
        case 1: return "face_rig_found_eyes"
        case 0: return "face_rig_not_found"
        default: return nil
        }
    }

    private func chip(_ label: String, on: Bool, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(label).font(.system(size: 12)).foregroundStyle(on ? AureaColors.accent : AureaColors.text).lineLimit(1)
                .padding(.horizontal, 12).frame(minHeight: 44)
                .background(on ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityLabel(label)
        .accessibilityAddTraits(on ? [.isSelected] : [])
    }
}
