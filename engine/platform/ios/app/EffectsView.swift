// Port of EffectsPanel.kt. Project data stays in the core.
//
// Omega 0.3.2 (área 07): o painel de efeitos no formato da referência, dentro da
// moldura comum (`PanelFrame`, coluna ‹ ◆± Curva = ⋯):
//  - P0-2 LISTA de linhas de 37 (seta · nome · selo · 👁 · ≡), as ferramentas-
//    efeito (Máscara, Rastreio, Legendas) no mesmo formato antes dos efeitos e o
//    "+ Adicionar efeito" no fim. Toque longo arrasta para reordenar; deslizar
//    para a esquerda apaga (com "Desfazer"). A lista nasce FECHADA.
//  - P0-3 CONTROLES de UM efeito sobrepostos à lista, nascendo da linha tocada
//    (recorte + deslocamento em 200 ms); tocar no cabeçalho fecha.
//  - P0-4 LINHAS com as medidas da referência (slider, régua, ponto, XYZ,
//    orientação, interruptor, escolha segmentada/suspensa, cor, seção, dica).
//  - P0-5 MENUS ancorados: ⋮ do efeito e ⋯ da lista.
//  - P1-1..P1-5 régua com arremesso, parâmetro ativo no preview, eixos, seções e
//    habilitação do motor, rolagem até a linha depois do ◆±.
// Mesmos ids e mesmo comportamento do EffectsPanel.kt / EffectParamRows.kt.
import SwiftUI
import UIKit
import UIKit.UIGestureRecognizerSubclass
import UniformTypeIdentifiers

@MainActor
struct EffectsView: View {
    var focusedType: UInt32? = nil
    var embedded = false
    @EnvironmentObject private var model: AureaModel
    @StateObject private var prefs = FxEffectPrefs()
    @ObservedObject private var puppet = PuppetStageState.shared
    @ObservedObject private var pointStage = EffectPointStageState.shared
    @State private var maskCount = 0
    @State private var hasMatte = false
    @State private var captionCount = 0
    @State private var cameraTracked = false
    @State private var about: EffectCatalogItem?
    @State private var pendingPick: UInt32?
    /// Arquivo de projeto de efeitos escolhido (DocumentImportPicker; o `.fileImporter`
    /// daqui ficava sob o da raiz do editor e não abria).
    @State private var amPick: URL?
    @State private var importTask: Task<Void, Never>?
    /// O efeito com os controles abertos (nil = a lista).
    @State private var openId: UInt32?
    @State private var loadedLayer: Int64?
    @State private var selected: EffectParamSelection?
    /// "Avançado" aberto, por efeito (estado local; nasce recolhido).
    @State private var advancedOpen: Set<UInt32> = []
    @State private var expressionLooks: [UInt32: ExpressionLook] = [:]
    /// O 1º efeito da camada, aberto sozinho: um ponto entra ativo com o guia longo.
    @State private var autoGuideFor: UInt32?
    // Reordenar (toque longo na linha ou arrasto no ≡).
    @State private var dragId: UInt32?
    @State private var dragOffset: CGFloat = 0
    @State private var dragStart = 0
    @State private var dragOrder: [UInt32]?
    @GestureState private var reorderHeld = false
    @State private var rowFrames: [UInt32: CGRect] = [:]
    // Deslizar para apagar: a linha sob o dedo, o deslocamento lógico (negativo =
    // para o início da linha) e o eixo travado (0 indeciso, 1 horizontal, 2 vertical).
    @State private var swipeId: UInt32?
    @State private var swipeOffset: CGFloat = 0
    @State private var swipeAxis = 0
    // A sobreposição dos controles (P0-3).
    @State private var overlayId: UInt32?
    @State private var overlayProgress: CGFloat = 0
    @State private var overlayFrom: CGFloat = 0
    @State private var overlayToken = 0
    @State private var effectMenuOpen = false
    // P1-5: depois do ◆±, o corpo rola até a linha do parâmetro.
    @State private var scrollParam: UInt32?
    @State private var scrollToken = 0
    @Environment(\.layoutDirection) private var layoutDirection

    /// Abrir/fechar: 200 ms, acelera-desacelera (a curva padrão do Android).
    private static let openAnimation = Animation.timingCurve(0.42, 0, 0.58, 1, duration: Double(FxParamMath.OPEN_MS) / 1000)
    private var gap: CGFloat { FxParamMath.LIST_GAP }
    /// Sem [focusedType] é o painel inteiro; com ele, o editor embutido.
    private var tabbed: Bool { focusedType == nil }
    private var locked: Bool { model.selectedLayer?.locked == true }
    /// Os efeitos que a lista mostra (sem as decorações de Borda e sombra).
    private var shown: [EffectItem] {
        model.effects.filter { focusedType == nil ? fxShownInEffectList($0) : $0.typeId == focusedType }
    }
    private var ordered: [EffectItem] {
        let list = shown
        let byId = Dictionary(list.map { ($0.effectId, $0) }, uniquingKeysWith: { first, _ in first })
        return (dragOrder ?? list.map(\.effectId)).compactMap { byId[$0] }
    }
    private var selectedParam: EffectParamItem? {
        guard let selected, selected.effect == openId else { return nil }
        return model.effectParams.first { $0.index == selected.param }
    }
    private var hasAudio: Bool { ((model.detail["audioFlags"] as? NSNumber)?.uint32Value ?? 0) & 4 != 0 }
    private var meshSelected: Bool {
        selected == nil && openId != nil && model.effects.contains { $0.effectId == openId && $0.typeId == fxEffectTypeId("aurea.distort.mesh_warp") }
    }
    /// Fantoche: os pinos são parâmetros ocultos (sem linha), então o losango e a
    /// curva do trilho miram o pino em foco (eixo X da posição dele).
    /// Mesmo contrato do Android (EffectsPanel.kt, puppetKey).
    private var puppetSelection: EffectParamSelection? {
        guard !meshSelected, let layer = model.primarySelection, let effect = openId, puppet.layer == layer,
              puppet.effect >= 0, UInt32(puppet.effect) == effect,
              model.effects.contains(where: { $0.effectId == effect && $0.typeId == PuppetStageState.type }),
              puppet.editing || selected == nil else { return nil }
        let pins = model.puppetPins(layer, effect: puppet.effect)
        let active = stride(from: 0, to: pins.count, by: 4).compactMap { index -> Int? in
            guard index + 3 < pins.count, pins[index].isFinite, pins[index] >= 0, pins[index] <= 15 else { return nil }
            let pin = Int(pins[index]); return Float(pin) == pins[index] ? pin : nil
        }
        guard let pin = active.contains(puppet.selected) ? puppet.selected : active.first else { return nil }
        return EffectParamSelection(effect: effect, param: UInt32(5 + pin * 3), component: 0)
    }
    /// O alvo do trilho (◆± e curva): o pino em foco do Fantoche ou o parâmetro escolhido.
    private var railSelection: EffectParamSelection? { puppetSelection ?? selected }
    private var railParam: EffectParamItem? {
        guard let s = railSelection, s.effect == openId else { return nil }
        return model.effectParams.first { $0.index == s.param }
    }
    private var canAnimate: Bool {
        guard openId != nil else { return false }
        return meshSelected || (railParam.map { $0.flags & 1 != 0 && fxComponentCount(Int($0.type)) > 0 } ?? false)
    }
    private var railLook: KeyframeLook {
        if meshSelected, let effect = openId, let layer = model.primarySelection {
            let keys = (model.keyframes[layer] ?? []).filter { $0.property == 44 && $0.effectIndex == effect }
            return keys.contains { $0.time == model.localPlayhead } ? .keyHere : keys.isEmpty ? .none : .animated
        }
        guard let selected = railSelection else { return .none }
        return look(effect: selected.effect, param: selected.param)
    }
    private var selectedTrack: [KeyframeItem] {
        guard let selected = railSelection, let layer = model.primarySelection else { return [] }
        return (model.keyframes[layer] ?? []).filter { $0.property == 31 && $0.effectIndex == selected.effect && $0.paramIndex == selected.param * 4 + UInt32(selected.component) }.sorted { $0.time < $1.time }
    }
    private var canExpress: Bool {
        guard !meshSelected, let p = railParam else { return false }
        return p.flags & 1 != 0 && fxComponentCount(Int(p.type)) > 0
    }
    private var railExpression: ExpressionLook {
        guard let s = railSelection, s.effect == openId else { return .none }
        return expressionLooks[s.param] ?? .none
    }

    /// A coluna esquerda (moldura comum): ‹ volta; ◆± e Curva miram o parâmetro
    /// escolhido; "=" abre a expressão dele; ⋯ = menu da lista, inativo com os
    /// controles de um efeito abertos (referência: `enableOverflow`).
    private var panelRail: PanelRail {
        PanelRail(onBack: { model.panel = .none },
                  keyframeLook: openId != nil ? railLook : .none,
                  onKeyframe: canAnimate ? { keyframeTapped() } : nil,
                  onDeleteAllKeyframes: canAnimate && !meshSelected ? { deleteAllKeys() } : nil,
                  onCurve: canAnimate && !meshSelected ? { openCurve() } : nil,
                  curveReady: selectedTrack.count >= 2,
                  expression: railExpression,
                  onExpression: canExpress ? { openExpression() } : nil,
                  moreEntries: tabbed ? { listMenuEntries() } : nil,
                  moreEnabled: openId == nil)
    }

    var body: some View {
        VStack(spacing: 0) {
            // O título da seção ("Efeitos") mora na barra de cima; o editor
            // embutido fora dela (letras do texto 3D) mantém o próprio cabeçalho.
            if !embedded && !tabbed {
                PanelHeader(title: AureaText.t("panel_efeitos"), onBack: { model.panel = .none })
            }
            PanelFrame(rail: panelRail, scrollContent: false) {
                GeometryReader { geometry in
                    ZStack(alignment: .top) {
                        list
                            .opacity(overlayId == nil ? 1 : 0)
                            .allowsHitTesting(overlayId == nil)
                            .accessibilityHidden(overlayId != nil)
                        if let id = overlayId, let effect = model.effects.first(where: { $0.effectId == id }) {
                            settingsOverlay(effect, height: geometry.size.height)
                        }
                    }
                    .frame(width: geometry.size.width, height: geometry.size.height, alignment: .top)
                }
                .background(AureaColors.fxPanel)
                .clipped()
            }
        }
        .onAppear { refreshTools(); enterLayer(); model.refreshSelectedLayer(); refreshExpressions(); updateTimelineFocus() }
        .onChange(of: model.primarySelection) { _ in refreshTools(); enterLayer() }
        .onChange(of: selected) { _ in updateTimelineFocus() }
        .onChange(of: openId) { _ in updateTimelineFocus(); syncStage() }
        .onChange(of: puppet.layer) { _ in updateTimelineFocus() }
        .onChange(of: puppet.effect) { _ in updateTimelineFocus() }
        .onChange(of: puppet.editing) { _ in updateTimelineFocus() }
        .onChange(of: puppet.selected) { _ in updateTimelineFocus() }
        .onChange(of: puppet.revision) { _ in updateTimelineFocus() }
        .onChange(of: reorderHeld) { held in if !held { finishReorder() } }
        .onChange(of: model.effects.map(\.effectId)) { ids in effectsChanged(ids) }
        .task(id: pendingPick) {
            // Render lento não é recusa: depois de 3 s relê o modelo e libera
            // um novo toque (sem isso, dois toques rápidos adicionariam dois).
            guard pendingPick != nil else { return }
            do { try await Task.sleep(nanoseconds: 3_000_000_000) } catch { return }
            model.refreshModel(force: true)
            pendingPick = nil
        }
        .sheet(item: $about) { entry in
            // "Detalhes do efeito" (⋮): a ficha, sem variações (o efeito já está na camada).
            EffectAboutSheet(prefs: prefs, entry: entry).environmentObject(model)
                .presentationDetents([.medium, .large])
        }
        .onChange(of: model.status.modelRevision) { _ in refreshExpressions(); refreshTools(); updateTimelineFocus() }
        .onChange(of: model.status.playhead) { _ in refreshExpressions() }
        // Projeto de efeitos: .xml/.amproj/.zip não têm tipo padrão; o motor reconhece pelo conteúdo.
        .onChange(of: amPick) { picked in
            guard let url = picked else { return }
            amPick = nil
            let target = model.primarySelection
            importTask?.cancel()
            importTask = Task {
                let data = await Task.detached(priority: .userInitiated) { () -> Data? in
                    let scoped = url.startAccessingSecurityScopedResource()
                    defer { if scoped { url.stopAccessingSecurityScopedResource() } }
                    return try? AureaPaths.readImport(url) { readable -> Data? in
                        guard let stream = InputStream(url: readable) else { return nil }
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
                    }
                }.value
                guard !Task.isCancelled else { return }
                if let data, model.primarySelection == target { model.importAlightMotion(data) }
                else { model.toast = AureaText.t("am_import_failed", AureaText.t("am_import_read_failed")) }
            }
        }
        .onDisappear {
            model.timelineFocus = nil
            pointStage.deactivate()
            importTask?.cancel(); importTask = nil
            finishReorder()
            model.endGesture()
        }
    }

    // =========================================================================
    // Entrar, abrir e fechar
    // =========================================================================

    /// ENTRAR = lista fechada (salvo pelo losango de um parâmetro de efeito na
    /// timeline ou por um pedido explícito de outro painel). Camada vazia NÃO abre
    /// o navegador sozinha: só mostra "+ Adicionar efeito".
    private func enterLayer() {
        guard loadedLayer != model.primarySelection else { return }
        loadedLayer = model.primarySelection
        closeNow(); advancedOpen = []; expressionLooks = [:]; pendingPick = nil; autoGuideFor = nil
        if let requested = model.requestedEffectFocusId {
            open(requested, animated: false); model.pendingEffectFocus = nil; return
        }
        if !tabbed {
            if let first = shown.first { open(first.effectId, animated: false) }
            return
        }
        guard (model.curveProperty == 31 || model.curveProperty == 44), model.curveSelectedTime != nil,
              model.effects.contains(where: { $0.effectId == model.curveEffect }) else { return }
        // Entrar pelo losango de um parâmetro é editar: abre aquele efeito naquela linha.
        if model.curveProperty == 44 { selected = nil; open(model.curveEffect, animated: false); return }
        let entry = EffectParamSelection(effect: model.curveEffect, param: model.curveParam / 4, component: Int(model.curveParam % 4))
        selected = entry; open(entry.effect, animated: false)
        let entryLayout = layout(entry.effect)
        if entryLayout.advanced.contains(where: { $0.index == entry.param }) { advancedOpen.insert(entry.effect) }
        // Parâmetro numa seção recolhida: a seção abre (EffectsPanel.kt faz igual).
        let closed = fxClosedSection(entryLayout, param: entry.param)
        if closed > 0 { setSectionOpen(entry.effect, ordinal: closed, open: true) }
    }

