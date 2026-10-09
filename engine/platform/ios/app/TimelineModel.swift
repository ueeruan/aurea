// =============================================================================
//  Aurea / platform / ios / app / TimelineModel.swift
//
//  PORTE de `TimelineMetrics.kt` + `TimelineModel.kt` + `TimelineState.kt`
//  (as duas últimas classes de material: linhas prontas para o pintor, tira de
//  miniaturas e waveform).
//
//  TimelineMetrics é a geometria em PX/pt: a MESMA conta serve para pintar e
//  para tocar (o que se vê é o que responde ao dedo). No iOS 1 pt = 1 dp, então
//  `density` = 1 (é o mesmo número que os testes de JVM do Android usam).
// =============================================================================
import CoreGraphics
import Foundation
import SwiftUI
import UIKit

// =============================================================================
// Geometria (TimelineMetrics.kt)
// =============================================================================
struct TimelineMetrics {
    let density: CGFloat
    let fontScale: CGFloat

    init(density: CGFloat = 1, fontScale: CGFloat = 1) {
        self.density = density
        self.fontScale = fontScale
    }

    private func dp(_ v: CGFloat) -> CGFloat { v * density }

    // --- Régua e linhas (redesenho 2026-09-29) -----------------------------------
    /// Riscos em 0..14; relógio e sublinhado logo abaixo.
    var rulerTicks: CGFloat { dp(AureaTimeline.rulerTicks) }
    /// Primeira linha em 44; pílula em 48 e barra em 50, como no Android.
    var rowsTop: CGFloat { dp(AureaTimeline.rulerTicks + AureaTimeline.rulerGap) }
    /// Passo da fileira: pílula 28 + 4 de vão.
    var row: CGFloat { dp(AureaTimeline.row) }
    /// Topo da pílula dentro da fileira (o vão de 4 fica em cima).
    var pillTop: CGFloat { dp(4) }
    var pillHeight: CGFloat { dp(28) }
    var pillRadius: CGFloat { dp(14) }
    /// Topo da barra dentro da fileira: 2 abaixo do topo da pílula.
    var barTop: CGFloat { pillTop + dp(2) }
    var bar: CGFloat { dp(AureaTimeline.bar) }
    var barRadius: CGFloat { dp(AureaTimeline.barRadius) }
    var barMinWidth: CGFloat { dp(40) }
    var track: CGFloat { dp(11) }
    /// Começo da faixa dos losangos (medido do topo da barra).
    var trackTop: CGFloat { bar - track }
    /// Folga abaixo das linhas (a última não cola na borda).
    var bottomPad: CGFloat { dp(24) }

    // --- Pílula da fileira (olho + quadradinho do glifo, colada à esquerda) -------
    /// Largura da pílula; as barras passam por BAIXO dela (ela é opaca).
    var headerColumn: CGFloat { dp(AureaTimeline.headerColumn) }
    /// Olho de 20 centrado em x 16; x < 28 é o toque do olho.
    let eyeIcon: CGFloat = 20
    var eyeCx: CGFloat { dp(16) }
    var eyeHitRight: CGFloat { dp(28) }
    /// Miniatura de 20 × 20, de x 38 a 58, raio 3.
    var glyphBoxLeft: CGFloat { dp(38) }
    var glyphBox: CGFloat { dp(20) }
    var glyphBoxRadius: CGFloat { dp(3) }
    var glyphBoxStroke: CGFloat { dp(1.5) }
    let glyphText: CGFloat = 11
    let glyphIcon: CGFloat = 12
    var shapeDot: CGFloat { dp(10) }
    /// Cadeado pequeno depois do quadradinho.
    let gutterLock: CGFloat = 10
    var lockCx: CGFloat { dp(66) }
    /// Trilhas de propriedade: ▸/▾ numa coluna de 28 e o nome a partir de x 30.
    var laneChevronCx: CGFloat { dp(16) }
    var laneHeader: CGFloat { dp(28) }
    var laneLabelX: CGFloat { dp(30) }

    // --- Régua ------------------------------------------------------------------
    /// Riscos de 1 pt: fortes de 12, finos de 6.
    var tickMajorTop: CGFloat { 0 }
    var tickMinorTop: CGFloat { 0 }
    var tickBottom: CGFloat { dp(12) }
    var tickMinorBottom: CGFloat { dp(6) }
    var tickMajorWidth: CGFloat { dp(1) }
    var tickMinorWidth: CGFloat { dp(1) }
    var tickLabelGap: CGFloat { dp(3) }

    // --- Conteúdo da barra --------------------------------------------------------
    var stripe: CGFloat { dp(AureaTimeline.clipStripe) }
    var padL: CGFloat { dp(8) }
    var padR: CGFloat { dp(8) }
    var padLNarrow: CGFloat { dp(6) }
    var padRNarrow: CGFloat { dp(3) }
    var narrowBar: CGFloat { dp(46) }
    let typeIcon: CGFloat = AureaTimeline.clipIcon
    var iconGap: CGFloat { dp(6) }
    let lockIcon: CGFloat = 10
    var lockGap: CGFloat { dp(5) }
    var arrowSlot: CGFloat { dp(22) }
    let arrowGlyph: CGFloat = 14
    let menuGlyph: CGFloat = 14
    var iconMinBar: CGFloat { dp(28) }
    var nameMinBar: CGFloat { dp(52) }
    var lockGapMinBar: CGFloat { dp(70) }
    var menuMinBar: CGFloat { dp(90) }
    var selStroke: CGFloat { dp(AureaTimeline.clipSelStroke) }
    var multiStroke: CGFloat { dp(AureaTimeline.clipSelStroke) }
    var lightLine: CGFloat { dp(1) }
    /// O toque do corpo inclui mais 4 pt abaixo da barra.
    var bodyHitBottom: CGFloat { bar + dp(4) }
    var arrowTouchPad: CGFloat { dp(6) }
    /// Fileira compacta, clipe escolhido (Efeitos.dc.html; par do TimelineMetrics.kt):
    /// tampa branca "‹" de 34 antes do início real do clipe (tocar = voltar), contorno
    /// branco de 1,5 e a ponta esquerda arredondada (raio 14, preso à meia barra).
    var capWidth: CGFloat { dp(34) }
    var capStroke: CGFloat { dp(1.5) }
    var capRadius: CGFloat { min(dp(14), bar / 2) }
    let capGlyph: CGFloat = 14
    var capNameGap: CGFloat { dp(10) }

    // --- Alça de trim (16 × (barra − 4), top 2, DENTRO das pontas) ------------------
    var trimWidth: CGFloat { dp(16) }
    var trimTop: CGFloat { dp(2) }
    var trimInsetStart: CGFloat { dp(3) }
    var trimInsetEnd: CGFloat { dp(13) }
    var trimRadius: CGFloat { dp(4) }
    var gripWidth: CGFloat { dp(2) }
    var gripHeight: CGFloat { dp(14) }
    /// Folga de toque para FORA da barra, além do desenho.
    var trimTouchOut: CGFloat { dp(13) }

