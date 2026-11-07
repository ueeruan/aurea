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
import PhotosUI
import Photos
import CoreText
import Combine
import UIKit

struct EditorView: View {
    @EnvironmentObject private var model: AureaModel
    @StateObject private var shell = ShellPresentation()
    // Omega conteúdo (área 06): o navegador de fontes das Opções de texto e da UI Edição.
    @ObservedObject private var fontBrowser = FontBrowserRequest.shared
    // 0.3.3 recursos: a divisa palco/timeline volta. A altura do palco escolhida
    // arrastando o transporte (0 = automática: zona de 280), guardada no aparelho
    // com a mesma chave da 0.3.2.
    @AppStorage("editor.previewHeight") private var previewPreference: Double = 0
    @State private var dividerStart: CGFloat?
    var body: some View {
        GeometryReader { geometry in
            let wide = !model.fullscreen && model.panel != .curve && EditorLayout.isWide(geometry.size.width, geometry.size.height)
            let sideWidth = EditorLayout.wideSheetWidth(geometry.size.width)
            let aspect: CGFloat = model.compositionHeight > 0 ? CGFloat(model.compositionWidth) / CGFloat(model.compositionHeight) : 0
            // Fora da tela cheia o palco tem 8 pt de margem de cada lado.
            let sideMargin: CGFloat = model.fullscreen ? 0 : EditorLayout.previewSideMargin
            // Omega fundação (F6): prévia fixa e zona inferior de 280 (0.3.3: a divisa voltou, `preferred`).
            // Omega chrome (área 02, P0-4): com a seleção múltipla a timeline fica com a zona
            // inteira e a barra inferior flutua por cima dela (sem o painel de lote).
            let metrics = EditorLayout.solve(total: geometry.size.height, content: model.sheetContent == .batch ? .none : model.sheetContent, fullscreen: model.fullscreen,
                                             width: max(0, geometry.size.width - sideMargin * 2), aspect: aspect,
                                             preferred: CGFloat(previewPreference),  // 0.3.3 recursos: a divisa
                                             dockRows: 2,  // Omega painel: (área 04) a zona da camada tem a altura do painel (200)
                                             fontScale: UIFont.preferredFont(forTextStyle: .body).pointSize / 17)
            Group {
                if model.sceneEditor {
                    SceneLayoutWorkspace(height: geometry.size.height, width: geometry.size.width)
                } else if model.uiMode == AureaUiMode.edit {
                    // A interface de Edição (0.2.5): trilha principal e Dock em níveis.
                    EditModeView(height: geometry.size.height)
                } else if wide {
                    VStack(spacing: 0) {
                        TopBarView().frame(height: EditorLayout.topBar)
                        HStack(spacing: 0) {
                            VStack(spacing: 0) {
                                PreviewStage(height: max(96, geometry.size.height - EditorLayout.topBar - EditorLayout.transport - EditorLayout.strip - EditorLayout.wideTimeline(geometry.size.height)
                                                         - (model.sheetContent == .addBar || model.sheetContent == .adding ? StageDim.addBar : 0)))
                                    .padding(.horizontal, sideMargin)
                                if EditorLayout.strip > 0 { Rectangle().fill(AureaColors.editorCanvas).frame(height: EditorLayout.strip) }
                                TransportView().frame(height: EditorLayout.transport)
                                TimelineView(timecodeStyle: model.selection.count == 1 ? .box : .underline).frame(height: EditorLayout.wideTimeline(geometry.size.height))
                                // A barra de adicionar na base da coluna do palco.
                                // Omega adicionar (área 08): também com o adicionar aberto.
                                if model.sheetContent == .addBar || model.sheetContent == .adding { ShellAddBar().frame(height: StageDim.addBar) }
                            }.frame(maxWidth: .infinity)
                            // 0.3.3 barra: a grade do "+" por cima do palco e da timeline, encostada embaixo.
                            .overlay(alignment: .bottom) { if model.sheetContent == .addBar || model.sheetContent == .adding { AddBarMoreOverlay() } }
                            // Omega conteúdo (área 06, P0-1): no modo de texto, o painel grande com a faixa de entrada.
                            if model.textModeActive { TextEditPanel().frame(width: sideWidth, height: max(1, geometry.size.height - EditorLayout.topBar)) }
                            else { ContextSheet(metrics: metrics).frame(width: sideWidth, height: max(1, geometry.size.height - EditorLayout.topBar)) }
                        }
                    }
                } else {
                    VStack(spacing: 0) {
                        if !model.fullscreen { TopBarView(height: metrics.topBar) }
                        PreviewStage(height: max(0, metrics.preview - (model.fullscreen ? StageDim.fullscreenTimeBar : 0)))
                            .padding(.horizontal, sideMargin)
                            // Omega adicionar (área 08, P0-1): adicionar aberto, tocar no palco fecha.
                            .overlay { if model.sheetContent == .adding { Color.clear.contentShape(Rectangle()).onTapGesture { model.showAddLayer = false } } }
                        if model.fullscreen {
                            FullscreenTimeBar()
                            TransportView().frame(height: metrics.transport)
                        } else if model.textModeActive {
                            // Omega conteúdo (área 06, P0-1): o MODO DE TEXTO — o painel grande (faixa de
                            // entrada de 60) ocupa o transporte e a zona inferior; a prévia não muda.
                            TextEditPanel().frame(height: metrics.transport + metrics.zone)
                        } else {
                            // 0.3.3 recursos: a divisa palco/timeline volta, como na 0.3.2 —
                            // arrastar na vertical no transporte (fora dos botões) troca palco
                            // por timeline (sobe ou desce a timeline); toque duplo volta à
                            // altura automática. A zona de baixo continua a da Omega: o painel
                            // de 200 fica por cima da timeline e, aberto, recebe a zona inteira.
                            TransportView().frame(height: metrics.transport)
                                .contentShape(Rectangle())
                                // `.contain` antes do identificador: sem ele o id da divisa
                                // sobrescreveria o dos botões do transporte.
                                .accessibilityElement(children: .contain)
                                .accessibilityIdentifier("editor.previewDivider")
                                .simultaneousGesture(DragGesture(minimumDistance: 8, coordinateSpace: .global)
                                    .onChanged { value in
                                        if dividerStart == nil {
                                            guard abs(value.translation.height) > abs(value.translation.width) else { return }
                                            dividerStart = metrics.preview
                                        }
                                        guard let start = dividerStart else { return }
                                        let top = EditorLayout.maxPreview(geometry.size.height)
                                        previewPreference = Double(min(max(start + value.translation.height, EditorLayout.previewMin), top))
                                    }
                                    .onEnded { _ in dividerStart = nil })
                                .simultaneousGesture(TapGesture(count: 2).onEnded { previewPreference = 0 })
                            // Omega fundação (F6): a zona inferior fixa (280). Sem camada, a
                            // timeline (210) e a barra de adicionar (70) empilhadas; com camada,
                            // a timeline ocupa a zona e o painel de 200 fica POR CIMA dela
                            // (sobram 80: régua + uma linha). A prévia não muda de altura.
                            ZStack(alignment: .bottom) {
                                VStack(spacing: 0) {
                                    // 0.3.6: o que o painel por cima (doca/painel) ou o adicionar cobre embaixo.
                                    if metrics.timeline > 0 {
                                        TimelineView(compactDock: true,
                                                     obscuredBottom: metrics.overlay ? metrics.obscuredTimeline
                                                        : model.sheetContent == .adding ? EditorLayout.addContent(metrics) : 0,
                                                     timecodeStyle: model.selection.count == 1 ? .box : .underline).frame(height: metrics.timeline)
                                    }
                                    if !metrics.overlay && metrics.sheet > 0 {
                                        if model.sheetContent == .addBar || model.sheetContent == .adding { ShellAddBar().frame(height: metrics.sheet) }
                                        else { ContextSheet(metrics: metrics).frame(height: metrics.sheet) }
                                    }
                                }
                                .frame(height: metrics.zone, alignment: .top)
                                if model.sheetContent == .adding {
                                    // 0.3.6: com o adicionar aberto, o pedaço da timeline (régua) à vista só
                                    // FECHA o adicionar: o toque é consumido inteiro e não puxa a régua nem
                                    // leva o cabeçote (par do `editor.adding.dismiss` do Android).
                                    // 0.4.1 (mockup "interface 03"): com a gaveta aberta, o resto escurece.
                                    Color.black.opacity(0.45).contentShape(Rectangle())
                                        .frame(height: metrics.timeline)
                                        .gesture(DragGesture(minimumDistance: 0).onEnded { _ in model.showAddLayer = false })
                                        .accessibilityHidden(true)
                                        .frame(maxHeight: .infinity, alignment: .top)
                                }
                                if model.sheetContent == .adding {
                                    // Omega adicionar (área 08, P0-1): o conteúdo do botão aberto (193)
                                    // por cima da timeline, encostado na barra de adicionar, que fica.
                                    AddPanelView().frame(height: EditorLayout.addContent(metrics))
                                        .contentShape(Rectangle()).padding(.bottom, metrics.sheet)
                                }
                                if metrics.overlay && metrics.sheet > 0 {
                                    // 0.3.4 painéis: o painel de propriedade tem a zona inteira menos a faixa
                                    // de foco (régua + linha em foco + folga; `EditorLayout.timelineFocusHeight`).
                                    // O painel por cima segura o toque: nada passa para a timeline embaixo.
                                    ContextSheet(metrics: metrics).frame(height: metrics.sheet).contentShape(Rectangle())
                                }
                                // Omega chrome (área 02, P0-4): a barra inferior flutuante da seleção múltipla.
                                if model.sheetContent == .batch && !RealCameraState.shared.painting(model) { MultiSelectBottomBar().keepLtr() }
                                // 0.3.3 barra: a grade do "+" sobe por cima da timeline e da barra.
                                if model.sheetContent == .addBar || model.sheetContent == .adding { AddBarMoreOverlay() }
                            }
                            .frame(height: metrics.zone)
                        }
                    }
                    // 0.4.1 (mockup "interface 03"): com a grade do "+" ou uma gaveta aberta, o véu
                    // (preto a 45 %) cobre também barra, palco e transporte; tocar fecha (par do Android).
                    .overlay(alignment: .top) {
                        if !model.fullscreen && !model.textModeActive {
                            DockVeil(height: metrics.topBar + metrics.preview + metrics.transport)
                        }
                    }
                }
            }
            // Omega conteúdo (área 06, P0-3): os popups da barra de estilo do modo de texto.
            .overlay(alignment: .top) {
                if !model.sceneEditor && model.uiMode != AureaUiMode.edit && !model.fullscreen {
                    TextStylePopupLayer(top: wide ? EditorLayout.topBar : metrics.topBar)
                }
            }
            .background(AureaColors.editorCanvas.ignoresSafeArea())
        }
        // Omega conteúdo (área 06, P0-1): no modo de texto o teclado não encolhe o editor
        // (a prévia fica); a faixa de entrada sobe sozinha (TextEditPanel).
        .ignoresSafeArea(model.textModeActive ? .keyboard : [], edges: .bottom)
        .fullScreenCover(isPresented: $model.showExport) { ExportView() }
        // 0.4.6: o assistente de IA em tela cheia (AssistantView.swift).
        .background(Color.clear.fullScreenCover(isPresented: $model.assistantOpen) {
            AssistantView(close: { model.assistantOpen = false }).environmentObject(model)
        })
        .fullScreenCover(item: $model.effectSearch) { request in
            // A tela "Adicionar efeito" (ou só a busca): EffectsBrowser.swift.
            EffectAddSheet(request: request, close: { model.effectSearch = nil })
                .environmentObject(model)
        }
        // Omega conteúdo (área 06, P2-15): o navegador de fontes pedido pelas Opções de texto e pela UI Edição.
        .background(Color.clear.fullScreenCover(isPresented: $fontBrowser.open) {
            FontBrowserView(onPick: { f in
                model.applyFontAnywhere(f)
                FontPrefs.shared.recordUse(f)
                fontBrowser.open = false
            }, onDismiss: { fontBrowser.open = false }).environmentObject(model)
        })
        .sheet(item: $model.markerEditingFrame) { marker in
            MarkerEditorSheet(frame: marker.frame, color: marker.color, label: marker.label, isNew: marker.isNew)
                .environmentObject(model)
                .presentationDetents([.medium, .large])
        }
        .overlay(alignment: .topTrailing) {
            if model.fullscreen {
                Button { model.fullscreen = false } label: {
                    CupertinoGlyph.text(CupertinoGlyph.FullscreenExit, size: 24)
                        .frame(width: 40, height: 40).background(StageInk.floatingDark, in: Circle())
                }.buttonStyle(.plain).accessibilityLabel(AureaText.t("editor_voltar_editor")).padding(10)
            }
        }
        // Omega adicionar (área 08, P0-1): o adicionar é embutido acima da barra; só a
        // interface de Edição (sem a barra) segue com a janela.
        .overlay { if model.showAddLayer && !model.sceneEditor && model.uiMode == AureaUiMode.edit { ShellAddCategoryDialog() } }
        // Omega adicionar (área 08, P1-1/P1-3/P0-6): os "Ver tudo" em tela cheia (um por fundo:
        // várias apresentações no mesmo nó o SwiftUI ignora).
        .background(Color.clear.fullScreenCover(isPresented: $model.omegaMediaBrowser) { MediaBrowserView().environmentObject(model) })
        .background(Color.clear.fullScreenCover(isPresented: $model.omegaAudioBrowser) { AudioBrowserView().environmentObject(model) })
        .background(Color.clear.fullScreenCover(isPresented: $model.omegaPresetsBrowser) { PresetsBrowserView().environmentObject(model) })
        // Gravar voz: o painel por cima da timeline, com o palco à vista (VoiceRecorder.swift).
        .overlay { if shell.voiceRecorder { VoiceRecorderOverlay() } }
        // 0.3.4 plugins: o painel de plugin aberto (moldura comum), alertas e consentimento.
        .overlay(alignment: .bottom) { PluginEditorPanelHost() }
        .overlay { ShellOverlayHost() }
        .environmentObject(shell)
        .modifier(ModelTexturesPrompt(shell: shell))
        .modifier(ModelOptimizePrompt())
        .modifier(AddLayerPickers(shell: shell))
    }
}

/// Os seletores do diálogo de adicionar, apresentados pela RAIZ do editor.
/// Antes moravam no próprio diálogo, que some ao escolher: o seletor de fotos
/// fechava junto com o dono e deixava a apresentação seguinte (o seletor de
/// arquivos do modelo 3D) presa e invisível — o modelo não entrava e nenhum
/// toque respondia. Aqui o dono fica na tela o tempo todo.
private struct AddLayerPickers: ViewModifier {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject var shell: ShellPresentation
    @State private var choosingPhotos = false
    @State private var photoKind = ShellAddPicker.gallery
    @State private var pickerProject = UUID()
    func body(content: Content) -> some View {
        content
            .onChange(of: shell.addPicker) { request in
                guard let request else { return }
                pickerProject = model.projectGeneration
                shell.addPicker = nil
                if request.isFile { presentFilePicker(request) }
                else { photoKind = request; choosingPhotos = true }
            }
            .sheet(isPresented: $choosingPhotos) {
                ShellMediaPicker(selectionLimit: photoKind == .audioFromVideo ? 1 : 0, filter: photoKind == .photo ? .images : ((photoKind == .video || photoKind == .audioFromVideo) ? .videos : .any(of: [.images, .videos]))) { items in
                    choosingPhotos = false
                    guard pickerProject == model.projectGeneration else { return }
                    if photoKind == .audioFromVideo, let first = items.first {
                        model.importMedia(url: first.0, kind: .audio)
                    } else { model.importMediaBatch(items) }
                }
            }
            // 0.3.0: de onde vem a sequência de imagens (rememberImageSequencePicker do Android).
            .confirmationDialog(AureaText.t("seq_title"), isPresented: $shell.sequenceChooser, titleVisibility: .visible) {
                Button(AureaText.t("seq_pick_folder")) { shell.addPicker = .sequenceFolder }
                    .accessibilityIdentifier("seq.pick.folder")
                Button(AureaText.t("seq_pick_files")) { shell.addPicker = .sequenceFiles }
                    .accessibilityIdentifier("seq.pick.files")
                Button(AureaText.t("common_cancel"), role: .cancel) { model.pendingOverlayImport = false }
            } message: {
                Text(AureaText.t("seq_hint"))
            }
            .sheet(item: Binding(get: { model.sequenceDraft }, set: { if $0 == nil { model.cancelImageSequence() } })) { draft in
                ImageSequenceSheet(draft: draft).environmentObject(model)
                    .presentationDetents([.medium, .large])
            }
            // 0.3.0: o relatório da importação do After Effects.
            .sheet(item: $model.aeImportResult) { result in
                AeImportSheet(result: result).environmentObject(model).presentationDetents([.medium, .large])
            }
    }
    /// Arquivo (modelo 3D, texturas/.mtl, SVG, PSD, áudio): seletor do UIKit
    /// pelo controlador do topo (DocumentImportPicker). Antes o modelo vinha
    /// num seletor embutido num `fullScreenCover` e o resto no `.fileImporter`
    /// da raiz — no iPhone com iOS 26/27 o embutido fechava sozinho ao escolher
    /// e "nada importava"; o `.fileImporter` da raiz calava os de baixo.
    private func presentFilePicker(_ kind: ShellAddPicker) {
        let project = pickerProject
        let model = self.model
        let multiple = kind == .model || kind == .modelTextures || kind == .sequenceFiles || kind == .mediaFiles
        DocumentImportPicker.present(types: Self.fileTypes(kind), multiple: multiple, asCopy: kind != .sequenceFolder) { urls in
            guard project == model.projectGeneration, let urls, let url = urls.first else { return }
            switch kind {
            // Omega bugs-034 mídia: fotos, vídeos e áudio do Arquivos; o áudio vira camada de áudio no lote.
            case .mediaFiles: model.importMediaBatch(urls.map { ($0, OmegaPickedMedia.kind($0) == .video) })
            // Seleção múltipla/pasta: o FBX/OBJ/glTF vem com as texturas, o .bin e o .mtl.
            case .model: model.importModelFiles(urls: urls)
            case .modelTextures: model.importModelTextures(urls: urls)
            case .svg: model.importSvg(url: url)
            case .psd: model.importPsd(url: url)
            case .sequenceFolder, .sequenceFiles: model.prepareImageSequence(urls: urls)
            case .aeImport: model.importAeFile(url: url)
            default: model.importMedia(url: url, kind: .audio)
            }
        }
    }
    private static func fileTypes(_ kind: ShellAddPicker) -> [UTType] {
        switch kind {
        case .svg: return [UTType(filenameExtension: "svg") ?? .data, .data]
        case .psd: return [UTType(filenameExtension: "psd") ?? .data, .data]
        case .sequenceFolder: return [.folder]
        case .aeImport: return [UTType(filenameExtension: "ffx") ?? .data, UTType(filenameExtension: "jsx") ?? .data, .plainText, .data]
        // EXR e TGA não têm tipo registrado em muitos provedores: o filtro é o motor.
        case .sequenceFiles: return [.image, .zip, UTType(filenameExtension: "exr") ?? .data, UTType(filenameExtension: "tga") ?? .data, .data]
        case .model: return DocumentImportPicker.modelTypes
        // .mtl (texto), .bin e texturas: alguns provedores não marcam public.data.
        case .modelTextures: return DocumentImportPicker.modelTypes + [.image, .text]
        // Omega bugs-034 mídia.
        case .mediaFiles: return OmegaPickedMedia.types
        default: return [.audio, .movie, .data]
        }
    }
}

/// "Importar texturas": o modelo FBX/OBJ procura arquivos que não vieram junto.
/// O seletor abre depois que o alerta fecha (o alvo fica guardado no modelo).
private struct ModelTexturesPrompt: ViewModifier {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject var shell: ShellPresentation
    func body(content: Content) -> some View {
        content
            .alert(AureaText.t("model_textures_title"), isPresented: Binding(
                get: { model.missingModelTextures != nil },
                set: { if !$0 { model.missingModelTextures = nil } })) {
                Button(AureaText.t("model_textures_choose")) {
                    model.missingModelTextures = nil
                    // O seletor sobe pela raiz (`AddLayerPickers`) depois que o alerta fecha.
                    let shell = shell
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.4) { shell.addPicker = .modelTextures }
                }
                Button(AureaText.t("common_cancel"), role: .cancel) { model.missingModelTextures = nil }
            } message: {
                let names = model.missingModelTextures?.names ?? []
                Text(AureaText.t("model_textures_message", names.prefix(8).joined(separator: "\n") + (names.count > 8 ? "\n…" : "")))
            }
    }
}

/// "Modelo pesado": o motor mediu o arquivo e o Original não cabe neste
/// aparelho (ou é denso demais para o preview). Uma escolha por qualidade, a
/// recomendada marcada; o Original só aparece quando cabe. A conta é do motor.
private struct ModelOptimizePrompt: ViewModifier {
    @EnvironmentObject private var model: AureaModel
    private func label(_ q: Int, _ plan: AureaModel.ModelPlanInfo) -> String {
        let name: String
        switch q {
        case 0: name = AureaText.t("model3d_quality_original")
        case 1: name = AureaText.t("model3d_quality_balanced")
        default: name = AureaText.t("model3d_quality_light")
        }
        return q == plan.recommended ? "\(name) · \(AureaText.t("model3d_recommended"))" : name
    }
    private func hint(_ q: Int, _ plan: AureaModel.ModelPlanInfo) -> String {
        let kept = AureaModel.modelCount(plan.keptTriangles(q))
        switch q {
        case 0: return AureaText.t("model3d_quality_original_hint")
        case 1: return AureaText.t("model3d_quality_balanced_hint", kept, plan.textureCap(q))
        default: return AureaText.t("model3d_quality_light_hint", kept, plan.textureCap(q))
        }
    }
    func body(content: Content) -> some View {
        content
            .alert(AureaText.t("model3d_heavy_title"), isPresented: Binding(
                get: { model.modelOptimize != nil },
                // Fechar só limpa o pedido: a pasta copiada sai no Cancelar (o botão
                // de importar pode rodar depois do fechamento e ainda precisa dela).
                set: { if !$0 { model.modelOptimize = nil } })) {
                if let req = model.modelOptimize {
                    ForEach(req.plan.offered, id: \.self) { q in
                        Button(label(q, req.plan)) { model.confirmModelOptimize(req, quality: q) }
                    }
                    Button(AureaText.t("common_cancel"), role: .cancel) { model.dismissModelOptimize(req) }
                }
            } message: {
                if let req = model.modelOptimize {
                    let tris = AureaModel.modelCount(req.plan.triangles)
                    let lines = req.plan.offered.map { "\(label($0, req.plan)): \(hint($0, req.plan))" }
                    Text(AureaText.t("model3d_heavy_body", req.plan.exact ? tris : "~" + tris) + "\n\n" + lines.joined(separator: "\n")
                         + "\n\n" + AureaText.t("model3d_risk_warning"))
                }
            }
    }
}

