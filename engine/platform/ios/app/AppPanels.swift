import SwiftUI

@MainActor final class LiveNotices: ObservableObject {
    struct Notice: Identifiable {
        var id: String; var text: String; var level: String; var link: URL?; var expires: Date?; var popup: Bool
    }
    @Published private(set) var visible: [Notice] = []
    @Published var popup: Notice?
    private var cached: [Notice] = []
    private var dismissed = Set(UserDefaults.standard.stringArray(forKey: "notices.dismissed") ?? [])
    private var seen = Set(UserDefaults.standard.stringArray(forKey: "notices.seen") ?? [])
    private var lastFetch = Date.distantPast
    init() {
        if let data = UserDefaults.standard.data(forKey: "notices.cache"), let list = try? JSONSerialization.jsonObject(with: data) as? [[String: Any]] { cached = Self.parse(list) }
        expire()
    }
    private func expire() {
        visible = cached.filter { !dismissed.contains($0.id) && ($0.expires == nil || $0.expires! > Date()) }
        popup = visible.first { $0.popup && !seen.contains($0.id) }
    }
    func dismiss(_ id: String) { dismissed.insert(id); UserDefaults.standard.set(Array(dismissed.suffix(100)), forKey: "notices.dismissed"); expire() }
    func closePopup() { if let id = popup?.id { seen.insert(id); UserDefaults.standard.set(Array(seen.suffix(100)), forKey: "notices.seen") }; popup = nil }
    func refresh() async {
        expire(); guard Date().timeIntervalSince(lastFetch) >= 600 else { return }; lastFetch = Date()
        do {
            var request = URLRequest(url: URL(string: "https://mural-do-aurea.aureaapp.workers.dev/aviso")!)
            request.timeoutInterval = 8
            let (stream, response) = try await URLSession.shared.bytes(for: request)
            guard (response as? HTTPURLResponse)?.statusCode == 200 else { return }
            var bytes = Data()
            for try await byte in stream { guard bytes.count < 65_536 else { return }; bytes.append(byte) }
            guard let root = try JSONSerialization.jsonObject(with: bytes) as? [String: Any] else { return }
            let list = root["avisos"] as? [[String: Any]] ?? (root["aviso"] as? [String: Any]).map { [$0] } ?? []
            cached = Self.parse(list)
            // Cache vazio nao substitui o anterior: sem dado, fica o que ja havia.
            if let data = AureaJSONData(list, false) { UserDefaults.standard.set(data, forKey: "notices.cache") }
            expire()
        } catch { /* O cache continua visível quando não há conexão. */ }
    }
    private static func parse(_ list: [[String: Any]]) -> [Notice] {
        let iso = ISO8601DateFormatter()
        return list.prefix(3).compactMap { row in
            let id = (row["id"] as? String ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
            let text = (row["texto"] as? String ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
            guard !id.isEmpty, !text.isEmpty else { return nil }
            let expiry = (row["ate"] as? String ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
            var date = iso.date(from: expiry)
            if date == nil { iso.formatOptions = [.withInternetDateTime, .withFractionalSeconds]; date = iso.date(from: expiry); iso.formatOptions = [.withInternetDateTime] }
            guard expiry.isEmpty || expiry == "null" || date != nil else { return nil }
            let raw = URL(string: row["link"] as? String ?? "")
            let link = raw?.host != nil && ["http", "https"].contains(raw?.scheme ?? "") ? raw : nil
            return Notice(id: id, text: text, level: row["nivel"] as? String ?? "info", link: link, expires: date, popup: row["popup"] as? Bool ?? false)
        }
    }
}

struct LiveNoticeBanners: View {
    @EnvironmentObject private var model: AureaModel
    @StateObject private var notices = LiveNotices()
    var body: some View {
        VStack(spacing: 0) {
            ForEach(notices.visible) { notice in
                VStack(alignment: .leading, spacing: 0) {
                    Text(notice.text).aureaFont(.of(12, tracking: 0.4, lineHeight: 4.0 / 3.0))
                        .foregroundStyle(Color(hex: notice.level == "problema" ? 0xF9DEDC : notice.level == "atencao" ? 0xFFD8E4 : 0xE8DEF8))
                        .fixedSize(horizontal: false, vertical: true)
                    HStack(spacing: 0) {
                        if let link = notice.link { Link(AureaText.t("notice_more"), destination: link).padding(.horizontal, 12).frame(minWidth: 64, minHeight: 48) }
                        Button(AureaText.t("notice_dismiss")) { notices.dismiss(notice.id) }.padding(.horizontal, 12).frame(minWidth: 64, minHeight: 48)
                    }.font(.aurea(size: 14, weight: .medium)).tracking(aureaTracking(0.1)).buttonStyle(.plain).foregroundStyle(AureaColors.text)
                }.padding(.horizontal, 16).padding(.vertical, 4).frame(maxWidth: .infinity, alignment: .leading)
                    // Material Surface ends at the status-bar inset. SwiftUI's
                    // ShapeStyle background otherwise expands behind that bar.
                    .background(Color(hex: notice.level == "problema" ? 0x8C1D18 : notice.level == "atencao" ? 0x633B48 : 0x4A4458), ignoresSafeAreaEdges: [])
            }
        }
            .task { while !Task.isCancelled { await notices.refresh(); try? await Task.sleep(nanoseconds: 60_000_000_000) } }
            .onChange(of: notices.popup?.id) { _ in
                guard let notice = notices.popup else { return }
                model.liveNoticePopup = LiveNoticePopupRequest(notice: notice) {
                    notices.closePopup(); model.liveNoticePopup = nil
                }
            }
    }
}

struct LiveNoticePopupRequest {
    let notice: LiveNotices.Notice
    let onClose: () -> Void
}

struct LiveNoticePopup: View {
    @Environment(\.openURL) private var openURL
    let request: LiveNoticePopupRequest
    var body: some View {
        ZStack {
            Color.black.opacity(0.32).ignoresSafeArea().onTapGesture(perform: request.onClose)
            VStack(alignment: .leading, spacing: 0) {
                Text(AureaText.t("notice_title")).font(.aurea(size: 24)).padding(.bottom, 16)
                Text(request.notice.text).font(.aurea(size: 14)).tracking(aureaTracking(0.25)).foregroundStyle(AureaColors.muted).fixedSize(horizontal: false, vertical: true)
                HStack(spacing: 8) {
                    Spacer(minLength: 0)
                    if let link = request.notice.link {
                        Button(AureaText.t("notice_more")) { request.onClose(); openURL(link) }.frame(minHeight: 48)
                    }
                    Button(AureaText.t("notice_ok"), action: request.onClose).frame(minHeight: 48)
                }.font(.aurea(size: 14, weight: .medium)).foregroundStyle(AureaColors.accent).padding(.top, 24).buttonStyle(.plain)
            }.padding(24).frame(maxWidth: 560).background(AureaColors.surfaceHigh, in: RoundedRectangle(cornerRadius: 28)).padding(.horizontal, 40)
        }.foregroundStyle(AureaColors.text)
    }
}

/// `editor/ProjectSettingsSheet.kt`: changes apply immediately to the core.
/// Omega chrome (área 02, P1-6): o painel da referência (`project_settings_fragment`)
/// — véu de 30 %, painel embaixo no fundo S9 com raio 20 só em cima, entra
/// subindo em 200 ms; a lupa da busca e o ✕ no topo. Fora de grupo: proporção,
/// tamanho livre, resolução, taxa de quadros, duração (extra do AUREA) e fundo.
/// Dentro de grupo (⚙ do grupo): largura × altura e fundo do GRUPO. Embaixo, a
/// seção "Mais do AUREA" com o que morava no menu da engrenagem.
struct ProjectSettingsPanel: View {
    @EnvironmentObject private var model: AureaModel
    var onDismiss: () -> Void = {}
    @State private var free = false
    @State private var pickingBackground = false
    @State private var enter: CGFloat = 1
    private let aspects: [(String, Float)] = [("16:9", 16 / 9), ("9:16", 9 / 16), ("4:5", 4 / 5), ("1:1", 1), ("4:3", 4 / 3)]
    private let resolutions: [(Int, String)] = [(480, "480p (SD)"), (720, "720p (HD)"), (1080, "1080p (FHD)"), (1440, "1440p (QHD)"), (2160, "2160p (4K)")]
    /// Atalhos; qualquer outra taxa (1–240, 23,976, 29,97…) vem de "Personalizado…".
    private let frameRates = [24, 25, 30, 50, 60, 90, 120, 144, 240]
    private let shutterOptions: [Float] = [90, 180, 270, 360]
    private var group: Bool { model.engine.precompDepth > 0 }
    private var id: UInt64 { (model.composition["id"] as? NSNumber)?.uint64Value ?? 0 }
    private var width: Int { Int(model.compositionWidth) }
    private var height: Int { Int(model.compositionHeight) }
    private var ratio: Float { height > 0 ? Float(width) / Float(height) : 16 / 9 }
    private var shortest: Int { min(width, height) }
    private var preset: String? { aspects.first { abs($0.1 - ratio) < 0.01 }?.0 }
    private var background: [Float] {
        let values = (model.composition["background"] as? [NSNumber] ?? []).map(\.floatValue)
        return values.count == 4 ? values : [0, 0, 0, 1]
    }
    /// Alfa 0 = "Transparente" (regra do motor: Composition::set_background_choice).
    private var backgroundTransparent: Bool { background[3] < 0.5 }
    private var backgroundName: String {
        if backgroundTransparent { return AureaText.t("sh_bg_transparent") }
        if background.prefix(3).allSatisfy({ abs($0) < 0.01 }) { return AureaText.t("sh_bg_black") }
        if background.prefix(3).allSatisfy({ abs($0 - 1) < 0.01 }) { return AureaText.t("sh_bg_white") }
        return AureaText.t("editor_personalizada")
    }
    private var cap: (Int, Int) {
        let values = (model.composition["sizeCap"] as? [NSNumber] ?? []).map(\.intValue)
        return values.count >= 2 ? (values[0], values[1]) : (0, 0)
    }

    var body: some View {
        GeometryReader { bounds in
            ZStack(alignment: .bottom) {
                Color.black.opacity(0.3).ignoresSafeArea().onTapGesture(perform: close)
                    .accessibilityIdentifier("projectSettings.scrim")
                VStack(spacing: 0) {
                    HStack(spacing: 0) {
                        Text(AureaText.t(group ? "om_tb_group_settings" : "editor_projeto_cbe9")).font(.aurea(size: 15, weight: .semibold))
                            .lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
                        Button {
                            close()
                            OmegaChromeRequests.shared.commandSearch = true
                        } label: {
                            CupertinoGlyph.text(CupertinoGlyph.Search, size: 19).frame(width: 40, height: 44).contentShape(Rectangle())
                        }.buttonStyle(.plain).accessibilityLabel(AureaText.t("edt_cmd_search_desc")).accessibilityIdentifier("commandSearchOpen")
                        Button(action: close) {
                            CupertinoGlyph.text(CupertinoGlyph.Xmark, size: 18).frame(width: 40, height: 44).contentShape(Rectangle())
                        }.buttonStyle(.plain).accessibilityLabel(AureaText.t("editor_fechar")).accessibilityIdentifier("projectSettings.close")
                    }.padding(.leading, 18).padding(.trailing, 6).padding(.top, 6)
                    ScrollView {
                        VStack(spacing: 0) {
                            if !group { aspectRow }
                            if free || group {
                                settingLine(group ? "om_gs_size" : "editor_tamanho") {
                                    HStack(spacing: 0) {
                                        sizeBox(width, title: "editor_largura", tag: group ? "groupSettings.size.w" : "projectSettings.size.w") { setSize(Int($0.rounded()), height) }
                                        Text("×").font(.aurea(size: 15)).foregroundStyle(AureaColors.muted).padding(.horizontal, 8)
                                        sizeBox(height, title: "editor_altura", tag: group ? "groupSettings.size.h" : "projectSettings.size.h") { setSize(width, Int($0.rounded())) }
                                    }.frame(width: 200).keepLtr()   // largura × altura: a ordem do dado, em qualquer idioma
                                }
                            }
                            if !group {
                                settingLine("editor_resolucao") {
                                    dropdown(resolutions.first { $0.0 == shortest }?.1 ?? "\(shortest)p", key: "res",
                                             tag: "projectSettings.resolution", label: "editor_resolucao")
                                }
                                settingLine("editor_quadros_segundo") {
                                    dropdown(homeFormatFps(model.compositionFps).replacingOccurrences(of: ".", with: ",") + " fps", key: "fps",
                                             tag: "projectSettings.fps", label: "editor_quadros_segundo")
                                }
                                // Extra do AUREA (sem par na referência): a duração da composição.
                                settingLine("editor_duracao") {
                                    Button {
                                        model.numericKeypad = KeypadRequest(title: AureaText.t("editor_duracao"),
                                            value: Float(Double(model.compositionDuration) / model.compositionFps), unit: "s",
                                            min: Float(1 / model.compositionFps), max: 86400, decimals: 3, onValue: { seconds in
                                                let frames = max(1, Int64((Double(seconds) * model.compositionFps).rounded()))
                                                model.mutate { $0.setComposition(id, duration: frames) }
                                                model.refreshModel(force: true)
                                            })
                                    } label: {
                                        Text(String(format: "%.3f s", Double(model.compositionDuration) / model.compositionFps))
                                            .font(.aurea(size: 15, weight: .semibold).monospacedDigit()).foregroundStyle(AureaColors.lightText)
                                            .frame(width: 180, height: 40)
                                            .background(AureaColors.lightSurface, in: RoundedRectangle(cornerRadius: 8))
                                    }.buttonStyle(.plain).accessibilityLabel(AureaText.t("editor_duracao")).accessibilityIdentifier("projectSettings.duration")
                                }
                            }
                            settingLine("editor_plano_fundo") {
                                dropdown(backgroundName, key: "bg", tag: group ? "groupSettings.background" : "projectSettings.background", label: "editor_plano_fundo",
                                         swatch: backgroundTransparent ? Color.clear
                                         : Color(.sRGB, red: Double(background[0]), green: Double(background[1]), blue: Double(background[2]), opacity: 1))
                            }
                            moreInAurea
                            Spacer().frame(height: 16)
                        }.padding(.bottom, 12)
                    }
                }
                .frame(maxHeight: bounds.size.height * 0.85)
                .fixedSize(horizontal: false, vertical: true)
                .frame(maxWidth: .infinity).background(OmegaChrome.column)
                .clipShape(TopCorners())
                .contentShape(Rectangle())
                .onTapGesture {}
                .offset(y: enter * bounds.size.height)
                .accessibilityIdentifier("projectSettings")
            }
        }.foregroundStyle(AureaColors.text)
            .onAppear {
                free = preset == nil
                // Entra subindo em 200 ms (depois de medir).
                withAnimation(.easeOut(duration: 0.2)) { enter = 0 }
            }
            .onDisappear { finishColor() }
    }

    // --- Mais do AUREA -----------------------------------------------------------
    @ViewBuilder private var moreInAurea: some View {
        AureaColors.border.frame(height: 1).padding(.top, 12)
        Text(AureaText.t("om_settings_more")).font(.aurea(size: 13, weight: .bold)).frame(maxWidth: .infinity, alignment: .leading)
            .padding(.horizontal, 18).padding(.top, 12).padding(.bottom, 2).accessibilityIdentifier("projectSettings.more")
        // Interface do projeto (0.2.5): Composição ou Edição, trocável a qualquer momento.
        VStack(alignment: .leading, spacing: 8) {
            Text(AureaText.t("ui_mode_label")).font(.aurea(size: 12, weight: .semibold)).foregroundStyle(AureaColors.muted)
            UiModeChoice(selected: model.uiMode) { model.setUiMode($0) }
        }.padding(.horizontal, 18).padding(.top, 4).padding(.bottom, 4)

        subheader("om_settings_preview")
        settingLine("om_settings_preview_quality") { previewQuality }
        toggleRow("editor_reproducao_loop", on: model.looping, tag: "projectSettings.more.loop") { model.setLooping(!model.looping) }
        actionRow(model.fullscreen ? "editor_sair_tela_cheia" : "editor_tela_cheia", tag: "projectSettings.more.fullscreen") {
            close(); model.fullscreen.toggle()
        }
        toggleRow("editor_desfoque_movimento_composicao", on: model.compMotionBlur, tag: "projectSettings.more.motionBlur",
                  detail: "editor_camadas_desfoque_movimento_so_borram_isto") { model.setCompositionMotionBlur(!model.compMotionBlur) }
        if model.compMotionBlur {
            let shutter = Int(model.shutterAngle)
            HStack(spacing: 0) {
                Text(AureaText.t("sh_menu_shutter", String(shutter))).font(.aurea(size: 15)).lineLimit(2).frame(maxWidth: .infinity, alignment: .leading)
                AureaDropdown(value: "\(shutter)°", tag: "projectSettings.more.shutter", accessibilityLabel: AureaText.t("sh_menu_shutter", String(shutter))) {
                    shutterOptions.map { angle in .item("\(Int(angle))°", selected: Int(angle) == shutter) { model.changeShutterAngle(angle) } }
                }
            }.padding(.horizontal, 18).padding(.vertical, 5)
        }
        toggleRow("editor_diagnostico_tela", on: model.hudVisible, tag: "projectSettings.more.hud",
                  detail: "editor_quadros_segundo_tempos_gpu_memoria_decodificador") { model.toggleHud() }
        actionRow("ios_perf_test_a11y", tag: "projectSettings.more.perfTest") { close(); OmegaChromeRequests.shared.performanceTest = true }

        subheader("om_settings_editing")
        toggleRow("i18n_snapping", on: model.snapping, tag: "projectSettings.more.snapping") { model.snapping.toggle() }
        toggleRow("editor_keyframes_todas_camadas", on: model.showAllKeyframes, tag: "projectSettings.more.allKeys",
                  detail: "editor_keyframes_todas_camadas_detalhe") { model.showAllKeyframes.toggle() }
        toggleRow("editor_timeline_magnetica_modo_edicao", on: model.editMode, tag: "projectSettings.more.magnetic",
                  detail: "editor_aparar_empurra_camadas_seguintes_excluir_fecha") { model.toggleEditMode() }
        actionRow("editor_remover_espacos_vazios", tag: "projectSettings.more.removeGaps") { close(); model.removeGaps() }
        actionRow("editor_aparar_projeto_cabecote", tag: "projectSettings.more.trimProject", enabled: model.status.playhead > 0) {
            close(); model.trimProjectAtPlayhead()
        }

        subheader("editor_marcas_ritmo")
        let marked = model.markerFrames.contains(model.status.playhead)
        toggleRow("editor_marcar_ou_desmarcar_este_instante", on: marked, tag: "projectSettings.more.marker") { model.toggleMarkerAt(model.status.playhead) }
        actionRow("marker_edit", tag: "projectSettings.more.editMarker", enabled: marked) { close(); model.editMarkerAtPlayhead() }
        actionRow("editor_ir_proxima_marca", tag: "projectSettings.more.nextMarker", enabled: !model.markerFrames.isEmpty) {
            close(); model.seekToNextMarker()
        }

        subheader("om_settings_import")
        actionRow("ae_import_menu", tag: "menu.ae_import",
                  detail: model.primarySelection != nil ? "ae_import_layer_detail" : "ae_import_new_layer_detail") {
            close(); OmegaChromeRequests.shared.aeImport = true
        }
    }

    /// Qualidade da prévia (D3): não muda a exportação.
    private var previewQuality: some View {
        let label: String = model.rawPlayback ? "RAW" : model.status.previewAuto != 0 ? "Auto"
            : (max(1, model.status.previewNumerator) >= max(1, model.status.previewDenominator) ? AureaText.t("i18n_preview_full")
               : "1/\(max(1, model.status.previewDenominator) / max(1, model.status.previewNumerator))")
        return AureaDropdown(value: label, tag: "projectSettings.previewQuality", accessibilityLabel: AureaText.t("om_settings_preview_quality")) {
            var entries: [MenuEntry] = []
            if ProxyOptimizer.shared.summary.generating > 0 {
                entries.append(.item(AureaText.t("proxy_optimizing", ProxyOptimizer.shared.summary.percent), enabled: false))
            }
            #if DEBUG
            // Teste de reprodução crua (texto fixo, sem tradução): só no build Debug.
            entries.append(.item(model.rawPlayback ? AureaText.t("ios_raw_back_to_compositor") : "AUREA RAW PLAYBACK TEST",
                                 selected: model.rawPlayback, tag: "rawPlaybackToggle") { model.toggleRawPlayback() })
            #endif
            let names = ["Auto", AureaText.t("i18n_preview_full"), "1/2", "1/4", "1/8"]
            let denominators: [UInt32] = [1, 1, 2, 4, 8]
            for index in names.indices {
                let denominator = denominators[index]
                let on = index == 0 ? model.status.previewAuto != 0 : model.status.previewAuto == 0 && model.status.previewDenominator == denominator
                entries.append(.item(names[index], selected: on) { model.setPreviewScale(num: 1, den: denominator, auto: index == 0) })
            }
            return entries
        }
    }

    private func subheader(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 12, weight: .semibold)).foregroundStyle(AureaColors.muted)
            .frame(maxWidth: .infinity, alignment: .leading).padding(.horizontal, 18).padding(.top, 12).padding(.bottom, 2)
    }

    /// Linha compacta de 40 pt com o interruptor à direita.
    private func toggleRow(_ key: String, on: Bool, tag: String, detail: String? = nil, action: @escaping () -> Void) -> some View {
        HStack(spacing: 10) {
            VStack(alignment: .leading, spacing: 0) {
                Text(AureaText.t(key)).font(.aurea(size: 14))
                if let detail { Text(AureaText.t(detail)).font(.aurea(size: 11)).foregroundStyle(AureaColors.muted) }
            }.frame(maxWidth: .infinity, alignment: .leading)
            AureaToggle(checked: on) { _ in action() }
        }
        .padding(.horizontal, 18).padding(.vertical, 4).frame(minHeight: 40)
        .contentShape(Rectangle())
        .onTapGesture(perform: action)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(AureaText.t(key))
        .accessibilityValue(AureaText.t(on ? "common_on" : "common_off"))
        .accessibilityAddTraits(.isButton)
        .accessibilityAction(.default, action)
        .accessibilityIdentifier(tag)
    }

    /// Linha compacta de 40 pt que faz uma ação (apagada quando não vale agora).
    private func actionRow(_ key: String, tag: String, enabled: Bool = true, detail: String? = nil, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 10) {
                VStack(alignment: .leading, spacing: 0) {
                    Text(AureaText.t(key)).font(.aurea(size: 14)).foregroundStyle(enabled ? AureaColors.text : StageInk.disabledMuted)
                    if let detail { Text(AureaText.t(detail)).font(.aurea(size: 11)).foregroundStyle(AureaColors.muted) }
                }.frame(maxWidth: .infinity, alignment: .leading)
                CupertinoGlyph.text(CupertinoGlyph.ChevronRight, size: 14, color: enabled ? AureaColors.muted : StageInk.disabledMuted).mirrorsInRtl()
            }.padding(.horizontal, 18).padding(.vertical, 4).frame(minHeight: 40).contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .disabled(!enabled)
        .accessibilityIdentifier(tag)
    }

