// Port of editor/TopBars.kt, Transport.kt and ChromeKit.kt.
import SwiftUI
import UIKit
import PhotosUI
import AVFoundation

// =============================================================================
// Omega chrome (área 02): glifos, papéis de cor e popups ancorados da moldura
// =============================================================================

/// Glifos do CupertinoIcons.ttf que a moldura usa (conferidos na fonte; os
/// mesmos codepoints do `ShellGlyph` do Android).
enum OmegaGlyph {
    /// Sair para os projetos (seta saindo da caixa para a esquerda).
    static let exitBox: Character = "\u{F90C}"
    /// ⋮ (menu da camada).
    static let ellipsisVertical: Character = "\u{F8DA}"
    /// Lupa com + (modo zoom do canvas).
    static let zoomIn: Character = "\u{F8AC}"
    /// Painel lateral à direita (abre e fecha a coluna de modos).
    static let sidebarRight: Character = "\u{F7D7}"
    /// «« (o primeiro item da trilha de grupos).
    static let chevronLeft2: Character = "\u{F5DA}"
    /// Grade de quadradinhos (pixels reais).
    static let pixelGrid: Character = "\u{F808}"
    /// Destaque da camada escolhida: normal e só ela (cheio).
    static let soloOff: Character = "\u{F7DF}"
    static let soloOn: Character = "\u{F7E0}"
    /// Levar uma ponta até o cabeçote (barra inferior da seleção).
    static let arrowLeftSquare: Character = "\u{F505}"
    static let arrowRightSquare: Character = "\u{F512}"
}

/// Os papéis da moldura da referência, todos pelos tokens da paleta (no Omega:
/// A10 barra, A9 camada, S9 coluna, S10 trilha). Par do `OmegaChrome` do Android.
enum OmegaChrome {
    /// Barras do projeto e de reprodução (A10).
    static var bar: Color { AureaColors.surface }
    /// Barra da camada escolhida (A9).
    static var layerBar: Color { AureaColors.surfaceHigh }
    /// Coluna de modos, barra inferior da seleção e o girador da grade (S9).
    static var column: Color { AureaColors.chipHigh }
    /// O primeiro item da trilha de grupos (S10).
    static var trailRoot: Color { AureaColors.editorRowPill }
    /// Caixa do zoom na coluna (A10).
    static var zoomChip: Color { AureaColors.surface }
    /// Texto secundário (P5).
    static var secondary: Color { AureaColors.muted }
}

/// Um popup preso a um retângulo da tela (espaço global), desenhado por cima de
/// tudo pela camada do `AureaPopupPresenter` (a do menu ancorado da fundação):
/// abaixo da âncora pela borda de início, ou à esquerda dela centrado na vertical.
enum OmegaPopup {
    enum Placement { case below, leftCentered }

    @MainActor @discardableResult
    static func present<Content: View>(anchor: CGRect, placement: Placement, owner: UUID = UUID(),
                                       onDismiss: @escaping () -> Void, @ViewBuilder content: @escaping () -> Content) -> UUID {
        let presenter = AureaPopupPresenter.shared
        presenter.present(owner: owner, onDismiss: onDismiss) {
            OmegaPopupOverlay(anchor: anchor, placement: placement,
                              dismiss: { presenter.dismiss(owner: owner, notify: true) }) { content() }
        }
        return owner
    }

    /// Fecha sem avisar o dono (quem chama já sabe).
    @MainActor static func dismiss(_ owner: UUID) { AureaPopupPresenter.shared.dismiss(owner: owner, notify: false) }
}

private struct OmegaPopupSizeKey: PreferenceKey {
    static var defaultValue: CGSize = .zero
    static func reduce(value: inout CGSize, nextValue: () -> CGSize) { value = nextValue() }
}

/// A janela do popup: toque fora fecha; o conteúdo fica onde a regra manda.
private struct OmegaPopupOverlay<Content: View>: View {
    let anchor: CGRect
    let placement: OmegaPopup.Placement
    let dismiss: () -> Void
    @ViewBuilder let content: () -> Content
    @Environment(\.layoutDirection) private var direction
    @State private var size: CGSize = .zero

    var body: some View {
        GeometryReader { geometry in
            let origin = geometry.frame(in: .global).origin
            let window = geometry.size
            let a = anchor.offsetBy(dx: -origin.x, dy: -origin.y)
            let margin = AnchoredMenuMetrics.screenMargin
            let position: CGPoint = {
                switch placement {
                case .below:
                    return CGPoint(
                        x: AnchoredMenuPosition.x(anchorLeft: a.minX, anchorRight: a.maxX, windowWidth: window.width,
                                                  popupWidth: size.width, rtl: direction == .rightToLeft, alignEnd: false, margin: margin),
                        y: AnchoredMenuPosition.y(anchorTop: a.minY, anchorBottom: a.maxY, windowHeight: window.height,
                                                  popupHeight: size.height, margin: margin, gap: AnchoredMenuMetrics.anchorGap))
                case .leftCentered:
                    let x = direction == .rightToLeft ? a.maxX + 6 : a.minX - 6 - size.width
                    let y = a.midY - size.height / 2
                    return CGPoint(x: min(max(x, margin), max(margin, window.width - size.width - margin)),
                                   y: min(max(y, margin), max(margin, window.height - size.height - margin)))
                }
            }()
            ZStack(alignment: .topLeading) {
                Color.clear.contentShape(Rectangle()).onTapGesture(perform: dismiss).accessibilityHidden(true)
                content()
                    .environment(\.layoutDirection, direction)
                    .fixedSize()
                    .background(GeometryReader { panel in Color.clear.preference(key: OmegaPopupSizeKey.self, value: panel.size) })
                    .accessibilityAction(.escape, dismiss)
                    .offset(x: position.x, y: position.y)
                    .opacity(size.height > 8 ? 1 : 0)
            }
            .environment(\.layoutDirection, .leftToRight)
        }
        .ignoresSafeArea()
        .onPreferenceChange(OmegaPopupSizeKey.self) { size = $0 }
    }
}

struct TopBarView: View {
    var height: CGFloat = EditorLayout.topBar
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    @ObservedObject private var requests = OmegaChromeRequests.shared
    private var layer: LayerItem? { model.selectedLayer }
    private var ids: [NSNumber] { model.selection.map { NSNumber(value: $0) } }
    private var count: Int { model.selection.count }
    /// Omega chrome (área 02, P0-4): o `›` da barra de seleção troca a fileira pela de alinhamento na tela.
    @State private var aligning = false
    /// Omega chrome (área 02, P1-1): o ⋮ da camada é um menu ancorado; o seletor de "Substituir mídia" mora aqui.
    @State private var layerMenu = false
    @State private var replaceTarget: LayerRef?
    /// A barra da camada entra descendo da borda de cima em 100 ms (a `action_bar_show` da referência).
    @State private var layerEnter: CGFloat = 0
    private var parent: Int64 { (model.detail["parentId"] as? NSNumber)?.int64Value ?? 0 }

