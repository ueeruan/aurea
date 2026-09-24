import SwiftUI
import UIKit
import UniformTypeIdentifiers

// TextPanel.kt / TextPathSection.kt. Controls are expanded in the Android order.
struct TextAppearanceControls: View {
    @EnvironmentObject private var model: AureaModel
    @State private var text: [String: Any] = [:]
    @State private var style: [Float] = []
    private var id: Int64 { model.primarySelection ?? 0 }
    private func load() { text = model.engine.text(forLayer: id) ?? [:]; style = model.engine.textStyle(id).map(\.floatValue) }
    private func refresh() { model.refreshModel(force: true); load() }
    private func value(_ key: String, _ fallback: Float = 0) -> Float { (text[key] as? NSNumber)?.floatValue ?? fallback }
    private func set(_ slot: Int, _ value: Float) {
        guard style.count == 18 else { return }; var next = style; next[slot] = value
        _ = model.engine.setTextStyle(id, values: next.map { NSNumber(value: $0) }); refresh()
    }
    private func components(_ key: String) -> [Float] { let v = (text[key] as? [NSNumber] ?? []).map(\.floatValue); return v.count >= 4 ? v : [1, 1, 1, 1] }
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            NativePanelRuler(label: AureaText.t("panel_tamanho"), value: value("size", 72), step: 0.5, range: 4...1000, unit: "px") { model.engine.setText(id, size: $0); refresh() }
            colorRow("panel_cor", values: components("color")) { color("color", "panel_cor") }
            HStack(spacing: 6) {
                Text(AureaText.t("panel_alinhamento")).font(.aurea(size: 13)).frame(maxWidth: .infinity, alignment: .leading)
                ForEach(Array(["panel_esquerda", "panel_centro", "panel_direita"].enumerated()), id: \.offset) { n, key in
                    NativePanelChip(AureaText.t(key), selected: Int(value("alignment")) == n, horizontal: 12) { model.engine.setText(id, alignment: UInt32(n)); refresh() }
                }
            }.frame(height: 48)
            section("panel_contorno")
            colorRow("panel_cor_contorno", values: components("strokeColor")) { color("strokeColor", "panel_cor_contorno") }
            NativePanelRuler(label: AureaText.t("panel_largura_contorno"), value: value("strokeWidth"), step: 0.1, range: 0...60, unit: "px") { model.engine.setText(id, strokeWidth: $0); refresh() }
            if style.count == 18 { styleSections }
            NativeTextPathSection()
            TextAnimationSection()
        }.foregroundStyle(AureaColors.text).onAppear { load() }.onChange(of: id) { _ in load() }
            .onChange(of: model.status.modelRevision) { _ in load() }
    }
    private var styleSections: some View {
        VStack(alignment: .leading, spacing: 0) {
            section("panel_caixa")
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    ForEach(Array(["panel_livre", "panel_paragrafo", "panel_tamanho_fixo", "panel_encolher_caber"].enumerated()), id: \.offset) { n, key in
                        NativePanelChip(AureaText.t(key), selected: Int(style[0]) == n) { set(0, Float(n)) }
                    }
                }.frame(height: 44)
            }
            if style[0] >= 1 {
                styleRow("panel_largura_caixa", 1, 2, 10...20000)
                if style[0] >= 2 { styleRow("panel_altura_caixa", 2, 2, 10...20000) }
            }
            styleToggle("panel_fundo", enabled: 3, color: 4).padding(.top, 6)
            if style[3] > 0.5 {
                styleRow("panel_margem_fundo", 8, 0.2, 0...500)
                styleRow("panel_cantos_arredondados", 9, 0.2, 0...500)
            }
            styleToggle("panel_sombra", enabled: 10, color: 11)
            if style[10] > 0.5 {
                styleRow("panel_distancia_x", 15, 0.2, -500...500)
                styleRow("panel_distancia_y", 16, 0.2, -500...500)
                styleRow("panel_desfoque", 17, 0.1, 0...200)
            }
        }
    }
    private func section(_ key: String) -> some View { Text(AureaText.t(key)).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted).padding(.top, 6) }
    private func styleRow(_ key: String, _ slot: Int, _ step: Float, _ range: ClosedRange<Float>) -> some View {
        NativePanelRuler(label: AureaText.t(key), value: style[slot], step: step, range: range, unit: "px") { set(slot, $0) }
    }
    private func styleToggle(_ key: String, enabled: Int, color: Int) -> some View {
        HStack(spacing: 10) {
            Text(AureaText.t(key)).font(.aurea(size: 13, weight: .bold)).frame(maxWidth: .infinity, alignment: .leading)
            if style[enabled] > 0.5 {
                NativePanelColorWell(values: Array(style[color..<(color + 4)])) {
                    let layer = id
                    model.beginGesture(AureaText.t(key))
                    model.colorSheet = ColorSheetRequest(title: AureaText.t(key), initial: Array(style[color..<(color + 4)]), onChange: { r, g, b, a in
                        var current = model.engine.textStyle(layer).map(\.floatValue)
                        guard current.count == 18 else { return }
                        for (n, v) in [r, g, b, a].enumerated() { current[color + n] = v }
                        _ = model.engine.setTextStyle(layer, values: current.map { NSNumber(value: $0) }); refresh()
                    }, onDone: { model.endGesture() })
                }
            }
            AureaToggle(checked: style[enabled] > 0.5) { set(enabled, $0 ? 1 : 0) }
        }.frame(height: 44)
    }
    private func colorRow(_ key: String, values: [Float], action: @escaping () -> Void) -> some View {
        HStack { Text(AureaText.t(key)).font(.aurea(size: 13)); Spacer(); NativePanelColorWell(values: Array(values.prefix(3)) + [1], action: action) }.frame(height: 48)
    }
    private func color(_ field: String, _ key: String) {
        let layer = id
        model.beginGesture(AureaText.t(key))
        model.colorSheet = ColorSheetRequest(title: AureaText.t(key), initial: components(field), onChange: { r, g, b, a in
            if field == "color" { model.engine.setText(layer, colorR: r, g: g, b: b, a: a) }
            else { model.engine.setText(layer, strokeR: r, g: g, b: b, a: a) }
            refresh()
        }, onDone: { model.endGesture() })
    }
}

private struct NativeTextPathSection: View {
    @EnvironmentObject private var model: AureaModel
    @State private var target: Int64 = 0
    @State private var offset: Float = 0
    @State private var perpendicular = false
    @State private var reversed = false
    private var id: Int64 { model.primarySelection ?? 0 }
    private var candidates: [LayerItem] { model.layers.filter { $0.id != id && $0.kind == 5 && ((model.engine.layerDetail($0.id)?["shapeTypePoints"] as? NSNumber)?.uint32Value ?? 0) & 0xFFFF == 11 } }
    private func load() {
        let v = model.engine.textPath(id)
        guard v.count >= 4 else { return }
        target = v[0].int64Value; offset = v[1].floatValue; perpendicular = v[2].boolValue; reversed = v[3].boolValue
    }
    private func write() {
        _ = model.engine.setTextPath(id, target: target, offset: offset, perpendicular: perpendicular, reversed: reversed)
        model.refreshModel(force: true); load()
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text(AureaText.t("panel_texto_caminho")).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted).padding(.top, 6)
            if candidates.isEmpty && target == 0 {
                Text(AureaText.t("panel_crie_camada_vetorial_desenho_vetorial_servir")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).padding(.vertical, 8)
            } else {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 6) {
                        NativePanelChip(AureaText.t("panel_linha_reta"), selected: target == 0) { target = 0; write() }
                        ForEach(candidates) { layer in NativePanelChip(layer.name, selected: target == layer.id) { target = layer.id; write() } }
                    }.frame(height: 44)
                }
                if target != 0 {
                    NativePanelRuler(label: AureaText.t("panel_margem_inicial"), value: offset, step: 1, range: -20000...20000, unit: "px", plainLabelWidth: 110) { offset = $0; write() }
                    NativePanelToggle(AureaText.t("panel_perpendicular_caminho"), checked: perpendicular, fontSize: 13, weight: .regular) { perpendicular = $0; write() }
                    NativePanelToggle(AureaText.t("panel_inverter_sentido"), checked: reversed, fontSize: 13, weight: .regular) { reversed = $0; write() }
                }
            }
        }.onAppear { load() }.onChange(of: id) { _ in load() }.onChange(of: model.status.modelRevision) { _ in load() }
    }
}

// Literal control geometry shared by the TextPanel and MaskPanel source ports.
struct NativePanelChip: View {
    let title: String
    var selected = false
    var horizontal: CGFloat = 10
    var height: CGFloat? = nil
    var radius: CGFloat = 8
    var fontSize: CGFloat = 12
    var bold = false
    let action: () -> Void
    init(_ title: String, selected: Bool = false, horizontal: CGFloat = 10, height: CGFloat? = nil, radius: CGFloat = 8, fontSize: CGFloat = 12, bold: Bool = false, action: @escaping () -> Void) {
        self.title = title; self.selected = selected; self.horizontal = horizontal; self.height = height; self.radius = radius; self.fontSize = fontSize; self.bold = bold; self.action = action
    }
    var body: some View {
        Button(action: action) {
            Text(title).font(.aurea(size: fontSize, weight: bold ? (selected ? .bold : .medium) : .regular)).foregroundStyle(selected ? AureaColors.accent : AureaColors.text).lineLimit(1)
                .padding(.horizontal, horizontal).padding(.vertical, height == nil ? 6 : 0).frame(height: height)
                .background(selected ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: radius))
        }.buttonStyle(AureaPressStyle(shrink: 1))
    }
}
private struct NativePanelAction: View {
    let key: String
    var detail: String? = nil
    var danger = false
    let action: () -> Void
    init(_ key: String, detail: String? = nil, danger: Bool = false, action: @escaping () -> Void) { self.key = key; self.detail = detail; self.danger = danger; self.action = action }
    var body: some View {
        Button(action: action) {
            VStack(alignment: .leading, spacing: 2) {
                Text(AureaText.t(key)).font(.aurea(size: 14, weight: .semibold)).foregroundStyle(danger ? AureaColors.danger : AureaColors.text)
                if let detail { Text(detail).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted) }
            }.frame(maxWidth: .infinity, alignment: .leading).padding(.horizontal, 14).padding(.vertical, 11)
                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 12))
        }.buttonStyle(AureaPressStyle())
    }
}
private struct NativePanelToggle: View {
    let title: String
    let checked: Bool
    var fontSize: CGFloat = 14
    var weight: Font.Weight = .semibold
    let set: (Bool) -> Void
    init(_ title: String, checked: Bool, fontSize: CGFloat = 14, weight: Font.Weight = .semibold, set: @escaping (Bool) -> Void) { self.title = title; self.checked = checked; self.fontSize = fontSize; self.weight = weight; self.set = set }
    var body: some View {
        HStack {
            Text(title).font(.aurea(size: fontSize, weight: weight)).frame(maxWidth: .infinity, alignment: .leading).contentShape(Rectangle()).onTapGesture { set(!checked) }
            AureaToggle(checked: checked, onCheckedChange: set)
        }.frame(height: 48)
    }
}
struct NativePanelColorWell: View {
    let values: [Float]
    let action: () -> Void
    private var color: Color { values.count >= 4 ? Color(.sRGB, red: Double(values[0]), green: Double(values[1]), blue: Double(values[2]), opacity: Double(values[3])) : .clear }
    var body: some View {
        Button(action: action) { AureaColorSwatch(color: color).frame(width: 30, height: 30).clipShape(RoundedRectangle(cornerRadius: 6)) }.buttonStyle(.plain)
    }
}
private struct NativePanelRuler: View {
    @EnvironmentObject private var model: AureaModel
    let label: String
    let value: Float
    let step: Float
    let range: ClosedRange<Float>
    var unit = ""
    var decimals = 0
    var reset: Float? = nil
    var keypad = false
    var plainLabelWidth: CGFloat? = nil
    var look: KeyframeLook = .none
    var toggleKey: (() -> Void)? = nil
    var expression: ExpressionLook = .none
    var onExpression: (() -> Void)? = nil
    var compactUnit = false
    let set: (Float) -> Void
    @State private var dragging = false
    @State private var live: Float = 0
    private var shown: Float { dragging ? live : value }
    var body: some View {
        HStack(spacing: 0) {
            if let plainLabelWidth { Text(label).font(.aurea(size: 13)).frame(width: plainLabelWidth, alignment: .leading) }
            else {
                PropertyLabelChip(label, expression: expression, keyframe: look, onTap: {})
                    .onLongPressGesture(minimumDuration: 0.5) { onExpression?() }
            }
            Color.clear.frame(width: plainLabelWidth != nil ? 0 : keypad ? 6 : 8)
            TickRuler(value: { shown }, unitsPerDp: step, active: true).frame(maxWidth: .infinity).frame(height: 40)
                .valueDrag(enabled: true, start: { value }, unitsPerDp: { step }, min: range.lowerBound, max: range.upperBound,
                           onStart: { live = value; dragging = true; model.beginGesture(label) }, onValue: { live = $0; set($0) }, onEnd: { dragging = false; model.endGesture() })
            Color.clear.frame(width: keypad ? 6 : 8)
            ValueBox(compactUnit ? numeroPtBr(shown, casas: decimals) + unit : comUnidade(numeroPtBr(shown, casas: decimals), unit), onTap: keypad ? openKeypad : nil)
            if let reset {
                Button { set(reset) } label: { CupertinoGlyph.text(CupertinoGlyph.ArrowCounterclockwise, size: 16, color: AureaColors.muted).frame(width: 34, height: 44) }
                    .buttonStyle(.plain).opacity(abs(shown - reset) > 0.001 * max(1, abs(reset)) ? 1 : 0).disabled(abs(shown - reset) <= 0.001 * max(1, abs(reset)))
            }
            if let toggleKey { Color.clear.frame(width: 4); Button(action: toggleKey) { KeyframeDiamondIcon(look: look, enabled: true).padding(4) }.buttonStyle(.plain) }
        }.frame(height: 48).onDisappear { if dragging { dragging = false; model.endGesture() } }
    }
    private func openKeypad() { model.numericKeypad = KeypadRequest(title: label, value: value, unit: unit, min: range.lowerBound, max: range.upperBound, decimals: decimals) { set($0.clamped(to: range)) } }
}


