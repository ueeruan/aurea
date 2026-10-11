import SwiftUI

// The C++ schema supplies declarations only. Values, commands, tracks and undo
// stay in AureaEngine; this adapter does not keep a second property model.
struct AureaBuiltinPropertySchema: Decodable {
    let version: Int
    let panels: [Panel]
    var valid: Bool {
        version == 1 && panels.count == 2 && Set(panels.map(\.domain)) == Set(["light", "material"])
            && panels.allSatisfy(\.valid)
    }
    struct Panel: Decodable {
        let domain: String
        let properties: [Property]
        var valid: Bool {
            let count = domain == "light" ? 11 : 6
            let bindings = properties.flatMap(\.bindingParams)
            return Set(properties.map(\.id)).count == properties.count
                && properties.allSatisfy(\.valid) && bindings.count == count && Set(bindings) == Set(0..<count)
                && properties.allSatisfy { p in
                    (p.type != fxParamColor || p.components == (domain == "light" ? 3 : 4))
                        && p.conditions.allSatisfy { (0..<count).contains($0.bindingParam) }
                }
        }
    }
    struct Condition: Decodable {
        let bindingParam: Int
        let equals: [Float]
    }
    struct Option: Decodable, Identifiable {
        let value: Float
        let id: String
        let label: String
    }
    struct Property: Decodable, Identifiable {
        let id: String
        let label: String
        let type: Int
        let components: Int
        let bindingParams: [Int]
        let trackProperties: [Int]
        let defaultValue: [Float]
        let sliderMin: Float
        let sliderMax: Float
        let typedMin: Float
        let typedMax: Float
        let unit: String
        let precision: Int
        let group: String
        let conditions: [Condition]
        let colorSpace: String
        let defaultSource: String
        let options: [Option]?

        var valid: Bool {
            (1...4).contains(components) && bindingParams.count == components && trackProperties.count == components
                && defaultValue.count == components && bindingParams.allSatisfy { $0 >= 0 }
                && !id.isEmpty && trackProperties.allSatisfy { $0 >= -1 } && defaultValue.allSatisfy(\.isFinite)
                && [fxParamFloat, fxParamBool, fxParamColor, fxParamAngle, fxParamEnum].contains(type)
                && (type == fxParamColor || components == 1)
                && [sliderMin, sliderMax, typedMin, typedMax].allSatisfy(\.isFinite)
                && typedMin <= sliderMin && sliderMin <= sliderMax && sliderMax <= typedMax
                && (0...6).contains(precision)
                && conditions.allSatisfy { !$0.equals.isEmpty && $0.equals.allSatisfy(\.isFinite) }
                && (type != fxParamEnum || !(options ?? []).isEmpty)
        }
        func visible(_ values: [Float]) -> Bool {
            conditions.allSatisfy { condition in
                values.indices.contains(condition.bindingParam)
                    && condition.equals.contains(values[condition.bindingParam])
            }
        }
    }
}

@MainActor private enum AureaBuiltinPropertyMetadata {
    static var cached: AureaBuiltinPropertySchema?
    static var attempted = false
    static func panel(_ domain: String, engine: AureaEngine) -> AureaBuiltinPropertySchema.Panel? {
        if !attempted {
            attempted = true
            if let data = engine.builtinPropertySchemaJSON().data(using: .utf8),
               let decoded = try? JSONDecoder().decode(AureaBuiltinPropertySchema.self, from: data),
               decoded.valid {
                cached = decoded
            }
        }
        return cached?.panels.first { $0.domain == domain }
    }
}

@MainActor
struct AureaPropertyPanel: View {
    let domain: String
    let layerId: Int64
    /// Native binding-param order: lightInfo[0...10], or material fields[0...5].
    let values: [Float]
    let projectGeneration: UUID
    let compositionId: UInt64
    var materialIndex: UInt32 = 0
    let onWrite: ([(UInt32, Float)]) -> Void
    @EnvironmentObject private var model: AureaModel
    @State private var selected: TimelineTrack?
    @State private var keypad: KeypadRequest?
    @State private var picker: ColorSheetRequest?
    @State private var colorGestureOpen = false