    /// A pilha mudou: um pedido EXPLÍCITO de outro painel (Texto "+ animador",
    /// Fantoche no palco, busca de comandos) abre o efeito pedido; o efeito aberto
    /// que sumiu volta à lista. O resto é da regra da lista (`revealAdded`).
    private func effectsChanged(_ ids: [UInt32]) {
        if !tabbed {
            let focused = shown.map(\.effectId)
            if openId == nil || !focused.contains(openId ?? 0) {
                if let first = focused.first { open(first, animated: false) } else { closeNow() }
            }
            return
        }
        if let requested = model.requestedEffectFocusId, ids.contains(requested) {
            pendingPick = nil
            model.pendingEffectFocus = nil
            if openId != requested { open(requested, animated: false) }
        } else if let current = openId ?? overlayId, !ids.contains(current) {
            closeNow()
        }
    }

    /// Efeito novo na lista (navegador, colar): abre os controles sozinho SÓ se a
    /// camada não tinha efeito nenhum (`FxParamMath.opensAfterAdd`); senão a lista
    /// continua fechada e rola até a linha nova. Os ids vistos ficam no MODELO: o
    /// painel recriado ainda reconhece o que é novo.
    private func revealAdded(_ ids: [UInt32], proxy: ScrollViewProxy) {
        guard tabbed, let layer = model.primarySelection else { return }
        let seen: Set<UInt32>? = model.seenEffectIds[layer]
        model.seenEffectIds[layer] = Set(ids)
        guard let seen else { return }
        let added = ids.filter { !seen.contains($0) }
        guard let last = added.last else { return }
        pendingPick = nil
        if FxParamMath.opensAfterAdd(previousCount: seen.count, addedCount: added.count) {
            autoGuideFor = last
            DispatchQueue.main.async { if openId != last { open(last, animated: true) } }
        } else if openId == nil {
            DispatchQueue.main.async {
                withAnimation(.easeOut(duration: 0.25)) { proxy.scrollTo(last, anchor: .bottom) }
            }
        }
    }

    /// ABRE os controles de um efeito: com `animated`, o cabeçalho nasce sobre a
    /// linha tocada e o corpo desdobra para baixo (recorte + deslocamento, 200 ms);
    /// a lista some no início (referência: invisível antes da animação).
    private func open(_ id: UInt32, animated: Bool) {
        guard let layer = model.primarySelection else { return }
        if pointStage.active && pointStage.effect != id { pointStage.deactivate() }
        effectMenuOpen = false
        overlayToken += 1
        openId = id
        overlayId = id
        model.loadParams(layerId: layer, effectId: id)
        chooseFirstParam(id)
        refreshExpressions()
        if animated && tabbed {
            overlayFrom = (rowFrames[id]?.minY ?? gap) - gap
            overlayProgress = 0
            DispatchQueue.main.async { withAnimation(Self.openAnimation) { overlayProgress = 1 } }
        } else {
            overlayProgress = 1
        }
    }

    /// FECHA (tocar no cabeçalho): o inverso da abertura; no fim, a lista volta.
    private func close(animated: Bool) {
        guard tabbed, let id = openId ?? overlayId else { return }
        guard animated else { closeNow(); return }
        pointStage.deactivate()
        effectMenuOpen = false
        openId = nil
        selected = nil
        overlayToken += 1
        let token = overlayToken
        overlayFrom = (rowFrames[id]?.minY ?? (overlayFrom + gap)) - gap
        withAnimation(Self.openAnimation) { overlayProgress = 0 }
        DispatchQueue.main.asyncAfter(deadline: .now() + Double(FxParamMath.OPEN_MS) / 1000) {
            guard overlayToken == token else { return }
            overlayId = nil
            if openId == nil { model.selectedEffectId = nil }
        }
    }

    /// Fecha na hora, sem animar (🗑 do cabeçalho, efeito que sumiu, outra camada).
    private func closeNow() {
        pointStage.deactivate()
        effectMenuOpen = false
        openId = nil
        selected = nil
        overlayToken += 1
        overlayId = nil
        overlayProgress = 0
        model.selectedEffectId = nil
    }

    /// Os controles abertos escolhem a primeira linha principal animável (◆± e
    /// Curva miram nela). 1º efeito da camada aberto sozinho: um ponto já entra
    /// ATIVO no preview com o guia longo.
    private func chooseFirstParam(_ id: UInt32) {
        if model.effects.contains(where: { $0.effectId == id && $0.typeId == fxEffectTypeId("aurea.distort.mesh_warp") }) {
            selected = nil
            autoGuideFor = nil
            return
        }
        guard selected?.effect != id else { return }
        let all = layout(id).all
        let first = all.first(where: { $0.flags & 1 != 0 && fxComponentCount(Int($0.type)) > 0 && $0.enabled })
            ?? all.first(where: { fxComponentCount(Int($0.type)) > 0 })
        selected = first.map { EffectParamSelection(effect: id, param: $0.index, component: 0) }
        guard autoGuideFor == id else { return }
        autoGuideFor = nil
        if let first, let layer = model.primarySelection,
           fxEffectParamDraggable(type: Int(first.type), flags: first.flags), first.enabled {
            pointStage.activate(layer, effect: id, param: first.index, control: FxParamMath.controlFor(first.control, Int(first.type)),
                                label: display(first, effectId: id).label, guide: .long)
        }
    }

    /// O parâmetro ativo no preview some quando o efeito fecha ou troca.
    private func syncStage() {
        guard pointStage.active else { return }
        if openId == nil || pointStage.effect != openId || pointStage.layer != model.primarySelection { pointStage.deactivate() }
    }

    /// Escolhe a linha (◆± e Curva miram nela). `explicit` = toque no rótulo ou num
    /// eixo: um ponto/XYZ/orientação fica ATIVO no preview e o guia aparece.
    private func choose(_ param: EffectParamItem, effect: UInt32, component: Int, explicit: Bool) {
        selected = EffectParamSelection(effect: effect, param: param.index, component: component)
        guard explicit, let layer = model.primarySelection else { return }
        if fxEffectParamDraggable(type: Int(param.type), flags: param.flags) && param.enabled && !locked {
            pointStage.activate(layer, effect: effect, param: param.index, control: FxParamMath.controlFor(param.control, Int(param.type)),
                                label: display(param, effectId: effect).label, guide: .short)
        } else {
            pointStage.deactivate()
        }
    }

    private func tapRow(_ effect: EffectItem) {
        guard overlayId == nil, dragId == nil else { return }
        if !effect.known { confirmUnknown(effect); return }
        open(effect.effectId, animated: true)
    }

    /// Efeito que saiu do catálogo: só entender e tirar (A.01).
    private func confirmUnknown(_ effect: EffectItem) {
        model.actionSheet = ActionSheetRequest(title: AureaText.t("panel_efeito_removido"),
                                               message: AureaText.t("panel_este_efeito_saiu_aurea_nao_desenha"),
                                               actions: [SheetAction(AureaText.t("panel_remover_efeito"), destructive: true, enabled: !locked) { remove(effect) }])
    }

    private func currentParam(_ index: UInt32) -> EffectParamItem? { model.effectParams.first { $0.index == index } }
    private func typeOf(_ effectId: UInt32) -> UInt32 { model.effects.first { $0.effectId == effectId }?.typeId ?? 0 }
    private func slot(_ param: EffectParamItem) -> FxParamSlot {
        FxParamSlot(index: Int(param.index), type: Int(param.type), min: param.minValue, max: param.maxValue, label: param.label, unit: param.unit, enumLabels: param.enumLabels)
    }
    /// O corpo dos controles (soltas, "Avançado" e seções) — regra de `fxEffectLayout`.
    private func layout(_ id: UInt32) -> FxLayout {
        let visible = model.effectParams.filter { $0.flags & 16 == 0 }
        return fxEffectLayout(typeOf(id), visible)
    }
    /// Abre/recolhe uma seção (apresentação: sem desfazer) e relê o efeito.
    private func setSectionOpen(_ effect: UInt32, ordinal: Int, open: Bool) {
        guard let layer = model.primarySelection, ordinal > 0 else { return }
        if model.engine.setEffectSection(layer, effect: effect, section: UInt32(ordinal), open: open) {
            model.loadParams(layerId: layer, effectId: effect)
        }
    }
    /// A exibição da linha; casas pelo passo quando o efeito o declarou (P2-3).
    private func display(_ param: EffectParamItem, effectId: UInt32) -> FxParamDisplay {
        fxParamDisplay(typeOf(effectId), slot(param), step: param.step, rowFlags: param.rowFlags)
    }
    /// O passo por risco na unidade MOSTRADA (nunca 0).
    private func displayStep(_ param: EffectParamItem, _ d: FxParamDisplay) -> Float {
        let s = abs(fxParamStep(param) * d.scale)
        return s > 0 && s.isFinite ? s : 1
    }
    private func displayName(_ effect: EffectItem) -> String {
        let name = fxEffectDisplayName(effect.typeId, effect.name)
        return effect.known || name.hasPrefix("?") ? name : "?" + name
    }
    /// A linha responde agora? (motor: habilitação cruzada e interruptor da seção).
    private func rowEnabled(_ param: EffectParamItem) -> Bool { param.enabled && param.flags & 16 == 0 && !locked }
    /// "Desligado por …" quando se sabe quem desliga (o interruptor da seção).
    private func disabledText(_ by: String?) -> String {
        guard let by, !by.isEmpty else { return AureaText.t("om_fx_disabled") }
        return AureaText.t("om_fx_disabled_by", by)
    }
    /// Escrita guardada: só na mesma camada/projeto/composição, destravada e com o efeito aberto.
    private func writeGuard(_ effect: UInt32) -> () -> Bool {
        let project = model.projectGeneration
        let composition = (model.composition[AureaCompositionId] as? NSNumber)?.uint64Value
        let layer = model.primarySelection
        return {
            layer != nil && model.projectGeneration == project && model.primarySelection == layer &&
                (model.composition[AureaCompositionId] as? NSNumber)?.uint64Value == composition &&
                model.selectedLayer?.locked != true && openId == effect
        }
    }
    private func rowIdentity(_ effect: UInt32, _ index: UInt32) -> String {
        "effect.row:\(model.projectGeneration):\(model.composition[AureaCompositionId] ?? ""):\(model.primarySelection ?? 0):\(effect):\(index)"
    }

    // =========================================================================
    // P0-2 · A LISTA
    // =========================================================================

    private var list: some View {
        ScrollViewReader { proxy in
            ScrollView(.vertical) {
                VStack(spacing: 0) {
                    if tabbed && model.selectedLayer?.adjustment == true { adjustmentIntensity.padding(.top, gap) }
                    ForEach(tools, id: \.key) { tool in toolRow(tool).padding(.top, gap) }
                    ForEach(ordered) { effect in
                        swipeable(effect) { listRow(effect) }
                            .background(GeometryReader { geometry in
                                Color.clear.preference(key: EffectCardFrames.self, value: [effect.effectId: geometry.frame(in: .named("effect-list"))])
                            })
                            .offset(y: dragId == effect.effectId ? dragOffset : 0)
                            .zIndex(dragId == effect.effectId ? 1 : 0)
                            .padding(.top, gap)
                            .id(effect.effectId)
                    }
                    if tabbed { addRow }
                    else if ordered.isEmpty, let type = focusedType {
                        Button {
                            guard let layer = model.primarySelection else { return }
                            model.mutate { $0.addEffect(type, toLayer: layer, at: UInt32.max) }
                        } label: {
                            Text(AureaText.t("t3d_enable_letters")).font(.aurea(size: 14)).foregroundStyle(AureaColors.text)
                                .frame(maxWidth: .infinity).frame(height: FxParamMath.ROW_LIST)
                                .background(AureaColors.fxLowSurface, in: RoundedRectangle(cornerRadius: 5))
                                .contentShape(Rectangle())
                        }
                        .buttonStyle(AureaPressStyle(shrink: 1))
                        .padding(.vertical, gap)
                    }
                }
                .padding(.horizontal, gap)
                .padding(.bottom, gap)
            }
            .coordinateSpace(name: "effect-list")
            .scrollDisabled(dragId != nil)
            .accessibilityIdentifier("aurea.effects.stack")
            .onPreferenceChange(EffectCardFrames.self) { rowFrames = $0 }
            // Depois do enterLayer (onAppear do painel): o onAppear da lista pode vir ANTES.
            .onAppear { DispatchQueue.main.async { revealAdded(ordered.map(\.effectId), proxy: proxy) } }
            .onChange(of: ordered.map(\.effectId)) { ids in revealAdded(ids, proxy: proxy) }
        }
    }

    /// A LINHA DE UM EFEITO (`effect_list_item`): 37, raio 5, `fxListSurface` sem
    /// realce de toque; `▸` 18 a 30 % · nome 14 · selo 35 (cadeado Hyper, "!" de
    /// pesado, plugin) · 👁 (alvo 44×37) · ≡ a 15 % (alvo 32×37). Desligado: nome a
    /// 45 %. Toque = abrir os controles; TOQUE LONGO = erguer e arrastar.
    /// Ids `effects.expand|eye|grip.<id>` (iguais no Android).
    private func listRow(_ effect: EffectItem) -> some View {
        let id = fxEffectCardId(effect.typeId)
        let name = fxEffectDisplayName(effect.typeId, effect.name)
        let lifted = dragId == effect.effectId
        let canReorder = overlayId == nil && !locked
        let index = shown.firstIndex(where: { $0.effectId == effect.effectId }) ?? 0
        let badge = badgeLabel(effect)
        return HStack(spacing: 0) {
            HStack(spacing: 0) {
                CupertinoGlyph.text(CupertinoGlyph.ChevronRight, size: 18, color: AureaColors.text.opacity(0.3))
                    .mirrorsInRtl()
                    .frame(width: 18, height: 18)
                Text(displayName(effect))
                    .font(.aurea(size: 14))
                    .foregroundStyle(AureaColors.text)
                    .lineLimit(1)
                    .truncationMode(.tail)
                    .opacity(effect.enabled ? 1 : 0.45)
                    .padding(.leading, 6)
                    .frame(maxWidth: .infinity, alignment: .leading)
                badgeView(effect).frame(width: 35, height: FxParamMath.ROW_LIST)
            }
            .padding(.leading, 6)
            .frame(maxWidth: .infinity)
            .frame(height: FxParamMath.ROW_LIST)
            .contentShape(Rectangle())
            .onTapGesture { tapRow(effect) }
            .gesture(reorderGesture(effect), including: canReorder ? .all : .subviews)
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(AureaText.t("fxui_a11y_expand", name))
            .accessibilityValue([effect.enabled ? "" : AureaText.t("fxui_effect_off"), badge].filter { !$0.isEmpty }.joined(separator: ", "))
            .accessibilityAddTraits(.isButton)
            .accessibilityAction { tapRow(effect) }
            .accessibilityAction(named: AureaText.t("panel_mover_cima")) { if index > 0 { move(effect, toListIndex: index - 1) } }
            .accessibilityAction(named: AureaText.t("panel_mover_baixo")) { if index < shown.count - 1 { move(effect, toListIndex: index + 1) } }
            .accessibilityAction(named: AureaText.t("fxui_a11y_remove", name)) { model.removeEffectWithUndo(effect.effectId) }
            .accessibilityIdentifier("effects.expand." + id)
            Button { enable(effect, !effect.enabled) } label: {
                CupertinoGlyph.text(effect.enabled ? CupertinoGlyph.Eye : CupertinoGlyph.EyeSlash, size: 18,
                                    color: effect.enabled ? AureaColors.text : AureaColors.fxDisabledText)
                    .frame(width: 44, height: FxParamMath.ROW_LIST)
                    .contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle(shrink: 1))
            .disabled(locked)
            .accessibilityLabel(AureaText.t(effect.enabled ? "fxui_a11y_disable" : "fxui_a11y_enable", name))
            .accessibilityIdentifier("effects.eye." + id)
            CupertinoGlyph.text(CupertinoGlyph.LineHorizontal3, size: 16, color: AureaColors.text.opacity(lifted ? 0.6 : 0.15))
                .frame(width: 32, height: FxParamMath.ROW_LIST)
                .contentShape(Rectangle())
                .highPriorityGesture(DragGesture(minimumDistance: 4, coordinateSpace: .global)
                    .updating($reorderHeld) { _, state, _ in state = true }
                    .onChanged { value in reorder(effect, translation: value.translation.height) }
                    .onEnded { _ in finishReorder() }, including: canReorder ? .all : .none)
                .accessibilityLabel(AureaText.t("fxui_a11y_drag", name))
                .accessibilityIdentifier("effects.grip." + id)
        }
        .frame(height: FxParamMath.ROW_LIST)
        .background(lifted ? AureaColors.fxSegmentOn : AureaColors.fxListSurface, in: RoundedRectangle(cornerRadius: 5))
    }

