// =============================================================================
//  Aurea / platform / ios / app / StageZoom.swift
//
//  Zoom da VISTA do palco, par do StageZoom.kt do Android: só muda como a
//  composição aparece na tela, nunca o projeto, o render nem o export. O motor
//  aplica o mesmo zoom/pan no passe de saída da prévia (`viewportZoom`/
//  `viewportPan`, em px do drawable); o overlay e os gestos usam os mesmos
//  números para desenhar as alças e converter o toque.
//
//  Omega chrome (área 02): os degraus da referência (1 % a 6400 %), o botão
//  de modo zoom com o rótulo "NN%" e a COLUNA DE MODOS à direita da prévia
//  (pixels reais · grade com espaçamento · destaque da camada escolhida ·
//  visão pela câmera · zoom em degraus).
// =============================================================================
import SwiftUI
import UIKit

/// Contas puras (as mesmas do StageZoomMath.kt, testadas na JVM do Android).
enum StageZoomMath {
    static let minZoom: CGFloat = 0.01
    static let maxZoom: CGFloat = 64

    /// Degraus do + e do − da coluna (1 · 5 · 10 · 25 · 50 · 100 · 150 · 200 · 300 · 400 · 800 · 1600 · 3200 · 6400 %).
    static let steps: [CGFloat] = [0.01, 0.05, 0.1, 0.25, 0.5, 1, 1.5, 2, 3, 4, 8, 16, 32, 64]

    /// O primeiro degrau ACIMA do zoom atual (no teto, fica).
    static func stepUp(_ z: CGFloat) -> CGFloat { steps.first { $0 > z * 1.0001 } ?? maxZoom }
    /// O último degrau ABAIXO do zoom atual (no piso, fica).
    static func stepDown(_ z: CGFloat) -> CGFloat { steps.last { $0 < z * 0.9999 } ?? minZoom }

    /// Zoom preso em [1 %, 6400 %]; lixo (NaN/∞) volta ao encaixe (100 %).
    static func clampZoom(_ z: CGFloat) -> CGFloat { z.isFinite ? min(maxZoom, max(minZoom, z)) : 1 }

    /// Pan máximo num eixo: a borda da composição vai até onde fica em 100 %.
    static func maxPan(_ zoom: CGFloat, _ baseExtent: CGFloat) -> CGFloat {
        guard baseExtent.isFinite && baseExtent > 0 else { return 0 }
        return max(0, (clampZoom(zoom) - 1) * baseExtent / 2)
    }

    static func clampPan(_ pan: CGFloat, zoom: CGFloat, baseExtent: CGFloat) -> CGFloat {
        guard pan.isFinite else { return 0 }
        let limit = maxPan(zoom, baseExtent)
        return min(limit, max(-limit, pan))
    }

    /// Pinça: o ponto sob o meio inicial dos dedos (`mid0`) fica sob `mid`.
    static func pinchPan(_ pan0: CGFloat, zoom0: CGFloat, zoom1: CGFloat, mid0: CGFloat, mid: CGFloat, centre: CGFloat) -> CGFloat {
        let z0 = zoom0 > 0 ? zoom0 : 1
        return (mid - centre) - (mid0 - centre - pan0) * (zoom1 / z0)
    }

    /// Fora do encaixe (ampliado ou reduzido)?
    static func isZoomed(_ zoom: CGFloat) -> Bool { abs(zoom - 1) > 0.005 }
    static func percent(_ zoom: CGFloat) -> Int { Int((zoom * 100).rounded()) }

    /// Encaixe da composição no palco com o zoom/pan da vista: escala (pt por
    /// px da composição) e origem (canto sup-esq da composição, em pt).
    static func fit(size: CGSize, composition: CGSize, zoom: CGFloat, pan: CGSize) -> (scale: CGFloat, origin: CGPoint) {
        let cw = max(1, composition.width), ch = max(1, composition.height)
        let base = min(size.width / cw, size.height / ch)
        let scale = base * zoom
        return (scale, CGPoint(x: (size.width - cw * scale) / 2 + pan.width, y: (size.height - ch * scale) / 2 + pan.height))
    }
}

