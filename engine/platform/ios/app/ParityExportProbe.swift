// Explicit opt-in CI fixture. Exercises the real Metal -> NV12 -> VideoToolbox
// -> MP4 path, then independently decodes the exported file with AVAssetReader.
// Imports a generated PCM tone through the production importer to verify AAC too.
// No Photos access, replacement renderer, or encoder stub.
#if DEBUG
import Foundation
import AVFoundation
import CoreImage
import UIKit

enum ParityExportProbe {
    private struct Failure: LocalizedError {
        let message: String
        var errorDescription: String? { message }
    }

    /// Call only from a dedicated parity scene: this replaces the open project.
    /// The environment gate also prevents accidental use in ordinary Debug runs.
    /// A terminal report is written atomically to Documents/parity-export-ready.json.
    @MainActor
    static func run(engine: AureaEngine, documents: URL) async -> [String: Any] {
        let environment = ProcessInfo.processInfo.environment
        guard environment["AUREA_PARITY_EXPORT"] == "1" else {
            return ["enabled": false, "finished": true, "passed": false]
        }
        let token = UUID().uuidString
        let movie = documents.appendingPathComponent("parity-export-\(token).mp4")
        let frame = documents.appendingPathComponent("parity-export-\(token).png")
        let fixture = documents.appendingPathComponent("parity-export-\(token).aurea")
        let tone = documents.appendingPathComponent("parity-tone:\(token).wav")
        let resultURL = documents.appendingPathComponent("parity-export-ready.json")
        let began = ProcessInfo.processInfo.systemUptime
        var exportStarted = false
        var report: [String: Any] = [
            "version": 2, "enabled": true, "finished": false, "passed": false,
            "phase": "fixture", "runID": environment["AUREA_UI_TEST_RUN_ID"] ?? token,
            "movieFile": movie.lastPathComponent, "frameFile": frame.lastPathComponent,
            "projectFile": fixture.lastPathComponent,
            "requested": ["width": 1920, "height": 1080, "fps": 30, "frames": 30,
                          "shortSide": 360, "bitrateMbps": 4, "codec": "h264",
                          "audioCodec": "aac", "audioSampleRate": 48000, "audioChannels": 2],
        ]
        func writeReport() throws {
            guard let data = AureaJSONData(report, true) else { return }
            try data.write(to: resultURL, options: .atomic)
        }
        do {
            try writeReport()
            guard engine.running else { throw Failure(message: "Core is not running") }
            guard (engine.exportProgress()[AureaExportRunning] as? NSNumber)?.boolValue != true else {
                throw Failure(message: "Another export is already running")
            }
            report["phase"] = "particle-camera"
            try writeReport()
            report["particleCamera"] = try await checkParticleCamera(engine: engine, documents: documents)
            report["phase"] = "paper-effects"
            try writeReport()
            report["paperEffects"] = try await checkPaperEffects(engine: engine, documents: documents)
            report["phase"] = "preview-viewport"
            try writeReport()
            report["previewViewport"] = try await checkPreviewViewport(engine: engine)
            report["phase"] = "fixture"
            engine.pause()
            _ = engine.flush()
            guard engine.newProjectWidth(1920, height: 1080, fps: 30, title: "Parity Export") else {
                throw Failure(message: "Core rejected the export fixture project")
            }
            guard let composition = engine.composition(),
                  let compositionID = (composition[AureaCompositionId] as? NSNumber)?.uint64Value else {
                throw Failure(message: "Fixture composition is unavailable")
            }
            engine.setComposition(compositionID, duration: 30)
            engine.setComposition(compositionID, backgroundR: 0, g: 0, b: 0, a: 1)
            // saveProject drains pending core commands without requiring a GPU
            // frame. flush only submits them; a subsequent query may be stale.
            guard engine.saveProject(fixture.path) else {
                throw Failure(message: "Could not apply and save the 30-frame composition")
            }
            let layer = engine.addShape(1)
            guard layer >= 0 else { throw Failure(message: "Core rejected the fixture shape: \(layer)") }
            engine.setLayer(layer, startFrame: 0, endFrame: 30, offsetFrames: 0, setOffset: true)
            engine.setShape(layer, fillR: 1, g: 1, b: 1, a: 1)
            engine.setPosition(forLayer: layer, x: 960, y: 540, z: 0)
            _ = engine.flush()
            // Resolve the serialized effect identity; added parameters must not
            // stop this fixture before the production renderer is exercised.
            guard let glow = engine.effectCatalog().first(where: {
                ($0[AureaEffectTypeId] as? NSNumber)?.uint32Value == fxEffectTypeId("aurea.light.glow") &&
                (($0[AureaEffectParamCount] as? NSNumber)?.intValue ?? 0) >= 4
            }), let typeID = (glow[AureaEffectTypeId] as? NSNumber)?.uint32Value else {
                throw Failure(message: "Original Glow effect is absent from the core catalog")
            }
            engine.addEffect(typeID, toLayer: layer, at: 0)
            guard engine.saveProject(fixture.path) else {
                throw Failure(message: "Could not apply and save the fixture shape and Glow")
            }
            guard let effect = engine.effects(forLayer: layer).first,
                  let effectID = (effect[AureaEffectId] as? NSNumber)?.uint32Value,
                  (effect[AureaEffectTypeId] as? NSNumber)?.uint32Value == typeID else {
                throw Failure(message: "Core did not attach Glow to the fixture shape")
            }
            engine.setEffect(effectID, forLayer: layer, enabled: true)
            engine.setEffect(effectID, forLayer: layer, paramIndex: 0, value: 48)
            engine.setEffect(effectID, forLayer: layer, paramIndex: 1, value: 60)
            engine.setEffect(effectID, forLayer: layer, paramIndex: 2, value: 2)
            _ = engine.flush()
            try writeTone(to: tone)
            let audioLayer = engine.importAudio(tone.path, name: "Generated stereo tone")
            guard audioLayer >= 0 else {
                throw Failure(message: "Production audio importer rejected the generated WAV: \(audioLayer)")
            }
            engine.setLayer(audioLayer, startFrame: 0, endFrame: 30, offsetFrames: 0, setOffset: true)
            engine.seek(toFrame: 0)
            _ = engine.flush()
            guard engine.saveProject(fixture.path) else {
                throw Failure(message: "Could not apply and save the final fixture parameters")
            }
            var status = AureaStatus()
            guard engine.readStatus(&status), status.compWidth == 1920, status.compHeight == 1080,
                  abs(status.compFps - 30) < 0.01, status.duration == 30, status.layerCount == 2 else {
                throw Failure(message: "Core fixture dimensions, frame rate, duration, or layers differ")
            }
            report["fixture"] = ["composition": engine.composition() ?? [:], "layers": engine.layers(),
                                 "effects": engine.effects(forLayer: layer),
                                 "effectParams": engine.effectParams(forLayer: layer, effectId: effectID)]
            report["phase"] = "export"
            try writeReport()
            guard engine.startExport(to: movie.path, codec: .h264, height: 360, fps: 30,
                                     bitrateMbps: 4, audioBitrateKbps: 128) else {
                throw Failure(message: "Real H.264 export could not start")
            }
            exportStarted = true
            let deadline = ProcessInfo.processInfo.systemUptime + 120
            while true {
                try Task.checkCancellation()
                let progress = engine.exportProgress()
                report["exportProgress"] = progress
                let running = (progress[AureaExportRunning] as? NSNumber)?.boolValue ?? false
                let finished = (progress[AureaExportFinished] as? NSNumber)?.boolValue ?? false
                if finished && !running {
                    exportStarted = false
                    guard (progress[AureaExportResult] as? NSNumber)?.intValue == 0 else {
                        throw Failure(message: "Export failed: \(progress[AureaExportMessage] ?? progress)")
                    }
                    guard (progress[AureaExportFramesTotal] as? NSNumber)?.intValue == 30,
                          (progress[AureaExportFramesDone] as? NSNumber)?.intValue == 30 else {
                        throw Failure(message: "Core export did not finish all 30 frames")
                    }
                    break
                }
                guard ProcessInfo.processInfo.systemUptime < deadline else {
                    throw Failure(message: "Real export exceeded the 120 second timeout")
                }
                try await Task.sleep(nanoseconds: 100_000_000)
            }
            report["phase"] = "decode"
            try writeReport()
            let decoded = try await Task.detached(priority: .utility) {
                try await decode(movie: movie, frame: frame)
            }.value
            report["decoded"] = decoded.dictionary
            report["decodedAudio"] = try await Task.detached(priority: .utility) {
                try await decodeAudio(movie: movie)
            }.value.dictionary
            guard decoded.frameCount == 30 else {
                throw Failure(message: "MP4 contains \(decoded.frameCount) decoded frames; expected 30")
            }
            guard abs(decoded.duration - 1) < 0.04 else {
                throw Failure(message: "MP4 duration is \(decoded.duration) seconds; expected 1")
            }
            guard decoded.lightPixels >= 500, decoded.darkPixels >= 500,
                  decoded.maxRGB - decoded.minRGB > 64 else {
                throw Failure(message: "Decoded exported frame is blank or lacks the fixture's shape/background contrast")
            }
            report["phase"] = "production-decoder"
            try writeReport()
            let productionDecoder = AureaVerifyVideoDecoder(movie.path, 30)
            report["productionDecoder"] = productionDecoder
            guard (productionDecoder["passed"] as? NSNumber)?.boolValue == true else {
                throw Failure(message: "Production video decoder regression failed: \(productionDecoder)")
            }
            let reordered = AureaVerifyVideoDecoder(documents.appendingPathComponent("preview-bframes.mp4").path, 90)
            report["bFrameDecoder"] = reordered
            guard (reordered["passed"] as? NSNumber)?.boolValue == true else {
                throw Failure(message: "Production B-frame decoder regression failed: \(reordered)")
            }
            let variableRate = AureaVerifyVideoDecoder(documents.appendingPathComponent("preview-vfr.mp4").path, 63)
            report["variableRateDecoder"] = variableRate
            guard (variableRate["passed"] as? NSNumber)?.boolValue == true else {
                throw Failure(message: "Production VFR decoder regression failed: \(variableRate)")
            }
            report["phase"] = "audio-failure-recovery"
            try writeReport()
            report["audioFailureRecovery"] = try await checkAudioFailureRecovery(engine: engine, documents: documents)
            report["passed"] = true
            report["phase"] = "complete"
        } catch {
            if exportStarted { engine.cancelExport() }
            report["error"] = error.localizedDescription
            report["failedPhase"] = report["phase"]
            report["phase"] = "failed"
        }
        report["finished"] = true
        report["elapsedSeconds"] = ProcessInfo.processInfo.systemUptime - began
        report["renderDiagnostics"] = engine.renderDiagnostics()
        report["exportProgress"] = engine.exportProgress()
        do { try writeReport() }
        catch { report["reportWriteError"] = error.localizedDescription }
        return report
    }

