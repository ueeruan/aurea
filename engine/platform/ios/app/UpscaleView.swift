// Upscale com IA — ferramenta própria, fora da aba Exportar (par do Android:
// upscale/UpscaleScreen.kt). A pessoa escolhe um vídeo da Fototeca, o fator
// (2× ou 4×) e toca em Melhorar: o modelo cria um projeto com o tamanho do
// vídeo, importa e exporta com a rede neural (AureaModel.upscaleVideo). O
// progresso e o resultado são os do export normal.
import AVFoundation
import PhotosUI
import SwiftUI
import UniformTypeIdentifiers

@MainActor struct UpscaleView: View {
    @EnvironmentObject private var model: AureaModel
    @State private var picking = false
    @State private var picked: URL?
    @State private var factor: UInt32 = 2
    @State private var sharing = false
    @State private var viewing = false

    private var busy: Bool { model.exporting || model.upscaleStage == .preparing }

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 4) {
                Button { model.closeUpscale() } label: {
                    Image(systemName: "chevron.left").font(.system(size: 18, weight: .semibold))
                        .foregroundStyle(busy ? AureaColors.disabled : AureaColors.text)
                        .frame(width: 40, height: 40)
                }
                .buttonStyle(.plain).disabled(busy)
                .accessibilityLabel(AureaText.t("common_back")).accessibilityIdentifier("upscale.voltar")
                Text(AureaText.t("upscale_title")).font(.aurea(size: 17, weight: .semibold)).foregroundStyle(AureaColors.text)
                Spacer()
            }
            .frame(height: 44).padding(.horizontal, 6)
            ScrollView {
                VStack(alignment: .leading, spacing: 0) {
                    if model.exporting { processing }
                    else if let url = model.exportedURL, model.upscaleStage == .done { done(url) }
                    else { options }
                    Spacer().frame(height: 24)
                }.padding(.horizontal, 18)
            }
        }
        .background(AureaColors.background.ignoresSafeArea()).foregroundStyle(AureaColors.text)
        .preferredColorScheme(.dark)
        .sheet(isPresented: $picking) {
            UpscaleVideoPicker(onDismiss: { picking = false }, onPick: { url in picking = false; picked = url; model.upscaleReset() },
                               onError: { error in picking = false; model.toast = error })
        }
        .sheet(isPresented: $sharing) { if let url = model.exportedURL { ExportShareSheet(url: url) } }
        .sheet(isPresented: $viewing) { if let url = model.exportedURL { ExportMoviePlayer(url: url) } }
    }

    private var muted: some ShapeStyle { AureaColors.muted }

    private var options: some View {
        VStack(alignment: .leading, spacing: 0) {
            Spacer().frame(height: 16)
            Text(AureaText.t("upscale_hint")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                .fixedSize(horizontal: false, vertical: true)
            Spacer().frame(height: 16)
            Button { picking = true } label: {
                VStack(spacing: 8) {
                    Image(systemName: "photo.on.rectangle").font(.system(size: 28)).foregroundStyle(AureaColors.accent)
                    Text(picked.map { model.upscaleSourceName ?? $0.deletingPathExtension().lastPathComponent } ?? AureaText.t("upscale_pick"))
                        .font(.aurea(size: 15, weight: .semibold)).foregroundStyle(AureaColors.text).multilineTextAlignment(.center)
                }
                .frame(maxWidth: .infinity, minHeight: 120).padding(18)
                .background(RoundedRectangle(cornerRadius: 14).fill(AureaColors.surface))
            }
            .buttonStyle(.plain).disabled(busy)
            .accessibilityLabel(AureaText.t("upscale_pick")).accessibilityIdentifier("upscale.escolher")
            Spacer().frame(height: 18)
            Text(AureaText.t("upscale_factor")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
            Spacer().frame(height: 8)
            HStack(spacing: 8) {
                ForEach([UInt32(2), UInt32(4)], id: \.self) { f in
                    Button { factor = f } label: {
                        Text("\(f)×").font(.aurea(size: 14, weight: .semibold))
                            .foregroundStyle(factor == f ? AureaColors.text : AureaColors.muted)
                            .padding(.horizontal, 18).frame(minHeight: 40)
                            .background(RoundedRectangle(cornerRadius: 10).fill(factor == f ? AureaColors.accent.opacity(0.22) : AureaColors.chip))
                    }
                    .buttonStyle(.plain).disabled(busy)
                    .accessibilityAddTraits(factor == f ? .isSelected : [])
                    .accessibilityIdentifier("upscale.fator.\(f)")
                }
            }
            if let size = model.upscaleSourceSize {
                Spacer().frame(height: 8)
                Text(String(format: AureaText.t("upscale_result"), Int(size.width), Int(size.height), Int(size.width) * Int(factor), Int(size.height) * Int(factor)))
                    .font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
            }
            if model.upscaleStage == .preparing {
                Spacer().frame(height: 18)
                ProgressView().tint(AureaColors.accent)
                Spacer().frame(height: 8)
                Text(AureaText.t("upscale_preparing")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
            }
            if model.upscaleStage == .failed || (model.exportMessage != nil && !model.exporting && model.exportedURL == nil) {
                Spacer().frame(height: 14)
                Text(model.exportMessage ?? AureaText.t("import_failed"))
                    .font(.aurea(size: 14)).foregroundStyle(AureaColors.danger)
                    .frame(maxWidth: .infinity, alignment: .leading).padding(14)
                    .background(RoundedRectangle(cornerRadius: 10).fill(AureaColors.surface))
                    .accessibilityIdentifier("upscale.erro")
            }
            Spacer().frame(height: 24)
            Text(AureaText.t("upscale_time_note")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                .fixedSize(horizontal: false, vertical: true)
            Spacer().frame(height: 14)
            bigButton(AureaText.t("upscale_start"), "upscale.iniciar", enabled: picked != nil && !busy) {
                if let url = picked { model.upscaleVideo(url: url, factor: factor) }
            }
        }
    }

    private var processing: some View {
        VStack(alignment: .leading, spacing: 0) {
            Spacer().frame(height: 24)
            Text(AureaText.t("upscale_processing")).font(.aurea(size: 22, weight: .bold))
            Spacer().frame(height: 8)
            Text(AureaText.t("upscale_time_note")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                .fixedSize(horizontal: false, vertical: true)
            Spacer().frame(height: 18)
            let done = (model.exportProgress["framesDone"] as? NSNumber)?.intValue ?? 0
            let total = (model.exportProgress["framesTotal"] as? NSNumber)?.intValue ?? 0
            let eta = (model.exportProgress["etaSeconds"] as? NSNumber)?.intValue ?? 0
            let fraction = total > 0 ? Double(done) / Double(total) : 0
            ProgressView(value: fraction).tint(AureaColors.accent).accessibilityIdentifier("upscale.progresso")
            Spacer().frame(height: 10)
            Text(total > 0 ? "\(Int(fraction * 100))% · \(done)/\(total)" + (eta > 0 ? " · \(eta / 60)m \(eta % 60)s" : "") : "\(Int(fraction * 100))%")
                .font(.aurea(size: 14, weight: .semibold))
            if let message = model.exportProgress["message"] as? String, message.hasPrefix("IA:") {
                Spacer().frame(height: 8)
                Text(AureaEngineText.aiProgress(message)).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
            }
            Spacer().frame(height: 24)
            bigButton(AureaText.t("common_cancel"), "upscale.cancelar", primary: false) { model.cancelExport() }
        }
    }

    private func done(_ url: URL) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            Spacer().frame(height: 24)
            VStack(spacing: 10) {
                Image(systemName: "checkmark.circle.fill").font(.system(size: 44)).foregroundStyle(AureaColors.success)
                Text(AureaText.t("upscale_done")).font(.aurea(size: 22, weight: .bold))
                Text(model.exportSavedToPhotos ? AureaText.t("ios_export_saved_photos") : url.lastPathComponent)
                    .font(.aurea(size: 14)).foregroundStyle(AureaColors.muted).multilineTextAlignment(.center)
            }.frame(maxWidth: .infinity)
            Spacer().frame(height: 24)
            bigButton(AureaText.t("upscale_open"), "upscale.abrir") { viewing = true }
            Spacer().frame(height: 10)
            bigButton(AureaText.t("upscale_share"), "upscale.compartilhar", primary: false) { sharing = true }
            Spacer().frame(height: 10)
            bigButton(AureaText.t("upscale_again"), "upscale.outro", primary: false) { picked = nil; model.upscaleReset() }
        }
    }

    private func bigButton(_ text: String, _ id: String, primary: Bool = true, enabled: Bool = true, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(text).font(.aurea(size: 15, weight: .semibold))
                .foregroundStyle(!enabled ? AureaColors.disabled : (primary ? AureaColors.onAccent : AureaColors.text))
                .frame(maxWidth: .infinity, minHeight: 48)
                .background(RoundedRectangle(cornerRadius: 12).fill(!enabled ? AureaColors.surface : (primary ? AureaColors.accent : AureaColors.chip)))
        }
        .buttonStyle(.plain).disabled(!enabled)
        .accessibilityLabel(text).accessibilityIdentifier(id)
    }
}

/// Seletor só de vídeos (PHPicker). O arquivo temporário do provedor é
/// copiado enquanto o callback vive, como o HomeMediaPicker.
private struct UpscaleVideoPicker: UIViewControllerRepresentable {
    let onDismiss: () -> Void
    let onPick: (URL) -> Void
    let onError: (String) -> Void
    func makeCoordinator() -> Coordinator { Coordinator(self) }
    func makeUIViewController(context: Context) -> PHPickerViewController {
        var configuration = PHPickerConfiguration()
        configuration.filter = .videos
        configuration.selectionLimit = 1
        configuration.preferredAssetRepresentationMode = .current
        let picker = PHPickerViewController(configuration: configuration)
        picker.delegate = context.coordinator
        return picker
    }
    func updateUIViewController(_ controller: PHPickerViewController, context: Context) {}
    final class Coordinator: NSObject, PHPickerViewControllerDelegate {
        let parent: UpscaleVideoPicker
        init(_ parent: UpscaleVideoPicker) { self.parent = parent }
        func picker(_ picker: PHPickerViewController, didFinishPicking results: [PHPickerResult]) {
            guard let provider = results.first?.itemProvider else { parent.onDismiss(); return }
            let identifier = provider.registeredTypeIdentifiers.first { UTType($0)?.conforms(to: .movie) == true } ?? UTType.movie.identifier
            provider.loadFileRepresentation(forTypeIdentifier: identifier) { [parent] url, error in
                guard let url else {
                    DispatchQueue.main.async { parent.onError(error?.localizedDescription ?? AureaText.t("ios_media_open_failed")) }
                    return
                }
                do {
                    let directory = FileManager.default.temporaryDirectory.appendingPathComponent("UpscaleImports", isDirectory: true)
                        .appendingPathComponent(UUID().uuidString, isDirectory: true)
                    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
                    let copy = directory.appendingPathComponent(url.lastPathComponent)
                    try FileManager.default.copyItem(at: url, to: copy)
                    DispatchQueue.main.async { parent.onPick(copy) }
                } catch {
                    DispatchQueue.main.async { parent.onError(error.localizedDescription) }
                }
            }
        }
    }
}
