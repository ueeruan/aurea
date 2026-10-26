// "Licenças de código aberto" (0.3.2). Lista os avisos que vão DENTRO do app e
// mostra o texto do próprio arquivo — nada é redigitado aqui. Par do Android:
// home/LicensesScreen.kt (as mesmas pastas):
//   licenses/oss/            engine/assets/licenses (THIRD_PARTY_HYPER.md…),
//                            hyper/<modelo>/LICENSE + manifest, ai/, rotobrush/
//                            (fase "Stage open-source licenses" do Xcode);
//   licenses/native-export/  FFmpeg, OpenH264, Kvazaar (fase do CMake);
//   Fonts/LICENSE-*.txt      as fontes embarcadas.
// accessibilityIdentifier: licencas.fechar · licencas.lista · licencas.item.<n> · licencas.texto
import SwiftUI

struct LicenseFile: Identifiable, Equatable {
    let url: URL
    let path: String   // relativo ao bundle
    let group: Int
    var id: String { path }
    var title: String {
        var p = path
        for prefix in ["licenses/", "oss/", "native-export/", "Fonts/"] where p.hasPrefix(prefix) { p.removeFirst(prefix.count) }
        return p.split(separator: "/").map { part -> String in
            let s = String(part)
            guard let dot = s.lastIndex(of: "."), dot != s.startIndex else { return s }
            return String(s[..<dot])
        }.joined(separator: " · ")
    }

    static func group(of path: String) -> Int {
        if path.hasPrefix("licenses/oss/hyper") || path.uppercased().contains("HYPER") { return 0 }
        if path.hasPrefix("licenses/native-export") { return 1 }
        if path.hasPrefix("licenses/oss/ai") || path.hasPrefix("licenses/oss/rotobrush") { return 2 }
        if path.hasPrefix("Fonts/") { return 4 }
        return 3
    }

    static func isNotice(_ name: String) -> Bool {
        let lower = name.lowercased()
        return lower.hasSuffix(".txt") || lower.hasSuffix(".md") || lower.hasSuffix(".json")
            || lower.hasPrefix("license") || lower.hasPrefix("notice") || lower.hasPrefix("copying")
    }

    /// Os arquivos de aviso do bundle, em ordem: Hyper, export, IA, demais, fontes.
    static func all(in bundle: Bundle = .main) -> [LicenseFile] {
        guard let root = bundle.resourceURL else { return [] }
        var out: [LicenseFile] = []
        let fm = FileManager.default
        let licenses = root.appendingPathComponent("licenses", isDirectory: true)
        if let walker = fm.enumerator(at: licenses, includingPropertiesForKeys: [.isRegularFileKey]) {
            for case let url as URL in walker where (try? url.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true {
                guard isNotice(url.lastPathComponent) else { continue }
                let relative = "licenses/" + url.path.dropFirst(licenses.path.count + 1)
                out.append(LicenseFile(url: url, path: relative, group: group(of: relative)))
            }
        }
        let fonts = root.appendingPathComponent("Fonts", isDirectory: true)
        for name in (try? fm.contentsOfDirectory(atPath: fonts.path)) ?? [] where name.hasPrefix("LICENSE-") {
            out.append(LicenseFile(url: fonts.appendingPathComponent(name), path: "Fonts/" + name, group: 4))
        }
        return out.sorted { ($0.group, $0.path) < ($1.group, $1.path) }
    }
}

struct LicensesView: View {
    let onDismiss: () -> Void
    @State private var files: [LicenseFile]?
    @State private var open: LicenseFile?
    @State private var text: String?

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 4) {
                Button { if open != nil { open = nil } else { onDismiss() } } label: {
                    Image(systemName: "chevron.left").font(.system(size: 18, weight: AureaIconWeight.symbol)).foregroundStyle(AureaColors.text)
                        .frame(width: 40, height: 40)
                }
                .buttonStyle(.plain)
                .accessibilityLabel(AureaText.t("common_back")).accessibilityIdentifier("licencas.fechar")
                Text(open?.title ?? AureaText.t("licenses_oss_title")).font(.aurea(size: 17, weight: .semibold)).lineLimit(1)
                    .accessibilityAddTraits(.isHeader)
                Spacer()
            }
            .frame(height: 44).padding(.horizontal, 6)
            if open != nil {
                ScrollView {
                    Text(text ?? "…").font(.system(size: 12, design: .monospaced)).textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading).padding(16)
                        .accessibilityIdentifier("licencas.texto")
                }
            } else {
                List {
                    Text(AureaText.t("licenses_oss_intro")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                        .listRowBackground(Color.clear)
                    if let files, files.isEmpty {
                        Text(AureaText.t("licenses_none")).font(.aurea(size: 14)).listRowBackground(Color.clear)
                    }
                    ForEach(Array((files ?? []).enumerated()), id: \.element.id) { index, file in
                        if index == 0 || files?[index - 1].group != file.group {
                            Text(AureaText.t(groupKey(file.group))).font(.aurea(size: 12, weight: .bold)).foregroundStyle(AureaColors.muted)
                                .listRowBackground(Color.clear).accessibilityAddTraits(.isHeader)
                        }
                        Button { open = file } label: {
                            VStack(alignment: .leading, spacing: 2) {
                                Text(file.title).font(.aurea(size: 15, weight: .medium)).foregroundStyle(AureaColors.text)
                                Text(file.path).font(.aurea(size: 11)).foregroundStyle(AureaColors.muted).lineLimit(1)
                            }.frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
                        }
                        .buttonStyle(.plain)
                        .listRowBackground(AureaColors.surface)
                        .accessibilityLabel(file.title)
                        .accessibilityIdentifier("licencas.item.\(index)")
                    }
                }
                .listStyle(.plain).scrollContentBackground(.hidden)
                .accessibilityIdentifier("licencas.lista")
            }
        }
        .background(AureaColors.background.ignoresSafeArea()).foregroundStyle(AureaColors.text)
        .preferredColorScheme(.dark)
        .task { if files == nil { files = await Task.detached { LicenseFile.all() }.value } }
        .onChange(of: open) { file in
            text = nil
            guard let file else { return }
            Task {
                let loaded = await Task.detached { try? String(contentsOf: file.url, encoding: .utf8) }.value
                text = loaded ?? AureaText.t("licenses_read_failed")
            }
        }
    }

    private func groupKey(_ group: Int) -> String {
        switch group {
        case 0: return "licenses_group_hyper"
        case 1: return "licenses_group_export"
        case 2: return "licenses_group_ai"
        case 4: return "licenses_group_fonts"
        default: return "licenses_group_other"
        }
    }
}