    @MainActor
    private static func checkPreviewViewport(engine: AureaEngine) async throws -> [String: Any] {
        guard engine.newProjectWidth(1920, height: 1080, fps: 30, title: "Preview viewport") else {
            throw Failure(message: "Viewport project unavailable")
        }
        _ = engine.addShape(0)
        engine.setViewportZoom(1, panX: 0, panY: 0)
        engine.setPreviewScaleNumerator(1, denominator: 1, automatic: false)
        _ = engine.flush()
        func wait(_ accepts: (AureaStatus) -> Bool) async throws -> AureaStatus {
            let deadline = ProcessInfo.processInfo.systemUptime + 10
            while ProcessInfo.processInfo.systemUptime < deadline {
                var state = AureaStatus()
                if engine.readStatus(&state), accepts(state) { return state }
                try await Task.sleep(nanoseconds: 20_000_000)
            }
            throw Failure(message: "Viewport render dimensions did not settle")
        }
        _ = try await wait { $0.previewWidth == 1920 && $0.previewAuto == 0 }
        func exact() async throws -> Data {
            let pixels: Data? = await withCheckedContinuation { continuation in
                DispatchQueue.global(qos: .userInitiated).async {
                    var width: UInt32 = 0, height: UInt32 = 0
                    let data = engine.captureFrame(320, outWidth: &width, outHeight: &height)
                    continuation.resume(returning: width == 320 && height == 180 ? data : nil)
                }
            }
            guard let pixels else { throw Failure(message: "Viewport exact capture unavailable") }
            return pixels
        }
        let original = try await exact()
        engine.setPreviewScaleNumerator(1, denominator: 1, automatic: true); _ = engine.flush()
        let automatic = try await wait { $0.previewAuto != 0 && $0.previewWidth > 0 && $0.previewWidth <= 1920 }
        // A large tablet may fit the full composition. Both native sizes remain valid.
        let captured = try await exact()
        guard original == captured else { throw Failure(message: "AUTO changed an exact capture") }
        engine.setPreviewScaleNumerator(1, denominator: 2, automatic: false); _ = engine.flush()
        _ = try await wait { $0.previewWidth == 960 && $0.previewAuto == 0 }
        engine.setPreviewScaleNumerator(1, denominator: 1, automatic: false); _ = engine.flush()
        _ = try await wait { $0.previewWidth == 1920 && $0.previewAuto == 0 }
        return ["passed": true, "automaticWidth": automatic.previewWidth,
                "automaticHeight": automatic.previewHeight, "manualFullWidth": 1920,
                "manualHalfWidth": 960, "exactCaptureUnchanged": true]
    }

