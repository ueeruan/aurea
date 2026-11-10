import SwiftUI

var extraTextPresetNames: [String] { [AureaText.t("ios_textpreset_x_bounce"), AureaText.t("ios_textpreset_x_soft_in"), AureaText.t("ios_textpreset_x_reveal"), AureaText.t("ios_textpreset_x_slide"),
                          AureaText.t("ios_textpreset_x_fast_in"), AureaText.t("ios_textpreset_x_elastic_jump"), AureaText.t("ios_textpreset_x_word_jump"), AureaText.t("ios_textpreset_x_smooth_motion")] + ["pack_text_0", "pack_text_1", "pack_text_2", "pack_text_3", "pack_text_4", "pack_text_5", "app_text_preset_explosion", "app_text_preset_smooth_bounce"].map { AureaText.t($0) }
}
import UIKit
import UniformTypeIdentifiers

// TextPanel.kt / TextPathSection.kt. Controls are expanded in the Android order.
struct TextAppearanceControls: View {
    @EnvironmentObject private var model: AureaModel
    /// −1 tudo; 0 Texto, 1 Estilo, 2 Layout (as abas das opções de texto).
    var page = -1
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
            if page <= 0 {
            NativePanelRuler(label: AureaText.t("panel_tamanho"), value: value("size", 72), step: 0.5, range: 4...TextSizeSteps.max, unit: "px", keypad: true) { model.engine.setText(id, size: $0); refresh() }
            colorRow("panel_cor", values: components("color")) { color("color", "panel_cor") }
            // O rótulo em cima e os três botões do mesmo tamanho: com as abas à direita a linha
            // não comporta os dois lado a lado em nenhum idioma.
            Text(AureaText.t("panel_alinhamento")).font(.aurea(size: 13)).padding(.top, 8)
            PanelItemGrid(items: [("panel_esquerda", ContentIconKind.alignLeft), ("panel_centro", ContentIconKind.alignCenter), ("panel_direita", ContentIconKind.alignRight)].enumerated().map { n, entry in
                PanelItem(glyph: CupertinoGlyph.Textformat, label: AureaText.t(entry.0), tag: "text.align.\(n)", on: Int(value("alignment")) == n, contentIcon: entry.1) { model.engine.setText(id, alignment: UInt32(n)); refresh() }
            }, columns: 3).padding(.top, 6).padding(.bottom, 8)
            }
            if page == -1 || page == 1 {
            PanelSection(title: AureaText.t("panel_contorno"), glyph: CupertinoGlyph.Circle, tag: "text.sec.stroke") {
                colorRow("panel_cor_contorno", values: components("strokeColor")) { color("strokeColor", "panel_cor_contorno") }
                NativePanelRuler(label: AureaText.t("panel_largura_contorno"), value: value("strokeWidth"), step: 0.1, range: 0...60, unit: "px", keypad: true) { model.engine.setText(id, strokeWidth: $0); refresh() }
            }
            }
            if style.count >= 20 {
                if page == -1 || page == 2 { layoutSections }
                if page == -1 || page == 1 { backdropSections }
            }
            if page <= 0 { NativeTextDynamicSection() }
            if page == -1 || page == 2 { NativeTextPathSection() }
            if page == -1 { NativePanelChip(AureaText.t("la_animators")) { model.transformTab = 6; model.openPanel(.transform) } }
        }.foregroundStyle(AureaColors.text).onAppear { load() }.onChange(of: id) { _ in load() }
            .onChange(of: model.status.modelRevision) { _ in load() }
    }
    private var layoutSections: some View {
        VStack(alignment: .leading, spacing: 0) {
            PanelSection(title: AureaText.t("edt_prop_spacing"), glyph: CupertinoGlyph.ArrowLeftRight, tag: "text.sec.spacing") {
                NativePanelRuler(label: AureaText.t("text_line_spacing"), value: style[18] * 100, step: 0.5, range: 10...1000, unit: "%", keypad: true) { set(18, $0 / 100) }
                trackingRow
            }
            PanelSection(title: AureaText.t("panel_caixa"), glyph: CupertinoGlyph.Square, tag: "text.sec.box") {
                PanelItemGrid(items: [("panel_livre", CupertinoGlyph.Move), ("panel_paragrafo", CupertinoGlyph.LineHorizontal3), ("panel_tamanho_fixo", CupertinoGlyph.Square), ("panel_encolher_caber", CupertinoGlyph.FullscreenExit)].enumerated().map { n, entry in
                    PanelItem(glyph: entry.1, label: AureaText.t(entry.0), tag: "text.box.\(n)", on: Int(style[0]) == n) { set(0, Float(n)) }
                })
                if style[0] >= 1 {
                    styleRow("panel_largura_caixa", 1, 2, 10...20000)
                    if style[0] >= 2 { styleRow("panel_altura_caixa", 2, 2, 10...20000) }
                }
            }
        }
    }
    private var backdropSections: some View {
        VStack(alignment: .leading, spacing: 0) {
            PanelSection(title: AureaText.t("panel_fundo"), glyph: CupertinoGlyph.Square, tag: "text.sec.backdrop", isOn: styleBinding(3)) {
                styleColor("panel_cor", color: 4)
                styleRow("panel_margem_fundo", 8, 0.2, 0...500)
                styleRow("panel_cantos_arredondados", 9, 0.2, 0...500)
            }
            PanelSection(title: AureaText.t("panel_sombra"), glyph: CupertinoGlyph.CircleLefthalfFill, tag: "text.sec.shadow", isOn: styleBinding(10)) {
                styleColor("panel_cor", color: 11)
                styleRow("panel_distancia_x", 15, 0.2, -500...500)
                styleRow("panel_distancia_y", 16, 0.2, -500...500)
                styleRow("panel_desfoque", 17, 0.1, 0...200)
            }
        }
    }
    private func styleBinding(_ slot: Int) -> Binding<Bool> {
        Binding(get: { style.count > slot && style[slot] > 0.5 }, set: { set(slot, $0 ? 1 : 0) })
    }
    /// O losango fica no rótulo, como no Alight: tocar nele escolhe a propriedade e o ◆ da coluna marca.
    private var trackingRow: some View {
        let track = TimelineTrack(property: 29)
        let keys = (model.keyframes[id] ?? []).filter { $0.property == 29 && $0.effectIndex == UInt32.max }
        let here = keys.contains { $0.time == model.localPlayhead }
        return NativePanelRuler(label: AureaText.t("text_letter_spacing"), value: style[19], step: 0.25,
            range: -1000...1000, unit: "‰ em", keypad: true, look: here ? .keyHere : keys.isEmpty ? .none : .animated,
            selected: model.timelineFocus == [track], onSelect: { model.timelineFocus = [track] }) { set(19, $0) }
            .accessibilityIdentifier("text.tracking.key")
    }
    private func styleRow(_ key: String, _ slot: Int, _ step: Float, _ range: ClosedRange<Float>) -> some View {
        NativePanelRuler(label: AureaText.t(key), value: style[slot], step: step, range: range, unit: "px", keypad: true) { set(slot, $0) }
    }
    private func styleColor(_ key: String, color: Int) -> some View {
        HStack(spacing: 10) {
            Text(AureaText.t(key)).font(.aurea(size: 13)).frame(maxWidth: .infinity, alignment: .leading)
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

/// 0.4.6: texto dinâmico — contador e timecode (TextPanel.kt `TextDynamicSection`).
/// As marcas {#} {tc} {t} {f} no texto viram valores a cada quadro.
private struct NativeTextDynamicSection: View {
    @EnvironmentObject private var model: AureaModel
    @State private var values: [Double] = []
    private var id: Int64 { model.primarySelection ?? 0 }
    private func load() { values = model.engine.textDynamic(id).map(\.doubleValue) }
    private func set(_ slot: Int, _ value: Double) {
        guard values.count >= 6 else { return }
        var next = values; next[slot] = value
        _ = model.engine.setTextDynamic(id, values: next.map { NSNumber(value: $0) })
        model.refreshModel(force: true); load()
    }
    private func insert(_ token: String) {
        if values.count >= 6 && values[0] < 0.5 { set(0, 1) }
        let current = (model.engine.text(forLayer: id)?["content"] as? String) ?? ""
        model.engine.setText(id, content: current.isEmpty ? token : current + " " + token)
        model.refreshModel(force: true)
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            if values.count >= 6 {
                PanelSection(title: AureaText.t("text_dynamic"), glyph: CupertinoGlyph.Timer, tag: "text.sec.dynamic",
                             isOn: Binding(get: { values.count >= 6 && values[0] > 0.5 }, set: { set(0, $0 ? 1 : 0) })) {
                    PanelItemGrid(items: [("{#}", "text_token_counter", CupertinoGlyph.Repeat), ("{tc}", "text_token_timecode", CupertinoGlyph.Timer), ("{t}", "text_token_time", CupertinoGlyph.Clock), ("{f}", "text_token_frame", CupertinoGlyph.Film)].map { token, key, glyph in
                        PanelItem(glyph: glyph, label: AureaText.t(key), tag: "text.token.\(token)") { insert(token) }
                    })
                    NativePanelRuler(label: AureaText.t("text_counter_from"), value: Float(values[1]), step: 0.5, range: -1_000_000_000...1_000_000_000,
                                     decimals: Int(values[3]), keypad: true) { set(1, Double($0)) }
                    NativePanelRuler(label: AureaText.t("text_counter_to"), value: Float(values[2]), step: 0.5, range: -1_000_000_000...1_000_000_000,
                                     decimals: Int(values[3]), keypad: true) { set(2, Double($0)) }
                    NativePanelRuler(label: AureaText.t("text_counter_decimals"), value: Float(values[3]), step: 0.05, range: 0...6) { set(3, Double($0.rounded())) }
                    NativePanelToggle(AureaText.t("text_counter_grouping"), checked: values[4] > 0.5, fontSize: 13, weight: .regular) { set(4, $0 ? 1 : 0) }
                    // Curva do contador: Linear (1), Suave (5), Entrada (3), Saída (4).
                    PanelChoiceRow(label: AureaText.t("panel_curva"), options: ["panel_linear", "pn_ease_in_out", "pn_ease_in", "pn_ease_out"].map { AureaText.t($0) },
                                   isOn: { [1, 5, 3, 4][$0] == Int(values[5]) }) { set(5, Double([1, 5, 3, 4][$0])) }
                }
            }
        }.onAppear { load() }.onChange(of: id) { _ in load() }.onChange(of: model.status.modelRevision) { _ in load() }
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
        let track = TimelineTrack(property: Int(property))
        let keys = (model.keyframes[id] ?? []).filter { $0.property == property && $0.effectIndex == UInt32.max }
        let here = keys.contains { $0.time == model.localPlayhead }
        return NativePanelRuler(label: AureaText.t(key), value: value, step: 1, range: -20000...20000, unit: "px", keypad: true,
            look: here ? .keyHere : keys.isEmpty ? .none : .animated,
            selected: model.timelineFocus == [track], onSelect: { model.timelineFocus = [track] }, identifier: identifier, set: set)
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            PanelSection(title: AureaText.t("panel_texto_caminho"), glyph: CupertinoGlyph.Scribble, tag: "text.sec.path") {
                if candidates.isEmpty && ownMasks.isEmpty && !onPath {
                    Text(AureaText.t("text027_path_hint")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).fixedSize(horizontal: false, vertical: true)
                } else {
                    PanelItemRow(glyph: CupertinoGlyph.Minus, title: AureaText.t("panel_linha_reta"), tag: "text.path.none", on: !onPath) { target = 0; mask = -1; write() }
                    ForEach(Array(ownMasks.enumerated()), id: \.offset) { n, m in
                        PanelItemRow(glyph: CupertinoGlyph.SquareOnSquare, title: AureaText.t("text027_path_mask", n + 1), tag: "text.path.mask.\(n)", on: mask == Int32(m)) { target = 0; mask = Int32(m); write() }
                    }
                    ForEach(candidates) { layer in
                        PanelItemRow(glyph: CupertinoGlyph.Scribble, title: layer.name.isEmpty ? AureaText.t("panel_vetor") : layer.name, tag: "text.path.layer.\(layer.id)", on: mask < 0 && target == layer.id) { target = layer.id; mask = -1; write() }
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
            }
        }.onAppear { load() }.onChange(of: id) { _ in load() }.onChange(of: model.status.modelRevision) { _ in load() }
    }
}

// Literal control geometry shared by the TextPanel and MaskPanel source ports.
struct NativePanelChip: View {
    @Environment(\.panelItemSurface) private var surface
    let title: String
    var selected = false
    var horizontal: CGFloat = 10
    var height: CGFloat? = nil
    var radius: CGFloat = 8
    var fontSize: CGFloat = 12
    var bold = false
    /// Divide a largura da linha com os botões vizinhos.
    var fill = false
    let action: () -> Void
    init(_ title: String, selected: Bool = false, horizontal: CGFloat = 10, height: CGFloat? = nil, radius: CGFloat = 8, fontSize: CGFloat = 12, bold: Bool = false, fill: Bool = false, action: @escaping () -> Void) {
        self.title = title; self.selected = selected; self.horizontal = horizontal; self.height = height; self.radius = radius; self.fontSize = fontSize; self.bold = bold; self.action = action
        self.fill = fill
    }
    var body: some View {
        Button(action: action) {
            Text(title).font(.aurea(size: fontSize, weight: bold ? (selected ? .bold : .medium) : .regular)).foregroundStyle(selected ? AureaColors.accent : AureaColors.text).lineLimit(1)
                .padding(.horizontal, horizontal).padding(.vertical, height == nil ? 6 : 0).frame(maxWidth: fill ? .infinity : nil).frame(height: height)
                .background(selected ? AureaColors.accentDim : surface ?? AureaColors.chip, in: RoundedRectangle(cornerRadius: radius))
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
    @Environment(\.paramRowStacked) private var stacked
    private var labelChip: some View {
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
    var body: some View {
        // Em seção estreita o rótulo sobe e a régua fica com a largura toda.
        let onTop = stacked && plainLabelWidth == nil
        VStack(spacing: 0) {
        if onTop { labelChip }
        HStack(spacing: 0) {
            if let plainLabelWidth { Text(label).font(.aurea(size: 13)).frame(width: plainLabelWidth, alignment: .leading) }
            else if !onTop { labelChip }
            Color.clear.frame(width: plainLabelWidth != nil || onTop ? 0 : keypad ? 6 : 8)
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
        }.frame(height: onTop ? 44 : ParamRowDims.row)
        }.padding(.vertical, onTop ? 3 : 0).onDisappear(perform: finish)
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
    /// 0.5.0: Engine::kMotionTextPresetBase.
    static let motionTextBase: UInt32 = 1000
    /// Os do modelo Motion só no 2D; os antigos só no 3D; os salvos em ambos.
    func fitsText(threeD: Bool) -> Bool {
        guard let preset = textPreset else { return true }
        return preset >= Self.motionTextBase ? !threeD : threeD
    }
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
                // 0.5.0: os do modelo Motion primeiro (motor: base 1000); os antigos ficam para o texto 3D.
                let motionKeys = ["tp_m_type_in", "tp_m_decode", "tp_m_uncensor", "tp_m_password", "tp_m_title_spread", "tp_m_fade_up", "tp_m_wave", "tp_m_spin", "tp_m_scale_word", "tp_m_scale_out"]
                for (index, key) in motionKeys.enumerated() {
                    result.append(PanelPresetEntry(id: "motion:\(index)", name: AureaText.t(key), kind: kind, textPreset: PanelPresetEntry.motionTextBase + UInt32(index)))
                }
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
                    ForEach(presets.filter { $0.fitsText(threeD: threeD) }) { entry in
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
    /// 0.3.4 presets: a busca fica recolhida numa lupa ao lado das abas.
    @State private var searchOpen = false

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
        let textIs3D = isText && !(model.engine.text3D(forLayer: model.primarySelection ?? 0) ?? [:]).isEmpty
        return values.filter { (!isText || ($0.kind == .text && $0.fitsText(threeD: textIs3D))) && (query.isEmpty || $0.name.range(of: query, options: .caseInsensitive) != nil) }
    }

    /// 0.3.4 presets: o título "Presets" mora na barra da seção (TopBarView); o
    /// cabeçalho próprio só fica onde ela não existe (Edição e Cena), como o
    /// `alwaysHeader` do Android.
    private var ownHeader: Bool { model.sceneEditor || model.uiMode == AureaUiMode.edit }

    var body: some View {
        VStack(spacing: 0) {
            if ownHeader { PanelHeader(title: AureaText.t("panel_presets"), onBack: { model.panel = .none }) }
            VStack(spacing: 0) {
                // Cabeçalho compacto: UMA fileira de 44 (lupa + abas + "Até o fim", ou o campo de busca).
                HStack(spacing: 0) {
                    if searchOpen {
                        searchField
                    } else {
                        Button {
                            searchOpen = true
                            // O campo só existe no quadro seguinte: o foco vai depois.
                            DispatchQueue.main.async { searching = true }
                        } label: {
                            CupertinoGlyph.text(CupertinoGlyph.Search, size: 15, color: AureaColors.text)
                                .frame(width: 34, height: 32)
                                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 9))
                                .frame(width: 44, height: 44, alignment: .leading).contentShape(Rectangle())
                        }.buttonStyle(AureaPressStyle()).accessibilityLabel(AureaText.t("panel_buscar_preset"))
                            .accessibilityIdentifier("presets.searchOpen")
                        ScrollView(.horizontal, showsIndicators: false) {
                            HStack(spacing: 6) {
                                ForEach(tabs, id: \.self) { value in
                                    Button { picked = value } label: {
                                        Text(tabLabel(value)).font(.aurea(size: 12.5, weight: tab == value ? .bold : .medium)).lineLimit(1)
                                            .foregroundStyle(tab == value ? AureaColors.accent : AureaColors.text)
                                            .padding(.horizontal, 12).frame(height: 32)
                                            .background(tab == value ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 9))
                                            .frame(height: 44).contentShape(Rectangle())
                                    }.buttonStyle(AureaPressStyle()).accessibilityAddTraits(tab == value ? .isSelected : [])
                                }
                            }
                        }.accessibilityIdentifier("presets.tabs")
                        // "Durar até o fim da camada" (antes uma linha inteira com interruptor):
                        // pílula curta liga/desliga na ponta da fileira, só na aba Animação.
                        if kind == .animation { stretchChip.padding(.leading, 6) }
                    }
                }
                .frame(height: 44).padding(.top, 4)
                if entries.isEmpty && kind == nil {
                    Text(query.isEmpty ? AureaText.t(tab == "favoritos" ? "panel_toque_preset_guardar_aqui" : "panel_ultimos_10_presets_aplicados_aparecem_aqui") : AureaText.t("ios_preset_none_matching", query))
                        .font(.aurea(size: 13)).foregroundStyle(AureaColors.muted).multilineTextAlignment(.center)
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                } else {
                    // A grade enche o resto do painel e rola por dentro (baixo ou alto, a
                    // primeira fileira inteira e parte da segunda sempre aparecem).
                    ScrollView {
                        // A dica do CC rola junto com os cartões (antes empurrava a grade para fora).
                        if kind == .effects {
                            Text(AureaText.t("cc_preset_hint")).font(.aurea(size: 11.5))
                                .foregroundStyle(AureaColors.muted).frame(maxWidth: .infinity, alignment: .leading)
                                .padding(.top, 4)
                        }
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
                                        CupertinoGlyph.text(CupertinoGlyph.Plus, size: 22, color: AureaColors.accent)
                                            .frame(maxWidth: .infinity).frame(height: PresetCardMetrics.preview)
                                        Color.clear.frame(height: 4)
                                        Text(AureaText.t("panel_salvar_desta_camada")).font(.aurea(size: 11, weight: .semibold))
                                            .foregroundStyle(AureaColors.accent).multilineTextAlignment(.center)
                                            .lineLimit(2).frame(height: PresetCardMetrics.name)
                                    }.padding(5).frame(maxWidth: .infinity)
                                        .background(AureaColors.accentDim, in: RoundedRectangle(cornerRadius: 12))
                                }.buttonStyle(AureaPressStyle()).accessibilityLabel(AureaText.t("panel_salvar_camada_como_preset"))
                            }
                        }.padding(.top, 4).padding(.bottom, 12)
                    }.accessibilityIdentifier("presets.grid")
                }
            }.padding(.horizontal, 12)
        }
        .foregroundStyle(AureaColors.text).background(AureaColors.editorPanel)
        .onAppear {
            if model.selectedLayer?.kind == 4 || model.engine.text3D(forLayer: layerId) != nil { picked = "texto" }
            // "Meus presets" (vindo dos efeitos) abre direto na aba pedida.
            if let kind = model.presetsOpenKind { picked = kind; model.presetsOpenKind = nil }
            if let query = model.presetsOpenSearch { search = query; searchOpen = !query.isEmpty; model.presetsOpenSearch = nil }
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
                .accessibilityIdentifier("presets.search")
            // ✕ limpa e recolhe a busca de volta na lupa.
            Button { search = ""; searching = false; searchOpen = false } label: {
                CupertinoGlyph.text(CupertinoGlyph.XmarkCircleFill, size: 16, color: AureaColors.muted)
                    .frame(width: 44, height: 34).contentShape(Rectangle())
            }.buttonStyle(AureaPressStyle()).accessibilityLabel(AureaText.t("panel_limpar_busca"))
        }.padding(.leading, 10).frame(height: 34)
            .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
    }
    /// 0.3.4 presets: o "Durar até o fim da camada" compacto (pílula liga/desliga, alvo de 44).
    private var stretchChip: some View {
        Button { stretch.toggle() } label: {
            HStack(spacing: 5) {
                CupertinoGlyph.text(CupertinoGlyph.ArrowRightToLine, size: 13, color: stretch ? AureaColors.accent : AureaColors.muted)
                Text(AureaText.t("p34_presets_to_end")).font(.aurea(size: 12, weight: stretch ? .bold : .medium)).lineLimit(1)
                    .foregroundStyle(stretch ? AureaColors.accent : AureaColors.muted)
            }
            .padding(.horizontal, 10).frame(height: 32)
            .background(stretch ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 9))
            .frame(height: 44).contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle())
            .accessibilityLabel(AureaText.t("panel_durar_ate_fim_camada"))
            .accessibilityAddTraits(stretch ? .isSelected : [])
            .accessibilityIdentifier("presets.stretch")
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

