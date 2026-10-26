// =============================================================================
//  Aurea / platform / ios / app / HomeView.swift
//
//  A HOME (Omega 0.3.2, área 01; par do `home/HomeScreen.kt`): barra superior
//  de 57 pt (☰ · AUREA · ✦ · avatar), as abas Início (Comunidade) · Perfil ·
//  [+] · Projetos · Presets no dock de 72 pt e a gaveta de ajustes. Escolhendo
//  projetos, o cabeçalho de seleção cobre a barra superior e a barra de seleção
//  cobre o dock. Clara no Omega (D6): pinta só com os papéis claros e os da Home.
//
//  O que é de quem:
//    HomeView.swift         a casca, as abas, a gaveta, a folha "Novo projeto",
//                           o Sobre e as Novidades
//    ProjectCards.swift     a linha do projeto, a ficha, as pílulas, o menu
//                           rápido, os diálogos de um projeto e a biblioteca
//    CommunityView.swift    o conteúdo da Início e do Perfil
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

/// Doação (opcional, aberta pela pessoa). Omega home: fora do editor é claro.
struct DonationCard: View {
    var exporting = false
    @Environment(\.openURL) private var openURL
    @State private var showing = false
    @State private var copied = false

    var body: some View {
        Button { copied = false; showing = true } label: {
            HStack(spacing: 12) {
                VStack(alignment: .leading, spacing: 2) {
                    Text(AureaText.t("donation_title")).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.lightText)
                    Text(AureaText.t("donation_note")).font(.aurea(size: 11)).foregroundStyle(AureaColors.lightTextMuted)
                }.frame(maxWidth: .infinity, alignment: .leading)
                Image(systemName: "heart").font(.system(size: 24)).foregroundStyle(AureaColors.lightAccentInk)
            }.padding(.horizontal, 15).padding(.vertical, 12)
                .background(AureaColors.lightSurfaceAlt, in: RoundedRectangle(cornerRadius: 6))
        }.buttonStyle(.plain)
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
                            Text("Pix · Ruan Pablo").foregroundStyle(AureaColors.lightTextMuted)
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
                            if exporting { Text(AureaText.t("donation_export_note")).foregroundStyle(AureaColors.lightTextMuted) }
                            if copied { Text(AureaText.t("donation_copied")).foregroundStyle(AureaColors.lightAccentInk) }
                        }.font(.aurea(size: 15)).padding(24)
                    }.background(AureaColors.lightDialog).foregroundStyle(AureaColors.lightText).tint(AureaColors.lightAccentInk)
                        .navigationTitle(AureaText.t("donation_title")).navigationBarTitleDisplayMode(.inline)
                        .toolbar { ToolbarItem(placement: .confirmationAction) {
                            Button(AureaText.t("editor_fechar")) { showing = false }
                        } }
                }.preferredColorScheme(AureaColors.lightIsLight ? .light : .dark)
            }
    }
    private func copy(_ value: String) { UIPasteboard.general.string = value; copied = true }
}

// =============================================================================
// Tokens da Home. Os que já existem no Theme.swift vêm de lá; estes são os que
// a casca da Home usa e ainda não estavam — MESMOS valores de `AureaTokens.kt`.
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
// As abas (o rawValue é o MESMO índice do Android: Início 0, Projetos 1,
// Presets 2, Perfil 3 — é o que fica gravado em "aurea.home.tab").
// =============================================================================
enum HomeTabKind: Int, CaseIterable, Identifiable {
    case start, projects, presets, profile
    var id: Int { rawValue }
}

/// O pedido de menu rápido (projeto ou preset), desenhado na raiz da Home.
struct OmegaQuickRequest {
    let row: CGRect
    let areaBottom: CGFloat
    let tag: String
    let options: [OmegaQuickOption]
    let rowCopy: AnyView
}

/// Os perfis do criador (os mesmos da tela de boas-vindas do AureaApp.swift).
let omegaCreatorProfiles: [(label: String, url: String, tag: String, glyph: Character)] = [
    ("TikTok @ruanzitwo", "https://www.tiktok.com/@ruanzitwo", "home.drawer.follow.tiktok", CupertinoGlyph.MusicNote),
    ("Instagram @ofruanzitwo", "https://www.instagram.com/ofruanzitwo/", "home.drawer.follow.instagram", CupertinoGlyph.Camera),
]

// =============================================================================
// A Home
// =============================================================================
struct HomeView: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var conta: ContaModel
    @StateObject private var library = HomeLibrary()
    @StateObject private var defaults = HomeDefaults()
    @Environment(\.layoutDirection) private var direction

    /// P1-8 (D5): primeira abertura em Início; depois, a última aba usada.
    @AppStorage("aurea.home.tab") private var savedTab = HomeTabKind.start.rawValue
    @State private var drawerOpen = false
    @State private var drawerDrag: CGFloat = 0
    @State private var accountMenu = false
    @State private var showAccount = false
    @State private var confirmLogout = false
    @State private var confirmDelete = false
    @State private var showAbout = false
    @State private var importing = false
    /// "Importar arquivo do projeto" e "Relatar um problema" (ProjectTransfer.swift).
    @State private var importingProjectFile = false
    @State private var reportingProblem = false
    @State private var projectsSearching = false
    /// Projetos marcados (o modo de seleção da aba Projetos).
    @State private var selection: Set<String> = []
    @State private var showNewProject = false
    @AppStorage("aurea.releaseNotes.read") private var readReleaseNotes = ""
    @State private var showReleaseNotes = false
    @State private var showDonationPrompt = false
    @State private var dialog: HomeProjectDialog?
    @State private var quick: OmegaQuickRequest?
    @State private var pendingProject: NewProjectDraft?
    private var releaseNotesEdition: String {
        "\(Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "unknown"):\(Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "unknown")"
    }
    private var tab: HomeTabKind { HomeTabKind(rawValue: savedTab) ?? .start }
    private var selecting: Bool { !selection.isEmpty && tab == .projects }
    private var drawerWidth: CGFloat { min(OmegaHomeDims.drawer, UIScreen.main.bounds.width * 0.85) }

    var body: some View {
        ZStack(alignment: .leading) {
            main
            drawerLayer
        }
        .background(AureaColors.lightBackground.ignoresSafeArea())
        .overlay { quickLayer }
        .overlay {
            if showNewProject {
                NewProjectSheet(defaults: defaults,
                                suggestedName: omegaSuggestedName(library.all.map(\.title)) { AureaText.t("project_new", $0) },
                                device: model.deviceReport,
                                onDismiss: { showNewProject = false }) { draft in
                    pendingProject = draft
                    showNewProject = false
                    createPendingProject()
                }
            }
        }
        .overlay { HomeProjectDialogs(state: $dialog, library: library, selection: $selection) }
        .overlay { accountDialogs }
        .sheet(isPresented: $showAccount) {
            NavigationStack {
                ContaView().toolbar { ToolbarItem(placement: .confirmationAction) {
                    Button(AureaText.t("editor_fechar")) { showAccount = false }
                        .accessibilityIdentifier("conta.fechar")
                } }
            }
        }
        .onChange(of: conta.logado) { if $0 { showAccount = false } }
        // O .aureaproj não tem tipo registrado: qualquer arquivo; o motor diz se é projeto.
        .fileImporter(isPresented: $importingProjectFile, allowedContentTypes: [.data, .item]) { result in
            guard case .success(let url) = result else { return }
            model.importProjectFile(url) { library.refresh() }
        }
        .sheet(isPresented: $reportingProblem) {
            ReportProblemView(sessao: conta.sessao()) { model.toast = $0 }
        }
        .sheet(isPresented: $importing) {
            HomeMediaPicker(onDismiss: { importing = false }, onPick: { [project = model.projectGeneration] url, kind in
                guard project == model.projectGeneration else { importing = false; return }
                importing = false; model.createFromMedia(url: url, kind: kind)
            }, onError: { error in importing = false; model.toast = error })
        }
        .fullScreenCover(isPresented: $showAbout) { HomeAboutView { showAbout = false } }
        .fullScreenCover(isPresented: $showReleaseNotes, onDismiss: {
            readReleaseNotes = releaseNotesEdition
            if model.donationPromptPending { showDonationPrompt = true }
        }) {
            OmegaReleaseNotes { showReleaseNotes = false }
        }
        .task {
            // A ficha do projeto que acabou de sair do editor vem do MOTOR.
            syncCurrentProjectMeta()
            library.refresh()
        }
        .onChange(of: model.projects) { _ in library.refresh() }
        .onAppear {
            #if DEBUG
            // Slow video fixtures yield to Home while preparing their real export.
            if ProcessInfo.processInfo.environment["AUREA_UI_TEST_PROBE"] == "1" { return }
            #endif
            if readReleaseNotes != releaseNotesEdition { showReleaseNotes = true }
            else if model.donationPromptPending { showDonationPrompt = true }
        }
        .onChange(of: model.screen) { screen in
            guard screen == .home else { return }
            syncCurrentProjectMeta()
            library.refresh()
        }
        .onChange(of: library.all) { all in
            // Some com marcas de projetos que deixaram de existir.
            let alive = Set(all.map(\.id))
            if !selection.isSubset(of: alive) { selection = selection.intersection(alive) }
        }
    }

    // MARK: casca

    private var main: some View {
        VStack(spacing: 0) {
            LiveNoticeBanners()
            AppConfigBanner()
            ZStack {
                if selecting {
                    OmegaSelectionHeader(count: selection.count, onSelectAll: {
                        selection = Set(arrangeHomeProjects(library.all, sort: defaults.sort, query: defaults.query).map(\.id))
                    }, onCancel: { selection = [] })
                    .transition(.opacity)
                } else {
                    OmegaHomeTopBar(signedInEmail: conta.email, accountMenu: $accountMenu,
                                    onMenu: openDrawer, onReleaseNotes: { showReleaseNotes = true },
                                    onAccount: {
                                        if conta.logado { accountMenu = true }
                                        else { conta.entrando = true; showAccount = true }
                                    },
                                    accountEntries: accountEntries)
                    .transition(.opacity)
                }
            }
            .animation(.easeInOut(duration: 0.3), value: selecting)
            ZStack {
                switch tab {
                case .start: CommunityView(profileMode: false)
                case .profile: CommunityView(profileMode: true)
                case .presets: OmegaPresetsTab(onQuickMenu: { quick = $0 })
                case .projects:
                    OmegaProjectsTab(library: library, defaults: defaults, searching: $projectsSearching,
                                     selection: $selection, onOpen: openProject,
                                     onQuickMenu: { quick = $0 }, onDialog: { dialog = $0 })
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .overlay(alignment: .leading) {
                // Arrastar a partir da borda inicial abre a gaveta.
                if !drawerOpen && !selecting {
                    Color.clear.frame(width: 16).contentShape(Rectangle()).gesture(openDrag)
                }
            }
            ZStack {
                if selecting {
                    OmegaSelectionBar(count: selection.count, onExport: {
                        if selection.count == 1, let entry = library.all.first(where: { selection.contains($0.id) }) {
                            dialog = .exportFile(entry)
                        }
                    }, onDuplicate: {
                        for entry in library.all where selection.contains(entry.id) { _ = duplicateHomeProject(entry) }
                        selection = []; library.refresh()
                    }, onDelete: { dialog = .batchDelete(selection) })
                    .transition(.opacity)
                } else {
                    OmegaHomeDock(selected: tab, onSelect: select, onCreate: { showNewProject = true })
                        .transition(.opacity)
                }
            }
            .animation(.easeInOut(duration: 0.3), value: selecting)
        }
        .accessibilityHidden(drawerOpen)
    }

    // MARK: gaveta (P0-3)

    private var rtlSign: CGFloat { direction == .rightToLeft ? -1 : 1 }

    /// Deslocamento da gaveta: 0 = aberta, −largura = fechada (no sentido da leitura).
    private var drawerShift: CGFloat {
        drawerOpen ? min(0, drawerDrag) : -drawerWidth + max(0, drawerDrag)
    }

    @ViewBuilder private var drawerLayer: some View {
        let width = drawerWidth
        let progress = max(0, min(1, (width + drawerShift) / width))
        if progress > 0 {
            AureaColors.homeCover.opacity(Double(progress)).ignoresSafeArea()
                .onTapGesture(perform: closeDrawer)
                .gesture(closeDrag)
                .accessibilityLabel(AureaText.t("editor_fechar"))
                .accessibilityAddTraits(.isButton)
        }
        HomeSettingsDrawer(model: model, defaults: defaults,
                           onImportMedia: { closeDrawer(); importing = true },
                           onImportProject: { closeDrawer(); importingProjectFile = true },
                           onHyper: { closeDrawer(); model.openHyper() },
                           onAbout: { closeDrawer(); showAbout = true },
                           onSupport: { closeDrawer(); reportingProblem = true })
            .frame(width: width)
            .frame(maxHeight: .infinity)
            .background(AureaColors.homeDrawer.ignoresSafeArea())
            .offset(x: drawerShift * rtlSign)
            .gesture(closeDrag)
            .opacity(progress > 0 ? 1 : 0)
            .allowsHitTesting(drawerOpen)
            .accessibilityHidden(!drawerOpen)
            .accessibilityAddTraits(drawerOpen ? .isModal : [])
            .accessibilityAction(.escape) { closeDrawer() }
            .accessibilityElement(children: .contain)
            .accessibilityLabel(AureaText.t("home_title_settings"))
            .accessibilityIdentifier("home.drawer")
    }

    private var openDrag: some Gesture {
        DragGesture(minimumDistance: 8)
            .onChanged { value in drawerDrag = max(0, value.translation.width * rtlSign) }
            .onEnded { value in
                let opened = value.translation.width * rtlSign > drawerWidth / 3
                withAnimation(.easeOut(duration: 0.25)) { drawerOpen = opened; drawerDrag = 0 }
            }
    }

    private var closeDrag: some Gesture {
        DragGesture(minimumDistance: 8)
            .onChanged { value in if drawerOpen { drawerDrag = min(0, value.translation.width * rtlSign) } }
            .onEnded { value in
                let closed = value.translation.width * rtlSign < -drawerWidth / 3
                withAnimation(.easeOut(duration: 0.25)) { drawerOpen = !closed && drawerOpen; drawerDrag = 0 }
            }
    }

    private func openDrawer() {
        withAnimation(.easeOut(duration: 0.25)) { drawerOpen = true; drawerDrag = 0 }
    }

    private func closeDrawer() {
        withAnimation(.easeOut(duration: 0.25)) { drawerOpen = false; drawerDrag = 0 }
    }

    // MARK: conta (P0-4)

    private func accountEntries() -> [MenuEntry] {
        guard let email = conta.email else { return [] }
        return [
            .header(AureaText.t("conta_conectado", email), tag: "home.account.email"),
            .item(AureaText.t("conta_sair"), glyph: CupertinoGlyph.ArrowUturnLeft, tag: "home.account.logout") { confirmLogout = true },
            .item(AureaText.t("conta_excluir"), glyph: CupertinoGlyph.Trash, destructive: true, tag: "home.account.delete") { confirmDelete = true },
        ]
    }

    @ViewBuilder private var accountDialogs: some View {
        if confirmLogout {
            AureaAlert(title: AureaText.t("conta_sair"), message: AureaText.t("conta_sair_mensagem"),
                       confirmLabel: AureaText.t("conta_sair"), destructive: true,
                       onConfirm: { conta.sair() }, onDismiss: { confirmLogout = false })
        } else if confirmDelete {
            AureaAlert(title: AureaText.t("conta_excluir"), message: AureaText.t("conta_excluir_mensagem"),
                       confirmLabel: AureaText.t("conta_excluir"), destructive: true,
                       onConfirm: {
                           conta.excluirConta { ok in model.toast = AureaText.t(ok ? "conta_excluida" : "conta_excluir_falhou") }
                       }, onDismiss: { confirmDelete = false })
        } else if showDonationPrompt {
            AureaAlert(title: AureaText.t("donation_title"), message: AureaText.t("donation_launch_question"),
                       confirmLabel: AureaText.t("donation_launch_yes"), cancelLabel: AureaText.t("donation_launch_later"),
                       onConfirm: {
                           model.donationPromptPending = false
                           if let url = URL(string: AureaDonations.paypal) { UIApplication.shared.open(url) }
                       }, onDismiss: { showDonationPrompt = false; model.donationPromptPending = false })
        }
    }

    // MARK: menu rápido (P1-2)

    @ViewBuilder private var quickLayer: some View {
        if let request = quick {
            OmegaQuickMenu(row: request.row, areaBottom: request.areaBottom, options: request.options, tag: request.tag,
                           onDismiss: { quick = nil }) { request.rowCopy }
                .ignoresSafeArea()
        }
    }

    // MARK: ações

    private func select(_ kind: HomeTabKind) {
        guard kind != tab else { return }
        UISelectionFeedbackGenerator().selectionChanged()
        savedTab = kind.rawValue
    }

    /// P2-1: "Prévia leve" abre o editor com a prévia em 1/2.
    private func openProject(_ entry: HomeProjectEntry) {
        model.open(entry.file)
        if defaults.lightPreview { model.setPreviewScale(num: 1, den: 2, auto: false) }
    }

    /// A ficha (`.meta.json`) do projeto aberto, com os números que o MOTOR
    /// respondeu — é o que a Home lê para a ficha da linha.
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
        defaults.remember(draft)
        let created = model.newProject(width: draft.width, height: draft.height,
                                       fps: draft.fps, title: draft.title,
                                       background: Array(draft.background.prefix(3)) + [draft.transparent ? 0 : 1],
                                       uiMode: draft.uiMode)
        guard created, let url = model.projectURL else {
            if (model.toast ?? "").isEmpty { model.toast = AureaText.t("msg_nao_foi_possivel_criar_o_projeto") }
            return
        }
        if defaults.lightPreview { model.setPreviewScale(num: 1, den: 2, auto: false) }
        // A capa e a ficha nascem com o projeto (o Android grava as duas no mesmo save).
        _ = model.saveProject(writeThumbnail: true)
        writeHomeProjectMeta(url: url, title: model.projectName,
                             width: Int(draft.width), height: Int(draft.height),
                             fps: draft.fps, durationFrames: Int(model.compositionDuration))
        library.refresh()
    }
}

