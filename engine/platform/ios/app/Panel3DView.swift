// Element3DPanel.kt: o painel 3D em ABAS. Forma 3D e modelo importado:
// Ambiente (luz, fundo e chão — "só o básico", beta 12/10/2026), Material e
// Partes (escolher a parte e pôr um EFEITO nela, como o Text 3D Layout faz
// no texto). Texto 3D: Material, Forma, Ambiente e Animação. Câmera: só o
// Ambiente. Cada linha de valor é a mesma T3DRow; os comandos do motor são
// os mesmos de antes.
import SwiftUI
import UniformTypeIdentifiers

/// As abas do painel 3D (a ordem é a da tela; o rawValue é o mesmo do Android).
/// 0.4.6: pedido de aba ao abrir o painel 3D (par do Element3DRequest do Android).
enum Panel3DRequest { @MainActor static var tab: Panel3DTab? }

/// F6 (13/11/2026; par do `Face3D` do Android): os quatro assuntos do 3D, um botão cada —
/// Cor e preenchimento → Material; Editar → Objeto 3D; Opções da luz e a Cena 3D da câmera →
/// Cena 3D; Rig e mocap → Rig. `.all` = todas as abas (volta a ele quando o painel fecha).
enum Face3D {
    case all, material, object, rig, scene
    var tabs: Set<Panel3DTab>? {
        switch self {
        case .all: return nil
        case .material: return [.material, .texture, .shadow]
        case .object: return [.shape, .parts, .anim]
        case .rig: return [.rig]
        case .scene: return [.light, .render]
        }
    }
    /// 13/11/2026: no MODELO IMPORTADO, Material e Objeto 3D são o mesmo painel simples do
    /// modelo (Material · Textura · Animação · Sombra); os outros assuntos não mudam.
    static let modelTabs: Set<Panel3DTab> = [.material, .texture, .anim, .shadow]
    func tabs(importedModel: Bool) -> Set<Panel3DTab>? {
        importedModel && (self == .material || self == .object) ? Face3D.modelTabs : tabs
    }
    var titleKey: String? {
        switch self {
        case .all: return nil
        case .material: return "ui3d_tab_material"
        case .object: return "fn_3d_object"
        case .rig: return "rig3d_tab"
        case .scene: return "fn_3d_scene"
        }
    }
}

enum Panel3DTab: Int, CaseIterable {
    // texture e shadow (13/11/2026): as abas do modelo importado (par do Tab3D do Android).
    case material, shape, light, anim, parts, rig, render, texture, shadow
    var key: String { ["ui3d_tab_material", "ui3d_tab_shape", "sh_dock_environment", "ui3d_tab_anim", "shape3d_parts", "rig3d_tab",
                       "env3d_tab_render", "fn_part_texture", "m3d_tab_light_shadow"][rawValue] }
    var id: String { ["material", "shape", "light", "anim", "parts", "rig", "render", "texture", "shadow"][rawValue] }
    var glyph: Character {
        [CupertinoGlyph.CubeFill, CupertinoGlyph.Textformat, CupertinoGlyph.Lightbulb, CupertinoGlyph.Sparkles,
         CupertinoGlyph.SquareOnSquare, CupertinoGlyph.PersonCropCircle, CupertinoGlyph.Camera,
         CupertinoGlyph.Photo, CupertinoGlyph.CircleLefthalfFill][rawValue]
    }
}

struct Panel3DView: View {
    @EnvironmentObject private var model: AureaModel
    /// A linha escolhida no Material/Animação do modelo importado (o ◆ do trilho marca nela).
    @ObservedObject private var keyTarget = Model3DKeyTarget.shared
    @State private var sceneSettings: [Float] = []
    @State private var text3D: [String: Any] = [:]
    /// Receita da Imagem 3D (scene3d::image3d_to_fields); vazia = não é Imagem 3D.
    @State private var image3D: [Float] = []
    @State private var draft = ""
    @State private var environment: [Float] = [0, 1, 0]
    @State private var objectEnvironment: [NSNumber] = []
    @State private var importedMaterials: [[Float]] = []
    @State private var selectedMaterial: UInt32 = 0
    @State private var shadows: [Float] = []
    /// Mostrar interior: 1/0; −1 = não é objeto 3D (câmera).
    @State private var interior: Int32 = -1
    @ObservedObject private var thumbs = MaterialThumbStore.shared
    /// 0.3.6: a parte escolhida da forma 3D decide qual material a Textura cobre.
    @ObservedObject private var shapeState = Shape3DState.shared
    @State private var hdriTarget: Int64?
    @State private var pickingTextTexture = false
    @State private var pending: Text3DChange?
    @State private var rebuild: DispatchWorkItem?
    @State private var closeTyping: DispatchWorkItem?
    @State private var typingActive = false
    @State private var gestureOpen = false
    @State private var tab: Panel3DTab?
    /// Seções que a pessoa recolheu ou abriu, em relação ao padrão delas.
    @State private var flippedSections: Set<String> = []
    /// 0.3.6 textura do material: o material que o seletor de imagem cobre
    /// (nulo = a textura do texto 3D, o uso de antes do mesmo seletor).
    @State private var materialTextureTarget: UInt32?
    @State private var selectedScalar: String?
    /// Parte escolhida do MODELO importado (forma 3D usa a do palco).
    @State private var modelPart = 0
    @FocusState private var editingText: Bool

    private var layerId: Int64 { model.primarySelection ?? 0 }
    private let presetKeys = ["pn_t3d_preset_chrome", "pn_t3d_preset_gold", "pn_t3d_preset_brushed",
                              "pn_t3d_preset_glossy", "pn_t3d_preset_matte", "pn_t3d_preset_neon", "pn_t3d_preset_cinematic",
                              // 0.4.0: estilos de tipografia 3D (motor 7..13).
                              "pn_t3d_preset_puffy", "pn_t3d_preset_retro80", "pn_t3d_preset_neon_edge", "pn_t3d_preset_block",
                              "pn_t3d_preset_ice", "pn_t3d_preset_stone", "pn_t3d_preset_rose_gold"]
    /// Cor da amostra de cada material pronto (a mesma ordem do motor).
    private let presetSwatches: [[Float]] = [[0.95, 0.96, 0.98], [1, 0.77, 0.34], [0.78, 0.79, 0.8], [0.9, 0.1, 0.12],
                                             [0.85, 0.85, 0.86], [0.1, 1, 0.85], [0.66, 0.68, 0.72],
                                             [1, 0.45, 0.72], [0.75, 0.1, 0.62], [1, 0.2, 0.85], [1, 0.82, 0.2],
                                             [0.78, 0.92, 1], [0.55, 0.53, 0.5], [0.98, 0.68, 0.6]]
    private let finishKeys = ["ui3d_finish_smooth", "ui3d_finish_brushed", "ui3d_finish_scratched",
                              "ui3d_finish_hammered", "ui3d_finish_weathered"]

    /// Câmera (8) e luz (9) não são objetos: só a cena (Ambiente e Render).
    private var isCamera: Bool { model.selectedLayer?.kind == 8 || model.selectedLayer?.kind == 9 }
    private var isShape: Bool { !isCamera && !model.engine.shape3D(layerId).isEmpty }
    private var isText: Bool { !isCamera && !text3D.isEmpty }
    private var isImage: Bool { !isCamera && !image3D.isEmpty }
    /// 13/11/2026 (conceito do dono): o MODELO IMPORTADO é uma camada com um painel simples —
    /// Material · Textura · Animação · Sombra (Model3DMaterialView e vizinhos, no fim do arquivo).
    private var isImported: Bool { !isCamera && !isShape && !isText && !isImage && model.selectedLayer?.kind == 10 }
    /// Material, Textura e Animação do modelo importado: o ◆ da coluna esquerda marca a linha escolhida.
    private var modelKeyed: Bool { isImported && (currentTab == .material || currentTab == .texture || currentTab == .anim) }
    /// F6: só as abas do assunto aberto (`model.face3D`); vazio = todas.
    private var tabs: [Panel3DTab] {
        let all = allTabs
        guard let face = model.face3D.tabs(importedModel: isImported) else { return all }
        let shown = all.filter { face.contains($0) }
        return shown.isEmpty ? all : shown
    }
    private var allTabs: [Panel3DTab] {
        if isCamera { return [.light, .render] }
        if isImage { return [.material, .shape, .light, .render] }
        if isText { return [.material, .shape, .light, .anim, .render] }
        // 0.3.6: modelo importado ganha a aba Esqueleto (AutoRig 3D, ossos, IK, Motion Capture).
        if isImported { return [.material, .texture, .anim, .shadow, .rig, .light, .render] }
        return [.light, .material, .parts, .render]
    }
    /// Editar do modelo importado abre na Animação; Cor e preenchimento, no Material.
    private var currentTab: Panel3DTab {
        if let tab, tabs.contains(tab) { return tab }
        if isImported && model.face3D == .object && tabs.contains(.anim) { return .anim }
        return tabs[0]
    }

