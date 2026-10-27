import SwiftUI
import UIKit

// =============================================================================
// Omega aparência (área 05, P0-4 + P1-9): BORDA E SOMBRA — par exato do
// `editor/panels/BorderShadowPanel.kt` (mesmos ids, textos e regras).
//
// A moldura comum (PanelFrame) com três abas na coluna direita:
//   · Traço (só forma SDF e texto): amostra · "Traço" · interruptor, régua de
//     largura 0–300 px (passo 0,5) e caixa 55×45. Forma: contorno da forma
//     (cor RGBA8 + parâmetro de forma 4, animável); texto: TextData.stroke*.
//   · Bordas: as decorações role 2 do motor (efeito "Contorno da silhueta"),
//     itens de 37 (seta · tamanho · cor · direção · ≡) e "+ Adicionar borda".
//     Tocar abre os ajustes NO LUGAR (recorte + translação em 200 ms) com
//     Direção · Cor · Tamanho, cada linha com ◆; ≡ arrasta e reordena; deslizar
//     para a esquerda apaga (com "Desfazer").
//   · Sombra: a decoração role 3 ("Sombra projetada"): Cor · Tamanho
//     (suavidade) · Alfa · Deslocamento X/Y (o motor converte para direção e
//     distância).
// O trilho (◆ · curva) mira o parâmetro em foco: a largura do traço da forma,
// a linha tocada da borda aberta ou da sombra. Arrasto = um passo de desfazer.
// =============================================================================

/// Parâmetros reaproveitados (EffectRegistry): borda = `aurea.stylize.stroke_outline`,
/// sombra = `aurea.stylize.drop_shadow`.
private enum BorderShadowParam {
    static let borderWidth: UInt32 = 0
    static let borderColor: UInt32 = 1
    static let borderPosition: UInt32 = 3     // enum 0 Fora, 1 Centro, 2 Dentro
    static let shadowColor: UInt32 = 0
    static let shadowOpacity: UInt32 = 1      // %
    static let shadowDirection: UInt32 = 2    // °
    static let shadowDistance: UInt32 = 3     // px
    static let shadowSoftness: UInt32 = 4     // px
    static let shapeStrokeWidth = 4           // parâmetro de forma
}

/// Medidas (referência: `fragment_border_and_shadow`, `border_list_item`).
private enum BorderShadowMetrics {
    // 0.3.4 painéis: linhas, amostras e réguas maiores (par do `BsDims` do Android).
    /// Linha amostra · título · interruptor (antes 44).
    static let header: CGFloat = 48
    static let swatch: CGFloat = 30
    static let ruler: CGFloat = 64
    static let rulerTop: CGFloat = 5
    static let valueW: CGFloat = 55
    static let valueH: CGFloat = 45
    static let gap: CGFloat = 6.5
    /// Item de 43 com a margem de 6,5 em cima e 0,5 embaixo (antes 37 em 44).
    static let slot: CGFloat = 50
    static let item: CGFloat = 43
    static let radius: CGFloat = PanelMetrics.fieldRadius
    static let cue: CGFloat = 20
    static let dot: CGFloat = 22
    static let label: CGFloat = 60
    static let key: CGFloat = 44
    static let row: CGFloat = 50
    static let pad: CGFloat = 10
    static let openSeconds = 0.2
    static let tickSpacing: CGFloat = 5.5
    static let strokeDefault: Float = 4
    static let strokeMax: Float = 300
    /// O contorno do texto vai até 200 px (o mesmo teto do EditorStore.setTextStrokeWidth).
    static let textStrokeMax: Float = 200
    static let sizeMax: Float = 300
    static let offsetMax: Float = 1000
}

/// O que o trilho mira (◆, curva, apagar todos e o foco da timeline).
private enum BorderShadowFocus: Equatable {
    case strokeWidth
    case param(UInt32, UInt32)
    case shadowOffset(UInt32)
}

/// A última largura do traço por camada (o interruptor desliga com 0 e religa com ela).
private enum BorderShadowMemory {
    static var strokeWidth: [Int64: Float] = [:]
}

/// Quadro de cada item da lista de bordas (abrir os ajustes a partir da linha).
private struct BorderShadowFramesKey: PreferenceKey {
    static let defaultValue: [UInt32: CGRect] = [:]
    static func reduce(value: inout [UInt32: CGRect], nextValue: () -> [UInt32: CGRect]) {
        value.merge(nextValue()) { $1 }
    }
}

struct BorderShadowPanel: View {
    @EnvironmentObject private var model: AureaModel
    @Environment(\.layoutDirection) private var layoutDirection
    @State private var tab: String?
    @State private var focus: BorderShadowFocus?
    @State private var openBorderId: UInt32?
    @State private var shownBorder: UInt32?
    @State private var openProgress: CGFloat = 0
    @State private var originY: CGFloat = 0
    @State private var openToken = 0
    @State private var params: [UInt32: [EffectParamItem]] = [:]
    @State private var shapeValues: [Float] = []
    @State private var textStrokeWidth: Float = 0
    @State private var textStrokeColor: [Float] = [1, 1, 1, 1]
    @State private var shadowOffsetValue: [Float] = [0, 0]
    @State private var offsetStart: [Float] = [0, 0]
    @State private var itemFrames: [UInt32: CGRect] = [:]
    @State private var dragId: UInt32?
    @State private var dragOrder: [UInt32]?
    @State private var dragOffset: CGFloat = 0
    @State private var dragShift: CGFloat = 0
    @State private var swipeId: UInt32?
    @State private var swipeOffset: CGFloat = 0
    @State private var swipeAxis = 0

    private static let space = "border.shadow.list"
    private static let tabStroke = "stroke"
    private static let tabBorders = "borders"
    private static let tabShadow = "shadow"

    // --- Estado lido do modelo ------------------------------------------------
    private var layerId: Int64 { model.primarySelection ?? 0 }
    private var locked: Bool { model.selectedLayer?.locked == true }
    private var isText: Bool { model.selectedLayer?.kind == 4 }
    private var isShape: Bool { model.selectedLayer?.kind == 5 && !model.isVectorLayer }
    private var hasStroke: Bool { isText || isShape }
    private var currentTab: String {
        let chosen = tab ?? (hasStroke ? Self.tabStroke : Self.tabBorders)
        return chosen == Self.tabStroke && !hasStroke ? Self.tabBorders : chosen
    }
    private var borders: [EffectItem] { model.decorations.filter { $0.role == EffectItem.roleBorder } }
    private var shadow: EffectItem? { model.decorations.first { $0.role == EffectItem.roleShadow } }
    private var layerKeys: [KeyframeItem] { model.keyframes[layerId] ?? [] }

    private func param(_ effect: UInt32, _ index: UInt32) -> EffectParamItem? { params[effect]?.first { $0.index == index } }
    private func scalar(_ effect: UInt32, _ index: UInt32, _ fallback: Float = 0) -> Float {
        guard let v = param(effect, index)?.value.first, v.isFinite else { return fallback }
        return v
    }
    private func canKey(_ p: EffectParamItem?) -> Bool {
        guard let p else { return false }
        return p.flags & 1 != 0 && fxComponentCount(Int(p.type)) > 0
    }

    private var shapeNumbers: [NSNumber] { model.detail["shape"] as? [NSNumber] ?? [] }
    private var strokeWidth: Float {
        if isShape {
            if shapeValues.count > BorderShadowParam.shapeStrokeWidth + 2 { return shapeValues[BorderShadowParam.shapeStrokeWidth] }
            return shapeNumbers.count > 3 ? shapeNumbers[3].floatValue : 0
        }
        return textStrokeWidth
    }
    /// RGBA 0..1 da cor do traço (sRGB, como o seletor fala).
    private var strokeColor: [Float] {
        if isShape {
            let packed = shapeNumbers.count > 2 ? shapeNumbers[2].uint32Value : 0
            return (0..<4).map { Float((packed >> UInt32($0 * 8)) & 255) / 255 }
        }
        return textStrokeColor
    }
    private var strokeMax: Float { isShape ? BorderShadowMetrics.strokeMax : BorderShadowMetrics.textStrokeMax }
    /// Bits de animado e de keyframe aqui: os dois últimos números de `shapeParams`.
    private var shapeAnimBits: UInt32 { shapeValues.count >= 2 ? UInt32(max(0, shapeValues[shapeValues.count - 2])) : 0 }
    private var shapeKeyBits: UInt32 { shapeValues.count >= 2 ? UInt32(max(0, shapeValues[shapeValues.count - 1])) : 0 }

