// =============================================================================
//  Aurea / platform / ios / app / ProjectCards.swift
//
//  Os cartões de projeto e tudo o que os cerca (Omega 0.3.2, área 01): a linha
//  de 96 pt da referência, a ficha 2 × 2, o vazio, as pílulas, a linha "Meus
//  projetos", o menu rápido, os diálogos claros de um projeto e a BIBLIOTECA —
//  a leitura real do disco.
//
//  Nada de estado falso: a lista vem dos `.aurea` que estão em Documents e a
//  ficha de cada um vem do sidecar `<nome>.aurea.meta.json` — o MESMO arquivo
//  que o Android grava (`EditorStore.saveBlocking`), com as mesmas chaves
//  (`title`, `width`, `height`, `fps`, `durationFrames`, `thumbnail`). Sem
//  sidecar, o cartão sai com o nome do arquivo e sem a resolução — como no
//  Android, em vez de inventar um número.
//
//  A miniatura é `Thumbs/<nome>.jpg`, o mesmo caminho do Android, decodificada
//  fora da thread principal no tamanho em que ela aparece.
// =============================================================================
import SwiftUI
import ImageIO

// =============================================================================
// A ficha de um projeto, como a Home precisa dela
// =============================================================================
struct HomeProjectEntry: Identifiable, Equatable {
    var file: ProjectFile
    var title: String
    var width: Int
    var height: Int
    var fps: Double
    var durationFrames: Int

    var id: String { file.url.path }
    var modified: Date { file.modified }
    var thumbnailURL: URL { file.thumbnailURL }
    /// Omega home: tamanho do `.aurea` (o scan já lê `.fileSizeKey`).
    var fileSize: Int64 { file.sizeBytes }
}

/// O sidecar de um projeto (o `.meta.json` do Android).
func homeMetaURL(_ url: URL) -> URL {
    URL(fileURLWithPath: url.path + ".meta.json")
}

/// Lê a pasta de projetos de Documents. Roda fora da thread principal.
func scanHomeProjects() -> [HomeProjectEntry] {
    let fm = FileManager.default
    let urls = (try? fm.contentsOfDirectory(at: AureaPaths.documents,
                                            includingPropertiesForKeys: [.contentModificationDateKey, .fileSizeKey],
                                            options: [.skipsHiddenFiles])) ?? []
    var found: [HomeProjectEntry] = []
    for url in urls where url.pathExtension.lowercased() == "aurea" {
        let values = try? url.resourceValues(forKeys: [.contentModificationDateKey, .fileSizeKey])
        let modified = values?.contentModificationDate ?? .distantPast
        let fallback = url.deletingPathExtension().lastPathComponent
        var title = fallback
        var width = 0, height = 0, durationFrames = 0
        var fps = 30.0
        if let data = try? Data(contentsOf: homeMetaURL(url)),
           let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
            if let stored = json["title"] as? String, !stored.isEmpty { title = stored }
            width = (json["width"] as? NSNumber)?.intValue ?? 0
            height = (json["height"] as? NSNumber)?.intValue ?? 0
            fps = (json["fps"] as? NSNumber)?.doubleValue ?? 30
            durationFrames = (json["durationFrames"] as? NSNumber)?.intValue ?? 0
        }
        found.append(HomeProjectEntry(
            file: ProjectFile(url: url, name: title, modified: modified,
                              sizeBytes: Int64(values?.fileSize ?? 0)),
            title: title, width: width, height: height, fps: fps, durationFrames: durationFrames))
    }
    return found.sorted { $0.modified > $1.modified }
}

/// Grava a ficha do projeto (mesmas chaves do sidecar do Android).
func writeHomeProjectMeta(url: URL, title: String, width: Int, height: Int,
                          fps: Double, durationFrames: Int) {
    let thumb = AureaPaths.thumbs.appendingPathComponent(url.deletingPathExtension().lastPathComponent + ".jpg")
    let meta: [String: Any] = ["title": title, "width": width, "height": height,
                               "fps": fps, "durationFrames": durationFrames, "thumbnail": thumb.path]
    guard let data = AureaJSONData(meta, false) else { return }
    try? data.write(to: homeMetaURL(url), options: .atomic)
}

/// Um nome de arquivo livre na pasta de projetos (o `uniqueName` do Android).
func uniqueHomeProjectName(_ title: String) -> String {
    let invalid = CharacterSet(charactersIn: "/\\:").union(.controlCharacters)
    let base = title.components(separatedBy: invalid).joined(separator: "-")
        .trimmingCharacters(in: .whitespacesAndNewlines)
    let safe = base.isEmpty ? AureaText.t("editor_projeto_cbe9") : base
    let fm = FileManager.default
    var name = safe
    var index = 2
    while fm.fileExists(atPath: AureaPaths.documents.appendingPathComponent(name + ".aurea").path) {
        name = "\(safe) (\(index))"
        index += 1
    }
    return name
}

/// Duplica um projeto: o `.aurea`, a miniatura e o sidecar com " (cópia)".
@discardableResult
func duplicateHomeProject(_ entry: HomeProjectEntry) -> Bool {
    let fm = FileManager.default
    let title = AureaText.t("ios_copy_suffix", entry.title)
    let name = uniqueHomeProjectName(title)
    let target = AureaPaths.documents.appendingPathComponent(name + ".aurea")
    let temporary = URL(fileURLWithPath: target.path + ".tmp")
    do {
        try fm.copyItem(at: entry.file.url, to: temporary)
        try fm.moveItem(at: temporary, to: target)
    } catch {
        try? fm.removeItem(at: temporary)
        try? fm.removeItem(at: target)
        return false
    }
    let thumb = AureaPaths.thumbs.appendingPathComponent(name + ".jpg")
    try? fm.copyItem(at: entry.thumbnailURL, to: thumb)
    writeHomeProjectMeta(url: target, title: title, width: entry.width, height: entry.height,
                         fps: entry.fps, durationFrames: entry.durationFrames)
    return true
}

/// Apaga projetos: o `.aurea`, a miniatura, o sidecar e os arquivos de
/// recuperação do motor que moram ao lado (o mesmo do `deleteProjects`).
func deleteHomeProjects(_ paths: [String]) {
    let fm = FileManager.default
    for path in paths {
        let url = URL(fileURLWithPath: path)
        let stem = url.deletingPathExtension().lastPathComponent
        try? fm.removeItem(at: AureaPaths.thumbs.appendingPathComponent(stem + ".jpg"))
        try? fm.removeItem(at: homeMetaURL(url))
        try? fm.removeItem(at: url)
        let siblings = (try? fm.contentsOfDirectory(at: url.deletingLastPathComponent(),
                                                    includingPropertiesForKeys: nil,
                                                    options: [.skipsHiddenFiles])) ?? []
        for sibling in siblings where sibling.lastPathComponent.hasPrefix(url.lastPathComponent + ".") {
            try? fm.removeItem(at: sibling)
        }
    }
}

/// Renomear na Home reescreve só o TÍTULO do sidecar (`renameProjectFile`): o
/// arquivo `.aurea` e a miniatura não mudam de nome.
func renameHomeProjectTitle(path: String, title: String) {
    let clean = title.trimmingCharacters(in: .whitespacesAndNewlines)
    guard !clean.isEmpty else { return }
    let url = URL(fileURLWithPath: path)
    let file = homeMetaURL(url)
    var meta: [String: Any] = [:]
    if let data = try? Data(contentsOf: file),
       let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] { meta = json }
    meta["title"] = clean
    guard let data = AureaJSONData(meta, false) else { return }
    try? data.write(to: file, options: .atomic)
}

/// A biblioteca da Home. Lê o disco numa fila própria e publica o resultado.
@MainActor
final class HomeLibrary: ObservableObject {
    @Published private(set) var all: [HomeProjectEntry] = []
    /// Enquanto o disco não respondeu, nada: o estado vazio não pode piscar.
    @Published private(set) var loaded = false

    private let queue = DispatchQueue(label: "com.aurea.home.library", qos: .userInitiated)

    func refresh() {
        queue.async {
            let list = scanHomeProjects()
            Task { @MainActor in
                self.all = list
                self.loaded = true
            }
        }
    }
}

// =============================================================================
// A ficha de um projeto e a ordem da lista (ProjectSpec.kt)
// =============================================================================
/// Um formato de quadro oferecido ao criar um projeto. O rótulo ("16:9") é
/// universal; a dica vem do catálogo.
struct ProjectAspectOption {
    var key: String
    var label: String
    var hint: String
    var ratio: CGFloat
}

