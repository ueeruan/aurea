import SwiftUI
import PhotosUI
import UniformTypeIdentifiers
import ImageIO

private struct SocialProfile: Codable, Identifiable {
    var id: String
    var username: String
    var name: String
    var bio: String?
    var avatar: String?
    var verification: String
    var followers: Int?
    var following: Int?
    var posts: Int?
    var followed: Bool?
}
private struct SocialAsset: Codable, Identifiable { var id: String; var kind: String; var name: String; var bytes: Int64 }
private struct SocialPost: Codable, Identifiable {
    var id: String; var body: String; var createdAt: Double; var author: SocialProfile
    var asset: SocialAsset?; var likes: Int; var comments: Int; var liked: Bool
}
private struct SocialComment: Codable, Identifiable { var id: String; var body: String; var createdAt: Double; var author: SocialProfile }
private struct SocialPage<T: Decodable>: Decodable { var items: [T]; var cursor: String? }
private struct SocialAccount: Decodable { var profile: SocialProfile; var canVerify: Bool? }
private struct SocialLike: Decodable { var likes: Int; var liked: Bool }
private struct SocialDone: Decodable { var id: String?; var ok: Bool? }
private struct SocialFailure: Error {
    var code: String
    var message: String { AureaText.t(code == "username_taken" ? "social_username_taken" : code == "profile_required" ? "social_profile_required" : code == "file_too_large" ? "social_file_limit" : code == "invalid_profile" ? "social_profile_hint" : code == "unauthorized" ? "social_session_error" : "social_error") }
}

private actor SocialAPI {
    static let root = URL(string: "https://aurea-ai-discovery.aureaapp.workers.dev/api/community")!
    let token: String
    init(token: String) { self.token = token }
    static func query(_ value: String) -> String { value.addingPercentEncoding(withAllowedCharacters: .alphanumerics) ?? "" }
    static func assetURL(_ id: String) -> URL? { UUID(uuidString: id) == nil ? nil : root.appendingPathComponent("assets/\(id)") }
    private func request(_ path: String, _ method: String) -> URLRequest {
        var r = URLRequest(url: URL(string: Self.root.absoluteString + path)!, timeoutInterval: 60)
        r.httpMethod = method; r.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        r.setValue("application/json", forHTTPHeaderField: "Accept")
        return r
    }
    private func decode<T: Decodable>(_ data: Data, _ response: URLResponse, as type: T.Type) throws -> T {
        guard data.count <= 1024 * 1024, let http = response as? HTTPURLResponse else { throw SocialFailure(code: "invalid_response") }
        guard (200...299).contains(http.statusCode) else {
            let body = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any]
            throw SocialFailure(code: body?["error"] as? String ?? "community_unavailable")
        }
        return try JSONDecoder().decode(type, from: data)
    }
    func call<T: Decodable>(_ path: String, method: String = "GET", body: [String: Any]? = nil, as type: T.Type) async throws -> T {
        var r = request(path, method)
        if let body { r.httpBody = try JSONSerialization.data(withJSONObject: body); r.setValue("application/json", forHTTPHeaderField: "Content-Type") }
        let (data, response) = try await ContaAPI.boundedData(for: r, limit: 1_048_576)
        return try decode(data, response, as: type)
    }
    func upload(_ url: URL, kind: String) async throws -> SocialAsset {
        let bytes = (try url.resourceValues(forKeys: [.fileSizeKey])).fileSize ?? 0
        let limit = kind == "avatar" ? 512 * 1024 : kind == "preset" ? 1024 * 1024 : 50 * 1024 * 1024
        guard (1...limit).contains(bytes) else { throw SocialFailure(code: "file_too_large") }
        var r = request("/assets?kind=\(kind)&name=\(Self.query(url.lastPathComponent))", "POST")
        r.setValue("application/octet-stream", forHTTPHeaderField: "Content-Type"); r.setValue(String(bytes), forHTTPHeaderField: "Content-Length")
        let (data, response) = try await ContaAPI.session.upload(for: r, fromFile: url)
        return try decode(data, response, as: SocialAsset.self)
    }
    func download(_ asset: SocialAsset) async throws -> URL {
        guard (1...50 * 1024 * 1024).contains(asset.bytes), UUID(uuidString: asset.id) != nil else { throw SocialFailure(code: "invalid_file") }
        let (file, response) = try await ContaAPI.session.download(for: request("/assets/\(asset.id)", "GET"))
        guard (response as? HTTPURLResponse)?.statusCode == 200,
              (try file.resourceValues(forKeys: [.fileSizeKey])).fileSize == Int(asset.bytes) else {
            try? FileManager.default.removeItem(at: file); throw SocialFailure(code: "invalid_file")
        }
        let target = FileManager.default.temporaryDirectory.appendingPathComponent("community-\(UUID().uuidString).\(asset.kind == "project" ? "aureaproj" : "json")")
        try FileManager.default.moveItem(at: file, to: target)
        return target
    }
}

