import SwiftUI
import UIKit
import UniformTypeIdentifiers
import Darwin

// Serialized, bounded diagnostic journal. The watchdog never calls the engine
// or waits for the main thread, so it can record a UI stall while it is happening.
private final class PerformanceJournal: @unchecked Sendable {
    let url: URL
    private let queue = DispatchQueue(label: "aurea.performance.journal", qos: .utility)
    private let lock = NSLock()
    private var heartbeat = ProcessInfo.processInfo.systemUptime
    private var timer: DispatchSourceTimer?
    private var file: FileHandle?
    private var bytes = 0
    private var closed = false
    init() throws {
        let folder = AureaPaths.documents.appendingPathComponent("PerformanceReports", isDirectory: true)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        url = folder.appendingPathComponent("Aurea-iPhone-\(Int(Date().timeIntervalSince1970))-\(UUID().uuidString.prefix(8)).jsonl")
        FileManager.default.createFile(atPath: url.path, contents: nil)
        file = try FileHandle(forWritingTo: url)
        let watchdog = DispatchSource.makeTimerSource(queue: queue)
        watchdog.schedule(deadline: .now() + 1, repeating: 1)
        watchdog.setEventHandler { [weak self] in
            guard let self else { return }
            self.lock.lock(); let last = self.heartbeat; self.lock.unlock()
            let lag = ProcessInfo.processInfo.systemUptime - last
            if lag >= 1 { self.append(["event": "main_thread_unresponsive", "lagMs": lag * 1000, "uptime": ProcessInfo.processInfo.systemUptime]) }
        }
        timer = watchdog; watchdog.resume()
    }
    func touch() { lock.lock(); heartbeat = ProcessInfo.processInfo.systemUptime; lock.unlock() }
    func record(_ row: [String: Any]) {
        guard JSONSerialization.isValidJSONObject(row), let data = try? JSONSerialization.data(withJSONObject: row, options: [.sortedKeys]) else { return }
        queue.async { [weak self] in self?.append(data) }
    }
    private func append(_ row: [String: Any]) {
        if let data = try? JSONSerialization.data(withJSONObject: row, options: [.sortedKeys]) { append(data) }
    }
    private func append(_ data: Data) {
        guard !closed, bytes + data.count < 8 * 1024 * 1024 else { return }
        do { try file?.write(contentsOf: data); try file?.write(contentsOf: Data([10])); try file?.synchronize(); bytes += data.count + 1 }
        catch { closed = true }
    }
    func finish() {
        queue.async { [self] in timer?.cancel(); timer = nil; try? file?.synchronize(); try? file?.close(); file = nil; closed = true }
    }
}

