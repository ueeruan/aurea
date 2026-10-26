// =============================================================================
//  Aurea / platform / ios / app / MediaBrowserView.swift
//
//  Omega 0.3.2, área 08: a faixa rápida de mídia do painel de adicionar (P0-3)
//  e o navegador de mídia em tela cheia ("Ver tudo", P1-1). Fotos e vídeos
//  juntos, álbuns, prévia 1 : 0,7, régua da duração da imagem, corte do vídeo,
//  seleção múltipla numerada com "Mesmo início" e "Em sequência" — as regras de
//  tempo e o desfazer único são do motor (EngineOmegaAdd.cpp). Par de
//  editor/MediaGallery.kt e editor/MediaBrowser.kt (Android): ids iguais.
// =============================================================================
import SwiftUI
import UIKit
import Photos
import AVFoundation

// MARK: - A biblioteca de fotos (fotos e vídeos juntos, álbuns)

struct OmegaAlbum: Identifiable, Equatable {
    let id: String          // "" = Tudo
    let title: String
    let count: Int
    let cover: PHAsset?
    let video: Bool
    static func == (a: OmegaAlbum, b: OmegaAlbum) -> Bool { a.id == b.id && a.count == b.count }
}

/// Fotos e vídeos do Fotos, do mais novo para o mais antigo, e os álbuns (o
/// álbum escolhido fica guardado). Usado pela faixa e pelo navegador.
final class OmegaPhotoLibrary: NSObject, ObservableObject, PHPhotoLibraryChangeObserver {
    @Published var assets: PHFetchResult<PHAsset>?
    @Published var albums: [OmegaAlbum] = []
    @Published var status = PHPhotoLibrary.authorizationStatus(for: .readWrite)
    @Published var loading = false
    @Published var albumID: String = OmegaAddPrefs.string("media.album") ?? ""
    private var generation = 0
    private var requesting = false

    override init() { super.init(); PHPhotoLibrary.shared().register(self) }
    deinit { PHPhotoLibrary.shared().unregisterChangeObserver(self) }
    func photoLibraryDidChange(_ changeInstance: PHChange) { DispatchQueue.main.async { [weak self] in self?.reload() } }

    var granted: Bool { status == .authorized || status == .limited }

    private static func options() -> PHFetchOptions {
        let options = PHFetchOptions()
        options.predicate = NSPredicate(format: "mediaType == %d || mediaType == %d", PHAssetMediaType.image.rawValue, PHAssetMediaType.video.rawValue)
        options.sortDescriptors = [NSSortDescriptor(key: "creationDate", ascending: false)]
        return options
    }

    func select(_ album: OmegaAlbum) {
        albumID = album.id
        OmegaAddPrefs.set(album.id, "media.album")
        reload()
    }

    func reload() {
        generation += 1
        let request = generation, album = albumID
        status = PHPhotoLibrary.authorizationStatus(for: .readWrite)
        if status == .notDetermined {
            loading = true
            guard !requesting else { return }
            requesting = true
            PHPhotoLibrary.requestAuthorization(for: .readWrite) { [weak self] _ in DispatchQueue.main.async {
                self?.requesting = false
                self?.reload()
            } }
            return
        }
        guard granted else { assets = nil; loading = false; return }
        loading = assets == nil
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            let options = Self.options()
            var result = PHAsset.fetchAssets(with: options)
            var found: [OmegaAlbum] = [OmegaAlbum(id: "", title: AureaText.t("om_media_all_albums"), count: result.count, cover: result.firstObject, video: false)]
            for type in [PHAssetCollectionType.smartAlbum, .album] {
                let collections = PHAssetCollection.fetchAssetCollections(with: type, subtype: .any, options: nil)
                collections.enumerateObjects { collection, _, _ in
                    if type == .smartAlbum && collection.assetCollectionSubtype == .smartAlbumUserLibrary { return }
                    let inside = PHAsset.fetchAssets(in: collection, options: options)
                    guard inside.count > 0 else { return }
                    var allVideo = true
                    inside.enumerateObjects { asset, _, stop in if asset.mediaType != .video { allVideo = false; stop.pointee = true } }
                    found.append(OmegaAlbum(id: collection.localIdentifier, title: collection.localizedTitle ?? "—",
                                            count: inside.count, cover: inside.firstObject, video: allVideo))
                }
            }
            if !album.isEmpty, let collection = PHAssetCollection.fetchAssetCollections(withLocalIdentifiers: [album], options: nil).firstObject {
                result = PHAsset.fetchAssets(in: collection, options: options)
            }
            let albums = [found[0]] + found.dropFirst().sorted { $0.count > $1.count }
            DispatchQueue.main.async {
                guard let self, self.generation == request else { return }
                if !album.isEmpty && !albums.contains(where: { $0.id == album }) { self.albumID = ""; OmegaAddPrefs.set("", "media.album") }
                self.assets = result
                self.albums = albums
                self.loading = false
            }
        }
    }

    var albumTitle: String { albums.first { $0.id == albumID }?.title ?? AureaText.t("om_media_all_albums") }

    func manage() {
        if status == .limited {
            let scenes = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }
            guard var presenter = scenes.flatMap(\.windows).first(where: \.isKeyWindow)?.rootViewController else { return }
            while let next = presenter.presentedViewController { presenter = next }
            PHPhotoLibrary.shared().presentLimitedLibraryPicker(from: presenter)
        } else if let url = URL(string: UIApplication.openSettingsURLString) { UIApplication.shared.open(url) }
    }
}

/// Mídias usadas (até 30; a faixa mostra as N primeiras quando "mídia recente" > 0
/// — padrão 0, como a referência) e a última duração de imagem.
enum OmegaMediaRecents {
    static let max = 30
    static var count: Int { min(max, Swift.max(0, OmegaAddPrefs.int("media.recentCount"))) }
    static var ids: [String] { OmegaAddPrefs.strings("media.recents") }
    static func markUsed(_ assets: [PHAsset]) {
        let keys = assets.map(\.localIdentifier)
        OmegaAddPrefs.set(Array((keys + ids.filter { !keys.contains($0) }).prefix(max)), "media.recents")
    }
}

