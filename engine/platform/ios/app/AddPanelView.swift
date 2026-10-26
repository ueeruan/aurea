// =============================================================================
//  Aurea / platform / ios / app / AddPanelView.swift
//
//  Omega 0.3.2, área 08 (adicionar): o conteúdo do botão aberto na barra de
//  adicionar, EMBUTIDO acima dela (193 pt; a barra do AUREA fica, sem "+").
//  Forma em páginas, faixas rápidas de mídia e áudio (MediaBrowserView.swift,
//  AudioBrowserView.swift), Elemento em grade (com Grupo vazio e
//  Elemento/Projeto), Presets como faixa de modelos, Vetor e 3D em grade de
//  objetos. Par de editor/AddLayerPanel.kt (Android): ids iguais.
// =============================================================================
import SwiftUI
import UIKit

// MARK: - Papéis de cor (Omega adicionar; derivados dos tons que todo tema tem)
extension AureaColors {
    /// Fundo do painel de adicionar e dos navegadores (A9).
    static var addPanel: Color { editorPanel }
    /// Prévia do navegador, divisores e células apertadas (A11).
    static var addPanelDeep: Color { background }
    /// Fundo da miniatura sem imagem e das células de ação (S8).
    static var addTile: Color { border }
    /// Barra de controle do navegador de mídia (S7).
    static var addControlBar: Color { chipHigh }
    /// Véu leve sobre miniaturas (#11000000), barra "+" do áudio (#32000000),
    /// faixa de título (27 %) e chip do nome (40 %).
    static let addScrimSoft = Color.black.opacity(0x11 / 255.0)
    static let addScrimBar = Color.black.opacity(0x32 / 255.0)
    static let addTitleScrim = Color.black.opacity(0.27)
    static let addChipScrim = Color.black.opacity(0.4)
    /// Selo "NOVO" e o texto escuro dele.
    static let addNewBadge = Color(hex: 0xFFCD4B)
    static let addOnNewBadge = Color(hex: 0x1F2238)
    /// Página "Exportar e compartilhar" (clara no Omega, D1).
    static var exportPage: Color { lightSheet }
    static var exportCard: Color { lightSurface }
    static var exportText: Color { lightText }
    static var exportHint: Color { lightTextMuted }
    static var exportLine: Color { lightDivider }
    static var exportPrimary: Color { lightInk }
    static var exportOnPrimary: Color { lightOnInk }
    /// Seta e ⓘ das linhas (#a4a4a4 no Omega).
    static var exportAction: Color { lightTextFaint }
    /// Página do resultado (azul-marinho) e os botões dela (S9).
    static var exportResult: Color { lightIsLight ? lightInk : background }
    static var exportResultButton: Color { chipHigh }
    /// Véu do progresso sobre a página (50 %).
    static let exportScrim = Color.black.opacity(0.5)
}

/// Preferências de interface do adicionar (não são do projeto).
enum OmegaAddPrefs {
    static let defaults = UserDefaults.standard
    static func bool(_ key: String) -> Bool { defaults.bool(forKey: "omega.add." + key) }
    static func set(_ value: Any?, _ key: String) { defaults.set(value, forKey: "omega.add." + key) }
    static func string(_ key: String) -> String? { defaults.string(forKey: "omega.add." + key) }
    static func strings(_ key: String) -> [String] { defaults.stringArray(forKey: "omega.add." + key) ?? [] }
    static func int(_ key: String) -> Int { defaults.integer(forKey: "omega.add." + key) }
}

// MARK: - O painel embutido (P0-1)

