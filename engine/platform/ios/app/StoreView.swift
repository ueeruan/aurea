import SwiftUI
import PhotosUI
import UniformTypeIdentifiers
import ImageIO

// 0.4.6 · Loja da Comunidade: plugins, efeitos e presets feitos por outras
// pessoas. Plugin baixado passa pela mesma folha de permissões do "Instalar
// plugin" (PluginRuntime.beginInstall); preset vai para Meus Presets. Plugin
// publicado só aparece depois da aprovação de um admin.
// Par do Android: community/StoreScreen.kt.

/// A loja só abre quando o servidor tem armazenamento (bucket R2
/// aurea-community-files). Até lá o botão mostra "Em breve". Par do Android:
/// `STORE_ENABLED` em StoreScreen.kt.
let storeEnabled = false

struct StoreAuthor: Codable { var id: String; var username: String; var name: String; var avatar: String?; var verification: String }
struct StorePermissions: Codable { var network: [String]; var clipboard: Bool; var files: Bool }
struct StoreItem: Codable, Identifiable {
    var id: String; var type: String; var format: String; var name: String; var description: String
    var tutorialUrl: String; var photos: Int; var pluginId: String?; var version: String; var presetKind: String?
    var permissions: StorePermissions; var hasScript: Bool; var downloads: Int; var likes: Int; var liked: Bool
    var bytes: Int64; var status: String; var reviewNote: String?; var mine: Bool; var author: StoreAuthor; var createdAt: Double
}
private struct StorePage: Decodable { var items: [StoreItem]; var cursor: String? }
private struct StoreItemReply: Decodable { var item: StoreItem }
private struct StoreLike: Decodable { var likes: Int; var liked: Bool }
private struct StoreOK: Decodable { var ok: Bool? }
private struct StoreAccount: Decodable { struct P: Decodable { var username: String }; var profile: P; var canVerify: Bool? }
struct StoreMeta: Decodable { var format: String?; var name: String?; var description: String? }
struct StoreFile: Decodable { var id: String; var role: String; var name: String; var bytes: Int64; var meta: StoreMeta? }

struct StoreFailure: Error {
    var code: String
    var message: String {
        let key: String
        switch code {
        case "storage_unavailable": key = "store_err_storage"
        case "invalid_plugin": key = "store_err_plugin"
        case "invalid_preset", "invalid_package": key = "store_err_package"
        case "invalid_image", "invalid_photo": key = "store_err_photo"
        case "plugin_id_taken": key = "store_err_taken"
        case "version_not_newer": key = "store_err_version"
        case "file_too_large": key = "social_file_limit"
        case "profile_required": key = "social_profile_required"
        case "rate_limited": key = "app_community_limit"
        case "unauthorized": key = "social_session_error"
        default: key = "social_error"
        }
        return AureaText.t(key)
    }
}

