// =============================================================================
//  Aurea / platform / ios / app / PanelParticles.swift
//
//  AUREA PARTICULAR — o painel. Porte de `ParticlesPanel.kt`.
//
//  SEIS grupos, nesta ordem de propósito: Emissor (de onde sai), Emissão (com
//  que frequência), Partícula (o que sai), Ao longo da vida (as curvas), Física
//  (para onde vai), Aux (o que ela gera), Rastro, Renderização e Avançado (o que
//  quase ninguém mexe). Nada de cem controles numa tela só.
//
//  Os índices são `ParticleParam` no motor: o número é CONTRATO, não posição na
//  tela. Um controle que muda de lugar não muda de significado.
//
//  O trilho da esquerda escolhe a LINHA (o losango acende o keyframe do
//  parâmetro escolhido, como nos efeitos); a curva do trilho abre o editor de
//  easing no trecho sob o cabeçote. Nada de valor guardado aqui: tudo sai do
//  núcleo e volta por `setParticle` / `insertKeyframe`.
// =============================================================================
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
    @State private var links: [Int64] = []
    @State private var curves: [[Float]] = [[], [], []]
    @State private var advanced = false
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
        // --- Emissor: de onde sai ---------------------------------------------
        let emitter = Int(value(PPP.emitterType))
        group(AureaText.t("particular_group_emitter"))
        choice(AureaText.t("particular_emitter"), PPP.emitterType, [
            "particular_emitter_point", "particular_emitter_box", "particular_emitter_sphere",
            "particular_emitter_disc", "particular_emitter_line", "particular_emitter_grid",
            "particular_emitter_layer", "particular_emitter_text", "particular_emitter_path",
            "particular_emitter_mesh",
        ])
        switch emitter {
        case 1:
            dim(AureaText.t("particular_width"), PPP.emitterWidth, 1, 0, 4000, " px")
            dim(AureaText.t("particular_height"), PPP.emitterHeight, 1, 0, 4000, " px")
        case 2:
            dim(AureaText.t("particular_radius"), PPP.emitterRadius, 0.5, 0, 4000, " px")
            toggleRow(AureaText.t("particular_fill"), PPP.emitFill)
        case 3:
            dim(AureaText.t("particular_radius"), PPP.emitterRadius, 0.5, 0, 4000, " px")
        case 4:
            dim(AureaText.t("particular_length"), PPP.emitterWidth, 1, 0, 4000, " px")
            angle(AureaText.t("particular_rotation"), PPP.emitterRotation)
        case 5:
            dim(AureaText.t("particular_width"), PPP.emitterWidth, 1, 1, 4000, " px")
            dim(AureaText.t("particular_height"), PPP.emitterHeight, 1, 1, 4000, " px")
            dim(AureaText.t("particular_cols"), PPP.gridX, 0.1, 1, 64, "")
            dim(AureaText.t("particular_rows"), PPP.gridY, 0.1, 1, 64, "")
        default:
            if emitter >= 6 {
                // Quem pode emitir: cada tipo lê a fonte do seu jeito (ver
                // ParticleExtras.cpp). Sem fonte, sai da caixa do emissor.
                let candidates = model.layers.filter { row in
                    guard row.id != id else { return false }
                    switch emitter {
                    case 6: return row.kind == 2 || row.kind == 4 || row.kind == 5
                    case 7: return row.kind == 4
                    case 8: return row.maskCount > 0 || row.kind == 5 || row.kind == 4
                    default: return row.kind == 10
                    }
                }
                layerPick(AureaText.t("particular_source"), links.count > 0 ? links[0] : 0, candidates,
                          AureaText.t("particular_source_hint_none")) { setLink(0, $0) }
                choice(AureaText.t("particular_emit_from"), PPP.emitFrom,
                       ["particular_emit_vertices", "particular_emit_surface", "particular_emit_edges"])
            }
        }
        dim(AureaText.t("particular_offset_x"), PPP.emitterOffsetX, 1, -4000, 4000, " px")
        dim(AureaText.t("particular_offset_y"), PPP.emitterOffsetY, 1, -4000, 4000, " px")
        choice(AureaText.t("particular_space"), PPP.emitterSpace,
               ["particular_space_local", "particular_space_world"])

        // --- Emissão ----------------------------------------------------------
        group(AureaText.t("particular_group_emission"))
        dim(AureaText.t("panel_particulas_segundo"), PPP.rate, 0.2, 0, 5000, "/s")
        dim(AureaText.t("particular_burst"), PPP.burst, 0.2, 0, 20000, "")
        dim(AureaText.t("panel_duracao_cada"), PPP.lifetime, 0.02, 0.05, 60, " s")
        percent(AureaText.t("particular_life_random"), PPP.lifeRandom)
        dim(AureaText.t("panel_velocidade"), PPP.speed, 4, 0, 8000, " px/s")
        percent(AureaText.t("particular_speed_random"), PPP.speedRandom)
        angle(AureaText.t("panel_direcao"), PPP.direction, -360, 360)
        angle(AureaText.t("panel_abertura"), PPP.spread)
        percent(AureaText.t("particular_inherit"), PPP.inheritVelocity, 2)
        dim(AureaText.t("particular_seed"), PPP.seed, 0.2, 0, 100000, "")

        // --- Partícula: o que sai ---------------------------------------------
        let shape = Int(value(PPP.particleType))
        group(AureaText.t("particular_group_particle"))
        choice(AureaText.t("particular_shape"), PPP.particleType, [
            "particular_shape_circle", "particular_shape_square", "particular_shape_streak",
            "particular_shape_soft", "particular_shape_texture", "particular_shape_mesh",
        ])
        if shape == 4 {
            layerPick(AureaText.t("particular_texture_image"), links.count > 1 ? links[1] : 0,
                      model.layers.filter { $0.id != id && $0.kind == 2 },
                      AureaText.t("particular_texture_hint")) { setLink(1, $0) }
        }
        if shape == 5 {
            layerPick(AureaText.t("particular_mesh_model"), links.count > 2 ? links[2] : 0,
                      model.layers.filter { $0.id != id && $0.kind == 10 },
                      AureaText.t("particular_mesh_hint")) { setLink(2, $0) }
            dim(AureaText.t("particular_mesh_scale"), PPP.meshScale, 0.01, 0.01, 100, "x", decimals: 2)
            toggleRow(AureaText.t("particular_mesh_lit"), PPP.meshLit)
        }
        dim(AureaText.t("panel_tamanho_inicial"), PPP.startSize, 0.5, 0, 2000, " px")
        dim(AureaText.t("panel_tamanho_final"), PPP.endSize, 0.5, 0, 2000, " px")
        percent(AureaText.t("panel_opacidade_inicial"), PPP.startOpacity)
        percent(AureaText.t("panel_opacidade_final"), PPP.endOpacity)
        angle(AureaText.t("panel_rotacao"), PPP.rotation)
        angle(AureaText.t("particular_rotation_random"), PPP.rotationRandom, 0, 360)
        dim(AureaText.t("panel_giro"), PPP.spin, 2, -3600, 3600, " °/s")
        percent(AureaText.t("particular_size_random"), PPP.sizeRandom)
        percent(AureaText.t("particular_opacity_random"), PPP.opacityRandom)
        percent(AureaText.t("particular_color_random"), PPP.colorRandom)

        // --- Ao longo da vida -------------------------------------------------
        group(AureaText.t("particular_group_life"))
        lifeGradient
        lifeCurve(1, AureaText.t("particular_size_curve"), 0, 4)
        lifeCurve(2, AureaText.t("particular_opacity_curve"), 0, 1)

        // --- Física: para onde vai --------------------------------------------
        group(AureaText.t("particular_group_physics"))
        dim(AureaText.t("particular_gravity_x"), PPP.gravityX, 8, -8000, 8000, " px/s²")
        dim(AureaText.t("panel_gravidade"), PPP.gravityY, 8, -8000, 8000, " px/s²", negate: true)
        dim(AureaText.t("particular_wind"), PPP.windX, 4, -8000, 8000, " px/s²", negate: true)
        percent(AureaText.t("particular_drag"), PPP.drag, 4)
        dim(AureaText.t("particular_turbulence"), PPP.turbulence, 0.5, 0, 2000, "")
        dim(AureaText.t("particular_turb_scale"), PPP.turbulenceScale, 0.05, 0.05, 20, "x")
        dim(AureaText.t("particular_turb_speed"), PPP.turbulenceSpeed, 0.05, 0, 20, "x")
        dim(AureaText.t("particular_vortex"), PPP.vortex, 4, -3600, 3600, " °/s")
        dim(AureaText.t("particular_attractor"), PPP.attractor, 0.5, -100, 100, "")
        let collision = Int(value(PPP.collision))
        choice(AureaText.t("particular_collision"), PPP.collision, [
            "particular_collision_none", "particular_collision_plane",
            "particular_collision_sphere", "particular_collision_box",
        ])
        switch collision {
        case 1:
            dim(AureaText.t("particular_collision_y"), PPP.collisionY, 1, -4000, 4000, " px")
        case 2, 3:
            // Centro a partir do centro do emissor (Y é o mesmo controle da
            // altura do plano).
            dim(AureaText.t("particular_collision_cx"), PPP.collisionX, 1, -4000, 4000, " px")
            dim(AureaText.t("particular_collision_cy"), PPP.collisionY, 1, -4000, 4000, " px")
            dim(AureaText.t("particular_collision_cz"), PPP.collisionZ, 1, -4000, 4000, " px")
            if collision == 2 {
                dim(AureaText.t("particular_radius"), PPP.collisionRadius, 0.5, 0, 8000, " px")
            } else {
                dim(AureaText.t("particular_width"), PPP.collisionWidth, 1, 0, 16000, " px")
                dim(AureaText.t("particular_height"), PPP.collisionHeight, 1, 0, 16000, " px")
                dim(AureaText.t("particular_depth"), PPP.collisionDepth, 1, 0, 16000, " px")
            }
        default:
            EmptyView()
        }
        if collision > 0 { percent(AureaText.t("particular_bounce"), PPP.collisionBounce) }

        // --- Aux --------------------------------------------------------------
        group(AureaText.t("particular_group_aux"))
        dim(AureaText.t("particular_aux_count"), PPP.auxCount, 0.1, 0, 16, "")
        if value(PPP.auxCount) >= 0.5 {
            percent(AureaText.t("particular_aux_probability"), PPP.auxProbability)
            percent(AureaText.t("particular_aux_at"), PPP.auxAt)
            dim(AureaText.t("panel_duracao_cada"), PPP.auxLife, 0.02, 0.05, 20, " s")
            dim(AureaText.t("panel_velocidade"), PPP.auxSpeed, 2, 0, 5000, " px/s")
            dim(AureaText.t("panel_tamanho"), PPP.auxSize, 0.3, 0, 500, " px")
            angle(AureaText.t("panel_abertura"), PPP.auxSpread)
        }

        // --- Rastro -----------------------------------------------------------
        group(AureaText.t("particular_group_trail"))
        dim(AureaText.t("particular_trail_len"), PPP.trailLength, 0.002, 0, 2, " s", decimals: 2)
        if value(PPP.trailLength) > 0 {
            percent(AureaText.t("particular_trail_taper"), PPP.trailTaper)
            dim(AureaText.t("particular_trail_width"), PPP.trailWidth, 0.01, 0, 8, "x", decimals: 2)
            percent(AureaText.t("particular_trail_opacity"), PPP.trailOpacity)
        }

        // --- Renderização -----------------------------------------------------
        group(AureaText.t("particular_group_render"))
        choice(AureaText.t("particular_blend"), PPP.blendMode,
               ["particular_blend_normal", "particular_blend_add"])
        percent(AureaText.t("particular_softness"), PPP.softness)

        Spacer(minLength: 10)
        AdvancedToggle(open: advanced, count: 5) { advanced.toggle() }
        if advanced {
            dim(AureaText.t("particular_max_particles"), PPP.maxParticles, 20, 1, 1000000, "")
            angle(AureaText.t("particular_emitter_rotation"), PPP.emitterRotation)
            dim(AureaText.t("particular_depth"), PPP.emitterDepth, 1, 0, 4000, " px")
            dim(AureaText.t("particular_gravity_z"), PPP.gravityZ, 8, -8000, 8000, " px/s²")
            dim(AureaText.t("particular_wind_y"), PPP.windY, 4, -8000, 8000, " px/s²")
        }
        Spacer(minLength: 6)
        Text(AureaText.t("particular_deterministic_note"))
            .font(AureaType.tiny)
            .foregroundStyle(AureaColors.muted)
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
                        Button { applyPreset(entry.offset) } label: {
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

    private let presetKeys = ["panel_faiscas", "panel_neve", "panel_poeira_luz", "preset_rain",
                              "preset_fireflies", "preset_embers", "preset_confetti",
                              "preset_starfield", "preset_magic_dust", "preset_logo_burst"]

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

    private func toggleRow(_ label: String, _ param: Int) -> some View {
        PropertyCustomRow(label, selected: selected == param, onSelect: { selected = param }, keyframe: look(param)) {
            AureaToggle(checked: value(param) >= 0.5) { set(param, $0 ? 1 : 0) }
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

    private func angle(_ label: String, _ param: Int, _ min: Float = -180, _ max: Float = 180) -> some View {
        ruler(label, param, "\(Int(value(param).rounded()))°", 1, min, max)
    }

    private func percent(_ label: String, _ param: Int, _ unitsPerDp: Float = 0.01) -> some View {
        ruler(label, param, "\(Int((value(param) * 100).rounded()))%", unitsPerDp, 0, 1)
    }

    private func ruler(_ label: String, _ param: Int, _ text: String,
                       _ unitsPerDp: Float, _ min: Float, _ max: Float, negate: Bool = false) -> some View {
        PropertyCustomRow(label, selected: selected == param, onSelect: { selected = param }, keyframe: look(param)) {
            HStack(spacing: 8) {
                TickRuler(value: { value(param) }, unitsPerDp: unitsPerDp, active: true)
                    .frame(maxWidth: .infinity)
                    .valueDrag(enabled: true, start: { value(param) }, unitsPerDp: { unitsPerDp },
                               min: min, max: max,
                               onStart: { model.engine.run { $0.beginUndoGroup() } },
                               onValue: { set(param, $0) },
                               onEnd: { model.engine.run { $0.endUndoGroup() } })
                ValueBox(text)
            }
        }
    }

    /// Escolha de uma camada do projeto (fonte, imagem, modelo), com "Nenhuma".
    private func layerPick(_ label: String, _ current: Int64, _ candidates: [LayerItem],
                           _ emptyHint: String, onPick: @escaping (Int64) -> Void) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(label).font(AureaType.tiny).foregroundStyle(AureaColors.muted)
            if candidates.isEmpty {
                Text(emptyHint).font(AureaType.tiny).foregroundStyle(AureaColors.muted)
            } else {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 6) {
                        Chip(AureaText.t("particular_none"), on: current == 0) { onPick(0) }
                        ForEach(candidates) { row in
                            Chip(row.name, on: current == row.id) { onPick(row.id) }
                        }
                    }
                }
            }
        }
        .padding(.vertical, 4)
    }

    /// Cor ao longo da vida: até 8 paradas (posição + cor sRGB). Sem paradas vale
    /// o início → fim da partícula.
    private var lifeGradient: some View {
        let stops = curveStops(0, stride: 4)
        return VStack(alignment: .leading, spacing: 6) {
            Text(AureaText.t("particular_color_gradient"))
                .font(AureaType.tiny)
                .foregroundStyle(AureaColors.muted)
            if stops.isEmpty {
                HStack(spacing: 6) {
                    Text(AureaText.t("particular_curve_off")).font(AureaType.tiny).foregroundStyle(AureaColors.muted)
                    Chip(AureaText.t("particular_curve_use"), on: false) {
                        writeCurve(0, [0, 1, 1, 1, 1, 1, 0.45, 0.1])
                    }
                }
            } else {
                ForEach(Array(stops.enumerated()), id: \.offset) { entry in
                    let index = entry.offset
                    let stop = entry.element
                    HStack(spacing: 6) {
                        ColorWell(color: Color(.sRGB, red: Double(stop[1]), green: Double(stop[2]),
                                               blue: Double(stop[3]), opacity: 1)) {
                            model.engine.run { $0.beginUndoGroup() }
                            model.colorSheet = ColorSheetRequest(title: AureaText.t("particular_color_gradient"), initial: [stop[1], stop[2], stop[3], 1], onChange: { r, g, b, _ in
                                editStop(0, index, 4) { $0[1] = r; $0[2] = g; $0[3] = b }
                            }, onDone: { model.engine.run { $0.endUndoGroup() } })
                        }
                        Spacer(minLength: 8)
                        pointRuler(AureaText.t("particular_curve_at"), stop[0], 0, 1, 0.005, percent: true) { v in
                            editStop(0, index, 4) { $0[0] = v }
                        }
                        removeChip { removeStop(0, index, 4) }
                    }
                }
                curveButtons(stops.count) { add in
                    if add, let last = stops.last {
                        let prev = stops.count >= 2 ? stops[stops.count - 2] : last
                        writeCurve(0, (stops + [[(prev[0] + last[0]) * 0.5, last[1], last[2], last[3]]]).flatMap { $0 })
                    } else {
                        writeCurve(0, [])
                    }
                }
            }
        }
    }

    /// Tamanho ou opacidade ao longo da vida: pontos (posição, multiplicador)
    /// numa curva suave (Hermite, presa entre os vizinhos).
    private func lifeCurve(_ kind: Int, _ title: String, _ min: Float, _ max: Float) -> some View {
        let pts = curveStops(kind, stride: 2)
        return VStack(alignment: .leading, spacing: 6) {
            Text(title).font(AureaType.tiny).foregroundStyle(AureaColors.muted)
            if pts.isEmpty {
                HStack(spacing: 6) {
                    Text(AureaText.t("particular_curve_off")).font(AureaType.tiny).foregroundStyle(AureaColors.muted)
                    Chip(AureaText.t("particular_curve_use"), on: false) {
                        // Sobe e desce (tamanho) / aparece e some (opacidade).
                        writeCurve(kind, [0, kind == 1 ? 0.2 : 0, 0.3, 1, 1, 0])
                    }
                }
            } else {
                ForEach(Array(pts.enumerated()), id: \.offset) { entry in
                    let index = entry.offset
                    let point = entry.element
                    HStack(spacing: 6) {
                        pointRuler(AureaText.t("particular_curve_at"), point[0], 0, 1, 0.005, percent: true) { v in
                            editStop(kind, index, 2) { $0[0] = v }
                        }
                        pointRuler(AureaText.t("particular_curve_value"), point[1], min, max, 0.005, percent: kind == 2) { v in
                            editStop(kind, index, 2) { $0[1] = v }
                        }
                        removeChip { removeStop(kind, index, 2) }
                    }
                }
                curveButtons(pts.count) { add in
                    if add, let last = pts.last {
                        let prev = pts.count >= 2 ? pts[pts.count - 2] : last
                        writeCurve(kind, (pts + [[(prev[0] + last[0]) * 0.5, last[1]]]).flatMap { $0 })
                    } else {
                        writeCurve(kind, [])
                    }
                }
            }
        }
    }

    /// "+ Ponto" (até 8) e "voltar ao início → fim".
    private func curveButtons(_ count: Int, onAction: @escaping (Bool) -> Void) -> some View {
        HStack(spacing: 6) {
            if count < 8 { Chip(AureaText.t("particular_curve_add"), on: false) { onAction(true) } }
            Chip(AureaText.t("particular_curve_clear"), on: false) { onAction(false) }
        }
    }

    private func removeChip(_ onClick: @escaping () -> Void) -> some View {
        Button(action: onClick) {
            Text("×").font(.aurea(size: 15)).foregroundStyle(AureaColors.muted)
                .padding(.horizontal, 10).frame(height: 36)
                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 9))
        }
        .buttonStyle(.plain)
        .accessibilityLabel(AureaText.t("particular_remove"))
    }

    /// Régua compacta de um ponto de curva (sem losango: a curva não é keyframe).
    private func pointRuler(_ label: String, _ value: Float, _ min: Float, _ max: Float,
                            _ unitsPerDp: Float, percent: Bool,
                            onValue: @escaping (Float) -> Void) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text("\(label) " + (percent ? "\(Int((value * 100).rounded()))%" : numeroPtBr(value, casas: 2)))
                .font(AureaType.tiny)
                .foregroundStyle(AureaColors.muted)
            TickRuler(value: { value }, unitsPerDp: unitsPerDp, active: true, height: 32)
                .frame(maxWidth: .infinity)
                .valueDrag(enabled: true, start: { value }, unitsPerDp: { unitsPerDp },
                           min: min, max: max,
                           onStart: { model.engine.run { $0.beginUndoGroup() } },
                           onValue: onValue,
                           onEnd: { model.engine.run { $0.endUndoGroup() } })
        }
    }

    // =========================================================================
    // Estado vindo do núcleo
    // =========================================================================
    private func value(_ param: Int) -> Float { param < values.count ? values[param] : 0 }

    private func load() {
        guard model.selectedLayer != nil else { values = []; links = []; return }
        values = model.engine.particleParams(id).map { $0.floatValue }
        links = model.engine.particleLinks(id).map { $0.int64Value }
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

    private func setLink(_ kind: UInt32, _ target: Int64) {
        _ = model.engine.setParticleLink(id, kind: kind, target: target)
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
    private func editStop(_ kind: Int, _ index: Int, _ stride: Int, _ change: (inout [Float]) -> Void) {
        var stops = curveStops(kind, stride: stride)
        guard index < stops.count else { return }
        change(&stops[index])
        writeCurve(kind, stops.flatMap { $0 })
    }

    private func removeStop(_ kind: Int, _ index: Int, _ stride: Int) {
        var stops = curveStops(kind, stride: stride)
        guard index < stops.count else { return }
        stops.remove(at: index)
        writeCurve(kind, stops.flatMap { $0 })
    }
}

/// O respiro à esquerda dos botões de preset (12 dp do Android).
private let Aurca12: CGFloat = 12
