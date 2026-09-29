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
import CoreText
import Combine

struct EditorView: View {
    @EnvironmentObject private var model: AureaModel
    @StateObject private var shell = ShellPresentation()
    /// A altura do palco escolhida arrastando a divisa (0 = automática), guardada no aparelho.
    @AppStorage("editor.previewHeight") private var previewPreference: Double = 0
    @State private var dividerStart: CGFloat?
    var body: some View {
        GeometryReader { geometry in
            let wide = !model.fullscreen && model.panel != .curve && EditorLayout.isWide(geometry.size.width, geometry.size.height)
            let sideWidth = (geometry.size.width * 0.4).clamped(to: 280...380)
            let aspect: CGFloat = model.compositionHeight > 0 ? CGFloat(model.compositionWidth) / CGFloat(model.compositionHeight) : 0
            let metrics = EditorLayout.solve(total: geometry.size.height, content: model.sheetContent, fullscreen: model.fullscreen,
                                             width: geometry.size.width, aspect: aspect, preferred: CGFloat(previewPreference))
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
                                Rectangle().fill(AureaColors.editorPanelHigh).frame(height: EditorLayout.strip)
                                TransportView().frame(height: EditorLayout.transport)
                                TimelineView().frame(height: EditorLayout.wideTimeline(geometry.size.height))
                                // A barra de adicionar na base da coluna do palco.
                                if model.sheetContent == .addBar { ShellAddBar().frame(height: StageDim.addBar) }
                            }.frame(maxWidth: .infinity)
                            ContextSheet(metrics: metrics).frame(width: sideWidth, height: max(1, geometry.size.height - EditorLayout.topBar))
                        }
                    }
                } else {
                    VStack(spacing: 0) {
                        if !model.fullscreen { TopBarView().frame(height: metrics.topBar) }
                        PreviewStage(height: max(0, metrics.preview - (model.fullscreen ? StageDim.fullscreenTimeBar : 0)))
                        if model.fullscreen {
                            FullscreenTimeBar()
                            TransportView().frame(height: metrics.transport)
                        } else {
                            // A divisa palco/transporte: arrastar na vertical (na faixa ou
                            // no transporte fora dos botões) troca palco por timeline;
                            // toque duplo volta à altura automática.
                            VStack(spacing: 0) {
                                Rectangle().fill(AureaColors.editorPanelHigh).frame(height: metrics.strip)
                                    .overlay(Capsule().fill(AureaColors.muted.opacity(0.55)).frame(width: 36, height: 3))
                                TransportView().frame(height: metrics.transport)
                            }
                            .contentShape(Rectangle())
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
                        if metrics.timeline > 0 { TimelineView(compactDock: true).frame(height: metrics.timeline) }
                        if metrics.sheet > 0 {
                            if model.sheetContent == .addBar { ShellAddBar().frame(height: metrics.sheet) }
                            else { ContextSheet(metrics: metrics).frame(height: metrics.sheet) }
                        }
                    }
                }
            }
            .background(AureaColors.background.ignoresSafeArea())
            // O "+" saiu: adicionar mora na barra fixa de baixo (`ShellAddBar`).
        }
        .fullScreenCover(isPresented: $model.showExport) { ExportView() }
        .fullScreenCover(item: $model.effectSearch) { request in
            EffectPickerSearch(prefs: request.prefs, sorted: request.sorted,
                               onPick: { entry in model.effectSearch = nil; request.onPick(entry) },
                               onFavorite: request.onFavorite, onDismiss: { model.effectSearch = nil })
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
        .modifier(ModelTexturesPrompt())
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
    @State private var importing = false
    @State private var fileKind = ShellAddPicker.model
    @State private var choosingPhotos = false
    @State private var photoKind = ShellAddPicker.gallery
    func body(content: Content) -> some View {
        content
            .onChange(of: shell.addPicker) { request in
                guard let request else { return }
                shell.addPicker = nil
                if request.isFile { fileKind = request; importing = true }
                else { photoKind = request; choosingPhotos = true }
            }
            .fileImporter(isPresented: $importing, allowedContentTypes: fileTypes, allowsMultipleSelection: fileKind == .model) { result in
                guard case .success(let urls) = result, let url = urls.first else { return }
                switch fileKind {
                // Seleção múltipla/pasta: o FBX/OBJ vem com as texturas e o .mtl.
                case .model: model.importModelFiles(urls: urls)
                case .svg: model.importSvg(url: url)
                default: model.importMedia(url: url, kind: .audio)
                }
            }
            .sheet(isPresented: $choosingPhotos) {
                ShellMediaPicker(filter: photoKind == .photo ? .images : ((photoKind == .video || photoKind == .audioFromVideo) ? .videos : .any(of: [.images, .videos]))) { url, video in
                    choosingPhotos = false
                    guard let url else { return }
                    let kind: AureaModel.ImportKind = photoKind == .audioFromVideo ? .audio : (video ? .video : .image)
                    model.importMedia(url: url, kind: kind)
                }
            }
    }
    private var fileTypes: [UTType] {
        switch fileKind {
        case .svg: return [UTType(filenameExtension: "svg") ?? .data]
        case .model: return [.data, .folder] // glTF/GLB/OBJ/FBX (+ texturas, ou a pasta): o motor valida a extensão.
        default: return [.audio]
        }
    }
}