@MainActor
private final class SocialState: ObservableObject {
    @Published var me: SocialProfile?
    @Published var profile: SocialProfile?
    @Published var canVerify = false
    @Published var feed: [SocialPost] = []
    @Published var cursor: String?
    @Published var following = false
    @Published var busy = false
    @Published var error: String?
    var api = SocialAPI(token: "")
    var ownMode = false
    func fail(_ error: Error) { self.error = (error as? SocialFailure)?.message ?? AureaText.t("social_error") }
    func run(_ action: @escaping () async throws -> Void) {
        guard !busy else { return }
        busy = true; error = nil
        Task { defer { busy = false }; do { try await action() } catch { if !Task.isCancelled { fail(error) } } }
    }
    func start(_ session: ContaSessao?, own: Bool) async {
        api = SocialAPI(token: session?.token ?? ""); ownMode = own; profile = nil; feed = []; busy = true; error = nil
        defer { busy = false }
        do { try await refresh() } catch { if !Task.isCancelled { fail(error) } }
    }
    func refresh() async throws {
        let account = try await api.call("/me", as: SocialAccount.self)
        me = account.profile; canVerify = account.canVerify == true
        if (ownMode && profile == nil) || profile?.id == me?.id { profile = me }
        try await loadFeed()
    }
    func loadFeed(more: Bool = false) async throws {
        let suffix = "?following=\(following && profile == nil ? 1 : 0)" + (profile.map { "&user=\($0.id)" } ?? "") + (more ? cursor.map { "&cursor=\(SocialAPI.query($0))" } ?? "" : "")
        let result = try await api.call("/posts\(suffix)", as: SocialPage<SocialPost>.self)
        feed = more ? feed + result.items.filter { item in !feed.contains { $0.id == item.id } } : result.items
        cursor = result.cursor
    }
    func open(_ id: String) async throws {
        profile = try await api.call("/profiles/\(id)", as: SocialAccount.self).profile
        try await loadFeed()
    }
    func follow(_ p: SocialProfile) async throws {
        profile = try await api.call("/profiles/\(p.id)/follow", method: p.followed == true ? "DELETE" : "PUT", as: SocialAccount.self).profile
    }
    func like(_ post: SocialPost) async throws {
        let result = try await api.call("/posts/\(post.id)/like", method: post.liked ? "DELETE" : "PUT", as: SocialLike.self)
        if let i = feed.firstIndex(where: { $0.id == post.id }) { feed[i].liked = result.liked; feed[i].likes = result.likes }
    }
}