    // --- Losango ------------------------------------------------------------------
    var diamond: CGFloat { dp(9) }
    var diamondRadius: CGFloat { dp(2) }
    var diamondStroke: CGFloat { dp(1.2) }
    /// Centro do losango: 22 abaixo do topo da barra (pode passar um pouco da base dela).
    var diamondCyNormal: CGFloat { bar / 2 }
    var diamondCyCompact: CGFloat { bar / 2 }
    var keyTouchHalf: CGFloat { dp(14) }
    /// Folga do losango até o alvo mínimo de 48 pt; cede às alças, tampa e setas (par do Android).
    var keyHitHalf: CGFloat { dp(24) }
    /// Histerese do arrasto de losango: o frame só troca depois de meio frame + isto.
    var keyDragHysteresis: CGFloat { dp(3) }
    var keyGlyphHalf: CGFloat { dp(6) }
    var keyTouchTop: CGFloat { 0 }
    var keyMergeGap: CGFloat { dp(4) }
    var keyPillHeight: CGFloat { dp(10) }
    var keyPillMinWidth: CGFloat { dp(16) }
    let keyDragScale: CGFloat = 1.4
    var balloonPadH: CGFloat { dp(5) }
    var balloonPadV: CGFloat { dp(2) }
    var balloonRadius: CGFloat { dp(4) }
    var balloonGap: CGFloat { dp(3) }

    // --- Cabeçote e relógio ----------------------------------------------------------
    /// Cabeçote branco de 1 pt, de y 36 (abaixo do relógio) até o fim.
    var playhead: CGFloat { dp(AureaTimeline.playhead) }
    var playheadTop: CGFloat { dp(36) }
    /// Compacto (painel aberto): o cabeçote vermelho de antes, de cima a baixo.
    var compactPlayhead: CGFloat { dp(1.6) }
    var knob: CGFloat { dp(8) }
    var knobRadius: CGFloat { dp(2) }
    /// Triângulo do cabeçote no alto da régua (camada escolhida): 10 × 8, no destaque.
    var markerWidth: CGFloat { dp(10) }
    var markerHeight: CGFloat { dp(8) }
    /// Faixa do relógio (MM:SS:FF), centrado no cabeçote.
    var timecodeTop: CGFloat { dp(14) }
    var timecodeBottom: CGFloat { dp(36) }
    /// Relógio de 16 pt, sublinhado de 1,5 pt da largura do texto em y 30.
    let timecodeFont: CGFloat = 16
    var underlineTop: CGFloat { dp(30) }
    var underlineHeight: CGFloat { dp(1.5) }
    /// Estilo caixa: 26 de alto, borda de 1,5 no destaque, raio 4, 6 de lado, 15 pt.
    let timecodeBoxFont: CGFloat = 15
    var timecodeBoxHeight: CGFloat { dp(26) }
    var timecodeBoxStroke: CGFloat { dp(1.5) }
    var timecodeBoxRadius: CGFloat { dp(4) }
    var timecodeBoxPad: CGFloat { dp(6) }

    // --- Gestos -------------------------------------------------------------------------
    var snapClip: CGFloat { dp(12) }
    var snapKey: CGFloat { dp(8) }
    /// Faixa da auto-rolagem: 48 pt no máximo (menos numa janela baixa, ver `AutoScroll.zone`).
    var autoEdge: CGFloat { dp(48) }
    /// Velocidade no fundo da faixa (pt/s); a rampa começa em 0 na entrada dela.
    var autoSpeed: CGFloat { dp(360) }
    /// Trilha baixa: a vizinha também responde até este tanto do centro dela (alvo ≥ 32 pt).
    var laneTouchReach: CGFloat { dp(16) }
    var autoIntent: CGFloat { dp(4) }
    /// O `touchSlop` do Android e o eixo do toque longo (A.01) valem 8 dp.
    var axisSlop: CGFloat { dp(8) }
    var flingMin: CGFloat { dp(50) }

    // --- Guias ----------------------------------------------------------------------------
    var guide: CGFloat { dp(1) }
    var reorderLine: CGFloat { dp(2) }
    var reorderDot: CGFloat { dp(3) }
}

// =============================================================================
// Tipos de camada (AureaTokens.LayerType) — cor e glifo do desenho da barra
// =============================================================================
enum TimelineLayerType: Int, CaseIterable {
    case video = 0, image, audio, text, shape, null, adjustment, camera, light, model3D, particles, group

    /// O `kind` do motor (LayerRow.kind).
    var kind: Int { rawValue + 1 }

    var color: Color {
        switch self {
        case .video:      return Color(hex: 0x6A52E0)
        case .image:      return Color(hex: 0x3D6FD9)
        case .audio:      return Color(hex: 0x1F8C93)
        case .text:       return Color(hex: 0xB07A16)
        case .shape:      return Color(hex: 0x2E9459)
        case .null:       return Color(hex: 0x444C5C)
        case .adjustment: return Color(hex: 0x5A4A7A)
        case .camera:     return Color(hex: 0x2A7B9B)
        case .light:      return Color(hex: 0xC06A24)
        case .model3D:    return Color(hex: 0xC06A24)
        case .particles:  return Color(hex: 0xB0417A)
        case .group:      return Color(hex: 0x4C5566)
        }
    }

    /// Os mesmos codepoints da fonte CupertinoIcons embarcada no Android.
    var glyph: Character {
        switch self {
        case .video:      return CupertinoGlyph.VideocamFill
        case .image:      return CupertinoGlyph.PhotoFill
        case .audio:      return CupertinoGlyph.MusicNote
        case .text:       return CupertinoGlyph.Textformat
        case .shape:      return CupertinoGlyph.CircleFill
        case .null:       return CupertinoGlyph.SmallcircleCircle
        case .adjustment: return CupertinoGlyph.SliderHorizontal3
        case .camera:     return CupertinoGlyph.CameraFill
        case .light:      return CupertinoGlyph.Lightbulb
        case .model3D:    return CupertinoGlyph.CubeFill
        case .particles:  return CupertinoGlyph.Sparkles
        case .group:      return CupertinoGlyph.FolderFill
        }
    }

    static func of(_ kind: UInt32) -> TimelineLayerType {
        TimelineLayerType(rawValue: Int(kind) - 1) ?? .null
    }
}

// =============================================================================
// Linhas (TimelineModel.kt)
// =============================================================================
/**
 * Uma linha da timeline, derivada do que o modelo LEU do motor (não é cópia
 * editável: some e renasce a cada revisão). Existe para o pintor não alocar por
 * quadro: o nome e os instantes de keyframe saem prontos daqui.
 */
struct TimelineRow {
    let id: Int64
    let type: TimelineLayerType
    let start: Int32
    let end: Int32
    let offset: Int32
    let visible: Bool
    let locked: Bool
    /// LINHA MAGNÉTICA: os cortes desta camada andam como faixa de montagem.
    let magnetic: Bool
    let animated: Bool
    let name: String
    /// Etiqueta de cor (0 = nenhuma; i = `labelPalette[i - 1]`).
    let label: UInt32
    /// Instantes com keyframe (qualquer trilha), em frames da TIMELINE, ordenados e sem repetição.
    let instants: [Int32]
    /// Keyframes de cada instante (todas as trilhas que têm marca ali), paralelo a `instants`.
    let keysAt: [[KeyframeItem]]
    var track: TimelineTrack? = nil
    /// A LINHA da timeline (`LayerItem.trackId`); 0 = sem linha (projeto antigo: a camada é a linha dela).
    var line: UInt32 = 0
    /// FILEIRA COMPARTILHADA: os trechos da MESMA linha que dividem esta
    /// fileira, lado a lado, em ordem de tempo. nil = fileira de um trecho só
    /// (ela mesma). Os campos desta fileira são só o resumo dela (pílula,
    /// altura); quem se desenha, se toca e se edita é cada trecho de `segments`.
    var shared: [TimelineRow]? = nil

