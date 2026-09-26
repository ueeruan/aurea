// Read-only telemetry for native UI gesture tests. Absent from Release builds.
#if DEBUG
import Foundation
import UIKit

@MainActor final class ParityGestureProbe {
    private weak var preview: UIView?
    private weak var model: AureaModel?
    private let runID: String
    private let scene: String

    init?(preview: UIView, model: AureaModel) {
        let environment = ProcessInfo.processInfo.environment
        guard environment["AUREA_UI_TEST_PROBE"] == "1",
              let runID = environment["AUREA_UI_TEST_RUN_ID"], !runID.isEmpty,
              let scene = environment["AUREA_PARITY_SCENE"] else { return nil }
        self.preview = preview; self.model = model; self.runID = runID; self.scene = scene
        preview.isAccessibilityElement = true
        preview.accessibilityIdentifier = "aurea.parity.stage"
        preview.accessibilityLabel = "Aurea preview diagnostics"
        preview.accessibilityTraits = []
        update()
        // The coordinator owns this probe. Once it is released, the next tick
        // invalidates the timer; neither the model nor preview is retained.
        let timer = Timer(timeInterval: 0.2, repeats: true) { [weak self] timer in
            guard self != nil else { timer.invalidate(); return }
            Task { @MainActor [weak self] in self?.update() }
        }
        RunLoop.main.add(timer, forMode: .common)
    }

    private func update() {
        guard let preview, let model else { return }
        let file = AureaPaths.documents.appendingPathComponent("parity-ready.json")
        let readiness = (try? Data(contentsOf: file))
            .flatMap { try? JSONSerialization.jsonObject(with: $0) as? [String: Any] } ?? [:]
        let ready = (readiness["uiTestRunID"] as? String) == runID && (readiness["scene"] as? String) == scene
        let primary = model.primarySelection
        let detail = primary.flatMap { model.engine.layerDetail($0) } ?? [:]
        let shape = primary.map { model.engine.shapeParams($0) } ?? []
        let text3D = primary.flatMap { model.engine.text3D(forLayer: $0) } ?? [:]
        var stageCorners: [Float] = []
        _ = StageGeom.corners(detail, &stageCorners)
        let stageGizmo = primary.map { model.engine.gizmo($0, length: ShellStageGeometry.gizmoLength) } ?? []
        var coreStatus = AureaStatus()
        let readCoreStatus = model.engine.readStatus(&coreStatus)
        let packet: [String: Any] = [
            "playbackReport": model.engine.playbackReport(),
            "processFootprintBytes": model.engine.perf()["processFootprintBytes"] ?? 0,
            "playing": readCoreStatus ? coreStatus.playing : 0,
            "version": 1, "runID": runID, "scene": scene, "ready": ready,
            "coreStarted": model.started, "coreError": model.startError ?? "",
            "modelRevision": model.status.modelRevision, "playhead": model.status.playhead,
            "corePlayhead": readCoreStatus ? coreStatus.playhead : -1,
            "layerCount": model.layers.count, "primaryID": primary ?? 0,
            "editMode": model.editMode, "coreEditMode": model.engine.timelineEditMode,
            "effectCount": model.effects.count,
            "clipTimeRemap": primary.map { model.engine.timeRemap($0) } ?? [],
            "textGlyphLayout": (text3D["separateGlyphs"] as? NSNumber)?.boolValue ?? false,
            "textSurfaceFinish": (text3D["surfaceFinish"] as? NSNumber)?.intValue ?? 0,
            "sheet": String(describing: model.sheetContent),
            "curveProperty": model.curveProperty, "curveParam": model.curveParam,
            "selectionCount": model.selection.count, "isManipulating": model.stageManipulating,
            "canUndo": model.status.canUndo != 0, "canRedo": model.status.canRedo != 0,
            "compositionWidth": model.compositionWidth, "compositionHeight": model.compositionHeight,
            "detail": detail, "shapeParams": shape,
            "stageCorners": stageCorners, "stageGizmo": stageGizmo,
            "frameWidth": readiness["frameWidth"] ?? 0, "frameHeight": readiness["frameHeight"] ?? 0,
        ]
        guard let data = AureaJSONData(packet, false),
              let json = String(data: data, encoding: .utf8) else {
            preview.accessibilityValue = "{\"probeError\":\"Core snapshot could not be serialized\"}"
            return
        }
        preview.accessibilityValue = json
    }
}
#endif