    private var currentOwner: Bool {
        model.projectGeneration == projectGeneration && model.primarySelection == layerId
            && (model.composition[AureaCompositionId] as? NSNumber)?.uint64Value == compositionId
    }
    private var locked: Bool { !currentOwner || (model.layers.first { $0.id == layerId }?.locked ?? true) }
    private var prefix: String { "property.\(domain).\(layerId).\(materialIndex)" }
    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            if let panel = AureaBuiltinPropertyMetadata.panel(domain, engine: model.engine),
               panel.properties.flatMap(\.bindingParams).allSatisfy({ values.indices.contains($0) }), values.allSatisfy(\.isFinite) {
                ForEach(panel.properties.filter { $0.visible(values) }) { property in
                    propertyControl(property)
                }
            } else {
                Text(AureaText.t("property_schema_unavailable")).font(.aurea(size: 13)).foregroundStyle(AureaColors.warning)
            }
        }
        .disabled(locked)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier(prefix)
        // These controls also appear inside native sheets. Hosting their
        // existing pickers locally prevents the root overlay being underneath.
        .sheet(item: $keypad) { request in
            NumericKeypadSheet(request: request) { keypad = nil }.environmentObject(model)
        }
        .sheet(item: $picker, onDismiss: finishColorGesture) { request in
            ColorPickerSheet(request: request) { picker = nil; request.onDone() }.environmentObject(model)
        }
        .onDisappear { finishColorGesture(); if model.timelineFocus == selected.map({ [$0] }) { model.timelineFocus = nil } }
    }

    private func label(_ p: AureaBuiltinPropertySchema.Property) -> String {
        let key: String?
        switch "\(domain).\(p.id)" {
        case "light.kind": key = "scene_light_kind"
        case "light.intensity": key = "panel_intensidade"
        case "light.color", "material.baseColor": key = "panel_cor"
        case "light.range": key = "scene_light_range"
        case "light.coneAngle": key = "scene_light_cone"
        case "light.penumbra": key = "scene_light_penumbra"
        case "light.castShadows": key = "scene_light_shadows"
        case "light.shadowBias": key = "scene_shadow_bias"
        case "light.shadowStrength": key = "scene_shadow_strength"
        case "material.metallic": key = "pn_t3d_metallic"
        case "material.roughness": key = "pn_t3d_roughness"
        default: key = nil
        }
        return key.map { AureaText.t($0) } ?? p.label
    }
    private func value(_ p: AureaBuiltinPropertySchema.Property, _ component: Int) -> Float {
        let binding = p.bindingParams[component]
        return values.indices.contains(binding) ? values[binding] : p.defaultValue[component]
    }
    private func track(_ p: AureaBuiltinPropertySchema.Property, _ component: Int) -> TimelineTrack? {
        guard p.trackProperties[component] >= 0 else { return nil }
        return TimelineTrack(property: p.trackProperties[component], effect: domain == "material" ? materialIndex : .max,
                             param: domain == "material" ? UInt32(p.bindingParams[component]) : 0)
    }
    private func keys(_ track: TimelineTrack) -> [KeyframeItem] {
        (model.keyframes[layerId] ?? []).filter {
            Int($0.property) == track.property && $0.effectIndex == track.effect && $0.paramIndex == track.param
        }
    }
    private func look(_ p: AureaBuiltinPropertySchema.Property, _ components: [Int]) -> KeyframeLook {
        let entries = components.compactMap { track(p, $0) }.flatMap { keys($0) }
        if entries.contains(where: { $0.time == model.localPlayhead }) { return .keyHere }
        return entries.isEmpty ? .none : .animated
    }
    private func select(_ p: AureaBuiltinPropertySchema.Property, _ component: Int) {
        guard !locked else { return }
        selected = track(p, component)
        model.timelineFocus = selected.map { [$0] }
    }
    private func write(_ p: AureaBuiltinPropertySchema.Property, _ component: Int, _ amount: Float) {
        guard !locked, amount.isFinite else { return }
        let bounded = amount.clamped(to: p.typedMin...p.typedMax)
        onWrite([(UInt32(p.bindingParams[component]), p.type == fxParamInt ? bounded.rounded() : bounded)])
    }
    private func commit(_ p: AureaBuiltinPropertySchema.Property, _ component: Int, _ amount: Float) {
        guard !locked else { return }
        model.beginGesture(label(p)); write(p, component, amount); model.endGesture()
    }
    private func toggleKey(_ p: AureaBuiltinPropertySchema.Property, _ components: [Int]) {
        guard !locked else { return }
        let existing = components.compactMap { track(p, $0) }.flatMap { keys($0) }.filter { $0.time == model.localPlayhead }
        model.beginGesture(label(p))
        model.mutate { core in
            if !existing.isEmpty {
                for key in existing {
                    core.editTrackKey(layerId, property: key.property, effect: key.effectIndex, param: key.paramIndex,
                                      time: key.time, action: 1, value: key.value, targetTime: key.time,
                                      interpolation: key.interpolation, handles: [])
                }
            } else {
                for component in components {
                    if let key = track(p, component) {
                        core.keyParameter(layerId, property: UInt32(key.property), effect: key.effect, param: key.param,
                                          time: model.localPlayhead, value: value(p, component))
                    }
                }
            }
        }
        model.endGesture()
    }

    @ViewBuilder private func propertyControl(_ p: AureaBuiltinPropertySchema.Property) -> some View {
        switch p.type {
        case fxParamFloat, fxParamInt: numeric(p, component: 0)
        case fxParamAngle:
            numeric(p, component: 0)
            AureaAngleControl(value: value(p, 0), range: p.typedMin...p.typedMax, label: label(p), identifier: prefix + "." + p.id + ".angle") {
                select(p, 0); write(p, 0, $0)
            }
        case fxParamPoint2D, fxParamPoint3D:
            ForEach(0..<p.components, id: \.self) { numeric(p, component: $0) }
        case fxParamColor: color(p)
        case fxParamBool:
            HStack {
                Text(label(p)).font(.aurea(size: 13)); Spacer(minLength: 8)
                AureaToggle(checked: value(p, 0) >= 0.5) { commit(p, 0, $0 ? 1 : 0) }
                    .accessibilityLabel(label(p)).accessibilityValue(AureaText.t(value(p, 0) >= 0.5 ? "common_on" : "common_off"))
                    .accessibilityIdentifier(prefix + "." + p.id)
            }.frame(minHeight: 44)
        case fxParamEnum: choice(p)
        default:
            Text(label(p) + " · " + AureaText.t("property_schema_unavailable")).foregroundStyle(AureaColors.warning)
        }
    }
    @ViewBuilder private func numeric(_ p: AureaBuiltinPropertySchema.Property, component: Int, showTrackActions: Bool = true) -> some View {
        let isColor = p.type == fxParamColor
        let scale: Float = isColor ? (component < 3 ? 255 : 100) : p.unit == "%" ? 100 : 1
        let unit = isColor ? (component == 3 ? "%" : "") : p.unit
        let nativeColor = (0..<4).map { $0 < p.components ? value(p, $0) : Float(1) }
        let displayColor = p.colorSpace == "linear" ? AureaColorSpace.engineToDisplay(nativeColor) : nativeColor
        let amount = (isColor ? displayColor[component] : value(p, component)) * scale
        // Sensitivity uses the comfortable slider span; native typed bounds
        // remain available to both ruler and keypad, including HDR intensity.
        let fingerMin = p.typedMin * scale, fingerMax = p.typedMax * scale
        let title = p.components > 1 ? (p.type == fxParamColor ? ["R", "G", "B", AureaText.t("tl_alpha")][component] : ["X", "Y", "Z"][component]) : label(p)
        let key = track(p, component)
        let state = look(p, [component])
        NativePanelRuler(label: title, value: amount,
                         step: max(pow(10, -Float(p.precision)), (p.sliderMax - p.sliderMin) * scale / 500),
                         range: fingerMin...fingerMax, unit: unit, decimals: p.precision,
                         keypad: true, look: state,
                         toggleKey: key == nil ? nil : { select(p, component); toggleKey(p, [component]) },
                         selected: selected == key && key != nil, onSelect: { select(p, component) },
                         typedRange: (p.typedMin * scale)...(p.typedMax * scale), identifier: prefix + "." + p.id + ".\(component)",
                         controlHeight: 44, accessibilityName: label(p) + (p.components > 1 ? " · " + title : ""),
                         presentKeypad: { keypad = $0 }) { next in
            select(p, component)
            if isColor && p.colorSpace == "linear" && component < 3 {
                var display = displayColor; display[component] = next / scale
                write(p, component, AureaColorSpace.displayToEngine(display[0], display[1], display[2], display[3])[component])
            } else { write(p, component, next / scale) }
        }
        if let key, selected == key, showTrackActions {
            NativeAnimationTrackActions(track: key, curveTag: prefix + "." + p.id + ".curve.\(component)")
        }
    }
    private func choice(_ p: AureaBuiltinPropertySchema.Property) -> some View {
        let options = p.options ?? []
        let current = options.first { $0.value == value(p, 0) }
        return HStack {
            Text(label(p)).font(.aurea(size: 13)); Spacer(minLength: 8)
            Menu {
                ForEach(options) { option in Button(optionLabel(option)) { commit(p, 0, option.value) } }
            } label: {
                HStack { Text(current.map(optionLabel) ?? p.label); CupertinoGlyph.text(CupertinoGlyph.ChevronDown, size: 12) }
                    .font(.aurea(size: 13)).foregroundStyle(AureaColors.accent)
                    .padding(.horizontal, 10).frame(minHeight: 44).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
            }.accessibilityLabel(label(p)).accessibilityIdentifier(prefix + "." + p.id)
        }.frame(minHeight: 44)
    }
    private func optionLabel(_ option: AureaBuiltinPropertySchema.Option) -> String {
        let key = ["directional": "scene_light_directional", "point": "scene_light_point", "spot": "scene_light_spot"][option.id]
        return key.map { AureaText.t($0) } ?? option.label
    }
    private func color(_ p: AureaBuiltinPropertySchema.Property) -> some View {
        let rgba = (0..<4).map { $0 < p.components ? value(p, $0) : Float(1) }
        let display = p.colorSpace == "linear" ? AureaColorSpace.engineToDisplay(rgba) : rgba
        return VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(label(p)).font(.aurea(size: 13)); Spacer(minLength: 8)
                Button { openColor(p, rgba: display) } label: {
                    AureaColorSwatch(color: AureaColorSpace.color(display)).frame(width: 64, height: 28)
                        .clipShape(RoundedRectangle(cornerRadius: 6)).padding(.vertical, 8)
                }.buttonStyle(.plain).accessibilityLabel(label(p)).accessibilityIdentifier(prefix + "." + p.id + ".picker")
                keyButton(p)
            }.frame(minHeight: 44)
            // The picker writes all supported channels atomically as one undo
            // gesture. Individual component rows keep per-channel animation.
            DisclosureGroup(AureaText.t("panel_avancado")) {
                ForEach(0..<p.components, id: \.self) { numeric(p, component: $0, showTrackActions: false) }
            }.font(.aurea(size: 13)).tint(AureaColors.accent)
            if let selected, let component = (0..<p.components).first(where: { track(p, $0) == selected }) {
                NativeAnimationTrackActions(track: selected, curveTag: prefix + "." + p.id + ".curve.\(component)")
            }
        }
    }
    private func keyButton(_ p: AureaBuiltinPropertySchema.Property) -> some View {
        let components = Array(0..<p.components)
        let state = look(p, components)
        return Button { select(p, 0); toggleKey(p, components) } label: {
            KeyframeDiamondIcon(look: state, enabled: true).frame(width: 44, height: 44)
        }.buttonStyle(.plain)
            .accessibilityLabel(AureaText.t(state == .keyHere ? "panel_tirar_keyframe_daqui" : "panel_marcar_keyframe_aqui") + " · " + label(p))
            .accessibilityIdentifier(prefix + "." + p.id + ".keyframe")
    }
    private func openColor(_ p: AureaBuiltinPropertySchema.Property, rgba: [Float]) {
        guard !locked else { return }
        select(p, 0)
        finishColorGesture(); model.beginGesture(label(p)); colorGestureOpen = true
        picker = ColorSheetRequest(title: label(p), initial: rgba, withAlpha: p.components == 4,
            onChange: { r, g, b, a in
                let channels = p.colorSpace == "linear" ? AureaColorSpace.displayToEngine(r, g, b, a) : [r, g, b, a]
                guard !locked, channels.allSatisfy(\.isFinite) else { return }
                onWrite((0..<p.components).map { (UInt32(p.bindingParams[$0]), channels[$0].clamped(to: p.typedMin...p.typedMax)) })
            }, onDone: finishColorGesture)
    }
    private func finishColorGesture() {
        if colorGestureOpen { colorGestureOpen = false; model.endGesture() }
    }
}

