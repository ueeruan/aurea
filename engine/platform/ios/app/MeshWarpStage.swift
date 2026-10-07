import SwiftUI

// =============================================================================
// MALHA DE DEFORMAÇÃO no palco — port de editor/MeshWarpStage.kt (Android).
//
// Com o cartão do efeito aurea.distort.mesh_warp aberto, a grade deformada e
// os vértices aparecem sobre a camada. Arrastar um vértice o move (as alças
// vão junto); com um vértice escolhido, as 4 alças de bezier dele aparecem e
// também arrastam. Cada arrasto = UM passo de desfazer; com o auto-key do palco
// ligado (ou a malha já animada) o motor grava o key da malha no cabeçote.
//
// A malha mora no motor (Engine::query_mesh_warp e família), normalizada à
// caixa da camada; aqui só dedo e desenho, pelos cantos da camada.
// =============================================================================
@MainActor final class MeshWarpStageState: ObservableObject {
    static let shared = MeshWarpStageState()
    static let header = 4
    static let floats = 10

    @Published var layer: Int64 = 0
    @Published var effect: Int32 = -1
    /// Vértice escolhido (alças à mostra); −1 = nenhum.
    @Published var selected = -1
    @Published var revision = 0

    func open(_ layerId: Int64, _ effectId: Int32) {
        if layer != layerId || effect != effectId { selected = -1 }
        layer = layerId
        effect = effectId
    }
    func close(_ effectId: Int32) {
        guard effect == effectId else { return }
        effect = -1
        selected = -1
    }
    func active(_ model: AureaModel) -> Bool {
        guard effect >= 0, model.selection.count == 1, model.primarySelection == layer,
              let l = model.selectedLayer else { return false }
        return !l.locked
    }
    func mesh(_ model: AureaModel) -> [Float] {
        model.engine.meshWarp(layer, effect: effect).map(\.floatValue)
    }
}

private let meshLineColor = Color(hex: 0xFFFFD166)

/// (u, v) normalizado → composição, pela afim dos cantos (TL, TR, BR, BL).
private func meshToComp(_ c: [Float], _ u: Float, _ v: Float) -> CGPoint {
    CGPoint(x: CGFloat(c[0] + u * (c[2] - c[0]) + v * (c[6] - c[0])),
            y: CGFloat(c[1] + u * (c[3] - c[1]) + v * (c[7] - c[1])))
}

/// Composição → (u, v); nil se a camada não tem área.
private func meshToUv(_ c: [Float], _ x: Float, _ y: Float) -> (Float, Float)? {
    let ax = c[2] - c[0], ay = c[3] - c[1], bx = c[6] - c[0], by = c[7] - c[1]
    let det = ax * by - ay * bx
    guard abs(det) > 1e-6 else { return nil }
    let px = x - c[0], py = y - c[1]
    return ((px * by - py * bx) / det, (ax * py - ay * px) / det)
}

