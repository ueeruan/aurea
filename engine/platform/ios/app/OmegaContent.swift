// =============================================================================
//  Aurea / platform / ios / app / OmegaContent.swift
//
//  Omega conteúdo (área 06): as peças comuns dos editores de conteúdo — abrir o
//  editor do tipo da camada (toque duplo no palco), as ações da barra da seção
//  (Editar pontos e Editar forma), as preferências do aparelho, os ícones
//  próprios que o CupertinoIcons não tem, o seletor de cor compacto e o palco
//  dos editores (desenho, Editar pontos e a alça de largura da caixa de texto).
//  Par do OmegaContent.kt / ContentColorPicker.kt / ContentStage.kt (mesmos ids).
// =============================================================================
import SwiftUI
import UIKit

// MARK: - Ícones próprios (mesmos traços do Kotlin)

enum ContentIconKind { case alignLeft, alignCenter, alignRight, eraser, pointCorner, pointCurve, angleLock, lengthLock, fromCenter }

struct ContentIcon: View {
    let kind: ContentIconKind
    var size: CGFloat = 22
    var tint: Color = AureaColors.text

    var body: some View {
        Canvas { context, dims in
            let u = min(dims.width, dims.height) / 24
            let style = StrokeStyle(lineWidth: 1.8 * u, lineCap: .round, lineJoin: .round)
            func p(_ x: CGFloat, _ y: CGFloat) -> CGPoint { CGPoint(x: x * u, y: y * u) }
            func line(_ x0: CGFloat, _ y0: CGFloat, _ x1: CGFloat, _ y1: CGFloat) {
                var path = Path()
                path.move(to: p(x0, y0)); path.addLine(to: p(x1, y1))
                context.stroke(path, with: .color(tint), style: style)
            }
            func dot(_ x: CGFloat, _ y: CGFloat, _ r: CGFloat) {
                context.fill(Path(ellipseIn: CGRect(x: (x - r) * u, y: (y - r) * u, width: 2 * r * u, height: 2 * r * u)), with: .color(tint))
            }
            switch kind {
            case .alignLeft, .alignCenter, .alignRight:
                // Quatro linhas: longa, curta, longa, curta — encostadas pelo lado do alinhamento.
                let widths: [CGFloat] = [16, 10, 16, 10]
                for i in 0..<4 {
                    let w = widths[i]
                    let x0: CGFloat = kind == .alignLeft ? 4 : (kind == .alignRight ? 20 - w : 12 - w / 2)
                    let y = 6 + CGFloat(i) * 4
                    line(x0, y, x0 + w, y)
                }
            case .eraser:
                // A borracha inclinada: o corpo e a ponta, e a linha do chão.
                let turn = CGAffineTransform(translationX: 12 * u, y: 12 * u).rotated(by: -40 * .pi / 180).translatedBy(x: -12 * u, y: -12 * u)
                let shape = Path(roundedRect: CGRect(x: 5 * u, y: 8 * u, width: 14 * u, height: 8 * u), cornerRadius: 1.6 * u)
                var tip = Path()
                tip.move(to: p(10, 8)); tip.addLine(to: p(10, 16))
                context.stroke(shape.applying(turn), with: .color(tint), style: style)
                context.stroke(tip.applying(turn), with: .color(tint), style: style)
                line(4, 20.5, 20, 20.5)
            case .pointCorner:
                // Canto: duas retas que se encontram num ponto.
                var path = Path()
                path.move(to: p(4, 19)); path.addLine(to: p(12, 6)); path.addLine(to: p(20, 19))
                context.stroke(path, with: .color(tint), style: style)
                context.fill(Path(CGRect(x: 10 * u, y: 4 * u, width: 4 * u, height: 4 * u)), with: .color(tint))
            case .pointCurve:
                // Curva: o arco com o ponto e as duas alças.
                var path = Path()
                path.move(to: p(4, 19)); path.addCurve(to: p(20, 19), control1: p(6, 6), control2: p(18, 6))
                context.stroke(path, with: .color(tint), style: style)
                line(5, 9.5, 19, 9.5)
                dot(12, 9.5, 2); dot(5, 9.5, 1.4); dot(19, 9.5, 1.4)
            case .angleLock:
                // Alça presa na reta: duas alças opostas e um cadeado pequeno.
                line(3, 15, 21, 15)
                dot(12, 15, 1.8)
                context.fill(Path(roundedRect: CGRect(x: 8.5 * u, y: 4.5 * u, width: 7 * u, height: 5.5 * u), cornerRadius: u), with: .color(tint))
                var arc = Path()
                arc.move(to: p(10, 4.5)); arc.addCurve(to: p(14, 4.5), control1: p(10, 1.5), control2: p(14, 1.5))
                context.stroke(arc, with: .color(tint), lineWidth: 1.4 * u)
            case .lengthLock:
                // Comprimento igual: a régua com as duas pontas.
                line(4, 12, 20, 12); line(4, 8, 4, 16); line(20, 8, 20, 16)
                line(7, 9, 4, 12); line(7, 15, 4, 12)
                line(17, 9, 20, 12); line(17, 15, 20, 12)
            case .fromCenter:
                // Quatro setas saindo do centro.
                dot(12, 12, 1.8)
                line(9, 9, 4, 4); line(4, 4, 8, 4); line(4, 4, 4, 8)
                line(15, 9, 20, 4); line(20, 4, 16, 4); line(20, 4, 20, 8)
                line(9, 15, 4, 20); line(4, 20, 8, 20); line(4, 20, 4, 16)
                line(15, 15, 20, 20); line(20, 20, 16, 20); line(20, 20, 20, 16)
            }
        }
        .frame(width: size, height: size)
        .accessibilityHidden(true)
    }
}

