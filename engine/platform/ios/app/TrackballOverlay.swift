import SwiftUI
import UIKit

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

    /// A linha pelos keyframes de posição (X/Y amostrados pelo motor com a
    /// mesma conta da prévia, `trackCurve`), levada do pai para a composição
    /// pelo `parentAffine` do detalhe; cor `texto` a 60 %.
    private static func drawMotionPath(_ context: inout GraphicsContext, model: AureaModel,
                                       screen: (Float, Float) -> CGPoint) {
        guard model.showMotionPath, model.panel == .transform, model.selection.count == 1,
              let id = model.primarySelection, let row = model.selectedLayer, row.id == id,
              !row.threeD, ![UInt32(8), 9, 10].contains(row.kind) else { return }
        let points = motionPath(model: model, layer: id)
        guard points.count >= 2 else { return }
        let affine = StageGeom.floats(model.detail["parentAffine"])
        func place(_ p: SIMD2<Float>) -> CGPoint {
            let c = StageGeom.applyAffine(affine, p.x, p.y)
            return screen(c.0, c.1)
        }
        var path = Path()
        path.move(to: place(points[0]))
        for p in points.dropFirst() { path.addLine(to: place(p)) }
        context.stroke(path, with: .color(StageInk.outlineUnder), style: StrokeStyle(lineWidth: 3, lineCap: .round, lineJoin: .round))
        context.stroke(path, with: .color(AureaColors.text.opacity(0.6)), style: StrokeStyle(lineWidth: 1.5, lineCap: .round, lineJoin: .round))
        for key in keyPoints {
            let c = place(key)
            let dot = Path(ellipseIn: CGRect(x: c.x - 3, y: c.y - 3, width: 6, height: 6))
            context.fill(dot, with: .color(StageInk.outlineUnder))
            context.fill(Path(ellipseIn: CGRect(x: c.x - 2, y: c.y - 2, width: 4, height: 4)), with: .color(AureaColors.text.opacity(0.85)))
        }
    }

    /// Amostras da posição (espaço do pai) entre o primeiro e o último keyframe
    /// de X/Y. Um eixo sem trilha fica no valor parado do detalhe.
    private static func motionPath(model: AureaModel, layer: Int64) -> [SIMD2<Float>] {
        let keys = (model.keyframes[layer] ?? []).filter { ($0.property == 0 || $0.property == 1) && $0.effectIndex == UInt32.max }
        guard keys.count >= 2, let from = keys.map(\.time).min(), let to = keys.map(\.time).max(), to > from else {
            pathKey = ""; pathPoints = []; keyPoints = []
            return []
        }
        let key = "\(layer):\(model.status.modelRevision):\(from):\(to):\(keys.count)"
        if key == pathKey { return pathPoints }
        let xs = model.engine.trackCurve(layer, property: 0, effect: UInt32.max, param: 0, from: from, to: to).map(\.floatValue)
        let ys = model.engine.trackCurve(layer, property: 1, effect: UInt32.max, param: 0, from: from, to: to).map(\.floatValue)
        let position = StageGeom.floats(model.detail["position"])
        let restX = position.count > 0 ? position[0] : 0, restY = position.count > 1 ? position[1] : 0
        let count = max(xs.count, ys.count)
        var out: [SIMD2<Float>] = []
        if count >= 2 {
            out.reserveCapacity(count)
            for i in 0..<count {
                let x = xs.count == count ? xs[i] : restX
                let y = ys.count == count ? ys[i] : restY
                if x.isFinite && y.isFinite { out.append(SIMD2(x, y)) }
            }
        }
        // Os pontos dos keyframes: a amostra mais perto de cada tempo de chave.
        var marks: [SIMD2<Float>] = []
        if out.count >= 2 {
            let times = Set(keys.map(\.time)).sorted()
            for time in times {
                let t = Double(time - from) / Double(to - from)
                let index = min(out.count - 1, max(0, Int((t * Double(out.count - 1)).rounded())))
                marks.append(out[index])
            }
        }
        pathKey = key; pathPoints = out; keyPoints = marks
        return out
    }
}