/// `h:mm:ss` (ou `m:ss`) de uma duração.
func omegaDuration(_ seconds: Double) -> String {
    let s = Swift.max(0, Int(seconds))
    return s >= 3600 ? String(format: "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60) : String(format: "%d:%02d", s / 60, s % 60)
}

/// Miniatura do Fotos (cantos retos, véu leve).
struct OmegaAssetThumb: View {
    let asset: PHAsset
    var size: CGFloat = 256
    @State private var image: UIImage?
    @State private var request = PHInvalidImageRequestID
    var body: some View {
        AureaColors.addTile
            .overlay { if let image { Image(uiImage: image).resizable().scaledToFill() } }
            .overlay { AureaColors.addScrimSoft }
            .clipped()
            .onAppear {
                let options = PHImageRequestOptions(); options.isNetworkAccessAllowed = true; options.resizeMode = .fast
                request = PHImageManager.default().requestImage(for: asset, targetSize: CGSize(width: size, height: size), contentMode: .aspectFill,
                                                                options: options) { result, _ in DispatchQueue.main.async { image = result } }
            }
            .onDisappear { PHImageManager.default().cancelImageRequest(request) }
    }
}

/// Exporta as mídias do Fotos para arquivos temporários (uma por vez), na ordem.
enum OmegaAssetExporter {
    static func export(_ assets: [PHAsset], done: @escaping ([(URL, Bool)?]) -> Void) {
        var out: [(URL, Bool)?] = []
        func next(_ index: Int) {
            if index == assets.count { DispatchQueue.main.async { done(out) }; return }
            let asset = assets[index]
            let video = asset.mediaType == .video
            let resources = PHAssetResource.assetResources(for: asset)
            let primary: PHAssetResourceType = video ? .fullSizeVideo : .fullSizePhoto
            let fallback: PHAssetResourceType = video ? .video : .photo
            guard let resource = resources.first(where: { $0.type == primary }) ?? resources.first(where: { $0.type == fallback }) else {
                out.append(nil); next(index + 1); return
            }
            let folder = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
            do { try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true) }
            catch { out.append(nil); next(index + 1); return }
            let destination = folder.appendingPathComponent(resource.originalFilename)
            let options = PHAssetResourceRequestOptions(); options.isNetworkAccessAllowed = true
            PHAssetResourceManager.default().writeData(for: resource, toFile: destination, options: options) { error in
                if error != nil { try? FileManager.default.removeItem(at: folder); out.append(nil) }
                else { out.append((destination, video)) }
                next(index + 1)
            }
        }
        next(0)
    }
}

// MARK: - A faixa rápida de mídia (P0-3)

