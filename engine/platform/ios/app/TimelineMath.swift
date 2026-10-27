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
 * Tempo da composição ↔ X absoluto da timeline. `view` é o tempo no centro
 * da janela, inclusive durante scroll/zoom fracionário. O cabeçote também
 * passa por xOf: seu frame inteiro pode não coincidir com `view` durante um gesto.
 */
enum TimeAxis {
    static func safeFps(_ fps: Float) -> Float { fps.isFinite && fps >= 1 && fps <= 1000 ? fps : 30 }

    /// px por frame para um zoom em dp/s.
    static func pxPerFrame(pps: CGFloat, density: CGFloat, fps: Float) -> CGFloat {
        pps * density / CGFloat(safeFps(fps))
    }

    static func xOf(frame: Double, view: Double, pxPerFrame: CGFloat, centerX: CGFloat) -> CGFloat {
        centerX + CGFloat(frame - view) * pxPerFrame
    }

    static func frameAt(x: CGFloat, view: Double, pxPerFrame: CGFloat, centerX: CGFloat) -> Double {
        guard pxPerFrame.isFinite, pxPerFrame > 0, x.isFinite, centerX.isFinite else { return view }
        return view + Double((x - centerX) / pxPerFrame)
    }

    /**
     * A vista não vai para antes do zero, mas PODE passar do fim da composição
     * — o motor também deixa o cursor lá. Prender a vista ao último quadro
     * travava a timeline inteira no fim do projeto. A duração cresce durante
     * a edição; só o limite numérico de frames limita a navegação.
     */
    static func clampView(_ view: Double, durationFrames: Int32) -> Double {
        view.isFinite ? min(max(view, 0), Double(Int32.max - 1)) : 0
    }
}

/// Decoration stays inside the real temporal rectangle even below one point.
/// Touch padding is independent and must never become painted duration.
enum ClipGeometry {
    static func strokeWidth(width: CGFloat, height: CGFloat, requested: CGFloat) -> CGFloat {
        max(0, min(requested, min(width, height)))
    }

    static func cornerRadius(width: CGFloat, height: CGFloat, requested: CGFloat) -> CGFloat {
        max(0, min(requested, min(width, height) / 2))
    }

    /// A fixed external grip must not dwarf a short clip at low zoom.
    static func trimPaintWidth(clipWidth: CGFloat, requested: CGFloat) -> CGFloat {
        max(0, min(requested, clipWidth / 2))
    }

    /// Narrow end grips finish at the beat; padded hit testing stays external.
    static func trimPaintEndInset(clipWidth: CGFloat, requested: CGFloat) -> CGFloat {
        clipWidth < requested * 2 ? trimPaintWidth(clipWidth: clipWidth, requested: requested) : 0
    }
}

/// Real cached frames mapped to the same axis as clips; gaps remain gaps.
enum TimelinePreviewBuffer {
    static let height: CGFloat = 3

    static func span(_ range: Range<Int64>, view: Double, pxPerFrame: CGFloat, width: CGFloat) -> ClosedRange<CGFloat>? {
        guard !range.isEmpty, range.lowerBound >= 0, view.isFinite,
              pxPerFrame.isFinite, pxPerFrame > 0, width.isFinite, width > 0 else { return nil }
        let left = max(0, TimeAxis.xOf(frame: Double(range.lowerBound), view: view, pxPerFrame: pxPerFrame, centerX: width / 2))
        let right = min(width, TimeAxis.xOf(frame: Double(range.upperBound), view: view, pxPerFrame: pxPerFrame, centerX: width / 2))
        return right > left ? left...right : nil
    }
}

/// Zoom em dp por segundo.
enum Zoom {
    static let minPPS: CGFloat = 2
    static let maxPPS: CGFloat = 800
    /// Reference APK: 100 dp/s (ten minor ticks at 10dp intervals).
    static let defaultPPS: CGFloat = 100
    /// A.01 enquadra projetos longos (≥ 20 s) na largura ao abrir.
    static let autoFitMinSeconds: CGFloat = 20

