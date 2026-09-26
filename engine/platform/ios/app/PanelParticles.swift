// Particle World: ten controls, two colors and three motion presets.
// Parameter IDs remain compatible with existing saved projects.
import SwiftUI

/// Índices de `ParticleParam` no motor. Nomes legíveis, números do contrato.
private enum PPP {
    static let emitterType = 0, emitterWidth = 1, emitterHeight = 2
    static let emitterRadius = 3, emitterRotation = 4, emitterDepth = 5
    static let gridX = 6, gridY = 7, emitFill = 8
    static let emitterOffsetX = 9, emitterOffsetY = 10
    static let rate = 11, burst = 12, lifetime = 13, lifeRandom = 14
    static let speed = 15, speedRandom = 16, direction = 17, spread = 18
    static let inheritVelocity = 19, seed = 20
    static let particleType = 21, softness = 22, rotation = 23
    static let rotationRandom = 24, spin = 25
    static let startSize = 26, endSize = 27, startOpacity = 28, endOpacity = 29
    static let gravityX = 30, gravityY = 31, gravityZ = 32, drag = 33
    static let windX = 34, windY = 35, turbulence = 36, turbulenceScale = 37
    static let turbulenceSpeed = 38, vortex = 39, attractor = 40
    static let trailLength = 41, trailTaper = 42
    static let auxCount = 43, auxAt = 44, auxLife = 45, auxSpeed = 46
    static let auxSize = 47, auxSpread = 48
    static let collision = 49, collisionY = 50, collisionBounce = 51
    static let blendMode = 52, maxParticles = 53
    // 8.2 (v21) — só no fim: o número é contrato.
    static let emitterSpace = 54, emitFrom = 55, auxProbability = 56
    static let trailWidth = 57, trailOpacity = 58
    static let sizeRandom = 59, opacityRandom = 60, colorRandom = 61
    static let collisionX = 62, collisionZ = 63, collisionRadius = 64
    static let collisionWidth = 65, collisionHeight = 66, collisionDepth = 67
    static let meshScale = 68, meshLit = 69
}

/// `TrackProperty::ParticleParam` — o número é contrato com o Kotlin e com os
/// projetos salvos.
private let particleProperty: UInt32 = 36

struct ParticlesPanel: View {
    @EnvironmentObject private var model: AureaModel
    @State private var values: [Float] = []
    @State private var curves: [[Float]] = [[], [], []]
    @State private var selected: Int?

    private var id: Int64 { model.primarySelection ?? 0 }

    var body: some View {
        VStack(spacing: 0) {
            PanelHeader(title: AureaText.t("panel_particulas"), onBack: { model.panel = .none })
            panelBody
        }
    }

    private var panelBody: some View {
        HStack(spacing: 0) {
            if values.isEmpty {
                PanelNotice(AureaText.t("panel_selecione_camada_particulas"))
            } else {
                LeftRail(keyframeLook: railLook,
                         onKeyframe: selected.map { param in { toggleKey(param) } },
                         curveAnimated: railTrack.count >= 2,
                         onCurve: railTrack.count >= 2 ? { openCurveAtPlayhead() } : nil,
                         onMore: nil,
                         onBack: { model.panel = .none })
                ScrollView {
                    VStack(alignment: .leading, spacing: 0) {
                        presetRow
                        Spacer(minLength: 10)
                        parameterGroups
                    }
                    .padding(.init(top: 12, leading: 18, bottom: 24, trailing: 18))
                }
            }
        }
        .onAppear(perform: load)
        .onChange(of: model.status.modelRevision) { _ in load() }
        .onChange(of: model.status.playhead) { _ in load() }
        .onChange(of: id) { _ in selected = nil; load() }
    }

    // =========================================================================
    @ViewBuilder private var parameterGroups: some View {
        if value(PPP.emitterType) < 10 {
            PanelNotice(AureaText.t("world_legacy"))
        } else {
        group(AureaText.t("particular_group_emitter"))
        dim(AureaText.t("particular_radius"), PPP.emitterRadius, 1, 0, 2000, " px")
        dim(AureaText.t("panel_particulas_segundo"), PPP.rate, 5, 0, 6000, "/s")
        dim(AureaText.t("panel_duracao_cada"), PPP.lifetime, 0.02, 0.05, 10, " s", decimals: 2)
        group(AureaText.t("particular_group_physics"))
        dim(AureaText.t("panel_velocidade"), PPP.speed, 2, 0, 4000, " px/s")
        dim(AureaText.t("panel_gravidade"), PPP.gravityY, 4, -4000, 4000, " px/s²", negate: true)
        dim(AureaText.t("world_resistance"), PPP.drag, 0.02, 0, 10, "", decimals: 2)
        group(AureaText.t("particular_group_particle"))
        choice(AureaText.t("particular_shape"), PPP.particleType,
               ["particular_shape_circle", "particular_shape_square", "particular_shape_streak", "particular_shape_soft"])
        dim(AureaText.t("panel_tamanho_inicial"), PPP.startSize, 0.25, 0.1, 120, " px", decimals: 1)
        dim(AureaText.t("panel_tamanho_final"), PPP.endSize, 0.25, 0, 120, " px", decimals: 1)
        percent(AureaText.t("panel_opacidade"), PPP.startOpacity)
        lifeGradient
        }
    }