    var body: some View {
        PanelFrame(rail: rail(), tabs: tabs, selectedTab: currentTab, onTab: selectTab, scrollContent: false) {
            switch currentTab {
            case Self.tabStroke: strokeTab
            case Self.tabBorders: bordersTab
            default: shadowTab
            }
        }
        .foregroundStyle(AureaColors.text)
        .onAppear { load(); updateTimelineFocus() }
        .onChange(of: model.primarySelection) { _ in resetForLayer(); load(); updateTimelineFocus() }
        .onChange(of: model.decorations) { _ in load(); closeIfMissing() }
        .onChange(of: model.status.modelRevision) { _ in load() }
        .onChange(of: model.status.playhead) { _ in load() }
        .onChange(of: focusTracks(railFocus)) { _ in updateTimelineFocus() }
        .onDisappear { model.timelineFocus = nil }
    }

    private var tabs: [PanelTab] {
        var list: [PanelTab] = []
        if hasStroke { list.append(PanelTab(id: Self.tabStroke, label: AureaText.t("om_ap_bs_stroke"), glyph: CupertinoGlyph.Scribble)) }
        list.append(PanelTab(id: Self.tabBorders, label: AureaText.t("om_ap_bs_borders"), glyph: CupertinoGlyph.Square))
        list.append(PanelTab(id: Self.tabShadow, label: AureaText.t("om_ap_bs_shadow"), glyph: CupertinoGlyph.SquareStack3dDownRight))
        return list
    }

    private func selectTab(_ id: String) {
        tab = id
        if id != Self.tabBorders { closeBorder(animated: false) }
    }

    private func resetForLayer() {
        tab = nil; focus = nil
        openBorderId = nil; shownBorder = nil; openProgress = 0
        dragId = nil; dragOrder = nil; dragOffset = 0; dragShift = 0
        swipeId = nil; swipeOffset = 0; swipeAxis = 0
    }

    /// Relê parâmetros das decorações, o traço e o deslocamento da sombra (só publica o que mudou).
    private func load() {
        guard let id = model.primarySelection else {
            if !params.isEmpty { params = [:] }
            return
        }
        var next: [UInt32: [EffectParamItem]] = [:]
        for item in model.decorations { next[item.effectId] = model.decorationParams(item.effectId) }
        if next != params { params = next }
        let shapeNext: [Float] = isShape ? model.engine.shapeParams(id).map(\.floatValue) : []
        if shapeNext != shapeValues { shapeValues = shapeNext }
        if isText, let text = model.engine.text(forLayer: id) {
            let width = (text["strokeWidth"] as? NSNumber)?.floatValue ?? 0
            if width != textStrokeWidth { textStrokeWidth = width }
            let raw = (text["strokeColor"] as? [NSNumber] ?? []).map(\.floatValue)
            let color: [Float] = raw.count >= 4 ? Array(raw.prefix(4)) : [1, 1, 1, 1]
            if color != textStrokeColor { textStrokeColor = color }
        }
        if let s = shadow {
            let raw = model.shadowOffset(s.effectId)
            let offset: [Float] = raw.count >= 2 ? Array(raw.prefix(2)) : [0, 0]
            if offset != shadowOffsetValue { shadowOffsetValue = offset }
        }
    }

    // =========================================================================
    // Trilho: o parâmetro em foco
    // =========================================================================

    private var railFocus: BorderShadowFocus? {
        switch currentTab {
        case Self.tabStroke:
            return isShape ? .strokeWidth : nil
        case Self.tabBorders:
            guard let id = openBorderId else { return nil }
            if case let .param(effect, _)? = focus, effect == id { return focus }
            return .param(id, BorderShadowParam.borderWidth)
        default:
            guard let id = shadow?.effectId else { return nil }
            switch focus {
            case let .param(effect, _)? where effect == id: return focus
            case let .shadowOffset(effect)? where effect == id: return focus
            default: return .param(id, BorderShadowParam.shadowSoftness)
            }
        }
    }

    private func rail() -> PanelRail {
        let f = railFocus
        let track = focusTrack(f)
        var onKey: (() -> Void)? = nil
        var onDelete: (() -> Void)? = nil
        var onCurve: (() -> Void)? = nil
        if let f, !locked, focusCanKey(f) {
            onKey = { toggleFocusKey(f) }
            onDelete = { deleteFocusKeys(f) }
            onCurve = { openFocusCurve(f) }
        }
        return PanelRail(onBack: { model.panel = .none }, keyframeLook: focusLook(f), onKeyframe: onKey,
                         onDeleteAllKeyframes: onDelete, onCurve: onCurve, curveReady: track.count >= 2)
    }

    private func effectLook(_ effect: UInt32, _ index: UInt32) -> KeyframeLook {
        let keys = layerKeys.filter { $0.property == 31 && $0.effectIndex == effect && $0.paramIndex / 4 == index }
        return keys.contains { $0.time == model.localPlayhead } ? .keyHere : keys.isEmpty ? .none : .animated
    }
    private func effectTrack(_ effect: UInt32, _ index: UInt32) -> [KeyframeItem] {
        layerKeys.filter { $0.property == 31 && $0.effectIndex == effect && $0.paramIndex == index * 4 }.sorted { $0.time < $1.time }
    }
    private var strokeTrack: [KeyframeItem] {
        layerKeys.filter { $0.property == 35 && $0.paramIndex == UInt32(BorderShadowParam.shapeStrokeWidth) }.sorted { $0.time < $1.time }
    }
    private func combine(_ a: KeyframeLook, _ b: KeyframeLook) -> KeyframeLook {
        if a == .keyHere || b == .keyHere { return .keyHere }
        if a == .animated || b == .animated { return .animated }
        return .none
    }

    private func focusLook(_ f: BorderShadowFocus?) -> KeyframeLook {
        switch f {
        case nil: return .none
        case .strokeWidth?:
            let bit = UInt32(1) << UInt32(BorderShadowParam.shapeStrokeWidth)
            if shapeKeyBits & bit != 0 { return .keyHere }
            return shapeAnimBits & bit != 0 ? .animated : .none
        case let .param(effect, index)?:
            return effectLook(effect, index)
        case let .shadowOffset(effect)?:
            return combine(effectLook(effect, BorderShadowParam.shadowDirection), effectLook(effect, BorderShadowParam.shadowDistance))
        }
    }

    private func focusCanKey(_ f: BorderShadowFocus) -> Bool {
        switch f {
        case .strokeWidth: return !shapeValues.isEmpty
        case let .param(effect, index): return canKey(param(effect, index))
        case let .shadowOffset(effect):
            return canKey(param(effect, BorderShadowParam.shadowDirection)) && canKey(param(effect, BorderShadowParam.shadowDistance))
        }
    }

    /// A trilha que a curva do trilho abre.
    private func focusTrack(_ f: BorderShadowFocus?) -> [KeyframeItem] {
        switch f {
        case nil: return []
        case .strokeWidth?: return strokeTrack
        case let .param(effect, index)?: return effectTrack(effect, index)
        case let .shadowOffset(effect)?:
            return preferredCurveTrack([effectTrack(effect, BorderShadowParam.shadowDistance), effectTrack(effect, BorderShadowParam.shadowDirection)])
        }
    }

    /// As trilhas do parâmetro em foco (o foco da timeline).
    private func focusTracks(_ f: BorderShadowFocus?) -> [TimelineTrack] {
        switch f {
        case nil: return []
        case .strokeWidth?: return [TimelineTrack(property: 35, effect: 0, param: UInt32(BorderShadowParam.shapeStrokeWidth))]
        case let .param(effect, index)?:
            let count = max(1, fxComponentCount(Int(param(effect, index)?.type ?? 0)))
            return (0..<count).map { TimelineTrack(property: 31, effect: effect, param: index * 4 + UInt32($0)) }
        case let .shadowOffset(effect)?:
            return [TimelineTrack(property: 31, effect: effect, param: BorderShadowParam.shadowDirection * 4),
                    TimelineTrack(property: 31, effect: effect, param: BorderShadowParam.shadowDistance * 4)]
        }
    }