    var body: some View {
        Group {
            // Segurar uma camada liga a seleção: a barra das ações de grupo já com 1 (BatchTopBar.kt).
            // Omega conteúdo (área 06, P0-1): no modo de texto, a barra de estilo no lugar da barra.
            if model.textModeActive { TextStyleBar() }
            else if count >= 2 || (count >= 1 && model.timelineLayerSelectMode) { batchBar }
            // Seção da camada aberta (redesenho 2026-09-29): `‹` + título centrado (SectionTopBar.kt).
            else if layer != nil, let title = sectionTitle { sectionBar(title) }
            else if let layer { layerBar(layer) }
            else { projectBar }
        }
        .frame(height: height)
        .background(AureaColors.editorCanvas)
        .clipped()
        // A busca de ferramentas (lupa dos Ajustes ou do Copiar e colar) e o teste de desempenho.
        .sheet(isPresented: $shell.performanceTest) { PerformanceTestPanel() }
        .sheet(isPresented: $shell.commandSearch) { CommandSearchView().environmentObject(model).environmentObject(shell) }
        .sheet(item: $replaceTarget) { target in
            ShellMediaPicker(selectionLimit: 1, filter: .any(of: [.images, .videos])) { items in
                replaceTarget = nil
                if let (url, video) = items.first { model.replaceMedia(layer: target.id, url: url, video: video) }
            }
        }
        // Omega chrome: pedidos do painel de Ajustes, que mora na raiz do app (fora da casca).
        .onChange(of: requests.commandSearch) { on in
            guard on else { return }
            requests.commandSearch = false; shell.commandSearch = true
        }
        .onChange(of: requests.performanceTest) { on in
            guard on else { return }
            requests.performanceTest = false; shell.performanceTest = true
        }
        .onChange(of: requests.aeImport) { on in
            guard on else { return }
            requests.aeImport = false
            let shell = shell
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.45) { shell.addPicker = .aeImport }
        }
    }

    /// Título da seção aberta da camada (par do `panelSectionTitle` do Android);
    /// nil sem seção (doca, nada aberto ou o painel de exportar).
    private var sectionTitle: String? {
        switch model.panel {
        case .none, .dock, .exportPanel: return nil
        case .transform: return AureaText.t("sh_dock_transform")
        case .text: return AureaText.t("text_options")
        case .effects: return AureaText.t("panel_efeitos")
        case .layer3D: return AureaText.t("panel_material_ambiente")
        case .appearance: return AureaText.t("panel_mistura_opacidade")
        case .speed: return AureaText.t("panel_tempo_velocidade")
        case .clipEdit: return AureaText.t("i18n_clip_modes")
        case .audio: return AureaText.t("panel_som")
        case .shape: return AureaText.t("panel_cor_preenchimento")
        case .shapeEdit: return AureaText.t("panel_editar_forma")
        case .mask: return AureaText.t("panel_mascara_recorte")
        case .textAnimation: return AureaText.t("panel_animacao")
        case .curve: return AureaText.t("panel_easing_curve")
        case .presets: return AureaText.t("panel_presets")
        case .particles: return AureaText.t("panel_particulas")
        case .tracking: return AureaText.t("panel_rastreio")
        case .captions: return AureaText.t("panel_legendas")
        case .vector: return AureaText.t("panel_vetor")
        case .aiVideo: return AureaText.t("panel_ai_video")
        case .filters: return AureaText.t("grade_title_filters")
        case .adjust: return AureaText.t("grade_title_adjust")
        // Omega aparência (área 05).
        case .borderShadow: return AureaText.t("om_ap_bs_title")
        case .camera: return AureaText.t("om_ap_cam_title")
        // Omega conteúdo (área 06).
        case .drawing: return AureaText.t("om_draw_title")
        case .editPoints: return AureaText.t("om_pts_title")
        }
    }

    /// A BARRA DA SEÇÃO (par do SectionTopBar.kt): a barra do painel de
    /// propriedade da referência (`PanelBar`, Omega fundação F5) — `‹` + o
    /// título À ESQUERDA; as áreas põem ações à direita (`trailing`). O `‹`
    /// fecha a seção e volta às ferramentas da camada.
    private func sectionBar(_ title: String) -> some View {
        PanelBar(title: title, onBack: { model.panel = .none }, tag: "editor.sectionBar", moreEntries: model.panelBarMore) {
            // Omega conteúdo (área 06, P0-6): o segmento do Editar pontos e as chaves do Editar forma.
            ContentSectionTrailing(panel: model.panel)
        }
    }

    private func searchButton(tint: Color = AureaColors.text) -> some View {
        Button {
            if model.status.playing != 0 { model.playPause() }
            shell.commandSearch = true
        } label: { Image(systemName: "magnifyingglass").fontWeight(AureaIconWeight.symbol).foregroundStyle(tint).frame(width: 44, height: 44).contentShape(Rectangle()) }
            .buttonStyle(.plain).accessibilityLabel(AureaText.t("edt_cmd_search_desc")).accessibilityIdentifier("commandSearchOpen")
    }

    /// Barra do PROJETO (referência `activity_edit_navbar`), fundo A10: sair · título ·
    /// ⚙ · compartilhar. O ⚙ abre os Ajustes DIRETO (o menu da engrenagem saiu; o que
    /// era dele mora em "Mais do AUREA" e no Copiar e colar). Dentro de grupo: a
    /// trilha de grupos e o ⚙ do grupo (sair, título e compartilhar somem).
    private var projectBar: some View {
        HStack(spacing: 0) {
            if model.engine.precompDepth > 0 {
                groupTrail
                ShellBarButton(glyph: CupertinoGlyph.Gear, description: AureaText.t("om_tb_group_settings"), width: 40, height: EditorLayout.topBar) { openSettings() }
                    .accessibilityIdentifier("editor.topbar.groupSettings")
            } else {
                ShellBarButton(glyph: OmegaGlyph.exitBox, description: AureaText.t("om_tb_exit"), width: 40, height: EditorLayout.topBar) { model.editorBack() }
                    .accessibilityIdentifier("editor.topbar.exit")
                ShellInlineName(name: model.projectName, placeholder: AureaText.t("edt_untitled"), limit: 320) { model.renameCurrentProject(to: $0) }
                    .accessibilityIdentifier("editor.topbar.title")
                ShellBarButton(glyph: CupertinoGlyph.Gear, description: AureaText.t("editor_ajustes_projeto"), width: 40, height: EditorLayout.topBar) { openSettings() }
                    .accessibilityIdentifier("editor.projectMenu")
                ShellBarButton(glyph: CupertinoGlyph.SquareArrowUp, description: AureaText.t("om_tb_share"), width: 40, height: EditorLayout.topBar) { model.openExport() }
                    .accessibilityIdentifier("editor.export")
            }
        }
        .frame(maxHeight: .infinity)
        .background(OmegaChrome.bar)
    }

    private func openSettings() {
        if model.status.playing != 0 { model.playPause() }
        model.showProjectSettings = true
    }

    /// A trilha de grupos: [projeto] › [grupo] › … › [atual]. O primeiro item tem o
    /// «« e fundo S10; os do meio, pílula S9; o último, só o texto. Tocar no item i
    /// sobe n−i−1 níveis. Rola até o fim.
    private var groupTrail: some View {
        let depth = Int(model.engine.precompDepth)
        var names = model.compositionTrail()
        if names.count != depth + 1 {
            names = Array(repeating: "", count: depth + 1)
            names[depth] = model.composition["name"] as? String ?? ""
        }
        names[0] = model.projectName
        let total = names.count
        return ScrollViewReader { proxy in
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 2.5) {
                    ForEach(Array(names.enumerated()), id: \.offset) { index, raw in
                        trailItem(index, raw, total: total).id(index)
                    }
                }
                .padding(.leading, 6)
                .frame(maxHeight: .infinity)
            }
            .onAppear { proxy.scrollTo(total - 1, anchor: .trailing) }
            .onChange(of: total) { n in proxy.scrollTo(n - 1, anchor: .trailing) }
        }
        .frame(maxWidth: .infinity)
    }

    private func trailItem(_ index: Int, _ raw: String, total: Int) -> some View {
        let last = index == total - 1
        let label = raw.isEmpty ? (index == 0 ? AureaText.t("edt_untitled") : "…") : raw
        return HStack(spacing: 2.5) {
            if index > 0 { CupertinoGlyph.text(CupertinoGlyph.ChevronRight, size: 12, color: OmegaChrome.secondary).mirrorsInRtl() }
            Button {
                model.showAddLayer = false
                for _ in 0..<(total - index - 1) { model.editorBack() }
            } label: {
                HStack(spacing: 3) {
                    if index == 0 { CupertinoGlyph.text(OmegaGlyph.chevronLeft2, size: 13, color: AureaColors.text).mirrorsInRtl() }
                    Text(label).font(.aurea(size: 14, weight: .medium)).foregroundStyle(AureaColors.text)
                        .lineLimit(1).truncationMode(.tail).frame(maxWidth: 100)
                }
                .padding(.horizontal, 6).padding(.vertical, 3).frame(minHeight: 30)
                .background(last ? Color.clear : index == 0 ? OmegaChrome.trailRoot : OmegaChrome.column, in: RoundedRectangle(cornerRadius: 5))
                .contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle(shrink: 1))
            .disabled(last)
            .accessibilityLabel(AureaText.t("om_tb_trail_item", label))
            .accessibilityIdentifier("editor.trail.\(index)")
        }
    }

    /// Barra da CAMADA (`fragment_element_actionbar`), fundo A9: ‹ · nome · vínculo ·
    /// 🗑 · ⋮. O vínculo some em camada de áudio; a lixeira apaga sem perguntar (um
    /// desfazer); o ⋮ abre o menu da camada ancorado nele. 3D e desfoque de
    /// movimento ficam na barra (0.3.3) e também no ⋮; duplicar, no Copiar e
    /// colar; a lupa, nos Ajustes.
    private func layerBar(_ row: LayerItem) -> some View {
        HStack(spacing: 0) {
            ShellBarButton(glyph: CupertinoGlyph.ChevronLeft, description: AureaText.t("editor_voltar_tirar_selecao"), size: 20, width: 40, height: EditorLayout.topBar) { model.editorBack() }
                .accessibilityIdentifier("layer.back")
            ShellInlineName(name: row.name, placeholder: AureaText.t("ios_unnamed_layer"), enabled: !row.locked) { value in
                model.mutate { $0.setLayer(row.id, name: value) }; model.refreshModel(force: true)
            }
            .id(row.id)
            .accessibilityIdentifier("layer.name")
            // 0.3.3 recursos: 3D e desfoque de movimento voltam à barra da camada
            // (como na 0.3.2: aceso em destaque quando ligado, apagado se travada).
            // Continuam também no ⋮.
            if row.kind != 3 {
                let intrinsic = [8, 9, 10].contains(row.kind)
                let threeD = row.threeD || intrinsic
                let motion = ((model.detail["timeFlags"] as? NSNumber)?.uint32Value ?? 0) & 2 != 0
                ShellBarButton(glyph: CupertinoGlyph.Cube, description: AureaText.t("sh_add_tab_3d"), size: 20, width: 40, height: EditorLayout.topBar,
                               tint: threeD ? AureaColors.accent : AureaColors.text, enabled: !row.locked && !intrinsic,
                               disabledTint: threeD ? AureaColors.accent : nil) {
                    _ = model.engine.setLayer3D(!threeD, forLayer: row.id); model.refreshModel(force: true)
                }
                .accessibilityValue(AureaText.t(threeD ? "common_on" : "common_off"))
                .accessibilityIdentifier("trial.layer.3d")
                ShellBarButton(glyph: CupertinoGlyph.MotionBlur, description: AureaText.t("editor_desfoque_movimento"), size: 20, width: 40, height: EditorLayout.topBar,
                               tint: motion ? AureaColors.accent : AureaColors.text, enabled: !row.locked) {
                    model.mutate { $0.setMotionBlur(!motion, forLayer: row.id) }; model.refreshModel(force: true)
                }
                .accessibilityValue(AureaText.t(motion ? "common_on" : "common_off"))
                .accessibilityIdentifier("trial.layer.motionBlur")
            }
            if row.kind != 3 {
                linkMenuButton(tint: parent != 0 ? AureaColors.accent : AureaColors.text, width: 40)
            }
            ShellBarButton(glyph: CupertinoGlyph.Trash, description: AureaText.t("editor_excluir_camada"), size: 19, width: 40, height: EditorLayout.topBar,
                           enabled: !row.locked) { removeSelection() }
                .accessibilityIdentifier("layer.delete")
            ShellBarButton(glyph: OmegaGlyph.ellipsisVertical, description: AureaText.t("editor_mais_acoes_camada"), size: 20, width: 40, height: EditorLayout.topBar) {
                if model.status.playing != 0 { model.playPause() }
                layerMenu = true
            }
            .accessibilityIdentifier("layer.menu")
            .aureaAnchoredMenu(isPresented: $layerMenu, alignEnd: true, tag: "layer.menu.popup") { [rowId = row.id] in
                // Lê a camada AGORA: uma alternância redesenha com o estado novo.
                guard let fresh = model.layers.first(where: { $0.id == rowId }) else { return [] }
                return layerMenuEntries(fresh)
            }
        }
        .frame(maxHeight: .infinity)
        .background(OmegaChrome.layerBar)
        .offset(y: layerEnter)
        .onAppear {
            layerEnter = -EditorLayout.topBar
            withAnimation(.easeOut(duration: 0.1)) { layerEnter = 0 }
        }
    }

    /// As linhas do ⋮ da camada: as mesmas da antiga folha, na mesma ordem (o
    /// conteúdo é do painel da camada), mais "Camada 3D", que saiu da barra.
    private func layerMenuEntries(_ row: LayerItem) -> [MenuEntry] {
        let ids = [NSNumber(value: row.id)]
        let index = model.layers.firstIndex(where: { $0.id == row.id }) ?? 0
        let inside = model.status.playhead > Int64(row.startFrame) && model.status.playhead < Int64(row.endFrame)
        let refresh: () -> Void = { model.refreshModel(force: true) }
        var out: [MenuEntry] = [.header(AureaText.t("editor_camada"))]
        if row.kind == 1 || row.kind == 3 {
            out.append(.item(AureaText.t("editor_velocidade"), glyph: CupertinoGlyph.Speedometer) { model.openPanel(.speed) })
        }
        // 0.3.3 recursos: entradas da doca da 0.3.2 que ficaram sem lugar no painel da camada.
        // Opções de texto (espaçamentos, caixa, fundo, caminho, sombra e trechos ricos).
        if row.kind == 4 {
            out.append(.item(AureaText.t("text_options"), glyph: ShellGlyph.SliderHorizontalBelowRectangle,
                             tag: "layer.menu.textOptions") { model.openPanel(.text) })
        }
        // Fantoche (o "Rig" da doca da imagem): o cartão do efeito já no modo de pinos.
        if row.kind == 2 {
            out.append(.item(AureaText.t("fx_name_puppet"), glyph: CupertinoGlyph.PersonCropCircle, enabled: !row.locked,
                             tag: "layer.menu.puppet") { model.openPuppetTool() })
        }
        out.append(.item(AureaText.t(row.locked ? "editor_desbloquear_camada" : "editor_bloquear_camada"),
                         glyph: row.locked ? ShellGlyph.LockOpenFill : CupertinoGlyph.LockFill,
                         sublabel: AureaText.t(row.locked ? "editor_volta_aceitar_movimento_edicao" : "editor_nao_aceita_movimento_corte_nem_edicao"),
                         keepOpen: true) { model.mutate { $0.setLayer(row.id, locked: !row.locked) }; refresh() })
        out.append(.item(AureaText.t(row.visible ? "editor_ocultar_camada" : "editor_mostrar_camada"),
                         glyph: row.visible ? CupertinoGlyph.EyeSlash : CupertinoGlyph.Eye, keepOpen: true) {
            model.mutate { $0.setLayer(row.id, visible: !row.visible) }; refresh()
        })
        // Omega timeline (área 03, D-1): as trilhas de keyframes da camada (antes pela pílula).
        out.append(.item(AureaText.t(model.timelineExpandedLayers.contains(row.id) ? "om_tl_hide_tracks" : "om_tl_show_tracks"),
                         glyph: CupertinoGlyph.Rhombus) { model.toggleTimelineTracks(row.id) })
        out.append(.item(AureaText.t("editor_solo"), glyph: CupertinoGlyph.Speaker2, sublabel: AureaText.t("editor_alguma_camada_solo_previa_som_so"),
                         checked: row.solo, keepOpen: true) { model.mutate { $0.setLayer(row.id, solo: !row.solo) }; refresh() })
        // Omega chrome (área 02, P0-3): "Camada 3D" saiu da barra da camada e mora aqui.
        if row.kind != 3 {
            let intrinsic = [8, 9, 10].contains(row.kind)
            let threeD = row.threeD || intrinsic
            out.append(.item(AureaText.t("sh_add_tab_3d"), glyph: CupertinoGlyph.Cube, checked: threeD, enabled: !row.locked && !intrinsic,
                             keepOpen: true, tag: "layer.menu.threeD") {
                _ = model.engine.setLayer3D(!threeD, forLayer: row.id); refresh()
            })
        }
        // Linha Magnética: só em trecho com FONTE (vídeo, imagem, áudio ou pré-composição).
        if row.kind == 1 || row.kind == 2 || row.kind == 3 || row.kind == 12 {
            out.append(.item(AureaText.t("editor_linha_magnetica"), glyph: CupertinoGlyph.Link, sublabel: AureaText.t("editor_linha_magnetica_detalhe"),
                             checked: row.magnetic, keepOpen: true) { model.setLayerMagneticTrack(row.id, !row.magnetic) })
        }
        if row.kind != 3 {
            out.append(.item(AureaText.t("editor_camada_ajuste"), glyph: CupertinoGlyph.SliderHorizontal3,
                             sublabel: AureaText.t("editor_efeitos_desta_camada_valem_todas_baixo"), checked: row.adjustment, keepOpen: true) {
                model.mutate { $0.setLayer(row.id, adjustment: !row.adjustment) }; refresh()
            })
            if row.adjustment {
                // Camadas afetadas: todas abaixo → só a logo abaixo → só as escolhidas.
                let scope = model.engine.adjustmentScope(row.id)
                out.append(.item(AureaText.t("la_affected") + " · " + AureaText.t(scope == 1 ? "la_affected_below" : scope == 2 ? "la_affected_chosen" : "la_affected_all"),
                                 glyph: CupertinoGlyph.SliderHorizontal3,
                                 sublabel: AureaText.t(scope == 2 ? "la_affected_chosen_hint" : "la_affected_hint"), keepOpen: true) {
                    model.engine.setAdjustmentScope((scope + 1) % 3, forLayer: row.id); refresh()
                })
                if scope == 2 {
                    let below = Array(model.layers.dropFirst(index + 1)).filter { $0.kind != 3 }
                    let chosen = Set(model.engine.adjustmentTargets(row.id).map(\.int64Value))
                    if below.isEmpty { out.append(.item(AureaText.t("la_affected_none_below"), glyph: CupertinoGlyph.SliderHorizontal3, enabled: false)) }
                    for b in below {
                        out.append(.item(b.name, glyph: CupertinoGlyph.SliderHorizontal3, checked: chosen.contains(b.id), keepOpen: true) {
                            model.engine.setAdjustmentTarget(b.id, on: !chosen.contains(b.id), forLayer: row.id); refresh()
                        })
                    }
                }
            }
            out.append(.item(AureaText.t("editor_guia_nao_exporta"), glyph: CupertinoGlyph.Grid, sublabel: AureaText.t("editor_aparece_aqui_editor_fica_fora_video"),
                             checked: row.guide, keepOpen: true) { model.mutate { $0.setLayer(row.id, guide: !row.guide) }; refresh() })
        }
        out.append(.item(AureaText.t("editor_duplicar"), glyph: CupertinoGlyph.PlusSquareOnSquare) { model.engine.duplicateLayers(ids); refresh() })
        out.append(.item(AureaText.t("editor_puxar_cabecote"), glyph: CupertinoGlyph.ArrowDownToLine, enabled: !row.locked) {
            if model.status.playing != 0 { model.playPause() }; model.moveToPlayhead(row.id)
        })
        out.append(.item(AureaText.t("editor_copiar_camada"), glyph: CupertinoGlyph.DocOnDoc) { model.engine.copyLayers(ids) })
        out.append(.item(AureaText.t("editor_colar_camada_cabecote"), glyph: CupertinoGlyph.DocOnClipboard, enabled: model.engine.clipboardState & 1 != 0) {
            model.engine.pasteLayers(model.status.playhead); refresh()
        })
        out.append(.item(AureaText.t("editor_copiar_estilo"), glyph: CupertinoGlyph.Paintbrush) { model.engine.copyStyle(row.id) })
        out.append(.item(AureaText.t("editor_colar_estilo"), glyph: ShellGlyph.PaintbrushFill, enabled: model.engine.clipboardState & 2 != 0) {
            model.engine.pasteStyle(ids); refresh()
        })
        out.append(.item(AureaText.t("editor_trazer_frente"), glyph: CupertinoGlyph.ArrowUpToLine, enabled: index > 0) {
            model.reorderLayer(row.id, displayIndex: index - 1)
        })
        out.append(.item(AureaText.t("editor_enviar_tras"), glyph: CupertinoGlyph.ArrowDownToLine, enabled: index < model.layers.count - 1) {
            model.reorderLayer(row.id, displayIndex: index + 1)
        })
        // Etiqueta: "sem" e as doze cores, numa fileira de ícones.
        out.append(.header(AureaText.t("editor_etiqueta")))
        var cells = [MenuIconCell(label: AureaText.t("editor_nenhum"), glyph: ShellGlyph.Nosign) {
            model.mutate { $0.setLayer(row.id, label: 0) }; refresh()
        }]
        for (i, color) in AureaColors.labelPalette.enumerated() {
            cells.append(MenuIconCell(label: AureaText.t("editor_etiqueta") + " \(i + 1)", swatch: color) {
                model.mutate { $0.setLayer(row.id, label: UInt32(i + 1)) }; refresh()
            })
        }
        out.append(.icons(cells, selectedIndex: Int(row.label), keepOpen: true, tag: "layer.menu.labels"))
        if row.kind != 3 {
            out.append(.header(AureaText.t("editor_grupo")))
            if row.clipToBelow || row.clipCandidate {
                out.append(.item(AureaText.t(row.clipToBelow ? "editor_liberar_mascara_recorte" : "editor_criar_mascara_recorte"),
                                 glyph: CupertinoGlyph.ArrowDownRightSquare, sublabel: AureaText.t("editor_mascara_recorte_detalhe"), tag: "layer.clipMask") {
                    model.setLayerClipToBelow(row.id, !row.clipToBelow)
                })
            }
            if row.kind == 12 {
                out.append(.item(AureaText.t("editor_entrar_grupo"), glyph: CupertinoGlyph.ArrowDownRightSquare) { model.openGroup(row.id) })
                out.append(.item(AureaText.t("editor_desagrupar"), glyph: ShellGlyph.SquareSplit2x2) { model.ungroup(row.id) })
                let through = model.engine.groupCameraPassThrough(row.id) == 1
                out.append(.item(AureaText.t("la_group_camera"), glyph: CupertinoGlyph.Camera, sublabel: AureaText.t("la_group_camera_hint"),
                                 checked: through, keepOpen: true) {
                    if model.engine.setGroupCameraPassThrough(!through, forLayer: row.id) {
                        model.toast = AureaText.t(through ? "app_group_camera_sealed" : "app_group_camera_reaches")
                    }
                    refresh()
                })
            } else {
                out.append(.item(AureaText.t("editor_converter_grupo"), glyph: CupertinoGlyph.RectangleStack) { model.groupSelection() })
            }
            let acceptsLights = model.engine.layerAcceptsLights(row.id)
            if acceptsLights >= 0 {
                out.append(.item(AureaText.t("la_accepts_lights"), glyph: CupertinoGlyph.Lightbulb, sublabel: AureaText.t("la_accepts_lights_hint"),
                                 checked: acceptsLights == 1, keepOpen: true) {
                    _ = model.engine.setLayerAcceptsLights(acceptsLights != 1, forLayer: row.id); refresh()
                })
            }
            // Pôr esta camada num grupo que já existe aqui / tirar do grupo aberto.
            for group in model.layers where group.kind == 12 && group.id != row.id {
                out.append(.item(AureaText.t("la_add_to_group") + " · " + group.name, glyph: CupertinoGlyph.RectangleStack) {
                    let why = model.engine.addLayers([NSNumber(value: row.id)], toGroup: group.id)
                    if !why.isEmpty { model.toast = AureaText.t("app_group_add_failed", AureaEngineText.reason(why)) }
                    refresh()
                })
            }
            if model.engine.precompDepth > 0 {
                out.append(.item(AureaText.t("la_remove_from_group"), glyph: CupertinoGlyph.ArrowUturnLeft) {
                    let why = model.engine.removeLayerFromGroup(row.id)
                    if !why.isEmpty { model.toast = AureaText.t("app_group_remove_failed", AureaEngineText.reason(why)) }
                    refresh()
                })
            }
        }
        if row.kind == 1 || row.kind == 2 || row.kind == 3 {
            out.append(.header(AureaText.t("sh_menu_media")))
            if row.kind == 1 || row.kind == 2 {
                out.append(.item(AureaText.t("layer_replace_media"), glyph: CupertinoGlyph.Arrow2Squarepath, sublabel: AureaText.t("layer_replace_media_detail"),
                                 enabled: !row.locked) { replaceTarget = LayerRef(id: row.id) })
            }
            let path = model.layerSourcePath(row.id)
            out.append(.item(AureaText.t("media_info_title"), glyph: CupertinoGlyph.InfoCircle, tag: "layer.menu.mediaInfo") {
                AureaPopupPresenter.shared.presentDialog { close in MediaInfoDialogView(path: path, onDismiss: close) }
            })
            if row.kind == 1 {
                out.append(.item(AureaText.t("editor_extrair_audio"), glyph: CupertinoGlyph.MusicNote2, sublabel: AureaText.t("editor_som_vira_camada_propria_video_fica")) {
                    _ = model.engine.extractAudio(fromLayer: row.id); refresh()
                })
            }
        }
        out.append(.header(AureaText.t("editor_movimento")))
        let timeFlags = (model.detail["timeFlags"] as? NSNumber)?.uint32Value ?? 0
        out.append(.item(AureaText.t("editor_desfoque_movimento"), glyph: CupertinoGlyph.MotionBlur,
                         sublabel: AureaText.t("editor_borra_direcao_movimento_obturador_nas_configuracoes"), checked: timeFlags & 2 != 0,
                         tag: "layer.menu.motionBlur") {
            model.mutate { $0.setMotionBlur(timeFlags & 2 == 0, forLayer: row.id) }; refresh()
        })
        if row.kind == 1 {
            out.append(.item(AureaText.t("editor_desfoque_movimento_video"), glyph: CupertinoGlyph.MotionBlur,
                             sublabel: AureaText.t("editor_borra_mexe_dentro_video_pelos_vetores"), checked: timeFlags & 32 != 0) {
                model.mutate { $0.setVectorBlur(forLayer: row.id, amount: timeFlags & 32 == 0 ? 1 : 0) }; refresh()
            })
        }
        // 0.3.1: preset .ffx nesta camada / script .jsx com ela em comp.selectedLayers.
        out.append(.item(AureaText.t("ae_import_menu"), glyph: CupertinoGlyph.SquareStack3dUp, sublabel: AureaText.t("ae_import_layer_detail"),
                         enabled: !row.locked, tag: "layer.aeImport") { OmegaChromeRequests.shared.aeImport = true })
        if row.kind == 1 || row.kind == 3 {
            out.append(.header(AureaText.t("editor_tempo")))
            out.append(.item(AureaText.t("i18n_clip_modes"), glyph: CupertinoGlyph.Scissors) { model.openPanel(.clipEdit) })
        }
        if row.kind == 1 {
            out.append(.item(AureaText.t("sh_menu_freeze_frame"), glyph: ShellGlyph.Snow, enabled: inside) {
                let created = model.engine.freezeFrame(forLayer: row.id, frame: Int32(clamping: model.status.playhead), hold: Int32(max(1, model.compositionFps * 3)))
                refresh()
                if created >= 0 { model.select(layerId: created) }
            })
            out.append(.header(AureaText.t("editor_rastreio")))
            out.append(.item(AureaText.t("dock2_tracking_tools"), glyph: ShellGlyph.Viewfinder, sublabel: AureaText.t("dock2_tracking_tools_detail")) {
                model.select(layerId: row.id); model.cameraTrackerVisible = false; model.openPanel(.tracking)
            })
            out.append(.item(AureaText.t("editor_rastrear_ponto"), glyph: ShellGlyph.Viewfinder, sublabel: AureaText.t("editor_cria_nulo_segue_ponto_ligue_outras")) {
                model.select(layerId: row.id); model.openPanel(.tracking); model.beginPointPick(stabilize: false)
            })
            out.append(.item(AureaText.t("editor_estabilizar_pelo_ponto"), glyph: ShellGlyph.Viewfinder, sublabel: AureaText.t("editor_move_video_ponto_ficar_parado_tela")) {
                model.select(layerId: row.id); model.openPanel(.tracking); model.beginPointPick(stabilize: true)
            })
        }
        return out
    }

    /// Seleção de camadas (`activity_edit_selectbar`), fundo de destaque, a partir
    /// de 1 camada: ✕ · "N camadas selecionadas" · Agrupar · Agrupar e mascarar ·
    /// Agrupar e excluir · 🗑 · ›. O `›` troca a fileira pela de ALINHAMENTO NA
    /// TELA: ‹ + 6 alinhamentos + 2 distribuições, todos do mesmo tamanho. As outras
    /// ações (escolher uma, desagrupar, duplicar, vincular) moram no Copiar e colar.
    private var batchBar: some View {
        let ink = AureaColors.onAccent
        let canMask = count >= 2 && model.engine.canGroupAndMask(ids)
        let dim = ink.opacity(0.38)
        let three = count >= 3
        func finish() { model.changeTimelineLayerSelectMode(false) }
        return HStack(spacing: 0) {
            ShellBarButton(glyph: CupertinoGlyph.Xmark, description: AureaText.t("editor_cancelar_selecao"), size: 18, width: 40, height: EditorLayout.topBar, tint: ink) {
                model.clearSelection(); finish()
            }.accessibilityIdentifier("batch.cancel")
            GeometryReader { geometry in
                let cell = max(1, geometry.size.width / 9)
                ZStack {
                    if !aligning {
                        HStack(spacing: 0) {
                            Text(AureaText.plural("om_layers_selected", count))
                                .font(.aurea(size: 13, weight: .bold)).foregroundStyle(ink)
                                .lineLimit(1).truncationMode(.middle).frame(maxWidth: .infinity, alignment: .leading)
                            ShellBarButton(glyph: ShellGlyph.FolderBadgePlus, description: AureaText.t("editor_agrupar"), size: 19, width: 40, height: EditorLayout.topBar, tint: ink) {
                                model.groupSelection(); finish()
                            }.accessibilityIdentifier("batch.group")
                            ShellBarButton(glyph: CupertinoGlyph.CircleLefthalfFill, description: AureaText.t("batch_group_mask_a11y"), size: 19, width: 40, height: EditorLayout.topBar,
                                           tint: canMask ? ink : dim) {
                                if canMask { model.groupAndMaskSelection() } else { model.toast = AureaText.t("batch_need_two_visual") }
                            }.accessibilityIdentifier("batch.groupMask")
                            ShellBarButton(glyph: CupertinoGlyph.MinusCircle, description: AureaText.t("batch_group_exclude_a11y"), size: 19, width: 40, height: EditorLayout.topBar,
                                           tint: canMask ? ink : dim) {
                                if canMask { model.groupAndMaskSelection(exclude: true) } else { model.toast = AureaText.t("batch_need_two_visual") }
                            }.accessibilityIdentifier("batch.groupExclude")
                            ShellBarButton(glyph: CupertinoGlyph.Trash, description: AureaText.t("editor_excluir_selecao"), size: 19, width: 40, height: EditorLayout.topBar, tint: ink) {
                                removeSelection(); finish()
                            }.accessibilityIdentifier("batch.delete")
                            ShellBarButton(glyph: CupertinoGlyph.ChevronRight, description: AureaText.t("om_sel_align"), size: 17, width: 40, height: EditorLayout.topBar, tint: ink) {
                                withAnimation(.easeIn(duration: 0.2)) { aligning = true }
                            }.accessibilityIdentifier("batch.align")
                        }
                        .transition(.asymmetric(insertion: .offset(x: -geometry.size.width / 2), removal: .opacity))
                    } else {
                        HStack(spacing: 0) {
                            alignCell(CupertinoGlyph.ChevronLeft, "batch_group_actions", "batch.align.back", ink, cell, size: 17) {
                                withAnimation(.easeOut(duration: 0.2)) { aligning = false }
                            }
                            alignCell(CupertinoGlyph.ArrowLeftToLine, "editor_alinhar_esquerda_tela", "batch.align.left", ink, cell) { OmegaBatchLayout.align(model, edge: 0) }
                            alignCell(CupertinoGlyph.ArrowLeftRight, "editor_centralizar_horizontal", "batch.align.hcenter", ink, cell) { OmegaBatchLayout.align(model, edge: 1) }
                            alignCell(CupertinoGlyph.ArrowRightToLine, "editor_alinhar_direita_tela", "batch.align.right", ink, cell) { OmegaBatchLayout.align(model, edge: 2) }
                            alignCell(CupertinoGlyph.ArrowUpToLine, "editor_alinhar_topo_tela", "batch.align.top", ink, cell) { OmegaBatchLayout.align(model, edge: 3) }
                            alignCell(CupertinoGlyph.ArrowUpArrowDown, "editor_centralizar_vertical", "batch.align.vcenter", ink, cell) { OmegaBatchLayout.align(model, edge: 4) }
                            alignCell(CupertinoGlyph.ArrowDownToLine, "editor_alinhar_base_tela", "batch.align.bottom", ink, cell) { OmegaBatchLayout.align(model, edge: 5) }
                            alignCell(CupertinoGlyph.ArrowUpDownSquare, "editor_distribuir_vertical_vaos_iguais", "batch.align.distV", three ? ink : dim, cell) {
                                if three { OmegaBatchLayout.distribute(model, horizontal: false) } else { model.toast = AureaText.t("om_sel_need_three") }
                            }
                            alignCell(CupertinoGlyph.ArrowLeftRightSquare, "editor_distribuir_horizontal_vaos_iguais", "batch.align.distH", three ? ink : dim, cell) {
                                if three { OmegaBatchLayout.distribute(model, horizontal: true) } else { model.toast = AureaText.t("om_sel_need_three") }
                            }
                        }
                        // Entrar no alinhamento: de +50 % da largura; voltar: de −50 %.
                        .transition(.asymmetric(insertion: .offset(x: geometry.size.width / 2), removal: .opacity))
                    }
                }
                .frame(width: geometry.size.width, height: geometry.size.height, alignment: .leading)
            }
            .clipped()
        }
        .frame(maxHeight: .infinity)
        .background(AureaColors.accent)
    }

    private func alignCell(_ glyph: Character, _ key: String, _ tag: String, _ tint: Color, _ width: CGFloat, size: CGFloat = 18,
                           action: @escaping () -> Void) -> some View {
        ShellBarButton(glyph: glyph, description: AureaText.t(key), size: size, width: width, height: EditorLayout.topBar, tint: tint, action: action)
            .accessibilityIdentifier(tag)
    }

    private func linkMenuButton(tint: Color, width: CGFloat = 40) -> some View {
        GeometryReader { bounds in
            ShellBarButton(glyph: parent != 0 ? CupertinoGlyph.LinkCircleFill : CupertinoGlyph.Link,
                description: AureaText.t(parent != 0 ? "editor_vinculada_outra_camada_trocar" : "editor_vincular_outra_camada"), size: 20, width: width,
                height: EditorLayout.topBar, tint: tint) {
                if model.status.playing != 0 { model.playPause() }
                shell.linkIds = Array(model.selection); shell.linkAnchor = bounds.frame(in: .global)
            }.accessibilityIdentifier("layer.parent")
        }.frame(width: width, height: EditorLayout.topBar)
    }
    private func removeSelection() {
        model.deleteSelectedLayers()
    }
}