actor StoreAPI {
    static let root = "https://aurea-ai-discovery.aureaapp.workers.dev/api/community"
    let token: String
    init(token: String) { self.token = token }
    static func query(_ value: String) -> String { value.addingPercentEncoding(withAllowedCharacters: .alphanumerics) ?? "" }
    private func request(_ path: String, _ method: String) -> URLRequest {
        var r = URLRequest(url: URL(string: Self.root + path)!, timeoutInterval: 60)
        r.httpMethod = method; r.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        r.setValue("application/json", forHTTPHeaderField: "Accept")
        return r
    }
    private func decode<T: Decodable>(_ data: Data, _ response: URLResponse, as type: T.Type) throws -> T {
        guard data.count <= 1024 * 1024, let http = response as? HTTPURLResponse else { throw StoreFailure(code: "invalid_response") }
        guard (200...299).contains(http.statusCode) else {
            let body = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any]
            throw StoreFailure(code: body?["error"] as? String ?? "community_unavailable")
        }
        return try JSONDecoder().decode(type, from: data)
    }
    func call<T: Decodable>(_ path: String, method: String = "GET", body: [String: Any]? = nil, as type: T.Type) async throws -> T {
        var r = request(path, method)
        if let body { r.httpBody = try JSONSerialization.data(withJSONObject: body); r.setValue("application/json", forHTTPHeaderField: "Content-Type") }
        let (data, response) = try await ContaAPI.boundedData(for: r, limit: 1_048_576)
        try Task.checkCancellation()
        return try decode(data, response, as: type)
    }
    /// Pacote (.aureaplugin / preset .json) ou foto; o servidor confere o conteúdo.
    func upload(_ url: URL, role: String, name: String) async throws -> StoreFile {
        let bytes = (try url.resourceValues(forKeys: [.fileSizeKey])).fileSize ?? 0
        let limit = role == "photo" ? 3 * 1024 * 1024 : name.lowercased().hasSuffix(".json") ? 1024 * 1024 : 20 * 1024 * 1024
        guard (1...limit).contains(bytes) else { throw StoreFailure(code: "file_too_large") }
        var r = request("/store/files?role=\(role)&name=\(Self.query(name))", "POST")
        r.setValue("application/octet-stream", forHTTPHeaderField: "Content-Type"); r.setValue(String(bytes), forHTTPHeaderField: "Content-Length")
        let (data, response) = try await ContaAPI.session.upload(for: r, fromFile: url)
        return try decode(data, response, as: StoreFile.self)
    }
    func download(_ item: StoreItem) async throws -> URL {
        guard (1...20 * 1024 * 1024).contains(item.bytes), UUID(uuidString: item.id) != nil else { throw StoreFailure(code: "invalid_file") }
        let (file, response) = try await ContaAPI.session.download(for: request("/store/\(item.id)/package", "GET"))
        defer { try? FileManager.default.removeItem(at: file) }
        try Task.checkCancellation()
        guard (response as? HTTPURLResponse)?.statusCode == 200,
              (try file.resourceValues(forKeys: [.fileSizeKey])).fileSize == Int(item.bytes) else { throw StoreFailure(code: "invalid_file") }
        let target = FileManager.default.temporaryDirectory.appendingPathComponent("store-\(UUID().uuidString).\(item.format == "plugin" ? "aureaplugin" : "json")")
        try FileManager.default.moveItem(at: file, to: target)
        return target
    }
    func photo(_ id: String, _ index: Int, maxSide: CGFloat) async -> UIImage? {
        guard UUID(uuidString: id) != nil, (0...3).contains(index),
              let reply = try? await ContaAPI.boundedData(for: request("/store/\(id)/photos/\(index)", "GET"), limit: 3 * 1024 * 1024),
              (reply.1 as? HTTPURLResponse)?.statusCode == 200,
              let source = CGImageSourceCreateWithData(reply.0 as CFData, nil) else { return nil }
        let options: [CFString: Any] = [kCGImageSourceCreateThumbnailFromImageAlways: true, kCGImageSourceCreateThumbnailWithTransform: true,
                                        kCGImageSourceThumbnailMaxPixelSize: maxSide]
        return CGImageSourceCreateThumbnailAtIndex(source, 0, options as CFDictionary).map { UIImage(cgImage: $0) }
    }
}

private func storeTypeLabel(_ type: String) -> String {
    AureaText.t(type == "plugin" ? "store_type_plugin" : type == "effect" ? "store_type_effect" : "store_type_preset")
}
private func storeStatusSuffix(_ item: StoreItem) -> String {
    item.status == "pending" ? " · " + AureaText.t("store_status_pending") : item.status == "rejected" ? " · " + AureaText.t("store_status_rejected") : ""
}

private struct StorePhotoView: View {
    let api: StoreAPI; let item: StoreItem; let index: Int; var maxSide: CGFloat = 720
    @State private var image: UIImage?
    var body: some View {
        ZStack {
            AureaColors.lightSurface
            if let image { Image(uiImage: image).resizable().scaledToFill() }
            else { Text(storeTypeLabel(item.type)).font(.caption).foregroundStyle(AureaColors.lightTextMuted) }
        }.clipShape(RoundedRectangle(cornerRadius: 16))
            .task(id: "\(item.id)-\(index)") { if index < item.photos { image = await api.photo(item.id, index, maxSide: maxSide) } }
    }
}

/// Salva um preset baixado em Meus Presets, sem sobrescrever um de mesmo nome.
func storeSavePreset(_ data: Data, fallbackName: String) throws {
    guard let value = try JSONSerialization.jsonObject(with: data) as? [String: Any], value["aurea_preset"] as? Int == 1,
          let raw = value["kind"] as? String,
          let kind = ["effects": PanelPresetKind.effects, "text": .text, "animation": .animation, "caption": .caption, "curve": .curve][raw]
    else { throw StoreFailure(code: "invalid_preset") }
    try FileManager.default.createDirectory(at: kind.directory, withIntermediateDirectories: true)
    let name = (value["name"] as? String).flatMap { $0.isEmpty ? nil : $0 } ?? fallbackName
    var target = kind.directory.appendingPathComponent(PanelPresetKind.fileName(name)), index = 2
    while FileManager.default.fileExists(atPath: target.path) { target = kind.directory.appendingPathComponent(PanelPresetKind.fileName("\(name) (\(index))")); index += 1 }
    try data.write(to: target, options: .atomic)
}

