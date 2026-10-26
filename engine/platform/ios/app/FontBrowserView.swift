// =============================================================================
//  Aurea / platform / ios / app / FontBrowserView.swift
//
//  Omega conteúdo (área 06, P0-2 / P0-3 / P1-12): o NAVEGADOR DE FONTES em tela
//  cheia e as preferências de fonte do aparelho (favoritas, recentes, uso,
//  filtros). O mini navegador do modo de texto (TextEditModeView.swift) usa as
//  mesmas regras daqui. Par do FontBrowser.kt (mesmos ids e chaves).
// =============================================================================
import SwiftUI
import UIKit
import CoreText

/// Uma face da lista do motor (`availableFonts`, FontEntry).
struct ContentFontItem: Hashable {
    var family: String
    var style: String
    var weight: UInt32
    var italic: Bool
    var path: String
    var imported: Bool
    /// FontCategory do motor: 0 sem serifa, 1 serifa, 2 display, 3 manuscrita, 4 mono.
    var category: Int
    /// Bits de escrita (FontScript do motor).
    var scripts: Int

    init(family: String, style: String = "", weight: UInt32 = 400, italic: Bool = false, path: String = "",
         imported: Bool = false, category: Int = 0, scripts: Int = FontScripts.latin) {
        self.family = family; self.style = style; self.weight = weight; self.italic = italic; self.path = path
        self.imported = imported; self.category = category; self.scripts = scripts
    }

    init(_ d: [String: Any]) {
        let path = d["path"] as? String ?? ""
        self.init(family: d["family"] as? String ?? "", style: d["style"] as? String ?? "",
                  weight: (d["weight"] as? NSNumber)?.uint32Value ?? 400, italic: (d["italic"] as? NSNumber)?.boolValue ?? false,
                  path: path,
                  imported: (d["imported"] as? NSNumber)?.boolValue ?? path.hasPrefix(AureaPaths.documents.path),
                  category: (d["category"] as? NSNumber)?.intValue ?? 0,
                  scripts: (d["scripts"] as? NSNumber)?.intValue ?? FontScripts.latin)
    }

    var name: String { "\(family) \(style)".trimmingCharacters(in: .whitespaces) }
    /// Chave da face nas preferências: "família|peso|itálico" (a mesma do Android).
    var key: String { "\(family)|\(weight)|\(italic ? 1 : 0)" }
}

/// As fontes que o motor conhece (relidas ao abrir o modo de texto e o navegador).
@MainActor final class FontLibrary: ObservableObject {
    static let shared = FontLibrary()
    @Published private(set) var items: [ContentFontItem] = []

    func reload(_ engine: AureaEngine) {
        items = engine.availableFonts().map { ContentFontItem($0) }.filter { !$0.family.isEmpty }
    }

    /// TTF/OTF/coleção/WOFF copiado para Documents/Media (pasta que o motor varre
    /// ao abrir), registrado no CoreText e no motor. Devolve a face importada.
    @discardableResult
    func importFont(_ url: URL, model: AureaModel) -> ContentFontItem? {
        guard ["ttf", "otf", "ttc", "otc", "woff"].contains(url.pathExtension.lowercased()) else {
            model.toast = AureaText.t("msg_use_uma_fonte_ttf_ou_otf"); return nil
        }
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        AureaPaths.ensureDirectories()
        let destination = AureaPaths.mediaDestination(for: url.lastPathComponent)
        do { try AureaPaths.copyImport(url, to: destination) }
        catch { model.toast = AureaText.t("msg_nao_deu_para_ler_essa_fonte"); return nil }
        guard let font = model.engine.importFont(atPath: destination.path) else {
            try? FileManager.default.removeItem(at: destination)
            model.toast = AureaText.t("msg_nao_deu_para_ler_essa_fonte"); return nil
        }
        FontImportPicker.registerWithCoreText((font["path"] as? String).map { URL(fileURLWithPath: $0) } ?? destination)
        reload(model.engine)
        model.toast = AureaText.t("msg_fonte_importada", font["family"] as? String ?? "")
        return ContentFontItem(font)
    }
}

