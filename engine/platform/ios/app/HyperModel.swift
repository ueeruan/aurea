// AureaHyper (0.3.2) — o modelo da tela da Home (HyperView.swift), o portão
// pago e o lado da plataforma (Vision, Neural Engine). Par do Android:
// hyper/HyperRecipe.kt, hyper/Entitlements.kt, hyper/HyperFlow.kt e
// hyper/HyperFaceDetector.kt. A regra das etapas fica no motor
// (engine/include/aurea/hyper/Hyper.hpp); aqui só a forma e a tela.
import AVFoundation
import Combine
import CoreGraphics
import CoreML
import CryptoKit
import Foundation
import UIKit
import Vision

// MARK: - Receita (hyper::Recipe)

enum HyperPath: Int, CaseIterable, Equatable {
    case auto = 0, turbo = 1, lite = 2, max = 3
    static let choices: [HyperPath] = [.turbo, .lite, .max]
    static func of(_ code: Int) -> HyperPath { HyperPath(rawValue: code) ?? .turbo }
    var nameKey: String {
        switch self {
        case .lite: return "hyper_path_lite"
        case .max: return "hyper_path_max"
        default: return "hyper_path_turbo"
        }
    }
    var bodyKey: String { nameKey + "_body" }
    var id: String {
        switch self {
        case .auto: return "auto"
        case .turbo: return "turbo"
        case .lite: return "lite"
        case .max: return "max"
        }
    }
}

enum HyperUpscaleMode: Int, Equatable { case general = 0, anime = 1 }

/// hyper::Recipe. Zeros/false = etapa desligada.
struct HyperRecipe: Equatable {
    var upscale: Int = 1
    var upscaleMode: HyperUpscaleMode = .general
    var denoise: Double = 0
    var deblock = false
    /// 0 desligado, 1 automático, 2 sempre.
    var deinterlace = 0
    /// Desligado por padrão: pode "trocar" o rosto.
    var face = false
    var faceStrength: Double = 0.5
    /// 0 = manter.
    var targetFps: Double = 0
    var slowMotion = 1
    var stabilize = false
    var stabilizeStrength: Double = 0.5
    var stabilizeToKeyframes = false
    var path: HyperPath = .auto

    static let upscaleFactors = [1, 2, 4]
    static let slowMotionFactors = [1, 2, 4, 8]
    static let targetFpsChoices: [Double] = [0, 30, 60]
    static let packedSize = 13

    var identity: Bool {
        upscale <= 1 && denoise <= 0 && !deblock && deinterlace == 0 && !face && targetFps <= 0 && slowMotion <= 1 && !stabilize
    }

    /// A ordem que a ponte lê (AureaEngine.mm / aurea_jni.cpp `hyper_recipe_from`).
    var packed: [Double] {
        [Double(upscale), Double(upscaleMode.rawValue), denoise, deblock ? 1 : 0, Double(deinterlace), face ? 1 : 0, faceStrength,
         targetFps, Double(slowMotion), stabilize ? 1 : 0, stabilizeStrength, stabilizeToKeyframes ? 1 : 0, Double(path.rawValue)]
    }

    init() {}

    init?(packed v: [Double]) {
        guard v.count >= Self.packedSize else { return nil }
        upscale = Int(v[0]); upscaleMode = v[1] >= 0.5 ? .anime : .general; denoise = v[2]; deblock = v[3] >= 0.5
        deinterlace = Int(v[4]); face = v[5] >= 0.5; faceStrength = v[6]; targetFps = v[7]; slowMotion = Int(v[8])
        stabilize = v[9] >= 0.5; stabilizeStrength = v[10]; stabilizeToKeyframes = v[11] >= 0.5; path = .of(Int(v[12]))
    }

    /// Nome curto para o título do projeto ("Upscale 2× · 60 fps").
    var summary: String {
        var parts: [String] = []
        if denoise > 0 || deblock || deinterlace > 0 { parts.append("Clean") }
        if stabilize { parts.append("Stable") }
        if upscale > 1 { parts.append("Upscale \(upscale)×") }
        if face { parts.append("Face") }
        if targetFps > 0 { parts.append("\(Int(targetFps.rounded())) fps") }
        if slowMotion > 1 { parts.append("Slow \(slowMotion)×") }
        return parts.isEmpty ? "Hyper" : parts.joined(separator: " · ")
    }
}

