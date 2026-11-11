// =============================================================================
//  Aurea / platform / ios / app / ColorFillPanel.swift
//
//  Omega aparência (área 05) · P0-3 + P1-4 (UI): o painel COR E PREENCHIMENTO
//  da Omega na moldura comum (`PanelFrame`, sem abas). Par do
//  `ColorFillPanel.kt` do Android (mesmos ids, textos e medidas).
//
//  • Barra de tipo (cartão de 29,5, raio 5, topo visível a 13; fileira de toque
//    de 44): forma SDF = Nenhum · Cor · Degradê; texto = Cor; vetor/grupo =
//    Original. Mídia não aparece nesta versão.
//  • Conteúdo de 127,5 (9 de margem lateral): Cor = o seletor embutido
//    (ColorPickerKit.swift); Degradê = Linear/Radial/Angular + as duas amostras
//    (abrem o balão) com a barra do degradê; Nenhum/Original = o ícone grande.
//  • ◆/Curva = a cor da forma (grupo 0), só no tipo Cor da forma; segurar ◆
//    apaga as chaves ShapeParam 32..35 num passo.
//  • Palco: com o Degradê aberto, as pontas do degradê viram alças
//    (`FillStageState`; o PreviewMetalView desenha e arrasta).
//  O contorno da forma NÃO mora aqui: é Borda e sombra › Traço.
// =============================================================================
import SwiftUI
import UIKit

/// O estado do palco que este painel liga: o modo "pontas do degradê" (P1-4).
/// O PreviewMetalView desenha as duas alças e arrasta a mais perta do toque.
@MainActor final class FillStageState: ObservableObject {
    static let shared = FillStageState()
    /// As alças das pontas do degradê aparecem e pegam o toque do palco.
    @Published var gradientHandles = false
    /// Ponta segura no palco agora (0 início, 1 fim; −1 nenhuma).
    @Published var grabbed = -1
}

/// Os tipos da barra (ids `fill.tab.<rawValue>`; textos `om_ap_fill_<rawValue>`).
enum ColorFillTab: String {
    case none, color, gradient, media, original

    var glyph: Character {
        switch self {
        case .none: return CupertinoGlyph.Circle
        case .color: return CupertinoGlyph.CircleFill
        case .gradient: return CupertinoGlyph.CircleLefthalfFill
        case .media: return CupertinoGlyph.Photo
        case .original: return CupertinoGlyph.Paintbrush
        }
    }

    var key: String { "om_ap_fill_" + rawValue }
}

/// Medidas do painel (as da referência).
/// 0.3.4 painéis: barra, botões e amostras pelas medidas centrais (`PanelMetrics`);
/// o conteúdo cresce com o painel (127,5 é o mínimo). Par do `ColorFillPanel.kt`.
enum ColorFillMetrics {
    static let barHeight: CGFloat = PanelMetrics.fieldHeight
    static let barRow: CGFloat = 44
    static let barTop: CGFloat = 13
    static let radius: CGFloat = PanelMetrics.fieldRadius
    static let side: CGFloat = 9
    static let content: CGFloat = 127.5
    static let gradientButton = CGSize(width: PanelMetrics.fieldWidth, height: PanelMetrics.fieldHeight)
    static let gradientTop: CGFloat = 15
    static let gradientSwatch: CGFloat = 30
    static let gradientBar: CGFloat = 12
}

/// Lê o retângulo GLOBAL de uma view (a âncora do balão).
private struct GlobalFrameReader: View {
    @Binding var frame: CGRect
    var body: some View {
        GeometryReader { geometry in
            Color.clear
                .onAppear { frame = geometry.frame(in: .global) }
                .onChange(of: geometry.frame(in: .global)) { frame = $0 }
        }
    }
}

