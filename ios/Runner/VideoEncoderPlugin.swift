import AVFoundation
import Flutter
import UIKit

/// CODIFICADOR DE VIDEO DA PLATAFORMA (iOS).
///
/// Mesmo papel do MediaCodec no Android: tira o x264 do caminho. O
/// AVAssetWriter usa o VideoToolbox, que e hardware — e nao arrasta
/// licenca GPL para dentro do aplicativo.
///
/// Os quadros chegam como PNG ja desenhados pelo Flutter; aqui viram
/// CVPixelBuffer e entram no fluxo.
final class VideoEncoderPlugin: NSObject {

    private var writer: AVAssetWriter?
    private var input: AVAssetWriterInput?
    private var adaptor: AVAssetWriterInputPixelBufferAdaptor?
    private var frameIndex: Int64 = 0
    private var fps: Int32 = 30
    private var width = 0
    private var height = 0

    private let queue = DispatchQueue(label: "aurea.encoder")

    static func register(with registrar: FlutterPluginRegistrar) {
        let channel = FlutterMethodChannel(
            name: "aurea/encoder",
            binaryMessenger: registrar.messenger()
        )
        let instance = VideoEncoderPlugin()
        channel.setMethodCallHandler { call, result in
            instance.queue.async {
                do {
                    let value = try instance.handle(call)
                    DispatchQueue.main.async { result(value) }
                } catch {
                    DispatchQueue.main.async {
                        result(FlutterError(
                            code: "encoder",
                            message: "\(error)",
                            details: nil))
                    }
                }
            }
        }
    }

    private func handle(_ call: FlutterMethodCall) throws -> Any? {
        let args = call.arguments as? [String: Any] ?? [:]
        switch call.method {
        case "available":
            return true

        case "start":
            try start(
                path: args["path"] as! String,
                w: args["width"] as! Int,
                h: args["height"] as! Int,
                fps: args["fps"] as! Int,
                bitrate: args["bitrate"] as! Int,
                hevc: (args["hevc"] as? Bool) ?? false
            )
            return true

        case "frame":
            try encode(path: args["path"] as! String)
            return true

        case "frames":
            let paths = args["paths"] as! [String]
            for p in paths { try encode(path: p) }
            return paths.count

        case "finish":
            return try finish()

        case "cancel":
            writer?.cancelWriting()
            reset()
            return true

        case "remux":
            return try remux(
                source: args["source"] as! String,
                target: args["target"] as! String,
                startUs: (args["startUs"] as? NSNumber)?.int64Value ?? 0,
                endUs: (args["endUs"] as? NSNumber)?.int64Value ?? 0
            )

        default:
            return nil
        }
    }

    private func start(
        path: String, w: Int, h: Int, fps: Int, bitrate: Int, hevc: Bool = false
    ) throws {
        reset()
        // O H.264 exige dimensao par.
        width = w % 2 == 0 ? w : w + 1
        height = h % 2 == 0 ? h : h + 1
        self.fps = Int32(fps < 1 ? 30 : fps)

        let url = URL(fileURLWithPath: path)
        try? FileManager.default.removeItem(at: url)
        try FileManager.default.createDirectory(
            at: url.deletingLastPathComponent(),
            withIntermediateDirectories: true)

        let escritor = try AVAssetWriter(outputURL: url, fileType: .mp4)

        // HEVC so quando o aparelho REALMENTE aceita: sem codificador de
        // H.265 a exportacao nao pode falhar — cai no H.264, que todo
        // aparelho tem.
        //
        // Quem responde e o PROPRIO ESCRITOR, com estes ajustes: canApply
        // e metodo de instancia, nao de tipo. Duas versoes anteriores
        // erraram aqui — primeiro VTIsHardwareDecodeSupported, que
        // pergunta por DECODIFICACAO e nem existe no iOS; depois
        // `AVAssetWriter.canApply`, chamado no tipo.
        let testeHevc: [String: Any] = [
            AVVideoCodecKey: AVVideoCodecType.hevc,
            AVVideoWidthKey: width,
            AVVideoHeightKey: height,
        ]
        let usaHevc =
            hevc
            && escritor.canApply(
                outputSettings: testeHevc, forMediaType: .video)
        var compressao: [String: Any] = [
            AVVideoAverageBitRateKey: bitrate,
            // Um quadro-chave por segundo: buscar no arquivo fica rapido.
            AVVideoMaxKeyFrameIntervalKey: Int(self.fps),
        ]
        if !usaHevc {
            compressao[AVVideoProfileLevelKey] = AVVideoProfileLevelH264HighAutoLevel
        }
        let settings: [String: Any] = [
            AVVideoCodecKey: usaHevc ? AVVideoCodecType.hevc : AVVideoCodecType.h264,
            AVVideoWidthKey: width,
            AVVideoHeightKey: height,
            AVVideoCompressionPropertiesKey: compressao,
        ]
        let inp = AVAssetWriterInput(mediaType: .video, outputSettings: settings)
        inp.expectsMediaDataInRealTime = false

        let attrs: [String: Any] = [
            kCVPixelBufferPixelFormatTypeKey as String:
                Int(kCVPixelFormatType_32BGRA),
            kCVPixelBufferWidthKey as String: width,
            kCVPixelBufferHeightKey as String: height,
        ]
        let ad = AVAssetWriterInputPixelBufferAdaptor(
            assetWriterInput: inp, sourcePixelBufferAttributes: attrs)

        guard escritor.canAdd(inp) else {
            throw NSError(domain: "aurea", code: 1,
                          userInfo: [NSLocalizedDescriptionKey:
                                        "Nao consegui criar a trilha de video"])
        }
        escritor.add(inp)
        escritor.startWriting()
        escritor.startSession(atSourceTime: .zero)

        writer = escritor
        input = inp
        adaptor = ad
        frameIndex = 0
    }

