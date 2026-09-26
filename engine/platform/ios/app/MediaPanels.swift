import SwiftUI
import AVFoundation
import UniformTypeIdentifiers
import Security
import CryptoKit

struct NativeCaptionBlock: Codable, Equatable, Identifiable {
    var id: Int64; var start: Int32; var end: Int32; var text: String
}
struct NativeCaptionTrack: Codable, Equatable {
    var layer: Int64; var source: Int64; var segments: [NativeCaptionBlock]
}

@MainActor
struct CaptionBlockEditor: View {
    @EnvironmentObject private var model: AureaModel
    let track: NativeCaptionTrack
    @State private var selected: Set<Int64> = []
    @State private var text = ""
    @State private var start = ""
    @State private var end = ""
    @State private var error: String?
    private func edit(_ op: String, _ fields: [String: Any] = [:]) {
        var value = fields; value["op"] = op; value["ids"] = Array(selected)
        guard let data = try? JSONSerialization.data(withJSONObject: value), let command = String(data: data, encoding: .utf8) else { return }
        if model.engine.editCaptionTrack(track.layer, command: command) { error = nil; model.refreshModel(force: true) }
        else { error = "A edição sobrepõe outro bloco ou possui tempos inválidos." }
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Faixa de legendas · selecione um ou vários blocos")
            ScrollView(.horizontal) {
                HStack {
                    ForEach(track.segments) { block in
                        Button { if selected.contains(block.id) { selected.remove(block.id) } else { selected.insert(block.id) }; text = block.text; start = String(block.start); end = String(block.end) } label: {
                            Text("\(block.start)–\(block.end)\n\(block.text)").lineLimit(2).padding(8).background(selected.contains(block.id) ? AureaColors.accent.opacity(0.5) : AureaColors.muted.opacity(0.15)).clipShape(RoundedRectangle(cornerRadius: 8))
                        }.buttonStyle(.plain)
                    }
                }
            }
            if !selected.isEmpty {
                HStack {
                    Button("← 1") { edit("move", ["delta": -1]) }
                    Button("1 →") { edit("move", ["delta": 1]) }
                    Button("Dividir") { edit("split", ["frame": model.status.playhead]) }.disabled(selected.count != 1)
                    Button("Unir") { edit("merge"); selected.removeAll() }.disabled(selected.count < 2)
                    Button("Excluir") { edit("delete"); selected.removeAll() }
                }
                if selected.count == 1 {
                    TextField("Texto", text: $text).textFieldStyle(.roundedBorder)
                    Button("Aplicar texto mantendo os tempos") { edit("text", ["text": text]) }
                    HStack { TextField("Início · quadro", text: $start); TextField("Fim · quadro", text: $end) }.textFieldStyle(.roundedBorder).keyboardType(.numberPad)
                    Button("Ajustar duração") { if let a = Int(start), let b = Int(end) { edit("trim", ["start": a, "end": b]) } }
                }
                Button("Limpar seleção") { selected.removeAll() }
            }
            if let error { Text(error).foregroundStyle(.red) }
        }.padding(.vertical, 10)
    }
}

