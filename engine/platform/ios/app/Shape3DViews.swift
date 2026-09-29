import SwiftUI
import UIKit
import PhotosUI

// =============================================================================
// FORMAS 3D — port de state/Shape3DInfo.kt, editor/ShapePartStage.kt,
// editor/Shape3DIcons.kt e editor/panels/Shape3DSection.kt (Android).
//
//  - Adicionar: "Formas 3D" na aba 3D abre a grade das 10 formas prontas.
//  - Painel 3D: as PARTES em fichas ("Tudo" = a forma inteira); cor e imagem
//    da galeria por parte, losango de keyframe, reset e réguas de posição,
//    giro e escala (o mesmo motor de keyframes: preview = export).
//  - Palco: com uma parte escolhida, tocar escolhe outra (vazio = forma
//    inteira), arrastar move a parte, pinça escala e gira. Um passo de
//    desfazer por gesto. Sem parte escolhida o palco fica limpo.
// Geometria, trilhas e imagens são do motor (Engine::add_shape3d e família).
// =============================================================================
@MainActor final class Shape3DState: ObservableObject {
    static let shared = Shape3DState()
    /// Floats por parte do `shape3DParts` (Engine::kShapePartFloats).
    static let partFloats = 14
    static let names = ["shape3d_cube", "shape3d_sphere", "shape3d_cylinder", "shape3d_cone", "shape3d_pyramid",
                        "shape3d_torus", "shape3d_star", "shape3d_heart", "shape3d_capsule", "shape3d_diamond"]

    @Published var layer: Int64 = 0
    /// Parte escolhida (−1 = a forma inteira).
    @Published var part = -1
    @Published var revision = 0
    /// O arrasto em curso já abriu o passo de desfazer (o resto vai junto).
    var gestureSent = false

    func part(_ model: AureaModel) -> Int {
        guard model.selection.count == 1, let id = model.primarySelection, id == layer else { return -1 }
        return part
    }
    func choose(_ model: AureaModel, _ p: Int) {
        guard let id = model.primarySelection else { return }
        layer = id
        part = p
        revision += 1
    }
    /// O palco está no modo de parte (forma 3D escolhida, destravada, fora da cena 3D)?
    func active(_ model: AureaModel) -> Bool {
        guard part(model) >= 0, !model.sceneEditor, let l = model.selectedLayer, !l.locked else { return false }
        return !model.engine.shape3D(layer).isEmpty
    }

    /// Nome curto da parte (a mesma ordem das partes do motor).
    static func partLabel(kind: Int, part: Int) -> String {
        let n = part + 1
        func pick(_ keys: [String]) -> String { part < keys.count ? AureaText.t(keys[part]) : AureaText.t("shape3d_part_n", n) }
        switch kind {
        case 0: return pick(["shape3d_part_front", "shape3d_part_back", "shape3d_part_right", "shape3d_part_left", "shape3d_part_top", "shape3d_part_bottom"])
        case 1: return pick(["shape3d_part_upper", "shape3d_part_lower"])
        case 2: return pick(["shape3d_part_top", "shape3d_part_bottom", "shape3d_part_side"])
        case 3: return pick(["shape3d_part_base", "shape3d_part_side"])
        case 4: return pick(["shape3d_part_base", "shape3d_part_front", "shape3d_part_right", "shape3d_part_back", "shape3d_part_left"])
        case 6: return part == 5 ? AureaText.t("shape3d_part_center") : AureaText.t("shape3d_part_tip", n)
        case 7: return pick(["shape3d_part_left", "shape3d_part_right"])
        case 8: return pick(["shape3d_part_top", "shape3d_part_body", "shape3d_part_bottom"])
        default: return AureaText.t("shape3d_part_n", n)
        }
    }
}

/// Receita lida do motor: forma, cor RGBA (sRGB) e "tem imagem" por parte.
struct Shape3DInfo {
    let kind: Int
    let colors: [[Float]]
    let images: [Bool]
    init?(_ raw: [Float]) {
        guard raw.count >= 2 else { return nil }
        let n = Int(raw[1])
        guard n > 0, raw.count >= 2 + n * 5 else { return nil }
        kind = Int(raw[0])
        colors = (0..<n).map { Array(raw[(2 + $0 * 5)..<(6 + $0 * 5)]) }
        images = (0..<n).map { raw[6 + $0 * 5] > 0.5 }
    }
}