enum ProjectPresets {
    static let aspects: [ProjectAspectOption] = [
        ProjectAspectOption(key: "16:9", label: "16:9", hint: "aspect_hint_tv", ratio: 16.0 / 9.0),
        ProjectAspectOption(key: "9:16", label: "9:16", hint: "aspect_hint_reels", ratio: 9.0 / 16.0),
        ProjectAspectOption(key: "1:1", label: "1:1", hint: "aspect_hint_feed", ratio: 1),
        ProjectAspectOption(key: "4:5", label: "4:5", hint: "aspect_hint_instagram", ratio: 4.0 / 5.0),
        ProjectAspectOption(key: "4:3", label: "4:3", hint: "aspect_hint_classic", ratio: 4.0 / 3.0),
    ]
    /// "Livre": os dois números na mão; a razão sai deles.
    static let free = ProjectAspectOption(key: "livre", label: "1:1", hint: "aspect_free_hint", ratio: 1)

    static let resolutions: [Int] = [720, 1080, 1440, 2160]
    static let fpsOptions: [Int] = [24, 30, 60]

    static func aspectByKey(_ key: String) -> ProjectAspectOption {
        aspects.first { $0.key == key } ?? aspects[0]
    }

    static func resolutionLabel(_ height: Int) -> String {
        switch height {
        case 720: return "HD 720p"
        case 1080: return "Full HD 1080p"
        case 1440: return "QHD 1440p"
        case 2160: return "4K 2160p"
        default: return "\(height)p"
        }
    }
}

/// fps como o app mostra: inteiro quando é inteiro (30), senão uma casa
/// (29,97 → "29.97").
func homeFormatFps(_ fps: Double) -> String {
    let whole = fps.rounded()
    if abs(fps - whole) < 0.01 { return String(Int(whole)) }
    var text = String(format: "%.2f", fps)
    while text.hasSuffix("0") { text.removeLast() }
    if text.hasSuffix(".") { text.removeLast() }
    return text
}

/// A ficha de um projeto: "4:5 · Full HD 1080p · 30 fps" (`projectSpec`).
/// Proporção = o preset cuja razão difere < 0,01; resolução = o lado menor.
/// Sem medida gravada (sidecar antigo) a resolução fica de fora em vez de
/// mostrar "0p".
func homeProjectSpec(_ entry: HomeProjectEntry) -> String {
    let ratio = entry.width > 0 && entry.height > 0
        ? CGFloat(entry.width) / CGFloat(entry.height) : 0
    let aspect = ProjectPresets.aspects.first { abs($0.ratio - ratio) < 0.01 } ?? ProjectPresets.aspects[0]
    let side = min(entry.width, entry.height)
    let fps = homeFormatFps(entry.fps)
    if side > 0 { return "\(aspect.label) · \(ProjectPresets.resolutionLabel(side)) · \(fps) fps" }
    return "\(aspect.label) · \(fps) fps"
}

/// O que um chip de metadados da linha do projeto mostra (o ícone sai daqui).
enum HomeProjectChipKind: Equatable {
    case fps, aspect, resolution, size
    var glyph: Character {
        switch self {
        case .fps: return CupertinoGlyph.Speedometer
        case .aspect: return CupertinoGlyph.Crop
        case .resolution: return CupertinoGlyph.Tv
        case .size: return CupertinoGlyph.Fullscreen
        }
    }
}

struct HomeProjectChip: Equatable {
    let kind: HomeProjectChipKind
    let text: String
}

/// Os chips da linha do projeto na lista (spec am-ui §6, P2-8): fps, proporção,
/// resolução e tamanho do quadro, todos do sidecar que a Home já lê (nada de
/// rede). Sem medida gravada só o fps aparece; proporção fora dos presets sai
/// reduzida ("21:9", "2.39:1"). Par do `projectChips` do Android.
func homeProjectChips(_ entry: HomeProjectEntry) -> [HomeProjectChip] {
    let fps = HomeProjectChip(kind: .fps, text: "\(homeFormatFps(entry.fps)) fps")
    guard entry.width > 0 && entry.height > 0 else { return [fps] }
    return [
        fps,
        HomeProjectChip(kind: .aspect, text: homeAspectLabel(width: entry.width, height: entry.height)),
        HomeProjectChip(kind: .resolution, text: ProjectPresets.resolutionLabel(min(entry.width, entry.height))),
        HomeProjectChip(kind: .size, text: "\(entry.width) × \(entry.height)"),
    ]
}

/// "16:9" dos presets (< 0,01 de diferença); senão a razão reduzida, ou "x.xx:1".
func homeAspectLabel(width: Int, height: Int) -> String {
    let ratio = CGFloat(width) / CGFloat(height)
    if let preset = ProjectPresets.aspects.first(where: { abs($0.ratio - ratio) < 0.01 }) { return preset.label }
    var a = width, b = height
    while b != 0 { (a, b) = (b, a % b) }
    let w = width / a, h = height / a
    if w <= 99 && h <= 99 { return "\(w):\(h)" }
    return ratio >= 1 ? String(format: "%.2f:1", locale: Locale(identifier: "en_US_POSIX"), Double(ratio))
        : String(format: "1:%.2f", locale: Locale(identifier: "en_US_POSIX"), Double(1 / ratio))
}

/// Chips de metadados da linha (fps, proporção, resolução, tamanho), cada um
/// com o seu ícone; quebram de linha quando a coluna é estreita. O leitor de
/// tela lê os quatro de uma vez (`home.project.meta`, o mesmo id do Android).
struct HomeProjectChips: View {
    let entry: HomeProjectEntry
    var body: some View {
        AureaFlowLayout(hGap: 6, vGap: 4) {
            ForEach(Array(homeProjectChips(entry).enumerated()), id: \.offset) { _, chip in
                HStack(spacing: 4) {
                    CupertinoGlyph.text(chip.kind.glyph, size: 11, color: AureaColors.muted)
                    Text(chip.text).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).lineLimit(1)
                }
                .padding(.horizontal, 6).padding(.vertical, 3)
                .background(AureaColors.surface, in: RoundedRectangle(cornerRadius: 6))
            }
        }
        .accessibilityElement(children: .combine)
        .accessibilityIdentifier("home.project.meta")
    }
}

/// Razão para a moldura do placeholder (≤ 0 → 16:9).
func homeProjectRatio(_ entry: HomeProjectEntry) -> CGFloat {
    entry.width > 0 && entry.height > 0
        ? CGFloat(entry.width) / CGFloat(entry.height) : 16.0 / 9.0
}

/// A lista arrumada (`projetosArrumados`): filtra pelo nome (contém, sem
/// diferenciar maiúsculas) e ordena. "Mais recentes" = a ordem do disco.
func arrangeHomeProjects(_ all: [HomeProjectEntry], sort: HomeSortOrder, query: String) -> [HomeProjectEntry] {
    let trimmed = query.trimmingCharacters(in: .whitespacesAndNewlines)
    let filtered = trimmed.isEmpty
        ? all
        : all.filter { $0.title.range(of: trimmed, options: .caseInsensitive) != nil }
    switch sort {
    case .recent: return filtered
    case .name: return filtered.sorted { $0.title.localizedLowercase < $1.title.localizedLowercase }
    case .longest:
        return filtered.sorted { seconds($0) > seconds($1) }
    }
}

private func seconds(_ entry: HomeProjectEntry) -> Double {
    entry.fps > 0 ? Double(entry.durationFrames) / entry.fps : 0
}

/// A ordem da lista. O ÍNDICE é o que fica gravado nas preferências — a
/// identidade é o índice, não o rótulo (que muda de idioma).
enum HomeSortOrder: Int, CaseIterable, Identifiable {
    case recent, name, longest
    var id: Int { rawValue }
    var label: String {
        switch self {
        case .recent: return AureaText.t("home_sort_recent")
        case .name: return AureaText.t("home_sort_name")
        case .longest: return AureaText.t("home_sort_longest")
        }
    }
    /// A ordem equivalente no modelo (usada pela lista de projetos do núcleo).
    var modelSort: AureaModel.ProjectSort {
        switch self {
        case .recent: return .recent
        case .name: return .name
        case .longest: return .longest
        }
    }
}

// =============================================================================
// Omega home (área 01): as contas da Home clara. Par de `HomeOmegaLogic.kt`
// (mesmas regras e textos; os testes JVM de lá descrevem estas também).
// =============================================================================
/// A ficha 2 × 2 da linha: proporção · resolução (lado menor) · tamanho do
/// arquivo · fps. Sem medida gravada, proporção e resolução ficam "—".
struct OmegaProjectFacts: Equatable {
    let aspect: String
    let resolution: String
    let size: String
    let fps: String
}

