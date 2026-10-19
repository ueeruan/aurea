"""Audit export preparation; execute the real Swift wrappers when swiftc exists.

The executable harness substitutes only the native blocking codec boundary. It
checks UIKit/main-actor responsiveness, cancellation races, frozen options and
fresh versus stale startup retry suggestions. It is not an iOS/Metal export.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
IOS = HERE.parent
source = (IOS / "app/AureaModel.swift").read_text(encoding="utf-8")


def block(header):
    start = source.index(header)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


start = block("func startExport()")
prepare = block("private func prepareExport(")
cancel = block("func cancelExport()")
poll = block("private func startExportPolling()")
startup_progress = block("nonisolated private static func exportStartupProgress(")
retry_level = block("static func nextSafeMode(current:")
stop = block("func stop()")
bridge = (IOS / "bridge/AureaEngine.mm").read_text(encoding="utf-8")
header = (IOS / "bridge/AureaEngine.h").read_text(encoding="utf-8")

assert "let options = exportOptions" in start
assert "pendingExportOptions = options" in start
assert "native.start" not in start and "engine.startExport" not in start
assert prepare.index("exportPreparing = true; exporting = true") < prepare.index("mediaQueue.async")
assert 'AureaText.t("msg_preparando")' in prepare
assert prepare.index("exportPreparing = true") < prepare.index("releaseInterfaceCaches()") < prepare.index("mediaQueue.async")
assert prepare.index("mediaQueue.async") < prepare.index("native.startExportChecked(")
assert prepare.index("mediaQueue.async") < prepare.index("native.startImageExport(")
assert "exportOptions." not in prepare
assert "while !request.isCancelled" in prepare and "else if !request.isCancelled" in prepare
assert "if request.isCancelled { native.cancelExport() }" in prepare
assert "self.exportStartup === request, self.started" in prepare
assert "self.projectGeneration == project, self.pendingExportURL == url" in prepare
assert prepare.index("if succeeded {") < prepare.index("self.startExportPolling()")
assert "exportStartup?.cancel()" in cancel
assert poll.index("!self.exportPreparing") < poll.index("self.engine.exportProgress()")
assert "self.prepareExport(url, options: options, safeMode: next & 0xFF)" in poll
# 0.2.6: a escada de tentativas do motor (export/ExportAttempts.hpp) decide o
# próximo degrau no início, no meio e quando a tela desiste por falta de progresso.
assert "Self.nextExportAttempt(level, code: code" in prepare
assert "Self.nextExportAttempt(self.exportSafeMode, code: Int32(result)" in poll
assert "stalled: true" in poll
ladder = "\n".join([
    source[source.index("    nonisolated static let exportAttemptWait"):source.index("\n", source.index("    nonisolated static let exportBusyRetries"))],
    block("nonisolated static func nextExportAttempt("),
    block("nonisolated static func exportReducedShortSide("),
    block("nonisolated static func exportRetryNotice("),
])
assert "result == 0 && !self.exportCancelled" in poll
assert "startVideoExport" not in source
assert "suggested > current && suggested <= safeModeMax ? suggested : 0" in retry_level
assert 'int32Value == code' in startup_progress and "isEqual(to: previous)" in startup_progress
assert 'fresh = before != after' in startup_progress
assert 'previousDiagnostics: previousDiagnostics, diagnostics: diagnostics' in prepare
assert 'ProblemReport.recordExportDiagnostics(diagnostics)' in prepare
assert 'return ["result": NSNumber(value: code)]' in startup_progress
assert "exportStartup?.cancel(); exportStartup = nil" in stop
assert stop.index("native.suspend()") < stop.index("captures.sync {}") < stop.index("native.stop()")
assert "!exportPreparing" in block("private func refreshStatus()")
assert "!exportPreparing" in block("private func playheadTick()")
assert "!exportPreparing, !exportBackgroundSavePending" in block("func refreshModel(")
background = block("private func suspendPreparingExport()")
assert "if exportPreparing" in block("func enterBackground()")
assert background.index("cancelExport()") < background.index("lifecycleQueue.async")
assert background.index("captures.sync { save ? native.saveProjectIfDirty()") < background.index("native.suspend()")
assert "!exportBackgroundSavePending" in block("private func refreshStatus()")
assert "!exportBackgroundSavePending" in block("private func playheadTick()")
assert "- (int32_t)startExportCheckedTo:" in header
checked = bridge[bridge.index("- (int32_t)startExportCheckedTo:"):]
assert "return e->start_export(settings, out.c_str()).raw();" in checked
assert "return [self startExportCheckedTo:" in bridge
view = (IOS / "app/ExportView.swift").read_text(encoding="utf-8")
assert "model.exportPreparing" in view and 'AureaText.t("msg_preparando")' in view
print("PASS: export starts/retries leave main actor; cancel, lifecycle and fresh progress guards are present")

compiler = shutil.which("swiftc")
if not compiler:
    print("SKIP: swiftc unavailable; asynchronous Swift cases NOT compiled/executed. Static checks are not native iOS validation.")
else:
    harness = r'''import Foundation
enum AureaExportCodec { case h264, hevc }
OPTIONS
KIND
TOKEN
enum AureaPaths {
    static let documents = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
}
enum AureaText {
    enum LanguageCode { case pt }
    struct Language { let resolved = LanguageCode.pt }
    static let language = Language()
    static func t(_ key: String, _ values: CVarArg...) -> String { key }
}
enum AureaEngineText { static func sentence(_ message: String?, code: Int = 0) -> String { "error-\(code)" } }
// Diagnostics persistence is an independent boundary; this harness exercises
// startup scheduling and substitutes it like the native codec provider.
enum ProblemReport {
    static func beginExportDiagnostics(previous: String) {}
    static func recordExportDiagnostics(_ raw: String) {}
}
let AureaExportFailure = "failure"
// A regra do motor (export/ExportAttempts.hpp) no que este harness exercita:
// nada ajuda (cancelado/sem espaço/mídia ausente/acima de 3 h), ocupado
// (refaz igual) e o resto sobe um degrau.
enum AureaEngine {
    static func exportNextAttempt(_ attempt: UInt32, code: Int32, failure: UInt32, stalled: Bool, tooLong: Bool,
                                  shortSide: UInt32, fps: Double, hevc: Bool, upscale: Bool) -> UInt32 {
        if tooLong { return 0 }
        if failure == 0 && !stalled && [0, 18, 25, 28].contains(code) { return 0 }
        if failure == 0 && !stalled && [5, 26, 32].contains(code) { return attempt | 0x100 }
        return attempt < 5 ? attempt + 1 : 0
    }
    static func exportAttemptShortSide(_ attempt: UInt32, requested: UInt32) -> UInt32 {
        attempt >= 5 ? min(requested, 480) : attempt >= 3 ? min(requested, 720) : requested
    }
}
final class Native: @unchecked Sendable {
    struct Call { let kind: UInt32?, safeMode: UInt32, fps: Double, height: UInt32, main: Bool }
    private let lock = NSLock()
    private var recorded: [Call] = []
    private var cancellations = 0
    private var progress: [String: Any] = [:]
    let release = DispatchSemaphore(value: 0)
    var codes: [Int32] = [0]
    var failedProgress: [[String: Any]] = []
    var calls: [Call] { lock.lock(); defer { lock.unlock() }; return recorded }
    var cancelCalls: Int { lock.lock(); defer { lock.unlock() }; return cancellations }
    func setProgress(_ value: [String: Any]) { lock.lock(); progress = value; lock.unlock() }
    func exportProgress() -> [String: Any] { lock.lock(); defer { lock.unlock() }; return progress }
    func exportDiagnostics() -> String { "{}" }
    func exportDuration(_ trimToContent: Bool) -> Int64 { 300 }
    func begin(kind: UInt32?, safeMode: UInt32, fps: Double, height: UInt32) -> Int32 {
        lock.lock()
        let index = recorded.count
        recorded.append(Call(kind: kind, safeMode: safeMode, fps: fps, height: height, main: Thread.isMainThread))
        lock.unlock()
        precondition(!Thread.isMainThread, "native startup ran on UIKit thread")
        precondition(release.wait(timeout: .now() + 5) == .success, "test did not release startup")
        let code = codes[min(index, codes.count - 1)]
        if code == 0 { setProgress(["running": true, "finished": false, "result": 0]) }
        else if index < failedProgress.count { setProgress(failedProgress[index]) }
        return code
    }
    func startExportChecked(to path: String, codec: AureaExportCodec, height: UInt32, fps: Double,
                            bitrateMbps: UInt32, audioBitrateKbps: UInt32, aiUpscale: UInt32,
                            trimToContent: Bool, quality: UInt32, safeMode: UInt32) -> Int32 {
        begin(kind: nil, safeMode: safeMode, fps: fps, height: height)
    }
    func startImageExport(to path: String, format: UInt32, shortSide: UInt32, maxWidth: UInt32,
                          fps: Double, trimToContent: Bool) -> Int32 {
        begin(kind: format, safeMode: 0, fps: fps, height: shortSide)
    }
    func cancelExport() {
        lock.lock(); cancellations += 1
        // Even a native success racing cancel must not publish to Photos.
        if progress["running"] as? Bool == true { progress = ["running": false, "finished": true, "result": 0] }
        lock.unlock()
    }
}
@MainActor final class Subject {
    let engine = Native(), mediaQueue = DispatchQueue(label: "export-startup-regression")
    var projectGeneration = UUID()
    var projectName = "Fixture", started = true, stopping = false
    var exportOptions = ExportOptions(), pendingExportOptions: ExportOptions?
    var exportedKind = ExportKind.video
    var exporting = false, exportPreparing = false, exportCancelled = false, exportPublishing = false
    var exportSavedToPhotos = false, exportStalled = false
    var exportProgress: [String: Any] = [:]
    var exportedURL: URL?, pendingExportURL: URL?
    var exportMessage: String?, toast: String?
    var exportSafeMode: UInt32 = 0
    var exportReducedSide: UInt32 = 0
    var exportTimer: Timer?
    var exportStartup: ExportStartupRequest?
    var exportWatch = ExportStallWatch()
    // 0.2.4 (C5): o watchdog do "Preparando" e a recusa acima de 3 h.
    var preparingTimer: Timer?
    var preparingWatch = ExportStallWatch()
    var compositionFps: Double = 30
    var polls = 0, published = 0
    func followPlayback(_ playing: Bool) {}
    func releaseInterfaceCaches() {}
    func startExportDiagnosticsTimer() {}
    func recordExportDiagnostics() {}
    func publishExportToPhotos(_ url: URL) { published += 1; exporting = false }
    START
    PREPARE
    STARTUP_PROGRESS
    IMAGE_FPS
    CANCEL
    PREPWATCH
    LADDER
    WATCH
    FAILURE
    POLL
    // Deterministic read of the real timer closure, without a run-loop timing
    // assumption. Its Task still resumes on the main actor.
    func tick() { polls += 1; exportTimer?.fire() }
}
@main struct Run {
    @MainActor static func wait(_ condition: @escaping () -> Bool) async {
        for _ in 0..<2000 {
            if condition() { return }
            try? await Task.sleep(nanoseconds: 1_000_000)
        }
        preconditionFailure("async export transition timed out")
    }
    @MainActor static func main() async throws {
        let identical: [String: Any] = ["finished": true, "result": 26, "flags": 2 << 16]
        let staleResult = Subject.exportStartupProgress(identical, previous: identical, code: 26,
            previousDiagnostics: "{\"sessionToken\":\"123\"}", diagnostics: "{\"sessionToken\":\"123\"}")
        precondition(staleResult["flags"] == nil)
        let freshResult = Subject.exportStartupProgress(identical, previous: identical, code: 26,
            previousDiagnostics: "{\"sessionToken\":\"123\"}", diagnostics: "{\"sessionToken\":\"456\"}")
        precondition((freshResult["flags"] as? NSNumber)?.intValue == 2 << 16)
        precondition(Subject.ExportStallWatch.nextSafeMode(current: 0, suggested: 2) == 2)
        precondition(Subject.ExportStallWatch.nextSafeMode(current: 2, suggested: 2) == 0)
        precondition(Subject.ExportStallWatch.nextSafeMode(current: 2, suggested: 3) == 0)
        try FileManager.default.createDirectory(at: AureaPaths.documents, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: AureaPaths.documents) }
        let slow = Subject()
        slow.startExport()
        precondition(slow.exporting && slow.exportPreparing && slow.exportTimer == nil)
        await wait { slow.engine.calls.count == 1 } // main actor is free during blocking native call
        let began = ProcessInfo.processInfo.systemUptime
        slow.cancelExport()
        precondition(ProcessInfo.processInfo.systemUptime - began < 0.25 && slow.exportCancelled)
        slow.engine.release.signal()
        await wait { !slow.exportPreparing }
        precondition(slow.engine.cancelCalls >= 2 && slow.exporting)
        slow.tick()
        await wait { !slow.exporting }
        precondition(slow.published == 0 && slow.exportedURL == nil)

        let queued = Subject(), queueGate = DispatchSemaphore(value: 0)
        queued.mediaQueue.async { queueGate.wait() }
        queued.startExport(); queued.cancelExport(); queueGate.signal()
        await wait { !queued.exporting }
        precondition(queued.engine.calls.isEmpty && queued.exportCancelled)

        for kind in [ExportKind.frame, .sequence, .gif] {
            let image = Subject()
            image.exportOptions.kind = kind
            image.exportOptions.imageShortSide = 720
            image.exportOptions.gifFps = 24; image.exportOptions.fps = 50
            image.engine.codes = [28]
            image.startExport()
            await wait { image.engine.calls.count == 1 }
            image.exportOptions.imageShortSide = 360; image.exportOptions.gifFps = 5
            precondition(image.exportPreparing && image.exportTimer == nil)
            image.engine.release.signal()
            await wait { !image.exporting }
            let call = image.engine.calls[0]
            precondition(call.kind == kind.engineCode && call.height == 720)
            precondition(call.fps == (kind == .gif ? 24 : kind == .sequence ? 50 : 0))
            precondition(image.toast == "error-28" && image.exportTimer == nil)
        }

        let retry = Subject()
        retry.exportOptions.fps = 60
        retry.engine.codes = [15, 0]
        retry.engine.failedProgress = [["running": false, "finished": true, "result": 15,
                                        "flags": 1 << 16, "failure": 1, "message": "fresh codec failure"]]
        retry.startExport()
        await wait { retry.engine.calls.count == 1 }
        retry.exportOptions.fps = 25; retry.engine.release.signal()
        await wait { retry.engine.calls.count == 2 }
        precondition(retry.engine.calls[1].safeMode == 1 && retry.engine.calls[1].fps == 60)
        precondition(retry.exportPreparing && retry.exportTimer == nil)
        retry.engine.release.signal()
        await wait { !retry.exportPreparing }
        precondition(retry.exportSafeMode == 1 && retry.exporting)
        retry.cancelExport(); retry.tick()
        await wait { !retry.exporting }

        let stale = Subject()
        stale.engine.setProgress(["running": false, "finished": true, "result": 28,
                                  "flags": 1 << 16, "failure": 1, "message": "old codec failure"])
        stale.engine.codes = [28] // admission failed before clearing an old context
        stale.startExport()
        await wait { stale.engine.calls.count == 1 }
        stale.engine.release.signal()
        await wait { !stale.exporting }
        precondition(stale.engine.calls.count == 1 && stale.exportProgress["flags"] == nil)
        precondition((stale.exportProgress["result"] as? NSNumber)?.intValue == 28)

        let closed = Subject()
        closed.startExport()
        await wait { closed.engine.calls.count == 1 }
        closed.exportStartup?.cancel(); closed.exportStartup = nil
        closed.started = false; closed.exporting = false; closed.exportPreparing = false
        closed.pendingExportURL = nil; closed.projectGeneration = UUID()
        closed.engine.release.signal()
        await wait { closed.engine.cancelCalls > 0 }
        try? await Task.sleep(nanoseconds: 10_000_000)
        precondition(closed.exportTimer == nil && !closed.exporting && closed.published == 0)
        print("PASS: real Swift export wrappers free main actor, honor queued/in-flight cancel, freeze settings, retry fresh failures and reject stale completion/progress")
    }
}
'''
    values = {
        "OPTIONS": block("struct ExportOptions:"),
        "KIND": block("enum ExportKind:"),
        "TOKEN": block("private final class ExportStartupRequest:").replace("private final", "final", 1),
        "START": start,
        "PREPARE": prepare.replace("private func", "func", 1),
        "STARTUP_PROGRESS": startup_progress.replace("private static", "static", 1),
        "IMAGE_FPS": block("private func imageExportFps(").replace("private func", "func", 1),
        "CANCEL": cancel,
        "PREPWATCH": "\n".join([
            source[source.index("    static let exportMaxSeconds"):source.index("\n", source.index("    static let exportMaxSeconds"))],
            block("func plannedExportSeconds("),
            block("private func startPreparingWatch(").replace("private func", "func", 1),
            block("private func stopPreparingWatch()").replace("private func", "func", 1),
            block("private func abandonPreparingExport(").replace("private func", "func", 1),
        ]),
        "LADDER": ladder,
        "WATCH": block("struct ExportStallWatch {"),
        "FAILURE": block("static func exportFailureReason("),
        "POLL": poll.replace("private func", "func", 1),
    }
    harness = re.sub(r"\b(" + "|".join(values) + r")\b", lambda match: values[match.group(0)], harness)
    with tempfile.TemporaryDirectory(prefix="aurea-export-startup-check-") as directory:
        main = Path(directory) / "main.swift"
        main.write_text(harness, encoding="utf-8")
        executable = str(Path(directory) / "export-check")
        subprocess.run([compiler, "-parse-as-library", str(main), "-o", executable], check=True)
        subprocess.run([executable], check=True, timeout=20)