struct StoreView: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var conta: ContaModel
    @Environment(\.dismiss) private var dismiss
    @State private var api = StoreAPI(token: "")
    @State private var canReview = false
    @State private var hasProfile = false
    @State private var type = ""
    @State private var popular = false
    @State private var scope = "all"
    @State private var query = ""
    @State private var items: [StoreItem] = []
    @State private var cursor: String?
    @State private var busy = false
    @State private var error: String?
    @State private var open: StoreItem?
    @State private var publishing = false

    private func run(_ action: @escaping () async throws -> Void) {
        guard !busy else { return }
        busy = true; error = nil
        Task { @MainActor in
            defer { busy = false }
            do { try await action() } catch { if !Task.isCancelled { self.error = (error as? StoreFailure)?.message ?? AureaText.t("social_error") } }
        }
    }
    private func load(more: Bool = false) async throws {
        var q = "?sort=\(popular ? "popular" : "new")"
        if !type.isEmpty { q += "&type=\(type)" }
        let trimmed = query.trimmingCharacters(in: .whitespaces)
        if !trimmed.isEmpty { q += "&q=\(StoreAPI.query(trimmed))" }
        if scope == "mine" { q += "&mine=1" } else if scope == "pending" { q += "&status=pending" }
        if more, let cursor { q += "&cursor=\(StoreAPI.query(cursor))" }
        let page = try await api.call("/store\(q)", as: StorePage.self)
        items = more ? items + page.items.filter { i in !items.contains { $0.id == i.id } } : page.items
        cursor = page.cursor
    }
    private func chip(_ label: String, _ on: Bool, id: String, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(label).font(.aurea(size: 13, weight: on ? .semibold : .regular)).padding(.horizontal, 14).frame(minHeight: 36)
                .background(on ? AureaColors.lightAccentInk.opacity(0.15) : AureaColors.lightSurface, in: Capsule())
        }.buttonStyle(.plain).accessibilityAddTraits(on ? .isSelected : []).accessibilityIdentifier(id)
    }

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Button(AureaText.t("editor_fechar")) { dismiss() }.frame(minHeight: 44).accessibilityIdentifier("store.close")
                Text(AureaText.t("store_title")).font(.aurea(size: 21, weight: .bold)).frame(maxWidth: .infinity, alignment: .leading)
                Button(AureaText.t("store_publish")) { if hasProfile { publishing = true } else { error = AureaText.t("social_profile_required") } }
                    .buttonStyle(.borderedProminent).tint(AureaColors.accent).foregroundStyle(AureaColors.onAccent).disabled(busy)
                    .accessibilityIdentifier("store.publish")
            }.padding(.horizontal, 16).frame(minHeight: 52)
            TextField(AureaText.t("store_search"), text: $query).textInputAutocapitalization(.never).autocorrectionDisabled()
                .textFieldStyle(.roundedBorder).padding(.horizontal, 16).accessibilityIdentifier("store.search")
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 8) {
                    ForEach([("", "store_all"), ("plugin", "store_plugins"), ("effect", "store_effects"), ("preset", "store_presets")], id: \.0) { value in
                        chip(AureaText.t(value.1), type == value.0, id: "store.type.\(value.0.isEmpty ? "all" : value.0)") { type = value.0 }
                    }
                    chip(AureaText.t("store_popular"), popular, id: "store.popular") { popular.toggle() }
                    chip(AureaText.t("store_mine"), scope == "mine", id: "store.mine") { scope = scope == "mine" ? "all" : "mine" }
                    if canReview { chip(AureaText.t("store_pending"), scope == "pending", id: "store.pending") { scope = scope == "pending" ? "all" : "pending" } }
                }.padding(.horizontal, 16).padding(.vertical, 6)
            }
            if let error { Text(error).font(.subheadline).foregroundStyle(AureaColors.lightDanger).padding(.horizontal, 16).accessibilityIdentifier("store.error") }
            ScrollView {
                LazyVStack(spacing: 12) {
                    if busy && items.isEmpty { ProgressView().padding(40) }
                    else if items.isEmpty { Text(AureaText.t("store_empty")).foregroundStyle(AureaColors.lightTextMuted).padding(.vertical, 24) }
                    ForEach(items) { item in
                        Button { open = item } label: { card(item) }.buttonStyle(.plain)
                            .accessibilityLabel(AureaText.t("store_card_a11y", item.name, storeTypeLabel(item.type), item.author.username))
                            .accessibilityIdentifier("store.item.\(item.id)")
                    }
                    if cursor != nil { Button(AureaText.t("social_more")) { run { try await load(more: true) } }.frame(minHeight: 44).disabled(busy) }
                }.padding(16)
            }.accessibilityIdentifier("store.list")
        }
        .background(AureaColors.lightBackground.ignoresSafeArea()).foregroundStyle(AureaColors.lightText).tint(AureaColors.lightAccentInk)
        .accessibilityIdentifier("store.screen")
        .task {
            api = StoreAPI(token: conta.sessao()?.token ?? "")
            if let account = try? await api.call("/me", as: StoreAccount.self) {
                canReview = account.canVerify == true; hasProfile = !account.profile.username.isEmpty
            }
        }
        // Filtros mudam na hora; a busca espera a pessoa parar de digitar.
        .task(id: "\(type)|\(popular)|\(scope)|\(query)") {
            if !query.isEmpty { try? await Task.sleep(nanoseconds: 350_000_000) }
            if Task.isCancelled { return }
            while busy { try? await Task.sleep(nanoseconds: 50_000_000) }
            run { try await load() }
        }
        .sheet(item: $open) { item in
            StoreDetailView(api: api, item: item, canReview: canReview, onChanged: { changed in
                open = changed; if let i = items.firstIndex(where: { $0.id == changed.id }) { items[i] = changed }
            }, onRemoved: { open = nil; items.removeAll { $0.id == item.id } }).environmentObject(model)
        }
        .sheet(isPresented: $publishing) {
            StorePublishView(api: api) { created in
                publishing = false; scope = "mine"
                items = [created] + items.filter { $0.id != created.id }
                model.toast = AureaText.t(created.status == "pending" ? "store_sent_review" : "store_published")
            }
        }
        .background(PluginOverlays())
    }

    private func card(_ item: StoreItem) -> some View {
        HStack(spacing: 12) {
            StorePhotoView(api: api, item: item, index: 0, maxSide: 256).frame(width: 88, height: 88)
            VStack(alignment: .leading, spacing: 4) {
                Text(item.name).font(.aurea(size: 15, weight: .semibold)).lineLimit(1)
                Text("@\(item.author.username) · \(storeTypeLabel(item.type))").font(.caption).foregroundStyle(AureaColors.lightTextMuted).lineLimit(1)
                if !item.description.isEmpty { Text(item.description).font(.caption).lineLimit(2) }
                Text(AureaText.t("store_stats", item.downloads, item.likes) + storeStatusSuffix(item)).font(.caption2).foregroundStyle(AureaColors.lightTextMuted)
            }
            Spacer(minLength: 0)
        }.padding(12).background(AureaColors.lightSurface, in: RoundedRectangle(cornerRadius: 20))
    }
}