// PresetsPanel.kt / PresetLibrary.kt. JSON belongs to the shared engine.
enum PanelPresetKind: String, CaseIterable {
    case effects = "efeitos", text = "texto", animation = "animacao", caption = "legenda", curve = "curva"
    var engineId: UInt32 {
        switch self { case .effects: return 0; case .text: return 1; case .animation: return 2; case .caption: return 3; case .curve: return 4 }
    }
    var label: String {
        AureaText.t(self == .effects ? "panel_efeitos" : self == .text ? "panel_texto" : self == .animation ? "panel_animacao" : self == .caption ? "pn_caption" : "panel_curva")
    }
    var saveMessage: String {
        AureaText.t(self == .effects ? "panel_guarda_efeitos_camada_keyframes" : self == .text ? "panel_guarda_estilo_ou_animacao_texto" : self == .animation ? "panel_guarda_keyframes_movimento_camada" : self == .caption ? "panel_guarda_opcoes_atuais_legenda" : "panel_guarda_curva_keyframe_escolhido")
    }
    var noData: String {
        AureaText.t(self == .effects ? "msg_esta_camada_nao_tem_efeitos" : self == .text ? "msg_so_camada_de_texto_tem_estilo" : self == .animation ? "msg_esta_camada_nao_tem_keyframes_de" : self == .curve ? "msg_toque_num_keyframe_com_o_seguinte" : "msg_nada_para_salvar")
    }
    var directory: URL { AureaPaths.documents.appendingPathComponent("presets/\(rawValue)", isDirectory: true) }
    static func fileName(_ name: String) -> String {
        let invalid = CharacterSet.controlCharacters.union(CharacterSet(charactersIn: "/\\:*?\"<>|"))
        let clean = String(name.trimmingCharacters(in: .whitespacesAndNewlines).unicodeScalars.map { invalid.contains($0) ? "_" : String($0) }.joined()
            .trimmingCharacters(in: CharacterSet(charactersIn: ". ")).prefix(60))
        return clean + ".json"
    }
}

private struct PanelPresetEntry: Identifiable {
    let id: String
    let name: String
    let kind: PanelPresetKind
    var json: String?
    var file: URL?
    var textPreset: UInt32?
    var source: String? { json ?? file.flatMap { try? String(contentsOf: $0, encoding: .utf8) } }
    var object: [String: Any]? {
        guard let data = source?.data(using: .utf8) else { return nil }
        return (try? JSONSerialization.jsonObject(with: data)) as? [String: Any]
    }
}

struct PresetsPanel: View {
    @EnvironmentObject private var model: AureaModel
    @State private var picked = "animacao"
    @State private var search = ""
    @State private var stretch = false
    @State private var presets: [PanelPresetEntry] = []
    @State private var favorites = Set<String>()
    @State private var recents: [String] = []
    @FocusState private var searching: Bool

    private var layerId: Int64 { model.primarySelection ?? 0 }
    private var tabs: [String] {
        (["favoritos", "recentes", "animacao", "efeitos", "texto", "legenda", "curva"])
            .filter { $0 != "texto" || model.selectedLayer?.kind == 4 }
            .filter { $0 != "legenda" || [1, 3, 4].contains(model.selectedLayer?.kind ?? 0) }
    }
    private var tab: String { tabs.contains(picked) ? picked : "animacao" }
    private var kind: PanelPresetKind? { PanelPresetKind(rawValue: tab) }
    private var query: String { search.trimmingCharacters(in: .whitespacesAndNewlines) }
    private var entries: [PanelPresetEntry] {
        let values: [PanelPresetEntry]
        if tab == "favoritos" { values = presets.filter { favorites.contains($0.id) } }
        else if tab == "recentes" { values = recents.compactMap { id in presets.first { $0.id == id } } }
        else { values = presets.filter { $0.kind == kind } }
        return values.filter { query.isEmpty || $0.name.range(of: query, options: .caseInsensitive) != nil }
    }

    var body: some View {
        VStack(spacing: 0) {
            PanelHeader(title: AureaText.t("panel_presets"), onBack: { model.panel = .none })
            VStack(spacing: 0) {
                searchField.padding(.top, 6)
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 6) {
                        ForEach(tabs, id: \.self) { value in
                            Button { picked = value } label: {
                                Text(tabLabel(value)).font(.aurea(size: 12.5, weight: tab == value ? .bold : .medium))
                                    .foregroundStyle(tab == value ? AureaColors.accent : AureaColors.text)
                                    .padding(.horizontal, 12).frame(height: 34)
                                    .background(tab == value ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 9))
                            }.buttonStyle(AureaPressStyle())
                        }
                    }.frame(height: 46)
                }
                if kind == .animation {
                    HStack(spacing: 0) {
                        Text(AureaText.t("panel_durar_ate_fim_camada")).font(.aurea(size: 13))
                            .frame(maxWidth: .infinity, alignment: .leading)
                        AureaToggle(checked: stretch) { stretch = $0 }
                    }.frame(height: 40)
                }
                if entries.isEmpty && kind == nil {
                    Text(query.isEmpty ? AureaText.t(tab == "favoritos" ? "panel_toque_preset_guardar_aqui" : "panel_ultimos_10_presets_aplicados_aparecem_aqui") : "Nenhum preset com \"\(query)\".")
                        .font(.aurea(size: 13)).foregroundStyle(AureaColors.muted).multilineTextAlignment(.center)
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                } else {
                    ScrollView {
                        LazyVGrid(columns: [GridItem(.adaptive(minimum: 100), spacing: 8)], spacing: 8) {
                            ForEach(entries) { entry in
                                PresetCardView(entry: entry, showKind: kind == nil, favorite: favorites.contains(entry.id),
                                               compositionWidth: Float(max(1, model.compositionWidth)),
                                               onApply: { searching = false; apply(entry) },
                                               onFavorite: { toggleFavorite(entry) },
                                               onDelete: entry.file == nil ? nil : { askDelete(entry) })
                            }
                            if let kind {
                                Button { askSave(kind) } label: {
                                    VStack(spacing: 0) {
                                        CupertinoGlyph.text(CupertinoGlyph.Plus, size: 24, color: AureaColors.accent)
                                            .frame(maxWidth: .infinity).frame(height: 62)
                                        Color.clear.frame(height: 5)
                                        Text(AureaText.t("panel_salvar_desta_camada")).font(.aurea(size: 11.5, weight: .semibold))
                                            .foregroundStyle(AureaColors.accent).multilineTextAlignment(.center)
                                            .lineLimit(2).frame(height: 30)
                                    }.padding(6).frame(maxWidth: .infinity)
                                        .background(AureaColors.accentDim, in: RoundedRectangle(cornerRadius: 12))
                                }.buttonStyle(AureaPressStyle()).accessibilityLabel("Salvar o da camada como preset")
                            }
                        }.padding(.bottom, 12)
                    }
                }
            }.padding(.horizontal, 12)
        }
        .foregroundStyle(AureaColors.text).background(AureaColors.editorPanel)
        .onAppear { if model.selectedLayer?.kind == 4 { picked = "texto" }; load() }
        .onChange(of: model.language) { _ in load() }
    }

    private var searchField: some View {
        HStack(spacing: 0) {
            CupertinoGlyph.text(CupertinoGlyph.Search, size: 15, color: AureaColors.muted)
                .frame(width: 15, height: 15).padding(.trailing, 8)
            TextField("", text: $search, prompt: Text(AureaText.t("panel_buscar_preset")).foregroundColor(AureaColors.muted))
                .font(.aurea(size: 13.5)).textFieldStyle(.plain).tint(AureaColors.accent)
                .autocorrectionDisabled().textInputAutocapitalization(.never).focused($searching)
            if !search.isEmpty {
                Button { search = "" } label: {
                    CupertinoGlyph.text(CupertinoGlyph.XmarkCircleFill, size: 16, color: AureaColors.muted).frame(width: 34, height: 34)
                }.buttonStyle(AureaPressStyle()).accessibilityLabel("Limpar busca")
            }
        }.padding(.horizontal, 10).frame(height: 38)
            .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
    }
    private func tabLabel(_ value: String) -> String {
        // PresetTab labels are literals in the Android source.
        ["favoritos": "★ Favoritos", "recentes": "Recentes", "animacao": "Animação", "efeitos": "Efeitos", "texto": "Texto", "legenda": "Legenda", "curva": "Curva"][value] ?? value
    }
    private func load() {
        favorites = Set(UserDefaults.standard.stringArray(forKey: "presetFavorites") ?? [])
        recents = UserDefaults.standard.stringArray(forKey: "presetRecents") ?? []
        var result: [PanelPresetEntry] = []
        for kind in PanelPresetKind.allCases {
            if kind == .text {
                let keys = ["pn_pop", "pn_textpreset_bounce", "pn_textpreset_slide", "panel_escala", "pn_textpreset_appear",
                            "panel_desfoque", "pn_textpreset_word_highlight", "pn_karaoke", "pn_textpreset_typewriter", "pn_textpreset_wave", "pn_textpreset_elastic"]
                for (index, key) in keys.enumerated() {
                    result.append(PanelPresetEntry(id: "text:\(index)", name: AureaText.t(key), kind: kind, textPreset: UInt32(index)))
                }
            } else if let url = Bundle.main.url(forResource: kind.rawValue, withExtension: "json", subdirectory: "presets"),
                      let data = try? Data(contentsOf: url), let objects = (try? JSONSerialization.jsonObject(with: data)) as? [[String: Any]] {
                for (index, object) in objects.enumerated() {
                    if let bytes = AureaJSONData(object, false), let json = String(data: bytes, encoding: .utf8) {
                        let name = object["name"] as? String ?? ""
                        result.append(PanelPresetEntry(id: "b:\(kind.rawValue):\(index)", name: name.isEmpty ? AureaText.t("pn_preset_n", index + 1) : name, kind: kind, json: json))
                    }
                }
            }
            let files = ((try? FileManager.default.contentsOfDirectory(at: kind.directory, includingPropertiesForKeys: [.isRegularFileKey])) ?? [])
                .filter { $0.pathExtension == "json" && (try? $0.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true }
                .sorted { $0.lastPathComponent.lowercased() < $1.lastPathComponent.lowercased() }
            for file in files {
                result.append(PanelPresetEntry(id: "user:\(kind.rawValue):\(file.lastPathComponent)", name: file.deletingPathExtension().lastPathComponent, kind: kind, file: file))
            }
        }
        presets = result
    }
    private func markUsed(_ entry: PanelPresetEntry) {
        recents.removeAll { $0 == entry.id }; recents.insert(entry.id, at: 0); recents = Array(recents.prefix(10))
        UserDefaults.standard.set(recents, forKey: "presetRecents")
    }
    private func toggleFavorite(_ entry: PanelPresetEntry) {
        if favorites.contains(entry.id) { favorites.remove(entry.id) } else { favorites.insert(entry.id) }
        UserDefaults.standard.set(Array(favorites), forKey: "presetFavorites")
    }
    private var curveKey: KeyframeItem? {
        let keys = (model.keyframes[layerId] ?? []).filter { $0.property == model.curveProperty && $0.effectIndex == model.curveEffect && $0.paramIndex == model.curveParam }.sorted { $0.time < $1.time }
        guard let time = model.curveSelectedTime, keys.count >= 2, let index = keys.firstIndex(where: { $0.time == time }) else { return nil }
        return keys[min(index, keys.count - 2)]
    }
    private func apply(_ entry: PanelPresetEntry) {
        if entry.kind == .curve {
            guard model.curveSelectedTime != nil else { model.toast = "Toque num keyframe da timeline para aplicar a curva"; return }
            guard let source = entry.source, let ease = presetCurve(model.engine.parseCurvePreset(source)) else { model.toast = "Preset de curva inválido"; return }
            guard let key = curveKey else { model.toast = "Crie pelo menos 2 keyframes para aplicar a curva"; return }
            model.beginGesture("preset de curva")
            for sibling in model.keyframes[layerId] ?? [] where sibling.time == key.time && curveSameGroup(sibling, key) {
                model.engine.editTrackKey(layerId, property: sibling.property, effect: sibling.effectIndex, param: sibling.paramIndex,
                                          time: sibling.time, action: 3, value: sibling.value, targetTime: sibling.time,
                                          interpolation: ease.interpolation, handles: [ease.x1, ease.y1, ease.x2, ease.y2].map { NSNumber(value: $0) })
            }
            model.endGesture(); markUsed(entry)
            model.toast = "Curva \"\(entry.name)\" aplicada"; return
        }
        if entry.kind == .caption {
            guard let source = entry.source else { model.toast = AureaText.t("msg_preset_de_legenda_invalido"); return }
            let parsed = model.engine.parseCaptionPreset(source)
            guard parsed.count >= 15 else { model.toast = AureaText.t("msg_preset_de_legenda_invalido"); return }
            let names = ["mode", "maxWords", "maxChars", "maxLines", "style", "highlight", "uppercase", "breakOnPause", "removeFillers",
                         "pauseSec", "posY", "sizeFrac", "highlightR", "highlightG", "highlightB"]
            let options = Dictionary(uniqueKeysWithValues: names.enumerated().map { ($0.element, parsed[$0.offset]) })
            model.captionOptions = options
            // Applying a style never creates a caption set that did not exist.
            let words = model.engine.captionCount(layerId) > 0 ? CaptionTranscriber.load(model.engine.layerMediaPath(layerId)) : []
            if !words.isEmpty {
                let error = model.engine.createCaptions(layerId, words: words.map(\.native), options: options)
                if !error.isEmpty { model.toast = error; return }
                model.refreshModel(force: true); model.toast = AureaText.t("msg_legendas_refeitas_com", entry.name)
            } else { model.toast = AureaText.t("msg_estilo_de_legenda_escolhido", entry.name) }
            markUsed(entry); return
        }
        guard let layer = model.selectedLayer else { return }
        if let native = entry.textPreset {
            guard layer.kind == 4 else { model.toast = AureaText.t("msg_animacao_de_texto_so_vale_para"); return }
            model.engine.applyTextPreset(layerId, preset: native)
        } else {
            guard let source = entry.source else { model.toast = AureaText.t("msg_arquivo_do_preset_nao_encontrado"); return }
            var duration: Int64 = 0
            if stretch && entry.kind == .animation {
                let start = Int64(layer.startFrame), end = Int64(layer.endFrame), frame = model.status.playhead
                let from = frame >= start && frame < end ? frame : start
                duration = max(1, end - from - 1)
            }
            let error = model.engine.applyPreset(layerId, json: source, duration: duration)
            if !error.isEmpty { model.toast = AureaText.t("msg_preset_nao_aplicado", error); return }
        }
        model.refreshModel(force: true); markUsed(entry); model.toast = AureaText.t("msg_aplicado", entry.name)
    }
    private func askSave(_ kind: PanelPresetKind) {
        searching = false
        model.presetDialog = PresetDialogRequest(mode: .save(kind), exists: { name in
            FileManager.default.fileExists(atPath: kind.directory.appendingPathComponent(PanelPresetKind.fileName(name)).path)
        }, onSave: { name, parts in save(kind, name: name, parts: parts) })
    }
    private func askDelete(_ entry: PanelPresetEntry) {
        searching = false
        model.presetDialog = PresetDialogRequest(mode: .delete(entry.name), onDelete: { delete(entry) })
    }
    private func save(_ kind: PanelPresetKind, name: String, parts: UInt32) {
        let json: String
        if kind == .curve {
            guard let key = curveKey else { model.toast = kind.noData; return }
            let h = model.engine.trackEasing(layerId, property: key.property, effect: key.effectIndex, param: key.paramIndex, time: key.time)
            guard h.count == 4 else { model.toast = kind.noData; return }
            let ease = CurveEase(interpolation: key.interpolation, x1: h[0].floatValue, y1: h[1].floatValue, x2: h[2].floatValue, y2: h[3].floatValue)
            json = model.engine.makeCurvePreset(name, interpolation: key.interpolation, handles: ease.handles.map { NSNumber(value: $0) })
        } else if kind == .caption {
            var options: [String: NSNumber] = ["mode": 0, "maxWords": 4, "maxChars": 18, "maxLines": 2, "style": 2,
                "highlight": true, "uppercase": false, "breakOnPause": true, "removeFillers": true,
                "pauseSec": 0.6, "posY": 0.78, "sizeFrac": 0.065, "highlightR": 1, "highlightG": 0.83, "highlightB": 0]
            options.merge(model.captionOptions) { _, fresh in fresh }
            json = model.engine.makeCaptionPreset(name, options: options)
        } else { json = model.engine.savePreset(layerId, kind: kind.engineId, name: name, parts: parts) }
        guard !json.isEmpty else { model.toast = kind.noData; return }
        let fileName = PanelPresetKind.fileName(name)
        guard fileName != ".json" else { model.toast = AureaText.t("msg_nao_foi_possivel_salvar_o_preset"); return }
        do {
            try FileManager.default.createDirectory(at: kind.directory, withIntermediateDirectories: true)
            try json.write(to: kind.directory.appendingPathComponent(fileName), atomically: true, encoding: .utf8)
            load(); model.toast = AureaText.t("msg_preset_salvo", name)
        } catch { model.toast = AureaText.t("msg_nao_foi_possivel_salvar_o_preset") }
    }
    private func delete(_ entry: PanelPresetEntry) {
        guard let file = entry.file, file.deletingLastPathComponent().standardizedFileURL == entry.kind.directory.standardizedFileURL else { return }
        do {
            try FileManager.default.removeItem(at: file)
            favorites.remove(entry.id); recents.removeAll { $0 == entry.id }
            UserDefaults.standard.set(Array(favorites), forKey: "presetFavorites"); UserDefaults.standard.set(recents, forKey: "presetRecents")
            load(); model.toast = AureaText.t("msg_preset_apagado", entry.name)
        } catch { model.toast = AureaText.t("msg_nao_foi_possivel_apagar") }
    }
}