    /// O selo (35): cadeado do Hyper sem o acesso, "!" do efeito pesado ou a peça do plugin.
    @ViewBuilder private func badgeView(_ effect: EffectItem) -> some View {
        if HyperEffectGate.locked(typeId: effect.typeId) {
            CupertinoGlyph.text(CupertinoGlyph.LockFill, size: 14, color: AureaColors.fxDisabledText)
        } else if let entry = model.effectCatalog.first(where: { $0.typeId == effect.typeId }), fxEffectCost(entry.effectClass) >= 3 {
            Text("!").font(.aurea(size: 13, weight: .black)).foregroundStyle(AureaColors.fxDisabledText)
        } else if fxIsPluginEffect(effect.typeId) {
            CupertinoGlyph.text(CupertinoGlyph.SquareStack3dUp, size: 14, color: AureaColors.fxDisabledText)
        } else {
            Color.clear
        }
    }
    private func badgeLabel(_ effect: EffectItem) -> String {
        if HyperEffectGate.locked(typeId: effect.typeId) { return AureaText.t("hyper_effect_locked") }
        if let entry = model.effectCatalog.first(where: { $0.typeId == effect.typeId }), fxEffectCost(entry.effectClass) >= 3 {
            return AureaText.t("fx_browser_heavy")
        }
        return fxIsPluginEffect(effect.typeId) ? AureaText.t("fxui_group_plugins") : ""
    }

    /// TOQUE LONGO (0,4 s) em qualquer ponto da linha ergue e arrasta para
    /// reordenar (`ItemTouchHelper` da referência). Andar antes solta o gesto para
    /// a rolagem ou para o deslizar.
    private func reorderGesture(_ effect: EffectItem) -> some Gesture {
        LongPressGesture(minimumDuration: 0.4)
            .sequenced(before: DragGesture(minimumDistance: 0, coordinateSpace: .global))
            .updating($reorderHeld) { value, state, _ in
                if case .second(true, _) = value { state = true }
            }
            .onChanged { value in
                guard case .second(true, let drag) = value else { return }
                reorder(effect, translation: drag?.translation.height ?? 0)
            }
            .onEnded { _ in finishReorder() }
    }

    /// Arrasto vertical: só a ordem local muda; a pilha recebe UM comando ao soltar.
    private func reorder(_ effect: EffectItem, translation: CGFloat) {
        guard !locked, overlayId == nil else { return }
        if dragId == nil {
            dragId = effect.effectId
            dragOrder = shown.map(\.effectId)
            dragStart = dragOrder?.firstIndex(of: effect.effectId) ?? 0
            dragOffset = 0
            swipeId = nil; swipeOffset = 0; swipeAxis = 0
            UIImpactFeedbackGenerator(style: .light).impactOccurred()
        }
        guard dragId == effect.effectId, var order = dragOrder, let current = order.firstIndex(of: effect.effectId) else { return }
        let pitch = FxParamMath.ROW_LIST + gap
        let target = Swift.min(Swift.max(dragStart + Int((translation / pitch).rounded()), 0), order.count - 1)
        if target != current {
            order.remove(at: current)
            order.insert(effect.effectId, at: target)
            dragOrder = order
        }
        dragOffset = translation - CGFloat(target - dragStart) * pitch
    }

    private func finishReorder() {
        guard let id = dragId else { return }
        let order = dragOrder
        dragId = nil; dragOrder = nil; dragOffset = 0
        guard let order, let destination = order.firstIndex(of: id),
              let current = shown.firstIndex(where: { $0.effectId == id }), current != destination,
              let effect = shown.first(where: { $0.effectId == id }) else { return }
        move(effect, toListIndex: destination)
    }

    /// DESLIZAR PARA A ESQUERDA (em toda linha da lista): passou de 35 % da largura
    /// (`fxSwipeCommits`), apaga com "Desfazer" no aviso; antes, volta. Atrás
    /// aparece a faixa vermelha com 🗑, na forma da linha.
    private func swipeable<Content: View>(_ effect: EffectItem, @ViewBuilder content: () -> Content) -> some View {
        let active = swipeId == effect.effectId
        let enabled = overlayId == nil && dragId == nil && !locked
        return ZStack(alignment: .trailing) {
            if active && swipeOffset < 0 {
                RoundedRectangle(cornerRadius: 5).fill(AureaColors.danger)
                    .overlay(alignment: .trailing) {
                        CupertinoGlyph.text(CupertinoGlyph.Trash, size: 18, color: AureaColors.text).padding(.trailing, 14)
                    }
                    .accessibilityHidden(true)
            }
            content().offset(x: active ? swipeOffset : 0)
        }
        .simultaneousGesture(DragGesture(minimumDistance: 12, coordinateSpace: .global)
            .onChanged { value in swipeChanged(effect, value, enabled: enabled) }
            .onEnded { value in swipeEnded(effect, value) }, including: enabled ? .all : .subviews)
        .accessibilityHint(enabled ? AureaText.t("fxui_a11y_swipe_delete") : "")
    }
    private func swipeChanged(_ effect: EffectItem, _ value: DragGesture.Value, enabled: Bool) {
        guard enabled, dragId == nil else { return }
        if swipeId != effect.effectId { swipeId = effect.effectId; swipeOffset = 0; swipeAxis = 0 }
        let dx = value.translation.width, dy = value.translation.height
        // Trava o eixo no primeiro movimento: vertical é a rolagem da lista.
        if swipeAxis == 0 { swipeAxis = abs(dx) > abs(dy) ? 1 : 2 }
        guard swipeAxis == 1 else { return }
        // Tela em árabe: o "para o início da linha" é para a direita.
        let logical = layoutDirection == .rightToLeft ? -dx : dx
        let width = rowFrames[effect.effectId]?.width ?? 0
        swipeOffset = Swift.min(0, Swift.max(-width, logical))
    }
    private func swipeEnded(_ effect: EffectItem, _ value: DragGesture.Value) {
        guard swipeId == effect.effectId else { return }
        let width = rowFrames[effect.effectId]?.width ?? 0
        if swipeAxis == 1 && dragId == nil && fxSwipeCommits(dx: swipeOffset, width: width, minimum: 96) {
            withAnimation(.easeOut(duration: 0.16)) { swipeOffset = -width }
            model.removeEffectWithUndo(effect.effectId)
            swipeId = nil; swipeOffset = 0; swipeAxis = 0
        } else {
            withAnimation(.easeOut(duration: 0.2)) { swipeOffset = 0 }
            swipeAxis = 0
        }
    }

    /// "+ ADICIONAR EFEITO" (`effect_list_add`): 37, raio 5, `fxLowSurface`
    /// (pressionado `fxListSurface`), "+" 12 e o texto 14 centralizados.
    /// Contêiner `effects.add.row`, tocável `aurea.effects.add` (mantido).
    private var addRow: some View {
        Button { showAdd() } label: {
            HStack(spacing: 6) {
                CupertinoGlyph.text(CupertinoGlyph.Plus, size: 12, color: AureaColors.text)
                    .accessibilityHidden(true)
                Text(AureaText.t("panel_adicionar_efeito")).font(.aurea(size: 14)).foregroundStyle(AureaColors.text)
            }
            .frame(maxWidth: .infinity)
            .frame(height: FxParamMath.ROW_LIST)
            .contentShape(Rectangle())
        }
        .buttonStyle(FxAddRowStyle())
        .accessibilityLabel(AureaText.t("panel_adicionar_efeito"))
        .accessibilityIdentifier("aurea.effects.add")
        .padding(.top, gap)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("effects.add.row")
    }