struct ColorFillPanel: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var stage = FillStageState.shared
    @State private var textColor: [Float] = [1, 1, 1, 1]
    @State private var startFrame: CGRect = .zero
    @State private var endFrame: CGRect = .zero

    /// O que a camada escolhida mostra aqui.
    private enum Target { case shape, text, original, other }

    private var id: Int64 { model.primarySelection ?? 0 }
    private var layerKind: UInt32 { model.selectedLayer?.kind ?? 0 }
    private var fill: [Float] { model.shapeFill }
    private var hasFill: Bool { fill.count >= 24 }

    private var target: Target {
        switch layerKind {
        case 4: return .text
        case 12: return .original
        case 5:
            if model.isVectorLayer { return .original }
            if hasFill && fill[ShapeFillIndex.supported] < 0.5 { return .original }
            return .shape
        default: return .other
        }
    }

    /// 0 nenhum, 1 cor, 2 degradê (forma).
    private var fillMode: Int {
        guard hasFill else { return 1 }
        if fill[ShapeFillIndex.filled] < 0.5 { return 0 }
        return fill[ShapeFillIndex.kind] > 0.5 ? 2 : 1
    }

    private var tabs: [ColorFillTab] {
        switch target {
        case .shape: return [.none, .color, .gradient]
        case .text: return [.color]
        case .original: return [.original]
        case .other: return []
        }
    }

    private var currentTab: ColorFillTab {
        switch target {
        case .shape: return fillMode == 0 ? .none : fillMode == 2 ? .gradient : .color
        case .text: return .color
        case .original, .other: return .original
        }
    }

    private var gradientOn: Bool { target == .shape && fillMode == 2 }

    // MARK: ◆ e Curva (cor da forma, grupo 0)

    private var keyable: Bool { target == .shape && fillMode == 1 && hasFill }

    private var keyLook: KeyframeLook {
        guard keyable else { return .none }
        let here = UInt32(max(0, fill[ShapeFillIndex.keyHere]))
        let animated = UInt32(max(0, fill[ShapeFillIndex.animated]))
        if here & 1 != 0 { return .keyHere }
        return animated & 1 != 0 ? .animated : .none
    }

    /// As trilhas da cor de preenchimento (ShapeParam 32..35).
    private var colorTracks: [[KeyframeItem]] {
        let keys = model.keyframes[id] ?? []
        return (UInt32(32)...UInt32(35)).map { param in
            keys.filter { $0.property == 35 && $0.paramIndex == param }.sorted { $0.time < $1.time }
        }
    }

    private var rail: PanelRail {
        let m = model
        let canKey = keyable
        return PanelRail(
            onBack: { m.panel = .none },
            keyframeLook: keyLook,
            onKeyframe: canKey ? { m.toggleShapeFillKey(ShapeFillIndex.groupColor) } : nil,
            onDeleteAllKeyframes: canKey ? { deleteColorKeys() } : nil,
            onCurve: canKey ? { openColorCurve() } : nil,
            curveReady: preferredCurveTrack(colorTracks).count >= 2)
    }

    var body: some View {
        PanelFrame(rail: rail, scrollContent: false) {
            VStack(spacing: 0) {
                if !tabs.isEmpty {
                    typeBar
                        .padding(.top, ColorFillMetrics.barTop - (ColorFillMetrics.barRow - ColorFillMetrics.barHeight) / 2)
                }
                // 0.4.1 (mockup "interface 03"): as 7 cores rápidas do sistema, um toque = um desfazer.
                if currentTab == .color && (target == .shape || target == .text) { quickColors }
                // 0.3.4 painéis: o conteúdo ocupa a altura que sobra (o painel é alto desde a 0.3.4).
                content
                    .frame(minHeight: ColorFillMetrics.content, maxHeight: .infinity)
                    .padding(.horizontal, ColorFillMetrics.side)
                    .padding(.top, 4)
                    .padding(.bottom, 10)
                    .accessibilityElement(children: .contain)
                    .accessibilityIdentifier("fill.content")
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        }
        .foregroundStyle(AureaColors.text)
        .onAppear { loadText(); syncStage(); focusTimeline() }
        .onChange(of: id) { _ in loadText(); focusTimeline() }
        .onChange(of: model.status.modelRevision) { _ in loadText() }
        .onChange(of: model.status.playhead) { _ in loadText() }
        .onChange(of: gradientOn) { _ in syncStage() }
        .onDisappear {
            stage.gradientHandles = false
            stage.grabbed = -1
            model.timelineFocus = nil
        }
    }

    // MARK: Barra de tipo

    private var typeBar: some View {
        HStack(spacing: 0) {
            ForEach(tabs, id: \.self) { tab in typeButton(tab) }
        }
        .frame(height: ColorFillMetrics.barRow)
        .background(
            RoundedRectangle(cornerRadius: ColorFillMetrics.radius)
                .fill(AureaColors.fxListSurface)
                .frame(height: ColorFillMetrics.barHeight)
        )
        .padding(.horizontal, ColorFillMetrics.side)
    }

    private func typeButton(_ tab: ColorFillTab) -> some View {
        let on = tab == currentTab
        return Button { select(tab) } label: {
            CupertinoGlyph.text(tab.glyph, size: 22, color: on ? AureaColors.accent : AureaColors.text)
                .frame(maxWidth: .infinity)
                .frame(height: ColorFillMetrics.barRow)
                .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .accessibilityLabel(AureaText.t(tab.key))
        .accessibilityAddTraits(on ? .isSelected : [])
        .accessibilityIdentifier("fill.tab." + tab.rawValue)
    }

    /// Trocar o tipo grava um passo (motor); ignorado com o conta-gotas ligado.
    private func select(_ tab: ColorFillTab) {
        guard target == .shape, model.eyedropper == nil, tab != currentTab else { return }
        switch tab {
        case .none: model.setShapeFillMode(0)
        case .color: model.setShapeFillMode(1)
        case .gradient: model.setShapeFillMode(2)
        case .media, .original: break
        }
    }

    // MARK: Conteúdo

    @ViewBuilder private var content: some View {
        switch target {
        case .shape:
            if fillMode == 0 {
                bigIcon(.none)
            } else if fillMode == 2 {
                gradientBody
            } else {
                shapeColorPicker
            }
        case .text:
            textColorPicker
        case .original:
            originalBody
        case .other:
            Color.clear
        }
    }

    private func bigIcon(_ tab: ColorFillTab) -> some View {
        CupertinoGlyph.text(tab.glyph, size: 56, color: AureaColors.muted)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(AureaColors.fxLowSurface, in: RoundedRectangle(cornerRadius: ColorFillMetrics.radius))
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(AureaText.t(tab.key))
    }

    // MARK: Cores rápidas (0.4.1, mockup "interface 03"; par do `QuickColors` do Android)

    /// As cores do mockup (branco e as do sistema iOS), em sRGB 0xRRGGBB.
    private static let quickColorList: [UInt32] = [0xFFFFFF, 0xFF453A, 0xFF9F0A, 0xFFD60A, 0x30D158, 0x0A84FF, 0xBF5AF2]

    private var quickColors: some View {
        HStack(spacing: 0) {
            ForEach(Array(Self.quickColorList.enumerated()), id: \.offset) { item in
                let rgb = item.element
                let r = Float((rgb >> 16) & 0xFF) / 255, g = Float((rgb >> 8) & 0xFF) / 255, b = Float(rgb & 0xFF) / 255
                if item.offset > 0 { Spacer(minLength: 0) }
                Button { pickQuickColor([r, g, b, 1]) } label: {
                    Circle().fill(Color(red: Double(r), green: Double(g), blue: Double(b)))
                        .overlay(Circle().stroke(Color.white.opacity(0.16), lineWidth: 1))
                        .frame(width: 28, height: 28)
                        .contentShape(Circle())
                }
                .buttonStyle(AureaPressStyle())
                .accessibilityLabel(String(format: "#%06X", rgb))
                .accessibilityIdentifier("fill.quick.\(item.offset)")
            }
        }
        .padding(.horizontal, 12).padding(.vertical, 6)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("fill.quickColors")
    }

    private func pickQuickColor(_ rgba: [Float]) {
        model.beginGesture("cor rápida")
        if target == .text { setTextColor(rgba) } else { model.setShapeFillValue(ShapeFillIndex.groupColor, rgba) }
        model.endGesture()
    }

    private var shapeColorPicker: some View {
        let m = model
        return EmbeddedColorPicker(
            color: m.shapeFillColor(at: ShapeFillIndex.color),
            withAlpha: true,
            onBegin: { m.beginGesture("cor do preenchimento") },
            onChange: { m.setShapeFillValue(ShapeFillIndex.groupColor, $0) },
            onEnd: { m.endGesture() })
    }

    // MARK: Texto (cor do texto, o mesmo caminho do painel de Texto)

    private var textColorPicker: some View {
        let m = model
        return EmbeddedColorPicker(
            color: textColor,
            withAlpha: true,
            onBegin: { m.beginGesture("cor do texto") },
            onChange: { c in setTextColor(c) },
            onEnd: { m.endGesture() })
    }

    private func loadText() {
        guard target == .text, let row = model.engine.text(forLayer: id) else { return }
        let values = (row["color"] as? [NSNumber] ?? []).map(\.floatValue)
        let next = values.count >= 4 ? Array(values.prefix(4)) : [1, 1, 1, 1]
        if next != textColor { textColor = next }
    }

    private func setTextColor(_ c: [Float]) {
        guard c.count >= 4, model.selectedLayer?.locked != true else { return }
        model.engine.setText(id, colorR: c[0], g: c[1], b: c[2], a: c[3])
        textColor = Array(c.prefix(4))
        model.refreshModel(force: true)
    }

    // MARK: Original (vetor e grupo)

    private var originalBody: some View {
        let group = layerKind == 12
        return HStack(spacing: 12) {
            CupertinoGlyph.text(ColorFillTab.original.glyph, size: 56, color: AureaColors.muted)
                .frame(width: 96)
                .accessibilityHidden(true)
            VStack(alignment: .leading, spacing: 10) {
                Text(AureaText.t("om_ap_fill_original_hint"))
                    .font(.aurea(size: 13))
                    .foregroundStyle(AureaColors.text)
                    .fixedSize(horizontal: false, vertical: true)
                Button { openOriginal(group: group) } label: {
                    Text(AureaText.t(group ? "editor_entrar_grupo" : "panel_vetor"))
                        .font(.aurea(size: PanelMetrics.chipText, weight: .semibold))
                        .foregroundStyle(AureaColors.onAccent)
                        .padding(.horizontal, PanelMetrics.chipPad)
                        .frame(minHeight: PanelMetrics.chipHeight)
                        .background(AureaColors.accent, in: Capsule())
                        .frame(minHeight: 44)
                        .contentShape(Rectangle())
                }
                .buttonStyle(AureaPressStyle())
                .accessibilityIdentifier("fill.original.open")
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .padding(.horizontal, 10)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(AureaColors.fxLowSurface, in: RoundedRectangle(cornerRadius: ColorFillMetrics.radius))
    }

    private func openOriginal(group: Bool) {
        if group { model.openGroup(id) } else { model.openPanel(.vector) }
    }

    // MARK: Degradê (P1-4)

    private var gradientBody: some View {
        VStack(spacing: 0) {
            HStack(spacing: 8) {
                gradientTypeButton(0, id: "linear", key: "om_ap_fill_grad_linear")
                gradientTypeButton(1, id: "radial", key: "om_ap_fill_grad_radial")
                gradientTypeButton(2, id: "sweep", key: "om_ap_fill_grad_sweep")
                Spacer(minLength: 0)
            }
            .padding(.top, ColorFillMetrics.gradientTop)
            HStack(spacing: 10) {
                gradientSwatch(start: true)
                gradientBar
                gradientSwatch(start: false)
            }
            .padding(.top, 16)
            // As pontas no palco valem para a tela: o degradê corre da esquerda para a direita.
            .keepLtr()
            Text(AureaText.t("om_ap_fill_grad_handles"))
                .font(.aurea(size: PanelMetrics.note))
                .foregroundStyle(AureaColors.muted)
                .lineLimit(2)
                .padding(.top, 12)
            Spacer(minLength: 0)
        }
    }

    private func gradientTypeButton(_ type: Int, id tag: String, key: String) -> some View {
        let on = hasFill && Int(fill[ShapeFillIndex.gradientType].rounded()) == type
        return Button {
            guard !on, model.eyedropper == nil else { return }
            model.setShapeGradientType(type)
        } label: {
            Text(AureaText.t(key))
                .font(.aurea(size: PanelMetrics.chipText, weight: .semibold))
                .foregroundStyle(on ? AureaColors.accent : AureaColors.text)
                .lineLimit(1)
                .minimumScaleFactor(0.7)
                .padding(.horizontal, 4)
                // 0.3.4 painéis: até 72 de largura; encolhe para os três caberem em 360.
                .frame(minWidth: 0, maxWidth: ColorFillMetrics.gradientButton.width)
                .frame(height: ColorFillMetrics.gradientButton.height)
                .background(on ? AureaColors.fxLowSurface : AureaColors.fxListSurface,
                            in: RoundedRectangle(cornerRadius: ColorFillMetrics.radius))
                .overlay(RoundedRectangle(cornerRadius: ColorFillMetrics.radius)
                    .stroke(on ? AureaColors.accent : Color.clear, lineWidth: 1))
                .padding(.vertical, 8)
                .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .padding(.vertical, -8)
        .accessibilityAddTraits(on ? .isSelected : [])
        .accessibilityIdentifier("fill.gradient." + tag)
    }

    private var gradientBar: some View {
        let c0 = PickerColor.color(model.shapeFillColor(at: ShapeFillIndex.color0))
        let c1 = PickerColor.color(model.shapeFillColor(at: ShapeFillIndex.color1))
        return AureaColorSwatch(color: Color.clear)
            .overlay(LinearGradient(colors: [c0, c1], startPoint: .leading, endPoint: .trailing))
            .frame(height: ColorFillMetrics.gradientBar)
            .frame(maxWidth: .infinity)
            .clipShape(Capsule())
            .accessibilityHidden(true)
    }

    private func gradientSwatch(start: Bool) -> some View {
        let values = model.shapeFillColor(at: start ? ShapeFillIndex.color0 : ShapeFillIndex.color1)
        let key = start ? "om_ap_fill_grad_start" : "om_ap_fill_grad_end"
        let hex = values.count >= 4
            ? model.engine.formatColorCode(values[0], g: values[1], b: values[2], a: values[3], withAlpha: true) : ""
        return Button { openGradientColor(start: start) } label: {
            AureaColorSwatch(color: PickerColor.color(values))
                .frame(width: ColorFillMetrics.gradientSwatch, height: ColorFillMetrics.gradientSwatch)
                .clipShape(RoundedRectangle(cornerRadius: ColorFillMetrics.radius))
                .overlay(RoundedRectangle(cornerRadius: ColorFillMetrics.radius).stroke(AureaColors.border, lineWidth: 1))
                .background(GlobalFrameReader(frame: start ? $startFrame : $endFrame))
                .padding(10)
                .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .padding(-10)
        .accessibilityLabel(AureaText.t(key))
        .accessibilityValue(hex)
        .accessibilityIdentifier(start ? "fill.gradient.start" : "fill.gradient.end")
    }

    /// Amostra do degradê: o balão do seletor ancorado nela (grupos 1/2), um
    /// passo de desfazer até o balão fechar.
    private func openGradientColor(start: Bool) {
        guard model.selectedLayer?.locked != true, model.eyedropper == nil else { return }
        let m = model
        let group = start ? ShapeFillIndex.groupStart : ShapeFillIndex.groupEnd
        let initial = m.shapeFillColor(at: start ? ShapeFillIndex.color0 : ShapeFillIndex.color1)
        let anchor = start ? startFrame : endFrame
        m.beginGesture(start ? "cor inicial do degradê" : "cor final do degradê")
        m.colorSheet = ColorSheetRequest(
            title: AureaText.t(start ? "om_ap_fill_grad_start" : "om_ap_fill_grad_end"),
            initial: initial.count >= 4 ? initial : [1, 1, 1, 1],
            withAlpha: true,
            anchor: anchor == .zero ? nil : anchor,
            onChange: { r, g, b, a in m.setShapeFillValue(group, [r, g, b, a]) },
            onDone: { m.endGesture() })
    }

    // MARK: Palco e timeline

    private func syncStage() {
        if stage.gradientHandles != gradientOn { stage.gradientHandles = gradientOn }
        if !gradientOn && stage.grabbed != -1 { stage.grabbed = -1 }
    }

    /// A timeline foca a trilha da cor (a mesma que o ◆ grava).
    private func focusTimeline() {
        guard target == .shape else { return }
        model.timelineFocus = [TimelineTrack(property: 35, effect: 0, param: 32)]
    }

    private func openColorCurve() {
        let track = preferredCurveTrack(colorTracks)
        guard track.count >= 2 else { return }
        let index = max(0, min(track.count - 2, track.lastIndex { $0.time <= model.localPlayhead } ?? 0))
        model.openCurve(property: 35, effect: track[index].effectIndex, param: track[index].paramIndex, time: track[index].time)
    }

    /// Segurar ◆: apaga as chaves da cor (ShapeParam 32..35) num passo de desfazer.
    private func deleteColorKeys() {
        let keys = (model.keyframes[id] ?? []).filter { $0.property == 35 && (UInt32(32)...UInt32(35)).contains($0.paramIndex) }
        guard !keys.isEmpty, model.selectedLayer?.locked != true else { return }
        let references: [NSNumber] = keys.flatMap { key -> [NSNumber] in
            [NSNumber(value: key.property), NSNumber(value: key.effectIndex), NSNumber(value: key.paramIndex),
             NSNumber(value: Int64(key.time))]
        }
        model.beginGesture("apagar keyframes")
        _ = model.engine.keyframeSelection(id, references: references, action: 2, delta: 0)
        model.endGesture()
    }
}

// =============================================================================
// 0.5.2: PREENCHIMENTO COM TEXTURA do texto (TextTextureSection.kt). Ligado, as
// letras levam uma textura procedural do motor em vez da cor lisa
// (Engine::set_text_texture). Mesma ordem, textos e acessibilidade do Android.
// =============================================================================
let textTextureNames = ["tex_preset_gold", "tex_preset_silver", "tex_preset_brushed", "tex_preset_wood", "tex_preset_marble",
                        "tex_preset_stone", "tex_preset_concrete", "tex_preset_glitter", "tex_preset_holo", "tex_preset_neon",
                        "tex_preset_fire", "tex_preset_water", "tex_preset_galaxy", "tex_preset_paper", "tex_preset_denim",
                        "tex_preset_leather", "tex_preset_rust", "tex_preset_ice"]
/// Duas cores de amostra por textura (claro → escuro), na ordem de `textTextureNames`.
private let textTextureSwatch: [(UInt32, UInt32)] = [
    (0xFFEFA8, 0x8A5A10), (0xF4F6FA, 0x3A3D44), (0xC9CCD2, 0x6E7178), (0xC8915A, 0x6B3E1C), (0xF2F1EE, 0x8C8D92),
    (0x9C978E, 0x4E4A44), (0xB3B0AA, 0x6F6D69), (0xF1E3F5, 0x8E8A99), (0x9FE6FF, 0xE9A2FF), (0xFF3DCB, 0x2CE6FF),
    (0xFFE07A, 0xB31A00), (0x8FE3F0, 0x0B4C7A), (0xB25BE0, 0x0A0420), (0xF7F3EA, 0xD9D3C5), (0x5D7BA6, 0x16264A),
    (0x9C6236, 0x4A2610), (0xD58A3E, 0x4A1C0A), (0xE8F7FF, 0x6DB3DD),
]

struct NativeTextTextureSection: View {
    @EnvironmentObject private var model: AureaModel
    @State private var values: [Float] = []
    private var id: Int64 { model.primarySelection ?? 0 }
    private func load() { values = model.engine.textTexture(id).map(\.floatValue) }
    private func set(_ changes: [Int: Float]) {
        guard values.count >= 17 else { return }
        var next = values
        for (k, v) in changes where k < 17 { next[k] = v }
        _ = model.engine.setTextTexture(id, values: next.prefix(17).map { NSNumber(value: $0) })
        model.refreshModel(force: true); load()
    }
    private var kind: Int { values.isEmpty ? 0 : Int(values[0].rounded()) }
    private var isOn: Binding<Bool> {
        Binding(get: { (1...18).contains(kind) }, set: { set([0: $0 ? 1 : 0]) })
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            if values.count >= 17 {
                PanelSection(title: AureaText.t("tex_section"), glyph: CupertinoGlyph.Paintbrush, tag: "text.sec.texture", isOn: isOn) {
                    LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 6), count: 3), spacing: 6) {
                        ForEach(0..<textTextureNames.count, id: \.self) { n in presetCell(n) }
                    }.padding(.top, 6)
                    if (1...18).contains(kind) { controls }
                }
            }
        }.onAppear { load() }.onChange(of: id) { _ in load() }
            .onChange(of: model.status.modelRevision) { _ in load() }
    }
    private func presetCell(_ n: Int) -> some View {
        let label = AureaText.t(textTextureNames[n])
        let selected = kind == n + 1
        let (a, b) = textTextureSwatch[n]
        return Button { set([0: Float(n + 1)]) } label: {
            VStack(spacing: 3) {
                RoundedRectangle(cornerRadius: 6)
                    .fill(LinearGradient(colors: [Color(hex: a), Color(hex: b)], startPoint: .topLeading, endPoint: .bottomTrailing))
                    .frame(height: 34)
                Text(label).font(.aurea(size: 11)).lineLimit(1)
                    .foregroundStyle(selected ? AureaColors.accent : AureaColors.text)
            }
            .padding(4)
            .overlay(RoundedRectangle(cornerRadius: 8).stroke(selected ? AureaColors.accent : .clear, lineWidth: 1.5))
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityLabel(AureaText.t("tex_preset_desc", label))
        .accessibilityAddTraits(selected ? [.isButton, .isSelected] : .isButton)
        .accessibilityIdentifier("text.texture.\(n + 1)")
    }
    private var controls: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text(AureaText.t("tex_mapping")).font(.aurea(size: 13)).padding(.top, 8)
            PanelItemGrid(items: [("tex_map_block", CupertinoGlyph.Square), ("tex_map_letter", CupertinoGlyph.Textformat)].enumerated().map { n, entry in
                PanelItem(glyph: entry.1, label: AureaText.t(entry.0), tag: "text.texture.map.\(n)", on: Int(values[1].rounded()) == n) { set([1: Float(n)]) }
            }).padding(.top, 6)
            HStack {
                Text(AureaText.t("tex_stroke_too")).font(.aurea(size: 14, weight: .semibold)).frame(maxWidth: .infinity, alignment: .leading)
                AureaToggle(checked: values[2] > 0.5, onCheckedChange: { set([2: $0 ? 1 : 0]) })
            }.frame(height: 48)
            NativePanelRuler(label: AureaText.t("tex_scale"), value: values[3] * 100, step: 1, range: 5...2000, unit: "%", keypad: true) { set([3: $0 / 100]) }
            NativePanelRuler(label: AureaText.t("tex_rotation"), value: values[4], step: 0.5, range: -360...360, unit: "°", keypad: true) { set([4: $0]) }
            NativePanelRuler(label: AureaText.t("tex_offset_x"), value: values[5] * 100, step: 1, range: -1000...1000, unit: "%", keypad: true) { set([5: $0 / 100]) }
            NativePanelRuler(label: AureaText.t("tex_offset_y"), value: values[6] * 100, step: 1, range: -1000...1000, unit: "%", keypad: true) { set([6: $0 / 100]) }
            NativePanelRuler(label: AureaText.t("tex_contrast"), value: values[7] * 100, step: 0.5, range: -100...100, unit: "%", keypad: true) { set([7: $0 / 100]) }
            NativePanelRuler(label: AureaText.t("tex_brightness"), value: values[8] * 100, step: 0.5, range: -100...100, unit: "%", keypad: true) { set([8: $0 / 100]) }
            HStack(spacing: 10) {
                Text(AureaText.t("tex_tint")).font(.aurea(size: 13)).frame(maxWidth: .infinity, alignment: .leading)
                NativePanelColorWell(values: [values[9], values[10], values[11], 1]) {
                    let layer = id
                    model.beginGesture(AureaText.t("tex_tint"))
                    model.colorSheet = ColorSheetRequest(title: AureaText.t("tex_tint"), initial: [values[9], values[10], values[11], 1], onChange: { r, g, b, _ in
                        var current = model.engine.textTexture(layer).map(\.floatValue)
                        guard current.count >= 17 else { return }
                        current[9] = r; current[10] = g; current[11] = b; current[12] = 1
                        _ = model.engine.setTextTexture(layer, values: current.prefix(17).map { NSNumber(value: $0) })
                        model.refreshModel(force: true); load()
                    }, onDone: { model.endGesture() })
                }
            }.frame(height: 44)
            NativePanelRuler(label: AureaText.t("tex_speed"), value: values[13] * 100, step: 1, range: 0...1000, unit: "%", keypad: true) { set([13: $0 / 100]) }
            NativePanelRuler(label: AureaText.t("tex_relief"), value: values[14] * 100, step: 0.5, range: 0...100, unit: "%", keypad: true) { set([14: $0 / 100]) }
            NativePanelRuler(label: AureaText.t("tex_light_angle"), value: values[15], step: 0.5, range: -360...360, unit: "°", keypad: true) { set([15: $0]) }
            NativePanelRuler(label: AureaText.t("tex_variation"), value: values[16], step: 0.1, range: 0...9999, keypad: true) { set([16: $0.rounded()]) }
        }
    }
}