private let socialBadges = [("blue", "social_badge_blue"), ("green", "social_badge_green"), ("gold", "social_badge_gold"), ("", "social_badge_none")]
private struct SocialBadge: View {
    let value: String
    var body: some View {
        if let option = socialBadges.first(where: { $0.0 == value && !value.isEmpty }) {
            CupertinoGlyph.text(CupertinoGlyph.CheckmarkSeal, size: 19,
                color: value == "blue" ? Color(red: 0.44, green: 0.72, blue: 1) : value == "green" ? Color(red: 0.42, green: 0.86, blue: 0.67) : Color(red: 1, green: 0.82, blue: 0.42))
                .frame(width: 22, height: 22).accessibilityLabel(AureaText.t(option.1))
        }
    }
}
private struct SocialAuthor: View {
    let profile: SocialProfile
    var large = false
    var body: some View {
        HStack(spacing: 12) {
            ZStack {
                Circle().fill(AureaColors.accent.opacity(0.14))
                Text(String((profile.name.isEmpty ? profile.username : profile.name).prefix(1)).uppercased())
                    .font(.system(size: large ? 26 : 18, weight: .bold)).foregroundStyle(AureaColors.accent)
                if let id = profile.avatar, let url = SocialAPI.assetURL(id) {
                    AsyncImage(url: url) { image in image.resizable().scaledToFill() } placeholder: { Color.clear }
                }
            }.frame(width: large ? 64 : 44, height: large ? 64 : 44).clipShape(Circle()).accessibilityHidden(true)
            VStack(alignment: .leading, spacing: 3) {
                HStack(spacing: 4) { Text(profile.name.isEmpty ? AureaText.t("social_profile") : profile.name).font(.body.weight(.semibold)); SocialBadge(value: profile.verification) }
                Text(profile.username.isEmpty ? AureaText.t("social_profile_required") : "@\(profile.username)").font(.caption).foregroundStyle(AureaColors.muted)
            }
            Spacer(minLength: 0)
        }.frame(minHeight: 48).foregroundStyle(AureaColors.text).contentShape(Rectangle())
    }
}

