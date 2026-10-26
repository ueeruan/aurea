// Android spec: effects/EffectPicker.kt, effects/EffectPickerLogic.kt,
// effects/EffectsCatalogView.kt (o detalhe), effects/EffectCatalogMeta.kt (as
// etiquetas) e effects/EffectPrefs.kt. A tela cheia "Adicionar efeito" da Omega
// (área 07, P0-6): recomendados, recentes/favoritos, categorias, busca com
// etiquetas, detalhe paginado com as variações, e as ferramentas-efeito.
import SwiftUI
import UIKit
import UniformTypeIdentifiers

private enum EffectPickerLayout {
    static let longPress: Double = 0.45
    /// Abrir/fechar o detalhe (referência: 200 ms subindo; véu + descida ao fechar).
    static let detailSeconds: Double = 0.2
    /// Transição da categoria (100 ms; alfa em 25 ms, na saída atrasado 75 ms).
    static let categorySeconds: Double = 0.1
}

// =============================================================================
//  A LÓGICA do escolhedor (espelho de EffectPickerLogic.kt). Tudo sai do
//  CATÁLOGO que o motor publica: efeito ou categoria novos aparecem sozinhos.
// =============================================================================

/// Id da ficha "Todos" (`effects.category.all`).
let fxAllCategoriesId = "all"

/// Id estável de uma categoria: o nome do motor sem acento, em minúsculas, com
/// `_` no lugar do que não é letra ou número ("Controles de expressão" →
/// `controles_de_expressao`).
func fxEffectCategoryId(_ category: String) -> String {
    var slug = ""
    var gap = false
    for scalar in fxNormalizeSearch(category).unicodeScalars {
        let ascii = scalar.isASCII && (CharacterSet.lowercaseLetters.contains(scalar) || CharacterSet.decimalDigits.contains(scalar))
        if ascii {
            if gap && !slug.isEmpty { slug.append("_") }
            slug.unicodeScalars.append(scalar); gap = false
        } else { gap = true }
    }
    return slug.isEmpty ? String(fxEffectTypeId(category), radix: 16) : slug
}

/// Id do cartão (`effects.card.<id>`): o `typeId` sem sinal, igual ao Android.
func fxEffectCardId(_ typeId: UInt32) -> String { String(typeId) }

private let fxAudioCategories: Set<String> = ["audio", "som", "sound"]

/// A categoria é de SOM (o efeito mexe no áudio da camada, não no pixel).
func fxIsAudioCategory(_ category: String) -> Bool { fxAudioCategories.contains(fxNormalizeSearch(category)) }

/// Efeitos que GERAM som (o Tom): servem até para camada muda.
private let fxSoundGeneratorIds: Set<UInt32> = Set([
    "aurea.audio.tone", "aurea.audio.tom", "aurea.audio.tone_generator",
    "aurea.audio.test_tone", "aurea.generate.tone", "aurea.generate.tone_generator",
].map(fxEffectTypeId))
private let fxSoundGeneratorNames: Set<String> = ["tom", "tone", "gerador de tom", "tone generator", "tom de teste", "test tone"]

func fxMakesSound(_ entry: EffectCatalogItem) -> Bool {
    fxSoundGeneratorIds.contains(entry.typeId) || fxSoundGeneratorNames.contains(fxNormalizeSearch(entry.name))
}

/// O que o catálogo oferece para ESTA camada: sem som, some o que só mexe no som.
func fxPickableEffects(_ catalog: [EffectCatalogItem], layerHasAudio: Bool, layerKind: UInt32 = 0) -> [EffectCatalogItem] {
    catalog.filter {
        !["aurea.motion.oscillate", "aurea.layout.grid_builder", "aurea.layout.grid_item", "aurea.light.scene_flare"].map(fxEffectTypeId).contains($0.typeId) &&
        (layerHasAudio || !fxIsAudioCategory($0.category) || fxMakesSound($0)) &&
        ($0.typeId != fxEffectTypeId("aurea.generate.raster_extrude") || [1, 2, 4, 5, 12].contains(layerKind))
    }
}

/// A grade sem busca: tudo (categoria nula) ou só a categoria escolhida.
func fxBrowseEffects(_ sorted: [EffectCatalogItem], category: String?) -> [EffectCatalogItem] {
    guard let category else { return sorted }
    return sorted.filter { $0.category == category }
}

/// Os recentes que ainda estão no catálogo desta camada, na ordem de uso.
func fxRecentEffects(_ recents: [UInt32], _ pickable: [EffectCatalogItem]) -> [EffectCatalogItem] {
    let byId = Dictionary(pickable.map { ($0.typeId, $0) }, uniquingKeysWith: { first, _ in first })
    var seen = Set<UInt32>()
    return recents.compactMap { id in seen.insert(id).inserted ? byId[id] : nil }
}

/// Os favoritos, na ordem do catálogo.
func fxFavoriteEffects(_ favorites: Set<UInt32>, _ sorted: [EffectCatalogItem]) -> [EffectCatalogItem] {
    sorted.filter { favorites.contains($0.typeId) }
}

/// O que a busca sabe de UM efeito, normalizado: os NOMES (acertar neles põe o
/// efeito na frente) e o TEXTO todo (nomes, categorias, sinônimos, descrição).
struct FxSearchDoc {
    let names: [String]
    let text: String
}

func fxSearchDoc(names: [String], extra: [String]) -> FxSearchDoc {
    var seen = Set<String>()
    let n = names.map(fxNormalizeSearch).filter { !$0.isEmpty && seen.insert($0).inserted }
    let all = n + extra.map(fxNormalizeSearch).filter { !$0.isEmpty && seen.insert($0).inserted }
    return FxSearchDoc(names: n, text: all.joined(separator: " "))
}

/// Todo termo tem de aparecer; a ordem é por PONTOS e, no empate, a do
/// catálogo. Pontos = os da busca do AUREA em 7 idiomas (4 nome igual · 3 nome
/// começa · 2 palavra do nome começa · 1 todos os termos no nome · 0 achado no
/// resto) SOMADOS aos da referência (`fxEffectSearchScore` com `fields`).
func fxSearchEffects(_ sorted: [EffectCatalogItem], _ docs: [UInt32: FxSearchDoc], _ query: String,
                     fields: [UInt32: FxScoreFields] = [:]) -> [EffectCatalogItem] {
    let q = fxNormalizeSearch(query).split(whereSeparator: { $0.isWhitespace }).joined(separator: " ")
    guard !q.isEmpty else { return [] }
    let terms = q.split(separator: " ").map(String.init)
    var ranked: [(points: Int, order: Int, entry: EffectCatalogItem)] = []
    for (order, entry) in sorted.enumerated() {
        guard let doc = docs[entry.typeId], terms.allSatisfy({ doc.text.contains($0) }) else { continue }
        let rank: Int
        if doc.names.contains(q) { rank = 0 }
        else if doc.names.contains(where: { $0.hasPrefix(q) }) { rank = 1 }
        else if doc.names.contains(where: { name in name.split(separator: " ").contains { word in word.hasPrefix(q) } }) { rank = 2 }
        else if doc.names.contains(where: { name in terms.allSatisfy { name.contains($0) } }) { rank = 3 }
        else { rank = 4 }
        ranked.append(((4 - rank) + fxEffectSearchScore(fields[entry.typeId], q), order, entry))
    }
    return ranked.sorted { $0.points != $1.points ? $0.points > $1.points : $0.order < $1.order }.map(\.entry)
}

/// Sinônimos por CATEGORIA, em vários idiomas (os mesmos do Android).
func fxCategorySynonyms(_ category: String) -> String { fxCategorySynonymTable[fxNormalizeSearch(category)] ?? "" }

private let fxCategorySynonymTable: [String: String] = [
    "distorcer": "distort distortion deform deformar distorsion warp torcer entortar искажение деформация विकृति تشويه distorsi",
    "glitch": "glitch falha defeito erro error digital сбой глитч ग्लिच خلل gangguan",
    "estilizar": "stylize stylise estilo style artistico artistic estilizar стилизация стиль शैली أسلوب gaya",
    "cor": "color colour correcao correction grading lut tom tono цвет रंग لون warna",
    "luz": "light luz brilho glow shine flare свет свечение प्रकाश चमक ضوء توهج cahaya",
    "glow e luz": "light luz brilho glow shine flare свет свечение प्रकाश चमक ضوء توهج cahaya",
    "desfoque": "blur desfoque desenfoque borrar borrao embacar размытие धुंधला ضبابية تمويه buram kabur",
    "ruido": "noise ruido grao grain granulado шум зерно शोर ضوضاء derau",
    "nitidez": "sharpen sharp nitidez enfocar realce резкость तीक्ष्णता حدة ketajaman tajam",
    "tempo": "time tempo tiempo velocidade eco echo время समय وقت waktu",
    "transicao": "transition transicao transicion wipe cortina entrada saida переход संक्रमण انتقال transisi",
    "recorte": "key keying chroma croma recorte mascara matte fundo verde green screen кеинг хромакей क्रोमा مفتاح potong",
    "gerar": "generate generator gerar generar gerador render fill генерировать उत्पन्न توليد buat",
    "utilitario": "utility tool ferramenta utilitario herramienta утилита उपयोगिता أداة utilitas",
    "controles de expressao": "expression control controle slider expressao expresion выражение अभिव्यक्ति تعبير ekspresi",
    "pattern": "pattern padrao padroes patron textura узор पैटर्न نمط pola",
    "audio": "audio som sound sonido reverb eco echo delay voz звук ध्वनि ऑडियो صوت suara",
]

/// O índice da busca: no idioma do app, tudo o que o catálogo sabe (com a
/// descrição); nos 7 idiomas, o nome traduzido, os sinônimos da tabela, a
/// categoria, o grupo e as ETIQUETAS. Montado uma vez por idioma/catálogo: a
/// troca de idioma abaixo é síncrona, no main thread, e volta ao idioma do app.
func fxEffectSearchDocs(_ entries: [EffectCatalogItem]) -> [UInt32: FxSearchDoc] {
    let saved = AureaText.language
    let key = "\(saved.resolved.rawValue):\(entries.count):\(entries.first?.typeId ?? 0):\(entries.last?.typeId ?? 0)"
    if key == FxSearchIndexCache.key { return FxSearchIndexCache.docs }
    let current = fxCatalogHaystack(entries)
    var perLanguage: [UInt32: [String]] = [:]
    for language in AureaLanguage.allCases where language != .system {
        AureaText.language = language
        for entry in entries {
            perLanguage[entry.typeId, default: []].append(fxEffectSearchText(entry.typeId, entry.name, entry.category))
            perLanguage[entry.typeId, default: []].append(fxEffectCategoryLabel(entry.category))
            perLanguage[entry.typeId, default: []].append(fxEffectGroupOf(entry).label)
            if let tool = fxEffectToolOf(entry.typeId) { perLanguage[entry.typeId, default: []].append(tool.label) }
            // Etiquetas (P2-4) nos 7 idiomas: "glitch", "сбой", "retrô"...
            for tag in fxEffectTags(entry) { perLanguage[entry.typeId, default: []].append(AureaText.t(fxEffectTagLabelKey(tag))) }
        }
    }
    AureaText.language = saved
    var docs: [UInt32: FxSearchDoc] = [:]
    for entry in entries {
        docs[entry.typeId] = fxSearchDoc(
            names: [fxPickerEntryName(entry), entry.name],
            extra: [current[entry.typeId] ?? "", entry.category, fxCategorySynonyms(entry.category),
                    fxEffectToolOf(entry.typeId)?.keywords ?? ""] + (perLanguage[entry.typeId] ?? []))
    }
    FxSearchIndexCache.key = key
    FxSearchIndexCache.docs = docs
    return docs
}

private enum FxSearchIndexCache {
    static var key = ""
    static var docs: [UInt32: FxSearchDoc] = [:]
}

// =============================================================================
//  FERRAMENTAS que moram no catálogo (espelho de EffectPickerLogic.kt): as
//  legendas automáticas (Texto), o rastreio de câmera (Movimentar e
//  transformar) e a máscara (Fosco, máscara e chave). No catálogo são uma
//  entrada como outra qualquer; o toque ABRE a ferramenta existente.
// =============================================================================

enum FxEffectTool: CaseIterable {
    case captions, cameraTrack, mask
    /// Chave falsa e estável: o `typeId` é o FNV dela, como o de um efeito.
    var key: String {
        switch self {
        case .captions: return "aurea.tool.auto_captions"
        case .cameraTrack: return "aurea.tool.camera_track"
        case .mask: return "aurea.tool.mask"
        }
    }
    /// A categoria que manda a entrada para o grupo certo.
    var category: String {
        switch self {
        case .captions: return "Texto"
        case .cameraTrack: return "Rastreio"
        case .mask: return "Máscara"
        }
    }
    var typeId: UInt32 { fxEffectTypeId(key) }
    /// Sinônimos da busca (os mesmos do EffectCatalogMeta.kt).
    var keywords: String {
        switch self {
        case .captions: return "legenda legendas automaticas captions subtitles subtitulos transcrever transcribe fala voz speech субтитры कैप्शन ترجمة teks"
        case .cameraTrack: return "rastreio rastrear camera tracking track 3d cena match move seguimiento трекинг ट्रैकिंग تتبع pelacakan"
        case .mask: return "mascara mask roto recorte desenhar forma esconder mostrar mascara маска मास्क قناع masker"
        }
    }
    /// A chave do rótulo (o mesmo `R.string` do Android).
    var labelKey: String {
        switch self {
        case .captions: return "fxui_tool_captions"
        case .cameraTrack: return "fxui_tool_camera_track"
        case .mask: return "fxui_tool_mask"
        }
    }
    var label: String { AureaText.t(labelKey) }
    var glyph: Character {
        switch self {
        case .captions: return CupertinoGlyph.CaptionsBubble
        case .cameraTrack: return ShellGlyph.Viewfinder
        case .mask: return CupertinoGlyph.PencilOutline
        }
    }
    /// O painel de sempre de cada ferramenta.
    var panel: AureaModel.PanelKind {
        switch self {
        case .captions: return .captions
        case .cameraTrack: return .tracking
        case .mask: return .mask
        }
    }
}

/// A ferramenta por trás de um `typeId` do catálogo (nil = efeito de verdade).
func fxEffectToolOf(_ typeId: UInt32) -> FxEffectTool? { FxEffectTool.allCases.first { $0.typeId == typeId } }

/// Rastreio de câmera só tem o que analisar num VÍDEO (tipo 1).
func fxPickableTools(layerKind: UInt32) -> [FxEffectTool] {
    FxEffectTool.allCases.filter { $0 != .cameraTrack || layerKind == 1 }
}

func fxToolCatalogItem(_ tool: FxEffectTool) -> EffectCatalogItem {
    EffectCatalogItem(effectClass: 0, typeId: tool.typeId, name: tool.label, category: tool.category, paramCount: 0)
}

/// O nome que o cartão mostra: o da ferramenta traduzido, ou o do efeito.
func fxPickerEntryName(_ entry: EffectCatalogItem) -> String {
    fxEffectToolOf(entry.typeId)?.label ?? fxEffectDisplayName(entry.typeId, entry.name)
}