struct PresetDialogRequest: Identifiable {
    enum Mode { case save(PanelPresetKind), delete(String) }
    let id = UUID()
    let mode: Mode
    let exists: (String) -> Bool
    let onSave: ((String, UInt32) -> Void)?
    let onDelete: (() -> Void)?
    init(mode: Mode, exists: @escaping (String) -> Bool = { _ in false },
         onSave: ((String, UInt32) -> Void)? = nil, onDelete: (() -> Void)? = nil) {
        self.mode = mode; self.exists = exists; self.onSave = onSave; self.onDelete = onDelete
    }
}

struct PresetDialog: View {
    @EnvironmentObject private var model: AureaModel
    let request: PresetDialogRequest
    let onDismiss: () -> Void
    @State private var name = ""
    @State private var style = true
    @State private var animation = true

    var body: some View {
        switch request.mode {
        case .delete(let title):
            AureaAlert(title: "Apagar \"\(title)\"?", message: AureaText.t("panel_preset_sai_deste_aparelho"),
                       confirmLabel: AureaText.t("panel_apagar"), destructive: true,
                       onConfirm: { request.onDelete?() }, onDismiss: onDismiss)
        case .save(let kind):
            AureaAlert(title: AureaText.t("pn_save_preset_of", kind.label.lowercased()), message: kind.saveMessage,
                       confirmLabel: AureaText.t("panel_salvar"), onConfirm: {
                let title = name.trimmingCharacters(in: .whitespacesAndNewlines)
                guard !title.isEmpty else { model.toast = "Dê um nome ao preset"; return }
                let parts: UInt32 = (style ? 1 : 0) | (animation ? 2 : 0)
                guard kind != .text || parts != 0 else { model.toast = "Escolha estilo e/ou animação"; return }
                request.onSave?(title, parts)
            }, onDismiss: onDismiss, extra: AnyView(fields(kind)))
        }
    }
    private func fields(_ kind: PanelPresetKind) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            TextField("", text: Binding(get: { name }, set: { name = String($0.prefix(60)) }))
                .font(.aurea(size: 15)).textFieldStyle(.plain).foregroundStyle(AureaColors.text).tint(AureaColors.accent)
                .padding(.horizontal, 8).padding(.vertical, 7)
                .background(Color(hex: 0x1C1C1E), in: RoundedRectangle(cornerRadius: 7)).padding(.top, 10)
            if !name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && request.exists(name) {
                Text(AureaText.t("panel_ja_existe_sera_substituido")).font(.aurea(size: 12)).foregroundStyle(AureaColors.danger)
            }
            if kind == .text {
                HStack {
                    Text(AureaText.t("panel_estilo")).font(.aurea(size: 13)).frame(maxWidth: .infinity, alignment: .leading)
                    AureaToggle(checked: style) { style = $0 }
                }.frame(height: 40)
                HStack {
                    Text(AureaText.t("panel_animacao")).font(.aurea(size: 13)).frame(maxWidth: .infinity, alignment: .leading)
                    AureaToggle(checked: animation) { animation = $0 }
                }.frame(height: 40)
            }
        }
    }
}

private struct PresetCardView: View {
    let entry: PanelPresetEntry
    let showKind: Bool
    let favorite: Bool
    let compositionWidth: Float
    let onApply: () -> Void
    let onFavorite: () -> Void
    let onDelete: (() -> Void)?
    var body: some View {
        Button(action: onApply) {
            VStack(alignment: .leading, spacing: 0) {
                PresetPreviewView(entry: entry, compositionWidth: compositionWidth).frame(height: 62)
                    .background(AureaColors.stage, in: RoundedRectangle(cornerRadius: 8)).clipShape(RoundedRectangle(cornerRadius: 8))
                Color.clear.frame(height: 5)
                Text(entry.name).font(.aurea(size: 11.5, weight: .semibold)).tracking(-0.1).lineLimit(2)
                    .lineSpacing(max(0, 14 - UIFont.systemFont(ofSize: 11.5, weight: .semibold).lineHeight))
                    .frame(maxWidth: .infinity, alignment: .topLeading).frame(height: 30, alignment: .topLeading)
                if showKind { Text(entry.kind.label).font(.aurea(size: 10)).foregroundStyle(AureaColors.muted).lineLimit(1) }
            }.padding(6).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 12))
        }.buttonStyle(AureaPressStyle()).accessibilityLabel("Aplicar \(entry.name)")
            .overlay(alignment: .topTrailing) {
                Button(action: onFavorite) {
                    CupertinoGlyph.text(favorite ? CupertinoGlyph.StarFill : CupertinoGlyph.Star, size: 15,
                                        color: favorite ? AureaColors.warning : AureaColors.muted)
                        .frame(width: 34, height: 34)
                }.buttonStyle(AureaPressStyle()).padding(6).accessibilityLabel(favorite ? "Tirar dos favoritos" : "Favoritar")
            }
            .overlay(alignment: .topLeading) {
                if let onDelete {
                    Button(action: onDelete) {
                        CupertinoGlyph.text(CupertinoGlyph.Trash, size: 14, color: AureaColors.danger).frame(width: 34, height: 34)
                    }.buttonStyle(AureaPressStyle()).padding(6).accessibilityLabel("Apagar preset")
                }
            }
    }
}

private func presetCurve(_ values: [NSNumber]) -> CurveEase? {
    guard values.count >= 5, values.allSatisfy({ $0.floatValue.isFinite }), (0...6).contains(values[0].intValue) else { return nil }
    return CurveEase(interpolation: values[0].uint32Value, x1: values[1].floatValue, y1: values[2].floatValue, x2: values[3].floatValue, y2: values[4].floatValue)
}

private struct PresetAnimationTrack {
    let property: String
    let keys: [[Float]]
    var rest: Float { keys.last?[1] ?? 0 }
    var span: Float { keys.last?[0] ?? 0 }
    func at(_ frame: Float) -> Float {
        guard let first = keys.first, let last = keys.last else { return 0 }
        if frame <= first[0] { return first[1] }
        for index in 0..<max(0, keys.count - 1) {
            let a = keys[index], b = keys[index + 1]
            if frame < b[0] {
                let u = (frame - a[0]) / max(0.001, b[0] - a[0])
                let mode: UInt32 = a[2] >= 0 && a[2] <= 6 ? UInt32(a[2]) : 2
                let ease = CurveEase(interpolation: mode, x1: a[3], y1: a[4], x2: a[5], y2: a[6])
                return a[1] + (b[1] - a[1]) * ease.transform(u)
            }
        }
        return last[1]
    }
    static func parse(_ object: [String: Any]?) -> [PresetAnimationTrack] {
        (object?["tracks"] as? [[String: Any]] ?? []).compactMap { track in
            let keys = (track["keys"] as? [[NSNumber]] ?? []).compactMap { raw -> [Float]? in
                var values: [Float] = [0, 0, 1, 0.33, 0, 0.67, 1]
                for index in 0..<min(7, raw.count) { values[index] = raw[index].floatValue }
                return values.allSatisfy(\.isFinite) ? values : nil
            }.sorted { $0[0] < $1[0] }
            return keys.isEmpty ? nil : PresetAnimationTrack(property: track["prop"] as? String ?? "", keys: keys)
        }
    }
}

private struct PresetPreviewView: View {
    @EnvironmentObject private var model: AureaModel
    let entry: PanelPresetEntry
    let compositionWidth: Float
    @State private var object: [String: Any]?
    @State private var tracks: [PresetAnimationTrack] = []
    @State private var ease: CurveEase?
    @State private var began = Date()

