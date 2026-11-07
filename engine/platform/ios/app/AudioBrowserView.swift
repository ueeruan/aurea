// =============================================================================
//  Aurea / platform / ios / app / AudioBrowserView.swift
//
//  Omega 0.3.2, área 08: a faixa rápida de áudio do painel de adicionar (P0-4)
//  e o navegador de áudio em tela cheia (P1-3), com pré-escuta (um tocador por
//  vez) e recentes (P1-4). A biblioteca é a do APP (D2): os áudios já usados
//  em qualquer projeto (cópias em Documents/Media), as gravações de voz e os
//  sons importados; nada da biblioteca de Música do aparelho. "Arquivos do
//  aparelho…" abre o seletor de arquivos de hoje. Par de editor/AudioBrowser.kt.
// =============================================================================
import SwiftUI
import UIKit
import AVFoundation
import UniformTypeIdentifiers

// MARK: - A biblioteca de sons da pessoa (SoundLibrary)

/// Pastas e arquivos que a pessoa importa (efeitos, músicas) ficam COPIADOS em
/// Documents/Sons, para usar em qualquer projeto mesmo que o original saia do aparelho.
/// Uma pasta importada vira uma seção; cada subpasta dela, outra ("SFX / Bells"). O índice
/// guarda caminho relativo, título e duração. Par do Android: editor/SoundLibrary.kt.
enum SoundLibrary {
    struct Folder: Identifiable { let name: String; let sounds: [OmegaAudioEntry]; var id: String { name } }
    private struct Item: Codable { var c: String; var p: String; var t: String; var d: Double }
    private static let audio: Set<String> = ["mp3", "wav", "m4a", "aac", "caf", "aif", "aiff", "flac", "ogg", "opus", "amr"]
    private static let maxFiles = 3000
    static var root: URL { AureaPaths.documents.appendingPathComponent("Sons", isDirectory: true) }
    private static var indexURL: URL { root.appendingPathComponent("index.json") }

    private static func read() -> [Item] {
        guard let data = try? Data(contentsOf: indexURL) else { return [] }
        return (try? JSONDecoder().decode([Item].self, from: data)) ?? []
    }
    private static func write(_ items: [Item]) {
        try? FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        if let data = try? JSONEncoder().encode(items) { try? data.write(to: indexURL, options: .atomic) }
    }
    private static func safe(_ name: String) -> String {
        let bad = CharacterSet(charactersIn: "\\/:*?\"<>|").union(.controlCharacters)
        let cleaned = name.components(separatedBy: bad).joined(separator: "_").trimmingCharacters(in: .whitespaces)
        return String((cleaned.isEmpty ? "_" : cleaned).prefix(80))
    }

    /// As seções em ordem alfabética; arquivo apagado sai da lista. Fora da thread principal.
    static func load() -> [Folder] {
        let items = read()
        let alive = items.filter { FileManager.default.fileExists(atPath: root.appendingPathComponent($0.p).path) }
        if alive.count != items.count { write(alive) }
        return Dictionary(grouping: alive, by: \.c).sorted { $0.key.localizedCaseInsensitiveCompare($1.key) == .orderedAscending }.map { name, list in
            Folder(name: name, sounds: list.sorted { $0.t.lowercased() < $1.t.lowercased() }.map {
                OmegaAudioEntry(url: root.appendingPathComponent($0.p), title: $0.t, album: name, artist: "", genre: "",
                                duration: $0.d, lastUsed: .distantPast)
            })
        }
    }

    /// Uma pasta inteira (com as subpastas). Devolve quantos sons entraram.
    static func importFolder(_ folder: URL, progress: @escaping (Int, Int) -> Void) -> Int {
        let scoped = folder.startAccessingSecurityScopedResource()
        defer { if scoped { folder.stopAccessingSecurityScopedResource() } }
        let base = folder.lastPathComponent
        var found: [(URL, String)] = []
        let walker = FileManager.default.enumerator(at: folder, includingPropertiesForKeys: [.isDirectoryKey], options: [.skipsHiddenFiles])
        while let file = walker?.nextObject() as? URL, found.count < maxFiles {
            guard audio.contains(file.pathExtension.lowercased()) else { continue }
            let rel = file.deletingLastPathComponent().path.replacingOccurrences(of: folder.path, with: "")
                .split(separator: "/").map(String.init)
            found.append((file, ([base] + rel).joined(separator: " / ")))
        }
        return copy(found, progress: progress)
    }

