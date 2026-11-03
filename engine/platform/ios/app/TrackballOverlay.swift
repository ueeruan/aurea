import SwiftUI
import UIKit
import simd

/// Trackball do gizmo 3D de girar (ferramenta Girar) — par do TrackballStage.kt.
/// Esfera branca no pivô, anéis vermelho/verde/azul = grandes círculos
/// perpendiculares aos eixos LOCAIS X/Y/Z, anel cinza de fora = giro em volta
/// da vista. Toda a conta (orientação na vista, toque, arrasto e Euler sem
/// salto em ±180°) é do motor (core/Trackball.hpp); aqui só se desenha e se
/// repassa o dedo.
@MainActor enum TrackballOverlay {
    /// Raio da esfera na tela (o anel cinza fica em ×1,25 = trackball::kViewRingScale).
    static let radius: CGFloat = 62
    static let viewRingScale: CGFloat = 1.25
    /// Parte agarrada agora (−1 = nenhuma): o anel em destaque enquanto o dedo arrasta.
    static var activePart = -1

    /// Ferramenta Girar numa camada 3D (sem parte de forma 3D escolhida).
    static func active(_ model: AureaModel) -> Bool {
        guard model.gizmoTool == 1, model.selection.count == 1, let id = model.primarySelection else { return false }
        if Shape3DState.shared.part(model) >= 0 { return false }
        return !model.engine.gizmo(id, length: ShellStageGeometry.gizmoLength).isEmpty
    }

    /// 23 números do motor: origem (2), eixos na vista A (9), frame F (9), Rotação XYZ (3).
    static func data(_ model: AureaModel) -> [Float]? {
        guard active(model), let id = model.primarySelection else { return nil }
        let t = model.engine.trackball(id).map(\.floatValue)
        return t.count == 23 ? t : nil
    }

    /// Desenha o trackball. Falso = não há (a ferramenta usa o desenho antigo).
    static func draw(_ context: inout GraphicsContext, model: AureaModel, screen: (Float, Float) -> CGPoint) -> Bool {
        guard let t = data(model) else { return false }
        let c = screen(t[0], t[1]), r = radius
        let sphere = Path(ellipseIn: CGRect(x: c.x - r, y: c.y - r, width: r * 2, height: r * 2))
        // Sem esfera pintada (o dono pediu só o gizmo): os anéis desenham a bola;
        // o miolo continua sendo a área do giro livre e só aparece, leve, no arrasto.
        if activePart == 4 { context.fill(sphere, with: .color(.white.opacity(0.12))) }
        // Anel cinza de fora: girar em volta da vista.
        let rv = r * viewRingScale
        let outer = Path(ellipseIn: CGRect(x: c.x - rv, y: c.y - rv, width: rv * 2, height: rv * 2))
        context.stroke(outer, with: .color(StageInk.outlineUnder), lineWidth: 5)
        context.stroke(outer, with: .color(activePart == 3 ? .white : Color(red: 0.77, green: 0.77, blue: 0.8)),
                       lineWidth: activePart == 3 ? 3.5 : 2.5)
        // Anéis: a metade de trás bem fraca por baixo, a da frente por cima.
        let colors = [StageInk.gizmoX, StageInk.gizmoY, StageInk.gizmoZ]
        var front = [Path(), Path(), Path()], back = [Path(), Path(), Path()]
        let n = 96
        for ring in 0..<3 {
            let u = 2 + ((ring + 1) % 3) * 3, v = 2 + ((ring + 2) % 3) * 3
            var prev = CGPoint.zero
            var pz: Float = 0
            for i in 0...n {
                let a = Float(i) * 2 * .pi / Float(n)
                let ca = cos(a), sa = sin(a)
                let p = CGPoint(x: c.x + r * CGFloat(ca * t[u] + sa * t[v]), y: c.y + r * CGFloat(ca * t[u + 1] + sa * t[v + 1]))
                let z = ca * t[u + 2] + sa * t[v + 2]
                if i > 0 {
                    if (z + pz) * 0.5 <= 0 { front[ring].move(to: prev); front[ring].addLine(to: p) }
                    else { back[ring].move(to: prev); back[ring].addLine(to: p) }
                }
                prev = p; pz = z
            }
        }
        for ring in 0..<3 { context.stroke(back[ring], with: .color(colors[ring].opacity(0.28)), lineWidth: 1.5) }
        for ring in 0..<3 {
            let hot = activePart == ring
            context.stroke(front[ring], with: .color(StageInk.outlineUnder), style: StrokeStyle(lineWidth: hot ? 7 : 5.5, lineCap: .round))
            context.stroke(front[ring], with: .color(colors[ring]), style: StrokeStyle(lineWidth: hot ? 4.5 : 3, lineCap: .round))
        }
        context.fill(Path(ellipseIn: CGRect(x: c.x - 4, y: c.y - 4, width: 8, height: 8)), with: .color(StageInk.outlineUnder))
        context.fill(Path(ellipseIn: CGRect(x: c.x - 3, y: c.y - 3, width: 6, height: 6)), with: .color(.white))
        return true
    }
}

