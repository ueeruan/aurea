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
import AVFoundation
import Photos
import ImageIO
import Metal
import UIKit

// =============================================================================
// Tipos da UI, derivados do que a ponte devolve.
// =============================================================================
struct MotionBlurControls: Equatable {
    var enabled = false
    var angle: Float = 180
    var phase: Float = -90
    var samples: UInt32 = 16
    var adaptiveLimit: UInt32 = 128
    var previewSamples: UInt32 = 16

    init() {}
    init(_ snapshot: [NSNumber]) {
        guard snapshot.count >= 6 else { return }
        let values = snapshot.map(\.floatValue)
        guard values.allSatisfy({ $0.isFinite }), values[1] >= 0, values[1] <= 720,
              values[2] >= -360, values[2] <= 360, values[3] >= 2, values[3] <= 64,
              values[4] >= values[3], values[4] <= 256, values[5] >= 1, values[5] <= 256,
              values[3].rounded() == values[3], values[4].rounded() == values[4],
              values[5].rounded() == values[5] else { return }
        enabled = values[0] != 0
        angle = values[1]; phase = values[2]
        samples = UInt32(values[3]); adaptiveLimit = UInt32(values[4]); previewSamples = UInt32(values[5])
    }
}

struct LayerItem: Identifiable, Equatable {
    var id: Int64
    var kind: UInt32
    var name: String
    var startFrame: Int32
    var endFrame: Int32
    var offsetFrames: Int32
    var parentIndex: Int32
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
    /// LINHA MAGNÉTICA: os cortes desta camada andam como faixa de montagem
    /// de vídeo (aparar e apagar puxam os vizinhos DA MESMA linha).
    var magnetic: Bool
    /// A LINHA da timeline (`LayerRow::trackId`): trechos com o mesmo número
    /// dividem a mesma fileira, lado a lado no tempo. 0 = projeto antigo sem
    /// linha (a camada é a linha dela).
    var trackId: UInt32 = 0

    var duration: Int32 { max(0, endFrame - startFrame) }
}

struct KeyframeItem: Identifiable, Equatable {
    var property: UInt32
    var paramIndex: UInt32
    var effectIndex: UInt32
    var time: Int32
    var value: Float
    var interpolation: UInt32
    var id: String { "\(property)-\(effectIndex)-\(paramIndex)-\(time)" }
}

struct MaskItem: Identifiable {
    var id: UInt32
    var operation: UInt32
    var inverted: Bool
    var feather: Float
    var expansion: Float
    var opacity: Float
    var closed: Bool
    var keyed: Bool
    var points: [Float]
    var keyCount: Int = 0
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
    var flags: UInt32 = 0
    var index: UInt32
    var type: UInt32
    var label: String
    var unit: String
    var paramId: String
    var value: [Float]
    var defaultValue: [Float]
    var minValue: Float
    var maxValue: Float
    /// Faixa DIGITADA (teclado numérico): contém minValue...maxValue (a da régua).
    var hardMin: Float
    var hardMax: Float
    var animated: Bool
    var enumLabels: [String]
    var id: UInt32 { index }

    var scalar: Float { value.first ?? 0 }
}

struct EffectCatalogItem: Identifiable, Equatable {
    var effectClass: UInt32 = 0
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
    var bitrateMbps: UInt32 = 0   // 0 = automático pela qualidade (BitratePolicy do motor)
    /// 0 Baixa, 1 Normal, 2 Alta.
    var quality: UInt32 = 2
    var audioBitrateKbps: UInt32 = 192
    var aiUpscale: UInt32 = 0
    var trimToContent = true
    var fps: Double = 0   // 0 = o da composição
    /// Vídeo, quadro atual (PNG), sequência PNG (.zip) ou GIF (Exporter.kt: ExportFormat).
    var kind: ExportKind = .video
    /// PNG e sequência: lado menor; 0 = a resolução da composição.
    var imageShortSide: UInt32 = 0
    /// GIF: largura máxima (px) e quadros por segundo. A sequência usa `fps`.
    var gifWidth: UInt32 = 480
    var gifFps: Double = 15
}

/// Formato do export. `engineCode` = ImageExportFormat do motor
/// (export/ImageEncode.hpp); o vídeo usa o encoder da plataforma.
enum ExportKind: Int, CaseIterable {
    case video = -1, frame = 0, sequence = 1, gif = 2
    var engineCode: UInt32 { UInt32(max(0, rawValue)) }
    var fileExtension: String {
        switch self {
        case .video: return "mp4"
        case .frame: return "png"
        case .sequence: return "zip"
        case .gif: return "gif"
        }
    }
    /// Limites do motor (kGifMaxFrames / kSequenceMaxFrames).
    func tooLong(_ frames: Int) -> Bool {
        switch self {
        case .gif: return frames > 1800
        case .sequence: return frames > 18000
        default: return false
        }
    }
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

    /// File providers may need to download the file before its bytes exist.
    /// Callers hold security-scoped access for the complete coordinated read.
    static func readImport<T>(_ source: URL, _ read: (URL) throws -> T) throws -> T {
        var result: Result<T, Error> = .failure(NSError(domain: NSCocoaErrorDomain, code: NSFileReadUnknownError))
        var coordinationError: NSError?
        NSFileCoordinator().coordinate(readingItemAt: source, options: [], error: &coordinationError) { readable in
            result = Result { try read(readable) }
        }
        if let error = coordinationError { throw error }
        return try result.get()
    }

    static func copyImport(_ source: URL, to destination: URL) throws {
        try readImport(source) { try FileManager.default.copyItem(at: $0, to: destination) }
    }

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
/// O cabeçote para a TIMELINE, a cada quadro da tela durante a reprodução.
///
/// O `status` do motor é lido a 5 Hz (`statusTimer`): publicar a árvore inteira
/// a 60 Hz redesenharia tudo por nada. Mas a timeline anda com o cabeçote — a
/// 5 Hz ela rolava aos saltos durante o play. Este relógio é um objeto à parte,
/// observado só pela timeline (e o timecode dela); um CADisplayLink o atualiza
/// enquanto o motor está reproduzindo e para sozinho na pausa.
@MainActor
final class PlayheadClock: ObservableObject {
    @Published fileprivate(set) var frame: Int64 = 0
}

@MainActor
private final class DisplayLinkProxy: NSObject {
    let tick: () -> Void
    init(_ tick: @escaping () -> Void) { self.tick = tick }
    @objc func step(_ link: CADisplayLink) { tick() }
}

@MainActor
final class AureaModel: ObservableObject {

    // --- Ponte e estado do motor -------------------------------------------
    let engine: AureaEngine
    lazy var effectPreviews = EffectPreviewStore(engine: engine)
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
    /// Face Pivô do Transformar aberta: o arrasto no palco move o PIVÔ.
    @Published var pivotStageEdit = false
    /// Pivô no palco: só com a face aberta, fora da cena 3D solta.
    var pivotStageActive: Bool { pivotStageEdit && panel == .transform && !sceneEditor && pointPick == nil }
    @Published var showAddLayer = false
    @Published var showExport = false
    /// Busca da aba de efeitos aberta em tela cheia. Apresentada pela RAIZ do
    /// editor (EditorView): de dentro do painel o fullScreenCover não abria.
    @Published var effectSearch: EffectSearchRequest?
    @Published var showProjectSettings = false
    @Published var showSettings = false
    @Published var donationPromptPending = true
    /// A folha de geração atual (a doca de painéis, o painel aberto ou o
    /// "adicionar camada").
    ///
    /// A doca fica SEMPRE à mão (é de onde saem os painéis): `.none` abre a
    /// doca, e não uma tela sem folha — foi o que a casca A.01 fixou, e cada
    /// ladrilho aberto é que decide a fração da folha.
    var sheetContent: SheetContent {
        // A barra de adicionar fica embaixo sem camada escolhida — e também com a
        // camada só "na mão" da timeline (segurada/arrastada sem abrir as
        // opções): a geometria não muda no meio do gesto. Escolhendo keyframes, a
        // timeline fica alta (sem barra). Igual ao EditorScreen.kt.
        let timelineOnly = !selection.isEmpty && selection == timelineOnlySelection
        if showAddLayer { return selection.isEmpty || (timelineOnly && !timelineKeySelectMode) ? .addBar : .none }
        // O painel da Aurea AI CRIA a camada: abre sem nada selecionado (igual ao Android).
        if panel == .aiVideo || panel == .captions { return .panel }
        if selection.isEmpty { return .addBar }
        if timelineOnly && (panel == .none || panel == .dock) { return timelineKeySelectMode ? .none : .addBar }
        if selection.count > 1 { return .batch }
        if panel == .curve { return .curve }
        return panel == .none || panel == .dock ? .dock : .panel
    }

    // --- Projeto ------------------------------------------------------------
    @Published private(set) var projectURL: URL?
    @Published var projectName: String = ""
    @Published private(set) var dirty = false
    @Published private(set) var projects: [ProjectFile] = []
    @Published var searchQuery = ""
    @Published var sort: ProjectSort = .recent
    @Published private(set) var openingProject = false
    @Published private(set) var importingMedia = false
    private var mediaCreationRequest = UUID()
    private(set) var projectGeneration = UUID()
    // Closing the editor invalidates its UI requests, but may still finish the
    // current project's cover. Replacing the document invalidates both.
    private var projectContentGeneration = UUID() { didSet { thumbOwners.removeAll() } }
    /// Pedaço de um corte → camada dona das miniaturas da timeline (o
    /// `ThumbnailCache.alias` do Android). Os dois lados de um split mostram a
    /// mesma mídia no mesmo lugar: o pedaço novo usa os ladrilhos do original em
    /// vez de pedir a tira inteira de novo ao motor (cada pedido no lock do
    /// modelo + uma UIImage na main — o "cortar fica lento" no aparelho fraco).
    /// Não é @Published: mudar não redesenha nada.
    private var thumbOwners: [Int64: Int64] = [:]
    private var pendingSplit: (before: Set<Int64>, targets: [LayerItem], at: Int32)?
    func thumbOwner(_ layer: Int64) -> Int64 { thumbOwners[layer] ?? layer }
    private var thumbnailRequest = UUID()
    private var thumbnailTask: Task<Void, Never>?
    private var beatDetectionRequest: UUID?
    private var projectOperations: Set<UUID> = []
    @Published private(set) var operationMessage = AureaText.t("ios_importing_media")
    @Published var pointPick: Bool?
    /// Pick Focus da lente armado: o próximo toque no palco mede a distância de foco.
    @Published var focusPick: Bool = false
    /// Lente da câmera selecionada (9 valores, ver `AureaEngine.cameraLens`); vazio se não é câmera.
    @Published private(set) var cameraLens: [Float] = []
    @Published var curveEffect: UInt32 = UInt32.max
    @Published var curveParam: UInt32 = 0
    func graphKeyGroup(_ layer: Int64, _ key: KeyframeItem) -> [KeyframeItem] {
        guard let row = layers.first(where: { $0.id == layer }),
              key.effectIndex == UInt32.max, key.property < 12 else { return [key] }
        let threeD = row.threeD || [UInt32(8), 9, 10].contains(row.kind)
        let linked2DScale = scaleAxesLinked && (3...4).contains(key.property)
        guard threeD || linked2DScale else { return [key] }
        let base = key.property / 3 * 3
        let peers = (keyframes[layer] ?? []).filter {
            $0.effectIndex == UInt32.max && $0.property >= base && $0.property < base + (threeD ? 3 : 2) && $0.time == key.time
        }
        return peers.isEmpty ? [key] : peers
    }

    @Published var curveGraphMode = 0
    @Published var scaleAxesLinked = true
    @Published var timelineKeyDragActive = false
    @Published var transformTab = 0
    @Published var transformAnimatorFocus: TimelineTrack?
    private var transformAnimatorLayer: Int64?
    func focusLayerAnimator(_ track: TimelineTrack) {
        transformAnimatorLayer = primarySelection
        if transformAnimatorFocus != track { transformAnimatorFocus = track }
        if timelineFocus != [track] { timelineFocus = [track] }
    }
    func animatorRailTrack() -> TimelineTrack? {
        guard let id = primarySelection else { return nil }
        let count = engine.layerAnimators(id).count / 32
        if let focused = timelineFocus?.first, focused.property == 40,
           Int(focused.effect) < count, focused.param < 18 { return focused }
        if transformAnimatorLayer == id, let saved = transformAnimatorFocus,
           saved.property == 40, Int(saved.effect) < count, saved.param < 18 { return saved }
        if count > 0 { return TimelineTrack(property: 40, effect: 0, param: 0) }
        return nil
    }
    @Published var curveSelectedTime: Int32?
    @Published var captionOptions: [String: NSNumber] = [:]
    @Published var vectorGroup: UInt32 = 0
    @Published var vectorPath: UInt32 = 0
    @Published var vectorFreehand = false
    @Published var vectorPointTool = 0
    @Published var shapeSizeLinked = false
    @Published var shapeSelectedParam = 5
    @Published var liveNoticePopup: LiveNoticePopupRequest?
    @Published var vectorEditingPoints = false
    @Published var snapping = true
    /// Losangos de keyframe em todas as linhas (padrão); desligado, só as escolhidas mostram e deixam tocar.
    @Published var showAllKeyframes: Bool = true
    @Published var autoKeyTransforms = true
    /// Como um gesto de transform escreve no motor (a MESMA regra do Android,
    /// `transformWrite`): Auto-Key desligado desloca a animação inteira; na
    /// cena 3D só a trilha PARADA é deslocada — a animada grava keyframe no
    /// cabeçote, como fora dela. Antes a cena nunca marcava keyframe nenhum.
    func transformLayout(animated: Bool) -> Bool { !autoKeyTransforms || (sceneEditor && !animated) }
    @Published var freehandPoints: [Float] = []
    @Published var actionSheet: ActionSheetRequest?
    @Published var numericKeypad: KeypadRequest?
    @Published var colorSheet: ColorSheetRequest?
    @Published var expressionSheet: ExpressionRequest?
    @Published var namePrompt: NamePromptRequest?
    /// Aba com que o painel de presets abre na próxima vez ("Meus presets" dos efeitos).
    var presetsOpenSearch: String?
    @Published var presetsOpenKind: String?

    /// Grava um preset de efeitos do usuário (mesma pasta/formato do PresetsPanel).
    @discardableResult
    func storeEffectPreset(name: String, json: String) -> String? {
        let dir = PanelPresetKind.effects.directory
        let fm = FileManager.default
        var finalName = String(name.prefix(60)), n = 2
        while fm.fileExists(atPath: dir.appendingPathComponent(PanelPresetKind.fileName(finalName)).path) && finalName.hasPrefix("AM · ") {
            let suffix = " \(n)"
            finalName = String(name.prefix(60 - suffix.count)) + suffix; n += 1
        }
        let fileName = PanelPresetKind.fileName(finalName)
        guard fileName != ".json" else { return nil }
        do {
            try fm.createDirectory(at: dir, withIntermediateDirectories: true)
            try json.write(to: dir.appendingPathComponent(fileName), atomically: true, encoding: .utf8)
            return finalName
        } catch { return nil }
    }

    /// Salva UM efeito da camada (parâmetros + keyframes) como preset do usuário.
    func saveEffectPreset(effectId: UInt32, name: String) {
        guard let layer = primarySelection else { return }
        let json = engine.saveEffectPreset(layer, effect: effectId, name: name)
        guard !json.isEmpty else { toast = AureaText.t("msg_esta_camada_nao_tem_efeitos"); return }
        if let saved = storeEffectPreset(name: name, json: json) { toast = AureaText.t("msg_preset_salvo", saved) }
        else { toast = AureaText.t("msg_nao_foi_possivel_salvar_o_preset") }
    }

    /// Importa efeitos do Alight Motion (.xml/.amproj/.zip): só o que tem
    /// equivalente vira preset (o resto é aviso); com camada escolhida, aplica.
    func importAlightMotion(_ data: Data) {
        let raw = engine.importAlightMotion(data)
        let envelope = (try? JSONSerialization.jsonObject(with: Data(raw.utf8))) as? [String: Any]
        let error = envelope?["error"] as? String ?? ""
        let preset = envelope?["preset"] as? String ?? ""
        let mapped = (envelope?["mapped"] as? NSNumber)?.intValue ?? 0
        guard envelope != nil, error.isEmpty, !preset.isEmpty, mapped > 0 else {
            toast = AureaText.t("am_import_failed", error.isEmpty ? AureaText.t("am_import_nothing") : AureaEngineText.reason(error)); return
        }
        let skipped = (envelope?["skipped"] as? [Any])?.count ?? 0
        let warnings = (envelope?["warnings"] as? [Any])?.count ?? 0
        guard skipped == 0 && warnings == 0 else {
            toast = AureaText.t("am_import_lossless_required"); return
        }
        let base = (envelope?["name"] as? String).flatMap { $0.isEmpty ? nil : $0 } ?? "Alight Motion"
        guard let saved = storeEffectPreset(name: "AM · \(base)", json: preset) else {
            toast = AureaText.t("msg_nao_foi_possivel_salvar_o_preset"); return
        }
        if let layer = primarySelection {
            let failure = engine.applyPreset(layer, json: preset, duration: 0)
            if !failure.isEmpty { toast = AureaText.t("msg_preset_nao_aplicado", AureaEngineText.reason(failure)); return }
            refreshModel(force: true)
        }
        toast = skipped > 0 ? AureaText.t("am_import_done_skipped", mapped, skipped, saved) : AureaText.t("am_import_done", mapped, saved)
    }
    @Published var text3DFontSheet: Text3DFontRequest?
    struct TextContentRequest: Identifiable {
        let id: Int64
        let content: String
        let is3D: Bool
        let selectAll: Bool
        var alignment: Int = 0
    }
    @Published var textContentRequest: TextContentRequest?

    func openTextContentEditor(selectAll: Bool = false) {
        guard let id = primarySelection, selectedLayer?.locked != true else { return }
        if status.playing != 0 { playPause() }
        showAddLayer = false
        let recipe = engine.text3D(forLayer: id) ?? [:]
        let is3D = !recipe.isEmpty
        guard let content = (is3D ? recipe : (engine.text(forLayer: id) ?? [:]))["content"] as? String else { return }
        textContentRequest = TextContentRequest(id: id, content: content, is3D: is3D, selectAll: selectAll,
            alignment: (engine.text(forLayer: id)?["alignment"] as? NSNumber)?.intValue ?? 0)
    }

    func commitTextContent(_ request: TextContentRequest, content: String) -> Bool {
        guard layers.contains(where: { $0.id == request.id && !$0.locked }) else { return false }
        if content != request.content {
            if request.is3D {
                guard !content.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty,
                      engine.setText3D(forLayer: request.id, property: "content", stringValue: content, numberValue: 0) else { return false }
            } else { engine.setText(request.id, content: content) }
        }
        textContentRequest = nil
        refreshModel(force: true)
        return true
    }
    @Published var presetDialog: PresetDialogRequest?
    @Published var curveReturnPanel: PanelKind = .none
    @Published var stageManipulating = false
    @Published var gizmoLocalSpace = false
    /// Ferramenta das alças do gizmo 3D (GIZMO_* do EditorStore): 0 mover,
    /// 1 girar (Rotação X/Y/Z), 2 escala (Escala X/Y/Z; o centro = uniforme).
    @Published var gizmoTool = 0
    /// Girar/escala editam os eixos da PRÓPRIA camada: o gizmo mostra os locais.
    var gizmoAxesLocal: Bool { gizmoLocalSpace || gizmoTool != 0 }
    func cycleGizmoTool() { gizmoTool = (gizmoTool + 1) % 3 }
    /// Efeitos que o painel já mostrou, por camada (EffectsView revela o novo).
    var seenEffectIds: [Int64: Set<UInt32>] = [:]
    @Published var toast: String?

    // --- Modelo em memória (o que a timeline e os painéis desenham) ---------
    @Published private(set) var layers: [LayerItem] = []
    @Published private(set) var captionTracks: [NativeCaptionTrack] = []
    @Published private(set) var keyframes: [Int64: [KeyframeItem]] = [:]
    @Published private(set) var selection: Set<Int64> = []
    @Published private(set) var composition: [String: Any] = [:]
    @Published private(set) var effectCatalog: [EffectCatalogItem] = []
    @Published private(set) var effects: [EffectItem] = []
    @Published private(set) var effectParams: [EffectParamItem] = []
    /// Detalhe da camada escolhida (transform avaliado no playhead).
    @Published private(set) var detail: [String: Any] = [:]
    @Published private(set) var masks: [MaskItem] = []
    @Published private(set) var maskAffine: [Float] = [1, 0, 0, 1, 0, 0]
    @Published var selectedMask: UInt32?
    @Published var maskDrawing = false
    @Published var selectedMaskPoint: Int?
    /// Camadas em que a ferramenta-efeito Máscara foi posta (catálogo, busca). O
    /// cartão "Máscara" da pilha fica nelas mesmo vazio — sem caminho escolhido —
    /// até o 🗑 do cartão; antes ele só existia com uma máscara pronta e sumia ao
    /// fechar o painel. Vale para o projeto aberto (paridade EditorStore.maskToolLayers).
    @Published private(set) var maskToolLayers: Set<Int64> = []
    func addMaskTool() { if let id = primarySelection, !maskToolLayers.contains(id) { maskToolLayers.insert(id) } }
    func dropMaskTool(_ layer: Int64) { if maskToolLayers.contains(layer) { maskToolLayers.remove(layer) } }

    // --- Export -------------------------------------------------------------
    @Published var exportOptions = ExportOptions()
    @Published private(set) var exportProgress: [String: Any] = [:]
    @Published private(set) var exporting = false
    @Published private(set) var exportedURL: URL?
    @Published private(set) var exportMessage: String?
    @Published private(set) var exportCancelled = false
    /// A tela desistiu de um export sem progresso por minutos (ExportStallWatch).
    @Published private(set) var exportStalled = false
    @Published private(set) var exportPublishing = false
    @Published private(set) var exportSavedToPhotos = false
    /// Formato do export em andamento ou do último pronto (título e "Abrir").
    @Published private(set) var exportedKind: ExportKind = .video
    func openExport() {
        if !exporting { exportedURL = nil; exportMessage = nil; exportCancelled = false; exportProgress = [:] }
        showExport = true
    }

    func openCurve(property: UInt32, effect: UInt32 = UInt32.max, param: UInt32 = 0, time: Int32? = nil) {
        if panel != .curve { curveReturnPanel = panel }
        curveProperty = property; curveEffect = effect; curveParam = param; curveSelectedTime = time; openPanel(.curve)
    }
    private var pendingExportURL: URL?
    /// A última rede contra o export parado (ExportStallWatch) e o modo de
    /// segurança do vídeo em andamento (export/ExportWatchdog.hpp).
    private var exportWatch = ExportStallWatch()
    private var exportSafeMode: UInt32 = 0
    private let mediaQueue = DispatchQueue(label: "com.aurea.media-import", qos: .userInitiated)
    private let autosaveQueue = DispatchQueue(label: "com.aurea.autosave", qos: .utility)
    private let lifecycleQueue = DispatchQueue(label: "com.aurea.lifecycle", qos: .userInitiated)
    private var stopping = false
    private var restartAfterStop = false
    /// Capa da Home atrasada em relação ao `.aurea` (o autosave não a grava).
    private var homeCardStale = false
    private var lastAutosaveActivity = ProcessInfo.processInfo.systemUptime
    private var autosaveRetryAfter = 0.0
    private var unsavedSince = 0.0
    private var autosaving = false
    private var autosaveFailureShown = false

    // --- Ajustes ------------------------------------------------------------
    @Published var language: AureaLanguage = .systemDefault {
        didSet { AureaText.language = language; UserDefaults.standard.set(language.rawValue, forKey: "aurea.language") }
    }
    @Published var showPerf = false
    /// Tema escolhido nos Ajustes. É a identidade da raiz: trocar reconstrói
    /// as telas com a paleta nova, na hora (as cores são lidas ao desenhar).
    @Published private(set) var themeId: String = AureaTheme.palette.id
    func setTheme(_ id: String) {
        let palette = AureaPalette.of(id)
        guard palette.id != themeId else { return }
        AureaTheme.palette = palette
        UserDefaults.standard.set(palette.id, forKey: "aurea.theme")
        themeId = palette.id
    }