// MARK: - Preferências do aparelho (P0-4, P0-6): ficam entre projetos

@MainActor final class ContentPrefs: ObservableObject {
    static let shared = ContentPrefs()
    private let defaults = UserDefaults.standard

    /// Forma: manter proporção / mudar a partir do centro (padrão = o de sempre: centro).
    @Published private(set) var lockAspect: Bool
    @Published private(set) var fromCenter: Bool
    /// Desenho: ferramenta (0 caneta…3 borracha), espessura (px) e cor (ARGB, como no Android).
    @Published private(set) var drawTool: Int
    @Published private(set) var drawWidth: Int
    @Published private(set) var drawColor: UInt32

    private init() {
        lockAspect = defaults.object(forKey: "aurea.shape.lockAspect") as? Bool ?? false
        fromCenter = defaults.object(forKey: "aurea.shape.fromCenter") as? Bool ?? true
        drawTool = min(3, max(0, defaults.object(forKey: "aurea.draw.tool") as? Int ?? 0))
        drawWidth = min(300, max(1, defaults.object(forKey: "aurea.draw.width") as? Int ?? 12))
        drawColor = UInt32(truncatingIfNeeded: defaults.object(forKey: "aurea.draw.color") as? Int ?? 0xFFFF_FFFF)
    }

    func setLockAspect(_ on: Bool) { lockAspect = on; defaults.set(on, forKey: "aurea.shape.lockAspect") }
    func setFromCenter(_ on: Bool) { fromCenter = on; defaults.set(on, forKey: "aurea.shape.fromCenter") }
    func setDrawTool(_ tool: Int) { drawTool = min(3, max(0, tool)); defaults.set(drawTool, forKey: "aurea.draw.tool") }
    func setDrawWidth(_ width: Int) { drawWidth = min(300, max(1, width)); defaults.set(drawWidth, forKey: "aurea.draw.width") }
    func setDrawColor(_ rgba: [Float]) {
        guard rgba.count >= 3 else { return }
        func byte(_ v: Float) -> UInt32 { UInt32(max(0, min(255, (v * 255).rounded()))) }
        let a = rgba.count >= 4 ? rgba[3] : 1
        drawColor = (byte(a) << 24) | (byte(rgba[0]) << 16) | (byte(rgba[1]) << 8) | byte(rgba[2])
        defaults.set(Int(drawColor), forKey: "aurea.draw.color")
    }
    /// A cor do desenho em RGBA 0…1 (o que o motor recebe).
    var drawRGBA: [Float] {
        [Float((drawColor >> 16) & 0xFF) / 255, Float((drawColor >> 8) & 0xFF) / 255, Float(drawColor & 0xFF) / 255, Float((drawColor >> 24) & 0xFF) / 255]
    }
}