/// O conteúdo do botão aberto (`shell.addCategory`), sem cabeçalho: o ✕ de 32 pt
/// fica no canto (alvo de 48). Trocar de botão troca o conteúdo; tocar no
/// botão aceso, no palco ou adicionar algo fecha.
struct AddPanelView: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation

    var body: some View {
        ZStack(alignment: .topTrailing) {
            content
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .accessibilityElement(children: .contain)
                .accessibilityIdentifier("editor.addPanel")
            Button(action: close) {
                ZStack {
                    Circle().fill(AureaColors.addPanelDeep.opacity(0.85)).frame(width: 32, height: 32)
                    CupertinoGlyph.text(CupertinoGlyph.Xmark, size: 14, color: AureaColors.text)
                }.frame(width: 48, height: 48).contentShape(Rectangle())
            }.buttonStyle(.plain)
                .accessibilityLabel(AureaText.t("editor_fechar_adicionar"))
                .accessibilityIdentifier("add.close")
        }
        .background(AureaColors.addPanel)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("add.panel")
    }

    @ViewBuilder private var content: some View {
        switch shell.addCategory {
        case 0: ShapesPagerView(close: close)
        case 1: MediaStripView(close: close)
        case 2: AudioStripView(close: close)
        case 3:
            ObjectGridView(cells: [ObjectCell(label: AureaText.t("sh_add_tab_text"), tag: "add.text", glyph: CupertinoGlyph.Textformat) {
                _ = model.addText(); close()
            }])
        case ShellAddCategories.textPresets: TextPresetStripView(close: close)
        case 4: ElementGridView(close: close)
        case 5: Model3DGridView(close: close)
        case 6:
            ObjectGridView(cells: [ObjectCell(label: AureaText.t("editor_mao_livre"), tag: "add.draw.freehand", glyph: ShellGlyph.Scribble) {
                close(); model.addVector(0, freehand: true)
            }])
        default: VectorGridView(close: close)
        }
    }

    private func close() { model.showAddLayer = false }
}

/// Pede um seletor à raiz do editor (`AddLayerPickers`) depois de fechar o painel.
@MainActor func omegaRequestPicker(_ model: AureaModel, _ shell: ShellPresentation, _ kind: ShellAddPicker) {
    model.showAddLayer = false
    shell.addPicker = kind
}

// MARK: - Grade de objetos (Elemento, Vetor, 3D)

struct ObjectCell: Identifiable {
    let id = UUID()
    let label: String
    let tag: String
    var glyph: Character? = nil
    var tint: Color? = nil
    var icon: AnyView? = nil
    let action: () -> Void
}

/// Grade centrada da referência (Objeto): margem de 10, células de 64 × 71 na
/// primeira fileira e 64 × 64 nas outras; com mais do que cabe, rola na vertical.
struct ObjectGridView: View {
    let cells: [ObjectCell]
    var footer: String? = nil
    var warning: String? = nil

    var body: some View {
        GeometryReader { geometry in
            let perRow = max(1, min(cells.count, Int((geometry.size.width - 20) / 84)))
            ScrollView(.vertical, showsIndicators: false) {
                VStack(spacing: 0) {
                    ForEach(Array(stride(from: 0, to: cells.count, by: perRow)), id: \.self) { start in
                        HStack(spacing: 0) {
                            ForEach(cells[start..<min(cells.count, start + perRow)]) { cell in
                                ObjectCellView(cell: cell, height: start == 0 ? 71 : 64)
                            }
                        }
                    }
                    if let footer {
                        Text(footer).font(.aurea(size: 11)).foregroundStyle(AureaColors.muted)
                            .fixedSize(horizontal: false, vertical: true).padding(.horizontal, 18).padding(.vertical, 6)
                    }
                    if let warning {
                        Text(warning).font(.aurea(size: 11)).foregroundStyle(AureaColors.warning)
                            .fixedSize(horizontal: false, vertical: true).padding(.horizontal, 18).padding(.vertical, 2)
                    }
                }
                .frame(maxWidth: .infinity)
                .padding(.top, 16).padding(.bottom, 10)
            }
        }
    }
}

private struct ObjectCellView: View {
    let cell: ObjectCell
    let height: CGFloat
    var body: some View {
        Button(action: cell.action) {
            VStack(spacing: 5) {
                if let icon = cell.icon { icon.frame(width: 30, height: 30) }
                else if let glyph = cell.glyph { CupertinoGlyph.text(glyph, size: 28, color: cell.tint ?? AureaColors.text).frame(width: 30, height: 30) }
                Text(cell.label).font(.aurea(size: 10, weight: .medium)).foregroundStyle(AureaColors.text)
                    .lineLimit(2).multilineTextAlignment(.center).frame(maxWidth: 60)
            }
            .frame(width: 64, height: height)
            .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle())
        .padding(10)
        .accessibilityLabel(cell.label)
        .accessibilityIdentifier(cell.tag)
    }
}

