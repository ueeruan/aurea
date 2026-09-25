// =============================================================================
//  Aurea / platform / ios / app / HomeView.swift
//
//  A casca da Home: o véu da status bar, os avisos ao vivo, as três abas
//  (Início, Projetos, Ajustes) e a barra de abas embaixo — as MESMAS
//  três do Android depois que Comunidade, Perfil e a aba Efeitos saíram (§2:
//  fora do editor não há camada escolhida, então não há o que fazer com um
//  efeito; o navegador vive onde ele serve para alguma coisa).
//
//  O que é de quem:
//    HomeView.swift         as três abas + a folha de novo projeto
//    ProjectCards.swift     os cartões, a barra da lista, a seleção em lote,
//                           a biblioteca de projetos (o disco) e as mutações
//    AppPanels.swift        os avisos ao vivo
//
//  O conteúdo rola POR BAIXO da barra, como no Android: ela fica numa ZStack
//  sobre a lista. HomeBackdrop captura apenas essa lista para o blur com os
//  mesmos sigmas, tintas e pisos de Backdrop.kt, fora da árvore das barras.
// =============================================================================
import SwiftUI
import UIKit
import PhotosUI
import UniformTypeIdentifiers
import CoreImage.CIFilterBuiltins

private enum AureaDonations {
    static let pix = "00020126360014br.gov.bcb.pix0114+55889961267175204000053039865802BR5911Ruan  Pablo6009Sao Paulo62240520daqr16872346348818576304A1F0"
    static let paypal = "https://www.paypal.com/donate/?business=C7C2A2UH88NGW&no_recurring=0&item_name=Manter+o+Aurea+APP+funcionando+de+gra%C3%A7a.&currency_code=BRL"
    // Cache one small image; export progress must not regenerate the QR code.
    static let qr: UIImage? = {
        let filter = CIFilter.qrCodeGenerator()
        filter.message = Data(pix.utf8)
        filter.correctionLevel = "M"
        guard let output = filter.outputImage,
              let cg = CIContext().createCGImage(output, from: output.extent) else { return nil }
        return UIImage(cgImage: cg)
    }()
}

struct DonationCard: View {
    var exporting = false
    @Environment(\.openURL) private var openURL
    @State private var showing = false
    @State private var copied = false

