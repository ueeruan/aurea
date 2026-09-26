// Direct port of Android TimelinePainter, TimelineHit and TimelineController.
// The core remains the only owner of layers, animation tracks and media.
import SwiftUI
import UIKit
import Combine

private func markerTint(_ packed: UInt32) -> Color {
    Color(.sRGB, red: Double(packed & 255) / 255, green: Double((packed >> 8) & 255) / 255,
          blue: Double((packed >> 16) & 255) / 255, opacity: 1)
}

struct MarkerEditorSheet: View {
    @EnvironmentObject private var model: AureaModel
    @Environment(\.dismiss) private var dismiss
    let original: Int64
    let isNew: Bool
    @State private var name: String
    @State private var frameText: String
    @State private var color: UInt32
    @State private var failure = false
    private let palette: [UInt32] = [0xFFF7C34F, 0xFF4D6EFF, 0xFF70D56B, 0xFFFFB75B, 0xFFD67BDB, 0xFFFFFFFF]
    init(frame: Int64, color: UInt32, label: String, isNew: Bool = false) {
        original = frame
        self.isNew = isNew
        _name = State(initialValue: label)
        _frameText = State(initialValue: String(frame))
        _color = State(initialValue: color)
    }
    var body: some View {
        NavigationView {
            Form {
                TextField(AureaText.t("new_project_name"), text: $name)
                    .onChange(of: name) { value in if value.count > 200 { name = String(value.prefix(200)) } }
                TextField(AureaText.t("marker_frame"), text: $frameText).keyboardType(.numberPad)
                Text(Timecode.format(Int32(clamping: max(0, Int64(frameText) ?? 0)), Float(model.compositionFps)))
                Button(AureaText.t("marker_at_playhead")) { frameText = String(model.status.playhead) }
                HStack {
                    ForEach(Array(palette.enumerated()), id: \.offset) { index, packed in
                        Button { color = packed } label: {
                            Circle().fill(markerTint(packed)).frame(width: color == packed ? 32 : 22, height: color == packed ? 32 : 22)
                                .frame(width: 40, height: 44)
                        }.buttonStyle(.borderless).accessibilityLabel(AureaText.t("marker_color", index + 1))
                    }
                }
                Text(AureaText.t(failure ? "marker_error" : "marker_hint")).font(.footnote)
                if !isNew {
                    Button(AureaText.t("common_delete"), role: .destructive) { model.deleteMarker(original); dismiss() }
                }
            }
            .navigationTitle(AureaText.t("marker_edit"))
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button(AureaText.t("common_cancel")) { dismiss() } }
                ToolbarItem(placement: .confirmationAction) {
                    Button(AureaText.t("common_save")) {
                        if let target = Int64(frameText), model.editMarker(from: isNew ? -1 : original, to: target, color: color, label: name) { dismiss() }
                        else { failure = true }
                    }
                }
            }
        }
    }
}

@MainActor
struct TimelineView: View {
    @EnvironmentObject private var model: AureaModel
    /// O cabeçote a cada quadro da tela durante o play (ver `PlayheadClock`).
    @EnvironmentObject private var clock: PlayheadClock
    @State private var pps = Zoom.defaultPPS
    @State private var scrollY: CGFloat = 0
    @State private var heldView: Double?
    @State private var gesture: Interaction?
    @State private var pinchPPS = Zoom.defaultPPS
    @State private var pinchFrame = 0.0
    @State private var pinching = false
    @State private var guide = Snap.none
    @State private var selectedKey: (layer: Int64, frame: Int32, track: TimelineTrack?)?
    @State private var reorderSource = -1
    @State private var reorderTarget = -1
    @State private var scrollVelocity: CGFloat = 0
    @State private var lastPointer = CGPoint.zero
    @State private var thumbnails: [Int64: [MediaTile]] = [:]
    @State private var waves: [Int64: TimelineWaveStrip.Entry] = [:]
    @State private var markers: [Marker] = []
    @State private var markersRevision: UInt32 = .max
    @State private var markersFrames: [Int64] = []
    @State private var expandedLayer: Int64?
    @State private var rowCache = TimelineRowCache()
    @State private var thumbCache = TimelineThumbStrip()
    @State private var waveCache = TimelineWaveStrip(capacity: 2048)
    private let m = TimelineMetrics()
    private let pulse = Timer.publish(every: 1.0 / 60, on: .main, in: .common).autoconnect()

    private struct MediaTile { var localFrame: Double; var width: CGFloat; var image: UIImage }
    private struct Marker { var frame: Int32; var packedColor: UInt32; var kind: UInt32 }
    private enum Mode { case scrub, scroll, move, trimStart, trimEnd, key, reorder, hold, blocked }
    private struct Interaction {
        var mode: Mode
        var start: CGPoint
        var view: Double
        var scroll: CGFloat
        var row: TimelineRow?
        var hit: TimelineHit
        var selection: [LayerItem]
        var snapTargets: [Int32]
        var keyIndex: Int = -1
        var keyFrame: Int32 = 0
        var movingKeys: [KeyframeItem] = []
        var keyLimits: (lo: Int32, hi: Int32) = (0, 0)
        var sentDelta: Int32 = 0
        var undoOpen = false
    }
    private var compact: Bool { model.sheetContent == .panel }
    private var fps: Float { TimeAxis.safeFps(Float(model.compositionFps)) }
    private var ppf: CGFloat { TimeAxis.pxPerFrame(pps: pps, density: 1, fps: fps) }
    private var viewFrame: Double { heldView ?? Double(clock.frame) }
    private var focusTracks: [TimelineTrack]? {
        model.panel == .curve ? [TimelineTrack(property: Int(model.curveProperty), effect: model.curveEffect, param: model.curveParam)] : model.timelineFocus
    }
    private var rows: [TimelineRow] {
        let cached = rowCache.build(model.layers, model.keyframes)
        let all = rowCache.focused(cached, id: model.primarySelection, tracks: focusTracks, layers: model.layers, keys: model.keyframes)
        return compact ? all.filter { $0.id == model.primarySelection } : rowCache.expanded(all, id: expandedLayer, revision: model.status.modelRevision, keys: model.keyframes, effects: {
            guard let id = expandedLayer else { return [] }
            return model.engine.effects(forLayer: id).map { row in
                EffectItem(effectId: (row["effectId"] as? NSNumber)?.uint32Value ?? 0,
                    typeId: (row["typeId"] as? NSNumber)?.uint32Value ?? 0,
                    name: row["name"] as? String ?? "",
                    enabled: row["enabled"] as? Bool ?? true,
                    paramCount: (row["paramCount"] as? NSNumber)?.uint32Value ?? 0,
                    known: row["known"] as? Bool ?? true)
            }
        })
    }
    private func x(_ frame: Double, width: CGFloat) -> CGFloat {
        TimeAxis.xOf(frame: frame, view: viewFrame, pxPerFrame: ppf, centerX: width / 2)
    }
    private func frame(_ x: CGFloat, width: CGFloat) -> Double {
        TimeAxis.frameAt(x: x, view: viewFrame, pxPerFrame: ppf, centerX: width / 2)
    }
    private func maxScroll(_ height: CGFloat) -> CGFloat {
        compact ? 0 : max(0, rowTop(rows.count) + m.bottomPad - (height - m.rowsTop))
    }