// =============================================================================
// O palco: o preview do motor e os gestos
// =============================================================================
/// Interno (não privado): a interface de Edição usa o mesmo palco.
@MainActor struct PreviewStage: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    let height: CGFloat
    private var compositionSize: CGSize { CGSize(width: CGFloat(model.compositionWidth), height: CGFloat(model.compositionHeight)) }
    var body: some View {
        ZStack {
            AureaColors.previewBackdrop
            PreviewMetalView(compositionSize: compositionSize, interactive: !model.fullscreen && !model.rawPlayback)
                .overlay { if !model.fullscreen && !model.rawPlayback { StageOverlay().allowsHitTesting(false) } }
                .overlay { if !model.fullscreen && !model.rawPlayback { StageInteractionOverlay().allowsHitTesting(false) } }
                // Omega efeitos (área 07, P1-2): parâmetro de efeito ATIVO — arrasto relativo em
                // qualquer lugar do preview e o guia "deslize para ajustar" (SaberStage.swift).
                // Fica abaixo das alças das ferramentas (rig, malha, fantoche, sabre, ponto, roto).
                .overlay { if !model.fullscreen && !model.rawPlayback { EffectParamDragOverlay() } }
                .overlay { if !model.fullscreen && !model.rawPlayback { RigStageOverlay() } }
                .overlay { if !model.fullscreen && !model.rawPlayback { MeshWarpStageOverlay() } }
                .overlay { if !model.fullscreen && !model.rawPlayback { PuppetStageOverlay() } }
                .overlay { if !model.fullscreen && !model.rawPlayback { SaberStageOverlay() } }
                // Ponto de efeito ativo (parâmetro tocado no cartão): a alça dele (SaberStage.swift).
                .overlay { if !model.fullscreen && !model.rawPlayback { EffectPointStageOverlay() } }
                .overlay { if !model.fullscreen && !model.rawPlayback { RotoPaintStageOverlay() } }
                // Omega conteúdo (área 06): desenho, Editar pontos e a alça de largura da caixa de texto.
                .overlay { if !model.fullscreen && !model.rawPlayback { OmegaContentStageOverlay() } }
                .overlay { if !model.fullscreen && !model.rawPlayback { RealCameraStageOverlay() } }
                .overlay { if !model.fullscreen && !model.rawPlayback { Shape3DPartOverlay() } }
            if model.panel == .tracking && model.cameraTrackerVisible && !model.cameraFeatures.isEmpty && model.pointPick == nil { CameraTrackingOverlay() }
            if let layer = model.selectedLayer, model.selection.count == 1, layer.locked {
                ShellStageBanner(label: AureaText.t("editor_camada_bloqueada"), button: AureaText.t("editor_desbloquear"), icon: CupertinoGlyph.LockFill) {
                    model.mutate { $0.setLayer(layer.id, locked: false) }; model.refreshModel(force: true)
                }.padding(.top, 8).padding(.horizontal, 8).frame(maxHeight: .infinity, alignment: .top)
            }
            if !model.fullscreen && !model.rawPlayback {
                RigModeBar().padding(.top, 8).padding(.horizontal, 8).frame(maxHeight: .infinity, alignment: .top)
            }
            if model.panel == .vector && (model.vectorFreehand || model.vectorEditingPoints) {
                ShellStageBanner(label: vectorHint, button: AureaText.t("editor_concluir")) {
                    model.vectorFreehand = false; model.vectorEditingPoints = false; model.maskDrawing = false; model.freehandPoints = []
                }.padding(.bottom, 10).padding(.horizontal, 8).frame(maxHeight: .infinity, alignment: .bottom)
            }
            // Na Cena 3D as ferramentas ficam na barra da esquerda (Scene3DToolbar).
            if !model.fullscreen && !model.rawPlayback && !model.sceneEditor, model.selection.count == 1, let id = model.primarySelection {
                HStack(spacing: 6) {
                    let hasGizmo = !model.engine.gizmo(id, length: ShellStageGeometry.gizmoLength).isEmpty
                    if hasGizmo {
                        Button { model.cycleGizmoTool() } label: {
                            CupertinoGlyph.text(model.gizmoTool == 1 ? CupertinoGlyph.ArrowCounterclockwise : model.gizmoTool == 2 ? CupertinoGlyph.ArrowDownRightSquare : CupertinoGlyph.ArrowUpDownSquare, size: 22, color: AureaColors.accent)
                                .frame(width: 48, height: 48)
                                .background(AureaColors.editorPanelHigh, in: RoundedRectangle(cornerRadius: 8))
                        }.accessibilityLabel(AureaText.t("gizmo_tool_label"))
                            .accessibilityValue(AureaText.t(model.gizmoTool == 1 ? "gizmo_tool_rotate" : model.gizmoTool == 2 ? "gizmo_tool_scale" : "gizmo_tool_move"))
                            .accessibilityHint(TrackballOverlay.active(model) ? AureaText.t("gizmo_trackball_hint") : "")
                            .accessibilityIdentifier("stage.gizmo.tool")
                    }
                    // Mundo/Local vale para mover; girar e escala usam os eixos da camada.
                    if hasGizmo && model.gizmoTool == 0 {
                        Button { model.gizmoLocalSpace.toggle() } label: {
                            CupertinoGlyph.text(model.gizmoLocalSpace ? CupertinoGlyph.CubeFill : CupertinoGlyph.Cube, size: 22, color: AureaColors.text)
                                .frame(width: 48, height: 48)
                                .background(AureaColors.editorPanelHigh, in: RoundedRectangle(cornerRadius: 8))
                        }.accessibilityLabel(model.gizmoLocalSpace ? "Local XYZ" : "World XYZ").accessibilityIdentifier("stage.gizmo.space")
                    }
                    if !model.sceneEditor {
                        Button {
                            model.autoKeyTransforms.toggle()
                            model.toast = AureaText.t(model.autoKeyTransforms ? "ios_autokey_on" : "ios_autokey_off")
                        } label: {
                            CupertinoGlyph.text(model.autoKeyTransforms ? CupertinoGlyph.SuitDiamondFill : CupertinoGlyph.SuitDiamond, size: 22, color: model.autoKeyTransforms ? AureaColors.accent : AureaColors.text)
                                .frame(width: 48, height: 48)
                                .background(AureaColors.editorPanelHigh, in: RoundedRectangle(cornerRadius: 8))
                        }.accessibilityLabel(model.autoKeyTransforms ? "Auto-Key: On" : "Auto-Key: Off").accessibilityIdentifier("stage.autokey")
                    }
                }.font(.aurea(size: 14)).foregroundStyle(AureaColors.text).buttonStyle(.plain)
                    .padding(8).frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottomLeading)
            }
            if model.hudVisible { ShellPerfHud().padding(.leading, 8).padding(.top, 6).frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading).allowsHitTesting(false) }
            if let label = model.previewBufferLabel, !model.exporting {
                HStack(spacing: 6) {
                    if model.previewBuffering {
                        ProgressView().tint(AureaColors.accent).scaleEffect(0.7)
                            .accessibilityHidden(true)
                    }
                    Text(label).font(.aurea(size: 12, weight: .medium)).multilineTextAlignment(.center)
                }
                .foregroundStyle(model.previewBufferLimited ? AureaColors.warning : AureaColors.text)
                .padding(.horizontal, 10).padding(.vertical, 6)
                .background(StageInk.resolutionChip, in: Capsule())
                .accessibilityElement(children: .ignore).accessibilityLabel(label)
                .accessibilityIdentifier("preview.buffer.status")
                .allowsHitTesting(false)
                .padding(.horizontal, 64).padding(.bottom, 8)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottom)
            }
            // Omega chrome (área 02, P1-2): o botão de modo zoom do canvas e o rótulo
            // "NN%" no canto sup-esq (o chip "250 %" saiu: o 100 % mora no número da
            // coluna de modos) e a coluna de modos presa à direita (P0-5).
            if !model.rawPlayback && !model.fullscreen && !model.sceneEditor {
                CanvasZoomMode().frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
                // 0.4.1 (mockup "interface 03"): grade, modos de visão e zoom flutuando no canto sup-dir.
                StageFloatPills().frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing)
                ViewModeColumn(width: ViewModeState.columnWidth(UIScreen.main.bounds.width))
            }
            // 0.4.6: o nome da vista do AE escolhida (Frente, Topo, Vista personalizada 1…).
            if !model.rawPlayback && model.viewMode != 0 && !model.sceneEditor {
                Text(AureaModel.viewModeName(model.viewMode))
                    .font(.aurea(size: 11, weight: .semibold)).foregroundStyle(AureaColors.accent)
                    .padding(.horizontal, 8).padding(.vertical, 4)
                    .background(Color(red: 0x14 / 255, green: 0x14 / 255, blue: 0x16 / 255).opacity(0xD9 / 255), in: RoundedRectangle(cornerRadius: 8))
                    .padding(.trailing, 16).padding(.bottom, 8)
                    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottomTrailing)
                    .allowsHitTesting(false)
            }
        }.frame(height: height).clipped()
        // O palco não espelha em árabe: preview, gizmos, alças, rig e os
        // botões/selos dos cantos ficam onde estão no LTR (par do KeepLtr do stage).
        .keepLtr()
        .onAppear { StageViewZoom.shared.resetIfProjectChanged(model.projectURL, engine: model.engine) }
        .onChange(of: model.projectURL) { url in StageViewZoom.shared.resetIfProjectChanged(url, engine: model.engine) }
    }
    private var previewLabel: String {
        if model.rawPlayback { return "RAW" }
        if model.status.previewAuto != 0 { return "AUTO" }
        let n = max(1, model.status.previewNumerator), d = max(1, model.status.previewDenominator)
        return n >= d ? AureaText.t("i18n_preview_full") : "1/\(d / n)"
    }
    private var vectorHint: String {
        if model.vectorFreehand { return AureaText.t("editor_mao_livre_desenhe_dedo") }
        let tool = (model.editingMask?.points.count ?? 0) < 12 ? 1 : model.vectorPointTool
        return AureaText.t(tool == 1 ? "editor_adicionar_toque_linha_ou_vazio_arraste" : tool == 2 ? "editor_remover_toque_ponto_apagar" : tool == 3 ? "editor_canto_suave_toque_ponto_alternar" : "editor_selecionar_arraste_pontos_alcas")
    }
}

private struct ShellStageBanner: View {
    let label: String
    let button: String
    var icon: Character? = nil
    let action: () -> Void
    var body: some View {
        HStack(spacing: 0) {
            if let icon { CupertinoGlyph.text(icon, size: 13, color: AureaColors.accent).padding(.trailing, 7) }
            Text(label).font(.aurea(size: 12, weight: .semibold)).foregroundStyle(AureaColors.text).lineLimit(1).layoutPriority(-1)
            Color.clear.frame(width: icon == nil ? 6 : 4, height: 1)
            Button(action: action) {
                Text(button).font(.aurea(size: 11.5, weight: .bold)).foregroundStyle(AureaColors.onAccent).padding(.horizontal, 10).padding(.vertical, 4)
                    .background(AureaColors.accent, in: Capsule()).padding(.horizontal, 4).frame(minHeight: 36)
            }.buttonStyle(AureaPressStyle(shrink: 1))
        }.frame(height: 36).padding(.leading, icon == nil ? 12 : 10).padding(.trailing, 4)
            .background(AureaColors.editorPanelHigh, in: RoundedRectangle(cornerRadius: 10))
            .overlay(RoundedRectangle(cornerRadius: 10).stroke(StageInk.accentHalf, lineWidth: 1))
    }
}

@MainActor private final class ShellDisplayRate: NSObject, ObservableObject {
    @Published var fps: Double = 0
    private var link: CADisplayLink?
    private var first: CFTimeInterval = 0
    private var samples = 0
    func start() { stop(); first = 0; samples = 0; let next = CADisplayLink(target: self, selector: #selector(tick(_:))); next.add(to: .main, forMode: .common); link = next }
    func stop() { link?.invalidate(); link = nil }
    @objc private func tick(_ link: CADisplayLink) {
        if first == 0 { first = link.timestamp; return }; samples += 1
        if link.timestamp - first >= 0.5 { fps = Double(samples) / (link.timestamp - first); first = link.timestamp; samples = 0 }
    }
}
@MainActor private struct ShellPerfHud: View {
    @EnvironmentObject private var model: AureaModel
    @StateObject private var display = ShellDisplayRate()
    @State private var stats: [String: Any] = [:]
    @State private var playbackReport = ""
    private let timer = Timer.publish(every: 0.25, on: .main, in: .common).autoconnect()
    var body: some View {
        Text(text).font(.aurea(size: 10, design: .monospaced)).lineSpacing(2).foregroundStyle(AureaColors.accent).fixedSize(horizontal: false, vertical: true)
            .padding(.horizontal, 10).padding(.vertical, 6).background(StageInk.floatingDark, in: RoundedRectangle(cornerRadius: 8))
            .overlay(RoundedRectangle(cornerRadius: 8).stroke(AureaColors.border, lineWidth: 0.5))
            .onAppear { stats = model.engine.perf(); display.start() }.onDisappear { display.stop() }
            .onReceive(timer) { _ in stats = model.engine.perf(); playbackReport = model.engine.playbackReport() }
    }
    private func number(_ key: String) -> NSNumber? { stats[key] as? NSNumber }
    private func count(_ key: String) -> String { number(key)?.stringValue ?? "—" }
    private func decimal(_ key: String) -> String { number(key).map { String(format: "%.1f", locale: Locale(identifier: "en_US_POSIX"), $0.doubleValue) } ?? "—" }
    private func mb(_ key: String) -> String { number(key).map { String($0.uint64Value / (1024 * 1024)) } ?? "—" }
    private func gpu(_ key: String) -> String { number("gpuTimers")?.boolValue == true ? decimal(key) : "—" }
    private func line(_ key: String, _ args: String...) -> String { String(format: AureaText.t(key), arguments: args.map { $0 as CVarArg }) }
    private var text: String {
        if model.rawPlayback && !playbackReport.isEmpty { return playbackReport }
        var out = [line("sh_diag_fps", decimal("previewFps"), String(format: "%.1f", display.fps))]
        if (number("pacingSamples")?.intValue ?? 0) > 0 { out.append(line("sh_diag_pacing", decimal("pacingP50Ms"), decimal("pacingP95Ms"), decimal("pacingP99Ms"), decimal("pacingStdMs"), count("pacingSamples"))) }
        out.append(line("sh_diag_cpu", decimal("cpuFrameMs"), decimal("cpuPrepareMs"), decimal("cpuRecordMs"), gpu("gpuFrameMs"), decimal("frameBudgetMs")))
        out.append(line("sh_diag_decode", decimal("decodeMs"), gpu("colorConvMs"), gpu("effectsMs")))
        out.append(line("sh_diag_blur", gpu("blurMs"), gpu("glowMs"), gpu("compositeMs")))
        out.append(line("sh_diag_output", gpu("outputMs"), decimal("presentMs"), decimal("acquireMs")))
        out.append(line("sh_diag_dropped", count("droppedFrames"), count("droppedRecent"), decimal("lastSeekMs")))
        let thermalKeys = ["sh_diag_thermal_normal", "sh_diag_thermal_warm", "sh_diag_thermal_serious", "sh_diag_thermal_critical", "sh_diag_thermal_emergency"]
        let thermal = number("thermal")?.intValue ?? -1
        out.append(line("sh_diag_scale", "\(count("renderScaleNum"))/\(count("renderScaleDen"))", number("renderAuto")?.boolValue == true ? " auto" : "", count("previewWidth"), count("previewHeight"), AureaText.t(thermalKeys.indices.contains(thermal) ? thermalKeys[thermal] : "sh_diag_thermal_unknown")))
        out.append(line("sh_diag_cache", count("decodedCacheFrames"), mb("decodedCacheBytes")))
        out.append(line("sh_diag_ram", mb("ramBytes"), count("memoryBudgetMB"), "—", "—"))
        out.append(line("sh_diag_gpu_memory", mb("gpuMemoryBytes"), mb("gpuReservedBytes"), count("gpuAllocations"), mb("transientBytes")))
        out.append(line("sh_diag_passes", count("passesExecuted"), count("passesCulled"), count("drawCalls"), count("layersRendered"), count("activeEffects")))
        if (number("draws3D")?.intValue ?? 0) > 0 || (number("triangles3D")?.intValue ?? 0) > 0 { out.append(line("sh_diag_3d", count("draws3D"), count("triangles3D"), count("culled3D"), mb("scene3dBytes"))) }
        if (number("particles")?.intValue ?? 0) > 0 { out.append(line("sh_diag_particles", count("particles"))) }
        if number("audioOutputOpen")?.boolValue == true { out.append(line("sh_diag_audio", count("audioQueuedMs"), count("audioOutputMs"), count("audioUnderruns"), count("audioMissingBlocks"))) }
        out.append(line("sh_diag_textures", count("physicalTextures"), count("aliasedTextures"), count("pipelinesTotal"), count("pipelineCompilesLive")))
        out.append(line("sh_diag_seeks", count("seeks"), count("coalesced"), count("staleFrames")))
        out.append("decoder " + (stats["decoder"] as? String ?? "—") + (number("hardwareDecoder")?.boolValue == true ? " (HW)" : "") + (number("zeroCopy")?.boolValue == true ? " · zero-copy" : ""))
        out.append(line(number("gpuTimers")?.boolValue == true ? "sh_diag_gpu_timers" : "sh_diag_gpu_no_timestamp", stats["gpuName"] as? String ?? "—"))
        return out.joined(separator: "\n")
    }
}


@MainActor private struct StageOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @Environment(\.displayScale) private var displayScale
    @EnvironmentObject private var shell: ShellPresentation
    @ObservedObject private var viewZoom = StageViewZoom.shared
    /// 0.4.1 (mockup "interface 03"): observa o "olho" do palco (`hideOverlay`).
    @ObservedObject private var viewModes = ViewModeState.shared
    var body: some View {
        Canvas { raw, size in
            var context = raw
            // Encaixe com o zoom/pan da vista (StageZoom.swift): o mesmo do passe
            // de saída do motor; alças seguem em pt fixos na tela.
            let placed = StageZoomMath.fit(size: size, composition: CGSize(width: CGFloat(model.compositionWidth), height: CGFloat(model.compositionHeight)),
                                           zoom: viewZoom.zoom, pan: viewZoom.pan)
            let fit = placed.scale
            let origin = placed.origin
            func screen(_ x: Float, _ y: Float) -> CGPoint { CGPoint(x: origin.x + CGFloat(x) * fit, y: origin.y + CGFloat(y) * fit) }
            // Borda do quadro: dá para ver onde a composição termina mesmo com
            // o fundo do projeto da cor da área de trabalho (só na prévia).
            context.stroke(Path(CGRect(x: origin.x, y: origin.y, width: CGFloat(model.compositionWidth) * fit,
                                       height: CGFloat(model.compositionHeight) * fit)),
                           with: .color(.white.opacity(0.16)), lineWidth: 1)
            // 0.4.1 (mockup "interface 03"): o "olho" do palco escondeu contorno e alças (ver o quadro limpo).
            if viewModes.hideOverlay { return }
            // Omega conteúdo (área 06): desenho e Editar pontos tomam o palco (OmegaContent.swift).
            if DrawingStage.active(model) || EditPointsStage.active(model) { return }
            // 0.4.6: nas vistas do AE (Frente..Personalizada) o chão e os eixos guiam, como na cena 3D.
            if !model.sceneEditor && model.viewMode >= 2 && model.viewOrbit.count >= 4 {
                let lines = model.engine.sceneGuides().map(\.floatValue)
                for i in stride(from: 0, to: lines.count, by: 5) {
                    var line = Path(); line.move(to: screen(lines[i], lines[i + 1])); line.addLine(to: screen(lines[i + 2], lines[i + 3]))
                    let color: Color = lines[i + 4] == 1 ? .yellow : lines[i + 4] == 2 ? .cyan : .gray.opacity(0.22)
                    context.stroke(line, with: .color(color), lineWidth: 1)
                }
            }
            if model.sceneEditor {
                let lines = model.engine.sceneGuides().map(\.floatValue)
                for i in stride(from: 0, to: lines.count, by: 5) {
                    var line = Path(); line.move(to: screen(lines[i], lines[i + 1])); line.addLine(to: screen(lines[i + 2], lines[i + 3]))
                    let kind = Int(lines[i + 4])
                    // 3 chão quadriculado, 4 eixo X, 5 eixo Z (query_scene_guides).
                    let color: Color = kind == 1 ? .yellow : kind == 2 ? .cyan : kind == 3 ? .white.opacity(0.10)
                        : kind == 4 ? SkeletonRing.colors[0].opacity(0.75) : kind == 5 ? SkeletonRing.colors[2].opacity(0.75) : .gray.opacity(0.22)
                    context.stroke(line, with: .color(color), lineWidth: kind >= 4 ? 1.5 : 1)
                }
                if model.rigFixActive {
                    // AutoRig 3D (0.3.6, RigPoseStage.kt `drawRigFixOverlay`): as 17 juntas canônicas.
                    let j = model.humanoidJoints, f = AureaModel.humanoidJointFloats, drag = model.rigFixDrag
                    let count = j.count / f
                    func at(_ b: Int) -> CGPoint? {
                        guard j[b * f] >= 0, j[b * f + 3] > 0.5 else { return nil }
                        if let drag, drag.0 == b { return screen(drag.1.x, drag.1.y) }
                        return screen(j[b * f + 1], j[b * f + 2])
                    }
                    for b in 0..<count {
                        let p = Int(j[b * f + 7])
                        guard p >= 0, p < count, let a = at(p), let c = at(b) else { continue }
                        var line = Path(); line.move(to: a); line.addLine(to: c)
                        context.stroke(line, with: .color(StageInk.outlineUnder), style: StrokeStyle(lineWidth: 4, lineCap: .round))
                        context.stroke(line, with: .color(.white.opacity(0.85)), style: StrokeStyle(lineWidth: 2, lineCap: .round))
                    }
                    for b in 0..<count {
                        guard let c = at(b) else { continue }
                        let on = drag?.0 == b
                        let r: CGFloat = on ? 8 : 5.5
                        circle(&context, c, r + 1.5, StageInk.outlineUnder)
                        circle(&context, c, r, on ? AureaColors.accent : Color(red: 1, green: 0.8, blue: 0.33))
                    }
                } else if model.rigPoseActive {
                    // Pose do rig (RigPoseStage.kt): juntas e ossos no lugar do gizmo.
                    let b = model.poseBones, f = AureaModel.boneFloats, chosen = model.poseBone
                    let count = b.count / f
                    if !model.skeletonHidden {
                        // Ossos como no Blender (RigPoseStage.kt): peça sólida em facetas; os ramos
                        // laterais (ombros no peito, pernas no quadril) são só uma linha tracejada.
                        let side = model.poseSideBones()
                        for i in 0..<count {
                            let parent = Int(b[i * f + 1])
                            guard parent >= 0, parent < count, b[i * f + 9] > 0.5, b[parent * f + 9] > 0.5 else { continue }
                            let a = screen(b[parent * f + 7], b[parent * f + 8]), c = screen(b[i * f + 7], b[i * f + 8])
                            if i < side.count && side[i] {
                                var dash = Path(); dash.move(to: a); dash.addLine(to: c)
                                // Fio escuro por baixo: o tracejado aparece no fundo e em cima de modelo claro.
                                context.stroke(dash, with: .color(SkeletonBoneInk.line.opacity(0.6)), lineWidth: 2)
                                context.stroke(dash, with: .color(.white.opacity(0.85)), style: StrokeStyle(lineWidth: 1, dash: [4, 3]))
                            } else {
                                SkeletonBoneInk.draw(&context, from: a, to: c, on: Int32(b[parent * f]) == chosen)
                            }
                        }
                        // Juntas: bolinhas de arame; a escolhida acesa; anel destaque = keyframe no cabeçote.
                        for i in 0..<count where b[i * f + 9] > 0.5 {
                            let c = screen(b[i * f + 7], b[i * f + 8])
                            let on = Int32(b[i * f]) == chosen
                            // A bolinha acompanha o osso: num dedo ela não pode tampar o desenho.
                            let parent = Int(b[i * f + 1])
                            let bone: CGFloat = parent >= 0 && parent < count
                                ? hypot(c.x - screen(b[parent * f + 7], b[parent * f + 8]).x, c.y - screen(b[parent * f + 7], b[parent * f + 8]).y) : .greatestFiniteMagnitude
                            let r: CGFloat = on ? 6 : min(4.5, max(1.75, bone * 0.12))
                            let ball = Path(ellipseIn: CGRect(x: c.x - r, y: c.y - r, width: r * 2, height: r * 2))
                            context.fill(ball, with: .color(on ? AureaColors.accent : SkeletonBoneInk.light))
                            context.stroke(ball, with: .color(SkeletonBoneInk.line), lineWidth: 0.75)
                            if r >= 3 {
                                var cross = Path()
                                cross.move(to: CGPoint(x: c.x - r, y: c.y)); cross.addLine(to: CGPoint(x: c.x + r, y: c.y))
                                cross.move(to: CGPoint(x: c.x, y: c.y - r)); cross.addLine(to: CGPoint(x: c.x, y: c.y + r))
                                context.stroke(cross, with: .color(SkeletonBoneInk.line), lineWidth: 0.75)
                            }
                            if b[i * f + 6] > 0.5 {
                                context.stroke(Path(ellipseIn: CGRect(x: c.x - r - 3.5, y: c.y - r - 3.5, width: (r + 3.5) * 2, height: (r + 3.5) * 2)),
                                               with: .color(AureaColors.keyframe), lineWidth: 2)
                            }
                        }
                    }
                    // Anéis do osso escolhido: X/Y/Z locais (metade de trás apagada) e o de fora (em volta do olhar).
                    let gz = model.poseGizmo
                    if gz.count == AureaModel.boneGizmoFloats, gz[2] > 0.5, !model.poseMove, !model.poseIk, !model.sceneSelectOnly, fit > 0 {
                        let ring = Float(SkeletonRing.radius / fit)
                        let c = screen(gz[0], gz[1])
                        let outer = SkeletonRing.radius * CGFloat(AureaModel.outerRing)
                        let outerPath = Path(ellipseIn: CGRect(x: c.x - outer, y: c.y - outer, width: outer * 2, height: outer * 2))
                        context.stroke(outerPath, with: .color(StageInk.outlineUnder), lineWidth: 3.5)
                        context.stroke(outerPath, with: .color(.white.opacity(0.75)), lineWidth: 1.5)
                        for axis in 0..<3 {
                            let o = 3 + axis * 6
                            var front = Path(), back = Path()
                            var prev: CGPoint? = nil, prevFront = true
                            for s in 0...48 {
                                let t = Float(s) / 48 * 2 * .pi
                                let cs = cos(t), sn = sin(t)
                                let p = screen(gz[0] + (gz[o] * cs + gz[o + 3] * sn) * ring, gz[1] + (gz[o + 1] * cs + gz[o + 4] * sn) * ring)
                                let isFront = gz[o + 2] * cs + gz[o + 5] * sn >= -0.05
                                if let q = prev {
                                    if prevFront && isFront { front.move(to: q); front.addLine(to: p) } else { back.move(to: q); back.addLine(to: p) }
                                }
                                prev = p; prevFront = isFront
                            }
                            let color = SkeletonRing.colors[axis]
                            context.stroke(back, with: .color(color.opacity(0.3)), style: StrokeStyle(lineWidth: 1.5, lineCap: .round))
                            context.stroke(front, with: .color(StageInk.outlineUnder), style: StrokeStyle(lineWidth: 4.5, lineCap: .round))
                            context.stroke(front, with: .color(color), style: StrokeStyle(lineWidth: 2.5, lineCap: .round))
                        }
                    }
                    // AutoRig 3D (0.3.6): alvo (quadrado) e polo (losango) dos membros com IK.
                    let ik = model.humanoidIk, g = AureaModel.humanoidIkFloats
                    let ikColor = Color(red: 0.33, green: 0.8, blue: 1)
                    for limb in 0..<(ik.count / g) where ik[limb * g] > 0.5 {
                        let o = limb * g
                        let t = screen(ik[o + 1], ik[o + 2]), p = screen(ik[o + 3], ik[o + 4])
                        var link = Path(); link.move(to: t); link.addLine(to: p)
                        context.stroke(link, with: .color(.white.opacity(0.35)), lineWidth: 1)
                        let s: CGFloat = 7
                        context.fill(Path(CGRect(x: t.x - s - 1.5, y: t.y - s - 1.5, width: 2 * s + 3, height: 2 * s + 3)), with: .color(StageInk.outlineUnder))
                        context.fill(Path(CGRect(x: t.x - s, y: t.y - s, width: 2 * s, height: 2 * s)), with: .color(ikColor))
                        if ik[o + 5] > 0.5 {
                            context.stroke(Path(ellipseIn: CGRect(x: t.x - s - 5, y: t.y - s - 5, width: (s + 5) * 2, height: (s + 5) * 2)),
                                           with: .color(AureaColors.accent), lineWidth: 2)
                        }
                        var diamond = Path()
                        diamond.move(to: CGPoint(x: p.x, y: p.y - s)); diamond.addLine(to: CGPoint(x: p.x + s, y: p.y))
                        diamond.addLine(to: CGPoint(x: p.x, y: p.y + s)); diamond.addLine(to: CGPoint(x: p.x - s, y: p.y)); diamond.closeSubpath()
                        context.fill(diamond, with: .color(ikColor.opacity(0.8)))
                    }
                } else if TrackballOverlay.draw(&context, model: model, screen: screen) {
                    // Ferramenta Girar: o trackball no lugar das setas.
                } else if let selected = model.selectedLayer {
                    let g = model.engine.gizmo(selected.id, length: ShellStageGeometry.gizmoLength, localSpace: model.gizmoAxesLocal).map(\.floatValue)
                    if g.count == 8 {
                        let tips = ShellStageGeometry.gizmoTips(stride(from: 0, to: 8, by: 2).map { screen(g[$0], g[$0 + 1]) })
                        for i in 1...3 {
                            var line = Path(); line.move(to: tips[0]); line.addLine(to: tips[i])
                            let color = [StageInk.gizmoX, StageInk.gizmoY, StageInk.gizmoZ][i - 1]
                            context.stroke(line, with: .color(color), lineWidth: 2.5)
                            circle(&context, tips[i], 7.5, color)
                        }
                    }
                }
                return
            }
            if model.panel == .vector && model.vectorFreehand {
                if model.freehandPoints.count >= 4 {
                    var path = Path(); path.move(to: screen(model.freehandPoints[0], model.freehandPoints[1]))
                    for i in stride(from: 2, to: model.freehandPoints.count - 1, by: 2) { path.addLine(to: screen(model.freehandPoints[i], model.freehandPoints[i + 1])) }
                    context.stroke(path, with: .color(StageInk.outlineUnder), style: StrokeStyle(lineWidth: 5, lineCap: .round))
                    context.stroke(path, with: .color(.white), style: StrokeStyle(lineWidth: 3, lineCap: .round))
                }
                return
            }
            if model.panel == .vector && model.vectorEditingPoints {
                if let mask = model.editingMask { drawPath(&context, mask: mask, selected: true, vector: true, screen: screen) }
                return
            }
            if model.panel == .mask {
                for mask in model.masks { drawPath(&context, mask: mask, selected: mask.id == model.selectedMask, vector: false, screen: screen) }
            }
            let playhead = model.status.playhead
            func active(_ row: LayerItem) -> Bool { playhead >= Int64(row.startFrame) && playhead < Int64(row.endFrame) }
            func corners(_ detail: [String: Any]) -> [CGPoint] {
                var data: [Float] = []; guard StageGeom.corners(detail, &data) else { return [] }
                return stride(from: 0, to: 8, by: 2).map { screen(data[$0], data[$0 + 1]) }
            }
            let chrome = !model.hideSelectionBox   // caixa escondida: sem contorno (a seleção continua)
            for row in model.layers where chrome && model.selection.count > 1 && model.selection.contains(row.id) && row.id != model.primarySelection && active(row) {
                if let detail = model.engine.layerDetail(row.id) { outline(&context, corners(detail), under: 2.5, over: 1.5, color: AureaColors.accent) }
            }
            guard let selected = model.selectedLayer, active(selected) else { return }
            let points = corners(model.detail)
            if chrome { outline(&context, points, under: 3.5, over: 2, color: selected.locked ? AureaColors.muted : AureaColors.accent) }
            // Face Pivô aberta: só a mira do pivô (o arrasto move ELE, não a camada).
            if model.pivotStageActive && model.selection.count == 1 && !selected.locked {
                if let pivot = PivotDragSession.pivotPoint(model) {
                    let p = screen(pivot.x, pivot.y)
                    var cross = Path()
                    cross.move(to: CGPoint(x: p.x - 22, y: p.y)); cross.addLine(to: CGPoint(x: p.x + 22, y: p.y))
                    cross.move(to: CGPoint(x: p.x, y: p.y - 22)); cross.addLine(to: CGPoint(x: p.x, y: p.y + 22))
                    let ring = Path(ellipseIn: CGRect(x: p.x - 12, y: p.y - 12, width: 24, height: 24))
                    context.stroke(cross, with: .color(StageInk.outlineUnder), lineWidth: 4)
                    context.stroke(ring, with: .color(StageInk.outlineUnder), lineWidth: 4.5)
                    context.stroke(cross, with: .color(AureaColors.accent), lineWidth: 1.5)
                    context.stroke(ring, with: .color(AureaColors.accent), lineWidth: 2)
                    circle(&context, p, 3.5, StageInk.outlineUnder); circle(&context, p, 2.5, .white)
                }
                return
            }
            // Só o centro e as setas dos eixos: alça de canto (giro/escala) em
            // camada pequena ficava em cima do corpo e roubava o arrasto. Girar
            // e escalar é pela pinça (e, no 3D, pela ferramenta do gizmo).
            let gizmo = model.engine.gizmo(selected.id, length: ShellStageGeometry.gizmoLength, localSpace: model.gizmoAxesLocal).map(\.floatValue)
            if let anchor = model.previewMarkerAnchor {
                let p = screen(anchor.x, anchor.y)
                if gizmo.count != 8 && !model.sceneEditor && model.selection.count == 1 && !selected.locked {
                    // 2D: alças brancas de esticar (LayerResizeGeometry), por baixo das setas.
                    for (i, h) in LayerResizeGeometry.handles(points).enumerated() {
                        let r: CGFloat = shell.grabbedResizeHandle == i ? 7 : i < 4 ? 6 : 5
                        circle(&context, h, r + 1.5, StageInk.outlineUnder); circle(&context, h, r, .white)
                        context.stroke(Path(ellipseIn: CGRect(x: h.x - r, y: h.y - r, width: r * 2, height: r * 2)), with: .color(AureaColors.accent), lineWidth: 1.5)
                    }
                }
                if gizmo.count != 8 {
                    // 2D: setas X/Y (Stage.kt drawAxisHandles), por baixo da mira.
                    let tips = ShellStageGeometry.axisHandles(p, size: size)
                    for i in tips.indices {
                        var line = Path(); line.move(to: p); line.addLine(to: tips[i])
                        let color = i == 0 ? StageInk.gizmoX : StageInk.gizmoY
                        context.stroke(line, with: .color(StageInk.outlineUnder), lineWidth: 5); context.stroke(line, with: .color(color), lineWidth: 2.5)
                        let r: CGFloat = shell.grabbedHandle == i ? 9 : 7.5
                        circle(&context, tips[i], r + 1.5, StageInk.outlineUnder); circle(&context, tips[i], r, color)
                    }
                }
                circle(&context, p, 7, StageInk.outlineUnder)
                circle(&context, p, 5, model.markerFrames.contains(model.status.playhead) ? AureaColors.accent : .white)
                var cross = Path()
                cross.move(to: CGPoint(x: p.x - 10, y: p.y)); cross.addLine(to: CGPoint(x: p.x + 10, y: p.y))
                cross.move(to: CGPoint(x: p.x, y: p.y - 10)); cross.addLine(to: CGPoint(x: p.x, y: p.y + 10))
                context.stroke(cross, with: .color(StageInk.outlineUnder), lineWidth: 3)
                context.stroke(cross, with: .color(AureaColors.accent), lineWidth: 1)
            }
            guard model.selection.count == 1, !selected.locked, !(model.panel == .mask && model.editingMask != nil), points.count == 4 else { return }
            if ShapeStageGeometry.enabled(model) { return }
            if gizmo.count == 8 && !(model.sceneEditor && model.sceneSelectOnly) && !TrackballOverlay.draw(&context, model: model, screen: screen) {
                // Ponta = ferramenta (Stage.kt drawGizmo): bola mover, anel girar, quadrado escala.
                let tips = ShellStageGeometry.gizmoTips(stride(from: 0, to: 8, by: 2).map { screen(gizmo[$0], gizmo[$0 + 1]) })
                let tool = model.gizmoTool
                for i in 1...3 {
                    var line = Path(); line.move(to: tips[0]); line.addLine(to: tips[i])
                    let color = [StageInk.gizmoX, StageInk.gizmoY, StageInk.gizmoZ][i - 1]
                    context.stroke(line, with: .color(StageInk.outlineUnder), lineWidth: 5); context.stroke(line, with: .color(color), lineWidth: 2.5)
                    if tool == 1 {
                        let ring = Path(ellipseIn: CGRect(x: tips[i].x - 10, y: tips[i].y - 10, width: 20, height: 20))
                        context.stroke(ring, with: .color(StageInk.outlineUnder), lineWidth: 5); context.stroke(ring, with: .color(color), lineWidth: 3)
                    } else if tool == 2 {
                        context.fill(Path(CGRect(x: tips[i].x - 9.5, y: tips[i].y - 9.5, width: 19, height: 19)), with: .color(StageInk.outlineUnder))
                        context.fill(Path(CGRect(x: tips[i].x - 8, y: tips[i].y - 8, width: 16, height: 16)), with: .color(color))
                    } else {
                        circle(&context, tips[i], 9, StageInk.outlineUnder); circle(&context, tips[i], 7.5, color)
                    }
                }
                if tool == 2 {
                    context.fill(Path(CGRect(x: tips[0].x - 8.5, y: tips[0].y - 8.5, width: 17, height: 17)), with: .color(StageInk.outlineUnder))
                    context.fill(Path(CGRect(x: tips[0].x - 7, y: tips[0].y - 7, width: 14, height: 14)), with: .color(.white))
                } else { circle(&context, tips[0], 4, .white) }
            }

        }
    }
    private func circle(_ context: inout GraphicsContext, _ p: CGPoint, _ r: CGFloat, _ color: Color) { context.fill(Path(ellipseIn: CGRect(x: p.x - r, y: p.y - r, width: r * 2, height: r * 2)), with: .color(color)) }
    private func outline(_ context: inout GraphicsContext, _ points: [CGPoint], under: CGFloat, over: CGFloat, color: Color) {
        guard let first = points.first else { return }; var p = Path(); p.move(to: first); for point in points.dropFirst() { p.addLine(to: point) }; p.closeSubpath()
        context.stroke(p, with: .color(StageInk.outlineUnder), lineWidth: under); context.stroke(p, with: .color(color), lineWidth: over)
    }
    private func drawPath(_ context: inout GraphicsContext, mask: MaskItem, selected: Bool, vector: Bool, screen: (Float, Float) -> CGPoint) {
        let count = mask.points.count / 6, affine = model.maskAffine, row = vector ? [0, 0, 1] : model.maskPerspective
        guard count > 0, affine.count == 6, row.count == 3 else { return }
        func point(_ index: Int, _ handle: Int = 0) -> CGPoint {
            let x = mask.points[index * 6] + (handle == 0 ? 0 : mask.points[index * 6 + handle])
            let y = mask.points[index * 6 + 1] + (handle == 0 ? 0 : mask.points[index * 6 + handle + 1])
            // Camada 3D: a matriz é projetiva, a conta divide pela perspectiva.
            let raw = row[0] * x + row[1] * y + row[2], w = abs(raw) < 0.000001 ? Float(0.000001) : raw
            return screen((affine[0] * x + affine[2] * y + affine[4]) / w, (affine[1] * x + affine[3] * y + affine[5]) / w)
        }
        var path = Path(); path.move(to: point(0))
        for i in 0..<(mask.closed ? count : max(0, count - 1)) { let j = (i + 1) % count; path.addCurve(to: point(j), control1: point(i, 4), control2: point(j, 2)) }
        if mask.closed { path.closeSubpath() }
        let ink = vector ? Color(hex: 0x5AA8FF) : selected ? StageInk.maskEdit : StageInk.maskOther
        context.stroke(path, with: .color(StageInk.outlineUnder), lineWidth: vector ? 3 : 3.5)
        context.stroke(path, with: .color(ink), lineWidth: vector ? 1.5 : selected ? 2 : 1.5)
        guard selected else { return }
        if vector { drawHandles(&context, count: count, vector: true, point: point) }
        for i in 0..<count {
            let p = point(i), half: CGFloat = vector ? 6 : model.maskDrawing && i == 0 && count >= 3 ? 8.8 : 5.5
            // Stage.kt / VectorStage.kt use 1.5 physical pixels here, unlike
            // the point half-size and the path strokes, which use dp.
            let border = 1.5 / max(1, displayScale)
            context.fill(Path(CGRect(x: p.x - half - border, y: p.y - half - border, width: 2 * (half + border), height: 2 * (half + border))), with: .color(StageInk.outlineUnder))
            context.fill(Path(CGRect(x: p.x - half, y: p.y - half, width: 2 * half, height: 2 * half)), with: .color(i == model.selectedMaskPoint ? (vector ? AureaColors.accent : StageInk.maskEdit) : .white))
            if vector && i == 0 && !mask.closed && count >= 2 { context.stroke(Path(ellipseIn: CGRect(x: p.x - half * 1.9, y: p.y - half * 1.9, width: half * 3.8, height: half * 3.8)), with: .color(AureaColors.accent), lineWidth: 1.5) }
        }
        if !vector { drawHandles(&context, count: count, vector: false, point: point) }
    }
    private func drawHandles(_ context: inout GraphicsContext, count: Int, vector: Bool, point: (Int, Int) -> CGPoint) {
        guard let selected = model.selectedMaskPoint, selected >= 0, selected < count else { return }
        let center = point(selected, 0)
        for index in [2, 4] {
            let h = point(selected, index); guard hypot(h.x - center.x, h.y - center.y) >= 1 else { continue }
            var line = Path(); line.move(to: center); line.addLine(to: h)
            if !vector { context.stroke(line, with: .color(StageInk.outlineUnder), lineWidth: 3) }
            context.stroke(line, with: .color(vector ? Color(hex: 0xB8C4D0) : StageInk.maskEdit), lineWidth: vector ? 1.4 : 1.5)
            circle(&context, h, 7.5, StageInk.outlineUnder); circle(&context, h, 6, vector ? .white : StageInk.maskEdit)
        }
    }
}


// =============================================================================
// A folha contextual: a doca de painéis ou o painel aberto
// =============================================================================
/// Interno (não privado): a interface de Edição usa o mesmo hospedeiro de painéis.
struct ContextSheet: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var realCamera = RealCameraState.shared
    let metrics: EditorMetrics

