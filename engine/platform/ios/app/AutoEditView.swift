// =============================================================================
//  AutoEditView.swift — Edição automática (1.0.8)
//
//  A tela só escolhe as opções, transcreve (Whisper local, como as legendas) e
//  manda tudo ao motor: as regras (cortes, zoom, sons, música, voz, cor,
//  legendas) moram em engine/src/autoedit e a montagem em Engine::auto_edit.
//  O par do Android é autoedit/AutoEdit.kt, com os mesmos identificadores de
//  teste ("autoedit.*", "edit.dock.autoEdit", "newProject.autoEdit").
// =============================================================================
import SwiftUI

/// As opções da folha = o bloco "options" do pedido ao motor.
struct AutoEditOptions: Equatable {
    var intensity = 1      // 0 leve, 1 média, 2 forte
    var recording = 0      // 0 com retomadas, 1 contínua (entrevista)
    var language = AutoEditOptions.defaultLanguage
    var cuts = true, zoom = true, sounds = true, music = true, voice = true, color = true, captions = true

    static var defaultLanguage: String {
        switch String((Locale.preferredLanguages.first ?? "pt").prefix(2)) { case "en": return "en"; case "es": return "es"; default: return "pt" }
    }
    var dictionary: [String: Any] {
        ["intensity": intensity, "recording": recording, "language": language, "cuts": cuts, "zoom": zoom,
         "sounds": sounds, "music": music, "voice": voice, "color": color, "captions": captions]
    }
}

/// Etapas mostradas (−1 = transcrevendo; 0..10 = as do motor, Engine::autoEditProgress).
let autoEditStepKeys = ["autoedit_step_prepare", "autoedit_step_audio", "autoedit_step_cuts", "autoedit_step_faces",
                        "autoedit_step_zoom", "autoedit_step_sounds", "autoedit_step_music", "autoedit_step_voice",
                        "autoedit_step_color", "autoedit_step_build", "autoedit_step_build"]

/// "aurea.light.light_leak" → id do tipo (FNV-1a 32, o mesmo de effect_type_id no motor).
func autoEditEffectTypeId(_ key: String) -> UInt32 {
    var h: UInt32 = 0x811C9DC5
    for b in key.utf8 { h ^= UInt32(b); h = h &* 16777619 }
    return h
}

@MainActor
final class AutoEditState: ObservableObject {
    @Published var options = AutoEditOptions()
    @Published private(set) var running = false
    /// −1 transcrevendo, 0..10 etapas do motor, nil parado.
    @Published private(set) var step: Int?
    @Published private(set) var percent = 0
    @Published private(set) var error: String?
    /// Relatório do motor da última edição que deu certo.
    @Published private(set) var report: [String: Any]?
    private var task: Task<Void, Never>?
    /// Home › Edição automática: abre a folha no 1º vídeo do projeto criado agora.
    private var armedAt: Date?

    func open(_ layer: Int64, model: AureaModel) {
        guard !running else { return }
        error = nil; report = nil; step = nil
        model.autoEditTarget = layer
    }

    func close(model: AureaModel) {
        if running { cancel(model: model) }
        model.autoEditTarget = nil
    }

    func arm() { armedAt = Date() }

    /// Chamado quando as camadas mudam: o vídeo importado pela Home abre a folha.
    func onLayers(model: AureaModel) {
        guard let at = armedAt else { return }
        if Date().timeIntervalSince(at) > 180 { armedAt = nil; return }
        if let video = model.layers.first(where: { $0.kind == 1 }) { armedAt = nil; open(video.id, model: model) }
    }

    func cancel(model: AureaModel) {
        task?.cancel()
        _ = model.engine.captionProgress(true)
        _ = model.engine.autoEditProgress(true)
    }