    var body: some View {
        VStack(spacing: 0) {
            PanelHeader(title: AureaText.t(isImported ? "m3d_title" : text3D.isEmpty ? "panel_material_ambiente" : "text_options"), onBack: {
                finishEditing(); model.panel = .none
            })
            // O desenho dos painéis de propriedade: a coluna fixa à esquerda, o conteúdo
            // em seções no centro e as abas em ícones à direita.
            HStack(spacing: 0) {
              LeftRail(keyframeLook: modelKeyed ? keyTarget.look : .none,
                       onKeyframe: modelKeyed ? { keyTarget.toggle?() } : nil,
                       onBack: { finishEditing(); model.panel = .none })
              AureaColors.contentStrongLine.frame(width: 0.7)
            ScrollView {
                VStack(alignment: .leading, spacing: 0) {
                    switch currentTab {
                    case .material:
                        if isImported {
                            Model3DMaterialView(layerId: layerId) { material in
                                let values = importedMaterials.first(where: { UInt32($0[0]) == material }) ?? [Float](repeating: 1, count: 8)
                                return AnyView(materialTextureRow(material, thumb: layerMaterialThumb(material, values: values),
                                                                  fallback: AureaColorSpace.color([values[2], values[3], values[4], 1])))
                            }
                        } else {
                        if isImage { imageMaterialTab }
                        else if isText { textMaterialTab }
                        else if isShape {
                            envSection("panel_material", id: "panel3d.material") {
                                Shape3DPanelSection(layerId: layerId, page: .material)
                                // 0.3.6: a parte escolhida pode vir de uma camada da timeline
                                // (a imagem do arquivo continua na linha "Imagem" acima).
                                let part = Shape3DState.shared.part(model)
                                if part >= 0 {
                                    materialTextureRow(UInt32(part), thumb: shapePartThumb(part), fallback: AureaColors.chip, allowFile: false)
                                }
                            }
                        }
                        else { envSection("panel_material", id: "panel3d.material") { importedMaterialSection } }
                        // 0.3.6: Objeto — projeta/recebe sombra (saíram do Ambiente) e o interior.
                        objectShadowSection
                        interiorToggle.padding(.horizontal, 10)
                        }
                    case .texture:
                        Model3DTextureView(layerId: layerId)
                    case .shadow:
                        // LUZ E SOMBRA do modelo (v76): projeta/recebe, o interior (dupla face) e a
                        // luz só deste objeto (estúdio ou HDRI próprio, intensidade, giro e exposição).
                        objectShadowSection
                        interiorToggle.padding(.horizontal, 10)
                        objectLightSection
                    case .shape:
                        if isImage { imageShapeTab }
                        else if isText { textShapeTab }
                    case .light:
                        environmentTab
                    case .render:
                        renderTab
                    case .anim:
                        if isImported { Model3DAnimationView(layerId: layerId) }
                        else if isText { NativePanelChip(AureaText.t("la_animators")) { finishEditing(); AnimationPanelRequest.tab = .text; model.openPanel(.animation) } }
                    case .parts:
                        partsTab.padding(.horizontal, 10)
                    case .rig:
                        Rig3DSection(layerId: layerId).padding(.horizontal, 10)
                    }
                }
                .padding(.bottom, 24)
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            // Cada aba começa no topo (antes herdava a rolagem da anterior).
            .id(currentTab)
            .onChange(of: currentTab) { _ in keyTarget.clear() }
            .accessibilityIdentifier("text3d.materialScroll")
              if tabs.count > 1 {
                  AureaColors.contentStrongLine.frame(width: 0.7)
                  PanelIconTabs(tabs: tabs.map { PanelIconTab(glyph: $0.glyph, label: AureaText.t($0.key), id: "panel3d.tab.\($0.id)") },
                                selected: tabs.firstIndex(of: currentTab) ?? 0, group: AureaText.t("ui3d_tabs")) { tab = tabs[$0] }
              }
            }
        }
        .id("panel3d:\(model.projectGeneration):\(model.composition[AureaCompositionId] ?? ""):\(layerId)")
        .background(AureaColors.background)
        // 13/11/2026 (pedido do dono: deslizantes do 3D compactos): no 3D o rótulo fica em cima e
        // a régua ocupa a largura toda (par do Element3DPanel.kt, LocalParamRowStacked).
        .environment(\.paramRowStacked, true)
        .sheet(isPresented: $pickingTextTexture) {
            ShellMediaPicker(selectionLimit: 1, filter: .images) { items in
                pickingTextTexture = false
                // 0.3.6: o mesmo seletor serve à textura do material (um `.sheet`
                // só: dois encadeados aqui calavam um dos dois).
                let material = materialTextureTarget
                materialTextureTarget = nil
                if let (url, _) = items.first {
                    if let material { model.setShapePartImage(layerId, part: 0, url: url, material: material) }
                    else { model.setShapePartImage(layerId, part: -1, url: url, textTexture: true) }
                }
            }
        }
        // HDRI pelo seletor do UIKit (DocumentImportPicker, em pickHdri): o
        // `.fileImporter` daqui ficava sob o da raiz do editor e não abria.
        // A fonte vem pelo seletor do UIKit (FontImportPicker): um segundo
        // `.fileImporter` encadeado aqui calava um dos dois.
        .onAppear {
            load()
            // 0.4.6: a Cena 3D abre o painel direto numa aba (Motion Capture → Esqueleto).
            if let requested = Panel3DRequest.tab {
                // Aba pedida fora do assunto atual: mostra tudo (nada fica inalcançável).
                if model.face3D.tabs(importedModel: isImported)?.contains(requested) == false { model.face3D = .all }
                if allTabs.contains(requested) { tab = requested }
                Panel3DRequest.tab = nil
            }
        }
        .onChange(of: model.status.modelRevision) { _ in load() }
        .onChange(of: model.localPlayhead) { _ in loadMaterials() }
        .onChange(of: layerId) { _ in finishEditing(); editingText = false; load() }
        .onChange(of: editingText) { focused in if !focused && typingActive { finishEditing() } }
        .onChange(of: tab) { _ in finishEditing() }
        .onDisappear {
            finishEditing()
            model.text3DFontSheet = nil
            model.face3D = .all
        }
    }

    // --- Texto 3D -------------------------------------------------------------

    private var textMaterialTab: some View {
        VStack(alignment: .leading, spacing: 0) {
            envSection("ui3d_ready_materials", id: "text3d.presets") {
            horizontal {
                ForEach([6, 0, 1, 2, 3, 4, 5] + Array(7...13), id: \.self) { index in
                    // A bola de estúdio do material (o motor calcula; até chegar, a cor chapada).
                    swatchChip(presetKeys[index], swatch: presetSwatches[index], thumb: presetThumb(index)) {
                        finishEditing()
                        model.reportText3D(model.engine.applyText3DPreset(layerId, preset: UInt32(index)))
                        refresh()
                    }
                    .accessibilityIdentifier("text3d.materialPreset.\(index)")
                }
            }
            }
            envSection("panel_material", id: "text3d.material") {
            HStack(spacing: 6) {
                Text(AureaText.t("env3d_texture")).font(.aurea(size: 13.5))
                Spacer()
                let hasTexture = !(text3D["texturePath"] as? String ?? "").isEmpty
                NativePanelChip(AureaText.t(hasTexture ? "shape3d_image_change" : "shape3d_image_pick")) {
                    finishEditing(); pickingTextTexture = true
                }.accessibilityIdentifier("text3d.texture.pick")
                if hasTexture {
                    NativePanelChip(AureaText.t("shape3d_image_clear")) {
                        finishEditing()
                        _ = model.engine.setText3D(forLayer: layerId, property: "texturePath", stringValue: "", numberValue: 0)
                        refresh()
                    }.accessibilityIdentifier("text3d.texture.clear")
                }
            }.frame(minHeight: 48)
            colorRow("panel_cor", key: "color", region: 0, height: 48)
            textRow("pn_t3d_metallic", key: "metallic", max: 1, reset: 0, gesture: "metalico")
            textRow("pn_t3d_roughness", key: "roughness", max: 1, reset: 0.35, gesture: "rugosidade")
            }
            envSection("ui3d_finish", id: "text3d.finish", startsClosed: true) {
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
            envSection("ui3d_text", id: "text3d.text") {
            HStack(spacing: 6) {
                chip("t3d_font") { openFonts() }
                chip("t3d_import_font") {
                    finishEditing()
                    let target = layerId
                    FontImportPicker.presentMultiple { urls in for url in urls { importFont(url, target: urls.count == 1 ? target : nil) } }
                }
            }
            Text(AureaText.t("panel_alinhamento")).font(.aurea(size: 13.5)).foregroundStyle(AureaColors.text).padding(.top, 8).padding(.bottom, 4)
            HStack(spacing: 6) {
                ForEach(Array(["panel_esquerda", "panel_centro", "panel_direita"].enumerated()), id: \.offset) { item in
                    chip(item.element, on: Int(number("alignment")) == item.offset) {
                        set3D("alignment", value: Float(item.offset))
                    }
                }
            }
            }
            envSection("ui3d_extrusion", id: "text3d.extrusion") {
            // 100 % = a altura da letra (a malha é gerada de novo a cada passo).
            textRow("pn_depth", key: "depth", max: 3, reset: 0.25, gesture: "profundidade do texto 3D")
            note("ui3d_depth_hint")
            }
            // O chanfro é GEOMETRIA: frente/fundo recuados e o anel do chanfro.
            envSection("pn_t3d_bevel", id: "text3d.bevel", on: number("bevel") > 0.5, onSwitch: { set3D("bevel", value: $0 ? 1 : 0) }) {
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
            actionCard("ui3d_deform_letters") {
                finishEditing()
                let type = fxEffectTypeId("aurea.text3d.layout")
                let target = layerId
                if !model.effects.contains(where: { $0.typeId == type }) {
                    model.mutate { $0.addEffect(type, toLayer: target, at: UInt32.max) }
                }
                model.openPanel(.effects)
            }
            .padding(10)
        }
    }

    // --- Imagem 3D (PNG para 3D) ------------------------------------------------
    // Índices: scene3d::image3d_to_fields (Image3DField no Android).

    private var imageMaterialTab: some View {
        VStack(alignment: .leading, spacing: 0) {
            envSection("ui3d_ready_materials", id: "image3d.presets") {
                horizontal {
                    ForEach([6, 0, 1, 2, 3, 4, 5] + Array(7...13), id: \.self) { index in
                        swatchChip(presetKeys[index], swatch: presetSwatches[index], thumb: presetThumb(index)) {
                            finishEditing()
                            model.reportText3D(model.engine.applyImage3DPreset(layerId, preset: UInt32(index)))
                            refresh()
                        }
                        .accessibilityIdentifier("image3d.materialPreset.\(index)")
                    }
                }
            }
            // A cor da frente é a própria imagem.
            envSection("image3d_front", id: "image3d.front") {
                imageRow("pn_t3d_metallic", field: 7, max: 1, reset: 0, gesture: "metalico da frente")
                imageRow("pn_t3d_roughness", field: 8, max: 1, reset: 0.45, gesture: "rugosidade da frente")
            }
            imageRegion("pn_t3d_region_side", id: "image3d.side", at: 9)
            imageRegion("pn_t3d_region_bevel", id: "image3d.bevelRegion", at: 14)
            imageRegion("image3d_back", id: "image3d.back", at: 19)
        }
    }

    private func imageRegion(_ title: String, id: String, at: Int) -> some View {
        envSection(title, id: id) {
            row("panel_cor", height: 48) {
                Button { openImageColor(at: at) } label: {
                    AureaColorSwatch(color: AureaColorSpace.color([imageNumber(at), imageNumber(at + 1), imageNumber(at + 2), 1]))
                        .frame(width: 30, height: 30).clipShape(RoundedRectangle(cornerRadius: 6))
                }.buttonStyle(AureaPressStyle()).accessibilityLabel(AureaText.t("panel_cor"))
            }
            imageRow("pn_t3d_metallic", field: at + 3, max: 1, reset: 0, gesture: "metalico da imagem 3D")
            imageRow("pn_t3d_roughness", field: at + 4, max: 1, reset: 0.35, gesture: "rugosidade da imagem 3D")
        }
    }

    private var imageShapeTab: some View {
        VStack(alignment: .leading, spacing: 0) {
            envSection("ui3d_extrusion", id: "image3d.extrusion") {
                imageRow("pn_depth", field: 0, max: 2, reset: 0.14, gesture: "profundidade da imagem 3D")
                note("image3d_depth_hint")
            }
            envSection("image3d_silhouette", id: "image3d.silhouette") {
                imageRow("image3d_threshold", field: 1, max: 0.95, reset: 0.5, gesture: "corte do alfa")
                note("image3d_threshold_hint")
            }
            envSection("pn_t3d_bevel", id: "image3d.bevel", on: imageNumber(2) > 0.5, onSwitch: { on in setImage { $0[2] = on ? 1 : 0 } }) {
                imageRow("pn_t3d_bevel_width", field: 3, max: 0.1, reset: 0.012, gesture: "chanfro", step: 0.05, decimals: 1)
                imageRow("pn_t3d_bevel_depth", field: 4, max: 0.1, reset: 0.012, gesture: "chanfro", step: 0.05, decimals: 1)
                imageRow("pn_t3d_bevel_roundness", field: 5, max: 1, reset: 1, gesture: "arredondamento")
                row("pn_t3d_bevel_segments", height: 48) {
                    HStack(spacing: 6) {
                        ForEach([1, 2, 3, 5, 8], id: \.self) { count in
                            T3DChip(label: "\(count)", on: Int(imageNumber(6)) == count) { setImage { $0[6] = Float(count) } }
                        }
                    }
                }
            }
        }
    }

    private func imageRow(_ label: String, field: Int, max: Float, reset: Float, gesture: String,
                          step: Float = 0.5, decimals: Int = 0) -> some View {
        let key = "image3d.\(field)"
        return T3DRow(label: AureaText.t(label), value: imageNumber(field) * 100, step: step, range: 0...(max * 100),
               unit: "%", decimals: decimals, reset: reset * 100, selected: selectedScalar == key, onSelect: { selectedScalar = key },
               onStart: { beginContinuous(gesture) }, onValue: { value in lazyImage { $0[field] = value / 100 } }, onEnd: finishEditing,
               onCommit: { value in beginContinuous(gesture); lazyImage { $0[field] = value / 100 }; finishEditing() })
    }
    private func imageNumber(_ index: Int) -> Float { image3D.indices.contains(index) ? image3D[index] : 0 }
    private func lazyImage(_ change: (inout [Float]) -> Void) {
        guard !image3D.isEmpty else { return }
        change(&image3D)
        schedule(.image(layerId, image3D), delay: 0.09)
    }
    private func setImage(_ change: (inout [Float]) -> Void) {
        finishEditing()
        guard !image3D.isEmpty else { return }
        change(&image3D)
        model.reportText3D(model.engine.setImage3D(layerId, values: image3D.map { NSNumber(value: $0) }))
        refresh()
    }
    private func openImageColor(at: Int) {
        beginContinuous("cor da imagem 3D")
        let target = layerId
        model.colorSheet = ColorSheetRequest(title: AureaText.t("ds_cor"), initial: [imageNumber(at), imageNumber(at + 1), imageNumber(at + 2), 1], onChange: { r, g, b, _ in
            guard layerId == target else { return }
            lazyImage { $0[at] = r; $0[at + 1] = g; $0[at + 2] = b }
        }, onDone: finishEditing)
    }

    /// Linha de valor do texto 3D em % (1 = 100 %): arrasto em passos leves (a
    /// malha acompanha), um passo de desfazer por gesto; teclado e ↺ gravam.
    private func textRow(_ label: String, key: String, max: Float, reset: Float, gesture: String,
                         step: Float = 0.5, decimals: Int = 0, localized: Bool = false) -> some View {
        return T3DRow(label: localized ? label : AureaText.t(label), value: number(key) * 100, step: step, range: 0...(max * 100),
               unit: "%", decimals: decimals, reset: reset * 100, selected: selectedScalar == key, onSelect: { selectedScalar = key },
               onStart: { beginContinuous(gesture) }, onValue: { lazyNumber(key, $0 / 100) }, onEnd: finishEditing,
               onCommit: { value in beginContinuous(gesture); lazyNumber(key, value / 100); finishEditing() })
    }

    // --- Luz e cena -----------------------------------------------------------

    private func setScene(_ index: UInt32, _ value: Float) {
        _ = model.engine.setSceneSetting(index, value: value)
        sceneSettings = model.engine.sceneSettings().map(\.floatValue)
        model.refreshModel(force: true)
    }

    /// Estúdios da fonte de luz, na ordem dos ids do motor (StudioEnvironment.hpp: 0 neutro … 4 cromado).
    private let studioLightKeys = ["scene031_light_neutral", "scene_dark", "scene_product", "scene_sky", "scene_chrome"]
    /// Operadores de tone map do 3D, na ordem do motor (0 PBR Neutral … 5 ACES).
    private let toneMapKeys = ["scene031_tonemap_neutral", "scene031_tonemap_agx", "scene031_tonemap_agx_punchy",
                               "scene031_tonemap_agx_golden", "scene031_tonemap_uchimura", "scene031_tonemap_aces"]

    /// 0.3.6 AMBIENTE refeito (par do Android: Scene3DEnvironment.kt
    /// `Environment3DTab`): só o ambiente, em seções com título e respiro
    /// (medidas de PanelMetrics) — Céu e fundo, Luz ambiente (com a exposição)
    /// e Chão e sombra. As sombras do objeto foram para a aba Material (seção
    /// Objeto); qualidade, tone map e bloom, para a aba Render.
    ///
    /// LUZ: uma fileira de fontes exclusivas (estúdios e "Imagem HDR…"), só uma
    /// acesa: a HDR quando há HDRI (ela vence o estúdio), senão o estúdio.
    /// Escolher um estúdio tira o HDRI (a ponte faz numa mutação só: um toque =
    /// um passo de desfazer).
    private var environmentTab: some View {
        let hdr = environment[0] >= 0.5
        let ready = sceneSettings.count >= 8
        return VStack(alignment: .leading, spacing: 0) {
            envSection("env3d_section_sky", hint: "env3d_sky_hint", id: "env3d.sky") {
                AureaFlowLayout(hGap: 4, vGap: 0) {
                    if ready {
                        ForEach(Array(studioLightKeys.enumerated()), id: \.offset) { i, key in
                            let on = !hdr && Int(sceneSettings[0]) == i
                            // Tocar no estúdio já aceso não gera passo de desfazer vazio.
                            envChoice(AureaText.t(key), on: on, id: "panel3d.light.\(i)") { if !on { finishEditing(); setScene(0, Float(i)) } }
                        }
                    }
                    // Acesa, toca de novo para trocar a imagem.
                    envChoice(AureaText.t("scene031_light_hdr"), on: hdr, id: "panel3d.light.hdr") { pickHdri(object: false) }
                }
                if hdr {
                    envLine("env3d_hdr_image") {
                        envChoice(AureaText.t("shape3d_image_change"), on: false, id: "env3d.hdr.change") { pickHdri(object: false) }
                    }
                }
                envToggle("environment_background", on: environment.count > 3 && environment[3] > 0.5, id: "env3d.background") {
                    _ = model.engine.setEnvironmentBackground($0); refresh()
                }
            }
            envSection("env3d_section_light", id: "env3d.light") {
                sceneRow("panel_intensidade", value: environment[1] * 100, range: 0...2000, unit: "%", reset: 100, gesture: "ambiente") { value in
                    _ = model.engine.setEnvironmentIntensity(value / 100, rotation: environment[2]); refresh()
                }
                sceneRow("pn_env_rotate_light", value: environment[2], range: -360...360, unit: "°", reset: 0, gesture: "ambiente") { value in
                    _ = model.engine.setEnvironmentIntensity(environment[1], rotation: value); refresh()
                }
                // Exposição: multiplicador linear (1 = 100 %). É do ambiente (claro/escuro da cena).
                if ready {
                    sceneRow("scene_exposure", value: sceneSettings[4] * 100, range: 1...400, unit: "%", reset: 100, gesture: "exposicao 3D") { value in
                        setScene(4, value / 100)
                    }
                }
            }
            if ready {
                // Chão: 0 sem chão, 1 visível, 2 só sombra (compõe sobre o 2D).
                envSection("env3d_section_floor", hint: "env3d_floor_hint", id: "env3d.floor") {
                    let floor = Int(sceneSettings[1])
                    AureaFlowLayout(hGap: 4, vGap: 0) {
                        // 0.4.1: "Reflective Floor" — o chão polido que espelha tudo, num toque. Par do Android.
                        let reflective = sceneSettings.count > 8 && sceneSettings[8] > 0.5
                        ForEach(Array(["scene031_floor_off", "scene031_floor_visible", "scene031_floor_shadow"].enumerated()), id: \.offset) { i, key in
                            envChoice(AureaText.t(key), on: floor == i && !(i == 1 && reflective), id: "panel3d.floor.\(i)") {
                                if floor != i || (i == 1 && reflective) { finishEditing(); setScene(1, Float(i)) }
                            }
                        }
                        envChoice(AureaText.t("scene_floor_reflective"), on: reflective, id: "panel3d.floor.reflective") {
                            if !reflective { finishEditing(); setScene(8, 1) }
                        }
                    }
                    // 0.5.2: chão molhado — poças que espelham as luzes (par do Scene3DEnvironment.kt).
                    if floor >= 1 && sceneSettings.count > 19 {
                        sceneRow("scene052_floor_wet", value: sceneSettings[18] * 100, range: 0...100, unit: "%", reset: 0,
                                 gesture: "chao molhado") { value in setScene(16, value / 100) }
                        if sceneSettings[18] > 0 {
                            sceneRow("scene052_puddle_size", value: sceneSettings[19], range: 20...2000, unit: "px", reset: 260,
                                     gesture: "chao molhado", step: 10) { value in setScene(17, value) }
                        }
                    }
                }
            }
            gap(16)
        }
    }

    /// RENDER (0.3.6): qualidade, tone map e bloom da cena 3D inteira (eram o
    /// "Avançado" do ambiente). Par do Android: `Render3DTab`.
    @ViewBuilder private var renderTab: some View {
        if sceneSettings.count >= 8 {
            VStack(alignment: .leading, spacing: 0) {
                envSection("scene_quality", hint: "env3d_render_hint", id: "env3d.quality") {
                    AureaFlowLayout(hGap: 4, vGap: 0) {
                        ForEach(Array(["scene_auto", "scene_low", "scene_medium", "scene_high", "scene_ultra"].enumerated()), id: \.offset) { i, key in
                            let on = Int(sceneSettings[2]) == i
                            envChoice(AureaText.t(key), on: on, id: "env3d.quality.\(i)") { if !on { finishEditing(); setScene(2, Float(i)) } }
                        }
                    }
                }
                envSection("scene_tonemap", id: "env3d.tonemap") {
                    AureaFlowLayout(hGap: 4, vGap: 0) {
                        ForEach(Array(toneMapKeys.enumerated()), id: \.offset) { i, key in
                            let on = Int(sceneSettings[3]) == i
                            envChoice(AureaText.t(key), on: on, id: "env3d.tonemap.\(i)") { if !on { finishEditing(); setScene(3, Float(i)) } }
                        }
                    }
                }
                // 0.5.1: oclusão ambiente (par do Render3DTab do Android).
                if sceneSettings.count >= 18 {
                    envSection("scene051_ao", hint: "scene051_ao_hint", id: "env3d.ao") {
                        envToggle("scene051_ao", on: sceneSettings[15] > 0.5, id: "env3d.ao.switch") { value in finishEditing(); setScene(13, value ? 1 : 0) }
                        if sceneSettings[15] > 0.5 {
                            sceneRow("scene051_ao_strength", value: sceneSettings[16] * 100, range: 0...300, unit: "%", reset: 100, gesture: "oclusao 3D") { value in
                                setScene(14, value / 100)
                            }
                            sceneRow("scene051_ao_radius", value: sceneSettings[17] * 100, range: 2...20, unit: "%", reset: 7, gesture: "oclusao 3D", step: 0.5, decimals: 1) { value in
                                setScene(15, value / 100)
                            }
                        }
                    }
                }
                // 0.4.6: névoa volumétrica com raios de luz (par do SceneFogSection do Android).
                SceneFogControls().padding(.bottom, 6)
                envSection("scene_bloom", id: "env3d.bloom") {
                    envToggle("scene_bloom", on: sceneSettings[5] > 0.5, id: "env3d.bloom.switch") { value in finishEditing(); setScene(5, value ? 1 : 0) }
                    if sceneSettings[5] > 0.5 {
                        sceneRow("ui3d_bloom_strength", value: sceneSettings[6] * 100, range: 0...400, unit: "%", reset: 60, gesture: "bloom 3D") { value in
                            setScene(6, value / 100)
                        }
                        sceneRow("scene031_bloom_threshold", value: sceneSettings[7], range: 0...8, unit: "", reset: 1.25, gesture: "bloom 3D",
                                 step: 0.05, decimals: 2) { value in
                            setScene(7, value)
                        }
                    }
                }
                gap(16)
            }
        }
    }

    /// OBJETO (aba Material, 0.3.6): projeta / recebe sombra — eram do
    /// ambiente, mas são deste objeto. Par do Android: `ObjectShadowSection`.
    @ViewBuilder private var objectShadowSection: some View {
        if !isCamera && shadows.count == 2 {
            envSection("env3d_section_object", id: "env3d.object") {
                envToggle("pn_t3d_cast_shadow", on: shadows[0] > 0.5, id: "panel3d.shadow.cast") { value in
                    finishEditing()
                    _ = model.engine.setModelShadows(layerId, cast: value, receive: shadows[1] > 0.5)
                    refresh()
                }
                envToggle("pn_t3d_receive_shadow", on: shadows[1] > 0.5, id: "panel3d.shadow.receive") { value in
                    finishEditing()
                    _ = model.engine.setModelShadows(layerId, cast: shadows[0] > 0.5, receive: value)
                    refresh()
                }
            }
        }
    }

    /// LUZ DO OBJETO (v76, aba Luz e sombra do modelo 3D; par do Android `ObjectLightSection`):
    /// o ambiente que ilumina e REFLETE só este objeto — o da cena, um estúdio pronto ou uma
    /// imagem HDR própria — com a intensidade (luz e reflexo), o giro e a exposição. O motor
    /// já tinha (`setObjectEnvironment`); faltava o lugar no painel.
    @ViewBuilder private var objectLightSection: some View {
        if !isCamera && objectEnvironment.count >= 5 {
            let own = objectNumber(0) >= 0.5
            let asset = UInt64(bitPattern: objectEnvironment[1].int64Value)
            // Estúdio pronto: a chave "STUD" + o número do estúdio (1..4) no lugar do asset.
            let studio = own && (asset & 0xFFFF_FFFF_0000_0000) == objectStudioKey ? Int(asset & 0xFFFF_FFFF) : 0
            let hdr = own && studio == 0 && asset != 0
            envSection("m3d_obj_light", hint: "m3d_obj_light_hint", id: "env3d.objectLight") {
                AureaFlowLayout(hGap: 4, vGap: 0) {
                    envChoice(AureaText.t("m3d_obj_light_scene"), on: !own, id: "panel3d.objlight.scene") {
                        if own { finishEditing(); setObjectEnvironment(0) }
                    }
                    ForEach(1..<studioLightKeys.count, id: \.self) { i in
                        envChoice(AureaText.t(studioLightKeys[i]), on: studio == i, id: "panel3d.objlight.\(i)") {
                            if studio != i { finishEditing(); setObjectStudio(i) }
                        }
                    }
                    // Acesa, toca de novo para trocar a imagem.
                    envChoice(AureaText.t("scene031_light_hdr"), on: hdr, id: "panel3d.objlight.hdr") { pickHdri(object: true) }
                }
                if own {
                    sceneRow("m3d_obj_light_intensity", value: objectNumber(2) * 100, range: 0...2000, unit: "%", reset: 100,
                             gesture: "luz do objeto") { setObjectEnvironment(1, intensity: $0 / 100) }
                    sceneRow("pn_env_rotate_light", value: objectNumber(3), range: -360...360, unit: "°", reset: 0,
                             gesture: "luz do objeto") { setObjectEnvironment(1, rotation: $0) }
                    sceneRow("scene_exposure", value: objectNumber(4) * 100, range: 5...400, unit: "%", reset: 100,
                             gesture: "luz do objeto") { setObjectEnvironment(1, exposure: $0 / 100) }
                }
            }
        }
    }

    /// TEXTURA do material `material` (0.3.6; par do Android: `MaterialTextureRow`):
    /// a bola do motor (com a textura nova) e o que cobre o material agora — o
    /// mapa do modelo, uma imagem ou uma camada da timeline — com Arquivo ·
    /// Timeline · Remover. `allowFile` falso na forma 3D (a imagem do arquivo ela
    /// já escolhe na linha "Imagem" da parte).
    /// `thumb`: a MESMA bola das fichas acima (mesma chave e revisão no
    /// MaterialThumbStore — revisões diferentes na mesma chave se pediriam sem fim).
    private func materialTextureRow(_ material: UInt32, thumb: UIImage?, fallback: Color, allowFile: Bool = true) -> some View {
        let info = model.engine.materialTexture(layerId, material: material)
        let kind = (info["kind"] as? NSNumber)?.intValue ?? 0
        let name = info["name"] as? String ?? ""
        let value: String = kind == 1 ? AureaText.t("env3d_texture_image")
            : kind == 2 ? AureaText.t("env3d_texture_layer_value", name.isEmpty ? AureaText.t("env3d_layer_unnamed") : name)
            : AureaText.t("env3d_texture_original")
        return VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 12) {
                MaterialBallView(image: thumb, fallback: fallback, size: 44)
                VStack(alignment: .leading, spacing: 1) {
                    Text(AureaText.t("env3d_texture")).font(.aurea(size: PanelMetrics.label, weight: .semibold)).foregroundStyle(AureaColors.text)
                    Text(value).font(.aurea(size: PanelMetrics.note)).foregroundStyle(kind != 0 ? AureaColors.accent : AureaColors.muted)
                        .lineLimit(1).truncationMode(.middle)
                }
                Spacer(minLength: 0)
            }
            .frame(minHeight: 56)
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(AureaText.t("env3d_texture_desc", Int(material) + 1) + ": " + value)
            HStack(spacing: 8) {
                if allowFile {
                    envChoice(AureaText.t("env3d_texture_from_file"), on: false, id: "panel3d.texture.file") {
                        finishEditing(); materialTextureTarget = material; pickingTextTexture = true
                    }
                }
                envChoice(AureaText.t("env3d_texture_from_timeline"), on: kind == 2, id: "panel3d.texture.timeline") {
                    chooseTextureLayer(material)
                }
                if kind != 0 {
                    envChoice(AureaText.t("env3d_texture_remove"), on: false, id: "panel3d.texture.remove") {
                        finishEditing()
                        _ = model.engine.clearMaterialTexture(layerId, material: material)
                        refresh()
                    }
                }
            }
        }
        .padding(.top, 6)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("panel3d.texture.\(material)")
    }

    /// As camadas que podem cobrir o material (o motor filtra: nunca o próprio
    /// objeto nem o que mora na cena 3D), na folha de ações.
    private func chooseTextureLayer(_ material: UInt32) {
        finishEditing()
        let target = layerId
        let ids = model.engine.materialTextureLayers(target).map(\.int64Value)
        let names = Dictionary(model.layers.map { ($0.id, $0.name) }, uniquingKeysWith: { first, _ in first })
        let actions = ids.map { id in
            SheetAction(names[id].flatMap { $0.isEmpty ? nil : $0 } ?? AureaText.t("env3d_layer_unnamed")) {
                guard model.primarySelection == target else { return }
                if !model.engine.setMaterialTextureLayer(target, material: material, source: id) {
                    model.toast = AureaText.t("env3d_texture_error")
                }
                refresh()
            }
        }
        model.actionSheet = ActionSheetRequest(title: AureaText.t("env3d_texture_layer_title"),
            message: AureaText.t(ids.isEmpty ? "env3d_texture_no_layers" : "env3d_texture_layer_hint"), actions: actions)
    }

    // --- Peças do Ambiente (medidas de PanelMetrics: título 15, rótulo 13, nota 12, chip 36/14, toque 44) ---

    /// Seção do painel: uma faixa de 43 pt com a seta, o título e, se a seção liga e
    /// desliga, o interruptor. Tocar recolhe; os controles ficam soltos embaixo dela.
    private func envSection<Content: View>(_ title: String, hint: String? = nil, id: String, startsClosed: Bool = false,
                                           on: Bool? = nil, onSwitch: ((Bool) -> Void)? = nil,
                                           @ViewBuilder content: () -> Content) -> some View {
        let open = flippedSections.contains(title) == startsClosed
        return VStack(alignment: .leading, spacing: 0) {
            HStack(spacing: 10) {
                CupertinoGlyph.text(CupertinoGlyph.ChevronRight, size: 12, color: AureaColors.muted)
                    .rotationEffect(.degrees(open ? 90 : 0))
                Text(AureaText.t(title)).font(.aurea(size: 14)).foregroundStyle(AureaColors.text).lineLimit(1)
                    .frame(maxWidth: .infinity, alignment: .leading)
                if let on, let onSwitch {
                    Toggle("", isOn: Binding(get: { on }, set: { onSwitch($0) })).labelsHidden().tint(AureaColors.accent)
                }
            }
            .padding(.horizontal, 10).frame(height: 43)
            .background(AureaColors.panelFrameContent)
            .contentShape(Rectangle())
            .onTapGesture {
                withAnimation(.easeInOut(duration: 0.3)) {
                    if flippedSections.contains(title) { flippedSections.remove(title) } else { flippedSections.insert(title) }
                }
            }
            .accessibilityAddTraits([.isHeader, .isButton])
            if open && on != false {
                VStack(alignment: .leading, spacing: 0) {
                    if let hint {
                        HStack(alignment: .top, spacing: 6) {
                            CupertinoGlyph.text(CupertinoGlyph.InfoCircle, size: 14, color: AureaColors.muted)
                            Text(AureaText.t(hint)).font(.aurea(size: PanelMetrics.note)).lineSpacing(2).foregroundStyle(AureaColors.muted)
                                .fixedSize(horizontal: false, vertical: true)
                        }.padding(.bottom, 4)
                    }
                    content()
                }
                .padding(.horizontal, 10).padding(.vertical, 6)
                .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
        .padding(.bottom, 1)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier(id)
    }
    /// Ficha de escolha: 36 de desenho, 44 de toque; "selecionado" para o VoiceOver.
    private func envChoice(_ label: String, on: Bool, id: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(label).font(.aurea(size: 12.5, weight: on ? .semibold : .regular))
                .foregroundStyle(on ? AureaColors.accent : AureaColors.text).lineLimit(1)
                .padding(.horizontal, 10).frame(minHeight: 30)
                .background(on ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 5))
                .frame(minHeight: PanelMetrics.touch).contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .accessibilityAddTraits(on ? [.isSelected] : [])
        .accessibilityIdentifier(id)
    }
    /// Linha "rótulo · interruptor" de 44 (a linha toda toca).
    private func envToggle(_ key: String, on: Bool, id: String, onChange: @escaping (Bool) -> Void) -> some View {
        toggleRow(key, on: on, onChange: onChange).frame(minHeight: 44).accessibilityIdentifier(id)
    }
    /// Linha "rótulo · conteúdo à direita".
    private func envLine<Trailing: View>(_ key: String, @ViewBuilder trailing: () -> Trailing) -> some View {
        HStack(spacing: 0) {
            Text(AureaText.t(key)).font(.aurea(size: 13.5)).foregroundStyle(AureaColors.text)
                .frame(maxWidth: .infinity, alignment: .leading)
            trailing()
        }.frame(minHeight: 44)
    }

    /// PARTES do objeto 3D (Element3DPanel.kt `PartsTab`): escolher a parte e
    /// pôr um efeito nela. Os ajustes e keyframes ficam no painel de Efeitos.
    @ViewBuilder private var partsTab: some View {
        let names = isShape ? [] : model.engine.modelPartNames(layerId)
        VStack(alignment: .leading, spacing: 0) {
            if !isShape && !names.isEmpty {
                // O modelo importado tem as abas Textura e Animação; isto sobra para um objeto 3D
                // sem forma pronta que não é modelo importado (par do Android).
                Parts3DEditor(layerId: layerId, names: names)
            } else {
            if isShape {
                Shape3DPanelSection(layerId: layerId, page: .shape)
            }
            gap(6)
            note("fx3d_parts_hint")
            gap(8)
            partEffectCard("aurea.3d.part_transform", name: "fx3d_name_part_transform", detail: "fx3d_desc_part_transform")
            partEffectCard("aurea.3d.part_hide", name: "fx3d_name_part_hide", detail: "fx3d_desc_part_hide")
            partEffectCard("aurea.3d.part_texture", name: "fx3d_name_part_texture", detail: "fx3d_desc_part_texture")
            }
            if isShape {
                shapeLayoutButton
                // Animações prontas por parte (entrada/saída/loop).
                Text3DAnimSection(layerId: layerId, parts: true)
            }
        }
    }

    private func partEffectCard(_ key: String, name: String, detail: String) -> some View {
        Button {
            finishEditing()
            let part = isShape ? max(0, Shape3DState.shared.part(model)) : modelPart
            if model.addPartEffect(key, part: part) { model.openPanel(.effects) }
        } label: {
            VStack(alignment: .leading, spacing: 2) {
                Text(AureaText.t(name)).font(.aurea(size: 14, weight: .semibold)).foregroundStyle(AureaColors.text)
                Text(AureaText.t(detail)).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                    .fixedSize(horizontal: false, vertical: true)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.horizontal, 14).padding(.vertical, 11)
            .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 12))
        }
        .buttonStyle(AureaPressStyle())
        .padding(.bottom, 6)
        .accessibilityIdentifier("panel3d.fx.\(key.replacingOccurrences(of: "aurea.3d.", with: ""))")
    }

