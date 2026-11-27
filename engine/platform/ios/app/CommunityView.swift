import SwiftUI
import PhotosUI
import Photos
import UniformTypeIdentifiers
import ImageIO
import AVKit
import Combine

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
private struct SocialAsset: Codable, Identifiable {
    var id: String; var kind: String; var name: String; var bytes: Int64
    var isMedia: Bool { kind == "image" || kind == "video" }
}
private struct SocialPost: Codable, Identifiable {
    var id: String; var body: String; var createdAt: Double; var author: SocialProfile
    var asset: SocialAsset?; var likes: Int; var comments: Int; var liked: Bool
    var category: String?
    var media: SocialAsset?
}
private struct SocialComment: Codable, Identifiable {
    var id: String; var body: String; var createdAt: Double; var author: SocialProfile
    var replyName: String?
}
private struct SocialPage<T: Decodable>: Decodable { var items: [T]; var cursor: String? }
private struct SocialAccount: Decodable {
    var profile: SocialProfile; var canVerify: Bool?; var feedFilters: Bool?
    var activity: Bool?; var mediaPosts: Bool?
    var feed: SocialPage<SocialPost>?
}
private struct SocialNotice: Decodable, Identifiable {
    var id: String; var post: String; var kind: String; var name: String; var createdAt: Double; var unread: Bool
    var title: String {
        switch kind {
        case "reply": return AureaText.t("social_activity_reply", name)
        case "announcement": return AureaText.t("social_activity_announcement", name)
        default: return AureaText.t("social_activity_comment", name)
        }
    }
}
private struct SocialActivityResponse: Decodable {
    var items: [SocialNotice]?; var unread: Int; var through: Int64?; var cursor: String?
}
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
    static func mediaURL(_ id: String) -> URL? { UUID(uuidString: id) == nil ? nil : root.appendingPathComponent("media/\(id)") }
    static func shareText(_ post: SocialPost) -> String {
        let author = post.author.username.isEmpty ? post.author.name : "@\(post.author.username)"
        let link = post.media.flatMap { mediaURL($0.id) } ?? post.asset.flatMap { assetURL($0.id) }
        return [author, post.body, link?.absoluteString ?? ""].filter { !$0.isEmpty }.joined(separator: "\n\n")
    }
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
        try Task.checkCancellation()
        return try decode(data, response, as: type)
    }
    func upload(_ url: URL, kind: String) async throws -> SocialAsset {
        let bytes = (try url.resourceValues(forKeys: [.fileSizeKey])).fileSize ?? 0
        let isMedia = kind == "image" || kind == "video"
        let limit: Int
        switch kind {
        case "avatar": limit = 512 * 1024
        case "preset": limit = 1024 * 1024
        case "image": limit = 8 * 1024 * 1024
        case "video": limit = 90 * 1024 * 1024
        default: limit = 50 * 1024 * 1024
        }
        guard (1...limit).contains(bytes) else { throw SocialFailure(code: "file_too_large") }
        var r = request("/\(isMedia ? "media" : "assets")?kind=\(kind)&name=\(Self.query(url.lastPathComponent))", "POST")
        r.setValue("application/octet-stream", forHTTPHeaderField: "Content-Type"); r.setValue(String(bytes), forHTTPHeaderField: "Content-Length")
        let (data, response) = try await ContaAPI.session.upload(for: r, fromFile: url)
        return try decode(data, response, as: SocialAsset.self)
    }
    func download(_ asset: SocialAsset) async throws -> URL {
        let limit: Int64 = asset.kind == "video" ? 90 * 1024 * 1024 : asset.kind == "image" ? 8 * 1024 * 1024 : 50 * 1024 * 1024
        guard (1...limit).contains(asset.bytes), UUID(uuidString: asset.id) != nil else { throw SocialFailure(code: "invalid_file") }
        let file: URL
        let response: URLResponse
        if asset.isMedia {
            guard let url = Self.mediaURL(asset.id) else { throw SocialFailure(code: "invalid_file") }
            (file, response) = try await URLSession.shared.download(from: url)
        } else {
            (file, response) = try await ContaAPI.session.download(for: request("/assets/\(asset.id)", "GET"))
        }
        defer { try? FileManager.default.removeItem(at: file) }
        try Task.checkCancellation()
        guard (response as? HTTPURLResponse)?.statusCode == 200,
              (try file.resourceValues(forKeys: [.fileSizeKey])).fileSize == Int(asset.bytes) else {
            try? FileManager.default.removeItem(at: file); throw SocialFailure(code: "invalid_file")
        }
        let ext: String
        if asset.isMedia {
            let final = response.url
            guard final?.scheme == "https", final?.host == Self.root.host || final?.host == "media.aureaeditor.com" else {
                throw SocialFailure(code: "invalid_file")
            }
            switch (asset.kind, response.mimeType ?? "") {
            case ("image", "image/jpeg"): ext = "jpg"
            case ("image", "image/png"): ext = "png"
            case ("video", "video/quicktime"): ext = "mov"
            case ("video", "video/mp4"): ext = "mp4"
            default: throw SocialFailure(code: "invalid_file")
            }
        } else { ext = asset.kind == "project" ? "aureaproj" : "json" }
        let target = FileManager.default.temporaryDirectory.appendingPathComponent("community-\(UUID().uuidString).\(ext)")
        try FileManager.default.moveItem(at: file, to: target)
        return target
    }
}

private enum SocialFeedTab: CaseIterable, Hashable {
    case all, following, official
    var title: String {
        switch self {
        case .all: return AureaText.t("social_all")
        case .following: return AureaText.t("social_following")
        case .official: return AureaText.t("social_official_tab")
        }
    }
}

private struct SocialChoice<Value: Hashable>: View {
    let label: String
    let options: [Value]
    let title: (Value) -> String
    @Binding var selection: Value
    var body: some View {
        Menu {
            Picker(label, selection: $selection) {
                ForEach(options, id: \.self) { option in Text(title(option)).tag(option) }
            }
        } label: {
            HStack(spacing: 6) {
                Text(title(selection)).lineLimit(1)
                Image(systemName: "chevron.down").font(.caption)
            }.frame(minHeight: 44)
        }
        .accessibilityLabel(label)
        .accessibilityValue(title(selection))
    }
}

private enum SocialFeedSort: String, CaseIterable {
    case newest, popular, oldest
    var title: String {
        switch self {
        case .newest: return AureaText.t("social_sort_newest")
        case .popular: return AureaText.t("social_sort_popular")
        case .oldest: return AureaText.t("social_sort_oldest")
        }
    }
}

private enum SocialCategory: String, CaseIterable {
    case all, text, showcase, project, preset, announcement
    var title: String {
        switch self {
        case .all: return AureaText.t("social_all_categories")
        case .text: return AureaText.t("social_category_text")
        case .showcase: return AureaText.t("social_category_showcase")
        case .project: return AureaText.t("social_category_project")
        case .preset: return AureaText.t("social_category_preset")
        case .announcement: return AureaText.t("social_category_announcement")
        }
    }
}

