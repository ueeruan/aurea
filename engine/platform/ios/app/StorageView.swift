// =============================================================================
//  Aurea / platform / ios / app / StorageView.swift
//
//  Armazenamento (0.5.2) — par do AppStorage.kt, CacheStorage.kt e
//  StorageDialog.kt do Android. A regra de o que pode sair do disco é do MOTOR
//  (project/MediaStorage.cpp, pela ponte): aqui só as pastas do app, a medida
//  e a tela.
//
//  Pasta de mídia (a ÚNICA de onde mídia é apagada, e só pela porta do motor):
//  Documents/Media — importações, mídia de arquivos de projeto (Media/Projetos),
//  PSD/GIF expandidos e sequências. Media/Modelos é biblioteca: nunca sai.
//
//  Caches (limpeza ao abrir e ao fim de cada export, como o Android):
//  prévias de efeito e forma de onda até 64 MB cada, a cópia do .zip de
//  sequência e o pacote de plugin zerados na abertura. O PreRender e a mídia
//  otimizada são do motor: ele apaga as pastas de PreRender sem dono na
//  abertura e mantém os proxies no teto dele (2 GB, nunca o último 1 GB
//  livre); a tela pede a ele para limpar. No tmp, as pastas de
//  importação, Hyper, Mocap, legendas, comunidade, loja e mural com mais de
//  24 h que nenhum projeto usa.
// =============================================================================
import CryptoKit
import Darwin
import Foundation
import SwiftUI

enum AureaStorage {
    static var projectsDirectory: String { AureaPaths.documents.path }
    static var managedRoots: [String] { [AureaPaths.media.path] }
    /// Bibliotecas dentro de Media (modelos 3D com as texturas): nunca saem daqui.
    static var keepRoots: [String] { [AureaPaths.media.appendingPathComponent("Modelos", isDirectory: true).path] }
    /// Importação mais nova que isto nunca é órfã (pode não ter chegado ao .aurea ainda).
    static let orphanMinAge: Int64 = 24 * 3600
    static let cacheLimit: Int64 = 64 << 20

    static func size(_ bytes: Int64) -> String { ByteCountFormatter.string(fromByteCount: bytes, countStyle: .file) }

    // MARK: Apagar projeto (S3)

    struct Release { let paths: [String]; let bytes: Int64 }

    /// Antes de apagar os arquivos de projeto: o motor lê TODOS (com .bak e
    /// cópias) e devolve a mídia que só estes usavam. nil = nada sai (projeto
    /// ilegível, export em andamento). Fora da main.
    static func planRelease(_ engine: AureaEngine, projects: [String], editorOpen: Bool) -> Release? {
        guard !projects.isEmpty,
              let r = engine.mediaReleasePlan(in: projectsDirectory, deleted: projects, roots: managedRoots,
                                              keep: keepRoots, editorOpen: editorOpen),
              (r["blocked"] as? NSNumber)?.boolValue == false else {
            NSLog("AureaStorage: apagar projeto: midia mantida (projeto ilegivel ou export em andamento)")
            return nil
        }
        return Release(paths: r["paths"] as? [String] ?? [], bytes: (r["bytes"] as? NSNumber)?.int64Value ?? 0)
    }

    /// Depois de apagar os arquivos de projeto: a mídia do plano, pela porta do motor.
    @discardableResult
    static func release(_ engine: AureaEngine, _ plan: Release) -> Int64 {
        guard !plan.paths.isEmpty else { return 0 }
        let r = engine.removeManagedMedia(plan.paths, roots: managedRoots, keep: keepRoots)
        NSLog("AureaStorage: apagar projeto: %ld arquivo(s) de midia liberado(s)", r.count > 1 ? r[1].intValue : 0)
        return r.first?.int64Value ?? 0
    }

    // MARK: Caches e temporários (S2)

    /// Ao abrir o app (com o motor pronto) e ao fim de cada export. Fora da main.
    static func trimCaches(atLaunch: Bool) {
        let caches = AureaPaths.caches
        if atLaunch {
            // Temporários com dono: a cópia do .zip escolhido (a extração fica em
            // Media/sequences) e o pacote de plugin. Sobra = queda no meio.
            for name in ["sequences", "plugin-install"] { removeContents(of: caches.appendingPathComponent(name, isDirectory: true)) }
        }
        // `preview-proxies` fica de fora: o motor tem o índice e o teto dessa
        // pasta, e aparar por fora ao fim de cada export apagava o proxy do
        // projeto aberto (refeito em seguida, com a prévia pesada enquanto isso).
        for name in ["effect-previews", "waveform", "headbop"] {   // headbop: rig do Headbop (regenerável)
            trim(caches.appendingPathComponent(name, isDirectory: true), to: cacheLimit)
        }
    }