// =============================================================================
// Omega home · P0-1: a barra superior (57 pt)
// =============================================================================
struct OmegaHomeTopBar: View {
    let signedInEmail: String?
    @Binding var accountMenu: Bool
    let onMenu: () -> Void
    let onReleaseNotes: () -> Void
    let onAccount: () -> Void
    let accountEntries: () -> [MenuEntry]

    var body: some View {
        ZStack {
            HStack(spacing: 8) {
                HomeBrandLogo().frame(width: 28, height: 28)
                Text("AUREA").font(.aurea(size: 20, weight: .bold)).tracking(1).foregroundStyle(AureaColors.lightText)
            }
            .accessibilityElement(children: .combine)
            .accessibilityAddTraits(.isHeader)
            .accessibilityIdentifier("home.logo")
            HStack(spacing: 0) {
                Button(action: onMenu) {
                    CupertinoGlyph.text(CupertinoGlyph.LineHorizontal3, size: 24, color: AureaColors.lightText)
                        .frame(width: 50, height: 50).contentShape(Rectangle())
                }.buttonStyle(AureaPressStyle())
                    .accessibilityLabel(AureaText.t("home_tab_settings"))
                    .accessibilityIdentifier("home.menu")
                Spacer(minLength: 0)
                Button(action: onReleaseNotes) {
                    CupertinoGlyph.text(CupertinoGlyph.Sparkles, size: 22, color: AureaColors.lightText)
                        .frame(width: 44, height: 44).contentShape(Rectangle())
                }.buttonStyle(AureaPressStyle())
                    .accessibilityLabel(AureaText.t("release_notes_title"))
                    .accessibilityIdentifier("home.releaseNotes")
                Button(action: onAccount) { avatar.frame(width: 44, height: 44).contentShape(Rectangle()) }
                    .buttonStyle(AureaPressStyle())
                    .accessibilityLabel(signedInEmail.map { AureaText.t("om_home_account") + " · " + $0 } ?? AureaText.t("om_home_account"))
                    .accessibilityIdentifier("home.account")
                    .aureaAnchoredMenu(isPresented: $accountMenu, alignEnd: true, tag: "home.account.menu", entries: accountEntries)
            }
            .padding(.leading, 4).padding(.trailing, 6)
        }
        .frame(height: OmegaHomeDims.topBar)
        .frame(maxWidth: .infinity)
        .background(AureaColors.lightBackground)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("home.topBar")
    }

    /// Avatar de 40: sem login, a pessoa sobre o cinza; com login, a inicial.
    @ViewBuilder private var avatar: some View {
        if let initial = signedInEmail?.trimmingCharacters(in: .whitespaces).first {
            Text(String(initial).uppercased()).font(.aurea(size: 15, weight: .bold)).foregroundStyle(AureaColors.lightOnInk)
                .frame(width: 30, height: 30).background(AureaColors.lightInk, in: Circle())
        } else {
            CupertinoGlyph.text(CupertinoGlyph.PersonCropCircle, size: 22, color: AureaColors.homeOnAvatar)
                .frame(width: 30, height: 30).background(AureaColors.homeAvatar, in: Circle())
        }
    }
}

/// Omega home · P0-6: o cabeçalho do modo de seleção (no lugar da barra superior).
struct OmegaSelectionHeader: View {
    let count: Int
    let onSelectAll: () -> Void
    let onCancel: () -> Void
    var body: some View {
        HStack(spacing: 0) {
            Text(AureaText.t("home_selected_chosen", count)).font(OmegaHomeType.listTitle).foregroundStyle(AureaColors.lightText)
                .lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
                .accessibilityAddTraits(.isHeader).accessibilityIdentifier("home.selection.count")
            Button(action: onSelectAll) {
                Text(AureaText.t("home_select_all")).font(.aurea(size: 14, weight: .medium)).foregroundStyle(AureaColors.lightAccentInk)
                    .lineLimit(1).padding(.horizontal, 8).frame(minHeight: 48).contentShape(Rectangle())
            }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityIdentifier("home.selection.all")
            Button(action: onCancel) {
                Text(AureaText.t("common_cancel")).font(OmegaHomeType.action).foregroundStyle(AureaColors.lightText)
                    .lineLimit(1).padding(.leading, 8).frame(minHeight: 48).contentShape(Rectangle())
            }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityIdentifier("home.selection.cancel")
        }
        .padding(.horizontal, 16).padding(.vertical, 4)
        .frame(height: OmegaHomeDims.topBar)
        .background(AureaColors.lightBackground)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("home.selection.header")
    }
}

/// Omega home · P0-2: o dock de 72 pt (branco, sombra para cima): Início · Perfil ·
/// [+] · Projetos · Presets. O "+" é o do AUREA (D4), no centro exato.
struct OmegaHomeDock: View {
    let selected: HomeTabKind
    let onSelect: (HomeTabKind) -> Void
    let onCreate: () -> Void
    var body: some View {
        HStack(spacing: 0) {
            tab(.start, CupertinoGlyph.House, CupertinoGlyph.HouseFill, "home_tab_start", "home.community")
            tab(.profile, CupertinoGlyph.PersonCropCircle, CupertinoGlyph.PersonCropCircle, "social_profile", "home.profile")
            Button(action: onCreate) {
                CupertinoGlyph.text(CupertinoGlyph.Plus, size: 30, color: AureaColors.onAccent)
                    .frame(width: 56, height: 56).background(AureaColors.accent, in: Circle())
                    .contentShape(Circle())
            }.buttonStyle(AureaPressStyle()).padding(.horizontal, 4)
                .accessibilityLabel(AureaText.t("home_new_project"))
                .accessibilityIdentifier("home.create")
            tab(.projects, CupertinoGlyph.RectangleStack, CupertinoGlyph.RectangleStack, "home_tab_projects", "home.projects")
            tab(.presets, CupertinoGlyph.WandStars, CupertinoGlyph.WandStars, "home_presets_short", "home.presets")
        }
        .padding(.horizontal, 8).padding(.vertical, 12)
        .frame(height: OmegaHomeDims.dock)
        .frame(maxWidth: .infinity)
        .background(AureaColors.lightSurface.ignoresSafeArea(edges: .bottom))
        .shadow(color: AureaColors.lightShadow, radius: 15, x: 0, y: -2)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("home.dock")
    }

    private func tab(_ kind: HomeTabKind, _ glyph: Character, _ glyphOn: Character, _ label: String, _ tag: String) -> some View {
        let on = selected == kind
        let ink = on ? AureaColors.lightText : AureaColors.lightTextMuted
        return Button { onSelect(kind) } label: {
            VStack(spacing: 2) {
                CupertinoGlyph.text(on ? glyphOn : glyph, size: 24, color: ink)
                Text(AureaText.t(label)).font(.aurea(size: 12, weight: on ? .semibold : .medium)).foregroundStyle(ink)
                    .lineLimit(1).truncationMode(.tail)
            }
            .frame(maxWidth: 158, minHeight: 48).contentShape(Rectangle())
            .animation(.easeInOut(duration: 0.3), value: on)
        }.buttonStyle(.plain).frame(maxWidth: .infinity)
            .accessibilityLabel(AureaText.t(label))
            .accessibilityAddTraits(on ? [.isButton, .isSelected] : .isButton)
            .accessibilityIdentifier(tag)
    }
}

/// Omega home · P0-6: a barra de seleção (72 pt) no lugar do dock: Exportar ·
/// Duplicar · Excluir. Exportar só com UM projeto (o arquivo é um por vez).
struct OmegaSelectionBar: View {
    let count: Int
    let onExport: () -> Void
    let onDuplicate: () -> Void
    let onDelete: () -> Void
    var body: some View {
        HStack(spacing: 0) {
            action(CupertinoGlyph.SquareArrowUp, "om_home_action_export", "home.selection.export", enabled: count == 1, onExport)
            action(CupertinoGlyph.PlusSquareOnSquare, "common_duplicate", "home.selection.duplicate", enabled: true, onDuplicate)
            action(CupertinoGlyph.Trash, "common_delete", "home.selection.delete", enabled: true, onDelete)
        }
        .padding(.horizontal, 8).padding(.vertical, 12)
        .frame(height: OmegaHomeDims.selectionBar)
        .frame(maxWidth: .infinity)
        .background(AureaColors.homeSelectionBar.ignoresSafeArea(edges: .bottom))
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("home.selection.bar")
    }
    private func action(_ glyph: Character, _ label: String, _ tag: String, enabled: Bool, _ run: @escaping () -> Void) -> some View {
        Button(action: run) {
            VStack(spacing: 2) {
                CupertinoGlyph.text(glyph, size: 22, color: AureaColors.homeOnSelectionBar)
                Text(AureaText.t(label)).font(OmegaHomeType.tabLabel).foregroundStyle(AureaColors.homeOnSelectionBar).lineLimit(1)
            }.frame(maxWidth: .infinity, minHeight: 48).contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle())
            .disabled(!enabled).opacity(enabled ? 1 : 0.4)
            .accessibilityIdentifier(tag)
    }
}