    /// A generated opaque image and a hidden-source emitter exercise the same
    /// catalog path as Android. Captures execute the real Metal backend.
    @MainActor
    private static func checkParticleCamera(engine: AureaEngine, documents: URL) async throws -> [String: Any] {
        engine.pause(); _ = engine.flush()
        guard engine.newProjectWidth(320, height: 180, fps: 30, title: "Particle camera probe"),
              let composition = engine.composition(),
              let compositionID = (composition[AureaCompositionId] as? NSNumber)?.uint64Value else {
            throw Failure(message: "Particle camera project could not be created")
        }
        engine.setComposition(compositionID, backgroundR: 0, g: 0, b: 0, a: 1)
        let format = UIGraphicsImageRendererFormat(); format.scale = 1; format.opaque = true
        let image = UIGraphicsImageRenderer(size: CGSize(width: 320, height: 180), format: format).image { context in
            UIColor.white.setFill(); context.fill(CGRect(x: 0, y: 0, width: 320, height: 180))
        }
        let source = documents.appendingPathComponent("particle-camera-source.png")
        guard let png = image.pngData() else { throw Failure(message: "Particle source PNG unavailable") }
        try png.write(to: source, options: .atomic)
        let layer = engine.importImageFile(source.path, name: "Emitter source")
        guard layer >= 0 else { throw Failure(message: "Particle source import failed") }
        let type = fxEffectTypeId("aurea.generate.particular") // saved-project compatibility key
        engine.addEffect(type, toLayer: layer, at: 0)
        let project = documents.appendingPathComponent("particle-camera-probe.aurea")
        guard engine.saveProject(project.path), let effect = engine.effects(forLayer: layer).first,
              let effectID = (effect[AureaEffectId] as? NSNumber)?.uint32Value else {
            throw Failure(message: "Particle catalog effect was not attached")
        }
        for p in [UInt32(5), 6, 7, 9, 15, 16, 17, 18, 20, 23, 25, 28, 29, 31, 36, 38] {
            engine.setEffect(effectID, forLayer: layer, paramIndex: p, value: 0)
        }
        let values: [UInt32: Float] = [0: 30, 1: 1000, 22: 30000, 24: 12, 26: 100, 37: 71]
        for (p, value) in values { engine.setEffect(effectID, forLayer: layer, paramIndex: p, value: value) }
        guard engine.saveProject(project.path) else { throw Failure(message: "Particle parameters could not be saved") }
        func measure(_ frame: Int64) async throws -> (x: Double, y: Double, energy: Double) {
            engine.seek(toFrame: frame); _ = engine.flush()
            let data: Data? = await withCheckedContinuation { continuation in
                DispatchQueue.global(qos: .userInitiated).async {
                    var width: UInt32 = 0, height: UInt32 = 0
                    let data = engine.captureFrame(320, outWidth: &width, outHeight: &height)
                    continuation.resume(returning: width == 320 && height == 180 ? data : nil)
                }
            }
            guard let data, data.count == 320 * 180 * 4 else {
                throw Failure(message: "Particle capture failed at frame \(frame)")
            }
            try data.write(to: documents.appendingPathComponent("particle-camera-\(frame).rgba"), options: .atomic)
            var energy = 0.0, x = 0.0, y = 0.0
            data.withUnsafeBytes { bytes in
                let pixels = bytes.bindMemory(to: UInt8.self)
                for i in stride(from: 0, to: pixels.count, by: 4) {
                    let value = Double(pixels[i]) + Double(pixels[i + 1]) + Double(pixels[i + 2])
                    energy += value; x += value * (Double(i / 4 % 320) + 0.5)
                    y += value * (Double(i / 4 / 320) + 0.5)
                }
            }
            guard energy > 2000 else { throw Failure(message: "Particle image is empty at frame \(frame)") }
            return (x / energy, y / energy, energy)
        }
        let baseline = try await measure(15)
        guard abs(baseline.x - 160) <= 1, abs(baseline.y - 90) <= 1 else {
            throw Failure(message: "Particle emitter has an unexpected initial position")
        }
        engine.seek(toFrame: 0); _ = engine.flush()
        guard engine.saveProject(project.path) else { throw Failure(message: "Particle seek did not apply") }
        let camera = engine.addCamera()
        guard camera >= 0 else { throw Failure(message: "Particle camera could not be created") }
        engine.setCameraParam(camera, param: 0, value: Float(12 / tan(Double.pi / 9)))
        engine.setPosition(forLayer: camera, x: 195, y: 90, z: Float(-90 / tan(Double.pi / 9)))
        engine.setLayer(camera, startFrame: 10, endFrame: 20, offsetFrames: 0, setOffset: true)
        guard engine.saveProject(project.path) else { throw Failure(message: "Particle camera range was not applied") }
        var samples: [[String: Any]] = []
        for frame in [Int64(9), 10, 15, 20, 15, 20, 9] {
            let sample = try await measure(frame)
            let expectedX = baseline.x - ((10..<20).contains(frame) ? 35 : 0)
            guard abs(sample.x - expectedX) <= 1, abs(sample.y - baseline.y) <= 1 else {
                throw Failure(message: "Particle camera mismatch at \(frame): \(sample.x),\(sample.y); expected \(expectedX),\(baseline.y)")
            }
            samples.append(["frame": frame, "x": sample.x, "y": sample.y,
                            "energy": sample.energy, "expectedX": expectedX])
        }
        let result: [String: Any] = ["passed": true, "width": 320, "height": 180,
                                     "baselineX": baseline.x, "baselineY": baseline.y, "samples": samples]
        if let data = AureaJSONData(result, true) {
            try data.write(to: documents.appendingPathComponent("particle-camera-ready.json"), options: .atomic)
        }
        return result
    }