/// Same pointer state machine as Android: stable IDs are owned by the arbiter.
/// Rebase at scale limits and discard angle changes while fingers cross.
struct StagePinchTracker {
    private(set) var factor: Float = 1
    private(set) var degrees: Float = 0
    private var span: CGFloat = 0
    private var angle: CGFloat = 0
    private var minimumSpan: CGFloat = 16
    private var ready = false
    mutating func start(_ a: CGPoint, _ b: CGPoint, minimum: CGFloat = 16) {
        factor = 1; degrees = 0; minimumSpan = max(1, minimum)
        span = hypot(a.x - b.x, a.y - b.y); angle = atan2(b.y - a.y, b.x - a.x)
        ready = span.isFinite && span >= minimumSpan
    }
    mutating func update(_ a: CGPoint, _ b: CGPoint, clamp: (Float) -> Float) -> Bool {
        guard a.x.isFinite && a.y.isFinite && b.x.isFinite && b.y.isFinite else { return false }
        let nextSpan = hypot(a.x - b.x, a.y - b.y), nextAngle = atan2(b.y - a.y, b.x - a.x)
        guard nextSpan.isFinite && nextAngle.isFinite else { return false }
        if !ready || nextSpan < minimumSpan {
            span = nextSpan; angle = nextAngle; ready = nextSpan >= minimumSpan
            return false
        }
        var delta = nextAngle - angle
        if delta > .pi { delta -= 2 * .pi }; if delta < -.pi { delta += 2 * .pi }
        let next = clamp(factor * Float(nextSpan / span))
        guard next.isFinite && next > 0 else { return false }
        let changed = abs(next - factor) > 0.000001 || abs(delta) > 0.000001
        factor = next; degrees += Float(delta * 180 / .pi); span = nextSpan; angle = nextAngle
        return changed
    }
}

/// Estado da vista do palco (sessão do editor, nunca vai para o projeto).
@MainActor final class StageViewZoom: ObservableObject {
    static let shared = StageViewZoom()
    @Published private(set) var zoom: CGFloat = 1
    /// Pan em pt da tela (o motor recebe em px do drawable).
    @Published private(set) var pan: CGSize = .zero
    /// Modo zoom ligado: a pinça amplia a VISTA, mesmo sobre a camada escolhida.
    @Published var zoomLock = false
    /// Omega chrome: muda a cada zoom/pan da vista (o rótulo "NN%" aparece e some em 2 s).
    @Published private(set) var viewTouch = 0
    private var projectKey: URL??
    /// O tamanho da composição na tela em 1× (pt) e pt → px: anotados pelo palco.
    private var extent: CGSize = .zero
    private var pixelScale: CGFloat = UIScreen.main.scale

    var zoomed: Bool { StageZoomMath.isZoomed(zoom) }

    func noteExtent(_ size: CGSize, pixelScale scale: CGFloat) {
        extent = size
        pixelScale = scale
    }

    /// Aplica e manda ao motor (só se mudou). `pixelScale` = pt → px do drawable.
    func set(zoom z: CGFloat, pan p: CGSize, engine: AureaEngine, pixelScale: CGFloat) {
        let nz = StageZoomMath.clampZoom(z)
        let np = CGSize(width: p.width.isFinite ? p.width : 0, height: p.height.isFinite ? p.height : 0)
        self.pixelScale = pixelScale
        if abs(nz - zoom) < 0.00001 && abs(np.width - pan.width) < 0.01 && abs(np.height - pan.height) < 0.01 { return }
        zoom = nz; pan = np
        viewTouch &+= 1
        engine.run { $0.setViewportZoom(Float(nz), panX: Float(np.width * pixelScale), panY: Float(np.height * pixelScale)) }
    }

    /// De volta ao encaixe (100 %), sem mostrar o rótulo (troca de projeto).
    func reset(engine: AureaEngine) {
        zoom = 1; pan = .zero
        engine.run { $0.setViewportZoom(1, panX: 0, panY: 0) }
    }

