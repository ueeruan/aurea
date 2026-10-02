// Android spec: effects/EffectPicker.kt, effects/EffectPickerLogic.kt and
// effects/EffectsCatalogView.kt (a ficha). A aba "Adicionar" do painel Efeitos.
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
        }
    }
    AureaText.language = saved
    var docs: [UInt32: FxSearchDoc] = [:]
    for entry in entries {
        docs[entry.typeId] = fxSearchDoc(
            names: [fxEffectDisplayName(entry.typeId, entry.name), entry.name],
            extra: [current[entry.typeId] ?? "", entry.category, fxCategorySynonyms(entry.category)] + (perLanguage[entry.typeId] ?? []))
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
//  A ABA "ADICIONAR": busca no topo, fichas de categoria, Recentes/Favoritos e
//  a grade. UM toque adiciona; SEGURAR favorita. Nenhum nível para descer.
// =============================================================================

struct EffectPickerView: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject var prefs: FxEffectPrefs
    let layerHasAudio: Bool
    let onPick: (EffectCatalogItem) -> Void
    @State private var chosen: String?

    private var pickable: [EffectCatalogItem] {
        fxPickableEffects(model.effectCatalog, layerHasAudio: layerHasAudio).filter {
            (model.selectedLayer?.kind == 4 || $0.typeId != fxEffectTypeId("aurea.text.transform") ||
                !(model.engine.text3D(forLayer: model.primarySelection ?? 0) ?? [:]).isEmpty) &&
            (model.selectedLayer?.kind == 4 || $0.typeId != fxEffectTypeId("aurea.text.animator") ||
                !(model.engine.text3D(forLayer: model.primarySelection ?? 0) ?? [:]).isEmpty)
        }
    }

    var body: some View {
        let pickable = self.pickable
        let categories = fxEffectCategories(pickable)
        let sorted = fxArrangeCatalog(pickable, categories)
        // Categoria que sumiu (camada sem som e a ficha era "Áudio") volta a "Todos".
        let category = chosen.flatMap { categories.contains($0) ? $0 : nil }
        let recents = fxRecentEffects(prefs.recents, pickable)
        let favorites = fxFavoriteEffects(prefs.favorites, sorted)
        let shown = fxBrowseEffects(sorted, category: category)
        VStack(spacing: 0) {
            searchLauncher
            categoryStrip(categories, selected: category)
            ScrollViewReader { proxy in
                ScrollView {
                    VStack(alignment: .leading, spacing: AureaDims.s3) {
                        Color.clear.frame(height: .zero).id("topo")
                        if category == nil {
                            if !recents.isEmpty { row(AureaText.t("effect_recentes"), recents, prefix: "effects.recent.") }
                            if !favorites.isEmpty { row(AureaText.t("effect_favoritos"), favorites, prefix: "effects.favorite.") }
                            VStack(alignment: .leading, spacing: 0) {
                                if !recents.isEmpty || !favorites.isEmpty { sectionTitle(AureaText.t("effects_all_effects")) }
                                if favorites.isEmpty {
                                    Text(AureaText.t("effects_favorites_hint")).font(HomeType.cardSpec).foregroundStyle(AureaColors.muted)
                                        .padding(.vertical, AureaDims.s1)
                                }
                            }
                        }
                        if shown.isEmpty {
                            emptyLine(AureaText.t("effect_empty_category"))
                        }
                        LazyVGrid(columns: [GridItem(.adaptive(minimum: EffectPickerLayout.cardMin), spacing: AureaDims.s2, alignment: .top)],
                                  alignment: .leading, spacing: AureaDims.s3) {
                            ForEach(shown) { entry in card(entry, prefix: "effects.card.") }
                        }
                    }.padding(.horizontal, AureaDims.s3).padding(.top, AureaDims.s1).padding(.bottom, AureaDims.s4)
                }
                .accessibilityIdentifier("effects.grid")
                .onChange(of: category) { _ in proxy.scrollTo("topo", anchor: .top) }
            }.frame(maxHeight: .infinity)
        }
    }

    /// Parece o campo; abre a busca em tela cheia, acima do teclado.
    private var searchLauncher: some View {
        Button {
            let pick = onPick
            let pickable = self.pickable
            let sorted = fxArrangeCatalog(pickable, fxEffectCategories(pickable))
            model.effectSearch = EffectSearchRequest(prefs: prefs, sorted: sorted, onPick: pick, onFavorite: toggleFavorite)
        } label: {
            HStack(spacing: EffectPickerLayout.fieldGap) {
                CupertinoGlyph.text(CupertinoGlyph.Search, size: AureaDims.iconSm, color: AureaColors.muted)
                Text(AureaText.t("effect_buscar_glitch_vhs_desfoque_cor")).font(EffectPickerLayout.description)
                    .foregroundStyle(AureaColors.muted).lineLimit(1)
                Spacer(minLength: 0)
            }.padding(.horizontal, AureaDims.s2)
                .frame(maxWidth: .infinity).frame(height: AureaDims.searchField - AureaDims.s1)
                .background(AureaColors.fieldFilled, in: RoundedRectangle(cornerRadius: AureaDims.radiusChip))
                .padding(.horizontal, AureaDims.s3)
                .frame(height: AureaDims.minTap).contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle(shrink: 1))
            .accessibilityLabel(AureaText.t("effect_buscar_glitch_vhs_desfoque_cor"))
            .accessibilityIdentifier("effects.search")
    }

    private func categoryStrip(_ categories: [String], selected: String?) -> some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: AureaDims.s2) {
                chip(AureaText.t("effect_todos"), on: selected == nil, id: fxAllCategoriesId) { chosen = nil }
                ForEach(categories, id: \.self) { category in
                    chip(fxEffectCategoryLabel(category), on: selected == category, id: fxEffectCategoryId(category)) { chosen = category }
                }
            }.padding(.horizontal, AureaDims.s3)
        }.frame(height: AureaDims.minTap)
    }

    private func chip(_ label: String, on: Bool, id: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(label).font(AureaTextSpec.chipLabel.font).lineLimit(1)
                .foregroundStyle(on ? AureaColors.action : AureaColors.text)
                .padding(.horizontal, AureaDims.s3).frame(height: AureaDims.chipHeight)
                .background(on ? AureaColors.actionDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: AureaDims.radiusChip))
                .overlay(RoundedRectangle(cornerRadius: AureaDims.radiusChip).stroke(on ? AureaColors.action : .clear, lineWidth: AureaDims.hairline))
                // O alvo é a faixa inteira (44); a ficha desenhada tem 34.
                .frame(height: AureaDims.minTap).contentShape(Rectangle())
        }.buttonStyle(AureaPressStyle(shrink: 1))
            .accessibilityAddTraits(on ? .isSelected : [])
            .accessibilityIdentifier("effects.category." + id)
    }

    private func row(_ title: String, _ entries: [EffectCatalogItem], prefix: String) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            sectionTitle(title)
            ScrollView(.horizontal, showsIndicators: false) {
                LazyHStack(alignment: .top, spacing: AureaDims.s2) {
                    ForEach(entries) { entry in card(entry, prefix: prefix).frame(width: EffectPickerLayout.rowCard) }
                }
            }
        }
    }

    private func card(_ entry: EffectCatalogItem, prefix: String) -> some View {
        EffectPickerCard(entry: entry, store: model.effectPreviews, favorite: prefs.isFavorite(entry.typeId),
                         identifier: prefix + fxEffectCardId(entry.typeId),
                         onPick: { onPick(entry) }, onFavorite: { toggleFavorite(entry) })
    }

    /// Segurar o cartão: favorita ou desfavorita, e diz o que fez (o gesto não se vê).
    private func toggleFavorite(_ entry: EffectCatalogItem) {
        let on = prefs.toggleFavorite(entry.typeId)
        let name = fxEffectDisplayName(entry.typeId, entry.name)
        model.toast = AureaText.t(on ? "effects_favorite_added" : "effects_favorite_removed", name)
    }

    private func sectionTitle(_ text: String) -> some View {
        Text(text).font(AureaTextSpec.section.font).tracking(0.3).foregroundStyle(AureaColors.muted)
            .padding(.top, AureaDims.s1).padding(.bottom, AureaDims.s2)
    }

    private func emptyLine(_ text: String) -> some View {
        Text(text).font(HomeType.bodySmall).foregroundStyle(AureaColors.muted)
            .multilineTextAlignment(.center).frame(maxWidth: .infinity).padding(AureaDims.s4)
    }
}

