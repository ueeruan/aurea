import SwiftUI

var extraTextPresetNames: [String] { [AureaText.t("ios_textpreset_x_bounce"), AureaText.t("ios_textpreset_x_soft_in"), AureaText.t("ios_textpreset_x_reveal"), AureaText.t("ios_textpreset_x_slide"),
                          AureaText.t("ios_textpreset_x_fast_in"), AureaText.t("ios_textpreset_x_elastic_jump"), AureaText.t("ios_textpreset_x_word_jump"), AureaText.t("ios_textpreset_x_smooth_motion")] + ["pack_text_0", "pack_text_1", "pack_text_2", "pack_text_3", "pack_text_4", "pack_text_5", "app_text_preset_explosion"].map { AureaText.t($0) }
}
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
        guard style.count >= 20 else { return }; var next = style; next[slot] = value
        _ = model.engine.setTextStyle(id, values: next.map { NSNumber(value: $0) }); refresh()
    }
    private func components(_ key: String) -> [Float] { let v = (text[key] as? [NSNumber] ?? []).map(\.floatValue); return v.count >= 4 ? v : [1, 1, 1, 1] }
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            NativePanelRuler(label: AureaText.t("panel_tamanho"), value: value("size", 72), step: 0.5, range: 4...1000, unit: "px", keypad: true) { model.engine.setText(id, size: $0); refresh() }
            colorRow("panel_cor", values: components("color")) { color("color", "panel_cor") }
            HStack(spacing: 6) {
                Text(AureaText.t("panel_alinhamento")).font(.aurea(size: 13)).frame(maxWidth: .infinity, alignment: .leading)
                ForEach(Array(["panel_esquerda", "panel_centro", "panel_direita"].enumerated()), id: \.offset) { n, key in
                    NativePanelChip(AureaText.t(key), selected: Int(value("alignment")) == n, horizontal: 12) { model.engine.setText(id, alignment: UInt32(n)); refresh() }
                }
            }.frame(height: 48)
            section("panel_contorno")
            colorRow("panel_cor_contorno", values: components("strokeColor")) { color("strokeColor", "panel_cor_contorno") }
            NativePanelRuler(label: AureaText.t("panel_largura_contorno"), value: value("strokeWidth"), step: 0.1, range: 0...60, unit: "px", keypad: true) { model.engine.setText(id, strokeWidth: $0); refresh() }
            if style.count >= 20 { styleSections }
            NativeTextPathSection()
            NativePanelChip(AureaText.t("la_animators")) { model.transformTab = 6; model.openPanel(.transform) }
        }.foregroundStyle(AureaColors.text).onAppear { load() }.onChange(of: id) { _ in load() }
            .onChange(of: model.status.modelRevision) { _ in load() }
    }
    private var styleSections: some View {
        VStack(alignment: .leading, spacing: 0) {
            NativePanelRuler(label: AureaText.t("text_line_spacing"), value: style[18] * 100, step: 0.5, range: 10...1000, unit: "%", keypad: true) { set(18, $0 / 100) }
            trackingRow
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
    private var trackingRow: some View {
        let keys = (model.keyframes[id] ?? []).filter { $0.property == 29 && $0.effectIndex == UInt32.max }
        let here = keys.contains { $0.time == model.localPlayhead }
        return NativePanelRuler(label: AureaText.t("text_letter_spacing"), value: style[19], step: 0.25,
            range: -1000...1000, unit: "‰ em", keypad: true, look: here ? .keyHere : keys.isEmpty ? .none : .animated, toggleKey: {
                guard !(model.selectedLayer?.locked ?? false) else { return }
                model.mutate { engine in
                    if here { engine.deleteKeyframe(forLayer: id, property: 29, time: model.localPlayhead) }
                    else { engine.insertKeyframe(forLayer: id, property: 29, time: model.localPlayhead, value: style[19]) }
                }
                refresh()
            }) { set(19, $0) }.accessibilityIdentifier("text.tracking.key")
    }
    private func styleRow(_ key: String, _ slot: Int, _ step: Float, _ range: ClosedRange<Float>) -> some View {
        NativePanelRuler(label: AureaText.t(key), value: style[slot], step: step, range: range, unit: "px", keypad: true) { set(slot, $0) }
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
                        guard current.count >= 20 else { return }
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

/// TextPathSection.kt (opções completas do 0.2.7, as Path Options do After Effects): o caminho
/// vem de uma camada vetorial OU de uma máscara da própria camada (que, usada como caminho, não
/// recorta o texto); margens inicial e final animáveis, forçar alinhamento, perpendicular e sentido.
private struct NativeTextPathSection: View {
    @EnvironmentObject private var model: AureaModel
    @State private var target: Int64 = 0
    @State private var mask: Int32 = -1
    @State private var firstMargin: Float = 0
    @State private var lastMargin: Float = 0
    @State private var perpendicular = true
    @State private var reversed = false
    @State private var forceAlign = false
    @State private var ownMasks: [UInt32] = []
    private var id: Int64 { model.primarySelection ?? 0 }
    private var onPath: Bool { target != 0 || mask >= 0 }
    private var candidates: [LayerItem] { model.layers.filter { $0.id != id && $0.kind == 5 && ((model.engine.layerDetail($0.id)?["shapeTypePoints"] as? NSNumber)?.uint32Value ?? 0) & 0xFFFF == 11 } }
    /// Máscaras da camada de texto (cabeçalho de 12 valores por máscara em `maskData`, como `refreshMasks`).
    private func loadMasks() {
        let data = model.engine.maskData(id).map(\.floatValue)
        var ids: [UInt32] = []
        if data.count >= 7 {
            var offset = 7
            for _ in 0..<Int(data[6]) {
                guard offset + 12 <= data.count else { break }
                let count = Int(data[offset + 7]) * 6
                ids.append(UInt32(data[offset]))
                offset += 12 + max(0, count)
            }
        }
        ownMasks = ids
    }
    private func load() {
        loadMasks()
        let v = model.engine.textPathOptions(id)
        guard v.count >= 7 else { return }
        target = v[0].int64Value; firstMargin = v[1].floatValue; perpendicular = v[2].boolValue; reversed = v[3].boolValue
        mask = v[4].int32Value; lastMargin = v[5].floatValue; forceAlign = v[6].boolValue
    }
    private func write() {
        _ = model.engine.setTextPathOptions(id, target: mask >= 0 ? 0 : target, mask: mask, firstMargin: firstMargin, lastMargin: lastMargin,
                                            perpendicular: perpendicular, reversed: reversed, forceAlign: forceAlign)
        model.refreshModel(force: true); load()
    }
    /// Margens animáveis (TrackProperty.TEXT_PATH_FIRST_MARGIN = 46, LAST = 47), o losango do Android.
    private func marginRow(_ key: String, property: UInt32, value: Float, identifier: String, set: @escaping (Float) -> Void) -> some View {
        let keys = (model.keyframes[id] ?? []).filter { $0.property == property && $0.effectIndex == UInt32.max }
        let here = keys.contains { $0.time == model.localPlayhead }
        return NativePanelRuler(label: AureaText.t(key), value: value, step: 1, range: -20000...20000, unit: "px", keypad: true,
            look: here ? .keyHere : keys.isEmpty ? .none : .animated, toggleKey: {
                guard !(model.selectedLayer?.locked ?? false) else { return }
                let layer = id
                model.mutate { engine in
                    if here { engine.deleteKeyframe(forLayer: layer, property: property, time: model.localPlayhead) }
                    else { engine.insertKeyframe(forLayer: layer, property: property, time: model.localPlayhead, value: value) }
                }
                model.refreshModel(force: true); load()
            }, identifier: identifier, set: set)
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text(AureaText.t("panel_texto_caminho")).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted).padding(.top, 6)
            if candidates.isEmpty && ownMasks.isEmpty && !onPath {
                Text(AureaText.t("text027_path_hint")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).padding(.vertical, 8)
            } else {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 6) {
                        NativePanelChip(AureaText.t("panel_linha_reta"), selected: !onPath) { target = 0; mask = -1; write() }
                            .accessibilityIdentifier("text.path.none")
                        ForEach(Array(ownMasks.enumerated()), id: \.offset) { n, m in
                            NativePanelChip(AureaText.t("text027_path_mask", n + 1), selected: mask == Int32(m)) { target = 0; mask = Int32(m); write() }
                                .accessibilityIdentifier("text.path.mask.\(n)")
                        }
                        ForEach(candidates) { layer in
                            NativePanelChip(layer.name.isEmpty ? AureaText.t("panel_vetor") : layer.name, selected: mask < 0 && target == layer.id) { target = layer.id; mask = -1; write() }
                                .accessibilityIdentifier("text.path.layer.\(layer.id)")
                        }
                    }.frame(height: 44)
                }
                if onPath {
                    marginRow("panel_margem_inicial", property: 46, value: firstMargin, identifier: "text.path.first.key") { firstMargin = $0; write() }
                    marginRow("text027_path_last_margin", property: 47, value: lastMargin, identifier: "text.path.last.key") { lastMargin = $0; write() }
                    NativePanelToggle(AureaText.t("text027_path_force_align"), checked: forceAlign, fontSize: 13, weight: .regular) { forceAlign = $0; write() }
                        .accessibilityIdentifier("text.path.force")
                    NativePanelToggle(AureaText.t("panel_perpendicular_caminho"), checked: perpendicular, fontSize: 13, weight: .regular) { perpendicular = $0; write() }
                        .accessibilityIdentifier("text.path.perpendicular")
                    NativePanelToggle(AureaText.t("panel_inverter_sentido"), checked: reversed, fontSize: 13, weight: .regular) { reversed = $0; write() }
                        .accessibilityIdentifier("text.path.reverse")
                    Text(AureaText.t("text027_path_alignment_note")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).padding(.vertical, 6)
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
struct NativePanelRuler: View {
    @EnvironmentObject private var model: AureaModel
    @Environment(\.isEnabled) private var enabled
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
    var selected = false
    var onSelect: (() -> Void)? = nil
    var compactUnit = false
    /// Shared property metadata distinguishes the finger range from precise typing.
    var typedRange: ClosedRange<Float>? = nil
    var identifier = ""
    var controlHeight: CGFloat = 40
    var accessibilityName: String? = nil
    /// Panels presented as native sheets host their own precise input above that sheet.
    var presentKeypad: ((KeypadRequest) -> Void)? = nil
    let set: (Float) -> Void
    @State private var dragging = false
    @State private var live: Float = 0
    private var shown: Float { dragging ? live : value }
    var body: some View {
        HStack(spacing: 0) {
            if let plainLabelWidth { Text(label).font(.aurea(size: 13)).frame(width: plainLabelWidth, alignment: .leading) }
            else {
                PropertyLabelChip(label, expression: expression, keyframe: look, onTap: { onSelect?() })
                    .transformEnvironment(\.aureaSelectedProperty) { if onSelect != nil { $0 = selected ? label : nil } }
                    .onLongPressGesture(minimumDuration: 0.5) { if reset == nil { onExpression?() } }
                    .contextMenu {
                        if let reset {
                            Button(AureaText.t("panel_voltar_padrao")) { set(reset) }
                                .disabled(abs(shown - reset) <= 0.001 * max(1, abs(reset)))
                            if let onExpression { Button(AureaText.t("panel_expressao"), action: onExpression) }
                        }
                    }
            }
            Color.clear.frame(width: plainLabelWidth != nil ? 0 : keypad ? 6 : 8)
            TickRuler(value: { shown }, unitsPerDp: step, active: enabled && (selected || dragging), height: ParamRowDims.labelH).frame(maxWidth: .infinity).frame(minWidth: 30, minHeight: ParamRowDims.labelH)
                .valueDrag(enabled: enabled, start: { value }, unitsPerDp: { step }, min: range.lowerBound, max: range.upperBound,
                           onStart: { live = value; dragging = true; onSelect?(); model.beginGesture(label) }, onValue: { live = $0; set($0) }, onEnd: finish)
                .accessibilityElement()
                .accessibilityLabel(accessibilityName ?? label)
                .accessibilityValue(comUnidade(numeroPtBr(shown, casas: decimals), unit))
                .accessibilityAdjustableAction { direction in
                    let delta = step * 10 * (direction == .increment ? 1 : direction == .decrement ? -1 : 0)
                    guard enabled, delta != 0 else { return }
                    onSelect?(); model.beginGesture(label); set((value + delta).clamped(to: range)); model.endGesture()
                }
                .accessibilityIdentifier(identifier)
            Color.clear.frame(width: keypad ? 6 : 8)
            ParamValueBox(compactUnit ? numeroPtBr(shown, casas: decimals) + unit : comUnidade(numeroPtBr(shown, casas: decimals), unit), enabled: enabled, onTap: keypad ? openKeypad : nil)
                .accessibilityLabel(accessibilityName ?? label)
                .accessibilityValue(comUnidade(numeroPtBr(shown, casas: decimals), unit))
                .accessibilityIdentifier(identifier.isEmpty ? "" : identifier + ".value")
            if let toggleKey {
                Color.clear.frame(width: 4)
                Button(action: toggleKey) { KeyframeDiamondIcon(look: look, enabled: enabled).frame(width: max(44, controlHeight), height: ParamRowDims.labelH) }.buttonStyle(.plain)
                    .accessibilityLabel(AureaText.t(look == .keyHere ? "panel_tirar_keyframe_daqui" : "panel_marcar_keyframe_aqui") + " · " + label)
            }
        }.frame(height: ParamRowDims.row).onDisappear(perform: finish)
    }
    private func finish() { if dragging { dragging = false; model.endGesture() } }
    private func openKeypad() {
        guard enabled, !dragging else { return }
        onSelect?()
        let limits = typedRange ?? range
        let project = model.projectGeneration, layer = model.primarySelection
        let composition = (model.composition[AureaCompositionId] as? NSNumber)?.uint64Value
        let request = KeypadRequest(title: label, value: value, unit: unit, min: limits.lowerBound, max: limits.upperBound, decimals: decimals, percentBase: range.upperBound) {
            guard model.projectGeneration == project, model.primarySelection == layer,
                  (model.composition[AureaCompositionId] as? NSNumber)?.uint64Value == composition else { return }
            model.beginGesture(label); set($0.clamped(to: limits)); model.endGesture()
        }
        if let presentKeypad { presentKeypad(request) } else { model.numericKeypad = request }
    }
}


// PresetsPanel.kt / PresetLibrary.kt. JSON belongs to the shared engine.
/// Nome traduzido dos presets embutidos (Resources/presets/<tipo>.json), na
/// ordem do arquivo — mesma tabela do Android (`BuiltinPresetNames`). Os de
/// marca ("CC · Detail", "Omino · Diffusion") ficam com o nome do arquivo.
let builtinPresetNameKeys: [String: [String]] = [
    "animacao": ["app_preset_anim_appear", "app_preset_anim_vanish", "app_preset_anim_enter_left", "app_preset_anim_rise",
                 "app_preset_anim_zoom_in", "app_preset_anim_pulse", "app_preset_anim_spin_in"],
    "curva": ["app_preset_curve_cubic", "app_preset_curve_strong_inout", "app_preset_curve_expo_out", "app_preset_curve_expo_in",
              "app_preset_curve_back_out", "app_preset_curve_back_in"],
    // "" = fica o nome do arquivo (os de marca: "CC · Detail", "Omino · Diffusion").
    "efeitos": ["app_preset_fx_soft_blur", "app_preset_fx_focus_in", "app_preset_fx_neon_glow", "app_preset_fx_bw",
                "app_preset_fx_strong_contrast", "app_preset_fx_sepia", "app_preset_fx_dream", "app_preset_fx_turbulence",
                "", "", "", "", "app_preset_fx_luma_in", "app_preset_fx_luma_out"],
    "legenda": ["app_preset_caption_viral", "app_preset_caption_karaoke", "app_preset_caption_subtle", "pack_text_4", "pack_text_5"],
]

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

struct PanelPresetEntry: Identifiable {
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

extension PanelPresetEntry {
    static func loadAll() -> [PanelPresetEntry] {
        var result: [PanelPresetEntry] = []
        for kind in PanelPresetKind.allCases {
            if kind == .text {
                let keys = ["pn_pop", "pn_textpreset_bounce", "pn_textpreset_slide", "panel_escala", "pn_textpreset_appear",
                            "panel_desfoque", "pn_textpreset_word_highlight", "pn_karaoke", "pn_textpreset_typewriter", "pn_textpreset_wave", "pn_textpreset_elastic"]
                for (index, key) in keys.enumerated() {
                    result.append(PanelPresetEntry(id: "text:\(index)", name: AureaText.t(key), kind: kind, textPreset: UInt32(index)))
                }
                for (index, name) in extraTextPresetNames.enumerated() {
                    result.append(PanelPresetEntry(id: "text:\(index + 11)", name: name, kind: kind, textPreset: UInt32(index + 11)))
                }
            } else if let url = Bundle.main.url(forResource: kind.rawValue, withExtension: "json", subdirectory: "presets"),
                      let data = try? Data(contentsOf: url), let objects = (try? JSONSerialization.jsonObject(with: data)) as? [[String: Any]] {
                for (index, object) in objects.enumerated() {
                    if let bytes = AureaJSONData(object, false), let json = String(data: bytes, encoding: .utf8) {
                        // Nome no idioma do app (o JSON fala pt-BR); sem recurso, o do arquivo.
                        let keys = builtinPresetNameKeys[kind.rawValue] ?? []
                        let name = index < keys.count && !keys[index].isEmpty ? AureaText.t(keys[index]) : object["name"] as? String ?? ""
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
        return result
    }
}

/// PRESETS DE TEXTO na barra de adicionar (par do `TextPresetPicker` do
/// Android): ao lado do "Texto", que segue criando o texto direto. Escolher 2D
/// ou 3D e tocar num cartão cria o texto já com o preset (motor:
/// `applyTextPreset` nos nativos, `applyPreset` nos salvos/baixados da
/// comunidade) e toca a prévia, sem abrir o teclado.
struct TextPresetPicker: View {
    @EnvironmentObject private var model: AureaModel
    var onAdded: () -> Void
    @State private var threeD = false
    @State private var presets: [PanelPresetEntry] = []
    @State private var favorites = Set<String>()

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(spacing: 6) {
                chip("sh_add_tab_text", on: !threeD) { threeD = false }
                chip("sh_add_text_3d", on: threeD) { threeD = true }
            }.frame(height: 46)
            Text(AureaText.t("tp_hint")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).padding(.bottom, 8)
            ScrollView {
                LazyVGrid(columns: [GridItem(.adaptive(minimum: 100), spacing: 8)], spacing: 8) {
                    ForEach(presets) { entry in
                        PresetCardView(entry: entry, showKind: false, favorite: favorites.contains(entry.id),
                                       compositionWidth: Float(max(1, model.compositionWidth)),
                                       onApply: { add(entry) }, onFavorite: { toggleFavorite(entry) }, onDelete: nil,
                                       applyLabel: AureaText.t("tp_add_named", entry.name))
                    }
                }.padding(.bottom, 12)
            }.accessibilityIdentifier("textPresets.grid")
        }
        .padding(.horizontal, 12)
        .foregroundStyle(AureaColors.text)
        .onAppear(perform: load)
        .onChange(of: model.language) { _ in load() }
    }

    private func chip(_ key: String, on: Bool, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(AureaText.t(key)).font(.aurea(size: 12.5, weight: on ? .bold : .medium))
                .foregroundStyle(on ? AureaColors.accent : AureaColors.text)
                .padding(.horizontal, 12).frame(height: 34)
                .background(on ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 9))
        }.buttonStyle(AureaPressStyle()).accessibilityAddTraits(on ? .isSelected : [])
    }
    private func load() {
        favorites = Set(UserDefaults.standard.stringArray(forKey: "presetFavorites") ?? [])
        presets = PanelPresetEntry.loadAll().filter { $0.kind == .text }
    }
    private func toggleFavorite(_ entry: PanelPresetEntry) {
        if favorites.contains(entry.id) { favorites.remove(entry.id) } else { favorites.insert(entry.id) }
        UserDefaults.standard.set(Array(favorites), forKey: "presetFavorites")
    }
    private func add(_ entry: PanelPresetEntry) {
        if model.status.playing != 0 { model.playPause() }
        let id = threeD ? model.addText3D(content: AureaText.t("panel_texto"), depth: 0.25, openEditor: false) : model.addText(openEditor: false)
        guard id >= 0 else { return }
        if let native = entry.textPreset {
            guard model.engine.applyTextPreset(id, preset: native) else {
                model.toast = AureaText.t("msg_animacao_de_texto_so_vale_para"); model.refreshModel(force: true); onAdded(); return
            }
        } else {
            guard let source = entry.source else { model.toast = AureaText.t("msg_arquivo_do_preset_nao_encontrado"); onAdded(); return }
            let error = model.engine.applyPreset(id, json: source, duration: 0)
            if !error.isEmpty { model.toast = AureaText.t("msg_preset_nao_aplicado", AureaEngineText.reason(error)); model.refreshModel(force: true); onAdded(); return }
        }
        var recents = UserDefaults.standard.stringArray(forKey: "presetRecents") ?? []
        recents.removeAll { $0 == entry.id }; recents.insert(entry.id, at: 0)
        UserDefaults.standard.set(Array(recents.prefix(10)), forKey: "presetRecents")
        model.refreshModel(force: true)
        model.toast = AureaText.t("msg_aplicado", entry.name)
        onAdded()
        model.playPause()
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
    /// Texto 2D/3D: o painel é o de PRESETS DE TEXTO (ficha "Presets" da doca do
    /// texto) — só a aba de texto e os favoritos/recentes de texto. Os presets de
    /// vídeo/imagem saíram da interface (2026-10-02) e continuam fora.
    private var isText: Bool { model.selectedLayer?.kind == 4 || model.engine.text3D(forLayer: layerId) != nil }
    private var tabs: [String] {
        if isText { return ["texto", "favoritos", "recentes"] }
        return (["favoritos", "recentes", "animacao", "efeitos", "texto", "legenda", "curva"])
            .filter { $0 != "texto" || isText }
            .filter { $0 != "legenda" || [1, 3, 4].contains(model.selectedLayer?.kind ?? 0) }
    }
    private var tab: String { tabs.contains(picked) ? picked : (tabs.first ?? "animacao") }
    private var kind: PanelPresetKind? { PanelPresetKind(rawValue: tab) }
    private var query: String { search.trimmingCharacters(in: .whitespacesAndNewlines) }
    private var entries: [PanelPresetEntry] {
        let values: [PanelPresetEntry]
        if tab == "favoritos" { values = presets.filter { favorites.contains($0.id) } }
        else if tab == "recentes" { values = recents.compactMap { id in presets.first { $0.id == id } } }
        else { values = presets.filter { $0.kind == kind } }
        return values.filter { (!isText || $0.kind == .text) && (query.isEmpty || $0.name.range(of: query, options: .caseInsensitive) != nil) }
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
                if kind == .effects {
                    Text(AureaText.t("cc_preset_hint")).font(.aurea(size: 12))
                        .foregroundStyle(AureaColors.muted).frame(maxWidth: .infinity, alignment: .leading)
                        .padding(.bottom, 8)
                }
                if kind == .animation {
                    HStack(spacing: 0) {
                        Text(AureaText.t("panel_durar_ate_fim_camada")).font(.aurea(size: 13))
                            .frame(maxWidth: .infinity, alignment: .leading)
                        AureaToggle(checked: stretch) { stretch = $0 }
                    }.frame(height: 40)
                }
                if entries.isEmpty && kind == nil {
                    Text(query.isEmpty ? AureaText.t(tab == "favoritos" ? "panel_toque_preset_guardar_aqui" : "panel_ultimos_10_presets_aplicados_aparecem_aqui") : AureaText.t("ios_preset_none_matching", query))
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
                                }.buttonStyle(AureaPressStyle()).accessibilityLabel(AureaText.t("panel_salvar_camada_como_preset"))
                            }
                        }.padding(.bottom, 12)
                    }
                }
            }.padding(.horizontal, 12)
        }
        .foregroundStyle(AureaColors.text).background(AureaColors.editorPanel)
        .onAppear {
            if model.selectedLayer?.kind == 4 || model.engine.text3D(forLayer: layerId) != nil { picked = "texto" }
            // "Meus presets" (vindo dos efeitos) abre direto na aba pedida.
            if let kind = model.presetsOpenKind { picked = kind; model.presetsOpenKind = nil }
            if let query = model.presetsOpenSearch { search = query; model.presetsOpenSearch = nil }
            load()
        }
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
                }.buttonStyle(AureaPressStyle()).accessibilityLabel(AureaText.t("panel_limpar_busca"))
            }
        }.padding(.horizontal, 10).frame(height: 38)
            .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
    }
    private func tabLabel(_ value: String) -> String {
        // PresetTab labels are literals in the Android source.
        if value == "favoritos" { return "★ " + AureaText.t("edt_cmd_favorites") }
        if value == "recentes" { return AureaText.t("effect_recentes") }
        return PanelPresetKind(rawValue: value)?.label ?? value
    }
    private func load() {
        favorites = Set(UserDefaults.standard.stringArray(forKey: "presetFavorites") ?? [])
        recents = UserDefaults.standard.stringArray(forKey: "presetRecents") ?? []
        presets = PanelPresetEntry.loadAll()
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
            guard model.curveSelectedTime != nil else { model.toast = AureaText.t("ios_curve_tap_keyframe"); return }
            guard let source = entry.source, let ease = presetCurve(model.engine.parseCurvePreset(source)) else { model.toast = AureaText.t("ios_curve_invalid_preset"); return }
            guard let key = curveKey else { model.toast = AureaText.t("ios_curve_need_two_keys"); return }
            model.beginGesture("preset de curva")
            for sibling in model.keyframes[layerId] ?? [] where sibling.time == key.time && curveSameTrack(sibling, key) {
                model.engine.editTrackKey(layerId, property: sibling.property, effect: sibling.effectIndex, param: sibling.paramIndex,
                                          time: sibling.time, action: 3, value: sibling.value, targetTime: sibling.time,
                                          interpolation: ease.interpolation, handles: [ease.x1, ease.y1, ease.x2, ease.y2, Float(ease.power), 1].map { NSNumber(value: $0) })
            }
            model.endGesture(); markUsed(entry)
            model.toast = AureaText.t("ios_curve_applied", entry.name); return
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
                if !error.isEmpty { model.toast = AureaEngineText.sentence(error); return }
                model.refreshModel(force: true); model.toast = AureaText.t("msg_legendas_refeitas_com", entry.name)
            } else { model.toast = AureaText.t("msg_estilo_de_legenda_escolhido", entry.name) }
            markUsed(entry); return
        }
        guard let layer = model.selectedLayer else { return }
        if let native = entry.textPreset {
            guard model.engine.applyTextPreset(layerId, preset: native) else {
                model.toast = AureaText.t("msg_animacao_de_texto_so_vale_para"); return
            }
        } else {
            guard let source = entry.source else { model.toast = AureaText.t("msg_arquivo_do_preset_nao_encontrado"); return }
            var duration: Int64 = 0
            if stretch && entry.kind == .animation {
                let start = Int64(layer.startFrame), end = Int64(layer.endFrame), frame = model.status.playhead
                let from = frame >= start && frame < end ? frame : start
                duration = max(1, end - from - 1)
            }
            let error = model.engine.applyPreset(layerId, json: source, duration: duration)
            if !error.isEmpty { model.toast = AureaText.t("msg_preset_nao_aplicado", AureaEngineText.reason(error)); return }
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
            guard h.count >= 4 else { model.toast = kind.noData; return }
            let ease = CurveEase(interpolation: key.interpolation, x1: h[0].floatValue, y1: h[1].floatValue, x2: h[2].floatValue, y2: h[3].floatValue,
                                 power: h.count >= 5 ? min(3, max(1, h[4].intValue)) : 1)
            // A força vai junto (5º número) quando é bézier.
            json = model.engine.makeCurvePreset(name, interpolation: key.interpolation,
                handles: (ease.handles + [Float(ease.isBezier ? ease.power : 1)]).map { NSNumber(value: $0) })
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
            AureaAlert(title: AureaText.t("ios_delete_named_q", title), message: AureaText.t("panel_preset_sai_deste_aparelho"),
                       confirmLabel: AureaText.t("panel_apagar"), destructive: true,
                       onConfirm: { request.onDelete?() }, onDismiss: onDismiss)
        case .save(let kind):
            AureaAlert(title: AureaText.t("pn_save_preset_of", kind.label.lowercased()), message: kind.saveMessage,
                       confirmLabel: AureaText.t("panel_salvar"), onConfirm: {
                let title = name.trimmingCharacters(in: .whitespacesAndNewlines)
                guard !title.isEmpty else { model.toast = AureaText.t("panel_nome_preset"); return }
                let parts: UInt32 = (style ? 1 : 0) | (animation ? 2 : 0)
                guard kind != .text || parts != 0 else { model.toast = AureaText.t("panel_escolha_estilo_ou_animacao"); return }
                request.onSave?(title, parts)
            }, onDismiss: onDismiss, extra: AnyView(fields(kind)))
        }
    }
    private func fields(_ kind: PanelPresetKind) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            // Omega fundação (F4): campo e textos do diálogo claro.
            AureaDialogField(text: Binding(get: { name }, set: { name = String($0.prefix(60)) }))
            if !name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && request.exists(name) {
                Text(AureaText.t("panel_ja_existe_sera_substituido")).font(.aurea(size: 12)).foregroundStyle(AureaColors.lightDanger)
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
    /// Rótulo de acessibilidade do toque (padrão: "Aplicar <nome>").
    var applyLabel: String? = nil
    var body: some View {
        Button(action: onApply) {
            VStack(alignment: .leading, spacing: 0) {
                PresetPreviewView(entry: entry, compositionWidth: compositionWidth).frame(height: 62)
                    .background(AureaColors.stage, in: RoundedRectangle(cornerRadius: 8)).clipShape(RoundedRectangle(cornerRadius: 8))
                Color.clear.frame(height: 5)
                Text(entry.name).font(.aurea(size: 11.5, weight: .semibold)).tracking(aureaTracking(-0.1)).lineLimit(2)
                    .lineSpacing(max(0, 14 - UIFont.systemFont(ofSize: 11.5, weight: .semibold).lineHeight))
                    .frame(maxWidth: .infinity, alignment: .topLeading).frame(height: 30, alignment: .topLeading)
                if showKind { Text(entry.kind.label).font(.aurea(size: 10)).foregroundStyle(AureaColors.muted).lineLimit(1) }
            }.padding(6).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 12))
        }.buttonStyle(AureaPressStyle()).accessibilityLabel(applyLabel ?? AureaText.t("ios_apply_named", entry.name))
            .overlay(alignment: .topTrailing) {
                Button(action: onFavorite) {
                    CupertinoGlyph.text(favorite ? CupertinoGlyph.StarFill : CupertinoGlyph.Star, size: 15,
                                        color: favorite ? AureaColors.warning : AureaColors.muted)
                        .frame(width: 34, height: 34)
                }.buttonStyle(AureaPressStyle()).padding(6).accessibilityLabel(AureaText.t(favorite ? "panel_tirar_favoritos" : "panel_favoritar"))
            }
            .overlay(alignment: .topLeading) {
                if let onDelete {
                    Button(action: onDelete) {
                        CupertinoGlyph.text(CupertinoGlyph.Trash, size: 14, color: AureaColors.danger).frame(width: 34, height: 34)
                    }.buttonStyle(AureaPressStyle()).padding(6).accessibilityLabel(AureaText.t("panel_apagar_preset"))
                }
            }
    }
}

