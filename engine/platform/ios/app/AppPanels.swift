import SwiftUI

@MainActor final class LiveNotices: ObservableObject {
    struct Notice: Identifiable {
        var id: String; var text: String; var level: String; var link: URL?; var expires: Date?; var popup: Bool
    }
    @Published private(set) var visible: [Notice] = []
    @Published var popup: Notice?
    private var cached: [Notice] = []
    private var dismissed = Set(UserDefaults.standard.stringArray(forKey: "notices.dismissed") ?? [])
    private var seen = Set(UserDefaults.standard.stringArray(forKey: "notices.seen") ?? [])
    private var lastFetch = Date.distantPast
    init() {
        if let data = UserDefaults.standard.data(forKey: "notices.cache"), let list = try? JSONSerialization.jsonObject(with: data) as? [[String: Any]] { cached = Self.parse(list) }
        expire()
    }
    private func expire() {
        visible = cached.filter { !dismissed.contains($0.id) && ($0.expires == nil || $0.expires! > Date()) }
        popup = visible.first { $0.popup && !seen.contains($0.id) }
    }
    func dismiss(_ id: String) { dismissed.insert(id); UserDefaults.standard.set(Array(dismissed.suffix(100)), forKey: "notices.dismissed"); expire() }
    func closePopup() { if let id = popup?.id { seen.insert(id); UserDefaults.standard.set(Array(seen.suffix(100)), forKey: "notices.seen") }; popup = nil }
    func refresh() async {
        expire(); guard Date().timeIntervalSince(lastFetch) >= 600 else { return }; lastFetch = Date()
        do {
            var request = URLRequest(url: URL(string: "https://mural-do-aurea.aureaapp.workers.dev/aviso")!)
            request.timeoutInterval = 8
            let (stream, response) = try await URLSession.shared.bytes(for: request)
            guard (response as? HTTPURLResponse)?.statusCode == 200 else { return }
            var bytes = Data()
            for try await byte in stream { guard bytes.count < 65_536 else { return }; bytes.append(byte) }
            guard let root = try JSONSerialization.jsonObject(with: bytes) as? [String: Any] else { return }
            let list = root["avisos"] as? [[String: Any]] ?? (root["aviso"] as? [String: Any]).map { [$0] } ?? []
            cached = Self.parse(list)
            // Cache vazio nao substitui o anterior: sem dado, fica o que ja havia.
            if let data = AureaJSONData(list, false) { UserDefaults.standard.set(data, forKey: "notices.cache") }
            expire()
        } catch { /* O cache continua visível quando não há conexão. */ }
    }
    private static func parse(_ list: [[String: Any]]) -> [Notice] {
        let iso = ISO8601DateFormatter()
        return list.prefix(3).compactMap { row in
            let id = (row["id"] as? String ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
            let text = (row["texto"] as? String ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
            guard !id.isEmpty, !text.isEmpty else { return nil }
            let expiry = (row["ate"] as? String ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
            var date = iso.date(from: expiry)
            if date == nil { iso.formatOptions = [.withInternetDateTime, .withFractionalSeconds]; date = iso.date(from: expiry); iso.formatOptions = [.withInternetDateTime] }
            guard expiry.isEmpty || expiry == "null" || date != nil else { return nil }
            let raw = URL(string: row["link"] as? String ?? "")
            let link = raw?.host != nil && ["http", "https"].contains(raw?.scheme ?? "") ? raw : nil
            return Notice(id: id, text: text, level: row["nivel"] as? String ?? "info", link: link, expires: date, popup: row["popup"] as? Bool ?? false)
        }
    }
}

struct LiveNoticeBanners: View {
    @EnvironmentObject private var model: AureaModel
    @StateObject private var notices = LiveNotices()
    var body: some View {
        VStack(spacing: 0) {
            ForEach(notices.visible) { notice in
                VStack(alignment: .leading, spacing: 0) {
                    Text(notice.text).aureaFont(.of(12, tracking: 0.4, lineHeight: 4.0 / 3.0))
                        .foregroundStyle(Color(hex: notice.level == "problema" ? 0xF9DEDC : notice.level == "atencao" ? 0xFFD8E4 : 0xE8DEF8))
                        .fixedSize(horizontal: false, vertical: true)
                    HStack(spacing: 0) {
                        if let link = notice.link { Link(AureaText.t("notice_more"), destination: link).padding(.horizontal, 12).frame(minWidth: 64, minHeight: 48) }
                        Button(AureaText.t("notice_dismiss")) { notices.dismiss(notice.id) }.padding(.horizontal, 12).frame(minWidth: 64, minHeight: 48)
                    }.font(.aurea(size: 14, weight: .medium)).tracking(0.1).buttonStyle(.plain).foregroundStyle(AureaColors.text)
                }.padding(.horizontal, 16).padding(.vertical, 4).frame(maxWidth: .infinity, alignment: .leading)
                    // Material Surface ends at the status-bar inset. SwiftUI's
                    // ShapeStyle background otherwise expands behind that bar.
                    .background(Color(hex: notice.level == "problema" ? 0x8C1D18 : notice.level == "atencao" ? 0x633B48 : 0x4A4458), ignoresSafeAreaEdges: [])
            }
        }
            .task { while !Task.isCancelled { await notices.refresh(); try? await Task.sleep(nanoseconds: 60_000_000_000) } }
            .onChange(of: notices.popup?.id) { _ in
                guard let notice = notices.popup else { return }
                model.liveNoticePopup = LiveNoticePopupRequest(notice: notice) {
                    notices.closePopup(); model.liveNoticePopup = nil
                }
            }
    }
}

struct LiveNoticePopupRequest {
    let notice: LiveNotices.Notice
    let onClose: () -> Void
}

struct LiveNoticePopup: View {
    @Environment(\.openURL) private var openURL
    let request: LiveNoticePopupRequest
    var body: some View {
        ZStack {
            Color.black.opacity(0.32).ignoresSafeArea().onTapGesture(perform: request.onClose)
            VStack(alignment: .leading, spacing: 0) {
                Text(AureaText.t("notice_title")).font(.aurea(size: 24)).padding(.bottom, 16)
                Text(request.notice.text).font(.aurea(size: 14)).tracking(0.25).foregroundStyle(AureaColors.muted).fixedSize(horizontal: false, vertical: true)
                HStack(spacing: 8) {
                    Spacer(minLength: 0)
                    if let link = request.notice.link {
                        Button(AureaText.t("notice_more")) { request.onClose(); openURL(link) }.frame(minHeight: 48)
                    }
                    Button(AureaText.t("notice_ok"), action: request.onClose).frame(minHeight: 48)
                }.font(.aurea(size: 14, weight: .medium)).foregroundStyle(AureaColors.accent).padding(.top, 24).buttonStyle(.plain)
            }.padding(24).frame(maxWidth: 560).background(AureaColors.surfaceHigh, in: RoundedRectangle(cornerRadius: 28)).padding(.horizontal, 40)
        }.foregroundStyle(AureaColors.text)
    }
}

/// `editor/ProjectSettingsSheet.kt`: changes apply immediately to the core.
/// The source sheet has four controls: aspect, resolution, fps and background.
struct ProjectSettingsPanel: View {
    @EnvironmentObject private var model: AureaModel
    var onDismiss: () -> Void = {}
    @State private var free = false
    @State private var menu: String?
    @State private var pickingBackground = false
    private let aspects: [(String, Float)] = [("16:9", 16 / 9), ("9:16", 9 / 16), ("4:5", 4 / 5), ("1:1", 1), ("4:3", 4 / 3)]
    private let resolutions: [(Int, String)] = [(480, "480p (SD)"), (720, "720p (HD)"), (1080, "1080p (FHD)"), (1440, "1440p (QHD)"), (2160, "2160p (4K)")]
    private let frameRates = [24, 25, 30, 50, 60]
    private var id: UInt64 { (model.composition["id"] as? NSNumber)?.uint64Value ?? 0 }
    private var width: Int { Int(model.compositionWidth) }
    private var height: Int { Int(model.compositionHeight) }
    private var ratio: Float { height > 0 ? Float(width) / Float(height) : 16 / 9 }
    private var shortest: Int { min(width, height) }
    private var preset: String? { aspects.first { abs($0.1 - ratio) < 0.01 }?.0 }
    private var background: [Float] {
        let values = (model.composition["background"] as? [NSNumber] ?? []).map(\.floatValue)
        return values.count == 4 ? values : [0, 0, 0, 1]
    }
    private var backgroundName: String {
        if background.prefix(3).allSatisfy({ abs($0) < 0.01 }) { return AureaText.t("sh_bg_black") }
        if background.prefix(3).allSatisfy({ abs($0 - 1) < 0.01 }) { return AureaText.t("sh_bg_white") }
        return AureaText.t("editor_personalizada")
    }
    private var cap: (Int, Int) {
        let values = (model.composition["sizeCap"] as? [NSNumber] ?? []).map(\.intValue)
        return values.count >= 2 ? (values[0], values[1]) : (0, 0)
    }

    var body: some View {
        GeometryReader { bounds in
            ZStack(alignment: .bottom) {
                Color.black.opacity(0.38).ignoresSafeArea().onTapGesture(perform: close)
                VStack(spacing: 0) {
                    Capsule().fill(AureaColors.muted.opacity(0.5)).frame(width: 36, height: 4).padding(.top, 6)
                    ScrollView {
                        VStack(spacing: 0) {
                            HStack(spacing: 0) {
                                Button(action: close) {
                                    CupertinoGlyph.text(CupertinoGlyph.Xmark, size: 20).frame(width: 44, height: 44).contentShape(Rectangle())
                                }.buttonStyle(.plain).accessibilityLabel(AureaText.t("editor_fechar"))
                                Text(AureaText.t("editor_projeto_cbe9")).font(.aurea(size: 16, weight: .bold))
                                Spacer()
                            }.padding(.leading, 6).padding(.trailing, 18)
                            aspectRow
                            if free {
                                settingLine("editor_tamanho") {
                                    HStack(spacing: 0) {
                                        sizeBox(width, title: "editor_largura") { setSize(Int($0.rounded()), height) }
                                        Text("×").font(.aurea(size: 15)).foregroundStyle(AureaColors.muted).padding(.horizontal, 10)
                                        sizeBox(height, title: "editor_altura") { setSize(width, Int($0.rounded())) }
                                        Text("px").font(.aurea(size: 13)).foregroundStyle(AureaColors.muted).padding(.leading, 8)
                                    }
                                }
                            }
                            settingLine("editor_resolucao") {
                                dropdown(resolutions.first { $0.0 == shortest }?.1 ?? "\(shortest)p", key: "res")
                            }
                            settingLine("editor_quadros_segundo") {
                                dropdown(homeFormatFps(model.compositionFps).replacingOccurrences(of: ".", with: ",") + " fps", key: "fps")
                            }
                            settingLine("editor_plano_fundo") {
                                dropdown(backgroundName, key: "bg", swatch: Color(.sRGB, red: Double(background[0]), green: Double(background[1]), blue: Double(background[2]), opacity: 1))
                            }
                            Spacer().frame(height: 16)
                        }.padding(.bottom, 12)
                    }
                }
                .frame(height: min(bounds.size.height * 0.7, free ? 378 : 320))
                .frame(maxWidth: .infinity).background(AureaColors.editorPanel)
                .clipShape(TopCorners())
            }
            .overlayPreferenceValue(MenuAnchors.self) { anchors in
                GeometryReader { geometry in
                    if let key = menu, let anchor = anchors[key] {
                        let rect = geometry[anchor], items = menuItems(key)
                        let popupWidth: CGFloat = min(220, geometry.size.width - 16)
                        let popupHeight = CGFloat(items.count) * 40 + 8
                        let x = min(max(8, rect.maxX - popupWidth), max(8, geometry.size.width - popupWidth - 8))
                        let below = rect.maxY + 4
                        let y = max(8, below + popupHeight <= geometry.size.height - 8 ? below : rect.minY - 4 - popupHeight)
                        Color.clear.contentShape(Rectangle()).onTapGesture { menu = nil }
                        popup(items).frame(width: popupWidth, height: popupHeight).position(x: x + popupWidth / 2, y: y + popupHeight / 2)
                    }
                }
            }
        }.foregroundStyle(AureaColors.text).onAppear { free = preset == nil }.onDisappear { finishColor() }
    }
    private var aspectRow: some View {
        HStack(spacing: 0) {
            ForEach(Array(aspects.enumerated()), id: \.offset) { index, option in
                if index > 0 { Spacer(minLength: 0) }
                let on = !free && preset == option.0
                Button {
                    free = false
                    if preset != option.0 { applyAspect(option.1) }
                } label: {
                    Text(option.0).font(.aurea(size: 12, weight: .bold)).foregroundStyle(on ? AureaColors.onAccent : AureaColors.text)
                        .frame(width: option.1 >= 1 ? 44 : CGFloat(44 * max(0.5, option.1) + 6),
                               height: option.1 >= 1 ? CGFloat(44 / option.1 + 6) : 44)
                        .background(on ? AureaColors.accent : AureaColors.chip, in: RoundedRectangle(cornerRadius: 6))
                }.buttonStyle(.plain).accessibilityLabel(AureaText.t("sh_aspect_ratio_desc", option.0))
            }
            Spacer(minLength: 0)
            Button { free = true } label: {
                CupertinoGlyph.text(CupertinoGlyph.Pencil, size: 18, color: free ? AureaColors.onAccent : AureaColors.text)
                    .frame(width: 40, height: 40).background(free ? AureaColors.accent : AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
            }.buttonStyle(.plain).accessibilityLabel(AureaText.t("editor_tamanho_livre"))
        }.padding(.horizontal, 18).frame(height: 64)
    }
    private func settingLine<Content: View>(_ key: String, @ViewBuilder content: () -> Content) -> some View {
        HStack(spacing: 0) {
            Text(AureaText.t(key)).font(.aurea(size: 14)).lineLimit(2).padding(.trailing, 8).frame(width: 132, alignment: .leading)
            content().frame(maxWidth: .infinity)
        }.padding(.horizontal, 18).padding(.vertical, 6)
    }
    private func dropdown(_ value: String, key: String, swatch: Color? = nil) -> some View {
        Button { menu = key } label: {
            HStack(spacing: 0) {
                if let swatch {
                    RoundedRectangle(cornerRadius: 5).fill(swatch).frame(width: 24, height: 24)
                        .overlay(RoundedRectangle(cornerRadius: 5).stroke(AureaColors.border, lineWidth: 1)).padding(.trailing, 10)
                }
                Text(value).font(.aurea(size: 15, weight: .semibold)).lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
                CupertinoGlyph.text(CupertinoGlyph.ChevronDown, size: 15, color: AureaColors.muted)
            }.padding(.horizontal, 14).frame(height: 46).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 12))
        }.buttonStyle(.plain).anchorPreference(key: MenuAnchors.self, value: .bounds) { [key: $0] }
    }
    private func sizeBox(_ value: Int, title: String, onValue: @escaping (Float) -> Void) -> some View {
        Button {
            model.numericKeypad = KeypadRequest(title: AureaText.t(title), value: Float(value), unit: "px", min: 16, max: 8192, decimals: 0, onValue: onValue)
        } label: {
            Text(String(value)).font(.aurea(size: 15, weight: .semibold).monospacedDigit())
                .frame(maxWidth: .infinity).frame(height: 46).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 12))
        }.buttonStyle(.plain)
    }
    private struct MenuItem: Identifiable {
        let label: String; let checked: Bool; let action: () -> Void
        var id: String { label }
    }
    private func menuItems(_ key: String) -> [MenuItem] {
        if key == "res" {
            return resolutions.map { side, label in
                MenuItem(label: label, checked: shortest == side) { let size = sizeFor(side, ratio: ratio); setSize(size.0, size.1) }
            }
        }
        if key == "fps" {
            return frameRates.map { fps in
                MenuItem(label: "\(fps) fps", checked: abs(model.compositionFps - Double(fps)) < 0.01) {
                    model.mutate { $0.setComposition(id, fps: Double(fps)) }; model.refreshModel(force: true)
                }
            }
        }
        return [
            MenuItem(label: AureaText.t("sh_bg_black"), checked: backgroundName == AureaText.t("sh_bg_black")) { setBackground([0, 0, 0, 1]) },
            MenuItem(label: AureaText.t("sh_bg_white"), checked: backgroundName == AureaText.t("sh_bg_white")) { setBackground([1, 1, 1, 1]) },
            MenuItem(label: AureaText.t("editor_outra_cor"), checked: backgroundName == AureaText.t("editor_personalizada")) {
                model.engine.run { $0.beginUndoGroup() }; pickingBackground = true
                // ProjectSettingsSheet.kt passes composition background as display sRGB.
                model.colorSheet = ColorSheetRequest(title: AureaText.t("ds_cor"), initial: background, withAlpha: false,
                    onChange: { r, g, b, _ in setBackground([r, g, b, 1]) }, onDone: finishColor)
            },
        ]
    }
    private func popup(_ items: [MenuItem]) -> some View {
        VStack(spacing: 0) {
            ForEach(items) { item in
                Button { menu = nil; item.action() } label: {
                    HStack(spacing: 10) {
                        Rectangle().fill(item.checked ? AureaColors.accent : .clear).frame(width: 4, height: 24)
                        Text(item.label).font(.aurea(size: 13)).foregroundStyle(item.checked ? AureaColors.accent : AureaColors.text)
                            .lineLimit(1).frame(maxWidth: .infinity, alignment: .leading)
                    }.padding(.trailing, 10).frame(height: 40).contentShape(Rectangle())
                }.buttonStyle(.plain)
            }
        }.padding(.vertical, 4).background(AureaColors.pill, in: RoundedRectangle(cornerRadius: 8))
    }
    private func applyAspect(_ target: Float) {
        var size = sizeFor(shortest, ratio: target)
        if !fits(size.0, size.1), cap.0 > 0, cap.1 > 0 {
            let factor = min(Float(cap.0) / Float(max(size.0, size.1)), Float(cap.1) / Float(min(size.0, size.1)))
            size = (even(Float(size.0) * factor), even(Float(size.1) * factor))
        }
        setSize(size.0, size.1)
    }
    private func sizeFor(_ short: Int, ratio: Float) -> (Int, Int) {
        ratio >= 1 ? (even(Float(short) * ratio), even(Float(short))) : (even(Float(short)), even(Float(short) / ratio))
    }
    private func even(_ value: Float) -> Int { max(2, Int((value / 2).rounded()) * 2) }
    private func fits(_ width: Int, _ height: Int) -> Bool { cap.0 <= 0 || (max(width, height) <= cap.0 && min(width, height) <= cap.1) }
    private func setSize(_ width: Int, _ height: Int) {
        let w = even(Float(width)), h = even(Float(height))
        guard fits(w, h) else { model.toast = AureaText.t("sh_device_exports_up_to", String(cap.0), String(cap.1)); return }
        model.mutate { $0.setComposition(id, width: UInt32(w), height: UInt32(h)) }; model.refreshModel(force: true)
    }
    private func setBackground(_ values: [Float]) {
        guard values.count >= 3 else { return }
        model.mutate { $0.setComposition(id, backgroundR: values[0], g: values[1], b: values[2], a: 1) }; model.refreshModel(force: true)
    }
    private func finishColor() {
        if pickingBackground { model.engine.run { $0.endUndoGroup() }; pickingBackground = false }
    }
    private func close() { finishColor(); onDismiss() }
    private struct MenuAnchors: PreferenceKey {
        static var defaultValue: [String: Anchor<CGRect>] = [:]
        static func reduce(value: inout [String: Anchor<CGRect>], nextValue: () -> [String: Anchor<CGRect>]) { value.merge(nextValue(), uniquingKeysWith: { _, next in next }) }
    }
    private struct TopCorners: Shape {
        var radius: CGFloat = 18
        func path(in rect: CGRect) -> Path {
            var path = Path()
            path.move(to: CGPoint(x: 0, y: rect.maxY)); path.addLine(to: CGPoint(x: 0, y: radius))
            path.addQuadCurve(to: CGPoint(x: radius, y: 0), control: .zero)
            path.addLine(to: CGPoint(x: rect.maxX - radius, y: 0)); path.addQuadCurve(to: CGPoint(x: rect.maxX, y: radius), control: CGPoint(x: rect.maxX, y: 0))
            path.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY)); path.closeSubpath(); return path
        }
    }

}