func omegaProjectFacts(_ entry: HomeProjectEntry, locale: Locale = Locale(identifier: AureaText.language.resolved.rawValue)) -> OmegaProjectFacts {
    let known = entry.width > 0 && entry.height > 0
    return OmegaProjectFacts(
        aspect: known ? homeAspectLabel(width: entry.width, height: entry.height) : "—",
        resolution: known ? "\(min(entry.width, entry.height))p" : "—",
        size: omegaFileSize(entry.fileSize, locale: locale),
        fps: "\(homeFormatFps(entry.fps)) fps")
}

/// Tamanho do arquivo (base 1000): "860 B", "12,3 MB", "1,3 GB"; uma casa abaixo de 100, sem ",0".
func omegaFileSize(_ bytes: Int64, locale: Locale) -> String {
    if bytes <= 0 { return "0 B" }
    if bytes < 1000 { return "\(bytes) B" }
    let units = ["KB", "MB", "GB", "TB"]
    var value = Double(bytes) / 1000
    var unit = 0
    while value >= 1000 && unit < units.count - 1 { value /= 1000; unit += 1 }
    let formatter = omegaFileSizeFormatter(locale: locale, fractionDigits: value < 100 ? 1 : 0)
    return (formatter.string(from: NSNumber(value: value)) ?? String(format: "%.1f", value)) + " " + units[unit]
}

/// 0.3.4: um `NumberFormatter` por (idioma, casas), criado uma vez — cada linha
/// da aba Projetos criava um novo ao aparecer na rolagem.
private let omegaFileSizeFormatterLock = NSLock()
private var omegaFileSizeFormatters: [String: NumberFormatter] = [:]
private func omegaFileSizeFormatter(locale: Locale, fractionDigits: Int) -> NumberFormatter {
    let key = "\(locale.identifier)|\(fractionDigits)"
    omegaFileSizeFormatterLock.lock(); defer { omegaFileSizeFormatterLock.unlock() }
    if let cached = omegaFileSizeFormatters[key] { return cached }
    let formatter = NumberFormatter()
    formatter.locale = locale
    formatter.numberStyle = .decimal
    formatter.usesGroupingSeparator = false
    formatter.minimumFractionDigits = 0
    formatter.maximumFractionDigits = fractionDigits
    formatter.roundingMode = .halfEven
    omegaFileSizeFormatters[key] = formatter
    return formatter
}

/// Duração: "m:ss" abaixo de 1 h, "h:mm:ss" acima; nil sem duração gravada.
func omegaProjectDuration(frames: Int, fps: Double) -> String? {
    guard frames > 0, fps.isFinite, fps > 0 else { return nil }
    let total = max(0, Int((Double(frames) / fps).rounded()))
    let h = total / 3600, m = (total % 3600) / 60, s = total % 60
    return h > 0 ? String(format: "%d:%02d:%02d", h, m, s) : String(format: "%d:%02d", m, s)
}

/// Um item da lista da aba Projetos: cabeçalho de data ou uma fileira de linhas.
enum OmegaProjectListItem: Identifiable, Equatable {
    case dateHeader(Date)
    case cards([HomeProjectEntry])
    var id: String {
        switch self {
        case .dateHeader(let day): return "dia-\(Int(day.timeIntervalSince1970))"
        case .cards(let entries): return "linha-" + entries.map(\.id).joined(separator: "|")
        }
    }
}

/// Com "Mais recentes", um cabeçalho de data antes do primeiro projeto de cada
/// dia; `columns` > 1 agrupa as linhas em fileiras sem atravessar o cabeçalho.
func omegaProjectListItems(_ arranged: [HomeProjectEntry], sort: HomeSortOrder, columns: Int,
                           calendar: Calendar = .current) -> [OmegaProjectListItem] {
    let cols = max(1, columns)
    var out: [OmegaProjectListItem] = []
    var run: [HomeProjectEntry] = []
    func flush() {
        var i = 0
        while i < run.count { out.append(.cards(Array(run[i..<min(run.count, i + cols)]))); i += cols }
        run.removeAll()
    }
    var lastDay: Date?
    for entry in arranged {
        if sort == .recent {
            let day = calendar.startOfDay(for: entry.modified)
            if day != lastDay { flush(); out.append(.dateHeader(day)); lastDay = day }
        }
        run.append(entry)
    }
    flush()
    return out
}

/// Colunas de linhas (mínimo 360 pt cada, 12 entre elas).
func omegaProjectColumns(width: CGFloat) -> Int {
    min(4, max(1, Int(((width - 32) + 12) / (360 + 12))))
}

/// Topo do cartão do menu rápido: embaixo se couber até o fim da área, senão
/// ACIMA do item (referência: `104 + 48 n`, sobe `−(104 + 48n) − 8`).
func omegaQuickMenuTop(rowTop: CGFloat, rowHeight: CGFloat, options: Int, areaBottom: CGFloat,
                       gap: CGFloat = 8, optionHeight: CGFloat = 48) -> CGFloat {
    let menu = optionHeight * CGFloat(options)
    let below = rowTop + rowHeight + gap
    return below + menu <= areaBottom ? below : rowTop - gap - menu
}

/// Nome sugerido: o primeiro "Projeto N" ainda não usado.
func omegaSuggestedName(_ titles: [String], nameFor: (Int) -> String) -> String {
    let used = Set(titles.map { $0.trimmingCharacters(in: .whitespacesAndNewlines).lowercased() })
    var n = 1
    while n < 10_000 && used.contains(nameFor(n).lowercased()) { n += 1 }
    return nameFor(n)
}

// =============================================================================
// Miniaturas: decodificadas uma vez, no tamanho de exibição, fora da main
// =============================================================================
final class HomeThumbCache {
    static let shared = HomeThumbCache()
    private let cache = NSCache<NSString, UIImage>()
    private let lock = NSLock()
    private var epoch: UInt64 = 0
    var generation: UInt64 { lock.lock(); defer { lock.unlock() }; return epoch }

    init() { cache.totalCostLimit = 24 * 1024 * 1024 }

    func clear() {
        lock.lock(); epoch &+= 1; cache.removeAllObjects(); lock.unlock()
    }

    func image(_ key: String) -> UIImage? { cache.object(forKey: key as NSString) }

    func store(_ key: String, _ image: UIImage, generation: UInt64) {
        lock.lock(); defer { lock.unlock() }
        guard generation == epoch else { return }
        cache.setObject(image, forKey: key as NSString,
                        cost: Int(image.size.width * image.size.height * 4))
    }
}

/// Decodifica no tamanho pedido (o `inSampleSize` do Android, pelo lado do iOS).
func decodeHomeThumbnail(path: String, maxPx: CGFloat) -> UIImage? {
    guard maxPx.isFinite, maxPx > 0,
          let source = CGImageSourceCreateWithURL(URL(fileURLWithPath: path) as CFURL,
                                                  [kCGImageSourceShouldCache: false] as CFDictionary),
          let thumbnail = CGImageSourceCreateThumbnailAtIndex(source, 0, [
            kCGImageSourceCreateThumbnailFromImageAlways: true,
            kCGImageSourceCreateThumbnailWithTransform: true,
            kCGImageSourceThumbnailMaxPixelSize: min(maxPx, 2048),
            kCGImageSourceShouldCacheImmediately: true
          ] as CFDictionary) else { return nil }
    return UIImage(cgImage: thumbnail)
}

/// A miniatura de um projeto. A chave leva o carimbo do arquivo: salvar
/// reescreve a capa no MESMO caminho, e sem o carimbo o cache mostraria a
/// imagem velha.
struct HomeThumbnail: View {
    @Environment(\.homeBackdrop) private var backdrop
    let url: URL?
    let stamp: Date
    let maxPx: CGFloat
    let ratio: CGFloat
    /// O hero tem o seu placeholder (degradê + film grande), o cartão tem a
    /// moldura do formato.
    var hero: Bool = false
    /// Omega home: sem imagem ainda, o brilho animado da referência.
    var shimmer: Bool = false

    @State private var image: UIImage?
    /// 0.3.4: a leitura terminou sem imagem (projeto sem capa). O brilho animado
    /// é só para "carregando": sem capa ele ficava rodando para sempre em cada
    /// linha visível, custando quadros na rolagem.
    @State private var missing = false

    private var key: String {
        "\(url?.path ?? "")|\(stamp.timeIntervalSince1970)|\(Int(maxPx))"
    }

