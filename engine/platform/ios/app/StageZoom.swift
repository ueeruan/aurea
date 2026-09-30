// =============================================================================
//  Aurea / platform / ios / app / StageZoom.swift
//
//  Zoom da VISTA do palco (lupa da prévia), par do StageZoom.kt do Android: só
//  muda como a composição aparece na tela, nunca o projeto, o render nem o
//  export. O motor aplica o mesmo zoom/pan no passe de saída da prévia
//  (`viewportZoom`/`viewportPan`, em px do drawable); o overlay e os gestos
//  usam os mesmos números para desenhar as alças e converter o toque.
// =============================================================================
import SwiftUI
import UIKit

/// Contas puras (as mesmas do StageZoomMath.kt, testadas na JVM do Android).
enum StageZoomMath {
    static let minZoom: CGFloat = 1
    static let maxZoom: CGFloat = 8

    /// Zoom preso em [1×, 8×]; lixo (NaN/∞) volta ao encaixe.
    static func clampZoom(_ z: CGFloat) -> CGFloat { z.isFinite ? min(maxZoom, max(minZoom, z)) : minZoom }

    /// Pan máximo num eixo: a borda da composição vai até onde fica em 100 %.
    static func maxPan(_ zoom: CGFloat, _ baseExtent: CGFloat) -> CGFloat {
        guard baseExtent.isFinite && baseExtent > 0 else { return 0 }
        return max(0, (clampZoom(zoom) - 1) * baseExtent / 2)
    }

    static func clampPan(_ pan: CGFloat, zoom: CGFloat, baseExtent: CGFloat) -> CGFloat {
        guard pan.isFinite else { return 0 }
        let limit = maxPan(zoom, baseExtent)
        return min(limit, max(-limit, pan))
    }

    /// Pinça: o ponto sob o meio inicial dos dedos (`mid0`) fica sob `mid`.
    static func pinchPan(_ pan0: CGFloat, zoom0: CGFloat, zoom1: CGFloat, mid0: CGFloat, mid: CGFloat, centre: CGFloat) -> CGFloat {
        let z0 = zoom0 > 0 ? zoom0 : 1
        return (mid - centre) - (mid0 - centre - pan0) * (zoom1 / z0)
    }

    static func isZoomed(_ zoom: CGFloat) -> Bool { zoom > minZoom + 0.005 }
    static func percent(_ zoom: CGFloat) -> Int { Int((zoom * 100).rounded()) }

    /// Encaixe da composição no palco com o zoom/pan da vista: escala (pt por
    /// px da composição) e origem (canto sup-esq da composição, em pt).
    static func fit(size: CGSize, composition: CGSize, zoom: CGFloat, pan: CGSize) -> (scale: CGFloat, origin: CGPoint) {
        let cw = max(1, composition.width), ch = max(1, composition.height)
        let base = min(size.width / cw, size.height / ch)
        let scale = base * zoom
        return (scale, CGPoint(x: (size.width - cw * scale) / 2 + pan.width, y: (size.height - ch * scale) / 2 + pan.height))
    }
}

/// Same pointer state machine as Android: stable IDs are owned by the arbiter.
/// Rebase at scale limits and discard angle changes while fingers cross.
struct StagePinchTracker {
    private(set) var factor: Float = 1
    private(set) var degrees: Float = 0
    private var span: CGFloat = 0
    private var angle: CGFloat = 0
    private var minimumSpan: CGFloat = 16
    private var ready = false
    mutating func start(_ a: CGPoint, _ b: CGPoint, minimum: CGFloat = 16) {
        factor = 1; degrees = 0; minimumSpan = max(1, minimum)
        span = hypot(a.x - b.x, a.y - b.y); angle = atan2(b.y - a.y, b.x - a.x)
        ready = span.isFinite && span >= minimumSpan
    }
    mutating func update(_ a: CGPoint, _ b: CGPoint, clamp: (Float) -> Float) -> Bool {
        guard a.x.isFinite && a.y.isFinite && b.x.isFinite && b.y.isFinite else { return false }
        let nextSpan = hypot(a.x - b.x, a.y - b.y), nextAngle = atan2(b.y - a.y, b.x - a.x)
        guard nextSpan.isFinite && nextAngle.isFinite else { return false }
        if !ready || nextSpan < minimumSpan {
            span = nextSpan; angle = nextAngle; ready = nextSpan >= minimumSpan
            return false
        }
        var delta = nextAngle - angle
        if delta > .pi { delta -= 2 * .pi }; if delta < -.pi { delta += 2 * .pi }
        let next = clamp(factor * Float(nextSpan / span))
        guard next.isFinite && next > 0 else { return false }
        let changed = abs(next - factor) > 0.000001 || abs(delta) > 0.000001
        factor = next; degrees += Float(delta * 180 / .pi); span = nextSpan; angle = nextAngle
        return changed
    }
}

/// Estado da vista do palco (sessão do editor, nunca vai para o projeto).
@MainActor final class StageViewZoom: ObservableObject {
    static let shared = StageViewZoom()
    @Published private(set) var zoom: CGFloat = 1
    /// Pan em pt da tela (o motor recebe em px do drawable).
    @Published private(set) var pan: CGSize = .zero
    /// Lupa ligada: a pinça sempre amplia a VISTA, mesmo sobre a camada escolhida.
    @Published var zoomLock = false
    private var projectKey: URL??

