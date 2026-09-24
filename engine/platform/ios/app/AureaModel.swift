// =============================================================================
//  Aurea / platform / ios / app / AureaModel.swift
//
//  A sessão do app: é ela que é dona da ponte (`AureaEngine`) e do estado que as
//  telas leem. É o equivalente do `EditorStore` do Android.
//
//  O QUE ESTE ARQUIVO NÃO FAZ: não edita o projeto. Toda alteração vira um
//  comando do motor (`aurea::Command`); nenhuma regra de timeline, efeito ou
//  keyframe é reimplementada aqui. Se uma regra aparecesse neste arquivo, o
//  mesmo projeto abriria diferente no Android e no iOS.
//
//  QUEM DIRIGE O PREVIEW: a thread de render do motor, com o CADisplayLink da
//  view só dando o pulso (`requestRender`). O SwiftUI NÃO desenha o preview e
//  não recebe bitmap nenhum.
//
//  RITMO DA UI: um tique de 5 Hz lê o `AureaStatus` e publica só o que mudou —
//  ler status a 60 Hz e publicar 30 propriedades redesenharia a tela inteira à
//  toa (e arrastaria o usuário para uma UI que engasga).
// =============================================================================
import Foundation
import Metal
import UIKit

// =============================================================================
// Tipos da UI, derivados do que a ponte devolve.
// =============================================================================
struct LayerItem: Identifiable, Equatable {
    var id: Int64
    var kind: UInt32
    var name: String
    var startFrame: Int32
    var endFrame: Int32
    var opacity: Float
    var effectCount: UInt32
    var maskCount: UInt32
    var keyframeCount: UInt32
    var blendMode: UInt32
    var selected: Bool
    var visible: Bool
    var locked: Bool
    var solo: Bool
    var animated: Bool
    var threeD: Bool
    var label: UInt32
    var adjustment: Bool
    var guide: Bool

    var duration: Int32 { max(0, endFrame - startFrame) }
}

struct KeyframeItem: Identifiable, Equatable {
    var property: UInt32
    var effectIndex: UInt32
    var time: Int32
    var value: Float
    var interpolation: UInt32
    var id: String { "\(property)-\(effectIndex)-\(time)" }
}

struct EffectItem: Identifiable, Equatable {
    var effectId: UInt32
    var typeId: UInt32
    var name: String
    var enabled: Bool
    var paramCount: UInt32
    var known: Bool
    var id: UInt32 { effectId }
}

struct EffectParamItem: Identifiable, Equatable {
    var index: UInt32
    var type: UInt32
    var label: String
    var unit: String
    var paramId: String
    var value: [Float]
    var defaultValue: [Float]
    var minValue: Float
    var maxValue: Float
    var animated: Bool
    var enumLabels: [String]
    var id: UInt32 { index }

    var scalar: Float { value.first ?? 0 }
}

struct EffectCatalogItem: Identifiable, Equatable {
    var typeId: UInt32
    var name: String
    var category: String
    var paramCount: UInt32
    var id: UInt32 { typeId }
}

struct ProjectFile: Identifiable, Equatable {
    var url: URL
    var name: String
    var modified: Date
    var sizeBytes: Int64
    var id: String { url.path }

    var thumbnailURL: URL {
        AureaPaths.thumbs.appendingPathComponent(url.deletingPathExtension().lastPathComponent + ".jpg")
    }
}

struct ExportOptions: Equatable {
    var codec: AureaExportCodec = .h264
    var shortSide: UInt32 = 1080
    var bitrateMbps: UInt32 = 20
    var audioBitrateKbps: UInt32 = 192
    var fps: Double = 0   // 0 = o da composição
}

/// Pastas do app. O `.aurea` e a mídia importada moram em Documents; o cache de
/// pipeline e as miniaturas no caches/documents do app.
enum AureaPaths {
    static var documents: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
    }
    static var caches: URL {
        FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0]
    }
    static var media: URL { documents.appendingPathComponent("Media", isDirectory: true) }
    static var thumbs: URL { documents.appendingPathComponent("Thumbs", isDirectory: true) }

    static func ensureDirectories() {
        for url in [media, thumbs] {
            try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        }
    }

    /// Um caminho livre dentro de Media, preservando a extensão (o decoder
    /// escolhe o demuxer por ela).
    static func mediaDestination(for name: String) -> URL {
        let ext = (name as NSString).pathExtension
        let base = (name as NSString).deletingPathExtension
        var url = media.appendingPathComponent(ext.isEmpty ? base : "\(base).\(ext)")
        var counter = 1
        while FileManager.default.fileExists(atPath: url.path) {
            url = media.appendingPathComponent(ext.isEmpty ? "\(base)-\(counter)" : "\(base)-\(counter).\(ext)")
            counter += 1
        }
        return url
    }
}