    /// Proporções desenhadas no formato (lado maior 40): borda branca, acesa no destaque.
    /// Os vãos entre elas também tocam (o alvo é 48 × 52).
    private var aspectRow: some View {
        HStack(spacing: 0) {
            ForEach(Array(aspects.enumerated()), id: \.offset) { index, option in
                if index > 0 { Spacer(minLength: 0) }
                let on = !free && preset == option.0
                let ink = on ? AureaColors.accent : AureaColors.text
                Button {
                    free = false
                    if preset != option.0 { applyAspect(option.1) }
                } label: {
                    Text(option.0).font(.aurea(size: 10, weight: .bold)).foregroundStyle(ink)
                        .frame(width: option.1 >= 1 ? 40 : CGFloat(40 * max(0.5, option.1) + 6),
                               height: option.1 >= 1 ? CGFloat(40 / option.1 + 6) : 40)
                        .overlay(RoundedRectangle(cornerRadius: 4).stroke(ink, lineWidth: 1.5))
                        .frame(width: 48, height: 52).contentShape(Rectangle())
                }.buttonStyle(.plain)
                    .accessibilityLabel(AureaText.t("sh_aspect_ratio_desc", option.0))
                    .accessibilityAddTraits(on ? .isSelected : [])
                    .accessibilityIdentifier("projectSettings.aspect.\(option.0)")
            }
            Spacer(minLength: 0)
            Button { free = true } label: {
                CupertinoGlyph.text(CupertinoGlyph.Pencil, size: 18, color: free ? AureaColors.accent : AureaColors.text)
                    .frame(width: 40, height: 40)
                    .overlay(RoundedRectangle(cornerRadius: 6).stroke(free ? AureaColors.accent : AureaColors.text, lineWidth: 1.5))
                    .frame(width: 48, height: 52).contentShape(Rectangle())
            }.buttonStyle(.plain).accessibilityLabel(AureaText.t("editor_tamanho_livre"))
                .accessibilityAddTraits(free ? .isSelected : [])
                .accessibilityIdentifier("projectSettings.aspect.free")
        }.padding(.horizontal, 30).frame(height: 56)
    }
    private func settingLine<Content: View>(_ key: String, @ViewBuilder content: () -> Content) -> some View {
        HStack(spacing: 0) {
            Text(AureaText.t(key)).font(.aurea(size: 15)).lineLimit(2).padding(.trailing, 8).frame(maxWidth: .infinity, alignment: .leading)
            content()
        }.padding(.horizontal, 18).padding(.vertical, 5)
    }
    /// A caixa suspensa clara da referência (180 de largura).
    private func dropdown(_ value: String, key: String, tag: String, label: String, swatch: Color? = nil) -> some View {
        AureaDropdown(value: value, swatch: swatch, tag: tag, accessibilityLabel: AureaText.t(label)) { menuItems(key) }
    }
    private func sizeBox(_ value: Int, title: String, tag: String, onValue: @escaping (Float) -> Void) -> some View {
        Button {
            model.numericKeypad = KeypadRequest(title: AureaText.t(title), value: Float(value), unit: "px", min: 16, max: 8192, decimals: 0, onValue: onValue)
        } label: {
            Text(String(value)).font(.aurea(size: 15, weight: .semibold).monospacedDigit()).foregroundStyle(AureaColors.lightText)
                .frame(maxWidth: .infinity).frame(height: 40).background(AureaColors.lightSurface, in: RoundedRectangle(cornerRadius: 8))
        }.buttonStyle(.plain).accessibilityIdentifier(tag)
    }
    private func menuItems(_ key: String) -> [MenuEntry] {
        if key == "res" {
            return resolutions.map { side, label -> MenuEntry in
                .item(label, selected: shortest == side) { let size = sizeFor(side, ratio: ratio); setSize(size.0, size.1) }
            }
        }
        if key == "fps" {
            let custom = !frameRates.contains { abs(model.compositionFps - Double($0)) < 0.01 }
            let rates: [MenuEntry] = frameRates.map { fps -> MenuEntry in
                .item("\(fps) fps", selected: abs(model.compositionFps - Double(fps)) < 0.01) { setFps(Double(fps)) }
            }
            return rates + [.item(AureaText.t("project_fps_custom"), selected: custom) {
                // fps livre: o teclado limita a 1–240 e aceita decimais (29,97).
                model.numericKeypad = KeypadRequest(title: AureaText.t("project_fps_custom_title"), value: Float(model.compositionFps),
                                                    unit: "fps", min: 1, max: 240, decimals: 3, onValue: { setFps(Double($0)) })
            }]
        }
        return [
            .item(AureaText.t("sh_bg_black"), selected: backgroundName == AureaText.t("sh_bg_black")) { setBackground([0, 0, 0, 1]) },
            .item(AureaText.t("sh_bg_white"), selected: backgroundName == AureaText.t("sh_bg_white")) { setBackground([1, 1, 1, 1]) },
            // Sem fundo: o motor guarda a cor e exporta PNG/GIF com alfa.
            .item(AureaText.t("sh_bg_transparent"), selected: backgroundTransparent) {
                setBackground([background[0], background[1], background[2], 0])
            },
            .item(AureaText.t("editor_outra_cor"), selected: backgroundName == AureaText.t("editor_personalizada")) {
                model.engine.run { $0.beginUndoGroup() }; pickingBackground = true
                // ProjectSettingsSheet.kt passes composition background as display sRGB.
                model.colorSheet = ColorSheetRequest(title: AureaText.t("ds_cor"), initial: Array(background.prefix(3)) + [1], withAlpha: false,
                    onChange: { r, g, b, _ in setBackground([r, g, b, 1]) }, onDone: finishColor)
            },
        ]
    }
    private func applyAspect(_ target: Float) {
        var size = sizeFor(shortest, ratio: target)
        if !fits(size.0, size.1), cap.0 > 0, cap.1 > 0 {
            let factor = min(Float(cap.0) / Float(max(size.0, size.1)), Float(cap.1) / Float(min(size.0, size.1)))
            size = (even(Float(size.0) * factor), even(Float(size.1) * factor))
        }
        setSize(size.0, size.1)
    }
    private func sizeFor(_ short: Int, ratio: Float) -> (Int, Int) {
        ratio >= 1 ? (even(Float(short) * ratio), even(Float(short))) : (even(Float(short)), even(Float(short) / ratio))
    }
    private func even(_ value: Float) -> Int { max(2, Int((value / 2).rounded()) * 2) }
    private func fits(_ width: Int, _ height: Int) -> Bool { cap.0 <= 0 || (max(width, height) <= cap.0 && min(width, height) <= cap.1) }
    private func setSize(_ width: Int, _ height: Int) {
        let w = even(Float(width)), h = even(Float(height))
        guard fits(w, h) else { model.toast = AureaText.t("sh_device_exports_up_to", String(cap.0), String(cap.1)); return }
        model.mutate { $0.setComposition(id, width: UInt32(w), height: UInt32(h)) }; model.refreshModel(force: true)
    }
    private func setFps(_ fps: Double) {
        model.mutate { $0.setComposition(id, fps: fps) }; model.refreshModel(force: true)
    }
    private func setBackground(_ values: [Float]) {
        guard values.count >= 3 else { return }
        // Alfa 0 = "Transparente"; qualquer outro valor = cor opaca (regra do motor).
        let alpha: Float = values.count > 3 && values[3] < 0.5 ? 0 : 1
        model.mutate { $0.setComposition(id, backgroundR: values[0], g: values[1], b: values[2], a: alpha) }; model.refreshModel(force: true)
    }
    private func finishColor() {
        if pickingBackground { model.engine.run { $0.endUndoGroup() }; pickingBackground = false }
    }
    private func close() { finishColor(); onDismiss() }
    private struct TopCorners: Shape {
        var radius: CGFloat = 20
        func path(in rect: CGRect) -> Path {
            var path = Path()
            path.move(to: CGPoint(x: 0, y: rect.maxY)); path.addLine(to: CGPoint(x: 0, y: radius))
            path.addQuadCurve(to: CGPoint(x: radius, y: 0), control: .zero)
            path.addLine(to: CGPoint(x: rect.maxX - radius, y: 0)); path.addQuadCurve(to: CGPoint(x: rect.maxX, y: radius), control: CGPoint(x: rect.maxX, y: 0))
            path.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY)); path.closeSubpath(); return path
        }
    }

}
