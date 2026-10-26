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

    var body: some View {
        VStack(spacing: 0) {
            if let category {
                BrowserTopBar(title: group ?? AureaText.t(category.key), glyph: CupertinoGlyph.ChevronLeft,
                              description: AureaText.t("om_add_back"), tag: "audio.back", action: back)
            } else {
                BrowserTopBar(title: AureaText.t("om_audio_title"), glyph: CupertinoGlyph.Xmark,
                              description: AureaText.t("editor_fechar"), tag: "audio.close", action: dismiss)
            }
            if let all = entries {
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
    private func back() { preview.stop(); if group != nil { group = nil } else { category = nil } }

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
                            OmegaAudioLibrary.markUsed(entry.url)
                            model.importAudioSource(entry.url, name: entry.title)
                            model.omegaAudioBrowser = false
                            model.showAddLayer = false
                        }
                    }
                }
            }
        }
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
                            TimelineView(.periodic(from: .now, by: 0.015)) { _ in
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