    @MainActor
    private static func checkPaperEffects(engine: AureaEngine, documents: URL) async throws -> [[String: Any]] {
        let format = UIGraphicsImageRendererFormat(); format.scale = 1; format.opaque = false
        let image = UIGraphicsImageRenderer(size: CGSize(width: 320, height: 180), format: format).image { context in
            for y in stride(from: 0, to: 180, by: 20) {
                for x in stride(from: 0, to: 320, by: 20) {
                    UIColor(red: x < 160 ? 240.0 / 255 : 32.0 / 255,
                            green: ((x / 20 + y / 20) % 2 == 0) ? 40.0 / 255 : 200.0 / 255,
                            blue: x < 160 ? 32.0 / 255 : 240.0 / 255, alpha: 1).setFill()
                    context.fill(CGRect(x: CGFloat(x), y: CGFloat(y), width: 20, height: 20))
                }
            }
            context.cgContext.clear(CGRect(x: 0, y: 80, width: 320, height: 20))
        }
        guard let png = image.pngData() else { throw Failure(message: "Paper source PNG unavailable") }
        let source = documents.appendingPathComponent("paper-source.png")
        try png.write(to: source, options: .atomic)
        func capture(_ name: String) async throws -> Data {
            let pixels: Data? = await withCheckedContinuation { continuation in
                DispatchQueue.global(qos: .userInitiated).async {
                    var width: UInt32 = 0, height: UInt32 = 0
                    let data = engine.captureFrame(320, outWidth: &width, outHeight: &height)
                    continuation.resume(returning: width == 320 && height == 180 ? data : nil)
                }
            }
            guard let pixels, pixels.count == 320 * 180 * 4,
                  let preview = UIImage.fromRGBA(pixels, width: 320, height: 180)?.pngData() else {
                throw Failure(message: "Native paper image is missing: \(name)")
            }
            try preview.write(to: documents.appendingPathComponent("paper-\(name).png"), options: .atomic)
            return pixels
        }
        func difference(_ a: Data, _ b: Data) -> Int {
            let x = [UInt8](a), y = [UInt8](b)
            guard x.count == y.count else { return Int.max }
            var changed = 0
            for i in stride(from: 0, to: x.count, by: 4) {
                let delta = abs(Int(x[i]) - Int(y[i])) + abs(Int(x[i+1]) - Int(y[i+1])) + abs(Int(x[i+2]) - Int(y[i+2]))
                if delta > 8 { changed += 1 }
            }
            return changed
        }
        var results: [[String: Any]] = []
        for (name, amount) in [("bender", Float(35)), ("bend", 65), ("curl", 145), ("page_turn", 50)] {
            engine.pause(); _ = engine.flush()
            guard engine.newProjectWidth(320, height: 180, fps: 30, title: "Paper \(name)"),
                  let composition = engine.composition(),
                  let compositionID = (composition[AureaCompositionId] as? NSNumber)?.uint64Value else {
                throw Failure(message: "Paper project could not be created")
            }
            engine.setComposition(compositionID, backgroundR: 0, g: 0, b: 0, a: 1)
            let layer = engine.importImageFile(source.path, name: "Paper source")
            guard layer >= 0 else { throw Failure(message: "Paper source import failed") }
            let original = try await capture(name + "-original")
            engine.addEffect(fxEffectTypeId("aurea.distort." + name), toLayer: layer, at: 0)
            let project = documents.appendingPathComponent("paper-\(name).aurea")
            guard engine.saveProject(project.path), let effect = engine.effects(forLayer: layer).first,
                  let effectID = (effect[AureaEffectId] as? NSNumber)?.uint32Value else {
                throw Failure(message: "Paper catalog effect could not be added: \(name)")
            }
            engine.setEffect(effectID, forLayer: layer, paramIndex: 0, value: amount)
            if name == "curl" { engine.setEffect(effectID, forLayer: layer, paramIndex: 1, value: 32) }
            if name == "page_turn" { engine.setEffect(effectID, forLayer: layer, paramIndex: 2, value: 16) }
            guard engine.saveProject(project.path) else { throw Failure(message: "Paper parameters could not be saved") }
            let deformed = try await capture(name + "-deformed")
            let changed = difference(original, deformed)
            guard changed > 400 else { throw Failure(message: "Paper effect did not change its pixels: \(name)") }
            guard engine.loadProject(project.path) else { throw Failure(message: "Paper project did not reopen") }
            let reopened = try await capture(name + "-reopened")
            guard difference(deformed, reopened) == 0 else { throw Failure(message: "Paper persistence changed pixels: \(name)") }
            engine.setEffect(effectID, forLayer: layer, paramIndex: 0, value: 0)
            _ = engine.flush()
            let neutral = try await capture(name + "-zero")
            guard difference(original, neutral) == 0 else { throw Failure(message: "Zero paper effect is not identity: \(name)") }
            results.append(["effect": name, "changedPixels": changed, "identityPassed": true, "persistencePassed": true])
        }
        if let data = AureaJSONData(["passed": true, "effects": results], true) {
            try data.write(to: documents.appendingPathComponent("paper-effects-ready.json"), options: .atomic)
        }
        return results
    }