    /// Linha de valor da cena/ambiente: um passo de desfazer por arrasto (quando há `gesture`).
    private func sceneRow(_ key: String, value: Float, range: ClosedRange<Float>, unit: String, reset: Float,
                          gesture: String?, step: Float = 1, decimals: Int = 0,
                          onValue: @escaping (Float) -> Void) -> some View {
        T3DRow(label: AureaText.t(key), value: value, step: step, range: range, unit: unit, decimals: decimals, reset: reset,
               selected: selectedScalar == key, onSelect: { selectedScalar = key },
               onStart: { if let gesture { beginContinuous(gesture) } }, onValue: onValue,
               onEnd: { if gesture != nil { finishEditing() } },
               onCommit: { v in if let gesture { beginContinuous(gesture) }; onValue(v); if gesture != nil { finishEditing() } })
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
    private func actionCard(_ key: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(AureaText.t(key)).font(.aurea(size: 14, weight: .semibold)).foregroundStyle(AureaColors.text)
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(.horizontal, 14).padding(.vertical, 11).frame(minHeight: 44)
                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 12))
                .contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle())
    }
    private func swatchChip(_ key: String, swatch: [Float], thumb: UIImage?, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 7) {
                MaterialBallView(image: thumb, fallback: AureaColorSpace.color(swatch + [1]), size: 30)
                Text(AureaText.t(key)).font(.aurea(size: 12.5)).foregroundStyle(AureaColors.text)
            }
            .padding(.leading, 5).padding(.trailing, 10).frame(minHeight: 44)
            .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 9))
            .contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle())
    }
    /// Ficha de material: bola + nome; acesa = destaque (MaterialChip do Android).
    private func materialChip(_ label: String, on: Bool, thumb: UIImage?, fallback: Color,
                              action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 7) {
                MaterialBallView(image: thumb, fallback: fallback, size: 30)
                Text(label).font(.aurea(size: 12.5)).foregroundStyle(on ? AureaColors.accent : AureaColors.text)
            }
            .padding(.leading, 5).padding(.trailing, 12).frame(minHeight: 44)
            .background(on ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 9))
            .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle())
        .accessibilityAddTraits(on ? [.isSelected] : [])
    }
    /// Bola de um material pronto do texto 3D (não depende do projeto).
    private func presetThumb(_ index: Int) -> UIImage? {
        let key = "p:\(index)"
        let engine = model.engine
        thumbs.request(key, revision: 0) { engine.text3DPresetPreview(UInt32(index), size: materialThumbSide) }
        return thumbs.image(key)
    }
    /// Bola do material `id` do modelo: pedida de novo quando o modelo ou os
    /// valores mudam (a anterior fica na tela até a nova chegar).
    private func layerMaterialThumb(_ id: UInt32, values: [Float]) -> UIImage? {
        let key = "m:\(layerId):\(id)"
        let engine = model.engine, target = layerId
        thumbs.request(key, revision: [Float(model.status.modelRevision)] + values) {
            engine.materialPreview(target, material: id, size: materialThumbSide)
        }
        return thumbs.image(key)
    }
    /// Bola da parte `i` da forma 3D: a mesma chave e revisão das fichas de
    /// Shape3DPanelSection (Shape3DViews.swift), para as duas não brigarem.
    private func shapePartThumb(_ i: Int) -> UIImage? {
        let key = "m:\(layerId):\(i)"
        let engine = model.engine, target = layerId
        thumbs.request(key, revision: model.status.modelRevision) {
            engine.materialPreview(target, material: UInt32(i), size: materialThumbSide)
        }
        return thumbs.image(key)
    }
    /// MOSTRAR INTERIOR (dupla face): as faces de dentro com a mesma textura —
    /// a câmera que entra no cubo vê o lado de dentro. Ligado por padrão nas
    /// formas 3D prontas; modelo importado e texto 3D começam desligados.
    @ViewBuilder private var interiorToggle: some View {
        if interior >= 0 {
            gap(6)
            toggleRow("ui3d_show_interior", on: interior == 1) { value in
                finishEditing()
                _ = model.engine.setModelInterior(layerId, on: value)
                refresh()
            }
            .accessibilityIdentifier("panel3d.interior")
            note("ui3d_show_interior_hint")
            gap(10)
        }
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
            let targetLayer = layerId
            // Element3DPanel.kt: uma ficha por material, com a BOLA do material de
            // verdade (cor, metal, rugosidade e a textura do arquivo), não só o nome.
            horizontal {
                ForEach(importedMaterials.indices, id: \.self) { row in
                    let values = importedMaterials[row]
                    let id = UInt32(values[0])
                    materialChip(AureaText.t("i18n_material_n", Int(id) + 1), on: id == index,
                                 thumb: layerMaterialThumb(id, values: values),
                                 fallback: AureaColorSpace.color([values[2], values[3], values[4], 1])) { selectedMaterial = id }
                        .accessibilityIdentifier("panel3d.material.\(id)")
                }
            }
            // 0.3.6: a TEXTURA do material escolhido, direto aqui (arquivo ou timeline).
            materialTextureRow(index, thumb: layerMaterialThumb(index, values: material),
                               fallback: AureaColorSpace.color([material[2], material[3], material[4], 1]))
            gap(6)
            AureaPropertyPanel(domain: "material", layerId: targetLayer, values: Array(material[2..<8]),
                projectGeneration: model.projectGeneration,
                compositionId: (model.composition[AureaCompositionId] as? NSNumber)?.uint64Value ?? 0, materialIndex: index) { changes in
                model.mutate { core in
                    for (param, value) in changes { _ = core.setMaterial(forLayer: targetLayer, index: index, param: param, value: value) }
                }
                refresh()
            }.id("material:\(model.projectGeneration):\(model.composition[AureaCompositionId] ?? ""):\(targetLayer):\(index)")
        }
    }
    private func degrees(_ value: Float) -> String { "\(rounded(value))°" }
    private func colorValues(_ key: String) -> [Float] {
        let values = (text3D[key] as? [NSNumber] ?? []).map(\.floatValue)
        return (0..<4).map { $0 < values.count ? values[$0] : $0 == 3 ? 1 : 0 }
    }

    private func set3D(_ key: String, value: Float) {
        finishEditing()
        model.reportText3D(model.engine.setText3D(forLayer: layerId, property: key, stringValue: nil, numberValue: value))
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
    /// v76: um estúdio pronto só para este objeto (1 escuro … 4 cromado; motor StudioEnvironment.hpp).
    private func setObjectStudio(_ preset: Int) {
        guard (1...4).contains(preset) else { return }
        _ = model.engine.setObjectEnvironment(forLayer: layerId, source: 1,
                                               hdri: Int64(bitPattern: objectStudioKey | UInt64(preset)),
                                               intensity: objectNumber(2), rotation: objectNumber(3), exposure: objectNumber(4))
        refresh()
    }
    private func pickHdri(object: Bool) {
        finishEditing(); hdriTarget = object ? layerId : nil
        let target = hdriTarget, model = self.model
        let types: [UTType] = [UTType(filenameExtension: "hdr") ?? .data, UTType(filenameExtension: "exr") ?? .data, .zip, .image, .data]
        DocumentImportPicker.present(types: types) { urls in
            guard let url = urls?.first else { return }
            if model.selectedLayer?.kind == 8 { _ = model.engine.setEnvironmentBackground(true) }
            model.importMedia(url: url, kind: .hdri, objectHDRI: target)
        }
    }
    private func openFonts() {
        finishEditing()
        let target = layerId
        model.text3DFontSheet = Text3DFontRequest(fonts: model.engine.availableFonts(),
                                                current: text3D["fontPath"] as? String ?? "") { path in
            guard model.primarySelection == target else { return }
            model.reportText3D(model.engine.setText3D(forLayer: target, property: "fontPath", stringValue: path, numberValue: 0))
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
            model.reportText3D(model.engine.setText3D(forLayer: id, property: key, stringValue: nil, numberValue: value))
        case let .color(id, region, value):
            model.reportText3D(model.engine.setText3DColor(id, region: region, values: value.map { NSNumber(value: $0) }))
        case let .image(id, values):
            model.reportText3D(model.engine.setImage3D(id, values: values.map { NSNumber(value: $0) }))
        case let .content(id, content):
            if !content.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                model.reportText3D(model.engine.setText3D(forLayer: id, property: "content", stringValue: content, numberValue: 0))
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
            image3D = (model.engine.image3D(forLayer: layerId) ?? []).map(\.floatValue)
            if !typingActive && !editingText { draft = text3D["content"] as? String ?? "" }
        }
        let values = model.engine.environment().map(\.floatValue)
        environment = values.count >= 3 ? values : [0, 1, 0]
        shadows = model.engine.modelShadows(layerId).map(\.floatValue)
        interior = isCamera ? -1 : model.engine.modelInterior(layerId)
        objectEnvironment = model.engine.objectEnvironment(forLayer: layerId)
        loadMaterials()
    }
    private func loadMaterials() {
        let materials = model.engine.materials(forLayer: layerId).map(\.floatValue)
        importedMaterials = stride(from: 0, to: materials.count - materials.count % 8, by: 8).map { Array(materials[$0..<($0 + 8)]) }
    }
    private func importFont(_ url: URL, target: Int64?) {
        // 0.1.8: coleções (TTC/OTC) e WOFF também — o motor converte em TTF/OTF.
        guard ["ttf", "otf", "ttc", "otc", "woff"].contains(url.pathExtension.lowercased()) else {
            model.toast = AureaText.t("msg_use_uma_fonte_ttf_ou_otf"); return
        }
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        AureaPaths.ensureDirectories()
        let destination = AureaPaths.mediaDestination(for: url.lastPathComponent)
        do {
            try AureaPaths.copyImport(url, to: destination)
            guard let font = model.engine.importFont(atPath: destination.path) else {
                try? FileManager.default.removeItem(at: destination)
                model.toast = AureaText.t("msg_nao_deu_para_ler_essa_fonte"); return
            }
            if let target = target, model.primarySelection == target {
                model.reportText3D(model.engine.setText3D(forLayer: target, property: "fontPath",
                                           stringValue: "docs:Media/\((font["path"] as? String).map { URL(fileURLWithPath: $0).lastPathComponent } ?? destination.lastPathComponent)", numberValue: 0))
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
    var selected = false
    var onSelect: () -> Void = {}
    let onStart: () -> Void
    let onValue: (Float) -> Void
    let onEnd: () -> Void
    let onCommit: (Float) -> Void
    @State private var dragging = false
    @State private var live: Float = 0
    private var shown: Float { dragging ? live : value }
    private var resetVisible: Bool { abs(shown - reset) > 0.001 * max(1, abs(reset)) }
    // 13/11/2026 (pedido do dono: "os deslizantes tão muito compactos"): o rótulo fica EM CIMA
    // e a régua (52) ocupa a largura toda, como a linha empilhada do AUREA (par do Android).
    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            PropertyLabelChip(label, expression: .none, keyframe: .none, onTap: onSelect).aureaSelectedProperty(selected ? label : nil)
                .contextMenu { Button(AureaText.t("panel_voltar_padrao")) { onCommit(reset) }.disabled(!resetVisible) }
        HStack(spacing: 0) {
            TickRuler(value: { shown }, unitsPerDp: step, active: selected || dragging, height: ParamRowDims.labelH, verticalPadding: 4)
                .frame(maxWidth: .infinity).padding(.vertical, 4)
                .valueDrag(enabled: true, start: { value }, unitsPerDp: { step }, min: range.lowerBound, max: range.upperBound,
                           onStart: { live = value; dragging = true; onSelect(); onStart() }, onValue: { live = $0; onValue($0) },
                           onEnd: finish)
                .accessibilityElement().accessibilityLabel(label)
                .accessibilityValue(comUnidade(numeroPtBr(shown, casas: decimals), unit))
                .accessibilityAdjustableAction { direction in
                    let delta = step * 10 * (direction == .increment ? 1 : direction == .decrement ? -1 : 0)
                    guard delta != 0 else { return }
                    onSelect(); onCommit((value + delta).clamped(to: range))
                }
            Color.clear.frame(width: 6)
            ParamValueBox(comUnidade(numeroPtBr(shown, casas: decimals), unit), onTap: {
                guard !dragging else { return }
                onSelect()
                let project = model.projectGeneration, layer = model.primarySelection
                let composition = (model.composition[AureaCompositionId] as? NSNumber)?.uint64Value
                model.numericKeypad = KeypadRequest(title: label, value: value, unit: unit, min: range.lowerBound, max: range.upperBound,
                                                    decimals: decimals) {
                    guard model.projectGeneration == project, model.primarySelection == layer,
                          (model.composition[AureaCompositionId] as? NSNumber)?.uint64Value == composition else { return }
                    onCommit($0.clamped(to: range))
                }
            })
        }.frame(height: ParamRowDims.row)
        }.padding(.vertical, 3).environment(\.paramRowStacked, true).onDisappear(perform: finish)
    }
    private func finish() { if dragging { dragging = false; onEnd() } }
}

private enum Text3DChange {
    case number(Int64, String, Float)
    case color(Int64, UInt32, [Float])
    case content(Int64, String)
    case image(Int64, [Float])
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
        // VoiceOver: "selecionado" no chip aceso (par do `selected` do KitChip).
        .accessibilityAddTraits(on ? [.isSelected] : [])
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

    /// 13/11/2026: a régua do AUREA no lugar do Slider de sistema (par do `AnimSlider` do
    /// Android): rótulo em cima, régua de 52 em toda a largura e o valor; aplica ao soltar.
    private func slider(_ key: String, value: Binding<Float>, range: ClosedRange<Float>, shown: String) -> some View {
        let step = (range.upperBound - range.lowerBound) / 180
        return VStack(alignment: .leading, spacing: 2) {
            Text(AureaText.t(key)).font(.aurea(size: 12, weight: .semibold)).foregroundStyle(AureaColors.text)
            HStack(spacing: 6) {
                TickRuler(value: { value.wrappedValue }, unitsPerDp: step, active: true, height: ParamRowDims.labelH, verticalPadding: 4)
                    .frame(maxWidth: .infinity)
                    .valueDrag(enabled: true, start: { value.wrappedValue }, unitsPerDp: { step }, min: range.lowerBound, max: range.upperBound,
                               onStart: {}, onValue: { value.wrappedValue = $0 }, onEnd: { reapply() })
                ParamValueBox(shown, onTap: nil)
            }
        }.padding(.vertical, 3)
            .accessibilityElement(children: .ignore).accessibilityLabel(AureaText.t(key)).accessibilityValue(shown)
            .accessibilityAdjustableAction { direction in
                let delta = step * 10 * (direction == .increment ? 1 : direction == .decrement ? -1 : 0)
                guard delta != 0 else { return }
                value.wrappedValue = (value.wrappedValue + delta).clamped(to: range); reapply()
            }
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

// =============================================================================
// MINIATURAS DE MATERIAL — port de editor/panels/MaterialThumbs.kt. A bola de
// estúdio é do MOTOR (Engine::material_preview, scene3d/MaterialPreview.hpp):
// cor, metal, rugosidade e a imagem do mapa de cor. Pedida numa fila de um só,
// fora da main thread; o motor guarda as prontas por receita (pedir de novo
// depois de uma edição só recalcula o material que mudou) e aqui fica a ÚLTIMA
// de cada item, para a lista reabrir já com as bolas e não piscar.
// =============================================================================

/// Lado da bola em px (nítida em 30 pt até 3×).
let materialThumbSide: UInt32 = 96

@MainActor final class MaterialThumbStore: ObservableObject {
    static let shared = MaterialThumbStore()
    @Published private(set) var images: [String: UIImage] = [:]
    /// Revisão já pedida por item: a mesma revisão não pede de novo.
    private var asked: [String: AnyHashable] = [:]
    private let queue = DispatchQueue(label: "aurea.material-thumbs", qos: .utility)

    func image(_ key: String) -> UIImage? { images[key] }

    /// Pede a bola quando `revision` muda. `render` roda na fila (fora da main
    /// thread) e devolve RGBA8 de alfa reto, `materialThumbSide` de lado.
    func request(_ key: String, revision: AnyHashable, render: @escaping () -> Data?) {
        if asked[key] == revision { return }
        asked[key] = revision
        let side = Int(materialThumbSide)
        queue.async { [weak self] in
            guard let data = render(), let image = UIImage.fromRGBA(data, width: side, height: side) else { return }
            DispatchQueue.main.async {
                guard let self else { return }
                if self.images.count > 160 { self.images.removeAll() }
                self.images[key] = image
            }
        }
    }

    /// Aviso de memória: solta as bolas (refeitas sob demanda).
    func trimMemory() {
        images.removeAll()
        asked.removeAll()
    }
}

/// A bola desenhada: a miniatura do motor ou, enquanto ela não chega, a
/// bolinha da cor do material. Decorativa para o VoiceOver — o nome ao lado
/// já diz o que é.
struct MaterialBallView: View {
    let image: UIImage?
    let fallback: Color
    let size: CGFloat
    var body: some View {
        Group {
            if let image {
                Image(uiImage: image).resizable().interpolation(.high)
            } else {
                Circle().fill(fallback).padding(size * 0.08)
            }
        }
        .frame(width: size, height: size)
        .accessibilityHidden(true)
    }
}

/// 0.4.6: a NÉVOA da cena (volumétrica, com raios de luz) — no Render 3D e na aba
/// Névoa da câmera: é a névoa que a câmera vê, um ajuste só. Par do Android
/// (`SceneFogSection`, Scene3DEnvironment.kt).
struct SceneFogControls: View {
    @EnvironmentObject private var model: AureaModel
    @State private var settings: [Float] = []
    private func load() { settings = model.engine.sceneSettings().map(\.floatValue) }
    private func set(_ index: UInt32, _ value: Float) {
        _ = model.engine.setSceneSetting(index, value: value)
        load(); model.refreshModel(force: true)
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            if settings.count >= 15 {
                Text(AureaText.t("scene046_fog")).font(.aurea(size: 13, weight: .semibold)).foregroundStyle(AureaColors.muted)
                    .accessibilityAddTraits(.isHeader)
                Text(AureaText.t("scene046_fog_hint")).font(.aurea(size: 11)).foregroundStyle(AureaColors.subtle)
                    .fixedSize(horizontal: false, vertical: true)
                HStack {
                    Text(AureaText.t("scene046_fog")).font(.aurea(size: 13)).frame(maxWidth: .infinity, alignment: .leading)
                    AureaToggle(checked: settings[9] > 0.5) { on in set(9, on ? 1 : 0) }
                }.frame(height: 44).accessibilityIdentifier("env3d.fog.switch")
                if settings[9] > 0.5 {
                    // Densidade em "por 10 mil px": 6 = névoa leve, 30 = cerração.
                    NativePanelRuler(label: AureaText.t("scene046_fog_density"), value: settings[10] * 10000, step: 0.5, range: 0...200,
                                     decimals: 1, keypad: true) { set(10, $0 / 10000) }
                    NativePanelRuler(label: AureaText.t("scene046_fog_rays"), value: settings[11] * 100, step: 5, range: 0...800,
                                     unit: "%", keypad: true) { set(11, $0 / 100) }
                }
            }
        }
        .accessibilityIdentifier("env3d.fog")
        .onAppear { load() }.onChange(of: model.status.modelRevision) { _ in load() }
    }
}

// =============================================================================
// PAINEL DO MODELO 3D (13/11/2026, conceito aprovado pelo dono; par do Model3DPanel.kt
// e do Parts3DEditor.kt do Android). Um modelo 3D é UMA camada; o painel tem poucas
// telas, um assunto por tela e controles grandes:
//   Material — bolas dos visuais do modelo inteiro (Original, Plástico, Fosco, Metal,
//              Cromo, Ouro, Vidro) e Cor · Metal · Rugosidade · Opacidade com ◆;
//              "Ajustar" = o modelo inteiro (padrão) ou um material.
//   Textura  — Aplicar em: modelo inteiro / uma parte › (textura e Esconder da parte).
//   Animação — clipe do arquivo, EVOLUÇÃO (0–100 %, com ◆), Repetir e Velocidade.
//   Sombra   — projeta/recebe sombra e o interior (no Panel3DView).
// A aba Mover por parte SAIU (o palco e a timeline já movem o modelo). A regra mora no
// motor (EngineModelPanel.cpp).
// =============================================================================

extension AureaModel {
    /// A camada escolhida é um MODELO IMPORTADO (não forma, texto ou imagem 3D).
    var importedModelSelected: Bool {
        guard let id = primarySelection, selectedLayer?.kind == 10 else { return false }
        return engine.shape3D(id).isEmpty && engine.text3D(forLayer: id) == nil && engine.image3D(forLayer: id) == nil
    }
}

/// Bits dos canais do material no motor: R, G, B, opacidade, metal, rugosidade; v76: brilho R, G, B
/// (6..8) e a força do brilho (9).
private let model3DColorMask: UInt32 = 7
private let model3DGlowMask: UInt32 = 0x1C0
/// Chave de um estúdio pronto no lugar do HDRI do objeto (motor: scene3d::studio_environment_key).
private let objectStudioKey: UInt64 = 0x5354_5544_0000_0000

/// 14/11/2026 (pedido do dono: "não é só tocar e marcar no normal?"): sem ◆ em cada linha.
/// Toca-se na linha (ou se mexe nela) para escolhê-la; o ◆ da coluna da esquerda marca nela.
@MainActor
final class Model3DKeyTarget: ObservableObject {
    static let shared = Model3DKeyTarget()
    @Published var tag: String? = nil
    @Published var look: KeyframeLook = .none
    var toggle: (() -> Void)? = nil
    func clear() { tag = nil; look = .none; toggle = nil }
    func claim(_ tag: String, look: KeyframeLook, toggle: @escaping () -> Void) {
        self.tag = tag; self.look = look; self.toggle = toggle
    }
    /// Mantém o ◆ do trilho em dia com a linha escolhida (o estado muda com o cabeçote).
    func sync(_ tag: String, look: KeyframeLook, toggle: @escaping () -> Void) {
        guard self.tag == tag else { return }
        if self.look != look { self.look = look }
        self.toggle = toggle
    }
}

@MainActor
struct Model3DMaterialView: View {
    let layerId: Int64
    /// A linha de textura de UM material (`Panel3DView.materialTextureRow`).
    let textureRow: (UInt32) -> AnyView
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var thumbs = MaterialThumbStore.shared
    @State private var materials: [[Float]] = []
    /// v76: o BRILHO (emissão) do alvo no cabeçote — {R, G, B lineares, força}.
    @State private var glow: [Float] = []
    @State private var look: Int32 = -1
    /// −1 = o modelo inteiro; senão o material do arquivo.
    @State private var target = -1
    private static let lookKeys = ["m3d_look_original", "m3d_look_plastic", "m3d_look_matte", "m3d_look_metal",
                                   "m3d_look_chrome", "m3d_look_gold", "m3d_look_glass", "m3d_look_neon"]
    private static let swatches: [[Float]] = [[0.73, 0.74, 0.78, 1], [0.89, 0.89, 0.9, 1], [0.61, 0.61, 0.62, 1], [0.56, 0.58, 0.62, 1],
                                              [0.95, 0.96, 0.98, 1], [1, 0.77, 0.34, 1], [0.78, 0.92, 1, 0.6], [0.08, 0.85, 1, 1]]
    private var engineTarget: UInt32 { target < 0 ? UInt32.max : UInt32(target) }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            // POSIÇÃO (v76): assentar no chão, centralizar e ajustar à tela — um passo de desfazer cada.
            title("m3d_place")
            HStack(spacing: 8) {
                placeTile(CupertinoGlyph.ArrowDownToLine, "m3d_place_ground", id: "panel3d.place.ground", mode: 0)
                placeTile(CupertinoGlyph.SmallcircleCircle, "m3d_place_center", id: "panel3d.place.center", mode: 1)
                placeTile(CupertinoGlyph.Fullscreen, "m3d_place_fit", id: "panel3d.place.fit", mode: 2)
            }
            .padding(.vertical, 6)
            .accessibilityElement(children: .contain)
            note("m3d_place_hint")
            Color.clear.frame(height: 8)
            if materials.isEmpty {
                note("m3d_no_materials").padding(.top, 10)
            } else {
                title("m3d_look")
                note(look < 0 ? "m3d_look_custom" : "m3d_look_hint")
                AureaFlowLayout(hGap: 8, vGap: 10) {
                    ForEach(0..<Self.lookKeys.count, id: \.self) { i in lookBall(i) }
                }.padding(.vertical, 10)
                title("m3d_adjust")
                if materials.count > 1 {
                    ScrollView(.horizontal, showsIndicators: false) {
                        HStack(spacing: 6) {
                            NativePanelChip(AureaText.t("m3d_whole_model"), selected: target < 0, horizontal: 12, height: 36, radius: 9, fontSize: 12.5) { target = -1 }
                                .frame(minHeight: 44).accessibilityIdentifier("panel3d.adjust.all")
                            ForEach(materials.indices, id: \.self) { row in
                                let id = Int(materials[row][0])
                                NativePanelChip(AureaText.t("m3d_material_n", id + 1), selected: target == id, horizontal: 12, height: 36, radius: 9, fontSize: 12.5) { target = id }
                                    .frame(minHeight: 44).accessibilityIdentifier("panel3d.adjust.\(id)")
                            }
                        }
                    }
                }
                let values = materials.first(where: { Int($0[0]) == target }) ?? materials[0]
                colorRow("panel_cor", linear: [values[2], values[3], values[4]], firstParam: 0, mask: model3DColorMask,
                         tag: "panel3d.mat.color", gesture: "cor do modelo 3D")
                slider("pn_t3d_metallic", values[6], param: 4, id: "panel3d.mat.metal")
                slider("pn_t3d_roughness", values[7], param: 5, id: "panel3d.mat.rough")
                slider("panel_opacidade", values[5], param: 3, id: "panel3d.mat.opacity")
                // BRILHO (v76): a luz própria do material (emissão) — cor e intensidade, cada uma com ◆.
                if glow.count >= 4 {
                    title("m3d_glow").padding(.top, 6)
                    colorRow("m3d_glow_color", linear: [glow[0], glow[1], glow[2]], firstParam: 6, mask: model3DGlowMask,
                             tag: "panel3d.mat.glow.color", gesture: "brilho do modelo 3D")
                    slider("m3d_glow_strength", glow[3], param: 9, id: "panel3d.mat.glow.strength", maxPct: 2000)
                    note("m3d_glow_hint")
                }
                if target >= 0 {
                    note("m3d_texture_material_hint").padding(.top, 4)
                    // A textura DESTE material (arquivo ou camada da timeline): a linha do Panel3DView,
                    // que tem o seletor de imagem e a folha de camadas.
                    textureRow(UInt32(target))
                }
            }
        }
        .padding(.horizontal, 12)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("panel3d.modelMaterial")
        .onAppear(perform: load)
        .onChange(of: model.status.modelRevision) { _ in load() }
        .onChange(of: model.localPlayhead) { _ in load() }
        .onChange(of: layerId) { _ in target = -1; load() }
        .onChange(of: target) { _ in load() }
    }

    private func load() {
        let flat = model.engine.materials(forLayer: layerId).map(\.floatValue)
        materials = stride(from: 0, to: flat.count - flat.count % 8, by: 8).map { Array(flat[$0..<($0 + 8)]) }
        look = model.engine.modelMaterialPreset(layerId)
        if target >= 0 && !materials.contains(where: { Int($0[0]) == target }) { target = -1 }
        glow = model.engine.materialEmission(layerId, material: engineTarget).map(\.floatValue)
    }
    /// Um botão grande da Posição (ícone em cima, nome embaixo); um passo de desfazer.
    private func placeTile(_ glyph: Character, _ key: String, id: String, mode: UInt32) -> some View {
        let locked = model.selectedLayer?.locked == true
        return Button {
            guard !locked else { return }
            model.commitPendingCommands()
            if model.engine.placeModelLayer(layerId, mode: mode) { refresh() }
        } label: {
            VStack(spacing: 6) {
                CupertinoGlyph.text(glyph, size: 22, color: locked ? AureaColors.muted : AureaColors.text)
                Text(AureaText.t(key)).font(.aurea(size: 12.5, weight: .semibold))
                    .foregroundStyle(locked ? AureaColors.muted : AureaColors.text)
                    .multilineTextAlignment(.center).lineLimit(2)
            }
            .padding(.horizontal, 6)
            .frame(maxWidth: .infinity, minHeight: 68)
            .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 12))
            .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle())
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(AureaText.t(key))
        .accessibilityAddTraits(.isButton)
        .accessibilityIdentifier(id)
    }
    private func refresh() { model.refreshModel(force: true); load() }
    private func title(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted)
            .padding(.top, 8).padding(.bottom, 4).accessibilityAddTraits(.isHeader)
    }
    private func note(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 12)).lineSpacing(2).foregroundStyle(AureaColors.muted)
            .fixedSize(horizontal: false, vertical: true)
    }
    private func lookThumb(_ i: Int) -> UIImage? {
        let key = "l:\(layerId):\(i)"
        let engine = model.engine, target = layerId
        thumbs.request(key, revision: 0) { engine.modelMaterialPresetPreview(target, preset: UInt32(i), size: materialThumbSide) }
        return thumbs.image(key)
    }
    /// Uma bola grande (64 pt) com o nome embaixo; a escolhida tem o anel de destaque.
    private func lookBall(_ i: Int) -> some View {
        let on = look == Int32(i)
        return Button {
            guard !on, model.selectedLayer?.locked != true else { return }
            if model.engine.applyModelMaterialPreset(layerId, preset: UInt32(i)) { refresh() }
        } label: {
            VStack(spacing: 5) {
                MaterialBallView(image: lookThumb(i), fallback: AureaColorSpace.color(Self.swatches[i]), size: 56)
                    .padding(4)
                    .overlay(Circle().stroke(on ? AureaColors.accent : AureaColors.border, lineWidth: on ? 2.5 : 1))
                    .frame(width: 64, height: 64)
                Text(AureaText.t(Self.lookKeys[i])).font(.aurea(size: 12.5, weight: on ? .bold : .medium))
                    .foregroundStyle(on ? AureaColors.accent : AureaColors.text).lineLimit(1)
            }.frame(width: 70).contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle())
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(AureaText.t(Self.lookKeys[i]))
        .accessibilityAddTraits(on ? [.isButton, .isSelected] : [.isButton])
        .accessibilityIdentifier("panel3d.look.\(i)")
    }
    private func lookOf(_ mask: UInt32) -> KeyframeLook {
        let targets: Set<UInt32> = target < 0 ? Set(materials.map { UInt32($0[0]) }) : [UInt32(target)]
        let keys = (model.keyframes[layerId] ?? []).filter {
            $0.property == 37 && targets.contains($0.effectIndex) && (mask >> $0.paramIndex) & 1 == 1
        }
        return keys.contains { $0.time == model.localPlayhead } ? .keyHere : keys.isEmpty ? .none : .animated
    }
    private func toggleKeys(_ mask: UInt32) {
        guard model.selectedLayer?.locked != true else { return }
        model.commitPendingCommands()
        if model.engine.toggleMaterialKeys(layerId, material: engineTarget, paramMask: mask) { refresh() }
    }
    private func write(_ param: UInt32, _ value: Float) {
        guard value.isFinite else { return }
        // v76: a força do brilho (9) passa de 1 (até kMaterialEmissionMax = 20).
        _ = model.engine.setMaterial(forLayer: layerId, index: engineTarget, param: param,
                                     value: value.clamped(to: param == 9 ? 0...20 : 0...1))
        refresh()
    }
    /// Uma cor do material (a do modelo, ou a do BRILHO na v76): amostra grande; tocar escolhe a
    /// linha (o ◆ da esquerda marca R, G e B juntos — `mask`). `linear` = os três canais a partir de `firstParam`.
    private func colorRow(_ key: String, linear: [Float], firstParam: UInt32, mask: UInt32, tag: String, gesture: String) -> some View {
        let display = AureaColorSpace.engineToDisplay([linear[0], linear[1], linear[2], 1])
        let look = lookOf(mask)
        let keys = Model3DKeyTarget.shared
        let toggle = { toggleKeys(mask) }
        DispatchQueue.main.async { keys.sync(tag, look: look, toggle: toggle) }
        return HStack(spacing: 4) {
            Text(AureaText.t(key)).font(.aurea(size: 14, weight: .semibold))
                .foregroundStyle(look == .none ? AureaColors.text : AureaColors.accent)
                .frame(maxWidth: .infinity, alignment: .leading)
                .contentShape(Rectangle())
                .onTapGesture { keys.claim(tag, look: look, toggle: toggle) }
            Button {
                guard model.selectedLayer?.locked != true else { return }
                keys.claim(tag, look: look, toggle: toggle)
                model.beginGesture(gesture)
                let target = layerId, index = engineTarget
                model.colorSheet = ColorSheetRequest(title: AureaText.t(key), initial: display, withAlpha: false, onChange: { r, g, b, _ in
                    guard model.primarySelection == target else { return }
                    let next = AureaColorSpace.displayToEngine(r, g, b, 1)
                    for c in 0..<3 { _ = model.engine.setMaterial(forLayer: target, index: index, param: firstParam + UInt32(c), value: next[c]) }
                    refresh()
                }, onDone: { model.endGesture() })
            } label: {
                AureaColorSwatch(color: AureaColorSpace.color(display)).frame(width: 44, height: 44).clipShape(RoundedRectangle(cornerRadius: 8))
            }
            .buttonStyle(AureaPressStyle()).accessibilityLabel(AureaText.t(key)).accessibilityIdentifier("\(tag).picker")
        }
        .frame(minHeight: 60)
        .padding(.horizontal, 6)
        .background(RoundedRectangle(cornerRadius: 10).fill(keys.tag == tag ? AureaColors.accent.opacity(0.12) : Color.clear))
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier(tag)
    }
    /// Régua grande de um canal (0–100 %; a força do brilho vai a `maxPct`), com o ◆ dela.
    private func slider(_ key: String, _ value01: Float, param: UInt32, id: String, maxPct: Float = 100) -> some View {
        let look = lookOf(1 << param)
        let keys = Model3DKeyTarget.shared
        let toggle = { toggleKeys(1 << param) }
        DispatchQueue.main.async { keys.sync(id, look: look, toggle: toggle) }
        return NativePanelRuler(label: AureaText.t(key), value: value01 * 100, step: maxPct > 100 ? 5 : 0.5, range: 0...maxPct, unit: "%", decimals: 0, keypad: true,
                                look: look, selected: keys.tag == id, onSelect: { keys.claim(id, look: look, toggle: toggle) },
                                identifier: id) { keys.claim(id, look: look, toggle: toggle); write(param, $0 / 100) }
    }
}