/// Alinhar e distribuir na tela (as contas do antigo painel de lote, iguais ao
/// `LayerOps` do Android): pelos limites de cada camada, um passo de desfazer.
@MainActor enum OmegaBatchLayout {
    private struct Box {
        let id: Int64
        let x: Float, y: Float, left: Float, top: Float, right: Float, bottom: Float
    }
    private static func boxes(_ model: AureaModel) -> [Box] {
        model.layers.filter { model.selection.contains($0.id) && !$0.locked }.compactMap { row in
            guard let detail = model.queryDetail(row.id), let b = StageGeom.bounds(detail) else { return nil }
            let pos = StageGeom.floats(detail["position"])
            guard pos.count >= 2 else { return nil }
            return Box(id: row.id, x: pos[0], y: pos[1], left: b.0, top: b.1, right: b.2, bottom: b.3)
        }
    }
    /// 0 esquerda · 1 centro H · 2 direita · 3 topo · 4 centro V · 5 base.
    static func align(_ model: AureaModel, edge: Int) {
        let w = Float(model.compositionWidth), h = Float(model.compositionHeight)
        let moves: [(Int64, Float, Float)] = boxes(model).map { box in
            var x = box.x, y = box.y
            switch edge {
            case 0: x -= box.left
            case 1: x += w / 2 - (box.left + box.right) / 2
            case 2: x += w - box.right
            case 3: y -= box.top
            case 4: y += h / 2 - (box.top + box.bottom) / 2
            default: y += h - box.bottom
            }
            return (box.id, x, y)
        }
        apply(model, moves, label: "alinhar")
    }
    static func distribute(_ model: AureaModel, horizontal: Bool) {
        let items = boxes(model).sorted { horizontal ? $0.left < $1.left : $0.top < $1.top }
        guard let first = items.first, let last = items.last, items.count >= 3 else { return }
        let span = horizontal ? last.right - first.left : last.bottom - first.top
        let sizes = items.reduce(Float(0)) { $0 + (horizontal ? $1.right - $1.left : $1.bottom - $1.top) }
        let gap = (span - sizes) / Float(items.count - 1)
        var cursor = horizontal ? first.left : first.top
        let moves: [(Int64, Float, Float)] = items.map { box in
            let delta = cursor - (horizontal ? box.left : box.top)
            cursor += (horizontal ? box.right - box.left : box.bottom - box.top) + gap
            return (box.id, box.x + (horizontal ? delta : 0), box.y + (horizontal ? 0 : delta))
        }
        apply(model, moves, label: "distribuir")
    }
    private static func apply(_ model: AureaModel, _ moves: [(Int64, Float, Float)], label: String) {
        guard !moves.isEmpty else { return }
        if model.status.playing != 0 { model.playPause() }
        model.beginGesture(label)
        for move in moves { model.setTransform2(0, move.1, 1, move.2, layer: move.0) }
        model.endGesture()
    }
}

