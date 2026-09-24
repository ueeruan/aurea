// Port of VectorPanel.kt and FormaKit.kt: values and paths belong to the engine.
import SwiftUI
import UniformTypeIdentifiers

struct VectorPanel: View {
    @EnvironmentObject private var model: AureaModel
    @State private var groups: [[String: Any]] = []
    @State private var tab = 0
    @State private var selected = -1
    @State private var adding = false
    @State private var advanced = Set<Int>()
    @State private var pathFlags = 0
    private let pathKinds = ["Caminho livre", "Retângulo", "Elipse", "Polígono", "Estrela"]
    private let tabs = ["Forma", "Preenchimento", "Borda", "Caminho", "Transformar", "Operadores", "Efeitos"]
    private var id: Int64 { model.primarySelection ?? 0 }
    private var group: [String: Any] { groups.indices.contains(Int(model.vectorGroup)) ? groups[Int(model.vectorGroup)] : [:] }
    private var params: [Float] { floats(group["params"]) }
    private var paths: [[Float]] { (group["paths"] as? [[NSNumber]] ?? []).map { $0.map(\.floatValue) } }
    private var path: [Float] { paths.indices.contains(Int(model.vectorPath)) ? paths[Int(model.vectorPath)] : [] }
    private var kind: Int { path.first.map(Int.init) ?? 0 }
    private var free: Bool { kind == 0 }
    private var pointCount: Int { (model.editingMask?.points.count ?? 0) / 6 }
    private var railLook: KeyframeLook {
        if selected >= 0 { return look(selected) }
        if selected == -2 { return pathFlags & 4 != 0 ? .keyHere : pathFlags & 2 != 0 ? .animated : .none }
        return .none
    }
    private var canKey: Bool { selected >= 0 ? params.count >= 22 : selected == -2 && free && !path.isEmpty }

    var body: some View {
        VStack(spacing: 0) {
            PanelHeader(title: AureaText.t("panel_vetor")) { model.panel = .none }
            HStack(spacing: 0) {
                LeftRail(keyframeLook: railLook, onKeyframe: canKey ? toggleKey : nil, onBack: { model.panel = .none })
                VStack(spacing: 0) {
                    pathPicker
                    ScrollView(.horizontal, showsIndicators: false) {
                        HStack(spacing: 6) {
                            ForEach(tabs.indices, id: \.self) { n in
                                VectorChip(tabs[n], selected: tab == n, bold: true, horizontal: 14) { tab = n; selected = -1 }
                            }
                        }.padding(.horizontal, 6).padding(.vertical, 6)
                    }.frame(height: 48)
                    ScrollView {
                        VStack(alignment: .leading, spacing: 0) {
                            if group.isEmpty { hint("panel_esta_camada_ainda_nao_tem_grupos") }
                            else {
                                switch tab {
                                case 0: shapeTab
                                case 1: fillTab
                                case 2: strokeTab
                                case 3: pathTab
                                case 4: transformTab
                                case 5: operatorsTab
                                default:
                                    VectorAction("panel_abrir_efeitos_camada", detail: "panel_desfoque_brilho_cor_outros_efeitos_aplicados") { model.openPanel(.effects) }.padding(.top, 8)
                                }
                            }
                        }.padding(.leading, 4).padding(.trailing, 10).padding(.bottom, 16)
                    }
                }
            }
        }.foregroundStyle(AureaColors.text).background(AureaColors.editorPanel)
            .onAppear(perform: load).onChange(of: model.status.modelRevision) { _ in load() }
            .onChange(of: model.status.playhead) { _ in load() }
            .onChange(of: id) { _ in selected = -1; load() }
            .onChange(of: model.vectorGroup) { _ in selected = -1; model.selectedMaskPoint = nil; load() }
            .onChange(of: model.vectorPath) { _ in selected = -1; model.selectedMaskPoint = nil; load() }
    }