/// A BUSCA em tela cheia: o campo já focado e a grade filtrando a cada letra.
/// Um toque no resultado (`effects.result.<id>`) adiciona e fecha.
/// Pedido de busca: o que o painel sabe (catálogo filtrado, favoritos e o
/// que fazer ao escolher) levado até a raiz do editor, que apresenta.
struct EffectSearchRequest: Identifiable {
    let id = UUID()
    let prefs: FxEffectPrefs
    let sorted: [EffectCatalogItem]
    let onPick: (EffectCatalogItem) -> Void
    let onFavorite: (EffectCatalogItem) -> Void
}

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

/// O CARTÃO: a prévia real (ou a cartela da categoria), a estrela quando é
/// favorito e o nome em até duas linhas. Um toque adiciona; segurar favorita.
private struct EffectPickerCard: View {
    let entry: EffectCatalogItem
    let store: EffectPreviewStore
    let favorite: Bool
    let identifier: String
    let onPick: () -> Void
    let onFavorite: () -> Void

    var body: some View {
        let name = fxEffectDisplayName(entry.typeId, entry.name)
        VStack(alignment: .leading, spacing: AureaDims.s1) {
            EffectBrowserPreview(entry: entry, store: store)
                .overlay(alignment: .topTrailing) {
                    if favorite {
                        CupertinoGlyph.text(CupertinoGlyph.StarFill, size: AureaDims.iconXs, color: AureaColors.accent)
                            .frame(width: EffectPickerLayout.favoriteBadge, height: EffectPickerLayout.favoriteBadge)
                            .background(.black.opacity(0.45), in: Circle()).padding(AureaDims.s1)
                    }
                }
            Text(name).font(.aurea(size: EffectPickerLayout.nameSize, weight: .semibold)).tracking(-0.1).lineLimit(2)
                .frame(height: EffectPickerLayout.nameLineHeight * 2, alignment: .topLeading)
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
        }.aspectRatio(EffectPickerLayout.previewAspect, contentMode: .fit)
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