    private func encode(path: String) throws {
        guard let adaptor = adaptor, let input = input else {
            throw NSError(domain: "aurea", code: 2,
                          userInfo: [NSLocalizedDescriptionKey:
                                        "Codificador nao iniciado"])
        }
        guard let image = UIImage(contentsOfFile: path)?.cgImage else {
            throw NSError(domain: "aurea", code: 3,
                          userInfo: [NSLocalizedDescriptionKey:
                                        "Quadro ilegivel: \(path)"])
        }

        // Espera o codificador aceitar mais dados em vez de descartar.
        while !input.isReadyForMoreMediaData {
            Thread.sleep(forTimeInterval: 0.004)
        }

        var pixelBuffer: CVPixelBuffer?
        guard let pool = adaptor.pixelBufferPool else {
            throw NSError(domain: "aurea", code: 4,
                          userInfo: [NSLocalizedDescriptionKey:
                                        "Sem pool de buffers"])
        }
        CVPixelBufferPoolCreatePixelBuffer(nil, pool, &pixelBuffer)
        guard let buffer = pixelBuffer else {
            throw NSError(domain: "aurea", code: 5,
                          userInfo: [NSLocalizedDescriptionKey:
                                        "Sem buffer de pixel"])
        }

        CVPixelBufferLockBaseAddress(buffer, [])
        if let context = CGContext(
            data: CVPixelBufferGetBaseAddress(buffer),
            width: width,
            height: height,
            bitsPerComponent: 8,
            bytesPerRow: CVPixelBufferGetBytesPerRow(buffer),
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.premultipliedFirst.rawValue
                | CGBitmapInfo.byteOrder32Little.rawValue
        ) {
            context.clear(CGRect(x: 0, y: 0, width: width, height: height))
            context.draw(image,
                         in: CGRect(x: 0, y: 0, width: width, height: height))
        }
        CVPixelBufferUnlockBaseAddress(buffer, [])

        let pts = CMTime(value: frameIndex, timescale: fps)
        adaptor.append(buffer, withPresentationTime: pts)
        frameIndex += 1
    }

    private func finish() throws -> Bool {
        guard let writer = writer, let input = input else { return false }
        input.markAsFinished()
        let sem = DispatchSemaphore(value: 0)
        writer.finishWriting { sem.signal() }
        sem.wait()
        let ok = writer.status == .completed
        let err = writer.error
        reset()
        if !ok, let err = err { throw err }
        return ok
    }

    private func reset() {
        writer = nil
        input = nil
        adaptor = nil
        frameIndex = 0
    }

    /// REMUX: corte puro sem recodificar.
    private func remux(source: String, target: String,
                       startUs: Int64, endUs: Int64) throws -> Bool {
        let asset = AVURLAsset(url: URL(fileURLWithPath: source))
        guard let session = AVAssetExportSession(
            asset: asset, presetName: AVAssetExportPresetPassthrough)
        else { return false }

        let url = URL(fileURLWithPath: target)
        try? FileManager.default.removeItem(at: url)
        session.outputURL = url
        session.outputFileType = .mp4
        session.shouldOptimizeForNetworkUse = true
        if endUs > startUs {
            session.timeRange = CMTimeRange(
                start: CMTime(value: startUs, timescale: 1_000_000),
                duration: CMTime(value: endUs - startUs, timescale: 1_000_000))
        }

        let sem = DispatchSemaphore(value: 0)
        session.exportAsynchronously { sem.signal() }
        sem.wait()
        if let err = session.error { throw err }
        return session.status == .completed
    }
}
