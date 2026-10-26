// =============================================================================
//  Aurea / platform / ios / app / DrawingEditorView.swift
//
//  Omega conteúdo (área 06, P0-4): o EDITOR DE DESENHO.
//
//    ┌───────────────────────────────────────────┬──────────┐
//    │[⟋][====== espessura (42 passos) =====][12]│  Caneta  │ coluna de 60 pt,
//    │ seletor de cor compacto (até 320 pt)       │  Pincel  │ 4 botões de altura
//    │                                            │ Preencher│ igual
//    │                                            │ Borracha │
//    └───────────────────────────────────────────┴──────────┘
//  O dedo (ou o Pencil, com a pressão) no palco desenha: cada traço vira um
//  grupo da camada de desenho no motor (Engine::add_drawing_stroke), um passo
//  de desfazer por traço. Ferramenta, espessura e cor ficam salvas no aparelho
//  (ContentPrefs). Par do DrawingEditor.kt (mesmos ids).
// =============================================================================
import SwiftUI
import UIKit

/// Os 42 passos da régua de espessura (1…20 de 1 em 1, depois mais largos).
enum DrawWidthSteps {
    static let values: [Int] = [
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
        22, 24, 26, 28, 30, 33, 36, 40, 45, 50, 60, 70, 80, 90, 100, 120, 140, 160, 180, 200, 250, 300,
    ]
    static func nearest(_ width: Int) -> Int {
        var best = 0
        for i in values.indices where abs(values[i] - width) < abs(values[best] - width) { best = i }
        return best
    }
}

/// O traço em curso no palco (px da composição), para o desenho ao vivo.
@MainActor final class DrawingStageState: ObservableObject {
    static let shared = DrawingStageState()
    @Published var stroke: [CGPoint] = []
    /// x, y (composição) e pressão intercalados: o que vai ao motor ao soltar.
    var samples: [Float] = []
    /// O toque começou fora do tempo da camada (avisou e não desenha).
    var blocked = false
}

@MainActor enum DrawingStage {
    /// O palco está desenhando? (painel Desenho aberto numa camada de desenho escolhida.)
    static func active(_ model: AureaModel) -> Bool {
        guard model.panel == .drawing, model.selection.count == 1, let row = model.selectedLayer, !row.locked else { return false }
        return model.isDrawingLayer
    }
}

// MARK: - O painel

struct DrawingPanelView: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var prefs = ContentPrefs.shared

    var body: some View {
        HStack(spacing: 0) {
            VStack(alignment: .leading, spacing: 8) {
                widthRow
                ContentColorPicker(color: prefs.drawRGBA, tag: "draw.color", label: AureaText.t("om_draw_color_cd"),
                                   onChange: { prefs.setDrawColor($0) })
                    .frame(maxWidth: 320)
            }
            .padding(.horizontal, 12).padding(.vertical, 6)
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
            AureaColors.contentStrongLine.frame(width: 0.7)
            VStack(spacing: 0) {
                tool(0, "om_draw_tool_pen", "draw.tool.pen") { tint in CupertinoGlyph.text(CupertinoGlyph.Pencil, size: 22, color: tint) }
                tool(1, "om_draw_tool_brush", "draw.tool.brush") { tint in CupertinoGlyph.text(CupertinoGlyph.Paintbrush, size: 22, color: tint) }
                tool(2, "om_draw_tool_fill", "draw.tool.fill") { tint in CupertinoGlyph.text(CupertinoGlyph.DropFill, size: 22, color: tint) }
                tool(3, "om_draw_tool_eraser", "draw.tool.eraser") { tint in ContentIcon(kind: .eraser, size: 22, tint: tint) }
            }
            .frame(width: 60)
        }
        .foregroundStyle(AureaColors.text)
        .background(AureaColors.panelFrameBackground)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("draw.panel")
    }

    private func tool<Icon: View>(_ value: Int, _ label: String, _ tag: String, @ViewBuilder icon: (Color) -> Icon) -> some View {
        let on = prefs.drawTool == value
        return Button { prefs.setDrawTool(value) } label: {
            icon(on ? AureaColors.accent : AureaColors.text)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .accessibilityLabel(AureaText.t(label))
        .accessibilityAddTraits(on ? .isSelected : [])
        .accessibilityIdentifier(tag)
    }

    /// A linha da espessura: ícone, régua de 42 passos e a pílula do valor.
    private var widthRow: some View {
        let width = prefs.drawWidth
        let index = DrawWidthSteps.nearest(width)
        let frac = CGFloat(index) / CGFloat(DrawWidthSteps.values.count - 1)
        return HStack(spacing: 8) {
            CupertinoGlyph.text(CupertinoGlyph.Scribble, size: 20)
            GeometryReader { geo in
                let w = geo.size.width
                ZStack(alignment: .leading) {
                    Capsule().fill(AureaColors.fxTrack).frame(height: 3)
                    Capsule().fill(AureaColors.accent).frame(width: w * frac, height: 3)
                    Circle().fill(AureaColors.accent).frame(width: 18, height: 18).offset(x: w * frac - 9)
                }
                .frame(maxHeight: .infinity)
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0).onChanged { g in
                    let i = Int((g.location.x / max(1, w) * CGFloat(DrawWidthSteps.values.count - 1)).rounded())
                    prefs.setDrawWidth(DrawWidthSteps.values[min(DrawWidthSteps.values.count - 1, max(0, i))])
                })
            }
            .accessibilityElement()
            .accessibilityLabel(AureaText.t("om_draw_width_value_cd", width))
            .accessibilityAdjustableAction { direction in
                let next = direction == .increment ? min(DrawWidthSteps.values.count - 1, index + 1) : max(0, index - 1)
                prefs.setDrawWidth(DrawWidthSteps.values[next])
            }
            .accessibilityIdentifier("draw.width.slider")
            Text("\(width)").font(.aurea(size: 12, weight: .semibold)).foregroundStyle(AureaColors.accent)
                .frame(width: 35, height: 22)
                .background(AureaColors.contentValuePill, in: RoundedRectangle(cornerRadius: 3.75))
                .accessibilityIdentifier("draw.width.value")
        }
        .frame(height: 40)
    }
}