/// "Importar texturas": o modelo FBX/OBJ procura arquivos que não vieram junto.
/// O seletor abre depois que o alerta fecha (o alvo fica guardado no modelo).
private struct ModelTexturesPrompt: ViewModifier {
    @EnvironmentObject private var model: AureaModel
    @State private var picking = false
    func body(content: Content) -> some View {
        content
            .alert(AureaText.t("model_textures_title"), isPresented: Binding(
                get: { model.missingModelTextures != nil },
                set: { if !$0 { model.missingModelTextures = nil } })) {
                Button(AureaText.t("model_textures_choose")) { model.missingModelTextures = nil; picking = true }
                Button(AureaText.t("common_cancel"), role: .cancel) { model.missingModelTextures = nil }
            } message: {
                let names = model.missingModelTextures?.names ?? []
                Text(AureaText.t("model_textures_message", names.prefix(8).joined(separator: "\n") + (names.count > 8 ? "\n…" : "")))
            }
            .fileImporter(isPresented: $picking, allowedContentTypes: [.image, .data], allowsMultipleSelection: true) { result in
                guard case .success(let urls) = result else { return }
                model.importModelTextures(urls: urls)
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
            AureaColors.editorTopBar
            PreviewMetalView(compositionSize: compositionSize, interactive: !model.fullscreen && !model.rawPlayback)
                .overlay { if !model.fullscreen && !model.rawPlayback { StageOverlay().allowsHitTesting(false) } }
                .overlay { if !model.fullscreen && !model.rawPlayback { StageInteractionOverlay().allowsHitTesting(false) } }
            if model.panel == .tracking && !model.cameraFeatures.isEmpty && model.pointPick == nil { CameraTrackingOverlay() }
            if let layer = model.selectedLayer, model.selection.count == 1, layer.locked {
                ShellStageBanner(label: AureaText.t("editor_camada_bloqueada"), button: AureaText.t("editor_desbloquear"), icon: CupertinoGlyph.LockFill) {
                    model.mutate { $0.setLayer(layer.id, locked: false) }; model.refreshModel(force: true)
                }.padding(.top, 8).padding(.horizontal, 8).frame(maxHeight: .infinity, alignment: .top)
            }
            if model.panel == .vector && (model.vectorFreehand || model.vectorEditingPoints) {
                ShellStageBanner(label: vectorHint, button: AureaText.t("editor_concluir")) {
                    model.vectorFreehand = false; model.vectorEditingPoints = false; model.maskDrawing = false; model.freehandPoints = []
                }.padding(.bottom, 10).padding(.horizontal, 8).frame(maxHeight: .infinity, alignment: .bottom)
            }
            if !model.fullscreen && !model.sceneEditor {
                Text(previewLabel).font(.aurea(size: 12)).foregroundStyle(AureaColors.text)
                    .padding(.horizontal, 10).padding(.vertical, 8).background(StageInk.resolutionChip, in: RoundedRectangle(cornerRadius: 6))
                    .overlay { GeometryReader { bounds in
                        Color.clear.contentShape(Rectangle()).onTapGesture { shell.resolutionAnchor = bounds.frame(in: .global) }.onLongPressGesture(minimumDuration: 0.5) { model.toggleHud() }
                    }}.accessibilityLabel(AureaText.t("editor_resolucao_previa_segure_diagnostico"))
                    .padding(4).frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing)
            }
            if !model.fullscreen && !model.rawPlayback, model.selection.count == 1, let id = model.primarySelection {
                HStack(spacing: 6) {
                    let hasGizmo = !model.engine.gizmo(id, length: ShellStageGeometry.gizmoLength).isEmpty
                    if hasGizmo {
                        Button { model.cycleGizmoTool() } label: {
                            Text(AureaText.t(model.gizmoTool == 1 ? "gizmo_tool_rotate" : model.gizmoTool == 2 ? "gizmo_tool_scale" : "gizmo_tool_move"))
                                .foregroundStyle(AureaColors.accent)
                                .padding(.horizontal, 12).frame(minHeight: 48)
                                .background(AureaColors.editorPanelHigh, in: RoundedRectangle(cornerRadius: 8))
                        }.accessibilityLabel(AureaText.t("gizmo_tool_label")).accessibilityIdentifier("stage.gizmo.tool")
                    }
                    // Mundo/Local vale para mover; girar e escala usam os eixos da camada.
                    if hasGizmo && model.gizmoTool == 0 {
                        Button { model.gizmoLocalSpace.toggle() } label: {
                            Text(model.gizmoLocalSpace ? "Local XYZ" : "World XYZ")
                                .padding(.horizontal, 12).frame(minHeight: 48)
                                .background(AureaColors.editorPanelHigh, in: RoundedRectangle(cornerRadius: 8))
                        }.accessibilityIdentifier("stage.gizmo.space")
                    }
                    if !model.sceneEditor {
                        Button {
                            model.autoKeyTransforms.toggle()
                            model.toast = AureaText.t(model.autoKeyTransforms ? "ios_autokey_on" : "ios_autokey_off")
                        } label: {
                            Text(model.autoKeyTransforms ? "Auto-Key: On" : "Auto-Key: Off")
                                .foregroundStyle(model.autoKeyTransforms ? AureaColors.accent : AureaColors.text)
                                .padding(.horizontal, 12).frame(minHeight: 48)
                                .background(AureaColors.editorPanelHigh, in: RoundedRectangle(cornerRadius: 8))
                        }.accessibilityIdentifier("stage.autokey")
                    }
                }.font(.aurea(size: 14)).foregroundStyle(AureaColors.text).buttonStyle(.plain)
                    .padding(8).frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottomLeading)
            }
            if model.hudVisible { ShellPerfHud().padding(.leading, 8).padding(.top, 6).frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading).allowsHitTesting(false) }
        }.frame(height: height).clipped()
    }
    private var previewLabel: String {
        if model.rawPlayback { return "RAW" }
        if model.status.previewAuto != 0 { return "AUTO" }
        let n = max(1, model.status.previewNumerator), d = max(1, model.status.previewDenominator)
        return n >= d ? "Full" : "1/\(d / n)"
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
    var body: some View {
        Canvas { raw, size in
            var context = raw
            let sx = size.width / CGFloat(max(1, model.compositionWidth)), sy = size.height / CGFloat(max(1, model.compositionHeight))
            let fit = min(sx, sy)
            let origin = CGPoint(x: (size.width - CGFloat(model.compositionWidth) * fit) / 2, y: (size.height - CGFloat(model.compositionHeight) * fit) / 2)
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
                    let color: Color = lines[i + 4] == 1 ? .yellow : lines[i + 4] == 2 ? .cyan : .gray.opacity(0.35)
                    context.stroke(line, with: .color(color), lineWidth: 1)
                }
                if let selected = model.selectedLayer {
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
            if gizmo.count == 8 {
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
            if model.sheetContent != .panel && model.sheetContent != .curve {
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
        VStack(spacing: 8) {
            HStack(spacing: 0) {
                tool(CupertinoGlyph.ArrowRightToLine, "editor_aparar_inicio_cabecote") { trim(start: true) }
                tool(CupertinoGlyph.Scissors, "editor_dividir_cabecote") { split() }
                tool(CupertinoGlyph.ArrowLeftToLine, "editor_aparar_fim_cabecote") { trim(start: false) }
                AureaColors.border.frame(width: 1, height: 24)
                vectorTool("automirrored.rounded.FormatAlignLeft", "editor_alinhar_inicios") { timeAlign(0) }
                vectorTool("rounded.Stairs", "editor_escada_comeca_quando_cima_termina") { timeAlign(1) }
                vectorTool("automirrored.rounded.FormatAlignRight", "editor_alinhar_fins") { timeAlign(2) }
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
            staggerRow
            Spacer(minLength: 0)
        }.padding(.horizontal, 10).padding(.top, 4)
    }
    /// "Escalonar" (par do StaggerRow do Android): − N + quadros e dois toques
    /// que aplicam — camadas inteiras ou só keyframes — em cascata na ordem da timeline.
    @State private var staggerStep: Int = StaggerPlan.defaultStep
    private var staggerRow: some View {
        HStack(spacing: 0) {
            Text(AureaText.t("editor_escalonar")).font(.aurea(size: 12, weight: .semibold)).foregroundStyle(AureaColors.muted)
                .lineLimit(1).padding(.leading, 12).padding(.trailing, 4)
            tool(CupertinoGlyph.Minus, "editor_escalonar_menos", size: 16) { staggerStep = StaggerPlan.step(staggerStep, -1) }
            Text(AureaText.t("editor_escalonar_quadros", String(staggerStep))).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.text)
                .lineLimit(1).frame(width: 52).accessibilityIdentifier("stagger.step")
            tool(CupertinoGlyph.Plus, "editor_escalonar_mais", size: 16) { staggerStep = StaggerPlan.step(staggerStep, 1) }
            AureaColors.border.frame(width: 1, height: 24)
            staggerApply("editor_escalonar_camadas", id: "stagger.layers") { model.staggerSelection(step: staggerStep, keysOnly: false) }
            staggerApply("editor_escalonar_keyframes", id: "stagger.keys") { model.staggerSelection(step: staggerStep, keysOnly: true) }
        }.frame(height: 48).background(StageInk.dockRow, in: RoundedRectangle(cornerRadius: 10))
    }
    private func staggerApply(_ key: String, id: String, action: @escaping () -> Void) -> some View {
        // Alvo de dedo: a altura mínima mora no próprio botão (só o texto media
        // 14 pt, e o toque/leitor de tela usavam essa caixa).
        Button(action: action) {
            Text(AureaText.t(key)).font(.aurea(size: 12, weight: .semibold)).foregroundStyle(AureaColors.accent).lineLimit(1)
                .frame(maxWidth: .infinity, minHeight: 44, maxHeight: .infinity)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain).frame(maxWidth: .infinity, minHeight: 44)
        .accessibilityLabel(AureaText.t(key)).accessibilityIdentifier(id)
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
    private func timeAlign(_ mode: Int) {
        let rows = selected.filter { !$0.locked }
        guard rows.count >= 2 else { model.toast = AureaText.t("sh_pick_two_unlocked_layers"); return }
        let start = rows.map(\.startFrame).min() ?? 0, end = rows.map(\.endFrame).max() ?? 0
        var cursor = Int(rows[0].startFrame)
        pause(); model.beginGesture(mode == 0 ? "alinhar inícios" : mode == 1 ? "escada" : "alinhar fins")
        for row in rows {
            let delta = mode == 0 ? Int(start - row.startFrame) : mode == 2 ? Int(end - row.endFrame) : cursor - Int(row.startFrame)
            cursor += Int(row.endFrame - row.startFrame)
            if delta != 0 { model.moveLayers([row.id], deltaFrames: delta) }
        }
        model.endGesture()
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

    private enum Section: String {
        case color, shape, vector, editText, text, text3DOptions, particles, audio, move, blend, environment, mask, tracking, captions, presets, effects
        var label: String {
            switch self {
            case .color: return "sh_dock_color_fill"
            case .shape: return "sh_dock_edit_shape"
            case .vector: return "sh_dock_edit_vector"
            case .editText: return "sh_dock_edit_text"
            case .text, .text3DOptions: return "text_options"
            case .particles: return "sh_dock_particles"
            case .audio: return "sh_add_tab_audio"
            case .move: return "sh_dock_transform"
            case .blend: return "sh_dock_opacity_blend"
            case .environment: return "sh_dock_environment"
            case .mask: return "sh_dock_mask"
            case .tracking: return "editor_rastreio"
            case .captions: return "sh_dock_captions"
            case .presets: return "sh_dock_presets"
            case .effects: return "sh_dock_effects"
            }
        }
        var glyph: Character {
            switch self {
            case .color: return CupertinoGlyph.Paintbrush
            case .shape: return ShellGlyph.SliderHorizontalBelowRectangle
            case .vector, .mask: return CupertinoGlyph.PencilOutline
            case .editText: return CupertinoGlyph.Textformat
            case .text, .text3DOptions: return ShellGlyph.SliderHorizontalBelowRectangle
            case .particles, .effects: return CupertinoGlyph.Sparkles
            case .audio: return CupertinoGlyph.Speaker2
            case .move: return CupertinoGlyph.Move
            case .blend: return CupertinoGlyph.CircleLefthalfFill
            case .environment: return CupertinoGlyph.Lightbulb
            case .tracking: return ShellGlyph.Viewfinder
            case .captions: return CupertinoGlyph.CaptionsBubble
            case .presets: return CupertinoGlyph.WandStars
            }
        }
        var panel: AureaModel.PanelKind {
            switch self {
            case .color: return .shape
            case .shape: return .shapeEdit
            case .vector: return .vector
            case .editText, .text: return .text
            case .text3DOptions: return .layer3D
            case .particles: return .particles
            case .audio: return .audio
            case .move: return .transform
            case .blend: return .appearance
            case .environment: return .layer3D
            case .mask: return .mask
            case .tracking: return .tracking
            case .captions: return .captions
            case .presets: return .presets
            case .effects: return .effects
            }
        }
    }
    private var hasAudio: Bool { ((model.detail["audioFlags"] as? NSNumber)?.uint32Value ?? 0) & 4 != 0 }
    private var muted: Bool { ((model.detail["audioFlags"] as? NSNumber)?.uint32Value ?? 0) & 1 != 0 }
    private var sections: [Section] {
        guard let layer = model.selectedLayer else { return [] }
        if layer.adjustment || layer.kind == 7 { return [.blend, .presets, .effects] }
        switch layer.kind {
        case 5: return model.isVectorLayer ? [.vector, .move, .blend, .mask, .presets, .effects] : [.color, .shape, .move, .blend, .mask, .presets, .effects]
        case 4: return [.editText, .text, .captions, .move, .blend, .mask, .presets, .effects]
        case 1:
            return [.move] + (hasAudio ? [.audio] : []) + [.mask, .blend, .tracking] + (hasAudio ? [.captions] : []) + [.presets, .effects]
        case 2, 12: return [.move, .blend, .mask, .presets, .effects]
        case 3: return [.audio, .captions, .presets, .effects]
        case 10: return (model.engine.text3D(forLayer: layer.id) ?? [:]).isEmpty
            ? [.move, .environment, .blend, .presets, .effects]
            : [.editText, .text3DOptions, .move, .blend, .presets, .effects]
        case 8: return [.move, .environment, .presets]
        case 11: return [.particles, .move, .blend, .mask, .presets, .effects]
        case 6, 9: return [.move, .presets]
        default: return []
        }
    }

    /// Até duas fileiras, a de baixo com a metade maior (`dockRows` do
    /// Android: 7 → 3 + 4, 8 → 4 + 4, 6 → 3 + 3): a doca tem altura fixa.
    private var rows: [[Section]] {
        let n = sections.count
        guard n > 4 else { return [sections] }
        return [Array(sections.prefix(n / 2)), Array(sections.suffix(n - n / 2))]
    }

    var body: some View {
        if let layer = model.selectedLayer {
            // Doca compacta: fileira rápida só de ícones e fichas baixas
            // (EditorLayout.dock). O que sobra fica para a timeline.
            VStack(spacing: 8) {
                HStack(spacing: 8) {
                    if layer.kind == 1 || layer.kind == 3 {
                        dockSquare { quickAction(CupertinoGlyph.Speedometer, "editor_velocidade", size: 21) { model.openPanel(.speed) } }
                    }
                    if layer.kind == 12 {
                        dockSquare { quickAction(CupertinoGlyph.ArrowDownRightSquare, "editor_entrar_grupo", size: 20) { model.openGroup(layer.id) } }
                        dockSquare { quickAction(ShellGlyph.SquareSplit2x2, "editor_desagrupar", size: 20) { model.ungroup(layer.id) } }
                    }
                    // O tempo num bloco só: aparar início | dividir | aparar fim | puxar.
                    HStack(spacing: 0) {
                        quickAction(CupertinoGlyph.ArrowRightToLine, "editor_aparar_inicio_cabecote") { timeEdit(layer) { model.trimStart(layer.id, at: model.status.playhead) } }
                        dockDivider
                        quickAction(CupertinoGlyph.Scissors, "editor_dividir_cabecote") { timeEdit(layer) { model.splitAtPlayhead([layer.id]) } }
                        dockDivider
                        quickAction(CupertinoGlyph.ArrowLeftToLine, "editor_aparar_fim_cabecote") { timeEdit(layer) { model.trimEnd(layer.id, at: model.status.playhead) } }
                        dockDivider
                        // Puxar para o cabeçote: o clipe inteiro anda até o
                        // cabeçote, a duração não muda. Sem o `timeEdit` (que
                        // exige o cabeçote DENTRO da camada) — é para quem está
                        // fora dele.
                        quickAction(CupertinoGlyph.ArrowDownToLine, "editor_puxar_cabecote") {
                            guard !layer.locked else { model.toast = AureaText.t("editor_camada_bloqueada_desbloqueie_editar"); return }
                            // `pause()` solto aqui era o pause(3) da libc (a DockView
                            // não tem um): suspendia o processo inteiro para sempre.
                            if model.status.playing != 0 { model.playPause() }
                            model.moveToPlayhead(layer.id)
                        }
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .background(StageInk.dockRow, in: RoundedRectangle(cornerRadius: 10))
                    if hasAudio {
                        dockSquare {
                            quickAction(muted ? CupertinoGlyph.SpeakerSlash : CupertinoGlyph.Speaker2,
                                        muted ? "editor_som_desligado_toque_ligar_segure_volume" : "editor_desligar_som_segure_volume",
                                        size: 20, tint: muted ? AureaColors.accent : AureaColors.text,
                                        hold: { model.openPanel(.audio) }) {
                                guard !layer.locked else { model.toast = AureaText.t("editor_camada_bloqueada_desbloqueie_editar"); return }
                                model.mutate { $0.setLayer(layer.id, audioMuted: !muted) }; model.refreshModel(force: true)
                            }
                        }
                    }
                }
                .frame(height: EditorLayout.dockQuick)
                ForEach(Array(rows.enumerated()), id: \.offset) { _, row in
                    HStack(spacing: 8) {
                        ForEach(row, id: \.rawValue) { section in
                            Button {
                                if section == .editText { model.openTextContentEditor() }
                                else { model.openPanel(section.panel) }
                            } label: {
                                VStack(spacing: 4) {
                                    if section == .move {
                                        MaterialGlyph("rounded.OpenWith", size: 22, color: StageInk.dockTileContent)
                                    } else {
                                        CupertinoGlyph.text(section.glyph, size: 22, color: StageInk.dockTileContent)
                                    }
                                    Text(AureaText.t(section.label))
                                        .font(.aurea(size: 10, weight: .medium))
                                        .foregroundStyle(StageInk.dockTileContent).lineLimit(2).multilineTextAlignment(.center)
                                }
                                .padding(4).frame(maxWidth: .infinity).frame(height: EditorLayout.dockTile)
                                .background(StageInk.dockTile, in: RoundedRectangle(cornerRadius: 10))
                            }.buttonStyle(.plain)
                        }
                    }
                }
                Spacer(minLength: 0)
            }
            .padding(.horizontal, StageDim.dockRowPad).padding(.top, 8)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(AureaColors.editorPanel)
        } else {
            Text(AureaText.t("editor_toque_num_objeto_tela_editar"))
                .font(.aurea(size: 12.5)).foregroundStyle(AureaColors.muted).lineLimit(1)
                .frame(maxWidth: .infinity, maxHeight: .infinity).padding(.horizontal, 16)
                .background(AureaColors.editorPanel)
        }
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

    private func timeEdit(_ layer: LayerItem, action: () -> Void) {
        guard !layer.locked else { model.toast = AureaText.t("editor_camada_bloqueada_desbloqueie_editar"); return }
        guard model.status.playhead > Int64(layer.startFrame), model.status.playhead < Int64(layer.endFrame) else { model.toast = AureaText.t("sh_playhead_into_layer"); return }
        if model.status.playing != 0 { model.playPause() }
        action()
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
    @State private var importingFont = false
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
                            NativeTextInput(text: $content, selection: $selection, editing: $editing, editable: false) { _ in }
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
            .fileImporter(isPresented: $importingFont, allowedContentTypes: [UTType(filenameExtension: "ttf") ?? .data, UTType(filenameExtension: "otf") ?? .data]) { result in
                guard case .success(let url) = result else { return }
                let scoped = url.startAccessingSecurityScopedResource(); defer { if scoped { url.stopAccessingSecurityScopedResource() } }
                let target = AureaPaths.mediaDestination(for: url.lastPathComponent)
                do {
                    try FileManager.default.copyItem(at: url, to: target)
                    guard let font = model.engine.importFont(atPath: target.path) else { model.toast = AureaText.t("msg_use_uma_fonte_ttf_ou_otf"); return }
                    fonts = model.engine.availableFonts(); chooseFont(font)
                } catch { model.toast = AureaText.t("msg_nao_deu_para_ler_essa_fonte") }
            }
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
                Button { importingFont = true } label: {
                    Text(AureaText.t("panel_importar")).font(.aurea(size: 13)).foregroundStyle(AureaColors.accent).padding(.horizontal, 12).padding(.vertical, 10).background(AureaColors.accentDim, in: RoundedRectangle(cornerRadius: 10))
                }.buttonStyle(AureaPressStyle(shrink: 1))
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
                    Text("importada").font(.aurea(size: 11)).foregroundStyle(AureaColors.muted)
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
                            autoFocus: true, selectAllOnFocus: request.selectAll, scrollable: true) { _ in failed = false }
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
    let onChange: (String) -> Void
    func makeCoordinator() -> Coordinator { Coordinator(self) }
    func makeUIView(context: Context) -> UITextView {
        let view = FocusedContentTextView(); view.delegate = context.coordinator; view.isScrollEnabled = scrollable
        view.isEditable = editable; view.wantsInitialFocus = autoFocus; view.selectAllInitially = selectAllOnFocus
        view.text = text; view.selectedRange = selection
        view.backgroundColor = .clear; view.font = .systemFont(ofSize: 15); view.textColor = UIColor(AureaColors.text); view.tintColor = UIColor(AureaColors.accent)
        view.textContainerInset = UIEdgeInsets(top: 10, left: 12, bottom: 10, right: 12); view.textContainer.lineFragmentPadding = 0
        view.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        return view
    }
    func updateUIView(_ view: UITextView, context: Context) {
        context.coordinator.parent = self
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
private struct AddLayerSheet: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    var tab = 0
    /// Aberto como `.sheet` (cena 3D): o seletor espera a folha fechar antes de subir.
    var inSheet = false
    private let categories: [(String, Character)] = [
        ("sh_add_tab_shape", ShellGlyph.SquareOnCircle), ("sh_add_tab_media", CupertinoGlyph.PhotoOnRectangle),
        ("sh_add_tab_audio", CupertinoGlyph.MusicNote2), ("sh_add_tab_text", CupertinoGlyph.Textformat),
        ("sh_add_tab_element", ShellGlyph.CircleGridHex), ("sh_add_tab_3d", CupertinoGlyph.Cube),
        ("sh_add_tab_draw", ShellGlyph.Scribble), ("sh_add_tab_vector", CupertinoGlyph.PencilOutline)
    ]
    private let shapes: [(Int, String)] = [
        (0, "sh_shape_circle"), (10, "sh_shape_square"), (1, "sh_shape_rounded"), (12, "sh_shape_capsule"),
        (4, "sh_shape_triangle"), (14, "sh_shape_right_triangle"), (6, "editor_poligono"), (11, "editor_estrela"),
        (2, "sh_shape_cross"), (3, "sh_shape_ring"), (5, "sh_shape_slice"), (7, "sh_shape_flower"), (8, "sh_shape_arrow"),
        (16, "sh_shape_pentagon"), (15, "sh_shape_octagon"), (17, "sh_shape_trapezoid"), (18, "sh_shape_parallelogram"),
        (19, "sh_shape_star4"), (20, "sh_shape_star6"), (21, "sh_shape_gear"), (22, "sh_shape_double_arrow")
    ]

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Text(AureaText.t(categories[tab].0)).font(.aurea(size: 16, weight: .semibold))
                Spacer()
                ShellBarButton(glyph: CupertinoGlyph.Xmark, description: AureaText.t("editor_fechar_adicionar"), action: close)
            }.padding(.leading, 16).frame(height: 48)
            AureaColors.hairline.frame(height: 1)
            if tab == 0 { shapeGrid } else { cards }
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
                        card("sh_add_tab_text", glyph: CupertinoGlyph.Textformat, accent: true) { model.addText(); close() }
                        card("sh_add_speech_captions", glyph: CupertinoGlyph.CaptionsBubble) {
                            model.openPanel(.captions)
                        }
                    case 4:
                        drawnCard("sh_add_null", kind: -1) { model.addNull(threeD: false); close() }
                        card("particular_title", glyph: CupertinoGlyph.Sparkles, color: ShellColors.text3D) { model.addParticles(20); close() }   // 20 = Particular (preset Padrão)
                        card("editor_camada_ajuste", glyph: CupertinoGlyph.WandStars) { close(); model.addAdjustmentLayer() }
                        card("sh_add_group_selection", glyph: CupertinoGlyph.Folder) {
                            if model.selection.isEmpty { model.toast = AureaText.t("sh_add_pick_layers_to_group") }
                            else { close(); model.groupSelection() }
                        }
                    case 5:
                        card("scene_workspace", glyph: CupertinoGlyph.Cube, accent: true) { close(); model.enterSceneEditor() }
                        card("sh_add_model_3d", glyph: CupertinoGlyph.Cube, accent: true) { files(.model) }
                        card("sh_add_text_3d", glyph: ShellGlyph.TextformatAlt, color: ShellColors.text3D) { model.addText3D(content: AureaText.t("panel_texto"), depth: 0.25); close() }
                        card("panel_camera_3d", glyph: CupertinoGlyph.CameraFill, accent: true) { model.addCamera(); close() }
                        drawnCard("sh_add_null_3d", kind: -1) { model.addNull(threeD: true); close() }
                    case 6:
                        card("editor_mao_livre", glyph: ShellGlyph.Scribble, accent: true) { close(); model.addVector(0, freehand: true) }
                    default:
                        drawnCard("editor_desenhar_pontos", kind: 0) { model.addVector(0) }
                        drawnCard("editor_retangulo", kind: 1) { model.addVector(1) }
                        drawnCard("editor_elipse", kind: 2) { model.addVector(2) }
                        drawnCard("editor_poligono", kind: 3) { model.addVector(3) }
                        drawnCard("editor_estrela", kind: 4) { model.addVector(4) }
                        card("editor_importar_svg", glyph: CupertinoGlyph.DocText) { files(.svg) }
                    }
                }
                if let hint {
                    Text(AureaText.t(hint)).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                        .padding(.horizontal, 4).padding(.vertical, 2)
                }
            }.padding(.horizontal, 12).padding(.vertical, 10)
        }
    }
    private var hint: String? {
        switch tab {
        case 5: return "sh_add_model_3d_hint"
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
        cardBody(label, action: action) { CupertinoGlyph.text(glyph, size: 28, color: color ?? (accent ? AureaColors.accent : StageInk.dockTileContent)).frame(width: 30, height: 30) }
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
struct ShellMediaPicker: UIViewControllerRepresentable {
    let filter: PHPickerFilter
    let picked: (URL?, Bool) -> Void
    func makeCoordinator() -> Coordinator { Coordinator(picked: picked) }
    func makeUIViewController(context: Context) -> PHPickerViewController {
        var configuration = PHPickerConfiguration()
        configuration.selectionLimit = 1; configuration.filter = filter
        configuration.preferredAssetRepresentationMode = .current
        let picker = PHPickerViewController(configuration: configuration)
        picker.delegate = context.coordinator; return picker
    }
    func updateUIViewController(_ controller: PHPickerViewController, context: Context) {}
    final class Coordinator: NSObject, PHPickerViewControllerDelegate {
        let picked: (URL?, Bool) -> Void
        init(picked: @escaping (URL?, Bool) -> Void) { self.picked = picked }
        func picker(_ picker: PHPickerViewController, didFinishPicking results: [PHPickerResult]) {
            guard let provider = results.first?.itemProvider else { picked(nil, false); return }
            let video = provider.hasItemConformingToTypeIdentifier(UTType.movie.identifier)
            let type = video ? UTType.movie.identifier : UTType.image.identifier
            provider.loadFileRepresentation(forTypeIdentifier: type) { [picked] source, _ in
                guard let source else { DispatchQueue.main.async { picked(nil, video) }; return }
                let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
                do {
                    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
                    let target = directory.appendingPathComponent(source.lastPathComponent)
                    try FileManager.default.copyItem(at: source, to: target)
                    DispatchQueue.main.async { picked(target, video) }
                } catch { DispatchQueue.main.async { picked(nil, video) } }
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
        ScrollView { VStack {
            HStack { Button(AureaText.t("scene_light_directional")) { model.addLight(0) }; Button(AureaText.t("scene_light_point")) { model.addLight(1) } }
            if let id = model.primarySelection {
                let values = model.engine.lightInfo(id).map(\.floatValue)
                if values.count == 10 {
                    ForEach(Array(1..<(values[0] == 0 ? 5 : 6)), id: \.self) { param in
                        HStack {
                            Text(["", AureaText.t("panel_intensidade"), "R", "G", "B", AureaText.t("scene_light_range")][param])
                            SceneNumberField(value: values[param]) { model.setLightParam(UInt32(param), value: $0) }
                                .id("light:\(id):\(param)")
                            if param < 5 {
                                let here = ((model.detail["keyAtPlayhead"] as? NSNumber)?.uint32Value ?? 0) & (1 << (20 + param)) != 0
                                Button(here ? "◆" : "◇") { model.toggleLightKey(UInt32(param), value: values[param]) }
                            }
                        }
                    }
                    if values[0] == 0 { Toggle(AureaText.t("scene_light_shadows"), isOn: Binding(get: { values[8] >= 0.5 }, set: { model.setLightParam(8, value: $0 ? 1 : 0) })) }
                }
            }
        }.padding() }
    }
}

/// Preserve intermediate numeric input and the insertion point across core refreshes.
@MainActor private struct SceneNumberField: View {
    let value: Float
    let onEdit: (Float) -> Void
    @State private var draft = ""
    @FocusState private var focused: Bool
    var body: some View {
        TextField("", text: $draft)
            .keyboardType(.numbersAndPunctuation).textFieldStyle(.roundedBorder)
            .focused($focused)
            .onAppear { draft = String(value) }
            .onChange(of: value) { next in if !focused { draft = String(next) } }
            .onChange(of: focused) { active in if !active { draft = String(value) } }
            .onChange(of: draft) { text in
                if focused, let next = Float(text), next.isFinite { onEdit(next) }
            }
    }
}

/// As categorias de adicionar (as mesmas `AddTab` do Android, mesma ordem,
/// glifos e nomes). O índice é o `tab` do `AddLayerSheet`.
enum ShellAddCategories {
    static let all: [(String, Character)] = [
        ("sh_add_tab_shape", ShellGlyph.SquareOnCircle), ("sh_add_tab_media", CupertinoGlyph.PhotoOnRectangle),
        ("sh_add_tab_audio", CupertinoGlyph.MusicNote2), ("sh_add_tab_text", CupertinoGlyph.Textformat),
        ("sh_add_tab_element", ShellGlyph.CircleGridHex), ("sh_add_tab_3d", CupertinoGlyph.Cube),
        ("sh_add_tab_draw", ShellGlyph.Scribble), ("sh_add_tab_vector", CupertinoGlyph.PencilOutline)
    ]
}

/// A barra fixa de adicionar, na base do editor, no lugar do "+" (par do
/// `AddBar` do Android): sempre à vista sem camada escolhida; tocar abre o
/// painel da categoria. A aberta ganha a pílula de destaque. Sem espaço para
/// todas, a barra rola de lado.
@MainActor private struct ShellAddBar: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    var body: some View {
        GeometryReader { geometry in
            let count = CGFloat(ShellAddCategories.all.count)
            let fits = geometry.size.width / count >= StageDim.addBarItemMin
            let item = fits ? geometry.size.width / count : StageDim.addBarItem
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 0) {
                    ForEach(ShellAddCategories.all.indices, id: \.self) { index in
                        let on = model.showAddLayer && shell.addCategory == index
                        let tint = on ? AureaColors.accent : AureaColors.text
                        Button { open(index) } label: {
                            VStack(spacing: StageDim.addBarItemInset) {
                                CupertinoGlyph.text(ShellAddCategories.all[index].1, size: StageDim.addBarIcon, color: tint)
                                Text(AureaText.t(ShellAddCategories.all[index].0)).font(.aurea(size: 11, weight: .medium))
                                    .foregroundStyle(tint).lineLimit(1)
                            }
                            .frame(width: max(0, item - StageDim.addBarItemInset), height: max(0, geometry.size.height - StageDim.addBarItemInset * 2.5))
                            .background(on ? AureaColors.chip : Color.clear, in: RoundedRectangle(cornerRadius: AureaDims.radiusCard))
                            .frame(width: item, height: geometry.size.height)
                            .contentShape(Rectangle())
                        }.buttonStyle(.plain)
                            .accessibilityLabel(AureaText.t(ShellAddCategories.all[index].0))
                            .accessibilityIdentifier("aurea.add.category.\(index)")
                    }
                }.padding(.horizontal, fits ? 0 : StageDim.addBarItemInset)
            }.scrollDisabled(fits)
        }
        .background(AureaColors.editorPanelHigh)
        .overlay(alignment: .top) { Rectangle().fill(AureaColors.border).frame(height: StageDim.hairline) }
        .accessibilityIdentifier("editor.addBar")
    }
    private func open(_ index: Int) {
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
                AddLayerSheet(tab: shell.addCategory)
                    .frame(width: min(380, max(200, geometry.size.width - 48)), height: min(390, max(180, geometry.size.height * 0.55)))
                    .clipShape(RoundedRectangle(cornerRadius: 20))
                    .overlay(RoundedRectangle(cornerRadius: 20).stroke(AureaColors.action, lineWidth: 1))
            }.frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }
}

// Real solved features in composition coordinates; dragging selects a region.
@MainActor private struct CameraTrackingOverlay: View {
    @EnvironmentObject private var model: AureaModel
    var body: some View {
        GeometryReader { geometry in
            let cw = CGFloat(max(1, model.compositionWidth)), ch = CGFloat(max(1, model.compositionHeight))
            let fit = min(geometry.size.width / cw, geometry.size.height / ch)
            let ox = (geometry.size.width - cw * fit) / 2, oy = (geometry.size.height - ch * fit) / 2
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
                .confirmationDialog("3D Camera Tracker", isPresented: $model.cameraContextMenu) {
                    Button("Create Camera") { model.createTrackedObject(0) }
                    if model.cameraSelectedCount > 0 {
                        Button("Create Null") { model.createTrackedObject(1) }
                        Button("Create Shape") { model.createTrackedObject(2) }
                        Button("Create Text") { model.createTrackedObject(3) }
                        Button("Create Solid") { model.createTrackedObject(4) }
                    }
                    Button("Close", role: .cancel) { }
                }
        }.accessibilityIdentifier("aurea.tracking.points")
    }
}
