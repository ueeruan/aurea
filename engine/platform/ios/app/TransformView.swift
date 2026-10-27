import SwiftUI
import UIKit

/// Omega aparência (área 05, P0-2 / P1-2 / P1-3): o painel MOVER E TRANSFORMAR
/// da referência, na moldura comum (`PanelFrame`, 200 pt, rolagem por dentro).
/// Par do `editor/panels/TransformPanel.kt` (mesmos ids e textos).
///
/// Abas (no máximo 4): **Mover ⇄ Pivô** (2º toque na aba ativa) · **Girar** ·
/// **Escala** · **Inclinar** (não na câmera). Os extras do AUREA ficam no ⋯:
/// Desfoque de movimento e Animadores (trocam o conteúdo), ímã e aceleração do
/// trackpad. A Opacidade saiu (fica em Mesclagem e opacidade) e a Lente da
/// câmera virou o painel Opções da câmera (`CameraOptionsPanel`).
///
/// `model.transformTab` continua Int (outros arquivos escrevem): 0 Mover,
/// 1 Escala, 2 Girar, 3 (Opacidade: saiu → 0), 4 Pivô, 5 Desfoque de movimento,
/// 6 Animadores, 7 (Lente: saiu → abre `.camera`), 8 Inclinar.
struct TransformView: View {
    @EnvironmentObject private var model: AureaModel
    @Environment(\.displayScale) private var displayScale
    private var tab: Int { get { model.transformTab } nonmutating set { model.transformTab = newValue } }
    private var animatorTrack: TimelineTrack? { tab == 6 ? model.animatorRailTrack() : nil }
    /// Eixo da roda em 3D (0 X, 1 Y, 2 Z); o padrão é Z.
    @State private var axis = 2
    private var linked: Bool { model.scaleAxesLinked }
    @State private var text3D = false
    @State private var wholeText = false
    /// Mover: modo Z (o trackpad ajusta a profundidade) × modo XY.
    @State private var zPicked = false
    @State private var fineMove = false
    @State private var blurAdvanced = false
    @State private var gestureOpen = false
    @State private var gestureProject: UUID?
    @State private var gestureLayer: Int64?
    @State private var gestureComposition: UInt64?
    @State private var gestureValues: [Float] = []
    @State private var pivotDrag: PivotDragSession?
    /// Estado de um arrasto do trackpad (dedo acumulado, ímã, velocidade).
    @State private var pad = TransformPadState()
    /// Toque-empurrão: quando o dedo desceu e se o toque virou arrasto do trackpad.
    @State private var tapDown: Date?
    @State private var padTouchDragged = false
    /// Bug 6b (0.3.5): quando o último arrasto do trackpad começou. Um arrasto que
    /// começa sobre um campo não abre o teclado ao soltar (ver `TransformPillButton`).
    @State private var padBeganAt: Date?
    /// Marcas de encaixe das réguas de Escala (larguras/alturas das outras camadas, px).
    @State private var scaleMarksW: [Float] = []
    @State private var scaleMarksH: [Float] = []
    /// Marca da régua de Escala em que o valor grudou (−1 = nenhuma): háptico na entrada.
    @State private var rulerSnapIndex = -1
    /// Troca do cadeado: as réguas entram com escala Y 1,1 → 1,0 em 120 ms.
    @State private var rulerPulse: CGFloat = 1
    /// Preferências do trackpad (mesma chave lógica do Android).
    @AppStorage("omega.transform.snap") private var snapOn = true
    @AppStorage("omega.transform.accelerate") private var accelerate = false

    private var id: Int64 { model.primarySelection ?? 0 }
    private var animatedMask: UInt32 { (model.detail["animatedMask"] as? NSNumber)?.uint32Value ?? 0 }
    /// Keyframes da propriedade no quadro do cabeçote (a edição em cima de um
    /// deles edita aquele keyframe, com ou sem Auto-Key).
    private var keyHereMask: UInt32 { (model.detail["keyAtPlayhead"] as? NSNumber)?.uint32Value ?? 0 }
    private var keyMask: UInt32 { (model.detail["keyAtPlayhead"] as? NSNumber)?.uint32Value ?? 0 }
    private var timeFlags: UInt32 { (model.detail["timeFlags"] as? NSNumber)?.uint32Value ?? 0 }
    private var locked: Bool { model.selectedLayer?.locked ?? false }
    private var uses3D: Bool {
        let kind = model.selectedLayer?.kind ?? 0
        return [8, 9, 10].contains(kind) || model.selectedLayer?.threeD == true || abs(value(6)) > 0.01 || abs(value(7)) > 0.01
            || abs(value(2)) > 0.01 || [UInt32(6), 7, 2].contains { animatedMask & (1 << $0) != 0 }
    }
    private var threeD: Bool { uses3D }
    private var isCamera: Bool { model.selectedLayer?.kind == 8 }
    /// Escala da câmera: a régua edita o campo de visão (lente), não a escala.
    private var cameraScale: Bool { tab == 1 && isCamera }
    private var rotationAxis: Int { threeD ? axis : 2 }

    /// Propriedades que a aba edita (expressão, curva).
    private var props: [UInt32] {
        switch tab {
        case 0, 3: return threeD && zPicked ? [2] : [0, 1]
        case 1: return isCamera ? [] : (threeD ? [3, 4, 5] : [3, 4])
        case 2: return [UInt32(6 + rotationAxis)]
        case 4: return threeD ? [9, 10, 11] : [9, 10]
        case 8: return [13, 14]
        default: return []
        }
    }
    /// O que o ◆ grava (vetores 3D gravam XYZ juntos).
    private var keyProps: [UInt32] {
        switch tab {
        case 0, 3: return threeD ? [0, 1, 2] : [0, 1]
        case 2: return threeD ? [6, 7, 8] : [8]
        default: return props
        }
    }
    /// Expressão: a inclinação não tem (o "=" não aparece em vez de não fazer nada).
    private var exprProps: [UInt32] { tab == 8 ? [] : props }
    private var look: KeyframeLook {
        if animatorTrack != nil {
            if curveKeys.contains(where: { $0.time == model.localPlayhead }) { return .keyHere }
            return curveKeys.isEmpty ? .none : .animated
        }
        if cameraScale { return lensLook(AureaModel.cameraLensFocal) }
        if !keyProps.isEmpty && (keyProps.count == 3 ? keyProps.allSatisfy { keyMask & (1 << $0) != 0 } : keyProps.contains { keyMask & (1 << $0) != 0 }) { return .keyHere }
        return keyProps.contains { animatedMask & (1 << $0) != 0 } ? .animated : .none
    }
    private var curveKeys: [KeyframeItem] {
        if let track = animatorTrack {
            return (model.keyframes[id] ?? []).filter {
                Int($0.property) == track.property && $0.effectIndex == track.effect && $0.paramIndex == track.param
            }.sorted { $0.time < $1.time }
        }
        if cameraScale, let property = model.cameraLensTrack(AureaModel.cameraLensFocal) {
            return (model.keyframes[id] ?? []).filter { $0.property == property && $0.effectIndex == UInt32.max }.sorted { $0.time < $1.time }
        }
        let candidates = props + keyProps.filter { !props.contains($0) }
        return preferredCurveTrack(candidates.map { property in
            (model.keyframes[id] ?? []).filter { $0.property == property && $0.effectIndex == UInt32.max }.sorted { $0.time < $1.time }
        })
    }
    private var expressionLook: ExpressionLook {
        var result: ExpressionLook = .none
        for property in exprProps {
            let info = model.engine.expression(id, property: property, effect: UInt32.max, param: 0)
            guard (info["exists"] as? NSNumber)?.boolValue == true else { continue }
            let on = (info["enabled"] as? NSNumber)?.boolValue ?? true
            if on && !(info["error"] as? String ?? "").isEmpty { return .error }
            if on { result = .ok } else if result == .none { result = .off }
        }
        return result
    }
    private var expressionControlsTransform: Bool { expressionLook == .ok || expressionLook == .error }
    private func vector(_ key: String) -> [Float] { (model.detail[key] as? [NSNumber] ?? []).map(\.floatValue) }
    private func value(_ property: UInt32) -> Float {
        if property == 12 { return (model.detail["opacity"] as? NSNumber)?.floatValue ?? 1 }
        if property == 13 || property == 14 {
            // Inclinação em GRAUS (core/LayerSkew.hpp): a UI mostra o mesmo número.
            let skew = vector("skew"), index = Int(property - 13)
            return skew.indices.contains(index) ? skew[index] : 0
        }
        let group = Int(property / 3), axis = Int(property % 3)
        guard group < 4 else { return 0 }
        let data = vector(["position", "scale", "rotation", "anchor"][group])
        return data.indices.contains(axis) ? data[axis] : (group == 1 ? 1 : 0)
    }
    private var sourceSize: [Float] {
        let source = vector("sourceSize")
        return source.count >= 2 ? source : [0, 0]
    }

    // =========================================================================
    // Moldura
    // =========================================================================

    var body: some View {
        PanelFrame(rail: rail, tabs: tabs, selectedTab: selectedTabId, onTab: selectTab, scrollContent: false) {
            content
        }
        // Ids próprios das abas (além de `panel.tab.<i>` da moldura); Pivô ocupa a célula 0.
        .overlay { PanelTabTags(tags: tabTags) }
        .id("transform:\(model.projectGeneration):\(model.composition[AureaCompositionId] ?? ""):\(id)")
        .foregroundStyle(AureaColors.text)
        .onAppear {
            text3D = !(model.engine.text3D(forLayer: id) ?? [:]).isEmpty
            normalizeTab()
            syncStage()
            refreshScaleMarks()
            updateTimelineFocus()
        }
        // Face Pivô aberta: o arrasto no palco move o pivô (PreviewMetalView.pivotEvent).
        .onChange(of: tab) { next in
            endGesture()
            normalizeTab()
            syncStage()
            if next == 1 { refreshScaleMarks() }
            updateTimelineFocus()
        }
        .onChange(of: id) { _ in
            endGesture()
            text3D = !(model.engine.text3D(forLayer: id) ?? [:]).isEmpty; wholeText = false
            normalizeTab()
            refreshScaleMarks()
            updateTimelineFocus()
        }
        .onChange(of: wholeText) { _ in updateTimelineFocus() }
        .onChange(of: keyProps) { _ in updateTimelineFocus() }
        .onChange(of: model.status.modelRevision) { _ in
            if tab == 6 { updateTimelineFocus() }
            if tab == 1 && !gestureOpen { refreshScaleMarks() }
        }
        .onDisappear {
            endGesture(); model.timelineFocus = nil; model.pivotStageEdit = false
            if model.showMotionPath { model.showMotionPath = false }
        }
    }

    private var tabs: [PanelTab] {
        var list = [
            PanelTab(id: "move", label: AureaText.t("om_ap_tf_move"), glyph: CupertinoGlyph.Move,
                     alt: PanelTab(id: "pivot", label: AureaText.t("om_ap_tf_pivot"), glyph: CupertinoGlyph.SmallcircleCircle)),
            PanelTab(id: "rotate", label: AureaText.t("om_ap_tf_rotate"), glyph: CupertinoGlyph.ArrowCounterclockwise),
            PanelTab(id: "scale", label: AureaText.t("om_ap_tf_scale"), glyph: CupertinoGlyph.ArrowDownRightSquare),
        ]
        if !isCamera {
            list.append(PanelTab(id: "skew", label: AureaText.t("om_ap_tf_skew"), glyph: CupertinoGlyph.Rhombus))
        }
        return list
    }
    private var tabTags: [String] {
        var tags = [tab == 4 ? "transform.tab.pivot" : "transform.tab.move", "transform.tab.rotate", "transform.tab.scale"]
        if !isCamera { tags.append("transform.tab.skew") }
        return tags
    }
    /// Desfoque de movimento e Animadores (⋯): nenhuma aba acesa.
    private var selectedTabId: String? {
        switch tab {
        case 4: return "pivot"
        case 2: return "rotate"
        case 1: return "scale"
        case 8: return "skew"
        case 5, 6: return nil
        default: return "move"
        }
    }
    private func selectTab(_ tabId: String) {
        endGesture()
        switch tabId {
        case "pivot": tab = 4
        case "rotate": tab = 2
        case "scale": tab = 1
        case "skew": tab = isCamera ? 0 : 8
        default: tab = 0
        }
    }
    /// Números antigos: Opacidade (3) saiu → Mover; Lente (7) virou o painel da câmera.
    private func normalizeTab() {
        if tab == 3 { tab = 0 }
        if tab == 8 && isCamera { tab = 0 }
        if tab == 7 {
            tab = 0
            if isCamera { DispatchQueue.main.async { model.panel = .camera } }
        }
    }
    /// O que o palco mostra com a aba: pivô (Pivô) e trajetória (Mover).
    private func syncStage() {
        model.pivotStageEdit = tab == 4
        let path = tab == 0
        if model.showMotionPath != path { model.showMotionPath = path }
    }

