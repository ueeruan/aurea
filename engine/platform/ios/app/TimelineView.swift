// =============================================================================
//  Aurea / platform / ios / app / TimelineView.swift
//
//  A timeline: régua de tempo, linhas de camada, playhead e os keyframes.
//
//  DESENHADA COM `Canvas`, não com N subviews: uma composição de 40 camadas com
//  centenas de keyframes viraria 400 views e o SwiftUI engasgaria num arrasto —
//  o mesmo motivo pelo qual o Android desenha a timeline com um `Canvas` do
//  Compose (`TimelinePainter.kt`).
//
//  SNAPSHOT: o `Canvas` recebe um `TimelineSnapshot` de VALORES, montado antes
//  do desenho. O desenho não toca no modelo (nem no motor) — o que a timeline
//  mostra é a leitura de um instante, e a próxima revisão do motor produz um
//  snapshot novo. Isso também é o que mantém o desenho barato: nenhuma consulta
//  ao motor acontece dentro do `Canvas`.
//
//  O QUE VEM DO MOTOR: as linhas (`Engine::query_layers`), os keyframes
//  (`query_all_keyframes`) e o instante (`fill_status`). O Swift não guarda
//  cópia do modelo: relê quando `modelRevision` muda.
// =============================================================================
import SwiftUI

/// Tudo que o desenho precisa, em valores. Imutável.
struct TimelineSnapshot: Equatable {
    struct Row: Equatable {
        var id: Int64
        var name: String
        var kind: UInt32
        var start: Int32
        var end: Int32
        var visible: Bool
        var locked: Bool
        var threeD: Bool
        var selected: Bool
        var keys: [Int32]      ///< keyframes de transform, tempo LOCAL da camada
    }

    var rows: [Row] = []
    var playhead: Int64 = 0
    var duration: Int64 = 0
    var fps: Double = 30
    var pointsPerFrame: CGFloat = 8
    var origin: Int64 = 0
}

struct TimelineView: View {
    @EnvironmentObject private var model: AureaModel

    /// Escala horizontal: pontos por quadro. O zoom é do app (a régua é
    /// desenhada, não rolada), como no Android.
    @State private var pointsPerFrame: CGFloat = 8
    @State private var origin: Int64 = 0
    @State private var scrubbing = false
    @State private var zoomBase: CGFloat = 8

    private let rulerHeight: CGFloat = 22
    private let rowHeight: CGFloat = 28

    // =========================================================================
    // O snapshot do instante
    // =========================================================================
    private var snapshot: TimelineSnapshot {
        var snap = TimelineSnapshot()
        snap.rows = model.layers.map { layer in
            let keys = (model.keyframes[layer.id] ?? [])
                .filter { $0.effectIndex == 0xFFFF_FFFF }   // keyframe de efeito vive no painel
                .map(\.time)
            return TimelineSnapshot.Row(id: layer.id, name: layer.name, kind: layer.kind,
                                        start: layer.startFrame, end: layer.endFrame,
                                        visible: layer.visible, locked: layer.locked,
                                        threeD: layer.threeD, selected: model.selection.contains(layer.id),
                                        keys: keys)
        }
        snap.playhead = model.status.playhead
        snap.duration = model.compositionDuration
        snap.fps = model.compositionFps
        snap.pointsPerFrame = pointsPerFrame
        snap.origin = origin
        return snap
    }

    func x(forFrame frame: Int64, _ snap: TimelineSnapshot) -> CGFloat {
        CGFloat(frame - snap.origin) * snap.pointsPerFrame
    }

    func frame(forX px: CGFloat, _ snap: TimelineSnapshot) -> Int64 {
        snap.origin + Int64((px / snap.pointsPerFrame).rounded(.down))
    }

    // =========================================================================
    var body: some View {
        GeometryReader { geometry in
            let snap = snapshot
            ZStack(alignment: .topLeading) {
                AureaColors.stage
                rulerCanvas(snap)
                    .frame(height: rulerHeight)
                layerCanvas(snap)
                    .frame(height: max(0, geometry.size.height - rulerHeight))
                    .offset(y: rulerHeight)
                playheadCanvas(snap)
                    .frame(height: geometry.size.height)
                    .allowsHitTesting(false)
            }
            .contentShape(Rectangle())
            .gesture(scrubGesture(snap))
            .simultaneousGesture(selectGesture(snap))
            .simultaneousGesture(zoomGesture)
            .clipped()
        }
        .background(AureaColors.stage)
        .overlay(alignment: .top) {
            Rectangle().fill(AureaColors.hairline).frame(height: AureaDims.hairline)
        }
    }

