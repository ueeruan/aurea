import SwiftUI

func preferredCurveTrack(_ tracks: [[KeyframeItem]]) -> [KeyframeItem] {
    tracks.first(where: { track in track.count >= 2 && track.contains { $0.value != track[0].value } })
        ?? tracks.first(where: { $0.count >= 2 })
        ?? tracks.first(where: { !$0.isEmpty }) ?? []
}
import UIKit

private var curveGreen: Color { AureaColors.accent }
private var curvePanelFill: Color { AureaColors.editorPanel }
private var curveRailFill: Color { AureaColors.editorPanelHigh }

// CurvePanel.kt: easing descriptions for drawing and editing the core's keys.
// These samples draw the control; project evaluation remains in the C++ core.
struct CurveEase: Equatable {
    var interpolation: UInt32
    var x1: Float
    var y1: Float
    var x2: Float
    var y2: Float
    static let linear = CurveEase(interpolation: 1, x1: 0, y1: 0, x2: 1, y2: 1)
    var isBezier: Bool { interpolation == 2 || interpolation == 6 }
    var hasHandles: Bool { isBezier || interpolation == 1 || interpolation == 3 || interpolation == 4 || interpolation == 5 }
    var handles: [Float] {
        switch interpolation {
        case 1: return [0, 0, 1, 1]
        case 3: return [1 / 3, 0, 2 / 3, 1 / 3]
        case 4: return [1 / 3, 2 / 3, 2 / 3, 1]
        case 5: return [0.5, 0, 0.5, 1]
        default: return [x1, y1, x2, y2]
        }
    }
    func transform(_ t: Float) -> Float {
        switch interpolation {
        case 0: return t < 1 ? 0 : 1
        case 1: return t
        case 3: return t * t
        case 4: return 1 - (1 - t) * (1 - t)
        case 5: return t < 0.5 ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t)
        case 7:
            let u = min(1, max(0,t))
            if u < 0.5 { return 4*u*u }
            let segment: (Float,Float,Float) = u < 0.75 ? (0.5,0.25,0.25) : u < 0.9 ? (0.75,0.15,0.0625) : (0.9,0.1,0.015625)
            let p = (u-segment.0)/segment.1
            return 1-4*segment.2*p*(1-p)
        case 8:
            if t <= 0 { return 0 }; if t >= 1 { return 1 }
            return Float((1-exp(-6*Double(t))*cos(6*Double.pi*Double(t)))/(1-exp(-6)))
        case 9: return floor(min(1,max(0,t))*4)/4
        default:
            if t <= 0 { return 0 }
            if t >= 1 { return 1 }
            func bezier(_ p: Float, _ q: Float, _ m: Double) -> Double {
                3 * Double(p) * (1 - m) * (1 - m) * m + 3 * Double(q) * (1 - m) * m * m + m * m * m
            }
            var low = 0.0, high = 1.0, parameter = Double(t)
            for _ in 0..<28 {
                let error = bezier(x1, x2, parameter) - Double(t)
                if error == 0 { return Float(bezier(y1, y2, parameter)) }
                if error < 0 { low = parameter } else { high = parameter }
                let m = 1 - parameter
                let slope = 3 * m * m * Double(x1) + 6 * m * parameter * (Double(x2) - Double(x1)) + 3 * parameter * parameter * (1 - Double(x2))
                var next = (low + high) * 0.5
                if abs(slope) > 1e-12 {
                    let candidate = parameter - error / slope
                    if candidate > low && candidate < high { next = candidate }
                }
                if abs(next - parameter) < 1e-9 { return Float(bezier(y1, y2, next)) }
                parameter = next
            }
            return Float(bezier(y1, y2, parameter))
        }
    }
    func same(_ other: CurveEase) -> Bool {
        interpolation == other.interpolation && (!isBezier ||
            (abs(x1 - other.x1) < 0.01 && abs(y1 - other.y1) < 0.01 && abs(x2 - other.x2) < 0.01 && abs(y2 - other.y2) < 0.01))
    }
    var inverted: CurveEase? {
        switch interpolation {
        case 3: var result = self; result.interpolation = 4; return result
        case 4: var result = self; result.interpolation = 3; return result
        case 2, 6:
            let result = CurveEase(interpolation: 2, x1: 1 - x2, y1: 1 - y2, x2: 1 - x1, y2: 1 - y1)
            if abs(result.x1 - x1) < 0.01 && abs(result.y1 - y1) < 0.01 && abs(result.x2 - x2) < 0.01 && abs(result.y2 - y2) < 0.01 { return nil }
            return result
        default: return nil
        }
    }
    var name: String {
        if let preset = CurvePresetItem.builtins.first(where: { same($0.ease) }) { return preset.name }
        let key: String
        switch interpolation {
        case 0: key = "pn_ease_hold"
        case 3: key = "pn_ease_in"
        case 4: key = "pn_ease_out"
        case 5: key = "pn_ease_in_out"
        case 7: key = "pn_textpreset_bounce"
        case 8: key = "pn_textpreset_elastic"
        case 9: key = "pn_curve_steps4"
        default: key = "pn_ease_bezier_custom"
        }
        return AureaText.t(key)
    }
}

private struct CurvePresetItem: Identifiable {
    let id: String
    let name: String
    let ease: CurveEase
    var stored = false
    static var builtins: [CurvePresetItem] {
        [CurvePresetItem(id: "linear", name: AureaText.t("panel_linear"), ease: .linear),
         CurvePresetItem(id: "in", name: AureaText.t("pn_ease_in"), ease: CurveEase(interpolation: 2, x1: 0.42, y1: 0, x2: 1, y2: 1)),
         CurvePresetItem(id: "out", name: AureaText.t("pn_ease_out"), ease: CurveEase(interpolation: 2, x1: 0, y1: 0, x2: 0.58, y2: 1)),
         CurvePresetItem(id: "inout", name: AureaText.t("pn_ease_in_out"), ease: CurveEase(interpolation: 2, x1: 0.42, y1: 0, x2: 0.58, y2: 1)),
         CurvePresetItem(id: "hold", name: AureaText.t("pn_ease_hold"), ease: CurveEase(interpolation: 0, x1: 0, y1: 0, x2: 1, y2: 1))]
    }
    static func read(_ object: [String: Any], id: String) -> CurvePresetItem? {
        guard object["kind"] as? String == "curve", let curve = object["curve"] as? [String: NSNumber],
              let interpolation = curve["interp"]?.uint32Value, interpolation <= 9 else { return nil }
        let h: [Float] = [curve["x1"]?.floatValue ?? 0.33, curve["y1"]?.floatValue ?? 0,
                 curve["x2"]?.floatValue ?? 0.67, curve["y2"]?.floatValue ?? 1]
        guard h.allSatisfy({ $0.isFinite }) else { return nil }
        return CurvePresetItem(id: id, name: object["name"] as? String ?? AureaText.t("panel_curva"),
            ease: CurveEase(interpolation: interpolation, x1: h[0], y1: h[1], x2: h[2], y2: h[3]), stored: true)
    }
}

@MainActor
private enum CurveClipboard { static var ease: CurveEase? }

func curveSameGroup(_ a: KeyframeItem, _ b: KeyframeItem) -> Bool {
    if a.property == 31 || b.property == 31 {
        return a.property == b.property && a.effectIndex == b.effectIndex && a.paramIndex / 4 == b.paramIndex / 4
    }
    if a.property == 35 && b.property == 35 {
        return a.paramIndex == b.paramIndex || ((5...6).contains(a.paramIndex) && (5...6).contains(b.paramIndex))
    }
    if a.property > 31 || b.property > 31 {
        return a.property == b.property && a.effectIndex == b.effectIndex && a.paramIndex == b.paramIndex
    }
    func group(_ property: UInt32) -> UInt32 {
        switch property {
        case 0...2: return 0
        case 3...5: return 1
        case 6...8: return 2
        case 9...11: return 3
        case 13...14: return 4
        default: return 100 + property
        }
    }
    return group(a.property) == group(b.property)
}