    static func clamp(_ pps: CGFloat) -> CGFloat { pps.isFinite ? min(max(pps, minPPS), maxPPS) : defaultPPS }

    /// Fit long projects without exceeding the initial reference zoom.
    static func autoFit(availableDp: CGFloat, seconds: CGFloat) -> CGFloat {
        !seconds.isFinite || !availableDp.isFinite || seconds <= 0 ? defaultPPS : min(max(availableDp / seconds, 4), defaultPPS)
    }

    /// Pinça: a vista que mantém `focusFrame` sob o ponto focal `focusX`.
    static func anchoredView(focusFrame: Double, focusX: CGFloat, centerX: CGFloat, pxPerFrame: CGFloat) -> Double {
        guard pxPerFrame.isFinite, pxPerFrame > 0, focusX.isFinite, centerX.isFinite else { return focusFrame }
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
        let base = String(format: TimeAxis.safeFps(fps) > 100 ? "%02d:%02d:%03d" : "%02d:%02d:%02d", p.minutes, p.seconds, p.frames)
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
    /// Dt máximo por quadro (s): um quadro atrasado não dá um salto.
    static let maxDt: CGFloat = 0.05

    static func direction(pos: CGFloat, from: CGFloat, low: CGFloat, high: CGFloat, intent: CGFloat) -> Int {
        if pos < low && pos < from - intent { return -1 }
        if pos > high && pos > from + intent { return 1 }
        return 0
    }

    /// Faixa da borda: `maxZone` (48 pt), mas nunca mais que 1/3 da janela.
    static func zone(maxZone: CGFloat, viewport: CGFloat) -> CGFloat { max(0, min(maxZone, viewport / 3)) }

    /// Rolagem em RAMPA (par do Android): fator com sinal em [−1, 1] — 0 fora da
    /// faixa, crescendo com o quanto o dedo entrou nela; só o lado a que o dedo
    /// FOI desde `from` conta.
    static func speed(pos: CGFloat, from: CGFloat, start: CGFloat, end: CGFloat, zone: CGFloat, intent: CGFloat) -> CGFloat {
        guard zone > 0, end > start, pos.isFinite else { return 0 }
        let low = start + zone, high = end - zone
        if pos < low && pos < from - intent { return -min(1, max(0, (low - pos) / zone)) }
        if pos > high && pos > from + intent { return min(1, max(0, (pos - high) / zone)) }
        return 0
    }

    /// Quanto andar neste quadro: fator × velocidade máxima × dt (dt ≤ `maxDt`).
    static func step(factor: CGFloat, maxSpeed: CGFloat, dt: CGFloat) -> CGFloat { factor * maxSpeed * min(maxDt, max(0, dt)) }
}

/// Instantes de keyframe de uma camada, em frames da timeline.
enum Keyframes {
    /// Tempo local → frame da timeline (`t + start − offset`).
    static func toTimeline(_ local: Int32, _ start: Int32, _ offset: Int32) -> Int32 { Int32(clamping: Int64(local) + Int64(start) - Int64(offset)) }
    static func toLocal(_ timeline: Int32, _ start: Int32, _ offset: Int32) -> Int32 { Int32(clamping: Int64(timeline) - Int64(start) + Int64(offset)) }