    private var rail: PanelRail {
        let animator = animatorTrack
        let canKey = cameraScale || animator != nil || !keyProps.isEmpty
        return PanelRail(
            onBack: { model.panel = .none },
            keyframeLook: look,
            onKeyframe: canKey ? { toggleKey() } : nil,
            onDeleteAllKeyframes: canKey && animator == nil ? { deleteAllKeys() } : nil,
            onCurve: canKey ? { openCurve() } : nil,
            curveReady: curveKeys.count >= 2,
            expression: expressionLook,
            onExpression: exprProps.isEmpty ? nil : { openExpression() },
            moreEntries: { moreEntries() },
            moreActive: tab == 5 || tab == 6)
    }

    @ViewBuilder private var content: some View {
        switch tab {
        case 4: moveFace(pivot: true)
        case 2: rotationFace
        case 1: scaleFace
        case 8: skewFace
        case 5: motionBlurFace
        case 6: animatorsFace
        default: moveFace(pivot: false)
        }
    }

    private func updateTimelineFocus() {
        if tab == 2 && text3D && !wholeText { return }
        if let track = animatorTrack { model.focusLayerAnimator(track); return }
        if cameraScale, let property = model.cameraLensTrack(AureaModel.cameraLensFocal) {
            model.timelineFocus = [TimelineTrack(property: Int(property))]; return
        }
        model.timelineFocus = keyProps.map { TimelineTrack(property: Int($0)) }
    }

    // =========================================================================
    // Mover / Pivô — o trackpad
    // =========================================================================