/// Grade HORIZONTAL de 2 fileiras com fotos e vídeos juntos, do mais recente.
/// Primeira célula partida: "Ver tudo" em cima e o álbum ▾ embaixo (menu de
/// álbuns). Com acesso parcial, "Gerenciar". No fim, os extras do AUREA: outros
/// arquivos, sequência de imagens e vídeo com IA. Tocar adiciona e fecha.
struct MediaStripView: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var shell: ShellPresentation
    @StateObject private var library = OmegaPhotoLibrary()
    @Environment(\.scenePhase) private var phase
    @State private var albumMenu = false
    @State private var importing = false
    let close: () -> Void

    var body: some View {
        ZStack {
            if library.loading || importing { ProgressView().tint(AureaColors.accent) }
            else if !library.granted {
                OmegaAccessNotice { library.manage() }
            } else {
                strip
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("add.media.strip")
        .onAppear { library.reload() }
        .onChange(of: phase) { if $0 == .active { library.reload() } }
    }

    private var strip: some View {
        GeometryReader { geometry in
            let cell = geometry.size.height / 2
            let assets = library.assets
            let recents = recentAssets(assets)
            ScrollViewReader { reader in
                ZStack(alignment: .leading) {
                    ScrollView(.horizontal, showsIndicators: false) {
                        LazyHGrid(rows: [GridItem(.fixed(cell), spacing: 0), GridItem(.fixed(cell), spacing: 0)], spacing: 0) {
                            firstCell(cell).id("first")
                            if library.status == .limited {
                                StripActionCell(cell: cell, label: AureaText.t("gallery_manage"), glyph: CupertinoGlyph.PlusSquareOnSquare, tag: "add.media.manage") { library.manage() }
                            }
                            ForEach(recents, id: \.localIdentifier) { asset in item(asset, cell: cell, recent: true) }
                            if let assets {
                                ForEach(0..<assets.count, id: \.self) { index in
                                    let asset = assets.object(at: index)
                                    if !recents.contains(where: { $0.localIdentifier == asset.localIdentifier }) { item(asset, cell: cell, recent: false) }
                                }
                            }
                            if (assets?.count ?? 0) == 0 {
                                Text(AureaText.t("gallery_empty")).font(.aurea(size: 15)).foregroundStyle(AureaColors.text)
                                    .multilineTextAlignment(.center).frame(width: 200, height: cell)
                            }
                            // Extras do AUREA, no fim da faixa.
                            StripActionCell(cell: cell, label: AureaText.t("gallery_files"), glyph: CupertinoGlyph.Folder, tag: "add.media.files") {
                                omegaRequestPicker(model, shell, .gallery)
                            }
                            StripActionCell(cell: cell, label: AureaText.t("seq_title"), glyph: CupertinoGlyph.SquareStack3dUp, tag: "add.media.sequence") {
                                // A escolha pasta × quadros mora na raiz (AddLayerPickers). Corrige o
                                // iOS, onde a ficha "Sequência" ficava inalcançável.
                                model.showAddLayer = false
                                model.pendingOverlayImport = false
                                shell.sequenceChooser = true
                            }
                            StripActionCell(cell: cell, label: AureaText.t("sh_add_ai_video"), glyph: CupertinoGlyph.WandStars, tag: "add.media.ai") {
                                model.openPanel(.aiVideo)
                            }
                        }
                    }
                    Button { withAnimation { reader.scrollTo("first", anchor: .leading) } } label: {
                        CupertinoGlyph.text(CupertinoGlyph.ChevronLeft, size: 12, color: AureaColors.text).mirrorsInRtl()
                            .frame(width: 15, height: 30)
                            .background(AureaColors.addPanelDeep.opacity(0.85), in: OmegaTrailingRoundedRect(trailing: 4))
                            .frame(width: 24, height: 44, alignment: .leading).contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel(AureaText.t("om_media_back_to_start"))
                    .accessibilityIdentifier("add.media.cue")
                }
            }
        }
    }

    private func recentAssets(_ assets: PHFetchResult<PHAsset>?) -> [PHAsset] {
        let count = OmegaMediaRecents.count
        guard count > 0 else { return [] }
        let ids = Array(OmegaMediaRecents.ids.prefix(count))
        guard !ids.isEmpty else { return [] }
        let found = PHAsset.fetchAssets(withLocalIdentifiers: ids, options: nil)
        var byId: [String: PHAsset] = [:]
        found.enumerateObjects { asset, _, _ in byId[asset.localIdentifier] = asset }
        // O arquivo sumiu do Fotos: a entrada sai.
        if byId.count != ids.count { OmegaAddPrefs.set(OmegaMediaRecents.ids.filter { byId[$0] != nil }, "media.recents") }
        return ids.compactMap { byId[$0] }
    }

    private func firstCell(_ cell: CGFloat) -> some View {
        VStack(spacing: 5) {
            Button { model.omegaMediaBrowser = true } label: {
                HStack(spacing: 4) {
                    CupertinoGlyph.text(CupertinoGlyph.SquareGrid2x2, size: 14, color: AureaColors.text)
                    Text(AureaText.t("om_add_view_all")).font(.aurea(size: 12)).foregroundStyle(AureaColors.text).lineLimit(2).minimumScaleFactor(0.67)
                }
                .padding(.horizontal, 6).frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(AureaColors.addTile, in: RoundedRectangle(cornerRadius: 4)).contentShape(Rectangle())
            }.buttonStyle(AureaPressStyle()).accessibilityIdentifier("add.media.viewAll")
            Button { albumMenu = true } label: {
                HStack(spacing: 2) {
                    Text(library.albumTitle).font(.aurea(size: 12)).foregroundStyle(AureaColors.text).lineLimit(2).minimumScaleFactor(0.67)
                    CupertinoGlyph.text(CupertinoGlyph.ChevronDown, size: 11, color: AureaColors.text)
                }
                .padding(.horizontal, 6).frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(AureaColors.addTile, in: RoundedRectangle(cornerRadius: 4)).contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle())
            .accessibilityLabel(library.albumTitle)
            .accessibilityIdentifier("add.media.album")
            .aureaAnchoredMenu(isPresented: $albumMenu, tag: "add.media.album.menu") {
                library.albums.enumerated().map { index, album in
                    MenuEntry.item(album.title, sublabel: String(album.count), selected: album.id == library.albumID, tag: "add.media.album.\(index)") {
                        library.select(album)
                    }
                }
            }
        }
        .padding(6)
        .frame(width: cell, height: cell)
    }

    private func item(_ asset: PHAsset, cell: CGFloat, recent: Bool) -> some View {
        let video = asset.mediaType == .video
        let name = PHAssetResource.assetResources(for: asset).first?.originalFilename ?? asset.localIdentifier
        let label = video ? AureaText.t("om_media_video_named", name, omegaDuration(asset.duration)) : AureaText.t("om_media_photo_named", name)
        return Button { pick(asset) } label: {
            OmegaAssetThumb(asset: asset)
                .padding(0.5)
                .overlay { if video { CupertinoGlyph.text(CupertinoGlyph.PlayFill, size: 18, color: AureaColors.onImage) } }
                .overlay(alignment: .bottomTrailing) {
                    if video { Text(omegaDuration(asset.duration)).font(.aurea(size: 10, weight: .semibold)).foregroundStyle(AureaColors.onImage)
                        .shadow(color: AureaColors.imageScrim, radius: 1.5).padding(4) }
                }
                .overlay(alignment: .bottomLeading) {
                    if recent { CupertinoGlyph.text(CupertinoGlyph.Clock, size: 15, color: AureaColors.onImage).padding(3) }
                }
                .frame(width: cell, height: cell)
                .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle())
        .accessibilityLabel(recent ? label + ", " + AureaText.t("om_media_recent") : label)
        .accessibilityIdentifier("add.media.item.\(name)")
    }

    private func pick(_ asset: PHAsset) {
        guard !importing else { return }
        importing = true
        OmegaMediaRecents.markUsed([asset])
        let project = model.projectGeneration
        OmegaAssetExporter.export([asset]) { files in
            importing = false
            guard model.projectGeneration == project else { return }
            let ready = files.compactMap { $0 }
            if ready.isEmpty { model.toast = AureaText.t("msg_nao_consegui_ler_esse_arquivo"); return }
            model.importMediaBatch(ready)
            close()
        }
    }
}

/// Retângulo com só os cantos do fim arredondados (iOS 16 não tem `UnevenRoundedRectangle`).
struct OmegaTrailingRoundedRect: Shape {
    var trailing: CGFloat
    func path(in rect: CGRect) -> Path {
        let r = min(trailing, rect.height / 2, rect.width / 2)
        var p = Path()
        p.move(to: CGPoint(x: rect.minX, y: rect.minY))
        p.addLine(to: CGPoint(x: rect.maxX - r, y: rect.minY))
        p.addArc(center: CGPoint(x: rect.maxX - r, y: rect.minY + r), radius: r, startAngle: .degrees(270), endAngle: .degrees(0), clockwise: false)
        p.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY - r))
        p.addArc(center: CGPoint(x: rect.maxX - r, y: rect.maxY - r), radius: r, startAngle: .degrees(0), endAngle: .degrees(90), clockwise: false)
        p.addLine(to: CGPoint(x: rect.minX, y: rect.maxY))
        p.closeSubpath()
        return p
    }
}