    /// Os trechos desenhados e tocados nesta fileira: os da linha, ou só ela.
    var segments: [TimelineRow] { shared ?? [self] }

    /// O trecho `id` desta fileira (nil = não mora aqui).
    func segment(_ id: Int64) -> TimelineRow? {
        guard let shared else { return self.id == id ? self : nil }
        return shared.first { $0.id == id }
    }

    /// Vídeo e imagem têm miniatura no motor; o resto é só a cor.
    var hasThumbs: Bool { track == nil && (type == .video || type == .image) }

    func toLocal(_ timelineFrame: Int32) -> Int32 {
        Keyframes.toLocal(timelineFrame, start, offset)
    }
}

func buildTimelineRow(_ l: LayerItem, _ all: [KeyframeItem]) -> TimelineRow {
    let keys = all.sorted { $0.time < $1.time }
    var instants: [Int32] = []
    var groups: [[KeyframeItem]] = []
    var from = 0
    while from < keys.count {
        var to = from + 1
        while to < keys.count && keys[to].time == keys[from].time { to += 1 }
        instants.append(Keyframes.toTimeline(keys[from].time, l.startFrame, l.offsetFrames))
        groups.append(Array(keys[from..<to]))
        from = to
    }
    return TimelineRow(id: l.id,
                       type: TimelineLayerType.of(l.kind),
                       start: l.startFrame,
                       end: l.endFrame,
                       offset: l.offsetFrames,
                       visible: l.visible,
                       locked: l.locked,
                       magnetic: l.magnetic,
                       animated: l.animated || !keys.isEmpty,
                       name: l.name,
                       label: l.label,
                       instants: instants,
                       keysAt: groups,
                       line: l.trackId)
}

/**
 * Fileiras por LINHA (`trackId`) — o mesmo do Android (`sharedRows`): trechos
 * da mesma linha dividem UMA fileira, lado a lado no tempo. A fileira fica na
 * posição do trecho MAIS ALTO da linha na ordem de desenho. Camada sem par
 * (linha só dela, ou linha 0 de projeto antigo) volta como estava. Trechos da
 * mesma linha que se SOBREPÕEM no tempo não se escondem: o que não cabe desce
 * para uma fileira logo abaixo, da mesma linha.
 */
func timelineSharedRows(_ rows: [TimelineRow]) -> [TimelineRow] {
    var byLine: [UInt32: [TimelineRow]] = [:]
    var anyShared = false
    for row in rows where row.line != 0 && row.track == nil {
        byLine[row.line, default: []].append(row)
        if byLine[row.line]!.count >= 2 { anyShared = true }
    }
    guard anyShared else { return rows }
    var out: [TimelineRow] = []
    out.reserveCapacity(rows.count)
    var emitted = Set<UInt32>()
    for row in rows {
        guard row.line != 0, row.track == nil, let group = byLine[row.line], group.count >= 2 else {
            out.append(row)
            continue
        }
        // Já saiu inteira com o trecho mais alto dela.
        if !emitted.insert(row.line).inserted { continue }
        let ordered = group.sorted {
            if $0.start != $1.start { return $0.start < $1.start }
            if $0.end != $1.end { return $0.end < $1.end }
            return $0.id < $1.id
        }
        var packed: [[TimelineRow]] = []
        var ends: [Int32] = []
        for segment in ordered {
            var k = 0
            while k < packed.count && ends[k] > segment.start { k += 1 }
            if k == packed.count {
                packed.append([segment])
                ends.append(segment.end)
            } else {
                packed[k].append(segment)
                ends[k] = max(ends[k], segment.end)
            }
        }
        for segments in packed {
            out.append(segments.count == 1 ? segments[0] : timelineSharedRow(line: row.line, segments))
        }
    }
    return out
}

/// O resumo de uma fileira compartilhada: a pílula acende se algum trecho aparece e trava se todos travam.
private func timelineSharedRow(line: UInt32, _ segments: [TimelineRow]) -> TimelineRow {
    let head = segments[0]
    var start = head.start, end = head.end
    var visible = false, locked = true, magnetic = false, animated = false
    for s in segments {
        start = min(start, s.start); end = max(end, s.end)
        visible = visible || s.visible
        locked = locked && s.locked
        magnetic = magnetic || s.magnetic
        animated = animated || s.animated
    }
    return TimelineRow(id: head.id, type: head.type, start: start, end: end, offset: 0,
                       visible: visible, locked: locked, magnetic: magnetic, animated: animated,
                       name: head.name, label: head.label, instants: [], keysAt: [],
                       line: line, shared: segments)
}

/// Chave de GRUPO de cada fileira para reordenar na vertical (o mesmo do
/// Android): fileiras seguidas com a mesma chave andam juntas. Trilha de
/// propriedade aberta anda com a camada dona; as fileiras de uma linha andam
/// juntas; o resto é a própria camada.
func timelineGroupKeys(_ rows: [TimelineRow]) -> [Int64] {
    var keys = [Int64](repeating: 0, count: rows.count)
    for (i, row) in rows.enumerated() {
        if row.track != nil && i > 0 { keys[i] = keys[i - 1] }
        else if row.line != 0 { keys[i] = Int64.min + Int64(row.line) }
        else { keys[i] = row.id }
    }
    return keys
}

/// Reordenar na vertical um GRUPO de camadas (os trechos de uma linha andam
/// juntos) com o comando de sempre, que leva UMA camada a uma posição da lista
/// (0 = topo). O grupo vai inteiro para logo ACIMA da camada mais alta do
/// destino (subindo) ou logo ABAIXO dela (descendo). Devolve os passos
/// (camada, posição final); uma camada sozinha dá UM passo, o mesmo de antes.
enum TimelineRowOrder {
    static func moves(order: [Int64], block: Set<Int64>, anchor: Int64, up: Bool) -> [(id: Int64, index: Int)] {
        guard !block.isEmpty, !block.contains(anchor) else { return [] }
        let rest = order.filter { !block.contains($0) }
        let moving = order.filter { block.contains($0) }
        guard let at = rest.firstIndex(of: anchor), !moving.isEmpty else { return [] }
        let p = up ? at : at + 1
        let target = Array(rest[0..<p]) + moving + Array(rest[p...])
        var current = order
        var out: [(id: Int64, index: Int)] = []
        func move(_ id: Int64, _ to: Int) {
            guard let from = current.firstIndex(of: id), from != to else { return }
            current.remove(at: from)
            current.insert(id, at: to)
            out.append((id: id, index: to))
        }
        // Subindo, de cima para baixo; descendo, de baixo para cima.
        let steps: [Int] = up ? Array(moving.indices) : Array(moving.indices.reversed())
        for j in steps { move(moving[j], p + j) }
        if current != target {
            // Rede de segurança: posição a posição.
            current = order
            out.removeAll()
            for k in target.indices where current[k] != target[k] { move(target[k], k) }
        }
        return out
    }
}

/**
 * Linhas feitas de novo SÓ onde a camada mudou. Cada revisão do modelo relê as
 * camadas do motor; aqui a linha velha volta quando tempo, estado, nome e a
 * lista de keyframes não mudaram. Arrastar um clipe entre 1000 refaz 1 linha.
 */