    enum Screen { case home, editor }
    enum PanelKind { case none, dock, transform, text, effects, layer3D, exportPanel, appearance, speed, clipEdit, audio, shape, shapeEdit, mask, textAnimation, curve, presets, particles, tracking, captions, vector, aiVideo }
    @Published var curveProperty: UInt32 = 0
    @Published var timelineFocus: [TimelineTrack]? = nil
    /// Keyframes escolhidos na timeline (várias trilhas de UMA camada; ações em
    /// TimelineModel.swift). nil = barra de ações fechada.
    @Published var timelineKeySelection: TimelineKeySelection? = nil
    /// Modo "Selecionar": tocar num losango soma/tira em vez de trocar.
    @Published var timelineKeySelectMode = false
    /// Modo "Selecionar várias camadas" (do app antigo): tocar num clipe da
    /// timeline soma/tira da seleção; a timeline fica inteira. Par do
    /// `layerSelectMode` do EditorStore.kt.
    @Published private(set) var timelineLayerSelectMode = false
    private var pendingPlayhead: Int64?
    private var pendingPlayheadUntil: TimeInterval = 0
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
    let playheadClock = PlayheadClock()
    private var playheadLink: CADisplayLink?
    private var memoryWarningObserver: NSObjectProtocol?
    @Published private(set) var memoryCacheEpoch: UInt64 = 0
    private var memoryCheckAt: TimeInterval = 0
    private var memoryTrimAt: TimeInterval = -.infinity
    private var memoryTrimPending = false
    private var memoryTrimLevel: Int32 = 0
    private var memoryTrimRequestedLevel: Int32 = 0
    private var memoryPressureLimited = false
    var thumbnailWorkAllowed: Bool { !memoryPressureLimited }
    private var lastRevision: UInt32 = 0
    private var lastThumbGeneration: UInt32 = 0
    private var lastEnginePlayhead: Int64 = .min
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
#if DEBUG
    private var parityPrepared = false
    /// CI only: create an actual core project and open the requested UI state.
    /// Release IPAs do not contain this entry point or fixture setup.
    func prepareParityCapture() {
        guard !parityPrepared, let scene = ProcessInfo.processInfo.environment["AUREA_PARITY_SCENE"] else { return }
        parityPrepared = true
        Task { @MainActor in
            language = .en
            var reopenedEditedProject = false
            var exportProbe: [String: Any] = [:]
            var precompProbe: [String: Any] = [:]
            var particleProbe: [String: Any] = [:]
            var faceProbe: [String: Any] = [:]
            var homeScrollProbe: [String: Any] = [:]
            var hdriProbe: [String: Any] = [:]
            if scene == "home-scroll", started {
                homeScrollProbe = await prepareParityHomeScroll()
            } else if scene == "android-hdri", started {
                hdriProbe = await prepareParityHDRI()
            } else if scene == "export-render", started {
                exportProbe = await ParityExportProbe.run(engine: engine, documents: AureaPaths.documents)
                refreshModel(force: true); enterEditor()
                if let id = layers.first?.id { select(layerId: id, additive: false); panel = .effects }
            } else if scene == "manual-android-project", started {
                // The binary project is produced by the Android instrumented
                // editing workflow. Do not reconstruct its layers on iOS.
                let url = AureaPaths.documents.appendingPathComponent("manual-editing.aurea")
                if engine.loadProject(url.path) {
                    projectURL = url; projectName = "Manual editing acceptance"
                    refreshModel(force:true); enterEditor(); seek(toFrame:0); setLooping(false)
                    if let id = layers.first(where: { $0.name == "Clip 1" })?.id {
                        select(layerId:id,additive:false); panel = .dock
                    }
                }
            } else if scene.hasPrefix("raw-"), started {
                let name = String(scene.dropFirst(4))
                let fps: Double = name.contains("60") || name.contains("vfr") ? 60 : 30
                let movie = AureaPaths.documents.appendingPathComponent(name + ".mp4")
                if FileManager.default.fileExists(atPath: movie.path),
                   newProject(width: 1920, height: 1080, fps: fps, title: "RAW playback test") {
                    let layer = engine.importVideo(movie.path, name: name)
                    if layer >= 0 {
                        refreshModel(force: true); enterEditor(); select(layerId: layer, additive: false)
                        panel = .dock; seek(toFrame: 0); setLooping(true); toggleRawPlayback()
                    }
                }
            } else if ["video-move", "playback-stress", "clip-edit"].contains(scene), started {
                // Reuse the real H.264 export fixture, then import through the
                // production decoder. The UI test operates only the visible dock.
                exportProbe = await ParityExportProbe.run(engine: engine, documents: AureaPaths.documents)
                if exportProbe["passed"] as? Bool == true,
                   let movie = exportProbe["movieFile"] as? String,
                   newProject(width: 640, height: 360, fps: 30, title: "Video dock move") {
                    let layer = engine.importVideo(AureaPaths.documents.appendingPathComponent(movie).path, name: "Dock move video")
                    if layer >= 0 {
                        if let compositionID = (engine.composition()?[AureaCompositionId] as? NSNumber)?.uint64Value {
                            engine.setComposition(compositionID, duration: scene == "playback-stress" ? 30 : 180)
                        }
                        if scene == "clip-edit" {
                            engine.setLayer(layer, startFrame: 0, endFrame: 15, offsetFrames: 5, setOffset: true)
                        }
                        refreshModel(force: true); enterEditor(); select(layerId: layer, additive: false)
                        panel = .dock; seek(toFrame: scene == "video-move" ? 60 : 0)
                        if scene == "playback-stress" {
                            _ = engine.addText("Texto durante reprodução")
                            _ = engine.createCaptions(layer, words: [["word": "Legenda", "start": 0.0, "end": 0.9]], options: [:])
                            refreshModel(force: true); select(layerId: layer, additive: false)
                            setLooping(true)
                        }
                        _ = saveProject(writeThumbnail: false)
                    }
                }
            } else if ["metal-face-culling", "metal-face-control"].contains(scene), started {
                faceProbe = prepareParityFaces(scene: scene)
            } else if ["android-precomp", "android-precomp-inside"].contains(scene), started {
                let url = AureaPaths.documents.appendingPathComponent("android-precomp.aurea")
                if engine.loadProject(url.path) {
                    projectURL = url; projectName = "Parity Precomp"; refreshModel(force: true); enterEditor()
                    precompProbe = ["loaded": true, "rootLayers": engine.layers()]
                    if let id = layers.first(where: { $0.kind == 12 })?.id {
                        select(layerId: id, additive: false); panel = .dock
                        // Save the root state before navigating into the real
                        // nested composition, so the exported fixture opens at root.
                        _ = saveProject(writeThumbnail: true)
                        if scene == "android-precomp-inside" {
                            openGroup(id); panel = .none
                            precompProbe["entered"] = engine.precompDepth == 1
                        }
                    }
                }
            } else if ["android-complex-3d", "android-complex-particles"].contains(scene), started {
                let url = AureaPaths.documents.appendingPathComponent("android-complex.aurea")
                if engine.loadProject(url.path) {
                    projectURL = url; projectName = "Parity Complex"; refreshModel(force: true); enterEditor()
                    seek(toFrame: 36)
                    let kind: UInt32 = scene == "android-complex-3d" ? 10 : 11
                    if let id = layers.first(where: { $0.kind == kind })?.id {
                        select(layerId: id, additive: false); panel = kind == 10 ? .layer3D : .particles
                        if kind == 11 { particleProbe = await captureParityParticles(layerId: id, scene: scene) }
                    }
                    _ = saveProject(writeThumbnail: true)
                }
            } else if ["android-project", "android-edited"].contains(scene), started {
                let url = AureaPaths.documents.appendingPathComponent("android-reference.aurea")
                if engine.loadProject(url.path) {
                    projectURL = url; projectName = "Project 1"; refreshModel(force: true); enterEditor()
                    if let id = layers.first?.id { select(layerId: id, additive: false); panel = .effects }
                    if scene == "android-edited" { editTransform(8, value: 15); editTransform(12, value: 0.75) }
                    _ = saveProject(writeThumbnail: true)
                    if scene == "android-edited" {
                        reopenedEditedProject = engine.loadProject(url.path)
                        refreshModel(force: true)
                        if let id = layers.first?.id { select(layerId: id, additive: false); panel = .effects }
                    }
                }
            } else if scene != "home", started {
                _ = newProject(width: 1920, height: 1080, fps: 30, title: "Project 1")
                if scene != "editor-empty" {
                    switch scene {
                    case "text-2d": addText(); textContentRequest = nil; panel = .text
                    case "text-3d": addText3D(content: AureaText.t("panel_texto"), depth: 0.25); textContentRequest = nil; panel = .layer3D
                    case "text-edit-2d": addText()
                    case "text-edit-3d": addText3D(content: AureaText.t("panel_texto"), depth: 0.25)
                    case "curve-null": addNull(threeD: true); panel = .transform
                    case "transform-expression":
                        addText(); textContentRequest = nil
                        if let id = primarySelection {
                            let request = ExpressionRequest(layer: id, label: "Position",
                                tracks: [ExpressionTrack(property: 0), ExpressionTrack(property: 1)])
                            _ = engine.setExpressions(id, tracks: request.packedTracks, source: "[540, 515]")
                            refreshModel(force: true)
                        }
                        transformTab = 0; panel = .transform
                    case "rotation-isolation":
                        addNull(threeD: true)
                        if let id = primarySelection {
                            for property in UInt32(6)...UInt32(8) {
                                engine.insertKeyframe(forLayer: id, property: property, time: 0, value: 0)
                            }
                            engine.seek(toFrame: 15); refreshModel(force: true)
                        }
                        panel = .transform
                    case "curve-isolation":
                        addNull(threeD: true)
                        if let id = primarySelection {
                            for time in [Int32(0), Int32(30), Int32(60)] {
                                for property in UInt32(0)...UInt32(2) {
                                    engine.insertKeyframe(forLayer: id, property: property, time: time, value: Float(200 + time))
                                }
                            }
                            engine.seek(toFrame: 0); refreshModel(force: true)
                        }
                        panel = .transform
                    case "curve-isolation-2d":
                        // Nulo 2D: Posição X e Y são trilhas independentes (sem grupo XYZ).
                        addNull(threeD: false)
                        if let id = primarySelection {
                            for time in [Int32(0), Int32(30), Int32(60)] {
                                for property in UInt32(0)...UInt32(1) {
                                    engine.insertKeyframe(forLayer: id, property: property, time: time, value: Float(200 + time))
                                }
                            }
                            engine.seek(toFrame: 0); refreshModel(force: true)
                        }
                        panel = .transform
                    case "timeline-markers":
                        addNull(threeD: false)
                        for frame in [Int64(30), 60, 90] {
                            _ = engine.editMarker(from: -1, to: frame, color: 0xFFFF00FF, label: "Beat \(frame)")
                        }
                        refreshModel(force: true); refreshMarkers()
                        panel = .dock; seek(toFrame: 60)
                    case "timeline-keys":
                        // Posição X em 0 e 30; Escala X em 15 e 45 (seleção entre propriedades).
                        addNull(threeD: false)
                        if let id = primarySelection {
                            let keys: [(UInt32, Int32)] = [(0, 0), (0, 30), (3, 15), (3, 45)]
                            for (property, time) in keys {
                                engine.insertKeyframe(forLayer: id, property: property, time: time, value: Float(100 + time))
                            }
                            refreshModel(force: true)
                            // Camada só na timeline (sem doca): a timeline fica alta e as trilhas cabem.
                            select(layerId: id, additive: false, openOptions: false)
                            panel = .none
                            seek(toFrame: 30)
                        }
                    case "timeline-linked-scale":
                        addShape(1)
                        if let id = primarySelection {
                            engine.run { commands in
                                for time in [Int32(0), 30, 60] {
                                    commands.insertKeyframe(forLayer: id, property: 3, time: time, value: time == 30 ? 2 : 1)
                                    commands.insertKeyframe(forLayer: id, property: 4, time: time, value: time == 30 ? 4 : 2)
                                }
                                commands.seek(toFrame: 0)
                            }
                            scaleAxesLinked = true; snapping = false; refreshModel(force: true)
                            openCurve(property: 3, time: 0)
                        }
                    case "timeline-reorder":
                        for _ in 0..<8 { addShape(1) }
                    case "scene-keyframe":
                        // Nulo 3D com Posição animada no quadro 0, cabeçote no 30, já dentro da
                        // cena 3D: o teste arrasta o nulo e espera o keyframe do quadro 30.
                        addNull(threeD: true)
                        refreshModel(force: true)
                        if let id = primarySelection {
                            let position = StageGeom.floats(detail["position"])
                            for property in UInt32(0)...UInt32(2) where position.count == 3 {
                                engine.insertKeyframe(forLayer: id, property: property, time: 0, value: position[Int(property)])
                            }
                            engine.seek(toFrame: 30); refreshModel(force: true)
                        }
                        enterSceneEditor()
                    case "parent-new-null":
                        // Duas formas escolhidas juntas: o teste liga as duas a um nulo novo.
                        addShape(1); addShape(1)
                        refreshModel(force: true)
                        if let first = layers.first?.id, let last = layers.last?.id, first != last {
                            select(layerId: first, additive: false)
                            select(layerId: last, additive: true)
                        }
                    case "mask-animation":
                        addShape(1); addMask(0)
                        if let id = primarySelection, let mask = selectedMask {
                            _ = engine.toggleMaskParamKey(id, mask: mask, param: 2)
                            engine.run { $0.seek(toFrame: 30) }
                            _ = engine.setMaskParam(id, mask: mask, param: 2, value: 0.25)
                            engine.run { $0.seek(toFrame: 0) }
                            panel = .mask; refreshModel(force: true)
                        }
                    case "motion-blur-controls":
                        addShape(1)
                        if let id = primarySelection {
                            engine.run { $0.setMotionBlur(true, forLayer: id) }
                            _ = engine.setMotionBlurSettings(true, shutter: 181, phase: 45, samples: 16, adaptiveLimit: 128)
                            engine.setLayerMotionBlurLength(1.25, forLayer: id)
                            refreshModel(force: true); transformTab = 5; panel = .transform
                        }
                    case "motion-blur-export":
                        let id = engine.addText("AUREA MOTION BLUR")
                        if id >= 0 {
                            _ = engine.applyTextPreset(id, preset: 11)
                            engine.run {
                                $0.setMotionBlur(true, forLayer: id)
                                $0.setMotionBlurSettings(true, shutter: 180)
                                $0.setLayer(id, startFrame: 0, endFrame: 60, offsetFrames: 0, setOffset: false)
                                $0.seek(toFrame: 0)
                            }
                            refreshModel(force: true); select(layerId: id, additive: false)
                            panel = .none
                        }
                    case "animator-curve-rail":
                        addText3D(content: "AUREA", depth: 0.25); textContentRequest = nil
                        if let id = primarySelection {
                            _ = engine.addLayerAnimator(id); _ = engine.addLayerAnimator(id)
                            engine.run {
                                $0.keyParameter(id, property: 40, effect: 1, param: 13, time: 0, value: 0)
                                $0.keyParameter(id, property: 40, effect: 1, param: 13, time: 30, value: 200)
                                $0.seek(toFrame: 0)
                            }
                            refreshModel(force: true); transformTab = 6
                            focusLayerAnimator(TimelineTrack(property: 40, effect: 1, param: 13))
                            panel = .transform
                        }
                    case "text-animator-editing":
                        let textId = engine.addText("AUREA")
                        if textId >= 0 { refreshModel(force: true); select(layerId: textId, additive: false) }
                        if let id = primarySelection {
                            _ = engine.addTextAnimator(id, props: 1 << 9)
                            engine.setTextAnimParam(id, index: 0, param: 6, value: 1)
                            engine.toggleTextAnimKey(id, index: 0, param: 6)
                            engine.run { $0.seek(toFrame: 30) }
                            engine.setTextAnimParam(id, index: 0, param: 6, value: 0)
                            engine.run {
                                $0.seek(toFrame: 0)
                                $0.addEffect(fxEffectTypeId("aurea.text.transform"), toLayer: id, at: UInt32.max)
                            }
                            refreshModel(force: true)
                            panel = .textAnimation
                        }
                    case "text-transform":
                        // Texto sozinho com Animação aberta: o teste põe o Text Transform
                        // pela ficha e arrasta Deslocamento X e depois Y (o "não funciona").
                        let textId = engine.addText("AUREA")
                        if textId >= 0 { refreshModel(force: true); select(layerId: textId, additive: false) }
                        panel = .textAnimation
                    case "null-link":
                        // Pedido 2026-10-02: "vincula mas não mexe". Texto ligado ao
                        // 4º nulo pela MESMA função do seletor de pai; o 4º fica escolhido.
                        let text = engine.addText("ABC")
                        for _ in 0..<4 { addNull(threeD: false) }
                        refreshModel(force: true)
                        if let fourth = primarySelection, text >= 0 {
                            setParent(text, parent: fourth)
                            refreshModel(force: true)
                            select(layerId: fourth, openOptions: false)
                        }
                    case "null-add":
                        // Um nulo já criado: o teste cria mais pela barra de adicionar.
                        addNull(threeD: false)
                        refreshModel(force: true)
                    case "timeline-arrangement":
                        addShape(1); addShape(1); addShape(1)
                        refreshModel(force: true)
                        let ranges: [(Int32, Int32)] = [(10, 20), (12, 37), (71, 78)]
                        engine.run { commands in
                            for (index, row) in layers.enumerated() {
                                commands.setLayer(row.id, startFrame: ranges[index].0, endFrame: ranges[index].1,
                                                  offsetFrames: 0, setOffset: false)
                            }
                        }
                        refreshModel(force: true)
                        for (index, row) in layers.enumerated() { select(layerId: row.id, additive: index > 0) }
                    case "stagger":
                        // Três formas no mesmo início, todas escolhidas: o teste escalona pela barra de lote.
                        addShape(1); addShape(1); addShape(1)
                        refreshModel(force: true)
                        for (index, row) in layers.enumerated() { select(layerId: row.id, additive: index > 0) }
                    default: addShape(1)
                    }
                    if scene == "transform" { panel = .transform }
                    if scene == "effects" { panel = .effects }
                    if scene == "export" { openExport() }
                    if scene == "project-settings" { clearSelection(); showProjectSettings = true }
                    if scene == "shape-edit" { panel = .shapeEdit }
                    if ["curve-null", "curve-shape"].contains(scene), let id = primarySelection {
                        let property: UInt32 = scene == "curve-null" ? 1 : 35
                        let param: UInt32 = scene == "curve-null" ? 0 : 6
                        for time in [Int32(0), Int32(30)] {
                            if scene == "curve-null" {
                                engine.insertKeyframe(forLayer: id, property: property, time: time, value: time == 0 ? 200 : 450)
                            } else {
                                // seek é enfileirado e keyShape é direto: o 2º key caía no
                                // quadro 0. Inserir pela fila fixa o tempo explicitamente.
                                engine.editTrackKey(id, property: property, effect: 0, param: param, time: time, action: 4,
                                                    value: time == 0 ? 200 : 450, targetTime: time, interpolation: 2, handles: [])
                            }
                        }
                        engine.seek(toFrame: 0)
                        refreshModel(force: true)
                        panel = scene == "curve-null" ? .transform : .shapeEdit
                    }
                    if scene == "appearance" { panel = .appearance }
                    if scene == "presets" { panel = .presets }
                    if scene == "mask" { addMask(1); panel = .mask }
                }
                _ = saveProject(writeThumbnail: true)
            }
            try? await Task.sleep(nanoseconds: 2_000_000_000)
            var frameWidth: UInt32 = 0, frameHeight: UInt32 = 0
            if started, !["home", "home-scroll"].contains(scene), let frame = await capturePreviewFrame(480),
               let preview = UIImage.fromRGBA(frame.data, width: Int(frame.width), height: Int(frame.height))?.pngData() {
                frameWidth = frame.width; frameHeight = frame.height
                try? preview.write(to: AureaPaths.documents.appendingPathComponent(scene + "-core.png"))
            }
            refreshStatus()
            if primarySelection != nil { refreshSelectedLayer() }
            let report: [String: Any] = ["scene": scene, "coreStarted": started,
                "coreError": startError ?? "", "layerCount": layers.count,
                "width": compositionWidth, "height": compositionHeight, "fps": compositionFps,
                "duration": compositionDuration, "layers": engine.layers(), "effects": effects.map { ["id": $0.effectId, "name": $0.name] },
                "project": projectURL?.lastPathComponent ?? "", "device": deviceSummary,
                "frameWidth": frameWidth, "frameHeight": frameHeight, "detail": detail,
                "renderDiagnostics": engine.renderDiagnostics(),
                "playhead": status.playhead, "particleProbe": particleProbe,
                "exportProbe": exportProbe,
                "faceProbe": faceProbe,
                "homeScrollProbe": homeScrollProbe,
                "hdriProbe": hdriProbe,
                "precompProbe": precompProbe, "precompDepth": engine.precompDepth,
                "reopenedEditedProject": reopenedEditedProject,
                "uiTestRunID": ProcessInfo.processInfo.environment["AUREA_UI_TEST_RUN_ID"] ?? ""]
            if let data = AureaJSONData(report, true) {
                try? data.write(to: AureaPaths.documents.appendingPathComponent("parity-ready.json"))
            }
        }
    }

    /// The environment must come from the original Android project. Never
    /// import/reassign its HDR here: that would hide a broken asset resolver.
    private func prepareParityHDRI() async -> [String: Any] {
        let original = AureaPaths.documents.appendingPathComponent("android-hdri.aurea")
        let roundtrip = AureaPaths.documents.appendingPathComponent("android-hdri-roundtrip.aurea")
        let originalBytes = try? Data(contentsOf: original)
        var probe: [String: Any] = ["fixture": original.lastPathComponent,
            "roundtripFile": roundtrip.lastPathComponent, "loaded": false,
            "saved": false, "reopened": false, "frames": [[String: Any]]()]
        guard engine.loadProject(original.path) else { return probe }
        probe["loaded"] = true
        projectURL = original; projectName = "Parity HDRI"
        refreshModel(force: true); enterEditor()
        var frames: [[String: Any]] = []
        func prepareFrame() {
            engine.run { core in core.pause(); core.seek(toFrame: 0) }
            if let id = layers.first(where: { $0.kind == 10 })?.id {
                select(layerId: id, additive: false); panel = .layer3D
            }
        }
        func capture(_ phase: String) async {
            var width: UInt32 = 0, height: UInt32 = 0
            var row: [String: Any] = ["phase": phase, "environment": engine.environment(),
                "layers": engine.layers(), "loadNotice": engine.lastLoadNotice,
                "missingAssets": engine.lastLoadMissingAssets]
            if let id = primarySelection { row["objectEnvironment"] = engine.objectEnvironment(forLayer: id) }
            if let frame = await capturePreviewFrame(480),
               let png = UIImage.fromRGBA(frame.data, width: Int(frame.width), height: Int(frame.height))?.pngData() {
                width = frame.width; height = frame.height
                let name = "android-hdri-\(phase).png"
                do { try png.write(to: AureaPaths.documents.appendingPathComponent(name)); row["file"] = name }
                catch { row["error"] = error.localizedDescription }
            }
            var actual = AureaStatus()
            row["statusRead"] = engine.readStatus(&actual)
            row["actualFrame"] = actual.playhead
            row["width"] = width; row["height"] = height
            frames.append(row)
        }
        prepareFrame(); await capture("loaded")
        // Save to a new path: retain the approved Android input byte-for-byte.
        let saved = engine.saveProject(roundtrip.path)
        probe["saved"] = saved
        if saved, engine.loadProject(roundtrip.path) {
            probe["reopened"] = true
            projectURL = roundtrip; refreshModel(force: true)
            prepareFrame(); await capture("reopened")
        }
        probe["originalUnchanged"] = originalBytes != nil && originalBytes == (try? Data(contentsOf: original))
        probe["frames"] = frames
        refreshStatus()
        return probe
    }

    /// Actual core projects and rendered JPEGs; no UI-only cards or fake covers.
    private func prepareParityHomeScroll() async -> [String: Any] {
        let colors: [[Float]] = [
            [1, 0.1, 0.15], [0.05, 0.4, 1], [0.1, 0.9, 0.25], [0.9, 0.1, 1],
            [1, 0.6, 0.05], [0.05, 0.8, 0.9], [0.75, 0.15, 0.05], [0.3, 0.1, 1],
            [0.1, 0.65, 0.4], [1, 0.15, 0.6], [0.7, 0.75, 0.05], [0.1, 0.4, 0.95],
        ]
        var rows: [[String: Any]] = []
        for (index, color) in colors.enumerated() {
            let title = String(format: "Parity Glass %02d", index + 1)
            guard newProject(width: 1920, height: 1080, fps: 30, title: title),
                  let url = projectURL else { break }
            addShape(index.isMultiple(of: 2) ? 1 : 2)
            let id = (composition["id"] as? NSNumber)?.uint64Value ?? 0
            let linear = AureaColorSpace.displayToEngine(color[0] * 0.75, color[1] * 0.75, color[2] * 0.75, 1)
            mutate { $0.setComposition(id, backgroundR: linear[0], g: linear[1], b: linear[2], a: 1) }
            editTransform(8, value: Float(index * 11))
            let saved = saveProject(writeThumbnail: false)
            await writeProjectThumbnail(name: url.deletingPathExtension().lastPathComponent).value
            writeHomeProjectMeta(url: url, title: title, width: 1920, height: 1080,
                                 fps: 30, durationFrames: Int(compositionDuration))
            let thumbnail = AureaPaths.thumbs.appendingPathComponent(url.deletingPathExtension().lastPathComponent + ".jpg")
            rows.append(["file": url.lastPathComponent, "saved": saved,
                         "thumbnail": thumbnail.lastPathComponent,
                         "hasThumbnail": FileManager.default.fileExists(atPath: thumbnail.path)])
        }
        clearSelection(); panel = .none; screen = .home; refreshProjectList()
        return ["projects": rows, "count": rows.count]
    }

    /// Real glTF import and Metal render; the host independently counts red and
    /// green pixels. The double-sided control proves both triangles imported.
    private func prepareParityFaces(scene: String) -> [String: Any] {
        let file = scene + ".gltf"
        var probe: [String: Any] = ["fixture": file,
            "doubleSided": scene == "metal-face-control", "imported": false, "saved": false]
        guard newProject(width: 1920, height: 1080, fps: 30, title: scene) else {
            probe["error"] = "Cannot create the front-face probe project"
            return probe
        }
        let url = AureaPaths.documents.appendingPathComponent(file)
        let id = engine.importModel(url.path, name: "Front red / back green")
        guard id >= 0 else {
            probe["error"] = engine.lastImportError
            return probe
        }
        probe["imported"] = true
        probe["layerID"] = id
        refreshModel(force: true)
        select(layerId: id, additive: false)
        panel = .layer3D
        engine.run { core in core.pause(); core.seek(toFrame: 0) }
        // Saving drains queued state before capture; no timer stands in for
        // import completion or a GPU result. captureFrame performs the render.
        probe["saved"] = saveProject(writeThumbnail: false)
        return probe
    }

    /// Render the imported Android Snow layer alone, so the 3D text beneath it
    /// cannot make a broken particle pass look successful. Every capture drains
    /// the real command queue and renders the requested frame through the core.
    private func captureParityParticles(layerId: Int64, scene: String) async -> [String: Any] {
        let originalLayers = layers
        let originalParameters = engine.particleParams(layerId)
        engine.run { core in
            core.pause()
            for layer in originalLayers { core.setLayer(layer.id, visible: layer.id == layerId) }
        }
        defer {
            engine.run { core in
                for layer in originalLayers { core.setLayer(layer.id, visible: layer.visible) }
                core.seek(toFrame: 36)
            }
        }
        var frames: [[String: Any]] = []
        var rgba: [Int64: [UInt8]] = [:]
        func capture(_ requested: Int64, mode: String) async {
            var width: UInt32 = 0, height: UInt32 = 0
            var row: [String: Any] = ["requestedFrame": requested, "mode": mode]
            if let frame = await capturePreviewFrame(480),
               let image = UIImage.fromRGBA(frame.data, width: Int(frame.width), height: Int(frame.height))?.pngData() {
                width = frame.width; height = frame.height
                let file = "\(scene)-isolated-\(requested).png"
                do {
                    try image.write(to: AureaPaths.documents.appendingPathComponent(file))
                    row["file"] = file
                    rgba[requested] = [UInt8](frame.data)
                } catch { row["error"] = error.localizedDescription }
            }
            var actual = AureaStatus()
            row["statusRead"] = engine.readStatus(&actual)
            row["actualFrame"] = actual.playhead
            row["localFrame"] = engine.layerDetail(layerId)?["localPlayhead"] ?? -1
            row["width"] = width; row["height"] = height
            row["visibleLayerIDs"] = engine.layers().filter { ($0["visible"] as? Bool) == true }
                .compactMap { $0["id"] as? NSNumber }
            frames.append(row)
        }
        for frame in [Int64(0), 36, 72] {
            seek(toFrame: frame)
            await capture(frame, mode: "seek")
        }
        engine.run { core in core.scrubBegin(); core.scrub(toFrame: 59) }
        await capture(59, mode: "scrub")
        engine.run { $0.scrubEnd() }
        func changedPixels(_ first: Int64, _ second: Int64) -> Int {
            guard let a = rgba[first], let b = rgba[second], a.count == b.count else { return -1 }
            var changed = 0
            for offset in stride(from: 0, to: a.count, by: 4) {
                if (0..<3).contains(where: { abs(Int(a[offset + $0]) - Int(b[offset + $0])) > 8 }) {
                    changed += 1
                }
            }
            return changed
        }
        return ["layerID": layerId, "parameters": originalParameters,
                "parametersUnchanged": originalParameters == engine.particleParams(layerId),
                "frames": frames, "changedFromZero": changedPixels(0, 36),
                "changedBetweenTimes": changedPixels(36, 72)]
    }
#endif

    func start() {
        if stopping { restartAfterStop = true; return }
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
            startError = (error as String?).map { AureaEngineText.sentence($0) } ?? AureaText.t("ios_engine_not_started")
        }
    }

    func enterBackground() {
        guard started else { return }
        // O play não segue soando em segundo plano (o `suspend` abaixo também
        // cala a saída; este pedido vai antes da gravação, que drena a fila).
        stopPlayback()
        releaseInterfaceCaches()
        // Grava ANTES de suspender (o motor ainda renderiza a capa) e dentro de
        // uma tarefa de segundo plano: o iOS pode congelar o app logo depois do
        // `.background`, e quem arrasta o app para fora do seletor mata o
        // processo. O motor drena os comandos e decide se há o que gravar — o
        // `status.dirty` da UI chega atrasado e deixava a última edição de fora.
        if screen == .editor, projectURL != nil {
            let app = UIApplication.shared
            var task = UIBackgroundTaskIdentifier.invalid
            task = app.beginBackgroundTask(withName: "aurea-salvar") {
                if task != .invalid { app.endBackgroundTask(task); task = .invalid }
            }
            saveOnLeave(forceThumbnail: false)
            if task != .invalid { app.endBackgroundTask(task); task = .invalid }
        }
        // Suspending cancels and drains any export before releasing decoders.
        // A codec may take time to return its last frame; never block UIKit's
        // lifecycle callback waiting for the worker (watchdog termination).
        let app = UIApplication.shared
        var task = UIBackgroundTaskIdentifier.invalid
        task = app.beginBackgroundTask(withName: "aurea-suspender") {
            if task != .invalid { app.endBackgroundTask(task); task = .invalid }
        }
        let lifecycleEngine = engine
        lifecycleQueue.async {
            lifecycleEngine.suspend()
            DispatchQueue.main.async {
                if task != .invalid { app.endBackgroundTask(task); task = .invalid }
            }
        }
    }

    func enterInactive() {
        guard started else { return }
        // Permission sheets and Control Center are transient. Stop playback,
        // but keep an export alive until the app actually enters background.
        stopPlayback()
    }

    /// Para a reprodução E o som (sair do editor, app inativo/em segundo
    /// plano). O motor cala o AVAudioEngine no próprio comando de pausa — não
    /// espera um quadro de render, que não vem com o palco fora da tela.
    /// Idempotente: pausar parado não faz nada.
    func stopPlayback() {
        guard started else { return }
        pendingPlayhead = nil
        engine.run { $0.pause() }
        status.playing = 0
        followPlayback(false)
    }

