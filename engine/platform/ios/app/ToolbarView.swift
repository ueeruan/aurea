// =============================================================================
//  Aurea / platform / ios / app / ToolbarView.swift
//
//  A barra do topo e o transporte. Os mesmos botões do Android, com os mesmos
//  significados — inclusive os gestos longos, que são a parte que ninguém
//  descobre sozinha:
//
//    toque  = reproduzir / pausar           segurar = ligar/desligar a repetição
//    toque  = quadro atrás                  segurar = vai ao início
//    toque  = quadro à frente               segurar = vai ao fim
//    toque  = keyframe anterior             segurar = primeiro keyframe
//    toque  = próximo keyframe              segurar = último keyframe
//
//  TODA ação vira um comando do motor (`PlaybackStep`, `PlaybackSeek`…). O
//  transporte não mexe no playhead por conta própria: quem manda no tempo é o
//  motor, e o status devolve o instante real.
// =============================================================================
import SwiftUI

// =============================================================================
// Barra do topo
// =============================================================================
struct TopBarView: View {
    @EnvironmentObject private var model: AureaModel
    @State private var renaming = false
    @State private var newName = ""
    @State private var showLayerMenu = false

    var body: some View {
        HStack(spacing: 2) {
            AureaIconButton(systemName: "chevron.left", size: 18) { model.closeProject() }
            AureaIconButton(systemName: "folder", size: 17) { model.toast = "projeto: \(model.projectName)" }

            Button {
                newName = model.projectName
                renaming = true
            } label: {
                HStack(spacing: 6) {
                    Text(model.projectName.isEmpty ? AureaText.t("editor_projeto_cbe9") : model.projectName)
                        .font(AureaType.section)
                        .foregroundStyle(AureaColors.text)
                        .lineLimit(1)
                    Text(model.timecode(model.status.playhead) + " · " + model.timecode(model.status.duration))
                        .font(AureaType.tiny)
                        .foregroundStyle(AureaColors.subtle)
                }
            }
            .buttonStyle(.plain)
            .frame(maxWidth: .infinity, alignment: .leading)

            AureaIconButton(systemName: "arrow.uturn.backward", enabled: model.status.canUndo != 0) { model.undo() }
            AureaIconButton(systemName: "arrow.uturn.forward", enabled: model.status.canRedo != 0) { model.redo() }
            AureaIconButton(systemName: "square.and.arrow.up", size: 18) { model.showExport = true }
            AureaIconButton(systemName: "ellipsis.circle", size: 18) { showLayerMenu = true }
        }
        .padding(.horizontal, 6)
        .background(AureaColors.background)
        .overlay(alignment: .bottom) {
            Rectangle().fill(AureaColors.hairline).frame(height: AureaDims.hairline)
        }
        .alert(AureaText.t("common_rename"), isPresented: $renaming) {
            TextField(AureaText.t("editor_nome_camada"), text: $newName)
            Button(AureaText.t("common_cancel"), role: .cancel) {}
            Button(AureaText.t("common_save")) {
                guard !newName.isEmpty else { return }
                model.renameCurrentProject(to: newName)
            }
        }
        .confirmationDialog(AureaText.t("editor_mais_acoes_camada"), isPresented: $showLayerMenu) {
            Button(AureaText.t("editor_agrupar")) { model.groupSelection() }
            Button(AureaText.t("editor_desagrupar")) {
                guard let layer = model.primarySelection else { return }
                model.ungroup(layer)
            }
            Button(AureaText.t("editor_dividir_cabecote")) {
                guard let layer = model.primarySelection else { return }
                model.mutate { $0.splitLayer(layer, atFrame: Int32(model.status.playhead)) }
                model.refreshModel(force: true)
            }
            Button(AureaText.t("editor_duplicar_camada")) {
                guard !model.selection.isEmpty else { return }
                model.mutate { $0.duplicateLayers(model.selection.map { NSNumber(value: $0) }) }
                model.refreshModel(force: true)
            }
            Button(AureaText.t("editor_excluir_camada"), role: .destructive) {
                guard !model.selection.isEmpty else { return }
                model.mutate { $0.deleteLayers(model.selection.map { NSNumber(value: $0) }) }
                model.clearSelection()
                model.refreshModel(force: true)
            }
            Button(AureaText.t("common_cancel"), role: .cancel) {}
        }
    }
}

// =============================================================================
// Transporte
// =============================================================================
struct TransportView: View {
    @EnvironmentObject private var model: AureaModel
    @State private var loop = false
    @State private var muted = false
    @State private var speed: Float = 1.0