    var body: some View {
        GeometryReader { geometry in
            let size = geometry.size
            Canvas { context, canvasSize in
                drawRows(&context, size: canvasSize)
                drawRuler(&context, size: canvasSize)
                let tint = compact ? AureaColors.danger : AureaColors.playhead
                context.fill(Path(CGRect(x: canvasSize.width / 2 - m.playhead / 2, y: 0, width: m.playhead, height: canvasSize.height)), with: .color(tint))
                if compact {
                    context.fill(Path(roundedRect: CGRect(x: canvasSize.width / 2 - m.knob / 2, y: 0, width: m.knob, height: m.knob), cornerRadius: m.knobRadius), with: .color(tint))
                }
            }
            .background(AureaColors.stage)
            .overlay {
                TimelineGestureSurface(
                    tap: { tap($0, width: size.width) },
                    pan: { state, start, point, velocity in pan(state, start: start, point: point, velocity: velocity, size: size) },
                    hold: { state, start, point in hold(state, start: start, point: point, size: size) },
                    pinch: { state, scale, focus in pinch(state, scale: scale, focus: focus, width: size.width) }
                )
            }
            .onAppear {
                let seconds = CGFloat(model.compositionDuration) / CGFloat(fps)
                if seconds >= Zoom.autoFitMinSeconds { pps = Zoom.autoFit(availableDp: size.width - 32, seconds: seconds) }
                refreshMedia(size: size)
            }
            .onChange(of: model.status.thumbnailGeneration) { _ in refreshMedia(size: size) }
            .onChange(of: model.status.playhead) { _ in refreshMedia(size: size) }
            .onChange(of: model.status.modelRevision) { _ in refreshMedia(size: size) }
            .onChange(of: scrollY) { _ in refreshMedia(size: size) }
            .onChange(of: pps) { _ in refreshMedia(size: size) }
            .onChange(of: heldView) { _ in refreshMedia(size: size) }
            .onChange(of: size) { _ in scrollY = min(scrollY, maxScroll(size.height)); refreshMedia(size: size) }
            .onChange(of: model.primarySelection) { _ in revealSelection(size: size); refreshMedia(size: size) }
            .onChange(of: model.curveSelectedTime) { time in
                if gesture?.mode != .key, let time, let id = model.primarySelection, let row = rows.first(where: { $0.id == id }) {
                    selectedKey = (id, Keyframes.toTimeline(time, row.start, row.offset),
                        TimelineTrack(property: Int(model.curveProperty), effect: model.curveEffect, param: model.curveParam))
                }
            }
            .onChange(of: compact) { _ in scrollY = 0; refreshMedia(size: size) }
            .onReceive(pulse) { _ in tick(size: size) }
            .onDisappear { finish(cancelled: true); finishPinch() }
            .clipped()
        }
    }

    // MARK: Android painter
    private func drawRuler(_ context: inout GraphicsContext, size: CGSize) {
        context.fill(Path(CGRect(x: 0, y: 0, width: size.width, height: m.rowsTop)), with: .color(AureaColors.stage))
        let steps = RulerSteps.of(pps: pps, fps: fps)
        let t0 = frame(0, width: size.width) / Double(fps)
        let t1 = frame(size.width, width: size.width) / Double(fps)
        var major = Path(), minor = Path()
        if t1 >= 0 {
            let first = max(0, Int(floor(max(0, t0) / steps.majorSeconds)))
            let last = max(first, Int(ceil(t1 / steps.majorSeconds)))
            for k in first...last {
                let seconds = Double(k) * steps.majorSeconds
                let px = x(seconds * Double(fps), width: size.width)
                major.move(to: CGPoint(x: px, y: m.tickMajorTop)); major.addLine(to: CGPoint(x: px, y: m.tickBottom))
                if !steps.frameMinors && steps.subdivisions > 1 {
                    for j in 1..<steps.subdivisions {
                        let sx = x((seconds + Double(j) * steps.majorSeconds / Double(steps.subdivisions)) * Double(fps), width: size.width)
                        if sx >= -1 && sx <= size.width + 1 {
                            minor.move(to: CGPoint(x: sx, y: m.tickMinorTop)); minor.addLine(to: CGPoint(x: sx, y: m.tickBottom))
                        }
                    }
                }
                if steps.labels {
                    let label = Text(Timecode.rulerLabel(timelineFrame(seconds))).font(.aurea(size: 9, weight: .medium)).monospacedDigit().foregroundColor(AureaTimeline.tickMajor)
                    let resolved = context.resolve(label)
                    let lw = resolved.measure(in: CGSize(width: 120, height: 20)).width
                    let lx = px + m.tickLabelGap
                    if lx + lw <= size.width / 2 - m.timecodeZoneHalf || lx >= size.width / 2 + m.timecodeZoneHalf {
                        context.draw(resolved, at: CGPoint(x: lx, y: 0), anchor: .topLeading)
                    }
                }
            }
            if steps.frameMinors {
                let firstFrame = max(0, Int64(ceil(t0 * Double(fps))))
                let lastFrame = Int64(floor(t1 * Double(fps)))
                if lastFrame >= firstFrame {
                    for f in firstFrame...lastFrame {
                        let sec = Double(f) / Double(fps)
                        if abs(sec - (sec / steps.majorSeconds).rounded() * steps.majorSeconds) * Double(fps) >= 0.5 {
                            let px = x(Double(f), width: size.width)
                            minor.move(to: CGPoint(x: px, y: m.tickMinorTop)); minor.addLine(to: CGPoint(x: px, y: m.tickBottom))
                        }
                    }
                }
            }
        }
        context.stroke(minor, with: .color(AureaTimeline.tickMinor), lineWidth: m.tickMinorWidth)
        context.stroke(major, with: .color(AureaTimeline.tickMajor), lineWidth: m.tickMajorWidth)
        for marker in markers {
            let px = x(Double(marker.frame), width: size.width), half = m.tickBottom * 0.28
            guard px >= -half && px <= size.width + half else { continue }
            let s = marker.kind == 1 ? half * 0.7 : half
            let color = Color(.sRGB, red: Double(marker.packedColor & 255) / 255,
                              green: Double((marker.packedColor >> 8) & 255) / 255,
                              blue: Double((marker.packedColor >> 16) & 255) / 255, opacity: 1)
            var path = Path(); path.move(to: CGPoint(x: px - s, y: 0)); path.addLine(to: CGPoint(x: px + s, y: 0))
            path.addLine(to: CGPoint(x: px, y: s * 1.4)); path.closeSubpath()
            context.fill(path, with: .color(color))
            var line = Path(); line.move(to: CGPoint(x: px, y: s * 1.4)); line.addLine(to: CGPoint(x: px, y: m.tickBottom))
            context.stroke(line, with: .color(color), lineWidth: marker.kind == 1 ? 1 : 1.5)
        }
        let clock = Text(Timecode.format(Int32(clamping: self.clock.frame), fps)).font(.aurea(size: 13, weight: .bold)).monospacedDigit().tracking(0.5).foregroundColor(.white)
        // Android anchors the text baseline at 21 dp, above the 28.2 dp underline.
        let resolvedClock = context.resolve(clock)
        let clockSize = resolvedClock.measure(in: CGSize(width: size.width, height: m.rowsTop))
        context.draw(resolvedClock, at: CGPoint(x: size.width / 2, y: m.timecodeBaseline - resolvedClock.firstBaseline(in: clockSize)), anchor: .top)
        context.fill(Path(CGRect(x: size.width / 2 - m.underlineWidth / 2, y: m.underlineTop, width: m.underlineWidth, height: m.underlineHeight)), with: .color(.white))
    }