    /// Sair do editor / do app: grava só o que mudou (um projeto limpo não é
    /// reescrito — o `.bak` segue sendo a gravação anterior de verdade) e refaz
    /// a capa da Home quando gravou, quando ela estava atrasada ou se pedida.
    @discardableResult
    private func saveOnLeave(forceThumbnail: Bool) -> Bool {
        guard let url = projectURL else { return false }
        let code = engine.saveProjectIfDirty()
        if code != 0 && code != -1 {
            toast = AureaText.t("ios_save_failed")
            return false
        }
        if exporting {
            // The exporter owns the render context until it has drained.
            // Refresh this cover on the next idle save instead.
            homeCardStale = true
        } else if code == 0 || homeCardStale || forceThumbnail {
            writeProjectThumbnail(name: url.deletingPathExtension().lastPathComponent)
            homeCardStale = false
        }
        dirty = false
        return true
    }

    func enterForeground() {
        guard started else { return }
        checkMemoryPressure(force: true)
        let lifecycleEngine = engine
        lifecycleQueue.async { [weak self] in
            lifecycleEngine.resume()
            lifecycleEngine.invalidate()
            DispatchQueue.main.async { self?.refreshModel(force: true) }
        }
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
                self.trimForMemoryPressure(level: 15)
            }
        }
    }

    private func releaseInterfaceCaches() {
        HomeThumbCache.shared.clear()
        effectPreviews.trimMemory()
        MaterialThumbStore.shared.trimMemory()
        memoryCacheEpoch &+= 1
    }

    private func trimForMemoryPressure(level: Int32) {
        memoryPressureLimited = true
        releaseInterfaceCaches()
        memoryTrimRequestedLevel = max(memoryTrimRequestedLevel, level)
        scheduleMemoryTrimIfNeeded()
    }

    private func scheduleMemoryTrimIfNeeded() {
        guard !memoryTrimPending else { return }
        let level = memoryTrimRequestedLevel
        let now = ProcessInfo.processInfo.systemUptime
        // Critical warnings bypass the moderate trim's cooldown. If a native
        // trim is in flight, retain only the strongest request until it returns.
        guard level > 0, level > memoryTrimLevel || now - memoryTrimAt >= 5 else { return }
        memoryTrimRequestedLevel = 0
        memoryTrimLevel = level
        memoryTrimAt = now
        memoryTrimPending = true
        let native = engine
        lifecycleQueue.async { [weak self] in
            _ = native.trimMemory(level)
            DispatchQueue.main.async {
                guard let self else { return }
                self.memoryTrimPending = false
                self.scheduleMemoryTrimIfNeeded()
            }
        }
    }

    /// The process's jetsam headroom is distinct from Android's free system RAM.
    /// Poll independently of the HUD: iOS can terminate without delivering a warning.
    private func checkMemoryPressure(force: Bool = false) {
        let now = ProcessInfo.processInfo.systemUptime
        guard force || now - memoryCheckAt >= 1 else { return }
        memoryCheckAt = now
        let available = engine.availableMemoryBytes()
        // Classe de memória LOW (até ~4 GB, o mesmo corte do motor): a reserva
        // sobe de 128/64 MB para 192/96 MB, como no Android.
        let reserve: UInt64 = DeviceMemoryClass.low ? 192 * 1024 * 1024 : 128 * 1024 * 1024
        let critical: UInt64 = DeviceMemoryClass.low ? 96 * 1024 * 1024 : 64 * 1024 * 1024
        // For an app, zero can also mean its allocation limit is already exceeded.
        if available < reserve {
            let level: Int32 = available < critical ? 15 : 10
            if !memoryPressureLimited || level > max(memoryTrimLevel, memoryTrimRequestedLevel) || now - memoryTrimAt >= 5 {
                trimForMemoryPressure(level: level)
            }
        } else if available >= reserve + 64 * 1024 * 1024 || !memoryPressureLimited {
            memoryPressureLimited = false
            effectPreviews.resumeMemoryWork()
        }
    }

    func stop() {
        restartAfterStop = false
        guard !stopping else { return }
        stopping = true
        if let observer = memoryWarningObserver {
            NotificationCenter.default.removeObserver(observer)
            memoryWarningObserver = nil
        }
        statusTimer?.invalidate()
        statusTimer = nil
        followPlayback(false)
        thumbnailTask?.cancel()
        started = false
        let native = engine, captures = mediaQueue
        lifecycleQueue.async { [weak self] in
            // Suspend cancels a cold capture. Drain the queue before deleting
            // the native host; neither the AI wait nor shutdown blocks UIKit.
            native.suspend()
            captures.sync {}
            native.stop()
            DispatchQueue.main.async {
                guard let self else { return }
                self.stopping = false
                if self.restartAfterStop { self.restartAfterStop = false; self.start() }
            }
        }
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
        // Detail is evaluated at the native playhead. The optimistic timeline
        // position must not consume this change before a queued seek arrives.
        let nativePlayhead = out.playhead
        let playheadChanged = nativePlayhead != lastEnginePlayhead
        lastEnginePlayhead = nativePlayhead
        refreshPreviewBufferRanges()
        if let pending = pendingPlayhead {
            if out.playhead == pending || ProcessInfo.processInfo.systemUptime >= pendingPlayheadUntil { pendingPlayhead = nil }
            else { out.playhead = pending }
        }
        if out.modelRevision != lastRevision {
            lastAutosaveActivity = ProcessInfo.processInfo.systemUptime
        }
        if playheadClock.frame != out.playhead { playheadClock.frame = out.playhead }
        followPlayback(out.playing != 0)
        // Só publica quando algo que a UI MOSTRA mudou: publicar a 5 Hz sem
        // filtrar redesenha a árvore inteira por nada (a FPS do painel DEV
        // muda sempre, e é para isso que existe o `showPerf`).
        if !statusEquals(out) {
            status = out
        }
        if out.modelRevision != lastRevision {
            lastRevision = out.modelRevision
            refreshModel(force: true)
        } else if playheadChanged && primarySelection != nil {
            refreshSelectedLayer(refreshEffectStack: false)
        }
        if out.thumbnailGeneration != lastThumbGeneration {
            lastThumbGeneration = out.thumbnailGeneration
        }
        if showPerf { perf = engine.perf() }
        checkMemoryPressure()
        autosaveIfIdle()
    }

    private func autosaveIfIdle() {
        let now = ProcessInfo.processInfo.systemUptime
        guard status.dirty != 0 else { unsavedSince = 0; return }
        if unsavedSince == 0 { unsavedSince = now }
        guard screen == .editor, status.dirty != 0, status.playing == 0,
              !autosaving, !importingMedia, !exporting,
              (now - lastAutosaveActivity >= 3 || now - unsavedSince >= 30), now >= autosaveRetryAfter,
              let url = projectURL else { return }
        _ = engine.flush()
        autosaving = true
        let saver = engine
        autosaveQueue.async { [weak self] in
            let saved = saver.autosaveProject()
            // O `fileExists` também fora da main (disco lento em aparelho de entrada).
            let missingCard = saved && !FileManager.default.fileExists(atPath: homeMetaURL(url).path)
            DispatchQueue.main.async {
                guard let self else { return }
                self.autosaving = false
                guard self.projectURL == url else { return }
                if saved {
                    self.homeCardStale = true
                    // Projeto novo ainda sem ficha: o app morto antes de sair do
                    // editor deixava o cartão da Home sem medida nem capa.
                    if missingCard {
                        self.writeProjectThumbnail(name: url.deletingPathExtension().lastPathComponent)
                    }
                    self.autosaveRetryAfter = 0
                    self.unsavedSince = ProcessInfo.processInfo.systemUptime
                    self.autosaveFailureShown = false
                } else {
                    self.autosaveRetryAfter = ProcessInfo.processInfo.systemUptime + 30
                    if !self.autosaveFailureShown { self.toast = AureaText.t("ios_save_failed") }
                    self.autosaveFailureShown = true
                }
            }
        }
    }

    private func statusEquals(_ other: AureaStatus) -> Bool {
        let a = status
        return a.playhead == other.playhead
            && a.modelRevision == other.modelRevision
            && a.thumbnailGeneration == other.thumbnailGeneration
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
            && a.previewBufferStatus == other.previewBufferStatus
            && abs(a.currentFps - other.currentFps) < 0.5
            && a.state == other.state
    }

    /// Relê o modelo que as telas desenham. Barato o bastante para rodar a cada
    /// mudança de revisão (é UMA travessia por lista, ver Engine::query_*).
    func refreshModel(force: Bool = false) {
        guard started else { return }
        refreshPreviewBufferRanges()
        let nextEditMode = engine.timelineEditMode
        if editMode != nextEditMode { editMode = nextEditMode }
        // Desfazer/refazer, abrir projeto e ripple também mexem nas marcas: a
        // lista local não pode ficar velha (a âncora acendia numa marca extinta).
        refreshMarkers()
        let rows = engine.layers()
        // Cada atribuição a um @Published redesenha TODA tela que observa o
        // modelo (a timeline inteira, o preview por cima): só publica o que
        // mudou de verdade. Era o grosso da timeline lenta no iOS.
        let nextLayers = rows.map { row in
            LayerItem(id: (row["id"] as? NSNumber)?.int64Value ?? 0,
                      kind: (row["kind"] as? NSNumber)?.uint32Value ?? 0,
                      name: row["name"] as? String ?? "",
                      startFrame: (row["startFrame"] as? NSNumber)?.int32Value ?? 0,
                      endFrame: (row["endFrame"] as? NSNumber)?.int32Value ?? 0,
                      offsetFrames: (row["offsetFrames"] as? NSNumber)?.int32Value ?? 0,
                      parentIndex: (row["parentIndex"] as? NSNumber)?.int32Value ?? -1,
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
                      guide: ((row["flags"] as? NSNumber)?.uint32Value ?? 0) & (1 << 7) != 0,
                      magnetic: ((row["flags"] as? NSNumber)?.uint32Value ?? 0) & (1 << 13) != 0,
                      trackId: (row["trackId"] as? NSNumber)?.uint32Value ?? 0)
        }
        if nextLayers != layers { layers = nextLayers }
        resolvePendingSplit()
        if let data = engine.captionTracks().data(using: .utf8), let tracks = try? JSONDecoder().decode([NativeCaptionTrack].self, from: data), tracks != captionTracks { captionTracks = tracks }
        let nextSelection = Set(layers.filter(\.selected).map(\.id))
        if nextSelection != selection { selection = nextSelection }

        // Keyframes: uma travessia para a composição inteira.
        var byLayer: [Int64: [KeyframeItem]] = [:]
        for entry in engine.allKeyframes() {
            guard let layerId = (entry["layerId"] as? NSNumber)?.int64Value,
                  let keys = entry["keys"] as? [[String: Any]] else { continue }
            byLayer[layerId] = keys.map { key in
                KeyframeItem(property: (key["property"] as? NSNumber)?.uint32Value ?? 0,
                             paramIndex: (key["paramIndex"] as? NSNumber)?.uint32Value ?? 0,
                             effectIndex: (key["effectIndex"] as? NSNumber)?.uint32Value ?? 0,
                             time: (key["time"] as? NSNumber)?.int32Value ?? 0,
                             value: (key["value"] as? NSNumber)?.floatValue ?? 0,
                             interpolation: (key["interpolation"] as? NSNumber)?.uint32Value ?? 0)
            }
        }
        if byLayer != keyframes { keyframes = byLayer }
        validateTimelineKeySelection()
        let nextComposition = engine.composition() ?? [:]
        if !NSDictionary(dictionary: nextComposition).isEqual(to: composition) { composition = nextComposition }
        if dirty != (status.dirty != 0) { dirty = status.dirty != 0 }
        // O catálogo de efeitos é fixo na sessão: lido uma vez, não a cada revisão.
        if effectCatalog.isEmpty {
            effectCatalog = engine.effectCatalog().map { row in
                EffectCatalogItem(effectClass: (row["effectClass"] as? NSNumber)?.uint32Value ?? 0, typeId: (row["typeId"] as? NSNumber)?.uint32Value ?? 0,
                                  name: row["name"] as? String ?? "",
                                  category: row["category"] as? String ?? "",
                                  paramCount: (row["paramCount"] as? NSNumber)?.uint32Value ?? 0)
            }
        }
        refreshSelectedLayer()
    }

    /// O que depende da camada escolhida: efeitos, parâmetros e o inspetor.
    func refreshSelectedLayer(refreshEffectStack: Bool = true) {
        guard let layerId = primarySelection, started else {
            if !effects.isEmpty { effects = [] }
            if !effectParams.isEmpty { effectParams = [] }
            if !detail.isEmpty { detail = [:] }
            if !cameraLens.isEmpty { cameraLens = [] }
            return
        }
        if refreshEffectStack {
          let nextEffects = engine.effects(forLayer: layerId).map { row in
            EffectItem(effectId: (row["effectId"] as? NSNumber)?.uint32Value ?? 0,
                       typeId: (row["typeId"] as? NSNumber)?.uint32Value ?? 0,
                       name: row["name"] as? String ?? "",
                       enabled: row["enabled"] as? Bool ?? true,
                       paramCount: (row["paramCount"] as? NSNumber)?.uint32Value ?? 0,
                       known: row["known"] as? Bool ?? true)
        }
          if nextEffects != effects { effects = nextEffects }
        }
        let nextDetail = engine.layerDetail(layerId) ?? [:]
        if !NSDictionary(dictionary: nextDetail).isEqual(to: detail) { detail = nextDetail }
        let nextLens: [Float] = selectedLayer?.kind == 8 ? engine.cameraLens(layerId).map(\.floatValue) : []
        if nextLens != cameraLens { cameraLens = nextLens }
        if panel == .mask || panel == .vector { refreshMasks() }
        if let effectId = selectedEffectId {
            loadParams(layerId: layerId, effectId: effectId)
        }
    }

    func loadParams(layerId: Int64, effectId: UInt32) {
        if selectedEffectId != effectId { selectedEffectId = effectId }
        let typeId = effects.first { $0.effectId == effectId }?.typeId ?? 0
        let nextParams = engine.effectParams(forLayer: layerId, effectId: effectId).map { row in
            let value = (row["value"] as? [NSNumber])?.map(\.floatValue) ?? [0, 0, 0, 0]
            let def = (row["defaultValue"] as? [NSNumber])?.map(\.floatValue) ?? [0, 0, 0, 0]
            let lo: Float = (row["min"] as? NSNumber)?.floatValue ?? 0
            let hi: Float = (row["max"] as? NSNumber)?.floatValue ?? 1
            // Sem a chave (ponte antiga) ou NaN: a faixa digitada é a do slider, nunca mais estreita.
            let rawHardMin: Float = (row["hardMin"] as? NSNumber)?.floatValue ?? lo
            let rawHardMax: Float = (row["hardMax"] as? NSNumber)?.floatValue ?? hi
            let hardLo: Float = rawHardMin.isNaN ? lo : Swift.min(rawHardMin, lo)
            let hardHi: Float = rawHardMax.isNaN ? hi : Swift.max(rawHardMax, hi)
            // O motor fala pt-BR: rótulo, opções e unidade saem traduzidos pela identidade.
            let index = (row["index"] as? NSNumber)?.uint32Value ?? 0
            let text = fxLocalizedParam(typeId, index: Int(index), label: row["label"] as? String ?? "", unit: row["unit"] as? String ?? "",
                                        options: row["enumLabels"] as? [String] ?? [])
            return EffectParamItem(flags: (row["flags"] as? NSNumber)?.uint32Value ?? 0, index: index,
                                   type: (row["type"] as? NSNumber)?.uint32Value ?? 0,
                                   label: text.label,
                                   unit: text.unit,
                                   paramId: row["id"] as? String ?? "",
                                   value: value,
                                   defaultValue: def,
                                   minValue: lo,
                                   maxValue: hi,
                                   hardMin: hardLo,
                                   hardMax: hardHi,
                                   animated: row["animated"] as? Bool ?? false,
                                   enumLabels: text.options)
        }
        if nextParams != effectParams { effectParams = nextParams }
    }

    @Published var selectedEffectId: UInt32?
    struct EffectFocusRequest {
        let layer: Int64
        let type: UInt32
        let previous: Set<UInt32>
    }
    var pendingEffectFocus: EffectFocusRequest?
    var requestedEffectFocusId: UInt32? {
        guard let request = pendingEffectFocus, request.layer == primarySelection else { return nil }
        return effects.last { $0.typeId == request.type && !request.previous.contains($0.effectId) }?.effectId
    }
    func addEffectAndFocus(_ type: UInt32, layer: Int64) {
        let previous = Set(effects.map(\.effectId))
        pendingEffectFocus = EffectFocusRequest(layer: layer, type: type, previous: previous)
        seenEffectIds[layer] = previous
        engine.run { $0.addEffect(type, toLayer: layer, at: UInt32.max) }
    }

    // --- Fantoche no palco (PuppetStage.swift; espelho de EditorStore.puppet*) ---
    func puppetPins(_ layer: Int64, effect: Int32) -> [Float] { engine.puppetPins(layer, effect: effect).map(\.floatValue) }
    func puppetMesh(_ layer: Int64, effect: Int32) -> [Float] { engine.puppetMesh(layer, effect: effect).map(\.floatValue) }
    func puppetAddPin(_ layer: Int64, effect: Int32, u: Float, v: Float) -> Int32 {
        if status.playing != 0 { playPause() }
        let pin = engine.puppetAddPin(layer, effect: effect, u: u, v: v)
        if pin >= 0 { refreshModel(force: true) }
        return pin
    }
    /// Arrasto ao vivo; `continuing` = mesmo gesto (um passo de desfazer); auto-key do palco.
    func puppetMovePin(_ layer: Int64, effect: Int32, pin: Int32, u: Float, v: Float, continuing: Bool) -> Bool {
        if status.playing != 0 { playPause() }
        return engine.puppetMovePin(layer, effect: effect, pin: pin, u: u, v: v, autoKey: autoKeyTransforms, continuing: continuing)
    }
    func puppetRemovePin(_ layer: Int64, effect: Int32, pin: Int32) {
        if engine.puppetRemovePin(layer, effect: effect, pin: pin) { refreshModel(force: true) }
    }
    /// A ferramenta Fantoche (a entrada "Rig" da doca): abre o cartão do efeito
    /// na camada escolhida — cria se ainda não tem — já no modo de pinos.
    func openPuppetTool() {
        guard let layer = primarySelection else { return }
        let type = PuppetStageState.type
        PuppetStageState.shared.pendingEdit = layer
        if effects.contains(where: { $0.typeId == type }) {
            pendingEffectFocus = EffectFocusRequest(layer: layer, type: type, previous: [])
        } else {
            addEffectAndFocus(type, layer: layer)
        }
        openPanel(.effects)
    }

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

    // Desfazer/refazer podem mover ou apagar os keyframes escolhidos: a seleção da timeline some.
    func undo() { clearTimelineKeySelection(); engine.run { $0.undo() }; syncAfterEdit(); restoreTrackingAfterHistory() }
    func redo() { clearTimelineKeySelection(); engine.run { $0.redo() }; syncAfterEdit(); restoreTrackingAfterHistory() }
    private func restoreTrackingAfterHistory() {
        guard let id = primarySelection else { return }
        if !cameraFeatures.isEmpty {
            if engine.restoreCameraTrack(forLayer: id) { cameraSelectedCount = 0; refreshCameraTrackPoints() }
            else { cameraFeatures = []; cameraTarget = [] }
        }
        if engine.restoreMotionTrack(id) { motionSource = id; motionStatus = engine.motionTrackStatus() }
    }

    func playPause() {
        pendingPlayhead = nil
        engine.run { $0.togglePlayback() }
        status.playing = status.playing == 0 ? 1 : 0
        followPlayback(status.playing != 0)
    }

    /// Liga/desliga o relógio da timeline (um CADisplayLink só enquanto toca).
    private func followPlayback(_ playing: Bool) {
        if playing {
            guard playheadLink == nil else { return }
            let link = CADisplayLink(target: DisplayLinkProxy { [weak self] in self?.playheadTick() },
                                     selector: #selector(DisplayLinkProxy.step(_:)))
            link.add(to: .main, forMode: .common)
            playheadLink = link
        } else {
            playheadLink?.invalidate()
            playheadLink = nil
        }
    }

    /// Um quadro da tela: só o cabeçote do motor vai para a timeline.
    private func playheadTick() {
        guard started else { followPlayback(false); return }
        var out = AureaStatus()
        guard engine.readStatus(&out) else { return }
        if playheadClock.frame != out.playhead { playheadClock.frame = out.playhead }
        if out.playing == 0 {
            followPlayback(false)
            refreshStatus()
        }
    }

    func seek(toFrame frame: Int64) {
        pendingPlayhead = frame
        pendingPlayheadUntil = ProcessInfo.processInfo.systemUptime + 2
        engine.run { $0.seek(toFrame: frame) }; status.playhead = frame
        playheadClock.frame = frame
        if primarySelection != nil { refreshSelectedLayer() }
    }
    func step(_ frames: Int32) { pendingPlayhead = nil; engine.run { $0.stepFrames(frames) } }
    func setLoop(_ on: Bool) { engine.run { $0.setLoop(on) } }
    func setSpeed(_ speed: Float) { engine.run { $0.setPlaybackSpeed(speed) } }
    func setPreviewScale(num: UInt32, den: UInt32, auto: Bool) {
        engine.run { $0.setPreviewScaleNumerator(num, denominator: den, automatic: auto) }
    }
    func toggleMarker() { engine.run { $0.toggleMarker(Int64(status.playhead)) } }
    /// O playhead enquanto o dedo arrasta a régua. É otimismo LOCAL e curto: o
    /// valor do MOTOR volta no próximo `fill_status` e o substitui. Sem isto, o
    /// playhead só andaria a cada 200 ms e o scrub pareceria travado.
    /// Scrub LEVE da timeline (pinça parada, auto-rolagem na borda): o motor e o
    /// relógio do cabeçote andam a cada passo, mas o `status` publicado e a
    /// releitura dos painéis ficam para o fim do gesto (`optimisticPlayhead`).
    /// Publicar 60 vezes por segundo redesenhava o app inteiro e a pinça
    /// engasgava. Exige `scrubBegin` aberto.
    func timelineScrub(_ frame: Int64) {
        pendingPlayhead = frame
        pendingPlayheadUntil = ProcessInfo.processInfo.systemUptime + 2
        engine.run { $0.scrub(toFrame: frame) }
        if playheadClock.frame != frame { playheadClock.frame = frame }
    }
    func optimisticPlayhead(_ frame: Int64) {
        pendingPlayhead = frame
        pendingPlayheadUntil = ProcessInfo.processInfo.systemUptime + 2
        guard status.playhead != frame else { return }
        status.playhead = frame
        playheadClock.frame = frame
        if primarySelection != nil { refreshSelectedLayer() }
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
        let ids = layers.filter { selection.contains($0.id) }.map { NSNumber(value: $0.id) }
        let created = engine.precomposeLayers(ids, name: nil)
        if created < 0 {
            toast = AureaText.t("msg_nao_foi_possivel_agrupar_erro", String(-created))
            return
        }
        selection = [created]
        engine.selectLayers([NSNumber(value: created)])
        refreshModel(force: true)
    }

    /// Alternar grupo (app antigo): um grupo escolhido desagrupa; senão as escolhidas viram grupo.
    func toggleGroup() {
        if selection.count == 1, let only = selection.first, layers.first(where: { $0.id == only })?.kind == 12 { ungroup(only) }
        else { groupSelection() }
    }

    func ungroup(_ layerId: Int64) {
        let why = engine.ungroupPrecomp(layerId)
        if !why.isEmpty { toast = AureaText.t("msg_nao_da_para_desagrupar_o_resultado", AureaEngineText.reason(why)) }
        refreshModel(force: true)
    }

    /// Renomeia o projeto ABERTO (arquivo + capa), mantendo o editor nele.
    func renameCurrentProject(to newName: String) {
        guard let url = projectURL, url.deletingPathExtension().lastPathComponent != newName else { return }
        rename(ProjectFile(url: url, name: projectName, modified: Date(), sizeBytes: 0), to: newName)
    }

    @Published private(set) var timelineOnlySelection: Set<Int64> = []

    func select(layerId: Int64, additive: Bool = false, openOptions: Bool = true) {
        let previousPrimary = primarySelection
        // A seleção de keyframes é de UMA camada: trocar a principal a descarta.
        if let keys = timelineKeySelection, additive || keys.layer != layerId { clearTimelineKeySelection() }
        if primarySelection != layerId { selectedMask = nil; selectedMaskPoint = nil; maskDrawing = false; pointPick = nil; focusPick = false; freehandPoints = []; panel = .none }
        if additive {
            var next = selection
            if next.contains(layerId) { next.remove(layerId) } else { next.insert(layerId) }
            engine.run { $0.selectLayers(next.map { NSNumber(value: $0) }) }
            selection = next
        } else {
            engine.run { $0.selectLayers([NSNumber(value: layerId)]) }
            selection = [layerId]
        }
        // Tocar de novo na MESMA camada (no palco, na timeline) não fecha o
        // efeito aberto: o cartão continuava na tela mas parava de reler os
        // valores, e mexer nele parecia não fazer nada (Text Transform no iOS).
        if previousPrimary != layerId || selection.count != 1 { selectedEffectId = nil }
        timelineOnlySelection = openOptions ? [] : selection
        if selection.count != 1 { panel = .none }
        refreshSelectedLayer()
    }

    func clearSelection() {
        timelineOnlySelection = []
        clearTimelineKeySelection()
        selectedMask = nil; selectedMaskPoint = nil; maskDrawing = false; masks = []
        engine.run { $0.clearSelection() }
        selection = []
        panel = .none
        selectedEffectId = nil
        refreshSelectedLayer()
    }

    func openAddLayer() {
        if status.playing != 0 { engine.run { $0.pause() }; status.playing = 0 }
        panel = .none
        showAddLayer = true
    }

    func openPanel(_ target: PanelKind) {
        timelineOnlySelection = []
        if status.playing != 0 { engine.run { $0.pause() }; status.playing = 0 }
        showAddLayer = false
        panel = target
        if target == .mask { refreshMasks() }
        if target == .vector { vectorGroup = 0; vectorPath = 0; vectorFreehand = false; vectorPointTool = 0; vectorEditingPoints = false; refreshMasks() }
    }

    func refreshMasks() {
        guard let id = primarySelection else { masks = []; return }
        if panel == .vector {
            let data = engine.vectorPath(id, group: vectorGroup, path: vectorPath).map(\.floatValue)
            guard data.count >= 9, data[8] >= 0, Int(data[8]) * 6 + 9 == data.count else { masks = []; return }
            maskAffine = Array(data.prefix(6))
            masks = [MaskItem(id: 0, operation: 0, inverted: false, feather: 0, expansion: 0, opacity: 1, closed: data[7] > 0.5, keyed: Int(data[6]) & 4 != 0, points: Array(data.dropFirst(9)))]
            selectedMask = 0
            return
        }
        let data = engine.maskData(id).map(\.floatValue)
        guard data.count >= 7 else { masks = []; return }
        maskAffine = Array(data.prefix(6))
        var next: [MaskItem] = [], offset = 7
        for _ in 0..<Int(data[6]) {
            guard offset + 12 <= data.count else { break }
            let h = Array(data[offset..<(offset + 12)]), count = Int(data[offset + 7]) * 6
            offset += 12
            guard count >= 0, offset + count <= data.count else { break }
            next.append(MaskItem(id: UInt32(h[0]), operation: UInt32(h[1]), inverted: h[2] > 0.5,
                                 feather: h[3], expansion: h[4], opacity: h[5], closed: h[6] > 0.5, keyed: h[9] > 0.5,
                                 points: Array(data[offset..<(offset + count)]), keyCount: Int(h[8])))
            offset += count
        }
        masks = next
        if let selectedMask, !next.contains(where: { $0.id == selectedMask }) { self.selectedMask = nil; maskDrawing = false; selectedMaskPoint = nil }
    }

    var editingMask: MaskItem? { masks.first { $0.id == selectedMask } }

    func setMaskPoints(_ points: [Float], closed: Bool, undo: Bool = true) {
        guard let id = primarySelection, let mask = selectedMask else { return }
        if panel == .vector {
            let values = [closed ? Float(1) : 0, Float(points.count / 6)] + points
            _ = engine.setVectorPath(id, group: vectorGroup, path: vectorPath, values: values.map { NSNumber(value: $0) }, continuing: !undo)
            refreshMasks(); return
        }
        _ = engine.setMaskPath(id, mask: mask, points: points.map { NSNumber(value: $0) }, closed: closed, undo: undo)
        refreshMasks()
    }

    func addMask(_ shape: Int) {
        guard let id = primarySelection else { return }
        let source = (detail["sourceSize"] as? [NSNumber] ?? []).map(\.floatValue)
        let w = max(1, source.first ?? Float(compositionWidth)), h = max(1, source.count > 1 ? source[1] : Float(compositionHeight))
        let cx = w / 2, cy = h / 2, rx = w * 0.35, ry = h * 0.35
        let points: [Float]
        if shape == 0 { points = [cx-rx, cy-ry, 0,0,0,0, cx+rx, cy-ry, 0,0,0,0, cx+rx, cy+ry, 0,0,0,0, cx-rx, cy+ry, 0,0,0,0] }
        else if shape == 1 {
            let kx = rx * 0.5523, ky = ry * 0.5523
            points = [cx,cy-ry,-kx,0,kx,0, cx+rx,cy,0,-ky,0,ky, cx,cy+ry,kx,0,-kx,0, cx-rx,cy,0,ky,0,-ky]
        } else { points = [] }
        let mask = engine.addMask(id, points: points.map { NSNumber(value: $0) }, closed: shape != 2)
        guard mask >= 0 else { toast = AureaText.t("ios_mask_add_failed"); return }
        refreshModel(force: true); refreshMasks(); selectedMask = UInt32(mask); maskDrawing = shape == 2; selectedMaskPoint = nil
    }

    /// Same back order as EditorScreen.shellBack on Android.
    func editorBack() {
        if showAddLayer { showAddLayer = false }
        else if fullscreen { fullscreen = false }
        else if panel != .none && panel != .dock { panel = .none }
        else if !selection.isEmpty { clearSelection() }
        else if engine.precompDepth > 0 { _ = engine.closePrecomp(); refreshModel(force: true) }
        else { closeProject() }
    }

    func editorBackFromTimeline() {
        if panel != .none && panel != .dock { panel = .none }
        else { clearSelection() }
    }

    var localPlayhead: Int32 {
        localFrame(for: primarySelection)
    }

    func localFrame(for id: Int64?) -> Int32 {
        guard let layer = layers.first(where: { $0.id == id }) else { return Int32(clamping: status.playhead) }
        return Int32(clamping: status.playhead - Int64(layer.startFrame) + Int64(layer.offsetFrames))
    }

    func keyProperty(_ property: UInt32, value: Float) {
        guard let id = primarySelection else { return }
        mutate { $0.insertKeyframe(forLayer: id, property: property, time: localPlayhead, value: value) }
        refreshModel(force: true)
    }

    /// A property with a track is edited at the playhead, as on Android.
    /// Writing only its base transform would be hidden by the existing track.
    func editTransform(_ property: UInt32, value: Float) {
        guard let id = primarySelection, value.isFinite, !(selectedLayer?.locked ?? false) else { return }
        if let d = engine.layerDetail(id), keyTransformGroup(id, detail: d, changes: [property: value]) { return }
        let mask = (detail["animatedMask"] as? NSNumber)?.uint32Value ?? 0
        let keyed = property < 32 && mask & (1 << property) != 0
        // Trilha animada com Auto-Key marca keyframe TAMBÉM na cena 3D (como no Android).
        if property < 15 && transformLayout(animated: keyed) {
            mutate { $0.layoutTransform(id, property: property, value: value) }
            refreshModel(force: true); return
        }
        if property < 15 {
            // Auto-Key ligado: o motor decide pela trilha viva (o `detail`
            // publicado pode estar um quadro atrás).
            mutate { $0.gestureKeyframe(forLayer: id, property: property, value: value, wholeGroup: false) }
            refreshModel(force: true)
            return
        }
        func vector(_ key: String) -> [Float] { (detail[key] as? [NSNumber] ?? []).map(\.floatValue) }
        mutate { engine in
            if property == 12 { engine.setOpacity(forLayer: id, value: value) }
            else if property >= 13 && property <= 14 {
                var v = vector("skew"); guard v.count >= 2 else { return }
                v[Int(property - 13)] = value; engine.setSkew(forLayer: id, x: v[0], y: v[1])
            } else if property < 12 {
                let group = Int(property / 3), axis = Int(property % 3)
                var v = vector(["position", "scale", "rotation", "anchor"][group]); guard v.count >= 3 else { return }
                v[axis] = value
                switch group {
                case 0: engine.setPosition(forLayer: id, x: v[0], y: v[1], z: v[2])
                case 1: engine.setScale(forLayer: id, x: v[0], y: v[1], z: v[2])
                case 2: engine.setRotation(forLayer: id, x: v[0], y: v[1], z: v[2])
                default: engine.setAnchor(forLayer: id, x: v[0], y: v[1], z: v[2])
                }
            }
        }
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
    @discardableResult
    func newProject(ratio: Double, shortSide: UInt32, fps: Double, title: String) -> Bool {
        guard ratio.isFinite, ratio > 0,
              Double(shortSide) * max(ratio, 1 / ratio) < Double(UInt32.max) else {
            toast = AureaText.t("msg_nao_foi_possivel_criar_o_projeto")
            return false
        }
        let width: UInt32 = ratio >= 1 ? UInt32((Double(shortSide) * ratio).rounded()) : shortSide
        let height: UInt32 = ratio >= 1 ? shortSide : UInt32((Double(shortSide) / ratio).rounded())
        return newProject(width: width, height: height, fps: fps, title: title)
    }

    /// `fps` livre (1–240; o núcleo encaixa 29,97 → 30000/1001). `background`
    /// (RGB sRGB) é o fundo com que a composição nasce, fora do histórico;
    /// nil = preto, como sempre (`home/ProjectMenu.kt`).
    @discardableResult
    func newProject(width: UInt32, height: UInt32, fps: Double, title: String, background: [Float]? = nil) -> Bool {
        guard canChangeProject() else { return false }
        guard started else {
            toast = startError ?? AureaText.t("engine_not_ready")
            return false
        }
        let trimmed = title.trimmingCharacters(in: .whitespacesAndNewlines)
        let name = trimmed.isEmpty ? AureaText.t("project_new", projects.count + 1) : trimmed
        let created: Bool
        if let bg = background, bg.count >= 3 {
            created = engine.newProjectWidth(width, height: height, fps: fps, title: name,
                                             backgroundR: bg[0], g: bg[1], b: bg[2],
                                             a: bg.count > 3 && bg[3] < 0.5 ? 0 : 1)   // alfa 0 = "Transparente"
        } else {
            created = engine.newProjectWidth(width, height: height, fps: fps, title: name)
        }
        guard created else {
            toast = AureaText.t("msg_nao_foi_possivel_criar_o_projeto")
            return false
        }
        let url = uniqueProjectURL(name)
        // The engine already owns the new document. A failed first save must
        // never leave the previous project's path attached to that document.
        projectURL = url
        projectName = name
        projectGeneration = UUID()
        projectContentGeneration = UUID()
        missingModelTextures = nil; texturesTarget = nil
        mediaCreationRequest = UUID()
        guard engine.saveProject(url.path) else {
            toast = AureaText.t("ios_project_save_failed")
            return false
        }
        refreshProjectList()
        refreshModel(force: true)
        enterEditor()
        return true
    }

    func open(_ project: ProjectFile) {
        guard canChangeProject() else { return }
        mediaCreationRequest = UUID()
        openingProject = true
        defer { openingProject = false }
        guard engine.loadProject(project.url.path) else {
            toast = AureaText.t("msg_nao_foi_possivel_abrir_o_projeto", "")
            return
        }
        projectURL = project.url
        projectName = project.name
        projectGeneration = UUID()
        projectContentGeneration = UUID()
        missingModelTextures = nil; texturesTarget = nil
        let notice = engine.lastLoadNotice
        if notice != 0 {
            // A UI AVISA em vez de esconder (o mesmo critério do Android: §55,
            // §120, §124).
            var parts: [String] = []
            if notice & 1 != 0 { parts.append(AureaText.t("ios_load_notice_recovery")) }
            if notice & 2 != 0 { parts.append(AureaText.t("ios_load_notice_missing_sections")) }
            if notice & 4 != 0 { parts.append(AureaText.t("ios_load_notice_old_format")) }
            if notice & 8 != 0 { parts.append(AureaText.t("ios_load_notice_missing_media", Int(engine.lastLoadMissingAssets))) }
            toast = parts.joined(separator: " · ")
        }
        refreshModel(force: true)
        enterEditor()
    }

    func enterEditor() {
        timelineOnlySelection = []
        timelineLayerSelectMode = false
        panel = .none
        screen = .editor
    }

    func closeProject() {
        guard canChangeProject() else { return }
        mediaCreationRequest = UUID()
        textContentRequest = nil
        if sceneEditor { exitSceneEditor() }
        // Antes da gravação (ela drena a fila): o áudio seguia tocando na Home.
        stopPlayback()
        guard saveOnLeave(forceThumbnail: true) else { return }
        projectGeneration = UUID()
        maskToolLayers = []
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
            toast = AureaText.t("ios_save_failed")
            return false
        }
        if writeThumbnail {
            writeProjectThumbnail(name: url.deletingPathExtension().lastPathComponent)
            homeCardStale = false
        } else {
            homeCardStale = true
        }
        dirty = false
        return true
    }

    /// A capa do projeto na Home. É o MESMO caminho do Android: o motor
    /// renderiza o quadro do cabeçote (`capture_frame_rgba`) e o arquivo vai
    /// para `Thumbs/<nome>.jpg`. Isto é uma MINIATURA — o preview continua indo
    /// direto para o CAMetalLayer, sem bitmap nenhum no meio.
    @discardableResult
    private func writeProjectThumbnail(name: String) -> Task<Void, Never> {
        let project = projectContentGeneration, sourceURL = projectURL
        let request = UUID()
        thumbnailRequest = request
        thumbnailTask?.cancel()
        let task = Task { @MainActor [weak self] in
            guard let self, !Task.isCancelled,
                  self.projectContentGeneration == project, self.projectURL == sourceURL else { return }
            guard let frame = await self.capturePreviewFrame(720) else {
                if self.thumbnailRequest == request, self.projectContentGeneration == project {
                    self.homeCardStale = true
                }
                return
            }
            let jpeg: Data? = await withCheckedContinuation { continuation in
                self.mediaQueue.async {
                    let image = UIImage.fromRGBA(frame.data, width: Int(frame.width), height: Int(frame.height))
                    continuation.resume(returning: image?.jpegData(compressionQuality: 0.82))
                }
            }
            // Publish only the current request. This also protects A -> B -> A
            // when the first capture was queued before either project switch.
            guard !Task.isCancelled, self.thumbnailRequest == request,
                  self.projectContentGeneration == project, self.projectURL == sourceURL else { return }
            guard let jpeg else { self.homeCardStale = true; return }
            let url = AureaPaths.thumbs.appendingPathComponent(name + ".jpg")
            do {
                try jpeg.write(to: url, options: .atomic)
                self.homeCardStale = false
                if self.screen == .home { self.refreshProjectList() }
            } catch { self.homeCardStale = true }
        }
        thumbnailTask = task
        return task
    }

    /// Exact capture can wait for local AI. Keep that wait off the main actor;
    /// retain the bridge until it returns; stop drains this queue before teardown.
    func capturePreviewFrame(_ maxDimension: UInt32) async -> (data: Data, width: UInt32, height: UInt32)? {
        guard started, !Task.isCancelled else { return nil }
        let native = engine, project = projectContentGeneration, sourceURL = projectURL
        let frame: (data: Data, width: UInt32, height: UInt32)? = await withCheckedContinuation { continuation in
            mediaQueue.async {
                var width: UInt32 = 0, height: UInt32 = 0
                guard let data = native.captureFrame(maxDimension, outWidth: &width, outHeight: &height),
                      width > 0, height > 0 else { continuation.resume(returning: nil); return }
                continuation.resume(returning: (data, width, height))
            }
        }
        guard started, !Task.isCancelled, projectContentGeneration == project, projectURL == sourceURL else { return nil }
        return frame
    }

    private func uniqueProjectURL(_ name: String) -> URL {
        let invalid = CharacterSet(charactersIn: "/\\:").union(.controlCharacters)
        let safe = name.components(separatedBy: invalid).joined(separator: "-")
        var url = AureaPaths.documents.appendingPathComponent(safe + ".aurea")
        var counter = 1
        while FileManager.default.fileExists(atPath: url.path) {
            url = AureaPaths.documents.appendingPathComponent("\(safe) \(counter).aurea")
            counter += 1
        }
        return url
    }

    /// `.aurea.bak`, `.aurea.tmp`, `.aurea.corrompido`, `.aurea.vN.bak`: as
    /// cópias de recuperação do motor andam com o projeto (um projeto novo com o
    /// mesmo nome nunca pode "recuperar" a cópia de outro).
    private func recoveryFamily(of url: URL) -> [URL] {
        let prefix = url.lastPathComponent + "."
        let siblings = (try? FileManager.default.contentsOfDirectory(at: url.deletingLastPathComponent(),
                                                                      includingPropertiesForKeys: nil)) ?? []
        return siblings.filter { $0.lastPathComponent.hasPrefix(prefix) }
    }

    func delete(_ project: ProjectFile) {
        let family = recoveryFamily(of: project.url)
        try? FileManager.default.removeItem(at: project.url)
        for url in family { try? FileManager.default.removeItem(at: url) }
        try? FileManager.default.removeItem(at: project.thumbnailURL)
        refreshProjectList()
    }

    func rename(_ project: ProjectFile, to newName: String) {
        let invalid = CharacterSet(charactersIn: "/\\:").union(.controlCharacters)
        let newName = newName.components(separatedBy: invalid).joined(separator: "-")
            .trimmingCharacters(in: .whitespacesAndNewlines)
        guard !newName.isEmpty else { return }
        let target = AureaPaths.documents.appendingPathComponent(newName + ".aurea")
        guard !FileManager.default.fileExists(atPath: target.path) else {
            toast = AureaText.t("ios_project_name_exists")
            return
        }
        let family = recoveryFamily(of: project.url)
        do { try FileManager.default.moveItem(at: project.url, to: target) } catch {
            toast = AureaText.t("msg_nao_foi_possivel_renomear", error.localizedDescription)
            return
        }
        for url in family {
            let suffix = String(url.lastPathComponent.dropFirst(project.url.lastPathComponent.count))
            try? FileManager.default.moveItem(at: url, to: target.deletingLastPathComponent()
                .appendingPathComponent(target.lastPathComponent + suffix))
        }
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
    private func canChangeProject() -> Bool {
        if exporting { toast = AureaText.t("editor_mantenha_aurea_aberto_ate_terminar"); return false }
        if importingMedia { toast = operationMessage; return false }
        if !projectOperations.isEmpty { toast = AureaText.t("ios_importing_media"); return false }
        if modelOptimize != nil { toast = AureaText.t("ios_importing_media"); return false }
        return true
    }

    /// Keep the source document active until a queued native media operation
    /// has returned, including after its view has requested cancellation.
    func beginProjectOperation() -> UUID {
        let token = UUID()
        projectOperations.insert(token)
        return token
    }

    func endProjectOperation(_ token: UUID) { projectOperations.remove(token) }
    /// Atalho da Home: primeiro cria a composição na proporção da mídia e só
    /// depois a importa. O importador do editor pressupõe um projeto aberto.
    func createFromMedia(url: URL, kind: ImportKind) {
        guard canChangeProject() else { return }
        let request = UUID()
        mediaCreationRequest = request
        let scoped = url.startAccessingSecurityScopedResource()
        Task { @MainActor in
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            var mediaWidth: CGFloat = 0
            var mediaHeight: CGFloat = 0
            switch kind {
            case .video:
                let asset = AVURLAsset(url: url)
                if let track = try? await asset.loadTracks(withMediaType: .video).first,
                   let size = try? await track.load(.naturalSize),
                   let transform = try? await track.load(.preferredTransform) {
                    let oriented = size.applying(transform)
                    mediaWidth = abs(oriented.width)
                    mediaHeight = abs(oriented.height)
                }
            case .image:
                if let source = CGImageSourceCreateWithURL(url as CFURL, nil),
                   let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any] {
                    mediaWidth = CGFloat((properties[kCGImagePropertyPixelWidth] as? NSNumber)?.intValue ?? 0)
                    mediaHeight = CGFloat((properties[kCGImagePropertyPixelHeight] as? NSNumber)?.intValue ?? 0)
                }
            default: break
            }
            guard !Task.isCancelled, mediaCreationRequest == request, canChangeProject() else { return }
            let ratio = mediaWidth.isFinite && mediaHeight.isFinite && mediaWidth > 0 && mediaHeight > 0
                ? min(5.0, max(0.2, Double(mediaWidth / mediaHeight))) : 9.0 / 16.0
            let width = UInt32((ratio >= 1 ? 1080.0 * ratio : 1080.0).rounded()) & ~UInt32(1)
            let height = UInt32((ratio >= 1 ? 1080.0 : 1080.0 / ratio).rounded()) & ~UInt32(1)
            let name = url.deletingPathExtension().lastPathComponent
            guard newProject(width: width, height: height, fps: 30, title: name) else { return }
            importMedia(url: url, kind: kind)
            _ = saveProject(writeThumbnail: false)
        }
    }

    /// Copia o arquivo escolhido para o sandbox (Media/) e importa. O motor
    /// guarda o caminho RELATIVO a Documents — é o que faz o mesmo .aurea
    /// abrir no Android e no iOS.
    /// `atPlayhead`: o clipe entra no cabeçote (o vídeo gerado pela IA), não no zero.
    func importMedia(url: URL, kind: ImportKind, objectHDRI: Int64? = nil, atPlayhead: Bool = false) {
        guard !importingMedia else { return }
        // Modelo 3D: o fluxo com orçamento de memória ("Otimizar modelo").
        if kind == .model { importModelFiles(urls: [url]); return }
        let performanceStart = ProcessInfo.processInfo.systemUptime
        IPhonePerformanceTest.shared.event("import_start", values: ["kind": String(describing: kind)])
        operationMessage = AureaText.t("ios_importing_media")
        importingMedia = true
        if status.playing != 0 { engine.run { $0.pause() }; status.playing = 0 }
        let scoped = url.startAccessingSecurityScopedResource()
        let destination = AureaPaths.mediaDestination(for: url.lastPathComponent)
        let name = url.deletingPathExtension().lastPathComponent
        let importer = engine
        mediaQueue.async { [weak self] in
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            var result: Int64 = -1
            var failure = ""
            do {
                try AureaPaths.copyImport(url, to: destination)
                switch kind {
                case .video: result = importer.importVideo(destination.path, name: name)
                case .audio: result = importer.importAudio(destination.path, name: name)
                case .image: result = importer.importImageFile(destination.path, name: name)
                case .model: result = importer.importModel(destination.path, name: name)
                case .hdri:
                    if let objectHDRI { result = importer.importObjectHDRI(destination.path, layer: objectHDRI) }
                    else { result = importer.importHdri(destination.path) }
                }
                if result < 0 {
                    // A frase do motor sai no idioma do app (AureaEngineText).
                    let raw = importer.lastImportError
                    if kind == .hdri, let reason = AureaModel.hdriFailure(code: -result) { failure = reason }
                    else { failure = raw.isEmpty ? AureaText.t("ios_import_failed_code", String(-result)) : AureaEngineText.sentence(raw, code: Int(-result)) }
                    try? FileManager.default.removeItem(at: destination)
                }
            } catch { failure = AureaText.t("ios_import_copy_failed", error.localizedDescription) }
            let importedId = result, importFailure = failure
            DispatchQueue.main.async {
                guard let self else { return }
                self.importingMedia = false
                IPhonePerformanceTest.shared.event("import_end", values: ["kind": String(describing: kind), "milliseconds": (ProcessInfo.processInfo.systemUptime - performanceStart) * 1000, "success": importFailure.isEmpty])
                if !importFailure.isEmpty { self.toast = importFailure; return }
                if kind == .hdri { self.toast = AureaText.t("eng_environment_imported") }
                else { self.engine.selectLayers([NSNumber(value: importedId)]); self.selection = [importedId] }
                self.refreshModel(force: true)
                if atPlayhead && kind != .hdri { self.moveToPlayhead(importedId) }
                _ = self.saveProject(writeThumbnail: false)
            }
        }
    }

    /// HDRI que não entrou: o código do motor (Errc) vira a frase do motivo —
    /// a mesma tabela do EditorStore.hdriErrorText no Android.
    static func hdriFailure(code: Int64) -> String? {
        switch code {
        case 3, 10: return AureaText.t("msg_hdri_err_unreadable")
        case 6, 16, 17: return AureaText.t("msg_hdri_err_format")
        case 11, 14: return AureaText.t("msg_hdri_err_corrupt")
        case 8, 9: return AureaText.t("msg_hdri_err_too_large")
        default: return nil
        }
    }

    /// "Importar texturas": a layer do modelo e os arquivos que ele referencia e não achou (só o nome).
    struct MissingModelTextures: Identifiable { let id = UUID(); let layer: Int64; let names: [String] }
    @Published var missingModelTextures: MissingModelTextures?
    /// O modelo do último pedido: o alerta fecha antes de o seletor devolver os arquivos.
    private var texturesTarget: MissingModelTextures?

    /// Copy provider files while their security scope is held. Folder imports retain
    /// relative paths, so glTF buffers and model textures remain beside the model.
    func importModelFiles(urls: [URL]) {
        guard !importingMedia else { toast = AureaText.t("ios_importing_media"); return }
        guard !urls.isEmpty else { toast = AureaText.t("msg_esse_arquivo_nao_e_um_modelo"); return }
        // Só o .mtl/texturas (o modelo já entrou antes sem eles): religa no
        // modelo 3D selecionado, pelo mesmo caminho de "Importar texturas".
        let sideExts: Set<String> = ["mtl", "bin", "png", "jpg", "jpeg", "webp", "tga", "bmp", "psd", "gif", "ktx2", "dds", "tif", "tiff"]
        if urls.allSatisfy({ sideExts.contains($0.pathExtension.lowercased()) }),
           let layer = primarySelection, !engine.modelFolder(layer).isEmpty {
            let req = MissingModelTextures(layer: layer, names: engine.modelMissingTextures(layer))
            texturesTarget = req
            importModelTextures(urls: urls)
            return
        }
        NSLog("Aurea model import: copying %ld selected item(s)", urls.count)
        let scoped = urls.filter { $0.startAccessingSecurityScopedResource() }
        operationMessage = AureaText.t("ios_importing_media")
        importingMedia = true
        if status.playing != 0 { engine.run { $0.pause() }; status.playing = 0 }
        let importer = engine
        mediaQueue.async { [weak self] in
            defer { scoped.forEach { $0.stopAccessingSecurityScopedResource() } }
            let fm = FileManager.default
            let modelExts: Set<String> = ["glb", "gltf", "fbx", "obj"]
            let name = urls[0].deletingPathExtension().lastPathComponent
            let root = AureaPaths.media.appendingPathComponent("Modelos", isDirectory: true)
            var folder = root.appendingPathComponent(name, isDirectory: true)
            var counter = 1
            while fm.fileExists(atPath: folder.path) {
                folder = root.appendingPathComponent("\(name)-\(counter)", isDirectory: true); counter += 1
            }
            var result: Int64 = -1
            var failure = ""
            var missing: [String] = []
            do {
                try fm.createDirectory(at: folder, withIntermediateDirectories: true)
                var candidates: [URL] = []
                for source in urls {
                    let target = folder.appendingPathComponent(source.lastPathComponent)
                    guard !fm.fileExists(atPath: target.path) else { continue }
                    try AureaPaths.copyImport(source, to: target)
                    var isDir: ObjCBool = false
                    if fm.fileExists(atPath: target.path, isDirectory: &isDir), isDir.boolValue {
                        if let walk = fm.enumerator(at: target, includingPropertiesForKeys: [.isRegularFileKey], options: [.skipsHiddenFiles]) {
                            let files = walk.allObjects.compactMap { $0 as? URL }.filter { modelExts.contains($0.pathExtension.lowercased()) }
                            candidates.append(contentsOf: files.sorted { $0.path < $1.path })
                        }
                    } else if target.pathExtension.lowercased() == "zip" {
                        let extracted = folder.appendingPathComponent("archive-\(UUID().uuidString)", isDirectory: true)
                        guard let files = AureaEngine.extractModelArchive(target.path, to: extracted.path) else {
                            throw NSError(domain: "AureaModelImport", code: 10,
                                userInfo: [NSLocalizedDescriptionKey: AureaText.t("msg_nao_consegui_ler_esse_arquivo")])
                        }
                        candidates.append(contentsOf: files.map { URL(fileURLWithPath: $0) }
                            .filter { modelExts.contains($0.pathExtension.lowercased()) })
                    } else if modelExts.contains(target.pathExtension.lowercased()) { candidates.append(target) }
                    else if target.pathExtension.isEmpty && ModelPlanInfo(importer.inspectModel(target.path)).valid {
                        candidates.append(target)
                    }
                }
                if let modelURL = candidates.first {
                    // Plano do motor ANTES do import (só cabeçalhos e contagens):
                    // o que cabe neste aparelho. Pesado = pergunta; pesado demais = recusa.
                    let plan = ModelPlanInfo(importer.inspectModel(modelURL.path))
                    let modelName = modelURL.deletingPathExtension().lastPathComponent
                    if plan.tooHeavy {
                        failure = AureaText.t("model3d_too_heavy")
                    } else if plan.heavy {
                        let request = ModelOptimizeRequest(path: modelURL.path, name: modelName, folder: folder, plan: plan)
                        DispatchQueue.main.async { [weak self] in
                            guard let self else { return }
                            self.importingMedia = false
                            self.modelOptimizeQuality = plan.recommended == 0 ? 1 : plan.recommended
                            self.modelOptimize = request
                        }
                        return
                    } else {
                        result = self?.runModelImport(importer, path: modelURL.path, name: modelName, quality: 0) ?? -1
                        if result < 0 {
                            failure = Self.modelImportFailure(importer, code: result)
                        } else { missing = importer.modelMissingTextures(result) }
                    }
                } else { failure = AureaText.t("msg_esse_arquivo_nao_e_um_modelo") }
            } catch { failure = AureaText.t("ios_import_copy_failed", error.localizedDescription) }
            if result < 0 { try? fm.removeItem(at: folder) }
            let importedId = result, importFailure = failure, stillMissing = missing
            NSLog("Aurea model import: %@", failure.isEmpty ? "completed" : "failed")
            DispatchQueue.main.async {
                guard let self else { return }
                self.importingMedia = false
                if !importFailure.isEmpty { self.toast = importFailure; return }
                self.engine.selectLayers([NSNumber(value: importedId)]); self.selection = [importedId]
                self.refreshModel(force: true)
                if !stillMissing.isEmpty { self.promptModelTextures(layer: importedId, names: stillMissing) }
                _ = self.saveProject(writeThumbnail: false)
            }
        }
    }

    // MARK: "Otimizar modelo" (orçamento de memória do import 3D)

    /// O plano que o motor devolve antes do import (Engine::inspect_model).
    /// Layout em AureaEngine.h (`inspectModel:`); a conta é toda do motor.
    struct ModelPlanInfo {
        let values: [Int64]
        init(_ raw: [NSNumber]) { values = raw.map { $0.int64Value } }
        private func at(_ i: Int) -> Int64 { i < values.count ? values[i] : 0 }
        var valid: Bool { at(0) != 0 }
        var exact: Bool { at(1) != 0 }
        var heavy: Bool { valid && at(2) != 0 }
        var tooHeavy: Bool { valid && at(3) != 0 }
        var recommended: Int { Int(max(0, min(2, at(4)))) }
        var triangles: Int64 { at(5) }
        func fits(_ q: Int) -> Bool { at(10 + max(0, min(2, q))) != 0 }
        func keptTriangles(_ q: Int) -> Int64 { at(16 + max(0, min(2, q))) }
        func textureCap(_ q: Int) -> Int { Int(at(19 + max(0, min(2, q)))) }
        /// Da mais fiel à mais leve; o Original só quando cabe.
        var offered: [Int] { (fits(0) ? [0] : []) + [1, 2] }
    }

    struct ModelOptimizeRequest: Identifiable {
        let id = UUID()
        let path: String
        let name: String
        let folder: URL
        let plan: ModelPlanInfo
    }
    @Published var modelOptimize: ModelOptimizeRequest?
    @Published var modelOptimizeQuality = 1

    /// "1,2 mi" / "850 mil" — triângulos curtos para o alerta e o aviso.
    static func modelCount(_ n: Int64) -> String {
        if n >= 1_000_000 { return String(format: "%.1f %@", Double(n) / 1_000_000, AureaText.t("model3d_million")) }
        if n >= 1_000 { return "\(n / 1_000) \(AureaText.t("model3d_thousand"))" }
        return "\(n)"
    }

    /// Erro do import em texto: −9 (BudgetExceeded) é o "pesado demais" localizado.
    static func modelImportFailure(_ importer: AureaEngine, code: Int64) -> String {
        if code == -9 { return AureaText.t("model3d_too_heavy_after") }
        let detail = importer.lastImportError
        return detail.isEmpty ? AureaText.t("ios_import_failed_code", String(-code)) : AureaEngineText.sentence(detail, code: Int(-code))
    }

    /// O import de verdade (bloqueia: fila de mídia), com as etapas do motor
    /// localizadas no aviso de progresso enquanto roda.
    func runModelImport(_ importer: AureaEngine, path: String, name: String, quality: Int) -> Int64 {
        let timer = DispatchSource.makeTimerSource(queue: .main)
        timer.schedule(deadline: .now() + 0.15, repeating: 0.15)
        timer.setEventHandler { [weak self] in
            let p = Int(importer.importModelProgress())
            let stage: String
            switch p / 1000 {
            case 1: stage = AureaText.t("msg_lendo_o_arquivo")
            case 2: stage = AureaText.t("msg_geometria")
            case 3: stage = AureaText.t("msg_texturas")
            case 4: stage = AureaText.t("msg_otimizando")
            case 5, 6: stage = AureaText.t("msg_preparando")
            case 7: stage = AureaText.t("model3d_stage_simplifying")
            default: stage = AureaText.t("msg_importando")
            }
            self?.setOperationMessage("\(stage)… \((p % 1000) / 10)%")
        }
        timer.resume()
        let result = importer.importModel(path, name: name, quality: Int32(quality))
        timer.cancel()
        return result
    }

    private func setOperationMessage(_ text: String) { operationMessage = text }

    func dismissModelOptimize(_ request: ModelOptimizeRequest) {
        modelOptimize = nil
        try? FileManager.default.removeItem(at: request.folder)
    }

    /// O botão do alerta: importa com a qualidade escolhida.
    func confirmModelOptimize(_ request: ModelOptimizeRequest, quality: Int) {
        modelOptimize = nil
        guard !importingMedia else { return }
        operationMessage = AureaText.t("ios_importing_media")
        importingMedia = true
        if status.playing != 0 { engine.run { $0.pause() }; status.playing = 0 }
        let importer = engine
        mediaQueue.async { [weak self] in
            guard let self else { return }
            let result = self.runModelImport(importer, path: request.path, name: request.name, quality: quality)
            let failure = result < 0 ? Self.modelImportFailure(importer, code: result) : ""
            let missing = result < 0 ? [] : importer.modelMissingTextures(result)
            let report = importer.lastModelImport().map { $0.int64Value }
            if result < 0 { try? FileManager.default.removeItem(at: request.folder) }
            DispatchQueue.main.async {
                self.importingMedia = false
                if !failure.isEmpty { self.toast = failure; return }
                self.engine.selectLayers([NSNumber(value: result)]); self.selection = [result]
                self.refreshModel(force: true)
                if !missing.isEmpty { self.promptModelTextures(layer: result, names: missing) }
                else if quality != 0, report.count >= 2, report[0] > report[1] {
                    self.toast = AureaText.t("model3d_optimized_toast", Self.modelCount(report[0]), Self.modelCount(report[1]))
                }
                _ = self.saveProject(writeThumbnail: false)
            }
        }
    }

    func promptModelTextures(layer: Int64, names: [String]) {
        let req = MissingModelTextures(layer: layer, names: names)
        texturesTarget = req; missingModelTextures = req
    }

    /// Abre "Importar texturas" para o modelo selecionado (o painel oferece quando falta algo).
    func askModelTextures() {
        guard let id = primarySelection else { return }
        let names = engine.modelMissingTextures(id)
        if !names.isEmpty && !engine.modelFolder(id).isEmpty { promptModelTextures(layer: id, names: names) }
    }

    /// As imagens (e o .mtl) escolhidas: copiadas para a pasta do modelo — com o
    /// nome que o MODELO grava quando o do seletor bate sem diferenciar
    /// maiúsculas — e o modelo é relido. O que ainda faltar reabre o pedido.
    func importModelTextures(urls: [URL]) {
        guard let req = texturesTarget, !urls.isEmpty, !importingMedia else { return }
        texturesTarget = nil; missingModelTextures = nil
        let folderPath = engine.modelFolder(req.layer)
        guard !folderPath.isEmpty else { return }
        let folder = URL(fileURLWithPath: folderPath, isDirectory: true)
        var wanted: [String: String] = [:]
        for n in req.names { wanted[n.lowercased()] = n }
        // O .mtl escolhido com outro nome vira o .mtl que o OBJ procura.
        let mtlNames = req.names.filter { $0.lowercased().hasSuffix(".mtl") }
        let missingMtl: String? = mtlNames.count == 1 ? mtlNames[0] : nil
        operationMessage = AureaText.t("msg_texturas") + "…"
        importingMedia = true
        let importer = engine
        mediaQueue.async { [weak self] in
            var copied = 0
            var copyFailure = ""
            for url in urls {
                let scoped = url.startAccessingSecurityScopedResource()
                defer { if scoped { url.stopAccessingSecurityScopedResource() } }
                let picked = url.lastPathComponent
                let named = wanted[picked.lowercased()] ?? (picked.lowercased().hasSuffix(".mtl") ? missingMtl : nil)
                let target = folder.appendingPathComponent(named ?? picked)
                // Fora da lista: entra, mas nunca por cima de um arquivo da pasta.
                if FileManager.default.fileExists(atPath: target.path) {
                    if named == nil { copied += 1; continue }
                }
                let staged = folder.appendingPathComponent(".texture-\(UUID().uuidString).pending")
                defer { try? FileManager.default.removeItem(at: staged) }
                do {
                    try AureaPaths.copyImport(url, to: staged)
                    let size = try staged.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0
                    guard size > 0 else { throw NSError(domain: NSCocoaErrorDomain, code: NSFileReadCorruptFileError) }
                    if FileManager.default.fileExists(atPath: target.path) {
                        _ = try FileManager.default.replaceItemAt(target, withItemAt: staged)
                    } else { try FileManager.default.moveItem(at: staged, to: target) }
                    copied += 1
                } catch {
                    copyFailure = AureaText.t("ios_import_copy_failed", error.localizedDescription)
                    NSLog("Aurea model textures: copy failed %@", error.localizedDescription)
                }
            }
            let left = copied > 0 ? importer.reloadModelTextures(req.layer) : -1
            let failure = copied == 0 ? (copyFailure.isEmpty ? AureaText.t("msg_nao_consegui_ler_esse_arquivo") : copyFailure)
                : left < 0 ? AureaEngineText.sentence(importer.lastImportError, code: Int(-left)) : copyFailure
            let still = left > 0 ? importer.modelMissingTextures(req.layer) : []
            DispatchQueue.main.async {
                guard let self else { return }
                self.importingMedia = false
                self.refreshModel(force: true)
                if left >= 0 {
                    _ = self.saveProject(writeThumbnail: false)
                    if !still.isEmpty { self.promptModelTextures(layer: req.layer, names: still) }
                }
                if !failure.isEmpty { self.toast = failure }
                else if still.isEmpty { self.toast = AureaText.t("model_textures_done") }
            }
        }
    }

    enum ImportKind { case video, audio, image, model, hdri }

    func performMediaOperation(_ message: String, operation: @escaping (AureaEngine) -> String) {
        guard !importingMedia else { return }
        importingMedia = true; operationMessage = message
        if status.playing != 0 { engine.run { $0.pause() }; status.playing = 0 }
        let native = engine
        mediaQueue.async { [weak self] in
            let error = operation(native)
            DispatchQueue.main.async {
                guard let self else { return }
                self.importingMedia = false
                self.refreshModel(force: true)
                if !error.isEmpty { self.toast = AureaEngineText.sentence(error) }
                else { _ = self.saveProject(writeThumbnail: false) }
            }
        }
    }

    @Published var cameraFeatures: [Float] = []
    @Published var cameraSelection: CGRect? = nil
    var cameraSelectionFrame: Int64 = -1
    @Published var cameraMultiSelect = false
    @Published var cameraTargetMode = true
    @Published var cameraTarget: [Float] = []
    @Published var cameraGoodPointsOnly = true
    @Published var cameraPointSize: Double = 3
    @Published var cameraContextMenu = false
    @Published var cameraSelectedCount: UInt32 = 0

    func refreshCameraTrackPoints() {
        cameraFeatures = engine.cameraTrackDetails(atFrame: status.playhead).map(\.floatValue)
        cameraTarget = engine.cameraTrackTarget(status.playhead).map(\.floatValue)
    }
    func moveCameraTarget(_ point: CGPoint) {
        let p = cameraFeatures
        let nearby = stride(from: 0, to: p.count - p.count % 6, by: 6).filter { p[$0+2] > 0 && (!cameraGoodPointsOnly || p[$0+2] >= 0.4) }.sorted {
            hypot(p[$0] - Float(point.x), p[$0+1] - Float(point.y)) < hypot(p[$1] - Float(point.x), p[$1+1] - Float(point.y))
        }.prefix(3).map { NSNumber(value: p[$0+3]) }
        cameraSelectedCount = engine.selectCameraTrackPoints(nearby, operation: 0)
        cameraSelection = nil; refreshCameraTrackPoints()
    }
    func calibrateCamera(_ operation: UInt32, distance: Float = 100) {
        guard engine.calibrateCameraScene(operation, distance: distance) else { toast = AureaText.t("ios_camera_calibrate_invalid"); return }
        refreshModel(force: true); refreshCameraTrackPoints(); toast = AureaText.t("ios_camera_reference_updated")
    }
    func placeTrackedModel(_ id: Int64) {
        guard engine.placeModelOnTrack(id) else { toast = AureaText.t("ios_camera_place_needs_surface"); return }
        refreshModel(force: true); toast = AureaText.t("ios_camera_model_placed")
    }
    func selectCameraPoint(_ location: CGPoint, radius: CGFloat) {
        engine.pause()
        var best: Int? = nil
        var distance = Float(radius * radius)
        for i in stride(from: 0, to: cameraFeatures.count - cameraFeatures.count % 6, by: 6) {
            if cameraFeatures[i+2] <= 0 || (cameraGoodPointsOnly && cameraFeatures[i+2] < 0.4) { continue }
            let dx = cameraFeatures[i] - Float(location.x), dy = cameraFeatures[i+1] - Float(location.y)
            let d = dx*dx + dy*dy
            if d < distance { best = i; distance = d }
        }
        if let i = best {
            let op: UInt32 = !cameraMultiSelect ? 0 : cameraFeatures[i+4] > 0.5 ? 2 : 1
            cameraSelectedCount = engine.selectCameraTrackPoints([NSNumber(value: cameraFeatures[i+3])], operation: op)
        } else if !cameraMultiSelect {
            cameraSelectedCount = engine.selectCameraTrackPoints([], operation: 0)
        }
        cameraSelection = nil
        refreshCameraTrackPoints()
    }
    func finishCameraSelectionBox() {
        guard let box = cameraSelection else { return }
        var ids: [NSNumber] = []
        for i in stride(from: 0, to: cameraFeatures.count - cameraFeatures.count % 6, by: 6) {
            if cameraFeatures[i+2] <= 0 || (cameraGoodPointsOnly && cameraFeatures[i+2] < 0.4) { continue }
            if box.contains(CGPoint(x: CGFloat(cameraFeatures[i]), y: CGFloat(cameraFeatures[i+1]))) { ids.append(NSNumber(value: cameraFeatures[i+3])) }
        }
        cameraSelectedCount = engine.selectCameraTrackPoints(ids, operation: cameraMultiSelect ? 1 : 0)
        cameraSelection = nil
        refreshCameraTrackPoints()
    }
    func createTrackedObject(_ kind: UInt32) {
        let error = engine.createCameraTrackObject(kind)
        if !error.isEmpty { toast = AureaEngineText.sentence(error) }
        cameraContextMenu = false
        refreshModel(force: true)
        refreshCameraTrackPoints()
    }

    func applySelectedCameraTracking() -> String {
        if let rect = cameraSelection {
            return engine.applyCameraSelection(atFrame: cameraSelectionFrame, x0: Float(rect.minX), y0: Float(rect.minY), x1: Float(rect.maxX), y1: Float(rect.maxY))
        }
        return engine.applyCameraTracking()
    }

    @Published var motionTool: UInt32 = 0
    @Published var motionModel: UInt32 = 0
    @Published var motionBackward = false
    @Published var motionFeature: Float = 12
    @Published var motionSearch: Float = 48
    @Published var motionStatus: [String: Any] = [:]
    @Published var motionPicked = 0
    private var motionSeeds: [NSNumber] = []
    private var motionSource: Int64?
    /// Menu da camada: "Rastrear um ponto" (cria um Nulo que segue o ponto) e
    /// "Estabilizar pelo ponto" (o ponto fica parado na tela). Escolhe o ponto,
    /// analisa em segundo plano e APLICA sozinho ao terminar — antes a análise
    /// rodava e o resultado nunca era aplicado (nem o painel abria).
    func beginPointPick(stabilize: Bool) {
        beginMotionPick(0)
        if pointPick != nil { motionAutoApply = stabilize ? 3 : 0 }
    }
    func beginMotionPick(_ tool: UInt32) {
        guard let layer = selectedLayer, layer.kind == 1, !layer.locked, let id = primarySelection else { return }
        guard (motionStatus["state"] as? NSNumber)?.intValue != 1 else { return }
        engine.run { $0.pause() }
        motionAutoApply = -1
        motionTool = tool; motionSource = id; motionSeeds = []; motionPicked = 0
        if tool == 4 { startPickedMotion(); return }
        pointPick = false
        toast = tool >= 2 ? AureaText.t("ios_motion_tap_corners") : AureaText.t("ios_motion_tap_detail")
    }
    func cancelMotionPick() { pointPick = nil; motionSeeds = []; motionPicked = 0; motionAutoApply = -1 }
    /// -1 nada; 0 cria o Nulo; 3 estabiliza pelo ponto — quando a análise do menu termina.
    private var motionAutoApply = -1
    private var motionPoll: Task<Void, Never>?
    func finishPointPick(_ point: CGPoint) {
        guard pointPick != nil, let id = primarySelection, id == motionSource else { cancelMotionPick(); return }
        let a = engine.maskData(id).prefix(6).map(\.floatValue)
        guard a.count == 6 else { return }
        let det = a[0] * a[3] - a[1] * a[2]
        guard abs(det) > 0.000001 else { return }
        let x = Float(point.x) - a[4], y = Float(point.y) - a[5]
        motionSeeds += [NSNumber(value: (a[3] * x - a[2] * y) / det), NSNumber(value: (-a[1] * x + a[0] * y) / det)]
        motionPicked += 1
        let needed = motionTool == 0 ? 1 : motionTool == 1 ? 2 : 4
        if motionPicked < needed { toast = AureaText.t("ios_motion_point_next", motionPicked, needed); return }
        pointPick = nil; startPickedMotion()
    }
    private func startPickedMotion() {
        guard let id = motionSource else { return }
        if !engine.startMotionTrack(id, tool: motionTool, model: motionModel, backward: motionBackward, points: motionSeeds, feature: motionFeature, search: motionSearch) {
            motionAutoApply = -1
            toast = AureaText.t("track_start_failed")
        }
        motionStatus = engine.motionTrackStatus()
        // Acompanha a análise mesmo com o painel fechado (progresso, fim, aplicar).
        motionPoll?.cancel()
        motionPoll = Task { @MainActor in
            while !Task.isCancelled {
                self.motionStatus = self.engine.motionTrackStatus()
                if (self.motionStatus["state"] as? NSNumber)?.intValue != 1 { break }
                try? await Task.sleep(nanoseconds: 200_000_000)
            }
            if Task.isCancelled { return }
            let auto = self.motionAutoApply
            self.motionAutoApply = -1
            let state = (self.motionStatus["state"] as? NSNumber)?.intValue ?? 0
            guard state == 2 else {
                if auto >= 0, state == 3, let message = self.motionStatus["message"] as? String, !message.isEmpty { self.toast = AureaEngineText.sentence(message) }
                return
            }
            if auto == 3 { self.applyMotion(3, lock: true, smooth: 0.5, maxScale: 1, crop: 0) }
            else if auto == 0 { self.applyMotion(0) }
        }
    }
    func restoreMotion() {
        if let id = primarySelection, engine.restoreMotionTrack(id) { motionSource = id; motionStatus = engine.motionTrackStatus() }
    }
    func applyMotion(_ apply: UInt32, lock: Bool = false, smooth: Float = 0.5, maxScale: Float = 1.15, crop: UInt32 = 1, target: Int64? = nil) {
        let error = engine.applyMotionTrack(target ?? primarySelection ?? 0, apply: apply, lock: lock, smooth: smooth, maxScale: maxScale, crop: crop)
        toast = error.isEmpty ? AureaText.t("ios_motion_applied") : AureaEngineText.sentence(error)
        refreshModel(force: true); motionStatus = engine.motionTrackStatus()
    }

    func removeGaps() {
        let removed = engine.removeGaps()
        refreshModel(force: true)
        let fps = max(1, (composition["fps"] as? NSNumber)?.doubleValue ?? 30)
        toast = removed > 0 ? AureaText.t("msg_gaps_removed", String(format: "%.1f", Double(removed) / fps))
            : AureaText.t("msg_nao_ha_espacos_vazios")
    }
    func trimProjectAtPlayhead() {
        if engine.trimComposition(status.playhead) {
            refreshModel(force: true)
            toast = AureaText.t("msg_projeto_aparado_no_cabecote")
        }
    }

    func addAdjustmentLayer() {
        guard started else { return }
        engine.run { $0.beginUndoGroup() }
        let id = engine.addNull(false)
        if id >= 0 {
            engine.setLayer(id, adjustment: true)
            engine.setLayer(id, name: AureaText.t("editor_camada_ajuste"))
        }
        engine.run { $0.endUndoGroup() }
        guard id >= 0 else { toast = AureaText.t("msg_nao_foi_possivel_criar_a_camada_2", -id); return }
        showAddLayer = false
        syncAfterEdit(); select(layerId: id, additive: false)
        toast = AureaText.t("msg_camada_de_ajuste_adicione_efeitos_nela")
    }

    func importSvg(url: URL) {
        guard !importingMedia else { return }
        importingMedia = true; operationMessage = AureaText.t("ios_importing_media")
        let scoped = url.startAccessingSecurityScopedResource()
        let native = engine
        mediaQueue.async { [weak self] in
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            var imported: Int64?
            do {
                let handle = try FileHandle(forReadingFrom: url)
                defer { try? handle.close() }
                var bytes = Data()
                while let chunk = try handle.read(upToCount: 65_536), !chunk.isEmpty {
                    guard bytes.count + chunk.count <= 32 * 1024 * 1024 else { throw CocoaError(.fileReadTooLarge) }
                    bytes.append(chunk)
                }
                if let text = String(data: bytes, encoding: .utf8), !text.isEmpty {
                    imported = native.importSVG(text, name: url.deletingPathExtension().lastPathComponent)
                }
            } catch { imported = nil }
            let result = imported
            DispatchQueue.main.async {
                guard let self else { return }
                self.importingMedia = false
                guard let id = result else { self.toast = AureaText.t("msg_nao_foi_possivel_ler_o_svg"); return }
                guard id >= 0 else {
                    self.toast = id == -17 ? AureaText.t("msg_svg_sem_formas_suportadas")
                        : AureaText.t("msg_nao_foi_possivel_importar_o_svg", String(-id))
                    return
                }
                self.showAddLayer = false
                self.syncAfterEdit(); self.select(layerId: id, additive: false)
                _ = self.saveProject(writeThumbnail: false)
            }
        }
    }

    func importPsd(url: URL) {
        guard !importingMedia else { return }
        importingMedia = true; operationMessage = AureaText.t("psd_importing")
        let native = engine
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            let scoped = url.startAccessingSecurityScopedResource()
            let id = native.importPSD(url.path, name: url.deletingPathExtension().lastPathComponent)
            if scoped { url.stopAccessingSecurityScopedResource() }
            DispatchQueue.main.async {
                guard let self else { return }
                self.importingMedia = false
                guard id >= 0 else { self.toast = AureaText.t("psd_failed"); return }
                self.showAddLayer = false; self.syncAfterEdit(); self.select(layerId: id, additive: false)
                self.toast = AureaText.t("psd_imported")
                _ = self.saveProject(writeThumbnail: false)
            }
        }
    }

    func createGrid() {
        let id = engine.createGrid(selection.map { NSNumber(value: $0) })
        guard id >= 0 else { toast = AureaText.t("grid_pick_layers"); return }
        showAddLayer = false; syncAfterEdit(); select(layerId: id, additive: false)
        openPanel(.effects)
    }

    func addCatalogEffect(_ type: UInt32, layers ids: [Int64]) {
        guard !ids.isEmpty else { return }
        func add() {
            mutate { native in
                native.beginUndoGroup()
                for id in ids { native.addEffect(type, toLayer: id, at: UInt32.max) }
                native.endUndoGroup()
            }
            refreshModel(force: true)
        }
        guard type == fxEffectTypeId("aurea.key.rotobrush") else { add(); return }
        guard ids.allSatisfy({ id in layers.contains { $0.id == id && ($0.kind == 1 || $0.kind == 2) } }) else { toast = AureaText.t("roto_select_media"); return }
        add()
    }

    func detectBeats() {
        guard started, !importingMedia, let layer = selectedLayer, layer.kind == 1 || layer.kind == 3 else {
            toast = AureaText.t("msg_escolha_uma_camada_de_audio_ou"); return
        }
        let request = UUID(), project = projectGeneration
        let compositionID = (composition[AureaCompositionId] as? NSNumber)?.uint64Value ?? 0
        beatDetectionRequest = request
        importingMedia = true; operationMessage = AureaText.t("msg_detectando_batidas")
        if status.playing != 0 { playPause() }
        let native = engine
        mediaQueue.async { [weak self] in
            var bpm: Double = 0
            let count = native.detectBeats(forLayer: layer.id, bpm: &bpm)
            let detectedBPM = bpm
            DispatchQueue.main.async {
                self?.finishBeatDetection(request: request, project: project, compositionID: compositionID,
                                          count: count, bpm: detectedBPM)
            }
        }
    }

    private func finishBeatDetection(request: UUID, project: UUID, compositionID: UInt64, count: Int64, bpm: Double) {
        guard beatDetectionRequest == request else { return }
        beatDetectionRequest = nil
        // A late callback must not clear another project's import/analysis flag.
        guard projectGeneration == project else { return }
        importingMedia = false
        guard started, (composition[AureaCompositionId] as? NSNumber)?.uint64Value == compositionID else { return }
        refreshModel(force: true); refreshMarkers()
        if count > 0 {
            guard bpm.isFinite, bpm >= 0, let roundedBPM = Int32(exactly: bpm.rounded()) else {
                toast = AureaText.t("msg_nao_foi_possivel_analisar_o_som", 10); return
            }
            toast = AureaText.t("msg_batidas_bpm", count, Int(roundedBPM))
            _ = saveProject(writeThumbnail: false)
        } else {
            toast = count == 0 ? AureaText.t("msg_nenhuma_batida_clara_neste_som")
                : AureaText.t("msg_nao_foi_possivel_analisar_o_som", count == Int64.min ? Int64.max : -count)
        }
    }

    func clearHomeCaches() {
        HomeThumbCache.shared.clear()
        let freed = engine.trimMemory(80)
        Task {
            let disk = await effectPreviews.clear()
            toast = AureaText.t("msg_cache_cleared", String(format: "%.1f", Double(freed + disk) / (1024 * 1024)))
        }
    }

    func analyseDeviceAgain() {
        // iOS probes on every engine start; there is no persisted Android DeviceProfile to invalidate.
        deviceReport = engine.deviceReport()
        deviceSummary = engine.deviceSummary
        toast = AureaText.t("settings_device_analysed")
    }

    func addShape(_ preset: UInt32) {
        let id = engine.addShape(preset)
        if id >= 0 { selection = [id]; engine.selectLayers([NSNumber(value: id)]) }
        showAddLayer = false
        syncAfterEdit()
    }

    /// Botão "Texto": camada nova já escolhida e o teclado aberto (salvo `openEditor: false`,
    /// usado pelos presets de texto da barra de adicionar). Devolve o id (negativo = falhou).
    @discardableResult
    func addText(openEditor: Bool = true) -> Int64 {
        let id = engine.addText(AureaText.t("pn_text3d_placeholder"))
        if id >= 0 { selection = [id]; engine.selectLayers([NSNumber(value: id)]) }
        showAddLayer = false
        syncAfterEdit()
        if id >= 0 && openEditor { openTextContentEditor(selectAll: true) }
        return Int64(id)
    }

    func addVector(_ preset: UInt32, freehand: Bool = false) {
        let id = engine.addVector(preset)
        guard id >= 0 else { toast = AureaText.t("ios_vector_create_failed"); return }
        selection = [id]; engine.selectLayers([NSNumber(value: id)]); showAddLayer = false
        syncAfterEdit(); openPanel(.vector); maskDrawing = preset == 0; vectorFreehand = freehand
    }

    func finishFreehand() {
        guard let id = primarySelection, freehandPoints.count >= 4 else { freehandPoints = []; return }
        let added = engine.addFreehand(id, points: freehandPoints.map { NSNumber(value: $0) }, error: 2)
        freehandPoints = []
        if added >= 0 {
            vectorGroup = UInt32(max(0, engine.vectorGroups(id).count - 1)); vectorPath = 0
            refreshModel(force: true); refreshMasks()
        } else { toast = AureaText.t("ios_drawing_add_failed") }
    }

    @Published var sceneEditor = false
    @Published var sceneYaw: Float = -30
    @Published var scenePitch: Float = 20
    @Published var sceneDistance: Float = 3
    func enterSceneEditor() {
        engine.run { $0.pause() }
        status.playing = 0; panel = .none; fullscreen = false
        sceneEditor = true
        updateSceneView()
    }
    func exitSceneEditor() {
        sceneEditor = false
        updateSceneView()
    }
    func updateSceneView() {
        // Os mesmos limites do motor: a vista nunca "vira" nem atravessa o centro.
        sceneYaw = Float(remainder(Double(sceneYaw), 360))
        scenePitch = min(80, max(-80, scenePitch))
        sceneDistance = min(10, max(0.25, sceneDistance))
        engine.setSceneEditor(sceneEditor, yaw: sceneYaw, pitch: scenePitch, distance: sceneDistance)
        refreshSelectedLayer()
    }

    func setSceneView(yaw: Float, pitch: Float, distance: Float) {
        sceneYaw = yaw; scenePitch = pitch; sceneDistance = distance
        updateSceneView()
    }

    /// Toque duplo no vazio da cena: a vista volta ao ângulo inicial.
    func resetSceneView() { setSceneView(yaw: -30, pitch: 20, distance: 3) }

    /// Objeto 3D sob o dedo (px da composição), decidido pelo motor
    /// (`Engine::scene_pick`): o raio da câmera de navegação contra o corpo real
    /// — plano do texto/forma, triângulos do texto 3D/forma 3D/modelo — em
    /// qualquer Z e órbita; ganha o mais perto. Sem corpo ali (câmera, luz,
    /// nulo), a origem mais perto até `radius`.
    func scenePick(_ point: SIMD2<Float>, radius: Float) -> Int64? {
        let id = engine.scenePick(x: point.x, y: point.y, radius: radius)
        return id != 0 ? Int64(id) : nil
    }

    private struct SceneDragSnapshot {
        let layer: Int64
        let composition: UInt64
        let project: UUID
        let frame: Int64
        let basis: [NSNumber]
        var dx: Float = 0
        var dy: Float = 0
        var previous: [Float]
    }
    private var sceneDragSnapshot: SceneDragSnapshot?
    private var transformGestureDepth = 0

    /// Incremental composition-pixel deltas, projected by the shared gesture
    /// basis captured once. Queued commands must not become the next base.
    func sceneDragObject(dx: Float, dy: Float) {
        guard let id = primarySelection, dx.isFinite, dy.isFinite else { return }
        let compositionID = (composition[AureaCompositionId] as? NSNumber)?.uint64Value ?? 0
        if transformGestureDepth == 0 || sceneDragSnapshot?.layer != id ||
            sceneDragSnapshot?.composition != compositionID || sceneDragSnapshot?.project != projectGeneration ||
            sceneDragSnapshot?.frame != status.playhead {
            let basis = engine.previewGestureBasis(id)
            guard basis.count == 13 else { sceneDragSnapshot = nil; return }
            sceneDragSnapshot = SceneDragSnapshot(layer: id, composition: compositionID, project: projectGeneration,
                frame: status.playhead, basis: basis, previous: basis.prefix(3).map(\.floatValue))
        }
        guard var snapshot = sceneDragSnapshot else { return }
        snapshot.dx += dx; snapshot.dy += dy
        guard snapshot.dx.isFinite, snapshot.dy.isFinite else { return }
        let next = engine.previewGestureValue(snapshot.basis, dx: snapshot.dx, dy: snapshot.dy, rotate: false).map(\.floatValue)
        applyGizmoComponents(id, base: 0, previous: snapshot.previous, next: next)
        snapshot.previous = next
        sceneDragSnapshot = snapshot
    }

    /// Posição vinda do gizmo/arrasto 3D: com Auto-Key, trilha animada grava
    /// keyframe no cabeçote (na cena 3D também); trilha parada é deslocada.
    func applyGizmoPosition(_ id: Int64, _ next: [Float]) {
        applyGizmoComponents(id, base: 0, previous: StageGeom.floats(detail["position"]), next: next)
    }

    /// Valores ABSOLUTOS de Escala (base 3) ou Rotação (base 6) XYZ vindos do
    /// gizmo/pinça 3D — calculados do início do gesto, sem deriva nem salto.
    func gizmoSetComponents(_ id: Int64, base: UInt32, values: [Float]) {
        let key = base == 0 ? "position" : base == 3 ? "scale" : base == 6 ? "rotation" : ""
        guard !key.isEmpty else { return }
        guard let current = engine.layerDetail(id) else { return }
        applyGizmoComponents(id, base: base, previous: StageGeom.floats(current[key]), next: values)
    }

    private func applyGizmoComponents(_ id: Int64, base: UInt32, previous: [Float], next: [Float]) {
        guard next.count == 3, previous.count >= 3, next.allSatisfy({ $0.isFinite }) else { return }
        guard let layerDetail = engine.layerDetail(id) else { return }
        guard (0..<3).contains(where: { abs(next[$0] - previous[$0]) >= 0.00001 }) else { return }
        let changes = Dictionary(uniqueKeysWithValues: (0..<3).map { (base + UInt32($0), next[$0]) })
        if keyTransformGroup(id, detail: layerDetail, changes: changes) { return }
        let animated = (layerDetail["animatedMask"] as? NSNumber)?.uint32Value ?? 0
        // Submission is asynchronous: three scalar setters would each read
        // the same old XYZ and overwrite the preceding axis command.
        mutate { core in
            for axis in 0...2 where abs(next[axis] - previous[axis]) >= 0.00001 {
                let property = base + UInt32(axis)
                // Na cena 3D também: o dedo no nulo/objeto animado grava o keyframe do cabeçote.
                if autoKeyTransforms && animated & (UInt32(1) << property) != 0 {
                    core.gestureKeyframe(forLayer: id, property: property, value: next[axis], wholeGroup: false)
                } else { core.layoutTransform(id, property: property, value: next[axis]) }
            }
        }
        refreshModel(force: true)
    }

    func addLight(_ kind: UInt32) {
        let id = engine.addLight(kind)
        guard id >= 0 else { toast = AureaText.t("scene_light_failed"); return }
        selection = [id]; engine.selectLayers([NSNumber(value: id)])
        refreshModel(force: true)
    }
    func setLightParam(_ param: UInt32, value: Float) {
        guard let id = primarySelection else { return }
        mutate { $0.setLightParam(id, param: param, value: value) }
        refreshModel(force: true)
    }
    func toggleLightKey(_ param: UInt32, value: Float) {
        guard let id = primarySelection else { return }
        let property: UInt32
        switch param { case 1...4: property = 20 + param; case 6: property = 25; case 7: property = 26; default: return }
        let here = ((detail["keyAtPlayhead"] as? NSNumber)?.uint32Value ?? 0) & (1 << property) != 0
        let local = localPlayhead
        mutate { core in
            if here { core.deleteKeyframe(forLayer: id, property: property, time: local) }
            else { core.insertKeyframe(forLayer: id, property: property, time: local, value: value) }
        }
        refreshModel(force: true)
    }

    // --- Lente da câmera 3D ---------------------------------------------------
    /// Índices de LayerSetCameraParam (ver Command.hpp): 0 mm, 1 DOF, 2 foco, 3 f/, 4 desfoque ×.
    static let cameraLensFocal: UInt32 = 0
    static let cameraLensDof: UInt32 = 1
    static let cameraLensFocus: UInt32 = 2
    static let cameraLensAperture: UInt32 = 3
    static let cameraLensBlur: UInt32 = 4

    /// Trilha animável de um parâmetro da lente (`aurea::TrackProperty`; DOF ligado não anima).
    func cameraLensTrack(_ param: UInt32) -> UInt32? {
        switch param {
        case AureaModel.cameraLensFocal: return 16
        case AureaModel.cameraLensFocus: return 17
        case AureaModel.cameraLensAperture: return 18
        case AureaModel.cameraLensBlur: return 38
        default: return nil
        }
    }

    /// Escreve um parâmetro da lente; trilha com keyframe → keyframe no cabeçote, senão o valor parado.
    func setCameraLens(_ param: UInt32, value: Float) {
        guard let id = primarySelection, value.isFinite else { return }
        if selectedLayer?.locked == true { toast = AureaText.t("editor_camada_bloqueada"); return }
        mutate { $0.setCameraParam(id, param: param, value: value) }
        refreshModel(force: true)
    }

    /// ◇ da lente: alterna o keyframe no cabeçote com o valor atual da lente.
    func toggleCameraLensKey(_ param: UInt32) {
        guard let id = primarySelection, let property = cameraLensTrack(param), cameraLens.count >= 9 else { return }
        let value: Float
        switch param {
        case AureaModel.cameraLensFocal: value = cameraLens[0]
        case AureaModel.cameraLensFocus: value = cameraLens[3]
        case AureaModel.cameraLensAperture: value = cameraLens[4]
        default: value = cameraLens[5]
        }
        let local: Int32 = localPlayhead
        let here: Bool = (keyframes[id] ?? []).contains { $0.property == property && $0.effectIndex == UInt32.max && $0.time == local }
        mutate { core in
            core.beginUndoGroup()
            if here { core.deleteKeyframe(forLayer: id, property: property, time: local) }
            else { core.insertKeyframe(forLayer: id, property: property, time: local, value: value) }
            core.endUndoGroup()
        }
        refreshModel(force: true)
    }

    /// "Tocar para focar": arma o próximo toque no palco.
    func armPickFocus() {
        guard let layer = selectedLayer, primarySelection != nil else { return }
        if layer.kind != 8 { toast = AureaText.t("lens_msg_not_camera"); return }
        if layer.locked { toast = AureaText.t("editor_camada_bloqueada"); return }
        engine.run { $0.pause() }
        cancelMotionPick()
        focusPick = true
        toast = AureaText.t("lens_msg_tap_to_focus")
    }
    func cancelFocusPick() { focusPick = false }
    /// Toque em (x, y) da composição: mede e grava a distância de foco (um comando = um desfazer).
    func finishFocusPick(_ point: CGPoint) {
        focusPick = false
        guard let id = primarySelection, selectedLayer?.kind == 8 else { return }
        let distance: Float = engine.pickFocusDistance(id, x: Float(point.x), y: Float(point.y))
        guard distance >= 0, distance.isFinite else { toast = AureaText.t("lens_msg_nothing_there"); return }
        mutate { $0.setCameraParam(id, param: AureaModel.cameraLensFocus, value: distance) }
        refreshModel(force: true)
        toast = AureaText.t("lens_msg_focus_set", numeroPtBr(distance, casas: 0))
    }

    func addCamera() {
        let id = engine.addCamera()
        if id >= 0 { selection = [id]; engine.selectLayers([NSNumber(value: id)]) }
        else { toast = AureaText.t("msg_nao_foi_possivel_criar_a_camera", String(-id)) }
        showAddLayer = false
        syncAfterEdit()
    }

    func addNull(threeD: Bool) {
        let id = engine.addNull(threeD)
        if id >= 0 { selection = [id]; engine.selectLayers([NSNumber(value: id)]) }
        // Par do Android: o nulo que não nasce DIZ por quê (antes sumia calado).
        else { toast = AureaText.t("msg_nao_foi_possivel_criar_o_nulo", String(-id)) }
        showAddLayer = false
        syncAfterEdit()
    }

    @discardableResult
    func addText3D(content: String, depth: Float, openEditor: Bool = true) -> Int64 {
        let id = engine.addText3D(content, depth: depth, alignment: 1, r: 1, g: 1, b: 1)
        if id >= 0 { selection = [id]; engine.selectLayers([NSNumber(value: id)]) }
        showAddLayer = false
        syncAfterEdit()
        if id >= 0 && openEditor { openTextContentEditor(selectAll: true) }
        return Int64(id)
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
        exportedURL = nil
        exportProgress = [:]; exportMessage = nil; exportCancelled = false; exportSavedToPhotos = false
        exportStalled = false
        exportWatch = ExportStallWatch()
        exportSafeMode = 0
        exportedKind = exportOptions.kind
        if exportOptions.kind != .video { startImageExport(); return }
        let name = (projectName.isEmpty ? "Aurea" : projectName) + ".mp4"
        var url = AureaPaths.documents.appendingPathComponent(name)
        var counter = 1
        while FileManager.default.fileExists(atPath: url.path) {
            url = AureaPaths.documents.appendingPathComponent("\(projectName) \(counter).mp4")
            counter += 1
        }
        let ok = startVideoExport(url, safeMode: 0)
        guard ok else {
            exportMessage = AureaText.t("ios_export_could_not_start")
            toast = exportMessage
            return
        }
        exporting = true
        pendingExportURL = url
        startExportPolling()
    }

    /// O vídeo no motor. `safeMode` > 0 só quando o motor sugeriu refazer depois
    /// de o encoder travar (H.264 Baseline em múltiplos de 16, taxa menor).
    private func startVideoExport(_ url: URL, safeMode: UInt32) -> Bool {
        engine.startExport(to: url.path,
                           codec: exportOptions.codec,
                           height: exportOptions.shortSide,
                           fps: exportOptions.fps,
                           bitrateMbps: exportOptions.bitrateMbps,
                           audioBitrateKbps: exportOptions.audioBitrateKbps,
                           aiUpscale: exportOptions.aiUpscale, trimToContent: exportOptions.trimToContent,
                           quality: exportOptions.quality, safeMode: safeMode)
    }

    /// O plano do export como imagem pela regra do motor (dimensões, quadros, bytes).
    func imageExportPlan() -> [String: NSNumber]? {
        let options = exportOptions
        return engine.imageExportPlan(options.kind.engineCode, shortSide: options.imageShortSide, maxWidth: options.gifWidth,
                                      fps: imageExportFps(options), trimToContent: options.trimToContent)
    }

    private func imageExportFps(_ options: ExportOptions) -> Double {
        switch options.kind {
        case .gif: return options.gifFps
        case .sequence: return options.fps
        default: return 0
        }
    }

    /// PNG, sequência .zip ou GIF: o motor renderiza e codifica; o progresso e
    /// o cancelamento são os do vídeo.
    private func startImageExport() {
        let options = exportOptions
        let base = projectName.isEmpty ? "Aurea" : projectName
        var url = AureaPaths.documents.appendingPathComponent("\(base).\(options.kind.fileExtension)")
        var counter = 1
        while FileManager.default.fileExists(atPath: url.path) {
            url = AureaPaths.documents.appendingPathComponent("\(base) \(counter).\(options.kind.fileExtension)")
            counter += 1
        }
        let code = engine.startImageExport(to: url.path, format: options.kind.engineCode, shortSide: options.imageShortSide,
                                           maxWidth: options.gifWidth, fps: imageExportFps(options), trimToContent: options.trimToContent)
        guard code == 0 else {
            exportMessage = code == 6 && options.kind != .frame ? AureaText.t("exp2_too_long") : AureaText.t("ios_export_could_not_start")
            toast = exportMessage
            return
        }
        exporting = true
        pendingExportURL = url
        startExportPolling()
    }

    func cancelExport() {
        guard exporting && !exportPublishing else { return }
        exportCancelled = true
        engine.cancelExport()
    }

    private var exportTimer: Timer?

    /// A última rede contra "a exportação parou num percentual" — a MESMA regra
    /// do ExportStallWatchdog.kt (kExportUiStallSeconds / kExportUiGiveUpSeconds
    /// do motor, export/ExportWatchdog.hpp). O motor já tem prazo em toda
    /// espera; isto é para o que nem ele vê: sem mudança no progresso por
    /// 180 s a tela cancela; sem conclusão 15 s depois, desiste e se libera.
    struct ExportStallWatch {
        enum Verdict { case running, cancel, giveUp }
        static let stallSeconds: TimeInterval = 180
        static let giveUpSeconds: TimeInterval = 15
        /// kExportSafeModeMax e kExportRetryShift (ExportWatchdog.hpp).
        static let safeModeMax: UInt32 = 2
        static let retryShift: UInt32 = 16
        private var started = false
        private var lastDone = 0
        private var lastMessage = ""
        private var lastChange: TimeInterval = 0
        private var cancelAt: TimeInterval = 0
        private(set) var cancelled = false

        mutating func observe(now: TimeInterval, framesDone: Int, message: String) -> Verdict {
            if !started || framesDone != lastDone || message != lastMessage {
                started = true
                lastDone = framesDone
                lastMessage = message
                lastChange = now
            }
            if !cancelled {
                if now - lastChange < Self.stallSeconds { return .running }
                cancelled = true
                cancelAt = now
                return .cancel
            }
            return now - cancelAt >= Self.giveUpSeconds ? .giveUp : .running
        }

        /// O modo de segurança em que o motor manda refazer (bits 16..17).
        static func suggestedSafeMode(flags: UInt32) -> UInt32 { (flags >> retryShift) & 0x3 }
        /// Só sobe (nunca repete nem volta) e até o máximo; 0 = não refazer.
        static func nextSafeMode(current: UInt32, suggested: UInt32) -> UInt32 {
            suggested > current && suggested <= safeModeMax ? suggested : 0
        }
    }

    /// Texto do motivo da falha (aurea::ExportFailure, export/ExportRules.hpp)
    /// no idioma do app — os MESMOS códigos e chaves do Exporter.kt. nil = o
    /// motor não deu motivo.
    static func exportFailureReason(_ progress: [String: Any]) -> String? {
        let code = (progress[AureaExportFailure] as? NSNumber)?.intValue ?? 0
        switch code {
        case 1: return AureaText.t("expfail_encoder")
        case 2: return AureaText.t("expfail_encoder_stalled")
        case 3: return AureaText.t("expfail_render")
        case 4: return AureaText.t("expfail_memory")
        case 5: return AureaText.t("expfail_media")
        case 6: return AureaText.t("expfail_file")
        case 7: return AureaText.t("msg_sem_espaco_no_aparelho_libere_espaco")
        case 8: return AureaText.t("expfail_unsupported")
        default: return nil
        }
    }

    private func startExportPolling() {
        exportTimer?.invalidate()
        let timer = Timer(timeInterval: 0.35, repeats: true) { [weak self] _ in
            Task { @MainActor in
                guard let self else { return }
                self.exportProgress = self.engine.exportProgress()
                let running = (self.exportProgress["running"] as? NSNumber)?.boolValue ?? false
                let finished = (self.exportProgress["finished"] as? NSNumber)?.boolValue ?? false
                if !finished {
                    // A última rede (ExportStallWatch): nada muda há minutos →
                    // cancela; nem assim conclui → a tela desiste e se libera.
                    let done = (self.exportProgress["framesDone"] as? NSNumber)?.intValue ?? 0
                    let message = self.exportProgress["message"] as? String ?? ""
                    switch self.exportWatch.observe(now: ProcessInfo.processInfo.systemUptime, framesDone: done, message: message) {
                    case .cancel:
                        self.exportStalled = true
                        self.engine.cancelExport()
                    case .giveUp:
                        self.exportTimer?.invalidate()
                        self.exportTimer = nil
                        self.pendingExportURL = nil
                        self.exporting = false
                        self.exportStalled = true
                        self.exportMessage = AureaText.t("ios_export_failed_detail", AureaText.t("expfail_stuck"))
                        self.toast = self.exportMessage
                        return
                    case .running:
                        break
                    }
                }
                if !running && finished {
                    self.exportTimer?.invalidate()
                    self.exportTimer = nil
                    let result = (self.exportProgress["result"] as? NSNumber)?.intValue ?? 0
                    // O encoder travou/recusou e o motor sugere o modo de
                    // segurança: refaz o vídeo do começo, com o aviso (nada de
                    // "falhou" na tela).
                    if result != 0, self.exportedKind == .video, !self.exportCancelled, !self.exportWatch.cancelled,
                       let url = self.pendingExportURL {
                        let flags = (self.exportProgress["flags"] as? NSNumber)?.uint32Value ?? 0
                        let next = ExportStallWatch.nextSafeMode(current: self.exportSafeMode,
                                                                 suggested: ExportStallWatch.suggestedSafeMode(flags: flags))
                        if next > 0 && self.startVideoExport(url, safeMode: next) {
                            self.exportSafeMode = next
                            self.exportWatch = ExportStallWatch()
                            self.exportProgress = [:]
                            self.startExportPolling()
                            return
                        }
                    }
                    self.exportedURL = result == 0 ? self.pendingExportURL : nil
                    self.pendingExportURL = nil
                    if let url = self.exportedURL {
                        self.exportCancelled = false
                        self.publishExportToPhotos(url)
                    } else {
                        self.exporting = false
                        if !self.exportCancelled {
                            let message = self.exportProgress["message"] as? String ?? ""
                            // A causa no log (o relato do problema lê o log do app).
                            let failure = (self.exportProgress[AureaExportFailure] as? NSNumber)?.intValue ?? 0
                            let done = (self.exportProgress["framesDone"] as? NSNumber)?.intValue ?? 0
                            let total = (self.exportProgress["framesTotal"] as? NSNumber)?.intValue ?? 0
                            NSLog("%@", "AureaExport: falhou motivo=\(failure) codigo=\(result) quadro=\(done)/\(total) " +
                                  "seguranca=\(self.exportSafeMode) travou=\(self.exportStalled) motor=\(message)")
                            // O motivo (código estável do motor) vira texto do
                            // catálogo; a frase crua do motor (português) não
                            // vai para a tela.
                            self.exportMessage = self.exportStalled ? AureaText.t("ios_export_failed_detail", AureaText.t("expfail_stuck"))
                                : result == 28 ? AureaText.t("msg_sem_espaco_no_aparelho_libere_espaco")
                                : Self.exportFailureReason(self.exportProgress).map { AureaText.t("ios_export_failed_detail", $0) }
                                ?? (message.isEmpty || AureaText.language.resolved != .pt ? AureaText.t("ios_export_failed") : message)
                            self.toast = self.exportMessage
                        }
                    }
                }
            }
        }
        RunLoop.main.add(timer, forMode: .common)
        exportTimer = timer
    }

    /// MediaStore's iOS counterpart: request add-only access after a real export.
    /// The Documents copy remains available for share/open even when Photos is denied.
    private func publishExportToPhotos(_ url: URL) {
        exportPublishing = true
        // A sequência .zip não vai para Fotos: fica em Arquivos (Documents) e
        // sai pela folha de compartilhamento.
        if exportedKind == .sequence {
            exportPublishing = false; exporting = false
            exportMessage = AureaText.t("exp2_saved_files")
            toast = AureaText.t("exp2_done_sequence")
            AureaAdsManager.shared.showExportInterstitialIfAvailable {}
            return
        }
        let kind = exportedKind
        let readyKey = kind == .video ? "editor_video_pronto" : kind == .gif ? "exp2_done_gif" : "exp2_done_frame"
        PHPhotoLibrary.requestAuthorization(for: .addOnly) { [weak self] status in
            guard status == .authorized || status == .limited else {
                Task { @MainActor in
                    self?.exportPublishing = false; self?.exporting = false
                    self?.exportMessage = AureaText.t("ios_export_saved_allow_photos")
                    self?.toast = AureaText.t(readyKey)
                    // Ponto seguro: o render acabou e o vídeo já está salvo. Não segura nada.
                    AureaAdsManager.shared.showExportInterstitialIfAvailable {}
                }
                return
            }
            PHPhotoLibrary.shared().performChanges({
                // PNG e GIF entram pelo arquivo (o GIF continua animado em Fotos).
                if kind == .video { PHAssetChangeRequest.creationRequestForAssetFromVideo(atFileURL: url) }
                else { PHAssetChangeRequest.creationRequestForAssetFromImage(atFileURL: url) }
            }) { saved, error in
                Task { @MainActor in
                    self?.exportSavedToPhotos = saved
                    self?.exportPublishing = false; self?.exporting = false
                    // Confirmação visível: o vídeo está no Fotos (ou o porquê de não estar).
                    if !saved { NSLog("AureaExport: Photos save failed: %@", error?.localizedDescription ?? "-") }
                    self?.exportMessage = saved ? AureaText.t("ios_export_saved_photos")
                        : AureaText.t("ios_export_photos_failed") + (error.map { " (\($0.localizedDescription))" } ?? "")
                    self?.toast = AureaText.t(readyKey)
                    AureaAdsManager.shared.showExportInterstitialIfAvailable {}
                }
            }
        }
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

    var previewBuffering: Bool { status.previewBufferStatus & 0x8000_0000 != 0 }
    @Published private(set) var previewBufferRanges: [Range<Int64>] = []
    @Published private(set) var localAiActivity: UInt32 = 0
    var previewTimelineCachedFrames: Int64 { previewBufferRanges.reduce(0) { $0 + $1.upperBound - $1.lowerBound } }
    private func refreshPreviewBufferRanges() {
        // Poll ranges even when the frame count stays equal: LRU replacement
        // moves the blue timeline segments without changing the count.
        let activity = engine.localAiStatus()
        if activity != localAiActivity { localAiActivity = activity }
        let pairs = engine.previewBufferRanges()
        var ranges: [Range<Int64>] = []
        ranges.reserveCapacity(pairs.count / 2)
        for index in stride(from: 0, to: pairs.count - pairs.count % 2, by: 2) {
            let start = pairs[index].int64Value, end = pairs[index + 1].int64Value
            if start >= 0 && end > start { ranges.append(start..<end) }
        }
        if ranges != previewBufferRanges { previewBufferRanges = ranges }
    }
    var previewBufferedFrames: Int { Int(status.previewBufferStatus & 0xff) }
    var previewBufferTarget: Int { Int((status.previewBufferStatus >> 8) & 0xff) }
    var previewBufferLimited: Bool { status.previewBufferStatus & 0x4000_0000 != 0 }
    /// Só enquanto o play espera o buffer; o "Prévia em memória" saiu do palco
    /// (atrapalhava) — a faixa da timeline já mostra o cache.
    var previewBufferLabel: String? {
        if previewBuffering { return AureaText.t("preview_buffer_preparing", previewBufferedFrames, previewBufferTarget) }
        return nil
    }

    // =========================================================================
    // --- casca do editor (SESSÃO B) ---
    //
    // O que o palco, as barras, o transporte e os menus precisam e a sessão
    // ainda não tinha: o MESMO recorte do `EditorStore` do Android. Nada aqui
    // guarda cópia do projeto — tudo é comando do motor (`aurea::Command`) ou
    // estado de APRESENTAÇÃO que o status do motor não carrega (o loop, o HUD,
    // o modo Edição, o obturador), exatamente como o store do Android.
    // =========================================================================

    /// Reprodução em loop: o status do motor não traz esse flag, então a sessão
    /// guarda o último pedido — é o que a casca pinta no play e marca no menu.
    @Published private(set) var looping = false
    /// HUD de desempenho na tela (DEV).
    @Published private(set) var hudVisible = false
    /// Modo Edição (timeline magnética).
    @Published private(set) var editMode = false
    /// Marcas do projeto (só os quadros; o motor guarda cor e tipo).
    @Published private(set) var markerFrames: [Int64] = []
    struct MarkerEditingFrame: Identifiable {
        let frame: Int64
        let color: UInt32
        let label: String
        /// Rascunho: a marca só nasce no Salvar (Cancelar não deixa marca perdida).
        var isNew = false
        var id: Int64 { frame }
    }
    @Published var markerEditingFrame: MarkerEditingFrame?

    /// Caixa da seleção escondida no preview (preferência do aparelho, como
    /// StagePrefs.kt): some o contorno, o centro e as setas X/Y; a camada segue
    /// escolhida e os gestos continuam valendo. O gizmo 3D fica.
    @Published var hideSelectionBox: Bool = UserDefaults.standard.bool(forKey: "aurea.stage.hideSelectionBox") {
        didSet { UserDefaults.standard.set(hideSelectionBox, forKey: "aurea.stage.hideSelectionBox") }
    }

    /// Composition-space anchor shared by the overlay and touch hit test.
    var previewMarkerAnchor: SIMD2<Float>? {
        guard selection.count == 1, let row = selectedLayer, row.visible, !row.locked,
              status.playhead >= Int64(row.startFrame), status.playhead < Int64(row.endFrame),
              pointPick == nil, panel != .mask, panel != .vector, panel != .tracking,
              !ShapeStageGeometry.enabled(self) else { return nil }
        let gizmo = engine.gizmo(row.id, length: ShellStageGeometry.gizmoLength).map(\.floatValue)
        if gizmo.count == 8, gizmo[0].isFinite, gizmo[1].isFinite { return SIMD2(gizmo[0], gizmo[1]) }
        if hideSelectionBox { return nil }   // sem centro nem setas: nada a tocar
        let position = StageGeom.floats(detail["position"])
        guard position.count >= 2 else { return nil }
        let parent = StageGeom.floats(detail["parentAffine"])
        let x = parent.count == 6 ? parent[0] * position[0] + parent[2] * position[1] + parent[4] : position[0]
        let y = parent.count == 6 ? parent[1] * position[0] + parent[3] * position[1] + parent[5] : position[1]
        return x.isFinite && y.isFinite ? SIMD2(x, y) : nil
    }

    func editMarkerAtPlayhead() { openMarkerEditor(status.playhead) }

    /// Editor da marca em `frame`; sem marca ali, abre um rascunho que só vira
    /// marca no Salvar. Segurar a âncora e desistir não deixa marca "do nada".
    func openMarkerEditor(_ frame: Int64) {
        let values = engine.markers()
        if let index = stride(from: 0, to: values.count - values.count % 3, by: 3).first(where: { values[$0].int64Value == frame }) {
            markerEditingFrame = MarkerEditingFrame(frame: frame, color: values[index + 1].uint32Value, label: engine.markerLabel(frame))
        } else {
            markerEditingFrame = MarkerEditingFrame(frame: frame, color: 0xFFF7C34F, label: "", isNew: true)
        }
    }

    /// A marca mais próxima de `frame` a até `tolerance` frames.
    func markerNear(_ frame: Int64, tolerance: Int64) -> Int64? {
        markerFrames.filter { abs($0 - frame) <= tolerance }.min { abs($0 - frame) < abs($1 - frame) }
    }

    func setLooping(_ on: Bool) {
        guard looping != on else { return }
        looping = on
        engine.run { $0.setLoop(on) }
    }

    @Published private(set) var rawPlayback = false
    func toggleRawPlayback() {
        let next = !rawPlayback
        guard engine.setRawPlayback(next) else { toast = AureaText.t("ios_raw_test_needs_video"); return }
        rawPlayback = next
        hudVisible = true
    }
    func toggleHud() { hudVisible.toggle() }

    /// Marca (ou desmarca) com aviso e vibração, como no Android: marca
    /// silenciosa parecia ter aparecido "do nada".
    func toggleMarkerAt(_ frame: Int64) {
        // Música tocando: TAP → marca, TAP → marca… sem parar o som (EditorStore.markBeatLive).
        if status.playing != 0 {
            markBeatLive()
            return
        }
        // Sem pause/seek: o cabeçote já está no frame, e o seek era uma
        // descontinuidade (decoder e som re-preparados) que, com o status
        // atrasado, voltava ao cabeçote VELHO. Tocando, o motor marca no relógio.
        let target = min(max(0, frame), max(0, compositionDuration - 1))
        engine.toggleMarker(target)
        refreshModel(force: true)
        refreshMarkers()
        let on = markerFrames.contains(target)
        toast = AureaText.t(on ? "msg_marca_adicionada" : "msg_marca_removida")
        UIImpactFeedbackGenerator(style: .light).impactOccurred()
    }

    /// Marca de batida ao vivo: o motor lê o relógio do áudio no instante do
    /// toque. Não pausa, não faz seek, não alterna e não mostra toast.
    @discardableResult
    func markBeatLive() -> Bool {
        guard engine.markBeatLive() >= 0 else { return false }
        refreshMarkers()
        UIImpactFeedbackGenerator(style: .light).impactOccurred()
        return true
    }

    func editMarker(from: Int64, to: Int64, color: UInt32, label: String) -> Bool {
        let ok = engine.editMarker(from: from, to: to, color: color, label: label)
        if ok { refreshModel(force: true); refreshMarkers() }
        return ok
    }

    func deleteMarker(_ frame: Int64) {
        _ = engine.deleteMarker(frame)
        refreshModel(force: true)
        refreshMarkers()
    }

    func refreshMarkers() {
        let all = engine.markers()
        var frames: [Int64] = []
        var index = 0
        while index < all.count {
            frames.append(all[index].int64Value)
            index += 3
        }
        if frames != markerFrames { markerFrames = frames }
    }

    func seekToNextMarker() {
        let frames = markerFrames
        guard !frames.isEmpty else { return }
        let next = frames.first { $0 > status.playhead } ?? frames[0]
        seek(toFrame: next)
    }

    // --- Obturador / desfoque de movimento da composição ---------------------
    var motionBlurControls: MotionBlurControls { MotionBlurControls(engine.motionBlurSettings()) }
    var compMotionBlur: Bool { motionBlurControls.enabled }
    var shutterAngle: Float { motionBlurControls.angle }

    private func writeMotionBlur(_ settings: MotionBlurControls) {
        let changed = engine.setMotionBlurSettings(settings.enabled, shutter: settings.angle,
            phase: settings.phase, samples: settings.samples, adaptiveLimit: settings.adaptiveLimit)
        if changed { refreshModel(force: true) }
    }

    func setCompositionMotionBlur(_ on: Bool) {
        var settings = motionBlurControls
        settings.enabled = on
        writeMotionBlur(settings)
    }

    func changeShutterAngle(_ degrees: Float) {
        guard degrees.isFinite else { return }
        var settings = motionBlurControls
        settings.angle = min(720, max(0, degrees))
        writeMotionBlur(settings)
    }

    func changeShutterPhase(_ degrees: Float) {
        guard degrees.isFinite else { return }
        var settings = motionBlurControls
        settings.phase = min(360, max(-360, degrees))
        writeMotionBlur(settings)
    }

    func centerMotionBlurExposure() {
        var settings = motionBlurControls
        settings.phase = -settings.angle / 2
        writeMotionBlur(settings)
    }

    func changeMotionBlurSamples(_ value: Float) {
        guard value.isFinite else { return }
        var settings = motionBlurControls
        settings.samples = UInt32(min(Float(min(64, settings.adaptiveLimit)), max(2, value.rounded())))
        writeMotionBlur(settings)
    }

    func changeMotionBlurAdaptiveLimit(_ value: Float) {
        guard value.isFinite else { return }
        var settings = motionBlurControls
        settings.adaptiveLimit = UInt32(min(256, max(Float(settings.samples), value.rounded())))
        writeMotionBlur(settings)
    }

    // --- Modo Edição ---------------------------------------------------------
    func toggleEditMode() {
        let next = !engine.timelineEditMode
        engine.run { $0.setEditMode(next) }
        refreshModel(force: true)
    }

    /// LINHA MAGNÉTICA da camada: ligada, os cortes dela andam como faixa de
    /// montagem (aparar e apagar puxam os vizinhos DA MESMA linha; quem está em
    /// outra linha não anda). Um passo de desfazer.
    func setLayerMagneticTrack(_ id: Int64, _ on: Bool) {
        mutate { engine in _ = engine.setLayer(id, magneticTrack: on) }
        refreshModel(force: true)
        toast = AureaText.t(on ? "msg_linha_magnetica_ligada" : "msg_linha_magnetica_desligada")
    }

    /// Arrasta o trecho para outro ponto da mesma linha, com os vizinhos
    /// abrindo espaço e a fita voltando a ficar encostada — reordenar os cortes.
    @discardableResult
    func reorderClip(_ id: Int64, toFrame frame: Int64) -> Bool {
        var ok = false
        mutate { engine in ok = engine.reorderClip(id, toFrame: frame) }
        if ok { refreshModel(force: true) }
        return ok
    }

    /// Arrasto vertical de UM trecho na timeline (como no Alight Motion: só ele
    /// anda, nunca a linha inteira). mode 0 = fileira própria logo acima da
    /// fileira de `anchor` (0 = no fundo); 1 = entrar na linha de `anchor` se
    /// couber. Um passo de desfazer; false = o motor recusou (nada mudou).
    @discardableResult
    func moveLayerToRow(_ id: Int64, anchor: Int64, mode: Int) -> Bool {
        var ok = false
        mutate { engine in ok = engine.moveLayer(id, toRowOf: anchor, mode: Int32(mode)) }
        if ok { refreshModel(force: true) }
        return ok
    }

    func deleteSelectedLayers(ripple: Bool? = nil) {
        let targets = layers.filter { selection.contains($0.id) && !$0.locked }
        guard !targets.isEmpty else { toast = AureaText.t("editor_camada_bloqueada_desbloqueie_editar"); return }
        if status.playing != 0 { playPause() }
        let ids = targets.map { NSNumber(value: $0.id) }
        // A LINHA MAGNÉTICA manda: apagar um trecho dela fecha o buraco mesmo
        // no modo Composição. Sem nenhum trecho magnético, vale o modo Edição.
        if ripple ?? (engine.timelineEditMode || targets.contains { $0.magnetic }) { engine.rippleDeleteLayers(ids) }
        else { engine.deleteLayers(ids) }
        clearSelection()
        refreshModel(force: true)
    }

    // --- Camadas: leitura e tempo -------------------------------------------
    /// O detalhe de QUALQUER camada no cabeçote (transform avaliado, tamanho da
    /// mídia) — o `queryDetail` do store, usado pelo palco e pelos menus.
    func queryDetail(_ layerId: Int64) -> [String: Any]? { engine.layerDetail(layerId) }

    /// Camada vetorial: forma cujo tipo é o caminho editável (VECTOR_SHAPE_TYPE).
    var isVectorLayer: Bool {
        guard let kind = detail["kind"] as? NSNumber,
              let shape = detail["shapeTypePoints"] as? NSNumber else { return false }
        return kind.uint32Value == 5 && (shape.uint32Value & 0xFFFF) == 11
    }

    /// Vai ao keyframe anterior/seguinte da camada principal (A.01: |◀ ▶|).
    func stepTransport(_ direction: Int) {
        if !markerFrames.isEmpty {
            let now = status.playhead
            let target = direction > 0
                ? markerFrames.filter { $0 > now }.min()
                : markerFrames.filter { $0 < now }.max()
            if let target { seek(toFrame: target) }
        } else if !stepToKeyframe(direction) { step(Int32(clamping: direction)) }
    }

    func stepToKeyframe(_ direction: Int) -> Bool {
        guard let id = primarySelection, let d = engine.layerDetail(id),
              let start = (d["startFrame"] as? NSNumber)?.int64Value,
              let offset = (d["offsetFrames"] as? NSNumber)?.int64Value else { return false }
        let times = Set((keyframes[id] ?? []).map { Int64($0.time) + start - offset }).sorted()
        let now = status.playhead
        let target = direction > 0 ? times.first { $0 > now } : times.last { $0 < now }
        guard let target else { return false }
        seek(toFrame: target)
        return true
    }

    /// Reordena na vertical: `displayIndex` é a posição na timeline (0 = topo).
    func reorderLayer(_ layerId: Int64, displayIndex: Int) {
        let count = layers.count
        guard count > 0, let index = layers.firstIndex(where: { $0.id == layerId }) else { return }
        let clamped = min(max(displayIndex, 0), count - 1)
        guard clamped != index else { return }
        engine.run { $0.setLayerOrder(layerId, newIndex: UInt32(count - 1 - clamped)) }
        refreshModel(force: true)
    }

    /// "Escalonar" as escolhidas (o mesmo do Android): cascata de `step`
    /// quadros na ordem da timeline — a de cima fica; passo negativo = de
    /// baixo para cima. `keysOnly` anda só a animação. Um passo de desfazer.
    func staggerSelection(step: Int, keysOnly: Bool) {
        let ordered: [Int64] = layers.filter { selection.contains($0.id) && !$0.locked }.map { $0.id }
        guard ordered.count >= 2 else { toast = AureaText.t("sh_pick_two_unlocked_layers"); return }
        guard step != 0 else { return }
        let chain: [Int64] = step > 0 ? ordered : Array(ordered.reversed())
        let numbers: [NSNumber] = chain.map { NSNumber(value: $0) }
        if status.playing != 0 { playPause() }
        let moved: Int32 = engine.staggerLayers(numbers, stepFrames: Int32(abs(step)), keysOnly: keysOnly)
        if moved < 0 {
            toast = AureaText.t("msg_escalonar_recusado")
            return
        }
        refreshModel(force: true)
        let key: String = keysOnly ? "msg_keyframes_escalonados" : "msg_camadas_escalonadas"
        toast = AureaText.t(key, String(Int(moved) + 1), String(abs(step)))
    }

    /// Shared C++ timing rules, identical to Android's multi-selection actions.
    func arrangeLayerTimes(_ mode: UInt32) {
        let ids = layers.filter { selection.contains($0.id) && !$0.locked }.map { NSNumber(value: $0.id) }
        if status.playing != 0 { playPause() }
        let moved = engine.arrangeLayerTimes(ids, mode: mode, playhead: status.playhead)
        if moved < 0 { toast = AureaText.t("timeline_arrange_failed") }
        else { refreshModel(force: true) }
    }

    /// Move camadas no tempo (o conteúdo anda junto). Um passo de desfazer.
    func moveLayers(_ ids: [Int64], deltaFrames: Int) {
        guard deltaFrames != 0 else { return }
        let rows = layers.filter { ids.contains($0.id) }
        guard let minStart = rows.map({ Int($0.startFrame) }).min() else { return }
        let delta = max(deltaFrames, -minStart)
        guard delta != 0 else { return }
        mutate { engine in
            for row in rows {
                engine.setLayer(row.id, startFrame: Int32(Int(row.startFrame) + delta),
                                endFrame: Int32(Int(row.endFrame) + delta),
                                offsetFrames: row.offsetFrames, setOffset: false)
            }
        }
        refreshModel(force: true)
    }

    /// Divide as camadas escolhidas no cabeçote (as que o cobrem).
    func splitAtPlayhead(_ ids: [Int64]) {
        let frame = Int32(clamping: status.playhead)
        let targets = layers.filter { ids.contains($0.id) && frame > $0.startFrame && frame < $0.endFrame }
        guard !targets.isEmpty, started else { return }
        pendingSplit = (Set(layers.map(\.id)), targets, frame)
        mutate { engine in for row in targets { engine.splitLayer(row.id, atFrame: frame) } }
        // Sem `refreshModel(force:)` aqui: o corte só vale no próximo quadro do
        // motor, e a releitura imediata lia o modelo VELHO (camadas, keyframes e
        // composição inteiros pela ponte, logo quando o corte invalida o cache
        // do preview). Uma leitura de status ~2 quadros depois pega a revisão
        // nova antes do timer de 0,2 s; se ainda não chegou, o timer pega.
        Task { @MainActor [weak self] in
            try? await Task.sleep(nanoseconds: 34_000_000)
            self?.refreshStatus()
        }
    }

    /// Pedaço novo de um corte → dono das miniaturas do original (mesma mídia, mesma origem).
    private func resolvePendingSplit() {
        guard let p = pendingSplit else { return }
        let created = layers.filter { !p.before.contains($0.id) }
        guard !created.isEmpty else { return }
        pendingSplit = nil
        for piece in created where piece.startFrame == p.at {
            guard let from = p.targets.first(where: { o in
                o.kind == piece.kind && o.trackId == piece.trackId && o.endFrame == piece.endFrame &&
                    o.startFrame &- o.offsetFrames == piece.startFrame &- piece.offsetFrames
            }) else { continue }
            if thumbOwners.count >= 4096 { thumbOwners.removeAll() }
            thumbOwners[piece.id] = thumbOwner(from.id)
        }
    }

    /// O tempo da camada mudou (velocidade, rampa, ao contrário): volta a pedir as miniaturas dela.
    func unaliasThumbs(_ layer: Int64) { thumbOwners.removeValue(forKey: layer) }

    /// Trim do INÍCIO para `frame`: o conteúdo fica parado e só a borda anda.
    @discardableResult func trimStart(_ layerId: Int64, at frame: Int64) -> Bool {
        let changed = engine.editClipTime(layerId, operation: 0, amount: frame, previous: 0, next: 0)
        refreshModel(force: true)
        return changed
    }

    /// Puxa a camada INTEIRA para o cabeçote: o clipe anda, a duração não muda e
    /// o conteúdo anda junto (o deslocamento interno fica). É o "trazer para o
    /// cabeçote" da fileira rápida — aparar come a borda, dividir corta em dois,
    /// isto só move (e é justamente para quem está FORA do cabeçote).
    func moveToPlayhead(_ layerId: Int64) {
        _ = engine.editClipTime(layerId, operation: 6, amount: status.playhead, previous: 0, next: 0)
        refreshModel(force: true)
    }

    /// Trim do FIM para `frame`. O vídeo não passa do fim da mídia.
    @discardableResult func trimEnd(_ layerId: Int64, at frame: Int64) -> Bool {
        let changed = engine.editClipTime(layerId, operation: 1, amount: frame, previous: 0, next: 0)
        refreshModel(force: true)
        return changed
    }

    func editClipTime(_ operation: UInt32, amount: Int64, previous: Int64 = 0, next: Int64 = 0) {
        guard let id = primarySelection else { return }
        if status.playing != 0 { playPause() }
        if !engine.editClipTime(id, operation: operation, amount: amount, previous: previous, next: next) {
            toast = AureaText.t("ios_clip_edit_failed")
        }
        refreshModel(force: true)
    }

    // --- Parentesco ----------------------------------------------------------
    /// Liga (ou solta, `parent` = 0) o pai. O motor compensa: a camada fica
    /// onde está na tela e passa a seguir o pai dali em diante.
    func setParent(_ layerId: Int64, parent: Int64) {
        engine.run { $0.setLayer(layerId, parent: parent) }
        refreshModel(force: true)
    }

    /// Vários filhos para o mesmo pai (0 = soltar), num passo de desfazer.
    func setParentMany(_ ids: [Int64], parent: Int64) {
        let children = ids.filter { id in
            id != parent && (parent == 0 || parentCandidatesForAll([id]).contains { $0.id == parent })
        }
        guard !children.isEmpty else { return }
        beginGesture(parent == 0 ? "soltar camadas" : "vincular camadas")
        mutate { engine in for id in children { engine.setLayer(id, parent: parent) } }
        endGesture()
        refreshModel(force: true)
    }

    /// "Vincular a novo nulo" (o mesmo do Android): o motor cria um nulo no
    /// centro das camadas (3D se alguma é 3D) e faz dele o pai de todas — nada
    /// sai do lugar na tela. Um passo de desfazer; o nulo novo fica selecionado.
    func parentSelectionToNewNull() {
        parentSelectionToNewNull(Array(selection))
    }

    func parentSelectionToNewNull(_ ids: [Int64]) {
        guard !ids.isEmpty else { return }
        let numbers: [NSNumber] = ids.map { NSNumber(value: $0) }
        let created: Int64 = engine.parent(toNewNull: numbers)
        if created < 0 {
            toast = AureaText.t("msg_nao_foi_possivel_criar_o_nulo", String(-created))
            return
        }
        refreshModel(force: true)
        select(layerId: created)
        var linked: Int = 0
        for id in ids {
            let parent: Int64 = (engine.layerDetail(id)?["parentId"] as? NSNumber)?.int64Value ?? 0
            if parent == created { linked += 1 }
        }
        toast = AureaText.t("msg_camadas_seguindo_novo_nulo", String(linked))
    }

    /// Pais possíveis para TODAS as escolhidas: nenhuma delas nem descendente.
    func parentCandidatesForAll(_ ids: [Int64]) -> [LayerItem] {
        guard !ids.isEmpty else { return [] }
        var allowed: Set<Int64>?
        for id in ids {
            let candidates = Set(parentCandidates(Set([id])).map(\.id))
            allowed = allowed.map { $0.intersection(candidates) } ?? candidates
        }
        let ok = (allowed ?? []).subtracting(ids)
        return layers.filter { ok.contains($0.id) }
    }

    /// Pais possíveis de uma escolha (o mesmo ciclo do `parentCandidates`).
    func parentCandidates(_ ids: Set<Int64>) -> [LayerItem] {
        layers.filter { candidate in
            var current = candidate
            var visited = Set<Int64>()
            while visited.insert(current.id).inserted {
                if ids.contains(current.id) { return false }
                let parent = (engine.layerDetail(current.id)?["parentId"] as? NSNumber)?.int64Value ?? 0
                guard let next = layers.first(where: { $0.id == parent }) else { return true }
                current = next
            }
            return false
        }
    }

    // --- Grupo ---------------------------------------------------------------
    func openGroup(_ layerId: Int64) {
        guard engine.openPrecomp(layerId) else { return }
        selection = []
        refreshModel(force: true)
    }

    func selectAll() {
        timelineOnlySelection = []
        clearTimelineKeySelection()
        let ids = layers.map { NSNumber(value: $0.id) }
        guard !ids.isEmpty else { return }
        engine.run { $0.selectLayers(ids) }
        selection = Set(layers.map(\.id))
        refreshSelectedLayer()
    }

    // --- Gestos e transform do palco ----------------------------------------
    /// Um gesto contínuo (arrasto, alça, pinça) = UM passo de desfazer. Abra no
    /// começo e feche no fim; tudo que for enviado no meio desfaz junto.
    func beginGesture(_ label: String) {
        if transformGestureDepth == 0 { sceneDragSnapshot = nil }
        transformGestureDepth += 1
        engine.run { $0.beginUndoGroup() }
    }

    func endGesture() {
        transformGestureDepth = max(0, transformGestureDepth - 1)
        if transformGestureDepth == 0 { sceneDragSnapshot = nil }
        engine.run { $0.endUndoGroup() }
        refreshModel(force: true)
    }

    /// Muda UMA propriedade de transform, com semântica de keyframe: animada
    /// cria/atualiza o keyframe no cabeçote, senão muda o valor fixo. Rotação
    /// Animated 3D vectors always key XYZ together at the layer-local playhead.
    private func keyTransformGroup(_ id: Int64, detail d: [String: Any], changes: [UInt32: Float]) -> Bool {
        guard autoKeyTransforms, let first = changes.keys.first, first < 12 else { return false }
        let base = first / 3 * 3
        guard changes.keys.allSatisfy({ $0 >= base && $0 < base + 3 }) else { return false }
        let mask = (d["animatedMask"] as? NSNumber)?.uint32Value ?? 0
        let row = layers.first { $0.id == id }
        let position = StageGeom.floats(d["position"]), rotation = StageGeom.floats(d["rotation"])
        let threeD = row?.threeD == true || [8, 9, 10].contains(row?.kind ?? 0) ||
            (position.count >= 3 && abs(position[2]) > 0.01) ||
            (rotation.count >= 2 && (abs(rotation[0]) > 0.01 || abs(rotation[1]) > 0.01)) || mask & ((1 << 2) | (1 << 6) | (1 << 7)) != 0
        guard threeD, mask & (UInt32(7) << base) != 0 else { return false }
        let current = StageGeom.floats(d[["position", "scale", "rotation", "anchor"][Int(base / 3)]])
        guard current.count >= 3 else { return false }
        let values = (0..<3).map { changes[base + UInt32($0)] ?? current[$0] }
        // O quadro é o que a prévia mostra: o motor resolve (Command.hpp kAutoKey*).
        mutate { core in
            core.beginUndoGroup()
            for axis in 0..<3 { core.gestureKeyframe(forLayer: id, property: base + UInt32(axis), value: values[axis], wholeGroup: true) }
            core.endUndoGroup()
        }
        refreshSelectedLayer()
        return true
    }

    /// Mover o pivô (par do EditorStore.setPivot): âncora XYZ e posição XYZ de
    /// uma vez — a posição compensa a âncora e a imagem não pula.
    func setPivot(_ layer: Int64, anchor: [Float], position: [Float]) {
        guard anchor.count == 3, position.count == 3, (anchor + position).allSatisfy(\.isFinite),
              let d = engine.layerDetail(layer) else { return }
        let animated = (d["animatedMask"] as? NSNumber)?.uint32Value ?? 0
        for (base, values) in [(UInt32(9), anchor), (UInt32(0), position)] {
            let changes = Dictionary(uniqueKeysWithValues: (0..<3).map { (base + UInt32($0), values[$0]) })
            if keyTransformGroup(layer, detail: d, changes: changes) { continue }
            let keyed = (0..<3).contains { animated & (UInt32(1) << (base + UInt32($0))) != 0 }
            mutate { core in
                if transformLayout(animated: keyed) {
                    for axis in 0..<3 { core.layoutTransform(layer, property: base + UInt32(axis), value: values[axis]) }
                } else if keyed {
                    for axis in 0..<3 { core.gestureKeyframe(forLayer: layer, property: base + UInt32(axis), value: values[axis], wholeGroup: true) }
                } else if base == 9 {
                    core.setAnchor(forLayer: layer, x: values[0], y: values[1], z: values[2])
                } else {
                    core.setPosition(forLayer: layer, x: values[0], y: values[1], z: values[2])
                }
            }
        }
        refreshSelectedLayer()
    }

    func setTransform(_ property: UInt32, value: Float, layer: Int64) {
        guard value.isFinite, let d = engine.layerDetail(layer) else { return }
        if keyTransformGroup(layer, detail: d, changes: [property: value]) { return }
        let animated = (d["animatedMask"] as? NSNumber)?.uint32Value ?? 0
        if property < 15 && transformLayout(animated: property < 32 && animated & (1 << property) != 0) {
            mutate { $0.layoutTransform(layer, property: property, value: value) }
            refreshSelectedLayer(); return
        }
        if property < 15 {
            // Auto-Key ligado: o MOTOR decide pela trilha viva — animada ganha
            // chave no quadro da prévia, parada muda o valor (par do
            // EditorStore.setTransform do Android).
            mutate { $0.gestureKeyframe(forLayer: layer, property: property, value: value, wholeGroup: false) }
            refreshSelectedLayer(); return
        }
        let local = localFrame(for: layer)
        let position = StageGeom.floats(d["position"])
        let scale = StageGeom.floats(d["scale"])
        let rotation = StageGeom.floats(d["rotation"])
        let anchor = StageGeom.floats(d["anchor"])
        func component(_ values: [Float], _ index: Int) -> Float { values.count > index ? values[index] : 0 }
        if property < 32 && animated & (1 << property) != 0 {
            mutate { engine in engine.autoKeyframe(forLayer: layer, property: property, time: local, value: value) }
        } else {
            mutate { engine in
                switch property {
                case 0: engine.setPosition(forLayer: layer, x: value, y: component(position, 1), z: component(position, 2))
                case 1: engine.setPosition(forLayer: layer, x: component(position, 0), y: value, z: component(position, 2))
                case 2: engine.setPosition(forLayer: layer, x: component(position, 0), y: component(position, 1), z: value)
                case 3: engine.setScale(forLayer: layer, x: value, y: component(scale, 1), z: component(scale, 2))
                case 4: engine.setScale(forLayer: layer, x: component(scale, 0), y: value, z: component(scale, 2))
                case 5: engine.setScale(forLayer: layer, x: component(scale, 0), y: component(scale, 1), z: value)
                case 6: engine.setRotation(forLayer: layer, x: value, y: component(rotation, 1), z: component(rotation, 2))
                case 7: engine.setRotation(forLayer: layer, x: component(rotation, 0), y: value, z: component(rotation, 2))
                case 8: engine.setRotation(forLayer: layer, x: component(rotation, 0), y: component(rotation, 1), z: value)
                case 9: engine.setAnchor(forLayer: layer, x: value, y: component(anchor, 1), z: component(anchor, 2))
                case 10: engine.setAnchor(forLayer: layer, x: component(anchor, 0), y: value, z: component(anchor, 2))
                case 11: engine.setAnchor(forLayer: layer, x: component(anchor, 0), y: component(anchor, 1), z: value)
                case 12: engine.setOpacity(forLayer: layer, value: value)
                default: break
                }
            }
        }
        refreshSelectedLayer()
    }

    /// Duas propriedades de uma vez (o arrasto da camada no palco: X e Y).
    func setTransform2(_ pa: UInt32, _ va: Float, _ pb: UInt32, _ vb: Float, layer: Int64) {
        guard va.isFinite, vb.isFinite, let d = engine.layerDetail(layer) else { return }
        if keyTransformGroup(layer, detail: d, changes: [pa: va, pb: vb]) { return }
        let animated = (d["animatedMask"] as? NSNumber)?.uint32Value ?? 0
        let isAnimated = (pa < 32 && animated & (1 << pa) != 0) || (pb < 32 && animated & (1 << pb) != 0)
        if pa < 15 && pb < 15 && transformLayout(animated: isAnimated) {
            mutate { $0.layoutTransform(layer, property: pa, value: va); $0.layoutTransform(layer, property: pb, value: vb) }
            refreshSelectedLayer(); return
        }
        if pa < 15 && pb < 15 {
            // Arrastar no palco/almofada com Auto-Key: o MOTOR decide por trilha
            // (chave no quadro que a prévia mostra ou valor parado). Antes o
            // detalhe lido aqui decidia: atrasado, o texto animado recebia o
            // valor parado, que a animação esconde, e não andava. Um passo de desfazer.
            mutate { core in
                core.beginUndoGroup()
                core.gestureKeyframe(forLayer: layer, property: pa, value: va, wholeGroup: false)
                core.gestureKeyframe(forLayer: layer, property: pb, value: vb, wholeGroup: false)
                core.endUndoGroup()
            }
            refreshSelectedLayer(); return
        }
        let local = localFrame(for: layer)
        let position = StageGeom.floats(d["position"])
        let scale = StageGeom.floats(d["scale"])
        func component(_ values: [Float], _ index: Int) -> Float { values.count > index ? values[index] : 0 }
        mutate { engine in
            if isAnimated {
                engine.autoKeyframe(forLayer: layer, property: pa, time: local, value: va)
                engine.autoKeyframe(forLayer: layer, property: pb, time: local, value: vb)
            } else if pa == 0 && pb == 1 {
                engine.setPosition(forLayer: layer, x: va, y: vb, z: component(position, 2))
            } else if pa == 3 && pb == 4 {
                engine.setScale(forLayer: layer, x: va, y: vb, z: component(scale, 2))
            } else if pa == 9 && pb == 10 {
                engine.setAnchor(forLayer: layer, x: va, y: vb, z: 0)
            }
        }
        refreshSelectedLayer()
    }

    /// Leva o palco ao ponto pedido (usado pela mira do rastreio).
    func maskCompPoint(_ point: CGPoint) -> (Float, Float)? {
        guard let id = primarySelection else { return nil }
        let a = engine.maskData(id).prefix(6).map(\.floatValue)
        guard a.count == 6 else { return nil }
        let det = a[0] * a[3] - a[1] * a[2]
        guard abs(det) > 0.000001 else { return nil }
        let x = Float(point.x) - a[4], y = Float(point.y) - a[5]
        return ((a[3] * x - a[2] * y) / det, (-a[1] * x + a[0] * y) / det)
    }
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

// =============================================================================
// Ações da seleção de keyframes da timeline (as mesmas do EditorStore.kt)
// =============================================================================
extension AureaModel {
    func clearTimelineKeySelection() {
        if timelineKeySelection != nil { timelineKeySelection = nil }
        if timelineKeySelectMode { timelineKeySelectMode = false }
    }

    /// Toque simples num losango: só ele fica escolhido.
    func tapTimelineKey(_ layer: Int64, _ key: KeyframeItem) {
        timelineKeySelection = TimelineKeySelection.single(layer, key)
        timelineKeySelectMode = false
    }

    /// Liga/desliga o modo "Selecionar". Ligar fecha o painel (a timeline volta
    /// alta, com as trilhas abertas) e deixa a camada escolhida só na timeline.
    func changeTimelineKeySelectMode(_ on: Bool) {
        if !on { timelineKeySelectMode = false; return }
        timelineLayerSelectMode = false
        let base: TimelineKeySelection
        if let current = timelineKeySelection { base = current }
        else if let id = primarySelection { base = TimelineKeySelection(layer: id) }
        else { return }
        select(layerId: base.layer, additive: false, openOptions: false)
        panel = .none
        timelineKeySelection = base
        timelineKeySelectMode = true
    }

    /// Liga/desliga "Selecionar várias camadas". Ligar fecha o painel e a
    /// escolha de keyframes; desligar ("Concluir") mantém a seleção.
    func changeTimelineLayerSelectMode(_ on: Bool) {
        if on {
            clearTimelineKeySelection()
            panel = .none
        }
        timelineLayerSelectMode = on
    }

    /// Toque num clipe no modo de escolha: soma/tira (com a doca/lote da seleção).
    func toggleTimelineLayerPick(_ layer: Int64) {
        select(layerId: layer, additive: true)
    }

    /// Modo de escolha: alterna o grupo do losango (1 keyframe numa trilha; o
    /// instante inteiro no resumo). No modo "Selecionar", losangos de OUTRA
    /// camada somam à seleção (várias camadas, como no app antigo) sem trocar a
    /// escolhida; fora dele, outra camada começa outra seleção.
    func toggleTimelineKeys(_ layer: Int64, _ group: [KeyframeItem]) {
        if group.isEmpty { return }
        let mode = timelineKeySelectMode
        if mode, let current = timelineKeySelection, current.layer != layer {
            timelineKeySelection = current.toggledGroup(on: layer, group)
            return
        }
        if primarySelection != layer || selection.count != 1 {
            select(layerId: layer, additive: false, openOptions: false)
        }
        timelineKeySelectMode = mode
        var base = TimelineKeySelection(layer: layer)
        if let current = timelineKeySelection, current.layer == layer { base = current }
        timelineKeySelection = base.toggledGroup(group)
    }

    func selectAllTimelineKeys() {
        guard let layer = timelineKeySelection?.layer ?? primarySelection else { return }
        let keys: [KeyframeItem] = keyframes[layer] ?? []
        // As outras camadas já escolhidas continuam na seleção.
        var next = TimelineKeySelection.all(layer, keys, focus: timelineFocus)
        if let current = timelineKeySelection, current.layer == layer { next.others = current.others }
        timelineKeySelection = next
    }

    /// Move a seleção inteira (atômico no motor: recusa colisão com keyframe
    /// não escolhido). Só atualiza as referências se o motor aceitou.
    @discardableResult
    func shiftTimelineKeys(_ delta: Int32) -> Bool {
        guard let sel = timelineKeySelection, !sel.isEmpty, delta != 0 else { return false }
        // Várias camadas: cada uma é atômica no motor; se uma recusa (colisão),
        // as que já andaram voltam — tudo ou nada, no mesmo passo de desfazer.
        let back = sel.shifted(delta)
        var moved: [Int64] = []
        for id in sel.layerIds {
            let changed: UInt32 = engine.keyframeSelection(id, references: sel.references(on: id), action: 1, delta: delta)
            if changed > 0 { moved.append(id); continue }
            for done in moved { _ = engine.keyframeSelection(done, references: back.references(on: done), action: 1, delta: -delta) }
            if !moved.isEmpty { refreshModel(force: true) }
            return false
        }
        timelineKeySelection = back
        refreshModel(force: true)
        return true
    }

    /// Copiar animação (app antigo): todos os keyframes da camada escolhida.
    func copyAnimation() {
        guard let id = primarySelection else { return }
        let copied = engine.copyAnimation(id)
        toast = copied > 0 ? AureaText.t("msg_keyframe_s_copiado_s", NSString(string: String(copied))) : AureaText.t("msg_camada_sem_keyframes")
    }

    /// Otimizar keyframes: só a propriedade dos keyframes escolhidos (se for
    /// uma), senão todas; 1 % da amplitude de cada uma. Um passo de desfazer.
    func optimizeKeyframes() {
        guard let id = primarySelection else { return }
        var property: Int32 = -1
        if let sel = timelineKeySelection, sel.layer == id, !sel.isEmpty {
            let props = Set(sel.keys.map(\.property))
            if props.count == 1, let only = props.first { property = Int32(only) }
        }
        let removed = engine.optimizeKeyframes(id, property: property, tolerance: 0.01)
        if removed > 0 { timelineKeySelection = nil }
        refreshModel(force: true)
        toast = removed > 0 ? AureaText.t("msg_keyframes_otimizados", NSString(string: String(removed))) : AureaText.t("msg_nada_otimizar")
    }

    func copyTimelineKeys() {
        guard let sel = timelineKeySelection, !sel.isEmpty else { return }
        let copied: UInt32 = engine.keyframeSelection(sel.layer, references: sel.references(), action: 0, delta: 0)
        if copied > 0 {
            let count: NSString = NSString(string: String(copied))
            toast = AureaText.t("msg_keyframe_s_copiado_s", count)
        }
    }

    /// Colar no cabeçote (o caminho de sempre), na camada da seleção.
    func pasteTimelineKeys() {
        guard let layer = timelineKeySelection?.layer else { return }
        engine.pasteKeyframes([NSNumber(value: layer)], atFrame: Int32(clamping: status.playhead))
        refreshModel(force: true)
    }

    /// Duplicar = copiar + colar 1 frame depois do último escolhido; as cópias viram a seleção.
    func duplicateTimelineKeys() {
        guard let sel = timelineKeySelection, let delta = sel.duplicateDelta(),
              let row = layers.first(where: { $0.id == sel.layer }) else { return }
        let target: Int64 = Int64(sel.maxTime) + 1 + Int64(row.startFrame) - Int64(row.offsetFrames)
        if target < Int64(Int32.min) || target > Int64(Int32.max) { return }
        let copied: UInt32 = engine.keyframeSelection(sel.layer, references: sel.references(), action: 0, delta: 0)
        if copied == 0 { return }
        if status.playing != 0 { playPause() }
        engine.pasteKeyframes([NSNumber(value: sel.layer)], atFrame: Int32(target))
        timelineKeySelection = sel.shifted(delta)
        refreshModel(force: true)
    }

    /// Excluir a seleção inteira (um passo de desfazer); a barra fecha.
    func deleteTimelineKeys() {
        guard let sel = timelineKeySelection, !sel.isEmpty else { return }
        let many: Bool = sel.crossLayer
        if many { engine.run { $0.beginUndoGroup() } }
        var removed: UInt32 = 0
        for id in sel.layerIds {
            removed += engine.keyframeSelection(id, references: sel.references(on: id), action: 2, delta: 0)
        }
        if many { engine.run { $0.endUndoGroup() } }
        if removed == 0 { return }
        if let time = curveSelectedTime {
            let primary = TimelineKeyRef(property: curveProperty, effect: curveEffect, param: curveParam, time: time)
            if sel.keys.contains(primary) { curveSelectedTime = nil }
        }
        clearTimelineKeySelection()
        refreshModel(force: true)
    }

    /// A seleção some se a camada sumiu ou algum keyframe referido deixou de existir.
    func validateTimelineKeySelection() {
        guard let sel = timelineKeySelection else { return }
        let alive: Bool = layers.contains { $0.id == sel.layer }
        let ids = Set(layers.map(\.id))
        let valid = sel.validatedAll { id in ids.contains(id) ? (self.keyframes[id] ?? []) : nil }
        if !alive || valid == nil { clearTimelineKeySelection() }
    }
}

// =============================================================================
// Substituir mídia e arquivo do projeto (motor: EngineMedia.cpp e
// ProjectPackage.cpp; telas em ProjectTransfer.swift). Mesmo comportamento do
// Android (EditorStore.replaceMedia / exportProjectFile / importProjectFile).
// =============================================================================
extension AureaModel {
    /// "Substituir mídia": copia para Media/ e troca a fonte da camada. Um desfazer.
    func replaceMedia(layer: Int64, url: URL, video: Bool) {
        guard !importingMedia else { return }
        importingMedia = true; operationMessage = AureaText.t("ios_importing_media")
        let operation = beginProjectOperation()
        let scoped = url.startAccessingSecurityScopedResource()
        let destination = AureaPaths.mediaDestination(for: url.lastPathComponent)
        let name = url.deletingPathExtension().lastPathComponent
        let importer = engine
        if status.playing != 0 { engine.run { $0.pause() }; status.playing = 0 }
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            var result: Int64 = -1
            var failure = ""
            do {
                try AureaPaths.copyImport(url, to: destination)
                result = video ? importer.replaceLayer(layer, withVideo: destination.path, name: name)
                               : importer.replaceLayer(layer, withImageFile: destination.path, name: name)
                if result < 0 {
                    let raw = importer.lastImportError
                    failure = raw.isEmpty ? "" : AureaEngineText.reason(raw, code: Int(-result))
                    try? FileManager.default.removeItem(at: destination)
                }
            } catch { failure = error.localizedDescription }
            let replaced = result, reason = failure
            DispatchQueue.main.async {
                guard let self else { return }
                self.endProjectOperation(operation)
                self.importingMedia = false
                if replaced < 0 {
                    self.toast = AureaText.t("layer_replace_failed", reason.isEmpty ? String(-replaced) : reason)
                    return
                }
                self.refreshModel(force: true)
                self.select(layerId: replaced, openOptions: false)
                self.toast = AureaText.t("layer_media_replaced", name)
                _ = self.saveProject(writeThumbnail: false)
            }
        }
    }

    /// Arquivo de origem da camada (para "Informações da mídia"); "" = nenhum.
    func layerSourcePath(_ layer: Int64) -> String { engine.layerSourcePath(layer) }

    /// Mensagem de erro do arquivo do projeto pelo código do motor (`aurea::Errc`).
    static func projectFileError(_ code: Int) -> String {
        switch code {
        case 17: return AureaText.t("project_file_err_not_project")
        case 12: return AureaText.t("project_file_err_newer")
        case 11, 13: return AureaText.t("project_file_err_corrupt")
        case 28: return AureaText.t("msg_sem_espaco_no_aparelho_libere_espaco")
        default: return AureaText.t("project_file_err_unreadable")
        }
    }

    /// "Exportar arquivo do projeto": grava o pacote numa pasta temporária e
    /// devolve o arquivo para a folha de compartilhar (nil = falhou; o motivo vai no toast).
    func exportProjectFile(path: String, title: String, includeMedia: Bool, requireComplete: Bool = false, done: @escaping (URL?) -> Void) {
        if projectURL?.path == path { _ = saveProject(writeThumbnail: false) }
        let importer = engine
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
        let invalid = CharacterSet(charactersIn: "/\\:").union(.controlCharacters)
        let safe = title.components(separatedBy: invalid).joined(separator: "-").trimmingCharacters(in: .whitespaces)
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent("ArquivoProjeto", isDirectory: true)
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        let out = directory.appendingPathComponent((safe.isEmpty ? "Aurea" : safe) + ".aureaproj")
        toast = AureaText.t("project_file_exporting")
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            var code = 0, skipped = 0
            var media: [String] = []
            if includeMedia {
                if let refs = importer.projectFileMedia(path) {
                    var i = 0
                    while i + 3 < refs.count {
                        let resolved = refs[i + 1].hasPrefix("file://") ? String(refs[i + 1].dropFirst(7)) : refs[i + 1]
                        media += [refs[i], resolved, refs[i + 2]]
                        i += 4
                    }
                } else { code = 11 }
            }
            if code == 0 {
                try? FileManager.default.removeItem(at: directory)
                try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
                let r = AureaEngine.exportProjectPackage(path, to: out.path, title: title, appVersion: version, media: media)
                code = r.first?.intValue ?? 10
                skipped = r.count > 2 ? r[2].intValue : 0
            }
            let finalCode = code, finalSkipped = skipped
            if requireComplete && skipped > 0 { try? FileManager.default.removeItem(at: out) }
            DispatchQueue.main.async {
                guard let self else { return }
                if finalCode != 0 || (requireComplete && finalSkipped > 0) {
                    let reason = finalCode != 0 ? AureaModel.projectFileError(finalCode) : AureaText.t("project_file_err_unreadable")
                    self.toast = AureaText.t("project_file_export_failed", reason)
                    done(nil)
                    return
                }
                self.toast = finalSkipped > 0 ? AureaText.t("project_file_exported_skipped", finalSkipped) : AureaText.t("project_file_exported")
                done(out)
            }
        }
    }

    /// "Importar arquivo do projeto": sempre um projeto NOVO (nome livre), a
    /// mídia do pacote em Media/Projetos/<nome>/. `done` roda na thread principal.
    func importProjectFile(_ url: URL, done: @escaping () -> Void) {
        guard canChangeProject() else { done(); return }
        importingMedia = true; operationMessage = AureaText.t("project_file_importing")
        let scoped = url.startAccessingSecurityScopedResource()
        let fallback = url.deletingPathExtension().lastPathComponent
        let name = uniqueHomeProjectName(fallback.isEmpty ? AureaText.t("project_file_imported_title") : fallback)
        let project = AureaPaths.documents.appendingPathComponent(name + ".aurea")
        let mediaDir = AureaPaths.media.appendingPathComponent("Projetos", isDirectory: true).appendingPathComponent(name, isDirectory: true)
        let temporary = FileManager.default.temporaryDirectory.appendingPathComponent("entrada-\(UUID().uuidString).aureaproj")
        toast = AureaText.t("project_file_importing")
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            var r: [String] = ["10", "", "", "0", "0"]
            do {
                try AureaPaths.copyImport(url, to: temporary)
                r = AureaEngine.importProjectPackage(temporary.path, project: project.path, mediaDir: mediaDir.path)
            } catch {}
            try? FileManager.default.removeItem(at: temporary)
            let code = Int(r.first ?? "10") ?? 10
            let title = (r.count > 1 && !r[1].isEmpty) ? r[1] : name
            let missing = r.count > 4 ? Int(r[4]) ?? 0 : 0
            DispatchQueue.main.async {
                guard let self else { done(); return }
                self.importingMedia = false
                if code == 0 {
                    // Sidecar com o título (Home); tamanho e duração vêm na primeira abertura.
                    writeHomeProjectMeta(url: project, title: title, width: 0, height: 0, fps: 30, durationFrames: 0)
                }
                if code != 0 { self.toast = AureaModel.projectFileError(code) }
                else if missing > 0 { self.toast = AureaText.t("project_file_imported_missing", title, missing) }
                else { self.toast = AureaText.t("project_file_imported", title) }
                done()
            }
        }
    }
}