@MainActor
struct CommunityView: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var conta: ContaModel
    let profileMode: Bool
    @StateObject private var state = SocialState()
    @State private var search = ""
    @State private var results: [SocialProfile] = []
    @State private var editing = false
    @State private var composing = false
    @State private var legacy = false
    @State private var verifying = false
    @State private var deleting: SocialPost?
    @State private var comments: SocialPost?
    @State private var people: String?
    var body: some View {
        VStack(spacing: 0) {
            HStack {
                if state.profile != nil && (!profileMode || state.profile?.id != state.me?.id) {
                    Button { state.run { state.profile = profileMode ? state.me : nil; try await state.loadFeed() } } label: { Image(systemName: "chevron.left").frame(width: 44, height: 44) }.accessibilityLabel(AureaText.t("common_back"))
                }
                Text(AureaText.t(state.profile == nil ? "social_community" : "social_profile")).font(.title2.bold())
                Spacer()
                Button(AureaText.t("social_refresh")) { state.run { try await state.refresh() } }.frame(minHeight: 44)
            }.padding(.horizontal, 20).disabled(state.busy)
            if state.busy { ProgressView().frame(maxWidth: .infinity).padding(4) }
            if let error = state.error { Text(error).font(.subheadline).foregroundStyle(AureaColors.danger).padding(.horizontal, 20).accessibilityIdentifier("social.error") }
            ScrollView {
                LazyVStack(spacing: 12) {
                    if let profile = state.profile { profileHeader(profile) }
                    else { feedHeader }
                    if state.profile == nil || state.profile?.id == state.me?.id {
                        Button { if state.me?.username.isEmpty != false { editing = true } else { composing = true } } label: {
                            Text(AureaText.t("social_create_post")).frame(maxWidth: .infinity, minHeight: 48)
                        }.buttonStyle(.borderedProminent).tint(AureaColors.accent).disabled(state.me == nil || state.busy).accessibilityIdentifier("social.compose")
                    }
                    if state.feed.isEmpty && !state.busy { Text(AureaText.t("social_empty")).foregroundStyle(AureaColors.muted).padding(.vertical, 24) }
                    ForEach(state.feed) { post in postCard(post) }
                    if state.cursor != nil { Button(AureaText.t("social_more")) { state.run { try await state.loadFeed(more: true) } }.frame(minHeight: 44).disabled(state.busy) }
                }.padding(16)
            }.accessibilityIdentifier("social.feed")
        }.background(AureaColors.background).foregroundStyle(AureaColors.text).tint(AureaColors.accent)
            .accessibilityIdentifier("social.screen")
            .task(id: profileMode) { await state.start(conta.sessao(), own: profileMode) }
            .task(id: search) {
                if search.trimmingCharacters(in: .whitespaces).count < 2 { results = []; return }
                do {
                    try await Task.sleep(nanoseconds: 300_000_000)
                    let r = try await state.api.call("/profiles?q=\(SocialAPI.query(search.replacingOccurrences(of: "@", with: "")))", as: SocialPage<SocialProfile>.self)
                    try Task.checkCancellation(); results = r.items
                } catch { if !Task.isCancelled { state.fail(error) } }
            }
            .sheet(isPresented: $editing) {
                if let me = state.me { SocialEditProfile(profile: me, api: state.api) { p in state.me = p; if profileMode || state.profile?.id == p.id { state.profile = p }; editing = false } }
            }
            .sheet(isPresented: $composing) { SocialCompose(api: state.api) { composing = false; state.run { try await state.refresh() } }.environmentObject(model) }
            .sheet(item: $comments, onDismiss: { state.run { try await state.loadFeed() } }) { post in
                SocialComments(post: post, ownID: state.me?.id, api: state.api) { id in comments = nil; state.run { try await state.open(id) } }
            }
            .sheet(isPresented: $legacy) { HomeCommunityPresets().padding(.top, 24) }
            .sheet(isPresented: Binding(get: { people != nil }, set: { if !$0 { people = nil } })) {
                if let type = people, let p = state.profile { SocialPeople(profileID: p.id, type: type, api: state.api) { id in people = nil; state.run { try await state.open(id) } } }
            }
            .confirmationDialog(AureaText.t("social_verification"), isPresented: $verifying, titleVisibility: .visible) {
                ForEach(socialBadges, id: \.0) { badge in Button(AureaText.t(badge.1)) { guard let id = state.profile?.id else { return }; state.run {
                    state.profile = try await state.api.call("/profiles/\(id)/verification", method: "PUT", body: ["verification": badge.0], as: SocialAccount.self).profile
                    try await state.loadFeed()
                } } }
            }
            .alert(AureaText.t("social_delete_post"), isPresented: Binding(get: { deleting != nil }, set: { if !$0 { deleting = nil } })) {
                Button(AureaText.t("social_delete"), role: .destructive) { guard let post = deleting else { return }; deleting = nil; state.run {
                    _ = try await state.api.call("/posts/\(post.id)", method: "DELETE", as: SocialDone.self); try await state.loadFeed()
                } }
                Button(AureaText.t("editor_fechar"), role: .cancel) { deleting = nil }
            }
    }
    private var feedHeader: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(AureaText.t("social_intro")).font(.subheadline).foregroundStyle(AureaColors.muted)
            TextField(AureaText.t("social_search"), text: $search).textInputAutocapitalization(.never).autocorrectionDisabled()
                .textFieldStyle(.roundedBorder).accessibilityIdentifier("social.search")
            ForEach(results) { p in Button { search = ""; state.run { try await state.open(p.id) } } label: { SocialAuthor(profile: p) }.buttonStyle(.plain).disabled(state.busy) }
            HStack {
                Button(AureaText.t("social_all")) { state.run { state.following = false; try await state.loadFeed() } }
                    .fontWeight(state.following ? .regular : .bold).frame(minHeight: 44)
                Button(AureaText.t("social_following")) { state.run { state.following = true; try await state.loadFeed() } }
                    .fontWeight(state.following ? .bold : .regular).frame(minHeight: 44)
                Spacer()
                Button(AureaText.t("pn_caption")) { legacy = true }.frame(minHeight: 44)
            }.disabled(state.busy)
        }
    }
    private func profileHeader(_ p: SocialProfile) -> some View {
        VStack(alignment: .leading, spacing: 12) {
            SocialAuthor(profile: p, large: true)
            if let bio = p.bio, !bio.isEmpty { Text(bio) }
            HStack {
                Button("\(p.followers ?? 0) \(AureaText.t("social_followers"))") { people = "followers" }.frame(minHeight: 44)
                Spacer()
                Button("\(p.following ?? 0) \(AureaText.t("social_following"))") { people = "following" }.frame(minHeight: 44)
            }
            if p.id == state.me?.id {
                Button(AureaText.t("social_edit_profile")) { editing = true }.buttonStyle(.borderedProminent).accessibilityIdentifier("social.edit")
            } else { Button(AureaText.t(p.followed == true ? "social_unfollow" : "social_follow")) { state.run { try await state.follow(p) } }.buttonStyle(.borderedProminent) }
            if state.canVerify { Button(AureaText.t("social_verification")) { verifying = true }.frame(minHeight: 44) }
        }.frame(maxWidth: .infinity, alignment: .leading).padding(20).background(AureaColors.surface, in: RoundedRectangle(cornerRadius: 24)).disabled(state.busy)
    }
    private func postCard(_ post: SocialPost) -> some View {
        VStack(alignment: .leading, spacing: 12) {
            Button { state.run { try await state.open(post.author.id) } } label: { SocialAuthor(profile: post.author) }.buttonStyle(.plain)
            Text(Date(timeIntervalSince1970: post.createdAt / 1000), style: .relative).font(.caption).foregroundStyle(AureaColors.muted)
            if !post.body.isEmpty { Text(post.body).textSelection(.enabled) }
            if let a = post.asset {
                Button { download(a) } label: {
                    HStack { Image(systemName: a.kind == "project" ? "square.stack" : "sparkles"); VStack(alignment: .leading) { Text(a.name).lineLimit(2); Text("\(a.bytes / 1024) KB").font(.caption) }; Spacer(); Text(AureaText.t("social_download")) }.frame(minHeight: 48)
                }.buttonStyle(.bordered)
            }
            HStack(spacing: 12) {
                Button { state.run { try await state.like(post) } } label: { Label("\(post.likes)", systemImage: post.liked ? "heart.fill" : "heart") }.accessibilityLabel("\(post.likes) \(AureaText.t("social_likes"))").accessibilityIdentifier("social.like.\(post.id)")
                Button { comments = post } label: { Label("\(post.comments) \(AureaText.t("social_comments"))", systemImage: "bubble") }
                Spacer(minLength: 0)
                if state.me?.id == post.author.id { Button { deleting = post } label: { Image(systemName: "trash").frame(width: 44, height: 44) }.accessibilityLabel(AureaText.t("social_delete_post")) }
            }.frame(minHeight: 44)
        }.padding(16).frame(maxWidth: .infinity, alignment: .leading).background(AureaColors.surface, in: RoundedRectangle(cornerRadius: 22)).disabled(state.busy)
    }
    private func download(_ asset: SocialAsset) {
        state.run {
            let file = try await state.api.download(asset)
            if asset.kind == "project" {
                model.importProjectFile(file) { try? FileManager.default.removeItem(at: file) }
            } else {
                defer { try? FileManager.default.removeItem(at: file) }
                let data = try Data(contentsOf: file)
                guard let value = try JSONSerialization.jsonObject(with: data) as? [String: Any], value["aurea_preset"] as? Int == 1,
                      let raw = value["kind"] as? String,
                      let kind = ["effects": PanelPresetKind.effects, "text": .text, "animation": .animation, "caption": .caption, "curve": .curve][raw] else { throw SocialFailure(code: "invalid_preset") }
                try FileManager.default.createDirectory(at: kind.directory, withIntermediateDirectories: true)
                let name = (value["name"] as? String).flatMap { $0.isEmpty ? nil : $0 } ?? (asset.name as NSString).deletingPathExtension
                var target = kind.directory.appendingPathComponent(PanelPresetKind.fileName(name)), index = 2
                while FileManager.default.fileExists(atPath: target.path) { target = kind.directory.appendingPathComponent(PanelPresetKind.fileName("\(name) (\(index))")); index += 1 }
                try data.write(to: target, options: .atomic); model.toast = AureaText.t("social_saved")
            }
        }
    }
}