    var body: some View {
        Group {
            switch entry.kind {
            case .animation:
                if tracks.isEmpty { glyph(CupertinoGlyph.Move) }
                else {
                    SwiftUI.TimelineView(.animation(minimumInterval: 1.0 / 30.0)) { timeline in
                        Canvas { context, size in drawAnimation(context, size: size, elapsed: timeline.date.timeIntervalSince(began)) }
                    }
                }
            case .curve:
                if let ease {
                    SwiftUI.TimelineView(.animation(minimumInterval: 1.0 / 30.0)) { timeline in
                        Canvas { context, size in drawCurve(context, size: size, ease: ease, elapsed: timeline.date.timeIntervalSince(began)) }
                            .padding(.horizontal, 14).padding(.vertical, 10)
                    }
                } else { glyph(CupertinoGlyph.Scribble) }
            case .caption: caption
            case .effects:
                let keys = (object?["effects"] as? [[String: Any]] ?? []).compactMap { $0["key"] as? String }
                glyph(effectGlyph(keys.first), badge: keys.count > 1 ? "\(keys.count) efeitos" : nil)
            case .text:
                Text(AureaText.t("panel_aa")).font(.aurea(size: 24, weight: .bold)).foregroundStyle(AureaColors.text)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .onAppear {
            object = entry.object; tracks = PresetAnimationTrack.parse(object)
            if entry.kind == .curve, let source = entry.source { ease = presetCurve(model.engine.parseCurvePreset(source)) }
            began = Date()
        }
    }
    private func glyph(_ character: Character, badge: String? = nil) -> some View {
        ZStack(alignment: .bottom) {
            CupertinoGlyph.text(character, size: 26, color: AureaColors.accent).frame(maxWidth: .infinity, maxHeight: .infinity)
            if let badge { Text(badge).font(.aurea(size: 9.5)).foregroundStyle(AureaColors.muted).padding(.bottom, 3) }
        }
    }
    private func effectGlyph(_ key: String?) -> Character {
        guard let key else { return CupertinoGlyph.WandStars }
        if key.contains(".blur.") { return CupertinoGlyph.DropFill }
        if key.contains(".light.") { return CupertinoGlyph.Sparkles }
        if key.contains(".color.") { return CupertinoGlyph.ColorFilter }
        if key.contains(".distort.") { return CupertinoGlyph.Scribble }
        return CupertinoGlyph.WandStars
    }
    private func drawAnimation(_ context: GraphicsContext, size: CGSize, elapsed: TimeInterval) {
        let span = max(1, tracks.map(\.span).max() ?? 1)
        let rawFps = (object?["fps"] as? NSNumber)?.floatValue ?? 30
        let fps = rawFps > 0 && rawFps.isFinite ? rawFps : 30
        let loop = Double(span) + Double(fps) * 0.6
        let period = max(0.3, floor(loop / Double(fps) * 1000) / 1000)
        let frame = min(span, Float(elapsed.truncatingRemainder(dividingBy: period) / period * loop))
        func track(_ key: String) -> PresetAnimationTrack? { tracks.last { $0.property == key } }
        func value(_ key: String, _ fallback: Float) -> Float { track(key)?.at(frame) ?? fallback }
        func offset(_ key: String) -> Float { track(key).map { $0.at(frame) - $0.rest } ?? 0 }
        let side = size.height * 0.4
        var drawing = context
        drawing.translateBy(x: size.width / 2 + CGFloat(offset("positionX") / compositionWidth) * size.width,
                            y: size.height / 2 + CGFloat(offset("positionY") / compositionWidth) * size.width)
        drawing.rotate(by: .degrees(Double(value("rotationZ", 0))))
        drawing.scaleBy(x: CGFloat(value("scaleX", 1)), y: CGFloat(value("scaleY", 1)))
        drawing.fill(Path(roundedRect: CGRect(x: -side / 2, y: -side / 2, width: side, height: side), cornerRadius: side * 0.18),
                     with: .color(AureaColors.accent.opacity(Double(value("opacity", 1).clamped(to: 0...1)))))
    }
    private func drawCurve(_ context: GraphicsContext, size: CGSize, ease: CurveEase, elapsed: TimeInterval) {
        let ys = (0...40).map { ease.transform(Float($0) / 40) }
        let low = min(0, ys.min() ?? 0), high = max(1, ys.max() ?? 1)
        func point(_ x: Float, _ y: Float) -> CGPoint {
            CGPoint(x: CGFloat(x) * size.width, y: size.height - CGFloat((y - low) / (high - low)) * size.height)
        }
        for y in [Float(0), 1] {
            var line = Path(); line.move(to: point(0, y)); line.addLine(to: point(1, y))
            context.stroke(line, with: .color(AureaColors.curveGrid), lineWidth: 1)
        }
        var path = Path()
        for (index, y) in ys.enumerated() {
            let p = point(Float(index) / 40, y)
            if index == 0 { path.move(to: p) } else { path.addLine(to: p) }
        }
        context.stroke(path, with: .color(AureaColors.keyframe), style: StrokeStyle(lineWidth: 2, lineCap: .round))
        let x = min(1, Float(elapsed.truncatingRemainder(dividingBy: 1.6) / 1.6) * 1.4), p = point(x, ease.transform(x))
        context.fill(Path(ellipseIn: CGRect(x: p.x - 3.5, y: p.y - 3.5, width: 7, height: 7)), with: .color(AureaColors.accent))
    }
    private var caption: some View {
        let options = object?["caption"] as? [String: Any] ?? [:]
        let one = (options["mode"] as? NSNumber)?.intValue == 1
        let count = one ? 1 : ((options["maxWords"] as? NSNumber)?.intValue ?? 4).clamped(to: 1...4)
        let upper = (options["uppercase"] as? NSNumber)?.boolValue ?? false
        let highlight = (options["highlight"] as? NSNumber)?.boolValue ?? false
        let posY = ((options["posY"] as? NSNumber)?.floatValue ?? 0.8).clamped(to: 0.1...0.9)
        let sizeFraction = (options["sizeFrac"] as? NSNumber)?.floatValue ?? 0.06
        let fontSize = CGFloat((8 + sizeFraction * 90).clamped(to: 8...16))
        let words = [AureaText.t("panel_sua"), "legenda", "aparece", "assim", "aqui", "hoje"]
        let line = words.prefix(count).enumerated().reduce(Text("")) { result, item in
            let word = upper ? item.element.uppercased() : item.element
            return result + Text((item.offset > 0 ? " " : "") + word)
                .foregroundColor(highlight && item.offset == (one ? 0 : 1) ? AureaColors.warning : .white)
        }
        return GeometryReader { geometry in
            line.font(.aurea(size: fontSize, weight: .heavy)).lineLimit(1).fixedSize()
                .frame(width: max(0, geometry.size.width - 8)).clipped()
                .position(x: geometry.size.width / 2, y: (geometry.size.height - fontSize * 1.35) * CGFloat(posY) + fontSize * 1.35 / 2)
        }
    }
}

enum LayerColors {
    static func unpack(_ packed: UInt32) -> Color {
        Color(.sRGB, red: Double(packed & 255) / 255, green: Double((packed >> 8) & 255) / 255,
              blue: Double((packed >> 16) & 255) / 255, opacity: Double((packed >> 24) & 255) / 255)
    }
    static func components(_ color: Color) -> [Float] {
        var r: CGFloat = 0, g: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 1
        UIColor(color).getRed(&r, green: &g, blue: &b, alpha: &a)
        return [Float(r), Float(g), Float(b), Float(a)]
    }
}

// AppearancePanel.kt: original opacity rail and grouped blend thumbnails.
struct AppearancePanel: View {
    @EnvironmentObject private var model: AureaModel
    @State private var tab = 0
    @State private var openCategories = Set<Int>()
    private var id: Int64 { model.primarySelection ?? 0 }
    private var opacity: Float { (model.detail["opacity"] as? NSNumber)?.floatValue ?? 1 }
    private var mode: UInt32 { model.selectedLayer?.blendMode ?? 0 }
    private var keys: [KeyframeItem] { (model.keyframes[id] ?? []).filter { $0.property == 12 && $0.effectIndex == .max && $0.paramIndex == 0 }.sorted { $0.time < $1.time } }
    private var look: KeyframeLook { keys.contains { $0.time == model.localPlayhead } ? .keyHere : keys.isEmpty ? .none : .animated }
    private var expr: ExpressionLook {
        let info = model.engine.expression(id, property: 12, effect: .max, param: 0)
        guard (info["exists"] as? NSNumber)?.boolValue == true else { return .none }
        if (info["enabled"] as? NSNumber)?.boolValue == false { return .off }
        return (info["error"] as? String ?? "").isEmpty ? .ok : .error
    }
    private static let groups: [[Int]] = [[0], [6, 3, 9], [7, 4, 8, 1], [5, 11, 10], [12, 13, 2], [14, 15, 16, 17]]
    private static let titles = ["panel_normal", "pn_blend_darken", "pn_blend_lighten", "pn_blend_cat_contrast", "pn_blend_difference", "pn_blend_color"]
    static let modes = ["panel_normal", "pn_blend_add", "pn_blend_subtract", "pn_blend_multiply", "pn_blend_screen", "pn_blend_overlay",
        "pn_blend_darken", "pn_blend_lighten", "pn_blend_color_dodge", "pn_blend_color_burn", "pn_blend_hard_light", "pn_blend_soft_light",
        "pn_blend_difference", "pn_blend_exclusion", "pn_blend_hue", "pn_blend_saturation", "pn_blend_color", "pn_blend_luminosity"]
    private static let previews: [GraphicsContext.BlendMode] = [.normal, .plusLighter, .normal, .multiply, .screen, .overlay, .darken, .lighten,
        .colorDodge, .colorBurn, .hardLight, .softLight, .difference, .exclusion, .hue, .saturation, .color, .luminosity]
    private func write(_ value: Float) { model.editTransform(12, value: value.clamped(to: 0...100) / 100) }
    private func key() {
        model.mutate { core in
            if look == .keyHere { core.deleteKeyframe(forLayer: id, property: 12, time: model.localPlayhead) }
            else { core.insertKeyframe(forLayer: id, property: 12, time: model.localPlayhead, value: opacity) }
        }
        model.refreshModel(force: true)
    }
    private func curve() {
        guard keys.count >= 2 else { return }
        model.openCurve(property: 12, time: (keys.last { $0.time <= model.localPlayhead } ?? keys[0]).time)
    }
    private func expression() { model.expressionSheet = ExpressionRequest(layer: id, label: AureaText.t("panel_opacidade"), tracks: [ExpressionTrack(property: 12)], scale: 100, unit: "%") }
    private func keypad() { model.numericKeypad = KeypadRequest(title: AureaText.t("panel_opacidade"), value: opacity * 100, unit: "%", min: 0, max: 100, decimals: 0, onValue: write) }
    var body: some View {
        VStack(spacing: 0) {
            PanelHeader(title: AureaText.t("panel_mistura_opacidade")) { model.panel = .none }
            HStack(spacing: 0) {
                LeftRail(keyframeLook: look, onKeyframe: tab == 0 ? key : nil, curveAnimated: look != .none,
                    onCurve: tab == 0 && keys.count >= 2 ? curve : nil, expression: expr, onExpression: tab == 0 ? expression : nil, onBack: { model.panel = .none })
                VStack(spacing: 0) {
                    ParamTabs([AureaText.t("panel_opacidade"), AureaText.t("panel_mistura"), AureaText.t("panel_mascara_recorte")], selected: tab, onSelect: { value in
                        if value == 2 { model.openPanel(.mask) } else { tab = value }
                    }, animated: { $0 == 0 && look != .none })
                    if tab == 0 { opacityBody } else { blendBody }
                }
            }
        }.foregroundStyle(AureaColors.text)
            .onAppear { openCategories = Set(Self.groups.indices.filter { Self.groups[$0].contains(Int(mode)) }) }
    }
    private var opacityBody: some View {
        VStack(spacing: 6) {
            PropertyCustomRow(AureaText.t("panel_opacidade"), selected: true, onSelect: {}, keyframe: look, expression: expr, onExpression: expression) {
                HStack(spacing: 8) {
                    ruler(40)
                    ValueBox("\(Int((opacity * 100).rounded()))%", width: 68, onTap: keypad)
                }
            }
            GeometryReader { bounds in ruler(bounds.size.height) }
        }.padding(.leading, 2).padding(.trailing, 10).padding(.top, 6).padding(.bottom, 10)
    }
    private func ruler(_ height: CGFloat) -> some View {
        TickRuler(value: { opacity * 100 }, unitsPerDp: 0.35, active: true, height: height)
            .valueDrag(enabled: true, start: { opacity * 100 }, unitsPerDp: { 0.35 }, min: 0, max: 100,
                onStart: { model.engine.beginUndoGroup() }, onValue: write, onEnd: { model.engine.endUndoGroup() })
    }
    private var blendBody: some View {
        ScrollView {
            LazyVStack(alignment: .leading, spacing: 0) {
                ForEach(Self.groups.indices, id: \.self) { index in
                    blendCategory(index)
                }
                Text(AureaText.t("pn_blend_help")).font(.aurea(size: 11)).foregroundStyle(AureaColors.muted).lineLimit(3).padding(.top, 4)
            }.padding(.leading, 8).padding(.trailing, 12).padding(.top, 4).padding(.bottom, 12)
        }
    }
    private func blendCategory(_ index: Int) -> some View {
        VStack(spacing: 0) {
            Button {
                if openCategories.contains(index) { openCategories.remove(index) } else { openCategories.insert(index) }
            } label: {
                HStack(spacing: 8) {
                    CupertinoGlyph.text(openCategories.contains(index) ? CupertinoGlyph.ChevronDown : CupertinoGlyph.ChevronRight, size: 13, color: AureaColors.muted)
                    Text(AureaText.t(Self.titles[index])).font(.aurea(size: 13.5, weight: .semibold))
                    Spacer(minLength: 0)
                    if Self.groups[index].contains(Int(mode)) {
                        Text(AureaText.t(Self.modes[Int(mode)])).font(.aurea(size: 12)).foregroundStyle(AureaColors.accent)
                        CupertinoGlyph.text(CupertinoGlyph.CheckmarkCircleFill, size: 16, color: AureaColors.accent)
                    }
                }.frame(height: 40).contentShape(Rectangle())
            }.buttonStyle(.plain)
            if openCategories.contains(index) {
                AureaFlowLayout(hGap: 8, vGap: 8) {
                    ForEach(Self.groups[index], id: \.self) { value in blendChip(value) }
                }.frame(maxWidth: .infinity, alignment: .leading).padding(.bottom, 8)
            }
        }
    }
    private func blendChip(_ value: Int) -> some View {
        Button {
            model.mutate { $0.setLayer(id, blendMode: UInt32(value)) }; model.refreshModel(force: true)
        } label: {
            VStack(spacing: 4) {
                Canvas { context, size in
                    let r = size.height * 0.46
                    context.fill(Path(ellipseIn: CGRect(x: size.width * 0.38 - r, y: size.height / 2 - r, width: r * 2, height: r * 2)), with: .color(AureaColors.blendThumbBottom))
                    context.blendMode = Self.previews[value]
                    context.fill(Path(ellipseIn: CGRect(x: size.width * 0.62 - r, y: size.height / 2 - r, width: r * 2, height: r * 2)), with: .color(AureaColors.blendThumbTop))
                }.frame(width: 34, height: 22).drawingGroup()
                Text(AureaText.t(Self.modes[value])).font(.aurea(size: 9.5)).foregroundStyle(mode == UInt32(value) ? AureaColors.accent : AureaColors.text).lineLimit(1).padding(.horizontal, 4)
            }.padding(.vertical, 8).frame(width: 74).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
                .overlay(RoundedRectangle(cornerRadius: 10).stroke(mode == UInt32(value) ? AureaColors.accent : .clear, lineWidth: 2))
        }.buttonStyle(.plain)
    }
}

// SpeedAudioPanels.kt: logarithmic speed ruler, real remap and mixer controls.
struct SpeedPanel: View {
    @EnvironmentObject private var model: AureaModel
    private var id: Int64 { model.primarySelection ?? 0 }
    private var speed: Float { (model.detail["speed"] as? NSNumber)?.floatValue ?? 1 }
    private var flags: UInt32 { (model.detail["timeFlags"] as? NSNumber)?.uint32Value ?? 0 }
    private var kind: UInt32 { model.selectedLayer?.kind ?? 0 }
    private var remap: Bool { flags & 4 != 0 }
    private var clock: String {
        let seconds = Int((Double(model.selectedLayer?.duration ?? 0) / max(1, model.compositionFps)).rounded())
        return String(format: "%d:%02d", seconds / 60, seconds % 60)
    }
    private var logarithm: Float { log2(speed.clamped(to: 0.05...16)) * 100 }
    private func change(_ action: (AureaEngine) -> Void) { model.mutate(action); model.refreshModel(force: true) }
    private func speedText(_ value: Float) -> String { String(format: "%.2f", value).replacingOccurrences(of: ".", with: ",") + "x" }
    private func setSpeed(_ value: Float) { change { $0.setLayer(id, speed: value.clamped(to: 0.05...16)) } }
    private func snap(_ value: Float) -> Float {
        for tick: Float in [0.25, 0.5, 1, 2, 4] where abs(value - tick) / tick < 0.03 { return tick }
        return (value * 100).rounded() / 100
    }
    var body: some View {
        VStack(spacing: 0) {
            PanelHeader(title: AureaText.t("panel_tempo_velocidade")) { model.panel = .none }
            ScrollView {
                VStack(alignment: .leading, spacing: 0) {
                    if kind != 1 && kind != 3 {
                        hint("panel_velocidade_vale_video_audio_nas_outras", size: 13)
                    } else if speed == 0 {
                        Text("Quadro congelado · " + clock).font(.aurea(size: 12)).foregroundStyle(AureaColors.accent)
                        hint("panel_este_trecho_quadro_parado_apare_bordas", size: 13).padding(.top, 8)
                    } else {
                        Text(speedText(speed) + " · " + clock).font(.aurea(size: 12)).foregroundStyle(AureaColors.accent)
                        hint("panel_inicio_camada_fica_lugar_fim_acompanha", size: 12).padding(.top, 10)
                        HStack(spacing: 6) {
                            CupertinoGlyph.text(CupertinoGlyph.Tortoise, size: 20, color: AureaColors.muted)
                            TickRuler(value: { logarithm }, unitsPerDp: 1, active: true, height: 40)
                                .valueDrag(enabled: true, start: { logarithm }, unitsPerDp: { 1 }, min: -332, max: 332,
                                    onStart: { model.engine.beginUndoGroup() }, onValue: { setSpeed(snap(pow(2, $0 / 100))) },
                                    onEnd: { model.engine.endUndoGroup() })
                            CupertinoGlyph.text(CupertinoGlyph.Hare, size: 20, color: AureaColors.muted)
                            ValueBox(speedText(speed), width: 64).padding(.leading, 2)
                        }.padding(.top, 12)
                        HStack(spacing: 6) {
                            ForEach(Array([Float(0.25), 0.5, 1, 2, 4].enumerated()), id: \.offset) { index, value in
                                speedChip(["0,25x", "0,5x", "1x", "2x", "4x"][index], on: abs(speed - value) < 0.005) { setSpeed(value) }
                            }
                        }.padding(.top, 12)
                        Text(AureaText.t("panel_acelerar_desacelerar_tempo")).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted).padding(.top, 14)
                        ScrollView(.horizontal, showsIndicators: false) {
                            HStack(spacing: 6) {
                                ForEach(Array(["panel_sem_rampa", "panel_suave", "panel_lento_meio", "panel_acelerar", "panel_desacelerar"].enumerated()), id: \.offset) { index, label in
                                    speedChip(AureaText.t(label), on: index == 0 && !remap) {
                                        change { core in
                                            if index == 0 { core.setTimeRemap(false, forLayer: id) }
                                            else { core.applySpeedRamp(UInt32(index), forLayer: id) }
                                        }
                                    }
                                }
                            }
                        }.padding(.top, 6)
                        if remap {
                            TimeRemapEditor().padding(.top, 8)
                            hint("panel_rampa_ligada_som_video_seguem_mesma", size: 12).padding(.top, 6)
                        }
                        if kind == 1 {
                            HStack {
                                Text(AureaText.t("panel_passar_tras_frente")).font(.aurea(size: 13)).frame(maxWidth: .infinity, alignment: .leading)
                                AureaToggle(checked: flags & 1 != 0) { v in change { $0.setLayer(id, reversed: v) } }
                            }.frame(height: 48).padding(.top, 8)
                            HStack(spacing: 6) {
                                Text(AureaText.t("panel_quadros_camera_lenta")).font(.aurea(size: 13)).frame(maxWidth: .infinity, alignment: .leading)
                                ForEach(Array(["panel_repetir_quadro", "panel_misturar", "panel_movimento_suave"].enumerated()), id: \.offset) { index, key in
                                    speedChip(AureaText.t(key), on: (flags & 16 != 0 ? 2 : flags & 8 != 0 ? 1 : 0) == index, padding: 10) {
                                        change { $0.setFrameBlendForLayer(id, mode: UInt32(index)) }
                                    }
                                }
                            }.frame(height: 48)
                        }
                    }
                }.padding(.horizontal, 18).padding(.top, 14).padding(.bottom, 24).frame(maxWidth: .infinity, alignment: .leading)
            }
        }.foregroundStyle(AureaColors.text)
    }
    private func speedChip(_ label: String, on: Bool, padding: CGFloat = 12, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(label).font(.aurea(size: 12)).foregroundStyle(on ? AureaColors.accent : AureaColors.text)
                .padding(.horizontal, padding).padding(.vertical, 6).background(on ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
        }.buttonStyle(.plain)
    }
    private func hint(_ key: String, size: CGFloat) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: size)).foregroundStyle(AureaColors.muted).fixedSize(horizontal: false, vertical: true)
    }
}