@MainActor final class TimelineRowCache {
    private struct Cached {
        let start: Int32
        let end: Int32
        let offset: Int32
        let visible: Bool
        let locked: Bool
        let animated: Bool
        let kind: UInt32
        let name: String
        let keys: [KeyframeItem]
        let row: TimelineRow

        func matches(_ l: LayerItem, _ keys: [KeyframeItem]) -> Bool {
            self.keys == keys && kind == l.kind && row.id == l.id &&
                start == l.startFrame && end == l.endFrame && offset == l.offsetFrames &&
                visible == l.visible && locked == l.locked && animated == l.animated &&
                name == l.name && row.label == l.label &&
                row.magnetic == l.magnetic && row.line == l.trackId
        }
    }

    private var sharedGeneration: UInt64 = .max
    private var sharedFocusID: Int64?
    private var sharedTracks: [TimelineTrack]?
    private var sharedResult: [TimelineRow] = []

    /// Uma fileira por LINHA, refeita só quando as linhas por camada mudam
    /// (mesma chave do `focused`: revisão das camadas + foco de trilhas).
    func shared(_ rows: [TimelineRow], focus: Int64?, tracks: [TimelineTrack]?) -> [TimelineRow] {
        let focusID: Int64? = tracks == nil ? nil : focus
        if sharedGeneration == generation && sharedFocusID == focusID && sharedTracks == tracks { return sharedResult }
        sharedGeneration = generation; sharedFocusID = focusID; sharedTracks = tracks
        sharedResult = timelineSharedRows(rows)
        // As trilhas abertas entram por baixo destas fileiras: refaz também.
        expandedRevision = .max
        return sharedResult
    }

    private var expandedRevision: UInt32 = .max
    private var expandedIDs: Set<Int64> = []
    private var expandedGroups: Set<TimelineLaneGroupKey> = []
    private var expandedResult: [TimelineRow] = []

    /// Trilhas abertas de VÁRIAS camadas (cada uma com o seu ▸/▾) e os grupos de eixos abertos.
    func expanded(_ base: [TimelineRow], ids: Set<Int64>, groups: Set<TimelineLaneGroupKey>, revision: UInt32, keys: [Int64: [KeyframeItem]], effects: (Int64) -> [EffectItem]) -> [TimelineRow] {
        guard !ids.isEmpty else { return base }
        if expandedIDs == ids && expandedGroups == groups && expandedRevision == revision { return expandedResult }
        expandedIDs = ids; expandedGroups = groups; expandedRevision = revision
        expandedResult = expandedTimelineRows(base, expanded: ids, openGroups: groups, keys: keys, effects: effects)
        return expandedResult
    }

    private var byId: [Int64: Cached] = [:]
    private var ordered: [Cached] = []
    private var last: [TimelineRow] = []
    private var generation: UInt64 = 0
    private var focusedGeneration: UInt64 = .max
    private var focusedID: Int64?
    private var focusedTracks: [TimelineTrack] = []
    private var focusedResult: [TimelineRow] = []

    func focused(_ base: [TimelineRow], id: Int64?, tracks: [TimelineTrack]?, layers: [LayerItem], keys: [Int64: [KeyframeItem]]) -> [TimelineRow] {
        guard let id, let tracks else {
            if focusedID != nil { focusedID = nil; expandedRevision = .max }
            return base
        }
        if focusedGeneration == generation && focusedID == id && focusedTracks == tracks { return focusedResult }
        focusedGeneration = generation; focusedID = id; focusedTracks = tracks
        focusedResult = base.map { row in
            guard row.id == id, let layer = layers.first(where: { $0.id == id }) else { return row }
            return buildTimelineRow(layer, (keys[id] ?? []).filter { key in
                tracks.contains { $0.property == Int(key.property) && $0.effect == key.effectIndex && $0.param == key.paramIndex }
            })
        }
        // Expanded rows also contain the base row, so a focus change invalidates them.
        expandedRevision = .max
        return focusedResult
    }

    func build(_ layers: [LayerItem], _ keyframes: [Int64: [KeyframeItem]]) -> [TimelineRow] {
        // Nada mudou (o caso comum: revisão que não mexeu na timeline): a MESMA
        // lista de antes, sem alocar.
        if layers.count == ordered.count {
            var same = true
            for i in 0..<layers.count {
                if i >= ordered.count || !ordered[i].matches(layers[i], keyframes[layers[i].id] ?? []) {
                    same = false
                    break
                }
            }
            if same { return last }
        }
        var next: [Int64: Cached] = [:]
        next.reserveCapacity(layers.count * 2)
        var byPos: [Cached] = []
        byPos.reserveCapacity(layers.count)
        var out: [TimelineRow] = []
        out.reserveCapacity(layers.count)
        for l in layers {
            let keys = keyframes[l.id] ?? []
            let old = byId[l.id]
            let c: Cached
            if let old, old.matches(l, keys) {
                c = old
            } else {
                c = Cached(start: l.startFrame, end: l.endFrame, offset: l.offsetFrames,
                           visible: l.visible, locked: l.locked, animated: l.animated,
                           kind: l.kind, name: l.name, keys: keys,
                           row: buildTimelineRow(l, keys))
            }
            next[l.id] = c
            byPos.append(c)
            out.append(c.row)
        }
        byId = next
        ordered = byPos
        last = out
        generation &+= 1
        return out
    }
}

// =============================================================================
// Miniaturas (TimelineState.kt, ThumbStrip)
// =============================================================================
/**
 * Miniaturas da tira, indexadas pelo balde de 250 ms da MÍDIA (não pelo frame
 * da timeline): mover ou aparar o clipe não troca a chave, então a tira não
 * pisca nem pede de novo ao motor. LRU limitado.
 */
@MainActor final class TimelineThumbStrip {
    private struct Key: Hashable {
        let layer: Int64
        let bucket: Int32
        let height: Int
    }

    private var hits: [Key: UIImage] = [:]
    private var order: [Key] = []
    private var misses: [Key: Int] = [:]
    private var aspects: [Int64: CGFloat] = [:]

    /// Largura/altura da miniatura da camada (16:9 até a primeira chegar). Pela
    /// DONA (`AureaModel.thumbOwner`): o pedaço de um corte usa a medida do original.
    func aspect(_ model: AureaModel, _ layer: Int64) -> CGFloat { aspects[model.thumbOwner(layer)] ?? 16.0 / 9.0 }

    /**
     * Perguntas ao motor que ainda cabem neste quadro. A que acerta cria uma
     * UIImage na thread da UI: rolando rápido, dezenas de baldes novos por
     * quadro viravam engasgo. O resto espera o próximo quadro (`starved`).
     */
    var budget = Int.max
    private(set) var starved = false

    func beginFrame(_ queries: Int) {
        budget = queries
        starved = false
    }