/// 0.3.4 presets: medidas do cartão (par de `CardPreview`/`CardName` do PresetsPanel.kt):
/// prévia 50 + nome em 2 linhas (26) = 90 com o respiro, para a grade mostrar 1,5+
/// fileiras no painel baixo.
enum PresetCardMetrics {
    static let preview: CGFloat = 50
    static let name: CGFloat = 26
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
                PresetPreviewView(entry: entry, compositionWidth: compositionWidth)
                    .frame(maxWidth: .infinity).frame(height: PresetCardMetrics.preview)
                    .background(AureaColors.stage, in: RoundedRectangle(cornerRadius: 8)).clipShape(RoundedRectangle(cornerRadius: 8))
                    .overlay(alignment: .bottomLeading) {
                        // O tipo vira selo na prévia (antes uma linha a mais no cartão).
                        if showKind {
                            Text(entry.kind.label).font(.aurea(size: 9.5)).foregroundStyle(AureaColors.muted).lineLimit(1)
                                .padding(.horizontal, 4).padding(.vertical, 1)
                                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 4)).padding(4)
                        }
                    }
                Color.clear.frame(height: 4)
                Text(entry.name).font(.aurea(size: 11, weight: .semibold)).tracking(aureaTracking(-0.1)).lineLimit(2)
                    .foregroundStyle(AureaColors.text)
                    .lineSpacing(max(0, 13 - UIFont.systemFont(ofSize: 11, weight: .semibold).lineHeight))
                    .frame(maxWidth: .infinity, alignment: .topLeading).frame(height: PresetCardMetrics.name, alignment: .topLeading)
            }.padding(5).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 12))
        }.buttonStyle(AureaPressStyle()).accessibilityLabel(applyLabel ?? AureaText.t("ios_apply_named", entry.name))
            .overlay(alignment: .topTrailing) {
                Button(action: onFavorite) {
                    CupertinoGlyph.text(favorite ? CupertinoGlyph.StarFill : CupertinoGlyph.Star, size: 15,
                                        color: favorite ? AureaColors.warning : AureaColors.muted)
                        .frame(width: 34, height: 34)
                }.buttonStyle(AureaPressStyle()).padding(5).accessibilityLabel(AureaText.t(favorite ? "panel_tirar_favoritos" : "panel_favoritar"))
            }
            .overlay(alignment: .topLeading) {
                if let onDelete {
                    Button(action: onDelete) {
                        CupertinoGlyph.text(CupertinoGlyph.Trash, size: 14, color: AureaColors.danger).frame(width: 34, height: 34)
                    }.buttonStyle(AureaPressStyle()).padding(5).accessibilityLabel(AureaText.t("panel_apagar_preset"))
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
        // 0.3.4 painéis: linha de 52 (antes 40), ícone 24, régua 48 e a caixa central (par do Android).
        HStack(spacing: 6) {
            CupertinoGlyph.text(CupertinoGlyph.CircleLefthalfFill, size: 24, color: AureaColors.text)
            TickRuler(value: { opacity * 100 }, unitsPerDp: 0.35, active: true, height: 48)
                .valueDrag(enabled: !locked, start: { opacity * 100 }, unitsPerDp: { 0.35 }, min: 0, max: 100,
                           onStart: { model.beginGesture("opacidade") }, onValue: write, onEnd: { model.endGesture() })
                .accessibilityElement()
                .accessibilityLabel(AureaText.t("panel_opacidade"))
                .accessibilityValue("\(Int((opacity * 100).rounded()))%")
                .accessibilityIdentifier("blend.opacity.ruler")
            ValueBox("\(Int((opacity * 100).rounded()))%", enabled: !locked, width: PanelMetrics.fieldWidth, onTap: keypad)
                .accessibilityIdentifier("blend.opacity.value")
        }
        .frame(height: 52)
        .padding(.top, 6).padding(.leading, 6).padding(.trailing, 4)
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
                        CupertinoGlyph.text(CupertinoGlyph.ChevronRight, size: 15, color: AureaColors.muted)
                            .mirrorsInRtl(!isOpen)
                            .rotationEffect(.degrees(isOpen ? 90 : 0))
                            .animation(.easeOut(duration: 0.15), value: isOpen)
                            .frame(width: 20, height: 20)
                    } else {
                        Color.clear.frame(width: 20, height: 20)
                    }
                    // 0.3.4 painéis: título 15, modo 13, cabeçalho de 44 (antes 13,5 / 12 / 37).
                    Text(AureaText.t(cat.title)).font(.aurea(size: PanelMetrics.title, weight: .semibold)).lineLimit(1)
                    Spacer(minLength: 4)
                    if current {
                        Text(AureaText.t(Self.modes[Int(mode)])).font(.aurea(size: PanelMetrics.label)).foregroundStyle(AureaColors.accent).lineLimit(1)
                        CupertinoGlyph.text(CupertinoGlyph.CheckmarkCircleFill, size: 18, color: AureaColors.accent)
                    }
                }
                .padding(.horizontal, 8)
                .frame(height: PanelMetrics.touch)
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
                .frame(height: 96)
            }
        }
        .background(isOpen ? AureaColors.chip : AureaColors.surfaceHigh, in: RoundedRectangle(cornerRadius: 5))
        .padding(.horizontal, 6.5)
        .padding(.top, 6.5)
    }

    /// Um modo: 72 × 96 — miniatura de 54 (raio = lado/6) e o nome 12 (0.3.4 painéis; antes 60 × 80, 45, 11).
    private func modeItem(_ value: Int) -> some View {
        let lit = mode == UInt32(value)
        return Button { setMode(value) } label: {
            VStack(spacing: 4) {
                ZStack {
                    RoundedRectangle(cornerRadius: 9).fill(AureaColors.surface)
                    if value == 22 || value == 23 {
                        BlendCutThumb(exclude: value == 23, size: CGSize(width: 50, height: 50))
                    } else {
                        Canvas { context, size in
                            let r = min(size.width, size.height) * 0.30
                            context.fill(Path(ellipseIn: CGRect(x: size.width * 0.38 - r, y: size.height / 2 - r, width: r * 2, height: r * 2)), with: .color(AureaColors.blendThumbBottom))
                            context.blendMode = Self.previews[value]
                            context.fill(Path(ellipseIn: CGRect(x: size.width * 0.62 - r, y: size.height / 2 - r, width: r * 2, height: r * 2)), with: .color(AureaColors.blendThumbTop))
                        }
                        .frame(width: 50, height: 50)
                        .drawingGroup()
                    }
                }
                .frame(width: 54, height: 54)
                .clipShape(RoundedRectangle(cornerRadius: 9))
                .overlay(RoundedRectangle(cornerRadius: 9).stroke(lit ? AureaColors.accent : .clear, lineWidth: 2))
                Text(AureaText.t(Self.modes[value])).font(.aurea(size: PanelMetrics.note))
                    .foregroundStyle(lit ? AureaColors.accent : AureaColors.muted)
                    .lineLimit(1).padding(.horizontal, 2)
            }
            .frame(width: 72, height: 96)
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
    private let types = [0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26]
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
                    // Gota (0.5.0): o canto arredonda a PONTA.
                    if [0, 16, 21, 26].contains(shapeType) {
                        shapeRow(1, shapeType == 26 ? "shp_tip_radius" : "panel_raio", value: value(1), step: 0.3, range: 0...max(0, min(value(5), value(6)) / 2), unit: "px", reset: 0, gesture: "raio") { write(1, $0) }
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
        case 25: return AureaText.t("sh_shape_moon"); case 26: return AureaText.t("sh_shape_teardrop")
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
        // Lua (0.5.0): o deslocamento do círculo que recorta (25 % = a lua fina).
        case 25: percentRow(7, "shp_offset", range: 5...95, reset: 25, gesture: "deslocamento")
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
            case 25: path = ShapeGlyphPaths.moon(center: c, radius: r)
            case 26: path = ShapeGlyphPaths.teardrop(center: c, radius: r)
            default: path.addRect(CGRect(x: c.x - r, y: c.y - r, width: 2 * r, height: 2 * r))
            }
            context.fill(path, with: .color(AureaColors.text))
        }
    }
}