/// Um arrasto no trackball (Stage.kt trackballGesture): a parte tocada vale o
/// gesto inteiro; o motor devolve a Rotação XYZ a cada passo (absoluta desde o
/// toque) e ela entra pelo caminho do gizmo (chave automática + um desfazer).
@MainActor final class TrackballSession {
    private let id: Int64
    private let center: CGPoint
    private let grab: CGPoint
    private var prev: CGPoint
    private var moved = false
    private var args: [Float]
    private let label: String

    /// Nil = o dedo não pegou o trackball (anéis ~14 pt primeiro, depois o anel cinza, depois a esfera).
    init?(model: AureaModel, point: CGPoint, screen: (Float, Float) -> CGPoint) {
        guard let t = TrackballOverlay.data(model), let id = model.primarySelection else { return nil }
        let c = screen(t[0], t[1])
        let gx = Float(point.x - c.x), gy = Float(point.y - c.y)
        let part = Int(model.engine.trackballHit(t[2..<11].map { NSNumber(value: $0) }, x: gx, y: gy,
                                                 radius: Float(TrackballOverlay.radius), tolerance: 14))
        guard part >= 0 else { return nil }
        var a = [Float](repeating: 0, count: 27)
        for i in 0..<9 { a[i] = t[11 + i] }                         // F
        for i in 0..<3 { a[9 + i] = t[20 + i]; a[12 + i] = t[20 + i] } // início = anterior
        a[18] = 1                                                    // Q acumulado = identidade
        a[19] = Float(part)
        a[20] = gx; a[21] = gy
        a[26] = Float(TrackballOverlay.radius)
        self.id = id
        center = c
        grab = point
        prev = point
        args = a
        label = part < 3 ? "girar no eixo \(["X", "Y", "Z"][part])" : part == 3 ? "girar na vista" : "girar livre"
        TrackballOverlay.activePart = part
    }

    /// Um passo do dedo; `begin` abre o passo de desfazer na 1ª mudança.
    func step(_ point: CGPoint, model: AureaModel, begin: (String) -> Void) {
        if !moved {
            // Folga de alça (4 pt): um toque parado nunca vira edição.
            guard hypot(point.x - grab.x, point.y - grab.y) >= 4 else { return }
            moved = true
        }
        guard point != prev else { return }
        args[22] = Float(prev.x - center.x); args[23] = Float(prev.y - center.y)
        args[24] = Float(point.x - center.x); args[25] = Float(point.y - center.y)
        prev = point
        let out = model.engine.trackballDrag(args.map { NSNumber(value: $0) }).map(\.floatValue)
        guard out.count == 7 else { return }
        begin(label)
        for i in 0..<3 { args[12 + i] = out[i] }   // continuidade: o próximo passo parte daqui
        for i in 0..<4 { args[15 + i] = out[3 + i] }
        model.gizmoSetComponents(id, base: 6, values: Array(out[0..<3]))
    }

    func end() { TrackballOverlay.activePart = -1 }
}

