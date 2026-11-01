// Roto Brush do Rotobrush IA (referência: Roto Brush do After Effects).
// "Pintar recorte" no cartão liga o modo: o palco recebe traços de Objeto
// (verde) ou Fundo (vermelho); o motor (EngineRoto.cpp) leva os pontos da
// composição para a camada, guarda no efeito e refaz os recortes.
import SwiftUI

/// O aviso de falha pelo motivo do motor (Renderer::roto_status [3]).
func rotoFailedKey(_ reason: Int64) -> String {
    switch reason {
    case 2, 4: return "om_roto_failed_model"
    case 3: return "om_roto_failed_decode"
    default: return "roto_failed"
    }
}

@MainActor final class RotoPaintState: ObservableObject {
    static let shared = RotoPaintState()

    @Published var layer: Int64 = 0
    @Published var effect: UInt32 = 0
    @Published var active = false
    var previousView: UInt32 = 0
    @Published var background = false
    /// Visualização DENTRO do modo pintar: 0 final, 1 máscara, 2 sobreposição.
    @Published var view: UInt32 = 2
    /// Raio do pincel em px da composição.
    @Published var radius: CGFloat = 24
    /// Traço em curso, em px da composição (desenho ao vivo).
    @Published var live: [CGPoint] = []
    /// [propagados, total, propagando, falhou, traços aqui, traços total,
    ///  quadro atual pronto, quadros do clipe prontos, quadros do clipe, há traços]
    @Published var status: [Int64] = Array(repeating: 0, count: 10)

    func painting(_ model: AureaModel) -> Bool { active && model.primarySelection == layer }

    func end(_ model: AureaModel) {
        guard active else { return }
        _ = model.engine.rotoSetView(previousView, effect: effect, layer: layer)
        active = false
        live = []
        model.refreshModel(force: true)
    }

    func refresh(_ model: AureaModel) {
        status = model.engine.rotoStatus(effect: effect, layer: layer).map { $0.int64Value }
        if status.count < 10 { status = Array(repeating: 0, count: 10) }
    }
}

@MainActor struct RotoPaintStageOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var roto = RotoPaintState.shared
    @ObservedObject private var viewZoom = StageViewZoom.shared

    var body: some View {
        if roto.painting(model) {
            GeometryReader { geo in
                let placed = StageZoomMath.fit(size: geo.size,
                                               composition: CGSize(width: CGFloat(model.compositionWidth), height: CGFloat(model.compositionHeight)),
                                               zoom: viewZoom.zoom, pan: viewZoom.pan)
                Canvas { context, _ in
                    let color = roto.background ? Color(red: 0.9, green: 0.28, blue: 0.3).opacity(0.8)
                                                : Color(red: 0.19, green: 0.77, blue: 0.42).opacity(0.8)
                    let pts = roto.live.map { CGPoint(x: placed.origin.x + $0.x * placed.scale, y: placed.origin.y + $0.y * placed.scale) }
                    let r = roto.radius * placed.scale
                    if pts.count > 1 {
                        var path = Path()
                        path.addLines(pts)
                        context.stroke(path, with: .color(color), style: StrokeStyle(lineWidth: r * 2, lineCap: .round, lineJoin: .round))
                    }
                    if let last = pts.last {
                        context.fill(Path(ellipseIn: CGRect(x: last.x - r, y: last.y - r, width: r * 2, height: r * 2)), with: .color(color))
                    }
                }
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { v in
                        let p = CGPoint(x: (v.location.x - placed.origin.x) / max(placed.scale, 0.0001),
                                        y: (v.location.y - placed.origin.y) / max(placed.scale, 0.0001))
                        roto.live.append(p)
                    }
                    .onEnded { _ in finish() })
                .accessibilityElement()
                .accessibilityLabel(AureaText.t("roto_paint"))
                .accessibilityHint(AureaText.t("roto_hint"))
                .accessibilityIdentifier("stage.roto_paint")
            }
        }
    }

    /// Soltar manda o traço inteiro ao motor (um passo de desfazer).
    private func finish() {
        let pts = roto.live
        roto.live = []
        guard !pts.isEmpty else { return }
        var xy: [NSNumber] = []
        xy.reserveCapacity(pts.count * 2)
        for p in pts { xy.append(NSNumber(value: Float(p.x))); xy.append(NSNumber(value: Float(p.y))) }
        if model.engine.rotoAddStroke(xy, background: roto.background, radius: Float(roto.radius), effect: roto.effect, forLayer: roto.layer) {
            model.refreshModel(force: true)
            roto.refresh(model)
        }
    }
}

