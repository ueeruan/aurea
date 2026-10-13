// Explicit opt-in CI fixture. Exercises the real offline Metal -> NV12 -> FFmpeg
// -> MP4 path, then independently decodes every picture with AVAssetReader.
// Imports a generated PCM tone through the production importer to verify AAC too.
// No Photos access, replacement renderer, or encoder stub.
#if DEBUG
import Foundation
import AVFoundation
import CoreImage
import UIKit
import ARKit
import simd

enum ParityExportProbe {
    private struct Failure: LocalizedError {
        let message: String
        let details: [String: Any]
        init(message: String, details: [String: Any] = [:]) { self.message = message; self.details = details }
        var errorDescription: String? { message }
    }

    /// Call only from a dedicated parity scene: this replaces the open project.
    /// The environment gate also prevents accidental use in ordinary Debug runs.
    /// A terminal report is written atomically to Documents/parity-export-ready.json.
    @MainActor
    static func run(engine: AureaEngine, documents: URL, model: AureaModel? = nil) async -> [String: Any] {
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
#if targetEnvironment(simulator)
            if let model {
                report["phase"] = "real-camera-shared"
                try writeReport()
                report["realCamera"] = try await checkRealCameraController(model: model, documents: documents)
            }
#endif
            report["phase"] = "native-feature-smoke"
            try writeReport()
            report["nativeFeatures"] = try await checkNativeFeatures(engine: engine, documents: documents)
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
            report["phase"] = "offline-stress"
            try writeReport()
            report["offlineStress"] = try await checkOfflineStress(engine: engine, documents: documents)
            report["phase"] = "legacy-offline-export"
            try writeReport()
            report["legacyOfflineExport"] = try await checkLegacyOfflineExport(engine: engine, documents: documents)
            report["phase"] = "nested-offline-audio"
            try writeReport()
            report["nestedOfflineAudio"] = try await checkNestedOfflineAudio(engine: engine, documents: documents)
            report["passed"] = true
            report["phase"] = "complete"
        } catch {
            if exportStarted { engine.cancelExport() }
            report["error"] = error.localizedDescription
            if let failure = error as? Failure, !failure.details.isEmpty { report["failureDetails"] = failure.details }
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

    /// Small native checks for build 2180. These exercise the public bridge and
    /// real Metal pixels; gesture/UI coverage remains a separate XCTest result.
    @MainActor
    private static func checkNativeFeatures(engine: AureaEngine, documents: URL) async throws -> [String: Any] {
        func project(_ title: String, width: UInt32 = 320, height: UInt32 = 180) throws -> UInt64 {
            engine.pause(); _ = engine.flush()
            guard engine.newProjectWidth(width, height: height, fps: 30, title: title),
                  let id = (engine.composition()?[AureaCompositionId] as? NSNumber)?.uint64Value else {
                throw Failure(message: "Native feature fixture unavailable: \(title)")
            }
            engine.setComposition(id, duration: 30)
            engine.setComposition(id, backgroundR: 0, g: 0, b: 0, a: 1)
            return id
        }
        func save(_ name: String) throws {
            guard engine.saveProject(documents.appendingPathComponent(name + ".aurea").path) else {
                throw Failure(message: "Native smoke commands did not drain: \(name)")
            }
        }
        func shape(x: Float, y: Float) throws -> Int64 {
            let layer = engine.addShape(0)
            guard layer >= 0 else { throw Failure(message: "Native smoke shape unavailable") }
            engine.setLayer(layer, startFrame: 0, endFrame: 30, offsetFrames: 0, setOffset: true)
            engine.setShape(layer, fillR: 1, g: 1, b: 1, a: 1)
            engine.setPosition(forLayer: layer, x: x, y: y, z: 0)
            return layer
        }
        func capture() async throws -> Data {
            let result: Data? = await withCheckedContinuation { continuation in
                DispatchQueue.global(qos: .userInitiated).async {
                    var width: UInt32 = 0, height: UInt32 = 0
                    let pixels = engine.captureFrame(320, outWidth: &width, outHeight: &height)
                    continuation.resume(returning: width == 320 && height == 180 ? pixels : nil)
                }
            }
            guard let result, result.count == 320 * 180 * 4 else { throw Failure(message: "Native smoke Metal capture unavailable") }
            return result
        }
        var result: [String: Any] = [:]
        _ = try project("PreRender native 2180", width: 960, height: 540)
        _ = try shape(x: 480, y: 270)
        try save("astra-smoke-prerender")
        defer { engine.cancelPreRender() }
        // Hold a strong native owner until startup returns; never run snapshot
        // preparation on UIKit's thread or replace the production renderer.
        let start: Int32 = await withCheckedContinuation { continuation in
            DispatchQueue.global(qos: .userInitiated).async {
                continuation.resume(returning: engine.startPreRender(direction: 1))
            }
        }
        guard start == 0 else { throw Failure(message: "Native PreRender startup failed code=\(start)") }
        let deadline = ProcessInfo.processInfo.systemUptime + 120
        var progress = PreRenderState()
        while true {
            progress = PreRenderState(engine.preRenderProgress())
            guard progress.error == 0 else { throw Failure(message: "Native PreRender failed code=\(progress.error) done=\(progress.framesDone)") }
            if progress.complete && !progress.running { break }
            guard ProcessInfo.processInfo.systemUptime < deadline else { throw Failure(message: "Native PreRender exceeded 120s") }
            try await Task.sleep(nanoseconds: 20_000_000)
        }
        guard progress.framesDone == 30, progress.framesTotal == 30, progress.height == 540,
              progress.width == 960, progress.diskBytes > 0,
              (engine.exportProgress()[AureaExportRunning] as? NSNumber)?.boolValue != true else {
            throw Failure(message: "Native PreRender dimensions/counts/export isolation differ: \(engine.preRenderProgress())")
        }
        _ = try shape(x: 300, y: 270)
        let invalidated = PreRenderState(engine.preRenderProgress())
        guard !invalidated.complete, !invalidated.running else { throw Failure(message: "Adding a layer did not invalidate native PreRender") }
        result["preRender"] = ["passed": true, "frames": progress.framesDone, "width": progress.width,
            "height": progress.height, "diskBytes": progress.diskBytes, "startupOffMain": true,
            "addLayerInvalidates": true, "uiGesturesValidated": false]

        _ = try project("Manual source getter 2180")
        let source = documents.appendingPathComponent("preview-bframes.mp4")
        guard FileManager.default.fileExists(atPath: source.path) else { throw Failure(message: "Pinned smoke video fixture missing") }
        let video = engine.importVideo(source.path, name: "Manual remap")
        guard video >= 0 else { throw Failure(message: "Manual remap video import failed") }
        engine.setLayer(video, startFrame: 0, endFrame: 30, offsetFrames: 0, setOffset: true)
        try save("astra-smoke-remap")
        engine.setTimeRemap(true, forLayer: video)
        guard engine.setTimeRemapValue(video, time: 0, value: 0),
              engine.setTimeRemapValue(video, time: 30, value: 60) else { throw Failure(message: "Manual remap keys rejected") }
        engine.seek(toFrame: 15); try save("astra-smoke-remap")
        let current = engine.timeRemapCurrentSource(video)
        guard current.isFinite, abs(current - 30) < 0.001, engine.effects(forLayer: video).isEmpty,
              engine.timeRemapCurrentSource(Int64.max).isNaN else {
            throw Failure(message: "Manual remap native source differs: actual=\(current) expected=30 without effect")
        }
        result["timeRemap"] = ["passed": true, "playhead": 15, "sourceFrames": current, "manualWithoutEffect": true]

        _ = try project("Raster volume 2180")
        let plane = try shape(x: 160, y: 90)
        engine.setScale(forLayer: plane, x: 0.3, y: 0.3, z: 1)
        let type = fxEffectTypeId("aurea.generate.raster_extrude")
        engine.addEffect(type, toLayer: plane, at: 0); try save("astra-smoke-raster")
        guard let effect = engine.effects(forLayer: plane).first,
              let id = (effect[AureaEffectId] as? NSNumber)?.uint32Value else { throw Failure(message: "Raster extrusion native effect missing") }
        engine.setEffect(id, forLayer: plane, paramIndex: 0, value: 48)
        engine.setEffectColor(id, forLayer: plane, paramIndex: 1, r: 1, g: 0, b: 0, a: 1)
        engine.setEffect(id, forLayer: plane, paramIndex: 2, value: 0)
        try save("astra-smoke-raster")
        let front = try await capture()
        engine.setRotation(forLayer: plane, x: 0, y: 55, z: 0); try save("astra-smoke-raster-side")
        let side = try await capture()
        func counts(_ data: Data) -> (light: Int, red: Int) {
            var light = 0, red = 0
            data.withUnsafeBytes { raw in
                let pixels = raw.bindMemory(to: UInt8.self)
                for i in stride(from: 0, to: pixels.count, by: 4) {
                    if pixels[i] > 180 && pixels[i+1] > 180 && pixels[i+2] > 180 { light += 1 }
                    if pixels[i] > 90 && Int(pixels[i]) > Int(pixels[i+1]) + 40 && Int(pixels[i]) > Int(pixels[i+2]) + 40 { red += 1 }
                }
            }
            return (light, red)
        }
        let frontCounts = counts(front), sideCounts = counts(side)
        try front.write(to: documents.appendingPathComponent("astra-smoke-raster-front.rgba"), options: .atomic)
        try side.write(to: documents.appendingPathComponent("astra-smoke-raster-side.rgba"), options: .atomic)
        guard frontCounts.light > 100, sideCounts.red > frontCounts.red + 20, front != side else {
            throw Failure(message: "Raster volume front/side failed frontLight=\(frontCounts.light) frontRed=\(frontCounts.red) sideRed=\(sideCounts.red)")
        }
        result["rasterExtrude"] = ["passed": true, "frontLight": frontCounts.light,
            "frontRed": frontCounts.red, "sideRed": sideCounts.red, "rotationY": 55, "realMetalCapture": true]
        return result
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
        for (name, amount) in [("bender", Float(35)), ("bend", 65), ("curl", 145), ("page_turn", 50), ("mesh_warp", 0)] {
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
            if name == "mesh_warp" {
                let meshID = Int32(truncatingIfNeeded: effectID)
                guard engine.meshWarpToggleKey(layer, effect: meshID) else { throw Failure(message: "Mesh identity key could not be marked") }
                engine.seek(toFrame: 30); _ = engine.flush()
                guard engine.saveProject(project.path), engine.meshWarpDrag(layer, effect: meshID, vertex: 36, grip: 0, u: 0.85, v: 0.65, autoKey: false, continuing: false) else {
                    throw Failure(message: "Animated Mesh Warp drag did not create the second key")
                }
                let meshKeys = engine.allKeyframes().filter { ($0["layerId"] as? NSNumber)?.int64Value == layer }
                    .flatMap { $0["keys"] as? [[String: Any]] ?? [] }
                    .filter { ($0["property"] as? NSNumber)?.uint32Value == 44 && ($0["effectIndex"] as? NSNumber)?.uint32Value == effectID }
                guard meshKeys.count == 2 else { throw Failure(message: "Mesh keys are invisible to the native timeline: \(meshKeys.count)") }
            } else { engine.setEffect(effectID, forLayer: layer, paramIndex: 0, value: amount) }
            if name == "curl" { engine.setEffect(effectID, forLayer: layer, paramIndex: 1, value: 32) }
            if name == "page_turn" { engine.setEffect(effectID, forLayer: layer, paramIndex: 2, value: 16) }
            guard engine.saveProject(project.path) else { throw Failure(message: "Paper parameters could not be saved") }
            let deformed = try await capture(name + "-deformed")
            let changed = difference(original, deformed)
            guard changed > 400 else { throw Failure(message: "Paper effect did not change its pixels: \(name)") }
            guard engine.loadProject(project.path) else { throw Failure(message: "Paper project did not reopen") }
            if name == "mesh_warp" { engine.seek(toFrame: 30); _ = engine.flush(); _ = engine.saveProject(project.path) }
            let reopened = try await capture(name + "-reopened")
            guard difference(deformed, reopened) == 0 else { throw Failure(message: "Paper persistence changed pixels: \(name)") }
            if name == "mesh_warp" {
                guard engine.meshWarpReset(layer, effect: Int32(truncatingIfNeeded: effectID)) else { throw Failure(message: "Mesh reset failed") }
            } else { engine.setEffect(effectID, forLayer: layer, paramIndex: 0, value: 0) }
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
        var width = 640
        var height = 360
        var distinctPictures = 0
        var frameCount = 0
        var duration = 0.0
        var fileBytes: UInt64 = 0
        var timestamps: [Double] = []
        var lightPixels = 0
        var darkPixels = 0
        var minRGB = 255
        var maxRGB = 0
        var dictionary: [String: Any] {
            ["codec": "h264", "width": width, "height": height, "frames": frameCount,
             "durationSeconds": duration, "fileBytes": fileBytes, "timestampsSeconds": timestamps,
             "lightPixels": lightPixels, "darkPixels": darkPixels, "minRGB": minRGB, "maxRGB": maxRGB,
             "distinctPictures": distinctPictures]
        }
    }

    @MainActor
    private static func checkOfflineStress(engine: AureaEngine, documents: URL) async throws -> [[String: Any]] {
        var results: [[String: Any]] = []
        for heavy in [true, false] {
            let name = heavy ? "astra-heavy3d-motionblur" : "astra-long-120s"
            let width = heavy ? 960 : 320, height = heavy ? 540 : 180
            let frames = heavy ? 60 : 3600
            let movie = documents.appendingPathComponent("\(name).mp4")
            let frame = documents.appendingPathComponent("\(name).png")
            let project = documents.appendingPathComponent("\(name).aurea")
            engine.pause()
            guard engine.newProjectWidth(UInt32(width), height: UInt32(height), fps: 30, title: name),
                  let composition = engine.composition(),
                  let id = (composition[AureaCompositionId] as? NSNumber)?.uint64Value else {
                throw Failure(message: "Could not create \(name)")
            }
            engine.setComposition(id, duration: Int64(frames))
            engine.setComposition(id, backgroundR: 0.03, g: 0.03, b: 0.03, a: 1)
            if heavy {
                for index in 0..<24 {
                    let layer = engine.addShape3D(kind: UInt32(index % 3), name: "Offline mesh \(index)")
                    guard layer >= 0 else { throw Failure(message: "Offline 3D mesh creation failed") }
                    engine.setLayer(layer, startFrame: 0, endFrame: Int32(frames), offsetFrames: 0, setOffset: true)
                    engine.setScale(forLayer: layer, x: 0.3, y: 0.3, z: 0.3)
                    engine.setPosition(forLayer: layer, x: Float(100 + index % 6 * 145), y: Float(75 + index / 6 * 125), z: 0)
                    engine.insertKeyframe(forLayer: layer, property: 7, time: 0, value: 0)
                    engine.insertKeyframe(forLayer: layer, property: 7, time: Int32(frames - 1), value: 100)
                    engine.setMotionBlur(true, forLayer: layer)
                }
                let obj = documents.appendingPathComponent("astra-owned-triangle.obj")
                try "o OwnedTriangle\nv -1 -1 0\nv 1 -1 0\nv 0 1 0\nf 1 2 3\n".write(to: obj, atomically: true, encoding: .utf8)
                for (index, file) in ["DamagedHelmet.glb", "Fox.glb", "animated-character.fbx", obj.lastPathComponent].enumerated() {
                    let layer = engine.importModel(documents.appendingPathComponent(file).path, name: "Offline imported \(file)")
                    guard layer >= 0 else { throw Failure(message: "Offline \(file) import failed") }
                    engine.setLayer(layer, startFrame: 0, endFrame: Int32(frames), offsetFrames: 0, setOffset: true)
                    engine.setScale(forLayer: layer, x: 0.5, y: 0.5, z: 0.5)
                    engine.setPosition(forLayer: layer, x: Float(120 + index * 240), y: 270, z: 0)
                    engine.insertKeyframe(forLayer: layer, property: 7, time: 0, value: 0)
                    engine.insertKeyframe(forLayer: layer, property: 7, time: Int32(frames - 1), value: 75)
                    engine.setMotionBlur(true, forLayer: layer)
                }
                guard engine.addParticles(20) >= 0, engine.addText("ASTRA OFFLINE") >= 0,
                      engine.setMotionBlurSettings(true, shutter: 270, phase: -135, samples: 8, adaptiveLimit: 16) else {
                    throw Failure(message: "Offline particles, text, or motion blur fixture failed")
                }
            } else {
                let shape = engine.addShape(1)
                guard shape >= 0 else { throw Failure(message: "Long composition shape failed") }
                engine.setLayer(shape, startFrame: 0, endFrame: Int32(frames), offsetFrames: 0, setOffset: true)
                engine.setScale(forLayer: shape, x: 0.18, y: 0.18, z: 1)
                engine.setShape(shape, fillR: 0.95, g: 0.1, b: 0.15, a: 1)
                engine.setPosition(forLayer: shape, x: 64, y: 90, z: 0)
                engine.insertKeyframe(forLayer: shape, property: 0, time: 0, value: 64)
                engine.insertKeyframe(forLayer: shape, property: 0, time: Int32(frames - 1), value: 256)
                let tone = documents.appendingPathComponent("astra-long-stereo.wav")
                try writeTone(to: tone, seconds: 120)
                guard engine.importAudio(tone.path, name: "Owned two minute stereo") >= 0 else {
                    throw Failure(message: "Long composition WAV import failed")
                }
            }
            guard engine.saveProject(project.path) else { throw Failure(message: "Offline fixture commands did not drain") }
            var running = false
            do {
                guard engine.startExport(to: movie.path, codec: .h264, height: UInt32(height), fps: 30,
                                         bitrateMbps: 4, audioBitrateKbps: 128) else {
                    throw Failure(message: "Offline \(name) export could not start")
                }
                running = true
                let deadline = ProcessInfo.processInfo.systemUptime + 1800
                var progress: [String: Any] = [:]
                while true {
                    try Task.checkCancellation()
                    progress = engine.exportProgress()
                    if (progress[AureaExportFinished] as? NSNumber)?.boolValue == true {
                        running = false
                        guard (progress[AureaExportResult] as? NSNumber)?.intValue == 0,
                              (progress[AureaExportFramesDone] as? NSNumber)?.intValue == frames,
                              (progress[AureaExportFramesTotal] as? NSNumber)?.intValue == frames,
                              ((progress[AureaExportFlags] as? NSNumber)?.uint32Value ?? 0) & 8 == 0 else {
                            throw Failure(message: "Offline \(name) lost a picture or failed: \(progress)")
                        }
                        break
                    }
                    guard ProcessInfo.processInfo.systemUptime < deadline else { throw Failure(message: "Offline \(name) timed out") }
                    try await Task.sleep(nanoseconds: 100_000_000)
                }
                let decoded = try await Task.detached(priority: .utility) {
                    try await decode(movie: movie, frame: frame, width: width, height: height, frames: frames)
                }.value
                guard decoded.frameCount == frames, decoded.distinctPictures > 1,
                      decoded.maxRGB - decoded.minRGB > 40 else {
                    throw Failure(message: "Offline \(name) decoded blank, repeated, or missing pictures")
                }
                var result: [String: Any] = ["name": name, "passed": true, "exportProgress": progress,
                                           "decoded": decoded.dictionary, "movieFile": movie.lastPathComponent]
                if !heavy {
                    result["decodedAudio"] = try await Task.detached(priority: .utility) {
                        try await decodeAudio(movie: movie, seconds: 120)
                    }.value.dictionary
                }
                results.append(result)
                if let data = AureaJSONData(["passed": true, "cases": results], true) {
                    try data.write(to: documents.appendingPathComponent("astra-offline-ready.json"), options: .atomic)
                }
            } catch {
                if running { engine.cancelExport() }
                throw error
            }
        }
        return results
    }

#if targetEnvironment(simulator)
    @MainActor
    private static func checkRealCameraController(model: AureaModel, documents: URL) async throws -> [String: Any] {
        let engine = model.engine
        engine.pause()
        guard engine.newProjectWidth(320, height: 180, fps: 30, title: "Shared Real Camera") else {
            throw Failure(message: "Real camera fixture could not be created")
        }
        model.refreshModel(force: true)
        model.startRealCamera()
        let camera = RealCameraState.shared
        defer { camera.stop(model) }
        let layer = camera.layer
        guard camera.active, !ARWorldTrackingConfiguration.isSupported else {
            throw Failure(message: "Simulator real camera session did not report its actual capability")
        }
        let initial = (engine.layerDetail(layer)?[AureaDetailPosition] as? [NSNumber] ?? []).map(\.floatValue)
        guard initial.count == 3 else { throw Failure(message: "Camera position is unavailable") }
        var pose = matrix_identity_float4x4
        guard camera.paritySubmitPose(pose, timestamp: 1, positional: true) else {
            throw Failure(message: "Native camera reference pose was rejected")
        }
        pose.columns.3 = SIMD4<Float>(0.25, -0.10, 0.20, 1)
        for index in 0..<5 {
            guard camera.paritySubmitPose(pose, timestamp: 2 + Double(index) * 0.1, positional: true) else {
                throw Failure(message: "Native camera translation was rejected")
            }
        }
        let translated = (engine.layerDetail(layer)?[AureaDetailPosition] as? [NSNumber] ?? []).map(\.floatValue)
        let expected: [Float] = [initial[0] + 22.5, initial[1] + 9, initial[2] - 18]
        guard translated.count == 3, zip(translated, expected).allSatisfy({ pair in abs(pair.0 - pair.1) < 0.01 }) else {
            throw Failure(message: "Camera translation did not preserve original position/OpenGL axes/metre scale")
        }
        // This is the same action invoked by the production Record/Stop chips.
        camera.toggleRecording(model)
        guard camera.recording else { throw Failure(message: "Swift Record did not arm the native session") }
        var frames = Set<Int64>()
        var heldFrames = Set<Int64>()
        var callbacks: [[String: Any]] = []
        for index in 0..<12 {
            let angle = Float(index) * 0.04
            pose = simd_float4x4(simd_quatf(angle: angle, axis: SIMD3<Float>(0, 1, 0)))
            pose.columns.3 = SIMD4<Float>(0.25 + Float(index) * 0.01, -0.10, 0.20, 1)
            let previousFrame = camera.lastFrame
            let accepted = index >= 8 ? camera.parityHoldTracking()
                : camera.paritySubmitPose(pose, timestamp: 3 + Double(index) * 0.05, positional: true)
            guard accepted, camera.recording, camera.lastFrame >= 0 else {
                throw Failure(message: "Swift pose/hold callback stopped before native camera keys were recorded")
            }
            frames.insert(camera.lastFrame)
            let newFrame = camera.lastFrame != previousFrame
            callbacks.append(["index": index, "hold": index >= 8, "beforeFrame": previousFrame,
                "afterFrame": camera.lastFrame, "newRecordedFrame": newFrame])
            // The native recorder accepts repeated callbacks in one frame but
            // deliberately keeps that frame's first key. A successful no-op
            // hold cannot relabel an earlier motion sample as a new held key.
            if index >= 8 && newFrame { heldFrames.insert(camera.lastFrame) }
            try await Task.sleep(nanoseconds: 50_000_000)
        }
        camera.toggleRecording(model)
        guard !camera.recording else { throw Failure(message: "Swift Stop did not end native recording") }
        let keys = engine.keyframes(forLayer: layer)
        let holdDiagnostics: [String: Any] = ["callbacks": callbacks, "heldFrames": heldFrames.sorted(), "keys": keys]
        if let data = AureaJSONData(holdDiagnostics, true) {
            try data.write(to: documents.appendingPathComponent("astra-real-camera-hold.json"), options: .atomic)
        }
        let expectedProperties = Set([0, 1, 2, 6, 7, 8])
        let properties = Set(keys.compactMap { ($0[AureaKeyframeProperty] as? NSNumber)?.intValue })
        let uniquePairs = Set(keys.compactMap { row -> String? in
            guard let property = row[AureaKeyframeProperty] as? NSNumber,
                  let time = row[AureaKeyframeTime] as? NSNumber else { return nil }
            return "\(property.intValue):\(time.int64Value)"
        })
        guard frames.count >= 2, properties == expectedProperties, keys.count == frames.count * 6,
              uniquePairs.count == keys.count,
              keys.allSatisfy({ ($0[AureaKeyframeInterpolation] as? NSNumber)?.intValue == 1 }) else {
            throw Failure(message: "Camera capture did not create six unique linear tracks on native frames")
        }
        guard heldFrames.count >= 2, !camera.trackingValid else {
            throw Failure(message: "Tracking loss did not retain recording over distinct native frames")
        }
        for property in expectedProperties {
            let rows = keys.filter { row in
                guard let p = row[AureaKeyframeProperty] as? NSNumber,
                      let t = row[AureaKeyframeTime] as? NSNumber else { return false }
                return p.intValue == property && heldFrames.contains(t.int64Value)
            }
            let held = rows.compactMap { ($0[AureaKeyframeValue] as? NSNumber)?.floatValue }
            guard held.count == heldFrames.count, let first = held.first,
                  held.allSatisfy({ abs($0 - first) < 0.00001 }) else {
                let actuals = rows.map { ["time": $0[AureaKeyframeTime] ?? NSNull(), "value": $0[AureaKeyframeValue] ?? NSNull()] }
                throw Failure(message: "Tracking loss values differ property=\(property) times=\(heldFrames.sorted()) actuals=\(actuals)",
                    details: ["property": property, "heldFrames": heldFrames.sorted(), "actuals": actuals,
                        "callbacks": callbacks, "expectedConstantTolerance": 0.00001])
            }
        }
        let fixture = documents.appendingPathComponent("astra-real-camera-capture.aurea")
        guard engine.saveProject(fixture.path) else { throw Failure(message: "Recorded camera project was not saved") }
        engine.run { $0.undo() }
        guard engine.saveProject(documents.appendingPathComponent("astra-real-camera-undo.aurea").path),
              engine.keyframes(forLayer: layer).isEmpty else {
            throw Failure(message: "One native undo did not remove the entire camera recording")
        }
        engine.run { $0.redo() }
        guard engine.saveProject(fixture.path), engine.keyframes(forLayer: layer).count == keys.count else {
            throw Failure(message: "One native redo did not restore the camera recording")
        }
        guard camera.paritySubmitPose(matrix_identity_float4x4, timestamp: 4, positional: false),
              !camera.positional else { throw Failure(message: "Rotation fallback incorrectly claimed position") }
        return ["passed": true, "sensorSource": "synthetic transport into production Swift controller",
                "physicalTrackingValidated": false, "arWorldTrackingSupported": false,
                "controllerRecordStop": true, "positionBefore": initial, "positionAfter": translated,
                "nativeFrames": frames.sorted(), "keys": keys.count, "linearTracks": properties.sorted(),
                "oneUndoRedo": true, "rotationFallbackPositional": camera.positional,
                "trackingLossHeldFrames": heldFrames.sorted(), "trackingLossKeptPoseConstant": true,
                "callbacks": callbacks, "ignoredSameFrameHold": callbacks.filter { ($0["hold"] as? Bool) == true && ($0["newRecordedFrame"] as? Bool) == false }.count,
                "projectFile": fixture.lastPathComponent]
    }
#endif

    @MainActor
    private static func checkLegacyOfflineExport(engine: AureaEngine, documents: URL) async throws -> [String: Any] {
        let original = documents.appendingPathComponent("astra-legacy-square-glow.aurea")
        let originalBytes = try Data(contentsOf: original)
        let movie = documents.appendingPathComponent("astra-legacy-square-glow.mp4")
        let frame = documents.appendingPathComponent("astra-legacy-square-glow.png")
        engine.pause()
        guard engine.loadProject(original.path), engine.lastLoadNotice & 11 == 0, engine.lastLoadMissingAssets == 0 else {
            throw Failure(message: "Original Android project could not be read with all assets and layers")
        }
        var status = AureaStatus()
        guard engine.readStatus(&status), status.compFps > 0 else { throw Failure(message: "Legacy project status is unavailable") }
        let sourceFrames = Double(status.duration)
        let sourceFps = Double(status.compFps)
        let outputFrames = sourceFrames * 30.0 / sourceFps
        let frames = Int(ceil(outputFrames - 1.0e-8))
        let ratio = Double(status.compWidth) / Double(status.compHeight)
        let width = ratio >= 1 ? Int((180 * ratio / 2).rounded()) * 2 : 180
        let height = ratio >= 1 ? 180 : Int((180 / ratio / 2).rounded()) * 2
        var running = false
        defer { if running { engine.cancelExport() } }
        guard engine.startExport(to: movie.path, codec: .h264, height: 180, fps: 30, bitrateMbps: 4, audioBitrateKbps: 128) else {
            throw Failure(message: "Legacy offline export did not start")
        }
        running = true
        let deadline = ProcessInfo.processInfo.systemUptime + 1800
        var progress: [String: Any] = [:]
        while true {
            try Task.checkCancellation()
            progress = engine.exportProgress()
            if (progress[AureaExportFinished] as? NSNumber)?.boolValue == true {
                running = false
                guard (progress[AureaExportResult] as? NSNumber)?.intValue == 0,
                      (progress[AureaExportFramesDone] as? NSNumber)?.intValue == frames,
                      ((progress[AureaExportFlags] as? NSNumber)?.uint32Value ?? 0) & 8 == 0 else {
                    throw Failure(message: "Legacy offline export failed: \(progress)")
                }
                break
            }
            guard ProcessInfo.processInfo.systemUptime < deadline else { throw Failure(message: "Legacy offline export timed out") }
            try await Task.sleep(nanoseconds: 100_000_000)
        }
        let decoded = try await Task.detached(priority: .utility) {
            try await decode(movie: movie, frame: frame, width: width, height: height, frames: frames)
        }.value
        guard originalBytes == (try Data(contentsOf: original)), decoded.frameCount == frames,
              decoded.maxRGB - decoded.minRGB > 40 else {
            throw Failure(message: "Legacy original changed, or its exported composition is incomplete")
        }
        return ["passed": true, "originalUnchanged": true, "originalBytes": originalBytes.count,
                "loadNotice": engine.lastLoadNotice, "decoded": decoded.dictionary, "exportProgress": progress]
    }

    @MainActor
    private static func checkNestedOfflineAudio(engine: AureaEngine, documents: URL) async throws -> [[String: Any]] {
        engine.pause()
        guard engine.newProjectWidth(320, height: 180, fps: 30, title: "Owned nested stereo clock"),
              let composition = engine.composition(),
              let id = (composition[AureaCompositionId] as? NSNumber)?.uint64Value else {
            throw Failure(message: "Nested clock project could not be created")
        }
        engine.setComposition(id, duration: 60)
        let shape = engine.addShape(1)
        guard shape >= 0 else { throw Failure(message: "Nested clock shape could not be created") }
        engine.setLayer(shape, startFrame: 0, endFrame: 60, offsetFrames: 0, setOffset: true)
        engine.setScale(forLayer: shape, x: 0.18, y: 0.18, z: 1)
        engine.setShape(shape, fillR: 0.95, g: 0.1, b: 0.15, a: 1)
        engine.setPosition(forLayer: shape, x: 64, y: 90, z: 0)
        engine.insertKeyframe(forLayer: shape, property: 0, time: 0, value: 64)
        engine.insertKeyframe(forLayer: shape, property: 0, time: 59, value: 256)
        let tone = documents.appendingPathComponent("astra-nested-stereo.wav")
        try writeTone(to: tone, seconds: 2)
        let audio = engine.importAudio(tone.path, name: "Owned nested stereo")
        guard audio >= 0 else { throw Failure(message: "Nested clock audio could not be imported") }
        let nested = engine.precomposeLayers([NSNumber(value: shape), NSNumber(value: audio)], name: "Owned nested bus")
        guard nested >= 0 else { throw Failure(message: "Nested clock bus could not be created") }
        engine.setComposition(id, duration: 30)
        var results: [[String: Any]] = []
        for remap in [false, true] {
            let name = remap ? "astra-nested-remap2x" : "astra-nested-speed2x"
            engine.setLayer(nested, speed: remap ? 1 : 2)
            engine.setLayer(nested, startFrame: 0, endFrame: 30, offsetFrames: 0, setOffset: true)
            guard engine.saveProject(documents.appendingPathComponent("\(name).aurea").path) else {
                throw Failure(message: "Nested fixture commands did not drain")
            }
            if remap {
                engine.setTimeRemap(true, forLayer: nested)
                guard engine.setTimeRemapValue(nested, time: 0, value: 0),
                      engine.setTimeRemapValue(nested, time: 30, value: 60) else {
                    throw Failure(message: "Nested source-clock remap could not be set")
                }
            }
            engine.setComposition(id, duration: 30)
            guard engine.saveProject(documents.appendingPathComponent("\(name)-frozen.aurea").path) else {
                throw Failure(message: "Nested fixture clock could not be frozen")
            }
            let movie = documents.appendingPathComponent("\(name).mp4")
            let frame = documents.appendingPathComponent("\(name).png")
            var running = false
            do {
                guard engine.startExport(to: movie.path, codec: .h264, height: 180, fps: 30, bitrateMbps: 4, audioBitrateKbps: 128) else {
                    throw Failure(message: "Nested offline export could not start")
                }
                running = true
                let deadline = ProcessInfo.processInfo.systemUptime + 180
                var progress: [String: Any] = [:]
                while true {
                    progress = engine.exportProgress()
                    if (progress[AureaExportFinished] as? NSNumber)?.boolValue == true {
                        running = false
                        guard (progress[AureaExportResult] as? NSNumber)?.intValue == 0,
                              (progress[AureaExportFramesDone] as? NSNumber)?.intValue == 30 else {
                            throw Failure(message: "Nested offline export failed: \(progress)")
                        }
                        break
                    }
                    guard ProcessInfo.processInfo.systemUptime < deadline else { throw Failure(message: "Nested export timed out") }
                    try await Task.sleep(nanoseconds: 50_000_000)
                }
                let decoded = try await Task.detached(priority: .utility) {
                    try await decode(movie: movie, frame: frame, width: 320, height: 180, frames: 30)
                }.value
                let pcm = try await Task.detached(priority: .utility) {
                    try await decodeAudio(movie: movie, expectedTones: [880, 1320])
                }.value
                guard decoded.distinctPictures > 1 else { throw Failure(message: "Nested video did not follow its source clock") }
                results.append(["name": name, "passed": true, "decoded": decoded.dictionary,
                                "decodedAudio": pcm.dictionary, "exportProgress": progress])
            } catch { if running { engine.cancelExport() }; throw error }
        }
        return results
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

    private static func writeTone(to url: URL, seconds: Int = 1) throws {
        let rate = 48_000, channels = 2, byteCount = rate * seconds * channels * 2
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
        FileManager.default.createFile(atPath: url.path, contents: nil)
        let output = try FileHandle(forWritingTo: url)
        defer { try? output.close() }
        try output.truncate(atOffset: 0)
        try output.write(contentsOf: bytes)
        bytes.removeAll(keepingCapacity: true)
        for sample in 0..<(rate * seconds) {
            for frequency in [440.0, 660.0] {
                let amplitude = sin(2 * Double.pi * frequency * Double(sample) / Double(rate)) * 8192
                littleEndian(Int16(amplitude.rounded()))
            }
            if bytes.count >= 16384 { try output.write(contentsOf: bytes); bytes.removeAll(keepingCapacity: true) }
        }
        if !bytes.isEmpty { try output.write(contentsOf: bytes) }
    }

    private struct DecodedAudio: Sendable {
        var frames = 0
        var buffers = 0
        var squaredSum = 0.0
        var peak = 0.0
        var firstTime = Double.nan
        var endTime = 0.0
        var toneHz: [Double] = []
        var toneCorrelations: [Double] = []
        var tonePhases: [Double] = []
        var rms: Double { frames > 0 ? sqrt(squaredSum / Double(frames * 2)) : 0 }
        var dictionary: [String: Any] {
            ["codec": "aac", "sampleRate": 48000, "channels": 2, "frames": frames,
             "buffers": buffers, "firstTimestampSeconds": firstTime,
             "endTimestampSeconds": endTime, "rms": rms, "peak": peak,
             "sourceClockToneHz": toneHz, "sourceClockCorrelations": toneCorrelations,
             "sourceClockPhaseRadians": tonePhases]
        }
    }

    private static func decodeAudio(movie: URL, seconds: Int = 1, expectedTones: [Double]? = nil) async throws -> DecodedAudio {
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
        var toneSine = [Double](repeating: 0, count: 2), toneCosine = toneSine, tonePower = toneSine
        var toneSamples = 0
        while let sample = output.copyNextSampleBuffer() {
            try Task.checkCancellation()
            guard result.buffers < 200 * seconds, let format = CMSampleBufferGetFormatDescription(sample),
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
            guard time.isFinite, result.buffers == 0 || abs(time - result.endTime) <= 2.0 / 48000 else {
                throw Failure(message: "Decoded AAC has discontinuous sample timestamps")
            }
            if result.buffers == 0 { result.firstTime = time }
            result.endTime = time + Double(frames) / 48000
            if let frequencies = expectedTones {
                guard frequencies.count == 2 else { throw Failure(message: "Known source clock requires stereo tone frequencies") }
                for sampleFrame in 0..<frames {
                    let clock = time + Double(sampleFrame) / 48000
                    if clock < Double(seconds) * 0.25 || clock >= Double(seconds) * 0.75 { continue }
                    for channel in 0..<2 {
                        let value = Double(values[sampleFrame * 2 + channel])
                        let phase = 2 * Double.pi * frequencies[channel] * clock
                        toneSine[channel] += value * sin(phase)
                        toneCosine[channel] += value * cos(phase)
                        tonePower[channel] += value * value
                    }
                    toneSamples += 1
                }
            }
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
        if let frequencies = expectedTones {
            guard toneSamples > 0 else { throw Failure(message: "Decoded nested audio misses its known source-clock interval") }
            result.toneHz = frequencies
            for channel in 0..<2 {
                let correlation = tonePower[channel] > 0 ? sqrt(2 * (toneSine[channel] * toneSine[channel] + toneCosine[channel] * toneCosine[channel]) / (Double(toneSamples) * tonePower[channel])) : 0
                let phase = atan2(toneCosine[channel], toneSine[channel])
                guard correlation > 0.97, abs(phase) < 0.35 else {
                    throw Failure(message: "Nested channel \(channel) misses its video source clock: correlation \(correlation), phase \(phase)")
                }
                result.toneCorrelations.append(correlation); result.tonePhases.append(phase)
            }
        }
        // AAC packets may expose codec priming/padding around the one-second edit.
        guard abs(result.frames - 48000 * seconds) <= 2048, abs(result.firstTime) < 0.05,
              abs(result.endTime - Double(seconds)) < 0.05, result.rms > 0.1, result.rms < 0.3,
              result.peak > 0.2, result.peak < 0.5 else {
            throw Failure(message: "Decoded AAC lost the tone or its duration: \(result.frames) frames, RMS \(result.rms)")
        }
        return result
    }

    private static func decode(movie: URL, frame: URL, width: Int = 640, height: Int = 360, frames: Int = 30) async throws -> DecodedMovie {
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
        result.width = width; result.height = height
        result.duration = duration; result.fileBytes = fileBytes
        var hashes = Set<UInt64>()
        let deadline = ProcessInfo.processInfo.systemUptime + 600
        while let sample = output.copyNextSampleBuffer() {
            try Task.checkCancellation()
            guard ProcessInfo.processInfo.systemUptime < deadline, result.frameCount < frames else {
                throw Failure(message: "Video decode exceeded its bounded fixture budget")
            }
            guard let pixelBuffer = CMSampleBufferGetImageBuffer(sample),
                  CVPixelBufferGetWidth(pixelBuffer) == width, CVPixelBufferGetHeight(pixelBuffer) == height,
                  CVPixelBufferGetPixelFormatType(pixelBuffer) == kCVPixelFormatType_32BGRA else {
                throw Failure(message: "Decoded video dimensions or format differ")
            }
            let timestamp = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sample))
            guard timestamp.isFinite, abs(timestamp - Double(result.frameCount) / 30) <= 0.000002 else {
                throw Failure(message: "Decoded video has a missing or mistimed frame at index \(result.frameCount)")
            }
            result.timestamps.append(timestamp)
            hashes.insert(try pictureHash(pixelBuffer))
            if result.frameCount == 0 {
                try inspect(pixelBuffer, result: &result, frame: frame)
            }
            result.frameCount += 1
        }
        guard reader.status == .completed else {
            throw Failure(message: reader.error?.localizedDescription ?? "AVAssetReader did not finish")
        }
        guard result.frameCount == frames, abs(duration - Double(frames) / 30) < 0.04 else {
            throw Failure(message: "Decoded movie duration or picture count differs")
        }
        result.distinctPictures = hashes.count
        return result
    }

    private static func pictureHash(_ buffer: CVPixelBuffer) throws -> UInt64 {
        guard CVPixelBufferLockBaseAddress(buffer, .readOnly) == kCVReturnSuccess else { throw Failure(message: "Decoded picture lock failed") }
        defer { CVPixelBufferUnlockBaseAddress(buffer, .readOnly) }
        guard let base = CVPixelBufferGetBaseAddress(buffer) else { throw Failure(message: "Decoded picture pixels absent") }
        let bytes = base.assumingMemoryBound(to: UInt8.self), row = CVPixelBufferGetBytesPerRow(buffer)
        var hash: UInt64 = 1469598103934665603
        for y in stride(from: 0, to: CVPixelBufferGetHeight(buffer), by: 4) {
            for x in stride(from: 0, to: CVPixelBufferGetWidth(buffer), by: 4) {
                for channel in 0..<3 { hash = (hash ^ UInt64(bytes[y * row + x * 4 + channel])) &* 1099511628211 }
            }
        }
        return hash
    }

    private static func inspect(_ buffer: CVPixelBuffer, result: inout DecodedMovie, frame: URL) throws {
        guard CVPixelBufferLockBaseAddress(buffer, .readOnly) == kCVReturnSuccess else {
            throw Failure(message: "Could not lock the decoded frame")
        }
        do {
            defer { CVPixelBufferUnlockBaseAddress(buffer, .readOnly) }
            guard let base = CVPixelBufferGetBaseAddress(buffer), CVPixelBufferGetBytesPerRow(buffer) >= result.width * 4 else {
                throw Failure(message: "Decoded frame has no readable BGRA pixels")
            }
            let bytes = base.assumingMemoryBound(to: UInt8.self)
            let stride = CVPixelBufferGetBytesPerRow(buffer)
            for y in 0..<result.height {
                for x in 0..<result.width {
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