/// Barra inferior flutuante da seleção (`activity_edit_select_bottomvar`), par da
/// `MultiSelectBottomBar` do Android: 42 pt, margens de 6, fundo S9 com raio 5,
/// por cima da timeline. À esquerda (120 pt) a variante pelo cabeçote (regra do
/// motor): cruza as escolhidas = aparar início · dividir · aparar fim; todas
/// depois = estender/levar o início; todas antes = estender/levar o fim. À
/// direita: alinhar inícios · distribuir no tempo · alinhar fins.
struct MultiSelectBottomBar: View {
    @EnvironmentObject private var model: AureaModel

    var body: some View {
        let variant = model.multiTimeVariant
        HStack(spacing: 0) {
            HStack(spacing: 0) {
                switch variant {
                case 1:
                    cell(CupertinoGlyph.ArrowLeftToLine, "om_sel_extend_start", "batch.bottom.extendStart") { model.extendSelectionToPlayhead(start: true) }
                    cell(OmegaGlyph.arrowLeftSquare, "om_sel_move_start", "batch.bottom.moveStart") { model.arrangeLayerTimes(5) }
                case 2:
                    cell(CupertinoGlyph.ArrowRightToLine, "om_sel_extend_end", "batch.bottom.extendEnd") { model.extendSelectionToPlayhead(start: false) }
                    cell(OmegaGlyph.arrowRightSquare, "om_sel_move_end", "batch.bottom.moveEnd") { model.arrangeLayerTimes(6) }
                default:
                    cell(CupertinoGlyph.ArrowRightToLine, "editor_aparar_inicio_cabecote", "batch.bottom.trimStart") { trim(start: true) }
                    cell(CupertinoGlyph.Scissors, "editor_dividir_cabecote", "batch.bottom.split") { split() }
                    cell(CupertinoGlyph.ArrowLeftToLine, "editor_aparar_fim_cabecote", "batch.bottom.trimEnd") { trim(start: false) }
                }
            }
            .frame(width: 120)
            Spacer(minLength: 0)
            vectorCell("automirrored.rounded.FormatAlignLeft", "editor_alinhar_inicios", "batch.bottom.alignStarts") { model.arrangeLayerTimes(0) }
            vectorCell("rounded.Stairs", "om_sel_distribute_time", "batch.bottom.distributeTime") { model.arrangeLayerTimes(3) }
            vectorCell("automirrored.rounded.FormatAlignRight", "editor_alinhar_fins", "batch.bottom.alignEnds") { model.arrangeLayerTimes(2) }
        }
        .frame(height: 42)
        .background(OmegaChrome.column, in: RoundedRectangle(cornerRadius: 5))
        .padding(.horizontal, 6).padding(.bottom, 6)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("batch.bottom")
    }

    private func cell(_ glyph: Character, _ key: String, _ tag: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            CupertinoGlyph.text(glyph, size: 19, color: AureaColors.text).frame(maxWidth: .infinity, maxHeight: .infinity).contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .accessibilityLabel(AureaText.t(key))
        .accessibilityIdentifier(tag)
    }

    private func vectorCell(_ name: String, _ key: String, _ tag: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            MaterialGlyph(name, size: 20, color: AureaColors.text).frame(width: 40, height: 42).contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .accessibilityLabel(AureaText.t(key))
        .accessibilityIdentifier(tag)
    }

    private var selected: [LayerItem] { model.layers.filter { model.selection.contains($0.id) } }
    private var timeTargets: [LayerItem] {
        selected.filter { !$0.locked && model.status.playhead > Int64($0.startFrame) && model.status.playhead < Int64($0.endFrame) }
    }
    private func trim(start: Bool) {
        let targets = timeTargets
        guard !targets.isEmpty else {
            model.toast = AureaText.t(!selected.isEmpty && selected.allSatisfy(\.locked) ? "sh_layers_locked_unlock_to_edit" : "editor_leve_cabecote_dentro_camadas")
            return
        }
        if model.status.playing != 0 { model.playPause() }
        model.beginGesture(start ? "aparar início" : "aparar fim")
        for row in targets {
            if start { model.trimStart(row.id, at: model.status.playhead) } else { model.trimEnd(row.id, at: model.status.playhead) }
        }
        model.endGesture()
    }
    private func split() {
        guard !timeTargets.isEmpty else { model.toast = AureaText.t("editor_leve_cabecote_dentro_camadas"); return }
        if model.status.playing != 0 { model.playPause() }
        model.splitAtPlayhead(timeTargets.map(\.id))
    }
}

/// Nome editável ali mesmo (`SettingEditText` da referência): tocar vira campo,
/// SEM caixa de fundo — só o cursor no destaque e o ✕ no fim, que APAGA o texto
/// (não cancela). Done ou perder o foco confirma; vazio não vale.
private struct ShellInlineName: View {
    let name: String
    let placeholder: String
    var limit = Int.max
    var enabled = true
    let rename: (String) -> Void
    @State private var editing = false
    @State private var value = ""
    @FocusState private var focused: Bool
    var body: some View {
        Group {
            if editing {
                HStack(spacing: 0) {
                    TextField(placeholder, text: $value).textInputAutocapitalization(.sentences)
                        .foregroundStyle(AureaColors.text).tint(AureaColors.accent)
                        .focused($focused).submitLabel(.done).onSubmit(commit)
                        .padding(.horizontal, 4)
                        .onAppear { focused = true }
                        .onChange(of: focused) { hasFocus in if !hasFocus { commit() } }
                        .onChange(of: value) { new in if new.count > limit { value = String(new.prefix(limit)) } }
                    if !value.isEmpty {
                        Button { value = "" } label: {
                            CupertinoGlyph.text(CupertinoGlyph.XmarkCircleFill, size: 16, color: OmegaChrome.secondary)
                                .frame(width: 36, height: 40).contentShape(Rectangle())
                        }
                        .buttonStyle(.plain)
                        .accessibilityLabel(AureaText.t("om_field_clear"))
                        .accessibilityIdentifier("field.clear")
                    }
                }
            } else {
                Text(name.isEmpty ? placeholder : name).foregroundStyle(name.isEmpty ? OmegaChrome.secondary : AureaColors.text)
                    .truncationMode(.tail)
                    .frame(maxWidth: .infinity, alignment: .leading).padding(.vertical, 12).padding(.horizontal, 4).contentShape(Rectangle())
                    .onTapGesture { if enabled { value = name; editing = true } }
            }
        }.font(.aurea(size: 14, weight: .medium)).lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
    }
    private func commit() {
        guard editing else { return }; editing = false
        let text = value.trimmingCharacters(in: .whitespacesAndNewlines)
        if !text.isEmpty && text != name { rename(text) }
    }
}

