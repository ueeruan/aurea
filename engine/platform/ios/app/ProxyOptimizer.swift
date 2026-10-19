import Foundation
import SwiftUI

/// "Otimizar vídeos pesados" (0.2.5) — par do `state/ProxyState.kt` (Android).
///
/// O motor decide o que otimizar (o custo de decodificar a fonte: 4K, acima de
/// 60 fps, HEVC/10 bits, bitrate alto — em qualquer aparelho), gera a versão
/// leve em segundo plano (uma por vez, parada durante o play) e troca a fonte
/// do preview sozinho quando ela fica pronta. Aqui mora só: o modo escolhido
/// (gravado no aparelho e mandado ao motor na abertura), o resumo da fila para
/// o selo "Otimizando 42%" e o tamanho/limpeza do cache.
///
/// Trocar o modo e medir/limpar o cache saem da main thread: o motor trava o
/// modelo para o modo e a medição percorre a pasta.
final class ProxyOptimizer: ObservableObject {
    static let shared = ProxyOptimizer()

    /// Os mesmos números do `ProxyMode` do C++ e do Android.
    enum Mode: UInt32, CaseIterable, Identifiable {
        case automatic = 0, always = 1, off = 2
        var id: UInt32 { rawValue }
        var labelKey: String {
            switch self {
            case .automatic: return "proxy_mode_auto"
            case .always: return "proxy_mode_always"
            case .off: return "proxy_mode_off"
            }
        }
    }

    /// Os pedidos desta sessão de projeto (nativeProxySummary no Android).
    struct Summary: Equatable {
        var queued = 0, generating = 0, ready = 0, failed = 0
        /// Do vídeo sendo otimizado agora, 0...1.
        var progress = 0.0
        var busy: Bool { queued > 0 || generating > 0 }
        /// "42" — o número do selo "Otimizando 42%".
        var percent: Int { min(99, max(0, Int(progress * 100))) }
    }

    @Published private(set) var mode: Mode
    @Published private(set) var summary = Summary()
    /// Bytes da mídia otimizada em disco; -1 = ainda não medido.
    @Published private(set) var cacheBytes: Int64 = -1
    /// Um vídeo acabou de ficar pronto (o AureaModel mostra o aviso).
    var onOptimized: (() -> Void)?
    /// Um vídeo não pôde ser otimizado: a prévia segue no original.
    var onFailed: (() -> Void)?

    private weak var engine: AureaEngine?
    private var timer: Timer?
    private var lastReady = -1
    private var lastFailed = -1
    private var wasBusy = false
    private var wasGenerating = false
    private let work = DispatchQueue(label: "aurea.proxy", qos: .utility)
    private static let modeKey = "proxy.mode"

    private init() {
        mode = Mode(rawValue: UInt32(max(0, UserDefaults.standard.integer(forKey: Self.modeKey)))) ?? .automatic
    }

    /// Motor no ar: o modo gravado vale antes do primeiro projeto abrir.
    func attach(_ engine: AureaEngine) {
        self.engine = engine
        engine.setProxyMode(mode.rawValue)
        lastReady = -1; lastFailed = -1; wasBusy = false; wasGenerating = false
        refreshCacheSize()
        poll()
    }

    /// Antes de o motor parar: nada novo entra na fila de trabalho.
    func detach() {
        timer?.invalidate(); timer = nil
        engine = nil
    }

    /// Espera o trabalho em curso (medir/limpar) antes de destruir o motor.
    func drain() { work.sync {} }

    func select(_ newMode: Mode) {
        guard newMode != mode else { return }
        mode = newMode
        UserDefaults.standard.set(Int(newMode.rawValue), forKey: Self.modeKey)
        guard let engine else { return }
        work.async { engine.setProxyMode(newMode.rawValue) }
    }

    func refreshCacheSize() {
        guard let engine else { return }
        work.async { [weak self] in
            let bytes = Int64(bitPattern: engine.proxyCacheBytes())
            DispatchQueue.main.async { self?.cacheBytes = bytes }
        }
    }