/// O nome em INGLÊS: o dos efeitos já é o mostrado (o AUREA mostra efeitos em
/// inglês em todo idioma); o das ferramentas é o rótulo em inglês. O detalhe e a
/// busca mostram a segunda linha só quando difere.
func fxPickerEntryEnglishName(_ entry: EffectCatalogItem) -> String {
    if let tool = fxEffectToolOf(entry.typeId) { return AureaText.english(tool.labelKey) }
    return fxEffectDisplayName(entry.typeId, entry.name)
}

/// ABRE uma ferramenta-efeito na camada escolhida: a única porta que o resto do
/// app precisa (catálogo, pilha da camada, busca de comandos). A Máscara fica
/// na pilha da camada desde aqui, mesmo que o painel feche sem caminho nenhum.
@MainActor
func fxOpenEffectTool(_ tool: FxEffectTool, in model: AureaModel) {
    if tool == .mask { model.addMaskTool() }
    if tool == .cameraTrack { model.cameraTrackerVisible = true }
    model.openPanel(tool.panel)
}

// =============================================================================
//  Os GRUPOS da tela "Adicionar efeito" (espelho de EffectGroup no Android): o
//  motor publica categorias em português e em inglês misturadas; a tela junta
//  tudo em grupos de gente, nesta ordem. `id` = `effects.category.<id>`.
//  Omega (P0-6): primeiro os da referência (… Visibilidade, Texto), depois os
//  extras do AUREA (Estilizar, Glitch, Tempo, Áudio, Utilitário, Plugins) e
//  "Outros" sempre por último.
// =============================================================================

enum FxEffectGroup: String, CaseIterable {
    // A ordem dos casos é a ordem dos ladrilhos (igual à do EffectGroup do Android).
    case colorLight = "color_light", drawEdge = "draw_edge", blur, distort, procedural, threeD = "3d"
    // `repeat` é palavra reservada do Swift: o caso se chama repeatTiles, o id continua "repeat".
    case motion, repeatTiles = "repeat", matte, visibility, text, stylize, glitch, time
    case audio, utility, plugins, other
    var id: String { rawValue }
    var label: String {
        switch self {
        case .colorLight: return AureaText.t("fxui_group_color_light")
        case .blur: return AureaText.t("fxui_group_blur")
        case .distort: return AureaText.t("fxui_group_distort")
        case .motion: return AureaText.t("fxui_group_motion")
        case .repeatTiles: return AureaText.t("fxui_group_repeat")
        case .visibility: return AureaText.t("fxui_group_visibility")
        case .stylize: return AureaText.t("fxui_group_stylize")
        case .glitch: return AureaText.t("fxui_group_glitch")
        case .drawEdge: return AureaText.t("fxui_group_draw_edge")
        case .procedural: return AureaText.t("fxui_group_procedural")
        case .matte: return AureaText.t("fxui_group_matte")
        case .time: return AureaText.t("fxui_group_time")
        case .text: return AureaText.t("fxui_group_text")
        case .threeD: return AureaText.t("fxui_group_3d")
        case .audio: return AureaText.t("fxui_group_audio")
        case .utility: return AureaText.t("fxui_group_utility")
        case .plugins: return AureaText.t("fxui_group_plugins")
        case .other: return AureaText.t("fxui_group_other")
        }
    }
    var glyph: Character {
        switch self {
        case .colorLight: return CupertinoGlyph.Sparkles
        case .blur: return CupertinoGlyph.DropFill
        case .distort, .motion: return CupertinoGlyph.Move
        case .repeatTiles, .stylize: return CupertinoGlyph.SquareGrid2x2
        case .visibility: return CupertinoGlyph.Sparkles
        case .glitch: return CupertinoGlyph.Bolt
        case .drawEdge: return CupertinoGlyph.PencilOutline
        case .procedural: return CupertinoGlyph.WandStars
        case .matte: return CupertinoGlyph.Scissors
        case .time: return CupertinoGlyph.Timer
        case .text: return CupertinoGlyph.CaptionsBubble
        case .audio: return CupertinoGlyph.MusicNote
        case .utility: return CupertinoGlyph.SliderHorizontal3
        case .threeD, .other: return CupertinoGlyph.WandStars
        case .plugins: return CupertinoGlyph.SquareStack3dUp
        }
    }
}

private let fxGroupByCategory: [String: FxEffectGroup] = {
    var map: [String: FxEffectGroup] = [:]
    for c in ["cor", "color", "colour", "luz", "light", "glow e luz"] { map[c] = .colorLight }
    for c in ["desenho", "drawing", "contorno", "edge"] { map[c] = .drawEdge }
    for c in ["desfoque", "blur", "nitidez", "sharpen"] { map[c] = .blur }
    for c in ["distorcer", "distort", "distorcao"] { map[c] = .distort }
    for c in ["transform", "transformar", "movimento", "motion", "rastreio", "tracking"] { map[c] = .motion }
    for c in ["repetir", "repeat"] { map[c] = .repeatTiles }
    for c in ["estilizar", "stylize", "stylise"] { map[c] = .stylize }
    map["glitch"] = .glitch
    for c in ["gerar", "generate", "pattern", "ruido", "noise"] { map[c] = .procedural }
    for c in ["recorte", "keying", "key", "mascara", "matte"] { map[c] = .matte }
    for c in ["transicao", "transition", "opacidade", "opacity", "visibilidade", "visibility"] { map[c] = .visibility }
    for c in ["tempo", "time"] { map[c] = .time }
    for c in ["texto", "text"] { map[c] = .text }
    map["3d"] = .threeD
    map["plugins"] = .plugins
    for c in ["audio", "som", "sound"] { map[c] = .audio }
    for c in ["utilitario", "utility", "controles de expressao", "expression controls"] { map[c] = .utility }
    return map
}()

private let fxGroupByKey: [UInt32: FxEffectGroup] = {
    var map: [UInt32: FxEffectGroup] = [:]
    map[fxEffectTypeId("aurea.generate.raster_extrude")] = .threeD
    for key in ["aurea.stylize.stroke_outline", "aurea.stylize.border", "aurea.stylize.drop_shadow",
                "aurea.stylize.find_edges", "aurea.stylize.bevel_alpha", "aurea.draw.drawing_progress"] { map[fxEffectTypeId(key)] = .drawEdge }
    for key in ["aurea.transform", "aurea.motion.oscillate.cycles", "aurea.motion.swing", "aurea.motion.wiggle",
                "aurea.motion.twitch", "aurea.distort.shake", "aurea.distort.corner_pin", "aurea.transform.parenting_helper"] {
        map[fxEffectTypeId(key)] = .motion
    }
    for key in ["aurea.stylize.repeat_tile", "aurea.stylize.motion_tile", "aurea.stylize.tiler", "aurea.stylize.hex_tile",
                "aurea.distort.kaleidoscope", "aurea.distort.mirror", "aurea.distort.griddler", "aurea.stylize.ball_grid"] {
        map[fxEffectTypeId(key)] = .repeatTiles
    }
    return map
}()

func fxEffectGroupOf(_ entry: EffectCatalogItem) -> FxEffectGroup {
    fxGroupByKey[entry.typeId] ?? fxGroupByCategory[fxNormalizeSearch(entry.category)] ?? .other
}

/// Os grupos que têm alguma entrada, na ordem fixa, cada um com as entradas na ordem recebida.
func fxGroupEntries(_ sorted: [EffectCatalogItem]) -> [(group: FxEffectGroup, entries: [EffectCatalogItem])] {
    let byGroup = Dictionary(grouping: sorted, by: fxEffectGroupOf)
    return FxEffectGroup.allCases.compactMap { group in
        guard let entries = byGroup[group], !entries.isEmpty else { return nil }
        return (group, entries)
    }
}

/// Os grupos do navegador: dentro de cada um, ordem ALFABÉTICA pelo nome mostrado.
func fxSortedGroups(_ sorted: [EffectCatalogItem]) -> [(group: FxEffectGroup, entries: [EffectCatalogItem])] {
    fxGroupEntries(sorted).map { item in (group: item.group, entries: fxSortedByName(item.entries) { fxPickerEntryName($0) }) }
}

private let fxGroupBannerKeys: [FxEffectGroup: String] = [
    .colorLight: "aurea.color.colorama", .blur: "aurea.blur.radial", .distort: "aurea.distort.wave_warp",
    .motion: "aurea.distort.corner_pin", .repeatTiles: "aurea.stylize.motion_tile", .stylize: "aurea.stylize.halftone",
    .glitch: "aurea.glitch.glitchify", .drawEdge: "aurea.stylize.find_edges", .procedural: "aurea.generate.fractal_noise",
    .matte: "aurea.key.chroma", .visibility: "aurea.transition.radial_wipe", .time: "aurea.time.warp_rgb",
]

/// A entrada que dá a prévia do ladrilho: a escolhida, ou o primeiro EFEITO do grupo.
func fxGroupBannerEntry(_ group: FxEffectGroup, _ entries: [EffectCatalogItem]) -> EffectCatalogItem? {
    let preferred = fxGroupBannerKeys[group].map(fxEffectTypeId)
    return entries.first { $0.typeId == preferred } ?? entries.first { fxEffectToolOf($0.typeId) == nil }
}

private let fxFeaturedKeys = [
    "aurea.draw.drawing_progress", "aurea.key.chroma", "aurea.blur.directional", "aurea.light.deep_glow",
    "aurea.glitch.vhs", FxEffectTool.captions.key, "aurea.stylize.halftone", "aurea.distort.wave_warp",
    "aurea.light.rays", "aurea.color.colorama", "aurea.stylize.pixel_sort", "aurea.generate.fractal_noise",
]

/// Os destaques fixos e, depois deles, os efeitos marcados como novos pelo motor (sem repetir).
func fxFeaturedEntries(_ sorted: [EffectCatalogItem]) -> [EffectCatalogItem] {
    let byId = Dictionary(sorted.map { ($0.typeId, $0) }, uniquingKeysWith: { first, _ in first })
    let curated = fxFeaturedKeys.compactMap { byId[fxEffectTypeId($0)] }
    let ids = Set(curated.map(\.typeId))
    return curated + sorted.filter { $0.isNew && !ids.contains($0.typeId) }
}

/// RECOMENDADOS (espelho de `recommendedEffects`): a curadoria fixa (o AUREA não
/// tem configuração remota), só o que a camada aceita, sem os efeitos que ela já
/// tem; sobrando vaga, os novos do motor. No máximo `max` (6).
func fxRecommendedEffects(_ catalog: [EffectCatalogItem], layerKind: UInt32, applied: [UInt32], max: Int = 6,
                          layerHasAudio: Bool = true) -> [EffectCatalogItem] {
    let offered = fxPickableEffects(catalog, layerHasAudio: layerHasAudio, layerKind: layerKind)
    let byId = Dictionary(offered.map { ($0.typeId, $0) }, uniquingKeysWith: { first, _ in first })
    let taken = Set(applied)
    let curated = fxFeaturedKeys.compactMap { byId[fxEffectTypeId($0)] }.filter { !taken.contains($0.typeId) }
    let ids = Set(curated.map(\.typeId))
    let fresh = offered.filter { $0.isNew && !ids.contains($0.typeId) && !taken.contains($0.typeId) }
    var seen = Set<UInt32>()
    return Array((curated + fresh).filter { seen.insert($0.typeId).inserted }.prefix(Swift.max(max, 0)))
}

/// Recentes: uma página de até 8, como a referência.
let fxQuickPageSize = 8

/// Itens por página de FAVORITOS: 2 linhas de ⌊(L − 20) / 69,5⌋ (8 em 360 pt).
func fxFavoritesPerPage(_ width: CGFloat) -> Int { 2 * max(1, Int(((width - 20) / 69.5).rounded(.down))) }

/// Colunas da grade de uma categoria: ⌊(L − 30) / 103⌋ (cartão de 88 + 15), no mínimo 1.
func fxCategoryGridColumns(_ width: CGFloat) -> Int { max(1, Int(((width - 30) / 103).rounded(.down))) }

/// As páginas de Recentes/Favoritos: a de Recentes (só se houver) e as de Favoritos.
func fxQuickPages<T>(_ recents: [T], _ favorites: [T], perPage: Int) -> [(recent: Bool, items: [T])] {
    var pages: [(recent: Bool, items: [T])] = []
    let size = max(perPage, 1)
    if !recents.isEmpty { pages.append((recent: true, items: Array(recents.prefix(min(fxQuickPageSize, size))))) }
    var start = 0
    while start < favorites.count {
        pages.append((recent: false, items: Array(favorites[start..<min(start + size, favorites.count)])))
        start += size
    }
    return pages
}

/// A página de 2 linhas enche por COLUNA, como a referência: (coluna, linha).
func fxQuickCell(_ index: Int) -> (column: Int, row: Int) { (index / 2, index % 2) }

/// O código curto de uma variação: "01", "02"…
func fxPresetCode(_ preset: Int) -> String { preset < 0 ? "" : String(format: "%02d", preset + 1) }

/// Ordem alfabética pelo nome no idioma do app, sem diferenciar caixa nem acento.
func fxSortedByName<T>(_ entries: [T], _ nameOf: (T) -> String) -> [T] {
    let locale = Locale(identifier: AureaText.language.resolved.rawValue)
    return entries.sorted {
        nameOf($0).compare(nameOf($1), options: [.caseInsensitive, .diacriticInsensitive], locale: locale) == .orderedAscending
    }
}

/// O grupo antes e o depois de `group` na ordem da tela (nulo nas pontas).
func fxAdjacentGroups(_ groups: [FxEffectGroup], _ group: FxEffectGroup) -> (previous: FxEffectGroup?, next: FxEffectGroup?) {
    guard let i = groups.firstIndex(of: group) else { return (nil, nil) }
    return (i > 0 ? groups[i - 1] : nil, i + 1 < groups.count ? groups[i + 1] : nil)
}

/// O recorte da prévia de comparação que mostra SÓ o efeito (a metade "depois"
/// de `preview_compare.frag`), cortado para preencher `dstAspect` sem esticar.
func fxEffectOnlyCrop(width: Int, height: Int, dstAspect: CGFloat) -> CGRect {
    let left = width / 2 + 1
    let halfW = max(width - left, 1)
    let h0 = max(height, 1)
    let srcAspect = CGFloat(halfW) / CGFloat(h0)
    if srcAspect > dstAspect {
        let w = min(max(Int(CGFloat(h0) * dstAspect), 1), halfW)
        return CGRect(x: left + (halfW - w) / 2, y: 0, width: w, height: h0)
    }
    let h = min(max(Int(CGFloat(halfW) / dstAspect), 1), h0)
    return CGRect(x: left, y: (h0 - h) / 2, width: halfW, height: h)
}

/// Só a metade "depois" da prévia, cortada para o quadro `size`.
func fxAfterOnly(_ image: UIImage?, size: CGSize) -> UIImage? {
    guard let cg = image?.cgImage, size.width > 0, size.height > 0 else { return nil }
    let rect = fxEffectOnlyCrop(width: cg.width, height: cg.height, dstAspect: size.width / size.height)
    return cg.cropping(to: rect).map { UIImage(cgImage: $0) }
}