    var body: some View {
        VStack(spacing: 0) {
            // ContextArea.kt: faixa VAZIA de 12; painel aberto começa no seu
            // próprio cabeçalho, sem puxador ou faixa adicionais.
            // 0.4.1 (mockup "interface 03"): o fio de cima saiu (a borda da folha o faz).
            if !EditorTimelineRefresh.enabled && model.sheetContent != .panel && model.sheetContent != .curve {
                AureaColors.editorPanelHigh.frame(height: StageDim.sheetHandle)
            }

            if realCamera.painting(model) {
                // Real Camera ligada: os controles saem da prévia e ficam aqui.
                RealCameraPanelView()
            } else if model.showAddLayer {
                // Omega adicionar (área 08, P0-1): no largo, o conteúdo do botão aberto na
                // folha da direita (um painel só; antes duplicava com a janela).
                VStack(spacing: 0) {
                    Spacer(minLength: 0)
                    AddPanelView().frame(height: EditorLayout.addContentHeight)
                }
            } else if model.selection.count > 1 && model.panel != .aiVideo && model.panel != .captions {
                BatchToolsView()
            } else {
            // 0.4.1 (mockup "interface 03"): a ferramenta entra deslizando da direita por cima
            // das ferramentas da camada, e o ‹ volta deslizando (~300 ms; par do ContextArea.kt).
            InspectorPageSlide(panelPage: model.panel != .none && model.panel != .dock) {
                // Omega painel: (área 04) o painel da camada da referência (LayerPanelView.swift).
                LayerPanelView()
            } panel: {
            // Omega painel: (área 04, P1-10) trocar de um painel para outro ainda entra por cima
            // (x +100 → 0 em 100 ms, fade de 25 ms); a curva não anima.
            Group {
            switch model.panel {
            case .none, .dock:
                EmptyView()
            case .transform:
                TransformView()
            case .text:
                TextPanelView()
            case .effects:
                EffectsView()
            case .layer3D:
                Panel3DView()
            case .exportPanel:
                ExportView()
            case .appearance:
                AppearancePanel()
            case .speed:
                SpeedPanel()
            case .clipEdit:
                ClipEditPanel()
            case .audio:
                AudioPanel()
            case .shape:
                // Omega aparência (P0-3): Cor e preenchimento na moldura comum (ColorFillPanel.swift).
                ColorFillPanel()
            case .shapeEdit:
                ShapePanel(geometry: true)
            case .mask:
                MaskPanel()
            case .textAnimation:
                TextAnimationPanel()
            case .curve:
                CurvePanel()
            case .presets:
                PresetsPanel()
            case .particles:
                ParticlesPanel()
            case .tracking:
                TrackingPanel()
            case .captions:
                CaptionsPanel()
            case .vector:
                VectorPanel()
            case .aiVideo:
                AiVideoPanel()
            case .filters:
                EditFiltersView()
            case .adjust:
                EditAdjustView()
            // Omega aparência (área 05).
            case .borderShadow:
                BorderShadowPanel()
            case .camera:
                CameraOptionsPanel()
            // Omega conteúdo (área 06): o editor de desenho e o Editar pontos.
            case .drawing:
                DrawingPanelView()
            case .editPoints:
                EditPointsPanelView()
            }
            }
            .modifier(PanelEntrance(panel: model.panel, onAppear: false))
            }
            }
        }
        // 0.4.1 (mockup "interface 03"): a folha do inspetor — cantos de cima de 28, fundo
        // #0E0E10, fio de 16 % e o puxador de 34 × 4 no topo.
        .padding(.top, 8)
        .background(AureaColors.editorPanel)
        .clipShape(DockSheetShape(radius: 28))
        .overlay(DockSheetShape(radius: 28).stroke(Color.white.opacity(0.16), lineWidth: 1))
        .overlay(alignment: .top) {
            RoundedRectangle(cornerRadius: 2).fill(Color.white.opacity(0.22))
                .frame(width: 34, height: 4).padding(.top, 3)
                .allowsHitTesting(false).accessibilityHidden(true)
        }
    }
}

/// 0.4.1 (mockup "interface 03"): a troca de página do inspetor. O painel aberto entra
/// pela borda final por cima das ferramentas da camada, que recuam 1/3 da largura;
/// voltar faz o caminho inverso (0,3 s). Com "reduzir movimento", troca sem animar.
private struct InspectorPageSlide<Tools: View, Panel: View>: View {
    let panelPage: Bool
    private let tools: () -> Tools
    private let panel: () -> Panel
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.layoutDirection) private var direction

    init(panelPage: Bool, @ViewBuilder tools: @escaping () -> Tools, @ViewBuilder panel: @escaping () -> Panel) {
        self.panelPage = panelPage
        self.tools = tools
        self.panel = panel
    }

    var body: some View {
        GeometryReader { geometry in
            let width = geometry.size.width, height = geometry.size.height
            let third = width / 3 * (direction == .rightToLeft ? 1 : -1)
            ZStack {
                if panelPage {
                    panel()
                        .frame(width: width, height: height)
                        .transition(reduceMotion ? AnyTransition.identity : AnyTransition.move(edge: .trailing))
                        .zIndex(1)
                } else {
                    tools()
                        .frame(width: width, height: height)
                        .transition(reduceMotion ? AnyTransition.identity : AnyTransition.offset(x: third))
                        .zIndex(0)
                }
            }
            .frame(width: width, height: height)
            .clipped()
            .animation(reduceMotion ? nil : .easeInOut(duration: 0.3), value: panelPage)
        }
    }
}

// =============================================================================
// A doca: os ladrilhos que abrem cada painel (`shell_dock_*`)
// =============================================================================
private struct BatchToolsView: View {
    @EnvironmentObject private var model: AureaModel
    private var selected: [LayerItem] { model.layers.filter { model.selection.contains($0.id) } }
    var body: some View {
        ScrollView(.vertical) {
        VStack(spacing: 8) {
            HStack(spacing: 0) {
                tool(CupertinoGlyph.ArrowRightToLine, "editor_aparar_inicio_cabecote") { trim(start: true) }
                tool(CupertinoGlyph.Scissors, "editor_dividir_cabecote") { split() }
                tool(CupertinoGlyph.ArrowLeftToLine, "editor_aparar_fim_cabecote") { trim(start: false) }
                AureaColors.border.frame(width: 1, height: 24)
                if EditorTimelineRefresh.enabled {
                    tool(CupertinoGlyph.RectangleStack, "editor_agrupar_camadas_escolhidas") { model.groupSelection() }
                    // Bug #6 (par do GroupSelectionSheet): desagrupar com grupo na seleção.
                    if selected.contains(where: { $0.kind == 12 }) {
                        tool(ShellGlyph.SquareSplit2x2, "editor_desagrupar") { model.ungroup(groups: selected.map(\.id)) }
                    }
                    tool(CupertinoGlyph.DocOnDoc, "editor_copiar_camada") { model.engine.copyLayers(selected.map { NSNumber(value: $0.id) }) }
                    tool(CupertinoGlyph.PlusSquareOnSquare, "editor_duplicar_camada") {
                        model.engine.duplicateLayers(selected.map { NSNumber(value: $0.id) }); model.refreshModel(force: true)
                    }
                } else {
                vectorTool("automirrored.rounded.FormatAlignLeft", "editor_alinhar_inicios") { model.arrangeLayerTimes(0) }
                vectorTool("rounded.Stairs", "editor_escada_comeca_quando_cima_termina") { model.arrangeLayerTimes(1) }
                vectorTool("automirrored.rounded.FormatAlignRight", "editor_alinhar_fins") { model.arrangeLayerTimes(2) }
                }
            }.frame(height: 52).background(StageInk.dockRow, in: RoundedRectangle(cornerRadius: 10))
            HStack(spacing: 0) {
                tool(CupertinoGlyph.ArrowLeftToLine, "editor_alinhar_esquerda_tela", size: 18) { align(0) }
                tool(CupertinoGlyph.ArrowLeftRight, "editor_centralizar_horizontal", size: 18) { align(1) }
                tool(CupertinoGlyph.ArrowRightToLine, "editor_alinhar_direita_tela", size: 18) { align(2) }
                tool(CupertinoGlyph.ArrowUpToLine, "editor_alinhar_topo_tela", size: 18) { align(3) }
                tool(CupertinoGlyph.ArrowUpArrowDown, "editor_centralizar_vertical", size: 18) { align(4) }
                tool(CupertinoGlyph.ArrowDownToLine, "editor_alinhar_base_tela", size: 18) { align(5) }
                tool(CupertinoGlyph.ArrowLeftRightSquare, "editor_distribuir_horizontal_vaos_iguais", size: 18, enabled: selected.count >= 3) { distribute(horizontal: true) }
                tool(CupertinoGlyph.ArrowUpDownSquare, "editor_distribuir_vertical_vaos_iguais", size: 18, enabled: selected.count >= 3) { distribute(horizontal: false) }
                // Ajustar / preencher a tela (app antigo), junto dos alinhamentos.
                tool(CupertinoGlyph.FullscreenExit, "editor_ajustar_tela", size: 18) { pause(); model.fitToCanvas(selected.map(\.id), fill: false) }
                tool(CupertinoGlyph.Fullscreen, "editor_preencher_tela", size: 18) { pause(); model.fitToCanvas(selected.map(\.id), fill: true) }
            }.frame(height: 48).background(StageInk.dockRow, in: RoundedRectangle(cornerRadius: 10))
            Spacer(minLength: 0)
        }.padding(.horizontal, 10).padding(.top, 4)
        }.accessibilityIdentifier("timeline.batch.tools")
    }
    private func tool(_ glyph: Character, _ key: String, size: CGFloat = 20, enabled: Bool = true, action: @escaping () -> Void) -> some View {
        Button(action: action) { CupertinoGlyph.text(glyph, size: size, color: enabled ? AureaColors.text : AureaColors.disabled).frame(maxWidth: .infinity, maxHeight: .infinity) }
            .buttonStyle(.plain).disabled(!enabled).accessibilityLabel(AureaText.t(key))
    }
    private func vectorTool(_ name: String, _ key: String, action: @escaping () -> Void) -> some View {
        Button(action: action) { MaterialGlyph(name, size: 22, color: AureaColors.text).frame(maxWidth: .infinity, maxHeight: .infinity) }
            .buttonStyle(.plain).accessibilityLabel(AureaText.t(key))
    }
    private var timeTargets: [LayerItem] {
        selected.filter { !$0.locked && model.status.playhead > Int64($0.startFrame) && model.status.playhead < Int64($0.endFrame) }
    }
    private func trim(start: Bool) {
        let targets = timeTargets
        guard !targets.isEmpty else { model.toast = AureaText.t(selected.allSatisfy(\.locked) ? "sh_layers_locked_unlock_to_edit" : "editor_leve_cabecote_dentro_camadas"); return }
        pause(); model.beginGesture(start ? "aparar início" : "aparar fim")
        for row in targets {
            if start { model.trimStartToPlayhead(row.id, at: model.status.playhead) }
            else { model.trimEnd(row.id, at: model.status.playhead) }
        }
        model.endGesture()
    }
    private func split() {
        guard !timeTargets.isEmpty else { model.toast = AureaText.t("editor_leve_cabecote_dentro_camadas"); return }
        pause(); model.splitAtPlayhead(timeTargets.map(\.id))
    }
    private struct Box {
        let id: Int64
        let x: Float, y: Float, left: Float, top: Float, right: Float, bottom: Float
    }
    private var boxes: [Box] {
        selected.filter { !$0.locked }.compactMap { row in
            guard let detail = model.queryDetail(row.id), let b = StageGeom.bounds(detail) else { return nil }
            let pos = StageGeom.floats(detail["position"])
            guard pos.count >= 2 else { return nil }
            return Box(id: row.id, x: pos[0], y: pos[1], left: b.0, top: b.1, right: b.2, bottom: b.3)
        }
    }
    private func align(_ edge: Int) {
        let moves: [(Int64, Float, Float)] = boxes.map { box in
            var x = box.x, y = box.y
            switch edge {
            case 0: x -= box.left
            case 1: x += Float(model.compositionWidth) / 2 - (box.left + box.right) / 2
            case 2: x += Float(model.compositionWidth) - box.right
            case 3: y -= box.top
            case 4: y += Float(model.compositionHeight) / 2 - (box.top + box.bottom) / 2
            default: y += Float(model.compositionHeight) - box.bottom
            }
            return (box.id, x, y)
        }
        apply(moves, label: "alinhar")
    }
    private func distribute(horizontal: Bool) {
        let items = boxes.sorted { horizontal ? $0.left < $1.left : $0.top < $1.top }
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
        apply(moves, label: "distribuir")
    }
    private func apply(_ moves: [(Int64, Float, Float)], label: String) {
        guard !moves.isEmpty else { return }
        pause(); model.beginGesture(label)
        for move in moves { model.setTransform2(0, move.1, 1, move.2, layer: move.0) }
        model.endGesture()
    }
    private func pause() { if model.status.playing != 0 { model.playPause() } }
}

