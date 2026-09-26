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

    // --- Régua e linhas -------------------------------------------------------
    var rulerTicks: CGFloat { dp(20) }
    /// Topo da 1ª linha: 20 de riscos + 18 de respiro (o relógio mora no respiro).
    var rowsTop: CGFloat { dp(20 + 18) }
    var row: CGFloat { dp(36) }
    var bar: CGFloat { dp(30) }
    var barRadius: CGFloat { dp(8) }
    var barMinWidth: CGFloat { dp(40) }
    var track: CGFloat { dp(11) }
    /// Começo da faixa dos losangos (medido do topo da barra).
    var trackTop: CGFloat { bar - track }
    /// Folga abaixo das linhas: o "+" da casca cobre a ponta de baixo.
    var bottomPad: CGFloat { dp(56) }

    // --- Coluna das pílulas (olho + quadradinho) -------------------------------
    var headerColumn: CGFloat { dp(66) }
    var pillLeft: CGFloat { dp(4) }
    var pillWidth: CGFloat { dp(58) }
    var pillHeight: CGFloat { dp(24) }
    var pillRadius: CGFloat { pillHeight / 2 }
    var eyeSlot: CGFloat { dp(26) }
    let eyeGlyph: CGFloat = 16
    var swatch: CGFloat { dp(18) }
    var swatchRadius: CGFloat { dp(4) }
    /// `spaceEvenly` do Row da A.01: três vãos iguais entre olho (26) e quadradinho (18).
    private var pillGap: CGFloat { (pillWidth - eyeSlot - swatch) / 3 }
    var eyeCenterX: CGFloat { pillLeft + pillGap + eyeSlot / 2 }
    var swatchLeft: CGFloat { pillLeft + pillGap * 2 + eyeSlot }
    /// O olho responde até o meio do vão que o separa do quadradinho.
    var eyeHitRight: CGFloat { pillLeft + pillGap * 1.5 + eyeSlot }

    // --- Régua ------------------------------------------------------------------
    var tickMajorTop: CGFloat { dp(2) }
    var tickMinorTop: CGFloat { dp(9) }
    var tickBottom: CGFloat { dp(18) }
    var tickMajorWidth: CGFloat { dp(1.4) }
    var tickMinorWidth: CGFloat { dp(1) }
    var tickLabelGap: CGFloat { dp(3) }

    // --- Conteúdo da barra --------------------------------------------------------
    var stripe: CGFloat { dp(4) }
    var padL: CGFloat { dp(14) }
    var padR: CGFloat { dp(10) }
    var padLNarrow: CGFloat { dp(7) }
    var padRNarrow: CGFloat { dp(3) }
    var narrowBar: CGFloat { dp(46) }
    let typeIcon: CGFloat = 11
    var iconGap: CGFloat { dp(6) }
    let lockIcon: CGFloat = 10
    var lockGap: CGFloat { dp(5) }
    let rhombusIcon: CGFloat = 12
    var rhombusGap: CGFloat { dp(6) }
    var arrowSlot: CGFloat { dp(22) }
    let arrowGlyph: CGFloat = 14
    let menuGlyph: CGFloat = 14
    var iconMinBar: CGFloat { dp(28) }
    var nameMinBar: CGFloat { dp(52) }
    var lockGapMinBar: CGFloat { dp(70) }
    var rhombusMinBar: CGFloat { dp(120) }
    var menuMinBar: CGFloat { dp(150) }
    var selStroke: CGFloat { dp(1.5) }
    var multiStroke: CGFloat { dp(1.2) }
    var lightLine: CGFloat { dp(1) }
    /// Toque do corpo vai um pouco abaixo da barra (os 10 dp que sobram na linha são do vazio).
    var bodyHitBottom: CGFloat { bar + dp(4) }
    var arrowTouchPad: CGFloat { dp(6) }

    // --- Alça de trim (A.01: 16 × (36 − 4), top 2, DENTRO das pontas) --------------
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
    var diamond: CGFloat { dp(11) }
    var diamondRadius: CGFloat { dp(2) }
    var diamondStroke: CGFloat { dp(1.2) }
    /// Centro do losango: A.01 `top 20, altura 17` (normal) e `top 23, altura 16` (compacto).
    var diamondCyNormal: CGFloat { dp(28.5) }
    var diamondCyCompact: CGFloat { dp(31) }
    var keyTouchHalf: CGFloat { dp(14) }
    var keyGlyphHalf: CGFloat { dp(7) }
    var keyTouchTop: CGFloat { trackTop - dp(2) }
    var keyMergeGap: CGFloat { dp(4) }
    var keyPillHeight: CGFloat { dp(10) }
    var keyPillMinWidth: CGFloat { dp(16) }
    let keyDragScale: CGFloat = 1.4
    var balloonPadH: CGFloat { dp(5) }
    var balloonPadV: CGFloat { dp(2) }
    var balloonRadius: CGFloat { dp(4) }
    var balloonGap: CGFloat { dp(3) }

    // --- Cabeçote e relógio ----------------------------------------------------------
    var playhead: CGFloat { dp(1.6) }
    var knob: CGFloat { dp(8) }
    var knobRadius: CGFloat { dp(2) }
    var timecodeBaseline: CGFloat { dp(21) }
    var underlineTop: CGFloat { dp(28.2) }
    var underlineWidth: CGFloat { dp(58) }
    var underlineHeight: CGFloat { dp(1.5) }
    /// Rótulo da régua perto do relógio some (não disputa leitura com ele).
    var timecodeZoneHalf: CGFloat { dp(36) }

    // --- Gestos -------------------------------------------------------------------------
    var snapClip: CGFloat { dp(12) }
    var snapKey: CGFloat { dp(8) }
    var autoEdge: CGFloat { dp(38) }
    var autoSpeed: CGFloat { dp(120) }
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
    let animated: Bool
    let name: String
    /// Etiqueta de cor (0 = nenhuma; i = `labelPalette[i - 1]`).
    let label: UInt32
    /// Instantes com keyframe (qualquer trilha), em frames da TIMELINE, ordenados e sem repetição.
    let instants: [Int32]
    /// Keyframes de cada instante (todas as trilhas que têm marca ali), paralelo a `instants`.
    let keysAt: [[KeyframeItem]]
    var track: TimelineTrack? = nil

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
                       animated: l.animated || !keys.isEmpty,
                       name: l.name,
                       label: l.label,
                       instants: instants,
                       keysAt: groups)
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
                name == l.name && row.label == l.label
        }
    }

    private var expandedRevision: UInt32 = .max
    private var expandedID: Int64?
    private var expandedEffects: [EffectItem] = []
    private var expandedResult: [TimelineRow] = []

    func expanded(_ base: [TimelineRow], id: Int64?, revision: UInt32, keys: [Int64: [KeyframeItem]], effects: () -> [EffectItem]) -> [TimelineRow] {
        guard id != nil else { return base }
        if expandedID == id && expandedRevision == revision { return expandedResult }
        expandedID = id; expandedRevision = revision; expandedEffects = effects()
        expandedResult = expandedTimelineRows(base, expanded: id, keys: keys, effects: expandedEffects)
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

    /// Largura/altura da miniatura da camada (16:9 até a primeira chegar).
    func aspect(_ layer: Int64) -> CGFloat { aspects[layer] ?? 16.0 / 9.0 }

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
        let key = Key(layer: layer, bucket: bucket, height: heightPx)
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
        if aspects[layer] == nil, image.size.height > 0 {
            aspects[layer] = min(max(image.size.width / image.size.height, 0.3), 4)
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

struct TimelineTrack: Hashable {
    let property: Int
    var effect: UInt32 = .max
    var param: UInt32 = 0
}
func expandedTimelineRows(_ base: [TimelineRow], expanded: Int64?, keys: [Int64: [KeyframeItem]], effects: [EffectItem]) -> [TimelineRow] {
    guard let expanded else { return base }
    return base.flatMap { row -> [TimelineRow] in
        guard row.id == expanded else { return [row] }
        var lanes = [row]
        func lane(_ track: TimelineTrack, _ name: String, _ values: [KeyframeItem] = []) {
            let groups = Dictionary(grouping: values, by: { $0.time })
            let times = groups.keys.sorted()
            lanes.append(TimelineRow(id: row.id, type: row.type, start: row.start, end: row.end, offset: row.offset,
                visible: row.visible, locked: row.locked, animated: !values.isEmpty, name: "  " + name, label: row.label,
                instants: times.map { Keyframes.toTimeline($0, row.start, row.offset) }, keysAt: times.map { groups[$0]! }, track: track))
        }
        lane(TimelineTrack(property: -1), AureaText.t("panel_transformar"))
        for effect in effects { lane(TimelineTrack(property: 31, effect: effect.effectId, param: .max), fxEffectDisplayName(effect.typeId, effect.name)) }
        let tracks = Dictionary(grouping: keys[row.id] ?? [], by: { TimelineTrack(property: Int($0.property), effect: $0.effectIndex, param: $0.paramIndex) })
        let ordered = tracks.keys.sorted {
            if $0.property != $1.property { return $0.property < $1.property }
            if $0.effect != $1.effect { return $0.effect < $1.effect }
            return $0.param < $1.param
        }
        let names = ["Position X", "Position Y", "Position Z", "Scale X", "Scale Y", "Scale Z", "Rotation X", "Rotation Y", "Rotation Z", "Anchor X", "Anchor Y", "Anchor Z", "Opacity", "Skew X", "Skew Y"]
        for track in ordered {
            let name: String
            if names.indices.contains(track.property) { name = names[track.property] }
            else {
                switch track.property {
                case 30: name = "Time remap"
                case 31: name = (effects.first { $0.effectId == track.effect }?.name ?? "Effect") + " · \(UInt64(track.param) + 1)"
                case 32: name = "Audio · \(UInt64(track.param) + 1)"
                case 33: name = "Text animation \(UInt64(track.effect) + 1) · \(UInt64(track.param) + 1)"
                case 34: name = "Vector · \(UInt64(track.param) + 1)"
                case 35: name = "Shape · \(UInt64(track.param) + 1)"
                case 36: name = "Particles · \(UInt64(track.param) + 1)"
                case 37:
                    let labels = ["R", "G", "B", "Alpha", "Metallic", "Roughness"]
                    name = "Material \(UInt64(track.effect) + 1) · \(Int(track.param) < labels.count ? labels[Int(track.param)] : String(track.param))"
                default: name = "3D · \(track.property)"
                }
            }
            lane(track, name, tracks[track] ?? [])
        }
        return lanes
    }
}