    /// Arquivos soltos: entram na seção `category`.
    static func importFiles(_ urls: [URL], category: String, progress: @escaping (Int, Int) -> Void) -> Int {
        copy(urls.filter { audio.contains($0.pathExtension.lowercased()) }.map { ($0, category) }, progress: progress)
    }

    private static func copy(_ files: [(URL, String)], progress: @escaping (Int, Int) -> Void) -> Int {
        var items = read()
        var known = Set(items.map(\.p))
        var added = 0
        for (index, pair) in files.enumerated() {
            progress(index, files.count)
            let (source, category) = pair
            let rel = (category.components(separatedBy: " / ").map(safe) + [safe(source.lastPathComponent)]).joined(separator: "/")
            guard !known.contains(rel) else { continue }   // a mesma pasta importada de novo
            let target = root.appendingPathComponent(rel)
            try? FileManager.default.createDirectory(at: target.deletingLastPathComponent(), withIntermediateDirectories: true)
            let scoped = source.startAccessingSecurityScopedResource()
            defer { if scoped { source.stopAccessingSecurityScopedResource() } }
            guard (try? FileManager.default.copyItem(at: source, to: target)) != nil else { continue }
            // Só entra o que o aparelho consegue tocar (a mesma pré-escuta da biblioteca).
            guard let player = try? AVAudioPlayer(contentsOf: target) else { try? FileManager.default.removeItem(at: target); continue }
            items.append(Item(c: category, p: rel, t: source.deletingPathExtension().lastPathComponent, d: player.duration))
            known.insert(rel)
            added += 1
        }
        progress(files.count, files.count)
        write(items)
        return added
    }

    /// O som está na biblioteca (e não num projeto)?
    static func owns(_ url: URL) -> Bool { url.standardizedFileURL.path.hasPrefix(root.standardizedFileURL.path + "/") }

    /// Uma cópia do som para os projetos (em Media/, como as outras importações): tirar a
    /// seção da biblioteca depois não deixa o projeto sem áudio. Mesmo tamanho e nome = a mesma cópia.
    static func projectCopy(_ url: URL) -> URL? {
        let size = (try? url.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0
        try? FileManager.default.createDirectory(at: AureaPaths.media, withIntermediateDirectories: true)
        let target = AureaPaths.media.appendingPathComponent("som-\(size)-\(safe(url.lastPathComponent))")
        if (try? target.resourceValues(forKeys: [.fileSizeKey]).fileSize) == size { return target }
        try? FileManager.default.removeItem(at: target)
        return (try? FileManager.default.copyItem(at: url, to: target)) != nil ? target : nil
    }

    /// Tira a seção da biblioteca e apaga as cópias dela.
    static func remove(_ category: String) {
        let items = read()
        for item in items where item.c == category { try? FileManager.default.removeItem(at: root.appendingPathComponent(item.p)) }
        write(items.filter { $0.c != category })
    }
}

// MARK: - A biblioteca de áudio do app

struct OmegaAudioEntry: Identifiable, Equatable {
    let url: URL
    let title: String
    let album: String
    let artist: String
    let genre: String
    let duration: Double
    let lastUsed: Date
    var id: String { url.path }
}

enum OmegaAudioLibrary {
    private static let extensions: Set<String> = ["m4a", "mp3", "wav", "aac", "caf", "aif", "aiff", "flac", "ogg", "opus", "amr", "3gp", "mid", "midi"]
    private static let usedKey = "audio.used"

    /// Caminho relativo a Media/ (a chave dos recentes sobrevive a mudar o sandbox).
    private static func key(_ url: URL) -> String {
        let base = AureaPaths.media.standardizedFileURL.path
        let path = url.standardizedFileURL.path
        return path.hasPrefix(base) ? String(path.dropFirst(base.count)) : path
    }

    private static var used: [String: Double] {
        (OmegaAddPrefs.defaults.dictionary(forKey: "omega.add." + usedKey) as? [String: Double]) ?? [:]
    }

    /// Usado agora: vai para o topo dos recentes (guarda 5; a faixa mostra 3).
    static func markUsed(_ url: URL) {
        var map = used
        map[key(url)] = Date().timeIntervalSince1970
        let kept = map.sorted { $0.value > $1.value }.prefix(5)
        OmegaAddPrefs.set(Dictionary(uniqueKeysWithValues: kept.map { ($0.key, $0.value) }), usedKey)
    }