// Omega painel: (área 04) a doca de fichas (`DockView`) saiu; o painel da camada
// mora em LayerPanelView.swift.

// TextPanel.kt: live content, selection spans, and the in-panel font browser.
private struct TextPanelView: View {
    @EnvironmentObject private var model: AureaModel
    @State private var content = ""
    @State private var selection = NSRange(location: 0, length: 0)
    @State private var editing = false
    @State private var showingFonts = false
    @State private var currentFont = ""
    @State private var weight: UInt32 = 400
    @State private var italic = false
    @State private var fonts: [[String: Any]] = []
    @State private var query = ""
    private var layerId: Int64 { model.primarySelection ?? 0 }
    private var familyNames: [String] {
        Array(Set(fonts.compactMap { $0["family"] as? String })).filter { query.trimmingCharacters(in: .whitespaces).isEmpty || $0.localizedCaseInsensitiveContains(query) }.sorted { $0.localizedCaseInsensitiveCompare($1) == .orderedAscending }
    }
    private var selectedStyles: [[String: Any]] { fonts.filter { $0["family"] as? String == currentFont } }
    var body: some View {
        VStack(spacing: 0) {
            PanelHeader(title: AureaText.t(showingFonts ? "panel_fonte" : "text_options")) { dismissEditing(); model.panel = .none }
            if showingFonts { fontPanel }
            else {
                ScrollView {
                    VStack(alignment: .leading, spacing: 0) {
                        ZStack(alignment: .topLeading) {
                            if content.isEmpty { Text(AureaText.t("panel_digite_texto")).font(.aurea(size: 15)).foregroundStyle(AureaColors.muted).padding(.horizontal, 12).padding(.vertical, 10).allowsHitTesting(false) }
                            NativeTextInput(text: $content, selection: $selection, editing: $editing, editable: false,
                                alignment: (model.engine.text(forLayer: layerId)?["alignment"] as? NSNumber)?.intValue ?? 0) { _ in }
                        }.frame(minHeight: 56).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
                        if selection.length > 0 { spanTools.padding(.top, 6) }
                        // Omega conteúdo (área 06, P0-2): o navegador de fontes em tela cheia.
                        Button { dismissEditing(); FontBrowserRequest.shared.open = true } label: {
                            HStack(spacing: 0) {
                                Text(AureaText.t("panel_fonte")).font(.aurea(size: 13)).foregroundStyle(AureaColors.text)
                                Spacer()
                                Text(currentFont.isEmpty ? AureaText.t("panel_padrao_aparelho") : currentFont).font(.aurea(size: 13)).foregroundStyle(AureaColors.accent).lineLimit(1)
                                Text("  ›").font(.aurea(size: 15)).foregroundStyle(AureaColors.muted)
                            }.frame(height: 48).contentShape(RoundedRectangle(cornerRadius: 10))
                        }.buttonStyle(AureaPressStyle(shrink: 1)).padding(.top, 10)
                        TextAppearanceControls()
                    }.padding(.horizontal, 18).padding(.top, 12).padding(.bottom, 24)
                }.scrollDismissesKeyboard(.interactively)
            }
        }.onAppear { load(); fonts = model.engine.availableFonts() }
            .onChange(of: layerId) { _ in dismissEditing(); content = ""; selection = NSRange(location: 0, length: 0); showingFonts = false; load() }
            .onChange(of: model.status.modelRevision) { _ in load() }
    }
    /// EditorStore.importFont: TTF/OTF copiado para Documents/Media (pasta que o
    /// motor varre ao abrir), registrado no CoreText e no motor e aplicado à
    /// camada que estava escolhida quando o seletor abriu.
    private func importFont(_ url: URL, target: Int64, apply: Bool = true) {
        // 0.1.8: coleções (TTC/OTC) e WOFF também — o motor converte em TTF/OTF.
        guard ["ttf", "otf", "ttc", "otc", "woff"].contains(url.pathExtension.lowercased()) else {
            model.toast = AureaText.t("msg_use_uma_fonte_ttf_ou_otf"); return
        }
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        AureaPaths.ensureDirectories()
        let destination = AureaPaths.mediaDestination(for: url.lastPathComponent)
        do { try AureaPaths.copyImport(url, to: destination) }
        catch { model.toast = AureaText.t("msg_nao_deu_para_ler_essa_fonte"); return }
        guard let font = model.engine.importFont(atPath: destination.path) else {
            try? FileManager.default.removeItem(at: destination)
            model.toast = AureaText.t("msg_nao_deu_para_ler_essa_fonte"); return
        }
        // O CoreText recebe o arquivo que o motor usa (WOFF/coleção viram TTF/OTF).
        FontImportPicker.registerWithCoreText((font["path"] as? String).map { URL(fileURLWithPath: $0) } ?? destination)
        query = ""
        fonts = model.engine.availableFonts()
        if apply && model.primarySelection == target { chooseFont(font) }
        model.toast = AureaText.t("msg_fonte_importada", font["family"] as? String ?? "")
    }
    private var spanTools: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 6) {
                Text(AureaText.t("panel_trecho_escolhido")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                NativePanelChip(AureaText.t("panel_cor")) { spanColor() }
                NativePanelChip(AureaText.t("panel_negrito")) { applySpan(weight: 700) }
                NativePanelChip(AureaText.t("panel_maior")) { applySpan(scale: 1.35) }
                NativePanelChip(AureaText.t("panel_normal")) {
                    guard let range = scalarRange else { return }
                    _ = model.engine.clearTextSpans(layerId, start: range.0, end: range.1); model.refreshModel(force: true)
                }
            }
        }
    }
    private var scalarRange: (UInt32, UInt32)? {
        let source = content as NSString
        let a = min(source.length, max(0, selection.location)), b = min(source.length, max(0, selection.location + selection.length))
        guard b > a else { return nil }
        return (UInt32(source.substring(to: a).unicodeScalars.count), UInt32(source.substring(to: b).unicodeScalars.count))
    }
    private func applySpan(weight: UInt32 = 0, scale: Float = 1) {
        guard let range = scalarRange else { return }
        _ = model.engine.setTextSpan(layerId, start: range.0, end: range.1, color: [], weight: weight, scale: scale); model.refreshModel(force: true)
    }
    private func spanColor() {
        guard let range = scalarRange else { return }
        let layer = layerId
        let color = (model.engine.text(forLayer: layer)?["color"] as? [NSNumber] ?? [1, 1, 1, 1]).map(\.floatValue)
        dismissEditing(); model.beginGesture(AureaText.t("panel_cor"))
        model.colorSheet = ColorSheetRequest(title: AureaText.t("panel_cor"), initial: color, onChange: { r, g, b, _ in
            _ = model.engine.setTextSpan(layer, start: range.0, end: range.1, color: [r, g, b, 1].map { NSNumber(value: $0) }, weight: 0, scale: 1)
            model.refreshModel(force: true)
        }, onDone: { model.endGesture() })
    }
    private var fontPanel: some View {
        VStack(spacing: 0) {
            HStack(spacing: 8) {
                TextField(AureaText.t("panel_buscar_fonte"), text: $query).font(.aurea(size: 14)).foregroundStyle(AureaColors.text)
                    .padding(.horizontal, 12).padding(.vertical, 10).frame(minHeight: 40).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
                Button {
                    dismissEditing()
                    let target = layerId
                    FontImportPicker.presentMultiple { urls in for url in urls { importFont(url, target: target, apply: urls.count == 1) } }
                } label: {
                    Text(AureaText.t("panel_importar")).font(.aurea(size: 13)).foregroundStyle(AureaColors.accent).padding(.horizontal, 12).padding(.vertical, 10).background(AureaColors.accentDim, in: RoundedRectangle(cornerRadius: 10))
                }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityIdentifier("text.font.import")
            }
            if selectedStyles.count > 1 {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 6) {
                        ForEach(Array(selectedStyles.enumerated()), id: \.offset) { entry in
                            let item = entry.element
                            Button { chooseFont(item) } label: {
                                Text(item["style"] as? String ?? "Regular").font(previewFont(item, size: 12)).foregroundStyle(isCurrent(item) ? AureaColors.accent : AureaColors.text)
                                    .padding(.horizontal, 10).padding(.vertical, 6).background(isCurrent(item) ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                            }.buttonStyle(AureaPressStyle(shrink: 1))
                        }
                    }
                }.padding(.top, 8)
            }
            ScrollView {
                LazyVStack(spacing: 0) {
                    fontRow(AureaText.t("panel_padrao_aparelho"), font: nil, selected: currentFont.isEmpty)
                    ForEach(familyNames, id: \.self) { family in
                        let item = fonts.filter { $0["family"] as? String == family }.min { fontScore($0) < fontScore($1) }
                        fontRow(family, font: item, selected: currentFont == family)
                    }
                }.padding(.top, 6)
            }
        }.padding(.horizontal, 14).padding(.top, 8)
    }
    private func fontRow(_ name: String, font: [String: Any]?, selected: Bool) -> some View {
        Button { chooseFont(font) } label: {
            HStack {
                Text(name).font(previewFont(font, size: 17)).lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
                if fonts.contains(where: { $0["family"] as? String == name && ($0["path"] as? String ?? "").hasPrefix(AureaPaths.documents.path) }) {
                    Text(AureaText.t("i18n_font_imported")).font(.aurea(size: 11)).foregroundStyle(AureaColors.muted)
                }
            }.foregroundStyle(selected ? AureaColors.accent : AureaColors.text).padding(.horizontal, 10).frame(height: 46)
                .background(selected ? AureaColors.accentDim : Color.clear, in: RoundedRectangle(cornerRadius: 10))
        }.buttonStyle(AureaPressStyle(shrink: 1))
    }
    private func fontScore(_ item: [String: Any]) -> Int { abs(((item["weight"] as? NSNumber)?.intValue ?? 400) - 400) + ((item["italic"] as? Bool ?? false) ? 1000 : 0) }
    private func isCurrent(_ item: [String: Any]) -> Bool { (item["weight"] as? NSNumber)?.uint32Value == weight && (item["italic"] as? Bool ?? false) == italic }
    private func previewFont(_ item: [String: Any]?, size: CGFloat) -> Font {
        guard let item else { return .aurea(size: size) }
        return NativeFontPreview.font(path: item["path"] as? String ?? "", family: item["family"] as? String ?? "", size: size)
    }
    private func load() {
        guard let info = model.engine.text(forLayer: layerId) else { return }
        if !editing { content = info["content"] as? String ?? "" }
        currentFont = info["fontFamily"] as? String ?? ""; weight = (info["fontWeight"] as? NSNumber)?.uint32Value ?? 400; italic = (info["fontItalic"] as? NSNumber)?.boolValue ?? false
    }
    private func dismissEditing() { UIApplication.shared.sendAction(#selector(UIResponder.resignFirstResponder), to: nil, from: nil, for: nil); editing = false }
    private func chooseFont(_ font: [String: Any]?) {
        let family = font?["family"] as? String ?? "", path = font?["path"] as? String ?? ""
        let ok = model.engine.setTextFont(forLayer: layerId, family: family, weight: (font?["weight"] as? NSNumber)?.uint32Value ?? 400,
            italic: font?["italic"] as? Bool ?? false, path: path.hasPrefix(AureaPaths.documents.path) ? path : "")
        if ok { model.refreshModel(force: true); load() } else { model.toast = AureaText.t("msg_nao_deu_para_ler_essa_fonte") }
    }
}

/// O seletor de fonte (TTF/OTF) apresentado pelo UIKit, por cima do que
/// estiver na tela. Antes era um `.fileImporter` dentro do painel de texto,
/// aninhado sob o `.fileImporter` da raiz do editor (AddLayerPickers); o
/// SwiftUI não apresenta com confiança um importador abaixo de outro e tocar
/// em "Importar" não abria nada. O painel 3D (dois importadores encadeados na
/// mesma view, também sob a raiz) tinha o mesmo defeito.
enum FontImportPicker {
    private static var delegate: Delegate?
    static var types: [UTType] {
        [UTType.font] + ["ttf", "otf", "ttc", "otc", "woff"].compactMap { UTType(filenameExtension: $0) }
    }
    static func presentMultiple(_ picked: @escaping ([URL]) -> Void) {
        let windows = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.flatMap(\.windows)
        guard var presenter = (windows.first(where: \.isKeyWindow) ?? windows.first)?.rootViewController else { return }
        while let next = presenter.presentedViewController, !next.isBeingDismissed { presenter = next }
        let picker = UIDocumentPickerViewController(forOpeningContentTypes: types, asCopy: true)
        // O UIKit guarda o delegate fraco: o dono fica aqui até a escolha.
        let owner = Delegate { urls in
            FontImportPicker.delegate = nil
            picked(urls)
        }
        FontImportPicker.delegate = owner
        picker.delegate = owner
        picker.allowsMultipleSelection = true
        picker.shouldShowFileExtensions = true
        presenter.present(picker, animated: true)
    }
    /// Para a prévia da lista (escrita na própria fonte). Já registrada não é erro.
    static func registerWithCoreText(_ url: URL) {
        var error: Unmanaged<CFError>?
        if !CTFontManagerRegisterFontsForURL(url as CFURL, .process, &error) { _ = error?.takeRetainedValue() }
    }
    final class Delegate: NSObject, UIDocumentPickerDelegate {
        private let done: ([URL]) -> Void
        private var finished = false
        init(_ done: @escaping ([URL]) -> Void) { self.done = done }
        private func finish(_ urls: [URL]) { guard !finished else { return }; finished = true; done(urls) }
        func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) { finish(urls) }
        func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) { finish([]) }
    }
}

/// Seletor de ARQUIVOS do app inteiro (modelo 3D, texturas/.mtl, áudio, SVG,
/// PSD, HDRI, LUT, legenda, projeto de efeitos): o `UIDocumentPickerViewController`
/// apresentado pelo UIKit a partir do controlador do topo — nunca um
/// `.fileImporter` aninhado (o SwiftUI só atende UM por hierarquia; os de
/// baixo ficavam mudos) nem um seletor embutido num `fullScreenCover` (no
/// iOS 26/27 o seletor embutido se fecha sozinho ao escolher e a capa do
/// SwiftUI fica presa: o segundo import não abria nada). `asCopy` = o arquivo
/// já chega copiado no sandbox (iCloud/Drive materializados pelo sistema);
/// quem chama ainda copia para a pasta do projeto.
enum DocumentImportPicker {
    private static var delegate: Delegate?
    /// `picked(nil)` = cancelado; lista vazia nunca chega.
    /// `asCopy` false = abrir no lugar (PASTA: o sistema não copia diretório); o chamador
    /// pede o acesso com `startAccessingSecurityScopedResource`.
    static func present(types: [UTType], multiple: Bool = false, asCopy: Bool = true, _ picked: @escaping ([URL]?) -> Void) {
        let windows = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.flatMap(\.windows)
        guard var presenter = (windows.first(where: \.isKeyWindow) ?? windows.first)?.rootViewController else {
            NSLog("Aurea file picker: sem janela para apresentar")
            picked(nil); return
        }
        while let next = presenter.presentedViewController, !next.isBeingDismissed { presenter = next }
        // `.item` no fim: provedor que não marca public.data (FBX/OBJ/MTL/GLB
        // sem tipo registrado) não fica cinza no seletor; o import valida.
        var all = types
        if asCopy && !all.contains(.item) { all.append(.item) }
        let picker = UIDocumentPickerViewController(forOpeningContentTypes: all, asCopy: asCopy)
        // O UIKit guarda o delegate fraco: o dono fica aqui até a escolha.
        let owner = Delegate { urls in
            DocumentImportPicker.delegate = nil
            NSLog("Aurea file picker: %@, %ld item(s)", urls == nil ? "cancelled" : "selected", urls?.count ?? 0)
            picked(urls?.isEmpty == true ? nil : urls)
        }
        DocumentImportPicker.delegate = owner
        picker.delegate = owner
        picker.allowsMultipleSelection = multiple
        picker.shouldShowFileExtensions = true
        // Tela cheia também no iPad: sem popover de provedor preso ao SwiftUI.
        picker.modalPresentationStyle = .fullScreen
        presenter.present(picker, animated: true)
    }
    final class Delegate: NSObject, UIDocumentPickerDelegate {
        private let done: ([URL]?) -> Void
        private var finished = false
        init(_ done: @escaping ([URL]?) -> Void) { self.done = done }
        private func finish(_ urls: [URL]?) {
            guard !finished else { return }; finished = true
            DispatchQueue.main.async { self.done(urls) }
        }
        func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) { finish(urls) }
        func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) { finish(nil) }
    }
    /// glTF/GLB/FBX/OBJ (+ .mtl, .zip e pastas): tipos declarados no Info.plist
    /// e os genéricos de reserva.
    static let modelTypes: [UTType] = [
        UTType(importedAs: "com.autodesk.fbx", conformingTo: .data),
        UTType(importedAs: "com.aurea.import.obj", conformingTo: .data),
        UTType(importedAs: "com.aurea.import.glb", conformingTo: .data),
        UTType(importedAs: "com.aurea.import.gltf", conformingTo: .data),
        UTType(importedAs: "com.aurea.import.mtl", conformingTo: .data),
        .data, .content, .zip, .folder, .item
    ]
}

private enum NativeFontPreview {
    static var names: [String: String] = [:]
    static func font(path: String, family: String, size: CGFloat) -> Font {
        if path.isEmpty { return family.isEmpty ? .aurea(size: size) : .custom(family, size: size) }
        if let name = names[path] { return .custom(name, size: size) }
        let url = URL(fileURLWithPath: path)
        CTFontManagerRegisterFontsForURL(url as CFURL, .process, nil)
        let descriptors = CTFontManagerCreateFontDescriptorsFromURL(url as CFURL) as? [CTFontDescriptor]
        let name = descriptors?.first.flatMap { CTFontDescriptorCopyAttribute($0, kCTFontNameAttribute) as? String } ?? family
        names[path] = name
        return .custom(name, size: size)
    }
}

// Omega conteúdo (área 06, P0-1): o diálogo "Editar texto" (TextContentEditor) saiu;
// o MODO DE TEXTO mora em TextEditModeView.swift.

private final class FocusedContentTextView: UITextView {
    var wantsInitialFocus = false
    var selectAllInitially = false
    override func didMoveToWindow() {
        super.didMoveToWindow()
        guard window != nil, wantsInitialFocus else { return }
        DispatchQueue.main.async { [weak self] in
            guard let self, self.window != nil, self.wantsInitialFocus else { return }
            if self.becomeFirstResponder() {
                self.wantsInitialFocus = false
                let count = (self.text as NSString).length
                self.selectedRange = NSRange(location: self.selectAllInitially ? 0 : count, length: self.selectAllInitially ? count : 0)
            }
        }
    }
}

private struct NativeTextInput: UIViewRepresentable {
    @Binding var text: String
    @Binding var selection: NSRange
    @Binding var editing: Bool
    var editable = true
    var autoFocus = false
    var selectAllOnFocus = false
    var scrollable = false
    var alignment = 0
    var onContentHeight: (CGFloat) -> Void = { _ in }
    private var nativeAlignment: NSTextAlignment { alignment == 1 ? .center : (alignment == 2 ? .right : .left) }
    let onChange: (String) -> Void
    func makeCoordinator() -> Coordinator { Coordinator(self) }
    func makeUIView(context: Context) -> UITextView {
        let view = FocusedContentTextView(); view.delegate = context.coordinator; view.isScrollEnabled = scrollable
        view.isEditable = editable; view.wantsInitialFocus = autoFocus; view.selectAllInitially = selectAllOnFocus
        view.text = text; view.textAlignment = nativeAlignment; view.selectedRange = selection
        view.backgroundColor = .clear; view.font = .systemFont(ofSize: 15); view.textColor = UIColor(AureaColors.text); view.tintColor = UIColor(AureaColors.accent)
        view.textContainerInset = UIEdgeInsets(top: 10, left: 12, bottom: 10, right: 12); view.textContainer.lineFragmentPadding = 0
        view.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        return view
    }
    func updateUIView(_ view: UITextView, context: Context) {
        context.coordinator.parent = self
        view.textAlignment = nativeAlignment
        if view.text != text && view.markedTextRange == nil {
            let old = view.selectedRange; view.text = text
            let start = min(old.location, (text as NSString).length)
            view.selectedRange = NSRange(location: start, length: min(old.length, (text as NSString).length - start))
        }
    }
    func sizeThatFits(_ proposal: ProposedViewSize, uiView: UITextView, context: Context) -> CGSize? {
        guard let width = proposal.width else { return nil }
        if scrollable {
            context.coordinator.publishContentHeight(uiView.sizeThatFits(CGSize(width: width, height: .greatestFiniteMagnitude)).height)
            return CGSize(width: width, height: max(0, proposal.height ?? 96))
        }
        let result = uiView.sizeThatFits(CGSize(width: width, height: .greatestFiniteMagnitude))
        return CGSize(width: width, height: max(56, result.height))
    }
    final class Coordinator: NSObject, UITextViewDelegate {
        var parent: NativeTextInput
        private var publishedHeight: CGFloat = -1
        init(_ parent: NativeTextInput) { self.parent = parent }
        func publishContentHeight(_ height: CGFloat) {
            guard height.isFinite, abs(height - publishedHeight) >= 0.5 else { return }
            publishedHeight = height
            DispatchQueue.main.async { [weak self] in self?.parent.onContentHeight(height) }
        }
        func textViewDidBeginEditing(_ textView: UITextView) { parent.editing = true }
        func textViewDidEndEditing(_ textView: UITextView) { parent.editing = false }
        func textViewDidChange(_ textView: UITextView) {
            parent.text = textView.text; parent.selection = textView.selectedRange; parent.onChange(textView.text)
            textView.invalidateIntrinsicContentSize()
        }
        func textViewDidChangeSelection(_ textView: UITextView) { if textView.isFirstResponder { parent.selection = textView.selectedRange } }
    }
}


// =============================================================================
// Adicionar camada (as abas do "＋" da casca)
// =============================================================================
private struct AddCardsHeightKey: PreferenceKey {
    static let defaultValue: CGFloat = 100
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) { value = nextValue() }
}