/// Ícone de cada forma (traço fino com uma parte em destaque) — Shape3DIcons.kt.
struct Shape3DGlyph: View {
    let kind: Int
    var body: some View {
        Canvas { context, size in
            let s = min(size.width, size.height)
            let o = CGPoint(x: (size.width - s) / 2, y: (size.height - s) / 2)
            func p(_ x: CGFloat, _ y: CGFloat) -> CGPoint { CGPoint(x: o.x + x * s, y: o.y + y * s) }
            let line = GraphicsContext.Shading.color(AureaColors.text)
            let face = GraphicsContext.Shading.color(AureaColors.accent.opacity(0.55))
            let width = s * 0.055
            func poly(_ pts: [CGPoint], fill: Bool = false) {
                var path = Path()
                path.addLines(pts)
                path.closeSubpath()
                if fill { context.fill(path, with: face) }
                context.stroke(path, with: line, lineWidth: width)
            }
            switch kind {
            case 0:
                poly([p(0.18, 0.34), p(0.62, 0.34), p(0.62, 0.84), p(0.18, 0.84)], fill: true)
                poly([p(0.18, 0.34), p(0.38, 0.16), p(0.82, 0.16), p(0.62, 0.34)])
                poly([p(0.62, 0.34), p(0.82, 0.16), p(0.82, 0.66), p(0.62, 0.84)])
            case 1:
                var top = Path()
                top.addArc(center: p(0.5, 0.5), radius: s * 0.36, startAngle: .degrees(180), endAngle: .degrees(360), clockwise: false)
                top.closeSubpath()
                context.fill(top, with: face)
                context.stroke(Path(ellipseIn: CGRect(x: o.x + 0.14 * s, y: o.y + 0.14 * s, width: 0.72 * s, height: 0.72 * s)), with: line, lineWidth: width)
                context.stroke(Path(ellipseIn: CGRect(x: o.x + 0.14 * s, y: o.y + 0.42 * s, width: 0.72 * s, height: 0.16 * s)), with: line, lineWidth: width)
            case 2:
                let cap = Path(ellipseIn: CGRect(x: o.x + 0.22 * s, y: o.y + 0.12 * s, width: 0.56 * s, height: 0.2 * s))
                context.fill(cap, with: face)
                context.stroke(cap, with: line, lineWidth: width)
                var sides = Path()
                sides.move(to: p(0.22, 0.22)); sides.addLine(to: p(0.22, 0.78))
                sides.move(to: p(0.78, 0.22)); sides.addLine(to: p(0.78, 0.78))
                sides.addArc(center: p(0.5, 0.78), radius: 0.28 * s, startAngle: .degrees(0), endAngle: .degrees(180), clockwise: false)
                context.stroke(sides, with: line, lineWidth: width)
            case 3:
                poly([p(0.5, 0.12), p(0.2, 0.76), p(0.8, 0.76)], fill: true)
                context.stroke(Path(ellipseIn: CGRect(x: o.x + 0.2 * s, y: o.y + 0.68 * s, width: 0.6 * s, height: 0.16 * s)), with: line, lineWidth: width)
            case 4:
                poly([p(0.5, 0.12), p(0.16, 0.72), p(0.56, 0.86)], fill: true)
                poly([p(0.5, 0.12), p(0.56, 0.86), p(0.86, 0.66)])
            case 5:
                var quarter = Path()
                quarter.addArc(center: p(0.5, 0.5), radius: 0.3 * s, startAngle: .degrees(180), endAngle: .degrees(270), clockwise: false)
                context.stroke(quarter, with: .color(AureaColors.accent.opacity(0.75)), lineWidth: 0.2 * s)
                context.stroke(Path(ellipseIn: CGRect(x: o.x + 0.1 * s, y: o.y + 0.1 * s, width: 0.8 * s, height: 0.8 * s)), with: line, lineWidth: width)
                context.stroke(Path(ellipseIn: CGRect(x: o.x + 0.3 * s, y: o.y + 0.3 * s, width: 0.4 * s, height: 0.4 * s)), with: line, lineWidth: width)
            case 6:
                let pts = (0..<10).map { k -> CGPoint in
                    let r: CGFloat = k % 2 == 0 ? 0.42 : 0.18
                    let a = -CGFloat.pi / 2 + CGFloat(k) * .pi / 5
                    return p(0.5 + r * cos(a), 0.54 + r * sin(a))
                }
                poly([pts[0], pts[1], pts[9]], fill: true)
                poly(pts)
            case 7:
                var half = Path()
                half.move(to: p(0.5, 0.3))
                half.addCurve(to: p(0.12, 0.4), control1: p(0.5, 0.1), control2: p(0.1, 0.12))
                half.addCurve(to: p(0.5, 0.86), control1: p(0.14, 0.6), control2: p(0.4, 0.72))
                half.closeSubpath()
                context.fill(half, with: face)
                var full = Path()
                full.move(to: p(0.5, 0.3))
                full.addCurve(to: p(0.12, 0.4), control1: p(0.5, 0.1), control2: p(0.1, 0.12))
                full.addCurve(to: p(0.5, 0.86), control1: p(0.14, 0.6), control2: p(0.4, 0.72))
                full.addCurve(to: p(0.88, 0.4), control1: p(0.6, 0.72), control2: p(0.86, 0.6))
                full.addCurve(to: p(0.5, 0.3), control1: p(0.9, 0.12), control2: p(0.5, 0.1))
                context.stroke(full, with: line, lineWidth: width)
            case 8:
                var cap = Path()
                cap.addArc(center: p(0.5, 0.3), radius: 0.2 * s, startAngle: .degrees(180), endAngle: .degrees(360), clockwise: false)
                cap.closeSubpath()
                context.fill(cap, with: face)
                context.stroke(Path(roundedRect: CGRect(x: o.x + 0.3 * s, y: o.y + 0.1 * s, width: 0.4 * s, height: 0.8 * s), cornerRadius: 0.2 * s),
                               with: line, lineWidth: width)
                var bands = Path()
                bands.move(to: p(0.3, 0.3)); bands.addLine(to: p(0.7, 0.3))
                bands.move(to: p(0.3, 0.7)); bands.addLine(to: p(0.7, 0.7))
                context.stroke(bands, with: line, lineWidth: s * 0.04)
            default:
                poly([p(0.5, 0.1), p(0.22, 0.46), p(0.5, 0.56)], fill: true)
                poly([p(0.5, 0.1), p(0.78, 0.46), p(0.5, 0.9), p(0.22, 0.46)])
                var facets = Path()
                facets.move(to: p(0.22, 0.46)); facets.addLine(to: p(0.5, 0.56)); facets.addLine(to: p(0.78, 0.46))
                facets.move(to: p(0.5, 0.1)); facets.addLine(to: p(0.5, 0.9))
                context.stroke(facets, with: line, lineWidth: s * 0.04)
            }
        }
    }
}