// MaskPanel.kt: máscaras numa tela só (três botões de criar sempre à vista,
// chips das máscaras e, da escolhida, Caminho, Modo, Borda, Seguir objeto e
// Apagar) e o recorte por outra camada na segunda aba.
// MARK: - Kit de lista (0.5.0), par do PanelList.kt

/// A organização do Motion: 37 pt por item, 5 pt de canto, seta + ícone + nome e
/// seções que abrem e fecham. Tudo que é escolha fica à vista, sem fileira cortada na borda.
struct PanelSection<Content: View>: View {
    let title: String
    let glyph: Character
    let tag: String
    var count = 0
    var flat = false
    /// O interruptor no título, como no Motion: ligar abre e desligar fecha.
    var isOn: Binding<Bool>? = nil
    /// false = rótulo, régua e número na mesma linha (painel na largura toda).
    var stacked = true
    let content: () -> Content
    @State private var open: Bool

    init(title: String, glyph: Character, tag: String, startOpen: Bool = true, count: Int = 0, flat: Bool = false, isOn: Binding<Bool>? = nil, stacked: Bool = true, @ViewBuilder content: @escaping () -> Content) {
        self.title = title; self.glyph = glyph; self.tag = tag; self.count = count; self.flat = flat; self.isOn = isOn; self.stacked = stacked; self.content = content
        _open = State(initialValue: isOn?.wrappedValue ?? startOpen)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(spacing: 0) {
                Button { withAnimation(.easeInOut(duration: 0.2)) { open.toggle() } } label: {
                    HStack(spacing: 8) {
                        CupertinoGlyph.text(open ? CupertinoGlyph.ChevronDown : CupertinoGlyph.ChevronRight, size: 14, color: AureaColors.text.opacity(open ? 0.9 : 0.3))
                        CupertinoGlyph.text(glyph, size: 18, color: AureaColors.text)
                        Text(title).font(.aurea(size: 13, weight: .semibold)).foregroundStyle(AureaColors.text).lineLimit(1)
                            .frame(maxWidth: .infinity, alignment: .leading)
                        if count > 0 { Text("\(count)").font(.aurea(size: 12)).foregroundStyle(AureaColors.muted) }
                    }.frame(height: 40).contentShape(Rectangle())
                }
                .buttonStyle(AureaPressStyle(shrink: 1))
                .accessibilityAddTraits(open ? .isSelected : [])
                .accessibilityIdentifier(tag)
                if let isOn {
                    AureaToggle(checked: isOn.wrappedValue) { value in
                        isOn.wrappedValue = value
                        withAnimation(.easeInOut(duration: 0.2)) { open = value }
                    }
                }
            }.padding(.horizontal, 8)
            if open {
                VStack(alignment: .leading, spacing: 0) { content() }
                    .padding(.horizontal, 8).padding(.bottom, 8)
                    .environment(\.paramRowStacked, stacked)
            }
        }
        .background(flat ? Color.clear : AureaColors.fxListSurface, in: RoundedRectangle(cornerRadius: 5))
        .padding(.top, flat ? 0 : 6)
        // O interruptor desligado usa a cor da seção: sobre ela, precisa escurecer para aparecer.
        .transformEnvironment(\.toggleOffTrack) { if !flat { $0 = AureaColors.fxLowSurface } }
    }
}

/// Um item que se aplica ou se escolhe: ícone, nome e o estado aceso.
struct PanelItem: Identifiable {
    let glyph: Character
    let label: String
    let tag: String
    var on = false
    var contentIcon: ContentIconKind? = nil
    let action: () -> Void
    var id: String { tag }
}

/// Dentro de um cartão escuro os itens clareiam, para não sumirem no fundo.
private struct PanelItemSurfaceKey: EnvironmentKey { static let defaultValue: Color? = nil }
extension EnvironmentValues {
    var panelItemSurface: Color? {
        get { self[PanelItemSurfaceKey.self] }
        set { self[PanelItemSurfaceKey.self] = newValue }
    }
}