    private func drawRows(_ context: inout GraphicsContext, size: CGSize) {
        var c = context
        c.clip(to: Path(CGRect(x: 0, y: m.rowsTop, width: size.width, height: max(0, size.height - m.rowsTop))))
        let visibleRows = rows
        // Topo de cada linha somado UMA vez: `rowTop(i)` refaz `rows` (que
        // compara todas as camadas e keyframes) — no laço, isso era quadrático.
        var tops: [CGFloat] = []
        tops.reserveCapacity(visibleRows.count + 1)
        var acc: CGFloat = 0
        for row in visibleRows { tops.append(acc); acc += rowHeight(row) }
        tops.append(acc)
        func topOf(_ i: Int) -> CGFloat { tops[min(max(0, i), tops.count - 1)] }
        for (index, row) in visibleRows.enumerated() {
            let top = m.rowsTop + tops[index] - (compact ? 0 : scrollY)
            if top + rowHeight(row) < m.rowsTop || top > size.height { continue }
            drawRow(&c, row: row, top: top, width: size.width)
        }
        if reorderSource >= 0 {
            let top = m.rowsTop + topOf(reorderSource) - scrollY
            c.fill(Path(CGRect(x: 0, y: top, width: size.width, height: m.row)), with: .color(AureaColors.accent.opacity(0.14)))
            let y = reorderTarget < 0 || reorderSource == reorderTarget ? CGFloat.nan : m.rowsTop + topOf(reorderTarget + (reorderTarget > reorderSource ? 1 : 0)) - scrollY
            if y.isFinite {
                c.fill(Path(CGRect(x: 0, y: y - m.reorderLine / 2, width: size.width, height: m.reorderLine)), with: .color(AureaColors.accent))
                c.fill(Path(ellipseIn: CGRect(x: m.pillLeft - m.reorderDot, y: y - m.reorderDot, width: m.reorderDot * 2, height: m.reorderDot * 2)), with: .color(AureaColors.accent))
            }
        }
        let shade = Gradient(stops: [.init(color: AureaColors.stage, location: 0), .init(color: AureaColors.stage.opacity(0.95), location: 0.78), .init(color: AureaColors.stage.opacity(0), location: 1)])
        c.fill(Path(CGRect(x: 0, y: m.rowsTop, width: m.headerColumn, height: max(0, size.height - m.rowsTop))), with: .linearGradient(shade, startPoint: .zero, endPoint: CGPoint(x: m.headerColumn, y: 0)))
        for (index, row) in visibleRows.enumerated() {
            let cy = m.rowsTop + tops[index] - (compact ? 0 : scrollY) + rowHeight(row) / 2
            guard cy + m.row / 2 >= m.rowsTop && cy - m.row / 2 <= size.height else { continue }
            if row.track != nil {
                glyph(&c, CupertinoGlyph.ChevronRight, size: 10, tint: .white.opacity(0.6), x: m.swatchLeft + m.swatch / 2, y: cy)
                continue
            }
            c.fill(Path(roundedRect: CGRect(x: m.pillLeft, y: cy - m.pillHeight / 2, width: m.pillWidth, height: m.pillHeight), cornerRadius: m.pillRadius), with: .color(AureaTimeline.headerPill))
            glyph(&c, row.visible ? CupertinoGlyph.Eye : CupertinoGlyph.EyeSlash, size: m.eyeGlyph, tint: .white.opacity(0.7), x: m.eyeCenterX, y: cy)
            c.fill(Path(roundedRect: CGRect(x: m.swatchLeft, y: cy - m.swatch / 2, width: m.swatch, height: m.swatch), cornerRadius: m.swatchRadius), with: .color(AureaTimeline.swatch))
            if row.locked {
                glyph(&c, CupertinoGlyph.LockFill, size: 11, tint: AureaTimeline.swatchGlyph, x: m.swatchLeft + m.swatch / 2, y: cy)
            } else if model.selection.count >= 2 && model.selection.contains(row.id) {
                glyph(&c, CupertinoGlyph.CheckmarkAlt, size: 13, tint: AureaTimeline.swatchGlyph, x: m.swatchLeft + m.swatch / 2, y: cy)
            } else {
                glyph(&c, expandedLayer == row.id ? CupertinoGlyph.ChevronDown : CupertinoGlyph.ChevronRight, size: 11, tint: AureaTimeline.swatchGlyph, x: m.swatchLeft + m.swatch / 2, y: cy)
            }
        }
        if guide != Snap.none {
            let gx = x(Double(guide), width: size.width)
            if gx >= m.headerColumn && gx <= size.width {
                c.fill(Path(CGRect(x: gx - m.guide / 2, y: m.rowsTop, width: m.guide, height: size.height - m.rowsTop)), with: .color(AureaColors.accent))
            }
        }
    }

    private func drawRow(_ context: inout GraphicsContext, row: TimelineRow, top: CGFloat, width: CGFloat) {
        if row.track != nil {
            var line = Path(); line.move(to: CGPoint(x: m.headerColumn, y: top + 20)); line.addLine(to: CGPoint(x: width, y: top + 20))
            context.stroke(line, with: .color(.white.opacity(0.08)), lineWidth: 1)
            let label = context.resolve(Text(row.name).font(.aurea(size: 11)).foregroundColor(AureaColors.muted))
            var title = context
            title.clip(to: Path(CGRect(x: m.headerColumn + 4, y: top, width: max(0, width - m.headerColumn - 8), height: 16)))
            title.draw(label, at: CGPoint(x: m.headerColumn + 4, y: top), anchor: .topLeading)
            drawKeys(&context, row: row, top: top + 20 - m.diamondCyNormal, width: width)
            return
        }
        if let track = model.captionTracks.first(where: { $0.layer == row.id }) {
            for block in track.segments {
                let left = x(Double(block.start) + Double(row.start) - Double(row.offset), width: width)
                let right = x(Double(block.end) + Double(row.start) - Double(row.offset), width: width) - 2
                if right < 0 || left > width || right <= left { continue }
                let rect = CGRect(x: max(0, left), y: top, width: min(width, right) - max(0, left), height: m.bar)
                var clipped = context
                clipped.clip(to: Path(rect))
                clipped.fill(Path(roundedRect: rect, cornerRadius: m.barRadius), with: .color(model.selection.contains(row.id) ? AureaColors.accent.opacity(0.7) : row.type.color.opacity(0.5)))
                clipped.draw(Text(block.text).font(.aurea(size: 11)).foregroundColor(.white), at: CGPoint(x: rect.minX + 5, y: top + 7), anchor: .topLeading)
            }
            return
        }
        let x0 = x(Double(row.start), width: width), x1 = max(x(Double(row.end), width: width), x0 + m.barMinWidth)
        let selected = model.selection.contains(row.id)
        if x1 >= -m.barRadius && x0 <= width + m.barRadius {
            let left = max(x0, -m.barRadius * 2), right = min(x1, width + m.barRadius * 2)
            let rect = CGRect(x: left, y: top, width: right - left, height: m.bar)
            let shape = Path(roundedRect: rect, cornerRadius: m.barRadius)
            var bar = context; bar.clip(to: shape)
            // Channel-wise sRGB interpolation, matching TimelinePainter.lerpSrgb.
            let amount: CGFloat = !row.visible ? 0.22 : selected ? 0.66 : 0.46
            var r: CGFloat = 0, g: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 0
            UIColor(row.type.color).getRed(&r, green: &g, blue: &b, alpha: &a)
            let fill = Color(.sRGB, red: Double(21.0 / 255 + (r - 21.0 / 255) * amount), green: Double(28.0 / 255 + (g - 28.0 / 255) * amount), blue: Double(36.0 / 255 + (b - 36.0 / 255) * amount), opacity: 1)
            bar.fill(shape, with: .color(fill))
            if row.track == nil, let tiles = thumbnails[row.id], !tiles.isEmpty {
                for tile in tiles {
                    let px = x(Double(row.start) - Double(row.offset) + tile.localFrame, width: width)
                    bar.draw(Image(uiImage: tile.image), in: CGRect(x: px, y: top, width: tile.width, height: m.bar))
                }
                let shade = Gradient(stops: [.init(color: .black.opacity(0.62), location: 0), .init(color: .black.opacity(0.22), location: 0.45)])
                bar.fill(shape, with: .linearGradient(shade, startPoint: CGPoint(x: x0, y: top), endPoint: CGPoint(x: x1, y: top)))
            }
            bar.fill(Path(CGRect(x: left, y: top + m.trackTop, width: right - left, height: m.track)), with: .color(.black.opacity(0.22)))
            if row.track == nil { drawWave(&bar, row: row, top: top, x0: x0, x1: x1, width: width) }
            let stripe = row.label > 0 && Int(row.label) <= AureaColors.labelPalette.count ? AureaColors.labelPalette[Int(row.label) - 1] : row.type.color
            bar.fill(Path(CGRect(x: x0, y: top, width: m.stripe, height: m.bar)), with: .color(stripe.opacity(row.visible ? 1 : 0.5)))
            var light = Path(); light.move(to: CGPoint(x: max(x0 + m.stripe, left), y: top + m.lightLine / 2)); light.addLine(to: CGPoint(x: right, y: top + m.lightLine / 2))
            bar.stroke(light, with: .color(.white.opacity(selected ? 0.24 : 0.1)), lineWidth: m.lightLine)
            drawContent(&bar, row: row, top: top, x0: x0, x1: x1, width: width)
            if selected {
                let stroke = model.selection.count >= 2 ? m.multiStroke : m.selStroke
                context.stroke(Path(roundedRect: rect.insetBy(dx: stroke / 2, dy: stroke / 2), cornerRadius: m.barRadius - stroke / 2), with: .color(.white), lineWidth: stroke)
            }
            if row.track == nil && !compact && model.selection.count == 1 && selected && !row.locked {
                if x0 >= m.headerColumn { drawHandle(&context, left: x0 - m.trimInsetStart, top: top) }
                if x1 <= width { drawHandle(&context, left: x1 - m.trimInsetEnd, top: top) }
            }
        } else if row.track == nil {
            // Clipe fora da janela: seta na borda para o lado dele (par do
            // TimelinePainter) — linha vazia parecia camada quebrada.
            let toRight = x0 > width
            let tip = toRight ? width - 10 : m.headerColumn + 10, back = toRight ? tip - 7 : tip + 7
            let cy = top + m.bar / 2
            var arrow = Path(); arrow.move(to: CGPoint(x: tip, y: cy))
            arrow.addLine(to: CGPoint(x: back, y: cy - 6)); arrow.addLine(to: CGPoint(x: back, y: cy + 6)); arrow.closeSubpath()
            context.fill(arrow, with: .color(row.type.color.opacity(row.visible ? 0.9 : 0.45)))
        }
        drawKeys(&context, row: row, top: top, width: width)
    }