// =============================================================================
// Omega home · P0-5: a aba Projetos
// =============================================================================
private struct OmegaScrollOffsetKey: PreferenceKey {
    static var defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) { value = nextValue() }
}

/// 24 · pílulas (por enquanto só "Projetos"; "Elementos" é o P2-6) · 24 · "Meus
/// projetos" com a busca e a ordem · 12 · cartões de 96 (16 nas laterais, 12
/// entre eles), cabeçalho de data em "Mais recentes" (P1-3) e a sombra de 25
/// quando a lista sai do topo. Tablet: colunas de linhas (mínimo 360).
struct OmegaProjectsTab: View {
    @ObservedObject var library: HomeLibrary
    @ObservedObject var defaults: HomeDefaults
    @Binding var searching: Bool
    @Binding var selection: Set<String>
    let onOpen: (HomeProjectEntry) -> Void
    let onQuickMenu: (OmegaQuickRequest) -> Void
    let onDialog: (HomeProjectDialog) -> Void
    @State private var sortOpen = false
    @State private var scrolled = false
    @State private var areaBottom: CGFloat = .greatestFiniteMagnitude

    private var arranged: [HomeProjectEntry] { arrangeHomeProjects(library.all, sort: defaults.sort, query: defaults.query) }
    private var selecting: Bool { !selection.isEmpty }

    var body: some View {
        VStack(spacing: 0) {
            OmegaSubtabs(options: [(AureaText.t("home_tab_projects"), "home.projects.subtab.projects")], selected: 0) { _ in }
                .padding(.top, 18).padding(.bottom, 8)
            if library.loaded {
                if library.all.isEmpty {
                    Spacer(minLength: 0)
                    OmegaProjectsEmptyState()
                    Spacer(minLength: 0)
                } else {
                    header
                    list.padding(.top, 2)
                }
            } else {
                Spacer(minLength: 0)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(GeometryReader { geometry in
            Color.clear
                .onAppear { areaBottom = geometry.frame(in: .global).maxY }
                .onChange(of: geometry.frame(in: .global).maxY) { areaBottom = $0 }
        })
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("home.projects.tab")
    }

    @ViewBuilder private var header: some View {
        if searching || !defaults.query.isEmpty {
            HStack(spacing: 8) {
                OmegaSearchField(query: $defaults.query) { defaults.query = ""; searching = false }
                Button { sortOpen = true } label: {
                    CupertinoGlyph.text(CupertinoGlyph.ArrowUpArrowDown, size: 20, color: AureaColors.lightText)
                        .frame(width: 44, height: 44).contentShape(Rectangle())
                }.buttonStyle(AureaPressStyle())
                    .accessibilityLabel(AureaText.t("home_sort_title"))
                    .accessibilityIdentifier("home.projects.sort")
                    .aureaAnchoredMenu(isPresented: $sortOpen, width: UIScreen.main.bounds.width * 0.6, alignEnd: true,
                                       tag: "home.projects.sortMenu", entries: sortEntries)
            }.padding(.horizontal, OmegaHomeDims.gutter)
        } else {
            OmegaListHeader(title: AureaText.t("om_home_my_projects"),
                            sortLabel: AureaText.t("om_home_sort_current", defaults.sort.label),
                            tag: "home.projects.header", sortTag: "home.projects.sort",
                            searchTag: "home.projects.search", onSearch: { searching = true },
                            onSort: { sortOpen = true }) { button in
                button.aureaAnchoredMenu(isPresented: $sortOpen, width: UIScreen.main.bounds.width * 0.6, alignEnd: true,
                                         tag: "home.projects.sortMenu", entries: sortEntries)
            }
        }
    }

    /// P1-3: Nome · Mais recentes · Mais longos (extra D8), no menu ancorado da fundação.
    private func sortEntries() -> [MenuEntry] {
        let order: [(HomeSortOrder, String)] = [(HomeSortOrder.name, "home.projects.sort.name"),
                                                 (HomeSortOrder.recent, "home.projects.sort.recent"),
                                                 (HomeSortOrder.longest, "home.projects.sort.longest")]
        return order.map { (pair: (HomeSortOrder, String)) -> MenuEntry in
            let (sort, tag) = pair
            return .item(sort.label, selected: defaults.sort == sort, checked: defaults.sort == sort, tag: tag) {
                defaults.sortIndex = sort.rawValue
            }
        }
    }

    private var dayFormatter: DateFormatter {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: AureaText.language.resolved.rawValue)
        formatter.dateStyle = .long
        formatter.timeStyle = .none
        return formatter
    }

    private var list: some View {
        GeometryReader { bounds in
            let columns = omegaProjectColumns(width: bounds.size.width)
            let items = omegaProjectListItems(arranged, sort: defaults.sort, columns: columns)
            let formatter = dayFormatter
            ZStack(alignment: .top) {
                if arranged.isEmpty {
                    Text(AureaText.t("home_no_results")).font(OmegaHomeType.body).foregroundStyle(AureaColors.lightTextMuted)
                        .multilineTextAlignment(.center).padding(.horizontal, 35).padding(.vertical, 32)
                } else {
                    ScrollView {
                        LazyVStack(spacing: 12) {
                            ForEach(items) { item in
                                switch item {
                                case .dateHeader(let day):
                                    Text(formatter.string(from: day)).font(OmegaHomeType.small).foregroundStyle(AureaColors.lightText)
                                        .frame(maxWidth: .infinity, alignment: .leading).padding(.top, 12)
                                        .accessibilityAddTraits(.isHeader)
                                        .accessibilityIdentifier("home.projects.dateHeader")
                                case .cards(let entries):
                                    HStack(spacing: 12) {
                                        ForEach(entries) { entry in row(entry) }
                                        ForEach(0..<max(0, columns - entries.count), id: \.self) { _ in Color.clear.frame(maxWidth: .infinity) }
                                    }
                                }
                            }
                        }
                        .padding(.horizontal, OmegaHomeDims.gutter).padding(.bottom, 16)
                        .background(GeometryReader { geometry in
                            Color.clear.preference(key: OmegaScrollOffsetKey.self,
                                                   value: geometry.frame(in: .named("omega.projects")).minY)
                        })
                    }
                    .coordinateSpace(name: "omega.projects")
                    .onPreferenceChange(OmegaScrollOffsetKey.self) { scrolled = $0 < -0.5 }
                    .scrollDisabled(sortOpen)
                    .accessibilityIdentifier("home.projects.list")
                    OmegaScrollShadow(visible: scrolled)
                }
            }
        }
    }

    private func row(_ entry: HomeProjectEntry) -> some View {
        OmegaProjectRow(entry: entry, selecting: selecting, marked: selection.contains(entry.id),
                        onOpen: {
                            if selecting { toggle(entry) } else { onOpen(entry) }
                        },
                        onLongPress: {
                            if selecting { toggle(entry) } else { selection.insert(entry.id) }
                        },
                        onMenu: { rect in onQuickMenu(quickRequest(entry, rect)) })
            .frame(maxWidth: .infinity)
    }

    private func toggle(_ entry: HomeProjectEntry) {
        if selection.contains(entry.id) { selection.remove(entry.id) } else { selection.insert(entry.id) }
    }

    /// Selecionar · Exportar · Renomear · Duplicar · Excluir (projeto local).
    private func quickRequest(_ entry: HomeProjectEntry, _ rect: CGRect) -> OmegaQuickRequest {
        OmegaQuickRequest(row: rect, areaBottom: areaBottom, tag: "home.projectMenu", options: [
            OmegaQuickOption(label: AureaText.t("home_select"), glyph: CupertinoGlyph.CheckmarkCircle, tag: "home.project.select") {
                selection.insert(entry.id)
            },
            OmegaQuickOption(label: AureaText.t("om_home_action_export"), glyph: CupertinoGlyph.SquareArrowUp, tag: "home.projectMenu.export") {
                onDialog(.exportFile(entry))
            },
            OmegaQuickOption(label: AureaText.t("common_rename"), glyph: CupertinoGlyph.Pencil, tag: "home.projectMenu.rename") {
                onDialog(.rename(entry))
            },
            OmegaQuickOption(label: AureaText.t("common_duplicate"), glyph: CupertinoGlyph.PlusSquareOnSquare, tag: "home.projectMenu.duplicate") {
                duplicateHomeProject(entry)
                library.refresh()
            },
            OmegaQuickOption(label: AureaText.t("common_delete"), glyph: CupertinoGlyph.Trash, tag: "home.projectMenu.delete", destructive: true) {
                onDialog(.confirmDelete(entry))
            },
        ], rowCopy: AnyView(OmegaProjectRow(entry: entry, selecting: false, marked: false, interactive: false,
                                            onOpen: {}, onLongPress: {}, onMenu: { _ in })))
    }
}

// =============================================================================
// Omega home · P1-5: a aba Presets (no lugar da aba Modelos da referência, D3)
// =============================================================================
struct OmegaPresetsTab: View {
    let onQuickMenu: (OmegaQuickRequest) -> Void
    @State private var page = 0
    var body: some View {
        VStack(spacing: 0) {
            OmegaSubtabs(options: [(AureaText.t("om_home_my_presets"), "home.presets.subtab.mine"),
                                   (AureaText.t("social_community"), "home.presets.subtab.community")],
                         selected: page) { index in withAnimation(.easeInOut(duration: 0.25)) { page = index } }
                .padding(.top, 18).padding(.bottom, 8)
            TabView(selection: $page) {
                OmegaMyPresets(onQuickMenu: onQuickMenu).tag(0)
                HomeCommunityPresets().tag(1)
            }
            .tabViewStyle(.page(indexDisplayMode: .never))
        }
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("home.presets.tab")
    }
}

/// "Meus presets": os da pessoa (com data e o menu rápido Renomear · Excluir) e
/// os nativos do AUREA (sem menu), em linhas de 96 com o glifo do tipo.
struct OmegaMyPresets: View {
    @EnvironmentObject private var model: AureaModel
    let onQuickMenu: (OmegaQuickRequest) -> Void
    @State private var entries: [PanelPresetEntry] = []
    @State private var byName = true
    @State private var sortOpen = false
    @State private var scrolled = false
    @State private var areaBottom: CGFloat = .greatestFiniteMagnitude

    private func modified(_ entry: PanelPresetEntry) -> Date? {
        guard let file = entry.file else { return nil }
        return (try? file.resourceValues(forKeys: [.contentModificationDateKey]))?.contentModificationDate
    }

    private var arranged: [PanelPresetEntry] {
        let mine = entries.filter { $0.file != nil }
        let builtin = entries.filter { $0.file == nil }.sorted { $0.name.localizedLowercase < $1.name.localizedLowercase }
        if byName { return (mine + builtin).sorted { $0.name.localizedLowercase < $1.name.localizedLowercase } }
        return mine.sorted { (modified($0) ?? .distantPast) > (modified($1) ?? .distantPast) } + builtin
    }

    var body: some View {
        VStack(spacing: 0) {
            OmegaListHeader(title: AureaText.t("om_home_my_presets"),
                            sortLabel: AureaText.t("om_home_sort_current", AureaText.t(byName ? "home_sort_name" : "home_sort_recent")),
                            tag: "home.presets.header", sortTag: "home.presets.sort", onSort: { sortOpen = true }) { button in
                button.aureaAnchoredMenu(isPresented: $sortOpen, width: UIScreen.main.bounds.width * 0.6, alignEnd: true,
                                         tag: "home.presets.sortMenu") {
                    [.item(AureaText.t("home_sort_name"), selected: byName, checked: byName, tag: "home.presets.sort.name") { byName = true },
                     .item(AureaText.t("home_sort_recent"), selected: !byName, checked: !byName, tag: "home.presets.sort.recent") { byName = false }]
                }
            }
            ZStack(alignment: .top) {
                ScrollView {
                    LazyVStack(spacing: 12) {
                        if !entries.contains(where: { $0.file != nil }) {
                            Text(AureaText.t("om_home_presets_empty")).font(OmegaHomeType.body).foregroundStyle(AureaColors.lightTextMuted)
                                .frame(maxWidth: .infinity, alignment: .leading).padding(.vertical, 8)
                                .accessibilityIdentifier("home.presets.empty")
                        }
                        ForEach(arranged) { entry in row(entry) }
                    }
                    .padding(.horizontal, OmegaHomeDims.gutter).padding(.bottom, 16)
                    .background(GeometryReader { geometry in
                        Color.clear.preference(key: OmegaScrollOffsetKey.self, value: geometry.frame(in: .named("omega.presets")).minY)
                    })
                }
                .coordinateSpace(name: "omega.presets")
                .onPreferenceChange(OmegaScrollOffsetKey.self) { scrolled = $0 < -0.5 }
                .accessibilityIdentifier("home.presets.list")
                OmegaScrollShadow(visible: scrolled)
            }
            .padding(.top, 2)
        }
        .background(GeometryReader { geometry in
            Color.clear
                .onAppear { areaBottom = geometry.frame(in: .global).maxY }
                .onChange(of: geometry.frame(in: .global).maxY) { areaBottom = $0 }
        })
        .onAppear { entries = PanelPresetEntry.loadAll() }
    }

    /// Os diálogos abrem por cima de tudo (fundação F2: `AureaPopupPresenter`),
    /// não presos à área da aba.
    private func askRename(_ entry: PanelPresetEntry) {
        AureaPopupPresenter.shared.presentDialog { close in
            AureaNamePrompt(title: AureaText.t("common_rename"), initial: entry.name,
                            onConfirm: { rename(entry, to: $0) }, onDismiss: close)
        }
    }

