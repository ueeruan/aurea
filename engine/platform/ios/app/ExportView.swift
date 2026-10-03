// Espelho de editor/ExportScreen.kt: prévia no topo, escolhas agrupadas
// (Resolução, Taxa de quadros, Qualidade com o tamanho estimado, Formato), o
// resto em "Avançado", e durante o render o animador em pixel art com frases
// que trocam a cada 3 s. Render e encoder ficam no núcleo.
import SwiftUI
import UIKit
import AVKit
import QuickLook

struct ExportView: View {
    @EnvironmentObject private var model: AureaModel
    @Environment(\.dismiss) private var dismiss
    @State private var preview: UIImage?
    @State private var attempted = false
    @State private var cancelled = false
    @State private var sharing = false
    @State private var viewing = false
    @State private var advancedOpen = false
    /// Teclado do fps livre quando a tela está em tela cheia (fullScreenCover
    /// fica acima do overlay do ContentView).
    @State private var fpsKeypad: KeypadRequest?
    /// Lado menor → rótulo da ficha (o mesmo do Android).
    private static let standardResolutions: [(UInt32, String)] = [(480, "480p"), (720, "720p"), (1080, "1080p"), (1440, "2K"), (2160, "4K")]
    /// Atalhos; "Personalizado…" digita qualquer taxa de 1 a 240 (o teto dos encoders).
    private static let frameRates: [Double] = [24, 25, 30, 50, 60, 120]
    /// Mbps manuais do Avançado (0 = automático pela qualidade).
    private static let customBitrates: [UInt32] = [5, 10, 15, 25, 40, 60]
    private var resolutions: [UInt32] {
        let standard = Self.standardResolutions.map(\.0)
        let neural = min(model.compositionWidth, model.compositionHeight) * model.exportOptions.aiUpscale
        return neural > 0 ? Array(Set(standard + [neural])).sorted() : standard
    }
    private var seconds: Double { Double(model.engine.exportDuration(model.exportOptions.trimToContent)) / max(1, model.compositionFps) }
    private var fps: Double { model.exportOptions.fps > 0 ? model.exportOptions.fps : model.compositionFps }
    private var codec: String { model.exportOptions.codec == .hevc ? "HEVC" : "H.264" }
    /// A MESMA regra do encoder (BitratePolicy.hpp), nunca uma conta paralela.
    private var estimatedMbps: Double {
        Double(model.engine.exportBitrateBps(outputSize.0, height: outputSize.1, fps: fps,
                                             codec: model.exportOptions.codec, quality: model.exportOptions.quality,
                                             customMbps: model.exportOptions.bitrateMbps)) / 1_000_000
    }
    /// Vídeo + AAC 192 kbps + contêiner.
    private var estimatedBytes: Double { seconds > 0 ? (estimatedMbps * 1_000_000 + 192_000) * seconds / 8 * 1.015 : 0 }
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
                        if model.exporting { ExportRenderingView(notice: progressNotice) }
                        else if let url = model.exportedURL {
                            previewCard(width: bounds.size.width - 36)
                            Spacer().frame(height: 18)
                            done(url)
                        } else {
                            previewCard(width: bounds.size.width - 36)
                            Spacer().frame(height: 10)
                            if attempted, let notice = failureNotice { noticeView(notice, danger: !cancelled).padding(.bottom, 4) }
                            options
                        }
                        Spacer().frame(height: 16)
                        DonationCard(exporting: model.exporting)
                        Spacer().frame(height: 24)
                    }.padding(.horizontal, 18)
                }
                footer.padding(.horizontal, 18).padding(.vertical, 12)
            }
        }
        .background(AureaColors.background.ignoresSafeArea()).foregroundStyle(AureaColors.text)
        .preferredColorScheme(.dark).interactiveDismissDisabled(model.exporting)
        .overlay { if let request = fpsKeypad { NumericKeypadSheet(request: request) { fpsKeypad = nil }.id(request.id) } }
        .sheet(isPresented: $sharing) { if let url = model.exportedURL { ExportShareSheet(url: url) } }
        .sheet(isPresented: $viewing) {
            if let url = model.exportedURL {
                // Vídeo no player; PNG, GIF e .zip na Visualização Rápida do sistema.
                if model.exportedKind == .video { ExportMoviePlayer(url: url) } else { ExportQuickLook(url: url) }
            }
        }
        .task {
            if !model.exporting {
                model.exportOptions = ExportOptions()
                model.exportOptions.shortSide = min(1080, max(720, min(model.compositionWidth, model.compositionHeight)))
                model.exportOptions.bitrateMbps = 0
                model.exportOptions.quality = 1
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
        let height = min(220, max(1, width) / ratio)
        return ZStack {
            AureaColors.stage
            if let preview { Image(uiImage: preview).resizable().scaledToFit() }
            else { CupertinoGlyph.text(CupertinoGlyph.Film, size: 34, color: AureaColors.muted) }
        }.frame(width: height * ratio, height: height)
            .clipShape(RoundedRectangle(cornerRadius: 12))
            .overlay(RoundedRectangle(cornerRadius: 12).stroke(AureaColors.border, lineWidth: 1))
            .frame(maxWidth: .infinity).padding(.top, 8)
            .accessibilityElement(children: .ignore).accessibilityLabel(AureaText.t("editor_previa"))
    }
    @ViewBuilder private var options: some View {
        if model.exportOptions.kind != .video {
            imageOptions
        } else {
            VStack(alignment: .leading, spacing: 0) {
                // Linha-resumo logo abaixo da prévia: o que vai sair, de relance.
                Text("\(outputSize.0) × \(outputSize.1) · \(format(fps)) fps · \(codec) · \(formatTime(seconds))")
                    .font(.aurea(size: 13).monospacedDigit()).foregroundStyle(AureaColors.muted)
                    .frame(maxWidth: .infinity).multilineTextAlignment(.center).padding(.bottom, 6)
                formatGroup
                group("editor_resolucao") { resolutionOptions }
                group("editor_quadros_segundo") { frameRateOptions }
                group("editor_qualidade") { qualityOptions }
                advanced
            }
        }
    }
    /// Formato: Vídeo (MP4) | Quadro atual (PNG) | Sequência PNG (.zip) | GIF (o mesmo do Android).
    private static let kindKeys: [(ExportKind, String, String)] = [
        (.video, "exp2_format_video", "exp2_format_note"),
        (.frame, "exp2_format_frame", "exp2_format_note_frame"),
        (.sequence, "exp2_format_sequence", "exp2_format_note_sequence"),
        (.gif, "exp2_format_gif", "exp2_format_note_gif"),
    ]
    static func doneKey(_ kind: ExportKind) -> String {
        switch kind {
        case .video: return "editor_video_pronto"
        case .frame: return "exp2_done_frame"
        case .sequence: return "exp2_done_sequence"
        case .gif: return "exp2_done_gif"
        }
    }
    static func renderingKey(_ kind: ExportKind) -> String {
        switch kind {
        case .video: return "exp2_rendering"
        case .frame: return "exp2_rendering_frame"
        case .sequence: return "exp2_rendering_sequence"
        case .gif: return "exp2_rendering_gif"
        }
    }
    private var formatGroup: some View {
        let labels = Self.kindKeys.map { AureaText.t($0.1) }
        let current = Self.kindKeys.firstIndex { $0.0 == model.exportOptions.kind } ?? 0
        return group("editor_formato") {
            chips(labels, selected: labels[current]) { picked in
                if let index = labels.firstIndex(of: picked) { model.exportOptions.kind = Self.kindKeys[index].0 }
            }
            Text(AureaText.t(Self.kindKeys[current].2)).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).padding(.top, 8)
        }
    }
    /// Escolhas do export como imagem; dimensões, quadros e tamanho vêm do MOTOR.
    private var imageOptions: some View {
        let kind = model.exportOptions.kind
        let plan = model.imageExportPlan() ?? [:]
        let width = plan["width"]?.intValue ?? Int(model.compositionWidth)
        let height = plan["height"]?.intValue ?? Int(model.compositionHeight)
        let frames = plan["frames"]?.intValue ?? 1
        let planFps = plan["fps"]?.doubleValue ?? model.compositionFps
        let bytes = plan["bytes"]?.doubleValue ?? 0
        let duration = planFps > 0 ? Double(frames) / planFps : 0
        let framesText = AureaText.t("exp2_frames_count", frames)
        return VStack(alignment: .leading, spacing: 0) {
            Text(kind == .frame ? "\(width) × \(height) · PNG"
                 : "\(width) × \(height) · \(format(planFps)) fps · \(framesText) · \(formatTime(duration))")
                .font(.aurea(size: 13).monospacedDigit()).foregroundStyle(AureaColors.muted)
                .frame(maxWidth: .infinity).multilineTextAlignment(.center).padding(.bottom, 6)
            formatGroup
            if kind == .gif {
                group("exp2_gif_width") {
                    chips([320, 480, 720].map { "\($0) px" }, selected: "\(model.exportOptions.gifWidth) px") { picked in
                        model.exportOptions.gifWidth = UInt32(picked.replacingOccurrences(of: " px", with: "")) ?? 480
                    }
                }
                group("editor_quadros_segundo") {
                    chips([10.0, 15, 24, 30].map(format), selected: format(model.exportOptions.gifFps)) { picked in
                        model.exportOptions.gifFps = Double(picked.replacingOccurrences(of: ",", with: ".")) ?? 15
                    }
                }
            } else {
                group("editor_resolucao") {
                    let short = min(model.compositionWidth, model.compositionHeight)
                    let original = "\(AureaText.t("exp2_resolution_original")) (\(model.compositionWidth) × \(model.compositionHeight))"
                    let sides: [UInt32] = [0] + Self.standardResolutions.map(\.0).filter { $0 != short }
                    let label: (UInt32) -> String = { $0 == 0 ? original : resolutionLabel($0) }
                    chips(sides.map(label), selected: label(sides.contains(model.exportOptions.imageShortSide) ? model.exportOptions.imageShortSide : 0)) { picked in
                        model.exportOptions.imageShortSide = sides.first { label($0) == picked } ?? 0
                    }
                }
                if kind == .sequence { group("editor_quadros_segundo") { frameRateOptions } }
            }
            if kind != .frame {
                group("exp2_range") {
                    chips([AureaText.t("export_range_content"), AureaText.t("export_range_full")],
                          selected: AureaText.t(model.exportOptions.trimToContent ? "export_range_content" : "export_range_full")) {
                        model.exportOptions.trimToContent = $0 == AureaText.t("export_range_content")
                    }
                }
            }
            group("exp2_summary") {
                Text(AureaText.t("exp2_estimated_size", sizeLabel(bytes))).font(.aurea(size: 13).monospacedDigit())
                if kind != .frame { summary("editor_duracao", formatTime(duration)) }
                if kind.tooLong(frames) {
                    Text(AureaText.t("exp2_too_long")).font(.aurea(size: 13)).foregroundStyle(AureaColors.danger).padding(.top, 8)
                }
            }
        }
    }
    private func sizeLabel(_ bytes: Double) -> String {
        let megabytes = bytes / 1_000_000
        return megabytes >= 1000 ? AureaText.t("unit_gigabyte", format((megabytes / 100).rounded() / 10))
            : AureaText.t("unit_megabyte", max(1, Int(megabytes.rounded())))
    }
    private var resolutionOptions: some View {
        VStack(alignment: .leading, spacing: 0) {
            chips(resolutions.map(resolutionLabel), selected: blocked.contains(model.exportOptions.shortSide) ? nil : resolutionLabel(model.exportOptions.shortSide),
                  disabled: Set(blocked.map(resolutionLabel))) { picked in
                if let side = resolutions.first(where: { resolutionLabel($0) == picked }) { model.exportOptions.shortSide = side }
            }
            if !blocked.isEmpty {
                Text(exportLimitReason ?? AureaText.t("sh_export_above_device", blocked.map(resolutionLabel).joined(separator: ", ")))
                    .font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).padding(.top, 8)
            }
        }
    }
    private var frameRateOptions: some View {
        let projectFps = AureaText.t("sh_export_fps_from_project", format(model.compositionFps))
        let customLabel = AureaText.t("project_fps_custom")
        let presets = Self.frameRates.filter { abs($0 - model.compositionFps) > 0.01 }
        // ExportScreen.kt: a taxa digitada fora dos atalhos ganha a própria
        // ficha (escolhida) e "Personalizado…" continua ao lado.
        let chosen = model.exportOptions.fps
        let typed: [String] = chosen > 0 && !presets.contains { abs($0 - chosen) < 0.01 } ? [format(chosen)] : []
        return VStack(alignment: .leading, spacing: 0) {
            chips([projectFps] + presets.map(format) + typed + [customLabel], selected: chosen == 0 ? projectFps : format(chosen)) { picked in
                if picked == projectFps { model.exportOptions.fps = 0 }
                else if picked == customLabel { openFpsKeypad() }
                else { model.exportOptions.fps = Double(picked.replacingOccurrences(of: ",", with: ".")) ?? model.compositionFps }
            }
            if fps > 60.01 {
                Text(AureaText.t("export_fps_high_note")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).padding(.top, 8)
            }
        }
    }
    private func openFpsKeypad() {
        let request = KeypadRequest(title: AureaText.t("project_fps_custom_title"), value: Float(fps), unit: "fps",
                                    min: 1, max: 240, decimals: 3, onValue: { model.exportOptions.fps = Double($0) })
        if model.showExport { fpsKeypad = request } else { model.numericKeypad = request }
    }
    private var qualityOptions: some View {
        let labels = [AureaText.t("exp2_quality_low"), AureaText.t("exp2_quality_normal"), AureaText.t("exp2_quality_high")]
        return VStack(alignment: .leading, spacing: 0) {
            chips(labels, selected: model.exportOptions.bitrateMbps > 0 ? nil : labels[Int(min(2, model.exportOptions.quality))]) { picked in
                model.exportOptions.quality = UInt32(labels.firstIndex(of: picked) ?? 1)
                model.exportOptions.bitrateMbps = 0
            }
            Text(AureaText.t("exp2_estimated_line", estimatedSize, formatMbps(estimatedMbps)))
                .font(.aurea(size: 13).monospacedDigit()).padding(.top, 10)
        }
    }
    /// "Avançado": fechado por padrão; tudo o que não é do dia a dia mora aqui.
    private var advanced: some View {
        VStack(alignment: .leading, spacing: 0) {
            Button { withAnimation(.easeInOut(duration: 0.2)) { advancedOpen.toggle() } } label: {
                HStack {
                    Text(AureaText.t("exp2_advanced")).font(.aurea(size: 15, weight: .semibold))
                    Spacer(minLength: 0)
                    CupertinoGlyph.text(advancedOpen ? CupertinoGlyph.ChevronUp : CupertinoGlyph.ChevronDown, size: 16, color: AureaColors.muted)
                }.padding(14).contentShape(Rectangle())
            }.buttonStyle(.plain)
                .accessibilityAddTraits(advancedOpen ? .isSelected : [])
            if advancedOpen {
                VStack(alignment: .leading, spacing: 0) {
                    label("exp2_codec")
                    chips(["H.264", "HEVC"], selected: codec, disabled: hevcAvailable ? [] : ["HEVC"]) {
                        model.exportOptions.codec = $0 == "HEVC" ? .hevc : .h264
                    }
                    Text(hevcAvailable ? AureaText.t(model.exportOptions.codec == .hevc ? "editor_hevc_arquivo_menor_mesma_qualidade_alguns" : "editor_h_264_abre_qualquer_aparelho_rede")
                         : AureaText.t("ios_export_hevc_unavailable"))
                        .font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).padding(.top, 6)

                    label("exp2_bitrate")
                    let auto = AureaText.t("exp2_bitrate_auto")
                    chips([auto] + Self.customBitrates.map { String($0) },
                          selected: model.exportOptions.bitrateMbps > 0 ? String(model.exportOptions.bitrateMbps) : auto) {
                        model.exportOptions.bitrateMbps = $0 == auto ? 0 : UInt32($0) ?? 0
                    }

                    label("exp2_range")
                    chips([AureaText.t("export_range_content"), AureaText.t("export_range_full")],
                          selected: AureaText.t(model.exportOptions.trimToContent ? "export_range_content" : "export_range_full")) {
                        model.exportOptions.trimToContent = $0 == AureaText.t("export_range_content")
                    }

                    upscaleOptions
                    exportSummary
                }.padding(.horizontal, 14).padding(.bottom, 14)
            }
        }
        .background(AureaColors.surface, in: RoundedRectangle(cornerRadius: 14))
        .padding(.top, 16)
    }
    private var upscaleOptions: some View {
        VStack(alignment: .leading, spacing: 0) {
            label("ai_upscale_title")
            let off = AureaText.t("common_off")
            chips([off, "2×", "4×"], selected: model.exportOptions.aiUpscale == 0 ? off : "\(model.exportOptions.aiUpscale)×") { label in
                let factor: UInt32 = label == "2×" ? 2 : label == "4×" ? 4 : 0
                model.exportOptions.aiUpscale = factor
                let short = min(model.compositionWidth, model.compositionHeight)
                model.exportOptions.shortSide = factor > 0 ? short * factor : min(1080, max(720, short))
            }
            if model.exportOptions.aiUpscale > 0 {
                Text(AureaText.t("ai_upscale_note")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).padding(.top, 8)
                Text(upscaleDimensionsDescription)
                    .font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).padding(.top, 6)
            }
        }
    }
    private var exportSummary: some View {
        VStack(spacing: 0) {
            summary("editor_video", "\(outputSize.0) × \(outputSize.1) · \(format(fps)) fps · \(codec)")
            summary("editor_duracao", formatTime(seconds))
            summary("editor_tamanho_estimado", estimatedSize)
            summary("editor_cor", AureaText.t("editor_sdr_bt_709"))
        }.padding(.top, 12)
    }
    private var upscaleDimensionsDescription: String {
        let scale: UInt32 = model.exportOptions.aiUpscale
        guard scale > 0 else { return "" }
        let divisor: UInt32 = scale * 2
        let size: (UInt32, UInt32) = outputSize
        let inputWidth: UInt32 = ((size.0 + divisor - 1) / divisor) * 2
        let inputHeight: UInt32 = ((size.1 + divisor - 1) / divisor) * 2
        return AureaText.t("ai_upscale_dimensions", Int(scale), Int(inputWidth), Int(inputHeight), Int(size.0), Int(size.1))
    }
    /// Um grupo de escolhas: título pequeno e o conteúdo num cartão.
    private func group<Content: View>(_ key: String, @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            Text(AureaText.t(key).uppercased()).font(.aurea(size: 12, weight: .semibold)).tracking(0.6)
                .foregroundStyle(AureaColors.muted).padding(.top, 16).padding(.bottom, 8).padding(.leading, 2)
                .accessibilityAddTraits(.isHeader)
            VStack(alignment: .leading, spacing: 0) { content() }
                .frame(maxWidth: .infinity, alignment: .leading).padding(14)
                .background(AureaColors.surface, in: RoundedRectangle(cornerRadius: 14))
        }
    }
    private func label(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 13, weight: .semibold)).foregroundStyle(AureaColors.muted)
            .padding(.top, 12).padding(.bottom, 8)
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
    private func done(_ url: URL) -> some View {
        VStack(spacing: 0) {
            CupertinoGlyph.text(CupertinoGlyph.CheckmarkCircleFill, size: 44, color: AureaColors.success).padding(.top, 8)
            Text(AureaText.t(Self.doneKey(model.exportedKind))).font(.aurea(size: 22, weight: .bold)).padding(.top, 10)
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
                // A taxa vem do motor pela qualidade; só o Mbps manual vai como número.
                wideButton("editor_exportar", filled: true, height: 56) {
                    attempted = true; cancelled = false
                    model.startExport()
                }
            }
        }
    }
    private func wideButton(_ key: String, filled: Bool, height: CGFloat = 52, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(AureaText.t(key)).font(.aurea(size: 17, weight: .semibold))
                .foregroundStyle(filled ? AureaColors.onAccent : AureaColors.text)
                .frame(maxWidth: .infinity).frame(height: height)
                .background(filled ? AureaColors.accent : AureaColors.chip, in: RoundedRectangle(cornerRadius: 14))
        }.buttonStyle(.plain)
    }
    /// ESPELHO de export_frame_size (ExportRules.hpp) e do VideoExportRules.kt:
    /// lado maior em múltiplo de 16, menor par, quadrado fica
    /// quadrado ("480p" 16:9 = 848×480; 854 derrubava o encoder MediaTek).
    private func sizeFor(_ side: UInt32) -> (UInt32, UInt32) {
        let w = Double(model.compositionWidth), h = Double(model.compositionHeight)
        guard w > 0, h > 0 else { return (0, 0) }
        let shortest = min(w, h)
        let k = Double(side > 0 ? side : UInt32(shortest)) / shortest
        func align(_ v: Double, _ a: Double) -> UInt32 { UInt32(max(a, (v / a + 0.5).rounded(.down) * a)) }
        if w == h { let s = align(w * k, 2); return (s, s) }
        return w > h ? (align(w * k, 16), align(h * k, 2)) : (align(w * k, 2), align(h * k, 16))
    }
    private func fits(_ size: (UInt32, UInt32)) -> Bool {
        let cap = (model.composition["sizeCap"] as? [NSNumber] ?? []).map(\.uint32Value)
        guard cap.count >= 2, cap[0] > 0 else { return true }
        return max(size.0, size.1) <= cap[0] && min(size.0, size.1) <= cap[1]
    }
    private func resolutionLabel(_ side: UInt32) -> String {
        if let standard = Self.standardResolutions.first(where: { $0.0 == side }) { return standard.1 }
        let scale = model.exportOptions.aiUpscale
        return scale > 0 ? "\(side)p (\(scale)×)" : "\(side)p"
    }
    private var exportLimitReason: String? {
        let h = model.deviceReport["maxExportHeight"]?.intValue ?? 0
        let w = model.deviceReport["maxExportWidth"]?.intValue ?? 0
        guard h > 0, h < 2160 else { return nil }
        switch model.deviceReport["exportLimit"]?.intValue ?? 0 {
        case 2: return AureaText.t("ios_export_limit_encoder", h, w, h)
        case 3: return AureaText.t("ios_export_limit_memory", h)
        case 4: return AureaText.t("ios_export_limit_no_h264", h)
        default: return AureaText.t("ios_export_limit", h)
        }
    }
    private var estimatedSize: String {
        let megabytes = estimatedBytes / 1_000_000
        return megabytes >= 1000 ? AureaText.t("unit_gigabyte", format((megabytes / 100).rounded() / 10))
            : AureaText.t("unit_megabyte", max(1, Int(megabytes.rounded())))
    }
    private var failureNotice: String? {
        if model.exportCancelled || cancelled { return AureaText.t("ios_export_cancelled") }
        let result = (model.exportProgress["result"] as? NSNumber)?.intValue ?? 0
        if result == 28 { return AureaText.t("msg_sem_espaco_no_aparelho_libere_espaco") }
        if result != 0 {
            // O motivo do motor (ExportRules.hpp) no idioma do app; a frase crua
            // do motor só em português (é diagnóstico, não texto de tela).
            if let reason = AureaModel.exportFailureReason(model.exportProgress) { return AureaText.t("ios_export_failed_detail", reason) }
            let raw = model.exportProgress["message"] as? String ?? ""
            return AureaText.t("ios_export_failed_detail", raw.isEmpty || AureaText.language.resolved != .pt ? AureaText.t("ios_error_code", "\(result)") : raw)
        }
        return model.exportMessage
    }
    private var progressNotice: String {
        let flags = (model.exportProgress["flags"] as? NSNumber)?.uint32Value ?? 0
        var notices: [String] = []
        if model.exportOptions.aiUpscale > 0, let message = model.exportProgress["message"] as? String, message.hasPrefix("IA:") {
            notices.append(AureaEngineText.aiProgress(message))
        }
        if flags & AureaExportFlag.softwareEncoder.rawValue != 0 {
            notices.append(AureaText.t("ios_export_software_encoder", codec))
        }
        if flags & AureaExportFlag.thermalReduced.rawValue != 0 { notices.append(AureaText.t("ios_export_thermal")) }
        if flags & AureaExportFlag.frameFallback.rawValue != 0 { notices.append(AureaText.t("ios_export_frame_fallback")) }
        return notices.joined(separator: "\n")
    }
    private func formatMbps(_ mbps: Double) -> String {
        mbps >= 10 ? String(Int(mbps.rounded())) : String(format: "%.1f", mbps).replacingOccurrences(of: ".", with: ",")
    }
    private func format(_ value: Double) -> String { ExportFormat.number(value) }
    private func formatTime(_ seconds: Double) -> String { ExportFormat.time(seconds) }
}

