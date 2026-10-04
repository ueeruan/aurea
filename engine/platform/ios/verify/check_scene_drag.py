"""Exercise the actual Swift delta wrapper with a delayed native queue when Swift exists."""
import pathlib
import shutil
import subprocess
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
source = (HERE.parent / "app/AureaModel.swift").read_text(encoding="utf-8")


def block(signature):
    start = source.index(signature)
    cursor = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[start:cursor]


drag = block("func sceneDragObject(dx:")
assert "gizmoMoveLocal" not in drag
assert "snapshot.dx += dx" in drag and "snapshot.previous = next" in drag
assert "projectGeneration" in drag and "compositionID" in drag and "status.playhead" in drag
assert "sceneDragSnapshot = nil" in block("func beginGesture(")
assert "sceneDragSnapshot = nil" in block("func endGesture()")
print("PASS: scene delta wrapper captures shared basis, owns queued targets and resets by gesture/context")

compiler = shutil.which("swiftc")
if not compiler:
    print("SKIP: swiftc unavailable; delayed-queue wrapper regression NOT compiled/executed")
else:
    harness = '''import Foundation
let AureaCompositionId = "id"
final class Native {
    var position: [Float] = [0, 0, 0]
    var pending: [[Float]] = []
    func previewGestureBasis(_ id: Int64) -> [NSNumber] {
        (position + [2, 1, -0.5, 0, 1, 0, 0, 0, 0, 1]).map { NSNumber(value: $0) }
    }
    func previewGestureValue(_ b: [NSNumber], dx: Float, dy: Float, rotate: Bool) -> [NSNumber] {
        (0..<3).map { NSNumber(value: b[$0].floatValue + dx * b[3+$0].floatValue + dy * b[6+$0].floatValue) }
    }
    func flush() { if let last = pending.last { position = last }; pending = [] }
    func beginUndoGroup() {}
    func endUndoGroup() {}
    func run(_ body: (Native) -> Void) { body(self) }
}
final class Subject {
    struct Status { var playhead: Int64 = 30 }
    var status = Status()
    var primarySelection: Int64? = 1
    var composition: [String: Any] = [AureaCompositionId: NSNumber(value: 1)]
    var projectGeneration = UUID()
    let engine = Native()
    var sceneDragSnapshot: SceneDragSnapshot?
    var transformGestureDepth = 0
    SNAPSHOT
    func refreshModel(force: Bool) {}
    func applyGizmoComponents(_ id: Int64, base: UInt32, previous: [Float], next: [Float]) {
        if previous != next { engine.pending.append(next) }
    }
    BEGIN
    END
    DRAG
}
func move(_ subject: Subject, flushEach: Bool) -> [Float] {
    subject.beginGesture("scene")
    for _ in 0..<8 { subject.sceneDragObject(dx: 4, dy: 0); if flushEach { subject.engine.flush() } }
    subject.endGesture(); subject.engine.flush()
    return subject.engine.position
}
let slow = move(Subject(), flushEach: true)
let fast = move(Subject(), flushEach: false)
precondition(slow == fast && fast == [64, 32, -16], "event batching lost deltas")
let returned = Subject()
returned.beginGesture("roundtrip")
for _ in 0..<8 { returned.sceneDragObject(dx: 4, dy: 0) }
for _ in 0..<8 { returned.sceneDragObject(dx: -4, dy: 0) }
returned.endGesture(); returned.engine.flush()
precondition(returned.engine.position == [0, 0, 0], "queued return to origin was omitted")
returned.beginGesture("new session")
returned.sceneDragObject(dx: 4, dy: 0); returned.engine.flush()
returned.projectGeneration = UUID(); returned.engine.position = [50, 50, 50]
returned.sceneDragObject(dx: 1, dy: 0); returned.engine.flush(); returned.endGesture()
precondition(returned.engine.position == [52, 51, 49.5], "old project gesture base survived")
print("PASS: actual Swift wrapper preserves batched deltas, reverse gesture and project identity")
'''
    harness = harness.replace("SNAPSHOT", block("private struct SceneDragSnapshot").replace("private struct", "struct"))
    harness = harness.replace("BEGIN", block("func beginGesture("))
    harness = harness.replace("END", block("func endGesture()"))
    harness = harness.replace("DRAG", drag)
    with tempfile.TemporaryDirectory(prefix="aurea-scene-drag-") as directory:
        main = pathlib.Path(directory) / "main.swift"
        main.write_text(harness, encoding="utf-8")
        executable = str(pathlib.Path(directory) / "scene-drag")
        subprocess.run([compiler, str(main), "-o", executable], check=True)
        subprocess.run([executable], check=True)