/// Bits de escrita do motor (FontEntry::scripts) e categorias (FontEntry::category).
enum FontScripts {
    static let latin = 1 << 0
    static let greek = 1 << 1
    static let cyrillic = 1 << 2
    static let hebrew = 1 << 3
    static let arabic = 1 << 4
    static let thai = 1 << 5
    static let korean = 1 << 6
    static let japanese = 1 << 7
    static let chinese = 1 << 8
    static let devanagari = 1 << 9
    /// A ordem do menu "Escrita" (bit, rótulo).
    static let menu: [(Int, String)] = [
        (latin, "om_fb_script_latin"), (greek, "om_fb_script_greek"), (cyrillic, "om_fb_script_cyrillic"),
        (hebrew, "om_fb_script_hebrew"), (arabic, "om_fb_script_arabic"), (thai, "om_fb_script_thai"),
        (korean, "om_fb_script_korean"), (japanese, "om_fb_script_japanese"), (chinese, "om_fb_script_chinese"),
        (devanagari, "om_fb_script_devanagari"),
    ]
    /// Categorias (FontCategory do motor).
    static let categories: [(Int, String)] = [
        (0, "om_fb_cat_sans"), (1, "om_fb_cat_serif"), (2, "om_fb_cat_display"), (3, "om_fb_cat_handwriting"), (4, "om_fb_cat_mono"),
    ]
    static let categoryTags = ["sans", "serif", "display", "handwriting", "mono"]
    static let filterFavorite = 1 << 5
    static let filterImported = 1 << 6

    /// Amostra própria do AUREA: latim, ou — se a face cobre UMA só escrita não
    /// latina — uma amostra curta dessa escrita (+ "Abcd" no navegador).
    static func sample(_ scripts: Int, long: Bool) -> String {
        var other = scripts & ~latin
        // Grego e cirílico vêm de brinde em fontes de outras escritas: não contam.
        let primary = japanese | korean | chinese | arabic | hebrew | thai | devanagari
        if other & primary != 0 { other &= ~(greek | cyrillic) }
        if other & (japanese | korean) != 0 { other &= ~chinese }
        let own: String?
        switch other {
        case greek: own = "Αβγ"
        case cyrillic: own = "Абв"
        case hebrew: own = "אבג"
        case arabic: own = "أبج"
        case thai: own = "กขค"
        case korean: own = "가나다"
        case japanese: own = "あいう"
        case chinese: own = "中文字"
        case devanagari: own = "कखग"
        default: own = nil
        }
        guard let own else { return long ? "Abcdefg" : "Abc" }
        return long ? "\(own) Abcd" : own
    }
}

/// Busca da referência: o início do nome ou o início de uma palavra dele
/// (posição em caracteres; −1 = não casa; busca vazia = 0).
func fontMatches(_ name: String, _ query: String) -> Int {
    let q = query.trimmingCharacters(in: .whitespaces).lowercased()
    if q.isEmpty { return 0 }
    let n = name.lowercased()
    if n.hasPrefix(q) { return 0 }
    guard let range = n.range(of: " " + q) else { return -1 }
    return n.distance(from: n.startIndex, to: range.lowerBound) + 1
}

/// Preferências de fonte do aparelho: favoritas, recentes (até 12), quantas
/// vezes cada uma foi usada ("Mais usadas"), ordenação, filtros e escrita.
@MainActor final class FontPrefs: ObservableObject {
    static let shared = FontPrefs()
    private let defaults = UserDefaults.standard

    @Published private(set) var favorites: Set<String>
    @Published private(set) var recents: [String]
    @Published private(set) var useCount: [String: Int]
    /// 0 mais usadas, 1 A–Z.
    @Published private(set) var sort: Int
    /// Bits 0..4 categorias, 5 favoritas, 6 importadas.
    @Published private(set) var filters: Int
    /// Bit da escrita escolhida (0 = todas).
    @Published private(set) var script: Int

