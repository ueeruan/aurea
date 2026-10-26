// =============================================================================
//  Aurea / platform / ios / app / LayerPanelView.swift
//
//  Omega painel (área 04): O PAINEL DA CAMADA (`fragment_element_edit` da
//  referência, 200 pt por cima da timeline). Substitui a doca de fichas. Par do
//  `editor/LayerPanel.kt` (mesmos ids, mesma grade, mesmas regras do motor).
//
//    y=15  Fileira 1 · 38 · 5 colunas  [Velocidade] [Cortar início|Dividir|Cortar fim] [Áudio]
//                                       (cabeçote fora do clipe: [◀ par ▶] no lugar do trio)
//    y=63  Fileira 2 · 57 · 3 colunas  [Cor e preenchimento] [Borda e sombra] [Mesclagem e opacidade]
//    y=126 Fileira 3 · 57 · 3–4 col.   [Mover e transformar] [Editar…] [Presets] [Efeitos]
//
//  Também moram aqui (par do `SpeedAudioPanels.kt`): o painel de velocidade e o
//  de som na moldura comum, o retempo, a régua de velocidade e os painéis
//  embutidos do grupo (velocidade) e do áudio (volume).
// =============================================================================
import SwiftUI

/// Bits de `Engine::query_layer_panel` (EngineOmegaLayer.cpp; iguais no Android).
enum LayerPanelBits {
    static let trimStart: UInt32 = 1
    static let split: UInt32 = 2
    static let trimEnd: UInt32 = 4
    static let extendStart: UInt32 = 8
    static let extendEnd: UInt32 = 16
    static let moveStart: UInt32 = 32
    static let moveEnd: UInt32 = 64
    static let retimeExtendStart: UInt32 = 128
    static let retimeShortenStart: UInt32 = 256
    static let retimeShortenEnd: UInt32 = 512
    static let retimeExtendEnd: UInt32 = 1024
    static let locked: UInt32 = 2048
    static let hasAudio: UInt32 = 4096
    static let gainKeys: UInt32 = 8192
    static let missingMedia: UInt32 = 16384
    static let speedBakeable: UInt32 = 32768
}

/// Um botão da grade. O id é `layer.panel.<rawValue>` nas duas plataformas.
enum LayerSlot: String, CaseIterable {
    case colorFill, borderShadow, blendOpacity, moveTransform, edit, presets, effects
}

/// O que a vaga "Editar…" é em cada tipo (uma por vez, como na referência).
enum LayerEdit: Equatable {
    case shape, mediaShape, points, drawing, text, group, camera, light, particles, material
    var glyph: Character {
        switch self {
        case .shape, .mediaShape: return ShellGlyph.SliderHorizontalBelowRectangle
        case .points: return CupertinoGlyph.PencilOutline
        case .drawing: return CupertinoGlyph.Scribble
        case .text: return CupertinoGlyph.Textformat
        case .group: return CupertinoGlyph.ArrowDownRightSquare
        case .camera: return CupertinoGlyph.CameraFill
        case .light: return CupertinoGlyph.Lightbulb
        case .particles: return CupertinoGlyph.Bolt
        case .material: return CupertinoGlyph.Cube
        }
    }
    var label: String {
        switch self {
        case .shape, .mediaShape: return "sh_dock_edit_shape"
        case .points: return "om_lp_edit_points"
        case .drawing: return "om_lp_edit_drawing"
        case .text: return "sh_dock_edit_text"
        case .group: return "om_lp_edit_group"
        case .camera: return "om_lp_camera_options"
        case .light: return "om_lp_light_options"
        case .particles: return "om_lp_edit_particles"
        case .material: return "panel_material_ambiente"
        }
    }
}

/// Para onde a Velocidade leva: o painel completo (mídia), o embutido (grupo) ou o constante (sem fonte).
enum SpeedRoute: Equatable { case panel, group, constant }

/// O que o painel precisa da camada.
struct PanelLayerInfo: Equatable {
    var id: Int64
    var kind: UInt32
    var locked: Bool
    var start: Int32
    var end: Int32
    var adjustment = false
    var vector = false
    /// Vetor de desenho à mão (a área 06 liga quando o tipo existir).
    var drawing = false
    var text3D = false
    var effectCount: UInt32 = 0
}

/// A grade de um tipo (par do `LayerPanelSpec` do Kotlin): fileira 2 com 3
/// vagas, fileira 3 com 1 a 4 (nil = invisível, guarda o espaço).
struct LayerPanelSpec: Equatable {
    let row2: [LayerSlot?]
    let row3: [LayerSlot?]
    let edit: LayerEdit?
    let tallRow3: Bool
    let speedArrow: Bool
    let speed: SpeedRoute

    static func of(_ l: PanelLayerInfo) -> LayerPanelSpec {
        let std: [LayerSlot?] = [.moveTransform, .edit, .presets, .effects]
        let none: [LayerSlot?] = [nil, nil, nil]
        if l.adjustment || l.kind == 7 {
            return LayerPanelSpec(row2: [nil, nil, .blendOpacity], row3: [nil, nil, nil, .effects], edit: nil, tallRow3: false, speedArrow: false, speed: .constant)
        }
        switch l.kind {
        case 5:
            let edit: LayerEdit = l.drawing ? .drawing : l.vector ? .points : .shape
            return LayerPanelSpec(row2: [.colorFill, .borderShadow, .blendOpacity], row3: std, edit: edit, tallRow3: false, speedArrow: false, speed: .constant)
        case 4:
            return LayerPanelSpec(row2: [.colorFill, .borderShadow, .blendOpacity], row3: std, edit: .text, tallRow3: false, speedArrow: false, speed: .constant)
        case 10:
            return LayerPanelSpec(row2: [.colorFill, nil, .blendOpacity], row3: std, edit: l.text3D ? .text : .material, tallRow3: false, speedArrow: false, speed: .constant)
        // Imagem e vídeo são forma com mídia na referência (evidência REA, seção 8).
        case 2:
            return LayerPanelSpec(row2: [nil, .borderShadow, .blendOpacity], row3: std, edit: .mediaShape, tallRow3: false, speedArrow: false, speed: .constant)
        case 1:
            return LayerPanelSpec(row2: [nil, .borderShadow, .blendOpacity], row3: std, edit: .mediaShape, tallRow3: false, speedArrow: false, speed: .panel)
        case 12:
            return LayerPanelSpec(row2: [nil, .borderShadow, .blendOpacity], row3: std, edit: .group, tallRow3: false, speedArrow: true, speed: .group)
        case 11:
            return LayerPanelSpec(row2: [nil, nil, .blendOpacity], row3: std, edit: .particles, tallRow3: false, speedArrow: false, speed: .constant)
        case 3:
            return LayerPanelSpec(row2: none, row3: [.effects], edit: nil, tallRow3: true, speedArrow: false, speed: .panel)
        case 8:
            return LayerPanelSpec(row2: none, row3: [.moveTransform, .edit, .presets, nil], edit: .camera, tallRow3: true, speedArrow: false, speed: .constant)
        case 9:
            return LayerPanelSpec(row2: none, row3: [.moveTransform, .edit, nil, l.effectCount > 0 ? .effects : nil], edit: .light, tallRow3: true, speedArrow: false, speed: .constant)
        default:
            // Nulo: a vaga de edição sai da cadeia (Mover | Presets).
            return LayerPanelSpec(row2: none, row3: [.moveTransform, .presets], edit: nil, tallRow3: true, speedArrow: false, speed: .constant)
        }
    }
}

/// O botão Áudio: habilitado com áudio (`hasAnyAudio` do motor) ou com mais de um keyframe de volume.
func layerAudioEnabled(_ bits: UInt32) -> Bool { bits & (LayerPanelBits.hasAudio | LayerPanelBits.gainKeys) != 0 }

private enum LayerPopout { case none, speed, volume }


