// =============================================================================
//  Aurea / platform / ios / app / EditorView.swift
//
//  O editor: a mesma casca do Android (Beta A.01), com as zonas resolvidas pelo
//  `EditorLayout.solve` — topo, preview, tira, transporte, timeline e a folha
//  contextual, nessa ordem.
//
//  A REGRA DA CASCA: abrir painel, adicionar camada ou trocar de aba NUNCA move
//  o preview. O painel tira espaço da TIMELINE, e a timeline tem piso. É o que
//  mantém o CAMetalLayer com o mesmo tamanho durante o uso (redimensionar o
//  drawable a cada toque faria o motor recriar o swapchain sem motivo).
// =============================================================================
import SwiftUI
import UniformTypeIdentifiers

struct EditorView: View {
    @EnvironmentObject private var model: AureaModel
    @State private var showAddSheet = false

    var body: some View {
        GeometryReader { geometry in
            let metrics = EditorLayout.solve(total: geometry.size.height,
                                             content: model.sheetContent,
                                             fullscreen: model.fullscreen)
            VStack(spacing: 0) {
                if !model.fullscreen {
                    TopBarView()
                        .frame(height: metrics.topBar)
                }

                PreviewStage(height: metrics.preview)

                if !model.fullscreen {
                    Rectangle().fill(AureaColors.stage).frame(height: metrics.strip)
                }

                TransportView()
                    .frame(height: metrics.transport)

                if metrics.timeline > 0 {
                    TimelineView()
                        .frame(height: metrics.timeline)
                }

                if metrics.sheet > 0 {
                    ContextSheet(metrics: metrics)
                        .frame(height: metrics.sheet)
                }
            }
            .background(AureaColors.background.ignoresSafeArea())
        }
        .sheet(isPresented: $showAddSheet) { AddLayerSheet() }
        .sheet(isPresented: $model.showExport) { ExportView() }
        .overlay(alignment: .topTrailing) {
            if model.fullscreen {
                // Em tela cheia a única coisa que fica é o "voltar ao editor",
                // como no Android (o HUD flutuante da A.01).
                Button { model.fullscreen = false } label: {
                    Label(AureaText.t("editor_voltar_editor"), systemImage: "arrow.down.right.and.arrow.up.left")
                        .font(AureaType.label)
                        .foregroundStyle(AureaColors.text)
                        .padding(.horizontal, 12)
                        .padding(.vertical, 8)
                        .background(AureaColors.surface.opacity(0.86), in: Capsule())
                }
                .buttonStyle(.plain)
                .padding(12)
            }
        }
    }
}

// =============================================================================
// O palco: o preview do motor e os gestos
// =============================================================================
private struct PreviewStage: View {
    @EnvironmentObject private var model: AureaModel

    let height: CGFloat

    private var compositionSize: CGSize {
        CGSize(width: CGFloat(model.compositionWidth), height: CGFloat(model.compositionHeight))
    }

    /// A caixa do quadro da composição dentro do palco (o `compositionRect` da
    /// casca). É o retângulo que o toque usa para converter px de tela em px da
    /// composição — a MESMA regra do palco do Android.
    private func frameRect(in size: CGSize) -> CGRect {
        let inset: CGFloat = min(8, min(size.width, size.height) / 4)
        let available = CGSize(width: max(1, size.width - inset * 2), height: max(1, size.height - inset * 2))
        let ratio = compositionSize.width / max(1, compositionSize.height)
        var width = available.width
        var height2 = width / ratio
        if height2 > available.height {
            height2 = available.height
            width = height2 * ratio
        }
        return CGRect(x: (size.width - width) / 2, y: (size.height - height2) / 2, width: width, height: height2)
    }

    var body: some View {
        GeometryReader { geometry in
            ZStack {
                AureaColors.stage
                PreviewMetalView(compositionSize: compositionSize, interactive: !model.fullscreen)
                    .clipShape(RoundedRectangle(cornerRadius: model.fullscreen ? 0 : 6))
                    .overlay(alignment: .bottomLeading) {
                        // O chip de resolução da prévia (AUTO / 1/2 / 1/4), o
                        // mesmo da casca. Ele MOSTRA o que o motor decidiu.
                        Text(previewLabel)
                            .font(AureaType.tiny)
                            .foregroundStyle(AureaColors.text)
                            .padding(.horizontal, 8)
                            .padding(.vertical, 4)
                            .background(AureaColors.surface.opacity(0.8), in: Capsule())
                            .padding(8)
                    }
            }
            .frame(width: geometry.size.width, height: geometry.size.height)
        }
        .frame(height: height)
    }

