import SwiftUI

/// Direct port of editor/panels/TransformPanel.kt. The six faces share the
/// same rail, keyframe groups and gesture units as the Android editor.
struct TransformView: View {
    @EnvironmentObject private var model: AureaModel
    @State private var tab = 0
    @State private var axis = 2
    @State private var linked = true
    @State private var expand3D = false
    @State private var zPicked = false
    @State private var gestureOpen = false
    @State private var gestureValues: [Float] = []
    @State private var previousAngle: Float?
    @State private var dialTotal: Float = 0
    @State private var dialWalked: CGFloat = 0
    @State private var dialLast: CGPoint?
    private let names = ["Posição", "Escala", "Rotação", "Opacidade", "Pivô", "Desfoque de movimento"]
    private let icons = ["material:rounded.OpenWith", "material:rounded.AspectRatio", "material:automirrored.rounded.RotateRight",
                         "material:rounded.Opacity", "material:rounded.FilterCenterFocus", "material:rounded.BlurOn"]
    private var id: Int64 { model.primarySelection ?? 0 }
    private var animatedMask: UInt32 { (model.detail["animatedMask"] as? NSNumber)?.uint32Value ?? 0 }
    private var keyMask: UInt32 { (model.detail["keyAtPlayhead"] as? NSNumber)?.uint32Value ?? 0 }
    private var timeFlags: UInt32 { (model.detail["timeFlags"] as? NSNumber)?.uint32Value ?? 0 }
    private var uses3D: Bool {
        let kind = model.selectedLayer?.kind ?? 0
        return [8, 9, 10].contains(kind) || model.selectedLayer?.threeD == true || abs(value(6)) > 0.01 || abs(value(7)) > 0.01
            || abs(value(2)) > 0.01 || [UInt32(6), 7, 2].contains { animatedMask & (1 << $0) != 0 }
    }
    private var threeD: Bool { expand3D || uses3D }
    private var rotationAxis: Int { threeD ? axis : 2 }
    private var props: [UInt32] {
        switch tab { case 0: return [0, 1]; case 1: return [3, 4]; case 2: return [UInt32(6 + rotationAxis)]
        case 3: return [12]; case 4: return [9, 10]; default: return [] }
    }
    private var keyProps: [UInt32] {
        if tab == 2 { return [6, 7, 8] }
        if threeD {
            switch tab { case 0: return [0, 1, 2]; case 1: return [3, 4, 5]; case 4: return [9, 10, 11]; default: break }
        }
        return props
    }
    private var look: KeyframeLook {
        if !keyProps.isEmpty && keyProps.allSatisfy({ keyMask & (1 << $0) != 0 }) { return .keyHere }
        return keyProps.contains { animatedMask & (1 << $0) != 0 } ? .animated : .none
    }
    private var curveKeys: [KeyframeItem] {
        guard let property = props.first else { return [] }
        return (model.keyframes[id] ?? []).filter { $0.property == property && $0.effectIndex == UInt32.max && $0.paramIndex == 0 }.sorted { $0.time < $1.time }
    }
    private var expressionLook: ExpressionLook {
        var result: ExpressionLook = .none
        for property in props {
            let info = model.engine.expression(id, property: property, effect: UInt32.max, param: 0)
            guard (info["exists"] as? NSNumber)?.boolValue == true else { continue }
            let on = (info["enabled"] as? NSNumber)?.boolValue ?? true
            if on && !(info["error"] as? String ?? "").isEmpty { return .error }
            if on { result = .ok } else if result == .none { result = .off }
        }
        return result
    }
    private func vector(_ key: String) -> [Float] { (model.detail[key] as? [NSNumber] ?? []).map(\.floatValue) }
    private func value(_ property: UInt32) -> Float {
        if property == 12 { return (model.detail["opacity"] as? NSNumber)?.floatValue ?? 1 }
        let group = Int(property / 3), axis = Int(property % 3)
        guard group < 4 else { return 0 }
        let data = vector(["position", "scale", "rotation", "anchor"][group])
        return data.indices.contains(axis) ? data[axis] : (group == 1 ? 1 : 0)
    }
    private var sourceSize: [Float] {
        let source = vector("sourceSize")
        return source.count >= 2 ? source : [0, 0]
    }