// =============================================================================
// O painel
// =============================================================================

struct LayerPanelView: View {
    @EnvironmentObject private var model: AureaModel
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @State private var popout: LayerPopout = .none
    @State private var reveal: CGFloat = 0
    @State private var enter: CGFloat = 0
    @State private var popoutLayer: Int64 = 0

    private func info(_ row: LayerItem) -> PanelLayerInfo {
        PanelLayerInfo(id: row.id, kind: row.kind, locked: row.locked, start: row.startFrame, end: row.endFrame,
                       adjustment: row.adjustment, vector: model.isVectorLayer,
                       drawing: model.isDrawingLayer,   // Omega conteúdo (área 06): a camada de desenho
                       text3D: row.kind == 10 && model.engine.text3D(forLayer: row.id) != nil, effectCount: row.effectCount)
    }

    var body: some View {
        if let row = model.selectedLayer {
            let l = info(row)
            let spec = LayerPanelSpec.of(l)
            // Lido a cada cabeçote/edição: o motor decide trio × par, retempo e Áudio.
            let bits = model.layerPanelBits(row.id)
            GeometryReader { geometry in
                let k = min(max(geometry.size.height / 200, 0.6), 2)
                ZStack(alignment: .topLeading) {
                    VStack(spacing: 0) {
                        Color.clear.frame(height: 15 * k)
                        timeRow(l, spec: spec, bits: bits, column: geometry.size.width / 5).frame(height: 38 * k)
                        Color.clear.frame(height: 10 * k)
                        if spec.tallRow3 {
                            gridRow(spec.row3, layer: l, spec: spec, tall: true).frame(height: 120 * k)
                        } else {
                            gridRow(spec.row2, layer: l, spec: spec, tall: false).frame(height: 57 * k)
                            Color.clear.frame(height: 6 * k)
                            gridRow(spec.row3, layer: l, spec: spec, tall: false).frame(height: 57 * k)
                        }
                        Spacer(minLength: 0)
                    }
                    if popout != .none && popoutLayer == row.id {
                        popoutView(l, bits: bits, head: (15 + 38) * k, column: geometry.size.width / 5)
                            .frame(width: geometry.size.width, height: geometry.size.height)
                            .mask(alignment: popout == .volume ? .topTrailing : .topLeading) {
                                Rectangle().frame(width: geometry.size.width * reveal, height: geometry.size.height * reveal)
                            }
                    }
                }
            }
            .background(AureaColors.layerPanel)
            .clipped()
            .offset(y: enter)
            .accessibilityElement(children: .contain)
            .accessibilityIdentifier("layer.panel")
            .onAppear {
                // P1-10: sobe 100 pt em 100 ms ao escolher a camada.
                if model.layerPanelLastShown != row.id && !reduceMotion {
                    enter = 100
                    withAnimation(.easeInOut(duration: 0.1)) { enter = 0 }
                }
                model.layerPanelLastShown = row.id
                warnMissingMedia(row.id, bits: bits)
            }
            .onChange(of: row.id) { id in
                popout = .none; reveal = 0; model.layerSpeedPopout = false
                if model.layerPanelLastShown != id && !reduceMotion {
                    enter = 100
                    withAnimation(.easeInOut(duration: 0.1)) { enter = 0 }
                }
                model.layerPanelLastShown = id
                warnMissingMedia(id, bits: model.layerPanelBits(id))
            }
            .onDisappear { model.layerSpeedPopout = false }
        }
    }

    // MARK: Fileira 1

    @ViewBuilder
    private func timeRow(_ l: PanelLayerInfo, spec: LayerPanelSpec, bits: UInt32, column: CGFloat) -> some View {
        let locked = bits & LayerPanelBits.locked != 0
        HStack(spacing: 0) {
            // Col. 1 · Velocidade (no grupo, com a seta ▾ do painel embutido).
            PanelPressButton(tag: "layer.panel.speed", label: AureaText.t("editor_velocidade"),
                             value: spec.speedArrow ? AureaText.t("om_lp_opens_panel") : nil) {
                if locked { model.toast = AureaText.t("editor_camada_bloqueada_desbloqueie_editar"); return }
                switch spec.speed {
                case .group: popout == .speed ? closePopout() : openPopout(.speed, layer: l.id)
                default: model.openPanel(.speed)
                }
            } icon: {
                ZStack(alignment: .bottomTrailing) {
                    CupertinoGlyph.text(CupertinoGlyph.Speedometer, size: 22).frame(maxWidth: .infinity, maxHeight: .infinity)
                    if spec.speedArrow {
                        CupertinoGlyph.text(CupertinoGlyph.ArrowtriangleDownFill, size: 8).padding(.trailing, 5).padding(.bottom, 3)
                    }
                }
            }
            .frame(width: column)
            // Cols. 2–4 · trio (cabeçote dentro) ou par Mover/Expandir (fora). LTR sempre.
            HStack(spacing: 0) {
                if bits & LayerPanelBits.extendStart != 0 {
                    timeButton(.moveStart, "om_lp_move_start_to_playhead", "layer.panel.timeLeft") {
                        pairEdit(locked) { if !model.moveLayerToPlayhead(l.id, alignEnd: false) { model.toast = AureaText.t("timeline_cut_failed") } }
                    }
                    rowDivider
                    timeButton(.extendStart, "editor_extend_start_to_playhead", "layer.panel.timeRight") {
                        pairEdit(locked) { if !model.extendToPlayhead(l.id, start: true) { model.toast = AureaText.t("timeline_cut_failed") } }
                    }
                } else if bits & LayerPanelBits.extendEnd != 0 {
                    timeButton(.extendEnd, "editor_extend_end_to_playhead", "layer.panel.timeLeft") {
                        pairEdit(locked) { if !model.extendToPlayhead(l.id, start: false) { model.toast = AureaText.t("timeline_cut_failed") } }
                    }
                    rowDivider
                    timeButton(.moveEnd, "om_lp_move_end_to_playhead", "layer.panel.timeRight") {
                        pairEdit(locked) { if !model.moveLayerToPlayhead(l.id, alignEnd: true) { model.toast = AureaText.t("timeline_cut_failed") } }
                    }
                } else {
                    timeButton(.trimStart, "editor_aparar_inicio_cabecote", "timeline.cut.start") {
                        insideEdit(l, locked) { if !model.trimStart(l.id, at: model.status.playhead) { model.toast = AureaText.t("timeline_cut_failed") } }
                    }
                    rowDivider
                    timeButton(.split, "editor_dividir_cabecote", "timeline.cut.split") {
                        insideEdit(l, locked) { model.splitAtPlayhead([l.id]) }
                    }
                    rowDivider
                    timeButton(.trimEnd, "editor_aparar_fim_cabecote", "timeline.cut.end") {
                        insideEdit(l, locked) { if !model.trimEnd(l.id, at: model.status.playhead) { model.toast = AureaText.t("timeline_cut_failed") } }
                    }
                }
            }
            .frame(maxHeight: .infinity)
            .background(AureaColors.layerPanelButton, in: RoundedRectangle(cornerRadius: 4))
            .clipShape(RoundedRectangle(cornerRadius: 4))
            .padding(.horizontal, 2)
            .keepLtr()
            .frame(width: column * 3)
            // Col. 5 · Áudio: abre o volume embutido; sem áudio fica desabilitado.
            audioButton(l, bits: bits).frame(width: column)
        }
    }