    // =========================================================================
    // Régua
    // =========================================================================
    private func rulerCanvas(_ snap: TimelineSnapshot) -> some View {
        Canvas { context, size in
            let fps = max(1.0, snap.fps)
            // Um risco por segundo quando cabe; senão, um a cada N segundos — a
            // régua nunca vira um borrão (a mesma regra do painter do Android).
            let pixelsPerSecond = snap.pointsPerFrame * CGFloat(fps)
            guard pixelsPerSecond > 0 else { return }
            var stepSeconds: Double = 1
            while pixelsPerSecond * CGFloat(stepSeconds) < 44 { stepSeconds *= 2 }
            let totalSeconds = Double(max(1, snap.duration)) / fps
            var second: Double = 0
            while second <= totalSeconds {
                let px = CGFloat(Int64(second * fps) - snap.origin) * snap.pointsPerFrame
                if px >= -40 && px <= size.width + 40 {
                    context.fill(Path(CGRect(x: px, y: size.height * 0.45, width: 1,
                                             height: size.height * 0.55)),
                                 with: .color(AureaColors.tickMajor))
                    context.draw(Text(Self.label(seconds: second))
                                    .font(.system(size: 9, weight: .medium))
                                    .foregroundStyle(AureaColors.tickMajor),
                                 at: CGPoint(x: px + 3, y: size.height * 0.28), anchor: .leading)
                    if pixelsPerSecond * CGFloat(stepSeconds) > 120 {
                        for tenth in 1..<10 {
                            let sub = second + stepSeconds * Double(tenth) / 10.0
                            let subX = CGFloat(Int64(sub * fps) - snap.origin) * snap.pointsPerFrame
                            if subX >= 0 && subX <= size.width {
                                context.fill(Path(CGRect(x: subX, y: size.height * 0.72,
                                                         width: 1, height: size.height * 0.28)),
                                             with: .color(AureaColors.tickMinor))
                            }
                        }
                    }
                }
                second += stepSeconds
            }
        }
    }

    static func label(seconds: Double) -> String {
        let total = Int(seconds)
        return String(format: "%d:%02d", total / 60, total % 60)
    }

    // =========================================================================
    // Camadas e keyframes
    // =========================================================================
    private func layerCanvas(_ snap: TimelineSnapshot) -> some View {
        Canvas { context, size in
            for (index, layer) in snap.rows.enumerated() {
                let y = CGFloat(index) * rowHeight
                if y > size.height { break }
                context.fill(Path(CGRect(x: 0, y: y, width: size.width, height: rowHeight - 1)),
                             with: .color(layer.selected ? AureaColors.accentDim.opacity(0.55)
                                                         : AureaColors.surface.opacity(0.35)))
                if layer.selected {
                    context.stroke(Path(CGRect(x: 0.5, y: y + 0.5, width: size.width - 1,
                                               height: rowHeight - 2)),
                                   with: .color(AureaColors.accent), lineWidth: 1)
                }

                let startX = CGFloat(Int64(layer.start) - snap.origin) * snap.pointsPerFrame
                let endX = CGFloat(Int64(layer.end) - snap.origin) * snap.pointsPerFrame
                let clip = CGRect(x: startX, y: y + 4, width: max(2, endX - startX), height: rowHeight - 10)
                let base = layer.threeD ? AureaColors.keyframe : Self.color(forKind: layer.kind)
                context.fill(Path(roundedRect: clip, cornerRadius: 4),
                             with: .color(base.opacity(layer.visible ? 0.75 : 0.28)))
                if layer.locked {
                    context.draw(Text(Image(systemName: "lock.fill"))
                                    .font(.system(size: 9))
                                    .foregroundStyle(AureaColors.text),
                                 at: CGPoint(x: clip.minX + 8, y: clip.midY))
                }
                if clip.width > 40 {
                    context.draw(Text(layer.name.isEmpty ? Self.kindName(layer.kind) : layer.name)
                                    .font(.system(size: 10, weight: .medium))
                                    .foregroundStyle(AureaColors.text),
                                 at: CGPoint(x: clip.minX + 6, y: clip.midY), anchor: .leading)
                }

                // Keyframes de transform: um losango por marca, no tempo LOCAL.
                for key in layer.keys {
                    let localX = CGFloat(Int64(key) + Int64(layer.start) - snap.origin) * snap.pointsPerFrame
                    guard localX > clip.minX - 6, localX < clip.maxX + 6 else { continue }
                    var diamond = Path()
                    diamond.move(to: CGPoint(x: localX, y: clip.midY - 4))
                    diamond.addLine(to: CGPoint(x: localX + 4, y: clip.midY))
                    diamond.addLine(to: CGPoint(x: localX, y: clip.midY + 4))
                    diamond.addLine(to: CGPoint(x: localX - 4, y: clip.midY))
                    diamond.closeSubpath()
                    let onPlayhead = Int64(key) + Int64(layer.start) == snap.playhead
                    context.fill(diamond, with: .color(onPlayhead ? AureaColors.keyframeOn
                                                                  : AureaColors.keyframe))
                }
            }
        }
    }

