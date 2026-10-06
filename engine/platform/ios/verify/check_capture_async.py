"""Audit native capture call sites; run the real Swift wrappers when swiftc exists."""
import pathlib
import shutil
import subprocess
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[3]
source = (HERE.parent / "app/AureaModel.swift").read_text(encoding="utf-8")


def method(signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


capture = method("func capturePreviewFrame(")
thumbnail = method("private func writeProjectThumbnail(")
assert source.count(".captureFrame(") == 1
assert capture.index("mediaQueue.async") < capture.index("native.captureFrame(")
assert "projectContentGeneration == project, projectURL == sourceURL" in capture
assert "Task.isCancelled" in capture
stop = method("func stop()")
assert "lifecycleQueue.async" in stop and "lifecycleQueue.sync" not in stop
assert stop.index("native.suspend()") < stop.index("captures.sync {}") < stop.index("native.stop()")
assert "thumbnailTask?.cancel()" in thumbnail
assert thumbnail.index("self.thumbnailRequest == request", thumbnail.index("let jpeg:")) < thumbnail.index("jpeg.write(")
assert "self.projectContentGeneration == project, self.projectURL == sourceURL" in thumbnail
for name in ("Sheets.swift", "ExportView.swift"):
    text = (HERE.parent / "app" / name).read_text(encoding="utf-8")
    assert ".captureFrame(" not in text
    assert "await model.capturePreviewFrame(" in text
    assert "!Task.isCancelled" in text and "model.projectGeneration == project" in text
android = ROOT / "android/app/src/main/java/com/aurea/aurea"
picker = (android / "ui/ds/ColorPicker.kt").read_text(encoding="utf-8")
assert "LaunchedEffect(picking, previewIdentity)" in picker
assert "withContext(Dispatchers.Default) { captured =" in picker
assert "captured?.recycle()" in picker
stress = (android / "diagnostics/StressBattery.kt").read_text(encoding="utf-8")
assert "private suspend fun captura(maxDim: Int): Captura? = withContext(Dispatchers.Default)" in stress
print("PASS: native captures leave the main thread; picker/export/thumbnail results check request and project identity")

compiler = shutil.which("swiftc")
if not compiler:
    print("SKIP: swiftc unavailable; asynchronous capture races NOT compiled/executed. Static checks are not native validation.")
else:
    harness = '''import Foundation
final class Native: @unchecked Sendable {
    private let lock = NSLock()
    private var count = 0
    let release = DispatchSemaphore(value: 0)
    var calls: Int { lock.lock(); defer { lock.unlock() }; return count }
    func captureFrame(_ size: UInt32, outWidth: inout UInt32, outHeight: inout UInt32) -> Data? {
        lock.lock(); count += 1; lock.unlock()
        release.wait()
        outWidth = 1; outHeight = 1
        return Data([10, 20, 30, 255])
    }
}
struct UIImage {
    let data: Data
    static func fromRGBA(_ data: Data, width: Int, height: Int) -> UIImage? { UIImage(data: data) }
    func jpegData(compressionQuality: Double) -> Data? { data }
}
enum AureaPaths { static let thumbs = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString) }
enum Screen { case home, editor }
@MainActor final class Subject {
    let engine = Native()
    let mediaQueue = DispatchQueue(label: "capture-regression")
    var projectContentGeneration = UUID(), projectGeneration = UUID(), thumbnailRequest = UUID()
    var projectURL: URL? = URL(fileURLWithPath: "/A.aurea")
    var thumbnailTask: Task<Void, Never>?
    var started = true, homeCardStale = true, screen = Screen.editor, refreshed = 0
    func refreshProjectList() { refreshed += 1 }
    CAPTURE
    THUMBNAIL
}
@main struct Run {
    @MainActor static func waitForCapture(_ subject: Subject) async {
        for _ in 0..<1000 {
            if subject.engine.calls > 0 { return }
            try? await Task.sleep(nanoseconds: 1_000_000)
        }
        preconditionFailure("capture never ran on the background queue")
    }
    @MainActor static func main() async throws {
        try FileManager.default.createDirectory(at: AureaPaths.thumbs, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: AureaPaths.thumbs) }
        let live = Subject()
        let liveTask = Task { await live.capturePreviewFrame(720) }
        await waitForCapture(live) // main actor must be free while native AI waits
        live.engine.release.signal()
        let frame = await liveTask.value
        precondition(frame?.data == Data([10, 20, 30, 255]))
        let switched = Subject()
        let switchedTask = switched.writeProjectThumbnail(name: "wrong")
        await waitForCapture(switched)
        switched.projectContentGeneration = UUID()
        switched.projectURL = URL(fileURLWithPath: "/B.aurea")
        switched.engine.release.signal()
        await switchedTask.value
        precondition(!FileManager.default.fileExists(atPath: AureaPaths.thumbs.appendingPathComponent("wrong.jpg").path))
        let closed = Subject()
        let closedTask = closed.writeProjectThumbnail(name: "closed")
        await waitForCapture(closed)
        closed.projectGeneration = UUID(); closed.screen = .home
        closed.engine.release.signal()
        await closedTask.value
        precondition(FileManager.default.fileExists(atPath: AureaPaths.thumbs.appendingPathComponent("closed.jpg").path))
        precondition(!closed.homeCardStale && closed.refreshed == 1)
        let cancelled = Subject()
        let cancelledTask = Task { await cancelled.capturePreviewFrame(720) }
        await waitForCapture(cancelled)
        cancelledTask.cancel(); cancelled.engine.release.signal()
        let discarded = await cancelledTask.value
        precondition(discarded == nil)
        print("PASS: actual capture wrappers keep main actor responsive, reject replacement/cancellation, and finish covers after close")
    }
}
'''.replace("CAPTURE", capture).replace("THUMBNAIL", thumbnail.replace("private func", "func", 1))
    with tempfile.TemporaryDirectory(prefix="aurea-capture-check-") as directory:
        main = pathlib.Path(directory) / "main.swift"
        main.write_text(harness, encoding="utf-8")
        exe = str(pathlib.Path(directory) / "capture-check")
        subprocess.run([compiler, "-parse-as-library", str(main), "-o", exe], check=True)
        subprocess.run([exe], check=True, timeout=15)