/// O catálogo oferecido para a camada escolhida: os efeitos que fazem sentido
/// (som só com som, letras só em texto) e as ferramentas, na ordem do navegador.
@MainActor
func fxPickerEntries(_ model: AureaModel, layerHasAudio: Bool) -> [EffectCatalogItem] {
    let text3D = !(model.engine.text3D(forLayer: model.primarySelection ?? 0) ?? [:]).isEmpty
    let textLayer = model.selectedLayer?.kind == 4
    let effects = fxPickableEffects(model.effectCatalog, layerHasAudio: layerHasAudio, layerKind: model.selectedLayer?.kind ?? 0).filter {
        (textLayer || text3D || $0.typeId != fxEffectTypeId("aurea.text.transform")) &&
        (textLayer || text3D || $0.typeId != fxEffectTypeId("aurea.text.animator"))
    }
    let all = effects + fxPickableTools(layerKind: model.selectedLayer?.kind ?? 0).map(fxToolCatalogItem)
    return fxArrangeCatalog(all, fxEffectCategories(all))
}

/// O nome de uma variação (preset) pelo id estável; sem tradução, o nome do motor.
func fxPresetName(_ typeId: UInt32, _ pid: String, _ fallback: String) -> String {
    if typeId == fxEffectTypeId("aurea.color.grade") {
        let looks = ["cinema", "warm", "cool", "vintage", "mono", "noir", "vivid", "faded", "sunset", "teal", "film", "anime", "matte"]
        return looks.contains(pid) ? AureaText.t("look_" + pid) : fallback
    }
    if typeId == fxEffectTypeId("aurea.enhance.video") {
        let modes = ["auto", "social", "restore", "anime", "upscaled", "film"]
        return modes.contains(pid) ? AureaText.t("fxp_enhance_" + pid) : fallback
    }
    switch pid {
    case "impact", "handheld", "glitch": return AureaText.t("fxp_" + pid)
    case "saber_default", "saber_lightsaber", "saber_neon", "saber_electric", "saber_fire",
         "saber_plasma", "saber_energy", "saber_soft", "saber_magic":
        return AureaText.t("fxp_" + pid)
    case "classic", "dust", "living_grid": return AureaText.t("fxp_constellation_" + pid)
    case "sphere": return AureaText.t("fx030c_src_sphere")
    default: return fallback
    }
}

// =============================================================================
//  Omega efeitos (navegador, P0-6): FAVORITOS e RECENTES com VARIAÇÃO (espelho
//  de EffectRef/EffectPrefs.kt). "<typeId>" ou "<typeId>/<preset>".
//
//  No iOS a classe `FxEffectPrefs` (EffectsHuman.swift) continua dona das
//  listas antigas (só tipos): elas seguem valendo para quem só conhece o tipo
//  (busca de comandos, "Limpar recentes") e não perdem nada. As variações moram
//  em chaves novas, e a leitura junta as duas.
// =============================================================================

struct FxEffectRef: Hashable {
    let typeId: UInt32
    /// Índice em `engine.effectPresets(typeId)`; −1 = o efeito base.
    let preset: Int
    init(_ typeId: UInt32, preset: Int = -1) {
        self.typeId = typeId
        self.preset = preset < 0 ? -1 : preset
    }
    var isBase: Bool { preset < 0 }
    var key: String { preset >= 0 ? "\(typeId)/\(preset)" : "\(typeId)" }
}

/// Lê uma referência gravada: aceita o inteiro antigo (com ou sem sinal) e "id/preset". Lixo = nil.
func fxParseEffectRef(_ stored: String) -> FxEffectRef? {
    let text = stored.trimmingCharacters(in: .whitespaces)
    guard !text.isEmpty else { return nil }
    let parts = text.split(separator: "/", omittingEmptySubsequences: false)
    guard parts.count <= 2, let raw = Int64(parts[0].trimmingCharacters(in: .whitespaces)),
          raw >= Int64(Int32.min), raw <= Int64(UInt32.max) else { return nil }
    let id = UInt32(truncatingIfNeeded: raw)
    guard parts.count == 2 else { return FxEffectRef(id) }
    guard let preset = Int(parts[1].trimmingCharacters(in: .whitespaces)), preset >= 0 else { return nil }
    return FxEffectRef(id, preset: preset)
}

/// "12,7/1,…": lixo e repetição somem; a ordem fica.
func fxParseEffectRefs(_ stored: String?) -> [FxEffectRef] {
    var seen = Set<FxEffectRef>()
    return (stored ?? "").split(separator: ",").compactMap { fxParseEffectRef(String($0)) }.filter { seen.insert($0).inserted }
}

func fxFormatEffectRefs(_ refs: [FxEffectRef]) -> String { refs.map(\.key).joined(separator: ",") }

/// O item vai para a frente dos recentes, sem repetir, no máximo `max`.
func fxPushRecentRef(_ recents: [FxEffectRef], _ ref: FxEffectRef, max: Int) -> [FxEffectRef] {
    Array(([ref] + recents.filter { $0 != ref }).prefix(Swift.max(max, 0)))
}

/// Junta os recentes com variação e a lista antiga (só tipos), na ordem da
/// antiga: cada tipo traz as variações dele na ordem da nova; o tipo que só a
/// antiga tem entra como efeito base; o que a antiga já não tem (limpa) sai.
func fxMergeRecentRefs(_ refs: [FxEffectRef], base: [UInt32], max: Int) -> [FxEffectRef] {
    var out: [FxEffectRef] = []
    var seen = Set<FxEffectRef>()
    for typeId in base {
        let mine = refs.filter { $0.typeId == typeId }
        for ref in (mine.isEmpty ? [FxEffectRef(typeId)] : mine) where seen.insert(ref).inserted { out.append(ref) }
    }
    return Array(out.prefix(Swift.max(max, 0)))
}

extension FxEffectPrefs {
    static let presetFavoritesKey = "aurea.efeitos.favoritos.variacoes"
    static let recentRefsKey = "aurea.efeitos.recentes.v2"
    static let quickPageKey = "aurea.efeitos.pagina_rapida"

    private var storedPresetFavorites: Set<FxEffectRef> {
        Set((UserDefaults.standard.stringArray(forKey: Self.presetFavoritesKey) ?? []).compactMap(fxParseEffectRef).filter { !$0.isBase })
    }

    /// Favoritos: os efeitos base (a lista de sempre) e as variações.
    var favoriteRefs: Set<FxEffectRef> {
        storedPresetFavorites.union(favorites.map { FxEffectRef($0) })
    }

    func isFavorite(_ ref: FxEffectRef) -> Bool {
        ref.isBase ? isFavorite(ref.typeId) : storedPresetFavorites.contains(ref)
    }

    /// Marca/desmarca o efeito ou a variação e grava. Devolve o estado novo.
    @discardableResult
    func toggleFavorite(_ ref: FxEffectRef) -> Bool {
        if ref.isBase { return toggleFavorite(ref.typeId) }
        objectWillChange.send()
        var presets = storedPresetFavorites
        let on = !presets.contains(ref)
        if on { presets.insert(ref) } else { presets.remove(ref) }
        UserDefaults.standard.set(presets.map(\.key).sorted(), forKey: Self.presetFavoritesKey)
        return on
    }

    /// Recentes com variação, o último na frente (até 8). Sem a chave nova
    /// (aparelho antigo), a lista antiga vira efeitos base.
    var recentRefs: [FxEffectRef] {
        guard let stored = UserDefaults.standard.string(forKey: Self.recentRefsKey) else {
            return Array(recents.prefix(fxQuickPageSize).map { FxEffectRef($0) })
        }
        return fxMergeRecentRefs(fxParseEffectRefs(stored), base: recents, max: fxQuickPageSize)
    }

    /// Põe o efeito ou a variação na frente dos recentes (as duas listas).
    func addRecent(_ ref: FxEffectRef) {
        let next = fxPushRecentRef(recentRefs, ref, max: fxQuickPageSize)
        UserDefaults.standard.set(fxFormatEffectRefs(next), forKey: Self.recentRefsKey)
        addRecent(ref.typeId)
    }

    /// A página de Recentes/Favoritos que estava à vista (lembrada).
    var quickPage: Int { UserDefaults.standard.integer(forKey: Self.quickPageKey) }

    func updateQuickPage(_ page: Int) {
        guard page >= 0 else { return }
        UserDefaults.standard.set(page, forKey: Self.quickPageKey)
    }
}

/// Um item de Recentes/Favoritos já achado no catálogo desta camada.
struct FxQuickItem: Equatable {
    let entry: EffectCatalogItem
    let preset: Int
    var ref: FxEffectRef { FxEffectRef(entry.typeId, preset: preset) }
}

/// Os recentes que ainda valem nesta camada (efeito oferecido e variação existente).
func fxRecentItems(_ recents: [FxEffectRef], _ pickable: [EffectCatalogItem], presetCount: (UInt32) -> Int,
                   max: Int = fxQuickPageSize) -> [FxQuickItem] {
    let byId = Dictionary(pickable.map { ($0.typeId, $0) }, uniquingKeysWith: { first, _ in first })
    var seen = Set<FxEffectRef>()
    var out: [FxQuickItem] = []
    for ref in recents where seen.insert(ref).inserted {
        guard let entry = byId[ref.typeId], ref.preset < presetCount(ref.typeId) else { continue }
        out.append(FxQuickItem(entry: entry, preset: ref.preset))
    }
    return Array(out.prefix(Swift.max(max, 0)))
}

/// Os favoritos na ordem do catálogo: o efeito base e depois as variações dele.
func fxFavoriteItems(_ favorites: Set<FxEffectRef>, _ sorted: [EffectCatalogItem], presetCount: (UInt32) -> Int) -> [FxQuickItem] {
    guard !favorites.isEmpty else { return [] }
    let byType = Dictionary(grouping: favorites, by: \.typeId)
    var out: [FxQuickItem] = []
    for entry in sorted {
        guard let refs = byType[entry.typeId] else { continue }
        let count = presetCount(entry.typeId)
        for preset in Set(refs.map(\.preset)).filter({ $0 < count }).sorted() { out.append(FxQuickItem(entry: entry, preset: preset)) }
    }
    return out
}

// =============================================================================
//  Omega efeitos (P2-4): ETIQUETAS do catálogo (espelho de EffectCatalogMeta.kt).
//  Ids estáveis (`effects.search.tag.<id>`, `effects.detail.tag.<id>`) e rótulo
//  `om_fxtag_<id>`. Vêm da categoria do motor, do grupo e da tabela por chave.
// =============================================================================

let fxEffectTagIds = [
    "glitch", "glow", "blur", "color", "retro", "distort", "transition", "3d", "text", "mask",
    "noise", "light", "repeat", "motion", "stylize", "time", "audio", "particles", "outline", "shadow",
]

func fxEffectTagLabelKey(_ tag: String) -> String { "om_fxtag_" + tag }

/// Os rótulos das etiquetas no idioma do app, na ordem.
func fxTagLabels() -> [(id: String, label: String)] {
    fxEffectTagIds.map { (id: $0, label: AureaText.t(fxEffectTagLabelKey($0))) }
}

private let fxTagByCategory: [String: String] = {
    var map: [String: String] = [:]
    for c in ["cor", "color", "colour"] { map[c] = "color" }
    for c in ["luz", "light", "glow e luz"] { map[c] = "light" }
    for c in ["desfoque", "blur"] { map[c] = "blur" }
    for c in ["distorcer", "distort", "distorcao"] { map[c] = "distort" }
    map["glitch"] = "glitch"
    for c in ["estilizar", "stylize", "stylise"] { map[c] = "stylize" }
    for c in ["ruido", "noise"] { map[c] = "noise" }
    for c in ["tempo", "time"] { map[c] = "time" }
    for c in ["transicao", "transition"] { map[c] = "transition" }
    for c in ["recorte", "keying", "key", "mascara", "matte"] { map[c] = "mask" }
    for c in ["texto", "text"] { map[c] = "text" }
    map["3d"] = "3d"
    for c in ["audio", "som", "sound"] { map[c] = "audio" }
    for c in ["desenho", "drawing", "contorno", "edge"] { map[c] = "outline" }
    for c in ["transform", "transformar", "movimento", "motion", "rastreio", "tracking"] { map[c] = "motion" }
    for c in ["repetir", "repeat", "pattern"] { map[c] = "repeat" }
    return map
}()

private let fxTagByGroup: [FxEffectGroup: String] = [
    .drawEdge: "outline", .blur: "blur", .distort: "distort", .threeD: "3d", .motion: "motion", .repeatTiles: "repeat",
    .matte: "mask", .text: "text", .stylize: "stylize", .glitch: "glitch", .time: "time", .audio: "audio",
]

private let fxTagByKey: [UInt32: [String]] = {
    let table: [(String, [String])] = [
        ("retro", ["aurea.glitch.vhs", "aurea.glitch.uni_vhs", "aurea.stylize.crt_emulator", "aurea.stylize.film_damage",
                   "aurea.stylize.scanlines", "aurea.stylize.jpeg_damage", "aurea.stylize.eight_bit", "aurea.stylize.pix_dither",
                   "aurea.time.low_fps", "aurea.stylize.grain", "aurea.stylize.halftone", "aurea.transition.burn_film",
                   "aurea.stylize.pixel_encoder", "aurea.time.posterize"]),
        ("glow", ["aurea.light.glow", "aurea.light.deep_glow", "aurea.light.glow_scan", "aurea.light.saber",
                  "aurea.light.light_burst", "aurea.light.halation", "aurea.light.rays", "aurea.light.sweep"]),
        ("light", ["aurea.light.spotlight", "aurea.light.lens_flare", "aurea.generate.aurora", "aurea.light.saber",
                   "aurea.light.light_burst", "aurea.color.exposure"]),
        ("noise", ["aurea.generate.fractal_noise", "aurea.stylize.grain", "aurea.stylize.film_damage",
                   "aurea.transition.noise_dissolve", "aurea.stylize.scatter", "aurea.distort.turbulence"]),
        ("particles", ["aurea.generate.particular", "aurea.transition.disintegrate", "aurea.stylize.ball_grid",
                       "aurea.stylize.star_field", "aurea.stylize.scatter", "aurea.generate.constellation", "aurea.3d.shatter"]),
        ("shadow", ["aurea.stylize.drop_shadow", "aurea.distort.slant", "aurea.stylize.bevel_alpha"]),
        ("outline", ["aurea.stylize.stroke_outline", "aurea.stylize.border", "aurea.stylize.find_edges", "aurea.3d.stroke",
                     "aurea.draw.drawing_progress", "aurea.light.saber"]),
        ("3d", ["aurea.distort.sphere_wrap", "aurea.distort.cylinder_wrap", "aurea.generate.raster_extrude",
                "aurea.3d.part_transform", "aurea.3d.part_hide", "aurea.3d.part_texture", "aurea.3d.shatter",
                "aurea.3d.stroke", "aurea.shape3d.layout", "aurea.generate.aurora", "aurea.generate.constellation",
                "aurea.ai.depth_map", FxEffectTool.cameraTrack.key]),
        ("transition", ["aurea.transition.luma_dissolve", "aurea.distort.ripple_dissolve", "aurea.glitch.dissolve_shake",
                        "aurea.transition.disintegrate", "aurea.transition.noise_dissolve"]),
        ("text", ["aurea.text.animator", "aurea.text.transform", FxEffectTool.captions.key]),
        ("mask", [FxEffectTool.mask.key, "aurea.key.chroma", "aurea.key.luma", "aurea.key.matte_refine", "aurea.key.rotobrush",
                  "aurea.utility.copy_background", "aurea.transform.crop", "aurea.ai.depth_map"]),
        ("motion", ["aurea.distort.shake", "aurea.time.flow_motion_blur", "aurea.blur.directional", FxEffectTool.cameraTrack.key,
                    "aurea.motion.swing", "aurea.motion.wiggle", "aurea.motion.twitch", "aurea.motion.oscillate.cycles"]),
        ("repeat", ["aurea.pattern.checkerboard", "aurea.pattern.hexagonal", "aurea.stylize.mosaic"]),
        ("color", ["aurea.time.warp_rgb", "aurea.color.color_offset", "aurea.color.threshold_rgb", "aurea.color.rgb_split", "aurea.color.grade"]),
        ("distort", ["aurea.glitch.datamosh", "aurea.distort.displacement_map"]),
        ("glitch", ["aurea.stylize.pixel_sort", "aurea.stylize.jpeg_damage", "aurea.color.rgb_split", "aurea.stylize.pixel_encoder",
                    "aurea.glitch.video_glitch", "aurea.glitch.datamosh", "aurea.time.random_edits"]),
        ("time", ["aurea.time.echo", "aurea.time.wide_time", "aurea.enhance.video"]),
        ("audio", ["aurea.generate.audio_spectrum", "aurea.generate.audio_waveform", "aurea.generate.spectrum_analyzer", "aurea.audio.tone"]),
        ("blur", ["aurea.time.flow_motion_blur", "aurea.blur.vector_blur"]),
        ("stylize", ["aurea.stylize.holomatrix", "aurea.stylize.vignette"]),
    ]
    var map: [UInt32: [String]] = [:]
    for (tag, keys) in table { for key in keys { map[fxEffectTypeId(key), default: []].append(tag) } }
    return map
}()

