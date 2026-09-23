// =============================================================================
//  Aurea / platform / ios / app / HomeView.swift
//
//  A Home: três abas (Início, Projetos, Ajustes) e a barra de abas embaixo — as
//  MESMAS três do Android depois que Comunidade, Perfil e a aba Efeitos saíram
//  (§2: fora do editor não há camada escolhida, então não há o que fazer com um
//  efeito; o navegador vive onde ele serve para alguma coisa).
//
//  A capa do projeto é lida de `Documents/Thumbs/<nome>.jpg` — o mesmo esquema
//  de arquivo do Android. Ela é uma MINIATURA da Home; o preview continua indo
//  direto para o CAMetalLayer, sem bitmap no meio.
// =============================================================================
import SwiftUI
import UniformTypeIdentifiers

struct HomeView: View {
    @EnvironmentObject private var model: AureaModel
    @State private var tab: Tab = .start
    @State private var showNewProject = false
    @State private var importing: AureaModel.ImportKind?

    enum Tab: String, CaseIterable, Identifiable {
        case start, projects, settings
        var id: String { rawValue }
        var label: String {
            switch self {
            case .start: return AureaText.t("home_tab_start")
            case .projects: return AureaText.t("home_tab_projects")
            case .settings: return AureaText.t("home_tab_settings")
            }
        }
        var icon: String {
            switch self {
            case .start: return "house"
            case .projects: return "rectangle.stack"
            case .settings: return "slider.horizontal.3"
            }
        }
        var iconActive: String {
            switch self {
            case .start: return "house.fill"
            case .projects: return "rectangle.stack.fill"
            case .settings: return "slider.horizontal.3"
            }
        }
    }