private struct NativeCurveGraph: View {
    let ease: CurveEase
    let overshoot: Bool
    let progress: Float?
    let onBegin: () -> Void
    let onChange: (CurveEase) -> Void
    let onEnd: () -> Void
    @State private var drag: HandleDrag?
    @GestureState private var touching = false
    private struct HandleDrag {
        var first: Bool
        var low: Float
        var high: Float
        var ease: CurveEase
        var grabOffset: CGPoint
        var began = false
    }
    private var low: Float { drag?.low ?? min(overshoot ? -0.5 : -0.12, min(ease.handles[1], ease.handles[3]) - 0.12) }
    private var high: Float { drag?.high ?? max(overshoot || ease.interpolation == 8 ? 1.5 : 1.12, max(ease.handles[1], ease.handles[3]) + 0.12) }
    var body: some View {
        GeometryReader { geometry in
            ZStack {
                Canvas { context, size in draw(context, size) }
                Canvas { context, size in
                    if let progress {
                        let p = plot(progress, ease.transform(progress), size, low, high)
                        var line = Path(); line.move(to: CGPoint(x: p.x, y: 0)); line.addLine(to: CGPoint(x: p.x, y: size.height))
                        context.stroke(line, with: .color(.white.opacity(0.4)), style: StrokeStyle(lineWidth: 1, dash: [2, 4]))
                    }
                }
            }.contentShape(Rectangle()).gesture(DragGesture(minimumDistance: 0)
                .updating($touching) { _, active, _ in active = true }
                .onChanged { value in move(value, size: geometry.size) }
                .onEnded { _ in finish() })
        }
        .onChange(of: touching) { active in if !active { finish() } }
        .onDisappear { finish() }
    }
    private func plot(_ x: Float, _ y: Float, _ size: CGSize, _ lo: Float, _ hi: Float) -> CGPoint {
        CGPoint(x: 24 + CGFloat(x) * max(1, size.width - 48), y: size.height - CGFloat((y - lo) / (hi - lo)) * size.height)
    }
    private func draw(_ context: GraphicsContext, _ size: CGSize) {
        func point(_ x: Float, _ y: Float) -> CGPoint { plot(x, y, size, low, high) }
        func line(_ a: CGPoint, _ b: CGPoint, _ color: Color, _ width: CGFloat, _ dash: [CGFloat] = []) {
            var path = Path(); path.move(to: a); path.addLine(to: b)
            context.stroke(path, with: .color(color), style: StrokeStyle(lineWidth: width, dash: dash))
        }
        func dot(_ p: CGPoint, radius: CGFloat, color: Color) {
            context.fill(Path(ellipseIn: CGRect(x: p.x - radius, y: p.y - radius, width: radius * 2, height: radius * 2)), with: .color(color))
        }
        for index in 1..<32 {
            let x = size.width * CGFloat(index) / 32, y = size.height * CGFloat(index) / 32
            line(CGPoint(x: x, y: 0), CGPoint(x: x, y: size.height), .white.opacity(0.045), 0.6)
            line(CGPoint(x: 0, y: y), CGPoint(x: size.width, y: y), .white.opacity(0.045), 0.6)
        }
        for y in [Float(0), Float(1)] { line(point(0, y), point(1, y), .white.opacity(0.24), 1, [4.5, 4.5]) }
        let base = point(0, 0).y
        var curve = Path(), area = Path(); area.move(to: CGPoint(x: 0, y: base))
        for index in 0...72 {
            let t = Float(index) / 72, p = point(t, ease.transform(t))
            if index == 0 { curve.move(to: p) } else { curve.addLine(to: p) }
            area.addLine(to: p)
        }
        area.addLine(to: CGPoint(x: size.width, y: base)); area.closeSubpath()
        context.fill(area, with: .color(curveGreen.opacity(0.025)))
        context.stroke(curve, with: .color(curveGreen), style: StrokeStyle(lineWidth: 3.5, lineCap: .round))
        let start = point(0, 0), end = point(1, 1)
        if ease.hasHandles {
            let h = ease.handles, first = point(h[0], h[1]), second = point(h[2], h[3])
            for p in [first, second] {
                line(CGPoint(x: p.x, y: min(p.y, base)), CGPoint(x: p.x, y: max(p.y, base)), .white.opacity(0.3), 1, [3, 3])
            }
            line(start, first, .white, 2.5); line(end, second, .white, 2.5)
            dot(first, radius: 11, color: .white); dot(second, radius: 11, color: .white)
        }
        dot(start, radius: 4.5, color: curveGreen); dot(end, radius: 4.5, color: curveGreen)
    }
    private func move(_ value: DragGesture.Value, size: CGSize) {
        guard size.width > 0, size.height > 0 else { return }
        if drag == nil {
            guard ease.hasHandles else { return }
            let h = ease.handles
            let a = plot(h[0], h[1], size, low, high), b = plot(h[2], h[3], size, low, high)
            func distance(_ p: CGPoint) -> CGFloat {
                let dx = value.startLocation.x - p.x, dy = value.startLocation.y - p.y
                return dx * dx + dy * dy
            }
            let d1 = distance(a), d2 = distance(b)
            guard min(d1, d2) <= 24 * 24 else { return }
            let first = d1 <= d2, point = first ? a : b
            drag = HandleDrag(first: first, low: low, high: high,
                ease: CurveEase(interpolation: 2, x1: h[0], y1: h[1], x2: h[2], y2: h[3]),
                grabOffset: CGPoint(x: point.x - value.startLocation.x, y: point.y - value.startLocation.y))
        }
        guard var current = drag else { return }
        guard current.began || hypot(value.translation.width, value.translation.height) >= 3 else { return }
        let location = CGPoint(x: value.location.x + current.grabOffset.x, y: value.location.y + current.grabOffset.y)
        let x = Float((location.x - 24) / max(1, size.width - 48)).clamped(to: 0...1)
        var y = current.low + Float((size.height - location.y) / size.height) * (current.high - current.low)
        if !overshoot { y = y.clamped(to: 0...1) }
        guard x.isFinite, y.isFinite else { return }
        if !current.began { current.began = true; onBegin() }
        if current.first { current.ease.x1 = x; current.ease.y1 = y } else { current.ease.x2 = x; current.ease.y2 = y }
        drag = current; onChange(current.ease)
    }
    private func finish() {
        let began = drag?.began == true; drag = nil
        if began { onEnd() }
    }
}

private struct CurvePresetThumb: View {
    let ease: CurveEase
    let selected: Bool
    var body: some View {
        Canvas { context, size in
            let pad: CGFloat = 6, width = size.width - 12, height = size.height - 12
            var path = Path()
            for index in 0...40 {
                let t = Float(index) / 40, value = ease.transform(t).clamped(to: -0.3...1.3)
                let p = CGPoint(x: pad + CGFloat(t) * width, y: pad + height - CGFloat(value) * height)
                if index == 0 { path.move(to: p) } else { path.addLine(to: p) }
            }
            let color = selected ? curveGreen : Color.white.opacity(0.7)
            context.stroke(path, with: .color(color), style: StrokeStyle(lineWidth: 2, lineCap: .round))
            for p in [CGPoint(x: pad, y: pad + height), CGPoint(x: pad + width, y: pad)] {
                context.fill(Path(ellipseIn: CGRect(x: p.x - 2.5, y: p.y - 2.5, width: 5, height: 5)),
                    with: .color(selected ? curveGreen : .white))
            }
        }
    }
}