    private func drawContent(_ context: inout GraphicsContext, row: TimelineRow, top: CGFloat, x0: CGFloat, x1: CGFloat, width: CGFloat) {
        let barWidth = x1 - x0
        let cl = TimelineHit.contentLeft(m, x0, x1), cr = TimelineHit.contentRight(m, x0, x1, width)
        guard cr > cl else { return }
        let cy = top + m.trackTop / 2
        var px = cl
        if compact { glyph(&context, CupertinoGlyph.ChevronLeft, size: m.arrowGlyph, tint: .white.opacity(0.7), x: px + m.arrowSlot / 2, y: cy); px += m.arrowSlot }
        if barWidth > m.iconMinBar {
            glyph(&context, row.type.glyph, size: m.typeIcon, tint: .white.opacity(row.visible ? 0.85 : 0.45), x: px + m.typeIcon / 2, y: cy)
            px += m.typeIcon + m.iconGap
        }
        if row.locked {
            glyph(&context, CupertinoGlyph.LockFill, size: m.lockIcon, tint: .white, x: px + m.lockIcon / 2, y: cy)
            px += m.lockIcon + (barWidth > m.lockGapMinBar ? m.lockGap : 0)
        }
        let menuRight = x1 - (barWidth < m.narrowBar ? m.padRNarrow : m.padR)
        let right = compact ? cr - m.arrowSlot : barWidth > m.menuMinBar ? min(cr, menuRight - m.menuGlyph) : cr
        let rhombusWidth = row.animated && barWidth > m.rhombusMinBar ? m.rhombusGap + m.rhombusIcon : 0
        let avail = right - rhombusWidth - px
        if barWidth > m.nameMinBar && !row.name.isEmpty && avail > 8 {
            let name = fittedName(row.name, width: floor(avail / 12) * 12)
            let resolved = context.resolve(Text(name).font(.aurea(size: 12, weight: .semibold)).tracking(-0.1).foregroundColor(.white))
            context.draw(resolved, at: CGPoint(x: px, y: cy), anchor: .leading)
            px += resolved.measure(in: CGSize(width: avail, height: m.trackTop)).width
        }
        if rhombusWidth > 0 && px + rhombusWidth <= right + 1 {
            glyph(&context, CupertinoGlyph.Rhombus, size: m.rhombusIcon, tint: .white, x: px + m.rhombusGap + m.rhombusIcon / 2, y: cy)
        }
        if compact {
            glyph(&context, CupertinoGlyph.ChevronRight, size: m.arrowGlyph, tint: .white.opacity(0.7), x: cr - m.arrowSlot / 2, y: cy)
        } else if barWidth > m.menuMinBar && menuRight <= width + m.menuGlyph {
            glyph(&context, CupertinoGlyph.LineHorizontal3, size: m.menuGlyph, tint: .white.opacity(0.7), x: menuRight - m.menuGlyph / 2, y: cy)
        }
    }

    private func drawHandle(_ context: inout GraphicsContext, left: CGFloat, top: CGFloat) {
        context.fill(Path(roundedRect: CGRect(x: left, y: top + m.trimTop, width: m.trimWidth, height: m.bar - m.trimTop * 2), cornerRadius: m.trimRadius), with: .color(AureaTimeline.trimHandle))
        context.fill(Path(CGRect(x: left + (m.trimWidth - m.gripWidth) / 2, y: top + (m.bar - m.gripHeight) / 2, width: m.gripWidth, height: m.gripHeight)), with: .color(.black.opacity(0.38)))
    }

    private func drawKeys(_ context: inout GraphicsContext, row: TimelineRow, top: CGFloat, width: CGFloat) {
        guard !row.instants.isEmpty else { return }
        var groups = [Int32](repeating: 0, count: 2 * Int((width + 2 * m.keyTouchHalf) / m.keyMergeGap) + 8)
        let count = Keyframes.visibleGroups(row.instants, view: viewFrame, pxPerFrame: ppf, centerX: width / 2, width: width, margin: m.keyTouchHalf, mergeGap: m.keyMergeGap, out: &groups)
        let cy = top + (compact ? m.diamondCyCompact : m.diamondCyNormal)
        let selectedIndex = lowerBound(row.instants, selectedKey?.frame ?? Snap.none)
        let selectedTrackMatches = selectedIndex < row.instants.count && row.instants[selectedIndex] == selectedKey?.frame && row.keysAt[selectedIndex].contains { key in
            selectedKey?.track == TimelineTrack(property: Int(key.property), effect: key.effectIndex, param: key.paramIndex)
        }
        let chosen = selectedKey?.layer == row.id && selectedTrackMatches ? selectedKey?.frame ?? Snap.none : Snap.none
        for group in 0..<count {
            let i = Int(groups[group * 2]), j = Int(groups[group * 2 + 1])
            let px = x(Double(row.instants[i]), width: width)
            let on = Keyframes.groupHas(row.instants, i, j, chosen)
            let fill = on ? AureaTimeline.keyframeOn : Color.white.opacity(0.9)
            if i == j {
                let dragTrackMatches = row.track == nil || gesture?.row?.track == nil || gesture?.row?.track == row.track
                let dragging = gesture?.mode == .key && gesture?.row?.id == row.id && dragTrackMatches && gesture?.keyFrame == row.instants[i]
                let side = m.diamond * (dragging ? m.keyDragScale : 1)
                var diamond = context; diamond.translateBy(x: px, y: cy); diamond.rotate(by: .degrees(45))
                let glow = side / 2 * 1.3
                diamond.fill(Path(roundedRect: CGRect(x: -glow, y: -glow, width: glow * 2, height: glow * 2), cornerRadius: m.diamondRadius * 1.5), with: .color(on ? AureaTimeline.keyframeOn.opacity(0.35) : .black.opacity(0.3)))
                diamond.fill(Path(roundedRect: CGRect(x: -side / 2, y: -side / 2, width: side, height: side), cornerRadius: m.diamondRadius), with: .color(fill))
                diamond.stroke(Path(roundedRect: CGRect(x: -side / 2, y: -side / 2, width: side, height: side).insetBy(dx: m.diamondStroke / 2, dy: m.diamondStroke / 2), cornerRadius: m.diamondRadius), with: .color(.black.opacity(0.85)), lineWidth: m.diamondStroke)
                if dragging {
                    let text = context.resolve(Text(Timecode.format(row.instants[i], fps)).font(.aurea(size: 10, weight: .bold)).monospacedDigit().foregroundColor(.white))
                    let measured = text.measure(in: CGSize(width: 180, height: 20))
                    let rect = CGRect(x: px - measured.width / 2 - m.balloonPadH, y: cy - side * 1.4142135 / 2 - m.balloonGap - measured.height - m.balloonPadV * 2, width: measured.width + m.balloonPadH * 2, height: measured.height + m.balloonPadV * 2)
                    context.fill(Path(roundedRect: rect, cornerRadius: m.balloonRadius), with: .color(.black.opacity(0.82)))
                    context.draw(text, at: CGPoint(x: rect.midX, y: rect.midY))
                }
            } else {
                let lastX = x(Double(row.instants[j]), width: width), pillWidth = max(lastX - px, m.keyPillMinWidth)
                let rect = CGRect(x: (px + lastX - pillWidth) / 2, y: cy - m.keyPillHeight / 2, width: pillWidth, height: m.keyPillHeight)
                context.fill(Path(roundedRect: rect, cornerRadius: m.keyPillHeight / 2), with: .color(fill))
                context.stroke(Path(roundedRect: rect.insetBy(dx: m.diamondStroke / 2, dy: m.diamondStroke / 2), cornerRadius: m.keyPillHeight / 2), with: .color(.black.opacity(0.85)), lineWidth: m.diamondStroke)
            }
        }
    }