    private struct DecodedMovie: Sendable {
        var frameCount = 0
        var duration = 0.0
        var fileBytes: UInt64 = 0
        var timestamps: [Double] = []
        var lightPixels = 0
        var darkPixels = 0
        var minRGB = 255
        var maxRGB = 0
        var dictionary: [String: Any] {
            ["codec": "h264", "width": 640, "height": 360, "frames": frameCount,
             "durationSeconds": duration, "fileBytes": fileBytes, "timestampsSeconds": timestamps,
             "lightPixels": lightPixels, "darkPixels": darkPixels, "minRGB": minRGB, "maxRGB": maxRGB]
        }
    }

    @MainActor
    private static func checkAudioFailureRecovery(engine: AureaEngine, documents: URL) async throws -> [String: Any] {
        let token = UUID().uuidString
        let tone = documents.appendingPathComponent("audio-failure-\(token).wav")
        let broken = documents.appendingPathComponent("audio-failure-\(token).mp4")
        let recovered = documents.appendingPathComponent("audio-recovered-\(token).mp4")
        let project = documents.appendingPathComponent("audio-failure-\(token).aurea")
        engine.pause()
        guard engine.newProjectWidth(320, height: 180, fps: 30, title: "Audio failure recovery") else {
            throw Failure(message: "Audio failure fixture could not open")
        }
        try writeTone(to: tone)
        guard engine.addShape(1) >= 0, engine.importAudio(tone.path, name: "Required audio") >= 0,
              let composition = engine.composition(),
              let id = (composition[AureaCompositionId] as? NSNumber)?.uint64Value else {
            throw Failure(message: "Audio failure fixture did not import its real WAV")
        }
        engine.setComposition(id, duration: 30)
        guard engine.saveProject(project.path) else { throw Failure(message: "Audio fixture commands were not applied") }
        // Corrupt only this generated temporary source after import establishes
        // valid metadata; export owns a fresh decoder, independent of preview.
        try Data("invalid generated WAV".utf8).write(to: tone, options: .atomic)
        var running = false
        defer { if running { engine.cancelExport() } }
        func export(_ target: URL) async throws -> [String: Any] {
            guard engine.startExport(to: target.path, codec: .h264, height: 180, fps: 30,
                                     bitrateMbps: 2, audioBitrateKbps: 128) else {
                throw Failure(message: "Audio failure test export did not start")
            }
            running = true
            let deadline = ProcessInfo.processInfo.systemUptime + 60
            while ProcessInfo.processInfo.systemUptime < deadline {
                let progress = engine.exportProgress()
                if (progress[AureaExportFinished] as? NSNumber)?.boolValue == true {
                    running = false
                    return progress
                }
                try await Task.sleep(nanoseconds: 50_000_000)
            }
            throw Failure(message: "Audio failure recovery exceeded60 seconds")
        }
        let failed = try await export(broken)
        guard (failed[AureaExportFailure] as? NSNumber)?.intValue == 5,
              (failed[AureaExportResult] as? NSNumber)?.intValue != 0,
              !FileManager.default.fileExists(atPath: broken.path) else {
            throw Failure(message: "Missing audible PCM was saved as successful silence: \(failed)")
        }
        try writeTone(to: tone)
        let success = try await export(recovered)
        guard (success[AureaExportResult] as? NSNumber)?.intValue == 0,
              (success[AureaExportFramesDone] as? NSNumber)?.intValue == 30 else {
            throw Failure(message: "Restored audio could not export all30 frames: \(success)")
        }
        let audio = try await Task.detached(priority: .utility) {
            try await decodeAudio(movie: recovered)
        }.value
        return ["passed": true, "failedProgress": failed, "recoveredProgress": success,
                "decodedAudio": audio.dictionary, "movieFile": recovered.lastPathComponent]
    }

