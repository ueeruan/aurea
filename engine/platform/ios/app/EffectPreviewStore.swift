// Port of effects/EffectPreview.kt: the same photo and core renderer, off main.
import Foundation
import UIKit
import ImageIO
import UniformTypeIdentifiers

/// Cenas-fonte da prévia animada (EffectPreviewScene no motor; a ordem é contrato).
enum FxPreviewScene: UInt32, CaseIterable, Identifiable {
    case photo = 0, plate = 1, shapes = 2, lowLight = 3
    var id: UInt32 { rawValue }
    var labelKey: String {
        switch self {
        case .photo: return "fx_preview_scene_photo"
        case .plate: return "fx_preview_scene_plate"
        case .shapes: return "fx_preview_scene_shapes"
        case .lowLight: return "fx_preview_scene_low_light"
        }
    }
}

final class EffectPreviewStore {
    private let engine: AureaEngine
    private let queue = DispatchQueue(label: "aurea.effect-previews", qos: .utility)
    private let memory = NSCache<NSNumber, UIImage>()
    private let directory: URL
    // Accessed only on queue. A foto é tentada UMA vez; se faltar ou não
    // decodificar, o motor usa a cartela procedural (igual ao Android).
    private var sourceAttempted = false
    private let pressureLock = NSLock()
    private var pressureEpoch: UInt64 = 0
    private var pressurePaused = false

    private func requestEpoch() -> UInt64? {
        pressureLock.lock(); defer { pressureLock.unlock() }
        return pressurePaused ? nil : pressureEpoch
    }

    func trimMemory() {
        pressureLock.lock(); pressureEpoch &+= 1; pressurePaused = true; pressureLock.unlock()
        memory.removeAllObjects()
        sequences.removeAllObjects()
    }

    func resumeMemoryWork() {
        pressureLock.lock(); pressurePaused = false; pressureLock.unlock()
    }

    init(engine: AureaEngine) {
        self.engine = engine
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "unknown"
        // -compare-v3: imagem de amostra nova (0.2.4) e recorte centralizado.
        directory = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("effect-previews/\(version)-compare-v3", isDirectory: true)
        // Classe de memória LOW (até ~4 GB): metade, como no Android.
        memory.totalCostLimit = DeviceMemoryClass.low ? 12 * 1024 * 1024 : 24 * 1024 * 1024
        sequences.totalCostLimit = DeviceMemoryClass.low ? 4 * 1024 * 1024 : 9 * 1024 * 1024
        // Pastas de versões/chaves antigas (com prévias da foto antiga) ficavam
        // órfãs: apaga as irmãs da atual, fora do main thread (como o Android
        // faz em EffectPreview.kt ensureDir).
        let current = directory.lastPathComponent
        let root = directory.deletingLastPathComponent()
        queue.async {
            let fm = FileManager.default
            guard let siblings = try? fm.contentsOfDirectory(at: root, includingPropertiesForKeys: nil) else { return }
            for sibling in siblings where sibling.lastPathComponent != current {
                try? fm.removeItem(at: sibling)
            }
        }
    }

    // A cancelled/offscreen tile must not enqueue more GPU work. Cancellation
    // cannot interrupt a native render already in flight; its result is cached.
    private final class Request {
        private let lock = NSLock()
        private var cancelled = false
        func cancel() { lock.lock(); cancelled = true; lock.unlock() }
        var isCancelled: Bool { lock.lock(); defer { lock.unlock() }; return cancelled }
    }

    func image(for typeId: UInt32) async -> UIImage? {
        guard let epoch = requestEpoch() else { return nil }
        let request = Request()
        return await withTaskCancellationHandler(operation: {
            await withCheckedContinuation { continuation in
                queue.async { [self] in
                    guard !request.isCancelled, self.requestEpoch() == epoch else { continuation.resume(returning: nil); return }
                    let image = autoreleasepool { load(typeId) }
                    guard self.requestEpoch() == epoch else {
                        memory.removeAllObjects()
                        continuation.resume(returning: nil); return
                    }
                    continuation.resume(returning: request.isCancelled ? nil : image)
                }
            }
        }, onCancel: { request.cancel() })
    }

