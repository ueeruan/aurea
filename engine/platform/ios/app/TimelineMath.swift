// =============================================================================
//  Aurea / platform / ios / app / TimelineMath.swift
//
//  PORTE de `android/app/src/main/java/com/aurea/aurea/editor/timeline/
//  TimelineMath.kt` — a lógica PURA da timeline: tempo ↔ px, zoom, passos da
//  régua, relógio, ímã, auto-rolagem, keyframes, miniaturas, waveform e
//  reordenar. Nada aqui desenha nem trata gesto.
//
//  Tempo em FRAMES da composição (a unidade do motor); a vista em Double para o
//  arrasto e a pinça não andarem de quadro em quadro.
// =============================================================================
import CoreGraphics
import Foundation

/// Arredonda ao quadro mais perto (a grade do motor), sem estourar o Int32.
@inline(__always) func timelineFrame(_ value: Double) -> Int32 {
    guard value.isFinite else { return 0 }
    let r = value.rounded()
    return Int32(max(-2147483648.0, min(2147483647.0, r)))
}

/// Primeiro índice com `a[i] >= v` (o insertion point do `Arrays.binarySearch`).
@inline(__always) func lowerBound(_ a: [Int32], _ v: Int32) -> Int {
    var lo = 0
    var hi = a.count
    while lo < hi {
        let mid = (lo + hi) / 2
        if a[mid] < v { lo = mid + 1 } else { hi = mid }
    }
    return lo
}

/**
 * Tempo ↔ px com o cabeçote FIXO no centro da timeline inteira: quem anda é o
 * conteúdo. `view` é o frame sob o cabeçote.
 */
enum TimeAxis {
    static func safeFps(_ fps: Float) -> Float { fps > 0 ? fps : 30 }

    /// px por frame para um zoom em dp/s.
    static func pxPerFrame(pps: CGFloat, density: CGFloat, fps: Float) -> CGFloat {
        pps * density / CGFloat(safeFps(fps))
    }

    static func xOf(frame: Double, view: Double, pxPerFrame: CGFloat, centerX: CGFloat) -> CGFloat {
        centerX + CGFloat(frame - view) * pxPerFrame
    }

    static func frameAt(x: CGFloat, view: Double, pxPerFrame: CGFloat, centerX: CGFloat) -> Double {
        guard pxPerFrame != 0 else { return view }
        return view + Double((x - centerX) / pxPerFrame)
    }

    /**
     * A vista não vai para antes do zero, mas PODE passar do fim da composição
     * — o motor também deixa o cursor lá. Prender a vista ao último quadro
     * travava a timeline inteira no fim do projeto. O teto fica em
     * `durationFrames` para não rolar para o vazio sem fim.
     */
    static func clampView(_ view: Double, durationFrames: Int32) -> Double {
        min(max(view, 0), Double(max(0, durationFrames)))
    }
}

/// Zoom em dp por segundo.
enum Zoom {
    static let minPPS: CGFloat = 2
    static let maxPPS: CGFloat = 800
    /// A.01: 80 dp/s (10 riscos por segundo a 8 dp).
    static let defaultPPS: CGFloat = 80
    /// A.01 enquadra projetos longos (≥ 20 s) na largura ao abrir.
    static let autoFitMinSeconds: CGFloat = 20

    static func clamp(_ pps: CGFloat) -> CGFloat { min(max(pps, minPPS), maxPPS) }

    /// `clamp((largura − 32) / segundos, 4, 80)` da A.01.
    static func autoFit(availableDp: CGFloat, seconds: CGFloat) -> CGFloat {
        seconds <= 0 ? defaultPPS : min(max(availableDp / seconds, 4), defaultPPS)
    }

    /// Pinça: a vista que mantém `focusFrame` sob o ponto focal `focusX`.
    static func anchoredView(focusFrame: Double, focusX: CGFloat, centerX: CGFloat, pxPerFrame: CGFloat) -> Double {
        guard pxPerFrame != 0 else { return focusFrame }
        return focusFrame - Double((focusX - centerX) / pxPerFrame)
    }
}

/**
 * Passos da régua. No zoom da A.01 (80 dp/s) dá exatamente o print: risco
 * forte por segundo e 10 finos. Fora dele os passos se adaptam para os riscos
 * nunca virarem borrão; só quando o forte vale mais de 1 s aparece rótulo.
 */
struct RulerSteps {
    let majorSeconds: Double
    /// Subdivisões entre dois fortes (0 = sem riscos finos). Ignorado se `frameMinors`.
    let subdivisions: Int
    /// Riscos finos em cada quadro (zoom muito aberto).
    let frameMinors: Bool
    let labels: Bool