/// Sem acesso: a explicação e "Permitir acesso".
struct OmegaAccessNotice: View {
    let allow: () -> Void
    var body: some View {
        VStack(spacing: 10) {
            Text(AureaText.t("gallery_access")).font(.aurea(size: 13)).foregroundStyle(AureaColors.text)
                .multilineTextAlignment(.center).lineLimit(3)
            Button(action: allow) {
                Text(AureaText.t("gallery_allow")).font(.aurea(size: 14, weight: .semibold)).foregroundStyle(AureaColors.text)
                    .padding(.horizontal, 16).frame(height: 38)
                    .background(AureaColors.addTile, in: RoundedRectangle(cornerRadius: 4))
            }.buttonStyle(AureaPressStyle()).accessibilityIdentifier("add.media.allow")
        }.padding(.horizontal, 24)
    }
}

// MARK: - O navegador de mídia em tela cheia (P1-1)

/// O corte de um vídeo escolhido (s da fonte).
private struct OmegaTrim: Equatable { var start: Double; var end: Double }

/// Overshoot (tensão 2), como o recolher da prévia da referência.
private let omegaOvershoot = Animation.timingCurve(0.34, 1.56, 0.64, 1, duration: 0.3)

/// Prévia 1 : 0,7 da mídia em foco, a barra de controle (régua da duração da
/// imagem ou corte do vídeo), a grade de 4 colunas e o álbum no título.
/// Pressão longa entra na seleção múltipla numerada; "Mesmo início" e "Em
/// sequência" adicionam na ordem de escolha, cada mídia com a sua duração ou o
/// seu corte, num passo de desfazer só.
struct MediaBrowserView: View {
    @EnvironmentObject private var model: AureaModel
    @StateObject private var library = OmegaPhotoLibrary()
    @State private var focused: PHAsset?
    @State private var multi = false
    @State private var selection: [PHAsset] = []
    @State private var ownDuration: [String: Int64] = [:]
    @State private var trims: [String: OmegaTrim] = [:]
    @State private var imageFrames: Int64 = 0
    @State private var albumsOpen = false
    @State private var tip = !OmegaAddPrefs.bool("media.albumTip")
    @State private var collapsed: CGFloat = 0
    @State private var dragStart: CGFloat?
    @State private var chip = true
    @State private var importing = false

    private var fps: Double { max(1, model.compositionFps) }
    private var maxFrames: Int64 { Int64((fps * 10).rounded()) + 50 }

    var body: some View {
        GeometryReader { geometry in
            let previewFull = geometry.size.width * 0.7
            VStack(spacing: 0) {
                if multi { multiBar } else { titleBar }
                if !library.granted && !library.loading {
                    OmegaAccessNotice { library.manage() }.frame(maxWidth: .infinity, maxHeight: .infinity)
                } else if library.loading || library.assets == nil {
                    ProgressView().tint(AureaColors.accent).frame(maxWidth: .infinity, maxHeight: .infinity)
                } else {
                    if let asset = focused {
                        let height = max(0, previewFull - min(collapsed, previewFull))
                        if height > 0.5 {
                            OmegaMediaPreview(asset: asset, trim: trims[asset.localIdentifier], chip: $chip)
                                .frame(height: height).clipped()
                        }
                        controlBar(asset, previewFull: previewFull)
                    }
                    if library.status == .limited { partialNotice }
                    grid
                }
            }
            .overlay(alignment: .top) {
                if albumsOpen { albumList.padding(.top, 44) }
                else if tip && !multi && library.assets != nil {
                    Text(AureaText.t("om_media_change_album_tip")).font(.aurea(size: 13)).foregroundStyle(AureaColors.lightText)
                        .padding(.horizontal, 12).padding(.vertical, 8)
                        .background(AureaColors.lightSurface, in: RoundedRectangle(cornerRadius: 6))
                        .padding(.top, 48)
                        .onTapGesture { tip = false; OmegaAddPrefs.set(true, "media.albumTip") }
                }
            }
            .overlay { if importing { ProgressView().tint(AureaColors.accent).frame(maxWidth: .infinity, maxHeight: .infinity).background(AureaColors.exportScrim) } }
        }
        .background(AureaColors.addPanel.ignoresSafeArea())
        .preferredColorScheme(.dark)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("media.browser")
        .onAppear {
            let saved = Int64(OmegaAddPrefs.int("media.imageDuration"))
            imageFrames = (1...maxFrames).contains(saved) ? saved : Int64((fps * 3).rounded())
            library.reload()
        }
        .onDisappear { OmegaAddPrefs.set(Int(imageFrames), "media.imageDuration") }
        .onChange(of: library.assets) { assets in
            guard let assets else { return }
            if let focused, assets.contains(focused) { return }
            focused = assets.firstObject
        }
    }

    // MARK: Barras

    private var titleBar: some View {
        ZStack {
            Button { albumsOpen.toggle(); if tip { tip = false; OmegaAddPrefs.set(true, "media.albumTip") } } label: {
                HStack(spacing: 6) {
                    Text(library.albumTitle).font(.aurea(size: 17)).foregroundStyle(AureaColors.text).lineLimit(1)
                    CupertinoGlyph.text(CupertinoGlyph.ChevronDown, size: 13, color: albumsOpen ? AureaColors.text : AureaColors.muted)
                        .rotationEffect(.degrees(albumsOpen ? 180 : 0)).animation(.easeInOut(duration: 0.15), value: albumsOpen)
                }.padding(.horizontal, 10).frame(height: 44).contentShape(Rectangle())
            }.buttonStyle(.plain).accessibilityLabel(library.albumTitle).accessibilityIdentifier("media.album")
            if !albumsOpen {
                HStack(spacing: 0) {
                    barButton(CupertinoGlyph.Xmark, "editor_fechar", "media.close") { model.omegaMediaBrowser = false }
                    Spacer(minLength: 0)
                    barButton(CupertinoGlyph.CheckmarkCircle, "om_media_select_multiple", "media.multi") {
                        multi = true
                        if let focused, !selection.contains(focused) { selection.append(focused) }
                    }
                    barButton(CupertinoGlyph.Plus, "om_media_add", "media.add", enabled: focused != nil) {
                        if let focused { add([focused], placement: 0) }
                    }
                }
            }
        }
        .frame(height: 44)
        .background(AureaColors.addPanel)
    }

