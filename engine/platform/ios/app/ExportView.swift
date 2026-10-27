// Omega adicionar (área 08, P0-8..P0-11): a página "Exportar e compartilhar",
// clara como a da referência (D1). Estimativas (espaço disponível e tamanho), a
// lista de cartões com rádio (Vídeo com a resolução embutida, Arquivo do
// projeto, Quadro atual, Sequência, GIF), a sub-página de ajustes que desliza
// (200 ms) e o botão "Exportar" azul-marinho. O progresso é o painel sobre véu;
// o resultado, a página azul-marinho com o player, Salvar, Compartilhar e Fechar.
// Todo o motor de exportação 2.0 (escada, retomada, segundo plano, avisos)
// continua no AureaModel. Espelho de editor/ExportScreen.kt.
import SwiftUI
import UIKit
import AVKit
import QuickLook
import ImageIO

/// Os itens da lista (na ordem da referência, só o que o AUREA tem; D4).
private enum ExportItem: String, CaseIterable {
    case video, package, frame, sequence, gif
    var kind: ExportKind? {
        switch self {
        case .video: return .video
        case .package: return nil
        case .frame: return .frame
        case .sequence: return .sequence
        case .gif: return .gif
        }
    }
    var nameKey: String {
        switch self {
        case .video: return "om_exp_opt_video"
        case .package: return "om_exp_opt_package"
        case .frame: return "om_exp_opt_frame"
        case .sequence: return "om_exp_opt_sequence"
        case .gif: return "om_exp_opt_gif"
        }
    }
    var noteKey: String { "om_exp_note_" + rawValue }
    var glyph: Character {
        switch self {
        case .video: return CupertinoGlyph.Film
        case .package: return CupertinoGlyph.DocOnDoc
        case .frame: return CupertinoGlyph.Photo
        case .sequence: return CupertinoGlyph.SquareStack3dUp
        case .gif: return CupertinoGlyph.PlayRectangle
        }
    }
}

struct ExportView: View {
    @EnvironmentObject private var model: AureaModel
    @Environment(\.dismiss) private var dismiss
    @Environment(\.layoutDirection) private var direction
    @State private var preview: UIImage?
    @State private var attempted = false
    @State private var cancelled = false
    @State private var item: ExportItem = .video
    @State private var settings: ExportItem?
    @State private var includeMedia = true
    @State private var sharedProject: SharedProjectFile?
    @State private var freeBytes: Int64 = -1
    @State private var packageSizes: (project: Int64, images: Int64, videos: Int64, audio: Int64, other: Int64)?
    /// Teclado do fps livre (fullScreenCover fica acima do overlay do ContentView).
    @State private var fpsKeypad: KeypadRequest?
    /// Lado menor → rótulo (o mesmo do Android).
    static let standardResolutions: [(UInt32, String)] = [(480, "480p"), (720, "720p"), (1080, "1080p"), (1440, "2K"), (2160, "4K")]
    /// Atalhos do "Mais…"; "Personalizado…" digita qualquer taxa de 1 a 240.
    static let frameRates: [Double] = [24, 25, 30, 50, 60, 120]
    /// Mbps manuais do Avançado (0 = automático pela qualidade).
    static let customBitrates: [UInt32] = [5, 10, 15, 25, 40, 60]
    /// GIF acima disto: aviso antes (o motor ainda aceita até 1800 quadros).
    static let gifRecommendedFrames = 450

    var body: some View {
        ZStack {
            if model.exportedURL != nil && !model.exporting {
                ExportResultPage(preview: preview, sequenceFrames: sequenceFrames) { close() }
            } else {
                page
                if model.exporting { ExportProgressPanel(cancelled: $cancelled) }
            }
        }
        .interactiveDismissDisabled(model.exporting)
        .overlay { if let request = fpsKeypad { NumericKeypadSheet(request: request) { fpsKeypad = nil }.id(request.id) } }
        .sheet(item: $sharedProject) { file in ProjectFileShareSheet(url: file.url) }
        .task {
            guard !model.exporting else { return }
            model.exportOptions = ExportOptions()
            model.exportOptions.shortSide = min(1080, max(720, min(model.compositionWidth, model.compositionHeight)))
            model.exportOptions.bitrateMbps = 0
            model.exportOptions.quality = 60
            refreshFree()
            let project = model.projectGeneration
            if let frame = await model.capturePreviewFrame(640),
               !Task.isCancelled, model.projectGeneration == project {
                preview = UIImage.fromRGBA(frame.data, width: Int(frame.width), height: Int(frame.height))
            }
        }
    }

    private var page: some View {
        VStack(spacing: 0) {
            topBar
            ZStack {
                if let settings {
                    ScrollView { VStack(spacing: 0) { estimates(settings); settingsPage(settings) }.padding(.bottom, 10) }
                        .transition(.asymmetric(insertion: .offset(x: shift(135)).combined(with: .opacity),
                                                removal: .offset(x: shift(-135)).combined(with: .opacity)))
                } else {
                    ScrollView {
                        VStack(spacing: 0) {
                            if attempted, let notice = failureNotice { noticeView(notice, danger: !cancelled) }
                            estimates(item)
                            optionList
                        }.padding(.bottom, 10)
                    }
                    .transition(.asymmetric(insertion: .offset(x: shift(-135)).combined(with: .opacity),
                                            removal: .offset(x: shift(135)).combined(with: .opacity)))
                }
            }
            .frame(maxHeight: .infinity)
            .animation(.easeOut(duration: 0.2), value: settings)
            exportButton
        }
        .background(AureaColors.exportPage.ignoresSafeArea())
        .foregroundStyle(AureaColors.exportText)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("export.page")
    }

    /// Sub-página: entra de +135 para 0 (a lista, do lado oposto); em árabe espelha.
    private func shift(_ value: CGFloat) -> CGFloat { direction == .rightToLeft ? -value : value }

    // MARK: Barra

    private var topBar: some View {
        VStack(spacing: 0) {
            ZStack {
                Text(settings.map { AureaText.t($0.nameKey) } ?? AureaText.t("om_exp_title"))
                    .font(.aurea(size: 17, weight: .bold)).foregroundStyle(AureaColors.exportText).lineLimit(1).padding(.horizontal, 52)
                HStack(spacing: 0) {
                    Button {
                        if settings != nil { settings = nil } else { close() }
                    } label: {
                        CupertinoGlyph.text(settings != nil ? CupertinoGlyph.ChevronLeft : CupertinoGlyph.Xmark, size: 20,
                                            color: model.exporting ? AureaColors.exportAction : AureaColors.exportText)
                            .mirrorsInRtl(settings != nil)
                            .frame(width: 44, height: 44).contentShape(Rectangle())
                    }
                    .buttonStyle(.plain).disabled(model.exporting)
                    .accessibilityLabel(AureaText.t(settings != nil ? "om_add_back" : "editor_fechar"))
                    .accessibilityIdentifier(settings != nil ? "export.back" : "export.close")
                    Spacer(minLength: 0)
                }
            }
            .frame(height: 49)
            .background(AureaColors.exportCard)
            Rectangle().fill(AureaColors.exportLine).frame(height: 0.7)
        }
    }