/// As etiquetas de um efeito (ou ferramenta), na ordem de `fxEffectTagIds`.
func fxEffectTags(_ entry: EffectCatalogItem) -> [String] {
    var found = Set<String>()
    if let tag = fxTagByCategory[fxNormalizeSearch(entry.category)] { found.insert(tag) }
    if let tag = fxTagByGroup[fxEffectGroupOf(entry)] { found.insert(tag) }
    for tag in fxTagByKey[entry.typeId] ?? [] { found.insert(tag) }
    return fxEffectTagIds.filter { found.contains($0) }
}

// --- A pontuação da referência ------------------------------------------------

/// O que a pontuação olha em UM efeito, sem acento e em minúsculas.
struct FxScoreFields {
    let tags: [String]
    let name: String
    let english: String
    let description: String
    init(tags: [String], name: String, english: String, description: String) {
        self.tags = tags.map(fxNormalizeSearch).filter { !$0.isEmpty }
        self.name = fxNormalizeSearch(name)
        self.english = fxNormalizeSearch(english)
        self.description = fxNormalizeSearch(description)
    }
}

/// Força do acerto (os dois normalizados): igual 3, começa 2, contém 1, nada 0.
func fxSearchMatch(_ field: String, _ query: String) -> Int {
    if query.isEmpty || field.isEmpty { return 0 }
    if field == query { return 3 }
    if field.hasPrefix(query) { return 2 }
    return field.contains(query) ? 1 : 0
}

/// Etiqueta ×7, nome ×3, nome em inglês ×2, descrição ×1 (× a força do acerto).
func fxEffectSearchScore(_ fields: FxScoreFields?, _ query: String) -> Int {
    guard let fields else { return 0 }
    let q = fxNormalizeSearch(query).split(whereSeparator: { $0.isWhitespace }).joined(separator: " ")
    guard !q.isEmpty else { return 0 }
    let tag = fields.tags.map { fxSearchMatch($0, q) }.max() ?? 0
    return tag * 7 + fxSearchMatch(fields.name, q) * 3 + fxSearchMatch(fields.english, q) * 2 + fxSearchMatch(fields.description, q)
}

/// Os efeitos que têm TODAS as etiquetas `chips` (fichas no campo).
func fxEffectsWithTags(_ entries: [EffectCatalogItem], _ chips: [String], _ tagsOf: (EffectCatalogItem) -> [String]) -> [EffectCatalogItem] {
    guard !chips.isEmpty else { return entries }
    return entries.filter { entry in let tags = tagsOf(entry); return chips.allSatisfy { tags.contains($0) } }
}

/// As etiquetas cujo rótulo casa com o que se digita: as que começam antes das que contêm.
func fxMatchingTags(_ labels: [(id: String, label: String)], _ query: String, exclude: [String] = []) -> [String] {
    let q = fxNormalizeSearch(query).split(whereSeparator: { $0.isWhitespace }).joined(separator: " ")
    guard !q.isEmpty else { return [] }
    var hits: [(id: String, strength: Int, order: Int)] = []
    for (order, item) in labels.enumerated() where !exclude.contains(item.id) {
        let strength = fxSearchMatch(fxNormalizeSearch(item.label), q)
        if strength > 0 { hits.append((item.id, strength, order)) }
    }
    return hits.sorted { $0.strength != $1.strength ? $0.strength > $1.strength : $0.order < $1.order }.map(\.id)
}

/// O que a busca sabe do catálogo (espelho de `SearchIndex` no Android).
struct FxSearchIndex {
    let docs: [UInt32: FxSearchDoc]
    let fields: [UInt32: FxScoreFields]
    let tags: [UInt32: [String]]
    let english: [UInt32: String]
    let names: [UInt32: String]
}

func fxEffectSearchIndex(_ entries: [EffectCatalogItem]) -> FxSearchIndex {
    let docs = fxEffectSearchDocs(entries)
    var fields: [UInt32: FxScoreFields] = [:]
    var tags: [UInt32: [String]] = [:]
    var english: [UInt32: String] = [:]
    var names: [UInt32: String] = [:]
    for entry in entries {
        let entryTags = fxEffectTags(entry)
        let shown = fxPickerEntryName(entry)
        let inEnglish = fxPickerEntryEnglishName(entry)
        tags[entry.typeId] = entryTags
        english[entry.typeId] = inEnglish
        names[entry.typeId] = shown
        fields[entry.typeId] = FxScoreFields(tags: entryTags.map { AureaText.t(fxEffectTagLabelKey($0)) }, name: shown,
                                             english: inEnglish, description: fxEffectDescription(entry.typeId, entry.category))
    }
    return FxSearchIndex(docs: docs, fields: fields, tags: tags, english: english, names: names)
}

/// Os resultados: com fichas, só os efeitos delas (filtrados pelo texto, ou em
/// ordem alfabética); sem fichas, a busca pelo texto.
func fxSearchResults(_ sorted: [EffectCatalogItem], index: FxSearchIndex, query: String, chips: [String]) -> [EffectCatalogItem] {
    let base = fxEffectsWithTags(sorted, chips) { index.tags[$0.typeId] ?? [] }
    if !query.trimmingCharacters(in: .whitespaces).isEmpty { return fxSearchEffects(base, index.docs, query, fields: index.fields) }
    if !chips.isEmpty { return fxSortedByName(base) { index.names[$0.typeId] ?? $0.name } }
    return []
}

// =============================================================================
//  O NAVEGADOR DE EFEITOS (espelho de EffectPicker.kt `EffectAddSheet`, Omega).
//  Moldura «superfície de lista»: ✕ · título 17 · 🔍 (some na categoria) e o fio
//  de 0,5 só com conteúdo rolado. Início: Recomendados, Recentes/Favoritos (toque
//  ADICIONA, toque longo abre o DETALHE), categorias e a porta dos plugins.
//  Categoria: grade de 88 pt em ordem alfabética, anterior/próxima nas pontas.
//  Cartão, recomendado e resultado → detalhe; a variação do detalhe adiciona.
// =============================================================================

/// Pedido da folha: o que o painel sabe (catálogo filtrado, favoritos e o que
/// fazer ao escolher) levado até a raiz do editor, que apresenta. [browse]
/// abre na tela inicial; sem ele, direto na busca.
struct EffectSearchRequest: Identifiable {
    let id = UUID()
    let prefs: FxEffectPrefs
    let sorted: [EffectCatalogItem]
    let onPick: (EffectCatalogItem) -> Void
    let onFavorite: (EffectCatalogItem) -> Void
    var browse = false
    var onTool: ((FxEffectTool) -> Void)? = nil
    /// Omega efeitos: recebe TODA adição com a variação (índice em
    /// `engine.effectPresets(typeId)`; −1 = só o efeito). Nulo: o efeito base vai
    /// a `onPick` e a variação a `AureaModel.addEffectWithPreset`.
    var onPickVariant: ((EffectCatalogItem, Int) -> Void)? = nil
}

/// Onde o detalhe abriu: a lista que ele pagina e o item tocado.
struct FxDetailRequest: Identifiable {
    let id = UUID()
    let entries: [EffectCatalogItem]
    let index: Int
}

/// Distância do conteúdo ao topo (o fio do cabeçalho aparece com o conteúdo rolado).
private struct FxScrollOffsetKey: PreferenceKey {
    static var defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) { value = nextValue() }
}

/// Mede, no topo de um ScrollView, quanto ele rolou (espaço de coordenadas `space`).
private struct FxScrollProbe: View {
    let space: String
    var body: some View {
        GeometryReader { geometry in
            Color.clear.preference(key: FxScrollOffsetKey.self, value: geometry.frame(in: .named(space)).minY)
        }.frame(height: 0)
    }
}