    /// Pastas do tmp com dono conhecido (os seletores, Hyper, Mocap, legendas,
    /// comunidade, loja, mural, arquivo do projeto).
    private static let temporaryOwners: Set<String> = ["HomeImports", "HyperImports", "Mocap", "cloud-captions",
                                                       "plugin-files", "ArquivoProjeto"]
    private static let temporaryPrefixes = ["community-", "avatar-", "store-", "mural-upload", "entrada-"]

    static func ownedTemporary(_ path: String) -> Bool {
        guard let at = path.range(of: "/tmp/") else { return false }
        let first = path[at.upperBound...].split(separator: "/", maxSplits: 1).first.map(String.init) ?? ""
        return temporaryOwners.contains(first) || temporaryPrefixes.contains { first.hasPrefix($0) }
            || UUID(uuidString: first) != nil
    }

    /// Sobras do tmp com mais de `minAge` que nenhum projeto (nem o aberto) usa.
    /// O motor decide (scan com `anyFile`) e apaga pela mesma porta. Fora da main.
    @discardableResult
    static func sweepTemporaries(_ engine: AureaEngine, minAge: Int64 = 24 * 3600) -> Int64 {
        let tmp = FileManager.default.temporaryDirectory.path
        guard let r = engine.mediaStorageScan(in: projectsDirectory, roots: [tmp], keep: [], minAgeSeconds: minAge, anyFile: true),
              (r["blocked"] as? NSNumber)?.boolValue == false else { return 0 }
        let owned = (r["orphans"] as? [String] ?? []).filter(ownedTemporary)
        let freed = owned.isEmpty ? 0 : engine.removeManagedMedia(owned, roots: [tmp], keep: []).first?.int64Value ?? 0
        removeEmptyTemporaryFolders()
        return freed
    }

    /// Pastas do tmp que ficaram vazias (a importação moveu o arquivo para Media).
    private static func removeEmptyTemporaryFolders() {
        let fm = FileManager.default
        let tmp = fm.temporaryDirectory
        for child in (try? fm.contentsOfDirectory(at: tmp, includingPropertiesForKeys: [.isDirectoryKey])) ?? [] {
            guard ownedTemporary(child.path), (try? child.resourceValues(forKeys: [.isDirectoryKey]))?.isDirectory == true else { continue }
            for inner in (try? fm.contentsOfDirectory(at: child, includingPropertiesForKeys: nil)) ?? [] {
                if (try? fm.contentsOfDirectory(atPath: inner.path))?.isEmpty == true { try? fm.removeItem(at: inner) }
            }
            if (try? fm.contentsOfDirectory(atPath: child.path))?.isEmpty == true { try? fm.removeItem(at: child) }
        }
    }

    /// A limpeza da abertura, numa fila de fundo.
    static func launchCleanup(_ engine: AureaEngine) {
        DispatchQueue.global(qos: .utility).async {
            AureaStorage.trimCaches(atLaunch: true)
            AureaStorage.sweepTemporaries(engine)
        }
    }

    // MARK: Exportações guardadas no app (S4)

    private static let ledgerKey = "aurea.storage.exportsSavedToPhotos"

    /// O Fotos CONFIRMOU a cópia de `url`: das exportações deste projeto que
    /// também já estão no Fotos, só a mais nova fica em Documents. Nunca apaga
    /// a única cópia: uma exportação que o Fotos recusou (ou sem permissão)
    /// nunca entra na lista; as de versões antigas, sem registro, ficam.
    static func exportSavedToPhotos(_ url: URL, project: String) {
        let defaults = UserDefaults.standard
        var ledger = defaults.dictionary(forKey: ledgerKey) as? [String: [String]] ?? [:]
        let name = url.lastPathComponent
        let documents = AureaPaths.documents.standardizedFileURL
        for old in ledger[project] ?? [] where old != name {
            // Só um nome solto em Documents (nunca um caminho que saia dela).
            guard !old.contains("/"), !old.hasSuffix(".aurea") else { continue }
            let file = documents.appendingPathComponent(old)
            if (try? file.resourceValues(forKeys: [.isRegularFileKey]))?.isRegularFile == true { try? FileManager.default.removeItem(at: file) }
        }
        ledger[project] = [name]
        defaults.set(ledger, forKey: ledgerKey)
    }

