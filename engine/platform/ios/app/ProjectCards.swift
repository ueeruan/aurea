// =============================================================================
//  Aurea / platform / ios / app / ProjectCards.swift
//
//  Os cartões de projeto e tudo o que os cerca: a barra da lista (contagem,
//  busca, ordenação, seleção), a barra de lote, os menus/diálogos de um projeto
//  e a BIBLIOTECA — a leitura real do disco.
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
    let safe = base.isEmpty ? "Projeto" : base
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
    let title = entry.title + " (cópia)"
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

/// Quantos projetos a Início mostra antes do "Ver todos".
let homeRecentOnStart = 6

/// Duas colunas em celular, mais em tela larga.
func homeGridColumns() -> Int {
    let width = UIScreen.main.bounds.width
    if width >= 840 { return 4 }
    if width >= 600 { return 3 }
    return 2
}

// =============================================================================
// Miniaturas: decodificadas uma vez, no tamanho de exibição, fora da main
// =============================================================================
final class HomeThumbCache {
    static let shared = HomeThumbCache()
    private let cache = NSCache<NSString, UIImage>()

    init() { cache.totalCostLimit = 24 * 1024 * 1024 }

    func clear() { cache.removeAllObjects() }

    func image(_ key: String) -> UIImage? { cache.object(forKey: key as NSString) }

    func store(_ key: String, _ image: UIImage) {
        cache.setObject(image, forKey: key as NSString,
                        cost: Int(image.size.width * image.size.height * 4))
    }
}