// =============================================================================
// Omega aparência (área 05, P1-2 e P1-3): o que o painel Transformar desenha no
// palco — as GUIAS do ímã do trackpad (`model.snapGuideX/Y`, px da composição,
// só durante o arrasto) e a TRAJETÓRIA da posição com a aba Mover aberta
// (`model.showMotionPath`). Chamado pelo `StageInteractionOverlay`
// (PreviewMetalView.swift). Par do desenho do palco no Android (Stage.kt).
// =============================================================================
@MainActor enum OmegaTransformStage {
    /// Trajetória em cache (espaço do PAI): só muda quando o modelo muda.
    private static var pathKey = ""
    private static var pathPoints: [SIMD2<Float>] = []
    private static var keyPoints: [SIMD2<Float>] = []

    static func draw(_ context: inout GraphicsContext, model: AureaModel, width: Float, height: Float,
                     screen: (Float, Float) -> CGPoint) {
        drawMotionPath(&context, model: model, screen: screen)
        // Guias do ímã: linhas finas no destaque, de borda a borda da composição.
        if let x = model.snapGuideX, x.isFinite {
            var line = Path(); line.move(to: screen(x, 0)); line.addLine(to: screen(x, height))
            context.stroke(line, with: .color(StageInk.outlineUnder), lineWidth: 2.5)
            context.stroke(line, with: .color(AureaColors.accent), lineWidth: 1)
        }
        if let y = model.snapGuideY, y.isFinite {
            var line = Path(); line.move(to: screen(0, y)); line.addLine(to: screen(width, y))
            context.stroke(line, with: .color(StageInk.outlineUnder), lineWidth: 2.5)
            context.stroke(line, with: .color(AureaColors.accent), lineWidth: 1)
        }
    }

    /// 0.4.6: a trajetória como o motor a AVALIA (`positionPath`, com a bézier
    /// espacial) e as alças de cada canto (par do TransformStage.kt). Pontos no
    /// espaço do pai; o desenho leva para a composição pelo `parentAffine`.
    private static func drawMotionPath(_ context: inout GraphicsContext, model: AureaModel,
                                       screen: (Float, Float) -> CGPoint) {
        guard pathActive(model), let id = model.primarySelection else { return }
        refresh(model: model, layer: id)
        guard pathPoints.count >= 2 else { return }
        let affine = StageGeom.floats(model.detail["parentAffine"])
        func place(_ p: SIMD2<Float>) -> CGPoint {
            let c = StageGeom.applyAffine(affine, p.x, p.y)
            return screen(c.0, c.1)
        }
        var path = Path()
        path.move(to: place(pathPoints[0]))
        for p in pathPoints.dropFirst() { path.addLine(to: place(p)) }
        context.stroke(path, with: .color(StageInk.outlineUnder), style: StrokeStyle(lineWidth: 3, lineCap: .round, lineJoin: .round))
        context.stroke(path, with: .color(AureaColors.text.opacity(0.6)), style: StrokeStyle(lineWidth: 1.5, lineCap: .round, lineJoin: .round))
        for key in pathKeys {
            let v = place(key.position)
            for handle in [key.inHandle, key.outHandle] where simd_length(handle) > 0.01 {
                let h = place(key.position + handle)
                var line = Path(); line.move(to: v); line.addLine(to: h)
                context.stroke(line, with: .color(AureaColors.accent), lineWidth: 1)
                context.fill(Path(ellipseIn: CGRect(x: h.x - 3, y: h.y - 3, width: 6, height: 6)), with: .color(AureaColors.accent))
            }
            let box = CGRect(x: v.x - 3.5, y: v.y - 3.5, width: 7, height: 7)
            context.fill(Path(box.insetBy(dx: -1, dy: -1)), with: .color(StageInk.outlineUnder))
            context.fill(Path(box), with: .color(AureaColors.text.opacity(0.85)))
        }
    }

    struct PathKey { var time: Int32; var position: SIMD2<Float>; var inHandle: SIMD2<Float>; var outHandle: SIMD2<Float> }
    private static var pathKeys: [PathKey] = []

