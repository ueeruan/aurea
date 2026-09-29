import SwiftUI

// =============================================================================
// RIG 2D no palco — port de editor/RigStage.kt (Android).
//
//  - MONTAR: tocar no vazio cria uma junta ligada à escolhida (a nova fica
//    escolhida: toques seguidos fazem a corrente); tocar numa junta a escolhe
//    (de novo = solta); arrastar move. A prévia mostra o desenho parado.
//  - ANIMAR: arrastar uma junta posa no cabeçote e grava keyframe (ponta da
//    corrente = IK de 2 ossos; junta do meio = gira o osso dela).
//
// Cada arrasto = UM passo de desfazer. Malha, pesos e pose são do motor
// (Engine::query_rig e família); aqui só dedo e desenho.
// =============================================================================
@MainActor final class RigStageState: ObservableObject {
    static let shared = RigStageState()
    enum Mode { case off, setup, animate }
    static let floats = 5

    @Published var mode: Mode = .off
    @Published var layer: Int64 = 0
    /// Id da junta escolhida (−1 = nenhuma).
    @Published var selected: Int32 = -1
    @Published var grabbed: Int32 = -1
    @Published var revision = 0

    /// Abre no Animar se já existe osso. Sem esqueleto, o automático já entra
    /// em cima do desenho (ninguém precisa montar junta por junta) e abre no
    /// Montar para ajustar as juntas ao corpo.
    func open(_ model: AureaModel) {
        guard let id = model.primarySelection else { return }
        layer = id
        selected = -1
        if model.engine.rigJoints(id, bind: true).count >= 2 * Self.floats { setMode(model, .animate); return }
        autoSkeleton(model)
    }
    /// Esqueleto automático (troca o rig que houver) e abre no Montar.
    func autoSkeleton(_ model: AureaModel) {
        selected = -1
        _ = model.engine.rigAutoHumanoid(layer)
        model.refreshModel(force: true)
        setMode(model, .setup)
    }
    func setMode(_ model: AureaModel, _ m: Mode) {
        mode = m
        model.engine.setRigSetupLayer(m == .setup ? layer : 0)
        revision += 1
    }
    func close(_ model: AureaModel) {
        guard mode != .off else { return }
        mode = .off
        selected = -1
        model.engine.setRigSetupLayer(0)
    }
    /// O palco está no rig da camada escolhida (imagem, destravada)?
    func active(_ model: AureaModel) -> Bool {
        guard mode != .off, model.selection.count == 1, model.primarySelection == layer,
              let l = model.selectedLayer else { return false }
        return l.kind == 2 && !l.locked
    }
    func joints(_ model: AureaModel) -> [Float] {
        model.engine.rigJoints(layer, bind: mode == .setup).map(\.floatValue)
    }
    static func index(_ j: [Float], _ id: Int32) -> Int? {
        stride(from: 0, to: j.count - floats + 1, by: floats).first { Int32(j[$0]) == id }.map { $0 / floats }
    }
}

private let rigBoneColor = Color(hex: 0xFFFFD166)