@MainActor final class IPhonePerformanceTest: NSObject, ObservableObject {
    static let shared = IPhonePerformanceTest()
    @Published private(set) var running = false
    @Published private(set) var elapsed = 0
    @Published private(set) var phase = ""
    @Published private(set) var summary = ""
    @Published private(set) var report: URL?
    private weak var model: AureaModel?
    private var journal: PerformanceJournal?
    private var link: CADisplayLink?
    private var started = 0.0, lastTick = 0.0, lastSample = 0.0, lastAction = 0.0
    private var intervalMax = 0.0, worstGap = 0.0, slowUI = 0
    private var lastStage = -1, samples = 0, slowRender = 0
    private var maxRAM = 0.0, maxCPU = 0.0, maxGPU = 0.0, maxDecode = 0.0
    private var initialDrops: Double?, finalDrops = 0.0
    private var stress = false
    private var originalFrame: Int64 = 0
    private var originalNum: UInt32 = 1, originalDen: UInt32 = 1
    private var originalAuto = true, originallyPlaying = false
    private var project: URL?
    override init() {
        super.init()
        let folder = AureaPaths.documents.appendingPathComponent("PerformanceReports")
        report = (try? FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil))?
            .filter { $0.pathExtension == "jsonl" }.sorted { $0.lastPathComponent > $1.lastPathComponent }.first
        NotificationCenter.default.addObserver(self, selector: #selector(backgrounded), name: UIApplication.didEnterBackgroundNotification, object: nil)
        NotificationCenter.default.addObserver(self, selector: #selector(memoryWarning), name: UIApplication.didReceiveMemoryWarningNotification, object: nil)
    }
    func start(_ model: AureaModel, stress: Bool) {
        guard !running, model.screen == .editor, !model.importingMedia else { return }
        do { journal = try PerformanceJournal() } catch { summary = "Não foi possível gravar o relatório: \(error.localizedDescription)"; return }
        self.model = model; self.stress = stress; project = model.projectURL
        originalFrame = Int64(model.status.playhead); originalNum = model.status.previewNumerator; originalDen = model.status.previewDenominator
        originalAuto = model.status.previewAuto != 0; originallyPlaying = model.status.playing != 0
        started = ProcessInfo.processInfo.systemUptime; lastTick = 0; lastSample = started; lastAction = started
        intervalMax = 0; worstGap = 0; slowUI = 0; slowRender = 0; samples = 0; elapsed = 0; lastStage = -1
        maxRAM = 0; maxCPU = 0; maxGPU = 0; maxDecode = 0; initialDrops = nil; finalDrops = 0
        report = journal?.url; summary = "Gravando. Você pode parar a qualquer momento."
        phase = stress ? "Reprodução automática" : "Edição e importação manual"
        running = true
        var device = utsname(); uname(&device)
        let machine = withUnsafeBytes(of: &device.machine) { bytes in String(decoding: bytes.prefix { $0 != 0 }, as: UTF8.self) }
        journal?.record(["event":"session_start", "schema":1, "uptime":started, "date":ISO8601DateFormatter().string(from:Date()),
            "build":Bundle.main.object(forInfoDictionaryKey:"CFBundleVersion") as? String ?? "?", "device":machine,
            "iOS":UIDevice.current.systemVersion, "physicalMemory":ProcessInfo.processInfo.physicalMemory,
            "processors":ProcessInfo.processInfo.activeProcessorCount, "lowPower":ProcessInfo.processInfo.isLowPowerModeEnabled,
            "mode":stress ? "stress_180_seconds" : "manual_300_seconds", "sampleIntervalSeconds":1,
            "metricNotes":"GPU times are unavailable when gpuTimers=0; ramBytes is engine accounting, processFootprintBytes is OS footprint. UI gaps are CADisplayLink intervals, not video frame times. No media, filenames or project titles included."])
        let next = CADisplayLink(target:self, selector:#selector(tick(_:))); next.add(to:.main, forMode:.common); link = next
    }
    func event(_ name: String, values: [String: Any] = [:]) {
        guard running else { return }
        var row = values; row["event"] = name; row["elapsedSeconds"] = ProcessInfo.processInfo.systemUptime - started
        journal?.record(row)
    }
    @objc private func backgrounded() { stop("App foi para segundo plano") }
    @objc private func memoryWarning() { event("memory_warning"); stop("Interrompido por pressão de memória") }
    @objc private func tick(_ display: CADisplayLink) {
        guard running, let model else { return }
        let now = ProcessInfo.processInfo.systemUptime
        journal?.touch()
        if lastTick > 0 {
            let gap = (now - lastTick)*1000; intervalMax = max(intervalMax,gap); worstGap = max(worstGap,gap)
            if gap >= 100 { slowUI += 1; event("ui_gap", values:["milliseconds":gap,"phase":phase,"frame":model.status.playhead]) }
        }
        lastTick = now
        if model.screen != .editor || model.projectURL != project { stop("Projeto ou tela mudou"); return }
        if ProcessInfo.processInfo.thermalState == .critical { stop("Interrompido: estado térmico crítico"); return }
        if now - started >= (stress ? 180 : 300) { stop("Concluído"); return }
        if stress && !model.importingMedia && !model.showExport {
            let stage = min(2,Int((now-started)/60))
            if stage != lastStage {
                lastStage = stage
                phase = ["Reprodução · qualidade automática","Reprodução · qualidade total","Saltos na timeline · qualidade total"][stage]
                event("phase",values:["name":phase])
                model.setPreviewScale(num:1,den:1,auto:stage == 0)
                if stage < 2 && model.status.playing == 0 { model.playPause() }
                if stage == 2 && model.status.playing != 0 { model.playPause() }
            }
            if stage < 2 && model.status.playing == 0 { model.seek(toFrame:0); model.playPause() }
            if stage == 2 && now-lastAction >= 0.25 {
                lastAction = now
                let n = Int64((now-started)*4), duration = max(Int64(1),Int64(model.compositionDuration)-1)
                model.seek(toFrame:(n * 7919) % duration)
            }
        }
        if now-lastSample >= 1 {
            lastSample = now; elapsed = Int(now-started)
            // A small read-only snapshot; no scene capture, encoding or full
            // layer serialization on the measured thread.
            let p = model.engine.perf()
            func num(_ key:String)->Double { (p[key] as? NSNumber)?.doubleValue ?? 0 }
            let cpu = num("cpuFrameMs"), gpu = num("gpuFrameMs"), budget = num("frameBudgetMs")
            samples += 1; if budget > 0 && (cpu > budget || (num("gpuTimers") > 0 && gpu > budget)) { slowRender += 1 }
            maxCPU = max(maxCPU,cpu); maxGPU = max(maxGPU,gpu); maxDecode = max(maxDecode,num("decodeMs")); maxRAM = max(maxRAM,num("processFootprintBytes"))
            if initialDrops == nil { initialDrops = num("droppedFrames") }; finalDrops = num("droppedFrames")
            journal?.record(["event":"sample","elapsedSeconds":now-started,"phase":phase,"frame":model.status.playhead,
                "playing":model.status.playing != 0,"importing":model.importingMedia,"exporting":model.showExport,
                "width":model.compositionWidth,"height":model.compositionHeight,"fps":model.compositionFps,"layers":model.layers.count,
                "uiMaxGapMs":intervalMax,"thermalState":ProcessInfo.processInfo.thermalState.rawValue,
                "lowPower":ProcessInfo.processInfo.isLowPowerModeEnabled,"perf":p])
            intervalMax = 0
        }
    }
    func stop(_ reason: String = "Parado pelo usuário") {
        guard running else { return }
        event("session_end",values:["reason":reason,"samples":samples,"overBudgetSamples":slowRender,"uiGapsOver100ms":slowUI,
            "worstUIGapMs":worstGap,"droppedFramesDelta":max(0,finalDrops-(initialDrops ?? finalDrops)),
            "maxProcessFootprintBytes":maxRAM,"maxCPUms":maxCPU,"maxGPUms":maxGPU,"maxDecodeMs":maxDecode])
        running = false; link?.invalidate(); link = nil; journal?.finish(); journal = nil
        if stress, let model, model.projectURL == project, model.screen == .editor {
            if model.status.playing != 0 { model.playPause() }
            model.setPreviewScale(num:originalNum,den:originalDen,auto:originalAuto); model.seek(toFrame:originalFrame)
            if originallyPlaying && reason == "Concluído" { model.playPause() }
        }
        summary = "\(reason). \(samples) amostras; \(slowRender) acima do orçamento de render; \(slowUI) intervalos da interface acima de 100 ms. Maior intervalo: \(Int(worstGap)) ms."
    }
}

struct PerformanceTestPanel: View {
    @EnvironmentObject private var model: AureaModel
    @Environment(\.dismiss) private var dismiss
    @ObservedObject private var test = IPhonePerformanceTest.shared
    @State private var pickingVideo = false
    var body: some View {
        NavigationStack {
            Form {
                Section("Teste no seu iPhone") {
                    Text("Abra um projeto com vídeo. O relatório mede prévia, CPU/GPU, decodificação, memória, áudio e pausas da interface. Nenhum vídeo é enviado para um servidor.")
                    if test.running {
                        Text("\(test.phase) · \(test.elapsed)s")
                        Button("Marcar: travou aqui") { test.event("user_reported_stall", values:["frame":model.status.playhead]) }
                        Button("Parar e salvar relatório", role:.destructive) { test.stop() }
                    } else {
                        Button("Gravar enquanto eu edito · até 5 min") { test.start(model,stress:false); dismiss() }.disabled(model.screen != .editor)
                        Button("Forçar prévia e timeline · 3 min") { test.start(model,stress:true); dismiss() }.disabled(model.screen != .editor || model.importingMedia)
                        Text("O teste automático reproduz em qualidade automática, depois total, e faz saltos pela timeline. Ao terminar, restaura a qualidade e a posição. Para se houver pressão de memória ou calor crítico.").font(.footnote)
                        Button("Importar vídeo e medir") { pickingVideo = true }.disabled(model.screen != .editor || model.importingMedia)
                    }
                }
                Section("Relatório") {
                    if !test.summary.isEmpty { Text(test.summary) }
                    if let report = test.report, !test.running {
                        ShareLink(item:report) { Label("Compartilhar último relatório",systemImage:"square.and.arrow.up") }
                        Text("Se o app fechar durante o teste, abra esta tela novamente. O arquivo mantém as amostras já gravadas; pode estar incompleto. Envie o arquivo .jsonl nesta conversa.").font(.footnote)
                    }
                }
            }
            .navigationTitle("Desempenho no iPhone")
            .toolbar { ToolbarItem(placement:.confirmationAction) { Button("Fechar") { dismiss() } } }
            .fileImporter(isPresented:$pickingVideo,allowedContentTypes:[.movie,.video]) { result in
                if case .success(let url) = result { test.start(model,stress:false); model.importMedia(url:url,kind:.video); dismiss() }
            }
        }
    }
}

struct PerformanceTestBadge: View {
    @ObservedObject private var test = IPhonePerformanceTest.shared
    @State private var details = false
    var body: some View {
        if test.running {
            HStack {
                Button("Teste · \(test.elapsed)s") { details = true }
                Button("Travou aqui") { test.event("user_reported_stall") }
                Button("Parar") { test.stop() }
            }.font(.caption.bold()).padding(10).background(.ultraThinMaterial,in:Capsule())
                .sheet(isPresented:$details) { PerformanceTestPanel() }
        }
    }
}