    /// Começa. `injected` pula a transcrição (testes: a fala vem pronta, em segundos da mídia).
    func start(model: AureaModel, injected: [CaptionWord]? = nil) {
        guard let layer = model.autoEditTarget, !running else { return }
        running = true; error = nil; report = nil; percent = 0
        step = injected == nil ? -1 : 0
        let opts = options, engine = model.engine
        let captionJSON = CaptionLook.draft ?? ""
        let lightLeak = !HyperEffectGate.locked(typeId: autoEditEffectTypeId("aurea.light.light_leak"))
        task = Task { @MainActor in
            defer { running = false; step = nil }
            do {
                let words: [CaptionWord]
                if let injected { words = injected } else { words = try await transcribe(layer: layer, language: opts.language, engine: engine) }
                try Task.checkCancellation()
                step = 0
                let request = Self.request(options: opts, words: words, captionJSON: captionJSON, lightLeak: lightLeak)
                let ticker = Task { @MainActor in
                    while !Task.isCancelled {
                        try? await Task.sleep(nanoseconds: 250_000_000)
                        let p = Int(engine.autoEditProgress(false))
                        if p >= 0 && p < 1000 { step = min(10, p / 100); percent = p % 100 }
                    }
                }
                defer { ticker.cancel() }
                let raw = await withTaskCancellationHandler {
                    await Task.detached(priority: .userInitiated) { engine.autoEdit(layer, request: request) }.value
                } onCancel: { _ = engine.autoEditProgress(true) }
                let result = (try? JSONSerialization.jsonObject(with: Data(raw.utf8))) as? [String: Any]
                if let result, (result["ok"] as? Bool) == true {
                    report = result
                    model.refreshModel(force: true)
                    if let id = (result["layer"] as? NSNumber)?.int64Value, id != 0 { model.select(layerId: id, openOptions: false) }
                } else {
                    error = Self.message(result?["error"] as? String ?? "", result?["message"] as? String ?? "")
                }
            } catch is CancellationError {
                error = AureaText.t("autoedit_cancelled")
            } catch {
                self.error = AureaText.t("autoedit_failed", error.localizedDescription)
            }
        }
    }

    private static func message(_ code: String, _ detail: String) -> String {
        switch code {
        case "speed": return AureaText.t("autoedit_error_speed")
        case "short": return AureaText.t("autoedit_error_short")
        case "layer": return AureaText.t("autoedit_need_video")
        case "cancelled": return AureaText.t("autoedit_cancelled")
        default: return AureaText.t("autoedit_failed", detail.isEmpty ? code : detail)
        }
    }

    /// Whisper local (o mesmo modelo das legendas): nenhum áudio sai do aparelho.
    private func transcribe(layer: Int64, language: String, engine: AureaEngine) async throws -> [CaptionWord] {
        let quality = WhisperQuality.initial().usable()
        let modelFile = try await CaptionTranscriber.prepareModel(quality) { _ in }
        try Task.checkCancellation()
        let ticker = Task { @MainActor in
            while !Task.isCancelled {
                try? await Task.sleep(nanoseconds: 300_000_000)
                percent = max(0, min(99, Int(engine.captionProgress(false))))
            }
        }
        defer { ticker.cancel() }
        return try await withTaskCancellationHandler {
            try await Task.detached(priority: .utility) {
                try engine.transcribeLocal(layer, model: modelFile.path, language: language, translateEnglish: false).compactMap(CaptionWord.init)
            }.value
        } onCancel: { _ = engine.captionProgress(true) }
    }

    /// O pedido ao motor: opções, a fala e a biblioteca de sons do usuário (pastas =
    /// categorias; o motor escolhe por papel e sintetiza os papéis que faltarem).
    static func request(options: AutoEditOptions, words: [CaptionWord], captionJSON: String, lightLeak: Bool) -> String {
        var library: [[String: Any]] = []
        for folder in SoundLibrary.load() {
            for sound in folder.sounds {
                library.append(["path": sound.url.path, "name": sound.title, "category": folder.name, "duration": sound.duration])
            }
        }
        let body: [String: Any] = [
            "options": options.dictionary,
            "words": words.map { w -> [String: Any] in ["text": w.word, "start": w.start, "end": w.end] },
            "library": library,
            "captionOptions": captionJSON,
            "lightLeak": lightLeak,
        ]
        guard let data = try? JSONSerialization.data(withJSONObject: body), let text = String(data: data, encoding: .utf8) else { return "{}" }
        return text
    }
}