private struct AddLayerSheet: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    var tab = 0
    var maxCardHeight: CGFloat = 340
    @State private var cardContentHeight: CGFloat = 100
    /// Aberto como `.sheet` (cena 3D): o seletor espera a folha fechar antes de subir.
    var inSheet = false
    /// Aba 3D mostrando a grade "Formas 3D" (Shape3DViews.swift).
    @State private var shapes3D = false
    private let categories: [(String, Character)] = [
        ("sh_add_tab_shape", ShellGlyph.SquareOnCircle), ("sh_add_tab_media", CupertinoGlyph.PhotoOnRectangle),
        ("sh_add_tab_audio", CupertinoGlyph.MusicNote2), ("sh_add_tab_text", CupertinoGlyph.Textformat),
        ("sh_add_tab_element", ShellGlyph.CircleGridHex), ("sh_add_tab_3d", CupertinoGlyph.Cube),
        ("sh_add_tab_draw", ShellGlyph.Scribble), ("sh_add_tab_vector", CupertinoGlyph.PencilOutline),
        // 8: presets de texto (fim da lista para não mudar os índices; na barra fica ao lado do Texto).
        ("tp_add_tab", CupertinoGlyph.WandStars)
    ]
    private let shapes: [(Int, String)] = [
        (0, "sh_shape_circle"), (10, "sh_shape_square"), (1, "sh_shape_rounded"), (12, "sh_shape_capsule"),
        (4, "sh_shape_triangle"), (14, "sh_shape_right_triangle"), (6, "editor_poligono"), (11, "editor_estrela"),
        (2, "sh_shape_cross"), (3, "sh_shape_ring"), (5, "sh_shape_slice"), (7, "sh_shape_flower"), (8, "sh_shape_arrow"),
        (16, "sh_shape_pentagon"), (15, "sh_shape_octagon"), (17, "sh_shape_trapezoid"), (18, "sh_shape_parallelogram"),
        (19, "sh_shape_star4"), (20, "sh_shape_star6"), (21, "sh_shape_gear"), (22, "sh_shape_double_arrow"),
        // Formas paramétricas (presets 23..32 do motor: explosão e os tipos 16..24).
        (23, "sh_shape_burst"), (24, "sh_shape_line"), (25, "sh_shape_diamond"), (26, "sh_shape_heart"),
        (27, "sh_shape_seal"), (28, "sh_shape_arc"), (29, "sh_shape_bubble"), (30, "sh_shape_bolt"),
        (31, "sh_shape_wave"), (32, "sh_shape_blob")
    ]

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Text(AureaText.t(tab == ShellAddCategories.textPresets ? "tp_title" : categories[tab].0)).font(.aurea(size: 16, weight: .semibold))
                Spacer()
                ShellBarButton(glyph: CupertinoGlyph.Xmark, description: AureaText.t("editor_fechar_adicionar"), action: close)
            }.padding(.leading, 16).frame(height: 48)
            AureaColors.hairline.frame(height: 1)
            if tab == 0 { shapeGrid }
            else if tab == 1 {
                EditorMediaGallery(openFiles: { photos($0 ? .video : .photo) }, openAI: { model.openPanel(.aiVideo) }) { items in
                    model.importMediaBatch(items); close()
                }
            } else if tab == ShellAddCategories.textPresets {
                TextPresetPicker(onAdded: close)
            } else { cards }
        }
        .background(AureaColors.editorPanel)
        // Os seletores NÃO moram aqui: ver `AddLayerPickers` (raiz do editor).
    }

    private var shapeGrid: some View {
        GeometryReader { geometry in
            let columns = min(3, max(2, Int((geometry.size.width - 24) / 96)))
            ScrollView {
                LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 8), count: columns), spacing: 10) {
                    ForEach(shapes, id: \.0) { preset, key in
                        Button { model.addShape(UInt32(preset)); close() } label: {
                            VStack(spacing: 4) {
                                ShellShapeGlyph(preset: preset).padding(12).aspectRatio(1, contentMode: .fit)
                                    .background(StageInk.dockTile, in: RoundedRectangle(cornerRadius: 12))
                                Text(AureaText.t(key)).font(.aurea(size: 11)).foregroundStyle(AureaColors.muted).lineLimit(1)
                            }
                        }.buttonStyle(.plain).accessibilityLabel(AureaText.t(key))
                    }
                }.padding(.horizontal, 12).padding(.vertical, 10)
            }
        }
    }

    private var cards: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 8) {
                LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 8), count: 3), spacing: 8) {
                    switch tab {
                    case 1:
                        card("editor_galeria", glyph: CupertinoGlyph.PhotoOnRectangle, accent: true) { photos(.gallery) }
                        card("editor_foto", glyph: CupertinoGlyph.Photo) { photos(.photo) }
                        card("editor_video", glyph: CupertinoGlyph.Videocam) { photos(.video) }
                        card("sh_add_ai_video", glyph: CupertinoGlyph.WandStars) { model.openPanel(.aiVideo) }
                        card("seq_title", glyph: CupertinoGlyph.SquareStack3dUp) { openSequenceChooser() }
                            .accessibilityIdentifier("add.sequence")
                    case 2:
                        card("editor_musica_ou_som", glyph: CupertinoGlyph.MusicNote, accent: true) { files(.audioFile) }
                        card("voice_title", glyph: CupertinoGlyph.MicFill, color: AureaColors.danger) { openVoiceRecorder() }
                            .accessibilityIdentifier("add.voice")
                        card("sh_add_video_sound", glyph: CupertinoGlyph.Film) { photos(.audioFromVideo) }
                        card("sh_add_marker_at_playhead", glyph: CupertinoGlyph.Bookmark) { close(); model.toggleMarkerAt(model.status.playhead) }
                    case 3:
                        // Legendas automáticas viraram efeito (seletor de efeitos da camada de fala).
                        card("sh_add_tab_text", glyph: CupertinoGlyph.Textformat, accent: true) { model.addText(); close() }
                    case 4:
                        drawnCard("sh_add_null", kind: -1) { model.addNull(threeD: false); close() }
                        card("particular_title", glyph: CupertinoGlyph.Sparkles, color: ShellColors.text3D) { model.addParticles(20); close() }   // 20 = Particular (preset Padrão)
                        card("add_element_aurora", glyph: CupertinoGlyph.Sparkles, color: ShellColors.text3D) { model.addAurora(); close() }
                        card("editor_camada_ajuste", glyph: CupertinoGlyph.WandStars) { close(); model.addAdjustmentLayer() }
                        card("scene_flare3d", glyph: CupertinoGlyph.Sparkles, color: ShellColors.text3D) { model.addLight(3); close() }
                        card("grid_builder", glyph: CupertinoGlyph.SquareGrid2x2) { model.createGrid(); close() }
                        card("sh_add_group_selection", glyph: CupertinoGlyph.Folder) {
                            if model.selection.isEmpty { model.toast = AureaText.t("sh_add_pick_layers_to_group") }
                            else { close(); model.groupSelection() }
                        }
                    case 5:
                        if shapes3D {
                            card("shape3d_back", glyph: CupertinoGlyph.ChevronLeft) { shapes3D = false }
                            ForEach(0..<Shape3DState.names.count, id: \.self) { kind in
                                cardBody(Shape3DState.names[kind], action: { model.addShape3D(kind: kind); close() }) {
                                    Shape3DGlyph(kind: kind).frame(width: 30, height: 30)
                                }.accessibilityIdentifier("shape3d.add.\(kind)")
                            }
                        } else {
                        cardBody("shape3d_title", action: { shapes3D = true }) { Shape3DGlyph(kind: 0).frame(width: 30, height: 30) }
                        card("scene_workspace", glyph: CupertinoGlyph.Cube, accent: true) { close(); model.enterSceneEditor() }
                        card("sh_add_model_3d", glyph: CupertinoGlyph.Cube, accent: true) { files(.model) }
                        card("sh_add_text_3d", glyph: ShellGlyph.TextformatAlt, color: ShellColors.text3D) { model.addText3D(content: AureaText.t("panel_texto"), depth: 0.25); close() }
                        card("scene_floor_reflective", glyph: CupertinoGlyph.Sparkles, color: ShellColors.text3D) { model.addReflectiveFloor(); close() }
                        card("panel_camera_3d", glyph: CupertinoGlyph.CameraFill, accent: true) { model.addCamera(); close() }
                        card("real_camera_title", glyph: CupertinoGlyph.CameraFill, accent: true) { model.startRealCamera(); close() }
                        drawnCard("sh_add_null_3d", kind: -1) { model.addNull(threeD: true); close() }
                        }
                    case 6:
                        card("editor_mao_livre", glyph: ShellGlyph.Scribble, accent: true) { close(); model.addVector(0, freehand: true) }
                    default:
                        drawnCard("editor_desenhar_pontos", kind: 0) { model.addVector(0) }
                        drawnCard("editor_retangulo", kind: 1) { model.addVector(1) }
                        drawnCard("editor_elipse", kind: 2) { model.addVector(2) }
                        drawnCard("editor_poligono", kind: 3) { model.addVector(3) }
                        drawnCard("editor_estrela", kind: 4) { model.addVector(4) }
                        card("editor_importar_svg", glyph: CupertinoGlyph.DocText) { files(.svg) }
                        card("psd_import", glyph: CupertinoGlyph.PhotoOnRectangle) { files(.psd) }
                    }
                }
                if let hint {
                    Text(AureaText.t(hint)).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                        .padding(.horizontal, 4).padding(.vertical, 2)
                }
                if tab == 5 && !shapes3D {
                    Text(AureaText.t("model3d_risk_warning")).font(.aurea(size: 12))
                        .foregroundStyle(AureaColors.warning).fixedSize(horizontal: false, vertical: true)
                        .padding(.horizontal, 4).padding(.vertical, 2)
                        .accessibilityIdentifier("model3d.risk.warning")
                }
            }.padding(.horizontal, 12).padding(.vertical, 10)
                .background(GeometryReader { geometry in
                    Color.clear.preference(key: AddCardsHeightKey.self, value: geometry.size.height)
                })
        }.frame(height: min(cardContentHeight, maxCardHeight))
            .onPreferenceChange(AddCardsHeightKey.self) { cardContentHeight = $0 }

    }
    private var hint: String? {
        switch tab {
        case 5: return shapes3D ? "shape3d_hint" : "sh_add_model_3d_hint"
        case 6: return "editor_desenhe_dedo_direto_palco_cada_traco"
        case 7: return "editor_vetor_contorno_pontos_voce_arrasta_curva"
        default: return nil
        }
    }
    private func close() { model.showAddLayer = false }
    /// "Gravar voz": fecha o diálogo e abre o painel de gravação pela raiz do
    /// editor (dentro de uma folha, depois que ela fecha).
    private func openVoiceRecorder() {
        close()
        model.pause()
        let shell = shell
        if inSheet {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.45) { shell.voiceRecorder = true }
        } else {
            shell.voiceRecorder = true
        }
    }
    private func photos(_ kind: ShellAddPicker) { request(kind) }
    private func files(_ kind: ShellAddPicker) { request(kind) }
    /// Sequência de imagens: a escolha pasta × quadros mora na raiz (AddLayerPickers).
    private func openSequenceChooser() {
        close()
        model.pendingOverlayImport = false
        let shell = shell
        if inSheet {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.45) { shell.sequenceChooser = true }
        } else {
            shell.sequenceChooser = true
        }
    }
    /// Fecha o diálogo e pede o seletor à raiz do editor (que continua na tela
    /// durante todo o abrir/fechar do seletor). Dentro de uma folha, espera ela
    /// fechar: duas apresentações ao mesmo tempo, o sistema recusa a segunda.
    private func request(_ kind: ShellAddPicker) {
        close()
        if inSheet {
            let shell = shell
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.45) { shell.addPicker = kind }
        } else {
            shell.addPicker = kind
        }
    }
    private func card(_ label: String, glyph: Character, accent: Bool = false, color: Color? = nil, action: @escaping () -> Void) -> some View {
        cardBody(label, action: action) { CupertinoGlyph.text(glyph, size: 28, color: color ?? (accent ? AureaColors.accent : StageInk.dockTileContent)).mirrorsInRtl(CupertinoGlyph.mirrorsInRtl(glyph)).frame(width: 30, height: 30) }
    }
    private func drawnCard(_ label: String, kind: Int, action: @escaping () -> Void) -> some View {
        cardBody(label, action: action) {
            Group { if kind < 0 { ShellNullGlyph() } else { ShellVectorGlyph(kind: kind) } }.frame(width: 30, height: 30)
        }
    }
    private func cardBody<Icon: View>(_ label: String, action: @escaping () -> Void, @ViewBuilder icon: () -> Icon) -> some View {
        Button(action: action) {
            VStack(spacing: 6) {
                icon()
                Text(AureaText.t(label)).font(.aurea(size: 12, weight: .medium)).lineLimit(2).multilineTextAlignment(.center)
                    .foregroundStyle(StageInk.dockTileContent)
            }.padding(.horizontal, 4).padding(.vertical, 8).frame(maxWidth: .infinity).frame(height: StageDim.addCardHeight)
                .background(StageInk.dockTile, in: RoundedRectangle(cornerRadius: 12))
        }.buttonStyle(.plain).accessibilityLabel(AureaText.t(label))
    }
}

/// Seletor de fotos do sistema, equivalente ao Photo Picker do Android.
/// `loadFileRepresentation` copia o arquivo temporário sem carregar um vídeo
/// inteiro em Data no processo da interface.
private final class EditorPhotoLibrary: NSObject, ObservableObject, PHPhotoLibraryChangeObserver {
    @Published var assets: PHFetchResult<PHAsset>?
    @Published var status = PHPhotoLibrary.authorizationStatus(for: .readWrite)
    @Published var loading = false
    private var video = false
    private var generation = 0
    private var requestingAuthorization = false
    override init() { super.init(); PHPhotoLibrary.shared().register(self) }
    deinit { PHPhotoLibrary.shared().unregisterChangeObserver(self) }
    func photoLibraryDidChange(_ changeInstance: PHChange) { DispatchQueue.main.async { [weak self] in self?.reload() } }
    func show(video: Bool) { self.video = video; reload() }
    func reload() {
        generation += 1
        let request = generation, isVideo = video
        status = PHPhotoLibrary.authorizationStatus(for: .readWrite)
        if status == .notDetermined {
            loading = true
            guard !requestingAuthorization else { return }
            requestingAuthorization = true
            PHPhotoLibrary.requestAuthorization(for: .readWrite) { [weak self] _ in DispatchQueue.main.async {
                self?.requestingAuthorization = false
                self?.reload()
            } }
            return
        }
        assets = nil
        guard status == .authorized || status == .limited else { loading = false; return }
        loading = true
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            let options = PHFetchOptions()
            options.sortDescriptors = [NSSortDescriptor(key: "creationDate", ascending: false)]
            let result = PHAsset.fetchAssets(with: isVideo ? .video : .image, options: options)
            DispatchQueue.main.async {
                guard let self, self.generation == request else { return }
                self.assets = result; self.loading = false
            }
        }
    }
    func manage() {
        if status == .limited {
            let scenes = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }
            guard var presenter = scenes.flatMap(\.windows).first(where: \.isKeyWindow)?.rootViewController else { return }
            while let next = presenter.presentedViewController { presenter = next }
            PHPhotoLibrary.shared().presentLimitedLibraryPicker(from: presenter)
        } else if let url = URL(string: UIApplication.openSettingsURLString) { UIApplication.shared.open(url) }
    }
}

private struct EditorMediaGallery: View {
    @EnvironmentObject private var model: AureaModel
    @StateObject private var library = EditorPhotoLibrary()
    @Environment(\.scenePhase) private var phase
    @State private var video = false
    @State private var importing = false
    @State private var failure = false
    @State private var importRequest = UUID()
    @State private var selecting = false
    @State private var selectedAssets: [PHAsset] = []
    let openFiles: (Bool) -> Void
    let openAI: () -> Void
    let picked: ([(URL, Bool)]) -> Void
    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 0) {
                ForEach([false, true], id: \.self) { mode in
                    Button { video = mode } label: {
                        Text(AureaText.t(mode ? "editor_video" : "editor_foto")).font(.aurea(size: 14, weight: .semibold))
                            .foregroundStyle(video == mode ? AureaColors.accent : AureaColors.muted)
                            .frame(maxWidth: .infinity, minHeight: 48)
                    }.buttonStyle(.plain).accessibilityIdentifier(mode ? "gallery.videos" : "gallery.photos")
                }
                ShellBarButton(glyph: CupertinoGlyph.Folder, description: AureaText.t("gallery_files"), size: 20, width: 48, height: 48) { openFiles(video) }
                ShellBarButton(glyph: CupertinoGlyph.WandStars, description: AureaText.t("sh_add_ai_video"), size: 20, width: 48, height: 48, action: openAI)
            }
            if library.status == .limited {
                HStack {
                    Text(AureaText.t("gallery_limited")).font(.aurea(size: 11)).foregroundStyle(AureaColors.muted)
                    Spacer()
                    Button(AureaText.t("gallery_manage")) { library.manage() }.frame(minHeight: 44)
                }.padding(.horizontal, 12)
            }
            Text(AureaText.t("gallery_recent")).font(.aurea(size: 11)).foregroundStyle(AureaColors.muted)
                .frame(maxWidth: .infinity, alignment: .leading).padding(.leading, 12).padding(.bottom, 6)
            HStack {
                Button(AureaText.t(selecting ? "editor_cancelar" : "beta_select_media")) {
                    selecting.toggle(); if !selecting { selectedAssets.removeAll() }
                }.frame(minHeight: 48).accessibilityIdentifier("gallery.select")
                Spacer()
                if selecting {
                    Button(AureaText.t("beta_add_media", selectedAssets.count)) { importAssets(selectedAssets) }
                        .frame(minHeight: 48).disabled(selectedAssets.isEmpty).accessibilityIdentifier("gallery.addSelected")
                }
            }.padding(.horizontal, 12)
            ZStack {
                if library.loading || importing { ProgressView().tint(AureaColors.accent) }
                else if library.status != .authorized && library.status != .limited {
                    VStack {
                        Text(AureaText.t("gallery_access")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                        Button(AureaText.t("gallery_allow")) { library.manage() }.frame(minHeight: 44)
                    }.padding(.horizontal, 16)
                } else if failure {
                    Button(AureaText.t("gallery_error")) { failure = false; library.reload() }.frame(minHeight: 44)
                } else if let assets = library.assets, assets.count > 0 {
                    ScrollView {
                        LazyVGrid(columns: [GridItem(.adaptive(minimum: 88), spacing: 3)], spacing: 3) {
                            ForEach(0..<assets.count, id: \.self) { index in
                                let asset = assets.object(at: index)
                                let chosen = selectedAssets.firstIndex { $0.localIdentifier == asset.localIdentifier }
                                Button {
                                    if selecting {
                                        if let chosen { selectedAssets.remove(at: chosen) } else { selectedAssets.append(asset) }
                                    } else { importAssets([asset]) }
                                } label: {
                                    EditorPhotoThumbnail(asset: asset).overlay(alignment: .topTrailing) {
                                        if let chosen { Text("✓ \(chosen + 1)").padding(6).background(AureaColors.chip).foregroundStyle(AureaColors.accent) }
                                    }
                                }
                                    .buttonStyle(.plain).id(asset.localIdentifier)
                                    .accessibilityAddTraits(chosen == nil ? [] : [.isSelected])
                                    .accessibilityLabel(AureaText.t(video ? "editor_video" : "editor_foto") + " " + (asset.creationDate?.formatted(date: .abbreviated, time: .shortened) ?? ""))
                            }
                        }.padding(3)
                    }.accessibilityIdentifier("gallery.grid")
                } else { Text(AureaText.t("gallery_empty")).foregroundStyle(AureaColors.muted).font(.aurea(size: 13)) }
            }.frame(maxWidth: .infinity, maxHeight: .infinity)
        }.tint(AureaColors.accent).accessibilityIdentifier("gallery.panel")
            .onAppear { library.show(video: video) }
            .onChange(of: video) { library.show(video: $0) }
            .onChange(of: phase) { if $0 == .active { library.reload() } }
            .onDisappear { importRequest = UUID() }
            .disabled(importing)
    }
    private func importAssets(_ assets: [PHAsset]) {
        guard !importing, !assets.isEmpty else { return }
        importRequest = UUID(); importing = true; failure = false
        prepareAsset(assets, index: 0, ready: [], request: importRequest, project: model.projectGeneration)
    }
    private func prepareAsset(_ assets: [PHAsset], index: Int, ready: [(URL, Bool)], request: UUID, project: UUID) {
        guard importRequest == request, model.projectGeneration == project else { return }
        if index == assets.count { importing = false; if !ready.isEmpty { picked(ready) }; return }
        let asset = assets[index]
        let isVideo = asset.mediaType == .video
        let resources = PHAssetResource.assetResources(for: asset)
        let primary: PHAssetResourceType = isVideo ? .fullSizeVideo : .fullSizePhoto
        let fallback: PHAssetResourceType = isVideo ? .video : .photo
        guard let resource = resources.first(where: { $0.type == primary }) ?? resources.first(where: { $0.type == fallback }) else {
            failure = true; prepareAsset(assets, index: index + 1, ready: ready, request: request, project: project); return
        }
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        do { try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true) }
        catch { failure = true; prepareAsset(assets, index: index + 1, ready: ready, request: request, project: project); return }
        let destination = folder.appendingPathComponent(resource.originalFilename)
        let options = PHAssetResourceRequestOptions(); options.isNetworkAccessAllowed = true
        PHAssetResourceManager.default().writeData(for: resource, toFile: destination, options: options) { error in
            DispatchQueue.main.async {
                guard importRequest == request, model.projectGeneration == project else {
                    try? FileManager.default.removeItem(at: folder)
                    return
                }
                var next = ready
                if error != nil { try? FileManager.default.removeItem(at: folder); failure = true }
                else { next.append((destination, isVideo)) }
                prepareAsset(assets, index: index + 1, ready: next, request: request, project: project)
            }
        }
    }
}

private struct EditorPhotoThumbnail: View {
    let asset: PHAsset
    @State private var image: UIImage?
    @State private var request = PHInvalidImageRequestID
    var body: some View {
        Color.clear.aspectRatio(1, contentMode: .fit).overlay {
            if let image { Image(uiImage: image).resizable().scaledToFill() }
            else { AureaColors.chip.overlay { ProgressView().tint(AureaColors.muted) } }
        }.clipped().overlay(alignment: .bottomTrailing) {
            if asset.mediaType == .video {
                let seconds = max(0, Int(asset.duration))
                Text(String(format: "%d:%02d", seconds / 60, seconds % 60)).font(.aurea(size: 11)).foregroundStyle(.white)
                    .padding(4).background(.black.opacity(0.65))
            }
        }.onAppear {
            let options = PHImageRequestOptions(); options.isNetworkAccessAllowed = true; options.resizeMode = .fast
            request = PHImageManager.default().requestImage(for: asset, targetSize: CGSize(width: 256, height: 256), contentMode: .aspectFill, options: options) { result, _ in
                DispatchQueue.main.async { image = result }
            }
        }.onDisappear { PHImageManager.default().cancelImageRequest(request) }
    }
}

struct ShellMediaPicker: UIViewControllerRepresentable {
    var selectionLimit = 0
    let filter: PHPickerFilter
    let picked: ([(URL, Bool)]) -> Void
    func makeCoordinator() -> Coordinator { Coordinator(picked: picked) }
    func makeUIViewController(context: Context) -> PHPickerViewController {
        var configuration = PHPickerConfiguration()
        configuration.selectionLimit = selectionLimit; configuration.filter = filter
        configuration.selection = .ordered
        configuration.preferredAssetRepresentationMode = .current
        let picker = PHPickerViewController(configuration: configuration)
        picker.delegate = context.coordinator; return picker
    }
    func updateUIViewController(_ controller: PHPickerViewController, context: Context) {}
    final class Coordinator: NSObject, PHPickerViewControllerDelegate {
        let picked: ([(URL, Bool)]) -> Void
        init(picked: @escaping ([(URL, Bool)]) -> Void) { self.picked = picked }
        func picker(_ picker: PHPickerViewController, didFinishPicking results: [PHPickerResult]) {
            load(results, index: 0, ready: [])
        }
        private func load(_ results: [PHPickerResult], index: Int, ready: [(URL, Bool)]) {
            guard index < results.count else { DispatchQueue.main.async { self.picked(ready) }; return }
            let provider = results[index].itemProvider
            let video = provider.hasItemConformingToTypeIdentifier(UTType.movie.identifier)
            let type = video ? UTType.movie.identifier : UTType.image.identifier
            provider.loadFileRepresentation(forTypeIdentifier: type) { source, _ in
                var next = ready
                if let source {
                    let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
                    do {
                        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
                        let target = directory.appendingPathComponent(source.lastPathComponent)
                        try FileManager.default.copyItem(at: source, to: target)
                        next.append((target, video))
                    } catch { try? FileManager.default.removeItem(at: directory) }
                }
                self.load(results, index: index + 1, ready: next)
            }
        }
    }
}