    private func close() {
        guard !model.exporting else { return }
        dismiss()
        model.showExport = false
    }

    // MARK: Estimativas

    private func estimates(_ which: ExportItem) -> some View {
        VStack(spacing: 0) {
            estimateRow("om_exp_space", freeBytes >= 0 ? sizeLabel(Double(freeBytes)) : "—", "export.estimate.space")
            Rectangle().fill(AureaColors.exportLine).frame(height: 0.7)
            if which == .package {
                if includeMedia, let sizes = packageSizes {
                    estimateRow("om_exp_package_images", sizeLabel(Double(sizes.images)), "export.estimate.images")
                    estimateRow("om_exp_package_videos", sizeLabel(Double(sizes.videos)), "export.estimate.videos")
                    estimateRow("om_exp_package_audio", sizeLabel(Double(sizes.audio)), "export.estimate.audio")
                }
                let total = packageSizes.map { Double($0.project + (includeMedia ? $0.images + $0.videos + $0.audio + $0.other : 0)) }
                estimateRow("om_exp_package_size", total.map(sizeLabel) ?? "—", "export.estimate.size")
            } else {
                estimateRow("om_exp_size", sizeLabel(estimatedBytes(which)), "export.estimate.size")
            }
        }
        .background(AureaColors.exportCard, in: RoundedRectangle(cornerRadius: 6))
        .padding(10)
        .onAppear { if which == .package { loadPackageSizes() } }
    }

    private func estimateRow(_ key: String, _ value: String, _ tag: String) -> some View {
        HStack {
            Text(AureaText.t(key)).font(.aurea(size: 15)).lineLimit(1).minimumScaleFactor(0.4)
            Spacer(minLength: 8)
            Text(ltrIsolado(value)).font(.aurea(size: 15, weight: .semibold).monospacedDigit())
        }
        .foregroundStyle(AureaColors.exportText)
        .padding(.horizontal, 14).frame(height: 44)
        .accessibilityElement(children: .combine)
        .accessibilityIdentifier(tag)
    }