/// Dentro do cartão do Rotobrush: o botão "Pintar recorte" e, no modo, os controles.
@MainActor struct RotoPaintCardTools: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var roto = RotoPaintState.shared
    let effectId: UInt32
    let previousView: UInt32
    private let timer = Timer.publish(every: 0.3, on: .main, in: .common).autoconnect()

    private func chip(_ key: String, _ id: String, _ on: Bool, _ action: @escaping () -> Void) -> some View {
        chipText(AureaText.t(key), key, id, on, action)
    }

    private func chipText(_ text: String, _ key: String, _ id: String, _ on: Bool, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(text)
                .font(.system(size: 12, weight: .semibold))
                .foregroundStyle(AureaColors.text)
                .padding(.horizontal, 12)
                .frame(minHeight: 36)
                .background(RoundedRectangle(cornerRadius: 8).fill(on ? AureaColors.text.opacity(0.18) : AureaColors.chip))
        }
        .buttonStyle(.plain)
        .accessibilityLabel(text)
        .accessibilityAddTraits(on ? .isSelected : [])
        .accessibilityIdentifier(id)
    }

    var body: some View {
        let layer = model.primarySelection ?? 0
        let mine = roto.active && roto.layer == layer && roto.effect == effectId
        VStack(alignment: .leading, spacing: 6) {
            if !mine {
                chip("roto_paint", "fx.roto.paint", false) {
                    // Outra sessão aberta: devolve a visualização dela antes.
                    roto.end(model)
                    model.pause()
                    roto.background = false
                    roto.view = 2
                    roto.layer = layer
                    roto.effect = effectId
                    roto.previousView = previousView
                    roto.active = true
                    _ = model.engine.rotoSetView(2, effect: effectId, layer: layer)
                    roto.refresh(model)
                    model.refreshModel(force: true)
                }
            } else {
                Text(AureaText.t("roto_hint"))
                    .font(.system(size: 11.5))
                    .foregroundStyle(AureaColors.muted)
                    .fixedSize(horizontal: false, vertical: true)
                // O que está acontecendo, SEMPRE visível: este quadro, o clipe, falha.
                if roto.status[9] != 0 {
                    let ready = roto.status[6] != 0
                    Text(AureaText.t(ready ? "roto_frame_ready" : "roto_frame_pending"))
                        .font(.system(size: 11.5, weight: .semibold))
                        .foregroundStyle(ready ? AureaColors.success : AureaColors.text)
                        .accessibilityIdentifier("fx.roto.frame")
                    if roto.status[8] > 1 {
                        Text(String(format: AureaText.t("roto_clip_status"), Int(roto.status[7]), Int(roto.status[8])))
                            .font(.system(size: 11.5))
                            .foregroundStyle(AureaColors.muted)
                            .accessibilityIdentifier("fx.roto.clip")
                    }
                    // Bugs 0.3.4: status[3] traz o motivo (2 rede/4 memória,
                    // 3 leitura do vídeo); o aviso diz o que fazer para cada um.
                    if roto.status[3] != 0 && roto.status[2] == 0 {
                        Text(AureaText.t(rotoFailedKey(roto.status[3])))
                            .font(.system(size: 11.5))
                            .foregroundStyle(AureaColors.danger)
                            .fixedSize(horizontal: false, vertical: true)
                            .accessibilityIdentifier("fx.roto.failed")
                    }
                }
                // Ver o resultado sem sair do modo pintar.
                HStack(spacing: 6) {
                    Text(AureaText.t("roto_view_label")).font(.system(size: 11.5)).foregroundStyle(AureaColors.muted)
                    ForEach([(UInt32(0), "roto_view_final", "fx.roto.view.final"),
                             (UInt32(2), "roto_view_overlay", "fx.roto.view.overlay"),
                             (UInt32(1), "roto_view_mask", "fx.roto.view.mask")], id: \.2) { mode, key, id in
                        chip(key, id, roto.view == mode) {
                            roto.view = mode
                            _ = model.engine.rotoSetView(mode, effect: effectId, layer: layer)
                            model.refreshModel(force: true)
                        }
                    }
                }
                HStack(spacing: 6) {
                    chip("roto_object", "fx.roto.object", !roto.background) { roto.background = false }
                    chip("roto_background", "fx.roto.background", roto.background) { roto.background = true }
                    chip("roto_undo", "fx.roto.undo", false) {
                        if model.engine.rotoUndoStroke(effect: effectId, layer: layer) {
                            model.refreshModel(force: true)
                            roto.refresh(model)
                        }
                    }
                }
                Text(AureaText.t("roto_brush_size"))
                    .font(.system(size: 11.5))
                    .foregroundStyle(AureaColors.muted)
                Slider(value: $roto.radius, in: 4...160)
                    .accessibilityLabel(AureaText.t("roto_brush_size"))
                    .accessibilityIdentifier("fx.roto.size")
                HStack(spacing: 6) {
                    let propagateKey = roto.status[8] > 1 ? "roto_propagate_count" : "roto_propagate"
                    chipText(roto.status[8] > 1 ? String(format: AureaText.t(propagateKey), Int(roto.status[8])) : AureaText.t(propagateKey),
                             propagateKey, "fx.roto.propagate", roto.status[2] != 0) {
                        if roto.status[2] != 0 { model.engine.rotoCancel() }
                        else { _ = model.engine.rotoPropagate(effect: effectId, layer: layer) }
                        roto.refresh(model)
                    }
                    chip("roto_done", "fx.roto.done", false) {
                        roto.end(model)
                    }
                }
                if roto.status[2] != 0 && roto.status[1] > 0 {
                    Text(String(format: AureaText.t("roto_progress"), Int(roto.status[0]), Int(roto.status[1])))
                        .font(.system(size: 11.5))
                        .foregroundStyle(AureaColors.muted)
                        .accessibilityIdentifier("fx.roto.progress")
                }
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.bottom, 6)
        .onReceive(timer) { _ in if mine { roto.refresh(model) } }
        // Fechar o painel encerra o modo pintar (Stage Android: DisposableEffect):
        // o palco deixa de comer toques e a sobreposição não fica no preview.
        .onDisappear {
            if roto.active && roto.layer == layer && roto.effect == effectId { roto.end(model) }
        }
    }
}