@MainActor
struct TrackingPanel: View {
    @EnvironmentObject private var model: AureaModel
    @State private var mode: UInt32 = 1
    @State private var status: [String: Any] = [:]
    private let timer = Timer.publish(every: 0.4, on: .main, in: .common).autoconnect()
    private var state: Int { (status["state"] as? NSNumber)?.intValue ?? 0 }
    var body: some View {
        VStack(spacing: 0) {
            PanelHeader(title: AureaText.t("panel_rastreio"), onBack: { model.panel = .none })
            ScrollView {
                VStack(alignment: .leading, spacing: 0) {
                    action("panel_rastrear_ponto", "panel_cria_ponto_guia_segue_objeto_ligue") { model.panel = .none; model.beginPointPick(stabilize: false) }
                    Spacer().frame(height: 10)
                    action("panel_estabilizar_pelo_ponto", "panel_move_video_ponto_ficar_parado_tela") { model.panel = .none; model.beginPointPick(stabilize: true) }
                    Spacer().frame(height: 12)
                    note(AureaText.t("panel_toque_num_detalhe_contraste_canto_luz"))
                    Spacer().frame(height: 18)
                    cameraSection
                }.padding(.init(top: 12, leading: 18, bottom: 24, trailing: 18))
            }
        }.foregroundStyle(AureaColors.text).background(AureaColors.editorPanel)
            .onAppear(perform: reload).onReceive(timer) { _ in reload() }
    }
    @ViewBuilder private var cameraSection: some View {
        Text(AureaText.t("panel_camera_3d")).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted)
        Spacer().frame(height: 6)
        HStack(spacing: 6) {
            ForEach(Array(["panel_rapido", "panel_equilibrado", "panel_alta_qualidade"].enumerated()), id: \.offset) { index, key in
                Button { mode = UInt32(index) } label: {
                    Text(AureaText.t(key)).font(.aurea(size: 12))
                        .foregroundStyle(mode == UInt32(index) ? AureaColors.accent : AureaColors.text)
                        .padding(.horizontal, 10).padding(.vertical, 6)
                        .background(mode == UInt32(index) ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                }.buttonStyle(AureaPressStyle())
            }
        }
        Spacer().frame(height: 8)
        if state == 1 {
            let progress = number("progress").clamped(to: 0...1)
            Text("Analisando o movimento… \(Int(progress * 100))%").font(.aurea(size: 13))
            Spacer().frame(height: 6)
            GeometryReader { geometry in
                ZStack(alignment: .leading) {
                    AureaColors.chip
                    AureaColors.accent.frame(width: geometry.size.width * CGFloat(progress))
                }.clipShape(RoundedRectangle(cornerRadius: 3))
            }.frame(height: 6)
            Spacer().frame(height: 8)
            action("panel_cancelar", "panel_analise_projeto_nao_muda") { model.engine.cancelCameraTracking(); reload() }
        } else if state == 2 {
            let kind = AureaText.t(flag("rotationOnly") ? "panel_camera_so_gira_lugar_sem_profundidade" : "panel_movimento_camera_encontrado")
            Text(kind + (flag("cached") ? AureaText.t("panel_analise_guardada") : ""))
                .font(.aurea(size: 13, weight: .semibold))
            Spacer().frame(height: 4)
            note("\(Int(number("solved"))) de \(Int(number("frames"))) quadros · precisão \(Int(number("confidence") * 100))% · abertura da lente \(Int(number("fovDeg").rounded()))°")
            Spacer().frame(height: 8)
            action("panel_criar_camera", "panel_cria_camera_3d_animada_ponto_guia") {
                let error = model.engine.applyCameraTracking()
                model.toast = error.isEmpty ? AureaText.t("msg_camera_rastreada_criada_ligue_modelos_3d") : error
                model.refreshModel(force: true); reload()
            }
            Spacer().frame(height: 8)
            action("panel_analisar_novo", "panel_modo_escolhido_acima", run: analyze)
        } else {
            if state == 3 || state == 4 {
                note(state == 4 ? AureaText.t("panel_analise_cancelada") : "Não deu para resolver: \(status["message"] as? String ?? "")")
                Spacer().frame(height: 8)
            }
            action("panel_analisar_camera", "panel_acha_movimento_camera_video_roda_segundo", run: analyze)
        }
    }
    private func number(_ key: String) -> Double { (status[key] as? NSNumber)?.doubleValue ?? 0 }
    private func flag(_ key: String) -> Bool { (status[key] as? NSNumber)?.boolValue ?? false }
    private func note(_ text: String) -> some View {
        Text(text).font(.aurea(size: 12)).lineSpacing(2).foregroundStyle(AureaColors.muted)
    }
    private func reload() { status = model.engine.cameraTrackingStatus() }
    private func analyze() {
        guard let id = model.primarySelection else { return }
        model.engine.setCameraTrack(mode, forLayer: id); reload()
    }
    private func action(_ title: String, _ subtitle: String, run: @escaping () -> Void) -> some View {
        Button(action: run) {
            VStack(alignment: .leading, spacing: 2) {
                Text(AureaText.t(title)).font(.aurea(size: 14, weight: .semibold))
                Text(AureaText.t(subtitle)).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
            }.frame(maxWidth: .infinity, alignment: .leading).padding(.horizontal, 14).padding(.vertical, 12)
                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 12))
        }.buttonStyle(AureaPressStyle())
    }
}