    var body: some View {
        // 0.3.4: a capa que já está no cache aparece no PRIMEIRO desenho da linha
        // (ao voltar a rolar), sem passar pelo brilho e por um redesenho a mais.
        let shown = image ?? HomeThumbCache.shared.image(key)
        ZStack {
            if let shown {
                Image(uiImage: shown).resizable().scaledToFill()
            } else if shimmer && missing {
                AureaColors.homeThumb
            } else if shimmer {
                OmegaShimmer()
            } else if hero {
                LinearGradient(colors: [AureaColors.surfaceHigh, AureaColors.surface],
                               startPoint: .topLeading, endPoint: .bottomTrailing)
                    .overlay(CupertinoGlyph.text(CupertinoGlyph.Film, size: 34, color: AureaColors.muted))
            } else {
                HomeFormatPlaceholder(ratio: ratio)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .clipped()
        .onChange(of: image) { _ in backdrop?.invalidate() }
        .task(id: key) {
            let requestKey = key
            let generation = HomeThumbCache.shared.generation
            guard let path = url?.path else { image = nil; missing = true; return }
            if let hit = HomeThumbCache.shared.image(requestKey) { image = hit; missing = false; return }
            let loaded = await Task.detached(priority: .userInitiated) {
                decodeHomeThumbnail(path: path, maxPx: maxPx)
            }.value
            guard !Task.isCancelled, key == requestKey, generation == HomeThumbCache.shared.generation else { return }
            if let loaded { HomeThumbCache.shared.store(requestKey, loaded, generation: generation) }
            image = loaded
            missing = loaded == nil
        }
    }
}

/// Antes da 1ª miniatura: a moldura do formato do projeto.
struct HomeFormatPlaceholder: View {
    let ratio: CGFloat

    var body: some View {
        GeometryReader { bounds in
            let width = ratio >= 1 ? bounds.size.width * 0.52 : bounds.size.height * 0.62 * ratio
            let height = ratio >= 1 ? width / ratio : bounds.size.height * 0.62
            ZStack {
                LinearGradient(colors: [AureaColors.surfaceHigh, AureaColors.surface],
                               startPoint: .topLeading, endPoint: .bottomTrailing)
                RoundedRectangle(cornerRadius: HomeDims.s1)
                    .fill(AureaColors.background.opacity(0.55))
                    .frame(width: width, height: height)
                    .overlay(CupertinoGlyph.text(CupertinoGlyph.Film, size: 18, color: AureaColors.muted))
            }
        }
    }
}

// =============================================================================
// O kit da Home (HomeComponents.kt): botão redondo, botão cheio, cabeçalhos
// =============================================================================
/// home/AureaLogo.kt: the four stroked passes and sphere, on the same 108-unit artboard.
struct HomeBrandLogo: View {
    var body: some View {
        Canvas { context, size in
            var drawing = context
            drawing.scaleBy(x: size.width / 108, y: size.height / 108)
            drawing.fill(Path(CGRect(x: 0, y: 0, width: 108, height: 108)), with: .color(AureaColors.background))
            var tube = Path()
            tube.move(to: CGPoint(x: 26, y: 77))
            tube.addCurve(to: CGPoint(x: 62, y: 29), control1: CGPoint(x: 26, y: 45), control2: CGPoint(x: 39, y: 29))
            tube.addCurve(to: CGPoint(x: 86, y: 49), control1: CGPoint(x: 77, y: 29), control2: CGPoint(x: 86, y: 37))
            tube.addCurve(to: CGPoint(x: 59, y: 68), control1: CGPoint(x: 86, y: 61), control2: CGPoint(x: 76, y: 68))
            tube.addLine(to: CGPoint(x: 44, y: 68))
            func stroke(_ width: CGFloat) -> StrokeStyle { StrokeStyle(lineWidth: width, lineCap: .round, lineJoin: .round) }
            drawing.stroke(tube, with: .color(Color(hex: 0x00134F)), style: stroke(9.9))
            let tubeGradient = Gradient(stops: [.init(color: Color(hex: 0x3E8BF0), location: 0),
                                                .init(color: Color(hex: 0x245D8C), location: 0.45),
                                                .init(color: Color(hex: 0x001A63), location: 1)])
            drawing.stroke(tube, with: .linearGradient(tubeGradient, startPoint: CGPoint(x: 95, y: 24), endPoint: CGPoint(x: 21, y: 84)), style: stroke(9))
            var light = drawing
            light.translateBy(x: -0.75, y: -0.75)
            light.stroke(tube, with: .color(Color(hex: 0x4E9BF5).opacity(140.0 / 255.0)), style: stroke(9 * 0.46))
            light.translateBy(x: -0.85, y: -0.85)
            light.stroke(tube, with: .color(Color(hex: 0xDFF1FF).opacity(153.0 / 255.0)), style: stroke(9 * 0.17))
            let sphereGradient = Gradient(stops: [.init(color: Color(hex: 0x7FC0FF), location: 0),
                                                  .init(color: Color(hex: 0x1F63C8), location: 0.45),
                                                  .init(color: Color(hex: 0x001460), location: 1)])
            drawing.fill(Path(ellipseIn: CGRect(x: 79, y: 68, width: 16, height: 16)),
                         with: .radialGradient(sphereGradient, center: CGPoint(x: 83.4, y: 72), startRadius: 0, endRadius: 16.8))
            drawing.fill(Path(ellipseIn: CGRect(x: 83, y: 71.6, width: 3.2, height: 3.2)), with: .color(Color(hex: 0xEAF6FF).opacity(179.0 / 255.0)))
        }
        .clipShape(RoundedRectangle(cornerRadius: HomeDims.logo * 0.22))
        .accessibilityHidden(true)
    }
}

/// `_BotaoRedondo`: alvo 44 com círculo 36 e ícone 17.
struct HomeRoundIconButton: View {
    let glyph: Character
    let description: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            ZStack {
                Circle().fill(AureaColors.surfaceHigh).frame(width: HomeDims.roundCircle, height: HomeDims.roundCircle)
                CupertinoGlyph.text(glyph, size: 17, color: AureaColors.text)
            }
            .frame(width: HomeDims.roundTarget, height: HomeDims.roundTarget)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityLabel(description)
    }
}

/// `_BotaoCheio`: um botão de largura inteira (criar projeto, ação principal).
struct HomeFillButton: View {
    let label: String
    var glyph: Character? = nil
    let action: () -> Void

    @State private var pressed = false

    var body: some View {
        Button(action: action) {
            HStack(spacing: HomeDims.s2) {
                if let glyph {
                    CupertinoGlyph.text(glyph, size: HomeDims.iconMd, color: AureaColors.onAccent)
                }
                Text(label).aureaFont(.button).foregroundStyle(AureaColors.onAccent)
            }
            .frame(maxWidth: .infinity)
            .frame(height: HomeDims.buttonHeight)
            .background(AureaColors.accent, in: RoundedRectangle(cornerRadius: HomeDims.rLg))
            .overlay(pressed ? HomeColors.pressHighlight : Color.clear)
            .clipShape(RoundedRectangle(cornerRadius: HomeDims.rLg))
        }
        .buttonStyle(.plain)
        .simultaneousGesture(DragGesture(minimumDistance: 0)
            .onChanged { _ in pressed = true }
            .onEnded { _ in pressed = false })
    }
}

/// Cabeçalho de seção: título 21 w700 com uma ação à direita ("Ver todos").
struct HomeSectionHeader: View {
    let text: String
    var actionLabel: String? = nil
    var onAction: (() -> Void)? = nil