    /// Mover: pílula X|Y + Z no topo do trackpad; Pivô: fileira de campos +
    /// "Centro" + grade de âncora em cima e o trackpad embaixo.
    @ViewBuilder private func moveFace(pivot: Bool) -> some View {
        VStack(spacing: 0) {
            if pivot { pivotFields.padding(.top, PanelMetrics.fieldRowTop) }
            GeometryReader { bounds in
                ZStack(alignment: .top) {
                    padSurface(pivot: pivot, size: bounds.size)
                    if !pivot { positionFields.padding(.top, PanelMetrics.fieldRowTop) }
                    if expressionControlsTransform && !pivot {
                        Text(AureaText.t("transform_expression_controlled"))
                            .font(.aurea(size: PanelMetrics.note)).foregroundStyle(AureaColors.warning)
                            .multilineTextAlignment(.center).fixedSize(horizontal: false, vertical: true)
                            .padding(.horizontal, 12)
                            .frame(maxHeight: .infinity, alignment: .bottom).padding(.bottom, 40)
                            .allowsHitTesting(false)
                            .accessibilityIdentifier("transform.expression.controlled")
                    }
                    if !pivot { preciseButton.frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottomTrailing) }
                }
                .frame(width: bounds.size.width, height: bounds.size.height)
                // Arrastar começando sobre os campos também passa para o trackpad.
                .simultaneousGesture(padDrag(pivot: pivot))
            }
        }
    }

    /// A área do trackpad: cantos, guia central e o toque-empurrão de 1 px.
    private func padSurface(pivot: Bool, size: CGSize) -> some View {
        ZStack {
            TransformCornerMarks()
            padHint(pivot: pivot)
                .opacity(gestureOpen || (expressionControlsTransform && !pivot) ? 0 : 1)
                // 0.3.4 painéis: abaixo da fileira de campos maior (par do Android).
                .padding(.top, pivot ? 0 : 56)
                .allowsHitTesting(false)
        }
        .frame(width: size.width, height: size.height)
        .contentShape(Rectangle())
        // Toque-empurrão: CURTO (< 100 ms, o tapTimeout do Android), parado (< 20 pt)
        // e sem ter virado arrasto do trackpad.
        .gesture(DragGesture(minimumDistance: 0)
            .onChanged { event in
                if tapDown == nil { tapDown = event.time; padTouchDragged = false }
            }
            .onEnded { event in
                let start = tapDown ?? event.time
                tapDown = nil
                let short = event.time.timeIntervalSince(start) < TransformPadState.tapTime
                let still = hypot(event.translation.width, event.translation.height) < TransformPadState.tapSlop
                if short && still && !padTouchDragged { nudge(at: event.location, size: size, pivot: pivot) }
            })
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(AureaText.t(pivot ? "om_ap_tf_pivot" : "om_ap_tf_move"))
        .accessibilityHint(AureaText.t(pivot ? "om_ap_tf_pivot_hint" : zPicked ? "om_ap_tf_depth_hint" : "om_ap_tf_move_hint")
                           + ". " + AureaText.t("om_ap_tf_nudge"))
        .accessibilityIdentifier(pivot ? "transform.pivot.pad" : "transform.move.pad")
    }

    @ViewBuilder private func padHint(pivot: Bool) -> some View {
        let key = pivot ? "om_ap_tf_pivot_hint" : zPicked ? "om_ap_tf_depth_hint" : "om_ap_tf_move_hint"
        VStack(spacing: 4) {
            if zPicked && !pivot { CupertinoGlyph.text(CupertinoGlyph.ArrowUp, size: 18, color: AureaColors.muted) }
            Text(AureaText.t(key)).font(.aurea(size: PanelMetrics.hint)).foregroundStyle(AureaColors.muted)
                .multilineTextAlignment(.center).padding(.horizontal, 16)
                .accessibilityIdentifier(pivot ? "transform.pivot.hint" : "transform.move.hint")
            if zPicked && !pivot { CupertinoGlyph.text(CupertinoGlyph.ArrowDown, size: 18, color: AureaColors.muted) }
        }
    }

    /// Preciso (extra do AUREA): o dedo anda 4× mais para o mesmo deslocamento.
    private var preciseButton: some View {
        Button { fineMove.toggle() } label: {
            Text(AureaText.t("panel_mover_preciso")).font(.aurea(size: PanelMetrics.chipText, weight: .semibold)).lineLimit(1)
                .foregroundStyle(fineMove ? AureaColors.accent : AureaColors.text).padding(.horizontal, PanelMetrics.chipPad)
                .frame(minHeight: PanelMetrics.chipHeight)
                .background(fineMove ? AureaColors.accent.opacity(0.18) : AureaColors.railModeFill, in: Capsule())
                .frame(minHeight: PanelMetrics.touch).contentShape(Rectangle())
        }
        .buttonStyle(.plain).padding(.trailing, 10).padding(.bottom, 4)
        .accessibilityValue(AureaText.t(fineMove ? "common_on" : "common_off"))
        .accessibilityIdentifier("transform.move.fine")
    }

    /// Pílula X|Y (só os cantos de fora arredondados) e o Z a 7 pt. Tocar no
    /// campo INATIVO troca o modo (XY ou Z); tocar no ATIVO abre o teclado do
    /// eixo; segurar sempre digita.
    private var positionFields: some View {
        let x = value(0), y = value(1), z = value(2)
        return HStack(alignment: .top, spacing: PanelMetrics.fieldGap) {
            HStack(alignment: .top, spacing: 1) {
                pillField(number(x, decimals: 2), label: "X", tint: zPicked ? AureaColors.text : AureaColors.accent,
                          corners: .leading, description: AureaText.t("panel_posicao_x"), identifier: "transform.position.x",
                          onLongPress: { positionKeypad(0) }) { tapXY(0) }
                pillField(number(y, decimals: 2), label: "Y", tint: zPicked ? AureaColors.text : AureaColors.accent,
                          corners: .trailing, description: AureaText.t("panel_posicao_y"), identifier: "transform.position.y",
                          onLongPress: { positionKeypad(1) }) { tapXY(1) }
            }
            pillField(number(z, decimals: 2), label: "Z", tint: zPicked ? AureaColors.accent : AureaColors.text,
                      description: AureaText.t("panel_profundidade_z"), identifier: "transform.position.z",
                      onLongPress: { positionKeypad(2) }) { tapZ() }
        }
        .keepLtr()
    }
    private func tapXY(_ axis: Int) {
        if zPicked { endGesture(); zPicked = false; updateTimelineFocus() } else { positionKeypad(axis) }
    }
    private func tapZ() {
        if !zPicked { endGesture(); zPicked = true; updateTimelineFocus() } else { positionKeypad(2) }
    }
    private func positionKeypad(_ axis: Int) {
        let property = UInt32(axis)
        let title = AureaText.t(["panel_posicao_x", "panel_posicao_y", "panel_profundidade_z"][axis])
        keypad(title, value(property), unit: "px", decimals: 2) { write([property: $0]) }
    }

    /// Pivô: X/Y(/Z) relativos ao centro da mídia, "Centro" e a grade de âncora (extras do AUREA).
    private var pivotFields: some View {
        let x = value(9) - sourceSize[0] / 2, y = value(10) - sourceSize[1] / 2
        return ScrollView(.horizontal, showsIndicators: false) {
            HStack(alignment: .top, spacing: PanelMetrics.fieldGap) {
                HStack(alignment: .top, spacing: 1) {
                    pillField(number(x, decimals: 2), label: "X", tint: AureaColors.accent, corners: .leading,
                              description: AureaText.t("panel_pivo_x"), identifier: "transform.pivot.x") {
                        keypad(AureaText.t("panel_pivo_x"), x, unit: "px", decimals: 2) { write([9: $0 + sourceSize[0] / 2]) }
                    }
                    pillField(number(y, decimals: 2), label: "Y", tint: AureaColors.accent, corners: .trailing,
                              description: AureaText.t("panel_pivo_y"), identifier: "transform.pivot.y") {
                        keypad(AureaText.t("panel_pivo_y"), y, unit: "px", decimals: 2) { write([10: $0 + sourceSize[1] / 2]) }
                    }
                }
                if threeD {
                    pillField(number(value(11), decimals: 2), label: "Z", tint: AureaColors.accent,
                              description: AureaText.t("edt_pivot_z"), identifier: "transform.pivot.z") {
                        keypad(AureaText.t("edt_pivot_z"), value(11), unit: "px", decimals: 2) { write([11: $0]) }
                    }
                }
                Button { write([9: sourceSize[0] / 2, 10: sourceSize[1] / 2]) } label: {
                    Text(AureaText.t("panel_centro")).font(.aurea(size: PanelMetrics.chipText, weight: .semibold)).foregroundStyle(AureaColors.text)
                        .lineLimit(1).padding(.horizontal, PanelMetrics.chipPad).frame(minHeight: PanelMetrics.fieldHeight)
                        .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: PanelMetrics.fieldRadius))
                        .frame(minHeight: PanelMetrics.touch, alignment: .top).contentShape(Rectangle())
                }
                .buttonStyle(.plain).disabled(locked)
                .accessibilityIdentifier("transform.pivot.center")
                // Âncora predefinida (app antigo): 9 pontos da mídia; a camada fica no lugar.
                AnchorPresetGrid { fx, fy in model.presetAnchor(id, fx: fx, fy: fy) }
                    .disabled(locked)
                    .accessibilityIdentifier("transform.pivot.anchor")
            }
            .padding(.horizontal, 8)
        }
        .keepLtr()
        // 0.3.4 painéis: a fileira de campos maior (campo 36 + rótulo); era 44.
        .frame(height: PanelMetrics.fieldBlock - PanelMetrics.fieldRowTop)
    }

    private func padDrag(pivot: Bool) -> some Gesture {
        DragGesture(minimumDistance: 8)
            .onChanged { event in padStep(event, pivot: pivot) }
            .onEnded { _ in endGesture() }
    }

    /// Começo do arrasto: valores de PARTIDA, alvos do ímã e o dedo no eixo
    /// "grudento" do motor (core/SnapMath.hpp).
    private func padBegin(_ event: DragGesture.Value, pivot: Bool) {
        gestureValues = pivot ? [value(9), value(10), value(11)] : [value(0), value(1), value(2)]
        pivotDrag = pivot ? PivotDragSession.pivotPoint(model).flatMap { PivotDragSession(model: model, pivot: $0) } : nil
        var state = TransformPadState()
        state.gain = Float(max(1, model.compositionWidth)) / 360 * (fineMove && !pivot ? 0.25 : 1)
        state.lastTime = event.time.addingTimeInterval(-0.016)
        if !pivot {
            state.finger = SIMD3(value(0), value(1), value(2))
            if snapOn && !zPicked {
                // Patamar de 25 px FÍSICOS de dedo (como o Android), nas unidades da posição (ganho do pad).
                state.plateau = TransformPadState.plateauPixels / Float(max(1, displayScale)) * state.gain
                let tx = model.snapTargets(axis: 0), ty = model.snapTargets(axis: 1)
                state.targetsX = tx.map { NSNumber(value: $0.value) }; state.guidesX = tx.map { $0.guide }
                state.targetsY = ty.map { NSNumber(value: $0.value) }; state.guidesY = ty.map { $0.guide }
                if !state.targetsX.isEmpty {
                    state.finger.x = model.engine.snapFingerFromValue(state.targetsX, plateau: state.plateau, value: value(0))
                }
                if !state.targetsY.isEmpty {
                    state.finger.y = model.engine.snapFingerFromValue(state.targetsY, plateau: state.plateau, value: value(1))
                }
            }
        }
        pad = state
        padTouchDragged = true
        padBeganAt = Date()
        beginGesture()
    }

    private func padStep(_ event: DragGesture.Value, pivot: Bool) {
        if !gestureOpen {
            guard !locked else { return }
            padBegin(event, pivot: pivot)
        }
        guard gestureValues.count >= 3 else { return }
        let stepX = event.translation.width - pad.last.width, stepY = event.translation.height - pad.last.height
        let dt = Float(event.time.timeIntervalSince(pad.lastTime))
        pad.last = event.translation; pad.lastTime = event.time
        var factor: Float = 1
        if accelerate {
            // Aceleração opcional (ControlPad): velocidade em px/s amortecida 0,65/0,35.
            let distance = Float(hypot(stepX, stepY) * displayScale)
            let speed = dt > 0 ? distance / dt : pad.speed
            pad.speed = pad.speed * 0.65 + speed * 0.35
            factor = model.engine.padAcceleration(pad.speed)
        }
        let dx = Float(stepX) * pad.gain * factor, dy = Float(stepY) * pad.gain * factor
        guard dx.isFinite, dy.isFinite else { return }
        if pivot {
            // O pivô anda na composição e a posição compensa: a imagem fica.
            pad.finger.x += dx; pad.finger.y += dy
            if let drag = pivotDrag, !locked { drag.move(to: drag.start + SIMD2(pad.finger.x, pad.finger.y), model: model) }
            return
        }
        if zPicked {
            pad.finger.z -= dy
            write([2: pad.finger.z])
            return
        }
        pad.finger.x += dx; pad.finger.y += dy
        let x = snapped(axis: 0), y = snapped(axis: 1)
        write([0: x, 1: y])
    }

    /// O valor do eixo com o ímã grudento; acende a guia do alvo ativo no palco.
    private func snapped(axis: Int) -> Float {
        let finger = axis == 0 ? pad.finger.x : pad.finger.y
        let targets = axis == 0 ? pad.targetsX : pad.targetsY
        guard snapOn, !targets.isEmpty, pad.plateau > 0 else { setGuide(axis: axis, nil); return finger }
        let out = model.engine.snapValueFromFinger(targets, plateau: pad.plateau, finger: finger).map(\.floatValue)
        guard out.count >= 2, out[0].isFinite else { setGuide(axis: axis, nil); return finger }
        let index = Int(out[1])
        let previous = axis == 0 ? pad.activeX : pad.activeY
        if index >= 0 && index != previous { UIImpactFeedbackGenerator(style: .light).impactOccurred() }
        if axis == 0 { pad.activeX = index } else { pad.activeY = index }
        let guides = axis == 0 ? pad.guidesX : pad.guidesY
        setGuide(axis: axis, index >= 0 && index < guides.count ? guides[index] : nil)
        return out[0]
    }
    private func setGuide(axis: Int, _ guide: Float?) {
        if axis == 0 { if model.snapGuideX != guide { model.snapGuideX = guide } }
        else { if model.snapGuideY != guide { model.snapGuideY = guide } }
    }

    /// Toque curto numa faixa de ¼ da borda: empurra 1 px para aquele lado.
    private func nudge(at point: CGPoint, size: CGSize, pivot: Bool) {
        guard !locked, !gestureOpen else { return }
        let side = Int(model.engine.nudgeSide(Float(point.x), y: Float(point.y), width: Float(size.width), height: Float(size.height)))
        guard side > 0 else { return }
        let dx: Float = side == 1 ? -1 : side == 3 ? 1 : 0
        let dy: Float = side == 2 ? -1 : side == 4 ? 1 : 0
        if pivot {
            guard let start = PivotDragSession.pivotPoint(model), let drag = PivotDragSession(model: model, pivot: start) else { return }
            model.beginGesture("empurrar o pivô")
            drag.move(to: start + SIMD2(dx, dy), model: model)
            model.endGesture()
            return
        }
        if zPicked {
            guard dy != 0 else { return }
            write([2: value(2) - dy])
        } else if dx != 0 {
            write([0: value(0) + dx])
        } else {
            write([1: value(1) + dy])
        }
    }

    // =========================================================================
    // Girar — a roda
    // =========================================================================

    @ViewBuilder private var rotationFace: some View {
        if text3D && !wholeText { lettersFace } else {
            VStack(spacing: 0) {
                if text3D {
                    Button(AureaText.t("t3d_letters")) { wholeText = false }
                        .font(.aurea(size: 12, weight: .semibold)).frame(minHeight: 44)
                }
                if threeD { rotationAxisRow.padding(.top, PanelMetrics.fieldRowTop) }
                rotationDial
            }
        }
    }
    private var rotationDial: some View {
        let property = UInt32(6 + rotationAxis)
        let angle = value(property)
        return TransformDial(angle: angle, enabled: !locked, snap: dialSnap,
                             onBegin: { beginGesture() }, onValue: { write([property: $0]) },
                             onEnd: { endGesture() }, onTap: { rotationKeypad(rotationAxis) })
            .padding(.horizontal, 6).padding(.vertical, 8)
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(AureaText.t("om_ap_tf_dial"))
            .accessibilityValue(number(angle, decimals: 1) + "°")
            .accessibilityAddTraits(.isButton)
            .accessibilityAction { rotationKeypad(rotationAxis) }
            .accessibilityAdjustableAction { direction in
                let step: Float
                switch direction {
                case .increment: step = 1
                case .decrement: step = -1
                @unknown default: return
                }
                beginGesture(); write([property: value(property) + step]); endGesture()
            }
            .accessibilityIdentifier("transform.rotation.dial")
    }
    /// Ímã de 45° da roda (core/SnapMath.hpp `dial_snap`, pela ponte).
    private func dialSnap(_ angle: Float) -> (value: Float, snapped: Bool) {
        let out = model.engine.dialSnap(angle).map(\.floatValue)
        return out.count >= 2 && out[0].isFinite ? (out[0], out[1] >= 0.5) : (angle, false)
    }
    /// 3D: X°/Y°/Z° (mesmo desenho da pílula); o ativo diz qual eixo a roda gira.
    private var rotationAxisRow: some View {
        HStack(alignment: .top, spacing: 1) {
            ForEach(0..<3, id: \.self) { index in
                let name = ["X", "Y", "Z"][index]
                pillField(number(value(UInt32(6 + index)), decimals: 1) + "°", label: name,
                          tint: rotationAxis == index ? AureaColors.accent : AureaColors.text,
                          corners: index == 0 ? .leading : index == 2 ? .trailing : .square,
                          description: AureaText.t("om_ap_tf_axis", name),
                          identifier: "transform.rotation.axis.\(name.lowercased())",
                          onLongPress: { rotationKeypad(index) }) {
                    if rotationAxis == index { rotationKeypad(index) }
                    else { endGesture(); axis = index; updateTimelineFocus() }
                }
                .accessibilityAddTraits(rotationAxis == index ? .isSelected : [])
            }
        }
        .keepLtr()
    }
    private func rotationKeypad(_ index: Int) {
        let property = UInt32(6 + index)
        keypad(AureaText.t("ios_rotation_axis", ["X", "Y", "Z"][index]), value(property), unit: "°", decimals: 1) {
            write([property: $0])
        }
    }
    private var lettersFace: some View {
        VStack(spacing: 0) {
            HStack {
                Text(AureaText.t("t3d_letters")).foregroundStyle(AureaColors.accent)
                Spacer()
                Button(AureaText.t("t3d_whole_object")) { wholeText = true }.frame(minHeight: 44)
            }.font(.subheadline).padding(.horizontal, 12)
            EffectsView(focusedType: fxEffectTypeId("aurea.text3d.layout"), embedded: true)
        }
    }

    // =========================================================================
    // Escala — px da caixa, cadeado, réguas com marcas de encaixe
    // =========================================================================

    @ViewBuilder private var scaleFace: some View {
        if isCamera { cameraScaleFace } else { layerScaleFace }
    }

    private var layerScaleFace: some View {
        let d = model.detail
        let w = StageGeom.width(d), h = StageGeom.height(d)
        let sized = w > 0 && h > 0
        let sx = value(3), sy = value(4)
        let widthText = sized ? number(w * sx, decimals: 1) : number(sx * 100, decimals: 1) + "%"
        let heightText = sized ? number(h * sy, decimals: 1) : number(sy * 100, decimals: 1) + "%"
        let heightIsLarger = sized && abs(h * sy) > abs(w * sx)
        // 0.3.4 painéis: as réguas repartem a altura que sobra no painel (par do Android).
        return GeometryReader { geo in
            let space = PanelMetrics.rulerSpace(geo.size.height)
            let tall = PanelMetrics.fillRulers(space, count: 1, min: PanelMetrics.rulerTall)
            let pair = PanelMetrics.fillRulers(space, count: 2, min: PanelMetrics.ruler)
            ScrollView(.vertical, showsIndicators: false) {
                VStack(spacing: 0) {
                    // No 3D (com o Z) a fileira pode passar da largura: rola de lado.
                    PanelFieldRow {
                        HStack(alignment: .top, spacing: 6) {
                            pillField(widthText, label: AureaText.t("om_ap_tf_width"), tint: AureaColors.accent,
                                      description: AureaText.t("om_ap_tf_width"), identifier: "transform.scale.width") { scaleKeypad(axisY: false) }
                            linkButton
                            pillField(heightText, label: AureaText.t("om_ap_tf_height"), tint: AureaColors.accent,
                                      description: AureaText.t("om_ap_tf_height"), identifier: "transform.scale.height") { scaleKeypad(axisY: true) }
                            if threeD {
                                pillField(number(value(5) * 100, decimals: 1) + "%", label: "Z", tint: AureaColors.accent,
                                          description: AureaText.t("edt_scale_z"), identifier: "transform.scale.z") { scaleZKeypad() }
                                    .padding(.leading, PanelMetrics.fieldGap - 6)
                            }
                        }
                    }
                    .keepLtr()
                    .padding(.top, PanelMetrics.fieldRowTop)
                    VStack(spacing: PanelMetrics.rulerGap) {
                        if linked {
                            scaleRuler(axisY: heightIsLarger, primary: true, identifier: "transform.scale.ruler", height: tall)
                        } else {
                            scaleRuler(axisY: false, primary: true, identifier: "transform.scale.ruler", height: pair)
                            scaleRuler(axisY: true, primary: false, identifier: "transform.scale.ruler.height", height: pair)
                        }
                    }
                    .scaleEffect(x: 1, y: rulerPulse, anchor: .center)
                    .padding(.top, PanelMetrics.rulerGap)
                    .padding(.bottom, PanelMetrics.rulerBottom)
                }
                .padding(.horizontal, 8)
            }
        }
    }

    /// Cadeado 25 × 25 (alvo de 44): travado = 1 régua (maior lado), destravado = 2.
    private var linkButton: some View {
        Button {
            endGesture()
            model.scaleAxesLinked.toggle()
            rulerPulse = 1.1
            DispatchQueue.main.async { withAnimation(.easeOut(duration: 0.12)) { rulerPulse = 1 } }
        } label: {
            MaterialGlyph(linked ? "rounded.Link" : "rounded.LinkOff", size: 20, color: linked ? AureaColors.accent : AureaColors.text)
                .frame(width: PanelMetrics.fieldHeight, height: PanelMetrics.fieldHeight)
                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: PanelMetrics.fieldRadius))
                .frame(width: PanelMetrics.touch, height: PanelMetrics.touch, alignment: .top)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityLabel(AureaText.t("om_ap_tf_link"))
        .accessibilityValue(AureaText.t(linked ? "common_on" : "common_off"))
        .accessibilityIdentifier("transform.scale.link")
    }

    /// Uma régua de Escala: px da caixa (`StageGeom` × escala) ou % sem tamanho.
    private func scaleRuler(axisY: Bool, primary: Bool, identifier: String, height: CGFloat) -> some View {
        let d = model.detail
        let w = StageGeom.width(d), h = StageGeom.height(d)
        let sized = w > 0 && h > 0
        let side = axisY ? h : w
        // Passo constante no gesto (px por pt): ~300 pt de dedo atravessam a mídia.
        let units: Float = sized ? max(0.5, max(w, h) / 300) : 0.5
        let property: UInt32 = axisY ? 4 : 3
        let marks = sized ? (axisY ? scaleMarksH : scaleMarksW) : []
        let current = sized ? side * value(property) : value(property) * 100
        return GeometryReader { bounds in
            TickRuler(value: { sized ? side * value(property) : value(property) * 100 }, unitsPerDp: units,
                      active: primary, height: bounds.size.height)
                .overlay { TransformRulerMarks(value: current, unitsPerDp: units, marks: marks, zero: false) }
                .valueDrag(enabled: !locked, start: { sized ? side * value(property) : value(property) * 100 },
                           unitsPerDp: { units }, min: -.infinity, max: .infinity,
                           onStart: {
                               gestureValues = [value(3) * 100, value(4) * 100, value(5) * 100]
                               rulerSnapIndex = -1
                               beginGesture()
                           },
                           onValue: { amount in
                               let v = rulerMagnet(amount, marks: marks, units: units)
                               let percent = sized ? (side != 0 ? v / side * 100 : 0) : v
                               scaleWrite(axisY: axisY, amount: percent, original: gestureValues)
                           },
                           onEnd: { endGesture() })
        }
        .frame(height: height)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(AureaText.t(axisY ? "om_ap_tf_height" : "om_ap_tf_width"))
        .accessibilityValue(sized ? number(current, decimals: 1) + " px" : number(current, decimals: 1) + "%")
        .accessibilityAdjustableAction { direction in
            let step: Float
            switch direction {
            case .increment: step = 1
            case .decrement: step = -1
            @unknown default: return
            }
            let original = [value(3) * 100, value(4) * 100, value(5) * 100]
            beginGesture(); scaleWrite(axisY: axisY, amount: original[axisY ? 1 : 0] + step, original: original); endGesture()
        }
        .accessibilityIdentifier(identifier)
    }
    /// Ímã da régua: a menos de 4 pt de dedo de uma marca, o valor gruda nela.
    private func rulerMagnet(_ amount: Float, marks: [Float], units: Float) -> Float {
        guard snapOn, !marks.isEmpty else { rulerSnapIndex = -1; return amount }
        var best = -1, distance = Float.infinity
        for (index, mark) in marks.enumerated() where abs(abs(amount) - mark) < distance {
            best = index; distance = abs(abs(amount) - mark)
        }
        guard best >= 0, distance <= 4 * units else { rulerSnapIndex = -1; return amount }
        if best != rulerSnapIndex { UIImpactFeedbackGenerator(style: .light).impactOccurred() }
        rulerSnapIndex = best
        return amount < 0 ? -marks[best] : marks[best]
    }
    private func refreshScaleMarks() {
        guard tab == 1, !isCamera, model.primarySelection != nil else {
            if !scaleMarksW.isEmpty { scaleMarksW = [] }
            if !scaleMarksH.isEmpty { scaleMarksH = [] }
            return
        }
        let w = model.snapTargets(axis: 2).map { $0.value }, h = model.snapTargets(axis: 3).map { $0.value }
        if w != scaleMarksW { scaleMarksW = w }
        if h != scaleMarksH { scaleMarksH = h }
    }
    /// Teclado em px (a caixa) que vira escala; sem tamanho, em %.
    private func scaleKeypad(axisY: Bool) {
        let d = model.detail
        let side = axisY ? StageGeom.height(d) : StageGeom.width(d)
        let original = [value(3) * 100, value(4) * 100, value(5) * 100]
        let title = AureaText.t(axisY ? "om_ap_tf_height" : "om_ap_tf_width")
        if side > 0 && StageGeom.hasSize(d) {
            keypad(title, side * value(axisY ? 4 : 3), unit: "px", decimals: 1) { amount in
                scaleWrite(axisY: axisY, amount: amount / side * 100, original: original)
            }
        } else {
            keypad(title, original[axisY ? 1 : 0], unit: "%", decimals: 1) { amount in
                scaleWrite(axisY: axisY, amount: amount, original: original)
            }
        }
    }
    private func scaleZKeypad() {
        keypad(AureaText.t("edt_scale_z"), value(5) * 100, unit: "%", decimals: 1) { amount in
            if linked {
                let from = value(5) * 100, factor = from != 0 ? amount / from : 1
                if from != 0 {
                    // Travado = zoom uniforme do volume (regra do motor: Z de conteúdo acompanha X).
                    let s = model.engine.gestureScale3D(Int32(model.selectedLayer?.kind ?? 0), scaleX: value(3), scaleY: value(4),
                                                        scaleZ: value(5), axis: 3, factor: factor).map(\.floatValue)
                    if s.count >= 3 { write([3: s[0], 4: s[1], 5: s[2]]) }
                } else { write([3: amount / 100, 4: amount / 100, 5: amount / 100]) }
            } else { write([5: amount / 100]) }
        }
    }
    private func scaleWrite(axisY: Bool, amount: Float, original: [Float]) {
        guard original.count >= 3 else { return }
        if linked {
            let from = original[axisY ? 1 : 0], multiplier = from != 0 ? amount / from : 1
            let x = axisY ? (from != 0 ? original[0] * multiplier : amount) : amount
            let y = axisY ? amount : (from != 0 ? original[1] * multiplier : amount)
            var changes: [UInt32: Float] = [3: x / 100, 4: y / 100]
            // Volume: a profundidade efetiva acompanha sem esticar (Z de conteúdo é relativo a X no motor).
            if threeD {
                let scaled = model.engine.gestureScale3D(Int32(model.selectedLayer?.kind ?? 0), scaleX: original[0] / 100,
                    scaleY: original[1] / 100, scaleZ: original[2] / 100, axis: 3, factor: multiplier)
                changes[5] = from != 0 && scaled.count >= 3 ? scaled[2].floatValue : amount / 100
            }
            write(changes)
        } else { write([(axisY ? 4 : 3): amount / 100]) }
    }
    private var canFit: Bool {
        (model.selectedLayer?.kind ?? 0) != 3 && StageGeom.hasSize(model.detail)
    }
    /// Preencher / Ajustar / Tamanho da composição (agora no ⋯).
    private var fitEntries: [MenuEntry] {
        let d = model.detail
        let size: [Float] = StageGeom.hasSize(d) ? [StageGeom.width(d), StageGeom.height(d)] : sourceSize
        let ratios = [Float(model.compositionWidth) / max(1, size[0]), Float(model.compositionHeight) / max(1, size[1])]
        let choices = [max(ratios[0], ratios[1]), min(ratios[0], ratios[1])]
        let keys = ["panel_preencher", "panel_ajustar", "panel_tamanho_composicao"]
        let tags = ["transform.fit.fill", "transform.fit.contain", "transform.fit.composition"]
        return (0..<3).map { index in
            let scale = index < 2 ? choices[index] : -1
            let selected = index < 2 && abs(abs(value(3)) - scale) < 0.001 && abs(abs(value(4)) - scale) < 0.001
            return MenuEntry.item(AureaText.t(keys[index]), checked: selected, enabled: !locked, tag: tags[index]) {
                endGesture()
                if index == 2 { model.makeCompositionSize(id) } else { model.fitToCanvas([id], fill: index == 0) }
            }
        }
    }

    /// Câmera: "Câmera: zoom x, ângulo y°" e a régua edita o campo de visão.
    private var cameraScaleFace: some View {
        let lens = model.cameraLens
        let focal = lens.count >= 1 ? lens[0] : 50
        let fov = lens.count >= 2 ? lens[1] : CameraLensMath.fovFromFocal(focal)
        let summary = AureaText.t("om_ap_tf_camera_scale", CameraLensMath.formatZoom(CameraLensMath.zoomFromFocal(focal)),
                                  CameraLensMath.formatAngle(fov))
        let angle: () -> Float = {
            let now = model.cameraLens
            return now.count >= 2 ? now[1] : CameraLensMath.fovFromFocal(now.first ?? 50)
        }
        let typeAngle: () -> Void = {
            guard !locked else { return }
            keypad(AureaText.t("om_ap_cam_view_angle"), fov, unit: "°", min: CameraLensMath.angleMin,
                   max: CameraLensMath.angleMax, decimals: 1) {
                model.setCameraLens(AureaModel.cameraLensFocal, value: CameraLensMath.focalForAngle($0))
            }
        }
        // 0.3.4 painéis: o resumo em texto de rótulo (13) e a régua crescendo com o painel.
        return GeometryReader { geo in
        let rulerHeight = PanelMetrics.fillRulers(PanelMetrics.rulerSpace(geo.size.height), count: 1, min: PanelMetrics.rulerTall)
        ScrollView(.vertical, showsIndicators: false) {
        VStack(alignment: .leading, spacing: 0) {
            // O texto da referência; tocar digita o ângulo.
            Button(action: typeAngle) {
                Text(summary).font(.aurea(size: PanelMetrics.label)).foregroundStyle(AureaColors.subtle)
                    .lineLimit(2).multilineTextAlignment(.leading)
                    .frame(maxWidth: .infinity, minHeight: PanelMetrics.touch, alignment: .leading)
                    .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .padding(.top, PanelMetrics.fieldRowTop).padding(.horizontal, 12)
            .accessibilityLabel(summary)
            .accessibilityIdentifier("transform.scale.camera")
            GeometryReader { bounds in
                TickRuler(value: angle, unitsPerDp: 0.1, active: true, height: bounds.size.height)
                    .valueDrag(enabled: !locked, start: angle, unitsPerDp: { 0.1 },
                               min: CameraLensMath.angleMin, max: CameraLensMath.angleMax,
                               onStart: { beginGesture() },
                               onValue: { model.setCameraLens(AureaModel.cameraLensFocal, value: CameraLensMath.focalForAngle($0)) },
                               onEnd: { endGesture() })
            }
            .frame(height: rulerHeight)
            .padding(.top, PanelMetrics.rulerGap).padding(.horizontal, 8)
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(AureaText.t("om_ap_cam_view_angle"))
            .accessibilityValue(CameraLensMath.formatAngle(fov) + "°")
            .accessibilityAction { typeAngle() }
            .accessibilityAdjustableAction { direction in
                let step: Float
                switch direction {
                case .increment: step = 1
                case .decrement: step = -1
                @unknown default: return
                }
                beginGesture()
                model.setCameraLens(AureaModel.cameraLensFocal, value: CameraLensMath.focalForAngle(fov + step))
                endGesture()
            }
            .accessibilityIdentifier("transform.scale.ruler")
            .padding(.bottom, PanelMetrics.rulerBottom)
        }
        }
        }
    }

    // =========================================================================
    // Inclinar
    // =========================================================================

    private var skewFace: some View {
        let x = value(13), y = value(14)
        // 0.3.4 painéis: as duas réguas repartem a altura que sobra no painel (par do Android).
        return GeometryReader { geo in
        let rulerHeight = PanelMetrics.fillRulers(PanelMetrics.rulerSpace(geo.size.height), count: 2, min: PanelMetrics.ruler)
        ScrollView(.vertical, showsIndicators: false) {
        VStack(spacing: 0) {
            HStack(alignment: .top, spacing: PanelMetrics.fieldGap) {
                pillField(number(x, decimals: 1) + "°", label: AureaText.t("om_ap_tf_skew_x"), tint: AureaColors.accent,
                          description: AureaText.t("om_ap_tf_skew_x"), identifier: "transform.skew.x") {
                    keypad(AureaText.t("om_ap_tf_skew_x"), x, unit: "°", decimals: 1) { writeSkew([13: $0]) }
                }
                pillField(number(y, decimals: 1) + "°", label: AureaText.t("om_ap_tf_skew_y"), tint: AureaColors.text,
                          description: AureaText.t("om_ap_tf_skew_y"), identifier: "transform.skew.y") {
                    keypad(AureaText.t("om_ap_tf_skew_y"), y, unit: "°", decimals: 1) { writeSkew([14: $0]) }
                }
            }
            .keepLtr()
            .padding(.top, PanelMetrics.fieldRowTop)
            VStack(spacing: PanelMetrics.rulerGap) {
                skewRuler(13, primary: true, identifier: "transform.skew.ruler.x", height: rulerHeight)
                skewRuler(14, primary: false, identifier: "transform.skew.ruler.y", height: rulerHeight)
            }
            .padding(.top, PanelMetrics.rulerGap)
            .padding(.bottom, PanelMetrics.rulerBottom)
        }
        .padding(.horizontal, 8)
        }
        }
    }
    /// Régua da inclinação: passo 0,1° por pt e linha do zero.
    private func skewRuler(_ property: UInt32, primary: Bool, identifier: String, height: CGFloat = PanelMetrics.ruler) -> some View {
        let units: Float = 0.1
        let current = value(property)
        return GeometryReader { bounds in
            TickRuler(value: { value(property) }, unitsPerDp: units, active: primary, height: bounds.size.height)
                .overlay { TransformRulerMarks(value: current, unitsPerDp: units, marks: [], zero: true) }
                .valueDrag(enabled: !locked, start: { value(property) }, unitsPerDp: { units }, min: -.infinity, max: .infinity,
                           onStart: { beginGesture() }, onValue: { writeSkew([property: $0]) }, onEnd: { endGesture() })
        }
        .frame(height: height)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(AureaText.t(property == 13 ? "om_ap_tf_skew_x" : "om_ap_tf_skew_y"))
        .accessibilityValue(number(current, decimals: 1) + "°")
        .accessibilityAdjustableAction { direction in
            let step: Float
            switch direction {
            case .increment: step = 1
            case .decrement: step = -1
            @unknown default: return
            }
            writeSkew([property: value(property) + step])
        }
        .accessibilityIdentifier(identifier)
    }
    /// Inclinação em graus pelo caminho comum do modelo (`setTransform` 13/14:
    /// trilha animada grava keyframe no cabeçote, senão o valor parado).
    private func writeSkew(_ changes: [UInt32: Float]) {
        guard !locked, !changes.isEmpty, changes.values.allSatisfy(\.isFinite) else { return }
        if gestureOpen {
            guard gestureProject == model.projectGeneration, gestureLayer == id,
                  gestureComposition == (model.composition[AureaCompositionId] as? NSNumber)?.uint64Value else { return }
        }
        let layer = id
        if !gestureOpen { model.engine.run { $0.beginUndoGroup() } }
        for (property, amount) in changes.sorted(by: { $0.key < $1.key }) {
            model.setTransform(property, value: amount, layer: layer)
        }
        if !gestureOpen { model.engine.run { $0.endUndoGroup() } }
    }

    // =========================================================================
    // Extras do AUREA: desfoque de movimento e animadores
    // =========================================================================

    private var motionBlurFace: some View {
        let settings = model.motionBlurControls
        let on = timeFlags & 2 != 0 && settings.enabled
        return ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                blurToggle("panel_desfoque_movimento", hint: "panel_borra_camada_direcao_ela_move", checked: on) { enabled in
                    beginGesture()
                    if enabled && !model.compMotionBlur { model.setCompositionMotionBlur(true) }
                    model.engine.setMotionBlur(enabled, forLayer: id); model.refreshModel(force: true); endGesture()
                }
                if on {
                    motionBlurRow("mb_shutter_angle", value: settings.angle, lower: 0, upper: 720,
                                  unit: "°", decimals: 1, identifier: "motionblur.shutter", onValue: model.changeShutterAngle)
                    Text(AureaText.t("mb_comp_settings_hint")).font(.caption)
                        .foregroundStyle(AureaColors.muted).padding(.top, 4).padding(.leading, 4)
                    DisclosureGroup(isExpanded: $blurAdvanced) {
                        motionBlurAdvanced(settings)
                    } label: {
                        Text(AureaText.t("mb_advanced")).font(.subheadline.weight(.semibold)).frame(minHeight: 44)
                            // Scope the identifier to the header; on the group
                            // SwiftUI also assigns it to every expanded control.
                            .accessibilityIdentifier("motionblur.advanced")
                    }.tint(AureaColors.accent).padding(.vertical, 4)
                    // Comprimento do rastro SÓ desta camada (× o obturador do projeto).
                    let length = model.engine.layerMotionBlurLength(id) * 100
                    scalarRow(AureaText.t("la_motion_blur_length"), amount: length, speed: 0.5, limit: 400,
                              onValue: { model.engine.setLayerMotionBlurLength(min(400, max(0, $0)) / 100, forLayer: id); model.refreshModel(force: true) }, onTap: {
                        keypad(AureaText.t("la_motion_blur_length"), length, unit: "%", min: 0, max: 400, decimals: 0) {
                            model.engine.setLayerMotionBlurLength($0 / 100, forLayer: id); model.refreshModel(force: true)
                        }
                    })
                    Text(AureaText.t("la_motion_blur_length_hint")).font(.aurea(size: 11.5))
                        .foregroundStyle(AureaColors.muted).padding(.top, 4).padding(.leading, 4)
                }
                if model.selectedLayer?.kind == 1 {
                    blurToggle("panel_desfoque_movimento_video", hint: "panel_borra_mexe_dentro_video", checked: timeFlags & 32 != 0) { enabled in
                        model.engine.setVectorBlur(forLayer: id, amount: enabled ? 1 : 0); model.refreshModel(force: true)
                    }.padding(.top, 8)
                }
            }.padding(.leading, 8).padding(.trailing, 12).padding(.top, 6).padding(.bottom, 12)
        }
    }

    private func motionBlurAdvanced(_ settings: MotionBlurControls) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            motionBlurRow("mb_shutter_phase", value: settings.phase, lower: -360, upper: 360,
                          unit: "°", decimals: 1, identifier: "motionblur.phase", onValue: model.changeShutterPhase)
            Button(AureaText.t("mb_center_exposure")) { model.centerMotionBlurExposure() }
                .font(.subheadline).frame(minHeight: 44).accessibilityIdentifier("motionblur.center")
            Text(AureaText.t("mb_phase_hint")).font(.caption).foregroundStyle(AureaColors.muted)
            motionBlurRow("mb_samples_per_frame", value: Float(settings.samples), lower: 2,
                          upper: Float(min(64, settings.adaptiveLimit)), identifier: "motionblur.samples",
                          onValue: model.changeMotionBlurSamples)
            motionBlurRow("mb_adaptive_limit", value: Float(settings.adaptiveLimit), lower: Float(settings.samples),
                          upper: 256, identifier: "motionblur.adaptive", onValue: model.changeMotionBlurAdaptiveLimit)
            Text(AureaText.t("mb_quality_hint")).font(.caption).foregroundStyle(AureaColors.muted)
            // 0.3.2: motion blur 3D rápido (vetores) × alta qualidade (amostras).
            blurToggle("mb_vector_3d", hint: "mb_vector_3d_hint", checked: settings.vectorBlur) { model.setMotionBlurVector($0) }
                .frame(minHeight: 48)
                .accessibilityElement(children: .ignore)
                .accessibilityLabel(AureaText.t("mb_vector_3d"))
                .accessibilityHint(AureaText.t("mb_vector_3d_hint"))
                .accessibilityValue(AureaText.t(settings.vectorBlur ? "common_on" : "common_off"))
                .accessibilityAddTraits(.isButton)
                .accessibilityAction { model.setMotionBlurVector(!settings.vectorBlur) }
                .accessibilityIdentifier("motionblur.vector3d")
        }.padding(.vertical, 4)
    }

    private func motionBlurRow(_ key: String, value: Float, lower: Float, upper: Float,
                               unit: String = "", decimals: Int = 0, identifier: String,
                               onValue: @escaping (Float) -> Void) -> some View {
        let label = AureaText.t(key)
        let shown = number(value, decimals: decimals) + unit
        let edit = { keypad(label, value, unit: unit, min: lower, max: upper, decimals: decimals, onValue: onValue) }
        return VStack(alignment: .leading, spacing: 2) {
            Text(label).font(.caption.weight(.semibold)).fixedSize(horizontal: false, vertical: true)
            HStack(spacing: 8) {
                TickRuler(value: { value }, unitsPerDp: decimals == 0 ? 0.1 : 0.5, active: true, height: 44)
                    .frame(maxWidth: .infinity)
                    .valueDrag(enabled: true, start: { value }, unitsPerDp: { decimals == 0 ? 0.1 : 0.5 }, min: lower, max: upper,
                               onStart: beginGesture, onValue: { onValue(decimals == 0 ? $0.rounded() : $0) }, onEnd: endGesture)
                Button(action: edit) { ValueBox(shown, width: 76).frame(minHeight: 44).contentShape(Rectangle()) }
                    .buttonStyle(.plain)
            }
        }.frame(minHeight: 48).padding(.vertical, 4)
            .accessibilityElement(children: .ignore).accessibilityLabel(label).accessibilityValue(shown)
            .accessibilityIdentifier(identifier).accessibilityAction { edit() }
            .accessibilityAdjustableAction { direction in
                let next: Float
                switch direction {
                case .increment: next = min(upper, value + 1)
                case .decrement: next = max(lower, value - 1)
                @unknown default: return
                }
                beginGesture(); onValue(next); endGesture()
            }
    }

    private var animatorsFace: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 8) {
                LayerAnimatorSection()
                if model.selectedLayer?.kind == 4 || text3D { TextAnimationSection(showAnimatorEffect: false) }
            }.padding(.leading, 8).padding(.trailing, 12).padding(.top, 6).padding(.bottom, 16)
        }
    }

    private func scalarRow(_ label: String, amount: Float, speed: Float, limit: Float, keyframe: KeyframeLook = .none,
                           onValue: @escaping (Float) -> Void, onTap: @escaping () -> Void) -> some View {
        HStack(spacing: 8) {
            ZStack(alignment: .topLeading) {
                Text(label).font(.aurea(size: 12, weight: .semibold)).foregroundStyle(AureaColors.accent).underline()
                    .multilineTextAlignment(.center).lineLimit(2).frame(width: 94, height: 32)
                    .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                if keyframe != .none {
                    TransformMiniDiamond(filled: keyframe == .keyHere).frame(width: 7, height: 7).offset(x: 3, y: 3)
                }
            }
            TickRuler(value: { amount }, unitsPerDp: speed, active: true, height: 40)
                .frame(maxWidth: .infinity)
                .valueDrag(enabled: true, start: { amount }, unitsPerDp: { speed }, min: 0, max: limit,
                           onStart: beginGesture, onValue: onValue, onEnd: endGesture)
            ValueBox(number(amount, decimals: 0) + "%", width: 68, onTap: onTap)
        }.frame(height: 48)
    }
    private func blurToggle(_ title: String, hint: String, checked: Bool, onChange: @escaping (Bool) -> Void) -> some View {
        HStack(spacing: 10) {
            VStack(alignment: .leading, spacing: 0) {
                Text(AureaText.t(title)).font(.aurea(size: 14, weight: .semibold))
                Text(AureaText.t(hint)).font(.aurea(size: 11.5)).foregroundStyle(AureaColors.muted)
            }.frame(maxWidth: .infinity, alignment: .leading)
            AureaToggle(checked: checked, onCheckedChange: onChange)
        }.padding(.vertical, 6).padding(.leading, 4)
    }

    // =========================================================================
    // Campos (0.3.4 painéis: `PanelMetrics` — 72 × 36, número 15 que encolhe, rótulo 12)
    // =========================================================================

    private func pillField(_ text: String, label: String, tint: Color, corners: TransformPillCorners = .all,
                           description: String, identifier: String, onLongPress: (() -> Void)? = nil,
                           onTap: @escaping () -> Void) -> some View {
        let isLocked = locked
        // Bug 6b (0.3.5): o toque é um `Button` (o toque mais confiável do SwiftUI,
        // também dentro de ScrollView e com o arrasto simultâneo do trackpad); antes
        // era `LongPressGesture.exclusively(before: TapGesture)`, que perdia toques.
        return TransformPillButton(
            // Arrasto do trackpad que começou depois de o dedo pousar no campo: não é toque.
            dragged: { start in (padBeganAt ?? .distantPast) >= start },
            onLongPress: { if !isLocked { (onLongPress ?? onTap)() } },
            onTap: { if !isLocked { onTap() } }
        ) {
            VStack(spacing: PanelMetrics.fieldLabelGap) {
                Text(text).font(.aurea(size: PanelMetrics.fieldValue, weight: .semibold).monospacedDigit())
                    .foregroundStyle(isLocked ? AureaColors.muted : tint)
                    .lineLimit(1).minimumScaleFactor(PanelMetrics.fieldValueMin / PanelMetrics.fieldValue).padding(.horizontal, 4)
                    .frame(width: PanelMetrics.fieldWidth)
                    .frame(minHeight: PanelMetrics.fieldHeight)
                    .background(TransformPillShape(corners: corners).fill(AureaColors.chip))
                Text(label).font(.aurea(size: PanelMetrics.fieldLabel)).foregroundStyle(AureaColors.muted)
                    .lineLimit(1).minimumScaleFactor(0.7).frame(width: PanelMetrics.fieldWidth)
            }
            .frame(width: PanelMetrics.fieldWidth)
            .frame(minHeight: PanelMetrics.touch, alignment: .top)
            .contentShape(Rectangle())
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(description)
        .accessibilityValue(text)
        .accessibilityAddTraits(.isButton)
        .accessibilityAction { if !isLocked { onTap() } }
        .accessibilityIdentifier(identifier)
    }

    // =========================================================================
    // ⋯ — redefinir por aba + tudo, ações de sempre e os extras do AUREA
    // =========================================================================

    private func moreEntries() -> [MenuEntry] {
        var entries: [MenuEntry] = []
        // Redefinir a aba (ids `transform.reset.<move|pivot|rotate|scale|skew>`) e tudo.
        switch tab {
        case 0, 3: entries.append(.item(AureaText.t("om_ap_tf_reset_position"), enabled: !locked, tag: "transform.reset.move") { resetFace { resetPosition() } })
        case 4: entries.append(.item(AureaText.t("om_ap_tf_reset_pivot"), enabled: !locked, tag: "transform.reset.pivot") { resetFace { resetPivot() } })
        case 2: entries.append(.item(AureaText.t("om_ap_tf_reset_rotation"), enabled: !locked, tag: "transform.reset.rotate") { resetFace { resetRotation() } })
        case 1: entries.append(.item(AureaText.t("om_ap_tf_reset_scale"), enabled: !locked, tag: "transform.reset.scale") { resetFace { resetScale() } })
        case 8: entries.append(.item(AureaText.t("om_ap_tf_reset_skew"), enabled: !locked, tag: "transform.reset.skew") { resetFace { resetSkew() } })
        default: break
        }
        entries.append(.item(AureaText.t("om_ap_tf_reset_all"), enabled: !locked, tag: "transform.reset.all") { resetAll() })
        entries.append(.divider)
        entries.append(.item(AureaText.t("transform_copy"), glyph: CupertinoGlyph.DocOnDoc) {
            if model.engine.copyTransform(id) { model.toast = AureaText.t("transform_copied") }
        })
        if model.engine.clipboardState & 16 != 0 {
            entries.append(.item(AureaText.t("transform_paste"), glyph: CupertinoGlyph.DocOnClipboard, enabled: !locked) {
                let targets = model.selection.isEmpty ? [NSNumber(value: id)] : model.selection.map { NSNumber(value: $0) }
                if model.engine.pasteTransform(targets) > 0 {
                    model.refreshModel(force: true); model.toast = AureaText.t("transform_pasted")
                }
            })
        }
        if tab == 2 {
            // Voltas (como o AE mostra "2x +30°"): o resto fica.
            let property = UInt32(6 + rotationAxis), split = rotationTurns(value(property))
            let turns = AureaText.t("ios_rotation_axis", ["X", "Y", "Z"][rotationAxis]) + " · " + AureaText.t("edt_rotation_turns")
            entries.append(.item(turns, enabled: !locked) {
                keypad(turns, Float(split.turns), unit: "x", decimals: 0) { write([property: $0.rounded() * 360 + split.rest]) }
            })
        }
        entries.append(.item(AureaText.t("panel_keyframe_anterior")) { model.engine.run { $0.pause() }; _ = model.stepToKeyframe(-1) })
        entries.append(.item(AureaText.t("panel_proximo_keyframe")) { model.engine.run { $0.pause() }; _ = model.stepToKeyframe(1) })
        if !exprProps.isEmpty {
            entries.append(.item(AureaText.t(expressionLook == .none ? "panel_adicionar_expressao" : "panel_editar_expressao")) { openExpression() })
        }
        let intrinsic3D = [8, 9, 10].contains(model.selectedLayer?.kind ?? 0)
        if tab == 2 && !threeD {
            // O antigo botão "Girar em 3D" da face (2D): liga X/Y/Z na camada.
            entries.append(.item(AureaText.t("panel_girar_3d_x_y"), glyph: CupertinoGlyph.Cube, enabled: !locked,
                                 tag: "transform.enable3d") { model.enableLayer3D() })
        } else if model.selectedLayer?.threeD == false && !intrinsic3D {
            entries.append(.item(AureaText.t("panel_mostrar_x_y_z_3d"), glyph: CupertinoGlyph.Cube, enabled: !locked) { model.enableLayer3D() })
        }
        if tab == 1 && !isCamera && canFit {
            // Preencher / Ajustar / Tamanho da composição (saíram da face Escala).
            entries.append(.divider)
            entries.append(contentsOf: fitEntries)
        }
        if tab == 0 || tab == 3 {
            entries.append(.divider)
            entries.append(.item(AureaText.t("om_ap_tf_snap"), checked: snapOn, keepOpen: true, tag: "transform.more.snap") { snapOn.toggle() })
            entries.append(.item(AureaText.t("om_ap_tf_accelerate"), checked: accelerate, keepOpen: true, tag: "transform.more.accelerate") {
                accelerate.toggle()
            })
        }
        // Mais do AUREA: as faces extras e, na câmera, o painel das opções da lente.
        entries.append(.divider)
        entries.append(.item(AureaText.t("panel_desfoque_movimento"), glyph: CupertinoGlyph.MotionBlur, selected: tab == 5,
                             tag: "transform.tab.motionblur") { endGesture(); tab = 5 })
        entries.append(.item(AureaText.t("la_animators"), glyph: CupertinoGlyph.Sparkles, selected: tab == 6,
                             tag: "transform.tab.animators") { endGesture(); tab = 6 })
        if isCamera {
            entries.append(.item(AureaText.t("om_ap_cam_title"), glyph: CupertinoGlyph.CameraViewfinder, tag: "transform.camera") {
                endGesture(); model.panel = .camera
            })
        }
        return entries
    }

    /// Redefinir UMA aba: um passo de desfazer.
    private func resetFace(_ body: () -> Void) {
        guard !locked else { return }
        let wasOpen = gestureOpen
        if !wasOpen { beginGesture() }
        body()
        if !wasOpen { endGesture() }
    }
    private func resetPosition() {
        let cx = Float(model.compositionWidth) / 2, cy = Float(model.compositionHeight) / 2
        write(threeD ? [0: cx, 1: cy, 2: 0] : [0: cx, 1: cy])
    }
    private func resetPivot() {
        write(threeD ? [9: sourceSize[0] / 2, 10: sourceSize[1] / 2, 11: 0] : [9: sourceSize[0] / 2, 10: sourceSize[1] / 2])
    }
    private func resetRotation() { write(threeD ? [6: 0, 7: 0, 8: 0] : [8: 0]) }
    private func resetScale() {
        if isCamera { model.setCameraLens(AureaModel.cameraLensFocal, value: CameraLensMath.defaultFocalMm); return }
        write(threeD ? [3: 1, 4: 1, 5: 1] : [3: 1, 4: 1])
    }
    private func resetSkew() { writeSkew([13: 0, 14: 0]) }
    /// Redefinir transformação: tudo num passo de desfazer.
    private func resetAll() {
        guard !locked else { return }
        let wasOpen = gestureOpen
        if !wasOpen { beginGesture() }
        resetPosition(); resetPivot(); resetRotation()
        if !isCamera { resetScale(); resetSkew() }
        if !wasOpen { endGesture() }
    }

    // =========================================================================
    // Keyframe, curva, expressão, escrita
    // =========================================================================

    private func lensLook(_ param: UInt32) -> KeyframeLook {
        guard let property = model.cameraLensTrack(param) else { return .none }
        let own: [KeyframeItem] = (model.keyframes[id] ?? []).filter { $0.property == property && $0.effectIndex == UInt32.max }
        if own.contains(where: { $0.time == model.localPlayhead }) { return .keyHere }
        return own.isEmpty ? .none : .animated
    }
    private func openCurve() {
        guard let first = curveKeys.first else { return }
        let local = model.localPlayhead
        let segment = curveKeys.dropLast().last { $0.time <= local } ?? curveKeys[0]
        model.openCurve(property: first.property, effect: first.effectIndex, param: first.paramIndex, time: segment.time)
    }
    private func openExpression() {
        guard !exprProps.isEmpty else { return }
        let title: String
        switch tab {
        case 2: title = AureaText.t("ios_rotation_axis", ["X", "Y", "Z"][rotationAxis])
        case 1: title = AureaText.t("om_ap_tf_scale")
        case 4: title = AureaText.t("om_ap_tf_pivot")
        default: title = AureaText.t("om_ap_tf_move")
        }
        model.expressionSheet = ExpressionRequest(layer: id, label: title,
            tracks: exprProps.map { ExpressionTrack(property: $0) },
            scale: tab == 1 ? 100 : 1,
            unit: tab == 1 ? "%" : tab == 2 ? "°" : "px")
    }
    private func toggleKey() {
        if let track = animatorTrack {
            guard !locked else { return }
            model.engine.toggleLayerAnimKey(id, index: track.effect, param: track.param)
            model.refreshModel(force: true); return
        }
        if cameraScale { guard !locked else { return }; model.toggleCameraLensKey(AureaModel.cameraLensFocal); return }
        guard !locked, !keyProps.isEmpty else { return }
        let present = keyProps.filter { property in
            (model.keyframes[id] ?? []).contains { $0.property == property && $0.effectIndex == UInt32.max && $0.time == model.localPlayhead }
        }.count
        let remove = keyProps.count == 3 ? present == 3 : present > 0
        let snapshot = keyProps.map { ($0, value($0)) }, local = model.localPlayhead
        model.mutate { core in
            core.beginUndoGroup()
            for (property, current) in snapshot {
                if remove { core.deleteKeyframe(forLayer: id, property: property, time: local) }
                else { core.insertKeyframe(forLayer: id, property: property, time: local, value: current) }
            }
            core.endUndoGroup()
        }
        model.refreshModel(force: true)
    }
    /// Segurar ◆: apaga os keyframes das propriedades da aba num passo de desfazer.
    private func deleteAllKeys() {
        guard !locked else { return }
        let properties: [UInt32]
        if cameraScale, let lens = model.cameraLensTrack(AureaModel.cameraLensFocal) { properties = [lens] } else { properties = keyProps }
        let keys = (model.keyframes[id] ?? []).filter { properties.contains($0.property) && $0.effectIndex == UInt32.max }
        guard !keys.isEmpty else { return }
        let layer = id
        model.beginGesture("apagar keyframes")
        model.mutate { core in for key in keys { core.deleteKeyframe(forLayer: layer, property: key.property, time: key.time) } }
        model.endGesture()
    }
    /// A 3D vector is keyed as XYZ; unchanged axes retain their evaluated value.
    private func write(_ changes: [UInt32: Float]) {
        guard !locked, !changes.isEmpty, changes.values.allSatisfy(\.isFinite) else { return }
        if changes.keys.contains(where: { $0 == 13 || $0 == 14 }) { writeSkew(changes); return }
        if gestureOpen {
            guard gestureProject == model.projectGeneration, gestureLayer == id,
                  gestureComposition == (model.composition[AureaCompositionId] as? NSNumber)?.uint64Value else { return }
        }
        let properties = changes.keys.sorted()
        let base = properties[0] / 3 * 3
        let grouped = threeD && properties[0] < 12 && properties.allSatisfy { $0 >= base && $0 < base + 3 }
        let group: [UInt32] = grouped && model.autoKeyTransforms ? [base, base + 1, base + 2] : properties
        let keyed = group.contains { animatedMask & (1 << $0) != 0 }
        let keyHere = group.contains { keyHereMask & (1 << $0) != 0 }
        let snapshot = group.map { ($0, changes[$0] ?? value($0)) }, local = model.localPlayhead
        model.mutate { core in
            if !gestureOpen { core.beginUndoGroup() }
            // Auto-Key + trilha animada grava keyframe também na cena 3D (regra do modelo).
            if model.transformLayout(animated: keyed, keyHere: keyHere) {
                for (property, amount) in snapshot { core.layoutTransform(id, property: property, value: amount) }
            } else if keyed {
                for (property, amount) in snapshot { core.insertKeyframe(forLayer: id, property: property, time: local, value: amount) }
            } else {
                let base = Int(properties[0] / 3)
                if properties[0] == 12 { core.setOpacity(forLayer: id, value: changes[12] ?? value(12)) }
                else {
                    var vector = (0..<3).map { value(UInt32(base * 3 + $0)) }
                    for (property, amount) in changes { vector[Int(property % 3)] = amount }
                    switch base {
                    case 0: core.setPosition(forLayer: id, x: vector[0], y: vector[1], z: vector[2])
                    case 1: core.setScale(forLayer: id, x: vector[0], y: vector[1], z: vector[2])
                    case 2: core.setRotation(forLayer: id, x: vector[0], y: vector[1], z: vector[2])
                    case 3: core.setAnchor(forLayer: id, x: vector[0], y: vector[1], z: vector[2])
                    default: break
                    }
                }
            }
            if !gestureOpen { core.endUndoGroup() }
        }
        model.refreshModel(force: true)
    }
    private func beginGesture() {
        guard !gestureOpen else { return }
        // Pin edits to a single timeline instant throughout the drag.
        model.engine.run { $0.pause(); $0.beginUndoGroup() }; gestureOpen = true
        gestureProject = model.projectGeneration; gestureLayer = id
        gestureComposition = (model.composition[AureaCompositionId] as? NSNumber)?.uint64Value
        if expressionControlsTransform { model.toast = AureaText.t("transform_expression_controlled") }
    }
    private func endGesture() {
        if gestureOpen { model.engine.run { $0.endUndoGroup() }; gestureOpen = false }
        gestureProject = nil; gestureLayer = nil; gestureComposition = nil
        gestureValues = []; pivotDrag = nil
        rulerSnapIndex = -1
        // As guias do ímã só existem durante o arrasto do trackpad.
        if model.snapGuideX != nil { model.snapGuideX = nil }
        if model.snapGuideY != nil { model.snapGuideY = nil }
    }
    private func keypad(_ title: String, _ value: Float, unit: String, min: Float = -.infinity, max: Float = .infinity,
                        decimals: Int, onValue: @escaping (Float) -> Void) {
        let project = model.projectGeneration, layer = id
        let composition = (model.composition[AureaCompositionId] as? NSNumber)?.uint64Value
        model.numericKeypad = KeypadRequest(title: title, value: value, unit: unit, min: min, max: max, decimals: decimals) { amount in
            guard model.projectGeneration == project, model.primarySelection == layer,
                  (model.composition[AureaCompositionId] as? NSNumber)?.uint64Value == composition else { return }
            onValue(amount)
        }
    }
    private func number(_ value: Float, decimals: Int) -> String { numeroPtBr(value, casas: decimals) }
}