    private static let exportExtensions: Set<String> = ["mp4", "mov", "m4v", "gif", "png", "zip"]

    /// Os arquivos de export em Documents (raiz): vídeo, GIF, PNG, sequência .zip.
    static func exportFiles() -> [URL] {
        let fm = FileManager.default
        return ((try? fm.contentsOfDirectory(at: AureaPaths.documents, includingPropertiesForKeys: [.isRegularFileKey],
                                             options: [.skipsHiddenFiles])) ?? [])
            .filter { exportExtensions.contains($0.pathExtension.lowercased())
                && (try? $0.resourceValues(forKeys: [.isRegularFileKey]))?.isRegularFile == true }
    }

    // MARK: Medida

    static func measure(_ urls: [URL]) -> (bytes: Int64, files: Int) {
        var bytes: Int64 = 0, files = 0
        let fm = FileManager.default
        for root in urls {
            if (try? root.resourceValues(forKeys: [.isRegularFileKey]))?.isRegularFile == true {
                bytes += Int64((try? root.resourceValues(forKeys: [.fileSizeKey]))?.fileSize ?? 0); files += 1
                continue
            }
            guard let walker = fm.enumerator(at: root, includingPropertiesForKeys: [.isRegularFileKey, .fileSizeKey]) else { continue }
            for case let file as URL in walker {
                guard let v = try? file.resourceValues(forKeys: [.isRegularFileKey, .fileSizeKey]), v.isRegularFile == true else { continue }
                bytes += Int64(v.fileSize ?? 0); files += 1
            }
        }
        return (bytes, files)
    }

    private static func files(in root: URL) -> [(url: URL, size: Int64, date: Date)] {
        guard let walker = FileManager.default.enumerator(at: root, includingPropertiesForKeys: [.isRegularFileKey, .fileSizeKey,
                                                                                                    .contentModificationDateKey]) else { return [] }
        var out: [(url: URL, size: Int64, date: Date)] = []
        for case let file as URL in walker {
            guard let v = try? file.resourceValues(forKeys: [.isRegularFileKey, .fileSizeKey, .contentModificationDateKey]),
                  v.isRegularFile == true else { continue }
            out.append((url: file, size: Int64(v.fileSize ?? 0), date: v.contentModificationDate ?? .distantPast))
        }
        return out
    }

    /// Os mais ANTIGOS saem até a pasta caber no teto (LRU pela data, como o Android).
    @discardableResult
    static func trim(_ root: URL, to limit: Int64) -> Int64 {
        let all = files(in: root)
        var total = all.reduce(Int64(0)) { $0 + $1.size }
        guard total > limit else { return 0 }
        var freed: Int64 = 0
        for file in all.sorted(by: { $0.date < $1.date }) where total > limit {
            if (try? FileManager.default.removeItem(at: file.url)) != nil { total -= file.size; freed += file.size }
        }
        return freed
    }

    @discardableResult
    static func removeContents(of root: URL) -> Int64 {
        let freed = measure([root]).bytes
        for child in (try? FileManager.default.contentsOfDirectory(at: root, includingPropertiesForKeys: nil)) ?? [] {
            try? FileManager.default.removeItem(at: child)
        }
        return freed
    }
}

// MARK: - Importação (S4): mover do tmp e não guardar o mesmo arquivo duas vezes

extension AureaPaths {
    /// Traz a mídia escolhida para Media/ (par do copyIntoApp do Android). Do
    /// tmp do app (cópia dos seletores) ela é MOVIDA e a pasta tmp/<UUID> sai;
    /// de fora, copiada. Mesmo conteúdo já em Media (outro projeto, a mesma
    /// foto de novo): o nome novo vira um link físico do antigo — cada projeto
    /// tem o seu caminho (apagar um não leva o do outro) e o disco guarda um
    /// só. Fora da main: o hash só roda quando o tamanho bate.
    static func importIntoMedia(_ source: URL) throws -> URL {
        ensureDirectories()
        let destination = mediaDestination(for: source.lastPathComponent)
        let fm = FileManager.default
        let tmp = fm.temporaryDirectory.standardizedFileURL.path
        let file = source.standardizedFileURL
        if file.path.hasPrefix(tmp + "/") {
            do { try fm.moveItem(at: file, to: destination) } catch { try copyImport(source, to: destination) }
            let folder = file.deletingLastPathComponent()
            if folder.path.hasPrefix(tmp + "/"), (try? fm.contentsOfDirectory(atPath: folder.path))?.isEmpty == true {
                try? fm.removeItem(at: folder)
            }
        } else {
            try copyImport(source, to: destination)
        }
        linkTwin(destination)
        return destination
    }

