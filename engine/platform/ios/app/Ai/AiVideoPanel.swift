// =============================================================================
//  Aurea iOS — AI VIDEO: geração remota (MiniMax H3). Porte de
//  android/.../editor/panels/AiVideoPanel.kt.
//
//  A tela não mostra endereço, túnel, ComfyUI nem Colab: mostra o estado
//  ("● Aurea AI • Online"), o que o servidor sabe fazer e o progresso. O vídeo
//  só aparece depois da recompensa do anúncio; pronto, entra na timeline como
//  qualquer vídeo importado.
// =============================================================================
import SwiftUI
import UIKit
import AVKit
import PhotosUI

@MainActor
struct AiVideoPanel: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var ai = AureaAiState.shared

    @State private var mode = "text_to_video"
    @State private var prompt = ""
    @State private var negative = ""
    @State private var advanced = false
    @State private var duration = 0
    @State private var aspect = 0
    @State private var resolution = 1
    @State private var audio = true
    @State private var turbo = true
    @State private var imageRef: String?
    @State private var imageName = ""
    @State private var uploadingImage = false
    @State private var pickedImage: PhotosPickerItem?

    private var caps: AiCapabilities { ai.capabilities }
    private var running: Bool { ai.job?.running == true }

    var body: some View {
        VStack(spacing: 0) {
            PanelHeader(title: AureaText.t("panel_ai_video"), onBack: { model.panel = .none })
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 0) {
                    statusLabel
                    if !ai.error.isEmpty { note(ai.error, AureaColors.danger) }
                    if !ai.message.isEmpty && ai.status.canGenerate { note(ai.message, AureaColors.muted) }
                    if ai.status.canGenerate { form } else { offline }
                    Spacer().frame(height: 24)
                }
                .padding(.horizontal, 18).padding(.vertical, 10)
            }
        }
        .background(AureaColors.editorPanel)
        .onAppear {
            // Abriu o painel, o app já sai atrás do servidor; o Rewarded já carrega.
            ai.connect()
            ai.prepareAd()
        }
        .onChange(of: caps) { c in
            if !c.durations.isEmpty { duration = min(max(duration, 0), c.durations.count - 1) }
            if !c.aspects.isEmpty { aspect = min(max(aspect, 0), c.aspects.count - 1) }
            if !c.resolutions.isEmpty { resolution = min(max(resolution, 0), c.resolutions.count - 1) }
            if !c.hasImageToVideo { mode = "text_to_video" }
        }
        .onChange(of: pickedImage) { item in
            guard let item else { return }
            Task { await upload(item) }
        }
    }

    // MARK: estado

    private var statusLabel: some View {
        let color: Color
        switch ai.status {
        case .connected, .generating: color = AureaColors.success
        case .checking, .reconnecting: color = AureaColors.warning
        case .error: color = AureaColors.danger
        case .disconnected: color = AureaColors.subtle
        }
        let detail = [ai.modelName, ai.gpu].filter { !$0.isEmpty }.joined(separator: " • ")
        return VStack(alignment: .leading, spacing: 0) {
            // Círculo cheio no ar, vazado fora: dá para ler o estado de longe.
            Text("\(ai.status.canGenerate ? "●" : "○")  Aurea AI • \(ai.status.label)")
                .font(.aurea(size: 13, weight: .semibold)).foregroundStyle(color)
                .frame(height: 28)
            if ai.status.canGenerate && !detail.isEmpty {
                Text(detail).font(.aurea(size: 11)).foregroundStyle(AureaColors.subtle)
            }
        }
    }

    private var offline: some View {
        VStack(alignment: .leading, spacing: 10) {
            note(AureaText.t("ai_offline_corpo"), AureaColors.muted)
            button(AureaText.t("ai_procurar_de_novo")) { ai.connect() }
        }.padding(.top, 8)
    }

    // MARK: formulário

    private var form: some View {
        VStack(alignment: .leading, spacing: 0) {
            label(AureaText.t("ai_modo"))
            strip(caps.modes.map(modeName), selected: caps.modes.firstIndex(of: mode) ?? 0, enabled: !running) { mode = caps.modes[$0] }

            if mode == "image_to_video" {
                label(AureaText.t("ai_imagem_de_partida"))
                HStack(spacing: 8) {
                    PhotosPicker(selection: $pickedImage, matching: .images) {
                        pill(AureaText.t(imageRef == nil ? "ai_escolher_imagem" : "ai_trocar_imagem"),
                             active: !uploadingImage && !running)
                    }.disabled(uploadingImage || running)
                    Text(uploadingImage ? AureaText.t("ai_enviando") : imageRef != nil ? imageName : AureaText.t("ai_nenhuma_imagem"))
                        .font(.aurea(size: 12)).foregroundStyle(AureaColors.subtle).lineLimit(1)
                }.frame(height: 44)
            }

            label(AureaText.t("ai_prompt"))
            field($prompt, hint: AureaText.t("ai_prompt_dica"), height: 88)
            if advanced {
                label(AureaText.t("ai_prompt_negativo"))
                field($negative, hint: AureaText.t("ai_prompt_negativo_dica"), height: 56)
            }

            if !caps.durations.isEmpty {
                label(AureaText.t("ai_duracao"))
                strip(caps.durations.map { "\($0)s" }, selected: duration, enabled: !running) { duration = $0 }
            }
            if !caps.aspects.isEmpty {
                label(AureaText.t("ai_proporcao"))
                strip(caps.aspects, selected: aspect, enabled: !running) { aspect = $0 }
            }
            if !caps.resolutions.isEmpty {
                label(AureaText.t("ai_qualidade"))
                strip(caps.resolutions.map(resolutionName), selected: resolution, enabled: !running) { resolution = $0 }
            }
            if caps.audio { toggle(AureaText.t("ai_com_audio"), $audio, enabled: !running) }
            toggle(AureaText.t("ai_avancado"), $advanced)
            if advanced {
                toggle(AureaText.t("ai_turbo"), $turbo, enabled: !running)
                note(AureaText.t("ai_turbo_nota"), AureaColors.subtle)
            }

            generateRow
            rewardState
            progress
            result
            historyList
            footer
        }
    }

    private var generateRow: some View {
        let canGo = !running && !ai.showingAd && !ai.sessionBusy && !prompt.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
            && (mode != "image_to_video" || imageRef != nil)
        return VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 8) {
                button(AureaText.t("ai_assistir_e_gerar"), primary: true, active: canGo) { generate() }
                if running { button(AureaText.t("ai_cancelar"), secondary: true) { ai.cancel() } }
            }
            if ai.showingAd { note(AureaText.t("ai_anuncio_em_curso"), AureaColors.muted) }
        }.padding(.top, 8)
    }

    private func generate() {
        ai.generateWithReward(AiRequest(
            mode: mode,
            prompt: prompt.trimmingCharacters(in: .whitespacesAndNewlines),
            negativePrompt: negative.trimmingCharacters(in: .whitespacesAndNewlines),
            duration: caps.durations.indices.contains(duration) ? caps.durations[duration] : 5,
            aspect: caps.aspects.indices.contains(aspect) ? caps.aspects[aspect] : "16:9",
            resolution: caps.resolutions.indices.contains(resolution) ? caps.resolutions[resolution] : "standard",
            fps: caps.fps.first ?? 24, audio: audio, turbo: turbo,
            imageRef: mode == "image_to_video" ? imageRef : nil))
    }

    // MARK: direito ao vídeo (Rewarded)

    @ViewBuilder private var rewardState: some View {
        if let s = ai.session {
            switch s.status {
            case .preparing:
                note(AureaText.t("ai_preparando_geracao"), AureaColors.muted)
            case .adUnavailable:
                note(AureaText.t("ai_anuncio_indisponivel"), AureaColors.danger)
                button(AureaText.t("ai_procurar_de_novo")) { ai.retryGeneration() }
            case .adShowing, .generating:
                if s.rewardEarned { note(AureaText.t("ai_video_finalizando"), AureaColors.accent) }
                else if s.adClosedEarly { note(AureaText.t("ai_assista_completo"), AureaColors.muted) }
                else { note(AureaText.t("ai_gerando_seu_video"), AureaColors.muted) }
                // Sem anúncio agora: o H3 segue; o vídeo espera o "Assistir e liberar".
                if s.adError != nil && !s.rewardEarned { note(AureaText.t("ai_anuncio_indisponivel"), AureaColors.danger) }
            case .locked:
                VStack(alignment: .leading, spacing: 0) {
                    Text(AureaText.t("ai_video_pronto_titulo")).font(.aurea(size: 15, weight: .semibold))
                        .foregroundStyle(AureaColors.accent).padding(.top, 10)
                    note(AureaText.t(s.adClosedEarly ? "ai_assista_completo" : "ai_video_pronto_assista"), AureaColors.muted)
                    if s.adError != nil { note(AureaText.t("ai_anuncio_indisponivel"), AureaColors.danger) }
                    button(AureaText.t("ai_assistir_e_liberar"), primary: true, active: !ai.showingAd) { ai.unlockWithAd() }
                }
            case .failed:
                // O POST /prompt falhou (node_errors, 400, servidor fora).
                // Antes isto não dizia nada e a tela ficava calada.
                VStack(alignment: .leading, spacing: 0) {
                    Text(AureaText.t("ai_falhou_titulo")).font(.aurea(size: 15, weight: .semibold))
                        .foregroundStyle(AureaColors.danger).padding(.top, 10)
                    note(s.error ?? AureaText.t("ai_falhou_corpo"), AureaColors.muted)
                    button(AureaText.t("ai_procurar_de_novo"), primary: true, active: !ai.showingAd) { ai.retryAfterFailure() }
                }
            case .unlocked:
                EmptyView()
            }
        }
    }

    // MARK: progresso e resultado

    @ViewBuilder private var progress: some View {
        if let j = ai.job, ai.session?.status != .locked {
            VStack(alignment: .leading, spacing: 4) {
                let line = j.status == "queued" && j.queuePosition > 0 ? AureaText.t("ai_na_fila", j.queuePosition)
                    : j.status == "queued" ? AureaText.t("ai_proximo_da_fila") : (j.stage.isEmpty ? j.status : j.stage)
                Text(line).font(.aurea(size: 13, weight: .semibold)).foregroundStyle(AureaColors.accent)
                // O ComfyUI não dá porcentagem pelo /history: a barra só aparece com progresso real.
                if j.progress > 0 {
                    ProgressView(value: min(max(j.progress, 0), 1)).tint(AureaColors.accent)
                }
                Text((j.progress > 0 ? "\(Int(j.progress * 100))%  ·  " : "") + formatAiDuration(j.seconds))
                    .font(.aurea(size: 12)).foregroundStyle(AureaColors.subtle)
            }.padding(.top, 10)
        }
    }

    @ViewBuilder private var result: some View {
        if let file = ai.lastFile {
            VStack(alignment: .leading, spacing: 8) {
                if let r = ai.job?.result {
                    Text("\(r.width)×\(r.height) · \(formatAiDuration(r.durationSeconds)) · \(r.fps) fps"
                         + (r.hasAudio ? " · " + AureaText.t("ai_com_audio_curto") : ""))
                        .font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                }
                AiVideoPreview(url: file)
                HStack(spacing: 8) {
                    button(AureaText.t("ai_adicionar_timeline"), primary: true) {
                        ai.addToTimeline(model)
                        model.panel = .none
                    }
                    button(AureaText.t("ai_salvar_galeria"), secondary: true) { ai.saveToGallery() }
                }
                note(AureaText.t("ai_ficou_no_aparelho"), AureaColors.subtle)
            }.padding(.top, 12)
        } else if ai.downloading {
            note(AureaText.t("ai_baixando"), AureaColors.muted)
        }
    }

    @ViewBuilder private var historyList: some View {
        if !ai.history.isEmpty && ai.job == nil {
            label(AureaText.t("ai_historico"))
            ForEach(ai.history, id: \.id) { h in
                HStack {
                    Text(h.stage.isEmpty ? h.status : h.stage).font(.aurea(size: 13)).foregroundStyle(AureaColors.text)
                    Spacer()
                    button(AureaText.t(h.running ? "ai_acompanhar" : "ai_abrir"), secondary: true) { ai.play(h) }
                }.frame(height: 40).padding(.vertical, 4)
            }
        }
    }

    private var footer: some View {
        VStack(alignment: .leading, spacing: 0) {
            if caps.queue > 0 { note(AureaText.t("ai_fila_do_servidor", caps.queue), AureaColors.subtle) }
        }.padding(.top, 14)
    }

    // MARK: imagem de partida

    private func upload(_ item: PhotosPickerItem) async {
        uploadingImage = true
        defer { uploadingImage = false; pickedImage = nil }
        guard let data = try? await item.loadTransferable(type: Data.self), data.count <= 12 * 1024 * 1024 else { return }
        // Só PNG/JPEG/WebP passam (o servidor recusa o resto); o resto vira JPEG.
        let type: String
        let bytes: Data
        if data.starts(with: [0x89, 0x50, 0x4E, 0x47]) { type = "image/png"; bytes = data }
        else if data.starts(with: [0xFF, 0xD8]) { type = "image/jpeg"; bytes = data }
        else if data.count > 12, data[8..<12].elementsEqual("WEBP".utf8) { type = "image/webp"; bytes = data }
        else if let jpeg = UIImage(data: data)?.jpegData(compressionQuality: 0.92) { type = "image/jpeg"; bytes = jpeg }
        else { return }
        let ref = await ai.uploadImage(bytes, type: type)
        imageRef = ref
        imageName = ref.map { String($0.suffix(28)) } ?? ""
    }

    // MARK: peças

    private func modeName(_ m: String) -> String {
        switch m {
        case "text_to_video": return AureaText.t("ai_modo_texto")
        case "image_to_video": return AureaText.t("ai_modo_imagem")
        default: return m
        }
    }

    private func resolutionName(_ r: String) -> String {
        switch r {
        case "preview": return "Rascunho"
        case "standard": return "Padrão"
        case "high": return "Alta"
        default: return r
        }
    }

    private func note(_ text: String, _ color: Color) -> some View {
        Text(text).font(.aurea(size: 12)).foregroundStyle(color).padding(.vertical, 5)
            .fixedSize(horizontal: false, vertical: true)
    }

    private func label(_ text: String) -> some View {
        Text(text).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted).padding(.top, 8)
    }

    private func strip(_ options: [String], selected: Int, enabled: Bool = true, pick: @escaping (Int) -> Void) -> some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 6) {
                ForEach(Array(options.enumerated()), id: \.offset) { i, o in
                    let on = i == selected
                    Button { if enabled { pick(i) } } label: {
                        Text(o).font(.aurea(size: 12))
                            .foregroundStyle(on ? AureaColors.accent : enabled ? AureaColors.text : AureaColors.muted)
                            .padding(.horizontal, 10).padding(.vertical, 6)
                            .background(on ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                    }.buttonStyle(.plain)
                }
            }
        }.frame(height: 44)
    }

    private func pill(_ text: String, primary: Bool = false, secondary: Bool = false, active: Bool = true) -> some View {
        let background = !active ? AureaColors.chip.opacity(0.4) : primary ? AureaColors.accentDim : secondary ? AureaColors.chipHigh : AureaColors.chip
        return Text(text).font(.aurea(size: 13, weight: .semibold))
            .foregroundStyle(!active ? AureaColors.muted : primary ? AureaColors.accent : AureaColors.text)
            .padding(.horizontal, 14).padding(.vertical, 10)
            .background(background, in: RoundedRectangle(cornerRadius: 10))
    }

    private func button(_ text: String, primary: Bool = false, secondary: Bool = false, active: Bool = true,
                        action: @escaping () -> Void) -> some View {
        Button { if active { action() } } label: { pill(text, primary: primary, secondary: secondary, active: active) }
            .buttonStyle(.plain).disabled(!active)
    }

    private func toggle(_ text: String, _ value: Binding<Bool>, enabled: Bool = true) -> some View {
        Button { if enabled { value.wrappedValue.toggle() } } label: {
            HStack {
                Text(text).font(.aurea(size: 13)).foregroundStyle(enabled ? AureaColors.text : AureaColors.muted)
                Spacer()
                Text(AureaText.t(value.wrappedValue ? "ai_sim" : "ai_nao")).font(.aurea(size: 12))
                    .foregroundStyle(value.wrappedValue && enabled ? AureaColors.accent : AureaColors.muted)
                    .padding(.horizontal, 12).padding(.vertical, 5)
                    .background(value.wrappedValue && enabled ? AureaColors.accentDim : AureaColors.chip, in: Capsule())
            }.frame(height: 44).contentShape(Rectangle())
        }.buttonStyle(.plain)
    }

    private func field(_ text: Binding<String>, hint: String, height: CGFloat) -> some View {
        ZStack(alignment: .topLeading) {
            if text.wrappedValue.isEmpty {
                Text(hint).font(.aurea(size: 13)).foregroundStyle(AureaColors.subtle).padding(.top, 8)
            }
            TextEditor(text: text).font(.aurea(size: 13)).foregroundStyle(AureaColors.text)
                .scrollContentBackground(.hidden).tint(AureaColors.accent)
        }
        .padding(.horizontal, 10).padding(.vertical, 4)
        .frame(height: height)
        .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
    }
}

/// Prévia do vídeo gerado (arquivo local), em loop — o player do sistema.
private struct AiVideoPreview: View {
    let url: URL
    @State private var player: AVQueuePlayer?
    @State private var looper: AVPlayerLooper?

    var body: some View {
        ZStack {
            AureaColors.stage
            if let player {
                VideoPlayer(player: player)
            } else {
                Text(AureaText.t("ai_preparando_previa")).font(.aurea(size: 12)).foregroundStyle(AureaColors.subtle)
            }
        }
        .frame(height: 180)
        .clipShape(RoundedRectangle(cornerRadius: 10))
        .onAppear {
            let p = AVQueuePlayer()
            looper = AVPlayerLooper(player: p, templateItem: AVPlayerItem(url: url))
            player = p
            p.play()
        }
        .onDisappear { player?.pause(); player = nil; looper = nil }
    }
}