@MainActor
struct NativeCurvePanel: View {
    @EnvironmentObject private var model: AureaModel
    @Environment(\.dismiss) private var dismissExpanded
    var expanded = false
    @State private var fullscreen = false
    @State private var ease = CurveEase.linear
    @State private var overshoot = false
    @State private var family = 0
    @State private var graphMode = 0
    @State private var saved: [CurvePresetItem] = []
    private struct Segment {
        let start: KeyframeItem
        let end: KeyframeItem
        let index: Int
        var id: String { start.id }
    }
    private var layer: Int64 { model.primarySelection ?? 0 }
    private var track: [KeyframeItem] {
        (model.keyframes[layer] ?? []).filter {
            $0.property == model.curveProperty && (model.curveProperty < 31 ||
                ($0.effectIndex == model.curveEffect && $0.paramIndex == model.curveParam))
        }.sorted { $0.time < $1.time }
    }
    private var segment: Segment? {
        let keys = track
        guard keys.count >= 2, let selected = model.curveSelectedTime,
              let selectedIndex = keys.firstIndex(where: { $0.time == selected }) else { return nil }
        let index = min(selectedIndex, keys.count - 2)
        return Segment(start: keys[index], end: keys[index + 1], index: index)
    }
    private func progress(_ segment: Segment) -> Float? {
        let frame = model.localPlayhead
        guard frame >= segment.start.time, frame < segment.end.time, segment.end.time > segment.start.time else { return nil }
        return Float(Int64(frame) - Int64(segment.start.time)) / Float(Int64(segment.end.time) - Int64(segment.start.time))
    }
    var body: some View {
        VStack(spacing: 0) {
            if let segment {
                HStack(spacing: 0) {
                    ForEach(0..<3, id: \.self) { index in
                        Button { graphMode = index } label: {
                            Text(AureaText.t(["panel_easing_curve", "particular_curve_value", "panel_velocidade"][index]))
                                .font(.aurea(size: 12)).lineLimit(1)
                                .foregroundStyle(graphMode == index ? AureaColors.accent : AureaColors.muted)
                                .frame(maxWidth: .infinity).frame(height: 48).contentShape(Rectangle())
                        }.buttonStyle(.plain).accessibilityIdentifier("curve.mode.\(index)")
                    }
                }.background(curveRailFill)
                HStack(spacing: 0) {
                    leftRail(segment)
                    VStack(spacing: 0) {
                        if graphMode == 0 {
                            NativeCurveGraph(ease: ease, overshoot: overshoot, progress: progress(segment),
                                onBegin: { model.beginGesture("curva") },
                                onChange: { apply($0, to: segment.start); model.refreshModel(force: true) },
                                onEnd: { model.endGesture() })
                                .id("\(layer):\(segment.id)")
                        } else {
                            NativeTrackGraph(layer: layer, keys: track, speed: graphMode == 2)
                        }
                        segmentNavigation(segment)
                    }.frame(maxWidth: .infinity, maxHeight: .infinity)
                    if graphMode == 0 { families }
                }.frame(maxHeight: .infinity)
            } else {
                VStack(spacing: 12) {
                    Text(AureaText.t(model.curveSelectedTime == nil ? "panel_toque_num_keyframe_timeline_npara_editar" : "panel_crie_pelo_menos_2_keyframes_npara"))
                        .font(.aurea(size: 13)).foregroundStyle(AureaColors.muted).multilineTextAlignment(.center)
                    Button(action: back) {
                        Text(AureaText.t("panel_voltar")).font(.aurea(size: 15)).foregroundStyle(AureaColors.accent)
                            .padding(.horizontal, 16).padding(.vertical, 8)
                    }.buttonStyle(AureaPressStyle(shrink: 1))
                }.frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }.background(curvePanelFill).accessibilityElement(children: .contain).accessibilityIdentifier("curve.panel")
        .overlay {
            if expanded {
                ZStack {
                    if let request = model.actionSheet {
                        AureaActionSheet(title: request.title, actions: request.actions) { model.actionSheet = nil }
                    }
                    if let request = model.namePrompt {
                        AureaNamePrompt(title: request.title, initial: request.initial, onConfirm: request.onConfirm,
                            onDismiss: { model.namePrompt = nil }).id(request.id)
                    }
                }
            }
        }
        .fullScreenCover(isPresented: $fullscreen) { NativeCurvePanel(expanded: true).environmentObject(model).interactiveDismissDisabled() }
        .onAppear { load(); family = [7,8].contains(ease.interpolation) ? 1 : [0,9].contains(ease.interpolation) ? 2 : 0; loadPresets() }
        .onChange(of: model.status.modelRevision) { _ in load() }
        .onChange(of: segment?.id) { _ in load() }
        .onChange(of: layer) { _ in load() }
    }
    private func back() { if expanded { dismissExpanded() } else { model.openPanel(model.curveReturnPanel == .curve ? .none : model.curveReturnPanel) } }
    private func load() {
        guard let key = segment?.start else { return }
        let h = model.engine.trackEasing(layer, property: key.property, effect: key.effectIndex, param: key.paramIndex, time: key.time).map(\.floatValue)
        ease = CurveEase(interpolation: key.interpolation, x1: h.count == 4 ? h[0] : 0.33,
            y1: h.count == 4 ? h[1] : 0, x2: h.count == 4 ? h[2] : 0.67, y2: h.count == 4 ? h[3] : 1)
    }
    private func apply(_ value: CurveEase, to key: KeyframeItem) {
        // Android's sameGroup: all axes/components with a mark at this instant.
        for sibling in model.keyframes[layer] ?? [] where sibling.time == key.time && curveSameGroup(sibling, key) {
            model.engine.editTrackKey(layer, property: sibling.property, effect: sibling.effectIndex, param: sibling.paramIndex,
                time: sibling.time, action: 3, value: sibling.value, targetTime: sibling.time,
                interpolation: value.interpolation, handles: [value.x1, value.y1, value.x2, value.y2].map { NSNumber(value: $0) })
        }
        ease = value
    }
    private func set(_ value: CurveEase, to key: KeyframeItem, label: String = "curva") {
        model.beginGesture(label); apply(value, to: key); model.endGesture()
    }
    private func leftRail(_ segment: Segment) -> some View {
        VStack(spacing: 0) {
            Spacer().frame(height: 6)
            glyphButton(CupertinoGlyph.ChevronBack, size: 24, target: 44, label: AureaText.t("panel_voltar"), action: back)
            Spacer(minLength: 0)
            glyphButton(CupertinoGlyph.ArrowRightArrowLeft, size: 20, target: 44, label: AureaText.t("panel_inverter_curva")) {
                if let inverted = ease.inverted { set(inverted, to: segment.start, label: "inverter curva") }
                else { model.toast = AureaText.t("pn_curve_symmetric") }
            }.disabled(!(1...6).contains(ease.interpolation))
                .opacity((1...6).contains(ease.interpolation) ? 1 : 0.35)
            Spacer().frame(height: 4)
            RailMoreButton(active: overshoot) { showMenu(segment) }
            Spacer().frame(height: 8)
        }.frame(width: 44).frame(maxHeight: .infinity).background(curveRailFill)
    }
    private func glyphButton(_ glyph: Character, size: CGFloat, target: CGFloat, label: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            CupertinoGlyph.text(glyph, size: size, color: .white)
                .frame(width: target, height: target).contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityLabel(label)
    }
    private func segmentNavigation(_ segment: Segment) -> some View {
        HStack(spacing: 4) {
            glyphButton(CupertinoGlyph.ChevronLeft, size: 16, target: 44, label: AureaText.t("panel_keyframe_anterior")) { jump(-1) }
            Text(graphMode == 0 ? ((ease.interpolation == 0 || (7...9).contains(ease.interpolation)) ? ease.name : "Cubic Bezier Easing")
                 : AureaText.t(graphMode == 1 ? "particular_curve_value" : "panel_velocidade"))
                .font(.aurea(size: 10)).foregroundStyle(.white.opacity(0.6))
                .lineLimit(1).truncationMode(.tail).multilineTextAlignment(.center)
            glyphButton(CupertinoGlyph.ChevronRight, size: 16, target: 44, label: AureaText.t("panel_proximo_keyframe")) { jump(1) }
        }.frame(maxWidth: .infinity).frame(height: 44)
    }
    private func jump(_ direction: Int) {
        guard let segment else { return }
        let keys = track, target = (segment.index + direction).clamped(to: 0...max(0, track.count - 2))
        guard target != segment.index, target + 1 < keys.count else { return }
        model.curveSelectedTime = keys[target].time
        if model.status.playing != 0 { model.playPause() }
        let local = Int64(keys[target].time) + (Int64(keys[target + 1].time) - Int64(keys[target].time)) / 2
        model.seek(toFrame: local + Int64(model.selectedLayer?.startFrame ?? 0) - Int64(model.selectedLayer?.offsetFrames ?? 0))
        load()
    }
    private var presets: [CurvePresetItem] {
        switch family {
        case 1: return [CurvePresetItem(id: "bounce", name: AureaText.t("pn_textpreset_bounce"), ease: CurveEase(interpolation: 7,x1: 0,y1: 0,x2: 1,y2: 1)),
                        CurvePresetItem(id: "elastic", name: AureaText.t("pn_textpreset_elastic"), ease: CurveEase(interpolation: 8,x1: 0,y1: 0,x2: 1,y2: 1))]
        case 2: return [CurvePresetItem(id: "steps4", name: AureaText.t("pn_curve_steps4"), ease: CurveEase(interpolation: 9,x1: 0,y1: 0,x2: 1,y2: 1))] + Array(CurvePresetItem.builtins.suffix(1))
        case 3: return saved
        default: return Array(CurvePresetItem.builtins.prefix(4))
        }
    }
    private var families: some View {
        HStack(spacing: 0) {
            GeometryReader { geometry in
                ScrollView {
                    LazyVStack(spacing: 2) {
                        ForEach(presets) { preset in presetTile(preset) }
                    }.padding(.horizontal, 2).padding(.vertical, 6)
                        .frame(minHeight: geometry.size.height, alignment: .center)
                }
            }.frame(width: 44)
            VStack(spacing: 0) {
                Spacer(minLength: 0)
                familyTab(0, glyph: CupertinoGlyph.Scribble, title: "pn_curve_family_bezier")
                familyTab(1, glyph: CupertinoGlyph.Scribble, title: "pn_textpreset_bounce")
                familyTab(2, glyph: CupertinoGlyph.ChartBarAltFill, title: "pn_curve_steps4")
                familyTab(3, glyph: CupertinoGlyph.Star, title: "panel_presets")
                Spacer(minLength: 0)
            }.frame(width: 44).background(curveRailFill)
        }.frame(width: 88).frame(maxHeight: .infinity)
    }
    private func presetTile(_ preset: CurvePresetItem) -> some View {
        Button {
            guard let key = segment?.start else { return }
            set(preset.ease, to: key)
            if preset.stored {
                var recents = UserDefaults.standard.stringArray(forKey: "presetRecents") ?? []
                recents.removeAll { $0 == preset.id }; recents.insert(preset.id, at: 0)
                UserDefaults.standard.set(Array(recents.prefix(10)), forKey: "presetRecents")
            }
        } label: {
            CurvePresetThumb(ease: preset.ease, selected: ease.same(preset.ease))
                .frame(width: 44, height: 44)
                .background(curveRailFill, in: RoundedRectangle(cornerRadius: 8))
                .overlay(RoundedRectangle(cornerRadius: 8).stroke(ease.same(preset.ease) ? curveGreen : .clear,
                    lineWidth: ease.same(preset.ease) ? 1.8 : 1))
                .clipShape(RoundedRectangle(cornerRadius: 8))
        }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityLabel(preset.name)
    }
    private func familyTab(_ index: Int, glyph: Character, title: String) -> some View {
        Button { family = index } label: {
            Group {
                if index == 3 { CupertinoGlyph.text(glyph, size: 20, color: family == index ? curveGreen : .white) }
                else { CurvePresetThumb(ease: index == 0 ? CurveEase(interpolation: 2, x1: 0.5, y1: 0, x2: 0.5, y2: 1) : CurveEase(interpolation: index == 1 ? 7 : 9, x1: 0, y1: 0, x2: 1, y2: 1), selected: family == index) }
            }.frame(width: 44, height: 44).contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityLabel(AureaText.t(title))
    }
    private func showMenu(_ segment: Segment) {
        let value = ease
        let copied = CurveClipboard.ease
        let actions: [SheetAction] = [
            SheetAction(AureaText.t("panel_curva")) { graphMode = 0 },
            SheetAction(AureaText.t("particular_curve_value")) { graphMode = 1 },
            SheetAction(AureaText.t("panel_velocidade")) { graphMode = 2 },
            SheetAction(AureaText.t(expanded ? "editor_sair_tela_cheia" : "panel_expandir")) { if expanded { dismissExpanded() } else { fullscreen = true } },
            SheetAction(AureaText.t("panel_copiar_curva")) { CurveClipboard.ease = value },
            SheetAction(AureaText.t("panel_salvar_curva_como_preset")) {
                model.namePrompt = NamePromptRequest(title: AureaText.t("panel_salvar_curva_como_preset"), initial: value.name) { savePreset(name: $0, ease: value) }
            },
            SheetAction(AureaText.t("panel_colar_curva"), enabled: copied != nil) {
                if let copied { set(copied, to: segment.start, label: "colar curva") }
            },
            SheetAction(AureaText.t("panel_aplicar_todos_segmentos")) {
                model.beginGesture("curva em todos")
                for key in track.dropLast() { apply(value, to: key) }
                model.endGesture()
            },
            SheetAction(AureaText.t(overshoot ? "panel_overshoot_9678" : "panel_overshoot")) { overshoot.toggle() }
        ]
        model.actionSheet = ActionSheetRequest(title: AureaText.t("panel_curva"), actions: actions)
    }
    private func loadPresets() {
        var result: [CurvePresetItem] = []
        if let url = Bundle.main.url(forResource: "curva", withExtension: "json", subdirectory: "presets"),
           let data = try? Data(contentsOf: url), let objects = (try? JSONSerialization.jsonObject(with: data)) as? [[String: Any]] {
            for (index, object) in objects.enumerated() {
                if let entry = CurvePresetItem.read(object, id: "b:curva:\(index)") { result.append(entry) }
            }
        }
        let directory = AureaPaths.documents.appendingPathComponent("presets/curva", isDirectory: true)
        let urls = (try? FileManager.default.contentsOfDirectory(at: directory, includingPropertiesForKeys: nil)) ?? []
        for url in urls.sorted(by: { $0.lastPathComponent < $1.lastPathComponent }) where url.pathExtension == "json" {
            if let data = try? Data(contentsOf: url), let object = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any],
               let entry = CurvePresetItem.read(object, id: "user:curva:\(url.lastPathComponent)") { result.append(entry) }
        }
        saved = result
    }
    private func savePreset(name: String, ease: CurveEase) {
        let title = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !title.isEmpty else { return }
        let h = ease.handles
        let object: [String: Any] = ["aurea_preset": 1, "kind": "curve", "name": title,
            "curve": ["interp": NSNumber(value: ease.interpolation), "x1": NSNumber(value: h[0]), "y1": NSNumber(value: h[1]),
                      "x2": NSNumber(value: h[2]), "y2": NSNumber(value: h[3])]]
        do {
            let directory = AureaPaths.documents.appendingPathComponent("presets/curva", isDirectory: true)
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            let safe = title.components(separatedBy: CharacterSet(charactersIn: "/\\:")).joined(separator: "-")
            var url = directory.appendingPathComponent(safe + ".json"), counter = 2
            while FileManager.default.fileExists(atPath: url.path) { url = directory.appendingPathComponent("\(safe) \(counter).json"); counter += 1 }
            // Sem dado nao se grava ARQUIVO VAZIO: um preset ilegivel e pior
            // que nenhum, porque aparece na lista e nao abre. Ver AureaJSON.h.
            guard let data = AureaJSONData(object, false) else { throw CocoaError(.fileWriteInvalidFileName) }
            try data.write(to: url, options: .atomic)
            loadPresets()
        } catch { model.toast = error.localizedDescription }
    }
}

/// Entry point for older curve panels; the actual editor is the shared sheet.
struct ExpressionEditor: View {
    @EnvironmentObject private var model: AureaModel
    var body: some View {
        Button {
            guard let layer = model.primarySelection else { return }
            model.expressionSheet = ExpressionRequest(layer: layer, label: AureaText.t("panel_expressao"),
                tracks: [ExpressionTrack(property: model.curveProperty, effect: model.curveEffect, param: model.curveParam)])
        } label: {
            HStack(spacing: 8) {
                Text("=").font(.aurea(size: 22, weight: .bold))
                Text(AureaText.t("panel_expressao")).font(.aurea(size: 14, weight: .semibold))
                Spacer()
            }.foregroundStyle(AureaColors.accent).frame(height: 44)
        }.buttonStyle(AureaPressStyle(shrink: 1)).padding(.horizontal, 14)
    }
}

struct TimeRemapEffectEditor: View {
    @EnvironmentObject private var model: AureaModel
    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    ForEach(Array(["panel_linear", "panel_suave", "panel_lento_meio", "panel_acelerar", "panel_desacelerar", "panel_congelar", "panel_passar_tras_frente"].enumerated()), id: \.offset) { index, label in
                        Button {
                            guard let id = model.primarySelection else { return }
                            model.engine.applySpeedRamp(UInt32(index), forLayer: id)
                            model.refreshModel(force: true)
                        } label: {
                            Text(AureaText.t(label)).font(.aurea(size: 12))
                                .padding(.horizontal, 10).padding(.vertical, 6)
                                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                        }.buttonStyle(.plain)
                    }
                }
            }
            TimeRemapEditor()
        }
    }
}