// =============================================================================
// A sessão
// =============================================================================
@MainActor
final class AureaModel: ObservableObject {

    // --- Ponte e estado do motor -------------------------------------------
    let engine: AureaEngine
    /// O dispositivo Metal. É o MESMO que a view do preview usa: um layer de
    /// outro dispositivo recusa drawables.
    let device: MTLDevice?
    @Published private(set) var status = AureaStatus()
    @Published private(set) var started = false
    @Published private(set) var startError: String?
    @Published private(set) var deviceSummary = ""
    @Published private(set) var deviceReport: [String: NSNumber] = [:]
    @Published private(set) var perf: [String: Any] = [:]

    // --- Navegação ----------------------------------------------------------
    @Published var screen: Screen = .home
    @Published var fullscreen = false
    @Published var panel: PanelKind = .none
    @Published var showAddLayer = false
    @Published var showExport = false
    @Published var showSettings = false
    /// A folha de geração atual (a doca de painéis, o painel aberto ou o
    /// "adicionar camada").
    ///
    /// A doca fica SEMPRE à mão (é de onde saem os painéis): `.none` abre a
    /// doca, e não uma tela sem folha — foi o que a casca A.01 fixou, e cada
    /// ladrilho aberto é que decide a fração da folha.
    var sheetContent: SheetContent {
        if showAddLayer { return .adding }
        switch panel {
        case .none, .dock: return .dock
        default: return .panel
        }
    }

    // --- Projeto ------------------------------------------------------------
    @Published private(set) var projectURL: URL?
    @Published var projectName: String = ""
    @Published private(set) var dirty = false
    @Published private(set) var projects: [ProjectFile] = []
    @Published var searchQuery = ""
    @Published var sort: ProjectSort = .recent
    @Published private(set) var openingProject = false
    @Published var toast: String?

    // --- Modelo em memória (o que a timeline e os painéis desenham) ---------
    @Published private(set) var layers: [LayerItem] = []
    @Published private(set) var keyframes: [Int64: [KeyframeItem]] = [:]
    @Published private(set) var selection: Set<Int64> = []
    @Published private(set) var composition: [String: Any] = [:]
    @Published private(set) var effectCatalog: [EffectCatalogItem] = []
    @Published private(set) var effects: [EffectItem] = []
    @Published private(set) var effectParams: [EffectParamItem] = []
    /// Detalhe da camada escolhida (transform avaliado no playhead).
    @Published private(set) var detail: [String: Any] = [:]

    // --- Export -------------------------------------------------------------
    @Published var exportOptions = ExportOptions()
    @Published private(set) var exportProgress: [String: Any] = [:]
    @Published private(set) var exporting = false

    // --- Ajustes ------------------------------------------------------------
    @Published var language: AureaLanguage = .systemDefault {
        didSet { AureaText.language = language; UserDefaults.standard.set(language.rawValue, forKey: "aurea.language") }
    }
    @Published var showPerf = false

    enum Screen { case home, editor }
    enum PanelKind { case none, dock, transform, effects, layer3D, exportPanel }
    enum ProjectSort: String, CaseIterable, Identifiable {
        case recent, name, longest, size
        var id: String { rawValue }
        var label: String {
            switch self {
            case .recent: return AureaText.t("home_sort_recent")
            case .name: return AureaText.t("home_sort_name")
            case .longest: return AureaText.t("home_sort_longest")
            case .size: return AureaText.t("home_sort_size")
            }
        }
    }

    private var statusTimer: Timer?
    private var memoryWarningObserver: NSObjectProtocol?
    private var lastRevision: UInt32 = 0
    private var lastThumbGeneration: UInt32 = 0
    private var lastLayerSignature: String = ""

    // =========================================================================
    // Ciclo de vida
    // =========================================================================
    init() {
        AureaPaths.ensureDirectories()
        if let stored = UserDefaults.standard.string(forKey: "aurea.language"),
           let parsed = AureaLanguage(rawValue: stored) {
            language = parsed
            AureaText.language = parsed
        }
        engine = AureaEngine(cacheDirectory: AureaPaths.caches.path,
                             documentsDirectory: AureaPaths.documents.path)
        device = MTLCreateSystemDefaultDevice()
        refreshProjectList()
    }

