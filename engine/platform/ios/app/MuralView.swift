import SwiftUI
import PhotosUI
import AVKit
import StoreKit
import UniformTypeIdentifiers

// =============================================================================
//  Mural (0.4.8): edits em vídeo que todo mundo vê na tela inicial. Par do
//  MuralScreen.kt. Ver não precisa de conta; cada publicação é uma compra
//  (consumível) que o servidor confere com a App Store (discovery/mural.js).
// =============================================================================

private struct MuralAuthor: Codable { var id: String; var username: String; var name: String? }
private struct MuralItem: Codable, Identifiable {
    var id: String; var caption: String; var width: Int; var height: Int
    var durationMs: Double; var createdAt: Double; var mine: Bool; var author: MuralAuthor
}
private struct MuralPage: Codable { var items: [MuralItem]; var cursor: String? }
private struct MuralConfig: Codable { var posting: Bool; var productId: String; var maxBytes: Int }
private struct MuralId: Codable { var id: String }
private struct MuralFailure: Error { var code: String }

private actor MuralAPI {
    static let root = "https://aurea-ai-discovery.aureaapp.workers.dev/api/mural"
    let token: String
    init(token: String) { self.token = token }
    static func media(_ id: String, _ kind: String) -> URL? { URL(string: "\(root)/media/\(id)/\(kind)") }
    private func request(_ url: String, _ method: String) -> URLRequest? {
        guard let target = URL(string: url) else { return nil }
        var r = URLRequest(url: target, timeoutInterval: 120)
        r.httpMethod = method
        if !token.isEmpty { r.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization") }
        r.setValue("application/json", forHTTPHeaderField: "Accept")
        return r
    }
    private func decode<T: Decodable>(_ data: Data, _ response: URLResponse, as type: T.Type) throws -> T {
        guard let http = response as? HTTPURLResponse else { throw MuralFailure(code: "mural_unavailable") }
        guard (200...299).contains(http.statusCode) else {
            let body = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any]
            throw MuralFailure(code: body?["error"] as? String ?? "mural_unavailable")
        }
        return try JSONDecoder().decode(type, from: data)
    }
    func call<T: Decodable>(_ path: String, method: String = "GET", body: [String: Any]? = nil, as type: T.Type) async throws -> T {
        guard var r = request(Self.root + path, method) else { throw MuralFailure(code: "mural_unavailable") }
        if let body {
            r.httpBody = try JSONSerialization.data(withJSONObject: body)
            r.setValue("application/json", forHTTPHeaderField: "Content-Type")
        }
        let (data, response) = try await URLSession.shared.data(for: r)
        return try decode(data, response, as: type)
    }
    func put(_ path: String, file: URL, type: String) async throws {
        guard var r = request(Self.root + path, "PUT") else { throw MuralFailure(code: "mural_unavailable") }
        r.setValue(type, forHTTPHeaderField: "Content-Type")
        let (data, response) = try await URLSession.shared.upload(for: r, fromFile: file)
        _ = try decode(data, response, as: [String: Bool].self)
    }
    func report(_ id: String, reason: String) async throws {
        let url = Self.root.replacingOccurrences(of: "/api/mural", with: "/api/community/reports")
        guard var r = request(url, "POST") else { throw MuralFailure(code: "mural_unavailable") }
        r.httpBody = try JSONSerialization.data(withJSONObject: ["kind": "mural", "id": id, "reason": reason])
        r.setValue("application/json", forHTTPHeaderField: "Content-Type")
        let (data, response) = try await URLSession.shared.data(for: r)
        _ = try decode(data, response, as: [String: Bool].self)
    }
}

private struct MuralMovie: Transferable {
    let url: URL
    static var transferRepresentation: some TransferRepresentation {
        FileRepresentation(contentType: .movie) { SentTransferredFile($0.url) } importing: { received in
            let target = FileManager.default.temporaryDirectory.appendingPathComponent("mural-upload.mov")
            try? FileManager.default.removeItem(at: target)
            try FileManager.default.copyItem(at: received.file, to: target)
            return MuralMovie(url: target)
        }
    }
}

private struct MuralPicked {
    let file: URL; let thumbFile: URL; let thumb: UIImage
    let width: Int; let height: Int; let durationMs: Int; let bytes: Int
}