    private func drawWave(_ context: inout GraphicsContext, row: TimelineRow, top: CGFloat, x0: CGFloat, x1: CGFloat, width: CGFloat) {
        guard let wave = waves[row.id] else { return }
        let left = max(x0 + m.stripe, 0), right = min(x1, width)
        guard right > left else { return }
        let audio = row.type == .audio
        let areaTop = top + m.trackTop * (audio ? 0.62 : 0.8), bottom = top + m.bar - m.lightLine
        let mid = (areaTop + bottom) / 2, half = (bottom - areaTop) / 2
        let first = WaveGrid.bucketAt(frame(left, width: width), wave.fpb), last = WaveGrid.bucketAt(frame(right, width: width), wave.fpb)
        var path = Path()
        if last >= first {
            for bucket in first...last {
                let amplitude = CGFloat(wave.at(bucket)) / 255 * half
                let px = x((Double(bucket) + 0.5) * wave.fpb, width: width)
                guard amplitude >= 0.5 && px >= left && px <= right else { continue }
                path.move(to: CGPoint(x: px, y: mid - amplitude)); path.addLine(to: CGPoint(x: px, y: mid + amplitude))
            }
        }
        context.stroke(path, with: .color(.white.opacity(Double(audio ? 170 : 150) / 255)), lineWidth: max(1, CGFloat(wave.fpb) * ppf * 0.72))
    }

    private func glyph(_ context: inout GraphicsContext, _ glyph: Character, size: CGFloat, tint: Color, x: CGFloat, y: CGFloat) {
        context.draw(Text(String(glyph)).font(CupertinoFont.font(size)).foregroundColor(tint), at: CGPoint(x: x, y: y))
    }
    private func fittedName(_ name: String, width: CGFloat) -> String {
        let attributes: [NSAttributedString.Key: Any] = [.font: UIFont.systemFont(ofSize: 12, weight: .semibold), .kern: -0.1]
        if (name as NSString).size(withAttributes: attributes).width <= width { return name }
        let letters = Array(name)
        var lo = 0, hi = letters.count
        while lo < hi {
            let n = (lo + hi + 1) / 2
            if ((String(letters.prefix(n)) + "…") as NSString).size(withAttributes: attributes).width <= width { lo = n } else { hi = n - 1 }
        }
        return String(letters.prefix(lo)) + "…"
    }

    private func refreshMedia(size: CGSize) {
        guard size.width > 0 && size.height > 0 else { return }
        thumbCache.beginFrame(6)
        let first = max(0, rowIndex(scrollY))
        let visible = rows.dropFirst(first).prefix(Int(size.height / 28) + 2)
        var next: [Int64: [MediaTile]] = [:], nextWaves: [Int64: TimelineWaveStrip.Entry] = [:]
        for row in visible where row.track == nil {
            let x0 = x(Double(row.start), width: size.width), x1 = max(x(Double(row.end), width: size.width), x0 + m.barMinWidth)
            let left = max(x0, 0), right = min(x1, size.width)
            guard right > left else { continue }
            if row.hasThumbs {
                let tileWidth = m.bar * thumbCache.aspect(row.id)
                let origin = x(Double(row.start) - Double(row.offset), width: size.width)
                let start = max(0, Int(floor((left - origin) / tileWidth))), end = Int(floor((right - origin) / tileWidth))
                var tiles: [MediaTile] = []
                if end >= start {
                    for index in start...end {
                        let local = Double(CGFloat(index) * tileWidth / ppf)
                        let bucket = row.type == .image ? -1 : Thumbs.bucketOf(local, fps)
                        let request = row.type == .image ? row.start : Keyframes.toTimeline(Thumbs.requestLocalFrame(bucket, fps), row.start, row.offset)
                        if let image = thumbCache.get(model, layer: row.id, bucket: bucket, timelineFrame: request, heightPx: Int(m.bar), generation: model.status.thumbnailGeneration) {
                            tiles.append(MediaTile(localFrame: local, width: tileWidth, image: image))
                        }
                    }
                }
                next[row.id] = tiles
            }
            if row.type == .audio || row.type == .video {
                let fpb = WaveGrid.framesPerBucket(targetPx: 1.5, pxPerFrame: ppf)
                let firstBucket = WaveGrid.bucketAt(frame(max(x0 + m.stripe, 0), width: size.width), fpb), lastBucket = WaveGrid.bucketAt(frame(right, width: size.width), fpb)
                nextWaves[row.id] = waveCache.get(model, layer: row.id, generation: model.status.modelRevision &+ model.status.thumbnailGeneration, fpb: fpb, first: firstBucket, last: lastBucket)
            }
        }
        thumbnails = next; waves = nextWaves
        // Marcas só mudam com o modelo (revisão) ou com a lista local do modelo:
        // relê-las a cada quadro de playback/scrub era trabalho jogado fora.
        let markerKey = (model.status.modelRevision, model.markerFrames)
        if markerKey.0 != markersRevision || markerKey.1 != markersFrames {
            markersRevision = markerKey.0; markersFrames = markerKey.1
            let values = model.engine.markers()
            markers = stride(from: 0, to: values.count - values.count % 3, by: 3).map { Marker(frame: values[$0].int32Value, packedColor: values[$0 + 1].uint32Value, kind: values[$0 + 2].uint32Value) }
        }
    }

    // MARK: Android controller and hit priorities
    private func rowHeight(_ row: TimelineRow) -> CGFloat { row.track == nil ? m.row : 28 }
    private func rowTop(_ index: Int) -> CGFloat { rowTop(index, in: rows) }
    private func rowTop(_ index: Int, in list: [TimelineRow]) -> CGFloat { list.prefix(max(0, index)).reduce(0) { $0 + rowHeight($1) } }
    private func rowIndex(_ y: CGFloat) -> Int {
        guard y >= 0 else { return -1 }
        var bottom: CGFloat = 0
        for (index, row) in rows.enumerated() {
            bottom += rowHeight(row)
            if y < bottom { return index }
        }
        return rows.count
    }

