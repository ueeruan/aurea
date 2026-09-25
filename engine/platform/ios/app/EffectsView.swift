// Port of EffectsPanel.kt and EffectStackCard. Project data stays in the core.
import SwiftUI
import UIKit

@MainActor
struct EffectsView: View {
    @EnvironmentObject private var model: AureaModel
    @State private var browsing = false
    @State private var openId: UInt32?
    @State private var known: Set<UInt32> = []
    @State private var loadedLayer: Int64?
    @State private var selected: EffectParamSelection?
    @State private var advanced: Set<UInt32> = []
    @State private var expressionLooks: [UInt32: ExpressionLook] = [:]
    @State private var dragId: UInt32?
    @State private var dragOffset: CGFloat = 0
    @State private var lastTranslation: CGFloat = 0
    @State private var dragOrder: [UInt32]?
    @State private var cardFrames: [UInt32: CGRect] = [:]
    private var ordered: [EffectItem] {
        let byId = Dictionary(uniqueKeysWithValues: model.effects.map { ($0.effectId, $0) })
        return (dragOrder ?? model.effects.map(\.effectId)).compactMap { byId[$0] }
    }
    private var selectedParam: EffectParamItem? {
        guard let selected, selected.effect == openId else { return nil }
        return model.effectParams.first { $0.index == selected.param }
    }
    private var canAnimate: Bool { selectedParam.map { $0.flags & 1 != 0 && fxComponentCount(Int($0.type)) > 0 } ?? false }
    private var railLook: KeyframeLook {
        guard let selected else { return .none }
        return look(effect: selected.effect, param: selected.param)
    }
    private var selectedTrack: [KeyframeItem] {
        guard let selected, let layer = model.primarySelection else { return [] }
        return (model.keyframes[layer] ?? []).filter { $0.property == 31 && $0.effectIndex == selected.effect && $0.paramIndex == selected.param * 4 + UInt32(selected.component) }.sorted { $0.time < $1.time }
    }
    var body: some View {
        VStack(spacing: 0) {
            PanelHeader(title: AureaText.t("panel_efeitos"), onBack: { model.panel = .none })
            HStack(spacing: 0) {
                rail
                ScrollView {
                    VStack(spacing: 0) {
                        ForEach(ordered) { effect in
                            card(effect).padding(.bottom, 8)
                                .background(GeometryReader { geometry in
                                    Color.clear.preference(key: EffectCardFrames.self, value: [effect.effectId: geometry.frame(in: .named("effect-stack"))])
                                })
                                .offset(y: dragId == effect.effectId ? dragOffset : 0)
                                .zIndex(dragId == effect.effectId ? 1 : 0)
                        }
                        footer
                    }.padding(.init(top: 8, leading: 2, bottom: 16, trailing: 12))
                }.coordinateSpace(name: "effect-stack")
                    .onPreferenceChange(EffectCardFrames.self) { cardFrames = $0 }
            }.frame(maxHeight: .infinity)
        }
        .background(AureaColors.editorPanel)
        .onAppear { enterLayer(); model.refreshSelectedLayer(); refreshExpressions() }
        .onChange(of: model.primarySelection) { _ in enterLayer() }
        .onChange(of: model.effects.map(\.effectId)) { ids in
            let added = Set(ids).subtracting(known); known = Set(ids)
            if let id = ids.last(where: { added.contains($0) }) { open(id) }
            else if let openId, !ids.contains(openId) { closeCard() }
        }
        .onChange(of: model.status.modelRevision) { _ in refreshExpressions() }
        .onChange(of: model.status.playhead) { _ in refreshExpressions() }
        .fullScreenCover(isPresented: $browsing) { EffectsBrowser(onDismiss: { browsing = false }).environmentObject(model) }
        .onDisappear { finishReorder(); model.endGesture() }
    }
    @ViewBuilder private var rail: some View {
        if openId == nil {
            VStack(spacing: 0) {
                Button { model.panel = .none } label: { MaterialGlyph("rounded.ChevronLeft", size: 24).frame(width: 46, height: 56) }.buttonStyle(AureaPressStyle(shrink: 1))
                Spacer(minLength: 0)
                Button { listMenu() } label: { CupertinoGlyph.text(CupertinoGlyph.Ellipsis, size: 24, color: AureaColors.text).frame(width: 46, height: 56) }.buttonStyle(AureaPressStyle(shrink: 1))
            }.frame(width: 46).frame(maxHeight: .infinity)
        } else {
            LeftRail(keyframeLook: railLook,
                     onKeyframe: canAnimate ? { toggleSelectedKey() } : nil,
                     curveAnimated: railLook != .none,
                     onCurve: selectedTrack.count >= 2 ? { openCurve() } : nil,
                     expression: selected.map { expressionLooks[$0.param] ?? .none } ?? .none,
                     onExpression: canAnimate ? { openExpression() } : nil,
                     onBack: { closeCard() })
        }
    }
    private func enterLayer() {
        guard loadedLayer != model.primarySelection else { return }
        loadedLayer = model.primarySelection; known = Set(model.effects.map(\.effectId))
        closeCard(); advanced = []; expressionLooks = [:]
        guard model.curveProperty == 31, model.curveSelectedTime != nil,
              model.effects.contains(where: { $0.effectId == model.curveEffect }) else { return }
        let entry = EffectParamSelection(effect: model.curveEffect, param: model.curveParam / 4, component: Int(model.curveParam % 4))
        selected = entry; open(entry.effect)
        if parameterGroups(entry.effect).rest.contains(where: { $0.index == entry.param }) { advanced.insert(entry.effect) }
    }
    private func open(_ id: UInt32) {
        guard let layer = model.primarySelection else { return }
        openId = id; model.loadParams(layerId: layer, effectId: id)
        if selected?.effect != id {
            selected = parameterGroups(id).main.first(where: { fxComponentCount(Int($0.type)) > 0 }).map { EffectParamSelection(effect: id, param: $0.index, component: 0) }
        }
        refreshExpressions()
    }
    private func closeCard() { openId = nil; selected = nil; model.selectedEffectId = nil }
    private func currentParam(_ index: UInt32) -> EffectParamItem? { model.effectParams.first { $0.index == index } }
    private func slot(_ param: EffectParamItem) -> FxParamSlot {
        FxParamSlot(index: Int(param.index), type: Int(param.type), min: param.minValue, max: param.maxValue, label: param.label, unit: param.unit, enumLabels: param.enumLabels)
    }
    private func parameterGroups(_ id: UInt32) -> (main: [EffectParamItem], rest: [EffectParamItem]) {
        let visible = model.effectParams.filter { $0.flags & 16 == 0 }
        let type = model.effects.first { $0.effectId == id }?.typeId ?? 0
        let groups = fxSplitPrincipal(type, visible.map { slot($0) })
        let byIndex = Dictionary(uniqueKeysWithValues: visible.map { (Int($0.index), $0) })
        return (groups.main.compactMap { byIndex[$0.index] }, groups.rest.compactMap { byIndex[$0.index] })
    }
    private func display(_ param: EffectParamItem, effectId: UInt32) -> FxParamDisplay {
        fxParamDisplay(model.effects.first { $0.effectId == effectId }?.typeId ?? 0, slot(param))
    }
    private func card(_ effect: EffectItem) -> some View {
        let expanded = openId == effect.effectId, lifted = dragId == effect.effectId
        return VStack(spacing: 0) {
            HStack(spacing: 0) {
                Button { expanded ? closeCard() : open(effect.effectId) } label: {
                    HStack(spacing: 14) {
                        CupertinoGlyph.text(expanded ? CupertinoGlyph.ArrowtriangleDownFill : CupertinoGlyph.ArrowtriangleRightFill, size: 13, color: AureaColors.text)
                        Text(fxEffectDisplayName(effect.typeId, effect.name)).font(.aurea(size: 17, weight: .semibold)).foregroundStyle(AureaColors.text)
                            .lineLimit(1).truncationMode(.tail).opacity(effect.enabled ? 1 : 0.45)
                        Spacer(minLength: 0)
                    }.frame(maxWidth: .infinity).frame(height: 52).contentShape(Rectangle())
                }.buttonStyle(AureaPressStyle(shrink: 1))
                if expanded {
                    cardButton(CupertinoGlyph.Ellipsis) { effectMenu(effect) }
                    cardButton(CupertinoGlyph.Trash) { remove(effect) }
                } else {
                    cardButton(effect.enabled ? CupertinoGlyph.Eye : CupertinoGlyph.EyeSlash, tint: effect.enabled ? AureaColors.text : AureaColors.muted) { enable(effect, !effect.enabled) }
                    CupertinoGlyph.text(CupertinoGlyph.LineHorizontal3, size: 22, color: lifted ? AureaColors.accent : AureaColors.muted)
                        .frame(width: 48, height: 48).contentShape(Rectangle())
                        .highPriorityGesture(DragGesture(minimumDistance: 4).onChanged { reorder(effect, value: $0) }.onEnded { _ in finishReorder() })
                }
            }.frame(height: 52)
            if expanded {
                VStack(spacing: 0) {
                    if !effect.known { PanelNotice(AureaText.t("panel_este_efeito_saiu_catalogo_ele_nao")) }
                    else if effect.typeId == fxEffectTypeId("aurea.time.remap") { TimeRemapEffectEditor() }
                    else {
                        let groups = parameterGroups(effect.effectId)
                        if groups.main.isEmpty && groups.rest.isEmpty { PanelNotice(AureaText.t("panel_este_efeito_nao_tem_ajustes")) }
                        ForEach(groups.main) { param in parameter(param, effect: effect.effectId) }
                        if !groups.rest.isEmpty {
                            AdvancedToggle(open: advanced.contains(effect.effectId), count: groups.rest.count) {
                                if advanced.contains(effect.effectId) { advanced.remove(effect.effectId) } else { advanced.insert(effect.effectId) }
                            }
                            if advanced.contains(effect.effectId) { ForEach(groups.rest) { param in parameter(param, effect: effect.effectId) } }
                        }
                    }
                }.opacity(effect.enabled ? 1 : 0.45)
            }
        }.padding(.leading, 10).padding(.trailing, 4).padding(.bottom, expanded ? 8 : 0)
            .background(lifted ? AureaColors.surfaceHigh : AureaColors.surface, in: RoundedRectangle(cornerRadius: 12))
            .overlay { if lifted { RoundedRectangle(cornerRadius: 12).stroke(AureaColors.accent, lineWidth: 1) } }
    }
    private func cardButton(_ glyph: Character, tint: Color = AureaColors.text, action: @escaping () -> Void) -> some View {
        Button(action: action) { CupertinoGlyph.text(glyph, size: 22, color: tint).frame(width: 48, height: 48) }.buttonStyle(AureaPressStyle())
    }
    @ViewBuilder private func parameter(_ param: EffectParamItem, effect: UInt32) -> some View {
        switch Int(param.type) {
        case fxParamFloat, fxParamInt, fxParamAngle: numberRow(param, effect: effect, component: 0)
        case fxParamPoint2D, fxParamPoint3D:
            ForEach(0..<fxComponentCount(Int(param.type)), id: \.self) { component in numberRow(param, effect: effect, component: component) }
        case fxParamBool:
            customRow(param, effect: effect) {
                HStack { Spacer(minLength: 0); AureaToggle(checked: param.scalar >= 0.5) { on in
                    selected = EffectParamSelection(effect: effect, param: param.index, component: 0)
                    writeComponent(param.index, effect: effect, component: 0, value: on ? 1 : 0)
                }; Spacer().frame(width: 4) }
            }
        case fxParamEnum:
            customRow(param, effect: effect) {
                let options = enumOptions(param)
                ChoiceChips(options, selected: min(max(0, Int(param.scalar.rounded())), options.count - 1)) { index in
                    selected = EffectParamSelection(effect: effect, param: param.index, component: 0)
                    writeComponent(param.index, effect: effect, component: 0, value: Float(index))
                }
            }
        case fxParamColor:
            customRow(param, effect: effect) {
                HStack { Spacer(minLength: 0)
                    Button {
                        let target = EffectParamSelection(effect: effect, param: param.index, component: 0)
                        selected = target; model.beginGesture("cor")
                        model.colorSheet = ColorSheetRequest(title: display(param, effectId: effect).label,
                            initial: AureaColorSpace.engineToDisplay(param.value), onChange: { r, g, b, a in
                                writeVector(param.index, effect: effect, values: AureaColorSpace.displayToEngine(r, g, b, a))
                            }, onDone: { model.endGesture() })
                    } label: {
                        AureaColorSwatch(color: color(param.value)).frame(width: 30, height: 30).clipShape(RoundedRectangle(cornerRadius: 6))
                    }.buttonStyle(AureaPressStyle())
                    Spacer().frame(width: 4)
                }
            }
        default:
            customRow(param, effect: effect) { Text(AureaText.t("panel_ainda_nao_editavel_app")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted) }
        }
    }
    private func enumOptions(_ param: EffectParamItem) -> [String] {
        if !param.enumLabels.isEmpty { return param.enumLabels }
        let span = param.maxValue - param.minValue
        let count = span.isFinite ? max(1, min(256, Int(span.clamped(to: 0...255).rounded()) + 1)) : 1
        return (0..<count).map { String($0 + 1) }
    }
    private func numberRow(_ param: EffectParamItem, effect: UInt32, component: Int) -> some View {
        let d = display(param, effectId: effect), key = EffectParamSelection(effect: effect, param: param.index, component: component)
        let label = Int(param.type) == fxParamPoint2D || Int(param.type) == fxParamPoint3D ? axisLabel(d.label, component: component) : d.label
        let lo = d.toDisplay(param.minValue), hi = d.toDisplay(param.maxValue)
        let range = param.maxValue - param.minValue
        let step: Float = Int(param.type) == fxParamAngle ? 0.5 : range.isFinite && range > 0 ? min(range / 500, 2) : 0.5
        let shown = d.toDisplay(component < param.value.count ? param.value[component] : 0)
        let send: (Float) -> Void = { value in
            let coreValue = min(param.maxValue, max(param.minValue, d.toEngine(value)))
            writeComponent(param.index, effect: effect, component: component, value: Int(param.type) == fxParamInt ? coreValue.rounded() : coreValue)
        }
        return EffectNumberRow(label: label, value: shown, unitsPerPoint: step * d.scale, minimum: min(lo, hi), maximum: max(lo, hi), suffix: d.suffix, decimals: d.decimals,
                               selected: selected == key, look: look(effect: effect, param: param.index, component: component), expression: expressionLooks[param.index] ?? .none,
                               onSelect: { selected = key }, onMenu: { paramMenu(param, effect: effect) },
                               onBegin: { model.beginGesture("ajustar " + label) }, onValue: send, onEnd: { model.endGesture() }, onKeypad: {
                                   selected = key
                                   model.numericKeypad = KeypadRequest(title: label, value: shown, unit: d.suffix, min: min(lo, hi), max: max(lo, hi), decimals: d.decimals, onValue: send)
                               })
    }
    private func customRow<Content: View>(_ param: EffectParamItem, effect: UInt32, @ViewBuilder content: () -> Content) -> some View {
        let key = EffectParamSelection(effect: effect, param: param.index, component: 0)
        return HStack(spacing: 8) {
            EffectPropertyLabel(label: display(param, effectId: effect).label, selected: selected?.effect == effect && selected?.param == param.index,
                                look: look(effect: effect, param: param.index), expression: expressionLooks[param.index] ?? .none,
                                onSelect: { selected = key }, onMenu: { paramMenu(param, effect: effect) })
            content().frame(maxWidth: .infinity, alignment: .leading)
        }.frame(minHeight: 48)
    }
    private func axisLabel(_ label: String, component: Int) -> String {
        let cuts = [" do ", " da ", " dos ", " das "].compactMap { label.range(of: $0)?.lowerBound }
        let shortened = cuts.min().map { String(label[..<$0]) } ?? label
        return shortened + " " + ["X", "Y", "Z"][component]
    }
    private func look(effect: UInt32, param: UInt32, component: Int? = nil) -> KeyframeLook {
        guard let layer = model.primarySelection else { return .none }
        let keys = (model.keyframes[layer] ?? []).filter { $0.property == 31 && $0.effectIndex == effect && $0.paramIndex / 4 == param && (component == nil || $0.paramIndex % 4 == UInt32(component!)) }
        return keys.contains(where: { $0.time == model.localPlayhead }) ? .keyHere : keys.isEmpty ? .none : .animated
    }
    private func refreshExpressions() {
        guard let layer = model.primarySelection, let effect = openId else { expressionLooks = [:]; return }
        var looks: [UInt32: ExpressionLook] = [:]
        for param in model.effectParams where param.flags & 1 != 0 {
            var result = ExpressionLook.none
            for component in 0..<fxComponentCount(Int(param.type)) {
                let info = model.engine.expression(layer, property: 31, effect: effect, param: param.index * 4 + UInt32(component))
                guard (info["exists"] as? NSNumber)?.boolValue == true else { continue }
                if !(info["error"] as? String ?? "").isEmpty { result = .error; break }
                result = (info["enabled"] as? NSNumber)?.boolValue == true ? .ok : result == .ok ? .ok : .off
            }
            looks[param.index] = result
        }
        expressionLooks = looks
    }
    private func toggleSelectedKey() {
        guard let selection = selected, let param = currentParam(selection.param), let layer = model.primarySelection,
              model.selectedLayer?.locked != true, param.flags & 1 != 0 else { return }
        let here = (model.keyframes[layer] ?? []).filter { $0.property == 31 && $0.effectIndex == selection.effect && $0.paramIndex / 4 == param.index && $0.time == model.localPlayhead }
        model.beginGesture("keyframe de efeito")
        if here.isEmpty {
            for component in 0..<fxComponentCount(Int(param.type)) {
                model.engine.keyParameter(layer, property: 31, effect: selection.effect, param: param.index * 4 + UInt32(component), time: model.localPlayhead, value: component < param.value.count ? param.value[component] : 0)
            }
        } else {
            for key in here { model.engine.editTrackKey(layer, property: 31, effect: key.effectIndex, param: key.paramIndex, time: key.time, action: 1, value: key.value, targetTime: key.time, interpolation: key.interpolation, handles: []) }
        }
        model.endGesture(); model.commitPendingCommands(); model.refreshModel(force: true)
    }
    private func openCurve() {
        guard let selected else { return }
        let track = selectedTrack
        guard track.count >= 2 else { return }
        let index = min(track.count - 2, track.lastIndex(where: { $0.time <= model.localPlayhead }) ?? 0)
        let key = track[index]
        model.openCurve(property: 31, effect: selected.effect, param: selected.param * 4 + UInt32(selected.component), time: key.time)
    }
    private func openExpression() {
        guard let selected, let param = selectedParam, let layer = model.primarySelection, canAnimate else { return }
        let d = display(param, effectId: selected.effect)
        let tracks = (0..<fxComponentCount(Int(param.type))).map { ExpressionTrack(property: 31, effect: selected.effect, param: param.index * 4 + UInt32($0)) }
        model.expressionSheet = ExpressionRequest(layer: layer, label: d.label, tracks: tracks, scale: d.scale, unit: d.suffix)
    }
    // Same EditorStore contract: editing one axis creates only its component key.
    private func writeComponent(_ index: UInt32, effect: UInt32, component: Int, value: Float) {
        guard value.isFinite, let layer = model.primarySelection, model.selectedLayer?.locked != true,
              let param = currentParam(index), param.flags & 16 == 0 else { return }
        let count = fxComponentCount(Int(param.type))
        guard component >= 0 && component < count else { return }
        if param.animated {
            model.engine.keyParameter(layer, property: 31, effect: effect, param: index * 4 + UInt32(component), time: model.localPlayhead, value: value)
        } else if count == 1 { model.engine.setEffect(effect, forLayer: layer, paramIndex: index, value: value) }
        else {
            var vector = Array((param.value + [0, 0, 0, 1]).prefix(4)); vector[component] = value
            model.engine.setEffectColor(effect, forLayer: layer, paramIndex: index, r: vector[0], g: vector[1], b: vector[2], a: vector[3])
        }
        model.commitPendingCommands(); model.refreshSelectedLayer()
    }
    private func writeVector(_ index: UInt32, effect: UInt32, values: [Float]) {
        guard let param = currentParam(index), model.selectedLayer?.locked != true, values.allSatisfy(\.isFinite) else { return }
        for component in 0..<min(fxComponentCount(Int(param.type)), values.count) { writeComponent(index, effect: effect, component: component, value: values[component]) }
    }
    private func color(_ values: [Float]) -> Color {
        let v = Array((values + [0, 0, 0, 1]).prefix(4))
        return Color(.sRGBLinear, red: Double(v[0]), green: Double(v[1]), blue: Double(v[2]), opacity: Double(v[3]))
    }
    private func enable(_ effect: EffectItem, _ on: Bool) {
        guard let layer = model.primarySelection, model.selectedLayer?.locked != true else { return }
        model.mutate { $0.setEffect(effect.effectId, forLayer: layer, enabled: on) }; model.refreshSelectedLayer()
    }
    private func move(_ effect: EffectItem, to index: Int) {
        guard let layer = model.primarySelection, model.selectedLayer?.locked != true, model.effects.indices.contains(index) else { return }
        model.mutate { $0.moveEffect(effect.effectId, inLayer: layer, to: UInt32(index)) }; model.refreshSelectedLayer()
    }
    private func remove(_ effect: EffectItem) {
        guard let layer = model.primarySelection, model.selectedLayer?.locked != true else { return }
        model.mutate { $0.removeEffect(effect.effectId, fromLayer: layer) }
        if openId == effect.effectId { closeCard() }
        model.refreshSelectedLayer()
    }
    private func reset(_ effect: EffectItem) {
        guard let layer = model.primarySelection else { return }
        model.loadParams(layerId: layer, effectId: effect.effectId)
        let params = model.effectParams
        model.beginGesture("redefinir efeito")
        for param in params where param.flags & 16 == 0 { writeVector(param.index, effect: effect.effectId, values: param.defaultValue) }
        model.endGesture(); model.refreshSelectedLayer()
    }
    private func listMenu() {
        var actions: [(String, () -> Void)] = [(AureaText.t("panel_adicionar_efeito"), { browsing = true })]
        if !model.effects.isEmpty { actions.append((AureaText.t("panel_copiar_efeitos"), { if let layer = model.primarySelection { model.engine.copyEffects(layer) } })) }
        if model.engine.clipboardState & 4 != 0 {
            actions.append((AureaText.t("panel_colar_efeitos"), {
                model.engine.pasteEffects(model.selection.map { NSNumber(value: $0) }); model.refreshModel(force: true)
            }))
        }
        if !model.effects.isEmpty {
            let anyOn = model.effects.contains(where: \.enabled)
            actions.append((AureaText.t(anyOn ? "panel_desligar_todos" : "panel_ligar_todos"), {
                let effects = model.effects
                model.beginGesture("ligar efeitos")
                for effect in effects where effect.enabled == anyOn { enable(effect, !anyOn) }
                model.endGesture()
            }))
        }
        model.actionSheet = ActionSheetRequest(title: AureaText.t("panel_efeitos_camada"), items: actions)
    }
    private func effectMenu(_ effect: EffectItem) {
        var actions: [(String, () -> Void)] = []
        if effect.known {
            actions.append((AureaText.t(effect.enabled ? "panel_desligar_efeito" : "panel_ligar_efeito"), { enable(effect, !effect.enabled) }))
            actions.append((AureaText.t("panel_redefinir_efeito"), { reset(effect) }))
            if let index = model.effects.firstIndex(where: { $0.effectId == effect.effectId }) {
                if index > 0 { actions.append((AureaText.t("panel_mover_cima"), { move(effect, to: index - 1) })) }
                if index < model.effects.count - 1 { actions.append((AureaText.t("panel_mover_baixo"), { move(effect, to: index + 1) })) }
            }
        }
        actions.append((AureaText.t("panel_remover_efeito"), { remove(effect) }))
        model.actionSheet = ActionSheetRequest(title: fxEffectDisplayName(effect.typeId, effect.name), items: actions)
    }
    private func paramMenu(_ param: EffectParamItem, effect: UInt32) {
        var actions: [(String, () -> Void)] = [(AureaText.t("panel_redefinir"), {
            model.beginGesture("redefinir parâmetro"); writeVector(param.index, effect: effect, values: param.defaultValue); model.endGesture()
        })]
        if param.flags & 1 != 0 && fxComponentCount(Int(param.type)) > 0 {
            actions.append((AureaText.t("panel_expressao_3c65"), { selected = EffectParamSelection(effect: effect, param: param.index, component: 0); openExpression() }))
        }
        model.actionSheet = ActionSheetRequest(title: display(param, effectId: effect).label, items: actions)
    }
    private func reorder(_ effect: EffectItem, value: DragGesture.Value) {
        guard model.selectedLayer?.locked != true else { return }
        if dragId == nil {
            dragId = effect.effectId; dragOrder = model.effects.map(\.effectId); dragOffset = 0; lastTranslation = 0
            UIImpactFeedbackGenerator(style: .light).impactOccurred()
        }
        guard dragId == effect.effectId, var order = dragOrder, let index = order.firstIndex(of: effect.effectId), let own = cardFrames[effect.effectId] else { return }
        dragOffset += value.translation.height - lastTranslation; lastTranslation = value.translation.height
        let center = own.midY + dragOffset
        if dragOffset > 0 && index + 1 < order.count, let next = cardFrames[order[index + 1]], center > next.midY {
            order.swapAt(index, index + 1); dragOffset -= next.height; dragOrder = order
        } else if dragOffset < 0 && index > 0, let previous = cardFrames[order[index - 1]], center < previous.midY {
            order.swapAt(index, index - 1); dragOffset += previous.height; dragOrder = order
        }
    }
    private func finishReorder() {
        if let id = dragId, let order = dragOrder, let destination = order.firstIndex(of: id), let effect = model.effects.first(where: { $0.effectId == id }), model.effects.firstIndex(where: { $0.effectId == id }) != destination { move(effect, to: destination) }
        dragId = nil; dragOrder = nil; dragOffset = 0; lastTranslation = 0
    }
    private var footer: some View {
        VStack(spacing: 0) {
            if model.selectedLayer?.adjustment == true { adjustmentIntensity.padding(.bottom, 8) }
            Button { browsing = true } label: {
                HStack(spacing: 8) {
                    CupertinoGlyph.text(CupertinoGlyph.Plus, size: 17, color: AureaColors.accent)
                    Text(AureaText.t("panel_adicionar_efeito")).font(.aurea(size: 16, weight: .semibold)).foregroundStyle(AureaColors.accent)
                }.frame(maxWidth: .infinity).frame(height: 52)
                    .background(AureaColors.surface, in: RoundedRectangle(cornerRadius: 12))
            }.buttonStyle(AureaPressStyle(shrink: 1))
        }
    }
    private var adjustmentIntensity: some View {
        let value = ((model.detail["opacity"] as? NSNumber)?.floatValue ?? 1) * 100
        return VStack(spacing: 0) {
            HStack {
                Text(AureaText.t("panel_intensidade_camada_ajuste")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                Spacer(minLength: 0)
                Button { toggleOpacityKey() } label: {
                    CupertinoGlyph.text(opacityLook == .keyHere ? CupertinoGlyph.RhombusFill : CupertinoGlyph.Rhombus, size: 16, color: opacityLook == .none ? AureaColors.muted : AureaColors.keyframe).frame(width: 44, height: 44)
                }.buttonStyle(AureaPressStyle())
            }
            EffectNumberRow(label: AureaText.t("panel_opacidade"), value: value, unitsPerPoint: 0.35, minimum: 0, maximum: 100, suffix: "%", decimals: 0, selected: true, look: opacityLook, expression: .none,
                            onSelect: {}, onMenu: {}, onBegin: { model.beginGesture("opacidade") }, onValue: { setOpacity($0) }, onEnd: { model.endGesture() }, onKeypad: {
                                model.numericKeypad = KeypadRequest(title: AureaText.t("panel_opacidade"), value: value, unit: "%", min: 0, max: 100, decimals: 0, onValue: { setOpacity($0) })
                            })
        }.padding(.init(top: 8, leading: 8, bottom: 4, trailing: 8))
            .background(AureaColors.chip.opacity(0.55), in: RoundedRectangle(cornerRadius: 12))
    }
    private var opacityLook: KeyframeLook {
        guard let layer = model.primarySelection else { return .none }
        let keys = (model.keyframes[layer] ?? []).filter { $0.property == 12 }
        return keys.contains(where: { $0.time == model.localPlayhead }) ? .keyHere : keys.isEmpty ? .none : .animated
    }
    private func setOpacity(_ value: Float) {
        guard let layer = model.primarySelection, model.selectedLayer?.locked != true else { return }
        model.setTransform(12, value: min(1, max(0, value / 100)), layer: layer)
    }
    private func toggleOpacityKey() {
        guard let layer = model.primarySelection, model.selectedLayer?.locked != true else { return }
        let here = (model.keyframes[layer] ?? []).first { $0.property == 12 && $0.time == model.localPlayhead }
        if let here { model.engine.editTrackKey(layer, property: 12, effect: UInt32.max, param: 0, time: here.time, action: 1, value: here.value, targetTime: here.time, interpolation: here.interpolation, handles: []) }
        else { model.engine.keyParameter(layer, property: 12, effect: UInt32.max, param: 0, time: model.localPlayhead, value: (model.detail["opacity"] as? NSNumber)?.floatValue ?? 1) }
        model.commitPendingCommands(); model.refreshModel(force: true)
    }
}
private struct EffectParamSelection: Identifiable, Equatable {
    let effect: UInt32
    let param: UInt32
    let component: Int
    var id: String { "\(effect):\(param):\(component)" }
}
private struct EffectCardFrames: PreferenceKey {
    static var defaultValue: [UInt32: CGRect] = [:]
    static func reduce(value: inout [UInt32: CGRect], nextValue: () -> [UInt32: CGRect]) { value.merge(nextValue(), uniquingKeysWith: { _, next in next }) }
}
// PropertyControls.kt: 94×32 label, 8 gap, ruler, 8 gap, 68×24 value.
private struct EffectNumberRow: View {
    let label: String
    let value: Float
    let unitsPerPoint: Float
    let minimum: Float
    let maximum: Float
    let suffix: String
    let decimals: Int
    let selected: Bool
    let look: KeyframeLook
    let expression: ExpressionLook
    let onSelect: () -> Void
    let onMenu: () -> Void
    let onBegin: () -> Void
    let onValue: (Float) -> Void
    let onEnd: () -> Void
    let onKeypad: () -> Void
    @State private var dragging = false
    @State private var live: Float = 0
    @State private var origin: Float = 0
    @State private var vertical = false
    @GestureState private var touching = false
    var body: some View {
        HStack(spacing: 8) {
            EffectPropertyLabel(label: label, selected: selected, look: look, expression: expression, onSelect: onSelect, onMenu: onMenu)
            TickRuler(value: { dragging ? live : value }, unitsPerDp: unitsPerPoint, active: selected)
                .frame(maxWidth: .infinity).contentShape(Rectangle())
                .simultaneousGesture(DragGesture(minimumDistance: 8)
                    .updating($touching) { _, active, _ in active = true }
                    .onChanged { gesture in
                    if !dragging && !vertical {
                        if abs(gesture.translation.height) > abs(gesture.translation.width) { vertical = true; return }
                        origin = value; live = value; dragging = true; onSelect(); onBegin()
                    }
                    guard dragging else { return }
                    let next = min(maximum, max(minimum, origin + Float(gesture.translation.width) * unitsPerPoint))
                    if next.isFinite { live = next; onValue(next) }
                }.onEnded { _ in finish() })
            ValueBox(comUnidade(numeroPtBr(dragging ? live : value, casas: decimals), suffix), width: 68, onTap: onKeypad)
        }.frame(height: 48)
            .onChange(of: touching) { active in if !active { finish() } }
            .onDisappear { finish() }
    }
    private func finish() { if dragging { dragging = false; onEnd() }; vertical = false }
}
private struct EffectPropertyLabel: View {
    let label: String
    let selected: Bool
    let look: KeyframeLook
    let expression: ExpressionLook
    let onSelect: () -> Void
    let onMenu: () -> Void
    var body: some View {
        Text(label).font(.aurea(size: 12, weight: .semibold)).lineLimit(2).multilineTextAlignment(.center)
            .foregroundStyle(selected ? AureaColors.accent : AureaColors.muted).underline(selected)
            .padding(.horizontal, 6).frame(width: 94, height: 32)
            .background(selected ? AureaColors.chip : Color.clear, in: RoundedRectangle(cornerRadius: 8))
            .overlay(alignment: .topLeading) {
                if look != .none {
                    Canvas { context, size in
                        var path = Path(); path.move(to: CGPoint(x: size.width / 2, y: 0)); path.addLine(to: CGPoint(x: size.width, y: size.height / 2))
                        path.addLine(to: CGPoint(x: size.width / 2, y: size.height)); path.addLine(to: CGPoint(x: 0, y: size.height / 2)); path.closeSubpath()
                        if look == .keyHere { context.fill(path, with: .color(AureaColors.keyframe)) }
                        else { context.stroke(path, with: .color(AureaColors.keyframe), lineWidth: 1) }
                    }.frame(width: 7, height: 7).offset(x: 3, y: 3)
                }
            }
            .overlay(alignment: .topTrailing) { if expression != .none { ExpressionBadge(look: expression).offset(x: 4) } }
            .contentShape(Rectangle()).onTapGesture(perform: onSelect).onLongPressGesture(minimumDuration: 0.5, perform: onMenu)
    }
}