/// Omega chrome (área 02, P0-5): a barra de reprodução da referência — 46 pt, 7
/// células de largura igual: ↶ · ↷ · ⇤ · ▶ · ⇥ · Copiar e colar · painel. As
/// setas vão ao evento estrito mais próximo (o começo, o último quadro, os
/// marcadores e, com camada, o início, o fim e os keyframes dela), sem toque
/// longo. A grade foi para a coluna de modos; a resolução da prévia, para os
/// Ajustes; marcar o instante, para o relógio da timeline; "Selecionar uma
/// camada", para o Copiar e colar.
struct TransportView: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    @ObservedObject private var modes = ViewModeState.shared
    var body: some View {
        Group {
            if model.stageManipulating { StageInfoBar() } else { buttons }
        }.frame(height: EditorLayout.transport).background(OmegaChrome.bar)
        // Transporte em LTR mesmo em árabe (par do KeepLtr do Android): início
        // à esquerda, fim à direita, e os ícones (desfazer/refazer inclusive)
        // na mesma ordem e sentido da timeline logo abaixo.
        .keepLtr()
    }

    private var buttons: some View {
        GeometryReader { geometry in
            // 0.3.3 recursos: 8 células (o marcador da 0.3.2 voltou, depois do ⇥).
            let side = max(1, geometry.size.width / 8)
            let h = EditorLayout.transport
            HStack(spacing: 0) {
                ShellBarButton(glyph: CupertinoGlyph.ArrowUturnLeft, description: AureaText.t("editor_desfazer"), size: 20, width: side, height: h,
                               enabled: model.status.canUndo != 0, disabledTint: AureaColors.transportDisabled) { model.undo() }
                    .accessibilityIdentifier("transport.undo")
                ShellBarButton(glyph: CupertinoGlyph.ArrowUturnRight, description: AureaText.t("editor_refazer"), size: 20, width: side, height: h,
                               enabled: model.status.canRedo != 0, disabledTint: AureaColors.transportDisabled) { model.redo() }
                    .accessibilityIdentifier("transport.redo")
                // 0.3.3 recursos: segurar ⇤ vai ao início e ⇥ ao fim (como na 0.3.2).
                ShellBarButton(glyph: CupertinoGlyph.BackwardEnd, description: AureaText.t("r33_tr_previous_hold"), size: 20, width: side, height: h,
                               onLongPress: { model.seek(toFrame: 0) }) {
                    model.stepTransportEdges(-1)
                }.accessibilityIdentifier("transport.previous")
                PreRenderPlayControl(side: side)
                ShellBarButton(glyph: CupertinoGlyph.ForwardEnd, description: AureaText.t("r33_tr_next_hold"), size: 20, width: side, height: h,
                               onLongPress: { model.seek(toFrame: Int64(model.compositionDuration)) }) {
                    model.stepTransportEdges(1)
                }.accessibilityIdentifier("transport.next")
                // 0.3.3 recursos: o marcador da 0.3.2 volta ao transporte — tocar marca ou
                // desmarca o instante (aceso num marcador); segurar abre o editor dele
                // (nome, quadro e cor). Par do Android.
                let marked = model.markerFrames.contains(model.status.playhead)
                ShellBarButton(glyph: marked ? ShellGlyph.BookmarkSolid : CupertinoGlyph.Bookmark,
                               description: AureaText.t("editor_marcar_ou_desmarcar_este_instante"), size: 18, width: side, height: h,
                               tint: marked ? AureaColors.accent : AureaColors.text,
                               onLongPress: { model.editMarkerAtPlayhead() }, action: { model.toggleMarkerAt(model.status.playhead) })
                    .accessibilityIdentifier("transport.marker")
                CopyPasteButton(side: side)
                ShellBarButton(glyph: modes.camera ? CupertinoGlyph.CameraViewfinder : OmegaGlyph.sidebarRight, description: AureaText.t("om_tr_view_modes"),
                               size: 20, width: side, height: h, tint: modes.columnOpen ? AureaColors.accent : AureaColors.text) {
                    modes.columnOpen.toggle()
                }
                .accessibilityValue(AureaText.t(modes.columnOpen ? "common_on" : "common_off"))
                .accessibilityIdentifier("transport.viewModes")
            }
            .frame(height: h)
        }
    }
}

private struct PreRenderPlayControl: View {
    @EnvironmentObject private var model: AureaModel
    let side: CGFloat
    @State private var pressed = false
    @State private var began: Double?
    @State private var pressID: UUID?
    @State private var hold: DispatchWorkItem?
    @State private var armed = false
    @State private var cancelled = false
    @State private var fired = false

    private var progressText: String {
        if model.preRenderPreparing { return AureaText.t("prerender_preparing") }
        if model.preRenderProgress.running { return AureaText.t("prerender_progress", Int((model.preRenderProgress.fraction * 100).rounded())) }
        if model.preRenderProgress.complete { return AureaText.t("prerender_ready") }
        return ""
    }

    var body: some View {
        Button { model.playPause() } label: {
            ZStack(alignment: .bottomTrailing) {
                ZStack {
                    CupertinoGlyph.text(model.status.playing != 0 ? CupertinoGlyph.PauseFill : CupertinoGlyph.PlayFill,
                        size: 22, color: model.looping ? AureaColors.accent : AureaColors.text)
                    if model.preRenderPreparing || model.preRenderProgress.running || model.preRenderProgress.complete {
                        Circle().trim(from: 0, to: CGFloat(model.preRenderPreparing ? 0.08 : model.preRenderProgress.fraction))
                            .stroke(AureaColors.accent, style: StrokeStyle(lineWidth: 2, lineCap: .round))
                            .frame(width: 30, height: 30).rotationEffect(.degrees(-90)).allowsHitTesting(false)
                    }
                }.frame(width: side, height: EditorLayout.transport).contentShape(Rectangle())
                if model.looping {
                    CupertinoGlyph.text(CupertinoGlyph.Repeat, size: 11, color: AureaColors.accent)
                        .padding(.trailing, 6).padding(.bottom, 7).allowsHitTesting(false)
                }
            }.opacity(pressed ? 0.65 : 1)
        }.buttonStyle(.plain)
            .highPriorityGesture(DragGesture(minimumDistance: 0)
                .onChanged { value in
                    if began == nil {
                        let id = UUID()
                        began = ProcessInfo.processInfo.systemUptime; pressID = id
                        pressed = true; armed = false; cancelled = false; fired = false
                        let work = DispatchWorkItem {
                            guard pressID == id, !cancelled else { return }
                            armed = true
                            UIImpactFeedbackGenerator(style: .light).impactOccurred()
                        }
                        hold = work
                        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5, execute: work)
                    }
                    if !armed, !cancelled, let began, ProcessInfo.processInfo.systemUptime - began >= 0.5 {
                        armed = true; hold?.cancel()
                    }
                    let delta = value.translation
                    if !armed && hypot(delta.width, delta.height) > 10 { cancelled = true; hold?.cancel() }
                    if armed && abs(delta.height) >= 24 && abs(delta.height) > abs(delta.width) { cancelled = true }
                    if armed && !cancelled && !fired && abs(delta.width) >= 24 && abs(delta.width) > abs(delta.height) {
                        fired = true
                        model.startPreRender(delta.width > 0 ? 1 : -1)
                    }
                }
                .onEnded { _ in
                    if !cancelled && !fired {
                        if armed || began.map({ ProcessInfo.processInfo.systemUptime - $0 >= 0.5 }) == true { model.setLooping(!model.looping) }
                        else { model.playPause() }
                    }
                    resetPress()
                })
            .accessibilityIdentifier("transport.play")
            .accessibilityLabel(AureaText.t(model.looping ? "editor_repeticao_ligada_segure_desligar" :
                (model.status.playing != 0 ? "editor_pausar" : "editor_reproduzir_segure_repetir")))
            .accessibilityHint(AureaText.t("prerender_gesture_hint"))
            .accessibilityValue(progressText)
            .accessibilityAction(named: Text(AureaText.t("prerender_forward"))) { model.startPreRender(1) }
            .accessibilityAction(named: Text(AureaText.t("prerender_backward"))) { model.startPreRender(-1) }
            .accessibilityAction(named: Text(AureaText.t("prerender_cancel"))) { model.cancelPreRender() }
            .accessibilityAction(named: Text(AureaText.t("editor_reproducao_loop"))) { model.setLooping(!model.looping) }
            .onDisappear { resetPress() }
    }

    private func resetPress() {
        hold?.cancel(); hold = nil; pressID = nil; began = nil
        pressed = false; armed = false; cancelled = false; fired = false
    }
}

/// Omega chrome (área 02, P1-4): a barra de informações no lugar da de reprodução
/// enquanto um dedo manipula algo no palco — modo texto, até 6 pares em 3
/// colunas: rótulo 12 a 50 % e valor 12. (A timeline mostra o tempo do arrasto
/// dela na própria faixa: área 03.)
private struct StageInfoBar: View {
    @EnvironmentObject private var model: AureaModel
    var body: some View {
        let position = StageGeom.floats(model.detail["position"])
        let scale = StageGeom.floats(model.detail["scale"])
        let rotation = StageGeom.floats(model.detail["rotation"])
        let pairs: [(String, String)] = [
            ("X", String(Int((position.first ?? 0).rounded()))),
            ("Y", String(Int((position.count > 1 ? position[1] : 0).rounded()))),
            (AureaText.t("editor_escala"), "\(Int(((scale.first ?? 1) * 100).rounded()))%"),
            (AureaText.t("editor_rotacao"), String(format: "%.1f°", rotation.count > 2 ? rotation[2] : 0).replacingOccurrences(of: ".", with: ",")),
        ]
        let lines = stride(from: 0, to: min(6, pairs.count), by: 3).map { Array(pairs[$0..<min($0 + 3, pairs.count)]) }
        VStack(spacing: 0) {
            ForEach(Array(lines.enumerated()), id: \.offset) { _, line in
                HStack(spacing: 0) {
                    ForEach(Array(line.enumerated()), id: \.offset) { _, pair in
                        HStack(spacing: 6) {
                            Text(pair.0).foregroundStyle(AureaColors.text.opacity(0.5))
                            Text(pair.1).monospacedDigit().foregroundStyle(AureaColors.text)
                        }.font(.aurea(size: 12)).lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
                    }
                    ForEach(0..<(3 - line.count), id: \.self) { _ in Color.clear.frame(maxWidth: .infinity) }
                }.frame(maxHeight: .infinity)
            }
        }
        .padding(.horizontal, 12).padding(.vertical, 4)
        .frame(height: EditorLayout.transport).background(OmegaChrome.bar)
        .accessibilityElement(children: .combine)
        .accessibilityIdentifier("transport.info")
    }
}

struct FullscreenTimeBar: View {
    @EnvironmentObject private var model: AureaModel
    @State private var dragging = false
    @State private var wasPlaying = false
    var body: some View {
        HStack(spacing: 12) {
            Text(ShellClock.tenths(model.status.playhead, Float(model.compositionFps))).foregroundStyle(AureaColors.text)
            GeometryReader { geometry in
                let fraction = model.compositionDuration > 1 ? min(1, max(0, Double(model.status.playhead) / Double(model.compositionDuration - 1))) : 0
                Canvas { context, size in
                    let y = size.height / 2, x = size.width * fraction
                    var track = Path(); track.move(to: CGPoint(x: 0, y: y)); track.addLine(to: CGPoint(x: size.width, y: y))
                    context.stroke(track, with: .color(AureaColors.track), style: StrokeStyle(lineWidth: dragging ? 5 : 3, lineCap: .round))
                    var progress = Path(); progress.move(to: CGPoint(x: 0, y: y)); progress.addLine(to: CGPoint(x: x, y: y))
                    context.stroke(progress, with: .color(AureaColors.accent), style: StrokeStyle(lineWidth: dragging ? 5 : 3, lineCap: .round))
                    let r: CGFloat = dragging ? 9 : 7
                    context.fill(Path(ellipseIn: CGRect(x: x - r, y: y - r, width: r * 2, height: r * 2)), with: .color(AureaColors.text))
                }.contentShape(Rectangle())
                    .gesture(DragGesture(minimumDistance: 0).onChanged { value in
                        let fraction = min(1, max(0, value.location.x / max(1, geometry.size.width)))
                        let frame = Int64((fraction * CGFloat(max(0, model.compositionDuration - 1))).rounded())
                        if !dragging {
                            wasPlaying = model.status.playing != 0
                            if wasPlaying { model.playPause() }
                            model.engine.run { $0.scrubBegin() }; dragging = true
                        }
                        model.engine.run { $0.scrub(toFrame: frame) }; model.optimisticPlayhead(frame)
                    }.onEnded { _ in
                        model.engine.run { $0.scrubEnd() }; dragging = false
                        if wasPlaying { model.playPause() }; wasPlaying = false
                    })
            }
            Text(ShellClock.tenths(model.compositionDuration, Float(model.compositionFps))).foregroundStyle(AureaColors.muted)
        }.font(.aurea(size: 12, weight: .semibold)).monospacedDigit().padding(.horizontal, 14).frame(height: StageDim.fullscreenTimeBar)
            .background(AureaColors.editorTopBar)
            // Barra de tempo: o início à esquerda em qualquer idioma (o gesto lê x absoluto).
            .keepLtr()
            .onDisappear { if dragging { model.engine.run { $0.scrubEnd() }; dragging = false; if wasPlaying { model.playPause() }; wasPlaying = false } }
    }
}