/// Grade de itens com nome e ícone. Até 2 colunas, em linha; 3 ou mais, ícone sobre o nome.
struct PanelItemGrid: View {
    @Environment(\.panelItemSurface) private var surface
    let items: [PanelItem]
    var columns = 2
    @ViewBuilder private func icon(_ item: PanelItem, _ tint: Color) -> some View {
        if let kind = item.contentIcon { ContentIcon(kind: kind, size: 18, tint: tint) }
        else { CupertinoGlyph.text(item.glyph, size: 18, color: tint) }
    }
    var body: some View {
        VStack(spacing: 6) {
            ForEach(Array(stride(from: 0, to: items.count, by: columns)), id: \.self) { start in
                HStack(spacing: 6) {
                    ForEach(items[start..<min(start + columns, items.count)]) { tile($0) }
                    ForEach(0..<max(0, start + columns - items.count), id: \.self) { _ in Color.clear.frame(maxWidth: .infinity, maxHeight: 1) }
                }
            }
        }
    }
    @ViewBuilder private func tile(_ item: PanelItem) -> some View {
        let tint = item.on ? AureaColors.accent : AureaColors.text
        let stacked = columns >= 3
        Button(action: item.action) {
            Group {
                if stacked {
                    VStack(spacing: 3) {
                        icon(item, tint)
                        Text(item.label).font(.aurea(size: 11)).foregroundStyle(tint).lineLimit(2).multilineTextAlignment(.center)
                    }.padding(.horizontal, 3)
                } else {
                    HStack(spacing: 8) {
                        icon(item, tint)
                        Text(item.label).font(.aurea(size: 12)).foregroundStyle(tint).lineLimit(2).frame(maxWidth: .infinity, alignment: .leading)
                    }.padding(.horizontal, 8)
                }
            }
            .frame(maxWidth: .infinity).frame(height: stacked ? 56 : 44)
            .background(item.on ? AureaColors.accentDim : surface ?? AureaColors.fxLowSurface, in: RoundedRectangle(cornerRadius: 5))
            .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .accessibilityLabel(item.label)
        .accessibilityAddTraits(item.on ? .isSelected : [])
        .accessibilityIdentifier(item.tag)
    }
}

/// Linha inteira: ícone, nome e, à direita, o detalhe; com `chevron`, leva a outra tela.
struct PanelItemRow: View {
    @Environment(\.panelItemSurface) private var surface
    let glyph: Character
    let title: String
    let tag: String
    var detail: String? = nil
    var on = false
    var danger = false
    var chevron = false
    let action: () -> Void
    var body: some View {
        let tint = danger ? AureaColors.danger : on ? AureaColors.accent : AureaColors.text
        Button(action: action) {
            HStack(spacing: 8) {
                CupertinoGlyph.text(glyph, size: 18, color: tint)
                Text(title).font(.aurea(size: 13)).foregroundStyle(tint).lineLimit(2).frame(maxWidth: .infinity, alignment: .leading).padding(.vertical, 8)
                if let detail { Text(detail).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).lineLimit(1) }
                if chevron { CupertinoGlyph.text(CupertinoGlyph.ChevronRight, size: 12, color: AureaColors.muted).mirrorsInRtl() }
            }
            .padding(.horizontal, 8).frame(minHeight: 37)
            .background(on ? AureaColors.accentDim : surface ?? AureaColors.fxLowSurface, in: RoundedRectangle(cornerRadius: 5))
            .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .padding(.vertical, 3)
        .accessibilityAddTraits(on ? .isSelected : [])
        .accessibilityIdentifier(tag)
    }
}

/// "+ Adicionar": o último item da lista, centrado, como no Motion.
struct PanelAddRow: View {
    let label: String
    let tag: String
    let action: () -> Void
    var body: some View {
        Button(action: action) {
            HStack(spacing: 6) {
                CupertinoGlyph.text(CupertinoGlyph.Plus, size: 16, color: AureaColors.text)
                Text(label).font(.aurea(size: 13)).foregroundStyle(AureaColors.text).lineLimit(1)
            }
            .frame(maxWidth: .infinity).frame(height: 37)
            .background(AureaColors.fxLowSurface, in: RoundedRectangle(cornerRadius: 5))
            .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .padding(.vertical, 3)
        .accessibilityIdentifier(tag)
    }
}

/// Rótulo em cima e as opções embaixo, quebrando linha (par do ChipRow do Android).
struct PanelChoiceRow: View {
    let label: String
    let options: [String]
    let isOn: (Int) -> Bool
    let pick: (Int) -> Void
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(label).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
            PanelFlow(spacing: 6) {
                ForEach(Array(options.enumerated()), id: \.offset) { n, title in
                    NativePanelChip(title, selected: isOn(n)) { pick(n) }
                }
            }
        }.padding(.vertical, 4)
    }
}

/// Quebra de linha simples para as escolhas (iOS 16+; antes, empilha).
private struct PanelFlow<Content: View>: View {
    let spacing: CGFloat
    @ViewBuilder let content: () -> Content
    var body: some View {
        if #available(iOS 16.0, *) {
            PanelFlowLayout(spacing: spacing) { content() }
        } else {
            VStack(alignment: .leading, spacing: spacing) { content() }
        }
    }
}

@available(iOS 16.0, *)
private struct PanelFlowLayout: Layout {
    let spacing: CGFloat
    private func rows(_ width: CGFloat, _ subviews: Subviews) -> [(range: Range<Int>, height: CGFloat)] {
        var result: [(range: Range<Int>, height: CGFloat)] = []
        var start = 0, x: CGFloat = 0, height: CGFloat = 0
        for (i, view) in subviews.enumerated() {
            let size = view.sizeThatFits(.unspecified)
            if x > 0 && x + size.width > width {
                result.append((start..<i, height))
                start = i; x = 0; height = 0
            }
            x += size.width + spacing
            height = max(height, size.height)
        }
        if start < subviews.count { result.append((start..<subviews.count, height)) }
        return result
    }
    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let width = proposal.width ?? 320
        let all = rows(width, subviews)
        return CGSize(width: width, height: all.reduce(0) { $0 + $1.height } + spacing * CGFloat(max(0, all.count - 1)))
    }
    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        var y = bounds.minY
        for row in rows(bounds.width, subviews) {
            var x = bounds.minX
            for i in row.range {
                let size = subviews[i].sizeThatFits(.unspecified)
                subviews[i].place(at: CGPoint(x: x, y: y + (row.height - size.height) / 2), proposal: ProposedViewSize(size))
                x += size.width + spacing
            }
            y += row.height + spacing
        }
    }
}

