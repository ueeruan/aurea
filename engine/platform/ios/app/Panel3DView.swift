// Element3DPanel.kt: text geometry, material, shadows and both environments.
import SwiftUI
import UniformTypeIdentifiers

struct Panel3DView: View {
    @EnvironmentObject private var model: AureaModel
    @State private var text3D: [String: Any] = [:]
    @State private var draft = ""
    @State private var environment: [Float] = [0, 1, 0]
    @State private var objectEnvironment: [NSNumber] = []
    @State private var importedMaterials: [[Float]] = []
    @State private var selectedMaterial: UInt32 = 0
    @State private var shadows: [Float] = []
    @State private var pickingHdri = false
    @State private var pickingFont = false
    @State private var hdriTarget: Int64?
    @State private var fontTarget: Int64?
    @State private var pending: Text3DChange?
    @State private var rebuild: DispatchWorkItem?
    @State private var closeTyping: DispatchWorkItem?
    @State private var typingActive = false
    @State private var gestureOpen = false
    @FocusState private var editingText: Bool

    private var layerId: Int64 { model.primarySelection ?? 0 }
    private let presetKeys = ["pn_t3d_preset_chrome", "pn_t3d_preset_gold", "pn_t3d_preset_brushed",
                              "pn_t3d_preset_glossy", "pn_t3d_preset_matte", "pn_t3d_preset_neon"]