    static func color(forKind kind: UInt32) -> Color {
        switch kind {
        case 1: return AureaColors.accent        // vídeo
        case 2: return AureaColors.success       // imagem
        case 3: return AureaColors.warning       // áudio
        case 4: return AureaColors.keyframe      // texto
        case 5: return Color(hex: 0x9E9E9E)      // forma
        default: return AureaColors.subtle
        }
    }

    static func kindName(_ kind: UInt32) -> String {
        switch kind {
        case 1: return AureaText.t("editor_video")
        case 2: return AureaText.t("editor_foto")
        case 3: return AureaText.t("editor_musica_ou_som")
        case 4: return "Texto"
        case 5: return "Forma"
        case 6: return "Nulo"
        default: return "Camada"
        }
    }

    // =========================================================================
    // Playhead
    // =========================================================================
    private func playheadCanvas(_ snap: TimelineSnapshot) -> some View {
        Canvas { context, size in
            let px = CGFloat(snap.playhead - snap.origin) * snap.pointsPerFrame
            guard px >= -1 && px <= size.width + 1 else { return }
            context.fill(Path(CGRect(x: px - 0.5, y: 0, width: 1, height: size.height)),
                         with: .color(AureaColors.playhead))
            context.fill(Path(ellipseIn: CGRect(x: px - 4, y: 0, width: 8, height: 8)),
                         with: .color(AureaColors.playhead))
        }
    }

    // =========================================================================
    // Gestos: arrastar o playhead (scrub), tocar uma linha escolhe a camada,
    // pinça dá zoom na régua.
    // =========================================================================
    private func scrubGesture(_ snap: TimelineSnapshot) -> some Gesture {
        DragGesture(minimumDistance: 2)
            .onChanged { value in
                if !scrubbing {
                    scrubbing = true
                    // O motor sabe que o dedo encostou: ele coalesce os pedidos
                    // de seek (um por instante, não uma fila — ver VideoSource).
                    model.engine.run { $0.scrubBegin() }
                }
                let clamped = max(0, min(frame(forX: value.location.x, snap), snap.duration))
                model.engine.run { $0.scrub(toFrame: clamped) }
                // Otimismo LOCAL: o playhead acompanha o dedo sem esperar o
                // tique do status (o valor real volta no próximo `fill_status`).
                model.optimisticPlayhead(clamped)
            }
            .onEnded { _ in
                if scrubbing {
                    scrubbing = false
                    model.engine.run { $0.scrubEnd() }
                }
            }
    }

    /// Tocar uma linha escolhe a camada. A camada é a que está NAQUELE y — a
    /// timeline desenha a frente em cima, a mesma ordem de `query_layers`.
    private func selectGesture(_ snap: TimelineSnapshot) -> some Gesture {
        SpatialTapGesture()
            .onEnded { value in
                let y = value.location.y - rulerHeight
                guard y >= 0 else { return }
                let index = Int(y / rowHeight)
                guard index >= 0 && index < model.layers.count else { return }
                model.select(layerId: model.layers[index].id, additive: false)
            }
    }

    /// Pinça: zoom na régua. O zoom é do DESENHO (pontos por quadro), não do
    /// motor: mudar a escala da timeline não custa nada ao preview.
    private var zoomGesture: some Gesture {
        MagnificationGesture()
            .onChanged { scale in
                pointsPerFrame = min(max(zoomBase * scale, 0.5), 60)
            }
            .onEnded { _ in
                zoomBase = pointsPerFrame
            }
    }
}
