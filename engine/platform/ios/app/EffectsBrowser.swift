// Android spec: editor/panels/EffectsBrowser.kt and effects/EffectsCatalogView.kt.
import SwiftUI
import UIKit

private enum EffectBrowserLayout {
    static let columnMinimum: CGFloat = 168
    static let previewAspect: CGFloat = 1.6
    static let favoriteDisc: CGFloat = 28
    static let cardGap: CGFloat = 6
    static let parameterPadding: CGFloat = 7
    static let title = Font.aurea(size: 18, weight: .bold)
    static let description = Font.aurea(size: 14)
    static let cost = Font.aurea(size: 9, weight: .medium)
}

struct EffectsBrowser: View {
    @EnvironmentObject private var model: AureaModel
    @StateObject private var prefs = FxEffectPrefs()
    @State private var query = ""
    @State private var filter: FxEffectFilter = .all
    @State private var detail: EffectCatalogItem?
    @State private var pendingApply: (type: UInt32, counts: [Int64: UInt32])?
    @FocusState private var searching: Bool
    let onDismiss: () -> Void

    private var categories: [String] { fxEffectCategories(model.effectCatalog) }
    private var results: [EffectCatalogItem] {
        fxFilterCatalog(model.effectCatalog, fxArrangeCatalog(model.effectCatalog, categories),
                        fxCatalogHaystack(model.effectCatalog), query, filter, prefs.recents, prefs.favorites)
    }

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 0) {
                Button(action: onDismiss) {
                    CupertinoGlyph.text(CupertinoGlyph.ChevronBack, size: AureaDims.iconLg)
                        .frame(width: AureaDims.iconLg, height: AureaDims.iconLg)
                        .padding(AureaDims.s2)
                }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityLabel(AureaText.t("editor_voltar_editor"))
                Color.clear.frame(width: 4)
                Text(AureaText.t("panel_efeitos")).font(EffectBrowserLayout.title).tracking(-0.1)
                Spacer()
                Text(AureaText.t("effect_count", model.effectCatalog.count))
                    .font(HomeType.cardSpec).foregroundStyle(AureaColors.muted)
                    .padding(.trailing, AureaDims.s3)
            }.frame(height: AureaDims.topBar).padding(.horizontal, AureaDims.s2)
            GeometryReader { geometry in
                let columns = max(2, min(5, Int(geometry.size.width / EffectBrowserLayout.columnMinimum)))
                ScrollView {
                    VStack(spacing: AureaDims.s4) {
                        searchField
                        if query.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty { filters }
                        if results.isEmpty {
                            Text(emptyMessage).font(HomeType.bodySmall).foregroundStyle(AureaColors.muted)
                                .multilineTextAlignment(.center).padding(AureaDims.s5)
                        }
                        LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: AureaDims.s3), count: columns), spacing: AureaDims.s4) {
                            ForEach(results) { entry in
                                EffectBrowserTile(entry: entry, store: model.effectPreviews,
                                                  favorite: prefs.isFavorite(entry.typeId),
                                                  onFavorite: { prefs.toggleFavorite(entry.typeId) },
                                                  onOpen: { searching = false; detail = entry })
                            }
                        }
                    }.padding(.horizontal, AureaDims.s3).padding(.bottom, AureaDims.s5)
                }
            }
        }
        .foregroundStyle(AureaColors.text).background(AureaColors.editorPanel.ignoresSafeArea())
        .buttonStyle(.plain)
        .onChange(of: model.status.modelRevision) { _ in finishPendingApply() }
        .task(id: pendingApply?.type) {
            guard pendingApply != nil else { return }
            do { try await Task.sleep(nanoseconds: 3_000_000_000) } catch { return }
            model.refreshModel(force: true)
            finishPendingApply()
            if pendingApply != nil {
                // A slow renderer is not a rejected command. Keep observing so
                // late confirmation still closes the browser without duplicate adds.
                model.toast = "Aguardando confirmação do efeito…"
            }
        }
        .overlay {
            if let entry = detail {
                EffectBrowserDetail(entry: entry, store: model.effectPreviews,
                                specs: model.engine.effectSpecs(entry.typeId),
                                prefs: prefs,
                                canApply: model.layers.contains { model.selection.contains($0.id) && !$0.locked },
                                onDismiss: { detail = nil; pendingApply = nil }) {
                let targets = model.layers.filter { model.selection.contains($0.id) && !$0.locked }
                guard model.started, !targets.isEmpty, pendingApply == nil else { return }
                let before = Dictionary(uniqueKeysWithValues: targets.map { ($0.id, $0.effectCount) })
                pendingApply = (entry.typeId, before)
                model.mutate { engine in
                    engine.beginUndoGroup()
                    for layer in targets { engine.addEffect(entry.typeId, toLayer: layer.id, at: UInt32.max) }
                    engine.endUndoGroup()
                }
                model.refreshModel(force: true)
                finishPendingApply()
                }.id(entry.typeId)
            }
        }
    }

    private func finishPendingApply() {
        guard let pending = pendingApply else { return }
        guard model.layers.contains(where: { row in
            pending.counts[row.id].map { row.effectCount > $0 } ?? false
        }) else { return }
        prefs.addRecent(pending.type)
        pendingApply = nil
        detail = nil
        onDismiss()
    }

    private var searchField: some View {
        HStack(spacing: 0) {
            CupertinoGlyph.text(CupertinoGlyph.Search, size: 18, color: AureaColors.muted)
                .frame(width: 18, height: 18).padding(.trailing, EffectBrowserLayout.cardGap)
            TextField("", text: $query, prompt: Text(AureaText.t("effect_buscar_glitch_vhs_desfoque_cor")).foregroundColor(AureaColors.muted))
                .font(EffectBrowserLayout.description).tracking(-0.1).textInputAutocapitalization(.never).autocorrectionDisabled()
                .textFieldStyle(.plain).tint(AureaColors.accent).focused($searching)
            if !query.isEmpty {
                Button { query = "" } label: {
                    CupertinoGlyph.text(CupertinoGlyph.XmarkCircleFill, size: AureaDims.iconSm, color: AureaColors.muted)
                        .frame(width: AureaDims.iconSm, height: AureaDims.iconSm)
                        .padding(AureaDims.s1)
                }.accessibilityLabel(AureaText.t("home_clear_search"))
            }
        }.padding(.horizontal, AureaDims.s2).frame(height: AureaDims.searchField)
            .background(AureaColors.fieldFilled, in: RoundedRectangle(cornerRadius: AureaDims.radiusChip))
    }

    private var filters: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: AureaDims.s2) {
                filterChip(AureaText.t("effect_todos"), .all)
                if prefs.recents.contains(where: { id in model.effectCatalog.contains { $0.typeId == id } }) {
                    filterChip(AureaText.t("effect_recentes"), .recent)
                }
                filterChip(AureaText.t("effect_favoritos"), .favorite)
                ForEach(categories, id: \.self) { category in
                    filterChip(fxEffectCategoryLabel(category), .category(category))
                }
            }
        }
    }

    private func filterChip(_ label: String, _ value: FxEffectFilter) -> some View {
        Button { filter = filter == value ? .all : value } label: {
            Text(label).font(AureaTextSpec.chipLabel.font).lineLimit(1)
                .foregroundStyle(filter == value ? AureaColors.action : AureaColors.text)
                .padding(.horizontal, AureaDims.s3).padding(.vertical, AureaDims.s2)
                .background(filter == value ? AureaColors.actionDim : AureaColors.chip,
                            in: RoundedRectangle(cornerRadius: AureaDims.radiusChip))
                .overlay(RoundedRectangle(cornerRadius: AureaDims.radiusChip)
                    .stroke(filter == value ? AureaColors.action : .clear, lineWidth: AureaDims.hairline))
        }.buttonStyle(AureaPressStyle(shrink: 1)).accessibilityAddTraits(filter == value ? .isSelected : [])
    }

    private var emptyMessage: String {
        if !query.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty { return AureaText.t("effect_empty_search") }
        switch filter {
        case .favorite: return AureaText.t("effect_empty_favorites")
        case .recent: return AureaText.t("effect_empty_recents")
        default: return AureaText.t("effect_empty_category")
        }
    }
}