    /// Outro arquivo de Media com o MESMO conteúdo (tamanho igual e SHA-256
    /// igual): `file` passa a ser um link físico dele. A cópia só sai quando o
    /// link já existe (rename atômico por cima).
    static func linkTwin(_ file: URL) {
        let fm = FileManager.default
        let keys: Set<URLResourceKey> = [.fileSizeKey, .isRegularFileKey]
        guard let size = (try? file.resourceValues(forKeys: keys))?.fileSize, size > 0 else { return }
        let siblings = (try? fm.contentsOfDirectory(at: media, includingPropertiesForKeys: Array(keys), options: [.skipsHiddenFiles])) ?? []
        var mine: Data?
        for other in siblings where other.lastPathComponent != file.lastPathComponent {
            guard let v = try? other.resourceValues(forKeys: keys), v.isRegularFile == true, v.fileSize == size else { continue }
            if mine == nil { mine = sha256(file) }
            guard let digest = mine, let theirs = sha256(other), digest == theirs else { continue }
            let link = file.deletingLastPathComponent().appendingPathComponent(".link-\(UUID().uuidString)")
            do {
                try fm.linkItem(at: other, to: link)
                if Darwin.rename(link.path, file.path) != 0 { try? fm.removeItem(at: link) }
            } catch {
                try? fm.removeItem(at: link)
            }
            return
        }
    }

    static func sha256(_ url: URL) -> Data? {
        guard let handle = try? FileHandle(forReadingFrom: url) else { return nil }
        defer { try? handle.close() }
        var hasher = SHA256()
        while true {
            let chunk: Data = autoreleasepool { handle.readData(ofLength: 1 << 20) }
            if chunk.isEmpty { break }
            hasher.update(data: chunk)
        }
        return Data(hasher.finalize())
    }
}

// MARK: - Apagar projetos pela Home

extension AureaModel {
    /// Apaga os projetos (com miniatura, sidecar e cópias de recuperação) e a
    /// mídia que SÓ eles usavam — a lista vem do motor ANTES de qualquer
    /// arquivo sair. `done` roda na main.
    func deleteProjectsReleasingMedia(_ paths: [String], done: @escaping () -> Void) {
        let importer = engine
        let editorOpen = screen == .editor
        let ready = started && !exporting
        DispatchQueue.global(qos: .userInitiated).async {
            let plan = ready ? AureaStorage.planRelease(importer, projects: paths, editorOpen: editorOpen) : nil
            deleteHomeProjects(paths)
            if let plan { AureaStorage.release(importer, plan) }
            DispatchQueue.main.async { done() }
        }
    }
}

// MARK: - Tela

/// Ids das linhas (os mesmos do Android: AppStorage.kt / EditorStore.kt).
enum StorageLineId {
    static let projectMedia = "midia-projetos", orphanMedia = "midia-orfa", appExports = "exportacoes"
    static let preRender = "prerender", proxies = "proxies", otherCaches = "outros-caches"
}

/// Uma linha da tela de armazenamento (a `StorageLine` do Android).
struct StorageLine: Identifiable {
    enum Clean { case none, cache, confirm }
    let id: String
    let title: String
    let detail: String
    let bytes: Int64
    let files: Int
    let clean: Clean
}

@MainActor
final class StorageScreen: ObservableObject {
    @Published private(set) var lines: [StorageLine]?