    private func hit(_ point: CGPoint, width: CGFloat) -> (TimelineRow?, TimelineHit) {
        if point.y < m.rowsTop { return (nil, TimelineHit(kind: .ruler)) }
        let index = rowIndex(point.y - m.rowsTop + (compact ? 0 : scrollY))
        let current = rows
        guard current.indices.contains(index) else { return (nil, TimelineHit(kind: .none)) }
        let row = current[index]
        let y = point.y - m.rowsTop - rowTop(index) + (compact ? 0 : scrollY)
        let x0 = x(Double(row.start), width: width), x1 = max(x(Double(row.end), width: width), x0 + m.barMinWidth)
        let handles = row.track == nil && !compact && model.selection.count == 1 && model.selection.contains(row.id) && !row.locked
        var result = TimelineHit.test(m, point: CGPoint(x: point.x, y: row.track == nil ? y : m.diamondCyNormal), width: width, x0: x0, x1: x1, handles: handles, compact: compact, instants: row.instants, view: viewFrame, ppf: ppf)
        if row.track != nil && result.kind != .key { result = TimelineHit(kind: .body) }
        return (row, result)
    }
    private func tap(_ point: CGPoint, width: CGFloat) {
        // Toque que só PAROU a rolagem inércia não é toque (igual ao Android).
        let stoppedFling = abs(scrollVelocity) > 1
        scrollVelocity = 0
        if stoppedFling { return }
        let (row, touched) = hit(point, width: width)
        switch touched.kind {
        case .ruler:
            // Régua = buscar. Não cria marca: ela divide a faixa com o relógio
            // e o topo do cabeçote, e cada busca deixava uma marca "do nada".
            // Tocar em cima de uma marca (até 12 pt) abre o editor dela.
            let target = Int64(max(0, timelineFrame(frame(point.x, width: width))))
            let marker = model.markerNear(target, tolerance: Int64(12 / max(0.0001, ppf)))
            model.seek(toFrame: marker ?? target)
            if let marker { model.openMarkerEditor(marker) }
            UISelectionFeedbackGenerator().selectionChanged()
        case .none:
            selectedKey = nil; model.editorBackFromTimeline()
        case .eye:
            guard let row else { return }
            model.engine.setLayer(row.id, visible: !row.visible); model.refreshModel(force: true)
        case .previous, .next:
            guard let selected = model.primarySelection, let index = model.layers.firstIndex(where: { $0.id == selected }) else { return }
            let neighbor = index + (touched.kind == .previous ? 1 : -1)
            if model.layers.indices.contains(neighbor) { model.select(layerId: model.layers[neighbor].id, additive: false) }
        case .key:
            guard let row, row.keysAt.indices.contains(touched.key) else { return }
            pause(); model.select(layerId: row.id, additive: false)
            let key = row.keysAt[touched.key][0]
            selectedKey = (row.id, row.instants[touched.key], TimelineTrack(property: Int(key.property), effect: key.effectIndex, param: key.paramIndex))
            model.seek(toFrame: Int64(row.instants[touched.key]))
            model.openCurve(property: key.property, effect: key.effectIndex, param: key.paramIndex, time: key.time)
        default:
            guard let row else { return }
            if let track = row.track {
                model.select(layerId: row.id, additive: false)
                switch track.property {
                case 30: model.openPanel(.effects)
                case 31:
                    model.openPanel(.effects)
                    model.loadParams(layerId: row.id, effectId: track.effect)
                case 32: model.openPanel(.audio)
                case 33: model.openPanel(.textAnimation)
                case 34: model.openPanel(.vector)
                case 35: model.openPanel(.shape)
                case 36: model.openPanel(.particles)
                case 37: model.openPanel(.layer3D)
                default: model.openPanel(.transform)
                }
                return
            }
            if touched.kind == .header && !compact {
                expandedLayer = expandedLayer == row.id ? nil : row.id
                UISelectionFeedbackGenerator().selectionChanged()
                return
            }
            if compact { selectedKey = nil; model.editorBackFromTimeline() }
            else { pause(); selectedKey = nil; model.select(layerId: row.id, additive: model.selection.count >= 2) }
        }
    }

    private func pause() { if model.status.playing != 0 { model.playPause() } }
    private func targets(excluding: Set<Int64>, own: TimelineRow?, edges: Bool, keys: Bool) -> [Int32] {
        var result = [Int32(0)] + markers.map(\.frame)
        if !compact {
            for row in rows where !excluding.contains(row.id) { result += [row.start, row.end] + row.instants }
        }
        if let own {
            if edges { result += [own.start, own.end] }
            if keys { result += own.instants }
        }
        return Snap.sortedDistinct(result)
    }
    private func begin(_ mode: Mode, start: CGPoint, width: CGFloat) {
        scrollVelocity = 0
        let (row, touched) = hit(start, width: width)
        var selected = model.layers.filter { model.selection.contains($0.id) }
        if mode == .move, let row, !selected.contains(where: { $0.id == row.id }), let item = model.layers.first(where: { $0.id == row.id }) { selected = [item] }
        var next = Interaction(mode: mode, start: start, view: viewFrame, scroll: scrollY, row: row, hit: touched, selection: selected, snapTargets: [])
        let excluded = Set(mode == .move ? selected.map(\.id) : row.map { [$0.id] } ?? [])
        next.snapTargets = targets(excluding: excluded, own: row, edges: mode == .key, keys: mode == .trimStart || mode == .trimEnd)
        if mode == .move && selected.contains(where: \.locked) || (mode == .key || mode == .trimStart || mode == .trimEnd || mode == .reorder) && row?.locked == true {
            next.mode = .blocked
            UINotificationFeedbackGenerator().notificationOccurred(.warning)
        }
        if next.mode == .key, let row, row.instants.indices.contains(touched.key) {
            next.keyIndex = touched.key; next.keyFrame = row.instants[touched.key]
            next.movingKeys = focusTracks != nil || row.track != nil ? row.keysAt[touched.key] : Array(row.keysAt[touched.key].prefix(1))
            let instants = row.instants.enumerated().filter { i, _ in row.keysAt[i].contains { candidate in
                next.movingKeys.contains { $0.property == candidate.property && $0.effectIndex == candidate.effectIndex && $0.paramIndex == candidate.paramIndex }
            } }.map { $0.element }
            next.keyLimits = Keyframes.dragLimits(instants, instants.firstIndex(of: next.keyFrame) ?? 0, start: row.start, end: row.end)
            if let key = next.movingKeys.first { selectedKey = (row.id, next.keyFrame, TimelineTrack(property: Int(key.property), effect: key.effectIndex, param: key.paramIndex)) }
            model.select(layerId: row.id, additive: false)
        }
        if next.mode == .reorder, let row {
            reorderSource = rows.firstIndex(where: { $0.id == row.id }) ?? -1; reorderTarget = reorderSource
        }
        if next.mode != .hold && next.mode != .blocked && next.mode != .scroll { pause() }
        if next.mode == .scrub {
            heldView = next.view; model.engine.run { $0.scrubBegin() }
        }
        gesture = next
    }

    private func pan(_ state: UIGestureRecognizer.State, start: CGPoint, point: CGPoint, velocity: CGPoint, size: CGSize) {
        guard !pinching else { return }
        if state == .began {
            let (row, touched) = hit(start, width: size.width)
            let horizontal = abs(point.x - start.x) >= abs(point.y - start.y)
            let mode: Mode
            if horizontal && touched.kind == .key { mode = .key }
            else if horizontal && touched.kind == .trimStart { mode = .trimStart }
            else if horizontal && touched.kind == .trimEnd { mode = .trimEnd }
            else if horizontal && !compact && touched.kind == .body && row.map({ $0.track == nil && model.selection.contains($0.id) }) == true { mode = .move }
            else { mode = horizontal || compact ? .scrub : .scroll }
            begin(mode, start: start, width: size.width)
        }
        if state == .began || state == .changed { lastPointer = point; update(point, size: size) }
        if state == .ended {
            let wasScroll = gesture?.mode == .scroll
            finish(cancelled: false)
            if wasScroll && abs(velocity.y) >= m.flingMin { scrollVelocity = -velocity.y }
        } else if state == .cancelled || state == .failed { finish(cancelled: true) }
    }