/// Omega chrome (área 02, P0-6/P1-3): o 6º botão do transporte e o popup dele
/// (`layer_copy_paste_menu_popup`), ancorado no botão. Itens na ordem da
/// referência: selecionar todas (ou limpar, com todas escolhidas) · duplicar ·
/// salvar preset · copiar (com estilo, quando é uma) · colar · colar estilo ›.
/// Depois de um fio, "Mais do AUREA": selecionar uma camada, escolher várias,
/// busca de ferramentas, efeitos, keyframes e animação e, com várias
/// escolhidas, ajustar/preencher a tela, desagrupar e vincular.
private struct CopyPasteButton: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    let side: CGFloat
    @State private var menu = false
    @State private var anchor: CGRect = .zero

    var body: some View {
        ShellBarButton(glyph: CupertinoGlyph.DocOnClipboard, description: AureaText.t("om_tr_copy_paste"), size: 20, width: side, height: EditorLayout.transport) {
            if model.status.playing != 0 { model.playPause() }
            menu = true
        }
        .accessibilityIdentifier("transport.copyPaste")
        .background(GeometryReader { g in
            Color.clear
                .onAppear { anchor = g.frame(in: .global) }
                .onChange(of: g.frame(in: .global)) { anchor = $0 }
        })
        .aureaAnchoredMenu(isPresented: $menu, tag: "copyPaste.menu") { entries() }
    }

    private func entries() -> [MenuEntry] {
        let ids = model.layers.filter { model.selection.contains($0.id) }.map { NSNumber(value: $0.id) }
        let count = ids.count
        let total = model.layers.count
        let clip = model.engine.clipboardState
        let primary = model.primarySelection
        let refresh: () -> Void = { model.refreshModel(force: true) }
        var out: [MenuEntry] = []
        if total > 0 && count == total {
            out.append(.item(AureaText.t("editor_limpar_selecao"), glyph: CupertinoGlyph.Square, tag: "copyPaste.clearSelection") { model.clearSelection() })
        } else {
            out.append(.item(AureaText.t("editor_selecionar_todas_camadas"), glyph: CupertinoGlyph.CheckmarkSquare, enabled: total > 0,
                             tag: "copyPaste.selectAll") { model.selectAll() })
        }
        out.append(.item(AureaText.plural("om_cp_duplicate", max(1, count)), glyph: CupertinoGlyph.PlusSquareOnSquare, enabled: count > 0,
                         tag: "copyPaste.duplicate") { model.engine.duplicateLayers(ids); refresh() })
        out.append(.item(AureaText.t("om_cp_save_preset"), glyph: CupertinoGlyph.SquareStack3dUp, enabled: count == 1,
                         tag: "copyPaste.savePreset") { model.openPanel(.presets) })
        out.append(.item(AureaText.plural("om_cp_copy", max(1, count)), glyph: CupertinoGlyph.DocOnDoc,
                         sublabel: count == 1 ? AureaText.t("om_cp_with_style") : nil, enabled: count > 0,
                         tag: "copyPaste.copy") { model.copyLayersWithStyle() })
        out.append(.item(AureaText.plural("om_cp_paste", 1), glyph: CupertinoGlyph.DocOnClipboard, enabled: clip & 1 != 0,
                         tag: "copyPaste.paste") { model.engine.pasteLayers(model.status.playhead); refresh() })
        out.append(.item(AureaText.t("om_cp_paste_style_more"), glyph: ShellGlyph.PaintbrushFill, chevron: true,
                         enabled: count > 0 && clip & 2 != 0, tag: "copyPaste.pasteStyle") { presentPasteStyle() })
        // Mais do AUREA (o que a referência não tem), depois de um fio.
        out.append(.divider)
        out.append(.header(AureaText.t("om_settings_more")))
        out.append(.item(AureaText.t("editor_selecionar_uma_camada"), glyph: CupertinoGlyph.RectangleStack, enabled: total > 0,
                         tag: "copyPaste.more.pickLayer") { presentPicker() })
        out.append(.item(AureaText.t("edt_tl_pick_layers"), glyph: CupertinoGlyph.CheckmarkSquare, sublabel: AureaText.t("edt_tl_pick_layers_detail"),
                         checked: model.timelineLayerSelectMode, enabled: total > 0, tag: "copyPaste.more.pickMode") {
            model.changeTimelineLayerSelectMode(!model.timelineLayerSelectMode)
        })
        out.append(.item(AureaText.t("edt_cmd_search_desc"), glyph: CupertinoGlyph.Search, tag: "commandSearchOpen") { shell.commandSearch = true })
        out.append(.item(AureaText.t("editor_copiar_efeitos"), glyph: CupertinoGlyph.Sparkles, enabled: primary != nil,
                         tag: "copyPaste.more.copyEffects") { if let id = primary { model.engine.copyEffects(id) } })
        out.append(.item(AureaText.t("editor_colar_efeitos"), glyph: CupertinoGlyph.WandStars, enabled: count > 0 && clip & 4 != 0,
                         tag: "copyPaste.more.pasteEffects") { model.engine.pasteEffects(ids); refresh() })
        out.append(.item(AureaText.t("editor_copiar_keyframes_cabecote"), glyph: CupertinoGlyph.DocOnDoc, enabled: primary != nil,
                         tag: "copyPaste.more.copyKeys") {
            if let id = primary { model.engine.copyKeyframes(id, atFrame: Int32(clamping: model.status.playhead)) }
        })
        out.append(.item(AureaText.t("editor_colar_keyframes_cabecote"), glyph: CupertinoGlyph.DocOnClipboard, enabled: count > 0 && clip & 8 != 0,
                         tag: "copyPaste.more.pasteKeys") {
            model.engine.pasteKeyframes(ids, atFrame: Int32(clamping: model.status.playhead)); refresh()
        })
        out.append(.item(AureaText.t("editor_copiar_animacao"), glyph: CupertinoGlyph.DocOnDoc, sublabel: AureaText.t("editor_copiar_animacao_detalhe"),
                         enabled: primary != nil, tag: "copyPaste.more.copyAnimation") { model.copyAnimation() })
        out.append(.item(AureaText.t("editor_otimizar_keyframes"), glyph: CupertinoGlyph.WandStars, sublabel: AureaText.t("editor_otimizar_keyframes_detalhe"),
                         enabled: primary != nil, tag: "copyPaste.more.optimizeKeys") { model.optimizeKeyframes() })
        if count >= 2 {
            let chosen = ids.map(\.int64Value)
            out.append(.item(AureaText.t("editor_ajustar_tela"), glyph: CupertinoGlyph.FullscreenExit, tag: "copyPaste.more.fit") {
                if model.status.playing != 0 { model.playPause() }; model.fitToCanvas(chosen, fill: false)
            })
            out.append(.item(AureaText.t("editor_preencher_tela"), glyph: CupertinoGlyph.Fullscreen, tag: "copyPaste.more.fill") {
                if model.status.playing != 0 { model.playPause() }; model.fitToCanvas(chosen, fill: true)
            })
            // 0.3.3 recursos: "Em escada" (cada uma começa quando a de cima termina) — no
            // telefone em pé a folha de lote da 0.3.2 saiu e a barra de baixo não tem esta.
            out.append(.item(AureaText.t("editor_escada_comeca_quando_cima_termina"), glyph: CupertinoGlyph.ArrowRightToLine,
                             tag: "copyPaste.more.stairs") { model.arrangeLayerTimes(1) })
            let groups =model.layers.filter { model.selection.contains($0.id) && $0.kind == 12 }.map(\.id)
            if !groups.isEmpty {
                out.append(.item(AureaText.t("editor_desagrupar"), glyph: ShellGlyph.SquareSplit2x2, tag: "copyPaste.more.ungroup") {
                    for group in groups { model.ungroup(group) }
                })
            }
            out.append(.item(AureaText.t("om_cp_link_selected"), glyph: CupertinoGlyph.Link, tag: "copyPaste.more.link") {
                if model.status.playing != 0 { model.playPause() }
                shell.linkIds = Array(model.selection); shell.linkAnchor = anchor
            })
        }
        return out
    }

    private func presentPasteStyle() {
        let owner = UUID()
        let model = self.model
        OmegaPopup.present(anchor: anchor, placement: .below, owner: owner, onDismiss: {}) {
            PasteStylePage(model: model) { OmegaPopup.dismiss(owner) }
        }
    }

    private func presentPicker() {
        let owner = UUID()
        let model = self.model
        OmegaPopup.present(anchor: anchor, placement: .below, owner: owner, onDismiss: {}) {
            LayerPickerPopupView { OmegaPopup.dismiss(owner) }.environmentObject(model)
        }
    }
}

/// A subpágina "Colar estilo": duas fileiras de botões de 58 pt (cor e
/// preenchimento · borda e sombra · mesclagem e opacidade / mover e transformar ·
/// texto · volume · efeitos · velocidade). Habilitado = a parte existe no estilo
/// copiado; aceso = escolhido. Ao escolher, o título mostra o nome por 1 s.
/// Colar aplica só as escolhidas (um passo de desfazer).
private struct PasteStylePage: View {
    @ObservedObject var model: AureaModel
    let close: () -> Void
    @State private var available: UInt32 = 0
    @State private var chosen: UInt32 = 0
    @State private var flash: String?
    @State private var flashToken = 0
    @State private var enter: CGFloat = 0.5
    private var colors: MenuColors { MenuColors.of(.light) }
    private static let rowA: [(UInt32, Character, String, String)] = [
        (1, CupertinoGlyph.DropFill, "om_ps_color_fill", "pasteStyle.colorFill"),
        (2, CupertinoGlyph.SquareOnSquare, "om_ps_border_shadow", "pasteStyle.borderShadow"),
        (4, CupertinoGlyph.CircleLefthalfFill, "om_ps_blend", "pasteStyle.blend"),
    ]
    private static let rowB: [(UInt32, Character, String, String)] = [
        (8, CupertinoGlyph.Move, "om_ps_transform", "pasteStyle.transform"),
        (16, CupertinoGlyph.Textformat, "om_ps_text", "pasteStyle.text"),
        (32, CupertinoGlyph.Speaker2, "om_ps_volume", "pasteStyle.volume"),
        (64, CupertinoGlyph.Sparkles, "om_ps_effects", "pasteStyle.effects"),
        (128, CupertinoGlyph.Speedometer, "om_ps_speed", "pasteStyle.speed"),
    ]

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 0) {
                Button(action: close) {
                    CupertinoGlyph.text(CupertinoGlyph.ChevronLeft, size: 16, color: colors.text).mirrorsInRtl()
                        .frame(width: 40, height: 40).contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel(AureaText.t("om_menu_back"))
                .accessibilityIdentifier("pasteStyle.back")
                Text(flash ?? AureaText.t("om_cp_paste_style_more")).font(.aurea(size: 15, weight: .semibold)).foregroundStyle(colors.text)
                    .lineLimit(1).frame(maxWidth: .infinity, alignment: .leading).padding(.trailing, 12)
            }.frame(height: 40)
            colors.divider.frame(height: 1)
            partRow(Self.rowA)
            partRow(Self.rowB.filter { $0.0 != 32 || available & 32 != 0 })
            colors.divider.frame(height: 1)
            HStack(spacing: 0) {
                Button {
                    let parts = chosen
                    close()
                    model.pasteStyleParts(parts)
                } label: {
                    Text(AureaText.t("om_ps_paste")).font(.aurea(size: 15, weight: .semibold)).foregroundStyle(colors.accent)
                        .frame(maxWidth: .infinity, maxHeight: .infinity).contentShape(Rectangle())
                }
                .buttonStyle(AureaPressStyle(shrink: 1))
                .disabled(chosen == 0)
                .opacity(chosen == 0 ? AnchoredMenuMetrics.disabledAlpha : 1)
                .accessibilityIdentifier("pasteStyle.paste")
                colors.divider.frame(width: 1)
                Button(action: close) {
                    Text(AureaText.t("common_cancel")).font(.aurea(size: 15)).foregroundStyle(colors.text)
                        .frame(maxWidth: .infinity, maxHeight: .infinity).contentShape(Rectangle())
                }
                .buttonStyle(AureaPressStyle(shrink: 1))
                .accessibilityIdentifier("pasteStyle.cancel")
            }.frame(height: 46)
        }
        .frame(width: AnchoredMenuMetrics.width)
        .background(colors.background, in: RoundedRectangle(cornerRadius: AnchoredMenuMetrics.radius))
        .overlay(RoundedRectangle(cornerRadius: AnchoredMenuMetrics.radius).stroke(colors.divider, lineWidth: 0.5))
        .clipShape(RoundedRectangle(cornerRadius: AnchoredMenuMetrics.radius))
        .offset(x: enter * AnchoredMenuMetrics.width)
        .accessibilityIdentifier("pasteStyle.page")
        .onAppear {
            available = model.copiedStyleParts
            chosen = available
            // Entra deslizando meia largura com repique (250 ms).
            withAnimation(.spring(response: 0.25, dampingFraction: 0.7)) { enter = 0 }
        }
    }

    private func partRow(_ parts: [(UInt32, Character, String, String)]) -> some View {
        HStack(spacing: 0) {
            ForEach(Array(parts.enumerated()), id: \.offset) { _, part in
                let enabled = available & part.0 != 0
                let on = enabled && chosen & part.0 != 0
                Button {
                    chosen ^= part.0
                    if chosen & part.0 != 0 { showFlash(AureaText.t(part.2)) }
                } label: {
                    CupertinoGlyph.text(part.1, size: 20, color: on ? AureaColors.onAccent : colors.text)
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                        .background(on ? AureaColors.accent : colors.pressed, in: RoundedRectangle(cornerRadius: 6))
                        .padding(3).contentShape(Rectangle())
                }
                .buttonStyle(AureaPressStyle(shrink: 1))
                .disabled(!enabled)
                .opacity(enabled ? 1 : AnchoredMenuMetrics.disabledAlpha)
                .accessibilityLabel(AureaText.t(part.2))
                .accessibilityAddTraits(on ? .isSelected : [])
                .accessibilityIdentifier(part.3)
            }
        }
        .padding(.horizontal, 4).padding(.vertical, 3)
        .frame(height: 58)
    }

    private func showFlash(_ text: String) {
        flashToken &+= 1
        let mine = flashToken
        flash = text
        DispatchQueue.main.asyncAfter(deadline: .now() + 1) { if mine == flashToken { flash = nil } }
    }
}