    private static func writeTone(to url: URL) throws {
        // Exactly one second of 16-bit stereo PCM; no external/user media.
        let rate = 48_000, channels = 2, byteCount = rate * channels * 2
        var bytes = Data()
        func ascii(_ value: String) { bytes.append(contentsOf: value.utf8) }
        func littleEndian<T: FixedWidthInteger>(_ value: T) {
            var value = value.littleEndian
            withUnsafeBytes(of: &value) { bytes.append(contentsOf: $0) }
        }
        ascii("RIFF"); littleEndian(UInt32(36 + byteCount)); ascii("WAVEfmt ")
        littleEndian(UInt32(16)); littleEndian(UInt16(1)); littleEndian(UInt16(channels))
        littleEndian(UInt32(rate)); littleEndian(UInt32(rate * channels * 2))
        littleEndian(UInt16(channels * 2)); littleEndian(UInt16(16))
        ascii("data"); littleEndian(UInt32(byteCount))
        for sample in 0..<rate {
            for frequency in [440.0, 660.0] {
                let amplitude = sin(2 * Double.pi * frequency * Double(sample) / Double(rate)) * 8192
                littleEndian(Int16(amplitude.rounded()))
            }
        }
        try bytes.write(to: url, options: .atomic)
    }

    private struct DecodedAudio: Sendable {
        var frames = 0
        var buffers = 0
        var squaredSum = 0.0
        var peak = 0.0
        var firstTime = Double.nan
        var endTime = 0.0
        var rms: Double { frames > 0 ? sqrt(squaredSum / Double(frames * 2)) : 0 }
        var dictionary: [String: Any] {
            ["codec": "aac", "sampleRate": 48000, "channels": 2, "frames": frames,
             "buffers": buffers, "firstTimestampSeconds": firstTime,
             "endTimestampSeconds": endTime, "rms": rms, "peak": peak]
        }
    }