/// As receitas da tela 2 (as mesmas do Android: HyperPreset).
enum HyperPreset: String, CaseIterable, Identifiable {
    case restore, sharper, fps60, slowmo, anime, stabilize
    var id: String { rawValue }
    var variants: [Int] {
        switch self {
        case .sharper: return [2, 4]
        case .slowmo: return [2, 4, 8]
        default: return []
        }
    }
    var titleKey: String {
        switch self {
        case .restore: return "hyper_preset_restore"
        case .sharper: return "hyper_preset_sharper"
        case .fps60: return "hyper_preset_fps60"
        case .slowmo: return "hyper_preset_slowmo"
        case .anime: return "hyper_preset_anime"
        case .stabilize: return "hyper_preset_stabilize"
        }
    }
    var bodyKey: String { titleKey + "_body" }
    var symbol: String {
        switch self {
        case .restore: return "wand.and.stars"
        case .sharper: return "sparkles"
        case .fps60: return "hare"
        case .slowmo: return "tortoise"
        case .anime: return "paintbrush"
        case .stabilize: return "move.3d"
        }
    }
    func recipe(_ variant: Int? = nil) -> HyperRecipe {
        let v = variant ?? variants.first ?? 0
        var r = HyperRecipe()
        switch self {
        case .restore:
            // Clean + Upscale 2× + Face (o rosto é o risco; a tela avisa).
            r.upscale = 2; r.denoise = 0.5; r.deblock = true; r.deinterlace = 1; r.face = true; r.faceStrength = 0.5
        case .sharper: r.upscale = v == 4 ? 4 : 2
        case .fps60: r.targetFps = 60
        case .slowmo: r.slowMotion = [2, 4, 8].contains(v) ? v : 2
        case .anime: r.upscale = 2; r.upscaleMode = .anime
        case .stabilize: r.stabilize = true; r.stabilizeStrength = 0.5
        }
        return r
    }
}

/// hyper::DeviceProfile como a ponte devolve.
struct HyperDeviceProfile: Equatable {
    var measured = false, abi64 = false, vulkan = false, vulkanFp16 = false, gles3 = false, npu = false
    var defaultPath: HyperPath = .turbo
    var backendNote = ""
    init() {}
    init(_ d: [String: Any]) {
        func flag(_ k: String) -> Bool { (d[k] as? NSNumber)?.boolValue ?? false }
        measured = flag("measured"); abi64 = flag("abi64"); vulkan = flag("vulkan"); vulkanFp16 = flag("vulkanFp16")
        gles3 = flag("gles3"); npu = flag("npu")
        defaultPath = .of((d["defaultPath"] as? NSNumber)?.intValue ?? 1)
        backendNote = d["backendNote"] as? String ?? ""
    }
}

/// hyper::Estimate: segundos por caminho (< 0 = indisponível) e a saída.
struct HyperEstimate: Equatable {
    var seconds: [Double] = [-1, -1, -1, -1]
    var peakBytes: Double = 0
    var outputWidth = 0, outputHeight = 0
    var outputFps: Double = 0
    init(seconds: [Double]) { self.seconds = seconds }
    init?(_ v: [NSNumber]?) {
        guard let v, v.count >= 8 else { return nil }
        seconds = v[0..<4].map(\.doubleValue); peakBytes = v[4].doubleValue
        outputWidth = v[5].intValue; outputHeight = v[6].intValue; outputFps = v[7].doubleValue
    }
    func secondsFor(_ p: HyperPath) -> Double { p.rawValue < seconds.count ? seconds[p.rawValue] : -1 }
    func available(_ p: HyperPath) -> Bool { secondsFor(p) >= 0 }
}

enum HyperStep: Int, CaseIterable {
    case prepare = 0, clean, stable, upscale, face, motion, encode, finish
    var nameKey: String { "hyper_step_" + ["prepare", "clean", "stable", "upscale", "face", "motion", "encode", "finish"][rawValue] }
    var id: String { ["prepare", "clean", "stable", "upscale", "face", "motion", "encode", "finish"][rawValue] }
    var processing: Bool { [.clean, .stable, .upscale, .face, .motion].contains(self) }
}

enum HyperStepState { case waiting, running, done, skipped }

/// hyper::JobSteps como a ponte devolve (o mesmo vetor do Android, HyperJobInfo).
struct HyperJobInfo: Equatable {
    var states: [Int], active: Bool, running: Bool, finished: Bool, result: Int
    var pass: Int, passCount: Int, audioMuted: Bool, progress: Double, paths: [HyperPath]
    static let size = 27
    init?(_ v: [NSNumber]) {
        guard v.count >= Self.size else { return nil }
        let k = HyperStep.allCases.count
        states = (0..<k).map { v[$0].intValue }
        active = v[k].boolValue; running = v[k + 1].boolValue; finished = v[k + 2].boolValue; result = v[k + 3].intValue
        pass = v[k + 4].intValue; passCount = v[k + 5].intValue; audioMuted = v[k + 6].boolValue
        progress = v[k + 7].doubleValue / 1000; paths = (0..<k).map { HyperPath.of(v[k + 8 + $0].intValue) }
    }
    func present(_ s: HyperStep) -> Bool { s.rawValue < states.count && states[s.rawValue] != 3 }
    func state(_ s: HyperStep) -> HyperStepState {
        switch s.rawValue < states.count ? states[s.rawValue] : 0 {
        case 1: return .running
        case 2: return .done
        case 3: return .skipped
        default: return .waiting
        }
    }
    func path(_ s: HyperStep) -> HyperPath { s.rawValue < paths.count ? paths[s.rawValue] : .auto }
}