struct EffectAddSheet: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject var prefs: FxEffectPrefs
    let request: EffectSearchRequest
    let close: () -> Void
    @State private var groupId: String?
    @State private var searching: Bool
    @State private var searchChips: [String] = []
    @State private var searchEpoch = 0
    @State private var detail: FxDetailRequest?
    @State private var detailClose = 0
    @State private var homeScrolled = false
    @State private var gridScrolled = false
    /// 0.3.0: plugins de efeito (instalar, ver, remover) — PluginManagerSheet.kt.
    @State private var managingPlugins = false
    /// 0.3.2: efeito Hyper sem o acesso abre a tela bloqueada (HyperEffectGate).
    @State private var hyperLocked = false
    @ObservedObject private var entitlements = HyperEntitlements.shared

    init(request: EffectSearchRequest, close: @escaping () -> Void) {
        _prefs = ObservedObject(wrappedValue: request.prefs)
        self.request = request
        self.close = close
        _searching = State(initialValue: !request.browse)
    }

    var body: some View {
        ZStack {
            browser
            if searching {
                FxSearchPane(sorted: request.sorted, chips: $searchChips, onDetail: openDetail,
                             onClose: { if request.browse { searching = false } else { close() } })
                    .id(searchEpoch)
            }
            if let current = detail {
                FxDetailOverlay(entries: current.entries, startIndex: current.index, closeSignal: detailClose, variants: true,
                                prefs: prefs, onClosed: { if detail?.id == current.id { detail = nil } }, onAdd: add,
                                onTag: { tag in detail = nil; openSearch([tag]) })
                    .id(current.id)
            }
        }
        .sheet(isPresented: $hyperLocked) { HyperLockedSheet { hyperLocked = false } }
    }

    private func openDetail(_ entries: [EffectCatalogItem], _ index: Int) {
        detail = FxDetailRequest(entries: entries, index: index)
    }

    private func openSearch(_ chips: [String]) {
        searchChips = chips
        searchEpoch += 1
        searching = true
    }

    /// Adicionar (do detalhe ou de Recentes/Favoritos): grava nos recentes, fecha
    /// e adiciona — com a variação — ou abre a ferramenta.
    private func add(_ entry: EffectCatalogItem, _ preset: Int) {
        if HyperEffectGate.locked(typeId: entry.typeId) { hyperLocked = true; return }
        let tool = fxEffectToolOf(entry.typeId)
        let variant = tool == nil ? preset : -1
        prefs.addRecent(FxEffectRef(entry.typeId, preset: variant))
        close()
        if let tool { request.onTool?(tool); return }
        if let pickVariant = request.onPickVariant { pickVariant(entry, variant) }
        else if variant < 0 { request.onPick(entry) }
        else { model.addEffectWithPreset(entry.typeId, preset: variant) }
    }

    private func openGroup(_ id: String?) {
        withAnimation(.easeOut(duration: EffectPickerLayout.categorySeconds)) { groupId = id }
    }

    private var categoryTransition: AnyTransition {
        .asymmetric(
            insertion: AnyTransition.offset(x: 100).animation(.easeOut(duration: EffectPickerLayout.categorySeconds))
                .combined(with: AnyTransition.opacity.animation(.linear(duration: 0.025))),
            removal: AnyTransition.offset(x: 100).animation(.easeIn(duration: EffectPickerLayout.categorySeconds))
                .combined(with: AnyTransition.opacity.animation(.linear(duration: 0.025).delay(0.075))))
    }

    private var browser: some View {
        let sorted = request.sorted
        let groups = fxSortedGroups(sorted)
        let group = groups.first { $0.group.id == groupId }
        return VStack(spacing: 0) {
            topBar(title: group?.group.label ?? AureaText.t("fx_browser_title"), inGroup: group != nil,
                   divider: group == nil ? homeScrolled : gridScrolled)
            ZStack {
                if let group {
                    let near = fxAdjacentGroups(groups.map { $0.group }, group.group)
                    FxGroupGrid(group: group.group, entries: group.entries, store: model.effectPreviews,
                                previous: near.previous, next: near.next, onDetail: openDetail,
                                onGroup: { groupId = $0.id }, onScrolled: { gridScrolled = $0 })
                        .transition(categoryTransition)
                        .zIndex(1)
                } else {
                    home(sorted: sorted, groups: groups)
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .foregroundStyle(AureaColors.text)
        .background(AureaColors.fxListSurface.ignoresSafeArea())
        .sheet(isPresented: $managingPlugins) { PluginManagerView().environmentObject(model).presentationDetents([.large]) }
    }

    private func home(sorted: [EffectCatalogItem], groups: [(group: FxEffectGroup, entries: [EffectCatalogItem])]) -> some View {
        let presetCount: (UInt32) -> Int = { typeId in
            fxEffectToolOf(typeId) == nil ? model.engine.effectPresets(typeId).count / 2 : 0
        }
        let recents = fxRecentItems(prefs.recentRefs, sorted, presetCount: presetCount)
        let favorites = fxFavoriteItems(prefs.favoriteRefs, sorted, presetCount: presetCount)
        let recommended = fxRecommendedEffects(sorted, layerKind: model.selectedLayer?.kind ?? 0, applied: model.effects.map(\.typeId))
        return FxBrowserHome(recommended: recommended, recents: recents, favorites: favorites, groups: groups, prefs: prefs,
                             store: model.effectPreviews, onAdd: add, onDetail: openDetail,
                             onPlugins: { managingPlugins = true }, onGroup: { openGroup($0.id) },
                             onScrolled: { homeScrolled = $0 })
    }

    /// ✕ (ou ‹ dentro de um grupo) · título 17 · 🔍 — `effects.close`, `effects.back`, `effects.search`.
    private func topBar(title: String, inGroup: Bool, divider: Bool) -> some View {
        VStack(spacing: 0) {
            HStack(spacing: 0) {
                Button { if inGroup { openGroup(nil) } else { close() } } label: {
                    CupertinoGlyph.text(inGroup ? CupertinoGlyph.ChevronBack : CupertinoGlyph.Xmark, size: 22)
                        .mirrorsInRtl(inGroup)
                        .frame(width: AureaDims.minTap, height: AureaDims.minTap).contentShape(Rectangle())
                }.buttonStyle(AureaPressStyle(shrink: 1))
                    .accessibilityLabel(AureaText.t(inGroup ? "fxui_back_categories" : "common_close"))
                    .accessibilityIdentifier(inGroup ? "effects.back" : "effects.close")
                Text(title).font(.aurea(size: 17, weight: .medium)).lineLimit(1).truncationMode(.tail)
                    .multilineTextAlignment(.center)
                    .padding(.horizontal, AureaDims.s1).frame(maxWidth: .infinity, alignment: .center)
                    .accessibilityAddTraits(.isHeader)
                if inGroup {
                    Color.clear.frame(width: AureaDims.minTap, height: AureaDims.minTap)
                } else {
                    Button { openSearch([]) } label: {
                        CupertinoGlyph.text(CupertinoGlyph.Search, size: 22)
                            .frame(width: AureaDims.minTap, height: AureaDims.minTap).contentShape(Rectangle())
                    }.buttonStyle(AureaPressStyle(shrink: 1))
                        .accessibilityLabel(AureaText.t("fxui_search_effects"))
                        .accessibilityIdentifier("effects.search")
                }
            }.frame(height: 44)
            AureaColors.fxLowSurface.opacity(divider ? 1 : 0).frame(height: 0.5)
        }
    }
}

// =============================================================================
//  A TELA INICIAL (espelho de EffectPicker.kt `BrowserHome`).
// =============================================================================

private struct FxBrowserHome: View {
    let recommended: [EffectCatalogItem]
    let recents: [FxQuickItem]
    let favorites: [FxQuickItem]
    let groups: [(group: FxEffectGroup, entries: [EffectCatalogItem])]
    @ObservedObject var prefs: FxEffectPrefs
    let store: EffectPreviewStore
    let onAdd: (EffectCatalogItem, Int) -> Void
    let onDetail: ([EffectCatalogItem], Int) -> Void
    let onPlugins: () -> Void
    let onGroup: (FxEffectGroup) -> Void
    let onScrolled: (Bool) -> Void

    var body: some View {
        GeometryReader { geometry in
            let perPage = fxFavoritesPerPage(geometry.size.width)
            let pages = fxQuickPages(recents, favorites, perPage: perPage)
            ScrollView {
                LazyVStack(spacing: 0) {
                    FxScrollProbe(space: "fx.home")
                    if !recommended.isEmpty {
                        FxRecommendedBand(entries: recommended, store: store, onDetail: onDetail)
                    }
                    if !pages.isEmpty {
                        FxQuickBand(pages: pages, perPage: perPage, hint: favorites.isEmpty && !recents.isEmpty, prefs: prefs,
                                    store: store, onAdd: onAdd, onDetail: onDetail)
                    }
                    Color.clear.frame(height: 0)
                        .accessibilityElement().accessibilityLabel(AureaText.t("fx_browser_categories"))
                        .accessibilityAddTraits(.isHeader)
                    categories
                    pluginDoor
                }
            }
            .coordinateSpace(name: "fx.home")
            .onPreferenceChange(FxScrollOffsetKey.self) { onScrolled($0 < -0.5) }
            .accessibilityIdentifier("effects.home")
        }
    }

    /// Categorias: 2 colunas, espaço de 1, fundo «mais escuro», ladrilhos de 65.
    private var categories: some View {
        LazyVGrid(columns: [GridItem(.flexible(), spacing: 1), GridItem(.flexible(), spacing: 1)], spacing: 1) {
            ForEach(groups.indices, id: \.self) { index in
                FxGroupTile(group: groups[index].group, entries: groups[index].entries, store: store) {
                    onGroup(groups[index].group)
                }
            }
        }
        .padding(.bottom, 1)
        .background(AureaColors.fxDarkest)
    }

    /// 0.3.0: a porta dos plugins fica sempre à vista (a categoria só aparece com um instalado).
    private var pluginDoor: some View {
        Button(action: onPlugins) {
            HStack(spacing: 10) {
                CupertinoGlyph.text(CupertinoGlyph.SquareStack3dUp, size: 20, color: AureaColors.accent)
                Text(AureaText.t("plug_manage")).font(.aurea(size: 14)).frame(maxWidth: .infinity, alignment: .leading)
                CupertinoGlyph.text(CupertinoGlyph.ChevronRight, size: 14, color: AureaColors.fxTextSecondary).mirrorsInRtl(true)
            }
            .padding(.horizontal, 16).frame(minHeight: 52)
            .background(AureaColors.fxListSurface)
            .contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityIdentifier("effects.plugins")
    }
}

/// Posição e largura do conteúdo de uma faixa horizontal (para os véus das bordas).
private struct FxHorizontalScroll: Equatable {
    var minX: CGFloat = 0
    var width: CGFloat = 0
}

private struct FxHorizontalScrollKey: PreferenceKey {
    static var defaultValue = FxHorizontalScroll()
    static func reduce(value: inout FxHorizontalScroll, nextValue: () -> FxHorizontalScroll) { value = nextValue() }
}

/// RECOMENDADOS: faixa de 186 «superfície baixa», itens de 121×153 (miniatura
/// 115), véus de 80 nas bordas só quando dá para rolar daquele lado.
private struct FxRecommendedBand: View {
    let entries: [EffectCatalogItem]
    let store: EffectPreviewStore
    let onDetail: ([EffectCatalogItem], Int) -> Void
    @State private var scroll = FxHorizontalScroll()

    var body: some View {
        GeometryReader { outer in
            ScrollView(.horizontal, showsIndicators: false) {
                LazyHStack(spacing: 7.5) {
                    ForEach(Array(entries.enumerated()), id: \.element.typeId) { index, entry in
                        FxRecommendedItem(entry: entry, store: store) { onDetail(entries, index) }
                    }
                }
                .padding(.horizontal, 7.5)
                .frame(height: 186)
                .background(GeometryReader { inner in
                    Color.clear.preference(key: FxHorizontalScrollKey.self,
                                           value: FxHorizontalScroll(minX: inner.frame(in: .named("fx.recommended")).minX, width: inner.size.width))
                })
            }
            .coordinateSpace(name: "fx.recommended")
            .onPreferenceChange(FxHorizontalScrollKey.self) { scroll = $0 }
            .overlay(alignment: .leading) {
                if scroll.minX < -1 { veil(leading: true) }
            }
            .overlay(alignment: .trailing) {
                if scroll.width + scroll.minX > outer.size.width + 1 { veil(leading: false) }
            }
        }
        .frame(height: 186)
        .background(AureaColors.fxLowSurface)
        .accessibilityElement(children: .contain)
        .accessibilityLabel(AureaText.t("om_fx_recommended"))
        .accessibilityIdentifier("effects.featured")
    }

    private func veil(leading: Bool) -> some View {
        let solid = AureaColors.fxSunken
        let clear = AureaColors.fxSunken.opacity(0)
        return LinearGradient(colors: leading ? [solid, clear] : [clear, solid], startPoint: .leading, endPoint: .trailing)
            .frame(width: 80).allowsHitTesting(false)
    }
}

private struct FxRecommendedItem: View {
    let entry: EffectCatalogItem
    let store: EffectPreviewStore
    let action: () -> Void

    var body: some View {
        let name = fxPickerEntryName(entry)
        Button(action: action) {
            VStack(spacing: 4) {
                FxAfterThumb(entry: entry, store: store)
                    .frame(width: 115, height: 115)
                    .background(AureaColors.fxPanel)
                    .clipShape(RoundedRectangle(cornerRadius: 5))
                    .overlay(alignment: .topLeading) { if entry.isNew { FxNewBadge().padding(5) } }
                    .overlay(alignment: .topTrailing) { FxEffectBadge(entry: entry).offset(x: 5, y: -5) }
                    .padding(.top, 8)
                Text(name).font(.aurea(size: 10)).foregroundStyle(AureaColors.text)
                    .multilineTextAlignment(.center).lineLimit(2).frame(width: 117)
                Spacer(minLength: 0)
            }
            .frame(width: 121, height: 153)
            .contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle(shrink: 0.96))
            .accessibilityLabel(name)
            .accessibilityIdentifier("effects.featured." + fxEffectCardId(entry.typeId))
    }
}

/// RECENTES/FAVORITOS: faixa de 210 com páginas de 2 linhas e o indicador de 9
/// (relógio = Recentes, estrela = cada página de Favoritos). A página é lembrada.
/// Toque ADICIONA; toque longo abre o DETALHE.
private struct FxQuickBand: View {
    let pages: [(recent: Bool, items: [FxQuickItem])]
    let perPage: Int
    let hint: Bool
    @ObservedObject var prefs: FxEffectPrefs
    let store: EffectPreviewStore
    let onAdd: (EffectCatalogItem, Int) -> Void
    let onDetail: ([EffectCatalogItem], Int) -> Void
    @State private var page = 0
    @State private var restored = false

    private var total: Int { pages.count + (hint ? 1 : 0) }

    /// O detalhe pagina por todos os efeitos da faixa (um por efeito, sem repetir).
    private var detailList: [EffectCatalogItem] {
        var seen = Set<UInt32>()
        return pages.flatMap { $0.items.map(\.entry) }.filter { seen.insert($0.typeId).inserted }
    }

    var body: some View {
        VStack(spacing: 0) {
            TabView(selection: $page) {
                ForEach(0..<total, id: \.self) { index in
                    pageView(index).tag(index)
                }
            }
            .tabViewStyle(.page(indexDisplayMode: .never))
            .padding(.horizontal, 10).padding(.vertical, 5)
            indicator.padding(.bottom, 5)
        }
        .frame(height: 210)
        .background(AureaColors.fxListSurface)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("effects.quick")
        .onAppear {
            guard !restored else { return }
            restored = true
            page = min(max(prefs.quickPage, 0), max(total - 1, 0))
        }
        .onChange(of: page) { prefs.updateQuickPage($0) }
    }

    @ViewBuilder
    private func pageView(_ index: Int) -> some View {
        if index < pages.count {
            HStack(spacing: 0) {
                ForEach(0..<max(perPage / 2, 1), id: \.self) { column in
                    VStack(spacing: 0) {
                        Spacer(minLength: 0)
                        cell(index, column * 2)
                        Spacer(minLength: 0)
                        cell(index, column * 2 + 1)
                        Spacer(minLength: 0)
                    }.frame(maxWidth: .infinity)
                }
            }
        } else {
            Text(AureaText.t("om_fx_favorites_empty_star")).font(.aurea(size: 13)).foregroundStyle(AureaColors.fxTextSecondary)
                .multilineTextAlignment(.center).padding(.horizontal, 24)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }

    @ViewBuilder
    private func cell(_ pageIndex: Int, _ itemIndex: Int) -> some View {
        let items = pages[pageIndex].items
        if itemIndex < items.count {
            let item = items[itemIndex]
            FxQuickTile(item: item, recent: pages[pageIndex].recent, store: store,
                        onAdd: { onAdd(item.entry, item.preset) },
                        onDetail: {
                            let list = detailList
                            let position = list.firstIndex(where: { $0.typeId == item.entry.typeId }) ?? 0
                            onDetail(list, position)
                        })
        } else {
            Color.clear.frame(width: 62, height: 82)
        }
    }

    private var indicator: some View {
        HStack(spacing: 8) {
            ForEach(0..<total, id: \.self) { index in
                let recent = index < pages.count && pages[index].recent
                CupertinoGlyph.text(recent ? CupertinoGlyph.Clock : CupertinoGlyph.StarFill, size: 9,
                                    color: page == index ? AureaColors.text : AureaColors.fxDisabledText)
                    .frame(width: 9, height: 9)
                    .accessibilityElement()
                    .accessibilityLabel(AureaText.t("om_fx_quick_page", index + 1, total) + ", " + AureaText.t(recent ? "om_fx_recent" : "om_fx_favorites"))
                    .accessibilityAddTraits(page == index ? .isSelected : [])
                    .accessibilityIdentifier("effects.quick.page.\(index)")
            }
        }
    }
}

/// Item de 62×82: miniatura 55 (com o código da variação na base), selo e nome 9 em 2 linhas.
private struct FxQuickTile: View {
    let item: FxQuickItem
    let recent: Bool
    let store: EffectPreviewStore
    let onAdd: () -> Void
    let onDetail: () -> Void

    var body: some View {
        let name = fxPickerEntryName(item.entry)
        let code = fxPresetCode(item.preset)
        let id = fxEffectCardId(item.entry.typeId) + (item.preset >= 0 ? ".\(item.preset)" : "")
        VStack(spacing: 2) {
            FxAfterThumb(entry: item.entry, store: store)
                .frame(width: 55, height: 55)
                .background(AureaColors.fxPanel)
                .overlay(alignment: .bottom) {
                    if !code.isEmpty {
                        Text(code).font(.aurea(size: 10, weight: .semibold)).foregroundStyle(AureaColors.text)
                            .frame(maxWidth: .infinity)
                            .background(LinearGradient(colors: [AureaColors.fxPanel.opacity(0), AureaColors.fxPanel],
                                                       startPoint: .top, endPoint: .bottom))
                    }
                }
                .clipShape(RoundedRectangle(cornerRadius: 5))
                .overlay(alignment: .topTrailing) { FxEffectBadge(entry: item.entry).offset(x: 4, y: -4) }
                .padding(.top, 8)
            Text(name).font(.aurea(size: 9)).foregroundStyle(AureaColors.text)
                .multilineTextAlignment(.center).lineLimit(2).frame(width: 62)
            Spacer(minLength: 0)
        }
        .frame(width: 62, height: 82)
        .contentShape(Rectangle())
        .onTapGesture { UISelectionFeedbackGenerator().selectionChanged(); onAdd() }
        .onLongPressGesture(minimumDuration: EffectPickerLayout.longPress) {
            UIImpactFeedbackGenerator(style: .medium).impactOccurred(); onDetail()
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(AureaText.t("effects_card_add", code.isEmpty ? name : name + " " + code))
        .accessibilityAddTraits(.isButton)
        .accessibilityIdentifier((recent ? "effects.recent." : "effects.favorite.") + id)
        .accessibilityAction { onAdd() }
        .accessibilityAction(named: Text(AureaText.t("om_fx_menu_details"))) { onDetail() }
    }
}

/// O LADRILHO de uma categoria (65): prévia a 70 %, véu «painel» a 50 % e o nome 15 negrito.
private struct FxGroupTile: View {
    let group: FxEffectGroup
    let entries: [EffectCatalogItem]
    let store: EffectPreviewStore
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            ZStack {
                Group {
                    if let banner = fxGroupBannerEntry(group, entries) {
                        FxAfterThumb(entry: banner, store: store)
                    } else {
                        FxToolPlate(glyph: group.glyph, opacity: 0.35)
                    }
                }.opacity(0.7)
                AureaColors.fxPanel.opacity(0.5)
                Text(group.label).font(.aurea(size: 15, weight: .bold)).foregroundStyle(AureaColors.text)
                    .multilineTextAlignment(.center).lineLimit(2).padding(.horizontal, 8)
            }
            .frame(maxWidth: .infinity).frame(height: 65)
            .background(AureaColors.fxPanel)
            .clipped()
            .contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle(shrink: 1))
            .accessibilityLabel(AureaText.t("fx_browser_open_group", group.label))
            .accessibilityIdentifier("effects.category." + group.id)
    }
}

/// Dentro de uma categoria: a grade de 88 (⌊(L − 30) / 103⌋ colunas) e,
/// embaixo, anterior/próxima nas pontas (desabilitados nas pontas, trocam no lugar).
private struct FxGroupGrid: View {
    let group: FxEffectGroup
    let entries: [EffectCatalogItem]
    let store: EffectPreviewStore
    let previous: FxEffectGroup?
    let next: FxEffectGroup?
    let onDetail: ([EffectCatalogItem], Int) -> Void
    let onGroup: (FxEffectGroup) -> Void
    let onScrolled: (Bool) -> Void

    var body: some View {
        GeometryReader { geometry in
            let columns = Array(repeating: GridItem(.flexible(), spacing: 0, alignment: .top),
                                count: fxCategoryGridColumns(geometry.size.width))
            VStack(spacing: 0) {
                ScrollViewReader { proxy in
                    ScrollView {
                        FxScrollProbe(space: "fx.grid").id("topo")
                        if entries.isEmpty {
                            Text(AureaText.t("effect_empty_category")).font(.aurea(size: 14)).foregroundStyle(AureaColors.fxTextSecondary)
                                .multilineTextAlignment(.center).padding(24)
                        }
                        LazyVGrid(columns: columns, spacing: 0) {
                            ForEach(Array(entries.enumerated()), id: \.element.typeId) { index, entry in
                                FxGridCard(entry: entry, store: store) { onDetail(entries, index) }
                            }
                        }
                        .padding(.horizontal, 15).padding(.top, 4).padding(.bottom, 8)
                    }
                    .coordinateSpace(name: "fx.grid")
                    .onPreferenceChange(FxScrollOffsetKey.self) { onScrolled($0 < -0.5) }
                    .accessibilityIdentifier("effects.grid")
                    .onChange(of: group) { _ in proxy.scrollTo("topo", anchor: .top) }
                }
                HStack(spacing: 0) {
                    neighbor(previous, forward: false)
                    Spacer(minLength: 0)
                    neighbor(next, forward: true)
                }.frame(height: 50)
            }
        }
    }

    private func neighbor(_ target: FxEffectGroup?, forward: Bool) -> some View {
        let label = target.map { AureaText.t(forward ? "fx_browser_next_group" : "fx_browser_prev_group", $0.label) }
            ?? AureaText.t(forward ? "om_fx_category_next" : "om_fx_category_prev")
        return Button { if let target { onGroup(target) } } label: {
            CupertinoGlyph.text(forward ? CupertinoGlyph.ChevronRight : CupertinoGlyph.ChevronLeft, size: 20,
                                color: target == nil ? AureaColors.fxDisabledText : AureaColors.text)
                .mirrorsInRtl()
                .frame(width: AureaDims.minTap, height: 50).contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .disabled(target == nil)
        .accessibilityLabel(label)
        .accessibilityIdentifier(forward ? "effects.category.next" : "effects.category.prev")
    }
}

/// Cartão de 88: miniatura 80 (raio 5), selo no canto e nome 10 em 2 linhas.
private struct FxGridCard: View {
    let entry: EffectCatalogItem
    let store: EffectPreviewStore
    let action: () -> Void

    var body: some View {
        let name = fxPickerEntryName(entry)
        Button(action: action) {
            VStack(spacing: 4) {
                FxAfterThumb(entry: entry, store: store)
                    .frame(width: 80, height: 80)
                    .background(AureaColors.fxPanel)
                    .clipShape(RoundedRectangle(cornerRadius: 5))
                    .overlay(alignment: .topLeading) { if entry.isNew { FxNewBadge().padding(4) } }
                    .overlay(alignment: .topTrailing) { FxEffectBadge(entry: entry).offset(x: 4, y: -4) }
                    .padding(.top, 8)
                Text(name).font(.aurea(size: 10)).foregroundStyle(AureaColors.text)
                    .multilineTextAlignment(.center).lineLimit(2).frame(width: 88)
            }
            .frame(width: 88).padding(.bottom, 8)
            .contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle(shrink: 0.96))
            .accessibilityLabel(name)
            .accessibilityIdentifier("effects.card." + fxEffectCardId(entry.typeId))
    }
}

/// O cadeado de um efeito Hyper bloqueado (recurso pago; HyperEffectGate).
private struct FxHyperLockBadge: View {
    var body: some View {
        Image(systemName: "lock.fill").font(.system(size: 10, weight: .bold)).foregroundStyle(AureaColors.accent)
            .frame(width: 20, height: 20).background(AureaColors.imageScrim, in: Circle())
            .accessibilityLabel(AureaText.t("hyper_effect_locked"))
            .accessibilityIdentifier("effects.hyper.cadeado")
    }
}

/// O selo de 23 no canto: cadeado Hyper ou o "!" de efeito pesado.
private struct FxEffectBadge: View {
    let entry: EffectCatalogItem
    var body: some View {
        if HyperEffectGate.locked(typeId: entry.typeId) {
            FxHyperLockBadge()
        } else if fxEffectCost(entry.effectClass) >= 3 {
            Text("!").font(.aurea(size: 13, weight: .black)).foregroundStyle(AureaColors.fxDarkest)
                .frame(width: 23, height: 23).background(AureaColors.onImage, in: Circle())
                .accessibilityLabel(AureaText.t("fx_browser_heavy"))
        }
    }
}

/// O selo "Novo" do AUREA (efeito marcado como novo pelo motor).
private struct FxNewBadge: View {
    var body: some View {
        Text(AureaText.t("fx_browser_badge")).font(.aurea(size: 9, weight: .bold)).foregroundStyle(AureaColors.onAccent)
            .lineLimit(1).padding(.horizontal, 4).padding(.vertical, 1)
            .background(AureaColors.accent, in: RoundedRectangle(cornerRadius: 4))
    }
}

/// A prévia do motor no quadro, só a metade "depois" (sem rótulos). Ferramenta
/// mostra a cartela dela; sem prévia (desligada, gerando ou indisponível), a
/// cartela da categoria.
private struct FxAfterThumb: View {
    let entry: EffectCatalogItem
    let store: EffectPreviewStore
    /// "Prévias de efeitos" desligado em Ajustes (0.2.4): só a cartela, sem pedir render.
    @AppStorage(FxEffectPrefs.previewsEnabledKey) private var previewsEnabled = true
    @State private var image: UIImage?

    var body: some View {
        GeometryReader { geometry in
            if let tool = fxEffectToolOf(entry.typeId) {
                FxToolPlate(glyph: tool.glyph)
            } else if previewsEnabled, let cropped = fxAfterOnly(image, size: geometry.size) {
                Image(uiImage: cropped).resizable().interpolation(.medium).scaledToFill()
                    .frame(width: geometry.size.width, height: geometry.size.height).clipped()
            } else {
                FxGenericPlate(glyph: fxCategoryGlyph(entry.category))
            }
        }
        .task(id: "\(entry.typeId)-\(previewsEnabled)") {
            guard fxEffectToolOf(entry.typeId) == nil, previewsEnabled else { return }
            image = nil
            let loaded = await store.image(for: entry.typeId)
            if !Task.isCancelled { image = loaded }
        }
    }
}

/// A cartela de recuo (mesma cena do Android `GenericPlate`) com o glifo da categoria.
private struct FxGenericPlate: View {
    let glyph: Character
    var body: some View {
        Canvas { context, size in
            context.fill(Path(CGRect(origin: .zero, size: size)), with: .linearGradient(
                Gradient(colors: [AureaColors.effectPreviewTop, AureaColors.effectPreviewBottom]),
                startPoint: .zero, endPoint: CGPoint(x: 0, y: size.height)))
            let radius = size.width * 0.19
            context.fill(Path(ellipseIn: CGRect(x: size.width * 0.34 - radius, y: size.height * 0.60 - radius,
                                               width: radius * 2, height: radius * 2)), with: .color(AureaColors.effectPreviewDisc.opacity(0.85)))
        }.overlay(alignment: .bottomTrailing) {
            CupertinoGlyph.text(glyph, size: AureaDims.iconSm, color: AureaColors.onImage.opacity(0.85)).padding(AureaDims.s2)
        }
    }
}

/// Cartela das ferramentas (e dos grupos sem prévia): degradê do Aurea e o glifo no meio.
struct FxToolPlate: View {
    let glyph: Character
    var glyphSize: CGFloat = 26
    var opacity: Double = 1
    var body: some View {
        LinearGradient(colors: [AureaColors.actionDim, AureaColors.surfaceHigh], startPoint: .top, endPoint: .bottom)
            .overlay { CupertinoGlyph.text(glyph, size: glyphSize, color: AureaColors.accent.opacity(opacity)) }
    }
}

// =============================================================================
//  A BUSCA (espelho de EffectPicker.kt `SearchPane`): fundo «busca», campo
//  «campo» de raio 10 com 🔍, fichas de etiqueta, texto e ✕. Vazia: etiquetas
//  sugeridas em coluna; digitando: as que casam numa fileira e os resultados em
//  linhas. Toque → detalhe.
// =============================================================================

/// Caractere invisível antes do texto quando há fichas: apagá-lo (⌫) apaga a última ficha.
private let fxChipSentinel = "\u{200B}"

private struct FxSearchPane: View {
    @EnvironmentObject private var model: AureaModel
    let sorted: [EffectCatalogItem]
    @Binding var chips: [String]
    let onDetail: ([EffectCatalogItem], Int) -> Void
    let onClose: () -> Void
    @State private var text = ""
    @State private var index: FxSearchIndex?
    @FocusState private var focused: Bool

    var body: some View {
        let query = text.replacingOccurrences(of: fxChipSentinel, with: "")
        let labels = fxTagLabels()
        let available = availableTags()
        VStack(spacing: 0) {
            bar(query: query, labels: labels)
            if chips.isEmpty && query.trimmingCharacters(in: .whitespaces).isEmpty {
                suggestions(available, labels: labels)
            } else {
                let matching = fxMatchingTags(labels.filter { available.contains($0.id) }, query, exclude: chips)
                if !matching.isEmpty { matchingRow(matching, labels: labels) }
                resultsList(index.map { fxSearchResults(sorted, index: $0, query: query, chips: chips) } ?? [])
            }
        }
        .foregroundStyle(AureaColors.text)
        .background(AureaColors.fxSearch.ignoresSafeArea())
        .onAppear {
            index = fxEffectSearchIndex(sorted)
            text = chips.isEmpty ? "" : fxChipSentinel
            DispatchQueue.main.async { focused = true }
        }
        .onChange(of: text) { handleText($0) }
    }

    private func availableTags() -> [String] {
        guard let index else { return [] }
        return fxEffectTagIds.filter { tag in sorted.contains { (index.tags[$0.typeId] ?? []).contains(tag) } }
    }

    private func label(_ tag: String, _ labels: [(id: String, label: String)]) -> String {
        labels.first { $0.id == tag }?.label ?? tag
    }

    /// ⌫ apagou o caractere invisível: sai a última ficha (o texto fica).
    private func handleText(_ value: String) {
        guard !chips.isEmpty else {
            if value.contains(fxChipSentinel) { text = value.replacingOccurrences(of: fxChipSentinel, with: "") }
            return
        }
        if !value.contains(fxChipSentinel) {
            chips.removeLast()
            text = chips.isEmpty ? value : fxChipSentinel + value
        } else if !value.hasPrefix(fxChipSentinel) {
            text = fxChipSentinel + value.replacingOccurrences(of: fxChipSentinel, with: "")
        }
    }

    private func addChip(_ tag: String) {
        if !chips.contains(tag) { chips.append(tag) }
        text = fxChipSentinel
        focused = true
    }

    private func removeChip(_ tag: String) {
        let rest = text.replacingOccurrences(of: fxChipSentinel, with: "")
        chips.removeAll { $0 == tag }
        text = (chips.isEmpty ? "" : fxChipSentinel) + rest
    }

    private func bar(query: String, labels: [(id: String, label: String)]) -> some View {
        HStack(spacing: 0) {
            Button(action: onClose) {
                CupertinoGlyph.text(CupertinoGlyph.ChevronBack, size: 22).mirrorsInRtl()
                    .frame(width: AureaDims.minTap, height: AureaDims.minTap).contentShape(Rectangle())
            }.buttonStyle(AureaPressStyle(shrink: 1))
                .accessibilityLabel(AureaText.t("common_close"))
                .accessibilityIdentifier("effects.search.back")
            HStack(spacing: 0) {
                CupertinoGlyph.text(CupertinoGlyph.Search, size: 16, color: AureaColors.fxTextSecondary).padding(.trailing, 6)
                ForEach(chips, id: \.self) { tag in fieldChip(tag, label: label(tag, labels)) }
                TextField("", text: $text, prompt: Text(AureaText.t("effect_buscar_glitch_vhs_desfoque_cor")).foregroundColor(AureaColors.fxTextSecondary))
                    .font(.aurea(size: 15)).textInputAutocapitalization(.never).autocorrectionDisabled()
                    .textFieldStyle(.plain).tint(AureaColors.accent).focused($focused).submitLabel(.search)
                    .accessibilityIdentifier("effects.search.field")
                if !query.isEmpty || !chips.isEmpty {
                    Button {
                        chips = []
                        text = ""
                        focused = true
                    } label: {
                        CupertinoGlyph.text(CupertinoGlyph.XmarkCircleFill, size: 16, color: AureaColors.fxTextSecondary)
                            .frame(width: AureaDims.minTap, height: AureaDims.minTap).contentShape(Rectangle())
                    }
                    .accessibilityLabel(AureaText.t("om_fx_search_clear"))
                    .accessibilityIdentifier("effects.search.clear")
                }
            }
            .padding(.leading, 10).frame(height: AureaDims.minTap)
            .background(AureaColors.fxField, in: RoundedRectangle(cornerRadius: 10))
            .padding(.trailing, 10)
        }.frame(height: 52)
    }

    private func chipFace(_ label: String, horizontal: CGFloat) -> some View {
        Text(label).font(.aurea(size: 12)).foregroundStyle(AureaColors.text).lineLimit(1)
            .padding(.horizontal, horizontal).frame(height: 23)
            .background(AureaColors.fxListSurface, in: RoundedRectangle(cornerRadius: 10))
            .overlay(RoundedRectangle(cornerRadius: 10).stroke(AureaColors.fxDarkest, lineWidth: 0.8))
    }

    /// A ficha dentro do campo: tocar tira a etiqueta (o ⌫ também).
    private func fieldChip(_ tag: String, label: String) -> some View {
        Button { removeChip(tag) } label: {
            chipFace(label, horizontal: 10).padding(.trailing, 4)
                .frame(minHeight: AureaDims.minTap).contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle(shrink: 0.96))
            .fixedSize()
            .accessibilityLabel(AureaText.t("om_fx_search_tag_remove", label))
            .accessibilityIdentifier("effects.search.chip.\(tag)")
    }

    /// Etiqueta sugerida ou que casa: 23 de altura num alvo de 44.
    private func tagChip(_ tag: String, label: String) -> some View {
        Button { addChip(tag) } label: {
            chipFace(label, horizontal: 15).frame(minHeight: AureaDims.minTap).contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle(shrink: 0.96))
            .accessibilityLabel(label)
            .accessibilityIdentifier("effects.search.tag.\(tag)")
    }

    private func suggestions(_ available: [String], labels: [(id: String, label: String)]) -> some View {
        ScrollView {
            VStack(spacing: 0) {
                Text(AureaText.t("om_fx_search_tags")).font(.aurea(size: 12, weight: .semibold))
                    .foregroundStyle(AureaColors.fxTextSecondary).padding(.bottom, 6)
                    .accessibilityAddTraits(.isHeader)
                ForEach(available, id: \.self) { tag in tagChip(tag, label: label(tag, labels)) }
            }.frame(maxWidth: .infinity).padding(.vertical, 20)
        }.scrollDismissesKeyboard(.interactively)
    }

    private func matchingRow(_ matching: [String], labels: [(id: String, label: String)]) -> some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 6) {
                ForEach(matching, id: \.self) { tag in tagChip(tag, label: label(tag, labels)) }
            }.padding(.horizontal, 15)
        }.padding(.top, 4)
    }

    private func resultsList(_ results: [EffectCatalogItem]) -> some View {
        ScrollView {
            LazyVStack(spacing: 0) {
                if results.isEmpty {
                    Text(AureaText.t("effect_empty_search")).font(.aurea(size: 13)).foregroundStyle(AureaColors.fxTextSecondary)
                        .multilineTextAlignment(.center).frame(maxWidth: .infinity).padding(AureaDims.s4)
                }
                ForEach(Array(results.enumerated()), id: \.element.typeId) { position, entry in
                    FxResultRow(entry: entry, english: index?.english[entry.typeId], store: model.effectPreviews) {
                        focused = false
                        onDetail(results, position)
                    }
                }
            }
        }
        .scrollDismissesKeyboard(.interactively)
        .accessibilityIdentifier("effects.search.results")
    }
}

/// Linha de resultado: miniatura 80, nome 17, nome em inglês 14 (se diferente), descrição 11.
private struct FxResultRow: View {
    let entry: EffectCatalogItem
    let english: String?
    let store: EffectPreviewStore
    let action: () -> Void

    var body: some View {
        let name = fxPickerEntryName(entry)
        let second: String? = (english ?? "").isEmpty || english == name ? nil : english
        Button(action: action) {
            HStack(alignment: .top, spacing: 12) {
                FxAfterThumb(entry: entry, store: store)
                    .frame(width: 80, height: 80)
                    .background(AureaColors.fxPanel)
                    .clipShape(RoundedRectangle(cornerRadius: 5))
                    .overlay(alignment: .topLeading) { if entry.isNew { FxNewBadge().padding(4) } }
                    .overlay(alignment: .topTrailing) { FxEffectBadge(entry: entry).offset(x: 4, y: -4) }
                VStack(alignment: .leading, spacing: 1) {
                    Text(name).font(.aurea(size: 17)).lineLimit(1)
                    if let second {
                        Text(second).font(.aurea(size: 14)).foregroundStyle(AureaColors.fxTextSecondary).lineLimit(1)
                    }
                    Text(fxEffectDescription(entry.typeId, entry.category)).font(.aurea(size: 11))
                        .lineLimit(second == nil ? 3 : 2).padding(.top, 2)
                }
                .frame(maxWidth: .infinity, maxHeight: 80, alignment: .topLeading)
            }
            .foregroundStyle(AureaColors.text)
            .padding(.horizontal, 15).padding(.vertical, 8)
            .contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle(shrink: 1))
            .accessibilityLabel(name)
            .accessibilityIdentifier("effects.result." + fxEffectCardId(entry.typeId))
    }
}

// =============================================================================
//  O DETALHE (espelho de EffectsCatalogView.kt `EffectDetailOverlay`): véu de
//  30 %, caixa colada embaixo (80 % da altura, cantos de 8, «mais escuro") que
//  sobe em 200 ms, páginas deslizáveis com os efeitos da lista de origem. Ids
//  `effects.detail`, `effects.detail.close`, `effects.detail.variant.<id>[.<p>]`,
//  `effects.detail.favorite.<id>[.<p>]`, `effects.detail.tag.<tag>`.
// =============================================================================

/// Cantos de cima arredondados (o `UnevenRoundedRectangle` é só do iOS 17).
private struct FxTopRoundedShape: Shape {
    let radius: CGFloat
    func path(in rect: CGRect) -> Path {
        Path(UIBezierPath(roundedRect: rect, byRoundingCorners: [.topLeft, .topRight],
                          cornerRadii: CGSize(width: radius, height: radius)).cgPath)
    }
}

private struct FxDetailOverlay: View {
    let entries: [EffectCatalogItem]
    let startIndex: Int
    let closeSignal: Int
    let variants: Bool
    @ObservedObject var prefs: FxEffectPrefs
    let onClosed: () -> Void
    let onAdd: (EffectCatalogItem, Int) -> Void
    var onTag: ((String) -> Void)? = nil
    @State private var page = 0
    @State private var shown = false
    @State private var closing = false
    @State private var firstSignal = 0
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    var body: some View {
        GeometryReader { geometry in
            let bottomInset = geometry.safeAreaInsets.bottom
            let full = geometry.size.height + bottomInset
            ZStack(alignment: .bottom) {
                AureaColors.fxScrim
                    .opacity(shown ? 1 : 0)
                    .ignoresSafeArea()
                    .contentShape(Rectangle())
                    .onTapGesture { close() }
                    .accessibilityElement()
                    .accessibilityLabel(AureaText.t("om_fx_detail_close"))
                    .accessibilityAddTraits(.isButton)
                    .accessibilityAction { close() }
                detailPanel(height: full * 0.8, bottomInset: bottomInset)
                    .offset(y: shown ? 0 : full)
            }
            .ignoresSafeArea(.container, edges: .bottom)
        }
        .onAppear {
            page = min(max(startIndex, 0), max(entries.count - 1, 0))
            firstSignal = closeSignal
            if reduceMotion { shown = true } else { withAnimation(.easeInOut(duration: EffectPickerLayout.detailSeconds)) { shown = true } }
        }
        .onChange(of: closeSignal) { value in if value != firstSignal { close() } }
    }

    private func close() {
        guard !closing else { return }
        closing = true
        if reduceMotion { onClosed(); return }
        withAnimation(.easeInOut(duration: EffectPickerLayout.detailSeconds)) { shown = false }
        DispatchQueue.main.asyncAfter(deadline: .now() + EffectPickerLayout.detailSeconds) { onClosed() }
    }

    private func detailPanel(height: CGFloat, bottomInset: CGFloat) -> some View {
        ZStack(alignment: .topLeading) {
            TabView(selection: $page) {
                ForEach(Array(entries.enumerated()), id: \.element.typeId) { index, entry in
                    FxDetailPage(entry: entry, index: index, count: entries.count, active: page == index, variants: variants,
                                 prefs: prefs, onAdd: onAdd, onTag: onTag, bottomInset: bottomInset)
                        .tag(index)
                }
            }
            .tabViewStyle(.page(indexDisplayMode: .never))
            Button { close() } label: {
                CupertinoGlyph.text(CupertinoGlyph.Xmark, size: 20)
                    .frame(width: AureaDims.minTap, height: AureaDims.minTap).contentShape(Rectangle())
            }.buttonStyle(AureaPressStyle(shrink: 1))
                .accessibilityLabel(AureaText.t("om_fx_detail_close"))
                .accessibilityIdentifier("effects.detail.close")
        }
        .frame(maxWidth: .infinity)
        .frame(height: height)
        .background(AureaColors.fxDarkest)
        .clipShape(FxTopRoundedShape(radius: 8))
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("effects.detail")
    }
}

/// Uma página do detalhe: prévia 1:1 com o texto na base e, a ¾ dela, as
/// variações (ou, no ⋮ "Detalhes", os parâmetros). Embaixo, os extras do AUREA.
private struct FxDetailPage: View {
    @EnvironmentObject private var model: AureaModel
    let entry: EffectCatalogItem
    let index: Int
    let count: Int
    let active: Bool
    let variants: Bool
    @ObservedObject var prefs: FxEffectPrefs
    let onAdd: (EffectCatalogItem, Int) -> Void
    var onTag: ((String) -> Void)?
    var bottomInset: CGFloat = 0
    @AppStorage(FxEffectPrefs.previewsEnabledKey) private var previewsEnabled = true
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @State private var scene: FxPreviewScene = .photo
    @State private var frames: [UIImage] = []
    @State private var loading = false
    @State private var failed = false

    private var animates: Bool {
        previewsEnabled && !reduceMotion && !UIAccessibility.isReduceMotionEnabled && active && fxEffectToolOf(entry.typeId) == nil
    }

    var body: some View {
        GeometryReader { geometry in
            let side = geometry.size.width
            ScrollView {
                VStack(spacing: 0) {
                    ZStack(alignment: .top) {
                        preview.frame(width: side, height: side)
                        textBlock.frame(width: side, height: side * 0.75, alignment: .bottomLeading)
                        list.padding(.top, side * 0.75)
                    }
                    extras
                }
            }
        }
        .task(id: "\(entry.typeId)-\(scene.rawValue)-\(animates)") {
            frames = []; failed = false
            guard animates else { loading = false; return }   // nada é pedido ao motor
            loading = true
            let loaded = await model.effectPreviews.sequence(for: entry.typeId, scene: scene, generate: true)
            if Task.isCancelled { return }
            loading = false
            if let loaded, !loaded.isEmpty { frames = loaded } else { failed = true }
        }
        .accessibilityValue(AureaText.t("om_fx_detail_page", index + 1, count))
    }

    private var preview: some View {
        let name = fxPickerEntryName(entry)
        let veil = AureaColors.fxSearch
        return ZStack {
            if let tool = fxEffectToolOf(entry.typeId) {
                FxToolPlate(glyph: tool.glyph, glyphSize: 56)
            } else if animates, !frames.isEmpty {
                FxAnimatedFrames(frames: frames, afterOnly: true)
            } else {
                FxAfterThumb(entry: entry, store: model.effectPreviews)
            }
            LinearGradient(stops: [.init(color: veil.opacity(0), location: 0), .init(color: veil.opacity(0), location: 0.3),
                                   .init(color: veil.opacity(0.7), location: 0.7), .init(color: veil.opacity(0.7), location: 1)],
                           startPoint: .top, endPoint: .bottom)
            if animates && loading {
                Text(AureaText.t("fx_preview_animating")).font(.aurea(size: 11, weight: .semibold)).foregroundStyle(AureaColors.text)
                    .padding(.horizontal, 6).padding(.vertical, 3)
                    .background(AureaColors.fxScrim, in: RoundedRectangle(cornerRadius: 6))
                    .frame(maxHeight: .infinity, alignment: .top).padding(.top, 12)
            }
        }
        .clipped()
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(animates && !frames.isEmpty
            ? AureaText.t("fx_preview_animated_desc", name, AureaText.t(scene.labelKey))
            : AureaText.t("fx_preview_still_desc", name))
        .accessibilityAddTraits(.isImage)
    }

    private var textBlock: some View {
        let name = fxPickerEntryName(entry)
        let english = fxPickerEntryEnglishName(entry)
        let tags = fxEffectTags(entry)
        return VStack(alignment: .leading, spacing: 2) {
            Text(name).font(.aurea(size: 20, weight: .bold)).lineLimit(2).accessibilityAddTraits(.isHeader)
            if english != name {
                Text(english).font(.aurea(size: 18)).foregroundStyle(AureaColors.fxTextSecondary).lineLimit(1)
            }
            Text(fxEffectDescription(entry.typeId, entry.category)).font(.aurea(size: 16)).lineLimit(10)
                .fixedSize(horizontal: false, vertical: true).padding(.top, 4)
            if !tags.isEmpty {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 6) { ForEach(tags, id: \.self) { tag in tagChip(tag) } }
                }
            }
        }
        .foregroundStyle(AureaColors.text)
        .padding(.horizontal, 16).padding(.bottom, 6)
    }

    /// A etiqueta do detalhe (31 num alvo de 44): tocar abre a busca com ela como ficha.
    @ViewBuilder
    private func tagChip(_ tag: String) -> some View {
        let label = AureaText.t(fxEffectTagLabelKey(tag))
        let face = Text(label).font(.aurea(size: 13)).foregroundStyle(AureaColors.text).lineLimit(1)
            .padding(.horizontal, 12).frame(height: 31)
            .background(AureaColors.fxListSurface, in: RoundedRectangle(cornerRadius: 10))
            .overlay(RoundedRectangle(cornerRadius: 10).stroke(AureaColors.fxDarkest, lineWidth: 0.8))
            .frame(minHeight: AureaDims.minTap)
        if let onTag {
            Button { onTag(tag) } label: { face.contentShape(Rectangle()) }
                .buttonStyle(AureaPressStyle(shrink: 0.96))
                .accessibilityLabel(label)
                .accessibilityIdentifier("effects.detail.tag.\(tag)")
        } else {
            face.accessibilityLabel(label).accessibilityIdentifier("effects.detail.tag.\(tag)")
        }
    }

    @ViewBuilder
    private var list: some View {
        if variants { variantList } else { paramList }
    }

    /// As VARIAÇÕES: a 1ª linha é o efeito base, depois um preset do motor por linha.
    private var variantList: some View {
        let presets = fxEffectToolOf(entry.typeId) == nil ? model.engine.effectPresets(entry.typeId) : []
        let id = fxEffectCardId(entry.typeId)
        let base = FxEffectRef(entry.typeId)
        return VStack(spacing: 0) {
            Color.clear.frame(height: 0)
                .accessibilityElement().accessibilityLabel(AureaText.t("om_fx_detail_variations")).accessibilityAddTraits(.isHeader)
            FxVariantRow(code: AureaText.t("om_fx_detail_base"), name: fxPickerEntryName(entry), favorite: prefs.isFavorite(base),
                         rowId: "effects.detail.variant.\(id)", starId: "effects.detail.favorite.\(id)",
                         onAdd: { onAdd(entry, -1) }, onFavorite: { _ = prefs.toggleFavorite(base) })
            ForEach(0..<(presets.count / 2), id: \.self) { preset in
                FxVariantRow(code: fxPresetCode(preset), name: fxPresetName(entry.typeId, presets[preset * 2], presets[preset * 2 + 1]),
                             favorite: prefs.isFavorite(FxEffectRef(entry.typeId, preset: preset)),
                             rowId: "effects.detail.variant.\(id).\(preset)", starId: "effects.detail.favorite.\(id).\(preset)",
                             onAdd: { onAdd(entry, preset) },
                             onFavorite: { _ = prefs.toggleFavorite(FxEffectRef(entry.typeId, preset: preset)) })
            }
        }
    }

    /// ⋮ "Detalhes do efeito": no lugar das variações, os parâmetros do AUREA.
    private var paramList: some View {
        let specs = model.engine.effectSpecs(entry.typeId).map { fxLocalizedSpec(entry.typeId, $0) }
        return VStack(spacing: 0) {
            if !specs.isEmpty {
                Text(AureaText.t("effect_parameters")).font(.aurea(size: 12, weight: .semibold))
                    .foregroundStyle(AureaColors.fxTextSecondary)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.horizontal, 16).padding(.vertical, 8)
                    .background(AureaColors.fxField)
                    .accessibilityAddTraits(.isHeader)
                ForEach(specs.indices, id: \.self) { i in
                    HStack(spacing: 12) {
                        Text(specs[i]["label"] as? String ?? "").font(.aurea(size: 15))
                            .frame(maxWidth: .infinity, alignment: .leading)
                        Text(fxParamSummary(specs[i])).font(.aurea(size: 13)).foregroundStyle(AureaColors.fxTextSecondary)
                            .multilineTextAlignment(.trailing)
                    }
                    .padding(.horizontal, 16).padding(.vertical, 6)
                    .frame(minHeight: AureaDims.minTap)
                    .background(AureaColors.fxField)
                    .overlay(alignment: .top) { AureaColors.fxDarkest.frame(height: 0.5) }
                }
            }
        }
    }

    /// Extras do AUREA: o seletor de cena da prévia animada e onde o efeito funciona.
    private var extras: some View {
        VStack(alignment: .leading, spacing: 8) {
            if animates {
                FxPreviewScenePicker(selection: $scene)
                if failed {
                    Text(AureaText.t("fx_preview_animation_unavailable")).font(.aurea(size: 11))
                        .foregroundStyle(AureaColors.fxTextSecondary).fixedSize(horizontal: false, vertical: true)
                }
            }
            Text(fxEffectCostLine(entry.category, fxEffectCost(entry.effectClass)) + " · " + fxEffectCompatibilityLine(entry.typeId))
                .font(.aurea(size: 12)).foregroundStyle(AureaColors.fxTextSecondary)
                .fixedSize(horizontal: false, vertical: true)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.horizontal, 16).padding(.top, 12).padding(.bottom, 12 + bottomInset)
    }
}