struct CaptionWord: Codable, Identifiable {
    var id = UUID()
    var word: String
    var start: Double
    var end: Double
    var native: [String: Any] { ["word": word, "start": start, "end": end] }
    init(word: String, start: Double, end: Double) { self.word = word; self.start = start; self.end = end }
    init?(_ row: [String: Any]) {
        guard let word = row["word"] as? String,
              let start = (row["start"] as? NSNumber)?.doubleValue,
              let end = (row["end"] as? NSNumber)?.doubleValue,
              start.isFinite, end.isFinite, end > start, !word.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { return nil }
        self.init(word: word, start: start, end: end)
    }
}

enum CaptionKeychain {
    private static let query: [String: Any] = [kSecClass as String: kSecClassGenericPassword,
        kSecAttrService as String: "com.aurea.captions", kSecAttrAccount as String: "groq_api_key"]
    static func read() -> String {
        var q = query; q[kSecReturnData as String] = true; q[kSecMatchLimit as String] = kSecMatchLimitOne
        var result: CFTypeRef?
        guard SecItemCopyMatching(q as CFDictionary, &result) == errSecSuccess, let data = result as? Data else { return "" }
        return String(data: data, encoding: .utf8) ?? ""
    }
    static func save(_ key: String) -> Bool {
        let value = key.trimmingCharacters(in: .whitespacesAndNewlines)
        if value.isEmpty { let status = SecItemDelete(query as CFDictionary); return status == errSecSuccess || status == errSecItemNotFound }
        let attrs: [String: Any] = [kSecValueData as String: Data(value.utf8), kSecAttrAccessible as String: kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly]
        let status = SecItemUpdate(query as CFDictionary, attrs as CFDictionary)
        if status == errSecItemNotFound { return SecItemAdd(query.merging(attrs) { _, new in new } as CFDictionary, nil) == errSecSuccess }
        return status == errSecSuccess
    }
}