    var body: some View {
        VStack(spacing: 0) {
            Group {
                switch tab {
                case .start: StartTab(showNewProject: $showNewProject)
                case .projects: ProjectsTab(showNewProject: $showNewProject)
                case .settings: SettingsTab()
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)

            tabBar
        }
        // O véu da barra de status (o `SystemBarVeil` do Android: #0B0F13)
        // atrás da status bar e o fundo do app no resto.
        .background(AureaColors.background.ignoresSafeArea())
        .background(AureaColors.systemBarVeil.ignoresSafeArea(edges: .top))
        .sheet(isPresented: $showNewProject) { NewProjectSheet() }
    }

    private var tabBar: some View {
        VStack(spacing: 0) {
            Rectangle().fill(AureaColors.hairline).frame(height: AureaDims.hairline)
            HStack(spacing: 0) {
                ForEach(Tab.allCases) { item in
                    Button {
                        tab = item
                    } label: {
                        VStack(spacing: 3) {
                            Image(systemName: tab == item ? item.iconActive : item.icon)
                                .font(.system(size: AureaDims.iconLg, weight: .regular))
                            Text(item.label)
                                .font(AureaType.tabLabel)
                                .lineLimit(1)
                        }
                        .foregroundStyle(tab == item ? AureaColors.accent : AureaColors.muted)
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                }
            }
            .frame(height: AureaDims.tabBarHeight)
            .background(AureaColors.surface.opacity(0.72))
        }
    }
}

// =============================================================================
// Aba Início: continuar o último projeto + criar
// =============================================================================
private struct StartTab: View {
    @EnvironmentObject private var model: AureaModel
    @Binding var showNewProject: Bool

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                Text(AureaText.t("home_title_start"))
                    .font(AureaType.title)
                    .foregroundStyle(AureaColors.text)
                    .padding(.horizontal, AureaDims.pad)
                    .padding(.top, 12)

                if let recent = model.projects.first {
                    Button { model.open(recent) } label: {
                        VStack(alignment: .leading, spacing: 0) {
                            ZStack {
                                if let image = UIImage(contentsOfFile: recent.thumbnailURL.path) {
                                    Image(uiImage: image).resizable().aspectRatio(contentMode: .fill)
                                } else {
                                    Rectangle().fill(AureaColors.chip)
                                        .overlay(Image(systemName: "film")
                                            .font(.system(size: 28))
                                            .foregroundStyle(AureaColors.subtle))
                                }
                            }
                            .frame(height: 190)
                            .clipped()
                            VStack(alignment: .leading, spacing: 4) {
                                Text(recent.name)
                                    .font(AureaType.section)
                                    .foregroundStyle(AureaColors.text)
                                Text(recent.modified.formatted(date: .abbreviated, time: .shortened))
                                    .font(AureaType.tiny)
                                    .foregroundStyle(AureaColors.subtle)
                                Text(AureaText.t("home_continue_action"))
                                    .font(AureaType.label)
                                    .foregroundStyle(AureaColors.accent)
                                    .padding(.top, 2)
                            }
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .padding(14)
                        }
                        .background(AureaColors.surface, in: RoundedRectangle(cornerRadius: AureaDims.corner))
                        .overlay(RoundedRectangle(cornerRadius: AureaDims.corner)
                            .stroke(AureaColors.border, lineWidth: 1))
                    }
                    .buttonStyle(.plain)
                    .padding(.horizontal, AureaDims.pad)
                } else {
                    VStack(alignment: .leading, spacing: 6) {
                        Text(AureaText.t("home_no_projects"))
                            .font(AureaType.section)
                            .foregroundStyle(AureaColors.text)
                        Text(AureaText.t("home_no_projects_hint"))
                            .font(AureaType.body)
                            .foregroundStyle(AureaColors.subtle)
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(16)
                    .background(AureaColors.surface, in: RoundedRectangle(cornerRadius: AureaDims.corner))
                    .padding(.horizontal, AureaDims.pad)
                }

                VStack(spacing: 10) {
                    Button { showNewProject = true } label: {
                        Label(AureaText.t("home_new_project"), systemImage: "plus")
                            .font(AureaType.section)
                            .foregroundStyle(AureaColors.onAccent)
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 13)
                            .background(AureaColors.brand, in: RoundedRectangle(cornerRadius: 11))
                    }
                    .buttonStyle(.plain)

                    Button { importing = .video } label: {
                        Label(AureaText.t("home_import_media"), systemImage: "square.and.arrow.down")
                            .font(AureaType.section)
                            .foregroundStyle(AureaColors.text)
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 13)
                            .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 11))
                    }
                    .buttonStyle(.plain)
                }
                .padding(.horizontal, AureaDims.pad)
                .padding(.bottom, 24)
            }
        }
        .fileImporter(isPresented: Binding(get: { importing != nil }, set: { if !$0 { importing = nil } }),
                      allowedContentTypes: importing.map(contentTypes) ?? [.movie]) { result in
            defer { importing = nil }
            guard case .success(let url) = result, let kind = importing else { return }
            model.importMedia(url: url, kind: kind)
        }
    }

    private func contentTypes(for kind: AureaModel.ImportKind) -> [UTType] {
        switch kind {
        case .video: return [.movie, .video]
        case .audio: return [.audio]
        case .image: return [.image]
        case .model: return [UTType(filenameExtension: "glb") ?? .data, UTType(filenameExtension: "gltf") ?? .data]
        case .hdri: return [UTType(filenameExtension: "hdr") ?? .data]
        }
    }
}

// =============================================================================
// Aba Projetos
// =============================================================================
private struct ProjectsTab: View {
    @EnvironmentObject private var model: AureaModel
    @Binding var showNewProject: Bool
    @State private var renaming: ProjectFile?
    @State private var renameText = ""