    private var pathPicker: some View {
        VStack(spacing: 0) {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    ForEach(groups.indices, id: \.self) { gi in
                        let pathRows = groups[gi]["paths"] as? [[NSNumber]] ?? []
                        ForEach(pathRows.indices, id: \.self) { pi in
                            let pathKind = pathRows[pi].first?.intValue ?? 0
                            let name = pathKinds[min(4, max(0, pathKind))] + (pathRows.count > 1 ? " \(pi + 1)" : "")
                            let title = groups.count > 1 ? "\(groups[gi]["name"] as? String ?? "") · \(name)" : name
                            VectorChip(title, selected: model.vectorGroup == UInt32(gi) && model.vectorPath == UInt32(pi)) {
                                model.vectorGroup = UInt32(gi); model.vectorPath = UInt32(pi); model.selectedMaskPoint = nil; load()
                            }
                        }
                    }
                    VectorChip(AureaText.t(adding ? "panel_fechar" : "panel_adicionar"), selected: adding) { adding.toggle() }
                }.padding(.vertical, 4)
            }
            if adding {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 6) {
                        ForEach(pathKinds.indices, id: \.self) { n in
                            VectorChip(pathKinds[n], selected: false) { addPath(n); adding = false }
                        }
                    }.padding(.vertical, 4)
                }
            }
        }
    }
    @ViewBuilder private var shapeTab: some View {
        if !path.isEmpty {
            HStack(spacing: 6) {
                pointTool(0, "panel_selecionar")
                pointTool(1, "panel_adicionar_ponto")
                pointTool(2, "panel_remover_ponto", enabled: !free || pointCount > 0)
                pointTool(3, "panel_canto_suave", enabled: !free || pointCount > 1)
                VectorPointTool(kind: 4, label: AureaText.t(model.editingMask?.closed == true ? "panel_abrir_caminho" : "panel_fechar_caminho"), selected: false,
                                enabled: free && pointCount >= 2, closed: model.editingMask?.closed == true) {
                    if let mask = model.editingMask { model.setMaskPoints(mask.points, closed: !mask.closed); refresh() }
                }
            }.padding(.leading, 4).padding(.top, 4).padding(.bottom, 6)
            hint(model.vectorEditingPoints ? "panel_editando_pontos_palco_toque_concluir_palco" : (free ? "panel_escolha_ferramenta_toque_palco_editar_pontos" : "panel_ferramentas_pontos_transformam_forma_pronta_caminho"))
            if free {
                PropertyCustomRow(AureaText.t("panel_forma_animada"), selected: selected == -2, onSelect: { selected = -2 }) {
                    let counts = group["pathKeyCounts"] as? [NSNumber] ?? []
                    let keyCount = counts.indices.contains(Int(model.vectorPath)) ? counts[Int(model.vectorPath)].intValue : 0
                    Text(keyCount == 0 ? AureaText.t("panel_parada_toque_trilho_animar") : "\(keyCount) keyframes — editar no cabeçote grava ali")
                        .font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                }.aureaSelectedProperty(selected == -2 ? AureaText.t("panel_forma_animada") : nil)
            } else if kind == 1 || kind == 2 {
                pathDimension(4, "panel_largura", step: 1, range: 1...20000, unit: "px", reset: 200)
                pathDimension(5, "panel_altura", step: 1, range: 1...20000, unit: "px", reset: 200)
                if kind == 1 { pathDimension(6, "panel_raio", step: 0.5, range: 0...10000, unit: "px", reset: 0) }
            } else {
                pathDimension(7, kind == 3 ? "panel_lados" : "panel_pontas", step: 0.06, range: 3...100, unit: "", reset: 5, integer: true)
                pathDimension(8, kind == 3 ? "panel_raio" : "panel_raio_externo", step: 1, range: 1...20000, unit: "px", reset: 100)
                if kind == 4 { pathDimension(9, "panel_raio_interno", step: 1, range: 1...20000, unit: "px", reset: 50) }
            }
            advancedSection {
                if !free && kind >= 3 {
                    pathDimension(10, "panel_arredondar_pontas", step: 0.5, range: -200...200, unit: "%", reset: 0)
                    pathDimension(12, "panel_giro", step: 0.5, range: -3600...3600, unit: "°", reset: 0)
                }
                VectorToggle("panel_inverter_direcao_caminho", on: path.count > 1 && path[1] > 0.5) { setPathField(1, $0 ? 1 : 0) }
                if !free {
                    VectorAction("panel_converter_caminho_livre", detail: "panel_mantem_forma_libera_pontos") { makeEditable() }.padding(.bottom, 6)
                }
                title("panel_novo_grupo_preenchimento_borda_proprios")
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 6) { ForEach(pathKinds.indices, id: \.self) { n in VectorChip("+ " + pathKinds[n], selected: false) { addGroup(n) } } }.padding(.vertical, 4)
                }
                if paths.count > 1 { VectorAction("panel_apagar_este_caminho", danger: true) { _ = model.engine.removeVectorPath(id, group: model.vectorGroup, path: model.vectorPath); refresh() }.padding(.vertical, 6) }
                if groups.count > 1 { VectorAction("", title: "Apagar o grupo \"\(group["name"] as? String ?? "")\"", danger: true) { _ = model.engine.removeVectorGroup(id, group: model.vectorGroup); refresh() } }
            }
        }
    }
    @ViewBuilder private var fillTab: some View {
        VectorToggle("panel_preencher", on: flag("fillEnabled")) { edit(2, [$0 ? 1 : 0]) }
        if flag("fillEnabled") {
            paintEditor("fill", field: 4, what: AureaText.t("panel_preenchimento_6e8f"))
            param(5, "panel_opacidade", step: 0.5, range: 0...100, unit: "%", reset: 100)
            advancedSection {
                title("panel_onde_caminhos_cruzam")
                choices(["panel_preencher_tudo", "panel_alternar_deixa_furos"], value: Int(number("fillRule"))) { edit(3, [Float($0)]) }
                paintAdvanced("fill", field: 4)
            }
        }
    }
    @ViewBuilder private var strokeTab: some View {
        VectorToggle("panel_borda", on: flag("strokeEnabled")) { edit(8, [$0 ? 1 : 0]) }
        if flag("strokeEnabled") {
            paintEditor("stroke", field: 9, what: "borda")
            param(3, "panel_largura", step: 0.2, range: 0...2000, unit: "px", decimals: 1, reset: 6)
            param(6, "panel_opacidade", step: 0.5, range: 0...100, unit: "%", reset: 100)
            advancedSection {
                title("panel_pontas"); choices(["panel_retas", "panel_redondas", "panel_quadradas"], value: Int(number("cap"))) { edit(13, [Float($0)]) }
                title("panel_cantos"); choices(["panel_vivos", "panel_redondos", "panel_chanfrados"], value: Int(number("join"))) { edit(14, [Float($0)]) }
                if number("join") == 0 { human("panel_limite_canto", value: number("miter"), step: 0.05, range: 1...100, decimals: 1, reset: 4) { edit(15, [$0]) } }
                paintAdvanced("stroke", field: 9)
            }
        }
    }
    private var pathTab: some View {
        let dashes = floats(group["dashes"])
        return VStack(alignment: .leading, spacing: 0) {
            VectorToggle("panel_aparar_desenhar_so_trecho", on: flag("trimEnabled")) { edit(17, [$0 ? 1 : 0]) }
            if flag("trimEnabled") {
                param(0, "panel_inicio", step: 0.3, range: 0...100, unit: "%", reset: 0)
                param(1, "panel_fim", step: 0.3, range: 0...100, unit: "%", reset: 100)
            }
            VectorToggle("panel_tracejado", on: !dashes.isEmpty) { on in
                model.beginGesture("tracejado"); edit(16, on ? [24, 12] : []); if on { edit(8, [1]) }; model.endGesture()
            }
            if dashes.count >= 2 {
                if !flag("strokeEnabled") { hint("panel_tracejado_aparece_borda_ligue_borda") }
                human("panel_traco", value: dashes[0], step: 0.3, range: 0...5000, unit: "px", reset: 24) { v in var next = dashes; next[0] = v; edit(16, next) }
                human("panel_espaco", value: dashes[1], step: 0.3, range: 0...5000, unit: "px", reset: 12) { v in var next = dashes; next[1] = v; edit(16, next) }
            }
            if flag("trimEnabled") || dashes.count >= 2 {
                advancedSection {
                    if flag("trimEnabled") {
                        param(2, "panel_deslocar_trecho", step: 0.5, range: -100000...100000, unit: "%", reset: 0)
                        title("panel_varios_caminhos"); choices(["panel_cada_sozinho", "panel_depois_outro"], value: Int(number("trimMode"))) { edit(18, [Float($0)]) }
                    }
                    if dashes.count >= 2 { param(4, "panel_deslocar_tracos", step: 0.5, range: -100000...100000, unit: "px", reset: 0) }
                }
            }
        }
    }
    private var transformTab: some View {
        VStack(alignment: .leading, spacing: 0) {
            hint("panel_move_so_este_grupo_dentro_camada")
            param(15, "panel_posicao_x", step: 0.5, range: -100000...100000, unit: "px", reset: 0)
            param(16, "panel_posicao_y", step: 0.5, range: -100000...100000, unit: "px", reset: 0)
            param(17, "panel_rotacao", step: 0.5, range: -3600...3600, unit: "°", reset: 0)
            param(18, "panel_escala", step: 0.3, range: -10000...10000, unit: "%", reset: 100)
            param(19, "panel_opacidade", step: 0.5, range: 0...100, unit: "%", reset: 100)
            advancedSection { VectorToggle("panel_grupo_visivel", on: flag("visible")) { edit(0, [$0 ? 1 : 0]) } }
        }
    }
    @ViewBuilder private var operatorsTab: some View {
        title("panel_juntar_caminhos_grupo")
        choices(["panel_nao_juntar", "panel_unir", "panel_subtrair", "panel_intersecao", "panel_excluir_sobreposicao"], value: Int(number("merge"))) { edit(1, [Float($0)]) }
        if paths.count < 2 { hint("panel_juntar_precisa_2_caminhos_grupo_use") }
        VectorToggle("panel_repetir_copias", on: flag("repeatEnabled")) { edit(19, [$0 ? 1 : 0]) }
        if flag("repeatEnabled") {
            param(7, "panel_copias", step: 0.05, range: 0...500, reset: 3)
            param(9, "panel_distancia_x", step: 0.5, range: -100000...100000, unit: "px", reset: 120)
            param(10, "panel_distancia_y", step: 0.5, range: -100000...100000, unit: "px", reset: 0)
            param(11, "panel_rotacao", step: 0.5, range: -3600...3600, unit: "°", reset: 0)
            advancedSection {
                param(12, "panel_escala", step: 0.3, range: -10000...10000, unit: "%", reset: 100)
                param(8, "panel_deslocamento", step: 0.05, range: -500...500, decimals: 1, reset: 0)
                param(13, "panel_opacidade_inicial", step: 0.5, range: 0...100, unit: "%", reset: 100)
                param(14, "panel_opacidade_final", step: 0.5, range: 0...100, unit: "%", reset: 100)
                title("panel_ordem_copias"); choices(["panel_copias_embaixo", "panel_copias_cima"], value: flag("repeatAbove") ? 1 : 0) { edit(20, [Float($0)]) }
            }
        }
    }

    @ViewBuilder private func paintEditor(_ key: String, field: UInt32, what: String) -> some View {
        let paint = group[key] as? [String: Any] ?? [:]
        let type = (paint["type"] as? NSNumber)?.intValue ?? 0
        let rgba = floats(paint["color"])
        let stops = (paint["stops"] as? [[NSNumber]] ?? []).map { $0.map(\.floatValue) }
        choices(["panel_cor_solida", "panel_degrade_reto", "panel_degrade_redondo"], value: type) { type in
            model.beginGesture("tinta")
            if type > 0 && stops.count < 2 { edit(field + 3, [0] + (rgba.count == 4 ? rgba : [1,1,1,1]) + [1,0,0,0,1]) }
            let points = floats(paint["points"])
            if type > 0 && points.count == 4 && points[0] == -100 && points[2] == 100 && path.count >= 6 && !free { edit(field + 2, [path[2] - path[4] / 2, path[3], path[2] + path[4] / 2, path[3]]) }
            edit(field, [Float(type)]); model.endGesture()
        }
        if type == 0 { colorLine("Cor da " + what, rgba: rgba) { edit(field + 1, $0) } }
        else {
            ForEach(stops.indices, id: \.self) { n in
                colorLine(n == 0 ? AureaText.t("panel_cor_inicial") : n == stops.count - 1 ? AureaText.t("panel_cor_final") : "Cor \(n + 1)", rgba: Array(stops[n].dropFirst())) { rgba in
                    var next = stops; next[n] = [next[n][0]] + rgba; edit(field + 3, next.flatMap { $0 })
                }
            }
        }
    }
    @ViewBuilder private func paintAdvanced(_ key: String, field: UInt32) -> some View {
        let paint = group[key] as? [String: Any] ?? [:]
        let type = (paint["type"] as? NSNumber)?.intValue ?? 0
        let points = floats(paint["points"])
        let stops = (paint["stops"] as? [[NSNumber]] ?? []).map { $0.map(\.floatValue) }
        if type != 0 {
            Text("Degradê").font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted).padding(.top, 8).padding(.bottom, 4)
            HStack(spacing: 6) {
                if stops.count >= 2 && stops.count < 8 { VectorChip(AureaText.t("panel_cor_meio"), selected: false) {
                    var next = stops; let a = next[next.count - 2], b = next[next.count - 1]
                    guard a.count == 5 && b.count == 5 else { return }
                    next.insert(zip(a, b).map { pair in (pair.0 + pair.1) / 2 }, at: next.count - 1); edit(field + 3, next.flatMap { $0 })
                } }
                if stops.count > 2 { VectorChip(AureaText.t("panel_cor_meio_dd59"), selected: false) { var next = stops; next.remove(at: next.count - 2); edit(field + 3, next.flatMap { $0 }) } }
            }.padding(.vertical, 4)
            if points.count == 4 {
                ForEach(0..<(type == 1 ? 4 : 3), id: \.self) { n in
                    let labels = type == 1 ? ["Início X", "Início Y", "Fim X", "Fim Y"] : ["Centro X", "Centro Y", "Raio"]
                    VectorHumanRow(label: labels[n], value: points[n], step: 0.5, range: -100000...100000, unit: "px", reset: n == 0 ? -100 : n == 2 ? 100 : 0) { v in var next = points; next[n] = v; edit(field + 2, next) }
                }
            }
        }
    }
    private func colorLine(_ label: String, rgba: [Float], set: @escaping ([Float]) -> Void) -> some View {
        Button {
            model.beginGesture("cor")
            model.colorSheet = ColorSheetRequest(title: label, initial: rgba, onChange: { r, g, b, a in set([r, g, b, a]) }, onDone: { model.endGesture() })
        } label: {
            HStack {
                Text(label).font(.aurea(size: 13.5)).foregroundStyle(AureaColors.text); Spacer()
                AureaColorSwatch(color: color(rgba)).frame(width: 30, height: 30).clipShape(RoundedRectangle(cornerRadius: 6))
            }.frame(height: 48).contentShape(Rectangle())
        }.buttonStyle(.plain)
    }
    private func color(_ values: [Float]) -> Color {
        guard values.count >= 4 else { return .white }
        return Color(.sRGB, red: Double(values[0]), green: Double(values[1]), blue: Double(values[2]), opacity: Double(values[3]))
    }
    private func param(_ n: Int, _ label: String, step: Float, range: ClosedRange<Float>, unit: String = "", decimals: Int = 0, reset: Float) -> some View {
        VectorHumanRow(label: AureaText.t(label), value: params.indices.contains(n) ? params[n] : reset, step: step, range: range, unit: unit, decimals: decimals, reset: reset,
                       selected: selected == n, look: look(n), select: { selected = n }) {
            _ = model.engine.setVectorParam(id, group: model.vectorGroup, param: UInt32(n), value: $0); refresh()
        }
    }
    private func human(_ label: String, value: Float, step: Float, range: ClosedRange<Float>, unit: String = "", decimals: Int = 0, reset: Float, set: @escaping (Float) -> Void) -> some View {
        VectorHumanRow(label: AureaText.t(label), value: value, step: step, range: range, unit: unit, decimals: decimals, reset: reset, set: set)
    }
    private func pathDimension(_ slot: Int, _ label: String, step: Float, range: ClosedRange<Float>, unit: String, reset: Float, integer: Bool = false) -> some View {
        human(label, value: path.indices.contains(slot) ? path[slot] : reset, step: step, range: range, unit: unit, reset: reset) { setPathField(slot, integer ? $0.rounded() : $0) }
    }
    private func pointTool(_ tool: Int, _ label: String, enabled: Bool = true) -> some View {
        VectorPointTool(kind: tool, label: AureaText.t(label), selected: model.vectorEditingPoints && model.vectorPointTool == tool, enabled: enabled) {
            if !free { makeEditable() }
            model.vectorPointTool = tool; model.vectorEditingPoints = true; model.vectorFreehand = false; model.maskDrawing = tool == 1
        }
    }
    private func title(_ key: String) -> some View { Text(AureaText.t(key)).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted).padding(.top, 8).padding(.bottom, 4) }
    private func hint(_ key: String) -> some View { Text(AureaText.t(key)).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted) }
    private func choices(_ keys: [String], value: Int, set: @escaping (Int) -> Void) -> some View { ChoiceChips(keys.map { AureaText.t($0) }, selected: value, onSelect: set) }
    @ViewBuilder private func advancedSection<Content: View>(@ViewBuilder content: () -> Content) -> some View {
        let open = advanced.contains(tab)
        Button { if open { advanced.remove(tab) } else { advanced.insert(tab) } } label: {
            HStack(spacing: 6) {
                Text(AureaText.t("panel_avancado")).font(.aurea(size: 13, weight: .semibold))
                CupertinoGlyph.text(open ? CupertinoGlyph.ChevronUp : CupertinoGlyph.ChevronDown, size: 12, color: AureaColors.muted)
                Spacer(minLength: 12); AureaColors.border.frame(height: 1)
            }.foregroundStyle(AureaColors.muted).frame(height: 44)
        }.buttonStyle(.plain)
        if open { content() }
    }
    private func floats(_ value: Any?) -> [Float] { (value as? [NSNumber] ?? []).map(\.floatValue) }
    private func flag(_ key: String) -> Bool { (group[key] as? NSNumber)?.boolValue ?? false }
    private func number(_ key: String) -> Float { (group[key] as? NSNumber)?.floatValue ?? 0 }
    private func look(_ n: Int) -> KeyframeLook {
        guard params.count >= 22 else { return .none }
        return Int(params[21]) & (1 << n) != 0 ? .keyHere : Int(params[20]) & (1 << n) != 0 ? .animated : .none
    }
    private func load() {
        groups = model.engine.vectorGroups(id)
        if model.vectorGroup >= UInt32(groups.count) { model.vectorGroup = UInt32(max(0, groups.count - 1)) }
        if model.vectorPath >= UInt32(paths.count) { model.vectorPath = UInt32(max(0, paths.count - 1)) }
        let state = model.engine.vectorPath(id, group: model.vectorGroup, path: model.vectorPath)
        pathFlags = state.count > 6 ? state[6].intValue : 0
        model.refreshMasks()
    }
    private func refresh() { model.refreshModel(force: true); load() }
    private func edit(_ field: UInt32, _ values: [Float]) {
        _ = model.engine.editVectorGroup(id, group: model.vectorGroup, field: field, values: values.map { NSNumber(value: $0) }); refresh()
    }
    private func setPathField(_ slot: Int, _ value: Float) {
        guard path.count == 13, path.indices.contains(slot) else { return }
        var next = path; next[slot] = value; edit(23, [Float(model.vectorPath)] + next)
    }
    private func makeEditable() { _ = model.engine.makeVectorPathEditable(id, group: model.vectorGroup, path: model.vectorPath); refresh() }
    private func toggleKey() {
        if selected >= 0 { _ = model.engine.toggleVectorParamKey(id, group: model.vectorGroup, param: UInt32(selected)) }
        else if selected == -2 && free { _ = model.engine.toggleVectorPathKey(id, group: model.vectorGroup, path: model.vectorPath) }
        refresh()
    }
    private func addPath(_ kind: Int) {
        let created = model.engine.addVectorPath(id, group: model.vectorGroup, kind: UInt32(kind))
        if created >= 0 { model.vectorPath = UInt32(created); if kind == 0 { model.vectorPointTool = 1; model.vectorEditingPoints = true; model.maskDrawing = true } }; refresh()
    }
    private func addGroup(_ kind: Int) {
        let created = model.engine.addVectorGroup(id, kind: UInt32(kind))
        if created >= 0 { model.vectorGroup = UInt32(created); model.vectorPath = 0; if kind == 0 { model.vectorPointTool = 1; model.vectorEditingPoints = true; model.maskDrawing = true } }; refresh()
    }
}