    var body: some View {
        HStack(spacing: HomeDims.s2) {
            Text(text).aureaFont(.screenTitle).foregroundStyle(AureaColors.text)
                .frame(maxWidth: .infinity, alignment: .leading)
            if let actionLabel, let onAction {
                Button(action: onAction) {
                    Text(actionLabel)
                        .font(.aurea(size: 13.5, weight: .semibold))
                        .foregroundStyle(AureaColors.accent)
                        .padding(HomeDims.s1)
                }
                .buttonStyle(.plain)
            }
        }
        .padding(.leading, HomeDims.gutter)
        .padding(.trailing, HomeDims.gutter)
        .padding(.top, 26)
        .padding(.bottom, 10)
    }
}

/// Rótulo em caixa-alta.
struct HomeCapsLabel: View {
    let text: String
    var body: some View {
        Text(text.uppercased()).aureaFont(.caps).foregroundStyle(AureaColors.muted)
    }
}

// =============================================================================
// Omega home · P0-5: a linha do projeto da referência (par do ProjectCards.kt)
// =============================================================================
/// Medidas da Home Omega (as da referência; `OmegaHomeDims` do Android).
enum OmegaHomeDims {
    static let topBar: CGFloat = 57
    static let dock: CGFloat = 72
    static let selectionBar: CGFloat = 72
    static let gutter: CGFloat = 16
    static let row: CGFloat = 96
    static let thumb: CGFloat = 80
    static let pill: CGFloat = 40
    static let drawer: CGFloat = 280
    static let drawerRow: CGFloat = 44
    static let scrollShadow: CGFloat = 25
}

/// Tipografia da Home Omega (tamanho e peso da referência, fonte do AUREA).
enum OmegaHomeType {
    static let tabLabel = Font.aurea(size: 12, weight: .medium)
    static let pill = Font.aurea(size: 16, weight: .medium)
    static let listTitle = Font.aurea(size: 18, weight: .bold)
    static let small = Font.aurea(size: 14)
    static let rowTitle = Font.aurea(size: 16, weight: .bold)
    static let meta = Font.aurea(size: 12)
    static let badge = Font.aurea(size: 10, weight: .medium)
    static let menu = Font.aurea(size: 16, weight: .medium)
    static let action = Font.aurea(size: 18)
    static let body = Font.aurea(size: 16)
    static let drawerTitle = Font.aurea(size: 17, weight: .bold)
    static let drawerLabel = Font.aurea(size: 12, weight: .bold)
    static let drawerNote = Font.aurea(size: 11)
}

extension View {
    /// Sombra colorida (a referência usa azul-marinho translúcido, não preto).
    func omegaShadow(_ radius: CGFloat, _ color: Color) -> some View {
        shadow(color: color, radius: radius / 2, x: 0, y: radius / 6)
    }

    /// 0.3.4 (rolagem da aba Projetos): o fundo do cartão com a sombra DESENHADA
    /// no próprio retângulo — a mesma sombra de `omegaShadow`, mas calculada só
    /// sobre a forma. `.shadow` no cartão inteiro sombreava o conteúdo todo
    /// (miniatura, textos, ícones) numa passada fora da tela a cada quadro de
    /// rolagem, em cada linha visível.
    func omegaCardBackground(_ fill: Color, cornerRadius: CGFloat = 12,
                             shadow radius: CGFloat = 12, _ color: Color) -> some View {
        background(RoundedRectangle(cornerRadius: cornerRadius)
            .fill(fill.shadow(.drop(color: color, radius: radius / 2, x: 0, y: radius / 6))))
    }
}

/// O brilho animado do quadrado sem imagem (carregando ou sem miniatura).
struct OmegaShimmer: View {
    @State private var phase: CGFloat = -1
    var body: some View {
        GeometryReader { bounds in
            AureaColors.homeThumb
                .overlay(
                    LinearGradient(colors: [.clear, AureaColors.lightSurface.opacity(0.55), .clear],
                                   startPoint: .leading, endPoint: .trailing)
                        .frame(width: bounds.size.width)
                        .offset(x: phase * bounds.size.width * 1.0)
                )
                .clipped()
        }
        .onAppear {
            withAnimation(.linear(duration: 1.2).repeatForever(autoreverses: false)) { phase = 1 }
        }
        .accessibilityHidden(true)
    }
}

/// A linha de 96 pt: cartão branco de raio 12 com sombra suave, miniatura de 80
/// com o selo de duração, título 16 negrito, ficha 2 × 2 e o ⋯ (menu rápido).
/// Toque abre (ou marca, selecionando); toque longo entra na seleção marcando o
/// projeto (P1-1, com háptico); selecionando, a caixa redonda aparece e o
/// cartão anda 40 pt em 300 ms; os não marcados ganham um véu claro.
struct OmegaProjectRow: View {
    let entry: HomeProjectEntry
    let selecting: Bool
    let marked: Bool
    var interactive = true
    let onOpen: () -> Void
    let onLongPress: () -> Void
    /// Recebe o retângulo global do cartão (o menu rápido desenha a cópia no mesmo lugar).
    let onMenu: (CGRect) -> Void
    /// O retângulo do cartão, guardado sem redesenhar a linha a cada rolagem.
    @State private var box = OmegaFrameBox()

    var body: some View {
        ZStack(alignment: .leading) {
            if selecting {
                ZStack {
                    if marked {
                        Circle().fill(AureaColors.lightAccentInk)
                        CupertinoGlyph.text(CupertinoGlyph.CheckmarkAlt, size: 12, color: AureaColors.lightSurface)
                    } else {
                        Circle().strokeBorder(AureaColors.lightTextMuted, lineWidth: 1.5)
                    }
                }
                .frame(width: 20, height: 20).padding(.leading, 2)
                .accessibilityIdentifier("home.project.check")
                .transition(.opacity)
            }
            card
                .padding(.leading, selecting ? 40 : 0)
                .animation(.easeInOut(duration: 0.3), value: selecting)
        }
        .frame(height: OmegaHomeDims.row)
        .accessibilityIdentifier("home.project.row")
    }

    private var card: some View {
        HStack(spacing: 0) {
            OmegaProjectThumb(entry: entry)
            VStack(alignment: .leading, spacing: 8) {
                Text(entry.title.isEmpty ? AureaText.t("new_project_untitled") : entry.title)
                    .font(OmegaHomeType.rowTitle).foregroundStyle(AureaColors.lightText).lineLimit(1)
                OmegaProjectMeta(entry: entry)
            }
            .frame(maxWidth: .infinity, alignment: .leading).padding(.leading, 16)
            Button { onMenu(box.rect) } label: {
                CupertinoGlyph.text(CupertinoGlyph.Ellipsis, size: 24, color: AureaColors.lightTextMuted.opacity(selecting ? 0.4 : 1))
                    .frame(width: 44, height: 44).contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle())
            .disabled(selecting || !interactive)
            .accessibilityLabel(AureaText.t("home_menu_content"))
            .accessibilityIdentifier("home.project.more")
            .padding(.trailing, 4)
        }
        .padding(8)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .omegaCardBackground(AureaColors.lightSurface, AureaColors.homeShadowSoft)
        .overlay {
            if selecting && !marked {
                RoundedRectangle(cornerRadius: 12).fill(AureaColors.lightSurface.opacity(0.5)).allowsHitTesting(false)
            }
        }
        // O retângulo é medido no toque do ⋯ (sem refazer a linha a cada quadro de rolagem).
        .background(OmegaFrameProbe(box: box))
        .contentShape(RoundedRectangle(cornerRadius: 12))
        .modifier(OmegaRowGestures(enabled: interactive, onTap: onOpen, onLong: {
            UIImpactFeedbackGenerator(style: .medium).impactOccurred()
            onLongPress()
        }))
        .accessibilityElement(children: .combine)
        .accessibilityAddTraits(selecting && marked ? [.isButton, .isSelected] : .isButton)
    }
}

/// O retângulo de um cartão na janela, sem ser estado observado.
///
/// 0.3.4: antes um `GeometryReader` com `onChange(of: frame(in: .global))`
/// regravava o retângulo em CADA linha visível a cada quadro de rolagem. Agora
/// uma sonda UIKit (`OmegaFrameProbe`) mede só quando o ⋯ é tocado.
final class OmegaFrameBox {
    weak var probe: UIView?
    /// Retângulo na janela (= o espaço global da Home, que ocupa a janela toda).
    var rect: CGRect {
        guard let view = probe, view.window != nil else { return .zero }
        return view.convert(view.bounds, to: nil)
    }
}

/// A sonda do `OmegaFrameBox`: uma `UIView` vazia, sem toque nem acessibilidade.
struct OmegaFrameProbe: UIViewRepresentable {
    let box: OmegaFrameBox
    func makeUIView(context: Context) -> UIView {
        let view = UIView()
        view.isUserInteractionEnabled = false
        view.backgroundColor = .clear
        view.isAccessibilityElement = false
        view.accessibilityElementsHidden = true
        box.probe = view
        return view
    }
    func updateUIView(_ view: UIView, context: Context) { box.probe = view }
}

/// Toque e toque longo (0,45 s) só quando a linha é interativa (a cópia do menu não é).
private struct OmegaRowGestures: ViewModifier {
    let enabled: Bool
    let onTap: () -> Void
    let onLong: () -> Void
    @ViewBuilder func body(content: Content) -> some View {
        if enabled { content.homeTapOrLong(onTap, long: onLong) } else { content }
    }
}

/// Miniatura de 80 (raio 8, fundo `homeThumb`, "cover"); sem imagem, o brilho.
struct OmegaProjectThumb: View {
    let entry: HomeProjectEntry
    var body: some View {
        ZStack(alignment: .bottomLeading) {
            HomeThumbnail(url: entry.thumbnailURL, stamp: entry.modified, maxPx: 256,
                          ratio: homeProjectRatio(entry), shimmer: true)
            if let duration = omegaProjectDuration(frames: entry.durationFrames, fps: entry.fps) {
                Text(duration).font(OmegaHomeType.badge).foregroundStyle(AureaColors.homeOnBadge)
                    .padding(.horizontal, 4).padding(.vertical, 1)
                    .background(AureaColors.homeBadge, in: RoundedRectangle(cornerRadius: 4))
                    .padding(4)
                    .accessibilityIdentifier("home.project.duration")
            }
        }
        .frame(width: OmegaHomeDims.thumb, height: OmegaHomeDims.thumb)
        .background(AureaColors.homeThumb)
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }
}

/// A ficha 2 × 2 (168): [proporção][resolução] / [tamanho][fps]; ícone 14 e texto 12.
struct OmegaProjectMeta: View {
    let entry: HomeProjectEntry
    var body: some View {
        let facts = omegaProjectFacts(entry)
        VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 0) {
                cell(CupertinoGlyph.Crop, facts.aspect)
                cell(CupertinoGlyph.Tv, facts.resolution)
            }
            HStack(spacing: 0) {
                cell(CupertinoGlyph.DocText, facts.size)
                cell(CupertinoGlyph.Speedometer, facts.fps)
            }
        }
        .frame(maxWidth: 168, alignment: .leading)
        .accessibilityElement(children: .combine)
        .accessibilityIdentifier("home.project.meta")
    }
    private func cell(_ glyph: Character, _ text: String) -> some View {
        HStack(spacing: 6) {
            CupertinoGlyph.text(glyph, size: 14, color: AureaColors.lightTextMuted)
            Text(text).font(OmegaHomeType.meta).foregroundStyle(AureaColors.lightTextMuted).lineLimit(1)
        }
        .frame(maxWidth: .infinity, alignment: .leading).frame(height: 16)
    }
}