    private init() {
        favorites = Set(defaults.stringArray(forKey: "aurea.fonts.favorites") ?? [])
        recents = defaults.stringArray(forKey: "aurea.fonts.recents") ?? []
        useCount = (defaults.dictionary(forKey: "aurea.fonts.useCount") as? [String: Int]) ?? [:]
        sort = min(1, max(0, defaults.integer(forKey: "aurea.fonts.sort")))
        filters = defaults.integer(forKey: "aurea.fonts.filters")
        script = defaults.integer(forKey: "aurea.fonts.script")
    }

    func toggleFavorite(_ f: ContentFontItem) {
        if favorites.contains(f.key) { favorites.remove(f.key) } else { favorites.insert(f.key) }
        defaults.set(Array(favorites), forKey: "aurea.fonts.favorites")
    }

    /// A face foi aplicada: vai para o topo das recentes e conta um uso.
    func recordUse(_ f: ContentFontItem) {
        recents = Array(([f.key] + recents.filter { $0 != f.key }).prefix(12))
        useCount[f.key, default: 0] += 1
        defaults.set(recents, forKey: "aurea.fonts.recents")
        defaults.set(useCount, forKey: "aurea.fonts.useCount")
    }

    func setSort(_ v: Int) { sort = v; defaults.set(v, forKey: "aurea.fonts.sort") }
    func toggleFilter(_ bit: Int) { filters ^= bit; defaults.set(filters, forKey: "aurea.fonts.filters") }
    func setScript(_ bit: Int) { script = bit; defaults.set(bit, forKey: "aurea.fonts.script") }

    /// O mini navegador (regra da referência): favoritas em A–Z; recentes sem as
    /// 3 primeiras favoritas — todas sem favoritas, senão max(6 − favoritas, 3);
    /// a fonte atual na frente se não estiver na lista. Cabeçalho 1 = relógio
    /// (1ª recente), 2 = estrela (1ª favorita).
    func miniList(_ all: [ContentFontItem], current: ContentFontItem?) -> [(ContentFontItem?, Int)] {
        var byKey: [String: ContentFontItem] = [:]
        for f in all where byKey[f.key] == nil { byKey[f.key] = f }
        let favs = favorites.compactMap { byKey[$0] }.sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
        let firstFavs = Set(favs.prefix(3).map(\.key))
        let rec = recents.compactMap { byKey[$0] }.filter { !firstFavs.contains($0.key) }
        let n = favs.isEmpty ? rec.count : max(6 - favs.count, 3)
        let recShown = Array(rec.prefix(n))
        let listed = Set((recShown + favs).map(\.key))
        var out: [(ContentFontItem?, Int)] = []
        if current == nil || !listed.contains(current!.key) { out.append((current, 0)) }
        for (i, f) in recShown.enumerated() { out.append((f, i == 0 ? 1 : 0)) }
        for (i, f) in favs.enumerated() { out.append((f, i == 0 ? 2 : 0)) }
        return out
    }
}

/// Prévia de cada face na PRÓPRIA fonte (registrada no CoreText uma vez por arquivo).
enum ContentFontPreview {
    private static var names: [String: String] = [:]
    private static var missing = Set<String>()

    /// nil = arquivo sem face legível (a prévia sai na fonte do app, em texto secundário).
    static func font(_ item: ContentFontItem?, size: CGFloat) -> Font? {
        guard let item else { return .aurea(size: size) }
        if item.path.isEmpty { return item.family.isEmpty ? .aurea(size: size) : .custom(item.family, size: size) }
        if missing.contains(item.path) { return nil }
        if let name = names[item.path] { return .custom(name, size: size) }
        let url = URL(fileURLWithPath: item.path)
        guard FileManager.default.fileExists(atPath: item.path) else { missing.insert(item.path); return nil }
        CTFontManagerRegisterFontsForURL(url as CFURL, .process, nil)
        let descriptors = CTFontManagerCreateFontDescriptorsFromURL(url as CFURL) as? [CTFontDescriptor] ?? []
        // A face pedida (peso/itálico) quando o arquivo traz várias.
        let wanted = descriptors.first { d in
            guard let style = CTFontDescriptorCopyAttribute(d, kCTFontStyleNameAttribute) as? String else { return false }
            return !item.style.isEmpty && style.caseInsensitiveCompare(item.style) == .orderedSame
        } ?? descriptors.first
        guard let name = wanted.flatMap({ CTFontDescriptorCopyAttribute($0, kCTFontNameAttribute) as? String }) else {
            missing.insert(item.path); return nil
        }
        names[item.path] = name
        return .custom(name, size: size)
    }
}