    var zoomed: Bool { StageZoomMath.isZoomed(zoom) }

    /// Aplica e manda ao motor (só se mudou). `pixelScale` = pt → px do drawable.
    func set(zoom z: CGFloat, pan p: CGSize, engine: AureaEngine, pixelScale: CGFloat) {
        let nz = StageZoomMath.clampZoom(z)
        let np = CGSize(width: p.width.isFinite ? p.width : 0, height: p.height.isFinite ? p.height : 0)
        if abs(nz - zoom) < 0.00001 && abs(np.width - pan.width) < 0.01 && abs(np.height - pan.height) < 0.01 { return }
        zoom = nz; pan = np
        engine.run { $0.setViewportZoom(Float(nz), panX: Float(np.width * pixelScale), panY: Float(np.height * pixelScale)) }
    }

    /// De volta ao encaixe (100 %).
    func reset(engine: AureaEngine) {
        zoom = 1; pan = .zero
        engine.run { $0.setViewportZoom(1, panX: 0, panY: 0) }
    }

    /// Cada projeto abre no encaixe (o arquivo pode trazer um zoom salvo).
    func resetIfProjectChanged(_ url: URL?, engine: AureaEngine) {
        if case .some(let key) = projectKey, key == url { return }
        projectKey = .some(url)
        reset(engine: engine)
    }
}

/// Chip "250 %" no canto inf-dir do palco: só com a vista ampliada; tocar
/// volta ao encaixe.
struct StageZoomChip: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var view = StageViewZoom.shared
    var body: some View {
        if view.zoomed {
            let pct = StageZoomMath.percent(view.zoom)
            Button { view.reset(engine: model.engine) } label: {
                HStack(spacing: 5) {
                    Image(systemName: "arrow.down.right.and.arrow.up.left").font(.system(size: 11, weight: .semibold)).foregroundColor(AureaColors.accent)
                    Text(AureaText.t("common_percent", pct)).font(.aurea(size: 12, weight: .semibold)).foregroundColor(AureaColors.text)
                }
                .padding(.horizontal, 10).frame(minHeight: 36)
                .background(Capsule().fill(Color.black.opacity(0.6)))
            }
            .buttonStyle(.plain)
            .accessibilityLabel(AureaText.t("stage_zoom_fit", pct))
        }
    }
}

/// A lupa da barra de transporte (ao lado da tela cheia).
struct StageZoomToggleButton: View {
    @ObservedObject private var view = StageViewZoom.shared
    let width: CGFloat
    let height: CGFloat
    var body: some View {
        Image(systemName: "plus.magnifyingglass")
            .font(.system(size: 18, weight: .regular))
            .foregroundColor(view.zoomLock ? AureaColors.accent : AureaColors.text)
            .frame(width: width, height: height)
            .contentShape(Rectangle())
            .onTapGesture { view.zoomLock.toggle() }
            .accessibilityLabel(AureaText.t(view.zoomLock ? "stage_zoom_view_on" : "stage_zoom_view_off"))
            .accessibilityAddTraits(.isButton)
    }
}

/// Lupa no canto sup-esq da prévia (redesenho 2026-09-29, Efeitos.dc.html; par do
/// `StageZoomButton` do Android): 34×30 colada à borda (raio 0/6/6/0, #2A3447),
/// liga/desliga a mesma lupa do menu da engrenagem. O alvo de toque é 48×44.
struct StageZoomCornerButton: View {
    @ObservedObject private var view = StageViewZoom.shared
    var body: some View {
        Image(systemName: "plus.magnifyingglass")
            .font(.system(size: 16, weight: .semibold))
            .foregroundColor(view.zoomLock ? AureaColors.accent : AureaColors.text)
            .frame(width: 34, height: 30)
            .background(UnevenRoundedCorners(radius: 6).fill(Color(red: 0x2A / 255, green: 0x34 / 255, blue: 0x47 / 255)))
            .padding(.top, 8)
            .frame(width: 48, height: 44, alignment: .topLeading)
            .contentShape(Rectangle())
            .onTapGesture { view.zoomLock.toggle() }
            .accessibilityLabel(AureaText.t(view.zoomLock ? "stage_zoom_view_on" : "stage_zoom_view_off"))
            .accessibilityAddTraits(.isButton)
            .accessibilityIdentifier("stage.zoom.toggle")
    }
}

/// Retângulo com só os cantos da DIREITA arredondados (iOS 15: sem `UnevenRoundedRectangle`).
private struct UnevenRoundedCorners: Shape {
    let radius: CGFloat
    func path(in rect: CGRect) -> Path {
        let r = min(radius, rect.height / 2, rect.width / 2)
        var p = Path()
        p.move(to: CGPoint(x: rect.minX, y: rect.minY))
        p.addLine(to: CGPoint(x: rect.maxX - r, y: rect.minY))
        p.addArc(center: CGPoint(x: rect.maxX - r, y: rect.minY + r), radius: r, startAngle: .degrees(-90), endAngle: .degrees(0), clockwise: false)
        p.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY - r))
        p.addArc(center: CGPoint(x: rect.maxX - r, y: rect.maxY - r), radius: r, startAngle: .degrees(0), endAngle: .degrees(90), clockwise: false)
        p.addLine(to: CGPoint(x: rect.minX, y: rect.maxY))
        p.closeSubpath()
        return p
    }
}