    private func askDelete(_ entry: PanelPresetEntry) {
        AureaPopupPresenter.shared.presentDialog { close in
            AureaAlert(title: AureaText.t("om_home_preset_delete_q", entry.name), message: AureaText.t("common_irreversible"),
                       confirmLabel: AureaText.t("common_delete"), destructive: true,
                       onConfirm: {
                           if let file = entry.file { try? FileManager.default.removeItem(at: file) }
                           entries = PanelPresetEntry.loadAll()
                       }, onDismiss: close)
        }
    }

    private func row(_ entry: PanelPresetEntry) -> some View {
        OmegaPresetRow(entry: entry, date: modified(entry)) { rect in
            onQuickMenu(OmegaQuickRequest(row: rect, areaBottom: areaBottom, tag: "home.presetMenu", options: [
                OmegaQuickOption(label: AureaText.t("common_rename"), glyph: CupertinoGlyph.Pencil, tag: "home.presetMenu.rename") { askRename(entry) },
                OmegaQuickOption(label: AureaText.t("common_delete"), glyph: CupertinoGlyph.Trash, tag: "home.presetMenu.delete",
                                 destructive: true) { askDelete(entry) },
            ], rowCopy: AnyView(OmegaPresetRow(entry: entry, date: modified(entry), interactive: false) { _ in })))
        }
    }

    /// Renomear um preset da pessoa = mover o arquivo para o nome novo (mesma pasta).
    private func rename(_ entry: PanelPresetEntry, to name: String) {
        let clean = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let file = entry.file, !clean.isEmpty, clean != entry.name else { return }
        let target = entry.kind.directory.appendingPathComponent(PanelPresetKind.fileName(clean))
        if FileManager.default.fileExists(atPath: target.path) {
            model.toast = AureaText.t("om_home_preset_name_taken")
            return
        }
        do { try FileManager.default.moveItem(at: file, to: target) }
        catch { model.toast = AureaText.t("ios_media_open_failed") }
        entries = PanelPresetEntry.loadAll()
    }
}

/// O glifo de cada tipo de preset (o mesmo do Android).
extension PanelPresetKind {
    var omegaGlyph: Character {
        switch self {
        case .effects: return CupertinoGlyph.Sparkles
        case .text: return CupertinoGlyph.Textformat
        case .animation: return CupertinoGlyph.Move
        case .caption: return CupertinoGlyph.CaptionsBubble
        case .curve: return CupertinoGlyph.Scribble
        }
    }
}

/// A linha de 96 de um preset: glifo do tipo (32) sobre `homeThumb`, o nome, a
/// ficha (tipo · data, ou "Do AUREA") e o ⋯ só nos presets da pessoa.
struct OmegaPresetRow: View {
    let entry: PanelPresetEntry
    let date: Date?
    var interactive = true
    let onMenu: (CGRect) -> Void
    @State private var box = OmegaFrameBox()

    var body: some View {
        HStack(spacing: 0) {
            CupertinoGlyph.text(entry.kind.omegaGlyph, size: 32, color: AureaColors.lightText)
                .frame(width: OmegaHomeDims.thumb, height: OmegaHomeDims.thumb)
                .background(AureaColors.homeThumb, in: RoundedRectangle(cornerRadius: 8))
            VStack(alignment: .leading, spacing: 4) {
                Text(entry.name).font(OmegaHomeType.rowTitle).foregroundStyle(AureaColors.lightText).lineLimit(1)
                    .padding(.bottom, 4)
                meta(entry.kind.omegaGlyph, entry.kind.label)
                meta(entry.file == nil ? CupertinoGlyph.CheckmarkSeal : CupertinoGlyph.Clock,
                     entry.file == nil ? AureaText.t("om_home_preset_builtin")
                        : date.map { DateFormatter.localizedString(from: $0, dateStyle: .medium, timeStyle: .none) } ?? "")
            }
            .frame(maxWidth: .infinity, alignment: .leading).padding(.leading, 16)
            if entry.file != nil {
                Button { onMenu(box.rect) } label: {
                    CupertinoGlyph.text(CupertinoGlyph.Ellipsis, size: 24, color: AureaColors.lightTextMuted)
                        .frame(width: 44, height: 44).contentShape(Rectangle())
                }.buttonStyle(AureaPressStyle()).disabled(!interactive)
                    .accessibilityLabel(AureaText.t("om_home_preset_menu"))
                    .accessibilityIdentifier("home.preset.more")
                    .padding(.trailing, 4)
            }
        }
        .padding(8)
        .frame(height: OmegaHomeDims.row)
        .background(AureaColors.lightSurface, in: RoundedRectangle(cornerRadius: 12))
        .omegaShadow(12, AureaColors.homeShadowSoft)
        .background(GeometryReader { geometry in
            Color.clear
                .onAppear { box.rect = geometry.frame(in: .global) }
                .onChange(of: geometry.frame(in: .global)) { box.rect = $0 }
        })
        .accessibilityElement(children: .combine)
        .accessibilityIdentifier("home.preset.row")
    }

    private func meta(_ glyph: Character, _ text: String) -> some View {
        HStack(spacing: 6) {
            CupertinoGlyph.text(glyph, size: 14, color: AureaColors.lightTextMuted)
            Text(text).font(OmegaHomeType.meta).foregroundStyle(AureaColors.lightTextMuted).lineLimit(1)
        }.frame(height: 16)
    }
}

@MainActor
struct HomeCommunityPresets: View {
    @State private var entries: [HomeCommunityEntry] = []
    @State private var offline = false
    @State private var query = ""
    @State private var revision = 0
    @State private var loading = false
    @State private var downloading: String?
    @State private var message: String?
    @State private var failed = false
    @State private var downloadTask: Task<Void, Never>?
    private var requestKey: String { "\(offline):\(revision)" }
    var body: some View {
        // Omega home: a subaba "Comunidade" da aba Presets (clara, como a Home).
        ScrollView {
            LazyVStack(alignment: .leading, spacing: 12) {
                Text(AureaText.t("home_community_caption_hint")).font(OmegaHomeType.body).foregroundStyle(AureaColors.lightTextMuted)
                Picker("", selection: $offline) {
                    Text(AureaText.t("home_presets_short")).tag(false)
                    Text(AureaText.t("home_downloaded")).tag(true)
                }.pickerStyle(.segmented)
                HStack {
                    TextField(AureaText.t("home_search_presets"), text: $query)
                        .submitLabel(.search).onSubmit { revision += 1 }
                        .foregroundStyle(AureaColors.lightText)
                        .padding(12).background(AureaColors.lightSurface, in: RoundedRectangle(cornerRadius: 8))
                        .onChange(of: query) { if $0.count > 80 { query = String($0.prefix(80)) } }
                    Button(AureaText.t("home_search_action")) { revision += 1 }.frame(minHeight: 48)
                }
                if loading { ProgressView().frame(maxWidth: .infinity) }
                if let message {
                    Text(message).font(.subheadline).foregroundStyle(AureaColors.lightTextMuted)
                    if failed { Button(AureaText.t("home_retry")) { revision += 1 }.frame(minHeight: 48) }
                } else if !loading && entries.isEmpty {
                    Text(AureaText.t("home_no_presets")).foregroundStyle(AureaColors.lightTextMuted).padding(.vertical, 32)
                }
                ForEach(entries) { entry in
                    VStack(alignment: .leading, spacing: 8) {
                        Text(entry.name).font(OmegaHomeType.rowTitle).foregroundStyle(AureaColors.lightText)
                        Text(entry.author).font(OmegaHomeType.meta).foregroundStyle(AureaColors.lightTextMuted)
                        if offline { Text(AureaText.t("home_downloaded")).font(.caption) }
                        else {
                            Button(AureaText.t(downloading == entry.id ? "home_downloading" : "home_download")) {
                                downloading = entry.id
                                downloadTask = Task {
                                    defer { downloading = nil }
                                    do {
                                        try await HomeCommunityAPI.download(entry)
                                        try Task.checkCancellation()
                                        failed = false; message = AureaText.t("home_preset_parts_saved")
                                    } catch is CancellationError {} catch {
                                        failed = true; message = AureaText.t("home_community_unavailable")
                                    }
                                }
                            }.frame(minHeight: 48).disabled(downloading != nil)
                        }
                    }.padding(16).frame(maxWidth: .infinity, alignment: .leading)
                        .background(AureaColors.lightSurface, in: RoundedRectangle(cornerRadius: 12))
                        .omegaShadow(12, AureaColors.homeShadowSoft)
                }
            }.padding(.horizontal, 16).padding(.bottom, 16)
        }.foregroundStyle(AureaColors.lightText).tint(AureaColors.lightAccentInk)
            .accessibilityIdentifier("home.presets.community")
            .task(id: requestKey) {
                loading = true; message = nil; failed = false
                defer { loading = false }
                do { entries = try await HomeCommunityAPI.list(offline: offline, query: query) }
                catch is CancellationError {} catch {
                    entries = []; failed = true; message = AureaText.t("home_community_unavailable")
                }
            }.onDisappear { downloadTask?.cancel() }
    }
}

private struct HomeCommunityEntry: Identifiable, Codable, Sendable {
    let id: String
    let name: String
    let author: String
    init?(_ value: [String: Any]) {
        guard let id = value["id"] as? String, UUID(uuidString: id) != nil,
              let name = value["name"] as? String else { return nil }
        self.id = id; self.name = name; self.author = value["author"] as? String ?? "Aurea"
    }
}

private enum HomeCommunityAPI {
    private static var root: URL { AureaPaths.documents.appendingPathComponent("caption-community", isDirectory: true) }
    private static func request(_ path: String, post: Bool = false, token: String? = nil) async throws -> [String: Any] {
        guard let url = URL(string: "https://aurea-ai-discovery.aureaapp.workers.dev/api/captions" + path) else { throw URLError(.badURL) }
        var request = URLRequest(url: url, timeoutInterval: 30)
        if post { request.httpMethod = "POST"; request.httpBody = Data("{}".utf8); request.setValue("application/json", forHTTPHeaderField: "Content-Type") }
        if let token { request.setValue("Bearer " + token, forHTTPHeaderField: "Authorization") }
        // Enforce the bound while receiving, before a large response can be allocated.
        let (stream, response) = try await ContaAPI.session.bytes(for: request)
        guard let http = response as? HTTPURLResponse, (200...299).contains(http.statusCode) else { throw URLError(.badServerResponse) }
        var data = Data()
        for try await byte in stream {
            guard data.count < 5 * 1024 * 1024 else { throw URLError(.dataLengthExceedsMaximum) }
            data.append(byte)
        }
        guard let value = try JSONSerialization.jsonObject(with: data) as? [String: Any] else { throw URLError(.cannotParseResponse) }
        return value
    }
    static func list(offline: Bool, query: String) async throws -> [HomeCommunityEntry] {
        if offline {
            let files = (try? FileManager.default.contentsOfDirectory(at: root, includingPropertiesForKeys: [.fileSizeKey])) ?? []
            return files.filter { $0.pathExtension == "json" }.compactMap { url in
                guard let size = try? url.resourceValues(forKeys: [.fileSizeKey]).fileSize, size < 132 * 1024,
                      let bytes = try? Data(contentsOf: url), let value = try? JSONSerialization.jsonObject(with: bytes) as? [String: Any] else { return nil }
                return HomeCommunityEntry(value)
            }.filter { query.isEmpty || $0.name.localizedCaseInsensitiveContains(query) }.sorted { $0.name < $1.name }
        }
        var components = URLComponents(); components.queryItems = [URLQueryItem(name: "sort", value: "recent"), URLQueryItem(name: "q", value: query)]
        let result = try await request("/presets?" + (components.percentEncodedQuery ?? ""))
        return (result["items"] as? [[String: Any]] ?? []).compactMap(HomeCommunityEntry.init)
    }
    static func download(_ entry: HomeCommunityEntry) async throws {
        let session = try await request("/session", post: true)
        guard let token = session["token"] as? String else { throw URLError(.userAuthenticationRequired) }
        let received = try await request("/presets/\(entry.id)/download", post: true, token: token)
        guard let preset = received["preset"] as? [String: Any], preset["schema"] as? Int == 1 else { throw URLError(.cannotParseResponse) }
        let bytes = try JSONSerialization.data(withJSONObject: preset)
        guard bytes.count <= 128 * 1024 else { throw URLError(.dataLengthExceedsMaximum) }
        // Keep the full bundle and expose each native component in the existing
        // editor library. No arbitrary URLs or external assets are imported.
        var parts: [(URL, Data)] = []
        for (key, kind) in [("caption", PanelPresetKind.caption), ("text", .text), ("effects", .effects), ("animation", .animation)] {
            guard let source = preset[key] as? String, !source.isEmpty else { continue }
            let data = Data(source.utf8)
            guard let object = try JSONSerialization.jsonObject(with: data) as? [String: Any],
                  object["aurea_preset"] as? Int == 1, object["kind"] as? String == key else { throw URLError(.cannotParseResponse) }
            let file = kind.directory.appendingPathComponent(PanelPresetKind.fileName(entry.name + " · " + String(entry.id.prefix(8))))
            parts.append((file, data))
        }
        guard parts.contains(where: { $0.0.deletingLastPathComponent() == PanelPresetKind.caption.directory }) else { throw URLError(.cannotParseResponse) }
        try Task.checkCancellation()
        for (file, data) in parts {
            try FileManager.default.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
            try data.write(to: file, options: .atomic)
        }
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        let stored: [String: Any] = ["id": entry.id, "name": entry.name, "author": entry.author, "preset": preset]
        try JSONSerialization.data(withJSONObject: stored).write(to: root.appendingPathComponent(entry.id + ".json"), options: .atomic)
    }
}

#Preview {
    HomeView().environmentObject(AureaModel()).environmentObject(ContaModel())
}