    var body: some View {
        Button { copied = false; showing = true } label: {
            HStack(spacing: 12) {
                VStack(alignment: .leading, spacing: 4) {
                    Text(AureaText.t("donation_title")).font(.aurea(size: 17, weight: .semibold))
                    Text(AureaText.t("donation_note")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                }.frame(maxWidth: .infinity, alignment: .leading)
                Image(systemName: "heart").font(.system(size: 26)).foregroundStyle(AureaColors.accent)
            }.padding(16).foregroundStyle(AureaColors.text)
                .background(AureaColors.surface, in: RoundedRectangle(cornerRadius: 16))
        }.buttonStyle(.plain).padding(.vertical, 12)
            .sheet(isPresented: $showing) {
                NavigationStack {
                    ScrollView {
                        VStack(spacing: 16) {
                            Text(AureaText.t("donation_detail"))
                            if let qr = AureaDonations.qr {
                                Image(uiImage: qr).interpolation(.none).resizable().scaledToFit()
                                    .frame(width: 216, height: 216).padding(20).background(.white)
                                    .accessibilityLabel(AureaText.t("donation_pix_qr"))
                            }
                            Text("Pix · Ruan Pablo").foregroundStyle(AureaColors.muted)
                            Button(AureaText.t("donation_copy_pix")) { copy(AureaDonations.pix) }
                                .buttonStyle(.bordered).frame(maxWidth: .infinity)
                            Button(AureaText.t(exporting ? "donation_copy_paypal" : "donation_paypal")) {
                                if exporting { copy(AureaDonations.paypal) }
                                else if let url = URL(string: AureaDonations.paypal) {
                                    openURL(url) { accepted in
                                        if !accepted { copy(AureaDonations.paypal) }
                                    }
                                }
                            }.buttonStyle(.bordered).frame(maxWidth: .infinity)
                            if exporting { Text(AureaText.t("donation_export_note")).foregroundStyle(AureaColors.muted) }
                            if copied { Text(AureaText.t("donation_copied")).foregroundStyle(AureaColors.accent) }
                        }.font(.aurea(size: 15)).padding(24)
                    }.background(AureaColors.background).foregroundStyle(AureaColors.text)
                        .navigationTitle(AureaText.t("donation_title")).navigationBarTitleDisplayMode(.inline)
                        .toolbar { ToolbarItem(placement: .confirmationAction) {
                            Button(AureaText.t("editor_fechar")) { showing = false }
                        } }
                }.preferredColorScheme(.dark)
            }
    }
    private func copy(_ value: String) { UIPasteboard.general.string = value; copied = true }
}

// =============================================================================
// Tokens da Home. Os que já existem no Theme.swift vêm de lá; estes são os que
// a casca da Home usa e ainda não estavam — MESMOS valores de `AureaTokens.kt`.
// Nenhuma cor ou medida inventada aqui.
// =============================================================================
enum HomeColors {
    /// `OnImage` / `OnImage70` — texto sobre miniatura.
    static let onImage = Color.white
    static let onImage70 = Color.white.opacity(0.70)
    /// `ImageScrim` #B3000000 — o degradê do fim do hero.
    static let imageScrim = Color.black.opacity(0.70)
    /// `Field` #FF000000 e `FieldBorder` #33FFFFFF — o CupertinoTextField da busca.
    static let field = Color.black
    static let fieldBorder = Color.white.opacity(0.20)
    static let fieldPlaceholder = Color(hex: 0xEBEBF5).opacity(0.30)
    /// `FieldDialog` #FF1C1C1E — o campo dentro do diálogo de renomear.
    static let fieldDialog = Color(hex: 0x1C1C1E)
    /// `ActionDim` #FF16304A — o chip escolhido da tela Exportar.
    static let actionDim = Color(hex: 0x16304A)
    /// `DestructiveCupertino` #FFFF453A.
    static let destructive = Color(hex: 0xFF453A)
    /// `PressHighlight` #0DFFFFFF — o realce de um botão cheio pressionado.
    static let pressHighlight = Color.white.opacity(0.05)
    /// `BetaFill` / `BetaBorder` / `Beta` #FFB020.
    static let beta = Color(hex: 0xFFB020)
    static let betaFill = Color(hex: 0xFFB020).opacity(0.13)
    static let betaBorder = Color(hex: 0xFFB020).opacity(0.33)
    /// Action sheet / alerta no estilo Cupertino (`Sheets.kt`).
    static let sheetFill = Color(hex: 0x292929).opacity(0.94)
    static let alertFill = Color(hex: 0x2D2D2D).opacity(0.95)
    static let sheetDivider = Color(hex: 0x7D7D7D).opacity(0.84)
    static let sheetTitle = Color(hex: 0xF1F1F1).opacity(0.59)
}

enum HomeDims {
    static let hairline: CGFloat = 0.5
    static let s1: CGFloat = 4
    static let s2: CGFloat = 8
    static let s3: CGFloat = 12
    static let s4: CGFloat = 16
    static let s5: CGFloat = 24
    /// Recuo lateral das telas fora do editor.
    static let gutter: CGFloat = 20
    /// Alvo redondo (44) e o círculo interno (36).
    static let roundTarget: CGFloat = 44
    static let roundCircle: CGFloat = 36
    /// Folga no fim das listas para passar da barra de abas translúcida.
    static let listEndSpace: CGFloat = 120
    static let rXs: CGFloat = 5
    static let rSm: CGFloat = 8
    static let rChip: CGFloat = 10
    static let rCard: CGFloat = 12
    static let rMd: CGFloat = 14
    static let rLg: CGFloat = 16
    static let rXl: CGFloat = 20
    static let iconXs: CGFloat = 13
    static let iconSm: CGFloat = 16
    static let iconMd: CGFloat = 20
    static let iconLg: CGFloat = 24
    static let iconXl: CGFloat = 32
    static let buttonHeight: CGFloat = 54
    static let chipHeight: CGFloat = 34
    static let searchField: CGFloat = 40
    static let quickAction: CGFloat = 46
    static let tabBar: CGFloat = 54
    static let logo: CGFloat = 38
    /// Proporção do cartão da grade: miniatura + duas linhas de texto.
    static let cardAspect: CGFloat = 1.08
    static let heroAspect: CGFloat = 16.0 / 9.0
    /// Largura de decodificação da miniatura, por onde ela aparece.
    static let heroDecode: CGFloat = 1080
    static let cardDecode: CGFloat = 512
    static let menuDot: CGFloat = 40
    static let menuDotIcon: CGFloat = 18
    static let segmentThumb: CGFloat = 7
}

/// Tipografia da Home — a MESMA escala de `AureaType` no Android.
enum HomeType {
    static let display = Font.aurea(size: 30, weight: .heavy)
    static let headlineLarge = Font.aurea(size: 34, weight: .bold)
    static let screenTitle = Font.aurea(size: 21, weight: .bold)
    static let titleLarge = Font.aurea(size: 22, weight: .bold)
    static let titleMedium = Font.aurea(size: 17, weight: .semibold)
    static let bodyLarge = Font.aurea(size: 17, weight: .regular)
    static let bodySmall = Font.aurea(size: 13, weight: .regular)
    static let greeting = Font.aurea(size: 13.5, weight: .regular)
    static let button = Font.aurea(size: 17, weight: .semibold)
    static let heroKicker = Font.aurea(size: 11, weight: .bold)
    static let heroTitle = Font.aurea(size: 18, weight: .bold)
    static let heroSpec = Font.aurea(size: 11, weight: .regular)
    static let heroPill = Font.aurea(size: 12.5, weight: .bold)
    static let listCount = Font.aurea(size: 15, weight: .bold)
    static let cardTitle = Font.aurea(size: 14, weight: .semibold)
    static let cardSpec = Font.aurea(size: 11, weight: .regular)
    static let featureTitle = Font.aurea(size: 15, weight: .semibold)
    static let empty = Font.aurea(size: 13.5, weight: .regular)
    static let batchCount = Font.aurea(size: 13, weight: .regular)
    static let batchAction = Font.aurea(size: 13, weight: .regular)
    static let searchText = Font.aurea(size: 17, weight: .regular)
    static let note = Font.aurea(size: 12.5, weight: .regular)
    static let footer = Font.aurea(size: 11, weight: .regular)
    static let segment = Font.aurea(size: 13, weight: .semibold)
    static let caps = Font.aurea(size: 12, weight: .medium)
    static let sheetSpec = Font.aurea(size: 12.5, weight: .regular)
    static let frameLabel = Font.aurea(size: 22, weight: .bold)
    static let frameHint = Font.aurea(size: 12, weight: .regular)
    static let formatLabel = Font.aurea(size: 12.5, weight: .semibold)
    static let formatHint = Font.aurea(size: 10, weight: .regular)
    static let nameField = Font.aurea(size: 17, weight: .regular)
    static let dimLabel = Font.aurea(size: 11, weight: .regular)
    static let dimField = Font.aurea(size: 16, weight: .regular)
    static let betaTitle = Font.aurea(size: 13, weight: .bold)
    static let betaBody = Font.aurea(size: 11, weight: .regular)
    static let tabLabel = Font.aurea(size: 10.5, weight: .medium)
    static let sheetAction = Font.aurea(size: 17, weight: .regular)
    static let sheetActionBold = Font.aurea(size: 17, weight: .semibold)
    static let dialogTitle = Font.aurea(size: 17, weight: .semibold)
    static let dialogBody = Font.aurea(size: 13, weight: .regular)
    static let dialogButton = Font.aurea(size: 16.8, weight: .semibold)
    static let dialogButtonPlain = Font.aurea(size: 16.8, weight: .regular)
    static let value = Font.aurea(size: 14, weight: .regular)
}

// =============================================================================
// As três abas
// =============================================================================
enum HomeTabKind: Int, CaseIterable, Identifiable {
    case start, projects, settings
    var id: Int { rawValue }

    var label: String {
        switch self {
        case .start: return AureaText.t("home_tab_start")
        case .projects: return AureaText.t("home_tab_projects")
        case .settings: return AureaText.t("home_tab_settings")
        }
    }
    var icon: Character {
        switch self {
        case .start: return CupertinoGlyph.House
        case .projects: return CupertinoGlyph.RectangleStack
        case .settings: return CupertinoGlyph.SliderHorizontal3
        }
    }
    /// O ícone cheio da aba acesa (`CupertinoGlyph.HouseFill`).
    var iconActive: Character {
        switch self {
        case .start: return CupertinoGlyph.HouseFill
        case .projects: return CupertinoGlyph.RectangleStack
        case .settings: return CupertinoGlyph.SliderHorizontal3
        }
    }
}

// =============================================================================
// A Home
// =============================================================================
struct HomeView: View {
    @EnvironmentObject private var model: AureaModel
    @StateObject private var library = HomeLibrary()
    @StateObject private var defaults = HomeDefaults()
    @StateObject private var backdrop = HomeBackdrop()

    @State private var tab: HomeTabKind = .start
    /// Busca aberta em cada aba (o Android tem uma bandeira por aba).
    @State private var startSearching = false
    @State private var projectsSearching = false
    /// Projetos marcados (a seleção em lote da aba Projetos).
    @State private var selection: Set<String> = []
    @State private var showNewProject = false
    @State private var settingsModalActive = false
    @State private var dialog: HomeProjectDialog?
    @State private var pendingProject: NewProjectDraft?
    #if DEBUG
    @State private var parityScrollConfigured = false
    #endif