// TimeRemapGraph.kt: source frame vertically, layer-local time horizontally.
struct TimeRemapEditor: View {
    @EnvironmentObject private var model: AureaModel
    var expanded = false
    var expandedHeight: CGFloat = 240
    @State private var fullscreen = false
    @State private var viewport: TrackGraphViewport?
    @State private var panInitial: TrackGraphViewport?
    @State private var grabOffset = CGPoint.zero
    @State private var values: [Float] = []
    @State private var selected = -1
    @State private var editingPoint: Int?
    private var id: Int64 { model.primarySelection ?? 0 }
    private var points: [[Float]] {
        guard values.count >= 5 else { return [] }
        let count = min(max(0, Int(values[0])), (values.count - 5) / 7)
        return (0..<count).map { index in Array(values[(5 + index * 7)..<(12 + index * 7)]) }
    }
    private var fitLow: Float { values.count >= 5 ? values[1] : 0 }
    private var low: Float { viewport.map { Float($0.from) } ?? fitLow }
    private var fitHigh: Float { values.count >= 5 ? max(values[2], fitLow + 1) : 1 }
    private var high: Float { viewport.map { Float($0.to) } ?? fitHigh }
    private var fitSourceMax: Float {
        if values.count >= 5 && values[3] > 0 { return values[3] }
        return max(1, (points.map { $0[1] }.max() ?? 0) * 1.2)
    }
    private var sourceLow: Float { viewport.map { Float($0.low) } ?? 0 }
    private var sourceMax: Float { viewport.map { Float($0.high) } ?? fitSourceMax }
    private var view: TrackGraphViewport { viewport ?? TrackGraphViewport(from: Double(fitLow), to: Double(fitHigh), low: 0, high: Double(fitSourceMax)) }
    private var speedLabel: String {
        let speed = values.count >= 5 ? values[4] : 0
        if abs(speed) < 0.005 { return AureaText.t("panel_congelado_cabecote") }
        let number = String(format: "%.2f", abs(speed))
        return "Velocidade no cabeçote: \(number)×" + (speed < 0 ? " ao contrário" : "")
    }
    private func load() {
        values = model.engine.timeRemap(id).map(\.floatValue)
        if !points.indices.contains(selected) { selected = -1 }
    }
    private func refresh() { model.refreshModel(force: true); load() }
    private func finish() {
        guard editingPoint != nil else { return }
        editingPoint = nil
        model.engine.endUndoGroup()
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            if !points.isEmpty {
                HStack {
                    Button("−") { viewport = view.transformed(zoom: 1/1.5) }.frame(width: 44, height: 44)
                    Button("+") { viewport = view.transformed(zoom: 1.5) }.frame(width: 44, height: 44)
                    Button(AureaText.t("panel_ajustar")) { viewport = nil }.frame(minWidth: 44, minHeight: 44)
                    Spacer()
                    if !expanded { Button(AureaText.t("panel_expandir")) { fullscreen = true }.frame(minHeight: 44) }
                }
                GeometryReader { geometry in
                    ZStack {
                        Canvas { context, size in draw(context, size: size) }
                        TimeRemapTouchSurface(
                            hit: { hit($0, size: geometry.size) },
                            onTap: { location in tap(location, size: geometry.size) },
                            onLongPress: { location in remove(location, size: geometry.size) },
                            onBegin: { index, location in
                                finish(); selected = index; editingPoint = index
                                let key = position(points[index], size: geometry.size)
                                grabOffset = CGPoint(x: key.x-location.x, y: key.y-location.y)
                                model.engine.beginUndoGroup()
                            },
                            onMove: { index, location in move(index, location: location, size: geometry.size) },
                            onEnd: finish,
                            onPan: { translation, ended in
                                if panInitial == nil { panInitial = view }
                                if let initial = panInitial {
                                    viewport = initial.transformed(zoom: 1, dx: Double(translation.x / max(1, geometry.size.width - 48)), dy: Double(translation.y / max(1, geometry.size.height - 48)))
                                }
                                if ended { panInitial = nil }
                            })
                    }
                }.frame(height: expanded ? expandedHeight : 240).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
                    .clipShape(RoundedRectangle(cornerRadius: 10))
                Text(speedLabel).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                    .frame(maxWidth: .infinity, alignment: .leading).padding(.top, 6)
                if points.indices.contains(selected) {
                    HStack(spacing: 6) {
                        Text("Ponto \(selected + 1)").font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                        interpolationChip(1, "panel_linear")
                        interpolationChip(5, "panel_suave")
                        interpolationChip(0, "panel_congelar")
                    }.padding(.top, 6)
                }
                Text(AureaText.t("panel_toque_curva_criar_ponto_arraste_mudar"))
                    .font(.aurea(size: 11)).foregroundStyle(AureaColors.muted)
                    .fixedSize(horizontal: false, vertical: true).padding(.top, 4)
            }
        }.frame(maxWidth: .infinity, alignment: .leading)
            .onAppear(perform: load)
            .onChange(of: model.status.modelRevision) { _ in load() }
            .onChange(of: model.status.playhead) { _ in load() }
            .onChange(of: id) { _ in finish(); selected = -1; load() }
            .onDisappear(perform: finish)
            .fullScreenCover(isPresented: $fullscreen) {
                GeometryReader { bounds in
                    VStack {
                        Button(AureaText.t("editor_sair_tela_cheia")) { fullscreen = false }.frame(minHeight: 44)
                        TimeRemapEditor(expanded: true, expandedHeight: max(96, bounds.size.height - 220)).environmentObject(model)
                        Spacer(minLength: 0)
                    }.padding(16).background(AureaColors.background)
                }.interactiveDismissDisabled()
            }
    }
    private func interpolationChip(_ interpolation: Int32, _ label: String) -> some View {
        let active = points.indices.contains(selected) && Int32(points[selected][2]) == interpolation
        return Button {
            guard points.indices.contains(selected) else { return }
            let point = points[selected]
            _ = model.engine.editTimeRemap(id, index: Int32(selected), time: Int64(point[0]), value: point[1], interpolation: interpolation)
            refresh()
        } label: {
            Text(AureaText.t(label)).font(.aurea(size: 12)).foregroundStyle(active ? AureaColors.accent : AureaColors.text)
                .padding(.horizontal, 10).padding(.vertical, 6)
                .background(active ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
        }.buttonStyle(AureaPressStyle())
    }
    private func position(_ point: [Float], size: CGSize) -> CGPoint {
        CGPoint(x: 24 + CGFloat((point[0] - low) / (high - low)) * max(1, size.width - 48),
                y: size.height - 24 - CGFloat((point[1] - sourceLow) / max(0.0001, sourceMax-sourceLow)) * max(1, size.height - 48))
    }
    private func frame(at x: CGFloat, size: CGSize) -> Int64 {
        let value = low + Float((x - 24) / max(1, size.width - 48)) * (high - low)
        return Int64(floor(Double(value) + 0.5))
    }
    private func hit(_ location: CGPoint, size: CGSize) -> Int? {
        var nearest: Int?, distance: CGFloat = 28
        for (index, point) in points.enumerated() {
            let p = position(point, size: size), d = hypot(p.x - location.x, p.y - location.y)
            if d < distance { nearest = index; distance = d }
        }
        return nearest
    }
    private func tap(_ location: CGPoint, size: CGSize) {
        if let index = hit(location, size: size) { selected = index; return }
        // index=-1 samples the existing C++ curve: inserting alone changes no motion.
        selected = Int(model.engine.editTimeRemap(id, index: -1, time: frame(at: location.x, size: size), value: 0, interpolation: -1))
        refresh()
    }
    private func remove(_ location: CGPoint, size: CGSize) {
        guard points.count > 2, let index = hit(location, size: size) else { return }
        model.engine.removeTimeRemap(id, index: UInt32(index))
        selected = -1; refresh()
    }
    private func move(_ index: Int, location: CGPoint, size: CGSize) {
        guard let index = editingPoint, points.indices.contains(index) else { return }
        let location = CGPoint(x: location.x+grabOffset.x, y: location.y+grabOffset.y)
        let value = max(0, sourceLow + Float((size.height - 24 - location.y) / max(1, size.height - 48)) * (sourceMax-sourceLow))
        let moved = model.engine.editTimeRemap(id, index: Int32(index), time: frame(at: location.x, size: size), value: value, interpolation: -1)
        if moved >= 0 { selected = Int(moved); editingPoint = Int(moved) }
        refresh()
    }
    private func draw(_ context: GraphicsContext, size: CGSize) {
        let pad: CGFloat = 24, width = max(1, size.width - 48), height = max(1, size.height - 48)
        func line(_ start: CGPoint, _ end: CGPoint, _ color: Color, _ stroke: CGFloat) {
            var path = Path(); path.move(to: start); path.addLine(to: end)
            context.stroke(path, with: .color(color), lineWidth: stroke)
        }
        for index in 1...3 {
            let x = pad + width * CGFloat(index) / 4, y = pad + height * CGFloat(index) / 4
            line(CGPoint(x: x, y: pad), CGPoint(x: x, y: size.height - pad), .white.opacity(0.06), 1)
            line(CGPoint(x: pad, y: y), CGPoint(x: size.width - pad, y: y), .white.opacity(0.06), 1)
        }
        let keys = points
        if let first = keys.first {
            var path = Path(); path.move(to: position(first, size: size))
            for index in 0..<max(0, keys.count - 1) {
                let a = keys[index], b = keys[index + 1]
                let ease = CurveEase(interpolation: UInt32(a[2]), x1: a[3], y1: a[4], x2: a[5], y2: a[6])
                for sample in 1...32 {
                    let u = Float(sample) / 32
                    path.addLine(to: position([a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * ease.transform(u)], size: size))
                }
            }
            context.stroke(path, with: .color(AureaColors.accent), lineWidth: 2.5)
        }
        let playhead = Float(model.localPlayhead)
        if playhead >= low && playhead <= high {
            let x = position([playhead, 0], size: size).x
            line(CGPoint(x: x, y: pad * 0.5), CGPoint(x: x, y: size.height - pad * 0.5), Color(red: 1, green: 90 / 255, blue: 90 / 255), 1.5)
        }
        for (index, point) in keys.enumerated() {
            let center = position(point, size: size)
            context.fill(Path(ellipseIn: CGRect(x: center.x - 8, y: center.y - 8, width: 16, height: 16)), with: .color(.black.opacity(0.5)))
            context.fill(Path(ellipseIn: CGRect(x: center.x - 6, y: center.y - 6, width: 12, height: 12)), with: .color(index == selected ? .white : AureaColors.accent))
        }
    }
}

