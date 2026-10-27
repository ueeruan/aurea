// =============================================================================
//  Aurea / platform / ios / app / AddPanelView.swift
//
//  Omega 0.3.2, área 08 (adicionar): o conteúdo do botão aberto na barra de
//  adicionar, EMBUTIDO acima dela (193 pt; a barra do AUREA fica, sem "+").
//  Forma em páginas, faixas rápidas de mídia e áudio (MediaBrowserView.swift,
//  AudioBrowserView.swift), Elemento em grade (com Grupo vazio e
//  Elemento/Projeto), Presets como faixa de modelos, Vetor e 3D em grade de
//  objetos. Par de editor/AddLayerPanel.kt (Android): ids iguais.
//  0.3.3 barra: a barra de adicionar nova (AddBarView, a grade do "+" em
//  AddBarMoreOverlay) também mora aqui, no fim do arquivo.
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
    @EnvironmentObject private var shell: ShellPresentation
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
                // 0.3.3 barra: o Vetor (pontos, SVG, PSD) não tem vaga na barra nova; mora aqui.
                ObjectCell(label: AureaText.t("sh_add_tab_vector"), tag: "add.element.vector", glyph: CupertinoGlyph.PencilOutline) {
                    shell.addCategory = 7
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
        SwiftUI.TimelineView(.animation) { context in
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

// MARK: - 0.3.3 barra: a nova barra de adicionar (desenho do dono)

/// As categorias da barra (índices das `ShellAddCategories`). Par de
/// `ADD_BAR_PILL`/`ADD_BAR_GRID`/`addBarSlot` do Android (AddLayerPanel.kt).
enum AddBarCatalog {
    /// As 5 da pílula: Forma, Mídia, Áudio, Texto, Elemento. Ids `addBar.<AddTab>`.
    static let pill = [0, 1, 2, 3, 4]
    /// A grade 4 × 2 do "+": Forma, Mídia, Áudio, Texto / Presets, Elemento, 3D, Desenho.
    /// Ids `addBar.grid.<AddTab>`.
    static let grid = [0, 1, 2, 3, ShellAddCategories.textPresets, 4, 5, 6]
    /// A vaga da pílula que acende para o conteúdo aberto (Presets no Texto; 3D e
    /// Vetor no Elemento), mostrando o ícone e o nome do que está aberto.
    static func slot(_ index: Int) -> Int {
        switch index {
        case ShellAddCategories.textPresets: return 3
        case 5, 7: return 4
        case 6: return 0
        default: return index
        }
    }
    /// Altura nominal do bloco da grade (par de `ADD_BAR_GRID_HEIGHT`).
    static let gridHeight: CGFloat = EditorLayout.addBarGridHeight
}

/// Estado só da barra (não é do projeto): a grade do "+" aberta.
@MainActor final class AddBarState: ObservableObject {
    static let shared = AddBarState()
    @Published var more = false

    /// Mola suave de ~200 ms; nada com "reduzir movimento".
    static func animation(_ reduce: Bool) -> Animation? { reduce ? nil : .spring(response: 0.24, dampingFraction: 0.86) }

    /// O "+" abre a grade (pausa e fecha o conteúdo aberto); o "X" fecha.
    static func toggle(_ model: AureaModel, reduce: Bool) {
        let open = !shared.more
        if open {
            model.openAddLayer()   // pausa e fecha o painel aberto
            model.showAddLayer = false
        }
        withAnimation(animation(reduce)) { shared.more = open }
    }

    static func close(reduce: Bool) {
        guard shared.more else { return }
        withAnimation(animation(reduce)) { shared.more = false }
    }

    /// Abre um destino pela pílula ou pela grade (a grade fecha antes). Texto cria
    /// o texto na hora e Desenho entra no desenho livre, como antes.
    static func open(_ index: Int, model: AureaModel, shell: ShellPresentation, reduce: Bool) {
        close(reduce: reduce)
        if index == 3 {
            model.openAddLayer()
            model.showAddLayer = false
            _ = model.addText()
            return
        }
        if index == 6 {
            model.openAddLayer()
            model.showAddLayer = false
            model.addVector(0, freehand: true)
            return
        }
        if model.showAddLayer && shell.addCategory == index {
            model.showAddLayer = false
            return
        }
        shell.addCategory = index
        model.openAddLayer()
    }
}

/// Cores da barra: só papéis da paleta AUREA.
private enum AddBarColors {
    /// O bloco flutuante: o tom mais fundo do tema.
    static var block: Color { AureaColors.background }
    /// A pílula, os botões redondos e o cartão da grade.
    static var pill: Color { AureaColors.chip }
    /// Os ladrilhos da grade e o fio fino em volta da pílula, dos botões e do cartão.
    static var raised: Color { AureaColors.chipHigh }
    /// A pílula interna acesa: o destaque da marca a 30 % (sobre a pílula).
    static var active: Color { AureaColors.accent.opacity(0.3) }
}

/// Ícones da barra e da grade: os do AUREA (fonte CupertinoIcons) e, onde a fonte
/// não tem o desenho, três desenhados (Forma: quadrado com nós; Elemento: flor;
/// Desenho: imagem com lápis), com a geometria do Android (caixa de 24, traço 2 + bold).
struct AddBarIcon: View {
    let category: Int
    let size: CGFloat
    var color: Color = AureaColors.text

    var body: some View {
        switch category {
        case 0, 4, 6:
            Canvas { context, box in AddBarDrawing.draw(category, context, box, color) }
                .frame(width: size, height: size)
                .accessibilityHidden(true)
        case 1:
            CupertinoGlyph.text(CupertinoGlyph.Photo, size: size, color: color).frame(width: size, height: size)
        default:
            CupertinoGlyph.text(ShellAddCategories.all[category].1, size: size, color: color).frame(width: size, height: size)
        }
    }
}

private enum AddBarDrawing {
    /// 0.3.4: o traço fino do estilo dos ícones (`AureaIconStyle.line`), o mesmo peso
    /// dos glifos da fonte ao lado. Par do `ADD_BAR_STROKE` do Android.
    static let stroke: CGFloat = AureaIconStyle.line

    static func draw(_ category: Int, _ context: GraphicsContext, _ box: CGSize, _ color: Color) {
        let u = min(box.width, box.height) / 24
        let o = CGPoint(x: (box.width - 24 * u) / 2, y: (box.height - 24 * u) / 2)
        func p(_ x: CGFloat, _ y: CGFloat) -> CGPoint { CGPoint(x: o.x + x * u, y: o.y + y * u) }
        let style = StrokeStyle(lineWidth: stroke * u, lineCap: .round, lineJoin: .round)
        var path = Path()
        switch category {
        case 0:
            // Forma: quadrado com um nó em cada canto.
            let lo: CGFloat = 5, hi: CGFloat = 19, r: CGFloat = 2.3
            for corner in [p(lo, lo), p(hi, lo), p(hi, hi), p(lo, hi)] {
                path.addEllipse(in: CGRect(x: corner.x - r * u, y: corner.y - r * u, width: 2 * r * u, height: 2 * r * u))
            }
            path.move(to: p(lo + r, lo)); path.addLine(to: p(hi - r, lo))
            path.move(to: p(hi, lo + r)); path.addLine(to: p(hi, hi - r))
            path.move(to: p(lo + r, hi)); path.addLine(to: p(hi - r, hi))
            path.move(to: p(lo, lo + r)); path.addLine(to: p(lo, hi - r))
            context.stroke(path, with: .color(color), style: style)
        case 4:
            // Elemento: flor de 8 pétalas (só o arco de fora de cada uma) e o miolo.
            let center = p(12, 12)
            let ring: CGFloat = 5.4 * u
            let petal: CGFloat = 3.1 * u
            let half: CGFloat = 64.3 * CGFloat.pi / 180
            for k in 0..<8 {
                let a = CGFloat(k) * CGFloat.pi / 4
                let px = center.x + ring * cos(a)
                let py = center.y + ring * sin(a)
                for t in 0...16 {
                    let b = a - half + 2 * half * CGFloat(t) / 16
                    let q = CGPoint(x: px + petal * cos(b), y: py + petal * sin(b))
                    if t == 0 { path.move(to: q) } else { path.addLine(to: q) }
                }
            }
            context.stroke(path, with: .color(color), style: style)
            let core: CGFloat = 2.1 * u
            context.stroke(Path(ellipseIn: CGRect(x: center.x - core, y: center.y - core, width: 2 * core, height: 2 * core)),
                           with: .color(color), lineWidth: stroke * u)
        default:
            // Desenho: moldura de imagem aberta no canto, montanha, sol e um lápis na diagonal.
            path.move(to: p(18, 10)); path.addLine(to: p(18, 6)); path.addQuadCurve(to: p(15, 3), control: p(18, 3))
            path.addLine(to: p(6, 3)); path.addQuadCurve(to: p(3, 6), control: p(3, 3))
            path.addLine(to: p(3, 15)); path.addQuadCurve(to: p(6, 18), control: p(3, 18)); path.addLine(to: p(10, 18))
            path.move(to: p(3.5, 15)); path.addLine(to: p(8, 10.5)); path.addLine(to: p(11.5, 14))
            path.move(to: p(11.2, 20.8)); path.addLine(to: p(14.45, 20.09)); path.addLine(to: p(21.87, 12.67))
            path.addLine(to: p(19.33, 10.13)); path.addLine(to: p(11.91, 17.55)); path.closeSubpath()
            context.stroke(path, with: .color(color), style: style)
            let sun: CGFloat = 1.5 * u
            let sunCenter = p(13, 7.5)
            context.stroke(Path(ellipseIn: CGRect(x: sunCenter.x - sun, y: sunCenter.y - sun, width: 2 * sun, height: 2 * sun)),
                           with: .color(color), lineWidth: stroke * u)
        }
    }
}

/// O "+" redondo (e o "X", que é o mesmo "+" girado 45°). Alvo de 48.
@MainActor private struct AddBarRoundButton: View {
    let label: String
    let id: String
    var rotation: Double = 0
    let action: () -> Void
    var body: some View {
        Button(action: action) {
            CupertinoGlyph.text(CupertinoGlyph.Plus, size: 22, color: AureaColors.text)
                .rotationEffect(.degrees(rotation))
                .frame(width: 48, height: 48)
                .background(AddBarColors.pill, in: Circle())
                .overlay(Circle().stroke(AddBarColors.raised, lineWidth: 0.7))
                .contentShape(Circle())
        }
        .buttonStyle(AureaPressStyle())
        .accessibilityLabel(label)
        .accessibilityIdentifier(id)
    }
}

/// A barra de adicionar (par do `AddBar` do Android): um bloco flutuante com a
/// pílula das 5 categorias e o "+" redondo à direita. Sem nada aberto, cada
/// categoria tem ícone e nome; com uma aberta, a pílula mostra só os ícones e a
/// aberta vira a pílula interna acesa (ícone e nome lado a lado), que desliza e
/// cresce até ela. Tocar na acesa fecha. O "+" abre a grade (`AddBarMoreOverlay`).
@MainActor struct AddBarView: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Namespace private var glow

    private enum Look { case stacked, iconOnly, inline }

    var body: some View {
        let open: Int? = model.showAddLayer ? shell.addCategory : nil
        let slot: Int? = open.map { AddBarCatalog.slot($0) }.flatMap { AddBarCatalog.pill.contains($0) ? $0 : nil }
        HStack(spacing: 8) {
            pill(open: open, slot: slot)
            AddBarRoundButton(label: AureaText.t("bar33_more"), id: "addBar.more") {
                AddBarState.toggle(model, reduce: reduceMotion)
            }
        }
        .padding(7)
        .frame(maxHeight: .infinity)
        .background(AddBarColors.block, in: RoundedRectangle(cornerRadius: 24, style: .continuous))
        .padding(.horizontal, 8)
        .padding(.vertical, 4)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(AureaColors.editorBar.ignoresSafeArea(edges: .bottom))
        .animation(reduceMotion ? nil : .spring(response: 0.3, dampingFraction: 0.84), value: slot)
        .animation(reduceMotion ? nil : .spring(response: 0.3, dampingFraction: 0.84), value: open)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("editor.addBar")
        // A barra saiu (camada escolhida, outra interface): a grade do "+" fecha junto.
        .onDisappear { AddBarState.shared.more = false }
    }

    private func pill(open: Int?, slot: Int?) -> some View {
        GeometryReader { geometry in
            let weights: [CGFloat] = AddBarCatalog.pill.map { slot == $0 ? 2 : 1 }
            let total = weights.reduce(0, +)
            HStack(spacing: 0) {
                ForEach(Array(AddBarCatalog.pill.enumerated()), id: \.element) { k, index in
                    let on = slot == index
                    let look: Look = slot == nil ? .stacked : (on ? .inline : .iconOnly)
                    item(index, shown: on ? (open ?? index) : index, look: look, on: on)
                        .frame(width: geometry.size.width * weights[k] / total, height: geometry.size.height)
                }
            }
        }
        .background(AddBarColors.pill, in: Capsule())
        .overlay(Capsule().stroke(AddBarColors.raised, lineWidth: 0.7))
        .clipShape(Capsule())
    }

    private func item(_ index: Int, shown: Int, look: Look, on: Bool) -> some View {
        let name = AureaText.t(ShellAddCategories.all[shown].0)
        return Button {
            if on { model.showAddLayer = false } else { AddBarState.open(index, model: model, shell: shell, reduce: reduceMotion) }
        } label: {
            ZStack {
                if on {
                    Capsule().fill(AddBarColors.active)
                        .matchedGeometryEffect(id: "addBar.glow", in: glow)
                        .padding(4)
                        .transition(.opacity)
                }
                switch look {
                case .stacked:
                    VStack(spacing: 3) {
                        AddBarIcon(category: shown, size: 21)
                        Text(name).font(.aurea(size: 11, weight: .medium)).foregroundStyle(AureaColors.text)
                            .lineLimit(1).minimumScaleFactor(0.75)
                    }
                    .transition(.opacity)
                case .iconOnly:
                    AddBarIcon(category: shown, size: 21).transition(.opacity)
                case .inline:
                    HStack(spacing: 6) {
                        AddBarIcon(category: shown, size: 21)
                        Text(name).font(.aurea(size: 13, weight: .medium)).foregroundStyle(AureaColors.text)
                            .lineLimit(1).minimumScaleFactor(0.75)
                    }
                    .padding(.horizontal, 10)
                    .transition(.opacity)
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityLabel(name)
        .accessibilityAddTraits(on ? .isSelected : [])
        .accessibilityIdentifier("addBar.\(ShellAddCategories.name(index))")
    }
}

/// A grade do "+": o bloco cresce para cima, por cima da timeline e da própria
/// barra, com o cartão 4 × 2 das categorias e o "X" redondo no lugar do "+".
/// Ocupa a área que recebe (o véu transparente: tocar fora do cartão fecha e nada
/// passa para a timeline). Mola suave de ~200 ms; nada com "reduzir movimento".
@MainActor struct AddBarMoreOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    @ObservedObject private var state = AddBarState.shared
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.layoutDirection) private var layoutDirection

    var body: some View {
        ZStack(alignment: .bottom) {
            if state.more {
                Color.clear.contentShape(Rectangle())
                    .onTapGesture { AddBarState.close(reduce: reduceMotion) }
                    .accessibilityHidden(true)
                AddBarGridBlock(reduce: reduceMotion) { index in
                    AddBarState.open(index, model: model, shell: shell, reduce: reduceMotion)
                }
                .transition(gridTransition)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottom)
    }

    private var gridTransition: AnyTransition {
        if reduceMotion { return .identity }
        let anchor: UnitPoint = layoutDirection == .rightToLeft ? .bottomLeading : .bottomTrailing
        return AnyTransition.scale(scale: 0.86, anchor: anchor).combined(with: .opacity)
    }
}

@MainActor private struct AddBarGridBlock: View {
    let reduce: Bool
    let open: (Int) -> Void
    @State private var turned = false

    var body: some View {
        HStack(alignment: .bottom, spacing: 8) {
            VStack(spacing: 6) {
                ForEach(0..<2, id: \.self) { row in
                    HStack(spacing: 6) {
                        ForEach(Array(AddBarCatalog.grid[(row * 4)..<(row * 4 + 4)]), id: \.self) { index in
                            tile(index)
                        }
                    }
                }
            }
            .padding(8)
            .background(AddBarColors.pill, in: RoundedRectangle(cornerRadius: 22, style: .continuous))
            .overlay(RoundedRectangle(cornerRadius: 22, style: .continuous).stroke(AddBarColors.raised, lineWidth: 0.7))
            .accessibilityElement(children: .contain)
            .accessibilityLabel(AureaText.t("bar33_grid"))
            .accessibilityIdentifier("addBar.grid")
            AddBarRoundButton(label: AureaText.t("bar33_close"), id: "addBar.close", rotation: turned ? 45 : 0) {
                AddBarState.close(reduce: reduce)
            }
        }
        .padding(7)
        .background(AddBarColors.block, in: RoundedRectangle(cornerRadius: 24, style: .continuous))
        // O fundo do bloco segura o toque: não fecha nem passa para a timeline.
        .contentShape(RoundedRectangle(cornerRadius: 24, style: .continuous))
        .onTapGesture { }
        .padding(.horizontal, 8)
        .padding(.vertical, 4)
        .onAppear {
            if reduce { turned = true } else { withAnimation(.spring(response: 0.26, dampingFraction: 0.8)) { turned = true } }
        }
    }

    private func tile(_ index: Int) -> some View {
        let name = AureaText.t(ShellAddCategories.all[index].0)
        return Button { open(index) } label: {
            VStack(spacing: 4) {
                AddBarIcon(category: index, size: 22)
                    .frame(maxWidth: .infinity)
                    .frame(height: 46)
                    .background(AddBarColors.raised, in: RoundedRectangle(cornerRadius: 15, style: .continuous))
                Text(name).font(.aurea(size: 12, weight: .medium)).foregroundStyle(AureaColors.text)
                    .lineLimit(1).minimumScaleFactor(0.75)
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle())
        .accessibilityLabel(name)
        .accessibilityIdentifier("addBar.grid.\(ShellAddCategories.name(index))")
    }
}
