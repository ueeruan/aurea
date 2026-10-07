// Port of EffectsPanel.kt. Project data stays in the core.
// Redesenho 2026-10-01: a pilha é `[trilho ‹ ◇ curva … ⋯] [cartões]` — no topo as
// FERRAMENTAS-EFEITO que a camada usa (máscaras, rastreio de câmera, legendas),
// depois um cartão compacto por efeito (`≡ · prévia · Nome · 👁 · ⌄`). O catálogo
// é a tela cheia "Adicionar efeito" (EffectAddSheet, EffectsBrowser.swift).
// Camada sem nada abre direto nela; com efeito, na pilha com o 1º cartão aberto.
import SwiftUI
import UIKit
import UniformTypeIdentifiers

@MainActor
struct EffectsView: View {
    var focusedType: UInt32? = nil
    var embedded = false
    @EnvironmentObject private var model: AureaModel
    @StateObject private var prefs = FxEffectPrefs()
    @State private var maskCount = 0
    @State private var hasMatte = false
    @State private var captionCount = 0
    @State private var cameraTracked = false
    @State private var about: EffectCatalogItem?
    @State private var pendingPick: UInt32?
    @State private var importingAM = false
    @State private var importTask: Task<Void, Never>?
    @State private var creatorMode = -1
    @State private var openId: UInt32?
    @State private var known: Set<UInt32> = []
    @State private var loadedLayer: Int64?
    @State private var selected: EffectParamSelection?
    @State private var advanced: Set<UInt32> = []
    @State private var expressionLooks: [UInt32: ExpressionLook] = [:]
    @State private var dragId: UInt32?
    @State private var dragOffset: CGFloat = 0
    @State private var lastTranslation: CGFloat = 0
    @State private var dragOrder: [UInt32]?
    @State private var cardFrames: [UInt32: CGRect] = [:]
    private var ordered: [EffectItem] {
        let byId = Dictionary(uniqueKeysWithValues: model.effects.map { ($0.effectId, $0) })
        return (dragOrder ?? model.effects.map(\.effectId)).compactMap { byId[$0] }
            .filter { focusedType == nil || $0.typeId == focusedType }
    }
    private var selectedParam: EffectParamItem? {
        guard let selected, selected.effect == openId else { return nil }
        return model.effectParams.first { $0.index == selected.param }
    }
    /// Sem [focusedType] é o painel inteiro, com abas; com ele, o editor embutido.
    private var tabbed: Bool { focusedType == nil }
    private var hasAudio: Bool { ((model.detail["audioFlags"] as? NSNumber)?.uint32Value ?? 0) & 4 != 0 }
    private var canAnimate: Bool { selectedParam.map { $0.flags & 1 != 0 && fxComponentCount(Int($0.type)) > 0 } ?? false }
    private var railLook: KeyframeLook {
        guard let selected else { return .none }
        return look(effect: selected.effect, param: selected.param)
    }
    private var selectedTrack: [KeyframeItem] {
        guard let selected, let layer = model.primarySelection else { return [] }
        return (model.keyframes[layer] ?? []).filter { $0.property == 31 && $0.effectIndex == selected.effect && $0.paramIndex == selected.param * 4 + UInt32(selected.component) }.sorted { $0.time < $1.time }
    }
    var body: some View {
        VStack(spacing: 0) {
            // O título da seção ("Efeitos") mora na barra de cima; o editor
            // embutido fora dela (letras do texto 3D) mantém o próprio cabeçalho.
            if !embedded && !tabbed {
                PanelHeader(title: AureaText.t("panel_efeitos"), onBack: { model.panel = .none })
            }
            HStack(spacing: 0) {
                rail
                ScrollViewReader { proxy in
                ScrollView {
                    VStack(spacing: 0) {
                        if tabbed { customEffects }
                        if tabbed && ordered.isEmpty && tools.isEmpty { PanelNotice(AureaText.t("effects_applied_empty")) }
                        ForEach(tools, id: \.key) { tool in toolCard(tool).padding(.bottom, 8) }
                        ForEach(ordered) { effect in
                            card(effect).padding(.bottom, 8).id(effect.effectId)
                                .background(GeometryReader { geometry in
                                    Color.clear.preference(key: EffectCardFrames.self, value: [effect.effectId: geometry.frame(in: .named("effect-stack"))])
                                })
                                .offset(y: dragId == effect.effectId ? dragOffset : 0)
                                .zIndex(dragId == effect.effectId ? 1 : 0)
                        }
                        if focusedType == nil { footer }
                        else if ordered.isEmpty, let type = focusedType {
                            Button(AureaText.t("t3d_enable_letters")) {
                                guard let layer = model.primarySelection else { return }
                                model.mutate { $0.addEffect(type, toLayer: layer, at: UInt32.max) }
                            }.frame(minHeight: 48)
                        }
                    }.padding(.init(top: 8, leading: 8, bottom: 16, trailing: 6))
                }.coordinateSpace(name: "effect-stack")
                    .accessibilityIdentifier("aurea.effects.stack")
                    .onPreferenceChange(EffectCardFrames.self) { cardFrames = $0 }
                    // Depois do enterLayer (onAppear do painel): a pilha agora está
                    // sempre montada e o onAppear dela pode vir ANTES — aí o
                    // enterLayer fechava o cartão novo e abria o 1º (busca de comandos).
                    .onAppear { DispatchQueue.main.async { revealAdded(ordered.map(\.effectId), proxy: proxy) } }
                    .onChange(of: ordered.map(\.effectId)) { ids in revealAdded(ids, proxy: proxy) }
                }
            }.frame(maxHeight: .infinity)
        }
        .background(ParamRowColors.panel)
        .onAppear { refreshTools(); enterLayer(); model.refreshSelectedLayer(); refreshExpressions() }
        .onChange(of: model.primarySelection) { _ in refreshTools(); enterLayer() }
        .onChange(of: selected) { _ in updateTimelineFocus() }
        .onAppear { updateTimelineFocus() }
        .onDisappear { model.timelineFocus = nil }
        .onChange(of: model.effects.map(\.effectId)) { ids in
            let added = Set(ids).subtracting(known); known = Set(ids)
            if let id = model.requestedEffectFocusId ?? ids.last(where: { added.contains($0) }) {
                // Adicionou (catálogo, busca geral, colar): os controles do novo
                // efeito aparecem na pilha.
                pendingPick = nil
                open(id)
                if model.requestedEffectFocusId == id { model.pendingEffectFocus = nil }
            }
            else if let openId, !ids.contains(openId) { closeCard() }
        }
        .task(id: pendingPick) {
            // Render lento não é recusa: depois de 3 s relê o modelo e libera
            // um novo toque (sem isso, dois toques rápidos adicionariam dois).
            guard pendingPick != nil else { return }
            do { try await Task.sleep(nanoseconds: 3_000_000_000) } catch { return }
            model.refreshModel(force: true)
            pendingPick = nil
        }
        .sheet(item: $about) { entry in EffectAboutSheet(prefs: prefs, entry: entry).environmentObject(model) }
        .onChange(of: model.status.modelRevision) { _ in refreshExpressions(); refreshTools() }
        .onChange(of: model.status.playhead) { _ in refreshExpressions() }
        // Alight Motion: .xml/.amproj/.zip não têm tipo padrão; o motor reconhece pelo conteúdo.
        .fileImporter(isPresented: $importingAM, allowedContentTypes: [.data, .xml, .zip]) { result in
            guard case .success(let url) = result else { return }
            let target = model.primarySelection
            importTask?.cancel()
            importTask = Task {
                let data = await Task.detached(priority: .userInitiated) { () -> Data? in
                    let scoped = url.startAccessingSecurityScopedResource()
                    defer { if scoped { url.stopAccessingSecurityScopedResource() } }
                    guard let stream = InputStream(url: url) else { return nil }
                    stream.open()
                    defer { stream.close() }
                    var result = Data()
                    var buffer = [UInt8](repeating: 0, count: 8192)
                    while true {
                        let count = stream.read(&buffer, maxLength: buffer.count)
                        if count < 0 { return nil }
                        if count == 0 { return result }
                        guard count <= 64 * 1024 * 1024 - result.count else { return nil }
                        result.append(contentsOf: buffer.prefix(count))
                    }
                }.value
                guard !Task.isCancelled else { return }
                if let data, model.primarySelection == target { model.importAlightMotion(data) }
                else { model.toast = AureaText.t("am_import_failed", AureaText.t("am_import_read_failed")) }
            }
        }
        .onDisappear { importTask?.cancel(); importTask = nil; finishReorder(); model.endGesture() }
    }
    /// O TRILHO (redesenho 2026-09-29), sempre à vista na pilha: ‹ volta às seções
    /// da camada; ◇+ e a curva miram o parâmetro escolhido (apagados sem cartão
    /// aberto); ⋯ no pé = ações da pilha inteira. O "=" saiu do trilho: a
    /// expressão mora no ••• do cartão e no toque longo do rótulo.
    private var rail: some View {
        LeftRail(keyframeLook: railLook,
                 onKeyframe: canAnimate ? { toggleSelectedKey() } : nil,
                 curveAnimated: railLook != .none,
                 onCurve: selectedTrack.count >= 2 ? { openCurve() } : nil,
                 onMore: { listMenu() },
                 onBack: { model.panel = .none })
    }
    private func enterLayer() {
        guard loadedLayer != model.primarySelection else { return }
        loadedLayer = model.primarySelection; known = Set(model.effects.map(\.effectId))
        closeCard(); advanced = []; expressionLooks = [:]; pendingPick = nil
        if let requested = model.requestedEffectFocusId {
            open(requested); model.pendingEffectFocus = nil; return
        }
        if let type = focusedType, let effect = model.effects.first(where: { $0.typeId == type }) {
            open(effect.effectId); return
        }
        guard model.curveProperty == 31, model.curveSelectedTime != nil,
              model.effects.contains(where: { $0.effectId == model.curveEffect }) else {
            // A pilha já abre com o primeiro cartão à vista (um toque a menos);
            // os outros recolhidos (acordeão: um aberto por vez). Camada sem
            // nada: direto na tela "Adicionar efeito".
            if tabbed, let first = model.effects.first { open(first.effectId) }
            else if tabbed && tools.isEmpty && model.primarySelection != nil { DispatchQueue.main.async { showAdd() } }
            return
        }
        // Entrar pelo losango de um parâmetro é editar: abre na pilha.
        let entry = EffectParamSelection(effect: model.curveEffect, param: model.curveParam / 4, component: Int(model.curveParam % 4))
        selected = entry; open(entry.effect)
        if parameterGroups(entry.effect).rest.contains(where: { $0.index == entry.param }) { advanced.insert(entry.effect) }
    }
    /// Efeito novo na camada (busca, navegador, colar): abre o cartão e rola
    /// até ele — paridade com o Android. Os ids vistos ficam no MODELO: o painel
    /// recriado (depois da busca de comandos) ainda reconhece o que é novo.
    private func revealAdded(_ ids: [UInt32], proxy: ScrollViewProxy) {
        guard let layer = model.primarySelection else { return }
        let seen: Set<UInt32>? = model.seenEffectIds[layer]
        model.seenEffectIds[layer] = Set(ids)
        guard let seen, let added = ids.last(where: { !seen.contains($0) }) else { return }
        open(added)
        DispatchQueue.main.async {
            withAnimation(.easeOut(duration: 0.25)) { proxy.scrollTo(added, anchor: .top) }
        }
    }
    private func open(_ id: UInt32) {
        guard let layer = model.primarySelection else { return }
        openId = id; model.loadParams(layerId: layer, effectId: id)
        if selected?.effect != id {
            selected = parameterGroups(id).main.first(where: { fxComponentCount(Int($0.type)) > 0 }).map { EffectParamSelection(effect: id, param: $0.index, component: 0) }
        }
        refreshExpressions()
    }
    private func closeCard() { openId = nil; selected = nil; model.selectedEffectId = nil }
    private func currentParam(_ index: UInt32) -> EffectParamItem? { model.effectParams.first { $0.index == index } }
    private func slot(_ param: EffectParamItem) -> FxParamSlot {
        FxParamSlot(index: Int(param.index), type: Int(param.type), min: param.minValue, max: param.maxValue, label: param.label, unit: param.unit, enumLabels: param.enumLabels)
    }
    private func parameterGroups(_ id: UInt32) -> (main: [EffectParamItem], rest: [EffectParamItem]) {
        let visible = model.effectParams.filter { $0.flags & 16 == 0 }
        let type = model.effects.first { $0.effectId == id }?.typeId ?? 0
        let groups = fxSplitPrincipal(type, visible.map { slot($0) })
        let byIndex = Dictionary(uniqueKeysWithValues: visible.map { (Int($0.index), $0) })
        return (groups.main.compactMap { byIndex[$0.index] }, groups.rest.compactMap { byIndex[$0.index] })
    }
    private func display(_ param: EffectParamItem, effectId: UInt32) -> FxParamDisplay {
        fxParamDisplay(model.effects.first { $0.effectId == effectId }?.typeId ?? 0, slot(param))
    }
    /// O CARTÃO COMPACTO (EffectsPanel.kt `FxStackCard`, redesenho 2026-10-01): raio
    /// 10. Recolhido: `≡ · prévia · Nome / grupo · 👁 · ⌄`; aberto: `≡ · prévia ·
    /// Nome · ••• · 🗑 · ⌃` e o corpo (linhas de 40 com vão de 4). O ≡ arrasta para
    /// reordenar; tocar no nome abre/fecha. Ids `effects.expand|eye|more|remove.<id>`.
    private func card(_ effect: EffectItem) -> some View {
        let expanded = openId == effect.effectId, lifted = dragId == effect.effectId
        let name = fxEffectDisplayName(effect.typeId, effect.name)
        let id = fxEffectCardId(effect.typeId)
        let entry = model.effectCatalog.first { $0.typeId == effect.typeId }
        let subtitle = !effect.enabled ? AureaText.t("fxui_effect_off") : entry.map { fxEffectGroupOf($0).label } ?? ""
        let toggleLabel = AureaText.t(expanded ? "fxui_a11y_collapse" : "fxui_a11y_expand", name)
        let toggle = { expanded ? closeCard() : open(effect.effectId) }
        return VStack(spacing: 0) {
            HStack(spacing: 0) {
                CupertinoGlyph.text(CupertinoGlyph.LineHorizontal3, size: 16, color: lifted ? AureaColors.accent : AureaColors.muted)
                    .frame(width: 32, height: 56).contentShape(Rectangle())
                    .highPriorityGesture(DragGesture(minimumDistance: 4).onChanged { reorder(effect, value: $0) }.onEnded { _ in finishReorder() })
                    .accessibilityLabel(AureaText.t("fxui_a11y_drag", name))
                HStack(spacing: 10) {
                    EffectStackThumb(typeId: effect.typeId, category: entry?.category ?? "", store: model.effectPreviews)
                        .opacity(effect.enabled ? 1 : 0.45)
                    VStack(alignment: .leading, spacing: 1) {
                        Text(name).font(.aurea(size: 15, weight: .semibold)).foregroundStyle(AureaColors.text)
                            .lineLimit(1).truncationMode(.tail).opacity(effect.enabled ? 1 : 0.45)
                        if !subtitle.isEmpty {
                            Text(subtitle).font(.aurea(size: 11)).foregroundStyle(AureaColors.muted).lineLimit(1)
                        }
                    }
                    Spacer(minLength: 0)
                }
                .frame(maxWidth: .infinity).frame(height: 56).contentShape(Rectangle())
                .onTapGesture { toggle() }
                .accessibilityElement(children: .contain)
                .accessibilityAddTraits(.isButton)
                .accessibilityHint(toggleLabel)
                .accessibilityAction { toggle() }
                .accessibilityIdentifier("effects.expand." + id)
                if expanded {
                    cardButton(CupertinoGlyph.Ellipsis) { effectMenu(effect) }
                        .accessibilityLabel(AureaText.t("app_a11y_more_options", name))
                        .accessibilityIdentifier("effects.more." + id)
                    cardButton(CupertinoGlyph.Trash) { remove(effect) }
                        .accessibilityLabel(AureaText.t("fxui_a11y_remove", name))
                        .accessibilityIdentifier("effects.remove." + id)
                } else {
                    cardButton(effect.enabled ? CupertinoGlyph.Eye : CupertinoGlyph.EyeSlash, tint: effect.enabled ? AureaColors.text : AureaColors.muted) { enable(effect, !effect.enabled) }
                        .accessibilityLabel(AureaText.t(effect.enabled ? "fxui_a11y_disable" : "fxui_a11y_enable", name))
                        .accessibilityIdentifier("effects.eye." + id)
                }
                cardButton(expanded ? CupertinoGlyph.ChevronUp : CupertinoGlyph.ChevronDown, tint: AureaColors.muted) { toggle() }
                    .accessibilityLabel(toggleLabel)
            }.padding(.trailing, 4).frame(height: 56)
            if expanded {
                VStack(spacing: 4) {
                    if !effect.known { PanelNotice(AureaText.t("panel_este_efeito_saiu_catalogo_ele_nao")) }
                    else if effect.typeId == fxEffectTypeId("aurea.time.remap") { TimeRemapEffectEditor(effectId: effect.effectId) }
                    else {
                        if effect.typeId == fxEffectTypeId("aurea.key.rotobrush") {
                            PanelNotice(AureaText.t("roto_note"))
                        }
                        let localAiBit: UInt32 = effect.typeId == fxEffectTypeId("aurea.ai.depth_map") ? 1 : effect.typeId == fxEffectTypeId("aurea.key.rotobrush") ? 2 : 0
                        if effect.enabled && localAiBit != 0 {
                            if model.localAiActivity & localAiBit != 0 {
                                PanelNotice(AureaText.t("local_ai_processing"))
                                    .accessibilityIdentifier("effects.local_ai.processing")
                            } else if model.localAiActivity & (localAiBit << 2) != 0 {
                                PanelNotice(AureaText.t("local_ai_failed"))
                                    .accessibilityIdentifier("effects.local_ai.failed")
                            }
                        }
                        let groups = parameterGroups(effect.effectId)
                        if groups.main.isEmpty && groups.rest.isEmpty { PanelNotice(AureaText.t("panel_este_efeito_nao_tem_ajustes")) }
                        // EQ paramétrico: o gráfico da resposta em cima das bandas.
                        if effect.typeId == fxEffectTypeId("aurea.audio.parametric_eq") {
                            FxEqResponseGraph(values: (0..<12).map { i in model.effectParams.first { $0.index == UInt32(i) }?.scalar ?? 0 })
                        }
                        // Presets do efeito (o do app antigo: Impacto/Na mão/Glitch do
                        // Tremor): fichas no topo do cartão; tocar = um passo de desfazer.
                        let presets = model.engine.effectPresets(effect.typeId)
                        if presets.count >= 2 {
                            FxPresetRow(presets: presets) { preset in
                                guard let layer = model.primarySelection else { return }
                                if model.engine.applyEffectPreset(UInt32(preset), effect: effect.effectId, forLayer: layer) { model.refreshModel(force: true) }
                            }
                        }
                        ForEach(groups.main) { param in parameter(param, effect: effect.effectId) }
                        if !groups.rest.isEmpty {
                            AdvancedToggle(open: advanced.contains(effect.effectId), count: groups.rest.count) {
                                if advanced.contains(effect.effectId) { advanced.remove(effect.effectId) } else { advanced.insert(effect.effectId) }
                            }
                            if creatorMode == 1 || advanced.contains(effect.effectId) { ForEach(groups.rest) { param in parameter(param, effect: effect.effectId) } }
                        }
                    }
                }.padding(.horizontal, 6).opacity(effect.enabled ? 1 : 0.45)
            }
        }.padding(.bottom, expanded ? 6 : 0)
            .background(lifted ? AureaColors.surfaceHigh : ParamRowColors.card, in: RoundedRectangle(cornerRadius: 10))
            .overlay { if lifted { RoundedRectangle(cornerRadius: 10).stroke(AureaColors.accent, lineWidth: 1) } }
    }
    private func cardButton(_ glyph: Character, tint: Color = AureaColors.text, action: @escaping () -> Void) -> some View {
        Button(action: action) { CupertinoGlyph.text(glyph, size: 19, color: tint).frame(width: 40, height: 40).contentShape(Rectangle()) }.buttonStyle(AureaPressStyle())
    }
    // --- Ferramentas-efeito na pilha (máscaras, rastreio de câmera, legendas) ---
    /// As ferramentas que a camada usa, na ordem dos cartões.
    private var tools: [FxEffectTool] {
        guard tabbed else { return [] }
        var out: [FxEffectTool] = []
        if maskTool { out.append(.mask) }
        if cameraTracked { out.append(.cameraTrack) }
        if captionCount > 0 { out.append(.captions) }
        return out
    }
    /// A Máscara posta pelo catálogo fica na pilha mesmo vazia (o painel fechou
    /// sem caminho): o cartão é a volta para desenhar. Sai só pelo 🗑 dele.
    private var maskTool: Bool {
        maskCount > 0 || hasMatte || (model.primarySelection.map { model.maskToolLayers.contains($0) } ?? false)
    }
    /// Relê no motor o que a camada tem de cada ferramenta. O rastreio guardado
    /// é restaurado como o painel de rastreio faz ao aparecer.
    private func refreshTools() {
        guard tabbed, let id = model.primarySelection else { maskCount = 0; hasMatte = false; captionCount = 0; cameraTracked = false; return }
        maskCount = fxMaskIds(model.engine.maskData(id)).count
        let matte = model.engine.trackMatte(id)
        hasMatte = matte.count > 1 && matte[0].int64Value != 0 && matte[1].intValue != 0
        captionCount = Int(model.engine.captionCount(id))
        cameraTracked = model.selectedLayer?.kind == 1 && model.engine.restoreCameraTrack(forLayer: id)
    }
    /// O CARTÃO DE UMA FERRAMENTA: `glifo · Nome / quantos · 🗑 · ›`. Tocar reabre a
    /// ferramenta; a lixeira pede confirmação. Id `effects.tool.<chave>`.
    private func toolCard(_ tool: FxEffectTool) -> some View {
        let subtitle: String
        switch tool {
        case .mask:
            subtitle = maskCount > 0 ? AureaText.t("fxui_tool_masks_n", maskCount)
                : hasMatte ? AureaText.t("panel_recorte_outra_camada") : AureaText.t("fxui_tool_mask_empty")
        case .captions: subtitle = AureaText.t("fxui_tool_captions_n", captionCount)
        case .cameraTrack: subtitle = AureaText.t("fxui_tool_camera_ready")
        }
        return HStack(spacing: 0) {
            HStack(spacing: 10) {
                FxToolPlate(glyph: tool.glyph, glyphSize: 20).frame(width: 40, height: 40)
                    .clipShape(RoundedRectangle(cornerRadius: 8))
                VStack(alignment: .leading, spacing: 1) {
                    Text(tool.label).font(.aurea(size: 15, weight: .semibold)).foregroundStyle(AureaColors.text).lineLimit(1)
                    Text(subtitle).font(.aurea(size: 11)).foregroundStyle(AureaColors.muted).lineLimit(1)
                }
                Spacer(minLength: 0)
            }
            .padding(.leading, 10).frame(maxWidth: .infinity).frame(height: 56).contentShape(Rectangle())
            .onTapGesture { fxOpenEffectTool(tool, in: model) }
            .accessibilityElement(children: .combine)
            .accessibilityAddTraits(.isButton)
            .accessibilityHint(AureaText.t("fxui_a11y_open", tool.label))
            .accessibilityAction { fxOpenEffectTool(tool, in: model) }
            .accessibilityIdentifier("effects.tool." + (tool.key.split(separator: ".").last.map(String.init) ?? ""))
            if tool != .cameraTrack {
                cardButton(CupertinoGlyph.Trash) { confirmRemoveTool(tool) }
                    .accessibilityLabel(AureaText.t("fxui_a11y_remove", tool.label))
            }
            CupertinoGlyph.text(CupertinoGlyph.ChevronRight, size: 14, color: AureaColors.muted).mirrorsInRtl().frame(width: 36, height: 40)
                .accessibilityHidden(true)
        }.padding(.trailing, 4).frame(height: 56)
            .background(ParamRowColors.card, in: RoundedRectangle(cornerRadius: 10))
    }
    private func confirmRemoveTool(_ tool: FxEffectTool) {
        let question = AureaText.t(tool == .mask ? "fxui_tool_remove_masks_q" : "fxui_tool_remove_captions_q")
        model.actionSheet = ActionSheetRequest(title: question, items: [(AureaText.t("panel_remover"), { removeTool(tool) })])
    }
    /// TIRA a ferramenta da camada: todas as máscaras (um passo de desfazer) ou as
    /// legendas geradas. O rastreio de câmera não tem "tirar" (paridade Android).
    private func removeTool(_ tool: FxEffectTool) {
        guard let id = model.primarySelection, model.selectedLayer?.locked != true else { return }
        switch tool {
        case .mask:
            // Tirar o cartão tira tudo o que ele mostra: as máscaras e o recorte por outra camada.
            model.dropMaskTool(id)
            let ids = fxMaskIds(model.engine.maskData(id))
            let matte = (model.engine.trackMatte(id).first?.int64Value ?? 0) != 0
            guard !ids.isEmpty || matte else { refreshTools(); return }
            model.beginGesture("remover máscaras")
            for mask in ids { _ = model.engine.removeMask(id, mask: mask) }
            if matte { model.engine.setTrackMatteForLayer(id, matte: 0, mode: 0) }
            model.endGesture()
            model.selectedMask = nil; model.selectedMaskPoint = nil; model.maskDrawing = false
        case .captions:
            model.engine.removeCaptions(id)
        case .cameraTrack:
            return
        }
        model.refreshModel(force: true); refreshTools()
    }
    @ViewBuilder private func parameter(_ param: EffectParamItem, effect: UInt32) -> some View {
        switch Int(param.type) {
        case fxParamFloat, fxParamInt, fxParamAngle: numberRow(param, effect: effect, component: 0)
        case fxParamPoint2D, fxParamPoint3D:
            ForEach(0..<fxComponentCount(Int(param.type)), id: \.self) { component in numberRow(param, effect: effect, component: component) }
        case fxParamBool:
            // O interruptor mora no lugar da caixa de valor, à direita.
            customRow(param, effect: effect) {
                HStack(spacing: 0) { Spacer(minLength: 0); AureaToggle(checked: param.scalar >= 0.5) { on in
                    selected = EffectParamSelection(effect: effect, param: param.index, component: 0)
                    writeComponent(param.index, effect: effect, component: 0, value: on ? 1 : 0)
                }
                .accessibilityLabel(display(param, effectId: effect).label)
                .accessibilityValue(AureaText.t(param.scalar >= 0.5 ? "common_on" : "common_off"))
                .accessibilityIdentifier("effects.toggle.\(effect).\(param.index)")
                Spacer().frame(width: 6) }
            }
        case fxParamEnum:
            customRow(param, effect: effect) { choiceBox(param, effect: effect) }
        case fxParamColor:
            customRow(param, effect: effect) { colorControl(param, effect: effect) }
        case fxParamTextureRef:
            if let layer = model.primarySelection { CubeLutImportRow(layer: layer, effect: effect) }
        case fxParamLayerRef:
            // Outra camada ("Camada de áudio"): o menu lista as camadas da
            // composição; o motor recebe o ÍNDICE da camada (−1 = nenhuma).
            customRow(param, effect: effect) {
                HStack { Spacer(minLength: 0)
                    Menu {
                        Button(AureaText.t("afx_layer_none")) { writeLayerRef(param.index, effect: effect, layerIndex: -1) }
                        ForEach(model.layers.filter { $0.id != model.primarySelection }) { layer in
                            Button(layer.name.isEmpty ? "#\(fxLayerIndex(layer.id))" : layer.name) {
                                writeLayerRef(param.index, effect: effect, layerIndex: fxLayerIndex(layer.id))
                            }
                        }
                    } label: {
                        Text(layerRefName(param)).font(.aurea(size: 13)).foregroundStyle(AureaColors.accent).lineLimit(1)
                            .padding(.horizontal, 6).padding(.vertical, 8)
                    }
                    Spacer().frame(width: 4)
                }
            }
        default:
            customRow(param, effect: effect) { Text(AureaText.t("panel_ainda_nao_editavel_app")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted) }
        }
    }
    /// Escolha (`EffectChoiceRow`): a caixa de valor (a opção + ▾) ocupa o resto da
    /// linha e abre a lista das opções; a atual vem marcada com "✓".
    private func choiceBox(_ param: EffectParamItem, effect: UInt32) -> some View {
        let options = enumOptions(param)
        let top = max(0, options.count - 1)
        let current = param.scalar.isFinite ? Int(param.scalar.rounded().clamped(to: 0...Float(top))) : 0
        let label = display(param, effectId: effect).label
        return Button {
            selected = EffectParamSelection(effect: effect, param: param.index, component: 0)
            let items: [(String, () -> Void)] = options.enumerated().map { (index, option) -> (String, () -> Void) in
                (index == current ? "✓ " + option : option, {
                    writeComponent(param.index, effect: effect, component: 0, value: Float(index))
                })
            }
            model.actionSheet = ActionSheetRequest(title: label, items: items)
        } label: {
            HStack(spacing: 6) {
                Text(options.indices.contains(current) ? options[current] : "")
                    .font(.aurea(size: 13)).foregroundStyle(AureaColors.text)
                    .lineLimit(1).truncationMode(.tail)
                Spacer(minLength: 0)
                CupertinoGlyph.text(CupertinoGlyph.ChevronDown, size: 12, color: AureaColors.muted)
            }
            .padding(.leading, 10).padding(.trailing, 8)
            .frame(maxWidth: .infinity).frame(height: ParamRowDims.labelH)
            .background(ParamRowColors.valueBox, in: RoundedRectangle(cornerRadius: ParamRowDims.radius))
            .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .accessibilityIdentifier("effects.choice.\(effect).\(param.index)")
    }
    /// Cor (`EffectColorRow`): "R G B" (0–255 da cor mostrada) + amostra que abre o
    /// seletor; a folha inteira = UM passo de desfazer.
    private func colorControl(_ param: EffectParamItem, effect: UInt32) -> some View {
        let shown = AureaColorSpace.engineToDisplay(param.value)
        let rgb = (0..<3).map { index -> String in
            let v = index < shown.count && shown[index].isFinite ? shown[index].clamped(to: 0...1) : 0
            return String(Int((v * 255).rounded()))
        }.joined(separator: " ")
        let label = display(param, effectId: effect).label
        return HStack(spacing: 0) {
            Spacer(minLength: 0)
            Text(rgb)
                .font(.aurea(size: 12).monospacedDigit())
                .foregroundStyle(ParamRowColors.rgbText)
                .lineLimit(1)
            Spacer().frame(width: 8)
            Button {
                let target = EffectParamSelection(effect: effect, param: param.index, component: 0)
                selected = target; model.beginGesture("cor")
                model.colorSheet = ColorSheetRequest(title: label,
                    initial: AureaColorSpace.engineToDisplay(param.value), onChange: { r, g, b, a in
                        writeVector(param.index, effect: effect, values: AureaColorSpace.displayToEngine(r, g, b, a))
                    }, onDone: { model.endGesture() })
            } label: {
                AureaColorSwatch(color: color(param.value))
                    .frame(width: 34, height: 30)
                    .clipShape(RoundedRectangle(cornerRadius: 5))
                    .overlay(RoundedRectangle(cornerRadius: 5).stroke(ParamRowColors.swatchBorder, lineWidth: 1))
                    .frame(minHeight: ParamRowDims.row)
                    .contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle())
            .accessibilityLabel(label)
            .accessibilityIdentifier("effects.color.\(effect).\(param.index)")
            Spacer().frame(width: 8)
        }
    }
    private func layerRefName(_ param: EffectParamItem) -> String {
        let chosen = Int(param.scalar.rounded())
        guard chosen >= 0, let layer = model.layers.first(where: { fxLayerIndex($0.id) == chosen }), !layer.name.isEmpty else {
            return AureaText.t("afx_layer_none")
        }
        return layer.name
    }
    private func writeLayerRef(_ index: UInt32, effect: UInt32, layerIndex: Int) {
        guard let layer = model.primarySelection, model.selectedLayer?.locked != true else { return }
        selected = EffectParamSelection(effect: effect, param: index, component: 0)
        model.engine.setEffect(effect, forLayer: layer, paramIndex: index, value: Float(layerIndex))
        model.commitPendingCommands(); model.refreshSelectedLayer()
    }
    private func enumOptions(_ param: EffectParamItem) -> [String] {
        if !param.enumLabels.isEmpty { return param.enumLabels }
        let span = param.maxValue - param.minValue
        let count = span.isFinite ? max(1, min(256, Int(span.clamped(to: 0...255).rounded()) + 1)) : 1
        return (0..<count).map { String($0 + 1) }
    }
    private func numberRow(_ param: EffectParamItem, effect: UInt32, component: Int) -> some View {
        let d = display(param, effectId: effect), key = EffectParamSelection(effect: effect, param: param.index, component: component)
        let label = Int(param.type) == fxParamPoint2D || Int(param.type) == fxParamPoint3D ? axisLabel(d.label, component: component) : d.label
        // Duas faixas (EffectsPanel.kt): a RÉGUA anda na do slider (minValue...maxValue);
        // o TECLADO aceita a digitada (hardMin...hardMax), que o motor impõe. A
        // escrita prende só na digitada — um valor digitado além da régua fica.
        let lo = d.toDisplay(param.minValue), hi = d.toDisplay(param.maxValue)
        let typedMin: Float = Swift.min(param.hardMin, param.minValue)
        let typedMax: Float = Swift.max(param.hardMax, param.maxValue)
        let typedLo = d.toDisplay(typedMin), typedHi = d.toDisplay(typedMax)
        let range = param.maxValue - param.minValue
        let step: Float = Int(param.type) == fxParamAngle ? 0.5 : range.isFinite && range > 0 ? min(range / 500, 2) : 0.5
        let shown = d.toDisplay(component < param.value.count ? param.value[component] : 0)
        let send: (Float) -> Void = { value in
            let coreValue = min(typedMax, max(typedMin, d.toEngine(value)))
            writeComponent(param.index, effect: effect, component: component, value: Int(param.type) == fxParamInt ? coreValue.rounded() : coreValue)
        }
        return EffectNumberRow(label: label, value: shown, unitsPerPoint: step * d.scale, minimum: min(lo, hi), maximum: max(lo, hi), suffix: d.suffix, decimals: d.decimals,
                               identifier: "effects.param.\(effect).\(param.index).\(component)",
                               selected: selected == key, look: look(effect: effect, param: param.index, component: component), expression: expressionLooks[param.index] ?? .none,
                               onSelect: { selected = key }, onMenu: { paramMenu(param, effect: effect) },
                               onBegin: { model.beginGesture("ajustar " + label) }, onValue: send, onEnd: { model.endGesture() }, onKeypad: {
                                   selected = key
                                   // "50%" continua sendo 50 % do fim da RÉGUA (não do teto digitado).
                                   model.numericKeypad = KeypadRequest(title: label, value: shown, unit: d.suffix, min: min(typedLo, typedHi), max: max(typedLo, typedHi),
                                                                       decimals: d.decimals, percentBase: max(lo, hi), onValue: send)
                               })
    }
    private func customRow<Content: View>(_ param: EffectParamItem, effect: UInt32, @ViewBuilder content: () -> Content) -> some View {
        let key = EffectParamSelection(effect: effect, param: param.index, component: 0)
        return HStack(spacing: ParamRowDims.gap) {
            EffectPropertyLabel(label: display(param, effectId: effect).label, selected: selected?.effect == effect && selected?.param == param.index,
                                look: look(effect: effect, param: param.index), expression: expressionLooks[param.index] ?? .none,
                                onSelect: { selected = key }, onMenu: { paramMenu(param, effect: effect) })
            content().frame(maxWidth: .infinity, alignment: .leading)
        }.frame(minHeight: ParamRowDims.row)
    }
    private func axisLabel(_ label: String, component: Int) -> String {
        let cuts = [" do ", " da ", " dos ", " das "].compactMap { label.range(of: $0)?.lowerBound }
        let shortened = cuts.min().map { String(label[..<$0]) } ?? label
        return shortened + " " + ["X", "Y", "Z"][component]
    }
    private func look(effect: UInt32, param: UInt32, component: Int? = nil) -> KeyframeLook {
        guard let layer = model.primarySelection else { return .none }
        let keys = (model.keyframes[layer] ?? []).filter { $0.property == 31 && $0.effectIndex == effect && $0.paramIndex / 4 == param && (component == nil || $0.paramIndex % 4 == UInt32(component!)) }
        return keys.contains(where: { $0.time == model.localPlayhead }) ? .keyHere : keys.isEmpty ? .none : .animated
    }
    private func refreshExpressions() {
        guard let layer = model.primarySelection, let effect = openId else { expressionLooks = [:]; return }
        var looks: [UInt32: ExpressionLook] = [:]
        for param in model.effectParams where param.flags & 1 != 0 {
            var result = ExpressionLook.none
            for component in 0..<fxComponentCount(Int(param.type)) {
                let info = model.engine.expression(layer, property: 31, effect: effect, param: param.index * 4 + UInt32(component))
                guard (info["exists"] as? NSNumber)?.boolValue == true else { continue }
                if !(info["error"] as? String ?? "").isEmpty { result = .error; break }
                result = (info["enabled"] as? NSNumber)?.boolValue == true ? .ok : result == .ok ? .ok : .off
            }
            looks[param.index] = result
        }
        expressionLooks = looks
    }
    private func toggleSelectedKey() {
        guard let selection = selected, let param = currentParam(selection.param), let layer = model.primarySelection,
              model.selectedLayer?.locked != true, param.flags & 1 != 0 else { return }
        let here = (model.keyframes[layer] ?? []).filter { $0.property == 31 && $0.effectIndex == selection.effect && $0.paramIndex / 4 == param.index && $0.time == model.localPlayhead }
        model.beginGesture("keyframe de efeito")
        if here.isEmpty {
            for component in 0..<fxComponentCount(Int(param.type)) {
                model.engine.keyParameter(layer, property: 31, effect: selection.effect, param: param.index * 4 + UInt32(component), time: model.localPlayhead, value: component < param.value.count ? param.value[component] : 0)
            }
        } else {
            for key in here { model.engine.editTrackKey(layer, property: 31, effect: key.effectIndex, param: key.paramIndex, time: key.time, action: 1, value: key.value, targetTime: key.time, interpolation: key.interpolation, handles: []) }
        }
        model.endGesture(); model.commitPendingCommands(); model.refreshModel(force: true)
    }
    private func updateTimelineFocus() {
        model.timelineFocus = selected.map { [TimelineTrack(property: 31, effect: $0.effect, param: $0.param * 4 + UInt32($0.component))] } ?? []
    }

    private func openCurve() {
        guard let selected else { return }
        let track = selectedTrack
        guard track.count >= 2 else { return }
        let index = min(track.count - 2, track.lastIndex(where: { $0.time <= model.localPlayhead }) ?? 0)
        let key = track[index]
        model.openCurve(property: 31, effect: selected.effect, param: selected.param * 4 + UInt32(selected.component), time: key.time)
    }
    private func openExpression() {
        guard let selected, let param = selectedParam, let layer = model.primarySelection, canAnimate else { return }
        let d = display(param, effectId: selected.effect)
        let tracks = (0..<fxComponentCount(Int(param.type))).map { ExpressionTrack(property: 31, effect: selected.effect, param: param.index * 4 + UInt32($0)) }
        model.expressionSheet = ExpressionRequest(layer: layer, label: d.label, tracks: tracks, scale: d.scale, unit: d.suffix)
    }
    // Same EditorStore contract: editing one axis creates only its component key.
    private func writeComponent(_ index: UInt32, effect: UInt32, component: Int, value: Float) {
        guard value.isFinite, let layer = model.primarySelection, model.selectedLayer?.locked != true,
              let param = currentParam(index), param.flags & 16 == 0 else { return }
        let count = fxComponentCount(Int(param.type))
        guard component >= 0 && component < count else { return }
        if param.animated {
            model.engine.keyParameter(layer, property: 31, effect: effect, param: index * 4 + UInt32(component), time: model.localPlayhead, value: value)
        } else if count == 1 { model.engine.setEffect(effect, forLayer: layer, paramIndex: index, value: value) }
        else {
            var vector = Array((param.value + [0, 0, 0, 1]).prefix(4)); vector[component] = value
            model.engine.setEffectColor(effect, forLayer: layer, paramIndex: index, r: vector[0], g: vector[1], b: vector[2], a: vector[3])
        }
        model.commitPendingCommands(); model.refreshSelectedLayer()
    }
    /// O vetor INTEIRO num comando só (cor, ponto, reset). Um `writeComponent`
    /// por componente relia o parâmetro entre um e outro — e a leitura logo
    /// depois de escrever ainda vinha com o valor antigo, então o último
    /// comando (velho R, G, B + novo A) desfazia a cor escolhida. Era o
    /// "Text Transform não funciona" da cor de preenchimento no iOS.
    private func writeVector(_ index: UInt32, effect: UInt32, values: [Float]) {
        guard let layer = model.primarySelection, let param = currentParam(index), model.selectedLayer?.locked != true,
              param.flags & 16 == 0, values.allSatisfy(\.isFinite) else { return }
        let count = min(fxComponentCount(Int(param.type)), values.count)
        guard count > 1 else {
            if let first = values.first { writeComponent(index, effect: effect, component: 0, value: first) }
            return
        }
        var vector = Array((param.value + [0, 0, 0, 1]).prefix(4))
        for component in 0..<count { vector[component] = values[component] }
        if param.animated {
            for component in 0..<count {
                model.engine.keyParameter(layer, property: 31, effect: effect, param: index * 4 + UInt32(component), time: model.localPlayhead, value: vector[component])
            }
        } else {
            model.engine.setEffectColor(effect, forLayer: layer, paramIndex: index, r: vector[0], g: vector[1], b: vector[2], a: vector[3])
        }
        model.commitPendingCommands(); model.refreshSelectedLayer()
    }
    private func color(_ values: [Float]) -> Color {
        let v = Array((values + [0, 0, 0, 1]).prefix(4))
        return Color(.sRGBLinear, red: Double(v[0]), green: Double(v[1]), blue: Double(v[2]), opacity: Double(v[3]))
    }
    private func enable(_ effect: EffectItem, _ on: Bool) {
        guard let layer = model.primarySelection, model.selectedLayer?.locked != true else { return }
        model.mutate { $0.setEffect(effect.effectId, forLayer: layer, enabled: on) }; model.refreshSelectedLayer()
    }
    private func move(_ effect: EffectItem, to index: Int) {
        guard let layer = model.primarySelection, model.selectedLayer?.locked != true, model.effects.indices.contains(index) else { return }
        model.mutate { $0.moveEffect(effect.effectId, inLayer: layer, to: UInt32(index)) }; model.refreshSelectedLayer()
    }
    private func remove(_ effect: EffectItem) {
        guard let layer = model.primarySelection, model.selectedLayer?.locked != true else { return }
        model.mutate { $0.removeEffect(effect.effectId, fromLayer: layer) }
        if openId == effect.effectId { closeCard() }
        model.refreshSelectedLayer()
    }
    private func reset(_ effect: EffectItem) {
        guard let layer = model.primarySelection else { return }
        model.loadParams(layerId: layer, effectId: effect.effectId)
        let params = model.effectParams
        model.beginGesture("redefinir efeito")
        for param in params where param.flags & 16 == 0 { writeVector(param.index, effect: effect.effectId, values: param.defaultValue) }
        model.endGesture(); model.refreshSelectedLayer()
    }
    /// Mostra a tela cheia "Adicionar efeito" (apresentada pela raiz do editor);
    /// no editor embutido, o painel Efeitos.
    private func showAdd() {
        guard tabbed else { model.openPanel(.effects); return }
        guard model.primarySelection != nil, model.effectSearch == nil else { return }
        model.effectSearch = EffectSearchRequest(
            prefs: prefs, sorted: fxPickerEntries(model, layerHasAudio: hasAudio), onPick: pick,
            onFavorite: { entry in _ = prefs.toggleFavorite(entry.typeId) }, browse: true,
            onTool: { tool in fxOpenEffectTool(tool, in: model) })
    }
    /// UM toque no cartão: adiciona às camadas escolhidas; a mudança na pilha
    /// (onChange acima) leva à aba "Na camada" com o efeito aberto.
    private func pick(_ entry: EffectCatalogItem) {
        let targets = model.layers.filter { model.selection.contains($0.id) && !$0.locked }
        guard model.started, !targets.isEmpty, pendingPick == nil else { return }
        if let layer = model.primarySelection { model.seenEffectIds[layer] = Set(model.effects.map(\.effectId)) }
        pendingPick = entry.typeId
        prefs.addRecent(entry.typeId)
        model.addCatalogEffect(entry.typeId, layers: targets.map(\.id))
        model.refreshModel(force: true)
    }
    private var customEffects: some View {
        VStack(alignment: .leading, spacing: 8) {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack {
                    Button(AureaText.t("fx_custom_create")) { creatorMode = creatorMode < 0 ? 0 : -1 }.frame(minHeight: 44)
                    Button(AureaText.t("fx_custom_library")) { model.presetsOpenKind = "efeitos"; model.openPanel(.presets) }.frame(minHeight: 44)
                }
            }
            if creatorMode >= 0 {
                Picker(AureaText.t("fx_custom_create"), selection: $creatorMode) {
                    Text(AureaText.t("fx_custom_normal")).tag(0)
                    Text(AureaText.t("fx_custom_advanced")).tag(1)
                }.pickerStyle(.segmented)
                Text(AureaText.t(creatorMode == 0 ? "fx_custom_normal_help" : "fx_custom_advanced_help"))
                    .font(.aurea(size: 13)).foregroundStyle(AureaColors.muted).fixedSize(horizontal: false, vertical: true)
                Button(AureaText.t("fx_custom_save")) {
                    let mode = creatorMode, effect = openId, layer = model.primarySelection
                    model.namePrompt = NamePromptRequest(title: AureaText.t("fx_custom_name"), initial: "") { name in
                        guard !name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty, let layer, layer == model.primarySelection else { return }
                        if mode == 1, let effect { model.saveEffectPreset(effectId: effect, name: name); return }
                        let json = model.engine.savePreset(layer, kind: 0, name: name, parts: 3)
                        guard !json.isEmpty, let saved = model.storeEffectPreset(name: name, json: json) else {
                            model.toast = AureaText.t("msg_nao_foi_possivel_salvar_o_preset"); return
                        }
                        model.toast = AureaText.t("msg_preset_salvo", saved)
                    }
                }.frame(minHeight: 44).disabled(ordered.isEmpty || (creatorMode == 1 && openId == nil))
            }
        }.padding(.vertical, 8)
    }