    private func audioButton(_ l: PanelLayerInfo, bits: UInt32) -> some View {
        let enabled = layerAudioEnabled(bits)
        let muted = ((model.detail["audioFlags"] as? NSNumber)?.uint32Value ?? 0) & 1 != 0
        let tint = !enabled ? AureaColors.layerPanelDisabled : muted ? AureaColors.accent : AureaColors.text
        return PanelPressButton(tag: "layer.panel.audio",
                                label: AureaText.t(enabled ? "om_lp_audio" : "panel_esta_camada_nao_tem_audio"),
                                value: muted && enabled ? AureaText.t("panel_mudo") : nil, enabled: enabled) {
            popout == .volume ? closePopout() : openPopout(.volume, layer: l.id)
        } icon: {
            ZStack(alignment: .bottom) {
                CupertinoGlyph.text(enabled && !muted ? CupertinoGlyph.Speaker2 : CupertinoGlyph.SpeakerSlash, size: 22, color: tint)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                CupertinoGlyph.text(CupertinoGlyph.ArrowtriangleDownFill, size: 7, color: tint).padding(.bottom, 3)
            }
        }
    }

    private var rowDivider: some View { AureaColors.layerPanelPressed.frame(width: 1) }

    private func timeButton(_ glyph: TimeGlyph, _ key: String, _ tag: String, action: @escaping () -> Void) -> some View {
        PanelPressButton(tag: tag, label: AureaText.t(key), action: action) {
            TimeGlyphView(glyph: glyph, color: AureaColors.text).frame(width: 22, height: 22)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }

    /// Trio: só com o cabeçote estritamente dentro.
    private func insideEdit(_ l: PanelLayerInfo, _ locked: Bool, _ action: () -> Void) {
        let t = model.status.playhead
        if locked { model.toast = AureaText.t("editor_camada_bloqueada_desbloqueie_editar"); return }
        guard t > Int64(l.start), t < Int64(l.end) else { model.toast = AureaText.t("sh_playhead_into_layer"); return }
        if model.status.playing != 0 { model.playPause() }
        action()
    }

    /// Par: mover e esticar, com o cabeçote fora do clipe.
    private func pairEdit(_ locked: Bool, _ action: () -> Void) {
        if locked { model.toast = AureaText.t("editor_camada_bloqueada_desbloqueie_editar"); return }
        if model.status.playing != 0 { model.playPause() }
        action()
    }

    // MARK: Fileiras 2 e 3

    private func gridRow(_ slots: [LayerSlot?], layer l: PanelLayerInfo, spec: LayerPanelSpec, tall: Bool) -> some View {
        HStack(spacing: 0) {
            ForEach(Array(slots.enumerated()), id: \.offset) { _, slot in
                if let slot {
                    gridButton(slot, layer: l, edit: spec.edit, tall: tall).frame(maxWidth: .infinity, maxHeight: .infinity)
                } else {
                    Color.clear.frame(maxWidth: .infinity, maxHeight: .infinity)
                }
            }
        }
    }

    private func gridButton(_ slot: LayerSlot, layer l: PanelLayerInfo, edit: LayerEdit?, tall: Bool) -> some View {
        let label = AureaText.t(Self.slotLabel(slot, edit))
        return PanelPressButton(tag: "layer.panel.\(slot.rawValue)", label: label, radius: 6) {
            onSlot(slot, layer: l, edit: edit)
        } icon: {
            VStack(spacing: 4) {
                CupertinoGlyph.text(Self.slotGlyph(slot, edit), size: 24)
                // 7 a 11,5 com redução automática (`panelbutton_autotext_*`).
                Text(label)
                    .font(.aurea(size: 11.5))
                    .minimumScaleFactor(0.6)
                    .lineLimit(2)
                    .multilineTextAlignment(.center)
                    .foregroundStyle(AureaColors.text)
                    .accessibilityHidden(true)
            }
            .padding(.horizontal, 3).padding(.vertical, tall ? 10 : 4)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .padding(.horizontal, 2)
    }

    static func slotGlyph(_ slot: LayerSlot, _ edit: LayerEdit?) -> Character {
        switch slot {
        case .colorFill: return CupertinoGlyph.Paintbrush
        case .borderShadow: return CupertinoGlyph.SquareOnSquare
        case .blendOpacity: return CupertinoGlyph.CircleLefthalfFill
        case .moveTransform: return CupertinoGlyph.Move
        case .edit: return edit?.glyph ?? CupertinoGlyph.Pencil
        case .presets: return CupertinoGlyph.WandStars
        case .effects: return CupertinoGlyph.Sparkles
        }
    }

    static func slotLabel(_ slot: LayerSlot, _ edit: LayerEdit?) -> String {
        switch slot {
        case .colorFill: return "sh_dock_color_fill"
        case .borderShadow: return "om_lp_border_shadow"
        case .blendOpacity: return "panel_mistura_opacidade"
        case .moveTransform: return "om_lp_move_transform"
        case .edit: return edit?.label ?? "sh_dock_edit_shape"
        case .presets: return "sh_dock_presets"
        case .effects: return "sh_dock_effects"
        }
    }

    /// Para onde cada botão leva HOJE (par do `onSlot` do Android). Os painéis
    /// novos das áreas 05 e 06 trocam o destino aqui quando existirem.
    private func onSlot(_ slot: LayerSlot, layer l: PanelLayerInfo, edit: LayerEdit?) {
        switch slot {
        // Omega aparência (área 05): Cor e preenchimento (forma e texto) e Borda e
        // sombra (todas as camadas visuais) têm painel próprio.
        case .colorFill:
            model.openPanel(l.kind == 10 ? .layer3D : l.vector ? .vector : .shape)
        case .borderShadow:
            model.openPanel(.borderShadow)
        case .blendOpacity: model.openPanel(.appearance)
        case .moveTransform: model.openPanel(.transform)
        case .edit:
            switch edit {
            case .text: model.openTextContentEditor()
            case .group: model.openGroup(l.id)
            case .shape: model.openPanel(.shapeEdit)
            // Omega conteúdo (área 06): Editar pontos e o editor de desenho.
            case .points: EditPointsState.shared.reset(); model.openPanel(.editPoints)
            case .drawing: model.openPanel(.drawing)
            case .mediaShape: model.openPanel(.mask)
            case .camera: model.openPanel(.camera)   // Omega aparência (P1-6): Opções da câmera
            case .light, .material: model.openPanel(.layer3D)
            case .particles: model.openPanel(.particles)
            case nil: break
            }
        case .presets: openPresets(l)
        case .effects: model.openPanel(.effects)
        }
    }

    // MARK: Presets (P1-12)

    private func openPresets(_ l: PanelLayerInfo) {
        let isText = l.kind == 4 || l.text3D
        let any = PanelPresetEntry.loadAll().contains { isText ? $0.kind == .text : $0.kind != .text }
        if any { model.openPanel(.presets); return }
        AureaPopupPresenter.shared.presentDialog { close in
            AureaDialog(title: AureaText.t("om_presets_empty_title"), message: AureaText.t("om_presets_empty_body"), actions: [
                DialogAction(AureaText.t("om_presets_empty_browse"), role: .positive, tag: "presets.empty.browse") { model.openPanel(.presets) },
                DialogAction(AureaText.t("common_close"), role: .negative, tag: "presets.empty.close"),
            ], onDismiss: close, tag: "presets.empty", content: AnyView(
                CupertinoGlyph.text(CupertinoGlyph.WandStars, size: 64, color: AureaColors.lightAccentInk)
                    .frame(maxWidth: .infinity).padding(.vertical, 8)
            ))
        }
    }

    // MARK: P2-16 · mídia ausente, uma vez por camada

    private func warnMissingMedia(_ id: Int64, bits: UInt32) {
        guard bits & LayerPanelBits.missingMedia != 0, !model.layerPanelWarnedMissing.contains(id) else { return }
        model.layerPanelWarnedMissing.insert(id)
        AureaPopupPresenter.shared.presentDialog { close in
            AureaAlert(title: AureaText.t("om_lp_missing_media_title"), message: AureaText.t("om_lp_missing_media"),
                       confirmLabel: AureaText.t("common_ok"), showCancel: false, onConfirm: {}, onDismiss: close)
        }
    }

    // MARK: Painéis embutidos (200 ms, do canto de cima)

    private func openPopout(_ p: LayerPopout, layer: Int64) {
        if model.status.playing != 0 { model.playPause() }
        popoutLayer = layer
        popout = p
        model.layerSpeedPopout = p == .speed
        reveal = 0
        withAnimation(.easeInOut(duration: reduceMotion ? 0 : 0.2)) { reveal = 1 }
    }

    private func closePopout() {
        withAnimation(.easeInOut(duration: reduceMotion ? 0 : 0.2)) { reveal = 0 }
        DispatchQueue.main.asyncAfter(deadline: .now() + (reduceMotion ? 0 : 0.2)) {
            if reveal == 0 { popout = .none; model.layerSpeedPopout = false }
        }
    }

    @ViewBuilder
    private func popoutView(_ l: PanelLayerInfo, bits: UInt32, head: CGFloat, column: CGFloat) -> some View {
        switch popout {
        case .speed:
            VStack(spacing: 0) {
                HStack(spacing: 0) {
                    // A "aba": a Velocidade ATIVA (destaque) com o cue ▴; tocar fecha.
                    PanelPressButton(tag: "layer.panel.groupSpeed.close", label: AureaText.t("om_lp_speed_close"), action: closePopout) {
                        ZStack(alignment: .bottomTrailing) {
                            CupertinoGlyph.text(CupertinoGlyph.Speedometer, size: 22, color: AureaColors.accent).frame(maxWidth: .infinity, maxHeight: .infinity)
                            CupertinoGlyph.text(CupertinoGlyph.ArrowtriangleDownFill, size: 8, color: AureaColors.accent)
                                .rotationEffect(.degrees(180)).padding(.trailing, 5).padding(.bottom, 3)
                        }
                    }
                    .frame(width: max(0, column - 4))
                    Spacer(minLength: 0)
                }
                .frame(height: max(0, head - 4))
                GroupSpeedBody(layerId: l.id, bits: bits)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .background(AureaColors.layerPanelBody,
                                in: UnevenRoundedRectangleCompat(bottom: 4))
            }
            .background(AureaColors.layerPanelButton, in: RoundedRectangle(cornerRadius: 4))
            .padding(.horizontal, 4).padding(.top, 4).padding(.bottom, 6)
            .accessibilityElement(children: .contain)
            .accessibilityIdentifier("layer.panel.groupSpeed")
        case .volume:
            VStack(spacing: 0) {
                HStack(spacing: 0) {
                    Spacer(minLength: 0)
                    // Cabeça transparente: só o Áudio PRESSIONADO na coluna 5; tocar fecha.
                    PanelPressButton(tag: "layer.panel.volume.close", label: AureaText.t("om_lp_volume_close"), action: closePopout) {
                        CupertinoGlyph.text(CupertinoGlyph.Speaker2, size: 22, color: AureaColors.accent)
                            .frame(maxWidth: .infinity, maxHeight: .infinity)
                    }
                    .frame(width: column)
                    .padding(.top, max(0, head - 38))
                    .background(AureaColors.layerPanelBody.padding(.top, max(0, head - 38)))
                }
                .frame(height: head)
                VolumeFrame(onBack: closePopout, tagPrefix: "layer.panel.volume")
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .background(AureaColors.layerPanelBody)
            }
            .accessibilityElement(children: .contain)
            .accessibilityIdentifier("layer.panel.volume")
        case .none:
            EmptyView()
        }
    }
}

/// P1-10: o painel de propriedade entra por cima do painel da camada — x +100 → 0
/// em 100 ms com fade de 25 ms (par do `PanelContent` do Android). A curva e o
/// próprio painel da camada não animam; "reduzir movimento" deixa só o fade.
struct PanelEntrance: ViewModifier {
    let panel: AureaModel.PanelKind
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.layoutDirection) private var direction
    @State private var offset: CGFloat = 0
    @State private var alpha: Double = 1

    func body(content: Content) -> some View {
        content
            .offset(x: offset)
            .opacity(alpha)
            .clipped()
            .onAppear { animate() }
            .onChange(of: panel) { _ in animate() }
    }

    private func animate() {
        guard panel != .none, panel != .dock, panel != .curve else { offset = 0; alpha = 1; return }
        offset = reduceMotion ? 0 : (direction == .rightToLeft ? -100 : 100)
        alpha = 0
        withAnimation(.linear(duration: 0.025)) { alpha = 1 }
        withAnimation(.easeInOut(duration: 0.1)) { offset = 0 }
    }
}

/// Cantos de baixo arredondados (o corpo do painel embutido), compatível com iOS 16.
struct UnevenRoundedRectangleCompat: Shape {
    var bottom: CGFloat
    func path(in rect: CGRect) -> Path {
        let r = min(bottom, rect.width / 2, rect.height / 2)
        var p = Path()
        p.move(to: CGPoint(x: rect.minX, y: rect.minY))
        p.addLine(to: CGPoint(x: rect.maxX, y: rect.minY))
        p.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY - r))
        p.addQuadCurve(to: CGPoint(x: rect.maxX - r, y: rect.maxY), control: CGPoint(x: rect.maxX, y: rect.maxY))
        p.addLine(to: CGPoint(x: rect.minX + r, y: rect.maxY))
        p.addQuadCurve(to: CGPoint(x: rect.minX, y: rect.maxY - r), control: CGPoint(x: rect.minX, y: rect.maxY))
        p.closeSubpath()
        return p
    }
}