// =============================================================================
// Peças do painel
// =============================================================================

/// Um arrasto do trackpad: o dedo acumulado por eixo (o eixo "grudento" do
/// ímã), os alvos/guias, a velocidade amortecida e o ganho (unidades por pt).
struct TransformPadState {
    /// Largura do patamar do ímã em pixels físicos de dedo (25 px na referência).
    static let plateauPixels: Float = 25
    /// Toque curto (toque-empurrão e toque da roda): o `tapTimeout` do Android.
    static let tapTime: TimeInterval = 0.1
    /// Bug 6b (0.3.5): o prazo do toque longo (o `longPressTimeout` do Android):
    /// até aqui o toque parado na roda abre o teclado; segurar um campo digita.
    static let holdTime: TimeInterval = 0.5
    /// Toque-empurrão: o dedo anda menos que isto (pt).
    static let tapSlop: CGFloat = 20
    var finger = SIMD3<Float>(0, 0, 0)
    var last = CGSize.zero
    var lastTime = Date()
    var speed: Float = 0
    var gain: Float = 1
    var plateau: Float = 0
    var targetsX: [NSNumber] = []
    var guidesX: [Float] = []
    var targetsY: [NSNumber] = []
    var guidesY: [Float] = []
    var activeX = -1
    var activeY = -1
}