    // --- Ferramentas-efeito na lista (máscaras, rastreio de câmera, legendas) ---
    /// As ferramentas que a camada usa, na ordem das linhas.
    private var tools: [FxEffectTool] {
        guard tabbed else { return [] }
        var out: [FxEffectTool] = []
        if maskTool { out.append(.mask) }
        if cameraTracked { out.append(.cameraTrack) }
        if captionCount > 0 { out.append(.captions) }
        return out
    }
    /// A Máscara posta pelo catálogo fica na lista mesmo vazia (o painel fechou
    /// sem caminho): a linha é a volta para desenhar. Sai só pelo 🗑 dela.
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
    /// A LINHA DE UMA FERRAMENTA no formato da lista: glifo no lugar da seta,
    /// contagem no lugar do selo, 🗑 no lugar do olho, sem ≡. Tocar abre a
    /// ferramenta; a lixeira pede confirmação. Id `effects.tool.<chave>`.
    private func toolRow(_ tool: FxEffectTool) -> some View {
        let key = tool.key.split(separator: ".").last.map(String.init) ?? ""
        let count = tool == .mask ? maskCount : tool == .captions ? captionCount : 0
        let subtitle: String
        switch tool {
        case .mask:
            subtitle = maskCount > 0 ? AureaText.t("fxui_tool_masks_n", maskCount)
                : hasMatte ? AureaText.t("panel_recorte_outra_camada") : AureaText.t("fxui_tool_mask_empty")
        case .captions: subtitle = AureaText.t("fxui_tool_captions_n", captionCount)
        case .cameraTrack: subtitle = AureaText.t("fxui_tool_camera_ready")
        }
        return HStack(spacing: 0) {
            HStack(spacing: 0) {
                CupertinoGlyph.text(tool.glyph, size: 18, color: AureaColors.accent).frame(width: 18, height: 18)
                Text(tool.label).font(.aurea(size: 14)).foregroundStyle(AureaColors.text).lineLimit(1).truncationMode(.tail)
                    .padding(.leading, 6)
                    .frame(maxWidth: .infinity, alignment: .leading)
                Text(count > 0 ? "\(count)" : "").font(.aurea(size: 12).monospacedDigit()).foregroundStyle(AureaColors.fxTipText)
                    .frame(width: 35)
            }
            .padding(.leading, 6)
            .frame(maxWidth: .infinity)
            .frame(height: FxParamMath.ROW_LIST)
            .contentShape(Rectangle())
            .onTapGesture { fxOpenEffectTool(tool, in: model) }
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(AureaText.t("fxui_a11y_open", tool.label))
            .accessibilityValue(subtitle)
            .accessibilityAddTraits(.isButton)
            .accessibilityAction { fxOpenEffectTool(tool, in: model) }
            .accessibilityIdentifier("effects.tool." + key)
            if tool != .cameraTrack {
                Button { confirmRemoveTool(tool) } label: {
                    CupertinoGlyph.text(CupertinoGlyph.Trash, size: 18, color: AureaColors.fxListIcon)
                        .frame(width: 44, height: FxParamMath.ROW_LIST)
                        .contentShape(Rectangle())
                }
                .buttonStyle(AureaPressStyle(shrink: 1))
                .disabled(locked)
                .accessibilityLabel(AureaText.t("fxui_a11y_remove", tool.label))
                .accessibilityIdentifier("effects.tool.remove." + key)
            } else {
                Color.clear.frame(width: 44, height: FxParamMath.ROW_LIST)
            }
            Color.clear.frame(width: 32, height: FxParamMath.ROW_LIST)
        }
        .frame(height: FxParamMath.ROW_LIST)
        .background(AureaColors.fxListSurface, in: RoundedRectangle(cornerRadius: 5))
    }
    private func confirmRemoveTool(_ tool: FxEffectTool) {
        let question = AureaText.t(tool == .mask ? "fxui_tool_remove_masks_q" : "fxui_tool_remove_captions_q")
        model.actionSheet = ActionSheetRequest(title: tool.label, message: question,
                                               actions: [SheetAction(AureaText.t("panel_remover"), destructive: true) { removeTool(tool) }])
    }
    /// TIRA a ferramenta da camada: todas as máscaras (um passo de desfazer) ou as
    /// legendas geradas. O rastreio de câmera não tem "tirar" (paridade Android).
    private func removeTool(_ tool: FxEffectTool) {
        guard let id = model.primarySelection, model.selectedLayer?.locked != true else { return }
        switch tool {
        case .mask:
            // Tirar a linha tira tudo o que ela mostra: as máscaras e o recorte por outra camada.
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

    /// "Intensidade da camada de ajuste": a opacidade da camada, 0–100 %, como linha de slider.
    private var adjustmentIntensity: some View {
        let value = ((model.detail["opacity"] as? NSNumber)?.floatValue ?? 1) * 100
        let label = AureaText.t("panel_opacidade")
        let text = comUnidade(numeroPtBr(value, casas: 0), "%")
        let boxH = FxParamMath.ROW_SLIDER - 2 * FxParamMath.LABEL_MARGIN
        return VStack(alignment: .leading, spacing: 0) {
            Text(AureaText.t("panel_intensidade_camada_ajuste"))
                .font(.aurea(size: 12)).foregroundStyle(AureaColors.fxTipText)
                .padding(.leading, 8).padding(.top, 6)
            HStack(spacing: 0) {
                FxParamLabel(label: label, height: boxH, active: true, keyframe: opacityLook)
                    .padding(FxParamMath.LABEL_MARGIN)
                AureaSlider(value: value, min: 0, max: 100, origin: 0, step: 1, snaps: [100], enabled: !locked,
                            identifier: "effects.intensity.slider",
                            accessibilityLabel: AureaText.t("om_fx_slider", AureaText.t("panel_intensidade_camada_ajuste")),
                            accessibilityValue: text,
                            onBegin: { model.beginGesture("opacidade") }, onValue: { setOpacity($0) }, onEnd: { model.endGesture() })
                    .padding(.horizontal, 8)
                FxValueBox(text: text, height: boxH, enabled: !locked) {
                    model.numericKeypad = KeypadRequest(title: label, value: value, unit: "%", min: 0, max: 100, decimals: 0, onValue: { setOpacity($0) })
                }
                .padding(FxParamMath.LABEL_MARGIN)
            }
            .frame(height: FxParamMath.ROW_SLIDER)
        }
        .background(AureaColors.fxListSurface, in: RoundedRectangle(cornerRadius: 5))
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("effects.intensity")
    }
    private var opacityLook: KeyframeLook {
        guard let layer = model.primarySelection else { return .none }
        let keys = (model.keyframes[layer] ?? []).filter { $0.property == 12 }
        return keys.contains(where: { $0.time == model.localPlayhead }) ? .keyHere : keys.isEmpty ? .none : .animated
    }
    private func setOpacity(_ value: Float) {
        guard let layer = model.primarySelection, model.selectedLayer?.locked != true else { return }
        model.setTransform(12, value: Swift.min(1, Swift.max(0, value / 100)), layer: layer)
    }

    // =========================================================================
    // P0-3 · OS CONTROLES SOBREPOSTOS
    // =========================================================================

    /// Os controles de UM efeito sobre a lista: cabeçalho de 37 (`▾ · Nome · ⋮ ·
    /// 🗑`) e o corpo rolável, cantos de 5, `fxListSurface`. Abre a partir da
    /// linha: recorte vertical de [0, linha] até [0, H] e deslocamento da posição
    /// da linha até 0, juntos. Ids `effects.settings.<id>`, `effects.settings.header`,
    /// `effects.more.<id>`, `effects.remove.<id>`.
    private func settingsOverlay(_ effect: EffectItem, height: CGFloat) -> some View {
        let cardId = fxEffectCardId(effect.typeId)
        let collapsed = gap + FxParamMath.ROW_LIST
        let reveal = collapsed + Swift.max(0, height - collapsed) * overlayProgress
        return VStack(spacing: 0) {
            settingsHeader(effect, cardId: cardId)
            settingsBody(effect).frame(maxHeight: .infinity)
        }
        .background(AureaColors.fxListSurface, in: RoundedRectangle(cornerRadius: 5))
        .clipShape(RoundedRectangle(cornerRadius: 5))
        .padding(gap)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        .mask(alignment: .top) { Rectangle().frame(height: reveal) }
        .offset(y: overlayFrom * (1 - overlayProgress))
        .accessibilityElement(children: .contain)
        .accessibilityLabel(AureaText.t("om_fx_effect_controls", fxEffectDisplayName(effect.typeId, effect.name)))
        .accessibilityIdentifier("effects.settings." + cardId)
    }

    private func settingsHeader(_ effect: EffectItem, cardId: String) -> some View {
        let name = fxEffectDisplayName(effect.typeId, effect.name)
        return HStack(spacing: 0) {
            HStack(spacing: 0) {
                CupertinoGlyph.text(CupertinoGlyph.ChevronDown, size: 18, color: AureaColors.text.opacity(0.9))
                    .frame(width: 18, height: 18)
                Text(displayName(effect))
                    .font(.aurea(size: 14))
                    .foregroundStyle(AureaColors.text)
                    .lineLimit(1)
                    .truncationMode(.tail)
                    .opacity(effect.enabled ? 1 : 0.45)
                    .padding(.leading, 6).padding(.trailing, 4)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
            .padding(.leading, 8)
            .frame(maxWidth: .infinity)
            .frame(height: FxParamMath.ROW_LIST)
            .contentShape(Rectangle())
            .onTapGesture { close(animated: true) }
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(AureaText.t("fxui_a11y_collapse", name))
            .accessibilityAddTraits(.isHeader)
            .accessibilityAddTraits(.isButton)
            .accessibilityAction { close(animated: true) }
            .accessibilityIdentifier("effects.settings.header")
            Button { effectMenuOpen = true } label: {
                CupertinoGlyph.text(CupertinoGlyph.Ellipsis, size: 20, color: AureaColors.fxListIcon)
                    .rotationEffect(.degrees(90))
                    .frame(width: 44, height: FxParamMath.ROW_LIST)
                    .contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle(shrink: 1))
            .accessibilityLabel(AureaText.t("app_a11y_more_options", name))
            .accessibilityIdentifier("effects.more." + cardId)
            .aureaAnchoredMenu(isPresented: $effectMenuOpen, width: 315, alignEnd: true, tag: "effects.menu") {
                effectMenuEntries(effect)
            }
            Button {
                closeNow()
                model.removeEffectWithUndo(effect.effectId)
            } label: {
                CupertinoGlyph.text(CupertinoGlyph.Trash, size: 18, color: AureaColors.fxListIcon)
                    .frame(width: 44, height: FxParamMath.ROW_LIST)
                    .contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle(shrink: 1))
            .disabled(locked)
            .accessibilityLabel(AureaText.t("fxui_a11y_remove", name))
            .accessibilityIdentifier("effects.remove." + cardId)
        }
        .frame(height: FxParamMath.ROW_LIST)
    }

    /// O CORPO, na ordem do P0-3: 1) ferramentas do efeito especial; 2) "Variação"
    /// (≥ 2 presets); 3) linhas soltas; 4) "Avançado" desenhado como seção
    /// (recolhida); 5) seções do motor. Seção recolhida não lista os membros.
    private func settingsBody(_ effect: EffectItem) -> some View {
        let id = effect.effectId
        let bodyLayout = layout(id)
        let advancedIsOpen = advancedOpen.contains(id)
        return ScrollViewReader { proxy in
            ScrollView(.vertical) {
                VStack(spacing: 0) {
                    if !effect.known {
                        FxBodyNotice(text: AureaText.t("panel_este_efeito_saiu_catalogo_ele_nao"))
                    } else {
                        specialTools(effect)
                        if effect.typeId != fxEffectTypeId("aurea.time.remap") {
                            if bodyLayout.isEmpty {
                                FxBodyNotice(text: AureaText.t("panel_este_efeito_nao_tem_ajustes"))
                            } else {
                                variationRow(effect)
                                // EQ paramétrico: o gráfico da resposta em cima das bandas.
                                if effect.typeId == fxEffectTypeId("aurea.audio.parametric_eq") {
                                    FxEqResponseGraph(values: (0..<12).map { i in model.effectParams.first { $0.index == UInt32(i) }?.scalar ?? 0 })
                                }
                                ForEach(bodyLayout.main) { param in parameter(param, effect: id, disabledBy: nil) }
                                if !bodyLayout.advanced.isEmpty {
                                    FxSectionHeader(title: AureaText.t("om_fx_advanced"), open: advancedIsOpen,
                                                    count: bodyLayout.advanced.count, identifier: "effects.section.\(id).advanced",
                                                    onOpen: { value in
                                                        if value { advancedOpen.insert(id) } else { advancedOpen.remove(id) }
                                                    })
                                    if advancedIsOpen {
                                        ForEach(bodyLayout.advanced) { param in parameter(param, effect: id, disabledBy: nil) }
                                    }
                                }
                                ForEach(bodyLayout.sections) { section in sectionView(section, effect: effect) }
                            }
                        }
                    }
                }
                .padding(.bottom, 6)
            }
            .onChange(of: scrollToken) { _ in
                guard let index = scrollParam else { return }
                withAnimation(.easeOut(duration: 0.25)) { proxy.scrollTo("fx.param.\(id).\(index)", anchor: .center) }
            }
        }
        .opacity(effect.enabled ? 1 : 0.45)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("effects.settings.body")
    }

    /// As ferramentas dos efeitos especiais (como antes, agora no topo do corpo).
    @ViewBuilder private func specialTools(_ effect: EffectItem) -> some View {
        let type = effect.typeId
        let localAiBit: UInt32 = type == fxEffectTypeId("aurea.ai.depth_map") ? 1 : type == fxEffectTypeId("aurea.key.rotobrush") ? 2 : 0
        if type == fxEffectTypeId("aurea.key.rotobrush") {
            VStack(spacing: 0) {
                FxBodyNotice(text: AureaText.t("roto_note"))
                RotoPaintCardTools(effectId: effect.effectId,
                    previousView: UInt32(Swift.max(0, Swift.min(2, Int(model.effectParams.first { $0.index == 10 }?.scalar ?? 0)))))
            }
            .padding(.horizontal, 6).padding(.vertical, 4)
        }
        if effect.enabled && localAiBit != 0 {
            if model.localAiActivity & localAiBit != 0 {
                FxBodyNotice(text: AureaText.t("local_ai_processing"))
                    .accessibilityIdentifier("effects.local_ai.processing")
            } else if model.localAiActivity & (localAiBit << 2) != 0 {
                FxBodyNotice(text: AureaText.t("local_ai_failed"))
                    .accessibilityIdentifier("effects.local_ai.failed")
            }
        }
        if type == fxEffectTypeId("aurea.time.remap") {
            TimeRemapEffectEditor(effectId: effect.effectId).padding(.horizontal, 6).padding(.vertical, 4)
        }
        if type == fxEffectTypeId("aurea.distort.mesh_warp") {
            MeshWarpCardTools(effectId: effect.effectId, selected: meshSelected, onSelect: { selected = nil; pointStage.deactivate() })
                .padding(.horizontal, 6).padding(.vertical, 4)
        }
        if type == fxEffectTypeId("aurea.distort.puppet") {
            PuppetCardTools(effectId: effect.effectId).padding(.horizontal, 6).padding(.vertical, 4)
        }
        if type == fxEffectTypeId("aurea.light.saber") { SaberCardTools(effectId: effect.effectId) }
    }

    /// "Variação" (o antigo `EffectPresetRow`) como SELETOR: sem opção marcada;
    /// escolher aplica o preset num passo de desfazer. Id `effects.variation.<efeito>`.
    @ViewBuilder private func variationRow(_ effect: EffectItem) -> some View {
        let presets = model.engine.effectPresets(effect.typeId)
        let labels = stride(from: 0, to: presets.count - 1, by: 2).map { fxPresetLabel(presets[$0], presets[$0 + 1]) }
        if labels.count >= 2 {
            choiceLayout(label: AureaText.t("om_fx_variation"), options: labels, current: -1,
                         segmented: labels.count <= 4 && labels.allSatisfy { $0.count <= 10 },
                         enabled: !locked, labelActive: false, tag: "effects.variation.\(effect.effectId)") { preset in
                guard let layer = model.primarySelection, !locked else { return }
                if model.engine.applyEffectPreset(UInt32(preset), effect: effect.effectId, forLayer: layer) { model.refreshModel(force: true) }
            }
        }
    }

    /// Uma seção do motor: cabeçalho (seta, título, interruptor) e, aberta, as linhas.
    /// Com a seção desligada, o motor manda os membros desabilitados.
    @ViewBuilder private func sectionView(_ section: FxSection, effect: EffectItem) -> some View {
        let id = effect.effectId
        let toggleLabel = section.toggle.map { display($0, effectId: id).label } ?? section.label
        FxSectionHeader(title: section.label, open: section.open, count: section.params.count,
                        identifier: "effects.section.\(id).\(section.ordinal)",
                        toggleIdentifier: "effects.section.toggle.\(id).\(section.ordinal)",
                        toggleLabel: toggleLabel, toggleOn: section.toggle.map { $0.scalar >= 0.5 },
                        toggleEnabled: section.toggle.map { rowEnabled($0) } ?? true,
                        onOpen: { setSectionOpen(id, ordinal: section.ordinal, open: $0) },
                        onToggle: { value in
                            guard let toggle = section.toggle else { return }
                            choose(toggle, effect: id, component: 0, explicit: false)
                            writeComponent(toggle.index, effect: id, component: 0, value: value ? 1 : 0)
                        })
        if section.open {
            ForEach(section.params) { param in parameter(param, effect: id, disabledBy: toggleLabel) }
        }
    }

    // =========================================================================
    // P0-4 · AS LINHAS
    // =========================================================================

    /// Uma linha (com a dica ⓘ do motor acima), com o id que o ◆± usa para rolar.
    private func parameter(_ param: EffectParamItem, effect: UInt32, disabledBy: String?) -> some View {
        VStack(spacing: 0) {
            if !param.tip.isEmpty { FxTipRow(effectId: effect, index: param.index, text: param.tip) }
            parameterRow(param, effect: effect, disabledBy: disabledBy)
        }
        .id("fx.param.\(effect).\(param.index)")
    }

    @ViewBuilder private func parameterRow(_ param: EffectParamItem, effect: UInt32, disabledBy: String?) -> some View {
        let control = FxParamMath.controlFor(param.control, Int(param.type))
        switch Int(param.type) {
        case fxParamFloat, fxParamInt, fxParamAngle:
            numberRow(param, effect: effect, control: control, disabledBy: disabledBy).id(rowIdentity(effect, param.index))
        case fxParamPoint2D, fxParamPoint3D:
            vectorRow(param, effect: effect, control: control, disabledBy: disabledBy).id(rowIdentity(effect, param.index))
        case fxParamBool:
            boolRow(param, effect: effect, disabledBy: disabledBy)
        case fxParamEnum:
            choiceRow(param, effect: effect, disabledBy: disabledBy)
        case fxParamColor:
            colorRow(param, effect: effect, disabledBy: disabledBy)
        case fxParamCurve:
            if let layer = model.primarySelection { ColorCurveEditor(layer: layer, effect: effect, param: param.index) }
        case fxParamTextureRef:
            if let layer = model.primarySelection {
                if typeOf(effect) == fxEffectTypeId("aurea.3d.part_texture") {
                    PartTextureImageRow(layer: layer, effect: effect)
                } else {
                    CubeLutImportRow(layer: layer, effect: effect)
                }
            }
        case fxParamLayerRef:
            layerRefRow(param, effect: effect)
        default:
            // Degradê: o motor tem, o app ainda não edita — sem botão falso.
            HStack(spacing: 0) {
                FxParamLabel(label: display(param, effectId: effect).label, height: FxParamMath.ROW_SMALL - 2 * FxParamMath.LABEL_MARGIN)
                    .padding(FxParamMath.LABEL_MARGIN)
                Text(AureaText.t("panel_ainda_nao_editavel_app")).font(.aurea(size: 12)).foregroundStyle(AureaColors.fxTipText)
                    .padding(.horizontal, 8)
                Spacer(minLength: 0)
            }
            .frame(height: FxParamMath.ROW_SMALL)
        }
    }

    /// NÚMERO (Float / Int / Ângulo): slider (51) ou régua (57), pelo controle do
    /// motor. Tudo na UNIDADE HUMANA (motor × escala); a escrita desfaz a escala e
    /// prende na faixa DIGITADA. Tocar no valor: teclado. Ids
    /// `effects.param.<e>.<i>.0` (linha), `effects.slider|ruler.<e>.<i>`.
    private func numberRow(_ param: EffectParamItem, effect: UInt32, control: Int, disabledBy: String?) -> some View {
        let d = display(param, effectId: effect)
        let key = EffectParamSelection(effect: effect, param: param.index, component: 0)
        let enabled = rowEnabled(param)
        let ok = writeGuard(effect)
        let lo = d.toDisplay(param.minValue), hi = d.toDisplay(param.maxValue)
        let shownLo = Swift.min(lo, hi), shownHi = Swift.max(lo, hi)
        // Duas faixas (EffectsPanel.kt): a régua/slider anda na do motor (minValue...maxValue);
        // o TECLADO aceita a digitada (hardMin...hardMax). A escrita prende só na digitada.
        let typedMin = Swift.min(param.hardMin, param.minValue), typedMax = Swift.max(param.hardMax, param.maxValue)
        let typedLo = d.toDisplay(typedMin), typedHi = d.toDisplay(typedMax)
        let step = displayStep(param, d)
        let def = d.toDisplay(param.defaultValue.first ?? 0)
        let shown = d.toDisplay(param.value.first ?? 0)
        let format: (Float) -> String = { comUnidade(numeroPtBr($0, casas: d.decimals), d.suffix) }
        let slider = control == FxParamControl.slider && shownLo.isFinite && shownHi.isFinite && shownHi > shownLo
        let rowH = slider ? FxParamMath.ROW_SLIDER : FxParamMath.ROW_RULER
        let boxH = rowH - 2 * FxParamMath.LABEL_MARGIN
        let off = disabledText(disabledBy)
        let isInt = Int(param.type) == fxParamInt
        let write: (Float) -> Void = { value in
            guard ok(), value.isFinite else { return }
            let core = Swift.min(typedMax, Swift.max(typedMin, d.toEngine(value)))
            writeComponent(param.index, effect: effect, component: 0, value: isInt ? core.rounded() : core)
        }
        let keypad: () -> Void = {
            choose(param, effect: effect, component: 0, explicit: false)
            // "50%" continua sendo 50 % do fim da RÉGUA (não do teto digitado).
            model.numericKeypad = KeypadRequest(title: d.label, value: shown, unit: d.suffix, min: Swift.min(typedLo, typedHi), max: Swift.max(typedLo, typedHi),
                                                decimals: d.decimals, percentBase: shownHi, onValue: write)
        }
        let begin: () -> Void = { choose(param, effect: effect, component: 0, explicit: false); model.beginGesture("ajustar " + d.label) }
        let turns: (turns: Int, rest: Float) = Int(param.type) == fxParamAngle ? FxParamMath.angleTurns(shown) : (turns: 0, rest: shown)
        return HStack(spacing: 0) {
            FxParamLabel(label: d.label, height: boxH, active: selected == key, enabled: enabled,
                         keyframe: look(effect: effect, param: param.index, component: 0), expression: expressionLooks[param.index] ?? .none,
                         onTap: { choose(param, effect: effect, component: 0, explicit: true) },
                         onLongPress: { paramMenu(param, effect: effect) })
                .padding(FxParamMath.LABEL_MARGIN)
            if slider {
                AureaSlider(value: shown, min: shownLo, max: shownHi, origin: d.toDisplay(param.origin),
                            step: isInt ? Swift.max(1, step) : step, snaps: [def], enabled: enabled,
                            identifier: "effects.slider.\(effect).\(param.index)",
                            accessibilityLabel: AureaText.t("om_fx_slider", d.label),
                            accessibilityValue: enabled ? format(shown) : off,
                            onBegin: begin, onValue: write, onEnd: { model.endGesture() })
                    .padding(.horizontal, 8)
                FxValueBox(text: format(shown), height: boxH, enabled: enabled, onTap: keypad)
                    .padding(FxParamMath.LABEL_MARGIN)
            } else {
                FxRuler(value: shown, step: step, min: shownLo, max: shownHi,
                        snaps: [def, 0].filter { $0.isFinite && $0 >= shownLo && $0 <= shownHi },
                        enabled: enabled, identifier: "effects.ruler.\(effect).\(param.index)",
                        accessibilityLabel: AureaText.t("om_fx_ruler", d.label),
                        accessibilityValue: enabled ? format(shown) : off,
                        onBegin: begin, onValue: write, onEnd: { model.endGesture() })
                    .frame(height: boxH)
                FxValueBox(text: format(turns.rest), height: boxH, aux: turns.turns != 0 ? "\(turns.turns)× +" : nil,
                           enabled: enabled, onTap: keypad)
                    .padding(FxParamMath.LABEL_MARGIN)
            }
        }
        .frame(height: rowH)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("effects.param.\(effect).\(param.index).0")
    }

    /// PONTO / XYZ / ORIENTAÇÃO (57): rótulo · régua do eixo escolhido (agulha na
    /// cor do eixo) · caixas por eixo. 1º toque numa caixa escolhe o eixo; 2º toque
    /// no eixo já escolhido abre o teclado; TOQUE LONGO zera aquele componente
    /// (Point também). Orientação: α/β/γ e rótulo de 42. Ids
    /// `effects.vector.<e>.<i>` (linha), `.ruler` e `effects.axis.<e>.<i>.<c>`.
    private func vectorRow(_ param: EffectParamItem, effect: UInt32, control: Int, disabledBy: String?) -> some View {
        let d = display(param, effectId: effect)
        let orientation = control == FxParamControl.orientation
        let comps = Swift.min(Swift.max(fxComponentCount(Int(param.type)), 2), 3)
        let own = selected?.effect == effect && selected?.param == param.index
        let axis = own ? Swift.min(comps - 1, Swift.max(0, selected?.component ?? 0)) : 0
        let values = (0..<comps).map { d.toDisplay($0 < param.value.count ? param.value[$0] : 0) }
        let enabled = rowEnabled(param)
        let ok = writeGuard(effect)
        let lo = d.toDisplay(param.minValue), hi = d.toDisplay(param.maxValue)
        let typedMin = Swift.min(param.hardMin, param.minValue), typedMax = Swift.max(param.hardMax, param.maxValue)
        let typedLo = d.toDisplay(typedMin), typedHi = d.toDisplay(typedMax)
        let step = displayStep(param, d)
        let names = orientation ? ["α", "β", "γ"] : ["X", "Y", "Z"]
        let spoken = orientation ? [AureaText.t("om_fx_axis_pitch"), AureaText.t("om_fx_axis_yaw"), AureaText.t("om_fx_axis_roll")] : names
        let format: (Float) -> String = { comUnidade(numeroPtBr($0, casas: d.decimals), d.suffix) }
        let compact: (Float) -> String = { compactPropertyNumber($0, decimals: d.decimals) }
        let off = disabledText(disabledBy)
        let tag = "effects.vector.\(effect).\(param.index)"
        let write: (Int, Float) -> Void = { component, value in
            guard ok(), value.isFinite else { return }
            writeComponent(param.index, effect: effect, component: component, value: Swift.min(typedMax, Swift.max(typedMin, d.toEngine(value))))
        }
        // P2-2: um teclado com TODOS os eixos (uma aba por eixo), foco no tocado; o OK
        // grava o vetor inteiro num passo de desfazer (par do `FxVectorRow.keypad`).
        let keypad: (Int) -> Void = { component in
            choose(param, effect: effect, component: component, explicit: false)
            let fields = (0..<comps).map { c in
                KeypadField(label: names[c], initial: values[c],
                            defaultValue: d.toDisplay(c < param.defaultValue.count ? param.defaultValue[c] : 0),
                            min: Swift.min(typedLo, typedHi), max: Swift.max(typedLo, typedHi))
            }
            model.numericKeypad = KeypadRequest(title: d.label, value: values[component], unit: d.suffix,
                                                min: Swift.min(typedLo, typedHi), max: Swift.max(typedLo, typedHi),
                                                decimals: d.decimals, percentBase: Swift.max(lo, hi),
                                                onValue: { write(component, $0) },
                                                fields: fields, focus: component,
                                                onConfirmFields: { edits in
                guard ok(), edits.contains(where: { $0 != nil }), let current = currentParam(param.index) else { return }
                var next = Array((current.value + [0, 0, 0, 1]).prefix(4))
                for (c, edit) in edits.enumerated() where c < comps {
                    if let edit, edit.isFinite { next[c] = Swift.min(typedMax, Swift.max(typedMin, d.toEngine(edit))) }
                }
                model.beginGesture("ajustar " + d.label)
                writeVector(param.index, effect: effect, values: Array(next.prefix(comps)))
                model.endGesture()
            })
        }
        let zero: (Int) -> Void = { component in
            guard ok(), enabled else { return }
            choose(param, effect: effect, component: component, explicit: false)
            model.beginGesture("zerar " + names[component])
            write(component, 0)
            model.endGesture()
        }
        let tapAxis: (Int) -> Void = { component in
            if own && component == axis { keypad(component) } else { choose(param, effect: effect, component: component, explicit: true) }
        }
        func box(_ component: Int, letter: String?) -> FxAxisBox {
            FxAxisBox(text: compact(values[component]), color: fxAxisColor(component), active: own && component == axis,
                      enabled: enabled, letter: letter, accessibilityLabel: d.label + " · " + spoken[component],
                      accessibilityValue: enabled ? format(values[component]) : off,
                      resetLabel: AureaText.t("om_fx_axis_reset", spoken[component]),
                      identifier: "effects.axis.\(effect).\(param.index).\(component)",
                      onTap: { tapAxis(component) }, onLongPress: { zero(component) })
        }
        return HStack(spacing: 0) {
            FxParamLabel(label: d.label, height: orientation ? 42 : FxParamMath.ROW_RULER - 12, active: own, enabled: enabled,
                         keyframe: look(effect: effect, param: param.index, component: axis), expression: expressionLooks[param.index] ?? .none,
                         onTap: { choose(param, effect: effect, component: axis, explicit: true) },
                         onLongPress: { paramMenu(param, effect: effect) })
                .padding(FxParamMath.LABEL_MARGIN)
            // Uma régua por eixo: trocar de eixo encerra o gesto (e o arremesso) do anterior.
            FxRuler(value: values[axis], step: step, min: Swift.min(lo, hi), max: Swift.max(lo, hi),
                    snaps: [Float(0)].filter { $0 >= Swift.min(lo, hi) && $0 <= Swift.max(lo, hi) },
                    needle: fxAxisColor(axis), enabled: enabled, identifier: tag + ".ruler",
                    accessibilityLabel: d.label + " · " + spoken[axis],
                    accessibilityValue: enabled ? format(values[axis]) : off,
                    onBegin: { choose(param, effect: effect, component: axis, explicit: false); model.beginGesture("ajustar " + d.label) },
                    onValue: { write(axis, $0) }, onEnd: { model.endGesture() })
                .frame(height: FxParamMath.ROW_RULER - 12)
                .id(axis)
            // Números e letras dos eixos sempre da esquerda para a direita.
            Group {
                if comps == 2 && control == FxParamControl.point {
                    VStack(spacing: 0) {
                        ForEach(0..<2, id: \.self) { component in
                            HStack(spacing: 0) {
                                Text(names[component] + ":").font(.aurea(size: 10)).foregroundStyle(AureaColors.fxAxisLabel)
                                    .frame(width: 16, alignment: .leading)
                                box(component, letter: nil)
                            }
                            .padding(.top, component == 0 ? 7 : 1)
                            .padding(.bottom, component == 0 ? 1 : 7)
                            .frame(maxHeight: .infinity)
                        }
                    }
                    .frame(width: 55 + 16)
                    .padding(.trailing, FxParamMath.LABEL_MARGIN)
                } else {
                    HStack(spacing: 2) {
                        ForEach(0..<comps, id: \.self) { component in box(component, letter: names[component]).frame(width: 36) }
                    }
                    .frame(height: FxParamMath.ROW_RULER - 12)
                    .padding(.trailing, FxParamMath.LABEL_MARGIN)
                }
            }
            .keepLtr()
        }
        .frame(height: FxParamMath.ROW_RULER)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier(tag)
    }

    /// INTERRUPTOR (38): padding 8 · rótulo em TEXTO SIMPLES (sem chip e sem ◆, como
    /// a referência) · interruptor. Tocar no texto escolhe o parâmetro; segurar abre
    /// o menu do parâmetro (extra do AUREA). Id `effects.toggle.<e>.<i>`.
    private func boolRow(_ param: EffectParamItem, effect: UInt32, disabledBy: String?) -> some View {
        let d = display(param, effectId: effect)
        let enabled = rowEnabled(param)
        let ok = writeGuard(effect)
        let on = param.scalar >= 0.5
        let chosen = selected?.effect == effect && selected?.param == param.index
        let off = disabledText(disabledBy)
        return HStack(spacing: 0) {
            Text(d.label)
                .font(.aurea(size: 14))
                .foregroundStyle(!enabled ? AureaColors.fxDisabledText : chosen ? AureaColors.accent : AureaColors.text)
                .lineLimit(2)
                .truncationMode(.tail)
                .frame(maxWidth: .infinity, alignment: .leading)
                .contentShape(Rectangle())
                .onTapGesture { if enabled { choose(param, effect: effect, component: 0, explicit: true) } }
                .onLongPressGesture(minimumDuration: 0.5) { if enabled { paramMenu(param, effect: effect) } }
                .accessibilityHidden(true)
            AureaToggle(checked: on, enabled: enabled) { value in
                choose(param, effect: effect, component: 0, explicit: false)
                guard ok() else { return }
                writeComponent(param.index, effect: effect, component: 0, value: value ? 1 : 0)
            }
            .accessibilityLabel(d.label)
            .accessibilityValue(enabled ? AureaText.t(on ? "common_on" : "common_off") : off)
            .accessibilityIdentifier("effects.toggle.\(effect).\(param.index)")
        }
        .padding(.horizontal, 8)
        .frame(height: FxParamMath.ROW_SMALL)
    }

    /// ESCOLHA (38): rótulo 10 (até 75, margem 15) · botões segmentados de 27,5
    /// (estilo do motor `enumSegmented`) ou caixa suspensa (`enumDropdown`) que abre
    /// o menu ancorado. Índice arredondado e preso. Ids `effects.choice.<e>.<i>[.<k>]`.
    private func choiceRow(_ param: EffectParamItem, effect: UInt32, disabledBy: String?) -> some View {
        let d = display(param, effectId: effect)
        let enabled = rowEnabled(param)
        let ok = writeGuard(effect)
        let options = enumOptions(param)
        let top = Swift.max(0, options.count - 1)
        let current = param.scalar.isFinite ? Int(param.scalar.rounded().clamped(to: 0...Float(top))) : 0
        let chosen = selected?.effect == effect && selected?.param == param.index
        let segmented = param.enumStyle == FxParamControl.enumSegmented
            || (param.enumStyle == 0 && options.count <= 4 && options.allSatisfy { $0.count <= 10 })
        return choiceLayout(label: d.label, options: options, current: current, segmented: segmented, enabled: enabled,
                            labelActive: chosen, tag: "effects.choice.\(effect).\(param.index)",
                            disabledState: enabled ? nil : disabledText(disabledBy),
                            onLabel: { choose(param, effect: effect, component: 0, explicit: true) },
                            onLabelLong: { paramMenu(param, effect: effect) }) { index in
            choose(param, effect: effect, component: 0, explicit: false)
            guard ok() else { return }
            writeComponent(param.index, effect: effect, component: 0, value: Float(index))
        }
    }

    /// A montagem da linha de escolha (também a da "Variação", sem opção vigente: `current` < 0).
    private func choiceLayout(label: String, options: [String], current: Int, segmented: Bool, enabled: Bool,
                              labelActive: Bool, tag: String, disabledState: String? = nil,
                              onLabel: (() -> Void)? = nil, onLabelLong: (() -> Void)? = nil,
                              onPick: @escaping (Int) -> Void) -> some View {
        HStack(spacing: 0) {
            Text(label)
                .font(.aurea(size: 10))
                .foregroundStyle(!enabled ? AureaColors.fxDisabledText : labelActive ? AureaColors.accent : AureaColors.text)
                .lineLimit(2)
                .truncationMode(.tail)
                .frame(maxWidth: FxParamMath.LABEL_W, alignment: .leading)
                .contentShape(Rectangle())
                .onTapGesture { if enabled { onLabel?() } }
                .onLongPressGesture(minimumDuration: 0.5) { if enabled { onLabelLong?() } }
                .accessibilityHidden(true)
            Spacer().frame(width: 15)
            if segmented {
                FxSegmented(options: options, selected: current, enabled: enabled, identifier: tag, label: label, onSelect: onPick)
            } else {
                FxDropdownBox(text: options.indices.contains(current) ? options[current] : "—", options: options, selected: current,
                              enabled: enabled, tag: tag, label: label, onPick: onPick)
            }
        }
        .padding(.leading, 8)
        .padding(.trailing, FxParamMath.LABEL_MARGIN)
        .frame(height: FxParamMath.ROW_SMALL)
        .accessibilityElement(children: .contain)
        .accessibilityValue(disabledState ?? "")
    }

    /// COR (38): rótulo 75 · "R G B" 10 em `fxColorText` · amostra 24,5 (alvo 44)
    /// que abre o seletor; a folha inteira = UM passo de desfazer.
    private func colorRow(_ param: EffectParamItem, effect: UInt32, disabledBy: String?) -> some View {
        let d = display(param, effectId: effect)
        let enabled = rowEnabled(param)
        let shownColor = AureaColorSpace.engineToDisplay(param.value)
        let rgb = (0..<3).map { index -> String in
            let v = index < shownColor.count && shownColor[index].isFinite ? shownColor[index].clamped(to: 0...1) : 0
            return String(Int((v * 255).rounded()))
        }.joined(separator: " ")
        let chosen = selected?.effect == effect && selected?.param == param.index
        let off = disabledText(disabledBy)
        return HStack(spacing: 0) {
            FxParamLabel(label: d.label, height: FxParamMath.ROW_SMALL - 2 * FxParamMath.LABEL_MARGIN, active: chosen, enabled: enabled,
                         keyframe: look(effect: effect, param: param.index), expression: expressionLooks[param.index] ?? .none,
                         onTap: { choose(param, effect: effect, component: 0, explicit: true) },
                         onLongPress: { paramMenu(param, effect: effect) })
                .padding(FxParamMath.LABEL_MARGIN)
            Text(rgb)
                .font(.aurea(size: 10).monospacedDigit())
                .foregroundStyle(enabled ? AureaColors.fxColorText : AureaColors.fxDisabledText)
                .lineLimit(1)
                .padding(.horizontal, 8)
                .keepLtr()
                .frame(maxWidth: .infinity, alignment: .leading)
            Button {
                choose(param, effect: effect, component: 0, explicit: false)
                model.beginGesture("cor")
                model.colorSheet = ColorSheetRequest(title: d.label,
                    initial: AureaColorSpace.engineToDisplay(param.value), onChange: { r, g, b, a in
                        writeVector(param.index, effect: effect, values: AureaColorSpace.displayToEngine(r, g, b, a))
                    }, onDone: { model.endGesture() })
            } label: {
                AureaColorSwatch(color: color(param.value))
                    .frame(width: 24.5, height: 24.5)
                    .clipShape(RoundedRectangle(cornerRadius: 3))
                    .overlay(RoundedRectangle(cornerRadius: 3).stroke(AureaColors.fxLowSurface, lineWidth: 0.5))
                    .padding(.trailing, 6)
                    .frame(width: 44, height: FxParamMath.ROW_SMALL, alignment: .trailing)
                    .contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle())
            .disabled(!enabled)
            .accessibilityLabel(d.label)
            .accessibilityValue(enabled ? rgb : off)
            .accessibilityIdentifier("effects.color.\(effect).\(param.index)")
        }
        .frame(height: FxParamMath.ROW_SMALL)
    }

    /// Outra camada ("Camada de áudio"): o menu lista as camadas da composição; o
    /// motor recebe o ÍNDICE da camada (−1 = nenhuma).
    private func layerRefRow(_ param: EffectParamItem, effect: UInt32) -> some View {
        let d = display(param, effectId: effect)
        return HStack(spacing: 0) {
            FxParamLabel(label: d.label, height: FxParamMath.ROW_SMALL - 2 * FxParamMath.LABEL_MARGIN,
                         onTap: { selected = EffectParamSelection(effect: effect, param: param.index, component: 0) },
                         onLongPress: { paramMenu(param, effect: effect) })
                .padding(FxParamMath.LABEL_MARGIN)
            Spacer(minLength: 0)
            Menu {
                Button(AureaText.t("afx_layer_none")) { writeLayerRef(param.index, effect: effect, layerIndex: -1) }
                ForEach(model.layers.filter { $0.id != model.primarySelection && fxAcceptsLayer(param.flags, kind: $0.kind) }) { layer in
                    Button(layer.name.isEmpty ? "#\(fxLayerIndex(layer.id))" : layer.name) {
                        writeLayerRef(param.index, effect: effect, layerIndex: fxLayerIndex(layer.id))
                    }
                }
            } label: {
                Text(layerRefName(param)).font(.aurea(size: 13)).foregroundStyle(AureaColors.accent).lineLimit(1)
                    .padding(.horizontal, 8).frame(minHeight: FxParamMath.ROW_SMALL)
            }
            .disabled(locked)
        }
        .frame(height: FxParamMath.ROW_SMALL)
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
        let count = span.isFinite ? Swift.max(1, Swift.min(256, Int(span.clamped(to: 0...255).rounded()) + 1)) : 1
        return (0..<count).map { String($0 + 1) }
    }
    private func look(effect: UInt32, param: UInt32, component: Int? = nil) -> KeyframeLook {
        guard let layer = model.primarySelection else { return .none }
        let keys = (model.keyframes[layer] ?? []).filter { $0.property == 31 && $0.effectIndex == effect && $0.paramIndex / 4 == param && (component == nil || $0.paramIndex % 4 == UInt32(component!)) }
        return keys.contains(where: { $0.time == model.localPlayhead }) ? .keyHere : keys.isEmpty ? .none : .animated
    }

    // =========================================================================
    // P0-5 · OS MENUS
    // =========================================================================

    /// O ⋮ do efeito aberto (ordem da referência): Detalhes do efeito · Restaurar
    /// padrão · Duplicar · Copiar efeito · Colar configurações do efeito (só com UM
    /// efeito do MESMO tipo copiado). Depois, "Mais do AUREA": Expressão do
    /// parâmetro escolhido · Salvar como preset do efeito · Preset Turbulência dinâmica.
    private func effectMenuEntries(_ effect: EffectItem) -> [MenuEntry] {
        let id = effect.effectId
        let entry = model.effectCatalog.first { $0.typeId == effect.typeId }
        var items: [MenuEntry] = [
            .item(AureaText.t("om_fx_menu_details"), enabled: entry != nil, tag: "effects.menu.details") { about = entry },
            .item(AureaText.t("om_fx_menu_reset_defaults"), enabled: !locked, tag: "effects.menu.reset") { reset(effect) },
            .item(AureaText.t("common_duplicate"), enabled: !locked, tag: "effects.menu.duplicate") {
                model.duplicateEffect(id)
                close(animated: true)
            },
            .item(AureaText.t("om_fx_menu_copy_effect"), tag: "effects.menu.copy") {
                if let layer = model.primarySelection { model.engine.copyEffect(id, fromLayer: layer) }
            },
        ]
        let clipboardType = model.clipboardEffectType
        if clipboardType != 0 && clipboardType == effect.typeId {
            items.append(.item(AureaText.t("om_fx_menu_paste_settings"), enabled: !locked, tag: "effects.menu.paste_settings") {
                model.pasteEffectSettings(id)
            })
        }
        items.append(.divider)
        items.append(.header(AureaText.t("om_fx_more_from_aurea")))
        if let chosen = selected, chosen.effect == id, openId == id, let param = currentParam(chosen.param),
           param.flags & 1 != 0, fxComponentCount(Int(param.type)) > 0 {
            items.append(.item(AureaText.t("fx_param_expression", display(param, effectId: id).label), tag: "effects.menu.expression") {
                openExpression()
            })
        }
        items.append(.item(AureaText.t("om_fx_menu_save_preset"), tag: "effects.menu.save_preset") { saveDialog(effect: id) })
        if effect.typeId == fxEffectTypeId("aurea.distort.turbulence") {
            items.append(.item(AureaText.t("app_preset_fx_turbulence"), enabled: !locked, tag: "effects.menu.turbulence") {
                applyTurbulencePreset(effect)
            })
        }
        return items
    }

    /// O ⋯ da lista (coluna esquerda, inativo com os controles abertos): Copiar
    /// efeitos · Colar efeito(s) — desabilitados sem conteúdo — · Ligar/Desligar
    /// todos · Criar efeito… · Meus efeitos · Importar efeitos de projeto….
    private func listMenuEntries() -> [MenuEntry] {
        let effects = shown
        let anyOn = effects.contains(where: \.enabled)
        let clipboardType = model.clipboardEffectType
        let hasClipboard = model.engine.clipboardState & 4 != 0
        return [
            .item(AureaText.t("panel_copiar_efeitos"), enabled: !effects.isEmpty, tag: "effects.list.copy") {
                if let layer = model.primarySelection { model.engine.copyEffects(layer) }
            },
            .item(AureaText.t(clipboardType != 0 ? "om_fx_list_paste_one" : "panel_colar_efeitos"),
                  enabled: hasClipboard && !locked, tag: "effects.list.paste") {
                model.engine.pasteEffects(model.selection.map { NSNumber(value: $0) }); model.refreshModel(force: true)
            },
            .divider,
            .item(AureaText.t(anyOn ? "panel_desligar_todos" : "panel_ligar_todos"), enabled: !effects.isEmpty && !locked,
                  tag: "effects.list.toggle_all") {
                model.beginGesture(anyOn ? "desligar efeitos" : "ligar efeitos")
                for effect in effects where effect.enabled == anyOn { enable(effect, !anyOn) }
                model.endGesture()
            },
            .item(AureaText.t("fx_custom_create") + "…", enabled: !effects.isEmpty, tag: "effects.list.create") { saveDialog(effect: nil) },
            .item(AureaText.t("fx_custom_library"), tag: "effects.list.library") {
                model.presetsOpenKind = "efeitos"; model.openPanel(.presets)
            },
            .item(AureaText.t("am_import_action"), tag: "effects.list.import") {
                DocumentImportPicker.present(types: [.data, .xml, .zip]) { urls in amPick = urls?.first }
            },
        ]
    }

    /// "Criar efeito…" (todos os efeitos da camada como preset de efeitos) ou
    /// "Salvar como preset do efeito" (só este): o nome num diálogo claro.
    private func saveDialog(effect: UInt32?) {
        guard let layer = model.primarySelection else { return }
        let title = AureaText.t(effect != nil ? "om_fx_menu_save_preset" : "fx_custom_create")
        AureaPopupPresenter.shared.presentDialog { close in
            FxSaveEffectDialog(title: title, onSave: { name in
                guard layer == model.primarySelection else { return true }
                if let effect { model.saveEffectPreset(effectId: effect, name: name); return true }
                let json = model.engine.savePreset(layer, kind: 0, name: name, parts: 3)
                guard !json.isEmpty, let saved = model.storeEffectPreset(name: name, json: json) else {
                    model.toast = AureaText.t("msg_nao_foi_possivel_salvar_o_preset"); return false
                }
                model.toast = AureaText.t("msg_preset_salvo", saved)
                return true
            }, onDismiss: close)
        }
    }

    /// "Preset Turbulência dinâmica" (⋮ do Turbulência): quantidade e tamanho 15,
    /// complexidade 1, o resto zerado e a evolução por expressão `time*6`.
    private func applyTurbulencePreset(_ effect: EffectItem) {
        guard let layer = model.primarySelection, model.selectedLayer?.locked != true else { return }
        model.beginGesture("Preset Turbulência dinâmica")
        for (param, value) in [(UInt32(0), Float(15)), (1, 15), (2, 1), (3, 0), (4, 0), (5, 0), (6, 0)] {
            model.engine.setEffect(effect.effectId, forLayer: layer, paramIndex: param, value: value)
        }
        let error = model.engine.setExpression(layer, property: 31, effect: effect.effectId, param: 24, source: "time*6")
        model.endGesture(); model.refreshModel(force: true)
        if !error.isEmpty { model.toast = error }
    }

    /// Segurar o rótulo (extra do AUREA): Redefinir · Expressão, com o valor padrão.
    private func paramMenu(_ param: EffectParamItem, effect: UInt32) {
        let d = display(param, effectId: effect)
        var actions: [SheetAction] = [SheetAction(AureaText.t("panel_redefinir"), enabled: !locked) {
            model.beginGesture("redefinir parâmetro"); writeVector(param.index, effect: effect, values: param.defaultValue); model.endGesture()
        }]
        if param.flags & 1 != 0 && fxComponentCount(Int(param.type)) > 0 {
            actions.append(SheetAction(AureaText.t("panel_expressao_3c65")) {
                selected = EffectParamSelection(effect: effect, param: param.index, component: 0); openExpression()
            })
        }
        model.actionSheet = ActionSheetRequest(title: d.label, message: AureaText.t("fx_default_value", defaultText(param, d)), actions: actions)
    }
    /// O valor padrão como a linha mostraria ("100%", "0°", "Ligado", a opção).
    private func defaultText(_ param: EffectParamItem, _ d: FxParamDisplay) -> String {
        let v = param.defaultValue
        switch Int(param.type) {
        case fxParamBool:
            return AureaText.t((v.first ?? 0) >= 0.5 ? "common_on" : "common_off")
        case fxParamEnum:
            let options = enumOptions(param)
            let index = Int((v.first ?? 0).rounded())
            return options.indices.contains(index) ? options[index] : String(index + 1)
        case fxParamColor:
            let rgba = AureaColorSpace.engineToDisplay(v)
            return "RGB " + (0..<3).map { String(Int((rgba[$0].clamped(to: 0...1) * 255).rounded())) }.joined(separator: " ")
        case fxParamPoint2D, fxParamPoint3D:
            return (0..<fxComponentCount(Int(param.type))).map {
                comUnidade(numeroPtBr(d.toDisplay($0 < v.count ? v[$0] : 0), casas: d.decimals), d.suffix)
            }.joined(separator: " × ")
        default:
            return comUnidade(numeroPtBr(d.toDisplay(v.first ?? 0), casas: d.decimals), d.suffix)
        }
    }

    // =========================================================================
    // Coluna esquerda: keyframe, curva, expressão
    // =========================================================================

    private func keyframeTapped() {
        toggleSelectedKey()
        // P1-5: o corpo rola até a linha do parâmetro escolhido.
        if !meshSelected, let s = railSelection, s.effect == openId {
            scrollParam = s.param
            scrollToken += 1
        }
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
        if meshSelected, let effect = openId, let layer = model.primarySelection, model.selectedLayer?.locked != true {
            if model.engine.meshWarpToggleKey(layer, effect: Int32(truncatingIfNeeded: effect)) {
                MeshWarpStageState.shared.revision += 1; model.refreshModel(force: true)
            }
            return
        }
        guard let selection = railSelection, let param = currentParam(selection.param), let layer = model.primarySelection,
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
    /// Segurar ◆ com keyframe no quadro: apaga TODAS as marcas do parâmetro, um passo.
    private func deleteAllKeys() {
        guard let selection = railSelection, let layer = model.primarySelection, model.selectedLayer?.locked != true else { return }
        let keys = (model.keyframes[layer] ?? []).filter { $0.property == 31 && $0.effectIndex == selection.effect && $0.paramIndex / 4 == selection.param }
        guard !keys.isEmpty else { return }
        model.beginGesture("apagar keyframes")
        for key in keys {
            model.engine.editTrackKey(layer, property: 31, effect: key.effectIndex, param: key.paramIndex, time: key.time, action: 1, value: key.value, targetTime: key.time, interpolation: key.interpolation, handles: [])
        }
        model.endGesture(); model.commitPendingCommands(); model.refreshModel(force: true)
    }
    private func updateTimelineFocus() {
        if meshSelected, let effect = openId {
            model.timelineFocus = [TimelineTrack(property: 44, effect: effect, param: 0)]
        } else if let layer = model.primarySelection, let effect = openId, puppet.layer == layer,
           puppet.effect >= 0, UInt32(puppet.effect) == effect,
           model.effects.contains(where: { $0.effectId == effect && $0.typeId == PuppetStageState.type }),
           puppet.editing || selected == nil {
            let pins = model.puppetPins(layer, effect: puppet.effect)
            let active = stride(from: 0, to: pins.count, by: 4).compactMap { index -> Int? in
                guard index + 3 < pins.count, pins[index].isFinite, pins[index] >= 0, pins[index] <= 15 else { return nil }
                let pin = Int(pins[index]); return Float(pin) == pins[index] ? pin : nil
            }
            let chosen = active.contains(puppet.selected) ? [puppet.selected] : active
            model.timelineFocus = chosen.flatMap { pin in
                let position = UInt32((5 + pin * 3) * 4)
                return [TimelineTrack(property: 31, effect: effect, param: position), TimelineTrack(property: 31, effect: effect, param: position + 1)]
            }
        } else {
            model.timelineFocus = selected.map { [TimelineTrack(property: 31, effect: $0.effect, param: $0.param * 4 + UInt32($0.component))] } ?? []
        }
    }
    private func openCurve() {
        guard let selected = railSelection else { return }
        let track = selectedTrack
        guard track.count >= 2 else { return }
        let index = Swift.min(track.count - 2, track.lastIndex(where: { $0.time <= model.localPlayhead }) ?? 0)
        let key = track[index]
        model.openCurve(property: 31, effect: selected.effect, param: selected.param * 4 + UInt32(selected.component), time: key.time)
    }
    private func openExpression() {
        guard let selected, let param = selectedParam, let layer = model.primarySelection, canAnimate else { return }
        let d = display(param, effectId: selected.effect)
        let tracks = (0..<fxComponentCount(Int(param.type))).map { ExpressionTrack(property: 31, effect: selected.effect, param: param.index * 4 + UInt32($0)) }
        model.expressionSheet = ExpressionRequest(layer: layer, label: d.label, tracks: tracks, scale: d.scale, unit: d.suffix)
    }

    // =========================================================================
    // Escrita
    // =========================================================================

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
    /// comando (velho R, G, B + novo A) desfazia a cor escolhida.
    private func writeVector(_ index: UInt32, effect: UInt32, values: [Float]) {
        guard let layer = model.primarySelection, let param = currentParam(index), model.selectedLayer?.locked != true,
              param.flags & 16 == 0, values.allSatisfy(\.isFinite) else { return }
        let count = Swift.min(fxComponentCount(Int(param.type)), values.count)
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
    /// Move para a posição `index` da LISTA; com decorações de Borda e sombra na
    /// pilha, o índice do motor é o do efeito que ocupa aquela posição.
    private func move(_ effect: EffectItem, toListIndex index: Int) {
        guard let layer = model.primarySelection, model.selectedLayer?.locked != true else { return }
        let list = shown.map(\.effectId)
        guard list.indices.contains(index) else { return }
        let full = model.engine.effects(forLayer: layer).compactMap { ($0["effectId"] as? NSNumber)?.uint32Value }
        let destination = full.firstIndex(of: list[index]) ?? index
        model.mutate { $0.moveEffect(effect.effectId, inLayer: layer, to: UInt32(destination)) }; model.refreshSelectedLayer()
    }
    private func remove(_ effect: EffectItem) {
        guard let layer = model.primarySelection, model.selectedLayer?.locked != true else { return }
        if openId == effect.effectId || overlayId == effect.effectId { closeNow() }
        model.mutate { $0.removeEffect(effect.effectId, fromLayer: layer) }
        model.refreshSelectedLayer()
    }
    /// RESTAURAR PADRÃO: todos os parâmetros ao padrão no cabeçote, um passo de desfazer.
    private func reset(_ effect: EffectItem) {
        guard let layer = model.primarySelection else { return }
        model.loadParams(layerId: layer, effectId: effect.effectId)
        let params = model.effectParams
        model.beginGesture("redefinir efeito")
        for param in params where param.flags & 16 == 0 && fxComponentCount(Int(param.type)) > 0 {
            writeVector(param.index, effect: effect.effectId, values: param.defaultValue)
        }
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
    /// Escolheu no navegador: adiciona NO FIM da pilha das camadas escolhidas. A
    /// lista decide se abre (só o 1º efeito da camada abre sozinho).
    private func pick(_ entry: EffectCatalogItem) {
        let targets = model.layers.filter { model.selection.contains($0.id) && !$0.locked }
        guard model.started, !targets.isEmpty, pendingPick == nil else { return }
        if let layer = model.primarySelection { model.seenEffectIds[layer] = Set(shown.map(\.effectId)) }
        pendingPick = entry.typeId
        prefs.addRecent(entry.typeId)
        model.addCatalogEffect(entry.typeId, layers: targets.map(\.id))
        model.refreshModel(force: true)
    }
}

/// O fundo do "+ Adicionar efeito": `fxLowSurface`, pressionado `fxListSurface`.
private struct FxAddRowStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background(configuration.isPressed ? AureaColors.fxListSurface : AureaColors.fxLowSurface,
                        in: RoundedRectangle(cornerRadius: 5))
    }
}

/// Uma caixa de aviso dentro do corpo (efeito sem ajustes, IA local…).
private struct FxBodyNotice: View {
    let text: String
    var body: some View {
        Text(text)
            .font(.aurea(size: 12))
            .foregroundStyle(AureaColors.fxTipText)
            .fixedSize(horizontal: false, vertical: true)
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(8)
    }
}

/// Cor do eixo (X `fxAxisX`, Y texto, Z `fxAxisZ`).
func fxAxisColor(_ component: Int) -> Color {
    component == 0 ? AureaColors.fxAxisX : component == 2 ? AureaColors.fxAxisZ : AureaColors.text
}

/// Caixa de UM eixo: fundo `fxLowSurface` (ativa/pressionada `fxSunken`), raio 4,
/// número na cor do eixo (8–13); `letter` (XYZ / α β γ) embaixo, 10.
private struct FxAxisBox: View {
    let text: String
    let color: Color
    let active: Bool
    let enabled: Bool
    let letter: String?
    let accessibilityLabel: String
    let accessibilityValue: String
    let resetLabel: String
    let identifier: String
    let onTap: () -> Void
    let onLongPress: () -> Void
    @State private var pressed = false

    var body: some View {
        ZStack(alignment: .bottom) {
            Text(text)
                .font(.aurea(size: 13).monospacedDigit())
                .minimumScaleFactor(8.0 / 13.0)
                .lineLimit(1)
                .foregroundStyle(enabled ? color : AureaColors.fxDisabledText)
                .padding(.bottom, letter != nil ? 8 : 0)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            if let letter {
                Text(letter).font(.aurea(size: 10)).foregroundStyle(AureaColors.fxAxisLabel).padding(.bottom, 1)
            }
        }
        .padding(.horizontal, 3)
        .background((active || pressed) && enabled ? AureaColors.fxSunken : AureaColors.fxLowSurface,
                    in: RoundedRectangle(cornerRadius: 4))
        .contentShape(Rectangle())
        .onTapGesture { if enabled { onTap() } }
        .onLongPressGesture(minimumDuration: 0.5, maximumDistance: 10, perform: {
            if enabled { onLongPress() }
        }, onPressingChanged: { down in pressed = down && enabled })
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(accessibilityLabel)
        .accessibilityValue(active ? accessibilityValue + " · " + AureaText.t("om_fx_axis_hint") : accessibilityValue)
        .accessibilityAddTraits(.isButton)
        .accessibilityAddTraits(active ? .isSelected : [])
        .accessibilityAction { if enabled { onTap() } }
        .accessibilityAction(named: resetLabel) { if enabled { onLongPress() } }
        .accessibilityIdentifier(identifier)
    }
}

/// O nome do preset para salvar: "Criar efeito…" (todos) ou "Salvar como preset
/// do efeito" (só um). Até 60 letras; vazio não salva. Ids `effects.save`,
/// `effects.save.confirm` e `dialog.field` (iguais no Android).
private struct FxSaveEffectDialog: View {
    let title: String
    /// Salva com o nome; verdadeiro = pode fechar.
    let onSave: (String) -> Bool
    let onDismiss: () -> Void
    @State private var name = ""
    @FocusState private var focused: Bool
    private var trimmed: String { name.trimmingCharacters(in: .whitespacesAndNewlines) }

    var body: some View {
        AureaDialog(title: title, actions: [
            DialogAction(AureaText.t("fx_custom_cancel"), role: .negative),
            DialogAction(AureaText.t("fx_custom_save"), role: .positive, enabled: !trimmed.isEmpty, dismiss: false,
                         tag: "effects.save.confirm") { save() },
        ], onDismiss: onDismiss, tag: "effects.save", content: AnyView(field))
        .onAppear { focused = true }
    }

    private var field: some View {
        AureaDialogField(text: $name, placeholder: AureaText.t("fx_custom_name"), tag: "dialog.field", focus: $focused) { save() }
            .onChange(of: name) { value in if value.count > 60 { name = String(value.prefix(60)) } }
    }

    private func save() {
        guard !trimmed.isEmpty else { return }
        if onSave(trimmed) { onDismiss() }
    }
}

/// Quantos dedos tocam a tela agora (o "dois dedos = 1/10" das réguas). Um
/// reconhecedor PASSIVO na janela: nunca reconhece, não cancela nem atrasa toque
/// nenhum e reconhece junto com todos os outros.
@MainActor final class FxTouchCounter: NSObject, UIGestureRecognizerDelegate {
    static let shared = FxTouchCounter()
    private(set) var count = 0
    private weak var window: UIWindow?

    func install() {
        guard window == nil else { return }
        let scenes = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }
        guard let target = scenes.flatMap({ $0.windows }).first(where: { $0.isKeyWindow }) ?? scenes.first?.windows.first else { return }
        let recognizer = FxTouchCountRecognizer(target: nil, action: nil)
        recognizer.onCount = { [weak self] value in self?.count = value }
        recognizer.cancelsTouchesInView = false
        recognizer.delaysTouchesBegan = false
        recognizer.delaysTouchesEnded = false
        recognizer.delegate = self
        target.addGestureRecognizer(recognizer)
        window = target
    }

    func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer,
                           shouldRecognizeSimultaneouslyWith otherGestureRecognizer: UIGestureRecognizer) -> Bool { true }
}

private final class FxTouchCountRecognizer: UIGestureRecognizer {
    var onCount: ((Int) -> Void)?
    private var down = Set<UITouch>()

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent) {
        down.formUnion(touches)
        onCount?(down.count)
    }
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent) { lift(touches) }
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent) { lift(touches) }
    private func lift(_ touches: Set<UITouch>) {
        down.subtract(touches)
        onCount?(down.count)
        if down.isEmpty { state = .failed }
    }
    override func reset() {
        down.removeAll()
        onCount?(0)
    }
}

