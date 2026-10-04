"""Check project guards and execute the actual rename wrapper when Swift is available."""
import pathlib
import re
import shutil
import subprocess
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
source = (HERE.parent / "app/AureaModel.swift").read_text(encoding="utf-8")


def method(signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


for signature in ["func newProject(width:", "func open(_ project:", "func closeProject()"]:
    assert "guard canChangeProject()" in method(signature), signature
assert "!projectOperations.isEmpty" in method("private func canChangeProject()")
creation = method("func createFromMedia(url:")
assert creation.index("mediaCreationRequest == request") < creation.index("guard newProject(width:")
rename = method("func renameCurrentProject(to")
assert "projectURL =" not in rename, "failed rename must not redirect autosave to another document"
assert "projectName =" not in rename
beats = method("private func finishBeatDetection(")
beat_start = method("func detectBeats()")
assert "guard started, !importingMedia" in beat_start
assert "beatDetectionRequest = request" in beat_start
assert beats.index("beatDetectionRequest == request") < beats.index("importingMedia = false")
assert beats.index("projectGeneration == project") < beats.index("importingMedia = false")
assert beats.index("guard started,") < beats.index("refreshModel(force: true)")
assert "uint64Value == compositionID" in beats
assert "bpm.isFinite, bpm >= 0" in beats and "Int32(exactly: bpm.rounded())" in beats
engine_root = HERE.parents[2]
cmake = (engine_root / "CMakeLists.txt").read_text(encoding="utf-8")
boundaries = re.search(r"set\(AUREA_IMPORT_BOUNDARIES\s+(.*?)\)", cmake, re.S)
assert boundaries and "src/audio/Beats.cpp" in boundaries.group(1) and "src/engine/Engine.cpp" in boundaries.group(1)
assert "${AUREA_IMPORT_BOUNDARIES} PROPERTIES COMPILE_OPTIONS -fexceptions" in cmake
beat_header = (engine_root / "include/aurea/audio/Beats.hpp").read_text(encoding="utf-8")
assert not re.search(r"BeatResult detect_beats\([^;]+noexcept", beat_header)
print("PASS: project replacement, media identity, failure-safe rename and beat callback cleanup source contracts")
print("PASS: beat allocation boundary retains C++ unwind through the core CMake target used by iOS")

compiler = shutil.which("swiftc")
if not compiler:
    print("SKIP: swiftc unavailable; rename/beat callback regressions NOT compiled/executed. Static checks are not native validation.")
else:
    harness = '''import Foundation
struct ProjectFile { let url: URL; let name: String; let modified: Date; let sizeBytes: Int64 }
final class Subject {
    var projectURL: URL? = URL(fileURLWithPath: "/documents/original.aurea")
    var projectName = "original"
    var failRename = true
    func rename(_ project: ProjectFile, to name: String) {
        if failRename { return }
        projectURL = project.url.deletingLastPathComponent().appendingPathComponent(name + ".aurea")
        projectName = name
    }
    METHOD
}
let subject = Subject()
let original = subject.projectURL
subject.renameCurrentProject(to: "existing")
precondition(subject.projectURL == original && subject.projectName == "original",
             "failed rename changed the autosave destination")
subject.failRename = false
subject.renameCurrentProject(to: "renamed")
precondition(subject.projectURL?.lastPathComponent == "renamed.aurea" && subject.projectName == "renamed")
print("PASS: failed rename preserves document identity; successful rename follows committed destination")
'''.replace("METHOD", rename)
    harness += '''
let AureaCompositionId = "id"
enum AureaText {
    static func t(_ key: String, _ values: Any...) -> String {
        key + ":" + values.map { String(describing: $0) }.joined(separator: ",")
    }
}
final class Beats {
    var projectGeneration = UUID(), beatDetectionRequest: UUID? = UUID()
    var composition: [String: Any] = [AureaCompositionId: NSNumber(value: 7)]
    var started = true, importingMedia = true, refreshes = 0, markers = 0, saves = 0
    var toast: String?
    func refreshModel(force: Bool) { refreshes += 1 }
    func refreshMarkers() { markers += 1 }
    func saveProject(writeThumbnail: Bool) -> Bool { saves += 1; return true }
    BEAT_METHOD
}
func finish(_ target: Beats, _ count: Int64 = 2, _ bpm: Double = 123.5) {
    target.finishBeatDetection(request: target.beatDetectionRequest!, project: target.projectGeneration,
                               compositionID: 7, count: count, bpm: bpm)
}
let valid = Beats(); finish(valid)
precondition(!valid.importingMedia && valid.saves == 1 && valid.toast == "msg_batidas_bpm:2,124")
for bpm in [Double.nan, Double.infinity, -1, Double(Int32.max) + 1] {
    let invalid = Beats(); finish(invalid, 2, bpm)
    precondition(!invalid.importingMedia && invalid.saves == 0 && invalid.toast == "msg_nao_foi_possivel_analisar_o_som:10")
}
let oldRequest = Beats(), obsolete = UUID()
oldRequest.finishBeatDetection(request: obsolete, project: oldRequest.projectGeneration, compositionID: 7, count: 2, bpm: 120)
precondition(oldRequest.importingMedia && oldRequest.refreshes == 0 && oldRequest.beatDetectionRequest != nil)
let oldProject = Beats()
oldProject.finishBeatDetection(request: oldProject.beatDetectionRequest!, project: UUID(), compositionID: 7, count: 2, bpm: 120)
precondition(oldProject.importingMedia && oldProject.refreshes == 0 && oldProject.toast == nil)
let stopped = Beats(); stopped.started = false; finish(stopped)
precondition(!stopped.importingMedia && stopped.refreshes == 0 && stopped.saves == 0)
let anotherComp = Beats(); anotherComp.composition = [AureaCompositionId: NSNumber(value: 9)]; finish(anotherComp)
precondition(!anotherComp.importingMedia && anotherComp.refreshes == 0 && anotherComp.saves == 0)
let failure = Beats(); finish(failure, -10)
precondition(!failure.importingMedia && failure.saves == 0 && failure.toast == "msg_nao_foi_possivel_analisar_o_som:10")
print("PASS: actual beat callback rejects stale requests/projects/compositions and invalid BPM; cleanup survives failure/stop")
'''.replace("BEAT_METHOD", beats.replace("private func", "func", 1))
    with tempfile.TemporaryDirectory(prefix="aurea-project-check-") as directory:
        main = pathlib.Path(directory) / "main.swift"
        main.write_text(harness, encoding="utf-8")
        exe = str(pathlib.Path(directory) / "project-check")
        subprocess.run([compiler, str(main), "-o", exe], check=True)
        subprocess.run([exe], check=True)