    /// A biblioteca inteira, os usados por último primeiro. Lê o disco: fora da thread principal.
    static func load() -> [OmegaAudioEntry] {
        let root = AureaPaths.media
        guard let walker = FileManager.default.enumerator(at: root, includingPropertiesForKeys: [.isRegularFileKey, .creationDateKey],
                                                          options: [.skipsHiddenFiles]) else { return [] }
        let usedMap = used
        var out: [OmegaAudioEntry] = []
        for case let url as URL in walker {
            // As sequências de imagens extraídas não têm som.
            if url.lastPathComponent == "sequences" { walker.skipDescendants(); continue }
            guard extensions.contains(url.pathExtension.lowercased()),
                  (try? url.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true else { continue }
            let asset = AVURLAsset(url: url)
            let duration = CMTimeGetSeconds(asset.duration)
            guard duration.isFinite, duration > 0 else { continue }
            let metadata = asset.commonMetadata
            func value(_ id: AVMetadataIdentifier) -> String {
                AVMetadataItem.metadataItems(from: metadata, filteredByIdentifier: id).first?.stringValue ?? ""
            }
            let created = (try? url.resourceValues(forKeys: [.creationDateKey]).creationDate) ?? .distantPast
            let mark = usedMap[key(url)].map { Date(timeIntervalSince1970: $0) } ?? .distantPast
            let title = value(.commonIdentifierTitle)
            out.append(OmegaAudioEntry(url: url, title: title.isEmpty ? url.deletingPathExtension().lastPathComponent : title,
                                       album: value(.commonIdentifierAlbumName), artist: value(.commonIdentifierArtist),
                                       genre: value(.commonIdentifierType), duration: duration,
                                       lastUsed: max(mark, created)))
        }
        return out.sorted { $0.lastUsed > $1.lastUsed }
    }

    /// Os N usados (ou entrados) por último.
    static func recents(_ entries: [OmegaAudioEntry], count: Int) -> [OmegaAudioEntry] {
        Array(entries.filter { $0.lastUsed > .distantPast }.prefix(5).prefix(count))
    }

    /// A capa embutida (ou nil). Fora da thread principal.
    static func cover(_ url: URL) -> UIImage? {
        let items = AVMetadataItem.metadataItems(from: AVURLAsset(url: url).commonMetadata, filteredByIdentifier: .commonIdentifierArtwork)
        guard let data = items.first?.dataValue else { return nil }
        return UIImage(data: data)
    }
}

// MARK: - Pré-escuta (um tocador por vez; P1-7)

final class OmegaAudioPreview: NSObject, ObservableObject, AVAudioPlayerDelegate {
    static let shared = OmegaAudioPreview()
    @Published private(set) var playing: URL?
    private var player: AVAudioPlayer?

    /// Liga e desliga. Formato que o aparelho não toca = `onUnsupported`.
    func toggle(_ url: URL, onUnsupported: () -> Void) {
        if playing == url { stop(); return }
        stop()
        do {
            let next = try AVAudioPlayer(contentsOf: url)
            next.delegate = self
            guard next.prepareToPlay(), next.play() else { onUnsupported(); return }
            player = next
            playing = url
        } catch { onUnsupported() }
    }

    func stop() {
        player?.stop()
        player = nil
        if playing != nil { playing = nil }
    }

    var progress: Double {
        guard let player, player.duration > 0 else { return 0 }
        return min(1, max(0, player.currentTime / player.duration))
    }

    func audioPlayerDidFinishPlaying(_ player: AVAudioPlayer, successfully flag: Bool) {
        DispatchQueue.main.async { [weak self] in if self?.player === player { self?.stop() } }
    }
}

/// O aviso de formato não suportado (diálogo claro, por cima de tudo).
func omegaAudioUnsupported() {
    AureaPopupPresenter.shared.presentDialog { close in
        AureaDialog(title: nil, message: AureaText.t("om_audio_unsupported"),
                    actions: [DialogAction(AureaText.t("common_ok"), role: .positive)], onDismiss: close, tag: "audio.unsupported")
    }
}

// MARK: - A faixa rápida de áudio (P0-4)

/// Grade HORIZONTAL de 2 fileiras: [Ver tudo], as fichas do AUREA (arquivos,
/// gravar voz, som de um vídeo, marca no cabeçote), até 3 recentes (relógio) e
/// o resto da biblioteca. Tocar no cartão liga a pré-escuta; o "+" adiciona e fecha.
struct AudioStripView: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    @ObservedObject private var preview = OmegaAudioPreview.shared
    @State private var entries: [OmegaAudioEntry] = []
    let close: () -> Void