    var body: some View {
        VStack(spacing: 0) {
            LiveNoticeBanners()
            ZStack(alignment: .bottom) {
                // As três abas ficam VIVAS: a rolagem de cada uma volta igual ao
                // trocar de aba ou ao voltar do editor (o mesmo que o
                // `HomeViewModel` guarda do lado do Android).
                ZStack {
                    HomeStartTab(library: library, defaults: defaults,
                                 searching: $startSearching, selection: $selection,
                                 onNewProject: { showNewProject = true },
                                 onDialog: { dialog = $0 },
                                 onOpenSettings: { select(.settings) }, onOpenProjects: { select(.projects) })
                        .opacity(tab == .start ? 1 : 0)
                        .allowsHitTesting(tab == .start)
                    HomeProjectsTab(library: library, defaults: defaults,
                                    searching: $projectsSearching, selection: $selection,
                                    onNewProject: { showNewProject = true },
                                    onDialog: { dialog = $0 })
                        .opacity(tab == .projects ? 1 : 0)
                        .allowsHitTesting(tab == .projects)
                    HomeSettingsTab(library: library, defaults: defaults, modalActive: $settingsModalActive)
                        .opacity(tab == .settings ? 1 : 0)
                        .allowsHitTesting(tab == .settings)
                }
                HomeTabBar(selected: tab, onSelect: select)
                    .zIndex(settingsModalActive ? -1 : 0)
            }
        }
        .background {
            GeometryReader { geometry in
                VStack(spacing: 0) {
                    AureaColors.systemBarVeil.frame(height: geometry.safeAreaInsets.top)
                    Spacer(minLength: 0)
                    AureaColors.navigationBar.frame(height: geometry.safeAreaInsets.bottom)
                }.frame(height: geometry.size.height + geometry.safeAreaInsets.top + geometry.safeAreaInsets.bottom)
                    .offset(y: -geometry.safeAreaInsets.top)
            }
        }
        .background(AureaColors.background.ignoresSafeArea())
        .environment(\.homeBackdrop, backdrop)
        .overlay {
            if showNewProject {
                NewProjectSheet(defaults: defaults,
                                suggestedName: AureaText.t("project_new", library.all.count + 1),
                                device: model.deviceReport,
                                onDismiss: { showNewProject = false }) { draft in
                    pendingProject = draft
                    showNewProject = false
                    createPendingProject()
                }
            }
        }
        .overlay {
            HomeProjectDialogs(state: $dialog, library: library, defaults: defaults,
                               selection: $selection, onOpen: { model.open($0.file) })
        }
        .task {
            // A ficha do projeto que acabou de sair do editor vem do MOTOR.
            syncCurrentProjectMeta()
            library.refresh()
        }
        .onChange(of: model.projects) { _ in library.refresh() }
        .onAppear { backdrop.select(tab); backdrop.start() }
        .onDisappear { backdrop.stop() }
        .onChange(of: tab) { backdrop.select($0) }
        .onChange(of: selection) { _ in backdrop.invalidate() }
        .onChange(of: defaults.query) { _ in backdrop.invalidate() }
        .onChange(of: defaults.sortIndex) { _ in backdrop.invalidate() }
        .onChange(of: model.language) { _ in backdrop.invalidate() }
        .onChange(of: model.screen) { screen in
            guard screen == .home else { return }
            syncCurrentProjectMeta()
            library.refresh()
        }
        .onChange(of: library.all) { all in
            // Some com marcas de projetos que deixaram de existir.
            let alive = Set(all.map(\.id))
            if !selection.isSubset(of: alive) { selection = selection.intersection(alive) }
            backdrop.invalidate()
            #if DEBUG
            if !parityScrollConfigured,
               ProcessInfo.processInfo.environment["AUREA_PARITY_SCENE"] == "home-scroll",
               all.filter({ $0.title.hasPrefix("Parity Glass") }).count >= 12 {
                parityScrollConfigured = true
                defaults.query = ""; defaults.sortIndex = 0
                selection = Set(all.prefix(2).map(\.id)); tab = .projects
            }
            #endif
        }
    }

    private func select(_ kind: HomeTabKind) {
        guard kind != tab else { return }
        UISelectionFeedbackGenerator().selectionChanged()
        tab = kind
    }

    /// A ficha (`.meta.json`) do projeto aberto, com os números que o MOTOR
    /// respondeu — é o que a Home lê para a linha "4:5 · Full HD 1080p · 30 fps".
    private func syncCurrentProjectMeta() {
        guard let url = model.projectURL else { return }
        let composition = model.composition
        writeHomeProjectMeta(url: url,
                             title: model.projectName.isEmpty
                                 ? url.deletingPathExtension().lastPathComponent : model.projectName,
                             width: Int((composition["width"] as? NSNumber)?.intValue ?? 0),
                             height: Int((composition["height"] as? NSNumber)?.intValue ?? 0),
                             fps: (composition["fps"] as? NSNumber)?.doubleValue ?? 30,
                             durationFrames: Int((composition["duration"] as? NSNumber)?.intValue ?? 0))
    }

    /// A folha devolve o projeto escolhido; a criação passa pelo núcleo.
    private func createPendingProject() {
        guard let draft = pendingProject else { return }
        pendingProject = nil
        let created = model.newProject(width: draft.width, height: draft.height,
                                       fps: draft.fps, title: draft.title)
        guard created, let url = model.projectURL else {
            // Antes daqui saia calado: o usuario tocava, nada acontecia, e nao
            // havia nem projeto nem explicacao.
            if (model.toast ?? "").isEmpty { model.toast = "não foi possível criar o projeto" }
            return
        }
        // A capa e a ficha nascem com o projeto (o Android grava as duas no
        // mesmo save) — sem elas o cartão ficaria na moldura do formato.
        _ = model.saveProject(writeThumbnail: true)
        writeHomeProjectMeta(url: url,
                             title: model.projectName,
                             width: Int(draft.width), height: Int(draft.height),
                             fps: draft.fps, durationFrames: Int(model.compositionDuration))
        library.refresh()
    }
}

/// HomeScreen.kt: hairline no topo, altura 54, sigma 24 e tinta de 72%.
private struct HomeTabBar: View {
    let selected: HomeTabKind
    let onSelect: (HomeTabKind) -> Void

    var body: some View {
        VStack(spacing: 0) {
            Rectangle().fill(AureaColors.hairline).frame(height: HomeDims.hairline)
            HStack(spacing: 0) {
                ForEach(HomeTabKind.allCases) { item in
                    Button { onSelect(item) } label: {
                        VStack(spacing: 3) {
                            CupertinoGlyph.text(selected == item ? item.iconActive : item.icon,
                                                size: HomeDims.iconLg,
                                                color: selected == item ? AureaColors.accent : AureaColors.muted)
                                .frame(width: HomeDims.iconLg, height: HomeDims.iconLg)
                                .accessibilityHidden(true)
                            Text(item.label)
                                .aureaFont(.tabLabel)
                                .lineLimit(1)
                        }
                        .foregroundStyle(selected == item ? AureaColors.accent : AureaColors.muted)
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                }
            }
            .frame(height: HomeDims.tabBar)
        }
        .background { HomeGlass(kind: .tab).allowsHitTesting(false) }
    }
}

#Preview {
    HomeView().environmentObject(AureaModel())
}

struct NewProjectDraft { let width: UInt32; let height: UInt32; let fps: Double; let title: String }

/// `home/NewProjectSheet.kt`: same format drawings, field order and segments.
/// This bottom sheet is drawn by the app so UIKit does not substitute its own
/// inset card, toolbar, detents or automatic close button.
struct NewProjectSheet: View {
    @ObservedObject var defaults: HomeDefaults
    let suggestedName: String
    let device: [String: NSNumber]
    let onDismiss: () -> Void
    let onCreate: (NewProjectDraft) -> Void
    @State private var aspectKey = "16:9"
    @State private var resolution = 1080
    @State private var fps = 30
    @State private var name = ""
    @State private var free = false
    @State private var freeWidth = "1080"
    @State private var freeHeight = "1350"
    @State private var submitting = false
    @State private var contentHeight: CGFloat = 650