    private var multiBar: some View {
        HStack(spacing: 0) {
            barButton(CupertinoGlyph.Xmark, "editor_fechar", "media.multi.close", tint: AureaColors.onAccent) { leaveMulti() }
            Text(AureaText.plural("om_media_selected", selection.count, selection.count)).font(.aurea(size: 17))
                .foregroundStyle(AureaColors.onAccent).lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
            barButton(CupertinoGlyph.SquareOnSquare, "om_media_add_same_start", "media.multi.sameStart", tint: AureaColors.onAccent,
                      enabled: !selection.isEmpty) { add(selection, placement: 0) }
            barButton(CupertinoGlyph.ArrowRightToLine, "om_media_add_sequence", "media.multi.sequence", tint: AureaColors.onAccent,
                      enabled: !selection.isEmpty) { add(selection, placement: 1) }
        }
        .frame(height: 44)
        .background(AureaColors.accent)
    }

    private func barButton(_ glyph: Character, _ key: String, _ tag: String, tint: Color = AureaColors.text, enabled: Bool = true,
                           action: @escaping () -> Void) -> some View {
        Button(action: action) {
            CupertinoGlyph.text(glyph, size: 20, color: enabled ? tint : tint.opacity(0.35)).frame(width: 44, height: 44).contentShape(Rectangle())
        }.buttonStyle(.plain).disabled(!enabled).accessibilityLabel(AureaText.t(key)).accessibilityIdentifier(tag)
    }

    // MARK: Controle (duração ou corte) e o recolher da prévia

    @ViewBuilder private func controlBar(_ asset: PHAsset, previewFull: CGFloat) -> some View {
        ZStack(alignment: .topTrailing) {
            Group {
                if asset.mediaType == .video {
                    OmegaTrimBar(asset: asset, trim: Binding(
                        get: { trims[asset.localIdentifier] ?? OmegaTrim(start: 0, end: asset.duration) },
                        set: { trims[asset.localIdentifier] = $0 }))
                } else {
                    let own = ownDuration[asset.localIdentifier]
                    OmegaDurationRuler(frames: own ?? imageFrames, maxFrames: maxFrames, fps: fps, linkVisible: multi, own: own != nil,
                                       onLink: {
                        if own != nil { ownDuration[asset.localIdentifier] = nil } else { ownDuration[asset.localIdentifier] = imageFrames }
                    }) { frames in
                        if own != nil { ownDuration[asset.localIdentifier] = frames } else { imageFrames = frames }
                    }
                }
            }
            if collapsed > 0 {
                Button { withAnimation(omegaOvershoot) { collapsed = 0 } } label: {
                    CupertinoGlyph.text(CupertinoGlyph.ChevronDown, size: 14, color: AureaColors.text).frame(width: 28, height: 28).contentShape(Rectangle())
                }.buttonStyle(.plain).accessibilityLabel(AureaText.t("om_media_show_preview")).accessibilityIdentifier("media.collapse")
            }
        }
        .frame(height: 55)
        .background(AureaColors.addControlBar)
        // Arrastar a barra para cima recolhe a prévia (abaixo de 1/3 volta; acima recolhe).
        .simultaneousGesture(DragGesture(minimumDistance: 12).onChanged { value in
            guard abs(value.translation.height) > abs(value.translation.width) else { return }
            let start = dragStart ?? collapsed
            if dragStart == nil { dragStart = collapsed }
            collapsed = min(previewFull, max(0, start - value.translation.height))
        }.onEnded { value in
            guard dragStart != nil else { return }
            dragStart = nil
            let target: CGFloat = value.predictedEndTranslation.height > 300 ? 0 : (collapsed > previewFull / 3 ? previewFull : 0)
            withAnimation(omegaOvershoot) { collapsed = target }
        })
    }

    private var partialNotice: some View {
        HStack {
            Text(AureaText.t("om_media_partial")).font(.aurea(size: 13)).foregroundStyle(AureaColors.text).padding(.vertical, 10)
            Spacer(minLength: 8)
            Button { library.manage() } label: {
                Text(AureaText.t("om_media_manage")).font(.aurea(size: 14, weight: .semibold)).foregroundStyle(AureaColors.accent)
                    .padding(.horizontal, 8).frame(minHeight: 48)
            }.buttonStyle(.plain).accessibilityIdentifier("media.manage")
        }
        .padding(.horizontal, 10)
        .background(AureaColors.addControlBar, in: RoundedRectangle(cornerRadius: 6))
        .padding(.horizontal, 8).padding(.top, 8)
    }

    // MARK: Grade