    private let columns = [GridItem(.adaptive(minimum: 156), spacing: 12)]

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 8) {
                Image(systemName: "magnifyingglass").foregroundStyle(AureaColors.subtle)
                TextField(AureaText.t("home_search_hint"), text: $model.searchQuery)
                    .textFieldStyle(.plain)
                    .foregroundStyle(AureaColors.text)
                    .onSubmit { model.refreshProjectList() }
                if !model.searchQuery.isEmpty {
                    Button { model.searchQuery = ""; model.refreshProjectList() } label: {
                        Image(systemName: "xmark.circle.fill").foregroundStyle(AureaColors.subtle)
                    }
                    .buttonStyle(.plain)
                }
                Menu {
                    ForEach(AureaModel.ProjectSort.allCases) { option in
                        Button(option.label) { model.sort = option; model.refreshProjectList() }
                    }
                } label: {
                    Image(systemName: "arrow.up.arrow.down").foregroundStyle(AureaColors.muted)
                }
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 10)
            .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 9))
            .padding(.horizontal, AureaDims.pad)
            .padding(.top, 10)

            if model.projects.isEmpty {
                VStack(spacing: 8) {
                    Text(AureaText.t("home_no_projects")).font(AureaType.section)
                        .foregroundStyle(AureaColors.text)
                    Text(AureaText.t("home_no_projects_hint")).font(AureaType.body)
                        .foregroundStyle(AureaColors.subtle)
                    Button(AureaText.t("home_new_project")) { showNewProject = true }
                        .font(AureaType.label)
                        .foregroundStyle(AureaColors.accent)
                        .padding(.top, 6)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                ScrollView {
                    LazyVGrid(columns: columns, spacing: 12) {
                        ForEach(model.projects) { project in
                            ProjectCard(project: project,
                                        onOpen: { model.open(project) },
                                        onRename: { renaming = project; renameText = project.name },
                                        onDelete: { model.delete(project) })
                        }
                    }
                    .padding(.horizontal, AureaDims.pad)
                    .padding(.vertical, 12)
                }
            }
        }
        .alert(AureaText.t("common_rename"), isPresented: Binding(get: { renaming != nil },
                                                                 set: { if !$0 { renaming = nil } })) {
            TextField(AureaText.t("editor_nome_camada"), text: $renameText)
            Button(AureaText.t("common_cancel"), role: .cancel) { renaming = nil }
            Button(AureaText.t("common_save")) {
                if let project = renaming, !renameText.isEmpty { model.rename(project, to: renameText) }
                renaming = nil
            }
        }
    }
}

private struct ProjectCard: View {
    let project: ProjectFile
    let onOpen: () -> Void
    let onRename: () -> Void
    let onDelete: () -> Void

    var body: some View {
        Button(action: onOpen) {
            VStack(alignment: .leading, spacing: 0) {
                ZStack {
                    if let image = UIImage(contentsOfFile: project.thumbnailURL.path) {
                        Image(uiImage: image).resizable().aspectRatio(contentMode: .fill)
                    } else {
                        Rectangle().fill(AureaColors.chip)
                            .overlay(Image(systemName: "film")
                                .foregroundStyle(AureaColors.subtle))
                    }
                }
                .frame(height: 96)
                .clipped()

                Text(project.name)
                    .font(AureaType.label)
                    .foregroundStyle(AureaColors.text)
                    .lineLimit(1)
                    .padding(.horizontal, 8)
                    .padding(.top, 6)
                Text(project.modified.formatted(date: .numeric, time: .omitted))
                    .font(AureaType.tiny)
                    .foregroundStyle(AureaColors.subtle)
                    .padding(.horizontal, 8)
                    .padding(.bottom, 8)
            }
            .background(AureaColors.surface, in: RoundedRectangle(cornerRadius: AureaDims.corner))
            .overlay(RoundedRectangle(cornerRadius: AureaDims.corner).stroke(AureaColors.border, lineWidth: 1))
        }
        .buttonStyle(.plain)
        .contextMenu {
            Button(AureaText.t("common_open"), action: onOpen)
            Button(AureaText.t("common_rename"), action: onRename)
            Button(AureaText.t("common_delete"), role: .destructive, action: onDelete)
        }
    }
}

// =============================================================================
// Aba Ajustes: idioma, aparelho, armazenamento e o painel DEV
// =============================================================================
private struct SettingsTab: View {
    @EnvironmentObject private var model: AureaModel

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                Text(AureaText.t("home_title_settings"))
                    .font(AureaType.title)
                    .foregroundStyle(AureaColors.text)

                VStack(alignment: .leading, spacing: 8) {
                    Text(AureaText.t("settings_group_language")).font(AureaType.section)
                        .foregroundStyle(AureaColors.text)
                    Picker("", selection: $model.language) {
                        ForEach(AureaLanguage.allCases) { language in
                            Text(language.label).tag(language)
                        }
                    }
                    .pickerStyle(.segmented)
                }

