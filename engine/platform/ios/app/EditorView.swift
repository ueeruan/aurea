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
    var body: some View {
        GeometryReader { geometry in
            let wide = !model.fullscreen && EditorLayout.isWide(geometry.size.width, geometry.size.height)
            let sideWidth = (geometry.size.width * 0.4).clamped(to: 280...380)
            let metrics = EditorLayout.solve(total: geometry.size.height, content: model.sheetContent, fullscreen: model.fullscreen)
            Group {
                if wide {
                    VStack(spacing: 0) {
                        TopBarView().frame(height: EditorLayout.topBar)
                        HStack(spacing: 0) {
                            VStack(spacing: 0) {
                                PreviewStage(height: max(96, geometry.size.height - EditorLayout.topBar - EditorLayout.transport - EditorLayout.strip - EditorLayout.wideTimeline(geometry.size.height)))
                                Rectangle().fill(AureaColors.editorPanelHigh).frame(height: EditorLayout.strip)
                                TransportView().frame(height: EditorLayout.transport)
                                TimelineView().frame(height: EditorLayout.wideTimeline(geometry.size.height))
                            }.frame(maxWidth: .infinity)
                            ContextSheet(metrics: metrics).frame(width: sideWidth, height: max(1, geometry.size.height - EditorLayout.topBar))
                        }
                    }
                } else {
                    VStack(spacing: 0) {
                        if !model.fullscreen { TopBarView().frame(height: metrics.topBar) }
                        PreviewStage(height: max(0, metrics.preview - (model.fullscreen ? StageDim.fullscreenTimeBar : 0)))
                        if model.fullscreen { FullscreenTimeBar() }
                        else { Rectangle().fill(AureaColors.editorPanelHigh).frame(height: metrics.strip) }
                        TransportView().frame(height: metrics.transport)
                        if metrics.timeline > 0 { TimelineView().frame(height: metrics.timeline) }
                        if metrics.sheet > 0 { ContextSheet(metrics: metrics).frame(height: metrics.sheet) }
                    }
                }
            }
            .background(AureaColors.background.ignoresSafeArea())
            .overlay(alignment: .bottomTrailing) {
                if !model.fullscreen && !model.showAddLayer && model.sheetContent != .panel {
                    Button { model.openAddLayer() } label: {
                        MaterialGlyph("filled.Add", size: 32, color: AureaColors.accent)
                            .frame(width: StageDim.fab, height: StageDim.fab)
                            .background(StageInk.fab, in: Circle())
                            .overlay(Circle().stroke(AureaColors.action, lineWidth: 2.2))
                            .shadow(color: StageInk.fabShadow, radius: 6, y: 3)
                    }.buttonStyle(.plain).accessibilityLabel(AureaText.t("editor_adicionar_camada"))
                        .padding(.trailing, 18 + (wide ? sideWidth : 0)).padding(.bottom, 18 + (wide ? 0 : metrics.sheet))
                }
            }
        }
        .fullScreenCover(isPresented: $model.showExport) { ExportView() }
        .overlay(alignment: .topTrailing) {
            if model.fullscreen {
                Button { model.fullscreen = false } label: {
                    CupertinoGlyph.text(CupertinoGlyph.FullscreenExit, size: 24)
                        .frame(width: 40, height: 40).background(StageInk.floatingDark, in: Circle())
                }.buttonStyle(.plain).accessibilityLabel(AureaText.t("editor_voltar_editor")).padding(10)
            }
        }
        .overlay { ShellOverlayHost() }
        .environmentObject(shell)
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
            PreviewMetalView(compositionSize: compositionSize, interactive: !model.fullscreen)
                .overlay { if !model.fullscreen { StageOverlay().allowsHitTesting(false) } }
                .overlay { if !model.fullscreen { StageInteractionOverlay().allowsHitTesting(false) } }.padding(StageDim.stageInset)
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
            if !model.fullscreen {
                Text(previewLabel).font(.aurea(size: 12)).foregroundStyle(AureaColors.text)
                    .padding(.horizontal, 10).padding(.vertical, 8).background(StageInk.resolutionChip, in: RoundedRectangle(cornerRadius: 6))
                    .overlay { GeometryReader { bounds in
                        Color.clear.contentShape(Rectangle()).onTapGesture { shell.resolutionAnchor = bounds.frame(in: .global) }.onLongPressGesture(minimumDuration: 0.5) { model.toggleHud() }
                    }}.accessibilityLabel(AureaText.t("editor_resolucao_previa_segure_diagnostico"))
                    .padding(4).frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing)
            }
            if model.hudVisible { ShellPerfHud().padding(.leading, 8).padding(.top, 6).frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading).allowsHitTesting(false) }
        }.frame(height: height).clipped()
    }
    private var previewLabel: String {
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
    private let timer = Timer.publish(every: 0.25, on: .main, in: .common).autoconnect()
    var body: some View {
        Text(text).font(.aurea(size: 10, design: .monospaced)).lineSpacing(2).foregroundStyle(AureaColors.accent).fixedSize(horizontal: false, vertical: true)
            .padding(.horizontal, 10).padding(.vertical, 6).background(StageInk.floatingDark, in: RoundedRectangle(cornerRadius: 8))
            .overlay(RoundedRectangle(cornerRadius: 8).stroke(AureaColors.border, lineWidth: 0.5))
            .onAppear { stats = model.engine.perf(); display.start() }.onDisappear { display.stop() }
            .onReceive(timer) { _ in stats = model.engine.perf() }
    }
    private func number(_ key: String) -> NSNumber? { stats[key] as? NSNumber }
    private func count(_ key: String) -> String { number(key)?.stringValue ?? "—" }
    private func decimal(_ key: String) -> String { number(key).map { String(format: "%.1f", locale: Locale(identifier: "en_US_POSIX"), $0.doubleValue) } ?? "—" }
    private func mb(_ key: String) -> String { number(key).map { String($0.uint64Value / (1024 * 1024)) } ?? "—" }
    private func gpu(_ key: String) -> String { number("gpuTimers")?.boolValue == true ? decimal(key) : "—" }
    private func line(_ key: String, _ args: String...) -> String { String(format: AureaText.t(key), arguments: args.map { $0 as CVarArg }) }
    private var text: String {
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
            let fit = min(size.width / CGFloat(max(1, model.compositionWidth)), size.height / CGFloat(max(1, model.compositionHeight)))
            let origin = CGPoint(x: (size.width - CGFloat(model.compositionWidth) * fit) / 2, y: (size.height - CGFloat(model.compositionHeight) * fit) / 2)
            func screen(_ x: Float, _ y: Float) -> CGPoint { CGPoint(x: origin.x + CGFloat(x) * fit, y: origin.y + CGFloat(y) * fit) }
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
            for row in model.layers where model.selection.count > 1 && model.selection.contains(row.id) && row.id != model.primarySelection && active(row) {
                if let detail = model.engine.layerDetail(row.id) { outline(&context, corners(detail), under: 2.5, over: 1.5, color: AureaColors.accent) }
            }
            guard let selected = model.selectedLayer, active(selected) else { return }
            let points = corners(model.detail)
            outline(&context, points, under: 3.5, over: 2, color: selected.locked ? AureaColors.muted : AureaColors.accent)
            guard model.selection.count == 1, !selected.locked, !(model.panel == .mask && model.editingMask != nil), points.count == 4 else { return }
            if ShapeStageGeometry.enabled(model) { return }
            let handles = ShellStageGeometry.handles(points, size: size)
            for n in 1..<handles.count {
                let radius: CGFloat = shell.grabbedHandle == n ? 6 : 5
                circle(&context, handles[n], radius + 1.5, StageInk.outlineUnder)
                circle(&context, handles[n], radius, .white)
                context.stroke(Path(ellipseIn: CGRect(x: handles[n].x - radius, y: handles[n].y - radius, width: radius * 2, height: radius * 2)), with: .color(AureaColors.accent), lineWidth: 1.5)
            }
            rotationHandle(&context, handles[0], grabbed: shell.grabbedHandle == 0)
            let gizmo = model.engine.gizmo(selected.id, length: ShellStageGeometry.gizmoLength).map(\.floatValue)
            if gizmo.count == 8 {
                let tips = ShellStageGeometry.gizmoTips(stride(from: 0, to: 8, by: 2).map { screen(gizmo[$0], gizmo[$0 + 1]) })
                for i in 1...3 {
                    var line = Path(); line.move(to: tips[0]); line.addLine(to: tips[i])
                    let color = [StageInk.gizmoX, StageInk.gizmoY, StageInk.gizmoZ][i - 1]
                    context.stroke(line, with: .color(StageInk.outlineUnder), lineWidth: 5); context.stroke(line, with: .color(color), lineWidth: 2.5)
                    circle(&context, tips[i], 9, StageInk.outlineUnder); circle(&context, tips[i], 7.5, color)
                }
                circle(&context, tips[0], 4, .white)
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
    private func rotationHandle(_ context: inout GraphicsContext, _ center: CGPoint, grabbed: Bool) {
        let r: CGFloat = 17.5 * (grabbed ? 0.62 : 0.55), ar = r * 0.52
        circle(&context, center, r + 1.5, StageInk.outlineUnder); circle(&context, center, r, grabbed ? AureaColors.accent : AureaColors.editorPanelHigh)
        if !grabbed { context.stroke(Path(ellipseIn: CGRect(x: center.x - r, y: center.y - r, width: r * 2, height: r * 2)), with: .color(AureaColors.accent), lineWidth: 1) }
        var arc = Path(); arc.addArc(center: center, radius: ar, startAngle: .degrees(-162), endAngle: .degrees(108), clockwise: false)
        context.stroke(arc, with: .color(.white), style: StrokeStyle(lineWidth: 1.6, lineCap: .round))
        let end = CGFloat(108) * .pi / 180, ex = center.x + ar * cos(end), ey = center.y + ar * sin(end), tx = -sin(end), ty = cos(end), nx = cos(end), ny = sin(end), s = r * 0.34
        var arrow = Path(); arrow.move(to: CGPoint(x: ex + tx * s, y: ey + ty * s)); arrow.addLine(to: CGPoint(x: ex - tx * s * 0.2 + nx * s * 0.8, y: ey - ty * s * 0.2 + ny * s * 0.8)); arrow.addLine(to: CGPoint(x: ex - tx * s * 0.2 - nx * s * 0.8, y: ey - ty * s * 0.2 - ny * s * 0.8)); arrow.closeSubpath()
        context.fill(arrow, with: .color(.white))
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
            if model.sheetContent != .panel {
                AureaColors.editorPanelHigh.frame(height: StageDim.sheetHandle)
            }

            if model.showAddLayer {
                AddLayerSheet()
            } else if model.selection.count > 1 && model.panel != .aiVideo {
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
            }.frame(height: 48).background(StageInk.dockRow, in: RoundedRectangle(cornerRadius: 10))
            Spacer(minLength: 0)
        }.padding(.horizontal, 10).padding(.top, 4)
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
        case color, shape, vector, text, particles, audio, move, blend, environment, mask, tracking, captions, presets, effects
        var label: String {
            switch self {
            case .color: return "sh_dock_color_fill"
            case .shape: return "sh_dock_edit_shape"
            case .vector: return "sh_dock_edit_vector"
            case .text: return "sh_dock_edit_text"
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
            case .text: return CupertinoGlyph.Textformat
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
            case .text: return .text
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
        case 4: return [.text, .move, .blend, .mask, .presets, .effects]
        case 1:
            return [.move] + (hasAudio ? [.audio] : []) + [.mask, .blend, .tracking] + (hasAudio ? [.captions] : []) + [.presets, .effects]
        case 2, 12: return [.move, .blend, .mask, .presets, .effects]
        case 3: return [.audio, .captions, .presets, .effects]
        case 10: return [.move, .environment, .blend, .presets, .effects]
        case 11: return [.particles, .move, .blend, .mask, .presets, .effects]
        case 6, 8, 9: return [.move, .presets]
        default: return []
        }
    }

    var body: some View {
        GeometryReader { geometry in
            if let layer = model.selectedLayer {
                let columns = sections.count <= 6 ? 3 : 4
                let rows = max(1, (sections.count + columns - 1) / columns)
                let tileHeight = ((geometry.size.height - 60 - 8 * CGFloat(rows)) / CGFloat(rows)).clamped(to: StageDim.dockTileMin...StageDim.dockTileMax)
                VStack(spacing: 0) {
                    HStack(spacing: 0) {
                        if layer.kind == 1 || layer.kind == 3 {
                            quickAction(CupertinoGlyph.Speedometer, "editor_velocidade", size: 21) { model.openPanel(.speed) }
                        }
                        if layer.kind == 12 {
                            quickAction(CupertinoGlyph.ArrowDownRightSquare, "editor_entrar_grupo", size: 20) { model.openGroup(layer.id) }
                            quickAction(ShellGlyph.SquareSplit2x2, "editor_desagrupar", size: 20) { model.ungroup(layer.id) }
                        }
                        quickAction(CupertinoGlyph.ArrowRightToLine, "editor_aparar_inicio_cabecote") { timeEdit(layer) { model.trimStart(layer.id, at: model.status.playhead) } }
                        quickAction(CupertinoGlyph.Scissors, "editor_dividir_cabecote") { timeEdit(layer) { model.splitAtPlayhead([layer.id]) } }
                        quickAction(CupertinoGlyph.ArrowLeftToLine, "editor_aparar_fim_cabecote") { timeEdit(layer) { model.trimEnd(layer.id, at: model.status.playhead) } }
                        if hasAudio {
                            quickAction(muted ? CupertinoGlyph.SpeakerSlash : CupertinoGlyph.Speaker2,
                                        muted ? "editor_som_desligado_toque_ligar_segure_volume" : "editor_desligar_som_segure_volume",
                                        size: 20, tint: muted ? AureaColors.accent : AureaColors.text,
                                        hold: { model.openPanel(.audio) }) {
                                guard !layer.locked else { model.toast = AureaText.t("editor_camada_bloqueada_desbloqueie_editar"); return }
                                model.mutate { $0.setLayer(layer.id, audioMuted: !muted) }; model.refreshModel(force: true)
                            }
                        }
                    }
                    .frame(height: StageDim.dockRowHeight)
                    .background(StageInk.dockRow, in: RoundedRectangle(cornerRadius: 10))
                    .padding(.horizontal, StageDim.dockRowPad).padding(.vertical, 8)
                    ScrollView {
                        LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 8), count: columns), spacing: 8) {
                            ForEach(sections, id: \.rawValue) { section in
                                Button { model.openPanel(section.panel) } label: {
                                    VStack(spacing: tileHeight < 72 ? 4 : 7) {
                                        if section == .move {
                                            MaterialGlyph("rounded.OpenWith", size: tileHeight < 72 ? 22 : 27, color: StageInk.dockTileContent)
                                        } else {
                                            CupertinoGlyph.text(section.glyph, size: tileHeight < 72 ? 22 : 27, color: StageInk.dockTileContent)
                                        }
                                        Text(AureaText.t(section.label))
                                            .font(.aurea(size: tileHeight < 72 ? 10 : 11, weight: .medium))
                                            .foregroundStyle(StageInk.dockTileContent).lineLimit(2).multilineTextAlignment(.center)
                                    }
                                    .padding(4).frame(maxWidth: .infinity).frame(height: tileHeight)
                                    .background(StageInk.dockTile, in: RoundedRectangle(cornerRadius: 10))
                                }.buttonStyle(.plain)
                            }
                        }.padding(.horizontal, 10).padding(.bottom, 8)
                    }
                }
            } else {
                Text(AureaText.t("editor_toque_num_objeto_tela_editar"))
                    .font(.aurea(size: 12.5)).foregroundStyle(AureaColors.muted).lineLimit(1)
                    .frame(maxWidth: .infinity, maxHeight: .infinity).padding(.horizontal, 16)
            }
        }
        .background(AureaColors.editorPanel)
    }

    private func timeEdit(_ layer: LayerItem, action: () -> Void) {
        guard !layer.locked else { model.toast = AureaText.t("editor_camada_bloqueada_desbloqueie_editar"); return }
        guard model.status.playhead > Int64(layer.startFrame), model.status.playhead < Int64(layer.endFrame) else { model.toast = AureaText.t("sh_playhead_into_layer"); return }
        if model.status.playing != 0 { model.playPause() }
        action()
    }
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
            PanelHeader(title: AureaText.t(showingFonts ? "panel_fonte" : "panel_texto")) { dismissEditing(); model.panel = .none }
            if showingFonts { fontPanel }
            else {
                ScrollView {
                    VStack(alignment: .leading, spacing: 0) {
                        ZStack(alignment: .topLeading) {
                            if content.isEmpty { Text(AureaText.t("panel_digite_texto")).font(.aurea(size: 15)).foregroundStyle(AureaColors.muted).padding(.horizontal, 12).padding(.vertical, 10).allowsHitTesting(false) }
                            NativeTextInput(text: $content, selection: $selection, editing: $editing) {
                                model.engine.setText(layerId, content: $0); model.refreshModel(force: true)
                            }
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

private struct NativeTextInput: UIViewRepresentable {
    @Binding var text: String
    @Binding var selection: NSRange
    @Binding var editing: Bool
    let onChange: (String) -> Void
    func makeCoordinator() -> Coordinator { Coordinator(self) }
    func makeUIView(context: Context) -> UITextView {
        let view = UITextView(); view.delegate = context.coordinator; view.isScrollEnabled = false
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
    @State private var tab = 0
    @State private var showingImporter = false
    @State private var fileKind = FileKind.audio
    @State private var showingPhotos = false
    @State private var photoKind = PhotoKind.gallery

    private enum FileKind { case audio, model, svg }
    private enum PhotoKind { case gallery, photo, video, audioFromVideo }
    private let categories: [(String, Character)] = [
        ("sh_add_tab_shape", ShellGlyph.SquareOnCircle), ("sh_add_tab_media", CupertinoGlyph.PhotoOnRectangle),
        ("sh_add_tab_audio", CupertinoGlyph.MusicNote2), ("sh_add_tab_text", CupertinoGlyph.Textformat),
        ("sh_add_tab_element", ShellGlyph.CircleGridHex), ("sh_add_tab_3d", CupertinoGlyph.Cube),
        ("sh_add_tab_draw", ShellGlyph.Scribble), ("sh_add_tab_vector", CupertinoGlyph.PencilOutline)
    ]
    private let shapes: [(Int, String)] = [
        (0, "sh_shape_circle"), (10, "sh_shape_square"), (1, "sh_shape_rounded"), (12, "sh_shape_capsule"),
        (4, "sh_shape_triangle"), (14, "sh_shape_right_triangle"), (6, "editor_poligono"), (11, "editor_estrela"),
        (2, "sh_shape_cross"), (3, "sh_shape_ring"), (5, "sh_shape_slice"), (7, "sh_shape_flower"), (8, "sh_shape_arrow")
    ]

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 0) {
                ShellBarButton(glyph: CupertinoGlyph.Xmark, description: AureaText.t("editor_fechar_adicionar"), size: 20,
                               width: 44, height: StageDim.addCategories, action: close)
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 0) {
                        ForEach(Array(categories.enumerated()), id: \.offset) { entry in
                            let selected = tab == entry.offset
                            let tint = selected ? AureaColors.accent : AureaColors.text
                            Button { tab = entry.offset } label: {
                                VStack(spacing: 4) {
                                    CupertinoGlyph.text(entry.element.1, size: 24, color: tint)
                                    Text(AureaText.t(entry.element.0)).font(.aurea(size: 11, weight: .semibold)).lineLimit(1)
                                }
                                .foregroundStyle(tint).frame(width: 60, height: 58)
                                .background(selected ? AureaColors.chip : .clear, in: RoundedRectangle(cornerRadius: 12))
                                .padding(.horizontal, 2).padding(.vertical, 5)
                            }.buttonStyle(.plain)
                        }
                    }.padding(.trailing, 6)
                }
            }.frame(height: StageDim.addCategories)
            AureaColors.hairline.frame(height: 1)
            if tab == 0 { shapeGrid } else { cards }
        }
        .background(AureaColors.editorPanel)
        .fileImporter(isPresented: $showingImporter, allowedContentTypes: fileTypes) { result in
            guard case .success(let url) = result else { return }
            switch fileKind {
            case .audio: model.importMedia(url: url, kind: .audio)
            case .model: model.importMedia(url: url, kind: .model)
            case .svg: model.importSvg(url: url)
            }
            close()
        }
        .sheet(isPresented: $showingPhotos) {
            ShellMediaPicker(filter: photoKind == .photo ? .images : ((photoKind == .video || photoKind == .audioFromVideo) ? .videos : .any(of: [.images, .videos]))) { url, video in
                showingPhotos = false
                guard let url else { return }
                let kind: AureaModel.ImportKind = photoKind == .audioFromVideo ? .audio : (video ? .video : .image)
                model.importMedia(url: url, kind: kind)
                close()
            }
        }
    }

    private var shapeGrid: some View {
        GeometryReader { geometry in
            let columns = min(9, max(5, Int((geometry.size.width - 24) / 68)))
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
                        card("editor_musica_ou_som", glyph: CupertinoGlyph.MusicNote, accent: true) { files(.audio) }
                        card("sh_add_video_sound", glyph: CupertinoGlyph.Film) { photos(.audioFromVideo) }
                        card("sh_add_detect_beats", glyph: ShellGlyph.Metronome) { close(); model.detectBeats() }
                        card("sh_add_marker_at_playhead", glyph: CupertinoGlyph.Bookmark) { close(); model.toggleMarkerAt(model.status.playhead) }
                    case 3:
                        card("sh_add_tab_text", glyph: CupertinoGlyph.Textformat, accent: true) { model.addText(); model.openPanel(.text) }
                        card("sh_add_speech_captions", glyph: CupertinoGlyph.CaptionsBubble) {
                            if model.selectedLayer?.kind == 1 || model.selectedLayer?.kind == 3 { model.openPanel(.captions) }
                            else { model.toast = AureaText.t("sh_add_captions_need_speech") }
                        }
                    case 4:
                        drawnCard("sh_add_null", kind: -1) { model.addNull(threeD: false); close() }
                        card("particular_title", glyph: CupertinoGlyph.Sparkles, color: ShellColors.text3D) { model.addParticles(0); close() }
                        card("editor_camada_ajuste", glyph: CupertinoGlyph.WandStars) { close(); model.addAdjustmentLayer() }
                        card("sh_add_group_selection", glyph: CupertinoGlyph.Folder) {
                            if model.selection.isEmpty { model.toast = AureaText.t("sh_add_pick_layers_to_group") }
                            else { close(); model.groupSelection() }
                        }
                    case 5:
                        card("sh_add_model_3d", glyph: CupertinoGlyph.Cube, accent: true) { files(.model) }
                        card("sh_add_text_3d", glyph: ShellGlyph.TextformatAlt, color: ShellColors.text3D) { model.addText3D(content: "Texto", depth: 0.25); close() }
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
    private func photos(_ kind: PhotoKind) { photoKind = kind; showingPhotos = true }
    private func files(_ kind: FileKind) { fileKind = kind; showingImporter = true }
    private var fileTypes: [UTType] {
        switch fileKind {
        case .audio: return [.audio]
        case .svg: return [UTType(filenameExtension: "svg") ?? .data]
        case .model: return [.data] // glTF/GLB/OBJ/FBX: o importador do motor valida a extensão.
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
        }.buttonStyle(.plain)
    }
}

/// Seletor de fotos do sistema, equivalente ao Photo Picker do Android.
/// `loadFileRepresentation` copia o arquivo temporário sem carregar um vídeo
/// inteiro em Data no processo da interface.
private struct ShellMediaPicker: UIViewControllerRepresentable {
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
