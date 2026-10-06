"""Check the six-field iOS bridge; exercise actual Swift controls when swiftc exists.

Source checks on Windows do not compile the app or execute SwiftUI/Metal.
The extracted wrapper test records native calls, not a replacement blur renderer.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
IOS = ROOT / "engine/platform/ios"
model = (IOS / "app/AureaModel.swift").read_text(encoding="utf-8")
view = (IOS / "app/TransformView.swift").read_text(encoding="utf-8")
bridge = (IOS / "bridge/AureaEngine.mm").read_text(encoding="utf-8")


def block(source, header):
    start = source.index(header)
    depth = 0
    for cursor in range(source.index("{", start), len(source)):
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        if depth == 0:
            return source[start:cursor + 1]
    raise ValueError("Unclosed block: " + header)


query = block(bridge, "- (NSArray<NSNumber*>*)motionBlurSettings")
fields = ["enabled", "shutterAngle", "shutterPhase", "samples", "adaptiveLimit", "previewSamples"]
assert "query_motion_blur_settings(settings)" in query
assert [query.index("@(settings." + field + ")") for field in fields] == sorted(
    query.index("@(settings." + field + ")") for field in fields)
old_setter = block(bridge, "- (void)setMotionBlurSettings:")
assert old_setter.count("set_motion_blur_settings(") == 1
assert "set_composition_motion_blur(" not in old_setter and "set_shutter_angle(" not in old_setter
setter = block(bridge, "- (BOOL)setMotionBlurSettings:")
assert "set_motion_blur_settings(enabled, shutter, phase, samples, adaptiveLimit)" in setter
face = block(view, "private var motionBlurFace:")
assert "/ 3.6" not in face and "DisclosureGroup" in face
assert "layerMotionBlurLength" in face and "setVectorBlur" in face
disclosure_label = block(face, '} label:')
assert '.accessibilityIdentifier("motionblur.advanced")' in disclosure_label
assert face.count('.accessibilityIdentifier("motionblur.advanced")') == 1
# Android already scopes its test tag to AdvancedToggle, separate from children.
android_view = (ROOT / "android/app/src/main/java/com/aurea/aurea/editor/panels/TransformPanel.kt").read_text(encoding="utf-8")
for identifier in ("advanced", "center"):
    assert android_view.count('.testTag("motionblur.' + identifier + '")') == 1
for identifier in ("shutter", "phase", "samples", "adaptive", "center"):
    assert '"motionblur.' + identifier + '"' in view
row = block(view, "private func motionBlurRow(")
assert "accessibilityAdjustableAction" in row and "accessibilityValue(shown)" in row
assert "minHeight: 44" in row and "keypad(label" in row
print("PASS: atomic six-field bridge, degree controls, advanced bounds and accessibility source contracts")

swiftc = shutil.which("swiftc")
if not swiftc:
    print("SKIP: swiftc unavailable; Swift wrapper cases, native UI and renderer NOT executed")
else:
    methods = ["var motionBlurControls:", "private func writeMotionBlur(", "func setCompositionMotionBlur(",
               "func changeShutterAngle(", "func changeShutterPhase(", "func centerMotionBlurExposure()",
               "func changeMotionBlurSamples(", "func changeMotionBlurAdaptiveLimit("]
    harness = r'''
import Foundation
CONTROLS
final class RecordingNative {
    var values: [NSNumber] = [1, 181, 45, 16, 128, 12]
    var writes = 0
    func motionBlurSettings() -> [NSNumber] { values }
    func setMotionBlurSettings(_ enabled: Bool, shutter: Float, phase: Float,
                               samples: UInt32, adaptiveLimit: UInt32) -> Bool {
        writes += 1
        values = [NSNumber(value: enabled), NSNumber(value: shutter), NSNumber(value: phase),
                  NSNumber(value: samples), NSNumber(value: adaptiveLimit), values[5]]
        return true
    }
}
final class Subject {
    let engine = RecordingNative()
    func refreshModel(force: Bool) {}
    METHODS
}
let subject = Subject()
let initial = subject.motionBlurControls
precondition(initial.enabled && initial.angle == 181 && initial.phase == 45 && initial.samples == 16
             && initial.adaptiveLimit == 128 && initial.previewSamples == 12, "snapshot field order")
subject.centerMotionBlurExposure()
precondition(subject.motionBlurControls.phase == -90.5 && subject.engine.writes == 1, "one center write, half-degree precision")
subject.changeShutterAngle(720)
precondition(subject.motionBlurControls.angle == 720 && subject.motionBlurControls.phase == -90.5, "phase must remain independent")
subject.centerMotionBlurExposure()
precondition(subject.motionBlurControls.phase == -360, "center endpoint")
subject.changeShutterPhase(-999)
precondition(subject.motionBlurControls.phase == -360, "phase bounded")
subject.changeMotionBlurAdaptiveLimit(8)
precondition(subject.motionBlurControls.samples == 16 && subject.motionBlurControls.adaptiveLimit == 16, "limit respects minimum without editing it")
subject.changeMotionBlurSamples(64)
precondition(subject.motionBlurControls.samples == 16 && subject.motionBlurControls.adaptiveLimit == 16, "minimum respects limit without editing it")
subject.changeMotionBlurAdaptiveLimit(999)
subject.changeMotionBlurSamples(15.6)
precondition(subject.motionBlurControls.adaptiveLimit == 256 && subject.motionBlurControls.samples == 16, "integer quality bounds")
subject.setCompositionMotionBlur(false)
precondition(!subject.motionBlurControls.enabled && subject.motionBlurControls.phase == -360
             && subject.motionBlurControls.previewSamples == 12, "toggle preserves phase and read-only preview cap")
let writes = subject.engine.writes
subject.changeShutterAngle(.nan); subject.changeShutterPhase(.infinity)
subject.changeMotionBlurSamples(.infinity); subject.changeMotionBlurAdaptiveLimit(.nan)
precondition(subject.engine.writes == writes, "invalid UI numbers never reach native")
precondition(MotionBlurControls([]) == MotionBlurControls(), "missing snapshot")
precondition(MotionBlurControls([1, 180, -90, 16, 8, 16]) == MotionBlurControls(), "invalid count ordering")
precondition(MotionBlurControls([1, 180, NSNumber(value: Float.nan), 16, 128, 16]) == MotionBlurControls(), "non-finite snapshot")
print("PASS: actual Swift snapshot and mutation wrappers preserve units, independent phase, limits and single writes")
'''
    harness = harness.replace("CONTROLS", block(model, "struct MotionBlurControls:"))
    harness = harness.replace("METHODS", "\n".join(block(model, method) for method in methods))
    with tempfile.TemporaryDirectory(prefix="aurea-motion-blur-") as directory:
        source = Path(directory) / "main.swift"
        output = Path(directory) / "motion-blur"
        source.write_text(harness, encoding="utf-8")
        subprocess.run([swiftc, str(source), "-o", str(output)], check=True)
        subprocess.run([str(output)], check=True)