// =============================================================================
// Ajustar / preencher a tela, tamanho da composição e âncora predefinida
// (ferramentas do app antigo — LayerOps.kt no Android, mesma conta)
// =============================================================================
extension AureaModel {
    private struct FitGeom { let w: Float; let h: Float; let anchor: [Float]; let position: [Float]; let scale: [Float]; let rad: Float; let centered: Bool; let threeD: Bool; var kind: Int32 = 0 }

    private func fitGeom(_ id: Int64) -> FitGeom? {
        guard let d = queryDetail(id) else { return nil }
        let w = StageGeom.width(d), h = StageGeom.height(d)
        let anchor = StageGeom.floats(d["anchor"]), position = StageGeom.floats(d["position"]), scale = StageGeom.floats(d["scale"])
        let rotation = StageGeom.floats(d["rotation"])
        guard w > 0, h > 0, anchor.count >= 3, position.count >= 3, scale.count >= 2 else { return nil }
        let mask = (d["animatedMask"] as? NSNumber)?.uint32Value ?? 0
        let threeD = [8, 9, 10].contains(StageGeom.layerKind(d)) || layers.first(where: { $0.id == id })?.threeD == true ||
            abs(position[2]) > 0.01 || (rotation.count >= 2 && (abs(rotation[0]) > 0.01 || abs(rotation[1]) > 0.01)) ||
            mask & ((1 << 2) | (1 << 6) | (1 << 7)) != 0
        return FitGeom(w: w, h: h, anchor: anchor, position: position, scale: scale,
                       rad: (rotation.count > 2 ? rotation[2] : 0) * .pi / 180, centered: StageGeom.layerKind(d) == 10, threeD: threeD,
                       kind: Int32(StageGeom.layerKind(d)))
    }