private enum ExportFormat {
    static func number(_ value: Double) -> String {
        if value == value.rounded() { return String(Int(value)) }
        var text = String(format: "%.2f", value)
        while text.hasSuffix("0") { text.removeLast() }
        if text.hasSuffix(".") { text.removeLast() }
        return text.replacingOccurrences(of: ".", with: ",")
    }
    static func time(_ seconds: Double) -> String {
        let value = max(0, Int(seconds.rounded()))
        return value >= 3600 ? String(format: "%d:%02d:%02d", value / 3600, value / 60 % 60, value % 60)
            : String(format: "%d:%02d", value / 60, value % 60)
    }
}

/// Render em andamento: animador, porcentagem, barra, ETA e a frase da vez.
private struct ExportRenderingView: View {
    @EnvironmentObject private var model: AureaModel
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    let notice: String
    @State private var frame = 0
    @State private var phrase = 0
    private static let phrases = (1...10).map { "exp2_fun_\($0)" }

    var body: some View {
        let done = (model.exportProgress["framesDone"] as? NSNumber)?.intValue ?? 0
        let total = (model.exportProgress["framesTotal"] as? NSNumber)?.intValue ?? 0
        let fraction = min(1, max(0, Double(done) / Double(max(1, total))))
        let speed = (model.exportProgress["fps"] as? NSNumber)?.doubleValue ?? 0
        let eta = (model.exportProgress["etaSeconds"] as? NSNumber)?.doubleValue ?? 0
        let publishing = model.exportPublishing
        return VStack(spacing: 0) {
            PixelAnimatorView(frame: frame)
                .frame(width: PixelAnimator.viewWidth, height: PixelAnimator.viewWidth * CGFloat(PixelAnimator.height) / CGFloat(PixelAnimator.width))
                .padding(.top, 20)
            Text(AureaText.t(publishing ? "editor_salvando_galeria" : ExportView.renderingKey(model.exportedKind)))
                .font(.aurea(size: 17, weight: .semibold)).padding(.top, 18)
            Text(publishing ? "100%" : "\(Int((fraction * 100).rounded()))%")
                .font(.aurea(size: 34, weight: .bold).monospacedDigit()).padding(.top, 6)
            GeometryReader { bounds in
                ZStack(alignment: .leading) {
                    AureaColors.chip
                    AureaColors.accent.frame(width: bounds.size.width * (publishing ? 1 : fraction))
                        .clipShape(RoundedRectangle(cornerRadius: 4))
                }.clipShape(RoundedRectangle(cornerRadius: 4))
            }.frame(height: 8).padding(.top, 12)
                .accessibilityElement().accessibilityValue("\(Int((fraction * 100).rounded()))%")
            if !publishing {
                if eta > 0 {
                    Text(AureaText.t("exp2_eta", ExportFormat.time(eta))).font(.aurea(size: 14).monospacedDigit()).padding(.top, 10)
                }
                // Com o "faltam" grande em cima, a linha de detalhe não repete o tempo.
                Text(eta > 0 ? AureaText.t("exp2_frames", String(done), String(total), ExportFormat.number(speed))
                     : AureaText.t("sh_export_progress", String(done), String(total), ExportFormat.number(speed), ExportFormat.time(eta)))
                    .font(.aurea(size: 12).monospacedDigit()).foregroundStyle(AureaColors.muted)
                    .multilineTextAlignment(.center).padding(.top, eta > 0 ? 4 : 10)
                Text(AureaText.t(Self.phrases[phrase % Self.phrases.count]))
                    .font(.aurea(size: 15, weight: .medium)).foregroundStyle(AureaColors.accent)
                    .multilineTextAlignment(.center).frame(maxWidth: .infinity, minHeight: 22).padding(.top, 16)
                    .id(phrase).transition(.opacity)
                Text(AureaText.t("editor_mantenha_aurea_aberto_ate_terminar")).font(.aurea(size: 13))
                    .foregroundStyle(AureaColors.muted).multilineTextAlignment(.center).padding(.top, 10)
                if !notice.isEmpty {
                    Text(notice).font(.aurea(size: 14)).foregroundStyle(AureaColors.warning)
                        .frame(maxWidth: .infinity, alignment: .leading).padding(14)
                        .background(AureaColors.surface, in: RoundedRectangle(cornerRadius: 10)).padding(.top, 12)
                }
            }
        }
        .frame(maxWidth: .infinity)
        // "Reduzir movimento": o animador fica parado no 1º quadro.
        .task(id: reduceMotion) {
            guard !reduceMotion else { frame = 0; return }
            while !Task.isCancelled {
                try? await Task.sleep(nanoseconds: PixelAnimator.frameNanos)
                frame = (frame + 1) % PixelAnimator.frames.count
            }
        }
        .task {
            while !Task.isCancelled {
                try? await Task.sleep(nanoseconds: 3_000_000_000)
                if reduceMotion { phrase += 1 } else { withAnimation(.easeInOut(duration: 0.3)) { phrase += 1 } }
            }
        }
    }
}