@MainActor
private struct SocialEditProfile: View {
    @Environment(\.dismiss) private var dismiss
    let profile: SocialProfile
    let api: SocialAPI
    let saved: (SocialProfile) -> Void
    @State private var username = ""
    @State private var name = ""
    @State private var bio = ""
    @State private var selectedPhoto: PhotosPickerItem?
    @State private var photo: Data?
    @State private var busy = false
    @State private var error: String?
    var body: some View {
        NavigationStack {
            Form {
                Section {
                    if let photo, let image = UIImage(data: photo) { Image(uiImage: image).resizable().scaledToFill().frame(width: 64, height: 64).clipShape(Circle()) }
                    PhotosPicker(AureaText.t("social_photo"), selection: $selectedPhoto, matching: .images).disabled(busy)
                }
                Section(footer: Text(AureaText.t("social_profile_hint"))) {
                    TextField(AureaText.t("social_username"), text: $username).textInputAutocapitalization(.never).autocorrectionDisabled().accessibilityIdentifier("social.username")
                    TextField(AureaText.t("social_name"), text: $name).accessibilityIdentifier("social.name")
                    TextField(AureaText.t("social_bio"), text: $bio, axis: .vertical).lineLimit(3...6)
                    Text("\(bio.count)/240").font(.caption)
                }
                if let error { Text(error).foregroundStyle(AureaColors.danger) }
                Button { save() } label: { if busy { ProgressView() } else { Text(AureaText.t("social_save")) } }
                    .disabled(busy || username.range(of: "^[a-zA-Z0-9_]{3,24}$", options: .regularExpression) == nil || name.trimmingCharacters(in: .whitespaces).isEmpty || name.count > 50 || bio.count > 240)
                    .accessibilityIdentifier("social.save")
            }.navigationTitle(AureaText.t("social_edit_profile")).navigationBarTitleDisplayMode(.inline)
                .toolbar { ToolbarItem(placement: .cancellationAction) { Button(AureaText.t("editor_fechar")) { dismiss() }.disabled(busy) } }
        }.tint(AureaColors.accent).interactiveDismissDisabled(busy)
            .onAppear { username = profile.username; name = profile.name; bio = profile.bio ?? "" }
            .task(id: selectedPhoto) {
                guard let item = selectedPhoto else { return }; busy = true; defer { busy = false }
                do {
                    guard let data = try await item.loadTransferable(type: Data.self) else { throw SocialFailure(code: "invalid_image") }
                    photo = try await Task.detached(priority: .userInitiated) {
                        guard let source = CGImageSourceCreateWithData(data as CFData, nil),
                              let image = CGImageSourceCreateThumbnailAtIndex(source, 0, [kCGImageSourceCreateThumbnailFromImageAlways: true, kCGImageSourceCreateThumbnailWithTransform: true, kCGImageSourceThumbnailMaxPixelSize: 512] as CFDictionary),
                              let jpeg = UIImage(cgImage: image).jpegData(compressionQuality: 0.85) else { throw SocialFailure(code: "invalid_image") }
                        return jpeg
                    }.value
                } catch { self.error = AureaText.t("social_error") }
            }
    }
    private func save() {
        busy = true; error = nil
        Task { defer { busy = false }
            do {
                var avatar = profile.avatar
                if let photo {
                    let file = FileManager.default.temporaryDirectory.appendingPathComponent("avatar-\(UUID().uuidString).jpg")
                    defer { try? FileManager.default.removeItem(at: file) }
                    try photo.write(to: file, options: .atomic); avatar = try await api.upload(file, kind: "avatar").id
                }
                let p = try await api.call("/me", method: "PUT", body: ["username": username, "name": name, "bio": bio, "avatar": avatar as Any? ?? NSNull()], as: SocialAccount.self)
                saved(p.profile)
            } catch { self.error = (error as? SocialFailure)?.message ?? AureaText.t("social_error") }
        }
    }
}

