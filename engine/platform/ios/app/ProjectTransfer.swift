// =============================================================================
//  Aurea / iOS / ProjectTransfer.swift
//
//  As telas das quatro ações vindas do app antigo, na casca do Aurea:
//    · "Informações da mídia" (MediaInfo.kt): nome, tamanho, resolução, fps,
//      duração, codecs, taxa de amostragem e HDR, lidos do próprio arquivo;
//    · "Relatar um problema" (ReportProblemSheet.kt + ProblemReport.kt): o
//      texto da pessoa + a ficha do aparelho para POST /api/report;
//    · a folha de compartilhar do "Exportar arquivo do projeto".
//  "Substituir mídia" e o arquivo do projeto em si moram no AureaModel.
// =============================================================================
import AVFoundation
import ImageIO
import SwiftUI
import UniformTypeIdentifiers

/// Uma camada como item de `.sheet(item:)`.
struct LayerRef: Identifiable { let id: Int64 }

/// Um arquivo pronto para a folha de compartilhar.
struct SharedProjectFile: Identifiable { let id = UUID(); let url: URL }

struct ProjectFileShareSheet: UIViewControllerRepresentable {
    let url: URL
    func makeUIViewController(context: Context) -> UIActivityViewController {
        UIActivityViewController(activityItems: [url], applicationActivities: nil)
    }
    func updateUIViewController(_ controller: UIActivityViewController, context: Context) {}
}

// =============================================================================
// Informações da mídia
// =============================================================================
enum MediaInfoProbe {
    /// Nome legível do codec pelo FourCC do formato.
    static func codecName(_ subtype: FourCharCode) -> String {
        switch subtype {
        case kCMVideoCodecType_H264: return "H.264 (AVC)"
        case kCMVideoCodecType_HEVC, kCMVideoCodecType_HEVCWithAlpha: return "HEVC (H.265)"
        case kCMVideoCodecType_AppleProRes422, kCMVideoCodecType_AppleProRes422HQ, kCMVideoCodecType_AppleProRes422LT,
             kCMVideoCodecType_AppleProRes422Proxy, kCMVideoCodecType_AppleProRes4444, kCMVideoCodecType_AppleProRes4444XQ:
            return "Apple ProRes"
        case kCMVideoCodecType_MPEG4Video: return "MPEG-4"
        case kCMVideoCodecType_JPEG: return "Motion JPEG"
        case kAudioFormatMPEG4AAC, kAudioFormatMPEG4AAC_HE, kAudioFormatMPEG4AAC_HE_V2: return "AAC"
        case kAudioFormatMPEGLayer3: return "MP3"
        case kAudioFormatAppleLossless: return "ALAC"
        case kAudioFormatFLAC: return "FLAC"
        case kAudioFormatOpus: return "Opus"
        case kAudioFormatLinearPCM: return "PCM"
        case kAudioFormatAC3: return "AC-3"
        case kAudioFormatEnhancedAC3: return "E-AC-3"
        default:
            let bytes = [24, 16, 8, 0].map { UInt8((subtype >> $0) & 0xFF) }
            return String(bytes: bytes, encoding: .ascii)?.trimmingCharacters(in: .whitespaces).uppercased() ?? "?"
        }
    }

    static func duration(_ seconds: Double) -> String {
        seconds < 60 ? String(format: "%.1f s", seconds)
            : String(format: "%d:%04.1f", Int(seconds / 60), seconds.truncatingRemainder(dividingBy: 60))
    }