// MARK: - Abrir o editor do tipo da camada (P0-7)

@MainActor enum OmegaContentEditors {
    /// Último toque numa camada do palco (o toque duplo abre o editor do tipo).
    private static var tapAt: CFTimeInterval = 0
    private static var tapLayer: Int64 = 0

    /// O editor de conteúdo da camada: texto (2D/3D) → modo de texto; desenho →
    /// editor de desenho; vetor → Editar pontos; forma → Editar forma; grupo →
    /// entra no grupo. Falso quando o tipo não tem editor.
    @discardableResult
    static func open(_ model: AureaModel, layer: Int64) -> Bool {
        guard model.primarySelection == layer, let row = model.selectedLayer, !row.locked else { return false }
        if row.kind == 4 || (row.kind == 10 && model.engine.text3D(forLayer: layer) != nil) { return model.openTextEdit() }
        if row.kind == 5 {
            if model.isDrawingLayer { model.openPanel(.drawing); return true }
            if model.isVectorLayer { EditPointsState.shared.reset(); model.openPanel(.editPoints); return true }
            model.openPanel(.shapeEdit)
            return true
        }
        if row.kind == 12 { model.openGroup(layer); return true }
        return false
    }

    /// Chamado a cada toque que acerta uma camada no palco. Um segundo toque na
    /// MESMA camada, já escolhida e sozinha, dentro de 0,3 s, abre o editor do tipo
    /// dela. Verdadeiro = o toque foi o duplo (quem chama não escolhe de novo).
    static func doubleTap(_ model: AureaModel, hit: Int64, now: CFTimeInterval = CACurrentMediaTime()) -> Bool {
        let again = tapLayer == hit && now - tapAt >= 0 && now - tapAt <= 0.3
        tapAt = again ? 0 : now
        tapLayer = hit
        guard again, model.primarySelection == hit, model.selection.count == 1, !model.showAddLayer, model.textEditLayer == nil else { return false }
        return open(model, layer: hit)
    }
}

// MARK: - Ações da barra da seção (P0-6)

/// Botão de 44 pt da barra da seção.
struct SectionBarButton<Icon: View>: View {
    let label: String
    let tag: String
    let on: Bool
    var enabled = true
    let action: () -> Void
    @ViewBuilder let icon: (Color) -> Icon

    var body: some View {
        Button(action: action) {
            icon(on ? AureaColors.accent : AureaColors.text)
                .frame(width: 44, height: 44)
                .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .disabled(!enabled)
        .opacity(enabled ? 1 : 0.3)
        .accessibilityLabel(label)
        .accessibilityAddTraits(on ? .isSelected : [])
        .accessibilityIdentifier(tag)
    }
}

/// O que vai à direita da barra da seção: o segmento ◆/• do Editar pontos e as
/// duas chaves do Editar forma (manter proporção, a partir do centro).
struct ContentSectionTrailing: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var prefs = ContentPrefs.shared
    @ObservedObject private var points = EditPointsState.shared
    let panel: AureaModel.PanelKind

    /// A forma aceita as chaves? Só as formas de caixa (a camada vetorial não).
    private var ratioOk: Bool {
        guard let row = model.selectedLayer else { return false }
        return row.kind == 5 && !model.isVectorLayer && !row.locked
    }