    private func refreshFree() {
        let values = try? AureaPaths.documents.resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey])
        freeBytes = values?.volumeAvailableCapacityForImportantUsage ?? -1
    }

    /// A soma dos tamanhos da mídia do projeto, por tipo (AssetKind: 1 vídeo, 2 áudio, 3 imagem).
    private func loadPackageSizes() {
        guard let path = model.projectURL?.path else { return }
        let engine = model.engine
        DispatchQueue.global(qos: .userInitiated).async {
            let size: (String) -> Int64 = { p in ((try? FileManager.default.attributesOfItem(atPath: p))?[.size] as? NSNumber)?.int64Value ?? 0 }
            var images: Int64 = 0, videos: Int64 = 0, audio: Int64 = 0, other: Int64 = 0
            if let refs = engine.projectFileMedia(path) {
                var i = 0
                while i + 3 < refs.count {
                    let resolved = refs[i + 1].hasPrefix("file://") ? String(refs[i + 1].dropFirst(7)) : refs[i + 1]
                    let bytes = size(resolved)
                    switch Int(refs[i + 3]) ?? 0 { case 1: videos += bytes; case 2: audio += bytes; case 3: images += bytes; default: other += bytes }
                    i += 4
                }
            }
            let result = (size(path), images, videos, audio, other)
            DispatchQueue.main.async { packageSizes = result }
        }
    }

    private func estimatedBytes(_ which: ExportItem) -> Double {
        var options = model.exportOptions
        if let kind = which.kind { options.kind = kind }
        if which == .video {
            let seconds = Double(model.engine.exportDuration(options.trimToContent)) / max(1, model.compositionFps)
            let size = sizeFor(options.shortSide)
            let fps = options.fps > 0 ? options.fps : model.compositionFps
            let mbps = Double(model.engine.exportBitrateBps(size.0, height: size.1, fps: fps, codec: options.codec,
                                                            quality: options.quality, customMbps: options.bitrateMbps)) / 1_000_000
            return seconds > 0 ? (mbps * 1_000_000 + 192_000) * seconds / 8 * 1.015 : 0
        }
        guard which.kind == model.exportOptions.kind else {
            // O plano do motor lê as opções do modelo: só o item escolhido tem plano.
            return 0
        }
        return model.imageExportPlan()?["bytes"]?.doubleValue ?? 0
    }

    // MARK: Lista

    private var optionList: some View {
        VStack(spacing: 0) {
            ForEach(ExportItem.allCases, id: \.self) { entry in
                VStack(spacing: 0) {
                    HStack(spacing: 0) {
                        Button { pick(entry) } label: {
                            HStack(spacing: 0) {
                                radio(entry == item).frame(width: 52)
                                CupertinoGlyph.text(entry.glyph, size: 22, color: AureaColors.exportText)
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(AureaText.t(entry.nameKey)).font(.aurea(size: 17, weight: .bold)).foregroundStyle(AureaColors.exportText).lineLimit(1)
                                    Text(AureaText.t(entry.noteKey)).font(.aurea(size: 11)).foregroundStyle(AureaColors.exportHint).lineLimit(1)
                                }.padding(.leading, 12)
                                Spacer(minLength: 0)
                            }.frame(height: 70).contentShape(Rectangle())
                        }
                        .buttonStyle(.plain)
                        .accessibilityLabel(AureaText.t(entry.nameKey))
                        .accessibilityAddTraits(entry == item ? [.isButton, .isSelected] : .isButton)
                        .accessibilityIdentifier("export.option.\(entry.rawValue)")
                        let hasSettings = entry != .package
                        Button {
                            if hasSettings { pick(entry); settings = entry } else { showInfo(entry) }
                        } label: {
                            CupertinoGlyph.text(hasSettings ? CupertinoGlyph.ChevronRight : CupertinoGlyph.InfoCircle, size: 18, color: AureaColors.exportAction)
                                .mirrorsInRtl(hasSettings)
                                .frame(width: 52, height: 70).contentShape(Rectangle())
                        }
                        .buttonStyle(.plain)
                        .accessibilityLabel(AureaText.t(hasSettings ? "om_exp_settings_named" : "om_exp_info_named", AureaText.t(entry.nameKey)))
                        .accessibilityIdentifier("export.option.\(entry.rawValue)." + (hasSettings ? "settings" : "info"))
                    }
                    if entry == .video && item == .video {
                        Rectangle().fill(AureaColors.exportPage).frame(height: 2)
                        resolutionDropdown(tag: "export.option.video.resolution").padding(10)
                    }
                }
                .background(AureaColors.exportCard, in: RoundedRectangle(cornerRadius: 6))
                .padding(.vertical, 5)
            }
        }
        .padding(.horizontal, 10)
    }

    private func pick(_ entry: ExportItem) {
        item = entry
        if let kind = entry.kind { model.exportOptions.kind = kind }
        if entry == .package { loadPackageSizes() }
    }

    private func radio(_ on: Bool) -> some View {
        ZStack {
            Circle().stroke(on ? AureaColors.lightAccentInk : AureaColors.exportText.opacity(0.35), lineWidth: 2).frame(width: 18, height: 18)
            if on { Circle().fill(AureaColors.lightAccentInk).frame(width: 9, height: 9) }
        }
    }

    private func resolutionDropdown(tag: String) -> some View {
        let options = model.exportOptions
        let size = sizeFor(options.shortSide)
        let current = Self.standardResolutions.first { $0.0 == options.shortSide }?.1 ?? "\(options.shortSide)p"
        return VStack(alignment: .leading, spacing: 6) {
            AureaDropdown(value: "\(current) · \(ltrIsolado("\(size.0) × \(size.1)"))", width: nil, tag: tag,
                          accessibilityLabel: AureaText.t("om_exp_resolution")) {
                Self.standardResolutions.map { side, label in
                    let s = sizeFor(side)
                    return MenuEntry.item(label, sublabel: ltrIsolado("\(s.0) × \(s.1)"), selected: side == options.shortSide,
                                          enabled: fits(s), tag: "\(tag).\(side)") { model.exportOptions.shortSide = side }
                }
            }
            .frame(height: 44)
            let blocked = Self.standardResolutions.filter { !fits(sizeFor($0.0)) }.map(\.1)
            if !blocked.isEmpty {
                Text(exportLimitReason ?? AureaText.t("sh_export_above_device", blocked.joined(separator: ", ")))
                    .font(.aurea(size: 12)).foregroundStyle(AureaColors.exportHint)
            }
        }
    }

    // MARK: Sub-páginas (P0-9)

    @ViewBuilder private func settingsPage(_ which: ExportItem) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            switch which {
            case .video:
                label("om_exp_resolution")
                resolutionDropdown(tag: "export.settings.resolution")
                label("exp2_codec")
                segmented([("h264", "H.264/AVC"), ("hevc", "H.265/HEVC")], selected: model.exportOptions.codec == .hevc ? "hevc" : "h264",
                          tag: "export.settings.codec", disabled: hevcAvailable ? [] : ["hevc"]) { picked in
                    model.exportOptions.codec = picked == "hevc" ? .hevc : .h264
                    // HEVC só no Automático: Compatível e Software são H.264 (ExportAttempts.hpp).
                    if picked == "hevc" { model.exportOptions.encoder = 0 }
                }
                if !hevcAvailable { hint(AureaText.t("ios_export_hevc_unavailable")) }
                fpsRow
                qualityRow
                advanced
            case .package:
                label("om_exp_opt_package")
                segmented([("media", AureaText.t("project_file_include_media")), ("project", AureaText.t("project_file_without_media"))],
                          selected: includeMedia ? "media" : "project", tag: "export.settings.package") { includeMedia = $0 == "media" }
                hint(AureaText.t("om_exp_package_info"))
            case .frame, .sequence:
                label("om_exp_resolution")
                let short = min(model.compositionWidth, model.compositionHeight)
                let original = "\(AureaText.t("exp2_resolution_original")) (\(ltrIsolado("\(model.compositionWidth) × \(model.compositionHeight)")))"
                let sides: [UInt32] = [0] + Self.standardResolutions.map(\.0).filter { $0 != short }
                let name: (UInt32) -> String = { side in side == 0 ? original : Self.standardResolutions.first { $0.0 == side }?.1 ?? "\(side)p" }
                AureaDropdown(value: name(sides.contains(model.exportOptions.imageShortSide) ? model.exportOptions.imageShortSide : 0), width: nil,
                              tag: "export.settings.resolution", accessibilityLabel: AureaText.t("om_exp_resolution")) {
                    sides.map { side in
                        MenuEntry.item(name(side), selected: side == model.exportOptions.imageShortSide, tag: "export.settings.resolution.\(side)") {
                            model.exportOptions.imageShortSide = side
                        }
                    }
                }.frame(height: 44)
                if which == .sequence {
                    fpsRow
                    rangeRow
                }
                backgroundRow
            case .gif:
                label("exp2_gif_width")
                segmented([320, 480, 720].map { ("\($0)", "\($0) px") }, selected: "\(model.exportOptions.gifWidth)", tag: "export.settings.gifWidth") {
                    model.exportOptions.gifWidth = UInt32($0) ?? 480
                }
                label("editor_quadros_segundo", value: AureaText.t("om_exp_fps_value", format(model.exportOptions.gifFps)))
                segmented([10.0, 15, 24, 30].map { (format($0), format($0)) }, selected: format(model.exportOptions.gifFps), tag: "export.settings.gifFps") {
                    model.exportOptions.gifFps = Double($0.replacingOccurrences(of: ",", with: ".")) ?? 15
                }
                rangeRow
                backgroundRow
            }
            if which == .sequence || which == .gif, model.exportOptions.kind == which.kind, let plan = model.imageExportPlan() {
                let frames = plan["frames"]?.intValue ?? 0
                hint([ltrIsolado("\(plan["width"]?.intValue ?? 0) × \(plan["height"]?.intValue ?? 0)"), AureaText.t("exp2_frames_count", frames)].joined(separator: " · "))
                if (which.kind ?? .gif).tooLong(frames) { hint(AureaText.t("exp2_too_long"), danger: true) }
            }
        }
        .padding(14)
        .background(AureaColors.exportCard, in: RoundedRectangle(cornerRadius: 6))
        .padding(.horizontal, 10)
    }

    private func label(_ key: String, value: String? = nil) -> some View {
        HStack {
            Text(AureaText.t(key)).font(.aurea(size: 17)).foregroundStyle(AureaColors.exportText)
            Spacer(minLength: 8)
            if let value { Text(ltrIsolado(value)).font(.aurea(size: 13).monospacedDigit()).foregroundStyle(AureaColors.exportHint) }
        }.padding(.top, 14).padding(.bottom, 7)
    }

    private func hint(_ text: String, danger: Bool = false) -> some View {
        Text(text).font(.aurea(size: 12)).foregroundStyle(danger ? AureaColors.lightDanger : AureaColors.exportHint)
            .fixedSize(horizontal: false, vertical: true).padding(.top, 8)
    }

    /// O segmentado da referência: 44, raio 5, padding 3; o ativo azul-marinho com texto de destaque.
    private func segmented(_ options: [(String, String)], selected: String?, tag: String, disabled: Set<String> = [],
                           onPick: @escaping (String) -> Void) -> some View {
        HStack(spacing: 0) {
            ForEach(options, id: \.0) { key, text in
                let on = key == selected, off = disabled.contains(key)
                Button { if !on && !off { onPick(key) } } label: {
                    Text(text).font(.aurea(size: 14, weight: .semibold)).lineLimit(1).minimumScaleFactor(0.7)
                        .foregroundStyle(off ? AureaColors.exportAction : on ? AureaColors.exportOnPrimary : AureaColors.exportText)
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                        .background(on ? AureaColors.exportPrimary : Color.clear, in: RoundedRectangle(cornerRadius: 5))
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain).disabled(off)
                .accessibilityAddTraits(on ? [.isButton, .isSelected] : .isButton)
                .accessibilityIdentifier("\(tag).\(key)")
            }
        }
        .padding(3)
        .frame(height: 44)
        .background(AureaColors.exportPage, in: RoundedRectangle(cornerRadius: 5))
    }

    /// fps: o valor "N fps" e Baixo | Reduzido | Cheio pela regra do motor
    /// (export_fps_option, centésimos, truncado); "Mais…" abre as taxas do AUREA.
    private var fpsRow: some View {
        let projectFps = model.compositionFps
        let fps100 = UInt32(max(0, (projectFps * 100).rounded()))
        let values = (0..<3).map { Double(AureaEngine.exportFpsOption(fps100, option: UInt32($0))) / 100 }
        let chosen = model.exportOptions.fps
        let current = chosen > 0 ? chosen : projectFps
        let selected: String? = chosen <= 0 ? "full" : abs(chosen - values[0]) < 0.005 ? "low"
            : abs(chosen - values[1]) < 0.005 ? "reduced" : abs(chosen - values[2]) < 0.005 ? "full" : nil
        return VStack(alignment: .leading, spacing: 0) {
            label("editor_quadros_segundo", value: AureaText.t("om_exp_fps_value", format(current)))
            segmented([("low", AureaText.t("om_exp_fps_low")), ("reduced", AureaText.t("om_exp_fps_reduced")), ("full", AureaText.t("om_exp_fps_full"))],
                      selected: selected, tag: "export.settings.fps") { picked in
                model.exportOptions.fps = picked == "low" ? values[0] : picked == "reduced" ? values[1] : 0
            }
            FpsMoreLink(current: current) { model.exportOptions.fps = $0 } openKeypad: {
                fpsKeypad = KeypadRequest(title: AureaText.t("project_fps_custom_title"), value: Float(current), unit: "fps",
                                          min: 1, max: 240, decimals: 3, onValue: { model.exportOptions.fps = Double($0) })
            }
            if current > 60.01 { hint(AureaText.t("export_fps_high_note")) }
        }
    }

    /// Qualidade: o valor à direita, "Baixa" e "Alta" nas pontas e o controle de 20 a 100.
    private var qualityRow: some View {
        let quality = model.exportOptions.quality >= 20 ? model.exportOptions.quality : (model.exportOptions.quality == 0 ? 20 : model.exportOptions.quality == 2 ? 100 : 60)
        let manual = model.exportOptions.bitrateMbps > 0
        return VStack(alignment: .leading, spacing: 0) {
            label("editor_qualidade", value: AureaText.t("om_exp_quality_value", Int(quality)))
            HStack(spacing: 10) {
                Text(AureaText.t("om_exp_quality_low")).font(.aurea(size: 13)).foregroundStyle(AureaColors.exportHint)
                Slider(value: Binding(get: { Double(quality) }, set: {
                    model.exportOptions.quality = UInt32($0.rounded()); model.exportOptions.bitrateMbps = 0
                }), in: 20...100, step: 1)
                .tint(AureaColors.lightAccentInk)
                .disabled(manual)
                .frame(height: 35)
                .accessibilityLabel(AureaText.t("editor_qualidade"))
                .accessibilityValue("\(quality)%")
                .accessibilityIdentifier("export.settings.quality")
                Text(AureaText.t("om_exp_quality_high")).font(.aurea(size: 13)).foregroundStyle(AureaColors.exportHint)
            }
        }
    }

    private var rangeRow: some View {
        VStack(alignment: .leading, spacing: 0) {
            label("exp2_range")
            segmented([("content", AureaText.t("export_range_content")), ("full", AureaText.t("export_range_full"))],
                      selected: model.exportOptions.trimToContent ? "content" : "full", tag: "export.settings.range") {
                model.exportOptions.trimToContent = $0 == "content"
            }
        }
    }

    /// P2-4: "Fundo: <cor | Transparente>" com o atalho para os ajustes do projeto.
    private var backgroundRow: some View {
        let values = (model.composition["background"] as? [NSNumber] ?? []).map(\.doubleValue)
        let transparent = values.count >= 4 && values[3] < 0.5
        let color = values.count >= 3 ? Color(red: values[0], green: values[1], blue: values[2]) : Color.black
        let hex = values.count >= 3 ? String(format: "#%02X%02X%02X", Int((values[0] * 255).rounded()), Int((values[1] * 255).rounded()), Int((values[2] * 255).rounded())) : "#000000"
        let value = transparent ? AureaText.t("sh_bg_transparent") : hex
        return Button {
            close()
            // Os ajustes do projeto (ContentView) abrem depois que a página fecha.
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.45) { model.showProjectSettings = true }
        } label: {
            HStack(spacing: 8) {
                Text(AureaText.t("om_exp_background")).font(.aurea(size: 17)).foregroundStyle(AureaColors.exportText)
                Spacer(minLength: 8)
                Circle().fill(transparent ? AureaColors.exportPage : color).frame(width: 20, height: 20)
                    .overlay(Circle().stroke(AureaColors.exportLine, lineWidth: 1))
                Text(ltrIsolado(value)).font(.aurea(size: 13)).foregroundStyle(AureaColors.exportHint)
                CupertinoGlyph.text(CupertinoGlyph.ChevronRight, size: 14, color: AureaColors.exportAction).mirrorsInRtl()
            }.frame(height: 44).contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .padding(.top, 14)
        .accessibilityLabel(AureaText.t("om_exp_background_open"))
        .accessibilityValue(value)
        .accessibilityIdentifier("export.settings.background")
    }

    /// "Avançado" (só vídeo, recolhido): codificador, taxa de bits, trecho e o resumo.
    @State private var advancedOpen = false
    private var advanced: some View {
        VStack(alignment: .leading, spacing: 0) {
            Button { withAnimation(.easeInOut(duration: 0.2)) { advancedOpen.toggle() } } label: {
                HStack {
                    Text(AureaText.t("exp2_advanced")).font(.aurea(size: 17, weight: .semibold)).foregroundStyle(AureaColors.exportText)
                    Spacer(minLength: 0)
                    CupertinoGlyph.text(advancedOpen ? CupertinoGlyph.ChevronUp : CupertinoGlyph.ChevronDown, size: 16, color: AureaColors.exportAction)
                }.frame(height: 44).contentShape(Rectangle())
            }.buttonStyle(.plain).padding(.top, 16)
                .accessibilityAddTraits(advancedOpen ? .isSelected : [])
                .accessibilityIdentifier("export.settings.advanced")
            if advancedOpen {
                label("export_encoder")
                segmented([("0", AureaText.t("export_encoder_auto")), ("1", AureaText.t("export_encoder_compatible")), ("2", AureaText.t("export_encoder_software"))],
                          selected: String(min(model.exportOptions.encoder, 2)), tag: "export.settings.encoder") { picked in
                    let chosen = UInt32(picked) ?? 0
                    model.exportOptions.encoder = chosen
                    if chosen != 0 { model.exportOptions.codec = .h264 }
                }
                hint(AureaText.t(model.exportOptions.encoder == 1 ? "export_encoder_compatible_note"
                                 : model.exportOptions.encoder == 2 ? "export_encoder_software_note" : "export_encoder_auto_note"))
                label("exp2_bitrate")
                AureaFlowLayout(hGap: 6, vGap: 6) {
                    ForEach([UInt32(0)] + Self.customBitrates, id: \.self) { mbps in
                        let on = model.exportOptions.bitrateMbps == mbps
                        Button { model.exportOptions.bitrateMbps = mbps } label: {
                            Text(mbps == 0 ? AureaText.t("exp2_bitrate_auto") : String(mbps)).font(.aurea(size: 14, weight: .semibold))
                                .foregroundStyle(on ? AureaColors.exportOnPrimary : AureaColors.exportText)
                                .padding(.horizontal, 14).padding(.vertical, 10)
                                .background(on ? AureaColors.exportPrimary : AureaColors.exportPage, in: RoundedRectangle(cornerRadius: 5))
                        }.buttonStyle(.plain).accessibilityAddTraits(on ? [.isButton, .isSelected] : .isButton)
                            .accessibilityIdentifier("export.settings.bitrate.\(mbps)")
                    }
                }
                rangeRow
                let size = sizeFor(model.exportOptions.shortSide)
                let fps = model.exportOptions.fps > 0 ? model.exportOptions.fps : model.compositionFps
                let seconds = Double(model.engine.exportDuration(model.exportOptions.trimToContent)) / max(1, model.compositionFps)
                VStack(spacing: 0) {
                    summary("editor_video", ["\(size.0) × \(size.1)", "\(format(fps)) fps", model.exportOptions.codec == .hevc ? "HEVC" : "H.264"].map(ltrIsolado).joined(separator: " · "))
                    summary("editor_duracao", ExportFormatText.time(seconds))
                    if seconds > 30 * 60 {
                        hint(seconds > AureaModel.exportMaxSeconds ? AureaText.t("exp_too_long_3h") : AureaText.t("exp_long_duration", ExportFormatText.time(seconds)), danger: true)
                    }
                    summary("editor_cor", AureaText.t("editor_sdr_bt_709"))
                }.padding(.top, 12)
            }
        }
    }

    private func summary(_ key: String, _ value: String) -> some View {
        HStack(alignment: .top, spacing: 8) {
            Text(AureaText.t(key)).foregroundStyle(AureaColors.exportHint).frame(maxWidth: .infinity, alignment: .leading)
            Text(value).monospacedDigit().foregroundStyle(AureaColors.exportText).multilineTextAlignment(.trailing)
                .fixedSize(horizontal: false, vertical: true).layoutPriority(1)
        }.font(.aurea(size: 14)).padding(.vertical, 5)
    }

    private func noticeView(_ text: String, danger: Bool) -> some View {
        Text(text).font(.aurea(size: 14)).foregroundStyle(danger ? AureaColors.lightDanger : AureaColors.exportText)
            .frame(maxWidth: .infinity, alignment: .leading).padding(14)
            .background(AureaColors.exportCard, in: RoundedRectangle(cornerRadius: 6))
            .padding(.horizontal, 10).padding(.top, 10)
            .accessibilityIdentifier("export.notice")
    }

    // MARK: Exportar (pré-checagens, P1-11)

    private var exportButton: some View {
        Button(action: go) {
            Text(AureaText.t("om_exp_go")).font(.aurea(size: 20, weight: .semibold))
                .foregroundStyle(model.exporting ? AureaColors.exportAction : AureaColors.exportOnPrimary)
                .frame(maxWidth: .infinity).padding(12)
                .background(AureaColors.exportPrimary, in: RoundedRectangle(cornerRadius: 5))
                .shadow(color: AureaColors.lightShadow, radius: 3, y: 1)
        }
        .buttonStyle(AureaPressStyle()).disabled(model.exporting)
        .padding(15)
        .background(AureaColors.exportCard.ignoresSafeArea(edges: .bottom))
        .accessibilityLabel(AureaText.t("om_exp_go"))
        .accessibilityIdentifier("export.go")
    }

    private func go() {
        if item == .package {
            guard let path = model.projectURL?.path else { return }
            model.exportProjectFile(path: path, title: model.projectName, includeMedia: includeMedia) { url in
                if let url { sharedProject = SharedProjectFile(url: url) }
            }
            return
        }
        if let kind = item.kind { model.exportOptions.kind = kind }
        let frames = model.engine.exportDuration(model.exportOptions.trimToContent)
        let seconds = Double(frames) / max(1, model.compositionFps)
        var check: (String, Bool)?
        if model.layers.isEmpty { check = (AureaText.t("om_exp_empty_project"), false) }
        else if frames < 1 { check = (AureaText.t("om_exp_too_short"), false) }
        else if item == .video && seconds > AureaModel.exportMaxSeconds { check = (AureaText.t("exp_too_long_3h"), false) }
        else if item == .gif || item == .sequence, let plan = model.imageExportPlan() {
            let planned = plan["frames"]?.intValue ?? 0
            if model.exportOptions.kind.tooLong(planned) { check = (AureaText.t("exp2_too_long"), false) }
            else if item == .gif && planned > Self.gifRecommendedFrames { check = (AureaText.t("om_exp_gif_heavy", Self.gifRecommendedFrames), true) }
        }
        guard let check else { start(); return }
        AureaPopupPresenter.shared.presentDialog { close in
            AureaDialog(title: nil, message: check.0, actions: check.1
                        ? [DialogAction(AureaText.t("common_close"), role: .negative),
                           DialogAction(AureaText.t("om_exp_anyway"), role: .positive, tag: "export.precheck.anyway") { start() }]
                        : [DialogAction(AureaText.t("common_ok"), role: .positive)],
                        onDismiss: close, tag: "export.precheck")
        }
    }

    private func start() {
        attempted = true; cancelled = false
        // A taxa vem do motor pela qualidade; só o Mbps manual vai como número.
        model.startExport()
    }

    private func showInfo(_ entry: ExportItem) {
        AureaPopupPresenter.shared.presentDialog { close in
            AureaDialog(title: AureaText.t(entry.nameKey), message: AureaText.t(entry == .package ? "om_exp_package_info" : entry.noteKey),
                        actions: [DialogAction(AureaText.t("common_ok"), role: .positive)], onDismiss: close, tag: "export.info")
        }
    }

    // MARK: Contas e textos

    private var sequenceFrames: Int {
        model.exportedKind == .sequence ? (model.imageExportPlan()?["frames"]?.intValue ?? 0) : 0
    }

    private var hevcAvailable: Bool {
        let bits = model.deviceReport["bits"]?.uint64Value ?? 0
        return bits & 1 == 0 || bits & 32 != 0
    }

    /// Mirrors export_attempt_frame_size at attempt 0 (ExportAttempts.hpp, 0.3.6):
    /// the native even size; an odd side exports aligned (long side ×16, short
    /// side even) instead of failing with a codec error.
    private func sizeFor(_ side: UInt32) -> (UInt32, UInt32) {
        let w = Double(model.compositionWidth), h = Double(model.compositionHeight)
        guard w > 0, h > 0 else { return (0, 0) }
        let shortest = min(w, h)
        let k = Double(side > 0 ? side : UInt32(shortest)) / shortest
        func align(_ v: Double, _ a: Double) -> UInt32 { UInt32(max(a, (v / a + 0.5).rounded(.down) * a)) }
        let odd = ((side == 0 || Double(side) == shortest) && (UInt32(w) % 2 != 0 || UInt32(h) % 2 != 0)) || side % 2 != 0
        if odd {
            if w == h { let s = align(w * k, 2); return (s, s) }
            return w > h ? (align(w * k, 16), align(h * k, 2)) : (align(w * k, 2), align(h * k, 16))
        }
        return (align(w * k, 2), align(h * k, 2))
    }

    private func fits(_ size: (UInt32, UInt32)) -> Bool {
        let cap = (model.composition["sizeCap"] as? [NSNumber] ?? []).map(\.uint32Value)
        guard cap.count >= 2, cap[0] > 0 else { return true }
        return max(size.0, size.1) <= cap[0] && min(size.0, size.1) <= cap[1]
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

    private func sizeLabel(_ bytes: Double) -> String {
        let megabytes = bytes / 1_000_000
        return megabytes >= 1000 ? AureaText.t("unit_gigabyte", format((megabytes / 100).rounded() / 10))
            : AureaText.t("unit_megabyte", max(1, Int(megabytes.rounded())))
    }

    private var failureNotice: String? {
        if model.exportCancelled || cancelled { return AureaText.t("ios_export_cancelled") }
        // A tela desistiu de um export sem progresso por minutos (o motor devolve "cancelado", mas não foi o usuário).
        if model.exportStalled { return AureaText.t("ios_export_failed_detail", AureaText.t("expfail_stuck")) }
        let result = (model.exportProgress["result"] as? NSNumber)?.intValue ?? 0
        if result == 28 { return AureaText.t("msg_sem_espaco_no_aparelho_libere_espaco") }
        if result != 0 {
            // O motivo do motor (ExportRules.hpp) no idioma do app; a frase crua só em português.
            if let reason = AureaModel.exportFailureReason(model.exportProgress) { return AureaText.t("ios_export_failed_detail", reason) }
            let raw = model.exportProgress["message"] as? String ?? ""
            return AureaText.t("ios_export_failed_detail", raw.isEmpty || AureaText.language.resolved != .pt ? AureaText.t("ios_error_code", "\(result)") : raw)
        }
        return model.exportMessage
    }

    private func format(_ value: Double) -> String { ExportFormatText.number(value) }
}