    /// Linhas (chave do rótulo, valor). nil = arquivo ilegível.
    static func read(_ path: String) async -> [(String, String)]? {
        guard !path.isEmpty else { return nil }
        let url = path.hasPrefix("file://") ? (URL(string: path) ?? URL(fileURLWithPath: path)) : URL(fileURLWithPath: path)
        guard FileManager.default.fileExists(atPath: url.path) else { return nil }
        var rows: [(String, String)] = [("media_info_file", url.lastPathComponent)]
        if let size = (try? url.resourceValues(forKeys: [.fileSizeKey]))?.fileSize {
            rows.append(("media_info_size", ByteCountFormatter.string(fromByteCount: Int64(size), countStyle: .file)))
        }
        if let type = UTType(filenameExtension: url.pathExtension), type.conforms(to: .image) {
            guard let source = CGImageSourceCreateWithURL(url as CFURL, nil),
                  let props = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any] else { return nil }
            let w = (props[kCGImagePropertyPixelWidth] as? NSNumber)?.intValue ?? 0
            let h = (props[kCGImagePropertyPixelHeight] as? NSNumber)?.intValue ?? 0
            if w > 0 { rows.append(("media_info_resolution", "\(w) × \(h)")) }
            if let uti = CGImageSourceGetType(source) as String?, let t = UTType(uti) {
                rows.append(("media_info_video_codec", (t.preferredFilenameExtension ?? uti).uppercased()))
            }
            return rows
        }
        let asset = AVURLAsset(url: url)
        var readable = false
        if let track = try? await asset.loadTracks(withMediaType: .video).first {
            readable = true
            if let size = try? await track.load(.naturalSize), let transform = try? await track.load(.preferredTransform) {
                let oriented = size.applying(transform)
                rows.append(("media_info_resolution", "\(Int(abs(oriented.width))) × \(Int(abs(oriented.height)))"))
            }
            if let fps = try? await track.load(.nominalFrameRate), fps > 0 {
                rows.append(("media_info_fps", String(format: "%.3f", fps).replacingOccurrences(of: "\\.?0+$", with: "", options: .regularExpression)))
            }
            if let seconds = try? await asset.load(.duration).seconds, seconds.isFinite, seconds > 0 {
                rows.append(("media_info_duration", duration(seconds)))
            }
            if let format = (try? await track.load(.formatDescriptions))?.first {
                rows.append(("media_info_video_codec", codecName(CMFormatDescriptionGetMediaSubType(format))))
                let transfer = CMFormatDescriptionGetExtension(format, extensionKey: kCMFormatDescriptionExtension_TransferFunction) as? String
                let hdr = transfer == (kCMFormatDescriptionTransferFunction_ITU_R_2100_HLG as String)
                    || transfer == (kCMFormatDescriptionTransferFunction_SMPTE_ST_2084_PQ as String)
                rows.append(("media_info_hdr", AureaText.t(hdr ? "common_yes" : "common_no")))
            }
        }
        if let track = try? await asset.loadTracks(withMediaType: .audio).first {
            if !readable, let seconds = try? await asset.load(.duration).seconds, seconds.isFinite, seconds > 0 {
                rows.append(("media_info_duration", duration(seconds)))
            }
            readable = true
            if let format = (try? await track.load(.formatDescriptions))?.first {
                rows.append(("media_info_audio_codec", codecName(CMFormatDescriptionGetMediaSubType(format))))
                if let asbd = CMAudioFormatDescriptionGetStreamBasicDescription(format)?.pointee, asbd.mSampleRate > 0 {
                    let channels = asbd.mChannelsPerFrame > 0 ? " · \(asbd.mChannelsPerFrame) ch" : ""
                    rows.append(("media_info_sample_rate", "\(Int(asbd.mSampleRate)) Hz" + channels))
                }
            }
        }
        return readable ? rows : nil
    }
}

struct MediaInfoSheetView: View {
    let path: String
    @Environment(\.dismiss) private var dismiss
    @State private var rows: [(String, String)]?
    @State private var loaded = false

    var body: some View {
        NavigationStack {
            List {
                if loaded, let rows {
                    ForEach(Array(rows.enumerated()), id: \.offset) { _, row in
                        HStack(alignment: .firstTextBaseline, spacing: 16) {
                            Text(AureaText.t(row.0)).foregroundStyle(AureaColors.text)
                            Spacer(minLength: 8)
                            Text(row.1).foregroundStyle(AureaColors.muted).multilineTextAlignment(.trailing)
                        }.font(.aurea(size: 15)).listRowBackground(AureaColors.editorPanel)
                    }
                } else if loaded {
                    Text(AureaText.t("media_info_unreadable")).font(.aurea(size: 15)).foregroundStyle(AureaColors.muted)
                        .listRowBackground(AureaColors.editorPanel)
                }
            }
            .scrollContentBackground(.hidden).background(AureaColors.background)
            .navigationTitle(AureaText.t("media_info_title")).navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button(AureaText.t("editor_fechar")) { dismiss() } } }
        }
        .presentationDetents([.medium, .large])
        .task {
            rows = await MediaInfoProbe.read(path)
            loaded = true
        }
    }
}

// =============================================================================
// Relatar um problema
// =============================================================================
enum ProblemReport {
    static let whatDidMax = 2000
    static let whatHappenedMax = 4000
    static let stepsMax = 4000
    private static let installKey = "aurea.crash.instalacao"   // o mesmo id dos relatórios de crash

    static var appVersion: String { Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "" }
    static var appBuild: String { Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "" }

    static func deviceModel() -> String {
        var info = utsname()
        _ = uname(&info)
        return withUnsafeBytes(of: &info.machine) { raw in
            String(decoding: raw.prefix { $0 != 0 }, as: UTF8.self)
        }
    }

    static func installationId() -> String {
        if let id = UserDefaults.standard.string(forKey: installKey) { return id }
        let novo = UUID().uuidString.lowercased()
        UserDefaults.standard.set(novo, forKey: installKey)
        return novo
    }