struct AudioPanel: View {
    @EnvironmentObject private var model: AureaModel
    private var id: Int64 { model.primarySelection ?? 0 }
    private func scalar(_ key: String, _ fallback: Float = 0) -> Float { (model.detail[key] as? NSNumber)?.floatValue ?? fallback }
    private var flags: UInt32 { (model.detail["audioFlags"] as? NSNumber)?.uint32Value ?? 0 }
    private var fps: Float { Float(max(1, model.compositionFps)) }
    private var volumeKeys: [KeyframeItem] { (model.keyframes[id] ?? []).filter { $0.property == 32 && $0.effectIndex == .max && $0.paramIndex == 0 } }
    private var keyLook: KeyframeLook { volumeKeys.contains { $0.time == model.localPlayhead } ? .keyHere : flags & 8 != 0 ? .animated : .none }
    private var expressionLook: ExpressionLook {
        let info = model.engine.expression(id, property: 32, effect: .max, param: 0)
        guard (info["exists"] as? NSNumber)?.boolValue == true else { return .none }
        if (info["enabled"] as? NSNumber)?.boolValue == false { return .off }
        return (info["error"] as? String ?? "").isEmpty ? .ok : .error
    }
    private func change(_ action: (AureaEngine) -> Void) { model.mutate(action); model.refreshModel(force: true) }
    private func decibel(_ value: Float) -> String {
        guard value > 0 else { return "−∞ dB" }
        let v = (20 * log10(value) * 10).rounded() / 10
        return (v > 0 ? "+" : v < 0 ? "−" : "") + String(format: "%.1f", abs(v)).replacingOccurrences(of: ".", with: ",") + " dB"
    }
    private func seconds(_ value: Float) -> String { String(format: "%.2f s", value).replacingOccurrences(of: ".", with: ",") }
    private func toggleVolumeKey() {
        change { core in
            if keyLook == .keyHere { core.deleteKeyframe(forLayer: id, property: 32, time: model.localPlayhead) }
            else { core.insertKeyframe(forLayer: id, property: 32, time: model.localPlayhead, value: scalar("audioVolume", 1)) }
        }
    }
    private func openExpression() { model.expressionSheet = ExpressionRequest(layer: id, label: "Volume", tracks: [ExpressionTrack(property: 32)], scale: 100, unit: "%") }
    var body: some View {
        VStack(spacing: 0) {
            PanelHeader(title: AureaText.t("panel_som")) { model.panel = .none }
            ScrollView {
                VStack(alignment: .leading, spacing: 0) {
                    if flags & 4 == 0 {
                        Text(AureaText.t(model.selectedLayer?.kind == 1 ? "panel_este_video_nao_tem_trilha_som" : "panel_esta_camada_nao_tem_audio")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                    } else {
                        HStack(spacing: 8) {
                            CupertinoGlyph.text(CupertinoGlyph.Speaker2, size: 18, color: AureaColors.accent)
                            Text(AureaText.t("panel_som")).font(.aurea(size: 17, weight: .bold))
                            Spacer(minLength: 0)
                            Text(flags & 1 != 0 || scalar("audioVolume", 1) * scalar("audioGain", 1) <= 0 ? "mudo" : decibel(scalar("audioVolume", 1) * scalar("audioGain", 1))).font(.aurea(size: 13)).foregroundStyle(AureaColors.accent)
                        }.padding(.bottom, 10)
                        audioToggle("panel_mudo", checked: flags & 1 != 0) { v in change { $0.setLayer(id, audioMuted: v) } }
                        audioToggle("panel_solo", checked: flags & 2 != 0) { v in change { $0.setLayer(id, audioSolo: v) } }
                        audioRuler("panel_volume", value: scalar("audioVolume", 1) * 100, text: "\(Int((scalar("audioVolume", 1) * 100).rounded()))%", units: 0.5, min: 0, max: 200,
                            keyframe: keyLook, onKeyframe: toggleVolumeKey, expression: expressionLook, onExpression: openExpression) { v in
                            change { core in
                                if flags & 8 != 0 { core.insertKeyframe(forLayer: id, property: 32, time: model.localPlayhead, value: v / 100) }
                                else { core.setLayer(id, audioVolume: v / 100) }
                            }
                        }
                        audioRuler("panel_reforco", value: scalar("audioGain", 1) <= 0 ? -24 : (20 * log10(scalar("audioGain", 1))).clamped(to: -24...12), text: decibel(scalar("audioGain", 1)), units: 0.1, min: -24, max: 12) { v in
                            change { $0.setLayer(id, audioGain: v <= -24 ? 0 : pow(10, v / 20)) }
                        }
                        let pan = Int((scalar("audioPan") * 100).rounded())
                        audioRuler("panel_esquerda_direita", value: scalar("audioPan") * 100, text: pan == 0 ? "Centro" : pan < 0 ? "E \(-pan)" : "D \(pan)", units: 0.5, min: -100, max: 100) { v in change { $0.setLayer(id, audioPan: v / 100) } }
                        let maxFade = max(0, Float(model.selectedLayer?.duration ?? 0) / fps / 2)
                        audioRuler("panel_entrada_suave", value: scalar("audioFadeIn") / fps, text: seconds(scalar("audioFadeIn") / fps), units: 0.02, min: 0, max: maxFade) { v in change { $0.setLayer(id, fadeIn: Int32((v * fps).rounded())) } }
                        audioRuler("panel_saida_suave", value: scalar("audioFadeOut") / fps, text: seconds(scalar("audioFadeOut") / fps), units: 0.02, min: 0, max: maxFade) { v in change { $0.setLayer(id, fadeOut: Int32((v * fps).rounded())) } }
                        Text(AureaText.t("panel_volume_sobe_desce_forma_natural_sem")).font(.aurea(size: 11)).foregroundStyle(AureaColors.muted).padding(.top, 6).fixedSize(horizontal: false, vertical: true)
                        if model.selectedLayer?.kind == 1 {
                            Button {
                                let added = model.engine.extractAudio(fromLayer: id)
                                if added > 0 { model.refreshModel(force: true); model.select(layerId: added, additive: false) }
                                else { model.toast = model.engine.lastImportError }
                            } label: {
                                HStack(spacing: 8) {
                                    CupertinoGlyph.text(CupertinoGlyph.MusicNote2, size: 16, color: AureaColors.accent)
                                    Text(AureaText.t("panel_extrair_audio_camada")).font(.aurea(size: 13))
                                    Spacer(minLength: 0)
                                }.padding(.horizontal, 12).frame(height: 44).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
                            }.buttonStyle(.plain).padding(.top, 12)
                        }
                    }
                }.padding(.horizontal, 18).padding(.top, 14).padding(.bottom, 24).frame(maxWidth: .infinity, alignment: .leading)
            }
        }.foregroundStyle(AureaColors.text)
    }
    private func audioToggle(_ key: String, checked: Bool, onChange: @escaping (Bool) -> Void) -> some View {
        HStack {
            Text(AureaText.t(key)).font(.aurea(size: 13)).frame(maxWidth: .infinity, alignment: .leading)
            AureaToggle(checked: checked, onCheckedChange: onChange)
        }.frame(height: 48)
    }
    private func audioRuler(_ key: String, value: Float, text: String, units: Float, min: Float, max: Float,
        keyframe: KeyframeLook = .none, onKeyframe: (() -> Void)? = nil, expression: ExpressionLook = .none,
        onExpression: (() -> Void)? = nil, onValue: @escaping (Float) -> Void) -> some View {
        PropertyCustomRow(AureaText.t(key), selected: keyframe != .none, onSelect: { onKeyframe?() }, keyframe: keyframe, expression: expression, onExpression: onExpression) {
            HStack(spacing: 8) {
                TickRuler(value: { value }, unitsPerDp: units, active: true, height: 40)
                    .valueDrag(enabled: true, start: { value }, unitsPerDp: { units }, min: min, max: max,
                        onStart: { model.engine.beginUndoGroup() }, onValue: onValue, onEnd: { model.engine.endUndoGroup() })
                ValueBox(text)
            }
        }
    }
}

struct ShapePanel: View {
    @EnvironmentObject private var model: AureaModel
    var geometry = false
    @State private var params: [Float] = []
    @State private var axis = 0
    @State private var sizeDragging = false
    @State private var sizeLive: Float = 0
    @State private var sizeStart: [Float] = [1, 1]
    @State private var gestureOpen = false
    private var id: Int64 { model.primarySelection ?? 0 }
    private var shape: [NSNumber] { model.detail["shape"] as? [NSNumber] ?? [] }
    private func value(_ index: Int) -> Float { index < params.count ? params[index] : 0 }
    private func load() { params = model.engine.shapeParams(id).map(\.floatValue) }
    private var shapeType: Int { shape.first.map { Int($0.uint32Value & 0xFFFF) } ?? Int(value(0)) }
    private var selected: Int { model.shapeSelectedParam }
    private var fill: [Float] { rgba(1) }
    private var stroke: [Float] { rgba(2) }
    private let types = [0, 1, 3, 4, 5, 6, 7, 8, 9, 10]
    private var track: [KeyframeItem] {
        (model.keyframes[id] ?? []).filter { $0.property == 35 && $0.paramIndex == UInt32(selected) }.sorted { $0.time < $1.time }
    }
    private func look(_ param: Int) -> KeyframeLook {
        if UInt32(value(8)) & (1 << UInt32(param)) != 0 { return .keyHere }
        return UInt32(value(7)) & (1 << UInt32(param)) != 0 ? .animated : .none
    }