/// The graph owns drags inside its bounds: keys take priority, empty space pans. Scroll outside the graph remains available.
private struct TimeRemapTouchSurface: UIViewRepresentable {
    var hit: (CGPoint) -> Int?
    var onTap: (CGPoint) -> Void
    var onLongPress: (CGPoint) -> Void
    var onBegin: (Int, CGPoint) -> Void
    var onMove: (Int, CGPoint) -> Void
    var onEnd: () -> Void
    var onPan: (CGPoint, Bool) -> Void
    func makeCoordinator() -> Coordinator { Coordinator(self) }
    func makeUIView(context: Context) -> UIView {
        let view = UIView(); view.backgroundColor = .clear
        let pan = UIPanGestureRecognizer(target: context.coordinator, action: #selector(Coordinator.pan(_:)))
        pan.maximumNumberOfTouches = 1; pan.delegate = context.coordinator
        let hold = UILongPressGestureRecognizer(target: context.coordinator, action: #selector(Coordinator.hold(_:)))
        hold.minimumPressDuration = 0.5
        let tap = UITapGestureRecognizer(target: context.coordinator, action: #selector(Coordinator.tap(_:)))
        tap.require(toFail: hold); tap.require(toFail: pan)
        view.addGestureRecognizer(pan); view.addGestureRecognizer(hold); view.addGestureRecognizer(tap)
        return view
    }
    func updateUIView(_ uiView: UIView, context: Context) { context.coordinator.owner = self }
    static func dismantleUIView(_ uiView: UIView, coordinator: Coordinator) { coordinator.finish() }
    final class Coordinator: NSObject, UIGestureRecognizerDelegate {
        var owner: TimeRemapTouchSurface
        private var index: Int?
        init(_ owner: TimeRemapTouchSurface) { self.owner = owner }
        func finish() {
            guard index != nil else { return }
            index = nil; owner.onEnd()
        }
        func gestureRecognizerShouldBegin(_ gestureRecognizer: UIGestureRecognizer) -> Bool {
            return true
        }
        @objc func tap(_ recognizer: UITapGestureRecognizer) {
            guard recognizer.state == .ended, let view = recognizer.view else { return }
            owner.onTap(recognizer.location(in: view))
        }
        @objc func hold(_ recognizer: UILongPressGestureRecognizer) {
            guard recognizer.state == .began, let view = recognizer.view else { return }
            owner.onLongPress(recognizer.location(in: view))
        }
        private func panTranslation(_ recognizer: UIPanGestureRecognizer, _ view: UIView) -> CGPoint { recognizer.translation(in: view) }
        @objc func pan(_ recognizer: UIPanGestureRecognizer) {
            guard let view = recognizer.view else { finish(); return }
            let location = recognizer.location(in: view)
            switch recognizer.state {
            case .began:
                let translation = recognizer.translation(in: view)
                index = owner.hit(CGPoint(x: location.x - translation.x, y: location.y - translation.y))
                if let index { owner.onBegin(index, CGPoint(x: location.x-translation.x, y: location.y-translation.y)) }
            case .changed:
                if let index { owner.onMove(index, location) } else { owner.onPan(panTranslation(recognizer, view), false) }
            case .ended, .cancelled, .failed:
                if index == nil { owner.onPan(panTranslation(recognizer, view), true) }; finish()
            default: break
            }
        }
    }
}

// ExpressionSheet.kt: shared expression target, including vector properties.
struct ExpressionTrack: Equatable {
    let property: UInt32
    let effect: UInt32
    let param: UInt32
    init(property: UInt32, effect: UInt32 = .max, param: UInt32 = 0) {
        self.property = property; self.effect = effect; self.param = param
    }
}
struct ExpressionRequest: Identifiable {
    let id = UUID()
    let layer: Int64
    let label: String
    let tracks: [ExpressionTrack]
    let scale: Float
    let unit: String
    init(layer: Int64, label: String, tracks: [ExpressionTrack], scale: Float = 1, unit: String = "") {
        self.layer = layer; self.label = label; self.tracks = tracks; self.scale = scale; self.unit = unit
    }
    var packedTracks: [NSNumber] { tracks.flatMap { [NSNumber(value: $0.property), NSNumber(value: $0.effect), NSNumber(value: $0.param)] } }
}
private struct ExpressionDiagnostic {
    var ok = true
    var message = ""
    var line = 0
    var column = 0
    init() {}
    init(_ info: [String: Any]) {
        ok = (info["ok"] as? NSNumber)?.boolValue ?? true
        message = info["message"] as? String ?? ""
        line = (info["line"] as? NSNumber)?.intValue ?? 0
        column = (info["column"] as? NSNumber)?.intValue ?? 0
    }
}

@MainActor
struct ExpressionSheet: View {
    @EnvironmentObject private var model: AureaModel
    let request: ExpressionRequest
    let onDismiss: () -> Void
    @State private var source = ""
    @State private var selection = NSRange(location: 0, length: 0)
    @State private var info: [String: Any] = [:]
    @State private var syntax = ExpressionDiagnostic()
    @State private var values: [Float] = []
    @State private var refused = false
    @State private var loaded = false
    @State private var codeScroll: CGFloat = 0
    private let snippets = ["wiggle(2, 30)", "loopOut(\"cycle\")", "time * 90", "value", "loopOut(\"pingpong\")", "linear(time, 0, 1, 0, 100)", "effect(\"Slider Control\")(\"Slider\")", "thisComp.layer(1).transform.position", "random(0, 100)"]
    private var applied: Bool { (info["exists"] as? NSNumber)?.boolValue == true }
    private var enabled: Bool { (info["enabled"] as? NSNumber)?.boolValue == true }
    private var dirty: Bool { source != (info["source"] as? String ?? "") }
    private var shownError: ExpressionDiagnostic? {
        if !syntax.ok { return syntax }
        if !dirty && applied, let error = info["error"] as? String, !error.isEmpty {
            var diagnostic = ExpressionDiagnostic(); diagnostic.ok = false; diagnostic.message = error
            diagnostic.line = (info["line"] as? NSNumber)?.intValue ?? 0
            diagnostic.column = (info["column"] as? NSNumber)?.intValue ?? 0
            return diagnostic
        }
        return nil
    }
    private var status: (String, Color) {
        if refused { return (AureaText.t("panel_motor_recusou_esta_propriedade"), AureaColors.danger) }
        if let error = shownError {
            let message = error.line > 0 ? AureaText.t("pn_expr_error_at", error.line, error.column, error.message) : error.message
            return (message + (!dirty && applied && syntax.ok ? AureaText.t("panel_usando_valor_keyframes") : ""), AureaColors.danger)
        }
        if dirty { return (AureaText.t(source.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty ? "panel_aplicar_sem_texto_remove_expressao" : "panel_sintaxe_ok_toque_aplicar"), AureaColors.muted) }
        if applied && !enabled { return (AureaText.t("panel_desligada_propriedade_usa_keyframes"), AureaColors.muted) }
        if applied { return (AureaText.t("panel_resultado_agora") + values.map { numeroPtBr($0, casas: 2) + request.unit }.joined(separator: " · "), AureaColors.keyframe) }
        return (AureaText.t("panel_escreva_expressao_ou_toque_num_atalho"), AureaColors.muted)
    }
    var body: some View {
        AureaBottomOverlay(modal: true, onDismiss: onDismiss) {
            VStack(alignment: .leading, spacing: 0) {
                HStack(spacing: 8) {
                    VStack(alignment: .leading, spacing: 0) {
                        Text(AureaText.t("panel_expressao")).font(.aurea(size: 17, weight: .bold))
                        Text(request.label).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                    }.frame(maxWidth: .infinity, alignment: .leading)
                    if applied {
                        Text(AureaText.t(enabled ? "panel_ligada" : "panel_desligada")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                        AureaToggle(checked: enabled) { on in
                            _ = model.engine.enableExpressions(request.layer, tracks: request.packedTracks, enabled: on)
                            model.refreshModel(force: true); reread()
                        }
                    }
                }.padding(.bottom, 12)
                codeEditor
                Text(status.0).font(.aurea(size: 12)).foregroundStyle(status.1).fixedSize(horizontal: false, vertical: true).padding(.top, 8)
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 6) {
                        ForEach(snippets, id: \.self) { snippet in
                            Button { insert(snippet) } label: {
                                Text(snippet).font(.aurea(size: 12, design: .monospaced)).foregroundStyle(AureaColors.keyframe)
                                    .padding(.horizontal, 10).padding(.vertical, 7).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                            }.buttonStyle(AureaPressStyle())
                        }
                    }
                }.padding(.top, 10)
                HStack(spacing: 8) {
                    if applied { sheetButton(AureaText.t("panel_remover"), color: AureaColors.danger) { apply(""); source = ""; selection = NSRange(location: 0, length: 0) } }
                    sheetButton(AureaText.t("panel_fechar"), color: AureaColors.text, action: onDismiss)
                    sheetButton(AureaText.t("panel_aplicar"), color: AureaColors.accent, enabled: dirty) { apply(source) }
                }.padding(.top, 14).padding(.bottom, 12)
            }.padding(.horizontal, 16).foregroundStyle(AureaColors.text)
        }
        .onAppear {
            reread(); source = info["source"] as? String ?? ""; selection = NSRange(location: (source as NSString).length, length: 0); loaded = true
        }
        .onChange(of: model.status.playhead) { _ in reread() }
        .onChange(of: model.status.modelRevision) { _ in reread() }
        .task(id: source) {
            guard loaded else { return }
            do { try await Task.sleep(nanoseconds: 250_000_000) } catch { return }
            guard !Task.isCancelled else { return }
            syntax = source.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty ? ExpressionDiagnostic() : ExpressionDiagnostic(model.engine.checkExpressionSyntax(source))
        }
    }
    private var codeEditor: some View {
        let lineCount = source.filter { $0 == "\n" }.count + 1
        let height = min(220, max(120, CGFloat(lineCount) * 20 + 20))
        return HStack(spacing: 0) {
            Canvas { context, size in
                for line in 1...lineCount {
                    let y = CGFloat(line - 1) * 20 + 10 - codeScroll
                    guard y >= -20 && y <= size.height else { continue }
                    let error = shownError?.line == line
                    context.draw(Text(String(line)).font(.aurea(size: 14, weight: error ? .bold : .regular, design: .monospaced)).foregroundColor(error ? AureaColors.danger : AureaColors.muted.opacity(0.6)), at: CGPoint(x: 28, y: y), anchor: .topTrailing)
                }
            }.frame(width: 34)
            ExpressionTextInput(text: $source, selection: $selection, scroll: $codeScroll).padding(.trailing, 10)
        }.frame(height: height).background(Color(hex: 0x0B1016), in: RoundedRectangle(cornerRadius: 10)).clipped()
    }
    private func sheetButton(_ label: String, color: Color, enabled: Bool = true, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(label).font(.aurea(size: 15, weight: .semibold)).foregroundStyle(enabled ? color : AureaColors.disabled)
                .frame(maxWidth: .infinity).frame(height: 44).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 12))
        }.buttonStyle(AureaPressStyle()).disabled(!enabled)
    }
    private func reread() {
        guard let first = request.tracks.first else { info = [:]; values = []; return }
        info = model.engine.expression(request.layer, property: first.property, effect: first.effect, param: first.param)
        values = request.tracks.map { track in
            let value = model.engine.expression(request.layer, property: track.property, effect: track.effect, param: track.param)
            return ((value["value"] as? NSNumber)?.floatValue ?? 0) * request.scale
        }
    }
    private func apply(_ text: String) {
        let result = model.engine.setExpressions(request.layer, tracks: request.packedTracks, source: text)
        refused = (result["accepted"] as? NSNumber)?.boolValue != true
        if !refused { syntax = ExpressionDiagnostic(result) }
        model.refreshModel(force: true); reread()
    }
    private func insert(_ snippet: String) {
        let text = source as NSString
        let start = min(selection.location, text.length), count = min(selection.length, text.length - min(selection.location, text.length))
        source = text.replacingCharacters(in: NSRange(location: start, length: count), with: snippet)
        selection = NSRange(location: start + (snippet as NSString).length, length: 0)
    }
}