    private static func decodeAudio(movie: URL) async throws -> DecodedAudio {
        let asset = AVURLAsset(url: movie)
        let tracks = try await asset.loadTracks(withMediaType: .audio)
        guard tracks.count == 1, let track = tracks.first else {
            throw Failure(message: "Exported MP4 must contain the imported tone as one audio track")
        }
        let formats = try await track.load(.formatDescriptions)
        guard !formats.isEmpty,
              formats.allSatisfy({ CMFormatDescriptionGetMediaSubType($0) == kAudioFormatMPEG4AAC }) else {
            throw Failure(message: "Exported audio is not AAC")
        }
        let reader = try AVAssetReader(asset: asset)
        let output = AVAssetReaderTrackOutput(track: track, outputSettings: [
            AVFormatIDKey: kAudioFormatLinearPCM, AVLinearPCMBitDepthKey: 32,
            AVLinearPCMIsFloatKey: true, AVLinearPCMIsBigEndianKey: false,
            AVLinearPCMIsNonInterleaved: false,
        ])
        guard reader.canAdd(output) else { throw Failure(message: "Cannot independently decode the audio track") }
        reader.add(output)
        guard reader.startReading() else {
            throw Failure(message: reader.error?.localizedDescription ?? "Audio reader could not start")
        }
        defer { if reader.status == .reading { reader.cancelReading() } }
        var result = DecodedAudio()
        while let sample = output.copyNextSampleBuffer() {
            try Task.checkCancellation()
            guard result.buffers < 200, let format = CMSampleBufferGetFormatDescription(sample),
                  let asbd = CMAudioFormatDescriptionGetStreamBasicDescription(format),
                  abs(asbd.pointee.mSampleRate - 48000) < 0.1,
                  asbd.pointee.mChannelsPerFrame == 2, asbd.pointee.mBitsPerChannel == 32,
                  let block = CMSampleBufferGetDataBuffer(sample) else {
                throw Failure(message: "Decoded audio is not bounded 48 kHz stereo float PCM")
            }
            let frames = CMSampleBufferGetNumSamples(sample)
            let bytes = CMBlockBufferGetDataLength(block)
            guard frames > 0, bytes == frames * 2 * MemoryLayout<Float>.size else {
                throw Failure(message: "Decoded PCM sample count disagrees with its buffer size")
            }
            var values = [Float](repeating: 0, count: frames * 2)
            let copied = values.withUnsafeMutableBytes {
                CMBlockBufferCopyDataBytes(block, atOffset: 0, dataLength: bytes, destination: $0.baseAddress!)
            }
            guard copied == kCMBlockBufferNoErr else { throw Failure(message: "Could not read decoded PCM") }
            let time = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sample))
            guard time.isFinite, result.buffers == 0 || abs(time - result.endTime) < 0.002 else {
                throw Failure(message: "Decoded AAC has discontinuous sample timestamps")
            }
            if result.buffers == 0 { result.firstTime = time }
            result.endTime = time + Double(frames) / 48000
            for value in values {
                guard value.isFinite else { throw Failure(message: "Decoded AAC contains nonfinite samples") }
                let amplitude = Double(value)
                result.squaredSum += amplitude * amplitude
                result.peak = max(result.peak, abs(amplitude))
            }
            result.frames += frames; result.buffers += 1
        }
        guard reader.status == .completed else {
            throw Failure(message: reader.error?.localizedDescription ?? "Audio reader did not finish")
        }
        // AAC packets may expose codec priming/padding around the one-second edit.
        guard abs(result.frames - 48000) <= 2048, abs(result.firstTime) < 0.05,
              abs(result.endTime - 1) < 0.05, result.rms > 0.1, result.rms < 0.3,
              result.peak > 0.2, result.peak < 0.5 else {
            throw Failure(message: "Decoded AAC lost the tone or its duration: \(result.frames) frames, RMS \(result.rms)")
        }
        return result
    }

    private static func decode(movie: URL, frame: URL) async throws -> DecodedMovie {
        let attributes = try FileManager.default.attributesOfItem(atPath: movie.path)
        guard let fileBytes = (attributes[.size] as? NSNumber)?.uint64Value, fileBytes > 0 else {
            throw Failure(message: "Exported MP4 is missing or empty")
        }
        let asset = AVURLAsset(url: movie)
        let tracks = try await asset.loadTracks(withMediaType: .video)
        guard tracks.count == 1, let track = tracks.first else {
            throw Failure(message: "Exported MP4 must contain one video track")
        }
        let formats = try await track.load(.formatDescriptions)
        guard !formats.isEmpty, formats.allSatisfy({ CMFormatDescriptionGetMediaSubType($0) == kCMVideoCodecType_H264 }) else {
            throw Failure(message: "Exported video is not H.264")
        }
        let duration = CMTimeGetSeconds(try await asset.load(.duration))
        guard duration.isFinite else { throw Failure(message: "MP4 duration is invalid") }
        let reader = try AVAssetReader(asset: asset)
        let output = AVAssetReaderTrackOutput(track: track, outputSettings: [
            kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
        ])
        output.alwaysCopiesSampleData = false
        guard reader.canAdd(output) else { throw Failure(message: "AVAssetReader cannot decode the video track") }
        reader.add(output)
        guard reader.startReading() else {
            throw Failure(message: reader.error?.localizedDescription ?? "AVAssetReader could not start")
        }
        defer { if reader.status == .reading { reader.cancelReading() } }
        var result = DecodedMovie()
        result.duration = duration; result.fileBytes = fileBytes
        let deadline = ProcessInfo.processInfo.systemUptime + 30
        while let sample = output.copyNextSampleBuffer() {
            try Task.checkCancellation()
            guard ProcessInfo.processInfo.systemUptime < deadline, result.frameCount < 120 else {
                throw Failure(message: "Video decode exceeded its bounded fixture budget")
            }
            guard let pixelBuffer = CMSampleBufferGetImageBuffer(sample),
                  CVPixelBufferGetWidth(pixelBuffer) == 640, CVPixelBufferGetHeight(pixelBuffer) == 360,
                  CVPixelBufferGetPixelFormatType(pixelBuffer) == kCVPixelFormatType_32BGRA else {
                throw Failure(message: "Decoded video is not 640x360 BGRA")
            }
            let timestamp = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sample))
            guard timestamp.isFinite, abs(timestamp - Double(result.frameCount) / 30) < 0.002 else {
                throw Failure(message: "Decoded video has a missing or mistimed frame at index \(result.frameCount)")
            }
            result.timestamps.append(timestamp)
            if result.frameCount == 0 {
                try inspect(pixelBuffer, result: &result, frame: frame)
            }
            result.frameCount += 1
        }
        guard reader.status == .completed else {
            throw Failure(message: reader.error?.localizedDescription ?? "AVAssetReader did not finish")
        }
        return result
    }

    private static func inspect(_ buffer: CVPixelBuffer, result: inout DecodedMovie, frame: URL) throws {
        guard CVPixelBufferLockBaseAddress(buffer, .readOnly) == kCVReturnSuccess else {
            throw Failure(message: "Could not lock the decoded frame")
        }
        do {
            defer { CVPixelBufferUnlockBaseAddress(buffer, .readOnly) }
            guard let base = CVPixelBufferGetBaseAddress(buffer), CVPixelBufferGetBytesPerRow(buffer) >= 640 * 4 else {
                throw Failure(message: "Decoded frame has no readable BGRA pixels")
            }
            let bytes = base.assumingMemoryBound(to: UInt8.self)
            let stride = CVPixelBufferGetBytesPerRow(buffer)
            for y in 0..<360 {
                for x in 0..<640 {
                    let i = y * stride + x * 4
                    let rgb = max(Int(bytes[i]), Int(bytes[i + 1]), Int(bytes[i + 2]))
                    result.minRGB = min(result.minRGB, rgb); result.maxRGB = max(result.maxRGB, rgb)
                    if rgb > 48 { result.lightPixels += 1 }
                    if rgb < 16 { result.darkPixels += 1 }
                }
            }
        }
        let image = CIImage(cvPixelBuffer: buffer)
        guard let cgImage = CIContext().createCGImage(image, from: image.extent),
              let png = UIImage(cgImage: cgImage).pngData() else {
            throw Failure(message: "Could not write the independently decoded export frame")
        }
        try png.write(to: frame, options: .atomic)
    }
}
#endif