    static let minMajorDp: CGFloat = 40
    static let minMinorDp: CGFloat = 4
    static let minFrameDp: CGFloat = 12

    private static let majors: [Double] = [1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600]
    private static let subs: [Int] = [10, 4, 5, 10, 3, 6, 6, 4, 5, 10, 6, 6]
    private static let oneSecondSubs: [Int] = [10, 5, 2]

    static func of(pps: CGFloat, fps: Float) -> RulerSteps {
        var i = majors.firstIndex { $0 * Double(pps) >= Double(minMajorDp) } ?? (majors.count - 1)
        if i < 0 { i = majors.count - 1 }
        let major = majors[i]
        if i == 0 {
            if pps / CGFloat(TimeAxis.safeFps(fps)) >= minFrameDp {
                return RulerSteps(majorSeconds: major, subdivisions: 0, frameMinors: true, labels: false)
            }
            for n in oneSecondSubs where pps / CGFloat(n) >= minMinorDp {
                return RulerSteps(majorSeconds: major, subdivisions: n, frameMinors: false, labels: false)
            }
            return RulerSteps(majorSeconds: major, subdivisions: 0, frameMinors: false, labels: false)
        }
        let n = subs[i]
        let subs = major * Double(pps) / Double(n) >= Double(minMinorDp) ? n : 0
        return RulerSteps(majorSeconds: major, subdivisions: subs, frameMinors: false, labels: true)
    }
}

/// Relógio `MM:SS:FF` da A.01; a partir de 1 h ganha a hora (`H:MM:SS:FF`).
enum Timecode {
    /// `[horas, minutos, segundos, quadros]`. Sem alocação de String.
    static func split(_ frame: Int32, _ fps: Float) -> (hours: Int32, minutes: Int32, seconds: Int32, frames: Int32) {
        let f = Double(TimeAxis.safeFps(fps))
        let fr = Double(max(0, frame))
        let totalSeconds = Int64((fr / f + 1e-9).rounded(.down))
        // Quadro dentro do segundo: conta a partir do 1º quadro que cai NAQUELE
        // segundo (com 29,97 fps o segundo 1 começa no quadro 30, não em 29,97).
        let firstOfSecond = Int64((Double(totalSeconds) * f - 1e-6).rounded(.up))
        let hours = Int32(totalSeconds / 3600)
        let minutes = Int32((totalSeconds / 60) % 60)
        let seconds = Int32(totalSeconds % 60)
        let frames = Int32(max(0, min(Int64(Int32.max), Int64(fr) - firstOfSecond)))
        return (hours, minutes, seconds, frames)
    }

    static func format(_ frame: Int32, _ fps: Float) -> String {
        let p = split(frame, fps)
        let base = String(format: fps > 100 ? "%02d:%02d:%03d" : "%02d:%02d:%02d", p.minutes, p.seconds, p.frames)
        return p.hours > 0 ? "\(p.hours):\(base)" : base
    }

    /// Rótulo da régua: `m:ss` (ou `h:mm:ss`).
    static func rulerLabel(_ seconds: Int32) -> String {
        let h = seconds / 3600
        let m = (seconds / 60) % 60
        let s = seconds % 60
        return h > 0 ? String(format: "%d:%02d:%02d", h, m, s) : String(format: "%d:%02d", m, s)
    }
}

/**
 * Ímã. Os alvos são reunidos UMA vez no começo do gesto (ordenados, busca
 * binária); o cabeçote disputa junto a cada passo porque a vista pode andar
 * (auto-rolagem).
 */
enum Snap {
    static let none: Int32 = Int32.min

    /// Alvo mais perto de `value` a no máximo `tol` frames (`extra` = cabeçote; none = sem).
    static func nearest(_ targets: [Int32], _ value: Double, extra: Int32, tol: Double) -> Int32 {
        var best = none
        var bestD = Double.greatestFiniteMagnitude
        if extra != none {
            let d = abs(Double(extra) - value)
            if d <= tol { bestD = d; best = extra }
        }
        if !targets.isEmpty {
            let i = lowerBound(targets, timelineFrame(value.rounded(.down)))
            for k in (i - 1)...(i + 1) {
                guard k >= 0 && k < targets.count else { continue }
                let d = abs(Double(targets[k]) - value)
                if d <= tol && d < bestD { bestD = d; best = targets[k] }
            }
        }
        return best
    }