private struct VectorHumanRow: View {
    @EnvironmentObject private var model: AureaModel
    let label: String
    let value: Float
    let step: Float
    let range: ClosedRange<Float>
    var unit = ""
    var decimals = 0
    let reset: Float
    var selected = false
    var look: KeyframeLook = .none
    var select: (() -> Void)? = nil
    let set: (Float) -> Void
    @State private var dragging = false
    @State private var live: Float = 0
    private var shown: Float { dragging ? live : value }
    var body: some View {
        HStack(spacing: 6) {
            PropertyLabelChip(label, expression: .none, onTap: select)
                .overlay(alignment: .trailing) { if look != .none { KeyframeDiamondIcon(look: look, enabled: true).scaleEffect(0.5).frame(width: 12) } }
            TickRuler(value: { shown }, unitsPerDp: step, active: selected || select == nil)
                .frame(maxWidth: .infinity)
                .valueDrag(enabled: true, start: { value }, unitsPerDp: { step }, min: range.lowerBound, max: range.upperBound,
                           onStart: { live = value; dragging = true; select?(); model.beginGesture(label) },
                           onValue: { live = $0; set($0) }, onEnd: { dragging = false; model.endGesture() })
            ValueBox(comUnidade(numeroPtBr(shown, casas: decimals), unit)) {
                model.numericKeypad = KeypadRequest(title: label, value: value, unit: unit, min: range.lowerBound, max: range.upperBound, decimals: decimals) { set($0.clamped(to: range)) }
            }
            Button { set(reset) } label: { CupertinoGlyph.text(CupertinoGlyph.ArrowCounterclockwise, size: 16, color: AureaColors.muted).frame(width: 34, height: 44) }
                .buttonStyle(.plain).opacity(abs(shown - reset) > 0.001 * max(1, abs(reset)) ? 1 : 0)
                .disabled(abs(shown - reset) <= 0.001 * max(1, abs(reset))).accessibilityLabel(AureaText.t("panel_voltar_padrao"))
        }.frame(height: 48).aureaSelectedProperty(selected ? label : nil)
            .onDisappear { if dragging { dragging = false; model.endGesture() } }
    }
}