/// Por que um caminho não está disponível (par do `HyperPathBlock` do Android). 0.3.4
/// (bugs-034, hyper): sem motivo de APARELHO (32 bits, GPU, medição): todo caminho vale
/// em qualquer celular, por conta e risco da pessoa (`HyperPathRules.risky`). Só sobra o
/// motivo dos AJUSTES: nenhuma etapa pedida tem rede nesse caminho (igual em todo aparelho).
enum HyperPathBlock { case none, unavailable
    var key: String {
        switch self {
        case .none: return ""
        case .unavailable: return "hyper035_path_unavailable"
        }
    }
}

enum HyperPathRules {
    /// A decisão é do motor (Estimate < 0 = nenhuma rede para estes ajustes); o perfil não bloqueia.
    static func block(_ path: HyperPath, estimate: HyperEstimate?, profile: HyperDeviceProfile?) -> HyperPathBlock {
        // 0.3.5: nenhum caminho é bloqueado (par do Android); sem estimativa a tela
        // diz "tempo desconhecido" e a pessoa escolhe assim mesmo.
        return .none
    }
    /// O aviso "por sua conta e risco": Lite/Max mais pesado que o sugerido pelo aparelho,
    /// ou num aparelho ainda não medido. Só avisa; não desliga nada.
    static func risky(_ path: HyperPath, profile: HyperDeviceProfile?) -> Bool {
        guard path == .lite || path == .max else { return false }
        guard let profile, profile.measured else { return true }
        let suggested: HyperPath = profile.defaultPath == .auto ? .turbo : profile.defaultPath
        return path.rawValue > suggested.rawValue
    }
    static func initial(estimate: HyperEstimate?, profile: HyperDeviceProfile?) -> HyperPath {
        let suggested = profile.map { $0.defaultPath == .auto ? .turbo : $0.defaultPath } ?? .turbo
        guard let estimate, !estimate.available(suggested) else { return suggested }
        return HyperPath.choices.first(where: { estimate.available($0) }) ?? .turbo
    }
}

/// O job da Home como o export do AureaModel o carrega (ExportOptions.hyperJob).
struct HyperJobRequest: Equatable {
    var source: String
    var recipe: [Double]
    var output: URL
}

// MARK: - Portão pago (compra única `aureahyper_lifetime`)
//
// Sem cobrança por enquanto: o acesso é concedido por conta Aurea no servidor
// (discovery/entitlements.js, GET /api/entitlements) e o app só confere. A
// StoreKit 2 entra depois como outro EntitlementProvider (Transaction.
// currentEntitlements + verificação no servidor), sem mudar a tela nem o editor.
// Regras (as do Android, EntitlementRules): nunca libera sem uma confirmação
// do servidor para esta conta; vale 30 dias offline; sair/excluir/401 apaga.

enum EntitlementCheck: Equatable {
    case granted(Set<String>, Int)
    case unauthorized
    case unreachable
}

protocol EntitlementProvider {
    func fetch(sessionToken: String) async -> EntitlementCheck
}

struct ServerEntitlementProvider: EntitlementProvider {
    func fetch(sessionToken: String) async -> EntitlementCheck {
        let r = await ContaAPI.chamar("/api/entitlements", metodo: "GET", corpo: nil, token: sessionToken)
        return Self.interpret(status: r.status, body: r.corpo)
    }
    static func interpret(status: Int, body: [String: Any]) -> EntitlementCheck {
        if status == 401 { return .unauthorized }
        guard (200...299).contains(status), let list = body["products"] as? [Any] else { return .unreachable }
        return .granted(Set(list.compactMap { $0 as? String }.filter { !$0.isEmpty }), (body["checkedAt"] as? NSNumber)?.intValue ?? 0)
    }
}

struct EntitlementCache: Codable, Equatable {
    var account: String
    var products: [String]
    var checkedAtMillis: Int64
}

/// `needsConfig`/`off` vêm da config remota assinada (AppConfig.swift); o resto, do portão por conta.
enum HyperAccess { case checking, unlocked, locked, signedOut, needsCheck, expired, needsConfig, off }