    /// Os degraus da coluna (+ / − / 100 %): zoom em torno do centro, pan preso.
    func zoomTo(_ z: CGFloat, engine: AureaEngine) {
        let nz = StageZoomMath.clampZoom(z)
        let k = zoom > 0 ? nz / zoom : 1
        let next = CGSize(width: StageZoomMath.clampPan(pan.width * k, zoom: nz, baseExtent: extent.width),
                          height: StageZoomMath.clampPan(pan.height * k, zoom: nz, baseExtent: extent.height))
        set(zoom: nz, pan: next, engine: engine, pixelScale: pixelScale)
        viewTouch &+= 1
    }

    /// Cada projeto abre no encaixe (o arquivo pode trazer um zoom salvo).
    func resetIfProjectChanged(_ url: URL?, engine: AureaEngine) {
        if case .some(let key) = projectKey, key == url { return }
        projectKey = .some(url)
        reset(engine: engine)
        ViewModeState.shared.resetForProject()
    }
}

// =============================================================================
// Omega chrome (área 02, P0-5/P1-2): os modos de visão da prévia
// =============================================================================

/// Tudo é da SESSÃO e só da prévia (par do `ViewModes` do Android): nada vai
/// para o projeto nem para o export. O espaçamento da grade fica no aparelho.
@MainActor final class ViewModeState: ObservableObject {
    static let shared = ViewModeState()
    static let gridMin = 5
    static let gridMax = 500
    /// A marca brilhante do girador (a referência começa nela).
    static let gridMark = 40
    private static let spacingKey = "omega.chrome.gridSpacing"

    @Published var columnOpen = false
    @Published var pixel = false
    @Published var grid = false
    @Published private(set) var gridSpacing: Int
    /// 0 normal, 1 só a(s) escolhida(s), 2 a(s) escolhida(s) a 50 % (só na prévia).
    @Published var solo = 0
    @Published var camera = false
    @Published var spacingOpen = false

    init() {
        let saved = UserDefaults.standard.integer(forKey: Self.spacingKey)
        gridSpacing = saved == 0 ? Self.gridMark : min(Self.gridMax, max(Self.gridMin, saved))
    }

    func changeGridSpacing(_ value: Int) {
        let v = min(Self.gridMax, max(Self.gridMin, value))
        guard v != gridSpacing else { return }
        gridSpacing = v
        UserDefaults.standard.set(v, forKey: Self.spacingKey)
    }

    /// O toque no destaque cicla NORMAL → SÓ A ESCOLHIDA → ESCOLHIDA A 50 % → NORMAL.
    func cycleSolo() { solo = (solo + 1) % 3 }

    /// Cada projeto abre com a prévia normal (a grade e o espaçamento ficam).
    func resetForProject() {
        columnOpen = false; pixel = false; solo = 0; camera = false; spacingOpen = false
    }

    /// Largura da coluna = a de uma célula do transporte de 7 (tela ÷ 7).
    static func columnWidth(_ screenWidth: CGFloat) -> CGFloat { min(72, max(44, screenWidth / 7)) }
}

/// Pedidos que nascem fora do editor (o painel de Ajustes mora na raiz do app)
/// e são atendidos pela barra do topo, que tem a apresentação da casca.
@MainActor final class OmegaChromeRequests: ObservableObject {
    static let shared = OmegaChromeRequests()
    @Published var commandSearch = false
    @Published var performanceTest = false
    /// Preset/script do After Effects (o seletor abre pela raiz do editor).
    @Published var aeImport = false
}