/// Capa e medidas do vídeo. Nil = o arquivo não abre.
private func muralPrepare(_ file: URL) async -> MuralPicked? {
    let asset = AVURLAsset(url: file)
    guard let track = try? await asset.loadTracks(withMediaType: .video).first,
          let natural = try? await track.load(.naturalSize), let transform = try? await track.load(.preferredTransform),
          let duration = try? await asset.load(.duration) else { return nil }
    let size = natural.applying(transform)
    let generator = AVAssetImageGenerator(asset: asset)
    generator.appliesPreferredTrackTransform = true
    generator.maximumSize = CGSize(width: 540, height: 540)
    guard let frame = try? generator.copyCGImage(at: CMTime(seconds: 1, preferredTimescale: 600), actualTime: nil) else { return nil }
    let image = UIImage(cgImage: frame)
    let thumbFile = FileManager.default.temporaryDirectory.appendingPathComponent("mural-upload.jpg")
    guard let jpeg = image.jpegData(compressionQuality: 0.8), (try? jpeg.write(to: thumbFile)) != nil else { return nil }
    let bytes = (try? file.resourceValues(forKeys: [.fileSizeKey]))?.fileSize ?? 0
    return MuralPicked(file: file, thumbFile: thumbFile, thumb: image, width: Int(abs(size.width)), height: Int(abs(size.height)),
                       durationMs: Int(duration.seconds * 1000), bytes: bytes)
}

/// A faixa grande do Mural no topo da Home.
struct MuralBanner: View {
    let onOpen: () -> Void
    var body: some View {
        Button(action: onOpen) {
            HStack {
                VStack(alignment: .leading, spacing: 2) {
                    Text(AureaText.t("mural_title")).font(.aurea(size: 30, weight: .bold))
                    Text(AureaText.t("mural_tagline")).font(.aurea(size: 14)).lineLimit(2)
                }
                Spacer()
                CupertinoGlyph.text(CupertinoGlyph.RectangleStack, size: 34, color: AureaColors.onAccent)
            }
            .foregroundStyle(AureaColors.onAccent).padding(.horizontal, 20)
            .frame(maxWidth: .infinity, minHeight: 104)
            .background(LinearGradient(colors: [AureaColors.accent, AureaColors.brand], startPoint: .leading, endPoint: .trailing),
                        in: RoundedRectangle(cornerRadius: 20))
        }
        .buttonStyle(.plain).padding(.horizontal, 16).padding(.vertical, 8)
        .accessibilityIdentifier("home.mural")
    }
}

struct MuralView: View {
    let onBack: () -> Void
    @EnvironmentObject private var conta: ContaModel
    @State private var posts: [MuralItem] = []
    @State private var cursor: String?
    @State private var failed = false
    @State private var config: MuralConfig?
    @State private var product: Product?
    @State private var photo: PhotosPickerItem?
    @State private var picking = false
    @State private var picked: MuralPicked?
    @State private var caption = ""
    @State private var stage: String?
    @State private var notice: String?
    @State private var playing: MuralItem?
    @State private var reporting: MuralItem?
    @State private var removing: MuralItem?
    @AppStorage("aurea.mural.credit") private var savedCredit = ""
    private static let reasons: [(String, String)] = [("spam", "mod_reason_spam"), ("nudity", "mod_reason_nudity"),
        ("violence", "mod_reason_violence"), ("hate", "mod_reason_hate"), ("harassment", "mod_reason_harassment"),
        ("illegal", "mod_reason_illegal"), ("copyright", "mod_reason_copyright"), ("other", "mod_reason_other")]

