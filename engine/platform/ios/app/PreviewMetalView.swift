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
        // Commands, resource completion and refinement wake the shared render
        // thread directly. A paused composition needs no extra vsync wakeups.
        view.isPaused = model.status.playing == 0
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
        view.isPaused = model.status.playing == 0
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
        private var gizmoAxis = -1
        private var gizmoVector = CGPoint.zero
        private var gizmoCollapsed = false
        private var lastGizmoPoint = CGPoint.zero
        // Gizmo.kt/Stage.kt gizmoGesture: ferramenta, alça desenhada e valores de
        // PARTIDA (rotação/escala absolutas desde o toque, sem deriva).
        private var gizmoTool = 0
        private var gizmoOrigin = CGPoint.zero
        private var gizmoHandle = CGPoint.zero
        private var gizmoFacing = false
        private var gizmoMoved = false
        private var gizmoBase: SIMD3<Float> = .zero
        private var gizmoSwept: CGFloat = 0
        private var gizmoAlong: CGFloat = 0
        private var gizmoLastAngle: CGFloat = 0
        private var gizmoDownTime: CFTimeInterval = 0
        /// Ferramenta Girar: arrasto no trackball (TrackballOverlay.swift) em vez das alças.
        private var trackball: TrackballSession?
        private var pinchThreeD = false
        private var pinchTracker = StagePinchTracker()
        private var pinchLayer: Int64?
        private enum StageMode { case pending, move, pinch, idle, gizmo, shape, resize, scene, pivot, view, viewPan, rigPose }
        // Face Pivô aberta: o arrasto move o pivô (PivotDragSession).
        private var pivotSession: PivotDragSession?
        // Cena 3D: 0 pendente, 1 órbita, 2 objeto, 3 pinça.
        private var sceneMode = 0
        private var scenePicked: Int64?
        /// Bug 9 (0.3.4): havia seleção no toque? Sem ela o gesto é da câmera da cena.
        private var sceneHadSelection = false
        private var sceneLast = CGPoint.zero
        private var sceneSpan: CGFloat = 1
        private var sceneDistance0: Float = 3
        private var sceneTapAt: TimeInterval = 0
        private var sceneTapPoint = CGPoint.zero
        private var stageMode: StageMode = .idle
        private var stageDown = CGPoint.zero
        /// Quando o dedo pousou no palco (segurar uma camada = escolher várias).
        private var stageDownTime: CFTimeInterval = 0
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
        private var axisLock = 0              // 1 = só X, 2 = só Y (setas de eixo)
        private var pinchRotationActive = false
        private var pinchRotationOffset: Float = 0
        // Encaixes da pinça (nil = solto): giro em múltiplos de 45°, escala em 100%.
        private var rotationSnap: Float?
        private var scaleSnap: Float?
        // Mover em grupo (2+ escolhidas): posição local e pai → composição de
        // PARTIDA de cada camada, e o centro da caixa que abraça o grupo.
        private var groupMove: [(id: Int64, local: SIMD2<Float>, affine: [Float])] = []
        private var groupCentre: SIMD2<Float>?
        private var shapeHandle = -1
        private var shapeWidth: Float = 1
        private var shapeHeight: Float = 1
        private var shapeRadius: Float = 0
        private var shapeU = SIMD2<Float>.zero
        private var shapeV = SIMD2<Float>.zero
        private var shapeScale = SIMD2<Float>(repeating: 1)
        private var shapeLinked = true
        // Omega conteúdo (área 06, P0-6): o passo já enviado ao motor (px da forma) e se o gesto já abriu o desfazer.
        private var shapeLastU: Float = 0
        private var shapeLastV: Float = 0
        private var shapeSent = false
        // Alça branca de esticar: índice, âncora e alça na composição, direção da conta.
        private var resizeHandle = -1
        private var resizeAnchor = SIMD2<Float>(0, 0)
        private var resizeStart = SIMD2<Float>(0, 0)
        private var resizeAxis = SIMD2<Float>(1, 0)
        private var resizeBase: Float = 1
        private let snapFeedback = UISelectionFeedbackGenerator()
        // Omega aparência (P1-1/P1-4): o toque do conta-gotas ao vivo e das pontas do degradê.
        let omegaAppearance = OmegaAppearanceStageGesture()

        init(model: AureaModel, shell: ShellPresentation, compositionSize: CGSize, interactive: Bool) {
            self.model = model
            self.shell = shell
            self.compositionSize = compositionSize
            self.interactive = interactive
        }

        func configureGestures() {
            let path = (model.panel == .vector && (model.vectorFreehand || model.vectorEditingPoints)) || (model.panel == .mask && model.editingMask != nil)
            let legacy = path || model.pointPick != nil || model.focusPick
            if tap?.isEnabled != (interactive && legacy) { tap?.isEnabled = interactive && legacy }
            if pan?.isEnabled != (interactive && path) { pan?.isEnabled = interactive && path }
            if stage?.isEnabled != (interactive && !legacy) { stage?.isEnabled = interactive && !legacy }
        }

        /// px da tela → px da composição (a razão entre o quadro da composição e
        /// o tamanho do palco, dividida pelo zoom da vista). O motor trabalha em
        /// px da composição; tolerâncias em pt × este fator ficam constantes na tela.
        private func scaleFactor(_ view: UIView) -> Float {
            let width = max(1, view.bounds.width)
            let height = max(1, view.bounds.height)
            let compWidth = max(1, compositionSize.width)
            let compHeight = max(1, compositionSize.height)
            // Omega chrome (área 02, P1-2): o zoom da vista agora vai de 1 % a 6400 %.
            return Float(max(compWidth / width, compHeight / height) / max(StageZoomMath.minZoom, StageViewZoom.shared.zoom))
        }

        @objc func handleTap(_ gesture: UITapGestureRecognizer) {
            guard interactive, let view = gesture.view else { return }
            let factor = CGFloat(scaleFactor(view))
            let c = compositionPoint(gesture.location(in: view), view: view)
            let point = CGPoint(x: CGFloat(c.x), y: CGFloat(c.y))
            if model.focusPick { model.finishFocusPick(point); return }
            if model.pointPick != nil { model.finishPointPick(point); return }
            if model.panel == .vector && model.vectorFreehand { return }
            if model.panel == .vector && model.vectorEditingPoints, let mask = model.editingMask, let local = maskLocal(point) {
                let hit = nearestMaskPoint(local, tolerance: Float(factor) * 20)
                if let hit {
                    if model.vectorPointTool == 2 { deleteVectorPoint(hit, mask: mask) }
                    else if model.vectorPointTool == 3 { toggleVectorCorner(hit, mask: mask) }
                    else {
                        if model.vectorPointTool == 1 && hit == 0 && !mask.closed && mask.points.count >= 18 && model.selectedMaskPoint == mask.points.count / 6 - 1 {
                            model.editVectorPath(PathOps.setClosed, [1]); model.refreshMasks()   // Omega conteúdo (P1-8)
                        }
                        model.selectedMaskPoint = hit
                    }
                } else if model.vectorPointTool == 1 {
                    if !insertVectorPoint(local, mask: mask, tolerance: Float(factor) * 20) && !mask.closed {
                        // Omega conteúdo (área 06, P1-8): acrescentar na ponta é regra do motor.
                        let r = model.editVectorPath(PathOps.append, [local.x, local.y, 0]); model.refreshMasks()
                        model.selectedMaskPoint = r >= 0 ? Int(r) : nil
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
        // Omega conteúdo (área 06, P1-8): as REGRAS de ponto (apagar, canto ↔ curva, inserir,
        // acrescentar, alças) são do motor (Engine::edit_vector_path, PathEdit.cpp); aqui só a escolha.
        private func deleteVectorPoint(_ i: Int, mask: MaskItem) {
            let r = model.editVectorPath(PathOps.remove, [Float(i)])
            model.refreshMasks()
            let left = (model.editingMask?.points.count ?? 0) / 6
            model.selectedMaskPoint = r >= 0 && left > 0 ? min(Int(r), left - 1) : nil
        }
        private func toggleVectorCorner(_ i: Int, mask: MaskItem) {
            let at = i * 6
            guard at + 5 < mask.points.count else { return }
            let curve = hypot(mask.points[at + 2], mask.points[at + 3]) > 0.001 || hypot(mask.points[at + 4], mask.points[at + 5]) > 0.001
            model.editVectorPath(PathOps.setType, [Float(i), curve ? 0 : 1])
            model.refreshMasks(); model.selectedMaskPoint = i
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
            // Omega conteúdo (área 06, P1-8): o corte de De Casteljau é do motor (a curva não muda).
            let r = model.editVectorPath(PathOps.insert, [Float(i), bestT])
            model.refreshMasks()
            if r >= 0 { model.selectedMaskPoint = Int(r) }
            return r >= 0
        }
        private func handleMaskPan(_ gesture: UIPanGestureRecognizer, view: UIView) {
            let factor = scaleFactor(view), c = compositionPoint(gesture.location(in: view), view: view)
            let comp = CGPoint(x: CGFloat(c.x), y: CGFloat(c.y))
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
                    // Omega conteúdo (área 06, P1-8): no vetor, acrescentar é regra do motor.
                    if model.panel == .vector { model.editVectorPath(PathOps.append, [local.x, local.y, 0]); model.refreshMasks() }
                    else { model.setMaskPoints(mask.points + [local.x, local.y, 0, 0, 0, 0], closed: false) }
                    maskDragChanged = true; maskDragSmooth = true; maskOppositeLength = -1
                }
                if let n = maskDragPoint, n * 6 + 1 < mask.points.count { maskDragOffset = SIMD2(mask.points[n * 6], mask.points[n * 6 + 1]) - local }
                model.selectedMaskPoint = maskDragPoint
            case .changed:
                guard let n = maskDragPoint, n * 6 + 5 < mask.points.count else { return }
                if model.panel == .vector {
                    // Omega conteúdo (área 06, P1-8): ponto e alças do vetor pelas regras do motor
                    // (alça suave = ângulo preso; ponto novo = ângulo e comprimento presos).
                    if maskDragHandle == 0 {
                        let d = local + maskDragOffset - SIMD2(mask.points[n * 6], mask.points[n * 6 + 1])
                        model.editVectorPath(PathOps.movePoints, [d.x, d.y, Float(n)], continuing: maskDragChanged)
                    } else {
                        let wanted = local - SIMD2(mask.points[n * 6], mask.points[n * 6 + 1])
                        let d = wanted - SIMD2(mask.points[n * 6 + maskDragHandle], mask.points[n * 6 + maskDragHandle + 1])
                        model.editVectorPath(PathOps.moveHandle, [Float(n), maskDragHandle == 4 ? 1 : 0, d.x, d.y,
                                                                  maskDragSmooth ? 1 : 0, maskOppositeLength < 0 ? 1 : 0], continuing: maskDragChanged)
                    }
                    model.refreshMasks(); maskDragChanged = true
                    return
                }
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

        // Com o zoom da vista: o centro da composição fica no centro do palco
        // mais o pan (pt), e `scaleFactor` já vem dividido pelo zoom.
        private func compositionPoint(_ point: CGPoint, view: UIView) -> SIMD2<Float> {
            let f = scaleFactor(view), pan = StageViewZoom.shared.pan
            return SIMD2(Float(point.x - view.bounds.width / 2 - pan.width) * f + Float(compositionSize.width / 2),
                         Float(point.y - view.bounds.height / 2 - pan.height) * f + Float(compositionSize.height / 2))
        }
        private func screenPoint(_ x: Float, _ y: Float, view: UIView) -> CGPoint {
            let f = CGFloat(scaleFactor(view)), pan = StageViewZoom.shared.pan
            return CGPoint(x: (CGFloat(x) - compositionSize.width / 2) / f + view.bounds.width / 2 + pan.width,
                           y: (CGFloat(y) - compositionSize.height / 2) / f + view.bounds.height / 2 + pan.height)
        }
        // Gesto da vista (zoom/pan do palco): valores de PARTIDA, nada acumula.
        private var viewZoom0: CGFloat = 1
        private var viewPan0 = CGSize.zero
        private var viewSpan0: CGFloat = 1
        private var viewMid0 = CGPoint.zero
        private func startView(_ a: CGPoint, _ b: CGPoint) {
            let zoom = StageViewZoom.shared
            viewZoom0 = zoom.zoom; viewPan0 = zoom.pan
            viewSpan0 = max(1, hypot(a.x - b.x, a.y - b.y))
            viewMid0 = CGPoint(x: (a.x + b.x) / 2, y: (a.y + b.y) / 2)
        }
        private func startViewPan() {
            viewZoom0 = StageViewZoom.shared.zoom; viewPan0 = StageViewZoom.shared.pan
        }
        /// Pinça da vista: zoom pela abertura; o ponto sob o meio dos dedos segue o meio.
        private func stepView(_ a: CGPoint, _ b: CGPoint, view: UIView) {
            guard a.x.isFinite && a.y.isFinite && b.x.isFinite && b.y.isFinite else { return }
            let span = hypot(a.x - b.x, a.y - b.y)
            let z = span >= 16 && viewSpan0 >= 16 ? StageZoomMath.clampZoom(viewZoom0 * span / viewSpan0) : viewZoom0
            let mid = CGPoint(x: (a.x + b.x) / 2, y: (a.y + b.y) / 2)
            let px = StageZoomMath.pinchPan(viewPan0.width, zoom0: viewZoom0, zoom1: z, mid0: viewMid0.x, mid: mid.x, centre: view.bounds.width / 2)
            let py = StageZoomMath.pinchPan(viewPan0.height, zoom0: viewZoom0, zoom1: z, mid0: viewMid0.y, mid: mid.y, centre: view.bounds.height / 2)
            applyView(z, CGSize(width: px, height: py), view: view)
            viewZoom0 = StageViewZoom.shared.zoom; viewPan0 = StageViewZoom.shared.pan
            viewSpan0 = span; viewMid0 = mid
        }
        private func stepViewPan(_ point: CGPoint, view: UIView) {
            applyView(viewZoom0, CGSize(width: viewPan0.width + point.x - stageDown.x, height: viewPan0.height + point.y - stageDown.y), view: view)
        }
        private func applyView(_ z: CGFloat, _ pan: CGSize, view: UIView) {
            let base = StageZoomMath.fit(size: view.bounds.size, composition: compositionSize, zoom: 1, pan: .zero).scale
            let clamped = CGSize(width: StageZoomMath.clampPan(pan.width, zoom: z, baseExtent: compositionSize.width * base),
                                 height: StageZoomMath.clampPan(pan.height, zoom: z, baseExtent: compositionSize.height * base))
            StageViewZoom.shared.set(zoom: z, pan: clamped, engine: model.engine,
                                     pixelScale: view.window?.screen.scale ?? UIScreen.main.scale)
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
            trackball?.end(); trackball = nil
            markerHold?.cancel(); markerHold = nil
            markerAnchorLayer = nil
            if editBegan { model.endGesture() }
            editBegan = false; model.stageManipulating = false
            shell.snapX = nil; shell.snapY = nil; shell.grabbedHandle = -1; shell.grabbedShapeHandle = -1
            shell.grabbedResizeHandle = -1; resizeHandle = -1
        }
        private func keepTransform() {
            startPosition = vector(model.detail["position"])
            startScale = vector(model.detail["scale"])
            startRotation = vector(model.detail["rotation"])
            startKind = Int32(StageGeom.layerKind(model.detail))
        }
        // Pose do rig: estado do gesto em curso.
        private var rigNear: Int32 = -1
        private var rigMoved = false
        private var rigBegan = false
        private var rigAngle: CGFloat?
        private var rigLast = SIMD2<Float>(0, 0)
        /// Um dedo gira o osso (trackball, px da composição); dois giram em volta do olhar.
        private func rigPoseEvent(_ pressed: [StageTouchPoint], view: UIView) {
            if pressed.isEmpty {
                if rigBegan { model.endGesture(); model.endPoseGesture() }
                if !rigMoved && rigNear < 0 { model.choosePoseBone(-1) }   // toque no vazio solta o osso
                stageFinger = nil; stageMode = .idle
                return
            }
            if pressed.count >= 2 {
                hadMultipleTouches = true
                let a = pressed[0].position, b = pressed[1].position
                let angle = atan2(b.y - a.y, b.x - a.x) * 180 / .pi
                defer { rigAngle = angle }
                rigMoved = true
                guard let previous = rigAngle else { return }
                var twist = Float(angle - previous)
                while twist > 180 { twist -= 360 }
                while twist < -180 { twist += 360 }
                if !rigBegan { model.beginGesture("posar osso"); rigBegan = true }
                model.dragPoseBone(dx: 0, dy: 0, twist: twist)
                return
            }
            if rigAngle != nil { return }   // voltou a um dedo depois do giro: não vira arrasto
            guard let finger = pressed.first(where: { $0.id == stageFinger }) ?? pressed.first else { return }
            if !rigMoved && hypot(finger.position.x - stageDown.x, finger.position.y - stageDown.y) < 8 { return }
            let point = compositionPoint(finger.position, view: view)
            rigMoved = true
            let dx = point.x - rigLast.x, dy = point.y - rigLast.y
            rigLast = point
            if dx == 0 && dy == 0 { return }
            if !rigBegan { model.beginGesture("posar osso"); rigBegan = true }
            model.dragPoseBone(dx: dx, dy: dy, twist: 0)
        }
        /// Tipo da camada no toque: a regra de profundidade do motor (Z de conteúdo acompanha X).
        private var startKind: Int32 = 0
        private var previewBasis: [NSNumber] = []
        private var pinchCenter = CGPoint.zero
        private var pinchPanActive = false
        private var pinchPointScale: Float = 1
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
            // Omega aparência (P1-1/P1-4): conta-gotas ligado ou pontas do degradê abertas
            // pegam a sequência inteira de toques antes do resto do palco.
            if omegaAppearanceEvent(points, cancelled: cancelled, view: view) { return }
            if cancelled || !interactive {
                finishStageEdit(); stageFinger = nil; stageMode = .idle; return
            }
            let pressed = points.filter(\.pressed)
            if stageFinger == nil {
                guard let first = pressed.first else { return }
                stageFinger = first.id; stageDown = first.position; stageDownTime = CACurrentMediaTime(); hadMultipleTouches = false
                stageMode = .pending; handle = -1; shapeHandle = -1; gizmoAxis = -1; targetLayer = nil
                axisLock = 0; pinchRotationActive = false
                rotationSnap = nil; scaleSnap = nil; groupMove = []; groupCentre = nil
                model.refreshSelectedLayer()
                if model.pivotStageActive && editableSelection(), let pivot = PivotDragSession.pivotPoint(model),
                   let session = PivotDragSession(model: model, pivot: pivot) {
                    pivotSession = session; stageMode = .pivot; return
                }
                // Pose do rig (RigPoseStage.kt): junta perto do dedo ou osso escolhido = gira o osso.
                let poseNear = model.rigPoseActive ? model.pickPoseBone(compositionPoint(first.position, view: view), reach: 36 * scaleFactor(view)) : -1
                if model.rigPoseActive && (poseNear >= 0 || model.poseBone >= 0) {
                    if poseNear >= 0 && poseNear != model.poseBone { model.choosePoseBone(poseNear) }
                    stageMode = .rigPose; rigNear = poseNear; rigMoved = false; rigBegan = false; rigAngle = nil
                    rigLast = compositionPoint(first.position, view: view)
                    return
                }
                if !model.sceneEditor && startShape(at: first.position, view: view) { stageMode = .shape }
                else if startGizmo(at: first.position, view: view) { stageMode = .gizmo }
                else if !model.sceneEditor && startResize(at: first.position, view: view) { stageMode = .resize }
                else if model.sceneEditor {
                    stageMode = .scene; sceneMode = 0; sceneLast = first.position
                    sceneHadSelection = !model.selection.isEmpty
                    scenePicked = model.scenePick(compositionPoint(first.position, view: view), radius: 36 * scaleFactor(view))
                    if scenePicked == nil, let selected = model.selectedLayer, [6,8].contains(selected.kind) { scenePicked = selected.id }
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
            if stageMode == .pivot {
                pivotEvent(pressed, view: view)
                return
            }
            if stageMode == .rigPose {
                rigPoseEvent(pressed, view: view)
                return
            }
            if stageMode == .scene {
                sceneEvent(pressed, view: view)
                if pressed.isEmpty { finishStageEdit(); stageFinger = nil; stageMode = .idle }
                return
            }
            if pressed.isEmpty {
                // Toque parado no quadrado central da escala = toque da âncora (beat).
                if stageMode == .gizmo && !gizmoMoved && gizmoAxis == 3 && !hadMultipleTouches && model.previewMarkerAnchor != nil {
                    if CACurrentMediaTime() - gizmoDownTime >= 0.45 { model.editMarkerAtPlayhead() }
                    else { model.toggleMarkerAt(model.status.playhead) }
                }
                if stageMode == .pending && !hadMultipleTouches && handle < 0 {
                    if let layer = markerAnchorLayer, layer == model.primarySelection, model.previewMarkerAnchor != nil {
                        model.toggleMarkerAt(model.status.playhead)
                    } else {
                        let point = compositionPoint(stageDown, view: view)
                        let selected3D = model.selectedLayer.flatMap { row -> Int64? in
                            guard row.kind == 10, row.visible, !row.locked, model.selection.count == 1, active(row),
                                  StageGeom.contains(model.detail, point.x, point.y, slack: scaleFactor(view) * 20) else { return nil }
                            return row.id
                        }
                        let hit = selected3D ?? hitLayer(point, slack: 0, includeLocked: false) ?? hitLayer(point, slack: scaleFactor(view) * 12, includeLocked: false)
                        // Segurar uma camada soma (ou tira) ela da seleção e liga o modo de
                        // escolher várias — a barra de grupo/máscara aparece no topo (Stage.kt).
                        let held = CACurrentMediaTime() - stageDownTime >= 0.5
                        if let hit, held {
                            if model.selection.isEmpty { model.select(layerId: hit, additive: false) } else { model.toggleTimelineLayerPick(hit) }
                            model.changeTimelineLayerSelectMode(true)
                            UIImpactFeedbackGenerator(style: .medium).impactOccurred()
                        } else if let hit {
                            // Omega conteúdo (área 06, P0-7): o segundo toque na MESMA camada abre o editor do tipo dela.
                            if !OmegaContentEditors.doubleTap(model, hit: hit),
                               model.primarySelection != hit || model.selection.count != 1 { model.select(layerId: hit, additive: false) }
                        } else if !held { model.clearSelection() }
                    }
                }
                finishStageEdit(); stageFinger = nil; stageMode = .idle; return
            }
            if stageMode == .shape || stageMode == .gizmo || stageMode == .resize {
                guard let first = pressed.first(where: { $0.id == stageFinger }) else { finishStageEdit(); stageMode = .idle; return }
                if stageMode == .shape { stepShape(first.position, view: view) }
                else if stageMode == .resize { stepResize(first.position, view: view) }
                else { stepGizmo(first.position, view: view) }
                return
            }
            if pressed.count >= 2 && stageMode != .pinch && stageMode != .view && stageMode != .idle {
                hadMultipleTouches = true; finishStageEdit()
                let a = pressed[0], b = pressed[pressed.count - 1]
                let middle = CGPoint(x: (a.position.x + b.position.x) / 2, y: (a.position.y + b.position.y) / 2)
                let slack = scaleFactor(view) * 12
                let over = [a.position, b.position, middle].contains { point in
                    let c = compositionPoint(point, view: view)
                    return StageGeom.contains(model.detail, c.x, c.y, slack: slack)
                }
                // Lupa ligada: a pinça é SEMPRE da vista. Senão, só é da camada
                // escolhida quando os dedos estão sobre ela; no vazio amplia a vista.
                let selected3D = model.primarySelection.map { !model.engine.previewGestureBasis($0).isEmpty } ?? false
                if !StageViewZoom.shared.zoomLock && editableSelection() && (over || selected3D) {
                    startLayerPinch(a, b, view: view); stageMode = .pinch
                } else {
                    pinchFingers = [a.id, b.id]; startView(a.position, b.position); stageMode = .view
                }
            }
            switch stageMode {
            case .pending:
                guard let first = pressed.first(where: { $0.id == stageFinger }) else { return }
                if hypot(first.position.x - stageDown.x, first.position.y - stageDown.y) > 8 {
                    markerHold?.cancel(); markerHold = nil; markerAnchorLayer = nil
                }
                if hypot(first.position.x - stageDown.x, first.position.y - stageDown.y) > (handle >= 0 ? 4 : 18) {
                    if handle < 0 && targetLayer == nil && StageViewZoom.shared.zoomed {
                        // Vazio com a vista ampliada: um dedo passeia a vista.
                        markerHold?.cancel(); markerHold = nil; markerAnchorLayer = nil
                        startViewPan(); stageMode = .viewPan; stepViewPan(first.position, view: view)
                        return
                    }
                    startDrag(view: view)
                    stepEdit(first.position, view: view)
                }
            case .move:
                if let first = pressed.first(where: { $0.id == stageFinger }) { stepEdit(first.position, view: view) }
                else { stageMode = .idle }
            case .pinch:
                guard pinchFingers.count == 2,
                      let a = pressed.first(where: { $0.id == pinchFingers[0] }), let b = pressed.first(where: { $0.id == pinchFingers[1] }) else {
                    finishStageEdit(); stageMode = .idle; return // The remaining finger stays idle until all lift.
                }
                stepPinch(a.position, b.position)
            case .view:
                guard pinchFingers.count == 2,
                      let a = pressed.first(where: { $0.id == pinchFingers[0] }), let b = pressed.first(where: { $0.id == pinchFingers[1] }) else {
                    stageMode = .idle; return
                }
                stepView(a.position, b.position, view: view)
            case .viewPan:
                if let first = pressed.first(where: { $0.id == stageFinger }) { stepViewPan(first.position, view: view) }
                else { stageMode = .idle }
            default: break
            }
        }
        /// Pivô: anda o mesmo que o dedo (relativo: o dedo não cobre a mira) e
        /// a posição compensa, a imagem fica. Toque parado leva o pivô ao ponto.
        /// Um arrasto = UM passo de desfazer.
        private func pivotEvent(_ pressed: [StageTouchPoint], view: UIView) {
            guard let session = pivotSession else { finishStageEdit(); stageFinger = nil; stageMode = .idle; return }
            if pressed.isEmpty {
                if !editBegan && !hadMultipleTouches {
                    beginEdit("mover pivô"); session.move(to: compositionPoint(stageDown, view: view), model: model)
                    snapFeedback.selectionChanged()
                }
                finishStageEdit(); pivotSession = nil; stageFinger = nil; stageMode = .idle; return
            }
            if pressed.count >= 2 { hadMultipleTouches = true }
            guard let first = pressed.first(where: { $0.id == stageFinger }) else { return }
            if !editBegan && hypot(first.position.x - stageDown.x, first.position.y - stageDown.y) > 8 {
                engage(); beginEdit("mover pivô")
            }
            guard editBegan else { return }
            let delta = compositionPoint(first.position, view: view) - compositionPoint(stageDown, view: view)
            session.move(to: session.start + delta, model: model)
        }
        private func recordTarget(_ point: CGPoint, view: UIView) {
            // Setas X/Y do 2D (a camada 3D tem o gizmo, que já pegou o toque).
            if editableSelection() && !ShapeStageGeometry.enabled(model), let anchor = model.previewMarkerAnchor,
               let id = model.primarySelection, model.engine.gizmo(id, length: ShellStageGeometry.gizmoLength).isEmpty {
                let tips = ShellStageGeometry.axisHandles(screenPoint(anchor.x, anchor.y, view: view), size: view.bounds.size)
                var closest = CGFloat.greatestFiniteMagnitude
                for i in tips.indices {
                    let distance = hypot(point.x - tips[i].x, point.y - tips[i].y)
                    if distance <= AureaDims.axisHandleTarget && distance < closest { handle = i; closest = distance }
                }
                if handle >= 0 { return }
            }
            let c = compositionPoint(point, view: view)
            if model.selection.count == 1, let row = model.selectedLayer, active(row), StageGeom.contains(model.detail, c.x, c.y, slack: row.kind == 10 ? scaleFactor(view) * 20 : 0) { targetLayer = row.id }
            // Seleção múltipla: dentro de uma das escolhidas vale ela (o arrasto
            // leva o grupo), mesmo com outra por cima.
            else if model.selection.count >= 2, let hit = hitSelected(c) { targetLayer = hit }
            else { targetLayer = hitLayer(c, slack: 0, includeLocked: true) }
            if targetLayer == nil, let row = model.selectedLayer, model.selection.count == 1,
               (row.kind == 6 || row.kind == 8 || row.kind == 9), !model.engine.previewGestureBasis(row.id).isEmpty {
                targetLayer = row.id
            }
        }
        private func hitSelected(_ point: SIMD2<Float>) -> Int64? {
            for row in model.layers where model.selection.contains(row.id) && row.visible && row.kind != 3 && active(row) {
                if let detail = model.engine.layerDetail(row.id), StageGeom.contains(detail, point.x, point.y, slack: 0) { return row.id }
            }
            return nil
        }
        /// As escolhidas que andam no mover em grupo: sem as travadas, sem as
        /// fora do tempo com posição animada (o keyframe cairia fora do clipe)
        /// e sem quem já é levado por um pai também escolhido (Stage.kt `groupMovers`).
        private func groupMovers() -> [(id: Int64, local: SIMD2<Float>, affine: [Float])] {
            var details: [Int64: [String: Any]] = [:]
            var order: [Int64] = []
            for row in model.layers where model.selection.contains(row.id) && !row.locked && row.kind != 3 {
                guard let d = model.engine.layerDetail(row.id) else { continue }
                let mask = (d["animatedMask"] as? NSNumber)?.uint32Value ?? 0
                if mask & 0b11 != 0 && !active(row) { continue }
                details[row.id] = d; order.append(row.id)
            }
            var parents: [Int64: Int64] = [:]
            let roots = StageMath.moveRoots(order) { (id: Int64) -> Int64 in
                if let d = details[id] { return (d["parentId"] as? NSNumber)?.int64Value ?? 0 }
                if let p = parents[id] { return p }
                let p = (self.model.engine.layerDetail(id)?["parentId"] as? NSNumber)?.int64Value ?? 0
                parents[id] = p; return p
            }
            return roots.compactMap { id -> (id: Int64, local: SIMD2<Float>, affine: [Float])? in
                guard let d = details[id] else { return nil }
                let p = self.vector(d["position"])
                return (id, SIMD2(p.x, p.y), StageGeom.floats(d["parentAffine"]))
            }
        }
        private func startDrag(view: UIView) {
            previewBasis = []
            // Seta do eixo: o mesmo mover, travado em X (0) ou Y (1) desde o toque.
            if handle >= 0 {
                guard editableSelection() else { stageMode = .idle; return }
                targetLayer = model.primarySelection
            }
            // 2+ escolhidas e o dedo numa delas: o grupo inteiro anda junto (as
            // travadas ficam), sem desfazer a seleção.
            if handle < 0, let id = targetLayer, model.selection.count >= 2, model.selection.contains(id) {
                let movers = groupMovers()
                guard !movers.isEmpty else { model.toast = AureaText.t("sh_layer_locked_unlock_to_move"); stageMode = .idle; return }
                groupMove = movers
                var sets: [[Float]] = []
                for mover in movers {
                    var c: [Float] = []
                    if let d = model.engine.layerDetail(mover.id), StageGeom.corners(d, &c) { sets.append(c) }
                }
                if let box = StageMath.unionBox(sets) { groupCentre = SIMD2((box.minX + box.maxX) / 2, (box.minY + box.maxY) / 2) }
                else { groupCentre = nil }
                moveDown = compositionPoint(stageDown, view: view); moveLast = moveDown
                axisLock = 0
                engage(); stageMode = .move
                return
            }
            if let id = targetLayer {
                if model.primarySelection != id || model.selection.count != 1 { model.select(layerId: id, additive: false) }
                guard let row = model.selectedLayer, row.id == id else { stageMode = .idle; return }
                guard !row.locked else { model.toast = AureaText.t("sh_layer_locked_unlock_to_move"); stageMode = .idle; return }
                keepTransform(); moveAffine = StageGeom.floats(model.detail["parentAffine"])
                if handle < 0 { previewBasis = model.engine.previewGestureBasis(id) }
                moveWorld = worldPosition(startPosition, affine: moveAffine)
                moveDown = compositionPoint(stageDown, view: view); moveLast = moveDown
                axisLock = handle >= 0 ? handle + 1 : 0
                if handle >= 0 { shell.grabbedHandle = handle }
                engage(); stageMode = .move
            } else { stageMode = .idle }
        }
        private func clampScale(_ value: Float) -> Float {
            pinchThreeD ? model.engine.clampPinchFactor3D(value, kind: startKind, scaleX: startScale.x, scaleY: startScale.y, scaleZ: startScale.z)
                : model.engine.clampPinchFactor(value, scaleX: startScale.x, scaleY: startScale.y, scaleZ: startScale.z, threeD: false)
        }
        private func stepEdit(_ point: CGPoint, view: UIView) {
            guard let id = model.primarySelection else { return }
            switch stageMode {
            case .move: stepMove(point, view: view, id: id)
            default: break
            }
        }
        private func startLayerPinch(_ a: StageTouchPoint, _ b: StageTouchPoint, view: UIView) {
            keepTransform(); pinchFingers = [a.id, b.id]; pinchLayer = model.primarySelection
            pinchTracker.start(a.position, b.position)
            previewBasis = model.primarySelection.map { model.engine.previewGestureBasis($0) } ?? []
            pinchThreeD = !previewBasis.isEmpty
            pinchCenter = CGPoint(x: (a.position.x+b.position.x)*0.5, y: (a.position.y+b.position.y)*0.5)
            pinchPointScale = scaleFactor(view); pinchPanActive = false
            pinchRotationActive = false; pinchRotationOffset = 0
            rotationSnap = StageMath.snapStep(startRotation.z, step: StageMath.rotStep, current: nil, enter: StageMath.rotEnter, exit: StageMath.rotExit)
            scaleSnap = StageMath.snapTarget(abs(startScale.x), target: 1, current: nil, enter: StageMath.scaleEnter, exit: StageMath.scaleExit)
            engage()
        }
        private func stepPinch(_ a: CGPoint, _ b: CGPoint) {
            guard let id = pinchLayer, id == model.primarySelection, editableSelection() else { return }
            if !previewBasis.isEmpty {
                let dx = (a.x+b.x)*0.5-pinchCenter.x, dy = (a.y+b.y)*0.5-pinchCenter.y
                if hypot(dx,dy) > 3 { pinchPanActive = true }
                if pinchPanActive {
                    beginEdit("pan 3D")
                    model.gizmoSetComponents(id, base: 0, values: model.engine.previewGestureValue(previewBasis,
                        dx: Float(dx)*pinchPointScale, dy: Float(dy)*pinchPointScale, rotate: false).map(\.floatValue))
                }
            }
            // Capture values outside the mutating tracker call (Swift exclusivity).
            let engine = model.engine, scale = startScale, threeD = pinchThreeD, kind = startKind
            guard pinchTracker.update(a, b, clamp: {
                threeD ? engine.clampPinchFactor3D($0, kind: kind, scaleX: scale.x, scaleY: scale.y, scaleZ: scale.z)
                    : engine.clampPinchFactor($0, scaleX: scale.x, scaleY: scale.y, scaleZ: scale.z, threeD: false)
            }) else { return }
            var f = pinchTracker.factor
            let degrees = pinchTracker.degrees
            if !pinchRotationActive && abs(degrees) > 4 { pinchRotationActive = true; pinchRotationOffset = degrees < 0 ? -4 : 4 }
            // Escala 100%: a escala X (em módulo) prende em 1 perto dele.
            let ref = abs(startScale.x)
            if ref > 0.0001 {
                let s = StageMath.snapTarget(ref * f, target: 1, current: scaleSnap, enter: StageMath.scaleEnter, exit: StageMath.scaleExit)
                if StageMath.snapEntered(scaleSnap, s) { snapFeedback.selectionChanged() }
                scaleSnap = s
                if s != nil { f = clampScale(1 / ref) }
            }
            beginEdit("pinça")
            // 3D: a profundidade efetiva acompanha (Z gravado é relativo a X no
            // conteúdo) — multiplicar Z aqui também esticava o volume (fator²).
            if pinchThreeD {
                model.gizmoSetComponents(id, base: 3, values: model.engine.gestureScale3D(startKind, scaleX: startScale.x, scaleY: startScale.y,
                                                                                         scaleZ: startScale.z, axis: 3, factor: f).map(\.floatValue))
            }
            else { model.setTransform2(3, startScale.x * f, 4, startScale.y * f, layer: id) }
            if pinchRotationActive {
                // Giro: prende nos múltiplos de 45° (0, 45, 90…) com um tique.
                var r = startRotation.z + degrees - pinchRotationOffset
                let s = StageMath.snapStep(r, step: StageMath.rotStep, current: rotationSnap, enter: StageMath.rotEnter, exit: StageMath.rotExit)
                if StageMath.snapEntered(rotationSnap, s) { snapFeedback.selectionChanged() }
                rotationSnap = s
                if let s { r = s }
                model.setTransform(8, value: r, layer: id)
            }
        }
        /// Mover em grupo: todas andam o MESMO delta da composição, cada uma no
        /// espaço do seu pai; o encaixe no centro usa o centro da caixa do grupo.
        private func stepGroupMove(_ point: CGPoint, view: UIView) {
            let c = compositionPoint(point, view: view)
            var delta = c - moveDown
            let tolerance = scaleFactor(view) * 6
            var snapX: Float?, snapY: Float?
            if let centre = groupCentre {
                if let snap = stageAnchorSnap(centre.x + delta.x, centre: Float(compositionSize.width / 2), tolerance: tolerance) { delta.x = snap - centre.x; snapX = snap }
                if let snap = stageAnchorSnap(centre.y + delta.y, centre: Float(compositionSize.height / 2), tolerance: tolerance) { delta.y = snap - centre.y; snapY = snap }
            }
            moveLast = c
            if (snapX != nil && snapX != shell.snapX) || (snapY != nil && snapY != shell.snapY) { snapFeedback.selectionChanged() }
            shell.snapX = snapX; shell.snapY = snapY
            beginEdit("mover")
            for mover in groupMove {
                let local = StageMath.moveInParent(mover.affine, local: mover.local, delta: delta)
                model.setTransform2(0, local.x, 1, local.y, layer: mover.id)
            }
        }
        private func stepMove(_ point: CGPoint, view: UIView, id: Int64) {
            if !previewBasis.isEmpty {
                let delta = compositionPoint(point, view: view) - moveDown
                beginEdit("rotate 3D")
                model.gizmoSetComponents(id, base: 6, values: model.engine.previewGestureValue(previewBasis,
                    dx: delta.x, dy: delta.y, rotate: true).map(\.floatValue))
                return
            }
            if !groupMove.isEmpty { stepGroupMove(point, view: view); return }
            let c = compositionPoint(point, view: view)
            // Absoluto desde o toque e LIVRE nos dois eixos (app antigo, Stage.kt
            // `move`): só as setas de eixo travam um lado.
            var next = moveWorld + c - moveDown
            if axisLock == 1 { next.y = moveWorld.y }; if axisLock == 2 { next.x = moveWorld.x }
            // Encaixe único: o ponto da camada (a âncora) no centro da composição,
            // a 6 pt de tela (Stage.kt `anchorSnap`).
            let tolerance = scaleFactor(view) * 6
            var snapX: Float?, snapY: Float?
            if axisLock != 2, let snap = stageAnchorSnap(next.x, centre: Float(compositionSize.width / 2), tolerance: tolerance) { next.x = snap; snapX = snap }
            if axisLock != 1, let snap = stageAnchorSnap(next.y, centre: Float(compositionSize.height / 2), tolerance: tolerance) { next.y = snap; snapY = snap }
            moveLast = c
            if (snapX != nil && snapX != shell.snapX) || (snapY != nil && snapY != shell.snapY) { snapFeedback.selectionChanged() }
            shell.snapX = snapX; shell.snapY = snapY
            let local = localPosition(next, affine: moveAffine)
            beginEdit("mover"); model.setTransform2(0, local.x, 1, local.y, layer: id)
        }
        /// Cena 3D "seca": tudo com o dedo na tela (par do `sceneGesture` e do
        /// `SceneGestureRules` do Android, bug 9 da 0.3.4).
        /// Sem seleção: 1 dedo orbita a vista (mesmo sobre um objeto), a pinça
        /// aproxima; tocar num objeto escolhe. Com seleção: 1 dedo no objeto o
        /// move, no vazio orbita; a pinça escala/torce o escolhido (a lupa do
        /// canto devolve a pinça à câmera). Tocar de novo no objeto escolhido
        /// solta a seleção; toque no vazio solta; toque duplo recentra. Um
        /// arrasto de objeto = UM passo de desfazer; a órbita é só da prévia.
        private func sceneEvent(_ pressed: [StageTouchPoint], view: UIView) {
            if pressed.isEmpty {
                guard sceneMode == 0 else { return }
                if let picked = scenePicked {
                    if model.primarySelection == picked && model.selection.count == 1 {
                        // Bug 9: o 2º toque no escolhido solta a seleção e a câmera volta.
                        model.clearSelection(); snapFeedback.selectionChanged()
                    } else {
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
                if sceneMode != 3 && sceneMode != 4 {
                    finishStageEdit()
                    // Bug 9: sem seleção a pinça é da câmera (não escolhe o objeto sob os dedos).
                    if sceneHadSelection, !StageViewZoom.shared.zoomLock, editableSelection(), let id = model.primarySelection,
                       !model.engine.previewGestureBasis(id).isEmpty {
                        sceneMode = 4; startLayerPinch(pressed[0], pressed[1], view: view)
                    } else { sceneMode = 3; sceneSpan = span; sceneDistance0 = model.sceneDistance }
                } else if sceneMode == 4 {
                    stepPinch(a, b)
                } else {
                    model.setSceneView(yaw: model.sceneYaw, pitch: model.scenePitch, distance: sceneDistance0 * Float(sceneSpan / span))
                }
                return
            }
            if sceneMode == 3 || sceneMode == 4 { return }   // sobrou um dedo da pinça: nada
            guard let first = pressed.first(where: { $0.id == stageFinger }) else { return }
            if sceneMode == 0 && hypot(first.position.x - stageDown.x, first.position.y - stageDown.y) > 12 {
                // Bug 9: sem seleção no toque, arrastar orbita (mesmo sobre um objeto).
                if sceneHadSelection, let picked = scenePicked {
                    if model.primarySelection != picked || model.selection.count != 1 { model.select(layerId: picked, additive: false) }
                    guard editableSelection() else { return }
                    keepTransform(); previewBasis = model.engine.previewGestureBasis(picked)
                    moveDown = compositionPoint(stageDown, view: view)
                    beginEdit("rotate 3D"); sceneMode = 2
                } else {
                    sceneMode = 1
                }
                sceneLast = stageDown   // a folga inteira entra: nada "pula" depois
            }
            let dx = first.position.x - sceneLast.x, dy = first.position.y - sceneLast.y
            switch sceneMode {
            case 1: model.setSceneView(yaw: model.sceneYaw - Float(dx) * 0.35, pitch: model.scenePitch + Float(dy) * 0.35, distance: model.sceneDistance)
            case 2: if let id = scenePicked, id == model.primarySelection {
                if previewBasis.isEmpty { let f = scaleFactor(view); model.sceneDragObject(dx: Float(dx)*f, dy: Float(dy)*f) }
                else { stepMove(first.position, view: view, id: id) }
            }
            default: break
            }
            if sceneMode != 0 { sceneLast = first.position }
        }
        /// Stage.kt gizmoGesture: o eixo tocado fica travado até o dedo subir.
        private func startGizmo(at point: CGPoint, view: UIView) -> Bool {
            guard editableSelection(), let id = model.primarySelection, !ShapeStageGeometry.enabled(model) else { return false }
            trackball?.end(); trackball = nil
            if TrackballOverlay.active(model) {
                trackball = TrackballSession(model: model, point: point) { screenPoint($0, $1, view: view) }
                return trackball != nil
            }
            let data = model.engine.gizmo(id, length: ShellStageGeometry.gizmoLength, localSpace: model.gizmoAxesLocal).map(\.floatValue)
            guard data.count == 8 else { return false }
            let raw = stride(from: 0, to: 8, by: 2).map { screenPoint(data[$0], data[$0 + 1], view: view) }
            let tips = ShellStageGeometry.gizmoTips(raw)
            let tool = model.gizmoTool
            var closest: CGFloat = 24
            for i in 1...3 {
                let distance = hypot(point.x - tips[i].x, point.y - tips[i].y)
                if distance < closest { closest = distance; gizmoAxis = i - 1 }
            }
            // Escala: o quadrado do centro escala X, Y e Z juntos.
            if gizmoAxis < 0 && tool == 2 && hypot(point.x - tips[0].x, point.y - tips[0].y) < 20 { gizmoAxis = 3 }
            guard gizmoAxis >= 0 else { return false }
            gizmoTool = tool
            gizmoOrigin = tips[0]
            if gizmoAxis < 3 {
                // Fixed-size display handles must not change world-space drag gain.
                gizmoVector = CGPoint(x: raw[gizmoAxis + 1].x - raw[0].x, y: raw[gizmoAxis + 1].y - raw[0].y)
                gizmoHandle = CGPoint(x: tips[gizmoAxis + 1].x - tips[0].x, y: tips[gizmoAxis + 1].y - tips[0].y)
            } else {
                gizmoVector = .zero
                gizmoHandle = .zero
            }
            let extent = (1...3).map { hypot(raw[$0].x - raw[0].x, raw[$0].y - raw[0].y) }.max() ?? 0
            gizmoFacing = gizmoAxis < 3 && hypot(gizmoVector.x, gizmoVector.y) * 80 / max(extent, 0.0001) < 44 * 0.6
            gizmoCollapsed = gizmoAxis == 2 && gizmoFacing
            let key = tool == 1 ? "rotation" : tool == 2 ? "scale" : "position"
            gizmoBase = vector(model.detail[key])
            gizmoSwept = 0; gizmoAlong = 0; gizmoMoved = false
            gizmoLastAngle = atan2(point.y - tips[0].y, point.x - tips[0].x)
            gizmoDownTime = CACurrentMediaTime()
            lastGizmoPoint = point
            return true
        }
        private func stepGizmo(_ point: CGPoint, view: UIView) {
            if let trackball {
                trackball.step(point, model: model) { label in engage(); beginEdit(label) }
                return
            }
            guard let id = model.primarySelection else { return }
            if !gizmoMoved {
                // Folga de alça (4 pt): um toque parado nunca vira edição.
                guard hypot(point.x - stageDown.x, point.y - stageDown.y) >= 4 else { return }
                gizmoMoved = true
            }
            // O 1º passo conta desde o toque: a alça alcança o dedo sem perder a folga.
            let dx = point.x - lastGizmoPoint.x, dy = point.y - lastGizmoPoint.y
            lastGizmoPoint = point
            guard dx != 0 || dy != 0 else { return }
            let axisName = gizmoAxis < 3 ? ["X", "Y", "Z"][gizmoAxis] : "XYZ"
            var out: [Float] = [gizmoBase.x, gizmoBase.y, gizmoBase.z]
            switch gizmoTool {
            case 1:
                let length = hypot(gizmoVector.x, gizmoVector.y)
                if gizmoFacing || length <= 1 {
                    let angle = atan2(point.y - gizmoOrigin.y, point.x - gizmoOrigin.x)
                    var delta = angle - gizmoLastAngle
                    while delta > .pi { delta -= 2 * .pi }
                    while delta < -.pi { delta += 2 * .pi }
                    gizmoSwept += delta * 180 / .pi
                    gizmoLastAngle = angle
                } else {
                    gizmoSwept += (-dx * gizmoVector.y + dy * gizmoVector.x) / length * 0.5
                }
                out[gizmoAxis] = gizmoBase[gizmoAxis] + Float(gizmoSwept)
                beginEdit("girar no eixo \(axisName)")
                model.gizmoSetComponents(id, base: 6, values: out)
            case 2:
                let handleLength: CGFloat = max(hypot(gizmoHandle.x, gizmoHandle.y), 1)
                if gizmoAxis == 3 { gizmoAlong += dx - dy }
                else if gizmoFacing { gizmoAlong += -dy }
                else { gizmoAlong += (dx * gizmoHandle.x + dy * gizmoHandle.y) / handleLength }
                // A regra de profundidade do motor (Z de conteúdo acompanha X):
                // o volume não estica (GestureMath.hpp).
                let factor: Float = gizmoAxis == 3 ? Float(exp(Double(gizmoAlong) / 120)) : max(0.01, 1 + Float(gizmoAlong / handleLength))
                out = model.engine.gestureScale3D(Int32(StageGeom.layerKind(model.detail)), scaleX: gizmoBase.x, scaleY: gizmoBase.y,
                                                  scaleZ: gizmoBase.z, axis: Int32(gizmoAxis), factor: factor).map { gizmoScale($0.floatValue) }
                beginEdit(gizmoAxis == 3 ? "escala uniforme" : "escala no eixo \(axisName)")
                model.gizmoSetComponents(id, base: 3, values: out)
            default:
                let length2 = gizmoVector.x * gizmoVector.x + gizmoVector.y * gizmoVector.y
                let amount: Float = gizmoCollapsed ? -Float(dy) * scaleFactor(view) * 2 : length2 > 1 ? Float((dx * gizmoVector.x + dy * gizmoVector.y) / length2) * ShellStageGeometry.gizmoLength : 0
                guard amount != 0 else { return }
                beginEdit("mover no eixo \(axisName)")
                let next = model.engine.gizmoMoveLocal(id, axis: UInt32(gizmoAxis + (model.gizmoLocalSpace ? 3 : 0)), amount: amount).map(\.floatValue)
                model.applyGizmoPosition(id, next)
            }
        }
        /// Escala do gizmo: mantém o sinal (espelho) e |escala| em [0,001; 100].
        private func gizmoScale(_ value: Float) -> Float {
            guard value.isFinite else { return 1 }
            let sign: Float = value < 0 ? -1 : 1
            return sign * min(100, max(0.001, abs(value)))
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
            // Omega conteúdo (área 06, P0-6): "manter proporção" é a chave da barra da seção (salva no aparelho).
            shapeLinked = ContentPrefs.shared.lockAspect; shapeLastU = 0; shapeLastV = 0; shapeSent = false
            shell.grabbedShapeHandle = shapeHandle
            beginEdit(shapeHandle == 8 ? "raio da forma" : "tamanho da forma"); return true
        }
        private func stepShape(_ point: CGPoint, view: UIView) {
            guard let id = model.primarySelection else { return }
            let f = scaleFactor(view), dx = Float(point.x - stageDown.x) * f, dy = Float(point.y - stageDown.y) * f
            let du = (dx * shapeU.x + dy * shapeU.y) / shapeScale.x, dv = (dx * shapeV.x + dy * shapeV.y) / shapeScale.y
            if shapeHandle == 8 {
                _ = model.engine.editShape(id, param: 1, value: (shapeRadius + (du + dv) / 2).clamped(to: 0...(min(shapeWidth, shapeHeight) / 2)), continuing: false)
            } else {
                // Omega conteúdo (área 06, P0-6): a regra (a partir do centro ou da borda oposta,
                // manter proporção) é do motor; vai o passo desde o último envio, em px da forma.
                _ = model.engine.resizeShapeHandle(id, handle: UInt32(shapeHandle), du: du - shapeLastU, dv: dv - shapeLastV,
                                                   keepRatio: shapeLinked, fromCenter: ContentPrefs.shared.fromCenter, continuing: shapeSent)
                shapeLastU = du; shapeLastV = dv; shapeSent = true
            }
            model.refreshSelectedLayer(); model.invalidatePreview()
        }

        /// Alças brancas de esticar (LayerResizeStage.kt): canto = fator uniforme
        /// pela reta âncora→canto; lado = só aquele eixo, pelo eixo da camada.
        /// Em volta da âncora, como a pinça; o arrasto = um passo de desfazer.
        private func startResize(at point: CGPoint, view: UIView) -> Bool {
            guard editableSelection(), let anchor = model.previewMarkerAnchor, let id = model.primarySelection,
                  model.engine.gizmo(id, length: ShellStageGeometry.gizmoLength).isEmpty else { return false }
            // A âncora e as setas de eixo ganham empate.
            let a = screenPoint(anchor.x, anchor.y, view: view)
            if hypot(point.x - a.x, point.y - a.y) <= 20 { return false }
            for tip in ShellStageGeometry.axisHandles(a, size: view.bounds.size)
                where hypot(point.x - tip.x, point.y - tip.y) <= AureaDims.axisHandleTarget { return false }
            let points = LayerResizeGeometry.handles(screenCorners(view: view))
            var closest: CGFloat = 22, hit = -1
            for i in points.indices {
                let distance = hypot(point.x - points[i].x, point.y - points[i].y)
                if distance < closest { closest = distance; hit = i }
            }
            guard hit >= 0 else { return false }
            var c: [Float] = []; guard StageGeom.corners(model.detail, &c) else { return false }
            let h: SIMD2<Float>
            if hit < 4 { h = SIMD2(c[hit * 2], c[hit * 2 + 1]) }
            else { let i = hit - 4, j = (i + 1) % 4; h = SIMD2((c[i * 2] + c[j * 2]) / 2, (c[i * 2 + 1] + c[j * 2 + 1]) / 2) }
            var e = hit == 5 || hit == 7 ? SIMD2(c[2] - c[0], c[3] - c[1]) : hit == 4 || hit == 6 ? SIMD2(c[6] - c[0], c[7] - c[1]) : h - anchor
            let length = hypot(e.x, e.y); guard length > 0.0001 else { return false }
            e /= length
            let base = (h.x - anchor.x) * e.x + (h.y - anchor.y) * e.y
            // Âncora em cima da alça: não há o que esticar.
            guard abs(base) / max(scaleFactor(view), 0.0001) >= 8 else { return false }
            keepTransform()
            resizeHandle = hit; resizeAnchor = anchor; resizeStart = h; resizeAxis = e; resizeBase = base
            shell.grabbedResizeHandle = hit
            engage(); beginEdit(hit >= 4 ? "esticar camada" : "escalar camada")
            return true
        }
        private func stepResize(_ point: CGPoint, view: UIView) {
            guard let id = model.primarySelection, resizeHandle >= 0, editableSelection() else { return }
            let f = scaleFactor(view)
            let p = resizeStart + SIMD2(Float(point.x - stageDown.x) * f, Float(point.y - stageDown.y) * f)
            let raw = ((p.x - resizeAnchor.x) * resizeAxis.x + (p.y - resizeAnchor.y) * resizeAxis.y) / resizeBase
            let k = model.engine.clampPinchFactor(raw, scaleX: startScale.x, scaleY: startScale.y, scaleZ: startScale.z, threeD: false)
            switch resizeHandle {
            case 5, 7: model.setTransform(3, value: startScale.x * k, layer: id)
            case 4, 6: model.setTransform(4, value: startScale.y * k, layer: id)
            default: model.setTransform2(3, startScale.x * k, 4, startScale.y * k, layer: id)
            }
        }

        @objc func handlePan(_ gesture: UIPanGestureRecognizer) {
            guard interactive, model.pointPick == nil, !model.focusPick, let view = gesture.view, model.primarySelection != nil, !(model.selectedLayer?.locked ?? false) else { return }
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

/// Encaixe do mover (app antigo; Stage.kt `anchorSnap`): `pos` vira `centre`
/// quando está a menos de `tolerance`; sem filtro de velocidade.
func stageAnchorSnap(_ pos: Float, centre: Float, tolerance: Float) -> Float? {
    tolerance > 0 && abs(pos - centre) < tolerance ? centre : nil
}

/// Contas puras dos gestos do palco (espelho de StageMath.kt, testado no JVM):
/// mover várias camadas juntas e os encaixes da pinça.
enum StageMath {
    /// Giro: encaixa a cada 45°, entra a menos de 4° e só solta além de 6°.
    static let rotStep: Float = 45, rotEnter: Float = 4, rotExit: Float = 6
    /// Escala: encaixa em 100% a menos de 3%, solta além de 4,5%.
    static let scaleEnter: Float = 0.03, scaleExit: Float = 0.045

    /// Caixa que abraça todos os cantos (x, y, x, y… em px da composição).
    static func unionBox(_ sets: [[Float]]) -> (minX: Float, minY: Float, maxX: Float, maxY: Float)? {
        var x0 = Float.infinity, y0 = Float.infinity, x1 = -Float.infinity, y1 = -Float.infinity
        for c in sets {
            var i = 0
            while i + 1 < c.count {
                let x = c[i], y = c[i + 1]
                if x.isFinite && y.isFinite { x0 = min(x0, x); y0 = min(y0, y); x1 = max(x1, x); y1 = max(y1, y) }
                i += 2
            }
        }
        return x0 <= x1 && y0 <= y1 ? (x0, y0, x1, y1) : nil
    }

    /// Anda `delta` px da COMPOSIÇÃO uma camada de posição `local` no espaço do
    /// pai (`affine` = pai → composição, a b c d tx ty). Pai sem inversa: a
    /// composição vale como local.
    static func moveInParent(_ affine: [Float], local: SIMD2<Float>, delta: SIMD2<Float>) -> SIMD2<Float> {
        let a: [Float] = affine.count >= 6 ? affine : [1, 0, 0, 1, 0, 0]
        let wx = a[0] * local.x + a[2] * local.y + a[4] + delta.x
        let wy = a[1] * local.x + a[3] * local.y + a[5] + delta.y
        let det = a[0] * a[3] - a[2] * a[1]
        guard abs(det) >= 1e-9 else { return SIMD2(wx, wy) }
        let rx = wx - a[4], ry = wy - a[5]
        return SIMD2((a[3] * rx - a[2] * ry) / det, (-a[1] * rx + a[0] * ry) / det)
    }

    /// Tira quem tem um ancestral (qualquer nível) também no conjunto: o pai já
    /// leva o filho. `parentOf` devolve 0 sem pai. Mantém a ordem.
    static func moveRoots(_ ids: [Int64], parentOf: (Int64) -> Int64) -> [Int64] {
        let set = Set(ids)
        return ids.filter { id in
            var p = parentOf(id), depth = 0
            while p != 0 && p != id && depth < 64 {
                if set.contains(p) { return false }
                p = parentOf(p); depth += 1
            }
            return true
        }
    }

    /// Encaixe em múltiplos de `step` com histerese: preso em `current`
    /// enquanto a até `exit` dele; senão prende no múltiplo mais próximo a
    /// menos de `enter`. nil = solto.
    static func snapStep(_ value: Float, step: Float, current: Float?, enter: Float, exit: Float) -> Float? {
        guard value.isFinite, step > 0 else { return nil }
        if let current, abs(value - current) <= exit { return current }
        let k = (value / step).rounded() * step
        return abs(value - k) < enter ? k : nil
    }

    static func snapTarget(_ value: Float, target: Float, current: Float?, enter: Float, exit: Float) -> Float? {
        guard value.isFinite else { return nil }
        if let current, abs(value - current) <= exit { return current }
        return abs(value - target) < enter ? target : nil
    }

    /// Entrou num encaixe (ou trocou de alvo): hora do tique.
    static func snapEntered(_ previous: Float?, _ next: Float?) -> Bool { next != nil && next != previous }
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

/// Alças brancas de esticar da camada 2D (Android LayerResizeStage.kt).
/// Índices: 0 TL, 1 TR, 2 BR, 3 BL, 4 cima, 5 direita, 6 baixo, 7 esquerda.
/// Camada pequena na tela não ganha alças: o disco roubava o arrasto de mover.
enum LayerResizeGeometry {
    static let minSide: CGFloat = 96
    static func handles(_ c: [CGPoint]) -> [CGPoint] {
        guard c.count == 4 else { return [] }
        let lengthU = hypot(c[1].x - c[0].x, c[1].y - c[0].y), lengthV = hypot(c[3].x - c[0].x, c[3].y - c[0].y)
        guard min(lengthU, lengthV) >= minSide else { return [] }
        var result = c
        for i in 0..<4 { let j = (i + 1) % 4; result.append(CGPoint(x: (c[i].x + c[j].x) / 2, y: (c[i].y + c[j].y) / 2)) }
        return result
    }
}

@MainActor struct StageInteractionOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    @ObservedObject private var viewZoom = StageViewZoom.shared
    @ObservedObject private var modes = ViewModeState.shared
    var body: some View {
        Canvas { context, size in
            let cw = CGFloat(max(1, model.compositionWidth)), ch = CGFloat(max(1, model.compositionHeight))
            let placed = StageZoomMath.fit(size: size, composition: CGSize(width: cw, height: ch), zoom: viewZoom.zoom, pan: viewZoom.pan)
            let fit = placed.scale, ox = placed.origin.x, oy = placed.origin.y
            func screen(_ x: Float, _ y: Float) -> CGPoint { CGPoint(x: CGFloat(x) * fit + ox, y: CGFloat(y) * fit + oy) }
            // Omega chrome (área 02, P1-2): o tamanho da composição em 1× vai para os
            // degraus de zoom da coluna; a grade de ESPAÇAMENTO (px da composição, a
            // partir do centro) + a cruz do centro, ligada pela coluna de modos.
            let base = fit / max(StageZoomMath.minZoom, viewZoom.zoom)
            viewZoom.noteExtent(CGSize(width: cw * base, height: ch * base), pixelScale: UIScreen.main.scale)
            if modes.grid {
                let ink = AureaColors.text.opacity(0.35)
                let step = CGFloat(max(1, modes.gridSpacing))
                var grid = Path()
                // Linhas a menos de 4 pt na tela viram borrão: só a cruz do centro.
                if step * fit >= 4 {
                    var d = step
                    while d <= cw / 2 {
                        for gx in [cw / 2 - d, cw / 2 + d] {
                            grid.move(to: screen(Float(gx), 0)); grid.addLine(to: screen(Float(gx), Float(ch)))
                        }
                        d += step
                    }
                    d = step
                    while d <= ch / 2 {
                        for gy in [ch / 2 - d, ch / 2 + d] {
                            grid.move(to: screen(0, Float(gy))); grid.addLine(to: screen(Float(cw), Float(gy)))
                        }
                        d += step
                    }
                }
                context.stroke(grid, with: .color(ink), lineWidth: 1)
                var cross = Path()
                let c = screen(Float(cw) / 2, Float(ch) / 2)
                cross.move(to: CGPoint(x: c.x - 10, y: c.y)); cross.addLine(to: CGPoint(x: c.x + 10, y: c.y))
                cross.move(to: CGPoint(x: c.x, y: c.y - 10)); cross.addLine(to: CGPoint(x: c.x, y: c.y + 10))
                context.stroke(cross, with: .color(AureaColors.text.opacity(0.6)), lineWidth: 1.5)
            }
            if let x = shell.snapX {
                var p = Path(); p.move(to: screen(x, 0)); p.addLine(to: screen(x, Float(ch)))
                context.stroke(p, with: .color(Color(hex: 0xFF6B6B).opacity(0.8)), lineWidth: 1.5)
            }
            if let y = shell.snapY {
                var p = Path(); p.move(to: screen(0, y)); p.addLine(to: screen(Float(cw), y))
                context.stroke(p, with: .color(Color(hex: 0xFF6B6B).opacity(0.8)), lineWidth: 1.5)
            }
            // Omega aparência: guias do ímã do trackpad (Mover) e trajetória da posição
            // (OmegaTransformStage, TrackballOverlay.swift).
            OmegaTransformStage.draw(&context, model: model, width: Float(cw), height: Float(ch), screen: screen)
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
        // Omega aparência (P1-1/P1-4): mira e lupa do conta-gotas, alças das pontas do degradê.
        .overlay { OmegaAppearanceStageOverlay() }
    }
}

// =============================================================================
// Omega aparência (área 05): conta-gotas AO VIVO (P1-1) e pontas do degradê
// (P1-4) no palco. Par do bloco "Omega aparência" do Stage.kt.
//
// • Conta-gotas (`model.eyedropper`, ligado pelo seletor): arrastar em
//   QUALQUER lugar do palco anda a mira RELATIVA (EyedropperStage), que amostra
//   do quadro capturado no começo de cada arrasto; soltar aplica. Continua
//   ligado até o seletor desligar.
// • Pontas do degradê (`FillStageState.gradientHandles`, ligado pelo painel Cor
//   e preenchimento com o Degradê aberto): o toque pega a ponta mais perto,
//   passa do "touch slop" e arrasta (relativo à caixa da camada) num passo de
//   desfazer (grupos 3/4 do preenchimento).
// =============================================================================

/// Uma sequência de toques que o conta-gotas ou as pontas do degradê pegaram,
/// do primeiro dedo até soltar todos.
@MainActor final class OmegaAppearanceStageGesture {
    enum Mode { case idle, eyedropper, gradient, drain }
    var mode: Mode = .idle
    var finger: ObjectIdentifier?
    var last = CGPoint.zero
    var down = CGPoint.zero
    var handle = -1
    var startRel = SIMD2<Float>(0, 0)
    var basisU = SIMD2<Float>(1, 0)
    var basisV = SIMD2<Float>(0, 1)
    var began = false
    /// "Touch slop": abaixo disto o toque não arrasta a ponta.
    static let slop: CGFloat = 8
}

/// A caixa da camada como base: ponto relativo (0..1) ↔ px da composição.
enum OmegaFillGeometry {
    struct Basis {
        let origin: SIMD2<Float>
        let u: SIMD2<Float>
        let v: SIMD2<Float>
        func point(_ rel: SIMD2<Float>) -> SIMD2<Float> { origin + u * rel.x + v * rel.y }
        /// Δ em px da composição → Δ relativo à caixa (nil = caixa achatada).
        func relative(_ delta: SIMD2<Float>) -> SIMD2<Float>? {
            let det = u.x * v.y - v.x * u.y
            guard abs(det) > 1e-6 else { return nil }
            return SIMD2((delta.x * v.y - v.x * delta.y) / det, (u.x * delta.y - delta.x * u.y) / det)
        }
    }

    /// Cantos TL, TR, BR, BL do detalhe: origem TL, u = TR − TL, v = BL − TL.
    static func basis(_ detail: [String: Any]) -> Basis? {
        var c: [Float] = []
        guard StageGeom.corners(detail, &c), c.count == 8 else { return nil }
        let origin = SIMD2(c[0], c[1])
        return Basis(origin: origin, u: SIMD2(c[2], c[3]) - origin, v: SIMD2(c[6], c[7]) - origin)
    }

    /// As pontas (início, fim) relativas à caixa, do preenchimento no cabeçote.
    static func ends(_ fill: [Float]) -> [SIMD2<Float>]? {
        guard fill.count >= 24 else { return nil }
        return [SIMD2(fill[ShapeFillIndex.start], fill[ShapeFillIndex.start + 1]),
                SIMD2(fill[ShapeFillIndex.end], fill[ShapeFillIndex.end + 1])]
    }
}

extension PreviewMetalView.Coordinator {
    /// true = esta sequência de toques é do conta-gotas ou das pontas do degradê.
    func omegaAppearanceEvent(_ points: [StageTouchPoint], cancelled: Bool, view: UIView) -> Bool {
        let s = omegaAppearance
        let pressed = points.filter(\.pressed)
        switch s.mode {
        case .idle:
            guard !cancelled, interactive, stageFinger == nil, let first = pressed.first else { return false }
            if model.eyedropper != nil {
                if model.status.playing != 0 { model.playPause() }
                s.mode = .eyedropper
                s.finger = first.id
                s.last = first.position
                EyedropperStage.shared.begin(model, composition: compositionSize)
                return true
            }
            if FillStageState.shared.gradientHandles, editableSelection(), startGradientHandle(first, view: view) {
                return true
            }
            return false
        case .drain:
            if cancelled || pressed.isEmpty { s.mode = .idle }
            return true
        case .eyedropper:
            if cancelled {
                EyedropperStage.shared.cancel(model)
                s.mode = .idle
                return true
            }
            guard let finger = pressed.first(where: { $0.id == s.finger }) else {
                EyedropperStage.shared.end(model)
                s.mode = pressed.isEmpty ? .idle : .drain
                return true
            }
            let f = CGFloat(scaleFactor(view))
            EyedropperStage.shared.move((finger.position.x - s.last.x) * f, (finger.position.y - s.last.y) * f, model: model)
            s.last = finger.position
            return true
        case .gradient:
            if cancelled {
                finishGradientHandle()
                s.mode = .idle
                return true
            }
            guard let finger = pressed.first(where: { $0.id == s.finger }) else {
                finishGradientHandle()
                s.mode = pressed.isEmpty ? .idle : .drain
                return true
            }
            stepGradientHandle(finger.position, view: view)
            return true
        }
    }

    /// Escolhe a ponta mais perto do toque (na tela).
    private func startGradientHandle(_ first: StageTouchPoint, view: UIView) -> Bool {
        let fill = model.shapeFill
        guard fill.count >= 24, fill[ShapeFillIndex.supported] > 0.5, fill[ShapeFillIndex.kind] > 0.5,
              let basis = OmegaFillGeometry.basis(model.detail), let ends = OmegaFillGeometry.ends(fill) else { return false }
        let screens = ends.map { rel -> CGPoint in
            let c = basis.point(rel)
            return screenPoint(c.x, c.y, view: view)
        }
        let d0 = hypot(first.position.x - screens[0].x, first.position.y - screens[0].y)
        let d1 = hypot(first.position.x - screens[1].x, first.position.y - screens[1].y)
        let s = omegaAppearance
        s.handle = d1 < d0 ? 1 : 0
        s.startRel = ends[s.handle]
        s.basisU = basis.u
        s.basisV = basis.v
        s.mode = .gradient
        s.finger = first.id
        s.down = first.position
        s.began = false
        return true
    }

    private func stepGradientHandle(_ point: CGPoint, view: UIView) {
        let s = omegaAppearance
        if !s.began {
            guard hypot(point.x - s.down.x, point.y - s.down.y) > OmegaAppearanceStageGesture.slop,
                  editableSelection() else { return }
            if model.status.playing != 0 { model.playPause() }
            model.beginGesture("pontas do degradê")
            s.began = true
            FillStageState.shared.grabbed = s.handle
        }
        let f = scaleFactor(view)
        let delta = SIMD2<Float>(Float(point.x - s.down.x) * f, Float(point.y - s.down.y) * f)
        let basis = OmegaFillGeometry.Basis(origin: SIMD2<Float>(0, 0), u: s.basisU, v: s.basisV)
        guard let rel = basis.relative(delta) else { return }
        let next = s.startRel + rel
        model.setShapeFillValue(s.handle == 0 ? ShapeFillIndex.groupStartPoint : ShapeFillIndex.groupEndPoint, [next.x, next.y])
    }

    private func finishGradientHandle() {
        let s = omegaAppearance
        if s.began { model.endGesture() }
        s.began = false
        s.handle = -1
        FillStageState.shared.grabbed = -1
    }
}

/// O desenho do conta-gotas (mira, lupa e a faixa de dica no topo do palco) e
/// das alças do degradê. Sem toque (o palco por baixo é quem trata o gesto);
/// ids de acessibilidade `stage.eyedropper` e `stage.gradient.<start|end>`.
@MainActor struct OmegaAppearanceStageOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var viewZoom = StageViewZoom.shared
    @ObservedObject private var dropper = EyedropperStage.shared
    @ObservedObject private var fillStage = FillStageState.shared

    private struct HandlePair {
        let start: CGPoint
        let end: CGPoint
        let c0: [Float]
        let c1: [Float]
    }

    var body: some View {
        GeometryReader { geo in
            let cw = CGFloat(max(1, model.compositionWidth)), ch = CGFloat(max(1, model.compositionHeight))
            let placed = StageZoomMath.fit(size: geo.size, composition: CGSize(width: cw, height: ch), zoom: viewZoom.zoom, pan: viewZoom.pan)
            let handles = gradientHandles(scale: placed.scale, origin: placed.origin)
            let cross = crosshair(scale: placed.scale, origin: placed.origin, cw: cw, ch: ch)
            ZStack(alignment: .topLeading) {
                Canvas { context, _ in
                    if let handles { drawHandles(&context, handles) }
                    if let cross { drawEyedropper(&context, cross, cw: cw, ch: ch) }
                }
                if let handles {
                    marker(handles.start, key: "om_ap_fill_grad_start", tag: "stage.gradient.start")
                    marker(handles.end, key: "om_ap_fill_grad_end", tag: "stage.gradient.end")
                }
                if let cross {
                    marker(cross, key: "om_ap_cp_eyedropper", tag: "stage.eyedropper")
                    Text(AureaText.t("om_ap_cp_eyedropper_hint"))
                        .font(.aurea(size: 12, weight: .semibold))
                        .foregroundStyle(AureaColors.text)
                        .lineLimit(1)
                        .padding(.horizontal, 12)
                        .padding(.vertical, 6)
                        .background(StageInk.resolutionChip, in: Capsule())
                        .frame(width: geo.size.width)
                        .padding(.top, 8)
                        .accessibilityIdentifier("stage.eyedropper.hint")
                }
            }
        }
    }

    private func gradientHandles(scale: CGFloat, origin: CGPoint) -> HandlePair? {
        let fill = model.shapeFill
        guard fillStage.gradientHandles, model.selection.count == 1, fill.count >= 24, fill[ShapeFillIndex.kind] > 0.5,
              let basis = OmegaFillGeometry.basis(model.detail), let ends = OmegaFillGeometry.ends(fill) else { return nil }
        let points = ends.map { rel -> CGPoint in
            let c = basis.point(rel)
            return CGPoint(x: CGFloat(c.x) * scale + origin.x, y: CGFloat(c.y) * scale + origin.y)
        }
        return HandlePair(start: points[0], end: points[1],
                          c0: Array(fill[ShapeFillIndex.color0..<(ShapeFillIndex.color0 + 4)]),
                          c1: Array(fill[ShapeFillIndex.color1..<(ShapeFillIndex.color1 + 4)]))
    }

    private func crosshair(scale: CGFloat, origin: CGPoint, cw: CGFloat, ch: CGFloat) -> CGPoint? {
        guard model.eyedropper != nil else { return nil }
        let p = dropper.crosshair ?? CGPoint(x: cw / 2, y: ch / 2)
        return CGPoint(x: p.x * scale + origin.x, y: p.y * scale + origin.y)
    }

    private func disk(_ p: CGPoint, _ r: CGFloat) -> Path {
        Path(ellipseIn: CGRect(x: p.x - r, y: p.y - r, width: 2 * r, height: 2 * r))
    }

    private func drawHandles(_ context: inout GraphicsContext, _ h: HandlePair) {
        var line = Path()
        line.move(to: h.start)
        line.addLine(to: h.end)
        context.stroke(line, with: .color(StageInk.outlineUnder), lineWidth: 3.5)
        context.stroke(line, with: .color(AureaColors.onImage), lineWidth: 1.5)
        for (index, point) in [h.start, h.end].enumerated() {
            let radius: CGFloat = fillStage.grabbed == index ? 12 : 10
            context.fill(disk(point, radius + 2), with: .color(StageInk.outlineUnder))
            context.fill(disk(point, radius), with: .color(AureaColors.onImage))
            context.fill(disk(point, radius - 3), with: .color(PickerColor.color(index == 0 ? h.c0 : h.c1)))
        }
    }

    /// A mira (sempre, com o conta-gotas ligado) e a lupa (durante o arrasto):
    /// ~11 pixels do quadro capturado ampliados, o pixel do centro marcado e o
    /// anel com a cor amostrada.
    private func drawEyedropper(_ context: inout GraphicsContext, _ p: CGPoint, cw: CGFloat, ch: CGFloat) {
        let ring = disk(p, 9)
        context.stroke(ring, with: .color(StageInk.outlineUnder), lineWidth: 4)
        context.stroke(ring, with: .color(AureaColors.onImage), lineWidth: 2)
        var cross = Path()
        cross.move(to: CGPoint(x: p.x - 16, y: p.y)); cross.addLine(to: CGPoint(x: p.x - 5, y: p.y))
        cross.move(to: CGPoint(x: p.x + 5, y: p.y)); cross.addLine(to: CGPoint(x: p.x + 16, y: p.y))
        cross.move(to: CGPoint(x: p.x, y: p.y - 16)); cross.addLine(to: CGPoint(x: p.x, y: p.y - 5))
        cross.move(to: CGPoint(x: p.x, y: p.y + 5)); cross.addLine(to: CGPoint(x: p.x, y: p.y + 16))
        context.stroke(cross, with: .color(StageInk.outlineUnder), lineWidth: 3.5)
        context.stroke(cross, with: .color(AureaColors.onImage), lineWidth: 1.5)
        guard dropper.dragging, let shot = dropper.shot, let point = dropper.crosshair,
              shot.width > 0, shot.height > 0 else { return }
        let r: CGFloat = 34, zoom = 2 * r / 11
        let px = (point.x / cw * CGFloat(shot.width)).rounded(.down)
        let py = (point.y / ch * CGFloat(shot.height)).rounded(.down)
        let above = p.y - r - 26
        let c = CGPoint(x: p.x, y: above >= r ? above : p.y + r + 26)
        var lens = context
        lens.clip(to: disk(c, r))
        lens.fill(disk(c, r), with: .color(StageInk.outlineUnder))
        let image = lens.resolve(Image(uiImage: shot.image).interpolation(.none))
        lens.draw(image, in: CGRect(x: c.x - (px + 0.5) * zoom, y: c.y - (py + 0.5) * zoom,
                                    width: CGFloat(shot.width) * zoom, height: CGFloat(shot.height) * zoom))
        lens.stroke(Path(CGRect(x: c.x - zoom / 2, y: c.y - zoom / 2, width: zoom, height: zoom)),
                    with: .color(AureaColors.onImage), lineWidth: 1.5)
        let sampled = dropper.color ?? [1, 1, 1, 1]
        context.stroke(disk(c, r - 2.5), with: .color(PickerColor.color(sampled)), lineWidth: 5)
        context.stroke(disk(c, r + 2.5), with: .color(AureaColors.onImage), lineWidth: 1.5)
    }

    private func marker(_ point: CGPoint, key: String, tag: String) -> some View {
        Color.clear
            .frame(width: 44, height: 44)
            .position(point)
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(AureaText.t(key))
            .accessibilityIdentifier(tag)
    }
}