@MainActor
struct Model3DTextureView: View {
    let layerId: Int64
    @EnvironmentObject private var model: AureaModel
    @State private var onePart = false
    var body: some View {
        let names = model.engine.modelPartNames(layerId)
        VStack(alignment: .leading, spacing: 0) {
            if names.isEmpty {
                Text(AureaText.t("m3d_texture_no_parts")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                    .fixedSize(horizontal: false, vertical: true).padding(.top, 10)
            } else {
                Text(AureaText.t("m3d_apply_to")).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted)
                    .padding(.top, 8).padding(.bottom, 4).accessibilityAddTraits(.isHeader)
                HStack(spacing: 8) {
                    NativePanelChip(AureaText.t("m3d_whole_model"), selected: !onePart, horizontal: 12, height: 36, radius: 9, fontSize: 12.5) { onePart = false }
                        .frame(minHeight: 44).accessibilityIdentifier("panel3d.texture.whole")
                    NativePanelChip(AureaText.t("m3d_one_part") + " ›", selected: onePart, horizontal: 12, height: 36, radius: 9, fontSize: 12.5) { onePart = true }
                        .frame(minHeight: 44).accessibilityIdentifier("panel3d.texture.part")
                }
                .accessibilityElement(children: .contain)
                Color.clear.frame(height: 6)
                Parts3DEditor(layerId: layerId, names: names, wholeModel: !onePart)
            }
        }
        .padding(.horizontal, 12)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("panel3d.modelTexture")
        .onChange(of: layerId) { _ in onePart = false }
    }
}