/// A coluna de modos (`viewmode.column`): presa à direita da prévia, abre pelo
/// último botão do transporte (o recorte vai da metade de baixo à coluna
/// inteira em 100 ms e volta). Consome todo toque (nada passa ao palco).
struct ViewModeColumn: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var modes = ViewModeState.shared
    @ObservedObject private var view = StageViewZoom.shared
    let width: CGFloat
    @State private var shown = false
    @State private var progress: CGFloat = 0
    @State private var gridAnchor: CGRect = .zero

    var body: some View {
        ZStack(alignment: .topTrailing) {
            if shown { column }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing)
        .onAppear { if modes.columnOpen { shown = true; progress = 1 } }
        .onChange(of: modes.columnOpen) { open in
            if open {
                shown = true
                withAnimation(.easeIn(duration: 0.1)) { progress = 1 }
            } else {
                withAnimation(.easeOut(duration: 0.1)) { progress = 0 }
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.11) { if !modes.columnOpen { shown = false } }
            }
        }
        .onChange(of: modes.solo) { _ in syncEngine() }
        .onChange(of: modes.pixel) { _ in syncEngine() }
        .onChange(of: model.selection) { _ in syncEngine() }
        .onChange(of: modes.spacingOpen) { open in
            if open { presentSpacing() } else { OmegaPopup.dismiss(Self.spacingOwner) }
        }
    }

    private func syncEngine() {
        model.setPreviewView(mode: model.selection.isEmpty ? 0 : UInt32(modes.solo), pixel: modes.pixel)
    }

    private var column: some View {
        GeometryReader { geometry in
            // 0.3.3 recursos: + a célula da caixa da seleção (6 células + zoom de 1,8).
            let unit = geometry.size.height / 6.8
            let hasSelection = !model.selection.isEmpty
            let hasCamera = model.layers.contains { $0.kind == 8 }
            VStack(spacing: 0) {
                cell(OmegaGlyph.pixelGrid, "om_vm_pixel", state: modes.pixel ? "common_on" : "common_off", on: modes.pixel,
                     tag: "viewmode.pixel") { modes.pixel.toggle() }
                    .frame(height: unit)
                cell(CupertinoGlyph.Grid, "om_vm_grid", state: modes.grid ? "common_on" : "common_off", on: modes.grid, tag: "viewmode.grid") {
                    // Ligar abre o girador do espaçamento; desligar fecha os dois.
                    modes.grid.toggle()
                    modes.spacingOpen = modes.grid
                }
                .frame(height: unit)
                .background(GeometryReader { g in
                    Color.clear
                        .onAppear { gridAnchor = g.frame(in: .global) }
                        .onChange(of: g.frame(in: .global)) { gridAnchor = $0 }
                })
                cell(modes.solo == 1 ? OmegaGlyph.soloOn : modes.solo == 2 ? CupertinoGlyph.CircleLefthalfFill : OmegaGlyph.soloOff,
                     "om_vm_solo", state: modes.solo == 1 ? "om_vm_solo_only" : modes.solo == 2 ? "om_vm_solo_dim" : "om_vm_solo_off",
                     on: modes.solo != 0, enabled: hasSelection, tag: "viewmode.solo") { modes.cycleSolo() }
                    .frame(height: unit)
                // 0.3.3 recursos: esconder/mostrar a caixa da seleção no palco (a 0.3.2 tinha o
                // botão na barra da camada; ela tampa o que está por baixo). Guardado no aparelho.
                cell(model.hideSelectionBox ? CupertinoGlyph.EyeSlash : CupertinoGlyph.Square,
                     model.hideSelectionBox ? "editor_mostrar_caixa_selecao" : "editor_esconder_caixa_selecao",
                     state: model.hideSelectionBox ? "common_off" : "common_on", on: model.hideSelectionBox,
                     tag: "stage.selectionBox") { model.hideSelectionBox.toggle() }
                    .frame(height: unit)
                // Visão pela câmera ativa (D5): existe na coluna, desligada. A prévia
                // do AUREA ainda olha sempre pela câmera ativa; desligar essa visão no
                // motor sem desalinhar as alças do palco fica para a próxima onda.
                cell(modes.camera ? CupertinoGlyph.VideocamFill : CupertinoGlyph.Videocam, "om_vm_camera",
                     state: hasCamera ? "om_vm_camera_unavailable" : "common_off", on: false, enabled: false, tag: "viewmode.camera") {}
                    .frame(height: unit)
                zoomCell.frame(height: unit * 1.8)
            }
            .frame(width: width, height: geometry.size.height)
            // Consome o toque nas folgas: nada passa para o palco embaixo.
            // 0.3.6 (sidebar): o tap vazio ficava no PAI das células e disputava o
            // toque com os botões (par do Android, onde o pai cancelava o clique);
            // agora é um fundo atrás delas, que só pega o que nenhuma célula pegou.
            .background(
                OmegaChrome.column
                    .contentShape(Rectangle())
                    .onTapGesture {}
                    .accessibilityHidden(true)
            )
            .mask(alignment: .bottom) {
                Rectangle().frame(height: geometry.size.height * (0.5 + 0.5 * progress))
            }
            .accessibilityElement(children: .contain)
            .accessibilityIdentifier("viewmode.column")
            .frame(maxWidth: .infinity, alignment: .trailing)
        }
    }

    private func cell(_ glyph: Character, _ key: String, state: String, on: Bool, enabled: Bool = true, tag: String,
                      action: @escaping () -> Void) -> some View {
        Button(action: action) {
            CupertinoGlyph.text(glyph, size: 20, color: on ? AureaColors.accent : AureaColors.text)
                .frame(maxWidth: .infinity, maxHeight: .infinity).contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .disabled(!enabled)
        .opacity(enabled ? 1 : 0.25)
        .accessibilityLabel(AureaText.t(key))
        .accessibilityValue(AureaText.t(state))
        .accessibilityIdentifier(tag)
    }

    /// A célula do zoom (peso 1,8): + · "NN%" · −, em degraus; tocar no número volta a 100 %.
    private var zoomCell: some View {
        VStack(spacing: 0) {
            Button { view.zoomTo(StageZoomMath.stepUp(view.zoom), engine: model.engine) } label: {
                CupertinoGlyph.text(CupertinoGlyph.Plus, size: 16).frame(maxWidth: .infinity, maxHeight: .infinity).contentShape(Rectangle())
            }.buttonStyle(AureaPressStyle(shrink: 1)).disabled(view.zoom >= StageZoomMath.maxZoom)
                .accessibilityLabel(AureaText.t("om_vm_zoom_in")).accessibilityIdentifier("viewmode.zoomIn")
            Button { view.zoomTo(1, engine: model.engine) } label: {
                Text(AureaText.t("common_percent", StageZoomMath.percent(view.zoom))).font(.aurea(size: 10)).monospacedDigit()
                    .lineLimit(1).minimumScaleFactor(0.7).foregroundStyle(AureaColors.text)
                    .frame(maxWidth: .infinity).frame(height: 20).contentShape(Rectangle())
            }.buttonStyle(AureaPressStyle(shrink: 1))
                .accessibilityLabel(AureaText.t("stage_zoom_fit", StageZoomMath.percent(view.zoom)))
                .accessibilityIdentifier("viewmode.zoomLabel")
            Button { view.zoomTo(StageZoomMath.stepDown(view.zoom), engine: model.engine) } label: {
                CupertinoGlyph.text(CupertinoGlyph.Minus, size: 16).frame(maxWidth: .infinity, maxHeight: .infinity).contentShape(Rectangle())
            }.buttonStyle(AureaPressStyle(shrink: 1)).disabled(view.zoom <= StageZoomMath.minZoom)
                .accessibilityLabel(AureaText.t("om_vm_zoom_out")).accessibilityIdentifier("viewmode.zoomOut")
        }
        .foregroundStyle(AureaColors.text)
        .background(OmegaChrome.zoomChip, in: RoundedRectangle(cornerRadius: 3))
        .padding(5)
    }

    /// O girador vertical do espaçamento, À ESQUERDA da coluna, centrado no botão da grade.
    private static let spacingOwner = UUID()
    private func presentSpacing() {
        OmegaPopup.present(anchor: gridAnchor, placement: .leftCentered, owner: Self.spacingOwner,
                           onDismiss: { ViewModeState.shared.spacingOpen = false }) {
            GridSpacingSpinner()
        }
    }
}