@MainActor
private struct SocialCompose: View {
    @EnvironmentObject private var model: AureaModel
    @Environment(\.dismiss) private var dismiss
    let api: SocialAPI
    let posted: () -> Void
    @State private var bodyText = ""
    @State private var attachment: URL?
    @State private var kind = "preset"
    @State private var picker: String?
    @State private var presets: [PanelPresetEntry] = []
    @State private var projects: [HomeProjectEntry] = []
    @State private var temporary: [URL] = []
    @State private var busy = false
    @State private var error: String?
    var body: some View {
        NavigationStack {
            Form {
                TextField(AureaText.t("social_post_hint"), text: $bodyText, axis: .vertical).lineLimit(3...8).accessibilityIdentifier("social.post.body")
                HStack {
                    Button(AureaText.t("social_attach_preset")) { presets = PanelPresetEntry.loadAll().filter { $0.textPreset == nil }; picker = "preset" }.buttonStyle(.borderless)
                    Spacer()
                    Button(AureaText.t("social_attach_project")) { projects = scanHomeProjects(); picker = "project" }.buttonStyle(.borderless)
                }.disabled(busy)
                if let attachment { HStack { Text(attachment.lastPathComponent); Spacer(); Button(AureaText.t("social_remove")) { self.attachment = nil }.disabled(busy) } }
                Text(AureaText.t("social_file_limit")).font(.caption).foregroundStyle(AureaColors.muted)
                if let error { Text(error).foregroundStyle(AureaColors.danger) }
                Button { publish() } label: { if busy { ProgressView() } else { Text(AureaText.t("social_publish")) } }
                    .disabled(busy || bodyText.count > 2000 || (bodyText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && attachment == nil)).accessibilityIdentifier("social.publish")
            }.navigationTitle(AureaText.t("social_create_post")).navigationBarTitleDisplayMode(.inline)
                .toolbar { ToolbarItem(placement: .cancellationAction) { Button(AureaText.t("editor_fechar")) { dismiss() }.disabled(busy) } }
                .sheet(isPresented: Binding(get: { picker != nil }, set: { if !$0 { picker = nil } })) {
                    NavigationStack { List {
                        if picker == "preset" {
                            if presets.isEmpty { Text(AureaText.t("social_no_files")) }
                            ForEach(presets) { p in Button(p.name) {
                                do {
                                    guard let source = p.source else { throw SocialFailure(code: "invalid_file") }
                                    let dir = FileManager.default.temporaryDirectory.appendingPathComponent("community-\(UUID().uuidString)")
                                    try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
                                    let file = dir.appendingPathComponent(PanelPresetKind.fileName(p.name))
                                    try source.write(to: file, atomically: true, encoding: .utf8); temporary.append(dir); attachment = file; kind = "preset"; picker = nil
                                } catch { self.error = AureaText.t("social_error"); picker = nil }
                            } }
                        } else {
                            if projects.isEmpty { Text(AureaText.t("social_no_files")) }
                            ForEach(projects) { p in Button(p.title) {
                                picker = nil; busy = true
                                model.exportProjectFile(path: p.file.url.path, title: p.title, includeMedia: true, requireComplete: true) { url in
                                    busy = false
                                    guard let url else { error = model.toast ?? AureaText.t("social_error"); return }
                                    attachment = url; temporary.append(url); kind = "project"
                                }
                            } }
                        }
                    }.navigationTitle(AureaText.t(picker == "preset" ? "social_attach_preset" : "social_attach_project")).toolbar {
                        ToolbarItem(placement: .cancellationAction) {
                            Button(AureaText.t("editor_fechar")) { picker = nil }
                        }
                    }
                    }
                }
        }.tint(AureaColors.accent).interactiveDismissDisabled(busy)
            .onDisappear { if !busy { for file in temporary { try? FileManager.default.removeItem(at: file) } } }
    }
    private func publish() {
        busy = true; error = nil
        Task { defer { busy = false }
            do {
                var asset: String?
                if let attachment { asset = try await api.upload(attachment, kind: kind).id }
                _ = try await api.call("/posts", method: "POST", body: ["body": bodyText, "asset": asset as Any? ?? NSNull()], as: SocialDone.self)
                for file in temporary { try? FileManager.default.removeItem(at: file) }; temporary = []; posted()
            } catch { self.error = (error as? SocialFailure)?.message ?? AureaText.t("social_error") }
        }
    }
}