    private var previewLabel: String {
        if model.status.previewAuto != 0 { return "AUTO" }
        let num = max(1, model.status.previewNumerator)
        let den = max(1, model.status.previewDenominator)
        return num >= den ? "FULL" : "1/\(den / max(1, num))"
    }
}

// =============================================================================
// A folha contextual: a doca de painéis ou o painel aberto
// =============================================================================
private struct ContextSheet: View {
    @EnvironmentObject private var model: AureaModel
    let metrics: EditorMetrics

    var body: some View {
        VStack(spacing: 0) {
            // Puxador (o `ContextSheet.handleHeight` da casca).
            Capsule()
                .fill(AureaColors.muted.opacity(0.4))
                .frame(width: 44, height: 4)
                .padding(.vertical, 4)
                .onTapGesture { model.panel = .none }

            switch model.panel {
            case .none, .dock:
                DockView()
            case .transform:
                TransformView()
            case .effects:
                EffectsView()
            case .layer3D:
                Panel3DView()
            case .exportPanel:
                ExportView()
            }
        }
        .background(AureaColors.surface.opacity(0.98))
        .overlay(alignment: .top) {
            Rectangle().fill(AureaColors.hairline).frame(height: AureaDims.hairline)
        }
    }
}

// =============================================================================
// A doca: os ladrilhos que abrem cada painel (`shell_dock_*`)
// =============================================================================
private struct DockView: View {
    @EnvironmentObject private var model: AureaModel

    private let tiles: [(String, String, AureaModel.PanelKind, String)] = [
        ("sh_dock_transform", "arrow.up.left.and.arrow.down.right", .transform, "Transformar"),
        ("sh_dock_effects", "wand.and.stars", .effects, "Efeitos"),
        ("sh_dock_environment", "cube", .layer3D, "3D"),
    ]

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            if model.selectedLayer == nil {
                Text(AureaText.t("editor_nenhum"))
                    .font(AureaType.body)
                    .foregroundStyle(AureaColors.subtle)
                    .padding(.horizontal, AureaDims.pad)
            }
            HStack(spacing: 10) {
                ForEach(tiles, id: \.0) { tile in
                    Button {
                        model.panel = tile.2
                        if tile.2 == .effects { model.refreshSelectedLayer() }
                    } label: {
                        VStack(spacing: 6) {
                            Image(systemName: tile.1)
                                .font(.system(size: 20, weight: .regular))
                                .foregroundStyle(AureaColors.text)
                            Text(AureaText.t(tile.0))
                                .font(AureaType.tiny)
                                .foregroundStyle(AureaColors.muted)
                                .lineLimit(1)
                        }
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 12)
                        .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 9))
                    }
                    .buttonStyle(.plain)
                }
            }
            .padding(.horizontal, AureaDims.pad)

            Button { model.showExport = true } label: {
                Label(AureaText.t("editor_exportar"), systemImage: "square.and.arrow.up")
                    .font(AureaType.label)
                    .foregroundStyle(AureaColors.accent)
            }
            .buttonStyle(.plain)
            .padding(.horizontal, AureaDims.pad)

            Spacer(minLength: 0)
        }
        .padding(.top, 4)
    }
}

// =============================================================================
// Adicionar camada (as abas do "＋" da casca)
// =============================================================================
private struct AddLayerSheet: View {
    @EnvironmentObject private var model: AureaModel
    @Environment(\.dismiss) private var dismiss
    @State private var tab: Int = 0
    @State private var text3D = "Aurea"
    @State private var depth: Float = 20
    @State private var importing: AureaModel.ImportKind?

    private let shapeNames = ["editor_retangulo", "editor_elipse", "editor_poligono", "editor_estrela"]