/// Botão do painel: sem fundo; pressionado = `layerPanelPressed` (A11).
struct PanelPressButton<Icon: View>: View {
    let tag: String
    let label: String
    var value: String? = nil
    var enabled: Bool = true
    var radius: CGFloat = 0
    let action: () -> Void
    let content: () -> Icon

    init(tag: String, label: String, value: String? = nil, enabled: Bool = true, radius: CGFloat = 0,
         action: @escaping () -> Void, @ViewBuilder icon content: @escaping () -> Icon) {
        self.tag = tag; self.label = label; self.value = value; self.enabled = enabled; self.radius = radius
        self.action = action; self.content = content
    }

    var body: some View {
        Button(action: action) {
            content().frame(maxWidth: .infinity, maxHeight: .infinity).contentShape(Rectangle())
        }
        .buttonStyle(PanelPressStyle(radius: radius))
        .disabled(!enabled)
        .opacity(enabled ? 1 : 0.6)
        .accessibilityLabel(label)
        .accessibilityValue(value ?? "")
        .accessibilityIdentifier(tag)
    }
}

private struct PanelPressStyle: ButtonStyle {
    var radius: CGFloat
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background(configuration.isPressed ? AureaColors.layerPanelPressed : Color.clear,
                        in: RoundedRectangle(cornerRadius: radius))
    }
}

/// Os desenhos da fileira de tempo (par do `TimeGlyph` do Kotlin).
enum TimeGlyph { case trimStart, split, trimEnd, moveStart, extendStart, extendEnd, moveEnd }

