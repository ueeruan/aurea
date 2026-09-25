// =============================================================================
//  Aurea / platform / ios / app / PreviewMetalView.swift
//
//  O preview. SwiftUI → UIViewRepresentable → `MetalPreviewView` (UIView cujo
//  layer é um CAMetalLayer) → a CAMetalLayer vai para `SurfaceDesc::nativeWindow`
//  no `attach_surface`.
//
//  A REGRA, QUE É O MOTIVO DESTE ARQUIVO EXISTIR: nenhum pixel do preview passa
//  pela UI. Não há `UIImage`, não há bitmap, não há captura de tela — o motor
//  desenha no drawable do layer e o sistema o compõe. É a mesma decisão do
//  Android (o preview vai para o ANativeWindow do SurfaceView).
//
//  OS GESTOS ficam aqui, por cima do layer, e viram COMANDOS do motor (mover,
//  escalar, girar). Nenhum deles recalcula a matemática da camada no Swift: o
//  motor é quem sabe a cadeia de pais, a âncora e a câmera.
// =============================================================================
import SwiftUI
import UIKit

struct PreviewMetalView: UIViewRepresentable {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    /// O quadro da composição, para converter px da tela em px da composição.
    let compositionSize: CGSize
    /// Ligado no palco em tela cheia: não trata gestos (o transporte manda).
    let interactive: Bool