/// "Trocar textura 3D": a imagem da galeria que cobre a parte (par do Android,
/// EffectsPanel.kt `PartTextureImageRow`).
@MainActor
private struct PartTextureImageRow: View {
    let layer: Int64
    let effect: UInt32
    @EnvironmentObject private var model: AureaModel
    @State private var picking = false
    @State private var filename = ""
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(AureaText.t("fxl_image") + " · " + (filename.isEmpty ? AureaText.t("fx3d_image_none") : filename))
                .font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).lineLimit(1)
            Button(AureaText.t(filename.isEmpty ? "fx3d_image_pick" : "shape3d_image_change")) { picking = true }
                .frame(maxWidth: .infinity, minHeight: 44)
                .accessibilityIdentifier("effect.part_texture.pick")
        }
        .padding(.horizontal, FxParamMath.LABEL_MARGIN).padding(.vertical, 4)
        .onAppear { filename = model.engine.effectImageName(effect, forLayer: layer) }
        .onChange(of: model.effects) { _ in filename = model.engine.effectImageName(effect, forLayer: layer) }
        .onChange(of: model.status.modelRevision) { _ in filename = model.engine.effectImageName(effect, forLayer: layer) }
        .sheet(isPresented: $picking) {
            ShellMediaPicker(selectionLimit: 1, filter: .images) { items in
                picking = false
                if let (url, _) = items.first { model.setEffectImage(layer, effect: effect, url: url) }
            }
        }
    }
}