    var body: some View {
        VStack(spacing: 0) {
            PanelHeader(title: AureaText.t("panel_transformar") + " · " + names[tab]) { model.panel = .none }
            HStack(spacing: 0) {
                LeftRail(keyframeLook: look, onKeyframe: props.isEmpty ? nil : toggleKey,
                         curveAnimated: look != .none, onCurve: curveKeys.count >= 2 ? openCurve : nil,
                         onMore: openMenu, expression: expressionLook,
                         onExpression: props.isEmpty ? nil : openExpression,
                         onBack: { model.panel = .none })
                VStack(spacing: 0) {
                    switch tab {
                    case 0: moveFace(pivot: false)
                    case 1: scaleFace
                    case 2: rotationFace
                    case 3: opacityFace
                    case 4: moveFace(pivot: true)
                    default: motionBlurFace
                    }
                    Spacer().frame(height: 10)
                }.frame(maxWidth: .infinity, maxHeight: .infinity)
                RightRail(modes: icons, selected: tab) { index in endGesture(); tab = index }
            }
        }
        .foregroundStyle(AureaColors.text)
        .onAppear { updateTimelineFocus() }
        .onChange(of: keyProps) { _ in updateTimelineFocus() }
        .onDisappear { endGesture(); model.timelineFocus = nil }
    }

    private func updateTimelineFocus() {
        model.timelineFocus = keyProps.map { TimelineTrack(property: Int($0)) }
    }