@MainActor
struct Model3DAnimationView: View {
    let layerId: Int64
    @EnvironmentObject private var model: AureaModel
    @State private var clips: [String] = []
    @State private var anim: [Float] = []
    private var locked: Bool { model.selectedLayer?.locked == true }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            title("ae_model_clip")
            if clips.isEmpty {
                note("ae_model_clip_empty")
            } else if anim.count >= 8 {
                let clip = Int(anim[0])
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 6) {
                        clipChip(AureaText.t("ae_model_clip_none"), on: clip < 0, id: "model3d.clip.none") { setClip(-1) }
                        ForEach(clips.indices, id: \.self) { i in
                            clipChip(clips[i].isEmpty ? AureaText.t("ae_model_clip_n", i + 1) : clips[i], on: clip == i, id: "model3d.clip.\(i)") { setClip(Int32(i)) }
                        }
                    }
                }
                if clip < 0 {
                    note("m3d_anim_pick_clip").padding(.top, 6)
                } else {
                    note2(AureaText.t("m3d_anim_duration", numeroPtBr(anim[1], casas: 2)))
                    Color.clear.frame(height: 10)
                    // EVOLUÇÃO: onde o clipe está (0–100 %), com ◆ — a animação do arquivo por keyframes.
                    let keyed = anim[6] > 0.5
                    let look: KeyframeLook = anim[7] > 0.5 ? .keyHere : keyed ? .animated : .none
                    let keys = Model3DKeyTarget.shared
                    // A Evolução vem escolhida (v76: a Velocidade também tem ◆; tocar nela a escolhe).
                    let _ = DispatchQueue.main.async {
                        if keys.tag?.hasPrefix("panel3d.anim.") != true { keys.claim("panel3d.anim.progress", look: look, toggle: { toggleKey() }) }
                        else { keys.sync("panel3d.anim.progress", look: look, toggle: { toggleKey() }) }
                    }
                    NativePanelRuler(label: AureaText.t("m3d_anim_progress"), value: anim[3] * 100, step: 0.25, range: 0...100, unit: "%", decimals: 1,
                                     keypad: true, look: look, selected: keys.tag == "panel3d.anim.progress",
                                     onSelect: { keys.claim("panel3d.anim.progress", look: look, toggle: { toggleKey() }) },
                                     identifier: "panel3d.anim.progress") {
                        keys.claim("panel3d.anim.progress", look: look, toggle: { toggleKey() }); set(0, $0 / 100)
                    }
                    note("m3d_anim_progress_hint")
                    Color.clear.frame(height: 10)
                    if anim[2] >= 0 || keyed {
                        PanelItemRow(glyph: CupertinoGlyph.PlayFill, title: AureaText.t("m3d_anim_auto"), tag: "panel3d.anim.auto") { set(0, -1) }
                        note("m3d_anim_auto_hint")
                    } else {
                        // Tocando sozinho: Repetir, Velocidade (com ◆), Começar em, De trás para frente e
                        // Ida e volta (com a Evolução marcada, quem manda é ela).
                        NativePanelToggle(AureaText.t("m3d_anim_loop"), checked: anim[4] > 0.5) { set(1, $0 ? 1 : 0) }
                            .accessibilityIdentifier("panel3d.anim.loop")
                        playingAloneExtras(keys)
                    }
                }
            }
        }
        .padding(.horizontal, 12)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("panel3d.modelAnim")
        .onAppear(perform: load)
        .onChange(of: model.status.modelRevision) { _ in load() }
        .onChange(of: model.localPlayhead) { _ in load() }
        .onChange(of: layerId) { _ in load() }
    }

    /// Velocidade com ◆ e, na v76, Começar em (segundos do clipe), De trás para frente e Ida e volta.
    @ViewBuilder private func playingAloneExtras(_ keys: Model3DKeyTarget) -> some View {
        let extras = anim.count >= 14
        let speedTag = "panel3d.anim.speed"
        let speedLook: KeyframeLook = extras && anim[12] > 0.5 ? .keyHere : extras && anim[11] > 0.5 ? .animated : .none
        let _ = DispatchQueue.main.async { keys.sync(speedTag, look: speedLook, toggle: { toggleSpeedKey() }) }
        NativePanelRuler(label: AureaText.t("m3d_anim_speed"), value: (extras ? anim[13] : anim[5]) * 100, step: 1, range: 5...400, unit: "%", decimals: 0,
                         reset: 100, keypad: true, look: speedLook, selected: keys.tag == speedTag,
                         onSelect: { keys.claim(speedTag, look: speedLook, toggle: { toggleSpeedKey() }) },
                         identifier: speedTag) {
            keys.claim(speedTag, look: speedLook, toggle: { toggleSpeedKey() }); set(2, $0 / 100)
        }
        note("m3d_anim_speed_hint")
        if extras {
            let duration = max(anim[1], 0.01)
            NativePanelRuler(label: AureaText.t("m3d_anim_start"), value: anim[8] * duration, step: 0.01, range: 0...duration, unit: "s",
                             decimals: 2, reset: 0, keypad: true, identifier: "panel3d.anim.start") { set(3, $0 / duration) }
            NativePanelToggle(AureaText.t("m3d_anim_reverse"), checked: anim[9] > 0.5) { set(4, $0 ? 1 : 0) }
                .accessibilityIdentifier("panel3d.anim.reverse")
            NativePanelToggle(AureaText.t("m3d_anim_pingpong"), checked: anim[10] > 0.5) { set(5, $0 ? 1 : 0) }
                .accessibilityIdentifier("panel3d.anim.pingpong")
        }
    }

    private func load() {
        clips = model.engine.modelAnimationClips(layerId)
        anim = model.engine.modelAnimation(layerId).map(\.floatValue)
    }
    private func toggleSpeedKey() {
        guard !locked else { return }
        model.commitPendingCommands()
        if model.engine.toggleModelAnimationParamKey(layerId, param: 2) { refresh() }
    }
    private func refresh() { model.refreshModel(force: true); load() }
    private func set(_ param: UInt32, _ value: Float) {
        guard !locked, value.isFinite else { return }
        if model.engine.setModelAnimationParam(layerId, param: param, value: value) { refresh() }
    }
    private func toggleKey() {
        guard !locked else { return }
        model.commitPendingCommands()
        if model.engine.toggleModelAnimationKey(layerId) { refresh() }
    }
    private func setClip(_ clip: Int32) {
        guard !locked else { return }
        model.commitPendingCommands()
        if model.engine.setModelAnimationClip(layerId, clip: clip) { refresh() }
    }
    private func clipChip(_ label: String, on: Bool, id: String, action: @escaping () -> Void) -> some View {
        NativePanelChip(label, selected: on, horizontal: 12, height: 36, radius: 9, fontSize: 12.5, action: action)
            .frame(minHeight: 44).accessibilityAddTraits(on ? [.isSelected] : []).accessibilityIdentifier(id)
    }
    private func title(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted)
            .padding(.top, 8).padding(.bottom, 4).accessibilityAddTraits(.isHeader)
    }
    private func note(_ key: String) -> some View { note2(AureaText.t(key)) }
    private func note2(_ text: String) -> some View {
        Text(text).font(.aurea(size: 12)).lineSpacing(2).foregroundStyle(AureaColors.muted).fixedSize(horizontal: false, vertical: true)
    }
}