    private var aspect: ProjectAspectOption { ProjectPresets.aspectByKey(aspectKey) }
    private var frame: (width: Int, height: Int) {
        if free {
            return (min(7680, max(64, Int(freeWidth) ?? 1080)),
                    min(7680, max(64, Int(freeHeight) ?? 1350)))
        }
        let ratio = Double(aspect.ratio)
        return ratio >= 1 ? (Int((Double(resolution) * ratio).rounded()), resolution)
            : (resolution, Int((Double(resolution) / ratio).rounded()))
    }
    private var previewRatio: CGFloat { free ? min(5, max(0.2, CGFloat(frame.width) / CGFloat(frame.height))) : aspect.ratio }

    var body: some View {
        GeometryReader { bounds in
            ZStack(alignment: .bottom) {
                Color.black.opacity(0.54).ignoresSafeArea().onTapGesture(perform: onDismiss)
                VStack(spacing: 0) {
                    Capsule().fill(Color.white.opacity(0.18))
                        .frame(width: 36, height: 5).padding(.top, 10).padding(.bottom, 6)
                    ScrollView {
                        sheetContent
                            .background(GeometryReader { proxy in
                                Color.clear.preference(key: NewProjectHeight.self, value: proxy.size.height)
                            })
                    }
                    .onPreferenceChange(NewProjectHeight.self) { contentHeight = $0 }
                }
                .frame(height: min(bounds.size.height - 12, contentHeight + 21))
                .frame(maxWidth: .infinity)
                .background(AureaColors.surface.ignoresSafeArea(edges: .bottom))
                .clipShape(UnevenHomeSheet())
            }
        }
        .foregroundStyle(AureaColors.text)
        .onAppear {
            aspectKey = defaults.aspect
            resolution = defaults.resolution
            fps = defaults.fps
        }
    }

    private var sheetContent: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(alignment: .bottom, spacing: 8) {
                Text(AureaText.t("new_project_title")).aureaFont(.titleLarge)
                    .frame(maxWidth: .infinity, alignment: .leading)
                Text("\(frame.width) × \(frame.height) · \(fps) fps")
                    .aureaFont(.sheetSpec).foregroundStyle(AureaColors.muted)
            }
            .padding(.bottom, 16)
            aspectPreview
            HStack(spacing: 0) {
                ForEach(ProjectPresets.aspects, id: \.key) { option in
                    formatOption(option, selected: !free && aspectKey == option.key) {
                        free = false; aspectKey = option.key
                    }
                }
                formatOption(ProjectPresets.free, selected: free) { free = true }
            }.padding(.top, 12)
            if free {
                HStack(alignment: .bottom, spacing: 10) {
                    dimensionField("new_project_width", text: $freeWidth)
                    Text("×").font(.aurea(size: 20)).foregroundStyle(AureaColors.muted).padding(.vertical, 10)
                    dimensionField("new_project_height", text: $freeHeight)
                }.padding(.top, 12)
            }
            HomeCapsLabel(text: AureaText.t("new_project_name")).padding(.top, 20)
            TextField("", text: $name,
                      prompt: Text(suggestedName.isEmpty ? AureaText.t("new_project_name_hint") : suggestedName)
                        .foregroundColor(AureaColors.muted))
                .aureaFont(.nameField).textInputAutocapitalization(.sentences)
                .submitLabel(.done).onSubmit(create)
                .padding(.horizontal, 14).padding(.vertical, 13)
                .background(AureaColors.surfaceHigh, in: RoundedRectangle(cornerRadius: 12))
                .padding(.top, 8)
            if !free {
                HomeCapsLabel(text: AureaText.t("settings_resolution")).padding(.top, 18)
                HomeSegmented(values: ProjectPresets.resolutions, selected: $resolution,
                              label: ProjectPresets.resolutionLabel,
                              background: AureaColors.surfaceHigh, thumb: AureaColors.background, verticalPadding: 7)
                    .padding(.top, 8)
            }
            if let exportLimit {
                Text(exportLimit).aureaFont(.note).foregroundStyle(AureaColors.muted).padding(.top, 8)
            }
            HomeCapsLabel(text: AureaText.t("settings_fps")).padding(.top, 18)
            HomeSegmented(values: ProjectPresets.fpsOptions, selected: $fps, label: { "\($0) fps" },
                          background: AureaColors.surfaceHigh, thumb: AureaColors.background, verticalPadding: 7)
                .padding(.top, 8)
            Button(action: create) {
                Text(AureaText.t("new_project_create")).aureaFont(.button)
                    .foregroundStyle(AureaColors.onAccent).frame(maxWidth: .infinity).frame(height: 52)
                    .background(AureaColors.accent, in: RoundedRectangle(cornerRadius: 14))
            }.buttonStyle(.plain).disabled(submitting).padding(.top, 24)
        }
        .padding(.horizontal, 20).padding(.top, 12).padding(.bottom, 16)
    }

    private var aspectPreview: some View {
        GeometryReader { bounds in
            let width = min(bounds.size.width, 150 * previewRatio)
            let height = width / previewRatio
            RoundedRectangle(cornerRadius: 12)
                .fill(LinearGradient(colors: [AureaColors.accent.opacity(0.22), AureaColors.accent.opacity(0.06)],
                                     startPoint: .topLeading, endPoint: .bottomTrailing))
                .overlay(RoundedRectangle(cornerRadius: 12).stroke(AureaColors.accent.opacity(0.6), lineWidth: 1.2))
                .overlay {
                    VStack(spacing: 0) {
                        Text(free ? "\(frame.width) × \(frame.height)" : aspect.label).aureaFont(.frameLabel)
                        Text(AureaText.t(free ? "aspect_free" : aspect.hint)).aureaFont(.frameHint)
                            .foregroundStyle(AureaColors.muted)
                    }.lineLimit(1).minimumScaleFactor(0.15).padding(.horizontal, 4)
                }
                .frame(width: width, height: height)
                .position(x: bounds.size.width / 2, y: 75)
        }
        .frame(height: 150)
        .animation(.timingCurve(0.33, 1, 0.68, 1, duration: 0.24), value: previewRatio)
    }

    private func formatOption(_ option: ProjectAspectOption, selected: Bool, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            VStack(spacing: 0) {
                RoundedRectangle(cornerRadius: 4)
                    .fill(selected ? AureaColors.accent.opacity(0.2) : .clear)
                    .overlay(RoundedRectangle(cornerRadius: 4).stroke(selected ? AureaColors.accent : AureaColors.muted,
                                                                                    lineWidth: selected ? 2 : 1.2))
                    .frame(width: option.ratio >= 1 ? 28 : 28 * option.ratio,
                           height: option.ratio >= 1 ? 28 / option.ratio : 28)
                    .frame(height: 28)
                Text(option.label).aureaFont(.formatLabel)
                    .foregroundStyle(selected ? AureaColors.accent : AureaColors.text).padding(.top, 7)
                Text(AureaText.t(option.hint)).aureaFont(.formatHint).foregroundStyle(AureaColors.muted)
                    .lineLimit(1).truncationMode(.tail)
            }.frame(maxWidth: .infinity).padding(.vertical, 6).contentShape(Rectangle())
        }.buttonStyle(.plain).animation(.easeOut(duration: 0.16), value: selected)
            .accessibilityLabel(AureaText.t("sh_aspect_ratio_desc", option.label))
            .accessibilityAddTraits(selected ? .isSelected : [])
    }

    private func dimensionField(_ key: String, text: Binding<String>) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(AureaText.t(key)).aureaFont(.dimLabel).foregroundStyle(AureaColors.muted)
            TextField("", text: Binding(get: { text.wrappedValue }, set: {
                text.wrappedValue = String($0.filter(\.isNumber).prefix(5))
            })).aureaFont(.dimField).keyboardType(.numberPad)
                .padding(.horizontal, 12).padding(.vertical, 10)
                .background(AureaColors.surfaceHigh, in: RoundedRectangle(cornerRadius: 10))
        }
    }

    private var exportLimit: String? {
        let short = device["maxExportHeight"]?.intValue ?? 0
        let long = device["maxExportWidth"]?.intValue ?? 0
        guard short > 0, min(frame.width, frame.height) > short || (long > 0 && max(frame.width, frame.height) > long) else { return nil }
        // Android NewProjectSheet's same informational note. Editing remains available.
        return "Este aparelho exporta até \(short == 2160 ? "4K" : "\(short)p"). Dá para editar em \(frame.width) × \(frame.height); a exportação sai em no máximo \(short == 2160 ? "4K" : "\(short)p")."
    }

    private func create() {
        guard !submitting else { return }
        submitting = true
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        let title = trimmed.isEmpty ? (suggestedName.isEmpty ? AureaText.t("new_project_untitled") : suggestedName) : trimmed
        onCreate(NewProjectDraft(width: UInt32(frame.width), height: UInt32(frame.height), fps: Double(fps), title: title))
    }
}

