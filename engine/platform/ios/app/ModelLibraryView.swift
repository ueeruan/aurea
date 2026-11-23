import SwiftUI
import UIKit

private struct LibraryModel: Decodable, Identifiable {
    let id: String
    let names: [String: String]
    let provider: String
    let category: String

    var name: String {
        let language = AureaText.language.resolved
        return names[language == .pt ? "pt" : language.rawValue] ?? names["en"] ?? id
    }

    func resource(_ ext: String) -> URL? {
        Bundle.main.url(forResource: id, withExtension: ext, subdirectory: "ModelLibrary")
    }

    static let catalog: [LibraryModel] = {
        guard let path = Bundle.main.url(forResource: "catalog", withExtension: "json", subdirectory: "ModelLibrary"),
              let data = try? Data(contentsOf: path),
              let models = try? JSONDecoder().decode([LibraryModel].self, from: data) else { return [] }
        return models
    }()
}

struct ModelLibraryView: View {
    let actionKey: String
    let onUse: (URL, String) -> Void
    var onClose: (() -> Void)? = nil
    @State private var category = ""

    var body: some View {
        VStack(spacing: 8) {
            HStack {
                Text(AureaText.t("model_shop_title")).font(.aurea(size: 24, weight: .semibold))
                    .foregroundStyle(AureaColors.lightText).accessibilityAddTraits(.isHeader)
                Spacer()
                if let onClose {
                    Button(AureaText.t("editor_fechar"), action: onClose)
                }
            }.padding(.horizontal, 16).padding(.top, 10)
            Picker(AureaText.t("model_shop_title"), selection: $category) {
                Text(AureaText.t("model_shop_all")).tag("")
                Text(AureaText.t("model_shop_low_poly")).tag("low_poly")
                Text(AureaText.t("model_shop_realistic")).tag("realistic")
            }
            .pickerStyle(.segmented)
            .padding(.horizontal, 16)
            .accessibilityIdentifier("models.filter")
            if LibraryModel.catalog.isEmpty {
                Text(AureaText.t("msg_nao_consegui_ler_esse_arquivo"))
                    .foregroundStyle(AureaColors.lightTextMuted)
            }
            ScrollView {
                LazyVGrid(columns: [GridItem(.adaptive(minimum: 144), spacing: 12)], spacing: 12) {
                    ForEach(LibraryModel.catalog.filter { category.isEmpty || $0.category == category }) { model in
                        card(model)
                    }
                }.padding(16)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .background(AureaColors.lightBackground)
        .accessibilityIdentifier("models.library")
    }

    private func card(_ model: LibraryModel) -> some View {
        let url = model.resource("glb")
        return VStack(alignment: .leading, spacing: 0) {
            Color.clear.aspectRatio(1.16, contentMode: .fit).overlay {
                if let path = model.resource("jpg")?.path, let thumb = UIImage(contentsOfFile: path) {
                    Image(uiImage: thumb).resizable().scaledToFit().accessibilityHidden(true)
                }
            }.background(AureaColors.lightBackground).clipped()
            VStack(alignment: .leading, spacing: 2) {
                Text(model.name).font(.aurea(size: 15, weight: .medium)).foregroundStyle(AureaColors.lightText)
                Text("\(model.provider) · CC0").font(.aurea(size: 12)).foregroundStyle(AureaColors.lightTextMuted)
            }.padding(.horizontal, 10).padding(.top, 8)
            Button {
                if let url { onUse(url, model.name) }
            } label: {
                Text(AureaText.t(actionKey))
                    .font(.aurea(size: 14, weight: .medium))
                    .frame(minHeight: 44)
            }
            .buttonStyle(.plain)
            .foregroundStyle(AureaColors.accent)
            .padding(.horizontal, 10)
            .disabled(url == nil)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(AureaColors.lightSurface, in: RoundedRectangle(cornerRadius: 14))
        .clipShape(RoundedRectangle(cornerRadius: 14))
        .accessibilityIdentifier("models.card.\(model.id)")
    }
}