    var body: some View {
        if panel == .editPoints {
            HStack(spacing: 0) {
                SectionBarButton(label: AureaText.t("om_pts_mode_keys_cd"), tag: "points.mode.keys", on: points.keysMode,
                                 action: { points.keysMode = true }) { tint in
                    CupertinoGlyph.text(CupertinoGlyph.RhombusFill, size: 18, color: tint)
                }
                SectionBarButton(label: AureaText.t("om_pts_mode_points_cd"), tag: "points.mode.points", on: !points.keysMode,
                                 action: { points.keysMode = false }) { tint in
                    CupertinoGlyph.text(CupertinoGlyph.CircleFill, size: 12, color: tint)
                }
            }
            .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
            .padding(.trailing, 4)
        } else if panel == .shapeEdit {
            HStack(spacing: 0) {
                SectionBarButton(label: AureaText.t("om_shp_lock_ratio_cd"), tag: "shape.lockAspect", on: prefs.lockAspect, enabled: ratioOk,
                                 action: {
                                     let next = !prefs.lockAspect
                                     prefs.setLockAspect(next)
                                     model.toast = AureaText.t(next ? "om_shp_ratio_locked" : "om_shp_ratio_free")
                                 }) { tint in
                    CupertinoGlyph.text(prefs.lockAspect ? CupertinoGlyph.LockFill : CupertinoGlyph.LockOpen, size: 20, color: tint)
                }
                SectionBarButton(label: AureaText.t("om_shp_from_center_cd"), tag: "shape.fromCenter", on: prefs.fromCenter, enabled: ratioOk,
                                 action: {
                                     let next = !prefs.fromCenter
                                     prefs.setFromCenter(next)
                                     model.toast = AureaText.t(next ? "om_shp_from_center" : "om_shp_from_edge")
                                 }) { tint in
                    ContentIcon(kind: .fromCenter, size: 22, tint: tint)
                }
                Color.clear.frame(width: 4, height: 1)
            }
        }
    }
}

// MARK: - Seletor de cor compacto (popup de cor do modo de texto e painel de desenho)

/// 281 × 128 pt no máximo: quadro saturação × brilho, faixa de matiz vertical e
/// cinco amostras rápidas. A cor sai viva em `onChange` (RGBA 0…1);
/// `onStart`/`onEnd` cercam cada arrasto (um passo de desfazer por gesto, quem chama decide).
struct ContentColorPicker: View {
    let color: [Float]
    var tag: String = "content.color"
    var label: String = ""
    var onStart: () -> Void = {}
    let onChange: ([Float]) -> Void
    var onEnd: () -> Void = {}

    @State private var h: CGFloat = 0
    @State private var s: CGFloat = 0
    @State private var v: CGFloat = 1
    @State private var dragging = false

    /// As amostras do seletor (dados de cor do usuário, não papéis do tema).
    private static let swatches: [[Float]] = [[1, 1, 1, 1], [0, 0, 0, 1], [1, 0.302, 0.302, 1], [1, 0.761, 0.2, 1], [0.239, 0.647, 1, 1]]

    private var alpha: Float { color.count >= 4 ? color[3] : 1 }