/// O girador vertical (45 × 179, a referência): o número em cima e a régua
/// embaixo, agulha no destaque e a marca brilhante em 40. Arrastar para cima
/// aumenta; 5 a 500 px da composição.
struct GridSpacingSpinner: View {
    @ObservedObject private var modes = ViewModeState.shared
    @State private var start: Int?
    private let tick: CGFloat = 4

    var body: some View {
        VStack(spacing: 0) {
            Text("\(modes.gridSpacing)").font(.aurea(size: 11, weight: .semibold)).monospacedDigit()
                .foregroundStyle(AureaColors.text).padding(.top, 6).padding(.bottom, 4)
            Canvas { context, size in
                let centre = size.height / 2
                let reach = Int(centre / tick) + 1
                let v0 = modes.gridSpacing
                for k in -reach...reach {
                    let v = v0 + k
                    guard v >= ViewModeState.gridMin && v <= ViewModeState.gridMax else { continue }
                    let y = centre - CGFloat(k) * tick
                    let mark = v == ViewModeState.gridMark
                    let length = mark || v % 10 == 0 ? size.width * 0.8 : v % 5 == 0 ? size.width * 0.55 : size.width * 0.3
                    var line = Path()
                    line.move(to: CGPoint(x: (size.width - length) / 2, y: y))
                    line.addLine(to: CGPoint(x: (size.width + length) / 2, y: y))
                    context.stroke(line, with: .color(mark ? AureaColors.text : AureaColors.text.opacity(0.4)), lineWidth: mark ? 2 : 1)
                }
                var needle = Path()
                needle.move(to: CGPoint(x: 0, y: centre)); needle.addLine(to: CGPoint(x: size.width, y: centre))
                context.stroke(needle, with: .color(AureaColors.accent), lineWidth: 2)
            }
            .frame(width: 36)
            .padding(.bottom, 6)
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { value in
                    if start == nil { start = modes.gridSpacing }
                    // Para cima aumenta: a régua anda junto com o dedo.
                    modes.changeGridSpacing((start ?? modes.gridSpacing) + Int((-value.translation.height / tick).rounded(.towardZero)))
                }
                .onEnded { _ in start = nil })
        }
        .frame(width: 45, height: 179)
        .background(OmegaChrome.column, in: RoundedRectangle(cornerRadius: 8))
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(AureaText.t("om_vm_grid_spacing"))
        .accessibilityValue("\(modes.gridSpacing)")
        .accessibilityAdjustableAction { direction in
            switch direction {
            case .increment: modes.changeGridSpacing(modes.gridSpacing + 1)
            case .decrement: modes.changeGridSpacing(modes.gridSpacing - 1)
            @unknown default: break
            }
        }
        .accessibilityIdentifier("viewmode.grid.spacing")
    }
}