/// Pontos das partes + dedo, por cima do preview, só no modo de parte.
@MainActor struct Shape3DPartOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var state = Shape3DState.shared
    @ObservedObject private var viewZoom = StageViewZoom.shared
    @State private var started = false
    @State private var moved = false
    @State private var last = CGPoint.zero
    @State private var pinchStart: [Float]?

    var body: some View {
        if state.active(model) {
            GeometryReader { geo in
                let placed = StageZoomMath.fit(size: geo.size,
                                               composition: CGSize(width: CGFloat(model.compositionWidth), height: CGFloat(model.compositionHeight)),
                                               zoom: viewZoom.zoom, pan: viewZoom.pan)
                Canvas { context, _ in
                    _ = state.revision
                    _ = model.status.playhead
                    draw(&context, placed.scale, placed.origin)
                }
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { v in changed(v, placed.scale, placed.origin) }
                    .onEnded { v in ended(v, placed.scale, placed.origin) })
                .simultaneousGesture(MagnificationGesture().simultaneously(with: RotationGesture())
                    .onChanged { v in pinch(scale: v.first ?? 1, angle: v.second ?? .zero) }
                    .onEnded { _ in endPinch() })
            }
        }
    }

    private func parts() -> [Float] { model.engine.shape3DParts(state.layer).map(\.floatValue) }

    private func draw(_ context: inout GraphicsContext, _ fit: CGFloat, _ origin: CGPoint) {
        let p = parts(), f = Shape3DState.partFloats, selected = state.part
        for i in 0..<(p.count / f) where p[i * f + 13] > 0.5 {
            let c = CGPoint(x: origin.x + CGFloat(p[i * f + 11]) * fit, y: origin.y + CGFloat(p[i * f + 12]) * fit)
            let r: CGFloat = i == selected ? 7 : 4.5
            dot(&context, c, r + 1.5, StageInk.outlineUnder)
            dot(&context, c, r, i == selected ? AureaColors.accent : .white)
            if p[i * f + 10] > 0.5 {   // keyframe no cabeçote
                context.stroke(Path(ellipseIn: CGRect(x: c.x - r - 4, y: c.y - r - 4, width: 2 * (r + 4), height: 2 * (r + 4))),
                               with: .color(AureaColors.accent), lineWidth: 2)
            }
        }
    }
    private func dot(_ context: inout GraphicsContext, _ c: CGPoint, _ r: CGFloat, _ color: Color) {
        context.fill(Path(ellipseIn: CGRect(x: c.x - r, y: c.y - r, width: 2 * r, height: 2 * r)), with: .color(color))
    }

    private func pick(_ point: CGPoint, _ fit: CGFloat, _ origin: CGPoint) -> Int {
        let p = parts(), f = Shape3DState.partFloats
        var best: CGFloat = 36, found = -1
        for i in 0..<(p.count / f) where p[i * f + 13] > 0.5 {
            let c = CGPoint(x: origin.x + CGFloat(p[i * f + 11]) * fit, y: origin.y + CGFloat(p[i * f + 12]) * fit)
            let d = hypot(point.x - c.x, point.y - c.y)
            if d < best { best = d; found = i }
        }
        return found
    }

    private func changed(_ v: DragGesture.Value, _ fit: CGFloat, _ origin: CGPoint) {
        if !started { started = true; moved = false; last = v.startLocation }
        guard pinchStart == nil else { return }
        if !moved && hypot(v.translation.width, v.translation.height) < 8 { return }
        if !moved { moved = true; model.beginShapeGesture("mover parte") }
        guard fit > 0 else { return }
        let dx = Float((v.location.x - last.x) / fit), dy = Float((v.location.y - last.y) / fit)
        last = v.location
        if dx == 0 && dy == 0 { return }
        if model.status.playing != 0 { model.playPause() }
        model.shapePartDragScreen(state.layer, part: state.part, dx: dx, dy: dy)
    }

    private func ended(_ v: DragGesture.Value, _ fit: CGFloat, _ origin: CGPoint) {
        defer { started = false }
        if moved {
            moved = false
            if pinchStart == nil { model.endShapeGesture() }
            return
        }
        guard pinchStart == nil, fit > 0 else { return }
        // Toque: outra parte = escolhe; vazio = volta à forma inteira.
        state.choose(model, pick(v.startLocation, fit, origin))
        model.refreshModel(force: true)
    }

    private func pinch(scale: CGFloat, angle: Angle) {
        if pinchStart == nil {
            if moved { model.endShapeGesture(); moved = false }
            guard let v = model.shapePartValues(state.layer, part: state.part) else { return }
            pinchStart = Array(v.prefix(9))
            model.beginShapeGesture("pinça da parte")
        }
        guard let start = pinchStart else { return }
        var out = start
        for i in 6...8 {
            let s = start[i] * Float(scale)
            out[i] = abs(s) < 0.01 ? 0.01 : min(100, max(-100, s))
        }
        // Horário na tela = giro negativo em Z do modelo (Z aponta para quem olha).
        out[5] = start[5] - Float(angle.degrees)
        model.setShapePart(state.layer, part: state.part, values: out, mask: (0b111 << 6) | (1 << 5), inGesture: true)
    }
    private func endPinch() {
        guard pinchStart != nil else { return }
        pinchStart = nil
        model.endShapeGesture()
    }
}