// =============================================================================
//  "Remover fundo" (0.4.5, aurea.key.background_remove). O recorte por quadro é
//  do motor (EngineBgRemove.cpp: cache salvo com o projeto, estável no tempo);
//  aqui ficam a análise do clipe (progresso, cancelar), o estado deste quadro e
//  os avisos. Os controles de refino são as linhas normais do efeito.
//  Par Android: BgRemoveControls (editor/panels/BgRemovePanel.kt).
// =============================================================================

let bgRemoveEffectKey = "aurea.key.background_remove"

/// O aviso de falha pelo motivo do motor (DepthMapService::Failure).
func bgRemoveFailedKey(_ reason: Int64) -> String {
    switch reason {
    case 2, 4: return "bg_remove_failed_model"
    case 3: return "bg_remove_failed_decode"
    default: return "bg_remove_failed_frames"
    }
}

/// No topo do cartão do Remover fundo: análise do clipe, progresso e estado.
@MainActor struct BgRemoveCardTools: View {
    @EnvironmentObject private var model: AureaModel
    let effectId: UInt32
    /// [analisados, total, analisando, falhou, quadro pronto, prontos no clipe, quadros do clipe, modelo, válido]
    @State private var status: [Int64] = Array(repeating: 0, count: 9)
    private let timer = Timer.publish(every: 0.25, on: .main, in: .common).autoconnect()
    @State private var ticks = 0

    private func refresh() {
        guard let layer = model.primarySelection else { return }
        let next = model.engine.bgRemoveStatus(effect: effectId, layer: layer).map { $0.int64Value }
        // Render ocupado (válido = 0): fica o último estado, sem piscar.
        if next.count >= 9 && next[8] != 0 { status = next }
    }