    /**
     * Encaixa um intervalo pelo INÍCIO ou pelo FIM, o que estiver mais perto.
     * Devolve o novo início e a guia (frame encaixado ou `none`).
     */
    static func span(_ targets: [Int32], start: Int32, length: Int32, extra: Int32, tol: Double) -> (start: Int32, guide: Int32) {
        let a = nearest(targets, Double(start), extra: extra, tol: tol)
        let end = Int64(start) + Int64(length)
        let b = nearest(targets, Double(end), extra: extra, tol: tol)
        let da = a == none ? Double.greatestFiniteMagnitude : abs(Double(Int64(a) - Int64(start)))
        let db = b == none ? Double.greatestFiniteMagnitude : abs(Double(Int64(b) - end))
        if a != none && da <= db { return (a, a) }
        if b != none { return (Int32(clamping: Int64(b) - Int64(length)), b) }
        return (start, none)
    }

    /// Ordena e tira repetidos.
    static func sortedDistinct(_ values: [Int32]) -> [Int32] {
        guard !values.isEmpty else { return [] }
        let a = values.sorted()
        var out: [Int32] = []
        out.reserveCapacity(a.count)
        out.append(a[0])
        for i in 1..<a.count where a[i] != out[out.count - 1] { out.append(a[i]) }
        return out
    }
}

/**
 * Auto-rolagem na borda durante arrastos: só rola para o lado a que o dedo
 * FOI desde o começo do gesto (pegar um clipe já perto da borda não sai rolando).
 */
enum AutoScroll {
    static func direction(pos: CGFloat, from: CGFloat, low: CGFloat, high: CGFloat, intent: CGFloat) -> Int {
        if pos < low && pos < from - intent { return -1 }
        if pos > high && pos > from + intent { return 1 }
        return 0
    }
}

/// Instantes de keyframe de uma camada, em frames da timeline.
enum Keyframes {
    /// Tempo local → frame da timeline (`t + start − offset`).
    static func toTimeline(_ local: Int32, _ start: Int32, _ offset: Int32) -> Int32 { Int32(clamping: Int64(local) + Int64(start) - Int64(offset)) }
    static func toLocal(_ timeline: Int32, _ start: Int32, _ offset: Int32) -> Int32 { Int32(clamping: Int64(timeline) - Int64(start) + Int64(offset)) }

    /**
     * Limites de arrasto do instante `index` (inclusive): nunca encosta no
     * vizinho (senão duas marcas da mesma trilha se fundem) e não sai da camada
     * — a não ser que já estivesse fora (não pula para dentro sozinho).
     */
    static func dragLimits(_ instants: [Int32], _ index: Int, start: Int32, end: Int32) -> (lo: Int32, hi: Int32) {
        guard index >= 0 && index < instants.count else { return (start, end) }
        let t = instants[index]
        var lo = min(start, t)
        var hi = max(end, t)
        if index > 0 { lo = max(lo, instants[index - 1] + 1) }
        if index < instants.count - 1 { hi = min(hi, instants[index + 1] - 1) }
        return (lo, max(lo, hi))
    }

    /// Índice do instante mais perto de `frame` (a lista está ordenada); −1 se vazia.
    static func nearestIndex(_ instants: [Int32], _ frame: Double) -> Int {
        guard !instants.isEmpty else { return -1 }
        let i = lowerBound(instants, timelineFrame(frame.rounded(.down)))
        var best = -1
        var bestD = Double.greatestFiniteMagnitude
        for k in (i - 1)...(i + 1) {
            guard k >= 0 && k < instants.count else { continue }
            let d = abs(Double(instants[k]) - frame)
            if d < bestD { bestD = d; best = k }
        }
        return best
    }

    /// Primeiro índice com instante ≥ `frame`.
    static func firstAtOrAfter(_ instants: [Int32], _ frame: Double) -> Int {
        lowerBound(instants, timelineFrame(frame.rounded(.up)))
    }