/// Grade + vértices + alças. Cobre o preview só com o cartão da malha aberto.
@MainActor struct MeshWarpStageOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var mesh = MeshWarpStageState.shared
    @ObservedObject private var viewZoom = StageViewZoom.shared
    @State private var started = false
    @State private var hit: (Int, Int)? = nil
    @State private var moved = false
    @State private var sent = false

    var body: some View {
        if mesh.active(model) {
            GeometryReader { geo in
                let placed = StageZoomMath.fit(size: geo.size,
                                               composition: CGSize(width: CGFloat(model.compositionWidth), height: CGFloat(model.compositionHeight)),
                                               zoom: viewZoom.zoom, pan: viewZoom.pan)
                Canvas { context, _ in
                    _ = mesh.revision
                    _ = model.status.playhead
                    draw(&context, mesh.mesh(model), placed.scale, placed.origin)
                }
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { v in changed(v, placed.scale, placed.origin) }
                    .onEnded { v in ended(v, placed.scale, placed.origin) })
                .accessibilityElement()
                .accessibilityLabel(AureaText.t("fx_name_mesh_warp"))
                .accessibilityHint(AureaText.t("mesh_warp_hint"))
                .accessibilityIdentifier("stage.mesh_warp")
            }
        }
    }

    private func corners() -> [Float]? {
        var c: [Float] = []
        return StageGeom.corners(model.detail, &c) && c.count == 8 ? c : nil
    }

    private func point(_ m: [Float], _ c: [Float], _ vertex: Int, _ grip: Int, _ fit: CGFloat, _ origin: CGPoint) -> CGPoint {
        let b = MeshWarpStageState.header + vertex * MeshWarpStageState.floats
        var u = m[b + grip * 2], v = m[b + grip * 2 + 1]
        if grip > 0 { u += m[b]; v += m[b + 1] }
        let q = meshToComp(c, u, v)
        return CGPoint(x: origin.x + q.x * fit, y: origin.y + q.y * fit)
    }

    private func draw(_ context: inout GraphicsContext, _ m: [Float], _ fit: CGFloat, _ origin: CGPoint) {
        guard m.count >= MeshWarpStageState.header, let c = corners() else { return }
        let rows = Int(m[0]), cols = Int(m[1])
        let n = (rows + 1) * (cols + 1)
        guard m.count >= MeshWarpStageState.header + n * MeshWarpStageState.floats else { return }
        // Arestas como curvas cúbicas (vértice → alça → alça do vizinho → vizinho).
        var path = Path()
        for r in 0...rows {
            for col in 0...cols {
                let i = r * (cols + 1) + col
                let a = point(m, c, i, 0, fit, origin)
                if col < cols {
                    let j = i + 1
                    path.move(to: a)
                    path.addCurve(to: point(m, c, j, 0, fit, origin), control1: point(m, c, i, 2, fit, origin),
                                  control2: point(m, c, j, 1, fit, origin))
                }
                if r < rows {
                    let j = i + cols + 1
                    path.move(to: a)
                    path.addCurve(to: point(m, c, j, 0, fit, origin), control1: point(m, c, i, 4, fit, origin),
                                  control2: point(m, c, j, 3, fit, origin))
                }
            }
        }
        context.stroke(path, with: .color(StageInk.outlineUnder), lineWidth: 3.5)
        context.stroke(path, with: .color(meshLineColor), lineWidth: 1.5)
        // Pontos do tamanho da célula NA TELA (6..14 pt de diâmetro): numa camada
        // pequena a grade 8×8 não vira um borrão de bolinhas por cima dela.
        let cellW = hypot(CGFloat(c[2] - c[0]), CGFloat(c[3] - c[1])) * fit / CGFloat(max(cols, 1))
        let cellH = hypot(CGFloat(c[6] - c[0]), CGFloat(c[7] - c[1])) * fit / CGFloat(max(rows, 1))
        let dotR = min(max(min(cellW, cellH) * 0.45, 6), 14) / 2
        for i in 0..<n {
            let o = point(m, c, i, 0, fit, origin)
            let chosen = i == mesh.selected
            let rr = chosen ? dotR + 1.5 : dotR
            dot(&context, o, rr + 1, StageInk.outlineUnder)
            dot(&context, o, rr, chosen ? AureaColors.accent : .white)
        }
        // Alças só do vértice escolhido.
        let s = mesh.selected
        if s >= 0 && s < n {
            let o = point(m, c, s, 0, fit, origin)
            for g in 1...4 {
                let t = point(m, c, s, g, fit, origin)
                var line = Path(); line.move(to: o); line.addLine(to: t)
                context.stroke(line, with: .color(StageInk.outlineUnder), style: StrokeStyle(lineWidth: 3, lineCap: .round))
                context.stroke(line, with: .color(.white), style: StrokeStyle(lineWidth: 1.5, lineCap: .round))
                dot(&context, t, dotR + 1, StageInk.outlineUnder)
                dot(&context, t, dotR, AureaColors.accent)
            }
            if m[2] > 0.5 {   // key da malha no cabeçote
                let k = dotR + 5
                context.stroke(Path(ellipseIn: CGRect(x: o.x - k, y: o.y - k, width: 2 * k, height: 2 * k)),
                               with: .color(meshLineColor), lineWidth: 2)
            }
        }
    }
    private func dot(_ context: inout GraphicsContext, _ c: CGPoint, _ r: CGFloat, _ color: Color) {
        context.fill(Path(ellipseIn: CGRect(x: c.x - r, y: c.y - r, width: 2 * r, height: 2 * r)), with: .color(color))
    }

    /// Alvo sob o dedo: o mais PERTO entre os vértices e as alças do escolhido (vértice ganha empate).
    private func pick(_ m: [Float], _ p: CGPoint, _ fit: CGFloat, _ origin: CGPoint) -> (Int, Int)? {
        guard m.count >= MeshWarpStageState.header, let c = corners() else { return nil }
        let n = (Int(m[0]) + 1) * (Int(m[1]) + 1)
        guard m.count >= MeshWarpStageState.header + n * MeshWarpStageState.floats else { return nil }
        var best: CGFloat = 22
        var found: (Int, Int)? = nil
        func test(_ vertex: Int, _ grip: Int) {
            let q = point(m, c, vertex, grip, fit, origin)
            let d = hypot(p.x - q.x, p.y - q.y)
            if d < best { best = d; found = (vertex, grip) }
        }
        for i in 0..<n { test(i, 0) }
        let s = mesh.selected
        if s >= 0 && s < n { for g in 1...4 { test(s, g) } }
        return found
    }

    private func changed(_ v: DragGesture.Value, _ fit: CGFloat, _ origin: CGPoint) {
        if !started {
            started = true
            moved = false
            sent = false
            hit = pick(mesh.mesh(model), v.startLocation, fit, origin)
        }
        if !moved && hypot(v.translation.width, v.translation.height) < 6 { return }
        moved = true
        guard let h = hit, fit > 0, let c = corners() else { return }
        let x = Float((v.location.x - origin.x) / fit), y = Float((v.location.y - origin.y) / fit)
        guard let uv = meshToUv(c, x, y) else { return }
        if model.status.playing != 0 { model.playPause() }
        if model.engine.meshWarpDrag(mesh.layer, effect: mesh.effect, vertex: Int32(h.0), grip: Int32(h.1), u: uv.0, v: uv.1,
                                     autoKey: model.autoKeyTransforms, continuing: sent) {
            mesh.revision += 1
        }
        sent = true
    }

    private func ended(_ v: DragGesture.Value, _ fit: CGFloat, _ origin: CGPoint) {
        defer { started = false }
        if !started { hit = pick(mesh.mesh(model), v.startLocation, fit, origin); moved = false; sent = false }
        if sent {
            if let h = hit, h.1 == 0 { mesh.selected = h.0 }
            mesh.revision += 1
            model.refreshModel(force: true)
            return
        }
        guard !moved else { return }
        // Toque: vértice = escolhe (de novo = solta); alça = mantém; vazio = solta.
        if let h = hit {
            if h.1 == 0 { mesh.selected = mesh.selected == h.0 ? -1 : h.0 }
        } else {
            mesh.selected = -1
        }
    }
}