struct MaskPanel: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var pen = EditPointsState.shared
    /// 0 Pontos (o modo de caneta, com o desenho do Editar pontos), 1 Máscaras, 2 Recorte.
    @State private var tab = 1
    @State private var matte: Int64 = 0
    @State private var mode = 0
    private var id: Int64 { model.primarySelection ?? 0 }
    private var mask: MaskItem? { model.editingMask }
    private var canKey: Bool { tab == 1 && mask?.closed == true && !model.maskDrawing }
    private var look: KeyframeLook { mask?.keyed == true ? .keyHere : (mask?.keyCount ?? 0) > 0 ? .animated : .none }
    private var candidates: [LayerItem] { model.layers.filter { $0.id != id && ![3, 8, 9].contains($0.kind) } }
    private func load() {
        let state = model.engine.trackMatte(id)
        matte = state.first?.int64Value ?? 0; mode = state.count > 1 ? state[1].intValue : 0
    }
    private func write(_ target: Int64, _ value: Int) {
        model.engine.setTrackMatteForLayer(id, matte: target, mode: UInt32(value)); model.refreshModel(force: true); load()
    }
    /// O modo de caneta usa o Editar pontos sobre o caminho desta máscara.
    private func syncPen() {
        let id = tab == 0 ? mask?.id : nil
        guard pen.mask != id else { return }
        pen.mask = id
        pen.reset()
        if id != nil, let point = model.selectedMaskPoint { pen.choosePoint(point) }
    }
    var body: some View {
        let editing = tab == 0 ? mask : nil
        let path = editing == nil ? nil : EPPath.mask(model)
        VStack(spacing: 0) {
            PanelHeader(title: AureaText.t("panel_mascara_recorte")) { model.panel = .none }
            HStack(spacing: 0) {
                if let editing {
                    MaskPointsColumn(mask: editing, path: path, look: look, onKey: toggleKey).frame(width: 60)
                    AureaColors.contentStrongLine.frame(width: 0.7)
                } else {
                    LeftRail(keyframeLook: look, onKeyframe: canKey ? toggleKey : nil,
                             onCurve: curveKeys.count >= 2 && tab == 1 ? { openCurve() } : nil, onBack: { model.panel = .none })
                }
                VStack(spacing: 0) {
                    // Na tela, como no Motion: primeiro a lista do que existe.
                    ParamTabs([AureaText.t("panel_mascaras"), AureaText.t("pn_mask_points"), AureaText.t("ui_mask_tab_matte")], selected: tab == 1 ? 0 : tab == 0 ? 1 : 2) { tab = $0 == 0 ? 1 : $0 == 1 ? 0 : 2 }
                    if editing != nil {
                        if model.masks.count > 1 { maskChips.padding(.horizontal, 6) }
                        PointsTrackpad(path: path).padding(6)
                    } else {
                        ScrollView {
                            VStack(alignment: .leading, spacing: 0) {
                                if tab == 2 { matteBody } else { masksBody }
                            }.padding(.leading, 4).padding(.trailing, 10).padding(.bottom, 16)
                        }.accessibilityIdentifier("mask.scroll")
                    }
                }
                if editing != nil {
                    AureaColors.contentStrongLine.frame(width: 0.7)
                    PointsStrip(path: path).frame(width: 60)
                }
            }
        }.foregroundStyle(AureaColors.text).onAppear { load(); syncPen() }
            .onChange(of: tab) { _ in syncPen() }
            .onChange(of: model.selectedMask) { _ in syncPen() }
            // A escolha da faixa vale no palco, e a do palco vale na faixa.
            .onChange(of: pen.index) { i in if pen.mask != nil { model.selectedMaskPoint = i >= 0 ? i : nil } }
            .onChange(of: model.selectedMaskPoint) { point in
                if pen.mask != nil, (point ?? -1) != pen.index { pen.choosePoint(point ?? -1) }
            }
            .onDisappear { pen.mask = nil; pen.reset() }
            .onChange(of: id) { _ in load() }
            .onChange(of: model.status.modelRevision) { _ in load() }
            .onChange(of: model.timelineFocus) { focus in
                if focus?.contains(where: { ($0.property == 43 || $0.property == 49) && $0.effect == mask?.id }) == true, tab == 2 { tab = 1 }
            }
            // Os keyframes do caminho aparecem na camada enquanto a máscara está aberta aqui.
            .onAppear { focusPath() }
            .onChange(of: model.selectedMask) { _ in focusPath() }
            .onChange(of: tab) { _ in focusPath() }
            .onDisappear { model.timelineFocus = nil }
    }
    private func focusPath() {
        if model.timelineFocus?.contains(where: { $0.property == 43 && $0.effect == mask?.id }) == true { return }
        model.timelineFocus = tab != 2 ? mask.map { [TimelineTrack(property: 49, effect: $0.id)] } : nil
    }
    private var curveKeys: [KeyframeItem] {
        guard let mask else { return [] }
        return (model.keyframes[id] ?? []).filter { Int($0.property) == 49 && $0.effectIndex == mask.id }.sorted { $0.time < $1.time }
    }
    private func openCurve() {
        guard let first = curveKeys.first else { return }
        let segment = curveKeys.dropLast().last { $0.time <= model.localPlayhead } ?? curveKeys[0]
        model.openCurve(property: first.property, effect: first.effectIndex, param: first.paramIndex, time: segment.time)
    }
    @ViewBuilder private var masksBody: some View {
        Color.clear.frame(height: 6)
        // Os três jeitos de criar uma máscara: sempre à vista, um toque cada.
        HStack(spacing: 6) {
            MaskShapeTool(kind: 0, label: AureaText.t("panel_desenhar_mao"), selected: model.maskDrawing && mask?.closed == false) { model.addMask(2); tab = 0 }
                .accessibilityIdentifier("mask.add.draw")
            MaskShapeTool(kind: 1, label: AureaText.t("panel_retangulo"), selected: false) { model.addMask(0); tab = 0 }
                .accessibilityIdentifier("mask.add.rect")
            MaskShapeTool(kind: 2, label: AureaText.t("panel_elipse"), selected: false) { model.addMask(1); tab = 0 }
                .accessibilityIdentifier("mask.add.ellipse")
        }
        if model.masks.isEmpty {
            Color.clear.frame(height: 10)
            hint("panel_mascara_mostra_so_parte_camada_escolha")
        } else {
            PanelSection(title: AureaText.t("panel_mascaras"), glyph: CupertinoGlyph.RectangleStack, tag: "mask.sec.list", count: model.masks.count) {
                ForEach(Array(model.masks.enumerated()), id: \.element.id) { entry in
                    let open = tab == 1 && mask?.id == entry.element.id
                    VStack(alignment: .leading, spacing: 0) {
                        maskRow(entry.offset, entry.element, open: open)
                        if open, let mask { VStack(alignment: .leading, spacing: 0) { maskEditor(mask) }.padding(.horizontal, 8).padding(.bottom, 8).environment(\.panelItemSurface, AureaColors.fxListSurface).environment(\.toggleOffTrack, nil) }
                    }
                    .background(AureaColors.fxLowSurface, in: RoundedRectangle(cornerRadius: 5))
                    .padding(.vertical, 3)
                }
            }
        }
    }
    /// A linha da máscara: seta, ícone, nome e o modo em que ela está.
    private func maskRow(_ index: Int, _ item: MaskItem, open: Bool) -> some View {
        Button {
            model.selectedMask = open ? nil : item.id
            model.selectedMaskPoint = nil
            model.maskDrawing = !open && !item.closed
        } label: {
            HStack(spacing: 8) {
                CupertinoGlyph.text(open ? CupertinoGlyph.ChevronDown : CupertinoGlyph.ChevronRight, size: 14, color: AureaColors.text.opacity(open ? 0.9 : 0.3))
                CupertinoGlyph.text(CupertinoGlyph.SquareOnSquare, size: 18, color: AureaColors.text)
                Text(AureaText.t(item.closed ? "pn_mask_n" : "pn_mask_n_open", index + 1)).font(.aurea(size: 13, weight: open ? .bold : .semibold))
                    .foregroundStyle(AureaColors.text).lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
                if let mode = maskModes.first(where: { $0.op == item.operation }) {
                    Text(AureaText.t(mode.key)).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).lineLimit(1)
                }
            }.padding(.horizontal, 8).frame(height: 37).contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .accessibilityIdentifier("mask.item.\(index)")
    }
    private var maskModes: [(op: UInt32, key: String, glyph: Character)] { [
        (0, "panel_somar", CupertinoGlyph.Plus), (1, "panel_subtrair", CupertinoGlyph.Minus), (2, "panel_intersecao", CupertinoGlyph.SquareOnSquare),
        (3, "panel_diferenca", CupertinoGlyph.CircleLefthalfFill), (4, "panel_desligada", CupertinoGlyph.EyeSlash),
    ] }
    private var maskChips: some View {
    ScrollView(.horizontal, showsIndicators: false) {
        HStack(spacing: 6) {
            ForEach(Array(model.masks.enumerated()), id: \.element.id) { entry in
                NativePanelChip(AureaText.t(entry.element.closed ? "pn_mask_n" : "pn_mask_n_open", entry.offset + 1), selected: mask?.id == entry.element.id, horizontal: 12, height: 36, radius: 9, fontSize: 12.5) {
                    model.selectedMask = entry.element.id; model.selectedMaskPoint = nil; model.maskDrawing = !entry.element.closed
                }
            }
        }.frame(height: 44)
    }
    }
    /// A máscara aberta na lista, separada em Pontos, Modo, Borda e Seguir objeto.
    @ViewBuilder private func maskEditor(_ m: MaskItem) -> some View {
        let pointsLabel = AureaText.plural("pn_mask_points_so_far", m.points.count / 6)
        if model.maskDrawing {
            hint("panel_toque_palco_pontos_arraste_curvar_toque")
            Color.clear.frame(height: 6)
            PanelItemRow(glyph: CupertinoGlyph.CheckmarkCircle, title: AureaText.t("panel_fechar_caminho"), tag: "mask.close", detail: pointsLabel) {
                if m.points.count >= 18 { model.setMaskPoints(m.points, closed: true); model.maskDrawing = false }
                else { model.toast = AureaText.t("msg_ponha_pelo_menos_3_pontos") }
            }
            // Sem caminho fechado ainda não há modo/borda para ajustar.
            deleteAction(m)
        } else {
            PanelItemRow(glyph: CupertinoGlyph.Pencil, title: AureaText.t("ui_edit_points"), tag: "mask.edit.points", detail: pointsLabel, chevron: true) { tab = 0 }
            VStack(alignment: .leading, spacing: 4) {
                PropertyLabelChip(AureaText.t("panel_caminho"), expression: .none, keyframe: look, onTap: { toggleKey() })
                Text(m.keyCount == 0 ? AureaText.t("panel_parado_toque_trilho_animar") : AureaText.plural("pn_keyframes_edit_at_playhead", Int(m.keyCount)))
                    .font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).frame(maxWidth: .infinity, alignment: .leading)
            }.padding(.vertical, 3).aureaSelectedProperty(AureaText.t("panel_caminho"))
            PanelSection(title: AureaText.t("panel_mask_mode"), glyph: CupertinoGlyph.SquareStack3dUp, tag: "mask.sec.mode", flat: true) {
                PanelItemGrid(items: maskModes.map { mode in
                    PanelItem(glyph: mode.glyph, label: AureaText.t(mode.key), tag: "mask.mode.\(mode.op)", on: mode.op == m.operation) { update(m, operation: mode.op) }
                }, columns: 3)
                NativePanelToggle(AureaText.t("panel_inverter_mostrar_lado_fora"), checked: m.inverted) { update(m, inverted: $0) }
            }
            PanelSection(title: AureaText.t("panel_mask_edge"), glyph: CupertinoGlyph.Drop, tag: "mask.sec.edge", flat: true) {
                maskRuler(m, param: 0, label: "panel_suavizar", value: m.feather, range: 0...500, unit: "px", reset: 0)
                maskRuler(m, param: 1, label: "panel_expandir", value: m.expansion, range: -500...500, unit: "px", reset: 0)
                maskRuler(m, param: 2, label: "panel_opacidade", value: m.opacity * 100, range: 0...100, unit: "%", reset: 100)
            }
            if model.selectedLayer?.kind == 1 {
                PanelSection(title: AureaText.t("panel_mask_follow"), glyph: CupertinoGlyph.CameraViewfinder, tag: "mask.sec.follow", startOpen: false, flat: true) {
                    if model.importingMedia { hint("panel_rastreando") }
                    else {
                        hint("panel_mascara_segue_esta_embaixo_dela_cabecote")
                        Color.clear.frame(height: 6)
                        PanelItemRow(glyph: CupertinoGlyph.Move, title: AureaText.t("panel_seguir_posicao"), tag: "mask.follow.0") { track(m.id, mode: 0) }
                        PanelItemRow(glyph: CupertinoGlyph.ArrowCounterclockwise, title: AureaText.t("panel_seguir_posicao_tamanho_giro"), tag: "mask.follow.1") { track(m.id, mode: 1) }
                    }
                }
            }
            deleteAction(m)
        }
    }
    private func deleteAction(_ m: MaskItem) -> some View {
        PanelItemRow(glyph: CupertinoGlyph.Trash, title: AureaText.t("panel_apagar_mascara"), tag: "mask.delete", danger: true) {
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
            PanelSection(title: AureaText.t("panel_1_recortar_pelo"), glyph: CupertinoGlyph.Crop, tag: "matte.sec.mode") {
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
                Color.clear.frame(height: 6)
                PanelItemGrid(items: matteModes.map { value, key, glyph in
                    PanelItem(glyph: glyph, label: AureaText.t(key), tag: "matte.mode.\(value)", on: value == mode) { pickMatteMode(value) }
                })
            }
            PanelSection(title: AureaText.t("panel_2_qual_camada_recorta"), glyph: CupertinoGlyph.RectangleStack, tag: "matte.sec.layer", count: candidates.count) {
                if candidates.isEmpty { hint("panel_nao_ha_outra_camada_possa_recortar") }
                ForEach(Array(candidates.enumerated()), id: \.element.id) { n, layer in
                    PanelItemRow(glyph: layerKindGlyph(Int(layer.kind)), title: layer.name, tag: "matte.layer.\(n)", on: matte == layer.id) { write(layer.id, mode == 0 ? 1 : mode) }
                }
                Color.clear.frame(height: 4)
                hint(matte != 0 ? "panel_camada_escolhida_recorta_esta_some_tela" : "panel_pela_forma_aparece_onde_outra_camada")
            }
        }
    }
    private var matteModes: [(Int, String, Character)] { [
        (0, "panel_nao_recortar", CupertinoGlyph.XmarkCircle), (1, "panel_pela_forma", CupertinoGlyph.Circle), (2, "panel_pela_forma_invertido", CupertinoGlyph.CircleFill),
        (3, "panel_pelo_brilho", CupertinoGlyph.Lightbulb), (4, "panel_pelo_brilho_invertido", CupertinoGlyph.CircleLefthalfFill),
    ] }
    private func pickMatteMode(_ value: Int) {
        if value == 0 { write(0, 0); return }
        // Sem camada escolhida: a logo ABAIXO na pilha (a lista vem com a
        // frente primeiro, `Engine::query_layers`). O motor usa a camada que
        // a UI passar; os textos do painel dizem "abaixo" (par do Android, P1-7).
        let own = model.layers.firstIndex { $0.id == id } ?? 0
        let below = model.layers.dropFirst(own + 1).first { item in candidates.contains { $0.id == item.id } }
        let target = matte != 0 ? matte : (below?.id ?? 0)
        if target == 0 { model.toast = AureaText.t("pn_mask_pick_layer_below") } else { write(target, value) }
    }
    private func layerKindGlyph(_ kind: Int) -> Character {
        switch kind {
        case 1: return CupertinoGlyph.VideocamFill
        case 2: return CupertinoGlyph.PhotoFill
        case 4: return CupertinoGlyph.Textformat
        case 5: return CupertinoGlyph.Circle
        default: return CupertinoGlyph.RectangleStack
        }
    }
    private func title(_ key: String) -> some View { Text(AureaText.t(key)).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted).padding(.top, 8).padding(.bottom, 4) }
    private func hint(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 12))
            .lineSpacing(max(0, 16 - (UIFont(name: "Roboto-Regular", size: 12)?.lineHeight ?? 12)))
            .foregroundStyle(AureaColors.muted).fixedSize(horizontal: false, vertical: true)
    }
    private func toggleKey() {
        guard let mask else { return }
        // Pontos escolhidos no laço: keyframe só deles (como no Motion).
        let chosen = EditPointsState.shared.multi
        if chosen.isEmpty { _ = model.engine.toggleMaskKey(id, mask: mask.id) }
        else { _ = model.engine.toggleMaskPathPointsKey(id, mask: mask.id, points: chosen.sorted().map { NSNumber(value: $0) }) }
        model.refreshModel(force: true)
    }
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
            // 0.3.4: o traço fino do estilo dos ícones (AureaIconStyle.line na caixa de 24).
            ctx.stroke(path, with: .color(color), style: StrokeStyle(lineWidth: AureaIconStyle.line * w / 24, lineCap: .round, lineJoin: .round))
            let r = w * 0.09
            ctx.fill(Path(CGRect(x: w * 0.06 - r, y: w * 0.82 - r, width: r * 2, height: r * 2)), with: .color(color))
            ctx.fill(Path(CGRect(x: w * 0.94 - r, y: w * 0.2 - r, width: r * 2, height: r * 2)), with: .color(color))
        default:
            let box = CGRect(x: w * 0.1, y: w * 0.2, width: w * 0.8, height: w * 0.6)
            ctx.stroke(kind == 2 ? Path(ellipseIn: box) : Path(box), with: .color(color),
                       style: StrokeStyle(lineWidth: AureaIconStyle.line * w / 24, lineJoin: .round))
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