/// "N pessoas cadastradas" sob o título (home/HomeScreen.kt: RegisteredUsersLine).
struct HomeRegisteredUsers: View {
    let count: Int

    var body: some View {
        let formatador: NumberFormatter = NumberFormatter()
        formatador.numberStyle = .decimal
        formatador.locale = Locale(identifier: AureaText.language.resolved.rawValue)
        let numero: String = formatador.string(from: NSNumber(value: count)) ?? String(count)
        return HStack(spacing: 6) {
            Circle().fill(AureaColors.success).frame(width: 6, height: 6)
            Text(AureaText.plural("conta_usuarios", count, numero))
                .font(.aurea(size: 12.5)).foregroundStyle(AureaColors.muted)
        }
        .accessibilityElement(children: .combine)
        .accessibilityIdentifier("home.registeredUsers")
    }
}

// =============================================================================
// Omega home · P1-6: a folha "Novo projeto" (par do NewProjectSheet.kt)
// =============================================================================
/// `fps` livre (1–240, decimais como 29,97); `background` = fundo RGB sRGB.
struct NewProjectDraft {
    let width: UInt32; let height: UInt32; let fps: Double; let title: String
    var background: [Float] = [0, 0, 0]
    /// "Transparente": sem fundo (o motor recebe alfa 0; PNG e GIF saem com alfa).
    var transparent = false
    /// Interface do projeto (0.2.5): Composição (padrão) ou Edição.
    var uiMode: UInt32 = AureaUiMode.composition
    /// O que a folha lembra: formato e resolução (nil na Livre), Livre e a corrente.
    var aspectKey: String? = nil
    var resolution: Int? = nil
    var free = false
    var keepRatio = true
}

/// As taxas da caixa de fps (as da referência) + "Personalizado…".
let omegaFpsOptions: [Int] = [12, 15, 18, 20, 24, 25, 30, 48, 50, 60]
/// Os fundos de um toque (os da referência); "Outra cor…" abre o seletor.
let omegaBackgrounds: [(key: String, rgb: [Float])] = [
    ("sh_bg_black", [0, 0, 0]), ("sh_bg_white", [1, 1, 1]), ("om_new_bg_light_grey", [0.8, 0.8, 0.8]),
    ("om_new_bg_green", [0, 0.5, 0]), ("om_new_bg_blue", [0, 0, 1]),
]
/// A ordem dos formatos na folha (a da referência): 16:9, 9:16, 4:5, 1:1, 4:3.
private let omegaSheetAspects = ["16:9", "9:16", "4:5", "1:1", "4:3"]

/// Cobertura preta a 70 % + folha clara (`lightSheet`, cantos de 20) que sobe
/// 500 pt em 200 ms e desce em 200 ms. De cima para baixo: Interface · 35 · Nome
/// · 10 · Proporções · 16 · Resolução (ou L × A com a corrente) · 16 · Taxa de
/// quadros · 16 · Fundo · 16 · "Criar projeto" · ✕ de 60. O gatilho continua o "+".
struct NewProjectSheet: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject var defaults: HomeDefaults
    let suggestedName: String
    let device: [String: NSNumber]
    let onDismiss: () -> Void
    let onCreate: (NewProjectDraft) -> Void
    @State private var aspectKey = "16:9"
    @State private var resolution = 1080
    @State private var fps: Double = 30
    @State private var background: [Float] = [0, 0, 0]
    @State private var transparent = false
    @State private var name = ""
    @State private var free = false
    @State private var freeWidth = "1080"
    @State private var freeHeight = "1350"
    @State private var keepRatio = true
    @State private var lockRatio: Double = 1080.0 / 1350.0
    // A interface NÃO lembra a última escolha: toda folha nova abre na Composição.
    @State private var uiMode = AureaUiMode.composition
    @State private var shown = false
    @State private var created: NewProjectDraft?

    private var aspect: ProjectAspectOption { ProjectPresets.aspectByKey(aspectKey) }
    private var fw: Int { min(7680, max(64, Int(freeWidth) ?? 1080)) }
    private var fh: Int { min(7680, max(64, Int(freeHeight) ?? 1350)) }
    private func frameFor(_ r: Int) -> (width: Int, height: Int) {
        let ratio = Double(aspect.ratio)
        return ratio >= 1 ? (Int((Double(r) * ratio).rounded()), r) : (r, Int((Double(r) / ratio).rounded()))
    }
    private var frame: (width: Int, height: Int) { free ? (fw, fh) : frameFor(resolution) }

    var body: some View {
        GeometryReader { bounds in
            ZStack(alignment: .bottom) {
                AureaColors.homeCover.opacity(shown ? 1 : 0).ignoresSafeArea().onTapGesture(perform: close)
                    .accessibilityLabel(AureaText.t("common_cancel")).accessibilityAddTraits(.isButton)
                VStack(spacing: 0) {
                    ScrollView { content.padding(.horizontal, 20).padding(.top, 24) }
                        .frame(maxHeight: min(575, bounds.size.height - 24))
                        .fixedSize(horizontal: false, vertical: true)
                    Button(action: close) {
                        CupertinoGlyph.text(CupertinoGlyph.Xmark, size: 24, color: AureaColors.lightText)
                            .frame(width: 60, height: 60).contentShape(Rectangle())
                    }.buttonStyle(AureaPressStyle())
                        .accessibilityLabel(AureaText.t("common_cancel"))
                        .accessibilityIdentifier("newProject.cancel")
                        .padding(.top, 12).padding(.bottom, 8)
                }
                .frame(maxWidth: .infinity)
                .background(AureaColors.lightSheet.ignoresSafeArea(edges: .bottom))
                .clipShape(UnevenHomeSheet())
                .offset(y: shown ? 0 : 500)
                .accessibilityElement(children: .contain)
                .accessibilityLabel(AureaText.t("new_project_title"))
                .accessibilityIdentifier("newProject.sheet")
            }
        }
        .onAppear {
            aspectKey = ProjectPresets.aspectByKey(defaults.aspect).key
            resolution = defaults.resolution
            fps = defaults.lastFps ?? Double(defaults.fps)
            free = defaults.lastFree
            freeWidth = String(defaults.lastFreeWidth)
            freeHeight = String(defaults.lastFreeHeight)
            keepRatio = defaults.lastKeepRatio
            lockRatio = Double(defaults.lastFreeWidth) / Double(max(1, defaults.lastFreeHeight))
            let saved = HomeDefaults.parseBackground(defaults.lastBackground)
            background = saved.rgb; transparent = saved.transparent
            withAnimation(.timingCurve(0.4, 0, 0.2, 1, duration: 0.2)) { shown = true }
        }
    }

    private func close() {
        withAnimation(.timingCurve(0, 0, 0.2, 1, duration: 0.2)) { shown = false }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) {
            onDismiss()
            if let created { onCreate(created) }
        }
    }

    private var content: some View {
        VStack(spacing: 0) {
            uiModeToggle
            Text(AureaText.t(uiMode == AureaUiMode.edit ? "ui_mode_edit_hint" : "ui_mode_composition_hint"))
                .font(.aurea(size: 12)).foregroundStyle(AureaColors.lightTextMuted).multilineTextAlignment(.center)
                .frame(maxWidth: 320).padding(.top, 6)
            nameField.padding(.top, 29)
            aspectRow.padding(.top, 10)
            sheetRow(AureaText.t("settings_resolution"), sub1: AureaText.t("om_new_frame_size"), sub2: "\(frame.width) × \(frame.height)") {
                if free { freeFields } else { resolutionBox }
            }.padding(.top, 16)
            if let exportLimit {
                Text(exportLimit).font(.aurea(size: 11)).foregroundStyle(AureaColors.lightTextMuted)
                    .frame(maxWidth: .infinity, alignment: .leading).padding(.top, 6)
            }
            sheetRow(AureaText.t("settings_fps")) { fpsBox }.padding(.top, 16)
            sheetRow(AureaText.t("editor_plano_fundo")) { backgroundBox }.padding(.top, 16)
            HStack {
                Spacer()
                Button(action: create) {
                    Text(AureaText.t("new_project_create")).font(.aurea(size: 16, weight: .bold)).foregroundStyle(AureaColors.lightAccentInk)
                        .padding(.horizontal, 8).frame(minHeight: 48).contentShape(Rectangle())
                }.buttonStyle(AureaPressStyle(shrink: 1)).disabled(created != nil)
                    .accessibilityIdentifier("newProject.create")
            }.padding(.top, 16)
        }
        .frame(maxWidth: 480)
        .frame(maxWidth: .infinity)
    }

    /// (1) A interface como o alternador Projeto · Elemento da referência (44, raio 8).
    private var uiModeToggle: some View {
        OmegaSegmented(values: [AureaUiMode.composition, AureaUiMode.edit], selected: $uiMode,
                       label: { AureaText.t($0 == AureaUiMode.edit ? "ui_mode_edit" : "ui_mode_composition") },
                       height: 44, radius: 8,
                       tag: { $0 == AureaUiMode.edit ? "newProject.uiMode.edit" : "newProject.uiMode.composition" })
            .frame(maxWidth: 320)
    }

    /// (3) O nome: 40 (até 320), 15, dica = nome sugerido, ✕ que limpa.
    private var nameField: some View {
        HStack(spacing: 0) {
            ZStack(alignment: .leading) {
                if name.isEmpty {
                    Text(suggestedName.isEmpty ? AureaText.t("new_project_name_hint") : suggestedName)
                        .font(.aurea(size: 15)).foregroundStyle(AureaColors.lightTextFaint).lineLimit(1)
                }
                TextField("", text: Binding(get: { name }, set: { name = String($0.prefix(120)) }))
                    .font(.aurea(size: 15)).foregroundStyle(AureaColors.lightText).tint(AureaColors.lightAccentInk)
                    .textInputAutocapitalization(.sentences).submitLabel(.done).onSubmit(create)
                    .accessibilityLabel(AureaText.t("new_project_name"))
                    .accessibilityIdentifier("newProject.name")
            }.padding(.leading, 12)
            if !name.isEmpty {
                Button { name = "" } label: {
                    CupertinoGlyph.text(CupertinoGlyph.XmarkCircleFill, size: 18, color: AureaColors.lightTextFaint)
                        .frame(width: 44, height: 40).contentShape(Rectangle())
                }.buttonStyle(.plain)
                    .accessibilityLabel(AureaText.t("om_new_clear_name"))
                    .accessibilityIdentifier("newProject.name.clear")
            }
        }
        .frame(height: 40).frame(maxWidth: 320)
        .background(AureaColors.lightSurface, in: RoundedRectangle(cornerRadius: 8))
    }

    /// (5) As 6 proporções em 56: ícone desenhado na proporção (lado maior 31,
    /// canto 2, borda 1; a escolhida preenchida; a Livre só com as marcas de canto).
    private var aspectRow: some View {
        HStack(spacing: 0) {
            ForEach(omegaSheetAspects + [ProjectPresets.free.key], id: \.self) { key in
                let isFree = key == ProjectPresets.free.key
                let option = isFree ? ProjectPresets.free : ProjectPresets.aspectByKey(key)
                let on = isFree ? free : (!free && aspectKey == key)
                let label = isFree ? AureaText.t("aspect_free") : option.label
                Button {
                    if isFree { free = true } else { free = false; aspectKey = key }
                } label: {
                    VStack(spacing: 4) {
                        OmegaAspectGlyph(ratio: isFree ? 1 : option.ratio, on: on, free: isFree)
                        Text(label).font(.aurea(size: 11, weight: on ? .bold : .regular))
                            .foregroundStyle(on ? AureaColors.lightText : AureaColors.lightTextMuted).lineLimit(1)
                    }.frame(maxWidth: .infinity, maxHeight: .infinity).contentShape(Rectangle())
                }.buttonStyle(.plain)
                    .accessibilityLabel(label)
                    .accessibilityAddTraits(on ? [.isButton, .isSelected] : .isButton)
                    .accessibilityIdentifier("newProject.aspect." + (isFree ? "free" : key.replacingOccurrences(of: ":", with: "x")))
            }
        }
        .frame(height: 56).padding(.horizontal, 4)
    }

    private func sheetRow<Control: View>(_ label: String, sub1: String? = nil, sub2: String? = nil,
                                          @ViewBuilder control: () -> Control) -> some View {
        HStack(spacing: 8) {
            VStack(alignment: .leading, spacing: 0) {
                Text(label).font(.aurea(size: 15, weight: .medium)).foregroundStyle(AureaColors.lightText).lineLimit(2)
                if let sub1 { Text(sub1).font(.aurea(size: 9)).foregroundStyle(AureaColors.lightTextMuted).lineLimit(1) }
                if let sub2 { Text(sub2).font(.aurea(size: 9).monospacedDigit()).foregroundStyle(AureaColors.lightTextMuted).lineLimit(1).keepLtr() }
            }.frame(maxWidth: .infinity, alignment: .leading)
            control()
        }
    }

    private func overLimit(_ w: Int, _ h: Int) -> Bool {
        let short = device["maxExportHeight"]?.intValue ?? 0
        let long = device["maxExportWidth"]?.intValue ?? 0
        return short > 0 && (min(w, h) > short || (long > 0 && max(w, h) > long))
    }

    /// (7) Resolução: caixa 180 × 45 com 720p…2160p ("acima do limite" pelo aparelho).
    private var resolutionBox: some View {
        AureaDropdown(value: "\(resolution)p", tag: "newProject.resolution",
                      accessibilityLabel: AureaText.t("settings_resolution")) {
            ProjectPresets.resolutions.map { (r: Int) -> MenuEntry in
                let f = frameFor(r)
                return .item("\(r)p" + (overLimit(f.width, f.height) ? " " + AureaText.t("om_new_over_limit") : ""),
                             sublabel: ProjectPresets.resolutionLabel(r), selected: r == resolution,
                             tag: "newProject.resolution.\(r)") { resolution = r }
            }
        }
    }

    /// Na Livre: L e A (68 cada) e a corrente que mantém a proporção.
    private var freeFields: some View {
        HStack(spacing: 4) {
            dimension($freeWidth, label: "new_project_width", tag: "newProject.width") { v in
                if keepRatio, let w = Int(v), lockRatio > 0 { freeHeight = String(min(7680, max(1, Int((Double(w) / lockRatio).rounded())))) }
            }
            Text("×").font(.aurea(size: 14)).foregroundStyle(AureaColors.lightTextMuted)
            dimension($freeHeight, label: "new_project_height", tag: "newProject.height") { v in
                if keepRatio, let h = Int(v) { freeWidth = String(min(7680, max(1, Int((Double(h) * lockRatio).rounded())))) }
            }
            Button {
                keepRatio.toggle()
                if keepRatio { lockRatio = Double(fw) / Double(fh) }
            } label: {
                CupertinoGlyph.text(CupertinoGlyph.Link, size: 16, color: keepRatio ? AureaColors.lightOnInk : AureaColors.lightTextMuted)
                    .frame(width: 30, height: 30)
                    .background(keepRatio ? AureaColors.lightInk : Color.clear, in: RoundedRectangle(cornerRadius: 8))
                    .overlay(RoundedRectangle(cornerRadius: 8).stroke(keepRatio ? AureaColors.lightInk : AureaColors.lightDivider, lineWidth: 1))
                    .frame(width: 44, height: 44).contentShape(Rectangle())
            }.buttonStyle(.plain)
                .accessibilityLabel(AureaText.t("om_new_keep_ratio"))
                .accessibilityAddTraits(keepRatio ? [.isButton, .isSelected] : .isButton)
                .accessibilityIdentifier("newProject.keepRatio")
        }.keepLtr()
    }

    private func dimension(_ text: Binding<String>, label: String, tag: String, onEdit: @escaping (String) -> Void) -> some View {
        TextField("", text: Binding(get: { text.wrappedValue }, set: { value in
            let clean = String(value.filter(\.isNumber).prefix(5))
            text.wrappedValue = clean
            onEdit(clean)
        }))
        .font(.aurea(size: 15).monospacedDigit()).foregroundStyle(AureaColors.lightText).tint(AureaColors.lightAccentInk)
        .multilineTextAlignment(.center).keyboardType(.numberPad)
        .frame(width: 68, height: 40)
        .background(AureaColors.lightSurface, in: RoundedRectangle(cornerRadius: 8))
        .accessibilityLabel(AureaText.t(label))
        .accessibilityIdentifier(tag)
    }

    /// (9) Taxa de quadros: 12…60 e "Personalizado…" (teclado 1–240).
    private var fpsBox: some View {
        let isPreset = omegaFpsOptions.contains { abs(Double($0) - fps) < 0.001 }
        return AureaDropdown(value: homeFormatFps(fps) + " fps", tag: "newProject.fps",
                             accessibilityLabel: AureaText.t("settings_fps")) {
            omegaFpsOptions.map { (v: Int) -> MenuEntry in
                .item("\(v) fps", selected: isPreset && abs(Double(v) - fps) < 0.001, tag: "newProject.fps.\(v)") { fps = Double(v) }
            } + [.item(AureaText.t("project_fps_custom"), selected: !isPreset, tag: "newProject.fps.custom") {
                model.numericKeypad = KeypadRequest(title: AureaText.t("project_fps_custom_title"), value: Float(fps),
                                                    unit: "fps", min: 1, max: 240, decimals: 3,
                                                    onValue: { fps = Double($0) })
            }]
        }
    }

    /// (11) Fundo: Preto, Branco, Cinza claro, Verde, Azul, Transparente, Outra cor…
    private var backgroundBox: some View {
        let preset: String? = transparent ? nil
            : omegaBackgrounds.first(where: { p in (0..<3).allSatisfy { abs(p.rgb[$0] - background[$0]) < 0.01 } })?.key
        let current = Color(.sRGB, red: Double(background[0]), green: Double(background[1]), blue: Double(background[2]), opacity: 1)
        let value = transparent ? AureaText.t("sh_bg_transparent")
            : preset.map { AureaText.t($0) } ?? AureaText.t("editor_outra_cor").trimmingCharacters(in: CharacterSet(charactersIn: "…."))
        return AureaDropdown(value: value, swatch: transparent ? .clear : current, tag: "newProject.background",
                             accessibilityLabel: AureaText.t("editor_plano_fundo")) {
            omegaBackgrounds.map { (p: (key: String, rgb: [Float])) -> MenuEntry in
                .item(AureaText.t(p.key), swatch: Color(.sRGB, red: Double(p.rgb[0]), green: Double(p.rgb[1]), blue: Double(p.rgb[2]), opacity: 1),
                      selected: preset == p.key) { background = p.rgb; transparent = false }
            } + [
                .item(AureaText.t("sh_bg_transparent"), swatch: .clear, selected: transparent) { transparent = true },
                .item(AureaText.t("editor_outra_cor"), glyph: CupertinoGlyph.Eyedropper, selected: preset == nil && !transparent) {
                    model.colorSheet = ColorSheetRequest(title: AureaText.t("ds_cor"), initial: background + [1], withAlpha: false,
                                                         onChange: { r, g, b, _ in background = [r, g, b]; transparent = false }, onDone: {})
                },
            ]
        }
    }

    private var exportLimit: String? {
        let short = device["maxExportHeight"]?.intValue ?? 0
        guard overLimit(frame.width, frame.height) else { return nil }
        // A mesma nota do Android: a edição continua possível.
        return AureaText.t("ios_home_export_limit_note", short == 2160 ? "4K" : "\(short)p", Int(frame.width), Int(frame.height))
    }

    private func create() {
        guard created == nil else { return }
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        let title = trimmed.isEmpty ? (suggestedName.isEmpty ? AureaText.t("new_project_untitled") : suggestedName) : trimmed
        created = NewProjectDraft(width: UInt32(frame.width), height: UInt32(frame.height), fps: fps, title: title,
                                  background: background, transparent: transparent, uiMode: uiMode,
                                  aspectKey: free ? nil : aspectKey, resolution: free ? nil : resolution,
                                  free: free, keepRatio: keepRatio)
        close()
    }
}