    /// Posição que põe o CENTRO da mídia em (cx, cy) com a escala (sx, sy) e o giro atual.
    private func positionForCenter(_ g: FitGeom, sx: Float, sy: Float, cx: Float, cy: Float) -> (Float, Float) {
        let ax = g.anchor[0] + (g.centered ? g.w * 0.5 : 0), ay = g.anchor[1] + (g.centered ? g.h * 0.5 : 0)
        let dx = (g.w * 0.5 - ax) * sx, dy = (g.h * 0.5 - ay) * sy
        let c = cos(g.rad), s = sin(g.rad)
        return (cx - (dx * c - dy * s), cy - (dx * s + dy * c))
    }

    /// Ajustar à tela (cabe inteira) ou Preencher (cobre): escala uniforme,
    /// espelho preservado, centro da mídia no centro. Um passo de desfazer.
    func fitToCanvas(_ ids: [Int64], fill: Bool) {
        let cw = Float(compositionWidth), ch = Float(compositionHeight)
        let targets = ids.filter { id in !(layers.first { $0.id == id }?.locked ?? false) }.compactMap { id in fitGeom(id).map { (id, $0) } }
        guard cw > 0, ch > 0, !targets.isEmpty else { return }
        beginGesture(fill ? "preencher a tela" : "ajustar à tela")
        for (id, g) in targets {
            let values: [Float] = [cw, ch, g.w, g.h, g.scale[0], g.scale[1],
                g.anchor[0] + (g.centered ? g.w / 2 : 0), g.anchor[1] + (g.centered ? g.h / 2 : 0),
                // Match the layer renderer: stored skew does not affect placement.
                g.rad * 180 / .pi, 0, 0]
            let fit = engine.fitCanvas(values.map { NSNumber(value: $0) }, fill: fill).map(\.floatValue)
            guard fit.count == 5, fit[4] > 0 else { continue }
            let sx = fit[0], sy = fit[1], k = fit[4], p = (fit[2], fit[3])
            if g.threeD {
                // A regra do motor: Z de conteúdo é relativo a X (o volume não estica).
                gizmoSetComponents(id, base: 3, values: engine.gestureScale3D(g.kind, scaleX: g.scale[0], scaleY: g.scale[1],
                                                                              scaleZ: g.scale.count > 2 ? g.scale[2] : 1, axis: 4, factor: k).map(\.floatValue))
            } else { setTransform2(3, sx, 4, sy, layer: id) }
            setTransform2(0, p.0, 1, p.1, layer: id)
        }
        endGesture()
    }