enum CaptionTranscriber {
    struct Failure: LocalizedError { var message: String; var errorDescription: String? { message } }
    static func cacheURL(_ path: String) -> URL {
        let size = ((try? FileManager.default.attributesOfItem(atPath: path)[.size]) as? NSNumber)?.int64Value ?? 0
        let hash = SHA256.hash(data: Data("\(path)|\(size)".utf8)).map { String(format: "%02x", $0) }.joined()
        return AureaPaths.documents.appendingPathComponent("Captions", isDirectory: true).appendingPathComponent(hash + ".json")
    }
    static func load(_ path: String) -> [CaptionWord] {
        guard let data = try? Data(contentsOf: cacheURL(path)) else { return [] }
        return (try? JSONDecoder().decode([CaptionWord].self, from: data)) ?? []
    }
    static func save(_ words: [CaptionWord], path: String) {
        guard !path.isEmpty else { return }
        let url = cacheURL(path)
        try? FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        if let data = try? JSONEncoder().encode(words) { try? data.write(to: url, options: .atomic) }
    }
    static func prepareModel() async throws -> URL {
        let base = ProcessInfo.processInfo.physicalMemory >= 6 * 1024 * 1024 * 1024 && !ProcessInfo.processInfo.isLowPowerModeEnabled
        let name = base ? "base" : "tiny"
        let expected = base ? 59707625 : 32152673
        let hash = base ? "422f1ae452ade6f30a004d7e5c6a43195e4433bc370bf23fac9cc591f01a8898" : "818710568da3ca15689e31a743197b520007872ff9576237bda97bd1b469c3d7"
        let folder = AureaPaths.documents.appendingPathComponent("Whisper", isDirectory: true)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        let target = folder.appendingPathComponent("ggml-\(name)-q5_1.bin")
        func valid(_ url: URL) throws -> Bool {
            guard (try FileManager.default.attributesOfItem(atPath: url.path)[.size] as? NSNumber)?.intValue == expected else { return false }
            let file = try FileHandle(forReadingFrom: url); defer { try? file.close() }
            var digest = SHA256()
            while let data = try file.read(upToCount: 65536), !data.isEmpty { digest.update(data: data) }
            return digest.finalize().map { String(format: "%02x", $0) }.joined() == hash
        }
        if (try? valid(target)) == true { return target }
        let remote = URL(string: "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/\(target.lastPathComponent)")!
        let (temporary, response) = try await URLSession.shared.download(from: remote)
        defer { try? FileManager.default.removeItem(at: temporary) }
        guard (response as? HTTPURLResponse)?.statusCode == 200, try valid(temporary) else { throw Failure(message: "Falha na verificação do modelo Whisper") }
        try Task.checkCancellation()
        if FileManager.default.fileExists(atPath: target.path) { try FileManager.default.removeItem(at: target) }
        try FileManager.default.moveItem(at: temporary, to: target)
        var values = URLResourceValues(); values.isExcludedFromBackup = true
        var local = target; try local.setResourceValues(values)
        return target
    }
    // Mesma combinação de palavras e pontuação de Captions.kt no Android.
    static func parse(_ object: [String: Any]) -> [CaptionWord] {
        var words = (object["words"] as? [[String: Any]] ?? []).compactMap(CaptionWord.init)
        let segments = object["segments"] as? [[String: Any]] ?? []
        if !words.isEmpty {
            func letters(_ text: String) -> String { String(text.lowercased().filter { $0.isLetter || $0.isNumber }) }
            var wi = 0
            for segment in segments {
                let end = (segment["end"] as? NSNumber)?.doubleValue ?? 0
                let tokens = (segment["text"] as? String ?? "").split(whereSeparator: \.isWhitespace).map(String.init).filter { !letters($0).isEmpty }
                var ti = 0
                while wi < words.count && words[wi].start < end + 0.05 && ti < tokens.count {
                    if letters(words[wi].word) == letters(tokens[ti]) { words[wi].word = tokens[ti]; ti += 1 }
                    wi += 1
                }
            }
            return words
        }
        for segment in segments {
            let tokens = (segment["text"] as? String ?? "").split(whereSeparator: \.isWhitespace).map(String.init)
            var time = (segment["start"] as? NSNumber)?.doubleValue ?? 0
            let end = (segment["end"] as? NSNumber)?.doubleValue ?? time
            let duration = end - time, total = Double(tokens.reduce(0) { $0 + $1.count + 1 })
            guard duration > 0, total > 0 else { continue }
            for token in tokens { let d = duration * Double(token.count + 1) / total; words.append(CaptionWord(word: token, start: time, end: time + d)); time += d }
        }
        return words
    }
}

