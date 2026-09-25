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
    @Published var showAddLayer = false
    @Published var showExport = false
    @Published var showProjectSettings = false
    @Published var showSettings = false
    /// A folha de geração atual (a doca de painéis, o painel aberto ou o
    /// "adicionar camada").
    ///
    /// A doca fica SEMPRE à mão (é de onde saem os painéis): `.none` abre a
    /// doca, e não uma tela sem folha — foi o que a casca A.01 fixou, e cada
    /// ladrilho aberto é que decide a fração da folha.
    var sheetContent: SheetContent {
        if showAddLayer { return .adding }
        // O painel da Aurea AI CRIA a camada: abre sem nada selecionado (igual ao Android).
        if panel == .aiVideo { return .panel }
        if selection.isEmpty { return .none }
        if selection.count > 1 { return .batch }
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
    @Published private(set) var operationMessage = "Importando mídia…"
    @Published var pointPick: Bool?
    @Published var curveEffect: UInt32 = UInt32.max
    @Published var curveParam: UInt32 = 0
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
    @Published var freehandPoints: [Float] = []
    @Published var actionSheet: ActionSheetRequest?
    @Published var numericKeypad: KeypadRequest?
    @Published var colorSheet: ColorSheetRequest?
    @Published var expressionSheet: ExpressionRequest?
    @Published var namePrompt: NamePromptRequest?
    @Published var text3DFontSheet: Text3DFontRequest?
    @Published var presetDialog: PresetDialogRequest?
    @Published var curveReturnPanel: PanelKind = .none
    @Published var stageManipulating = false
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
    @Published private(set) var masks: [MaskItem] = []
    @Published private(set) var maskAffine: [Float] = [1, 0, 0, 1, 0, 0]
    @Published var selectedMask: UInt32?
    @Published var maskDrawing = false
    @Published var selectedMaskPoint: Int?

    // --- Export -------------------------------------------------------------
    @Published var exportOptions = ExportOptions()
    @Published private(set) var exportProgress: [String: Any] = [:]
    @Published private(set) var exporting = false
    @Published private(set) var exportedURL: URL?
    @Published private(set) var exportMessage: String?
    @Published private(set) var exportCancelled = false
    @Published private(set) var exportPublishing = false
    @Published private(set) var exportSavedToPhotos = false
    func openExport() {
        if !exporting { exportedURL = nil; exportMessage = nil; exportCancelled = false; exportProgress = [:] }
        showExport = true
    }

    func openCurve(property: UInt32, effect: UInt32 = UInt32.max, param: UInt32 = 0, time: Int32? = nil) {
        if panel != .curve { curveReturnPanel = panel }
        curveProperty = property; curveEffect = effect; curveParam = param; curveSelectedTime = time; openPanel(.curve)
    }
    private var pendingExportURL: URL?
    private let mediaQueue = DispatchQueue(label: "com.aurea.media-import", qos: .userInitiated)
    private let autosaveQueue = DispatchQueue(label: "com.aurea.autosave", qos: .utility)
    private var lastAutosaveActivity = ProcessInfo.processInfo.systemUptime
    private var autosaveRetryAfter = 0.0
    private var autosaving = false
    private var autosaveFailureShown = false

    // --- Ajustes ------------------------------------------------------------
    @Published var language: AureaLanguage = .systemDefault {
        didSet { AureaText.language = language; UserDefaults.standard.set(language.rawValue, forKey: "aurea.language") }
    }
    @Published var showPerf = false

    enum Screen { case home, editor }
    enum PanelKind { case none, dock, transform, text, effects, layer3D, exportPanel, appearance, speed, audio, shape, shapeEdit, mask, textAnimation, curve, presets, particles, tracking, captions, vector, aiVideo }
    @Published var curveProperty: UInt32 = 0
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
                homeScrollProbe = prepareParityHomeScroll()
            } else if scene == "android-hdri", started {
                hdriProbe = prepareParityHDRI()
            } else if scene == "export-render", started {
                exportProbe = await ParityExportProbe.run(engine: engine, documents: AureaPaths.documents)
                refreshModel(force: true); enterEditor()
                if let id = layers.first?.id { select(layerId: id, additive: false); panel = .effects }
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
                        if kind == 11 { particleProbe = captureParityParticles(layerId: id, scene: scene) }
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
                    case "text-2d": addText(); panel = .text
                    case "text-3d": addText3D(content: "Texto", depth: 0.25); panel = .layer3D
                    case "vector": addVector(1); panel = .vector
                    default: addShape(1)
                    }
                    if scene == "transform" { panel = .transform }
                    if scene == "effects" { panel = .effects }
                    if scene == "export" { openExport() }
                    if scene == "project-settings" { clearSelection(); showProjectSettings = true }
                    if scene == "shape-edit" { panel = .shapeEdit }
                    if scene == "appearance" { panel = .appearance }
                    if scene == "presets" { panel = .presets }
                    if scene == "mask" { addMask(1); panel = .mask }
                }
                _ = saveProject(writeThumbnail: true)
            }
            try? await Task.sleep(nanoseconds: 2_000_000_000)
            var frameWidth: UInt32 = 0, frameHeight: UInt32 = 0
            if started, !["home", "home-scroll"].contains(scene), let pixels = engine.captureFrame(480, outWidth: &frameWidth, outHeight: &frameHeight),
               let preview = UIImage.fromRGBA(pixels, width: Int(frameWidth), height: Int(frameHeight))?.pngData() {
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
    private func prepareParityHDRI() -> [String: Any] {
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
        func capture(_ phase: String) {
            var width: UInt32 = 0, height: UInt32 = 0
            var row: [String: Any] = ["phase": phase, "environment": engine.environment(),
                "layers": engine.layers(), "loadNotice": engine.lastLoadNotice,
                "missingAssets": engine.lastLoadMissingAssets]
            if let id = primarySelection { row["objectEnvironment"] = engine.objectEnvironment(forLayer: id) }
            if let bytes = engine.captureFrame(480, outWidth: &width, outHeight: &height),
               let png = UIImage.fromRGBA(bytes, width: Int(width), height: Int(height))?.pngData() {
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
        prepareFrame(); capture("loaded")
        // Save to a new path: retain the approved Android input byte-for-byte.
        let saved = engine.saveProject(roundtrip.path)
        probe["saved"] = saved
        if saved, engine.loadProject(roundtrip.path) {
            probe["reopened"] = true
            projectURL = roundtrip; refreshModel(force: true)
            prepareFrame(); capture("reopened")
        }
        probe["originalUnchanged"] = originalBytes != nil && originalBytes == (try? Data(contentsOf: original))
        probe["frames"] = frames
        refreshStatus()
        return probe
    }

    /// Actual core projects and rendered JPEGs; no UI-only cards or fake covers.
    private func prepareParityHomeScroll() -> [String: Any] {
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
            let saved = saveProject(writeThumbnail: true)
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
    private func captureParityParticles(layerId: Int64, scene: String) -> [String: Any] {
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
        func capture(_ requested: Int64, mode: String) {
            var width: UInt32 = 0, height: UInt32 = 0
            var row: [String: Any] = ["requestedFrame": requested, "mode": mode]
            if let data = engine.captureFrame(480, outWidth: &width, outHeight: &height),
               let image = UIImage.fromRGBA(data, width: Int(width), height: Int(height))?.pngData() {
                let file = "\(scene)-isolated-\(requested).png"
                do {
                    try image.write(to: AureaPaths.documents.appendingPathComponent(file))
                    row["file"] = file
                    rgba[requested] = [UInt8](data)
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
            capture(frame, mode: "seek")
        }
        engine.run { core in core.scrubBegin(); core.scrub(toFrame: 59) }
        capture(59, mode: "scrub")
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
        _ = engine.flush()
        engine.suspend()
        if screen == .editor, projectURL != nil {
            _ = saveProject(writeThumbnail: false)
        }
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
        followPlayback(false)
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
        let playheadChanged = out.playhead != lastEnginePlayhead
        if playheadChanged || out.modelRevision != lastRevision {
            lastAutosaveActivity = ProcessInfo.processInfo.systemUptime
        }
        lastEnginePlayhead = out.playhead
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
            refreshSelectedLayer()
        }
        if out.thumbnailGeneration != lastThumbGeneration {
            lastThumbGeneration = out.thumbnailGeneration
        }
        if showPerf { perf = engine.perf() }
        autosaveIfIdle()
    }

    private func autosaveIfIdle() {
        let now = ProcessInfo.processInfo.systemUptime
        guard screen == .editor, status.dirty != 0, status.playing == 0,
              !autosaving, !importingMedia, !exporting,
              now - lastAutosaveActivity >= 3, now >= autosaveRetryAfter,
              let url = projectURL else { return }
        _ = engine.flush()
        autosaving = true
        let saver = engine
        autosaveQueue.async { [weak self] in
            let saved = saver.autosaveProject()
            DispatchQueue.main.async {
                guard let self else { return }
                self.autosaving = false
                guard self.projectURL == url else { return }
                if saved {
                    self.autosaveRetryAfter = 0
                    self.autosaveFailureShown = false
                } else {
                    self.autosaveRetryAfter = ProcessInfo.processInfo.systemUptime + 30
                    if !self.autosaveFailureShown { self.toast = "falha ao salvar" }
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
                             paramIndex: (key["paramIndex"] as? NSNumber)?.uint32Value ?? 0,
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
            EffectCatalogItem(effectClass: (row["effectClass"] as? NSNumber)?.uint32Value ?? 0, typeId: (row["typeId"] as? NSNumber)?.uint32Value ?? 0,
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
        if panel == .mask || panel == .vector { refreshMasks() }
        if let effectId = selectedEffectId {
            loadParams(layerId: layerId, effectId: effectId)
        }
    }

    func loadParams(layerId: Int64, effectId: UInt32) {
        selectedEffectId = effectId
        effectParams = engine.effectParams(forLayer: layerId, effectId: effectId).map { row in
            let value = (row["value"] as? [NSNumber])?.map(\.floatValue) ?? [0, 0, 0, 0]
            let def = (row["defaultValue"] as? [NSNumber])?.map(\.floatValue) ?? [0, 0, 0, 0]
            return EffectParamItem(flags: (row["flags"] as? NSNumber)?.uint32Value ?? 0, index: (row["index"] as? NSNumber)?.uint32Value ?? 0,
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

    func playPause() {
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
        engine.run { $0.seek(toFrame: frame) }; status.playhead = frame
        playheadClock.frame = frame
        if primarySelection != nil { refreshSelectedLayer() }
    }
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

    func select(layerId: Int64, additive: Bool = false) {
        if primarySelection != layerId { selectedMask = nil; selectedMaskPoint = nil; maskDrawing = false; pointPick = nil; freehandPoints = []; panel = .none }
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
        if selection.count != 1 { panel = .none }
        refreshSelectedLayer()
    }

    func clearSelection() {
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
        guard mask >= 0 else { toast = "Não foi possível adicionar a máscara"; return }
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
        if let n = detail["localPlayhead"] as? NSNumber { return n.int32Value }
        guard let layer = selectedLayer else { return Int32(clamping: status.playhead) }
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
        let mask = (detail["animatedMask"] as? NSNumber)?.uint32Value ?? 0
        if property < 32 && mask & (1 << property) != 0 {
            keyProperty(property, value: value)
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
        let width: UInt32 = ratio >= 1 ? UInt32((Double(shortSide) * ratio).rounded()) : shortSide
        let height: UInt32 = ratio >= 1 ? shortSide : UInt32((Double(shortSide) / ratio).rounded())
        return newProject(width: width, height: height, fps: fps, title: title)
    }

    @discardableResult
    func newProject(width: UInt32, height: UInt32, fps: Double, title: String) -> Bool {
        guard started else {
            toast = startError ?? "o motor ainda não está pronto"
            return false
        }
        let trimmed = title.trimmingCharacters(in: .whitespacesAndNewlines)
        let name = trimmed.isEmpty ? "Projeto \(projects.count + 1)" : trimmed
        guard engine.newProjectWidth(width, height: height, fps: fps, title: name) else {
            toast = "não foi possível criar o projeto"
            return false
        }
        let url = uniqueProjectURL(name)
        guard engine.saveProject(url.path) else {
            toast = "não foi possível salvar o projeto"
            return false
        }
        projectURL = url
        projectName = name
        refreshProjectList()
        refreshModel(force: true)
        enterEditor()
        return true
    }

    func open(_ project: ProjectFile) {
        openingProject = true
        defer { openingProject = false }
        guard engine.loadProject(project.url.path) else {
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
    /// Atalho da Home: primeiro cria a composição na proporção da mídia e só
    /// depois a importa. O importador do editor pressupõe um projeto aberto.
    func createFromMedia(url: URL, kind: ImportKind) {
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
            let ratio = mediaWidth > 0 && mediaHeight > 0
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
        operationMessage = "Importando mídia…"
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
                try FileManager.default.copyItem(at: url, to: destination)
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
                    failure = importer.lastImportError
                    if failure.isEmpty { failure = "Importação falhou (\(-result))" }
                    try? FileManager.default.removeItem(at: destination)
                }
            } catch { failure = "Não deu para copiar o arquivo: \(error.localizedDescription)" }
            let importedId = result, importFailure = failure
            DispatchQueue.main.async {
                guard let self else { return }
                self.importingMedia = false
                if !importFailure.isEmpty { self.toast = importFailure; return }
                if kind == .hdri { self.toast = "Ambiente importado" }
                else { self.engine.selectLayers([NSNumber(value: importedId)]); self.selection = [importedId] }
                self.refreshModel(force: true)
                if atPlayhead && kind != .hdri { self.moveToPlayhead(importedId) }
                _ = self.saveProject(writeThumbnail: false)
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
                if !error.isEmpty { self.toast = error }
                else { _ = self.saveProject(writeThumbnail: false) }
            }
        }
    }

    func beginPointPick(stabilize: Bool) {
        guard let layer = selectedLayer, layer.kind == 1, !layer.locked else { return }
        engine.run { $0.pause(); $0.seek(toFrame: Int64(layer.startFrame)) }
        refreshStatus(); refreshSelectedLayer()
        pointPick = stabilize; panel = .none
    }

    func finishPointPick(_ point: CGPoint) {
        guard let stabilize = pointPick, let id = primarySelection else { return }
        let a = engine.maskData(id).prefix(6).map(\.floatValue)
        guard a.count == 6 else { return }
        let det = a[0] * a[3] - a[1] * a[2]
        guard abs(det) > 0.000001 else { return }
        let x = Float(point.x) - a[4], y = Float(point.y) - a[5]
        let localX = (a[3] * x - a[2] * y) / det, localY = (-a[1] * x + a[0] * y) / det
        pointPick = nil
        performMediaOperation(stabilize ? "Estabilizando…" : "Rastreando o ponto…") {
            $0.trackPoint(id, x: localX, y: localY, stabilize: stabilize)
        }
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
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        guard let text = try? String(contentsOf: url, encoding: .utf8) else {
            toast = AureaText.t("msg_nao_foi_possivel_importar_svg"); return
        }
        let id = engine.importSVG(text, name: url.deletingPathExtension().lastPathComponent)
        guard id >= 0 else { toast = AureaText.t("msg_nao_foi_possivel_importar_svg"); return }
        showAddLayer = false
        syncAfterEdit(); select(layerId: id, additive: false)
    }

    func detectBeats() {
        guard !importingMedia, let layer = selectedLayer, layer.kind == 1 || layer.kind == 3 else {
            toast = AureaText.t("msg_escolha_uma_camada_de_audio_ou"); return
        }
        importingMedia = true; operationMessage = AureaText.t("msg_detectando_batidas")
        if status.playing != 0 { playPause() }
        let native = engine
        mediaQueue.async { [weak self] in
            var bpm: Double = 0
            let count = native.detectBeats(forLayer: layer.id, bpm: &bpm)
            DispatchQueue.main.async {
                guard let self else { return }
                self.importingMedia = false
                self.refreshModel(force: true); self.refreshMarkers()
                if count > 0 {
                    self.toast = AureaText.t("msg_batidas_bpm", count, Int(bpm.rounded()))
                    _ = self.saveProject(writeThumbnail: false)
                } else {
                    self.toast = count == 0 ? AureaText.t("msg_nenhuma_batida_clara_neste_som")
                        : AureaText.t("msg_nao_foi_possivel_analisar_o_som", -count)
                }
            }
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

    func addText() {
        let id = engine.addText(AureaText.t("pn_text3d_placeholder"))
        if id >= 0 { selection = [id]; engine.selectLayers([NSNumber(value: id)]) }
        showAddLayer = false
        syncAfterEdit()
    }

    func addVector(_ preset: UInt32, freehand: Bool = false) {
        let id = engine.addVector(preset)
        guard id >= 0 else { toast = "Não foi possível criar o vetor"; return }
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
        } else { toast = "Não foi possível adicionar o desenho" }
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
        exportedURL = nil
        exportProgress = [:]; exportMessage = nil; exportCancelled = false; exportSavedToPhotos = false
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
            exportMessage = "o export não pôde começar"
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
                    let result = (self.exportProgress["result"] as? NSNumber)?.intValue ?? 0
                    self.exportedURL = result == 0 ? self.pendingExportURL : nil
                    self.pendingExportURL = nil
                    if let url = self.exportedURL {
                        self.exportCancelled = false
                        self.publishExportToPhotos(url)
                    } else {
                        self.exporting = false
                        if !self.exportCancelled {
                            let message = self.exportProgress["message"] as? String ?? ""
                            self.exportMessage = message.isEmpty ? "o export falhou" : message
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
        PHPhotoLibrary.requestAuthorization(for: .addOnly) { [weak self] status in
            guard status == .authorized || status == .limited else {
                Task { @MainActor in
                    self?.exportPublishing = false; self?.exporting = false
                    self?.exportMessage = "Vídeo salvo no Aurea. Permita adicionar ao Fotos para salvar na galeria."
                    self?.toast = AureaText.t("editor_video_pronto")
                    // Ponto seguro: o render acabou e o vídeo já está salvo. Não segura nada.
                    AureaAdsManager.shared.showExportInterstitialIfAvailable {}
                }
                return
            }
            PHPhotoLibrary.shared().performChanges({ PHAssetChangeRequest.creationRequestForAssetFromVideo(atFileURL: url) }) { saved, error in
                Task { @MainActor in
                    self?.exportSavedToPhotos = saved
                    self?.exportPublishing = false; self?.exporting = false
                    self?.exportMessage = saved ? nil : (error?.localizedDescription ?? "Não foi possível adicionar o vídeo ao Fotos.")
                    self?.toast = AureaText.t("editor_video_pronto")
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

    func setLooping(_ on: Bool) {
        guard looping != on else { return }
        looping = on
        engine.run { $0.setLoop(on) }
    }

    func toggleHud() { hudVisible.toggle() }

    func toggleMarkerAt(_ frame: Int64) {
        _ = engine.toggleMarker(frame)
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
        markerFrames = frames
    }

    func seekToNextMarker() {
        let frames = markerFrames
        guard !frames.isEmpty else { return }
        let next = frames.first { $0 > status.playhead } ?? frames[0]
        seek(toFrame: next)
    }

    // --- Obturador / desfoque de movimento da composição ---------------------
    /// {ligado, obturador em graus} — o mesmo par do `motion_blur_settings`.
    private var motionBlur: (on: Bool, shutter: Float) {
        let values = engine.motionBlurSettings()
        return (values.count > 0 && values[0].floatValue != 0, values.count > 1 ? values[1].floatValue : 180)
    }

    var compMotionBlur: Bool { motionBlur.on }
    var shutterAngle: Float { motionBlur.shutter }

    func setCompositionMotionBlur(_ on: Bool) {
        engine.run { $0.setMotionBlurSettings(on, shutter: shutterAngle) }
        refreshModel(force: true)
    }

    func changeShutterAngle(_ degrees: Float) {
        engine.run { $0.setMotionBlurSettings(compMotionBlur, shutter: degrees) }
        refreshModel(force: true)
    }

    // --- Modo Edição ---------------------------------------------------------
    func toggleEditMode() {
        editMode.toggle()
        engine.run { $0.setEditMode(editMode) }
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
        guard !targets.isEmpty else { return }
        mutate { engine in for row in targets { engine.splitLayer(row.id, atFrame: frame) } }
        refreshModel(force: true)
    }

    /// Trim do INÍCIO para `frame`: o conteúdo fica parado e só a borda anda.
    func trimStart(_ layerId: Int64, at frame: Int64) {
        guard layers.first(where: { $0.id == layerId })?.locked == false else { return }
        guard let d = engine.layerDetail(layerId),
              let end = (d["endFrame"] as? NSNumber)?.int32Value,
              let start = (d["startFrame"] as? NSNumber)?.int32Value,
              let offset = (d["offsetFrames"] as? NSNumber)?.int32Value,
              let source = (d["sourceFrames"] as? NSNumber)?.int32Value else { return }
        var newStart = Int(min(Int32(clamping: frame), end - 1))
        var newOffset = Int(offset) + (newStart - Int(start))
        if source > 0 && newOffset < 0 {
            newStart -= newOffset
            newOffset = 0
        }
        newStart = max(0, newStart)
        guard newStart != Int(start) else { return }
        engine.run { $0.setLayer(layerId, startFrame: Int32(newStart), endFrame: end, offsetFrames: Int32(newOffset), setOffset: true) }
        refreshModel(force: true)
    }

    /// Puxa a camada INTEIRA para o cabeçote: o clipe anda, a duração não muda e
    /// o conteúdo anda junto (o deslocamento interno fica). É o "trazer para o
    /// cabeçote" da fileira rápida — aparar come a borda, dividir corta em dois,
    /// isto só move (e é justamente para quem está FORA do cabeçote).
    func moveToPlayhead(_ layerId: Int64) {
        guard let row = layers.first(where: { $0.id == layerId }), !row.locked else { return }
        let start = Int(row.startFrame), end = Int(row.endFrame)
        let target = max(0, Int(status.playhead))
        guard target != start else { return }
        engine.run {
            $0.setLayer(layerId, startFrame: Int32(target), endFrame: Int32(target + (end - start)),
                        offsetFrames: row.offsetFrames, setOffset: false)
        }
        refreshModel(force: true)
    }

    /// Trim do FIM para `frame`. O vídeo não passa do fim da mídia.
    func trimEnd(_ layerId: Int64, at frame: Int64) {
        guard layers.first(where: { $0.id == layerId })?.locked == false else { return }
        guard let d = engine.layerDetail(layerId),
              let end = (d["endFrame"] as? NSNumber)?.int32Value,
              let start = (d["startFrame"] as? NSNumber)?.int32Value,
              let offset = (d["offsetFrames"] as? NSNumber)?.int32Value,
              let source = (d["sourceFrames"] as? NSNumber)?.int32Value else { return }
        var newEnd = max(Int32(clamping: frame), start + 1)
        if source > 0 { newEnd = min(newEnd, start - offset + source) }
        guard newEnd != end else { return }
        engine.run { $0.setLayer(layerId, startFrame: start, endFrame: newEnd, offsetFrames: offset, setOffset: false) }
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
        engine.run { $0.beginUndoGroup() }
    }

    func endGesture() {
        engine.run { $0.endUndoGroup() }
        refreshModel(force: true)
    }

    /// Muda UMA propriedade de transform, com semântica de keyframe: animada
    /// cria/atualiza o keyframe no cabeçote, senão muda o valor fixo. Rotação
    /// X/Y/Z têm UM keyframe só (os três eixos no mesmo instante).
    func setTransform(_ property: UInt32, value: Float, layer: Int64) {
        guard value.isFinite, let d = engine.layerDetail(layer) else { return }
        let animated = (d["animatedMask"] as? NSNumber)?.uint32Value ?? 0
        let local = (d["localPlayhead"] as? NSNumber)?.int32Value ?? Int32(clamping: status.playhead)
        let position = StageGeom.floats(d["position"])
        let scale = StageGeom.floats(d["scale"])
        let rotation = StageGeom.floats(d["rotation"])
        let anchor = StageGeom.floats(d["anchor"])
        func component(_ values: [Float], _ index: Int) -> Float { values.count > index ? values[index] : 0 }
        if property >= 6 && property <= 8 && (animated & (1 << 6 | 1 << 7 | 1 << 8)) != 0 {
            mutate { engine in
                for axis in 0..<3 {
                    engine.insertKeyframe(forLayer: layer, property: UInt32(6 + axis), time: local,
                                          value: axis == Int(property) - 6 ? value : component(rotation, axis))
                }
            }
        } else if property < 32 && animated & (1 << property) != 0 {
            mutate { engine in engine.insertKeyframe(forLayer: layer, property: property, time: local, value: value) }
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
        let animated = (d["animatedMask"] as? NSNumber)?.uint32Value ?? 0
        let local = (d["localPlayhead"] as? NSNumber)?.int32Value ?? Int32(clamping: status.playhead)
        let position = StageGeom.floats(d["position"])
        let scale = StageGeom.floats(d["scale"])
        func component(_ values: [Float], _ index: Int) -> Float { values.count > index ? values[index] : 0 }
        let isAnimated = (pa < 32 && animated & (1 << pa) != 0) || (pb < 32 && animated & (1 << pb) != 0)
        mutate { engine in
            if isAnimated {
                engine.insertKeyframe(forLayer: layer, property: pa, time: local, value: va)
                engine.insertKeyframe(forLayer: layer, property: pb, time: local, value: vb)
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