    var body: some View {
        GeometryReader { geometry in
            let cell = geometry.size.height / 2
            let recents = OmegaAudioLibrary.recents(entries, count: 3)
            let rest = entries.filter { entry in !recents.contains(entry) }
            ScrollView(.horizontal, showsIndicators: false) {
                LazyHGrid(rows: [GridItem(.fixed(cell), spacing: 0), GridItem(.fixed(cell), spacing: 0)], spacing: 0) {
                    StripActionCell(cell: cell, label: AureaText.t("om_add_view_all"), glyph: CupertinoGlyph.MusicNote2, tag: "add.audio.viewAll") {
                        preview.stop(); model.omegaAudioBrowser = true
                    }
                    StripActionCell(cell: cell, label: AureaText.t("editor_musica_ou_som"), glyph: CupertinoGlyph.Folder, tag: "add.audio.files") {
                        preview.stop(); omegaRequestPicker(model, shell, .audioFile)
                    }
                    StripActionCell(cell: cell, label: AureaText.t("voice_title"), glyph: CupertinoGlyph.MicFill, tag: "add.voice", tint: AureaColors.danger) {
                        // Narração: o painel de gravação abre por cima da timeline.
                        preview.stop(); close(); model.pause(); shell.voiceRecorder = true
                    }
                    StripActionCell(cell: cell, label: AureaText.t("sh_add_video_sound"), glyph: CupertinoGlyph.Film, tag: "add.audio.videoSound") {
                        preview.stop(); omegaRequestPicker(model, shell, .audioFromVideo)
                    }
                    StripActionCell(cell: cell, label: AureaText.t("sh_add_marker_at_playhead"), glyph: CupertinoGlyph.Bookmark, tag: "add.audio.marker") {
                        close(); model.toggleMarkerAt(model.status.playhead)
                    }
                    ForEach(Array(recents.enumerated()), id: \.element.id) { index, entry in
                        AudioCard(entry: entry, recent: true, cell: cell, tag: "add.audio.item.\(index)", onAdd: { add(entry) })
                    }
                    ForEach(Array(rest.enumerated()), id: \.element.id) { index, entry in
                        AudioCard(entry: entry, recent: false, cell: cell, tag: "add.audio.item.\(index + recents.count)", onAdd: { add(entry) })
                    }
                }
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("add.audio.strip")
        .onAppear {
            DispatchQueue.global(qos: .userInitiated).async {
                let found = OmegaAudioLibrary.load()
                DispatchQueue.main.async { entries = found }
            }
        }
        // Fechar o painel ou trocar de botão para a pré-escuta.
        .onDisappear { preview.stop() }
    }

    private func add(_ entry: OmegaAudioEntry) {
        preview.stop()
        OmegaAudioLibrary.markUsed(entry.url)
        model.importAudioSource(entry.url, name: entry.title)
        close()
    }
}

/// Cartão de 75: capa ou nota, nome e duração no alto, barra "+" de 26 na base.
private struct AudioCard: View {
    let entry: OmegaAudioEntry
    let recent: Bool
    let cell: CGFloat
    let tag: String
    let onAdd: () -> Void
    @ObservedObject private var preview = OmegaAudioPreview.shared
    @State private var cover: UIImage?

    var body: some View {
        let playing = preview.playing == entry.url
        VStack(spacing: 0) {
            Button { preview.toggle(entry.url) { omegaAudioUnsupported() } } label: {
                ZStack(alignment: .topLeading) {
                    AureaColors.addTile
                    if let cover { Image(uiImage: cover).resizable().scaledToFill() }
                    else { CupertinoGlyph.text(CupertinoGlyph.MusicNote, size: 22, color: AureaColors.muted).frame(maxWidth: .infinity, maxHeight: .infinity) }
                    AureaColors.addScrimSoft
                    VStack(alignment: .leading, spacing: 1) {
                        Text(entry.title).font(.aurea(size: 11, weight: .semibold)).foregroundStyle(AureaColors.onImage).lineLimit(1)
                        Text(omegaDuration(entry.duration)).font(.aurea(size: 9)).foregroundStyle(AureaColors.onImage70)
                    }.padding(.leading, 5).padding(.top, 4).padding(.trailing, 18)
                    HStack(spacing: 0) {
                        Spacer(minLength: 0)
                        if playing { CupertinoGlyph.text(CupertinoGlyph.PlayFill, size: 15, color: AureaColors.accent) }
                        if recent { CupertinoGlyph.text(CupertinoGlyph.Clock, size: 15, color: AureaColors.onImage) }
                    }.padding(3)
                }
                .frame(width: 75, height: 49).clipped().contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .accessibilityLabel(AureaText.t(playing ? "om_audio_stop_named" : "om_audio_preview_named", entry.title))
            .accessibilityIdentifier(tag)
            Button(action: onAdd) {
                CupertinoGlyph.text(CupertinoGlyph.Plus, size: 15, color: AureaColors.onImage)
                    .frame(width: 75, height: 26).background(AureaColors.addScrimBar).contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .accessibilityLabel(AureaText.t("om_audio_add_named", entry.title))
            .accessibilityIdentifier(tag + ".add")
        }
        .background(AureaColors.addTile)
        .clipShape(RoundedRectangle(cornerRadius: 2))
        .overlay { if playing { RoundedRectangle(cornerRadius: 2).stroke(AureaColors.accent, lineWidth: 1.5) } }
        .padding(.horizontal, 2.5)
        .frame(width: cell, height: cell)
        .onAppear {
            let url = entry.url
            DispatchQueue.global(qos: .utility).async {
                let image = OmegaAudioLibrary.cover(url)
                DispatchQueue.main.async { cover = image }
            }
        }
    }
}

// MARK: - O navegador de áudio em tela cheia (P1-3)

private enum OmegaAudioCategory: String, CaseIterable {
    case songs, albums, artists, genres
    var key: String {
        switch self {
        case .songs: return "om_audio_songs"
        case .albums: return "om_audio_albums"
        case .artists: return "om_audio_artists"
        case .genres: return "om_audio_genres"
        }
    }
    var glyph: Character {
        switch self {
        case .songs: return CupertinoGlyph.MusicNote
        case .albums: return CupertinoGlyph.RectangleStack
        case .artists: return CupertinoGlyph.PersonCropCircle
        case .genres: return CupertinoGlyph.MusicNote2
        }
    }
}

/// Categorias (Músicas · Álbuns · Artistas · Gêneros) em linhas de 70 e, no
/// alto, "Arquivos do aparelho…". Álbuns, artistas e gêneros abrem a lista dos
/// grupos e depois as faixas; cada faixa tem ▶ com anel de progresso e "+".
struct AudioBrowserView: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var preview = OmegaAudioPreview.shared
    @State private var entries: [OmegaAudioEntry]?
    @State private var category: OmegaAudioCategory?
    @State private var group: String?
    @State private var choosingFile = false
    // A biblioteca de sons da pessoa (SoundLibrary): pastas e arquivos importados.
    @State private var sounds: [SoundLibrary.Folder] = []
    @State private var soundFolder: String?
    @State private var importing: (Int, Int)?
    @State private var removing: String?
    @State private var choosingFolder = false
    @State private var choosingSounds = false

    var body: some View {
        VStack(spacing: 0) {
            if let soundFolder {
                BrowserTopBar(title: soundFolder, glyph: CupertinoGlyph.ChevronLeft,
                              description: AureaText.t("om_add_back"), tag: "audio.back", action: back)
            } else if let category {
                BrowserTopBar(title: group ?? AureaText.t(category.key), glyph: CupertinoGlyph.ChevronLeft,
                              description: AureaText.t("om_add_back"), tag: "audio.back", action: back)
            } else {
                BrowserTopBar(title: AureaText.t("om_audio_title"), glyph: CupertinoGlyph.Xmark,
                              description: AureaText.t("editor_fechar"), tag: "audio.close", action: dismiss)
            }
            if let soundFolder {
                trackList(sounds.first { $0.name == soundFolder }?.sounds ?? [])
                Button(AureaText.t("sl_remove_folder")) { removing = soundFolder }
                    .font(.aurea(size: 14)).foregroundStyle(AureaColors.muted).padding(18)
                    .accessibilityIdentifier("sounds.remove")
            } else if let all = entries {
                if let category {
                    if category == .songs || group != nil {
                        trackList(group == nil ? all : all.filter { groupKey($0, category) == group })
                    } else {
                        groupList(all, category)
                    }
                } else {
                    categories(all)
                }
            } else {
                ProgressView().tint(AureaColors.accent).frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .background(AureaColors.addPanel.ignoresSafeArea())
        .preferredColorScheme(.dark)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("audio.browser")
        .onAppear {
            DispatchQueue.global(qos: .userInitiated).async {
                let found = OmegaAudioLibrary.load()
                DispatchQueue.main.async { entries = found }
            }
        }
        .onDisappear { preview.stop() }
        .task {
            let found = await Task.detached { SoundLibrary.load() }.value
            sounds = found
        }
        .overlay {
            if let importing {
                VStack(spacing: 14) {
                    ProgressView().tint(AureaColors.accent)
                    Text(importing.1 > 0 ? AureaText.t("sl_importing", importing.0, importing.1) : AureaText.t("sl_importing_start"))
                        .font(.aurea(size: 14)).foregroundStyle(AureaColors.text)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity).background(AureaColors.addPanel.opacity(0.85))
                .accessibilityIdentifier("sounds.importing")
            }
        }
        .alert(AureaText.t("sl_remove_folder"), isPresented: Binding(get: { removing != nil }, set: { if !$0 { removing = nil } })) {
            Button(AureaText.t("panel_remover"), role: .destructive) {
                guard let name = removing else { return }
                removing = nil
                preview.stop()
                Task {
                    let found = await Task.detached { SoundLibrary.remove(name); return SoundLibrary.load() }.value
                    sounds = found; soundFolder = nil
                }
            }
            Button(AureaText.t("common_cancel"), role: .cancel) { removing = nil }
        } message: { Text(AureaText.t("sl_remove_message", removing ?? "")) }
        .sheet(isPresented: $choosingFolder) {
            SoundFolderPicker(folders: true) { urls in
                choosingFolder = false
                if let folder = urls.first { runImport { SoundLibrary.importFolder(folder, progress: $0) } }
            }
        }
        .sheet(isPresented: $choosingSounds) {
            SoundFolderPicker(folders: false) { urls in
                choosingSounds = false
                let label = AureaText.t("sl_imported_section")
                if !urls.isEmpty { runImport { SoundLibrary.importFiles(urls, category: label, progress: $0) } }
            }
        }
        .sheet(isPresented: $choosingFile) {
            // "Arquivos do aparelho…": o seletor de arquivos de hoje; o áudio entra
            // na biblioteca (cópia em Media/) e na camada.
            OmegaAudioFilePicker { url in
                choosingFile = false
                guard let url else { return }
                model.importMedia(url: url, kind: .audio)
                model.omegaAudioBrowser = false
                model.showAddLayer = false
            }
        }
    }

    private func dismiss() { preview.stop(); model.omegaAudioBrowser = false }
    private func back() {
        preview.stop()
        if soundFolder != nil { soundFolder = nil } else if group != nil { group = nil } else { category = nil }
    }

    /// Copia fora da thread principal, com o andamento na tela.
    private func runImport(_ work: @escaping (@escaping (Int, Int) -> Void) -> Int) {
        preview.stop()
        importing = (0, 0)
        Task {
            let added = await Task.detached {
                work { done, total in Task { @MainActor in importing = (done, total) } }
            }.value
            let found = await Task.detached { SoundLibrary.load() }.value
            sounds = found
            importing = nil
            model.toast = added > 0 ? AureaText.t("sl_imported", added) : AureaText.t("sl_none")
        }
    }

    private func groupKey(_ entry: OmegaAudioEntry, _ category: OmegaAudioCategory) -> String {
        let unknown = AureaText.t("om_audio_unknown")
        switch category {
        case .albums: return entry.album.isEmpty ? unknown : entry.album
        case .artists: return entry.artist.isEmpty ? unknown : entry.artist
        case .genres: return entry.genre.isEmpty ? unknown : entry.genre
        case .songs: return ""
        }
    }

    private func categories(_ all: [OmegaAudioEntry]) -> some View {
        ScrollView {
            LazyVStack(spacing: 0) {
                row(AureaText.t("om_audio_device_files"), count: nil, glyph: CupertinoGlyph.Folder, tag: "audio.files", chevron: false) {
                    preview.stop(); choosingFile = true
                }
                // Minha biblioteca de sons: importar e as seções que a pessoa já trouxe.
                sectionTitle(AureaText.t("sl_title"))
                row(AureaText.t("sl_import_folder"), count: nil, glyph: CupertinoGlyph.Folder, tag: "sounds.importFolder", chevron: false) {
                    preview.stop(); choosingFolder = true
                }
                row(AureaText.t("sl_import_files"), count: nil, glyph: CupertinoGlyph.Plus, tag: "sounds.importFiles", chevron: false) {
                    preview.stop(); choosingSounds = true
                }
                if sounds.isEmpty {
                    Text(AureaText.t("sl_hint")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                        .multilineTextAlignment(.center).padding(.horizontal, 24).padding(.vertical, 12)
                }
                ForEach(Array(sounds.enumerated()), id: \.element.id) { index, folder in
                    row(folder.name, count: AureaText.plural("om_audio_count", folder.sounds.count, folder.sounds.count),
                        glyph: CupertinoGlyph.Waveform, tag: "sounds.folder.\(index)") { soundFolder = folder.name }
                }
                sectionTitle(AureaText.t("om_audio_title"))
                ForEach(OmegaAudioCategory.allCases, id: \.self) { item in
                    let count = item == .songs ? all.count : Set(all.map { groupKey($0, item) }).count
                    row(AureaText.t(item.key), count: AureaText.plural("om_audio_count", count, count), glyph: item.glyph,
                        tag: "audio.category.\(item.rawValue)") { category = item }
                }
                if all.isEmpty {
                    Text(AureaText.t("om_audio_empty")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                        .multilineTextAlignment(.center).padding(24)
                }
            }
        }
    }

    private func groupList(_ all: [OmegaAudioEntry], _ category: OmegaAudioCategory) -> some View {
        let groups = Dictionary(grouping: all) { groupKey($0, category) }.sorted { $0.key.lowercased() < $1.key.lowercased() }
        return ScrollView {
            LazyVStack(spacing: 0) {
                ForEach(Array(groups.enumerated()), id: \.element.key) { index, pair in
                    row(pair.key, count: AureaText.plural("om_audio_count", pair.value.count, pair.value.count), glyph: category.glyph,
                        tag: "audio.group.\(index)") { group = pair.key }
                }
            }
        }
    }

    @ViewBuilder private func trackList(_ tracks: [OmegaAudioEntry]) -> some View {
        if tracks.isEmpty {
            Text(AureaText.t("om_audio_empty")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        } else {
            ScrollView {
                LazyVStack(spacing: 0) {
                    ForEach(Array(tracks.enumerated()), id: \.element.id) { index, entry in
                        TrackRow(index: index, entry: entry) {
                            preview.stop()
                            // Da biblioteca de sons: o projeto usa uma cópia própria (remover a seção não o afeta).
                            let source = SoundLibrary.owns(entry.url) ? (SoundLibrary.projectCopy(entry.url) ?? entry.url) : entry.url
                            OmegaAudioLibrary.markUsed(source)
                            model.importAudioSource(source, name: entry.title)
                            model.omegaAudioBrowser = false
                            model.showAddLayer = false
                        }
                    }
                }
            }
        }
    }

    private func sectionTitle(_ text: String) -> some View {
        Text(text).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted)
            .frame(maxWidth: .infinity, alignment: .leading).padding(.leading, 14).padding(.top, 18).padding(.bottom, 6)
            .accessibilityAddTraits(.isHeader)
    }

    /// Linha de 70: ícone quadrado, título 15 + contagem 12, seta ›.
    private func row(_ title: String, count: String?, glyph: Character, tag: String, chevron: Bool = true, action: @escaping () -> Void) -> some View {
        VStack(spacing: 0) {
            Button(action: action) {
                HStack(spacing: 0) {
                    CupertinoGlyph.text(glyph, size: 24, color: AureaColors.text)
                        .frame(width: 56, height: 56).background(AureaColors.addPanelDeep, in: RoundedRectangle(cornerRadius: 2)).padding(7)
                    VStack(alignment: .leading, spacing: 2) {
                        Text(title).font(.aurea(size: 15)).foregroundStyle(AureaColors.text).lineLimit(1)
                        if let count { Text(count).font(.aurea(size: 12)).foregroundStyle(AureaColors.text.opacity(0.3)) }
                    }.padding(.leading, 5)
                    Spacer(minLength: 8)
                    if chevron { CupertinoGlyph.text(CupertinoGlyph.ChevronRight, size: 14, color: AureaColors.muted).mirrorsInRtl().padding(.trailing, 12) }
                }
                .frame(height: 70).contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .accessibilityLabel(count.map { "\(title), \($0)" } ?? title)
            .accessibilityIdentifier(tag)
            Rectangle().fill(AureaColors.addPanelDeep).frame(height: 0.5)
        }
    }
}

/// Faixa de 70: capa com ▶ e anel de progresso, título, álbum e duração, "+".
private struct TrackRow: View {
    let index: Int
    let entry: OmegaAudioEntry
    let onAdd: () -> Void
    @ObservedObject private var preview = OmegaAudioPreview.shared
    @State private var cover: UIImage?

    var body: some View {
        let playing = preview.playing == entry.url
        VStack(spacing: 0) {
            HStack(spacing: 0) {
                Button { preview.toggle(entry.url) { omegaAudioUnsupported() } } label: {
                    ZStack {
                        AureaColors.addPanelDeep
                        if let cover { Image(uiImage: cover).resizable().scaledToFill() }
                        AureaColors.addScrimBar
                        CupertinoGlyph.text(playing ? CupertinoGlyph.PauseFill : CupertinoGlyph.PlayFill, size: 18, color: AureaColors.onImage)
                        if playing {
                            // O anel de progresso, atualizado a cada ~15 ms.
                            SwiftUI.TimelineView(.periodic(from: .now, by: 0.015)) { _ in
                                Circle().trim(from: 0, to: preview.progress)
                                    .stroke(AureaColors.accent, style: StrokeStyle(lineWidth: 2, lineCap: .round))
                                    .rotationEffect(.degrees(-90)).frame(width: 40, height: 40)
                            }
                        }
                    }
                    .frame(width: 56, height: 56).clipShape(RoundedRectangle(cornerRadius: 2)).contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .padding(7)
                .accessibilityLabel(AureaText.t(playing ? "om_audio_stop_named" : "om_audio_preview_named", entry.title))
                .accessibilityIdentifier("audio.row.\(index).play")
                VStack(alignment: .leading, spacing: 2) {
                    Text(entry.title).font(.aurea(size: 15)).foregroundStyle(AureaColors.text).lineLimit(1).minimumScaleFactor(0.55)
                    Text([entry.album, omegaDuration(entry.duration)].filter { !$0.isEmpty }.joined(separator: " · "))
                        .font(.aurea(size: 12)).foregroundStyle(AureaColors.text.opacity(0.3)).lineLimit(1)
                }.padding(.leading, 5)
                Spacer(minLength: 8)
                Button(action: onAdd) {
                    CupertinoGlyph.text(CupertinoGlyph.Plus, size: 20, color: AureaColors.text).frame(width: 48, height: 48).contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel(AureaText.t("om_audio_add_named", entry.title))
                .accessibilityIdentifier("audio.row.\(index).add")
            }
            .frame(height: 70)
            Rectangle().fill(AureaColors.addPanelDeep).frame(height: 0.5)
        }
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("audio.row.\(index)")
        .onAppear {
            let url = entry.url
            DispatchQueue.global(qos: .utility).async {
                let image = OmegaAudioLibrary.cover(url)
                DispatchQueue.main.async { cover = image }
            }
        }
    }
}

/// O seletor de arquivos de áudio do sistema (cópia: o original fica onde está).
private struct OmegaAudioFilePicker: UIViewControllerRepresentable {
    let picked: (URL?) -> Void
    func makeCoordinator() -> Coordinator { Coordinator(picked: picked) }
    func makeUIViewController(context: Context) -> UIDocumentPickerViewController {
        let picker = UIDocumentPickerViewController(forOpeningContentTypes: [.audio, .movie, .data], asCopy: true)
        picker.delegate = context.coordinator
        return picker
    }
    func updateUIViewController(_ controller: UIDocumentPickerViewController, context: Context) {}
    final class Coordinator: NSObject, UIDocumentPickerDelegate {
        let picked: (URL?) -> Void
        init(picked: @escaping (URL?) -> Void) { self.picked = picked }
        func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) { picked(urls.first) }
        func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) { picked(nil) }
    }
}

/// O seletor da biblioteca de sons: uma pasta (sem copiar: o app lê e copia o que for
/// áudio) ou vários arquivos de áudio de uma vez.
private struct SoundFolderPicker: UIViewControllerRepresentable {
    let folders: Bool
    let picked: ([URL]) -> Void
    func makeCoordinator() -> Coordinator { Coordinator(picked: picked) }
    func makeUIViewController(context: Context) -> UIDocumentPickerViewController {
        let picker = folders ? UIDocumentPickerViewController(forOpeningContentTypes: [.folder], asCopy: false)
                             : UIDocumentPickerViewController(forOpeningContentTypes: [.audio], asCopy: true)
        picker.allowsMultipleSelection = !folders
        picker.delegate = context.coordinator
        return picker
    }
    func updateUIViewController(_ controller: UIDocumentPickerViewController, context: Context) {}
    final class Coordinator: NSObject, UIDocumentPickerDelegate {
        let picked: ([URL]) -> Void
        init(picked: @escaping ([URL]) -> Void) { self.picked = picked }
        func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) { picked(urls) }
        func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) { picked([]) }
    }
}