    /**
     * Lista de desenho dos losangos de UMA linha: só os instantes na tela
     * (busca binária na borda esquerda, para na direita) e vizinhos a menos de
     * `mergeGap` px viram um grupo (a pílula). Escreve pares [primeiro, último]
     * em `out` e devolve quantos grupos; `out` precisa de 2 × (instantes na tela).
     */
    static func visibleGroups(
        _ instants: [Int32], view: Double, pxPerFrame: CGFloat, centerX: CGFloat,
        width: CGFloat, margin: CGFloat, mergeGap: CGFloat, out: inout [Int32]
    ) -> Int {
        guard !instants.isEmpty else { return 0 }
        var i = firstAtOrAfter(instants, TimeAxis.frameAt(x: -margin, view: view, pxPerFrame: pxPerFrame, centerX: centerX))
        var n = 0
        while i < instants.count {
            let kx = TimeAxis.xOf(frame: Double(instants[i]), view: view, pxPerFrame: pxPerFrame, centerX: centerX)
            if kx > width + margin { break }
            var j = i
            var lastX = kx
            while j + 1 < instants.count && lastX <= width + margin {
                let nx = TimeAxis.xOf(frame: Double(instants[j + 1]), view: view, pxPerFrame: pxPerFrame, centerX: centerX)
                if nx - lastX >= mergeGap { break }
                // Denso (zoom aberto, milhares de marcas): tudo antes de lastX +
                // mergeGap entra no grupo de uma vez (busca binária).
                let k = firstAtOrAfter(instants, TimeAxis.frameAt(x: lastX + mergeGap, view: view, pxPerFrame: pxPerFrame, centerX: centerX))
                j = max(j + 1, min(k, instants.count) - 1)
                lastX = TimeAxis.xOf(frame: Double(instants[j]), view: view, pxPerFrame: pxPerFrame, centerX: centerX)
            }
            if 2 * n + 1 >= out.count { return n }
            out[2 * n] = Int32(i)
            out[2 * n + 1] = Int32(j)
            n += 1
            i = j + 1
        }
        return n
    }

    /// Algum instante do grupo `[a, b]` é `frame`? (busca binária no grupo)
    static func groupHas(_ instants: [Int32], _ a: Int, _ b: Int, _ frame: Int32) -> Bool {
        guard frame != Snap.none, a >= 0, b >= a, b < instants.count else { return false }
        let i = lowerBound(instants, frame)
        return i <= b && instants[i] == frame
    }
}

/**
 * Miniaturas: o motor as guarda em baldes de 250 ms do tempo da MÍDIA. A
 * timeline pede um frame por balde (o mesmo sempre), então mexer o clipe não
 * troca a chave e a tira não pisca.
 */
enum Thumbs {
    static let bucketSeconds = 0.25

    static func bucketOf(_ localFrame: Double, _ fps: Float) -> Int32 {
        Int32((max(0, localFrame) / Double(TimeAxis.safeFps(fps)) / bucketSeconds + 1e-9).rounded(.down))
    }

    /// Frame local que o motor põe no balde `bucket` (o 1º quadro dele).
    static func requestLocalFrame(_ bucket: Int32, _ fps: Float) -> Int32 {
        Int32((Double(bucket) * bucketSeconds * Double(TimeAxis.safeFps(fps)) - 1e-6).rounded(.up))
    }
}

/**
 * Waveform em grade FIXA do tempo: o balde `k` cobre os frames
 * `[k·fpb, (k+1)·fpb)` — não depende de onde está a vista, então rolar ou
 * tocar não faz a forma "tremer". O tamanho do balde anda em degraus de √2.
 */
enum WaveGrid {
    static func framesPerBucket(targetPx: CGFloat, pxPerFrame: CGFloat) -> Double {
        guard pxPerFrame > 0, targetPx > 0 else { return 1 }
        let exact = Double(targetPx / pxPerFrame)
        let step = (log2(exact) * 2).rounded() / 2
        return pow(2, step)
    }

    static func bucketAt(_ frame: Double, _ fpb: Double) -> Int64 {
        guard fpb > 0 else { return 0 }
        return Int64((frame / fpb).rounded(.down))
    }

    /**
     * Janela a pedir para ver `[first, last]`: a vista com uma tela de folga
     * de cada lado, no máximo `max` baldes. Devolve (início, fim exclusivo).
     */
    static func window(first: Int64, last: Int64, max cap: Int) -> (Int64, Int64) {
        let visible = last - first + 1
        let pad = min(max((Int64(cap) - visible) / 2, 0), visible)
        let w0 = first - pad
        return (w0, min(last + 1 + pad, w0 + Int64(cap)))
    }
}

/// Reordenar: linha de destino sob o dedo (0 = topo = camada da frente).
enum Reorder {
    static func targetIndex(y: CGFloat, rowsTop: CGFloat, scroll: CGFloat, rowHeight: CGFloat, count: Int) -> Int {
        guard count > 0, rowHeight > 0 else { return -1 }
        let i = Int(((y - rowsTop + scroll) / rowHeight).rounded(.down))
        return min(max(i, 0), count - 1)
    }

    /// y do traço de destino: acima do destino quando sobe, abaixo quando desce; NaN se não muda.
    static func dropLineY(source: Int, target: Int, rowsTop: CGFloat, scroll: CGFloat, rowHeight: CGFloat) -> CGFloat {
        if target < 0 || target == source { return .nan }
        return target < source ? rowsTop + CGFloat(target) * rowHeight - scroll
                               : rowsTop + CGFloat(target + 1) * rowHeight - scroll
    }
}
