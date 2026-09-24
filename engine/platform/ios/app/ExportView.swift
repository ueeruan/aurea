// Direct port of editor/ExportScreen.kt: preview, ordered choices, summary,
// progress and a fixed footer. Video rendering/encoding stays in the core.
import SwiftUI
import UIKit
import AVKit

struct ExportView: View {
    @EnvironmentObject private var model: AureaModel
    @Environment(\.dismiss) private var dismiss
    @State private var preview: UIImage?
    @State private var highQuality = false
    @State private var attempted = false
    @State private var cancelled = false
    @State private var sharing = false
    @State private var viewing = false
    private let resolutions: [UInt32] = [720, 1080, 1440, 2160]
    private var seconds: Double { Double(model.compositionDuration) / max(1, model.compositionFps) }
    private var fps: Double { model.exportOptions.fps > 0 ? model.exportOptions.fps : model.compositionFps }
    private var codec: String { model.exportOptions.codec == .hevc ? "HEVC" : "H.264" }
    private var estimatedMbps: Double {
        min(120, max(2, Double(outputSize.0) * Double(outputSize.1) * fps * 0.2 / 1_000_000)) * (highQuality ? 1.6 : 1)
    }
    private var outputSize: (UInt32, UInt32) { sizeFor(model.exportOptions.shortSide) }
    private var blocked: [UInt32] { resolutions.filter { !fits(sizeFor($0)) } }
    private var hevcAvailable: Bool {
        let bits = model.deviceReport["bits"]?.uint64Value ?? 0
        return bits & 1 == 0 || bits & 32 != 0
    }

    var body: some View {
        GeometryReader { bounds in
            VStack(spacing: 0) {
                topBar
                ScrollView {
                    VStack(alignment: .leading, spacing: 0) {
                        previewCard(width: bounds.size.width - 36)
                        Spacer().frame(height: 18)
                        if model.exporting { progress }
                        else if let url = model.exportedURL { done(url) }
                        else {
                            if attempted, let notice = failureNotice { noticeView(notice, danger: !cancelled) }
                            options
                        }
                        Spacer().frame(height: 24)
                    }.padding(.horizontal, 18)
                }
                footer.padding(.horizontal, 18).padding(.vertical, 12)
            }
        }
        .background(AureaColors.background.ignoresSafeArea()).foregroundStyle(AureaColors.text)
        .preferredColorScheme(.dark).interactiveDismissDisabled(model.exporting)
        .sheet(isPresented: $sharing) { if let url = model.exportedURL { ExportShareSheet(url: url) } }
        .sheet(isPresented: $viewing) {
            if let url = model.exportedURL { ExportMoviePlayer(url: url) }
        }
        .task {
            if !model.exporting {
                model.exportOptions = ExportOptions()
                model.exportOptions.shortSide = min(1080, max(720, min(model.compositionWidth, model.compositionHeight)))
                model.exportOptions.bitrateMbps = 0
            }
            let native = model.engine
            let image = await Task.detached(priority: .userInitiated) { () -> UIImage? in
                var width: UInt32 = 0, height: UInt32 = 0
                guard let data = native.captureFrame(640, outWidth: &width, outHeight: &height) else { return nil }
                return UIImage.fromRGBA(data, width: Int(width), height: Int(height))
            }.value
            if !Task.isCancelled { preview = image }
        }
    }