/// Esqueleto desenhado + dedo. Cobre o preview só com o rig aberto.
@MainActor struct RigStageOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var rig = RigStageState.shared
    @ObservedObject private var viewZoom = StageViewZoom.shared
    @State private var started = false
    @State private var hit: Int32 = -1
    @State private var moved = false
    @State private var sent = false
    @State private var downJoints: [Float] = []

    var body: some View {
        if rig.active(model) {
            GeometryReader { geo in
                let placed = StageZoomMath.fit(size: geo.size,
                                               composition: CGSize(width: CGFloat(model.compositionWidth), height: CGFloat(model.compositionHeight)),
                                               zoom: viewZoom.zoom, pan: viewZoom.pan)
                Canvas { context, _ in
                    _ = rig.revision
                    _ = model.status.playhead
                    draw(&context, rig.joints(model), placed.scale, placed.origin)
                }
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { v in changed(v, placed.scale, placed.origin) }
                    .onEnded { v in ended(v, placed.scale, placed.origin) })
            }
        }
    }

    private func screen(_ j: [Float], _ i: Int, _ fit: CGFloat, _ origin: CGPoint) -> CGPoint {
        CGPoint(x: origin.x + CGFloat(j[i * RigStageState.floats + 2]) * fit, y: origin.y + CGFloat(j[i * RigStageState.floats + 3]) * fit)
    }

    private func draw(_ context: inout GraphicsContext, _ j: [Float], _ fit: CGFloat, _ origin: CGPoint) {
        let n = j.count / RigStageState.floats
        for i in 0..<n {
            guard let p = RigStageState.index(j, Int32(j[i * RigStageState.floats + 1])) else { continue }
            var bone = Path(); bone.move(to: screen(j, p, fit, origin)); bone.addLine(to: screen(j, i, fit, origin))
            context.stroke(bone, with: .color(StageInk.outlineUnder), style: StrokeStyle(lineWidth: 7, lineCap: .round))
            context.stroke(bone, with: .color(rigBoneColor), style: StrokeStyle(lineWidth: 3.5, lineCap: .round))
        }
        for i in 0..<n {
            let id = Int32(j[i * RigStageState.floats])
            let c = screen(j, i, fit, origin)
            let chosen = id == rig.selected || id == rig.grabbed
            let r: CGFloat = chosen ? 9 : 7
            dot(&context, c, r + 1.5, StageInk.outlineUnder)
            dot(&context, c, r, chosen ? AureaColors.accent : .white)
            if j[i * RigStageState.floats + 1] < 0 { dot(&context, c, r * 0.4, StageInk.outlineUnder) }   // raiz
            if rig.mode == .animate && j[i * RigStageState.floats + 4] > 0.5 {   // keyframe no cabeçote
                context.stroke(Path(ellipseIn: CGRect(x: c.x - r - 4, y: c.y - r - 4, width: 2 * (r + 4), height: 2 * (r + 4))),
                               with: .color(rigBoneColor), lineWidth: 2)
            }
        }
    }
    private func dot(_ context: inout GraphicsContext, _ c: CGPoint, _ r: CGFloat, _ color: Color) {
        context.fill(Path(ellipseIn: CGRect(x: c.x - r, y: c.y - r, width: 2 * r, height: 2 * r)), with: .color(color))
    }

    private func pick(_ j: [Float], _ p: CGPoint, _ fit: CGFloat, _ origin: CGPoint) -> Int32 {
        var best: CGFloat = 22   // com 28 o toque para a próxima junta caía na anterior
        var found: Int32 = -1
        for i in 0..<(j.count / RigStageState.floats) {
            let c = screen(j, i, fit, origin)
            let d = hypot(p.x - c.x, p.y - c.y)
            if d < best { best = d; found = Int32(j[i * RigStageState.floats]) }
        }
        return found
    }

    private func changed(_ v: DragGesture.Value, _ fit: CGFloat, _ origin: CGPoint) {
        if !started {
            started = true
            moved = false
            sent = false
            downJoints = rig.joints(model)
            hit = pick(downJoints, v.startLocation, fit, origin)
            rig.grabbed = hit
        }
        if !moved && hypot(v.translation.width, v.translation.height) < 8 { return }
        moved = true
        guard hit >= 0, fit > 0 else { return }
        let x = Float((v.location.x - origin.x) / fit), y = Float((v.location.y - origin.y) / fit)
        if rig.mode == .setup {
            if model.engine.rigMoveJoint(rig.layer, joint: hit, x: x, y: y, continuing: sent) { rig.revision += 1 }
        } else {
            if model.status.playing != 0 { model.playPause() }
            if model.engine.rigPoseJoint(rig.layer, joint: hit, x: x, y: y, continuing: sent) { rig.revision += 1 }
        }
        sent = true
    }

    private func ended(_ v: DragGesture.Value, _ fit: CGFloat, _ origin: CGPoint) {
        defer { started = false; rig.grabbed = -1 }
        if !started { downJoints = rig.joints(model); hit = pick(downJoints, v.startLocation, fit, origin); moved = false; sent = false }
        if sent {
            rig.selected = hit
            rig.revision += 1
            model.refreshModel(force: true)
            return
        }
        guard !moved, fit > 0 else { return }
        // Toque: junta = escolhe (de novo = solta); vazio no Montar = junta nova.
        if hit >= 0 { rig.selected = rig.selected == hit ? -1 : hit; return }
        if rig.mode == .setup {
            let parent: Int32 = rig.selected >= 0 && RigStageState.index(downJoints, rig.selected) != nil ? rig.selected : -1
            let x = Float((v.startLocation.x - origin.x) / fit), y = Float((v.startLocation.y - origin.y) / fit)
            let id = model.engine.rigAddJoint(rig.layer, parent: parent, x: x, y: y)
            if id >= 0 { rig.selected = id }
            rig.revision += 1
            model.refreshModel(force: true)
        } else {
            rig.selected = -1
        }
    }
}

/// A barrinha do rig: Montar · Animar · Apagar junta · Pronto, e a dica do modo.
@MainActor struct RigModeBar: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var rig = RigStageState.shared

    var body: some View {
        let active = rig.active(model)
        Group {
            if active {
                VStack(spacing: 4) {
                    HStack(spacing: 2) {
                        chip(AureaText.t("rig_setup"), chosen: rig.mode == .setup) { rig.setMode(model, .setup) }
                        chip(AureaText.t("rig_animate"), chosen: rig.mode == .animate) { rig.setMode(model, .animate) }
                        chip(AureaText.t("rig_auto")) { rig.autoSkeleton(model) }
                        chip(AureaText.t("rig_delete_joint"), enabled: rig.selected >= 0) {
                            if model.engine.rigRemoveJoint(rig.layer, joint: rig.selected) { model.refreshModel(force: true) }
                            rig.selected = -1
                            rig.revision += 1
                        }
                        chip(AureaText.t("rig_done"), accent: true) { rig.close(model) }
                    }
                    .padding(.horizontal, 4).padding(.vertical, 3)
                    .background(AureaColors.editorPanelHigh, in: RoundedRectangle(cornerRadius: 10))
                    .overlay(RoundedRectangle(cornerRadius: 10).stroke(StageInk.accentHalf, lineWidth: 1))
                    Text(AureaText.t(rig.mode == .setup ? "rig_hint_setup" : "rig_hint_animate"))
                        .font(.aurea(size: 11, weight: .semibold)).foregroundStyle(.white).lineLimit(1)
                        .padding(.horizontal, 8).padding(.vertical, 2)
                        .background(StageInk.outlineUnder, in: RoundedRectangle(cornerRadius: 6))
                }
            }
        }
        // Trocou de camada, travou ou apagou: o modo fecha (a prévia volta a deformar).
        .onChange(of: active) { now in if !now { rig.close(model) } }
    }

    private func chip(_ label: String, chosen: Bool = false, accent: Bool = false, enabled: Bool = true,
                      action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(label).font(.aurea(size: 12, weight: .bold)).lineLimit(1)
                .foregroundStyle(accent ? AureaColors.onAccent : AureaColors.text)
                .padding(.horizontal, 10).padding(.vertical, 5)
                .background(accent ? AureaColors.accent : (chosen ? StageInk.accentHalf : Color.clear), in: Capsule())
                .frame(minHeight: 36)
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .disabled(!enabled).opacity(enabled ? 1 : 0.4)
    }
}
