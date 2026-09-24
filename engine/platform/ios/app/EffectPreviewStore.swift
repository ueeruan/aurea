// Port of effects/EffectPreview.kt: the same photo and core renderer, off main.
import Foundation
import UIKit
import ImageIO

final class EffectPreviewStore {
    private let engine: AureaEngine
    private let queue = DispatchQueue(label: "aurea.effect-previews", qos: .utility)
    private let memory = NSCache<NSNumber, UIImage>()
    private let directory: URL
    private var sourceLoaded = false // Accessed only on queue.

    init(engine: AureaEngine) {
        self.engine = engine
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "unknown"
        directory = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("effect-previews/\(version)-photo-v1", isDirectory: true)
        memory.totalCostLimit = 24 * 1024 * 1024
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
        let request = Request()
        return await withTaskCancellationHandler(operation: {
            await withCheckedContinuation { continuation in
                queue.async { [self] in
                    guard !request.isCancelled else { continuation.resume(returning: nil); return }
                    let image = autoreleasepool { load(typeId) }
                    continuation.resume(returning: request.isCancelled ? nil : image)
                }
            }
        }, onCancel: { request.cancel() })
    }

    func clear() async -> Int64 {
        await withCheckedContinuation { continuation in
            queue.async { [self] in
                memory.removeAllObjects()
                var freed: Int64 = 0
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
        // A missing official photo is not silently replaced by another image.
        guard ensureSource() else { return nil }
        var size = CGSize.zero
        guard let rgba = engine.effectPreview(typeId, width: 320, height: 200, outSize: &size),
              let image = UIImage.fromRGBA(rgba, width: Int(size.width), height: Int(size.height)) else { return nil }
        cache(image, key: key)
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        if let png = image.pngData() { try? png.write(to: file, options: .atomic) }
        return image
    }

    private func cache(_ image: UIImage, key: NSNumber) {
        let cost = image.cgImage.map { $0.bytesPerRow * $0.height } ?? 0
        memory.setObject(image, forKey: key, cost: cost)
    }

    private func ensureSource() -> Bool {
        if sourceLoaded { return true }
        guard let url = Bundle.main.url(forResource: "previa_efeitos", withExtension: "jpg"),
              let source = CGImageSourceCreateWithURL(url as CFURL, nil),
              let image = CGImageSourceCreateImageAtIndex(source, 0, nil),
              let space = CGColorSpace(name: CGColorSpace.sRGB) else { return false }
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
        guard decoded else { return false }
        sourceLoaded = engine.setEffectPreviewSource(Data(pixels), width: UInt32(width), height: UInt32(height))
        return sourceLoaded
    }
}