/// Reuses the existing rotation dial artwork; only this property's range and
/// the existing native setter determine the editable cone angle.
@MainActor private struct AureaAngleControl: View {
    let value: Float
    let range: ClosedRange<Float>
    let label: String
    let identifier: String
    let onValue: (Float) -> Void
    @EnvironmentObject private var model: AureaModel
    @State private var previous: Float?
    @State private var accumulated: Float = 0
    @State private var editing = false
    var body: some View {
        TransformDial(angle: editing ? accumulated : value)
            .frame(width: 112, height: 112).contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0).onChanged { event in
                let dx = event.location.x - 56, dy = event.location.y - 56
                guard hypot(dx, dy) >= 22 else { previous = nil; return }
                let angle = Float(atan2(dy, dx) * 180 / .pi)
                if !editing { accumulated = value; editing = true; model.beginGesture(label) }
                if let previous {
                    var delta = angle - previous
                    if delta > 180 { delta -= 360 }; if delta < -180 { delta += 360 }
                    accumulated = (accumulated + delta).clamped(to: range); onValue(accumulated)
                }
                previous = angle
            }.onEnded { _ in end() })
            .accessibilityElement().accessibilityLabel(label).accessibilityValue(comUnidade(numeroPtBr(value, casas: 1), "°"))
            .accessibilityAdjustableAction { direction in
                let delta: Float = direction == .increment ? 1 : direction == .decrement ? -1 : 0
                guard delta != 0 else { return }
                model.beginGesture(label); onValue((value + delta).clamped(to: range)); model.endGesture()
            }.accessibilityIdentifier(identifier).onDisappear(perform: end)
    }
    private func end() { previous = nil; if editing { editing = false; model.endGesture() } }
}