/// O animador em pixel art: um bonequinho de boina na mesa, desenhando quadros
/// no papel. 4 quadros de 20×14 "pixels", um caractere por pixel (paleta
/// abaixo). Os MESMOS dados estão em ExportScreen.kt (PixelAnimator).
enum PixelAnimator {
    static let width = 20
    static let height = 14
    static let frameNanos: UInt64 = 180_000_000
    /// Largura na tela (pt): 10 pt por pixel do sprite, como os 200 dp do Android.
    static let viewWidth: CGFloat = 200
    static let palette: [Character: Color] = [
        "K": Color(hex: 0x1E1B2E),
        "B": Color(hex: 0xE5484D),
        "H": Color(hex: 0x6B4226),
        "S": Color(hex: 0xF6C9A0),
        "R": Color(hex: 0xE86A8A),
        "T": Color(hex: 0x4C7DFF),
        "D": Color(hex: 0x9A6232),
        "P": Color(hex: 0xF4F1E8),
        "Y": Color(hex: 0xFFC53D),
    ]
    private static let head = [
        "....................",
        "...BBBBB............",
        "..BBBBBBB...........",
        "..KHHHHHK...........",
        "..KSSSSSK...........",
    ]
    private static let eyes = "..KSKSKSK..........."
    private static let blink = "..KSSSSSK..........."
    private static let cheeks = "..KRSSSRK..........."
    private static let mouth = "...KSRSK............"
    private static let neck = "....KKK............."
    private static let body = "..TTTTTTT...PPPPPP.."
    private static let desk = ["DDDDDDDDDDDDDDDDDDDD", ".DD..............DD."]
    static let frames: [[String]] = [
        head + [eyes, cheeks, mouth, neck, body,
                ".TTTTTTTTSSSYPPPPP..", ".TTTTTTTT...PPPPPP.."] + desk,
        head + [eyes, cheeks, mouth, neck, body,
                ".TTTTTTTTTSSSYPPPP..", ".TTTTTTTT...KPPPPP.."] + desk,
        head + [eyes, cheeks, "...KSRSK........Y...", neck, body,
                ".TTTTTTTTTTSSSYPPP..", ".TTTTTTTT...KKPPPP.."] + desk,
        head + [blink, "..KRSSSRK.......Y...", mouth, neck, body,
                ".TTTTTTTTTTTSSSYPP..", ".TTTTTTTT...KKKPPP.."] + desk,
    ]
}