/// Ferramentas da malha no cartão do efeito: liga o palco enquanto o cartão
/// está aberto e oferece "Redefinir malha" (um passo de desfazer).
@MainActor struct MeshWarpCardTools: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var mesh = MeshWarpStageState.shared
    let effectId: UInt32

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(AureaText.t("mesh_warp_hint"))
                .font(.system(size: 11.5))
                .foregroundStyle(AureaColors.muted)
                .fixedSize(horizontal: false, vertical: true)
            Button {
                mesh.selected = -1
                if let layer = model.primarySelection,
                   model.engine.meshWarpReset(layer, effect: Int32(truncatingIfNeeded: effectId)) {
                    mesh.revision += 1
                    model.refreshModel(force: true)
                }
            } label: {
                Text(AureaText.t("mesh_warp_reset"))
                    .font(.system(size: 12, weight: .semibold))
                    .foregroundStyle(AureaColors.text)
                    .padding(.horizontal, 12)
                    .frame(minHeight: 36)
                    .background(RoundedRectangle(cornerRadius: 8).fill(AureaColors.chip))
            }
            .buttonStyle(.plain)
            .accessibilityLabel(AureaText.t("mesh_warp_reset"))
            .accessibilityIdentifier("fx.mesh_warp.reset")
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.bottom, 6)
        .onAppear { if let layer = model.primarySelection { mesh.open(layer, Int32(truncatingIfNeeded: effectId)) } }
        .onDisappear { mesh.close(Int32(truncatingIfNeeded: effectId)) }
    }
}