private struct EffectBrowserTile: View {
    let entry: EffectCatalogItem
    let store: EffectPreviewStore
    let favorite: Bool
    let onFavorite: () -> Void
    let onOpen: () -> Void

    var body: some View {
        Button {
            UISelectionFeedbackGenerator().selectionChanged()
            onOpen()
        } label: {
            VStack(alignment: .leading, spacing: 0) {
                EffectBrowserPreview(entry: entry, store: store)
                    .overlay(alignment: .bottomLeading) {
                        let cost = fxEffectCost(entry.effectClass)
                        if cost > 1 {
                            Text(AureaText.t(cost >= 3 ? "effect_cost_high" : "effect_cost_medium"))
                                .font(EffectBrowserLayout.cost).foregroundStyle(.white)
                                .padding(.horizontal, 5).padding(.vertical, 2)
                                .background(.black.opacity(0.55), in: RoundedRectangle(cornerRadius: AureaDims.radiusSm))
                                .padding(AureaDims.s2)
                        }
                    }
                Color.clear.frame(height: 6)
                Text(fxEffectDisplayName(entry.typeId, entry.name))
                    .font(.aurea(size: 14, weight: .semibold)).tracking(-0.1).lineLimit(1)
                    .frame(height: 14 * 1.35, alignment: .leading)
                Text(fxEffectCategoryLabel(entry.category)).font(.aurea(size: 11)).tracking(-0.1)
                    .foregroundStyle(AureaColors.muted).lineLimit(1)
                    .frame(height: 11 * 1.35, alignment: .leading)
            }.frame(maxWidth: .infinity, alignment: .leading).contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 0.97))
        .accessibilityLabel("Ver \(fxEffectDisplayName(entry.typeId, entry.name))")
        .overlay(alignment: .topTrailing) {
            Button(action: onFavorite) {
                CupertinoGlyph.text(favorite ? CupertinoGlyph.StarFill : CupertinoGlyph.Star,
                                    size: AureaDims.iconSm, color: favorite ? AureaColors.accent : .white)
                    .frame(width: EffectBrowserLayout.favoriteDisc, height: EffectBrowserLayout.favoriteDisc)
                    .background(.black.opacity(0.35), in: Circle())
                    .frame(width: AureaDims.minTap, height: AureaDims.minTap).contentShape(Rectangle())
            }.buttonStyle(AureaPressStyle())
                .accessibilityLabel(AureaText.t(favorite ? "effect_tirar_favoritos" : "effect_nos_favoritos"))
        }
    }
}