/// Linha de 70,5: "+" · código sobre o nome · divisória · estrela 56×56.
private struct FxVariantRow: View {
    let code: String
    let name: String
    let favorite: Bool
    let rowId: String
    let starId: String
    let onAdd: () -> Void
    let onFavorite: () -> Void

    var body: some View {
        HStack(spacing: 0) {
            Button(action: onAdd) {
                HStack(spacing: 14) {
                    CupertinoGlyph.text(CupertinoGlyph.Plus, size: 18)
                    VStack(alignment: .leading, spacing: 1) {
                        Text(code).font(.aurea(size: 12)).foregroundStyle(AureaColors.fxTextSecondary).lineLimit(1)
                        Text(name).font(.aurea(size: 17, weight: .bold)).foregroundStyle(AureaColors.text).lineLimit(1)
                    }
                    Spacer(minLength: 0)
                }
                .padding(.leading, 16).padding(.trailing, 12)
                .frame(maxHeight: .infinity)
                .contentShape(Rectangle())
            }.buttonStyle(AureaPressStyle(shrink: 1))
                .accessibilityLabel(AureaText.t("om_fx_detail_add_variant", name))
                .accessibilityIdentifier(rowId)
            AureaColors.fxLowSurface.frame(width: 1).padding(.vertical, 12)
            Button(action: onFavorite) {
                CupertinoGlyph.text(favorite ? CupertinoGlyph.StarFill : CupertinoGlyph.Star, size: 22,
                                    color: favorite ? AureaColors.accent : AureaColors.fxTextSecondary)
                    .frame(width: 56, height: 56).contentShape(Rectangle())
            }.buttonStyle(AureaPressStyle(shrink: 0.9))
                .accessibilityLabel(AureaText.t(favorite ? "om_fx_favorite_remove" : "om_fx_favorite_add"))
                .accessibilityIdentifier(starId)
        }
        .frame(height: 70.5)
        .background(AureaColors.fxField)
        .overlay(alignment: .top) { AureaColors.fxDarkest.frame(height: 0.5) }
    }
}