    var body: some View {
        VStack(spacing: 0) {
            PanelHeader(title: AureaText.t(geometry ? "panel_editar_forma" : "panel_cor_preenchimento")) { finishGesture(); model.panel = .none }
            if geometry {
                if model.selectedLayer?.kind != 5 {
                    PanelNotice(AureaText.t("panel_escolha_forma_editar_silhueta")).padding(.horizontal, 18)
                } else if model.isVectorLayer {
                    PanelNotice(AureaText.t("panel_esta_camada_vetorial_edite_caminhos_painel")).padding(.horizontal, 18)
                } else { editBody }
            } else { fillBody }
        }.foregroundStyle(AureaColors.text)
            .onAppear { load(); if geometry { model.shapeSelectedParam = axis == 0 ? 5 : 6 } }
            .onChange(of: id) { _ in finishGesture(); load() }
            .onChange(of: model.status.modelRevision) { _ in load() }
            .onChange(of: model.status.playhead) { _ in load() }
            .onDisappear { finishGesture() }
    }

    private var fillBody: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                kitTitle("panel_preenchimento")
                HStack(spacing: 0) {
                    Text(AureaText.t("panel_preencher")).font(.aurea(size: 14, weight: .semibold)).frame(maxWidth: .infinity, alignment: .leading)
                    NativePanelColorWell(values: Array(fill.prefix(3)) + [1]) { openColor(stroke: false) }
                    Color.clear.frame(width: 12)
                    AureaToggle(checked: fill[3] > 0) { on in setColor(Array(fill.prefix(3)) + [on ? 1 : 0], stroke: false, layer: id) }
                }.frame(height: 48)
                kitTitle("panel_contorno")
                HStack(spacing: 0) {
                    Text(AureaText.t("panel_cor_contorno")).font(.aurea(size: 13.5)).frame(maxWidth: .infinity, alignment: .leading)
                    NativePanelColorWell(values: Array(stroke.prefix(3)) + [1]) { openColor(stroke: true) }
                }.frame(height: 48).contentShape(Rectangle()).onTapGesture { openColor(stroke: true) }
                ShapeHumanRow(label: AureaText.t("panel_largura"), value: value(4), step: 0.2, range: 0...500, unit: "px", reset: 0,
                              onStart: { beginGesture("contorno") }, onValue: { write(4, $0) }, onEnd: finishGesture,
                              onCommit: { v in
                                  beginGesture("contorno")
                                  if v > 0 && stroke[3] <= 0 { setColor(Array(stroke.prefix(3)) + [1], stroke: true, layer: id) }
                                  write(4, v); finishGesture()
                              })
                if value(4) > 0 && stroke[3] <= 0 { kitHint(AureaText.t("panel_contorno_esta_sem_cor_escolha_cor")) }
            }.padding(.leading, 18).padding(.trailing, 12).padding(.top, 8).padding(.bottom, 24)
        }
    }

    private var editBody: some View {
        HStack(spacing: 0) {
            LeftRail(keyframeLook: look(selected), onKeyframe: markKey, curveAnimated: look(selected) != .none,
                     onCurve: track.count >= 2 ? openCurve : nil, onBack: { finishGesture(); model.panel = .none })
            ScrollView {
                VStack(alignment: .leading, spacing: 0) {
                    switcher
                    Color.clear.frame(height: 4)
                    sizeRow
                    if shapeType == 0 {
                        shapeRow(1, "panel_raio", value: value(1), step: 0.3, range: 0...max(0, min(value(5), value(6)) / 2), unit: "px", reset: 0, gesture: "raio") { write(1, $0) }
                    }
                    if [3, 4, 8].contains(shapeType) {
                        shapeRow(2, shapeType == 3 ? "panel_lados" : shapeType == 8 ? "panel_petalas" : "panel_pontas", value: value(2), step: 0.06, range: 3...64, reset: 5, gesture: "pontas") { write(2, $0.rounded()) }
                    }
                    if shapeType == 4 {
                        shapeRow(3, "panel_raio_interno", value: value(3) * 100, step: 0.3, range: 5...95, unit: "%", reset: 50, gesture: AureaText.t("panel_raio_interno_75cf")) { write(3, $0 / 100) }
                    } else if shapeType == 5 || shapeType == 6 {
                        shapeRow(3, "panel_espessura", value: (shapeType == 6 ? 1 - value(3) : value(3)) * 100, step: 0.3, range: 5...95, unit: "%", reset: 50, gesture: "espessura") { write(3, shapeType == 6 ? 1 - $0 / 100 : $0 / 100) }
                    }
                    Color.clear.frame(height: 6)
                    kitHint(AureaText.t("panel_arraste_alcas_palco_mudar_tamanho") + (shapeType == 0 ? "; a alça azul arredonda os cantos. " : ". ") + AureaText.t("panel_losango_trilho_grava_keyframe_linha_acesa"))
                }.padding(.top, 6).padding(.trailing, 10).padding(.bottom, 16)
            }
        }
    }

    private var switcher: some View {
        HStack(spacing: 0) {
            switchButton(CupertinoGlyph.ChevronLeft, label: "Forma anterior", delta: -1)
            ShapeEditGlyph(type: shapeType).frame(width: 26, height: 26)
                .frame(width: 44, height: 40).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
            switchButton(CupertinoGlyph.ChevronRight, label: "Próxima forma", delta: 1)
            Color.clear.frame(width: 6)
            Text(shapeName).font(.aurea(size: 15, weight: .bold)).frame(maxWidth: .infinity, alignment: .leading)
            Button { model.shapeSizeLinked.toggle() } label: {
                MaterialGlyph(model.shapeSizeLinked ? "rounded.Link" : "rounded.LinkOff", size: 20, color: model.shapeSizeLinked ? AureaColors.accent : AureaColors.text)
                    .frame(width: 44, height: 36)
                    .background(model.shapeSizeLinked ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                    .overlay(RoundedRectangle(cornerRadius: 8).stroke(model.shapeSizeLinked ? AureaColors.accent : .clear, lineWidth: 1.5))
            }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityLabel(model.shapeSizeLinked ? "Soltar proporção" : "Manter proporção")
        }.frame(height: 52)
    }
    private func switchButton(_ glyph: Character, label: String, delta: Int) -> some View {
        Button {
            let index = types.firstIndex(of: shapeType) ?? 0, next = types[(index + delta + types.count) % types.count]
            beginGesture("trocar forma")
            // Type 0 is a command; the animated shape API only accepts 1...6.
            model.mutate { $0.setShape(id, param: 0, value: Float(next)) }
            finishGesture(); load()
        } label: { CupertinoGlyph.text(glyph, size: 18).frame(width: 44, height: 44) }
            .buttonStyle(AureaPressStyle(shrink: 1)).accessibilityLabel(label)
    }
    private var shapeName: String {
        switch shapeType {
        case 0: return "Retângulo"; case 1: return "Elipse"; case 3: return "Polígono"; case 4: return "Estrela"
        case 5: return "Cruz"; case 6: return "Anel"; case 7: return "Fatia"; case 8: return "Flor"
        case 9: return "Seta"; case 10: return "Triângulo"; default: return "Forma"
        }
    }
    private var sizeRow: some View {
        let title = AureaText.t("panel_tamanho"), current = value(axis == 0 ? 5 : 6)
        return HStack(spacing: 0) {
            PropertyLabelChip(title, expression: .none).aureaSelectedProperty(title)
            Color.clear.frame(width: 6)
            TickRuler(value: { sizeDragging ? sizeLive : current }, unitsPerDp: 1, active: true, height: 40, verticalPadding: 8)
                .frame(maxWidth: .infinity).padding(.vertical, 6)
                .valueDrag(enabled: true, start: { value(axis == 0 ? 5 : 6) }, unitsPerDp: { 1 }, min: 1, max: 16384,
                           onStart: { sizeStart = [value(5), value(6)]; sizeLive = current; sizeDragging = true; model.shapeSelectedParam = axis == 0 ? 5 : 6; beginGesture("tamanho da forma") },
                           onValue: { sizeLive = $0; writeSize($0, from: sizeStart) },
                           onEnd: { sizeDragging = false; finishGesture() })
            Color.clear.frame(width: 6)
            sizeBox(0)
            Color.clear.frame(width: 4)
            sizeBox(1)
        }.frame(height: 52)
    }
    private func sizeBox(_ item: Int) -> some View {
        let shown = item == axis && sizeDragging ? sizeLive : value(item == 0 ? 5 : 6)
        let label = AureaText.t(item == 0 ? "panel_x_largura" : "panel_y_altura")
        let activate: () -> Void = {
            if axis != item { axis = item; model.shapeSelectedParam = item == 0 ? 5 : 6 }
            else {
                let from = [value(5), value(6)]
                model.numericKeypad = KeypadRequest(title: item == 0 ? "Largura" : "Altura", value: shown, unit: "px", min: 1, max: 16384, decimals: 0) { v in
                    beginGesture("tamanho da forma"); writeSize(v, from: from); finishGesture()
                }
            }
        }
        return VStack(spacing: 3) {
            ValueBox(numeroPtBr(shown, casas: 0), enabled: true, tint: item == axis ? AureaColors.accent : .white, width: 58, onTap: activate)
            Text(label).font(.aurea(size: 9)).foregroundStyle(AureaColors.muted)
                .lineLimit(1).frame(width: 58).contentShape(Rectangle()).onTapGesture(perform: activate)
        }.accessibilityLabel(label)
    }
    private func shapeRow(_ param: Int, _ label: String, value: Float, step: Float, range: ClosedRange<Float>, unit: String = "", reset: Float, gesture: String, set: @escaping (Float) -> Void) -> some View {
        ShapeHumanRow(label: AureaText.t(label), value: value, step: step, range: range, unit: unit, reset: reset,
                      selected: selected == param, look: look(param), onSelect: { model.shapeSelectedParam = param },
                      onStart: { model.shapeSelectedParam = param; beginGesture(gesture) }, onValue: set, onEnd: finishGesture,
                      onCommit: { v in model.shapeSelectedParam = param; beginGesture(gesture); set(v); finishGesture() })
    }
    private func rgba(_ index: Int) -> [Float] {
        let packed = index < shape.count ? shape[index].uint32Value : 0
        return (0..<4).map { Float((packed >> UInt32($0 * 8)) & 255) / 255 }
    }
    private func openColor(stroke isStroke: Bool) {
        let layer = id, original = isStroke ? stroke : fill
        beginGesture(isStroke ? "cor do contorno" : "cor da forma")
        model.colorSheet = ColorSheetRequest(title: AureaText.t(isStroke ? "panel_cor_contorno" : "panel_preenchimento"),
                                            initial: Array(original.prefix(3)) + [original[3] > 0 ? original[3] : 1],
                                            onChange: { r, g, b, a in setColor([r, g, b, a], stroke: isStroke, layer: layer) }, onDone: finishGesture)
    }
    private func setColor(_ components: [Float], stroke: Bool, layer: Int64) {
        guard components.count == 4, components.allSatisfy(\.isFinite) else { return }
        // Shape RGBA8 is sRGB already, exactly like Android's rgba8/ColorRequest.
        model.mutate {
            if stroke { $0.setShape(layer, strokeR: components[0], g: components[1], b: components[2], a: components[3]) }
            else { $0.setShape(layer, fillR: components[0], g: components[1], b: components[2], a: components[3]) }
        }
        model.refreshModel(force: true)
    }
    private func write(_ param: Int, _ amount: Float) {
        guard amount.isFinite, !(model.selectedLayer?.locked ?? false) else { return }
        _ = model.engine.editShape(id, param: UInt32(param), value: amount, continuing: false)
        model.refreshModel(force: true); load()
    }
    private func writeSize(_ amount: Float, from: [Float]) {
        let v = amount.clamped(to: 1...16384)
        if model.shapeSizeLinked {
            let k = from[axis] > 0 ? v / from[axis] : 1
            write(5, axis == 0 ? v : (from[0] * k).clamped(to: 1...16384))
            write(6, axis == 1 ? v : (from[1] * k).clamped(to: 1...16384))
        } else { write(axis == 0 ? 5 : 6, v) }
    }
    private func beginGesture(_ label: String) {
        guard !gestureOpen else { return }; model.beginGesture(label); gestureOpen = true
    }
    private func finishGesture() {
        if gestureOpen { model.endGesture(); gestureOpen = false }; sizeDragging = false
    }
    private func markKey() {
        _ = model.engine.keyShape(id, param: UInt32(selected)); model.refreshModel(force: true); load()
    }
    private func openCurve() {
        guard track.count >= 2 else { return }
        let index = max(0, min(track.count - 2, track.lastIndex { $0.time <= model.localPlayhead } ?? 0))
        model.openCurve(property: 35, effect: track[index].effectIndex, param: UInt32(selected), time: track[index].time)
    }
    private func kitTitle(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted)
            .frame(height: 17.55, alignment: .leading).padding(.top, 8).padding(.bottom, 4)
    }
    private func kitHint(_ text: String) -> some View {
        Text(text).font(.aurea(size: 12))
            .lineSpacing(max(0, 16 - (UIFont(name: "Roboto-Regular", size: 12)?.lineHeight ?? 12)))
            .foregroundStyle(AureaColors.muted).fixedSize(horizontal: false, vertical: true)
    }
}