    func get(_ model: AureaModel, layer: Int64, bucket: Int32, timelineFrame: Int32, heightPx: Int, generation: UInt32) -> UIImage? {
        guard model.thumbnailWorkAllowed else { starved = true; return nil }
        // Os dois lados de um corte dividem a chave (mesma mídia, mesma origem).
        let owner = model.thumbOwner(layer)
        let key = Key(layer: owner, bucket: bucket, height: heightPx)
        if let hit = hits[key] { return hit }
        if let missed = misses[key], missed == Int(generation) { return nil }
        if budget <= 0 { starved = true; return nil }
        budget -= 1
        var w: UInt32 = 0
        guard let data = model.engine.thumbnail(forLayer: layer, frame: timelineFrame, height: UInt32(max(1, heightPx)), outWidth: &w),
              w > 0, heightPx > 0,
              let image = UIImage.fromRGBA(data, width: Int(w), height: heightPx) else {
            if misses.count > 512 { misses.removeAll() }
            misses[key] = Int(generation)
            return nil
        }
        misses.removeValue(forKey: key)
        if hits.count >= 240, let oldest = order.first {
            order.removeFirst()
            hits.removeValue(forKey: oldest)
        }
        order.append(key)
        hits[key] = image
        if aspects[owner] == nil, image.size.height > 0 {
            aspects[owner] = min(max(image.size.width / image.size.height, 0.3), 4)
        }
        return image
    }
}

// =============================================================================
// Waveform (TimelineState.kt, WaveStrip)
// =============================================================================
/**
 * Waveform da timeline guardada por linha em grade fixa do tempo, numa janela
 * maior que a tela. Antes cada linha de áudio/vídeo visível perguntava ao motor
 * A CADA QUADRO; agora pergunta quando a vista sai da janela, o degrau de zoom
 * muda ou o modelo muda.
 */
@MainActor final class TimelineWaveStrip {
    struct Entry {
        var generation: UInt32 = .max
        var fpb = 0.0
        var w0: Int64 = 0
        var w1: Int64 = 0
        var count = 0
        var data: [UInt8]

        func at(_ k: Int64) -> Int {
            guard k >= w0, k < w0 + Int64(count) else { return 0 }
            return Int(data[Int(k - w0)])
        }
    }

    private let capacity: Int
    private var entries: [Int64: Entry] = [:]
    private var order: [Int64] = []
    /// Perguntas feitas ao motor (telemetria).
    private(set) var queries = 0

    init(capacity: Int) { self.capacity = capacity }

    /// A entrada da camada com `[first, last]` coberto, pedindo ao motor só se preciso; nil = sem som.
    func get(
        _ model: AureaModel, layer: Int64, generation: UInt32,
        fpb: Double, first: Int64, last: Int64
    ) -> Entry? {
        guard last >= first, last - first + 1 <= Int64(capacity) else { return nil }
        if let e = entries[layer], e.generation == generation, e.fpb == fpb, first >= e.w0, last < e.w1 {
            return e.count > 0 ? e : nil
        }
        let win = WaveGrid.window(first: first, last: last, max: capacity)
        let n = Int(win.1 - win.0)
        queries += 1
        let got = model.engine.waveform(forLayer: layer, startFrame: Double(win.0) * fpb,
                                        framesPerBucket: fpb, count: UInt32(max(0, n)))
        var e = entries[layer] ?? Entry(data: [UInt8](repeating: 0, count: capacity))
        e.generation = generation
        e.fpb = fpb
        e.w0 = win.0
        e.w1 = win.1
        let bytes = got.map { [UInt8]($0.prefix(capacity)) } ?? []
        e.count = min(bytes.count, n)
        if e.count > 0 { e.data.replaceSubrange(0..<e.count, with: bytes[0..<e.count]) }
        if entries[layer] == nil {
            if order.count >= 32, let oldest = order.first {
                order.removeFirst()
                entries.removeValue(forKey: oldest)
            }
            order.append(layer)
        }
        entries[layer] = e
        return e.count > 0 ? e : nil
    }
}

/// Uma trilha real do motor, uma seção (property −1) sem keyframes sintéticos,
/// ou — com `group` — a TRILHA DE GRUPO de uma propriedade de vários eixos
/// (Posição X/Y/Z, Escala, Rotação, Âncora...): property/param são os do 1º eixo
/// e a trilha junta os keyframes de todos. Só vista e gesto (par do Android).
struct TimelineTrack: Hashable {
    let property: Int
    var effect: UInt32 = .max
    var param: UInt32 = 0
    var group: Bool = false
}

/// O grupo de eixos de uma trilha (a trilha-base, com `group`), ou nil quando a
/// propriedade é de um componente só (par do `trackGroup` do Android).
func timelineTrackGroup(_ t: TimelineTrack) -> TimelineTrack? {
    if t.group { return t }
    if (0...11).contains(t.property) { return TimelineTrack(property: t.property / 3 * 3, effect: t.effect, param: t.param, group: true) }
    if t.property == 13 || t.property == 14 { return TimelineTrack(property: 13, effect: t.effect, param: t.param, group: true) }
    if t.property == 42 && t.param <= 8 { return TimelineTrack(property: 42, effect: t.effect, param: t.param / 3 * 3, group: true) }
    return nil
}

/// Um grupo de eixos aberto (▾) numa camada.
struct TimelineLaneGroupKey: Hashable {
    let layer: Int64
    let track: TimelineTrack
}

// Nomes das trilhas no idioma do app (as mesmas chaves tl_* do Android).
private func timelineAxisName(_ property: Int) -> String? {
    let bases = ["tl_position", "tl_scale", "tl_rotation", "tl_anchor"], axes = ["X", "Y", "Z"]
    switch property {
    case 0...11: return AureaText.t(bases[property / 3]) + " " + axes[property % 3]
    case 12: return AureaText.t("tl_opacity")
    case 13, 14: return AureaText.t("tl_skew") + " " + axes[property - 13]
    default: return nil
    }
}

private func timelinePartAxisName(_ param: Int) -> String? {
    let bases = ["tl_position", "tl_rotation", "tl_scale"], axes = ["X", "Y", "Z"]
    guard param >= 0, param / 3 < bases.count else { return nil }
    return AureaText.t(bases[param / 3]) + " " + axes[param % 3]
}

private func timelineTrackName(_ track: TimelineTrack, _ effects: [EffectItem]) -> String {
    let effect = Int(UInt64(track.effect) &+ 1), param = Int(track.param)
    if track.group {
        switch track.property {
        case 0: return AureaText.t("tl_position")
        case 3: return AureaText.t("tl_scale")
        case 6: return AureaText.t("tl_rotation")
        case 9: return AureaText.t("tl_anchor")
        case 13: return AureaText.t("tl_skew")
        case 42:
            let parts = ["tl_position", "tl_rotation", "tl_scale"]
            let i = param / 3
            return AureaText.t("tl_part", effect, i < parts.count ? AureaText.t(parts[i]) : String(track.param))
        default: return AureaText.t("tl_3d", track.property)
        }
    }
    if let axis = timelineAxisName(track.property) { return axis }
    switch track.property {
    case 30: return AureaText.t("tl_time_remap")
    case 39: return AureaText.t("tl_speed")
    case 40: return AureaText.t("tl_animator", effect, param + 1)
    case 31: return (effects.first { $0.effectId == track.effect }?.name ?? AureaText.t("tl_effect")) + " · \(UInt64(track.param) + 1)"
    case 32: return AureaText.t("tl_audio", param + 1)
    case 33: return AureaText.t("tl_text_anim", effect, param + 1)
    case 43:
        let labels = ["tl_feather", "tl_expansion", "tl_opacity"]
        return AureaText.t("tl_mask", effect, param < labels.count ? AureaText.t(labels[param]) : String(track.param))
    case 34: return AureaText.t("tl_vector", param + 1)
    case 35: return AureaText.t("tl_shape", param + 1)
    case 36: return AureaText.t("tl_particles", param + 1)
    case 37:
        let labels = ["R", "G", "B", AureaText.t("tl_alpha"), AureaText.t("tl_metallic"), AureaText.t("tl_roughness")]
        return AureaText.t("tl_material", effect, param < labels.count ? labels[param] : String(track.param))
    case 42:
        return AureaText.t("tl_part", effect, timelinePartAxisName(param) ?? String(track.param))
    default: return AureaText.t("tl_3d", track.property)
    }
}