/// Bug 6b (0.3.5): o toque e o "segurar" de um campo da pílula (X/Y/Z, eixos da
/// roda, Largura/Altura, Inclinação). O toque é um `Button` — dispara ao soltar,
/// funciona dentro de ScrollView e convive com o arrasto simultâneo do trackpad.
/// O "segurar" sai do próprio aperto do botão (`isPressed` do estilo): 0,45 s
/// apertado dispara `onLongPress`, e o soltar seguinte não repete como toque.
/// `dragged(início)`: o trackpad começou um arrasto depois de o dedo pousar aqui —
/// então nem toque nem "segurar" (arrastar começando no campo move a camada).
private struct TransformPillButton<Label: View>: View {
    /// O "segurar" dos campos (o mesmo 0,45 s de antes).
    static var holdTime: TimeInterval { 0.45 }
    let dragged: (Date) -> Bool
    let onLongPress: () -> Void
    let onTap: () -> Void
    let label: Label
    @State private var pressStart: Date?
    @State private var holdTask: DispatchWorkItem?
    @State private var longFired = false

    init(dragged: @escaping (Date) -> Bool, onLongPress: @escaping () -> Void, onTap: @escaping () -> Void,
         @ViewBuilder label: () -> Label) {
        self.dragged = dragged; self.onLongPress = onLongPress; self.onTap = onTap; self.label = label()
    }