// MARK: - Forma em páginas (P0-2)

/// Presets de `Engine::add_shape` na ordem de leitura (a mesma do Android).
private let omegaShapes: [(Int, String)] = [
    (0, "sh_shape_circle"), (10, "sh_shape_square"), (1, "sh_shape_rounded"), (12, "sh_shape_capsule"),
    (4, "sh_shape_triangle"), (14, "sh_shape_right_triangle"), (6, "editor_poligono"), (11, "editor_estrela"),
    (2, "sh_shape_cross"), (3, "sh_shape_ring"), (5, "sh_shape_slice"), (7, "sh_shape_flower"), (8, "sh_shape_arrow"),
    (16, "sh_shape_pentagon"), (15, "sh_shape_octagon"), (17, "sh_shape_trapezoid"), (18, "sh_shape_parallelogram"),
    (19, "sh_shape_star4"), (20, "sh_shape_star6"), (21, "sh_shape_gear"), (22, "sh_shape_double_arrow"),
    (23, "sh_shape_burst"), (24, "sh_shape_line"), (25, "sh_shape_diamond"), (26, "sh_shape_heart"),
    (27, "sh_shape_seal"), (28, "sh_shape_arc"), (29, "sh_shape_bubble"), (30, "sh_shape_bolt"),
    (31, "sh_shape_wave"), (32, "sh_shape_blob"),
]

/// Páginas de 3 fileiras × `max(4, (largura − 18)/48)` colunas, células de 48
/// com a miniatura de 40 em branco a 70 %, pontos de página embaixo. Sem rótulo
/// visível (fica na acessibilidade). Tocar põe a forma e fecha.
private struct ShapesPagerView: View {
    @EnvironmentObject private var model: AureaModel
    let close: () -> Void
    @State private var page = 0

    var body: some View {
        GeometryReader { geometry in
            let usable = max(0, geometry.size.width - 18)
            let cols = max(4, Int(usable / 48))
            let perPage = cols * 3
            let pages = max(1, (omegaShapes.count + perPage - 1) / perPage)
            let cellWidth = usable / CGFloat(cols)
            VStack(spacing: 0) {
                TabView(selection: $page) {
                    ForEach(0..<pages, id: \.self) { index in
                        let slice = Array(omegaShapes.dropFirst(index * perPage).prefix(perPage))
                        VStack(spacing: 0) {
                            ForEach(0..<3, id: \.self) { row in
                                HStack(spacing: 0) {
                                    ForEach(0..<cols, id: \.self) { col in
                                        let at = row * cols + col
                                        ZStack {
                                            if at < slice.count { shapeCell(slice[at].0, AureaText.t(slice[at].1)) }
                                        }.frame(width: cellWidth, height: 48)
                                    }
                                }
                            }
                        }
                        .padding(.horizontal, 9)
                        .tag(index)
                    }
                }
                .tabViewStyle(.page(indexDisplayMode: .never))
                .accessibilityIdentifier("add.shape.pager")
                PageDots(count: pages, current: page).accessibilityIdentifier("add.shape.dots")
            }
            .padding(.vertical, 5)
        }
    }

    private func shapeCell(_ preset: Int, _ label: String) -> some View {
        Button { model.addShape(UInt32(preset)); close() } label: {
            AureaColors.text.opacity(0.7)
                .mask(ShellShapeGlyph(preset: preset).padding(2.5))
                .frame(width: 40, height: 40)
                .frame(width: 48, height: 48)
                .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle())
        .accessibilityLabel(label)
        .accessibilityIdentifier("add.shape.\(preset)")
    }
}

/// Pontos de página (10 pt de altura), o atual em destaque.
struct PageDots: View {
    let count: Int
    let current: Int
    var body: some View {
        HStack(spacing: 6) {
            ForEach(0..<count, id: \.self) { index in
                Circle().fill(index == current ? AureaColors.accent : AureaColors.text.opacity(0.3)).frame(width: 6, height: 6)
            }
        }
        .frame(height: 10)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(AureaText.t("om_add_shape_page", current + 1, count))
    }
}

// MARK: - Células das faixas (Ver tudo, Voltar, ações)