                VStack(alignment: .leading, spacing: 8) {
                    Text(AureaText.t("settings_group_device")).font(AureaType.section)
                        .foregroundStyle(AureaColors.text)
                    // O que o MOTOR decidiu para este aparelho, em números — a
                    // mesma tabela da tela "Este aparelho" do Android.
                    ForEach(reportRows, id: \.0) { row in
                        HStack {
                            Text(row.0).font(AureaType.label).foregroundStyle(AureaColors.muted)
                            Spacer()
                            Text(row.1).font(AureaType.value).foregroundStyle(AureaColors.text)
                        }
                    }
                    Text(model.deviceSummary)
                        .font(AureaType.tiny)
                        .foregroundStyle(AureaColors.subtle)
                }
                .padding(14)
                .background(AureaColors.surface, in: RoundedRectangle(cornerRadius: AureaDims.corner))
                .overlay(RoundedRectangle(cornerRadius: AureaDims.corner).stroke(AureaColors.border, lineWidth: 1))

                Toggle(isOn: $model.showPerf) {
                    Text(AureaText.t("settings_dev_tools")).font(AureaType.body).foregroundStyle(AureaColors.text)
                }
                .tint(AureaColors.accent)

                if model.showPerf {
                    VStack(alignment: .leading, spacing: 6) {
                        ForEach(perfRows, id: \.0) { row in
                            HStack {
                                Text(row.0).font(AureaType.tiny).foregroundStyle(AureaColors.subtle)
                                Spacer()
                                Text(row.1).font(AureaType.value).foregroundStyle(AureaColors.text)
                            }
                        }
                    }
                    .padding(12)
                    .background(AureaColors.surfaceHigh, in: RoundedRectangle(cornerRadius: AureaDims.corner))
                }
            }
            .padding(AureaDims.pad)
            .padding(.bottom, 24)
        }
    }

    /// Os mesmos nomes do `DeviceReport.kt`: o mesmo número tem o mesmo nome.
    private var reportRows: [(String, String)] {
        let report = model.deviceReport
        func value(_ key: String) -> NSNumber? { report[key] }
        var rows: [(String, String)] = []
        if let cores = value("totalCores")?.intValue {
            let perf = value("performanceCores")?.intValue ?? 0
            let eff = value("efficiencyCores")?.intValue ?? 0
            rows.append(("Núcleos", eff > 0 ? "\(cores) (\(perf)+\(eff))" : "\(cores)"))
        }
        if let ram = value("totalMemoryMb")?.intValue { rows.append(("RAM", "\(ram) MB")) }
        if let budget = value("budgetMb")?.intValue { rows.append(("Orçamento", "\(budget) MB")) }
        if let tex = value("maxTexture")?.intValue { rows.append(("Textura", "\(tex) px")) }
        if let short = value("maxExportHeight")?.intValue { rows.append(("Export", "\(short)p")) }
        if let scale = value("initialScale")?.intValue { rows.append(("Prévia inicial", "\(scale)")) }
        if let thermal = value("thermalTier")?.intValue { rows.append(("Faixa térmica", "\(thermal)")) }
        if let freq = value("maxFrequencyMhz")?.intValue, freq > 0 {
            rows.append(("CPU máx", "\(freq) MHz"))
        }
        return rows
    }

    private var perfRows: [(String, String)] {
        let perf = model.perf
        func number(_ key: String) -> NSNumber? { perf[key] as? NSNumber }
        var rows: [(String, String)] = []
        if let fps = number("previewFps") { rows.append(("Prévia", String(format: "%.1f fps", fps.floatValue))) }
        if let cpu = number("cpuMs") { rows.append(("CPU", String(format: "%.2f ms", cpu.floatValue))) }
        if let gpu = number("gpuMs") { rows.append(("GPU", String(format: "%.2f ms", gpu.floatValue))) }
        if let decode = number("decodeMs") { rows.append(("Decode", String(format: "%.2f ms", decode.floatValue))) }
        if let passes = number("passesExecuted") { rows.append(("Passes", "\(passes.intValue)")) }
        if let culled = number("passesCulled") { rows.append(("Cortados", "\(culled.intValue)")) }
        if let draws = number("drawCalls") { rows.append(("Draw calls", "\(draws.intValue)")) }
        if let pipelines = number("pipelines") { rows.append(("Pipelines", "\(pipelines.intValue)")) }
        if let dropped = number("droppedFrames") { rows.append(("Quadros perdidos", "\(dropped.intValue)")) }
        if let decoder = perf["decoder"] as? String, !decoder.isEmpty { rows.append(("Decoder", decoder)) }
        if let gpuName = perf["gpuName"] as? String, !gpuName.isEmpty { rows.append(("GPU", gpuName)) }
        return rows
    }
}