// MARK: - O palco

/// O traço ao vivo e o toque do desenho (o dedo grava x, y e a pressão até subir;
/// soltar manda o traço inteiro ao motor).
@MainActor struct DrawingStageOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var live = DrawingStageState.shared
    @ObservedObject private var prefs = ContentPrefs.shared
    @ObservedObject private var viewZoom = StageViewZoom.shared

    var body: some View {
        GeometryReader { geo in
            let placed = StageZoomMath.fit(size: geo.size, composition: CGSize(width: CGFloat(model.compositionWidth), height: CGFloat(model.compositionHeight)),
                                           zoom: viewZoom.zoom, pan: viewZoom.pan)
            ZStack {
                Canvas { context, _ in
                    let pts = live.stroke.map { CGPoint(x: placed.origin.x + $0.x * placed.scale, y: placed.origin.y + $0.y * placed.scale) }
                    guard pts.count >= 2 else { return }
                    var path = Path()
                    path.addLines(pts)
                    let rgba = prefs.drawRGBA
                    let ink = prefs.drawTool == 3 ? Color.white.opacity(0.5)
                        : Color(.sRGB, red: Double(rgba[0]), green: Double(rgba[1]), blue: Double(rgba[2]), opacity: Double(rgba[3]))
                    if prefs.drawTool == 2 {
                        path.closeSubpath()
                        context.fill(path, with: .color(ink.opacity(0.6)))
                        context.stroke(path, with: .color(ink), lineWidth: 1.5)
                    } else {
                        context.stroke(path, with: .color(ink),
                                       style: StrokeStyle(lineWidth: max(1.5, CGFloat(prefs.drawWidth) * placed.scale), lineCap: .round, lineJoin: .round))
                    }
                }
                .allowsHitTesting(false)
                DrawingTouchSurface { phase, samples in touch(phase, samples, placed) }
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel(AureaText.t("om_draw_title"))
        .accessibilityIdentifier("stage.drawing")
    }

    private func touch(_ phase: DrawingTouchSurface.Phase, _ samples: [(CGPoint, CGFloat)], _ placed: (scale: CGFloat, origin: CGPoint)) {
        let k = max(placed.scale, 0.0001)
        func add(_ list: [(CGPoint, CGFloat)]) {
            for (p, pressure) in list {
                let c = CGPoint(x: (p.x - placed.origin.x) / k, y: (p.y - placed.origin.y) / k)
                live.samples += [Float(c.x), Float(c.y), Float(min(1, max(0, pressure)))]
                live.stroke.append(c)
            }
        }
        switch phase {
        case .began:
            live.samples = []; live.stroke = []
            guard let row = model.selectedLayer, Int64(row.startFrame) <= model.status.playhead, model.status.playhead < Int64(row.endFrame) else {
                live.blocked = true
                model.toast = AureaText.t("sh_playhead_into_layer")
                return
            }
            live.blocked = false
            if model.status.playing != 0 { model.playPause() }
            add(samples)
        case .moved:
            if !live.blocked { add(samples) }
        case .ended:
            guard !live.blocked else { return }
            add(samples)
            let points = live.samples
            live.samples = []; live.stroke = []
            model.commitDrawingStroke(tool: prefs.drawTool, rgba: prefs.drawRGBA, width: Float(prefs.drawWidth), points: points)
        case .cancelled:
            live.samples = []; live.stroke = []; live.blocked = false
        }
    }
}

/// A superfície que lê os toques com a pressão (força do 3D Touch/Pencil; dedo
/// sem força = 1) e os toques agrupados entre dois quadros.
struct DrawingTouchSurface: UIViewRepresentable {
    enum Phase { case began, moved, ended, cancelled }
    let onTouch: (Phase, [(CGPoint, CGFloat)]) -> Void

    func makeUIView(context: Context) -> DrawingTouchView {
        let view = DrawingTouchView()
        view.backgroundColor = .clear
        view.isMultipleTouchEnabled = false
        view.onTouch = onTouch
        return view
    }

    func updateUIView(_ view: DrawingTouchView, context: Context) { view.onTouch = onTouch }
}

final class DrawingTouchView: UIView {
    var onTouch: ((DrawingTouchSurface.Phase, [(CGPoint, CGFloat)]) -> Void)?
    private weak var tracked: UITouch?

    private static func pressure(_ t: UITouch) -> CGFloat {
        t.maximumPossibleForce > 0 ? min(1, max(0, t.force / t.maximumPossibleForce)) : 1
    }

    private func samples(_ touch: UITouch, _ event: UIEvent?) -> [(CGPoint, CGFloat)] {
        let all = event?.coalescedTouches(for: touch) ?? [touch]
        return all.map { ($0.location(in: self), Self.pressure($0)) }
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard tracked == nil, let t = touches.first else { return }
        tracked = t
        onTouch?(.began, [(t.location(in: self), Self.pressure(t))])
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let t = tracked, touches.contains(t) else { return }
        onTouch?(.moved, samples(t, event))
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let t = tracked, touches.contains(t) else { return }
        tracked = nil
        onTouch?(.ended, [(t.location(in: self), Self.pressure(t))])
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let t = tracked, touches.contains(t) else { return }
        tracked = nil
        onTouch?(.cancelled, [])
    }
}