struct TimeGlyphView: View {
    let glyph: TimeGlyph
    let color: Color
    var body: some View {
        Canvas { context, size in
            let u = size.width / 24
            let stroke = StrokeStyle(lineWidth: 1.8 * u, lineCap: .round, lineJoin: .round)
            let dashed = StrokeStyle(lineWidth: 1.8 * u, lineCap: .round, lineJoin: .round, dash: [2.2 * u, 2.0 * u])
            func line(_ a: CGPoint, _ b: CGPoint, _ style: StrokeStyle) {
                var p = Path(); p.move(to: a); p.addLine(to: b); context.stroke(p, with: .color(color), style: style)
            }
            func bracket(_ outer: CGFloat, _ inner: CGFloat, dash: Bool) {
                var p = Path()
                p.move(to: CGPoint(x: outer * u, y: 6 * u)); p.addLine(to: CGPoint(x: inner * u, y: 6 * u))
                p.addLine(to: CGPoint(x: inner * u, y: 18 * u)); p.addLine(to: CGPoint(x: outer * u, y: 18 * u))
                context.stroke(p, with: .color(color), style: dash ? dashed : stroke)
            }
            func playhead(_ x: CGFloat) { line(CGPoint(x: x * u, y: 3.5 * u), CGPoint(x: x * u, y: 20.5 * u), stroke) }
            func clip(_ l: CGFloat, _ r: CGFloat) {
                context.stroke(Path(roundedRect: CGRect(x: l * u, y: 8 * u, width: (r - l) * u, height: 8 * u), cornerRadius: 1.5 * u),
                               with: .color(color), style: stroke)
            }
            func arrow(_ from: CGFloat, _ to: CGFloat) {
                line(CGPoint(x: from * u, y: 12 * u), CGPoint(x: to * u, y: 12 * u), stroke)
                let d: CGFloat = to < from ? 2.5 : -2.5
                line(CGPoint(x: to * u, y: 12 * u), CGPoint(x: (to + d) * u, y: 9.5 * u), stroke)
                line(CGPoint(x: to * u, y: 12 * u), CGPoint(x: (to + d) * u, y: 14.5 * u), stroke)
            }
            switch glyph {
            case .trimStart, .split, .trimEnd:
                bracket(3, 9, dash: glyph == .trimStart)
                bracket(21, 15, dash: glyph == .trimEnd)
                playhead(12)
            case .moveStart:
                playhead(3); clip(13, 21); arrow(11, 5.5)
            case .extendStart:
                playhead(3); clip(13, 21)
                line(CGPoint(x: 5.5 * u, y: 8 * u), CGPoint(x: 13 * u, y: 8 * u), dashed)
                line(CGPoint(x: 5.5 * u, y: 16 * u), CGPoint(x: 13 * u, y: 16 * u), dashed)
            case .extendEnd:
                playhead(21); clip(3, 11)
                line(CGPoint(x: 11 * u, y: 8 * u), CGPoint(x: 18.5 * u, y: 8 * u), dashed)
                line(CGPoint(x: 11 * u, y: 16 * u), CGPoint(x: 18.5 * u, y: 16 * u), dashed)
            case .moveEnd:
                playhead(21); clip(3, 11); arrow(13, 18.5)
            }
        }
        .accessibilityHidden(true)
    }
}

// =============================================================================
// Velocidade na moldura comum (P1-7; par do `SpeedPanel` do Kotlin)
// =============================================================================

struct SpeedPanel: View {
    @EnvironmentObject private var model: AureaModel
    @State private var tab = "speed"
    private var id: Int64 { model.primarySelection ?? 0 }
    private var kind: UInt32 { model.selectedLayer?.kind ?? 0 }
    private var flags: UInt32 { (model.detail["timeFlags"] as? NSNumber)?.uint32Value ?? 0 }
    private var media: Bool { kind == 1 || kind == 3 }
    private var keyed: Bool { media || kind == 12 }
    private var remap: Bool { flags & 4 != 0 }
    private var animated: Bool { flags & 64 != 0 }
    private var speedKeys: [KeyframeItem] { (model.keyframes[id] ?? []).filter { $0.property == 39 && $0.effectIndex == .max } }
    private var look: KeyframeLook {
        guard keyed else { return .none }
        return speedKeys.contains { $0.time == model.localPlayhead } ? .keyHere : animated ? .animated : .none
    }

    var body: some View {
        let canKey = keyed && !remap
        let rail = PanelRail(
            onBack: { model.panel = .none },
            keyframeLook: look,
            onKeyframe: canKey ? { toggleSpeedKey() } : nil,
            onDeleteAllKeyframes: { model.deleteAllKeyframes(id, property: 39) },
            onCurve: canKey ? { openSpeedCurve() } : nil,
            curveReady: speedKeys.count >= 2)
        let tabs: [PanelTab] = media ? [
            PanelTab(id: "speed", label: AureaText.t("editor_velocidade"), glyph: CupertinoGlyph.Speedometer),
            PanelTab(id: "curve", label: AureaText.t("om_lp_speed_tab_curve"), glyph: CupertinoGlyph.Waveform),
            PanelTab(id: "options", label: AureaText.t("om_lp_speed_tab_options"), glyph: CupertinoGlyph.SliderHorizontal3),
        ] : []
        PanelFrame(rail: rail, tabs: tabs, selectedTab: tab, onTab: { tab = $0 }) {
            switch media ? tab : "speed" {
            case "curve": curveTab
            case "options": optionsTab
            default: mainTab
            }
        }
        .foregroundStyle(AureaColors.text)
    }

    private func toggleSpeedKey() {
        let speed = (model.detail["speed"] as? NSNumber)?.floatValue ?? 1
        let here = speedKeys.contains { $0.time == model.localPlayhead }
        model.mutate { core in
            if here { core.deleteKeyframe(forLayer: id, property: 39, time: model.localPlayhead) }
            else { core.insertKeyframe(forLayer: id, property: 39, time: model.localPlayhead, value: speed.clamped(to: 0.05...16)) }
        }
        model.refreshModel(force: true)
    }

    private func openSpeedCurve() {
        let keys = speedKeys.sorted { $0.time < $1.time }
        guard keys.count >= 2 else { return }
        let local = model.localPlayhead
        let key = keys.last(where: { $0.time <= local && $0 != keys.last }) ?? (local >= keys.last!.time ? keys[keys.count - 2] : keys[0])
        model.openCurve(property: 39, time: key.time)
    }

    private var mainTab: some View {
        let speed = (model.detail["speed"] as? NSNumber)?.floatValue ?? 1
        let bits = model.layerPanelBits(id)
        let frozen = keyed && speed == 0 && !animated
        let notBakeable = !keyed && bits & LayerPanelBits.speedBakeable == 0
        let blocked = remap || frozen || notBakeable
        let seconds = Int((Double(model.selectedLayer?.duration ?? 0) / max(1, model.compositionFps)).rounded())
        let value = SpeedText.label(speed) + " · " + String(format: "%d:%02d", seconds / 60, seconds % 60)
        return VStack(alignment: .leading, spacing: 0) {
            Color.clear.frame(height: 10)
            RetimeRow(layerId: id, bits: bits)
            Color.clear.frame(height: 8)
            Group {
                if remap { hint("om_lp_speed_remap_on") }
                else if frozen { hint("panel_este_trecho_quadro_parado_apare_bordas") }
                else if notBakeable { hint("om_lp_speed_keyed") }
                else {
                    Button { openKeypad(speed) } label: {
                        Text(value).font(.aurea(size: 12)).foregroundStyle(AureaColors.text)
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel(AureaText.t("editor_velocidade") + " " + value)
                    .accessibilityIdentifier("speed.value")
                }
            }
            Color.clear.frame(height: 6)
            SpeedSlider(value: speed, enabled: !blocked, height: 40, tag: "speed.slider",
                        onStart: { model.beginGesture("velocidade") }, onValue: { setSpeed($0) }, onEnd: { model.endGesture() })
            if !keyed { hint("om_lp_speed_constant_hint").padding(.top, 4) }
            Color.clear.frame(height: 10)
        }
        .padding(.horizontal, 15)
    }

    private var curveTab: some View {
        VStack(alignment: .leading, spacing: 8) {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 8) {
                    SpeedChip(label: AureaText.t("remap_normal"), on: !remap) { change { $0.setTimeRemap(false, forLayer: id) } }
                    SpeedChip(label: AureaText.t("remap_linha_tempo"), on: remap) { change { $0.setTimeRemap(true, forLayer: id) } }
                }
            }
            if remap { TimeRemapEffectEditor(effectId: .max) }
            else { hint("panel_velocidade_keyframes_duracao_fica") }
        }
        .padding(.horizontal, 12).padding(.vertical, 8)
    }

    private var optionsTab: some View {
        let speed = (model.detail["speed"] as? NSNumber)?.floatValue ?? 1
        let blend = flags & 16 != 0 ? 2 : flags & 8 != 0 ? 1 : 0
        return VStack(alignment: .leading, spacing: 0) {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    ForEach(Array([Float(0.25), 0.5, 1, 2, 4].enumerated()), id: \.offset) { index, value in
                        SpeedChip(label: ["0,25x", "0,5x", "1x", "2x", "4x"][index], on: abs(speed - value) < 0.005) { setSpeed(value) }
                    }
                }
            }
            if kind == 1 {
                HStack {
                    Text(AureaText.t("panel_passar_tras_frente")).font(.aurea(size: 13)).frame(maxWidth: .infinity, alignment: .leading)
                    AureaToggle(checked: flags & 1 != 0) { v in change { $0.setLayer(id, reversed: v) } }
                }.frame(height: 48)
                Text(AureaText.t("panel_quadros_camera_lenta")).font(.aurea(size: 13))
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 6) {
                        ForEach(Array(["panel_repetir_quadro", "panel_misturar", "panel_movimento_suave"].enumerated()), id: \.offset) { index, key in
                            SpeedChip(label: AureaText.t(key), on: blend == index) { change { $0.setFrameBlendForLayer(id, mode: UInt32(index)) } }
                        }
                    }
                }.padding(.top, 6)
            }
        }
        .padding(.horizontal, 12).padding(.vertical, 8)
    }

    private func change(_ action: (AureaEngine) -> Void) { model.mutate(action); model.refreshModel(force: true) }

    /// Mídia e grupo: velocidade do motor (com keyframe, grava no cabeçote); sem fonte, a constante reescalada.
    private func setSpeed(_ value: Float) {
        let v = value.clamped(to: 0.05...16)
        if !keyed { model.setLayerSpeedBaked(v); return }
        if animated { change { $0.insertKeyframe(forLayer: id, property: 39, time: model.localPlayhead, value: v) } }
        else { change { $0.setLayer(id, speed: v) } }
    }

    private func openKeypad(_ speed: Float) {
        model.numericKeypad = KeypadRequest(title: AureaText.t("editor_velocidade"), value: speed, unit: "x", min: 0.05, max: 16, decimals: 2) { v in
            model.beginGesture("velocidade"); setSpeed(v); model.endGesture()
        }
    }

    private func hint(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).fixedSize(horizontal: false, vertical: true)
    }
}