    var body: some View {
        VStack(spacing: 0) {
            PanelHeader(title: AureaText.t("panel_material_ambiente"), onBack: {
                finishEditing(); model.panel = .none
            })
            ScrollView {
                VStack(alignment: .leading, spacing: 0) {
                    if !text3D.isEmpty { textSection }
                    section("panel_material")
                    if !text3D.isEmpty {
                        colorRow("panel_cor", key: "color", region: 0, height: 48)
                        materialSection
                    } else { importedMaterialSection }
                    lightingSection
                    gap(16)
                    environmentSection
                    objectEnvironmentSection
                }
                .padding(.horizontal, 18).padding(.top, 12).padding(.bottom, 24)
                .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
        .background(AureaColors.background)
        .fileImporter(isPresented: $pickingHdri,
                      allowedContentTypes: [UTType(filenameExtension: "hdr") ?? .data, .data]) { result in
            if case .success(let url) = result {
                model.importMedia(url: url, kind: .hdri, objectHDRI: hdriTarget)
            }
        }
        .fileImporter(isPresented: $pickingFont, allowedContentTypes: [.data]) { result in
            if case .success(let url) = result { importFont(url, target: fontTarget) }
        }
        .onAppear(perform: load)
        .onChange(of: model.status.modelRevision) { _ in load() }
        .onChange(of: model.localPlayhead) { _ in loadMaterials() }
        .onChange(of: layerId) { _ in finishEditing(); editingText = false; load() }
        .onChange(of: editingText) { focused in if !focused && typingActive { finishEditing() } }
        .onDisappear {
            finishEditing()
            model.text3DFontSheet = nil
        }
    }

    // The entire column has a single 18 dp inset; its controls add no inset.
    private var textSection: some View {
        VStack(alignment: .leading, spacing: 0) {
            section("pn_text3d_title")
            HStack(spacing: 6) {
                chip("t3d_font") { openFonts() }
                chip("t3d_import_font") {
                    finishEditing(); fontTarget = layerId; pickingFont = true
                }
            }
            gap(12)
            TextField("", text: Binding(get: { draft }, set: changeText), axis: .vertical)
                .font(.aurea(size: 15)).foregroundStyle(AureaColors.text).tint(AureaColors.accent)
                .textFieldStyle(.plain).focused($editingText)
                .frame(maxWidth: .infinity, minHeight: 32, alignment: .topLeading)
                .overlay(alignment: .topLeading) {
                    if draft.isEmpty {
                        Text(AureaText.t("pn_text3d_placeholder")).font(.aurea(size: 15))
                            .foregroundStyle(AureaColors.muted).allowsHitTesting(false)
                    }
                }
                .padding(.horizontal, 12).padding(.vertical, 10)
                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
            gap(6)
            textRuler("pn_depth", key: "depth", step: 0.005, min: 0, max: 3,
                      shown: percent(number("depth")), gesture: "profundidade do texto 3D")
            row("panel_alinhamento", height: 48) {
                HStack(spacing: 6) {
                    ForEach(Array(["panel_esquerda", "panel_centro", "panel_direita"].enumerated()), id: \.offset) { item in
                        chip(item.element, on: Int(number("alignment")) == item.offset) {
                            set3D("alignment", value: Float(item.offset))
                        }
                    }
                }
            }
            gap(14)
            section("pn_t3d_geometry")
            onOffRow("pn_t3d_bevel", on: number("bevel") > 0.5) { set3D("bevel", value: $0 ? 1 : 0) }
            if number("bevel") > 0.5 {
                textRuler("pn_t3d_bevel_width", key: "bevelWidth", step: 0.0002, min: 0, max: 0.2,
                          shown: "\(rounded(number("bevelWidth") * 1000))", gesture: "chanfro")
                textRuler("pn_t3d_bevel_depth", key: "bevelDepth", step: 0.0002, min: 0, max: 0.2,
                          shown: "\(rounded(number("bevelDepth") * 1000))", gesture: "chanfro")
                row("pn_t3d_bevel_segments") {
                    HStack(spacing: 6) {
                        ForEach([1, 2, 3, 5, 8], id: \.self) { count in
                            T3DChip(label: "\(count)", on: Int(number("bevelSegments")) == count) {
                                set3D("bevelSegments", value: Float(count))
                            }
                        }
                    }
                }
                textRuler("pn_t3d_bevel_roundness", key: "bevelRoundness", step: 0.006, min: 0, max: 1,
                          shown: percent(number("bevelRoundness")), gesture: "arredondamento")
            }
            gap(14)
            section("pn_t3d_presets")
            horizontal {
                ForEach(Array(presetKeys.enumerated()), id: \.offset) { item in
                    chip(item.element) {
                        finishEditing()
                        _ = model.engine.applyText3DPreset(layerId, preset: UInt32(item.offset))
                        refresh()
                    }
                }
            }
            gap(14)
        }
    }

    private var materialSection: some View {
        VStack(alignment: .leading, spacing: 0) {
            textRuler("pn_t3d_metallic", key: "metallic", step: 0.005, min: 0, max: 1,
                      shown: percent(number("metallic")), gesture: "metalico")
            textRuler("pn_t3d_roughness", key: "roughness", step: 0.005, min: 0, max: 1,
                      shown: percent(number("roughness")), gesture: "rugosidade")
            textRuler("pn_t3d_specular", key: "specular", step: 0.005, min: 0, max: 1,
                      shown: percent(number("specular")), gesture: "especular")
            colorRow("pn_t3d_emissive", key: "emissive", region: 3)
            textRuler("pn_t3d_emissive_strength", key: "emissiveStrength", step: 0.02, min: 0, max: 8,
                      shown: percent(number("emissiveStrength")), gesture: "forca da emissao")
            onOffRow("pn_t3d_regions", on: number("regionMaterials") > 0.5) {
                set3D("regionMaterials", value: $0 ? 1 : 0)
            }
            if number("regionMaterials") > 0.5 {
                textRuler("pn_t3d_region_side", key: "sideRoughness", step: 0.005, min: 0, max: 1,
                          shown: percent(number("sideRoughness")), gesture: "rugosidade da lateral")
                colorRow("pn_t3d_region_bevel", key: "bevelColor", region: 2)
                ruler(AureaText.t("pn_t3d_metallic") + " · " + AureaText.t("pn_t3d_region_bevel"),
                      value: number("bevelMetallic"), step: 0.005, min: 0, max: 1,
                      shown: percent(number("bevelMetallic")), gesture: "metalico do chanfro") {
                    lazyNumber("bevelMetallic", $0)
                }
            }
        }
    }

    @ViewBuilder private var lightingSection: some View {
        if shadows.count == 2 {
            gap(16)
            section("pn_t3d_lighting")
            onOffRow("pn_t3d_cast_shadow", on: shadows[0] > 0.5) { value in
                finishEditing()
                _ = model.engine.setModelShadows(layerId, cast: value, receive: shadows[1] > 0.5)
                refresh()
            }
            onOffRow("pn_t3d_receive_shadow", on: shadows[1] > 0.5) { value in
                finishEditing()
                _ = model.engine.setModelShadows(layerId, cast: shadows[0] > 0.5, receive: value)
                refresh()
            }
        }
    }

    private var environmentSection: some View {
        VStack(alignment: .leading, spacing: 0) {
            section("panel_luz_ambiente")
            horizontal {
                chip("panel_estudio_neutro", on: environment[0] < 0.5) {
                    finishEditing(); model.engine.clearHdri(); refresh()
                }
                chip(environment[0] >= 0.5 ? "panel_imagem_ambiente" : "panel_usar_imagem_ambiente_hdr",
                     on: environment[0] >= 0.5) { pickHdri(object: false) }
            }
            gap(10)
            ruler(AureaText.t("panel_intensidade"), value: environment[1], step: 0.01, min: 0, max: 20,
                  shown: percent(environment[1]), gesture: "ambiente") { value in
                _ = model.engine.setEnvironmentIntensity(value, rotation: environment[2]); refresh()
            }
            ruler(AureaText.t("pn_env_rotate_light"), value: environment[2], step: 1, min: -360, max: 360,
                  shown: degrees(environment[2]), gesture: "ambiente") { value in
                _ = model.engine.setEnvironmentIntensity(environment[1], rotation: value); refresh()
            }
            gap(8)
            note("pn_env_light_hint")
        }
    }

    @ViewBuilder private var objectEnvironmentSection: some View {
        if objectEnvironment.count >= 5 {
            let own = objectNumber(0) >= 0.5
            gap(16)
            section("panel_ambiente_do_objeto")
            horizontal {
                chip("panel_do_projeto", on: !own) { finishEditing(); setObjectEnvironment(0) }
                chip("panel_proprio", on: own) {
                    finishEditing(); setObjectEnvironment(1)
                    if objectEnvironment[1].int64Value <= 0 { pickHdri(object: true) }
                }
            }
            if own {
                gap(10)
                horizontal {
                    chip(objectEnvironment[1].int64Value > 0 ? "panel_trocar_imagem" : "panel_usar_imagem_ambiente_hdr",
                         on: objectEnvironment[1].int64Value > 0) { pickHdri(object: true) }
                }
                gap(10)
                ruler(AureaText.t("panel_intensidade"), value: objectNumber(2), step: 0.01, min: 0, max: 20,
                      shown: percent(objectNumber(2)), gesture: "ambiente") { setObjectEnvironment(1, intensity: $0) }
                ruler(AureaText.t("pn_env_rotate_light"), value: objectNumber(3), step: 1, min: -360, max: 360,
                      shown: degrees(objectNumber(3)), gesture: "ambiente") { setObjectEnvironment(1, rotation: $0) }
                ruler(AureaText.t("panel_exposicao"), value: objectNumber(4), step: 0.01, min: 0.05, max: 20,
                      shown: percent(objectNumber(4)), gesture: "ambiente") { setObjectEnvironment(1, exposure: $0) }
            }
        }
    }

    private func section(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 13, weight: .bold))
            .foregroundStyle(AureaColors.muted).padding(.bottom, 6)
    }
    private func note(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 12)).lineSpacing(2)
            .foregroundStyle(AureaColors.muted).fixedSize(horizontal: false, vertical: true)
    }
    private func gap(_ height: CGFloat) -> some View { Spacer().frame(height: height) }
    private func chip(_ key: String, on: Bool = false, action: @escaping () -> Void) -> some View {
        T3DChip(label: AureaText.t(key), on: on, action: action)
    }
    private func horizontal<Content: View>(@ViewBuilder content: () -> Content) -> some View {
        ScrollView(.horizontal, showsIndicators: false) { HStack(spacing: 6, content: content) }
    }
    private func row<Content: View>(_ key: String, height: CGFloat = 44,
                                    @ViewBuilder content: () -> Content) -> some View {
        HStack(spacing: 0) {
            Text(AureaText.t(key)).font(.aurea(size: 13)).foregroundStyle(AureaColors.text)
                .frame(maxWidth: .infinity, alignment: .leading)
            content().fixedSize(horizontal: true, vertical: false)
        }.frame(height: height)
    }
    private func onOffRow(_ key: String, on: Bool, onChange: @escaping (Bool) -> Void) -> some View {
        row(key) {
            HStack(spacing: 6) {
                chip("pn_t3d_off", on: !on) { onChange(false) }
                chip("pn_t3d_on", on: on) { onChange(true) }
            }
        }
    }
    private func colorRow(_ title: String, key: String, region: UInt32, height: CGFloat = 44) -> some View {
        row(title, height: height) {
            // Element3DPanel passes these material channels directly as sRGB.
            Button { openColor(key, region: region) } label: {
                AureaColorSwatch(color: AureaColorSpace.color(colorValues(key)))
                    .frame(width: 30, height: 30).clipShape(RoundedRectangle(cornerRadius: 6))
            }.buttonStyle(AureaPressStyle()).accessibilityLabel(AureaText.t(title))
        }
    }
    private func textRuler(_ label: String, key: String, step: Float, min: Float, max: Float,
                           shown: String, gesture: String) -> some View {
        ruler(AureaText.t(label), value: number(key), step: step, min: min, max: max,
              shown: shown, gesture: gesture) { lazyNumber(key, $0) }
    }
    private func ruler(_ label: String, value: Float, step: Float, min: Float, max: Float,
                       shown: String, gesture: String, onValue: @escaping (Float) -> Void) -> some View {
        PropertyCustomRow(label, selected: false, onSelect: {}) {
            HStack(spacing: 8) {
                TickRuler(value: { value }, unitsPerDp: step, active: true)
                    .frame(maxWidth: .infinity)
                    .valueDrag(enabled: true, start: { value }, unitsPerDp: { step }, min: min, max: max,
                               onStart: { beginContinuous(gesture) }, onValue: onValue, onEnd: finishEditing)
                // The source explicitly has onTap = null for 3D/environment values.
                ValueBox(shown)
            }
        }
    }

    private func number(_ key: String) -> Float { (text3D[key] as? NSNumber)?.floatValue ?? 0 }
    private func objectNumber(_ index: Int) -> Float { objectEnvironment[index].floatValue }
    private func rounded(_ value: Float) -> Int { Int(floor(Double(value) + 0.5)) }
    private func percent(_ value: Float) -> String { "\(rounded(value * 100))%" }

    @ViewBuilder private var importedMaterialSection: some View {
        if let material = importedMaterials.first(where: { UInt32($0[0]) == selectedMaterial }) ?? importedMaterials.first {
            let index = UInt32(material[0])
            Menu {
                ForEach(importedMaterials.indices, id: \.self) { row in
                    Button("Material \(Int(importedMaterials[row][0]) + 1)") { selectedMaterial = UInt32(importedMaterials[row][0]) }
                }
            } label: { Text("Material \(index + 1)").font(.aurea(size: 14)).frame(minHeight: 44) }
            ForEach(0..<6, id: \.self) { param in
                materialControl(material, index: index, param: param)
            }
        }
    }

    private func materialControl(_ material: [Float], index: UInt32, param: Int) -> some View {
        let labels = ["R", "G", "B", "Alpha", AureaText.t("pn_t3d_metallic"), AureaText.t("pn_t3d_roughness")]
        let value = min(1, max(0, material[param + 2]))
        let keys = (model.keyframes[layerId] ?? []).filter { $0.property == 37 && $0.effectIndex == index && $0.paramIndex == UInt32(param) }
        let here = keys.first { $0.time == model.localPlayhead }
        return HStack(spacing: 8) {
            Text(labels[param]).font(.aurea(size: 13)).frame(width: 78, alignment: .leading)
            Slider(value: Binding(get: { value }, set: { newValue in
                _ = model.engine.setMaterial(forLayer: layerId, index: index, param: UInt32(param), value: newValue)
                refresh()
            }), in: 0...1, onEditingChanged: { editing in
                if editing { beginContinuous("material") } else { finishEditing() }
            })
            Button {
                model.beginGesture("keyframe de material")
                if let here {
                    model.engine.editTrackKey(layerId, property: 37, effect: index, param: UInt32(param), time: here.time,
                                              action: 1, value: here.value, targetTime: here.time, interpolation: here.interpolation, handles: [])
                } else {
                    model.engine.keyParameter(layerId, property: 37, effect: index, param: UInt32(param), time: model.localPlayhead, value: value)
                }
                model.endGesture(); model.commitPendingCommands(); refresh()
            } label: { Text(here == nil ? "◇" : "◆").frame(width: 44, height: 44) }
            .accessibilityLabel(AureaText.t(here == nil ? "panel_marcar_keyframe_aqui" : "panel_tirar_keyframe_daqui") + " · " + labels[param])
        }
    }
    private func degrees(_ value: Float) -> String { "\(rounded(value))°" }
    private func colorValues(_ key: String) -> [Float] {
        let values = (text3D[key] as? [NSNumber] ?? []).map(\.floatValue)
        return (0..<4).map { $0 < values.count ? values[$0] : $0 == 3 ? 1 : 0 }
    }

    private func set3D(_ key: String, value: Float) {
        finishEditing()
        _ = model.engine.setText3D(forLayer: layerId, property: key, stringValue: nil, numberValue: value)
        refresh()
    }
    private func lazyNumber(_ key: String, _ value: Float) {
        text3D[key] = NSNumber(value: value)
        schedule(.number(layerId, key, value), delay: 0.09)
    }
    private func openColor(_ key: String, region: UInt32) {
        beginContinuous("cor do texto 3D")
        let target = layerId
        model.colorSheet = ColorSheetRequest(title: AureaText.t("ds_cor"), initial: colorValues(key), onChange: { r, g, b, _ in
            guard layerId == target else { return }
            let value: [Float] = [r, g, b, 1]
            text3D[key] = value.map { NSNumber(value: $0) }
            schedule(.color(target, region, value), delay: 0.09)
        }, onDone: finishEditing)
    }
    private func setObjectEnvironment(_ source: UInt32, intensity: Float? = nil,
                                      rotation: Float? = nil, exposure: Float? = nil) {
        guard objectEnvironment.count >= 5 else { return }
        // Keep the asset handle as Int64, rather than round it through Float.
        _ = model.engine.setObjectEnvironment(forLayer: layerId, source: source,
                                               hdri: objectEnvironment[1].int64Value,
                                               intensity: intensity ?? objectNumber(2),
                                               rotation: rotation ?? objectNumber(3),
                                               exposure: exposure ?? objectNumber(4))
        refresh()
    }
    private func pickHdri(object: Bool) {
        finishEditing(); hdriTarget = object ? layerId : nil; pickingHdri = true
    }
    private func openFonts() {
        finishEditing()
        let target = layerId
        model.text3DFontSheet = Text3DFontRequest(fonts: model.engine.availableFonts(),
                                                current: text3D["fontPath"] as? String ?? "") { path in
            guard model.primarySelection == target else { return }
            _ = model.engine.setText3D(forLayer: target, property: "fontPath", stringValue: path, numberValue: 0)
            refresh()
        }
    }

    // EditorStore.setText3D: 90 ms for a drag, 250 ms for mesh typing and one
    // undo step for the typing burst, closed after 1 s without another key.
    private func changeText(_ content: String) {
        draft = content
        if !typingActive {
            finishEditing()
            typingActive = true; gestureOpen = true
            model.beginGesture("editar texto 3D")
        }
        closeTyping?.cancel()
        let close = DispatchWorkItem { finishEditing() }
        closeTyping = close
        DispatchQueue.main.asyncAfter(deadline: .now() + 1, execute: close)
        schedule(.content(layerId, content), delay: 0.25)
    }
    private func beginContinuous(_ label: String) {
        finishEditing(); editingText = false; gestureOpen = true; model.beginGesture(label)
    }
    private func schedule(_ change: Text3DChange, delay: Double) {
        rebuild?.cancel(); pending = change
        let work = DispatchWorkItem { applyPending() }
        rebuild = work
        DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: work)
    }
    private func applyPending() {
        rebuild?.cancel(); rebuild = nil
        guard let change = pending else { return }
        pending = nil
        switch change {
        case let .number(id, key, value):
            _ = model.engine.setText3D(forLayer: id, property: key, stringValue: nil, numberValue: value)
        case let .color(id, region, value):
            _ = model.engine.setText3DColor(id, region: region, values: value.map { NSNumber(value: $0) })
        case let .content(id, content):
            if !content.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                _ = model.engine.setText3D(forLayer: id, property: "content", stringValue: content, numberValue: 0)
            }
        }
        refresh()
    }
    private func finishEditing() {
        closeTyping?.cancel(); closeTyping = nil
        applyPending()
        typingActive = false
        if gestureOpen { gestureOpen = false; model.endGesture() }
    }
    private func refresh() { model.refreshModel(force: true); load() }
    private func load() {
        if pending == nil {
            text3D = model.engine.text3D(forLayer: layerId) ?? [:]
            if !typingActive && !editingText { draft = text3D["content"] as? String ?? "" }
        }
        let values = model.engine.environment().map(\.floatValue)
        environment = values.count >= 3 ? values : [0, 1, 0]
        shadows = model.engine.modelShadows(layerId).map(\.floatValue)
        objectEnvironment = model.engine.objectEnvironment(forLayer: layerId)
        loadMaterials()
    }
    private func loadMaterials() {
        let materials = model.engine.materials(forLayer: layerId).map(\.floatValue)
        importedMaterials = stride(from: 0, to: materials.count - materials.count % 8, by: 8).map { Array(materials[$0..<($0 + 8)]) }
    }
    private func importFont(_ url: URL, target: Int64?) {
        guard ["ttf", "otf"].contains(url.pathExtension.lowercased()) else {
            model.toast = AureaText.t("msg_use_uma_fonte_ttf_ou_otf"); return
        }
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        AureaPaths.ensureDirectories()
        let destination = AureaPaths.mediaDestination(for: url.lastPathComponent)
        do {
            try FileManager.default.copyItem(at: url, to: destination)
            guard let font = model.engine.importFont(atPath: destination.path) else {
                try? FileManager.default.removeItem(at: destination)
                model.toast = AureaText.t("msg_nao_deu_para_ler_essa_fonte"); return
            }
            if let target = target, model.primarySelection == target {
                _ = model.engine.setText3D(forLayer: target, property: "fontPath",
                                           stringValue: "docs:Media/\(destination.lastPathComponent)", numberValue: 0)
                refresh()
            }
            model.toast = AureaText.t("msg_fonte_importada", font["family"] as? String ?? "")
        } catch { model.toast = AureaText.t("msg_nao_deu_para_ler_essa_fonte") }
    }
}

