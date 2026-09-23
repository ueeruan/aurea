// =============================================================================
//  Aurea / platform / ios / app / TransformView.swift
//
//  O inspetor da camada: posição, escala, rotação, âncora, opacidade e
//  cisalhamento, com o losango de keyframe em cada linha.
//
//  DE ONDE VÊM OS NÚMEROS: do `LayerDetailPOD` do motor, avaliado NO PLAYHEAD
//  (a animação já aplicada). O painel não guarda estado próprio de valor — se
//  guardasse, um keyframe criado em outro lugar deixaria a tela mentindo. O que
//  ele guarda é só o TEXTO que está sendo digitado.
// =============================================================================
import SwiftUI

struct TransformView: View {
    @EnvironmentObject private var model: AureaModel
    @State private var showing3D = false

    private var detail: [String: Any] { model.detail }

    private func vector(_ key: String) -> SIMD3<Float> {
        guard let numbers = detail[key] as? [NSNumber] else { return .zero }
        var out = SIMD3<Float>.zero
        if numbers.count > 0 { out.x = numbers[0].floatValue }
        if numbers.count > 1 { out.y = numbers[1].floatValue }
        if numbers.count > 2 { out.z = numbers[2].floatValue }
        return out
    }

    private func scalar(_ key: String, _ fallback: Float = 0) -> Float {
        (detail[key] as? NSNumber)?.floatValue ?? fallback
    }

    private func animated(_ property: Int) -> Bool {
        let mask = (detail["animatedMask"] as? NSNumber)?.uint32Value ?? 0
        return (mask & (1 << UInt32(property))) != 0
    }

    private func keyedNow(_ property: Int) -> Bool {
        let mask = (detail["keyAtPlayhead"] as? NSNumber)?.uint32Value ?? 0
        return (mask & (1 << UInt32(property))) != 0
    }

    /// Título da camada escolhida + o que ela é.
    private var header: String {
        guard let layer = model.selectedLayer else { return AureaText.t("editor_nenhum") }
        return layer.name.isEmpty ? "Camada" : layer.name
    }

    var body: some View {
        ScrollView {
            VStack(spacing: 2) {
                HStack {
                    Text(header)
                        .font(AureaType.section)
                        .foregroundStyle(AureaColors.text)
                    Spacer()
                    Button {
                        showing3D.toggle()
                    } label: {
                        Text(AureaText.t(showing3D ? "panel_esconder_x_y_z_3d" : "panel_mostrar_x_y_z_3d"))
                            .font(AureaType.tiny)
                            .foregroundStyle(AureaColors.accent)
                    }
                    .buttonStyle(.plain)
                }
                .padding(.horizontal, AureaDims.pad)
                .padding(.top, 6)

                if model.selectedLayer == nil {
                    Text(AureaText.t("editor_nenhum"))
                        .font(AureaType.body)
                        .foregroundStyle(AureaColors.subtle)
                        .padding(.top, 14)
                } else {
                    vectorRow(AureaText.t("panel_transformar"), [(.positionX, "X"), (.positionY, "Y"), (.positionZ, "Z")],
                              values: vector("position"),
                              set: { axis, value in
                                  var v = vector("position")
                                  v[axis] = value
                                  model.engine.setPosition(forLayer: id, x: v.x, y: v.y, z: v.z)
                              })

                    vectorRow(AureaText.t("editor_escala"), [(.scaleX, "X"), (.scaleY, "Y"), (.scaleZ, "Z")],
                              values: vector("scale"),
                              set: { axis, value in
                                  var v = vector("scale")
                                  v[axis] = value
                                  model.engine.setScale(forLayer: id, x: v.x, y: v.y, z: v.z)
                              })

                    vectorRow(AureaText.t("editor_rotacao"), [(.rotationX, "X"), (.rotationY, "Y"), (.rotationZ, "Z")],
                              values: vector("rotation"),
                              set: { axis, value in
                                  var v = vector("rotation")
                                  v[axis] = value
                                  model.engine.setRotation(forLayer: id, x: v.x, y: v.y, z: v.z)
                              })

                    vectorRow(AureaText.t("panel_centro"), [(.anchorX, "X"), (.anchorY, "Y"), (.anchorZ, "Z")],
                              values: vector("anchor"),
                              set: { axis, value in
                                  var v = vector("anchor")
                                  v[axis] = value
                                  model.engine.setAnchor(forLayer: id, x: v.x, y: v.y, z: v.z)
                              })

                    scalarRow(AureaText.t("panel_opacidade"), .opacity, value: scalar("opacity", 1), range: 0...1) { value in
                        model.engine.setOpacity(forLayer: id, value: value)
                    }

                    scalarRow("Cisalhamento", .skewX, value: scalar("skew"), range: -89...89) { value in
                        model.engine.setSkew(forLayer: id, x: value, y: 0)
                    }

                    Divider().background(AureaColors.hairline).padding(.vertical, 4)

                    // --- Mesclagem, tipo e mistura ---------------------------
                    AureaPropertyRow(title: AureaText.t("editor_mesclagem_opacidade_efeitos_cores")) {
                        Picker("", selection: Binding(
                            get: { Int(model.selectedLayer?.blendMode ?? 0) },
                            set: { newValue in
                                model.mutate { $0.setLayer(id, blendMode: UInt32(newValue)) }
                                model.refreshModel()
                            })) {
                            ForEach(["Normal", "Adicionar", "Subtrair", "Multiplicar", "Tela", "Sobrepor",
                                     "Escurecer", "Clarear", "Subexpor", "Superexpor", "Luz intensa",
                                     "Luz suave", "Diferença", "Exclusão", "Matiz", "Saturação",
                                     "Cor", "Luminosidade"].indices, id: \.self) { index in
                                Text(blendName(index)).tag(index)
                            }
                        }
                        .pickerStyle(.menu)
                        .tint(AureaColors.accent)
                    }

                    flagRow(AureaText.t("editor_mostrar_camada"), on: model.selectedLayer?.visible ?? true) { on in
                        model.mutate { $0.setLayer(id, visible: on) }
                    }
                    flagRow(AureaText.t("editor_bloquear_camada"), on: model.selectedLayer?.locked ?? false) { on in
                        model.mutate { $0.setLayer(id, locked: on) }
                    }
                    flagRow(AureaText.t("editor_guia_nao_exporta"), on: model.selectedLayer?.guide ?? false) { on in
                        model.mutate { $0.setLayer(id, guide: on) }
                    }
                    flagRow(AureaText.t("editor_camada_ajuste"), on: model.selectedLayer?.adjustment ?? false) { on in
                        model.mutate { $0.setLayer(id, adjustment: on) }
                    }
                    flagRow(AureaText.t("editor_solo"), on: model.selectedLayer?.solo ?? false) { on in
                        model.mutate { $0.setLayer(id, solo: on) }
                    }

                    // Etiquetas de cor (a paleta do `LayerLabel`).
                    AureaPropertyRow(title: AureaText.t("editor_etiqueta")) {
                        HStack(spacing: 6) {
                            ForEach(Array(AureaColors.labelPalette.prefix(6).enumerated()), id: \.offset) { index, color in
                                Button {
                                    model.mutate { $0.setLayer(id, label: UInt32(index + 1)) }
                                    model.refreshModel()
                                } label: {
                                    Circle().fill(color)
                                        .frame(width: 18, height: 18)
                                        .overlay(Circle().stroke(AureaColors.text.opacity(0.25), lineWidth: 1))
                                }
                                .buttonStyle(.plain)
                            }
                            Button {
                                model.mutate { $0.setLayer(id, label: 0) }
                                model.refreshModel()
                            } label: {
                                Image(systemName: "nosign").foregroundStyle(AureaColors.subtle)
                            }
                            .buttonStyle(.plain)
                        }
                    }
                }
            }
            .padding(.bottom, 16)
        }
        .onAppear { model.refreshSelectedLayer() }
    }