/// Compatível com a versão de UMA camada aberta.
func expandedTimelineRows(_ base: [TimelineRow], expanded: Int64?, keys: [Int64: [KeyframeItem]], effects: [EffectItem]) -> [TimelineRow] {
    guard let expanded else { return base }
    return expandedTimelineRows(base, expanded: [expanded], openGroups: [], keys: keys, effects: { _ in effects })
}

/// Trilhas abertas de VÁRIAS camadas (par do `expandedRows` do Android): uma
/// propriedade de vários eixos com 2+ eixos animados vira UMA trilha de grupo
/// (um losango por instante, a união dos eixos); o grupo aberto mostra as
/// trilhas por eixo logo abaixo. Um eixo sozinho continua sendo a trilha dele.
func expandedTimelineRows(_ base: [TimelineRow], expanded: Set<Int64>, openGroups: Set<TimelineLaneGroupKey>, keys: [Int64: [KeyframeItem]], effects: (Int64) -> [EffectItem]) -> [TimelineRow] {
    if expanded.isEmpty { return base }
    return base.flatMap { row -> [TimelineRow] in
        // Numa fileira compartilhada, as trilhas abertas são do TRECHO aberto
        // (tempo e keyframes dele) e entram logo abaixo da fileira.
        guard let owner = row.segments.first(where: { expanded.contains($0.id) }) else { return [row] }
        let fx = effects(owner.id)
        var lanes = [row]
        func lane(_ track: TimelineTrack, _ name: String, _ values: [KeyframeItem] = []) {
            let groups = Dictionary(grouping: values, by: { $0.time })
            let times = groups.keys.sorted()
            lanes.append(TimelineRow(id: owner.id, type: owner.type, start: owner.start, end: owner.end, offset: owner.offset,
                visible: owner.visible, locked: owner.locked, magnetic: owner.magnetic, animated: !values.isEmpty, name: name, label: owner.label,
                instants: times.map { Keyframes.toTimeline($0, owner.start, owner.offset) }, keysAt: times.map { groups[$0]! }, track: track))
        }
        lane(TimelineTrack(property: -1), "  " + AureaText.t("panel_transformar"))
        for effect in fx { lane(TimelineTrack(property: 31, effect: effect.effectId, param: .max), "  " + fxEffectDisplayName(effect.typeId, effect.name)) }
        let tracks = Dictionary(grouping: keys[owner.id] ?? [], by: { TimelineTrack(property: Int($0.property), effect: $0.effectIndex, param: $0.paramIndex) })
        let ordered = tracks.keys.sorted {
            if $0.property != $1.property { return $0.property < $1.property }
            if $0.effect != $1.effect { return $0.effect < $1.effect }
            return $0.param < $1.param
        }
        // Os eixos animados de cada grupo (só 2+ vira trilha de grupo).
        var members: [TimelineTrack: [TimelineTrack]] = [:]
        for track in ordered { if let g = timelineTrackGroup(track) { members[g, default: []].append(track) } }
        var emitted = Set<TimelineTrack>()
        for track in ordered {
            guard let group = timelineTrackGroup(track), let axes = members[group], axes.count >= 2 else {
                lane(track, "  " + timelineTrackName(track, fx), tracks[track] ?? [])
                continue
            }
            if !emitted.insert(group).inserted { continue }
            lane(group, "  " + timelineTrackName(group, fx), axes.flatMap { tracks[$0] ?? [] })
            if openGroups.contains(TimelineLaneGroupKey(layer: owner.id, track: group)) {
                for axis in axes { lane(axis, "      " + timelineTrackName(axis, fx), tracks[axis] ?? []) }
            }
        }
        return lanes
    }
}

/// Altura de uma trilha de propriedade: baixa (16 pt), para caberem muitas.
let timelineLaneHeight: CGFloat = 16

/// Folga vertical de toque das trilhas baixas (par do `LaneTouch` do Android):
/// a fileira sob o dedo e depois a trilha vizinha cujo centro está a até
/// `reach` do dedo (a mais perto primeiro) — alvo de cada losango ≥ 32 pt.
enum TimelineLaneTouch {
    static func order(isLane: (Int) -> Bool, tops: [CGFloat], index: Int, y: CGFloat, reach: CGFloat) -> [Int] {
        guard index >= 0, index + 1 < tops.count else { return [index] }
        var near: [(Int, CGFloat)] = []
        for j in [index - 1, index + 1] where j >= 0 && j + 1 < tops.count && isLane(j) {
            let d = abs((tops[j] + tops[j + 1]) / 2 - y)
            if d <= reach { near.append((j, d)) }
        }
        // Estável no empate (a de cima primeiro), como o Android.
        if near.count == 2 && near[1].1 < near[0].1 { near.swapAt(0, 1) }
        return [index] + near.map { $0.0 }
    }
}

/// Seleção por RETÂNGULO no modo "Selecionar" (par do `BoxSelect` do Android):
/// os keyframes cujos losangos têm o centro dentro de [frameLo, frameHi] ×
/// [yLo, yHi] (tempo da timeline e y de conteúdo), por camada.
enum TimelineBoxSelect {
    static func pick(rows: [TimelineRow], tops: [CGFloat], keyCy: (TimelineRow) -> CGFloat,
                     frameLo: Double, frameHi: Double, yLo: CGFloat, yHi: CGFloat,
                     keysVisible: (TimelineRow) -> Bool) -> [Int64: [KeyframeItem]] {
        var out: [Int64: [KeyframeItem]] = [:]
        let lo = (min(frameLo, frameHi) - 1e-9).rounded(.up)
        let hi = (max(frameLo, frameHi) + 1e-9).rounded(.down)
        let top = min(yLo, yHi), bottom = max(yLo, yHi)
        for (i, row) in rows.enumerated() where i < tops.count {
            let cy = tops[i] + keyCy(row)
            if cy < top || cy > bottom { continue }
            for segment in row.segments where keysVisible(segment) {
                for (k, t) in segment.instants.enumerated() where Double(t) >= lo && Double(t) <= hi {
                    var list = out[segment.id] ?? []
                    for key in segment.keysAt[k] where !list.contains(where: { TimelineKeyRef($0) == TimelineKeyRef(key) }) { list.append(key) }
                    out[segment.id] = list
                }
            }
        }
        return out
    }
}

// =============================================================================
// Seleção de keyframes da timeline (KeySelection.kt)
// =============================================================================
/// Um keyframe pela TRILHA (propriedade, efeito, componente) e tempo LOCAL da
/// camada — sem valor nem curva: a chave que o motor entende em
/// `keyframeSelection` (4 números por keyframe).
struct TimelineKeyRef: Hashable {
    let property: UInt32
    let effect: UInt32
    let param: UInt32
    let time: Int32

    func matches(_ key: KeyframeItem) -> Bool {
        key.property == property && key.effectIndex == effect && key.paramIndex == param && key.time == time
    }
}