/// Cena 3D com as cores do AUREA (par do SceneLayoutWorkspace.kt): cabeçalho, duas
/// viewports (edição e prévia limpa, redimensionáveis) e, embaixo, o painel de animação
/// com os ossos, a timeline em segundos e o gráfico da curva. Em pé as viewports empilham,
/// a barra do painel usa duas linhas e o gráfico fica embaixo da timeline. Gestos:
/// `sceneEvent` e `rigPoseEvent` no PreviewMetalView.swift.
@MainActor private struct SceneLayoutWorkspace: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    let height: CGFloat
    let width: CGFloat
    @State private var materials = false
    @State private var lights = false
    @State private var curve = false
    @State private var area = 0
    @State private var addOpen = false
    @State private var preview = true
    /// A divisão que a pessoa escolheu; sem escolha, segue a tela.
    @State private var splitChoice: Bool?
    @State private var split: CGFloat = 0.5
    @State private var splitStart: CGFloat?
    @State private var fullscreen = false
    private static let skeletonArea = 3
    private var isModel: Bool { model.selectedLayer?.kind == 10 }
    private var wide: Bool { width >= 600 }
    private var sideBySide: Bool { splitChoice ?? wide }
    private var bottomHeight: CGFloat { wide ? 12 + 40 + (area == 0 ? 116 : 150) : 12 + 80 + 270 }
    private var viewportsHeight: CGFloat { max(120, height - 36 - (fullscreen ? 6 : bottomHeight)) }
    private func chooseArea(_ i: Int) {
        area = i
        model.openSkeletonPanel(i == Self.skeletonArea)
    }
    var body: some View {
        VStack(spacing: 0) {
            header
            viewports.frame(height: viewportsHeight).padding(.horizontal, 8)
            if !fullscreen {
                Scene3DStudioBottom(area: area, wide: wide, isModel: isModel, onArea: chooseArea, onAdd: { addOpen.toggle() },
                                    openMaterial: openMaterial, openLights: { lights = true }, openCurve: { curve = true },
                                    more: AnyView(Group { moreMenu; Divider(); selectMenu })).environmentObject(model)
                    .frame(height: bottomHeight)
            }
        }
        .frame(height: height)
        .background(AureaColors.background)
        // Deitado a altura é pouca: as barras do sistema saem para dar espaço às viewports.
        .statusBarHidden(wide)
        .persistentSystemOverlays(wide ? .hidden : .automatic)
        .onChange(of: wide) { _ in splitChoice = nil }
        .sheet(isPresented: $model.showAddLayer) { AddLayerSheet(tab: 5, inSheet: true).environmentObject(model).environmentObject(shell) }
        // Material e luz em folha de meia altura (0.3.1): a cena fica à vista em cima.
        .sheet(isPresented: $lights) {
            SceneLightControls { lights = false }.environmentObject(model)
                .presentationDetents([.medium, .large])
        }
        .sheet(isPresented: $materials) {
            // O cabeçalho do Panel3DView já tem título e ‹ (fecha: panel = .none).
            Panel3DView().environmentObject(model)
                .presentationDetents([.medium, .large])
        }
        .sheet(isPresented: $curve, onDismiss: { if model.panel == .curve { model.openPanel(.none) } }) {
            CurvePanel().environmentObject(model)
                .presentationDetents([.large])
        }
        .onChange(of: model.panel) { panel in if panel == .none { materials = false; curve = false } }
        // O painel 3D e os menus abrem o Esqueleto: ele vira a área de baixo.
        .onChange(of: model.skeletonPanel) { on in if on { area = Self.skeletonArea } }
        .onChange(of: isModel) { now in if !now && area == Self.skeletonArea { area = 0 } }
    }
    private func openMaterial() { model.panel = .layer3D; materials = true }

    /// Voltar, o título, tela cheia e fechar.
    private var header: some View {
        HStack(spacing: 0) {
            headerIcon(CupertinoGlyph.ChevronLeft, AureaText.t("editor_voltar_editor")) { model.exitSceneEditor() }
            CupertinoGlyph.text(CupertinoGlyph.Cube, size: 20, color: AureaColors.text).padding(.leading, 8)
            Text(AureaText.t("s3d_scene_title")).font(.aurea(size: 16, weight: .semibold)).foregroundStyle(AureaColors.text)
                .lineLimit(1).padding(.leading, 10)
            Spacer(minLength: 0)
            headerIcon(fullscreen ? CupertinoGlyph.FullscreenExit : CupertinoGlyph.Fullscreen, AureaText.t("s3d_fullscreen")) { fullscreen.toggle() }
            headerIcon(CupertinoGlyph.Xmark, AureaText.t("editor_voltar_editor")) { model.exitSceneEditor() }
        }.padding(.horizontal, 6).frame(height: 36)
            .environment(\.layoutDirection, .leftToRight)
    }
    private func headerIcon(_ glyph: Character, _ label: String, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            CupertinoGlyph.text(glyph, size: 18).foregroundStyle(AureaColors.text).frame(width: 40, height: 36).contentShape(Rectangle())
        }.buttonStyle(.plain).accessibilityLabel(label)
    }

    // --- As duas viewports ---------------------------------------------------
    // AnyLayout: trocar a divisão ou esconder a prévia não recria o palco.
    private var viewports: some View {
        GeometryReader { geo in
            let bar: CGFloat = preview ? 14 : 0
            let total: CGFloat = (sideBySide ? geo.size.width : geo.size.height) - bar
            let first: CGFloat = preview ? (total * split).rounded() : total
            let second: CGFloat = max(0, total - first)
            let layout = sideBySide ? AnyLayout(HStackLayout(spacing: 0)) : AnyLayout(VStackLayout(spacing: 0))
            layout {
                editViewport(width: sideBySide ? first : geo.size.width, height: sideBySide ? geo.size.height : first)
                if preview {
                    splitBar(total: total).frame(width: sideBySide ? bar : geo.size.width, height: sideBySide ? geo.size.height : bar)
                    previewViewport(width: sideBySide ? second : geo.size.width, height: sideBySide ? geo.size.height : second)
                }
            }
        }
    }
    private func editViewport(width: CGFloat, height: CGFloat) -> some View {
        ZStack(alignment: .topLeading) {
            PreviewStage(height: height)
            Menu {
                modeMenu
                Divider()
                viewMenu
                Divider()
                Button(AureaText.t(preview ? "s3d_preview_hide" : "s3d_preview_show")) { preview.toggle() }
            } label: {
                Scene3DPill(label: AureaText.t("s3d_viewport_edit"), glyph: nil)
            }.padding(6).accessibilityIdentifier("scene.viewport")
            Scene3DToolbar(height: height - 50).environmentObject(model).padding(.top, 44)
            if model.rigFixMode && !model.humanoidJoints.isEmpty {
                rigFixBar.frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottom)
            }
            if addOpen {
                Scene3DStudioAddMenu(onClose: { addOpen = false }).environmentObject(model)
                    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
            }
        }
        .frame(width: width, height: height)
        .clipShape(RoundedRectangle(cornerRadius: 12))
        .environment(\.layoutDirection, .leftToRight)   // a cena 3D e os gizmos não espelham
    }
    private func previewViewport(width: CGFloat, height: CGFloat) -> some View {
        ZStack(alignment: .topLeading) {
            Scene3DPreviewPane().environmentObject(model)
            Menu {
                Button(AureaText.t("s3d_preview_hide")) { preview = false }
                Button(AureaText.t("s3d_split_switch")) { splitChoice = !sideBySide }
            } label: {
                Scene3DPill(label: AureaText.t("s3d_preview"), glyph: CupertinoGlyph.Eye)
            }.padding(6).accessibilityIdentifier("scene.preview.menu")
        }
        .frame(width: width, height: height)
        .clipShape(RoundedRectangle(cornerRadius: 12))
        .environment(\.layoutDirection, .leftToRight)
    }
    private func splitBar(total: CGFloat) -> some View {
        ZStack {
            AureaColors.editorBar
            RoundedRectangle(cornerRadius: 2).fill(AureaColors.muted)
                .frame(width: sideBySide ? 4 : 40, height: sideBySide ? 40 : 4)
        }
        .contentShape(Rectangle())
        .gesture(DragGesture(minimumDistance: 0, coordinateSpace: .global).onChanged { g in
            let start = splitStart ?? split
            splitStart = start
            let moved = sideBySide ? g.translation.width : g.translation.height
            split = min(0.8, max(0.25, start + moved / max(1, total)))
        }.onEnded { _ in splitStart = nil })
        .accessibilityLabel(AureaText.t("s3d_split_drag")).accessibilityIdentifier("scene.split")
    }

    // --- Os menus ------------------------------------------------------------
    @ViewBuilder private var modeMenu: some View {
        let canPose = isModel && !model.poseBones.isEmpty
        checked(AureaText.t("s3d_mode_object"), !model.rigPoseMode) { if model.rigPoseMode { model.setRigPose(false) } }
        checked(AureaText.t("s3d_mode_pose") + (canPose ? "" : " — " + AureaText.t("s3d_no_rig")), model.rigPoseMode) { model.setRigPose(true) }
            .disabled(!canPose)
        Button(AureaText.t(model.gizmoLocalSpace ? "s3d_space_local" : "s3d_space_global")) { model.gizmoLocalSpace.toggle() }
    }
    @ViewBuilder private var moreMenu: some View {
        if model.rigPoseActive { poseMenu } else { objectMenu }
    }
    private func checked(_ label: String, _ on: Bool, _ action: @escaping () -> Void) -> some View {
        Button(action: action) { if on { Label(label, systemImage: "checkmark") } else { Text(label) } }
    }
    @ViewBuilder private var viewMenu: some View {
        Button(AureaText.t("s3d_view_frame")) { Scene3DNav.frameAll(model) }
        checked(AureaText.t("s3d_view_camera"), model.sceneCameraView) { model.toggleSceneCameraView() }
        Divider()
        Button(AureaText.t("om_view_front")) { model.snapSceneView(yaw: 0, pitch: 0) }
        Button(AureaText.t("om_view_back")) { model.snapSceneView(yaw: 180, pitch: 0) }
        Button(AureaText.t("om_view_left")) { model.snapSceneView(yaw: -90, pitch: 0) }
        Button(AureaText.t("om_view_right")) { model.snapSceneView(yaw: 90, pitch: 0) }
        Button(AureaText.t("om_view_top")) { model.snapSceneView(yaw: model.sceneYaw, pitch: 80) }
        Button(AureaText.t("om_view_bottom")) { model.snapSceneView(yaw: model.sceneYaw, pitch: -80) }
        Divider()
        checked(AureaText.t("s3d_overlays"), !ViewModeState.shared.hideOverlay) { ViewModeState.shared.hideOverlay.toggle() }
        if model.rigPoseActive {
            checked(AureaText.t("skeleton_show"), !model.skeletonHidden) { model.skeletonHidden.toggle() }
        }
    }
    @ViewBuilder private var selectMenu: some View {
        if model.rigPoseActive {
            Button(AureaText.t("s3d_select_none")) { model.choosePoseBone(-1) }.disabled(model.poseBone < 0)
            Button(AureaText.t("skeleton_bones")) { chooseArea(Self.skeletonArea) }
        } else {
            Button(AureaText.t("s3d_select_all")) { model.selectAll() }
            Button(AureaText.t("s3d_select_none")) { model.clearSelection() }.disabled(model.selection.isEmpty)
            Divider()
            ForEach(model.layers.filter { $0.threeD }) { o in
                checked(o.name, o.id == model.primarySelection) { model.select(layerId: o.id, additive: false, openOptions: false) }
            }
        }
    }
    @ViewBuilder private var poseMenu: some View {
        Button(AureaText.t("rig_key_bone")) { model.togglePoseKey(all: false) }.disabled(model.poseBone < 0)
        Button(AureaText.t("rig_key_all")) { model.togglePoseKey(all: true) }
        Divider()
        Button(AureaText.t("skeleton_copy")) { model.copyPose() }
        Button(AureaText.t("skeleton_paste")) { model.pastePose() }.disabled(model.poseClipboard.isEmpty)
        Button(AureaText.t("skeleton_mirror_pose")) { model.mirrorPose(all: true) }
        Divider()
        Button(AureaText.t("skeleton_zero_pose")) { model.zeroPose(all: true) }
        Button(AureaText.t("rig_reset_all")) { model.resetPose(all: true) }
    }
    @ViewBuilder private var objectMenu: some View {
        let selected = model.selectedLayer
        Button(AureaText.t("s3d_insert_key_desc")) { model.toggleSceneKey() }.disabled(selected == nil)
        Button(AureaText.t("s3d_duplicate")) {
            model.engine.duplicateLayers(model.selection.map { NSNumber(value: $0) }); model.refreshModel(force: true)
        }.disabled(selected == nil)
        if isModel || (selected?.kind == 4 && selected?.threeD == true) {
            Divider()
            Button(AureaText.t("scene_material")) { openMaterial() }
        }
        if selected?.kind == 9 {
            Divider()
            Button(AureaText.t("pn_t3d_lighting")) { lights = true }
        }
        if isModel {
            Divider()
            Button(AureaText.t("skeleton_title")) { chooseArea(Self.skeletonArea) }
            Button(AureaText.t("mocap_title")) { Panel3DRequest.tab = .rig; openMaterial() }
        }
        Divider()
        if let selected {
            Button(AureaText.t("s3d_delete_named", selected.name), role: .destructive) {
                model.select(layerId: selected.id, additive: false, openOptions: false); model.deleteSelectedLayers()
            }
        }
    }

    /// Barra do Corrigir esqueleto (RigFixBar do SceneLayoutWorkspace.kt): dica, progresso dos pesos e concluir.
    private var rigFixBar: some View {
        let working = model.autoRigState == 1
        let text = working ? AureaText.t("rig3d_working", min(100, max(0, Int(model.autoRigProgress * 100)))) : AureaText.t("rig3d_fix_hint")
        return VStack(spacing: 8) {
            Text(text).font(.aurea(size: 12)).foregroundStyle(AureaColors.text)
                .multilineTextAlignment(.center).padding(.horizontal, 12).padding(.vertical, 8)
                .background(StageInk.floatingDark, in: RoundedRectangle(cornerRadius: 12)).padding(.horizontal, 24)
            HStack(spacing: 8) {
                if working { Button { model.cancelAutoRig() } label: { pill(CupertinoGlyph.ArrowCounterclockwise, AureaText.t("rig3d_cancel")) }.buttonStyle(.plain) }
                Button { model.setRigFix(false) } label: { pill(CupertinoGlyph.CheckmarkAlt, AureaText.t("rig_done")) }.buttonStyle(.plain)
            }.padding(.horizontal, 12)
        }.padding(.bottom, 12)
    }
    private func pill(_ glyph: Character, _ label: String) -> some View {
        HStack(spacing: 6) {
            CupertinoGlyph.text(glyph, size: 16)
            Text(label).font(.aurea(size: 13, weight: .medium)).lineLimit(1)
        }.foregroundStyle(AureaColors.text).padding(.horizontal, 14).frame(height: 40)
            .background(StageInk.floatingDark, in: Capsule())
    }
}

// =============================================================================
// A viewport da Cena 3D (par do Scene3DViewport.kt): ferramentas à esquerda, gizmo de
// eixos e navegação à direita e o nome do que está escolhido no canto.
// =============================================================================

private enum Scene3DNav {
    static let fill = Color(hex: 0xE6151C24)
    static let tool: CGFloat = 34
    /// Vista inicial e sem zoom/pan do palco.
    @MainActor static func frameAll(_ model: AureaModel) {
        StageViewZoom.shared.reset(engine: model.engine)
        model.resetSceneView()
    }
}

extension AureaModel {
    /// Há chave de transformação da camada escolhida no cabeçote?
    var sceneKeyHere: Bool {
        guard let id = primarySelection, let d = engine.layerDetail(id) else { return false }
        let keyMask = (d["keyAtPlayhead"] as? NSNumber)?.uint32Value ?? 0
        return (0..<9).contains { keyMask & (1 << UInt32($0)) != 0 }
    }
    /// Marca/desmarca as 9 trilhas de posição, rotação e escala no quadro (ou o osso, na pose).
    func toggleSceneKey() {
        if rigPoseMode && !poseBones.isEmpty { togglePoseKey(all: poseBone < 0); return }
        toggleObjectKey()
    }
    /// As chaves do mesmo instante andam juntas: um passo de desfazer.
    func moveKeysTogether(layer: Int64, keys: [KeyframeItem], delta: Int32) {
        guard !keys.isEmpty, delta != 0 else { return }
        beginGesture("mover keyframes")
        mutate { core in
            for k in keys {
                core.moveKeyframe(forLayer: layer, property: k.property, effectIndex: k.effectIndex, paramIndex: k.paramIndex, from: k.time, to: k.time + delta)
            }
        }
        endGesture()
        refreshModel(force: true)
    }
    func deleteKeysTogether(layer: Int64, keys: [KeyframeItem]) {
        guard !keys.isEmpty else { return }
        beginGesture("apagar keyframes")
        mutate { core in
            for k in keys {
                core.deleteKeyframe(forLayer: layer, property: k.property, effectIndex: k.effectIndex, paramIndex: k.paramIndex, time: k.time)
            }
        }
        endGesture()
        refreshModel(force: true)
    }
    /// A chave do objeto escolhido, mesmo no modo pose.
    func toggleObjectKey() {
        guard let id = primarySelection, let d = engine.layerDetail(id) else { return }
        let any = sceneKeyHere
        let values = ["position", "scale", "rotation"].flatMap { (d[$0] as? [NSNumber] ?? []).prefix(3).map(\.floatValue) }
        let local = localPlayhead
        beginGesture("chave da cena 3d")
        mutate { core in
            for p in 0..<9 {
                // Ordem das trilhas: posição 0-2, escala 3-5, rotação 6-8 (TrackProperty).
                if any { core.deleteKeyframe(forLayer: id, property: UInt32(p), time: local) }
                else if p < values.count { core.insertKeyframe(forLayer: id, property: UInt32(p), time: local, value: values[p]) }
            }
        }
        endGesture()
        refreshModel(force: true)
    }
}

/// Barra da esquerda: as ferramentas e os dois modos. Sem altura para todas, abre outra coluna.
@MainActor private struct Scene3DToolbar: View {
    @EnvironmentObject private var model: AureaModel
    let height: CGFloat
    private var poseTool: Int {
        if model.sceneSelectOnly { return AureaModel.poseToolSelect }
        if model.poseIk { return AureaModel.poseToolIk }
        return model.poseMove ? AureaModel.poseToolMove : AureaModel.poseToolRotate
    }
    private var objectTool: Int { model.sceneSelectOnly ? AureaModel.sceneToolSelect : model.gizmoTool }
    private static let count = 6
    var body: some View {
        let size = Scene3DNav.tool
        let rows = max(1, min(Self.count, Int((height - 4) / (size + 2))))
        let columns = (Self.count + rows - 1) / rows
        LazyHGrid(rows: Array(repeating: GridItem(.fixed(size), spacing: 2), count: rows), spacing: 2) {
            if model.rigPoseActive {
                selectTool(poseTool == AureaModel.poseToolSelect) { model.choosePoseTool(AureaModel.poseToolSelect) }
                tool(CupertinoGlyph.ArrowCounterclockwise, "gizmo_tool_rotate", "scene.tool.rotate", on: poseTool == AureaModel.poseToolRotate) { model.choosePoseTool(AureaModel.poseToolRotate) }
                tool(CupertinoGlyph.Move, "rig3d_move", "scene.tool.move", on: poseTool == AureaModel.poseToolMove) { model.choosePoseTool(AureaModel.poseToolMove) }
                tool(CupertinoGlyph.Link, "skeleton_ik", "scene.tool.ik", on: poseTool == AureaModel.poseToolIk) { model.choosePoseTool(AureaModel.poseToolIk) }
            } else {
                selectTool(objectTool == AureaModel.sceneToolSelect) { model.chooseSceneTool(AureaModel.sceneToolSelect) }
                tool(CupertinoGlyph.Move, "gizmo_tool_move", "scene.tool.move", on: objectTool == 0) { model.chooseSceneTool(0) }
                tool(CupertinoGlyph.ArrowCounterclockwise, "gizmo_tool_rotate", "scene.tool.rotate", on: objectTool == 1) { model.chooseSceneTool(1) }
                tool(CupertinoGlyph.ArrowDownRightSquare, "gizmo_tool_scale", "scene.tool.scale", on: objectTool == 2) { model.chooseSceneTool(2) }
            }
            mode(CupertinoGlyph.Cube, "s3d_mode_object", "scene.mode.object", active: !model.rigPoseActive) {
                if model.rigPoseMode { model.setRigPose(false) }
            }
            mode(CupertinoGlyph.PersonCropCircle, "s3d_mode_pose", "scene.mode.pose", active: model.rigPoseActive) {
                if !model.rigPoseMode && !model.poseBones.isEmpty && model.selectedLayer?.kind == 10 { model.setRigPose(true) }
            }
        }
        .frame(width: CGFloat(columns) * size + CGFloat(columns - 1) * 2, height: CGFloat(rows) * size + CGFloat(rows - 1) * 2)
        .padding(3).background(Scene3DNav.fill, in: RoundedRectangle(cornerRadius: 10))
        .padding(.leading, 6)
    }
    private func mode(_ glyph: Character, _ key: String, _ id: String, active: Bool, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            CupertinoGlyph.text(glyph, size: 18, color: active ? AureaColors.text : AureaColors.muted)
                .frame(width: Scene3DNav.tool, height: Scene3DNav.tool).contentShape(Rectangle())
        }.buttonStyle(.plain).accessibilityLabel(AureaText.t(key)).accessibilityAddTraits(active ? [.isSelected] : []).accessibilityIdentifier(id)
    }
    private func tool(_ glyph: Character, _ key: String, _ id: String, on: Bool, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            CupertinoGlyph.text(glyph, size: 18, color: on ? AureaColors.onAction : AureaColors.text)
                .frame(width: Scene3DNav.tool, height: Scene3DNav.tool)
                .background(on ? AureaColors.brand : Color.clear, in: RoundedRectangle(cornerRadius: 9)).contentShape(Rectangle())
        }.buttonStyle(.plain).accessibilityLabel(AureaText.t(key)).accessibilityAddTraits(on ? [.isSelected] : []).accessibilityIdentifier(id)
    }
    /// Selecionar: um quadrado tracejado (o conjunto de ícones do app não tem cursor).
    private func selectTool(_ on: Bool, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Rectangle().stroke(on ? AureaColors.onAction : AureaColors.text, style: StrokeStyle(lineWidth: 1.6, dash: [3, 2.5]))
                .frame(width: 15, height: 15)
                .frame(width: Scene3DNav.tool, height: Scene3DNav.tool)
                .background(on ? AureaColors.brand : Color.clear, in: RoundedRectangle(cornerRadius: 9)).contentShape(Rectangle())
        }.buttonStyle(.plain).accessibilityLabel(AureaText.t("s3d_menu_select")).accessibilityAddTraits(on ? [.isSelected] : []).accessibilityIdentifier("scene.tool.select")
    }
}

/// O rótulo com seta que abre um menu: o título das viewports e do painel.
private struct Scene3DPill: View {
    let label: String
    let glyph: Character?
    var body: some View {
        HStack(spacing: 7) {
            if let glyph { CupertinoGlyph.text(glyph, size: 15) }
            if !label.isEmpty { Text(label).font(.aurea(size: 13)).lineLimit(1) }
            CupertinoGlyph.text(CupertinoGlyph.ChevronDown, size: 11, color: AureaColors.muted)
        }.foregroundStyle(AureaColors.text).padding(.horizontal, 10).frame(height: 32)
            .background(AureaColors.editorRowPill, in: RoundedRectangle(cornerRadius: 9))
    }
}

/// A viewport de prévia: o resultado pela câmera, sem grade, gizmo ou osso. O palco é um
/// só, então isto é um render à parte da mesma cena. Acompanha o cabeçote e cada edição.
@MainActor private struct Scene3DPreviewPane: View {
    @EnvironmentObject private var model: AureaModel
    @State private var frame: UIImage?
    private var stamp: String { "\(model.status.playhead)-\(model.status.modelRevision)" }
    var body: some View {
        ZStack {
            Color.black
            if let frame { Image(uiImage: frame).resizable().scaledToFit() }
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(AureaText.t("s3d_preview")).accessibilityIdentifier("scene.preview")
        .task {
            var last = ""
            while !Task.isCancelled {
                let now = stamp
                if now != last, let shot = await model.capturePreviewFrame(640, previewQuality: true), !Task.isCancelled {
                    last = now
                    frame = UIImage.fromRGBA(shot.data, width: Int(shot.width), height: Int(shot.height)) ?? frame
                }
                let wait: UInt64 = model.status.playing != 0 ? 250_000_000 : 120_000_000
                try? await Task.sleep(nanoseconds: wait)
            }
        }
    }
}


// =============================================================================
// 0.4.6: a nova Cena 3D — menu Adicionar e área de baixo (par do
// SceneLayoutWorkspace.kt: StudioAddMenu, StudioTimeline, StudioProperties,
// StudioOutliner). Mesmos textos, mesmas ações do motor.
// =============================================================================

@MainActor private struct Scene3DStudioAddMenu: View {
    @EnvironmentObject private var model: AureaModel
    let onClose: () -> Void
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 10) {
                HStack {
                    Text(String(AureaText.t("panel_adicionar").drop(while: { $0 == "+" || $0 == " " })))
                        .font(.aurea(size: 15, weight: .bold)).foregroundStyle(AureaColors.text)
                    Spacer()
                    Button(action: onClose) { CupertinoGlyph.text(CupertinoGlyph.Xmark, size: 16, color: AureaColors.text).frame(width: 40, height: 40) }
                        .buttonStyle(.plain).accessibilityLabel(AureaText.t("common_close"))
                }
                section("s3d_section_import")
                Button { onClose(); model.showAddLayer = true } label: {
                    HStack(spacing: 10) {
                        CupertinoGlyph.text(CupertinoGlyph.CubeFill, size: 20)
                        VStack(alignment: .leading, spacing: 2) {
                            Text(AureaText.t("s3d_import_model")).font(.aurea(size: 14, weight: .semibold))
                            Text(AureaText.t("s3d_import_formats")).font(.aurea(size: 11)).opacity(0.75)
                        }
                        Spacer()
                    }.foregroundStyle(Color.black).padding(.horizontal, 14).padding(.vertical, 12)
                        .background(AureaColors.accent, in: RoundedRectangle(cornerRadius: 12))
                }.buttonStyle(.plain)
                section("s3d_section_mesh")
                // Shape3DKind: 0 cubo, 1 esfera, 2 cilindro, 3 cone, 4 pirâmide, 5 toro.
                grid([("s3d_cube", 0), ("s3d_sphere", 1), ("s3d_cylinder", 2), ("s3d_cone", 3), ("s3d_pyramid", 4), ("s3d_torus", 5)].map { key, kind in
                    (CupertinoGlyph.CubeFill, AureaText.t(key), { model.addShape3D(kind: kind) })
                })
                section("s3d_section_other")
                grid([
                    (CupertinoGlyph.Textformat, AureaText.t("sh_add_text_3d"), { _ = model.addText3D(content: AureaText.t("panel_texto"), depth: 0.25) }),
                    (CupertinoGlyph.Lightbulb, AureaText.t("scene_light_point"), { model.addLight(1) }),
                    (CupertinoGlyph.Lightbulb, AureaText.t("scene_light_directional"), { model.addLight(0) }),
                    (CupertinoGlyph.Lightbulb, AureaText.t("scene_light_spot"), { model.addLight(2) }),
                    (CupertinoGlyph.Sparkles, AureaText.t("scene_flare3d"), { model.addLight(3) }),
                    (CupertinoGlyph.Videocam, AureaText.t("panel_camera_3d"), { model.addCamera() }),
                    (CupertinoGlyph.SmallcircleCircle, AureaText.t("s3d_empty"), { model.addNull(threeD: true) }),
                    (CupertinoGlyph.Camera, AureaText.t("real_camera_title"), { model.startRealCamera() }),
                ])
            }.padding(12)
        }
        .background(AureaColors.surface, in: RoundedRectangle(cornerRadius: 16))
        .overlay(RoundedRectangle(cornerRadius: 16).stroke(AureaColors.border, lineWidth: 1))
        .padding(8)
    }
    private func section(_ key: String) -> some View {
        Text(AureaText.t(key).uppercased()).font(.aurea(size: 11, weight: .semibold)).foregroundStyle(AureaColors.muted)
    }
    private func grid(_ items: [(Character, String, () -> Void)]) -> some View {
        LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 6), count: 3), spacing: 6) {
            ForEach(Array(items.enumerated()), id: \.offset) { _, item in
                Button { onClose(); item.2() } label: {
                    VStack(spacing: 6) {
                        CupertinoGlyph.text(item.0, size: 22)
                        Text(item.1).font(.aurea(size: 12)).lineLimit(1)
                    }.foregroundStyle(AureaColors.text).frame(maxWidth: .infinity).padding(.vertical, 12).padding(.horizontal, 4)
                        .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 12))
                }.buttonStyle(.plain).accessibilityLabel(AureaText.t("s3d_add_named", item.1))
            }
        }
    }
}

/// Uma linha do painel de animação: um objeto (`node` -1) ou um osso dele.
private struct Scene3DLane: Hashable {
    let layer: Int64
    var node: Int32 = -1
}