@MainActor
/// Interno (0.3.0): a aba "Mais" do Ajustar (modo de edição) usa o mesmo importador.
struct CubeLutImportRow: View {
    let layer: Int64
    let effect: UInt32
    @EnvironmentObject private var model: AureaModel
    /// O .cube escolhido (DocumentImportPicker: o `.fileImporter` aninhado não abria).
    @State private var lutPick: URL?
    @State private var busy = false
    @State private var filename = ""
    private var palette: Bool { model.effects.first { $0.effectId == effect }?.typeId == fxEffectTypeId("aurea.color.palettize") }
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            if !filename.isEmpty {
                Text(filename).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).lineLimit(1)
            }
            Button(AureaText.t(palette ? "palette_import" : busy ? "lut_importing" : "lut_import")) {
                // O mesmo importador serve o LUT (.cube) e a paleta do Palettize.
                let types: [UTType] = palette
                    ? [.png, .jpeg, UTType(filenameExtension: "gpl") ?? .data, UTType(filenameExtension: "hex") ?? .data, .data]
                    : [UTType(filenameExtension: "cube") ?? .data, .data]
                DocumentImportPicker.present(types: types) { urls in lutPick = urls?.first }
            }
                .frame(maxWidth: .infinity, minHeight: 44).disabled(busy)
                .accessibilityIdentifier("effect.lut.import")
        }
        .onAppear { filename = model.engine.colorLutName(layer, effect: effect) }
        .onChange(of: model.effects) { _ in filename = model.engine.colorLutName(layer, effect: effect) }
        .onChange(of: lutPick) { picked in
            guard let url = picked else { return }
            lutPick = nil
            let isPalette = ["png", "jpg", "jpeg", "gpl", "hex"].contains(url.pathExtension.lowercased())
            guard isPalette || url.pathExtension.lowercased() == "cube" else { model.toast = AureaText.t(palette ? "palette_invalid" : "lut_invalid"); return }
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
                    model.toast = AureaText.t(isPalette ? (imported ? "palette_imported" : "palette_invalid") : (imported ? "lut_imported" : "lut_invalid"))
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

/// Índice de camada (parte baixa do id empacotado) — o que a "Camada de áudio" guarda.
func fxLayerIndex(_ id: Int64) -> Int { Int(truncatingIfNeeded: id & 0xFFFF_FFFF) }

/// 0.3.1 (plugins 3D): a referência que só aceita câmera / nulo / luz
/// (kParamRefCamera/Null/Light, bits 8..10 das flags). Nenhum bit = qualquer
/// camada; o motor recusa o resto do mesmo jeito (Android: acceptsLayer).
func fxAcceptsLayer(_ flags: UInt32, kind: UInt32) -> Bool {
    let accept = (flags >> 8) & 0x7
    if accept == 0 { return true }
    return (accept & 1 != 0 && kind == 8) || (accept & 2 != 0 && kind == 6) || (accept & 4 != 0 && kind == 9)
}

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

/// O nome de um preset do efeito (o do app antigo: Impacto/Na mão/Glitch do
/// Tremor…), no idioma do app; `fallback` = o nome que o motor publica.
func fxPresetLabel(_ pid: String, _ fallback: String) -> String {
    switch pid {
    case "impact": return AureaText.t("fxp_impact")
    case "handheld": return AureaText.t("fxp_handheld")
    case "glitch": return AureaText.t("fxp_glitch")
    case "saber_default", "saber_lightsaber", "saber_neon", "saber_electric", "saber_fire",
         "saber_plasma", "saber_energy", "saber_soft", "saber_magic":
        return AureaText.t("fxp_" + pid)
    case "classic", "dust", "living_grid": return AureaText.t("fxp_constellation_" + pid)
    case "sphere": return AureaText.t("fx030c_src_sphere")
    case "auto", "social", "restore", "anime", "upscaled", "film": return AureaText.t("fxp_enhance_" + pid)
    default: return fallback
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


@MainActor
struct ColorCurveEditor: View {
    let layer: Int64
    let effect: UInt32
    let param: UInt32
    /// Nomes dos 4 canais (curvas de matiz da Correção de cor, 0.3.0); nil = RGB/R/G/B.
    var channelNames: [String]? = nil
    @EnvironmentObject private var model: AureaModel
    @State private var channel: UInt32 = 0
    @State private var selected = 0
    @State private var points: [Float] = []
    @State private var samples: [Float] = []
    @State private var gesturing = false
    private var labels: [String] { channelNames ?? ["RGB", AureaText.t("curve_color_red"), AureaText.t("curve_color_green"), AureaText.t("curve_color_blue")] }
    private var tint: Color {
        channelNames != nil ? AureaColors.text
            : [AureaColors.text, Color(red: 1, green: 0.45, blue: 0.45), Color(red: 0.41, green: 0.84, blue: 0.61), Color(red: 0.45, green: 0.72, blue: 1)][Int(channel)]
    }
    private func reload() {
        points = model.engine.effectCurve(layer, effect: effect, param: param, channel: channel, samples: false).map { $0.floatValue }
        samples = model.engine.effectCurve(layer, effect: effect, param: param, channel: channel, samples: true).map { $0.floatValue }
        selected = min(max(0, selected), max(0, points.count / 2 - 1))
    }
    private func begin() { if !gesturing { gesturing = true; model.beginGesture("curva de cor") } }
    private func finish() { if gesturing { gesturing = false; model.endGesture(); reload() } }
    private func edit(_ action: UInt32, x: Float = 0, y: Float = 0) {
        let result = model.engine.editEffectCurve(layer, effect: effect, param: param, channel: channel, action: action, point: UInt32(selected), x: x, y: y)
        if result >= 0 { selected = Int(result) }; reload()
    }
    private func axis(_ index: Int) -> some View {
        let label = AureaText.t(index == 0 ? "curve_color_input" : "curve_color_output")
        return VStack(alignment: .leading, spacing: 0) {
            Text(label + " \(Int((points[selected * 2 + index] * 255).rounded()))").font(.aurea(size: 13)).foregroundStyle(AureaColors.text)
            Slider(value: Binding(get: { Double(points[selected * 2 + index]) }, set: { value in
                begin(); edit(0, x: index == 0 ? Float(value) : points[selected * 2], y: index == 1 ? Float(value) : points[selected * 2 + 1])
            }), in: 0...1, onEditingChanged: { active in if active { begin() } else { finish() } })
                .disabled(index == 0 && (selected == 0 || selected + 1 == points.count / 2))
                .accessibilityLabel(label).accessibilityIdentifier("fx.curve.axis.\(index)")
        }
    }
    private var graph: some View {
        GeometryReader { geo in
            let pad: CGFloat = 18
            let w = max(1, geo.size.width - 2 * pad); let h = max(1, geo.size.height - 2 * pad)
            let at: (Float, Float) -> CGPoint = { x, y in CGPoint(x: pad + CGFloat(x) * w, y: pad + CGFloat(1 - y) * h) }
            Canvas { context, _ in
                var grid = Path()
                for i in 0...4 { let v = Float(i) / 4; grid.move(to: at(v, 0)); grid.addLine(to: at(v, 1)); grid.move(to: at(0, v)); grid.addLine(to: at(1, v)) }
                context.stroke(grid, with: .color(AureaColors.muted.opacity(0.18)), lineWidth: 1)
                var diagonal = Path(); diagonal.move(to: at(0, 0)); diagonal.addLine(to: at(1, 1))
                context.stroke(diagonal, with: .color(AureaColors.muted.opacity(0.35)), lineWidth: 1)
                var path = Path()
                for i in samples.indices { let p = at(Float(i) / Float(max(1, samples.count - 1)), samples[i]); if i == 0 { path.move(to: p) } else { path.addLine(to: p) } }
                context.stroke(path, with: .color(tint), lineWidth: 2)
                for i in 0..<points.count / 2 {
                    let p = at(points[i * 2], points[i * 2 + 1]); let r: CGFloat = i == selected ? 7 : 5
                    context.fill(Path(ellipseIn: CGRect(x: p.x - r, y: p.y - r, width: r * 2, height: r * 2)), with: .color(i == selected ? AureaColors.accent : tint))
                    context.fill(Path(ellipseIn: CGRect(x: p.x - 2, y: p.y - 2, width: 4, height: 4)), with: .color(AureaColors.chip))
                }
            }.background(AureaColors.chip).contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0).onChanged { value in
                    let x = Float((value.location.x - pad) / w); let y = Float(1 - (value.location.y - pad) / h)
                    if !gesturing {
                        begin()
                        let hit = (0..<points.count / 2).min { a, b in
                            let pa = at(points[a * 2], points[a * 2 + 1]); let pb = at(points[b * 2], points[b * 2 + 1])
                            return hypot(pa.x - value.location.x, pa.y - value.location.y) < hypot(pb.x - value.location.x, pb.y - value.location.y)
                        }
                        if let hit, hypot(at(points[hit * 2], points[hit * 2 + 1]).x - value.location.x, at(points[hit * 2], points[hit * 2 + 1]).y - value.location.y) <= 24 { selected = hit }
                        else { edit(1, x: x, y: y) }
                    } else { edit(0, x: x, y: y) }
                }.onEnded { _ in finish() })
                .accessibilityLabel(AureaText.t("curve_color_graph")).accessibilityHint(AureaText.t("curve_color_hint"))
                .accessibilityIdentifier("fx.curve.graph")
        }.frame(height: 190)
    }
    var body: some View {
        VStack(spacing: 8) {
            HStack(spacing: 4) {
                ForEach(0..<4, id: \.self) { i in
                    Button { finish(); channel = UInt32(i); selected = 0; reload() } label: {
                        Text(channelNames?[i] ?? ["RGB", "R", "G", "B"][i]).font(.aurea(size: channelNames == nil ? 13 : 10))
                            .lineLimit(2).multilineTextAlignment(.center).frame(maxWidth: .infinity, minHeight: 44)
                            .background(channel == UInt32(i) ? AureaColors.accent.opacity(0.18) : AureaColors.chip).clipShape(RoundedRectangle(cornerRadius: 8))
                    }.buttonStyle(.plain).foregroundStyle(channel == UInt32(i) ? AureaColors.accent : AureaColors.text)
                        .accessibilityLabel(labels[i]).accessibilityAddTraits(channel == UInt32(i) ? .isSelected : [])
                        .accessibilityIdentifier("fx.curve.channel.\(i)")
                }
            }
            graph
            Text(AureaText.t("curve_color_hint")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).frame(maxWidth: .infinity, alignment: .leading)
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 4) {
                    ForEach(0..<points.count / 2, id: \.self) { i in
                        Button { finish(); selected = i } label: {
                            Text("\(i + 1)").font(.aurea(size: 13)).frame(minWidth: 44, minHeight: 44)
                                .background(selected == i ? AureaColors.accent.opacity(0.18) : AureaColors.chip).clipShape(RoundedRectangle(cornerRadius: 8))
                        }.buttonStyle(.plain).foregroundStyle(AureaColors.text)
                            .accessibilityLabel(AureaText.t("curve_color_point") + " \(i + 1)")
                            .accessibilityAddTraits(selected == i ? .isSelected : []).accessibilityIdentifier("fx.curve.point.\(i)")
                    }
                }
            }
            if points.count >= 4 { axis(0); axis(1) }
            HStack {
                Button(AureaText.t("panel_redefinir")) { finish(); edit(3); model.refreshModel(force: true) }.frame(minHeight: 44).accessibilityIdentifier("fx.curve.reset")
                Spacer()
                Button(AureaText.t("common_delete")) { finish(); edit(2); model.refreshModel(force: true) }.frame(minHeight: 44)
                    .disabled(selected == 0 || selected + 1 >= points.count / 2).accessibilityIdentifier("fx.curve.delete")
            }.font(.aurea(size: 13)).tint(AureaColors.accent)
        }.onAppear { reload() }.onChange(of: model.status.modelRevision) { _ in if !gesturing { reload() } }.onDisappear { finish() }
    }
}