    private var id: Int64 { model.primarySelection ?? 0 }

    private func blendName(_ index: Int) -> String {
        switch index {
        case 0: return "Normal"
        case 1: return "Adicionar"
        case 2: return "Subtrair"
        case 3: return "Multiplicar"
        case 4: return "Tela"
        default: return "Modo \(index)"
        }
    }

    // =========================================================================
    // Linhas
    // =========================================================================
    private func vectorRow(_ title: String,
                           _ axes: [(AureaTrackProperty, String)],
                           values: SIMD3<Float>,
                           set: @escaping (Int, Float) -> Void) -> some View {
        AureaPropertyRow(title: title) {
            HStack(spacing: 6) {
                ForEach(Array(axes.enumerated()), id: \.offset) { index, axis in
                    if index > 0 && !showing3D && index == 2 { EmptyView() } else {
                        HStack(spacing: 3) {
                            Text(axis.1)
                                .font(AureaType.tiny)
                                .foregroundStyle(AureaColors.subtle)
                            AureaNumberField(value: index == 0 ? values.x : (index == 1 ? values.y : values.z)) { newValue in
                                model.engine.beginBatch()
                                set(index, newValue)
                                _ = model.engine.flush()
                                model.refreshSelectedLayer()
                            }
                            .frame(width: 54)
                            AureaKeyframeDiamond(on: keyedNow(Int(axis.0.rawValue))) {
                                let value = index == 0 ? values.x : (index == 1 ? values.y : values.z)
                                model.mutate {
                                    $0.insertKeyframe(forLayer: id, property: axis.0.rawValue,
                                                      time: Int32(model.status.playhead), value: value)
                                }
                                model.refreshSelectedLayer()
                            }
                        }
                    }
                }
            }
        }
    }

    private func scalarRow(_ title: String, _ property: AureaTrackProperty, value: Float,
                           range: ClosedRange<Float>, set: @escaping (Float) -> Void) -> some View {
        AureaPropertyRow(title: title) {
            Slider(value: Binding(get: { value },
                                  set: { newValue in
                                      model.engine.beginBatch()
                                      set(newValue)
                                      model.commitPendingCommands()
                                  }),
                   in: range)
                .tint(AureaColors.accent)
            Text(String(format: "%.2f", value))
                .font(AureaType.value)
                .foregroundStyle(AureaColors.muted)
                .frame(width: 48, alignment: .trailing)
            AureaKeyframeDiamond(on: keyedNow(Int(property.rawValue))) {
                model.mutate {
                    $0.insertKeyframe(forLayer: id, property: property.rawValue,
                                      time: Int32(model.status.playhead), value: value)
                }
                model.refreshSelectedLayer()
            }
        }
    }

    private func flagRow(_ title: String, on: Bool, set: @escaping (Bool) -> Void) -> some View {
        AureaPropertyRow(title: title) {
            Toggle("", isOn: Binding(get: { on }, set: { newValue in
                set(newValue)
                model.refreshSelectedLayer()
            }))
            .tint(AureaColors.accent)
            Spacer()
        }
    }
}