    /// Tamanho da composição = o da camada (mídia × escala, pares); a camada vai para o centro.
    func makeCompositionSize(_ id: Int64) {
        guard let g = fitGeom(id), let comp = (composition["id"] as? NSNumber)?.uint64Value else { return }
        func even(_ v: Float) -> Int { max(2, Int((v / 2).rounded()) * 2) }
        let w = even(g.w * abs(g.scale[0])), h = even(g.h * abs(g.scale[1]))
        beginGesture("tamanho da composição")
        mutate { $0.setComposition(comp, width: UInt32(w), height: UInt32(h)) }
        let p = positionForCenter(g, sx: g.scale[0], sy: g.scale[1], cx: Float(w) / 2, cy: Float(h) / 2)
        setTransform2(0, p.0, 1, p.1, layer: id)
        endGesture()
    }

    /// Âncora predefinida: um dos 9 pontos (fx, fy ∈ 0, ½, 1) da mídia; a posição compensa.
    func presetAnchor(_ id: Int64, fx: Float, fy: Float) {
        guard let g = fitGeom(id) else { return }
        let ax = fx * g.w - (g.centered ? g.w * 0.5 : 0), ay = fy * g.h - (g.centered ? g.h * 0.5 : 0)
        let dx = (ax - g.anchor[0]) * g.scale[0], dy = (ay - g.anchor[1]) * g.scale[1]
        let c = cos(g.rad), s = sin(g.rad)
        beginGesture("âncora predefinida")
        setPivot(id, anchor: [ax, ay, g.anchor[2]], position: [g.position[0] + dx * c - dy * s, g.position[1] + dx * s + dy * c, g.position[2]])
        endGesture()
    }
}