/// Cabeçalho de SEÇÃO da referência (`effect_setting_static_section`, 43): faixa
/// `fxLowSurface` de 40 colada embaixo (3 de folga em cima), seta de 20 no texto a
/// 30 % que gira 0→90° em 300 ms acelera-desacelera, título 14 regular (margem 13)
/// e o interruptor opcional (margem 20). A contagem "N ajustes" quando recolhida é
/// extra do AUREA. Tocar na faixa abre/recolhe; o interruptor escreve o Bool.
/// Ids `effects.section.<efeito>.<ordinal>` / `effects.section.toggle.<efeito>.<ordinal>`
/// (o "Avançado" usa `effects.section.<efeito>.advanced`), iguais no Android.
struct FxSectionHeader: View {
    let title: String
    let open: Bool
    let count: Int
    let identifier: String
    var toggleIdentifier: String? = nil
    var toggleLabel = ""
    /// nil = sem interruptor.
    var toggleOn: Bool? = nil
    var toggleEnabled = true
    let onOpen: (Bool) -> Void
    var onToggle: (Bool) -> Void = { _ in }
    @Environment(\.layoutDirection) private var layoutDirection

    var body: some View {
        let countText = count == 1 ? AureaText.t("ds_1_ajuste") : AureaText.t("ds_adjustment_count", count)
        let angle: Double = open ? (layoutDirection == .rightToLeft ? -90 : 90) : 0
        return VStack(spacing: 0) {
            Spacer().frame(height: FxParamMath.ROW_SECTION - 40)
            HStack(spacing: 0) {
                HStack(spacing: 0) {
                    CupertinoGlyph.text(CupertinoGlyph.ChevronRight, size: 20, color: AureaColors.text.opacity(0.3))
                        .mirrorsInRtl()
                        .rotationEffect(.degrees(angle))
                        .animation(.timingCurve(0.42, 0, 0.58, 1, duration: Double(FxParamMath.SECTION_ARROW_MS) / 1000), value: open)
                    Text(title).font(.aurea(size: 14)).foregroundStyle(AureaColors.text)
                        .lineLimit(1).truncationMode(.tail)
                        .padding(.leading, 13)
                    if !open {
                        Text(countText).font(.aurea(size: 12)).foregroundStyle(AureaColors.fxTipText).lineLimit(1)
                            .padding(.leading, 8)
                    }
                    Spacer(minLength: 0)
                }
                .padding(.leading, 6).padding(.trailing, 10)
                .frame(maxWidth: .infinity).frame(height: 40)
                .contentShape(Rectangle())
                .onTapGesture { onOpen(!open) }
                .accessibilityElement(children: .ignore)
                .accessibilityLabel(AureaText.t(open ? "fxui_a11y_section_collapse" : "fxui_a11y_section_expand", title))
                .accessibilityValue(open ? "" : countText)
                .accessibilityAddTraits(.isButton)
                .accessibilityAction { onOpen(!open) }
                .accessibilityIdentifier(identifier)
                if let toggleOn {
                    AureaToggle(checked: toggleOn, enabled: toggleEnabled) { onToggle($0) }
                        .accessibilityLabel(toggleLabel)
                        .accessibilityValue(AureaText.t(toggleOn ? "common_on" : "common_off"))
                        .modifier(OptionalIdentifier(tag: toggleIdentifier))
                    Spacer().frame(width: 20)
                }
            }
            .frame(height: 40)
            .background(AureaColors.fxLowSurface)
        }
        .frame(height: FxParamMath.ROW_SECTION)
    }
}

/// DICA só de leitura: ⓘ 14 + texto 12 em `fxTipText`, margens 8, acima da linha.
/// Id `effects.tip.<efeito>.<índice>`.
struct FxTipRow: View {
    let effectId: UInt32
    let index: UInt32
    let text: String

    var body: some View {
        HStack(alignment: .top, spacing: 8) {
            CupertinoGlyph.text(CupertinoGlyph.InfoCircle, size: 14, color: AureaColors.fxTipText).padding(.top, 1)
            Text(text).font(.aurea(size: 12)).foregroundStyle(AureaColors.fxTipText)
                .fixedSize(horizontal: false, vertical: true)
            Spacer(minLength: 0)
        }
        .padding(.horizontal, 8).padding(.top, 6).padding(.bottom, 2)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(AureaText.t("fxui_a11y_tip", text))
        .accessibilityIdentifier("effects.tip.\(effectId).\(index)")
    }
}