    var body: some View {
        HStack(spacing: 8) {
            // Quadro S × V.
            GeometryReader { geo in
                let size = geo.size
                ZStack(alignment: .topLeading) {
                    LinearGradient(colors: [.white, Color(hue: Double(h / 360), saturation: 1, brightness: 1)], startPoint: .leading, endPoint: .trailing)
                    LinearGradient(colors: [.clear, .black], startPoint: .top, endPoint: .bottom)
                    Circle().stroke(Color.black.opacity(0.5), lineWidth: 3).frame(width: 14, height: 14)
                        .position(x: s * size.width, y: (1 - v) * size.height)
                    Circle().stroke(Color.white, lineWidth: 1.5).frame(width: 14, height: 14)
                        .position(x: s * size.width, y: (1 - v) * size.height)
                }
                .clipShape(RoundedRectangle(cornerRadius: 6))
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { g in
                        begin()
                        s = min(1, max(0, g.location.x / max(1, size.width)))
                        v = 1 - min(1, max(0, g.location.y / max(1, size.height)))
                        emit()
                    }
                    .onEnded { _ in end() })
            }
            .frame(maxWidth: .infinity)
            // Matiz.
            GeometryReader { geo in
                let height = geo.size.height
                ZStack(alignment: .topLeading) {
                    LinearGradient(colors: (0...6).map { Color(hue: Double($0 % 6) / 6, saturation: 1, brightness: 1) }, startPoint: .top, endPoint: .bottom)
                    Rectangle().fill(Color.white).frame(height: 3).offset(y: h / 360 * height - 1.5)
                }
                .clipShape(RoundedRectangle(cornerRadius: 6))
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { g in
                        begin()
                        h = min(359.9, max(0, g.location.y / max(1, height) * 360))
                        emit()
                    }
                    .onEnded { _ in end() })
            }
            .frame(width: 22)
            // Amostras rápidas.
            VStack(spacing: 0) {
                ForEach(0..<Self.swatches.count, id: \.self) { i in
                    let q = Self.swatches[i]
                    Button {
                        onStart()
                        sync(q)
                        emit()
                        onEnd()
                    } label: {
                        Circle().fill(Color(.sRGB, red: Double(q[0]), green: Double(q[1]), blue: Double(q[2]), opacity: 1))
                            .overlay(Circle().stroke(AureaColors.border, lineWidth: 1))
                            .frame(width: 22, height: 22)
                            .frame(width: 30, height: 25.6)
                            .contentShape(Rectangle())
                    }
                    .buttonStyle(AureaPressStyle(shrink: 1))
                    .accessibilityHidden(true)
                }
            }
        }
        .frame(height: 128)
        .accessibilityElement(children: .contain)
        .accessibilityLabel(label)
        .accessibilityIdentifier(tag)
        .onAppear { sync(color) }
        .onChange(of: color) { next in if !dragging { sync(next) } }
    }

    private func begin() { if !dragging { dragging = true; onStart() } }
    private func end() { if dragging { dragging = false; onEnd() } }

    private func sync(_ rgba: [Float]) {
        guard rgba.count >= 3 else { return }
        var hue: CGFloat = 0, sat: CGFloat = 0, bri: CGFloat = 0, a: CGFloat = 0
        UIColor(red: CGFloat(rgba[0]), green: CGFloat(rgba[1]), blue: CGFloat(rgba[2]), alpha: 1).getHue(&hue, saturation: &sat, brightness: &bri, alpha: &a)
        // Cinza não tem matiz: o quadro mantém a que estava.
        if sat > 0.001 { h = hue * 360 }
        s = sat; v = bri
    }

    private func emit() {
        var r: CGFloat = 0, g: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 0
        UIColor(hue: h / 360, saturation: s, brightness: v, alpha: 1).getRed(&r, green: &g, blue: &b, alpha: &a)
        onChange([Float(r), Float(g), Float(b), alpha])
    }
}

/// A amostra redonda de 24,5 pt (botão de cor da barra de estilo e do desenho).
struct ContentColorDot: View {
    let rgba: [Float]
    var size: CGFloat = 24.5
    var body: some View {
        Circle()
            .fill(Color(.sRGB, red: Double(rgba.count > 0 ? rgba[0] : 1), green: Double(rgba.count > 1 ? rgba[1] : 1),
                        blue: Double(rgba.count > 2 ? rgba[2] : 1), opacity: Double(rgba.count > 3 ? rgba[3] : 1)))
            .overlay(Circle().stroke(AureaColors.text.opacity(0.6), lineWidth: 1))
            .frame(width: size, height: size)
    }
}