    private func listMenu() {
        // "Meus presets" saiu (pedido de 2026-10-01: ninguém usa); o motor
        // continua lendo os presets dos projetos antigos.
        var actions: [(String, () -> Void)] = [(AureaText.t("panel_adicionar_efeito"), { showAdd() })]
        actions.append((AureaText.t("am_import_action"), { importingAM = true }))
        if !model.effects.isEmpty { actions.append((AureaText.t("panel_copiar_efeitos"), { if let layer = model.primarySelection { model.engine.copyEffects(layer) } })) }
        if model.engine.clipboardState & 4 != 0 {
            actions.append((AureaText.t("panel_colar_efeitos"), {
                model.engine.pasteEffects(model.selection.map { NSNumber(value: $0) }); model.refreshModel(force: true)
            }))
        }
        if !model.effects.isEmpty {
            let anyOn = model.effects.contains(where: \.enabled)
            actions.append((AureaText.t(anyOn ? "panel_desligar_todos" : "panel_ligar_todos"), {
                let effects = model.effects
                model.beginGesture("ligar efeitos")
                for effect in effects where effect.enabled == anyOn { enable(effect, !anyOn) }
                model.endGesture()
            }))
        }
        model.actionSheet = ActionSheetRequest(title: AureaText.t("panel_efeitos_camada"), items: actions)
    }
    private func effectMenu(_ effect: EffectItem) {
        var actions: [(String, () -> Void)] = []
        if effect.known {
            if effect.typeId == fxEffectTypeId("aurea.distort.turbulence") {
                actions.append((AureaText.t("app_preset_fx_turbulence"), {
                    guard let layer = model.primarySelection, model.selectedLayer?.locked != true else { return }
                    model.beginGesture("Preset Turbulência dinâmica")
                    for (param, value) in [(UInt32(0), Float(15)), (1, 15), (2, 1), (3, 0), (4, 0), (5, 0), (6, 0)] {
                        model.engine.setEffect(effect.effectId, forLayer: layer, paramIndex: param, value: value)
                    }
                    let error = model.engine.setExpression(layer, property: 31, effect: effect.effectId, param: 24, source: "time*6")
                    model.endGesture(); model.refreshModel(force: true)
                    if !error.isEmpty { model.toast = error }
                }))
            }
            actions.append((AureaText.t(effect.enabled ? "panel_desligar_efeito" : "panel_ligar_efeito"), { enable(effect, !effect.enabled) }))
            actions.append((AureaText.t("panel_redefinir_efeito"), { reset(effect) }))
            // A expressão do parâmetro escolhido deste efeito (o "=" saiu do trilho).
            if let chosen = selected, chosen.effect == effect.effectId, openId == effect.effectId,
               let param = currentParam(chosen.param), param.flags & 1 != 0, fxComponentCount(Int(param.type)) > 0 {
                let label = display(param, effectId: effect.effectId).label
                actions.append((AureaText.t("fx_param_expression", label), { openExpression() }))
            }
            if let entry = model.effectCatalog.first(where: { $0.typeId == effect.typeId }) {
                actions.append((AureaText.t("effects_about"), { about = entry }))
            }
            actions.append((AureaText.t("fx_copy_this_effect"), {
                if let layer = model.primarySelection { model.engine.copyEffect(effect.effectId, fromLayer:layer) }
            }))
            if let index = model.effects.firstIndex(where: { $0.effectId == effect.effectId }) {
                if index > 0 { actions.append((AureaText.t("panel_mover_cima"), { move(effect, to: index - 1) })) }
                if index < model.effects.count - 1 { actions.append((AureaText.t("panel_mover_baixo"), { move(effect, to: index + 1) })) }
            }
        }
        actions.append((AureaText.t("panel_remover_efeito"), { remove(effect) }))
        model.actionSheet = ActionSheetRequest(title: fxEffectDisplayName(effect.typeId, effect.name), items: actions)
    }
    private func paramMenu(_ param: EffectParamItem, effect: UInt32) {
        var actions: [(String, () -> Void)] = [(AureaText.t("panel_redefinir"), {
            model.beginGesture("redefinir parâmetro"); writeVector(param.index, effect: effect, values: param.defaultValue); model.endGesture()
        })]
        if param.flags & 1 != 0 && fxComponentCount(Int(param.type)) > 0 {
            actions.append((AureaText.t("panel_expressao_3c65"), { selected = EffectParamSelection(effect: effect, param: param.index, component: 0); openExpression() }))
        }
        model.actionSheet = ActionSheetRequest(title: display(param, effectId: effect).label, items: actions)
    }
    private func reorder(_ effect: EffectItem, value: DragGesture.Value) {
        guard model.selectedLayer?.locked != true else { return }
        if dragId == nil {
            dragId = effect.effectId; dragOrder = model.effects.map(\.effectId); dragOffset = 0; lastTranslation = 0
            UIImpactFeedbackGenerator(style: .light).impactOccurred()
        }
        guard dragId == effect.effectId, var order = dragOrder, let index = order.firstIndex(of: effect.effectId), let own = cardFrames[effect.effectId] else { return }
        dragOffset += value.translation.height - lastTranslation; lastTranslation = value.translation.height
        let center = own.midY + dragOffset
        if dragOffset > 0 && index + 1 < order.count, let next = cardFrames[order[index + 1]], center > next.midY {
            order.swapAt(index, index + 1); dragOffset -= next.height; dragOrder = order
        } else if dragOffset < 0 && index > 0, let previous = cardFrames[order[index - 1]], center < previous.midY {
            order.swapAt(index, index - 1); dragOffset += previous.height; dragOrder = order
        }
    }
    private func finishReorder() {
        if let id = dragId, let order = dragOrder, let destination = order.firstIndex(of: id), let effect = model.effects.first(where: { $0.effectId == id }), model.effects.firstIndex(where: { $0.effectId == id }) != destination { move(effect, to: destination) }
        dragId = nil; dragOrder = nil; dragOffset = 0; lastTranslation = 0
    }
    private var footer: some View {
        VStack(spacing: 0) {
            if model.selectedLayer?.adjustment == true { adjustmentIntensity.padding(.bottom, 8) }
            Button { showAdd() } label: {
                HStack(spacing: 8) {
                    CupertinoGlyph.text(CupertinoGlyph.Plus, size: 16, color: AureaColors.accent)
                        .accessibilityHidden(true)
                    Text(AureaText.t("panel_adicionar_efeito")).font(.aurea(size: 15, weight: .semibold)).foregroundStyle(AureaColors.accent)
                }.frame(maxWidth: .infinity).frame(height: 48)
                    .background(ParamRowColors.card, in: RoundedRectangle(cornerRadius: 10))
            }.buttonStyle(AureaPressStyle(shrink: 1))
                .accessibilityLabel(AureaText.t("panel_adicionar_efeito"))
                .accessibilityIdentifier("aurea.effects.add")
        }
    }
    private var adjustmentIntensity: some View {
        let value = ((model.detail["opacity"] as? NSNumber)?.floatValue ?? 1) * 100
        return VStack(spacing: 0) {
            HStack {
                Text(AureaText.t("panel_intensidade_camada_ajuste")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                Spacer(minLength: 0)
                Button { toggleOpacityKey() } label: {
                    CupertinoGlyph.text(opacityLook == .keyHere ? CupertinoGlyph.RhombusFill : CupertinoGlyph.Rhombus, size: 16, color: opacityLook == .none ? AureaColors.muted : AureaColors.keyframe).frame(width: 44, height: 44)
                }.buttonStyle(AureaPressStyle())
            }
            EffectNumberRow(label: AureaText.t("panel_opacidade"), value: value, unitsPerPoint: 0.35, minimum: 0, maximum: 100, suffix: "%", decimals: 0, selected: true, look: opacityLook, expression: .none,
                            onSelect: {}, onMenu: {}, onBegin: { model.beginGesture("opacidade") }, onValue: { setOpacity($0) }, onEnd: { model.endGesture() }, onKeypad: {
                                model.numericKeypad = KeypadRequest(title: AureaText.t("panel_opacidade"), value: value, unit: "%", min: 0, max: 100, decimals: 0, onValue: { setOpacity($0) })
                            })
        }.padding(.init(top: 8, leading: 8, bottom: 4, trailing: 8))
            .background(ParamRowColors.card, in: RoundedRectangle(cornerRadius: 10))
    }
    private var opacityLook: KeyframeLook {
        guard let layer = model.primarySelection else { return .none }
        let keys = (model.keyframes[layer] ?? []).filter { $0.property == 12 }
        return keys.contains(where: { $0.time == model.localPlayhead }) ? .keyHere : keys.isEmpty ? .none : .animated
    }
    private func setOpacity(_ value: Float) {
        guard let layer = model.primarySelection, model.selectedLayer?.locked != true else { return }
        model.setTransform(12, value: min(1, max(0, value / 100)), layer: layer)
    }
    private func toggleOpacityKey() {
        guard let layer = model.primarySelection, model.selectedLayer?.locked != true else { return }
        let here = (model.keyframes[layer] ?? []).first { $0.property == 12 && $0.time == model.localPlayhead }
        if let here { model.engine.editTrackKey(layer, property: 12, effect: UInt32.max, param: 0, time: here.time, action: 1, value: here.value, targetTime: here.time, interpolation: here.interpolation, handles: []) }
        else { model.engine.keyParameter(layer, property: 12, effect: UInt32.max, param: 0, time: model.localPlayhead, value: (model.detail["opacity"] as? NSNumber)?.floatValue ?? 1) }
        model.commitPendingCommands(); model.refreshModel(force: true)
    }
}

@MainActor
private struct CubeLutImportRow: View {
    let layer: Int64
    let effect: UInt32
    @EnvironmentObject private var model: AureaModel
    @State private var picking = false
    @State private var busy = false
    @State private var filename = ""
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            if !filename.isEmpty {
                Text(filename).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).lineLimit(1)
            }
            Button(AureaText.t(busy ? "lut_importing" : "lut_import")) { picking = true }
                .frame(maxWidth: .infinity, minHeight: 44).disabled(busy)
                .accessibilityIdentifier("effect.lut.import")
        }
        .onAppear { filename = model.engine.colorLutName(layer, effect: effect) }
        .onChange(of: model.effects) { _ in filename = model.engine.colorLutName(layer, effect: effect) }
        .fileImporter(isPresented: $picking, allowedContentTypes: [.data], allowsMultipleSelection: false) { result in
            guard case .success(let urls) = result, let url = urls.first else { return }
            guard url.pathExtension.lowercased() == "cube" else { model.toast = AureaText.t("lut_invalid"); return }
            busy = true
            let engine = model.engine
            let targetLayer = layer, targetEffect = effect
            let operation = model.beginProjectOperation()
            let root = AureaPaths.documents.appendingPathComponent("LUTs", isDirectory: true)
            DispatchQueue.global(qos: .userInitiated).async {
                let scoped = url.startAccessingSecurityScopedResource()
                defer { if scoped { url.stopAccessingSecurityScopedResource() } }
                let folder = root.appendingPathComponent(UUID().uuidString, isDirectory: true)
                let copy = folder.appendingPathComponent(url.lastPathComponent)
                var success = false
                do {
                    try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
                    guard FileManager.default.createFile(atPath: copy.path, contents: nil) else { throw CocoaError(.fileWriteUnknown) }
                    let input = try FileHandle(forReadingFrom: url)
                    defer { try? input.close() }
                    let output = try FileHandle(forWritingTo: copy)
                    defer { try? output.close() }
                    var total = 0
                    while let chunk = try input.read(upToCount: 65536), !chunk.isEmpty {
                        total += chunk.count
                        guard total <= 32 * 1024 * 1024 else { throw CocoaError(.fileReadTooLarge) }
                        try output.write(contentsOf: chunk)
                    }
                    success = engine.importColorLut(targetLayer, effect: targetEffect, path: copy.path) == 0
                } catch { success = false }
                if !success { try? FileManager.default.removeItem(at: folder) }
                let imported = success
                DispatchQueue.main.async {
                    model.endProjectOperation(operation)
                    busy = false
                    filename = engine.colorLutName(targetLayer, effect: targetEffect)
                    model.refreshModel(force: true)
                    model.toast = AureaText.t(imported ? "lut_imported" : "lut_invalid")
                }
            }
        }
    }
}
private struct EffectParamSelection: Identifiable, Equatable {
    let effect: UInt32
    let param: UInt32
    let component: Int
    var id: String { "\(effect):\(param):\(component)" }
}
private struct EffectCardFrames: PreferenceKey {
    static var defaultValue: [UInt32: CGRect] = [:]
    static func reduce(value: inout [UInt32: CGRect], nextValue: () -> [UInt32: CGRect]) { value.merge(nextValue(), uniquingKeysWith: { _, next in next }) }
}
// PropertyControls.kt `PropertyRow` (redesenho 2026-09-29): 40 de altura =
// [rótulo 70×36] 6 [régua 36, centro aceso] 6 [valor 62×36]. O arrasto é o
// `valueDrag` (acumula desde o início, faixa estendida, controle fino lento).
private struct EffectNumberRow: View {
    let label: String
    let value: Float
    let unitsPerPoint: Float
    let minimum: Float
    let maximum: Float
    let suffix: String
    let decimals: Int
    /// `effects.param.<efeito>.<parâmetro>.<componente>` (o testTag do Android).
    var identifier: String = ""
    let selected: Bool
    let look: KeyframeLook
    let expression: ExpressionLook
    let onSelect: () -> Void
    let onMenu: () -> Void
    let onBegin: () -> Void
    let onValue: (Float) -> Void
    let onEnd: () -> Void
    let onKeypad: () -> Void
    @State private var dragging = false
    @State private var live: Float = 0
    var body: some View {
        HStack(spacing: ParamRowDims.gap) {
            EffectPropertyLabel(label: label, selected: selected, look: look, expression: expression, onSelect: onSelect, onMenu: onMenu)
            // Valor já fora da régua (digitado além do slider): o `valueDrag`
            // estende a faixa do gesto até ele — o toque não o puxa de volta.
            TickRuler(value: { dragging ? live : value }, unitsPerDp: unitsPerPoint, active: true,
                      height: ParamRowDims.labelH, verticalPadding: 2)
                .frame(maxWidth: .infinity).contentShape(Rectangle())
                .valueDrag(enabled: true, start: { value }, unitsPerDp: { unitsPerPoint }, min: minimum, max: maximum,
                           onStart: { live = value; dragging = true; onSelect(); onBegin() },
                           onValue: { next in live = next; onValue(next) },
                           onEnd: { dragging = false; onEnd() })
                // A régua é o controle: rótulo, valor e ajuste por gesto do
                // VoiceOver (um passo = 10 pontos de arrasto).
                .accessibilityElement()
                .accessibilityLabel(label)
                .accessibilityValue(comUnidade(numeroPtBr(dragging ? live : value, casas: decimals), suffix))
                .accessibilityAdjustableAction { direction in
                    let delta = unitsPerPoint * 10 * (direction == .increment ? 1 : direction == .decrement ? -1 : 0)
                    guard delta != 0 else { return }
                    onSelect(); onBegin(); onValue(Swift.min(Swift.max(value + delta, minimum), maximum)); onEnd()
                }
                .accessibilityIdentifier(identifier)
            ParamValueBox(comUnidade(numeroPtBr(dragging ? live : value, casas: decimals), suffix), onTap: onKeypad)
        }.frame(height: ParamRowDims.row)
    }
}
/// O rótulo das linhas de Efeitos: o MESMO desenho do `PropertyLabelChip`
/// (`ParamRowLabel`); tocar escolhe a linha, segurar abre o menu do parâmetro.
private struct EffectPropertyLabel: View {
    let label: String
    let selected: Bool
    let look: KeyframeLook
    let expression: ExpressionLook
    let onSelect: () -> Void
    let onMenu: () -> Void
    var body: some View {
        ParamRowLabel(label: label, selected: selected, keyframe: look, expression: expression)
            .contentShape(Rectangle()).onTapGesture(perform: onSelect).onLongPressGesture(minimumDuration: 0.5, perform: onMenu)
    }
}