    private func hold(_ state: UIGestureRecognizer.State, start: CGPoint, point: CGPoint, size: CGSize) {
        guard !pinching else { return }
        if state == .began {
            begin(.hold, start: start, width: size.width)
            UIImpactFeedbackGenerator(style: .medium).impactOccurred()
        }
        if state == .changed, let g = gesture, g.mode == .hold {
            let dx = point.x - start.x, dy = point.y - start.y
            if hypot(dx, dy) >= m.axisSlop {
                let horizontal = abs(dx) > abs(dy)
                let mode: Mode
                if (g.row?.track != nil && g.hit.kind != .key) || g.row == nil || g.hit.kind == .none || g.hit.kind == .ruler || g.hit.kind == .eye { mode = horizontal || compact ? .scrub : .scroll }
                else if g.hit.kind == .key { mode = horizontal ? .key : .blocked }
                else if g.hit.kind == .header { mode = !horizontal && !compact ? .reorder : .blocked }
                else { mode = horizontal || compact ? .move : .reorder }
                if mode == .move, let row = g.row, !model.selection.contains(row.id) {
                    model.select(layerId: row.id, additive: model.selection.count >= 2, openOptions: false)
                }
                begin(mode, start: start, width: size.width)
            }
        }
        if state == .began || state == .changed { lastPointer = point; update(point, size: size) }
        if state == .ended {
            if let g = gesture, g.mode == .hold, let row = g.row {
                if g.row?.track != nil {
                    tap(start, width: size.width)
                } else if g.hit.kind == .header {
                    model.engine.setLayer(row.id, locked: !row.locked); model.refreshModel(force: true)
                } else if g.hit.kind == .key { tap(start, width: size.width) }
                else if !compact && g.hit.kind != .eye && g.hit.kind != .none {
                    if model.selection.isEmpty { model.select(layerId: row.id, additive: false) }
                    else if !(model.selection.count == 1 && model.selection.contains(row.id)) { model.select(layerId: row.id, additive: true) }
                }
            }
            finish(cancelled: false)
        } else if state == .cancelled || state == .failed { finish(cancelled: true) }
    }

    private func update(_ point: CGPoint, size: CGSize) {
        guard var g = gesture else { return }
        let delta = frame(point.x, width: size.width) - TimeAxis.frameAt(x: g.start.x, view: g.view, pxPerFrame: ppf, centerX: size.width / 2)
        switch g.mode {
        case .scrub:
            let desired = g.view - Double((point.x - g.start.x) / ppf)
            holdView(desired)
        case .scroll:
            scrollY = min(maxScroll(size.height), max(0, g.scroll - (point.y - g.start.y)))
        case .reorder:
            reorderTarget = min(max(0, rowIndex(point.y - m.rowsTop + scrollY)), max(0, rows.count - 1))
        case .move:
            guard let row = g.row, let earliest = g.selection.map(\.startFrame).min(), let latest = g.selection.map(\.endFrame).max() else { return }
            let desired = timelineFrame(Double(row.start) + delta)
            let snapped = model.snapping ? Snap.span(g.snapTargets, start: desired, length: row.end - row.start, extra: timelineFrame(viewFrame), tol: Double(m.snapClip / ppf)) : (start: desired, guide: Snap.none)
            let change = Int32(clamping: max(-Int64(earliest), min(Int64(Int32.max) - Int64(latest), Int64(snapped.start) - Int64(row.start))))
            guide = Int64(row.start) + Int64(change) == Int64(snapped.start) ? snapped.guide : Snap.none
            if change != g.sentDelta {
                openUndo(&g)
                for item in g.selection {
                    model.engine.setLayer(item.id, startFrame: item.startFrame + change, endFrame: item.endFrame + change, offsetFrames: item.offsetFrames, setOffset: false)
                }
                g.sentDelta = change; model.refreshModel(force: true)
            }
        case .trimStart, .trimEnd:
            guard let row = g.row else { return }
            let origin = g.mode == .trimStart ? row.start : row.end
            let desired = Double(origin) + delta
            let snapped = model.snapping ? Snap.nearest(g.snapTargets, desired, extra: timelineFrame(viewFrame), tol: Double(m.snapClip / ppf)) : Snap.none
            let target = max(0, snapped == Snap.none ? timelineFrame(desired) : snapped)
            let change = Int32(clamping: Int64(target) - Int64(origin))
            if change != g.sentDelta {
                openUndo(&g)
                if g.mode == .trimStart {
                    if model.editMode, let current = model.layers.first(where: { $0.id == row.id }) {
                        model.trimStart(row.id, at: Int64(current.startFrame) + Int64(change) - Int64(g.sentDelta))
                    } else { model.trimStart(row.id, at: Int64(target)) }
                } else { model.trimEnd(row.id, at: Int64(target)) }
                g.sentDelta = change
            }
            guide = snapped
        case .key:
            guard let row = g.row, row.instants.indices.contains(g.keyIndex) else { return }
            let desired = Double(row.instants[g.keyIndex]) + delta
            let snapped = model.snapping ? Snap.nearest(g.snapTargets, desired, extra: timelineFrame(viewFrame), tol: Double(m.snapKey / ppf)) : Snap.none
            let target = min(g.keyLimits.hi, max(g.keyLimits.lo, snapped == Snap.none ? timelineFrame(desired) : snapped))
            if target != g.keyFrame {
                openUndo(&g)
                let source = row.toLocal(g.keyFrame), destination = row.toLocal(target)
                for key in g.movingKeys {
                    model.engine.editTrackKey(row.id, property: key.property, effect: key.effectIndex, param: key.paramIndex, time: source, action: 2, value: key.value, targetTime: destination, interpolation: key.interpolation, handles: [])
                }
                g.keyFrame = target
                if let key = g.movingKeys.first { selectedKey = (row.id, target, TimelineTrack(property: Int(key.property), effect: key.effectIndex, param: key.paramIndex)) }
                model.curveSelectedTime = destination
                model.refreshModel(force: true)
            }
            guide = snapped == target ? snapped : Snap.none
        case .hold, .blocked: break
        }
        gesture = g
    }
    private func openUndo(_ g: inout Interaction) {
        if !g.undoOpen { model.engine.run { $0.beginUndoGroup() }; g.undoOpen = true }
    }
    private func holdView(_ desired: Double) {
        let target = TimeAxis.clampView(desired, durationFrames: Int32(clamping: model.compositionDuration))
        let frame = Int64(timelineFrame(target))
        heldView = Double(frame)
        model.engine.run { $0.scrub(toFrame: frame) }; model.optimisticPlayhead(frame)
    }
    private func finish(cancelled: Bool) {
        guard let g = gesture else { return }
        if g.mode == .reorder && !cancelled, let row = g.row, reorderTarget >= 0 && reorderTarget != reorderSource {
            model.engine.run { $0.beginUndoGroup() }; model.reorderLayer(row.id, displayIndex: model.layers.firstIndex { $0.id == rows[reorderTarget].id } ?? 0); model.engine.run { $0.endUndoGroup() }
        }
        if g.mode == .scrub { model.engine.run { $0.scrubEnd() } }
        if g.undoOpen { model.engine.run { $0.endUndoGroup() } }
        gesture = nil; heldView = nil; guide = Snap.none; reorderSource = -1; reorderTarget = -1
    }