@MainActor private struct Scene3DStudioBottom: View {
    @EnvironmentObject private var model: AureaModel
    let area: Int
    let wide: Bool
    let isModel: Bool
    let onArea: (Int) -> Void
    let onAdd: () -> Void
    let openMaterial: () -> Void
    let openLights: () -> Void
    let openCurve: () -> Void
    let more: AnyView
    @State private var pickedLane: Scene3DLane?
    @State private var pickedFrame = 0
    @State private var folded = false
    // O toque na timeline: a chave agarrada e até onde ela foi.
    @State private var touching = false
    @State private var grabbedLane: Scene3DLane?
    @State private var grabbedFrame = 0
    @State private var dragTo: Int?
    private var labels: CGFloat { wide ? 168 : 140 }
    private static let rowHeight: CGFloat = 26
    private static let ruler: CGFloat = 24
    private static let pad: CGFloat = 10
    private var areas: [String] {
        ["s3d_animation", "s3d_area_properties", "s3d_area_outliner"] + (isModel ? ["skeleton_title"] : [])
    }
    private var pose: Bool { model.rigPoseMode && !model.poseBones.isEmpty }
    private var activeLane: Scene3DLane? {
        if let pickedLane { return pickedLane }
        guard let id = model.primarySelection else { return nil }
        return Scene3DLane(layer: id, node: pose ? model.poseBone : -1)
    }
    var body: some View {
        VStack(spacing: 0) {
            toolbar
            Group {
                switch area {
                case 0: animation
                case 1: properties
                case 2: outliner
                default: SkeletonPanelView().environmentObject(model)
                }
            }.frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .background(AureaColors.editorCanvas, in: RoundedRectangle(cornerRadius: 12))
        .clipShape(RoundedRectangle(cornerRadius: 12))
        .padding(.horizontal, 8).padding(.vertical, 6)
        .environment(\.layoutDirection, .leftToRight)
        .onChange(of: model.primarySelection) { _ in pickedLane = nil; folded = false }
    }

    // --- A barra do painel ---------------------------------------------------
    // Deitado cabe numa linha; em pé são duas.
    @ViewBuilder private var toolbar: some View {
        if wide {
            HStack(spacing: 0) {
                areaMenu
                Spacer(minLength: 4)
                editButtons
                Rectangle().fill(AureaColors.editorBarLine).frame(width: 1, height: 20)
                transport
                Spacer(minLength: 4)
                keyButtons
            }.padding(.horizontal, 6).frame(height: 40)
        } else {
            VStack(spacing: 0) {
                HStack(spacing: 0) {
                    areaMenu
                    Spacer(minLength: 4)
                    editButtons
                }.frame(height: 40)
                HStack(spacing: 0) {
                    transport
                    Spacer(minLength: 4)
                    keyButtons
                }.frame(height: 40)
            }.padding(.horizontal, 6)
        }
    }
    private var areaMenu: some View {
        let keys = areas
        let current = keys[min(area, keys.count - 1)]
        return Menu {
            ForEach(Array(keys.enumerated()), id: \.offset) { i, key in
                Button { onArea(i) } label: {
                    if i == area { Label(AureaText.t(key), systemImage: "checkmark") } else { Text(AureaText.t(key)) }
                }
            }
        } label: {
            Scene3DPill(label: AureaText.t(current), glyph: CupertinoGlyph.PersonCropCircle)
        }.accessibilityIdentifier("scene.area")
    }
    private var editButtons: some View {
        HStack(spacing: 0) {
            action(CupertinoGlyph.Plus, "panel_adicionar", "scene.add", onAdd)
            icon(CupertinoGlyph.ArrowUturnLeft, "editor_desfazer") { model.undo() }
            icon(CupertinoGlyph.ArrowUturnRight, "editor_refazer") { model.redo() }
        }
    }
    private var transport: some View {
        let playing = model.status.playing != 0
        return HStack(spacing: 0) {
            icon(playing ? CupertinoGlyph.PauseFill : CupertinoGlyph.PlayFill, playing ? "s3d_pause" : "s3d_play") {
                if playing { model.pause() } else { model.play() }
            }
            icon(CupertinoGlyph.BackwardEnd, "s3d_prev_key") { _ = model.stepToKeyframe(-1) }
            icon(CupertinoGlyph.ForwardEnd, "s3d_next_key") { _ = model.stepToKeyframe(1) }
            Menu {
                Button(AureaText.t("s3d_insert_key_desc")) { model.toggleSceneKey() }
                Button(AureaText.t("s3d_delete_key")) { deleteKey() }
                Button { model.autoKeyTransforms.toggle() } label: {
                    let text = AureaText.t(model.autoKeyTransforms ? "s3d_autokey_on" : "s3d_autokey_off")
                    if model.autoKeyTransforms { Label(text, systemImage: "checkmark") } else { Text(text) }
                }
            } label: {
                Scene3DPill(label: "", glyph: CupertinoGlyph.SuitDiamond)
            }.accessibilityLabel(AureaText.t("s3d_insert_key_desc")).accessibilityIdentifier("scene.key.menu")
        }
    }
    private var keyButtons: some View {
        HStack(spacing: 0) {
            action(CupertinoGlyph.SuitDiamond, "s3d_insert_key_desc", "scene.key") { model.toggleSceneKey() }
            icon(CupertinoGlyph.Scribble, "s3d_curve") { showCurve() }
            Menu { more } label: {
                CupertinoGlyph.text(CupertinoGlyph.Ellipsis, size: 18, color: AureaColors.text).frame(width: 40, height: 36).contentShape(Rectangle())
            }.accessibilityLabel(AureaText.t("s3d_more")).accessibilityIdentifier("scene.more")
        }
    }
    private func icon(_ glyph: Character, _ key: String, _ run: @escaping () -> Void) -> some View {
        Button(action: run) {
            CupertinoGlyph.text(glyph, size: 18, color: AureaColors.text).frame(width: 40, height: 36).contentShape(Rectangle())
        }.buttonStyle(.plain).accessibilityLabel(AureaText.t(key))
    }
    private func action(_ glyph: Character, _ key: String, _ id: String, _ run: @escaping () -> Void) -> some View {
        Button(action: run) {
            CupertinoGlyph.text(glyph, size: 16, color: AureaColors.onAction).frame(width: 38, height: 30)
                .background(AureaColors.brand, in: RoundedRectangle(cornerRadius: 8)).contentShape(Rectangle())
        }.buttonStyle(.plain).accessibilityLabel(AureaText.t(key)).accessibilityIdentifier(id)
    }

    // --- As chaves de cada linha (as mesmas da timeline principal) -----------
    private var objects: [LayerItem] { model.layers.filter { $0.threeD } }
    private func transformKeyed(_ k: KeyframeItem) -> Bool { k.effectIndex == UInt32.max && k.property <= 12 && k.timelineVisible }
    private func laneKeys(_ lane: Scene3DLane) -> [KeyframeItem] {
        let all = model.keyframes[lane.layer] ?? []
        guard lane.node >= 0 else { return all.filter(transformKeyed) }
        let node = UInt32(lane.node)
        return all.filter { $0.property == 45 && $0.effectIndex == node && $0.timelineVisible }
    }
    /// Uma trilha só da linha (um eixo do osso): é a que o gráfico mostra.
    private func laneTrack(_ lane: Scene3DLane) -> [KeyframeItem] {
        let all = laneKeys(lane)
        guard let first = all.first else { return [] }
        let track = all.filter { k in
            k.property == first.property && (k.property < 44 || (k.effectIndex == first.effectIndex && k.paramIndex == first.paramIndex))
        }
        return track.sorted { $0.time < $1.time }
    }
    /// Tempo local da camada → quadro da composição.
    private func laneShift(_ layer: Int64) -> Int {
        guard let row = model.layers.first(where: { $0.id == layer }) else { return 0 }
        return Int(row.startFrame) - Int(row.offsetFrames)
    }
    private func laneFrames(_ lane: Scene3DLane) -> [Int] {
        let shift = laneShift(lane.layer)
        return Array(Set(laneKeys(lane).map { Int($0.time) + shift })).sorted()
    }
    private var laneRows: [Scene3DLane] {
        var rows: [Scene3DLane] = []
        let layer = model.primarySelection
        let f = AureaModel.boneFloats, bones = model.poseBones
        for o in objects where o.id == layer || !laneKeys(Scene3DLane(layer: o.id)).isEmpty {
            rows.append(Scene3DLane(layer: o.id))
            if o.id == layer && !folded {
                for i in 0..<(bones.count / f) { rows.append(Scene3DLane(layer: o.id, node: Int32(bones[i * f]))) }
            }
        }
        return rows
    }
    private func deleteKey() {
        guard let lane = activeLane else { return }
        let at = pickedLane != nil ? pickedFrame : Int(model.status.playhead)
        let shift = laneShift(lane.layer)
        model.deleteKeysTogether(layer: lane.layer, keys: laneKeys(lane).filter { Int($0.time) + shift == at })
        pickedLane = nil
    }
    private func showCurve() {
        let keys = activeLane.map(laneTrack) ?? []
        guard let lane = activeLane, keys.count >= 2 else {
            model.toast = AureaText.t("s3d_curve_need_keys")
            return
        }
        if lane.layer != model.primarySelection { model.select(layerId: lane.layer, additive: false, openOptions: false) }
        let local = model.localPlayhead
        let start = keys.dropLast().last { $0.time <= local } ?? keys[0]
        model.openCurve(property: start.property, effect: start.effectIndex, param: start.paramIndex, time: start.time)
        openCurve()
    }

    // --- Os ossos, a timeline em segundos e o gráfico da curva ---------------
    private var animation: some View {
        let end = max(1, Int(model.compositionDuration) - 1)
        let rows = laneRows
        var keys: [Scene3DLane: [Int]] = [:]
        for lane in rows { keys[lane] = laneFrames(lane) }
        let frames = keys
        return VStack(spacing: 0) {
            HStack(spacing: 0) {
                lanes(rows: rows, keys: frames, end: end)
                if wide {
                    Rectangle().fill(AureaColors.editorBarLine).frame(width: 1)
                    curveGraph.frame(width: 176)
                }
            }
            if !wide {
                Rectangle().fill(AureaColors.editorBarLine).frame(height: 1)
                curveGraph.frame(height: 84)
            }
        }
    }
    private func lanes(rows: [Scene3DLane], keys frames: [Scene3DLane: [Int]], end: Int) -> some View {
        GeometryReader { geo in
                let width = max(1, geo.size.width - labels - 1)
                ZStack(alignment: .topLeading) {
                    VStack(spacing: 0) {
                        HStack(spacing: 0) {
                            Color.clear.frame(width: labels + 1, height: Self.ruler)
                            rulerView(end: end, width: width)
                        }
                        ScrollViewReader { proxy in
                            ScrollView {
                                HStack(alignment: .top, spacing: 0) {
                                    VStack(spacing: 0) {
                                        ForEach(rows, id: \.self) { lane in
                                            laneLabel(lane, here: (frames[lane] ?? []).contains(Int(model.status.playhead))).id(lane)
                                        }
                                    }.frame(width: labels)
                                    Rectangle().fill(AureaColors.editorBarLine).frame(width: 1)
                                    rowsView(rows: rows, keys: frames, end: end, width: width)
                                }
                            }
                            // Escolheu um osso na viewport: a lista vai até ele.
                            .onChange(of: model.poseBone) { node in
                                guard let id = model.primarySelection, node >= 0 else { return }
                                withAnimation { proxy.scrollTo(Scene3DLane(layer: id, node: node)) }
                            }
                        }
                    }
                    playheadView(end: end, width: width).offset(x: labels + 1).allowsHitTesting(false)
                }
        }
    }
    private func keyX(_ frame: Int, end: Int, width: CGFloat) -> CGFloat {
        Self.pad + CGFloat(frame) / CGFloat(end) * (width - Self.pad * 2)
    }
    private func frameAt(_ x: CGFloat, end: Int, width: CGFloat) -> Int {
        let raw = (x - Self.pad) / max(1, width - Self.pad * 2) * CGFloat(end)
        return min(end, max(0, Int(raw.rounded())))
    }
    private func laneLabel(_ lane: Scene3DLane, here: Bool) -> some View {
        let bone = lane.node >= 0
        let row = objects.first { $0.id == lane.layer }
        let on = lane.layer == model.primarySelection && lane.node == (pose ? model.poseBone : -1)
        let name = bone ? model.modelNodeName(lane.node) : (row?.name ?? "")
        return HStack(spacing: 0) {
            if bone {
                CupertinoGlyph.text(CupertinoGlyph.Link, size: 12, color: AureaColors.muted).padding(.trailing, 6)
            } else if let row {
                Button {
                    model.mutate { $0.setLayer(row.id, visible: !row.visible) }
                    model.refreshModel(force: true)
                } label: {
                    CupertinoGlyph.text(row.visible ? CupertinoGlyph.Eye : CupertinoGlyph.EyeSlash, size: 15, color: AureaColors.muted)
                        .frame(width: 22, height: 22).contentShape(Rectangle())
                }.buttonStyle(.plain).padding(.trailing, 4).accessibilityLabel(row.name)
                if row.id == model.primarySelection && !model.poseBones.isEmpty {
                    Button { folded.toggle() } label: {
                        CupertinoGlyph.text(folded ? CupertinoGlyph.ChevronRight : CupertinoGlyph.ChevronDown, size: 10, color: AureaColors.muted)
                            .frame(width: 18, height: 18).contentShape(Rectangle())
                    }.buttonStyle(.plain).accessibilityLabel(AureaText.t(folded ? "s3d_area_expand" : "s3d_area_collapse"))
                }
            }
            Text(name).font(.aurea(size: 12)).foregroundStyle(on ? AureaColors.text : AureaColors.muted).lineLimit(1)
            Spacer(minLength: 0)
            // Cheio = há chave neste instante. Tocar põe ou tira a chave da linha.
            Button { toggleKey(lane) } label: {
                Circle().strokeBorder(here ? AureaColors.brand : AureaColors.muted, lineWidth: 1)
                    .background(Circle().fill(here ? AureaColors.brand : Color.clear))
                    .frame(width: 11, height: 11).frame(width: 26, height: 26).contentShape(Rectangle())
            }.buttonStyle(.plain).accessibilityLabel(AureaText.t("s3d_insert_key_desc") + " " + name)
                .accessibilityAddTraits(here ? [.isSelected] : [])
        }
        .padding(.leading, bone ? 26 : 8).padding(.trailing, 2)
        .frame(height: Self.rowHeight)
        .background(on ? AureaColors.editorRowPill : Color.clear)
        .contentShape(Rectangle())
        .onTapGesture { chooseLane(lane) }
    }
    private func chooseLane(_ lane: Scene3DLane) {
        if lane.node >= 0 {
            if !model.rigPoseMode { model.setRigPose(true) }
            model.choosePoseBone(lane.node)
        } else {
            model.select(layerId: lane.layer, additive: false, openOptions: false)
        }
    }
    private func toggleKey(_ lane: Scene3DLane) {
        if lane.node >= 0 {
            if !model.rigPoseMode { model.setRigPose(true) }
            model.choosePoseBone(lane.node)
            model.togglePoseKey(all: false)
        } else {
            if lane.layer != model.primarySelection { model.select(layerId: lane.layer, additive: false, openOptions: false) }
            model.toggleObjectKey()
        }
    }
    /// Régua em segundos: o menor passo com rótulos a pelo menos 56 pt.
    private func rulerView(end: Int, width: CGFloat) -> some View {
        let fps = max(1, model.compositionFps)
        let seconds = Double(end) / fps
        let usable = Double(width - Self.pad * 2)
        let step = [0.1, 0.25, 0.5, 1, 2, 5, 10, 30, 60].first { $0 / seconds * usable >= 56 } ?? 120
        let tick = AureaColors.editorBarLine, muted = AureaColors.muted
        return Canvas { ctx, size in
            ctx.fill(Path(CGRect(origin: .zero, size: size)), with: .color(tick.opacity(0.25)))
            var t = 0.0
            while t <= seconds + 0.001 {
                let x = keyX(Int((t * fps).rounded()), end: end, width: width)
                var line = Path()
                line.move(to: CGPoint(x: x, y: size.height - 6))
                line.addLine(to: CGPoint(x: x, y: size.height))
                ctx.stroke(line, with: .color(tick), lineWidth: 1)
                let text = step < 1 ? String(format: "%.1fs", locale: Locale(identifier: "en_US_POSIX"), t) : "\(Int(t))s"
                ctx.draw(Text(text).font(.system(size: 10)).foregroundColor(muted), at: CGPoint(x: x + 3, y: 2), anchor: .topLeading)
                t += step
            }
        }
        .frame(width: width, height: Self.ruler)
        .contentShape(Rectangle())
        .gesture(DragGesture(minimumDistance: 0).onChanged { g in
            if model.status.playing != 0 { model.pause() }
            pickedLane = nil
            model.seek(toFrame: Int64(frameAt(g.location.x, end: end, width: width)))
        })
        .accessibilityLabel(AureaText.t("s3d_area_timeline")).accessibilityValue("\(model.status.playhead)")
    }
    private func rowsView(rows: [Scene3DLane], keys: [Scene3DLane: [Int]], end: Int, width: CGFloat) -> some View {
        let brand = AureaColors.brand, picked = AureaColors.keyframe, line = AureaColors.editorBarLine
        let height = CGFloat(rows.count) * Self.rowHeight
        return Canvas { ctx, size in
            for (r, lane) in rows.enumerated() {
                let top = CGFloat(r) * Self.rowHeight
                var rule = Path()
                rule.move(to: CGPoint(x: 0, y: top + Self.rowHeight))
                rule.addLine(to: CGPoint(x: size.width, y: top + Self.rowHeight))
                ctx.stroke(rule, with: .color(line.opacity(0.5)), lineWidth: 1)
                let cy = top + Self.rowHeight / 2
                let moving = grabbedLane == lane ? dragTo : nil
                let shown = (keys[lane] ?? []).map { f -> Int in
                    if let moving, f == grabbedFrame { return moving }
                    return f
                }
                if let low = shown.min(), let high = shown.max(), low != high {
                    var join = Path()
                    join.move(to: CGPoint(x: keyX(low, end: end, width: width), y: cy))
                    join.addLine(to: CGPoint(x: keyX(high, end: end, width: width), y: cy))
                    ctx.stroke(join, with: .color(brand.opacity(0.5)), lineWidth: 1)
                }
                for f in shown {
                    let x = keyX(f, end: end, width: width), rr: CGFloat = 5.5
                    var d = Path()
                    d.move(to: CGPoint(x: x, y: cy - rr))
                    d.addLine(to: CGPoint(x: x + rr, y: cy))
                    d.addLine(to: CGPoint(x: x, y: cy + rr))
                    d.addLine(to: CGPoint(x: x - rr, y: cy))
                    d.closeSubpath()
                    let chosen = pickedLane == lane && pickedFrame == f
                    ctx.fill(d, with: .color(chosen ? picked : brand))
                }
            }
        }
        .frame(width: width, height: height)
        .contentShape(Rectangle())
        .gesture(DragGesture(minimumDistance: 0).onChanged { g in
            timelineTouch(g, rows: rows, keys: keys, end: end, width: width)
        }.onEnded { _ in timelineRelease() })
        .accessibilityLabel(AureaText.t("s3d_area_timeline")).accessibilityValue("\(model.status.playhead)")
    }
    private func timelineTouch(_ g: DragGesture.Value, rows: [Scene3DLane], keys: [Scene3DLane: [Int]], end: Int, width: CGFloat) {
        if model.status.playing != 0 { model.pause() }
        if !touching {
            touching = true
            grabbedLane = nil
            let row = Int(g.startLocation.y / Self.rowHeight)
            if row >= 0 && row < rows.count {
                let lane = rows[row]
                let startX = g.startLocation.x
                let near = (keys[lane] ?? []).min { abs(keyX($0, end: end, width: width) - startX) < abs(keyX($1, end: end, width: width) - startX) }
                if let near, abs(keyX(near, end: end, width: width) - startX) < 14 {
                    grabbedLane = lane
                    grabbedFrame = near
                    pickedLane = lane
                    pickedFrame = near
                    if lane.layer != model.primarySelection { model.select(layerId: lane.layer, additive: false, openOptions: false) }
                    model.seek(toFrame: Int64(near))
                    return
                }
            }
            pickedLane = nil
        }
        let frame = frameAt(g.location.x, end: end, width: width)
        if grabbedLane != nil {
            if abs(g.translation.width) > 6 { dragTo = frame }
        } else {
            model.seek(toFrame: Int64(frame))
        }
    }
    /// Soltou: as chaves do instante andam juntas, em um passo de desfazer.
    private func timelineRelease() {
        touching = false
        if let lane = grabbedLane, let to = dragTo, to != grabbedFrame {
            let from = grabbedFrame
            let shift = laneShift(lane.layer)
            model.moveKeysTogether(layer: lane.layer, keys: laneKeys(lane).filter { Int($0.time) + shift == from }, delta: Int32(to - from))
            pickedLane = lane
            pickedFrame = to
            model.seek(toFrame: Int64(to))
        }
        grabbedLane = nil
        dragTo = nil
    }
    private func playheadView(end: Int, width: CGFloat) -> some View {
        let head = AureaColors.accent
        let frame = min(end, max(0, Int(model.status.playhead)))
        let x = keyX(frame, end: end, width: width)
        return Canvas { ctx, size in
            var line = Path()
            line.move(to: CGPoint(x: x, y: Self.ruler))
            line.addLine(to: CGPoint(x: x, y: size.height))
            ctx.stroke(line, with: .color(head), lineWidth: 1.5)
            let handle = CGRect(x: x - 6, y: 3, width: 12, height: Self.ruler - 8)
            ctx.fill(Path(roundedRect: handle, cornerRadius: 3), with: .color(head))
        }.frame(width: width)
    }
    /// O gráfico da trilha escolhida, em % entre o menor e o maior valor. Tocar abre o editor.
    private var curveGraph: some View {
        let keys = activeLane.map(laneTrack) ?? []
        let brand = AureaColors.brand, accent = AureaColors.accent, grid = AureaColors.editorBarLine, muted = AureaColors.muted
        return Canvas { ctx, size in
            let left: CGFloat = 30, top: CGFloat = 12, bottom = size.height - 12, right = size.width - 8
            func y(_ p: CGFloat) -> CGFloat { bottom - p / 100 * (bottom - top) }
            for p in [CGFloat(0), 50, 100] {
                var rule = Path()
                rule.move(to: CGPoint(x: left, y: y(p)))
                rule.addLine(to: CGPoint(x: right, y: y(p)))
                ctx.stroke(rule, with: .color(grid.opacity(0.5)), lineWidth: 1)
                ctx.draw(Text("\(Int(p))").font(.system(size: 10)).foregroundColor(muted), at: CGPoint(x: left - 5, y: y(p)), anchor: .trailing)
            }
            var axis = Path()
            axis.move(to: CGPoint(x: left, y: top))
            axis.addLine(to: CGPoint(x: left, y: bottom))
            ctx.stroke(axis, with: .color(grid), lineWidth: 1)
            guard keys.count >= 2, let first = keys.first, let last = keys.last else { return }
            let t0 = CGFloat(first.time), t1 = max(CGFloat(last.time), t0 + 1)
            let values = keys.map { CGFloat($0.value) }
            let low = values.min() ?? 0, high = values.max() ?? 0
            let span = high - low
            func pct(_ v: CGFloat) -> CGFloat { span > 0.00001 ? (v - low) / span * 100 : 50 }
            func x(_ t: CGFloat) -> CGFloat { left + (t - t0) / (t1 - t0) * (right - left) }
            var curve = Path()
            curve.move(to: CGPoint(x: x(t0), y: y(pct(values[0]))))
            for i in 0..<(keys.count - 1) {
                let a = CGFloat(keys[i].time), b = CGFloat(keys[i + 1].time)
                for k in 1...16 {
                    let u = CGFloat(k) / 16
                    let e = u * u * (3 - 2 * u)
                    let v = values[i] + (values[i + 1] - values[i]) * e
                    curve.addLine(to: CGPoint(x: x(a + (b - a) * u), y: y(pct(v))))
                }
            }
            ctx.stroke(curve, with: .color(brand), lineWidth: 2)
            for (i, key) in keys.enumerated() {
                let c = CGPoint(x: x(CGFloat(key.time)), y: y(pct(values[i])))
                ctx.fill(Path(ellipseIn: CGRect(x: c.x - 3, y: c.y - 3, width: 6, height: 6)), with: .color(accent))
            }
        }
        .contentShape(Rectangle())
        .onTapGesture { showCurve() }
        .accessibilityLabel(AureaText.t("s3d_curve")).accessibilityAddTraits(.isButton).accessibilityIdentifier("scene.graph")
    }

    // --- Propriedades --------------------------------------------------------
    private var properties: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 6) {
                if let id = model.primarySelection, let row = model.layers.first(where: { $0.id == id }), let d = model.engine.layerDetail(id) {
                    let keyMask = (d["keyAtPlayhead"] as? NSNumber)?.uint32Value ?? 0
                    let keyed = (0..<9).contains { keyMask & (1 << UInt32($0)) != 0 }
                    HStack {
                        Text(row.name).font(.aurea(size: 13, weight: .semibold)).foregroundStyle(AureaColors.text).lineLimit(1)
                        Spacer()
                        Text(keyed ? AureaText.t("s3d_key_here", Int(model.status.playhead)) : AureaText.t("s3d_no_key"))
                            .font(.aurea(size: 11)).foregroundStyle(keyed ? AureaColors.keyframe : AureaColors.muted)
                    }
                    HStack(spacing: 6) {
                        if row.kind == 10 || (row.kind == 4 && row.threeD) { pill(CupertinoGlyph.CubeFill, AureaText.t("scene_material"), openMaterial) }
                        if row.kind == 9 { pill(CupertinoGlyph.Lightbulb, AureaText.t("pn_t3d_lighting"), openLights) }
                        if row.kind == 10 && !model.poseBones.isEmpty && !model.rigPoseMode {
                            pill(CupertinoGlyph.PersonCropCircle, AureaText.t("rig_pose")) { model.setRigPose(true) }
                        }
                    }
                    // 0.4.6: AutoRig 3D e Motion Capture direto da Cena 3D (o painel abre na aba Esqueleto).
                    if row.kind == 10 {
                        HStack(spacing: 6) {
                            pill(CupertinoGlyph.Sparkles, AureaText.t("rig3d_autorig")) { model.startAutoRig() }
                            pill(CupertinoGlyph.Videocam, AureaText.t("mocap_title")) { Panel3DRequest.tab = .rig; openMaterial() }
                        }
                    }
                    vec3("s3d_position", d["position"], base: 0, unit: "", step: 10, decimals: 0, id: id)
                    vec3("s3d_rotation", d["rotation"], base: 6, unit: "°", step: 5, decimals: 0, id: id)
                    vec3("s3d_scale", d["scale"], base: 3, unit: "", step: 0.1, decimals: 2, id: id)
                } else {
                    Text(AureaText.t("s3d_nothing_selected")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                }
            }.padding(10)
        }
    }
    private func pill(_ glyph: Character, _ label: String, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 6) { CupertinoGlyph.text(glyph, size: 14); Text(label).font(.aurea(size: 12)).lineLimit(1) }
                .foregroundStyle(AureaColors.text).padding(.horizontal, 12).frame(height: 40)
                .background(AureaColors.editorRowPill, in: RoundedRectangle(cornerRadius: 10))
        }.buttonStyle(.plain)
    }
    private func vec3(_ key: String, _ raw: Any?, base: UInt32, unit: String, step: Float, decimals: Int, id: Int64) -> some View {
        let v = (raw as? [NSNumber] ?? []).map(\.floatValue)
        return VStack(alignment: .leading, spacing: 1) {
            Text(AureaText.t(key)).font(.aurea(size: 11)).foregroundStyle(AureaColors.muted)
            ForEach(0..<3, id: \.self) { k in
                let value = k < v.count ? v[k] : 0
                let text = String(format: "%.\(decimals)f", locale: Locale(identifier: "en_US_POSIX"), value) + unit
                HStack(spacing: 0) {
                    Button { model.setTransform(base + UInt32(k), value: value - step, layer: id) } label: {
                        CupertinoGlyph.text(CupertinoGlyph.ChevronLeft, size: 12, color: AureaColors.muted).frame(width: 40, height: 40)
                    }.buttonStyle(.plain)
                    Text(["X", "Y", "Z"][k]).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                    Spacer()
                    Text(text).font(.aurea(size: 12)).foregroundStyle(AureaColors.text)
                    Spacer()
                    Button { model.setTransform(base + UInt32(k), value: value + step, layer: id) } label: {
                        CupertinoGlyph.text(CupertinoGlyph.ChevronRight, size: 12, color: AureaColors.muted).frame(width: 40, height: 40)
                    }.buttonStyle(.plain)
                }
                .frame(height: 40).background(AureaColors.editorGlyphBox)
                .clipShape(RoundedRectangle(cornerRadius: k == 1 ? 0 : 10))
                .gesture(DragGesture(minimumDistance: 4).onChanged { g in
                    model.setTransform(base + UInt32(k), value: value + Float(g.translation.width) * step * 0.1, layer: id)
                })
                .accessibilityElement(children: .ignore)
                .accessibilityLabel(AureaText.t(key) + " " + ["X", "Y", "Z"][k]).accessibilityValue(text)
                .accessibilityAdjustableAction { dir in
                    model.setTransform(base + UInt32(k), value: value + (dir == .increment ? step : -step), layer: id)
                }
            }
        }
    }

    // --- Outliner ------------------------------------------------------------
    private var outliner: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                row(depth: 0, glyph: CupertinoGlyph.CubeFill, name: AureaText.t("s3d_scene"), on: false, keyed: false, delete: nil) {}
                ForEach(objects) { o in
                    let keyed = (model.keyframes[o.id] ?? []).contains(where: transformKeyed)
                    row(depth: 1, glyph: o.kind == 9 ? CupertinoGlyph.Lightbulb : CupertinoGlyph.CubeFill, name: o.name,
                        on: o.id == model.primarySelection, keyed: keyed, delete: {
                            model.select(layerId: o.id, additive: false, openOptions: false); model.deleteSelectedLayers()
                        }) { model.select(layerId: o.id, additive: false, openOptions: false) }
                    if model.rigPoseMode && o.id == model.primarySelection && !model.poseBones.isEmpty {
                        let floats = model.poseBones
                        let count = floats.count / 10
                        ForEach(0..<count, id: \.self) { i in
                            let node = Int32(floats[i * 10])
                            row(depth: 2 + boneDepth(floats, i), glyph: nil, name: model.modelNodeName(node), on: model.poseBone == node,
                                keyed: false, delete: nil) { model.choosePoseBone(node) }
                        }
                    }
                }
            }.padding(.vertical, 4)
        }
    }
    private func boneDepth(_ b: [Float], _ i: Int) -> Int {
        var d = 0, p = Int(b[i * 10 + 1])
        while p >= 0 && d < 8 && p * 10 + 1 < b.count { d += 1; p = Int(b[p * 10 + 1]) }
        return d
    }
    private func row(depth: Int, glyph: Character?, name: String, on: Bool, keyed: Bool, delete: (() -> Void)?, _ action: @escaping () -> Void) -> some View {
        HStack(spacing: 6) {
            Button(action: action) {
                HStack(spacing: 6) {
                    if let glyph { CupertinoGlyph.text(glyph, size: 14, color: AureaColors.muted) } else { Text("◇").font(.system(size: 11)).foregroundStyle(AureaColors.muted) }
                    Text(name).font(.aurea(size: 13)).foregroundStyle(on ? AureaColors.text : AureaColors.muted).lineLimit(1)
                    Spacer()
                    if keyed { Text("◆").font(.system(size: 10)).foregroundStyle(AureaColors.keyframe) }
                }.contentShape(Rectangle())
            }.buttonStyle(.plain)
            if let delete {
                Button(action: delete) { CupertinoGlyph.text(CupertinoGlyph.Trash, size: 14, color: AureaColors.muted).frame(width: 44, height: 44) }
                    .buttonStyle(.plain).accessibilityLabel(AureaText.t("s3d_delete_named", name))
            }
        }.padding(.leading, CGFloat(10 + depth * 14)).padding(.trailing, 6).frame(height: 44)
            .background(on ? AureaColors.accent.opacity(0.35) : Color.clear)
    }
}

