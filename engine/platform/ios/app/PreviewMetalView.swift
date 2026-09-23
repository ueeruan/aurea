// =============================================================================
//  Aurea / platform / ios / app / PreviewMetalView.swift
//
//  O preview. SwiftUI → UIViewRepresentable → `AureaMetalView` (UIView cujo
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
    /// O quadro da composição, para converter px da tela em px da composição.
    let compositionSize: CGSize
    /// Ligado no palco em tela cheia: não trata gestos (o transporte manda).
    let interactive: Bool

    func makeUIView(context: Context) -> AureaMetalView {
        let view = AureaMetalView()
        view.device = model.device
        view.engine = model.engine
        view.isPaused = false
        // Um arrasto na view move a camada escolhida; o gesto é reconhecido
        // aqui e traduzido em comandos no coordinator.
        if interactive {
            let pan = UIPanGestureRecognizer(target: context.coordinator,
                                            action: #selector(Coordinator.handlePan(_:)))
            pan.maximumNumberOfTouches = 1
            view.addGestureRecognizer(pan)
            let pinch = UIPinchGestureRecognizer(target: context.coordinator,
                                                action: #selector(Coordinator.handlePinch(_:)))
            view.addGestureRecognizer(pinch)
            let rotate = UIRotationGestureRecognizer(target: context.coordinator,
                                                     action: #selector(Coordinator.handleRotate(_:)))
            view.addGestureRecognizer(rotate)
            context.coordinator.pan = pan
            context.coordinator.pinch = pinch
            context.coordinator.rotate = rotate
        }
        return view
    }

    func updateUIView(_ view: AureaMetalView, context: Context) {
        // Reatribuir dispara o attach de novo quando o motor subiu DEPOIS de a
        // view existir (é o caso normal: a Home aparece antes do editor).
        view.device = model.device
        view.engine = model.engine
        context.coordinator.model = model
        context.coordinator.compositionSize = compositionSize
        context.coordinator.interactive = interactive
    }

    func makeCoordinator() -> Coordinator {
        Coordinator(model: model, compositionSize: compositionSize, interactive: interactive)
    }

    static func dismantleUIView(_ view: AureaMetalView, coordinator: Coordinator) {
        // A view desanexa a superfície no `deinit`/`didMoveToWindow`, mas o
        // caminho explícito aqui garante a ordem quando a tela é trocada.
        view.paused = true
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
        var compositionSize: CGSize
        var interactive: Bool
        weak var pan: UIPanGestureRecognizer?
        weak var pinch: UIPinchGestureRecognizer?
        weak var rotate: UIRotationGestureRecognizer?

        // Estado do gesto em curso. A posição/ângulo/escala de PARTIDA é lida do
        // motor no começo do arrasto (o `detail`), nunca acumulada no Swift —
        // acumular erraria a cada toque e divergiria do valor real.
        private var startPosition: SIMD3<Float> = .zero
        private var startScale: SIMD3<Float> = .one
        private var startRotation: SIMD3<Float> = .zero

        init(model: AureaModel, compositionSize: CGSize, interactive: Bool) {
            self.model = model
            self.compositionSize = compositionSize
            self.interactive = interactive
        }

        /// px da tela → px da composição (a razão entre o quadro da composição e
        /// o tamanho do palco). O motor trabalha em px da composição.
        private func scaleFactor(_ view: UIView) -> Float {
            let width = max(1, view.bounds.width)
            let height = max(1, view.bounds.height)
            let compWidth = max(1, compositionSize.width)
            let compHeight = max(1, compositionSize.height)
            return Float(min(compWidth / width, compHeight / height))
        }

        /// Lê um vetor do detalhe da camada. O detalhe vem do MOTOR no instante
        /// do playhead (`LayerDetailPOD`): é o valor real, com a animação já
        /// aplicada — o Swift nunca guarda uma cópia própria.
        private func vector(_ value: Any?) -> SIMD3<Float> {
            guard let numbers = value as? [NSNumber] else { return .zero }
            var out = SIMD3<Float>.zero
            if numbers.count > 0 { out.x = numbers[0].floatValue }
            if numbers.count > 1 { out.y = numbers[1].floatValue }
            if numbers.count > 2 { out.z = numbers[2].floatValue }
            return out
        }

        @objc func handlePan(_ gesture: UIPanGestureRecognizer) {
            guard interactive, let view = gesture.view, let layerId = model.primarySelection else { return }
            guard !(model.selectedLayer?.locked ?? false) else { return }
            let factor = scaleFactor(view)
            switch gesture.state {
            case .began:
                model.refreshSelectedLayer()
                startPosition = vector(model.detail["position"])
                model.engine.beginBatch()
            case .changed:
                let delta = gesture.translation(in: view)
                let position = SIMD3<Float>(startPosition.x + Float(delta.x) * factor,
                                            startPosition.y + Float(delta.y) * factor,
                                            startPosition.z)
                model.engine.setPosition(forLayer: layerId, x: position.x, y: position.y, z: position.z)
            case .ended, .cancelled, .failed:
                _ = model.engine.flush()
                model.refreshSelectedLayer()
            default:
                break
            }
        }

        @objc func handlePinch(_ gesture: UIPinchGestureRecognizer) {
            guard interactive, let layerId = model.primarySelection else { return }
            guard !(model.selectedLayer?.locked ?? false) else { return }
            switch gesture.state {
            case .began:
                model.refreshSelectedLayer()
                startScale = vector(model.detail["scale"])
                model.engine.beginBatch()
            case .changed:
                let k = Float(gesture.scale)
                model.engine.setScale(forLayer: layerId,
                                      x: startScale.x * k, y: startScale.y * k, z: startScale.z)
            case .ended, .cancelled, .failed:
                _ = model.engine.flush()
                model.refreshSelectedLayer()
            default:
                break
            }
        }

        @objc func handleRotate(_ gesture: UIRotationGestureRecognizer) {
            guard interactive, let layerId = model.primarySelection else { return }
            guard !(model.selectedLayer?.locked ?? false) else { return }
            switch gesture.state {
            case .began:
                model.refreshSelectedLayer()
                startRotation = vector(model.detail["rotation"])
                baseRotationDelta = 0
                model.engine.beginBatch()
            case .changed:
                // O gesto do iOS é em radianos e o motor trabalha em GRAUS.
                let degrees = Float(gesture.rotation) * 180.0 / .pi
                model.engine.setRotation(forLayer: layerId,
                                         x: startRotation.x, y: startRotation.y,
                                         z: startRotation.z + degrees)
            case .ended, .cancelled, .failed:
                _ = model.engine.flush()
                model.refreshSelectedLayer()
            default:
                break
            }
        }
    }
}
