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
        let stageGizmo = primary.map { model.engine.gizmo($0, length: ShellStageGeometry.gizmoLength, localSpace: model.gizmoAxesLocal) } ?? []
        var coreStatus = AureaStatus()
        let readCoreStatus = model.engine.readStatus(&coreStatus)
        let keySelectionCount: Int = model.timelineKeySelection?.count ?? -1
        let textAnimatorCount = primary.map { model.engine.textAnimators($0).count / 40 } ?? 0
        // Deslocamento (parâmetro 5) do primeiro Text Transform da escolhida, lido no motor.
        var textTransformOffset: [Float] = []
        if let primary, let effect = model.engine.effects(forLayer: primary).first(where: {
            ($0["typeId"] as? NSNumber)?.uint32Value == fxEffectTypeId("aurea.text.transform")
        }), let effectId = (effect["effectId"] as? NSNumber)?.uint32Value,
           let offset = model.engine.effectParams(forLayer: primary, effectId: effectId).first(where: { ($0["index"] as? NSNumber)?.uint32Value == 5 }) {
            textTransformOffset = Array(((offset["value"] as? [NSNumber]) ?? []).prefix(2).map(\.floatValue))
        }
        let packet: [String: Any] = [
            "textAnimatorCount": textAnimatorCount,
            "textTransformOffset": textTransformOffset,
            "layerNames": model.layers.map(\.name),
            "layerCenters": model.layers.map { (row: LayerItem) -> [Float] in
                var c: [Float] = []
                guard let d = model.engine.layerDetail(row.id), StageGeom.corners(d, &c), c.count == 8 else { return [] }
                return [(c[0] + c[2] + c[4] + c[6]) / 4, (c[1] + c[3] + c[5] + c[7]) / 4]
            },
            "playbackReport": model.engine.playbackReport(),
            "processFootprintBytes": model.engine.perf()["processFootprintBytes"] ?? 0,
            "playing": readCoreStatus ? coreStatus.playing : 0,
            "version": 1, "runID": runID, "scene": scene, "ready": ready,
            "coreStarted": model.started, "coreError": model.startError ?? "",
            "modelRevision": model.status.modelRevision, "playhead": model.status.playhead,
            "corePlayhead": readCoreStatus ? coreStatus.playhead : -1,
            "previewBufferRanges": model.previewBufferRanges.map { [$0.lowerBound, $0.upperBound] },
            "motionBlurSettings": model.engine.motionBlurSettings().map(\.floatValue),
            "layerCount": model.layers.count, "primaryID": primary ?? 0,
            "layerOrder": model.layers.map { $0.id },
            "layerStarts": model.layers.map { (row: LayerItem) -> Int64 in Int64(row.startFrame) },
            "layerEnds": model.layers.map { (row: LayerItem) -> Int64 in Int64(row.endFrame) },
            "layerParents": model.layers.map { (row: LayerItem) -> Int64 in
                (model.engine.layerDetail(row.id)?["parentId"] as? NSNumber)?.int64Value ?? 0
            },
            "markerCount": model.markerFrames.count,
            "missingAssets": model.engine.lastLoadMissingAssets,
            "curveKeys": (primary.flatMap { model.keyframes[$0] } ?? []).map {
                ["property": Int($0.property), "time": Int($0.time), "interpolation": Int($0.interpolation),
                 "value": Double($0.value)] as [String: Any]
            },
            "editMode": model.editMode, "coreEditMode": model.engine.timelineEditMode,
            "effectCount": model.effects.count,
            "clipTimeRemap": primary.map { model.engine.timeRemap($0) } ?? [],
            "textGlyphLayout": (text3D["separateGlyphs"] as? NSNumber)?.boolValue ?? false,
            "textSurfaceFinish": (text3D["surfaceFinish"] as? NSNumber)?.intValue ?? 0,
            "sheet": String(describing: model.sheetContent),
            "curveProperty": model.curveProperty, "curveParam": model.curveParam,
            "selectionCount": model.selection.count, "isManipulating": model.stageManipulating,
            "keySelectionCount": keySelectionCount,
            "keySelectMode": model.timelineKeySelectMode,
            "canUndo": model.status.canUndo != 0, "canRedo": model.status.canRedo != 0,
            "compositionWidth": model.compositionWidth, "compositionHeight": model.compositionHeight,
            "detail": detail, "publishedDetail": model.detail, "shapeParams": shape,
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