@MainActor private struct SceneLightControls: View {
    @EnvironmentObject private var model: AureaModel
    /// Fecha a folha (o ✕ do cabeçalho; par do `SceneSheetHeader` do Android).
    let onClose: () -> Void
    var body: some View {
        let _ = model.status.modelRevision
        let _ = model.localPlayhead
        VStack(spacing: 0) {
            HStack(spacing: 0) {
                Text(AureaText.t("pn_t3d_lighting")).font(.aurea(size: 15, weight: .semibold)).foregroundStyle(AureaColors.text)
                    .lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
                    .accessibilityAddTraits(.isHeader)
                Button(action: onClose) {
                    CupertinoGlyph.text(CupertinoGlyph.Xmark, size: 16, color: AureaColors.muted)
                        .frame(width: 44, height: 44).contentShape(Rectangle())
                }
                .buttonStyle(AureaPressStyle(shrink: 1))
                .accessibilityLabel(AureaText.t("common_close"))
                .accessibilityIdentifier("scene.light.close")
            }.padding(.leading, 16).padding(.trailing, 4).padding(.top, 8)
            controls
        }.background(AureaColors.background)
    }
    private var controls: some View {
        ScrollView { VStack(alignment: .leading, spacing: 12) {
            HStack(spacing: 8) {
                addLight("scene_light_directional", kind: 0)
                addLight("scene_light_point", kind: 1)
                addLight("scene_light_spot", kind: 2)
            }
            if let id = model.primarySelection {
                let values = model.engine.lightInfo(id).map(\.floatValue)
                if values.count >= 11 {
                    AureaPropertyPanel(domain: "light", layerId: id, values: values,
                        projectGeneration: model.projectGeneration,
                        compositionId: (model.composition[AureaCompositionId] as? NSNumber)?.uint64Value ?? 0) { changes in
                        model.mutate { core in
                            for (param, value) in changes { core.setLightParam(id, param: param, value: value) }
                        }
                        model.refreshModel(force: true)
                    }.id("light:\(model.projectGeneration):\(model.composition[AureaCompositionId] ?? ""):\(id)")
                }
            }
        }.padding() }.background(AureaColors.background)
    }
    private func addLight(_ key: String, kind: UInt32) -> some View {
        Button { model.addLight(kind) } label: {
            Text(AureaText.t(key)).font(.aurea(size: 13, weight: .medium)).lineLimit(1).minimumScaleFactor(0.75)
                .frame(maxWidth: .infinity, minHeight: 44).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
        }.buttonStyle(AureaPressStyle()).accessibilityIdentifier("scene.light.add.\(kind)")
    }
}

/// As categorias de adicionar (as mesmas `AddTab` do Android, mesma ordem,
/// glifos e nomes). O índice é o `tab` do `AddLayerSheet`.
enum ShellAddCategories {
    /// Índice da categoria "Presets de texto" (par do `AddTab.TextPresets` do Android).
    static let textPresets = 8
    /// Ordem na barra: a do Android, com os presets de texto logo depois do Texto.
    static let order = [0, 1, 2, 3, textPresets, 4, 5, 6, 7]
    /// Omega adicionar (área 08, P0-12): o nome do `AddTab` do Android para o índice (ids iguais).
    static func name(_ index: Int) -> String {
        switch index {
        case 0: return "Shape"
        case 1: return "Media"
        case 2: return "Audio"
        case 3: return "Text"
        case textPresets: return "TextPresets"
        case 4: return "Element"
        case 5: return "Model3D"
        case 6: return "Draw"
        default: return "Vector"
        }
    }
    static let all: [(String, Character)] = [
        ("sh_add_tab_shape", ShellGlyph.SquareOnCircle), ("sh_add_tab_media", CupertinoGlyph.PhotoOnRectangle),
        ("sh_add_tab_audio", CupertinoGlyph.MusicNote2), ("sh_add_tab_text", CupertinoGlyph.Textformat),
        ("sh_add_tab_element", ShellGlyph.CircleGridHex), ("sh_add_tab_3d", CupertinoGlyph.Cube),
        ("sh_add_tab_draw", ShellGlyph.Scribble), ("sh_add_tab_vector", CupertinoGlyph.PencilOutline),
        // 8: presets de texto (fim da lista para não mudar os índices; na barra fica ao lado do Texto).
        ("tp_add_tab", CupertinoGlyph.WandStars)
    ]
}

/// 0.3.3 barra: a barra de adicionar nova (pílula de 5 categorias + o "+" que abre
/// a grade 4 × 2) mora em AddPanelView.swift (`AddBarView`, par do `AddBar` do Android).
@MainActor private struct ShellAddBar: View {
    var body: some View { AddBarView() }
}

/// O painel da categoria escolhida na barra: o mesmo `AddLayerSheet` de antes,
/// numa janela no meio da tela; tocar fora fecha.
private struct ShellAddCategoryDialog: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    var body: some View {
        GeometryReader { geometry in
            ZStack {
                Color.black.opacity(0.18).contentShape(Rectangle()).onTapGesture { model.showAddLayer = false }
                let limit = min(390, max(0, geometry.size.height - 48))
                let fillsPanel = [0, 1, ShellAddCategories.textPresets].contains(shell.addCategory)
                AddLayerSheet(tab: shell.addCategory, maxCardHeight: max(0, limit - 49))
                    .frame(width: min(380, max(200, geometry.size.width - 48)))
                    .frame(height: fillsPanel ? min(limit, max(180, geometry.size.height * 0.55)) : nil)
                    .clipShape(RoundedRectangle(cornerRadius: 20))
                    .overlay(RoundedRectangle(cornerRadius: 20).stroke(AureaColors.action, lineWidth: 1))
            }.frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }
}

// Real solved features in composition coordinates; dragging selects a region.
@MainActor private struct CameraTrackingOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var viewZoom = StageViewZoom.shared
    var body: some View {
        GeometryReader { geometry in
            let cw = CGFloat(max(1, model.compositionWidth)), ch = CGFloat(max(1, model.compositionHeight))
            let placed = StageZoomMath.fit(size: geometry.size, composition: CGSize(width: cw, height: ch), zoom: viewZoom.zoom, pan: viewZoom.pan)
            let fit = placed.scale
            let ox = placed.origin.x, oy = placed.origin.y
            Canvas { context, _ in
                let points = model.cameraFeatures
                let ring = model.cameraTarget
                if ring.count >= 6 {
                    var target = Path(); target.move(to: CGPoint(x: CGFloat(ring[0])*fit+ox, y: CGFloat(ring[1])*fit+oy))
                    for i in stride(from: 2, to: ring.count-1, by: 2) { target.addLine(to: CGPoint(x: CGFloat(ring[i])*fit+ox, y: CGFloat(ring[i+1])*fit+oy)) }
                    target.closeSubpath(); context.fill(target, with: .color(.green.opacity(0.15))); context.stroke(target, with: .color(.green), lineWidth: 2)
                }
                for i in stride(from: 0, to: points.count - points.count % 6, by: 6) {
                    if model.cameraGoodPointsOnly && points[i+2] < 0.4 { continue }
                    let point = CGPoint(x: CGFloat(points[i]), y: CGFloat(points[i + 1]))
                    let x = point.x * fit + ox, y = point.y * fit + oy
                    let arm = CGFloat(model.cameraPointSize)
                    var path = Path(); path.move(to: CGPoint(x: x-arm, y: y)); path.addLine(to: CGPoint(x: x+arm, y: y))
                    path.move(to: CGPoint(x: x, y: y-arm)); path.addLine(to: CGPoint(x: x, y: y+arm))
                    let selected = points[i+4] > 0.5 || model.cameraSelection?.contains(point) == true
                    context.stroke(path, with: .color(selected ? .green : points[i + 2] > 0.5 ? .yellow : .red), lineWidth: 2)
                }
                if let r = model.cameraSelection {
                    let box = CGRect(x: r.minX * fit + ox, y: r.minY * fit + oy, width: r.width * fit, height: r.height * fit)
                    context.stroke(Path(box), with: .color(.green), lineWidth: 1)
                }
            }.contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0).onChanged { value in
                    guard fit > 0 else { return }
                    model.engine.pause()
                    if hypot(value.translation.width, value.translation.height) < 8 { return }
                    let a = CGPoint(x: (value.startLocation.x - ox) / fit, y: (value.startLocation.y - oy) / fit)
                    let b = CGPoint(x: (value.location.x - ox) / fit, y: (value.location.y - oy) / fit)
                    if model.cameraTargetMode { model.moveCameraTarget(b); return }
                    let slack: CGFloat = 0
                    model.cameraSelection = CGRect(x: min(a.x,b.x)-slack, y: min(a.y,b.y)-slack, width: abs(a.x-b.x)+2*slack, height: abs(a.y-b.y)+2*slack)
                    model.cameraSelectionFrame = model.status.playhead
                }.onEnded { value in
                    guard fit > 0 else { return }
                    if hypot(value.translation.width, value.translation.height) < 8 {
                        model.selectCameraPoint(CGPoint(x: (value.location.x-ox)/fit, y: (value.location.y-oy)/fit), radius: 24/fit)
                    } else if !model.cameraTargetMode { model.finishCameraSelectionBox() }
                })
                .simultaneousGesture(LongPressGesture(minimumDuration: 0.5).onEnded { _ in model.cameraContextMenu = true })
                .confirmationDialog(AureaText.t("cam_tracker_title"), isPresented: $model.cameraContextMenu) {
                    Button(AureaText.t("panel_criar_camera")) { model.createTrackedObject(0) }
                    if model.cameraSelectedCount > 0 {
                        Button(AureaText.t("trk_create_null")) { model.createTrackedObject(1) }
                        Button(AureaText.t("cam_create_shape")) { model.createTrackedObject(2) }
                        Button(AureaText.t("cam_create_text")) { model.createTrackedObject(3) }
                        Button(AureaText.t("cam_create_solid")) { model.createTrackedObject(4) }
                    }
                    Button(AureaText.t("common_close"), role: .cancel) { }
                }
        }.accessibilityIdentifier("aurea.tracking.points")
    }
}

/// A folha da doca: só os cantos de CIMA arredondados (iOS 16 não tem `UnevenRoundedRectangle`).
private struct DockSheetShape: Shape {
    var radius: CGFloat
    func path(in rect: CGRect) -> Path {
        let r = min(radius, rect.width / 2, rect.height / 2)
        var p = Path()
        p.move(to: CGPoint(x: rect.minX, y: rect.maxY))
        p.addLine(to: CGPoint(x: rect.minX, y: rect.minY + r))
        p.addArc(center: CGPoint(x: rect.minX + r, y: rect.minY + r), radius: r, startAngle: .degrees(180), endAngle: .degrees(270), clockwise: false)
        p.addLine(to: CGPoint(x: rect.maxX - r, y: rect.minY))
        p.addArc(center: CGPoint(x: rect.maxX - r, y: rect.minY + r), radius: r, startAngle: .degrees(270), endAngle: .degrees(0), clockwise: false)
        p.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY))
        p.closeSubpath()
        return p
    }
}

// =============================================================================
// Sequência de imagens (0.3.0, ImageSequenceSheet.kt): o que a sondagem achou —
// padrão, quadros, buracos e formato — e a taxa, a transparência e a cor antes
// de entrar na timeline. Fechar a folha cancela (a cópia sai).
// =============================================================================
struct ImageSequenceSheet: View {
    @EnvironmentObject private var model: AureaModel
    let draft: AureaModel.ImageSequenceDraft
    @State private var rate = 0
    @State private var alpha = 0
    @State private var color = 0
    private var rates: [Double] {
        let suggested = draft.double("suggestedFps") > 0 ? draft.double("suggestedFps") : 24
        var out = [suggested]
        for r in [23.976, 24, 25, 30, 50, 60] where !out.contains(where: { abs($0 - r) < 1e-6 }) { out.append(r) }
        return out
    }
    private func fpsLabel(_ fps: Double) -> String {
        abs(fps - fps.rounded()) < 1e-6 ? String(Int(fps.rounded())) : String(format: "%.3f", fps).replacingOccurrences(of: "0+$", with: "", options: .regularExpression)
    }
    private func chips(_ labels: [String], _ selected: Int, _ id: String, _ pick: @escaping (Int) -> Void) -> some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 6) {
                ForEach(Array(labels.enumerated()), id: \.offset) { i, label in
                    NativePanelChip(label, selected: i == selected) { pick(i) }.accessibilityIdentifier("\(id).\(i)")
                }
            }
        }.frame(height: 40)
    }
    private func section(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted).padding(.top, 10)
    }
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 4) {
                Text(AureaText.t("seq_title")).font(.aurea(size: 17, weight: .semibold))
                Text(draft.string("pattern")).font(.system(size: 13, design: .monospaced)).foregroundStyle(AureaColors.accent)
                    .accessibilityIdentifier("seq.pattern")
                Text(AureaText.t("seq_frames", draft.int("frameCount"), draft.int("first"), draft.int("last"))).font(.aurea(size: 13))
                if draft.int("missingCount") > 0 {
                    let list = (draft.info["missing"] as? [NSNumber] ?? []).prefix(12).map { $0.stringValue }.joined(separator: ", ")
                    Text(AureaText.t("seq_missing", draft.int("missingCount"), list + (draft.int("missingCount") > 12 ? "…" : "")))
                        .font(.aurea(size: 13)).foregroundStyle(AureaColors.warning)
                }
                Text(AureaText.t("seq_format", draft.int("width"), draft.int("height"), draft.string("format").uppercased(), draft.int("bitDepth"),
                                 AureaText.t(draft.bool("hasAlpha") ? "seq_alpha_yes" : "seq_alpha_no"))).font(.aurea(size: 13))
                if draft.int("candidates") > 1 { Text(AureaText.t("seq_candidates", draft.int("candidates"))).font(.aurea(size: 13)) }

                section("seq_rate")
                chips(rates.enumerated().map { i, r in fpsLabel(r) + (i == 0 ? " (\(AureaText.t("seq_suggested")))" : "") }, rate, "seq.rate") { rate = $0 }
                section("seq_alpha_mode")
                chips(["seq_auto", "seq_alpha_straight", "seq_alpha_premultiplied", "seq_alpha_ignore"].map { AureaText.t($0) }, alpha, "seq.alpha") { alpha = $0 }
                section("seq_color_mode")
                chips(["seq_auto", "seq_color_srgb", "seq_color_linear"].map { AureaText.t($0) }, color, "seq.color") { color = $0 }
                Text(AureaText.t("seq_auto_note")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)

                HStack(spacing: 10) {
                    Button { model.cancelImageSequence() } label: {
                        Text(AureaText.t("common_cancel")).font(.aurea(size: 15, weight: .semibold)).frame(maxWidth: .infinity, minHeight: 46)
                            .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 12))
                    }.buttonStyle(.plain).accessibilityIdentifier("seq.cancel")
                    Button { model.importImageSequence(fps: rate == 0 ? 0 : rates[rate], alphaMode: Int32(alpha), colorMode: Int32(color)) } label: {
                        Text(AureaText.t("seq_import")).font(.aurea(size: 15, weight: .semibold)).foregroundStyle(AureaColors.onAccent)
                            .frame(maxWidth: .infinity, minHeight: 46).background(AureaColors.accent, in: RoundedRectangle(cornerRadius: 12))
                    }.buttonStyle(.plain).accessibilityIdentifier("seq.import")
                }.padding(.top, 14)
            }
            .foregroundStyle(AureaColors.text)
            .padding(.horizontal, 18).padding(.vertical, 16)
        }
        .background(AureaColors.surface.ignoresSafeArea())
    }
}

// =============================================================================
// Importar do After Effects (0.3.0, AeImportSheet.kt): o que entrou e, sem
// esconder nada, o que ficou de fora e o que entrou aproximado (com a linha).
// =============================================================================
struct AeImportSheet: View {
    @EnvironmentObject private var model: AureaModel
    let result: AureaModel.AeImportResult
    @State private var showNotes = false
    private var partial: Bool { !result.ok && (result.hasCounts || result.changes > 0) }
    private func kindLabel(_ kind: String) -> String {
        switch kind {
        case "efeito": return AureaText.t("ae_kind_effect")
        case "parametro": return AureaText.t("ae_kind_param")
        case "propriedade": return AureaText.t("ae_kind_property")
        case "texto": return AureaText.t("ae_kind_text")
        case "seletor": return AureaText.t("ae_kind_selector")
        case "expressao": return AureaText.t("ae_kind_expression")
        case "camada": return AureaText.t("ae_kind_layer")
        case "mascara": return AureaText.t("ae_kind_mask")
        case "script": return AureaText.t("ae_kind_script")
        case "chamada": return AureaText.t("ae_kind_call")
        default: return AureaText.t("ae_kind_other")
        }
    }
    private func row(_ item: AureaModel.AeImportItem) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(kindLabel(item.kind) + " · " + item.name + (item.line > 0 ? " · " + AureaText.t("ae_line", item.line) : ""))
                .font(.aurea(size: 13, weight: .medium))
            if !item.reason.isEmpty { Text(item.reason).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted) }
        }.frame(maxWidth: .infinity, alignment: .leading).padding(.horizontal, 12).padding(.vertical, 8)
            .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
    }
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 6) {
                // 0.3.1 (AeImportSheet.kt): script que parou com erro DEPOIS de mudar o projeto =
                // "importado em parte" (contagens + o erro); rodou sem mudar nada = "nada foi importado".
                Text(AureaText.t(result.ok ? "ae_done_title" : partial ? "ae_partial_title" : "ae_failed_title"))
                    .font(.aurea(size: 17, weight: .semibold)).accessibilityAddTraits(.isHeader)
                Text(result.fileName).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                if result.ok || result.hasCounts {
                    Text(AureaText.t("ae_counts", result.effects, result.keyframes, result.layers, result.expressions, result.animators))
                        .font(.aurea(size: 14)).accessibilityIdentifier("ae.counts")
                }
                if !result.message.isEmpty { Text(result.message).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted) }
                if !result.skipped.isEmpty {
                    Text(AureaText.t("ae_skipped_title", result.skipped.count)).font(.aurea(size: 14, weight: .semibold))
                        .foregroundStyle(AureaColors.warning).padding(.top, 8)
                    ForEach(result.skipped.prefix(60)) { row($0) }
                }
                if !result.notes.isEmpty {
                    Button { showNotes.toggle() } label: {
                        Text(AureaText.t("ae_notes_title", result.notes.count)).font(.aurea(size: 14, weight: .semibold)).foregroundStyle(AureaColors.accent)
                    }.buttonStyle(.plain).padding(.top, 8).accessibilityIdentifier("ae.notes")
                    if showNotes { ForEach(result.notes.prefix(60)) { row($0) } }
                }
                if result.ok || partial {
                    Text(AureaText.t("ae_undo_hint")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).padding(.top, 6)
                }
                Button { model.aeImportResult = nil } label: {
                    Text(AureaText.t("common_ok")).font(.aurea(size: 15, weight: .semibold)).foregroundStyle(AureaColors.onAccent)
                        .frame(maxWidth: .infinity, minHeight: 46).background(AureaColors.accent, in: RoundedRectangle(cornerRadius: 12))
                }.buttonStyle(.plain).padding(.top, 8).accessibilityIdentifier("ae.ok")
            }
            .foregroundStyle(AureaColors.text)
            .padding(.horizontal, 18).padding(.vertical, 16)
        }
        .background(AureaColors.surface.ignoresSafeArea())
    }
}