@MainActor
private struct ExpressionTextInput: UIViewRepresentable {
    @Binding var text: String
    @Binding var selection: NSRange
    @Binding var scroll: CGFloat
    func makeCoordinator() -> Coordinator { Coordinator(self) }
    func makeUIView(context: Context) -> UITextView {
        let view = UITextView(); view.delegate = context.coordinator
        view.backgroundColor = .clear; view.textColor = UIColor(AureaColors.text); view.tintColor = UIColor(AureaColors.accent)
        view.font = UIFont.monospacedSystemFont(ofSize: 14, weight: .regular)
        view.textContainerInset = UIEdgeInsets(top: 10, left: 0, bottom: 10, right: 0); view.textContainer.lineFragmentPadding = 0
        view.autocorrectionType = .no; view.autocapitalizationType = .none; view.spellCheckingType = .no; view.keyboardType = .asciiCapable
        view.smartQuotesType = .no; view.smartDashesType = .no; view.isScrollEnabled = true
        let paragraph = NSMutableParagraphStyle(); paragraph.minimumLineHeight = 20; paragraph.maximumLineHeight = 20
        view.typingAttributes = [.font: UIFont.monospacedSystemFont(ofSize: 14, weight: .regular), .foregroundColor: UIColor(AureaColors.text), .paragraphStyle: paragraph]
        return view
    }
    func updateUIView(_ view: UITextView, context: Context) {
        context.coordinator.parent = self
        if view.text != text {
            let paragraph = NSMutableParagraphStyle(); paragraph.minimumLineHeight = 20; paragraph.maximumLineHeight = 20
            view.attributedText = NSAttributedString(string: text, attributes: [.font: UIFont.monospacedSystemFont(ofSize: 14, weight: .regular), .foregroundColor: UIColor(AureaColors.text), .paragraphStyle: paragraph])
        }
        let length = (text as NSString).length
        let range = NSRange(location: min(selection.location, length), length: min(selection.length, length - min(selection.location, length)))
        if view.selectedRange != range { view.selectedRange = range }
    }
    final class Coordinator: NSObject, UITextViewDelegate {
        var parent: ExpressionTextInput
        init(_ parent: ExpressionTextInput) { self.parent = parent }
        func textViewDidChange(_ textView: UITextView) { parent.text = textView.text; parent.selection = textView.selectedRange }
        func textViewDidChangeSelection(_ textView: UITextView) { if parent.selection != textView.selectedRange { parent.selection = textView.selectedRange } }
        func scrollViewDidScroll(_ scrollView: UIScrollView) { parent.scroll = scrollView.contentOffset.y }
    }
}