/// P0-7: o vazio centralizado (padding 35): título 18 negrito, corpo com o "+"
/// em negrito, 30 e a ilustração do AUREA (moldura de filme com a marca).
struct OmegaProjectsEmptyState: View {
    var body: some View {
        VStack(spacing: 0) {
            Text(AureaText.t("home_no_projects")).font(OmegaHomeType.listTitle).foregroundStyle(AureaColors.lightText)
                .multilineTextAlignment(.center).accessibilityAddTraits(.isHeader)
            bodyText.font(OmegaHomeType.body).foregroundStyle(AureaColors.lightTextMuted)
                .multilineTextAlignment(.center).padding(.top, 8)
            illustration.padding(.top, 30)
        }
        .padding(.horizontal, 35)
        .accessibilityIdentifier("home.empty")
    }
    private var bodyText: Text {
        let full = AureaText.t("om_home_empty_body", "+")
        guard let range = full.range(of: "+") else { return Text(full) }
        return Text(String(full[..<range.lowerBound]))
            + Text("+").fontWeight(.bold).foregroundColor(AureaColors.lightText)
            + Text(String(full[range.upperBound...]))
    }
    private var illustration: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 14).fill(AureaColors.lightSurface)
            RoundedRectangle(cornerRadius: 14).stroke(AureaColors.lightDivider, lineWidth: 1)
            VStack {
                holes
                Spacer(minLength: 0)
                holes
            }.padding(.vertical, 6)
            HomeBrandLogo().frame(width: 40, height: 40)
        }
        .frame(width: 132, height: 96)
        .omegaShadow(12, AureaColors.homeShadowSoft)
        .accessibilityHidden(true)
    }
    private var holes: some View {
        HStack(spacing: 0) {
            ForEach(0..<6, id: \.self) { _ in
                Spacer(minLength: 0)
                RoundedRectangle(cornerRadius: 2).fill(AureaColors.homeThumb).frame(width: 10, height: 7)
            }
            Spacer(minLength: 0)
        }
    }
}

/// Pílula de subaba (40, raio 12, padding 16/2, texto 16 médio): escolhida =
/// texto claro sobre azul-marinho; não escolhida = cinza sobre branco, que fica
/// azul-marinho sobre lilás enquanto pressionada (cores lidas pelo REA).
struct OmegaPill: View {
    let label: String
    let selected: Bool
    let tag: String
    let action: () -> Void
    var body: some View {
        Button(action: action) { EmptyView() }
            .buttonStyle(OmegaPillStyle(label: label, selected: selected))
            .accessibilityLabel(label)
            .accessibilityAddTraits(selected ? [.isButton, .isSelected] : .isButton)
            .accessibilityIdentifier(tag)
    }
}

private struct OmegaPillStyle: ButtonStyle {
    let label: String
    let selected: Bool
    func makeBody(configuration: Configuration) -> some View {
        let fill = selected ? AureaColors.homePillSelected : configuration.isPressed ? AureaColors.homePillPressed : AureaColors.homePillUnselected
        let ink = selected ? AureaColors.homeOnPillSelected : configuration.isPressed ? AureaColors.homeOnPillPressed : AureaColors.homeOnPillUnselected
        return Text(label).font(OmegaHomeType.pill).foregroundStyle(ink).lineLimit(1)
            .padding(.horizontal, 16).padding(.bottom, 2)
            .frame(minHeight: OmegaHomeDims.pill)
            .background(fill, in: RoundedRectangle(cornerRadius: 12))
            .omegaShadow(12, selected ? AureaColors.homeShadowStrong : AureaColors.homeShadowSoft)
            .animation(.easeOut(duration: 0.15), value: configuration.isPressed)
    }
}

/// A fileira de pílulas (rolagem horizontal; 16 antes, 12 entre, 16 depois).
struct OmegaSubtabs: View {
    let options: [(label: String, tag: String)]
    let selected: Int
    let onSelect: (Int) -> Void
    var body: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 12) {
                ForEach(Array(options.enumerated()), id: \.offset) { index, option in
                    OmegaPill(label: option.label, selected: index == selected, tag: option.tag) { onSelect(index) }
                }
            }.padding(.horizontal, 16).padding(.vertical, 6)
        }
    }
}

/// A linha "Meus projetos": título 18 negrito; à direita a busca (extra D8) e
/// "Ordem: …" com o ícone. 24 pt de desenho num alvo de 44.
struct OmegaListHeader<SortMenu: View>: View {
    let title: String
    let sortLabel: String
    let tag: String
    let sortTag: String
    var searchTag: String? = nil
    var onSearch: (() -> Void)? = nil
    let onSort: () -> Void
    @ViewBuilder let sortMenu: (AnyView) -> SortMenu

    var body: some View {
        HStack(spacing: 0) {
            Text(title).font(OmegaHomeType.listTitle).foregroundStyle(AureaColors.lightText).lineLimit(1)
                .frame(maxWidth: .infinity, alignment: .leading).accessibilityAddTraits(.isHeader)
            if let onSearch {
                Button(action: onSearch) {
                    CupertinoGlyph.text(CupertinoGlyph.Search, size: 20, color: AureaColors.lightText)
                        .frame(width: 44, height: 44).contentShape(Rectangle())
                }.buttonStyle(AureaPressStyle())
                    .accessibilityLabel(AureaText.t("common_search"))
                    .modifier(OptionalIdentifier(tag: searchTag))
            }
            sortMenu(AnyView(
                Button(action: onSort) {
                    HStack(spacing: 4) {
                        Text(sortLabel).font(OmegaHomeType.small).foregroundStyle(AureaColors.lightText).lineLimit(1)
                        CupertinoGlyph.text(CupertinoGlyph.ArrowUpArrowDown, size: 16, color: AureaColors.lightText)
                    }.frame(minHeight: 44).contentShape(Rectangle())
                }.buttonStyle(AureaPressStyle(shrink: 1))
                    .accessibilityIdentifier(sortTag)
            ))
        }
        .padding(.horizontal, OmegaHomeDims.gutter)
        .frame(minHeight: 44)
        .accessibilityIdentifier(tag)
    }
}