    @ViewBuilder private var grid: some View {
        if let assets = library.assets, assets.count > 0 {
            ScrollView {
                LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 0), count: 4), spacing: 0) {
                    ForEach(0..<assets.count, id: \.self) { index in
                        let asset = assets.object(at: index)
                        gridCell(asset)
                    }
                }
            }
            .padding(.top, 8)
            .accessibilityIdentifier("media.grid")
        } else {
            Text(AureaText.t("gallery_empty")).font(.aurea(size: 15)).foregroundStyle(AureaColors.text)
                .frame(maxWidth: .infinity, maxHeight: .infinity).accessibilityIdentifier("media.empty")
        }
    }

    private func gridCell(_ asset: PHAsset) -> some View {
        let id = asset.localIdentifier
        let video = asset.mediaType == .video
        let order = multi ? selection.firstIndex(of: asset) : nil
        let isFocused = focused == asset
        let name = PHAssetResource.assetResources(for: asset).first?.originalFilename ?? id
        let label = video ? AureaText.t("om_media_video_named", name, omegaDuration(asset.duration)) : AureaText.t("om_media_photo_named", name)
        return OmegaAssetThumb(asset: asset, size: 200)
            .aspectRatio(1, contentMode: .fill)
            .padding(0.5)
            .overlay(alignment: .bottomTrailing) {
                if video { Text(omegaDuration(asset.duration)).font(.aurea(size: 10, weight: .semibold)).foregroundStyle(AureaColors.onImage)
                    .shadow(color: AureaColors.imageScrim, radius: 1.5).padding(4) }
            }
            .overlay(alignment: .bottomLeading) {
                HStack(spacing: 2) {
                    if ownDuration[id] != nil { CupertinoGlyph.text(CupertinoGlyph.Link, size: 12, color: AureaColors.onImage) }
                    if trims[id] != nil { CupertinoGlyph.text(CupertinoGlyph.Scissors, size: 12, color: AureaColors.onImage) }
                }.padding(.leading, 4).padding(.bottom, 5)
            }
            .overlay { if isFocused { Rectangle().stroke(AureaColors.accent, lineWidth: 2) } }
            .overlay(alignment: .topTrailing) {
                if let order {
                    Text("\(order + 1)").font(.aurea(size: 10, weight: .bold)).foregroundStyle(AureaColors.onAccent)
                        .frame(width: 20, height: 20).background(AureaColors.accent, in: Circle()).padding(4)
                }
            }
            .contentShape(Rectangle())
            .onTapGesture { tap(asset) }
            .onLongPressGesture(minimumDuration: 0.45) {
                guard !multi else { return }
                multi = true; selection = [asset]; focused = asset
            }
            .accessibilityElement(children: .ignore)
            .accessibilityLabel([label, ownDuration[id] != nil ? AureaText.t("om_media_duration_own") : nil,
                                 trims[id] != nil ? AureaText.t("om_media_trimmed") : nil].compactMap { $0 }.joined(separator: ", "))
            .accessibilityAddTraits(order != nil || isFocused ? [.isButton, .isSelected] : .isButton)
            .accessibilityIdentifier("media.item.\(name)")
    }

    private var albumList: some View {
        ScrollView {
            LazyVStack(spacing: 0) {
                ForEach(Array(library.albums.enumerated()), id: \.element.id) { index, album in
                    Button { library.select(album); albumsOpen = false } label: {
                        HStack(spacing: 0) {
                            ZStack {
                                AureaColors.addPanelDeep
                                if let cover = album.cover { OmegaAssetThumb(asset: cover, size: 120) }
                                if album.video { CupertinoGlyph.text(CupertinoGlyph.PlayFill, size: 14, color: AureaColors.onImage) }
                            }.frame(width: 56, height: 56).clipped().padding(7)
                            VStack(alignment: .leading, spacing: 2) {
                                Text(album.title).font(.aurea(size: 14)).foregroundStyle(AureaColors.text).lineLimit(1)
                                Text(String(album.count)).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                            }.padding(.leading, 6)
                            Spacer(minLength: 0)
                        }
                        .frame(height: 70)
                        .background(album.id == library.albumID ? AureaColors.addTile : AureaColors.addPanel)
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .accessibilityAddTraits(album.id == library.albumID ? .isSelected : [])
                    .accessibilityIdentifier("media.album.\(index)")
                    Rectangle().fill(AureaColors.addPanelDeep).frame(height: 0.5)
                }
            }
        }
        .background(AureaColors.addPanel)
        .accessibilityIdentifier("media.albums")
    }

    // MARK: Ações

    private func tap(_ asset: PHAsset) {
        guard multi else { focused = asset; return }
        if !selection.contains(asset) { selection.append(asset); focused = asset }
        else if focused == asset {
            selection.removeAll { $0 == asset }
            ownDuration[asset.localIdentifier] = nil; trims[asset.localIdentifier] = nil
            focused = selection.last ?? asset
        } else { focused = asset }
    }

    private func leaveMulti() { multi = false; selection = [] }

    private func add(_ assets: [PHAsset], placement: UInt32) {
        guard !assets.isEmpty, !importing else { return }
        importing = true
        OmegaMediaRecents.markUsed(assets)
        OmegaAddPrefs.set(Int(imageFrames), "media.imageDuration")
        let project = model.projectGeneration
        let ownDuration = ownDuration, trims = trims, imageFrames = imageFrames
        OmegaAssetExporter.export(assets) { files in
            importing = false
            guard model.projectGeneration == project else { return }
            var picks: [AureaModel.OmegaMediaPick] = []
            for (asset, file) in zip(assets, files) {
                guard let file else { continue }
                let (url, video) = file
                let id = asset.localIdentifier
                var pick = AureaModel.OmegaMediaPick(url: url, video: video)
                if video, let trim = trims[id] {
                    pick.trimInUs = Int64(trim.start * 1_000_000); pick.trimOutUs = Int64(trim.end * 1_000_000)
                } else if !video { pick.durationFrames = ownDuration[id] ?? imageFrames }
                picks.append(pick)
            }
            if picks.isEmpty { model.toast = AureaText.t("msg_nao_consegui_ler_esse_arquivo"); return }
            model.importMediaPicks(picks, placement: placement)
            model.omegaMediaBrowser = false
            model.showAddLayer = false
        }
    }
}

/// A prévia (preta): foto ou vídeo (só o trecho cortado), ▶ de 30 e o chip do
/// nome, que mostra as informações da mídia.
private struct OmegaMediaPreview: View {
    let asset: PHAsset
    let trim: OmegaTrim?
    @Binding var chip: Bool
    @State private var image: UIImage?
    @State private var player: AVPlayer?
    @State private var playing = false
    @State private var observer: Any?