// =============================================================================
// Novo projeto — as mesmas opções do Android (ProjectSpec.kt)
// =============================================================================
private struct NewProjectSheet: View {
    @EnvironmentObject private var model: AureaModel
    @Environment(\.dismiss) private var dismiss

    /// key, proporção, rótulo, dica (traduzida).
    private let aspects: [(String, Double, String, String)] = [
        ("16:9", 16.0 / 9.0, "16:9", "aspect_hint_tv"),
        ("9:16", 9.0 / 16.0, "9:16", "aspect_hint_reels"),
        ("1:1", 1, "1:1", "aspect_hint_feed"),
        ("4:5", 4.0 / 5.0, "4:5", "aspect_hint_instagram"),
        ("4:3", 4.0 / 3.0, "4:3", "aspect_hint_classic"),
    ]
    private let resolutions: [UInt32] = [720, 1080, 1440, 2160]
    private let fpsOptions: [Double] = [24, 30, 60]

    @State private var aspectIndex = 0
    @State private var shortSide: UInt32 = 1080
    @State private var fps: Double = 30
    @State private var title = ""

    private func resolutionLabel(_ short: UInt32) -> String {
        switch short {
        case 720: return "HD 720p"
        case 1080: return "Full HD 1080p"
        case 1440: return "QHD 1440p"
        case 2160: return "4K 2160p"
        default: return "\(short)p"
        }
    }

    /// A resolução é o LADO MENOR (a mesma regra do Android): 1080p em 9:16 é
    /// 1080 × 1920, não 608 × 1080.
    private var frame: (UInt32, UInt32) {
        let ratio = aspects[aspectIndex].1
        if ratio >= 1 { return (UInt32((Double(shortSide) * ratio).rounded()), shortSide) }
        return (shortSide, UInt32((Double(shortSide) / ratio).rounded()))
    }

    var body: some View {
        NavigationStack {
            Form {
                Section(AureaText.t("settings_aspect")) {
                    Picker("", selection: $aspectIndex) {
                        ForEach(aspects.indices, id: \.self) { index in
                            Text(aspects[index].2).tag(index)
                        }
                    }
                    .pickerStyle(.segmented)
                    Text(AureaText.t(aspects[aspectIndex].3))
                        .font(AureaType.tiny)
                        .foregroundStyle(AureaColors.subtle)
                }
                Section(AureaText.t("settings_resolution")) {
                    Picker("", selection: $shortSide) {
                        ForEach(resolutions, id: \.self) { value in
                            Text(resolutionLabel(value)).tag(value)
                        }
                    }
                    .pickerStyle(.segmented)
                    Text("\(frame.0) × \(frame.1)")
                        .font(AureaType.value)
                        .foregroundStyle(AureaColors.muted)
                }
                Section(AureaText.t("settings_fps")) {
                    Picker("", selection: $fps) {
                        ForEach(fpsOptions, id: \.self) { value in
                            Text(String(format: "%g", value)).tag(value)
                        }
                    }
                    .pickerStyle(.segmented)
                }
                Section(AureaText.t("editor_nome_camada")) {
                    TextField("Aurea", text: $title)
                }
            }
            .scrollContentBackground(.hidden)
            .background(AureaColors.background)
            .navigationTitle(AureaText.t("home_new_project"))
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button(AureaText.t("common_cancel")) { dismiss() }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button(AureaText.t("common_save")) {
                        model.newProject(ratio: aspects[aspectIndex].1, shortSide: shortSide,
                                         fps: fps, title: title)
                        dismiss()
                    }
                }
            }
        }
        .preferredColorScheme(.dark)
    }
}