enum SpeedText {
    static func label(_ value: Float) -> String { String(format: "%.2f", value).replacingOccurrences(of: ".", with: ",") + "x" }
}

struct SpeedChip: View {
    let label: String
    let on: Bool
    let action: () -> Void
    var body: some View {
        Button(action: action) {
            Text(label).font(.aurea(size: 12)).foregroundStyle(on ? AureaColors.accent : AureaColors.text)
                .padding(.horizontal, 12).frame(minHeight: 44)
                .background(on ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
        }
        .buttonStyle(.plain)
        .accessibilityAddTraits(on ? .isSelected : [])
    }
}

/// Os quatro botões de retempo (37 pt, S9; alfa 0,35 desabilitado). Estados do motor.
struct RetimeRow: View {
    @EnvironmentObject private var model: AureaModel
    let layerId: Int64
    let bits: UInt32

    var body: some View {
        HStack(spacing: 1.5) {
            button(0, "om_lp_retime_extend_start", "speed.retime.extendStart", LayerPanelBits.retimeExtendStart)
            button(1, "om_lp_retime_shorten_start", "speed.retime.shortenStart", LayerPanelBits.retimeShortenStart)
            button(2, "om_lp_retime_shorten_end", "speed.retime.shortenEnd", LayerPanelBits.retimeShortenEnd)
            button(3, "om_lp_retime_extend_end", "speed.retime.extendEnd", LayerPanelBits.retimeExtendEnd)
        }
        .frame(height: 37)
        .clipShape(RoundedRectangle(cornerRadius: 4))
        .keepLtr()
    }

    private func button(_ edge: UInt32, _ key: String, _ tag: String, _ bit: UInt32) -> some View {
        let enabled = bits & bit != 0
        return Button {
            if bits & LayerPanelBits.locked != 0 { model.toast = AureaText.t("editor_camada_bloqueada_desbloqueie_editar"); return }
            if model.status.playing != 0 { model.playPause() }
            if !model.retimeLayerToPlayhead(layerId, edge: edge) { model.toast = AureaText.t("timeline_cut_failed") }
        } label: {
            RetimeGlyphView(edge: edge, color: AureaColors.text).frame(width: 22, height: 22)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(AureaColors.layerPanelRetime)
                .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .disabled(!enabled)
        .opacity(enabled ? 1 : 0.35)
        .accessibilityLabel(AureaText.t(key))
        .accessibilityIdentifier(tag)
    }
}

/// O clipe com "quadros" dentro e a seta na ponta (par do `RetimeGlyph` do Kotlin).
private struct RetimeGlyphView: View {
    let edge: UInt32
    let color: Color
    var body: some View {
        Canvas { context, size in
            let u = size.width / 24
            let style = StrokeStyle(lineWidth: 1.6 * u, lineCap: .round)
            let start = edge <= 1
            let l: CGFloat = start ? 9 : 3
            let r: CGFloat = start ? 21 : 15
            context.stroke(Path(roundedRect: CGRect(x: l * u, y: 7 * u, width: (r - l) * u, height: 10 * u), cornerRadius: 1.5 * u),
                           with: .color(color), style: style)
            for i in 1...3 {
                let x = l + (r - l) * CGFloat(i) / 4
                var p = Path(); p.move(to: CGPoint(x: x * u, y: 9.5 * u)); p.addLine(to: CGPoint(x: x * u, y: 14.5 * u))
                context.stroke(p, with: .color(color.opacity(0.55)), lineWidth: u)
            }
            let (from, to): (CGFloat, CGFloat) = edge == 0 ? (8, 2) : edge == 1 ? (2, 8) : edge == 2 ? (22, 16) : (16, 22)
            var a = Path(); a.move(to: CGPoint(x: from * u, y: 12 * u)); a.addLine(to: CGPoint(x: to * u, y: 12 * u))
            let d: CGFloat = to < from ? 2.5 : -2.5
            a.move(to: CGPoint(x: to * u, y: 12 * u)); a.addLine(to: CGPoint(x: (to + d) * u, y: 9.5 * u))
            a.move(to: CGPoint(x: to * u, y: 12 * u)); a.addLine(to: CGPoint(x: (to + d) * u, y: 14.5 * u))
            context.stroke(a, with: .color(color), style: style)
        }
        .accessibilityHidden(true)
    }
}

/// Encosta em 0,25 · 0,5 · 0,75 · 1 · 2 · 3 e anda de 0,05 em 0,05 (par do `snapLinearSpeed`).
func snapLinearSpeed(_ v: Float) -> Float {
    for s: Float in [0.25, 0.5, 0.75, 1, 2, 3] where abs(v - s) < 0.06 { return s }
    return ((v / 0.05).rounded() * 0.05).clamped(to: 0.05...4)
}

/// A RÉGUA DA VELOCIDADE (`AlightSlider` da referência; par do `SpeedSlider`):
/// linear de 0,05 a 4×, origem em 1×, tartaruga e lebre a 50 % nas pontas.
struct SpeedSlider: View {
    let value: Float
    let enabled: Bool
    let height: CGFloat
    let tag: String
    let onStart: () -> Void
    let onValue: (Float) -> Void
    let onEnd: () -> Void
    @State private var dragging = false
    @State private var last: Float = -1
    let lo: Float = 0.05
    let hi: Float = 4