/// Índice de camada (parte baixa do id empacotado) — o que a "Camada de áudio" guarda.
func fxLayerIndex(_ id: Int64) -> Int { Int(truncatingIfNeeded: id & 0xFFFF_FFFF) }

/// O gráfico da resposta do EQ paramétrico: dB (±24) × frequência (20 Hz–20 kHz,
/// log), com a MESMA conta do filtro que toca (`fxEqResponseDb`).
struct FxEqResponseGraph: View {
    let values: [Float]
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(AureaText.t("afx_eq_response")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
            Canvas { context, size in
                let mid = size.height / 2
                let range = 24.0
                for db in [-12.0, 0.0, 12.0] {
                    let y = mid - CGFloat(db / range) * mid
                    var line = Path()
                    line.move(to: CGPoint(x: 0, y: y))
                    line.addLine(to: CGPoint(x: size.width, y: y))
                    context.stroke(line, with: .color(AureaColors.hairline), lineWidth: db == 0 ? 1.5 : 1)
                }
                for hz in [100.0, 1000.0, 10000.0] {
                    let x = CGFloat(log10(hz / 20.0) / 3.0) * size.width
                    var line = Path()
                    line.move(to: CGPoint(x: x, y: 0))
                    line.addLine(to: CGPoint(x: x, y: size.height))
                    context.stroke(line, with: .color(AureaColors.hairline), lineWidth: 1)
                }
                var curve = Path()
                let steps = 160
                for i in 0...steps {
                    let t = Double(i) / Double(steps)
                    let db = min(max(fxEqResponseDb(values, 20.0 * pow(1000.0, t)), -range), range)
                    let point = CGPoint(x: CGFloat(t) * size.width, y: mid - CGFloat(db / range) * mid)
                    if i == 0 { curve.move(to: point) } else { curve.addLine(to: point) }
                }
                context.stroke(curve, with: .color(AureaColors.accent), lineWidth: 2)
            }
            .frame(height: 96)
            .background(AureaColors.surfaceHigh, in: RoundedRectangle(cornerRadius: 8))
            .keepLtr()   // frequência grave → aguda da esquerda para a direita, como todo EQ
        }
        .padding(.leading, 12).padding(.trailing, 4).padding(.vertical, 6)
    }
}

