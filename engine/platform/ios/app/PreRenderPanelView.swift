// =============================================================================
//  PreRenderPanelView.swift — Pre Render & Pre Export (1.0.2)
//
//  O painel escolhe o trecho (Início, Fim, Inteiro, Limpar), vai a ele (Ir ao
//  início/fim), repete (Loop), escolhe a qualidade (Inteira, 1/2, 1/4, 1/8)
//  com o "cabe ~Xs" ao vivo e pede ao motor o PreRender (quadros no cache) ou
//  o pré-export (um MP4 no cache que o player toca no lugar do render ao vivo).
//  Toda conta é do motor (EnginePreRenderRange.cpp); o Android tem o mesmo
//  painel (PreRenderPanel.kt), com os mesmos identificadores de teste.
// =============================================================================
import AVKit
import SwiftUI

/// "12s", "2m 05s", "1h 03m" (o mesmo formato do Android, preRenderDuration).
func preRenderDuration(_ seconds: Double) -> String {
    let s = max(0, seconds)
    if s < 10 { return String(format: "%.1fs", locale: Locale(identifier: "en_US_POSIX"), s) }
    let total = Int64(s)
    if total < 60 { return "\(total)s" }
    if total < 3600 { return "\(total / 60)m \(String(format: "%02lld", total % 60))s" }
    return "\(total / 3600)h \(String(format: "%02lld", (total % 3600) / 60))m"
}