    var body: some View {
        Button {
            // Sem aperto visto (toque rapidíssimo): nada a descontar.
            let start = pressStart ?? .distantFuture
            let skip = longFired || dragged(start)
            longFired = false; pressStart = nil
            if !skip { onTap() }
        } label: { label }
        .buttonStyle(TransformPillPressStyle { pressed in
            holdTask?.cancel(); holdTask = nil
            guard pressed else {
                // O soltar chega antes ou junto da ação; um aperto cancelado (o dedo
                // saiu) não pode deixar um início velho para o próximo toque.
                let ended = pressStart
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { if pressStart == ended { pressStart = nil } }
                return
            }
            let start = Date()
            pressStart = start
            longFired = false
            let task = DispatchWorkItem {
                guard !dragged(start) else { return }
                longFired = true
                onLongPress()
            }
            holdTask = task
            DispatchQueue.main.asyncAfter(deadline: .now() + Self.holdTime, execute: task)
        })
    }
}

/// Sem efeito visual (o campo continua igual); só avisa quando o aperto começa e termina.
private struct TransformPillPressStyle: ButtonStyle {
    let onPressed: (Bool) -> Void
    func makeBody(configuration: Configuration) -> some View {
        configuration.label.onChange(of: configuration.isPressed) { onPressed($0) }
    }
}

