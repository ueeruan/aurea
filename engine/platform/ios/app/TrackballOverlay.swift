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
