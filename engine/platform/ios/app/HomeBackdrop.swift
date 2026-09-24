// Home/Backdrop.kt. Capture only the Home list, never its sibling bars or the
// editor. Public UIKit/Core Image APIs; no private backdrop filters/materials.
import SwiftUI
import UIKit
import CoreImage
import QuartzCore

private struct HomeBackdropKey: EnvironmentKey {
    static let defaultValue: HomeBackdrop? = nil
}
extension EnvironmentValues {
    var homeBackdrop: HomeBackdrop? {
        get { self[HomeBackdropKey.self] }
        set { self[HomeBackdropKey.self] = newValue }
    }
}

enum HomeGlassKind: String {
    case tab, batch
    var sigma: CGFloat { self == .tab ? 24 : 20 }
    var tint: UIColor {
        UIColor(self == .tab ? AureaColors.background : AureaColors.surface)
            .withAlphaComponent(self == .tab ? 0.72 : 0.88)
    }
    var floor: UIColor {
        UIColor(self == .tab ? AureaColors.background : AureaColors.surface)
            .withAlphaComponent(self == .tab ? 0.97 : 1)
    }
}

/// Kept inside the ScrollView content so walking its public superview chain
/// selects that list alone, without depending on SwiftUI's private class names.
struct HomeBackdropSource: UIViewRepresentable {
    @Environment(\.homeBackdrop) private var backdrop
    let tab: HomeTabKind
    func makeUIView(context: Context) -> HomeBackdropMarker { HomeBackdropMarker() }
    func updateUIView(_ view: HomeBackdropMarker, context: Context) {
        view.backdrop = backdrop; view.tab = tab
        view.resolveSource()
    }
}

final class HomeBackdropMarker: UIView {
    weak var backdrop: HomeBackdrop?
    var tab: HomeTabKind = .start
    override init(frame: CGRect) {
        super.init(frame: frame)
        isUserInteractionEnabled = false; accessibilityElementsHidden = true
        backgroundColor = .clear
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
    override func didMoveToWindow() { super.didMoveToWindow(); resolveSource() }
    override func layoutSubviews() { super.layoutSubviews(); resolveSource() }
    func resolveSource() {
        guard window != nil else { return }
        var ancestor = superview
        while let view = ancestor {
            if let scroll = view as? UIScrollView {
                backdrop?.register(scroll, tab: tab)
                return
            }
            ancestor = view.superview
        }
    }
}

struct HomeGlass: UIViewRepresentable {
    @Environment(\.homeBackdrop) private var backdrop
    let kind: HomeGlassKind
    func makeUIView(context: Context) -> HomeGlassView { HomeGlassView(kind: kind) }
    func updateUIView(_ view: HomeGlassView, context: Context) {
        view.backdrop = backdrop
        backdrop?.register(view)
    }
    static func dismantleUIView(_ view: HomeGlassView, coordinator: ()) {
        view.backdrop?.remove(view)
    }
}

final class HomeGlassView: UIView {
    let kind: HomeGlassKind
    weak var backdrop: HomeBackdrop?
    private let image = UIImageView()
    private let tint = UIView()
    init(kind: HomeGlassKind) {
        self.kind = kind
        super.init(frame: .zero)
        isUserInteractionEnabled = false; accessibilityElementsHidden = true
        clipsToBounds = true; backgroundColor = kind.floor
        image.contentMode = .scaleToFill
        tint.backgroundColor = kind.tint
        addSubview(image); addSubview(tint)
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
    override func didMoveToWindow() {
        super.didMoveToWindow()
        if window != nil { backdrop?.register(self) }
        else { backdrop?.remove(self) }
    }
    override func layoutSubviews() {
        super.layoutSubviews()
        let changed = image.frame != bounds
        image.frame = bounds; tint.frame = bounds
        if changed { backdrop?.invalidate() }
    }
    func display(_ cgImage: CGImage, scale: CGFloat) {
        image.image = UIImage(cgImage: cgImage, scale: scale, orientation: .up)
    }
    func clear() { image.image = nil }
}

private final class HomeScrollReference {
    weak var view: UIScrollView?
    var observations: [NSKeyValueObservation] = []
    init(_ view: UIScrollView) { self.view = view }
}
private final class HomeGlassReference {
    weak var view: HomeGlassView?
    init(_ view: HomeGlassView) { self.view = view }
}
@MainActor private final class HomeDisplayLinkTarget: NSObject {
    weak var owner: HomeBackdrop?
    @objc func tick() { owner?.tick() }
}

private struct HomeBlurInput {
    let kind: HomeGlassKind
    let image: CGImage
    let scale: CGFloat
}
private struct HomeBlurOutput {
    let input: HomeBlurInput
    let image: CGImage
    let measuredSigma: Double
}

/// One serial renderer and one in-flight job. Rapid scroll changes coalesce;
/// when idle the display link is paused and neither snapshots nor filters run.
@MainActor final class HomeBackdrop: ObservableObject {
    private var sources: [HomeTabKind: HomeScrollReference] = [:]
    private var glasses: [HomeGlassKind: HomeGlassReference] = [:]
    private var activeTab: HomeTabKind = .start
    private var displayLink: CADisplayLink?
    private let target = HomeDisplayLinkTarget()
    private let queue = DispatchQueue(label: "com.aurea.home-backdrop", qos: .userInteractive)
    private let renderer = HomeBlurRenderer()
    private var dirty = false
    private var settle = false
    private var rendering = false
    private var running = false
    private var generation = 0
    #if DEBUG
    private var snapshotTimes: [Double] = []
    private var filterTimes: [Double] = []
    private var captureCounts: [HomeGlassKind: Int] = [:]
    private var failedSnapshots = 0
    private var debugScrolling = false
    private var debugScrollCompleted = false
    private var debugWritten = false
    #endif

