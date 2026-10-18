"""Audit reference timeline geometry; run actual Swift math when swiftc exists.

Windows performs source-contract checks only. On macOS this extracts the
production metrics, time conversion and hit testing and executes regressions.
Neither mode is native SwiftUI or device validation.
"""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
APP = ROOT / "engine/platform/ios/app"


def block(source: str, header: str) -> str:
    start = source.index(header)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        depth += (source[index] == "{") - (source[index] == "}")
        if depth == 0:
            return source[start:index + 1]
    raise ValueError(f"Unclosed Swift block: {header}")


CASES = r'''
var failures = 0
func check(_ ok: Bool, _ message: String) {
    if !ok { failures += 1; print("FAILED: " + message) }
}
func near(_ a: CGFloat, _ b: CGFloat) -> Bool { abs(a - b) < 0.0001 }
for refreshed in [true, false] {
    EditorTimelineRefresh.enabled = refreshed
    let m = TimelineMetrics()
    if refreshed {
        check(near(m.rowsTop, 42) && near(m.row, 28) && near(m.bar, 23), "reference row geometry")
        check(near(m.barTop, 2.5) && near(m.headerColumn, 70) && near(m.barRadius, 1.5), "reference inset and header")
    }
    for compact in [true, false] {
        // Same coordinates as the reported screenshot: decorative cap starts
        // before x216; the real clip and its first animation anchor start x216.
        let start: Int32 = 30, end: Int32 = 31
        let ppf: CGFloat = 10, width: CGFloat = 432
        let x0 = TimeAxis.xOf(frame: Double(start), view: Double(start), pxPerFrame: ppf, centerX: width / 2)
        let x1 = TimeAxis.xOf(frame: Double(end), view: Double(start), pxPerFrame: ppf, centerX: width / 2)
        let key0 = Keyframes.toTimeline(0, start, 0)
        let keyX = TimeAxis.xOf(frame: Double(key0), view: Double(start), pxPerFrame: ppf, centerX: width / 2)
        check(near(x0, 216) && near(keyX, x0), "key0 is on temporal clip start")
        check(TimelineHit.capLeft(m, x0) < keyX, "decorative cap is before key0")
        check(near(x1, 226), "short clip end is not inflated to minimum painted width")
        check(near(x0 - m.trimInsetStart + m.trimWidth, x0), "start handle ends at body start")
        check(near(x1 - m.trimInsetEnd, x1), "end handle starts at body end")
        func hit(_ x: CGFloat, keys: [Int32]) -> TimelineHit.Kind {
            TimelineHit.test(m, point: CGPoint(x: x, y: m.bar / 2), width: width,
                x0: x0, x1: x1, handles: true, compact: compact, instants: keys,
                view: Double(start), ppf: ppf, header: m.headerColumn, eyeRight: m.eyeHitRight).kind
        }
        let paintedWidth = ClipGeometry.trimPaintWidth(clipWidth: x1 - x0, requested: m.trimWidth)
        let paintedCenter = x1 - ClipGeometry.trimPaintEndInset(clipWidth: x1 - x0, requested: m.trimWidth) + paintedWidth / 2
        check(hit(paintedCenter, keys: []) == .trimEnd, "visible narrow end grip trims instead of moving body")
        check(hit(x0 + (x1 - x0) / 4, keys: []) == .body, "short clip left half still moves body")
        check(hit(paintedCenter, keys: [start, end]) == .key, "boundary key still wins painted narrow end grip")
        check(hit(x0, keys: [start, end]) == .key, "start diamond wins on the exact clip edge")
        check(hit(x1, keys: [start, end]) == .key, "end diamond wins on the exact clip edge")
        check(hit(x0 - 15, keys: [start, end]) == .trimStart, "external start handle remains editable")
        check(hit(x1 + 15, keys: [start, end]) == .trimEnd, "external end handle remains editable")
        // The start grip has only five points before the fixed header. The
        // clip's temporal start still selects its key/body and cannot become
        // a fallback trim region inside the content.
        let crampedX = m.headerColumn + 5
        let crampedWidth = crampedX * 2
        let withKey = TimelineHit.test(m, point: CGPoint(x: crampedX, y: m.bar / 2), width: crampedWidth,
            x0: crampedX, x1: crampedX + 10, handles: true, compact: compact, instants: [start, end],
            view: Double(start), ppf: ppf, header: m.headerColumn, eyeRight: m.eyeHitRight)
        let withoutKey = TimelineHit.test(m, point: CGPoint(x: crampedX, y: m.bar / 2), width: crampedWidth,
            x0: crampedX, x1: crampedX + 10, handles: true, compact: compact, instants: [],
            view: Double(start), ppf: ppf, header: m.headerColumn, eyeRight: m.eyeHitRight)
        check(withKey.kind == .key, "cramped first diamond wins over trim")
        check(withoutKey.kind == .body, "cramped body start does not become trim")
        let bodyX = TimeAxis.xOf(frame: Double(Keyframes.toTimeline(12, start, 12)), view: Double(start), pxPerFrame: ppf, centerX: width / 2)
        check(near(bodyX, x0), "trim offset maps the first visible anchor to body start")
    }
}
EditorTimelineRefresh.enabled = true
let large = TimelineMetrics(fontScale: 2)
check(large.barTop + large.bar <= large.row, "large text preserves row separation")
check(Zoom.defaultPPS == 100, "reference initial zoom")
let laneKeys = visibleLaneKeys([
    KeyframeItem(property: 0, effectIndex: .max, paramIndex: 0, time: 0, timelineVisible: false),
    KeyframeItem(property: 0, effectIndex: .max, paramIndex: 0, time: 12, timelineVisible: true),
    KeyframeItem(property: 0, effectIndex: .max, paramIndex: 0, time: 13, timelineVisible: true),
])
check(laneKeys.map { $0.time }.sorted() == [12, 13], "expanded lanes hide trimmed keys and keep inclusive end")
check(denseFocusOrder([0, 90, 0, 1, Snap.none]) == [90, 0, 1], "dense adjacent selected key paints after start anchor")
check(denseFocusOrder([0, 90, 1, 0, Snap.none]) == [90, 1, 0], "selected boundary keeps its last occurrence")
check(denseFocusOrder([0, 90, 1, 1, 0]) == [90, 1, 0], "dragged boundary paints last without duplicates")
let selectedX = TimeAxis.xOf(frame: 1, view: 0, pxPerFrame: 0.02, centerX: 216)
check(near(selectedX, 216.02), "dense selected anchor stays on x(time) without a visual offset")
for duration in [1, 2, 5, 30] {
    for density in [CGFloat(1), CGFloat(2.625)] {
        for fps in [Float(23.976), Float(30), Float(60)] {
            for view in [119.625, 120.0, 120.375] {
                for zoom in [CGFloat(800), 100, 20, 2, 20, 100, 800] {
                    let ppf = TimeAxis.pxPerFrame(pps: zoom, density: density, fps: fps)
                    let x0 = TimeAxis.xOf(frame: Double(120 - duration), view: view, pxPerFrame: ppf, centerX: 216 * density)
                    let x1 = TimeAxis.xOf(frame: 120, view: view, pxPerFrame: ppf, centerX: 216 * density)
                    let marker = TimeAxis.xOf(frame: 120, view: view, pxPerFrame: ppf, centerX: 216 * density)
                    check(near(x1, marker), "zoom cycle keeps short clip end on beat")
                    check(abs((x1 - x0) - CGFloat(duration) * ppf) < 0.0002 * density, "zoom cycle keeps exact scaled duration")
                    let stroke = ClipGeometry.strokeWidth(width: x1 - x0, height: 23 * density, requested: 2 * density)
                    let radius = ClipGeometry.cornerRadius(width: x1 - x0, height: 23 * density, requested: 14 * density)
                    check(stroke <= x1 - x0 && radius * 2 <= x1 - x0, "stroke and corners cannot inflate duration")
                    let handleWidth = ClipGeometry.trimPaintWidth(clipWidth: x1 - x0, requested: 16 * density)
                    let handleLeft = x1 - ClipGeometry.trimPaintEndInset(clipWidth: x1 - x0, requested: 16 * density)
                    if x1 - x0 < 32 * density {
                        check(near(handleLeft + handleWidth, marker), "narrow painted end grip stops on beat")
                    }
                    check(timelineFrame(TimeAxis.frameAt(x: x1, view: view, pxPerFrame: ppf, centerX: 216 * density)) == 120, "end remains temporal beat")
                }
            }
        }
    }
}
if failures > 0 { exit(1) }
print("check_timeline_reference: Swift geometry and hit-test regressions OK")
'''