// =============================================================================
// TEXTURA POR PARTE (par do Parts3DEditor.kt): com `wholeModel`, a imagem do modelo
// inteiro (Trocar textura 3D com "todas as partes"), Repetir e Voltar à textura
// original; senão a lista de partes → parte aberta com a textura DELA (vale por cima
// da do modelo inteiro) e Esconder esta parte. Por baixo, os mesmos efeitos do motor
// (`aurea.3d.part_texture`, `part_hide`): o 1º ajuste cria o efeito.
// =============================================================================

@MainActor
struct Parts3DEditor: View {
    let layerId: Int64
    let names: [String]
    var wholeModel = false
    @EnvironmentObject private var model: AureaModel
    @State private var open: Int?
    @State private var picking = false
    private static let textureKey = "aurea.3d.part_texture"
    private static let hideKey = "aurea.3d.part_hide"

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            if wholeModel { wholeBody }
            else if let part = open, names.indices.contains(part) { openBody(part) } else { listBody }
        }
        .onChange(of: layerId) { _ in open = nil }
        .sheet(isPresented: $picking) {
            ShellMediaPicker(selectionLimit: 1, filter: .images) { items in
                picking = false
                guard let (url, _) = items.first else { return }
                let fx = wholeModel ? ensureWhole() : open.flatMap { ensure(Self.textureKey, part: $0) }
                guard let fx else { return }
                model.setEffectImage(layerId, effect: fx, url: url)
            }
        }
    }

    // ---- Leitura dos efeitos ----------------------------------------------------------
    private func params(_ effect: UInt32) -> [[String: Any]] { model.engine.effectParamsForLayer(layerId, effectId: effect) }
    private func number(_ p: [String: Any]?, _ key: String, _ fallback: Float) -> Float { (p?[key] as? NSNumber)?.floatValue ?? fallback }
    private func value(_ p: [String: Any]?, _ fallback: Float) -> Float {
        ((p?[AureaParamValue] as? [NSNumber])?.first?.floatValue) ?? fallback
    }
    /// "Todas as partes" (parâmetro 2) ligado: é a textura do modelo inteiro.
    private func coversAll(_ e: EffectItem) -> Bool {
        let ps = params(e.effectId)
        return value(ps.count > 2 ? ps[2] : nil, 0) >= 0.5
    }
    private func effect(_ key: String, part: Int) -> EffectItem? {
        let type = fxEffectTypeId(key)
        return model.effects.first { e in
            guard e.typeId == type else { return false }
            let ps = params(e.effectId)
            let first = Int(value(ps.first, 0).rounded())
            if key == Self.hideKey { return first == part + 1 && Int(value(ps.count > 1 ? ps[1] : nil, 0).rounded()) == part + 1 }
            return first == part + 1 && !coversAll(e)
        }
    }
    private func ensure(_ key: String, part: Int) -> UInt32? {
        if let fx = effect(key, part: part) { return fx.effectId }
        guard model.addPartEffect(key, part: part) else { return nil }
        return effect(key, part: part)?.effectId
    }
    private var whole: EffectItem? {
        let type = fxEffectTypeId(Self.textureKey)
        return model.effects.first { $0.typeId == type && coversAll($0) }
    }
    /// Cria a textura do modelo inteiro ANTES das de parte (no motor, a de baixo vence):
    /// a imagem de uma parte continua valendo por cima.
    private func ensureWhole() -> UInt32? {
        if let fx = whole { return fx.effectId }
        let type = fxEffectTypeId(Self.textureKey)
        let before = Set(model.effects.map(\.effectId))
        guard model.addPartEffect(Self.textureKey, part: 0),
              let fx = model.effects.first(where: { $0.typeId == type && !before.contains($0.effectId) }) else { return nil }
        model.engine.setEffect(fx.effectId, forLayer: layerId, paramIndex: 2, value: 1)
        if let first = model.effects.firstIndex(where: { $0.typeId == type }),
           let at = model.effects.firstIndex(where: { $0.effectId == fx.effectId }), at > first {
            model.engine.moveEffect(fx.effectId, inLayer: layerId, toIndex: UInt32(first))
        }
        model.commitPendingCommands(); model.refreshModel(force: true)
        return fx.effectId
    }
    /// Animado = keyframe no cabeçote; senão o valor parado (o contrato do EffectsView).
    private func write(_ fx: UInt32, index: UInt32, _ v: Float) {
        guard v.isFinite else { return }
        let ps = params(fx)
        let p = ps.indices.contains(Int(index)) ? ps[Int(index)] : nil
        if (p?[AureaParamAnimated] as? NSNumber)?.boolValue == true {
            model.engine.keyParameter(layerId, property: 31, effect: fx, param: index * 4, time: model.localPlayhead, value: v)
        } else {
            model.engine.setEffect(fx, forLayer: layerId, paramIndex: index, value: v)
        }
        model.commitPendingCommands(); model.refreshModel(force: true)
    }

    // ---- Modelo inteiro --------------------------------------------------------------
    @ViewBuilder private var wholeBody: some View {
        Text(AureaText.t("m3d_texture_hint")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
            .fixedSize(horizontal: false, vertical: true).padding(.bottom, 6)
        textureRows(whole, tag: "panel3d.texture.whole")
    }

    /// As linhas comuns: escolher a imagem, Repetir e voltar à textura do arquivo.
    @ViewBuilder private func textureRows(_ fx: EffectItem?, tag: String) -> some View {
        let filename = fx.map { model.engine.effectImageName($0.effectId, forLayer: layerId) } ?? ""
        PanelItemRow(glyph: CupertinoGlyph.Photo, title: AureaText.t(filename.isEmpty ? "m3d_texture_pick" : "shape3d_image_change"), tag: "\(tag).pick",
                     detail: filename.isEmpty ? AureaText.t("fn_part_texture_none") : filename, chevron: true) { picking = true }
        if let fx {
            let ps = params(fx.effectId)
            // 14/11/2026 (v76): Repetir, Deslocar X/Y e Girar com ◆ (toca na linha, marca no ◆ da
            // esquerda); ROLAR X/Y faz a textura andar sozinha com o tempo (esteira, água, tela).
            textureRuler(fx.effectId, ps, index: 3, key: "fxo_repeat", unit: "%", step: 0.5, decimals: 0, reset: 100,
                         id: "\(tag).repeat", keyable: true)
            if ps.count > 8 {
                sectionTitle("m3d_tex_transform")
                textureRuler(fx.effectId, ps, index: 4, key: "m3d_tex_offset_x", unit: "%", step: 0.5, decimals: 1, reset: 0,
                             id: "\(tag).offsetX", keyable: true)
                textureRuler(fx.effectId, ps, index: 5, key: "m3d_tex_offset_y", unit: "%", step: 0.5, decimals: 1, reset: 0,
                             id: "\(tag).offsetY", keyable: true)
                textureRuler(fx.effectId, ps, index: 6, key: "m3d_tex_rotation", unit: "°", step: 1, decimals: 0, reset: 0,
                             id: "\(tag).rotation", keyable: true)
                sectionTitle("m3d_tex_scroll")
                textureRuler(fx.effectId, ps, index: 7, key: "m3d_tex_scroll_x", unit: "%/s", step: 1, decimals: 0, reset: 0,
                             id: "\(tag).scrollX", keyable: false)
                textureRuler(fx.effectId, ps, index: 8, key: "m3d_tex_scroll_y", unit: "%/s", step: 1, decimals: 0, reset: 0,
                             id: "\(tag).scrollY", keyable: false)
                Text(AureaText.t("m3d_tex_scroll_hint")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                    .fixedSize(horizontal: false, vertical: true)
            }
            PanelItemRow(glyph: CupertinoGlyph.ArrowCounterclockwise, title: AureaText.t("m3d_texture_original"), tag: "\(tag).remove") {
                model.engine.removeEffect(fx.effectId, fromLayer: layerId); model.refreshModel(force: true)
            }
        }
    }

    private func sectionTitle(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted)
            .padding(.top, 8).padding(.bottom, 4).accessibilityAddTraits(.isHeader)
    }
    /// Uma régua da textura (parâmetro `index` do efeito). `keyable`: tocar/arrastar escolhe a
    /// linha e o ◆ da coluna da esquerda marca o keyframe dela (Model3DKeyTarget, como no Material).
    @ViewBuilder private func textureRuler(_ fx: UInt32, _ ps: [[String: Any]], index: Int, key: String, unit: String, step: Float,
                                           decimals: Int, reset: Float, id: String, keyable: Bool) -> some View {
        if ps.indices.contains(index) {
            let p = ps[index]
            let keys = Model3DKeyTarget.shared
            let look: KeyframeLook = keyable ? effectLook(fx, UInt32(index)) : .none
            let toggle = { toggleEffectKey(fx, UInt32(index)) }
            let _ = { if keyable { DispatchQueue.main.async { keys.sync(id, look: look, toggle: toggle) } } }()
            NativePanelRuler(label: AureaText.t(key), value: value(p, reset), step: step,
                             range: number(p, AureaParamMin, -100)...number(p, AureaParamMax, 100), unit: unit, decimals: decimals,
                             reset: reset, keypad: true, look: look, selected: keyable && keys.tag == id,
                             onSelect: keyable ? ({ keys.claim(id, look: look, toggle: toggle) } as () -> Void) : nil,
                             identifier: id) {
                if keyable { keys.claim(id, look: look, toggle: toggle) }
                write(fx, index: UInt32(index), $0)
            }
        }
    }
    private func effectLook(_ fx: UInt32, _ index: UInt32) -> KeyframeLook {
        let keys = (model.keyframes[layerId] ?? []).filter { $0.property == 31 && $0.effectIndex == fx && $0.paramIndex / 4 == index }
        return keys.contains { $0.time == model.localPlayhead } ? .keyHere : keys.isEmpty ? .none : .animated
    }
    /// ◆ do parâmetro no cabeçote (o mesmo caminho da lista de efeitos), num passo de desfazer.
    private func toggleEffectKey(_ fx: UInt32, _ index: UInt32) {
        guard model.selectedLayer?.locked != true else { return }
        let time = model.localPlayhead
        let here = (model.keyframes[layerId] ?? []).filter {
            $0.property == 31 && $0.effectIndex == fx && $0.paramIndex / 4 == index && $0.time == time
        }
        model.beginGesture("keyframe da textura 3D")
        if here.isEmpty {
            let ps = params(fx)
            let v = value(ps.indices.contains(Int(index)) ? ps[Int(index)] : nil, 0)
            model.engine.keyParameter(layerId, property: 31, effect: fx, param: index * 4, time: time, value: v)
        } else {
            for key in here {
                model.engine.editTrackKey(layerId, property: key.property, effect: key.effectIndex, param: key.paramIndex, time: key.time,
                                          action: 1, value: key.value, targetTime: key.time, interpolation: key.interpolation, handles: [])
            }
        }
        model.commitPendingCommands()
        model.endGesture()
        model.refreshModel(force: true)
    }

    // ---- Lista -------------------------------------------------------------------

    @ViewBuilder private var listBody: some View {
        Text(AureaText.t("fn_part_list_hint")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
            .fixedSize(horizontal: false, vertical: true).padding(.bottom, 4)
        ForEach(Array(names.enumerated()), id: \.offset) { i, n in
            let name = n.isEmpty ? AureaText.t("fx3d_part_n", i + 1) : n
            let textured = effect(Self.textureKey, part: i) != nil
            let hidden = effect(Self.hideKey, part: i)?.enabled == true
            let state = [textured ? AureaText.t("m3d_part_own_texture") : nil, hidden ? AureaText.t("m3d_part_hidden") : nil]
                .compactMap { $0 }.joined(separator: " · ")
            Button { open = i } label: {
                HStack(spacing: 10) {
                    CupertinoGlyph.text(CupertinoGlyph.CubeBox, size: 18, color: hidden ? AureaColors.muted : AureaColors.text)
                    Text(name).font(.aurea(size: 14, weight: .semibold)).foregroundStyle(AureaColors.text).lineLimit(1)
                        .frame(maxWidth: .infinity, alignment: .leading)
                    if !state.isEmpty { Text(state).font(.aurea(size: 12)).foregroundStyle(AureaColors.accent).lineLimit(1) }
                    CupertinoGlyph.text(CupertinoGlyph.ChevronRight, size: 14, color: AureaColors.muted).mirrorsInRtl()
                }.padding(.horizontal, 12).frame(minHeight: 48).contentShape(Rectangle())
                    .background(AureaColors.fxLowSurface, in: RoundedRectangle(cornerRadius: 6))
            }
            .buttonStyle(AureaPressStyle(shrink: 1)).padding(.vertical, 2)
            .accessibilityLabel(name)
            .accessibilityValue(state)
            .accessibilityIdentifier("panel3d.part.\(i)")
        }
    }

    // ---- Parte aberta ------------------------------------------------------------------
    @ViewBuilder private func openBody(_ part: Int) -> some View {
        let name = names[part].isEmpty ? AureaText.t("fx3d_part_n", part + 1) : names[part]
        HStack(spacing: 0) {
            Button { open = nil } label: {
                CupertinoGlyph.text(CupertinoGlyph.ChevronLeft, size: 18, color: AureaColors.text).mirrorsInRtl()
                    .frame(width: 48, height: 48).contentShape(Rectangle())
            }.buttonStyle(.plain).accessibilityLabel(AureaText.t("fn_part_back")).accessibilityIdentifier("panel3d.part.back")
            Text(name).font(.aurea(size: 15, weight: .bold)).lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
        }.accessibilityElement(children: .contain).accessibilityIdentifier("panel3d.part.open")
        textureRows(effect(Self.textureKey, part: part), tag: "panel3d.part.texture")
        let hide = effect(Self.hideKey, part: part)
        NativePanelToggle(AureaText.t("fn_part_hide"), checked: hide?.enabled == true) { on in
            if on, hide == nil {
                if let fx = ensure(Self.hideKey, part: part) {
                    model.engine.setEffect(fx, forLayer: layerId, paramIndex: 1, value: Float(part + 1))
                    model.commitPendingCommands(); model.refreshModel(force: true)
                }
            } else if let hide {
                model.engine.setEffect(hide.effectId, forLayer: layerId, enabled: on); model.refreshModel(force: true)
            }
        }.padding(.top, 6).accessibilityIdentifier("panel3d.part.hide")
    }
}