    func start() {
        running = true
        if displayLink == nil {
            target.owner = self
            let link = CADisplayLink(target: target, selector: #selector(HomeDisplayLinkTarget.tick))
            link.preferredFrameRateRange = CAFrameRateRange(minimum: 30, maximum: 60, preferred: 60)
            link.isPaused = true; link.add(to: .main, forMode: .common)
            displayLink = link
        }
        invalidate()
    }
    func stop() {
        running = false; generation += 1; dirty = false; settle = false
        displayLink?.invalidate(); displayLink = nil
    }
    deinit { displayLink?.invalidate() }

    func select(_ tab: HomeTabKind) {
        guard activeTab != tab else { return }
        activeTab = tab; generation += 1
        glasses.values.forEach { $0.view?.clear() }
        invalidate()
        #if DEBUG
        if let scroll = sources[tab]?.view { beginDebugScrollIfReady(scroll, tab: tab) }
        #endif
    }

    func register(_ scroll: UIScrollView, tab: HomeTabKind) {
        guard sources[tab]?.view !== scroll else {
            #if DEBUG
            beginDebugScrollIfReady(scroll, tab: tab)
            #endif
            return
        }
        let reference = HomeScrollReference(scroll)
        reference.observations = [
            scroll.observe(\.contentOffset, options: [.new]) { [weak self] _, _ in
                DispatchQueue.main.async { if self?.activeTab == tab { self?.invalidate() } }
            },
            scroll.observe(\.contentSize, options: [.new]) { [weak self] view, _ in
                DispatchQueue.main.async {
                    guard let self else { return }
                    if self.activeTab == tab { self.invalidate() }
                    #if DEBUG
                    self.beginDebugScrollIfReady(view, tab: tab)
                    #endif
                }
            },
            scroll.observe(\.bounds, options: [.new]) { [weak self] _, _ in
                DispatchQueue.main.async { if self?.activeTab == tab { self?.invalidate() } }
            },
        ]
        sources[tab] = reference
        if activeTab == tab { invalidate() }
        #if DEBUG
        beginDebugScrollIfReady(scroll, tab: tab)
        #endif
    }
    func register(_ glass: HomeGlassView) {
        guard glasses[glass.kind]?.view !== glass else { return }
        glasses[glass.kind] = HomeGlassReference(glass)
        invalidate()
        #if DEBUG
        if let scroll = sources[activeTab]?.view { beginDebugScrollIfReady(scroll, tab: activeTab) }
        #endif
    }
    func remove(_ glass: HomeGlassView) {
        if glasses[glass.kind]?.view === glass { glasses.removeValue(forKey: glass.kind) }
    }
    func invalidate() {
        // SwiftUI may notify us just before committing its new contents. One
        // trailing pass makes the final idle image include that commit while
        // retaining afterScreenUpdates:false (no synchronous layout/snapshot).
        dirty = true; settle = true
        if running { displayLink?.isPaused = false }
    }

    fileprivate func tick() {
        guard running, dirty, !rendering else {
            if !dirty { displayLink?.isPaused = true }
            return
        }
        dirty = settle; settle = false
        guard let source = sources[activeTab]?.view, source.window != nil,
              source.bounds.width > 0, source.bounds.height > 0 else { return }
        let captureStart = CACurrentMediaTime()
        var inputs: [HomeBlurInput] = []
        for kind in [HomeGlassKind.tab, .batch] {
            guard kind != .batch || activeTab == .projects,
                  let glass = glasses[kind]?.view, glass.window === source.window,
                  glass.bounds.width > 0, glass.bounds.height > 0,
                  !glass.isDescendant(of: source) else { continue }
            let scale = source.window?.screen.scale ?? 1
            let region = glass.convert(glass.bounds, to: source)
                .offsetBy(dx: -source.bounds.minX, dy: -source.bounds.minY)
            let format = UIGraphicsImageRendererFormat()
            format.scale = scale; format.opaque = false; format.preferredRange = .standard
            var complete = false
            let image = UIGraphicsImageRenderer(size: glass.bounds.size, format: format).image { _ in
                // drawHierarchy maps the current visible bounds into this rect.
                // UIScrollView's bounds.origin already contains contentOffset.
                complete = source.drawHierarchy(in: CGRect(x: -region.minX, y: -region.minY,
                    width: source.bounds.width, height: source.bounds.height), afterScreenUpdates: false)
            }
            guard complete, let cgImage = image.cgImage else {
                #if DEBUG
                failedSnapshots += 1
                #endif
                glass.clear(); continue
            }
            inputs.append(HomeBlurInput(kind: kind, image: cgImage, scale: scale))
        }
        guard !inputs.isEmpty else { return }
        #if DEBUG
        snapshotTimes.append((CACurrentMediaTime() - captureStart) * 1000)
        #endif
        rendering = true
        let capturedGeneration = generation
        let work = inputs
        queue.async { [weak self, renderer] in
            let filterStart = CACurrentMediaTime()
            let results = work.compactMap { renderer.render($0) }
            let milliseconds = (CACurrentMediaTime() - filterStart) * 1000
            DispatchQueue.main.async {
                guard let self else { return }
                self.rendering = false
                guard self.running, self.generation == capturedGeneration else { return }
                for result in results {
                    self.glasses[result.input.kind]?.view?.display(result.image, scale: result.input.scale)
                    #if DEBUG
                    self.captureCounts[result.input.kind, default: 0] += 1
                    #endif
                }
                #if DEBUG
                self.filterTimes.append(milliseconds)
                self.writeDebugIfReady(results)
                #endif
            }
        }
    }

    #if DEBUG
    private var isDebugScene: Bool { ProcessInfo.processInfo.environment["AUREA_PARITY_SCENE"] == "home-scroll" }
    private func beginDebugScrollIfReady(_ scroll: UIScrollView, tab: HomeTabKind) {
        guard isDebugScene, tab == .projects, activeTab == .projects, !debugScrolling,
              scroll.bounds.height > 0, scroll.contentSize.height > scroll.bounds.height + 100,
              glasses[.batch]?.view != nil else { return }
        debugScrolling = true
        // Exercise offset invalidation in both directions, then leave the last
        // row's thumbnails under both bars, before the list's 120-point footer.
        DispatchQueue.main.asyncAfter(deadline: .now() + 1) { [weak self, weak scroll] in
            guard let self, let scroll else { return }
            let maximum = max(0, scroll.contentSize.height - scroll.bounds.height)
            let finalY = max(0, maximum - 140)
            for step in 0...18 {
                DispatchQueue.main.asyncAfter(deadline: .now() + Double(step) / 20) { [weak self, weak scroll] in
                    guard let self, let scroll, self.running else { return }
                    let progress = CGFloat(step) / 18
                    let y = finalY + sin(progress * .pi) * min(100, maximum - finalY)
                    scroll.setContentOffset(CGPoint(x: 0, y: y), animated: false)
                    self.invalidate()
                    if step == 18 {
                        DispatchQueue.main.asyncAfter(deadline: .now() + 0.6) { [weak self] in
                            self?.debugScrollCompleted = true; self?.invalidate()
                        }
                    }
                }
            }
        }
    }
    private func writeDebugIfReady(_ outputs: [HomeBlurOutput]) {
        guard isDebugScene, debugScrollCompleted, !debugWritten, outputs.count == 2 else { return }
        let folder = AureaPaths.documents
        var bars: [String: Any] = [:]
        for output in outputs {
            let input = output.input
            let prefix = "home-backdrop-\(input.kind.rawValue)"
            guard let source = UIImage(cgImage: input.image).pngData(),
                  let blur = UIImage(cgImage: output.image).pngData() else { return }
            do {
                try source.write(to: folder.appendingPathComponent(prefix + "-source.png"), options: .atomic)
                try blur.write(to: folder.appendingPathComponent(prefix + "-blur.png"), options: .atomic)
            } catch { return }
            // Double explícito, nunca CGFloat: `CGFloat` dentro de `[String: Any]`
            // chega ao NSJSONSerialization como `__SwiftValue` — um tipo Swift
            // sem ponte — e a API LEVANTA (não devolve erro). `try?` não pega
            // exceção de ObjC, então isso derrubava o app inteiro. Ver
            // bridge/AureaJSON.h.
            bars[input.kind.rawValue] = ["captureCount": captureCounts[input.kind, default: 0],
                "sigmaPoints": Double(input.kind.sigma), "measuredSigmaPixels": output.measuredSigma,
                "scale": Double(input.scale), "sourceWidth": input.image.width, "sourceHeight": input.image.height]
        }
        func metrics(_ values: [Double]) -> [String: Any] {
            let sorted = values.sorted()
            return ["count": sorted.count, "mean": sorted.reduce(0, +) / Double(max(1, sorted.count)),
                    "p95": sorted.isEmpty ? 0 : sorted[min(sorted.count - 1, Int(Double(sorted.count) * 0.95))],
                    "max": sorted.last ?? 0]
        }
        let report: [String: Any] = ["version": 1, "sourceOnly": true, "scrollCompleted": true,
            "bars": bars, "snapshotMilliseconds": metrics(snapshotTimes),
            "filterMilliseconds": metrics(filterTimes), "failedSnapshots": failedSnapshots,
            "frames": snapshotTimes.count,
            "contentOffsetY": Double(sources[.projects]?.view?.contentOffset.y ?? 0)]
        if let data = AureaJSONData(report, true) {
            do { try data.write(to: folder.appendingPathComponent("home-backdrop.json"), options: .atomic); debugWritten = true }
            catch { }
        }
    }
    #endif
}

/// Core Image's public parameter is named radius, whereas Android's token is
/// sigma. Measure an impulse once per scale/token and calibrate its second
/// moment, instead of assuming the radius names mean the same on both APIs.
private final class HomeBlurRenderer {
    private let colorSpace = CGColorSpace(name: CGColorSpace.sRGB)!
    private lazy var context = CIContext(options: [.workingColorSpace: colorSpace, .cacheIntermediates: false])
    private var kernels: [Double: (radius: Double, sigma: Double)] = [:]