private enum Text3DChange {
    case number(Int64, String, Float)
    case color(Int64, UInt32, [Float])
    case content(Int64, String)
}

private struct T3DChip: View {
    let label: String
    var on: Bool = false
    let action: () -> Void
    var body: some View {
        Button(action: action) {
            Text(label).font(.aurea(size: 12)).foregroundStyle(on ? AureaColors.accent : AureaColors.text)
                .padding(.horizontal, 12).padding(.vertical, 6)
                .background(on ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
        }.buttonStyle(AureaPressStyle())
    }
}

struct Text3DFontRequest: Identifiable {
    let id = UUID()
    let fonts: [[String: Any]]
    let current: String
    let onPick: (String) -> Void
}

// The source uses a Material AlertDialog for 3D fonts, with a 400 dp list.
// Present at the app root so the veil covers the stage and timeline as well.
struct T3DFontSheet: View {
    let request: Text3DFontRequest
    let onDismiss: () -> Void

    var body: some View {
        GeometryReader { geometry in
            ZStack {
                Color.black.opacity(0.60).ignoresSafeArea().onTapGesture(perform: onDismiss)
                VStack(alignment: .leading, spacing: 0) {
                    Text(AureaText.t("t3d_font")).font(.aurea(size: 24))
                        .foregroundStyle(AureaColors.text).frame(minHeight: 32).padding(.bottom, 16)
                    ScrollView {
                        LazyVStack(alignment: .leading, spacing: 0) {
                            fontButton(AureaText.t("t3d_default_font"), path: "", selected: false)
                            ForEach(Array(request.fonts.enumerated()), id: \.offset) { _, font in
                                if let path = font["path"] as? String {
                                    fontButton("\(font["family"] as? String ?? "") \(font["style"] as? String ?? "")",
                                               path: path, selected: sameFont(path))
                                }
                            }
                        }.frame(maxWidth: .infinity, alignment: .leading)
                    }
                    .frame(height: min(400, min(CGFloat(request.fonts.count + 1) * 48, max(48, geometry.size.height - 200))))
                    .padding(.bottom, 24)
                    HStack {
                        Spacer(minLength: 0)
                        Button(action: onDismiss) {
                            Text(AureaText.t("t3d_close")).font(.aurea(size: 14, weight: .medium))
                                .foregroundStyle(AureaColors.text).padding(.horizontal, 12).frame(height: 48)
                        }.buttonStyle(AureaPressStyle())
                    }
                }
                .padding(24)
                .frame(width: min(280, geometry.size.width - 48))
                .background(AureaColors.surfaceHigh, in: RoundedRectangle(cornerRadius: 28))
            }.frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }
    private func fontButton(_ title: String, path: String, selected: Bool) -> some View {
        Button { onDismiss(); request.onPick(path) } label: {
            Text(title).font(.aurea(size: 14, weight: .medium)).multilineTextAlignment(.leading)
                .foregroundStyle(selected ? AureaColors.accent : AureaColors.text)
                .padding(.horizontal, 12).padding(.vertical, 8).frame(minHeight: 48)
        }.buttonStyle(AureaPressStyle())
    }
    private func sameFont(_ path: String) -> Bool {
        if request.current == path { return true }
        if request.current.hasPrefix("docs:") {
            let resolved = AureaPaths.documents.appendingPathComponent(String(request.current.dropFirst(5))).path
            return resolved == path
        }
        return false
    }
}