enum EntitlementRules {
    static let graceMillis: Int64 = 30 * 24 * 3600 * 1000
    static let clockSkewMillis: Int64 = 24 * 3600 * 1000

    /// A conta como chave do cache (o mesmo hash do Android: SHA-256, 16 bytes em hex).
    static func accountKey(_ email: String) -> String {
        let digest = SHA256.hash(data: Data(email.trimmingCharacters(in: .whitespacesAndNewlines).lowercased().utf8))
        return digest.prefix(16).map { String(format: "%02x", $0) }.joined()
    }

    static func access(cache: EntitlementCache?, email: String?, product: String, nowMillis: Int64, checking: Bool) -> HyperAccess {
        guard let email else { return .signedOut }
        guard let mine = cache, mine.account == accountKey(email) else { return checking ? .checking : .needsCheck }
        let age = nowMillis - mine.checkedAtMillis
        if age < -clockSkewMillis || age > graceMillis { return checking ? .checking : .expired }
        return mine.products.contains(product) ? .unlocked : .locked
    }
}

@MainActor
final class HyperEntitlements: ObservableObject {
    static let shared = HyperEntitlements()
    static let product = "aureahyper_lifetime"   // hyper::kProductId
    private static let keychainKey = "hyper_entitlements"

    @Published private(set) var access: HyperAccess = .needsConfig
    /// Só o portão por conta (o que vale no modo "entitled" da config remota).
    @Published private(set) var entitlementAccess: HyperAccess = .signedOut
    var unlocked: Bool { access == .unlocked }

    private var provider: EntitlementProvider = ServerEntitlementProvider()
    private var cache: EntitlementCache?
    private var email: String?
    private var inFlight = 0

    private init() {
        if let raw = ContaKeychain.ler(Self.keychainKey), let data = raw.data(using: .utf8) {
            cache = try? JSONDecoder().decode(EntitlementCache.self, from: data)
        }
    }

    /// Testes e, no futuro, a loja.
    func use(provider: EntitlementProvider) { self.provider = provider }

    func refresh(token: String?, email accountEmail: String?) {
        email = accountEmail
        guard let token, let accountEmail else { recompute(); return }
        inFlight += 1
        recompute()
        let provider = self.provider
        Task { @MainActor in
            let result = await provider.fetch(sessionToken: token)
            inFlight = max(0, inFlight - 1)
            guard email == accountEmail else { recompute(); return }
            switch result {
            case .granted(let products, _):
                store(EntitlementCache(account: EntitlementRules.accountKey(accountEmail), products: products.sorted(),
                                       checkedAtMillis: Self.nowMillis()))
            case .unauthorized: store(nil)
            case .unreachable: break
            }
            recompute()
        }
    }

    func clear() {
        email = nil
        store(nil)
        recompute()
    }

    private func store(_ value: EntitlementCache?) {
        cache = value
        if let value, let data = try? JSONEncoder().encode(value), let text = String(data: data, encoding: .utf8) {
            ContaKeychain.gravar(Self.keychainKey, text)
        } else {
            ContaKeychain.apagar(Self.keychainKey)
        }
    }

    private func recompute() {
        entitlementAccess = EntitlementRules.access(cache: cache, email: email, product: Self.product, nowMillis: Self.nowMillis(),
                                                    checking: inFlight > 0)
        // A config remota manda: "free" libera, "entitled" usa o portão acima, "off" bloqueia.
        let store = AppConfigStore.shared
        access = AppConfigPolicy.hyperAccess(store.config, entitlement: entitlementAccess, now: AppConfigStore.now(),
                                             fetching: store.fetching)
    }

    /// A config remota mudou (ou o relógio passou do prazo): recalcula.
    func recheck() { recompute() }

    private static func nowMillis() -> Int64 { Int64(Date().timeIntervalSince1970 * 1000) }
}

/// Os efeitos de camada do AureaHyper (hyper::effect_keys). Sem o acesso eles
/// aparecem com o cadeado e abrem a tela bloqueada (EffectsBrowser.swift).
enum HyperEffectGate {
    static let keys = ["aurea.hyper.upscale", "aurea.hyper.motion", "aurea.hyper.clean"]
    static let typeIds: Set<UInt32> = Set(keys.map(fxEffectTypeId))
    static func isHyper(typeId: UInt32) -> Bool { typeIds.contains(typeId) }
    @MainActor static func locked(typeId: UInt32) -> Bool { isHyper(typeId: typeId) && !HyperEntitlements.shared.unlocked }
}

// MARK: - Plataforma: rostos (Vision), Neural Engine, pasta dos modelos