    private func updateTimelineFocus() {
        let tracks = focusTracks(railFocus)
        model.timelineFocus = tracks.isEmpty ? nil : tracks
    }

    /// ◆ do trilho (e o ◆ das linhas de deslocamento): marca/tira no cabeçote, um passo de desfazer.
    private func toggleFocusKey(_ f: BorderShadowFocus) {
        guard !locked, let layer = model.primarySelection else { return }
        switch f {
        case .strokeWidth:
            _ = model.engine.keyShape(layer, param: UInt32(BorderShadowParam.shapeStrokeWidth))
            model.refreshModel(force: true)
            load()
        case let .param(effect, index):
            toggleEffectKeys(layer, effect: effect, indices: [index])
        case let .shadowOffset(effect):
            // Direção e distância andam juntas: com marca aqui em qualquer uma, tira; senão marca as duas.
            toggleEffectKeys(layer, effect: effect, indices: [BorderShadowParam.shadowDirection, BorderShadowParam.shadowDistance])
        }
    }

    /// Marca ou tira o keyframe dos parâmetros no cabeçote (o mesmo caminho da lista de efeitos):
    /// com marca aqui em qualquer um deles, tira as marcas daqui; senão marca todos os componentes.
    private func toggleEffectKeys(_ layer: Int64, effect: UInt32, indices: [UInt32]) {
        let time = model.localPlayhead
        let here = layerKeys.filter { $0.property == 31 && $0.effectIndex == effect && indices.contains($0.paramIndex / 4) && $0.time == time }
        model.beginGesture("keyframe de borda e sombra")
        if here.isEmpty {
            for index in indices {
                guard let p = param(effect, index), canKey(p) else { continue }
                for component in 0..<fxComponentCount(Int(p.type)) {
                    model.engine.keyParameter(layer, property: 31, effect: effect, param: index * 4 + UInt32(component), time: time,
                                              value: component < p.value.count ? p.value[component] : 0)
                }
            }
        } else {
            for key in here {
                model.engine.editTrackKey(layer, property: key.property, effect: key.effectIndex, param: key.paramIndex, time: key.time,
                                          action: 1, value: key.value, targetTime: key.time, interpolation: key.interpolation, handles: [])
            }
        }
        model.commitPendingCommands()
        model.endGesture()
        load()
    }

    /// Segurar ◆: apaga todas as marcas do parâmetro em foco num passo de desfazer.
    private func deleteFocusKeys(_ f: BorderShadowFocus) {
        guard !locked, let layer = model.primarySelection else { return }
        let keys: [KeyframeItem]
        switch f {
        case .strokeWidth:
            keys = strokeTrack
        case let .param(effect, index):
            keys = layerKeys.filter { $0.property == 31 && $0.effectIndex == effect && $0.paramIndex / 4 == index }
        case let .shadowOffset(effect):
            let pair = [BorderShadowParam.shadowDirection, BorderShadowParam.shadowDistance]
            keys = layerKeys.filter { $0.property == 31 && $0.effectIndex == effect && pair.contains($0.paramIndex / 4) }
        }
        guard !keys.isEmpty else { return }
        model.beginGesture("apagar keyframes")
        for key in keys {
            model.engine.editTrackKey(layer, property: key.property, effect: key.effectIndex, param: key.paramIndex, time: key.time,
                                      action: 1, value: key.value, targetTime: key.time, interpolation: key.interpolation, handles: [])
        }
        model.commitPendingCommands()
        model.endGesture()
        load()
    }

    /// Curva do trecho sob o cabeçote (fora dos trechos, o primeiro ou o último).
    private func openFocusCurve(_ f: BorderShadowFocus) {
        let track = focusTrack(f)
        guard track.count >= 2 else { return }
        let index = max(0, min(track.count - 2, track.lastIndex { $0.time <= model.localPlayhead } ?? 0))
        let key = track[index]
        model.openCurve(property: key.property, effect: key.effectIndex, param: key.paramIndex, time: key.time)
    }

    // =========================================================================
    // Traço
    // =========================================================================

    private var strokeTab: some View {
        let width = strokeWidth
        let title = AureaText.t("om_ap_bs_stroke")
        let widthTitle = AureaText.t("om_ap_bs_width")
        return ScrollView(.vertical) {
            VStack(spacing: 0) {
                headerLine(title: title, color: strokeColor, swatchId: "border.stroke.color", switchId: "border.stroke.switch",
                           checked: width > 0, swatchEnabled: !locked, onSwatch: openStrokeColor, onSwitch: setStrokeOn)
                HStack(spacing: 4) {
                    BorderShadowRuler(value: width, step: 0.5, minimum: 0, maximum: strokeMax, enabled: !locked,
                                      onStart: {
                                          focus = .strokeWidth
                                          model.beginGesture("largura do traço")
                                          ensureStrokeVisible()
                                      },
                                      onValue: writeStrokeWidth,
                                      onEnd: { model.endGesture(); load() })
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                        .accessibilityLabel(widthTitle)
                        .accessibilityIdentifier("border.stroke.ruler")
                    BorderShadowValueBox(text: numeroPtBr(width, casas: 1), enabled: !locked) {
                        focus = .strokeWidth
                        model.numericKeypad = KeypadRequest(title: widthTitle, value: width, unit: "px", min: 0, max: strokeMax, decimals: 1) { v in
                            guard !locked else { return }
                            model.beginGesture("largura do traço")
                            if v > 0 { ensureStrokeVisible() }
                            writeStrokeWidth(v)
                            model.endGesture()
                            load()
                        }
                    }
                    .accessibilityLabel(widthTitle)
                    .accessibilityIdentifier("border.stroke.width")
                }
                .frame(height: BorderShadowMetrics.ruler)
                .padding(.top, BorderShadowMetrics.rulerTop)
                Color.clear.frame(height: 8)
            }
            .padding(.horizontal, BorderShadowMetrics.pad)
        }
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("border.tab.stroke")
    }

    private func writeStrokeWidth(_ value: Float) {
        guard value.isFinite, !locked, let id = model.primarySelection else { return }
        let width = Swift.min(Swift.max(value, 0), strokeMax)
        if isShape {
            _ = model.engine.editShape(id, param: UInt32(BorderShadowParam.shapeStrokeWidth), value: width, continuing: false)
        } else {
            model.engine.setText(id, strokeWidth: width)
        }
        model.refreshModel(force: true)
        load()
    }

    private func setStrokeColor(_ rgba: [Float], layer: Int64) {
        guard rgba.count == 4, rgba.allSatisfy(\.isFinite), model.primarySelection == layer else { return }
        // Forma: RGBA8 sRGB, como o ColorRequest do Android; texto: a cor do contorno do TextData.
        if isShape {
            model.mutate { $0.setShape(layer, strokeR: rgba[0], g: rgba[1], b: rgba[2], a: rgba[3]) }
        } else {
            model.engine.setText(layer, strokeR: rgba[0], g: rgba[1], b: rgba[2], a: rgba[3])
        }
        model.refreshModel(force: true)
        load()
    }

    /// Largura sem cor: o traço nasce visível (senão o número mente).
    private func ensureStrokeVisible() {
        guard let id = model.primarySelection else { return }
        let c = strokeColor
        if c.count >= 4 && c[3] <= 0 { setStrokeColor([c[0], c[1], c[2], 1], layer: id) }
    }

    /// Interruptor: desligar = largura 0 (guarda a última); ligar = a última (ou 4 px).
    private func setStrokeOn(_ on: Bool) {
        guard !locked, let id = model.primarySelection else { return }
        model.beginGesture("traço")
        if on {
            ensureStrokeVisible()
            let remembered = BorderShadowMemory.strokeWidth[id] ?? 0
            writeStrokeWidth(remembered > 0 ? remembered : BorderShadowMetrics.strokeDefault)
        } else {
            if strokeWidth > 0 { BorderShadowMemory.strokeWidth[id] = strokeWidth }
            writeStrokeWidth(0)
        }
        model.endGesture()
        load()
    }

    private func openStrokeColor(_ anchor: CGRect) {
        guard !locked, let layer = model.primarySelection else { return }
        let original = strokeColor
        let alpha: Float = original.count > 3 && original[3] > 0 ? original[3] : 1
        model.beginGesture("cor do traço")
        model.colorSheet = ColorSheetRequest(title: AureaText.t("om_ap_bs_stroke"), initial: Array(original.prefix(3)) + [alpha], anchor: anchor,
                                             onChange: { r, g, b, a in setStrokeColor([r, g, b, a], layer: layer) },
                                             onDone: { model.endGesture() })
    }

