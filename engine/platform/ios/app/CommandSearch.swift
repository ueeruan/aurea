import SwiftUI

struct EditorCommand: Decodable, Identifiable {
    let id: String
    let title: String
    let detail: String
    let keywords: String
    let requires: String
    static let catalog: [EditorCommand] = {
        guard let url = Bundle.main.url(forResource: "editor_commands", withExtension: "json"),
              let data = try? Data(contentsOf: url),
              let values = try? JSONDecoder().decode([EditorCommand].self, from: data) else { return [] }
        return values
    }()
}

private struct CommandHit: Identifiable {
    let id: String
    let title: String
    let detail: String
    let search: String
    let category: String
    let requires: String
    var effect: UInt32?
    var preset: PanelPresetEntry?
}

@MainActor
struct CommandSearchView: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    @Environment(\.dismiss) private var dismiss
    @StateObject private var effectPrefs = FxEffectPrefs()
    @State private var favorites = Set(UserDefaults.standard.stringArray(forKey: "aurea.commands.favorites") ?? [])
    @State private var presetFavorites = Set(UserDefaults.standard.stringArray(forKey: "presetFavorites") ?? [])
    @State private var query = ""
    @State private var category = "Tudo"
    @State private var hits: [CommandHit] = []

    private func unavailable(_ requirement: String) -> String? {
        let selected = model.layers.filter { model.selection.contains($0.id) }
        if requirement == "any" { return nil }
        if requirement == "layers" { return model.layers.isEmpty ? "Adicione uma camada" : nil }
        if requirement == "undo" { return model.status.canUndo == 0 ? "Nenhuma edição para desfazer" : nil }
        if requirement == "redo" { return model.status.canRedo == 0 ? "Nenhuma edição para refazer" : nil }
        if selected.isEmpty { return "Selecione uma camada" }
        if selected.contains(where: { $0.locked }) { return "Desbloqueie a seleção para editar" }
        if requirement == "selection" { return nil }
        if requirement == "inside" {
            return selected.contains { model.status.playhead > Int64($0.startFrame) && model.status.playhead < Int64($0.endFrame) } ? nil : "Posicione o cabeçote dentro do clipe"
        }
        guard selected.count == 1, let layer = selected.first else { return "Selecione apenas uma camada" }
        switch requirement {
        case "single": return nil
        case "inside_single": return model.status.playhead > Int64(layer.startFrame) && model.status.playhead < Int64(layer.endFrame) ? nil : "Posicione o cabeçote dentro do clipe"
        case "video", "video_inside":
            if layer.kind != 1 { return "Selecione um vídeo" }
            return requirement == "video_inside" && (model.status.playhead < Int64(layer.startFrame) || model.status.playhead >= Int64(layer.endFrame)) ? "Posicione o cabeçote dentro do vídeo" : nil
        case "media": return [1, 3].contains(layer.kind) ? nil : "Selecione vídeo ou áudio"
        case "text": return layer.kind == 4 ? nil : "Selecione um texto"
        case "visual": return layer.kind != 3 ? nil : "Selecione uma camada visual"
        case "model3d": return layer.kind == 10 ? nil : "Selecione um modelo 3D"
        case "particles": return layer.kind == 11 ? nil : "Selecione partículas"
        case "vector": return model.isVectorLayer ? nil : "Selecione um vetor"
        case "precomp": return layer.kind == 12 ? nil : "Selecione uma pré-composição"
        default: return "Ação indisponível neste contexto"
        }
    }

    private func favorite(_ hit: CommandHit) -> Bool {
        if let effect = hit.effect { return effectPrefs.isFavorite(effect) }
        if let preset = hit.preset { return presetFavorites.contains(preset.id) }
        return favorites.contains(hit.id)
    }
    private var results: [CommandHit] {
        let terms = fxNormalizeSearch(query).split(whereSeparator: { $0.isWhitespace }).map(String.init)
        return hits.filter { hit in
            (category == "Tudo" || category == hit.category || category == "Favoritos" && favorite(hit)) &&
            terms.allSatisfy { hit.search.contains($0) } &&
            (!terms.isEmpty || category != "Tudo" || favorite(hit) || hit.category == "Ações" && unavailable(hit.requires) == nil)
        }.enumerated().sorted { a, b in
            let af = favorite(a.element), bf = favorite(b.element)
            return af == bf ? a.offset < b.offset : af
        }.map(\.element)
    }

    var body: some View {
        VStack(spacing: 10) {
            HStack {
                Text("Ferramentas").font(.aurea(size: 20, weight: .semibold))
                Spacer()
                Button("Fechar") { dismiss() }.frame(minWidth: 44, minHeight: 44)
            }
            TextField("Buscar ação, efeito ou preset", text: $query)
                .textInputAutocapitalization(.never).autocorrectionDisabled()
                .padding(12).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
                .accessibilityIdentifier("commandSearchField")
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 8) {
                    ForEach(["Tudo", "Favoritos", "Ações", "Efeitos", "Presets"], id: \.self) { name in
                        Button { category = name } label: {
                            Text(name).font(.aurea(size: 13)).padding(.horizontal, 12).frame(minHeight: 44)
                                .background(category == name ? AureaColors.accentDim : AureaColors.chip, in: Capsule())
                        }.buttonStyle(.plain)
                    }
                }
            }
            if results.isEmpty {
                Text(category == "Favoritos" ? "Toque na estrela de uma ferramenta para guardar aqui." : "Nenhuma ferramenta encontrada.")
                    .foregroundStyle(AureaColors.muted).padding(.vertical, 20)
            }
            ScrollView {
                LazyVStack(spacing: 0) {
                    ForEach(results) { hit in commandRow(hit) }
                }
            }
        }
        .padding(16).foregroundStyle(AureaColors.text).background(AureaColors.editorPanel)
        .onAppear(perform: load)
    }

    private func commandRow(_ hit: CommandHit) -> some View {
        let reason = unavailable(hit.requires)
        return HStack(spacing: 0) {
            Button {
                guard unavailable(hit.requires) == nil else { return }
                dismiss()
                execute(hit)
            } label: {
                VStack(alignment: .leading, spacing: 4) {
                    Text(hit.title).font(.aurea(size: 15)).foregroundStyle(reason == nil ? AureaColors.text : AureaColors.muted)
                    Text(reason ?? hit.detail).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                }.padding(.vertical, 10).frame(maxWidth: .infinity, minHeight: 64, alignment: .leading).contentShape(Rectangle())
            }.buttonStyle(.plain).disabled(reason != nil).accessibilityIdentifier("command:\(hit.id)")
            Button { toggleFavorite(hit) } label: {
                Image(systemName: favorite(hit) ? "star.fill" : "star").foregroundStyle(AureaColors.accent).frame(width: 48, height: 48)
            }.buttonStyle(.plain).accessibilityLabel("\(favorite(hit) ? "Remover dos" : "Adicionar aos") favoritos: \(hit.title)")
        }
    }

    private func load() {
        hits = EditorCommand.catalog.map {
            CommandHit(id: $0.id, title: $0.title, detail: $0.detail, search: fxNormalizeSearch("\($0.title) \($0.detail) \($0.keywords)"), category: "Ações", requires: $0.requires)
        }
        hits += model.effectCatalog.map {
            CommandHit(id: "effect:\($0.typeId)", title: fxEffectDisplayName($0.typeId, $0.name), detail: "Adicionar efeito · \($0.category)", search: fxEffectSearchText($0.typeId, $0.name, $0.category), category: "Efeitos", requires: "selection", effect: $0.typeId)
        }
        hits += PanelPresetEntry.loadAll().map {
            CommandHit(id: "preset:\($0.id)", title: $0.name, detail: "Abrir preset · \($0.kind.rawValue)", search: fxNormalizeSearch("\($0.name) \($0.kind.rawValue) preset"), category: "Presets", requires: $0.kind == .text ? "text" : "single", preset: $0)
        }
    }

    private func toggleFavorite(_ hit: CommandHit) {
        if let effect = hit.effect { effectPrefs.toggleFavorite(effect); return }
        if let preset = hit.preset {
            if presetFavorites.contains(preset.id) { presetFavorites.remove(preset.id) } else { presetFavorites.insert(preset.id) }
            UserDefaults.standard.set(Array(presetFavorites), forKey: "presetFavorites")
        } else {
            if favorites.contains(hit.id) { favorites.remove(hit.id) } else { favorites.insert(hit.id) }
            UserDefaults.standard.set(Array(favorites), forKey: "aurea.commands.favorites")
        }
    }

    private func execute(_ hit: CommandHit) {
        if let effect = hit.effect {
            let targets = model.layers.filter { model.selection.contains($0.id) && !$0.locked }
            model.mutate { engine in
                engine.beginUndoGroup()
                for layer in targets { engine.addEffect(effect, toLayer: layer.id, at: UInt32.max) }
                engine.endUndoGroup()
            }
            effectPrefs.addRecent(effect); model.refreshModel(force: true); model.openPanel(.effects)
            return
        }
        if let preset = hit.preset {
            model.presetsOpenKind = preset.kind.rawValue; model.presetsOpenSearch = preset.name; model.openPanel(.presets)
            return
        }
        let layer = model.primarySelection
        switch hit.id {
        case "split": model.splitAtPlayhead(Array(model.selection))
        case "duplicate": model.engine.duplicateLayers(model.selection.map { NSNumber(value: $0) }); model.refreshModel(force: true)
        case "ripple_delete": model.deleteSelectedLayers(ripple: true)
        case "trim_start": if let layer { model.trimStart(layer, at: model.status.playhead) }
        case "trim_end": if let layer { model.trimEnd(layer, at: model.status.playhead) }
        case "speed": model.openPanel(.speed)
        case "freeze":
            if let layer {
                let created = model.engine.freezeFrame(forLayer: layer, frame: Int32(clamping: model.status.playhead), hold: Int32(max(1, model.compositionFps * 3)))
                model.refreshModel(force: true); if created >= 0 { model.select(layerId: created) }
            }
        case "extract_audio": if let layer { _ = model.engine.extractAudio(fromLayer: layer); model.refreshModel(force: true) }
        case "audio": model.openPanel(.audio)
        case "beats": model.detectBeats()
        case "transform": model.openPanel(.transform)
        case "text": model.openPanel(.text)
        case "mask": model.openPanel(.mask)
        case "effects": model.openPanel(.effects)
        case "appearance": model.openPanel(.appearance)
        case "tracking": model.openPanel(.tracking)
        case "environment": model.openPanel(.layer3D)
        case "particles": model.openPanel(.particles)
        case "vector": model.openPanel(.vector)
        case "presets": model.openPanel(.presets)
        case "precompose": model.groupSelection()
        case "enter_precomp": if let layer { model.openGroup(layer) }
        case "marker": model.toggleMarker()
        case "magnetic": model.toggleEditMode()
        case "remove_gaps": model.removeGaps()
        case "previous_frame": model.step(-1)
        case "next_frame": model.step(1)
        case "add_text": model.addText()
        case "add_null": model.addNull(threeD: false)
        case "add_null3d": model.addNull(threeD: true)
        case "add_camera": model.addCamera()
        case "captions": model.openPanel(.captions)
        case "project_settings": model.showProjectSettings = true
        case "search_layers": shell.sheet = .searchLayers
        case "undo": model.undo()
        case "redo": model.redo()
        default: break
        }
    }
}