private struct EffectBrowserPreview: View {
    let entry: EffectCatalogItem
    let store: EffectPreviewStore
    @State private var image: UIImage?

    var body: some View {
        GeometryReader { geometry in
            if let image {
                Image(uiImage: image).resizable().interpolation(.low).scaledToFill()
                    .frame(width: geometry.size.width, height: geometry.size.height).clipped()
            } else {
                // Same labelled loading/unavailable plate as Android, never a fake effect.
                Canvas { context, size in
                    context.fill(Path(CGRect(origin: .zero, size: size)), with: .linearGradient(
                        Gradient(colors: [AureaColors.effectPreviewTop, AureaColors.effectPreviewBottom]),
                        startPoint: .zero, endPoint: CGPoint(x: 0, y: size.height)))
                    let radius = size.width * 0.19
                    context.fill(Path(ellipseIn: CGRect(x: size.width * 0.34 - radius, y: size.height * 0.60 - radius,
                                                       width: radius * 2, height: radius * 2)), with: .color(AureaColors.effectPreviewDisc.opacity(0.85)))
                    context.fill(Path(CGRect(x: size.width * 0.20, y: size.height * 0.18, width: size.width * 0.60, height: size.height * 0.012)), with: .color(.white.opacity(0.6)))
                    context.fill(Path(CGRect(x: size.width * 0.20, y: size.height * 0.26, width: size.width * 0.40, height: size.height * 0.012)), with: .color(.white.opacity(0.25)))
                }.overlay(alignment: .bottomTrailing) {
                    CupertinoGlyph.text(fxCategoryGlyph(entry.category), size: AureaDims.iconSm, color: .white.opacity(0.85)).padding(AureaDims.s2)
                }
            }
        }.aspectRatio(EffectBrowserLayout.previewAspect, contentMode: .fit)
            .clipShape(RoundedRectangle(cornerRadius: AureaDims.radiusCard))
            .task(id: entry.typeId) {
                let loaded = await store.image(for: entry.typeId)
                if !Task.isCancelled { image = loaded }
            }
    }
}

private struct EffectDetailHeight: PreferenceKey {
    static var defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) { value = max(value, nextValue()) }
}

private struct EffectBrowserDetail: View {
    let entry: EffectCatalogItem
    let store: EffectPreviewStore
    let specs: [[String: Any]]
    @ObservedObject var prefs: FxEffectPrefs
    private var favorite: Bool { prefs.isFavorite(entry.typeId) }
    let canApply: Bool
    let onDismiss: () -> Void
    let onApply: () -> Void
    @State private var contentHeight: CGFloat = 0

    var body: some View {
        GeometryReader { geometry in
            let available = max(0, geometry.size.height - 21)
            AureaBottomOverlay(modal: true, onDismiss: onDismiss) {
                ScrollView {
                    content
                        .padding(.horizontal, AureaDims.s5).padding(.top, AureaDims.s2).padding(.bottom, AureaDims.s6)
                        .background(GeometryReader { bounds in
                            Color.clear.preference(key: EffectDetailHeight.self, value: bounds.size.height)
                        })
                }
                .frame(height: min(available, contentHeight > 0 ? contentHeight : available))
                .onPreferenceChange(EffectDetailHeight.self) { contentHeight = $0 }
            }
        }.foregroundStyle(AureaColors.text)
    }