@MainActor
private final class SocialState: ObservableObject {
    @Published var me: SocialProfile?
    @Published var profile: SocialProfile?
    @Published var canVerify = false
    @Published var feed: [SocialPost] = []
    @Published var cursor: String?
    @Published var feedTab: SocialFeedTab = .all
    @Published var feedSort: SocialFeedSort = .newest
    @Published var feedCategory: SocialCategory = .all
    @Published var feedFilters = false
    @Published var activityAvailable = false
    @Published var mediaPosts = false
    @Published var unread = 0
    @Published var busy = false
    @Published var error: String?
    /// Omega home: sem rede (URLError de conexão) = o estado sem rede da referência.
    @Published var offline = false
    var api = SocialAPI(token: "")
    var ownMode = false
    private var generation = UUID()
    private func requireCurrent(_ request: UUID) throws {
        try Task.checkCancellation()
        if request != generation { throw CancellationError() }
    }
    func fail(_ error: Error) {
        let codes: [URLError.Code] = [.notConnectedToInternet, .timedOut, .cannotFindHost, .cannotConnectToHost,
                                      .networkConnectionLost, .dnsLookupFailed, .dataNotAllowed, .internationalRoamingOff]
        offline = (error as? URLError).map { codes.contains($0.code) } ?? false
        self.error = (error as? SocialFailure)?.message ?? AureaText.t("social_error")
    }
    /// Puxar para atualizar (`.refreshable`): espera o trabalho terminar.
    func pullRefresh() async {
        guard !busy else { return }
        let request = generation
        busy = true; error = nil
        defer { if generation == request { busy = false } }
        do { try await refresh(); if generation == request { offline = false } }
        catch { if !Task.isCancelled && generation == request { fail(error) } }
    }
    func run(_ action: @escaping () async throws -> Void) {
        guard !busy else { return }
        let request = generation
        busy = true; error = nil
        Task {
            defer { if generation == request { busy = false } }
            do { try requireCurrent(request); try await action(); if generation == request { offline = false } }
            catch { if !Task.isCancelled && generation == request { fail(error) } }
        }
    }
    func start(_ session: ContaSessao?, own: Bool) async {
        let request = UUID(); generation = request
        api = SocialAPI(token: session?.token ?? ""); ownMode = own; profile = nil; me = nil
        feed = []; cursor = nil; feedTab = .all; feedSort = .newest; feedCategory = .all; feedFilters = false
        activityAvailable = false; mediaPosts = false; unread = 0
        busy = true; error = nil
        defer { if generation == request { busy = false } }
        do { try await refresh(initial: !own); offline = false } catch { if !Task.isCancelled && generation == request { fail(error) } }
    }
    func refresh(initial: Bool = false) async throws {
        let request = generation
        let account = try await api.call(initial ? "/me?includeFeed=1" : "/me", as: SocialAccount.self)
        try requireCurrent(request)
        me = account.profile; canVerify = account.canVerify == true; feedFilters = account.feedFilters == true
        activityAvailable = account.activity == true; mediaPosts = account.mediaPosts == true
        if !feedFilters && feedTab == .official { feedTab = .all }
        if (ownMode && profile == nil) || profile?.id == me?.id { profile = me }
        if initial, let page = account.feed { feed = page.items; cursor = page.cursor }
        else { try await loadFeed() }
    }
    func loadFeed(more: Bool = false) async throws {
        let request = generation
        var suffix = "?following=\(feedTab == .following && profile == nil ? 1 : 0)"
        if activityAvailable { suffix += "&compact=1" }
        if let profile { suffix += "&user=\(profile.id)" }
        if feedFilters && profile == nil {
            suffix += "&sort=\(feedSort.rawValue)"
            let category = feedTab == .official ? .announcement : feedCategory
            if category != .all { suffix += "&category=\(category.rawValue)" }
        }
        if more, let cursor { suffix += "&cursor=\(SocialAPI.query(cursor))" }
        let result = try await api.call("/posts\(suffix)", as: SocialPage<SocialPost>.self)
        try requireCurrent(request)
        let displayed = more ? Set(feed.map(\.id)) : []
        feed = more ? feed + result.items.filter { !displayed.contains($0.id) } : result.items
        cursor = result.cursor
    }
    func pollActivity() async {
        let request = generation
        while !Task.isCancelled && request == generation {
            do {
                let status = try await api.call("/notifications?summary=1", as: SocialActivityResponse.self)
                try requireCurrent(request)
                unread = status.unread
            } catch { if Task.isCancelled { return } }
            do { try await Task.sleep(nanoseconds: 45_000_000_000) } catch { return }
        }
    }
    func updateComments(_ id: String, delta: Int) {
        if let index = feed.firstIndex(where: { $0.id == id }) {
            feed[index].comments = max(0, feed[index].comments + delta)
        }
    }
    func resetFeed() async throws {
        feed = []; cursor = nil
        try await loadFeed()
    }
    func open(_ id: String) async throws {
        let request = generation
        let result = try await api.call("/profiles/\(id)", as: SocialAccount.self).profile
        try requireCurrent(request)
        profile = result
        try await resetFeed()
    }
    func follow(_ p: SocialProfile) async throws {
        let request = generation
        let result = try await api.call("/profiles/\(p.id)/follow", method: p.followed == true ? "DELETE" : "PUT", as: SocialAccount.self).profile
        try requireCurrent(request)
        profile = result
    }
    func like(_ post: SocialPost) async throws {
        let request = generation
        let result = try await api.call("/posts/\(post.id)/like", method: post.liked ? "DELETE" : "PUT", as: SocialLike.self)
        try requireCurrent(request)
        if let i = feed.firstIndex(where: { $0.id == post.id }) { feed[i].liked = result.liked; feed[i].likes = result.likes }
    }
}

private let socialBadges = [("blue", "social_badge_blue"), ("green", "social_badge_green"), ("gold", "social_badge_gold"), ("", "social_badge_none")]
private let socialReportReasons = [
    ("spam", "mod_reason_spam"), ("nudity", "mod_reason_nudity"), ("violence", "mod_reason_violence"),
    ("hate", "mod_reason_hate"), ("harassment", "mod_reason_harassment"), ("illegal", "mod_reason_illegal"),
    ("copyright", "mod_reason_copyright"), ("other", "mod_reason_other"),
]
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
private struct SocialAvatar: View {
    let profile: SocialProfile
    let size: CGFloat
    var body: some View {
        ZStack {
            Circle().fill(AureaColors.lightAccentInk.opacity(0.14))
            let initial = String((profile.name.isEmpty ? profile.username : profile.name).prefix(1)).uppercased()
            Text(initial.isEmpty ? "A" : initial)
                .font(.system(size: size * 0.42, weight: .bold)).foregroundStyle(AureaColors.lightAccentInk)
            if let id = profile.avatar, let url = SocialAPI.assetURL(id) {
                AsyncImage(url: url) { image in image.resizable().scaledToFill() } placeholder: { Color.clear }
            }
        }.frame(width: size, height: size).clipShape(Circle()).accessibilityHidden(true)
    }
}