enum HyperPlatform {
    /// Antes de criar o motor (AureaModel.init): o medidor lê a NPU na inicialização.
    static func install() {
        let models = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("hyper-models", isDirectory: true)
        try? FileManager.default.createDirectory(at: models, withIntermediateDirectories: true)
        AureaEngine.hyperSetModelDirectory(models.path)
        AureaEngine.setHyperNpuAvailable(neuralEngineAvailable())
        AureaEngine.setHyperFaceDetector { data, width, height, stride, maxBoxes in
            detectFaces(data, width: Int(width), height: Int(height), stride: Int(stride), maxBoxes: Int(maxBoxes))
        }
    }

    /// [detector registrado, modelo de rostos (GFPGAN, sob demanda) presente].
    static var faceModelReady: Bool {
        let status = AureaEngine.hyperFaceStatus()
        return status.count > 1 && status[1].boolValue
    }

    private static func neuralEngineAvailable() -> Bool {
        if #available(iOS 17.0, *) {
            return MLComputeDevice.allComputeDevices.contains { device in
                if case .neuralEngine = device { return true }
                return false
            }
        }
        return false   // iOS 16: sem consulta pública; o medidor fica no conservador
    }

    /// Vision na thread do grafo: caixas em pixels (origem no topo) e os 5
    /// pontos do rosto, esquerdo/direito NA IMAGEM (17 números por rosto).
    private static func detectFaces(_ data: Data, width: Int, height: Int, stride: Int, maxBoxes: Int) -> [NSNumber]? {
        guard width >= 16, height >= 16, stride >= width * 3, data.count >= stride * height,
              let provider = CGDataProvider(data: data as CFData),
              let image = CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 24, bytesPerRow: stride,
                                  space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.none.rawValue),
                                  provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent) else { return nil }
        let request = VNDetectFaceLandmarksRequest()
        do { try VNImageRequestHandler(cgImage: image, options: [:]).perform([request]) } catch { return nil }
        let size = CGSize(width: width, height: height)
        var out: [NSNumber] = []
        for face in (request.results ?? []).prefix(maxBoxes) {
            let box = face.boundingBox
            let x = box.minX * size.width, y = (1 - box.maxY) * size.height
            let w = box.width * size.width, h = box.height * size.height
            let yaw = (face.yaw?.doubleValue ?? 0) * 180 / .pi
            var values: [Double] = [x, y, w, h, Double(face.confidence), yaw]
            if let lm = face.landmarks, let points = fivePoints(lm, size: size) {
                values.append(1); values.append(contentsOf: points)
            } else {
                values.append(0); values.append(contentsOf: Array(repeating: 0, count: 10))
            }
            out.append(contentsOf: values.map { NSNumber(value: $0) })
        }
        return out
    }

    private static func fivePoints(_ lm: VNFaceLandmarks2D, size: CGSize) -> [Double]? {
        func points(_ region: VNFaceLandmarkRegion2D?) -> [CGPoint] {
            (region?.pointsInImage(imageSize: size) ?? []).map { CGPoint(x: $0.x, y: size.height - $0.y) }
        }
        func center(_ p: [CGPoint]) -> CGPoint? {
            guard !p.isEmpty else { return nil }
            return CGPoint(x: p.map(\.x).reduce(0, +) / CGFloat(p.count), y: p.map(\.y).reduce(0, +) / CGFloat(p.count))
        }
        guard let a = center(points(lm.leftEye)), let b = center(points(lm.rightEye)),
              let nose = points(lm.noseCrest).last ?? center(points(lm.nose)) else { return nil }
        let lips = points(lm.outerLips)
        guard let mouthLeft = lips.min(by: { $0.x < $1.x }), let mouthRight = lips.max(by: { $0.x < $1.x }) else { return nil }
        let (eyeL, eyeR) = a.x <= b.x ? (a, b) : (b, a)
        return [eyeL.x, eyeL.y, eyeR.x, eyeR.y, nose.x, nose.y, mouthLeft.x, mouthLeft.y, mouthRight.x, mouthRight.y].map(Double.init)
    }
}

// MARK: - O fluxo de 4 telas

@MainActor
final class HyperFlow: ObservableObject {
    enum Step: Int { case choose = 0, recipe, preview, working }

    struct Source: Equatable {
        var path: String
        var name: String
        var width: Int
        var height: Int
        var fps: Double
        var duration: Double
    }

    struct Preview {
        var before: UIImage
        var after: UIImage
        var path: HyperPath
    }

    struct Pending: Equatable {
        var source: Source
        var recipe: HyperRecipe
        var path: HyperPath
        var output: String
    }