@MainActor
struct CaptionsPanel: View {
    @EnvironmentObject private var model: AureaModel
    @State private var words: [CaptionWord] = []
    @State private var sourcePath = ""
    @State private var transcriptSource: String?
    @State private var hasKey = false
    @State private var language = ""
    @State private var importing = false
    @State private var busy: String?
    @State private var error: String?
    @State private var job: Task<Void, Never>?
    @State private var loading: Task<Void, Never>?
    @State private var fillers: Set<UUID> = []
    @State private var wordRevision = UUID()
    @State private var editing: UUID?
    @State private var captionCount = 0
    @State private var mode = 0
    @State private var style = 2
    @State private var maxWords = 4
    @State private var maxChars = 18
    @State private var maxLines = 2
    @State private var highlight = true
    @State private var uppercase = false
    @State private var breakOnPause = true
    @State private var removeFillers = true
    @State private var posY: Float = 0.78
    @State private var size: Float = 0.065
    private var captionTrack: NativeCaptionTrack? { model.captionTracks.first { $0.layer == model.primarySelection || $0.source == model.primarySelection } }
    private var id: Int64 { captionTrack?.source ?? model.primarySelection ?? 0 }
    private var path: String { model.engine.layerMediaPath(id) }
    private var languages: [String] { [AureaText.t("pn_caption_lang_auto"), "Português", "English", "Español"] }
    private let languageCodes = ["", "pt", "en", "es"]
    var body: some View {
        VStack(spacing: 0) {
            PanelHeader(title: AureaText.t("panel_legendas"), onBack: { model.panel = .none })
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 0) {
                    controls
                    ForEach(0..<((words.count + 23) / 24), id: \.self) { chunk in
                        transcriptChunk(chunk)
                    }
                    Spacer().frame(height: 24)
                }.padding(.horizontal, 18).padding(.vertical, 10)
            }
        }.foregroundStyle(AureaColors.text).background(AureaColors.editorPanel)
        .onAppear { open(); loadOptions() }
        .onChange(of: id) { _ in open() }
        .onChange(of: model.status.modelRevision) { _ in captionCount = Int(model.engine.captionCount(id)) }
        .onDisappear { _ = model.engine.captionProgress(true); job?.cancel(); loading?.cancel(); persist(); model.captionOptions = options }
        .fileImporter(isPresented: $importing, allowedContentTypes: [UTType(filenameExtension: "srt") ?? .plainText, .plainText, .data]) { result in
            importSRT(result)
        }
    }
    private var controls: some View {
        VStack(alignment: .leading, spacing: 0) {
            if let captionTrack { CaptionBlockEditor(track: captionTrack) }
            if let busy { note(busy, color: AureaColors.accent) }
            if let error { note(error, color: AureaColors.danger) }
            note("Whisper no aparelho. O primeiro uso baixa o modelo; seu áudio permanece local.", color: AureaColors.muted)
            if busy != nil { CaptionAction(label: "Cancelar") { _ = model.engine.captionProgress(true); job?.cancel() } }
            label("panel_idioma_fala")
            CaptionChips(options: languages, selected: languageCodes.firstIndex(of: language) ?? 0) { language = languageCodes[$0] }
            HStack(spacing: 8) {
                CaptionAction(label: AureaText.t(words.isEmpty ? "panel_gerar_legendas" : "panel_transcrever_novo"), primary: true, enabled: busy == nil, action: transcribe)
                CaptionAction(label: AureaText.t("panel_importar_legenda_srt"), enabled: busy == nil) { importing = true }
            }.padding(.vertical, 6)
            label("panel_estilo")
            CaptionChips(options: ["pn_caption_style_classic", "panel_caixa", "pn_caption_style_highlight", "pn_caption_style_neon", "pn_karaoke", "pn_pop"].map { AureaText.t($0) }, selected: style) { style = $0 }
            label("pn_caption")
            CaptionChips(options: [AureaText.t("pn_caption_grouped"), AureaText.t("pn_caption_one_per_word")], selected: mode) { mode = $0 }
            if mode == 0 { groupingControls }
            label("pn_caption_screen_height")
            CaptionChips(options: ["pn_caption_pos_top", "pn_caption_pos_middle", "pn_caption_pos_bottom"].map { AureaText.t($0) },
                selected: [Float(0.2), 0.5, 0.78].firstIndex(where: { abs($0 - posY) < 0.01 }) ?? -1) { posY = [0.2, 0.5, 0.78][$0] }
            label("panel_tamanho")
            CaptionChips(options: ["pn_size_small_short", "pn_size_medium_short", "pn_size_large_short"].map { AureaText.t($0) },
                selected: [Float(0.045), 0.065, 0.09].firstIndex(where: { abs($0 - size) < 0.001 }) ?? -1) { size = [0.045, 0.065, 0.09][$0] }
            captionToggle("pn_caption_highlight_word", value: $highlight)
            captionToggle("pn_caption_uppercase", value: $uppercase)
            captionToggle("pn_caption_break_pauses", value: $breakOnPause)
            captionToggle("pn_caption_remove_fillers", value: $removeFillers)
            HStack(spacing: 8) {
                CaptionAction(label: AureaText.t(captionCount > 0 ? "pn_caption_redo" : "pn_caption_create"), primary: true, enabled: !words.isEmpty && busy == nil, action: apply)
                if captionCount > 0 {
                    CaptionAction(label: AureaText.t("pn_caption_remove_n", captionCount), enabled: busy == nil) {
                        model.engine.removeCaptions(id); model.refreshModel(force: true)
                        captionCount = Int(model.engine.captionCount(id))
                    }
                }
            }.padding(.vertical, 8)
            if !words.isEmpty {
                Text(transcriptSource.map { AureaText.plural("pn_caption_transcript_source", words.count, $0, words.count) }
                    ?? AureaText.plural("pn_caption_transcript", words.count, words.count))
                    .font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted).padding(.top, 8)
            }
        }
    }
    private var groupingControls: some View {
        VStack(alignment: .leading, spacing: 0) {
            label("pn_caption_words_per_caption")
            CaptionChips(options: (1...6).map(String.init), selected: maxWords - 1) { maxWords = $0 + 1 }
            label("pn_caption_chars_per_line")
            CaptionChips(options: ["12", "18", "24", "32"], selected: [12, 18, 24, 32].firstIndex(of: maxChars) ?? -1) { maxChars = [12, 18, 24, 32][$0] }
            label("pn_caption_lines")
            CaptionChips(options: ["1", "2", "3"], selected: maxLines - 1) { maxLines = $0 + 1 }
        }
    }
    private func label(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted).padding(.top, 8)
    }
    private func note(_ text: String, color: Color) -> some View {
        Text(text).font(.aurea(size: 13)).foregroundStyle(color).padding(.vertical, 6)
    }
    private func captionToggle(_ key: String, value: Binding<Bool>) -> some View {
        HStack {
            Text(AureaText.t(key)).font(.aurea(size: 13)).frame(maxWidth: .infinity, alignment: .leading)
            AureaToggle(checked: value.wrappedValue, onCheckedChange: { value.wrappedValue = $0 })
        }.frame(height: 44)
    }
    private func transcriptChunk(_ chunk: Int) -> some View {
        let lower = chunk * 24, upper = min(words.count, lower + 24)
        let items = Array(words[lower..<upper])
        return VStack(alignment: .leading, spacing: 0) {
            AureaFlowLayout(hGap: 6, vGap: 6) {
                ForEach(items) { word in
                    let struck = removeFillers && fillers.contains(word.id)
                    Button { editing = word.id } label: {
                        Text(word.word).font(.aurea(size: 13)).strikethrough(struck)
                            .foregroundStyle(struck ? AureaColors.muted : AureaColors.text)
                            .padding(.horizontal, 8).padding(.vertical, 4)
                            .background(editing == word.id ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 6))
                    }.buttonStyle(AureaPressStyle())
                }
            }.padding(.bottom, 6)
            if let editing, let word = items.first(where: { $0.id == editing }) {
                CaptionWordEditor(word: word) { edit(word.id, text: $0) }.id(word.id).padding(.bottom, 8)
            }
        }
    }
    private func open() {
        loading?.cancel(); job?.cancel(); busy = nil; error = nil; editing = nil
        let source = path, sourceId = id
        sourcePath = source; words = []; fillers = []; transcriptSource = nil; wordRevision = UUID()
        hasKey = !CaptionKeychain.read().isEmpty; captionCount = Int(model.engine.captionCount(sourceId))
        guard !source.isEmpty else { return }
        loading = Task {
            let cached = await Task.detached(priority: .userInitiated) { CaptionTranscriber.load(source) }.value
            guard !Task.isCancelled, model.primarySelection == sourceId, words.isEmpty else { return }
            setWords(cached, from: cached.isEmpty ? nil : "cache")
        }
    }
    private func setWords(_ value: [CaptionWord], from source: String?) {
        words = value; transcriptSource = source; editing = nil; fillers = []
        let revision = UUID(); wordRevision = revision
        let engine = model.engine
        Task {
            let marked = await Task.detached(priority: .userInitiated) {
                Set(value.filter { engine.isFillerWord($0.word) }.map(\.id))
            }.value
            if wordRevision == revision {
                let current = Dictionary(uniqueKeysWithValues: words.map { ($0.id, $0.word) })
                let unchanged = Set(value.filter { marked.contains($0.id) && current[$0.id] == $0.word }.map(\.id))
                let edited = Set(value.filter { current[$0.id] != $0.word }.map(\.id))
                fillers = unchanged.union(fillers.intersection(edited))
            }
        }
    }
    private func persist() {
        let source = sourcePath, snapshot = words
        guard !source.isEmpty else { return }
        Task.detached(priority: .utility) { CaptionTranscriber.save(snapshot, path: source) }
    }
    private func edit(_ wordId: UUID, text: String) {
        guard let index = words.firstIndex(where: { $0.id == wordId }) else { return }
        let text = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if text.isEmpty { words.remove(at: index) }
        else { words[index].word = text }
        fillers.remove(wordId)
        if !text.isEmpty && model.engine.isFillerWord(text) { fillers.insert(wordId) }
        editing = nil; persist()
    }
    private func importSRT(_ result: Result<URL, Error>) {
        let sourceId = id, engine = model.engine
        job = Task {
            do {
                let url = try result.get()
                let text = try await Task.detached(priority: .userInitiated) {
                    let access = url.startAccessingSecurityScopedResource()
                    defer { if access { url.stopAccessingSecurityScopedResource() } }
                    return try String(contentsOf: url, encoding: .utf8)
                }.value
                let parsed = engine.parseSRT(text).compactMap(CaptionWord.init)
                guard !Task.isCancelled, model.primarySelection == sourceId else { return }
                if parsed.isEmpty { error = "Esse arquivo não tem legendas SRT legíveis."; return }
                error = nil; setWords(parsed, from: "SRT"); persist()
            } catch { if !Task.isCancelled { self.error = error.localizedDescription } }
        }
    }
    private func transcribe() {
        guard busy == nil else { return }
        let sourceId = id, engine = model.engine, selectedLanguage = language
        guard !path.isEmpty else { error = "Esta camada não tem mídia com som."; return }
        busy = "Preparando modelo Whisper local…"; error = nil
        job = Task {
            do {
                let modelFile = try await Task.detached(priority: .utility) { try await CaptionTranscriber.prepareModel() }.value
                try Task.checkCancellation()
                busy = "Transcrevendo no aparelho…"
                let ticker = Task { @MainActor in
                    while !Task.isCancelled { try? await Task.sleep(nanoseconds: 400_000_000); if !Task.isCancelled { busy = "Whisper local: \(engine.captionProgress(false))%" } }
                }
                defer { ticker.cancel() }
                let result = try await withTaskCancellationHandler {
                    try await Task.detached(priority: .utility) {
                        try engine.transcribeLocal(sourceId, model: modelFile.path, language: selectedLanguage).compactMap(CaptionWord.init)
                    }.value
                } onCancel: { _ = engine.captionProgress(true) }
                try Task.checkCancellation()
                guard model.primarySelection == sourceId else { busy = nil; return }
                setWords(result, from: "Whisper local"); busy = nil; persist(); apply()
            } catch { if !Task.isCancelled { self.error = error.localizedDescription }; busy = nil }
        }
    }
    private func apply() {
        guard busy == nil, !words.isEmpty else { return }
        guard words.allSatisfy({ $0.start.isFinite && $0.end.isFinite && $0.start >= 0 && $0.end > $0.start && !$0.word.isEmpty }) else {
            error = "Confira os tempos das palavras antes de aplicar."; return
        }
        persist(); model.captionOptions = options; busy = "Criando legendas…"; error = nil
        let sourceId = id, engine = model.engine, snapshot = words.map(\.native), settings = options
        job = Task {
            let result = await Task.detached(priority: .userInitiated) {
                engine.createCaptions(sourceId, words: snapshot, options: settings)
            }.value
            model.refreshModel(force: true)
            guard model.primarySelection == sourceId else { return }
            busy = nil; error = result.isEmpty ? nil : result; captionCount = Int(engine.captionCount(sourceId))
        }
    }
    private var options: [String: NSNumber] {
        var result = model.captionOptions
        result.merge(["mode": NSNumber(value: mode), "style": NSNumber(value: style), "maxWords": NSNumber(value: maxWords),
            "maxChars": NSNumber(value: maxChars), "maxLines": NSNumber(value: maxLines), "highlight": NSNumber(value: highlight),
            "uppercase": NSNumber(value: uppercase), "breakOnPause": NSNumber(value: breakOnPause), "removeFillers": NSNumber(value: removeFillers),
            "posY": NSNumber(value: posY), "sizeFrac": NSNumber(value: size)]) { _, fresh in fresh }
        return result
    }
    private func loadOptions() {
        let v = model.captionOptions
        mode = (v["mode"]?.intValue ?? 0).clamped(to: 0...1); style = (v["style"]?.intValue ?? 2).clamped(to: 0...5)
        maxWords = (v["maxWords"]?.intValue ?? 4).clamped(to: 1...12); maxChars = (v["maxChars"]?.intValue ?? 18).clamped(to: 6...60); maxLines = (v["maxLines"]?.intValue ?? 2).clamped(to: 1...4)
        highlight = v["highlight"]?.boolValue ?? true; uppercase = v["uppercase"]?.boolValue ?? false
        breakOnPause = v["breakOnPause"]?.boolValue ?? true; removeFillers = v["removeFillers"]?.boolValue ?? true
        posY = (v["posY"]?.floatValue ?? 0.78).clamped(to: 0.1...0.9); size = (v["sizeFrac"]?.floatValue ?? 0.065).clamped(to: 0.02...0.15)
    }
}