/// A sombra de 25 pt no topo da lista, só quando ela saiu do topo (REA: `P.B8K.Rw`).
struct OmegaScrollShadow: View {
    let visible: Bool
    var body: some View {
        LinearGradient(colors: [AureaColors.lightShadow, .clear], startPoint: .top, endPoint: .bottom)
            .frame(height: OmegaHomeDims.scrollShadow)
            .opacity(visible ? 1 : 0)
            .allowsHitTesting(false)
            .accessibilityHidden(true)
    }
}

/// Campo de busca claro (40, raio 8), com foco automático e o ✕ que limpa e fecha.
struct OmegaSearchField: View {
    @Binding var query: String
    let onClear: () -> Void
    @FocusState private var focused: Bool
    var body: some View {
        HStack(spacing: 0) {
            CupertinoGlyph.text(CupertinoGlyph.Search, size: 16, color: AureaColors.lightTextMuted).padding(.leading, 10)
            ZStack(alignment: .leading) {
                if query.isEmpty {
                    Text(AureaText.t("home_search_hint")).font(.aurea(size: 16)).foregroundStyle(AureaColors.lightTextFaint).lineLimit(1)
                }
                TextField("", text: $query).font(.aurea(size: 16)).foregroundStyle(AureaColors.lightText)
                    .tint(AureaColors.lightAccentInk)
                    .textInputAutocapitalization(.sentences).submitLabel(.search).focused($focused)
                    .accessibilityIdentifier("home.projects.searchField")
            }.padding(.horizontal, 8)
            Button(action: onClear) {
                CupertinoGlyph.text(CupertinoGlyph.XmarkCircleFill, size: 18, color: AureaColors.lightTextMuted)
                    .frame(width: 44, height: 40).contentShape(Rectangle())
            }.buttonStyle(.plain).accessibilityLabel(AureaText.t("home_clear_search"))
        }
        .frame(height: 40)
        .background(AureaColors.lightSurface, in: RoundedRectangle(cornerRadius: 8))
        .overlay(RoundedRectangle(cornerRadius: 8).stroke(AureaColors.lightDivider, lineWidth: 0.5))
        .onAppear { focused = true }
    }
}

// =============================================================================
// Omega home · P1-2: o menu rápido do projeto (referência `ProjectItemSelected`)
// =============================================================================
/// Uma opção do menu rápido (texto 16 médio, ícone no fim).
struct OmegaQuickOption: Identifiable {
    let id = UUID()
    let label: String
    let glyph: Character
    let tag: String
    var destructive = false
    let action: () -> Void
}

/// Véu claro na tela inteira (toque fecha; "escape" do VoiceOver também), a
/// cópia da linha no mesmo lugar e, 8 pt abaixo, o cartão de 70 % alinhado ao
/// fim com opções de 48. Se não couber até o fim da área útil, o cartão abre
/// ACIMA do item. Entra com fade de 200 ms. Desenhado na raiz da Home, em
/// coordenadas globais (`frame(in: .global)`).
struct OmegaQuickMenu<RowCopy: View>: View {
    let row: CGRect
    let areaBottom: CGFloat
    let options: [OmegaQuickOption]
    var tag = "home.projectMenu"
    let onDismiss: () -> Void
    @ViewBuilder let rowCopy: () -> RowCopy
    @State private var shown = false
    @Environment(\.layoutDirection) private var direction

    var body: some View {
        GeometryReader { geometry in
            let origin = geometry.frame(in: .global).origin
            let rowLeft = row.minX - origin.x
            let rowTop = row.minY - origin.y
            let width = row.width * 0.7
            let top = omegaQuickMenuTop(rowTop: rowTop, rowHeight: row.height, options: options.count,
                                        areaBottom: areaBottom - origin.y)
            // Alinhado ao FIM da linha (direita em LTR, esquerda em árabe). As
            // coordenadas globais não espelham; `.position` também não.
            let left = direction == .leftToRight ? rowLeft + row.width - width : rowLeft
            ZStack(alignment: .topLeading) {
                AureaColors.homeMenuScrim.ignoresSafeArea()
                    .contentShape(Rectangle()).onTapGesture(perform: onDismiss)
                    .accessibilityLabel(AureaText.t("common_cancel"))
                    .accessibilityAddTraits(.isButton)
                rowCopy()
                    .frame(width: row.width, height: row.height)
                    .position(x: rowLeft + row.width / 2, y: rowTop + row.height / 2)
                    .allowsHitTesting(false)
                    .environment(\.layoutDirection, direction)
                VStack(spacing: 0) {
                    ForEach(options) { option in
                        Button {
                            onDismiss()
                            option.action()
                        } label: {
                            HStack(spacing: 24) {
                                Text(option.label).font(OmegaHomeType.menu).lineLimit(1)
                                    .frame(maxWidth: .infinity, alignment: .leading)
                                CupertinoGlyph.text(option.glyph, size: 20,
                                                    color: option.destructive ? AureaColors.lightDanger : AureaColors.lightText)
                            }
                            .foregroundStyle(option.destructive ? AureaColors.lightDanger : AureaColors.lightText)
                            .padding(.horizontal, 16).frame(height: 48).contentShape(Rectangle())
                        }
                        .buttonStyle(AureaPressStyle(shrink: 1))
                        .accessibilityIdentifier(option.tag)
                    }
                }
                .environment(\.layoutDirection, direction)
                .frame(width: width)
                .background(AureaColors.lightSurface, in: RoundedRectangle(cornerRadius: 12))
                .omegaShadow(12, AureaColors.homeShadowStrong)
                .position(x: left + width / 2, y: top + CGFloat(options.count) * 24)
                .accessibilityIdentifier(tag)
            }
            .environment(\.layoutDirection, .leftToRight)
        }
        .ignoresSafeArea()
        .opacity(shown ? 1 : 0)
        .onAppear { withAnimation(.easeOut(duration: 0.2)) { shown = true } }
        .accessibilityAddTraits(.isModal)
        .accessibilityAction(.escape, onDismiss)
    }
}

// =============================================================================
// Gesto: toque e toque longo, como o `tocavel(onLongClick, onClick)`
// =============================================================================
extension View {
    func homeTap(_ action: @escaping () -> Void) -> some View {
        contentShape(Rectangle()).gesture(TapGesture().onEnded { action() })
    }

    func homeTapOrLong(_ action: @escaping () -> Void, long: @escaping () -> Void) -> some View {
        contentShape(Rectangle())
            .gesture(LongPressGesture(minimumDuration: 0.45)
                .onEnded { _ in long() }
                .exclusively(before: TapGesture().onEnded { action() }))
    }
}

// =============================================================================
// Os diálogos de um projeto (ProjectMenu.kt, Omega home): um por vez, a partir
// do menu rápido (P1-2) e da barra de seleção (P0-6). Todos claros (F4).
// =============================================================================
enum HomeProjectDialog: Equatable {
    case confirmDelete(HomeProjectEntry)
    case rename(HomeProjectEntry)
    /// "Exportar arquivo do projeto": com ou sem a mídia.
    case exportFile(HomeProjectEntry)
    case batchDelete(Set<String>)
}

/// Desenha o diálogo aberto e age.
struct HomeProjectDialogs: View {
    @Binding var state: HomeProjectDialog?
    @ObservedObject var library: HomeLibrary
    @Binding var selection: Set<String>
    @EnvironmentObject private var model: AureaModel
    @State private var shared: SharedProjectFile?

    var body: some View {
        dialogs.sheet(item: $shared) { file in ProjectFileShareSheet(url: file.url) }
    }

    @ViewBuilder private var dialogs: some View {
        switch state {
        case .none:
            EmptyView()
        case .exportFile(let entry):
            AureaDialog(title: AureaText.t("project_file_export"), message: AureaText.t("project_file_export_message"),
                        actions: [true, false].map { withMedia in
                            DialogAction(AureaText.t(withMedia ? "project_file_include_media" : "project_file_without_media"),
                                         role: withMedia ? .positive : .neutral,
                                         tag: withMedia ? "home.exportFile.withMedia" : "home.exportFile.withoutMedia") {
                                selection = []
                                model.exportProjectFile(path: entry.id, title: entry.title, includeMedia: withMedia) { url in
                                    if let url { shared = SharedProjectFile(url: url) }
                                }
                            }
                        } + [DialogAction(AureaText.t("common_cancel"), role: .negative)],
                        onDismiss: close, tag: "home.exportFile")
        case .confirmDelete(let entry):
            AureaAlert(title: entry.title, message: AureaText.t("common_irreversible"),
                       confirmLabel: AureaText.t("project_delete"), destructive: true,
                       onConfirm: {
                           deleteHomeProjects([entry.id])
                           library.refresh()
                       }, onDismiss: close)
        case .rename(let entry):
            AureaNamePrompt(title: AureaText.t("common_rename"), initial: entry.title,
                            onConfirm: { title in
                                let clean = title.trimmingCharacters(in: .whitespacesAndNewlines)
                                if !clean.isEmpty && clean != entry.title { renameHomeProjectTitle(path: entry.id, title: clean) }
                                library.refresh()
                            }, onDismiss: close)
        case .batchDelete(let paths):
            AureaAlert(title: AureaText.t("project_delete_many_title", paths.count), message: AureaText.t("common_irreversible"),
                       confirmLabel: AureaText.t("common_delete"), destructive: true,
                       onConfirm: {
                           deleteHomeProjects(Array(paths))
                           selection = []
                           library.refresh()
                       }, onDismiss: close)
        }
    }