/// O ícone de uma proporção, desenhado em código (a Livre só com as marcas de canto).
struct OmegaAspectGlyph: View {
    let ratio: CGFloat
    let on: Bool
    let free: Bool
    var body: some View {
        let side: CGFloat = 31
        let w = ratio >= 1 ? side : side * ratio
        let h = ratio >= 1 ? side / ratio : side
        let ink = on ? AureaColors.lightInk : AureaColors.lightTextMuted
        ZStack {
            if free {
                Canvas { context, size in
                    let m: CGFloat = 5.5
                    var path = Path()
                    for (p, d) in [(CGPoint(x: 0, y: 0), CGPoint(x: 1, y: 1)), (CGPoint(x: size.width, y: 0), CGPoint(x: -1, y: 1)),
                                   (CGPoint(x: 0, y: size.height), CGPoint(x: 1, y: -1)), (CGPoint(x: size.width, y: size.height), CGPoint(x: -1, y: -1))] {
                        path.move(to: CGPoint(x: p.x + d.x * m, y: p.y)); path.addLine(to: p); path.addLine(to: CGPoint(x: p.x, y: p.y + d.y * m))
                    }
                    context.stroke(path, with: .color(ink), lineWidth: on ? 2 : 1)
                }.frame(width: w, height: h)
            } else if on {
                RoundedRectangle(cornerRadius: 2).fill(ink).frame(width: w, height: h)
            } else {
                RoundedRectangle(cornerRadius: 2).strokeBorder(ink, lineWidth: 1).frame(width: w, height: h)
            }
        }.frame(width: side, height: side)
    }
}

/// iOS 16-compatible top corners (Android ModalBottomSheet radius 20).
private struct UnevenHomeSheet: Shape {
    func path(in rect: CGRect) -> Path {
        Path(UIBezierPath(roundedRect: rect, byRoundingCorners: [.topLeft, .topRight],
                          cornerRadii: CGSize(width: 20, height: 20)).cgPath)
    }
}

/// O seletor segmentado da referência (gaveta e folha "Novo projeto"):
/// contêiner `lightSurface` (raio 5, folga 3); o ativo é a tinta forte com o
/// texto `lightOnInk`, os outros `lightText`. Par do `OmegaSegmented` do Android.
struct OmegaSegmented<Value: Hashable>: View {
    let values: [Value]
    @Binding var selected: Value
    let label: (Value) -> String
    var height: CGFloat = 38
    var radius: CGFloat = 5
    var tag: ((Value) -> String)? = nil
    var body: some View {
        HStack(spacing: 0) {
            ForEach(values, id: \.self) { value in
                let on = value == selected
                Button { selected = value } label: {
                    Text(label(value)).font(.aurea(size: 12, weight: .semibold))
                        .foregroundStyle(on ? AureaColors.lightOnInk : AureaColors.lightText)
                        .multilineTextAlignment(.center).lineLimit(2).padding(.horizontal, 4)
                        .frame(maxWidth: .infinity, minHeight: height - 6)
                        .background(on ? AureaColors.lightInk : Color.clear, in: RoundedRectangle(cornerRadius: radius))
                        .contentShape(Rectangle())
                }.buttonStyle(.plain)
                    .accessibilityAddTraits(on ? [.isButton, .isSelected] : .isButton)
                    .modifier(OptionalIdentifier(tag: tag?(value)))
            }
        }
        .padding(3)
        .frame(minHeight: height)
        .background(AureaColors.lightSurface, in: RoundedRectangle(cornerRadius: radius))
    }
}

// =============================================================================
// Omega home · P0-3: a gaveta de ajustes (par do SettingsTab.kt)
// =============================================================================
/// Cartões de raio 6 no papel `lightSurfaceAlt`, 6 das bordas, 15 entre eles;
/// linhas de 44; rótulo 12 negrito; texto a 15 da borda do cartão. Ordem: Criar
/// · Opções · grupos do AUREA · Sobre · Suporte · Acompanhe o criador · doação,
/// licenças, política, faixa beta e ferramentas de desenvolvedor.
struct HomeSettingsDrawer: View {
    @ObservedObject var model: AureaModel
    @ObservedObject var defaults: HomeDefaults
    let onImportMedia: () -> Void
    let onImportProject: () -> Void
    let onHyper: () -> Void
    let onAbout: () -> Void
    let onSupport: () -> Void
    @Environment(\.openURL) private var openURL
    @State private var languageMenu = false
    @State private var keyDialog = false
    @State private var groqKey = ""
    @State private var hasGroqKey = false
    @State private var betaTaps = 0
    @State private var licenses = false
    @State private var performanceTest = false
    @AppStorage("home.developerTools") private var developerTools = false
    @AppStorage(FxEffectPrefs.previewsEnabledKey) private var effectPreviewsEnabled = true
    /// "Otimizar vídeos pesados" (0.2.5): modo e mídia otimizada (ProxyOptimizer.swift).
    @ObservedObject private var proxies = ProxyOptimizer.shared