/// FormaKit.HumanRow: selectable chip, scrolling ruler, numeric entry and reset.
private struct ShapeHumanRow: View {
    @EnvironmentObject private var model: AureaModel
    let label: String
    let value: Float
    let step: Float
    let range: ClosedRange<Float>
    var unit = ""
    let reset: Float
    var selected = false
    var look: KeyframeLook = .none
    var onSelect: (() -> Void)? = nil
    let onStart: () -> Void
    let onValue: (Float) -> Void
    let onEnd: () -> Void
    let onCommit: (Float) -> Void
    @State private var dragging = false
    @State private var live: Float = 0
    private var shown: Float { dragging ? live : value }
    private var resetVisible: Bool { abs(shown - reset) > 0.001 * max(1, abs(reset)) }
    var body: some View {
        HStack(spacing: 0) {
            PropertyLabelChip(label, expression: .none, keyframe: look, onTap: onSelect).aureaSelectedProperty(selected ? label : nil)
            Color.clear.frame(width: 6)
            TickRuler(value: { shown }, unitsPerDp: step, active: selected || onSelect == nil, height: 40, verticalPadding: 8)
                .frame(maxWidth: .infinity).padding(.vertical, 4)
                .valueDrag(enabled: true, start: { value }, unitsPerDp: { step }, min: range.lowerBound, max: range.upperBound,
                           onStart: { live = value; dragging = true; onSelect?(); onStart() }, onValue: { live = $0; onValue($0) },
                           onEnd: { dragging = false; onEnd() })
            Color.clear.frame(width: 6)
            ValueBox(comUnidade(numeroPtBr(shown, casas: 0), unit), onTap: {
                model.numericKeypad = KeypadRequest(title: label, value: value, unit: unit, min: range.lowerBound, max: range.upperBound, decimals: 0) { onCommit($0.clamped(to: range)) }
            })
            Button { onCommit(reset) } label: {
                CupertinoGlyph.text(CupertinoGlyph.ArrowCounterclockwise, size: 16, color: AureaColors.muted).frame(width: 34, height: 44)
            }.buttonStyle(AureaPressStyle(shrink: 1)).opacity(resetVisible ? 1 : 0).disabled(!resetVisible).accessibilityLabel(AureaText.t("panel_voltar_padrao"))
        }.frame(height: 48).onDisappear { if dragging { dragging = false; onEnd() } }
    }
}

/// The original drawShapeGlyph silhouettes, independent of the layer renderer.
private struct ShapeEditGlyph: View {
    let type: Int
    var body: some View {
        Canvas { context, size in
            let r = min(size.width, size.height) / 2, c = CGPoint(x: size.width / 2, y: size.height / 2)
            let box = CGRect(x: c.x - r, y: c.y - r * 0.8, width: 2 * r, height: 1.6 * r)
            var path = Path()
            switch type {
            case 0: path.addRoundedRect(in: box, cornerSize: CGSize(width: r * 0.25, height: r * 0.25))
            case 1: path.addEllipse(in: box)
            case 3, 4:
                let count = type == 3 ? 5 : 10
                for i in 0..<count {
                    let a = -.pi / 2 + CGFloat(i) * .pi * 2 / CGFloat(count), radius = type == 4 && i % 2 == 1 ? r * 0.45 : r
                    let p = CGPoint(x: c.x + radius * cos(a), y: c.y + radius * sin(a))
                    if i == 0 { path.move(to: p) } else { path.addLine(to: p) }
                }; path.closeSubpath()
            case 5:
                let t = r * 0.36
                path.addRect(CGRect(x: c.x - t, y: c.y - r, width: 2 * t, height: 2 * r))
                path.addRect(CGRect(x: c.x - r, y: c.y - t, width: 2 * r, height: 2 * t))
            case 6:
                path.addEllipse(in: CGRect(x: c.x - r * 0.78, y: c.y - r * 0.78, width: r * 1.56, height: r * 1.56))
                context.stroke(path, with: .color(AureaColors.text), lineWidth: r * 0.36); return
            case 7:
                path.move(to: c); path.addLine(to: CGPoint(x: c.x + r, y: c.y))
                path.addArc(center: c, radius: r, startAngle: .degrees(0), endAngle: .degrees(270), clockwise: false); path.closeSubpath()
            case 8:
                for i in 0...72 {
                    let a = CGFloat(i) / 72 * .pi * 2, rr = r * (0.72 + 0.28 * cos(5 * a))
                    let p = CGPoint(x: c.x + rr * cos(a), y: c.y + rr * sin(a))
                    if i == 0 { path.move(to: p) } else { path.addLine(to: p) }
                }; path.closeSubpath()
            case 9:
                let points: [(CGFloat, CGFloat)] = [(-1,-0.22),(0.1,-0.22),(0.1,-0.8),(1,0),(0.1,0.8),(0.1,0.22),(-1,0.22)]
                for (i, p) in points.enumerated() {
                    let point = CGPoint(x: c.x + r * p.0, y: c.y + r * p.1)
                    if i == 0 { path.move(to: point) } else { path.addLine(to: point) }
                }; path.closeSubpath()
            case 10:
                path.move(to: CGPoint(x: c.x - r, y: c.y - r)); path.addLine(to: CGPoint(x: c.x - r, y: c.y + r)); path.addLine(to: CGPoint(x: c.x + r, y: c.y + r)); path.closeSubpath()
            default: path.addRect(CGRect(x: c.x - r, y: c.y - r, width: 2 * r, height: 2 * r))
            }
            context.fill(path, with: .color(AureaColors.text))
        }
    }
}

// MaskPanel.kt: masks, path steps and track matte keep their original order.
struct MaskPanel: View {
    @EnvironmentObject private var model: AureaModel
    @State private var tab = 0
    @State private var step = 0
    @State private var adding = false
    @State private var matte: Int64 = 0
    @State private var mode = 0
    private var id: Int64 { model.primarySelection ?? 0 }
    private var mask: MaskItem? { model.editingMask }
    private var canKey: Bool { tab == 0 && mask?.closed == true && !model.maskDrawing }
    private var look: KeyframeLook { mask?.keyed == true ? .keyHere : (mask?.keyCount ?? 0) > 0 ? .animated : .none }
    private var candidates: [LayerItem] { model.layers.filter { $0.id != id && ![3, 8, 9].contains($0.kind) } }
    private func load() {
        let state = model.engine.trackMatte(id)
        matte = state.first?.int64Value ?? 0; mode = state.count > 1 ? state[1].intValue : 0
    }
    private func write(_ target: Int64, _ value: Int) {
        model.engine.setTrackMatteForLayer(id, matte: target, mode: UInt32(value)); model.refreshModel(force: true); load()
    }
    var body: some View {
        VStack(spacing: 0) {
            PanelHeader(title: AureaText.t("panel_mascara_recorte")) { model.panel = .none }
            HStack(spacing: 0) {
                LeftRail(keyframeLook: look, onKeyframe: canKey ? toggleKey : nil, onBack: { model.panel = .none })
                VStack(spacing: 0) {
                    ParamTabs([AureaText.t("panel_mascaras"), AureaText.t("panel_recorte_outra_camada")], selected: tab) { tab = $0 }
                    ScrollView {
                        VStack(alignment: .leading, spacing: 0) {
                            if tab == 0 { masksBody } else { matteBody }
                        }.padding(.leading, 4).padding(.trailing, 10).padding(.bottom, 16)
                    }
                }
            }
        }.foregroundStyle(AureaColors.text).onAppear { load() }
            .onChange(of: id) { _ in adding = false; step = 0; load() }
            .onChange(of: model.status.modelRevision) { _ in load() }
    }
    @ViewBuilder private var masksBody: some View {
        if model.masks.isEmpty {
            Color.clear.frame(height: 6)
            hint("panel_mascara_mostra_so_parte_camada_escolha")
            Color.clear.frame(height: 8)
            addChoices
        } else {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    ForEach(Array(model.masks.enumerated()), id: \.element.id) { entry in
                        NativePanelChip(AureaText.t(entry.element.closed ? "pn_mask_n" : "pn_mask_n_open", entry.offset + 1), selected: mask?.id == entry.element.id, horizontal: 12, height: 36, radius: 9, fontSize: 12.5) {
                            model.selectedMask = entry.element.id; model.selectedMaskPoint = nil; model.maskDrawing = !entry.element.closed; adding = false
                        }
                    }
                    NativePanelChip(AureaText.t(adding ? "panel_fechar" : "panel_adicionar_mascara"), selected: adding, horizontal: 12, height: 36, radius: 9, fontSize: 12.5) { adding.toggle() }
                }.frame(height: 44)
            }
            if adding { addChoices }
            else if let mask { maskSteps(mask) }
            else { hint("panel_escolha_mascara_acima_editar_palco") }
        }
    }
    private var addChoices: some View {
        VStack(spacing: 6) {
            NativePanelAction("panel_desenhar_mao", detail: AureaText.t("panel_toque_palco_pontos_arraste_curvar")) { model.addMask(2); adding = false; step = 0 }
            NativePanelAction("panel_retangulo", detail: AureaText.t("panel_retangulo_meio_camada_pronto_ajustar")) { model.addMask(0); adding = false; step = 0 }
            NativePanelAction("panel_elipse", detail: AureaText.t("panel_elipse_meio_camada_pronta_ajustar")) { model.addMask(1); adding = false; step = 0 }
        }
    }
    @ViewBuilder private func maskSteps(_ m: MaskItem) -> some View {
        let labels = ["panel_1_caminho", "panel_2_modo", "panel_3_borda"] + (model.selectedLayer?.kind == 1 ? ["panel_4_rastrear"] : [])
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 6) {
                ForEach(Array(labels.enumerated()), id: \.offset) { n, key in
                    NativePanelChip(AureaText.t(key), selected: step == n, horizontal: 14, height: 36, radius: 9, fontSize: 12.5, bold: true) { step = n }
                }
            }.padding(.horizontal, 6).frame(height: 48)
        }
        switch min(step, labels.count - 1) {
        case 0:
            if model.maskDrawing {
                hint("panel_toque_palco_pontos_arraste_curvar_toque")
                Color.clear.frame(height: 8)
                NativePanelAction("panel_fechar_caminho", detail: AureaText.plural("pn_mask_points_so_far", m.points.count / 6)) {
                    if m.points.count >= 18 { model.setMaskPoints(m.points, closed: true); model.maskDrawing = false }
                    else { model.toast = AureaText.t("msg_ponha_pelo_menos_3_pontos") }
                }
            } else {
                hint("panel_arraste_pontos_palco_toque_num_ponto")
                HStack(spacing: 8) {
                    PropertyLabelChip(AureaText.t("panel_caminho"), expression: .none, keyframe: look, onTap: {})
                    Text(m.keyCount == 0 ? AureaText.t("panel_parado_toque_trilho_animar") : AureaText.plural("pn_keyframes_edit_at_playhead", Int(m.keyCount)))
                        .font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).frame(maxWidth: .infinity, alignment: .leading)
                }.frame(height: 48).aureaSelectedProperty(AureaText.t("panel_caminho"))
            }
            Color.clear.frame(height: 10)
            NativePanelAction("panel_apagar_mascara", danger: true) {
                _ = model.engine.removeMask(id, mask: m.id); model.selectedMask = nil; model.selectedMaskPoint = nil; model.maskDrawing = false; model.refreshModel(force: true)
            }
        case 1:
            title("panel_como_esta_mascara_combina_outras")
            ChoiceChips(["panel_somar", "panel_subtrair", "panel_intersecao", "panel_diferenca", "panel_desligada"].map { AureaText.t($0) }, selected: Int(m.operation)) { update(m, operation: UInt32($0)) }
            NativePanelToggle(AureaText.t("panel_inverter_mostrar_lado_fora"), checked: m.inverted) { update(m, inverted: $0) }
        case 2:
            NativePanelRuler(label: AureaText.t("panel_suavizar"), value: m.feather, step: 0.5, range: 0...500, unit: "px", reset: 0, keypad: true) { update(m, feather: $0) }
            NativePanelRuler(label: AureaText.t("panel_expandir"), value: m.expansion, step: 0.5, range: -500...500, unit: "px", reset: 0, keypad: true) { update(m, expansion: $0) }
            NativePanelRuler(label: AureaText.t("panel_opacidade"), value: m.opacity * 100, step: 0.5, range: 0...100, unit: "%", reset: 100, keypad: true) { update(m, opacity: $0 / 100) }
        default:
            if model.importingMedia { hint("panel_rastreando") }
            else {
                hint("panel_mascara_segue_esta_embaixo_dela_cabecote")
                Color.clear.frame(height: 8)
                NativePanelAction("panel_seguir_posicao", detail: AureaText.t("panel_objetos_so_andam_pela_tela")) { track(m.id, mode: 0) }
                Color.clear.frame(height: 6)
                NativePanelAction("panel_seguir_posicao_tamanho_giro", detail: AureaText.t("panel_objetos_aproximam_ou_giram")) { track(m.id, mode: 1) }
            }
        }
    }
    private var matteBody: some View {
        VStack(alignment: .leading, spacing: 0) {
            title("panel_1_recortar_pelo")
            ChoiceChips(["panel_nao_recortar", "panel_pela_forma", "panel_pela_forma_invertido", "panel_pelo_brilho", "panel_pelo_brilho_invertido"].map { AureaText.t($0) }, selected: mode) { value in
                if value == 0 { write(0, 0) }
                else {
                    let own = model.layers.firstIndex { $0.id == id } ?? 0
                    let above = model.layers.dropFirst(own + 1).first { item in candidates.contains { $0.id == item.id } }
                    let target = matte != 0 ? matte : (above?.id ?? 0)
                    if target == 0 { model.toast = AureaText.t("pn_mask_pick_layer_below") } else { write(target, value) }
                }
            }
            title("panel_2_qual_camada_recorta")
            if candidates.isEmpty { hint("panel_nao_ha_outra_camada_possa_recortar") }
            else {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 6) {
                        ForEach(candidates) { layer in
                            NativePanelChip(layer.name, selected: matte == layer.id, horizontal: 12, height: 36, radius: 9, fontSize: 12.5) { write(layer.id, mode == 0 ? 1 : mode) }
                        }
                    }.frame(height: 44)
                }
            }
            Color.clear.frame(height: 4)
            hint(matte != 0 ? "panel_camada_escolhida_recorta_esta_some_tela" : "panel_pela_forma_aparece_onde_outra_camada")
        }
    }
    private func title(_ key: String) -> some View { Text(AureaText.t(key)).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted).padding(.top, 8).padding(.bottom, 4) }
    private func hint(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 12))
            .lineSpacing(max(0, 16 - (UIFont(name: "Roboto-Regular", size: 12)?.lineHeight ?? 12)))
            .foregroundStyle(AureaColors.muted).fixedSize(horizontal: false, vertical: true)
    }
    private func toggleKey() { if let mask { _ = model.engine.toggleMaskKey(id, mask: mask.id); model.refreshModel(force: true) } }
    private func track(_ mask: UInt32, mode: UInt32) { let layer = id; model.performMediaOperation(AureaText.t("panel_rastreando")) { $0.trackMask(layer, mask: mask, mode: mode) } }
    private func update(_ mask: MaskItem, operation: UInt32? = nil, inverted: Bool? = nil, feather: Float? = nil, expansion: Float? = nil, opacity: Float? = nil) {
        _ = model.engine.setMaskProps(id, mask: mask.id, operation: operation ?? mask.operation, inverted: inverted ?? mask.inverted, feather: feather ?? mask.feather, expansion: expansion ?? mask.expansion, opacity: opacity ?? mask.opacity)
        model.refreshModel(force: true)
    }
}