/// Seção "Partes" do painel 3D (Shape3DSection.kt).
struct Shape3DPanelSection: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var state = Shape3DState.shared
    let layerId: Int64
    @State private var picking = false
    @State private var pendingColor: DispatchWorkItem?
    @State private var gestureOpen = false

    var body: some View {
        let _ = model.status.modelRevision
        let _ = model.localPlayhead
        let _ = state.revision
        if let info = model.shape3DInfo(layerId) {
            let part = state.part(model)
            VStack(alignment: .leading, spacing: 0) {
                Text(AureaText.t(info.kind < Shape3DState.names.count ? Shape3DState.names[info.kind] : "shape3d_title") + " · " + AureaText.t("shape3d_parts"))
                    .font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted).padding(.bottom, 6)
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 6) {
                        chip(AureaText.t("shape3d_whole"), on: part < 0) { choose(-1) }
                        ForEach(0..<info.colors.count, id: \.self) { i in
                            chip(Shape3DState.partLabel(kind: info.kind, part: i), on: part == i, dot: AureaColorSpace.color(info.colors[i])) {
                                choose(part == i ? -1 : i)
                            }.accessibilityIdentifier("shape3d.part.\(i)")
                        }
                    }
                }
                let shown = part >= 0 && part < info.colors.count ? info.colors[part] : (info.colors.first ?? [1, 1, 1, 1])
                HStack {
                    Text(AureaText.t("panel_cor")).font(.aurea(size: 13))
                    Spacer()
                    Button { openColor(part, shown) } label: {
                        AureaColorSwatch(color: AureaColorSpace.color(shown)).frame(width: 30, height: 30).clipShape(RoundedRectangle(cornerRadius: 6))
                    }.buttonStyle(AureaPressStyle()).accessibilityLabel(AureaText.t("panel_cor"))
                }.frame(height: 44)
                let hasImage = part >= 0 ? (part < info.images.count && info.images[part]) : info.images.contains(true)
                HStack(spacing: 6) {
                    Text(AureaText.t("shape3d_image")).font(.aurea(size: 13))
                    Spacer()
                    chip(AureaText.t(hasImage ? "shape3d_image_change" : "shape3d_image_pick"), on: hasImage) { picking = true }
                        .accessibilityIdentifier("shape3d.image")
                    if hasImage {
                        chip(AureaText.t("shape3d_image_clear"), on: false) {
                            _ = model.engine.setShape3DPartStyle(layerId, part: Int32(part), color: nil, image: "")
                            state.revision += 1
                            model.refreshModel(force: true)
                        }
                    }
                }.frame(height: 44)
                if part >= 0, let v = model.shapePartValues(layerId, part: part) {
                    partControls(part, v)
                } else {
                    hint("shape3d_whole_hint")
                }
            }
            .padding(.bottom, 14)
            .sheet(isPresented: $picking) {
                ShellMediaPicker(filter: .images) { url, _ in
                    picking = false
                    if let url { model.setShapePartImage(layerId, part: state.part(model), url: url) }
                }
            }
        }
    }

    @ViewBuilder private func partControls(_ part: Int, _ v: [Float]) -> some View {
        let animated = UInt32(v[9]), here = UInt32(v[10])
        HStack(spacing: 6) {
            chip((here != 0 ? "◆ " : "◇ ") + AureaText.t(here != 0 ? "panel_tirar_keyframe_daqui" : "panel_marcar_keyframe_aqui"), on: here != 0) {
                if model.engine.toggleShape3DPartKey(layerId, part: Int32(part)) < 0 { model.toast = AureaText.t("shape3d_key_outside") }
                state.revision += 1
                model.refreshModel(force: true)
            }.accessibilityIdentifier("shape3d.key")
            chip(AureaText.t("shape3d_reset"), on: false) {
                _ = model.engine.resetShape3DPart(layerId, part: Int32(part))
                state.revision += 1
                model.refreshModel(force: true)
            }
        }.padding(.vertical, 4)
        ForEach(0..<3, id: \.self) { axis in
            ruler("\(AureaText.t("fx_posicao")) \(["X", "Y", "Z"][axis])", value: v[axis], step: 0.005, min: -5, max: 5,
                  shown: String(format: "%.2f", v[axis]), animated: animated & (1 << UInt32(axis)) != 0) { value in
                var out = [Float](repeating: 0, count: 9); out[axis] = value
                model.setShapePart(layerId, part: part, values: out, mask: 1 << UInt32(axis), inGesture: true)
            }
        }
        ForEach(0..<3, id: \.self) { axis in
            let c = 3 + axis
            ruler("\(AureaText.t("panel_rotacao")) \(["X", "Y", "Z"][axis])", value: v[c], step: 1, min: -3600, max: 3600,
                  shown: "\(Int(v[c].rounded()))°", animated: animated & (1 << UInt32(c)) != 0) { value in
                var out = [Float](repeating: 0, count: 9); out[c] = value
                model.setShapePart(layerId, part: part, values: out, mask: 1 << UInt32(c), inGesture: true)
            }
        }
        ruler(AureaText.t("panel_escala"), value: v[6], step: 0.005, min: 0.01, max: 20,
              shown: "\(Int((v[6] * 100).rounded()))%", animated: animated & (0b111 << 6) != 0) { value in
            model.setShapePart(layerId, part: part, values: (0..<9).map { $0 >= 6 ? value : 0 }, mask: 0b111 << 6, inGesture: true)
        }
        hint("shape3d_part_hint")
    }

    private func choose(_ p: Int) {
        state.choose(model, p)
        model.refreshModel(force: true)
    }

    private func openColor(_ part: Int, _ initial: [Float]) {
        let target = layerId
        model.beginGesture("cor da parte")
        gestureOpen = true
        model.colorSheet = ColorSheetRequest(title: AureaText.t("ds_cor"), initial: initial, withAlpha: false, onChange: { r, g, b, _ in
            // Arrasto na roda: a malha acompanha em passos curtos (90 ms), como no Android.
            pendingColor?.cancel()
            let work = DispatchWorkItem {
                _ = model.engine.setShape3DPartStyle(target, part: Int32(part), color: [r, g, b, 1].map { NSNumber(value: $0) }, image: nil)
                state.revision += 1
            }
            pendingColor = work
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.09, execute: work)
        }, onDone: {
            if let work = pendingColor { work.cancel(); work.perform(); pendingColor = nil }
            if gestureOpen { gestureOpen = false; model.endGesture() }
        })
    }

    private func hint(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).padding(.top, 6)
    }

    private func chip(_ label: String, on: Bool, dot: Color? = nil, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 6) {
                if let dot { Circle().fill(dot).frame(width: 10, height: 10) }
                Text(label).font(.aurea(size: 12)).foregroundStyle(on ? AureaColors.accent : AureaColors.text)
            }
            .padding(.horizontal, 12).padding(.vertical, 6)
            .frame(minHeight: 44)
            .background(on ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
            .contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle())
    }

    private func ruler(_ label: String, value: Float, step: Float, min: Float, max: Float, shown: String, animated: Bool,
                       onValue: @escaping (Float) -> Void) -> some View {
        PropertyCustomRow(label, selected: false, onSelect: {}) {
            HStack(spacing: 8) {
                TickRuler(value: { value }, unitsPerDp: step, active: true)
                    .frame(maxWidth: .infinity)
                    .valueDrag(enabled: true, start: { value }, unitsPerDp: { step }, min: min, max: max,
                               onStart: { model.beginShapeGesture("parte da forma 3D") }, onValue: onValue,
                               onEnd: { model.endShapeGesture() })
                ValueBox(shown, enabled: true, tint: animated ? AureaColors.accent : AureaColors.text)
            }
        }
    }
}