    var body: some View {
        NavigationStack {
            VStack(spacing: 14) {
                Picker("", selection: $tab) {
                    Text(AureaText.t("editor_video")).tag(0)
                    Text(AureaText.t("editor_foto")).tag(1)
                    Text(AureaText.t("editor_cor")).tag(2)
                }
                .pickerStyle(.segmented)
                .padding(.horizontal, AureaDims.pad)

                ScrollView {
                    VStack(spacing: 10) {
                        if tab == 0 {
                            action(AureaText.t("editor_video"), "film") {
                                dismiss(); importing = .video
                            }
                            action(AureaText.t("editor_musica_ou_som"), "waveform") {
                                dismiss(); importing = .audio
                            }
                        } else if tab == 1 {
                            action(AureaText.t("editor_foto"), "photo") {
                                dismiss(); importing = .image
                            }
                            action(AureaText.t("editor_formato") + " 3D", "cube") {
                                dismiss(); importing = .model
                            }
                            action(AureaText.t("panel_imagem_ambiente"), "sun.max") {
                                dismiss(); importing = .hdri
                            }
                        } else {
                            HStack(spacing: 8) {
                                ForEach(Array(shapeNames.enumerated()), id: \.offset) { index, key in
                                    Button {
                                        model.addShape(UInt32(index))
                                        dismiss()
                                    } label: {
                                        Text(AureaText.t(key))
                                            .font(AureaType.tiny)
                                            .foregroundStyle(AureaColors.text)
                                            .frame(maxWidth: .infinity)
                                            .padding(.vertical, 12)
                                            .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                                    }
                                    .buttonStyle(.plain)
                                }
                            }
                            .padding(.horizontal, AureaDims.pad)

                            action(AureaText.t("pn_text3d_title"), "textformat") {
                                model.addText()
                                dismiss()
                            }
                            action(AureaText.t("editor_nenhum"), "cube.transparent") {
                                model.addNull(threeD: false)
                                dismiss()
                            }
                            action(AureaText.t("sh_dock_particles"), "sparkles") {
                                model.addParticles(0)
                                dismiss()
                            }
                        }

                        // Texto 3D: a receita (texto + profundidade) vai para o
                        // `add_text3d` do motor, que gera a malha.
                        VStack(alignment: .leading, spacing: 8) {
                            Text(AureaText.t("pn_text3d_title"))
                                .font(AureaType.section)
                                .foregroundStyle(AureaColors.text)
                            TextField(AureaText.t("pn_text3d_placeholder"), text: $text3D)
                                .padding(9)
                                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                                .foregroundStyle(AureaColors.text)
                            AureaPropertyRow(title: AureaText.t("pn_depth")) {
                                Slider(value: $depth, in: 1...200)
                                    .tint(AureaColors.accent)
                                Text(String(format: "%.0f", depth))
                                    .font(AureaType.value)
                                    .foregroundStyle(AureaColors.muted)
                            }
                            Button {
                                model.addText3D(content: text3D, depth: depth)
                                dismiss()
                            } label: {
                                Text(AureaText.t("pn_text3d_title"))
                                    .font(AureaType.label)
                                    .foregroundStyle(AureaColors.onAccent)
                                    .frame(maxWidth: .infinity)
                                    .padding(.vertical, 10)
                                    .background(AureaColors.brand, in: RoundedRectangle(cornerRadius: 9))
                            }
                            .buttonStyle(.plain)
                        }
                        .padding(.horizontal, AureaDims.pad)
                        .padding(.top, 6)
                    }
                }
            }
            .background(AureaColors.background)
            .navigationTitle(AureaText.t("editor_adicionar_camada"))
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button(AureaText.t("common_cancel")) { dismiss() }
                }
            }
            .fileImporter(isPresented: Binding(get: { importing != nil }, set: { if !$0 { importing = nil } }),
                          allowedContentTypes: allowed) { result in
                defer { importing = nil }
                guard case .success(let url) = result, let kind = importing else { return }
                model.importMedia(url: url, kind: kind)
            }
        }
        .preferredColorScheme(.dark)
    }

    private var allowed: [UTType] {
        switch importing {
        case .video: return [.movie]
        case .audio: return [.audio]
        case .image: return [.image]
        case .model: return [UTType(filenameExtension: "glb") ?? .data]
        case .hdri, .none: return [.data]
        }
    }

    private func action(_ title: String, _ icon: String, _ run: @escaping () -> Void) -> some View {
        Button(action: run) {
            Label(title, systemImage: icon)
                .font(AureaType.body)
                .foregroundStyle(AureaColors.text)
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(.vertical, 12)
                .padding(.horizontal, 12)
                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 9))
        }
        .buttonStyle(.plain)
        .padding(.horizontal, AureaDims.pad)
    }
}