private let storeReportReasons = ["spam", "nudity", "violence", "hate", "harassment", "illegal", "copyright", "other"]

struct StoreDetailView: View {
    @EnvironmentObject private var model: AureaModel
    @Environment(\.openURL) private var openURL
    let api: StoreAPI
    @State var item: StoreItem
    let canReview: Bool
    let onChanged: (StoreItem) -> Void
    let onRemoved: () -> Void
    @State private var busy = false
    @State private var error: String?
    @State private var reporting = false
    @State private var confirmDelete = false
    @State private var note = ""

    private func run(_ action: @escaping () async throws -> Void) {
        guard !busy else { return }
        busy = true; error = nil
        Task { @MainActor in
            defer { busy = false }
            do { try await action() } catch { if !Task.isCancelled { self.error = (error as? StoreFailure)?.message ?? AureaText.t("social_error") } }
        }
    }
    private func changed(_ value: StoreItem) { item = value; onChanged(value) }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                if item.photos > 0 {
                    ScrollView(.horizontal, showsIndicators: false) {
                        HStack(spacing: 10) {
                            ForEach(0..<item.photos, id: \.self) { i in
                                StorePhotoView(api: api, item: item, index: i).frame(width: 260, height: 325)
                                    .accessibilityLabel(AureaText.t("store_photo_a11y", i + 1, item.photos))
                            }
                        }
                    }
                }
                Text(item.name).font(.aurea(size: 21, weight: .bold))
                Text("@\(item.author.username) · \(storeTypeLabel(item.type))" + (item.version.isEmpty ? "" : " · v\(item.version)"))
                    .font(.subheadline).foregroundStyle(AureaColors.lightTextMuted)
                Text(AureaText.t("store_stats", item.downloads, item.likes) + storeStatusSuffix(item)).font(.subheadline).foregroundStyle(AureaColors.lightTextMuted)
                if let note = item.reviewNote, !note.isEmpty, item.status != "approved" {
                    Text(AureaText.t("store_review_note", note)).font(.subheadline).foregroundStyle(AureaColors.lightDanger)
                }
                if !item.description.isEmpty { Text(item.description) }
                if item.format == "plugin" {
                    // O que o plugin pede (o aparelho pergunta de novo na instalação).
                    let needs = [item.hasScript ? AureaText.t("store_perm_script") : nil,
                                 item.permissions.network.isEmpty ? nil : AureaText.t("store_perm_network", item.permissions.network.joined(separator: ", ")),
                                 item.permissions.clipboard ? AureaText.t("store_perm_clipboard") : nil,
                                 item.permissions.files ? AureaText.t("store_perm_files") : nil].compactMap { $0 }
                    Text(needs.isEmpty ? AureaText.t("store_perm_none") : needs.joined(separator: "\n"))
                        .font(.subheadline).foregroundStyle(AureaColors.lightTextMuted).accessibilityIdentifier("store.permissions")
                }
                if item.tutorialUrl.hasPrefix("https://"), let url = URL(string: item.tutorialUrl) {
                    Button(AureaText.t("store_tutorial")) { openURL(url) }.buttonStyle(.bordered).frame(maxWidth: .infinity, minHeight: 48)
                        .accessibilityIdentifier("store.tutorial")
                }
                Button {
                    run {
                        let file = try await api.download(item)
                        if item.format == "plugin" {
                            // Mesmo caminho do arquivo local: lê o pacote e abre o consentimento.
                            PluginRuntime.shared.beginInstall(file)
                        } else {
                            defer { try? FileManager.default.removeItem(at: file) }
                            try storeSavePreset(try Data(contentsOf: file), fallbackName: item.name)
                            model.toast = AureaText.t("social_saved")
                        }
                        var updated = item; if !item.mine { updated.downloads += 1 }; changed(updated)
                    }
                } label: {
                    Text(AureaText.t(item.format == "plugin" ? "store_install" : "store_get_preset")).frame(maxWidth: .infinity, minHeight: 52)
                }.buttonStyle(.borderedProminent).tint(AureaColors.accent).foregroundStyle(AureaColors.onAccent)
                    .disabled(busy || !(item.status == "approved" || item.mine || canReview)).accessibilityIdentifier("store.install")
                HStack(spacing: 8) {
                    if item.status == "approved" {
                        Button(AureaText.t(item.liked ? "store_unlike" : "store_like")) { run {
                            let r = try await api.call("/store/\(item.id)/like", method: item.liked ? "DELETE" : "PUT", as: StoreLike.self)
                            var updated = item; updated.likes = r.likes; updated.liked = r.liked; changed(updated)
                        } }.buttonStyle(.bordered).frame(maxWidth: .infinity, minHeight: 48).accessibilityIdentifier("store.like")
                    }
                    if !item.mine { Button(AureaText.t("store_report")) { reporting = true }.buttonStyle(.bordered).frame(maxWidth: .infinity, minHeight: 48)
                        .accessibilityIdentifier("store.report") }
                    if item.mine || canReview { Button(AureaText.t("store_delete"), role: .destructive) { confirmDelete = true }.buttonStyle(.bordered)
                        .frame(maxWidth: .infinity, minHeight: 48).accessibilityIdentifier("store.delete") }
                }.disabled(busy)
                if canReview {
                    // Revisão (admin): aprovar ou recusar com um motivo que o criador vê.
                    TextField(AureaText.t("store_review_reason"), text: $note).textFieldStyle(.roundedBorder)
                    HStack(spacing: 8) {
                        ForEach([("approved", "store_approve"), ("rejected", "store_reject")], id: \.0) { pair in
                            Button(AureaText.t(pair.1)) { run {
                                changed(try await api.call("/store/\(item.id)/review", method: "PUT", body: ["status": pair.0, "note": String(note.prefix(500))], as: StoreItemReply.self).item)
                            } }.buttonStyle(.borderedProminent).frame(maxWidth: .infinity, minHeight: 48).disabled(busy || item.status == pair.0)
                                .accessibilityIdentifier("store.review.\(pair.0)")
                        }
                    }
                }
                if let error { Text(error).font(.subheadline).foregroundStyle(AureaColors.lightDanger) }
            }.padding(20)
        }
        .background(AureaColors.lightBackground.ignoresSafeArea()).foregroundStyle(AureaColors.lightText).tint(AureaColors.lightAccentInk)
        .accessibilityIdentifier("store.detail")
        .presentationDetents([.large])
        .confirmationDialog(AureaText.t("mod_report_title"), isPresented: $reporting, titleVisibility: .visible) {
            ForEach(storeReportReasons, id: \.self) { reason in
                Button(AureaText.t("mod_reason_\(reason)")) { run {
                    _ = try await api.call("/reports", method: "POST", body: ["kind": "store", "id": item.id, "reason": reason], as: StoreOK.self)
                    model.toast = AureaText.t("mod_report_thanks")
                } }
            }
        }
        .alert(AureaText.t("store_delete_title"), isPresented: $confirmDelete) {
            Button(AureaText.t("store_delete"), role: .destructive) { run {
                _ = try await api.call("/store/\(item.id)", method: "DELETE", as: StoreOK.self); onRemoved()
            } }
            Button(AureaText.t("editor_cancelar"), role: .cancel) {}
        } message: { Text(AureaText.t("store_delete_message")) }
    }
}