    func render(_ input: HomeBlurInput) -> HomeBlurOutput? {
        let sigma = Double(input.kind.sigma * input.scale)
        let kernel = kernels[sigma] ?? calibrate(sigma)
        kernels[sigma] = kernel
        guard kernel.sigma > 0 else { return nil }
        let image = CIImage(cgImage: input.image)
        // Backdrop.kt records a layer of the BAR's size before BlurEffect with
        // TileMode.Clamp. GraphicsLayer's offscreen raster is clipped to size:
        // https://developer.android.com/reference/kotlin/androidx/compose/ui/graphics/layer/GraphicsLayer
        // Expanding this strip before filtering would change that edge behavior.
        let blurred = image.clampedToExtent().applyingFilter("CIGaussianBlur", parameters: [kCIInputRadiusKey: kernel.radius])
            .cropped(to: image.extent)
        guard let result = context.createCGImage(blurred, from: image.extent, format: .RGBA8, colorSpace: colorSpace) else { return nil }
        return HomeBlurOutput(input: input, image: result, measuredSigma: kernel.sigma)
    }

    private func calibrate(_ sigma: Double) -> (radius: Double, sigma: Double) {
        let width = Int(ceil(sigma * 12)) | 1
        let center = width / 2
        var pixels = [Float](repeating: 0, count: width * 4)
        for x in 0..<width { pixels[x * 4 + 3] = 1 }
        for channel in 0..<3 { pixels[center * 4 + channel] = 1 }
        let data = pixels.withUnsafeBytes { Data($0) }
        let image = CIImage(bitmapData: data, bytesPerRow: width * 4 * MemoryLayout<Float>.size,
                            size: CGSize(width: CGFloat(width), height: 1), format: .RGBAf, colorSpace: colorSpace)
        var radius = sigma
        var measured = 0.0
        for iteration in 0..<3 {
            let blurred = image.clampedToExtent().applyingFilter("CIGaussianBlur", parameters: [kCIInputRadiusKey: radius])
            var result = [Float](repeating: 0, count: width * 4)
            result.withUnsafeMutableBytes { buffer in
                context.render(blurred, toBitmap: buffer.baseAddress!, rowBytes: width * 4 * MemoryLayout<Float>.size,
                               bounds: image.extent, format: .RGBAf, colorSpace: colorSpace)
            }
            var sum = 0.0
            var moment = 0.0
            for x in 0..<width {
                let weight = max(0, Double(result[x * 4]))
                sum += weight; moment += weight * Double((x - center) * (x - center))
            }
            guard sum > 0 else { break }
            measured = sqrt(moment / sum)
            guard measured > 0, abs(measured - sigma) / sigma > 0.005 else { break }
            if iteration < 2 { radius *= sigma / measured }
        }
        return (radius, measured)
    }
}