    // =========================================================================
    // Bordas
    // =========================================================================

    private var orderedBorders: [EffectItem] {
        let list = borders
        guard let order = dragOrder else { return list }
        return order.compactMap { id in list.first { $0.effectId == id } }
    }

    private var bordersTab: some View {
        let list = borders
        return ZStack(alignment: .top) {
            ScrollView(.vertical) {
                VStack(spacing: 0) {
                    ForEach(orderedBorders, id: \.effectId) { item in
                        borderItem(item, index: list.firstIndex(where: { $0.effectId == item.effectId }) ?? 0, count: list.count)
                    }
                    addBorderItem
                    Color.clear.frame(height: BorderShadowMetrics.gap)
                }
            }
            .scrollDisabled(dragId != nil)
            if let shown = shownBorder {
                borderSettings(shown, index: list.firstIndex(where: { $0.effectId == shown }) ?? 0)
            }
        }
        .coordinateSpace(name: Self.space)
        .onPreferenceChange(BorderShadowFramesKey.self) { itemFrames = $0 }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        .clipped()
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("border.tab.borders")
    }

    private func directionKey(_ position: Int) -> String {
        switch position {
        case 0: return "om_ap_bs_outside"
        case 1: return "om_ap_bs_center"
        default: return "om_ap_bs_inside"
        }
    }

    private func borderPosition(_ effect: UInt32) -> Int {
        let raw = scalar(effect, BorderShadowParam.borderPosition, 2)
        return Swift.min(2, Swift.max(0, Int(raw.rounded())))
    }

    private func borderItem(_ item: EffectItem, index: Int, count: Int) -> some View {
        let id = item.effectId
        let width = scalar(id, BorderShadowParam.borderWidth)
        let color = param(id, BorderShadowParam.borderColor).map { AureaColorSpace.engineToDisplay($0.value) }
        let name = AureaText.t("om_ap_bs_border_n", index + 1)
        let size = AureaText.t("om_ap_bs_px", numeroPtBr(width, casas: 0))
        let direction = AureaText.t(directionKey(borderPosition(id)))
        let lifted = dragId == id
        let swiping = swipeId == id
        let enabled = !locked
        let gap = BorderShadowMetrics.gap
        return ZStack(alignment: .trailing) {
            if swiping && swipeOffset < 0 {
                RoundedRectangle(cornerRadius: BorderShadowMetrics.radius).fill(AureaColors.danger)
                    .overlay(alignment: .trailing) {
                        CupertinoGlyph.text(CupertinoGlyph.Trash, size: 18, color: AureaColors.text).padding(.trailing, 14)
                    }
                    .padding(EdgeInsets(top: gap, leading: gap, bottom: 0.5, trailing: gap))
                    .accessibilityHidden(true)
            }
            HStack(spacing: 0) {
                Button { openBorder(id) } label: {
                    borderItemLabel(size: size, color: color, direction: direction)
                }
                .buttonStyle(AureaPressStyle(shrink: 1))
                .accessibilityLabel("\(name), \(size), \(direction)")
                .accessibilityHint(enabled ? AureaText.t("fxui_a11y_swipe_delete") : "")
                .accessibilityIdentifier("border.item.\(index)")
                .accessibilityActions {
                    if enabled {
                        Button(AureaText.t("om_ap_bs_delete_border")) { model.removeEffectWithUndo(id); load() }
                        if index > 0 {
                            Button(AureaText.t("panel_mover_cima")) { model.moveDecoration(id, slot: index - 1); load() }
                        }
                        if index < count - 1 {
                            Button(AureaText.t("panel_mover_baixo")) { model.moveDecoration(id, slot: index + 1); load() }
                        }
                    }
                }
                borderHandle(id, index: index, enabled: enabled)
            }
            .background(alignment: .top) {
                RoundedRectangle(cornerRadius: BorderShadowMetrics.radius)
                    .fill(lifted ? AureaColors.fxSunken : AureaColors.fxListSurface)
                    .frame(height: BorderShadowMetrics.item)
                    .padding(.top, gap)
            }
            .padding(.horizontal, gap)
            .offset(x: swiping ? swipeOffset : 0)
        }
        .frame(height: BorderShadowMetrics.slot)
        .background(GeometryReader { geo in
            Color.clear.preference(key: BorderShadowFramesKey.self, value: [id: geo.frame(in: .named(Self.space))])
        })
        .offset(y: lifted ? dragOffset : 0)
        .zIndex(lifted ? 1 : 0)
        .simultaneousGesture(DragGesture(minimumDistance: 12, coordinateSpace: .global)
            .onChanged { value in swipeChanged(id, value, enabled: enabled) }
            .onEnded { value in swipeEnded(id, value) }, including: enabled ? .all : .subviews)
    }

