// =============================================================================
//  Aurea / platform / ios / app / ExportView.swift
//
//  O export. As opções são as mesmas do Android (formato, resolução, qualidade,
//  fps, faixas de áudio) e o rodapé diz o que o aparelho NÃO faz — nada é
//  escondido (é a regra do §109, e o `DeviceReport` do motor responde).
//
//  O trabalho é do MOTOR: ele percorre a timeline no tempo de saída, renderiza
//  cada quadro com o MESMO renderer do preview, converte para Y'CbCr na GPU e
//  mixa o áudio. Aqui só se escolhe e se acompanha — o encoder é o
//  VTCompressionSession + AVAssetWriter do `IOSVideoDecoder.mm`.
// =============================================================================
import SwiftUI
import UIKit

struct ExportView: View {
    @EnvironmentObject private var model: AureaModel
    @Environment(\.dismiss) private var dismiss

    private let shortSides: [UInt32] = [720, 1080, 1440, 2160]
    private let bitrates: [(String, UInt32)] = [
        ("8 Mbps", 8), ("12 Mbps", 12), ("20 Mbps", 20), ("40 Mbps", 40), ("80 Mbps", 80),
    ]

    var body: some View {
        NavigationStack {
            Group {
                if model.exporting {
                    progress
                } else {
                    options
                }
            }
            .background(AureaColors.background)
            .navigationTitle(AureaText.t("editor_exportar"))
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    if model.exporting {
                        Button(AureaText.t("common_cancel")) {
                            model.cancelExport()
                            dismiss()
                        }
                    } else {
                        Button(AureaText.t("common_cancel")) { dismiss() }
                    }
                }
                if !model.exporting {
                    ToolbarItem(placement: .confirmationAction) {
                        Button(AureaText.t("editor_exportar")) {
                            model.startExport()
                        }
                    }
                }
            }
        }
        .preferredColorScheme(.dark)
    }

    // =========================================================================
    // Opções
    // =========================================================================
    private var options: some View {
        Form {
            Section(AureaText.t("editor_formato")) {
                Picker("", selection: $model.exportOptions.codec) {
                    Text("H.264").tag(AureaExportCodec.h264)
                    Text("HEVC").tag(AureaExportCodec.hevc)
                }
                .pickerStyle(.segmented)
                Text(AureaText.t(model.exportOptions.codec == .h264
                                 ? "editor_h_264_abre_qualquer_aparelho_rede"
                                 : "editor_hevc_arquivo_menor_mesma_qualidade_alguns"))
                    .font(AureaType.tiny)
                    .foregroundStyle(AureaColors.subtle)
            }

            Section(AureaText.t("editor_resolucao")) {
                Picker("", selection: $model.exportOptions.shortSide) {
                    ForEach(shortSides, id: \.self) { side in
                        Text(label(for: side)).tag(side)
                    }
                }
                .pickerStyle(.segmented)
                Text(exportFrame)
                    .font(AureaType.value)
                    .foregroundStyle(AureaColors.muted)
            }

            Section(AureaText.t("editor_qualidade")) {
                Picker("", selection: $model.exportOptions.bitrateMbps) {
                    ForEach(bitrates, id: \.1) { option in
                        Text(option.0).tag(option.1)
                    }
                }
                .pickerStyle(.menu)
            }

            Section(AureaText.t("editor_quadros_segundo")) {
                Picker("", selection: $model.exportOptions.fps) {
                    Text(AureaText.t("sh_export_fps_from_project")).tag(0.0)
                    Text("24").tag(24.0)
                    Text("30").tag(30.0)
                    Text("60").tag(60.0)
                }
                .pickerStyle(.segmented)
            }

            Section(AureaText.t("editor_duracao")) {
                Text(model.timecode(model.compositionDuration))
                    .font(AureaType.value)
                    .foregroundStyle(AureaColors.text)
                Text(AureaText.t("editor_mantenha_aurea_aberto_ate_terminar"))
                    .font(AureaType.tiny)
                    .foregroundStyle(AureaColors.subtle)
            }

            if let limit = model.deviceReport["maxExportHeight"]?.intValue, limit > 0,
               Int(model.exportOptions.shortSide) > limit {
                Section {
                    Text(AureaText.t("sh_export_above_device"))
                        .font(AureaType.tiny)
                        .foregroundStyle(AureaColors.warning)
                }
            }
        }
    }

    private func label(for short: UInt32) -> String {
        // O rótulo é o do ANDROID: resolução é o lado menor.
        switch short {
        case 720: return "720p"
        case 1080: return "1080p"
        case 1440: return "1440p"
        case 2160: return "4K"
        default: return "\(short)p"
        }
    }

    private var exportFrame: String {
        let compositionW = Double(model.compositionWidth)
        let compositionH = Double(model.compositionHeight)
        let short = min(compositionW, compositionH)
        let k = Double(model.exportOptions.shortSide) / short
        let width = UInt32((compositionW * k).rounded())
        let height = UInt32((compositionH * k).rounded())
        return "\(width) × \(height)"
    }

    // =========================================================================
    // Progresso
    // =========================================================================
    private var progress: some View {
        VStack(spacing: 18) {
            let done = (model.exportProgress["framesDone"] as? NSNumber)?.doubleValue ?? 0
            let total = max(1, (model.exportProgress["framesTotal"] as? NSNumber)?.doubleValue ?? 1)

            ProgressView(value: done, total: total)
                .tint(AureaColors.accent)
                .padding(.horizontal, AureaDims.pad)

            Text(String(format: "%.0f / %.0f", done, total))
                .font(AureaType.value)
                .foregroundStyle(AureaColors.text)

            if let eta = (model.exportProgress["etaSeconds"] as? NSNumber)?.intValue, eta > 0 {
                Text("~\(eta)s")
                    .font(AureaType.tiny)
                    .foregroundStyle(AureaColors.subtle)
            }

            let flags = (model.exportProgress["flags"] as? NSNumber)?.uint32Value ?? 0
            if flags & AureaExportFlag.softwareEncoder.rawValue != 0 {
                // Caiu para software: a UI AVISA (não faz silenciosamente um
                // export várias vezes mais lento).
                Text(AureaText.t("editor_hevc_arquivo_menor_mesma_qualidade_alguns"))
                    .font(AureaType.tiny)
                    .foregroundStyle(AureaColors.warning)
            }
            if flags & AureaExportFlag.thermalReduced.rawValue != 0 {
                Text("Calor: menos quadros em voo (a qualidade é a mesma)")
                    .font(AureaType.tiny)
                    .foregroundStyle(AureaColors.warning)
            }

            Text(AureaText.t("editor_mantenha_aurea_aberto_ate_terminar"))
                .font(AureaType.tiny)
                .foregroundStyle(AureaColors.subtle)

            Spacer()
        }
        .padding(.top, 30)
    }
}