    private var version: String { Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "?" }
    private var build: String { Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "?" }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 15) {
                // Dois grupos: o ViewBuilder do iOS 16 aceita até 10 filhos por bloco.
                Group {
                Text(AureaText.t("home_title_settings")).font(OmegaHomeType.drawerTitle).foregroundStyle(AureaColors.lightText)
                    .frame(height: 50).padding(.leading, 5).padding(.top, 5)
                    .accessibilityAddTraits(.isHeader).accessibilityIdentifier("home.drawer.title")
                card {
                    tile(CupertinoGlyph.PhotoOnRectangle, "home_import_media", tag: "home.drawer.importMedia", action: onImportMedia)
                    divider
                    tile(CupertinoGlyph.ArrowDownToLine, "project_file_import", tag: "home.drawer.importProject", action: onImportProject)
                    divider
                    tile(CupertinoGlyph.Bolt, "hyper_title", tag: "home.drawer.hyper", action: onHyper)
                }
                card {
                    switchRow("om_drawer_light_preview", note: "om_drawer_light_preview_note",
                              isOn: Binding(get: { defaults.lightPreview }, set: { defaults.lightPreview = $0 }), tag: "home.drawer.lowPreview")
                }
                group("settings_group_defaults") {
                    segmentedRow("settings_aspect", values: ProjectPresets.aspects.map(\.key),
                                 selected: Binding(get: { defaults.aspect }, set: { defaults.aspect = $0; defaults.lastFree = false }), label: { $0 })
                    divider
                    segmentedRow("settings_resolution", values: ProjectPresets.resolutions, selected: $defaults.resolution,
                                 label: { $0 == 2160 ? "4K" : "\($0)p" })
                    divider
                    segmentedRow("settings_fps", values: ProjectPresets.fpsOptions,
                                 selected: Binding(get: { defaults.fps }, set: { defaults.fps = $0; defaults.lastFps = nil }), label: { "\($0)" })
                    note("settings_defaults_note")
                }
                group("settings_group_language") {
                    tapRow("settings_language", subtitle: model.language.label, tag: "home.drawer.language") { languageMenu = true }
                        .aureaAnchoredMenu(isPresented: $languageMenu, tag: "home.drawer.language.menu") {
                            AureaLanguage.allCases.map { (language: AureaLanguage) -> MenuEntry in
                                .item(language.label, checked: language == model.language) { model.language = language }
                            }
                        }
                    note("settings_language_note")
                }
                group("settings_group_theme") {
                    themePicker
                    note("settings_theme_note")
                }
                group("settings_group_captions") {
                    tapRow("settings_groq_key", subtitle: AureaText.t(hasGroqKey ? "settings_groq_set" : "settings_groq_unset")) {
                        groqKey = ""; hasGroqKey = !CaptionKeychain.read().isEmpty; keyDialog = true
                    }
                    note("settings_groq_note")
                }
                group("settings_group_device") {
                    tapRow("settings_device_auto", subtitle: deviceSummary) { model.analyseDeviceAgain() }
                    divider
                    tapRow("ios_perf_test_button", subtitle: nil) { performanceTest = true }
                    note("settings_device_auto_note")
                    divider
                    // Par do ProxyGroup: o QUE otimizar é do motor; aqui o modo e a limpeza.
                    segmentedRow("proxy_settings_title", values: ProxyOptimizer.Mode.allCases,
                                 selected: Binding(get: { proxies.mode }, set: { proxies.select($0) }),
                                 label: { AureaText.t($0.labelKey) })
                    divider
                    tapRow("proxy_clear_title_unknown", subtitle: AureaText.t("proxy_clear_note"),
                           literalTitle: proxies.cacheBytes >= 0 ? AureaText.t("proxy_clear_title", ProxyOptimizer.megabytes(proxies.cacheBytes)) : nil) {
                        proxies.clearCache { mb in model.toast = AureaText.t("proxy_cleared", mb) }
                    }
                    note("proxy_settings_note")
                }
                .onAppear { proxies.refreshCacheSize() }
                }
                Group {
                group("settings_group_general") {
                    tapRow("settings_clear_cache", subtitle: AureaText.t("settings_clear_cache_note")) { model.clearHomeCaches() }
                    divider
                    switchRow("settings_effect_previews", note: "settings_effect_previews_note", isOn: $effectPreviewsEnabled, tag: nil)
                    divider
                    tapRow("settings_clear_recents", subtitle: AureaText.t("settings_clear_recents_note")) {
                        FxEffectPrefs().clearRecents()
                        model.toast = AureaText.t("settings_recents_cleared")
                    }
                    if UmpConsent.privacyOptionsRequired {
                        divider
                        tapRow("settings_ad_privacy", subtitle: AureaText.t("settings_ad_privacy_note")) { UmpConsent.presentPrivacyOptions() }
                    }
                }
                card { tapRow("settings_group_about", subtitle: nil, tag: "home.drawer.about", action: onAbout) }
                card { tapRow("om_drawer_support", subtitle: AureaText.t("report_title"), tag: "home.drawer.support", action: onSupport) }
                card {
                    Text(AureaText.t("social_title")).font(.aurea(size: 11, weight: .semibold)).foregroundStyle(AureaColors.lightTextMuted)
                        .padding(.horizontal, 15).padding(.top, 12).padding(.bottom, 4).accessibilityAddTraits(.isHeader)
                    ForEach(Array(omegaCreatorProfiles.enumerated()), id: \.offset) { index, profile in
                        if index > 0 { divider }
                        Button {
                            if let url = URL(string: profile.url) { openURL(url) }
                        } label: {
                            HStack(spacing: 10) {
                                CupertinoGlyph.text(profile.glyph, size: 18, color: AureaColors.lightOnInk)
                                    .frame(width: 38, height: 38).background(AureaColors.lightInk, in: Circle())
                                Text(profile.label).font(OmegaHomeType.drawerLabel).foregroundStyle(AureaColors.lightText)
                                    .frame(maxWidth: .infinity, alignment: .leading)
                                CupertinoGlyph.text(CupertinoGlyph.ChevronRight, size: 14, color: AureaColors.lightTextMuted).mirrorsInRtl()
                            }.padding(.horizontal, 15).padding(.vertical, 3).frame(minHeight: 44).contentShape(Rectangle())
                        }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityIdentifier(profile.tag)
                    }
                }
                DonationCard()
                card {
                    tapRow("licenses_oss_title", subtitle: "AureaHyper · FFmpeg · ncnn · MiDaS · U2-Net") { licenses = true }
                    divider
                    // A política publicada também fica acessível dentro do app (par do Android).
                    tapRow("settings_privacy_policy", subtitle: AureaText.t("settings_privacy_policy_note")) {
                        if let url = URL(string: "https://aurea-ai-discovery.aureaapp.workers.dev/privacidade") { openURL(url) }
                    }
                    divider
                    Text(AureaText.t("ios_version_build", version, build)).font(OmegaHomeType.drawerNote).foregroundStyle(AureaColors.lightTextMuted)
                        .padding(.horizontal, 15).frame(minHeight: 44, alignment: .leading)
                        .accessibilityIdentifier("app.version")
                }
                betaBanner
                if developerTools {
                    card {
                        Text(AureaText.t("settings_dev_tools") + " · " + AureaText.t("ios_version_build", version, build))
                            .font(OmegaHomeType.drawerLabel).foregroundStyle(AureaColors.lightText)
                            .padding(.horizontal, 15).frame(minHeight: 44, alignment: .leading)
                    }
                }
                Text(AureaText.t("settings_made_by")).font(OmegaHomeType.drawerNote).foregroundStyle(AureaColors.lightTextMuted)
                    .frame(maxWidth: .infinity).padding(.bottom, 24)
                }
            }
            .padding(.horizontal, 6)
        }
        .foregroundStyle(AureaColors.lightText)
        .overlay {
            if keyDialog {
                AureaDialog(title: AureaText.t("settings_groq_key"), message: AureaText.t("settings_groq_note"),
                            actions: [DialogAction(AureaText.t("common_save"), role: .positive,
                                                   enabled: !groqKey.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty) { saveGroqKey(groqKey) }]
                                + (hasGroqKey ? [DialogAction(AureaText.t("common_remove"), role: .neutral, destructive: true) { saveGroqKey("") }] : [])
                                + [DialogAction(AureaText.t("common_cancel"), role: .negative)],
                            onDismiss: { keyDialog = false }, tag: "home.drawer.groq",
                            content: AnyView(SecureField("gsk_…", text: $groqKey).font(.aurea(size: 15))
                                .textInputAutocapitalization(.never).autocorrectionDisabled()
                                .foregroundStyle(AureaColors.lightText)
                                .padding(.horizontal, 10).frame(height: 40)
                                .background(AureaColors.lightSurface, in: RoundedRectangle(cornerRadius: 6))
                                .accessibilityIdentifier("dialog.field")))
            }
        }
        .onAppear { hasGroqKey = !CaptionKeychain.read().isEmpty }
        .sheet(isPresented: $performanceTest) { PerformanceTestPanel() }
        .fullScreenCover(isPresented: $licenses) { LicensesView { licenses = false } }
    }

    // MARK: peças

    private func card<Content: View>(@ViewBuilder _ content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 0, content: content)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(AureaColors.lightSurfaceAlt, in: RoundedRectangle(cornerRadius: 6))
    }
    private func group<Content: View>(_ header: String, @ViewBuilder _ content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(AureaText.t(header)).font(.aurea(size: 11, weight: .semibold)).foregroundStyle(AureaColors.lightTextMuted)
                .padding(.horizontal, 15).accessibilityAddTraits(.isHeader)
            card(content)
        }
    }
    private var divider: some View {
        Rectangle().fill(AureaColors.lightDivider).frame(height: 1).padding(.leading, 15)
    }
    private func note(_ key: String) -> some View {
        Text(AureaText.t(key)).font(OmegaHomeType.drawerNote).foregroundStyle(AureaColors.lightTextMuted)
            .frame(maxWidth: .infinity, alignment: .leading).padding(.horizontal, 15).padding(.top, 4).padding(.bottom, 10)
    }
    private func tile(_ glyph: Character, _ title: String, tag: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 10) {
                CupertinoGlyph.text(glyph, size: 22, color: AureaColors.lightText).frame(width: 38, height: 38)
                Text(AureaText.t(title)).font(OmegaHomeType.drawerLabel).foregroundStyle(AureaColors.lightText)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }.padding(.horizontal, 15).padding(.vertical, 3).frame(minHeight: 44).contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityIdentifier(tag)
    }
    /// `literalTitle`: texto já montado (com número) no lugar da chave.
    private func tapRow(_ title: String, subtitle: String?, literalTitle: String? = nil, tag: String? = nil,
                        action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 0) {
                VStack(alignment: .leading, spacing: 1) {
                    Text(literalTitle ?? AureaText.t(title)).font(OmegaHomeType.drawerLabel).foregroundStyle(AureaColors.lightText)
                    if let subtitle { Text(subtitle).font(OmegaHomeType.drawerNote).foregroundStyle(AureaColors.lightTextMuted) }
                }.frame(maxWidth: .infinity, alignment: .leading)
                CupertinoGlyph.text(CupertinoGlyph.ChevronRight, size: 14, color: AureaColors.lightTextMuted).mirrorsInRtl()
            }.padding(.horizontal, 15).padding(.vertical, 6).frame(minHeight: 44).contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle(shrink: 1)).modifier(OptionalIdentifier(tag: tag))
    }
    private func switchRow(_ title: String, note: String, isOn: Binding<Bool>, tag: String?) -> some View {
        Toggle(isOn: isOn) {
            VStack(alignment: .leading, spacing: 1) {
                Text(AureaText.t(title)).font(OmegaHomeType.drawerLabel).foregroundStyle(AureaColors.lightText)
                Text(AureaText.t(note)).font(OmegaHomeType.drawerNote).foregroundStyle(AureaColors.lightTextMuted)
            }
        }
        .tint(AureaColors.accent)
        .padding(.horizontal, 15).padding(.vertical, 6).frame(minHeight: 44)
        .accessibilityLabel(AureaText.t(title))
        .accessibilityHint(AureaText.t(note))
        .modifier(OptionalIdentifier(tag: tag))
    }
    private func segmentedRow<Value: Hashable>(_ key: String, values: [Value], selected: Binding<Value>,
                                               label: @escaping (Value) -> String) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(AureaText.t(key)).font(OmegaHomeType.drawerLabel).foregroundStyle(AureaColors.lightText)
            OmegaSegmented(values: values, selected: selected, label: label)
        }.padding(.horizontal, 15).padding(.vertical, 10)
    }
    /// Temas como amostras: o fundo do tema com o destaque por cima, o nome embaixo.
    private var themePicker: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 12) {
                ForEach(AureaPalette.all) { palette in
                    let on = palette.id == model.themeId
                    Button { model.setTheme(palette.id) } label: {
                        VStack(spacing: 4) {
                            ZStack {
                                Circle().fill(palette.background)
                                Circle().fill(palette.accent).frame(width: 18, height: 18)
                                Circle().strokeBorder(on ? AureaColors.lightAccentInk : AureaColors.lightDivider, lineWidth: on ? 3 : 1)
                            }.frame(width: 44, height: 44)
                            Text(AureaText.t("theme_" + palette.id)).font(.aurea(size: 10, weight: on ? .bold : .regular))
                                .foregroundStyle(on ? AureaColors.lightText : AureaColors.lightTextMuted).lineLimit(1)
                        }.frame(width: 56)
                    }.buttonStyle(.plain).accessibilityLabel(AureaText.t("theme_" + palette.id))
                        .accessibilityAddTraits(on ? .isSelected : [])
                }
            }.padding(.horizontal, 12).padding(.vertical, 12)
        }
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
                CupertinoGlyph.text(CupertinoGlyph.ExclamationmarkTriangle, size: 18, color: HomeColors.beta)
                VStack(alignment: .leading, spacing: 2) {
                    Text(AureaText.t("settings_beta_title", version)).font(OmegaHomeType.drawerLabel).foregroundStyle(AureaColors.lightText)
                    Text(AureaText.t("settings_beta_body")).font(OmegaHomeType.drawerNote).foregroundStyle(AureaColors.lightTextMuted)
                }.frame(maxWidth: .infinity, alignment: .leading)
            }.padding(.horizontal, 15).padding(.vertical, 10)
                .background(HomeColors.betaFill, in: RoundedRectangle(cornerRadius: 6))
                .overlay(RoundedRectangle(cornerRadius: 6).stroke(HomeColors.betaBorder, lineWidth: 1))
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
        let clock = mhz > 0 ? " " + AureaText.t("ios_device_clock_up_to", String(format: "%.1f GHz", Double(mhz) / 1000)) : ""
        return AureaText.t("ios_device_summary", cores, topology, clock, String(format: "%.1f GB", ram), String(format: "%.1f GB", budget))
    }
    private func saveGroqKey(_ value: String) {
        if CaptionKeychain.save(value) { hasGroqKey = !value.isEmpty; groqKey = ""; keyDialog = false }
        else { model.toast = AureaText.t("ios_key_save_failed") }
    }
}