    unowned let model: AureaModel
    @Published private(set) var step: Step = .choose
    @Published private(set) var source: Source?
    @Published private(set) var importing = false
    @Published private(set) var preset: HyperPreset?
    @Published private(set) var variant = 0
    @Published private(set) var recipe = HyperRecipe()
    @Published var advanced = false
    @Published private(set) var profile: HyperDeviceProfile?
    @Published private(set) var measuring = false
    @Published private(set) var measureStartedAt = Date()
    @Published private(set) var estimate: HyperEstimate?
    @Published private(set) var estimating = false
    @Published private(set) var path: HyperPath = .turbo
    @Published private(set) var preview: Preview?
    @Published private(set) var previewLoading = false
    @Published private(set) var previewFailed = false
    @Published private(set) var finishing = false
    @Published var error: String?
    @Published private(set) var cancelRequested = false
    @Published private(set) var faceReady = true
    @Published private(set) var pending: Pending?
    @Published private(set) var jobInfo: HyperJobInfo?

    private var previewGeneration = 0
    private var jobOutput: URL?
    private var stepsTimer: Timer?
    private let worker = DispatchQueue(label: "aurea.hyper", qos: .userInitiated)
    private static let pendingKey = "aurea.hyper.pending"

    init(model: AureaModel) { self.model = model }

    var busy: Bool { importing || finishing || model.exporting }

