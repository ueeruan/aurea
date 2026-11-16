"""Check mobile editing contracts and run the production clamp when Swift exists.

Source checks and isolated Swift math do not validate SwiftUI on an iOS device.
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

APP = Path(__file__).resolve().parent.parent / "app"


def block(source: str, header: str) -> str:
    start = source.index(header)
    depth = 0
    for cursor in range(source.index("{", start), len(source)):
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        if depth == 0:
            return source[start:cursor + 1]
    raise ValueError(f"Unclosed block {header}")


def main() -> int:
    source = (APP / "EditModeView.swift").read_text(encoding="utf-8")
    editor = (APP / "EditorView.swift").read_text(encoding="utf-8")
    toolbar = (APP / "ToolbarView.swift").read_text(encoding="utf-8")
    layer_bar = block(toolbar, "private func layerBar(")
    scene = block(editor, "private struct SceneLayoutWorkspace: View")
    scene_timeline = block(editor, "private struct Scene3DStudioBottom: View")
    more = block(source, "private var moreSheet: some View")
    timeline = block(source, "private struct EditTimelineView: View")
    checks = {
        "mobile categories are contextual in the bottom dock": '"edit.workspace.' not in source
        and '.accessibilityIdentifier("edit.dock")' in source,
        "the default timeline keeps main, populated and explicitly revealed tracks": "let used = Set(model.layers.map(\\.trackId))" in timeline
        and "used.contains($0.id) || $0.id == main || $0.id == revealedTrack" in timeline,
        "advanced tools remain reachable through More": ".sheet(isPresented: $showMore)" in timeline
        and 'id: "edit.tool.\\(kind.rawValue)"' in more,
        "track controls remain reachable through More": '"edit.track.\\(track.label).target"' in more,
        "new empty tracks are explicitly revealed": all(
            f"revealedTrack = model.addEditTrack(audio: {audio})" in more for audio in ["false", "true"]
        ),
        "clips use the timeline center as the temporal origin": "size.width / 2 + (CGFloat(clip.row.startFrame) - scrollFrames)" in source,
        "the playhead stays centered while content moves": "let playX = centerX" in source
        and "centerX: size.width / 2" in source,
        "playback follows the centered absolute frame": "scrollFrames = clampedScroll(CGFloat(frame), width: width)" in source,
        "the sequence prefix follows temporal content": "size.width / 2 - scrollFrames * pxPerFrame - sequencePrefixWidth" in source,
        "add stays on the trailing edge with an accessible touch target": '.accessibilityIdentifier("edit.add")' in source
        and ".frame(width: 36, height: 36)" in source
        and ".frame(width: 44, height: 44).contentShape(Rectangle())" in source
        and "alignment: .topTrailing)" in source,
        "the default editor preserves preview and transport entry points": all(
            f'.accessibilityIdentifier("edit.{key}")' in source for key in ["preview", "clock", "play"]
        ),
        "the mobile scene owns one preview surface": scene.count("PreviewStage(") == 1,
        "the mobile scene keeps hierarchy behind its toggle": "if hierarchyOpen {" in scene
        and '.accessibilityIdentifier("scene.hierarchy.panel")' in scene,
        "hierarchy rows address real layer IDs": '"scene.hierarchy.row.\\($0.id)"' in editor
        and '"scene.hierarchy.visible.\\(object.id)"' in editor,
        "the mobile scene uses its contextual dock": "private var mobileDock: some View" in scene
        and "if model.selectedLayer == nil {" in scene,
        "numeric transform panels require a real selected object": "if transformOpen && model.selectedLayer != nil" in scene
        and '.accessibilityIdentifier("scene.transform.panel")' in scene,
        "numeric scene edits reach the model with project and selection validation": "model.projectGeneration == project, model.primarySelection == id" in scene
        and "model.setTransform(property + UInt32(axis), value: amount, layer: id)" in scene,
        "animation expands without a permanent desktop toolbar": "showToolbar: false, showGraph: expanded" in scene
        and '.accessibilityIdentifier("scene.timeline.expand")' in scene
        and "Scene3DToolbar(" not in scene,
        "scene transport keeps its shared entry points": all(
            f'.accessibilityIdentifier("scene.{key}")' in scene for key in ["key", "transport.play", "dock.back"]
        ),
        "compact scene timeline keeps a fixed temporal scale": "compactPointsPerSecond: CGFloat = 72" in scene_timeline
        and "centerFrame) * compactPixelsPerFrame" in scene_timeline,
        "compact scene gestures keep their original frame basis": "gestureOriginFrame ?? 0) - translation / compactPixelsPerFrame" in scene_timeline
        and "compactCenterFrame = frame" in scene_timeline,
        "compact scene key movement preserves the grabbed key offset": "frameAt(keyX(grabbedFrame, end: end, width: width) + g.translation.width" in scene_timeline,
        "compact scene head is fixed at the panel center": "showGraph ? keyX(frame, end: end, width: width) : compactHeadX(width: width)" in scene_timeline,
        "composition keeps 3D and motion blur directly in the selected layer header": all(
            f'.accessibilityIdentifier("trial.layer.{key}")' in layer_bar for key in ["3d", "motionBlur"]
        ),
        "composition header 3D and motion blur reach real engine flags": "model.engine.setLayer3D(!threeD, forLayer: row.id)" in layer_bar
        and "setMotionBlur(!motion, forLayer: row.id)" in layer_bar,
        "composition motion blur keeps panel access and its accessible toggle": 'model.openPanel(.transform)' in layer_bar
        and '.accessibilityAction(named: Text(AureaText.t("fn_motion_blur_toggle")))' in layer_bar,
    }
    failed = [label for label, valid in checks.items() if not valid]
    for label in failed:
        print("FAILED " + label)
    if failed:
        return 1
    print(f"check_mobile_editor_ui {len(checks)} source contracts passed")
    compiler = shutil.which("swiftc")
    if not compiler:
        print("Swift compiler unavailable. Clamp execution and native iOS validation remain pending.")
        return 0
    clamp = block(source, "private func clampedScroll(").replace("private func", "func", 1)
    scene_properties = [
        block(scene_timeline, header).replace("private ", "", 1)
        for header in [
            "private var labels: CGFloat",
            "private var compactPixelsPerFrame: CGFloat",
            "private var centerFrame: CGFloat",
        ]
    ]
    scene_constants = [
        line.strip().replace("private ", "", 1)
        for line in scene_timeline.splitlines()
        if "private static let compactPointsPerSecond:" in line or "private static let pad:" in line
    ]
    scene_methods = [
        block(scene_timeline, header).replace("private func", "func", 1)
        for header in ["private func compactHeadX(", "private func keyX(", "private func frameAt(", "private func panCompactTimeline("]
    ]
    harness = """import Foundation