struct CurvePanel: View {
    var body: some View { NativeCurvePanel() }
}

// Preserved route for saved panel state; TextPanel embeds the same section.
struct TextAnimationPanel: View {
    @EnvironmentObject private var model: AureaModel
    var body: some View {
        VStack(spacing: 0) {
            PanelHeader(title: AureaText.t("panel_animacao")) { model.openPanel(.text) }
            ScrollView { TextAnimationSection().padding(.horizontal, 18).padding(.bottom, 24) }
        }
    }
}

struct TextAnimationSection: View {
    @EnvironmentObject private var model: AureaModel
    @State private var animators: [[Float]] = []
    private var id: Int64 { model.primarySelection ?? 0 }
    private let presets = ["Pop", "Pulo", "Deslizar", "Escala", "Surgir", "Desfoque", "Destaque palavra", "Karaokê", "Máquina de escrever", "Onda", "Elástico"]
    private func load() {
        let flat = model.engine.textAnimators(id).map(\.floatValue)
        animators = stride(from: 0, to: flat.count - flat.count % 40, by: 40).map { Array(flat[$0..<($0 + 40)]) }
    }
    private func refresh() { model.refreshModel(force: true); load() }
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text(AureaText.t("panel_animacao")).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted).padding(.top, 10)
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    ForEach(Array(presets.enumerated()), id: \.offset) { n, title in
                        NativePanelChip(title) { model.engine.applyTextPreset(id, preset: UInt32(n)); refresh() }
                    }
                }.frame(height: 44)
            }
            ForEach(animators.indices, id: \.self) { index in NativeTextAnimatorCard(index: UInt32(index), values: animators[index]).padding(.top, 8) }
            NativePanelChip(AureaText.t("panel_adicionar_animacao")) { model.engine.addTextAnimator(id, props: 8); refresh() }.padding(.top, 4)
        }.foregroundStyle(AureaColors.text).onAppear { load() }.onChange(of: id) { _ in load() }
            .onChange(of: model.status.modelRevision) { _ in load() }.onChange(of: model.status.playhead) { _ in load() }
    }
}

private struct NativeAnimParam: Identifiable {
    var id: Int
    var slot: Int
    var bit: Int
    var label: String
    var unit: String
    var step: Float
    var range: ClosedRange<Float>
    static let selectors = [
        NativeAnimParam(id: 0, slot: 7, bit: 0, label: "Início", unit: "%", step: 0.5, range: 0...100),
        NativeAnimParam(id: 1, slot: 8, bit: 0, label: "Fim", unit: "%", step: 0.5, range: 0...100),
        NativeAnimParam(id: 2, slot: 9, bit: 0, label: "Atraso entre elas", unit: "%", step: 0.5, range: -1000...1000),
        NativeAnimParam(id: 3, slot: 10, bit: 0, label: "Intensidade", unit: "%", step: 0.5, range: -100...100)
    ]
    static let properties = [
        NativeAnimParam(id: 10, slot: 14, bit: 0, label: "Posição X", unit: "px", step: 1, range: -5000...5000),
        NativeAnimParam(id: 11, slot: 15, bit: 0, label: "Posição Y", unit: "px", step: 1, range: -5000...5000),
        NativeAnimParam(id: 12, slot: 16, bit: 0, label: "Profundidade", unit: "px", step: 1, range: -5000...5000),
        NativeAnimParam(id: 13, slot: 17, bit: 1, label: "Escala X", unit: "%", step: 1, range: -2000...2000),
        NativeAnimParam(id: 14, slot: 18, bit: 1, label: "Escala Y", unit: "%", step: 1, range: -2000...2000),
        NativeAnimParam(id: 15, slot: 19, bit: 2, label: "Rotação X", unit: "°", step: 1, range: -3600...3600),
        NativeAnimParam(id: 16, slot: 20, bit: 2, label: "Rotação Y", unit: "°", step: 1, range: -3600...3600),
        NativeAnimParam(id: 17, slot: 21, bit: 2, label: "Rotação Z", unit: "°", step: 1, range: -3600...3600),
        NativeAnimParam(id: 18, slot: 22, bit: 3, label: "Opacidade", unit: "%", step: 0.5, range: 0...100),
        NativeAnimParam(id: 19, slot: 23, bit: 4, label: "Espaçamento", unit: "px", step: 0.5, range: -500...500),
        NativeAnimParam(id: 20, slot: 24, bit: 5, label: "Desfoque", unit: "px", step: 0.2, range: 0...200),
        NativeAnimParam(id: 21, slot: 25, bit: 6, label: "Inclinação", unit: "°", step: 0.5, range: -80...80),
        NativeAnimParam(id: 22, slot: 26, bit: 7, label: "Contorno", unit: "px", step: 0.1, range: -50...50),
        NativeAnimParam(id: 23, slot: 27, bit: 8, label: "Embaralhar letra", unit: "", step: 0.1, range: -1000...1000)
    ]
}

private struct NativeTextAnimatorCard: View {
    @EnvironmentObject private var model: AureaModel
    let index: UInt32
    let values: [Float]
    private var id: Int64 { model.primarySelection ?? 0 }
    private let names = ["Posição", "Escala", "Rotação", "Opacidade", "Espaçamento", "Desfoque", "Inclinação", "Contorno", "Embaralhar letra", "Cor", "Cor do contorno"]
    private func set(_ updates: [Int: Float]) {
        let flat = model.engine.textAnimators(id).map(\.floatValue), start = Int(index) * 40
        guard flat.count >= start + 40 else { return }
        var next = Array(flat[start..<(start + 40)])
        for (slot, value) in updates { next[slot] = value }
        _ = model.engine.setTextAnimator(id, index: index, values: next.map { NSNumber(value: $0) }); model.refreshModel(force: true)
    }
    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 8) {
                Text("Animação \(index + 1)").font(.aurea(size: 13, weight: .bold)).frame(maxWidth: .infinity, alignment: .leading)
                NativePanelChip(AureaText.t("panel_remover")) { model.engine.removeTextAnimator(id, index: index); model.refreshModel(force: true) }
                AureaToggle(checked: values[0] > 0.5) { set([0: $0 ? 1 : 0]) }
            }.frame(height: 40)
            choices("panel_anima_cada", ["panel_letra", "panel_palavra", "panel_linha"], slot: 2)
            choices("panel_escolhe", ["panel_ordem", "panel_sorteado"], slot: 3)
            selectorControls
            ForEach(NativeAnimParam.properties.filter { Int(values[1]) & (1 << $0.bit) != 0 }) { param in NativeTextAnimRuler(index: index, param: param, values: values) }
            ForEach([9, 10], id: \.self) { bit in if Int(values[1]) & (1 << bit) != 0 { colorRow(bit) } }
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    ForEach(Array(names.enumerated()), id: \.offset) { bit, name in
                        NativePanelChip(name, selected: Int(values[1]) & (1 << bit) != 0) { set([1: Float(Int(values[1]) ^ (1 << bit))]) }
                    }
                }.frame(height: 44)
            }
        }.padding(8).background(AureaColors.chip.opacity(0.45), in: RoundedRectangle(cornerRadius: 10))
    }
    @ViewBuilder private var selectorControls: some View {
        if values[3] < 0.5 {
            choices("panel_passagem", ["panel_seco", "panel_sobe", "panel_desce", "panel_triangulo", "panel_redondo", "panel_suave"], slot: 4)
            HStack {
                Text(AureaText.t("panel_ordem_aleatoria")).font(.aurea(size: 12)).frame(maxWidth: .infinity, alignment: .leading)
                AureaToggle(checked: values[5] > 0.5) { set([5: $0 ? 1 : 0]) }
            }.frame(height: 40)
            ForEach(NativeAnimParam.selectors) { param in NativeTextAnimRuler(index: index, param: param, values: values) }
        } else {
            NativeTextAnimRuler(index: index, param: NativeAnimParam(id: 25, slot: 13, bit: 0, label: AureaText.t("panel_trocas_segundo"), unit: "", step: 0.05, range: 0...60), values: values)
            NativeTextAnimRuler(index: index, param: NativeAnimParam.selectors[3], values: values)
        }
    }
    private func choices(_ label: String, _ keys: [String], slot: Int) -> some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 6) {
                Text(AureaText.t(label)).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                ForEach(Array(keys.enumerated()), id: \.offset) { n, key in NativePanelChip(AureaText.t(key), selected: Int(values[slot]) == n) { set([slot: Float(n)]) } }
            }.frame(height: 40)
        }
    }
    private func colorRow(_ bit: Int) -> some View {
        let base = bit == 9 ? 28 : 32
        return HStack {
            Text(names[bit]).font(.aurea(size: 12)); Spacer()
            NativePanelColorWell(values: [values[base], values[base + 1], values[base + 2], 1]) {
                model.beginGesture(names[bit])
                model.colorSheet = ColorSheetRequest(title: names[bit], initial: [values[base], values[base + 1], values[base + 2], 1], onChange: { r, g, b, _ in set([base: r, base + 1: g, base + 2: b]) }, onDone: { model.endGesture() })
            }
        }.frame(height: 44)
    }
}

private struct NativeTextAnimRuler: View {
    @EnvironmentObject private var model: AureaModel
    let index: UInt32
    let param: NativeAnimParam
    let values: [Float]
    private var id: Int64 { model.primarySelection ?? 0 }
    private var look: KeyframeLook {
        let bit = 1 << (param.id < 10 ? param.id : param.id - 10)
        if Int(values[param.id < 10 ? 38 : 39]) & bit != 0 { return .keyHere }
        return Int(values[param.id < 10 ? 36 : 37]) & bit != 0 ? .animated : .none
    }
    private var expression: ExpressionLook {
        let info = model.engine.expression(id, property: 33, effect: index, param: UInt32(param.id))
        guard (info["exists"] as? NSNumber)?.boolValue == true else { return .none }
        guard (info["enabled"] as? NSNumber)?.boolValue ?? true else { return .off }
        return (info["error"] as? String ?? "").isEmpty ? .ok : .error
    }
    var body: some View {
        NativePanelRuler(label: param.label, value: values[param.slot], step: param.step, range: param.range, unit: param.unit, decimals: param.step < 0.5 ? 1 : 0,
            look: look, toggleKey: {
                model.engine.toggleTextAnimKey(id, index: index, param: UInt32(param.id)); model.refreshModel(force: true)
            }, expression: expression, onExpression: {
                model.expressionSheet = ExpressionRequest(layer: id, label: param.label, tracks: [ExpressionTrack(property: 33, effect: index, param: UInt32(param.id))], unit: param.unit)
            }, compactUnit: true) { model.engine.setTextAnimParam(id, index: index, param: UInt32(param.id), value: $0); model.refreshModel(force: true) }
    }
}