// Value/speed graph uses the core track evaluator, not the easing thumbnail.
private struct TrackGraphViewport: Equatable {
    var from: Double, to: Double, low: Double, high: Double
    var duration: Double { max(1, to - from) }
    var range: Double { max(0.0001, high - low) }
    func transformed(zoom: Double, dx: Double = 0, dy: Double = 0) -> TrackGraphViewport {
        let duration = min(1e9, max(1, self.duration / min(4, max(0.25, zoom))))
        let range = min(1e12, max(0.0001, self.range / min(4, max(0.25, zoom))))
        let from = min(1e9 - duration, max(-1e9, self.from + (self.duration - duration) * 0.5 - dx * self.duration))
        let low = self.low + (self.range - range) * 0.5 + dy * self.range
        return TrackGraphViewport(from: from, to: from + duration, low: low, high: low + range)
    }
}

@MainActor
private struct NativeTrackGraph: View {
    @EnvironmentObject private var model: AureaModel
    let layer: Int64
    let keys: [KeyframeItem]
    let speed: Bool
    @State private var viewport = TrackGraphViewport(from: 0, to: 100, low: 0, high: 1)
    @State private var samples: [CGPoint] = []
    @State private var drag: GraphDrag?
    @GestureState private var touching = false
    private struct GraphDrag {
        let initial: TrackGraphViewport
        let key: KeyframeItem?
        var began = false
    }
    private var start: Int32 { Int32(floor(viewport.from)) }
    private var end: Int32 { max(start + 1, Int32(ceil(viewport.to))) }
    private var signature: String {
        guard let first = keys.first else { return "" }
        return "\(layer):\(first.property):\(first.effectIndex):\(first.paramIndex):\(speed)"
    }
    private func read(from: Int32, to: Int32) -> [CGPoint] {
        guard let first = keys.first, to > from else { return [] }
        let values = model.engine.trackCurve(layer, property: first.property, effect: first.effectIndex,
            param: first.paramIndex, from: from, to: to)
        var result: [CGPoint] = []
        for (index, number) in values.enumerated() {
            let frame = Double(Int64(Double(from) + (Double(to) - Double(from)) * Double(index) / Double(max(1, values.count - 1))))
            let value = number.doubleValue
            if value.isFinite && (result.last == nil || result.last!.x != CGFloat(frame)) {
                result.append(CGPoint(x: frame, y: value))
            }
        }
        if !speed { return result }
        let fps = max(1, Double(model.status.compFps))
        return zip(result, result.dropFirst()).compactMap { a, b in
            let velocity = Double(b.y - a.y) * fps / Double(b.x - a.x)
            return velocity.isFinite ? CGPoint(x: (a.x + b.x) * 0.5, y: velocity) : nil
        }
    }
    private func reload() { samples = read(from: start, to: end) }
    private func fit() {
        guard let first = keys.first, let last = keys.last else { return }
        let to = max(first.time + 1, last.time)
        let full = read(from: first.time, to: to)
        let values = full.map { Double($0.y) } + (speed ? [0] : keys.map { Double($0.value) })
        let low = values.min() ?? 0, high = values.max() ?? 1
        let margin = max(0.1, max(high - low, abs(high) * 0.05) * 0.12)
        let timeMargin = max(1, (Double(to) - Double(first.time)) * 0.06)
        viewport = TrackGraphViewport(from: Double(first.time) - timeMargin, to: Double(to) + timeMargin, low: low - margin, high: high + margin)
        reload()
    }
    private func point(_ frame: Double, _ value: Double, _ size: CGSize, _ view: TrackGraphViewport) -> CGPoint {
        CGPoint(x: (frame - view.from) / view.duration * Double(size.width),
            y: Double(size.height) - (value - view.low) / view.range * Double(size.height))
    }
    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 6) {
                Text(String(format: "%.3g%@", viewport.high, speed ? " /s" : ""))
                    .font(.aurea(size: 10)).foregroundStyle(AureaColors.muted)
                Spacer(minLength: 0)
                Button("−") { viewport = viewport.transformed(zoom: 1 / 1.5) }.frame(width: 44, height: 44)
                Button("+") { viewport = viewport.transformed(zoom: 1.5) }.frame(width: 44, height: 44)
                Button(AureaText.t("panel_ajustar")) { fit() }.font(.aurea(size: 11))
            }.foregroundStyle(AureaColors.accent)
            GeometryReader { geometry in
                Canvas { context, size in
                    let view = viewport
                    func plot(_ frame: Double, _ value: Double) -> CGPoint {
                        CGPoint(x: (frame - view.from) / view.duration * Double(size.width),
                            y: Double(size.height) - (value - view.low) / view.range * Double(size.height))
                    }
                    func line(_ a: CGPoint, _ b: CGPoint, color: Color, width: CGFloat = 1) {
                        var path = Path(); path.move(to: a); path.addLine(to: b)
                        context.stroke(path, with: .color(color), lineWidth: width)
                    }
                    for index in 1...3 {
                        let x = size.width * CGFloat(index) / 4, y = size.height * CGFloat(index) / 4
                        line(CGPoint(x: x, y: 0), CGPoint(x: x, y: size.height), color: .white.opacity(0.1))
                        line(CGPoint(x: 0, y: y), CGPoint(x: size.width, y: y), color: .white.opacity(0.1))
                    }
                    let zero = plot(viewport.from, 0).y
                    if zero >= 0 && zero <= size.height { line(CGPoint(x: 0, y: zero), CGPoint(x: size.width, y: zero), color: .white.opacity(0.3)) }
                    var path = Path()
                    for (index, sample) in samples.enumerated() {
                        let p = plot(Double(sample.x), Double(sample.y))
                        if index == 0 { path.move(to: p) } else { path.addLine(to: p) }
                    }
                    context.stroke(path, with: .color(AureaColors.accent), lineWidth: 2)
                    if !speed {
                        for key in keys {
                            let p = plot(Double(key.time), Double(key.value))
                            let dot = Path(ellipseIn: CGRect(x: p.x - 6, y: p.y - 6, width: 12, height: 12))
                            context.fill(dot, with: .color(key.time == model.curveSelectedTime ? .white : AureaColors.accent))
                        }
                    }
                    let x = plot(Double(model.localPlayhead), 0).x
                    if x >= 0 && x <= size.width { line(CGPoint(x: x, y: 0), CGPoint(x: x, y: size.height), color: AureaColors.danger) }
                }.clipped().contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0)
                    .updating($touching) { _, active, _ in active = true }
                    .onChanged { move($0, size: geometry.size) }
                    .onEnded { _ in finish() })
            }
            HStack {
                Text(String(format: "%.3g%@", viewport.low, speed ? " /s" : ""))
                Spacer()
                Text("\(start)–\(end) f")
            }.font(.aurea(size: 10)).foregroundStyle(AureaColors.muted)
        }
        .onAppear { fit() }
        .onChange(of: signature) { _ in finish(); fit() }
        .onChange(of: viewport) { _ in reload() }
        .onChange(of: model.status.modelRevision) { _ in reload() }
        .onChange(of: touching) { active in if !active { finish() } }
        .onDisappear { finish() }
    }
    private func move(_ value: DragGesture.Value, size: CGSize) {
        guard size.width > 0, size.height > 0 else { return }
        if drag == nil {
            var nearest: KeyframeItem?
            var distance = CGFloat(24 * 24)
            if !speed {
                for key in keys {
                    let p = point(Double(key.time), Double(key.value), size, viewport)
                    let dx = p.x - value.startLocation.x, dy = p.y - value.startLocation.y
                    let d = dx * dx + dy * dy
                    if d <= distance { nearest = key; distance = d }
                }
            }
            drag = GraphDrag(initial: viewport, key: nearest)
            if let nearest { model.curveSelectedTime = nearest.time }
        }
        guard var current = drag else { return }
        if let key = current.key {
            guard current.began || abs(value.translation.height) >= 1 else { return }
            let changed = Double(key.value) - Double(value.translation.height / size.height) * current.initial.range
            guard changed.isFinite, abs(changed) <= Double(Float.greatestFiniteMagnitude) else { return }
            if !current.began { model.beginGesture("editar valor do keyframe"); current.began = true; drag = current }
            model.engine.editTrackKey(layer, property: key.property, effect: key.effectIndex, param: key.paramIndex,
                time: key.time, action: 0, value: Float(changed), targetTime: key.time, interpolation: key.interpolation, handles: [])
            model.refreshModel(force: true)
        } else {
            viewport = current.initial.transformed(zoom: 1,
                dx: Double(value.translation.width / size.width), dy: Double(value.translation.height / size.height))
        }
    }
    private func finish() {
        let began = drag?.began == true; drag = nil
        if began { model.endGesture() }
    }
}