private func presetCurve(_ values: [NSNumber]) -> CurveEase? {
    guard values.count >= 5, values.allSatisfy({ $0.floatValue.isFinite }), (0...6).contains(values[0].intValue) else { return nil }
    // [interp, x1, y1, x2, y2, força]; preset antigo não tem a força (×1).
    let power = values.count >= 6 ? min(3, max(1, values[5].intValue)) : 1
    return CurveEase(interpolation: values[0].uint32Value, x1: values[1].floatValue, y1: values[2].floatValue, x2: values[3].floatValue, y2: values[4].floatValue, power: power)
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
                glyph(effectGlyph(keys.first), badge: keys.count > 1 ? AureaText.t("ios_effects_count", keys.count) : nil)
            case .text:
                SwiftUI.TimelineView(.animation(minimumInterval: 1.0 / 30.0)) { timeline in
                    let phase = timeline.date.timeIntervalSince(began).truncatingRemainder(dividingBy: 2.4) / 2.4
                    HStack(spacing: 0) {
                        ForEach(Array("Aurea".enumerated()), id: \.offset) { index, glyph in
                            let progress = min(1, max(0, phase * 2 - Double(index) * 0.12))
                            let id = entry.textPreset ?? 0
                            let alpha = id == 22 ? 0.3 + 0.7 * abs(sin(phase * 60 + Double(index) * 4)) : ([20, 21, 23, 24].contains(id) ? min(1, progress * 3) * min(1, (1 - phase) * 5) : 1)
                            Text(String(glyph)).font(.aurea(size: 24, weight: .bold)).foregroundStyle(AureaColors.text)
                                .opacity(alpha).scaleEffect(id == 19 ? 1 - 0.8 * sin(progress * .pi) : 1)
                        }
                    }.frame(maxWidth: .infinity, maxHeight: .infinity)
                }
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

// Omega aparência (área 05, P0-1 + P1-5) — par do AppearancePanel.kt: MESCLAGEM E
// OPACIDADE em página única, como a referência. A régua de opacidade no topo e, embaixo,
// a sanfona de categorias (uma aberta por vez; "Normal" aplica direto; abrir rola a
// categoria para o topo) com os modos numa fileira horizontal de itens 60×80. O texto
// de ajuda saiu da página e virou a entrada "Sobre a mesclagem" do ⋯.
struct AppearancePanel: View {
    @EnvironmentObject private var model: AureaModel
    @State private var openCategory: Int?
    @State private var openedFor: Int64?
    private var id: Int64 { model.primarySelection ?? 0 }
    private var opacity: Float { (model.detail["opacity"] as? NSNumber)?.floatValue ?? 1 }
    private var mode: UInt32 { model.selectedLayer?.blendMode ?? 0 }
    private var locked: Bool { model.selectedLayer?.locked == true }
    private var keys: [KeyframeItem] { (model.keyframes[id] ?? []).filter { $0.property == 12 && $0.effectIndex == .max && $0.paramIndex == 0 }.sorted { $0.time < $1.time } }
    private var look: KeyframeLook { keys.contains { $0.time == model.localPlayhead } ? .keyHere : keys.isEmpty ? .none : .animated }
    private var expr: ExpressionLook {
        let info = model.engine.expression(id, property: 12, effect: .max, param: 0)
        guard (info["exists"] as? NSNumber)?.boolValue == true else { return .none }
        if (info["enabled"] as? NSNumber)?.boolValue == false { return .off }
        return (info["error"] as? String ?? "").isEmpty ? .ok : .error
    }
    /// A ORDEM DA REFERÊNCIA (catálogo de 28). Números = `aurea::BlendMode` (Types.hpp);
    /// "Adicionar" (1) é extra do AUREA, no fim de Clarear. `direct` = toque aplica sem abrir.
    private static let categories: [(key: String, title: String, modes: [Int], direct: Int?)] = [
        ("normal", "panel_normal", [0], 0),
        ("darken", "pn_blend_darken", [3, 6, 24, 9, 21], nil),
        ("lighten", "pn_blend_lighten", [4, 7, 25, 8, 20, 1], nil),
        ("contrast", "pn_blend_cat_contrast", [5, 11, 10, 28, 19, 26, 27], nil),
        ("difference", "pn_blend_difference", [12, 13, 2, 18], nil),
        ("color", "pn_blend_color", [14, 15, 16, 17], nil),
        ("mask", "pn_blend_cat_mask", [22, 23], nil),
    ]
    static let modes = ["panel_normal", "pn_blend_add", "pn_blend_subtract", "pn_blend_multiply", "pn_blend_screen", "pn_blend_overlay",
        "pn_blend_darken", "pn_blend_lighten", "pn_blend_color_dodge", "pn_blend_color_burn", "pn_blend_hard_light", "pn_blend_soft_light",
        "pn_blend_difference", "pn_blend_exclusion", "pn_blend_hue", "pn_blend_saturation", "pn_blend_color", "pn_blend_luminosity",
        "pn_blend_divide", "pn_blend_vivid_light", "pn_blend_linear_dodge", "pn_blend_linear_burn", "pn_blend_mask", "pn_blend_exclude",
        "om_ap_blend_darker_color", "om_ap_blend_lighter_color", "om_ap_blend_linear_light", "om_ap_blend_pin_light", "om_ap_blend_soft_overlay"]
    /// A miniatura é ilustrativa (o modo do sistema mais próximo); a conta é a do motor (blend.frag).
    private static let previews: [GraphicsContext.BlendMode] = [.normal, .plusLighter, .difference, .multiply, .screen, .overlay, .darken, .lighten,
        .colorDodge, .colorBurn, .hardLight, .softLight, .difference, .exclusion, .hue, .saturation, .color, .luminosity,
        .colorDodge, .colorDodge, .plusLighter, .multiply, .destinationIn, .destinationOut,
        .darken, .lighten, .hardLight, .overlay, .softLight]

    private func write(_ value: Float) { model.editTransform(12, value: value.clamped(to: 0...100) / 100) }
    private func setMode(_ value: Int) {
        guard !locked else { return }
        model.mutate { $0.setLayer(id, blendMode: UInt32(value)) }
        model.refreshModel(force: true)
    }
    private func key() {
        model.mutate { core in
            if look == .keyHere { core.deleteKeyframe(forLayer: id, property: 12, time: model.localPlayhead) }
            else { core.insertKeyframe(forLayer: id, property: 12, time: model.localPlayhead, value: opacity) }
        }
        model.refreshModel(force: true)
    }
    /// ◆ segurado: apaga todas as chaves da opacidade num passo de desfazer.
    private func deleteAll() {
        let times = keys.map(\.time)
        guard !times.isEmpty else { return }
        model.mutate { core in
            core.beginUndoGroup()
            for time in times { core.deleteKeyframe(forLayer: id, property: 12, time: time) }
            core.endUndoGroup()
        }
        model.refreshModel(force: true)
    }
    private func curve() {
        guard keys.count >= 2 else { return }
        model.openCurve(property: 12, time: (keys.last { $0.time <= model.localPlayhead } ?? keys[0]).time)
    }
    private func expression() { model.expressionSheet = ExpressionRequest(layer: id, label: AureaText.t("panel_opacidade"), tracks: [ExpressionTrack(property: 12)], scale: 100, unit: "%") }
    private func keypad() { model.numericKeypad = KeypadRequest(title: AureaText.t("panel_opacidade"), value: opacity * 100, unit: "%", min: 0, max: 100, decimals: 0, onValue: write) }
    private func showAbout() {
        AureaPopupPresenter.shared.presentDialog { close in
            AureaAlert(title: AureaText.t("om_ap_blend_about"), message: AureaText.t("pn_blend_help"), showCancel: false,
                       onConfirm: {}, onDismiss: close)
        }
    }

    var body: some View {
        PanelFrame(rail: PanelRail(onBack: { model.panel = .none }, keyframeLook: look, onKeyframe: key,
                                   onDeleteAllKeyframes: deleteAll, onCurve: curve, curveReady: keys.count >= 2,
                                   expression: expr, onExpression: expression,
                                   moreEntries: { [.item(AureaText.t("om_ap_blend_about"), glyph: CupertinoGlyph.InfoCircle, tag: "blend.about") { showAbout() }] }),
                   scrollContent: false) {
            VStack(spacing: 0) {
                opacityLine
                accordion
            }
        }
        .foregroundStyle(AureaColors.text)
        .onAppear { syncOpenCategory(); model.timelineFocus = [TimelineTrack(property: 12)] }
        .onChange(of: id) { _ in syncOpenCategory(); model.timelineFocus = [TimelineTrack(property: 12)] }
        .onDisappear { model.timelineFocus = nil }
    }

    /// A categoria do modo ligado nasce aberta (Normal não abre).
    private func syncOpenCategory() {
        guard openedFor != id else { return }
        openedFor = id
        openCategory = Self.categories.firstIndex { $0.direct == nil && $0.modes.contains(Int(mode)) }
    }

    /// Ícone · régua · caixa de valor 56 (teclado 0–100). Um arrasto = um desfazer.
    private var opacityLine: some View {
        HStack(spacing: 6) {
            CupertinoGlyph.text(CupertinoGlyph.CircleLefthalfFill, size: 22, color: AureaColors.text)
            TickRuler(value: { opacity * 100 }, unitsPerDp: 0.35, active: true, height: 40)
                .valueDrag(enabled: !locked, start: { opacity * 100 }, unitsPerDp: { 0.35 }, min: 0, max: 100,
                           onStart: { model.beginGesture("opacidade") }, onValue: write, onEnd: { model.endGesture() })
                .accessibilityElement()
                .accessibilityLabel(AureaText.t("panel_opacidade"))
                .accessibilityValue("\(Int((opacity * 100).rounded()))%")
                .accessibilityIdentifier("blend.opacity.ruler")
            ValueBox("\(Int((opacity * 100).rounded()))%", enabled: !locked, width: 56, onTap: keypad)
                .accessibilityIdentifier("blend.opacity.value")
        }
        .frame(height: 40)
        .padding(.top, 5).padding(.leading, 6).padding(.trailing, 4)
    }

    private var accordion: some View {
        ScrollViewReader { proxy in
            ScrollView(.vertical) {
                LazyVStack(spacing: 0) {
                    ForEach(Self.categories.indices, id: \.self) { index in
                        category(index, proxy: proxy).id(index)
                    }
                }.padding(.bottom, 6.5)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private func category(_ index: Int, proxy: ScrollViewProxy) -> some View {
        let cat = Self.categories[index]
        let isOpen = openCategory == index
        let current = cat.modes.contains(Int(mode))
        return VStack(spacing: 0) {
            Button {
                if let direct = cat.direct {
                    setMode(direct)
                    openCategory = nil
                } else if isOpen {
                    openCategory = nil
                } else {
                    openCategory = index
                    withAnimation(.easeOut(duration: 0.2)) { proxy.scrollTo(index, anchor: .top) }
                }
            } label: {
                HStack(spacing: 6) {
                    if cat.direct == nil {
                        CupertinoGlyph.text(CupertinoGlyph.ChevronRight, size: 13, color: AureaColors.muted)
                            .mirrorsInRtl(!isOpen)
                            .rotationEffect(.degrees(isOpen ? 90 : 0))
                            .animation(.easeOut(duration: 0.15), value: isOpen)
                            .frame(width: 18, height: 18)
                    } else {
                        Color.clear.frame(width: 18, height: 18)
                    }
                    Text(AureaText.t(cat.title)).font(.aurea(size: 13.5, weight: .semibold)).lineLimit(1)
                    Spacer(minLength: 4)
                    if current {
                        Text(AureaText.t(Self.modes[Int(mode)])).font(.aurea(size: 12)).foregroundStyle(AureaColors.accent).lineLimit(1)
                        CupertinoGlyph.text(CupertinoGlyph.CheckmarkCircleFill, size: 16, color: AureaColors.accent)
                    }
                }
                .padding(.horizontal, 8)
                .frame(height: 37)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .disabled(locked)
            .accessibilityIdentifier("blend.category.\(cat.key)")
            .accessibilityAddTraits(current ? .isSelected : [])
            if isOpen {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 2) {
                        ForEach(cat.modes, id: \.self) { value in modeItem(value) }
                    }.padding(.horizontal, 4)
                }
                .frame(height: 80)
            }
        }
        .background(isOpen ? AureaColors.chip : AureaColors.surfaceHigh, in: RoundedRectangle(cornerRadius: 5))
        .padding(.horizontal, 6.5)
        .padding(.top, 6.5)
    }

    /// Um modo: 60 × 80 — miniatura de 45 (raio = lado/6) e o nome 11.
    private func modeItem(_ value: Int) -> some View {
        let lit = mode == UInt32(value)
        return Button { setMode(value) } label: {
            VStack(spacing: 4) {
                ZStack {
                    RoundedRectangle(cornerRadius: 7.5).fill(AureaColors.surface)
                    if value == 22 || value == 23 {
                        BlendCutThumb(exclude: value == 23, size: CGSize(width: 41, height: 41))
                    } else {
                        Canvas { context, size in
                            let r = min(size.width, size.height) * 0.30
                            context.fill(Path(ellipseIn: CGRect(x: size.width * 0.38 - r, y: size.height / 2 - r, width: r * 2, height: r * 2)), with: .color(AureaColors.blendThumbBottom))
                            context.blendMode = Self.previews[value]
                            context.fill(Path(ellipseIn: CGRect(x: size.width * 0.62 - r, y: size.height / 2 - r, width: r * 2, height: r * 2)), with: .color(AureaColors.blendThumbTop))
                        }
                        .frame(width: 41, height: 41)
                        .drawingGroup()
                    }
                }
                .frame(width: 45, height: 45)
                .clipShape(RoundedRectangle(cornerRadius: 7.5))
                .overlay(RoundedRectangle(cornerRadius: 7.5).stroke(lit ? AureaColors.accent : .clear, lineWidth: 2))
                Text(AureaText.t(Self.modes[value])).font(.aurea(size: 11))
                    .foregroundStyle(lit ? AureaColors.accent : AureaColors.muted)
                    .lineLimit(1).padding(.horizontal, 2)
            }
            .frame(width: 60, height: 80)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .disabled(locked)
        .accessibilityLabel(AureaText.t(Self.modes[value]))
        .accessibilityAddTraits(lit ? .isSelected : [])
        .accessibilityIdentifier("blend.mode.\(value)")
    }
}

// SpeedAudioPanels.kt: logarithmic speed ruler, real remap and mixer controls.
struct ClipEditPanel: View {
    @EnvironmentObject private var model: AureaModel
    @State private var mode: UInt32 = 2
    @State private var step = "1"
    @State private var previous: Int64 = 0
    @State private var next: Int64 = 0
    private var before: [LayerItem] {
        guard let row = model.selectedLayer else { return [] }
        return model.layers.filter { $0.id != row.id && !$0.locked && $0.endFrame == row.startFrame }
    }
    private var after: [LayerItem] {
        guard let row = model.selectedLayer else { return [] }
        return model.layers.filter { $0.id != row.id && !$0.locked && $0.startFrame == row.endFrame }
    }
    private var left: Int64 { before.contains { $0.id == previous } ? previous : before.count == 1 ? before[0].id : 0 }
    private var right: Int64 { after.contains { $0.id == next } ? next : after.count == 1 ? after[0].id : 0 }
    private var frames: Int64? { guard let value = Int64(step), (1...3600).contains(value) else { return nil }; return value }
    private var enabled: Bool {
        model.selectedLayer?.locked == false && frames != nil && ((mode != 3 && mode != 5) || left != 0) && ((mode != 4 && mode != 5) || right != 0)
    }
    var body: some View {
        VStack(spacing: 0) {
            PanelHeader(title: AureaText.t("i18n_clip_modes")) { model.panel = .none }
            ScrollView {
                VStack(alignment: .leading, spacing: 12) {
                    if let row = model.selectedLayer {
                        Text(row.name).font(.aurea(size: 15))
                        Text(AureaText.t("ios_clip_range_frames", "\(row.startFrame)", "\(row.endFrame)", "\(row.duration)")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                        Button(AureaText.t("beta_extend_clip")) { model.editClipTime(7, amount: 0) }
                            .frame(maxWidth: .infinity, minHeight: 48).disabled(row.locked)
                    }
                    LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())]) {
                        ForEach(2..<6) { value in
                            let title = [AureaText.t("i18n_clip_slip"), AureaText.t("ios_clip_roll_in"), AureaText.t("ios_clip_roll_out"), AureaText.t("i18n_clip_slide")][value - 2]
                            Button { mode = UInt32(value) } label: {
                                Text(mode == UInt32(value) ? "✓ " + title : title).frame(maxWidth: .infinity, minHeight: 48)
                            }.accessibilityLabel(title).accessibilityIdentifier("clipEdit.mode.\(value)").foregroundStyle(mode == UInt32(value) ? AureaColors.accent : AureaColors.text)
                        }
                    }
                    Text(AureaText.t(mode == 2 ? "ios_clip_slip_desc" : mode == 5 ? "ios_clip_slide_desc" : "ios_clip_roll_desc"))
                        .font(.aurea(size: 13)).foregroundStyle(AureaColors.muted).fixedSize(horizontal: false, vertical: true)
                    if mode == 3 || mode == 5 { neighbour(AureaText.t("ios_clip_prev_neighbour"), items: before, selected: left) { previous = $0 } }
                    if mode == 4 || mode == 5 { neighbour(AureaText.t("ios_clip_next_neighbour"), items: after, selected: right) { next = $0 } }
                    TextField(AureaText.t("ios_clip_step_hint"), text: $step).keyboardType(.numberPad)
                        .textFieldStyle(.roundedBorder).accessibilityIdentifier("clipEdit.step").frame(minHeight: 44)
                    HStack(spacing: 12) {
                        Button { model.editClipTime(mode, amount: -(frames ?? 1), previous: left, next: right) } label: {
                            Text(AureaText.t("ios_clip_back_n", frames ?? 0)).frame(maxWidth: .infinity, minHeight: 48)
                        }.accessibilityLabel(AureaText.t("ios_clip_back_a11y")).accessibilityIdentifier("clipEdit.back")
                        Button { model.editClipTime(mode, amount: frames ?? 1, previous: left, next: right) } label: {
                            Text(AureaText.t("ios_clip_forward_n", frames ?? 0)).frame(maxWidth: .infinity, minHeight: 48)
                        }.accessibilityLabel(AureaText.t("ios_clip_forward_a11y")).accessibilityIdentifier("clipEdit.forward")
                    }.disabled(!enabled)
                    if !enabled { Text(AureaText.t("ios_clip_edit_disabled_hint")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted) }
                }.padding(16)
            }
        }.foregroundStyle(AureaColors.text)
    }
    private func neighbour(_ title: String, items: [LayerItem], selected: Int64, select: @escaping (Int64) -> Void) -> some View {
        Menu {
            ForEach(items) { item in Button(item.name) { select(item.id) } }
        } label: {
            Text(title + ": " + (items.first { $0.id == selected }?.name ?? AureaText.t("edt_clip_choose"))).frame(maxWidth: .infinity, minHeight: 48)
        }
    }
}

// Omega painel: (área 04) `SpeedPanel` e `AudioPanel` foram para a moldura comum
// em LayerPanelView.swift (par do SpeedAudioPanels.kt).

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
    private let types = [0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24]
    private var track: [KeyframeItem] {
        preferredCurveTrack(([selected] + ([5, 6, 1, 2, 3, 4] + Array(7...14)).filter { $0 != selected }).map { param in
            (model.keyframes[id] ?? []).filter { $0.property == 35 && $0.paramIndex == UInt32(param) }.sorted { $0.time < $1.time }
        })
    }
    /// Bits de animado e de keyframe aqui: os dois últimos números de `shapeParams`
    /// (o motor tem 15 parâmetros; antes eram 7 — nunca por índice fixo).
    private var animBits: UInt32 { params.count >= 2 ? UInt32(max(0, params[params.count - 2])) : 0 }
    private var keyBits: UInt32 { params.count >= 2 ? UInt32(max(0, params[params.count - 1])) : 0 }
    private func look(_ param: Int) -> KeyframeLook {
        if keyBits & (1 << UInt32(param)) != 0 { return .keyHere }
        return animBits & (1 << UInt32(param)) != 0 ? .animated : .none
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
            .onAppear { load(); if geometry { model.shapeSelectedParam = axis == 0 ? 5 : 6 }; updateTimelineFocus() }
            .onChange(of: id) { _ in finishGesture(); load(); updateTimelineFocus() }
            .onChange(of: model.shapeSelectedParam) { _ in updateTimelineFocus() }
            .onChange(of: model.status.modelRevision) { _ in load() }
            .onChange(of: model.status.playhead) { _ in load() }
            .onDisappear { finishGesture(); model.timelineFocus = nil }
    }

    private func updateTimelineFocus() {
        let params = geometry ? ((selected == 5 || selected == 6) ? [5, 6] : [selected]) : [4]
        model.timelineFocus = params.filter { (1...14).contains($0) }.map { TimelineTrack(property: 35, effect: 0, param: UInt32($0)) }
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
                     onCurve: track.isEmpty ? nil : openCurve, onBack: { finishGesture(); model.panel = .none })
            ScrollView {
                VStack(alignment: .leading, spacing: 0) {
                    switcher
                    Color.clear.frame(height: 4)
                    sizeRow
                    if [0, 16, 21].contains(shapeType) {
                        shapeRow(1, "panel_raio", value: value(1), step: 0.3, range: 0...max(0, min(value(5), value(6)) / 2), unit: "px", reset: 0, gesture: "raio") { write(1, $0) }
                    }
                    if [3, 4, 8, 14].contains(shapeType) {
                        shapeRow(2, shapeType == 3 ? "panel_lados" : shapeType == 8 ? "panel_petalas" : shapeType == 14 ? "panel_dentes" : "panel_pontas", value: value(2), step: 0.06, range: 3...64, reset: 5, gesture: "pontas") { write(2, $0.rounded()) }
                    }
                    if [19, 23, 24].contains(shapeType) { countRow }
                    if shapeType == 4 {
                        shapeRow(3, "panel_raio_interno", value: value(3) * 100, step: 0.3, range: 5...95, unit: "%", reset: 50, gesture: AureaText.t("panel_raio_interno_75cf")) { write(3, $0 / 100) }
                    } else if shapeType == 5 || shapeType == 6 {
                        shapeRow(3, "panel_espessura", value: (shapeType == 6 ? 1 - value(3) : value(3)) * 100, step: 0.3, range: 5...95, unit: "%", reset: 50, gesture: "espessura") { write(3, shapeType == 6 ? 1 - $0 / 100 : $0 / 100) }
                    } else if [12, 13, 14, 15].contains(shapeType) {
                        // Trapézio: largura do topo; paralelogramo: inclinação; engrenagem: cubo; seta dupla: haste.
                        let key = ["panel_largura_topo", "panel_inclinacao", "panel_cubo", "panel_espessura"][shapeType - 12]
                        let reset: Float = [60, 50, 30, 22][shapeType - 12]
                        shapeRow(3, key, value: value(3) * 100, step: 0.3, range: 5...95, unit: "%", reset: reset, gesture: key) { write(3, $0 / 100) }
                    }
                    paramRows
                    Color.clear.frame(height: 6)
                    kitHint(AureaText.t("panel_arraste_alcas_palco_mudar_tamanho") + (shapeType == 0 ? "; " + AureaText.t("ios_shape_blue_handle_rounds") + ". " : ". ") + AureaText.t("panel_losango_trilho_grava_keyframe_linha_acesa"))
                }.padding(.top, 6).padding(.trailing, 10).padding(.bottom, 16)
            }
        }
    }

    private var switcher: some View {
        HStack(spacing: 0) {
            switchButton(CupertinoGlyph.ChevronLeft, label: AureaText.t("panel_forma_anterior"), delta: -1)
            ShapeEditGlyph(type: shapeType).frame(width: 26, height: 26)
                .frame(width: 44, height: 40).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
            switchButton(CupertinoGlyph.ChevronRight, label: AureaText.t("panel_proxima_forma"), delta: 1)
            Color.clear.frame(width: 6)
            Text(shapeName).font(.aurea(size: 15, weight: .bold)).frame(maxWidth: .infinity, alignment: .leading)
            // Omega conteúdo (área 06, P0-6): "manter proporção" e "a partir do centro" moram
            // na barra da seção (ContentSectionTrailing), salvos no aparelho.
        }.frame(height: 52)
    }
    private func switchButton(_ glyph: Character, label: String, delta: Int) -> some View {
        Button {
            let index = types.firstIndex(of: shapeType) ?? 0, next = types[(index + delta + types.count) % types.count]
            beginGesture("trocar forma")
            // Type 0 is a command; the animated shape API takes 1...14.
            model.mutate { $0.setShape(id, param: 0, value: Float(next)) }
            finishGesture(); load()
        } label: { CupertinoGlyph.text(glyph, size: 18).mirrorsInRtl(CupertinoGlyph.mirrorsInRtl(glyph)).frame(width: 44, height: 44) }
            .buttonStyle(AureaPressStyle(shrink: 1)).accessibilityLabel(label)
    }
    private var shapeName: String {
        switch shapeType {
        case 0: return AureaText.t("panel_retangulo"); case 1: return AureaText.t("panel_elipse"); case 3: return AureaText.t("editor_poligono"); case 4: return AureaText.t("editor_estrela")
        case 5: return AureaText.t("sh_shape_cross"); case 6: return AureaText.t("sh_shape_ring"); case 7: return AureaText.t("sh_shape_slice"); case 8: return AureaText.t("sh_shape_flower")
        case 9: return AureaText.t("sh_shape_arrow"); case 10: return AureaText.t("sh_shape_triangle")
        case 12: return AureaText.t("sh_shape_trapezoid"); case 13: return AureaText.t("sh_shape_parallelogram")
        case 14: return AureaText.t("sh_shape_gear"); case 15: return AureaText.t("sh_shape_double_arrow")
        case 16: return AureaText.t("sh_shape_line"); case 17: return AureaText.t("sh_shape_diamond"); case 18: return AureaText.t("sh_shape_heart")
        case 19: return AureaText.t("sh_shape_seal"); case 20: return AureaText.t("sh_shape_arc"); case 21: return AureaText.t("sh_shape_bubble")
        case 22: return AureaText.t("sh_shape_bolt"); case 23: return AureaText.t("sh_shape_wave"); case 24: return AureaText.t("sh_shape_blob")
        default: return AureaText.t("target_shape")
        }
    }
    /// Selo: saliências; onda: ondas; mancha: lóbulos (o parâmetro 2 do motor).
    private var countRow: some View {
        let key = shapeType == 19 ? "shp_bumps" : shapeType == 23 ? "shp_waves" : "shp_lobes"
        let low: Float = shapeType == 19 ? 3 : shapeType == 23 ? 1 : 2
        let high: Float = shapeType == 19 ? 48 : 12
        let reset: Float = shapeType == 19 ? 14 : shapeType == 23 ? 3 : 4
        return shapeRow(2, key, value: value(2), step: 0.06, range: low...high, reset: reset, gesture: "contagem") { write(2, $0.rounded()) }
    }
    /// Parâmetros das formas paramétricas (7..14 do motor, ShapeGeometry.hpp);
    /// o valor vem resolvido (padrão da forma) em `shapeParams`.
    @ViewBuilder private var paramRows: some View {
        switch shapeType {
        case 7: sweepRow
        case 8: percentRow(7, "shp_depth", range: 5...95, reset: 28, gesture: "profundidade")
        case 9:
            percentRow(11, "shp_head", range: 10...90, reset: 45, gesture: "ponta")
            percentRow(12, "shp_shaft", range: 5...100, reset: 44, gesture: "haste")
        case 14: percentRow(7, "shp_depth", range: 5...95, reset: 22, gesture: "profundidade")
        case 15: percentRow(11, "shp_head", range: 10...90, reset: 27.5, gesture: "ponta")
        case 16: percentRow(9, "panel_espessura", range: 2...100, reset: 100, gesture: "espessura")
        case 17: percentRow(8, "shp_waist", range: 5...95, reset: 50, gesture: "cintura")
        case 18: percentRow(7, "shp_depth", range: 5...95, reset: 40, gesture: "profundidade")
        case 19: percentRow(7, "shp_depth", range: 5...95, reset: 60, gesture: "profundidade")
        case 20:
            percentRow(9, "panel_espessura", range: 2...50, reset: 20, gesture: "espessura")
            sweepRow
        case 21: percentRow(8, "shp_tail", range: 0...100, reset: 30, gesture: "ponta do balão")
        case 22: percentRow(8, "panel_inclinacao", range: 0...100, reset: 50, gesture: "inclinacao")
        case 23:
            percentRow(9, "panel_espessura", range: 2...100, reset: 40, gesture: "espessura")
            percentRow(13, "shp_amplitude", range: 0...100, reset: 100, gesture: "amplitude")
        case 24:
            percentRow(7, "shp_variation", range: 5...95, reset: 50, gesture: "variacao")
            shapeRow(14, "shp_seed", value: value(14), step: 0.06, range: 0...99, reset: 0, gesture: "variante") { write(14, $0.rounded()) }
        default: EmptyView()
        }
    }
    /// Parâmetro em porcentagem (o motor guarda a fração 0..1).
    private func percentRow(_ param: Int, _ key: String, range: ClosedRange<Float>, reset: Float, gesture: String) -> some View {
        shapeRow(param, key, value: value(param) * 100, step: 0.3, range: range, unit: "%", reset: reset, gesture: gesture) { write(param, $0 / 100) }
    }
    /// Abertura da fatia e do arco (graus; 270° = a fatia de 3/4 de sempre).
    private var sweepRow: some View {
        shapeRow(10, "shp_sweep", value: value(10), step: 0.6, range: 1...360, unit: "°", reset: 270, gesture: "abertura") { write(10, $0) }
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
                model.numericKeypad = KeypadRequest(title: AureaText.t(item == 0 ? "panel_largura" : "panel_altura"), value: shown, unit: "px", min: 1, max: 16384, decimals: 0) { v in
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
        if ContentPrefs.shared.lockAspect {   // Omega conteúdo (área 06, P0-6)
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
        guard !track.isEmpty else { return }
        let index = max(0, min(track.count - 2, track.lastIndex { $0.time <= model.localPlayhead } ?? 0))
        model.openCurve(property: 35, effect: track[index].effectIndex, param: track[index].paramIndex, time: track[index].time)
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
            case 12: path = ShapeGlyphPaths.quad(center: c, radius: r, [-0.6, -0.7, 0.6, -0.7, 1, 0.7, -1, 0.7])
            case 13: path = ShapeGlyphPaths.quad(center: c, radius: r, [-0.5, -0.7, 1, -0.7, 0.5, 0.7, -1, 0.7])
            case 14:
                context.fill(ShapeGlyphPaths.gear(center: c, radius: r, teeth: 10, hub: 0.3), with: .color(AureaColors.text), style: FillStyle(eoFill: true)); return
            case 15: path = ShapeGlyphPaths.doubleArrow(center: c, radius: r)
            case 16: path.addRoundedRect(in: CGRect(x: c.x - r, y: c.y - r * 0.14, width: 2 * r, height: 0.28 * r), cornerSize: CGSize(width: r * 0.06, height: r * 0.06))
            case 17: path = ShapeGlyphPaths.diamond(center: c, radius: r)
            case 18: path = ShapeGlyphPaths.heart(center: c, radius: r)
            case 19: path = ShapeGlyphPaths.seal(center: c, radius: r, bumps: 12)
            case 20:
                path.addArc(center: c, radius: r * 0.82, startAngle: .degrees(0), endAngle: .degrees(270), clockwise: false)
                context.stroke(path, with: .color(AureaColors.text), lineWidth: r * 0.36); return
            case 21:
                for part in ShapeGlyphPaths.bubble(center: c, radius: r) { context.fill(part, with: .color(AureaColors.text)) }
                return
            case 22: path = ShapeGlyphPaths.bolt(center: c, radius: r)
            case 23:
                context.stroke(ShapeGlyphPaths.wave(center: c, radius: r, waves: 2), with: .color(AureaColors.text), lineWidth: r * 0.3); return
            case 24: path = ShapeGlyphPaths.blob(center: c, radius: r)
            default: path.addRect(CGRect(x: c.x - r, y: c.y - r, width: 2 * r, height: 2 * r))
            }
            context.fill(path, with: .color(AureaColors.text))
        }
    }
}