    private var topBar: some View {
        HStack(spacing: 4) {
            Button { if !model.exporting { dismiss() } } label: {
                CupertinoGlyph.text(CupertinoGlyph.ChevronLeft, size: 22, color: model.exporting ? AureaColors.disabled : AureaColors.text)
                    .frame(width: 40, height: 44).contentShape(Rectangle())
            }.buttonStyle(.plain).disabled(model.exporting).accessibilityLabel(AureaText.t("editor_fechar"))
            Text(AureaText.t("editor_exportar")).font(.aurea(size: 17, weight: .semibold))
            Spacer(minLength: 0)
        }.padding(.horizontal, 6).frame(height: 44)
    }
    private func previewCard(width: CGFloat) -> some View {
        let ratio = CGFloat(model.compositionWidth) / CGFloat(max(1, model.compositionHeight))
        let height = min(252, max(1, width) / ratio)
        return ZStack {
            AureaColors.stage
            if let preview { Image(uiImage: preview).resizable().scaledToFit() }
            else { CupertinoGlyph.text(CupertinoGlyph.Film, size: 34, color: AureaColors.muted) }
        }.frame(width: height * ratio, height: height)
            .clipShape(RoundedRectangle(cornerRadius: 10))
            .overlay(RoundedRectangle(cornerRadius: 10).stroke(AureaColors.border, lineWidth: 1))
            .frame(maxWidth: .infinity).padding(.top, 8).accessibilityLabel(AureaText.t("editor_previa"))
    }
    private var options: some View {
        VStack(alignment: .leading, spacing: 0) {
            section("editor_resolucao")
            chips(resolutions.map(resolutionLabel), selected: blocked.contains(model.exportOptions.shortSide) ? nil : resolutionLabel(model.exportOptions.shortSide),
                  disabled: Set(blocked.map(resolutionLabel))) { picked in
                if let side = resolutions.first(where: { resolutionLabel($0) == picked }) { model.exportOptions.shortSide = side }
            }
            if !blocked.isEmpty {
                Text(exportLimitReason ?? AureaText.t("sh_export_above_device", blocked.map(resolutionLabel).joined(separator: ", ")))
                    .font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).padding(.top, 6)
            }
            section("editor_quadros_segundo")
            let projectFps = AureaText.t("sh_export_fps_from_project", format(model.compositionFps))
            chips([projectFps, "24", "30", "60"], selected: model.exportOptions.fps == 0 ? projectFps : format(model.exportOptions.fps)) {
                model.exportOptions.fps = $0 == projectFps ? 0 : Double($0.replacingOccurrences(of: ",", with: ".")) ?? model.compositionFps
            }
            section("editor_formato")
            chips(["H.264", "HEVC"], selected: codec, disabled: hevcAvailable ? [] : ["HEVC"]) {
                model.exportOptions.codec = $0 == "HEVC" ? .hevc : .h264
            }
            Text(hevcAvailable ? AureaText.t(model.exportOptions.codec == .hevc ? "editor_hevc_arquivo_menor_mesma_qualidade_alguns" : "editor_h_264_abre_qualquer_aparelho_rede")
                 : "HEVC indisponível neste aparelho: ele não tem codificador HEVC. O vídeo sai em H.264.")
                .font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).padding(.top, 6)
            section("editor_qualidade")
            let standard = AureaText.t("editor_padrao"), high = AureaText.t("editor_alta")
            chips([standard, high], selected: highQuality ? high : standard) { highQuality = $0 == high }
            VStack(spacing: 0) {
                summary("editor_video", "\(outputSize.0) × \(outputSize.1) · \(format(fps)) fps · \(codec)")
                summary("editor_duracao", formatTime(seconds))
                summary("editor_tamanho_estimado", estimatedSize)
                summary("editor_cor", AureaText.t("editor_sdr_bt_709"))
            }.padding(.top, 18)
        }
    }
    private func section(_ key: String) -> some View {
        Text(AureaText.t(key).uppercased()).font(.aurea(size: 12, weight: .semibold)).tracking(0.6)
            .foregroundStyle(AureaColors.muted).padding(.top, 16).padding(.bottom, 8)
    }
    private func chips(_ choices: [String], selected: String?, disabled: Set<String> = [], onPick: @escaping (String) -> Void) -> some View {
        AureaFlowLayout(hGap: 8, vGap: 8) {
            ForEach(choices, id: \.self) { choice in
                let on = selected == choice, off = disabled.contains(choice)
                Button { if !on && !off { onPick(choice) } } label: {
                    Text(choice).font(.aurea(size: 14, weight: .semibold))
                        .foregroundStyle(off ? AureaColors.disabled : on ? AureaColors.accent : AureaColors.text)
                        .padding(.horizontal, 14).padding(.vertical, 10)
                        .background(on ? AureaColors.actionDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
                        .overlay(RoundedRectangle(cornerRadius: 10).stroke(on ? AureaColors.brand : .clear, lineWidth: 1))
                }.buttonStyle(.plain).disabled(off).accessibilityAddTraits(on ? .isSelected : [])
            }
        }
    }
    private func summary(_ key: String, _ value: String) -> some View {
        HStack(alignment: .top, spacing: 8) {
            Text(AureaText.t(key)).foregroundStyle(AureaColors.muted).frame(maxWidth: .infinity, alignment: .leading)
            Text(value).monospacedDigit().multilineTextAlignment(.trailing)
                // Android measures the unweighted value first and gives the
                // label the remaining width. Preserve that ordering so the
                // codec fits on the same line at the 393 pt viewport.
                .fixedSize(horizontal: false, vertical: true).layoutPriority(1)
        }.font(.aurea(size: 14)).padding(.vertical, 5)
    }
    private func noticeView(_ text: String, danger: Bool) -> some View {
        Text(text).font(.aurea(size: 14)).foregroundStyle(danger ? AureaColors.danger : AureaColors.warning)
            .frame(maxWidth: .infinity, alignment: .leading).padding(14)
            .background(AureaColors.surface, in: RoundedRectangle(cornerRadius: 10))
    }
    private var progress: some View {
        let done = (model.exportProgress["framesDone"] as? NSNumber)?.intValue ?? 0
        let total = (model.exportProgress["framesTotal"] as? NSNumber)?.intValue ?? 0
        let fraction = min(1, max(0, Double(done) / Double(max(1, total))))
        let speed = (model.exportProgress["fps"] as? NSNumber)?.doubleValue ?? 0
        let eta = (model.exportProgress["etaSeconds"] as? NSNumber)?.doubleValue ?? 0
        return VStack(spacing: 0) {
            Text(model.exportPublishing ? AureaText.t("editor_salvando_galeria") : "\(Int((fraction * 100).rounded()))%").font(.aurea(size: 34, weight: .bold)).padding(.top, 12)
            GeometryReader { bounds in
                ZStack(alignment: .leading) {
                    AureaColors.chip
                    AureaColors.accent.frame(width: bounds.size.width * (model.exportPublishing ? 1 : fraction))
                }.clipShape(RoundedRectangle(cornerRadius: 3))
            }.frame(height: 6).padding(.top, 14)
            if !model.exportPublishing {
            Text(AureaText.t("sh_export_progress", String(done), String(total), format(speed), formatTime(eta)))
                .font(.aurea(size: 13).monospacedDigit()).foregroundStyle(AureaColors.muted)
                .multilineTextAlignment(.center).padding(.top, 12)
            Text(AureaText.t("editor_mantenha_aurea_aberto_ate_terminar")).font(.aurea(size: 13))
                .foregroundStyle(AureaColors.muted).multilineTextAlignment(.center).padding(.top, 6)
            if !progressNotice.isEmpty { noticeView(progressNotice, danger: false).padding(.top, 12) }
            }
        }.frame(maxWidth: .infinity)
    }
    private func done(_ url: URL) -> some View {
        VStack(spacing: 0) {
            CupertinoGlyph.text(CupertinoGlyph.CheckmarkCircleFill, size: 44, color: AureaColors.success).padding(.top, 8)
            Text(AureaText.t("editor_video_pronto")).font(.aurea(size: 22, weight: .bold)).padding(.top, 10)
            Text(model.exportMessage ?? url.lastPathComponent).font(.aurea(size: 14)).foregroundStyle(AureaColors.muted)
                .multilineTextAlignment(.center).padding(.top, 6)
        }.frame(maxWidth: .infinity)
    }
    private var footer: some View {
        Group {
            if model.exportPublishing {
                wideButton("editor_salvando", filled: false) {}.disabled(true)
            } else if model.exporting {
                wideButton("editor_cancelar", filled: false) { cancelled = true; model.cancelExport() }.disabled(model.exportCancelled)
            } else if model.exportedURL != nil {
                HStack(spacing: 10) {
                    wideButton("editor_abrir", filled: false) { viewing = true }
                    wideButton("editor_compartilhar", filled: true) { sharing = true }
                }
            } else {
                wideButton("editor_exportar", filled: true) {
                    attempted = true; cancelled = false
                    model.exportOptions.bitrateMbps = highQuality ? UInt32(max(1, estimatedMbps)) : 0
                    model.startExport()
                }
            }
        }
    }
    private func wideButton(_ key: String, filled: Bool, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(AureaText.t(key)).font(.aurea(size: 17, weight: .semibold))
                .foregroundStyle(filled ? AureaColors.onAccent : AureaColors.text)
                .frame(maxWidth: .infinity).frame(height: 52)
                .background(filled ? AureaColors.accent : AureaColors.chip, in: RoundedRectangle(cornerRadius: 14))
        }.buttonStyle(.plain)
    }
    private func sizeFor(_ side: UInt32) -> (UInt32, UInt32) {
        let shortest = Double(max(1, min(model.compositionWidth, model.compositionHeight)))
        return (UInt32((Double(model.compositionWidth) * Double(side) / shortest / 2).rounded()) * 2,
                UInt32((Double(model.compositionHeight) * Double(side) / shortest / 2).rounded()) * 2)
    }
    private func fits(_ size: (UInt32, UInt32)) -> Bool {
        let cap = (model.composition["sizeCap"] as? [NSNumber] ?? []).map(\.uint32Value)
        guard cap.count >= 2, cap[0] > 0 else { return true }
        return max(size.0, size.1) <= cap[0] && min(size.0, size.1) <= cap[1]
    }
    private func resolutionLabel(_ side: UInt32) -> String { ProjectPresets.resolutionLabel(Int(side)) }
    private var exportLimitReason: String? {
        let h = model.deviceReport["maxExportHeight"]?.intValue ?? 0
        let w = model.deviceReport["maxExportWidth"]?.intValue ?? 0
        guard h > 0, h < 2160 else { return nil }
        switch model.deviceReport["exportLimit"]?.intValue ?? 0 {
        case 2: return "Este aparelho exporta até \(h)p: o codificador de vídeo dele não passa de \(w) × \(h)."
        case 3: return "Este aparelho exporta até \(h)p: a memória não comporta os quadros de uma exportação maior."
        case 4: return "Este aparelho exporta até \(h)p: o sistema não informou codificador H.264."
        default: return "Este aparelho exporta até \(h)p."
        }
    }
    private var estimatedSize: String {
        let megabytes = estimatedMbps * seconds / 8
        return megabytes >= 1000 ? AureaText.t("unit_gigabyte", format(megabytes / 1000)) : AureaText.t("unit_megabyte", Int(megabytes.rounded()))
    }
    private var failureNotice: String? {
        if model.exportCancelled || cancelled { return "Exportação cancelada." }
        let result = (model.exportProgress["result"] as? NSNumber)?.intValue ?? 0
        if result != 0 { return "Não deu para exportar: \(model.exportProgress["message"] as? String ?? "erro \(result)")." }
        return model.exportMessage
    }
    private var progressNotice: String {
        let flags = (model.exportProgress["flags"] as? NSNumber)?.uint32Value ?? 0
        var notices: [String] = []
        if flags & AureaExportFlag.softwareEncoder.rawValue != 0 {
            notices.append("Este aparelho não tem encoder de hardware \(codec) para esta resolução: exportando por software (mais lento, mesma qualidade).")
        }
        if flags & AureaExportFlag.thermalReduced.rawValue != 0 { notices.append("Aparelho quente: a exportação desacelerou para esfriar. A qualidade não muda.") }
        return notices.joined(separator: "\n")
    }
    private func format(_ value: Double) -> String {
        if value == value.rounded() { return String(Int(value)) }
        var text = String(format: "%.2f", value)
        while text.hasSuffix("0") { text.removeLast() }
        if text.hasSuffix(".") { text.removeLast() }
        return text.replacingOccurrences(of: ".", with: ",")
    }
    private func formatTime(_ seconds: Double) -> String {
        let value = max(0, Int(seconds.rounded()))
        return value >= 3600 ? String(format: "%d:%02d:%02d", value / 3600, value / 60 % 60, value % 60)
            : String(format: "%d:%02d", value / 60, value % 60)
    }
}

private struct ExportShareSheet: UIViewControllerRepresentable {
    let url: URL
    func makeUIViewController(context: Context) -> UIActivityViewController { UIActivityViewController(activityItems: [url], applicationActivities: nil) }
    func updateUIViewController(_ controller: UIActivityViewController, context: Context) {}
}
private struct ExportMoviePlayer: View {
    let url: URL
    @Environment(\.dismiss) private var dismiss
    @State private var player: AVPlayer?
    var body: some View {
        VideoPlayer(player: player)
            .onAppear { player = AVPlayer(url: url); player?.play() }.onDisappear { player?.pause(); player = nil }
            .overlay(alignment: .topTrailing) {
                Button { dismiss() } label: { CupertinoGlyph.text(CupertinoGlyph.Xmark, size: 20).padding(14).background(.black.opacity(0.5), in: Circle()) }.padding(12)
            }
    }
}