    func clear() async -> Int64 {
        await withCheckedContinuation { continuation in
            queue.async { [self] in
                memory.removeAllObjects()
                sequences.removeAllObjects()
                var freed: Int64 = 0
                if let strips = try? FileManager.default.contentsOfDirectory(at: animatedDirectory, includingPropertiesForKeys: [.fileSizeKey]) {
                    for file in strips {
                        let bytes = (try? file.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0
                        do { try FileManager.default.removeItem(at: file); freed += Int64(bytes) } catch { }
                    }
                }
                if let files = try? FileManager.default.contentsOfDirectory(at: directory, includingPropertiesForKeys: [.fileSizeKey]) {
                    for file in files where file.pathExtension == "png" {
                        let bytes = (try? file.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0
                        do { try FileManager.default.removeItem(at: file); freed += Int64(bytes) } catch { }
                    }
                }
                continuation.resume(returning: freed)
            }
        }
    }

    private func load(_ typeId: UInt32) -> UIImage? {
        let key = NSNumber(value: typeId)
        if let image = memory.object(forKey: key) { return image }
        let file = directory.appendingPathComponent("\(typeId)@320x200.png")
        if let image = UIImage(contentsOfFile: file.path) {
            cache(image, key: key)
            return image
        }
        // Foto ausente ou ilegível: o motor desenha a cartela procedural no
        // lugar (paridade com EditorStore.kt ensureEffectPreviewPhoto), em vez
        // de deixar todo cartão em "Prévia indisponível".
        ensureSource()
        var size = CGSize.zero
        guard let rgba = engine.effectPreview(typeId, width: 320, height: 200, outSize: &size),
              let image = UIImage.fromRGBA(rgba, width: Int(size.width), height: Int(size.height)) else { return nil }
        cache(image, key: key)
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        if let png = image.pngData() { try? png.write(to: file, options: .atomic) }
        return image
    }

    // =========================================================================
    //  PRÉVIA ANIMADA (port de effects/EffectAnimatedPreview.kt). O motor
    //  renderiza 16 quadros antes/depois sobre a cena escolhida; aqui só se
    //  pede, guarda e devolve. Gerar é só da ficha (`generate`); o cartão
    //  segurado lê o que já estiver pronto. Busy (o preview do editor está com
    //  a GPU) repete com recuo exponencial; cancelar a Task para a espera.
    //  Disco: uma tira por (efeito, cena) em `<pasta versionada>/animated`,
    //  HEIC (PNG se o aparelho não codificar HEIC), teto de 24 MB por LRU.
    // =========================================================================
    static let sequenceWidth: UInt32 = 320
    static let sequenceHeight: UInt32 = 200
    static let sequenceFrames: UInt32 = 16
    static let sequenceFrameSeconds: Double = 0.066
    private static let sequenceDiskCap: Int = 24 * 1024 * 1024
    private static let busyStatus: Int32 = 32   // Errc::Busy (core/Result.hpp)
    private final class FrameBox { let frames: [UIImage]; init(_ frames: [UIImage]) { self.frames = frames } }
    private let sequences = NSCache<NSString, FrameBox>()
    private var animatedDirectory: URL { directory.appendingPathComponent("animated", isDirectory: true) }

    /// A sequência do efeito na cena: memória → disco → (com `generate`) GPU.
    /// Nil = sem animação (ainda): quem chama mostra a prévia parada.
    func sequence(for typeId: UInt32, scene: FxPreviewScene, generate: Bool) async -> [UIImage]? {
        var wait: UInt64 = 150_000_000
        for attempt in 0..<7 {
            if Task.isCancelled { return nil }
            guard let epoch = requestEpoch() else { return nil }
            let request = Request()
            let outcome: SequenceOutcome = await withTaskCancellationHandler(operation: {
                await withCheckedContinuation { continuation in
                    queue.async { [self] in
                        guard !request.isCancelled, self.requestEpoch() == epoch else {
                            continuation.resume(returning: SequenceOutcome(frames: nil, status: -1)); return
                        }
                        let result = autoreleasepool { loadSequence(typeId, scene: scene, generate: generate) }
                        continuation.resume(returning: request.isCancelled ? SequenceOutcome(frames: nil, status: -1) : result)
                    }
                }
            }, onCancel: { request.cancel() })
            if let frames = outcome.frames, !frames.isEmpty { return frames }
            if !generate || outcome.status != Self.busyStatus || attempt == 6 { return nil }
            try? await Task.sleep(nanoseconds: wait)
            wait = min(wait * 2, 2_400_000_000)
        }
        return nil
    }

    private struct SequenceOutcome {
        let frames: [UIImage]?
        let status: Int32
    }

    private func sequenceKey(_ typeId: UInt32, _ scene: FxPreviewScene) -> String {
        "\(typeId)@\(Self.sequenceWidth)x\(Self.sequenceHeight)-c\(scene.rawValue)"
    }

    private func loadSequence(_ typeId: UInt32, scene: FxPreviewScene, generate: Bool) -> SequenceOutcome {
        let key = sequenceKey(typeId, scene)
        if let box = sequences.object(forKey: key as NSString) { return SequenceOutcome(frames: box.frames, status: 0) }
        if let frames = readStrip(key) {
            cacheSequence(frames, key: key)
            return SequenceOutcome(frames: frames, status: 0)
        }
        guard generate else { return SequenceOutcome(frames: nil, status: 0) }
        ensureSource()
        var w: UInt32 = 0, h: UInt32 = 0, n: UInt32 = 0
        var status: Int32 = 0
        guard let rgba = engine.effectPreviewSequence(typeId, width: Self.sequenceWidth, height: Self.sequenceHeight,
                                                      frames: Self.sequenceFrames, scene: scene.rawValue,
                                                      outWidth: &w, outHeight: &h, outFrames: &n, status: &status),
              w > 0, h > 0, n > 0 else { return SequenceOutcome(frames: nil, status: status) }
        let frameBytes = Int(w) * Int(h) * 4
        guard rgba.count >= frameBytes * Int(n) else { return SequenceOutcome(frames: nil, status: -1) }
        var frames: [UIImage] = []
        frames.reserveCapacity(Int(n))
        for i in 0..<Int(n) {
            let slice = rgba.subdata(in: (i * frameBytes)..<((i + 1) * frameBytes))
            guard let image = UIImage.fromRGBA(slice, width: Int(w), height: Int(h)) else {
                return SequenceOutcome(frames: nil, status: -1)
            }
            frames.append(image)
        }
        cacheSequence(frames, key: key)
        writeStrip(frames, key: key)
        return SequenceOutcome(frames: frames, status: 0)
    }

    private func cacheSequence(_ frames: [UIImage], key: String) {
        let cost = frames.reduce(0) { total, frame in total + (frame.cgImage.map { $0.bytesPerRow * $0.height } ?? 0) }
        sequences.setObject(FrameBox(frames), forKey: key as NSString, cost: cost)
    }

    /// Tira: quadros empilhados na vertical; o nome guarda quantos são.
    private func readStrip(_ key: String) -> [UIImage]? {
        let fm = FileManager.default
        guard let files = try? fm.contentsOfDirectory(at: animatedDirectory, includingPropertiesForKeys: nil),
              let file = files.first(where: { $0.lastPathComponent.hasPrefix(key + "-n") }) else { return nil }
        let stem = file.deletingPathExtension().lastPathComponent
        guard let count = Int(stem.dropFirst(key.count + 2)), count > 0, count <= 64,
              let source = CGImageSourceCreateWithURL(file as CFURL, nil),
              let strip = CGImageSourceCreateImageAtIndex(source, 0, nil) else {
            try? fm.removeItem(at: file)
            return nil
        }
        let h = strip.height / count
        guard h > 0, strip.width > 0 else { try? fm.removeItem(at: file); return nil }
        var frames: [UIImage] = []
        for i in 0..<count {
            guard let part = strip.cropping(to: CGRect(x: 0, y: i * h, width: strip.width, height: h)) else {
                try? fm.removeItem(at: file)
                return nil
            }
            frames.append(UIImage(cgImage: part))
        }
        try? fm.setAttributes([.modificationDate: Date()], ofItemAtPath: file.path)   // LRU: usado agora
        return frames
    }

    private func writeStrip(_ frames: [UIImage], key: String) {
        guard let first = frames.first?.cgImage else { return }
        let w = first.width, h = first.height
        let fm = FileManager.default
        try? fm.createDirectory(at: animatedDirectory, withIntermediateDirectories: true)
        guard let space = CGColorSpace(name: CGColorSpace.sRGB),
              let context = CGContext(data: nil, width: w, height: h * frames.count, bitsPerComponent: 8, bytesPerRow: 0,
                                      space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return }
        for (i, frame) in frames.enumerated() {
            guard let cg = frame.cgImage else { return }
            // CoreGraphics tem a origem embaixo: o quadro 0 fica no topo da tira.
            context.draw(cg, in: CGRect(x: 0, y: (frames.count - 1 - i) * h, width: w, height: h))
        }
        guard let strip = context.makeImage() else { return }
        for (type, ext) in [(UTType.heic, "heic"), (UTType.png, "png")] {
            let file = animatedDirectory.appendingPathComponent("\(key)-n\(frames.count).\(ext)")
            guard let destination = CGImageDestinationCreateWithURL(file as CFURL, type.identifier as CFString, 1, nil) else { continue }
            CGImageDestinationAddImage(destination, strip, [kCGImageDestinationLossyCompressionQuality: 0.8] as CFDictionary)
            if CGImageDestinationFinalize(destination) { enforceSequenceCap(); return }
            try? fm.removeItem(at: file)
        }
    }

    /// Teto de disco das tiras: apaga as menos usadas até caber.
    private func enforceSequenceCap() {
        let keys: [URLResourceKey] = [.fileSizeKey, .contentModificationDateKey]
        guard let files = try? FileManager.default.contentsOfDirectory(at: animatedDirectory,
                                                                       includingPropertiesForKeys: keys) else { return }
        var entries = files.map { url -> (url: URL, bytes: Int, used: Date) in
            let values = try? url.resourceValues(forKeys: Set(keys))
            return (url, values?.fileSize ?? 0, values?.contentModificationDate ?? .distantPast)
        }
        var total = entries.reduce(0) { $0 + $1.bytes }
        guard total > Self.sequenceDiskCap else { return }
        entries.sort { $0.used < $1.used }
        for entry in entries where total > Self.sequenceDiskCap {
            if (try? FileManager.default.removeItem(at: entry.url)) != nil { total -= entry.bytes }
        }
    }

    private func cache(_ image: UIImage, key: NSNumber) {
        let cost = image.cgImage.map { $0.bytesPerRow * $0.height } ?? 0
        memory.setObject(image, forKey: key, cost: cost)
    }

    private func ensureSource() {
        if sourceAttempted { return }
        sourceAttempted = true
        guard let url = Bundle.main.url(forResource: "previa_efeitos", withExtension: "jpg"),
              let source = CGImageSourceCreateWithURL(url as CFURL, nil),
              let image = CGImageSourceCreateImageAtIndex(source, 0, nil),
              let space = CGColorSpace(name: CGColorSpace.sRGB) else { return }
        let width = image.width, height = image.height
        var pixels = [UInt8](repeating: 0, count: width * height * 4)
        let decoded = pixels.withUnsafeMutableBytes { bytes -> Bool in
            guard let context = CGContext(data: bytes.baseAddress, width: width, height: height,
                                          bitsPerComponent: 8, bytesPerRow: width * 4, space: space,
                                          bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue) else { return false }
            context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
            return true
        }
        // The official JPEG is opaque: premultiplication preserves its RGB.
        guard decoded else { return }
        _ = engine.setEffectPreviewSource(Data(pixels), width: UInt32(width), height: UInt32(height))
    }
}