private struct SocialAuthor: View {
    let profile: SocialProfile
    var large = false
    var compact = false
    var body: some View {
        HStack(spacing: 12) {
            SocialAvatar(profile: profile, size: large ? 64 : compact ? 32 : 44)
            VStack(alignment: .leading, spacing: 3) {
                HStack(spacing: 4) {
                    Text(profile.name.isEmpty ? AureaText.t("social_profile") : profile.name)
                        .font((compact ? Font.caption : Font.body).weight(.semibold)).lineLimit(1)
                    SocialBadge(value: profile.verification)
                }
                Text(profile.username.isEmpty ? AureaText.t("social_profile_required") : "@\(profile.username)")
                    .font(compact ? .caption2 : .caption).foregroundStyle(AureaColors.lightTextMuted).lineLimit(1)
            }
            Spacer(minLength: 0)
        }.frame(minHeight: 48).foregroundStyle(AureaColors.lightText).contentShape(Rectangle())
    }
}

@MainActor
private struct SocialPostMedia: View {
    let asset: SocialAsset
    let api: SocialAPI
    var detail = false
    var onOpen: () -> Void = {}
    @State private var image: UIImage?
    @State private var player: AVPlayer?
    @State private var failed = false
    private static let images: NSCache<NSString, UIImage> = {
        let cache = NSCache<NSString, UIImage>()
        cache.totalCostLimit = 16 * 1024 * 1024
        return cache
    }()
    private var label: String { AureaText.t(asset.kind == "video" ? "social_play_video" : "social_open_post") }
    private var poster: some View {
        let ratio = detail ? image.map { min(1.8, max(0.8, $0.size.width / max(1, $0.size.height))) } ?? 1.78 : 1
        return GeometryReader { geometry in
            ZStack {
                AureaColors.lightAccentInk.opacity(0.10)
                if let image {
                    Image(uiImage: image).resizable()
                        .aspectRatio(contentMode: detail ? .fit : .fill).frame(width: geometry.size.width, height: geometry.size.height)
                } else if asset.kind == "image" {
                    Image(systemName: "photo").foregroundStyle(AureaColors.lightTextMuted)
                }
                if asset.kind == "video" {
                    Image(systemName: "play.fill").font(.system(size: 20, weight: AureaIconWeight.symbol))
                        .foregroundStyle(.white).frame(width: 52, height: 52)
                        .background(.black.opacity(0.62), in: Circle()).accessibilityHidden(true)
                }
            }.clipped()
        }
        .aspectRatio(ratio, contentMode: .fit)
    }
    var body: some View {
        Group {
            if detail && asset.kind == "video" {
                if failed { Text(AureaText.t("social_media_error")).foregroundStyle(AureaColors.lightTextMuted) }
                else if let player, let item = player.currentItem {
                    VideoPlayer(player: player).aspectRatio(16.0 / 9, contentMode: .fit)
                        .onReceive(item.publisher(for: \.status)) { status in
                            if status == .failed { failed = true }
                        }
                } else { ProgressView().frame(maxWidth: .infinity, minHeight: 100) }
            } else if detail {
                poster.accessibilityLabel(asset.name)
            } else {
                Button(action: onOpen) { poster }.buttonStyle(.plain).accessibilityLabel(label)
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: detail ? 14 : 0))
        .task(id: asset.id) {
            guard let url = SocialAPI.mediaURL(asset.id) else { return }
            if asset.kind == "video" {
                if detail {
                    let playback = AVPlayer(url: url)
                    player = playback
                    playback.play()
                } else if (1...(90 * 1024 * 1024)).contains(asset.bytes) {
                    let key = "video:\(asset.id):720" as NSString
                    if let cached = Self.images.object(forKey: key) { image = cached; return }
                    let generator = AVAssetImageGenerator(asset: AVURLAsset(url: url))
                    generator.appliesPreferredTrackTransform = true
                    generator.maximumSize = CGSize(width: 720, height: 720)
                    guard let frame = try? await generator.image(at: .zero), !Task.isCancelled else { return }
                    let decoded = UIImage(cgImage: frame.image)
                    Self.images.setObject(decoded, forKey: key, cost: frame.image.bytesPerRow * frame.image.height)
                    image = decoded
                }
            } else if asset.kind == "image" {
                let side = detail ? 1440 : 840
                let key = "\(asset.id):\(side)" as NSString
                if let cached = Self.images.object(forKey: key) { image = cached; return }
                do {
                    let file = try await api.download(asset)
                    defer { try? FileManager.default.removeItem(at: file) }
                    let pixels = await Task.detached(priority: .utility) {
                        guard let source = CGImageSourceCreateWithURL(file as CFURL, nil) else { return nil as CGImage? }
                        return CGImageSourceCreateThumbnailAtIndex(source, 0, [
                            kCGImageSourceCreateThumbnailFromImageAlways: true,
                            kCGImageSourceCreateThumbnailWithTransform: true,
                            kCGImageSourceThumbnailMaxPixelSize: side,
                        ] as CFDictionary)
                    }.value
                    guard !Task.isCancelled, let pixels else { return }
                    let decoded = UIImage(cgImage: pixels)
                    Self.images.setObject(decoded, forKey: key, cost: pixels.bytesPerRow * pixels.height)
                    image = decoded
                } catch { }
            }
        }
        .onDisappear { player?.pause(); player = nil }
    }
}

@MainActor
private struct SocialActivity: View {
    @Environment(\.dismiss) private var dismiss
    let api: SocialAPI
    let onUnread: (Int) -> Void
    let onOpen: (SocialPost) -> Void
    @State private var items: [SocialNotice] = []
    @State private var cursor: String?
    @State private var through: Int64 = 0
    @State private var busy = false
    @State private var failed = false
    var body: some View {
        NavigationStack {
            List {
                if busy { ProgressView() }
                if failed { Button(AureaText.t("social_refresh")) { run { try await load() } } }
                if !busy && !failed && items.isEmpty { Text(AureaText.t("social_activity_empty")).foregroundStyle(AureaColors.lightTextMuted) }
                ForEach(items) { entry in
                    Button { run { onOpen(try await api.call("/posts/\(entry.post)", as: SocialPost.self)) } } label: {
                        HStack(spacing: 12) {
                            Circle().fill(entry.unread ? AureaColors.lightAccentInk : Color.clear).frame(width: 9, height: 9)
                                .accessibilityHidden(true)
                            VStack(alignment: .leading, spacing: 4) {
                                Text(entry.title).foregroundStyle(AureaColors.lightText)
                                Text(Date(timeIntervalSince1970: entry.createdAt / 1000), style: .relative)
                                    .font(.caption).foregroundStyle(AureaColors.lightTextMuted)
                            }
                            Spacer()
                            Image(systemName: "chevron.right").mirrorsInRtl().font(.caption).accessibilityHidden(true)
                        }
                    }.disabled(busy)
                }
                if cursor != nil { Button(AureaText.t("social_more")) { run { try await load(more: true) } }.disabled(busy) }
            }.navigationTitle(AureaText.t("social_activity")).navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button(AureaText.t("editor_fechar")) { dismiss() } }
                ToolbarItem(placement: .primaryAction) {
                    if items.contains(where: \.unread) {
                        Button(AureaText.t("social_activity_mark_read")) { run {
                            _ = try await api.call("/notifications/read", method: "PUT", body: ["through": through], as: SocialDone.self)
                            try await load()
                        } }.disabled(busy)
                    }
                }
            }
            .refreshable { try? await load() }
        }.tint(AureaColors.lightAccentInk).task { run { try await load() } }
    }
    private func run(_ action: @escaping () async throws -> Void) {
        guard !busy else { return }
        busy = true; failed = false
        Task {
            defer { busy = false }
            do { try await action() } catch { if !Task.isCancelled { failed = true } }
        }
    }
    private func load(more: Bool = false) async throws {
        let suffix = more ? cursor.map { "?cursor=\(SocialAPI.query($0))" } ?? "" : ""
        let page = try await api.call("/notifications\(suffix)", as: SocialActivityResponse.self)
        let displayed = more ? Set(items.map(\.id)) : []
        items = (more ? items : []) + (page.items ?? []).filter { !displayed.contains($0.id) }
        cursor = page.cursor
        if !more { through = page.through ?? 0 }
        onUnread(page.unread)
    }
}