    var body: some View {
        ZStack(alignment: .topLeading) {
            AureaColors.stage
            if asset.mediaType == .video, let player {
                OmegaPlayerLayer(player: player)
            } else if let image {
                Image(uiImage: image).resizable().scaledToFit().frame(maxWidth: .infinity, maxHeight: .infinity)
            }
            Color.clear.contentShape(Rectangle()).onTapGesture { if asset.mediaType == .video && playing { stop() } else { chip.toggle() } }
            if asset.mediaType == .video && !playing {
                Button { play() } label: {
                    CupertinoGlyph.text(CupertinoGlyph.PlayFill, size: 30, color: AureaColors.onImage).frame(width: 48, height: 48).contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .accessibilityLabel(AureaText.t("editor_reproduzir"))
                .accessibilityIdentifier("media.preview.play")
            }
            if chip {
                let name = PHAssetResource.assetResources(for: asset).first?.originalFilename ?? ""
                Button { showInfo(name) } label: {
                    HStack(spacing: 6) {
                        CupertinoGlyph.text(CupertinoGlyph.InfoCircle, size: 14, color: AureaColors.onImage)
                        Text(name).font(.aurea(size: 12)).foregroundStyle(AureaColors.onImage).lineLimit(1).frame(maxWidth: 180, alignment: .leading)
                    }
                    .padding(.horizontal, 10).frame(height: 30)
                    .background(AureaColors.addChipScrim, in: Capsule())
                }.buttonStyle(.plain).padding(8).accessibilityIdentifier("media.preview.info")
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("media.preview")
        .onAppear(perform: load)
        .onChange(of: asset) { _ in stop(); player = nil; image = nil; load() }
        .onDisappear { stop() }
    }

    private func load() {
        let target = asset
        if asset.mediaType == .video {
            let options = PHVideoRequestOptions(); options.isNetworkAccessAllowed = true
            PHImageManager.default().requestPlayerItem(forVideo: asset, options: options) { item, _ in
                DispatchQueue.main.async { if target == asset, let item { player = AVPlayer(playerItem: item); seekToStart() } }
            }
        } else {
            let options = PHImageRequestOptions(); options.isNetworkAccessAllowed = true; options.deliveryMode = .highQualityFormat
            PHImageManager.default().requestImage(for: asset, targetSize: CGSize(width: 1280, height: 1280), contentMode: .aspectFit, options: options) { result, _ in
                DispatchQueue.main.async { if target == asset { image = result } }
            }
        }
    }

    private func seekToStart() {
        player?.seek(to: CMTime(seconds: trim?.start ?? 0, preferredTimescale: 600), toleranceBefore: .zero, toleranceAfter: .zero)
    }

    private func play() {
        guard let player else { return }
        seekToStart()
        let end = trim?.end ?? asset.duration
        if let observer { player.removeTimeObserver(observer) }
        observer = player.addPeriodicTimeObserver(forInterval: CMTime(value: 1, timescale: 25), queue: .main) { time in
            if time.seconds >= end - 0.02 { stop() }
        }
        player.play()
        playing = true
    }

    private func stop() {
        player?.pause()
        if let observer, let player { player.removeTimeObserver(observer) }
        observer = nil
        playing = false
        seekToStart()
    }

    private func showInfo(_ name: String) {
        var lines = [name, "\(asset.pixelWidth) × \(asset.pixelHeight)"]
        if asset.mediaType == .video { lines.append(omegaDuration(asset.duration)) }
        let message = lines.joined(separator: "\n")
        AureaPopupPresenter.shared.presentDialog { close in
            AureaDialog(title: AureaText.t("media_info_title"), message: message,
                        actions: [DialogAction(AureaText.t("common_ok"), role: .positive)], onDismiss: close, tag: "media.info")
        }
    }
}

/// Camada de vídeo sem controles.
private struct OmegaPlayerLayer: UIViewRepresentable {
    let player: AVPlayer
    final class LayerView: UIView {
        override class var layerClass: AnyClass { AVPlayerLayer.self }
        var playerLayer: AVPlayerLayer { layer as! AVPlayerLayer }
    }
    func makeUIView(context: Context) -> LayerView {
        let view = LayerView(); view.playerLayer.videoGravity = .resizeAspect; view.playerLayer.player = player; return view
    }
    func updateUIView(_ view: LayerView, context: Context) { view.playerLayer.player = player }
}

/// A régua da duração da imagem: agulha de destaque no meio, ticks a cada 5,5 pt
/// (um quadro cada; o maior a cada segundo), o valor `mm:ss:ff` à direita.
/// Arrastar muda a duração (1 quadro a fps×10+50). Na múltipla, o botão de
/// vínculo (55 pt) dá a duração própria da imagem em foco.
private struct OmegaDurationRuler: View {
    let frames: Int64
    let maxFrames: Int64
    let fps: Double
    let linkVisible: Bool
    let own: Bool
    let onLink: () -> Void
    let onChange: (Int64) -> Void
    @State private var dragBase: Int64?
    private let step: CGFloat = 5.5

    var body: some View {
        HStack(spacing: 0) {
            if linkVisible {
                Button(action: onLink) {
                    CupertinoGlyph.text(CupertinoGlyph.Link, size: 20, color: own ? AureaColors.accent : AureaColors.text).frame(width: 55, height: 55).contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel(AureaText.t(own ? "om_media_duration_own" : "om_media_duration_shared"))
                .accessibilityAddTraits(own ? .isSelected : [])
                .accessibilityIdentifier("media.duration.link")
            }
            Canvas { context, size in
                let mid = size.width / 2
                let fpsInt = max(1, Int(fps.rounded()))
                let half = Int64(mid / step) + 1
                for f in (frames - half)...(frames + half) where f >= 1 && f <= maxFrames {
                    let x = mid + CGFloat(f - frames) * step
                    let h = f % Int64(fpsInt) == 0 ? size.height * 0.42 : size.height * 0.24
                    var line = Path(); line.move(to: CGPoint(x: x, y: (size.height - h) / 2)); line.addLine(to: CGPoint(x: x, y: (size.height + h) / 2))
                    context.stroke(line, with: .color(AureaColors.text.opacity(0.55)), lineWidth: 0.5)
                }
                var needle = Path(); needle.move(to: CGPoint(x: mid, y: size.height * 0.18)); needle.addLine(to: CGPoint(x: mid, y: size.height * 0.82))
                context.stroke(needle, with: .color(AureaColors.accent), lineWidth: 1.5)
            }
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 2).onChanged { value in
                let base = dragBase ?? frames
                if dragBase == nil { dragBase = frames }
                let delta = Int64((-value.translation.width / step).rounded(.towardZero))
                onChange(min(maxFrames, max(1, base + delta)))
            }.onEnded { _ in dragBase = nil })
            .accessibilityElement()
            .accessibilityLabel(AureaText.t("om_media_image_duration"))
            .accessibilityValue(Self.time(frames, fps))
            .accessibilityAdjustableAction { direction in
                switch direction {
                case .increment: onChange(min(maxFrames, frames + 1))
                case .decrement: onChange(max(1, frames - 1))
                @unknown default: break
                }
            }
            .accessibilityIdentifier("media.duration")
            Text(Self.time(frames, fps)).font(.aurea(size: 16, weight: .bold).monospacedDigit()).foregroundStyle(AureaColors.text)
                .padding(.horizontal, 12)
        }
    }

    /// `mm:ss:ff` na taxa da composição.
    static func time(_ frames: Int64, _ fps: Double) -> String {
        let f = Int64(max(1, Int(fps.rounded())))
        let seconds = frames / f
        return String(format: "%02d:%02d:%02d", seconds / 60, seconds % 60, frames % f)
    }
}

/// O corte do vídeo: a tira de miniaturas (margens 25/12) e as alças de início
/// e fim por cima; fora do trecho fica escurecido.
private struct OmegaTrimBar: View {
    let asset: PHAsset
    @Binding var trim: OmegaTrim
    @State private var frames: [UIImage] = []
    @State private var dragBase: OmegaTrim?

    var body: some View {
        GeometryReader { geometry in
            let width = max(1, geometry.size.width)
            let duration = max(0.1, asset.duration)
            let a = CGFloat(trim.start / duration) * width
            let b = CGFloat(trim.end / duration) * width
            ZStack(alignment: .leading) {
                HStack(spacing: 0) {
                    ForEach(Array(frames.enumerated()), id: \.offset) { _, image in
                        Image(uiImage: image).resizable().scaledToFill().frame(width: width / CGFloat(max(1, frames.count)), height: geometry.size.height).clipped()
                    }
                }
                .frame(width: width, height: geometry.size.height, alignment: .leading)
                .background(AureaColors.addPanelDeep)
                .clipShape(RoundedRectangle(cornerRadius: 3))
                AureaColors.exportScrim.frame(width: max(0, a))
                AureaColors.exportScrim.frame(width: max(0, width - b)).offset(x: b)
                Rectangle().stroke(AureaColors.accent, lineWidth: 2).frame(width: max(0, b - a)).offset(x: a)
                handle(start: true, x: a, width: width, duration: duration)
                handle(start: false, x: b, width: width, duration: duration)
            }
        }
        .padding(EdgeInsets(top: 6, leading: 25, bottom: 6, trailing: 12))
        .accessibilityElement(children: .contain)
        .accessibilityLabel(AureaText.t("om_media_trim"))
        .accessibilityIdentifier("media.trim")
        .onAppear(perform: loadFrames)
        .onChange(of: asset) { _ in frames = []; loadFrames() }
    }

    private func handle(start: Bool, x: CGFloat, width: CGFloat, duration: Double) -> some View {
        RoundedRectangle(cornerRadius: 2).fill(AureaColors.accent).frame(width: 12)
            .frame(width: 44).contentShape(Rectangle())
            .offset(x: x - 22)
            .gesture(DragGesture(minimumDistance: 0).onChanged { value in
                let base = dragBase ?? trim
                if dragBase == nil { dragBase = trim }
                let delta = Double(value.translation.width / width) * duration
                if start { trim.start = min(max(0, base.start + delta), trim.end - 0.1) }
                else { trim.end = max(min(duration, base.end + delta), trim.start + 0.1) }
            }.onEnded { _ in dragBase = nil })
            .accessibilityLabel(AureaText.t(start ? "om_media_trim_start" : "om_media_trim_end"))
            .accessibilityValue(omegaDuration(start ? trim.start : trim.end))
            .accessibilityAdjustableAction { direction in
                let step = max(0.1, duration / 50)
                let sign: Double = direction == .increment ? 1 : -1
                if start { trim.start = min(max(0, trim.start + sign * step), trim.end - 0.1) }
                else { trim.end = max(min(duration, trim.end + sign * step), trim.start + 0.1) }
            }
            .accessibilityIdentifier(start ? "media.trim.start" : "media.trim.end")
    }

    private func loadFrames() {
        let target = asset
        let options = PHVideoRequestOptions(); options.isNetworkAccessAllowed = true
        PHImageManager.default().requestAVAsset(forVideo: asset, options: options) { avAsset, _, _ in
            guard let avAsset else { return }
            let generator = AVAssetImageGenerator(asset: avAsset)
            generator.appliesPreferredTrackTransform = true
            generator.maximumSize = CGSize(width: 120, height: 120)
            let duration = max(0.1, target.duration)
            var images: [UIImage] = []
            for i in 0..<8 {
                let time = CMTime(seconds: duration * Double(i) / 8, preferredTimescale: 600)
                if let cg = try? generator.copyCGImage(at: time, actualTime: nil) { images.append(UIImage(cgImage: cg)) }
            }
            DispatchQueue.main.async { if target == asset { frames = images } }
        }
    }
}