/// O ANIMADOR DE TEXTO, único. A lista mostra o que está aplicado e "+ Adicionar"; abrir um
/// animador troca a lista pelo editor, na largura toda, com as seções recolhidas do Transformar
/// texto do Motion. Par do TextAnimSection.kt.
struct TextAnimationSection: View {
    var showAnimatorEffect = true
    var beforeAnimation: () -> Void = {}
    var onEditing: (Bool) -> Void = { _ in }
    @EnvironmentObject private var model: AureaModel
    @State private var animators: [[Float]] = []
    @State private var editing = -1
    @State private var animationError = false
    private var id: Int64 { model.primarySelection ?? 0 }
    private var presets: [(key: String, glyph: Character)] { [
        ("tp_m_type_in", CupertinoGlyph.Pencil), ("tp_m_decode", CupertinoGlyph.LockOpen), ("tp_m_uncensor", CupertinoGlyph.Eye), ("tp_m_password", CupertinoGlyph.Lock),
        ("tp_m_title_spread", CupertinoGlyph.ArrowLeftRight), ("tp_m_fade_up", CupertinoGlyph.ArrowUp), ("tp_m_wave", CupertinoGlyph.Waveform),
        ("tp_m_spin", CupertinoGlyph.ArrowCounterclockwise), ("tp_m_scale_word", CupertinoGlyph.Fullscreen), ("tp_m_scale_out", CupertinoGlyph.FullscreenExit),
    ] }
    private func load() {
        let flat = model.engine.textAnimators(id).map(\.floatValue)
        animators = stride(from: 0, to: flat.count - flat.count % 40, by: 40).map { Array(flat[$0..<($0 + 40)]) }
    }
    private func refresh() { model.refreshModel(force: true); load() }
    private var open: Bool { animators.indices.contains(editing) }
    private static func glyph(_ props: Int) -> Character {
        if props & (1 << 0) != 0 { return CupertinoGlyph.Move }
        if props & (1 << 1) != 0 { return CupertinoGlyph.Fullscreen }
        if props & (1 << 2) != 0 { return CupertinoGlyph.ArrowCounterclockwise }
        if props & (1 << 3) != 0 { return CupertinoGlyph.CircleLefthalfFill }
        if props & (1 << 5) != 0 { return CupertinoGlyph.Drop }
        if props & (1 << 9) != 0 { return CupertinoGlyph.Paintbrush }
        return CupertinoGlyph.Sparkles
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            if open {
                TextAnimatorEditor(index: editing, values: animators[editing], reload: refresh) { editing = -1 }
            } else {
                PanelSection(title: AureaText.t("ui_text_animators"), glyph: CupertinoGlyph.Sparkles, tag: "text.sec.animators", count: animators.count) {
                    ForEach(animators.indices, id: \.self) { row($0) }
                    PanelAddRow(label: AureaText.t("la_add"), tag: "anim.add") {
                        beforeAnimation()
                        let created = model.engine.addTextAnimator(id, props: 0b1111)
                        refresh()
                        if created >= 0 { editing = created }
                    }
                }
                PanelSection(title: AureaText.t("ui_ready_anims"), glyph: CupertinoGlyph.WandStars, tag: "text.sec.presets") {
                    PanelItemGrid(items: presets.enumerated().map { n, preset in
                        PanelItem(glyph: preset.glyph, label: AureaText.t(preset.key), tag: "text.motion.preset.\(n)") {
                            beforeAnimation()
                            animationError = !model.engine.applyMotionTextPreset(id, preset: UInt32(n))
                            refresh()
                        }
                    })
                }
                // 0.4.4 Explodir texto (só camada de texto 2D): cada pedaço vira uma camada.
                if model.selectedLayer?.kind == 4 {
                    PanelSection(title: AureaText.t("tx044_explode"), glyph: CupertinoGlyph.Scissors, tag: "text.sec.explode", startOpen: false) {
                        Text(AureaText.t("tx044_explode_hint")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).fixedSize(horizontal: false, vertical: true).padding(.bottom, 6)
                        PanelItemGrid(items: [(CupertinoGlyph.Textformat, "tx044_explode_chars"), (CupertinoGlyph.DocText, "tx044_explode_words"), (CupertinoGlyph.LineHorizontal3, "tx044_explode_lines")].enumerated().map { mode, entry in
                            PanelItem(glyph: entry.0, label: AureaText.t(entry.1), tag: "text.explode.\(mode)") {
                                beforeAnimation()
                                let made = model.engine.explodeText(id, mode: UInt32(mode), keepOriginal: false)
                                model.toast = made > 0 ? AureaText.t("tx044_explode_done", Int(made)) : AureaText.t("tx044_explode_fail")
                                refresh()
                            }
                        }, columns: 3)
                    }
                }
            }
        }.foregroundStyle(AureaColors.text).onAppear { load(); onEditing(open) }
            .onChange(of: id) { _ in editing = -1; load() }
            .onChange(of: model.status.modelRevision) { _ in load() }.onChange(of: model.status.playhead) { _ in load() }
            .onChange(of: open) { onEditing($0) }
            .alert(AureaText.t("ios_text_anim_title"), isPresented: $animationError) {
                Button(AureaText.t("pn_ok"), role: .cancel) {}
            } message: {
                Text(AureaText.t("ios_text_anim_failed"))
            }
    }
    private func row(_ index: Int) -> some View {
        let v = animators[index], on = v[0] > 0.5
        return HStack(spacing: 0) {
            Button { editing = index } label: {
                HStack(spacing: 8) {
                    CupertinoGlyph.text(Self.glyph(Int(v[1])), size: 18, color: on ? AureaColors.text : AureaColors.muted)
                    Text(AureaText.t("edt_animation_n", index + 1)).font(.aurea(size: 13, weight: .semibold))
                        .foregroundStyle(on ? AureaColors.text : AureaColors.muted).lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
                }.frame(height: 37).contentShape(Rectangle())
            }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityIdentifier("anim.item.\(index)")
            Button { setEnabled(index, !on) } label: {
                CupertinoGlyph.text(on ? CupertinoGlyph.Eye : CupertinoGlyph.EyeSlash, size: 18, color: on ? AureaColors.text : AureaColors.muted).frame(width: 34, height: 37).contentShape(Rectangle())
            }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityIdentifier("anim.item.\(index).eye")
            Button { model.engine.removeTextAnimator(id, index: UInt32(index)); refresh() } label: {
                CupertinoGlyph.text(CupertinoGlyph.Trash, size: 18, color: AureaColors.fxListIcon).frame(width: 34, height: 37).contentShape(Rectangle())
            }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityLabel(AureaText.t("panel_remover")).accessibilityIdentifier("anim.item.\(index).delete")
        }
        .padding(.horizontal, 6).frame(height: 37)
        .background(AureaColors.fxLowSurface, in: RoundedRectangle(cornerRadius: 5))
        .padding(.vertical, 3)
    }
    private func setEnabled(_ index: Int, _ on: Bool) {
        var next = animators[index]
        next[0] = on ? 1 : 0
        _ = model.engine.setTextAnimator(id, index: UInt32(index), values: next.map { NSNumber(value: $0) })
        refresh()
    }
}

private struct TextKeyParam {
    let key: Int
    let label: String
    let unit: String
    let step: Float
    let range: ClosedRange<Float>
}