private struct NewProjectHeight: PreferenceKey {
    static var defaultValue: CGFloat = 650
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) { value = nextValue() }
}

/// iOS 16-compatible top corners (Android ModalBottomSheet radius 20).
private struct UnevenHomeSheet: Shape {
    func path(in rect: CGRect) -> Path {
        Path(UIBezierPath(roundedRect: rect, byRoundingCorners: [.topLeft, .topRight],
                          cornerRadii: CGSize(width: 20, height: 20)).cgPath)
    }
}

/// `home/HomeComponents.kt::AureaSegmented`. Text, tracks, separators and
/// moving thumb retain Android's dimensions instead of the OS Picker style.
private struct HomeSegmented<Value: Hashable>: View {
    let values: [Value]
    @Binding var selected: Value
    let label: (Value) -> String
    var background = AureaColors.background
    var thumb = AureaColors.surfaceHigh
    var verticalPadding: CGFloat = 6
    private var index: Int { values.firstIndex(of: selected) ?? 0 }
    var body: some View {
        HStack(spacing: 0) {
            ForEach(values, id: \.self) { value in
                Button { selected = value } label: {
                    Text(label(value)).aureaFont(.segment)
                        .foregroundStyle(selected == value ? AureaColors.accent : AureaColors.text)
                        .multilineTextAlignment(.center).lineLimit(2)
                        .frame(maxWidth: .infinity).padding(.vertical, verticalPadding)
                        .contentShape(Rectangle())
                }.buttonStyle(.plain)
                    .accessibilityAddTraits(selected == value ? .isSelected : [])
            }
        }
        .background {
            GeometryReader { bounds in
                let width = bounds.size.width / CGFloat(max(1, values.count))
                ZStack(alignment: .leading) {
                    ForEach(1..<max(1, values.count), id: \.self) { separator in
                        if separator != index && separator != index + 1 {
                            Rectangle().fill(Color.white.opacity(0.12)).frame(width: 0.5)
                                .padding(.vertical, 5).offset(x: width * CGFloat(separator))
                        }
                    }
                    RoundedRectangle(cornerRadius: 7).fill(thumb)
                        .frame(width: width).offset(x: width * CGFloat(index))
                        .animation(.interpolatingSpring(stiffness: 500, damping: 45), value: index)
                }
            }
        }
        .padding(.horizontal, 3).padding(.vertical, 2)
        .frame(minHeight: 28).background(background, in: RoundedRectangle(cornerRadius: 9))
    }
}

/// `home/SettingsTab.kt`: same groups, row order and real persistent actions.
struct HomeSettingsTab: View {
    @ObservedObject var library: HomeLibrary
    @ObservedObject var defaults: HomeDefaults
    @EnvironmentObject private var model: AureaModel
    @Binding var modalActive: Bool
    @State private var languageSheet = false
    @State private var keyDialog = false
    @State private var groqKey = ""
    @State private var hasGroqKey = false
    @State private var betaTaps = 0
    @State private var licenses = false
    @AppStorage("home.developerTools") private var developerTools = false

    private var version: String { Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "?" }
    private var build: String { Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "?" }