@MainActor
struct CommunityView: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var conta: ContaModel
    @Environment(\.scenePhase) private var scenePhase
    let profileMode: Bool
    var onSignIn: (() -> Void)? = nil
    @StateObject private var state = SocialState()
    @State private var search = ""
    @State private var results: [SocialProfile] = []
    @State private var editing = false
    @State private var composing = false
    @State private var legacy = false
    @State private var storeOpen = false
    @State private var storeSoon = false
    /// Ligada pelo servidor (GET /store/config): sem armazenamento, "Em breve".
    @State private var storeReady = false
    @State private var verifying = false
    @State private var deleting: SocialPost?
    @State private var reporting: SocialPost?
    @State private var comments: SocialPost?
    @State private var activityOpen = false
    @State private var feedTools = false
    @State private var savingMediaID: String?
    @State private var pendingPost: SocialPost?
    @State private var people: String?
    var body: some View {
        VStack(spacing: 0) {
            communityHeader
            Divider()
            if state.busy && !state.feed.isEmpty { ProgressView().frame(maxWidth: .infinity).padding(4) }
            if let error = state.error, !state.offline {
                VStack(alignment: .leading, spacing: 4) {
                    Text(error).font(.subheadline).foregroundStyle(AureaColors.lightDanger).accessibilityIdentifier("social.error")
                    if state.feed.isEmpty {
                        Button(AureaText.t("social_refresh")) { state.run { try await state.refresh() } }
                            .frame(minHeight: 44).disabled(state.busy)
                    }
                }.frame(maxWidth: .infinity, alignment: .leading).padding(.horizontal, 20).padding(.vertical, 8)
            }
            if state.offline && state.feed.isEmpty && !state.busy {
                ScrollView {
                    VStack(spacing: 15) {
                        CupertinoGlyph.text(CupertinoGlyph.ExclamationmarkTriangle, size: 48, color: AureaColors.lightTextMuted)
                        Text(AureaText.t("conta_erro_rede")).font(.aurea(size: 12, weight: .bold)).foregroundStyle(AureaColors.lightText)
                            .multilineTextAlignment(.center).frame(maxWidth: 300)
                    }.frame(maxWidth: .infinity).padding(.top, 120).padding(24)
                    .background(ScrollBounceProbe(bounces: true).accessibilityHidden(true))
                }
                .refreshable { await state.pullRefresh() }
                .accessibilityIdentifier("home.feed.offline")
            } else if state.busy && state.feed.isEmpty && state.me == nil {
                Spacer()
                ProgressView().tint(AureaColors.lightAccentInk).accessibilityIdentifier("home.feed.loading")
                Spacer()
            } else {
            ScrollViewReader { scroll in
            ScrollView {
                LazyVStack(spacing: 6) {
                    if let profile = state.profile { profileHeader(profile).padding(.horizontal, 10) }
                    else if feedTools { feedHeader.padding(.horizontal, 14).id("social.feed.options.top") }
                    if let profile = state.profile, profile.id == state.me?.id {
                        Button { if state.me?.username.isEmpty != false { editing = true } else { composing = true } } label: {
                            Text(AureaText.t(officialFeed ? "social_create_announcement" : "social_create_post"))
                                .frame(maxWidth: .infinity, minHeight: 48)
                        }.buttonStyle(.borderedProminent).tint(AureaColors.accent).foregroundStyle(AureaColors.onAccent)
                            .disabled(state.me == nil || state.busy).padding(.horizontal, 10).accessibilityIdentifier("social.compose")
                    }
                    if state.feed.isEmpty {
                        if state.busy {
                            ProgressView().tint(AureaColors.lightAccentInk).padding(.vertical, 24)
                                .accessibilityIdentifier("home.feed.loading")
                        } else if state.error == nil {
                            Text(AureaText.t(officialFeed ? "social_empty_announcements" : "social_empty"))
                                .foregroundStyle(AureaColors.lightTextMuted).padding(.vertical, 24)
                        }
                    }
                    ForEach(state.feed) { post in postCard(post) }
                    if let cursor = state.cursor {
                        Button(AureaText.t("social_more")) { state.run { try await state.loadFeed(more: true) } }
                            .frame(minHeight: 44).disabled(state.busy)
                            .task(id: cursor) {
                                if state.activityAvailable && state.error == nil { state.run { try await state.loadFeed(more: true) } }
                            }
                    }
                }.frame(maxWidth: 620).padding(.top, 8).padding(.bottom, 69)
                .frame(maxWidth: .infinity)
                .background(ScrollBounceProbe(bounces: true).accessibilityHidden(true))
            }
            .refreshable { await state.pullRefresh() }
            .accessibilityIdentifier("social.feed")
            .onChange(of: feedTools) { open in
                if open { scroll.scrollTo("social.feed.options.top", anchor: .top) }
            }
            }
            }
        }.background(AureaColors.lightBackground).foregroundStyle(AureaColors.lightText).tint(AureaColors.lightAccentInk)
            .accessibilityIdentifier("social.screen")
            .task(id: [profileMode ? "profile" : "feed", conta.sessao()?.token ?? ""]) {
                await state.start(conta.sessao(), own: profileMode)
            }
            .task(id: state.activityAvailable && !activityOpen && scenePhase == .active) {
                if state.activityAvailable && !activityOpen && scenePhase == .active { await state.pollActivity() }
            }
            .task { storeReady = await StoreAPI.ready(token: conta.sessao()?.token ?? "") }
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
            .sheet(isPresented: $composing) {
                SocialCompose(api: state.api, feedFilters: state.feedFilters, canAnnounce: state.canVerify, mediaPosts: state.mediaPosts,
                              initialCategory: officialFeed ? .announcement : (
                                state.profile == nil && state.feedCategory == .showcase ? .showcase : .all)) {
                    composing = false; state.run { try await state.refresh() }
                }.environmentObject(model)
            }
            .sheet(isPresented: $activityOpen, onDismiss: {
                comments = pendingPost; pendingPost = nil
            }) {
                SocialActivity(api: state.api, onUnread: { state.unread = $0 }, onOpen: { post in
                    pendingPost = post; activityOpen = false
                })
            }
            .sheet(item: $comments) { post in
                SocialComments(post: post, own: state.me, replies: state.activityAvailable, api: state.api,
                    openProfile: { id in comments = nil; state.run { try await state.open(id) } },
                    onCommentDelta: { state.updateComments(post.id, delta: $0) })
                    .presentationDetents([.large])
            }
            .sheet(isPresented: $legacy) { HomeCommunityPresets().padding(.top, 24) }
            .fullScreenCover(isPresented: $storeOpen) { StoreView().environmentObject(model).environmentObject(conta) }
            .alert(AureaText.t("store_soon"), isPresented: $storeSoon) {
                Button(AureaText.t("editor_fechar"), role: .cancel) {}
            } message: { Text(AureaText.t("store_soon_message")) }
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
            .confirmationDialog(AureaText.t("mod_report_title"),
                isPresented: Binding(get: { reporting != nil }, set: { if !$0 { reporting = nil } }),
                titleVisibility: .visible, presenting: reporting) { post in
                ForEach(socialReportReasons, id: \.0) { reason in
                    Button(AureaText.t(reason.1)) { state.run {
                        _ = try await state.api.call("/reports", method: "POST", body: ["kind": "post", "id": post.id, "reason": reason.0], as: SocialDone.self)
                        model.toast = AureaText.t("mod_report_thanks")
                    } }
                }
            }
    }
    private var officialFeed: Bool { state.profile == nil && state.feedTab == .official }

    private var communityHeader: some View {
        HStack(spacing: 0) {
            if state.profile != nil && (!profileMode || state.profile?.id != state.me?.id) {
                Button { state.run { state.profile = profileMode ? state.me : nil; try await state.resetFeed() } } label: {
                    headerIcon("chevron.left").mirrorsInRtl()
                }.accessibilityLabel(AureaText.t("common_back")).disabled(state.busy)
            }
            Text(AureaText.t(officialFeed ? "social_official_tab" : state.profile == nil ? "social_community" : "social_profile"))
                .font(.aurea(size: 21, weight: .bold)).foregroundStyle(AureaColors.lightText)
                .lineLimit(1).minimumScaleFactor(0.8)
            Spacer(minLength: 4)
            if state.profile == nil {
                Button {
                    feedTools.toggle()
                    if !feedTools { search = "" }
                } label: {
                    headerIcon("slider.horizontal.3")
                        .foregroundStyle(feedTools ? AureaColors.lightAccentInk : AureaColors.lightText)
                }.accessibilityLabel(AureaText.t("social_feed_options"))
                    .accessibilityAddTraits(feedTools ? .isSelected : [])
                    .accessibilityIdentifier("social.feed.options")
                if !officialFeed || state.canVerify {
                    Button { if state.me?.username.isEmpty != false { editing = true } else { composing = true } } label: {
                        headerIcon("plus.square")
                    }.accessibilityLabel(AureaText.t(officialFeed ? "social_create_announcement" : "social_create_post"))
                        .accessibilityIdentifier("social.compose").disabled(state.me == nil || state.busy)
                }
            }
            if state.activityAvailable {
                Button { activityOpen = true } label: {
                    headerIcon("bubble").overlay(alignment: .topTrailing) {
                        if state.unread > 0 {
                            Text(state.unread > 99 ? "99+" : "\(state.unread)").font(.caption2.bold())
                                .padding(.horizontal, 5).padding(.vertical, 3)
                                .foregroundStyle(AureaColors.onAccent).background(AureaColors.accent, in: Capsule())
                        }
                    }
                }.accessibilityLabel(AureaText.t("social_activity_unread", state.unread)).accessibilityIdentifier("social.activity")
            }
            if state.profile == nil, let me = state.me {
                Button {
                    feedTools = false; search = ""
                    state.run { try await state.open(me.id) }
                } label: {
                    SocialAvatar(profile: me, size: 32).frame(width: 44, height: 44).contentShape(Rectangle())
                }.accessibilityLabel(AureaText.t("social_my_profile")).accessibilityIdentifier("social.myProfile").disabled(state.busy)
            } else if state.profile == nil, !conta.logado, let onSignIn {
                Button(action: onSignIn) { headerIcon("person.crop.circle") }
                    .accessibilityLabel(AureaText.t("conta_entrar")).accessibilityIdentifier("social.signIn")
            }
        }.buttonStyle(.plain)
            .padding(.leading, 16).padding(.trailing, 8).frame(minHeight: 56)
    }

    private func headerIcon(_ name: String) -> some View {
        Image(systemName: name).font(.system(size: 21, weight: AureaIconWeight.symbol))
            .frame(width: 44, height: 44).contentShape(Rectangle())
    }

    private var feedHeader: some View {
        VStack(alignment: .leading, spacing: 12) {
            TextField(AureaText.t("social_search"), text: $search).textInputAutocapitalization(.never).autocorrectionDisabled()
                .textFieldStyle(.roundedBorder).accessibilityIdentifier("social.search")
            ForEach(results) { p in Button { feedTools = false; search = ""; state.run { try await state.open(p.id) } } label: { SocialAuthor(profile: p) }.buttonStyle(.plain).disabled(state.busy) }
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 8) {
                    ForEach(SocialFeedTab.allCases.filter { state.feedFilters || $0 != .official }, id: \.self) { tab in
                        Button {
                            guard state.feedTab != tab else { return }
                            state.run { state.feedTab = tab; try await state.resetFeed() }
                        } label: {
                            Text(tab.title).fontWeight(state.feedTab == tab ? .bold : .regular).frame(minHeight: 36)
                        }.buttonStyle(.bordered)
                            .accessibilityAddTraits(state.feedTab == tab ? .isSelected : [])
                            .accessibilityIdentifier("social.tab.\(tab)")
                    }
                }
            }.disabled(state.busy)
            if state.feedFilters {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 16) {
                        SocialChoice(label: AureaText.t("social_sort"), options: SocialFeedSort.allCases, title: { $0.title },
                                     selection: Binding(get: { state.feedSort }, set: { value in
                            guard state.feedSort != value else { return }
                            state.run { state.feedSort = value; try await state.resetFeed() }
                        })).accessibilityIdentifier("social.sort")
                        if !officialFeed {
                            SocialChoice(label: AureaText.t("social_filter"), options: SocialCategory.allCases.filter { $0 != .announcement },
                                         title: { $0.title }, selection: Binding(get: { state.feedCategory }, set: { value in
                                guard state.feedCategory != value else { return }
                                state.run { state.feedCategory = value; try await state.resetFeed() }
                            })).accessibilityIdentifier("social.category")
                        }
                    }
                }.disabled(state.busy)
            }
            // 0.4.6: a Loja da Comunidade (plugins, efeitos e presets). Par do Android.
            // Enquanto o armazenamento (R2) não está ativo no servidor: "Em breve".
            // Beta (nov/2026): na linha dos filtros o nome quebrava no meio; agora tem a linha dele.
            if !officialFeed {
                Button(AureaText.t("store_title") + (storeReady ? "" : " · " + AureaText.t("store_soon"))) {
                    if storeReady { storeOpen = true } else { storeSoon = true }
                }.lineLimit(1).truncationMode(.tail).frame(minHeight: 44).accessibilityIdentifier("social.store").disabled(state.busy)
                Button(AureaText.t("pn_caption")) { legacy = true }.lineLimit(1).frame(minHeight: 44)
            }
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
        }.frame(maxWidth: .infinity, alignment: .leading).padding(20).background(AureaColors.lightSurface, in: RoundedRectangle(cornerRadius: 24)).disabled(state.busy)
    }
    private func postCard(_ post: SocialPost) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            postAuthor(post)
            postArtwork(post)
            postActions(post)
            if post.media != nil && !post.body.isEmpty {
                Button { comments = post } label: {
                    Text(post.body).lineLimit(3).frame(maxWidth: .infinity, alignment: .leading)
                        .padding(.horizontal, 14)
                }.buttonStyle(.plain).accessibilityHint(AureaText.t("social_open_post"))
            }
            HStack(spacing: 8) {
                if let category = post.category.flatMap(SocialCategory.init(rawValue:)), category != .text && category != .all {
                    Text(category.title).foregroundStyle(AureaColors.lightAccentInk)
                }
                Text(Date(timeIntervalSince1970: post.createdAt / 1000), style: .relative)
                    .foregroundStyle(AureaColors.lightTextMuted)
            }.font(.caption).padding(.horizontal, 14).padding(.top, 2).padding(.bottom, 10)
            Divider()
        }.frame(maxWidth: .infinity, alignment: .leading)
            .background(AureaColors.lightSurface)
    }

    private func postAuthor(_ post: SocialPost) -> some View {
        HStack(spacing: 0) {
            Button {
                feedTools = false; search = ""
                state.run { try await state.open(post.author.id) }
            } label: {
                SocialAuthor(profile: post.author, compact: true)
            }.buttonStyle(.plain).disabled(state.busy)
            Menu {
                if post.media != nil, let asset = post.asset {
                    Button { download(asset) } label: { Label(asset.name, systemImage: "arrow.down.to.line") }
                }
                if state.me?.id != post.author.id {
                    Button { reporting = post } label: { Label(AureaText.t("mod_report"), systemImage: "flag") }
                }
                if state.me?.id == post.author.id || state.canVerify {
                    Button(role: .destructive) { deleting = post } label: { Label(AureaText.t("social_delete_post"), systemImage: "trash") }
                }
            } label: {
                headerIcon("ellipsis").foregroundStyle(AureaColors.lightTextMuted)
            }.buttonStyle(.plain).disabled(state.busy).accessibilityLabel(AureaText.t("mod_more_actions"))
                .accessibilityIdentifier("social.more.\(post.id)")
        }.padding(.leading, 14).padding(.trailing, 4).accessibilityIdentifier("social.author.\(post.id)")
    }

    private func postArtwork(_ post: SocialPost) -> some View {
        let file = post.media ?? post.asset
        let badge = AureaColors.lightBackground.opacity(0.94)
        return Group {
            if let media = post.media {
                SocialPostMedia(asset: media, api: state.api, onOpen: { comments = post })
            } else {
                Button { comments = post } label: {
                    VStack(alignment: .leading, spacing: 8) {
                        if !post.body.isEmpty { Text(post.body).lineLimit(6) }
                        if let asset = post.asset {
                            Label(asset.name, systemImage: asset.kind == "project" ? "square.stack" : "sparkles")
                                .font(.caption).lineLimit(1).foregroundStyle(AureaColors.lightAccentInk)
                        }
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.top, 10).padding(.leading, 14).padding(.bottom, 14).padding(.trailing, file == nil ? 14 : 65)
                    .frame(minHeight: file == nil ? 48 : 64, alignment: .top)
                    .contentShape(Rectangle())
                }.buttonStyle(.plain).accessibilityHint(AureaText.t("social_open_post"))
            }
        }.overlay(alignment: .bottomTrailing) {
            if let file {
                Button { download(file) } label: {
                    headerIcon("arrow.down.to.line").background(badge, in: Circle())
                }.buttonStyle(.plain).padding(8).accessibilityLabel(AureaText.t("social_download"))
                    .accessibilityIdentifier("social.download.\(post.id)")
                    .disabled(state.busy || (file.isMedia && savingMediaID != nil))
            }
        }.accessibilityIdentifier("social.post.\(post.id)")
    }

    private func postActions(_ post: SocialPost) -> some View {
        HStack(spacing: 0) {
            Button { state.run { try await state.like(post) } } label: {
                postActionLabel(post.liked ? "heart.fill" : "heart", count: post.likes,
                                tint: post.liked ? AureaColors.lightAccentInk : AureaColors.lightText)
            }.disabled(state.busy)
                .accessibilityLabel("\(post.likes) \(AureaText.t("social_likes"))").accessibilityIdentifier("social.like.\(post.id)")
            Button { comments = post } label: { postActionLabel("bubble", count: post.comments) }
                .accessibilityLabel("\(post.comments) \(AureaText.t("social_comments"))").accessibilityIdentifier("social.comments.\(post.id)")
            ShareLink(item: SocialAPI.shareText(post)) { postActionLabel("square.and.arrow.up") }
                .accessibilityLabel(AureaText.t("editor_compartilhar")).accessibilityIdentifier("social.share.\(post.id)")
            Spacer(minLength: 0)
        }.buttonStyle(.plain).padding(.horizontal, 4).frame(minHeight: 48)
    }

    private func postActionLabel(_ symbol: String, count: Int = 0, tint: Color = AureaColors.lightText) -> some View {
        HStack(spacing: 5) {
            Image(systemName: symbol).font(.system(size: 21, weight: AureaIconWeight.symbol)).foregroundStyle(tint)
            if count > 0 { Text("\(count)").font(.subheadline) }
        }.padding(.horizontal, 9).frame(minWidth: 44, minHeight: 48).contentShape(Rectangle())
    }

    private func saveMedia(_ asset: SocialAsset) {
        guard savingMediaID == nil else { return }
        savingMediaID = asset.id
        Task {
            defer { savingMediaID = nil }
            do {
                let permission = await PHPhotoLibrary.requestAuthorization(for: .addOnly)
                guard permission == .authorized || permission == .limited else { throw SocialFailure(code: "gallery_denied") }
                let file = try await state.api.download(asset)
                defer { try? FileManager.default.removeItem(at: file) }
                try await PHPhotoLibrary.shared().performChanges {
                    let request = PHAssetCreationRequest.forAsset()
                    request.addResource(with: asset.kind == "video" ? .video : .photo, fileURL: file, options: nil)
                }
                model.toast = AureaText.t("social_media_saved_gallery")
            } catch {
                model.toast = AureaText.t("social_media_save_failed")
            }
        }
    }
    private func download(_ asset: SocialAsset) {
        if asset.isMedia { saveMedia(asset); return }
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
                if let error { Text(error).foregroundStyle(AureaColors.lightDanger) }
                Button { save() } label: { if busy { ProgressView() } else { Text(AureaText.t("social_save")) } }
                    .disabled(busy || username.range(of: "^[a-zA-Z0-9_]{3,24}$", options: .regularExpression) == nil || name.trimmingCharacters(in: .whitespaces).isEmpty || name.count > 50 || bio.count > 240)
                    .accessibilityIdentifier("social.save")
            }.navigationTitle(AureaText.t("social_edit_profile")).navigationBarTitleDisplayMode(.inline)
                .toolbar { ToolbarItem(placement: .cancellationAction) { Button(AureaText.t("editor_fechar")) { dismiss() }.disabled(busy) } }
        }.tint(AureaColors.lightAccentInk).interactiveDismissDisabled(busy)
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