/// Quais cantos da pílula arredondam (X|Y: só os de fora).
enum TransformPillCorners { case all, leading, trailing, square }

/// Retângulo com raio 5 só nos cantos pedidos (o `UnevenRoundedRectangle` é do iOS 17).
struct TransformPillShape: Shape {
    var corners: TransformPillCorners = .all
    var radius: CGFloat = PanelMetrics.fieldRadius
    func path(in rect: CGRect) -> Path {
        let r = min(radius, rect.height / 2, rect.width / 2)
        let left: CGFloat = corners == .all || corners == .leading ? r : 0
        let right: CGFloat = corners == .all || corners == .trailing ? r : 0
        var p = Path()
        p.move(to: CGPoint(x: rect.minX + left, y: rect.minY))
        p.addLine(to: CGPoint(x: rect.maxX - right, y: rect.minY))
        if right > 0 {
            p.addArc(center: CGPoint(x: rect.maxX - right, y: rect.minY + right), radius: right,
                     startAngle: .degrees(-90), endAngle: .degrees(0), clockwise: false)
        }
        p.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY - right))
        if right > 0 {
            p.addArc(center: CGPoint(x: rect.maxX - right, y: rect.maxY - right), radius: right,
                     startAngle: .degrees(0), endAngle: .degrees(90), clockwise: false)
        }
        p.addLine(to: CGPoint(x: rect.minX + left, y: rect.maxY))
        if left > 0 {
            p.addArc(center: CGPoint(x: rect.minX + left, y: rect.maxY - left), radius: left,
                     startAngle: .degrees(90), endAngle: .degrees(180), clockwise: false)
        }
        p.addLine(to: CGPoint(x: rect.minX, y: rect.minY + left))
        if left > 0 {
            p.addArc(center: CGPoint(x: rect.minX + left, y: rect.minY + left), radius: left,
                     startAngle: .degrees(180), endAngle: .degrees(270), clockwise: false)
        }
        p.closeSubpath()
        return p
    }
}

/// Marcas por cima de uma `TickRuler`: encaixes (tamanhos das outras camadas)
/// e a linha do zero. Mesma conta da régua: o valor `t` cai em
/// `centro + (t − valor) / unidadesPorPt`.
struct TransformRulerMarks: View {
    let value: Float
    let unitsPerDp: Float
    let marks: [Float]
    let zero: Bool
    var body: some View {
        Canvas { context, size in
            let center = size.width / 2
            let per = unitsPerDp > 0 && unitsPerDp.isFinite ? unitsPerDp : 1
            let v = value.isFinite ? value : 0
            if zero {
                let x = center + CGFloat((0 - v) / per)
                if x >= 0 && x <= size.width && abs(x - center) > 1 {
                    var line = Path()
                    line.move(to: CGPoint(x: x, y: 6)); line.addLine(to: CGPoint(x: x, y: size.height - 6))
                    context.stroke(line, with: .color(AureaColors.muted), lineWidth: 1.5)
                }
            }
            for mark in marks {
                let x = center + CGFloat((mark - abs(v)) / per) * (v < 0 ? -1 : 1)
                guard x >= 0 && x <= size.width else { continue }
                var line = Path()
                line.move(to: CGPoint(x: x, y: 8)); line.addLine(to: CGPoint(x: x, y: size.height - 8))
                context.stroke(line, with: .color(AureaColors.fxAxisZ), style: StrokeStyle(lineWidth: 2, lineCap: .round))
            }
        }
        .allowsHitTesting(false)
    }
}