// MaskPanel.kt: máscaras numa tela só (três botões de criar sempre à vista,
// chips das máscaras e, da escolhida, Caminho, Modo, Borda, Seguir objeto e
// Apagar) e o recorte por outra camada na segunda aba.
struct MaskPanel: View {
    @EnvironmentObject private var model: AureaModel
    @State private var tab = 0
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
                    }.accessibilityIdentifier("mask.scroll")
                }
            }
        }.foregroundStyle(AureaColors.text).onAppear { load() }
            .onChange(of: id) { _ in load() }
            .onChange(of: model.status.modelRevision) { _ in load() }
            .onChange(of: model.timelineFocus) { focus in
                if focus?.contains(where: { $0.property == 43 && $0.effect == mask?.id }) == true { tab = 0 }
            }
    }
    @ViewBuilder private var masksBody: some View {
        Color.clear.frame(height: 6)
        // Os três jeitos de criar uma máscara: sempre à vista, um toque cada.
        HStack(spacing: 6) {
            MaskShapeTool(kind: 0, label: AureaText.t("panel_desenhar_mao"), selected: model.maskDrawing && mask?.closed == false) { model.addMask(2) }
                .accessibilityIdentifier("mask.add.draw")
            MaskShapeTool(kind: 1, label: AureaText.t("panel_retangulo"), selected: false) { model.addMask(0) }
                .accessibilityIdentifier("mask.add.rect")
            MaskShapeTool(kind: 2, label: AureaText.t("panel_elipse"), selected: false) { model.addMask(1) }
                .accessibilityIdentifier("mask.add.ellipse")
        }
        if model.masks.isEmpty {
            Color.clear.frame(height: 10)
            hint("panel_mascara_mostra_so_parte_camada_escolha")
        } else {
            Color.clear.frame(height: 4)
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    ForEach(Array(model.masks.enumerated()), id: \.element.id) { entry in
                        NativePanelChip(AureaText.t(entry.element.closed ? "pn_mask_n" : "pn_mask_n_open", entry.offset + 1), selected: mask?.id == entry.element.id, horizontal: 12, height: 36, radius: 9, fontSize: 12.5) {
                            model.selectedMask = entry.element.id; model.selectedMaskPoint = nil; model.maskDrawing = !entry.element.closed
                        }
                    }
                }.frame(height: 44)
            }
            if let mask { maskEditor(mask) }
            else { hint("panel_escolha_mascara_acima_editar_palco") }
        }
    }
    /// A máscara escolhida, tudo numa tela só, na ordem de uso.
    @ViewBuilder private func maskEditor(_ m: MaskItem) -> some View {
        title("panel_caminho")
        if model.maskDrawing {
            hint("panel_toque_palco_pontos_arraste_curvar_toque")
            Color.clear.frame(height: 8)
            NativePanelAction("panel_fechar_caminho", detail: AureaText.plural("pn_mask_points_so_far", m.points.count / 6)) {
                if m.points.count >= 18 { model.setMaskPoints(m.points, closed: true); model.maskDrawing = false }
                else { model.toast = AureaText.t("msg_ponha_pelo_menos_3_pontos") }
            }
            // Sem caminho fechado ainda não há modo/borda para ajustar.
            Color.clear.frame(height: 10)
            deleteAction(m)
        } else {
            hint("panel_arraste_pontos_palco_toque_num_ponto")
            HStack(spacing: 8) {
                PropertyLabelChip(AureaText.t("panel_caminho"), expression: .none, keyframe: look, onTap: {})
                Text(m.keyCount == 0 ? AureaText.t("panel_parado_toque_trilho_animar") : AureaText.plural("pn_keyframes_edit_at_playhead", Int(m.keyCount)))
                    .font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).frame(maxWidth: .infinity, alignment: .leading)
            }.frame(height: 48).aureaSelectedProperty(AureaText.t("panel_caminho"))

            title("panel_mask_mode")
            ChoiceChips(["panel_somar", "panel_subtrair", "panel_intersecao", "panel_diferenca", "panel_desligada"].map { AureaText.t($0) }, selected: Int(m.operation)) { update(m, operation: UInt32($0)) }
            NativePanelToggle(AureaText.t("panel_inverter_mostrar_lado_fora"), checked: m.inverted) { update(m, inverted: $0) }

            title("panel_mask_edge")
            maskRuler(m, param: 0, label: "panel_suavizar", value: m.feather, range: 0...500, unit: "px", reset: 0)
            maskRuler(m, param: 1, label: "panel_expandir", value: m.expansion, range: -500...500, unit: "px", reset: 0)
            maskRuler(m, param: 2, label: "panel_opacidade", value: m.opacity * 100, range: 0...100, unit: "%", reset: 100)

            if model.selectedLayer?.kind == 1 {
                title("panel_mask_follow")
                if model.importingMedia { hint("panel_rastreando") }
                else {
                    hint("panel_mascara_segue_esta_embaixo_dela_cabecote")
                    Color.clear.frame(height: 8)
                    NativePanelAction("panel_seguir_posicao", detail: AureaText.t("panel_objetos_so_andam_pela_tela")) { track(m.id, mode: 0) }
                    Color.clear.frame(height: 6)
                    NativePanelAction("panel_seguir_posicao_tamanho_giro", detail: AureaText.t("panel_objetos_aproximam_ou_giram")) { track(m.id, mode: 1) }
                }
            }
            Color.clear.frame(height: 14)
            deleteAction(m)
        }
    }
    private func deleteAction(_ m: MaskItem) -> some View {
        NativePanelAction("panel_apagar_mascara", danger: true) {
            guard model.engine.removeMask(id, mask: m.id) else { return }
            if model.timelineFocus?.contains(where: { $0.property == 43 && $0.effect == m.id }) == true { model.timelineFocus = nil }
            if model.curveProperty == 43 && model.curveEffect == m.id { model.curveSelectedTime = nil }
            model.selectedMask = nil; model.selectedMaskPoint = nil; model.maskDrawing = false; model.refreshModel(force: true)
        }
    }
    private func maskRuler(_ mask: MaskItem, param: UInt32, label: String, value: Float, range: ClosedRange<Float>, unit: String, reset: Float) -> some View {
        let track = TimelineTrack(property: 43, effect: mask.id, param: param)
        let keys = (model.keyframes[id] ?? []).filter { $0.property == 43 && $0.effectIndex == mask.id && $0.paramIndex == param }
        let look: KeyframeLook = keys.contains { $0.time == model.localPlayhead } ? .keyHere : keys.isEmpty ? .none : .animated
        let selected = model.timelineFocus == [track]
        return VStack(spacing: 0) {
            NativePanelRuler(label: AureaText.t(label), value: value, step: 0.5, range: range, unit: unit, decimals: 1, reset: reset, keypad: true, look: look, toggleKey: {
                model.timelineFocus = [track]
                _ = model.engine.toggleMaskParamKey(id, mask: mask.id, param: param); model.refreshModel(force: true)
            }, selected: selected, onSelect: { model.timelineFocus = [track] }) {
                model.timelineFocus = [track]
                _ = model.engine.setMaskParam(id, mask: mask.id, param: param, value: param == 2 ? $0 / 100 : $0); model.refreshModel(force: true)
            }.accessibilityIdentifier("mask.\(mask.id).param.\(param)")
            if selected { NativeAnimationTrackActions(track: track, curveTag: "mask.\(mask.id).curve.\(param)") }
        }
    }
    private var matteBody: some View {
        VStack(alignment: .leading, spacing: 0) {
            title("panel_1_recortar_pelo")
            Text(AureaText.t("pn_mask_target", model.layers.first { $0.id == id }?.name ?? ""))
                .font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
            if mode != 0, let source = candidates.first(where: { $0.id == matte }) {
                Text(AureaText.t("pn_mask_source", source.name))
                    .font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                Canvas { context, size in
                    let center = CGPoint(x: size.width / 2, y: size.height / 2)
                    let radius = size.height * 0.36
                    let circle = Path(ellipseIn: CGRect(x: center.x - radius, y: center.y - radius,
                        width: radius * 2, height: radius * 2))
                    context.fill(circle, with: .color(AureaColors.muted.opacity(0.18)))
                    var clipped = context
                    let inverted = mode == 2 || mode == 4
                    clipped.clip(to: Path(CGRect(x: inverted ? 0 : center.x, y: 0,
                        width: inverted ? center.x : size.width - center.x, height: size.height)))
                    clipped.fill(circle, with: .color(AureaColors.accent))
                    context.stroke(Path(CGRect(x: center.x, y: center.y - radius,
                        width: radius * 1.4, height: radius * 2)), with: .color(AureaColors.muted), lineWidth: 1)
                }.frame(height: 64).accessibilityHidden(true)
            }
            ChoiceChips(["panel_nao_recortar", "panel_pela_forma", "panel_pela_forma_invertido", "panel_pelo_brilho", "panel_pelo_brilho_invertido"].map { AureaText.t($0) }, selected: mode) { value in
                if value == 0 { write(0, 0) }
                else {
                    // Sem camada escolhida: a logo ABAIXO na pilha (a lista vem com a
                    // frente primeiro, `Engine::query_layers`). O motor usa a camada que
                    // a UI passar; os textos do painel dizem "abaixo" (par do Android, P1-7).
                    let own = model.layers.firstIndex { $0.id == id } ?? 0
                    let below = model.layers.dropFirst(own + 1).first { item in candidates.contains { $0.id == item.id } }
                    let target = matte != 0 ? matte : (below?.id ?? 0)
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


/// Botão grande de criar máscara: 0 desenhar, 1 retângulo, 2 elipse. Mesmo
/// visual da ferramenta do Vetor e do `ToolTile` do Android (MaskPanel.kt).
private struct MaskShapeTool: View {
    let kind: Int
    let label: String
    let selected: Bool
    let action: () -> Void
    var body: some View {
        let color = selected ? AureaColors.accent : AureaColors.text
        Button(action: action) {
            VStack(spacing: 4) {
                Canvas { context, size in draw(context, width: size.width, color: color) }.frame(width: 22, height: 22)
                Text(label).font(.aurea(size: 10, weight: .semibold)).foregroundStyle(color).lineLimit(2).multilineTextAlignment(.center)
            }.padding(.horizontal, 2).padding(.vertical, 6).frame(maxWidth: .infinity).frame(height: 60)
                .background(selected ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
                .overlay { if selected { RoundedRectangle(cornerRadius: 10).stroke(AureaColors.accent, lineWidth: 1.5) } }
        }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityLabel(label)
            .accessibilityAddTraits(selected ? .isSelected : [])
    }
    private func draw(_ ctx: GraphicsContext, width w: CGFloat, color: Color) {
        func p(_ x: CGFloat, _ y: CGFloat) -> CGPoint { CGPoint(x: w * x, y: w * y) }
        switch kind {
        case 0:
            var path = Path()
            path.move(to: p(0.06, 0.82)); path.addCurve(to: p(0.94, 0.2), control1: p(0.22, 0.18), control2: p(0.62, 0.98))
            ctx.stroke(path, with: .color(color), style: StrokeStyle(lineWidth: 1.6, lineCap: .round))
            let r = w * 0.09
            ctx.fill(Path(CGRect(x: w * 0.06 - r, y: w * 0.82 - r, width: r * 2, height: r * 2)), with: .color(color))
            ctx.fill(Path(CGRect(x: w * 0.94 - r, y: w * 0.2 - r, width: r * 2, height: r * 2)), with: .color(color))
        default:
            let box = CGRect(x: w * 0.1, y: w * 0.2, width: w * 0.8, height: w * 0.6)
            ctx.stroke(kind == 2 ? Path(ellipseIn: box) : Path(box), with: .color(color), lineWidth: 1.7)
        }
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
            ScrollView { TextAnimationSection().padding(.horizontal, 18).padding(.bottom, 24) }.accessibilityIdentifier("text.anim.scroll")
        }
    }
}

struct TextAnimationSection: View {
    var showAnimatorEffect = true
    var beforeAnimation: () -> Void = {}
    @EnvironmentObject private var model: AureaModel
    @State private var animators: [[Float]] = []
    @State private var animationError = false
    private var id: Int64 { model.primarySelection ?? 0 }
    private var presets: [String] { ["edt_tp_pop", "edt_tp_jump", "pn_textpreset_slide", "panel_escala", "pn_textpreset_appear", "panel_desfoque", "pn_textpreset_word_highlight", "pn_pop", "pn_textpreset_typewriter", "pn_textpreset_wave", "pn_textpreset_elastic"].map { AureaText.t($0) } + extraTextPresetNames }
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
                        NativePanelChip(title) {
                            beforeAnimation()
                            animationError = !model.engine.applyTextPreset(id, preset: UInt32(n))
                            refresh()
                        }
                    }
                }.frame(height: 44)
            }
            // Omega conteúdo (área 06, P1-11): os animadores em LISTA (linha de 37 pt que abre os
            // ajustes a partir dela) e "+ Adicionar" no fim, com os tipos.
            TextAnimatorList(animators: animators, reload: refresh, beforeAnimation: beforeAnimation).padding(.top, 6)
            if showAnimatorEffect { NativePanelChip(AureaText.t("text_animator_add")) {
                beforeAnimation()
                model.addEffectAndFocus(fxEffectTypeId("aurea.text.animator"), layer: id)
                refresh(); model.openPanel(.effects)
            }.accessibilityIdentifier("text.animator.add") }
            if showAnimatorEffect { NativePanelChip(AureaText.t("text_transform_add")) {
                beforeAnimation()
                model.addEffectAndFocus(fxEffectTypeId("aurea.text.transform"), layer: id)
                refresh(); model.openPanel(.effects)
            }.accessibilityIdentifier("text.transform.add") }
        }.foregroundStyle(AureaColors.text).onAppear { load() }.onChange(of: id) { _ in load() }
            .onChange(of: model.status.modelRevision) { _ in load() }.onChange(of: model.status.playhead) { _ in load() }
            .alert(AureaText.t("ios_text_anim_title"), isPresented: $animationError) {
                Button(AureaText.t("pn_ok"), role: .cancel) {}
            } message: {
                Text(AureaText.t("ios_text_anim_failed"))
            }
    }
}