private struct PixelAnimatorView: View {
    let frame: Int
    var body: some View {
        Canvas { context, size in
            let rows = PixelAnimator.frames[frame % PixelAnimator.frames.count]
            let cell = min(size.width / CGFloat(PixelAnimator.width), size.height / CGFloat(PixelAnimator.height))
            let ox = (size.width - cell * CGFloat(PixelAnimator.width)) / 2
            let oy = (size.height - cell * CGFloat(PixelAnimator.height)) / 2
            for (y, row) in rows.enumerated() {
                for (x, c) in row.enumerated() {
                    guard let color = PixelAnimator.palette[c] else { continue }
                    // +0,5 pt: sem frestas entre pixels vizinhos.
                    let rect = CGRect(x: ox + CGFloat(x) * cell, y: oy + CGFloat(y) * cell, width: cell + 0.5, height: cell + 0.5)
                    context.fill(Path(rect), with: .color(color))
                }
            }
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(AureaText.t("exp2_animator_a11y"))
        .accessibilityAddTraits(.isImage)
    }
}

private struct ExportShareSheet: UIViewControllerRepresentable {
    let url: URL
    func makeUIViewController(context: Context) -> UIActivityViewController { UIActivityViewController(activityItems: [url], applicationActivities: nil) }
    func updateUIViewController(_ controller: UIActivityViewController, context: Context) {}
}
/// PNG, GIF (animado) e .zip pela Visualização Rápida do sistema.
private struct ExportQuickLook: UIViewControllerRepresentable {
    let url: URL
    func makeCoordinator() -> Coordinator { Coordinator(url: url) }
    func makeUIViewController(context: Context) -> UINavigationController {
        let controller = QLPreviewController()
        controller.dataSource = context.coordinator
        return UINavigationController(rootViewController: controller)
    }
    func updateUIViewController(_ controller: UINavigationController, context: Context) {}
    final class Coordinator: NSObject, QLPreviewControllerDataSource {
        let url: URL
        init(url: URL) { self.url = url }
        func numberOfPreviewItems(in controller: QLPreviewController) -> Int { 1 }
        func previewController(_ controller: QLPreviewController, previewItemAt index: Int) -> QLPreviewItem { url as NSURL }
    }
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
