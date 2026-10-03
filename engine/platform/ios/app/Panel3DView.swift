// Element3DPanel.kt: o painel 3D em ABAS — Material, Forma, Luz e cena e
// Animação (texto 3D e forma 3D); modelo importado: Material e Luz e cena;
// câmera: só a cena. Cada linha de valor é a mesma T3DRow (rótulo, régua,
// valor, ↺ ao padrão), interruptores são AureaToggle e o raro fica em
// "Avançado", fechado. Os comandos do motor são os mesmos de antes.
import SwiftUI
import UniformTypeIdentifiers

/// As abas do painel 3D (a ordem é a da tela; o rawValue é o mesmo do Android).
enum Panel3DTab: Int, CaseIterable {
    case material, shape, light, anim
    var key: String { ["ui3d_tab_material", "ui3d_tab_shape", "ui3d_tab_light", "ui3d_tab_anim"][rawValue] }
    var id: String { ["material", "shape", "light", "anim"][rawValue] }
}

struct Panel3DView: View {
    @EnvironmentObject private var model: AureaModel
    @State private var sceneSettings: [Float] = []
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
    @State private var tab: Panel3DTab = .material
    @State private var materialAdvanced = false
    @State private var sceneAdvanced = false
    @FocusState private var editingText: Bool

    private var layerId: Int64 { model.primarySelection ?? 0 }
    private let presetKeys = ["pn_t3d_preset_chrome", "pn_t3d_preset_gold", "pn_t3d_preset_brushed",
                              "pn_t3d_preset_glossy", "pn_t3d_preset_matte", "pn_t3d_preset_neon", "pn_t3d_preset_cinematic"]
    /// Cor da amostra de cada material pronto (a mesma ordem do motor).
    private let presetSwatches: [[Float]] = [[0.95, 0.96, 0.98], [1, 0.77, 0.34], [0.78, 0.79, 0.8], [0.9, 0.1, 0.12],
                                             [0.85, 0.85, 0.86], [0.1, 1, 0.85], [0.66, 0.68, 0.72]]
    private let finishKeys = ["ui3d_finish_smooth", "ui3d_finish_brushed", "ui3d_finish_scratched",
                              "ui3d_finish_hammered", "ui3d_finish_weathered"]

    private var isCamera: Bool { model.selectedLayer?.kind == 8 }
    private var isShape: Bool { !isCamera && !model.engine.shape3D(layerId).isEmpty }
    private var isText: Bool { !isCamera && !text3D.isEmpty }
    private var tabs: [Panel3DTab] {
        if isCamera { return [.light] }
        return isText || isShape ? [.material, .shape, .light, .anim] : [.material, .light]
    }
    private var currentTab: Panel3DTab { tabs.contains(tab) ? tab : tabs[0] }