/// "Selecionar uma camada…" como popup ancorado (no Copiar e colar), com a busca
/// no topo quando há 7 camadas ou mais. Tocar numa escolhe SÓ ela e fecha.
private struct LayerPickerPopupView: View {
    @EnvironmentObject private var model: AureaModel
    let close: () -> Void
    @State private var search = ""
    private var colors: MenuColors { MenuColors.of(.light) }
    private var hits: [LayerItem] {
        let query = search.trimmingCharacters(in: .whitespacesAndNewlines)
        return model.layers.filter { row in
            query.isEmpty || row.name.localizedCaseInsensitiveContains(query)
                || (row.kind == 4 && (model.engine.text(forLayer: row.id)?["content"] as? String ?? "").localizedCaseInsensitiveContains(query))
        }
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text(AureaText.t("editor_selecionar_uma_camada")).font(.aurea(size: 11)).foregroundStyle(colors.muted)
                .padding(.horizontal, 14).padding(.top, 6).padding(.bottom, 4)
            if model.layers.count >= 7 || !search.isEmpty {
                TextField(AureaText.t("editor_nome_ou_texto_camada"), text: $search).font(.aurea(size: 14)).foregroundStyle(colors.text).tint(colors.accent)
                    .padding(.horizontal, 10).padding(.vertical, 9)
                    .background(colors.pressed, in: RoundedRectangle(cornerRadius: 6))
                    .padding(.horizontal, 10).padding(.vertical, 4)
                    .accessibilityIdentifier("layerPicker.search")
            }
            if !search.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && hits.isEmpty {
                Text(AureaText.t("sh_menu_no_layer_matches", search.trimmingCharacters(in: .whitespacesAndNewlines)))
                    .font(.aurea(size: 13)).foregroundStyle(colors.muted).padding(.horizontal, 14).padding(.vertical, 10)
            }
            ScrollView {
                VStack(spacing: 0) {
                    ForEach(hits) { row in
                        Button {
                            close()
                            model.select(layerId: row.id, additive: false)
                        } label: {
                            HStack(spacing: 10) {
                                ShellLayerThumbnail(row: row, size: 32)
                                Text(row.name.isEmpty ? AureaText.t("editor_camada") : row.name).font(.aurea(size: 14)).foregroundStyle(colors.text)
                                    .lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
                                if row.label > 0 && Int(row.label) <= AureaColors.labelPalette.count {
                                    Circle().fill(AureaColors.labelPalette[Int(row.label) - 1]).frame(width: 10, height: 10)
                                }
                                if row.selected { CupertinoGlyph.text(CupertinoGlyph.CheckmarkAlt, size: 16, color: colors.accent) }
                            }
                            .padding(.horizontal, 10).frame(maxWidth: .infinity, minHeight: 48)
                            .background(row.selected ? colors.pressed : Color.clear)
                            .contentShape(Rectangle())
                        }
                        .buttonStyle(AureaPressStyle(shrink: 1))
                        .accessibilityLabel(row.name)
                        .accessibilityIdentifier("layerPicker.row.\(row.id)")
                    }
                }
            }
            .frame(maxHeight: 360)
        }
        .padding(.vertical, AnchoredMenuMetrics.verticalPadding)
        .frame(width: AnchoredMenuMetrics.width)
        .background(colors.background, in: RoundedRectangle(cornerRadius: AnchoredMenuMetrics.radius))
        .overlay(RoundedRectangle(cornerRadius: AnchoredMenuMetrics.radius).stroke(colors.divider, lineWidth: 0.5))
        .clipShape(RoundedRectangle(cornerRadius: AnchoredMenuMetrics.radius))
        .accessibilityIdentifier("layerPicker.popup")
    }
}

/// Omega chrome (área 02, P2-4): "Informações da mídia" como o diálogo claro da
/// referência: Nome · Dimensões · Taxa de quadros · Duração · Formato · Tamanho ·
/// Taxa de amostragem, e depois os extras do AUREA (codecs, canais, HDR). O que
/// não se aplica ao tipo some (imagem sem taxa, duração e amostragem).
struct MediaInfoDialogView: View {
    let path: String
    let onDismiss: () -> Void
    @State private var rows: [(String, String)]?
    @State private var loaded = false

    var body: some View {
        AureaDialog(title: AureaText.t("media_info_title"),
                    actions: [DialogAction(AureaText.t("common_ok"), role: .positive, tag: "mediaInfo.ok")],
                    onDismiss: onDismiss, tag: "mediaInfo.dialog",
                    content: AnyView(table))
            .task {
                rows = await Self.load(path)
                loaded = true
            }
    }

    private var table: some View {
        VStack(alignment: .leading, spacing: 6) {
            if loaded, let rows {
                ForEach(Array(rows.enumerated()), id: \.offset) { _, row in
                    HStack(alignment: .firstTextBaseline, spacing: 16) {
                        Text(AureaText.t(row.0))
                        Spacer(minLength: 8)
                        Text(row.1).multilineTextAlignment(.trailing)
                    }.font(.aurea(size: 14)).foregroundStyle(AureaColors.lightText)
                }
            } else if loaded {
                Text(AureaText.t("media_info_unreadable")).font(.aurea(size: 14)).foregroundStyle(AureaColors.lightText)
            }
        }.padding(.horizontal, 10)
    }

    /// As linhas do `MediaInfoProbe` na ordem da referência, com a duração em
    /// relógio (hhh:mm:ss.ttt no vídeo, hhh:mm:ss no áudio) e o formato.
    private static func load(_ path: String) async -> [(String, String)]? {
        guard let probe = await MediaInfoProbe.read(path) else { return nil }
        var by: [String: String] = [:]
        for (key, value) in probe where by[key] == nil { by[key] = value }
        let url = path.hasPrefix("file://") ? (URL(string: path) ?? URL(fileURLWithPath: path)) : URL(fileURLWithPath: path)
        let image = by["media_info_duration"] == nil && by["media_info_audio_codec"] == nil
        let audioOnly = by["media_info_resolution"] == nil && by["media_info_audio_codec"] != nil
        var out: [(String, String)] = []
        out.append(("om_mi_name", by["media_info_file"] ?? url.lastPathComponent))
        if !audioOnly, let size = by["media_info_resolution"] { out.append(("om_mi_dimensions", size)) }
        if !image, let fps = by["media_info_fps"] { out.append(("media_info_fps", fps)) }
        if !image, by["media_info_duration"] != nil {
            if let seconds = try? await AVURLAsset(url: url).load(.duration).seconds, seconds.isFinite, seconds > 0 {
                out.append(("media_info_duration", clock(seconds, millis: !audioOnly)))
            } else if let duration = by["media_info_duration"] {
                out.append(("media_info_duration", duration))
            }
        }
        let format = url.pathExtension.uppercased()
        if !format.isEmpty { out.append(("om_mi_format", format)) }
        if let size = by["media_info_size"] { out.append(("media_info_size", size)) }
        var channels = ""
        if !image, let rate = by["media_info_sample_rate"] {
            let parts = rate.components(separatedBy: " · ")
            out.append(("media_info_sample_rate", parts.first ?? rate))
            if parts.count > 1 { channels = " · " + parts[1] }
        }
        // Extras do AUREA.
        if let codec = by["media_info_video_codec"] { out.append(("media_info_video_codec", codec)) }
        if let codec = by["media_info_audio_codec"] { out.append(("media_info_audio_codec", codec + channels)) }
        if let hdr = by["media_info_hdr"] { out.append(("media_info_hdr", hdr)) }
        return out
    }

    private static func clock(_ seconds: Double, millis: Bool) -> String {
        let totalMs = Int((seconds * 1000).rounded())
        let h = totalMs / 3_600_000, m = (totalMs / 60_000) % 60, s = (totalMs / 1000) % 60, ms = totalMs % 1000
        return millis ? String(format: "%d:%02d:%02d.%03d", h, m, s, ms) : String(format: "%d:%02d:%02d", h, m, s)
    }
}

private struct ShellMenuHeightKey: PreferenceKey {
    static var defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) { value = max(value, nextValue()) }
}
private struct ShellMenu<Content: View>: View {
    let maxHeight: CGFloat
    let bottomInset: CGFloat
    let onDismiss: () -> Void
    @ViewBuilder let content: () -> Content
    @State private var contentHeight: CGFloat = 1000
    @State private var drag: CGFloat = 0
    var body: some View {
        VStack(spacing: 0) {
            Capsule().fill(StageInk.menuHandle).frame(width: 36, height: 4).padding(.top, 6)
                .frame(maxWidth: .infinity).contentShape(Rectangle().inset(by: -12))
                .gesture(DragGesture().onChanged { drag = max(0, $0.translation.height) }.onEnded { value in
                    if value.translation.height > 60 || value.predictedEndTranslation.height > 140 { onDismiss() }; drag = 0
                })
            ScrollView {
                VStack(alignment: .leading, spacing: 0, content: content).padding(.bottom, 12 + bottomInset)
                    .background(GeometryReader { bounds in Color.clear.preference(key: ShellMenuHeightKey.self, value: bounds.size.height) })
            }.frame(height: min(maxHeight, contentHeight))
        }.onPreferenceChange(ShellMenuHeightKey.self) { contentHeight = $0 }
            .background(AureaColors.editorPanel).foregroundStyle(AureaColors.text)
            .clipShape(ShellTopCorners(radius: 18)).offset(y: drag)
    }
}
private struct ShellTopCorners: Shape {
    let radius: CGFloat
    func path(in rect: CGRect) -> Path { Path(UIBezierPath(roundedRect: rect, byRoundingCorners: [.topLeft, .topRight], cornerRadii: CGSize(width: radius, height: radius)).cgPath) }
}

private struct ShellMenuSection: View {
    let key: String
    init(_ key: String) { self.key = key }
    var body: some View {
        Text(AureaText.t(key)).font(.aurea(size: 12, weight: .semibold)).tracking(aureaTracking(0.2)).foregroundStyle(AureaColors.muted)
            .padding(.horizontal, 20).padding(.top, 14).padding(.bottom, 4)
    }
}
private struct ShellMenuRow: View {
    let glyph: Character
    let key: String
    var title: String? = nil
    var enabled = true
    var checked: Bool? = nil
    var detail: String? = nil
    var danger = false
    /// Símbolo do sistema no lugar do glifo (ex.: a lupa da prévia, a mesma do botão antigo).
    var symbol: String? = nil
    let action: () -> Void
    init(_ glyph: Character, _ key: String, title: String? = nil, enabled: Bool = true, checked: Bool? = nil,
         detail: String? = nil, danger: Bool = false, symbol: String? = nil, action: @escaping () -> Void) {
        self.glyph = glyph; self.key = key; self.title = title; self.enabled = enabled; self.checked = checked
        self.detail = detail; self.danger = danger; self.symbol = symbol; self.action = action
    }
    var body: some View {
        Button(action: action) {
            HStack(spacing: 14) {
                if let symbol {
                    Image(systemName: symbol).font(.system(size: 17, weight: AureaIconWeight.symbol)).foregroundStyle(color).frame(width: 20, height: 20)
                } else {
                    CupertinoGlyph.text(glyph, size: 20, color: color).mirrorsInRtl(CupertinoGlyph.mirrorsInRtl(glyph)).frame(width: 20, height: 20)
                }
                VStack(alignment: .leading, spacing: 0) {
                    Text(title ?? AureaText.t(key)).font(.aurea(size: 15)).foregroundStyle(color)
                    if let detail { Text(AureaText.t(detail)).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted) }
                }.frame(maxWidth: .infinity, alignment: .leading)
                if checked == true { CupertinoGlyph.text(CupertinoGlyph.CheckmarkAlt, size: 18, color: AureaColors.accent) }
            }.padding(.horizontal, 20).padding(.vertical, 8).frame(minHeight: 48)
        }.buttonStyle(.plain).disabled(!enabled)
            // O glifo da fonte de ícones entrava no rótulo como vazio (", Colar efeitos").
            .accessibilityLabel(title ?? AureaText.t(key))
            .accessibilityHint(detail.map { AureaText.t($0) } ?? "")
            .accessibilityAddTraits(checked == true ? .isSelected : [])
    }
    private var color: Color { !enabled ? StageInk.disabledMuted : danger ? AureaColors.danger : AureaColors.text }
}

