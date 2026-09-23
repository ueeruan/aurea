// =============================================================================
//  Aurea / platform / ios / app / Panel3DView.swift
//
//  O painel 3D: a cena, o ambiente e o que é de cada OBJETO.
//
//  A regra do ambiente por objeto (Fase 9, contrato v22) é a mesma do Android:
//  um modelo escolhe entre o ambiente do PROJETO (0) e o DELE (1), com HDRI,
//  intensidade, giro e exposição próprios. Mexer aqui NÃO toca nos outros
//  objetos — e é isso que a UI precisa deixar claro, por isso os dois blocos
//  aparecem separados.
// =============================================================================
import SwiftUI
import UniformTypeIdentifiers

struct Panel3DView: View {
    @EnvironmentObject private var model: AureaModel
    @State private var importingHdri = false
    @State private var environmentIntensity: Float = 1
    @State private var environmentRotation: Float = 0
    @State private var objectSource: UInt32 = 0
    @State private var objectIntensity: Float = 1
    @State private var objectRotation: Float = 0
    @State private var objectExposure: Float = 1

    private var layerId: Int64 { model.primarySelection ?? 0 }

    /// O id do empacotamento da cena: a composição atual carrega a cena 3D.
    private var sceneId: UInt64 {
        (model.composition["id"] as? NSNumber)?.uint64Value ?? 0
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 4) {
                if model.selectedLayer == nil {
                    Text(AureaText.t("editor_toque_num_objeto_tela_editar"))
                        .font(AureaType.body)
                        .foregroundStyle(AureaColors.subtle)
                        .padding(AureaDims.pad)
                } else {
                    // --- Ambiente do PROJETO --------------------------------
                    AureaSectionHeader(title: AureaText.t("panel_do_projeto"))
                    AureaPropertyRow(title: AureaText.t("panel_intensidade")) {
                        Slider(value: Binding(get: { environmentIntensity },
                                              set: { newValue in
                                                  environmentIntensity = newValue
                                                  _ = model.engine.setEnvironmentIntensity(newValue,
                                                                                            rotation: environmentRotation)
                                              }),
                               in: 0...4)
                            .tint(AureaColors.accent)
                        Text(String(format: "%.2f", environmentIntensity))
                            .font(AureaType.value).foregroundStyle(AureaColors.muted)
                    }
                    AureaPropertyRow(title: "Giro") {
                        Slider(value: Binding(get: { environmentRotation },
                                              set: { newValue in
                                                  environmentRotation = newValue
                                                  _ = model.engine.setEnvironmentIntensity(environmentIntensity,
                                                                                            rotation: newValue)
                                              }),
                               in: 0...360)
                            .tint(AureaColors.accent)
                        Text(String(format: "%.0f°", environmentRotation))
                            .font(AureaType.value).foregroundStyle(AureaColors.muted)
                    }
                    HStack(spacing: 10) {
                        Button {
                            importingHdri = true
                        } label: {
                            Label(AureaText.t("panel_trocar_imagem"), systemImage: "photo")
                                .font(AureaType.label)
                                .foregroundStyle(AureaColors.accent)
                        }
                        .buttonStyle(.plain)
                        Button {
                            model.engine.clearHdri()
                            model.toast = AureaText.t("panel_estudio_neutro")
                        } label: {
                            Label(AureaText.t("panel_estudio_neutro"), systemImage: "circle.slash")
                                .font(AureaType.label)
                                .foregroundStyle(AureaColors.muted)
                        }
                        .buttonStyle(.plain)
                    }
                    .padding(.horizontal, AureaDims.pad)

                    Divider().background(AureaColors.hairline).padding(.vertical, 6)

                    // --- Ambiente DESTE objeto ------------------------------
                    AureaSectionHeader(title: AureaText.t("panel_ambiente_do_objeto"))
                    AureaPropertyRow(title: AureaText.t("panel_alinhamento")) {
                        Picker("", selection: Binding(get: { Int(objectSource) },
                                                      set: { newValue in
                                                          objectSource = UInt32(newValue)
                                                          applyObjectEnvironment()
                                                      })) {
                            Text(AureaText.t("panel_do_projeto")).tag(0)
                            Text(AureaText.t("panel_proprio")).tag(1)
                        }
                        .pickerStyle(.segmented)
                    }
                    AureaPropertyRow(title: AureaText.t("panel_intensidade")) {
                        Slider(value: Binding(get: { objectIntensity },
                                              set: { objectIntensity = $0; applyObjectEnvironment() }),
                               in: 0...4)
                            .tint(AureaColors.accent)
                    }
                    AureaPropertyRow(title: AureaText.t("panel_exposicao")) {
                        Slider(value: Binding(get: { objectExposure },
                                              set: { objectExposure = $0; applyObjectEnvironment() }),
                               in: 0...4)
                            .tint(AureaColors.accent)
                    }
                    AureaPropertyRow(title: "Giro") {
                        Slider(value: Binding(get: { objectRotation },
                                              set: { objectRotation = $0; applyObjectEnvironment() }),
                               in: 0...360)
                            .tint(AureaColors.accent)
                    }

                    Divider().background(AureaColors.hairline).padding(.vertical, 6)

                    // --- Material -------------------------------------------
                    AureaSectionHeader(title: AureaText.t("panel_material"))
                    // O motor AINDA não aceita editar material por comando
                    // (`SceneSetMaterialParam` responde NotImplemented — ver
                    // Engine.cpp). Em vez de mostrar sliders que não fazem nada,
                    // o painel diz o que é verdade: a cor, o brilho, o metálico
                    // e a rugosidade vêm do ARQUIVO. Um controle morto na tela é
                    // pior do que um controle ausente.
                    Text(AureaText.t("panel_cor_brilho_metalico_rugosidade_vem_arquivo"))
                        .font(AureaType.body)
                        .foregroundStyle(AureaColors.subtle)
                        .padding(.horizontal, AureaDims.pad)
                        .padding(.top, 2)

                    HStack(spacing: 10) {
                        Button {
                            model.engine.setCameraTrack(1, for: layerId)
                            model.toast = "analise de camera iniciada"
                        } label: {
                            Label(AureaText.t("editor_rastreio"), systemImage: "viewfinder")
                                .font(AureaType.label)
                                .foregroundStyle(AureaColors.accent)
                        }
                        .buttonStyle(.plain)
                    }
                    .padding(.horizontal, AureaDims.pad)
                    .padding(.top, 8)
                }
            }
            .padding(.bottom, 18)
        }
        .fileImporter(isPresented: $importingHdri,
                      allowedContentTypes: [UTType(filenameExtension: "hdr") ?? .data]) { result in
            guard case .success(let url) = result else { return }
            model.importMedia(url: url, kind: .hdri)
        }
        .onAppear { loadEnvironment() }
    }

    // =========================================================================
    private func loadEnvironment() {
        let environment = model.engine.environment()
        if environment.count >= 3 {
            environmentIntensity = environment[1].floatValue
            environmentRotation = environment[2].floatValue
        }
        guard model.selectedLayer != nil else { return }
        let object = model.engine.objectEnvironment(forLayer: layerId)
        if object.count >= 5 {
            objectSource = object[0].uint32Value
            objectIntensity = object[2].floatValue
            objectRotation = object[3].floatValue
            objectExposure = object[4].floatValue
        }
    }

    private func applyObjectEnvironment() {
        guard model.selectedLayer != nil else { return }
        // `hdri` 0 = o estúdio neutro DAQUELE objeto. A troca da imagem em si é
        // o `import_hdri`, que devolve o asset e é aplicado aqui.
        _ = model.engine.setObjectEnvironment(forLayer: layerId, source: objectSource, hdri: 0,
                                              intensity: objectIntensity, rotation: objectRotation,
                                              exposure: objectExposure)
    }

}