    func makeUIView(context: Context) -> MetalPreviewView {
        let view = MetalPreviewView()
        view.device = model.device
        view.engine = model.engine
        view.isPaused = false
        view.isMultipleTouchEnabled = true
        // Vector/mask editing keeps its path gestures. Layer transforms use
        // the Android pointer arbiter, including its 18/4 point thresholds.
        do {
            let tap = UITapGestureRecognizer(target: context.coordinator, action: #selector(Coordinator.handleTap(_:)))
            view.addGestureRecognizer(tap)
            let pan = UIPanGestureRecognizer(target: context.coordinator,
                                            action: #selector(Coordinator.handlePan(_:)))
            pan.maximumNumberOfTouches = 1
            view.addGestureRecognizer(pan)
            let stage = StageTouchRecognizer(target: nil, action: nil)
            stage.onEvent = { [weak coordinator = context.coordinator, weak view] points, cancelled in
                guard let view else { return }; coordinator?.stageEvent(points, cancelled: cancelled, view: view)
            }
            view.addGestureRecognizer(stage)
            context.coordinator.tap = tap
            context.coordinator.pan = pan
            context.coordinator.stage = stage
        }
        context.coordinator.configureGestures()
#if DEBUG
        context.coordinator.parityProbe = ParityGestureProbe(preview: view, model: model)
#endif
        return view
    }

    func updateUIView(_ view: MetalPreviewView, context: Context) {
        // Reatribuir dispara o attach de novo quando o motor subiu DEPOIS de a
        // view existir (é o caso normal: a Home aparece antes do editor).
        view.device = model.device
        view.engine = model.engine
        context.coordinator.model = model
        context.coordinator.shell = shell
        context.coordinator.compositionSize = compositionSize
        context.coordinator.interactive = interactive
        context.coordinator.configureGestures()
    }

    func makeCoordinator() -> Coordinator {
        Coordinator(model: model, shell: shell, compositionSize: compositionSize, interactive: interactive)
    }

    static func dismantleUIView(_ view: MetalPreviewView, coordinator: Coordinator) {
        // A view desanexa a superfície no `deinit`/`didMoveToWindow`, mas o
        // caminho explícito aqui garante a ordem quando a tela é trocada.
        view.isPaused = true
        coordinator.finishStageEdit()
        view.engine = nil
    }

    // =========================================================================
    // Gestos → comandos
    // =========================================================================
    /// @MainActor: os métodos são alvos de gesto do UIKit, que entrega no
    /// main; e a sessão (`AureaModel`) é main-actor. Sem a anotação, chamar
    /// `model.mutate` daqui seria uma travessia de ator não declarada.
    @MainActor
    final class Coordinator: NSObject {
        var model: AureaModel
        var shell: ShellPresentation
        var compositionSize: CGSize
        var interactive: Bool
        weak var tap: UITapGestureRecognizer?
        weak var pan: UIPanGestureRecognizer?
        weak var stage: StageTouchRecognizer?
#if DEBUG
        var parityProbe: ParityGestureProbe?
#endif

        // Estado do gesto em curso. A posição/ângulo/escala de PARTIDA é lida do
        // motor no começo do arrasto (o `detail`), nunca acumulada no Swift —
        // acumular erraria a cada toque e divergiria do valor real.
        private var startPosition: SIMD3<Float> = .zero
        private var startScale: SIMD3<Float> = .one
        private var startRotation: SIMD3<Float> = .zero
        private var maskDragPoint: Int?
        private var maskDragHandle = 0
        private var maskDragChanged = false
        private var maskDragSmooth = false
        private var maskOppositeLength: Float = 0
        private var maskDragOffset = SIMD2<Float>.zero
        private var handle = -1
        private var pivot = CGPoint.zero
        private var initialDistance: CGFloat = 1
        private var lastAngle: CGFloat = 0
        private var sweptAngle: CGFloat = 0
        private var gizmoAxis = -1
        private var gizmoVector = CGPoint.zero
        private var gizmoCollapsed = false
        private var lastGizmoPoint = CGPoint.zero
        private enum StageMode { case pending, move, scale, rotate, pinch, idle, gizmo, shape, scene }
        // Cena 3D: 0 pendente, 1 órbita, 2 objeto, 3 pinça.
        private var sceneMode = 0
        private var scenePicked: Int64?
        private var sceneLast = CGPoint.zero
        private var sceneSpan: CGFloat = 1
        private var sceneDistance0: Float = 3
        private var sceneTapAt: TimeInterval = 0
        private var sceneTapPoint = CGPoint.zero
        private var stageMode: StageMode = .idle
        private var stageDown = CGPoint.zero
        private var stageFinger: ObjectIdentifier?
        private var markerAnchorLayer: Int64?
        private var markerHold: DispatchWorkItem?
        private var pinchFingers: [ObjectIdentifier] = []
        private var targetLayer: Int64?
        private var hadMultipleTouches = false
        private var editBegan = false
        private var moveWorld = SIMD2<Float>.zero
        private var moveDown = SIMD2<Float>.zero
        private var moveLast = SIMD2<Float>.zero
        private var moveAffine: [Float] = []
        private var axisLock = 0
        private var moveOffsetsX: [Float] = []
        private var moveOffsetsY: [Float] = []
        private var snapTargetsX: [Float] = []
        private var snapTargetsY: [Float] = []
        private var pinchSpan: CGFloat = 1
        private var pinchRotationActive = false
        private var pinchRotationOffset: Float = 0
        private var shapeHandle = -1
        private var shapeWidth: Float = 1
        private var shapeHeight: Float = 1
        private var shapeRadius: Float = 0
        private var shapeU = SIMD2<Float>.zero
        private var shapeV = SIMD2<Float>.zero
        private var shapeScale = SIMD2<Float>(repeating: 1)
        private var shapeLinked = true
        private let snapFeedback = UISelectionFeedbackGenerator()

        init(model: AureaModel, shell: ShellPresentation, compositionSize: CGSize, interactive: Bool) {
            self.model = model
            self.shell = shell
            self.compositionSize = compositionSize
            self.interactive = interactive
        }

        func configureGestures() {
            let path = (model.panel == .vector && (model.vectorFreehand || model.vectorEditingPoints)) || (model.panel == .mask && model.editingMask != nil)
            let legacy = path || model.pointPick != nil
            if tap?.isEnabled != (interactive && legacy) { tap?.isEnabled = interactive && legacy }
            if pan?.isEnabled != (interactive && path) { pan?.isEnabled = interactive && path }
            if stage?.isEnabled != (interactive && !legacy) { stage?.isEnabled = interactive && !legacy }
        }

        /// px da tela → px da composição (a razão entre o quadro da composição e
        /// o tamanho do palco). O motor trabalha em px da composição.
        private func scaleFactor(_ view: UIView) -> Float {
            let width = max(1, view.bounds.width)
            let height = max(1, view.bounds.height)
            let compWidth = max(1, compositionSize.width)
            let compHeight = max(1, compositionSize.height)
            return Float(max(compWidth / width, compHeight / height))
        }

        @objc func handleTap(_ gesture: UITapGestureRecognizer) {
            guard interactive, let view = gesture.view else { return }
            let factor = CGFloat(scaleFactor(view))
            let p = gesture.location(in: view)
            let point = CGPoint(x: (p.x - view.bounds.width / 2) * factor + compositionSize.width / 2,
                                y: (p.y - view.bounds.height / 2) * factor + compositionSize.height / 2)
            if model.pointPick != nil { model.finishPointPick(point); return }
            if model.panel == .vector && model.vectorFreehand { return }
            if model.panel == .vector && model.vectorEditingPoints, let mask = model.editingMask, let local = maskLocal(point) {
                let hit = nearestMaskPoint(local, tolerance: Float(factor) * 20)
                if let hit {
                    if model.vectorPointTool == 2 { deleteVectorPoint(hit, mask: mask) }
                    else if model.vectorPointTool == 3 { toggleVectorCorner(hit, mask: mask) }
                    else {
                        if model.vectorPointTool == 1 && hit == 0 && !mask.closed && mask.points.count >= 18 && model.selectedMaskPoint == mask.points.count / 6 - 1 {
                            model.setMaskPoints(mask.points, closed: true)
                        }
                        model.selectedMaskPoint = hit
                    }
                } else if model.vectorPointTool == 1 {
                    if !insertVectorPoint(local, mask: mask, tolerance: Float(factor) * 20) && !mask.closed {
                        model.setMaskPoints(mask.points + [local.x, local.y, 0, 0, 0, 0], closed: false)
                        model.selectedMaskPoint = mask.points.count / 6
                    }
                } else { model.selectedMaskPoint = nil }
                return
            }
            if model.panel == .mask, let mask = model.editingMask, let local = maskLocal(point) {
                if model.maskDrawing {
                    if mask.points.count >= 18, hypot(local.x - mask.points[0], local.y - mask.points[1]) < Float(factor) * 15 {
                        model.setMaskPoints(mask.points, closed: true); model.maskDrawing = false
                    } else {
                        model.setMaskPoints(mask.points + [local.x, local.y, 0, 0, 0, 0], closed: false)
                        model.selectedMaskPoint = mask.points.count / 6
                    }
                } else { model.selectedMaskPoint = nearestMaskPoint(local, tolerance: Float(factor) * 15) }
                return
            }
            for layer in model.layers where layer.visible && !layer.locked && layer.kind != 3 &&
                Int64(layer.startFrame) <= model.status.playhead && Int64(layer.endFrame) > model.status.playhead {
                guard let d = model.engine.layerDetail(layer.id),
                      (d["opacity"] as? NSNumber)?.floatValue ?? 0 > 0.01 else { continue }
                var corners: [Float] = []
                guard StageGeom.corners(d, &corners) else { continue }
                let path = UIBezierPath()
                path.move(to: CGPoint(x: CGFloat(corners[0]), y: CGFloat(corners[1])))
                for index in 1..<4 { path.addLine(to: CGPoint(x: CGFloat(corners[index * 2]), y: CGFloat(corners[index * 2 + 1]))) }
                path.close()
                if path.contains(point) { model.select(layerId: layer.id, additive: false); return }
            }
            model.clearSelection()
        }

        private func maskLocal(_ p: CGPoint) -> SIMD2<Float>? {
            let a = model.maskAffine
            guard a.count == 6 else { return nil }
            let det = a[0] * a[3] - a[1] * a[2]
            guard abs(det) > 0.000001 else { return nil }
            let x = Float(p.x) - a[4], y = Float(p.y) - a[5]
            return SIMD2((a[3] * x - a[2] * y) / det, (-a[1] * x + a[0] * y) / det)
        }
        private func nearestMaskPoint(_ p: SIMD2<Float>, tolerance: Float) -> Int? {
            guard let mask = model.editingMask else { return nil }
            func distance(_ i: Int) -> Float { maskDistance(p - SIMD2(mask.points[i * 6], mask.points[i * 6 + 1])) }
            return (0..<(mask.points.count / 6)).filter { distance($0) < tolerance }.min { distance($0) < distance($1) }
        }
        private func maskDistance(_ delta: SIMD2<Float>) -> Float {
            let a = model.maskAffine
            guard a.count == 6 else { return hypot(delta.x, delta.y) }
            return hypot(a[0] * delta.x + a[2] * delta.y, a[1] * delta.x + a[3] * delta.y)
        }
        // Direct port of VectorStage.VectorPathOps; all commits go to the C++ path.
        private func deleteVectorPoint(_ i: Int, mask: MaskItem) {
            var points = mask.points
            points.removeSubrange((i * 6)..<(i * 6 + 6))
            model.setMaskPoints(points, closed: points.count >= 18 && mask.closed)
            model.selectedMaskPoint = points.isEmpty ? nil : min(i, points.count / 6 - 1)
        }
        private func toggleVectorCorner(_ i: Int, mask: MaskItem) {
            var p = mask.points
            let at = i * 6, n = p.count / 6
            if hypot(p[at + 2], p[at + 3]) > 0.001 || hypot(p[at + 4], p[at + 5]) > 0.001 {
                for offset in 2...5 { p[at + offset] = 0 }
            } else {
                let prev = (i > 0 ? i - 1 : (mask.closed ? n - 1 : i)) * 6
                let next = (i < n - 1 ? i + 1 : (mask.closed ? 0 : i)) * 6
                let d = SIMD2(p[next] - p[prev], p[next + 1] - p[prev + 1])
                let length = hypot(d.x, d.y)
                guard length >= 0.001 else { return }
                let lin = hypot(p[at] - p[prev], p[at + 1] - p[prev + 1]) / 3
                let lout = hypot(p[next] - p[at], p[next + 1] - p[at + 1]) / 3
                p[at + 2] = -d.x / length * lin; p[at + 3] = -d.y / length * lin
                p[at + 4] = d.x / length * lout; p[at + 5] = d.y / length * lout
            }
            model.setMaskPoints(p, closed: mask.closed); model.selectedMaskPoint = i
        }
        @discardableResult
        private func insertVectorPoint(_ pos: SIMD2<Float>, mask: MaskItem, tolerance: Float) -> Bool {
            let n = mask.points.count / 6
            guard n >= 2 else { return false }
            var p = mask.points, best = tolerance, segment: Int?, bestT: Float = 0
            func vertex(_ index: Int, _ offset: Int = 0) -> SIMD2<Float> { SIMD2(p[index * 6 + offset], p[index * 6 + offset + 1]) }
            for i in 0..<(mask.closed ? n : n - 1) {
                let j = (i + 1) % n, a = vertex(i), b = vertex(j)
                let c1 = a + vertex(i, 4), c2 = b + vertex(j, 2)
                for k in 1..<48 {
                    let t = Float(k) / 48, u = 1 - t
                    let q = a * (u * u * u) + c1 * (3 * u * u * t) + c2 * (3 * u * t * t) + b * (t * t * t)
                    let d = maskDistance(q - pos)
                    if d < best { best = d; segment = i; bestT = t }
                }
            }
            guard let i = segment else { return false }
            let j = (i + 1) % n, a = vertex(i), b = vertex(j)
            let p1 = a + vertex(i, 4), p2 = b + vertex(j, 2)
            func lerp(_ a: SIMD2<Float>, _ b: SIMD2<Float>) -> SIMD2<Float> { a + (b - a) * bestT }
            let aa = lerp(a, p1), bb = lerp(p1, p2), cc = lerp(p2, b)
            let dd = lerp(aa, bb), ee = lerp(bb, cc), f = lerp(dd, ee)
            p[i * 6 + 4] = aa.x - a.x; p[i * 6 + 5] = aa.y - a.y
            p[j * 6 + 2] = cc.x - b.x; p[j * 6 + 3] = cc.y - b.y
            let index = j == 0 ? n : i + 1
            p.insert(contentsOf: [f.x, f.y, dd.x - f.x, dd.y - f.y, ee.x - f.x, ee.y - f.y], at: index * 6)
            model.setMaskPoints(p, closed: mask.closed); model.selectedMaskPoint = index
            return true
        }
        private func handleMaskPan(_ gesture: UIPanGestureRecognizer, view: UIView) {
            let factor = scaleFactor(view), p = gesture.location(in: view)
            let comp = CGPoint(x: (p.x - view.bounds.width / 2) * CGFloat(factor) + compositionSize.width / 2,
                               y: (p.y - view.bounds.height / 2) * CGFloat(factor) + compositionSize.height / 2)
            guard let local = maskLocal(comp), var mask = model.editingMask else { return }
            switch gesture.state {
            case .began:
                maskDragChanged = false; maskDragHandle = 0; maskDragSmooth = false
                maskDragPoint = nearestMaskPoint(local, tolerance: factor * 20)
                if let n = model.selectedMaskPoint, n * 6 + 5 < mask.points.count {
                    for handle in [2, 4] {
                        let x = mask.points[n * 6] + mask.points[n * 6 + handle]
                        let y = mask.points[n * 6 + 1] + mask.points[n * 6 + handle + 1]
                        if maskDistance(local - SIMD2(x, y)) < factor * 20 && hypot(mask.points[n * 6 + handle], mask.points[n * 6 + handle + 1]) > 0.1 {
                            maskDragPoint = n; maskDragHandle = handle
                            let ix = mask.points[n * 6 + 2], iy = mask.points[n * 6 + 3], ox = mask.points[n * 6 + 4], oy = mask.points[n * 6 + 5]
                            let li = hypot(ix, iy), lo = hypot(ox, oy)
                            maskDragSmooth = li > 0.001 && lo > 0.001 && ix * ox + iy * oy < 0 && abs(ix * oy - iy * ox) / (li * lo) < 0.02
                            maskOppositeLength = handle == 2 ? lo : li
                        }
                    }
                }
                if model.panel == .vector && (model.vectorPointTool == 2 || model.vectorPointTool == 3) && maskDragHandle == 0 { maskDragPoint = nil; return }
                if maskDragPoint == nil && model.maskDrawing && !mask.closed {
                    maskDragPoint = mask.points.count / 6; maskDragHandle = 4
                    model.setMaskPoints(mask.points + [local.x, local.y, 0, 0, 0, 0], closed: false)
                    maskDragChanged = true; maskDragSmooth = true; maskOppositeLength = -1
                }
                if let n = maskDragPoint, n * 6 + 1 < mask.points.count { maskDragOffset = SIMD2(mask.points[n * 6], mask.points[n * 6 + 1]) - local }
                model.selectedMaskPoint = maskDragPoint
            case .changed:
                guard let n = maskDragPoint, n * 6 + 5 < mask.points.count else { return }
                if maskDragHandle == 0 { mask.points[n * 6] = local.x + maskDragOffset.x; mask.points[n * 6 + 1] = local.y + maskDragOffset.y }
                else {
                    let dx = local.x - mask.points[n * 6], dy = local.y - mask.points[n * 6 + 1]
                    mask.points[n * 6 + maskDragHandle] = dx; mask.points[n * 6 + maskDragHandle + 1] = dy
                    let other = maskDragHandle == 2 ? 4 : 2
                    if model.panel != .vector || maskDragSmooth {
                        let length = hypot(dx, dy)
                        let gain = maskOppositeLength < 0 || model.panel != .vector ? Float(1) : maskOppositeLength / max(0.0001, length)
                        mask.points[n * 6 + other] = -dx * gain; mask.points[n * 6 + other + 1] = -dy * gain
                    }
                }
                model.setMaskPoints(mask.points, closed: mask.closed, undo: !maskDragChanged); maskDragChanged = true
            case .ended, .cancelled, .failed:
                maskDragPoint = nil; maskDragChanged = false; model.refreshModel(force: true)
            default: break
            }
        }

        private func compositionPoint(_ point: CGPoint, view: UIView) -> SIMD2<Float> {
            let f = scaleFactor(view)
            return SIMD2(Float(point.x - view.bounds.width / 2) * f + Float(compositionSize.width / 2),
                         Float(point.y - view.bounds.height / 2) * f + Float(compositionSize.height / 2))
        }
        private func screenPoint(_ x: Float, _ y: Float, view: UIView) -> CGPoint {
            let f = CGFloat(scaleFactor(view))
            return CGPoint(x: (CGFloat(x) - compositionSize.width / 2) / f + view.bounds.width / 2,
                           y: (CGFloat(y) - compositionSize.height / 2) / f + view.bounds.height / 2)
        }
        private func vector(_ value: Any?) -> SIMD3<Float> {
            let v = StageGeom.floats(value)
            return SIMD3(v.count > 0 ? v[0] : 0, v.count > 1 ? v[1] : 0, v.count > 2 ? v[2] : 0)
        }
        private func active(_ row: LayerItem) -> Bool { Int64(row.startFrame) <= model.status.playhead && model.status.playhead < Int64(row.endFrame) }
        private func hitLayer(_ point: SIMD2<Float>, slack: Float, includeLocked: Bool) -> Int64? {
            for row in model.layers where row.visible && row.kind != 3 && active(row) && (includeLocked || !row.locked) {
                guard let detail = model.engine.layerDetail(row.id), (detail["opacity"] as? NSNumber)?.floatValue ?? 0 > 0.01 else { continue }
                if StageGeom.contains(detail, point.x, point.y, slack: slack) { return row.id }
            }
            return nil
        }
        private func screenCorners(view: UIView) -> [CGPoint] {
            var c: [Float] = []; guard StageGeom.corners(model.detail, &c) else { return [] }
            return stride(from: 0, to: 8, by: 2).map { screenPoint(c[$0], c[$0 + 1], view: view) }
        }
        private func editableSelection() -> Bool {
            guard model.selection.count == 1, let row = model.selectedLayer else { return false }
            return !row.locked && active(row)
        }
        private func beginEdit(_ label: String) {
            if !editBegan { model.beginGesture(label); editBegan = true }
        }
        private func engage() {
            if model.status.playing != 0 { model.playPause() }
            model.stageManipulating = true
        }
        func finishStageEdit() {
            markerHold?.cancel(); markerHold = nil
            markerAnchorLayer = nil
            if editBegan { model.endGesture() }
            editBegan = false; model.stageManipulating = false
            shell.snapX = nil; shell.snapY = nil; shell.grabbedHandle = -1; shell.grabbedShapeHandle = -1
        }
        private func keepTransform() {
            startPosition = vector(model.detail["position"])
            startScale = vector(model.detail["scale"])
            startRotation = vector(model.detail["rotation"])
        }
        private func worldPosition(_ position: SIMD3<Float>, affine: [Float]) -> SIMD2<Float> {
            guard affine.count == 6 else { return SIMD2(position.x, position.y) }
            return SIMD2(affine[0] * position.x + affine[2] * position.y + affine[4], affine[1] * position.x + affine[3] * position.y + affine[5])
        }
        private func localPosition(_ point: SIMD2<Float>, affine: [Float]) -> SIMD2<Float> {
            guard affine.count == 6 else { return point }
            let det = affine[0] * affine[3] - affine[1] * affine[2]
            guard abs(det) > 0.000001 else { return point }
            let x = point.x - affine[4], y = point.y - affine[5]
            return SIMD2((affine[3] * x - affine[2] * y) / det, (-affine[1] * x + affine[0] * y) / det)
        }

        // Stage.kt's pointer arbiter: handles > selected body > top layer > empty.
        func stageEvent(_ points: [StageTouchPoint], cancelled: Bool, view: UIView) {
            if cancelled || !interactive {
                finishStageEdit(); stageFinger = nil; stageMode = .idle; return
            }
            let pressed = points.filter(\.pressed)
            if stageFinger == nil {
                guard let first = pressed.first else { return }
                stageFinger = first.id; stageDown = first.position; hadMultipleTouches = false
                stageMode = .pending; handle = -1; shapeHandle = -1; gizmoAxis = -1; targetLayer = nil
                axisLock = 0; sweptAngle = 0; pinchRotationActive = false
                model.refreshSelectedLayer()
                if !model.sceneEditor && startShape(at: first.position, view: view) { stageMode = .shape }
                else if startGizmo(at: first.position, view: view) { stageMode = .gizmo }
                else if model.sceneEditor {
                    stageMode = .scene; sceneMode = 0; sceneLast = first.position
                    scenePicked = model.scenePick(compositionPoint(first.position, view: view), radius: 36 * scaleFactor(view))
                }
                else { recordTarget(first.position, view: view) }
                if stageMode == .pending && handle < 0, let anchor = model.previewMarkerAnchor {
                    let p = screenPoint(anchor.x, anchor.y, view: view)
                    // Outra camada visível por cima do ponto tocado ganha: tocar
                    // num texto sobre o vídeo selecionado seleciona o texto, não
                    // marca o vídeo (era marca "do nada").
                    let over = hitLayer(compositionPoint(first.position, view: view), slack: 0, includeLocked: false)
                    if hypot(first.position.x - p.x, first.position.y - p.y) <= 24,
                       over == nil || over == model.primarySelection {
                        markerAnchorLayer = model.primarySelection
                        let hold = DispatchWorkItem { [weak self] in
                            guard let self, self.stageMode == .pending, !self.hadMultipleTouches,
                                  let layer = self.markerAnchorLayer, layer == self.model.primarySelection,
                                  self.model.previewMarkerAnchor != nil else { return }
                            self.model.editMarkerAtPlayhead()
                            self.finishStageEdit(); self.stageMode = .idle
                        }
                        markerHold = hold
                        DispatchQueue.main.asyncAfter(deadline: .now() + 0.45, execute: hold)
                    }
                }
                if pressed.count < 2 { return }
            }
            if stageMode == .scene {
                sceneEvent(pressed, view: view)
                if pressed.isEmpty { finishStageEdit(); stageFinger = nil; stageMode = .idle }
                return
            }
            if pressed.isEmpty {
                if stageMode == .pending && !hadMultipleTouches && handle < 0 {
                    if let layer = markerAnchorLayer, layer == model.primarySelection, model.previewMarkerAnchor != nil {
                        model.toggleMarkerAt(model.status.playhead)
                    } else {
                        let point = compositionPoint(stageDown, view: view)
                        let hit = hitLayer(point, slack: 0, includeLocked: false) ?? hitLayer(point, slack: scaleFactor(view) * 12, includeLocked: false)
                        if let hit {
                            if model.primarySelection != hit || model.selection.count != 1 { model.select(layerId: hit, additive: false) }
                        } else { model.clearSelection() }
                    }
                }
                finishStageEdit(); stageFinger = nil; stageMode = .idle; return
            }
            if stageMode == .shape || stageMode == .gizmo {
                guard let first = pressed.first(where: { $0.id == stageFinger }) else { finishStageEdit(); stageMode = .idle; return }
                if stageMode == .shape { stepShape(first.position, view: view) }
                else { stepGizmo(first.position, view: view) }
                return
            }
            if pressed.count >= 2 && stageMode != .pinch && stageMode != .idle {
                hadMultipleTouches = true; finishStageEdit()
                let a = pressed[0], b = pressed[pressed.count - 1]
                let middle = CGPoint(x: (a.position.x + b.position.x) / 2, y: (a.position.y + b.position.y) / 2)
                let slack = scaleFactor(view) * 12
                let over = [a.position, b.position, middle].contains { point in
                    let c = compositionPoint(point, view: view)
                    return StageGeom.contains(model.detail, c.x, c.y, slack: slack)
                }
                if editableSelection() && over {
                    keepTransform(); pinchFingers = [a.id, b.id]
                    pinchSpan = max(1, hypot(a.position.x - b.position.x, a.position.y - b.position.y))
                    lastAngle = atan2(b.position.y - a.position.y, b.position.x - a.position.x)
                    sweptAngle = 0; pinchRotationActive = false; pinchRotationOffset = 0
                    engage(); stageMode = .pinch
                } else { stageMode = .idle }
            }
            switch stageMode {
            case .pending:
                guard let first = pressed.first(where: { $0.id == stageFinger }) else { return }
                if hypot(first.position.x - stageDown.x, first.position.y - stageDown.y) > 8 {
                    markerHold?.cancel(); markerHold = nil; markerAnchorLayer = nil
                }
                if hypot(first.position.x - stageDown.x, first.position.y - stageDown.y) > (handle >= 0 ? 4 : 18) {
                    startDrag(view: view)
                    stepEdit(first.position, view: view)
                }
            case .move, .scale, .rotate:
                if let first = pressed.first(where: { $0.id == stageFinger }) { stepEdit(first.position, view: view) }
                else { stageMode = .idle }
            case .pinch:
                guard pinchFingers.count == 2,
                      let a = pressed.first(where: { $0.id == pinchFingers[0] }), let b = pressed.first(where: { $0.id == pinchFingers[1] }) else {
                    stageMode = .idle; return // The remaining finger stays idle until all lift.
                }
                stepPinch(a.position, b.position)
            default: break
            }
        }
        private func recordTarget(_ point: CGPoint, view: UIView) {
            if editableSelection() && !ShapeStageGeometry.enabled(model) {
                let handles = ShellStageGeometry.handles(screenCorners(view: view), size: view.bounds.size)
                var closest = CGFloat.greatestFiniteMagnitude
                for i in handles.indices {
                    let distance = hypot(point.x - handles[i].x, point.y - handles[i].y)
                    if distance <= (i == 0 ? 22 : 26) && distance < closest { handle = i; closest = distance }
                }
                if handle >= 0 { return }
            }
            let c = compositionPoint(point, view: view)
            if model.selection.count == 1, let row = model.selectedLayer, active(row), StageGeom.contains(model.detail, c.x, c.y, slack: 0) { targetLayer = row.id }
            else { targetLayer = hitLayer(c, slack: 0, includeLocked: true) }
        }
        private func startDrag(view: UIView) {
            if handle >= 0 {
                guard editableSelection() else { stageMode = .idle; return }
                keepTransform()
                let world = worldPosition(startPosition, affine: StageGeom.floats(model.detail["parentAffine"]))
                pivot = screenPoint(world.x, world.y, view: view)
                initialDistance = max(8, hypot(stageDown.x - pivot.x, stageDown.y - pivot.y))
                lastAngle = atan2(stageDown.y - pivot.y, stageDown.x - pivot.x); sweptAngle = 0
                shell.grabbedHandle = handle; engage(); stageMode = handle == 0 ? .rotate : .scale
            } else if let id = targetLayer {
                if model.primarySelection != id || model.selection.count != 1 { model.select(layerId: id, additive: false) }
                guard let row = model.selectedLayer, row.id == id else { stageMode = .idle; return }
                guard !row.locked else { model.toast = AureaText.t("sh_layer_locked_unlock_to_move"); stageMode = .idle; return }
                keepTransform(); moveAffine = StageGeom.floats(model.detail["parentAffine"])
                moveWorld = worldPosition(startPosition, affine: moveAffine)
                moveDown = compositionPoint(stageDown, view: view); moveLast = moveDown; axisLock = 0
                if let b = StageGeom.bounds(model.detail) {
                    moveOffsetsX = [b.0 - moveWorld.x, (b.0 + b.2) / 2 - moveWorld.x, b.2 - moveWorld.x]
                    moveOffsetsY = [b.1 - moveWorld.y, (b.1 + b.3) / 2 - moveWorld.y, b.3 - moveWorld.y]
                } else { moveOffsetsX = [0, 0, 0]; moveOffsetsY = [0, 0, 0] }
                collectSnapTargets(id); engage(); stageMode = .move
            } else { stageMode = .idle }
        }
        private func clampScale(_ value: Float) -> Float {
            let ax = max(abs(startScale.x), 0.0001), ay = max(abs(startScale.y), 0.0001)
            let low = max(0.001 / ax, 0.001 / ay), high = min(100 / ax, 100 / ay)
            return low <= high ? value.clamped(to: low...high) : value
        }
        private func sweep(_ angle: CGFloat) {
            var delta = angle - lastAngle
            while delta > .pi { delta -= 2 * .pi }; while delta < -.pi { delta += 2 * .pi }
            sweptAngle += delta; lastAngle = angle
        }
        private func stepEdit(_ point: CGPoint, view: UIView) {
            guard let id = model.primarySelection else { return }
            switch stageMode {
            case .move: stepMove(point, view: view, id: id)
            case .scale:
                let f = clampScale(Float(hypot(point.x - pivot.x, point.y - pivot.y) / initialDistance))
                beginEdit("escala"); model.setTransform2(3, startScale.x * f, 4, startScale.y * f, layer: id)
            case .rotate:
                sweep(atan2(point.y - pivot.y, point.x - pivot.x))
                beginEdit("girar"); model.setTransform(8, value: startRotation.z + Float(sweptAngle * 180 / .pi), layer: id)
            default: break
            }
        }
        private func stepPinch(_ a: CGPoint, _ b: CGPoint) {
            guard let id = model.primarySelection else { return }
            let f = clampScale(Float(hypot(a.x - b.x, a.y - b.y) / pinchSpan))
            sweep(atan2(b.y - a.y, b.x - a.x))
            let degrees = Float(sweptAngle * 180 / .pi)
            if !pinchRotationActive && abs(degrees) > 4 { pinchRotationActive = true; pinchRotationOffset = degrees < 0 ? -4 : 4 }
            beginEdit("pinça"); model.setTransform2(3, startScale.x * f, 4, startScale.y * f, layer: id)
            if pinchRotationActive { model.setTransform(8, value: startRotation.z + degrees - pinchRotationOffset, layer: id) }
        }
        private func collectSnapTargets(_ own: Int64) {
            snapTargetsX = [0, Float(compositionSize.width / 2), Float(compositionSize.width)]
            snapTargetsY = [0, Float(compositionSize.height / 2), Float(compositionSize.height)]
            for row in model.layers where row.id != own && row.visible && active(row) {
                guard let detail = model.engine.layerDetail(row.id), let b = StageGeom.bounds(detail) else { continue }
                snapTargetsX += [b.0, (b.0 + b.2) / 2, b.2]; snapTargetsY += [b.1, (b.1 + b.3) / 2, b.3]
            }
        }
        private func nearestTarget(_ value: Float, offsets: [Float], targets: [Float], tolerance: Float) -> (target: Float, delta: Float)? {
            var target: Float?, closest = tolerance
            for offset in offsets { for candidate in targets {
                let distance = abs(candidate - value - offset)
                if distance <= closest { closest = distance; target = candidate }
            }}
            guard let target else { return nil }
            var delta: Float = 0; closest = .greatestFiniteMagnitude
            for offset in offsets {
                let d = target - (value + offset)
                if abs(d) <= tolerance && abs(d) < closest { closest = abs(d); delta = d }
            }
            return (target, delta)
        }
        private func stepMove(_ point: CGPoint, view: UIView, id: Int64) {
            let c = compositionPoint(point, view: view)
            var next = moveWorld + c - moveDown
            let dx = abs(point.x - stageDown.x), dy = abs(point.y - stageDown.y)
            if axisLock == 0 {
                if dx > 24 && dy < 12 { axisLock = 1 }
                else if dy > 24 && dx < 12 { axisLock = 2 }
            }
            if axisLock == 1 { next.y = moveWorld.y }; if axisLock == 2 { next.x = moveWorld.x }
            let tolerance = scaleFactor(view) * 10
            var snapX: Float?, snapY: Float?
            if axisLock != 2 && abs(c.x - moveLast.x) <= tolerance, let snap = nearestTarget(next.x, offsets: moveOffsetsX, targets: snapTargetsX, tolerance: tolerance) { next.x += snap.delta; snapX = snap.target }
            if axisLock != 1 && abs(c.y - moveLast.y) <= tolerance, let snap = nearestTarget(next.y, offsets: moveOffsetsY, targets: snapTargetsY, tolerance: tolerance) { next.y += snap.delta; snapY = snap.target }
            moveLast = c
            if (snapX != nil && snapX != shell.snapX) || (snapY != nil && snapY != shell.snapY) { snapFeedback.selectionChanged() }
            shell.snapX = snapX; shell.snapY = snapY
            let local = localPosition(next, affine: moveAffine)
            beginEdit("mover"); model.setTransform2(0, local.x, 1, local.y, layer: id)
        }
        /// Cena 3D "seca": tudo com o dedo na tela (par do `sceneGesture` do
        /// Android). Objeto sob o dedo: escolhe e arrasta no plano mais de
        /// frente. Vazio: 1 dedo gira a vista; toque solta a seleção; toque
        /// duplo recentra. 2 dedos: pinça aproxima/afasta. Um arrasto de
        /// objeto = UM passo de desfazer; a órbita é só da prévia.
        private func sceneEvent(_ pressed: [StageTouchPoint], view: UIView) {
            if pressed.isEmpty {
                guard sceneMode == 0 else { return }
                if let picked = scenePicked {
                    if model.primarySelection != picked || model.selection.count != 1 {
                        model.select(layerId: picked, additive: false); snapFeedback.selectionChanged()
                    }
                    return
                }
                let now = ProcessInfo.processInfo.systemUptime
                let again = now - sceneTapAt < 0.32 && hypot(stageDown.x - sceneTapPoint.x, stageDown.y - sceneTapPoint.y) < 48
                sceneTapAt = again ? 0 : now; sceneTapPoint = stageDown
                if again { model.resetSceneView() } else { model.clearSelection() }
                return
            }
            if pressed.count >= 2 {
                let a = pressed[0].position, b = pressed[1].position
                let span = max(1, hypot(a.x - b.x, a.y - b.y))
                if sceneMode != 3 {
                    finishStageEdit(); sceneMode = 3; sceneSpan = span; sceneDistance0 = model.sceneDistance
                } else {
                    model.setSceneView(yaw: model.sceneYaw, pitch: model.scenePitch, distance: sceneDistance0 * Float(sceneSpan / span))
                }
                return
            }
            if sceneMode == 3 { return }   // sobrou um dedo da pinça: nada
            guard let first = pressed.first(where: { $0.id == stageFinger }) else { return }
            if sceneMode == 0 && hypot(first.position.x - stageDown.x, first.position.y - stageDown.y) > 12 {
                if let picked = scenePicked {
                    if model.primarySelection != picked || model.selection.count != 1 { model.select(layerId: picked, additive: false) }
                    beginEdit("mover na cena"); sceneMode = 2
                } else {
                    sceneMode = 1
                }
                sceneLast = stageDown   // a folga inteira entra: nada "pula" depois
            }
            let dx = first.position.x - sceneLast.x, dy = first.position.y - sceneLast.y
            switch sceneMode {
            case 1: model.setSceneView(yaw: model.sceneYaw - Float(dx) * 0.35, pitch: model.scenePitch + Float(dy) * 0.35, distance: model.sceneDistance)
            case 2: let f = scaleFactor(view); model.sceneDragObject(dx: Float(dx) * f, dy: Float(dy) * f)
            default: break
            }
            if sceneMode != 0 { sceneLast = first.position }
        }
        private func startGizmo(at point: CGPoint, view: UIView) -> Bool {
            guard editableSelection(), let id = model.primarySelection, !ShapeStageGeometry.enabled(model) else { return false }
            let data = model.engine.gizmo(id, length: ShellStageGeometry.gizmoLength).map(\.floatValue)
            guard data.count == 8 else { return false }
            let raw = stride(from: 0, to: 8, by: 2).map { screenPoint(data[$0], data[$0 + 1], view: view) }
            let tips = ShellStageGeometry.gizmoTips(raw)
            var closest: CGFloat = 24
            for i in 1...3 {
                let distance = hypot(point.x - tips[i].x, point.y - tips[i].y)
                if distance < closest { closest = distance; gizmoAxis = i - 1 }
            }
            guard gizmoAxis >= 0 else { return false }
            gizmoVector = CGPoint(x: tips[gizmoAxis + 1].x - tips[0].x, y: tips[gizmoAxis + 1].y - tips[0].y)
            gizmoCollapsed = gizmoAxis == 2 && hypot(raw[3].x - raw[0].x, raw[3].y - raw[0].y) < 44 * 0.6
            lastGizmoPoint = point; beginEdit("mover no eixo"); return true
        }
        private func stepGizmo(_ point: CGPoint, view: UIView) {
            guard let id = model.primarySelection else { return }
            let dx = point.x - lastGizmoPoint.x, dy = point.y - lastGizmoPoint.y
            lastGizmoPoint = point
            let length2 = gizmoVector.x * gizmoVector.x + gizmoVector.y * gizmoVector.y
            let amount = gizmoCollapsed ? -Float(dy) * scaleFactor(view) * 2 : length2 > 1 ? Float((dx * gizmoVector.x + dy * gizmoVector.y) / length2) * ShellStageGeometry.gizmoLength : 0
            guard amount != 0 else { return }
            let next = model.engine.gizmoMoveLocal(id, axis: UInt32(gizmoAxis), amount: amount).map(\.floatValue)
            model.applyGizmoPosition(id, next)
        }
        private func startShape(at point: CGPoint, view: UIView) -> Bool {
            guard ShapeStageGeometry.enabled(model) else { return false }
            let points = ShapeStageGeometry.handles(model.detail, corners: screenCorners(view: view))
            var closest: CGFloat = 26
            for i in points.indices.reversed() {
                let distance = hypot(point.x - points[i].x, point.y - points[i].y)
                if distance < closest - 0.5 { closest = distance; shapeHandle = i }
            }
            guard shapeHandle >= 0 else { return false }
            var corners: [Float] = []; guard StageGeom.corners(model.detail, &corners) else { return false }
            shapeWidth = max(1, StageGeom.width(model.detail)); shapeHeight = max(1, StageGeom.height(model.detail))
            let shape = StageGeom.floats(model.detail["shape"]); shapeRadius = shape.count > 4 ? shape[4] : 0
            let u = SIMD2(corners[2] - corners[0], corners[3] - corners[1]), v = SIMD2(corners[6] - corners[0], corners[7] - corners[1])
            let lengthU = hypot(u.x, u.y), lengthV = hypot(v.x, v.y)
            guard lengthU >= 0.0001 && lengthV >= 0.0001 else { return false }
            shapeU = u / lengthU; shapeV = v / lengthV; shapeScale = SIMD2(lengthU / shapeWidth, lengthV / shapeHeight)
            shapeLinked = model.shapeSizeLinked; shell.grabbedShapeHandle = shapeHandle
            beginEdit(shapeHandle == 8 ? "raio da forma" : "tamanho da forma"); return true
        }
        private func stepShape(_ point: CGPoint, view: UIView) {
            guard let id = model.primarySelection else { return }
            let f = scaleFactor(view), dx = Float(point.x - stageDown.x) * f, dy = Float(point.y - stageDown.y) * f
            let du = (dx * shapeU.x + dy * shapeU.y) / shapeScale.x, dv = (dx * shapeV.x + dy * shapeV.y) / shapeScale.y
            if shapeHandle == 8 {
                _ = model.engine.editShape(id, param: 1, value: (shapeRadius + (du + dv) / 2).clamped(to: 0...(min(shapeWidth, shapeHeight) / 2)), continuing: false)
            } else {
                let sx: Float = [-1, 1, 1, -1, 0, 1, 0, -1][shapeHandle], sy: Float = [-1, -1, 1, 1, -1, 0, 1, 0][shapeHandle]
                var w = sx != 0 ? shapeWidth + 2 * sx * du : shapeWidth
                var h = sy != 0 ? shapeHeight + 2 * sy * dv : shapeHeight
                if shapeLinked {
                    let kx = w / shapeWidth, ky = h / shapeHeight
                    let k = sx == 0 ? ky : sy == 0 ? kx : abs(kx - 1) >= abs(ky - 1) ? kx : ky
                    w = shapeWidth * k; h = shapeHeight * k
                }
                if sx != 0 || shapeLinked { _ = model.engine.editShape(id, param: 5, value: w.clamped(to: 1...16384), continuing: false) }
                if sy != 0 || shapeLinked { _ = model.engine.editShape(id, param: 6, value: h.clamped(to: 1...16384), continuing: false) }
            }
            model.refreshSelectedLayer(); model.invalidatePreview()
        }

        @objc func handlePan(_ gesture: UIPanGestureRecognizer) {
            guard interactive, model.pointPick == nil, let view = gesture.view, model.primarySelection != nil, !(model.selectedLayer?.locked ?? false) else { return }
            if model.panel == .vector && model.vectorFreehand {
                let p = compositionPoint(gesture.location(in: view), view: view), f = scaleFactor(view)
                switch gesture.state {
                case .began: model.freehandPoints = [p.x, p.y]
                case .changed:
                    let points = model.freehandPoints
                    if points.count < 20000 && (points.count < 2 || hypot(p.x - points[points.count - 2], p.y - points[points.count - 1]) > f * 2) { model.freehandPoints += [p.x, p.y] }
                case .ended: model.finishFreehand()
                case .cancelled, .failed: model.freehandPoints = []
                default: break
                }
            } else if (model.panel == .mask || (model.panel == .vector && model.vectorEditingPoints)) && model.editingMask != nil { handleMaskPan(gesture, view: view) }
        }
    }
}

struct StageTouchPoint {
    let id: ObjectIdentifier
    let position: CGPoint
    let pressed: Bool
}

/// One recognizer owns a continuous pointer sequence; UIKit's independent
/// pan/pinch/rotation recognizers cannot express Android's remaining-finger rule.
@MainActor final class StageTouchRecognizer: UIGestureRecognizer {
    var onEvent: (([StageTouchPoint], Bool) -> Void)?
    private var touches: [UITouch] = []
    override func touchesBegan(_ new: Set<UITouch>, with event: UIEvent) {
        for touch in new.sorted(by: { $0.timestamp < $1.timestamp }) where !touches.contains(where: { $0 === touch }) { touches.append(touch) }
        state = state == .possible ? .began : .changed
        emit(cancelled: false)
    }
    override func touchesMoved(_ moved: Set<UITouch>, with event: UIEvent) { state = .changed; emit(cancelled: false) }
    override func touchesEnded(_ ended: Set<UITouch>, with event: UIEvent) {
        emit(cancelled: false)
        touches.removeAll { ended.contains($0) }
        state = touches.isEmpty ? .ended : .changed
    }
    override func touchesCancelled(_ cancelled: Set<UITouch>, with event: UIEvent) {
        emit(cancelled: true); touches.removeAll(); state = .cancelled
    }
    override func reset() {
        if !touches.isEmpty { onEvent?([], true) }
        touches.removeAll(); super.reset()
    }
    private func emit(cancelled: Bool) {
        guard let view else { return }
        onEvent?(touches.map { StageTouchPoint(id: ObjectIdentifier($0), position: $0.location(in: view), pressed: $0.phase != .ended && $0.phase != .cancelled) }, cancelled)
    }
}

@MainActor enum ShapeStageGeometry {
    static func enabled(_ model: AureaModel) -> Bool {
        guard model.panel == .shapeEdit, model.selection.count == 1, let row = model.selectedLayer else { return false }
        return row.kind == 5 && !model.isVectorLayer && !row.locked && Int64(row.startFrame) <= model.status.playhead && model.status.playhead < Int64(row.endFrame)
    }
    static func handles(_ detail: [String: Any], corners c: [CGPoint]) -> [CGPoint] {
        guard c.count == 4 else { return [] }
        var result = c
        for i in 0..<4 { let j = (i + 1) % 4; result.append(CGPoint(x: (c[i].x + c[j].x) / 2, y: (c[i].y + c[j].y) / 2)) }
        let shape = StageGeom.floats(detail["shape"]), w = CGFloat(StageGeom.width(detail)), h = CGFloat(StageGeom.height(detail))
        let type = (detail["shapeTypePoints"] as? NSNumber)?.uint32Value ?? 0
        if type & 0xFFFF == 0 && w > 0 && h > 0 {
            let rho = min(CGFloat(shape.count > 4 ? shape[4] : 0), min(w, h) / 2)
            let ux = (c[1].x - c[0].x) / w, uy = (c[1].y - c[0].y) / w
            let vx = (c[3].x - c[0].x) / h, vy = (c[3].y - c[0].y) / h
            var px = (ux + vx) * rho, py = (uy + vy) * rho
            if hypot(px, py) < 24 {
                let dx = ux + vx, dy = uy + vy, length = hypot(dx, dy)
                if length > 0.000001 { px = dx / length * 24; py = dy / length * 24 }
            }
            result.append(CGPoint(x: c[0].x + px, y: c[0].y + py))
        }
        return result
    }
}

@MainActor struct StageInteractionOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    var body: some View {
        Canvas { context, size in
            let cw = CGFloat(max(1, model.compositionWidth)), ch = CGFloat(max(1, model.compositionHeight))
            let fit = min(size.width / cw, size.height / ch), ox = (size.width - cw * fit) / 2, oy = (size.height - ch * fit) / 2
            func screen(_ x: Float, _ y: Float) -> CGPoint { CGPoint(x: CGFloat(x) * fit + ox, y: CGFloat(y) * fit + oy) }
            if let x = shell.snapX {
                var p = Path(); p.move(to: screen(x, 0)); p.addLine(to: screen(x, Float(ch)))
                context.stroke(p, with: .color(Color(hex: 0xFF6B6B).opacity(0.8)), lineWidth: 1.5)
            }
            if let y = shell.snapY {
                var p = Path(); p.move(to: screen(0, y)); p.addLine(to: screen(Float(cw), y))
                context.stroke(p, with: .color(Color(hex: 0xFF6B6B).opacity(0.8)), lineWidth: 1.5)
            }
            guard ShapeStageGeometry.enabled(model) else { return }
            var data: [Float] = []; guard StageGeom.corners(model.detail, &data) else { return }
            let points = ShapeStageGeometry.handles(model.detail, corners: stride(from: 0, to: 8, by: 2).map { screen(data[$0], data[$0 + 1]) })
            func disk(_ point: CGPoint, _ radius: CGFloat) -> Path { Path(ellipseIn: CGRect(x: point.x - radius, y: point.y - radius, width: radius * 2, height: radius * 2)) }
            for i in points.indices {
                let selected = shell.grabbedShapeHandle == i
                let radius: CGFloat = i == 8 ? (selected ? 8 : 7) : selected ? 7 : i < 4 ? 6 : 5
                context.fill(disk(points[i], radius + 1.5), with: .color(StageInk.outlineUnder))
                context.fill(disk(points[i], radius), with: .color(i == 8 ? AureaColors.accent : .white))
                if i == 8 { context.fill(disk(points[i], radius * 0.35), with: .color(.white)) }
                else { context.stroke(disk(points[i], radius), with: .color(AureaColors.accent), lineWidth: 1.5) }
            }
        }
    }
}