// One editor-sized host for bottom sheets, dialogs and anchored shell popups.
// Omega chrome (área 02): o ⋮ da camada e o Copiar e colar são menus ancorados
// nos próprios botões (AnchoredMenu.swift); o menu da engrenagem saiu (o ⚙ abre
// os Ajustes direto). Aqui ficam os diálogos, a lista "Selecionar uma camada"
// da busca de ferramentas e o popup de pai.
@MainActor struct ShellOverlayHost: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    @State private var search = ""
    @State private var linkHeight: CGFloat = 330
    @FocusState private var searchFocused: Bool
    private var layer: LayerItem? { model.selectedLayer }
    private var searchHits: [LayerItem] {
        let query = search.trimmingCharacters(in: .whitespacesAndNewlines)
        return model.layers.filter { row in
            query.isEmpty || row.name.localizedCaseInsensitiveContains(query) || (row.kind == 4 && (model.engine.text(forLayer: row.id)?["content"] as? String ?? "").localizedCaseInsensitiveContains(query))
        }
    }
    var body: some View {
        GeometryReader { geometry in
            ZStack(alignment: .topLeading) {
                if let sheet = shell.sheet {
                    switch sheet {
                    case .renameLayer:
                        AureaNamePrompt(title: AureaText.t("editor_renomear"), initial: layer?.name ?? "", onConfirm: { name in
                            if let layer, !layer.locked { model.mutate { $0.setLayer(layer.id, name: name) }; model.refreshModel(force: true) }
                        }, onDismiss: dismiss)
                    case .goToTime: ShellTimeDialog(initial: shell.timeInput, onDismiss: dismiss)
                    case .projectSettings: EmptyView()
                    case .layerMenu, .copyPaste:
                        Color.clear.onAppear { shell.sheet = nil }
                    case .timelineMenu:
                        Color.clear.onAppear { shell.sheet = nil; model.showProjectSettings = true }
                    default:
                        StageInk.menuScrim.ignoresSafeArea().onTapGesture(perform: dismiss)
                        ShellMenu(maxHeight: geometry.size.height * 0.7, bottomInset: geometry.safeAreaInsets.bottom, onDismiss: dismiss) {
                            searchMenu
                        }.id(sheet).frame(maxHeight: .infinity, alignment: .bottom).ignoresSafeArea(edges: .bottom)
                    }
                }
                if let anchor = shell.linkAnchor {
                    Color.clear.contentShape(Rectangle()).ignoresSafeArea().onTapGesture { shell.linkAnchor = nil }
                    let size = CGSize(width: min(AnchoredMenuMetrics.width, max(1, geometry.size.width - 16)),
                                      height: min(AnchoredMenuMetrics.maxHeight, min(geometry.size.height - 16, linkHeight)))
                    let pos = popupPosition(anchor, size, geometry)
                    ShellLinkPopup(ids: shell.linkIds, dismiss: { shell.linkAnchor = nil }, onHeight: { linkHeight = $0 })
                        .frame(width: size.width, height: size.height).offset(x: pos.x, y: pos.y)
                }
            }.onChange(of: shell.sheet) { sheet in if sheet == .searchLayers { search = "" } }
        }
        .allowsHitTesting(shell.sheet != nil || shell.linkAnchor != nil)
    }
    /// Abaixo da âncora, pela borda de INÍCIO (a regra do menu ancorado); para cima se não couber.
    private func popupPosition(_ anchor: CGRect, _ size: CGSize, _ geometry: GeometryProxy) -> CGPoint {
        let origin = geometry.frame(in: .global).origin
        let a = anchor.offsetBy(dx: -origin.x, dy: -origin.y)
        return CGPoint(
            x: AnchoredMenuPosition.x(anchorLeft: a.minX, anchorRight: a.maxX, windowWidth: geometry.size.width, popupWidth: size.width,
                                      rtl: false, alignEnd: false, margin: AnchoredMenuMetrics.screenMargin),
            y: AnchoredMenuPosition.y(anchorTop: a.minY, anchorBottom: a.maxY, windowHeight: geometry.size.height, popupHeight: size.height,
                                      margin: AnchoredMenuMetrics.screenMargin, gap: AnchoredMenuMetrics.anchorGap))
    }
    private func dismiss() { shell.dismiss(); UIApplication.shared.sendAction(#selector(UIResponder.resignFirstResponder), to: nil, from: nil, for: nil) }
    private func act(_ action: () -> Void) { dismiss(); action() }
    /// SELECIONAR UMA CAMADA em folha: o caminho da busca de ferramentas (sem botão
    /// por perto para ancorar; no editor ela abre ancorada no Copiar e colar).
    /// Todas as camadas (miniatura ou selo do tipo + nome); tocar escolhe SÓ ela.
    @ViewBuilder private var searchMenu: some View {
        ShellMenuSection("editor_selecionar_uma_camada")
        if model.layers.count >= 7 || !search.isEmpty {
            TextField(AureaText.t("editor_nome_ou_texto_camada"), text: $search).font(.aurea(size: 14)).foregroundStyle(AureaColors.text).tint(AureaColors.accent)
                .focused($searchFocused).padding(.horizontal, 12).padding(.vertical, 10).frame(minHeight: 40)
                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 10)).padding(.horizontal, 16).padding(.vertical, 4)
        }
        if !search.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && searchHits.isEmpty {
            Text(AureaText.t("sh_menu_no_layer_matches", search.trimmingCharacters(in: .whitespacesAndNewlines))).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted).padding(.horizontal, 20).padding(.vertical, 12)
        }
        ForEach(searchHits) { row in
            Button { act { model.select(layerId: row.id, additive: false) } } label: {
                HStack(spacing: 12) {
                    ShellLayerThumbnail(row: row)
                    Text(row.name.isEmpty ? AureaText.t("editor_camada") : row.name).font(.aurea(size: 15)).foregroundStyle(row.selected ? AureaColors.accent : AureaColors.text).lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
                    if row.label > 0 && Int(row.label) <= AureaColors.labelPalette.count {
                        Circle().fill(AureaColors.labelPalette[Int(row.label) - 1]).frame(width: 10, height: 10)
                    }
                    if row.selected { CupertinoGlyph.text(CupertinoGlyph.CheckmarkAlt, size: 16, color: AureaColors.accent) }
                }.padding(.horizontal, 16).frame(maxWidth: .infinity, minHeight: 56).contentShape(Rectangle())
            }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityLabel(row.name)
        }
    }

}

private struct ShellTimeDialog: View {
    @EnvironmentObject private var model: AureaModel
    let initial: String
    let onDismiss: () -> Void
    @State private var text = ""
    @FocusState private var focused: Bool
    var body: some View {
        // Omega fundação (F4): o campo do diálogo claro.
        AureaAlert(title: AureaText.t("editor_ir_tempo"), confirmLabel: AureaText.t("editor_ir"), onConfirm: seek, onDismiss: onDismiss, extra: AnyView(
            AureaDialogField(text: $text, placeholder: AureaText.t("editor_segundos_ou_mm_ss_ms"), monospacedDigits: true,
                             submitLabel: .go, focus: $focused) { onDismiss(); seek() }
        )).onAppear { text = initial; focused = true }
    }
    private func seek() { if let frame = ShellClock.parseToFrame(text, Float(model.compositionFps)) { model.seek(toFrame: frame) } }
}

/// Omega chrome (área 02, P1-1): o popup de pai (`layer_parent_menu_popup`).
/// "Nenhum" no topo (destacado quando sem pai), um fio e todas as camadas que
/// não são áudio, da frente para o fundo: faixa da etiqueta, miniatura, nome e,
/// só no pai atual, o ícone de vínculo com fundo de destaque. A própria camada
/// fica sublinhada; ela e as descendentes (fariam ciclo) aparecem apagadas e
/// sem toque. Rola até a camada atual. "Novo nulo" (extra do AUREA) no fim.
private struct ShellLinkPopup: View {
    @EnvironmentObject private var model: AureaModel
    let ids: [Int64]
    let dismiss: () -> Void
    let onHeight: (CGFloat) -> Void
    private var colors: MenuColors { MenuColors.of(.light) }
    private var current: Int64 {
        let parents = Set(model.layers.filter { ids.contains($0.id) }.map { row -> Int64 in
            (model.engine.layerDetail(row.id)?["parentId"] as? NSNumber)?.int64Value ?? 0
        })
        return parents.count == 1 ? parents.first! : -1
    }
    var body: some View {
        let candidates = Set(model.parentCandidatesForAll(ids).map(\.id))
        let visual = model.layers.filter { $0.kind != 3 }
        let now = current
        return ScrollViewReader { proxy in
            ScrollView {
                VStack(spacing: 0) {
                    row(AureaText.t("editor_nenhum"), id: 0, strip: .clear, current: now == 0, tag: "parent.none") {
                        CupertinoGlyph.text(ShellGlyph.Nosign, size: 18, color: colors.text)
                    }
                    colors.divider.frame(height: 1)
                    ForEach(visual) { layer in
                        row(layer.name.isEmpty ? AureaText.t("editor_camada") : layer.name, id: layer.id,
                            strip: layer.label > 0 && Int(layer.label) <= AureaColors.labelPalette.count ? AureaColors.labelPalette[Int(layer.label) - 1] : .clear,
                            current: now == layer.id, blank: layer.name.isEmpty, isSelf: ids.contains(layer.id),
                            enabled: candidates.contains(layer.id), tag: "parent.row.\(layer.id)") {
                            ShellLayerThumbnail(row: layer, size: 32)
                        }.id(layer.id)
                    }
                    colors.divider.frame(height: 1).padding(.vertical, 4)
                    newNullRow
                }
                .padding(.vertical, AnchoredMenuMetrics.verticalPadding)
                .background(GeometryReader { bounds in Color.clear.preference(key: ShellMenuHeightKey.self, value: bounds.size.height) })
            }
            .onAppear { if let first = ids.first { proxy.scrollTo(first, anchor: .center) } }
        }
        .onPreferenceChange(ShellMenuHeightKey.self, perform: onHeight)
        .background(colors.background, in: RoundedRectangle(cornerRadius: AnchoredMenuMetrics.radius))
        .clipShape(RoundedRectangle(cornerRadius: AnchoredMenuMetrics.radius))
        .overlay(RoundedRectangle(cornerRadius: AnchoredMenuMetrics.radius).stroke(colors.divider, lineWidth: 0.5))
        .accessibilityIdentifier("parent.menu")
    }
    /// "Novo nulo": cria o nulo no centro das escolhidas e vincula todas a ele.
    private var newNullRow: some View {
        let type: TimelineLayerType = .null
        return Button {
            dismiss(); model.parentSelectionToNewNull(ids)
        } label: {
            HStack(spacing: 6) {
                ZStack {
                    type.color
                    CupertinoGlyph.text(type.glyph, size: 16, color: .white)
                }.frame(width: 28, height: 28).clipShape(RoundedRectangle(cornerRadius: 5)).frame(width: 40, height: 40)
                Text(AureaText.t("editor_vincular_novo_nulo")).font(.aurea(size: 14)).foregroundStyle(colors.text)
                    .lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
            }.padding(.leading, 4).padding(.trailing, 12).frame(maxWidth: .infinity, minHeight: AnchoredMenuMetrics.rowHeight).contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityLabel(AureaText.t("editor_vincular_novo_nulo")).accessibilityIdentifier("parent.newNull")
    }
    /// A LINHA INTEIRA é o alvo (40 pt, largura toda).
    private func row<Thumb: View>(_ title: String, id: Int64, strip: Color, current: Bool, blank: Bool = false, isSelf: Bool = false,
                                  enabled: Bool = true, tag: String, @ViewBuilder thumb: () -> Thumb) -> some View {
        Button {
            let previous = self.current; dismiss(); if previous != id { model.setParentMany(ids, parent: id) }
        } label: {
            HStack(spacing: 0) {
                strip.frame(width: 4).frame(maxHeight: .infinity)
                thumb().frame(width: 40, height: 40)
                Text(title).underline(isSelf).font(.aurea(size: 14)).foregroundStyle(blank ? colors.muted : colors.text)
                    .lineLimit(1).frame(maxWidth: .infinity, alignment: .leading).padding(.leading, 6)
                if current {
                    CupertinoGlyph.text(CupertinoGlyph.Link, size: 18, color: colors.accent).frame(width: 40, height: 40)
                } else {
                    Color.clear.frame(width: 12, height: 1)
                }
            }
            .frame(maxWidth: .infinity).frame(height: AnchoredMenuMetrics.rowHeight)
            .background(current ? colors.pressed : Color.clear)
            .contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle(shrink: 1))
            .disabled(!enabled)
            .opacity(enabled ? 1 : AnchoredMenuMetrics.disabledAlpha)
            .accessibilityLabel(title).accessibilityAddTraits(current ? .isSelected : [])
            .accessibilityIdentifier(tag)
    }
}

@MainActor private struct ShellLayerThumbnail: View {
    @EnvironmentObject private var model: AureaModel
    let row: LayerItem
    var size: CGFloat = 44
    @State private var image: UIImage?
    @State private var cache = TimelineThumbStrip()
    private var type: TimelineLayerType { TimelineLayerType.of(row.kind) }
    var body: some View {
        ZStack {
            type.color
            if let image { Image(uiImage: image).resizable().scaledToFill() }
            else { CupertinoGlyph.text(type.glyph, size: size * 0.45, color: .white) }
        }.frame(width: size, height: size).clipShape(RoundedRectangle(cornerRadius: size < 40 ? 5 : 6))
            .onAppear(perform: load).onChange(of: model.status.thumbnailGeneration) { _ in load() }
    }
    private func load() {
        guard row.kind == 1 || row.kind == 2 else { return }
        let fps = Float(model.compositionFps), bucket = Thumbs.bucketOf(Double(row.offsetFrames), Float(model.compositionFps))
        let frame = row.kind == 2 ? row.startFrame : Int32(clamping: Int64(Thumbs.requestLocalFrame(bucket, fps)) + Int64(row.startFrame) - Int64(row.offsetFrames))
        image = cache.get(model, layer: row.id, bucket: bucket, timelineFrame: frame, heightPx: Int((size * UIScreen.main.scale).clamped(to: 16...256)), generation: model.status.thumbnailGeneration)
    }
}