extension TimelineKeyRef {
    init(_ key: KeyframeItem) {
        self.init(property: key.property, effect: key.effectIndex, param: key.paramIndex, time: key.time)
    }
}

/// UMA camada e um conjunto de keyframes de QUALQUER trilha dela. Mesmo
/// contrato do Android (`KeySelection`): trilha alterna 1 keyframe; resumo
/// alterna o instante inteiro; mover desloca todas as referências pelo mesmo
/// delta; se alguma referência sumiu dos keyframes relidos, a seleção inteira
/// é descartada.
struct TimelineKeySelection: Equatable {
    let layer: Int64
    var keys: Set<TimelineKeyRef> = []
    /// Keyframes escolhidos em OUTRAS camadas (do app antigo: no modo
    /// "Selecionar" losangos de outra fileira somam). `layer` continua a
    /// principal (Copiar/Colar/Duplicar); mover e excluir valem para todas.
    var others: [Int64: Set<TimelineKeyRef>] = [:]

    var count: Int { others.values.reduce(keys.count) { $0 + $1.count } }
    var isEmpty: Bool { count == 0 }
    var crossLayer: Bool { !others.isEmpty }

    /// As referências de uma camada (vazio se ela não tem nada escolhido).
    func on(_ id: Int64) -> Set<TimelineKeyRef> { id == layer ? keys : (others[id] ?? []) }

    /// Camadas com algum keyframe escolhido (a principal primeiro, as outras em ordem de id).
    var layerIds: [Int64] {
        var out: [Int64] = keys.isEmpty ? [] : [layer]
        for id in others.keys.sorted() where !(others[id] ?? []).isEmpty { out.append(id) }
        return out
    }

    func containsAny(on id: Int64, _ group: [KeyframeItem]) -> Bool {
        let refs = on(id)
        if refs.isEmpty { return false }
        for key in group where refs.contains(TimelineKeyRef(key)) { return true }
        return false
    }

    /// Alterna o grupo na camada dada: na principal ou numa das outras.
    func toggledGroup(on id: Int64, _ group: [KeyframeItem]) -> TimelineKeySelection {
        if id == layer { return toggledGroup(group) }
        if group.isEmpty { return self }
        var refs = Set<TimelineKeyRef>()
        for key in group { refs.insert(TimelineKeyRef(key)) }
        var current = others[id] ?? []
        if refs.isSubset(of: current) { current.subtract(refs) } else { current.formUnion(refs) }
        var next = self
        next.others[id] = current.isEmpty ? nil : current
        return next
    }

    /// Quanto a seleção inteira pode andar para cada camada ficar no próprio
    /// clipe; `clip` dá (início, fim, deslocamento) de cada camada. O mais
    /// restritivo de todas (par do `shiftLimits` do Android).
    func shiftLimits(_ clip: (Int64) -> (start: Int32, end: Int32, offset: Int32)?) -> (lo: Int32, hi: Int32) {
        var lo = Int64.min
        var hi = Int64.max
        for id in layerIds {
            guard let c = clip(id) else { continue }
            let refs = on(id)
            let minT = refs.map(\.time).min() ?? 0
            let maxT = refs.map(\.time).max() ?? 0
            let first = Int64(Keyframes.toTimeline(minT, c.start, c.offset))
            let last = Int64(Keyframes.toTimeline(maxT, c.start, c.offset))
            lo = max(lo, min(0, Int64(c.start) - first))
            hi = min(hi, max(0, Int64(c.end) - last))
        }
        return (lo == Int64.min ? 0 : Int32(clamping: lo), hi == Int64.max ? 0 : Int32(clamping: hi))
    }

    /// Todas as camadas continuam válidas? `current` nil = a camada sumiu.
    func validatedAll(_ current: (Int64) -> [KeyframeItem]?) -> TimelineKeySelection? {
        for id in layerIds {
            guard let rows = current(id) else { return nil }
            var present = Set<TimelineKeyRef>()
            for key in rows { present.insert(TimelineKeyRef(key)) }
            if !on(id).isSubset(of: present) { return nil }
        }
        return self
    }

    /// Empacotado para o motor, só das referências de uma camada.
    func references(on id: Int64) -> [NSNumber] { Self.pack(on(id)) }

    func contains(_ key: KeyframeItem) -> Bool { keys.contains(TimelineKeyRef(key)) }

    func containsAny(_ group: [KeyframeItem]) -> Bool {
        for key in group where keys.contains(TimelineKeyRef(key)) { return true }
        return false
    }

    /// Entra tudo o que falta do grupo; se já estava todo escolhido, sai todo.
    func toggledGroup(_ group: [KeyframeItem]) -> TimelineKeySelection {
        if group.isEmpty { return self }
        var refs = Set<TimelineKeyRef>()
        for key in group { refs.insert(TimelineKeyRef(key)) }
        var next = self
        if refs.isSubset(of: keys) { next.keys.subtract(refs) } else { next.keys.formUnion(refs) }
        return next
    }

    /// SOMA (sem alternar) os keyframes de cada camada — a seleção por
    /// retângulo; a principal fica a mesma (par do `plusAll` do Android).
    func plusAll(_ picked: [Int64: [KeyframeItem]]) -> TimelineKeySelection {
        if picked.isEmpty { return self }
        var next = self
        for (id, group) in picked where !group.isEmpty {
            var refs = Set<TimelineKeyRef>()
            for key in group { refs.insert(TimelineKeyRef(key)) }
            if id == layer { next.keys.formUnion(refs) }
            else { next.others[id, default: []].formUnion(refs) }
        }
        return next
    }

    /// Todas as referências andam `delta` frames (depois que o motor aceitou o mesmo delta).
    func shifted(_ delta: Int32) -> TimelineKeySelection {
        if delta == 0 { return self }
        func move(_ refs: Set<TimelineKeyRef>) -> Set<TimelineKeyRef> {
            var moved = Set<TimelineKeyRef>()
            for ref in refs {
                let time = Int32(clamping: Int64(ref.time) + Int64(delta))
                moved.insert(TimelineKeyRef(property: ref.property, effect: ref.effect, param: ref.param, time: time))
            }
            return moved
        }
        return TimelineKeySelection(layer: layer, keys: move(keys), others: others.mapValues(move))
    }

    /// Continua valendo só se TODA referência ainda existe; senão nil.
    func validated(_ current: [KeyframeItem]) -> TimelineKeySelection? {
        if keys.isEmpty { return self }
        var present = Set<TimelineKeyRef>()
        for key in current { present.insert(TimelineKeyRef(key)) }
        return keys.isSubset(of: present) ? self : nil
    }

    var minTime: Int32 {
        var result = Int32.max
        for ref in keys where ref.time < result { result = ref.time }
        return keys.isEmpty ? 0 : result
    }

    var maxTime: Int32 {
        var result = Int32.min
        for ref in keys where ref.time > result { result = ref.time }
        return keys.isEmpty ? 0 : result
    }

    /// Empacotado para o motor: propriedade, efeito, componente, tempo local.
    func references() -> [NSNumber] { Self.pack(keys) }

    private static func pack(_ refs: Set<TimelineKeyRef>) -> [NSNumber] {
        var out: [NSNumber] = []
        out.reserveCapacity(refs.count * 4)
        for ref in refs {
            out.append(NSNumber(value: ref.property))
            out.append(NSNumber(value: ref.effect))
            out.append(NSNumber(value: ref.param))
            out.append(NSNumber(value: Int64(ref.time)))
        }
        return out
    }