/// O botão de modo zoom do canvas (canto sup-esq da prévia, 10 pt abaixo do
/// topo) e o rótulo "NN%" logo abaixo (par do `CanvasZoomMode` do Android).
/// Com camada escolhida o botão fica e liga/desliga o modo zoom (a pinça mexe na
/// vista em vez da camada). Sem camada, a pinça sempre mexe na vista: o botão
/// só aparece junto com o rótulo, apagado. O rótulo some com alfa em 2000 ms.
struct CanvasZoomMode: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var view = StageViewZoom.shared
    @State private var showing = false
    @State private var fade: Double = 0
    @State private var token = 0

    var body: some View {
        let hasSelection = !model.selection.isEmpty
        VStack(spacing: 0) {
            if hasSelection || showing {
                Button { view.zoomLock.toggle() } label: {
                    CupertinoGlyph.text(OmegaGlyph.zoomIn, size: 22,
                                        color: !hasSelection ? OmegaChrome.secondary : view.zoomLock ? AureaColors.accent : AureaColors.text)
                        .frame(width: 48, height: 48).contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .disabled(!hasSelection)
                .opacity(hasSelection ? 1 : fade)
                .accessibilityLabel(AureaText.t(view.zoomLock ? "stage_zoom_view_on" : "stage_zoom_view_off"))
                .accessibilityAddTraits(.isButton)
                .accessibilityIdentifier("stage.zoom.toggle")
            }
            if showing {
                Text(AureaText.t("common_percent", StageZoomMath.percent(view.zoom)))
                    .font(.aurea(size: 12)).monospacedDigit().foregroundStyle(OmegaChrome.secondary)
                    .shadow(color: AureaColors.imageScrim, radius: 1.5, x: 0, y: 1)
                    .lineLimit(1).frame(width: 50)
                    .opacity(fade)
                    .allowsHitTesting(false)
                    .accessibilityIdentifier("stage.zoom.label")
            }
        }
        .padding(.top, 10)
        .onChange(of: view.viewTouch) { _ in
            token &+= 1
            let mine = token
            showing = true
            fade = 1
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
                guard mine == token else { return }
                withAnimation(.linear(duration: 2)) { fade = 0 }
                DispatchQueue.main.asyncAfter(deadline: .now() + 2.05) { if mine == token { showing = false } }
            }
        }
    }
}