    private var api: MuralAPI { MuralAPI(token: conta.sessao()?.token ?? "") }
    private var postLabel: String {
        product.map { String(format: AureaText.t("mural_post_price"), $0.displayPrice) } ?? AureaText.t("mural_post")
    }

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Button(AureaText.t("mural_back"), action: onBack).foregroundStyle(AureaColors.text)
                Spacer()
                Text(AureaText.t("mural_title")).font(.aurea(size: 22, weight: .bold))
                Spacer()
                Color.clear.frame(width: 64, height: 1)
            }.padding(.horizontal, 16).padding(.vertical, 8)
            Button(action: startPost) {
                Text(postLabel).font(.aurea(size: 16, weight: .semibold)).foregroundStyle(AureaColors.onAccent)
                    .frame(maxWidth: .infinity, minHeight: 52).background(AureaColors.accent, in: RoundedRectangle(cornerRadius: 14))
            }.buttonStyle(.plain).disabled(stage != nil).padding(.horizontal, 16).accessibilityIdentifier("mural.post")
            if let stage { Text(stage).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted).padding(.top, 10) }
            if posts.isEmpty {
                Spacer()
                Text(AureaText.t(failed ? "mural_load_failed" : "mural_empty")).font(.aurea(size: 15))
                    .foregroundStyle(AureaColors.muted).multilineTextAlignment(.center).padding(32)
                Spacer()
            } else {
                ScrollView {
                    LazyVGrid(columns: [GridItem(.flexible(), spacing: 8), GridItem(.flexible(), spacing: 8)], spacing: 8) {
                        ForEach(posts) { post in card(post) }
                        if cursor != nil { Color.clear.frame(height: 1).task { await load(reset: false) } }
                    }.padding(12)
                }
            }
        }
        .foregroundStyle(AureaColors.text)
        .accessibilityIdentifier("mural.screen")
        .task { await start() }
        .photosPicker(isPresented: $picking, selection: $photo, matching: .videos)
        .onChange(of: photo) { item in if let item { Task { await open(item) } } }
        .fullScreenCover(item: $playing) { post in
            if let url = MuralAPI.media(post.id, "video") { ExportMoviePlayer(url: url) }
        }
        .sheet(isPresented: Binding(get: { picked != nil }, set: { if !$0 && stage == nil { picked = nil } })) { sheet }
        .confirmationDialog(AureaText.t("mod_report_title"), isPresented: Binding(get: { reporting != nil }, set: { if !$0 { reporting = nil } }),
                            titleVisibility: .visible) {
            ForEach(Self.reasons, id: \.0) { reason in
                Button(AureaText.t(reason.1)) {
                    guard let post = reporting else { return }
                    Task {
                        do { try await api.report(post.id, reason: reason.0); notice = AureaText.t("mod_report_thanks") }
                        catch { notice = AureaText.t("mural_load_failed") }
                    }
                }
            }
        }
        .alert(AureaText.t("mural_delete"), isPresented: Binding(get: { removing != nil }, set: { if !$0 { removing = nil } })) {
            Button(AureaText.t("mural_delete"), role: .destructive) {
                guard let post = removing else { return }
                Task {
                    do {
                        _ = try await api.call("/posts/\(post.id)", method: "DELETE", as: [String: Bool].self)
                        posts.removeAll { $0.id == post.id }
                    } catch { notice = AureaText.t("mural_load_failed") }
                }
            }
        }
        .alert(notice ?? "", isPresented: Binding(get: { notice != nil }, set: { if !$0 { notice = nil } })) {
            Button("OK", role: .cancel) {}
        }
    }

    private func card(_ post: MuralItem) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            Button { playing = post } label: {
                Color.black.aspectRatio(9.0 / 14.0, contentMode: .fit).overlay {
                    AsyncImage(url: MuralAPI.media(post.id, "thumb")) { image in
                        image.resizable().scaledToFill()
                    } placeholder: { Color.black }
                }.clipped()
            }.buttonStyle(.plain)
            HStack(alignment: .top) {
                VStack(alignment: .leading, spacing: 1) {
                    Text("@" + post.author.username).font(.aurea(size: 13, weight: .semibold)).lineLimit(1)
                    if !post.caption.isEmpty {
                        Text(post.caption).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).lineLimit(2)
                    }
                }
                Spacer(minLength: 2)
                Button("⋯") { if post.mine { removing = post } else { reporting = post } }
                    .foregroundStyle(AureaColors.muted).frame(width: 32, height: 32)
            }.padding(.leading, 10).padding(.trailing, 2).padding(.vertical, 6)
        }
        .background(AureaColors.surface, in: RoundedRectangle(cornerRadius: 14))
        .clipShape(RoundedRectangle(cornerRadius: 14))
        .accessibilityIdentifier("mural.card")
    }

    private var sheet: some View {
        VStack(spacing: 12) {
            if let picked { Image(uiImage: picked.thumb).resizable().scaledToFit().frame(maxHeight: 220).clipShape(RoundedRectangle(cornerRadius: 12)) }
            TextField(AureaText.t("mural_caption"), text: $caption).textFieldStyle(.roundedBorder)
            if let stage { Text(stage).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted) }
            Button(postLabel) { Task { await send() } }.disabled(stage != nil)
        }.padding(20).presentationDetents([.medium])
    }

    private func start() async {
        await load(reset: true)
        guard let loaded = try? await api.call("/config", as: MuralConfig.self) else { return }
        config = loaded
        if loaded.posting { product = try? await Product.products(for: [loaded.productId]).first }
    }

    private func load(reset: Bool) async {
        failed = false
        let query = !reset ? (cursor.flatMap { $0.addingPercentEncoding(withAllowedCharacters: .alphanumerics) }.map { "?cursor=\($0)" } ?? "") : ""
        do {
            let page = try await api.call("/feed" + query, as: MuralPage.self)
            if reset { posts = page.items } else { posts += page.items }
            cursor = page.cursor
        } catch { failed = true }
    }

    private func startPost() {
        if config?.posting != true || product == nil { notice = AureaText.t("mural_unavailable") }
        else if conta.sessao() == nil { notice = AureaText.t("mural_sign_in") }
        else { photo = nil; picking = true }
    }

    private func open(_ item: PhotosPickerItem) async {
        stage = AureaText.t("mural_preparing")
        defer { stage = nil }
        guard let movie = try? await item.loadTransferable(type: MuralMovie.self), let video = await muralPrepare(movie.url) else {
            notice = AureaText.t("mural_failed_open"); return
        }
        if video.bytes > (config?.maxBytes ?? 0) { notice = AureaText.t("mural_too_big"); return }
        caption = ""
        picked = video
    }

    /// Transação paga e ainda não entregue: o app caiu antes de terminar.
    private func unfinished() async -> StoreKit.Transaction? {
        guard let id = config?.productId else { return nil }
        for await result in StoreKit.Transaction.unfinished {
            if case .verified(let transaction) = result, transaction.productID == id { return transaction }
        }
        return nil
    }

    private func buy() async throws -> StoreKit.Transaction? {
        if let open = await unfinished() { return open }
        guard let product else { return nil }
        switch try await product.purchase() {
        case .success(let result):
            if case .verified(let transaction) = result { return transaction }
            return nil
        case .pending:
            notice = AureaText.t("mural_pending"); return nil
        default:
            return nil
        }
    }

    private func send() async {
        guard let video = picked else { return }
        defer { stage = nil }
        do {
            // Crédito guardado: já pagou e a publicação caiu. Não cobra de novo.
            if savedCredit.isEmpty {
                stage = AureaText.t("mural_paying")
                guard let transaction = try await buy() else { return }
                let credit = try await api.call("/credits", method: "POST", body: ["platform": "apple", "token": String(transaction.id)], as: MuralId.self)
                savedCredit = credit.id
                await transaction.finish()
            }
            stage = AureaText.t("mural_uploading")
            let post = try await api.call("/posts", method: "POST", body: ["creditId": savedCredit, "caption": caption.trimmingCharacters(in: .whitespacesAndNewlines),
                "bytes": video.bytes, "width": video.width, "height": video.height, "durationMs": video.durationMs], as: MuralId.self)
            try await api.put("/posts/\(post.id)/video", file: video.file, type: "video/mp4")
            try await api.put("/posts/\(post.id)/thumb", file: video.thumbFile, type: "image/jpeg")
            _ = try await api.call("/posts/\(post.id)/publish", method: "POST", as: MuralId.self)
            savedCredit = ""
            picked = nil
            notice = AureaText.t("mural_done")
            await load(reset: true)
        } catch let failure as MuralFailure {
            if failure.code == "credit_used" { savedCredit = "" }
            notice = AureaText.t(failure.code == "profile_required" ? "mural_profile" : "mural_failed")
        } catch {
            notice = AureaText.t("mural_failed")
        }
    }
}