// MARK: - O palco dos editores de conteúdo

/// Por cima do palco: o desenho e o Editar pontos tomam o palco inteiro; no modo
/// de texto, só a alça de LARGURA da caixa (P1-10) pega o toque.
@MainActor struct OmegaContentStageOverlay: View {
    @EnvironmentObject private var model: AureaModel

    var body: some View {
        if DrawingStage.active(model) {
            DrawingStageOverlay()
        } else if EditPointsStage.active(model) {
            EditPointsStageOverlay()
        } else if model.textModeActive {
            TextBoxHandleOverlay()
        }
    }
}

/// A alça de largura da caixa de texto: meio da borda lateral da caixa (direita;
/// esquerda no texto alinhado à direita). Arrastar muda a LARGURA (motor).
@MainActor private struct TextBoxHandleOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var viewZoom = StageViewZoom.shared
    @State private var last: CGPoint?
    @State private var sent = false

    var body: some View {
        GeometryReader { geo in
            let placed = StageZoomMath.fit(size: geo.size, composition: CGSize(width: CGFloat(model.compositionWidth), height: CGFloat(model.compositionHeight)),
                                           zoom: viewZoom.zoom, pan: viewZoom.pan)
            if let point = handle(placed) {
                let grabbed = last != nil
                ZStack {
                    Circle().fill(StageInk.outlineUnder).frame(width: (grabbed ? 7 : 6) * 2 + 3, height: (grabbed ? 7 : 6) * 2 + 3)
                    Circle().fill(Color.white).frame(width: (grabbed ? 7 : 6) * 2, height: (grabbed ? 7 : 6) * 2)
                    Circle().stroke(AureaColors.accent, lineWidth: 1.5).frame(width: (grabbed ? 7 : 6) * 2, height: (grabbed ? 7 : 6) * 2)
                }
                .frame(width: 44, height: 44)
                .contentShape(Circle())
                .position(point)
                .gesture(DragGesture(minimumDistance: 0, coordinateSpace: .global)
                    .onChanged { g in
                        let previous = last ?? g.startLocation
                        last = g.location
                        let k = max(placed.scale, 0.0001)
                        let dx = Float((g.location.x - previous.x) / k), dy = Float((g.location.y - previous.y) / k)
                        guard dx != 0 || dy != 0 else { return }
                        model.dragTextBoxWidth(dx: dx, dy: dy, continuing: sent)
                        sent = true
                    }
                    .onEnded { _ in
                        if sent { model.refreshModel(force: true) }
                        last = nil; sent = false
                    })
                .accessibilityElement()
                .accessibilityLabel(AureaText.t("panel_largura_caixa"))
                .accessibilityIdentifier("text.stage.boxHandle")
            }
        }
    }

    private func handle(_ placed: (scale: CGFloat, origin: CGPoint)) -> CGPoint? {
        guard let id = model.primarySelection, let row = model.selectedLayer, !row.locked,
              model.engine.text3D(forLayer: id) == nil,
              Int64(row.startFrame) <= model.status.playhead, model.status.playhead < Int64(row.endFrame) else { return nil }
        var c: [Float] = []
        guard StageGeom.corners(model.detail, &c), c.count >= 8 else { return nil }
        let alignment = (model.engine.text(forLayer: id)?["alignment"] as? NSNumber)?.intValue ?? 0
        // Cantos: TL, TR, BR, BL. Esquerda/centro: a borda direita; direita: a esquerda.
        let (a, b) = alignment == 2 ? (0, 3) : (1, 2)
        let x = (c[a * 2] + c[b * 2]) / 2, y = (c[a * 2 + 1] + c[b * 2 + 1]) / 2
        return CGPoint(x: placed.origin.x + CGFloat(x) * placed.scale, y: placed.origin.y + CGFloat(y) * placed.scale)
    }
}