/// Regrava uma foto em JPEG de até 2048 px (o servidor aceita até 3 MB).
private func storePreparePhoto(_ data: Data) -> URL? {
    guard let source = CGImageSourceCreateWithData(data as CFData, nil) else { return nil }
    let options: [CFString: Any] = [kCGImageSourceCreateThumbnailFromImageAlways: true, kCGImageSourceCreateThumbnailWithTransform: true,
                                    kCGImageSourceThumbnailMaxPixelSize: 2048]
    guard let cg = CGImageSourceCreateThumbnailAtIndex(source, 0, options as CFDictionary) else { return nil }
    let image = UIImage(cgImage: cg)
    for quality in stride(from: 0.88, through: 0.5, by: -0.12) {
        guard let jpeg = image.jpegData(compressionQuality: CGFloat(quality)) else { return nil }
        if jpeg.count <= 3 * 1024 * 1024 || quality <= 0.52 {
            let url = FileManager.default.temporaryDirectory.appendingPathComponent("store-photo-\(UUID().uuidString).jpg")
            guard (try? jpeg.write(to: url)) != nil, jpeg.count <= 3 * 1024 * 1024 else { return nil }
            return url
        }
    }
    return nil
}

struct StorePublishView: View {
    let api: StoreAPI
    let onDone: (StoreItem) -> Void
    @State private var package: StoreFile?
    @State private var photos: [String] = []
    @State private var picked: [PhotosPickerItem] = []
    @State private var name = ""
    @State private var description = ""
    @State private var tutorial = ""
    @State private var choosing = false
    @State private var busy = false
    @State private var error: String?