// -----------------------------------------------------------------------------
// A folha
// -----------------------------------------------------------------------------

/// A apresentação da folha no editor (e o vídeo que a Home acabou de importar).
struct AutoEditPresentation: ViewModifier {
    @ObservedObject var model: AureaModel
    func body(content: Content) -> some View {
        content
            .background(Color.clear.sheet(isPresented: Binding(get: { model.autoEditTarget != nil },
                                                               set: { if !$0 { model.autoEdit.close(model: model) } })) {
                AutoEditSheet(state: model.autoEdit).environmentObject(model).presentationDetents([.large])
            })
            .onChange(of: model.layers.first(where: { $0.kind == 1 })?.id) { _ in model.autoEdit.onLayers(model: model) }
    }
}

struct AutoEditSheet: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject var state: AutoEditState

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                Text(AureaText.t("autoedit_title"))
                    .font(.system(size: 17, weight: .semibold)).foregroundColor(AureaColors.text)
                    .accessibilityAddTraits(.isHeader)
                    .padding(.horizontal, 20).padding(.top, 14)
                Text(AureaText.t("autoedit_subtitle"))
                    .font(.system(size: 12)).foregroundColor(AureaColors.muted)
                    .padding(.horizontal, 20).padding(.vertical, 4)
                section("autoedit_intensity")
                segmented(["autoedit_light", "autoedit_medium", "autoedit_strong"], selected: state.options.intensity, tag: "autoedit.intensity") {
                    state.options.intensity = $0
                }
                section("autoedit_recording")
                segmented(["autoedit_retakes", "autoedit_continuous"], selected: state.options.recording, tag: "autoedit.recording") {
                    state.options.recording = $0
                }
                section("autoedit_language")
                let languages = ["pt", "en", "es"]
                segmented(["autoedit_lang_pt", "autoedit_lang_en", "autoedit_lang_es"],
                          selected: languages.firstIndex(of: state.options.language) ?? 0, tag: "autoedit.language") {
                    state.options.language = languages[$0]
                }
                section("autoedit_steps")
                toggle("autoedit_cuts", "cuts", \.cuts)
                toggle("autoedit_zoom", "zoom", \.zoom)
                toggle("autoedit_sounds", "sounds", \.sounds)
                toggle("autoedit_music", "music", \.music)
                toggle("autoedit_voice", "voice", \.voice)
                toggle("autoedit_color", "color", \.color)
                toggle("autoedit_captions", "captions", \.captions)
                status.padding(.top, 10)
                buttons
            }
            .padding(.bottom, 16)
        }
        .background(AureaColors.editorPanel.ignoresSafeArea())
        .accessibilityIdentifier("autoedit.sheet")
    }

    private func section(_ key: String) -> some View {
        Text(AureaText.t(key))
            .font(.system(size: 12, weight: .semibold)).foregroundColor(AureaColors.muted)
            .padding(.leading, 20).padding(.top, 14).padding(.bottom, 4)
    }

    private func segmented(_ keys: [String], selected: Int, tag: String, onSelect: @escaping (Int) -> Void) -> some View {
        HStack(spacing: 0) {
            ForEach(Array(keys.enumerated()), id: \.offset) { index, key in
                let on = index == selected
                let label = AureaText.t(key)
                Button { onSelect(index) } label: {
                    Text(label).font(.system(size: 13, weight: .semibold)).lineLimit(1)
                        .foregroundColor(on ? AureaColors.onAccent : AureaColors.text)
                        .frame(maxWidth: .infinity, minHeight: 30)
                        .background(RoundedRectangle(cornerRadius: 8).fill(on ? AureaColors.accent : Color.clear))
                        .contentShape(Rectangle())
                }
                .buttonStyle(AureaPressStyle(shrink: 1))
                .disabled(state.running)
                .accessibilityLabel(label)
                .accessibilityAddTraits(on ? [.isSelected] : [])
                .accessibilityIdentifier("\(tag).\(index)")
            }
        }
        .padding(3)
        .background(RoundedRectangle(cornerRadius: 10).fill(AureaColors.chip))
        .padding(.horizontal, 16).padding(.vertical, 4)
    }

    private func toggle(_ key: String, _ id: String, _ path: WritableKeyPath<AutoEditOptions, Bool>) -> some View {
        HStack {
            Text(AureaText.t(key)).font(.system(size: 15)).foregroundColor(AureaColors.text)
            Spacer()
            AureaToggle(checked: state.options[keyPath: path], enabled: !state.running) { state.options[keyPath: path] = $0 }
        }
        .frame(minHeight: 44)
        .padding(.horizontal, 20)
        .accessibilityElement(children: .combine)
        .accessibilityLabel(AureaText.t(key))
        .accessibilityValue(state.options[keyPath: path] ? "on" : "off")
        .accessibilityIdentifier("autoedit.toggle.\(id)")
    }

    @ViewBuilder private var status: some View {
        if let step = state.step {
            let label = step < 0 ? AureaText.t("autoedit_step_transcribe") : AureaText.t(autoEditStepKeys[max(0, min(10, step))])
            VStack(alignment: .leading, spacing: 6) {
                Text("\(label) · \(state.percent)%").font(.system(size: 13)).foregroundColor(AureaColors.text)
                ProgressView(value: Double(state.percent), total: 100).tint(AureaColors.accent)
            }
            .padding(.horizontal, 20).padding(.vertical, 6)
            .accessibilityElement(children: .combine)
            .accessibilityIdentifier("autoedit.progress")
        }
        if let error = state.error {
            Text(error).font(.system(size: 13)).foregroundColor(AureaColors.warning)
                .padding(.horizontal, 20).padding(.vertical, 6)
                .accessibilityIdentifier("autoedit.error")
        }
        if let report = state.report {
            Text(AureaText.t("autoedit_done", Self.clock(report["sourceSeconds"]), Self.clock(report["editSeconds"])))
                .font(.system(size: 13)).foregroundColor(AureaColors.text)
                .padding(.horizontal, 20).padding(.vertical, 6)
                .accessibilityIdentifier("autoedit.result")
        }
    }

    private static func clock(_ value: Any?) -> String {
        guard let s = (value as? NSNumber)?.doubleValue else { return "—" }
        return String(format: "%d:%02d", Int(s / 60), Int(s.truncatingRemainder(dividingBy: 60)))
    }

    private var buttons: some View {
        HStack(spacing: 10) {
            if state.running {
                actionButton("autoedit_cancel", "autoedit.cancel", primary: false) { state.cancel(model: model) }
            } else {
                actionButton("autoedit_close", "autoedit.close", primary: false) { state.close(model: model) }
                actionButton(state.report == nil ? "autoedit_start" : "autoedit_again", "autoedit.start", primary: true) {
                    state.start(model: model)
                }
            }
        }
        .padding(.horizontal, 16).padding(.vertical, 8)
    }

    private func actionButton(_ key: String, _ id: String, primary: Bool, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(AureaText.t(key)).font(.system(size: 15, weight: .semibold))
                .foregroundColor(primary ? AureaColors.onAccent : AureaColors.text)
                .frame(maxWidth: .infinity, minHeight: 46)
                .background(RoundedRectangle(cornerRadius: 12).fill(primary ? AureaColors.accent : AureaColors.chip))
                .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .accessibilityLabel(AureaText.t(key))
        .accessibilityIdentifier(id)
    }
}