    // Fields are part of the touch pad for Position, above it for Pivot.
    private func moveFace(pivot: Bool) -> some View {
        VStack(spacing: 0) {
            if pivot { moveFields(pivot: true).frame(height: 44) }
            GeometryReader { bounds in
                ZStack(alignment: .top) {
                    TransformCornerMarks()
                    Text(moveHint(pivot: pivot)).font(.aurea(size: 12))
                        .foregroundStyle(gestureOpen ? .clear : AureaColors.muted).multilineTextAlignment(.center)
                        .padding(.horizontal, 16).padding(.top, pivot ? 0 : 36)
                        .frame(width: bounds.size.width, height: bounds.size.height)
                }
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 8).onChanged { event in
                    if !gestureOpen {
                        gestureValues = pivot ? [value(9), value(10), value(2)] : [value(0), value(1), value(2)]
                        beginGesture()
                    }
                    guard gestureValues.count >= 3 else { return }
                    let gain = Float(max(1, model.compositionWidth)) / 360
                    let dx = Float(event.translation.width), dy = Float(event.translation.height)
                    if pivot { write([9: gestureValues[0] + dx * gain, 10: gestureValues[1] + dy * gain]); return }
                    if zPicked && threeD { write([2: gestureValues[2] - dy * gain]); return }
                    var x = gestureValues[0] + dx * gain, y = gestureValues[1] + dy * gain
                    if abs(dx) > 12 && abs(dy) < 6 { y = gestureValues[1] }
                    else if abs(dy) > 12 && abs(dx) < 6 { x = gestureValues[0] }
                    let cx = Float(model.compositionWidth) / 2, cy = Float(model.compositionHeight) / 2
                    if abs(x - cx) < 5 * gain { x = cx }; if abs(y - cy) < 5 * gain { y = cy }
                    write([0: x, 1: y])
                }.onEnded { _ in endGesture() })
                .overlay(alignment: .top) {
                    if !pivot { moveFields(pivot: false).padding(.top, 10).padding(.horizontal, 16) }
                }
            }
        }
    }
    private func moveFields(pivot: Bool) -> some View {
        let x = value(pivot ? 9 : 0) - (pivot ? sourceSize[0] / 2 : 0)
        let y = value(pivot ? 10 : 1) - (pivot ? sourceSize[1] / 2 : 0)
        return HStack(alignment: .top, spacing: 0) {
            field(number(x, decimals: 0) + "px", label: "x", width: 64) {
                keypad(pivot ? "Pivô em X" : "Posição X", x, unit: "px", decimals: 1) {
                    write([(pivot ? 9 : 0): $0 + (pivot ? sourceSize[0] / 2 : 0)])
                }
            }
            field(number(y, decimals: 0) + "px", label: "y", width: 64) {
                keypad(pivot ? "Pivô em Y" : "Posição Y", y, unit: "px", decimals: 1) {
                    write([(pivot ? 10 : 1): $0 + (pivot ? sourceSize[1] / 2 : 0)])
                }
            }.padding(.leading, 6)
            if pivot && threeD {
                field(number(value(11), decimals: 0) + "px", label: "z", width: 64) {
                    keypad("Pivô Z", value(11), unit: "px", decimals: 1) { write([11: $0]) }
                }.padding(.leading, 6)
            }
            if pivot {
                Button { write([9: sourceSize[0] / 2, 10: sourceSize[1] / 2]) } label: {
                    Text(AureaText.t("panel_centro")).font(.aurea(size: 12, weight: .semibold)).foregroundStyle(.white)
                        .padding(.horizontal, 14).frame(height: 30).background(AureaColors.controlButton, in: RoundedRectangle(cornerRadius: 8))
                }.buttonStyle(.plain).padding(.leading, 14)
            } else if threeD {
                field(number(value(2), decimals: 0) + "px", label: "z", width: 64,
                      color: zPicked ? AureaColors.accent : .white, onLongPress: {
                    keypad("Profundidade Z", value(2), unit: "px", decimals: 1) { write([2: $0]) }
                }) { zPicked.toggle() }.padding(.leading, 14)
            } else { Spacer().frame(width: 14) }
        }.frame(maxWidth: .infinity)
    }
    private func moveHint(pivot: Bool) -> String {
        AureaText.t(pivot ? "panel_deslize_ponto_giro_botao_centro_devolve"
            : zPicked && threeD ? "panel_deslize_ajustar_profundidade_toque_z_voltar"
            : threeD ? "panel_deslize_mover_toque_z_profundidade" : "panel_deslize_mover_camada")
    }

    private var rotationFace: some View {
        VStack(spacing: 0) {
            HStack(spacing: 6) {
                if threeD {
                    ForEach(0..<3) { index in
                        Button { axis = index } label: {
                            Text(["X", "Y", "Z"][index]).font(.aurea(size: 13, weight: .bold))
                                .foregroundStyle(axis == index ? AureaColors.accent : AureaColors.text)
                                .padding(.horizontal, 18).padding(.vertical, 6)
                                .background(axis == index ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                        }.buttonStyle(.plain)
                    }
                } else {
                    Button { expand3D = true } label: {
                        Text(AureaText.t("panel_girar_3d_x_y")).font(.aurea(size: 12.5, weight: .semibold))
                            .padding(.horizontal, 14).padding(.vertical, 6)
                            .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                    }.buttonStyle(.plain)
                }
            }.frame(height: 44).frame(maxWidth: .infinity).padding(.horizontal, 12)
            GeometryReader { bounds in
                let property = UInt32(6 + rotationAxis)
                let angle = value(property)
                let center = CGPoint(x: bounds.size.width / 2, y: bounds.size.height / 2)
                ZStack {
                    TransformDial(angle: angle)
                        .contentShape(Rectangle())
                        .gesture(DragGesture(minimumDistance: 0).onChanged { event in
                            if dialLast == nil {
                                dialLast = event.startLocation; dialWalked = 0; dialTotal = angle
                                previousAngle = dialRaw(event.startLocation, center: center)
                            }
                            if let last = dialLast { dialWalked += hypot(event.location.x - last.x, event.location.y - last.y) }
                            dialLast = event.location
                            guard let raw = dialRaw(event.location, center: center) else { previousAngle = nil; return }
                            defer { previousAngle = raw }
                            guard let before = previousAngle else { return }
                            var step = raw - before
                            if step > 180 { step -= 360 }; if step < -180 { step += 360 }
                            guard step != 0 else { return }
                            beginGesture(); dialTotal += step; write([property: dialTotal])
                        }.onEnded { event in
                            if gestureOpen { endGesture() }
                            else if dialWalked < 2, let raw = dialRaw(event.location, center: center) {
                                write([property: floor(angle / 360) * 360 + raw])
                            }
                            dialLast = nil; previousAngle = nil
                        })
                    Button {
                        keypad("Rotação \(["X", "Y", "Z"][rotationAxis])", angle, unit: "°", decimals: 1) { write([property: $0]) }
                    } label: {
                        Text(number(angle, decimals: (angle * 10).rounded() / 10 == angle.rounded() ? 0 : 1) + "°")
                            .font(.aurea(size: 24, weight: .bold).monospacedDigit()).foregroundStyle(AureaColors.accent)
                            .padding(.horizontal, 16).padding(.vertical, 8)
                            .background(AureaColors.dialValueBox, in: RoundedRectangle(cornerRadius: 8))
                    }.buttonStyle(.plain)
                }
            }
        }
    }
    private func dialRaw(_ point: CGPoint, center: CGPoint) -> Float? {
        guard hypot(point.x - center.x, point.y - center.y) >= 22 else { return nil }
        return Float(atan2(point.y - center.y, point.x - center.x) * 180 / .pi)
    }

    private var scaleFace: some View {
        VStack(spacing: 0) {
            HStack(spacing: 0) {
                field(number(value(3) * 100, decimals: 1) + "%", label: AureaText.t("panel_largura"), width: 61) {
                    keypad("Largura", value(3) * 100, unit: "%", decimals: 1) { scaleWrite(axisY: false, amount: $0, original: [value(3) * 100, value(4) * 100]) }
                }
                Button { linked.toggle() } label: {
                    MaterialGlyph(linked ? "rounded.Link" : "rounded.LinkOff", size: 16, color: .white)
                        .frame(width: 34, height: 24).background(AureaColors.controlButton, in: RoundedRectangle(cornerRadius: 8))
                }.buttonStyle(.plain).padding(.horizontal, 5)
                    .accessibilityLabel(AureaText.t(linked ? "panel_soltar_largura_altura" : "panel_travar_largura_altura"))
                field(number(value(4) * 100, decimals: 1) + "%", label: AureaText.t("panel_altura"), width: 61, color: .white) {
                    keypad("Altura", value(4) * 100, unit: "%", decimals: 1) { scaleWrite(axisY: true, amount: $0, original: [value(3) * 100, value(4) * 100]) }
                }
            }.frame(height: 44).frame(maxWidth: .infinity)
            if threeD {
                field(number(value(5) * 100, decimals: 1) + "%", label: "z", width: 80) {
                    keypad("Escala Z", value(5) * 100, unit: "%", decimals: 1) { write([5: $0 / 100]) }
                }.frame(height: 44)
            }
            if [UInt32(1), 2].contains(model.selectedLayer?.kind ?? 0), sourceSize[0] > 0, sourceSize[1] > 0 {
                fitChips.padding(.bottom, 6)
            }
            scaleTape(axisY: false)
            Spacer().frame(height: 8)
            scaleTape(axisY: true)
        }
    }
    private func scaleTape(axisY: Bool) -> some View {
        GeometryReader { bounds in
            TickRuler(value: { value(axisY ? 4 : 3) * 100 }, unitsPerDp: 0.5, active: !axisY, height: bounds.size.height)
                .valueDrag(enabled: true, start: { value(axisY ? 4 : 3) * 100 }, unitsPerDp: { 0.5 }, min: -.infinity, max: .infinity,
                           onStart: { gestureValues = [value(3) * 100, value(4) * 100]; beginGesture() },
                           onValue: { scaleWrite(axisY: axisY, amount: $0, original: gestureValues) }, onEnd: endGesture)
        }
    }
    private func scaleWrite(axisY: Bool, amount: Float, original: [Float]) {
        guard original.count >= 2 else { return }
        if linked {
            let from = original[axisY ? 1 : 0], multiplier = from != 0 ? amount / from : 1
            let x = axisY ? (from != 0 ? original[0] * multiplier : amount) : amount
            let y = axisY ? amount : (from != 0 ? original[1] * multiplier : amount)
            write([3: x / 100, 4: y / 100])
        } else { write([(axisY ? 4 : 3): amount / 100]) }
    }
    private var fitChips: some View {
        let ratios = [Float(model.compositionWidth) / max(1, sourceSize[0]), Float(model.compositionHeight) / max(1, sourceSize[1])]
        let choices = [max(ratios[0], ratios[1]), min(ratios[0], ratios[1])]
        return HStack(spacing: 8) {
            ForEach(0..<2) { index in
                let scale = choices[index], selected = abs(value(3) - scale) < 0.001 && abs(value(4) - scale) < 0.001
                Button {
                    beginGesture(); write([3: scale, 4: scale]); write([0: Float(model.compositionWidth) / 2, 1: Float(model.compositionHeight) / 2]); endGesture()
                } label: {
                    Text(AureaText.t(index == 0 ? "panel_preencher" : "panel_ajustar")).font(.aurea(size: 12, weight: .semibold))
                        .foregroundStyle(selected ? AureaColors.accent : AureaColors.text).padding(.horizontal, 14).frame(height: 30)
                        .background(selected ? AureaColors.accent.opacity(0.18) : AureaColors.railModeFill, in: Capsule())
                }.buttonStyle(.plain)
            }
        }.frame(maxWidth: .infinity)
    }

    private var opacityFace: some View {
        VStack(spacing: 6) {
            scalarRow(AureaText.t("panel_opacidade"), amount: value(12) * 100, speed: 0.35, limit: 100, keyframe: look,
                      onValue: { write([12: $0 / 100]) }, onTap: {
                keypad("Opacidade", value(12) * 100, unit: "%", min: 0, max: 100, decimals: 0) { write([12: $0 / 100]) }
            })
            GeometryReader { bounds in
                TickRuler(value: { value(12) * 100 }, unitsPerDp: 0.35, active: true, height: bounds.size.height)
                    .valueDrag(enabled: true, start: { value(12) * 100 }, unitsPerDp: { 0.35 }, min: 0, max: 100,
                               onStart: beginGesture, onValue: { write([12: $0 / 100]) }, onEnd: endGesture)
            }
        }.padding(.leading, 2).padding(.trailing, 10).padding(.top, 6)
    }
    private var motionBlurFace: some View {
        let on = timeFlags & 2 != 0 && model.compMotionBlur
        return ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                blurToggle("panel_desfoque_movimento", hint: "panel_borra_camada_direcao_ela_move", checked: on) { enabled in
                    beginGesture()
                    if enabled && !model.compMotionBlur { model.setCompositionMotionBlur(true) }
                    model.engine.setMotionBlur(enabled, forLayer: id); model.refreshModel(force: true); endGesture()
                }
                if on {
                    scalarRow(AureaText.t("panel_intensidade"), amount: model.shutterAngle / 3.6, speed: 0.5, limit: 200,
                              onValue: { model.changeShutterAngle($0 * 3.6) }, onTap: {
                        keypad("Intensidade do desfoque", model.shutterAngle / 3.6, unit: "%", min: 0, max: 200, decimals: 0) { model.changeShutterAngle($0 * 3.6) }
                    })
                    Text(AureaText.t("panel_intensidade_vale_todas_camadas_desfoque_neste")).font(.aurea(size: 11.5))
                        .foregroundStyle(AureaColors.muted).padding(.top, 4).padding(.leading, 4)
                }
                if model.selectedLayer?.kind == 1 {
                    blurToggle("panel_desfoque_movimento_video", hint: "panel_borra_mexe_dentro_video", checked: timeFlags & 32 != 0) { enabled in
                        model.engine.setVectorBlur(forLayer: id, amount: enabled ? 1 : 0); model.refreshModel(force: true)
                    }.padding(.top, 8)
                }
            }.padding(.leading, 8).padding(.trailing, 12).padding(.top, 6)
        }
    }

    private func field(_ text: String, label: String, width: CGFloat, color: Color = AureaColors.accent,
                       onLongPress: (() -> Void)? = nil, onTap: @escaping () -> Void) -> some View {
        VStack(spacing: 3) {
            Text(text).font(.aurea(size: 13, weight: .semibold).monospacedDigit()).foregroundStyle(color).underline()
                .lineLimit(1).minimumScaleFactor(0.62).padding(.horizontal, 4).frame(width: width, height: 24)
                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
            Text(label).font(.aurea(size: 9)).foregroundStyle(AureaColors.muted).lineLimit(1)
        }.frame(width: width).contentShape(Rectangle())
            .gesture(LongPressGesture(minimumDuration: 0.45).onEnded { _ in onLongPress?() }
                .exclusively(before: TapGesture().onEnded { _ in onTap() }))
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
            }.onLongPressGesture { if tab == 3 { openExpression() } }
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
    private func openMenu() {
        model.actionSheet = ActionSheetRequest(title: AureaText.t("panel_transformar"), items: menuActions.map { ($0.label, $0.action) })
    }
    private var menuActions: [HomeSheetAction] {
        var items = [
            HomeSheetAction(AureaText.t("panel_keyframe_anterior")) { model.engine.run { $0.pause() }; _ = model.stepToKeyframe(-1) },
            HomeSheetAction(AureaText.t("panel_proximo_keyframe")) { model.engine.run { $0.pause() }; _ = model.stepToKeyframe(1) },
        ]
        if tab != 5 { items.append(HomeSheetAction(AureaText.t("panel_voltar_padrao"), action: reset)) }
        if !props.isEmpty { items.append(HomeSheetAction(AureaText.t(expressionLook == .none ? "panel_adicionar_expressao" : "panel_editar_expressao"), action: openExpression)) }
        if !uses3D {
            items.append(HomeSheetAction(AureaText.t(expand3D ? "panel_esconder_x_y_z_3d" : "panel_mostrar_x_y_z_3d")) { expand3D.toggle() })
        }
        return items
    }
    private func reset() {
        switch tab {
        case 0: write([0: Float(model.compositionWidth) / 2, 1: Float(model.compositionHeight) / 2])
        case 1: write([3: 1, 4: 1])
        case 2: write([UInt32(6 + rotationAxis): 0])
        case 3: write([12: 1])
        case 4: write([9: sourceSize[0] / 2, 10: sourceSize[1] / 2])
        default: break
        }
    }
    private func openCurve() {
        guard let property = props.first, curveKeys.count >= 2 else { return }
        let local = model.localPlayhead
        let segment = curveKeys.last { $0.time <= local } ?? curveKeys[0]
        model.openCurve(property: property, time: segment.time)
    }
    private func openExpression() {
        guard !props.isEmpty else { return }
        let title = tab == 2 ? "Rotação \(["X", "Y", "Z"][rotationAxis])" : names[tab]
        model.expressionSheet = ExpressionRequest(layer: id, label: title,
            tracks: props.map { ExpressionTrack(property: $0) },
            scale: tab == 1 || tab == 3 ? 100 : 1,
            unit: tab == 1 || tab == 3 ? "%" : tab == 2 ? "°" : "px")
    }
    private func toggleKey() {
        guard !(model.selectedLayer?.locked ?? false), !keyProps.isEmpty else { return }
        let remove = keyProps.allSatisfy { property in
            (model.keyframes[id] ?? []).contains { $0.property == property && $0.effectIndex == UInt32.max && $0.time == model.localPlayhead }
        }
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
    /// Preserve grouped track editing from EditorStore.setTransform(2): if one
    /// member animates, all members are keyed at the same local playhead.
    private func write(_ changes: [UInt32: Float]) {
        guard !(model.selectedLayer?.locked ?? false), !changes.isEmpty, changes.values.allSatisfy(\.isFinite) else { return }
        let properties = changes.keys.sorted()
        let rotation = properties.contains { (6...8).contains($0) }
        let group: [UInt32] = rotation ? [6, 7, 8] : properties
        let keyed = group.contains { animatedMask & (1 << $0) != 0 }
        let snapshot = group.map { ($0, changes[$0] ?? value($0)) }, local = model.localPlayhead
        model.mutate { core in
            if !gestureOpen { core.beginUndoGroup() }
            if keyed {
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
        model.engine.run { $0.beginUndoGroup() }; gestureOpen = true
    }
    private func endGesture() {
        if gestureOpen { model.engine.run { $0.endUndoGroup() }; gestureOpen = false }
        gestureValues = []
    }
    private func keypad(_ title: String, _ value: Float, unit: String, min: Float = -.infinity, max: Float = .infinity,
                        decimals: Int, onValue: @escaping (Float) -> Void) {
        model.numericKeypad = KeypadRequest(title: title, value: value, unit: unit, min: min, max: max, decimals: decimals, onValue: onValue)
    }
    private func number(_ value: Float, decimals: Int) -> String { numeroPtBr(value, casas: decimals) }
}

/// Android CornerMarks: four open L corners, inset 12, arm 26, round stroke2.
private struct TransformCornerMarks: View {
    var body: some View {
        Canvas { context, size in
            let l: CGFloat = 12, r = size.width - 12, t: CGFloat = 12, b = size.height - 12
            guard r > l, b > t else { return }
            let arm = min(26, min((r - l) / 2, (b - t) / 2))
            var path = Path()
            path.move(to: CGPoint(x: l + arm, y: t)); path.addLine(to: CGPoint(x: l, y: t)); path.addLine(to: CGPoint(x: l, y: t + arm))
            path.move(to: CGPoint(x: r - arm, y: t)); path.addLine(to: CGPoint(x: r, y: t)); path.addLine(to: CGPoint(x: r, y: t + arm))
            path.move(to: CGPoint(x: l + arm, y: b)); path.addLine(to: CGPoint(x: l, y: b)); path.addLine(to: CGPoint(x: l, y: b - arm))
            path.move(to: CGPoint(x: r - arm, y: b)); path.addLine(to: CGPoint(x: r, y: b)); path.addLine(to: CGPoint(x: r, y: b - arm))
            context.stroke(path, with: .color(AureaColors.muted.opacity(0.45)), style: StrokeStyle(lineWidth: 2, lineCap: .round, lineJoin: .round))
        }
    }
}

private struct TransformDial: View {
    let angle: Float
    var body: some View {
        Canvas { context, size in
            let radius = max(0, (min(size.width, size.height) - 32) / 2)
            guard radius > 0 else { return }
            let center = CGPoint(x: size.width / 2, y: size.height / 2)
            let rect = CGRect(x: center.x - radius, y: center.y - radius, width: radius * 2, height: radius * 2)
            context.stroke(Path(ellipseIn: rect), with: .color(AureaColors.dialTrack), lineWidth: 2)
            let finite = angle.isFinite ? angle : 0
            if abs(finite) > 0.5 {
                let sweep = abs(finite) > 360 ? Float(360) : finite
                var arc = Path()
                arc.addArc(center: center, radius: radius, startAngle: .degrees(0), endAngle: .degrees(Double(sweep)), clockwise: sweep < 0)
                context.stroke(arc, with: .color(AureaColors.accent), style: StrokeStyle(lineWidth: 3.5, lineCap: .round))
            }
            let radians = Double(finite) * .pi / 180
            let knob = CGPoint(x: center.x + CGFloat(cos(radians)) * radius, y: center.y + CGFloat(sin(radians)) * radius)
            context.fill(Path(ellipseIn: CGRect(x: knob.x - 15, y: knob.y - 15, width: 30, height: 30)), with: .color(.white))
        }
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