/// O que o controle faz, em linguagem de edição: "0 a 100 %", "Cor", "Liga/desliga"…
func fxParamSummary(_ spec: [String: Any]) -> String {
    let enums = spec["enumLabels"] as? [String] ?? []
    if !enums.isEmpty { return enums.prefix(3).joined(separator: ", ") + (enums.count > 3 ? "…" : "") }
    switch (spec["type"] as? NSNumber)?.intValue {
    case fxParamColor: return AureaText.t("ds_cor")
    case fxParamBool: return AureaText.t("effect_param_bool")
    case fxParamPoint2D: return AureaText.t("effect_param_point")
    default:
        let low = (spec["min"] as? NSNumber)?.floatValue ?? 0
        let high = (spec["max"] as? NSNumber)?.floatValue ?? 0
        let unit = spec["unit"] as? String ?? ""
        return low.isFinite && high.isFinite && high > low
            ? AureaText.t("fx_param_range", fxTrimNumber(low), fxTrimNumber(high)) + (unit.isEmpty ? "" : " " + unit) : unit
    }
}

private func fxTrimNumber(_ value: Float) -> String {
    if value.rounded() == value { return String(format: "%.0f", locale: Locale(identifier: "en_US_POSIX"), Double(value)) }
    return String(format: "%.2f", locale: Locale(identifier: "en_US_POSIX"), Double(value))
        .replacingOccurrences(of: "0+$", with: "", options: .regularExpression)
        .replacingOccurrences(of: "\\.$", with: "", options: .regularExpression)
}