/// Omega conteúdo (área 06, P1-11): a LISTA de animadores. Cada um é uma linha de 37 pt
/// (▸, ícone, nome e o que ele mexe); tocar abre os ajustes a partir da linha (200 ms)
/// com cabeçalho ▾ · ícone · nome · liga · ⋯ (duplicar, subir, descer) · 🗑. O último
/// item é "+ Adicionar", que abre os tipos. Par do AnimatorList do TextAnimSection.kt.
private struct TextAnimatorList: View {
    @EnvironmentObject private var model: AureaModel
    let animators: [[Float]]
    let reload: () -> Void
    var beforeAnimation: () -> Void = {}
    @State private var open = -1
    @State private var adding = false
    @State private var more = -1
    private var id: Int64 { model.primarySelection ?? 0 }
    /// As propriedades do animador (bit do motor, rótulo), na ordem do cartão.
    private static let props: [(Int, String)] = [
        (1 << 0, "fx_posicao"), (1 << 1, "panel_escala"), (1 << 2, "panel_rotacao"), (1 << 3, "panel_opacidade"),
        (1 << 4, "ios_spacing"), (1 << 5, "panel_desfoque"), (1 << 6, "ios_skew"), (1 << 7, "panel_contorno"),
        (1 << 8, "ios_scramble_letter"), (1 << 9, "panel_cor"), (1 << 10, "panel_cor_contorno"),
    ]

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            ForEach(animators.indices, id: \.self) { index in
                VStack(spacing: 0) {
                    if open == index {
                        header(index)
                        NativeTextAnimatorCard(index: UInt32(index), values: animators[index], embedded: true)
                    } else {
                        row(index)
                    }
                }
                .background(AureaColors.fxListSurface, in: RoundedRectangle(cornerRadius: 5))
                .clipShape(RoundedRectangle(cornerRadius: 5))
                .padding(.vertical, 3.25)
            }
            Button { adding = true } label: {
                HStack(spacing: 8) {
                    CupertinoGlyph.text(CupertinoGlyph.Plus, size: 16)
                    Text(AureaText.t("la_add")).font(.aurea(size: 13))
                    Spacer(minLength: 0)
                }
                .padding(.horizontal, 10).frame(height: 37)
                .background(AureaColors.fxLowSurface, in: RoundedRectangle(cornerRadius: 5))
                .contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle(shrink: 1))
            .padding(.vertical, 2)
            .accessibilityIdentifier("anim.add")
            .aureaAnchoredMenu(isPresented: $adding, width: 300, tone: .editor, tag: "anim.add.menu") { addEntries() }
        }
        .foregroundStyle(AureaColors.text)
        .animation(.easeInOut(duration: 0.2), value: open)
        .accessibilityElement(children: .contain)
        .accessibilityLabel(AureaText.t("om_anim_list_cd"))
        .accessibilityIdentifier("anim.list")
        .onChange(of: model.primarySelection) { _ in open = -1 }
    }

    private func what(_ v: [Float]) -> String {
        let bits = Int(v.count > 1 ? v[1] : 0)
        return Self.props.filter { bits & $0.0 != 0 }.map { AureaText.t($0.1) }.joined(separator: ", ")
    }

    /// A linha fechada: ▸ (alfa 0,3), ícone do tipo, o nome e o que ele mexe.
    private func row(_ index: Int) -> some View {
        let v = animators[index], list = what(v)
        return Button { open = index } label: {
            HStack(spacing: 0) {
                CupertinoGlyph.text(CupertinoGlyph.ArrowtriangleRightFill, size: 18, color: AureaColors.text.opacity(0.3))
                Color.clear.frame(width: 4, height: 1)
                CupertinoGlyph.text(CupertinoGlyph.Textformat, size: 18, color: v[0] > 0.5 ? AureaColors.text : AureaColors.muted)
                Color.clear.frame(width: 8, height: 1)
                Text(AureaText.t("edt_animation_n", index + 1)).font(.aurea(size: 13, weight: .semibold))
                Color.clear.frame(width: 8, height: 1)
                Text(list).font(.aurea(size: 11)).foregroundStyle(AureaColors.muted).lineLimit(1)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
            .padding(.horizontal, 6).frame(height: 37).contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .accessibilityLabel(AureaText.t("om_anim_item_cd", index + 1, list))
        .accessibilityIdentifier("anim.item.\(index)")
    }

    /// Cabeçalho do animador aberto: ▾ · ícone · nome · liga · ⋯ · 🗑.
    private func header(_ index: Int) -> some View {
        let v = animators[index]
        return HStack(spacing: 0) {
            Button { open = -1 } label: {
                CupertinoGlyph.text(CupertinoGlyph.ArrowtriangleDownFill, size: 16).frame(width: 36, height: 40).contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle(shrink: 1))
            .accessibilityLabel(AureaText.t("om_anim_collapse_cd"))
            CupertinoGlyph.text(CupertinoGlyph.Textformat, size: 18)
            Color.clear.frame(width: 8, height: 1)
            Text(AureaText.t("edt_animation_n", index + 1)).font(.aurea(size: 13, weight: .bold))
                .frame(maxWidth: .infinity, alignment: .leading)
            AureaToggle(checked: v[0] > 0.5) { set(index, [0: $0 ? 1 : 0]) }
            Button { more = index } label: {
                CupertinoGlyph.text(CupertinoGlyph.Ellipsis, size: 18, color: AureaColors.fxListIcon).frame(width: 36, height: 40).contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle(shrink: 1))
            .accessibilityLabel(AureaText.t("editor_mais"))
            .accessibilityIdentifier("anim.item.\(index).more")
            .aureaAnchoredMenu(isPresented: Binding(get: { more == index }, set: { if !$0 { more = -1 } }), tone: .editor, alignEnd: true,
                               tag: "anim.item.\(index).menu") {
                [
                    .item(AureaText.t("om_anim_duplicate"), glyph: CupertinoGlyph.PlusSquareOnSquare, tag: "anim.item.\(index).duplicate") {
                        if model.engine.duplicateTextAnimator(id, index: UInt32(index)) >= 0 { open = index + 1 }
                        reload()
                    },
                    .item(AureaText.t("panel_mover_cima"), glyph: CupertinoGlyph.ArrowUp, enabled: index > 0) {
                        if model.engine.moveTextAnimator(id, from: UInt32(index), to: UInt32(index - 1)) { open = index - 1 }
                        reload()
                    },
                    .item(AureaText.t("panel_mover_baixo"), glyph: CupertinoGlyph.ArrowDown, enabled: index < animators.count - 1) {
                        if model.engine.moveTextAnimator(id, from: UInt32(index), to: UInt32(index + 1)) { open = index + 1 }
                        reload()
                    },
                ]
            }
            Button {
                model.engine.removeTextAnimator(id, index: UInt32(index))
                open = -1
                reload()
            } label: {
                CupertinoGlyph.text(CupertinoGlyph.Trash, size: 18, color: AureaColors.fxListIcon).frame(width: 36, height: 40).contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle(shrink: 1))
            .accessibilityLabel(AureaText.t("panel_remover"))
            .accessibilityIdentifier("anim.item.\(index).delete")
        }
        .padding(.horizontal, 6)
        .frame(height: 40)
    }

    private func set(_ index: Int, _ updates: [Int: Float]) {
        let flat = model.engine.textAnimators(id).map(\.floatValue), start = index * 40
        guard flat.count >= start + 40 else { return }
        var next = Array(flat[start..<(start + 40)])
        for (slot, value) in updates { next[slot] = value }
        _ = model.engine.setTextAnimator(id, index: UInt32(index), values: next.map { NSNumber(value: $0) })
        reload()
    }

    /// Os tipos do "+ Adicionar": de texto (por propriedade), de camada e os efeitos de movimento.
    private func addEntries() -> [MenuEntry] {
        var entries: [MenuEntry] = [.header(AureaText.t("text_animator_add"))]
        for (bit, key) in Self.props {
            entries.append(.item(AureaText.t(key), glyph: CupertinoGlyph.Textformat) {
                beforeAnimation()
                let created = model.engine.addTextAnimator(id, props: UInt32(bit))
                reload()
                if created >= 0 { open = created }
            })
        }
        entries.append(.divider)
        entries.append(.item(AureaText.t("la_animators"), glyph: CupertinoGlyph.Move) { model.transformTab = 6; model.openPanel(.transform) })
        let motion: [(String, String)] = [("aurea.motion.oscillate.cycles", "fx_name_oscillate"), ("aurea.motion.swing", "fx_name_swing"),
                                          ("aurea.motion.wiggle", "fx_name_wiggle"), ("aurea.motion.twitch", "fx_name_twitch")]
        for (type, key) in motion {
            entries.append(.item(AureaText.t(key), glyph: CupertinoGlyph.Sparkles) {
                beforeAnimation()
                model.addEffectAndFocus(fxEffectTypeId(type), layer: id)
                reload()
                model.openPanel(.effects)
            })
        }
        return entries
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
    static var selectors: [NativeAnimParam] { [
        NativeAnimParam(id: 0, slot: 7, bit: 0, label: AureaText.t("panel_inicio"), unit: "%", step: 0.5, range: 0...100),
        NativeAnimParam(id: 1, slot: 8, bit: 0, label: AureaText.t("panel_fim"), unit: "%", step: 0.5, range: 0...100),
        NativeAnimParam(id: 2, slot: 9, bit: 0, label: AureaText.t("ios_anim_delay_between"), unit: "%", step: 0.5, range: -1000...1000),
        NativeAnimParam(id: 3, slot: 10, bit: 0, label: AureaText.t("panel_intensidade"), unit: "%", step: 0.5, range: -100...100)
    ] }
    static var properties: [NativeAnimParam] { [
        NativeAnimParam(id: 10, slot: 14, bit: 0, label: AureaText.t("panel_posicao_x"), unit: "px", step: 1, range: -5000...5000),
        NativeAnimParam(id: 11, slot: 15, bit: 0, label: AureaText.t("panel_posicao_y"), unit: "px", step: 1, range: -5000...5000),
        NativeAnimParam(id: 12, slot: 16, bit: 0, label: AureaText.t("pn_depth"), unit: "px", step: 1, range: -5000...5000),
        NativeAnimParam(id: 13, slot: 17, bit: 1, label: AureaText.t("fx_escala_x"), unit: "%", step: 1, range: -2000...2000),
        NativeAnimParam(id: 14, slot: 18, bit: 1, label: AureaText.t("fx_escala_y"), unit: "%", step: 1, range: -2000...2000),
        NativeAnimParam(id: 15, slot: 19, bit: 2, label: AureaText.t("edt_rotation_x"), unit: "°", step: 1, range: -3600...3600),
        NativeAnimParam(id: 16, slot: 20, bit: 2, label: AureaText.t("edt_rotation_y"), unit: "°", step: 1, range: -3600...3600),
        NativeAnimParam(id: 17, slot: 21, bit: 2, label: AureaText.t("edt_rotation_z"), unit: "°", step: 1, range: -3600...3600),
        NativeAnimParam(id: 18, slot: 22, bit: 3, label: AureaText.t("panel_opacidade"), unit: "%", step: 0.5, range: 0...100),
        NativeAnimParam(id: 19, slot: 23, bit: 4, label: AureaText.t("ios_spacing"), unit: "px", step: 0.5, range: -500...500),
        NativeAnimParam(id: 20, slot: 24, bit: 5, label: AureaText.t("panel_desfoque"), unit: "px", step: 0.2, range: 0...200),
        NativeAnimParam(id: 21, slot: 25, bit: 6, label: AureaText.t("ios_skew"), unit: "°", step: 0.5, range: -80...80),
        NativeAnimParam(id: 22, slot: 26, bit: 7, label: AureaText.t("panel_contorno"), unit: "px", step: 0.1, range: -50...50),
        NativeAnimParam(id: 23, slot: 27, bit: 8, label: AureaText.t("ios_scramble_letter"), unit: "", step: 0.1, range: -1000...1000)
    ] }
}

private struct NativeTextAnimatorCard: View {
    @EnvironmentObject private var model: AureaModel
    let index: UInt32
    let values: [Float]
    /// Omega conteúdo (área 06, P1-11): dentro da lista o cabeçalho é da linha aberta.
    var embedded = false
    private var id: Int64 { model.primarySelection ?? 0 }
    // Portuguese names stay as the undo labels sent to the engine; the screen shows `titles`.
    private let names = ["Posição", "Escala", "Rotação", "Opacidade", "Espaçamento", "Desfoque", "Inclinação", "Contorno", "Embaralhar letra", "Cor", "Cor do contorno"]
    private var titles: [String] { ["fx_posicao", "panel_escala", "panel_rotacao", "panel_opacidade", "ios_spacing", "panel_desfoque", "ios_skew", "panel_contorno", "ios_scramble_letter", "panel_cor", "panel_cor_contorno"].map { AureaText.t($0) } }
    private func set(_ updates: [Int: Float]) {
        let flat = model.engine.textAnimators(id).map(\.floatValue), start = Int(index) * 40
        guard flat.count >= start + 40 else { return }
        var next = Array(flat[start..<(start + 40)])
        for (slot, value) in updates { next[slot] = value }
        _ = model.engine.setTextAnimator(id, index: index, values: next.map { NSNumber(value: $0) }); model.refreshModel(force: true)
    }
    var body: some View {
        VStack(spacing: 0) {
            if !embedded {
                HStack(spacing: 8) {
                    Text(AureaText.t("ios_animation_n", Int(index) + 1)).font(.aurea(size: 13, weight: .bold)).frame(maxWidth: .infinity, alignment: .leading)
                    NativePanelChip(AureaText.t("panel_remover")) { model.engine.removeTextAnimator(id, index: index); model.refreshModel(force: true) }
                    AureaToggle(checked: values[0] > 0.5) { set([0: $0 ? 1 : 0]) }
                }.frame(height: 40)
            }
            choices("panel_anima_cada", ["panel_letra", "panel_palavra", "panel_linha"], slot: 2)
            choices("panel_escolhe", ["panel_ordem", "panel_sorteado", "ios_range_ae"], slot: 3)
            selectorControls
            ForEach(NativeAnimParam.properties.filter { Int(values[1]) & (1 << $0.bit) != 0 }) { param in NativeTextAnimRuler(index: index, param: param, values: values) }
            ForEach([9, 10], id: \.self) { bit in if Int(values[1]) & (1 << bit) != 0 { colorRow(bit) } }
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    ForEach(Array(names.enumerated()), id: \.offset) { bit, name in
                        NativePanelChip(titles[bit], selected: Int(values[1]) & (1 << bit) != 0) { set([1: Float(Int(values[1]) ^ (1 << bit))]) }
                    }
                }.frame(height: 44)
            }
        }.padding(8).background(embedded ? Color.clear : AureaColors.chip.opacity(0.45), in: RoundedRectangle(cornerRadius: 10))
    }
    @ViewBuilder private var selectorControls: some View {
        if Int(values[3]) != 1 {
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
            Text(titles[bit]).font(.aurea(size: 12)); Spacer()
            NativePanelColorWell(values: [values[base], values[base + 1], values[base + 2], 1]) {
                model.beginGesture(names[bit])
                model.colorSheet = ColorSheetRequest(title: titles[bit], initial: [values[base], values[base + 1], values[base + 2], 1], onChange: { r, g, b, _ in set([base: r, base + 1: g, base + 2: b]) }, onDone: { model.endGesture() })
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

struct NativeAnimationTrackActions: View {
    @EnvironmentObject private var model: AureaModel
    let track: TimelineTrack
    let curveTag: String
    private var id: Int64 { model.primarySelection ?? 0 }
    var body: some View {
            let navigation = model.engine.keyframeNavigation(id, property: UInt32(track.property), effect: track.effect,
                param: track.param, compositionFrame: model.status.playhead)
            let flags = navigation?["flags"]?.uint32Value ?? 0
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 12) {
                    Button(AureaText.t("panel_keyframe_anterior")) {
                        if let frame = navigation?["previousFrame"]?.int64Value { model.seek(toFrame: frame) }
                    }.disabled(flags & 2 == 0).frame(minHeight: 44)
                        .accessibilityIdentifier(curveTag + ".previous")
                    Button(AureaText.t("panel_proximo_keyframe")) {
                        if let frame = navigation?["nextFrame"]?.int64Value { model.seek(toFrame: frame) }
                    }.disabled(flags & 4 == 0).frame(minHeight: 44)
                        .accessibilityIdentifier(curveTag + ".next")
                    Button(AureaText.t("panel_curva")) {
                        guard let start = navigation?["segmentStart"]?.int64Value else { return }
                        model.openCurve(property: UInt32(track.property), effect: track.effect, param: track.param,
                                        time: Int32(clamping: start))
                    }.disabled(flags & 8 == 0).frame(minHeight: 44).accessibilityIdentifier(curveTag)
                }.buttonStyle(.borderless).frame(minHeight: 44)
            }
    }
}

// MARK: - Animadores de camada (qualquer tipo) — porta de editor/panels/LayerAnimSection.kt

/// ANIMADORES DA CAMADA: entrada "começa de", saída, força e atraso entre
/// letras, curva e wiggle — o mesmo cartão, régua e losango da animação de
/// texto. O motor avalia (prévia = export). 32 floats por animador.
struct LayerAnimatorSection: View {
    @EnvironmentObject private var model: AureaModel
    @State private var animators: [[Float]] = []
    @State private var failed = false
    private var id: Int64 { model.primarySelection ?? 0 }
    private func load() {
        let flat = model.engine.layerAnimators(id).map(\.floatValue)
        animators = stride(from: 0, to: flat.count - flat.count % 32, by: 32).map { Array(flat[$0..<($0 + 32)]) }
    }
    private func refresh() { model.refreshModel(force: true); load() }
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text(AureaText.t("la_animators")).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted)
            if animators.isEmpty {
                Text(AureaText.t("la_empty")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).padding(.top, 6)
            }
            ForEach(animators.indices, id: \.self) { index in
                NativeLayerAnimatorCard(index: UInt32(index), values: animators[index], reload: refresh).padding(.top, 8)
            }
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    NativePanelChip(AureaText.t("la_add")) { failed = model.engine.addLayerAnimator(id) < 0; refresh() }
                    if !animators.isEmpty { NativePanelChip(AureaText.t("la_copy")) { _ = model.engine.copyLayerAnimators(id) } }
                    NativePanelChip(AureaText.t("la_paste")) {
                        let ids = model.selection.isEmpty ? [id] : Array(model.selection)
                        _ = model.engine.pasteLayerAnimators(ids.map { NSNumber(value: $0) }); refresh()
                    }
                }.frame(height: 44)
            }.padding(.top, 6)
        }.foregroundStyle(AureaColors.text).onAppear { load() }.onChange(of: id) { _ in load() }
            .onChange(of: model.status.modelRevision) { _ in load() }.onChange(of: model.status.playhead) { _ in load() }
            .alert(AureaText.t("la_animators"), isPresented: $failed) {
                Button(AureaText.t("pn_ok"), role: .cancel) {}
            } message: {
                Text(AureaText.t("app_layer_anim_add_failed"))
            }
    }
}