    private func chip(_ text: String, _ id: String, _ on: Bool, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(text)
                .font(.system(size: 12, weight: .semibold))
                .foregroundStyle(AureaColors.text)
                .padding(.horizontal, 14)
                .frame(minHeight: 44)
                .background(RoundedRectangle(cornerRadius: 8).fill(on ? AureaColors.text.opacity(0.18) : AureaColors.chip))
        }
        .buttonStyle(.plain)
        .accessibilityLabel(text)
        .accessibilityIdentifier(id)
    }

    var body: some View {
        let layer = model.primarySelection ?? 0
        let video = model.layers.first { $0.id == layer }?.kind == 1
        let running = status[2] != 0
        let clipFrames = status[6]
        let readyFrames = status[5]
        let requestedPerson = Int(model.effectParams.first { $0.index == 10 }?.scalar ?? 0) == 1
        VStack(alignment: .leading, spacing: 6) {
            Text(AureaText.t("bg_remove_note"))
                .font(.system(size: 11.5))
                .foregroundStyle(AureaColors.muted)
                .fixedSize(horizontal: false, vertical: true)
            if status[8] != 0 {
                let ready = status[4] != 0
                Text(AureaText.t(ready ? "bg_remove_frame_ready" : "bg_remove_frame_pending"))
                    .font(.system(size: 11.5, weight: .semibold))
                    .foregroundStyle(ready ? AureaColors.success : AureaColors.text)
                    .accessibilityIdentifier("fx.bg.frame")
                if video && clipFrames > 1 {
                    Text(readyFrames >= clipFrames ? AureaText.t("bg_remove_clip_done")
                         : String(format: AureaText.t("bg_remove_clip_status"), Int(readyFrames), Int(clipFrames)))
                        .font(.system(size: 11.5))
                        .foregroundStyle(AureaColors.muted)
                        .accessibilityIdentifier("fx.bg.clip")
                }
                if requestedPerson && status[7] == 0 {
                    Text(AureaText.t("bg_remove_person_unavailable"))
                        .font(.system(size: 11.5))
                        .foregroundStyle(AureaColors.muted)
                        .fixedSize(horizontal: false, vertical: true)
                        .accessibilityIdentifier("fx.bg.person")
                }
                if status[3] != 0 && !running {
                    Text(AureaText.t(bgRemoveFailedKey(status[3])))
                        .font(.system(size: 11.5))
                        .foregroundStyle(AureaColors.danger)
                        .fixedSize(horizontal: false, vertical: true)
                        .accessibilityIdentifier("fx.bg.failed")
                }
            }
            if video {
                HStack(spacing: 6) {
                    if running {
                        chip(AureaText.t("bg_remove_cancel"), "fx.bg.cancel", true) {
                            model.engine.bgRemoveCancel()
                            status[2] = 0
                        }
                    } else if readyFrames < clipFrames || clipFrames <= 1 {
                        let label = clipFrames > 1 ? String(format: AureaText.t("bg_remove_analyze_count"), Int(clipFrames))
                                                   : AureaText.t("bg_remove_analyze")
                        chip(label, "fx.bg.analyze", false) {
                            if model.status.playing != 0 { model.pause() }
                            if model.engine.bgRemoveAnalyze(effect: effectId, layer: layer) {
                                status[2] = 1; status[0] = 0; status[1] = Swift.max(1, clipFrames)
                            } else {
                                model.toast = AureaText.t("bg_remove_failed")
                            }
                        }
                    }
                }
                if running && status[1] > 0 {
                    let done = Swift.min(Swift.max(0, status[0]), status[1])
                    let label = String(format: AureaText.t("bg_remove_progress"), Int(done), Int(status[1]))
                    ProgressView(value: Double(done), total: Double(status[1]))
                        .tint(AureaColors.keyframe)
                        .accessibilityLabel(label)
                        .accessibilityIdentifier("fx.bg.progressBar")
                    Text(label)
                        .font(.system(size: 11.5))
                        .foregroundStyle(AureaColors.muted)
                        .accessibilityIdentifier("fx.bg.progress")
                }
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.bottom, 6)
        .onAppear { refresh() }
        .onReceive(timer) { _ in
            // Analisando ou com este quadro pendente: 4×/s; parado, ~1×/s.
            ticks += 1
            if status[2] != 0 || status[4] == 0 || ticks % 4 == 0 { refresh() }
        }
    }
}
