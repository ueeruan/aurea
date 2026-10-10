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
    /// A altura do palco escolhida arrastando a divisa (0 = automática), guardada no aparelho.
    @AppStorage("editor.previewHeight") private var previewPreference: Double = 0
    @State private var dividerStart: CGFloat?
    var body: some View {
        GeometryReader { geometry in
            let wide = !model.fullscreen && model.panel != .curve && EditorLayout.isWide(geometry.size.width, geometry.size.height)
            let sideWidth = EditorLayout.wideSheetWidth(geometry.size.width)
            let aspect: CGFloat = model.compositionHeight > 0 ? CGFloat(model.compositionWidth) / CGFloat(model.compositionHeight) : 0
            // Fora da tela cheia o palco tem 8 pt de margem de cada lado.
            let sideMargin: CGFloat = model.fullscreen ? 0 : EditorLayout.previewSideMargin
            let metrics = EditorLayout.solve(total: geometry.size.height, content: model.sheetContent, fullscreen: model.fullscreen,
                                             width: max(0, geometry.size.width - sideMargin * 2), aspect: aspect, preferred: CGFloat(previewPreference),
                                             dockRows: model.sheetContent == .dock ? DockView.tileRows(model) : 2,
                                             fontScale: UIFont.preferredFont(forTextStyle: .body).pointSize / 17)
            Group {
                if model.sceneEditor {
                    SceneLayoutWorkspace(height: geometry.size.height)
                } else if wide {
                    VStack(spacing: 0) {
                        TopBarView().frame(height: EditorLayout.topBar)
                        HStack(spacing: 0) {
                            VStack(spacing: 0) {
                                PreviewStage(height: max(96, geometry.size.height - EditorLayout.topBar - EditorLayout.transport - EditorLayout.strip - EditorLayout.wideTimeline(geometry.size.height)
                                                         - (model.sheetContent == .addBar ? StageDim.addBar : 0)))
                                    .padding(.horizontal, sideMargin)
                                if EditorLayout.strip > 0 { Rectangle().fill(AureaColors.editorCanvas).frame(height: EditorLayout.strip) }
                                TransportView().frame(height: EditorLayout.transport)
                                TimelineView(timecodeStyle: model.selection.count == 1 ? .box : .underline).frame(height: EditorLayout.wideTimeline(geometry.size.height))
                                // A barra de adicionar na base da coluna do palco.
                                if model.sheetContent == .addBar { ShellAddBar().frame(height: StageDim.addBar) }
                            }.frame(maxWidth: .infinity)
                            ContextSheet(metrics: metrics).frame(width: sideWidth, height: max(1, geometry.size.height - EditorLayout.topBar))
                        }
                    }
                } else {
                    VStack(spacing: 0) {
                        if !model.fullscreen { TopBarView(height: metrics.topBar) }
                        PreviewStage(height: max(0, metrics.preview - (model.fullscreen ? StageDim.fullscreenTimeBar : 0)))
                            .padding(.horizontal, sideMargin)
                        if model.fullscreen {
                            FullscreenTimeBar()
                            TransportView().frame(height: metrics.transport)
                        } else {
                            // A divisa palco/transporte: arrastar na vertical no transporte
                            // (fora dos botões) troca palco por timeline; toque duplo volta à
                            // altura automática. A faixa de 8 saiu (strip 0) — o gesto ficou.
                            VStack(spacing: 0) {
                                if metrics.strip > 0 {
                                    Rectangle().fill(AureaColors.editorCanvas).frame(height: metrics.strip)
                                        .overlay(Capsule().fill(AureaColors.muted.opacity(0.55)).frame(width: 36, height: 3))
                                }
                                TransportView().frame(height: metrics.transport)
                            }
                            .contentShape(Rectangle())
                            // `.contain` antes do identificador: sem ele o id da divisa
                            // sobrescrevia o dos botões do transporte (ex.: `transport.duplicate`).
                            .accessibilityElement(children: .contain)
                            .accessibilityIdentifier("editor.previewDivider")
                            .simultaneousGesture(DragGesture(minimumDistance: 8, coordinateSpace: .global)
                                .onChanged { value in
                                    if dividerStart == nil {
                                        guard abs(value.translation.height) > abs(value.translation.width) else { return }
                                        dividerStart = metrics.preview
                                    }
                                    guard let start = dividerStart else { return }
                                    previewPreference = Double((start + value.translation.height)
                                        .clamped(to: EditorLayout.previewMin...EditorLayout.maxPreview(geometry.size.height)))
                                }
                                .onEnded { _ in dividerStart = nil })
                            .simultaneousGesture(TapGesture(count: 2).onEnded { previewPreference = 0 })
                        }
                        if metrics.timeline > 0 { TimelineView(compactDock: true, timecodeStyle: model.selection.count == 1 ? .box : .underline).frame(height: metrics.timeline) }
                        if metrics.sheet > 0 {
                            if model.sheetContent == .addBar { ShellAddBar().frame(height: metrics.sheet) }
                            else { ContextSheet(metrics: metrics).frame(height: metrics.sheet) }
                        }
                    }
                }
            }
            .background(AureaColors.editorCanvas.ignoresSafeArea())
        }
        .fullScreenCover(isPresented: $model.showExport) { ExportView() }
        .fullScreenCover(item: $model.effectSearch) { request in
            // A tela "Adicionar efeito" (ou só a busca): EffectsBrowser.swift.
            EffectAddSheet(request: request, close: { model.effectSearch = nil })
                .environmentObject(model)
        }
        .sheet(item: $model.textContentRequest) { request in
            TextContentEditor(request: request).environmentObject(model)
                .presentationDetents([.large]).interactiveDismissDisabled()
        }
        .sheet(item: $model.markerEditingFrame) { marker in
            MarkerEditorSheet(frame: marker.frame, color: marker.color, label: marker.label, isNew: marker.isNew)
                .environmentObject(model)
        }
        .overlay(alignment: .topTrailing) {
            if model.fullscreen {
                Button { model.fullscreen = false } label: {
                    CupertinoGlyph.text(CupertinoGlyph.FullscreenExit, size: 24)
                        .frame(width: 40, height: 40).background(StageInk.floatingDark, in: Circle())
                }.buttonStyle(.plain).accessibilityLabel(AureaText.t("editor_voltar_editor")).padding(10)
            }
        }
        .overlay { if model.showAddLayer && !model.sceneEditor { ShellAddCategoryDialog() } }
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
    }
    /// Arquivo (modelo 3D, texturas/.mtl, SVG, PSD, áudio): seletor do UIKit
    /// pelo controlador do topo (DocumentImportPicker). Antes o modelo vinha
    /// num seletor embutido num `fullScreenCover` e o resto no `.fileImporter`
    /// da raiz — no iPhone com iOS 26/27 o embutido fechava sozinho ao escolher
    /// e "nada importava"; o `.fileImporter` da raiz calava os de baixo.
    private func presentFilePicker(_ kind: ShellAddPicker) {
        let project = pickerProject
        let model = self.model
        let multiple = kind == .model || kind == .modelTextures
        DocumentImportPicker.present(types: Self.fileTypes(kind), multiple: multiple) { urls in
            guard project == model.projectGeneration, let urls, let url = urls.first else { return }
            switch kind {
            // Seleção múltipla/pasta: o FBX/OBJ/glTF vem com as texturas, o .bin e o .mtl.
            case .model: model.importModelFiles(urls: urls)
            case .modelTextures: model.importModelTextures(urls: urls)
            case .svg: model.importSvg(url: url)
            case .psd: model.importPsd(url: url)
            default: model.importMedia(url: url, kind: .audio)
            }
        }
    }
    private static func fileTypes(_ kind: ShellAddPicker) -> [UTType] {
        switch kind {
        case .svg: return [UTType(filenameExtension: "svg") ?? .data, .data]
        case .psd: return [UTType(filenameExtension: "psd") ?? .data, .data]
        case .model: return DocumentImportPicker.modelTypes
        // .mtl (texto), .bin e texturas: alguns provedores não marcam public.data.
        case .modelTextures: return DocumentImportPicker.modelTypes + [.image, .text]
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
@MainActor private struct PreviewStage: View {
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
                .overlay { if !model.fullscreen && !model.rawPlayback { RigStageOverlay() } }
                .overlay { if !model.fullscreen && !model.rawPlayback { MeshWarpStageOverlay() } }
                .overlay { if !model.fullscreen && !model.rawPlayback { PuppetStageOverlay() } }
                .overlay { if !model.fullscreen && !model.rawPlayback { RotoPaintStageOverlay() } }
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
            if !model.fullscreen && !model.rawPlayback, model.selection.count == 1, let id = model.primarySelection {
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
            // Zoom da vista: "250 %" só com a prévia ampliada; tocar volta a 100 %.
            StageZoomChip().padding(8).frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottomTrailing)
            // Lupa da prévia no canto sup-esq (redesenho 2026-09-29), nos dois estados.
            if !model.rawPlayback { StageZoomCornerButton().frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading) }
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
            if model.sceneEditor {
                let lines = model.engine.sceneGuides().map(\.floatValue)
                for i in stride(from: 0, to: lines.count, by: 5) {
                    var line = Path(); line.move(to: screen(lines[i], lines[i + 1])); line.addLine(to: screen(lines[i + 2], lines[i + 3]))
                    let color: Color = lines[i + 4] == 1 ? .yellow : lines[i + 4] == 2 ? .cyan : .gray.opacity(0.22)
                    context.stroke(line, with: .color(color), lineWidth: 1)
                }
                if TrackballOverlay.draw(&context, model: model, screen: screen) {
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
            if gizmo.count == 8 && !TrackballOverlay.draw(&context, model: model, screen: screen) {
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
        let count = mask.points.count / 6, affine = model.maskAffine
        guard count > 0, affine.count == 6 else { return }
        func point(_ index: Int, _ handle: Int = 0) -> CGPoint {
            let x = mask.points[index * 6] + (handle == 0 ? 0 : mask.points[index * 6 + handle])
            let y = mask.points[index * 6 + 1] + (handle == 0 ? 0 : mask.points[index * 6 + handle + 1])
            return screen(affine[0] * x + affine[2] * y + affine[4], affine[1] * x + affine[3] * y + affine[5])
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
private struct ContextSheet: View {
    @EnvironmentObject private var model: AureaModel
    let metrics: EditorMetrics

    var body: some View {
        VStack(spacing: 0) {
            // ContextArea.kt: faixa VAZIA de 12; painel aberto começa no seu
            // próprio cabeçalho, sem puxador ou faixa adicionais.
            Rectangle().fill(AureaColors.border).frame(height: 1)
            if !EditorTimelineRefresh.enabled && model.sheetContent != .panel && model.sheetContent != .curve {
                AureaColors.editorPanelHigh.frame(height: StageDim.sheetHandle)
            }

            if model.showAddLayer {
                AddLayerSheet()
            } else if model.selection.count > 1 && model.panel != .aiVideo && model.panel != .captions {
                BatchToolsView()
            } else {
            switch model.panel {
            case .none, .dock:
                DockView()
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
                ShapePanel()
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
            }
            }
        }
        .background(AureaColors.editorPanel)
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
            if start { model.trimStart(row.id, at: model.status.playhead) }
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

private struct DockView: View {
    @EnvironmentObject private var model: AureaModel

    fileprivate enum Section: String {
        // Máscara, rastreio de câmera e legendas automáticas viraram EFEITOS
        // (seletor de efeitos); os presets de vídeo/imagem saíram. Os presets de
        // TEXTO voltaram (2026-10-03): ficha só no texto 2D/3D.
        case enterGroup, mask, color, shape, vector, editText, text, text3DOptions, particles, audio, mute, speed, move, rig, blend, environment, presets, effects
        var label: String {
            switch self {
            case .enterGroup: return "editor_entrar_grupo"
            case .mask: return "panel_mascara_recorte"
            case .color: return "sh_dock_color_fill"
            case .shape: return "sh_dock_edit_shape"
            case .vector: return "sh_dock_edit_vector"
            case .editText: return "sh_dock_edit_text"
            case .text, .text3DOptions: return "text_options"
            case .particles: return "sh_dock_particles"
            case .audio: return "sh_add_tab_audio"
            case .mute: return "editor_mute_audio"
            case .speed: return "editor_velocidade"
            case .move: return "sh_dock_transform"
            case .rig: return "fx_name_puppet"
            case .blend: return "sh_dock_opacity_blend"
            case .environment: return "sh_dock_environment"
            case .presets: return "sh_dock_presets"
            case .effects: return "sh_dock_effects"
            }
        }
        /// Short visible action; accessibility continues to use the full label.
        var shortLabel: String {
            switch self {
            case .mask: return "sh_dock_mask"
            case .move: return "gizmo_tool_move"
            case .blend: return "panel_misturar"
            default: return label
            }
        }
        var glyph: Character {
            switch self {
            case .enterGroup: return CupertinoGlyph.ArrowDownRightSquare
            case .mask: return CupertinoGlyph.Crop
            case .color: return CupertinoGlyph.Paintbrush
            case .shape: return ShellGlyph.SliderHorizontalBelowRectangle
            case .vector: return CupertinoGlyph.PencilOutline
            case .editText: return CupertinoGlyph.Textformat
            case .text, .text3DOptions: return ShellGlyph.SliderHorizontalBelowRectangle
            case .particles, .effects: return CupertinoGlyph.Sparkles
            case .audio: return CupertinoGlyph.Speaker2
            case .mute: return CupertinoGlyph.SpeakerSlash
            case .speed: return CupertinoGlyph.Speedometer
            case .move: return CupertinoGlyph.Move
            case .rig: return CupertinoGlyph.PersonCropCircle
            case .blend: return CupertinoGlyph.CircleLefthalfFill
            case .environment: return CupertinoGlyph.Lightbulb
            case .presets: return CupertinoGlyph.WandStars
            }
        }
        var panel: AureaModel.PanelKind {
            switch self {
            case .enterGroup: return .transform
            case .mask: return .mask
            case .color: return .shape
            case .shape: return .shapeEdit
            case .vector: return .vector
            case .editText, .text: return .text
            case .text3DOptions: return .layer3D
            case .particles: return .particles
            case .audio, .mute: return .audio
            case .speed: return .speed
            case .move, .rig: return .transform   // rig: não abre painel (RigStage.swift)
            case .blend: return .appearance
            case .environment: return .layer3D
            case .presets: return .presets
            case .effects: return .effects
            }
        }
    }
    private var hasAudio: Bool { ((model.detail["audioFlags"] as? NSNumber)?.uint32Value ?? 0) & 4 != 0 }
    private var muted: Bool { ((model.detail["audioFlags"] as? NSNumber)?.uint32Value ?? 0) & 1 != 0 }
    /// As fichas do TIPO, enxutas e na ordem de uso (par do `sectionsFor` do
    /// Android): o que é próprio do tipo primeiro, depois Transformar, Efeitos e
    /// Opacidade/mesclagem. Aparar e dividir moram na fileira rápida;
    /// som e velocidade ficam visíveis na mídia; as operações raras ficam no ⋯.
    private var sections: [Section] { Self.sections(model) }
    fileprivate static func sections(_ model: AureaModel) -> [Section] {
        guard let layer = model.selectedLayer else { return [] }
        let hasAudio = ((model.detail["audioFlags"] as? NSNumber)?.uint32Value ?? 0) & 4 != 0
        let media: [Section] = layer.kind == 3 ? [.mute, .speed]
            : layer.kind == 1 ? (hasAudio ? [.mute, .speed] : [.speed]) : []
        if layer.adjustment || layer.kind == 7 { return [.effects, .blend] }
        if EditorTimelineRefresh.enabled && layer.kind == 12 { return [.enterGroup, .move, .mask, .effects, .blend] }
        if EditorTimelineRefresh.enabled && ![3, 6, 8, 9, 10].contains(layer.kind) {
            let edit: Section? = layer.kind == 5 ? (model.isVectorLayer ? .vector : .shape)
                : layer.kind == 4 ? .editText : layer.kind == 11 ? .particles : nil
            let content: [Section] = layer.kind == 4 ? [.editText, .text] : (edit.map { [$0] } ?? [])
            return media + content + [.move, .mask, .effects, .blend] +
                (layer.kind == 1 && hasAudio ? [.audio] : []) + (layer.kind == 4 ? [.presets] : [])
        }
        // Frequent actions stay before the horizontally scrollable extras.
        let common: [Section] = [.move, .effects, .blend]
        switch layer.kind {
        case 5: return model.isVectorLayer ? [Section.vector] + common : [Section.shape] + common + [Section.color]
        case 4: return [Section.editText, Section.text] + common + [Section.presets]
        case 1: return media + common + (hasAudio ? [.audio] : [])
        case 2: return common + [Section.rig]
        case 12: return common
        case 3: return media + [.audio, .effects]
        case 10: return (model.engine.text3D(forLayer: layer.id) ?? [:]).isEmpty
            ? [.move, .environment, .effects, .blend]
            : [Section.editText, Section.text3DOptions] + common + [Section.presets]
        case 8: return [.move, .environment]
        case 11: return [Section.particles] + common
        case 9: return layer.effectCount > 0 ? common : [.move]
        case 6: return [.move]
        default: return []
        }
    }
    /// Três cortes contextuais e uma fileira rolável de ferramentas.
    fileprivate static func tileRows(_ model: AureaModel) -> Int { 1 }

    var body: some View {
        if let layer = model.selectedLayer {
            // Superfície compacta, com rolagem quando a área disponível é curta.
            let fontScale = UIFont.preferredFont(forTextStyle: .body).pointSize / 17
            ScrollView(.vertical, showsIndicators: false) {
            VStack(spacing: 8) {
                HStack(spacing: 8) {
                    let actions = model.engine.queryClipTimeActions(layer.id, frame: model.status.playhead)
                    HStack(spacing: 0) {
                        if actions & 8 != 0 {
                            layerTimeAction(CupertinoGlyph.ArrowLeftToLine, "timeline_extend_left", "editor_extend_start_to_playhead", "timeline.extend.start", enabled: !layer.locked) {
                                extendEdit(layer, start: true)
                            }
                        } else if actions & 16 != 0 {
                            layerTimeAction(CupertinoGlyph.ArrowRightToLine, "timeline_extend_right", "editor_extend_end_to_playhead", "timeline.extend.end", enabled: !layer.locked) {
                                extendEdit(layer, start: false)
                            }
                        } else if actions != 0 || EditorTimelineRefresh.enabled {
                            trimAction(.start, "editor_aparar_inicio_cabecote") { timeEdit(layer) { if !model.trimStart(layer.id, at: model.status.playhead) { model.toast = AureaText.t("timeline_cut_failed") } } }
                            dockDivider
                            trimAction(.split, "editor_dividir_cabecote") { timeEdit(layer) { model.splitAtPlayhead([layer.id]) } }
                            dockDivider
                            trimAction(.end, "editor_aparar_fim_cabecote") { timeEdit(layer) { if !model.trimEnd(layer.id, at: model.status.playhead) { model.toast = AureaText.t("timeline_cut_failed") } } }
                        }
                        if EditorTimelineRefresh.enabled {
                            dockDivider
                            quickAction(CupertinoGlyph.Trash, "editor_excluir_camada", tint: AureaColors.danger) { model.deleteSelectedLayers() }
                        }
                    }.frame(maxWidth: .infinity, maxHeight: .infinity)
                        .background(StageInk.dockRow, in: RoundedRectangle(cornerRadius: EditorTimelineRefresh.enabled ? 24 : 10))
                }.frame(height: EditorLayout.dockQuick)
                GeometryReader { geometry in
                    let gap: CGFloat = EditorTimelineRefresh.enabled ? 4 : 8
                    let minimum = CGFloat(EditorTimelineRefresh.enabled ? 60 : 76) + 28 * (min(max(fontScale, 1), 2) - 1)
                    let available = geometry.size.width - (EditorTimelineRefresh.enabled ? 49 + gap : 0)
                    let fitted = (available - gap * CGFloat(max(0, sections.count - 1))) / CGFloat(max(1, sections.count))
                    let toolWidth = min(max(fitted, minimum), 160)
                    ScrollView(.horizontal, showsIndicators: true) {
                    HStack(spacing: gap) {
                        if EditorTimelineRefresh.enabled {
                            Button { model.clearSelection() } label: {
                                CupertinoGlyph.text(CupertinoGlyph.ChevronLeft, size: 20, color: AureaColors.text)
                                    .frame(width: 48, height: EditorLayout.dockTileHeight(fontScale: fontScale))
                            }.buttonStyle(.plain).accessibilityLabel(AureaText.t("editor_limpar_selecao"))
                                .accessibilityIdentifier("trial.layer.deselect")
                            dockDivider
                        }
                        ForEach(sections, id: \.rawValue) { section in
                            sectionTile(section, width: toolWidth, fontScale: fontScale)
                        }
                    }
                    }
                    .id(layer.id)
                    .accessibilityIdentifier("dock.tools")
                }
                .frame(height: CGFloat(Self.tileRows(model)) * EditorLayout.dockTileHeight(fontScale: fontScale) + CGFloat(Self.tileRows(model) - 1) * 8)
            }
            .padding(.horizontal, 8).padding(.top, 4).padding(.bottom, 8)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(EditorTimelineRefresh.enabled ? AureaColors.editorCanvas : StageInk.dockSheet, in: DockSheetShape(radius: 12))
            .padding(.top, 4)
        } else {
            Text(AureaText.t("editor_toque_num_objeto_tela_editar"))
                .font(.aurea(size: 12.5)).foregroundStyle(AureaColors.muted).lineLimit(1)
                .frame(maxWidth: .infinity, maxHeight: .infinity).padding(.horizontal, 16)
                .background(AureaColors.editorPanel)
        }
    }

    private func sectionTile(_ section: Section, width: CGFloat, fontScale: CGFloat) -> some View {
        let isMute = section == .mute
        let title = isMute && muted ? "editor_unmute_audio" : section.label
        return Button {
            if isMute {
                guard let layer = model.selectedLayer else { return }
                guard !layer.locked else { model.toast = AureaText.t("editor_camada_bloqueada_desbloqueie_editar"); return }
                let next = !muted
                model.mutate { $0.setLayer(layer.id, audioMuted: next) }
                model.refreshModel(force: true)
            }
            else if section == .enterGroup, let layer = model.selectedLayer { model.openGroup(layer.id) }
            else if section == .editText { model.openTextContentEditor() }
            // O antigo Rig abre o Fantoche (pinos); o rig gravado continua desenhando.
            else if section == .rig { model.openPuppetTool() }
            else { model.openPanel(section.panel) }
        } label: {
            VStack(spacing: 4) {
                if section == .move {
                    MaterialGlyph("rounded.OpenWith", size: 20, color: StageInk.dockTileIcon)
                } else {
                    CupertinoGlyph.text(isMute && !muted ? CupertinoGlyph.Speaker2 : section.glyph,
                        size: 20, color: isMute && muted ? AureaColors.accent : StageInk.dockTileIcon)
                }
                Text(AureaText.t(isMute ? title : section.shortLabel))
                    .font(.aurea(size: 12 * min(max(fontScale, 1), 2)))
                    .foregroundStyle(StageInk.dockTileContent).lineLimit(2).multilineTextAlignment(.center)
            }
            .padding(.horizontal, 6).padding(.vertical, 4).frame(width: width, height: EditorLayout.dockTileHeight(fontScale: fontScale))
            .background(EditorTimelineRefresh.enabled ? AureaColors.editorCanvas : StageInk.dockSheet, in: RoundedRectangle(cornerRadius: 10))
            .contentShape(RoundedRectangle(cornerRadius: 10))
        }.buttonStyle(.plain)
            .accessibilityLabel(AureaText.t(title))
            .accessibilityValue(isMute ? AureaText.t(muted ? "panel_mudo" : "editor_audio_enabled") : "")
            .accessibilityIdentifier("dock.tool.\(section.rawValue)")
    }


    /// Quadrado da fileira rápida para uma ferramenta sozinha (velocidade, mudo, grupo).
    private func dockSquare<Content: View>(@ViewBuilder _ content: () -> Content) -> some View {
        content()
            .frame(width: EditorLayout.dockQuick, height: EditorLayout.dockQuick)
            .background(StageInk.dockRow, in: RoundedRectangle(cornerRadius: 10))
    }
    private var dockDivider: some View {
        Rectangle().fill(AureaColors.border).frame(width: 1, height: 20)
    }

    private func layerTimeAction(_ glyph: Character, _ title: String, _ description: String, _ id: String,
                                 enabled: Bool = true, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            VStack(spacing: 0) {
                CupertinoGlyph.text(glyph, size: 19, color: enabled ? AureaColors.text : StageInk.dockDisabled)
                Text(AureaText.t(title)).font(.aurea(size: 10)).lineLimit(2).multilineTextAlignment(.center)
                    .foregroundStyle(enabled ? AureaColors.text : StageInk.dockDisabled)
            }.frame(maxWidth: .infinity, minHeight: 48).contentShape(Rectangle())
        }.buttonStyle(.plain).disabled(!enabled)
            .accessibilityLabel(AureaText.t(description)).accessibilityIdentifier(id)
    }

    private func extendEdit(_ layer: LayerItem, start: Bool) {
        guard !layer.locked else { model.toast = AureaText.t("editor_camada_bloqueada_desbloqueie_editar"); return }
        if model.status.playing != 0 { model.playPause() }
        let changed = model.extendToPlayhead(layer.id, start: start)
        if !changed { model.toast = AureaText.t("timeline_cut_failed") }
    }
    private func timeEdit(_ layer: LayerItem, action: () -> Void) {
        guard !layer.locked else { model.toast = AureaText.t("editor_camada_bloqueada_desbloqueie_editar"); return }
        guard model.status.playhead > Int64(layer.startFrame), model.status.playhead < Int64(layer.endFrame) else { model.toast = AureaText.t("sh_playhead_into_layer"); return }
        if model.status.playing != 0 { model.playPause() }
        action()
    }
    /// Os três colchetes da fileira de tempo (desenho próprio, no jeito do AM;
    /// mesma geometria do `DockTrimTool` do Android).
    private enum TrimGlyph { case start, split, end }
    private func trimAction(_ kind: TrimGlyph, _ key: String, action: @escaping () -> Void) -> some View {
        VStack(spacing: 0) {
        Canvas { context, size in
            let u = size.width / 24
            let stroke = 1.8 * u
            func bracket(_ outer: CGFloat, _ inner: CGFloat, dashed: Bool) {
                var path = Path()
                path.move(to: CGPoint(x: outer * u, y: 6 * u)); path.addLine(to: CGPoint(x: inner * u, y: 6 * u))
                path.addLine(to: CGPoint(x: inner * u, y: 18 * u)); path.addLine(to: CGPoint(x: outer * u, y: 18 * u))
                context.stroke(path, with: .color(AureaColors.text),
                               style: StrokeStyle(lineWidth: stroke, lineCap: .round, lineJoin: .round, dash: dashed ? [2.2 * u, 2.0 * u] : []))
            }
            bracket(3, 9, dashed: kind == .start)
            bracket(21, 15, dashed: kind == .end)
            var mid = Path()
            mid.move(to: CGPoint(x: 12 * u, y: 3.5 * u)); mid.addLine(to: CGPoint(x: 12 * u, y: 20.5 * u))
            context.stroke(mid, with: .color(AureaColors.text), style: StrokeStyle(lineWidth: stroke, lineCap: .round))
        }
        .frame(width: 22, height: 22)
        if !EditorTimelineRefresh.enabled { Text(AureaText.t(kind == .start ? "timeline_cut_left" : kind == .end ? "timeline_cut_right" : "dock_short_split"))
            .font(.aurea(size: 10)).lineLimit(1) }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity).contentShape(Rectangle())
        .onTapGesture(perform: action)
        .accessibilityIdentifier(kind == .start ? "timeline.cut.start" : kind == .end ? "timeline.cut.end" : "timeline.cut.split")
        .accessibilityElement().accessibilityLabel(AureaText.t(key)).accessibilityAddTraits(.isButton)
    }
    /// Só o ícone, como na fileira de tempo do editor antigo; a descrição
    /// completa fica no leitor de tela.
    private func quickAction(_ glyph: Character, _ key: String, size: CGFloat = 19, tint: Color = AureaColors.text,
                             hold: (() -> Void)? = nil, action: @escaping () -> Void) -> some View {
        CupertinoGlyph.text(glyph, size: size, color: tint)
            .frame(maxWidth: .infinity, maxHeight: .infinity).contentShape(Rectangle())
            .onTapGesture(perform: action).onLongPressGesture(minimumDuration: 0.45) { hold?() }
            .accessibilityLabel(AureaText.t(key)).accessibilityAddTraits(.isButton)
    }
}

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
                        Button { dismissEditing(); fonts = model.engine.availableFonts(); showingFonts = true } label: {
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
        guard ["ttf", "otf"].contains(url.pathExtension.lowercased()) else {
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
        FontImportPicker.registerWithCoreText(destination)
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
        [UTType.font] + ["ttf", "otf"].compactMap { UTType(filenameExtension: $0) }
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
/// PSD, HDRI, LUT, legenda, Alight Motion): o `UIDocumentPickerViewController`
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
    static func present(types: [UTType], multiple: Bool = false, _ picked: @escaping ([URL]?) -> Void) {
        let windows = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.flatMap(\.windows)
        guard var presenter = (windows.first(where: \.isKeyWindow) ?? windows.first)?.rootViewController else {
            NSLog("Aurea file picker: sem janela para apresentar")
            picked(nil); return
        }
        while let next = presenter.presentedViewController, !next.isBeingDismissed { presenter = next }
        // `.item` no fim: provedor que não marca public.data (FBX/OBJ/MTL/GLB
        // sem tipo registrado) não fica cinza no seletor; o import valida.
        var all = types
        if !all.contains(.item) { all.append(.item) }
        let picker = UIDocumentPickerViewController(forOpeningContentTypes: all, asCopy: true)
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

private struct TextContentEditor: View {
    @EnvironmentObject private var model: AureaModel
    let request: AureaModel.TextContentRequest
    @State private var content: String
    @State private var selection: NSRange
    @State private var editing = false
    @State private var failed = false
    init(request: AureaModel.TextContentRequest) {
        self.request = request
        _content = State(initialValue: request.content)
        let length = (request.content as NSString).length
        _selection = State(initialValue: NSRange(location: request.selectAll ? 0 : length, length: request.selectAll ? length : 0))
    }
    private var valid: Bool { !request.is3D || !content.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty }
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text(AureaText.t("sh_dock_edit_text")).font(.aurea(size: 18, weight: .semibold))
                Spacer()
                Button(AureaText.t("common_cancel")) { model.textContentRequest = nil }
                    .frame(minHeight: 44).accessibilityIdentifier("text.content.cancel")
                Button(AureaText.t("editor_concluir")) {
                    failed = !model.commitTextContent(request, content: content)
                }.frame(minHeight: 44).disabled(!valid).accessibilityIdentifier("text.content.done")
            }
            NativeTextInput(text: $content, selection: $selection, editing: $editing,
                            autoFocus: true, selectAllOnFocus: request.selectAll, scrollable: true, alignment: request.alignment) { _ in failed = false }
                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
                .accessibilityIdentifier("text.content.input")
            if !valid { Text(AureaText.t("text_content_3d_required")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted) }
            if failed { Text(AureaText.t("text_content_save_failed")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted) }
        }.padding(18).foregroundStyle(AureaColors.text).tint(AureaColors.accent)
            .background(AureaColors.editorPanel.ignoresSafeArea())
    }
}

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
        if scrollable { return CGSize(width: width, height: max(96, proposal.height ?? 240)) }
        let result = uiView.sizeThatFits(CGSize(width: width, height: .greatestFiniteMagnitude))
        return CGSize(width: width, height: max(56, result.height))
    }
    final class Coordinator: NSObject, UITextViewDelegate {
        var parent: NativeTextInput
        init(_ parent: NativeTextInput) { self.parent = parent }
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
                    case 2:
                        card("editor_musica_ou_som", glyph: CupertinoGlyph.MusicNote, accent: true) { files(.audioFile) }
                        card("sh_add_video_sound", glyph: CupertinoGlyph.Film) { photos(.audioFromVideo) }
                        card("sh_add_marker_at_playhead", glyph: CupertinoGlyph.Bookmark) { close(); model.toggleMarkerAt(model.status.playhead) }
                    case 3:
                        // Legendas automáticas viraram efeito (seletor de efeitos da camada de fala).
                        card("sh_add_tab_text", glyph: CupertinoGlyph.Textformat, accent: true) { model.addText(); close() }
                    case 4:
                        drawnCard("sh_add_null", kind: -1) { model.addNull(threeD: false); close() }
                        card("particular_title", glyph: CupertinoGlyph.Sparkles, color: ShellColors.text3D) { model.addParticles(20); close() }   // 20 = Particular (preset Padrão)
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
                        card("panel_camera_3d", glyph: CupertinoGlyph.CameraFill, accent: true) { model.addCamera(); close() }
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
    private func photos(_ kind: ShellAddPicker) { request(kind) }
    private func files(_ kind: ShellAddPicker) { request(kind) }
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

/// Cena 3D "seca" (par do SceneLayoutWorkspace.kt): a cena ocupa a tela e se
/// mexe com o dedo — arrastar o objeto move, 1 dedo no vazio gira a vista,
/// pinça aproxima, toque duplo recentra (ver `sceneEvent`). Sem sliders nem
/// campos XYZ: voltar, desfazer, adicionar, trocar de objeto e, conforme o
/// escolhido, material ou luz. A órbita é só da prévia.
@MainActor private struct SceneLayoutWorkspace: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    let height: CGFloat
    @State private var materials = false
    @State private var lights = false
    @State private var hint = true
    var body: some View {
        ZStack {
            PreviewStage(height: height)
            VStack(spacing: 0) {
                HStack(spacing: 6) {
                    roundButton(CupertinoGlyph.ChevronLeft, "editor_voltar_editor") { model.exitSceneEditor() }
                    Text(AureaText.t("scene_workspace")).font(.aurea(size: 15, weight: .semibold)).foregroundStyle(AureaColors.text)
                        .padding(.leading, 2)
                    Spacer()
                    roundButton(CupertinoGlyph.ArrowUturnLeft, "editor_desfazer") { model.undo() }
                    roundButton(CupertinoGlyph.ArrowUturnRight, "editor_refazer") { model.redo() }
                }.padding(.horizontal, 8).padding(.top, 6)
                Spacer()
                if hint {
                    Text(AureaText.t("scene_hint")).font(.aurea(size: 12)).foregroundStyle(AureaColors.text)
                        .multilineTextAlignment(.center).padding(.horizontal, 12).padding(.vertical, 8)
                        .background(StageInk.floatingDark, in: RoundedRectangle(cornerRadius: 12))
                        .padding(.horizontal, 24).padding(.bottom, 8).transition(.opacity).allowsHitTesting(false)
                }
                let objects = model.layers.filter { $0.threeD }
                if !objects.isEmpty {
                    ScrollView(.horizontal, showsIndicators: false) {
                        HStack(spacing: 6) {
                            ForEach(objects) { row in
                                let on = model.primarySelection == row.id
                                Button { model.select(layerId: row.id, additive: false) } label: {
                                    Text(row.name).font(.aurea(size: 13)).lineLimit(1)
                                        .foregroundStyle(on ? Color.black : AureaColors.text)
                                        .padding(.horizontal, 12).padding(.vertical, 7)
                                        .background(on ? AureaColors.accent.opacity(0.9) : StageInk.floatingDark, in: Capsule())
                                }.buttonStyle(.plain)
                            }
                        }.padding(.horizontal, 12)
                    }.padding(.bottom, 8)
                }
                HStack(spacing: 8) {
                    Menu {
                        Button(AureaText.t("sh_add_text_3d")) { model.addText3D(content: AureaText.t("panel_texto"), depth: 0.25) }
                        Button(AureaText.t("panel_camera_3d")) { model.addCamera() }
                        Button(AureaText.t("sh_add_null_3d")) { model.addNull(threeD: true) }
                        Button(AureaText.t("scene_light_directional")) { model.addLight(0) }
                        Button(AureaText.t("scene_light_point")) { model.addLight(1) }
                        Button(AureaText.t("scene_light_spot")) { model.addLight(2) }
                        Button(AureaText.t("sh_add_model_3d") + "…") { model.showAddLayer = true }
                    } label: { pill(CupertinoGlyph.Plus, String(AureaText.t("panel_adicionar").drop(while: { $0 == "+" || $0 == " " }))) }
                    if let layer = model.selectedLayer, layer.kind == 8 || layer.kind == 10 || (layer.kind == 4 && layer.threeD) {
                        Button { model.panel = .layer3D; materials = true } label: { pill(CupertinoGlyph.CubeFill, AureaText.t(layer.kind == 8 ? "environment_texture" : "scene_material")) }.buttonStyle(.plain)
                    }
                    if model.selectedLayer?.kind == 9 {
                        Button { lights = true } label: { pill(CupertinoGlyph.Lightbulb, AureaText.t("pn_t3d_lighting")) }.buttonStyle(.plain)
                    }
                    Button { model.resetSceneView() } label: { pill(CupertinoGlyph.ArrowCounterclockwise, AureaText.t("scene_recenter")) }.buttonStyle(.plain)
                }.padding(.bottom, 12)
            }
        }
        .frame(height: height)
        .task { try? await Task.sleep(nanoseconds: 7_000_000_000); withAnimation { hint = false } }
        .sheet(isPresented: $model.showAddLayer) { AddLayerSheet(tab: 5, inSheet: true).environmentObject(model).environmentObject(shell) }
        .sheet(isPresented: $lights) { SceneLightControls().environmentObject(model) }
        .sheet(isPresented: $materials) { Panel3DView().environmentObject(model) }
        .onChange(of: model.panel) { panel in if panel == .none { materials = false } }
    }
    private func roundButton(_ glyph: Character, _ key: String, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            CupertinoGlyph.text(glyph, size: 19).foregroundStyle(AureaColors.text)
                .mirrorsInRtl(CupertinoGlyph.mirrorsInRtl(glyph))   // voltar e desfazer/refazer viram em árabe (HIG)
                .frame(width: 40, height: 40).background(StageInk.floatingDark, in: Circle())
        }.buttonStyle(.plain).accessibilityLabel(AureaText.t(key))
    }
    private func pill(_ glyph: Character, _ label: String) -> some View {
        HStack(spacing: 6) {
            CupertinoGlyph.text(glyph, size: 16)
            Text(label).font(.aurea(size: 13, weight: .medium)).lineLimit(1)
        }.foregroundStyle(AureaColors.text).padding(.horizontal, 14).frame(height: 40)
            .background(StageInk.floatingDark, in: Capsule())
    }
}

@MainActor private struct SceneLightControls: View {
    @EnvironmentObject private var model: AureaModel
    var body: some View {
        let _ = model.status.modelRevision
        let _ = model.localPlayhead
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
    static let all: [(String, Character)] = [
        ("sh_add_tab_shape", ShellGlyph.SquareOnCircle), ("sh_add_tab_media", CupertinoGlyph.PhotoOnRectangle),
        ("sh_add_tab_audio", CupertinoGlyph.MusicNote2), ("sh_add_tab_text", CupertinoGlyph.Textformat),
        ("sh_add_tab_element", ShellGlyph.CircleGridHex), ("sh_add_tab_3d", CupertinoGlyph.Cube),
        ("sh_add_tab_draw", ShellGlyph.Scribble), ("sh_add_tab_vector", CupertinoGlyph.PencilOutline),
        // 8: presets de texto (fim da lista para não mudar os índices; na barra fica ao lado do Texto).
        ("tp_add_tab", CupertinoGlyph.WandStars)
    ]
}

/// A barra fixa de adicionar, na base do editor, no lugar do "+" (par do
/// `AddBar` do Android): sempre à vista sem camada escolhida; tocar abre o
/// painel da categoria. A aberta ganha a pílula de destaque. Itens de 64 × 64
/// (ícone 23, 4 de vão, nome 11): espalhados quando todos cabem; senão a barra
/// rola de lado. O fundo desce pela área segura de baixo.
@MainActor private struct ShellAddBar: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    var body: some View {
        GeometryReader { geometry in
            let count = CGFloat(ShellAddCategories.all.count)
            let fits = geometry.size.width / count >= StageDim.addBarItem
            let slot = fits ? geometry.size.width / count : StageDim.addBarItem
            let side = min(StageDim.addBarItem, geometry.size.height)
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 0) {
                    ForEach(ShellAddCategories.order, id: \.self) { index in
                        item(index, slot: slot, side: side, height: geometry.size.height)
                    }
                }.padding(.horizontal, fits ? 0 : StageDim.addBarItemInset)
            }.scrollDisabled(fits)
        }
        .background(AureaColors.editorBar.ignoresSafeArea(edges: .bottom))
        .overlay(alignment: .top) { Rectangle().fill(AureaColors.editorBarLine).frame(height: StageDim.hairline) }
        .accessibilityIdentifier("editor.addBar")
    }
    private func item(_ index: Int, slot: CGFloat, side: CGFloat, height: CGFloat) -> some View {
        let on = model.showAddLayer && shell.addCategory == index
        let tint = on ? AureaColors.accent : AureaColors.text
        return Button { open(index) } label: {
            VStack(spacing: StageDim.addBarItemInset) {
                CupertinoGlyph.text(ShellAddCategories.all[index].1, size: StageDim.addBarIcon, color: tint)
                Text(AureaText.t(ShellAddCategories.all[index].0)).font(.aurea(size: 11, weight: .medium))
                    .foregroundStyle(tint).lineLimit(1).minimumScaleFactor(0.8)
            }
            .frame(width: side, height: side)
            .background(on ? AureaColors.chip : Color.clear, in: RoundedRectangle(cornerRadius: AureaDims.radiusCard))
            .frame(width: slot, height: height)
            .contentShape(Rectangle())
        }.buttonStyle(.plain)
            .accessibilityLabel(AureaText.t(ShellAddCategories.all[index].0))
            .accessibilityIdentifier("aurea.add.category.\(index)")
    }
    private func open(_ index: Int) {
        // Texto tem uma peça só (legendas viraram efeito): o toque já cria o texto.
        if index == 3 {
            model.openAddLayer()   // pausa e fecha o painel aberto, como as outras categorias
            model.showAddLayer = false
            model.addText()
            return
        }
        shell.addCategory = index
        model.openAddLayer()
    }
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