    /**
     * Limites inclusivos: permite a âncora de animação na borda final.
     * A colisão com um vizinho é recusada pelo motor sem apagar keys.
     */
    static func dragLimits(_ instants: [Int32], _ index: Int, start: Int32, end: Int32) -> (lo: Int32, hi: Int32) {
        guard index >= 0 && index < instants.count else { return (start, end) }
        let t = instants[index]
        // Crossing a neighbor is allowed. The shared engine rejects an occupied
        // destination atomically instead of overwriting it or moving half a group.
        let lo = min(start, t)
        let hi = max(end, t)
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

/// Arrasto VERTICAL de UM trecho (como na referência: só ele anda, nunca a
/// linha inteira) — porte de `RowDrop` (TimelineModel.kt). Diz onde o trecho
/// cai com o dedo em `y` (conteúdo: do topo da 1ª fileira, com a rolagem):
/// ENTRE fileiras (fileira própria ali — o traço de inserção) ou DENTRO de uma
/// linha magnética/compartilhada (faixa do meio da fileira, só se couber no
/// tempo). O motor (`move_layer_to_row`) decide a pilha; aqui sai o pedido:
/// âncora + modo (0 = acima da fileira da âncora, âncora 0 = no fundo; 1 =
/// entrar na linha da âncora).
enum TimelineRowDrop {
    enum Kind: Equatable { case none, insert, join }
    struct Target: Equatable {
        var kind: Kind
        var anchor: Int64
        var mode: Int
        /// y (conteúdo) do traço de inserção; NaN fora de `.insert`.
        var lineY: CGFloat
        /// Fileira acesa ao entrar numa linha; −1 fora de `.join`.
        var row: Int
        static let none = Target(kind: .none, anchor: 0, mode: 0, lineY: .nan, row: -1)
        static func == (a: Target, b: Target) -> Bool {
            a.kind == b.kind && a.anchor == b.anchor && a.mode == b.mode && a.row == b.row
                && (a.lineY == b.lineY || (a.lineY.isNaN && b.lineY.isNaN))
        }
    }

    /// Faixa do meio da fileira que conta como "dentro da linha".
    static let joinEdge: CGFloat = 0.25

    static func target(rows: [TimelineRow], tops: [CGFloat], y: CGFloat, moving: TimelineRow, source: Int) -> Target {
        let n = rows.count
        guard n > 0, tops.count == n + 1, rows.indices.contains(source), y.isFinite else { return .none }
        let keys = timelineGroupKeys(rows)
        var t = 0
        if y >= tops[n] { t = n - 1 }
        else if y >= tops[0] { while t < n - 1 && y >= tops[t + 1] { t += 1 } }
        var gs = t, ge = t + 1
        while gs > 0 && keys[gs - 1] == keys[t] { gs -= 1 }
        while ge < n && keys[ge] == keys[t] { ge += 1 }
        var ss = source, se = source + 1
        while ss > 0 && keys[ss - 1] == keys[source] { ss -= 1 }
        while se < n && keys[se] == keys[source] { se += 1 }
        let line = moving.line
        var sharesLine = false
        if line != 0 {
            for r in rows where r.track == nil && r.line == line {
                if r.segments.contains(where: { $0.id != moving.id }) { sharesLine = true }
            }
        }
        let row = rows[t]
        let h = tops[t + 1] - tops[t]
        let frac = h > 0 ? (y - tops[t]) / h : 0.5
        let middle = frac >= joinEdge && frac <= 1 - joinEdge
        // No meio da própria fileira (ou de outra fileira da mesma linha): fica onde está.
        if middle && (t == source || (line != 0 && row.track == nil && row.line == line)) { return .none }
        if row.track == nil && row.line != 0 && row.line != line && middle {
            var magnetic = false, count = 0, fits = true
            var anchor: Int64 = 0
            for r in rows where r.track == nil && r.line == row.line {
                for s in r.segments where s.id != moving.id {
                    count += 1
                    if anchor == 0 { anchor = s.id }
                    if s.magnetic { magnetic = true }
                    if s.start < moving.end && moving.start < s.end { fits = false }
                }
            }
            if (magnetic || count >= 2) && fits && anchor != 0 {
                return Target(kind: .join, anchor: anchor, mode: 1, lineY: .nan, row: t)
            }
        }
        // ENTRE fileiras: metade de cima do grupo = acima dele; de baixo = abaixo.
        let mid = (tops[gs] + tops[ge]) / 2
        let gap = y < mid ? gs : ge
        if !sharesLine && (gap == ss || gap == se) { return .none }
        var anchor: Int64 = 0
        if gap < n {
            var e = gap + 1
            while e < n && keys[e] == keys[gap] { e += 1 }
            search: for k in gap..<e where rows[k].track == nil {
                for s in rows[k].segments where s.id != moving.id { anchor = s.id; break search }
            }
            if anchor == 0 { return .none }
        }
        return Target(kind: .insert, anchor: anchor, mode: 0, lineY: tops[gap], row: -1)
    }
}

/// Reordenar: linha de destino sob o dedo (0 = topo = camada da frente).
enum Reorder {
    struct Preview {
        let offsets: [CGFloat]
        let sourceStart: Int
        let sourceEnd: Int
        let gapTop: CGFloat
    }

    /// Presentation only, matching Android. A layer includes its expanded tracks.
    static func preview(tops: [CGFloat], ids: [Int64], source: Int, target: Int, dragTop: CGFloat) -> Preview {
        var offsets = [CGFloat](repeating: 0, count: ids.count)
        guard ids.indices.contains(source), ids.indices.contains(target), tops.count == ids.count + 1, dragTop.isFinite else {
            return Preview(offsets: offsets, sourceStart: -1, sourceEnd: -1, gapTop: .nan)
        }
        var start = source, end = source + 1
        while start > 0 && ids[start - 1] == ids[source] { start -= 1 }
        while end < ids.count && ids[end] == ids[source] { end += 1 }
        var targetStart = target, targetEnd = target + 1
        while targetStart > 0 && ids[targetStart - 1] == ids[target] { targetStart -= 1 }
        while targetEnd < ids.count && ids[targetEnd] == ids[target] { targetEnd += 1 }
        let height = tops[end] - tops[start]
        for i in ids.indices {
            if i >= start && i < end { offsets[i] = dragTop - tops[start] }
            else if targetStart > start && i >= end && i < targetEnd { offsets[i] = -height }
            else if targetStart < start && i >= targetStart && i < start { offsets[i] = height }
        }
        let gap = targetStart > start ? tops[targetEnd] - height : tops[targetStart]
        return Preview(offsets: offsets, sourceStart: start, sourceEnd: end, gapTop: gap)
    }

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

// =============================================================================
// Acabamentos (TimelineEmphasis.kt)
// =============================================================================
/// Ênfase dos losangos na barra do clipe (spec am-ui §4.2, P1-6): com um painel
/// editando um parâmetro, os losangos DELE ficam fortes e os dos outros
/// parâmetros animados da camada esmaecidos — só de referência, sem toque nem
/// arrasto. O losango sob o cabeçote acende na cor de destaque. Par do
/// `KeyEmphasis` do Android.
enum KeyEmphasis {
    /// Opacidade dos losangos dos outros parâmetros (160/255, a da referência).
    static let dimAlpha: Double = 160.0 / 255.0

    /// Cor de um losango forte, da maior prioridade para a menor.
    enum Tone: Equatable { case chosen, picked, playhead, off }

    /// Instantes de `all` que o foco não marca (os dois ordenados, sem repetir).
    static func dimmed(_ all: [Int32], strong: [Int32]) -> [Int32] {
        if strong.isEmpty { return all }
        let marked = Set(strong)
        return all.filter { !marked.contains($0) }
    }

    /// Escolhido (âmbar) > na seleção da timeline (azul) > sob o cabeçote (destaque) > branco.
    static func tone(chosen: Bool, picked: Bool, underPlayhead: Bool) -> Tone {
        if chosen { return .chosen }
        if picked { return .picked }
        if underPlayhead { return .playhead }
        return .off
    }
}

/// Fileira nova surge com fade (spec am-ui §4.3: alfa 0→1 em 600 ms,
/// Decelerate). "Nova" = camada que não estava na leitura anterior da MESMA
/// composição; abrir projeto ou entrar num grupo (lista trocada inteira) não
/// anima. Par do `RowFade` do Android.
enum TimelineRowFade {
    static let duration: Double = 0.6

    /// Opacidade `elapsed` segundos depois de nascer: Decelerate(1) = 1 − (1 − t)².
    static func alpha(_ elapsed: Double) -> Double {
        if elapsed <= 0 { return 0 }
        if elapsed >= duration { return 1 }
        let t = elapsed / duration
        return 1 - (1 - t) * (1 - t)
    }

    /// Camadas de `current` fora de `previous`. nil = primeira leitura: nada nasce;
    /// nenhuma em comum com uma lista anterior não vazia = lista trocada: nada nasce;
    /// lista anterior vazia e UMA camada agora = a primeira do projeto vazio.
    static func born(previous: [Int64]?, current: [Int64]) -> [Int64] {
        guard let previous, !current.isEmpty else { return [] }
        if previous.isEmpty { return current.count == 1 ? current : [] }
        let known = Set(previous)
        let fresh = current.filter { !known.contains($0) }
        return fresh.count == current.count ? [] : fresh
    }
}

/// Rolagem vertical ao entrar e sair da fileira compacta (referência:
/// `originalScrollY`): entrar guarda a da lista inteira e zera (o compacto tem
/// uma fileira só); sair devolve a guardada, presa ao máximo de agora. O
/// Android guarda a mesma rolagem no `TimelineState` (o compacto não a toca).
enum TimelineCompactScroll {
    static func transition(entering: Bool, current: CGFloat, saved: CGFloat?, maxScroll: CGFloat) -> (scroll: CGFloat, saved: CGFloat?) {
        if entering { return (0, saved ?? current) }
        return (min(max(0, saved ?? current), max(0, maxScroll)), nil)
    }
}

/**
 * Teto da auto-rolagem durante um arraste/aparo (docs/diagnostico/export-falhas.md,
 * A.3 item 1 e C2 item 1). Par do `GestureLimit` do Android (TimelineMath.kt).
 *
 * Sem ele, segurar um clipe (ou a borda dele) na margem direita virava uma
 * catraca: a vista é limitada pelo `navigationEnd` VIVO, que durante o arraste
 * é o fim do próprio clipe; cada passo da auto-rolagem empurra o clipe, que
 * empurra o teto, e a composição crescia até horas.
 *
 * Regra: no começo do gesto vale `max(navigationEnd, duração) + 10 s` (em
 * quadros, pelo fps da composição), congelado até soltar. A vista e o
 * deslocamento aplicado não passam disso. Fora de gesto nada muda (`none`).
 */
enum GestureLimit {
    /// Folga além do conteúdo que um gesto pode abrir de uma vez.
    static let marginSeconds: Double = 10
    /// Sem gesto em curso: nenhum teto extra.
    static let none: Int64 = .max
    /// Maior quadro do protocolo de frames.
    private static let maxFrame: Int64 = Int64(Int32.max) - 1

    static func autoScrollLimit(startNavEnd: Int64, startDuration: Int64, fps: Double) -> Int64 {
        let safe = fps.isFinite && fps >= 1 && fps <= 1000 ? fps : 30
        let margin = Int64((marginSeconds * safe).rounded(.up))
        let base = min(maxFrame, max(0, max(startNavEnd, startDuration)))
        return min(maxFrame, base + margin)
    }

    /// Próxima vista da auto-rolagem: não avança além de `limit`. Se a vista já
    /// estava além (o teto não a puxa de volta), só deixa recuar.
    static func clampView(_ next: Double, current: Double, limit: Int64) -> Double {
        guard limit != none, next > current else { return next }
        return min(next, max(current, Double(limit)))
    }

    /// Quadro-alvo de um arraste/aparo: no máximo `limit`.
    static func clampFrame(_ frame: Int32, limit: Int64) -> Int32 {
        guard limit != none, Int64(frame) > limit else { return frame }
        return Int32(clamping: min(max(0, limit), maxFrame))
    }

    /// Maior deslocamento à direita de um lote cujo maior fim é `maxEnd`: nem
    /// além do protocolo de frames, nem com o fim passando de `limit`.
    static func maxShift(maxEnd: Int32, limit: Int64) -> Int32 {
        let protocolShift = maxFrame - Int64(maxEnd)
        let bounded = limit == none ? protocolShift : min(protocolShift, max(0, limit - Int64(maxEnd)))
        return Int32(clamping: max(0, bounded))
    }
}

// =============================================================================
//  Omega 0.3.2 (área 03): regras puras da timeline da referência. Par do bloco
//  "Omega" de TimelineMath.kt; os mesmos números (verify/check_timeline_omega.py).
// =============================================================================

/// 0.3.4 (bugs-034, cores): cor do cartão do clipe, identidade própria do AUREA
/// (par do `ClipColors` do Android, a mesma conta inteira). Cor SÓLIDA do tipo
/// (`TimelineLayerType.argb`), 1/5 dessaturada rumo ao cinza de mesma luma e 1/8
/// mais escura, para o rótulo branco ficar legível. Sem degradê e sem variação por id.
enum ClipColors {
    /// Interpolação por canal ARGB, `num/den` de `a` para `b`, arredondada.
    static func mix(_ a: UInt32, _ b: UInt32, _ num: Int, _ den: Int) -> UInt32 {
        guard den > 0 else { return a }
        let n = min(max(num, 0), den)
        var out: UInt32 = 0
        for shift: UInt32 in [24, 16, 8, 0] {
            let x = Int((a >> shift) & 0xFF), y = Int((b >> shift) & 0xFF)
            let v = (x * (den - n) + y * n + den / 2) / den
            out |= UInt32(min(max(v, 0), 255)) << shift
        }
        return out
    }

    /// Luma inteira (Rec. 601, arredondada) de uma cor ARGB.
    static func luma(_ argb: UInt32) -> UInt32 {
        let r = Int((argb >> 16) & 0xFF), g = Int((argb >> 8) & 0xFF), b = Int(argb & 0xFF)
        return UInt32((r * 299 + g * 587 + b * 114 + 500) / 1000)
    }

    /// Cor do cartão a partir da cor do tipo `base` (ARGB): 1/5 dessaturada, 1/8 mais escura, opaca.
    static func solid(_ base: UInt32) -> UInt32 {
        let y = luma(base)
        let gray: UInt32 = 0xFF00_0000 | (y << 16) | (y << 8) | y
        let muted = mix(base | 0xFF00_0000, gray, 1, 5)
        return mix(muted, 0xFF00_0000, 1, 8)
    }
}

/// Cor das marcas (par do `MarkerColors` do Android). O motor guarda RGBA8 com o
/// R no byte BAIXO (0xAABBGGRR, `Marker::color`); a timeline desenha ARGB.
enum MarkerColors {
    /// A cor padrão que o motor dá à marca criada no toque.
    static let engineDefault: UInt32 = 0xFFF7C34F
    /// As quatro aparências da Omega em ARGB (os mesmos tons de `AureaTimeline.bookmarks`).
    static let roles: [UInt32] = [0xFFF16586, 0xFFFAD3A5, 0xFF4BE29D, 0xFF3C8CE7]

    /// RGBA do motor (R baixo) → ARGB. A troca R↔B é a própria inversa.
    static func unpack(_ packed: UInt32) -> UInt32 {
        0xFF00_0000 | ((packed & 0xFF) << 16) | (packed & 0xFF00) | ((packed >> 16) & 0xFF)
    }
    /// ARGB → RGBA do motor (R baixo), opaco.
    static func pack(_ argb: UInt32) -> UInt32 { unpack(argb) }

    // 0.3.3 recursos: o marcador antigo (seis cores) volta, com a correção de cor.
    /// A paleta de seis cores do marcador da 0.3.2, em ARGB (amarelo, azul, verde,
    /// laranja, lilás, branco). O amarelo é a cor padrão do motor (`engineDefault`).
    static let legacy: [UInt32] = [0xFFF7C34F, 0xFF4D6EFF, 0xFF70D56B, 0xFFFFB75B, 0xFFD67BDB, 0xFFFFFFFF]

    /// A cor (ARGB) com que a marca aparece. A 0.3.2 gravava os literais ARGB da
    /// paleta sem converter (e o motor nasce com `engineDefault`): esses valores
    /// crus são reconhecidos e mostrados na cor que pretendiam. O resto (o que o
    /// editor grava agora, com `pack`) é desempacotado normalmente. Par do Android.
    static func display(_ packed: UInt32) -> UInt32 {
        let opaque = packed | 0xFF00_0000
        if legacy.contains(opaque) { return opaque }
        return unpack(packed)
    }

    /// Índice da cor da paleta antiga (0..5) com que a marca aparece; −1 = outra cor.
    static func legacyIndex(_ packed: UInt32) -> Int { legacy.firstIndex(of: display(packed)) ?? -1 }

    /// Aparência (0..3): a cor exata de um papel, a cor padrão do motor (= 0) ou o
    /// papel mais perto (marcas antigas com a paleta de 6 cores).
    static func appearance(_ packed: UInt32, roles: [UInt32] = MarkerColors.roles) -> Int {
        guard !roles.isEmpty else { return 0 }
        if packed | 0xFF00_0000 == engineDefault { return 0 }
        let argb = unpack(packed)
        var best = 0, bestD = Int.max
        for (i, role) in roles.enumerated() {
            let c = role | 0xFF00_0000
            if c == argb { return i }
            let dr = Int((c >> 16) & 0xFF) - Int((argb >> 16) & 0xFF)
            let dg = Int((c >> 8) & 0xFF) - Int((argb >> 8) & 0xFF)
            let db = Int(c & 0xFF) - Int(argb & 0xFF)
            let d = dr * dr + dg * dg + db * db
            if d < bestD { bestD = d; best = i }
        }
        return best
    }
}

/// Régua da Omega: tiques pendurados do alto, em três alturas, sem rótulos.
enum OmegaRuler {
    static let minTickDp: CGFloat = 4
    static let secondTick = 2, halfTick = 1, frameTick = 0

    /// Passo dos tiques de quadro: a próxima potência de 2 de max(1, quadros que
    /// cabem em 4 pt). 30 fps a 100 pt/s = 1; a 10 pt/s = 16.
    static func frameStep(pps: CGFloat, fps: Float) -> Int {
        let ppfDp = Double(pps) / Double(TimeAxis.safeFps(fps))
        guard ppfDp > 0, ppfDp.isFinite else { return 1 << 20 }
        let n = Int(min(max(1, (Double(minTickDp) / ppfDp).rounded(.down)), Double(1 << 20)))
        var p = 1
        while p < n { p <<= 1 }
        return p
    }

    /// Passo dos tiques de segundo (em segundos, potência de 2): 4 pt no mínimo entre eles.
    static func secondStep(pps: CGFloat) -> Int {
        guard pps > 0, pps.isFinite else { return 1 << 20 }
        var p = 1
        while CGFloat(p) * pps < minTickDp && p < (1 << 20) { p <<= 1 }
        return p
    }

    /// O 1º quadro que cai no instante `seconds`.
    static func frameOf(_ seconds: Double, fps: Float) -> Int64 {
        Int64((seconds * Double(TimeAxis.safeFps(fps)) - 1e-6).rounded(.up))
    }

    /// O quadro começa um segundo (2), um meio segundo (1) ou nenhum dos dois (0)?
    static func tickClass(_ frame: Int64, fps: Float) -> Int {
        let f = Double(TimeAxis.safeFps(fps))
        func starts(_ period: Double) -> Bool {
            frame <= 0 || (Double(frame) / period + 1e-9).rounded(.down) > (Double(frame - 1) / period + 1e-9).rounded(.down)
        }
        if starts(f) { return secondTick }
        if starts(f / 2) { return halfTick }
        return frameTick
    }

    /// Altura do tique da classe (pt).
    static func height(_ tickClass: Int) -> CGFloat {
        tickClass == secondTick ? 18 : tickClass == halfTick ? 12 : 5
    }
}

/// Auto-rolagem da Omega (referência: 120 pt/s × fator, a cada quadro).
enum OmegaScroll {
    static let edge: CGFloat = 38
    static let speed: CGFloat = 120
    static let maxFactor: CGFloat = 8

    /// Fator com sinal na faixa das bordas: 1× ao entrar e até 5× na borda; 0
    /// fora dela. Só conta o lado a que o dedo FOI desde `from`.
    static func edgeFactor(pos: CGFloat, from: CGFloat, start: CGFloat, end: CGFloat, zone: CGFloat, intent: CGFloat) -> CGFloat {
        guard zone > 0, end > start, pos.isFinite else { return 0 }
        let low = start + zone, high = end - zone
        if pos < low && pos < from - intent { return -min(maxFactor, max(1, (low - pos) / (zone / 5))) }
        if pos > high && pos > from + intent { return min(maxFactor, max(1, (pos - high) / (zone / 5))) }
        return 0
    }

    /// Reordenar na vertical: em cima, abaixo de `rowsTop + faixa/2`; embaixo, a
    /// partir de `altura − faixa`. Negativo = rolar para cima.
    static func reorderFactor(y: CGFloat, height: CGFloat, rowsTop: CGFloat, zone: CGFloat) -> CGFloat {
        guard zone > 0, y.isFinite else { return 0 }
        let top = rowsTop + zone / 2
        if y < top { return -min(maxFactor, max(1, (top - y) / (zone / 3))) }
        if y > height - zone { return min(maxFactor, max(1, (y - height) / (zone / 5))) }
        return 0
    }
}

/// Quanto do toque longo cada alvo espera na Omega (par do `OmegaHold` do Android).
enum OmegaHold {
    enum Target { case trim, body, key, header, other }
    static func fraction(_ target: Target, compact: Bool) -> Double {
        switch (target, compact) {
        case (.trim, true), (.body, true): return 0.25
        case (.key, true): return 0.9
        case (.header, false), (.body, false), (.key, false): return 0.95
        default: return 1
        }
    }
    /// O toque longo do sistema que o iOS iguala ao Android 12+ (400 ms).
    static let base: Double = 0.4
}

/// Toque curto na régua da Omega: navega entre os marcadores (não busca o quadro tocado).
enum MarkerNav {
    static func target(_ frames: [Int64], x: CGFloat, width: CGFloat, playhead: Int64, near: CGFloat, xOf: (Int64) -> CGFloat) -> Int64? {
        var best: Int64?
        var bestD = CGFloat.greatestFiniteMagnitude
        for f in frames {
            let d = abs(xOf(f) - x)
            if d <= near && d < bestD { best = f; bestD = d }
        }
        if let best { return best }
        return x >= width / 2 ? next(frames, playhead) : previous(frames, playhead)
    }
    static func next(_ frames: [Int64], _ playhead: Int64) -> Int64? { frames.first { $0 > playhead } }
    static func previous(_ frames: [Int64], _ playhead: Int64) -> Int64? { frames.last { $0 < playhead } }
}

/// Pulso ao levantar o clipe: 1 → 1,04 → 1 em 130 ms, curva sin(πt).
enum LiftPulse {
    static let duration: Double = 0.13
    static let peak: CGFloat = 0.04
    static func scale(_ elapsed: Double) -> CGFloat {
        guard elapsed > 0, elapsed < duration else { return 1 }
        return 1 + peak * CGFloat(sin(Double.pi * elapsed / duration))
    }
}

/// Reticências no MEIO ("começo…fim"), a maior que cabe (par do Android).
enum MiddleEllipsis {
    static func cut(_ text: String, keep: Int) -> String {
        let letters = Array(text)
        if keep >= letters.count { return text }
        let k = max(0, keep), head = (k + 1) / 2, tail = k / 2
        return String(letters.prefix(head)) + "\u{2026}" + String(letters.suffix(tail))
    }
    static func fit(_ text: String, fits: (String) -> Bool) -> String {
        if text.isEmpty || fits(text) { return text }
        var lo = 0, hi = text.count - 1
        while lo < hi {
            let mid = (lo + hi + 1) / 2
            if fits(cut(text, keep: mid)) { lo = mid } else { hi = mid - 1 }
        }
        let out = cut(text, keep: lo)
        return lo == 0 && !fits(out) ? "" : out
    }
}

/// Duas camadas ou mais escolhidas, ou o modo de escolher várias: a "seleção múltipla" da referência.
func omegaMultiSelection(layerSelectMode: Bool, selected: Int) -> Bool { layerSelectMode || selected >= 2 }