    var body: some View {
        HStack(spacing: 0) {
            // Quadro atrás / segurar = início.
            HoldButton(systemName: "backward.frame.fill",
                       tap: { model.step(-1) },
                       hold: { model.seek(toFrame: 0) })

            // Keyframe anterior / segurar = primeiro.
            HoldButton(systemName: "diamond.fill",
                       tap: { stepKeyframe(forward: false) },
                       hold: { seekKeyframe(edge: false) })

            Button { model.toggleMarker() } label: {
                Image(systemName: "flag")
                    .font(.system(size: 16))
                    .foregroundStyle(AureaColors.muted)
                    .frame(width: 40, height: 38)
            }
            .buttonStyle(.plain)

            Spacer(minLength: 4)

            Button { model.playPause() } label: {
                Image(systemName: model.status.playing != 0 ? "pause.fill" : "play.fill")
                    .font(.system(size: 24))
                    .foregroundStyle(AureaColors.text)
                    .frame(width: 56, height: 40)
            }
            .buttonStyle(.plain)

            Spacer(minLength: 4)

            // Keyframe seguinte / segurar = último.
            HoldButton(systemName: "diamond",
                       tap: { stepKeyframe(forward: true) },
                       hold: { seekKeyframe(edge: true) })

            // Quadro à frente / segurar = fim.
            HoldButton(systemName: "forward.frame.fill",
                       tap: { model.step(1) },
                       hold: { model.seek(toFrame: model.compositionDuration) })

            Button {
                loop.toggle()
                model.setLoop(loop)
            } label: {
                Image(systemName: "repeat")
                    .font(.system(size: 16))
                    .foregroundStyle(loop ? AureaColors.accent : AureaColors.muted)
                    .frame(width: 38, height: 38)
            }
            .buttonStyle(.plain)

            Menu {
                ForEach([0.25, 0.5, 1.0, 1.5, 2.0, 4.0], id: \.self) { value in
                    Button(String(format: "%g×", value)) {
                        speed = Float(value)
                        model.setSpeed(Float(value))
                    }
                }
            } label: {
                Text(String(format: "%g×", speed))
                    .font(AureaType.tiny)
                    .foregroundStyle(speed == 1 ? AureaColors.muted : AureaColors.accent)
                    .frame(width: 38, height: 38)
            }

            Button {
                muted.toggle()
                // Silenciar é um COMANDO de áudio por camada escolhida
                // (`AudioSetMuted`) — o mixer do núcleo é quem cala.
                for id in model.selection { model.engine.setLayer(id, audioMuted: muted) }
                if model.selection.isEmpty {
                    model.toast = AureaText.t("editor_nenhum")
                }
            } label: {
                Image(systemName: muted ? "speaker.slash.fill" : "speaker.wave.2.fill")
                    .font(.system(size: 15))
                    .foregroundStyle(muted ? AureaColors.danger : AureaColors.muted)
                    .frame(width: 34, height: 38)
            }
            .buttonStyle(.plain)

            AureaIconButton(systemName: model.fullscreen ? "arrow.down.right.and.arrow.up.left" : "arrow.up.left.and.arrow.down.right",
                            size: 16) {
                model.fullscreen.toggle()
                model.invalidatePreview()
            }
        }
        .padding(.horizontal, 2)
        .background(AureaColors.background)
        .overlay(alignment: .top) {
            Rectangle().fill(AureaColors.hairline).frame(height: AureaDims.hairline)
        }
    }

    /// O keyframe mais próximo, ANTES ou DEPOIS do playhead, entre as camadas
    /// escolhidas — a mesma busca que o transporte do Android faz.
    private func stepKeyframe(forward: Bool) {
        let playhead = model.status.playhead
        var candidates: [Int32] = []
        for id in model.selection {
            for key in model.keyframes[id] ?? [] where key.effectIndex == 0xFFFF_FFFF {
                candidates.append(key.time)
            }
        }
        candidates.sort()
        let next = forward ? candidates.first { Int64($0) > playhead } : candidates.last { Int64($0) < playhead }
        if let target = next { model.seek(toFrame: Int64(target)) }
    }

    private func seekKeyframe(edge: Bool) {
        var candidates: [Int32] = []
        for id in model.selection {
            for key in model.keyframes[id] ?? [] { candidates.append(key.time) }
        }
        guard !candidates.isEmpty else { return }
        candidates.sort()
        model.seek(toFrame: Int64(edge ? (candidates.last ?? 0) : (candidates.first ?? 0)))
    }
}

/// Botão com os dois gestos (toque e segurar). É o que a casca usa para os
/// controles de quadro e de keyframe.
struct HoldButton: View {
    let systemName: String
    let tap: () -> Void
    let hold: () -> Void

    var body: some View {
        Image(systemName: systemName)
            .font(.system(size: 16))
            .foregroundStyle(AureaColors.muted)
            .frame(width: 40, height: 38)
            .contentShape(Rectangle())
            .onTapGesture { tap() }
            .onLongPressGesture(minimumDuration: 0.45) {
                hold()
            }
    }
}