@MainActor
private struct SocialComments: View {
    @Environment(\.dismiss) private var dismiss
    let post: SocialPost; let ownID: String?; let api: SocialAPI; let openProfile: (String) -> Void
    @State private var items: [SocialComment] = []
    @State private var cursor: String?
    @State private var text = ""
    @State private var busy = false
    @State private var error: String?
    var body: some View {
        NavigationStack {
            VStack {
                if busy { ProgressView() }
                if let error { Text(error).foregroundStyle(AureaColors.danger); Button(AureaText.t("social_refresh")) { run { try await load() } } }
                ScrollView { LazyVStack(alignment: .leading, spacing: 16) {
                    if items.isEmpty && !busy { Text(AureaText.t("social_no_comments")).foregroundStyle(AureaColors.muted) }
                    ForEach(items) { c in VStack(alignment: .leading, spacing: 6) {
                        Button { openProfile(c.author.id) } label: { SocialAuthor(profile: c.author) }.buttonStyle(.plain)
                        Text(c.body).textSelection(.enabled)
                        if ownID == c.author.id { Button(AureaText.t("social_delete"), role: .destructive) { run { _ = try await api.call("/comments/\(c.id)", method: "DELETE", as: SocialDone.self); try await load() } }.frame(minHeight: 44) }
                    } }
                    if cursor != nil { Button(AureaText.t("social_more")) { run { try await load(more: true) } }.frame(minHeight: 44) }
                }.padding(20) }
                HStack {
                    TextField(AureaText.t("social_comment_hint"), text: $text, axis: .vertical).lineLimit(1...3).textFieldStyle(.roundedBorder).accessibilityIdentifier("social.comment.body")
                    Button(AureaText.t("social_send")) { run {
                        _ = try await api.call("/posts/\(post.id)/comments", method: "POST", body: ["body": text], as: SocialDone.self); text = ""; try await load()
                    } }.disabled(busy || text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty || text.count > 1000).frame(minHeight: 44)
                }.padding(16)
            }.navigationTitle(AureaText.t("social_comments")).navigationBarTitleDisplayMode(.inline)
                .toolbar { ToolbarItem(placement: .cancellationAction) { Button(AureaText.t("editor_fechar")) { dismiss() }.disabled(busy) } }
        }.tint(AureaColors.accent).interactiveDismissDisabled(busy).task { run { try await load() } }
    }
    private func run(_ action: @escaping () async throws -> Void) {
        guard !busy else { return }; busy = true; error = nil
        Task { defer { busy = false }; do { try await action() } catch { self.error = (error as? SocialFailure)?.message ?? AureaText.t("social_error") } }
    }
    private func load(more: Bool = false) async throws {
        let result = try await api.call("/posts/\(post.id)/comments" + (more ? cursor.map { "?cursor=\(SocialAPI.query($0))" } ?? "" : ""), as: SocialPage<SocialComment>.self)
        items = more ? items + result.items.filter { item in !items.contains { $0.id == item.id } } : result.items; cursor = result.cursor
    }
}