    // =========================================================================
    // O trilho: a linha escolhida é o alvo do losango
    // =========================================================================
    private var railLook: KeyframeLook {
        guard let param = selected else { return .none }
        return look(param)
    }

    private var railTrack: [KeyframeItem] {
        guard let param = selected else { return [] }
        return (model.keyframes[id] ?? [])
            .filter { $0.property == particleProperty && $0.paramIndex == UInt32(param) }
            .sorted { $0.time < $1.time }
    }

    /// A MARCA QUE ABRE O TRECHO sob o cabeçote — a curva de um keyframe é a do
    /// trecho que começa nele.
    private func openCurveAtPlayhead() {
        let track = railTrack
        guard track.count >= 2 else { return }
        let playhead = model.localPlayhead
        let index = max(0, min(track.count - 2, track.lastIndex { $0.time <= playhead } ?? 0))
        model.openCurve(property: particleProperty, effect: UInt32.max,
                        param: UInt32(selected ?? 0), time: track[index].time)
    }

    private func look(_ param: Int) -> KeyframeLook {
        var animated = false
        for key in model.keyframes[id] ?? [] where key.property == particleProperty && key.paramIndex == UInt32(param) {
            if key.time == model.localPlayhead { return .keyHere }
            animated = true
        }
        return animated ? .animated : .none
    }

    private func keyed(_ param: Int) -> Bool {
        (model.keyframes[id] ?? []).contains { $0.property == particleProperty && $0.paramIndex == UInt32(param) }
    }

    /// Losango de keyframe de um parâmetro: no cabeçote apaga, fora dele grava.
    private func toggleKey(_ param: Int) {
        let here = (model.keyframes[id] ?? []).filter {
            $0.property == particleProperty && $0.paramIndex == UInt32(param) && $0.time == model.localPlayhead
        }
        model.mutate { engine in
            if here.isEmpty {
                engine.keyParameter(id, property: particleProperty, effect: UInt32.max, param: UInt32(param),
                                    time: model.localPlayhead, value: value(param))
            } else {
                for key in here {
                    engine.editTrackKey(id, property: particleProperty, effect: UInt32.max, param: UInt32(param),
                                        time: key.time, action: 1, value: 0, targetTime: key.time,
                                        interpolation: 0, handles: [])
                }
            }
        }
        model.refreshModel(force: true)
        load()
    }

    // =========================================================================
    // Linhas do painel
    // =========================================================================
    private var presetRow: some View {
        VStack(alignment: .leading, spacing: 5) {
            Text(AureaText.t("particular_presets"))
                .font(.aurea(size: 11.5))
                .foregroundStyle(AureaColors.muted)
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    ForEach(Array(presetKeys.enumerated()), id: \.offset) { entry in
                        Button { applyPreset(entry.offset + 10) } label: {
                            Text(AureaText.t(entry.element))
                                .font(.aurea(size: 12))
                                .foregroundStyle(AureaColors.text)
                                .padding(.horizontal, Aurca12)
                                .padding(.vertical, 6)
                                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
        }
    }

    private let presetKeys = ["world_explosive", "world_jet", "world_vortex"]

    private func group(_ title: String) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Spacer(minLength: 14)
            Text(title).font(.aurea(size: 11.5)).foregroundStyle(AureaColors.muted)
        }
    }

    /// Escolha em fichas — para enum, onde arrastar não faz sentido.
    private func choice(_ label: String, _ param: Int, _ options: [String]) -> some View {
        PropertyCustomRow(label, selected: selected == param, onSelect: { selected = param }, keyframe: look(param)) {
            ScrollView(.horizontal, showsIndicators: false) {
                // ParticlesPanel.Choice uses its own filled 7 dp chips, not
                // the shared, dimmed property-picker appearance.
                HStack(spacing: 5) {
                    ForEach(Array(options.enumerated()), id: \.offset) { index, key in
                        let on = index == Int(value(param).rounded())
                        Button { set(param, Float(index)) } label: {
                            Text(AureaText.t(key))
                                .font(.aurea(size: 11.5))
                                .foregroundStyle(on ? AureaColors.onAccent : AureaColors.text)
                                .padding(.horizontal, 10).padding(.vertical, 5)
                                .background(on ? AureaColors.accent : AureaColors.chip,
                                            in: RoundedRectangle(cornerRadius: 7))
                        }
                        .buttonStyle(.plain)
                        .accessibilityAddTraits(on ? .isSelected : [])
                    }
                }
            }
        }
    }