private struct VectorChip: View {
    let title: String
    var selected: Bool
    var bold = false
    var horizontal: CGFloat = 12
    let action: () -> Void
    init(_ title: String, selected: Bool, bold: Bool = false, horizontal: CGFloat = 12, action: @escaping () -> Void) {
        self.title = title; self.selected = selected; self.bold = bold; self.horizontal = horizontal; self.action = action
    }
    var body: some View {
        Button(action: action) { Text(title).font(.aurea(size: 12.5, weight: bold ? (selected ? .bold : .medium) : .regular)).lineLimit(1)
            .foregroundStyle(selected ? AureaColors.accent : AureaColors.text).padding(.horizontal, horizontal).frame(height: 36)
            .background(selected ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 9))
        }.buttonStyle(AureaPressStyle(shrink: 1))
    }
}
private struct VectorToggle: View {
    let key: String
    let on: Bool
    let set: (Bool) -> Void
    init(_ key: String, on: Bool, set: @escaping (Bool) -> Void) { self.key = key; self.on = on; self.set = set }
    var body: some View {
        HStack {
            Text(AureaText.t(key)).font(.aurea(size: 14, weight: .semibold)).frame(maxWidth: .infinity, alignment: .leading).contentShape(Rectangle()).onTapGesture { set(!on) }
            AureaToggle(checked: on, onCheckedChange: set)
        }.frame(height: 48)
    }
}
private struct VectorAction: View {
    let key: String
    var title: String? = nil
    var detail: String? = nil
    var danger = false
    let action: () -> Void
    init(_ key: String, title: String? = nil, detail: String? = nil, danger: Bool = false, action: @escaping () -> Void) {
        self.key = key; self.title = title; self.detail = detail; self.danger = danger; self.action = action
    }
    var body: some View {
        Button(action: action) {
            VStack(alignment: .leading, spacing: 2) {
                Text(title ?? AureaText.t(key)).font(.aurea(size: 14, weight: .semibold)).foregroundStyle(danger ? AureaColors.danger : AureaColors.text)
                if let detail { Text(AureaText.t(detail)).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted) }
            }.frame(maxWidth: .infinity, alignment: .leading).padding(.horizontal, 14).padding(.vertical, 11)
                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 12))
        }.buttonStyle(AureaPressStyle())
    }
}
private struct VectorPointTool: View {
    let kind: Int
    let label: String
    let selected: Bool
    var enabled = true
    var closed = false
    let action: () -> Void
    var body: some View {
        let color = !enabled ? AureaColors.railDisabled : selected ? AureaColors.accent : AureaColors.text
        Button(action: action) {
            VStack(spacing: 4) {
                Canvas { context, size in draw(context, width: size.width, color: color) }.frame(width: 22, height: 22)
                Text(label).font(.aurea(size: 10, weight: .semibold)).foregroundStyle(color).lineLimit(2).multilineTextAlignment(.center)
            }.padding(.horizontal, 2).padding(.vertical, 6).frame(maxWidth: .infinity).frame(height: 60)
                .background(selected ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
                .overlay { if selected { RoundedRectangle(cornerRadius: 10).stroke(AureaColors.accent, lineWidth: 1.5) } }
        }.buttonStyle(AureaPressStyle(shrink: 1)).disabled(!enabled).accessibilityLabel(label)
    }
    private func draw(_ ctx: GraphicsContext, width w: CGFloat, color: Color) {
        func p(_ x: CGFloat, _ y: CGFloat) -> CGPoint { CGPoint(x: w * x, y: w * y) }
        func stroke(_ path: Path, _ width: CGFloat = 1.6) { ctx.stroke(path, with: .color(color), style: StrokeStyle(lineWidth: width, lineCap: .round)) }
        func dot(_ x: CGFloat, _ y: CGFloat, _ r: CGFloat) { ctx.fill(Path(ellipseIn: CGRect(x: w * (x-r), y: w * (y-r), width: w * 2 * r, height: w * 2 * r)), with: .color(color)) }
        var path = Path()
        switch kind {
        case 0:
            let points: [CGPoint] = [p(0.22,0.08),p(0.22,0.84),p(0.42,0.64),p(0.56,0.94),p(0.68,0.88),p(0.54,0.58),p(0.82,0.58)]
            path.addLines(points); path.closeSubpath(); ctx.fill(path, with: .color(color))
        case 1, 2:
            path.move(to: p(0.02,0.78)); path.addCurve(to: p(0.5,0.52), control1: p(0.25,0.3), control2: p(0.45,0.3)); stroke(path)
            ctx.fill(Path(CGRect(x: w * 0.37, y: w * 0.39, width: w * 0.26, height: w * 0.26)), with: .color(color))
            var sign = Path(); sign.move(to: p(0.61,0.26)); sign.addLine(to: p(0.95,0.26))
            if kind == 1 { sign.move(to: p(0.78,0.09)); sign.addLine(to: p(0.78,0.43)) }; stroke(sign, 2)
        case 3:
            path.addLines([p(0.04,0.85),p(0.24,0.2),p(0.44,0.85)]); stroke(path)
            var curve = Path(); curve.move(to: p(0.56,0.85)); curve.addCurve(to: p(0.96,0.85), control1: p(0.62,0.1), control2: p(0.9,0.1)); stroke(curve)
            dot(0.24,0.2,0.08); dot(0.76,0.3,0.08)
        default:
            if closed { path.addArc(center: p(0.5,0.5), radius: w * 0.36, startAngle: .degrees(20), endAngle: .degrees(320), clockwise: false) }
            else { path.addEllipse(in: CGRect(x: w * 0.14, y: w * 0.14, width: w * 0.72, height: w * 0.72)) }
            stroke(path, 1.7); dot(0.86,0.5,0.09)
        }
    }
}