    /// Seta 18 · "6 px" 17 · círculo 20 com a cor · direção 17 (autoajuste) no item de 37.
    private func borderItemLabel(size: String, color: [Float]?, direction: String) -> some View {
        HStack(spacing: 0) {
            CupertinoGlyph.text(CupertinoGlyph.ChevronRight, size: BorderShadowMetrics.cue)
                .mirrorsInRtl()
                .frame(width: BorderShadowMetrics.cue, height: BorderShadowMetrics.cue)
            Color.clear.frame(width: 6)
            Text(size).font(.aurea(size: 17).monospacedDigit()).foregroundStyle(AureaColors.text)
                .lineLimit(1).fixedSize()
            Color.clear.frame(width: 10)
            BorderShadowSwatch(color: color, shape: Circle())
                .frame(width: BorderShadowMetrics.dot, height: BorderShadowMetrics.dot)
            Color.clear.frame(width: 10)
            Text(direction).font(.aurea(size: 17)).foregroundStyle(AureaColors.text)
                .lineLimit(1).minimumScaleFactor(8.0 / 17.0)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
        .padding(.leading, 3)
        .frame(height: BorderShadowMetrics.item)
        .padding(.top, BorderShadowMetrics.gap).padding(.bottom, 0.5)
        .contentShape(Rectangle())
    }

    /// A alça ≡: arrasto vertical reordena (soltar grava com UM comando).
    private func borderHandle(_ id: UInt32, index: Int, enabled: Bool) -> some View {
        CupertinoGlyph.text(CupertinoGlyph.LineHorizontal3, size: 18, color: enabled ? AureaColors.text : AureaColors.fxDisabledText)
            .frame(width: 44, height: BorderShadowMetrics.item)
            .padding(.top, BorderShadowMetrics.gap).padding(.bottom, 0.5)
            .contentShape(Rectangle())
            .highPriorityGesture(DragGesture(minimumDistance: 1, coordinateSpace: .named(Self.space))
                .onChanged { g in dragChanged(id, translation: g.translation.height) }
                .onEnded { _ in dragEnded() }, including: enabled ? .all : .none)
            .accessibilityLabel(AureaText.t("om_ap_bs_reorder"))
            .accessibilityIdentifier("border.item.\(index).handle")
    }

    private var addBorderItem: some View {
        let label = AureaText.t("om_ap_bs_add_border")
        let gap = BorderShadowMetrics.gap
        return Button {
            guard !locked else { return }
            model.addDecoration(role: EffectItem.roleBorder)
            load()
        } label: {
            HStack(spacing: 6) {
                CupertinoGlyph.text(CupertinoGlyph.Plus, size: 18)
                Text(label).font(.aurea(size: 15)).foregroundStyle(AureaColors.text).lineLimit(1)
            }
            .frame(maxWidth: .infinity)
            .frame(height: BorderShadowMetrics.item)
            .background(AureaColors.fxListSurface, in: RoundedRectangle(cornerRadius: BorderShadowMetrics.radius))
            .padding(EdgeInsets(top: gap, leading: gap, bottom: 0.5, trailing: gap))
            .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .disabled(locked)
        .opacity(locked ? 0.45 : 1)
        .accessibilityLabel(label)
        .accessibilityIdentifier("border.add")
    }

    // --- Reordenar pela alça (um comando ao soltar = um passo de desfazer) ----
    private func dragChanged(_ id: UInt32, translation: CGFloat) {
        guard !locked else { return }
        if dragId != id {
            dragId = id
            dragOrder = borders.map(\.effectId)
            dragShift = 0
            swipeId = nil; swipeOffset = 0; swipeAxis = 0
        }
        guard var order = dragOrder, let i = order.firstIndex(of: id) else { return }
        let pitch = BorderShadowMetrics.slot
        var offset = translation - dragShift
        if offset > pitch / 2 && i < order.count - 1 {
            order.swapAt(i, i + 1); dragShift += pitch; offset -= pitch
        } else if offset < -pitch / 2 && i > 0 {
            order.swapAt(i, i - 1); dragShift -= pitch; offset += pitch
        }
        dragOrder = order
        dragOffset = offset
    }

    private func dragEnded() {
        if let id = dragId, let order = dragOrder, let to = order.firstIndex(of: id),
           let from = borders.firstIndex(where: { $0.effectId == id }), to != from {
            model.moveDecoration(id, slot: to)
        }
        dragId = nil; dragOrder = nil; dragOffset = 0; dragShift = 0
        load()
    }

    // --- Deslizar para a esquerda apaga (com "Desfazer" no aviso) -------------
    private func swipeChanged(_ id: UInt32, _ value: DragGesture.Value, enabled: Bool) {
        guard enabled, dragId == nil else { return }
        if swipeId != id { swipeId = id; swipeOffset = 0; swipeAxis = 0 }
        let dx = value.translation.width, dy = value.translation.height
        // Trava o eixo no primeiro movimento: vertical é a rolagem da lista.
        if swipeAxis == 0 { swipeAxis = abs(dx) > abs(dy) ? 1 : 2 }
        guard swipeAxis == 1 else { return }
        // Tela em árabe: o "para o início da linha" é para a direita.
        let logical = layoutDirection == .rightToLeft ? -dx : dx
        let width = itemFrames[id]?.width ?? 0
        swipeOffset = Swift.min(0, Swift.max(-width, logical))
    }

    private func swipeEnded(_ id: UInt32, _ value: DragGesture.Value) {
        guard swipeId == id else { return }
        let width = itemFrames[id]?.width ?? 0
        if swipeAxis == 1 && fxSwipeCommits(dx: swipeOffset, width: width, minimum: 96) {
            withAnimation(.easeOut(duration: 0.16)) { swipeOffset = -width }
            if openBorderId == id { closeBorder(animated: false) }
            model.removeEffectWithUndo(id)
            swipeId = nil; swipeOffset = 0; swipeAxis = 0
            load()
        } else {
            withAnimation(.easeOut(duration: 0.2)) { swipeOffset = 0 }
            swipeAxis = 0
        }
    }

    // --- Abrir no lugar (P1-9) -------------------------------------------------
    /// Os ajustes nascem na linha tocada com a altura de um item e crescem até a
    /// lista inteira (recorte + translação em 200 ms); fechar faz o contrário.
    private func openBorder(_ id: UInt32) {
        originY = itemFrames[id]?.minY ?? 0
        focus = .param(id, BorderShadowParam.borderWidth)
        openBorderId = id
        shownBorder = id
        openToken += 1
        if UIAccessibility.isReduceMotionEnabled {
            openProgress = 1
        } else {
            openProgress = 0
            withAnimation(.easeInOut(duration: BorderShadowMetrics.openSeconds)) { openProgress = 1 }
        }
    }

    private func closeBorder(animated: Bool = true) {
        openBorderId = nil
        guard shownBorder != nil else { return }
        openToken += 1
        let token = openToken
        if animated && !UIAccessibility.isReduceMotionEnabled {
            withAnimation(.easeInOut(duration: BorderShadowMetrics.openSeconds)) { openProgress = 0 }
            DispatchQueue.main.asyncAfter(deadline: .now() + BorderShadowMetrics.openSeconds) {
                if openToken == token { shownBorder = nil }
            }
        } else {
            openProgress = 0
            shownBorder = nil
        }
    }

    /// A borda aberta sumiu (desfazer, apagar por outro caminho): fecha.
    private func closeIfMissing() {
        if let id = openBorderId, !borders.contains(where: { $0.effectId == id }) { closeBorder() }
    }

    /// Cabeçalho dos ajustes: seta aberta · "Borda N" · 🗑 36. Tocar fecha (a margem de cima entra no alvo).
    private func borderSettingsHeader(_ id: UInt32, index: Int, name: String) -> some View {
        let gap = BorderShadowMetrics.gap
        return ZStack(alignment: .trailing) {
            Button { closeBorder() } label: {
                HStack(spacing: 0) {
                    CupertinoGlyph.text(CupertinoGlyph.ChevronDown, size: BorderShadowMetrics.cue, color: AureaColors.text.opacity(0.9))
                        .frame(width: BorderShadowMetrics.cue, height: BorderShadowMetrics.cue)
                        .padding(.leading, 8)
                    Text(name).font(.aurea(size: 15)).foregroundStyle(AureaColors.text).lineLimit(1)
                        .padding(.leading, 6).padding(.trailing, 4)
                        .frame(maxWidth: .infinity, alignment: .leading)
                    Color.clear.frame(width: 44)
                }
                .padding(.leading, 3).padding(.trailing, 4)
                .frame(height: BorderShadowMetrics.item)
                .padding(.top, gap)
                .contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle(shrink: 1))
            .accessibilityLabel(name)
            .accessibilityAddTraits(.isHeader)
            Button {
                guard !locked else { return }
                closeBorder(animated: false)
                model.removeEffectWithUndo(id)
                load()
            } label: {
                CupertinoGlyph.text(CupertinoGlyph.Trash, size: 20, color: locked ? AureaColors.fxDisabledText : AureaColors.fxListIcon)
                    .frame(width: 44, height: BorderShadowMetrics.item)
                    .contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle(shrink: 1))
            .disabled(locked)
            .padding(.top, gap)
            .padding(.trailing, 4)
            .accessibilityLabel(AureaText.t("om_ap_bs_delete_border"))
            .accessibilityIdentifier("border.item.\(index).delete")
        }
    }

    /// Os AJUSTES DA BORDA abertos no lugar: cabeçalho (seta aberta · "Borda N" · 🗑)
    /// e as linhas Direção · Cor · Tamanho.
    private func borderSettings(_ id: UInt32, index: Int) -> some View {
        let name = AureaText.t("om_ap_bs_border_n", index + 1)
        let progress = openProgress
        let gap = BorderShadowMetrics.gap
        let slot = BorderShadowMetrics.slot
        return GeometryReader { geo in
            VStack(spacing: 0) {
                borderSettingsHeader(id, index: index, name: name)
                ScrollView(.vertical) {
                    VStack(spacing: 0) {
                        directionRow(id)
                        colorRow(id, index: BorderShadowParam.borderColor, label: AureaText.t("om_ap_bs_color"), tag: "border.edit.color")
                        rulerRow(id, index: BorderShadowParam.borderWidth, label: AureaText.t("om_ap_bs_size"), tag: "border.edit.size",
                                 step: 1, minimum: 0, maximum: BorderShadowMetrics.sizeMax, decimals: 0, unit: "px", gesture: "tamanho da borda")
                    }
                    .padding(.horizontal, 4).padding(.vertical, 2)
                }
            }
            // Cabeçalho e corpo no mesmo cartão (raio 5) da «superfície de lista».
            .background(alignment: .top) {
                RoundedRectangle(cornerRadius: BorderShadowMetrics.radius).fill(AureaColors.fxListSurface).padding(.top, gap)
            }
            .padding(.horizontal, gap).padding(.bottom, gap)
            .frame(width: geo.size.width, height: geo.size.height, alignment: .top)
            .background(AureaColors.panelFrameContent)
            .background(AureaColors.panelFrameBackground)
            .mask(alignment: .top) {
                Rectangle().frame(height: Swift.max(0, slot + (geo.size.height - slot) * progress))
            }
            .offset(y: originY * (1 - progress))
        }
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("border.settings")
    }

    // =========================================================================
    // Sombra
    // =========================================================================

    private var shadowTab: some View {
        let item = shadow
        let title = AureaText.t("om_ap_bs_shadow")
        let color: [Float]? = item.flatMap { s in param(s.effectId, BorderShadowParam.shadowColor).map { AureaColorSpace.engineToDisplay($0.value) } }
        return ScrollView(.vertical) {
            VStack(spacing: 0) {
                headerLine(title: title, color: color, swatchId: "shadow.swatch", switchId: "shadow.switch",
                           checked: item?.enabled == true, swatchEnabled: !locked && item != nil,
                           onSwatch: { anchor in
                               guard let s = shadow else { return }
                               focus = .param(s.effectId, BorderShadowParam.shadowColor)
                               openEffectColor(s.effectId, BorderShadowParam.shadowColor, label: AureaText.t("om_ap_bs_color"), anchor: anchor)
                           },
                           onSwitch: setShadowOn)
                    .padding(.leading, 4)
                if let s = item {
                    colorRow(s.effectId, index: BorderShadowParam.shadowColor, label: AureaText.t("om_ap_bs_color"), tag: "shadow.color")
                    rulerRow(s.effectId, index: BorderShadowParam.shadowSoftness, label: AureaText.t("om_ap_bs_size"), tag: "shadow.size",
                             step: 1, minimum: 0, maximum: BorderShadowMetrics.sizeMax, decimals: 0, unit: "px", gesture: "tamanho da sombra")
                    rulerRow(s.effectId, index: BorderShadowParam.shadowOpacity, label: AureaText.t("om_ap_bs_alpha"), tag: "shadow.alpha",
                             step: 1, minimum: 0, maximum: 100, decimals: 0, unit: "%", gesture: "alfa da sombra")
                    offsetRow(s.effectId, axis: 0)
                    offsetRow(s.effectId, axis: 1)
                }
                Color.clear.frame(height: 8)
            }
            .padding(.horizontal, 6)
        }
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("border.tab.shadow")
    }

    /// Sem sombra: ligar cria a decoração (nasce ligada); com sombra: liga/desliga o efeito.
    private func setShadowOn(_ on: Bool) {
        guard !locked, let layer = model.primarySelection else { return }
        if let s = shadow {
            model.mutate { $0.setEffect(s.effectId, forLayer: layer, enabled: on) }
            model.refreshSelectedLayer()
        } else if on {
            model.addDecoration(role: EffectItem.roleShadow)
        }
        load()
    }

    /// DESLOCAMENTO X/Y em px (o motor guarda direção e distância; a conversão é dele).
    /// O ◆ das duas linhas marca direção e distância juntas.
    private func offsetRow(_ id: UInt32, axis: Int) -> some View {
        let axisName = axis == 0 ? "X" : "Y"
        let label = AureaText.t("om_ap_bs_offset") + " " + axisName
        let value = axis < shadowOffsetValue.count ? shadowOffsetValue[axis] : 0
        let me = BorderShadowFocus.shadowOffset(id)
        let look = combine(effectLook(id, BorderShadowParam.shadowDirection), effectLook(id, BorderShadowParam.shadowDistance))
        let tag = "shadow.offset." + axisName.lowercased()
        let limit = BorderShadowMetrics.offsetMax
        return paramRow(label: label, height: BorderShadowMetrics.ruler, focused: railFocus == me, look: look,
                        canKey: !locked && focusCanKey(me), keyTag: tag + ".key",
                        onFocus: { focus = me }, onKey: { toggleFocusKey(me) }) {
            HStack(spacing: 3) {
                BorderShadowRuler(value: value, step: 0.5, minimum: -limit, maximum: limit, enabled: !locked,
                                  onStart: {
                                      focus = me
                                      offsetStart = shadowOffsetValue
                                      model.beginGesture("posição da sombra")
                                  },
                                  onValue: { writeOffset(id, axis: axis, value: $0) },
                                  onEnd: { model.endGesture(); load() })
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .accessibilityLabel(label)
                    .accessibilityIdentifier(tag + ".ruler")
                BorderShadowValueBox(text: numeroPtBr(value, casas: 1), enabled: !locked) {
                    focus = me
                    model.numericKeypad = KeypadRequest(title: label, value: value, unit: "px", min: -limit, max: limit, decimals: 1) { v in
                        offsetStart = shadowOffsetValue
                        model.beginGesture("posição da sombra")
                        writeOffset(id, axis: axis, value: v)
                        model.endGesture()
                        load()
                    }
                }
                .accessibilityLabel(label)
                .accessibilityIdentifier(tag)
            }
        }
    }

    private func writeOffset(_ id: UInt32, axis: Int, value: Float) {
        guard value.isFinite, !locked else { return }
        let limit = BorderShadowMetrics.offsetMax
        let v = Swift.min(Swift.max(value, -limit), limit)
        let base: [Float] = offsetStart.count >= 2 ? offsetStart : [0, 0]
        if axis == 0 { model.setShadowOffset(id, dx: v, dy: base[1]) } else { model.setShadowOffset(id, dx: base[0], dy: v) }
        load()
    }

    // =========================================================================
    // Peças das linhas
    // =========================================================================

    /// Amostra · título 15 · interruptor (a linha de 37,5 da referência).
    private func headerLine(title: String, color: [Float]?, swatchId: String, switchId: String, checked: Bool,
                            swatchEnabled: Bool, onSwatch: @escaping (CGRect) -> Void,
                            onSwitch: @escaping (Bool) -> Void) -> some View {
        let s = BorderShadowMetrics.swatch
        let rtl = layoutDirection == .rightToLeft
        return HStack(spacing: 0) {
            GeometryReader { geo in
                Button {
                    let f = geo.frame(in: .global)
                    onSwatch(CGRect(x: rtl ? f.maxX - s : f.minX, y: f.midY - s / 2, width: s, height: s))
                } label: {
                    BorderShadowSwatch(color: color, shape: RoundedRectangle(cornerRadius: 4))
                        .frame(width: s, height: s)
                        .opacity(swatchEnabled ? 1 : 0.4)
                        .frame(width: 44, height: 44, alignment: .leading)
                        .contentShape(Rectangle())
                }
                .buttonStyle(AureaPressStyle(shrink: 1))
                .disabled(!swatchEnabled)
                .accessibilityLabel(title + ", " + AureaText.t("om_ap_bs_color"))
                .accessibilityIdentifier(swatchId)
            }
            .frame(width: 44, height: 44)
            Text(title).font(.aurea(size: 15)).foregroundStyle(AureaColors.text).lineLimit(1)
                .padding(.leading, 4).padding(.trailing, 8)
                .frame(maxWidth: .infinity, alignment: .leading)
            AureaToggle(checked: checked, enabled: !locked) { onSwitch($0) }
                .accessibilityLabel(title)
                .accessibilityValue(AureaText.t(checked ? "common_on" : "common_off"))
                .accessibilityIdentifier(switchId)
        }
        .frame(height: BorderShadowMetrics.header)
    }

    /// Uma linha de parâmetro: rótulo (toque = foco do trilho) · controle · ◆ da linha.
    private func paramRow<Content: View>(label: String, height: CGFloat, focused: Bool, look: KeyframeLook, canKey: Bool,
                                         keyTag: String, onFocus: @escaping () -> Void, onKey: @escaping () -> Void,
                                         @ViewBuilder content: () -> Content) -> some View {
        HStack(spacing: 0) {
            BorderShadowLabel(label: label, height: height - 6, active: focused, enabled: !locked, onTap: onFocus)
            Color.clear.frame(width: 4)
            content().frame(maxWidth: .infinity)
            Button {
                onFocus()
                onKey()
            } label: {
                KeyframeDiamondIcon(look: look, enabled: canKey)
                    .scaleEffect(PanelMetrics.railIcon / AureaDims.diamondIcon)
                    .frame(width: BorderShadowMetrics.key, height: height)
                    .contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle(shrink: 1))
            .disabled(!canKey)
            .accessibilityLabel(label + ": " + AureaText.t(look == .keyHere ? "panel_tirar_keyframe_daqui" : "panel_marcar_keyframe_aqui"))
            .accessibilityIdentifier(keyTag)
        }
        .frame(height: height)
    }

    /// `paramRow` de um parâmetro de efeito (o ◆ marca pelo mesmo caminho da lista de efeitos).
    private func effectRow<Content: View>(_ effect: UInt32, _ index: UInt32, label: String, height: CGFloat, keyTag: String,
                                          @ViewBuilder content: () -> Content) -> some View {
        let me = BorderShadowFocus.param(effect, index)
        return paramRow(label: label, height: height, focused: railFocus == me, look: effectLook(effect, index),
                        canKey: !locked && canKey(param(effect, index)), keyTag: keyTag,
                        onFocus: { focus = me },
                        onKey: {
                            guard !locked, let layer = model.primarySelection else { return }
                            toggleEffectKeys(layer, effect: effect, indices: [index])
                        },
                        content: content)
    }

    /// Direção [Dentro | Centro | Fora] (enum do motor: 0 Fora, 1 Centro, 2 Dentro).
    private func directionRow(_ id: UInt32) -> some View {
        let current = borderPosition(id)
        return effectRow(id, BorderShadowParam.borderPosition, label: AureaText.t("om_ap_bs_direction"), height: BorderShadowMetrics.row,
                         keyTag: "border.edit.direction.key") {
            BorderShadowSegmented(options: [AureaText.t("om_ap_bs_inside"), AureaText.t("om_ap_bs_center"), AureaText.t("om_ap_bs_outside")],
                                  selected: 2 - current, enabled: !locked, tag: "border.edit.direction") { choice in
                focus = .param(id, BorderShadowParam.borderPosition)
                writeScalar(id, BorderShadowParam.borderPosition, Float(2 - choice))
            }
            .accessibilityElement(children: .contain)
            .accessibilityIdentifier("border.edit.direction")
        }
    }

    /// Cor: amostra que abre o seletor ancorado (o balão inteiro = um passo de desfazer).
    private func colorRow(_ id: UInt32, index: UInt32, label: String, tag: String) -> some View {
        let shown = param(id, index).map { AureaColorSpace.engineToDisplay($0.value) }
        return effectRow(id, index, label: label, height: BorderShadowMetrics.row, keyTag: tag + ".key") {
            HStack(spacing: 0) {
                Spacer(minLength: 0)
                GeometryReader { geo in
                    Button {
                        let f = geo.frame(in: .global)
                        focus = .param(id, index)
                        openEffectColor(id, index, label: label, anchor: CGRect(x: f.midX - 23, y: f.midY - 13, width: 46, height: 26))
                    } label: {
                        BorderShadowSwatch(color: shown, shape: RoundedRectangle(cornerRadius: 5))
                            .frame(width: 46, height: 26)
                            .frame(width: 64, height: 44)
                            .contentShape(Rectangle())
                    }
                    .buttonStyle(AureaPressStyle(shrink: 1))
                    .disabled(locked)
                    .accessibilityLabel(label)
                    .accessibilityIdentifier(tag)
                }
                .frame(width: 64, height: 44)
            }
        }
    }

    /// Número: régua de riscos (agulha em destaque) + caixa de valor (toque = teclado).
    private func rulerRow(_ id: UInt32, index: UInt32, label: String, tag: String, step: Float, minimum: Float, maximum: Float,
                          decimals: Int, unit: String, gesture: String) -> some View {
        let value = scalar(id, index)
        let me = BorderShadowFocus.param(id, index)
        let send: (Float) -> Void = { v in writeScalar(id, index, decimals == 0 ? v.rounded() : v) }
        return effectRow(id, index, label: label, height: BorderShadowMetrics.ruler, keyTag: tag + ".key") {
            HStack(spacing: 3) {
                BorderShadowRuler(value: value, step: step, minimum: minimum, maximum: maximum, enabled: !locked,
                                  onStart: { focus = me; model.beginGesture(gesture) },
                                  onValue: send,
                                  onEnd: { model.endGesture(); load() })
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .accessibilityLabel(label)
                    .accessibilityIdentifier(tag + ".ruler")
                BorderShadowValueBox(text: numeroPtBr(value, casas: decimals) + (unit == "%" ? "%" : ""), enabled: !locked) {
                    focus = me
                    model.numericKeypad = KeypadRequest(title: label, value: value, unit: unit, min: minimum, max: maximum, decimals: decimals) { v in
                        model.beginGesture(gesture)
                        send(Swift.min(Swift.max(v, minimum), maximum))
                        model.endGesture()
                        load()
                    }
                }
                .accessibilityLabel(label)
                .accessibilityIdentifier(tag)
            }
        }
    }

    // =========================================================================
    // Escrita (o mesmo contrato do EffectsView: animado = keyframe no cabeçote)
    // =========================================================================

    private func writeScalar(_ effect: UInt32, _ index: UInt32, _ value: Float) {
        guard value.isFinite, !locked, let layer = model.primarySelection, let p = param(effect, index) else { return }
        let lo = Swift.min(p.hardMin, p.minValue), hi = Swift.max(p.hardMax, p.maxValue)
        let v = hi >= lo ? Swift.min(Swift.max(value, lo), hi) : value
        if p.animated {
            model.engine.keyParameter(layer, property: 31, effect: effect, param: index * 4, time: model.localPlayhead, value: v)
        } else {
            model.engine.setEffect(effect, forLayer: layer, paramIndex: index, value: v)
        }
        model.commitPendingCommands()
        model.refreshSelectedLayer()
        load()
    }

    /// O vetor INTEIRO num comando só (cor): um componente por vez relia o parâmetro velho.
    private func writeVector(_ effect: UInt32, _ index: UInt32, _ values: [Float]) {
        guard !locked, let layer = model.primarySelection, let p = param(effect, index), values.allSatisfy(\.isFinite) else { return }
        let count = Swift.min(fxComponentCount(Int(p.type)), values.count)
        guard count > 0 else { return }
        var vector = Array((p.value + [0, 0, 0, 1]).prefix(4))
        for component in 0..<count { vector[component] = values[component] }
        if p.animated {
            for component in 0..<count {
                model.engine.keyParameter(layer, property: 31, effect: effect, param: index * 4 + UInt32(component),
                                          time: model.localPlayhead, value: vector[component])
            }
        } else if count == 1 {
            model.engine.setEffect(effect, forLayer: layer, paramIndex: index, value: vector[0])
        } else {
            model.engine.setEffectColor(effect, forLayer: layer, paramIndex: index, r: vector[0], g: vector[1], b: vector[2], a: vector[3])
        }
        model.commitPendingCommands()
        model.refreshSelectedLayer()
        load()
    }

    private func openEffectColor(_ effect: UInt32, _ index: UInt32, label: String, anchor: CGRect) {
        guard !locked, let layer = model.primarySelection, let p = param(effect, index) else { return }
        model.beginGesture("cor " + label)
        model.colorSheet = ColorSheetRequest(title: label, initial: AureaColorSpace.engineToDisplay(p.value), anchor: anchor,
                                             onChange: { r, g, b, a in
                                                 guard model.primarySelection == layer else { return }
                                                 writeVector(effect, index, AureaColorSpace.displayToEngine(r, g, b, a))
                                             },
                                             onDone: { model.endGesture() })
    }
}

// =============================================================================
// Controles próprios do painel (medidas da referência; pares das peças Fx* do
// EffectsControls.kt usadas no Android)
// =============================================================================

/// A RÉGUA DE RISCOS: risco a cada 5,5 pt (0,5 pt, «risco»; o maior a cada 5 no
/// texto), esmaecendo até as bordas; agulha de 1,5 pt em destaque. 1 risco = 1
/// `step`; dedo para a esquerda aumenta; o valor anda de risco em risco e fica
/// preso na faixa (estendida até o valor de partida). Um gesto = um passo de desfazer.
private struct BorderShadowRuler: View {
    let value: Float
    let step: Float
    let minimum: Float
    let maximum: Float
    let enabled: Bool
    let onStart: () -> Void
    let onValue: (Float) -> Void
    let onEnd: () -> Void
    @State private var live: Float?
    @State private var start: Float = 0
    @State private var axis = 0
    @State private var active = false
    @GestureState private var dragging = false

    // Explícito: com estado privado o inicializador sintetizado ficaria privado.
    init(value: Float, step: Float, minimum: Float, maximum: Float, enabled: Bool = true,
         onStart: @escaping () -> Void, onValue: @escaping (Float) -> Void, onEnd: @escaping () -> Void) {
        self.value = value; self.step = step; self.minimum = minimum; self.maximum = maximum
        self.enabled = enabled; self.onStart = onStart; self.onValue = onValue; self.onEnd = onEnd
    }

    var body: some View {
        Canvas { context, size in draw(context, size: size, value: live ?? value) }
            .contentShape(Rectangle())
            .simultaneousGesture(drag, including: enabled ? .all : .none)
            .onChange(of: dragging) { held in if !held { finish() } }
            .onChange(of: enabled) { on in if !on { finish() } }
            .onDisappear { finish() }
            .keepLtr()
            .accessibilityElement()
            .accessibilityValue(numeroPtBr(live ?? value, casas: 1))
            .accessibilityAdjustableAction { direction in
                guard enabled else { return }
                let delta: Float = direction == .increment ? step : -step
                onStart()
                onValue(clampToRange(value + delta))
                onEnd()
            }
    }

    private var drag: some Gesture {
        DragGesture(minimumDistance: 4)
            .updating($dragging) { _, held, _ in held = true }
            .onChanged { g in
                // Trava o eixo no primeiro movimento: vertical é a rolagem do painel.
                if axis == 0 { axis = abs(g.translation.width) >= abs(g.translation.height) ? 1 : 2 }
                guard axis == 1 else { return }
                if !active {
                    active = true
                    start = value.isFinite ? value : 0
                    onStart()
                }
                let walked = Float(-g.translation.width)
                let raw = start + walked / Float(BorderShadowMetrics.tickSpacing) * step
                let v = clampToDrag(snap(raw))
                if v != live {
                    live = v
                    onValue(v)
                }
            }
            .onEnded { _ in finish() }
    }

    private func snap(_ raw: Float) -> Float {
        guard step > 0, step.isFinite, raw.isFinite else { return raw }
        return (raw / step).rounded() * step
    }

    /// A faixa de UM arrasto: a da régua, estendida até o valor de partida.
    private func clampToDrag(_ v: Float) -> Float {
        let lo = Swift.min(Swift.min(minimum, maximum), start)
        let hi = Swift.max(Swift.max(minimum, maximum), start)
        return Swift.min(Swift.max(v, lo), hi)
    }

    private func clampToRange(_ v: Float) -> Float {
        Swift.min(Swift.max(v, Swift.min(minimum, maximum)), Swift.max(minimum, maximum))
    }

    private func finish() {
        axis = 0
        if active {
            active = false
            live = nil
            onEnd()
        }
    }

    private func draw(_ context: GraphicsContext, size: CGSize, value: Float) {
        let pad: CGFloat = 8
        let top = pad, bottom = size.height - pad
        guard bottom > top, size.width > 0 else { return }
        let spacing = BorderShadowMetrics.tickSpacing
        let center = size.width / 2
        let v = Double(value.isFinite ? value : 0)
        let pos = step > 0 && step.isFinite ? v / Double(step) : 0
        if abs(pos) < 1e7 {
            let reach = Double(center / spacing)
            let first = Int((pos - reach).rounded(.down)) - 1
            let last = Int((pos + reach).rounded(.up)) + 1
            if last > first && last - first < 4000 {
                for k in first...last {
                    let x = center + CGFloat(Double(k) - pos) * spacing
                    let b = FxParamRowMath.tickBrightness(x: x, width: size.width)
                    guard b > 0 else { continue }
                    let major = ((k % 5) + 5) % 5 == 0
                    var line = Path()
                    line.move(to: CGPoint(x: x, y: top))
                    line.addLine(to: CGPoint(x: x, y: bottom))
                    context.stroke(line, with: .color((major ? AureaColors.text : AureaColors.fxTick).opacity(Double(b))), lineWidth: 0.5)
                }
            }
        }
        var needle = Path()
        needle.move(to: CGPoint(x: center, y: 0))
        needle.addLine(to: CGPoint(x: center, y: size.height))
        context.stroke(needle, with: .color(enabled ? AureaColors.accent : AureaColors.fxDisabledText), lineWidth: 1.5)
    }
}

/// A CAIXA DE VALOR 55×45: raio 4, «véu 20 %», número 8–14 pt alinhado ao fim, sempre LTR.
private struct BorderShadowValueBox: View {
    let text: String
    var enabled: Bool = true
    let onTap: () -> Void

    var body: some View {
        Button(action: onTap) {
            Text(text)
                .font(.aurea(size: PanelMetrics.fieldValue).monospacedDigit())
                .foregroundStyle(enabled ? AureaColors.text : AureaColors.fxDisabledText)
                .lineLimit(1)
                .minimumScaleFactor(8.0 / PanelMetrics.fieldValue)
                .padding(.horizontal, 6)
                .frame(width: BorderShadowMetrics.valueW, height: BorderShadowMetrics.valueH, alignment: .trailing)
                .background(AureaColors.fxValueVeil, in: RoundedRectangle(cornerRadius: 4))
                .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .disabled(!enabled)
        .keepLtr()
    }
}

/// O RÓTULO DA LINHA: raio 4, «rótulo» (ativo: «afundado» e texto em destaque),
/// texto 8–12 pt em até 2 linhas. Toque = foco do trilho.
private struct BorderShadowLabel: View {
    let label: String
    let height: CGFloat
    let active: Bool
    let enabled: Bool
    let onTap: () -> Void

    var body: some View {
        Button(action: onTap) {
            Text(label)
                .font(.aurea(size: PanelMetrics.label))
                .multilineTextAlignment(.center)
                .lineLimit(2)
                .minimumScaleFactor(8.0 / PanelMetrics.label)
                .foregroundStyle(!enabled ? AureaColors.fxDisabledText : active ? AureaColors.accent : AureaColors.text)
                .padding(3)
                .frame(width: BorderShadowMetrics.label, height: Swift.max(0, height))
                .background(active && enabled ? AureaColors.fxSunken : AureaColors.fxLabel, in: RoundedRectangle(cornerRadius: 4))
                .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .disabled(!enabled)
        .accessibilityAddTraits(active ? .isSelected : [])
    }
}

/// ESCOLHA SEGMENTADA: botões de 27,5 pt em «segmento» / «segmento ativo» (raio 5);
/// o alvo de toque de cada botão tem a altura da linha (44). Ids "\(tag).\(i)".
private struct BorderShadowSegmented: View {
    let options: [String]
    let selected: Int
    let enabled: Bool
    let tag: String
    let onSelect: (Int) -> Void

    var body: some View {
        HStack(spacing: 1) {
            ForEach(Array(options.enumerated()), id: \.offset) { index, option in
                let on = index == selected
                Button { onSelect(index) } label: {
                    Text(option)
                        .font(.aurea(size: PanelMetrics.label))
                        .foregroundStyle(!enabled ? AureaColors.fxDisabledText : on ? AureaColors.accent : AureaColors.text)
                        .lineLimit(1).truncationMode(.tail).minimumScaleFactor(0.8)
                        .padding(.horizontal, 4)
                        .frame(maxWidth: .infinity)
                        .frame(height: PanelMetrics.fieldHeight)
                        .background(on ? AureaColors.fxSegmentOn : AureaColors.fxSegment)
                        .frame(height: 44)
                        .contentShape(Rectangle())
                }
                .buttonStyle(AureaPressStyle(shrink: 1))
                .disabled(!enabled)
                .accessibilityAddTraits(on ? .isSelected : [])
                .accessibilityIdentifier("\(tag).\(index)")
            }
        }
        .mask(alignment: .center) { RoundedRectangle(cornerRadius: 5).frame(height: 27.5) }
    }
}

/// Amostra de cor com o xadrez da transparência por baixo.
private struct BorderShadowSwatch<S: Shape>: View {
    let color: [Float]?
    let shape: S

    var body: some View {
        Canvas { context, size in
            context.fill(Path(CGRect(origin: .zero, size: size)), with: .color(AureaColors.chipHigh))
            let cell: CGFloat = 5
            var y: CGFloat = 0
            var row = 0
            while y < size.height {
                var x: CGFloat = row % 2 == 0 ? cell : 0
                while x < size.width {
                    context.fill(Path(CGRect(x: x, y: y, width: cell, height: cell)), with: .color(AureaColors.chip))
                    x += cell * 2
                }
                y += cell
                row += 1
            }
            if let color {
                context.fill(Path(CGRect(origin: .zero, size: size)), with: .color(AureaColorSpace.color(color)))
            }
        }
        .clipShape(shape)
        .overlay(shape.stroke(AureaColors.fxSunken, lineWidth: 1))
        .accessibilityHidden(true)
    }
}