private func copySocialMedia(_ source: URL, maxBytes: Int, extension suffix: String) throws -> URL {
    let size = try source.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0
    guard (1...maxBytes).contains(size) else { throw SocialFailure(code: "file_too_large") }
    let destination = FileManager.default.temporaryDirectory.appendingPathComponent("aurea-media-\(UUID().uuidString).\(suffix)")
    try FileManager.default.copyItem(at: source, to: destination)
    return destination
}

private struct SocialMovieFile: Transferable {
    let url: URL
    static var transferRepresentation: some TransferRepresentation {
        FileRepresentation(contentType: .movie) { SentTransferredFile($0.url) } importing: { received in
            let ext = received.file.pathExtension.lowercased()
            let url = try copySocialMedia(received.file, maxBytes: 90 * 1024 * 1024, extension: ext == "mov" ? "mov" : "mp4")
            return SocialMovieFile(url: url)
        }
    }
}

private struct SocialImageFile: Transferable {
    let url: URL
    static var transferRepresentation: some TransferRepresentation {
        FileRepresentation(contentType: .image) { SentTransferredFile($0.url) } importing: { received in
            guard let source = CGImageSourceCreateWithURL(received.file as CFURL, nil),
                  let image = CGImageSourceCreateThumbnailAtIndex(source, 0, [
                    kCGImageSourceCreateThumbnailFromImageAlways: true,
                    kCGImageSourceCreateThumbnailWithTransform: true,
                    kCGImageSourceThumbnailMaxPixelSize: 1920,
                  ] as CFDictionary) else { throw SocialFailure(code: "invalid_image") }
            let url = FileManager.default.temporaryDirectory.appendingPathComponent("aurea-photo-\(UUID().uuidString).jpg")
            guard let output = CGImageDestinationCreateWithURL(url as CFURL, UTType.jpeg.identifier as CFString, 1, nil)
            else { throw SocialFailure(code: "invalid_image") }
            CGImageDestinationAddImage(output, image, [kCGImageDestinationLossyCompressionQuality: 0.87] as CFDictionary)
            guard CGImageDestinationFinalize(output) else {
                try? FileManager.default.removeItem(at: url)
                throw SocialFailure(code: "invalid_image")
            }
            return SocialImageFile(url: url)
        }
    }
}