    var body: some View {
        ScrollView {
            LazyVStack(alignment: .leading, spacing: 0) {
                Text(AureaText.t("home_title_settings")).aureaFont(.headlineLarge).padding(.bottom, HomeDims.s5)
                groupHeader("settings_group_defaults")
                group {
                    segmentedRow("settings_aspect", values: ProjectPresets.aspects.map(\.key), selected: $defaults.aspect, label: { $0 })
                    groupDivider
                    segmentedRow("settings_resolution", values: ProjectPresets.resolutions, selected: $defaults.resolution,
                                 label: { $0 == 2160 ? "4K" : "\($0)p" })
                    groupDivider
                    segmentedRow("settings_fps", values: ProjectPresets.fpsOptions, selected: $defaults.fps, label: { "\($0)" })
                }
                groupNote("settings_defaults_note")
                groupHeader("settings_group_language", top: true)
                group {
                    tapRow("settings_language", subtitle: model.language.label) { languageSheet = true }
                }
                groupNote("settings_language_note")
                groupHeader("settings_group_captions", top: true)
                group {
                    tapRow("settings_groq_key", subtitle: AureaText.t(hasGroqKey ? "settings_groq_set" : "settings_groq_unset")) {
                        groqKey = ""; hasGroqKey = !CaptionKeychain.read().isEmpty; keyDialog = true
                    }
                    groupNote("settings_groq_note")
                }
                groupHeader("settings_group_device", top: true)
                group {
                    tapRow("settings_device_auto", subtitle: deviceSummary) { model.analyseDeviceAgain() }
                }
                groupNote("settings_device_auto_note")
                groupHeader("settings_group_general", top: true)
                group {
                    tapRow("settings_clear_cache", subtitle: AureaText.t("settings_clear_cache_note")) { model.clearHomeCaches() }
                    groupDivider
                    tapRow("settings_clear_recents", subtitle: AureaText.t("settings_clear_recents_note")) {
                        FxEffectPrefs().clearRecents()
                        model.toast = AureaText.t("settings_recents_cleared")
                    }
                }
                DonationCard()
                groupHeader("settings_group_about", top: true)
                group {
                    tileRow(CupertinoGlyph.Bolt, title: "settings_technology", subtitle: AureaText.t("settings_technology_ios_value"))
                    groupDivider
                    tileRow(CupertinoGlyph.PersonCropCircle, title: "settings_creator",
                            subtitle: AureaText.t("home_ruanzitwo_ofruanzitwo_tiktok_ruanzitwo"))
                    groupDivider
                    tapRow("licenses_title", subtitle: "Real-ESRGAN · Tencent/ncnn") { licenses = true }
                }
                betaBanner.padding(.top, HomeDims.s4)
                if developerTools {
                    group {
                        tileRow(CupertinoGlyph.Wrench, title: "settings_dev_tools", subtitle: "Versão \(version) · build \(build)")
                    }.padding(.top, HomeDims.s4)
                }
                Text(AureaText.t("settings_made_by")).aureaFont(.footer).foregroundStyle(AureaColors.subtle)
                    .frame(maxWidth: .infinity).padding(.top, HomeDims.s5)
            }
            .padding(.horizontal, HomeDims.gutter).padding(.top, HomeDims.s3).padding(.bottom, HomeDims.listEndSpace)
            .background { HomeBackdropSource(tab: .settings).allowsHitTesting(false) }
        }
        .foregroundStyle(AureaColors.text)
        .overlay {
            if languageSheet {
                HomeActionSheet(title: AureaText.t("settings_language"), actions: AureaLanguage.allCases.map { language in
                    HomeSheetAction((language == model.language ? "✓  " : "") + language.label) { model.language = language }
                }, onDismiss: { languageSheet = false })
            }
            if keyDialog { groqDialog }
        }
        .onAppear { hasGroqKey = !CaptionKeychain.read().isEmpty }
        .sheet(isPresented: $licenses) {
            NavigationStack {
                ScrollView { Text(AureaText.t("licenses_ai_body")).font(.system(size: 13)).textSelection(.enabled).padding(20) }
                    .navigationTitle(AureaText.t("licenses_title")).navigationBarTitleDisplayMode(.inline)
                    .toolbar { ToolbarItem(placement: .confirmationAction) {
                        Button(AureaText.t("editor_fechar")) { licenses = false }
                    } }
            }.preferredColorScheme(.dark)
        }
        .onChange(of: languageSheet) { _ in modalActive = languageSheet || keyDialog }
        .onChange(of: keyDialog) { _ in modalActive = languageSheet || keyDialog }
    }

    private func groupHeader(_ key: String, top: Bool = false) -> some View {
        HomeCapsLabel(text: AureaText.t(key)).padding(.leading, 16).padding(.bottom, 8).padding(.top, top ? 24 : 0)
    }
    private func groupNote(_ key: String) -> some View {
        Text(AureaText.t(key)).aureaFont(.note).foregroundStyle(AureaColors.muted)
            .frame(maxWidth: .infinity, alignment: .leading).padding(.horizontal, 16).padding(.bottom, 14)
    }
    private func group<Content: View>(@ViewBuilder _ content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 0, content: content).frame(maxWidth: .infinity)
            .background(AureaColors.surface, in: RoundedRectangle(cornerRadius: 16))
            .clipShape(RoundedRectangle(cornerRadius: 16))
    }
    private var groupDivider: some View {
        Rectangle().fill(AureaColors.hairline).frame(height: 0.5).padding(.leading, 16)
    }
    private func segmentedRow<Value: Hashable>(_ key: String, values: [Value], selected: Binding<Value>, label: @escaping (Value) -> String) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(AureaText.t(key)).aureaFont(.bodyLarge)
            HomeSegmented(values: values, selected: selected, label: label)
        }.padding(.horizontal, 16).padding(.vertical, 14)
    }
    private func tapRow(_ title: String, subtitle: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 0) {
                VStack(alignment: .leading, spacing: 1) {
                    Text(AureaText.t(title)).aureaFont(.bodyLarge)
                    Text(subtitle).aureaFont(.bodySmall).foregroundStyle(AureaColors.muted)
                }.frame(maxWidth: .infinity, alignment: .leading)
                CupertinoGlyph.text(CupertinoGlyph.ChevronRight, size: 16, color: AureaColors.muted)
            }.padding(.horizontal, 16).padding(.vertical, 12).contentShape(Rectangle())
        }.buttonStyle(.plain)
    }
    private func tileRow(_ glyph: Character, title: String, subtitle: String) -> some View {
        HStack(spacing: 16) {
            CupertinoGlyph.text(glyph, size: 21, color: AureaColors.accent).frame(width: 24, alignment: .leading)
            VStack(alignment: .leading, spacing: 0) {
                Text(AureaText.t(title)).aureaFont(.bodyLarge)
                Text(subtitle).aureaFont(.bodySmall).foregroundStyle(AureaColors.muted)
            }.frame(maxWidth: .infinity, alignment: .leading)
        }.padding(.leading, 16).padding(.trailing, 24).padding(.vertical, 8).frame(minHeight: 72)
    }
    private var betaBanner: some View {
        Button {
            betaTaps += 1
            if betaTaps >= 7 {
                betaTaps = 0; developerTools.toggle()
                model.toast = AureaText.t(developerTools ? "settings_dev_on" : "settings_dev_off")
            }
        } label: {
            HStack(spacing: 10) {
                CupertinoGlyph.text(CupertinoGlyph.ExclamationmarkTriangle, size: 20, color: HomeColors.beta)
                VStack(alignment: .leading, spacing: 3) {
                    Text(AureaText.t("settings_beta_title", version)).aureaFont(.betaTitle)
                    Text(AureaText.t("settings_beta_body")).aureaFont(.betaBody)
                }.frame(maxWidth: .infinity, alignment: .leading)
                CupertinoGlyph.text(CupertinoGlyph.ChevronRight, size: 14, color: HomeColors.beta)
            }.foregroundStyle(HomeColors.beta).padding(14)
                .background(HomeColors.betaFill, in: RoundedRectangle(cornerRadius: 14))
                .overlay(RoundedRectangle(cornerRadius: 14).stroke(HomeColors.betaBorder, lineWidth: 1))
        }.buttonStyle(.plain)
    }
    private var deviceSummary: String {
        let report = model.deviceReport
        guard let cores = report["totalCores"]?.intValue, cores > 0 else { return AureaText.t("settings_device_auto") }
        let performance = report["performanceCores"]?.intValue ?? 0
        let efficiency = report["efficiencyCores"]?.intValue ?? 0
        let ram = Double(report["totalMemoryMb"]?.intValue ?? 0) / 1024
        let budget = Double(report["budgetMb"]?.intValue ?? 0) / 1024
        let mhz = report["maxFrequencyMhz"]?.intValue ?? 0
        let topology = efficiency > 0 ? " (\(performance)+\(efficiency))" : ""
        let clock = mhz > 0 ? " até \(String(format: "%.1f GHz", Double(mhz) / 1000))" : ""
        return "\(cores) núcleos\(topology)\(clock) · \(String(format: "%.1f GB", ram)) · orçamento \(String(format: "%.1f GB", budget))"
    }
    private var groqDialog: some View {
        ZStack {
            Color.black.opacity(0.54).ignoresSafeArea().onTapGesture { keyDialog = false }
            VStack(alignment: .leading, spacing: 16) {
                Text(AureaText.t("settings_groq_key")).aureaFont(.titleLarge)
                Text(AureaText.t("settings_groq_note")).aureaFont(.note).foregroundStyle(AureaColors.muted)
                SecureField("gsk_…", text: $groqKey).aureaFont(.bodyLarge)
                    .textInputAutocapitalization(.never).autocorrectionDisabled()
                    .padding(14).overlay(RoundedRectangle(cornerRadius: 4).stroke(AureaColors.muted, lineWidth: 1))
                HStack(spacing: 16) {
                    if hasGroqKey {
                        Button(AureaText.t("common_remove")) { saveGroqKey("") }
                    }
                    Spacer(minLength: 0)
                    Button(AureaText.t("common_cancel")) { keyDialog = false }
                    Button(AureaText.t("common_save")) { saveGroqKey(groqKey) }
                        .disabled(groqKey.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                }.font(.aurea(size: 14, weight: .medium)).foregroundStyle(AureaColors.accent)
            }.padding(24).frame(maxWidth: 400).background(AureaColors.surfaceHigh, in: RoundedRectangle(cornerRadius: 28)).padding(24)
        }
    }
    private func saveGroqKey(_ value: String) {
        if CaptionKeychain.save(value) { hasGroqKey = !value.isEmpty; groqKey = ""; keyDialog = false }
        else { model.toast = "Não foi possível salvar a chave neste aparelho." }
    }
}