// =============================================================================
// Formas 3D (Engine::add_shape3d e família) — as views estão em Shape3DViews.swift.
// =============================================================================
extension AureaModel {
    func addShape3D(kind: Int) {
        let key = kind < Shape3DState.names.count ? Shape3DState.names[kind] : "shape3d_title"
        let id = engine.addShape3D(kind: UInt32(kind), name: AureaText.t(key))
        if id >= 0 { selection = [id]; engine.selectLayers([NSNumber(value: id)]) }
        else { toast = AureaText.t("shape3d_add_failed") }
        showAddLayer = false
        refreshModel(force: true)
    }

    func shape3DInfo(_ id: Int64) -> Shape3DInfo? { Shape3DInfo(engine.shape3D(id).map(\.floatValue)) }

    /// Divide o cubo em `count` fatias no eixo `axis` (0 X, 1 Y, 2 Z) — Engine::split_shape3d.
    /// A camada vira um nulo 3D (mesmo id, mesmo movimento) com as fatias filhas; cada
    /// fatia é uma camada normal. A seleção fica no grupo. Um passo de desfazer.
    @discardableResult func splitShape3D(_ id: Int64, axis: Int, count: Int) -> Bool {
        let group = engine.splitShape3D(id, axis: UInt32(max(0, min(2, axis))), count: UInt32(max(0, count)))
        guard group >= 0 else {
            toast = AureaText.t("shape3d_split_failed")
            return false
        }
        Shape3DState.shared.part = -1
        Shape3DState.shared.revision += 1
        selection = [group]
        engine.selectLayers([NSNumber(value: group)])
        refreshModel(force: true)
        toast = AureaText.t("shape3d_split_done", count)
        return true
    }