@MainActor
private struct SocialCompose: View {
    @EnvironmentObject private var model: AureaModel
    @Environment(\.dismiss) private var dismiss
    let api: SocialAPI
    let feedFilters: Bool
    let canAnnounce: Bool
    let mediaPosts: Bool
    let posted: () -> Void
    @State private var bodyText = ""
    @State private var attachment: URL?
    @State private var kind = "preset"
    @State private var picker: String?
    @State private var pickedMedia: PhotosPickerItem?
    @State private var presets: [PanelPresetEntry] = []
    @State private var projects: [HomeProjectEntry] = []
    @State private var temporary: [URL] = []
    @State private var busy = false
    @State private var error: String?
    @State private var category: SocialCategory

    init(api: SocialAPI, feedFilters: Bool, canAnnounce: Bool, mediaPosts: Bool, initialCategory: SocialCategory, posted: @escaping () -> Void) {
        self.api = api
        self.feedFilters = feedFilters
        self.canAnnounce = canAnnounce
        self.mediaPosts = mediaPosts
        self.posted = posted
        _category = State(initialValue: initialCategory)
    }

    var body: some View {
        NavigationStack {
            Form {
                if feedFilters {
                    Picker(AureaText.t("social_post_type"), selection: $category) {
                        Text(AureaText.t("social_standard_post")).tag(SocialCategory.all)
                        Text(SocialCategory.showcase.title).tag(SocialCategory.showcase)
                        if canAnnounce { Text(SocialCategory.announcement.title).tag(SocialCategory.announcement) }
                    }.disabled(busy).accessibilityIdentifier("social.post.category")
                }
                TextField(AureaText.t("social_post_hint"), text: $bodyText, axis: .vertical).lineLimit(3...8).accessibilityIdentifier("social.post.body")
                if mediaPosts {
                    PhotosPicker(selection: $pickedMedia, matching: .any(of: [.images, .videos]), preferredItemEncoding: .current) {
                        Label(AureaText.t("social_attach_media"), systemImage: "photo.on.rectangle")
                    }.disabled(busy).accessibilityIdentifier("social.attach.media")
                }
                HStack {
                    Button(AureaText.t("social_attach_preset")) { presets = PanelPresetEntry.loadAll().filter { $0.textPreset == nil }; picker = "preset" }.buttonStyle(.borderless)
                    Spacer()
                    Button(AureaText.t("social_attach_project")) { projects = scanHomeProjects(); picker = "project" }.buttonStyle(.borderless)
                }.disabled(busy)
                if let attachment { HStack { Text(attachment.lastPathComponent); Spacer(); Button(AureaText.t("social_remove")) { self.attachment = nil }.disabled(busy) } }
                Text(AureaText.t(kind == "image" || kind == "video" ? "social_media_limits" : "social_file_limit"))
                    .font(.caption).foregroundStyle(AureaColors.lightTextMuted)
                if let error { Text(error).foregroundStyle(AureaColors.lightDanger) }
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
        }.tint(AureaColors.lightAccentInk).interactiveDismissDisabled(busy)
            .task(id: pickedMedia) {
                guard let item = pickedMedia else { return }
                busy = true; error = nil
                defer { busy = false }
                do {
                    let video = item.supportedContentTypes.contains { $0.conforms(to: .movie) }
                    let url: URL?
                    if video { url = try await item.loadTransferable(type: SocialMovieFile.self)?.url }
                    else { url = try await item.loadTransferable(type: SocialImageFile.self)?.url }
                    guard let url else { throw SocialFailure(code: "invalid_file") }
                    if Task.isCancelled { try? FileManager.default.removeItem(at: url); return }
                    temporary.append(url); attachment = url; kind = video ? "video" : "image"
                } catch {
                    if !Task.isCancelled {
                        self.error = AureaText.t((error as? SocialFailure)?.code == "file_too_large" ? "social_media_limits" : "social_media_error")
                    }
                }
            }
            .onDisappear { if !busy { for file in temporary { try? FileManager.default.removeItem(at: file) } } }
    }
    private func publish() {
        busy = true; error = nil
        Task { defer { busy = false }
            do {
                var asset: String?
                let media = kind == "image" || kind == "video"
                if let attachment { asset = try await api.upload(attachment, kind: kind).id }
                var post: [String: Any] = ["body": bodyText]
                if let asset { post[media ? "media" : "asset"] = asset }
                if feedFilters && category != .all { post["category"] = category.rawValue }
                do {
                    _ = try await api.call("/posts", method: "POST", body: post, as: SocialDone.self)
                } catch {
                    if media, let asset { _ = try? await api.call("/media/\(asset)", method: "DELETE", as: SocialDone.self) }
                    throw error
                }
                for file in temporary { try? FileManager.default.removeItem(at: file) }; temporary = []; posted()
            } catch {
                let code = (error as? SocialFailure)?.code
                if code == "invalid_video" || code == "invalid_image" { self.error = AureaText.t("social_media_error") }
                else if code == "file_too_large" && (kind == "image" || kind == "video") { self.error = AureaText.t("social_media_limits") }
                else { self.error = (error as? SocialFailure)?.message ?? AureaText.t("social_error") }
            }
        }
    }
}

@MainActor
private struct SocialComments: View {
    @Environment(\.dismiss) private var dismiss
    @EnvironmentObject private var model: AureaModel
    let post: SocialPost; let own: SocialProfile?; let replies: Bool; let api: SocialAPI
    let openProfile: (String) -> Void
    let onCommentDelta: (Int) -> Void
    @State private var items: [SocialComment] = []
    @State private var cursor: String?
    @State private var text = ""
    @State private var busy = false
    @State private var error: String?
    @State private var replyTo: SocialComment?
    @State private var reporting: SocialComment?
    @State private var submittedComment: String?
    @FocusState private var commentFieldFocused: Bool
    var body: some View {
        NavigationStack {
            VStack {
                if busy { ProgressView() }
                if let error { Text(error).foregroundStyle(AureaColors.lightDanger); Button(AureaText.t("social_refresh")) { run { try await load() } } }
                ScrollViewReader { scroll in
                ScrollView { LazyVStack(alignment: .leading, spacing: 16) {
                    Button { openProfile(post.author.id) } label: { SocialAuthor(profile: post.author) }.buttonStyle(.plain)
                    if let media = post.media { SocialPostMedia(asset: media, api: api, detail: true) }
                    if !post.body.isEmpty { Text(post.body).textSelection(.enabled) }
                    Divider()
                    Text(AureaText.t("social_comments")).font(.headline)
                    if items.isEmpty && !busy { Text(AureaText.t("social_no_comments")).foregroundStyle(AureaColors.lightTextMuted) }
                    ForEach(items) { c in VStack(alignment: .leading, spacing: 6) {
                        Button { openProfile(c.author.id) } label: { SocialAuthor(profile: c.author) }.buttonStyle(.plain)
                        if let parent = c.replyName {
                            Text(AureaText.t("social_reply_to", parent)).font(.caption).foregroundStyle(AureaColors.lightAccentInk)
                        }
                        Text(c.body).textSelection(.enabled)
                        HStack {
                            if replies {
                                Button(AureaText.t("social_reply")) { replyTo = c }.frame(minHeight: 44)
                                    .accessibilityIdentifier("social.comment.reply.\(c.id)")
                            }
                            if own?.id == c.author.id {
                                Button(AureaText.t("social_delete"), role: .destructive) { run {
                                    _ = try await api.call("/comments/\(c.id)", method: "DELETE", as: SocialDone.self)
                                    onCommentDelta(-1); try await load()
                                } }.frame(minHeight: 44)
                            } else {
                                Button(AureaText.t("mod_report")) { reporting = c }.frame(minHeight: 44)
                            }
                        }.disabled(busy)
                    }.padding(.leading, c.replyName == nil ? 0 : 16).id(c.id) }
                    if cursor != nil { Button(AureaText.t("social_more")) { run { try await load(more: true) } }.frame(minHeight: 44) }
                }.padding(20) }
                    .onChange(of: submittedComment) { id in
                        if let id { scroll.scrollTo(id, anchor: .bottom) }
                    }
                }
                if let parent = replyTo {
                    HStack {
                        Text(AureaText.t("social_reply_to", parent.author.name.isEmpty ? parent.author.username : parent.author.name))
                            .font(.caption).foregroundStyle(AureaColors.lightAccentInk)
                        Spacer()
                        Button(AureaText.t("common_cancel")) { replyTo = nil }.disabled(busy)
                    }.padding(.horizontal, 16)
                }
                HStack {
                    TextField(AureaText.t("social_comment_hint"), text: $text, axis: .vertical).lineLimit(1...3).textFieldStyle(.roundedBorder)
                        .focused($commentFieldFocused).accessibilityIdentifier("social.comment.body")
                    Button(AureaText.t("social_send")) { run {
                        let parent = replyTo
                        let message = text.trimmingCharacters(in: .whitespacesAndNewlines)
                        var fields = ["body": message]
                        if let parent { fields["replyTo"] = parent.id }
                        let result = try await api.call("/posts/\(post.id)/comments", method: "POST", body: fields, as: SocialDone.self)
                        commentFieldFocused = false
                        text = ""; replyTo = nil; onCommentDelta(1)
                        if cursor == nil { try await load() }
                        else if let id = result.id, let own {
                            items.append(SocialComment(id: id, body: message, createdAt: Date().timeIntervalSince1970 * 1000,
                                author: own, replyName: parent.map { $0.author.name.isEmpty ? $0.author.username : $0.author.name }))
                        }
                        submittedComment = result.id
                    } }.disabled(busy || text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty || text.count > 1000).frame(minHeight: 44)
                }.padding(16)
            }.navigationTitle(AureaText.t("social_post_title")).navigationBarTitleDisplayMode(.inline)
                .toolbar { ToolbarItem(placement: .cancellationAction) { Button(AureaText.t("editor_fechar")) { dismiss() }.disabled(busy) } }
        }.tint(AureaColors.lightAccentInk).interactiveDismissDisabled(busy).task { run { try await load() } }
            .confirmationDialog(AureaText.t("mod_report_title"),
                isPresented: Binding(get: { reporting != nil }, set: { if !$0 { reporting = nil } }),
                titleVisibility: .visible, presenting: reporting) { comment in
                ForEach(socialReportReasons, id: \.0) { reason in
                    Button(AureaText.t(reason.1)) { run {
                        _ = try await api.call("/reports", method: "POST", body: ["kind": "comment", "id": comment.id, "reason": reason.0], as: SocialDone.self)
                        model.toast = AureaText.t("mod_report_thanks")
                    } }
                }
            }
    }
    private func run(_ action: @escaping () async throws -> Void) {
        guard !busy else { return }; busy = true; error = nil
        Task { defer { busy = false }; do { try await action() } catch { self.error = (error as? SocialFailure)?.message ?? AureaText.t("social_error") } }
    }
    private func load(more: Bool = false) async throws {
        let result = try await api.call("/posts/\(post.id)/comments" + (more ? cursor.map { "?cursor=\(SocialAPI.query($0))" } ?? "" : ""), as: SocialPage<SocialComment>.self)
        let displayed = more ? Set(items.map(\.id)) : []
        items = more ? (items + result.items.filter { !displayed.contains($0.id) }).sorted {
            ($0.createdAt, $0.id) < ($1.createdAt, $1.id)
        } : result.items
        cursor = result.cursor
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
        }.tint(AureaColors.lightAccentInk).task { load() }
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