    private func run(_ action: @escaping () async throws -> Void) {
        guard !busy else { return }
        busy = true; error = nil
        Task { @MainActor in
            defer { busy = false }
            do { try await action() } catch { if !Task.isCancelled { self.error = (error as? StoreFailure)?.message ?? AureaText.t("social_error") } }
        }
    }
    private var tutorialOK: Bool { tutorial.isEmpty || tutorial.hasPrefix("https://") }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                Text(AureaText.t("store_publish_title")).font(.aurea(size: 21, weight: .bold))
                Text(AureaText.t("store_publish_hint")).font(.subheadline).foregroundStyle(AureaColors.lightTextMuted)
                Button(package.map { AureaText.t("store_package_ready", $0.name) } ?? AureaText.t("store_pick_package")) { choosing = true }
                    .buttonStyle(.bordered).frame(maxWidth: .infinity, minHeight: 48).disabled(busy).accessibilityIdentifier("store.publish.package")
                if package?.meta?.format == "plugin" {
                    Text(AureaText.t("store_plugin_review_note")).font(.subheadline).foregroundStyle(AureaColors.lightTextMuted)
                }
                TextField(AureaText.t("store_field_name"), text: $name).textFieldStyle(.roundedBorder).accessibilityIdentifier("store.publish.name")
                    .onChange(of: name) { value in if value.count > 64 { name = String(value.prefix(64)) } }
                TextField(AureaText.t("store_field_description"), text: $description, axis: .vertical).lineLimit(3...8)
                    .textFieldStyle(.roundedBorder).accessibilityIdentifier("store.publish.description")
                    .onChange(of: description) { value in if value.count > 2000 { description = String(value.prefix(2000)) } }
                TextField(AureaText.t("store_field_tutorial"), text: $tutorial).textInputAutocapitalization(.never).autocorrectionDisabled()
                    .keyboardType(.URL).textFieldStyle(.roundedBorder).accessibilityIdentifier("store.publish.tutorial")
                Text(AureaText.t("store_field_tutorial_hint")).font(.caption).foregroundStyle(tutorialOK ? AureaColors.lightTextMuted : AureaColors.lightDanger)
                PhotosPicker(selection: $picked, maxSelectionCount: max(1, 4 - photos.count), matching: .images) {
                    Text(AureaText.t("store_add_photos", photos.count)).frame(maxWidth: .infinity, minHeight: 48)
                }.buttonStyle(.bordered).disabled(busy || photos.count >= 4).accessibilityIdentifier("store.publish.photos")
                if !photos.isEmpty { Button(AureaText.t("store_clear_photos")) { photos = [] }.frame(minHeight: 44).disabled(busy) }
                if let error { Text(error).font(.subheadline).foregroundStyle(AureaColors.lightDanger) }
                Button {
                    guard let package else { return }
                    run {
                        let body: [String: Any] = ["package": package.id, "name": name.trimmingCharacters(in: .whitespaces),
                                                   "description": description.trimmingCharacters(in: .whitespacesAndNewlines),
                                                   "tutorialUrl": tutorial.trimmingCharacters(in: .whitespaces), "photos": photos]
                        onDone(try await api.call("/store", method: "POST", body: body, as: StoreItemReply.self).item)
                    }
                } label: {
                    Group { if busy { ProgressView() } else { Text(AureaText.t("store_publish_send")) } }.frame(maxWidth: .infinity, minHeight: 52)
                }.buttonStyle(.borderedProminent).tint(AureaColors.accent).foregroundStyle(AureaColors.onAccent)
                    .disabled(busy || package == nil || name.trimmingCharacters(in: .whitespaces).isEmpty || !tutorialOK)
                    .accessibilityIdentifier("store.publish.send")
            }.padding(20)
        }
        .background(AureaColors.lightBackground.ignoresSafeArea()).foregroundStyle(AureaColors.lightText).tint(AureaColors.lightAccentInk)
        .accessibilityIdentifier("store.publish.sheet")
        .fileImporter(isPresented: $choosing, allowedContentTypes: [.data, .json, .zip]) { result in
            guard case .success(let url) = result else { return }
            run {
                let scoped = url.startAccessingSecurityScopedResource()
                defer { if scoped { url.stopAccessingSecurityScopedResource() } }
                var fileName = url.lastPathComponent
                if !fileName.lowercased().hasSuffix(".json") && !fileName.lowercased().hasSuffix(".aureaplugin") { fileName += ".aureaplugin" }
                let temp = FileManager.default.temporaryDirectory.appendingPathComponent("store-up-\(UUID().uuidString)")
                try FileManager.default.copyItem(at: url, to: temp)
                defer { try? FileManager.default.removeItem(at: temp) }
                let file = try await api.upload(temp, role: "package", name: fileName)
                package = file
                if name.isEmpty { name = String((file.meta?.name ?? "").prefix(64)) }
                if description.isEmpty { description = file.meta?.description ?? "" }
            }
        }
        .onChange(of: picked) { items in
            guard !items.isEmpty else { return }
            let chosen = items; picked = []
            run {
                for item in chosen.prefix(4 - photos.count) {
                    guard let data = try await item.loadTransferable(type: Data.self), let url = storePreparePhoto(data) else { throw StoreFailure(code: "invalid_image") }
                    defer { try? FileManager.default.removeItem(at: url) }
                    photos.append(try await api.upload(url, role: "photo", name: "foto.jpg").id)
                }
            }
        }
    }
}