    static func pathActive(_ model: AureaModel) -> Bool {
        guard model.showMotionPath, model.panel == .transform, model.selection.count == 1,
              let id = model.primarySelection, let row = model.selectedLayer, row.id == id,
              !row.threeD, ![UInt32(8), 9, 10].contains(row.kind) else { return false }
        return true
    }

    /// Amostras (espaço do pai) e os cantos com as alças, refeitos quando o modelo muda.
    private static func refresh(model: AureaModel, layer: Int64) {
        let key = "\(layer):\(model.status.modelRevision)"
        if key == pathKey { return }
        pathKey = key
        let raw = model.engine.positionPath(layer).map(\.floatValue)
        pathPoints = []; pathKeys = []; keyPoints = []
        guard raw.count >= 2 else { return }
        let n = Int(raw[0]), k = Int(raw[1])
        guard n >= 2, raw.count >= 2 + n * 2 + k * 7 else { return }
        pathPoints = (0..<n).map { SIMD2(raw[2 + $0 * 2], raw[3 + $0 * 2]) }
        let base = 2 + n * 2
        pathKeys = (0..<k).map { i in
            let o = base + i * 7
            return PathKey(time: Int32(raw[o]), position: SIMD2(raw[o + 1], raw[o + 2]),
                           inHandle: SIMD2(raw[o + 3], raw[o + 4]), outHandle: SIMD2(raw[o + 5], raw[o + 6]))
        }
        keyPoints = pathKeys.map(\.position)
    }

    /// O que o dedo pegou (ponto da composição): canto (0), ponta da entrada (1) ou da saída (2).
    static func pathHit(_ model: AureaModel, _ comp: SIMD2<Float>, reach: Float) -> (Int, Int)? {
        guard pathActive(model), let id = model.primarySelection else { return nil }
        refresh(model: model, layer: id)
        let affine = StageGeom.floats(model.detail["parentAffine"])
        var best: (Int, Int)? = nil, bestD = reach
        for (i, key) in pathKeys.enumerated() {
            for part in 0...2 {
                let off = part == 0 ? SIMD2<Float>(0, 0) : part == 1 ? key.inHandle : key.outHandle
                if part > 0 && simd_length(off) < 0.01 { continue }
                let c = StageGeom.applyAffine(affine, key.position.x + off.x, key.position.y + off.y)
                let d = hypot(c.0 - comp.x, c.1 - comp.y)
                if d < bestD || (part > 0 && d < bestD + reach * 0.3) { bestD = d; best = (i, part) }
            }
        }
        return best
    }

    /// Arrasto: o canto arranca as duas alças simétricas; a ponta de uma alça a
    /// ajusta e a outra acompanha espelhada (como no After Effects).
    static func pathDrag(_ model: AureaModel, hit: (Int, Int), comp: SIMD2<Float>, first: Bool) {
        guard let id = model.primarySelection, hit.0 < pathKeys.count else { return }
        let key = pathKeys[hit.0]
        let affine = StageGeom.floats(model.detail["parentAffine"])
        let local = toParent(affine, comp)
        let d = local - key.position
        if first { model.beginGesture("curvar caminho") }
        if hit.1 == 1 { model.setPositionTangents(id, time: key.time, inHandle: d, outHandle: -d) }
        else { model.setPositionTangents(id, time: key.time, inHandle: -d, outHandle: d) }
    }

    /// Toque parado no canto: volta a canto reto.
    static func pathReset(_ model: AureaModel, hit: (Int, Int)) {
        guard let id = model.primarySelection, hit.0 < pathKeys.count else { return }
        model.beginGesture("canto reto")
        model.setPositionTangents(id, time: pathKeys[hit.0].time, inHandle: .zero, outHandle: .zero)
        model.endGesture()
    }

    private static func toParent(_ affine: [Float], _ p: SIMD2<Float>) -> SIMD2<Float> {
        guard affine.count == 6 else { return p }
        let det = affine[0] * affine[3] - affine[1] * affine[2]
        guard abs(det) > 0.000001 else { return p }
        let x = p.x - affine[4], y = p.y - affine[5]
        return SIMD2((affine[3] * x - affine[2] * y) / det, (-affine[1] * x + affine[0] * y) / det)
    }
}