/// Fichas dos presets de um efeito (EffectsPanel.kt `EffectPresetRow`), como a
/// fileira de presets do Particular. `presets` = [id, nome do motor, id, nome, …].
struct FxPresetRow: View {
    let presets: [String]
    let apply: (Int) -> Void
    private func label(_ pid: String, _ fallback: String) -> String {
        switch pid {
        case "impact": return AureaText.t("fxp_impact")
        case "handheld": return AureaText.t("fxp_handheld")
        case "glitch": return AureaText.t("fxp_glitch")
        default: return fallback
        }
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 5) {
            Text(AureaText.t("fx_presets")).font(.aurea(size: 11.5)).foregroundStyle(AureaColors.muted)
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    ForEach(0..<(presets.count / 2), id: \.self) { i in
                        let pid = presets[i * 2]
                        Button { apply(i) } label: {
                            Text(label(pid, presets[i * 2 + 1]))
                                .font(.aurea(size: 12))
                                .foregroundStyle(AureaColors.text)
                                .padding(.horizontal, 12)
                                .padding(.vertical, 6)
                                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                        }
                        .buttonStyle(.plain)
                        .accessibilityIdentifier("fx.preset." + pid)
                    }
                }
            }
        }.frame(maxWidth: .infinity, alignment: .leading).padding(.bottom, 6)
    }
}