    var body: some View {
        HStack(spacing: 0) {
            CupertinoGlyph.text(CupertinoGlyph.Tortoise, size: 20, color: AureaColors.text.opacity(0.5))
                .frame(width: 40, height: 40).accessibilityLabel(AureaText.t("om_lp_speed_slower"))
            GeometryReader { g in
                let pad: CGFloat = 8
                let w = max(1, g.size.width - pad * 2)
                let x = { (v: Float) -> CGFloat in pad + w * CGFloat((v.clamped(to: lo...hi) - lo) / (hi - lo)) }
                ZStack(alignment: .topLeading) {
                    RoundedRectangle(cornerRadius: 2).fill(AureaColors.layerPanelValueBox)
                        .frame(width: w, height: 4).position(x: pad + w / 2, y: g.size.height / 2)
                    ForEach([Float(0.5), 1, 2, 3, 4], id: \.self) { t in
                        Rectangle().fill(AureaColors.text.opacity(0.35)).frame(width: 1, height: 4)
                            .position(x: x(t), y: g.size.height / 2 + 7)
                    }
                    let o = x(1), vx = x(value)
                    Capsule().fill(AureaColors.accent).frame(width: abs(vx - o) + 4, height: 4)
                        .position(x: (o + vx) / 2, y: g.size.height / 2)
                    Circle().fill(AureaColors.text).frame(width: 16, height: 16).position(x: vx, y: g.size.height / 2)
                }
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0).onChanged { drag in
                    guard enabled else { return }
                    if !dragging { dragging = true; last = -1; onStart() }
                    let v = snapLinearSpeed(lo + Float(((drag.location.x - pad) / w).clamped(to: 0...1)) * (hi - lo))
                    if v != last { last = v; onValue(v) }
                }.onEnded { _ in
                    guard dragging else { return }
                    dragging = false; onEnd()
                })
            }
            .accessibilityElement()
            .accessibilityLabel(AureaText.t("editor_velocidade"))
            .accessibilityValue(SpeedText.label(value))
            .accessibilityAdjustableAction { direction in
                guard enabled else { return }
                let step: Float = direction == .increment ? 0.05 : -0.05
                onStart(); onValue(snapLinearSpeed(value + step)); onEnd()
            }
            .accessibilityIdentifier(tag)
            CupertinoGlyph.text(CupertinoGlyph.Hare, size: 20, color: AureaColors.text.opacity(0.5))
                .frame(width: 40, height: 40).accessibilityLabel(AureaText.t("om_lp_speed_faster"))
        }
        .frame(height: height)
        .opacity(enabled ? 1 : 0.35)
        .keepLtr()
        .onDisappear { if dragging { dragging = false; onEnd() } }
    }
}

/// Caixa de valor dos painéis embutidos (56 × 36, branco 5 %); o toque abre o teclado.
struct PopoutValueBox: View {
    let text: String
    let tag: String
    let label: String
    let onTap: () -> Void
    var body: some View {
        Button(action: onTap) {
            Text(text).font(.aurea(size: 12)).foregroundStyle(AureaColors.text).lineLimit(1)
                .frame(width: 56, height: 36)
                .background(AureaColors.layerPanelValueBox, in: RoundedRectangle(cornerRadius: 4))
        }
        .buttonStyle(AureaPressStyle())
        .accessibilityLabel(label + " " + text)
        .accessibilityIdentifier(tag)
    }
}

/// A VELOCIDADE EMBUTIDA DO GRUPO (corpo do `panelSpeedControl`; par do `GroupSpeedBody`).
struct GroupSpeedBody: View {
    @EnvironmentObject private var model: AureaModel
    let layerId: Int64
    let bits: UInt32
    var body: some View {
        let speed = (model.detail["speed"] as? NSNumber)?.floatValue ?? 1
        VStack(spacing: 0) {
            Color.clear.frame(height: 10)
            RetimeRow(layerId: layerId, bits: bits)
            Spacer(minLength: 0)
            HStack(spacing: 6) {
                SpeedSlider(value: speed, enabled: bits & LayerPanelBits.locked == 0, height: 30, tag: "layer.panel.groupSpeed.slider",
                            onStart: { model.beginGesture("velocidade") },
                            onValue: { v in model.mutate { $0.setLayer(layerId, speed: v) }; model.refreshModel(force: true) },
                            onEnd: { model.endGesture() })
                PopoutValueBox(text: String(format: "%.2fx", speed), tag: "layer.panel.groupSpeed.value", label: AureaText.t("editor_velocidade")) {
                    model.numericKeypad = KeypadRequest(title: AureaText.t("editor_velocidade"), value: speed, unit: "x", min: 0.05, max: 16, decimals: 2) { v in
                        model.beginGesture("velocidade"); model.mutate { $0.setLayer(layerId, speed: v) }; model.endGesture()
                    }
                }
            }
            Color.clear.frame(height: 8)
        }
        .padding(.horizontal, 15)
    }
}

// =============================================================================
// Som na moldura comum (P1-8; par do `AudioPanel`/`VolumeFrame` do Kotlin)
// =============================================================================

struct AudioPanel: View {
    @EnvironmentObject private var model: AureaModel
    var body: some View { VolumeFrame(onBack: { model.panel = .none }, tagPrefix: "volume") }
}

struct VolumeFrame: View {
    @EnvironmentObject private var model: AureaModel
    let onBack: () -> Void
    let tagPrefix: String
    @State private var tab = "volume"

    private var id: Int64 { model.primarySelection ?? 0 }
    private func scalar(_ key: String, _ fallback: Float = 0) -> Float { (model.detail[key] as? NSNumber)?.floatValue ?? fallback }
    private var flags: UInt32 { (model.detail["audioFlags"] as? NSNumber)?.uint32Value ?? 0 }
    private var fps: Float { Float(max(1, model.compositionFps)) }
    private var volumeKeys: [KeyframeItem] { (model.keyframes[id] ?? []).filter { $0.property == 32 && $0.effectIndex == .max && $0.paramIndex == 0 } }
    private var keyLook: KeyframeLook { volumeKeys.contains { $0.time == model.localPlayhead } ? .keyHere : flags & 8 != 0 ? .animated : .none }
    private var expressionLook: ExpressionLook {
        let info = model.engine.expression(id, property: 32, effect: .max, param: 0)
        guard (info["exists"] as? NSNumber)?.boolValue == true else { return .none }
        if (info["enabled"] as? NSNumber)?.boolValue == false { return .off }
        return (info["error"] as? String ?? "").isEmpty ? .ok : .error
    }

    var body: some View {
        // `hasAnyAudio` do motor (todo vídeo, áudio, grupo com som) + o som real do vídeo.
        let hasAudio = flags & 4 != 0 || layerAudioEnabled(model.layerPanelBits(id))
        let onVolume = hasAudio && tab == "volume"
        let rail = PanelRail(
            onBack: onBack,
            keyframeLook: onVolume ? keyLook : .none,
            onKeyframe: onVolume ? { toggleVolumeKey() } : nil,
            onDeleteAllKeyframes: { model.deleteAllKeyframes(id, property: 32) },
            onCurve: onVolume ? { openVolumeCurve() } : nil,
            curveReady: volumeKeys.count >= 2,
            expression: onVolume ? expressionLook : .none,
            onExpression: onVolume ? {
                model.expressionSheet = ExpressionRequest(layer: id, label: AureaText.t("panel_volume"), tracks: [ExpressionTrack(property: 32)], scale: 100, unit: "%")
            } : nil)
        let tabs: [PanelTab] = hasAudio ? [
            PanelTab(id: "volume", label: AureaText.t("panel_volume"), glyph: CupertinoGlyph.Speaker2),
            PanelTab(id: "boost", label: AureaText.t("panel_reforco"), glyph: CupertinoGlyph.Bolt),
            PanelTab(id: "pan", label: AureaText.t("panel_esquerda_direita"), glyph: CupertinoGlyph.ArrowLeftRight),
            PanelTab(id: "fades", label: AureaText.t("om_lp_volume_tab_fades"), glyph: CupertinoGlyph.Waveform),
        ] : []
        return PanelFrame(rail: rail, tabs: tabs, selectedTab: tab, onTab: { tab = $0 }) {
            if !hasAudio {
                // Sem áudio: só o aviso, centralizado, e o ◆ e a Curva desabilitados.
                Text(AureaText.t("panel_esta_camada_nao_tem_audio"))
                    .font(.aurea(size: 15)).foregroundStyle(AureaColors.layerPanelDisabled)
                    .multilineTextAlignment(.center)
                    .frame(maxWidth: .infinity, minHeight: 150).padding(.horizontal, 12)
            } else {
                content.padding(.horizontal, 12).padding(.vertical, 8)
            }
        }
        .foregroundStyle(AureaColors.text)
        .accessibilityIdentifier("\(tagPrefix).frame")
    }