/// Decodifica no tamanho pedido (o `inSampleSize` do Android, pelo lado do iOS).
func decodeHomeThumbnail(path: String, maxPx: CGFloat) -> UIImage? {
    guard let full = UIImage(contentsOfFile: path) else { return nil }
    let width = full.size.width, height = full.size.height
    guard width > 0, height > 0 else { return full }
    let scale = maxPx / max(width, height)
    if scale >= 1 { return full }
    return full.preparingThumbnail(of: CGSize(width: width * scale, height: height * scale))
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

    @State private var image: UIImage?

    private var key: String {
        "\(url?.path ?? "")|\(stamp.timeIntervalSince1970)|\(Int(maxPx))"
    }

    var body: some View {
        ZStack {
            if let image {
                Image(uiImage: image).resizable().scaledToFill()
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
            guard let path = url?.path else { image = nil; return }
            if let hit = HomeThumbCache.shared.image(key) { image = hit; return }
            let loaded = await Task.detached(priority: .userInitiated) {
                decodeHomeThumbnail(path: path, maxPx: maxPx)
            }.value
            if let loaded { HomeThumbCache.shared.store(key, loaded) }
            image = loaded
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
// Os cartões (ProjectCards.kt)
// =============================================================================
/// Placeholder de miniatura: degradê #1B2530 → #151C24 (topLeft → bottomRight).
private let homePlaceholderGradient = LinearGradient(
    colors: [AureaColors.surfaceHigh, AureaColors.surface],
    startPoint: .topLeading, endPoint: .bottomTrailing)

/// O cartão "Continuar editando": o projeto mais recente em 16:9, miniatura de
/// verdade, scrim, nome e a pílula. Toque abre; toque longo ou ⋯ abre o menu.
struct HomeContinueCard: View {
    let entry: HomeProjectEntry
    let onOpen: () -> Void
    let onMenu: () -> Void

    var body: some View {
        ZStack(alignment: .bottomLeading) {
            HomeThumbnail(url: entry.thumbnailURL, stamp: entry.modified,
                          maxPx: HomeDims.heroDecode, ratio: homeProjectRatio(entry), hero: true)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            LinearGradient(colors: [.clear, HomeColors.imageScrim],
                           startPoint: .center, endPoint: .bottom)
            HStack(alignment: .bottom, spacing: 10) {
                VStack(alignment: .leading, spacing: 0) {
                    Text(AureaText.t("home_continue"))
                        .aureaFont(.heroKicker).foregroundStyle(AureaColors.accent)
                    Text(entry.title)
                        .aureaFont(.heroTitle).foregroundStyle(HomeColors.onImage)
                        .lineLimit(1).padding(.top, 3)
                    Text(homeProjectSpec(entry))
                        .aureaFont(.heroSpec).foregroundStyle(HomeColors.onImage70)
                        .lineLimit(1).padding(.top, 2)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                HStack(spacing: 6) {
                    CupertinoGlyph.text(CupertinoGlyph.PlayFill, size: HomeDims.iconXs, color: AureaColors.onAccent)
                    Text(AureaText.t("home_continue_action"))
                        .aureaFont(.heroPill).foregroundStyle(AureaColors.onAccent)
                }
                .padding(.horizontal, HomeDims.rMd)
                .padding(.vertical, 9)
                .background(AureaColors.accent, in: Capsule())
            }
            .padding(.leading, HomeDims.s4)
            .padding(.trailing, HomeDims.s4)
            .padding(.bottom, HomeDims.rMd)

            VStack {
                HStack {
                    Spacer()
                    Button(action: onMenu) {
                        CupertinoGlyph.text(CupertinoGlyph.Ellipsis, size: HomeDims.menuDotIcon, color: HomeColors.onImage70)
                            .frame(width: HomeDims.menuDot, height: HomeDims.menuDot)
                            .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel(AureaText.t("home_menu_content"))
                }
                Spacer()
            }
            .padding(.top, 2)
            .padding(.trailing, 2)
        }
        .aspectRatio(HomeDims.heroAspect, contentMode: .fit)
        .clipShape(RoundedRectangle(cornerRadius: HomeDims.rXl))
        .homeTapOrLong(onOpen, long: onMenu)
        .padding(.leading, HomeDims.gutter)
        .padding(.trailing, HomeDims.gutter)
        .padding(.top, HomeDims.s4)
    }
}

/// O cartão da grade: miniatura (raio 12), o nome, a ficha e as reticências.
/// Escolhendo vários, o toque marca em vez de abrir.
struct HomeProjectCard: View {
    let entry: HomeProjectEntry
    let selecting: Bool
    let marked: Bool
    let onOpen: () -> Void
    let onMenu: () -> Void
    let onMark: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            ZStack {
                HomeThumbnail(url: entry.thumbnailURL, stamp: entry.modified,
                              maxPx: HomeDims.cardDecode, ratio: homeProjectRatio(entry))
                if selecting {
                    AureaColors.background.opacity(marked ? 0.35 : 0.15)
                    VStack {
                        HStack {
                            Spacer()
                            CupertinoGlyph.text(marked ? CupertinoGlyph.CheckmarkCircleFill : CupertinoGlyph.Circle,
                                                size: HomeDims.iconLg,
                                                color: marked ? AureaColors.accent : HomeColors.onImage70)
                        }
                        Spacer()
                    }
                    .padding(6)
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .clipShape(RoundedRectangle(cornerRadius: HomeDims.rMd))
            .layoutPriority(1)

            HStack(spacing: 0) {
                VStack(alignment: .leading, spacing: 1) {
                    Text(entry.title)
                        .aureaFont(.cardTitle).foregroundStyle(AureaColors.text)
                        .lineLimit(1)
                    Text(homeProjectSpec(entry))
                        .aureaFont(.cardSpec).foregroundStyle(AureaColors.muted)
                        .lineLimit(1)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                Button(action: selecting ? onMark : onMenu) {
                    CupertinoGlyph.text(CupertinoGlyph.Ellipsis, size: HomeDims.menuDotIcon, color: AureaColors.muted)
                        .frame(width: HomeDims.menuDot, height: HomeDims.menuDot)
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel(AureaText.t("home_menu_content"))
            }
            .padding(.top, 6)
        }
        .aspectRatio(HomeDims.cardAspect, contentMode: .fit)
        .homeTapOrLong(selecting ? onMark : onOpen, long: selecting ? onMark : onMenu)
    }
}

/// `_SemProjetos`: film + a frase. Também serve para busca sem resultado.
struct HomeProjectsEmptyState: View {
    let message: String

    var body: some View {
        HStack(spacing: HomeDims.s3) {
            CupertinoGlyph.text(CupertinoGlyph.Film, size: 22, color: AureaColors.muted)
            Text(message)
                .aureaFont(.empty).foregroundStyle(AureaColors.muted)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
        .padding(.leading, HomeDims.gutter)
        .padding(.trailing, HomeDims.gutter)
        .padding(.top, HomeDims.s1)
    }
}

// =============================================================================
// A barra da lista, o campo de busca e a barra de lote
// =============================================================================
/// "{N} projetos" + buscar/ordenar/selecionar; buscando, o campo toma o lugar
/// do título; selecionando, "{n} escolhidos" + marcar todos + sair.
struct HomeProjectListBar: View {
    let count: Int
    let selecting: Bool
    let selectedCount: Int
    let searching: Bool
    @Binding var query: String
    let onOpenSearch: () -> Void
    let onClearSearch: () -> Void
    let onSort: () -> Void
    let onSelectAll: () -> Void
    let onExitSelection: () -> Void
    var inset: Bool = true

    private var countLabel: String {
        AureaText.t(count == 1 ? "home_project_count" : "home_project_count_plural", count)
    }

    var body: some View {
        HStack(spacing: 0) {
            if selecting {
                Text(AureaText.t("home_selected_chosen", selectedCount))
                    .aureaFont(.listCount).foregroundStyle(AureaColors.text)
                    .frame(maxWidth: .infinity, alignment: .leading)
                HomeBarButton(glyph: CupertinoGlyph.CheckmarkCircle,
                              description: AureaText.t("home_mark_all"), action: onSelectAll)
                HomeBarButton(glyph: CupertinoGlyph.Xmark,
                              description: AureaText.t("home_exit_selection"), action: onExitSelection)
            } else {
                if searching {
                    HomeSearchField(query: $query, onClear: onClearSearch)
                } else {
                    Text(countLabel)
                        .aureaFont(.listCount).foregroundStyle(AureaColors.text)
                        .frame(maxWidth: .infinity, alignment: .leading)
                    HomeBarButton(glyph: CupertinoGlyph.Search,
                                  description: AureaText.t("common_search"), action: onOpenSearch)
                }
                HomeBarButton(glyph: CupertinoGlyph.ArrowUpArrowDown,
                              description: AureaText.t("home_sort_title"), action: onSort)
                if !searching {
                    HomeBarButton(glyph: CupertinoGlyph.CheckmarkCircle,
                                  description: AureaText.t("home_select"), action: onSelectAll)
                }
            }
        }
        .padding(.leading, inset ? HomeDims.gutter : 0)
        .padding(.trailing, inset ? HomeDims.s3 : 0)
        .padding(.top, inset ? 18 : 0)
        .padding(.bottom, inset ? 6 : 0)
    }
}

/// `_BotaoDaBarra`: padding 8 + ícone 19 muted.
struct HomeBarButton: View {
    let glyph: Character
    let description: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            CupertinoGlyph.text(glyph, size: 19, color: AureaColors.muted)
                .padding(HomeDims.s2)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityLabel(description)
    }
}

/// O `CupertinoTextField` padrão no escuro (altura 34, raio 5, hairline), com
/// foco automático e o "x" que limpa e fecha a busca.
struct HomeSearchField: View {
    @Binding var query: String
    let onClear: () -> Void

    @FocusState private var focused: Bool

    var body: some View {
        HStack(spacing: 0) {
            ZStack(alignment: .leading) {
                if query.isEmpty {
                    Text(AureaText.t("home_search_hint"))
                        .aureaFont(.searchText).foregroundStyle(HomeColors.fieldPlaceholder)
                        .lineLimit(1)
                }
                TextField("", text: $query)
                    .aureaFont(.searchText).foregroundStyle(AureaColors.text)
                    .textInputAutocapitalization(.sentences)
                    .submitLabel(.search)
                    .focused($focused)
            }
            .padding(.horizontal, 7)
            .frame(maxWidth: .infinity, alignment: .leading)
            Button(action: onClear) {
                CupertinoGlyph.text(CupertinoGlyph.XmarkCircleFill, size: HomeDims.iconSm, color: AureaColors.text)
                    .padding(.horizontal, HomeDims.s2)
                    .frame(maxHeight: .infinity)
                    .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .accessibilityLabel(AureaText.t("home_clear_search"))
        }
        .frame(height: HomeDims.chipHeight)
        .background(HomeColors.field)
        .overlay(RoundedRectangle(cornerRadius: HomeDims.rXs).stroke(HomeColors.fieldBorder, lineWidth: HomeDims.hairline))
        .clipShape(RoundedRectangle(cornerRadius: HomeDims.rXs))
        .onAppear { focused = true }
    }
}

/// `_AcoesEmLote`: vidro, "{n} escolhidos", Duplicar e Excluir. Fica ACIMA da
/// barra de abas.
struct HomeBatchBar: View {
    let count: Int
    let onDuplicate: () -> Void
    let onDelete: () -> Void

    var body: some View {
        VStack(spacing: 0) {
            Rectangle().fill(AureaColors.border).frame(height: HomeDims.hairline)
            HStack(spacing: 0) {
                Text(AureaText.t("home_selected_chosen", count))
                    .aureaFont(.batchCount).foregroundStyle(AureaColors.muted)
                    .frame(maxWidth: .infinity, alignment: .leading)
                HomeBatchButton(glyph: CupertinoGlyph.PlusSquareOnSquare,
                                label: AureaText.t("common_duplicate"),
                                color: AureaColors.text, action: onDuplicate)
                HomeBatchButton(glyph: CupertinoGlyph.Trash,
                                label: AureaText.t("common_delete"),
                                color: AureaColors.danger, action: onDelete)
            }
            .padding(.horizontal, HomeDims.s3)
            .padding(.vertical, HomeDims.s2)
        }
        .background {
            HomeGlass(kind: .batch).allowsHitTesting(false)
        }
    }
}

private struct HomeBatchButton: View {
    let glyph: Character
    let label: String
    let color: Color
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(spacing: 6) {
                CupertinoGlyph.text(glyph, size: 18)
                Text(label).aureaFont(.batchAction)
            }
            .foregroundStyle(color)
            .padding(.horizontal, HomeDims.rMd)
            .padding(.vertical, HomeDims.s2)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

/// Ação secundária da Início: cartão com ícone e rótulo, largura dividida.
struct HomeQuickAction: View {
    let glyph: Character
    let label: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(spacing: HomeDims.s2) {
                CupertinoGlyph.text(glyph, size: HomeDims.iconMd, color: AureaColors.accent)
                Text(label).aureaFont(.featureTitle).foregroundStyle(AureaColors.text)
            }
            .frame(maxWidth: .infinity)
            .frame(height: HomeDims.quickAction)
            .background(AureaColors.surface, in: RoundedRectangle(cornerRadius: HomeDims.rMd))
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
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
// Os menus e diálogos de um projeto (ProjectMenu.kt)
// =============================================================================
enum HomeProjectDialog: Equatable {
    case menu(HomeProjectEntry)
    case confirmDelete(HomeProjectEntry)
    case rename(HomeProjectEntry)
    case deleteAll
    case batchDelete(Set<String>)
    case sort
}

/// Desenha o diálogo aberto e age. A lista inteira entra para o "apagar todos"
/// e para a poda da seleção.
struct HomeProjectDialogs: View {
    @Binding var state: HomeProjectDialog?
    @ObservedObject var library: HomeLibrary
    @ObservedObject var defaults: HomeDefaults
    @Binding var selection: Set<String>
    let onOpen: (HomeProjectEntry) -> Void

    var body: some View {
        switch state {
        case .none:
            EmptyView()
        case .menu(let entry):
            HomeActionSheet(
                title: entry.title,
                message: homeProjectSpec(entry),
                actions: [
                    HomeSheetAction(AureaText.t("common_open")) { onOpen(entry) },
                    HomeSheetAction(AureaText.t("common_duplicate")) {
                        duplicateHomeProject(entry)
                        library.refresh()
                    },
                    HomeSheetAction(AureaText.t("common_rename")) { state = .rename(entry) },
                    HomeSheetAction(AureaText.t("project_delete"), destructive: true) { state = .confirmDelete(entry) },
                    HomeSheetAction(AureaText.t("project_delete_all"), destructive: true) {
                        if !library.all.isEmpty { state = .deleteAll }
                    },
                ],
                onDismiss: close)
        case .confirmDelete(let entry):
            // "Apagar todos" fica só no menu: ao lado da exclusão unitária o
            // risco de apagar tudo por engano não se paga.
            HomeActionSheet(
                title: entry.title,
                message: nil,
                actions: [HomeSheetAction(AureaText.t("project_delete"), destructive: true) {
                    deleteHomeProjects([entry.id])
                    library.refresh()
                }],
                onDismiss: close)
        case .rename(let entry):
            HomeRenamePrompt(initial: entry.title,
                             onSave: { title in
                                 if title != entry.title { renameHomeProjectTitle(path: entry.id, title: title) }
                             },
                             onDismiss: close,
                             onFinished: { library.refresh() })
        case .deleteAll:
            HomeAlert(title: AureaText.t("project_delete_all_title"),
                      message: AureaText.t("project_delete_all_message", library.all.count),
                      confirmLabel: AureaText.t("project_delete_all_confirm"),
                      destructive: true,
                      onConfirm: {
                          deleteHomeProjects(library.all.map(\.id))
                          selection = []
                          library.refresh()
                      },
                      onDismiss: close)
        case .batchDelete(let paths):
            HomeActionSheet(
                title: AureaText.t("project_delete_many_title", paths.count),
                message: AureaText.t("common_irreversible"),
                actions: [HomeSheetAction(AureaText.t("common_delete"), destructive: true) {
                    deleteHomeProjects(Array(paths))
                    selection = []
                    library.refresh()
                }],
                onDismiss: close)
        case .sort:
            HomeActionSheet(
                title: AureaText.t("home_sort_title"),
                message: nil,
                actions: HomeSortOrder.allCases.map { order in
                    HomeSheetAction(order.label) {
                        defaults.sortIndex = order.rawValue
                    }
                },
                onDismiss: close)
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
    let action: () -> Void

    init(_ label: String, destructive: Bool = false, action: @escaping () -> Void) {
        self.label = label
        self.destructive = destructive
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