/// Ids próprios das abas por cima da coluna direita do `PanelFrame` (que só
/// marca `panel.tab.<i>`); par do `PanelTabTags` do TransformPanel.kt.
/// Contêineres vazios e sem toque: o toque e o VoiceOver continuam na aba de
/// baixo; o id serve aos testes de UI (`transform.tab.*`, `camera.tab.*`).
struct PanelTabTags: View {
    let tags: [String]
    var body: some View {
        if tags.count >= 2 {
            HStack(spacing: 0) {
                Spacer(minLength: 0)
                VStack(spacing: 0) {
                    ForEach(Array(tags.prefix(PanelFrameMetrics.maxTabs).enumerated()), id: \.offset) { _, tag in
                        Color.clear
                            .frame(maxWidth: .infinity, maxHeight: .infinity)
                            .accessibilityElement(children: .contain)
                            .accessibilityIdentifier(tag)
                    }
                }
                .frame(width: PanelFrameMetrics.railWidth)
                .frame(maxHeight: .infinity)
            }
            .allowsHitTesting(false)
        }
    }
}

/// Android CornerMarks: four open L corners, inset 8, arm 26, round stroke2.
private struct TransformCornerMarks: View {
    var body: some View {
        Canvas { context, size in
            let l: CGFloat = 8, r = size.width - 8, t: CGFloat = 8, b = size.height - 8
            guard r > l, b > t else { return }
            let arm = min(26, min((r - l) / 2, (b - t) / 2))
            var path = Path()
            path.move(to: CGPoint(x: l + arm, y: t)); path.addLine(to: CGPoint(x: l, y: t)); path.addLine(to: CGPoint(x: l, y: t + arm))
            path.move(to: CGPoint(x: r - arm, y: t)); path.addLine(to: CGPoint(x: r, y: t)); path.addLine(to: CGPoint(x: r, y: t + arm))
            path.move(to: CGPoint(x: l + arm, y: b)); path.addLine(to: CGPoint(x: l, y: b)); path.addLine(to: CGPoint(x: l, y: b - arm))
            path.move(to: CGPoint(x: r - arm, y: b)); path.addLine(to: CGPoint(x: r, y: b)); path.addLine(to: CGPoint(x: r, y: b - arm))
            context.stroke(path, with: .color(AureaColors.muted.opacity(0.45)), style: StrokeStyle(lineWidth: 2, lineCap: .round, lineJoin: .round))
        }
        .allowsHitTesting(false)
    }
}

/// Rotação em voltas + resto, como o AE mostra ("2x +30°"): as voltas
/// truncam para o zero e o resto tem o MESMO sinal (−405° = "-1x -45°").
/// Conta em décimos de grau para 719,99° não virar "1x +360°". Igual ao
/// `rotationTurns` do Android.
func rotationTurns(_ angle: Float) -> (turns: Int, rest: Float) {
    let tenths = Int64((Double(angle) * 10).rounded())
    let turns = tenths / 3600
    return (Int(turns), Float(Double(tenths - turns * 3600) / 10))
}

/// A RODA DE ROTAÇÃO (RotateView da referência; par do `RotationDial.kt`):
/// trilha de 2,5, alça de 35 com sombra, arco do excedente acima de ±360°,
/// centro com o ângulo dentro da volta (`%.1f°`, fundo `campo`) e "N×" acima.
/// Giro RELATIVO ao ângulo do dedo em volta do centro; o ímã de 45° vem do
/// motor (`snap`); toque curto chama `onTap` (teclado). Um gesto = `onBegin`…`onEnd`.
struct TransformDial: View {
    let angle: Float
    let enabled: Bool
    let snap: (Float) -> (value: Float, snapped: Bool)
    let onBegin: () -> Void
    let onValue: (Float) -> Void
    let onEnd: () -> Void
    let onTap: () -> Void

    /// Limite prático da roda (a referência: ±3.600.000°).
    static let limit: Float = 3_600_000
    static let knob: CGFloat = 35
    static let track: CGFloat = 2.5
    /// Bug 6b (0.3.5): o toque parado que abre o teclado vale até o prazo do toque
    /// longo (0,5 s), não os 100 ms do toque-empurrão — um toque de dedo dura
    /// 100–200 ms e a roda "não respondia". Par do `isDialTap` do RotationDial.kt.
    static let tapTime: TimeInterval = TransformPadState.holdTime

    @State private var touching = false
    @State private var moved = false
    @State private var accum: Float = 0
    @State private var lastAngle: Double = 0
    @State private var startTime = Date()
    @State private var wasSnapped = false
    /// O valor do gesto (mostra na hora, antes de o motor responder).
    @State private var live: Float?

    init(angle: Float, enabled: Bool = true,
         snap: @escaping (Float) -> (value: Float, snapped: Bool) = { ($0, false) },
         onBegin: @escaping () -> Void = {}, onValue: @escaping (Float) -> Void = { _ in },
         onEnd: @escaping () -> Void = {}, onTap: @escaping () -> Void = {}) {
        self.angle = angle; self.enabled = enabled; self.snap = snap
        self.onBegin = onBegin; self.onValue = onValue; self.onEnd = onEnd; self.onTap = onTap
    }

    var body: some View {
        GeometryReader { geometry in
            let size = geometry.size
            let center = CGPoint(x: size.width / 2, y: size.height / 2)
            let radius = max(0, min(size.width, size.height) / 2 - 2 - TransformDial.knob / 2)
            let current = live ?? angle
            let finite = current.isFinite ? current : 0
            let turns = Int((finite / 360).rounded(.towardZero))
            let within = finite - Float(turns) * 360
            ZStack {
                Canvas { context, _ in
                    guard radius > 0 else { return }
                    let rect = CGRect(x: center.x - radius, y: center.y - radius, width: radius * 2, height: radius * 2)
                    context.stroke(Path(ellipseIn: rect), with: .color(AureaColors.dialTrack), lineWidth: TransformDial.track)
                    if turns != 0 && abs(within) > 0.05 {
                        // Arco do excedente: a volta em andamento depois de ±360°.
                        var arc = Path()
                        arc.addArc(center: center, radius: radius, startAngle: .degrees(-90),
                                   endAngle: .degrees(-90 + Double(within)), clockwise: within < 0)
                        context.stroke(arc, with: .color(AureaColors.accent), style: StrokeStyle(lineWidth: 3.5, lineCap: .round))
                    }
                    let radians = Double(finite) * .pi / 180
                    let knob = CGPoint(x: center.x + CGFloat(sin(radians)) * radius, y: center.y - CGFloat(cos(radians)) * radius)
                    let half = TransformDial.knob / 2
                    let disk = Path(ellipseIn: CGRect(x: knob.x - half, y: knob.y - half, width: half * 2, height: half * 2))
                    context.drawLayer { layer in
                        layer.addFilter(.shadow(color: AureaColors.scrim, radius: 4))
                        layer.fill(disk, with: .color(AureaColors.playhead))
                    }
                }
                VStack(spacing: 2) {
                    if turns != 0 {
                        Text("\(turns)×").font(.aurea(size: max(9, radius * 0.2), weight: .semibold).monospacedDigit())
                            .foregroundStyle(AureaColors.muted)
                    }
                    Text(numeroPtBr(within, casas: 1) + "°")
                        .font(.aurea(size: max(11, min(28, radius * 0.4 * 0.6)), weight: .semibold).monospacedDigit())
                        .foregroundStyle(enabled ? AureaColors.accent : AureaColors.muted)
                        .lineLimit(1).minimumScaleFactor(0.6)
                        .padding(.horizontal, 8).padding(.vertical, 3)
                        .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 5))
                }
                .keepLtr()
                .frame(maxWidth: max(40, radius * 1.4))
                .position(center)
                .allowsHitTesting(false)
            }
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { event in step(event, center: center) }
                .onEnded { event in finish(event) })
        }
    }

    private func degrees(_ point: CGPoint, _ center: CGPoint) -> Double {
        // 0° no topo, positivo no sentido horário (y para baixo).
        atan2(Double(point.x - center.x), Double(center.y - point.y)) * 180 / .pi
    }

    private func step(_ event: DragGesture.Value, center: CGPoint) {
        guard enabled else { return }
        let a = degrees(event.location, center)
        if !touching {
            touching = true; moved = false; startTime = event.time
            lastAngle = a; accum = angle.isFinite ? angle : 0; wasSnapped = false
            return
        }
        if !moved {
            guard hypot(event.translation.width, event.translation.height) >= 6 else { return }
            moved = true
            onBegin()
        }
        var delta = a - lastAngle
        while delta > 180 { delta -= 360 }
        while delta < -180 { delta += 360 }
        lastAngle = a
        // Perto do centro o ângulo do dedo não presta.
        guard hypot(event.location.x - center.x, event.location.y - center.y) > 8 else { return }
        accum = min(TransformDial.limit, max(-TransformDial.limit, accum + Float(delta)))
        let out = snap(accum)
        if out.snapped && !wasSnapped { UIImpactFeedbackGenerator(style: .light).impactOccurred() }
        wasSnapped = out.snapped
        live = out.value
        onValue(out.value)
    }

    private func finish(_ event: DragGesture.Value) {
        defer { touching = false; moved = false; live = nil }
        guard enabled, touching else { return }
        if moved { onEnd() }
        else if event.time.timeIntervalSince(startTime) < TransformDial.tapTime { onTap() }
    }
}

private struct TransformMiniDiamond: View {
    let filled: Bool
    var body: some View {
        Canvas { context, size in
            var path = Path()
            path.move(to: CGPoint(x: size.width / 2, y: 0)); path.addLine(to: CGPoint(x: size.width, y: size.height / 2))
            path.addLine(to: CGPoint(x: size.width / 2, y: size.height)); path.addLine(to: CGPoint(x: 0, y: size.height / 2)); path.closeSubpath()
            if filled { context.fill(path, with: .color(AureaColors.keyframe)) }
            else { context.stroke(path, with: .color(AureaColors.keyframe), lineWidth: 1) }
        }
    }
}

/// 3 × 3 pontos da caixa da mídia (âncora predefinida); o toque escolhe a célula.
struct AnchorPresetGrid: View {
    let onPick: (Float, Float) -> Void
    var body: some View {
        GeometryReader { geo in
            Canvas { context, size in
                for i in 0..<3 { for j in 0..<3 {
                    let r: CGFloat = (i == 1 && j == 1) ? 2.2 : 1.6
                    let c = CGPoint(x: size.width * (CGFloat(i) + 0.5) / 3, y: size.height * (CGFloat(j) + 0.5) / 3)
                    context.fill(Path(ellipseIn: CGRect(x: c.x - r, y: c.y - r, width: 2 * r, height: 2 * r)), with: .color(.white))
                } }
            }
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0).onEnded { e in
                let cx = min(2, max(0, Int(e.location.x / max(1, geo.size.width) * 3)))
                let cy = min(2, max(0, Int(e.location.y / max(1, geo.size.height) * 3)))
                onPick(Float(cx) * 0.5, Float(cy) * 0.5)
            })
        }
        // 0.3.4 painéis: 40 (antes 30), par do Android (campo de 36 + 4).
        .frame(width: PanelMetrics.fieldHeight + 4, height: PanelMetrics.fieldHeight + 4)
        .background(AureaColors.controlButton, in: RoundedRectangle(cornerRadius: PanelMetrics.fieldRadius))
        .accessibilityLabel(AureaText.t("panel_ancora_predefinida"))
    }
}