    @ViewBuilder private var content: some View {
        switch tab {
        case "boost":
            let gain = scalar("audioGain", 1)
            CompactRuler(label: AureaText.t("panel_reforco"), tag: "\(tagPrefix).boost",
                         value: gain <= 0 ? -24 : (20 * log10(gain)).clamped(to: -24...12), text: Self.decibel(gain),
                         units: 0.1, min: -24, max: 12, unit: "dB", decimals: 1) { v in
                change { $0.setLayer(id, audioGain: v <= -24 ? 0 : pow(10, v / 20)) }
            }
        case "pan":
            let pan = Int((scalar("audioPan") * 100).rounded())
            CompactRuler(label: AureaText.t("panel_esquerda_direita"), tag: "\(tagPrefix).pan", value: scalar("audioPan") * 100,
                         text: pan == 0 ? AureaText.t("panel_centro") : pan < 0 ? AureaText.t("ios_pan_left_n", -pan) : AureaText.t("ios_pan_right_n", pan),
                         units: 0.5, min: -100, max: 100, unit: "", decimals: 0) { v in change { $0.setLayer(id, audioPan: v / 100) } }
        case "fades":
            let maxFade = max(0, Float(model.selectedLayer?.duration ?? 0) / fps / 2)
            VStack(alignment: .leading, spacing: 4) {
                CompactRuler(label: AureaText.t("panel_entrada_suave"), tag: "\(tagPrefix).fadeIn", value: scalar("audioFadeIn") / fps,
                             text: Self.seconds(scalar("audioFadeIn") / fps), units: 0.02, min: 0, max: maxFade, unit: "s", decimals: 2, showLabel: true) { v in
                    change { $0.setLayer(id, fadeIn: Int32((v * fps).rounded())) }
                }
                CompactRuler(label: AureaText.t("panel_saida_suave"), tag: "\(tagPrefix).fadeOut", value: scalar("audioFadeOut") / fps,
                             text: Self.seconds(scalar("audioFadeOut") / fps), units: 0.02, min: 0, max: maxFade, unit: "s", decimals: 2, showLabel: true) { v in
                    change { $0.setLayer(id, fadeOut: Int32((v * fps).rounded())) }
                }
            }
        default:
            // Volume de 0 a 200 % (a faixa do AUREA; a referência vai a 150 %).
            let muted = flags & 1 != 0
            VStack(spacing: 0) {
                HStack(spacing: 6) {
                    CupertinoGlyph.text(muted ? CupertinoGlyph.SpeakerSlash : CupertinoGlyph.Speaker2, size: 18,
                                        color: muted ? AureaColors.accent : AureaColors.muted)
                    CompactRuler(label: AureaText.t("panel_volume"), tag: "\(tagPrefix).slider", value: scalar("audioVolume", 1) * 100,
                                 text: "\(Int((scalar("audioVolume", 1) * 100).rounded()))%", units: 0.5, min: 0, max: 200, unit: "%", decimals: 0,
                                 valueTag: "\(tagPrefix).value") { v in
                        change { core in
                            if flags & 8 != 0 { core.insertKeyframe(forLayer: id, property: 32, time: model.localPlayhead, value: v / 100) }
                            else { core.setLayer(id, audioVolume: v / 100) }
                        }
                    }
                }
                HStack(spacing: 8) {
                    Text(AureaText.t("panel_mudo")).font(.aurea(size: 13)).frame(maxWidth: .infinity, alignment: .leading)
                    AureaToggle(checked: muted) { v in change { $0.setLayer(id, audioMuted: v) } }
                        .accessibilityIdentifier("\(tagPrefix).mute")
                    Color.clear.frame(width: 8)
                    Text(AureaText.t("panel_solo")).font(.aurea(size: 13)).frame(maxWidth: .infinity, alignment: .leading)
                    AureaToggle(checked: flags & 2 != 0) { v in change { $0.setLayer(id, audioSolo: v) } }
                        .accessibilityIdentifier("\(tagPrefix).solo")
                }
                .frame(height: 44)
            }
        }
    }

    private func change(_ action: (AureaEngine) -> Void) { model.mutate(action); model.refreshModel(force: true) }

    private func toggleVolumeKey() {
        let here = keyLook == .keyHere
        change { core in
            if here { core.deleteKeyframe(forLayer: id, property: 32, time: model.localPlayhead) }
            else { core.insertKeyframe(forLayer: id, property: 32, time: model.localPlayhead, value: scalar("audioVolume", 1)) }
        }
    }

    private func openVolumeCurve() {
        let keys = volumeKeys.sorted { $0.time < $1.time }
        guard keys.count >= 2 else { return }
        let local = model.localPlayhead
        let key = keys.last(where: { $0.time <= local && $0 != keys.last }) ?? (local >= keys.last!.time ? keys[keys.count - 2] : keys[0])
        model.openCurve(property: 32, time: key.time)
    }

    static func decibel(_ value: Float) -> String {
        guard value > 0 else { return "−∞ dB" }
        let v = (20 * log10(value) * 10).rounded() / 10
        return (v > 0 ? "+" : v < 0 ? "−" : "") + String(format: "%.1f", abs(v)).replacingOccurrences(of: ".", with: ",") + " dB"
    }

    static func seconds(_ value: Float) -> String { String(format: "%.2f s", value).replacingOccurrences(of: ".", with: ",") }
}

/// Régua compacta (40 pt) + caixa de valor de 56 × 36 (toque = teclado). Um arrasto = um gesto.
private struct CompactRuler: View {
    @EnvironmentObject private var model: AureaModel
    let label: String
    let tag: String
    let value: Float
    let text: String
    let units: Float
    let min: Float
    let max: Float
    let unit: String
    let decimals: Int
    var showLabel = false
    var valueTag: String? = nil
    let onValue: (Float) -> Void

    init(label: String, tag: String, value: Float, text: String, units: Float, min: Float, max: Float, unit: String, decimals: Int,
         showLabel: Bool = false, valueTag: String? = nil, onValue: @escaping (Float) -> Void) {
        self.label = label; self.tag = tag; self.value = value; self.text = text; self.units = units
        self.min = min; self.max = max; self.unit = unit; self.decimals = decimals
        self.showLabel = showLabel; self.valueTag = valueTag; self.onValue = onValue
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            if showLabel { Text(label).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted) }
            HStack(spacing: 6) {
                TickRuler(value: { value }, unitsPerDp: units, active: true, height: 40)
                    .valueDrag(enabled: true, start: { value }, unitsPerDp: { units }, min: min, max: max,
                               onStart: { model.beginGesture(label) }, onValue: onValue, onEnd: { model.endGesture() })
                    .accessibilityElement()
                    .accessibilityLabel(label)
                    .accessibilityValue(text)
                    .accessibilityIdentifier(tag)
                PopoutValueBox(text: text, tag: valueTag ?? "\(tag).value", label: label) {
                    model.numericKeypad = KeypadRequest(title: label, value: value, unit: unit, min: min, max: max, decimals: decimals) { v in
                        model.beginGesture(label); onValue(v); model.endGesture()
                    }
                }
            }
        }
    }
}