/// Célula de ação das faixas: 75 pt, ícone + texto de 12 (e o selo NOVO opcional).
struct StripActionCell: View {
    let cell: CGFloat
    let label: String
    let glyph: Character
    let tag: String
    var tint: Color? = nil
    var badge: String? = nil
    let action: () -> Void
    var body: some View {
        Button(action: action) {
            VStack(spacing: 4) {
                CupertinoGlyph.text(glyph, size: 22, color: tint ?? AureaColors.text)
                Text(label).font(.aurea(size: 12)).foregroundStyle(AureaColors.text)
                    .lineLimit(2).multilineTextAlignment(.center).minimumScaleFactor(0.8)
            }
            .padding(4)
            .frame(width: 75, height: 75)
            .background(AureaColors.addTile, in: RoundedRectangle(cornerRadius: 2.5))
            .overlay(alignment: .topTrailing) {
                if let badge {
                    Text(badge).font(.aurea(size: 7, weight: .bold)).foregroundStyle(AureaColors.addOnNewBadge)
                        .padding(.horizontal, 3).padding(.vertical, 1)
                        .background(AureaColors.addNewBadge, in: RoundedRectangle(cornerRadius: 3)).padding(4)
                }
            }
            .frame(width: cell, height: cell)
            .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle())
        .accessibilityLabel(label)
        .accessibilityIdentifier(tag)
    }
}

/// Faixa de título dos cartões: véu preto a 27 %, nome 10 em negrito.
struct TitleStrip: View {
    let title: String
    var body: some View {
        Text(title).font(.aurea(size: 10, weight: .bold)).foregroundStyle(AureaColors.onImage).lineLimit(1)
            .padding(.horizontal, 4).padding(.vertical, 2)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(AureaColors.addTitleScrim)
    }
}

/// Xadrez de 6 pt (fundo transparente dos cartões).
struct CheckerBackground: View {
    var body: some View {
        Canvas { context, size in
            context.fill(Path(CGRect(origin: .zero, size: size)), with: .color(AureaColors.addPanelDeep))
            let cell: CGFloat = 6
            var y: CGFloat = 0, row = 0
            while y < size.height {
                var x: CGFloat = row % 2 == 0 ? 0 : cell
                while x < size.width {
                    context.fill(Path(CGRect(x: x, y: y, width: cell, height: cell)), with: .color(AureaColors.addTile))
                    x += cell * 2
                }
                y += cell; row += 1
            }
        }
    }
}

/// Barra de 42 pt dos navegadores: botão à esquerda (✕ ou ←) e o título centrado.
struct BrowserTopBar<Trailing: View>: View {
    let title: String
    let glyph: Character
    let description: String
    let tag: String
    let action: () -> Void
    @ViewBuilder var trailing: Trailing
    var body: some View {
        VStack(spacing: 0) {
            ZStack {
                Text(title).font(.aurea(size: 14, weight: .semibold)).foregroundStyle(AureaColors.text).lineLimit(1)
                    .padding(.horizontal, 56)
                HStack(spacing: 0) {
                    Button(action: action) {
                        CupertinoGlyph.text(glyph, size: 18, color: AureaColors.text).mirrorsInRtl(CupertinoGlyph.mirrorsInRtl(glyph))
                            .frame(width: 44, height: 42).contentShape(Rectangle())
                    }.buttonStyle(.plain).accessibilityLabel(description).accessibilityIdentifier(tag)
                    Spacer(minLength: 0)
                    trailing
                }
            }
            .frame(height: 42)
            .background(AureaColors.addPanel)
            Rectangle().fill(AureaColors.addPanelDeep).frame(height: 0.5)
        }
    }
}

extension BrowserTopBar where Trailing == EmptyView {
    init(title: String, glyph: Character, description: String, tag: String, action: @escaping () -> Void) {
        self.init(title: title, glyph: glyph, description: description, tag: tag, action: action) { EmptyView() }
    }
}

// MARK: - Elemento (P0-5) e a lista de projetos (P1-6)

/// Câmera, Nulo, Grupo vazio e Elemento/Projeto primeiro (a referência), depois
/// os extras do AUREA. Elemento/Projeto troca o conteúdo pela faixa de projetos.
private struct ElementGridView: View {
    @EnvironmentObject private var model: AureaModel
    let close: () -> Void
    @State private var projects = false