struct PreRenderPanelView: View {
    @EnvironmentObject private var model: AureaModel
    @State private var help = false
    private let qualities = [1, 2, 4, 8]

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                header
                modeSelector
                sectionTitle(AureaText.t("prx_quality"))
                HStack(spacing: 6) { ForEach(qualities, id: \.self) { qualityChip($0) } }
                    .padding(.horizontal, 12)
                fitSummary
                rangeSection
                actionSection
            }
            .padding(.bottom, 16)
        }
        .background(AureaColors.editorPanel.ignoresSafeArea())
        .accessibilityIdentifier("prerender.panel")
        .alert(AureaText.t("prx_help_title"), isPresented: $help) {
            Button(AureaText.t("prx_close"), role: .cancel) {}
        } message: {
            Text(AureaText.t("prx_help_body"))
        }
        .onAppear { model.refreshPreRenderPanel() }
        .onDisappear { model.closePreRenderPanel() }
    }

    private func qualityLabel(_ divisor: Int) -> String {
        divisor == 1 ? AureaText.t("prx_quality_full") : "1/\(divisor)"
    }

    private func sectionTitle(_ title: String) -> some View {
        Text(title)
            .font(.system(size: 12, weight: .semibold))
            .foregroundColor(AureaColors.muted)
            .padding(.leading, 20).padding(.top, 14).padding(.bottom, 4)
    }

    private var header: some View {
        HStack {
            Text(AureaText.t("prx_title"))
                .font(.system(size: 17, weight: .semibold))
                .foregroundColor(AureaColors.text)
                .accessibilityAddTraits(.isHeader)
            Spacer()
            Button { help = true } label: {
                CupertinoGlyph.text(CupertinoGlyph.InfoCircle, size: 20, color: AureaColors.muted)
                    .frame(width: 44, height: 44).contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle(shrink: 1))
            .accessibilityLabel(AureaText.t("prx_help"))
            .accessibilityIdentifier("prerender.help")
        }
        .padding(.leading, 20).padding(.trailing, 8).padding(.top, 14)
    }

    private var modeSelector: some View {
        HStack(spacing: 0) {
            ForEach([0, 1], id: \.self) { mode in
                let on = model.preRenderMode == mode
                let label = AureaText.t(mode == 0 ? "prx_mode_prerender" : "prx_mode_preexport")
                Button { model.changePreRenderMode(mode) } label: {
                    Text(label)
                        .font(.system(size: 14, weight: .semibold))
                        .foregroundColor(on ? AureaColors.onAccent : AureaColors.text)
                        .frame(maxWidth: .infinity, minHeight: 30)
                        .background(RoundedRectangle(cornerRadius: 8).fill(on ? AureaColors.accent : Color.clear))
                        .contentShape(Rectangle())
                }
                .buttonStyle(AureaPressStyle(shrink: 1))
                .accessibilityLabel(label)
                .accessibilityAddTraits(on ? [.isSelected] : [])
                .accessibilityIdentifier(mode == 0 ? "prerender.mode.prerender" : "prerender.mode.preexport")
            }
        }
        .padding(3)
        .background(RoundedRectangle(cornerRadius: 10).fill(AureaColors.chip))
        .padding(.horizontal, 16).padding(.vertical, 6)
        .accessibilityElement(children: .contain)
        .accessibilityLabel(AureaText.t("prx_mode"))
    }

    private func qualityChip(_ divisor: Int) -> some View {
        let on = model.preRenderQuality == divisor
        let fit = model.preRenderFits[divisor]
        let label = qualityLabel(divisor)
        let fits = fit.map { AureaText.t("prx_fits", preRenderDuration($0.fitsSeconds)) } ?? "…"
        return Button { model.changePreRenderQuality(divisor) } label: {
            VStack(spacing: 2) {
                Text(label).font(.system(size: 15, weight: .semibold))
                    .foregroundColor(on ? AureaColors.onAccent : AureaColors.text)
                Text(fits).font(.system(size: 11).monospacedDigit()).lineLimit(1)
                    .foregroundColor(on ? AureaColors.onAccent.opacity(0.85)
                                     : (fit.map { !$0.fits } ?? false) ? AureaColors.warning : AureaColors.muted)
            }
            .frame(maxWidth: .infinity, minHeight: 58)
            .background(RoundedRectangle(cornerRadius: 10).fill(on ? AureaColors.accent : AureaColors.chip))
            .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .accessibilityLabel("\(label), \(fits)")
        .accessibilityAddTraits(on ? [.isSelected] : [])
        .accessibilityIdentifier("prerender.quality.\(divisor)")
    }

    @ViewBuilder private var fitSummary: some View {
        if let fit = model.preRenderFits[model.preRenderQuality] {
            VStack(alignment: .leading, spacing: 2) {
                Text(AureaText.t("prx_fits", preRenderDuration(fit.fitsSeconds)))
                    .font(.system(size: 22, weight: .semibold).monospacedDigit())
                    .foregroundColor(fit.fits ? AureaColors.text : AureaColors.warning)
                    .accessibilityIdentifier("prerender.fits")
                Text(AureaText.t("prx_fits_detail", fit.width, fit.height, preRenderDuration(fit.rangeSeconds)))
                    .font(.system(size: 12).monospacedDigit()).foregroundColor(AureaColors.muted)
                Text(AureaText.t("prx_fits_hint")).font(.system(size: 12)).foregroundColor(AureaColors.subtle)
                if !fit.fits && fit.rangeSeconds > 0 {
                    Text(AureaText.t("prx_not_fit")).font(.system(size: 12)).foregroundColor(AureaColors.warning)
                        .padding(.top, 4).accessibilityIdentifier("prerender.notFit")
                }
            }
            .padding(.horizontal, 20).padding(.vertical, 8)
            .accessibilityElement(children: .combine)
        }
    }

    private var rangeSection: some View {
        let range = model.preRenderRange
        let fps = Float(range.fps)
        let from = ShellClock.short(range.effectiveIn, fps), to = ShellClock.short(range.effectiveOut, fps)
        return VStack(alignment: .leading, spacing: 6) {
            Text(range.chosen ? AureaText.t("prx_range", from, to) : AureaText.t("prx_range_whole", from, to))
                .font(.system(size: 13).monospacedDigit()).foregroundColor(AureaColors.text)
                .padding(.horizontal, 20).padding(.vertical, 6)
                .accessibilityIdentifier("prerender.range")
            // Em LTR também em árabe: o tempo corre da esquerda para a direita.
            VStack(spacing: 6) {
                HStack(spacing: 6) {
                    rangeButton(CupertinoGlyph.ArrowRightToLine, "prx_start", tag: "prerender.start") { model.preRenderRangeAction(0) }
                    rangeButton(CupertinoGlyph.ArrowLeftToLine, "prx_end", tag: "prerender.end") { model.preRenderRangeAction(1) }
                    rangeButton(CupertinoGlyph.Film, "prx_full", tag: "prerender.full") { model.preRenderRangeAction(2) }
                    rangeButton(CupertinoGlyph.Xmark, "prx_clear", tag: "prerender.clear", enabled: range.chosen) { model.preRenderRangeAction(3) }
                }
                HStack(spacing: 6) {
                    rangeButton(CupertinoGlyph.BackwardEnd, "prx_go_start", tag: "prerender.goStart") { model.preRenderGoStart() }
                    rangeButton(CupertinoGlyph.ForwardEnd, "prx_go_end", tag: "prerender.goEnd") { model.preRenderGoEnd() }
                    rangeButton(CupertinoGlyph.Repeat, "prx_loop", tag: "prerender.loop", on: range.loop) {
                        model.setPreRenderLoop(!range.loop)
                    }
                }
            }
            .padding(.horizontal, 12)
            .keepLtr()
        }
    }

    private func rangeButton(_ glyph: Character, _ key: String, tag: String, enabled: Bool = true, on: Bool? = nil,
                             action: @escaping () -> Void) -> some View {
        let lit = on == true
        return Button(action: action) {
            VStack(spacing: 3) {
                CupertinoGlyph.text(glyph, size: 18, color: lit ? AureaColors.onAccent : AureaColors.text)
                Text(AureaText.t(key)).font(.system(size: 11)).lineLimit(1)
                    .foregroundColor(lit ? AureaColors.onAccent : AureaColors.text)
            }
            .frame(maxWidth: .infinity, minHeight: 54)
            .background(RoundedRectangle(cornerRadius: 10).fill(lit ? AureaColors.accent : AureaColors.chip))
            .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .disabled(!enabled)
        .opacity(enabled ? 1 : 0.4)
        .accessibilityLabel(AureaText.t(key))
        .accessibilityValue(on.map { AureaText.t($0 ? "common_on" : "common_off") } ?? "")
        .accessibilityIdentifier(tag)
    }

    @ViewBuilder private var actionSection: some View {
        let mode = model.preRenderMode
        let progress = model.preRenderProgress
        let export = model.preExport
        let preparing = mode == 0 ? model.preRenderPreparing : model.preExportPreparing
        let running = preparing || (mode == 0 ? progress.running : export.running)
        Spacer().frame(height: 12)
        if running {
            let fraction = mode == 0 ? progress.fraction : export.fraction
            let text = preparing ? AureaText.t("prx_preparing")
                : AureaText.t(mode == 0 ? "prx_progress_prerender" : "prx_progress_preexport", Int((fraction * 100).rounded()))
            HStack(spacing: 12) {
                VStack(alignment: .leading, spacing: 6) {
                    Text(text).font(.system(size: 14).monospacedDigit()).foregroundColor(AureaColors.text)
                        .accessibilityIdentifier("prerender.progress")
                    GeometryReader { g in
                        ZStack(alignment: .leading) {
                            RoundedRectangle(cornerRadius: 3).fill(AureaColors.chip)
                            RoundedRectangle(cornerRadius: 3).fill(AureaColors.accent)
                                .frame(width: g.size.width * CGFloat(preparing ? 0.06 : min(1, max(0, fraction))))
                        }
                    }.frame(height: 6)
                }
                .accessibilityElement(children: .combine)
                Button { if mode == 0 { model.cancelPreRender() } else { model.cancelPreExport() } } label: {
                    Text(AureaText.t("prx_cancel")).font(.system(size: 14)).foregroundColor(AureaColors.text)
                        .padding(.horizontal, 14).frame(minHeight: 40)
                        .overlay(RoundedRectangle(cornerRadius: 10).stroke(AureaColors.muted, lineWidth: 1))
                        .contentShape(Rectangle())
                }
                .buttonStyle(AureaPressStyle(shrink: 1))
                .accessibilityIdentifier("prerender.cancel")
            }
            .padding(.horizontal, 16)
        } else {
            let label = AureaText.t(mode == 0 ? "prx_action_prerender" : "prx_action_preexport")
            Button { if mode == 0 { model.startPreRenderRange() } else { model.startPreExport() } } label: {
                Text(label).font(.system(size: 16, weight: .semibold)).foregroundColor(AureaColors.onAccent)
                    .frame(maxWidth: .infinity, minHeight: 48)
                    .background(RoundedRectangle(cornerRadius: 12).fill(AureaColors.accent))
                    .contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle())
            .padding(.horizontal, 16)
            .accessibilityLabel(label)
            .accessibilityIdentifier(mode == 0 ? "prerender.start.prerender" : "prerender.start.preexport")
        }
        if let status = statusText(mode: mode, progress: progress, export: export) {
            let warn = (mode == 0 && progress.dirtySegments > 0) || (mode == 1 && export.stale)
            Text(status).font(.system(size: 13)).foregroundColor(warn ? AureaColors.warning : AureaColors.success)
                .padding(.horizontal, 20).padding(.vertical, 10)
                .accessibilityIdentifier("prerender.status")
        }
        if mode == 1 && export.playable {
            Button { model.watchPreExport() } label: {
                HStack(spacing: 8) {
                    CupertinoGlyph.text(CupertinoGlyph.PlayFill, size: 16, color: AureaColors.accent)
                    Text(AureaText.t("prx_watch")).font(.system(size: 15, weight: .semibold)).foregroundColor(AureaColors.accent)
                }
                .frame(maxWidth: .infinity, minHeight: 46)
                .overlay(RoundedRectangle(cornerRadius: 12).stroke(AureaColors.accent, lineWidth: 1))
                .contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle())
            .padding(.horizontal, 16)
            .accessibilityLabel(AureaText.t("prx_watch"))
            .accessibilityIdentifier("prerender.watch")
        }
    }

    private func statusText(mode: Int, progress: PreRenderState, export: PreExportState) -> String? {
        if mode == 0 {
            if progress.complete && progress.dirtySegments > 0 { return AureaText.t("prx_stale", progress.dirtySegments) }
            if progress.complete { return AureaText.t("prx_ready_prerender") }
            return nil
        }
        if export.complete && export.stale { return AureaText.t("prx_stale_export") }
        if export.complete { return AureaText.t("prx_ready_preexport") }
        return nil
    }
}

/// O pré-export tocando: o MP4 do trecho decodificado pelo AVPlayer (sem render
/// ao vivo), do ponto do cabeçote dentro do trecho, repetindo com o Loop.
/// Fechar leva o cabeçote ao instante em que parou.
struct PreExportPlayerView: View {
    @EnvironmentObject private var model: AureaModel
    let path: String
    @StateObject private var holder = PreExportPlayerHolder()

    var body: some View {
        ZStack(alignment: .topTrailing) {
            Color.black.ignoresSafeArea()
            if let player = holder.player {
                VideoPlayer(player: player)
                    .ignoresSafeArea()
                    .accessibilityIdentifier("prerender.player")
                    .accessibilityLabel(AureaText.t("prx_watch"))
            }
            Button { close() } label: {
                CupertinoGlyph.text(CupertinoGlyph.Xmark, size: 18, color: .white)
                    .frame(width: 44, height: 44)
                    .background(Circle().fill(Color.black.opacity(0.5)))
            }
            .buttonStyle(AureaPressStyle(shrink: 1))
            .padding(12)
            .accessibilityLabel(AureaText.t("prx_close"))
            .accessibilityIdentifier("prerender.player.close")
        }
        .onAppear {
            let export = model.preExport
            let frame = model.status.playhead
            let start = frame >= export.inFrame && frame < export.outFrame && export.fps > 0
                ? Double(frame - export.inFrame) / export.fps : 0
            holder.start(path: path, at: start, loop: model.preRenderRange.loop)
        }
        .onDisappear { holder.stop() }
    }

    private func close() {
        let seconds = holder.player.map { CMTimeGetSeconds($0.currentTime()) } ?? -1
        holder.stop()
        model.closePreExportPlayer(seconds: seconds.isFinite ? seconds : -1)
    }
}

@MainActor
final class PreExportPlayerHolder: ObservableObject {
    @Published private(set) var player: AVPlayer?
    private var endObserver: NSObjectProtocol?

    func start(path: String, at seconds: Double, loop: Bool) {
        guard player == nil else { return }
        let item = AVPlayerItem(url: URL(fileURLWithPath: path))
        let next = AVPlayer(playerItem: item)
        if loop {
            endObserver = NotificationCenter.default.addObserver(forName: .AVPlayerItemDidPlayToEndTime, object: item,
                                                                 queue: .main) { [weak next] _ in
                next?.seek(to: .zero)
                next?.play()
            }
        }
        if seconds > 0 { next.seek(to: CMTime(seconds: seconds, preferredTimescale: 600)) }
        player = next
        next.play()
    }

    func stop() {
        player?.pause()
        if let observer = endObserver { NotificationCenter.default.removeObserver(observer) }
        endObserver = nil
    }
}

/// O caminho do pré-export como item de apresentação (fullScreenCover).
struct PreExportPlayback: Identifiable {
    let path: String
    var id: String { path }
}