    /// Apaga a mídia otimizada; `done` recebe o texto "12,3" (MB liberados).
    func clearCache(_ done: @escaping (String) -> Void) {
        guard let engine else { return }
        work.async { [weak self] in
            let freed = engine.clearProxyCache()
            let left = Int64(bitPattern: engine.proxyCacheBytes())
            DispatchQueue.main.async {
                self?.cacheBytes = left
                done(Self.megabytes(freed))
            }
        }
    }

    /// MB com uma casa, no formato do idioma do aparelho.
    static func megabytes<T: BinaryInteger>(_ bytes: T) -> String {
        String(format: "%.1f", locale: Locale.current, Double(max(0, Int64(bytes))) / (1024 * 1024))
    }

    /// O resumo só trava a fila do proxy (não o modelo): barato na main thread.
    private func poll() {
        timer?.invalidate(); timer = nil
        guard let engine else { return }
        let d = engine.proxySummary()
        let s = Summary(queued: d["queued"]?.intValue ?? 0, generating: d["generating"]?.intValue ?? 0,
                        ready: d["ready"]?.intValue ?? 0, failed: d["failed"]?.intValue ?? 0,
                        progress: min(1, max(0, d["progress"]?.doubleValue ?? 0)))
        if s != summary { summary = s }
        // Um vídeo GERADO ficou pronto (o achado no disco ao reabrir não avisa):
        // a edição já usa a versão leve e o tamanho em disco mudou.
        if lastReady >= 0 && s.ready > lastReady && wasGenerating { onOptimized?() }
        if lastFailed >= 0 && s.failed > lastFailed { onFailed?() }
        if wasBusy && !s.busy { refreshCacheSize() }
        lastReady = s.ready; lastFailed = s.failed; wasBusy = s.busy; wasGenerating = s.generating > 0
        timer = Timer.scheduledTimer(withTimeInterval: s.busy ? 0.7 : 2.0, repeats: false) { [weak self] _ in
            self?.poll()
        }
    }
}

/// O fio de progresso no chip de resolução da prévia (par do
/// `PreviewResolutionButton` do Android): embaixo do rótulo, sem texto novo na
/// barra. Só este pedaço redesenha a cada leitura da fila.
struct ProxyChipBar: ViewModifier {
    @ObservedObject var optimizer: ProxyOptimizer = .shared

    func body(content: Content) -> some View {
        content.overlay(alignment: .bottomLeading) {
            if optimizer.summary.generating > 0 {
                GeometryReader { bounds in
                    Rectangle().fill(AureaColors.accent)
                        .frame(width: bounds.size.width * CGFloat(max(0.05, min(1, optimizer.summary.progress))), height: 2)
                        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottomLeading)
                }
                .clipShape(RoundedRectangle(cornerRadius: 4))
                .allowsHitTesting(false)
                .accessibilityHidden(true)
            }
        }
    }
}

/// O estado falado do chip: "Otimizando vídeo pesado para a edição: 42%".
struct ProxyChipAccessibility: ViewModifier {
    @ObservedObject var optimizer: ProxyOptimizer = .shared

    func body(content: Content) -> some View {
        let s = optimizer.summary
        let state = s.generating > 0 ? AureaText.t("proxy_optimizing_description", s.percent)
            : s.busy ? AureaText.t("proxy_queued") : ""
        return content.accessibilityValue(state)
    }
}

/// Linha informativa no topo do menu da prévia: "Otimizando 42%".
struct ProxyMenuStatus: View {
    @ObservedObject var optimizer: ProxyOptimizer = .shared

    var body: some View {
        if optimizer.summary.generating > 0 {
            HStack(spacing: 10) {
                Rectangle().fill(Color.clear).frame(width: 4, height: 24)
                Text(AureaText.t("proxy_optimizing", optimizer.summary.percent)).font(.aurea(size: 13))
                    .foregroundStyle(AureaColors.muted).frame(maxWidth: .infinity, alignment: .leading)
            }.padding(.trailing, 10).frame(height: 40)
        }
    }
}