struct Model { var compositionDuration: Int64 = 300 }
struct Subject {
    var model = Model()
    var headW: CGFloat = 0
    var pxPerFrame: CGFloat = 2
    CLAMP
}
for duration in [Int64(-1), 0, 1, 300] {
    var subject = Subject()
    subject.model.compositionDuration = duration
    for width in [CGFloat(320), 844] {
        for offset in [CGFloat(-450), 0, 0.5, 150.5, 450] {
            let expected = min(max(0, offset), CGFloat(max(0, duration)))
            precondition(subject.clampedScroll(offset, width: width) == expected)
        }
        precondition(subject.clampedScroll(.nan, width: width) == 0)
        precondition(subject.clampedScroll(.infinity, width: width) == 0)
    }
    precondition(subject.clampedScroll(10, width: 0) == 0)
    subject.pxPerFrame = 0
    precondition(subject.clampedScroll(10, width: 320) == 0)
}
print("Production centered clamp regressions passed")
final class SceneModel {
    struct Status { var playhead: Int64 = 150 }
    var status = Status()
    var compositionFps: Double = 30
    var commitSeeks = true
    var pendingFrame: Int64?
    func seek(toFrame frame: Int64) {
        pendingFrame = frame
        if commitSeeks { status.playhead = frame }
    }
}
final class SceneSubject {
    var model = SceneModel()
    var showGraph = false
    var wide = false
    var gestureOriginFrame: CGFloat?
    var compactCenterFrame: CGFloat?
    SCENE_BODY
}
for outerWidth in [CGFloat(320), 600, 1024] {
    for wide in [false, true] {
        let subject = SceneSubject()
        subject.wide = wide
        let width = outerWidth - subject.labels - 1
        let head = subject.compactHeadX(width: width)
        precondition(abs(subject.labels + 1 + head - outerWidth / 2) < 0.0001)
        for fps in [23.976, 30.0, 60.0] {
            subject.model.compositionFps = fps
            precondition(abs(subject.compactPixelsPerFrame * CGFloat(fps) - 72) < 0.0001)
            for frame in [0, 1, 150, 299] {
                let x = subject.keyX(frame, end: 299, width: width)
                precondition(subject.frameAt(x, end: 299, width: width) == frame)
            }
        }
    }
}
for commit in [true, false] {
    let subject = SceneSubject()
    subject.model.commitSeeks = commit
    subject.panCompactTimeline(translation: 24, end: 299)
    precondition(subject.model.pendingFrame == 140)
    subject.panCompactTimeline(translation: 48, end: 299)
    precondition(subject.model.pendingFrame == 130)
    subject.panCompactTimeline(translation: -100000, end: 299)
    precondition(subject.model.pendingFrame == 299)
    subject.panCompactTimeline(translation: 100000, end: 299)
    precondition(subject.model.pendingFrame == 0)
}
print("Production compact scene scale, centered head, inverse mapping and pan regressions passed")
""".replace("CLAMP", clamp)
    harness = harness.replace("SCENE_BODY", "\n".join(scene_properties + scene_constants + scene_methods))
    with tempfile.TemporaryDirectory(prefix="aurea-mobile-editor-") as temporary:
        script = Path(temporary) / "main.swift"
        executable = Path(temporary) / "mobile-editor"
        script.write_text(harness, encoding="utf-8")
        built = subprocess.run([compiler, str(script), "-o", str(executable)])
        if built.returncode:
            return built.returncode
        return subprocess.run([str(executable)]).returncode


if __name__ == "__main__":
    sys.exit(main())