    static func deviceSummary() -> String {
        "Aurea \(appVersion) (\(appBuild)) · iOS \(UIDevice.current.systemVersion) · Apple \(deviceModel())"
    }

    enum Result { case sent, invalid, rateLimited, offline, failed }

    static func send(sessao: ContaSessao?, whatDid: String, whatHappened: String, steps: String) async -> Result {
        var body: [String: Any] = [
            "reportId": "report-\(Int64(Date().timeIntervalSince1970 * 1000))-\(UUID().uuidString.prefix(8).lowercased())",
            "installId": installationId(), "platform": "ios",
            "appVersion": appVersion, "appBuild": appBuild, "os": "iOS", "osVersion": UIDevice.current.systemVersion,
            "deviceModel": deviceModel(), "manufacturer": "Apple", "abi": "arm64",
            "locale": Locale.current.identifier,
            "whatDid": String(whatDid.trimmingCharacters(in: .whitespacesAndNewlines).prefix(whatDidMax)),
            "whatHappened": String(whatHappened.trimmingCharacters(in: .whitespacesAndNewlines).prefix(whatHappenedMax)),
            "steps": String(steps.trimmingCharacters(in: .whitespacesAndNewlines).prefix(stepsMax)),
        ]
        if let email = sessao?.email { body["email"] = email }
        let r = await ContaAPI.chamar("/api/report", metodo: "POST", corpo: body, token: sessao?.token)
        if r.ok { return .sent }
        switch r.status {
        case 0: return .offline
        case 429: return .rateLimited
        case 400, 413: return .invalid
        default: return .failed
        }
    }
}

struct ReportProblemView: View {
    let sessao: ContaSessao?
    let onResult: (String) -> Void
    @Environment(\.dismiss) private var dismiss
    @State private var whatDid = ""
    @State private var whatHappened = ""
    @State private var steps = ""
    @State private var sending = false

    private var canSend: Bool { whatHappened.trimmingCharacters(in: .whitespacesAndNewlines).count >= 3 && !sending }

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    Text(AureaText.t("report_subtitle")).font(.aurea(size: 12.5)).foregroundStyle(AureaColors.muted)
                    field("report_what_did", $whatDid, ProblemReport.whatDidMax)
                    field("report_what_happened", $whatHappened, ProblemReport.whatHappenedMax)
                    field("report_steps", $steps, ProblemReport.stepsMax)
                    Text(AureaText.t("report_device").uppercased()).font(.aurea(size: 11, weight: .semibold)).foregroundStyle(AureaColors.muted)
                    Text(ProblemReport.deviceSummary()).font(.aurea(size: 12.5)).foregroundStyle(AureaColors.muted)
                    Button(action: send) {
                        Text(AureaText.t(sending ? "report_sending" : "report_send"))
                            .font(.aurea(size: 17, weight: .semibold)).foregroundStyle(.white)
                            .frame(maxWidth: .infinity, minHeight: 52)
                            .background(AureaColors.accent.opacity(canSend ? 1 : 0.45), in: RoundedRectangle(cornerRadius: 14))
                    }.buttonStyle(.plain).disabled(!canSend)
                }.padding(20)
            }
            .background(AureaColors.background)
            .navigationTitle(AureaText.t("report_title")).navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .cancellationAction) { Button(AureaText.t("common_cancel")) { dismiss() }.disabled(sending) } }
        }
        .interactiveDismissDisabled(sending)
    }

    private func field(_ key: String, _ text: Binding<String>, _ max: Int) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(AureaText.t(key).uppercased()).font(.aurea(size: 11, weight: .semibold)).foregroundStyle(AureaColors.muted)
            TextEditor(text: Binding(get: { text.wrappedValue }, set: { text.wrappedValue = String($0.prefix(max)) }))
                .font(.aurea(size: 15)).foregroundStyle(AureaColors.text).scrollContentBackground(.hidden)
                .frame(minHeight: 72).padding(8)
                .background(AureaColors.editorPanel, in: RoundedRectangle(cornerRadius: 10))
        }
    }

    private func send() {
        guard canSend else { return }
        sending = true
        Task { @MainActor in
            let r = await ProblemReport.send(sessao: sessao, whatDid: whatDid, whatHappened: whatHappened, steps: steps)
            sending = false
            switch r {
            case .sent: onResult(AureaText.t("report_sent")); dismiss()
            case .offline: onResult(AureaText.t("report_offline"))
            case .rateLimited: onResult(AureaText.t("report_rate_limited"))
            case .invalid, .failed: onResult(AureaText.t("report_failed"))
            }
        }
    }
}