// =============================================================================
//  PRÉVIA ANIMADA (port de effects/EffectAnimatedPreview.kt): o detalhe anima,
//  com seletor de cena. Nada anima com "Reduzir movimento" ou com "Prévias de
//  efeitos" desligado.
// =============================================================================

/// Os quadros em laço (~15 qps); parado, o primeiro. `afterOnly`: só a metade
/// "depois" da comparação, sem rótulos (o detalhe da Omega).
struct FxAnimatedFrames: View {
    let frames: [UIImage]
    var playing: Bool = true
    var afterOnly = false

    var body: some View {
        GeometryReader { geometry in
            SwiftUI.TimelineView(.periodic(from: .now, by: EffectPreviewStore.sequenceFrameSeconds)) { context in
                let index = playing && frames.count > 1
                    ? Int(context.date.timeIntervalSinceReferenceDate / EffectPreviewStore.sequenceFrameSeconds) % frames.count
                    : 0
                let frame = frames[min(index, frames.count - 1)]
                if afterOnly {
                    Image(uiImage: fxAfterOnly(frame, size: geometry.size) ?? frame).resizable().interpolation(.medium).scaledToFill()
                        .frame(width: geometry.size.width, height: geometry.size.height).clipped()
                } else {
                    Image(uiImage: frame).resizable().interpolation(.medium).scaledToFit()
                        .frame(width: geometry.size.width, height: geometry.size.height).clipped()
                }
            }
        }
        .overlay(alignment: .bottom) {
            if !afterOnly {
                HStack {
                    fxPreviewLabel("fx_browser_before")
                    Spacer()
                    fxPreviewLabel("fx_browser_after")
                }.padding(6).environment(\.layoutDirection, .leftToRight)
            }
        }
        .accessibilityHidden(true)
    }
}

func fxPreviewLabel(_ key: String) -> some View {
    Text(AureaText.t(key)).font(.aurea(size: 10.5, weight: .semibold)).foregroundStyle(AureaColors.onImage)
        .padding(.horizontal, 5).padding(.vertical, 2)
        .background(AureaColors.imageScrim, in: RoundedRectangle(cornerRadius: 6))
}

/// Seletor de cena do detalhe: um botão por cena, o escolhido marcado para o VoiceOver.
struct FxPreviewScenePicker: View {
    @Binding var selection: FxPreviewScene

    var body: some View {
        HStack(spacing: AureaDims.s2) {
            ForEach(FxPreviewScene.allCases) { scene in
                let on = scene == selection
                Button { selection = scene } label: {
                    // 0.3.3 tema: conteúdo sobre o destaque (accent) é o onAccent.
                    Text(AureaText.t(scene.labelKey)).font(.aurea(size: 12, weight: .semibold))
                        .foregroundStyle(on ? AureaColors.onAccent : AureaColors.text)
                        .multilineTextAlignment(.center).lineLimit(2)
                        .frame(maxWidth: .infinity, minHeight: AureaDims.minTap)
                        .background(on ? AureaColors.accent : AureaColors.chip,
                                    in: RoundedRectangle(cornerRadius: AureaDims.radiusChip))
                }
                .buttonStyle(AureaPressStyle())
                .accessibilityLabel(AureaText.t(scene.labelKey))
                .accessibilityAddTraits(on ? [.isSelected] : [])
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel(AureaText.t("fx_preview_scene_label"))
    }
}

// =============================================================================
//  "DETALHES DO EFEITO" (⋮ de um efeito da camada; EffectsCatalogView.kt
//  `EffectDetailSheet` com onApply nulo): a mesma página do detalhe do
//  navegador, SEM as variações — no lugar delas, os parâmetros do AUREA. A
//  estrela do efeito fica no alto, à direita.
// =============================================================================

struct EffectAboutSheet: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject var prefs: FxEffectPrefs
    let entry: EffectCatalogItem
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        let favorite = prefs.isFavorite(entry.typeId)
        ZStack(alignment: .top) {
            FxDetailPage(entry: entry, index: 0, count: 1, active: true, variants: false, prefs: prefs,
                         onAdd: { _, _ in }, onTag: nil)
            HStack(spacing: 0) {
                Button { dismiss() } label: {
                    CupertinoGlyph.text(CupertinoGlyph.Xmark, size: 20)
                        .frame(width: AureaDims.minTap, height: AureaDims.minTap).contentShape(Rectangle())
                }.buttonStyle(AureaPressStyle(shrink: 1))
                    .accessibilityLabel(AureaText.t("om_fx_detail_close"))
                    .accessibilityIdentifier("effects.detail.close")
                Spacer(minLength: 0)
                Button { _ = prefs.toggleFavorite(entry.typeId) } label: {
                    CupertinoGlyph.text(favorite ? CupertinoGlyph.StarFill : CupertinoGlyph.Star, size: 22,
                                        color: favorite ? AureaColors.accent : AureaColors.text)
                        .frame(width: AureaDims.minTap, height: AureaDims.minTap).contentShape(Rectangle())
                }.buttonStyle(AureaPressStyle(shrink: 0.9))
                    .accessibilityLabel(AureaText.t(favorite ? "om_fx_favorite_remove" : "om_fx_favorite_add"))
                    .accessibilityIdentifier("effects.detail.favorite." + fxEffectCardId(entry.typeId))
            }
        }
        .foregroundStyle(AureaColors.text)
        .background(AureaColors.fxDarkest.ignoresSafeArea())
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("effects.detail")
    }
}

// =============================================================================
//  PLUGINS DE EFEITO (0.3.0, PluginManagerSheet.kt): instalar um .aureaplugin ou
//  um shader ISF (.fs), ver o que está instalado (autor, versão, se funciona neste
//  aparelho) e remover. O guia de criação completo mora em docs/plugins.
// =============================================================================
struct PluginManagerView: View {
    @EnvironmentObject private var model: AureaModel
    @State private var removing: FxPluginInfo?
    @State private var help = false
    private var tag: String { FxPluginTexts.appTag }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 8) {
                Text(AureaText.t("plug_title")).font(.aurea(size: 17, weight: .semibold))
                Text(AureaText.t("plug_hint")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                Button {
                    DocumentImportPicker.present(types: [UTType(filenameExtension: "aureaplugin") ?? .data, UTType(filenameExtension: "fs") ?? .data, .zip, .data]) { urls in
                        if let url = urls?.first { model.installPlugin(url: url) }
                    }
                } label: {
                    Text(AureaText.t("plug_install")).font(.aurea(size: 15, weight: .semibold)).foregroundStyle(AureaColors.onAccent)
                        .frame(maxWidth: .infinity, minHeight: 46).background(AureaColors.accent, in: RoundedRectangle(cornerRadius: 12))
                }.buttonStyle(.plain).disabled(model.importingMedia).accessibilityIdentifier("plugins.install")
                if model.importingMedia { Text(model.operationMessage).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted) }
                if model.plugins.isEmpty {
                    Text(AureaText.t("plug_empty")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted).padding(.vertical, 8)
                }
                ForEach(model.plugins) { p in row(p) }
                Text(AureaText.t("plug_safety")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                Button { help.toggle() } label: {
                    Text(AureaText.t("plug_help_title")).font(.aurea(size: 14, weight: .semibold)).foregroundStyle(AureaColors.accent)
                }.buttonStyle(.plain).padding(.top, 6).accessibilityIdentifier("plugins.help")
                if help { Text(AureaText.t("plug_help_body")).font(.aurea(size: 13)).fixedSize(horizontal: false, vertical: true) }
            }
            .foregroundStyle(AureaColors.text)
            .padding(.horizontal, 18).padding(.vertical, 16)
        }
        .background(AureaColors.surface.ignoresSafeArea())
        .onAppear { model.refreshPlugins() }
        .alert(AureaText.t("plug_remove_title", removing.flatMap { FxPluginTexts.pick($0.names, tag) } ?? removing?.id ?? ""),
               isPresented: Binding(get: { removing != nil }, set: { if !$0 { removing = nil } })) {
            Button(AureaText.t("plug_remove"), role: .destructive) { if let p = removing { model.removePlugin(id: p.id) }; removing = nil }
            Button(AureaText.t("common_cancel"), role: .cancel) { removing = nil }
        } message: {
            Text(AureaText.t("plug_remove_body"))
        }
    }

    private func row(_ p: FxPluginInfo) -> some View {
        HStack(alignment: .center, spacing: 10) {
            VStack(alignment: .leading, spacing: 2) {
                Text(FxPluginTexts.pick(p.names, tag) ?? p.id).font(.aurea(size: 14, weight: .semibold))
                Text(AureaText.t("plug_by", p.author.isEmpty ? p.id : p.author, p.version) + " · "
                     + AureaText.t(p.kind == "stack" ? "plug_kind_stack" : (p.scene3d ? "plug_kind_shader3d" : "plug_kind_shader")))
                    .font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                if let d = FxPluginTexts.pick(p.descriptions, tag), !d.isEmpty { Text(d).font(.aurea(size: 12)) }
                if !p.ok {
                    Text(AureaText.t("plug_error", p.error)).font(.aurea(size: 12)).foregroundStyle(AureaColors.warning)
                } else if p.testRenderMs > 0 {
                    Text(AureaText.t("plug_render_ms", String(format: "%.1f", p.testRenderMs))).font(.aurea(size: 11)).foregroundStyle(AureaColors.muted)
                }
            }.frame(maxWidth: .infinity, alignment: .leading)
            Button { removing = p } label: {
                Text(AureaText.t("plug_remove")).font(.aurea(size: 13)).foregroundStyle(AureaColors.danger)
            }.buttonStyle(.plain).accessibilityIdentifier("plugins.remove.\(p.id)")
        }
        .padding(12).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 12))
        .accessibilityIdentifier("plugins.row.\(p.id)")
    }
}