// State shared by the Home tabs, matching HomeViewModel preference keys.
@MainActor final class HomeDefaults: ObservableObject {
    @Published var query = ""
    @Published var sortIndex = UserDefaults.standard.integer(forKey: "home.sort") {
        didSet { UserDefaults.standard.set(sortIndex, forKey: "home.sort") }
    }
    @Published var aspect = UserDefaults.standard.string(forKey: "home.aspect") ?? "16:9" {
        didSet { UserDefaults.standard.set(aspect, forKey: "home.aspect") }
    }
    @Published var resolution = (UserDefaults.standard.object(forKey: "home.resolution") as? Int) ?? 1080 {
        didSet { UserDefaults.standard.set(resolution, forKey: "home.resolution") }
    }
    @Published var fps = (UserDefaults.standard.object(forKey: "home.fps") as? Int) ?? 30 {
        didSet { UserDefaults.standard.set(fps, forKey: "home.fps") }
    }
    var sort: HomeSortOrder { HomeSortOrder(rawValue: sortIndex) ?? .recent }
}

struct HomeStartTab: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject var library: HomeLibrary
    @ObservedObject var defaults: HomeDefaults
    @Binding var searching: Bool
    @Binding var selection: Set<String>
    let onNewProject: () -> Void
    let onDialog: (HomeProjectDialog) -> Void
    let onOpenSettings: () -> Void
    var onOpenProjects: () -> Void = {}
    @State private var importing = false
    private var arranged: [HomeProjectEntry] { arrangeHomeProjects(library.all, sort: defaults.sort, query: defaults.query) }
    private var filtering: Bool { !defaults.query.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty || defaults.sort != .recent }
    private var hero: HomeProjectEntry? { filtering ? nil : arranged.first }
    private var visible: [HomeProjectEntry] {
        let list = arranged.filter { $0.id != hero?.id }
        return filtering ? list : Array(list.prefix(homeRecentOnStart))
    }
    private var greeting: String {
        let hour = Calendar.current.component(.hour, from: Date())
        return AureaText.t((5...11).contains(hour) ? "greeting_morning" : (12...17).contains(hour) ? "greeting_afternoon" : "greeting_evening")
    }
    var body: some View {
        ScrollView {
            LazyVStack(alignment: .leading, spacing: 0) {
                HStack(spacing: 0) {
                    HomeBrandLogo().frame(width: 30, height: 30)
                    VStack(alignment: .leading, spacing: 2) {
                        Text("AUREA").aureaFont(.titleLarge)
                        Text(greeting).aureaFont(.greeting).foregroundStyle(AureaColors.muted).lineLimit(1)
                    }.frame(maxWidth: .infinity, alignment: .leading).padding(.leading, HomeDims.s3)
                    HomeRoundIconButton(glyph: CupertinoGlyph.Search, description: AureaText.t("home_search_projects")) { searching.toggle() }
                    HomeRoundIconButton(glyph: CupertinoGlyph.SliderHorizontal3, description: AureaText.t("home_title_settings"), action: onOpenSettings)
                        .padding(.leading, 2)
                }.padding(.leading, HomeDims.gutter).padding(.trailing, HomeDims.s3).padding(.top, 14).padding(.bottom, HomeDims.s3)
                if searching || filtering {
                    HomeProjectListBar(count: arranged.count, selecting: false, selectedCount: 0, searching: true,
                                       query: $defaults.query, onOpenSearch: {},
                                       onClearSearch: { defaults.query = ""; searching = false }, onSort: { onDialog(.sort) },
                                       onSelectAll: {}, onExitSelection: {})
                        .padding(.leading, HomeDims.gutter).padding(.trailing, HomeDims.s3).padding(.top, HomeDims.s1).padding(.bottom, HomeDims.s3)
                }
                VStack(spacing: HomeDims.s2) {
                    HStack(spacing: HomeDims.s3) {
                        HomeStudioAction(glyph: CupertinoGlyph.Plus, label: AureaText.t("home_new_project"), primary: true, action: onNewProject)
                        HomeStudioAction(glyph: CupertinoGlyph.PhotoOnRectangle, label: AureaText.t("home_import_media")) { importing = true }
                    }
                    HStack(spacing: HomeDims.s3) {
                        HomeQuickAction(glyph: CupertinoGlyph.RectangleStack, label: AureaText.plural("home_project_count", library.all.count), action: onOpenProjects)
                        HomeQuickAction(glyph: CupertinoGlyph.ArrowUpArrowDown, label: AureaText.t("home_sort_title")) { onDialog(.sort) }
                    }
                }.padding(.horizontal, HomeDims.gutter).padding(.top, HomeDims.s1)
                if let hero, !searching {
                    HomeContinueCard(entry: hero, onOpen: { model.open(hero.file) }, onMenu: { onDialog(.menu(hero)) })
                }
                if !arranged.isEmpty {
                    HomeSectionHeader(text: filtering ? AureaText.t(arranged.count == 1 ? "home_project_count" : "home_project_count_plural", arranged.count) : AureaText.t("home_sort_recent"),
                                      actionLabel: !filtering && library.all.count > visible.count + (hero == nil ? 0 : 1) ? AureaText.t("home_ver_todos") : nil,
                                      onAction: onOpenProjects)
                }
                if library.loaded && arranged.isEmpty {
                    HomeProjectsEmptyState(message: AureaText.t(defaults.query.isEmpty ? "home_empty_hint" : "home_no_results"))
                }
                HomeProjectGrid(entries: visible, selection: .constant([]), selectable: false, onDialog: onDialog)
            }.padding(.bottom, HomeDims.listEndSpace)
                .background { HomeBackdropSource(tab: .start).allowsHitTesting(false) }
        }.foregroundStyle(AureaColors.text)
            .sheet(isPresented: $importing) {
                HomeMediaPicker(onDismiss: { importing = false }, onPick: { url, kind in
                    importing = false
                    model.createFromMedia(url: url, kind: kind)
                }, onError: { error in importing = false; model.toast = error })
            }
    }
}