/// "Mais…": as taxas do AUREA (24 a 120 e personalizado) num menu ancorado.
private struct FpsMoreLink: View {
    let current: Double
    let onPick: (Double) -> Void
    let openKeypad: () -> Void
    @State private var open = false
    var body: some View {
        Button { open = true } label: {
            Text(AureaText.t("om_exp_fps_more")).font(.aurea(size: 13, weight: .semibold)).foregroundStyle(AureaColors.lightAccentInk)
                .padding(.vertical, 8).contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .padding(.top, 6)
        .accessibilityIdentifier("export.settings.fps.more")
        .aureaAnchoredMenu(isPresented: $open, tag: "export.settings.fps.more.menu") {
            ExportView.frameRates.map { rate in
                MenuEntry.item(AureaText.t("om_exp_fps_value", ExportFormatText.number(rate)), selected: abs(rate - current) < 0.01) { onPick(rate) }
            } + [MenuEntry.item(AureaText.t("project_fps_custom")) { openKeypad() }]
        }
    }
}

enum ExportFormatText {
    static func number(_ value: Double) -> String {
        if abs(value - value.rounded()) < 0.0005 { return String(Int(value.rounded())) }
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

// MARK: - Progresso (P0-10)

/// O painel sobre véu de 50 % (a página continua por baixo): mensagem 18,
/// barra de 220 × 7, % e tempo restante, a caixa de aviso do motor 2.0
/// (retomada, finalizando, validando, modo seguro, software, térmico),
/// "Cancelar" em destaque e o rodapé "não saia do app".
private struct ExportProgressPanel: View {
    @EnvironmentObject private var model: AureaModel
    @Binding var cancelled: Bool
    @State private var startedAt: Date?
    @State private var lastEta: Double = -1
    @State private var shownEta: Double = -1

    var body: some View {
        let done = (model.exportProgress["framesDone"] as? NSNumber)?.intValue ?? 0
        let total = (model.exportProgress["framesTotal"] as? NSNumber)?.intValue ?? 0
        let fraction = min(1, max(0, Double(done) / Double(max(1, total))))
        let eta = (model.exportProgress["etaSeconds"] as? NSNumber)?.doubleValue ?? 0
        let publishing = model.exportPublishing
        let preparing = model.exportPreparing
        ZStack {
            AureaColors.exportScrim.ignoresSafeArea().contentShape(Rectangle()).onTapGesture {}
            VStack(spacing: 0) {
                Text(preparing ? AureaText.t("msg_preparando") : AureaText.t(publishing ? "editor_salvando_galeria" : ExportProgressPanel.renderingKey(model.exportedKind)))
                    .font(.aurea(size: 18)).foregroundStyle(AureaColors.onImage).multilineTextAlignment(.center)
                ZStack(alignment: .leading) {
                    if !preparing {
                        AureaColors.addPanelDeep
                        AureaColors.accent.frame(width: 220 * (publishing ? 1 : fraction))
                    }
                }
                .frame(width: 220, height: 7).clipShape(RoundedRectangle(cornerRadius: 3.5)).padding(.top, 15)
                .accessibilityElement().accessibilityValue("\(Int((fraction * 100).rounded()))%")
                HStack {
                    Text(preparing ? "" : "\(publishing ? 100 : Int((fraction * 100).rounded()))%")
                        .font(.aurea(size: 13).monospacedDigit()).foregroundStyle(AureaColors.onImage)
                        .accessibilityIdentifier("export.progress.percent")
                    Spacer(minLength: 0)
                    if !publishing && !preparing && shownEta > 0 {
                        Text(shownEta < 60 ? AureaText.t("om_exp_eta_less") : AureaText.plural("om_exp_eta_minutes", Int((shownEta / 60).rounded(.up)), Int((shownEta / 60).rounded(.up))))
                            .font(.aurea(size: 13)).foregroundStyle(AureaColors.onImage)
                            .accessibilityIdentifier("export.progress.eta")
                    }
                }.frame(width: 220).padding(.top, 6)
                let notice = progressNotice
                if !preparing && !notice.isEmpty {
                    Text(notice).font(.aurea(size: 12)).foregroundStyle(AureaColors.warning)
                        .frame(width: 240, alignment: .leading).padding(10)
                        .background(AureaColors.addPanelDeep, in: RoundedRectangle(cornerRadius: 6)).padding(.top, 14)
                        .accessibilityIdentifier("export.progress.notice")
                }
            }
            .padding(.horizontal, 15)
            VStack(spacing: 0) {
                Spacer()
                if !publishing {
                    Button { cancelled = true; model.cancelExport() } label: {
                        Text(AureaText.t("editor_cancelar")).font(.aurea(size: 17, weight: .semibold)).foregroundStyle(AureaColors.accent)
                            .padding(.horizontal, 16).padding(.vertical, 10)
                    }
                    .buttonStyle(.plain)
                    .accessibilityHint(AureaText.t(preparing ? "msg_preparando" : "editor_cancelar"))
                    .accessibilityIdentifier("export.progress.cancel")
                    .padding(.bottom, 30)
                }
                HStack(spacing: 6) {
                    CupertinoGlyph.text(CupertinoGlyph.ExclamationmarkTriangle, size: 14, color: AureaColors.onImage70)
                    Text(AureaText.t("editor_mantenha_aurea_aberto_ate_terminar")).font(.aurea(size: 12)).foregroundStyle(AureaColors.onImage70)
                }
                .padding(.horizontal, 30).padding(.bottom, 30)
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("export.progress")
        .onAppear { if !model.exportPreparing && startedAt == nil { startedAt = Date() } }
        .onChange(of: preparing) { if !$0 && startedAt == nil { startedAt = Date() } }
        // Tempo restante da referência: só depois de 5 s e só quando a estimativa
        // muda menos de 40 s da anterior (estabilizou). Constantes de interface.
        .onChange(of: eta) { value in
            guard value > 0, let startedAt, Date().timeIntervalSince(startedAt) >= 5 else { return }
            if lastEta >= 0 && abs(value - lastEta) < 40 { shownEta = value }
            lastEta = value
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

    private var progressNotice: String {
        if model.exportPreparing { return "" }
        let flags = (model.exportProgress["flags"] as? NSNumber)?.uint32Value ?? 0
        var notices: [String] = []
        if model.exportLighterAfterClose { notices.append(AureaText.t("export_after_system_close")) }
        // 0.3.2 (CheckpointManager): continuou de um checkpoint.
        if flags & AureaExportFlag.resumed.rawValue != 0 && model.exportResumedFromPercent >= 0 {
            notices.append(AureaText.t("export_resumed_from", model.exportResumedFromPercent))
        }
        switch (flags >> 8) & 15 {
        case 4: notices.append(AureaText.t("export_v2_finalizing"))
        case 5: notices.append(AureaText.t("export_v2_validating"))
        default: break
        }
        if flags & AureaExportFlag.safeMode.rawValue != 0 { notices.append(AureaText.t("app_export_safe_mode")) }
        if model.exportOptions.aiUpscale > 0, let message = model.exportProgress["message"] as? String, message.hasPrefix("IA:") {
            notices.append(AureaEngineText.aiProgress(message))
        }
        if flags & AureaExportFlag.softwareEncoder.rawValue != 0 {
            notices.append(AureaText.t("ios_export_software_encoder", model.exportOptions.codec == .hevc ? "HEVC" : "H.264"))
        }
        if flags & AureaExportFlag.thermalReduced.rawValue != 0 { notices.append(AureaText.t("ios_export_thermal")) }
        if flags & AureaExportFlag.frameFallback.rawValue != 0 { notices.append(AureaText.t("ios_export_frame_fallback")) }
        notices.append(contentsOf: AureaModel.exportQualityNotes(flags))
        return notices.joined(separator: "\n")
    }
}

// MARK: - Resultado (P0-11)

/// A página azul-marinho do resultado: a prévia (70 %) — vídeo com ▶ de 62 e a
/// busca, GIF animado, PNG, a imagem da sequência — e a base (30 %) com Salvar |
/// Compartilhar (57), Fechar e o cartão de apoio. O AUREA já salvou em Fotos:
/// o Salvar diz "Salvo em Fotos ✓" e abre "Salvar em…" (Arquivos); se Fotos
/// recusou, volta a ser "Salvar" e tenta de novo.
private struct ExportResultPage: View {
    @EnvironmentObject private var model: AureaModel
    let preview: UIImage?
    let sequenceFrames: Int
    let onClose: () -> Void
    @State private var sharing = false
    @State private var savingToFiles = false

    var body: some View {
        VStack(spacing: 0) {
            Text(AureaText.t(Self.doneKey(model.exportedKind))).font(.aurea(size: 17, weight: .bold)).foregroundStyle(AureaColors.onImage)
                .frame(maxWidth: .infinity).frame(height: 42)
            GeometryReader { geometry in
                VStack(spacing: 0) {
                    ZStack {
                        AureaColors.addPanelDeep
                        if let url = model.exportedURL {
                            switch model.exportedKind {
                            case .video: ResultVideoPlayer(url: url)
                            case .gif: ResultGifView(url: url).padding(10)
                            case .frame:
                                if let image = UIImage(contentsOfFile: url.path) { Image(uiImage: image).resizable().scaledToFit().padding(10) }
                            case .sequence:
                                ZStack(alignment: .bottom) {
                                    if let preview { Image(uiImage: preview).resizable().scaledToFit().padding(10) }
                                    if sequenceFrames > 0 {
                                        Text(AureaText.t("om_exp_sequence_first", sequenceFrames)).font(.aurea(size: 13)).foregroundStyle(AureaColors.onImage).padding(10)
                                    }
                                }
                            }
                        }
                    }
                    .frame(height: geometry.size.height * 0.7)
                    ScrollView {
                        VStack(spacing: 0) {
                            if let message = model.exportMessage, !message.isEmpty {
                                Text(message).font(.aurea(size: 12)).foregroundStyle(AureaColors.onImage70).lineLimit(4)
                                    .frame(maxWidth: .infinity, alignment: .leading).padding(.bottom, 10)
                            }
                            HStack(spacing: 10) {
                                let saved = model.exportSavedToPhotos || model.exportedKind == .sequence
                                resultButton(saved ? (model.exportedKind == .sequence ? AureaText.t("ios_export_save_files") : AureaText.t("om_exp_saved_photos"))
                                             : AureaText.t("om_exp_save"),
                                             glyph: saved ? CupertinoGlyph.CheckmarkCircleFill : CupertinoGlyph.ArrowDownToLine, tag: "export.result.save") {
                                    if saved { savingToFiles = true } else { model.retrySaveExportToPhotos() }
                                }
                                resultButton(AureaText.t("editor_compartilhar"), glyph: CupertinoGlyph.SquareArrowUp, tag: "export.result.share") { sharing = true }
                            }
                            Button(action: onClose) {
                                Text(AureaText.t("om_exp_close")).font(.aurea(size: 17, weight: .semibold)).foregroundStyle(AureaColors.accent)
                                    .frame(maxWidth: .infinity).frame(height: 57).contentShape(Rectangle())
                            }.buttonStyle(.plain).padding(.top, 15).accessibilityIdentifier("export.result.close")
                            DonationCard(exporting: false).padding(.top, 12)
                        }.padding(20)
                    }
                }
            }
        }
        .background(AureaColors.exportResult.ignoresSafeArea())
        .preferredColorScheme(.dark)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("export.result")
        .sheet(isPresented: $sharing) { if let url = model.exportedURL { ExportShareSheet(url: url) } }
        .sheet(isPresented: $savingToFiles) { if let url = model.exportedURL { ExportFilesPicker(url: url) } }
    }

    private func resultButton(_ title: String, glyph: Character, tag: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 8) {
                CupertinoGlyph.text(glyph, size: 20, color: AureaColors.onImage)
                Text(title).font(.aurea(size: 17)).foregroundStyle(AureaColors.onImage).lineLimit(1).minimumScaleFactor(0.7)
            }
            .padding(.horizontal, 10)
            .frame(maxWidth: .infinity).frame(height: 57)
            .background(AureaColors.exportResultButton, in: RoundedRectangle(cornerRadius: 5))
            .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle())
        .disabled(model.exportPublishing)
        .accessibilityIdentifier(tag)
    }

    static func doneKey(_ kind: ExportKind) -> String {
        switch kind {
        case .video: return "editor_video_pronto"
        case .frame: return "exp2_done_frame"
        case .sequence: return "exp2_done_sequence"
        case .gif: return "exp2_done_gif"
        }
    }
}

/// O vídeo pronto (sem controles do sistema) com a barra de 62: ▶ de 62 e a busca.
private struct ResultVideoPlayer: View {
    let url: URL
    @State private var player: AVPlayer?
    @State private var playing = false
    @State private var position: Double = 0
    @State private var observer: Any?

    var body: some View {
        VStack(spacing: 0) {
            ZStack { if let player { ResultPlayerLayer(player: player) } }.frame(maxWidth: .infinity, maxHeight: .infinity)
            HStack(spacing: 0) {
                Button {
                    guard let player else { return }
                    if playing { player.pause(); playing = false }
                    else {
                        if position >= 0.999 { player.seek(to: .zero) }
                        player.play(); playing = true
                    }
                } label: {
                    CupertinoGlyph.text(playing ? CupertinoGlyph.PauseFill : CupertinoGlyph.PlayFill, size: 24, color: AureaColors.onImage)
                        .frame(width: 62, height: 62).background(AureaColors.addPanelDeep).contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel(AureaText.t(playing ? "editor_pausar" : "editor_reproduzir"))
                .accessibilityIdentifier("export.result.play")
                Slider(value: Binding(get: { position }, set: { value in
                    position = value
                    guard let player, let item = player.currentItem else { return }
                    let duration = CMTimeGetSeconds(item.duration)
                    if duration.isFinite && duration > 0 { player.seek(to: CMTime(seconds: duration * value, preferredTimescale: 600)) }
                }), in: 0...1)
                .tint(AureaColors.accent)
                .padding(.horizontal, 14)
                .accessibilityLabel(AureaText.t("om_exp_seek"))
                .accessibilityIdentifier("export.result.seek")
            }
            .frame(height: 62)
        }
        .onAppear {
            let next = AVPlayer(url: url)
            player = next
            observer = next.addPeriodicTimeObserver(forInterval: CMTime(value: 1, timescale: 10), queue: .main) { time in
                guard let item = next.currentItem else { return }
                let duration = CMTimeGetSeconds(item.duration)
                if duration.isFinite && duration > 0 { position = min(1, time.seconds / duration) }
                if next.rate == 0 && playing && position >= 0.999 { playing = false }
            }
        }
        .onDisappear {
            player?.pause()
            if let observer { player?.removeTimeObserver(observer) }
            observer = nil; player = nil
        }
    }
}

private struct ResultPlayerLayer: UIViewRepresentable {
    let player: AVPlayer
    final class LayerView: UIView {
        override class var layerClass: AnyClass { AVPlayerLayer.self }
        var playerLayer: AVPlayerLayer { layer as! AVPlayerLayer }
    }
    func makeUIView(context: Context) -> LayerView {
        let view = LayerView(); view.playerLayer.videoGravity = .resizeAspect; view.playerLayer.player = player; return view
    }
    func updateUIView(_ view: LayerView, context: Context) { view.playerLayer.player = player }
}

/// GIF animado (os quadros e os tempos do arquivo).
private struct ResultGifView: UIViewRepresentable {
    let url: URL
    func makeUIView(context: Context) -> UIImageView {
        let view = UIImageView(); view.contentMode = .scaleAspectFit
        view.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        view.setContentCompressionResistancePriority(.defaultLow, for: .vertical)
        if let source = CGImageSourceCreateWithURL(url as CFURL, nil) {
            var images: [UIImage] = []
            var duration: Double = 0
            for i in 0..<CGImageSourceGetCount(source) {
                guard let cg = CGImageSourceCreateImageAtIndex(source, i, nil) else { continue }
                images.append(UIImage(cgImage: cg))
                let props = CGImageSourceCopyPropertiesAtIndex(source, i, nil) as? [CFString: Any]
                let gif = props?[kCGImagePropertyGIFDictionary] as? [CFString: Any]
                duration += (gif?[kCGImagePropertyGIFUnclampedDelayTime] as? Double) ?? (gif?[kCGImagePropertyGIFDelayTime] as? Double) ?? 0.1
            }
            view.image = images.count > 1 ? UIImage.animatedImage(with: images, duration: duration) : images.first
        }
        return view
    }
    func updateUIView(_ view: UIImageView, context: Context) {}
}

/// "Salvar em Arquivos": o seletor do sistema exporta uma CÓPIA (o original
/// fica no Documents do app para abrir/compartilhar).
private struct ExportFilesPicker: UIViewControllerRepresentable {
    let url: URL
    func makeUIViewController(context: Context) -> UIDocumentPickerViewController {
        UIDocumentPickerViewController(forExporting: [url], asCopy: true)
    }
    func updateUIViewController(_ controller: UIDocumentPickerViewController, context: Context) {}
}

struct ExportShareSheet: UIViewControllerRepresentable {
    let url: URL
    func makeUIViewController(context: Context) -> UIActivityViewController { UIActivityViewController(activityItems: [url], applicationActivities: nil) }
    func updateUIViewController(_ controller: UIActivityViewController, context: Context) {}
}

struct ExportMoviePlayer: View {
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