private struct NativeLayerAnimParam: Identifiable {
    var id: Int
    var label: String
    var unit: String
    var step: Float
    var range: ClosedRange<Float>
    var slot: Int { 6 + id }
    static var strength: [NativeLayerAnimParam] { [
        NativeLayerAnimParam(id: 0, label: AureaText.t("la_progress"), unit: "%", step: 0.5, range: 0...100),
        NativeLayerAnimParam(id: 1, label: AureaText.t("la_strength"), unit: "%", step: 0.5, range: 0...100)
    ] }
    static var delay: NativeLayerAnimParam { NativeLayerAnimParam(id: 2, label: AureaText.t("la_delay"), unit: "ms", step: 2, range: 0...10000) }
    static var from: [NativeLayerAnimParam] { [
        NativeLayerAnimParam(id: 3, label: AureaText.t("panel_opacidade"), unit: "%", step: 0.5, range: 0...100),
        NativeLayerAnimParam(id: 4, label: AureaText.t("panel_posicao_x"), unit: "px", step: 1, range: -20000...20000),
        NativeLayerAnimParam(id: 5, label: AureaText.t("panel_posicao_y"), unit: "px", step: 1, range: -20000...20000),
        NativeLayerAnimParam(id: 6, label: AureaText.t("panel_escala"), unit: "%", step: 1, range: 0...5000),
        NativeLayerAnimParam(id: 7, label: AureaText.t("fx_escala_y"), unit: "%", step: 1, range: 0...5000),
        NativeLayerAnimParam(id: 8, label: AureaText.t("panel_rotacao"), unit: "°", step: 1, range: -3600...3600),
        NativeLayerAnimParam(id: 9, label: AureaText.t("edt_rotation_x"), unit: "°", step: 1, range: -3600...3600),
        NativeLayerAnimParam(id: 10, label: AureaText.t("edt_rotation_y"), unit: "°", step: 1, range: -3600...3600),
        NativeLayerAnimParam(id: 11, label: AureaText.t("ios_spacing"), unit: "px", step: 0.5, range: -20000...20000)
    ] }
    static var wiggle: [NativeLayerAnimParam] { [
        NativeLayerAnimParam(id: 12, label: AureaText.t("panel_posicao_x"), unit: "px", step: 1, range: -20000...20000),
        NativeLayerAnimParam(id: 13, label: AureaText.t("panel_posicao_y"), unit: "px", step: 1, range: -20000...20000),
        NativeLayerAnimParam(id: 14, label: AureaText.t("panel_escala"), unit: "%", step: 0.5, range: 0...1000),
        NativeLayerAnimParam(id: 15, label: AureaText.t("panel_rotacao"), unit: "°", step: 0.5, range: -3600...3600),
        NativeLayerAnimParam(id: 16, label: AureaText.t("la_wiggle_speed"), unit: "/s", step: 0.05, range: 0...60),
        NativeLayerAnimParam(id: 17, label: AureaText.t("la_wiggle_hold"), unit: "%", step: 0.5, range: 0...100)
    ] }
}