// MARK: - O navegador em tela cheia

/// Barra ‹ · ☰ · título · 🔍, grade de 2 colunas com uma célula de 100 pt por
/// face, ★ para favoritar, ícone das importadas (detalhes e "Apagar fonte") e a
/// gaveta de filtros. Tocar na célula aplica a face (`onPick`) e fecha.
struct FontBrowserView: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var library = FontLibrary.shared
    @ObservedObject private var prefs = FontPrefs.shared
    let onPick: (ContentFontItem) -> Void
    let onDismiss: () -> Void

    @State private var query = ""
    @State private var searching = false
    @State private var drawer = false
    @State private var details: ContentFontItem?
    @State private var confirmDelete = false
    @FocusState private var searchFocused: Bool

    private var visible: [ContentFontItem] {
        let filters = prefs.filters, script = prefs.script
        let cats = Set((0...4).filter { filters & (1 << $0) != 0 })
        let importedOn = filters & FontScripts.filterImported != 0
        let favOn = filters & FontScripts.filterFavorite != 0
        let noCategory = cats.isEmpty && !importedOn
        let list = library.items.filter { f in
            let fav = prefs.favorites.contains(f.key)
            let catOk = noCategory || cats.contains(f.category) || (importedOn && f.imported)
            let scriptOk = script == 0 || (f.scripts & script) != 0
            return ((favOn && fav) || (catOk && scriptOk && (!favOn || !noCategory || fav))) && fontMatches(f.name, query) >= 0
        }
        func alpha(_ a: ContentFontItem, _ b: ContentFontItem) -> Bool {
            let c = a.family.localizedCaseInsensitiveCompare(b.family)
            if c != .orderedSame { return c == .orderedAscending }
            if a.italic != b.italic { return !a.italic }
            return a.weight < b.weight
        }
        if prefs.sort == 0 {
            return list.sorted { a, b in
                let ua = prefs.useCount[a.key] ?? 0, ub = prefs.useCount[b.key] ?? 0
                return ua != ub ? ua > ub : alpha(a, b)
            }
        }
        return list.sorted(by: alpha)
    }

    private var title: String {
        let filters = prefs.filters
        let cats = FontScripts.categories.filter { filters & (1 << $0.0) != 0 }
        let favOn = filters & FontScripts.filterFavorite != 0
        let importedOn = filters & FontScripts.filterImported != 0
        let count = cats.count + (favOn ? 1 : 0) + (importedOn ? 1 : 0)
        let scriptLabel = FontScripts.menu.first { $0.0 == prefs.script }?.1
        if count == 0 && scriptLabel == nil { return AureaText.t("om_fb_title_all") }
        if count == 0, let scriptLabel { return AureaText.t("om_fb_title_of", AureaText.t(scriptLabel)) }
        if count == 1 && scriptLabel == nil {
            return AureaText.t("om_fb_title_of", AureaText.t(cats.first?.1 ?? (favOn ? "om_fb_cat_favorite" : "om_fb_cat_imported")))
        }
        return AureaText.t("om_fb_title_filtered")
    }

    var body: some View {
        ZStack(alignment: .topLeading) {
            AureaColors.contentBrowser.ignoresSafeArea()
            VStack(spacing: 0) {
                bar
                let rows = visible
                if rows.isEmpty {
                    Text(AureaText.t("om_fb_empty")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                } else {
                    ScrollView {
                        LazyVGrid(columns: [GridItem(.flexible(), spacing: 6), GridItem(.flexible(), spacing: 6)], spacing: 6) {
                            ForEach(rows, id: \.self) { f in cell(f) }
                        }
                        .padding(8)
                    }
                    .accessibilityIdentifier("font.browser.grid")
                }
            }
            if drawer {
                HStack(spacing: 0) {
                    filterDrawer
                    AureaColors.fxScrim.contentShape(Rectangle()).onTapGesture { drawer = false }
                        .accessibilityLabel(AureaText.t("common_close"))
                }
                .ignoresSafeArea(edges: .bottom)
            }
            if let f = details {
                if confirmDelete {
                    AureaAlert(title: AureaText.t("om_fb_delete_confirm"), confirmLabel: AureaText.t("om_fb_delete_font"), destructive: true,
                               onConfirm: { if model.removeImportedFont(f) { model.toast = AureaText.t("om_fb_deleted") } },
                               onDismiss: { details = nil; confirmDelete = false })
                } else {
                    AureaDialog(title: f.name, message: importedOn(f).map { AureaText.t("om_fb_imported_on", $0) }, actions: [
                        DialogAction(AureaText.t("common_close"), role: .negative),
                        DialogAction(AureaText.t("om_fb_delete_font"), role: .neutral, destructive: true, dismiss: false,
                                     tag: "font.item.\(f.key).delete") { confirmDelete = true },
                    ], onDismiss: { details = nil }, tag: "font.details")
                }
            }
        }
        .foregroundStyle(AureaColors.text)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("font.browser")
        .onAppear { library.reload(model.engine) }
    }

    private var bar: some View {
        HStack(spacing: 0) {
            barIcon(AureaText.t("common_close"), "font.browser.close", action: onDismiss) {
                CupertinoGlyph.text(CupertinoGlyph.ChevronLeft, size: 22).mirrorsInRtl()
            }
            if !searching {
                barIcon(AureaText.t("om_fb_menu_cd"), "font.browser.menu", action: { drawer = true }) {
                    CupertinoGlyph.text(CupertinoGlyph.LineHorizontal3, size: 22)
                }
            }
            ZStack {
                if searching {
                    TextField(AureaText.t("panel_buscar_fonte"), text: $query)
                        .font(.aurea(size: 14)).foregroundStyle(AureaColors.accent).tint(AureaColors.accent)
                        .textInputAutocapitalization(.never).autocorrectionDisabled()
                        .submitLabel(.done)
                        .focused($searchFocused)
                        .accessibilityIdentifier("font.browser.search.field")
                        .onAppear { searchFocused = true }
                } else {
                    Text(title).font(.aurea(size: 14, weight: .semibold)).lineLimit(1)
                        .accessibilityAddTraits(.isHeader)
                }
            }
            .frame(maxWidth: .infinity)
            if searching {
                barIcon(AureaText.t("common_cancel"), "font.browser.search.cancel", action: { searching = false; query = "" }) {
                    CupertinoGlyph.text(CupertinoGlyph.Xmark, size: 20)
                }
            } else {
                barIcon(AureaText.t("om_fb_search_cd"), "font.browser.search", action: { searching = true }) {
                    CupertinoGlyph.text(CupertinoGlyph.Search, size: 20)
                }
            }
        }
        .frame(height: 44)
        .overlay(alignment: .bottom) { AureaColors.contentBrowserLine.frame(height: 0.5) }
    }

    private func barIcon<Icon: View>(_ label: String, _ tag: String, action: @escaping () -> Void, @ViewBuilder icon: () -> Icon) -> some View {
        Button(action: action) { icon().frame(width: 44, height: 44).contentShape(Rectangle()) }
            .buttonStyle(AureaPressStyle(shrink: 1))
            .accessibilityLabel(label)
            .accessibilityIdentifier(tag)
    }

    private func cell(_ f: ContentFontItem) -> some View {
        let tag = "font.item.\(f.key)"
        let preview = ContentFontPreview.font(f, size: 20)
        let favorite = prefs.favorites.contains(f.key)
        return ZStack(alignment: .topLeading) {
            Button { onPick(f) } label: {
                VStack(alignment: .leading, spacing: 0) {
                    Text(FontScripts.sample(f.scripts, long: true))
                        .font(preview ?? .aurea(size: 20))
                        .foregroundStyle(preview != nil ? AureaColors.text : AureaColors.muted)
                        .lineLimit(1)
                        .frame(maxWidth: .infinity, minHeight: 46, alignment: .leading)
                    highlighted(f.name)
                        .font(.aurea(size: 12))
                        .lineLimit(1).truncationMode(.tail)
                        .padding(.trailing, 30)
                }
                .padding(.horizontal, 10)
                .frame(maxWidth: .infinity, minHeight: 100, maxHeight: 100, alignment: .leading)
                .contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle(shrink: 1))
            .accessibilityLabel(f.name)
            .accessibilityIdentifier(tag)
            if f.imported {
                Button { confirmDelete = false; details = f } label: {
                    CupertinoGlyph.text(CupertinoGlyph.ArrowDownCircleFill, size: 18, color: AureaColors.text.opacity(0.38))
                        .frame(width: 36, height: 36).contentShape(Rectangle())
                }
                .buttonStyle(AureaPressStyle(shrink: 1))
                .accessibilityLabel(f.name)
                .accessibilityIdentifier(tag + ".imported")
            }
            Button { prefs.toggleFavorite(f) } label: {
                CupertinoGlyph.text(favorite ? CupertinoGlyph.StarFill : CupertinoGlyph.Star, size: 18, color: favorite ? AureaColors.accent : AureaColors.text)
                    .frame(width: 44, height: 44).contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle(shrink: 1))
            .accessibilityLabel(AureaText.t(favorite ? "om_fb_fav_remove_cd" : "om_fb_fav_add_cd"))
            .accessibilityAddTraits(favorite ? .isSelected : [])
            .accessibilityIdentifier(tag + ".fav")
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottomTrailing)
        }
        .frame(height: 100)
        .clipShape(RoundedRectangle(cornerRadius: 6))
        .overlay(RoundedRectangle(cornerRadius: 6).stroke(AureaColors.contentBrowserLine, lineWidth: 1))
    }

    /// Nome com o trecho buscado em destaque e o resto em texto secundário.
    private func highlighted(_ name: String) -> Text {
        let at = fontMatches(name, query)
        let q = query.trimmingCharacters(in: .whitespaces)
        guard !q.isEmpty, at >= 0 else { return Text(name).foregroundColor(AureaColors.text) }
        let chars = Array(name)
        let end = min(chars.count, at + q.count)
        let start = min(at, end)
        return Text(String(chars[0..<start])).foregroundColor(AureaColors.muted)
            + Text(String(chars[start..<end])).foregroundColor(AureaColors.accent)
            + Text(String(chars[end...])).foregroundColor(AureaColors.muted)
    }

    private func importedOn(_ f: ContentFontItem) -> String? {
        guard let date = (try? FileManager.default.attributesOfItem(atPath: f.path))?[.modificationDate] as? Date else { return nil }
        let format = DateFormatter()
        format.dateStyle = .medium
        format.timeStyle = .none
        return format.string(from: date)
    }

    // MARK: Gaveta de filtros

    @State private var sortOpen = false
    @State private var scriptOpen = false

    private var filterDrawer: some View {
        let hasImported = library.items.contains { $0.imported }
        let scripts = library.items.reduce(0) { $0 | $1.scripts }
        return ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                Text(AureaText.t("om_fb_menu_cd")).font(.aurea(size: 14, weight: .semibold))
                    .padding(.horizontal, 16).frame(maxWidth: .infinity, minHeight: 42, alignment: .leading)
                    .background(AureaColors.contentDrawerHeader)
                Color.clear.frame(height: 12)
                ForEach(0..<FontScripts.categories.count, id: \.self) { i in
                    let bit = FontScripts.categories[i].0
                    filterBox(AureaText.t(FontScripts.categories[i].1), "font.filter.\(FontScripts.categoryTags[i])", prefs.filters & (1 << bit) != 0) {
                        prefs.toggleFilter(1 << bit)
                    }
                }
                filterBox(AureaText.t("om_fb_cat_favorite"), "font.filter.favorite", prefs.filters & FontScripts.filterFavorite != 0) {
                    prefs.toggleFilter(FontScripts.filterFavorite)
                }
                if hasImported {
                    filterBox(AureaText.t("om_fb_cat_imported"), "font.filter.imported", prefs.filters & FontScripts.filterImported != 0) {
                        prefs.toggleFilter(FontScripts.filterImported)
                    }
                }
                divider
                Text(AureaText.t("om_fb_sort")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).padding(.leading, 16).padding(.top, 6)
                menuBox(AureaText.t(prefs.sort == 0 ? "om_fb_sort_used" : "om_fb_sort_alpha"), "font.sort") { sortOpen = true }
                    .aureaAnchoredMenu(isPresented: $sortOpen, matchAnchorWidth: true, tag: "font.sort.menu") {
                        [MenuEntry.item(AureaText.t("om_fb_sort_used"), selected: prefs.sort == 0) { prefs.setSort(0) },
                         MenuEntry.item(AureaText.t("om_fb_sort_alpha"), selected: prefs.sort == 1) { prefs.setSort(1) }]
                    }
                    .padding(.horizontal, 16).padding(.vertical, 6)
                Text(AureaText.t("om_fb_script")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).padding(.leading, 16).padding(.top, 6)
                menuBox(AureaText.t(FontScripts.menu.first { $0.0 == prefs.script }?.1 ?? "om_fb_script_all"), "font.script") { scriptOpen = true }
                    .aureaAnchoredMenu(isPresented: $scriptOpen, matchAnchorWidth: true, tag: "font.script.menu") {
                        [MenuEntry.item(AureaText.t("om_fb_script_all"), selected: prefs.script == 0) { prefs.setScript(0) }]
                            + FontScripts.menu.filter { scripts & $0.0 != 0 }.map { entry in
                                MenuEntry.item(AureaText.t(entry.1), selected: prefs.script == entry.0) { prefs.setScript(entry.0) }
                            }
                    }
                    .padding(.horizontal, 16).padding(.vertical, 6)
                divider
                Button {
                    drawer = false
                    let model = self.model
                    FontImportPicker.presentMultiple { urls in
                        for url in urls { FontLibrary.shared.importFont(url, model: model) }
                    }
                } label: {
                    HStack(spacing: 10) {
                        CupertinoGlyph.text(CupertinoGlyph.ArrowDownCircleFill, size: 18)
                        Text(AureaText.t("om_fb_import")).font(.aurea(size: 14))
                        Spacer(minLength: 0)
                    }
                    .padding(.horizontal, 16).frame(height: 48).contentShape(Rectangle())
                }
                .buttonStyle(AureaPressStyle(shrink: 1))
                .accessibilityIdentifier("font.import")
            }
        }
        .frame(width: 280)
        .background(AureaColors.contentDrawer.ignoresSafeArea(edges: .bottom))
    }

    private var divider: some View { AureaColors.contentBrowserLine.frame(height: 1).padding(10) }

    private func filterBox(_ label: String, _ tag: String, _ on: Bool, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 12) {
                ZStack {
                    RoundedRectangle(cornerRadius: 4).fill(on ? AureaColors.accent : AureaColors.contentDrawer)
                    RoundedRectangle(cornerRadius: 4).stroke(on ? AureaColors.accent : AureaColors.muted, lineWidth: 1.5)
                    if on { CupertinoGlyph.text(CupertinoGlyph.CheckmarkAlt, size: 14, color: AureaColors.onAccent) }
                }
                .frame(width: 20, height: 20)
                Text(label).font(.aurea(size: 14))
                Spacer(minLength: 0)
            }
            .padding(.horizontal, 16).frame(height: 44).contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .accessibilityAddTraits(on ? .isSelected : [])
        .accessibilityIdentifier(tag)
    }

    private func menuBox(_ value: String, _ tag: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 6) {
                Text(value).font(.aurea(size: 14)).lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
                CupertinoGlyph.text(CupertinoGlyph.ChevronDown, size: 14, color: AureaColors.muted)
            }
            .padding(.horizontal, 12).frame(height: 44)
            .background(AureaColors.contentStrongLine, in: RoundedRectangle(cornerRadius: 6))
            .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .accessibilityIdentifier(tag)
    }
}

/// Pedido de abrir o navegador de fontes fora do modo de texto (o Dock da UI Edição
/// e a linha "Fonte" das Opções de texto).
@MainActor final class FontBrowserRequest: ObservableObject {
    static let shared = FontBrowserRequest()
    @Published var open = false
}