/// O editor do animador, na largura toda. Par do AnimatorEditor do TextAnimSection.kt.
private struct TextAnimatorEditor: View {
    @EnvironmentObject private var model: AureaModel
    let index: Int
    let values: [Float]
    let reload: () -> Void
    let onClose: () -> Void
    private var id: Int64 { model.primarySelection ?? 0 }
    private var fields: [Float] { model.engine.textAnimatorFields(id, index: UInt32(index)).map(\.floatValue) }
    private func selectorField(_ s: Int, _ f: Int) -> Int { 16 * (s + 1) + f }
    private func setField(_ field: Int, _ value: Float) {
        _ = model.engine.setTextAnimatorField(id, index: UInt32(index), field: UInt32(field), value: value)
        reload()
    }
    private func param(_ key: Int, _ label: String, _ unit: String, _ step: Float, _ range: ClosedRange<Float>) -> TextKeyParam {
        TextKeyParam(key: key, label: AureaText.t(label), unit: unit, step: step, range: range)
    }
    private func row(_ p: TextKeyParam) -> some View {
        let state = model.engine.textAnimValues(id, index: UInt32(index), keys: [NSNumber(value: p.key)]).map(\.floatValue)
        let value = state.first ?? 0, flags = state.count > 1 ? Int(state[1]) : 0
        let look: KeyframeLook = flags & 2 != 0 ? .keyHere : flags & 1 != 0 ? .animated : .none
        let track = TimelineTrack(property: 33, effect: UInt32(index), param: UInt32(p.key))
        let info = model.engine.expression(id, property: 33, effect: UInt32(index), param: UInt32(p.key))
        let expression: ExpressionLook = (info["exists"] as? NSNumber)?.boolValue != true ? .none
            : !((info["enabled"] as? NSNumber)?.boolValue ?? true) ? .off
            : (info["error"] as? String ?? "").isEmpty ? .ok : .error
        return NativePanelRuler(label: p.label, value: value, step: p.step, range: p.range, unit: p.unit, decimals: p.step < 0.5 ? 1 : 0, keypad: true,
            look: look, expression: expression, onExpression: {
                model.expressionSheet = ExpressionRequest(layer: id, label: p.label, tracks: [ExpressionTrack(property: 33, effect: UInt32(index), param: UInt32(p.key))], unit: p.unit)
            }, selected: model.timelineFocus == [track], onSelect: { model.timelineFocus = [track] }, compactUnit: true, identifier: "anim.\(index).key.\(p.key)") {
                model.engine.setTextAnimParam(id, index: UInt32(index), param: UInt32(p.key), value: $0)
                reload()
            }
    }
    private func choice(_ label: String, _ keys: [String], field: Int, current: Int) -> some View {
        PanelChoiceRow(label: AureaText.t(label), options: keys.map { AureaText.t($0) }, isOn: { $0 == current }) { setField(field, Float($0)) }
    }
    private func toggle(_ key: String, _ checked: Bool, _ set: @escaping (Bool) -> Void) -> some View {
        NativePanelToggle(AureaText.t(key), checked: checked, fontSize: 13, weight: .regular, set: set)
    }
    private func color(_ key: String, base: Int) -> some View {
        let slot = base == 6 ? 28 : 32
        let rgba = [values[slot], values[slot + 1], values[slot + 2], 1]
        let title = AureaText.t(key)
        return HStack {
            Text(title).font(.aurea(size: 13)); Spacer()
            NativePanelColorWell(values: rgba) {
                model.colorSheet = ColorSheetRequest(title: title, initial: rgba, onChange: { r, g, b, _ in
                    for (n, c) in [r, g, b].enumerated() { model.engine.setTextAnimParam(id, index: UInt32(index), param: UInt32(base + n), value: c) }
                    reload()
                }, onDone: {})
            }
        }.frame(height: 44)
    }
    var body: some View {
        let f = fields
        if f.count >= 32 { editor(f) }
    }
    @ViewBuilder private func editor(_ f: [Float]) -> some View {
        let props = Int(f[0]), rig = f[1] > 0.5, tag = "anim.\(index)", on = values[0] > 0.5
        let toggleProp: (Int) -> Void = { setField(0, Float(props ^ $0)) }
        VStack(alignment: .leading, spacing: 0) {
            HStack(spacing: 0) {
                Button(action: onClose) {
                    HStack(spacing: 8) {
                        CupertinoGlyph.text(CupertinoGlyph.ChevronLeft, size: 18, color: AureaColors.text).mirrorsInRtl()
                        Text(AureaText.t("edt_animation_n", index + 1)).font(.aurea(size: 14, weight: .bold)).lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
                    }.frame(height: 40).contentShape(Rectangle())
                }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityIdentifier("\(tag).back")
                Button {
                    var next = values; next[0] = on ? 0 : 1
                    _ = model.engine.setTextAnimator(id, index: UInt32(index), values: next.map { NSNumber(value: $0) }); reload()
                } label: {
                    CupertinoGlyph.text(on ? CupertinoGlyph.Eye : CupertinoGlyph.EyeSlash, size: 18, color: on ? AureaColors.text : AureaColors.muted).frame(width: 36, height: 40).contentShape(Rectangle())
                }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityIdentifier("\(tag).eye")
                Button { _ = model.engine.duplicateTextAnimator(id, index: UInt32(index)); reload() } label: {
                    CupertinoGlyph.text(CupertinoGlyph.PlusSquareOnSquare, size: 18, color: AureaColors.fxListIcon).frame(width: 36, height: 40).contentShape(Rectangle())
                }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityIdentifier("\(tag).duplicate")
                Button { model.engine.removeTextAnimator(id, index: UInt32(index)); onClose(); reload() } label: {
                    CupertinoGlyph.text(CupertinoGlyph.Trash, size: 18, color: AureaColors.fxListIcon).frame(width: 36, height: 40).contentShape(Rectangle())
                }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityLabel(AureaText.t("panel_remover")).accessibilityIdentifier("\(tag).delete")
            }
            PanelSection(title: AureaText.t("fxs_range"), glyph: CupertinoGlyph.ArrowLeftRight, tag: "\(tag).sec.range", startOpen: false, stacked: false) {
                row(param(0, "panel_inicio", "%", 0.5, -1000...1000)); row(param(1, "panel_fim", "%", 0.5, -1000...1000)); row(param(2, "fxl_offset", "%", 0.5, -1000...1000))
            }
            PanelSection(title: AureaText.t("fxs_transform"), glyph: CupertinoGlyph.Move, tag: "\(tag).sec.transform", startOpen: false, stacked: false) {
                transformRows(f, props: props, rig: rig, toggleProp: toggleProp)
            }
            if !rig {
                PanelSection(title: AureaText.t("fxl_anchor_point_grouping"), glyph: CupertinoGlyph.Move, tag: "\(tag).sec.grouping", startOpen: false, stacked: false) {
                    choice("fxl_anchor_point_grouping", ["panel_letra", "panel_palavra", "panel_linha", "edt_cmd_all"], field: 3, current: Int(f[3]))
                    row(param(56, "fxl_group_align_x", "%", 0.5, -100...100)); row(param(57, "fxl_group_align_y", "%", 0.5, -100...100))
                }
            }
            PanelSection(title: AureaText.t("fxs_visual_attrs"), glyph: CupertinoGlyph.Paintbrush, tag: "\(tag).sec.visual", startOpen: false, stacked: false) {
                toggle("fxl_override_fill_color", props & (1 << 9) != 0) { _ in toggleProp(1 << 9) }
                if props & (1 << 9) != 0 { color("panel_cor", base: 6) }
                if !rig {
                    toggle("panel_cor_contorno", props & (1 << 10) != 0) { _ in toggleProp(1 << 10) }
                    if props & (1 << 10) != 0 { color("panel_cor_contorno", base: 29) }
                    toggle("ui_fill_hsb", props & (1 << 13) != 0) { _ in toggleProp(1 << 13) }
                    if props & (1 << 13) != 0 {
                        row(param(46, "fx_matiz", "°", 1, -3600...3600)); row(param(47, "fx_saturacao", "%", 0.5, -100...100)); row(param(48, "fx_brilho", "%", 0.5, -100...100))
                    }
                }
            }
            PanelSection(title: AureaText.t("fxs_interp_ease"), glyph: CupertinoGlyph.ArrowUpArrowDown, tag: "\(tag).sec.ease", startOpen: false, stacked: false) {
                choice("fxl_shape", rig ? ["fxo_square_2", "fxo_smooth", "fxo_triangle"] : ["fxo_square_2", "panel_sobe", "panel_desce", "fxo_triangle", "panel_redondo", "fxo_smooth"], field: selectorField(0, 5), current: Int(f[selectorField(0, 5)]))
                if !rig { row(param(32, "fx_suavidade", "%", 0.5, 0...100)) }
                row(param(4, rig ? "fxl_ease_in" : "fxl_ease_high", "%", 0.5, -100...100)); row(param(5, rig ? "fxl_ease_out" : "fxl_ease_low", "%", 0.5, -100...100))
                if rig { row(param(41, "fxl_overlap", "%", 0.5, 0...1000)) }
            }
            PanelSection(title: AureaText.t("fxl_random_order"), glyph: CupertinoGlyph.Arrow2Squarepath, tag: "\(tag).sec.random", startOpen: false, stacked: false) {
                toggle("fxl_random_order", f[selectorField(0, 6)] > 0.5) { setField(selectorField(0, 6), $0 ? 1 : 0) }
                HStack(spacing: 8) {
                    Text(AureaText.t("fxl_seed")).font(.aurea(size: 13)).frame(maxWidth: .infinity, alignment: .leading)
                    Text("\(Int(f[selectorField(0, 7)]))").font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                    NativePanelChip(AureaText.t("la_reroll")) { setField(selectorField(0, 7), Float(Int.random(in: 1...999_999))) }
                }.frame(height: 44)
            }
            if !rig { selectors(f, tag: tag) }
        }
    }
    @ViewBuilder private func transformRows(_ f: [Float], props: Int, rig: Bool, toggleProp: @escaping (Int) -> Void) -> some View {
        choice("panel_anima_cada", ["panel_letra", "panel_palavra", "panel_linha", "ui_char_no_spaces"], field: selectorField(0, 2), current: Int(f[selectorField(0, 2)]))
        if rig {
            choice("fxl_anchor", ["fxo_layer", "fxl_component"], field: 2, current: Int(f[2]))
            row(param(10, "panel_posicao_x", "px", 1, -20000...20000)); row(param(11, "panel_posicao_y", "px", 1, -20000...20000))
            row(param(17, "fxl_angle", "°", 1, -1800...1800)); row(param(13, "panel_escala", "%", 1, -100...1000))
            row(param(14, "fxl_stretch", "%", 1, -100...1000)); row(param(18, "panel_opacidade", "%", 0.5, -100...100))
        } else {
            row(param(10, "panel_posicao_x", "px", 1, -20000...20000)); row(param(11, "panel_posicao_y", "px", 1, -20000...20000)); row(param(12, "fxl_position_z", "px", 1, -20000...20000))
            row(param(13, "fx_escala_x", "%", 1, -2000...2000)); row(param(14, "fx_escala_y", "%", 1, -2000...2000))
            row(param(17, "panel_rotacao", "°", 1, -3600...3600)); row(param(15, "edt_rotation_x", "°", 1, -3600...3600)); row(param(16, "edt_rotation_y", "°", 1, -3600...3600))
            row(param(18, "panel_opacidade", "%", 0.5, 0...100))
            if props & (1 << 5) != 0 { row(param(20, "panel_desfoque", "px", 0.2, 0...200)) }
            if props & (1 << 4) != 0 { row(param(19, "edt_prop_spacing", "px", 0.5, -500...500)) }
            if props & (1 << 6) != 0 { row(param(21, "edt_prop_skew", "°", 0.5, -80...80)) }
            if props & (1 << 7) != 0 { row(param(22, "panel_contorno", "px", 0.1, -50...50)) }
            if props & (1 << 12) != 0 { row(param(45, "fxl_line_spacing", "px", 1, -20000...20000)) }
            if props & (1 << 11) != 0 { row(param(42, "fxl_anchor", "px", 1, -20000...20000)); row(param(43, "fxl_anchor", "px", 1, -20000...20000)) }
            let extras = [(1 << 5, "panel_desfoque"), (1 << 4, "edt_prop_spacing"), (1 << 6, "edt_prop_skew"), (1 << 7, "panel_contorno"), (1 << 12, "fxl_line_spacing"), (1 << 11, "fxl_anchor")]
            PanelChoiceRow(label: AureaText.t("panel_propriedades"), options: extras.map { AureaText.t($0.1) }, isOn: { props & extras[$0].0 != 0 }) { toggleProp(extras[$0].0) }
        }
    }
    private static let modes = ["panel_somar", "panel_subtrair", "panel_intersecao", "fxo_min", "fxo_maximum", "panel_diferenca"]
    private func selectors(_ f: [Float], tag: String) -> some View {
        let count = Int(f[15])
        return PanelSection(title: AureaText.t("ui_selectors"), glyph: CupertinoGlyph.SliderHorizontal3, tag: "\(tag).sec.selectors", startOpen: false, count: count, stacked: false) {
            choice("fxl_units", ["fxo_percentage", "fxo_index"], field: selectorField(0, 3), current: Int(f[selectorField(0, 3)]))
            choice("fxl_mode", Self.modes, field: selectorField(0, 4), current: Int(f[selectorField(0, 4)]))
            row(param(3, "panel_intensidade", "%", 0.5, -100...100))
            ForEach(Array(1..<max(1, count)), id: \.self) { selectorBody($0, f, tag: tag) }
            PanelItemGrid(items: [
                PanelItem(glyph: CupertinoGlyph.ArrowLeftRight, label: AureaText.t("fxs_range"), tag: "\(tag).add.range") { addSelector(2) },
                PanelItem(glyph: CupertinoGlyph.Waveform, label: AureaText.t("ui_wiggly"), tag: "\(tag).add.wiggly") { addSelector(5) },
                PanelItem(glyph: CupertinoGlyph.Textformat, label: AureaText.t("panel_expressao"), tag: "\(tag).add.expression") { addSelector(3) },
            ], columns: 3).padding(.top, 4)
        }
    }
    private func addSelector(_ type: UInt32) {
        _ = model.engine.addTextSelector(id, index: UInt32(index), type: type)
        reload()
    }
    private func selectorBody(_ s: Int, _ f: [Float], tag: String) -> some View {
        let type = Int(f[selectorField(s, 1)]), block = 64 * s
        let title = AureaText.t(type == 5 ? "ui_wiggly" : type == 3 ? "panel_expressao" : "fxs_range")
        let glyph = type == 5 ? CupertinoGlyph.Waveform : type == 3 ? CupertinoGlyph.Textformat : CupertinoGlyph.ArrowLeftRight
        return PanelSection(title: "\(title) \(s + 1)", glyph: glyph, tag: "\(tag).sel.\(s).\(type)", flat: true, stacked: false) {
            choice("fxl_mode", Self.modes, field: selectorField(s, 4), current: Int(f[selectorField(s, 4)]))
            if type == 5 {
                row(param(block + 34, "fxl_max_amount", "%", 0.5, -100...100)); row(param(block + 33, "fxl_min_amount", "%", 0.5, -100...100))
                row(param(block + 25, "fxl_wiggles_second", "/s", 0.05, 0...60)); row(param(block + 35, "fxl_correlation", "%", 0.5, 0...100))
                row(param(block + 36, "fxl_temporal_phase", "°", 1, -3600...3600)); row(param(block + 37, "fxl_spatial_phase", "°", 1, -3600...3600))
                toggle("fxl_lock_dimensions", f[selectorField(s, 8)] > 0.5) { setField(selectorField(s, 8), $0 ? 1 : 0) }
            } else if type == 3 {
                Text(AureaText.t("ui_expr_selector_hint")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).fixedSize(horizontal: false, vertical: true)
                row(param(block + 38, "panel_intensidade", "%", 1, -1000...1000))
            } else {
                row(param(block, "panel_inicio", "%", 0.5, -1000...1000)); row(param(block + 1, "panel_fim", "%", 0.5, -1000...1000)); row(param(block + 2, "fxl_offset", "%", 0.5, -1000...1000))
                row(param(block + 3, "panel_intensidade", "%", 0.5, -100...100))
            }
            PanelItemRow(glyph: CupertinoGlyph.Trash, title: AureaText.t("panel_remover"), tag: "\(tag).sel.\(s).delete", danger: true) {
                _ = model.engine.removeTextSelector(id, index: UInt32(index), selector: UInt32(s))
                reload()
            }
        }
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
/// texto. O motor avalia (prévia = export). 48 floats por animador
/// (AureaModel.layerAnimFloats; a onda em 31..45).
struct LayerAnimatorSection: View {
    @EnvironmentObject private var model: AureaModel
    @State private var animators: [[Float]] = []
    @State private var failed = false
    @State private var open = 0
    private var id: Int64 { model.primarySelection ?? 0 }
    private func load() {
        let flat = model.engine.layerAnimators(id).map(\.floatValue)
        let n = AureaModel.layerAnimFloats
        animators = stride(from: 0, to: flat.count - flat.count % n, by: n).map { Array(flat[$0..<($0 + n)]) }
    }
    private func refresh() { model.refreshModel(force: true); load() }
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            PanelSection(title: AureaText.t("la_animators"), glyph: CupertinoGlyph.Move, tag: "layer.anim.sec", count: animators.count) {
                if animators.isEmpty {
                    Text(AureaText.t("la_empty")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                }
                ForEach(animators.indices, id: \.self) { index in
                    NativeLayerAnimatorCard(index: UInt32(index), values: animators[index], reload: refresh, open: open == index) { open = open == index ? -1 : index }
                }
                PanelAddRow(label: AureaText.t("la_add"), tag: "layer.anim.add") {
                    failed = model.engine.addLayerAnimator(id) < 0
                    open = animators.count
                    refresh()
                }
                PanelItemGrid(items: (animators.isEmpty ? [] : [PanelItem(glyph: CupertinoGlyph.DocOnDoc, label: AureaText.t("la_copy"), tag: "layer.anim.copy") { _ = model.engine.copyLayerAnimators(id) }]) + [
                    PanelItem(glyph: CupertinoGlyph.DocOnClipboard, label: AureaText.t("la_paste"), tag: "layer.anim.paste") {
                        let ids = model.selection.isEmpty ? [id] : Array(model.selection)
                        _ = model.engine.pasteLayerAnimators(ids.map { NSNumber(value: $0) }); refresh()
                    },
                ])
            }
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
    /// Posição na consulta (Engine::layer_anim_slot): 0..17 em 6 + id; a onda
    /// (18..29) em 32 + (id − 18), com os bits de keyframe em 44/45.
    static let firstWave = 18
    var slot: Int { id < Self.firstWave ? 6 + id : 32 + (id - Self.firstWave) }
    var animatedSlot: Int { id < Self.firstWave ? 24 : 44 }
    var keyedSlot: Int { id < Self.firstWave ? 25 : 45 }
    var bit: Int { 1 << (id < Self.firstWave ? id : id - Self.firstWave) }
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
    /// Onda (0.5.0): o animador automático do Alight Motion, em Hz e graus como no After Effects.
    static var wave: [NativeLayerAnimParam] { [
        NativeLayerAnimParam(id: 18, label: AureaText.t("panel_posicao_x"), unit: "px", step: 1, range: -20000...20000),
        NativeLayerAnimParam(id: 20, label: AureaText.t("la_wave_freq_x"), unit: "Hz", step: 0.01, range: 0...60),
        NativeLayerAnimParam(id: 19, label: AureaText.t("panel_posicao_y"), unit: "px", step: 1, range: -20000...20000),
        NativeLayerAnimParam(id: 21, label: AureaText.t("la_wave_freq_y"), unit: "Hz", step: 0.01, range: 0...60),
        NativeLayerAnimParam(id: 22, label: AureaText.t("panel_rotacao"), unit: "°", step: 0.5, range: -3600...3600),
        NativeLayerAnimParam(id: 23, label: AureaText.t("la_wave_rot_freq"), unit: "Hz", step: 0.01, range: 0...60),
        NativeLayerAnimParam(id: 24, label: AureaText.t("panel_escala"), unit: "%", step: 0.5, range: -1000...1000),
        NativeLayerAnimParam(id: 25, label: AureaText.t("la_wave_scale_freq"), unit: "Hz", step: 0.01, range: 0...60),
        NativeLayerAnimParam(id: 26, label: AureaText.t("la_wave_opacity_low"), unit: "%", step: 0.5, range: 0...100),
        NativeLayerAnimParam(id: 27, label: AureaText.t("la_wave_opacity_high"), unit: "%", step: 0.5, range: 0...100),
        NativeLayerAnimParam(id: 28, label: AureaText.t("la_wave_opacity_freq"), unit: "Hz", step: 0.01, range: 0...60),
        NativeLayerAnimParam(id: 29, label: AureaText.t("la_wave_phase"), unit: "°", step: 1, range: -3600...3600)
    ] }
}

private struct NativeLayerAnimatorCard: View {
    @EnvironmentObject private var model: AureaModel
    let index: UInt32
    let values: [Float]
    let reload: () -> Void
    let open: Bool
    let onToggle: () -> Void
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
            header
            if open {
                PanelSection(title: AureaText.t("fxo_general"), glyph: CupertinoGlyph.SliderHorizontal3, tag: "layer.anim.\(index).sec.general", flat: true) {
                    if isText { choices("la_unit", ["la_unit_whole", "panel_letra", "panel_palavra", "panel_linha"], slot: 1) }
                    toggleLine("la_exit", hint: "la_exit_hint", checked: values[2] > 0.5) { set([2: $0 ? 1 : 0]) }
                    choices("la_ease", ["la_ease_linear", "la_ease_smooth", "la_ease_inout", "la_ease_back"], slot: 3)
                }
                PanelSection(title: AureaText.t("la_strength_delay"), glyph: CupertinoGlyph.Speedometer, tag: "layer.anim.\(index).sec.strength", flat: true) {
                    ForEach(NativeLayerAnimParam.strength) { ruler($0) }
                    if isText && unit != 0 {
                        ruler(NativeLayerAnimParam.delay)
                        NativePanelRuler(label: AureaText.t("beta_overlap"), value: values[28], step: 0.5,
                            range: 0...100, unit: "%", keypad: true) { set([28: $0]) }
                    }
                }
                PanelSection(title: AureaText.t("la_from"), glyph: CupertinoGlyph.ArrowDownRightSquare, tag: "layer.anim.\(index).sec.from", startOpen: false, flat: true) {
                    ForEach(NativeLayerAnimParam.from.filter { ($0.id != 7 || values[4] > 0.5) && ($0.id != 11 || (isText && unit != 0)) }) { ruler($0) }
                    toggleLine("la_scale_separate", hint: nil, checked: values[4] > 0.5) { set([4: $0 ? 1 : 0]) }
                }
                PanelSection(title: AureaText.t("la_wiggle"), glyph: CupertinoGlyph.Scribble, tag: "layer.anim.\(index).sec.wiggle", startOpen: false, flat: true) {
                    ForEach(NativeLayerAnimParam.wiggle) { ruler($0) }
                    Text(AureaText.t("la_wiggle_seed")).font(.aurea(size: 12)).padding(.top, 6)
                    HStack(spacing: 8) {
                        Text("\(Int(values[5]))").font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                        NativePanelChip(AureaText.t("la_reroll")) { set([5: Float(Int.random(in: 1...999_999))]) }
                    }.frame(height: 44)
                }
                if values.count >= AureaModel.layerAnimFloats {
                    PanelSection(title: AureaText.t("la_wave"), glyph: CupertinoGlyph.Waveform, tag: "layer.anim.\(index).sec.wave", startOpen: false, flat: true) {
                        Text(AureaText.t("la_wave_hint")).font(.aurea(size: 11)).foregroundStyle(AureaColors.muted)
                            .fixedSize(horizontal: false, vertical: true)
                        // Forma (31): lida pelo motor só com a marca 2146 em 30.
                        PanelChoiceRow(label: AureaText.t("la_wave_shape"), options: [AureaText.t("la_wave_sine"), AureaText.t("la_wave_triangle")], isOn: { Int(values[31]) == $0 }) { set([30: 2146, 31: Float($0)]) }
                            .accessibilityIdentifier("layer.anim.\(index).wave.shape")
                        ForEach(NativeLayerAnimParam.wave) { ruler($0) }
                    }
                }
            }
        }
        .background(AureaColors.fxLowSurface, in: RoundedRectangle(cornerRadius: 5))
        .environment(\.toggleOffTrack, nil)
        .environment(\.panelItemSurface, AureaColors.fxListSurface)
        .padding(.vertical, 3)
    }
    private var header: some View {
        let on = values[0] > 0.5
        return HStack(spacing: 0) {
            Button(action: onToggle) {
                HStack(spacing: 8) {
                    CupertinoGlyph.text(open ? CupertinoGlyph.ChevronDown : CupertinoGlyph.ChevronRight, size: 14, color: AureaColors.text.opacity(open ? 0.9 : 0.3))
                    CupertinoGlyph.text(CupertinoGlyph.Move, size: 18, color: on ? AureaColors.text : AureaColors.muted)
                    Text(AureaText.t("la_animator_n", Int(index) + 1)).font(.aurea(size: 13, weight: open ? .bold : .semibold))
                        .foregroundStyle(on ? AureaColors.text : AureaColors.muted).lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
                }.frame(height: 37).contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle(shrink: 1))
            .accessibilityIdentifier("layer.anim.\(index).open")
            Button { set([0: on ? 0 : 1]) } label: {
                CupertinoGlyph.text(on ? CupertinoGlyph.Eye : CupertinoGlyph.EyeSlash, size: 18, color: on ? AureaColors.text : AureaColors.muted)
                    .frame(width: 34, height: 37).contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle(shrink: 1))
            .accessibilityAddTraits(on ? .isSelected : [])
            .accessibilityIdentifier("layer.anim.\(index).eye")
            Button { model.engine.removeLayerAnimator(id, index: index); reload() } label: {
                CupertinoGlyph.text(CupertinoGlyph.Trash, size: 18, color: AureaColors.fxListIcon)
                    .frame(width: 34, height: 37).contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle(shrink: 1))
            .accessibilityLabel(AureaText.t("panel_remover"))
            .accessibilityIdentifier("layer.anim.\(index).delete")
        }.padding(.horizontal, 6).frame(height: 37)
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
        PanelChoiceRow(label: AureaText.t(label), options: keys.map { AureaText.t($0) }, isOn: { Int(values[slot]) == $0 }) { set([slot: Float($0)]) }
    }
    private func ruler(_ param: NativeLayerAnimParam) -> some View {
        let bit = param.bit
        let look: KeyframeLook = Int(values[param.keyedSlot]) & bit != 0 ? .keyHere : (Int(values[param.animatedSlot]) & bit != 0 ? .animated : .none)
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