private struct NativeLayerAnimatorCard: View {
    @EnvironmentObject private var model: AureaModel
    let index: UInt32
    let values: [Float]
    let reload: () -> Void
    private var id: Int64 { model.primarySelection ?? 0 }
    private var isText: Bool { values[26] > 0.5 }
    private var unit: Int { Int(values[1]) }
    private func set(_ updates: [Int: Float]) {
        var next = values
        for (slot, value) in updates { next[slot] = value }
        _ = model.engine.setLayerAnimator(id, index: index, values: next.map { NSNumber(value: $0) })
        reload()
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(spacing: 8) {
                Text(AureaText.t("la_animator_n", Int(index) + 1)).font(.aurea(size: 13, weight: .bold)).frame(maxWidth: .infinity, alignment: .leading)
                NativePanelChip(AureaText.t("panel_remover")) { model.engine.removeLayerAnimator(id, index: index); reload() }
                AureaToggle(checked: values[0] > 0.5) { set([0: $0 ? 1 : 0]) }
            }.frame(height: 40)
            if isText { choices("la_unit", ["la_unit_whole", "panel_letra", "panel_palavra", "panel_linha"], slot: 1) }
            toggleLine("la_exit", hint: "la_exit_hint", checked: values[2] > 0.5) { set([2: $0 ? 1 : 0]) }
            choices("la_ease", ["la_ease_linear", "la_ease_smooth", "la_ease_inout", "la_ease_back"], slot: 3)
            groupLabel("la_strength_delay")
            ForEach(NativeLayerAnimParam.strength) { ruler($0) }
            if isText && unit != 0 {
                ruler(NativeLayerAnimParam.delay)
                NativePanelRuler(label: AureaText.t("beta_overlap"), value: values[28], step: 0.5,
                    range: 0...100, unit: "%", keypad: true) { set([28: $0]) }
            }
            groupLabel("la_from")
            ForEach(NativeLayerAnimParam.from.filter { ($0.id != 7 || values[4] > 0.5) && ($0.id != 11 || (isText && unit != 0)) }) { ruler($0) }
            toggleLine("la_scale_separate", hint: nil, checked: values[4] > 0.5) { set([4: $0 ? 1 : 0]) }
            groupLabel("la_wiggle")
            ForEach(NativeLayerAnimParam.wiggle) { ruler($0) }
            HStack(spacing: 8) {
                Text(AureaText.t("la_wiggle_seed")).font(.aurea(size: 12)).frame(maxWidth: .infinity, alignment: .leading)
                Text("\(Int(values[5]))").font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                NativePanelChip(AureaText.t("la_reroll")) { set([5: Float(Int.random(in: 1...999_999))]) }
            }.frame(height: 44)
        }.padding(8).background(AureaColors.chip.opacity(0.45), in: RoundedRectangle(cornerRadius: 10))
    }
    private func groupLabel(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 12, weight: .bold)).foregroundStyle(AureaColors.muted).padding(.top, 10).padding(.bottom, 2)
    }
    private func toggleLine(_ key: String, hint: String?, checked: Bool, onChange: @escaping (Bool) -> Void) -> some View {
        HStack {
            VStack(alignment: .leading, spacing: 1) {
                Text(AureaText.t(key)).font(.aurea(size: 12))
                if let hint { Text(AureaText.t(hint)).font(.aurea(size: 11)).foregroundStyle(AureaColors.muted) }
            }.frame(maxWidth: .infinity, alignment: .leading)
            AureaToggle(checked: checked) { onChange($0) }
        }.padding(.vertical, 4)
    }
    private func choices(_ label: String, _ keys: [String], slot: Int) -> some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 6) {
                Text(AureaText.t(label)).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                ForEach(Array(keys.enumerated()), id: \.offset) { n, key in NativePanelChip(AureaText.t(key), selected: Int(values[slot]) == n) { set([slot: Float(n)]) } }
            }.frame(height: 40)
        }
    }
    private func ruler(_ param: NativeLayerAnimParam) -> some View {
        let bit = 1 << param.id
        let look: KeyframeLook = Int(values[25]) & bit != 0 ? .keyHere : (Int(values[24]) & bit != 0 ? .animated : .none)
        // Expressão no valor do animador (TrackProperty::LayerAnimParam = 40).
        let info = model.engine.expression(id, property: 40, effect: index, param: UInt32(param.id))
        let expression: ExpressionLook = (info["exists"] as? NSNumber)?.boolValue != true ? .none
            : !((info["enabled"] as? NSNumber)?.boolValue ?? true) ? .off
            : (info["error"] as? String ?? "").isEmpty ? .ok : .error
        let track = TimelineTrack(property: 40, effect: index, param: UInt32(param.id))
        let selected = model.timelineFocus == [track]
        return VStack(spacing: 0) {
        NativePanelRuler(label: param.label, value: values[param.slot], step: param.step, range: param.range, unit: param.unit,
            decimals: param.step < 0.1 ? 2 : param.step < 1 ? 1 : 0, keypad: true, look: look, toggleKey: {
                model.focusLayerAnimator(track)
                model.engine.toggleLayerAnimKey(id, index: index, param: UInt32(param.id)); reload()
            }, expression: expression, onExpression: {
                model.expressionSheet = ExpressionRequest(layer: id, label: param.label, tracks: [ExpressionTrack(property: 40, effect: index, param: UInt32(param.id))], unit: param.unit)
            }, selected: selected, onSelect: { model.focusLayerAnimator(track) }, compactUnit: true) {
                model.focusLayerAnimator(track); model.engine.setLayerAnimParam(id, index: index, param: UInt32(param.id), value: $0); reload()
            }.accessibilityIdentifier("layer.anim.\(index).param.\(param.id)")
        if selected { NativeAnimationTrackActions(track: track, curveTag: "layer.anim.\(index).curve.\(param.id)") }
        }
    }
}