/// Os ids das máscaras no pacote do motor (`maskData`: 6 da afim, a contagem e,
/// por máscara, 12 de cabeçalho com o id no 1º e o nº de pontos no 8º, + 6 por ponto).
func fxMaskIds(_ raw: [NSNumber]) -> [UInt32] {
    let data = raw.map(\.floatValue)
    guard data.count >= 7 else { return [] }
    var ids: [UInt32] = [], offset = 7
    for _ in 0..<max(0, Int(data[6])) {
        guard offset + 12 <= data.count else { break }
        let points = Int(data[offset + 7]) * 6
        ids.append(UInt32(max(0, data[offset])))
        offset += 12 + max(0, points)
    }
    return ids
}

/// Miniatura de 40 do cartão da pilha: a prévia do efeito, ou a cartela.
private struct EffectStackThumb: View {
    let typeId: UInt32
    let category: String
    let store: EffectPreviewStore
    @State private var image: UIImage?
    var body: some View {
        Group {
            if let image {
                Image(uiImage: image).resizable().interpolation(.low).scaledToFill()
            } else {
                FxToolPlate(glyph: fxCategoryGlyph(category), glyphSize: 16, opacity: 0.8)
            }
        }
        .frame(width: 40, height: 40).clipShape(RoundedRectangle(cornerRadius: 8))
        .accessibilityHidden(true)
        .task(id: typeId) {
            let loaded = await store.image(for: typeId)
            if !Task.isCancelled { image = loaded }
        }
    }
}