    var body: some View {
        VStack(spacing: 0) {
            PanelHeader(title: AureaText.t(text3D.isEmpty ? "panel_material_ambiente" : "text_options"), onBack: {
                finishEditing(); model.panel = .none
            })
            if tabs.count > 1 { tabBar.padding(.horizontal, 18).padding(.top, 10).padding(.bottom, 4) }
            ScrollView {
                VStack(alignment: .leading, spacing: 0) {
                    switch currentTab {
                    case .material:
                        if isText { textMaterialTab }
                        else if isShape { Shape3DPanelSection(layerId: layerId, page: .material) }
                        else { section("panel_material"); importedMaterialSection }
                    case .shape:
                        if isText { textShapeTab }
                        else if isShape { Shape3DPanelSection(layerId: layerId, page: .shape); shapeLayoutButton }
                    case .light:
                        lightSceneTab
                    case .anim:
                        if isText { TextAnimationSection(showAnimatorEffect: false, beforeAnimation: finishEditing) }
                        else if isShape { Text3DAnimSection(layerId: layerId, parts: true) }
                    }
                }
                .padding(.horizontal, 18).padding(.top, 6).padding(.bottom, 24)
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            .accessibilityIdentifier("text3d.materialScroll")
        }
        .background(AureaColors.background)
        .fileImporter(isPresented: $pickingHdri,
                      allowedContentTypes: [UTType(filenameExtension: "hdr") ?? .data, .image, .data]) { result in
            if case .success(let url) = result {
                if model.selectedLayer?.kind == 8 { _ = model.engine.setEnvironmentBackground(true) }
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
        .onChange(of: tab) { _ in finishEditing() }
        .onDisappear {
            finishEditing()
            model.text3DFontSheet = nil
        }
    }

    /// Controle segmentado: cada aba divide a largura; a acesa usa o destaque.
    private var tabBar: some View {
        HStack(spacing: 3) {
            ForEach(tabs, id: \.self) { item in
                let on = item == currentTab
                Button { tab = item } label: {
                    Text(AureaText.t(item.key)).font(.aurea(size: 12.5, weight: on ? .bold : .medium))
                        .foregroundStyle(on ? AureaColors.accent : AureaColors.text)
                        .lineLimit(1).minimumScaleFactor(0.8)
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                        .background(on ? AureaColors.accentDim : Color.clear, in: RoundedRectangle(cornerRadius: 8))
                        .contentShape(Rectangle())
                }
                .buttonStyle(AureaPressStyle(shrink: 1))
                .accessibilityIdentifier("panel3d.tab.\(item.id)")
                .accessibilityAddTraits(on ? [.isSelected] : [])
            }
        }
        .padding(3).frame(height: 40)
        .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
        .accessibilityElement(children: .contain)
        .accessibilityLabel(AureaText.t("ui3d_tabs"))
    }

    // --- Texto 3D -------------------------------------------------------------

    private var textMaterialTab: some View {
        VStack(alignment: .leading, spacing: 0) {
            section("ui3d_ready_materials")
            horizontal {
                ForEach([6, 0, 1, 2, 3, 4, 5], id: \.self) { index in
                    swatchChip(presetKeys[index], swatch: presetSwatches[index]) {
                        finishEditing()
                        _ = model.engine.applyText3DPreset(layerId, preset: UInt32(index))
                        refresh()
                    }
                    .accessibilityIdentifier("text3d.materialPreset.\(index)")
                }
            }
            gap(6)
            colorRow("panel_cor", key: "color", region: 0, height: 48)
            textRow("pn_t3d_metallic", key: "metallic", max: 1, reset: 0, gesture: "metalico")
            textRow("pn_t3d_roughness", key: "roughness", max: 1, reset: 0.35, gesture: "rugosidade")
            advancedHeader(open: materialAdvanced) { materialAdvanced.toggle() }
            if materialAdvanced {
                section("ui3d_finish")
                horizontal {
                    ForEach(Array(finishKeys.enumerated()), id: \.offset) { item in
                        chip(item.element, on: Int(number("surfaceFinish")) == item.offset) {
                            set3D("surfaceFinish", value: Float(item.offset))
                        }
                    }
                }
                textRow("pn_t3d_specular", key: "specular", max: 1, reset: 1, gesture: "especular")
                colorRow("pn_t3d_emissive", key: "emissive", region: 3)
                textRow("pn_t3d_emissive_strength", key: "emissiveStrength", max: 8, reset: 1, gesture: "forca da emissao", step: 2)
                toggleRow("pn_t3d_regions", on: number("regionMaterials") > 0.5) { set3D("regionMaterials", value: $0 ? 1 : 0) }
                if number("regionMaterials") > 0.5 {
                    // Frente = a cor acima. Aqui vão a lateral e o chanfro.
                    textRow("pn_t3d_region_side", key: "sideRoughness", max: 1, reset: 0.35, gesture: "rugosidade da lateral")
                    colorRow("pn_t3d_region_bevel", key: "bevelColor", region: 2)
                    textRow(AureaText.t("pn_t3d_metallic") + " · " + AureaText.t("pn_t3d_region_bevel"), key: "bevelMetallic",
                            max: 1, reset: 0, gesture: "metalico do chanfro", localized: true)
                }
            }
        }
    }

    private var textShapeTab: some View {
        VStack(alignment: .leading, spacing: 0) {
            section("ui3d_text")
            HStack(spacing: 6) {
                chip("t3d_font") { openFonts() }
                chip("t3d_import_font") { finishEditing(); fontTarget = layerId; pickingFont = true }
            }
            row("panel_alinhamento", height: 48) {
                HStack(spacing: 6) {
                    ForEach(Array(["panel_esquerda", "panel_centro", "panel_direita"].enumerated()), id: \.offset) { item in
                        chip(item.element, on: Int(number("alignment")) == item.offset) {
                            set3D("alignment", value: Float(item.offset))
                        }
                    }
                }
            }
            section("ui3d_extrusion")
            // 100 % = a altura da letra (a malha é gerada de novo a cada passo).
            textRow("pn_depth", key: "depth", max: 3, reset: 0.25, gesture: "profundidade do texto 3D")
            note("ui3d_depth_hint")
            gap(6)
            // O chanfro é GEOMETRIA: frente/fundo recuados e o anel do chanfro.
            toggleRow("pn_t3d_bevel", on: number("bevel") > 0.5) { set3D("bevel", value: $0 ? 1 : 0) }
            if number("bevel") > 0.5 {
                textRow("pn_t3d_bevel_width", key: "bevelWidth", max: 0.2, reset: 0.02, gesture: "chanfro", step: 0.05, decimals: 1)
                textRow("pn_t3d_bevel_depth", key: "bevelDepth", max: 0.2, reset: 0.02, gesture: "chanfro", step: 0.05, decimals: 1)
                textRow("pn_t3d_bevel_roundness", key: "bevelRoundness", max: 1, reset: 1, gesture: "arredondamento")
                row("pn_t3d_bevel_segments", height: 48) {
                    HStack(spacing: 6) {
                        ForEach([1, 2, 3, 5, 8], id: \.self) { count in
                            T3DChip(label: "\(count)", on: Int(number("bevelSegments")) == count) {
                                set3D("bevelSegments", value: Float(count))
                            }
                        }
                    }
                }
            }
            gap(10)
            actionCard("ui3d_deform_letters") {
                finishEditing()
                let type = fxEffectTypeId("aurea.text3d.layout")
                let target = layerId
                if !model.effects.contains(where: { $0.typeId == type }) {
                    model.mutate { $0.addEffect(type, toLayer: target, at: UInt32.max) }
                }
                model.openPanel(.effects)
            }
        }
    }

    /// Linha de valor do texto 3D em % (1 = 100 %): arrasto em passos leves (a
    /// malha acompanha), um passo de desfazer por gesto; teclado e ↺ gravam.
    private func textRow(_ label: String, key: String, max: Float, reset: Float, gesture: String,
                         step: Float = 0.5, decimals: Int = 0, localized: Bool = false) -> some View {
        T3DRow(label: localized ? label : AureaText.t(label), value: number(key) * 100, step: step, range: 0...(max * 100),
               unit: "%", decimals: decimals, reset: reset * 100,
               onStart: { beginContinuous(gesture) }, onValue: { lazyNumber(key, $0 / 100) }, onEnd: finishEditing,
               onCommit: { value in beginContinuous(gesture); lazyNumber(key, value / 100); finishEditing() })
    }

    // --- Luz e cena -----------------------------------------------------------

    private func setScene(_ index: UInt32, _ value: Float) {
        _ = model.engine.setSceneSetting(index, value: value)
        sceneSettings = model.engine.sceneSettings().map(\.floatValue)
        model.refreshModel(force: true)
    }

    /// LUZ E CENA: sombras do objeto, estúdio e ambiente na frente; qualidade,
    /// tom, exposição, brilho e o ambiente do objeto em "Avançado".
    private var lightSceneTab: some View {
        VStack(alignment: .leading, spacing: 0) {
            if !isCamera && shadows.count == 2 {
                section("ui3d_shadows")
                toggleRow("pn_t3d_cast_shadow", on: shadows[0] > 0.5) { value in
                    finishEditing()
                    _ = model.engine.setModelShadows(layerId, cast: value, receive: shadows[1] > 0.5)
                    refresh()
                }
                toggleRow("pn_t3d_receive_shadow", on: shadows[1] > 0.5) { value in
                    finishEditing()
                    _ = model.engine.setModelShadows(layerId, cast: shadows[0] > 0.5, receive: value)
                    refresh()
                }
            }
            if sceneSettings.count >= 8 {
                section("scene_studio")
                horizontal {
                    ForEach(Array(["scene_none", "scene_dark", "scene_product", "scene_sky"].enumerated()), id: \.offset) { i, key in
                        T3DChip(label: AureaText.t(key), on: Int(sceneSettings[0]) == i) { setScene(0, Float(i)) }
                    }
                }
                toggleRow("scene_floor", on: sceneSettings[1] > 0) { setScene(1, $0 ? 1 : 0) }
            }
            section("environment_texture")
            horizontal {
                chip("panel_estudio_neutro", on: environment[0] < 0.5) {
                    finishEditing(); model.engine.clearHdri(); refresh()
                }
                chip(environment[0] >= 0.5 ? "panel_imagem_ambiente" : "panel_usar_imagem_ambiente_hdr",
                     on: environment[0] >= 0.5) { pickHdri(object: false) }
            }
            sceneRow("panel_intensidade", value: environment[1] * 100, range: 0...2000, unit: "%", reset: 100, gesture: "ambiente") { value in
                _ = model.engine.setEnvironmentIntensity(value / 100, rotation: environment[2]); refresh()
            }
            sceneRow("pn_env_rotate_light", value: environment[2], range: -360...360, unit: "°", reset: 0, gesture: "ambiente") { value in
                _ = model.engine.setEnvironmentIntensity(environment[1], rotation: value); refresh()
            }
            toggleRow("environment_background", on: environment.count > 3 && environment[3] > 0.5) {
                _ = model.engine.setEnvironmentBackground($0); refresh()
            }
            note("environment_hint")
            gap(4)
            advancedHeader(open: sceneAdvanced) { sceneAdvanced.toggle() }
            if sceneAdvanced {
                if sceneSettings.count >= 8 {
                    section("scene_quality")
                    horizontal {
                        ForEach(Array(["scene_auto", "scene_low", "scene_medium", "scene_high", "scene_ultra"].enumerated()), id: \.offset) { i, key in
                            T3DChip(label: AureaText.t(key), on: Int(sceneSettings[2]) == i) { setScene(2, Float(i)) }
                        }
                    }
                    section("scene_tonemap")
                    horizontal {
                        T3DChip(label: "PBR Neutral", on: sceneSettings[3] == 0) { setScene(3, 0) }
                        T3DChip(label: "AgX", on: sceneSettings[3] == 1) { setScene(3, 1) }
                    }
                    sceneRow("scene_exposure", value: Swift.min(4, Swift.max(0.01, sceneSettings[4])) * 100, range: 1...400, unit: "%", reset: 100, gesture: nil) {
                        setScene(4, $0 / 100)
                    }
                    toggleRow("scene_bloom", on: sceneSettings[5] > 0) { setScene(5, $0 ? 1 : 0) }
                    if sceneSettings[5] > 0 {
                        sceneRow("ui3d_bloom_strength", value: Swift.min(4, Swift.max(0, sceneSettings[6])) * 100, range: 0...400, unit: "%", reset: 100, gesture: nil) {
                            setScene(6, $0 / 100)
                        }
                    }
                }
                if !isCamera { objectEnvironmentSection }
            }
        }
    }

    /// Linha de valor da cena/ambiente: um passo de desfazer por arrasto (quando há `gesture`).
    private func sceneRow(_ key: String, value: Float, range: ClosedRange<Float>, unit: String, reset: Float,
                          gesture: String?, onValue: @escaping (Float) -> Void) -> some View {
        T3DRow(label: AureaText.t(key), value: value, step: 1, range: range, unit: unit, decimals: 0, reset: reset,
               onStart: { if let gesture { beginContinuous(gesture) } }, onValue: onValue,
               onEnd: { if gesture != nil { finishEditing() } },
               onCommit: { v in if let gesture { beginContinuous(gesture) }; onValue(v); if gesture != nil { finishEditing() } })
    }

    @ViewBuilder private var objectEnvironmentSection: some View {
        if objectEnvironment.count >= 5 {
            let own = objectNumber(0) >= 0.5
            section("panel_ambiente_do_objeto")
            horizontal {
                chip("panel_do_projeto", on: !own) { finishEditing(); setObjectEnvironment(0) }
                chip("panel_proprio", on: own) {
                    finishEditing(); setObjectEnvironment(1)
                    if objectEnvironment[1].int64Value <= 0 { pickHdri(object: true) }
                }
                if own {
                    chip(objectEnvironment[1].int64Value > 0 ? "panel_trocar_imagem" : "panel_usar_imagem_ambiente_hdr",
                         on: objectEnvironment[1].int64Value > 0) { pickHdri(object: true) }
                }
            }
            if own {
                sceneRow("panel_intensidade", value: objectNumber(2) * 100, range: 0...2000, unit: "%", reset: 100, gesture: "ambiente") {
                    setObjectEnvironment(1, intensity: $0 / 100)
                }
                sceneRow("pn_env_rotate_light", value: objectNumber(3), range: -360...360, unit: "°", reset: 0, gesture: "ambiente") {
                    setObjectEnvironment(1, rotation: $0)
                }
                sceneRow("panel_exposicao", value: objectNumber(4) * 100, range: 5...2000, unit: "%", reset: 100, gesture: "ambiente") {
                    setObjectEnvironment(1, exposure: $0 / 100)
                }
            }
        }
    }

    // --- Peças comuns ---------------------------------------------------------

    private func section(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 13, weight: .bold))
            .foregroundStyle(AureaColors.muted).padding(.top, 8).padding(.bottom, 4)
    }
    private func note(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 12)).lineSpacing(2)
            .foregroundStyle(AureaColors.muted).fixedSize(horizontal: false, vertical: true)
    }
    private func gap(_ height: CGFloat) -> some View { Spacer().frame(height: height) }
    /// "Avançado ▾": o resto, fechado por padrão; a linha inteira é o alvo.
    private func advancedHeader(open: Bool, toggle: @escaping () -> Void) -> some View {
        Button(action: toggle) {
            HStack(spacing: 6) {
                Text(AureaText.t("panel_avancado")).font(.aurea(size: 13, weight: .semibold)).foregroundStyle(AureaColors.muted)
                CupertinoGlyph.text(open ? CupertinoGlyph.ChevronUp : CupertinoGlyph.ChevronDown, size: 12, color: AureaColors.muted)
                Rectangle().fill(AureaColors.border).frame(height: 1).padding(.leading, 8)
            }.frame(height: 44).contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .accessibilityAddTraits(open ? [.isSelected] : [])
    }
    private func actionCard(_ key: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(AureaText.t(key)).font(.aurea(size: 14, weight: .semibold)).foregroundStyle(AureaColors.text)
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(.horizontal, 14).padding(.vertical, 11).frame(minHeight: 44)
                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 12))
                .contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle())
    }
    private func swatchChip(_ key: String, swatch: [Float], action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 7) {
                Circle().fill(AureaColorSpace.color(swatch + [1])).frame(width: 16, height: 16)
                Text(AureaText.t(key)).font(.aurea(size: 12.5)).foregroundStyle(AureaColors.text)
            }
            .padding(.horizontal, 10).frame(minHeight: 44)
            .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 9))
            .contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle())
    }
    private func toggleRow(_ key: String, on: Bool, onChange: @escaping (Bool) -> Void) -> some View {
        HStack(spacing: 0) {
            Text(AureaText.t(key)).font(.aurea(size: 14, weight: .semibold)).foregroundStyle(AureaColors.text)
                .frame(maxWidth: .infinity, alignment: .leading)
            AureaToggle(checked: on, onCheckedChange: onChange)
        }
        .frame(height: 48).contentShape(Rectangle())
        .onTapGesture { onChange(!on) }
        .accessibilityElement(children: .combine)
    }
    /// "Efeito das partes" (forma 3D): põe o Shape 3D Layout (uma vez) e abre os efeitos.
    private var shapeLayoutButton: some View {
        VStack(alignment: .leading, spacing: 2) {
            Button(AureaText.t("shape3d_layout_effect")) {
                let type = fxEffectTypeId("aurea.shape3d.layout")
                let target = layerId
                if !model.effects.contains(where: { $0.typeId == type }) {
                    model.mutate { $0.addEffect(type, toLayer: target, at: UInt32.max) }
                }
                model.openPanel(.effects)
            }
            .font(.aurea(size: 14)).frame(minHeight: 44)
            .accessibilityIdentifier("shape3d.layout")
            Text(AureaText.t("shape3d_layout_hint")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                .fixedSize(horizontal: false, vertical: true)
        }.padding(.top, 10)
    }
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
    private func colorRow(_ title: String, key: String, region: UInt32, height: CGFloat = 48) -> some View {
        row(title, height: height) {
            // Element3DPanel passes these material channels directly as sRGB.
            Button { openColor(key, region: region) } label: {
                AureaColorSwatch(color: AureaColorSpace.color(colorValues(key)))
                    .frame(width: 30, height: 30).clipShape(RoundedRectangle(cornerRadius: 6))
            }.buttonStyle(AureaPressStyle()).accessibilityLabel(AureaText.t(title))
        }
    }

    private func number(_ key: String) -> Float { (text3D[key] as? NSNumber)?.floatValue ?? 0 }
    private func objectNumber(_ index: Int) -> Float {
        // A selection refresh may clear the query while a gesture is ending.
        guard objectEnvironment.indices.contains(index) else { return index == 2 || index == 4 ? 1 : 0 }
        return objectEnvironment[index].floatValue
    }
    private func rounded(_ value: Float) -> Int { Int(floor(Double(value) + 0.5)) }
    private func percent(_ value: Float) -> String { "\(rounded(value * 100))%" }

    @ViewBuilder private var importedMaterialSection: some View {
        if let material = importedMaterials.first(where: { UInt32($0[0]) == selectedMaterial }) ?? importedMaterials.first {
            let index = UInt32(material[0])
            Menu {
                ForEach(importedMaterials.indices, id: \.self) { row in
                    Button(AureaText.t("i18n_material_n", Int(importedMaterials[row][0]) + 1)) { selectedMaterial = UInt32(importedMaterials[row][0]) }
                }
            } label: { Text(AureaText.t("i18n_material_n", Int(index) + 1)).font(.aurea(size: 14)).frame(minHeight: 44) }
            ForEach(0..<6, id: \.self) { param in
                materialControl(material, index: index, param: param)
            }
        }
    }

    private func materialControl(_ material: [Float], index: UInt32, param: Int) -> some View {
        let labels = ["R", "G", "B", AureaText.t("tl_alpha"), AureaText.t("pn_t3d_metallic"), AureaText.t("pn_t3d_roughness")]
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
        sceneSettings = model.engine.sceneSettings().map(\.floatValue)
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

/// FormaKit.HumanRow do painel 3D: rótulo, régua, valor (teclado) e ↺ ao padrão.
private struct T3DRow: View {
    @EnvironmentObject private var model: AureaModel
    let label: String
    let value: Float
    let step: Float
    let range: ClosedRange<Float>
    var unit = ""
    var decimals = 0
    let reset: Float
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
            PropertyLabelChip(label, expression: .none, keyframe: .none, onTap: nil)
            Color.clear.frame(width: 6)
            TickRuler(value: { shown }, unitsPerDp: step, active: true, height: 40, verticalPadding: 8)
                .frame(maxWidth: .infinity).padding(.vertical, 4)
                .valueDrag(enabled: true, start: { value }, unitsPerDp: { step }, min: range.lowerBound, max: range.upperBound,
                           onStart: { live = value; dragging = true; onStart() }, onValue: { live = $0; onValue($0) },
                           onEnd: { dragging = false; onEnd() })
            Color.clear.frame(width: 6)
            ValueBox(comUnidade(numeroPtBr(shown, casas: decimals), unit), onTap: {
                model.numericKeypad = KeypadRequest(title: label, value: value, unit: unit, min: range.lowerBound, max: range.upperBound,
                                                    decimals: decimals) { onCommit($0.clamped(to: range)) }
            })
            Button { onCommit(reset) } label: {
                CupertinoGlyph.text(CupertinoGlyph.ArrowCounterclockwise, size: 16, color: AureaColors.muted).frame(width: 34, height: 44)
            }.buttonStyle(AureaPressStyle(shrink: 1)).opacity(resetVisible ? 1 : 0).disabled(!resetVisible)
                .accessibilityLabel(AureaText.t("panel_voltar_padrao") + " · " + label)
        }.frame(height: 48).onDisappear { if dragging { dragging = false; onEnd() } }
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
                .frame(minHeight: 44)
                .background(on ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                .contentShape(Rectangle())
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

/// TextAnimSection / Text3DAnimSection.kt: animação de texto do texto 3D — grade
/// de presets (um toque aplica), entrada / saída / loop, unidade, duração e
/// atraso. São os animadores de camada por letra do motor (preview = export).
/// `parts` = forma 3D: a mesma seção, cada parte no papel de uma letra (o
/// motor força a unidade "parte").
private struct Text3DAnimSection: View {
    @EnvironmentObject private var model: AureaModel
    let layerId: Int64
    var parts = false
    @State private var anim: [Float] = []
    @State private var mode = 0
    @State private var unit = 1
    @State private var duration: Float = 0.6
    @State private var stagger: Float = 60

    private static let floats = 5   // Engine::kText3DAnimFloats
    private let presetKeys = ["t3a_none", "t3a_fade", "t3a_rise", "t3a_drop", "t3a_pop", "t3a_spin_y", "t3a_flip_x",
                              "t3a_typewriter", "t3a_wave", "t3a_cascade", "t3a_zoom", "t3a_swing"]
    private var current: Int { anim.count >= 15 ? Int((anim[mode * Self.floats]).rounded()) : -1 }

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Spacer().frame(height: 14)
            Text(AureaText.t(parts ? "s3a_title" : "t3a_title")).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted)
            Text(AureaText.t(parts ? "s3a_hint" : "t3a_hint")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                .fixedSize(horizontal: false, vertical: true)
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    ForEach(Array(["t3a_in", "t3a_out", "t3a_loop"].enumerated()), id: \.offset) { index, key in
                        let used = anim.count >= 15 && anim[index * Self.floats] >= 0
                        T3DChip(label: AureaText.t(key) + (used ? " \u{2022}" : ""), on: mode == index) { mode = index; sync() }
                    }
                }
            }
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    Text(AureaText.t("panel_anima_cada")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                    if parts {
                        T3DChip(label: AureaText.t("shape3d_parts"), on: true) {}
                    } else {
                        ForEach(Array(["panel_letra", "panel_palavra", "panel_linha"].enumerated()), id: \.offset) { index, key in
                            T3DChip(label: AureaText.t(key), on: unit == index + 1) { unit = index + 1; reapply() }
                        }
                    }
                }
            }
            // Grade de presets: três por linha; um toque aplica (ou troca) o do modo.
            ForEach(0..<((presetKeys.count + 2) / 3), id: \.self) { row in
                HStack(spacing: 6) {
                    ForEach(row * 3..<min(presetKeys.count, row * 3 + 3), id: \.self) { index in
                        T3DChip(label: AureaText.t(presetKeys[index]), on: current == index - 1) { apply(index - 1) }
                    }
                }
            }
            slider("t3a_duration", value: $duration, range: 0.1...3, shown: String(format: "%.1f s", duration))
            slider("t3a_stagger", value: $stagger, range: 0...500, shown: "\(Int(stagger.rounded())) ms")
        }
        .onAppear { load() }
        .onChange(of: layerId) { _ in load() }
        .onChange(of: model.status.modelRevision) { _ in load() }
    }

    private func slider(_ key: String, value: Binding<Float>, range: ClosedRange<Float>, shown: String) -> some View {
        HStack(spacing: 8) {
            Text(AureaText.t(key)).font(.aurea(size: 12)).foregroundStyle(AureaColors.text).frame(width: 118, alignment: .leading)
            Slider(value: value, in: range, onEditingChanged: { editing in if !editing { reapply() } })
            Text(shown).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).frame(width: 52, alignment: .trailing)
        }.frame(height: 44)
    }
    private func load() {
        anim = model.engine.text3DAnim(layerId).map(\.floatValue)
        sync()
    }
    /// O modo escolhido mostra o que já está aplicado nele.
    private func sync() {
        guard anim.count >= 15, anim[mode * Self.floats] >= 0 else { return }
        let row = mode * Self.floats
        unit = min(3, max(1, Int(anim[row + 1].rounded())))
        duration = min(3, max(0.1, anim[row + 2]))
        stagger = min(500, max(0, anim[row + 3]))
    }
    private func apply(_ preset: Int) {
        _ = model.engine.applyText3DAnim(layerId, preset: Int32(preset), mode: UInt32(mode), unit: UInt32(unit),
                                          duration: duration, stagger: stagger)
        model.refreshModel(force: true)
        load()
    }
    private func reapply() { if current >= 0 { apply(current) } }
}