    private var content: some View {
        VStack(alignment: .leading, spacing: 0) {
            EffectBrowserPreview(entry: entry, store: store)
            gap(AureaDims.s4)
            HStack(spacing: 0) {
                VStack(alignment: .leading, spacing: 2) {
                    Text(fxEffectDisplayName(entry.typeId, entry.name)).font(.aurea(size: 22, weight: .bold))
                        .tracking(-0.4).frame(minHeight: 22 * 1.35, alignment: .leading)
                    cardSpec(fxEffectCostLine(entry.category, fxEffectCost(entry.effectClass)))
                }.frame(maxWidth: .infinity, alignment: .leading)
                Button { prefs.toggleFavorite(entry.typeId) } label: {
                    CupertinoGlyph.text(favorite ? CupertinoGlyph.StarFill : CupertinoGlyph.Star,
                                        size: AureaDims.iconLg, color: favorite ? AureaColors.accent : AureaColors.muted)
                        .frame(width: AureaDims.minTap, height: AureaDims.minTap)
                }.buttonStyle(AureaPressStyle())
                    .accessibilityLabel(AureaText.t(favorite ? "effect_tirar_favoritos" : "effect_nos_favoritos"))
            }
            gap(AureaDims.s3)
            Text(fxEffectDescription(entry.typeId, entry.category)).font(.aurea(size: 14)).tracking(-0.1)
                .lineSpacing(max(0, 14 * 1.45 - UIFont.systemFont(ofSize: 14).lineHeight))
                .fixedSize(horizontal: false, vertical: true)
            gap(AureaDims.s3)
            cardSpec(fxEffectCompatibilityLine(entry.typeId))
            gap(AureaDims.s5)
            if !specs.isEmpty {
                Text(AureaText.t("effect_parameters")).font(.aurea(size: 11, weight: .semibold))
                    .tracking(0.3).foregroundStyle(AureaColors.muted).frame(minHeight: 11 * 1.35)
                gap(AureaDims.s2)
                ForEach(specs.indices, id: \.self) { index in
                    HStack(spacing: 0) {
                        Text(specs[index]["label"] as? String ?? "").font(.aurea(size: 14)).tracking(-0.1)
                            .frame(maxWidth: .infinity, minHeight: 14 * 1.35, alignment: .leading)
                        cardSpec(summary(specs[index])).multilineTextAlignment(.trailing)
                    }.padding(.vertical, EffectBrowserLayout.parameterPadding)
                }
            }
            gap(AureaDims.s5)
            Button(action: onApply) {
                Text(AureaText.t("effect_adicionar_selecao")).font(.aurea(size: 17, weight: .semibold)).tracking(-0.2)
                    .frame(maxWidth: .infinity).frame(height: AureaDims.buttonHeight)
                    .foregroundStyle(AureaColors.onAccent)
                    .background(AureaColors.accent, in: RoundedRectangle(cornerRadius: AureaDims.radiusLg))
            }.buttonStyle(AureaPressStyle(shrink: 1)).disabled(!canApply).opacity(canApply ? 1 : 0.45)
        }.frame(maxWidth: .infinity, alignment: .leading)
    }

    private func gap(_ height: CGFloat) -> some View { Color.clear.frame(height: height) }
    private func cardSpec(_ text: String) -> some View {
        Text(text).font(.aurea(size: 11)).tracking(-0.1).foregroundStyle(AureaColors.muted)
            .frame(minHeight: 11 * 1.35).fixedSize(horizontal: false, vertical: true)
    }
    private func summary(_ spec: [String: Any]) -> String {
        let enums = spec["enumLabels"] as? [String] ?? []
        if !enums.isEmpty { return enums.prefix(3).joined(separator: ", ") + (enums.count > 3 ? "…" : "") }
        switch (spec["type"] as? NSNumber)?.intValue {
        case fxParamColor: return "Cor" // The source's parameter summary is literal.
        case fxParamBool: return AureaText.t("effect_param_bool")
        case fxParamPoint2D: return AureaText.t("effect_param_point")
        default:
            let low = (spec["min"] as? NSNumber)?.floatValue ?? 0
            let high = (spec["max"] as? NSNumber)?.floatValue ?? 0
            let unit = spec["unit"] as? String ?? ""
            return low.isFinite && high.isFinite && high > low
                ? "\(fxTrimNumber(low)) a \(fxTrimNumber(high))" + (unit.isEmpty ? "" : " " + unit) : unit
        }
    }
    private func fxTrimNumber(_ value: Float) -> String {
        if value.rounded() == value { return String(format: "%.0f", locale: Locale(identifier: "en_US_POSIX"), Double(value)) }
        return String(format: "%.2f", locale: Locale(identifier: "en_US_POSIX"), Double(value))
            .replacingOccurrences(of: "0+$", with: "", options: .regularExpression)
            .replacingOccurrences(of: "\\.$", with: "", options: .regularExpression)
    }
}