def main() -> int:
    model = (APP / "TimelineModel.swift").read_text(encoding="utf-8")
    math = (APP / "TimelineMath.swift").read_text(encoding="utf-8")
    view = (APP / "TimelineView.swift").read_text(encoding="utf-8")
    theme = (APP / "Theme.swift").read_text(encoding="utf-8")
    animation = (APP / "AnimationTools.swift").read_text(encoding="utf-8")
    checks = {
        "exact temporal end in painter and hit test": "barMinWidth" not in model and "barMinWidth" not in view,
        "selection decoration is clipped to actual span": "outline.clip(to: Path(rect))" in view,
        "custom corners fit narrow visible fragments": "ClipGeometry.cornerRadius(width: rect.width" in view,
        "narrow visible end grip owns matching padded hit target": "x > max(x1 - ClipGeometry.trimPaintEndInset(clipWidth: x1 - x0, requested: m.trimWidth), mid)" in view,
        "summary uses shared visible-key flag": "all.filter { $0.timelineVisible }" in model,
        "expanded lanes use shared visible-key flag": "(keys[owner.id] ?? []).filter { $0.timelineVisible }" in model,
        "diamond uses temporal x": "let px = x(Double(row.instants[i]), width: width)" in view,
        "legacy cap ends at real clip start": "capR = min(x0, right)" in view,
        "start handle outside clip": "trimInsetStart: CGFloat { trimWidth }" in model,
        "end handle outside clip": "trimInsetEnd: CGFloat { dp(0) }" in model,
        "navigation supplied by core": "model.engine.keyframeNavigation" in view,
        "dense clusters preserve edge anchors": "let focus = [row.start, row.end," in view,
        "dense focus dedup preserves last occurrence": "!focus.dropFirst(position + 1).contains(frame)" in view,
        "key edge wins before trim": view.index("if key >= 0 { return TimelineHit") < view.index("if start { return TimelineHit"),
        "curve graph uses shared segment": "let query = navigation(local: selected)" in animation,
        "curve graph excludes hidden keys": "$0.timelineVisible && $0.property == model.curveProperty" in animation,
    }
    for label, valid in checks.items():
        if not valid:
            print("FAILED: " + label)
            return 1
    print(f"check_timeline_reference: {len(checks)} source-contract checks OK")
    swiftc = shutil.which("swiftc")
    if not swiftc:
        print("No swiftc: Swift regressions NOT executed; native iOS/device validation NOT performed.")
        return 0
    metrics = block(model, "struct TimelineMetrics {")
    dense_gate = re.search(r"guard (!focus\.[^,]+), Keyframes\.groupHas\(row\.instants", view)
    if not dense_gate:
        raise ValueError("Missing production dense-focus dedup predicate")
    fields = sorted(set(re.findall(r"AureaTimeline\.(\w+)", metrics)))
    constants = []
    for field in fields:
        value = re.search(rf"static let {field}(?:: CGFloat)?\s*=\s*([0-9.]+)", theme)
        if not value:
            raise ValueError(f"Missing numeric timeline token: {field}")
        constants.append(f"static let {field}: CGFloat = {value[1]}")
    program = "\n\n".join([
        "import CoreGraphics\nimport Foundation",
        "enum AureaTimeline { " + "; ".join(constants) + " }",
        "enum EditorTimelineRefresh { static var enabled = true }",
        metrics,
        block(math, "@inline(__always) func timelineFrame"),
        block(math, "@inline(__always) func lowerBound"),
        block(math, "enum TimeAxis {"),
        block(math, "enum ClipGeometry {"),
        block(math, "enum Zoom {"),
        block(math, "enum Snap {"),
        block(math, "enum Keyframes {"),
        block(view, "private struct TimelineHit {"),
        "struct TimelineTrack: Hashable { var property: Int; var effect: UInt32; var param: UInt32 }",
        "struct KeyframeItem { var property: UInt32; var effectIndex: UInt32; var paramIndex: UInt32; var time: Int32; var timelineVisible: Bool }",
        "struct LaneOwner { var id: Int64 }",
        "func visibleLaneKeys(_ list: [KeyframeItem]) -> [KeyframeItem] {\n"
        "let owner = LaneOwner(id: 1)\nlet keys: [Int64: [KeyframeItem]] = [1: list]\n" +
        next(line.strip() for line in model.splitlines() if "let tracks = Dictionary(grouping: (keys[owner.id]" in line) +
        "\nreturn tracks.values.flatMap { $0 }\n}",
        "func denseFocusOrder(_ focus: [Int32]) -> [Int32] {\nvar order: [Int32] = []\n"
        "for (position, frame) in focus.enumerated() {\nguard " + dense_gate[1] +
        ", frame != Snap.none else { continue }\norder.append(frame)\n}\nreturn order\n}",
        CASES,
    ])
    with tempfile.TemporaryDirectory() as temporary:
        source = Path(temporary) / "main.swift"
        executable = Path(temporary) / "timeline-reference"
        source.write_text(program, encoding="utf-8")
        result = subprocess.run([swiftc, str(source), "-o", str(executable)])
        if result.returncode:
            return result.returncode
        return subprocess.run([str(executable)]).returncode


if __name__ == "__main__":
    sys.exit(main())