    /// Sobe o motor. Chamado quando a janela aparece (e de novo ao voltar do
    /// segundo plano, se por algum motivo ele não estiver de pé).
    func start() {
        guard !started else { return }
        var error: NSString?
        let refresh = Double(UIScreen.main.maximumFramesPerSecond > 0 ? UIScreen.main.maximumFramesPerSecond : 60)
        if engine.start(with: device, refreshRate: refresh, debug: false, error: &error) {
            started = true
            startError = nil
            deviceSummary = engine.deviceSummary
            deviceReport = engine.deviceReport()
            startStatusTimer()
            observeMemoryWarnings()
        } else {
            started = false
            startError = (error as String?) ?? "o motor não subiu"
        }
    }

    func enterBackground() {
        guard started else { return }
        engine.suspend()
    }

    func enterForeground() {
        guard started else { return }
        engine.resume()
        engine.invalidate()
        refreshModel(force: true)
    }

    /// Aviso de memória do sistema — o equivalente do `onTrimMemory` do
    /// Android (MainActivity). O aviso do iOS não traz nível, então o valor é o
    /// degrau da tabela do motor (`trim_stage_for_os_level`) que devolve
    /// memória SEM derrubar o que está na tela: 15 = RUNNING_CRITICAL, que solta
    /// os quadros decodificados sem uso e o cache de render antigo. O projeto, o
    /// histórico e a timeline nunca entram (é a garantia do `trim_memory`).
    private func observeMemoryWarnings() {
        guard memoryWarningObserver == nil else { return }
        memoryWarningObserver = NotificationCenter.default.addObserver(
            forName: UIApplication.didReceiveMemoryWarningNotification,
            object: nil, queue: .main) { [weak self] _ in
            Task { @MainActor in
                guard let self, self.started else { return }
                _ = self.engine.trimMemory(15)
            }
        }
    }

    func stop() {
        if let observer = memoryWarningObserver {
            NotificationCenter.default.removeObserver(observer)
            memoryWarningObserver = nil
        }
        statusTimer?.invalidate()
        statusTimer = nil
        engine.stop()
        started = false
    }