    /// Valor com unidade. `negate` inverte o SENTIDO do arrasto (gravidade para
    /// baixo é o natural, como no Android).
    private func dim(_ label: String, _ param: Int, _ unitsPerDp: Float, _ min: Float, _ max: Float,
                     _ unit: String, negate: Bool = false, decimals: Int = 0) -> some View {
        let shown = negate ? -value(param) : value(param)
        let text = decimals > 0 ? numeroPtBr(shown, casas: decimals) + unit : "\(Int(shown.rounded()))\(unit)"
        return ruler(label, param, text, unitsPerDp, min, max, negate: negate)
    }


    private func percent(_ label: String, _ param: Int, _ unitsPerDp: Float = 0.01) -> some View {
        ruler(label, param, "\(Int((value(param) * 100).rounded()))%", unitsPerDp, 0, 1)
    }

    private func ruler(_ label: String, _ param: Int, _ text: String,
                       _ unitsPerDp: Float, _ min: Float, _ max: Float, negate: Bool = false) -> some View {
        PropertyCustomRow(label, selected: selected == param, onSelect: { selected = param }, keyframe: look(param)) {
            HStack(spacing: 8) {
                TickRuler(value: { negate ? -value(param) : value(param) }, unitsPerDp: unitsPerDp, active: true)
                    .frame(maxWidth: .infinity)
                    .valueDrag(enabled: true, start: { negate ? -value(param) : value(param) }, unitsPerDp: { unitsPerDp },
                               min: min, max: max,
                               onStart: { model.engine.run { $0.beginUndoGroup() } },
                               onValue: { set(param, negate ? -$0 : $0) },
                               onEnd: { model.engine.run { $0.endUndoGroup() } })
                ValueBox(text)
            }
        }
    }

    /// Escolha de uma camada do projeto (fonte, imagem, modelo), com "Nenhuma".


    /// Cor ao longo da vida: até 8 paradas (posição + cor sRGB). Sem paradas vale
    /// o início → fim da partícula.
    private var lifeGradient: some View {
        let saved = curveStops(0, stride: 4)
        let stops: [[Float]] = saved.count >= 2 ? [saved[0], saved[saved.count-1]] :
            [[0, 1, 1, 0.3137255], [1, 0.7843137, 0.1568627, 0.1568627]]
        return HStack(spacing: 12) {
            ForEach(0..<2, id: \.self) { index in
                let stop = stops[index]
                ColorWell(color: Color(.sRGB, red: Double(stop[1]), green: Double(stop[2]), blue: Double(stop[3]), opacity: 1)) {
                    model.engine.run { $0.beginUndoGroup() }
                    model.colorSheet = ColorSheetRequest(title: AureaText.t(index == 0 ? "world_birth_color" : "world_death_color"), initial: [stop[1], stop[2], stop[3], 1], onChange: { r, g, b, _ in
                        var colors = stops
                        colors[index] = [Float(index), r, g, b]
                        writeCurve(0, colors.flatMap { $0 })
                    }, onDone: { model.engine.run { $0.endUndoGroup() } })
                }
                Text(AureaText.t(index == 0 ? "world_birth_color" : "world_death_color")).font(AureaType.tiny)
            }
        }.padding(.vertical, 12)
    }


    /// "+ Ponto" (até 8) e "voltar ao início → fim".


    /// Régua compacta de um ponto de curva (sem losango: a curva não é keyframe).


    // =========================================================================
    // Estado vindo do núcleo
    // =========================================================================
    private func value(_ param: Int) -> Float { param < values.count ? values[param] : 0 }

    private func load() {
        guard model.selectedLayer != nil else { values = []; return }
        values = model.engine.particleParams(id).map { $0.floatValue }
        curves = (0..<3).map { model.engine.particleCurve(id, kind: UInt32($0)).map { $0.floatValue } }
    }

    /// Com keyframe, o valor vai para o keyframe do playhead (como nos efeitos).
    private func set(_ param: Int, _ newValue: Float) {
        if keyed(param) {
            model.mutate {
                $0.keyParameter(id, property: particleProperty, effect: UInt32.max, param: UInt32(param),
                                time: model.localPlayhead, value: newValue)
            }
        } else {
            _ = model.engine.setParticle(id, param: UInt32(param), value: newValue)
        }
        model.refreshModel(force: true)
        load()
    }


    private func applyPreset(_ preset: Int) {
        _ = model.engine.applyParticlePreset(id, preset: UInt32(preset))
        model.refreshModel(force: true)
        load()
    }

    private func writeCurve(_ kind: Int, _ data: [Float]) {
        _ = model.engine.setParticleCurve(id, kind: UInt32(kind), values: data.map { NSNumber(value: $0) })
        model.refreshModel(force: true)
        load()
    }

    private func curveStops(_ kind: Int, stride: Int) -> [[Float]] {
        let data = kind < curves.count ? curves[kind] : []
        guard data.count >= stride else { return [] }
        return Swift.stride(from: 0, to: data.count - (data.count % stride), by: stride).map {
            Array(data[$0..<($0 + stride)])
        }
    }

    /// Mexe num ponto da curva e devolve a lista inteira ao núcleo.

}

/// O respiro à esquerda dos botões de preset (12 dp do Android).
private let Aurca12: CGFloat = 12