// =============================================================================
// Estado compartilhado pelas abas (mesmas chaves do HomeViewModel do Android)
// =============================================================================
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

    // --- Omega home · P1-6: o que a folha "Novo projeto" lembra entre aberturas.
    @Published var lastFree = UserDefaults.standard.bool(forKey: "newProject.free") {
        didSet { UserDefaults.standard.set(lastFree, forKey: "newProject.free") }
    }
    @Published var lastFreeWidth = (UserDefaults.standard.object(forKey: "newProject.freeWidth") as? Int) ?? 1080 {
        didSet { UserDefaults.standard.set(lastFreeWidth, forKey: "newProject.freeWidth") }
    }
    @Published var lastFreeHeight = (UserDefaults.standard.object(forKey: "newProject.freeHeight") as? Int) ?? 1350 {
        didSet { UserDefaults.standard.set(lastFreeHeight, forKey: "newProject.freeHeight") }
    }
    @Published var lastKeepRatio = (UserDefaults.standard.object(forKey: "newProject.keepRatio") as? Bool) ?? true {
        didSet { UserDefaults.standard.set(lastKeepRatio, forKey: "newProject.keepRatio") }
    }
    /// Nil = vale o padrão dos Ajustes (mudar o padrão esquece a última taxa).
    @Published var lastFps = UserDefaults.standard.object(forKey: "newProject.fps") as? Double {
        didSet { UserDefaults.standard.set(lastFps, forKey: "newProject.fps") }
    }
    /// "r,g,b" (0–1) ou "t" (transparente).
    @Published var lastBackground = UserDefaults.standard.string(forKey: "newProject.background") {
        didSet { UserDefaults.standard.set(lastBackground, forKey: "newProject.background") }
    }
    // --- Omega home · P2-1: "Prévia leve" (o editor abre com a prévia em 1/2).
    @Published var lightPreview = UserDefaults.standard.bool(forKey: "settings.lightPreview") {
        didSet { UserDefaults.standard.set(lightPreview, forKey: "settings.lightPreview") }
    }

    func remember(_ draft: NewProjectDraft) {
        lastFree = draft.free
        lastFreeWidth = Int(draft.width)
        lastFreeHeight = Int(draft.height)
        lastKeepRatio = draft.keepRatio
        lastFps = draft.fps
        lastBackground = draft.transparent ? "t" : draft.background.prefix(3).map { String($0) }.joined(separator: ",")
        if let key = draft.aspectKey { aspect = key }
        if let r = draft.resolution { resolution = r }
    }

    /// "r,g,b" → cor; "t" → transparente; o resto → preto (par do `parseBackground` do Android).
    static func parseBackground(_ saved: String?) -> (rgb: [Float], transparent: Bool) {
        if saved == "t" { return ([0, 0, 0], true) }
        let parts = (saved ?? "").split(separator: ",").compactMap { Float($0.trimmingCharacters(in: .whitespaces)) }
        if parts.count == 3, parts.allSatisfy({ (0...1).contains($0) }) { return (parts, false) }
        return ([0, 0, 0], false)
    }
}

// =============================================================================
// Omega home · P1-7: a tela Sobre (par do AboutScreen.kt)
// =============================================================================
struct HomeAboutView: View {
    @EnvironmentObject private var model: AureaModel
    @Environment(\.openURL) private var openURL
    let onClose: () -> Void
    @State private var licenses = false

    private var rows: [(String, String)] {
        let info = Bundle.main
        let version = info.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "?"
        let build = info.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "?"
        var system = utsname(); uname(&system)
        let machine = withUnsafePointer(to: &system.machine) { $0.withMemoryRebound(to: CChar.self, capacity: 1) { String(cString: $0) } }
        let locale = Locale(identifier: AureaText.language.resolved.rawValue)
        let memory = omegaFileSize(Int64(ProcessInfo.processInfo.physicalMemory), locale: locale)
        let values = try? URL(fileURLWithPath: NSHomeDirectory()).resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey, .volumeTotalCapacityKey])
        let storage: String = {
            guard let free = values?.volumeAvailableCapacityForImportantUsage, let total = values?.volumeTotalCapacity else { return "—" }
            return AureaText.t("om_about_storage_value", omegaFileSize(free, locale: locale), omegaFileSize(Int64(total), locale: locale))
        }()
        let report = model.deviceReport
        let exportW = report["maxExportWidth"]?.intValue ?? 0, exportH = report["maxExportHeight"]?.intValue ?? 0
        return [
            (AureaText.t("om_about_version"), version),
            (AureaText.t("om_about_build"), build),
            (AureaText.t("om_about_device"), "\(UIDevice.current.model) (\(machine))"),
            (AureaText.t("om_about_chipset"), AureaText.t("om_about_cores", ProcessInfo.processInfo.activeProcessorCount)),
            (AureaText.t("om_about_system"), "\(UIDevice.current.systemName) \(UIDevice.current.systemVersion)"),
            (AureaText.t("om_about_memory"), memory),
            (AureaText.t("om_about_storage"), storage),
            (AureaText.t("om_about_export_max"), exportW > 0 && exportH > 0 ? "\(exportW) × \(exportH)" : "—"),
            (AureaText.t("om_about_languages"), model.language.label + " · " + Locale.preferredLanguages.prefix(3).joined(separator: ", ")),
        ]
    }

    var body: some View {
        let table = rows
        VStack(spacing: 0) {
            ZStack {
                Text(AureaText.t("settings_group_about")).font(.aurea(size: 17, weight: .bold)).foregroundStyle(AureaColors.text)
                    .accessibilityAddTraits(.isHeader)
                HStack {
                    Spacer()
                    Button(action: onClose) {
                        CupertinoGlyph.text(CupertinoGlyph.Xmark, size: 20, color: AureaColors.text)
                            .frame(width: 44, height: 44).contentShape(Rectangle())
                    }.buttonStyle(AureaPressStyle())
                        .accessibilityLabel(AureaText.t("editor_fechar"))
                        .accessibilityIdentifier("about.close")
                }.padding(.trailing, 8)
            }.frame(height: 56)
            ScrollView {
                VStack(spacing: 0) {
                    HomeBrandLogo().frame(width: 72, height: 72)
                    Text("AUREA").font(.aurea(size: 24, weight: .bold)).tracking(2).foregroundStyle(AureaColors.text).padding(.top, 12)
                    Text(AureaText.t("settings_made_by")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                        .multilineTextAlignment(.center).padding(.top, 4)
                    Button {
                        UIPasteboard.general.string = table.map { "\($0.0): \($0.1)" }.joined(separator: "\n")
                        model.toast = AureaText.t("om_about_copied")
                    } label: {
                        VStack(spacing: 0) {
                            ForEach(Array(table.enumerated()), id: \.offset) { _, row in
                                HStack(spacing: 8) {
                                    Text(row.0).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted).frame(width: 120, alignment: .leading)
                                    Text(row.1).font(.aurea(size: 13, weight: .medium)).foregroundStyle(AureaColors.text)
                                        .multilineTextAlignment(.trailing).frame(maxWidth: .infinity, alignment: .trailing)
                                }.padding(.horizontal, 14).padding(.vertical, 5).frame(minHeight: 32)
                            }
                        }.padding(.vertical, 6).frame(maxWidth: 480)
                            .background(AureaColors.surface, in: RoundedRectangle(cornerRadius: 12))
                    }.buttonStyle(AureaPressStyle(shrink: 1)).padding(.top, 24)
                        .accessibilityIdentifier("about.table")
                    Text(AureaText.t("om_about_copy_hint")).font(.aurea(size: 11)).foregroundStyle(AureaColors.subtle).padding(.top, 8)
                    HStack(spacing: 20) {
                        Button(AureaText.t("settings_privacy_policy")) {
                            if let url = URL(string: "https://aurea-ai-discovery.aureaapp.workers.dev/privacidade") { openURL(url) }
                        }.accessibilityIdentifier("about.privacy")
                        Button(AureaText.t("licenses_oss_title")) { licenses = true }.accessibilityIdentifier("about.licenses")
                    }.font(.aurea(size: 12)).underline().foregroundStyle(AureaColors.accent).frame(minHeight: 44).padding(.top, 24)
                }.padding(.horizontal, 24).padding(.vertical, 16)
            }
        }
        .background(AureaColors.editorPanel.ignoresSafeArea())
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("about.screen")
        .fullScreenCover(isPresented: $licenses) { LicensesView { licenses = false } }
    }
}

// =============================================================================
// Omega home · P2-2: as Novidades em tela cheia (par do ReleaseNotes.kt)
// =============================================================================
struct OmegaReleaseNotes: View {
    let onClose: () -> Void
    @State private var page = 0
    private var parts: (header: String, pages: [[String]]) {
        let lines = AureaText.t("release_notes_body").split(separator: "\n").map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
        let header = lines.first ?? ""
        let items = lines.dropFirst().map { $0.hasPrefix("•") ? String($0.dropFirst()).trimmingCharacters(in: .whitespaces) : $0 }
        var pages: [[String]] = []
        var i = 0
        while i < items.count { pages.append(Array(items[i..<min(items.count, i + 4)])); i += 4 }
        return (header, pages.isEmpty ? [[]] : pages)
    }
    var body: some View {
        let notes = parts
        VStack(spacing: 0) {
            ZStack {
                Text(AureaText.t("release_notes_title")).font(.aurea(size: 17, weight: .bold)).foregroundStyle(AureaColors.text)
                    .accessibilityAddTraits(.isHeader)
                HStack {
                    Spacer()
                    Button(action: onClose) {
                        CupertinoGlyph.text(CupertinoGlyph.Xmark, size: 20, color: AureaColors.text)
                            .frame(width: 44, height: 44).contentShape(Rectangle())
                    }.buttonStyle(AureaPressStyle())
                        .accessibilityLabel(AureaText.t("editor_fechar"))
                        .accessibilityIdentifier("releaseNotes.close")
                }.padding(.trailing, 8)
            }.frame(height: 56)
            Text(notes.header).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted).multilineTextAlignment(.center)
                .padding(.horizontal, 24)
            TabView(selection: $page) {
                ForEach(Array(notes.pages.enumerated()), id: \.offset) { index, items in
                    ScrollView {
                        VStack(alignment: .leading, spacing: 14) {
                            ForEach(Array(items.enumerated()), id: \.offset) { _, note in
                                HStack(alignment: .top, spacing: 10) {
                                    Text("•").font(.aurea(size: 15)).foregroundStyle(AureaColors.accent)
                                    Text(note).font(.aurea(size: 15)).foregroundStyle(AureaColors.text)
                                        .frame(maxWidth: .infinity, alignment: .leading)
                                }
                            }
                        }.padding(.horizontal, 24).padding(.vertical, 16)
                    }.tag(index)
                }
            }
            .tabViewStyle(.page(indexDisplayMode: .never))
            if notes.pages.count > 1 {
                HStack(spacing: 6) {
                    ForEach(0..<notes.pages.count, id: \.self) { i in
                        Circle().fill(i == page ? AureaColors.accent : AureaColors.muted)
                            .frame(width: i == page ? 10 : 6, height: i == page ? 10 : 6)
                    }
                }.frame(height: 10).padding(.top, 8).padding(.bottom, 30)
            } else {
                Spacer().frame(height: 30)
            }
        }
        .background(AureaColors.background.ignoresSafeArea())
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("releaseNotes.screen")
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
                    DispatchQueue.main.async { parent.onError(error?.localizedDescription ?? AureaText.t("ios_media_open_failed")) }
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

/// A INTERFACE do projeto em dois cartões (Composição, com o selo "Padrão", e
/// Edição). Fica para os Ajustes do projeto no editor (escuro, AppPanels.swift);
/// a folha "Novo projeto" usa o alternador da referência (`OmegaSegmented`).
struct UiModeChoice: View {
    let selected: UInt32
    let onSelect: (UInt32) -> Void

    var body: some View {
        HStack(alignment: .top, spacing: 10) {
            card(AureaUiMode.composition, title: "ui_mode_composition", hint: "ui_mode_composition_hint", id: "newProject.uiMode.composition")
            card(AureaUiMode.edit, title: "ui_mode_edit", hint: "ui_mode_edit_hint", id: "newProject.uiMode.edit")
        }
    }

    private func card(_ mode: UInt32, title: String, hint: String, id: String) -> some View {
        let active = mode == selected
        return Button { onSelect(mode) } label: {
            VStack(alignment: .leading, spacing: 4) {
                HStack(spacing: 6) {
                    Text(AureaText.t(title)).font(.aurea(size: 15, weight: .bold))
                        .foregroundStyle(active ? AureaColors.accent : AureaColors.text).lineLimit(1)
                    if mode == AureaUiMode.composition {
                        Text(AureaText.t("ui_mode_default_badge")).font(.aurea(size: 10.5, weight: .bold))
                            .foregroundStyle(AureaColors.background).padding(.horizontal, 5).padding(.vertical, 1)
                            .background(AureaColors.accent, in: RoundedRectangle(cornerRadius: 6))
                    }
                }
                Text(AureaText.t(hint)).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                    .multilineTextAlignment(.leading).fixedSize(horizontal: false, vertical: true)
            }
            .padding(12).frame(maxWidth: .infinity, alignment: .leading)
            .background(active ? AureaColors.accentDim : AureaColors.surfaceHigh, in: RoundedRectangle(cornerRadius: 14))
            .overlay(RoundedRectangle(cornerRadius: 14).stroke(active ? AureaColors.accent : AureaColors.hairline, lineWidth: active ? 2 : 1))
        }
        .buttonStyle(AureaPressStyle(shrink: 0.98))
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(AureaText.t(title) + ". " + AureaText.t(hint))
        .accessibilityAddTraits(active ? [.isButton, .isSelected] : .isButton)
        .accessibilityIdentifier(id)
    }
}