    /// "Duplicar": a cópia começa 1 frame depois do ÚLTIMO escolhido (o motor
    /// cola ancorado no mais cedo). nil se estoura o Int32 do motor.
    func duplicateDelta() -> Int32? {
        if keys.isEmpty { return nil }
        let delta: Int64 = Int64(maxTime) + 1 - Int64(minTime)
        let last: Int64 = Int64(maxTime) + delta
        if last > Int64(Int32.max) { return nil }
        return Int32(delta)
    }

    static func single(_ layer: Int64, _ key: KeyframeItem) -> TimelineKeySelection {
        TimelineKeySelection(layer: layer, keys: [TimelineKeyRef(key)])
    }

    /// "Todos": todos os keyframes que a timeline mostra da camada (respeita o foco de trilhas).
    static func all(_ layer: Int64, _ keys: [KeyframeItem], focus: [TimelineTrack]?) -> TimelineKeySelection {
        var refs = Set<TimelineKeyRef>()
        for key in keys {
            if let focus {
                let track = TimelineTrack(property: Int(key.property), effect: key.effectIndex, param: key.paramIndex)
                if !focus.contains(track) { continue }
            }
            refs.insert(TimelineKeyRef(key))
        }
        return TimelineKeySelection(layer: layer, keys: refs)
    }
}

/// Quem mostra (e deixa tocar) os losangos de keyframe na timeline — o mesmo
/// do Android (`KeyframeVisibility`): trilha aberta sempre; a linha da camada,
/// com "Keyframes de todas as camadas" ligado ou quando ela está escolhida.
enum KeyframeVisibility {
    static func visible(showAll: Bool, isPropertyLane: Bool, selected: Bool) -> Bool {
        return showAll || isPropertyLane || selected
    }
}

/// O que o dedo QUIS, decidido uma vez por gesto — o mesmo do Android (`Press`).
/// Rolar e fazer scrub não custam nada e ficam nos 45°; EDITAR o projeto (mover
/// clipe, aparar, arrastar losango, reordenar) exige eixo claro, 2:1 — o empate
/// de 45° classificava uma rolagem um pouco torta como "mover clipe", e a camada
/// era escolhida e ia junto com o dedo.
/// Contas puras do arrasto de losango (par do `KeyDrag.kt`). Beta "é difícil
/// mover o keyframe": o frame só troca depois de meio frame + uma histerese
/// pequena (sem tremer na fronteira; some de longe, quando um px vale vários
/// frames), e o cabeçote parado no instante de ORIGEM não é ímã — tocar no
/// losango leva o cabeçote até ele, e o ímã o segurava nos primeiros 8 pt.
enum KeyDrag {
    static func quantize(_ desired: Double, current: Int32, pxPerFrame: CGFloat, hysteresisPx: CGFloat) -> Int32 {
        guard desired.isFinite else { return current }
        let extra: Double = pxPerFrame > 0 && pxPerFrame.isFinite ? min(0.25, Double(hysteresisPx / pxPerFrame)) : 0
        if abs(desired - Double(current)) <= 0.5 + extra { return current }
        return Int32(max(-2147483648.0, min(2147483647.0, (desired + 0.5).rounded(.down))))
    }
    static func playheadMagnet(_ playhead: Int32, origin: Int32) -> Int32 { playhead == origin ? Snap.none : playhead }
}

enum TimelinePress {
    static let editRatio: CGFloat = 2
    static func horizontal(_ dx: CGFloat, _ dy: CGFloat) -> Bool { abs(dx) >= abs(dy) }
    /// Rolar a pilha ganha cedo: |dy| ≥ 0,6·|dx| (≈ 31° da horizontal) — o dedo
    /// que sobe um pouco torto quer ver as outras camadas. Só na lista inteira;
    /// na fileira compacta vale o empate de 45° (`horizontal`). Par do
    /// `Press.scrollWins` do Android.
    static let scrollRatio: CGFloat = 0.6
    static func scrollWins(_ dx: CGFloat, _ dy: CGFloat) -> Bool { dy != 0 && abs(dy) >= scrollRatio * abs(dx) }
    /// Claramente no eixo do tempo: mover, aparar, arrastar losango.
    static func timeEdit(_ dx: CGFloat, _ dy: CGFloat) -> Bool { abs(dx) >= editRatio * abs(dy) }
    /// Claramente na pilha: reordenar.
    static func stackEdit(_ dx: CGFloat, _ dy: CGFloat) -> Bool { abs(dy) >= editRatio * abs(dx) }
    /// Fileira compacta: o arrasto vertical passa pelas camadas como uma roda —
    /// cada altura de linha é uma camada; `travel` = quanto o dedo SUBIU
    /// (positivo = a de baixo). Par do `Press.compactSteps` do Android.
    static func compactSteps(_ travel: CGFloat, row: CGFloat) -> Int {
        guard travel.isFinite, row.isFinite, row > 0 else { return 0 }
        return Int(travel / row)
    }
}

/// A timeline vira a fileira única da camada? (par do `timelineCompact` do
/// Android). Painel aberto: sempre. Doca aberta: só sem trilhas de propriedade
/// abertas e fora do modo de escolher keyframes — senão fica inteira, para
/// dar para mexer nas trilhas e nos losangos.
func timelineCompact(panel: Bool, dock: Bool, tracksOpen: Bool, selectingKeys: Bool) -> Bool {
    if panel { return true }
    return dock && !tracksOpen && !selectingKeys
}

/// O que um toque no corpo de um clipe faz com a seleção de camadas (par do
/// `layerTap` do Android). Modo "Selecionar várias camadas" (do app antigo):
/// soma/tira, antes de tudo. Fora dele: compacto = sai do painel (noutro pedaço
/// da linha, troca); lote de 2+
/// = soma/tira; senão troca a escolhida.
/// Tocar de novo na única escolhida (com as opções abertas) a solta, como no
/// app antigo; escolhida só "na mão" da timeline (`timelineOnly`), o toque abre
/// as opções. Outra camada troca direto.
enum TimelineLayerTap: Equatable { case leaveCompact, toggle, replace, deselect }

func timelineLayerTap(picking: Bool, compact: Bool, selected: Int,
                      tappedSelected: Bool = false, timelineOnly: Bool = false) -> TimelineLayerTap {
    if picking { return .toggle }
    // Na fileira compacta os outros pedaços da mesma linha aparecem: tocar num
    // deles troca a escolhida; tocar na própria sai do painel.
    if compact { return tappedSelected ? .leaveCompact : .replace }
    if selected >= 2 { return .toggle }
    if selected == 1 && tappedSelected && !timelineOnly { return .deselect }
    return .replace
}

/// "Escalonar": a conta da UI antes do motor (par do `Stagger` do Android).
enum StaggerPlan {
    static let minStep: Int = -120
    static let maxStep: Int = 120
    static let defaultStep: Int = 3

    /// Próximo valor do contador: anda `delta`, nunca para no 0 e fica na faixa.
    static func step(_ current: Int, _ delta: Int) -> Int {
        var next: Int = min(max(current + delta, minStep), maxStep)
        if next == 0 { next = delta > 0 ? 1 : -1 }
        return next
    }
}