    /// Os 9 canais da parte no cabeçote (posição, rotação °, escala) + bits de keyframe.
    func shapePartValues(_ id: Int64, part: Int) -> [Float]? {
        let p = engine.shape3DParts(id).map(\.floatValue)
        let o = part * Shape3DState.partFloats
        guard part >= 0, o + Shape3DState.partFloats <= p.count else { return nil }
        return Array(p[o..<(o + Shape3DState.partFloats)])
    }

    /// Grava os canais `mask` da parte (canal animado = keyframe no cabeçote).
    func setShapePart(_ id: Int64, part: Int, values: [Float], mask: UInt32, inGesture: Bool) {
        let state = Shape3DState.shared
        let continuing = inGesture && state.gestureSent
        if engine.setShape3DPart(id, part: Int32(part), values: values.map { NSNumber(value: $0) }, mask: mask, continuing: continuing), inGesture {
            state.gestureSent = true
        }
        state.revision += 1
        if !inGesture { refreshModel(force: true) }
    }

    func beginShapeGesture(_ label: String) { Shape3DState.shared.gestureSent = false; beginGesture(label) }
    func endShapeGesture() { Shape3DState.shared.gestureSent = false; endGesture() }

    /// Dedo no palco: a parte anda `dx`, `dy` px da composição no plano que a câmera vê de frente.
    func shapePartDragScreen(_ id: Int64, part: Int, dx: Float, dy: Float) {
        let g = engine.shape3DPartGizmo(id, part: Int32(part), length: ShellStageGeometry.gizmoLength, localSpace: false).map(\.floatValue)
        guard g.count == 8 else { return }
        let ax = (0..<3).map { g[($0 + 1) * 2] - g[0] }, ay = (0..<3).map { g[($0 + 1) * 2 + 1] - g[1] }
        var u = 0, v = 1, area: Float = -1
        for (i, j) in [(0, 1), (0, 2), (1, 2)] {
            let a = abs(ax[i] * ay[j] - ay[i] * ax[j])
            if a > area { area = a; u = i; v = j }
        }
        guard area >= 1 else { return }
        let det = ax[u] * ay[v] - ay[u] * ax[v]
        let a = (dx * ay[v] - dy * ax[v]) / det, b = (ax[u] * dy - ay[u] * dx) / det
        let base = engine.shape3DPartMove(id, part: Int32(part), axis: UInt32(u), amount: 0).map(\.floatValue)
        let pu = engine.shape3DPartMove(id, part: Int32(part), axis: UInt32(u), amount: a * ShellStageGeometry.gizmoLength).map(\.floatValue)
        let pv = engine.shape3DPartMove(id, part: Int32(part), axis: UInt32(v), amount: b * ShellStageGeometry.gizmoLength).map(\.floatValue)
        guard base.count == 3, pu.count == 3, pv.count == 3 else { return }
        var out = [Float](repeating: 0, count: 9)
        for i in 0..<3 { out[i] = pu[i] + pv[i] - base[i] }
        setShapePart(id, part: part, values: out, mask: 0b111, inGesture: true)
    }

    /// Imagem da galeria na parte (−1 = a forma inteira): normalizada (EXIF,
    /// HEIC → JPEG/PNG, até 2048 px) e gravada em Documentos/formas3d — o
    /// projeto guarda o caminho relativo.
    func setShapePartImage(_ id: Int64, part: Int, url: URL) {
        guard !importingMedia else { toast = AureaText.t("ios_importing_media"); return }
        importingMedia = true
        operationMessage = AureaText.t("ios_importing_media")
        let operation = beginProjectOperation()
        let project = projectGeneration
        let scoped = url.startAccessingSecurityScopedResource()
        let engine = self.engine
        mediaQueue.async { [weak self] in
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            var path: String?
            autoreleasepool {
                // ImageIO decodes directly at the requested size; Data + UIImage
                // decoded the full camera image before creating its 2048px copy.
                if let image = decodeHomeThumbnail(path: url.path, maxPx: 2048) {
                    let info = image.cgImage?.alphaInfo ?? CGImageAlphaInfo.none
                    let alpha = info != CGImageAlphaInfo.none && info != .noneSkipFirst && info != .noneSkipLast
                    let bytes = alpha ? image.pngData() : image.jpegData(compressionQuality: 0.92)
                    let dir = AureaPaths.documents.appendingPathComponent("formas3d", isDirectory: true)
                    let file = dir.appendingPathComponent(UUID().uuidString + (alpha ? ".png" : ".jpg"))
                    if let bytes, (try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)) != nil,
                       (try? bytes.write(to: file)) != nil { path = file.path }
                }
            }
            let ok = path.map { engine.setShape3DPartStyle(id, part: Int32(part), color: nil, image: $0) } ?? false
            if !ok, let path { try? FileManager.default.removeItem(atPath: path) }
            DispatchQueue.main.async {
                guard let self else { return }
                self.endProjectOperation(operation)
                self.importingMedia = false
                guard self.projectGeneration == project else { return }
                if !ok { self.toast = AureaText.t("msg_nao_foi_possivel_importar_a_imagem") }
                Shape3DState.shared.revision += 1
                self.refreshModel(force: true)
            }
        }
    }
}

/// Classe de memória LOW no iOS: RAM física abaixo de 4608 MiB (o corte do
/// motor, DeviceCapabilities memory_tier). Caches de imagem da UI e o limiar
/// de pressão seguem a classe — o mesmo que DeviceMemoryClass no Android.
enum DeviceMemoryClass {
    static let lowTotalBytes: UInt64 = 4608 * 1024 * 1024
    static let low: Bool = ProcessInfo.processInfo.physicalMemory < lowTotalBytes
}