@MainActor
private struct SocialPeople: View {
    let profileID: String; let type: String; let api: SocialAPI; let open: (String) -> Void
    @Environment(\.dismiss) private var dismiss
    @State private var items: [SocialProfile] = []
    @State private var cursor: String?
    @State private var busy = false
    @State private var error = false
    var body: some View {
        NavigationStack { List {
            if busy { ProgressView() }
            if error { Button(AureaText.t("social_error")) { load() } }
            if items.isEmpty && !busy { Text(AureaText.t("social_empty")) }
            ForEach(items) { p in Button { open(p.id) } label: { SocialAuthor(profile: p) } }
            if cursor != nil { Button(AureaText.t("social_more")) { load(more: true) }.disabled(busy) }
        }.navigationTitle(AureaText.t(type == "followers" ? "social_followers" : "social_following"))
            .toolbar { ToolbarItem(placement: .cancellationAction) { Button(AureaText.t("editor_fechar")) { dismiss() } } }
        }.tint(AureaColors.accent).task { load() }
    }
    private func load(more: Bool = false) {
        guard !busy else { return }; busy = true; error = false
        Task { defer { busy = false }
            do {
                let r = try await api.call("/profiles/\(profileID)/\(type)" + (more ? cursor.map { "?cursor=\(SocialAPI.query($0))" } ?? "" : ""), as: SocialPage<SocialProfile>.self)
                items = more ? items + r.items : r.items; cursor = r.cursor
            } catch { self.error = true }
        }
    }
}