    // =========================================================================
    // Estado do motor
    // =========================================================================
    private func startStatusTimer() {
        statusTimer?.invalidate()
        let timer = Timer(timeInterval: 0.2, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.refreshStatus() }
        }
        RunLoop.main.add(timer, forMode: .common)
        statusTimer = timer
    }

    private func refreshStatus() {
        guard started else { return }
        var out = AureaStatus()
        guard engine.readStatus(&out) else { return }
        // Só publica quando algo que a UI MOSTRA mudou: publicar a 5 Hz sem
        // filtrar redesenha a árvore inteira por nada (a FPS do painel DEV
        // muda sempre, e é para isso que existe o `showPerf`).
        if !statusEquals(out) {
            status = out
        }
        if out.modelRevision != lastRevision {
            lastRevision = out.modelRevision
            refreshModel(force: true)
        }
        if out.thumbnailGeneration != lastThumbGeneration {
            lastThumbGeneration = out.thumbnailGeneration
        }
        if showPerf { perf = engine.perf() }
    }

    private func statusEquals(_ other: AureaStatus) -> Bool {
        let a = status
        return a.playhead == other.playhead
            && a.duration == other.duration
            && a.playing == other.playing
            && a.layerCount == other.layerCount
            && a.selectedCount == other.selectedCount
            && a.canUndo == other.canUndo
            && a.canRedo == other.canRedo
            && a.dirty == other.dirty
            && a.previewNumerator == other.previewNumerator
            && a.previewDenominator == other.previewDenominator
            && a.previewWidth == other.previewWidth
            && a.previewHeight == other.previewHeight
            && abs(a.currentFps - other.currentFps) < 0.5
            && a.state == other.state
    }

    /// Relê o modelo que as telas desenham. Barato o bastante para rodar a cada
    /// mudança de revisão (é UMA travessia por lista, ver Engine::query_*).
    func refreshModel(force: Bool = false) {
        guard started else { return }
        let rows = engine.layers()
        layers = rows.map { row in
            LayerItem(id: (row["id"] as? NSNumber)?.int64Value ?? 0,
                      kind: (row["kind"] as? NSNumber)?.uint32Value ?? 0,
                      name: row["name"] as? String ?? "",
                      startFrame: (row["startFrame"] as? NSNumber)?.int32Value ?? 0,
                      endFrame: (row["endFrame"] as? NSNumber)?.int32Value ?? 0,
                      opacity: (row["opacity"] as? NSNumber)?.floatValue ?? 1,
                      effectCount: (row["effectCount"] as? NSNumber)?.uint32Value ?? 0,
                      maskCount: (row["maskCount"] as? NSNumber)?.uint32Value ?? 0,
                      keyframeCount: (row["keyframeCount"] as? NSNumber)?.uint32Value ?? 0,
                      blendMode: (row["blendMode"] as? NSNumber)?.uint32Value ?? 0,
                      selected: row["selected"] as? Bool ?? false,
                      visible: row["visible"] as? Bool ?? true,
                      locked: row["locked"] as? Bool ?? false,
                      solo: row["solo"] as? Bool ?? false,
                      animated: row["animated"] as? Bool ?? false,
                      threeD: row["threeD"] as? Bool ?? false,
                      label: (row["label"] as? NSNumber)?.uint32Value ?? 0,
                      adjustment: ((row["flags"] as? NSNumber)?.uint32Value ?? 0) & (1 << 6) != 0,
                      guide: ((row["flags"] as? NSNumber)?.uint32Value ?? 0) & (1 << 7) != 0)
        }
        selection = Set(layers.filter(\.selected).map(\.id))

        // Keyframes: uma travessia para a composição inteira.
        var byLayer: [Int64: [KeyframeItem]] = [:]
        for entry in engine.allKeyframes() {
            guard let layerId = (entry["layerId"] as? NSNumber)?.int64Value,
                  let keys = entry["keys"] as? [[String: Any]] else { continue }
            byLayer[layerId] = keys.map { key in
                KeyframeItem(property: (key["property"] as? NSNumber)?.uint32Value ?? 0,
                             effectIndex: (key["effectIndex"] as? NSNumber)?.uint32Value ?? 0,
                             time: (key["time"] as? NSNumber)?.int32Value ?? 0,
                             value: (key["value"] as? NSNumber)?.floatValue ?? 0,
                             interpolation: (key["interpolation"] as? NSNumber)?.uint32Value ?? 0)
            }
        }
        keyframes = byLayer
        composition = engine.composition() ?? [:]
        dirty = status.dirty != 0
        effectCatalog = engine.effectCatalog().map { row in
            EffectCatalogItem(typeId: (row["typeId"] as? NSNumber)?.uint32Value ?? 0,
                              name: row["name"] as? String ?? "",
                              category: row["category"] as? String ?? "",
                              paramCount: (row["paramCount"] as? NSNumber)?.uint32Value ?? 0)
        }
        refreshSelectedLayer()
    }

    /// O que depende da camada escolhida: efeitos, parâmetros e o inspetor.
    func refreshSelectedLayer() {
        guard let layerId = primarySelection, started else {
            effects = []
            effectParams = []
            detail = [:]
            return
        }
        effects = engine.effects(forLayer: layerId).map { row in
            EffectItem(effectId: (row["effectId"] as? NSNumber)?.uint32Value ?? 0,
                       typeId: (row["typeId"] as? NSNumber)?.uint32Value ?? 0,
                       name: row["name"] as? String ?? "",
                       enabled: row["enabled"] as? Bool ?? true,
                       paramCount: (row["paramCount"] as? NSNumber)?.uint32Value ?? 0,
                       known: row["known"] as? Bool ?? true)
        }
        detail = engine.layerDetail(layerId) ?? [:]
        if let effectId = selectedEffectId {
            loadParams(layerId: layerId, effectId: effectId)
        }
    }

    func loadParams(layerId: Int64, effectId: UInt32) {
        selectedEffectId = effectId
        effectParams = engine.effectParams(forLayer: layerId, effectId: effectId).map { row in
            let value = (row["value"] as? [NSNumber])?.map(\.floatValue) ?? [0, 0, 0, 0]
            let def = (row["defaultValue"] as? [NSNumber])?.map(\.floatValue) ?? [0, 0, 0, 0]
            return EffectParamItem(index: (row["index"] as? NSNumber)?.uint32Value ?? 0,
                                   type: (row["type"] as? NSNumber)?.uint32Value ?? 0,
                                   label: row["label"] as? String ?? "",
                                   unit: row["unit"] as? String ?? "",
                                   paramId: row["id"] as? String ?? "",
                                   value: value,
                                   defaultValue: def,
                                   minValue: (row["min"] as? NSNumber)?.floatValue ?? 0,
                                   maxValue: (row["max"] as? NSNumber)?.floatValue ?? 1,
                                   animated: row["animated"] as? Bool ?? false,
                                   enumLabels: row["enumLabels"] as? [String] ?? [])
        }
    }

    @Published var selectedEffectId: UInt32?

    var primarySelection: Int64? { selection.first }

    /// A camada escolhida, para os painéis.
    var selectedLayer: LayerItem? {
        guard let id = primarySelection else { return nil }
        return layers.first { $0.id == id }
    }

    // =========================================================================
    // Comandos — a UI inteira passa por aqui
    // =========================================================================
    /// Envolve um bloco de comandos e manda o lote UMA vez. É o desenho do
    /// `CommandBatch` do Android: um arrasto que mexe em posição e opacidade
    /// custa UMA travessia de fronteira, não duas.
    func mutate(_ body: (AureaEngine) -> Void) {
        guard started else { return }
        engine.beginBatch()
        body(engine)
        _ = engine.flush()
    }

    func undo() { engine.run { $0.undo() }; syncAfterEdit() }
    func redo() { engine.run { $0.redo() }; syncAfterEdit() }

    func playPause() { engine.run { $0.togglePlayback() }; status.playing = status.playing == 0 ? 1 : 0 }
    func seek(toFrame frame: Int64) { engine.run { $0.seek(toFrame: frame) }; status.playhead = frame }
    func step(_ frames: Int32) { engine.run { $0.stepFrames(frames) } }
    func setLoop(_ on: Bool) { engine.run { $0.setLoop(on) } }
    func setSpeed(_ speed: Float) { engine.run { $0.setPlaybackSpeed(speed) } }
    func setPreviewScale(num: UInt32, den: UInt32, auto: Bool) {
        engine.run { $0.setPreviewScaleNumerator(num, denominator: den, automatic: auto) }
    }
    func toggleMarker() { engine.run { $0.toggleMarker(Int64(status.playhead)) } }
    /// O playhead enquanto o dedo arrasta a régua. É otimismo LOCAL e curto: o
    /// valor do MOTOR volta no próximo `fill_status` e o substitui. Sem isto, o
    /// playhead só andaria a cada 200 ms e o scrub pareceria travado.
    func optimisticPlayhead(_ frame: Int64) {
        guard status.playhead != frame else { return }
        status.playhead = frame
    }
    /// Redesenha e reapresenta mesmo sem mudança no modelo: a janela voltou a
    /// aparecer, ou o palco mudou de tamanho (tela cheia).
    func invalidatePreview() { engine.run { $0.invalidate() } }

    /// Fecha o lote agora. Os setters de parâmetro de efeito NÃO fecham
    /// sozinhos (um arrasto de slider emite dezenas de valores por segundo), e
    /// o motor aplica todos no mesmo bloco — uma travessia por dedo, não por
    /// pixel. Diferir este `flush` para o próximo ciclo seria pior: o
    /// `beginBatch` do próximo gesto limpa o que ainda não foi enviado.
    func commitPendingCommands() { _ = engine.flush() }

    /// Agrupa a seleção numa pré-composição (o "agrupar" do Android). Uma ação
    /// de desfazer, como qualquer outra edição.
    func groupSelection() {
        guard !selection.isEmpty else { return }
        let ids = selection.map { NSNumber(value: $0) }
        let created = engine.precomposeLayers(ids, name: nil)
        if created < 0 {
            toast = "nao deu para agrupar"
            return
        }
        selection = [created]
        engine.selectLayers([NSNumber(value: created)])
        refreshModel(force: true)
    }

    func ungroup(_ layerId: Int64) {
        let why = engine.ungroupPrecomp(layerId)
        if !why.isEmpty { toast = why }
        refreshModel(force: true)
    }

    /// Renomeia o projeto ABERTO (arquivo + capa), mantendo o editor nele.
    func renameCurrentProject(to newName: String) {
        guard let url = projectURL, url.deletingPathExtension().lastPathComponent != newName else { return }
        rename(ProjectFile(url: url, name: projectName, modified: Date(), sizeBytes: 0), to: newName)
        projectURL = AureaPaths.documents.appendingPathComponent(newName + ".aurea")
        projectName = newName
    }

    func select(layerId: Int64, additive: Bool) {
        if additive {
            var next = selection
            if next.contains(layerId) { next.remove(layerId) } else { next.insert(layerId) }
            engine.run { $0.selectLayers(next.map { NSNumber(value: $0) }) }
            selection = next
        } else {
            engine.run { $0.selectLayers([NSNumber(value: layerId)]) }
            selection = [layerId]
        }
        selectedEffectId = nil
        refreshSelectedLayer()
    }

    func clearSelection() {
        engine.run { $0.clearSelection() }
        selection = []
        selectedEffectId = nil
        refreshSelectedLayer()
    }

    /// Depois de uma edição que muda o modelo, relê. O motor publica
    /// `modelRevision` no status; aqui a releitura é imediata para o dedo não
    /// esperar o próximo tique.
    private func syncAfterEdit() { refreshStatus(); refreshModel() }

    // =========================================================================
    // Projetos
    // =========================================================================
    func refreshProjectList() {
        let fm = FileManager.default
        let urls = (try? fm.contentsOfDirectory(at: AureaPaths.documents,
                                                includingPropertiesForKeys: [.contentModificationDateKey, .fileSizeKey],
                                                options: [.skipsHiddenFiles])) ?? []
        var found: [ProjectFile] = []
        for url in urls where url.pathExtension.lowercased() == "aurea" {
            let values = try? url.resourceValues(forKeys: [.contentModificationDateKey, .fileSizeKey])
            found.append(ProjectFile(url: url,
                                     name: url.deletingPathExtension().lastPathComponent,
                                     modified: values?.contentModificationDate ?? .distantPast,
                                     sizeBytes: Int64(values?.fileSize ?? 0)))
        }
        switch sort {
        case .recent: found.sort { $0.modified > $1.modified }
        case .name: found.sort { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
        case .longest: found.sort { $0.modified > $1.modified }
        case .size: found.sort { $0.sizeBytes > $1.sizeBytes }
        }
        if !searchQuery.isEmpty {
            found = found.filter { $0.name.localizedCaseInsensitiveContains(searchQuery) }
        }
        projects = found
    }

    /// Cria um projeto e entra no editor. Mesma regra do Android: a resolução é
    /// o LADO MENOR (1080p em 9:16 = 1080×1920).
    func newProject(ratio: Double, shortSide: UInt32, fps: Double, title: String) {
        let width: UInt32 = ratio >= 1 ? UInt32((Double(shortSide) * ratio).rounded()) : shortSide
        let height: UInt32 = ratio >= 1 ? shortSide : UInt32((Double(shortSide) / ratio).rounded())
        let name = title.isEmpty ? "Aurea" : title
        guard engine.newProjectWidth(width, height: height, fps: fps, title: name) else {
            toast = "não foi possível criar o projeto"
            return
        }
        let url = uniqueProjectURL(name)
        projectURL = url
        projectName = name
        _ = engine.saveProject(url.path)
        refreshProjectList()
        enterEditor()
    }

    func open(_ project: ProjectFile) {
        openingProject = true
        defer { openingProject = false }
        guard engine.loadProject(project.path) else {
            toast = "não foi possível abrir o projeto"
            return
        }
        projectURL = project.url
        projectName = project.name
        let notice = engine.lastLoadNotice
        if notice != 0 {
            // A UI AVISA em vez de esconder (o mesmo critério do Android: §55,
            // §120, §124).
            var parts: [String] = []
            if notice & 1 != 0 { parts.append("aberto de uma cópia de recuperação") }
            if notice & 2 != 0 { parts.append("algumas seções faltando") }
            if notice & 4 != 0 { parts.append("formato antigo") }
            if notice & 8 != 0 { parts.append("\(engine.lastLoadMissingAssets) mídia(s) ausente(s)") }
            toast = parts.joined(separator: " · ")
        }
        refreshModel(force: true)
        enterEditor()
    }

    func enterEditor() {
        panel = .none
        screen = .editor
    }

    func closeProject() {
        if exporting { toast = AureaText.t("editor_mantenha_aurea_aberto_ate_terminar"); return }
        saveProject(writeThumbnail: true)
        engine.clearSelection()
        screen = .home
        fullscreen = false
        panel = .none
        refreshProjectList()
    }

    @discardableResult
    func saveProject(writeThumbnail: Bool) -> Bool {
        guard let url = projectURL else { return false }
        guard engine.saveProject(url.path) else {
            toast = "falha ao salvar"
            return false
        }
        if writeThumbnail { writeProjectThumbnail(name: url.deletingPathExtension().lastPathComponent) }
        dirty = false
        return true
    }

    /// A capa do projeto na Home. É o MESMO caminho do Android: o motor
    /// renderiza o quadro do cabeçote (`capture_frame_rgba`) e o arquivo vai
    /// para `Thumbs/<nome>.jpg`. Isto é uma MINIATURA — o preview continua indo
    /// direto para o CAMetalLayer, sem bitmap nenhum no meio.
    private func writeProjectThumbnail(name: String) {
        var width: UInt32 = 0
        var height: UInt32 = 0
        guard let data = engine.captureFrame(720, outWidth: &width, outHeight: &height),
              width > 0, height > 0,
              let image = UIImage.fromRGBA(data, width: Int(width), height: Int(height)),
              let jpeg = image.jpegData(compressionQuality: 0.82) else { return }
        let url = AureaPaths.thumbs.appendingPathComponent(name + ".jpg")
        try? jpeg.write(to: url, options: .atomic)
    }

    private func uniqueProjectURL(_ name: String) -> URL {
        var url = AureaPaths.documents.appendingPathComponent(name + ".aurea")
        var counter = 1
        while FileManager.default.fileExists(atPath: url.path) {
            url = AureaPaths.documents.appendingPathComponent("\(name) \(counter).aurea")
            counter += 1
        }
        return url
    }

    func delete(_ project: ProjectFile) {
        try? FileManager.default.removeItem(at: project.url)
        try? FileManager.default.removeItem(at: project.thumbnailURL)
        refreshProjectList()
    }

    func rename(_ project: ProjectFile, to newName: String) {
        let target = AureaPaths.documents.appendingPathComponent(newName + ".aurea")
        guard !FileManager.default.fileExists(atPath: target.path) else {
            toast = "já existe um projeto com esse nome"
            return
        }
        try? FileManager.default.moveItem(at: project.url, to: target)
        if let from = try? Data(contentsOf: project.thumbnailURL) {
            try? from.write(to: AureaPaths.thumbs.appendingPathComponent(newName + ".jpg"))
            try? FileManager.default.removeItem(at: project.thumbnailURL)
        }
        if projectURL?.path == project.url.path { projectURL = target; projectName = newName }
        refreshProjectList()
    }

    // =========================================================================
    // Importação
    // =========================================================================
    /// Copia o arquivo escolhido para o sandbox (Media/) e importa. O motor
    /// guarda o caminho RELATIVO a Documents — é o que faz o mesmo .aurea
    /// abrir no Android e no iOS.
    func importMedia(url: URL, kind: ImportKind) {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        let destination = AureaPaths.mediaDestination(for: url.lastPathComponent)
        do {
            if FileManager.default.fileExists(atPath: destination.path) {
                try FileManager.default.removeItem(at: destination)
            }
            try FileManager.default.copyItem(at: url, to: destination)
        } catch {
            toast = "não deu para copiar o arquivo"
            return
        }
        let name = url.deletingPathExtension().lastPathComponent
        var result: Int64 = -1
        switch kind {
        case .video: result = engine.importVideo(destination.path, name: name)
        case .audio: result = engine.importAudio(destination.path, name: name)
        case .image: result = engine.importImageFile(destination.path, name: name)
        case .model: result = engine.importModel(destination.path, name: name)
        case .hdri: result = engine.importHdri(destination.path)
        }
        if result < 0 {
            let why = engine.lastImportError
            // O motor devolve −Errc e o motivo em texto. Nunca um "importado"
            // com a tela preta (é a regra do import_model no Engine.hpp).
            toast = why.isEmpty ? "importação falhou (\(-result))" : why
            try? FileManager.default.removeItem(at: destination)
            return
        }
        if kind == .hdri {
            toast = "ambiente importado"
        } else {
            engine.selectLayers([NSNumber(value: result)])
            selection = [result]
        }
        syncAfterEdit()
    }

    enum ImportKind { case video, audio, image, model, hdri }

    func addShape(_ preset: UInt32) {
        let id = engine.addShape(preset)
        if id >= 0 { selection = [id]; engine.selectLayers([NSNumber(value: id)]) }
        showAddLayer = false
        syncAfterEdit()
    }

    func addText() {
        let id = engine.addText(AureaText.t("pn_text3d_placeholder"))
        if id >= 0 { selection = [id]; engine.selectLayers([NSNumber(value: id)]) }
        showAddLayer = false
        syncAfterEdit()
    }

    func addNull(threeD: Bool) {
        let id = engine.addNull(threeD)
        if id >= 0 { selection = [id]; engine.selectLayers([NSNumber(value: id)]) }
        showAddLayer = false
        syncAfterEdit()
    }

    func addText3D(content: String, depth: Float) {
        let id = engine.addText3D(content, depth: depth, alignment: 1, r: 1, g: 1, b: 1)
        if id >= 0 { selection = [id]; engine.selectLayers([NSNumber(value: id)]) }
        showAddLayer = false
        syncAfterEdit()
    }

    func addParticles(_ preset: UInt32) {
        let id = engine.addParticles(preset)
        if id >= 0 { selection = [id]; engine.selectLayers([NSNumber(value: id)]) }
        showAddLayer = false
        syncAfterEdit()
    }

    // =========================================================================
    // Export
    // =========================================================================
    func startExport() {
        guard !exporting else { return }
        let name = (projectName.isEmpty ? "Aurea" : projectName) + ".mp4"
        var url = AureaPaths.documents.appendingPathComponent(name)
        var counter = 1
        while FileManager.default.fileExists(atPath: url.path) {
            url = AureaPaths.documents.appendingPathComponent("\(projectName) \(counter).mp4")
            counter += 1
        }
        let ok = engine.startExport(to: url.path,
                                    codec: exportOptions.codec,
                                    height: exportOptions.shortSide,
                                    fps: exportOptions.fps,
                                    bitrateMbps: exportOptions.bitrateMbps,
                                    audioBitrateKbps: exportOptions.audioBitrateKbps)
        guard ok else {
            toast = "o export não pôde começar"
            return
        }
        exporting = true
        startExportPolling()
    }

    func cancelExport() {
        engine.cancelExport()
        exporting = false
    }

    private var exportTimer: Timer?

    private func startExportPolling() {
        exportTimer?.invalidate()
        let timer = Timer(timeInterval: 0.35, repeats: true) { [weak self] _ in
            Task { @MainActor in
                guard let self else { return }
                self.exportProgress = self.engine.exportProgress()
                let running = (self.exportProgress["running"] as? NSNumber)?.boolValue ?? false
                let finished = (self.exportProgress["finished"] as? NSNumber)?.boolValue ?? false
                if !running && finished {
                    self.exportTimer?.invalidate()
                    self.exportTimer = nil
                    self.exporting = false
                    let result = (self.exportProgress["result"] as? NSNumber)?.intValue ?? 0
                    self.toast = result == 0 ? AureaText.t("editor_video_pronto") : "o export falhou"
                    // Ponto seguro: o render acabou e o vídeo já está salvo. Não segura nada.
                    if result == 0 { AureaAdsManager.shared.showExportInterstitialIfAvailable {} }
                }
            }
        }
        RunLoop.main.add(timer, forMode: .common)
        exportTimer = timer
    }

    // =========================================================================
    // Formatação
    // =========================================================================
    func timecode(_ frame: Int64) -> String {
        let fps = max(1.0, (composition["fps"] as? NSNumber)?.doubleValue ?? 30)
        let seconds = Double(frame) / fps
        let mm = Int(seconds) / 60
        let ss = Int(seconds) % 60
        let ff = Int((seconds - Double(Int(seconds))) * fps)
        return String(format: "%02d:%02d:%02d", mm, ss, ff)
    }

    var compositionFps: Double { (composition["fps"] as? NSNumber)?.doubleValue ?? 30 }
    var compositionDuration: Int64 { (composition["duration"] as? NSNumber)?.int64Value ?? 0 }
    var compositionWidth: UInt32 { (composition["width"] as? NSNumber)?.uint32Value ?? 1920 }
    var compositionHeight: UInt32 { (composition["height"] as? NSNumber)?.uint32Value ?? 1080 }
}