private struct CaptionChips: View {
    let options: [String]
    let selected: Int
    let onPick: (Int) -> Void
    var body: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 6) {
                ForEach(Array(options.enumerated()), id: \.offset) { index, title in
                    Button { onPick(index) } label: {
                        Text(title).font(.aurea(size: 12)).foregroundStyle(index == selected ? AureaColors.accent : AureaColors.text)
                            .padding(.horizontal, 10).padding(.vertical, 6)
                            .background(index == selected ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                    }.buttonStyle(AureaPressStyle())
                }
            }.frame(height: 44)
        }.frame(height: 44)
    }
}

private struct CaptionAction: View {
    let label: String
    var primary = false
    var enabled = true
    let action: () -> Void
    var body: some View {
        Button(action: action) {
            Text(label).font(.aurea(size: 13, weight: .semibold))
                .foregroundStyle(!enabled ? AureaColors.muted : primary ? AureaColors.accent : AureaColors.text)
                .padding(.horizontal, 14).padding(.vertical, 10)
                .background(!enabled ? AureaColors.chip.opacity(0.4) : primary ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
        }.buttonStyle(AureaPressStyle()).disabled(!enabled)
    }
}

private struct CaptionWordEditor: View {
    let word: CaptionWord
    let onDone: (String) -> Void
    @State private var text = ""
    var body: some View {
        HStack(spacing: 8) {
            Text(numeroPtBr(Float(word.start), casas: 2) + " s").font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
            TextField("", text: $text).font(.aurea(size: 14)).foregroundStyle(AureaColors.text)
                .tint(AureaColors.accent).padding(10).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                .submitLabel(.done).onSubmit { onDone(text) }
            CaptionAction(label: AureaText.t("pn_ok"), primary: true) { onDone(text) }
            CaptionAction(label: AureaText.t("pn_caption_delete_word")) { onDone("") }
        }.padding(.top, 10).onAppear { text = word.word }
    }
}