    var body: some View {
        if projects {
            ProjectStripView(close: close) { projects = false }
        } else {
            ObjectGridView(cells: [
                // D6: Câmera aqui (referência) e também na 3D.
                ObjectCell(label: AureaText.t("om_add_camera"), tag: "add.element.camera", glyph: CupertinoGlyph.CameraFill) { model.addCamera(); close() },
                ObjectCell(label: AureaText.t("sh_add_null"), tag: "add.element.null", icon: AnyView(ShellNullGlyph())) { model.addNull(threeD: false); close() },
                ObjectCell(label: AureaText.t("om_add_empty_group"), tag: "add.element.emptyGroup", glyph: CupertinoGlyph.Folder) {
                    model.addEmptyGroup(AureaText.t("om_add_empty_group")); close()
                },
                ObjectCell(label: AureaText.t("om_add_element_project"), tag: "add.element.project", glyph: CupertinoGlyph.SquareStack3dUp) { projects = true },
                // Extras do AUREA.
                ObjectCell(label: AureaText.t("particular_title"), tag: "add.element.particles", glyph: CupertinoGlyph.Sparkles, tint: ShellColors.text3D) {
                    close(); model.addParticles(20)   // 20 = Particular (preset Padrão)
                },
                ObjectCell(label: AureaText.t("editor_camada_ajuste"), tag: "add.element.adjust", glyph: CupertinoGlyph.WandStars) { close(); model.addAdjustmentLayer() },
                ObjectCell(label: AureaText.t("scene_flare3d"), tag: "add.element.flare", glyph: CupertinoGlyph.Sparkles, tint: ShellColors.text3D) { model.addLight(3); close() },
                ObjectCell(label: AureaText.t("grid_builder"), tag: "add.element.grid", glyph: CupertinoGlyph.SquareGrid2x2) { model.createGrid(); close() },
                ObjectCell(label: AureaText.t("sh_add_group_selection"), tag: "add.element.group", glyph: CupertinoGlyph.FolderFill) {
                    if model.selection.isEmpty { model.toast = AureaText.t("sh_add_pick_layers_to_group") }
                    else { close(); model.groupSelection() }
                },
            ])
        }
    }
}

/// Faixa de 2 fileiras: [Ver tudo] [Voltar] e os outros projetos do mais
/// recente (cartão de 75 com miniatura sobre xadrez e faixa de título). Tocar
/// insere o projeto como camada de grupo.
private struct ProjectStripView: View {
    @EnvironmentObject private var model: AureaModel
    let close: () -> Void
    let back: () -> Void
    @State private var showAll = false

    private var list: [ProjectFile] {
        model.projects.filter { $0.url.path != model.projectURL?.path }.sorted { $0.modified > $1.modified }
    }