    /// Mede tudo fora da main (lê todos os projetos e as pastas inteiras).
    func refresh(model: AureaModel) {
        lines = nil
        let engine = model.engine
        let ready = model.started
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            let measured = Self.measure(engine: ready ? engine : nil)
            DispatchQueue.main.async { self?.lines = measured }
        }
    }

    nonisolated private static func measure(engine: AureaEngine?) -> [StorageLine] {
        let caches = AureaPaths.caches, documents = AureaPaths.documents
        var usedBytes: Int64 = 0, usedFiles = 0, orphanBytes: Int64 = 0, orphanFiles = 0, blocked = true
        if let engine, let r = engine.mediaStorageScan(in: AureaStorage.projectsDirectory, roots: AureaStorage.managedRoots,
                                                      keep: AureaStorage.keepRoots, minAgeSeconds: AureaStorage.orphanMinAge, anyFile: false) {
            blocked = (r["blocked"] as? NSNumber)?.boolValue ?? true
            usedBytes = (r["usedBytes"] as? NSNumber)?.int64Value ?? 0
            usedFiles = (r["usedFiles"] as? NSNumber)?.intValue ?? 0
            orphanBytes = (r["orphanBytes"] as? NSNumber)?.int64Value ?? 0
            orphanFiles = (r["orphanFiles"] as? NSNumber)?.intValue ?? 0
        } else {
            let all = AureaStorage.measure([AureaPaths.media])
            usedBytes = all.bytes; usedFiles = all.files
        }
        let library = AureaStorage.measure(AureaStorage.keepRoots.map { URL(fileURLWithPath: $0, isDirectory: true) }
            + ["LUTs", "formas3d", "Sons"].map { documents.appendingPathComponent($0, isDirectory: true) })
        let exports = AureaStorage.measure(AureaStorage.exportFiles())
        let preRender = AureaStorage.measure([caches.appendingPathComponent("pre-render", isDirectory: true)])
        let proxies = AureaStorage.measure([caches.appendingPathComponent("preview-proxies", isDirectory: true)])
        let others = AureaStorage.measure(["effect-previews", "waveform", "headbop", "sequences", "plugin-install"]
            .map { caches.appendingPathComponent($0, isDirectory: true) }
            + ((try? FileManager.default.contentsOfDirectory(at: FileManager.default.temporaryDirectory, includingPropertiesForKeys: nil)) ?? [])
                .filter { AureaStorage.ownedTemporary($0.path) })
        return [
            StorageLine(id: StorageLineId.projectMedia, title: AureaText.t("storage_media_title"), detail: AureaText.t("storage_media_detail"),
                        bytes: usedBytes + library.bytes, files: usedFiles + library.files, clean: .none),
            StorageLine(id: StorageLineId.orphanMedia, title: AureaText.t("storage_orphans_title"),
                        detail: blocked ? AureaText.t("storage_orphans_blocked") : AureaText.t("storage_orphans_detail"),
                        bytes: orphanBytes, files: orphanFiles, clean: blocked ? .none : .confirm),
            StorageLine(id: StorageLineId.appExports, title: AureaText.t("storage_exports_title"), detail: AureaText.t("storage_exports_detail"),
                        bytes: exports.bytes, files: exports.files, clean: .confirm),
            StorageLine(id: StorageLineId.preRender, title: AureaText.t("storage_prerender_title"), detail: AureaText.t("storage_prerender_detail"),
                        bytes: preRender.bytes, files: preRender.files, clean: .cache),
            StorageLine(id: StorageLineId.proxies, title: AureaText.t("storage_proxies_title"), detail: AureaText.t("storage_proxies_detail"),
                        bytes: proxies.bytes, files: proxies.files, clean: .cache),
            StorageLine(id: StorageLineId.otherCaches, title: AureaText.t("storage_caches_title"), detail: AureaText.t("storage_caches_detail"),
                        bytes: others.bytes, files: others.files, clean: .cache),
        ]
    }

    /// Limpa uma linha (a de confirmação já foi confirmada na tela).
    func clean(_ id: String, model: AureaModel) {
        guard !model.exporting else { model.toast = AureaText.t("storage_busy"); return }
        let engine = model.engine
        let ready = model.started
        if id == StorageLineId.otherCaches {
            HomeThumbCache.shared.clear()
            Task { _ = await model.effectPreviews.clear() }
        }
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            var freed: Int64 = 0
            switch id {
            case StorageLineId.orphanMedia where ready:
                // Varre de novo: apaga o que é órfão AGORA, não o que era quando a tela abriu.
                if let r = engine.mediaStorageScan(in: AureaStorage.projectsDirectory, roots: AureaStorage.managedRoots,
                                                   keep: AureaStorage.keepRoots, minAgeSeconds: AureaStorage.orphanMinAge, anyFile: false),
                   (r["blocked"] as? NSNumber)?.boolValue == false, let orphans = r["orphans"] as? [String], !orphans.isEmpty {
                    freed = engine.removeManagedMedia(orphans, roots: AureaStorage.managedRoots, keep: AureaStorage.keepRoots)
                        .first?.int64Value ?? 0
                }
            case StorageLineId.appExports:
                for file in AureaStorage.exportFiles() {
                    let size = AureaStorage.measure([file]).bytes
                    if (try? FileManager.default.removeItem(at: file)) != nil { freed += size }
                }
            case StorageLineId.preRender where ready:
                let before = AureaStorage.measure([AureaPaths.caches.appendingPathComponent("pre-render", isDirectory: true)]).bytes
                if engine.clearPreRenderCache() { freed = before }
            case StorageLineId.proxies where ready:
                freed = Int64(engine.clearProxyCache())
            case StorageLineId.otherCaches:
                for name in ["effect-previews", "waveform", "headbop", "sequences", "plugin-install"] {
                    freed += AureaStorage.removeContents(of: AureaPaths.caches.appendingPathComponent(name, isDirectory: true))
                }
                // O tmp em uso agora (a folha de compartilhar aberta) tem menos de 1 h.
                if ready { freed += AureaStorage.sweepTemporaries(engine, minAge: 3600) }
            default:
                break
            }
            let message = freed > 0 ? AureaText.t("storage_cleaned", AureaStorage.size(freed)) : AureaText.t("storage_nothing")
            DispatchQueue.main.async {
                model.toast = message
                self?.refresh(model: model)
            }
        }
    }
}