    private static var folder: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0].appendingPathComponent("hyper", isDirectory: true)
    }

    // ---- 1. Escolher --------------------------------------------------------

    func reset() {
        guard !model.exporting, !finishing else { return }
        step = .choose
        if let old = source { try? FileManager.default.removeItem(atPath: old.path) }
        source = nil; preset = nil; variant = 0; recipe = HyperRecipe(); advanced = false
        estimate = nil; preview = nil; previewFailed = false; error = nil; cancelRequested = false; jobInfo = nil
        pending = loadPending()
        faceReady = HyperPlatform.faceModelReady
    }

    /// O vídeo escolhido (já copiado pelo seletor) vai para Application Support/hyper e é sondado.
    func pick(_ url: URL) {
        guard !busy else { return }
        importing = true
        error = nil
        Task { @MainActor in
            defer { importing = false }
            let folder = Self.folder.appendingPathComponent("fontes", isDirectory: true)
            try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
            let ext = url.pathExtension.isEmpty ? "mov" : url.pathExtension
            let target = folder.appendingPathComponent("fonte-\(Int(Date().timeIntervalSince1970 * 1000)).\(ext)")
            do { try FileManager.default.moveItem(at: url, to: target) } catch {
                do { try FileManager.default.copyItem(at: url, to: target) } catch {
                    self.error = AureaText.t("hyper_error_video"); return
                }
            }
            guard let probed = await Self.probe(target, name: url.deletingPathExtension().lastPathComponent) else {
                try? FileManager.default.removeItem(at: target)
                self.error = AureaText.t("hyper_error_video"); return
            }
            if let old = source, old.path != probed.path { try? FileManager.default.removeItem(atPath: old.path) }
            source = probed
            estimate = nil; preview = nil
            faceReady = HyperPlatform.faceModelReady
            step = .recipe
        }
    }

    private static func probe(_ url: URL, name: String) async -> Source? {
        let asset = AVURLAsset(url: url)
        guard let track = try? await asset.loadTracks(withMediaType: .video).first,
              let size = try? await track.load(.naturalSize), let transform = try? await track.load(.preferredTransform) else { return nil }
        let oriented = size.applying(transform)
        let w = Int(abs(oriented.width).rounded()), h = Int(abs(oriented.height).rounded())
        guard w >= 2, h >= 2 else { return nil }
        var fps = 30.0
        if let rate = try? await track.load(.nominalFrameRate), rate >= 1, rate <= 240 { fps = Double(rate) }
        let duration = (try? await asset.load(.duration)).map { CMTimeGetSeconds($0) } ?? 0
        return Source(path: url.path, name: name.isEmpty ? AureaText.t("app_imported_media") : name, width: w, height: h, fps: fps,
                      duration: duration.isFinite ? duration : 0)
    }

    // ---- 2. O que fazer -----------------------------------------------------

    func choose(_ p: HyperPreset, variant v: Int? = nil) {
        preset = p
        variant = v ?? p.variants.first ?? 0
        var r = p.recipe(variant)
        r.path = recipe.path
        recipe = r
    }

    func edit(_ r: HyperRecipe) {
        recipe = r
        if let p = preset {
            var expected = p.recipe(variant)
            expected.path = r.path
            if expected != r { preset = nil }
        }
    }

    func toPreview() {
        guard let s = source, !recipe.identity else { return }
        step = .preview
        error = nil
        Task { @MainActor in
            await ensureProfile(remeasure: false)
            await refreshEstimate(s)
            refreshPreview()
        }
    }

    func back() {
        guard !busy else { return }
        switch step {
        case .choose: model.closeHyper()
        case .recipe: step = .choose
        case .preview: previewGeneration += 1; step = .recipe
        case .working: step = .preview; jobInfo = nil
        }
    }

    // ---- 3. Prévia e estimativa ---------------------------------------------

    func measure() {
        guard !measuring else { return }
        Task { @MainActor in
            await ensureProfile(remeasure: true)
            if let s = source { await refreshEstimate(s) }
        }
    }

    private func ensureProfile(remeasure: Bool) async {
        guard !measuring else { return }
        if !remeasure, profile?.measured == true { return }
        measuring = true
        measureStartedAt = Date()
        let engine = model.engine
        let dict: [String: Any] = await withCheckedContinuation { done in
            worker.async { done.resume(returning: engine.hyperDeviceProfile(remeasure: remeasure)) }
        }
        if !dict.isEmpty { profile = HyperDeviceProfile(dict) }
        measuring = false
    }

    private func refreshEstimate(_ s: Source) async {
        estimating = true
        let engine = model.engine
        let packed = recipe.packed.map { NSNumber(value: $0) }
        let values: [NSNumber]? = await withCheckedContinuation { done in
            worker.async { done.resume(returning: engine.hyperEstimate(packed, source: s.path)) }
        }
        estimate = HyperEstimate(values)
        path = HyperPathRules.initial(estimate: estimate, profile: profile)
        estimating = false
    }

    func select(_ p: HyperPath) {
        guard path != p else { return }   // 0.3.5: a estimativa não bloqueia a escolha
        path = p
        refreshPreview()
    }

    func refreshPreview() {
        guard let s = source else { return }
        var r = recipe
        r.path = path
        let chosen = path
        previewGeneration += 1
        let generation = previewGeneration
        previewLoading = true
        previewFailed = false
        let engine = model.engine
        let packed = r.packed.map { NSNumber(value: $0) }
        let seconds = max(0, s.duration * 0.33)
        worker.async { [weak self] in
            let result = engine.hyperPreviewFrame(packed, source: s.path, seconds: seconds, path: Int32(chosen.rawValue))
            let w = (result["width"] as? NSNumber)?.intValue ?? 0, h = (result["height"] as? NSNumber)?.intValue ?? 0
            let before = (result["before"] as? Data).flatMap { Self.image($0, width: w, height: h) }
            let after = (result["after"] as? Data).flatMap { Self.image($0, width: w, height: h) }
            DispatchQueue.main.async {
                guard let self, generation == self.previewGeneration else { return }
                if let before, let after { self.preview = Preview(before: before, after: after, path: chosen) }
                self.previewFailed = before == nil || after == nil
                self.previewLoading = false
            }
        }
    }

    nonisolated private static func image(_ rgba: Data, width: Int, height: Int) -> UIImage? {
        guard width > 0, height > 0, rgba.count >= width * height * 4, let provider = CGDataProvider(data: rgba as CFData),
              let cg = CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: width * 4,
                               space: CGColorSpace(name: CGColorSpace.sRGB) ?? CGColorSpaceCreateDeviceRGB(),
                               bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue),
                               provider: provider, decode: nil, shouldInterpolate: true, intent: .defaultIntent) else { return nil }
        return UIImage(cgImage: cg)
    }

    // ---- 4. Trabalhando -----------------------------------------------------

    @Published private(set) var checkingConfig = false

    /// Antes de cada job a config remota é consultada de novo: o modo do
    /// AureaHyper pode ter mudado ("off", "entitled") ou a versão ficado abaixo da mínima.
    func start(resume output: URL? = nil) {
        guard !model.exporting, !finishing, !recipe.identity, !checkingConfig else { return }
        checkingConfig = true
        Task { @MainActor in
            _ = await AppConfigStore.shared.refreshNow()
            HyperEntitlements.shared.recheck()
            checkingConfig = false
            if AppConfigStore.shared.updateRequired { return }
            guard HyperEntitlements.shared.unlocked else {
                switch HyperEntitlements.shared.access {
                case .off: error = AureaText.t("hyper_off_default")
                case .needsConfig: error = AureaText.t("hyper_needs_internet")
                default: error = AureaText.t("hyper_effect_locked")
                }
                return
            }
            startNow(resume: output)
        }
    }

    private func startNow(resume output: URL?) {
        guard let s = source, !model.exporting, !finishing, !recipe.identity else { return }
        // 0.3.5: iniciar nunca é bloqueado pela estimativa; se faltar algo, o trabalho mostra o erro.
        try? FileManager.default.createDirectory(at: Self.folder, withIntermediateDirectories: true)
        let stamp = Int(Date().timeIntervalSince1970)
        let base = String(s.name.unicodeScalars.filter { CharacterSet.alphanumerics.contains($0) || $0 == " " || $0 == "-" || $0 == "_" })
            .trimmingCharacters(in: .whitespaces).prefix(60)
        // "Tentar de novo" e "Continuar" usam a MESMA saída: o motor retoma o que já validou.
        let out = output ?? Self.folder.appendingPathComponent("\(base.isEmpty ? "Aurea" : String(base)) Hyper \(stamp).mp4")
        jobOutput = out
        var r = recipe
        r.path = path
        savePending(Pending(source: s, recipe: recipe, path: path, output: out.path))
        pending = nil
        cancelRequested = false
        error = nil
        jobInfo = nil
        model.startHyperExport(HyperJobRequest(source: s.path, recipe: r.packed, output: out))
        step = .working
        startStepsTimer()
    }

    func retry() { start(resume: jobOutput) }

    func cancel() {
        guard model.exporting else { return }
        cancelRequested = true
        model.cancelExport()
        clearPending()
    }

    private func startStepsTimer() {
        stepsTimer?.invalidate()
        let timer = Timer(timeInterval: 0.5, repeats: true) { [weak self] _ in
            Task { @MainActor in
                guard let self else { return }
                self.jobInfo = HyperJobInfo(self.model.engine.hyperJobSteps()).flatMap { $0.active ? $0 : nil } ?? self.jobInfo
                if !self.model.exporting {
                    self.stepsTimer?.invalidate(); self.stepsTimer = nil
                    self.finishIfDone()
                }
            }
        }
        RunLoop.main.add(timer, forMode: .common)
        stepsTimer = timer
    }

    /// O resultado vira projeto: o modelo cria, importa, grava e abre o editor.
    func finishIfDone() {
        guard !finishing, !model.exporting, let out = model.exportedURL, out == jobOutput, let s = source else { return }
        finishing = true
        Task { @MainActor in
            defer { finishing = false }
            guard let media = await Self.probe(out, name: s.name) else { error = AureaText.t("hyper_error_result"); return }
            let title = "\(s.name) · \(recipe.summary)"
            let w = UInt32(media.width) & ~UInt32(1), h = UInt32(media.height) & ~UInt32(1)
            guard model.newProject(width: w, height: h, fps: media.fps, title: title) else {
                error = AureaText.t("hyper_error_project"); return
            }
            model.importMedia(url: out, kind: .video)
            let deadline = Date().addingTimeInterval(300)
            while model.importingMedia, Date() < deadline { try? await Task.sleep(nanoseconds: 100_000_000) }
            _ = model.saveProject(writeThumbnail: true)
            // O import copiou o vídeo para Documents/Media: a saída e a fonte já não servem.
            try? FileManager.default.removeItem(at: out)
            try? FileManager.default.removeItem(atPath: s.path)
            clearPending()
            source = nil; preset = nil; recipe = HyperRecipe(); preview = nil; estimate = nil; jobInfo = nil
            step = .choose
        }
    }

    // ---- job interrompido ------------------------------------------------------

    private func savePending(_ p: Pending) {
        let o: [String: Any] = ["path": p.source.path, "name": p.source.name, "width": p.source.width, "height": p.source.height,
                                "fps": p.source.fps, "duration": p.source.duration, "recipe": p.recipe.packed,
                                "route": p.path.rawValue, "output": p.output]
        UserDefaults.standard.set(o, forKey: Self.pendingKey)
    }

    private func clearPending() {
        pending = nil
        UserDefaults.standard.removeObject(forKey: Self.pendingKey)
    }

    private func loadPending() -> Pending? {
        guard let o = UserDefaults.standard.dictionary(forKey: Self.pendingKey),
              let path = o["path"] as? String, FileManager.default.fileExists(atPath: path),
              let packed = o["recipe"] as? [Double], let r = HyperRecipe(packed: packed),
              let output = o["output"] as? String else { return nil }
        let s = Source(path: path, name: o["name"] as? String ?? "", width: o["width"] as? Int ?? 0, height: o["height"] as? Int ?? 0,
                       fps: o["fps"] as? Double ?? 30, duration: o["duration"] as? Double ?? 0)
        return Pending(source: s, recipe: r, path: .of(o["route"] as? Int ?? 1), output: output)
    }

    func resumePending() {
        guard let p = pending, !model.exporting else { return }
        source = p.source
        recipe = p.recipe
        preset = nil
        path = p.path
        start(resume: URL(fileURLWithPath: p.output))
    }

    func discardPending() {
        guard let p = pending else { return }
        clearPending()
        try? FileManager.default.removeItem(atPath: p.output)
        try? FileManager.default.removeItem(atPath: p.source.path)
    }
}