// =============================================================================
// Açúcar: chamar um método simples da ponte sem repetir o lote.
// =============================================================================
extension AureaEngine {
    /// Para as chamadas que já são um comando único (play, undo, seek…).
    func run(_ body: (AureaEngine) -> Void) {
        beginBatch()
        body(self)
        _ = flush()
    }
}

extension UIImage {
    /// RGBA8 de alfa RETO → UIImage. O `alphaInfo` certo é
    /// `premultipliedLast` DEPOIS de multiplicar — o CoreGraphics não desenha
    /// alfa reto, então a conversão é feita aqui, uma vez, na miniatura.
    static func fromRGBA(_ data: Data, width: Int, height: Int) -> UIImage? {
        guard width > 0, height > 0, data.count >= width * height * 4 else { return nil }
        var premultiplied = [UInt8](data)
        for i in 0..<(width * height) {
            let a = UInt32(premultiplied[i * 4 + 3])
            if a == 0 || a == 255 { continue }
            for c in 0..<3 {
                premultiplied[i * 4 + c] = UInt8(UInt32(premultiplied[i * 4 + c]) * a / 255)
            }
        }
        let space = CGColorSpaceCreateDeviceRGB()
        let info = CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue)
        guard let provider = CGDataProvider(data: Data(premultiplied) as CFData),
              let cg = CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32,
                               bytesPerRow: width * 4, space: space, bitmapInfo: info,
                               provider: provider, decode: nil, shouldInterpolate: false,
                               intent: .defaultIntent) else { return nil }
        return UIImage(cgImage: cg)
    }
}