    var body: some View {
        GeometryReader { geometry in
            let cell = geometry.size.height / 2
            ScrollView(.horizontal, showsIndicators: false) {
                LazyHGrid(rows: [GridItem(.fixed(cell), spacing: 0), GridItem(.fixed(cell), spacing: 0)], spacing: 0) {
                    StripActionCell(cell: cell, label: AureaText.t("om_add_view_all"), glyph: CupertinoGlyph.SquareGrid2x2, tag: "add.element.list.viewAll") { showAll = true }
                    StripActionCell(cell: cell, label: AureaText.t("om_add_back"), glyph: CupertinoGlyph.ChevronLeft, tag: "add.element.list.back", action: back)
                    ForEach(list) { file in
                        ProjectCard(file: file, size: 75).frame(width: cell, height: cell)
                            .onTapGesture { insert(file) }
                            .accessibilityAddTraits(.isButton)
                            .accessibilityIdentifier("add.element.list.item.\(file.url.deletingPathExtension().lastPathComponent)")
                    }
                    if list.isEmpty {
                        Text(AureaText.t("om_add_project_none")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                            .multilineTextAlignment(.center).frame(width: 160, height: cell)
                    }
                }
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("add.element.list")
        .onAppear { model.refreshProjectList() }
        .fullScreenCover(isPresented: $showAll) {
            ProjectPickerView(list: list, onPick: { file in showAll = false; insert(file) }, onClose: { showAll = false })
                .environmentObject(model)
        }
    }

    private func insert(_ file: ProjectFile) {
        model.insertProjectAsLayer(file.url, title: file.name)
        close()
    }
}

/// Cartão de projeto: miniatura sobre xadrez, faixa de título.
private struct ProjectCard: View {
    let file: ProjectFile
    let size: CGFloat
    var body: some View {
        ZStack(alignment: .bottom) {
            CheckerBackground()
            if let image = UIImage(contentsOfFile: file.thumbnailURL.path) {
                Image(uiImage: image).resizable().scaledToFit()
            }
            TitleStrip(title: file.name)
        }
        .frame(width: size, height: size)
        .clipShape(RoundedRectangle(cornerRadius: 2.5))
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(AureaText.t("om_add_project_named", file.name))
    }
}

/// "Ver tudo" dos projetos: a lista inteira em tela cheia, só para escolher.
private struct ProjectPickerView: View {
    let list: [ProjectFile]
    let onPick: (ProjectFile) -> Void
    let onClose: () -> Void
    var body: some View {
        VStack(spacing: 0) {
            BrowserTopBar(title: AureaText.t("om_add_element_project"), glyph: CupertinoGlyph.Xmark,
                          description: AureaText.t("editor_fechar"), tag: "add.element.list.close", action: onClose)
            if list.isEmpty {
                Text(AureaText.t("om_add_project_none")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                ScrollView {
                    LazyVGrid(columns: [GridItem(.adaptive(minimum: 96), spacing: 6)], spacing: 6) {
                        ForEach(list) { file in
                            ProjectCard(file: file, size: 96).onTapGesture { onPick(file) }.accessibilityAddTraits(.isButton)
                        }
                    }.padding(6)
                }
            }
        }
        .background(AureaColors.addPanel.ignoresSafeArea())
    }
}

// MARK: - 3D (só no AUREA) e Vetor (P0-7) na grade de objetos

private struct Model3DGridView: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    let close: () -> Void
    @State private var shapes = false

    var body: some View {
        if shapes {
            ObjectGridView(cells: [ObjectCell(label: AureaText.t("shape3d_back"), tag: "add.model3d.back", glyph: CupertinoGlyph.ChevronLeft) { shapes = false }]
                           + (0..<Shape3DState.names.count).map { kind in
                ObjectCell(label: AureaText.t(Shape3DState.names[kind]), tag: "shape3d.add.\(kind)", icon: AnyView(Shape3DGlyph(kind: kind))) {
                    model.addShape3D(kind: kind); close()
                }
            }, footer: AureaText.t("shape3d_hint"))
        } else {
            ObjectGridView(cells: [
                ObjectCell(label: AureaText.t("shape3d_title"), tag: "add.model3d.shapes", icon: AnyView(Shape3DGlyph(kind: 0))) { shapes = true },
                ObjectCell(label: AureaText.t("sh_add_model_3d"), tag: "add.model3d.file", glyph: CupertinoGlyph.Cube, tint: AureaColors.accent) {
                    omegaRequestPicker(model, shell, .model)
                },
                ObjectCell(label: AureaText.t("sh_add_text_3d"), tag: "add.model3d.text", glyph: ShellGlyph.TextformatAlt, tint: ShellColors.text3D) {
                    _ = model.addText3D(content: AureaText.t("panel_texto"), depth: 0.25); close()
                },
                ObjectCell(label: AureaText.t("scene_workspace"), tag: "add.model3d.scene", glyph: CupertinoGlyph.Cube) { close(); model.enterSceneEditor() },
                ObjectCell(label: AureaText.t("panel_camera_3d"), tag: "add.model3d.camera", glyph: CupertinoGlyph.CameraFill) { model.addCamera(); close() },
                ObjectCell(label: AureaText.t("real_camera_title"), tag: "add.model3d.realCamera", glyph: CupertinoGlyph.CameraViewfinder) { model.startRealCamera(); close() },
                ObjectCell(label: AureaText.t("sh_add_null_3d"), tag: "add.model3d.null", icon: AnyView(ShellNullGlyph())) { model.addNull(threeD: true); close() },
            ], footer: AureaText.t("sh_add_model_3d_hint"), warning: AureaText.t("model3d_risk_warning"))
        }
    }
}

private struct VectorGridView: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    let close: () -> Void
    var body: some View {
        ObjectGridView(cells: [
            ObjectCell(label: AureaText.t("editor_desenhar_pontos"), tag: "add.vector.points", icon: AnyView(ShellVectorGlyph(kind: 0))) { model.addVector(0) },
            ObjectCell(label: AureaText.t("editor_retangulo"), tag: "add.vector.rect", icon: AnyView(ShellVectorGlyph(kind: 1))) { model.addVector(1) },
            ObjectCell(label: AureaText.t("editor_elipse"), tag: "add.vector.ellipse", icon: AnyView(ShellVectorGlyph(kind: 2))) { model.addVector(2) },
            ObjectCell(label: AureaText.t("editor_poligono"), tag: "add.vector.polygon", icon: AnyView(ShellVectorGlyph(kind: 3))) { model.addVector(3) },
            ObjectCell(label: AureaText.t("editor_estrela"), tag: "add.vector.star", icon: AnyView(ShellVectorGlyph(kind: 4))) { model.addVector(4) },
            ObjectCell(label: AureaText.t("editor_importar_svg"), tag: "add.vector.svg", glyph: CupertinoGlyph.DocText) { omegaRequestPicker(model, shell, .svg) },
            ObjectCell(label: AureaText.t("psd_import"), tag: "add.vector.psd", glyph: CupertinoGlyph.PhotoOnRectangle) { omegaRequestPicker(model, shell, .psd) },
        ], footer: AureaText.t("editor_vetor_contorno_pontos_voce_arrasta_curva"))
    }
}

// MARK: - Presets como faixa de modelos (P0-6)

/// Faixa de 2 fileiras: [Ver tudo] (o seletor completo de hoje, em tela cheia),
/// [Descobrir mais] (os presets da comunidade, com o selo NOVO até o primeiro
/// toque) e os presets de texto, os recentes primeiro. Tocar cria o texto já com
/// o preset e fecha.
private struct TextPresetStripView: View {
    @EnvironmentObject private var model: AureaModel
    let close: () -> Void
    @State private var entries: [PanelPresetEntry] = []
    @State private var discoverSeen = OmegaAddPrefs.bool("presets.discover.seen")
    @State private var discover = false

    var body: some View {
        GeometryReader { geometry in
            let cell = geometry.size.height / 2
            ScrollView(.horizontal, showsIndicators: false) {
                LazyHGrid(rows: [GridItem(.fixed(cell), spacing: 0), GridItem(.fixed(cell), spacing: 0)], spacing: 0) {
                    StripActionCell(cell: cell, label: AureaText.t("om_add_view_all"), glyph: CupertinoGlyph.SquareGrid2x2, tag: "add.presets.viewAll") {
                        model.omegaPresetsBrowser = true
                    }
                    StripActionCell(cell: cell, label: AureaText.t("om_add_discover_presets"), glyph: CupertinoGlyph.Sparkles, tag: "add.presets.discover",
                                    badge: discoverSeen ? nil : AureaText.t("om_badge_new")) {
                        if !discoverSeen { discoverSeen = true; OmegaAddPrefs.set(true, "presets.discover.seen") }
                        discover = true
                    }
                    ForEach(entries) { entry in
                        Button { add(entry) } label: {
                            ZStack(alignment: .bottom) {
                                AureaColors.addPanelDeep
                                PresetMotionThumb(preset: entry.textPreset).padding(.bottom, 16)
                                TitleStrip(title: entry.name)
                            }
                            .frame(width: 75, height: 75).clipShape(RoundedRectangle(cornerRadius: 2.5))
                            .frame(width: cell, height: cell).contentShape(Rectangle())
                        }
                        .buttonStyle(AureaPressStyle())
                        .accessibilityLabel(AureaText.t("tp_add_named", entry.name))
                        .accessibilityIdentifier("add.presets.item.\(entry.id)")
                    }
                }
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("add.presets.strip")
        .onAppear(perform: load)
        .fullScreenCover(isPresented: $discover) {
            VStack(spacing: 0) {
                BrowserTopBar(title: AureaText.t("om_add_discover_presets"), glyph: CupertinoGlyph.Xmark,
                              description: AureaText.t("editor_fechar"), tag: "add.presets.discover.close") { discover = false }
                HomeCommunityPresets()
            }
            .background(AureaColors.lightBackground.ignoresSafeArea())
            .environmentObject(model)
        }
    }

    private func load() {
        let all = PanelPresetEntry.loadAll().filter { $0.kind == .text }
        let recents = UserDefaults.standard.stringArray(forKey: "presetRecents") ?? []
        let recent = recents.compactMap { key in all.first { $0.id == key } }
        entries = recent + all.filter { entry in !recent.contains { $0.id == entry.id } }
    }

    /// O mesmo caminho do `TextPresetPicker`: cria o texto e aplica o preset.
    private func add(_ entry: PanelPresetEntry) {
        if model.status.playing != 0 { model.playPause() }
        let id = model.addText(openEditor: false)
        guard id >= 0 else { return }
        if let native = entry.textPreset {
            guard model.engine.applyTextPreset(id, preset: native) else {
                model.toast = AureaText.t("msg_animacao_de_texto_so_vale_para"); model.refreshModel(force: true); close(); return
            }
        } else {
            guard let source = entry.source else { model.toast = AureaText.t("msg_arquivo_do_preset_nao_encontrado"); close(); return }
            let error = model.engine.applyPreset(id, json: source, duration: 0)
            if !error.isEmpty { model.toast = AureaText.t("msg_preset_nao_aplicado", AureaEngineText.reason(error)); model.refreshModel(force: true); close(); return }
        }
        var recents = UserDefaults.standard.stringArray(forKey: "presetRecents") ?? []
        recents.removeAll { $0 == entry.id }; recents.insert(entry.id, at: 0)
        UserDefaults.standard.set(Array(recents.prefix(10)), forKey: "presetRecents")
        model.refreshModel(force: true)
        model.toast = AureaText.t("msg_aplicado", entry.name)
        close()
        model.playPause()
    }
}

/// "Aa" animado pelo tipo do preset (a mesma ideia da prévia do seletor).
private struct PresetMotionThumb: View {
    let preset: UInt32?
    var body: some View {
        TimelineView(.animation) { context in
            let phase = CGFloat(context.date.timeIntervalSinceReferenceDate.truncatingRemainder(dividingBy: 2.4) / 2.4)
            HStack(spacing: 0) {
                ForEach(Array("Aa".enumerated()), id: \.offset) { index, glyph in
                    let progress = min(1, max(0, min(2, phase * 2) - CGFloat(index) * 0.12))
                    let opacity: CGFloat = {
                        switch preset {
                        case .some(20), .some(21), .some(23), .some(24): return min(1, max(0, progress * 3)) * min(1, max(0, (1 - phase) * 5))
                        case .some(22): return 0.3 + 0.7 * abs(sin(phase * 60 + CGFloat(index) * 4))
                        case .none: return 1
                        default: return min(1, max(0.35, progress * 2))
                        }
                    }()
                    let scale: CGFloat = preset == 19 ? 1 - 0.8 * sin(progress * .pi) : 1
                    let lift: CGFloat = preset == 2 || preset == 9 ? (1 - progress) * 8 : 0
                    Text(String(glyph)).font(.aurea(size: 22, weight: .bold)).foregroundStyle(AureaColors.text)
                        .opacity(opacity).scaleEffect(scale).offset(y: lift)
                }
            }
        }
        .accessibilityHidden(true)
    }
}

/// "Ver tudo" dos presets: o seletor completo de hoje (Texto | Texto 3D), em tela cheia.
struct PresetsBrowserView: View {
    @EnvironmentObject private var model: AureaModel
    var body: some View {
        VStack(spacing: 0) {
            BrowserTopBar(title: AureaText.t("tp_title"), glyph: CupertinoGlyph.Xmark, description: AureaText.t("editor_fechar"),
                          tag: "presets.browser.close") { model.omegaPresetsBrowser = false }
            TextPresetPicker {
                model.omegaPresetsBrowser = false
                model.showAddLayer = false
            }
            .padding(.top, 6)
        }
        .background(AureaColors.addPanel.ignoresSafeArea())
        .preferredColorScheme(.dark)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("presets.browser")
    }
}