    private func pinch(_ state: UIGestureRecognizer.State, scale: CGFloat, focus: CGPoint, width: CGFloat) {
        if state == .began {
            finish(cancelled: true); scrollVelocity = 0; pause(); pinching = true
            pinchPPS = pps; pinchFrame = frame(focus.x, width: width)
            model.engine.run { $0.scrubBegin() }
        }
        if state == .began || state == .changed {
            pps = Zoom.clamp(pinchPPS * scale)
            holdView(Zoom.anchoredView(focusFrame: pinchFrame, focusX: focus.x, centerX: width / 2, pxPerFrame: ppf))
        }
        if state == .ended || state == .cancelled || state == .failed { finishPinch() }
    }
    private func finishPinch() {
        if pinching { model.engine.run { $0.scrubEnd() }; pinching = false; heldView = nil }
    }
    private func tick(size: CGSize) {
        if let g = gesture {
            if g.mode == .move || g.mode == .key || g.mode == .trimStart || g.mode == .trimEnd {
                let direction = AutoScroll.direction(pos: lastPointer.x, from: g.start.x, low: m.headerColumn + m.autoEdge, high: size.width - m.autoEdge, intent: m.autoIntent)
                if direction != 0 {
                    // Auto-scroll moves the presentation window; the editing gesture
                    // reapplies its absolute target and the core remains authoritative.
                    let target = TimeAxis.clampView(viewFrame + Double(direction) * Double(m.autoSpeed / 60 / ppf), durationFrames: Int32(clamping: model.compositionDuration))
                    heldView = target; model.seek(toFrame: Int64(timelineFrame(target)))
                    update(lastPointer, size: size)
                }
            } else if g.mode == .reorder {
                let direction = AutoScroll.direction(pos: lastPointer.y, from: g.start.y, low: m.rowsTop + m.autoEdge, high: size.height - m.autoEdge, intent: m.autoIntent)
                if direction != 0 { scrollY = min(maxScroll(size.height), max(0, scrollY + CGFloat(direction) * m.autoSpeed / 60)); update(lastPointer, size: size) }
            }
        } else if abs(scrollVelocity) > 1 {
            let next = min(maxScroll(size.height), max(0, scrollY + scrollVelocity / 60))
            if next == scrollY { scrollVelocity = 0 } else { scrollY = next; scrollVelocity *= 0.94 }
        }
        if thumbCache.starved { refreshMedia(size: size) }
    }
    private func revealSelection(size: CGSize) {
        guard !compact, gesture == nil, let id = model.primarySelection, let index = rows.firstIndex(where: { $0.id == id }) else { return }
        let top = rowTop(index), bottom = top + rowHeight(rows[index])
        let visibleHeight = max(0, size.height - m.rowsTop)
        if top < scrollY { scrollY = top }
        else if bottom > scrollY + visibleHeight { scrollY = min(maxScroll(size.height), max(0, bottom - visibleHeight)) }
    }
}

/// The same priority and geometry as Android RowHit; drawing and hit testing
/// share TimelineMetrics so a handle cannot be grabbed beneath the header.
private struct TimelineHit {
    enum Kind { case none, ruler, eye, header, key, trimStart, trimEnd, previous, next, body }
    var kind: Kind
    var key = -1
    static func contentLeft(_ m: TimelineMetrics, _ x0: CGFloat, _ x1: CGFloat) -> CGFloat {
        max(x0, m.headerColumn) + (x1 - x0 < m.narrowBar ? m.padLNarrow : m.padL)
    }
    static func contentRight(_ m: TimelineMetrics, _ x0: CGFloat, _ x1: CGFloat, _ width: CGFloat) -> CGFloat {
        min(x1, width) - (x1 - x0 < m.narrowBar ? m.padRNarrow : m.padR)
    }
    static func test(_ m: TimelineMetrics, point: CGPoint, width: CGFloat, x0: CGFloat, x1: CGFloat, handles: Bool, compact: Bool, instants: [Int32], view: Double, ppf: CGFloat) -> TimelineHit {
        let x = point.x, y = point.y
        guard y >= 0 && y < m.row else { return TimelineHit(kind: .none) }
        if x < m.headerColumn { return TimelineHit(kind: x < m.eyeHitRight ? .eye : .header) }
        var key = -1, keyX: CGFloat = 0
        if y >= m.keyTouchTop && !instants.isEmpty {
            let i = Keyframes.nearestIndex(instants, TimeAxis.frameAt(x: x, view: view, pxPerFrame: ppf, centerX: width / 2))
            let px = TimeAxis.xOf(frame: Double(instants[i]), view: view, pxPerFrame: ppf, centerX: width / 2)
            if abs(px - x) <= m.keyTouchHalf { key = i; keyX = px }
        }
        let over = y < m.bodyHitBottom, mid = (x0 + x1) / 2
        let start = handles && over && x0 >= m.headerColumn && x >= x0 - m.trimInsetStart - m.trimTouchOut && x < min(x0 - m.trimInsetStart + m.trimWidth, mid)
        let end = handles && over && x1 <= width && x > max(x1 - m.trimInsetEnd, mid) && x <= x1 - m.trimInsetEnd + m.trimWidth + m.trimTouchOut
        if key >= 0 && ((!start && !end) || (abs(keyX - x) <= m.keyGlyphHalf && x >= x0 && x <= x1)) { return TimelineHit(kind: .key, key: key) }
        if start { return TimelineHit(kind: .trimStart) }
        if end { return TimelineHit(kind: .trimEnd) }
        if over && x >= x0 && x <= x1 {
            if compact {
                let cl = contentLeft(m, x0, x1), cr = contentRight(m, x0, x1, width)
                if x >= cl - m.arrowTouchPad && x <= cl + m.arrowSlot + m.arrowTouchPad { return TimelineHit(kind: .previous) }
                if x >= cr - m.arrowSlot - m.arrowTouchPad && x <= cr + m.arrowTouchPad { return TimelineHit(kind: .next) }
            }
            return TimelineHit(kind: .body)
        }
        return TimelineHit(kind: .none)
    }
}

/// UIKit only arbitrates touch ownership. All coordinates remain in the same
/// point space as Canvas; it neither stores nor edits the project.
@MainActor
private struct TimelineGestureSurface: UIViewRepresentable {
    var tap: (CGPoint) -> Void
    var pan: (UIGestureRecognizer.State, CGPoint, CGPoint, CGPoint) -> Void
    var hold: (UIGestureRecognizer.State, CGPoint, CGPoint) -> Void
    var pinch: (UIGestureRecognizer.State, CGFloat, CGPoint) -> Void
    func makeCoordinator() -> Coordinator { Coordinator(self) }
    func makeUIView(context: Context) -> UIView {
        let view = UIView(); view.backgroundColor = .clear; view.isMultipleTouchEnabled = true
        #if DEBUG
        if ProcessInfo.processInfo.environment["AUREA_UI_TEST_PROBE"] == "1" {
            view.isAccessibilityElement = true
            view.accessibilityIdentifier = "aurea.parity.timeline"
            view.accessibilityLabel = "Aurea timeline gesture surface"
        }
        #endif
        let tap = UITapGestureRecognizer(target: context.coordinator, action: #selector(Coordinator.tapped(_:)))
        let pan = UIPanGestureRecognizer(target: context.coordinator, action: #selector(Coordinator.panned(_:)))
        pan.maximumNumberOfTouches = 1
        let hold = UILongPressGestureRecognizer(target: context.coordinator, action: #selector(Coordinator.held(_:)))
        hold.minimumPressDuration = 0.5; hold.allowableMovement = 8
        let pinch = UIPinchGestureRecognizer(target: context.coordinator, action: #selector(Coordinator.pinched(_:)))
        pan.require(toFail: hold); tap.require(toFail: pan); tap.require(toFail: hold)
        let recognizers: [UIGestureRecognizer] = [tap, pan, hold, pinch]
        for recognizer in recognizers { recognizer.delegate = context.coordinator; view.addGestureRecognizer(recognizer) }
        return view
    }
    func updateUIView(_ uiView: UIView, context: Context) { context.coordinator.parent = self }
    final class Coordinator: NSObject, UIGestureRecognizerDelegate {
        var parent: TimelineGestureSurface
        private var holdStart = CGPoint.zero
        init(_ parent: TimelineGestureSurface) { self.parent = parent }
        func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer, shouldRecognizeSimultaneouslyWith otherGestureRecognizer: UIGestureRecognizer) -> Bool {
            gestureRecognizer is UIPinchGestureRecognizer || otherGestureRecognizer is UIPinchGestureRecognizer
        }
        @objc func tapped(_ sender: UITapGestureRecognizer) {
            if sender.state == .ended { parent.tap(sender.location(in: sender.view)) }
        }
        @objc func panned(_ sender: UIPanGestureRecognizer) {
            let point = sender.location(in: sender.view), translation = sender.translation(in: sender.view)
            parent.pan(sender.state, CGPoint(x: point.x - translation.x, y: point.y - translation.y), point, sender.velocity(in: sender.view))
        }
        @objc func held(_ sender: UILongPressGestureRecognizer) {
            let point = sender.location(in: sender.view)
            if sender.state == .began { holdStart = point }
            parent.hold(sender.state, holdStart, point)
        }
        @objc func pinched(_ sender: UIPinchGestureRecognizer) { parent.pinch(sender.state, sender.scale, sender.location(in: sender.view)) }
    }
}