/// O diálogo claro de armazenamento (par do StorageDialog.kt), aberto pelos Ajustes.
struct StorageDialog: View {
    @ObservedObject var model: AureaModel
    let onDismiss: () -> Void
    @StateObject private var screen = StorageScreen()
    @State private var confirm: StorageLine?

    var body: some View {
        ZStack {
            AureaDialog(title: AureaText.t("storage_title"),
                        message: screen.lines.map { AureaText.t("storage_total", AureaStorage.size($0.reduce(Int64(0)) { $0 + $1.bytes })) }
                            ?? AureaText.t("storage_measuring"),
                        actions: [DialogAction(AureaText.t("common_close"), role: .positive, tag: "storage.close")],
                        onDismiss: onDismiss, tag: "home.drawer.storage",
                        content: AnyView(rows))
            if let line = confirm {
                AureaAlert(title: line.id == StorageLineId.orphanMedia ? AureaText.t("storage_confirm_orphans_title")
                                                                       : AureaText.t("storage_confirm_exports_title"),
                           message: line.id == StorageLineId.orphanMedia
                               ? AureaText.t("storage_confirm_orphans_message", line.files, AureaStorage.size(line.bytes))
                               : AureaText.t("storage_confirm_exports_message", AureaStorage.size(line.bytes)),
                           confirmLabel: AureaText.t("common_delete"), destructive: true,
                           onConfirm: { screen.clean(line.id, model: model) },
                           onDismiss: { confirm = nil })
            }
        }
        .onAppear { screen.refresh(model: model) }
    }

    private var rows: some View {
        VStack(alignment: .leading, spacing: 0) {
            ForEach(Array((screen.lines ?? []).enumerated()), id: \.element.id) { index, line in
                if index > 0 { Rectangle().fill(AureaColors.lightDivider).frame(height: AureaDialogMetrics.hairline) }
                row(line)
            }
        }
    }

    private func row(_ line: StorageLine) -> some View {
        let size = AureaStorage.size(line.bytes)
        let canClean = line.clean != .none && line.bytes > 0
        return HStack(spacing: 8) {
            VStack(alignment: .leading, spacing: 1) {
                Text(line.title).font(.aurea(size: 15, weight: .medium)).foregroundStyle(AureaColors.lightText)
                Text(size).font(.aurea(size: 15)).foregroundStyle(AureaColors.lightText)
                Text(line.detail).font(.aurea(size: 12)).foregroundStyle(AureaColors.lightTextMuted)
                    .fixedSize(horizontal: false, vertical: true)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(AureaText.t("storage_row_a11y", line.title, size))
            .accessibilityHint(line.detail)
            if canClean {
                Button {
                    if line.clean == .confirm { confirm = line } else { screen.clean(line.id, model: model) }
                } label: {
                    Text(AureaText.t("storage_clean"))
                        .font(.aurea(size: 15, weight: .medium))
                        .foregroundStyle(line.clean == .confirm ? AureaColors.lightDanger : AureaColors.lightAccentInk)
                        .padding(.horizontal, 8).padding(.vertical, 10)
                        .contentShape(Rectangle())
                }
                .buttonStyle(AureaPressStyle(shrink: 1))
                .accessibilityLabel(AureaText.t("storage_clean_a11y", line.title))
                .accessibilityIdentifier("storage.clean.\(line.id)")
            }
        }
        .padding(.vertical, 8)
        .frame(minHeight: 52)
        .accessibilityIdentifier("storage.row.\(line.id)")
    }
}