struct HomeProjectsTab: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject var library: HomeLibrary
    @ObservedObject var defaults: HomeDefaults
    @Binding var searching: Bool
    @Binding var selection: Set<String>
    let onNewProject: () -> Void
    let onDialog: (HomeProjectDialog) -> Void
    private var arranged: [HomeProjectEntry] { arrangeHomeProjects(library.all, sort: defaults.sort, query: defaults.query) }
    var body: some View {
        ZStack(alignment: .bottom) {
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 0) {
                    Text(AureaText.t("home_title_projects")).aureaFont(.headlineLarge)
                        .padding(.horizontal, HomeDims.gutter).padding(.top, HomeDims.s5)
                    HomeProjectListBar(count: arranged.count, selecting: !selection.isEmpty,
                                       selectedCount: selection.count, searching: searching || !defaults.query.isEmpty,
                                       query: $defaults.query, onOpenSearch: { searching = true },
                                       onClearSearch: { defaults.query = ""; searching = false },
                                       onSort: { onDialog(.sort) }, onSelectAll: { selection = Set(arranged.map(\.id)) },
                                       onExitSelection: { selection = [] })
                    if library.loaded && arranged.isEmpty {
                        HomeProjectsEmptyState(message: AureaText.t(defaults.query.isEmpty ? "home_empty_hint" : "home_no_results"))
                    }
                    HomeProjectGrid(entries: arranged, selection: $selection, selectable: true, onDialog: onDialog)
                }.padding(.bottom, HomeDims.listEndSpace)
                    .background { HomeBackdropSource(tab: .projects).allowsHitTesting(false) }
            }
            if !selection.isEmpty {
                HomeBatchBar(count: selection.count, onDuplicate: {
                    for entry in library.all where selection.contains(entry.id) { _ = duplicateHomeProject(entry) }
                    selection = []; library.refresh()
                }, onDelete: { onDialog(.batchDelete(selection)) }).padding(.bottom, HomeDims.tabBar)
            }
        }.foregroundStyle(AureaColors.text)
    }
}

private struct HomeProjectGrid: View {
    @EnvironmentObject private var model: AureaModel
    let entries: [HomeProjectEntry]
    @Binding var selection: Set<String>
    let selectable: Bool
    let onDialog: (HomeProjectDialog) -> Void
    var body: some View {
        LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: HomeDims.s3), count: homeGridColumns()), spacing: HomeDims.s4) {
            ForEach(entries) { entry in
                HomeProjectCard(entry: entry, selecting: selectable && !selection.isEmpty,
                                marked: selection.contains(entry.id), onOpen: { model.open(entry.file) },
                                onMenu: { onDialog(.menu(entry)) }, onMark: {
                    guard selectable else { return }
                    if selection.contains(entry.id) { selection.remove(entry.id) } else { selection.insert(entry.id) }
                })
            }
        }.padding(.horizontal, HomeDims.gutter)
    }
}


/// Platform media picker corresponding to Android PickVisualMedia. Providers
/// hand out temporary files: copy while their callback is alive, then let the
/// existing core import path inspect and retain the media in the project.
private struct HomeMediaPicker: UIViewControllerRepresentable {
    let onDismiss: () -> Void
    let onPick: (URL, AureaModel.ImportKind) -> Void
    let onError: (String) -> Void
    func makeCoordinator() -> Coordinator { Coordinator(self) }
    func makeUIViewController(context: Context) -> PHPickerViewController {
        var configuration = PHPickerConfiguration()
        configuration.filter = .any(of: [.images, .videos])
        configuration.selectionLimit = 1
        configuration.preferredAssetRepresentationMode = .current
        let picker = PHPickerViewController(configuration: configuration)
        picker.delegate = context.coordinator
        return picker
    }
    func updateUIViewController(_ controller: PHPickerViewController, context: Context) {}
    final class Coordinator: NSObject, PHPickerViewControllerDelegate {
        let parent: HomeMediaPicker
        init(_ parent: HomeMediaPicker) { self.parent = parent }
        func picker(_ picker: PHPickerViewController, didFinishPicking results: [PHPickerResult]) {
            guard let provider = results.first?.itemProvider else { parent.onDismiss(); return }
            let video = provider.hasItemConformingToTypeIdentifier(UTType.movie.identifier)
            let kind: AureaModel.ImportKind = video ? .video : .image
            let target: UTType = video ? .movie : .image
            let identifier = provider.registeredTypeIdentifiers.first { UTType($0)?.conforms(to: target) == true } ?? target.identifier
            provider.loadFileRepresentation(forTypeIdentifier: identifier) { [parent] url, error in
                guard let url else {
                    DispatchQueue.main.async { parent.onError(error?.localizedDescription ?? "Não foi possível abrir a mídia escolhida.") }
                    return
                }
                do {
                    let directory = FileManager.default.temporaryDirectory.appendingPathComponent("HomeImports", isDirectory: true)
                        .appendingPathComponent(UUID().uuidString, isDirectory: true)
                    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
                    let copy = directory.appendingPathComponent(url.lastPathComponent)
                    try FileManager.default.copyItem(at: url, to: copy)
                    DispatchQueue.main.async { parent.onPick(copy, kind) }
                } catch {
                    DispatchQueue.main.async { parent.onError(error.localizedDescription) }
                }
            }
        }
    }
}
