// =============================================================================
//  Aurea / platform / ios / app / EffectsView.swift
//
//  O navegador de efeitos e o painel da pilha da camada.
//
//  A ORDEM É DO MOTOR: efeitos na ordem de aplicação (o primeiro é o que a
//  imagem atravessa primeiro), parâmetros na ordem da declaração do tipo. O
//  painel NÃO ordena nem filtra por conta própria — o que está na tela é o que
//  o `EffectGraph` do núcleo vai executar.
//
//  O rótulo de cada parâmetro vem do motor em pt/en e o `id` estável
//  ("blurriness") é o que permite traduzir a INTERFACE sem mexer na identidade
//  do efeito (é a regra da Fase 8.1, e vale igual aqui).
// =============================================================================
import SwiftUI
import UIKit

struct EffectsView: View {
    @EnvironmentObject private var model: AureaModel
    @State private var browsing = false
    @State private var search = ""

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Text(AureaText.t("panel_efeitos_camada"))
                    .font(AureaType.section)
                    .foregroundStyle(AureaColors.text)
                Spacer()
                Button {
                    browsing.toggle()
                } label: {
                    Label(AureaText.t("panel_adicionar_efeito"), systemImage: "plus")
                        .font(AureaType.label)
                        .foregroundStyle(AureaColors.accent)
                }
                .buttonStyle(.plain)
            }
            .padding(.horizontal, AureaDims.pad)
            .padding(.top, 6)

            if browsing {
                catalog
            } else {
                applied
            }
        }
        .onAppear { model.refreshSelectedLayer() }
    }

    // =========================================================================
    // Catálogo (adicionar)
    // =========================================================================
    private var catalog: some View {
        VStack(spacing: 0) {
            HStack(spacing: 8) {
                Image(systemName: "magnifyingglass").foregroundStyle(AureaColors.subtle)
                TextField(AureaText.t("common_search"), text: $search)
                    .textFieldStyle(.plain)
                    .foregroundStyle(AureaColors.text)
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 8)
            .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
            .padding(.horizontal, AureaDims.pad)
            .padding(.vertical, 6)

            ScrollView {
                LazyVStack(spacing: 6) {
                    ForEach(filteredCatalog) { item in
                        Button {
                            guard let layerId = model.primarySelection else { return }
                            // `index` >= tamanho da pilha = acrescenta no fim (é
                            // o contrato do EffectAdd no Engine.cpp).
                            model.mutate { $0.addEffect(item.typeId, toLayer: layerId, atIndex: UInt32.max) }
                            model.refreshSelectedLayer()
                            browsing = false
                        } label: {
                            HStack {
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(item.name)
                                        .font(AureaType.body)
                                        .foregroundStyle(AureaColors.text)
                                    if !item.category.isEmpty {
                                        Text(item.category)
                                            .font(AureaType.tiny)
                                            .foregroundStyle(AureaColors.subtle)
                                    }
                                }
                                Spacer()
                                Image(systemName: "plus.circle")
                                    .foregroundStyle(AureaColors.accent)
                            }
                            .padding(10)
                            .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                        }
                        .buttonStyle(.plain)
                    }
                }
                .padding(.horizontal, AureaDims.pad)
                .padding(.bottom, 12)
            }
        }
    }

    private var filteredCatalog: [EffectCatalogItem] {
        guard !search.isEmpty else { return model.effectCatalog }
        return model.effectCatalog.filter {
            $0.name.localizedCaseInsensitiveContains(search) ||
            $0.category.localizedCaseInsensitiveContains(search)
        }
    }

    // =========================================================================
    // Pilha aplicada + parâmetros
    // =========================================================================
    private var applied: some View {
        ScrollView {
            VStack(spacing: 4) {
                if model.effects.isEmpty {
                    Text(AureaText.t("panel_este_efeito_nao_tem_ajustes"))
                        .font(AureaType.body)
                        .foregroundStyle(AureaColors.subtle)
                        .padding(.top, 12)
                }

                ForEach(model.effects) { effect in
                    effectRow(effect)
                }

                if let effectId = model.selectedEffectId, !model.effectParams.isEmpty {
                    Divider().background(AureaColors.hairline)
                    ForEach(model.effectParams) { param in
                        paramRow(param, effectId: effectId)
                    }
                }
            }
            .padding(.bottom, 16)
        }
    }

    private func effectRow(_ effect: EffectItem) -> some View {
        VStack(spacing: 6) {
            HStack(spacing: 8) {
                Button {
                    guard let layerId = model.primarySelection else { return }
                    model.mutate { $0.setEffect(effect.effectId, forLayer: layerId, enabled: !effect.enabled) }
                    model.refreshSelectedLayer()
                } label: {
                    Image(systemName: effect.enabled ? "checkmark.circle.fill" : "circle")
                        .foregroundStyle(effect.enabled ? AureaColors.accent : AureaColors.subtle)
                }
                .buttonStyle(.plain)

                Button {
                    model.loadParams(layerId: model.primarySelection ?? 0, effectId: effect.effectId)
                } label: {
                    VStack(alignment: .leading, spacing: 1) {
                        Text(effect.name.isEmpty ? "?" : effect.name)
                            .font(AureaType.body)
                            .foregroundStyle(AureaColors.text)
                        if !effect.known {
                            // Tipo que esta versão não conhece: aparece e diz,
                            // nunca some sem explicação (o `known` do row).
                            Text(AureaText.t("panel_este_efeito_saiu_catalogo_ele_nao"))
                                .font(AureaType.tiny)
                                .foregroundStyle(AureaColors.warning)
                        }
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }
                .buttonStyle(.plain)

                Menu {
                    Button(AureaText.t("panel_mover_cima")) { move(effect, delta: -1) }
                    Button(AureaText.t("panel_mover_baixo")) { move(effect, delta: 1) }
                    Button(AureaText.t("panel_redefinir")) { reset(effect) }
                    Button(AureaText.t("panel_remover_efeito"), role: .destructive) { remove(effect) }
                } label: {
                    Image(systemName: "ellipsis").foregroundStyle(AureaColors.muted)
                }
            }
            .padding(.horizontal, AureaDims.pad)
            .padding(.vertical, 7)
        }
    }

    private func move(_ effect: EffectItem, delta: Int) {
        guard let layerId = model.primarySelection,
              let index = model.effects.firstIndex(where: { $0.effectId == effect.effectId }) else { return }
        let target = index + delta
        guard target >= 0, target < model.effects.count else { return }
        model.mutate { $0.moveEffect(effect.effectId, inLayer: layerId, toIndex: UInt32(target)) }
        model.refreshSelectedLayer()
    }

    private func remove(_ effect: EffectItem) {
        guard let layerId = model.primarySelection else { return }
        model.mutate { $0.removeEffect(effect.effectId, fromLayer: layerId) }
        if model.selectedEffectId == effect.effectId { model.selectedEffectId = nil }
        model.refreshSelectedLayer()
    }

    private func reset(_ effect: EffectItem) {
        guard let layerId = model.primarySelection else { return }
        // Redefinir = escrever o VALOR PADRÃO da declaração, um comando por
        // parâmetro (o motor não tem "reset" — e não deve ter: o padrão é do
        // tipo, não da instância).
        let params = model.effectParams
        model.mutate { engine in
            for param in params {
                engine.setEffect(effect.effectId, forLayer: layerId, paramIndex: param.index,
                                 value: param.defaultValue.first ?? 0)
            }
        }
        model.refreshSelectedLayer()
    }

    // =========================================================================
    // Um parâmetro
    // =========================================================================
    @ViewBuilder
    private func paramRow(_ param: EffectParamItem, effectId: UInt32) -> some View {
        // `ParamType` (effects/Parameter.hpp): 0 escalar, 1 ângulo, 2 cor,
        // 3 ponto 2D, 4 ponto 3D, 5 inteiro, 6 escolha, 7 liga/desliga.
        switch param.type {
        case 2:
            colorRow(param, effectId: effectId)
        case 5, 6:
            choiceRow(param, effectId: effectId)
        case 7:
            toggleRow(param, effectId: effectId)
        default:
            scalarRow(param, effectId: effectId)
        }
    }

    private func scalarRow(_ param: EffectParamItem, effectId: UInt32) -> some View {
        AureaPropertyRow(title: param.label) {
            Slider(value: Binding(get: { Double(param.scalar) },
                                  set: { newValue in
                                      guard let layerId = model.primarySelection else { return }
                                      model.engine.setEffect(effectId, forLayer: layerId,
                                                             paramIndex: param.index, value: Float(newValue))
                                      model.commitPendingCommands()
                                  }),
                   in: Double(param.minValue)...Double(max(param.minValue + 0.0001, param.maxValue)))
                .tint(AureaColors.accent)
            Text(String(format: "%.2f%@", param.scalar, param.unit.isEmpty ? "" : " \(param.unit)"))
                .font(AureaType.value)
                .foregroundStyle(AureaColors.muted)
                .frame(width: 74, alignment: .trailing)
            AureaKeyframeDiamond(on: param.animated) {
                guard let layerId = model.primarySelection else { return }
                model.engine.insertKeyframe(forLayer: layerId, effectIndex: effectId,
                                            paramIndex: param.index,
                                            time: Int32(model.status.playhead), value: param.scalar)
                model.refreshModel()
            }
        }
    }

    private func colorRow(_ param: EffectParamItem, effectId: UInt32) -> some View {
        AureaPropertyRow(title: param.label) {
            ColorPicker("", selection: Binding(
                get: { Color(.sRGB,
                             red: Double(param.value.count > 0 ? param.value[0] : 0),
                             green: Double(param.value.count > 1 ? param.value[1] : 0),
                             blue: Double(param.value.count > 2 ? param.value[2] : 0),
                             opacity: Double(param.value.count > 3 ? param.value[3] : 1)) },
                set: { color in
                    guard let layerId = model.primarySelection else { return }
                    let components = UIColor(color).cgColor.components ?? [0, 0, 0, 1]
                    model.engine.setEffectColor(effectId, forLayer: layerId, paramIndex: param.index,
                                                r: Float(components.count > 0 ? components[0] : 0),
                                                g: Float(components.count > 1 ? components[1] : 0),
                                                b: Float(components.count > 2 ? components[2] : 0),
                                                a: Float(components.count > 3 ? components[3] : 1))
                }), supportsOpacity: true)
            Spacer()
        }
    }

    private func choiceRow(_ param: EffectParamItem, effectId: UInt32) -> some View {
        AureaPropertyRow(title: param.label) {
            Picker("", selection: Binding(
                get: { Int(param.scalar) },
                set: { newValue in
                    guard let layerId = model.primarySelection else { return }
                    model.engine.setEffect(effectId, forLayer: layerId, paramIndex: param.index,
                                           value: Float(newValue))
                    model.commitPendingCommands()
                })) {
                ForEach(Array(param.enumLabels.enumerated()), id: \.offset) { index, label in
                    Text(label).tag(index)
                }
                if param.enumLabels.isEmpty {
                    Text(String(format: "%.0f", param.scalar)).tag(Int(param.scalar))
                }
            }
            .pickerStyle(.menu)
            .tint(AureaColors.accent)
        }
    }

    private func toggleRow(_ param: EffectParamItem, effectId: UInt32) -> some View {
        AureaPropertyRow(title: param.label) {
            Toggle("", isOn: Binding(
                get: { param.scalar >= 0.5 },
                set: { newValue in
                    guard let layerId = model.primarySelection else { return }
                    model.engine.setEffect(effectId, forLayer: layerId, paramIndex: param.index,
                                           value: newValue ? 1 : 0)
                    model.commitPendingCommands()
                }))
            .tint(AureaColors.accent)
            Spacer()
        }
    }
}
