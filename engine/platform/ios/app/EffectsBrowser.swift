// Android spec: effects/EffectPicker.kt, effects/EffectPickerLogic.kt and
// effects/EffectsCatalogView.kt (a ficha). A tela cheia "Adicionar efeito"
// (destaques, recentes, ladrilhos de categoria, busca) e as ferramentas-efeito.
import SwiftUI
import UIKit

private enum EffectPickerLayout {
    static let cardMin: CGFloat = 100
    static let rowCard: CGFloat = 92
    static let previewAspect: CGFloat = 1.6
    static let favoriteBadge: CGFloat = 22
    static let nameSize: CGFloat = 12
    static let nameLineHeight: CGFloat = 14.4
    static let fieldGap: CGFloat = 6
    static let longPress: Double = 0.45
    static let parameterPadding: CGFloat = 7
    static let backTarget: CGFloat = 48
    static let description = Font.aurea(size: 14)
}

// =============================================================================
//  A LÓGICA do escolhedor (espelho de EffectPickerLogic.kt). Tudo sai do
//  CATÁLOGO que o motor publica: efeito ou categoria novos aparecem sozinhos.
// =============================================================================

/// As duas abas do painel de efeitos.
enum FxEffectsTab { case applied, add }

/// Camada com efeito abre na pilha; camada sem efeito abre direto no catálogo.
func fxInitialEffectsTab(_ effectCount: Int) -> FxEffectsTab { effectCount > 0 ? .applied : .add }

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
func fxPickableEffects(_ catalog: [EffectCatalogItem], layerHasAudio: Bool) -> [EffectCatalogItem] {
    catalog.filter { !["aurea.motion.oscillate", "aurea.layout.grid_builder", "aurea.layout.grid_item", "aurea.light.scene_flare"].map(fxEffectTypeId).contains($0.typeId) && (layerHasAudio || !fxIsAudioCategory($0.category) || fxMakesSound($0)) }
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

/// Todo termo tem de aparecer; a ordem é por relevância e, no empate, a do
/// catálogo: 0 nome igual · 1 nome começa · 2 palavra do nome começa · 3 todos
/// os termos no nome · 4 achado no resto.
func fxSearchEffects(_ sorted: [EffectCatalogItem], _ docs: [UInt32: FxSearchDoc], _ query: String) -> [EffectCatalogItem] {
    let q = fxNormalizeSearch(query).split(whereSeparator: { $0.isWhitespace }).joined(separator: " ")
    guard !q.isEmpty else { return [] }
    let terms = q.split(separator: " ").map(String.init)
    var ranked: [(rank: Int, order: Int, entry: EffectCatalogItem)] = []
    for (order, entry) in sorted.enumerated() {
        guard let doc = docs[entry.typeId], terms.allSatisfy({ doc.text.contains($0) }) else { continue }
        let rank: Int
        if doc.names.contains(q) { rank = 0 }
        else if doc.names.contains(where: { $0.hasPrefix(q) }) { rank = 1 }
        else if doc.names.contains(where: { name in name.split(separator: " ").contains { word in word.hasPrefix(q) } }) { rank = 2 }
        else if doc.names.contains(where: { name in terms.allSatisfy { name.contains($0) } }) { rank = 3 }
        else { rank = 4 }
        ranked.append((rank, order, entry))
    }
    return ranked.sorted { ($0.rank, $0.order) < ($1.rank, $1.order) }.map(\.entry)
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
/// descrição); nos 7 idiomas, o nome traduzido, os sinônimos da tabela e a
/// categoria. Montado uma vez por idioma/catálogo: a troca de idioma abaixo é
/// síncrona, no main thread, e volta ao idioma do app antes de sair.
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
    var label: String {
        switch self {
        case .captions: return AureaText.t("fxui_tool_captions")
        case .cameraTrack: return AureaText.t("fxui_tool_camera_track")
        case .mask: return AureaText.t("fxui_tool_mask")
        }
    }
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

/// ABRE uma ferramenta-efeito na camada escolhida: a única porta que o resto do
/// app precisa (catálogo, pilha da camada, busca de comandos).
@MainActor
func fxOpenEffectTool(_ tool: FxEffectTool, in model: AureaModel) { model.openPanel(tool.panel) }

// =============================================================================
//  Os GRUPOS da tela "Adicionar efeito" (espelho de EffectGroup no Android): o
//  motor publica categorias em português e em inglês misturadas; a tela junta
//  tudo em grupos de gente, nesta ordem. `id` = `effects.category.<id>`.
// =============================================================================

enum FxEffectGroup: String, CaseIterable {
    case colorLight = "color_light", blur, distort, motion, stylize, glitch
    case drawEdge = "draw_edge", procedural, matte, time, text
    case threeD = "3d", audio, utility, other
    var id: String { rawValue }
    var label: String {
        switch self {
        case .colorLight: return AureaText.t("fxui_group_color_light")
        case .blur: return AureaText.t("fxui_group_blur")
        case .distort: return AureaText.t("fxui_group_distort")
        case .motion: return AureaText.t("fxui_group_motion")
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
        case .other: return AureaText.t("fxui_group_other")
        }
    }
    var glyph: Character {
        switch self {
        case .colorLight: return CupertinoGlyph.Sparkles
        case .blur: return CupertinoGlyph.DropFill
        case .distort, .motion: return CupertinoGlyph.Move
        case .stylize: return CupertinoGlyph.SquareGrid2x2
        case .glitch: return CupertinoGlyph.Bolt
        case .drawEdge: return CupertinoGlyph.PencilOutline
        case .procedural: return CupertinoGlyph.WandStars
        case .matte: return CupertinoGlyph.Scissors
        case .time: return CupertinoGlyph.Timer
        case .text: return CupertinoGlyph.CaptionsBubble
        case .audio: return CupertinoGlyph.MusicNote
        case .utility: return CupertinoGlyph.SliderHorizontal3
        case .threeD, .other: return CupertinoGlyph.WandStars
        }
    }
}

private let fxGroupByCategory: [String: FxEffectGroup] = {
    var map: [String: FxEffectGroup] = [:]
    for c in ["cor", "color", "colour", "luz", "light", "glow e luz"] { map[c] = .colorLight }
    for c in ["desfoque", "blur", "nitidez", "sharpen"] { map[c] = .blur }
    for c in ["distorcer", "distort", "distorcao"] { map[c] = .distort }
    for c in ["transform", "transformar", "movimento", "motion", "rastreio", "tracking"] { map[c] = .motion }
    for c in ["estilizar", "stylize", "stylise"] { map[c] = .stylize }
    map["glitch"] = .glitch
    for c in ["gerar", "generate", "pattern", "ruido", "noise"] { map[c] = .procedural }
    for c in ["recorte", "keying", "key", "mascara", "matte"] { map[c] = .matte }
    for c in ["tempo", "time", "transicao", "transition"] { map[c] = .time }
    for c in ["texto", "text"] { map[c] = .text }
    map["3d"] = .threeD
    for c in ["audio", "som", "sound"] { map[c] = .audio }
    for c in ["utilitario", "utility", "controles de expressao", "expression controls"] { map[c] = .utility }
    return map
}()

private let fxGroupByKey: [UInt32: FxEffectGroup] = {
    var map: [UInt32: FxEffectGroup] = [:]
    for key in ["aurea.stylize.stroke_outline", "aurea.stylize.border", "aurea.stylize.drop_shadow",
                "aurea.stylize.find_edges", "aurea.stylize.bevel_alpha"] { map[fxEffectTypeId(key)] = .drawEdge }
    for key in ["aurea.transform", "aurea.motion.oscillate.cycles", "aurea.motion.swing", "aurea.motion.wiggle",
                "aurea.motion.twitch", "aurea.distort.shake", "aurea.distort.corner_pin", "aurea.transform.parenting_helper"] {
        map[fxEffectTypeId(key)] = .motion
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

private let fxGroupBannerKeys: [FxEffectGroup: String] = [
    .colorLight: "aurea.color.colorama", .blur: "aurea.blur.radial", .distort: "aurea.distort.wave_warp",
    .motion: "aurea.distort.corner_pin", .stylize: "aurea.stylize.halftone", .glitch: "aurea.glitch.glitchify",
    .drawEdge: "aurea.stylize.find_edges", .procedural: "aurea.generate.fractal_noise", .matte: "aurea.key.chroma",
    .time: "aurea.time.warp_rgb",
]

/// A entrada que dá a prévia do ladrilho: a escolhida, ou o primeiro EFEITO do grupo.
func fxGroupBannerEntry(_ group: FxEffectGroup, _ entries: [EffectCatalogItem]) -> EffectCatalogItem? {
    let preferred = fxGroupBannerKeys[group].map(fxEffectTypeId)
    return entries.first { $0.typeId == preferred } ?? entries.first { fxEffectToolOf($0.typeId) == nil }
}

private let fxFeaturedKeys = [
    FxEffectTool.captions.key, "aurea.light.deep_glow", "aurea.glitch.vhs", "aurea.stylize.halftone",
    "aurea.distort.wave_warp", "aurea.light.rays", "aurea.color.colorama", "aurea.blur.radial",
    "aurea.stylize.pixel_sort", "aurea.generate.fractal_noise",
]

func fxFeaturedEntries(_ sorted: [EffectCatalogItem]) -> [EffectCatalogItem] {
    let byId = Dictionary(sorted.map { ($0.typeId, $0) }, uniquingKeysWith: { first, _ in first })
    return fxFeaturedKeys.compactMap { byId[fxEffectTypeId($0)] }
}

/// O catálogo oferecido para a camada escolhida: os efeitos que fazem sentido
/// (som só com som, letras só em texto) e as ferramentas, na ordem do navegador.
@MainActor
func fxPickerEntries(_ model: AureaModel, layerHasAudio: Bool) -> [EffectCatalogItem] {
    let text3D = !(model.engine.text3D(forLayer: model.primarySelection ?? 0) ?? [:]).isEmpty
    let textLayer = model.selectedLayer?.kind == 4
    let effects = fxPickableEffects(model.effectCatalog, layerHasAudio: layerHasAudio).filter {
        (textLayer || text3D || $0.typeId != fxEffectTypeId("aurea.text.transform")) &&
        (textLayer || text3D || $0.typeId != fxEffectTypeId("aurea.text.animator"))
    }
    let all = effects + fxPickableTools(layerKind: model.selectedLayer?.kind ?? 0).map(fxToolCatalogItem)
    return fxArrangeCatalog(all, fxEffectCategories(all))
}

// =============================================================================
//  A TELA "ADICIONAR EFEITO" (espelho de EffectPicker.kt `EffectAddSheet`).
//  Folha de tela cheia: ✕ · título · 🔍; DESTAQUES (faixa de cartões grandes);
//  RECENTES e FAVORITOS (miniaturas, 4 por linha); CATEGORIAS (ladrilhos 2 por
//  linha com a prévia escurecida). Tocar num ladrilho abre o grupo (‹ volta).
//  UM toque adiciona; SEGURAR favorita; ferramenta abre a ferramenta.
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
}

private enum EffectAddLayout {
    static let featuredCard: CGFloat = 148
    static let smallMax = 8
    static let tileAspect: CGFloat = 2.2
    static let allGroup = fxAllCategoriesId
}

struct EffectAddSheet: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject var prefs: FxEffectPrefs
    let request: EffectSearchRequest
    let close: () -> Void
    @State private var groupId: String?
    @State private var searching: Bool

    init(request: EffectSearchRequest, close: @escaping () -> Void) {
        _prefs = ObservedObject(wrappedValue: request.prefs)
        self.request = request
        self.close = close
        _searching = State(initialValue: !request.browse)
    }

    var body: some View {
        if searching {
            EffectPickerSearch(prefs: prefs, sorted: request.sorted, onPick: pick, onFavorite: toggleFavorite,
                               onDismiss: { if request.browse { searching = false } else { close() } })
        } else {
            browser
        }
    }

    /// UM toque: grava nos recentes, fecha e adiciona (ou abre a ferramenta).
    private func pick(_ entry: EffectCatalogItem) {
        prefs.addRecent(entry.typeId)
        close()
        if let tool = fxEffectToolOf(entry.typeId) { request.onTool?(tool) } else { request.onPick(entry) }
    }

    private func toggleFavorite(_ entry: EffectCatalogItem) {
        let on = prefs.toggleFavorite(entry.typeId)
        model.toast = AureaText.t(on ? "effects_favorite_added" : "effects_favorite_removed", fxPickerEntryName(entry))
    }

    private var browser: some View {
        let sorted = request.sorted
        let groups = fxGroupEntries(sorted)
        let group = groups.first { $0.group.id == groupId }
        let showingAll = groupId == EffectAddLayout.allGroup
        let featured = fxFeaturedEntries(sorted)
        let recents = fxRecentEffects(prefs.recents, sorted)
        let favorites = fxFavoriteEffects(prefs.favorites, sorted)
        let title = showingAll ? AureaText.t("effects_all_effects") : group?.group.label ?? AureaText.t("panel_adicionar_efeito")
        return VStack(spacing: 0) {
            topBar(title: title, inGroup: showingAll || group != nil)
            ScrollViewReader { proxy in
                ScrollView {
                    Color.clear.frame(height: .zero).id("topo")
                    if showingAll || group != nil {
                        let shown = showingAll ? sorted : group?.entries ?? []
                        LazyVGrid(columns: [GridItem(.adaptive(minimum: EffectPickerLayout.cardMin), spacing: AureaDims.s2, alignment: .top)],
                                  alignment: .leading, spacing: AureaDims.s3) {
                            ForEach(shown) { entry in card(entry, prefix: "effects.card.") }
                        }.padding(.horizontal, AureaDims.s3).padding(.top, AureaDims.s2).padding(.bottom, AureaDims.s5)
                    } else {
                        home(featured: featured, recents: recents, favorites: favorites, groups: groups)
                    }
                }
                .accessibilityIdentifier(showingAll || group != nil ? "effects.grid" : "effects.home")
                .onChange(of: groupId) { _ in proxy.scrollTo("topo", anchor: .top) }
            }.frame(maxHeight: .infinity)
        }
        .foregroundStyle(AureaColors.text).background(AureaColors.editorPanel.ignoresSafeArea())
    }

    private func home(featured: [EffectCatalogItem], recents: [EffectCatalogItem], favorites: [EffectCatalogItem],
                      groups: [(group: FxEffectGroup, entries: [EffectCatalogItem])]) -> some View {
        let four = Array(repeating: GridItem(.flexible(), spacing: AureaDims.s2, alignment: .top), count: 4)
        let two = Array(repeating: GridItem(.flexible(), spacing: AureaDims.s2, alignment: .top), count: 2)
        let entriesByGroup = Dictionary(uniqueKeysWithValues: groups.map { ($0.group, $0.entries) })
        return VStack(alignment: .leading, spacing: AureaDims.s2) {
            if !featured.isEmpty {
                sectionTitle(AureaText.t("fxui_featured"))
                ScrollView(.horizontal, showsIndicators: false) {
                    LazyHStack(alignment: .top, spacing: AureaDims.s3) {
                        ForEach(featured) { entry in
                            card(entry, prefix: "effects.featured.", aspect: 1, nameSize: 13).frame(width: EffectAddLayout.featuredCard)
                        }
                    }
                }
            }
            if !recents.isEmpty {
                sectionTitle(AureaText.t("effect_recentes"))
                LazyVGrid(columns: four, alignment: .leading, spacing: AureaDims.s2) {
                    ForEach(Array(recents.prefix(EffectAddLayout.smallMax))) { entry in card(entry, prefix: "effects.recent.", aspect: 1, nameSize: 11) }
                }
            }
            if !favorites.isEmpty {
                sectionTitle(AureaText.t("effect_favoritos"))
                LazyVGrid(columns: four, alignment: .leading, spacing: AureaDims.s2) {
                    ForEach(Array(favorites.prefix(EffectAddLayout.smallMax))) { entry in card(entry, prefix: "effects.favorite.", aspect: 1, nameSize: 11) }
                }
            } else {
                Text(AureaText.t("effects_favorites_hint")).font(HomeType.cardSpec).foregroundStyle(AureaColors.muted)
                    .padding(.top, AureaDims.s1)
            }
            sectionTitle(AureaText.t("fxui_categories"))
            LazyVGrid(columns: two, alignment: .leading, spacing: AureaDims.s2) {
                EffectGroupTile(label: AureaText.t("effects_all_effects"), id: EffectAddLayout.allGroup,
                                banner: featured.first { fxEffectToolOf($0.typeId) == nil }, glyph: CupertinoGlyph.SquareGrid2x2,
                                store: model.effectPreviews) { groupId = EffectAddLayout.allGroup }
                ForEach(groups.map { $0.group }, id: \.self) { group in
                    EffectGroupTile(label: group.label, id: group.id, banner: fxGroupBannerEntry(group, entriesByGroup[group] ?? []),
                                    glyph: group.glyph, store: model.effectPreviews) { groupId = group.id }
                }
            }
        }.padding(.horizontal, AureaDims.s3).padding(.top, AureaDims.s1).padding(.bottom, AureaDims.s5)
    }

    /// ✕ (ou ‹ dentro de um grupo) · título · 🔍 — `effects.close`, `effects.back`, `effects.search`.
    private func topBar(title: String, inGroup: Bool) -> some View {
        HStack(spacing: 0) {
            Button { if inGroup { groupId = nil } else { close() } } label: {
                CupertinoGlyph.text(inGroup ? CupertinoGlyph.ChevronBack : CupertinoGlyph.Xmark, size: AureaDims.iconLg)
                    .frame(width: AureaDims.minTap + AureaDims.s1, height: AureaDims.minTap).contentShape(Rectangle())
            }.buttonStyle(AureaPressStyle(shrink: 1))
                .accessibilityLabel(AureaText.t(inGroup ? "fxui_back_categories" : "common_close"))
                .accessibilityIdentifier(inGroup ? "effects.back" : "effects.close")
            Text(title).font(.aurea(size: 18, weight: .bold)).lineLimit(1).truncationMode(.tail)
                .padding(.horizontal, AureaDims.s1).frame(maxWidth: .infinity, alignment: .leading)
                .accessibilityAddTraits(.isHeader)
            Button { searching = true } label: {
                CupertinoGlyph.text(CupertinoGlyph.Search, size: AureaDims.iconLg)
                    .frame(width: AureaDims.minTap + AureaDims.s1, height: AureaDims.minTap).contentShape(Rectangle())
            }.buttonStyle(AureaPressStyle(shrink: 1))
                .accessibilityLabel(AureaText.t("fxui_search_effects"))
                .accessibilityIdentifier("effects.search")
        }.padding(.horizontal, AureaDims.s1).frame(height: AureaDims.topBar + AureaDims.s2)
    }

    private func sectionTitle(_ text: String) -> some View {
        Text(text).font(AureaTextSpec.section.font).tracking(0.3).foregroundStyle(AureaColors.muted)
            .padding(.top, AureaDims.s3).padding(.bottom, AureaDims.s1)
            .accessibilityAddTraits(.isHeader)
    }

    private func card(_ entry: EffectCatalogItem, prefix: String, aspect: CGFloat = EffectPickerLayout.previewAspect, nameSize: CGFloat = EffectPickerLayout.nameSize) -> some View {
        EffectPickerCard(entry: entry, store: model.effectPreviews, favorite: prefs.isFavorite(entry.typeId),
                         identifier: prefix + fxEffectCardId(entry.typeId), aspect: aspect, nameSize: nameSize,
                         onPick: { pick(entry) }, onFavorite: { toggleFavorite(entry) })
    }
}

/// O LADRILHO de um grupo: a prévia real de um efeito dele, escurecida, e o nome
/// em branco, em negrito, no meio (`effects.category.<id>`).
private struct EffectGroupTile: View {
    let label: String
    let id: String
    let banner: EffectCatalogItem?
    let glyph: Character
    let store: EffectPreviewStore
    let action: () -> Void
    @State private var image: UIImage?

    var body: some View {
        Button(action: action) {
            Color.clear.aspectRatio(EffectAddLayout.tileAspect, contentMode: .fit)
                .overlay {
                    if let image {
                        Image(uiImage: image).resizable().interpolation(.low).scaledToFill()
                    } else {
                        FxToolPlate(glyph: glyph, glyphSize: 30, opacity: 0.35)
                    }
                }
                .overlay { Color.black.opacity(0.52) }
                .overlay {
                    Text(label).font(.aurea(size: 15, weight: .heavy)).foregroundStyle(.white)
                        .multilineTextAlignment(.center).lineLimit(2).padding(.horizontal, AureaDims.s2)
                }
                .clipShape(RoundedRectangle(cornerRadius: AureaDims.radiusCard))
                .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 0.97))
        .accessibilityLabel(AureaText.t("fxui_a11y_category", label))
        .accessibilityIdentifier("effects.category." + id)
        .task(id: banner?.typeId) {
            guard let typeId = banner?.typeId else { image = nil; return }
            let loaded = await store.image(for: typeId)
            if !Task.isCancelled { image = loaded }
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

/// A BUSCA em tela cheia: o campo já focado e a grade filtrando a cada letra.
/// Um toque no resultado (`effects.result.<id>`) adiciona e fecha.
struct EffectPickerSearch: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject var prefs: FxEffectPrefs
    let sorted: [EffectCatalogItem]
    let onPick: (EffectCatalogItem) -> Void
    let onFavorite: (EffectCatalogItem) -> Void
    let onDismiss: () -> Void

    init(prefs: FxEffectPrefs, sorted: [EffectCatalogItem], onPick: @escaping (EffectCatalogItem) -> Void,
         onFavorite: @escaping (EffectCatalogItem) -> Void, onDismiss: @escaping () -> Void) {
        _prefs = ObservedObject(wrappedValue: prefs)
        self.sorted = sorted
        self.onPick = onPick
        self.onFavorite = onFavorite
        self.onDismiss = onDismiss
    }
    @State private var query = ""
    @State private var docs: [UInt32: FxSearchDoc] = [:]
    @FocusState private var focused: Bool

    var body: some View {
        let blank = query.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
        let results = blank ? sorted : fxSearchEffects(sorted, docs, query)
        VStack(spacing: 0) {
            HStack(spacing: 0) {
                Button(action: onDismiss) {
                    CupertinoGlyph.text(CupertinoGlyph.ChevronBack, size: AureaDims.iconLg)
                        .frame(width: EffectPickerLayout.backTarget, height: AureaDims.minTap).contentShape(Rectangle())
                }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityLabel(AureaText.t("common_close"))
                HStack(spacing: 0) {
                    CupertinoGlyph.text(CupertinoGlyph.Search, size: AureaDims.iconSm, color: AureaColors.muted)
                        .padding(.trailing, EffectPickerLayout.fieldGap)
                    TextField("", text: $query, prompt: Text(AureaText.t("effect_buscar_glitch_vhs_desfoque_cor")).foregroundColor(AureaColors.muted))
                        .font(EffectPickerLayout.description).textInputAutocapitalization(.never).autocorrectionDisabled()
                        .textFieldStyle(.plain).tint(AureaColors.accent).focused($focused).submitLabel(.search)
                        .accessibilityIdentifier("effects.search.field")
                    if !query.isEmpty {
                        Button { query = "" } label: {
                            CupertinoGlyph.text(CupertinoGlyph.XmarkCircleFill, size: AureaDims.iconSm, color: AureaColors.muted)
                                .frame(width: AureaDims.minTap, height: AureaDims.minTap).contentShape(Rectangle())
                        }.accessibilityLabel(AureaText.t("home_clear_search"))
                    }
                }.padding(.leading, AureaDims.s2).frame(height: AureaDims.minTap)
                    .background(AureaColors.fieldFilled, in: RoundedRectangle(cornerRadius: AureaDims.radiusChip))
            }.padding(.trailing, AureaDims.s3).frame(height: AureaDims.topBar + AureaDims.s2)
            ScrollView {
                if results.isEmpty {
                    Text(AureaText.t("effect_empty_search")).font(HomeType.bodySmall).foregroundStyle(AureaColors.muted)
                        .multilineTextAlignment(.center).frame(maxWidth: .infinity).padding(AureaDims.s4)
                }
                LazyVGrid(columns: [GridItem(.adaptive(minimum: EffectPickerLayout.cardMin + AureaDims.s2), spacing: AureaDims.s3, alignment: .top)],
                          alignment: .leading, spacing: AureaDims.s3) {
                    ForEach(results) { entry in
                        EffectPickerCard(entry: entry, store: model.effectPreviews, favorite: prefs.isFavorite(entry.typeId),
                                         identifier: "effects.result." + fxEffectCardId(entry.typeId),
                                         onPick: { onPick(entry) }, onFavorite: { onFavorite(entry) })
                    }
                }.padding(.horizontal, AureaDims.s3).padding(.top, AureaDims.s2).padding(.bottom, AureaDims.s5)
            }.scrollDismissesKeyboard(.interactively).accessibilityIdentifier("effects.search.results")
        }
        .foregroundStyle(AureaColors.text).background(AureaColors.editorPanel.ignoresSafeArea())
        .onAppear {
            docs = fxEffectSearchDocs(sorted)
            DispatchQueue.main.async { focused = true }
        }
    }
}

/// O CARTÃO: a prévia real (ou a cartela da categoria; a ferramenta tem a
/// dela), a estrela quando é favorito e o nome em até duas linhas. Um toque
/// adiciona; segurar favorita.
private struct EffectPickerCard: View {
    let entry: EffectCatalogItem
    let store: EffectPreviewStore
    let favorite: Bool
    let identifier: String
    var aspect: CGFloat = EffectPickerLayout.previewAspect
    var nameSize: CGFloat = EffectPickerLayout.nameSize
    let onPick: () -> Void
    let onFavorite: () -> Void

    var body: some View {
        let name = fxPickerEntryName(entry)
        VStack(alignment: .leading, spacing: AureaDims.s1) {
            Group {
                if let tool = fxEffectToolOf(entry.typeId) {
                    Color.clear.aspectRatio(aspect, contentMode: .fit)
                        .overlay { FxToolPlate(glyph: tool.glyph) }
                        .clipShape(RoundedRectangle(cornerRadius: AureaDims.radiusCard))
                } else {
                    EffectBrowserPreview(entry: entry, store: store, aspect: aspect)
                }
            }
                .overlay(alignment: .topTrailing) {
                    if favorite {
                        CupertinoGlyph.text(CupertinoGlyph.StarFill, size: AureaDims.iconXs, color: AureaColors.accent)
                            .frame(width: EffectPickerLayout.favoriteBadge, height: EffectPickerLayout.favoriteBadge)
                            .background(.black.opacity(0.45), in: Circle()).padding(AureaDims.s1)
                    }
                }
            Text(name).font(.aurea(size: nameSize, weight: .semibold)).tracking(-0.1).lineLimit(2)
                .frame(height: nameSize * 1.2 * 2, alignment: .topLeading)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .contentShape(Rectangle())
        // O toque vem antes do toque longo: assim a rolagem da grade continua livre.
        .onTapGesture {
            UISelectionFeedbackGenerator().selectionChanged()
            onPick()
        }
        .onLongPressGesture(minimumDuration: EffectPickerLayout.longPress) {
            UIImpactFeedbackGenerator(style: .medium).impactOccurred()
            onFavorite()
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(AureaText.t("effects_card_add", name))
        .accessibilityAddTraits(.isButton)
        .accessibilityIdentifier(identifier)
        .accessibilityAction { onPick() }
        .accessibilityAction(named: Text(AureaText.t(favorite ? "effect_tirar_favoritos" : "effect_nos_favoritos"))) { onFavorite() }
    }
}

private struct EffectBrowserPreview: View {
    let entry: EffectCatalogItem
    let store: EffectPreviewStore
    var aspect: CGFloat = EffectPickerLayout.previewAspect
    @State private var image: UIImage?

    var body: some View {
        GeometryReader { geometry in
            if let image {
                Image(uiImage: image).resizable().interpolation(.low).scaledToFill()
                    .frame(width: geometry.size.width, height: geometry.size.height).clipped()
            } else {
                // Same labelled loading/unavailable plate as Android, never a fake effect.
                Canvas { context, size in
                    context.fill(Path(CGRect(origin: .zero, size: size)), with: .linearGradient(
                        Gradient(colors: [AureaColors.effectPreviewTop, AureaColors.effectPreviewBottom]),
                        startPoint: .zero, endPoint: CGPoint(x: 0, y: size.height)))
                    let radius = size.width * 0.19
                    context.fill(Path(ellipseIn: CGRect(x: size.width * 0.34 - radius, y: size.height * 0.60 - radius,
                                                       width: radius * 2, height: radius * 2)), with: .color(AureaColors.effectPreviewDisc.opacity(0.85)))
                    context.fill(Path(CGRect(x: size.width * 0.20, y: size.height * 0.18, width: size.width * 0.60, height: size.height * 0.012)), with: .color(.white.opacity(0.6)))
                    context.fill(Path(CGRect(x: size.width * 0.20, y: size.height * 0.26, width: size.width * 0.40, height: size.height * 0.012)), with: .color(.white.opacity(0.25)))
                }.overlay(alignment: .bottomTrailing) {
                    CupertinoGlyph.text(fxCategoryGlyph(entry.category), size: AureaDims.iconSm, color: .white.opacity(0.85)).padding(AureaDims.s2)
                }
            }
        }.aspectRatio(aspect, contentMode: .fit)
            .clipShape(RoundedRectangle(cornerRadius: AureaDims.radiusCard))
            .task(id: entry.typeId) {
                let loaded = await store.image(for: entry.typeId)
                if !Task.isCancelled { image = loaded }
            }
    }
}

// =============================================================================
//  "SOBRE O EFEITO" (⋯ de um efeito na pilha): a ficha — prévia, o que ele faz,
//  onde funciona, custo e parâmetros. Só leitura; a estrela continua valendo.
// =============================================================================

struct EffectAboutSheet: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject var prefs: FxEffectPrefs
    let entry: EffectCatalogItem

    private var favorite: Bool { prefs.isFavorite(entry.typeId) }

    var body: some View {
        let specs = model.engine.effectSpecs(entry.typeId).map { fxLocalizedSpec(entry.typeId, $0) }
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                EffectBrowserPreview(entry: entry, store: model.effectPreviews)
                gap(AureaDims.s4)
                HStack(spacing: 0) {
                    VStack(alignment: .leading, spacing: 2) {
                        Text(fxEffectDisplayName(entry.typeId, entry.name)).font(.aurea(size: 22, weight: .bold))
                            .tracking(-0.4).frame(minHeight: 22 * 1.35, alignment: .leading)
                        cardSpec(fxEffectCostLine(entry.category, fxEffectCost(entry.effectClass)))
                    }.frame(maxWidth: .infinity, alignment: .leading)
                    Button { prefs.toggleFavorite(entry.typeId) } label: {
                        CupertinoGlyph.text(favorite ? CupertinoGlyph.StarFill : CupertinoGlyph.Star,
                                            size: AureaDims.iconLg, color: favorite ? AureaColors.accent : AureaColors.muted)
                            .frame(width: AureaDims.minTap, height: AureaDims.minTap)
                    }.buttonStyle(AureaPressStyle())
                        .accessibilityLabel(AureaText.t(favorite ? "effect_tirar_favoritos" : "effect_nos_favoritos"))
                }
                gap(AureaDims.s3)
                Text(fxEffectDescription(entry.typeId, entry.category)).font(EffectPickerLayout.description).tracking(-0.1)
                    .lineSpacing(max(0, 14 * 1.45 - UIFont.systemFont(ofSize: 14).lineHeight))
                    .fixedSize(horizontal: false, vertical: true)
                gap(AureaDims.s3)
                cardSpec(fxEffectCompatibilityLine(entry.typeId))
                gap(AureaDims.s5)
                if !specs.isEmpty {
                    Text(AureaText.t("effect_parameters")).font(.aurea(size: 11, weight: .semibold))
                        .tracking(0.3).foregroundStyle(AureaColors.muted).frame(minHeight: 11 * 1.35)
                    gap(AureaDims.s2)
                    ForEach(specs.indices, id: \.self) { index in
                        HStack(spacing: 0) {
                            Text(specs[index]["label"] as? String ?? "").font(EffectPickerLayout.description).tracking(-0.1)
                                .frame(maxWidth: .infinity, minHeight: 14 * 1.35, alignment: .leading)
                            cardSpec(summary(specs[index])).multilineTextAlignment(.trailing)
                        }.padding(.vertical, EffectPickerLayout.parameterPadding)
                    }
                }
            }.frame(maxWidth: .infinity, alignment: .leading)
                .padding(.horizontal, AureaDims.s5).padding(.top, AureaDims.s5).padding(.bottom, AureaDims.s6)
        }
        .foregroundStyle(AureaColors.text).background(AureaColors.editorPanel.ignoresSafeArea())
    }

    private func gap(_ height: CGFloat) -> some View { Color.clear.frame(height: height) }
    private func cardSpec(_ text: String) -> some View {
        Text(text).font(.aurea(size: 11)).tracking(-0.1).foregroundStyle(AureaColors.muted)
            .frame(minHeight: 11 * 1.35).fixedSize(horizontal: false, vertical: true)
    }
    private func summary(_ spec: [String: Any]) -> String {
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
                ? AureaText.t("fx_param_range", trimNumber(low), trimNumber(high)) + (unit.isEmpty ? "" : " " + unit) : unit
        }
    }
    private func trimNumber(_ value: Float) -> String {
        if value.rounded() == value { return String(format: "%.0f", locale: Locale(identifier: "en_US_POSIX"), Double(value)) }
        return String(format: "%.2f", locale: Locale(identifier: "en_US_POSIX"), Double(value))
            .replacingOccurrences(of: "0+$", with: "", options: .regularExpression)
            .replacingOccurrences(of: "\\.$", with: "", options: .regularExpression)
    }
}