    private func close() { state = nil }
}

// =============================================================================
// As folhas no estilo Cupertino (Sheets.kt): action sheet, alerta e o pedido
// de nome. São do tamanho do app, não do sistema — como no Android.
// =============================================================================
struct HomeSheetAction: Identifiable {
    let id = UUID()
    let label: String
    var destructive: Bool = false
    /// Id de acessibilidade/teste (o mesmo `testTag` do Android); nil = sem.
    var tag: String? = nil
    let action: () -> Void

    init(_ label: String, destructive: Bool = false, tag: String? = nil, action: @escaping () -> Void) {
        self.label = label
        self.destructive = destructive
        self.tag = tag
        self.action = action
    }
}

struct HomeActionSheet: View {
    var title: String? = nil
    var message: String? = nil
    let actions: [HomeSheetAction]
    let onDismiss: () -> Void

    var body: some View {
        ZStack(alignment: .bottom) {
            Color.black.opacity(0.54).ignoresSafeArea()
                .onTapGesture(perform: onDismiss)
            VStack(spacing: HomeDims.s2) {
                VStack(spacing: 0) {
                    if title != nil || message != nil {
                        VStack(spacing: 2) {
                            if let title {
                                Text(title)
                                    .font(.aurea(size: 13, weight: .semibold))
                                    .foregroundStyle(HomeColors.sheetTitle)
                                    .multilineTextAlignment(.center)
                            }
                            if let message {
                                Text(message)
                                    .font(.aurea(size: 13, weight: .regular))
                                    .foregroundStyle(HomeColors.sheetTitle)
                                    .multilineTextAlignment(.center)
                            }
                        }
                        .padding(.horizontal, HomeDims.s4)
                        .padding(.vertical, 13.5)
                        Rectangle().fill(HomeColors.sheetDivider).frame(height: 0.3)
                    }
                    ForEach(Array(actions.enumerated()), id: \.element.id) { index, item in
                        if index > 0 { Rectangle().fill(HomeColors.sheetDivider).frame(height: 0.3) }
                        Button {
                            onDismiss()
                            item.action()
                        } label: {
                            Text(item.label)
                                .font(HomeType.sheetAction)
                                .foregroundStyle(item.destructive ? HomeColors.destructive : AureaColors.accent)
                                .frame(maxWidth: .infinity)
                                .frame(minHeight: 57)
                                .contentShape(Rectangle())
                        }
                        .buttonStyle(.plain)
                        .accessibilityIdentifier(item.tag ?? "")
                    }
                }
                .background(HomeColors.sheetFill, in: RoundedRectangle(cornerRadius: HomeDims.rMd))
                Button(action: onDismiss) {
                    Text(AureaText.t("common_cancel"))
                        .font(HomeType.sheetActionBold)
                        .foregroundStyle(AureaColors.accent)
                        .frame(maxWidth: .infinity)
                        .frame(minHeight: 57)
                        .background(HomeColors.sheetFill, in: RoundedRectangle(cornerRadius: HomeDims.rMd))
                }
                .buttonStyle(.plain)
            }
            .padding(HomeDims.s2)
        }
    }
}

struct HomeAlert: View {
    let title: String
    var message: String? = nil
    var confirmLabel: String
    var cancelLabel: String = AureaText.t("common_cancel")
    var showCancel: Bool = true
    var destructive: Bool = false
    var onConfirm: () -> Void
    let onDismiss: () -> Void

    var body: some View {
        ZStack {
            Color.black.opacity(0.54).ignoresSafeArea()
                .onTapGesture(perform: onDismiss)
            VStack(spacing: 0) {
                VStack(spacing: HomeDims.s1) {
                    Text(title)
                        .font(HomeType.dialogTitle)
                        .foregroundStyle(AureaColors.text)
                        .multilineTextAlignment(.center)
                    if let message {
                        Text(message)
                            .font(HomeType.dialogBody)
                            .foregroundStyle(AureaColors.text)
                            .multilineTextAlignment(.center)
                    }
                }
                .padding(.horizontal, HomeDims.s4)
                .padding(.top, 19)
                .padding(.bottom, HomeDims.s4)
                .frame(maxWidth: .infinity)

                Rectangle().fill(HomeColors.sheetDivider).frame(height: 0.3)
                HStack(spacing: 0) {
                    if showCancel {
                        HomeDialogButton(cancelLabel, bold: false, color: AureaColors.accent) { onDismiss() }
                        Rectangle().fill(HomeColors.sheetDivider).frame(width: 0.3, height: 45)
                    }
                    HomeDialogButton(confirmLabel, bold: true,
                                     color: destructive ? HomeColors.destructive : AureaColors.accent) {
                        onDismiss()
                        onConfirm()
                    }
                }
                .frame(minHeight: 45)
            }
            .frame(width: 270)
            .background(HomeColors.alertFill, in: RoundedRectangle(cornerRadius: HomeDims.rMd))
        }
    }
}

private struct HomeDialogButton: View {
    let label: String
    let bold: Bool
    let color: Color
    let action: () -> Void

    init(_ label: String, bold: Bool, color: Color, action: @escaping () -> Void) {
        self.label = label
        self.bold = bold
        self.color = color
        self.action = action
    }

    var body: some View {
        Button(action: action) {
            Text(label)
                .font(bold ? HomeType.dialogButton : HomeType.dialogButtonPlain)
                .foregroundStyle(color)
                .frame(maxWidth: .infinity)
                .frame(minHeight: 45)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

/// "Renomear" com o campo em foco (texto selecionado para trocar de uma vez),
/// capitalização de frases, Enter salva. Vazio → nada.
struct HomeRenamePrompt: View {
    let initial: String
    let onSave: (String) -> Void
    let onDismiss: () -> Void
    var onFinished: () -> Void = {}

    @State private var value: String = ""
    @FocusState private var focused: Bool

    var body: some View {
        ZStack {
            Color.black.opacity(0.54).ignoresSafeArea()
                .onTapGesture(perform: dismiss)
            VStack(spacing: 0) {
                VStack(spacing: HomeDims.s3) {
                    Text(AureaText.t("common_rename"))
                        .font(HomeType.dialogTitle)
                        .foregroundStyle(AureaColors.text)
                    TextField("", text: $value)
                        .aureaFont(.nameField)
                        .foregroundStyle(AureaColors.text)
                        .textInputAutocapitalization(.sentences)
                        .submitLabel(.done)
                        .focused($focused)
                        .onSubmit { save() }
                        .padding(.horizontal, HomeDims.s2)
                        .padding(.vertical, 7)
                        .background(HomeColors.fieldDialog, in: RoundedRectangle(cornerRadius: 7))
                }
                .padding(.horizontal, HomeDims.s4)
                .padding(.top, 19)
                .padding(.bottom, HomeDims.s4)
                .frame(maxWidth: .infinity)

                Rectangle().fill(HomeColors.sheetDivider).frame(height: 0.3)
                HStack(spacing: 0) {
                    HomeDialogButton(AureaText.t("common_cancel"), bold: false, color: AureaColors.accent) { dismiss() }
                    Rectangle().fill(HomeColors.sheetDivider).frame(width: 0.3, height: 45)
                    HomeDialogButton(AureaText.t("common_save"), bold: true, color: AureaColors.accent) { save() }
                }
                .frame(minHeight: 45)
            }
            .frame(width: 270)
            .background(HomeColors.alertFill, in: RoundedRectangle(cornerRadius: HomeDims.rMd))
        }
        .onAppear {
            value = initial
            focused = true
        }
    }

    private func save() {
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        if !trimmed.isEmpty { onSave(trimmed) }
        dismiss()
    }

    private func dismiss() {
        onDismiss()
        onFinished()
    }
}
