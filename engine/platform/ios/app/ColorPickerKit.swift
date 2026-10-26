// =============================================================================
//  Aurea / platform / ios / app / ColorPickerKit.swift
//
//  Omega aparência (área 05) · O SELETOR DE COR da Omega. Par do seletor do
//  Android (mesmos ids, textos, medidas e comportamento):
//
//  • `EmbeddedColorPicker` (P0-5): a caixa de 127,5 com a coluna direita de 59
//    (Conta-gotas · Ajuste fino · Mais cores). Modo amostras: faixa de 26 com a
//    cor, o código e o "+"; grade de 7 colunas da paleta. Modo fino: quadrado
//    S×V (arrasto relativo), anel de matiz de 108 (giro relativo, ímã de 60°) e
//    faixa de alfa de 29 (liga/desliga pela setinha de 20). Segurar o código
//    abre o menu claro de copiar/colar (P1-7).
//  • `ColorPopover` (P0-5): o balão com seta, ancorado na amostra tocada.
//  • `ColorPickerFull` (P1-8): a tela cheia Roda · RGB · ✓ com a paleta que
//    se reordena e apaga na lixeira.
//  • `ColorPaletteStore`: a paleta ÚNICA (a "paleta salva" antiga migra para o
//    topo e as 14 cores do AUREA completam).
//  • `EyedropperStage` (P1-1): o estado do conta-gotas ao vivo que o palco
//    (PreviewMetalView.swift) mexe e desenha.
//
//  Cores: sRGB reto 0..1, RGBA (o mesmo contrato do `ColorSheetRequest`).
//  As contas de matiz, código e contraste são do motor (core/SnapMath.hpp e
//  core/ColorCode.hpp), pela ponte.
// =============================================================================
import SwiftUI
import UIKit

// MARK: - Contas e medidas

/// Medidas do seletor embutido (as da referência).
enum ColorPickerMetrics {
    static let height: CGFloat = 127.5
    static let radius: CGFloat = 5
    static let sideColumn: CGFloat = 59
    static let titleStrip: CGFloat = 26
    static let swatch: CGFloat = 27
    /// A amostra de 27 com 1,5 de cada lado: 3 entre duas.
    static let swatchCell: CGFloat = 30
    static let columns = 7
    static let hueRing: CGFloat = 108
    static let hueRingThickness: CGFloat = 12
    static let alphaStrip: CGFloat = 29
    static let alphaToggle: CGFloat = 20
    /// Arrasto relativo: Δ / (lado × 1,1).
    static let dragGain: CGFloat = 1.1
}

/// Tinta legível por cima da cor do USUÁRIO (preto ou branco pela regra do
/// motor, `colorWantsDarkText`). Não é cor de tema: depende da cor mostrada.
enum ColorPickerInk {
    static let dark = Color(hex: 0xFF000000)
    static let light = Color(hex: 0xFFFFFFFF)
}

fileprivate func cpClamp01(_ v: Float) -> Float { v.isFinite ? min(max(v, 0), 1) : 0 }
fileprivate func cpPercent(_ v: Float) -> String { "\(Int((cpClamp01(v) * 100).rounded()))%" }

/// RGBA de 4 componentes a partir de qualquer lista (falta = 0; alfa = 1).
enum PickerColor {
    static func rgba(_ c: [Float]) -> [Float] {
        (0..<4).map { i in
            let v: Float = i < c.count ? c[i] : (i == 3 ? 1 : 0)
            return v.isFinite ? min(max(v, 0), 1) : (i == 3 ? 1 : 0)
        }
    }
    static func pack(_ c: [Float]) -> UInt32 { SavedPalette.pack(rgba(c)) }
    static func unpack(_ v: UInt32) -> [Float] { SavedPalette.unpack(v) }
    static func color(_ c: [Float]) -> Color { AureaColorSpace.color(rgba(c)) }
    static func opaque(_ c: [Float]) -> [Float] { let v = rgba(c); return [v[0], v[1], v[2], 1] }
    static func hue(_ h: Float) -> Color { AureaColorSpace.color(AureaColorSpace.rgba(h: h, s: 1, v: 1, a: 1)) }
    /// As 7 paradas do arco-íris (0°…360°) do anel de matiz.
    static var hueStops: [Color] { (0...6).map { hue(Float($0) * 60) } }
}

/// Matiz, saturação, brilho e alfa do seletor. A matiz fica guardada quando a
/// cor é cinza (sem ela o anel "pularia" para o vermelho).
struct PickerHSV: Equatable {
    var h: Float
    var s: Float
    var v: Float
    var a: Float

    init(h: Float = 0, s: Float = 0, v: Float = 1, a: Float = 1) {
        self.h = h; self.s = s; self.v = v; self.a = a
    }

    init(_ rgba: [Float], keepHue: Float) {
        let x = AureaColorSpace.hsv(PickerColor.rgba(rgba))
        h = x.s > 0.0001 && x.v > 0.0001 ? x.h : keepHue
        s = x.s; v = x.v; a = x.a
    }

    func rgba(alpha: Bool) -> [Float] { AureaColorSpace.rgba(h: h, s: s, v: v, a: alpha ? a : 1) }
}

// MARK: - Paleta única

/// A paleta do seletor, POR APARELHO, compartilhada pelo seletor embutido, pelo
/// balão e pela tela cheia (o par do Android). Na primeira leitura, as cores da
/// "paleta salva" antiga (`SavedPalette`, Sheets.swift) vêm primeiro, na ordem
/// delas, e as 14 cores do AUREA que faltarem completam. "+" põe a cor NO
/// INÍCIO, sem repetir; no máximo 63 (9 fileiras de 7).
@MainActor final class ColorPaletteStore: ObservableObject {
    static let shared = ColorPaletteStore()
    static let key = "omega.colorpicker.palette"
    static let limit = 63
    /// As 14 cores próprias do AUREA (sRGB), em 2 fileiras de 7.
    static let aureaDefaults: [UInt32] = [
        0xFF3B30, 0xFF9500, 0xFFCC00, 0x34C759, 0x00C7BE, 0x007AFF, 0xAF52DE,
        0xFFFFFF, 0xC7C7CC, 0x8E8E93, 0x48484A, 0x000000, 0x5AC8FA, 0xA8E063,
    ]

    /// RGBA de 8 bits empacotado (o formato do `SavedPalette.pack`).
    @Published private(set) var colors: [UInt32]

    private init() {
        let store = UserDefaults.standard
        if let saved = store.array(forKey: Self.key) as? [NSNumber] {
            colors = Array(saved.map(\.uint32Value).prefix(Self.limit))
        } else {
            var merged: [UInt32] = []
            let defaults = Self.aureaDefaults.map { ($0 << 8) | 0xFF }
            for packed in SavedPalette.load() + defaults where !merged.contains(packed) { merged.append(packed) }
            colors = Array(merged.prefix(Self.limit))
            store.set(colors.map { NSNumber(value: $0) }, forKey: Self.key)
        }
    }

    func contains(_ packed: UInt32) -> Bool { colors.contains(packed) }

    func add(_ packed: UInt32) {
        guard !colors.contains(packed) else { return }
        save([packed] + colors)
    }

    func remove(_ packed: UInt32) { save(colors.filter { $0 != packed }) }

    /// A ordem nova inteira (reordenar na tela cheia); repetidas saem.
    func replace(_ next: [UInt32]) {
        var seen = Set<UInt32>()
        save(next.filter { seen.insert($0).inserted })
    }

    private func save(_ next: [UInt32]) {
        let bounded = Array(next.prefix(Self.limit))
        if bounded != colors { colors = bounded }
        UserDefaults.standard.set(bounded.map { NSNumber(value: $0) }, forKey: Self.key)
    }
}

// MARK: - Conta-gotas ao vivo (estado do palco)

/// O conta-gotas ao vivo (P1-1). O seletor liga com `model.eyedropper`; o palco
/// (PreviewMetalView) manda aqui o arrasto e desenha a mira e a lupa. A mira
/// é RELATIVA: anda o quanto o dedo anda, em qualquer lugar do palco, e começa
/// no centro da composição ou onde parou da última vez. Cada arrasto captura UM
/// quadro da prévia no começo e amostra dele (alfa = o do seletor).
@MainActor final class EyedropperStage: ObservableObject {
    static let shared = EyedropperStage()
    /// Mira em px da composição (nil = nunca usada: começa no centro).
    @Published private(set) var crosshair: CGPoint?
    @Published private(set) var dragging = false
    /// O quadro capturado no começo do arrasto (RGBA8 sRGB, alfa reto).
    @Published private(set) var shot: PreviewShot?
    /// A última cor amostrada (RGBA; alfa 1).
    @Published private(set) var color: [Float]?
    private var composition = CGSize(width: 1, height: 1)
    private var capture: Task<Void, Never>?
    private var generation = 0
    private var commitPending = false

    /// O dedo pousou no palco com o conta-gotas ligado.
    func begin(_ model: AureaModel, composition size: CGSize) {
        composition = CGSize(width: max(1, size.width), height: max(1, size.height))
        crosshair = clamp(crosshair ?? CGPoint(x: composition.width / 2, y: composition.height / 2))
        dragging = true
        shot = nil
        color = nil
        commitPending = false
        generation &+= 1
        let ticket = generation
        capture?.cancel()
        capture = Task { @MainActor [weak self, weak model] in
            guard let model else { return }
            let frame = await model.capturePreviewFrame(720)
            guard let self, !Task.isCancelled, self.generation == ticket, let frame,
                  let image = UIImage.fromRGBA(frame.data, width: Int(frame.width), height: Int(frame.height)) else { return }
            self.shot = PreviewShot(image: image, rgba: frame.data, width: Int(frame.width), height: Int(frame.height))
            guard let request = model.eyedropper, let sample = self.sampleHere() else { return }
            self.color = sample
            if self.commitPending {
                // O dedo já saiu antes de o quadro chegar: aplica agora.
                self.commitPending = false
                request.onCommit(sample)
            } else if self.dragging {
                request.onSample(sample)
            }
        }
    }

    /// O dedo andou `dx`, `dy` px da composição.
    func move(_ dx: CGFloat, _ dy: CGFloat, model: AureaModel) {
        guard dragging, let point = crosshair else { return }
        crosshair = clamp(CGPoint(x: point.x + dx, y: point.y + dy))
        guard let sample = sampleHere() else { return }
        color = sample
        model.eyedropper?.onSample(sample)
    }

    /// O dedo saiu: aplica a cor sob a mira (um passo de desfazer, no seletor).
    func end(_ model: AureaModel) {
        guard dragging else { return }
        dragging = false
        if let sample = sampleHere() {
            color = sample
            model.eyedropper?.onCommit(sample)
        } else if model.eyedropper != nil {
            commitPending = true
        }
    }

    /// O toque foi cancelado pelo sistema: fecha o passo aberto no seletor.
    func cancel(_ model: AureaModel) {
        capture?.cancel()
        dragging = false
        commitPending = false
        model.eyedropper?.onCancel()
    }

    private func clamp(_ p: CGPoint) -> CGPoint {
        CGPoint(x: min(max(p.x, 0), composition.width - 1), y: min(max(p.y, 0), composition.height - 1))
    }

    private func sampleHere() -> [Float]? {
        guard let shot, let p = crosshair, shot.width > 0, shot.height > 0 else { return nil }
        let x = Int((p.x / composition.width * CGFloat(shot.width)).rounded(.down))
        let y = Int((p.y / composition.height * CGFloat(shot.height)).rounded(.down))
        let rgb = shot.color(x, y)
        guard rgb.count >= 3 else { return nil }
        return [rgb[0], rgb[1], rgb[2], 1]
    }
}

/// Um passo de desfazer do conta-gotas: abre no primeiro `onSample` e fecha no
/// `onCommit` (ou no `onCancel`).
private final class EyedropperUndo {
    var open = false
}

// MARK: - Seletor embutido (P0-5)

/// O seletor de cor embutido da Omega: 127,5 de altura, coluna direita de 59.
///
/// - `color`: a cor mostrada (RGBA sRGB 0..1); muda por fora → o seletor acompanha.
/// - `onBegin`/`onEnd`: um passo de desfazer por gesto (arrasto, toque numa
///   amostra, colar, conta-gotas). No balão o passo é de quem abriu (no-op aqui).
/// - `onChange`: cada cor nova (só quando muda).
/// - `onFull`: "Mais cores". Nil = abre a tela cheia (`model.colorSheet` sem âncora).
struct EmbeddedColorPicker: View {
    let color: [Float]
    let withAlpha: Bool
    let onBegin: () -> Void
    let onChange: ([Float]) -> Void
    let onEnd: () -> Void
    let onFull: (() -> Void)?

    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var palette = ColorPaletteStore.shared
    @AppStorage("omega.colorpicker.alpha") private var alphaStrip = false
    @State private var hsv: PickerHSV
    @State private var shown: [Float]
    @State private var emitted: UInt32
    @State private var fine = false
    /// Centro do anel durante um arrasto: "H°", "S% / V%" ou o alfa.
    @State private var readout: String?
    @State private var codeMenu = false
    @State private var clipboard: [Float]?
    @State private var dropperId: UUID?
    @State private var svStart: PickerHSV?
    @State private var ring: HueRingDrag?
    @State private var alphaStart: Float?

    /// Giro relativo do anel: matiz acumulada (sem ímã), último ângulo do dedo,
    /// se está no ímã (háptico ao entrar) e se o toque caiu no miolo (ignorado).
    private struct HueRingDrag {
        var raw: Float
        var last: Float
        var snapped: Bool
        var ignored: Bool
    }

    init(color: [Float], withAlpha: Bool = true, onBegin: @escaping () -> Void = {},
         onChange: @escaping ([Float]) -> Void, onEnd: @escaping () -> Void = {}, onFull: (() -> Void)? = nil) {
        var c = PickerColor.rgba(color)
        if !withAlpha { c[3] = 1 }
        self.color = c
        self.withAlpha = withAlpha
        self.onBegin = onBegin
        self.onChange = onChange
        self.onEnd = onEnd
        self.onFull = onFull
        _hsv = State(initialValue: PickerHSV(c, keepHue: 0))
        _shown = State(initialValue: c)
        _emitted = State(initialValue: PickerColor.pack(c))
    }

    var body: some View {
        HStack(spacing: 1) {
            Group {
                if fine { fineArea } else { swatchArea }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            sideColumn
        }
        .frame(height: ColorPickerMetrics.height)
        .background(AureaColors.panelFrameDivider)
        .clipShape(RoundedRectangle(cornerRadius: ColorPickerMetrics.radius))
        .onChange(of: PickerColor.pack(color)) { next in
            if next != emitted { adopt(color) }
        }
        .onDisappear { stopEyedropper() }
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("colorpicker")
    }

    // MARK: Estado

    private var codeText: String {
        model.engine.formatColorCode(shown[0], g: shown[1], b: shown[2], a: shown[3], withAlpha: withAlpha)
    }

    private var ink: Color {
        model.engine.colorWantsDarkText(shown[0], g: shown[1], b: shown[2], a: shown[3]) ? ColorPickerInk.dark : ColorPickerInk.light
    }

    /// A cor mudou por fora (desfazer, outro painel, keyframe): acompanha sem emitir.
    private func adopt(_ c: [Float]) {
        var v = PickerColor.rgba(c)
        if !withAlpha { v[3] = 1 }
        hsv = PickerHSV(v, keepHue: hsv.h)
        shown = v
        emitted = PickerColor.pack(v)
    }

    /// Cor nova a partir de H/S/V/A (arrastos).
    private func push(_ next: PickerHSV) {
        hsv = next
        let c = next.rgba(alpha: withAlpha)
        shown = c
        let packed = PickerColor.pack(c)
        guard packed != emitted else { return }
        emitted = packed
        onChange(c)
    }

    /// A cor inteira (amostra, colar, conta-gotas).
    private func set(_ c: [Float]) {
        var v = PickerColor.rgba(c)
        if !withAlpha { v[3] = 1 }
        hsv = PickerHSV(v, keepHue: hsv.h)
        shown = v
        let packed = PickerColor.pack(v)
        guard packed != emitted else { return }
        emitted = packed
        onChange(v)
    }

    /// Uma cor inteira num passo de desfazer.
    private func apply(_ c: [Float]) {
        onBegin()
        set(c)
        onEnd()
    }

    // MARK: Coluna direita

    private var showsEyedropper: Bool { model.screen == .editor }

    private var sideColumn: some View {
        VStack(spacing: 1) {
            if showsEyedropper {
                sideButton(CupertinoGlyph.Eyedropper, key: "om_ap_cp_eyedropper", tag: "colorpicker.eyedropper",
                           on: dropperActive, action: toggleEyedropper)
            }
            sideButton(CupertinoGlyph.SliderHorizontal3, key: "om_ap_cp_fine", tag: "colorpicker.fine", on: fine) {
                fine.toggle()
                readout = nil
            }
            sideButton(CupertinoGlyph.ColorFilter, key: "om_ap_cp_full", tag: "colorpicker.full", on: false, action: openFull)
        }
        .frame(width: ColorPickerMetrics.sideColumn)
    }

    private func sideButton(_ glyph: Character, key: String, tag: String, on: Bool,
                            action: @escaping () -> Void) -> some View {
        Button(action: action) {
            CupertinoGlyph.text(glyph, size: 20, color: on ? AureaColors.accent : AureaColors.text)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(on ? AureaColors.fxListSurface : AureaColors.fxLowSurface)
                .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .accessibilityLabel(AureaText.t(key))
        .accessibilityAddTraits(on ? .isSelected : [])
        .accessibilityIdentifier(tag)
    }

    private func openFull() {
        stopEyedropper()
        if let onFull { onFull(); return }
        // Tela cheia: um passo de desfazer até fechar (o ✓ sempre aplica).
        let change = onChange, end = onEnd
        onBegin()
        model.colorSheet = ColorSheetRequest(title: AureaText.t("om_ap_cp_full"), initial: shown, withAlpha: withAlpha,
                                             anchor: nil, onChange: { r, g, b, a in change([r, g, b, a]) }, onDone: end)
    }

    // MARK: Conta-gotas (P1-1)

    private var dropperActive: Bool { dropperId != nil && model.eyedropper?.id == dropperId }

    private func toggleEyedropper() {
        if dropperActive { stopEyedropper(); return }
        let undo = EyedropperUndo()
        let begin = onBegin, end = onEnd
        let picker = self
        let request = EyedropperRequest(
            onSample: { rgba in
                if !undo.open { undo.open = true; begin() }
                picker.set(picker.withPickerAlpha(rgba))
            },
            onCommit: { rgba in
                if !undo.open { undo.open = true; begin() }
                picker.set(picker.withPickerAlpha(rgba))
                undo.open = false
                end()
            },
            onCancel: {
                if undo.open { undo.open = false; end() }
            })
        // Outro conta-gotas ligado (outro seletor): fecha o passo dele.
        model.eyedropper?.onCancel()
        model.eyedropper = request
        dropperId = request.id
    }

    /// A amostra do palco com o alfa ATUAL do seletor.
    private func withPickerAlpha(_ rgba: [Float]) -> [Float] {
        let c = PickerColor.rgba(rgba)
        return [c[0], c[1], c[2], withAlpha ? hsv.a : 1]
    }

    private func stopEyedropper() {
        guard dropperActive, let request = model.eyedropper else { dropperId = nil; return }
        model.eyedropper = nil
        dropperId = nil
        request.onCancel()
    }

    // MARK: Modo amostras

    private var swatchArea: some View {
        VStack(spacing: 0) {
            titleStrip
            swatchGrid
        }
    }

    /// Faixa de 26: o fundo é a cor (xadrez sob alfa), o código no centro e o
    /// "+" que salva na paleta (só quando a cor ainda não está lá).
    private var titleStrip: some View {
        let code = codeText
        let inkColor = ink
        return ZStack {
            AureaColorSwatch(color: PickerColor.color(shown))
            Text(code)
                .font(.aurea(size: 13, weight: .semibold).monospacedDigit())
                .foregroundStyle(inkColor)
                .lineLimit(1)
                .padding(.horizontal, 10)
                .frame(maxHeight: .infinity)
                .contentShape(Rectangle())
                .onLongPressGesture(minimumDuration: 0.45) { openCodeMenu() }
                .aureaAnchoredMenu(isPresented: $codeMenu, tone: .light, tag: "colorpicker.code.menu") { codeEntries }
                .accessibilityElement(children: .ignore)
                .accessibilityLabel(AureaText.t("om_ap_cp_code", code))
                .accessibilityAddTraits(.isButton)
                .accessibilityAction(.default) { openCodeMenu() }
                .accessibilityAction(named: AureaText.t("om_ap_cp_copy_code")) { copyCode() }
                .accessibilityIdentifier("colorpicker.hex")
            HStack(spacing: 0) {
                Spacer(minLength: 0)
                if !palette.contains(PickerColor.pack(shown)) {
                    Button { palette.add(PickerColor.pack(shown)) } label: {
                        CupertinoGlyph.text(CupertinoGlyph.Plus, size: 16, color: inkColor)
                            .frame(width: 44, height: ColorPickerMetrics.titleStrip)
                            .contentShape(Rectangle())
                    }
                    .buttonStyle(AureaPressStyle(shrink: 1))
                    .accessibilityLabel(AureaText.t("om_ap_cp_add"))
                    .accessibilityIdentifier("colorpicker.add")
                }
            }
        }
        .frame(height: ColorPickerMetrics.titleStrip)
    }

    private var swatchGrid: some View {
        let columns = Array(repeating: GridItem(.fixed(ColorPickerMetrics.swatchCell), spacing: 0),
                            count: ColorPickerMetrics.columns)
        return ScrollView(.vertical, showsIndicators: false) {
            LazyVGrid(columns: columns, spacing: 0) {
                ForEach(Array(palette.colors.enumerated()), id: \.offset) { index, packed in
                    swatchCell(index, packed)
                }
            }
            .padding(.vertical, 1.5)
            .frame(maxWidth: .infinity)
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel(AureaText.t("om_ap_cp_palette"))
    }

    private func swatchCell(_ index: Int, _ packed: UInt32) -> some View {
        let c = PickerColor.unpack(packed)
        let blocked = !withAlpha && c[3] < 1
        let current = packed == PickerColor.pack(shown)
        let dark = model.engine.colorWantsDarkText(c[0], g: c[1], b: c[2], a: c[3])
        let hex = model.engine.formatColorCode(c[0], g: c[1], b: c[2], a: c[3], withAlpha: true)
        return Button { pick(c, blocked: blocked) } label: {
            AureaColorSwatch(color: PickerColor.color(c))
                .frame(width: ColorPickerMetrics.swatch, height: ColorPickerMetrics.swatch)
                .clipShape(RoundedRectangle(cornerRadius: ColorPickerMetrics.radius))
                .overlay {
                    if blocked {
                        CupertinoGlyph.text(CupertinoGlyph.XmarkCircle, size: 16,
                                            color: dark ? ColorPickerInk.dark : ColorPickerInk.light)
                    }
                }
                .overlay(RoundedRectangle(cornerRadius: ColorPickerMetrics.radius)
                    .stroke(current ? AureaColors.text : Color.clear, lineWidth: 1.5))
                .frame(width: ColorPickerMetrics.swatchCell, height: ColorPickerMetrics.swatchCell)
                .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .accessibilityLabel(AureaText.t("om_ap_cp_swatch", hex))
        .accessibilityAddTraits(current ? .isSelected : [])
        .accessibilityIdentifier("colorpicker.swatch.\(index)")
    }

    /// Amostra com alfa num campo sem alfa: não aplica e avisa.
    private func pick(_ c: [Float], blocked: Bool) {
        if blocked { model.toast = AureaText.t("om_ap_cp_unavailable"); return }
        apply(c)
    }

    // MARK: Código da cor (P1-7)

    private var codeEntries: [MenuEntry] {
        [
            .item(AureaText.t("om_ap_cp_copy_code"), tag: "colorpicker.code.copy") { copyCode() },
            .item(AureaText.t("om_ap_cp_paste_code"), swatch: clipboard.map { PickerColor.color($0) },
                  enabled: clipboard != nil, tag: "colorpicker.code.paste") {
                if let c = clipboard { apply(c) }
            },
        ]
    }

    /// Lê a área de transferência UMA vez (ao segurar), para mostrar a amostra
    /// e habilitar o "Colar" só quando o texto é um código de cor.
    private func openCodeMenu() {
        clipboard = nil
        if UIPasteboard.general.hasStrings, let text = UIPasteboard.general.string {
            let parsed = model.engine.parseColorCode(text).map(\.floatValue)
            if parsed.count >= 4 { clipboard = parsed }
        }
        codeMenu = true
    }

    private func copyCode() {
        UIPasteboard.general.string = codeText
        model.toast = AureaText.t("om_ap_cp_code_copied")
    }

    // MARK: Modo fino

    private var fineArea: some View {
        HStack(spacing: 6) {
            svSquare
            hueRing
                .frame(width: ColorPickerMetrics.hueRing, height: ColorPickerMetrics.hueRing)
            if withAlpha {
                if alphaStrip { alphaBar.frame(width: ColorPickerMetrics.alphaStrip) }
                alphaToggle
            }
        }
        .padding(.vertical, 6)
        .padding(.leading, 6)
        .padding(.trailing, withAlpha ? 0 : 6)
        // O quadrado, o anel e a faixa usam o eixo X da tela: LTR em qualquer idioma.
        .keepLtr()
    }

    /// Quadrado Saturação × Brilho, arrasto RELATIVO (Δ / (lado × 1,1)).
    private var svSquare: some View {
        GeometryReader { geo in
            let w = max(1, geo.size.width), h = max(1, geo.size.height)
            ZStack(alignment: .topLeading) {
                LinearGradient(colors: [ColorPickerInk.light, PickerColor.hue(hsv.h)], startPoint: .leading, endPoint: .trailing)
                LinearGradient(colors: [Color.clear, ColorPickerInk.dark], startPoint: .top, endPoint: .bottom)
                Circle()
                    .stroke(ColorPickerInk.light, lineWidth: 2)
                    .background(Circle().stroke(ColorPickerInk.dark.opacity(0.35), lineWidth: 4))
                    .frame(width: 14, height: 14)
                    .position(x: CGFloat(hsv.s) * w, y: CGFloat(1 - hsv.v) * h)
            }
            .clipShape(RoundedRectangle(cornerRadius: 4))
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { value in
                    let start: PickerHSV
                    if let held = svStart { start = held } else { start = hsv; svStart = hsv; onBegin() }
                    var next = hsv
                    next.s = cpClamp01(start.s + Float(value.translation.width / (w * ColorPickerMetrics.dragGain)))
                    next.v = cpClamp01(start.v - Float(value.translation.height / (h * ColorPickerMetrics.dragGain)))
                    readout = cpPercent(next.s) + " / " + cpPercent(next.v)
                    push(next)
                }
                .onEnded { _ in
                    guard svStart != nil else { return }
                    svStart = nil
                    readout = nil
                    onEnd()
                })
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(AureaText.t("om_ap_cp_saturation") + " / " + AureaText.t("om_ap_cp_brightness"))
        .accessibilityValue(cpPercent(hsv.s) + " / " + cpPercent(hsv.v))
        .accessibilityAdjustableAction { direction in
            var next = hsv
            switch direction {
            case .increment: next.v = cpClamp01(next.v + 0.05)
            case .decrement: next.v = cpClamp01(next.v - 0.05)
            @unknown default: return
            }
            onBegin(); push(next); onEnd()
        }
        .accessibilityIdentifier("colorpicker.sv")
    }

    /// Anel de matiz de 108: giro RELATIVO, miolo de ¼ do raio ignorado, ímã a
    /// cada 60° (motor `hueSnap`, zona de 15°) com háptico ao entrar; o toque a
    /// mais de 45° da matiz atual salta para o múltiplo de 60° (`hueTouchStart`).
    private var hueRing: some View {
        GeometryReader { geo in
            let size = min(geo.size.width, geo.size.height)
            let center = CGPoint(x: geo.size.width / 2, y: geo.size.height / 2)
            let thickness = ColorPickerMetrics.hueRingThickness
            let radius = size / 2 - thickness / 2
            let angle = Double(hsv.h - 90) * .pi / 180
            ZStack {
                Circle()
                    .strokeBorder(AngularGradient(colors: PickerColor.hueStops, center: .center,
                                                  startAngle: .degrees(-90), endAngle: .degrees(270)),
                                  lineWidth: thickness)
                    .frame(width: size, height: size)
                Circle()
                    .fill(PickerColor.hue(hsv.h))
                    .overlay(Circle().stroke(ColorPickerInk.light, lineWidth: 2))
                    .frame(width: thickness + 4, height: thickness + 4)
                    .position(x: center.x + CGFloat(cos(angle)) * radius, y: center.y + CGFloat(sin(angle)) * radius)
                Text(readout ?? codeText)
                    .font(.aurea(size: 11, weight: .medium).monospacedDigit())
                    .foregroundStyle(AureaColors.text)
                    .lineLimit(1)
                    .minimumScaleFactor(0.7)
                    .frame(width: max(10, size - 2 * thickness - 10))
                    .position(center)
            }
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { value in ringChanged(value, center: center, radius: size / 2) }
                .onEnded { _ in ringEnded() })
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(AureaText.t("om_ap_cp_hue"))
        .accessibilityValue("\(Int(hsv.h.rounded()) % 360)°")
        .accessibilityAdjustableAction { direction in
            var next = hsv
            switch direction {
            case .increment: next.h = (next.h + 5).truncatingRemainder(dividingBy: 360)
            case .decrement: next.h = (next.h + 355).truncatingRemainder(dividingBy: 360)
            @unknown default: return
            }
            onBegin(); push(next); onEnd()
        }
        .accessibilityIdentifier("colorpicker.hue")
    }

    /// Ângulo do dedo como matiz: 0° no topo, sentido horário (o desenho do anel).
    private func touchHue(_ p: CGPoint, _ c: CGPoint) -> Float {
        var a = Float(atan2(p.y - c.y, p.x - c.x) * 180 / .pi) + 90
        if a < 0 { a += 360 }
        if a >= 360 { a -= 360 }
        return a
    }

    private func ringChanged(_ value: DragGesture.Value, center: CGPoint, radius: CGFloat) {
        // Estado local: o @State escrito aqui só é relido no próximo evento.
        var drag: HueRingDrag
        if let held = ring {
            drag = held
        } else {
            let distance = hypot(value.startLocation.x - center.x, value.startLocation.y - center.y)
            guard distance >= radius / 4 else {
                ring = HueRingDrag(raw: hsv.h, last: 0, snapped: false, ignored: true)
                return
            }
            let touch = touchHue(value.startLocation, center)
            drag = HueRingDrag(raw: model.engine.hueTouchStart(hsv.h, touch: touch), last: touch, snapped: false, ignored: false)
            onBegin()
        }
        guard !drag.ignored else { return }
        let angle = touchHue(value.location, center)
        var delta = angle - drag.last
        if delta > 180 { delta -= 360 } else if delta < -180 { delta += 360 }
        drag.raw += delta
        drag.last = angle
        let snap = model.engine.hueSnap(drag.raw).map(\.floatValue)
        let shownHue = snap.first ?? drag.raw
        let stuck = snap.count > 1 && snap[1] > 0.5
        if stuck && !drag.snapped { UISelectionFeedbackGenerator().selectionChanged() }
        drag.snapped = stuck
        ring = drag
        moveHue(shownHue)
    }

    private func moveHue(_ h: Float) {
        var next = hsv
        next.h = h
        readout = "\(Int(h.rounded()) % 360)°"
        push(next)
    }

    private func ringEnded() {
        guard let drag = ring else { return }
        ring = nil
        readout = nil
        if !drag.ignored { onEnd() }
    }

    /// Faixa de alfa de 29: arrasto vertical RELATIVO (opaco em cima).
    private var alphaBar: some View {
        GeometryReader { geo in
            let h = max(1, geo.size.height)
            let opaque = PickerColor.color(PickerColor.opaque(shown))
            ZStack(alignment: .top) {
                AureaColorSwatch(color: Color.clear)
                LinearGradient(colors: [opaque, opaque.opacity(0)], startPoint: .top, endPoint: .bottom)
                Rectangle()
                    .fill(ColorPickerInk.light)
                    .frame(height: 3)
                    .overlay(Rectangle().stroke(ColorPickerInk.dark.opacity(0.35), lineWidth: 1))
                    .offset(y: CGFloat(1 - hsv.a) * (h - 3))
            }
            .clipShape(RoundedRectangle(cornerRadius: 4))
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { value in
                    let start: Float
                    if let held = alphaStart { start = held } else { start = hsv.a; alphaStart = hsv.a; onBegin() }
                    var next = hsv
                    next.a = cpClamp01(start - Float(value.translation.height / (h * ColorPickerMetrics.dragGain)))
                    readout = cpPercent(next.a)
                    push(next)
                }
                .onEnded { _ in
                    guard alphaStart != nil else { return }
                    alphaStart = nil
                    readout = nil
                    onEnd()
                })
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(AureaText.t("om_ap_cp_alpha"))
        .accessibilityValue(cpPercent(hsv.a))
        .accessibilityAdjustableAction { direction in
            var next = hsv
            switch direction {
            case .increment: next.a = cpClamp01(next.a + 0.05)
            case .decrement: next.a = cpClamp01(next.a - 0.05)
            @unknown default: return
            }
            onBegin(); push(next); onEnd()
        }
        .accessibilityIdentifier("colorpicker.alpha")
    }

    /// Setinha de 20 que mostra/esconde a faixa de alfa (a escolha fica salva).
    private var alphaToggle: some View {
        Button { alphaStrip.toggle() } label: {
            CupertinoGlyph.text(alphaStrip ? CupertinoGlyph.ChevronRight : CupertinoGlyph.ChevronLeft, size: 14,
                                color: AureaColors.muted)
                .frame(width: ColorPickerMetrics.alphaToggle)
                .frame(maxHeight: .infinity)
                .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .accessibilityLabel(AureaText.t("om_ap_cp_alpha_toggle"))
        .accessibilityValue(AureaText.t(alphaStrip ? "common_on" : "common_off"))
        .accessibilityIdentifier("colorpicker.alpha.toggle")
    }
}

// MARK: - Balão (P0-5)

/// Onde o balão abre (conta pura; mesmos números do Android): 290 de largura
/// (presa à janela − 16), seta de 9 × 18; ABAIXO da amostra quando cabe
/// (`anchor.maxY + 9 + 127,5 + 25`), senão acima; a seta a 30 da borda do lado
/// da metade da tela em que a amostra está.
enum ColorPopoverLayout {
    static let width: CGFloat = 290
    static let arrowHeight: CGFloat = 9
    static let arrowWidth: CGFloat = 18
    static let padding: CGFloat = 6
    static let margin: CGFloat = 8
    static let arrowInset: CGFloat = 30
    static let radius: CGFloat = 8

    struct Place: Equatable {
        var x: CGFloat
        var y: CGFloat
        var width: CGFloat
        var height: CGFloat
        var below: Bool
        /// Centro da seta, a partir da borda esquerda do balão.
        var arrowX: CGFloat
    }

    static func place(anchor: CGRect, window: CGSize) -> Place {
        let width = min(Self.width, max(120, window.width - 16))
        let height = ColorPickerMetrics.height + padding * 2 + arrowHeight
        let below = anchor.maxY + arrowHeight + ColorPickerMetrics.height + 25 <= window.height
        let leftHalf = anchor.midX < window.width / 2
        var x = leftHalf ? anchor.midX - arrowInset : anchor.midX + arrowInset - width
        x = min(max(x, margin), max(margin, window.width - width - margin))
        var y = below ? anchor.maxY : anchor.minY - height
        y = min(max(y, margin), max(margin, window.height - height - margin))
        let half = arrowWidth / 2 + radius
        let arrowX = min(max(anchor.midX - x, half), width - half)
        return Place(x: x, y: y, width: width, height: height, below: below, arrowX: arrowX)
    }
}

private struct PopoverArrow: Shape {
    let up: Bool
    func path(in r: CGRect) -> Path {
        var p = Path()
        if up {
            p.move(to: CGPoint(x: r.minX, y: r.maxY))
            p.addLine(to: CGPoint(x: r.midX, y: r.minY))
            p.addLine(to: CGPoint(x: r.maxX, y: r.maxY))
        } else {
            p.move(to: CGPoint(x: r.minX, y: r.minY))
            p.addLine(to: CGPoint(x: r.midX, y: r.maxY))
            p.addLine(to: CGPoint(x: r.maxX, y: r.minY))
        }
        p.closeSubpath()
        return p
    }
}

/// O balão do seletor (P0-5): ancorado no retângulo GLOBAL da amostra
/// (`ColorSheetRequest.anchor`), sem animação, toque fora fecha. Desenhado
/// pela raiz (ContentView), por cima da moldura de 200. Com o conta-gotas dele
/// ligado, o toque fora PASSA para o palco. Fecha também quando a camada
/// principal ou o painel mudam.
struct ColorPopover: View {
    let request: ColorSheetRequest
    let onDismiss: () -> Void
    @EnvironmentObject private var model: AureaModel
    @Environment(\.layoutDirection) private var direction
    @State private var current: [Float]

    init(request: ColorSheetRequest, onDismiss: @escaping () -> Void) {
        self.request = request
        self.onDismiss = onDismiss
        _current = State(initialValue: PickerColor.rgba(request.initial))
    }

    var body: some View {
        GeometryReader { geo in
            let frame = geo.frame(in: .global)
            let anchor = (request.anchor ?? .zero).offsetBy(dx: -frame.minX, dy: -frame.minY)
            let place = ColorPopoverLayout.place(anchor: anchor, window: geo.size)
            ZStack(alignment: .topLeading) {
                Color.clear
                    .contentShape(Rectangle())
                    .onTapGesture(perform: onDismiss)
                    .allowsHitTesting(model.eyedropper == nil)
                    .accessibilityHidden(true)
                bubble(place)
                    .frame(width: place.width, height: place.height)
                    .offset(x: place.x, y: place.y)
            }
            // Coordenadas da tela: sempre LTR; o seletor segue o idioma.
            .environment(\.layoutDirection, .leftToRight)
        }
        .ignoresSafeArea()
        .onChange(of: model.primarySelection) { _ in onDismiss() }
        .onChange(of: model.panel) { _ in onDismiss() }
    }

    private func bubble(_ place: ColorPopoverLayout.Place) -> some View {
        VStack(spacing: 0) {
            if place.below { arrow(up: true, x: place.arrowX) }
            EmbeddedColorPicker(color: current, withAlpha: request.withAlpha,
                                onChange: { c in
                                    current = c
                                    request.onChange(c[0], c[1], c[2], c[3])
                                },
                                onFull: openFull)
                .environment(\.layoutDirection, direction)
                .padding(ColorPopoverLayout.padding)
                .background(AureaColors.panelFrameBackground,
                            in: RoundedRectangle(cornerRadius: ColorPopoverLayout.radius))
            if !place.below { arrow(up: false, x: place.arrowX) }
        }
        .compositingGroup()
        .shadow(color: AureaColors.lightShadow, radius: 8, x: 0, y: 3)
        .accessibilityElement(children: .contain)
        .accessibilityLabel(request.title)
        .accessibilityAddTraits(.isModal)
        .accessibilityAction(.escape, onDismiss)
        .accessibilityIdentifier("colorpicker.popover")
    }

    private func arrow(up: Bool, x: CGFloat) -> some View {
        PopoverArrow(up: up)
            .fill(AureaColors.panelFrameBackground)
            .frame(width: ColorPopoverLayout.arrowWidth, height: ColorPopoverLayout.arrowHeight)
            .padding(.leading, max(0, x - ColorPopoverLayout.arrowWidth / 2))
            .frame(maxWidth: .infinity, alignment: .leading)
            .accessibilityHidden(true)
    }

    /// "Mais cores" no balão: a MESMA cor segue na tela cheia (os retornos de
    /// quem abriu continuam valendo; o `onDone` sai quando a tela cheia fecha).
    private func openFull() {
        let r = request
        model.colorSheet = ColorSheetRequest(title: r.title, initial: current, withAlpha: r.withAlpha, anchor: nil,
                                             onChange: r.onChange, onDone: r.onDone)
    }
}

// MARK: - Tela cheia (P1-8)

private struct FullPaletteCellKey: PreferenceKey {
    static var defaultValue: [UInt32: CGRect] = [:]
    static func reduce(value: inout [UInt32: CGRect], nextValue: () -> [UInt32: CGRect]) {
        value.merge(nextValue()) { $1 }
    }
}

private struct FullPaletteTrashKey: PreferenceKey {
    static var defaultValue: CGRect = .zero
    static func reduce(value: inout CGRect, nextValue: () -> CGRect) {
        let next = nextValue()
        if next != .zero { value = next }
    }
}

/// Régua da tela cheia: trilha com o degradê do canal, polegar com a cor.
private struct FullPickerSlider: View {
    let value: Float
    let colors: [Color]
    let checker: Bool
    let thumb: Color
    let onValue: (Float) -> Void

    var body: some View {
        GeometryReader { geo in
            let w = max(1, geo.size.width - 24)
            ZStack(alignment: .leading) {
                ZStack {
                    if checker { AureaColorSwatch(color: Color.clear) }
                    LinearGradient(colors: colors, startPoint: .leading, endPoint: .trailing)
                }
                .frame(height: 10)
                .clipShape(Capsule())
                .padding(.horizontal, 12)
                Circle()
                    .fill(thumb)
                    .overlay(Circle().stroke(AureaColors.onImage, lineWidth: 2.5))
                    .shadow(color: AureaColors.lightShadow, radius: 2, x: 0, y: 1)
                    .frame(width: 24, height: 24)
                    .offset(x: CGFloat(cpClamp01(value)) * w)
            }
            .frame(width: geo.size.width, height: geo.size.height)
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0).onChanged { g in
                onValue(cpClamp01(Float((g.location.x - 12) / w)))
            })
        }
        .keepLtr()
    }
}

/// O seletor em tela cheia (P1-8): topo de 45 com **Roda · RGB · ✓** (a aba
/// fica lembrada), a página escolhida e a paleta de 112 no rodapé. Mostrado
/// pela raiz (ContentView) quando o pedido NÃO tem âncora. Emite ao vivo só
/// quando a cor muda; fechar (✓ ou escape) sempre aplica. Tocar na metade
/// "atual" do cartão volta à cor de partida.
struct ColorPickerFull: View {
    let request: ColorSheetRequest
    let onDismiss: () -> Void
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var palette = ColorPaletteStore.shared
    @AppStorage("omega.colorpicker.tab") private var tab = "wheel"
    private let initial: [Float]
    @State private var hsv: PickerHSV
    @State private var shown: [Float]
    @State private var emitted: UInt32
    @State private var wheelActive: Bool?
    // Paleta: segurar levanta, arrastar reordena ao vivo, lixeira apaga.
    @State private var dragged: UInt32?
    @State private var working: [UInt32]?
    @State private var dragPoint: CGPoint = .zero
    @State private var lifted = false
    @State private var cells: [UInt32: CGRect] = [:]
    @State private var trashFrame: CGRect = .zero
    @State private var overTrash = false
    @State private var tapBlockedUntil = Date.distantPast
    @State private var notice: String?

    private static let space = "aurea.colorpicker.full"
    private var withAlpha: Bool { request.withAlpha }

    init(request: ColorSheetRequest, onDismiss: @escaping () -> Void) {
        self.request = request
        self.onDismiss = onDismiss
        var c = PickerColor.rgba(request.initial)
        if !request.withAlpha { c[3] = 1 }
        initial = c
        _hsv = State(initialValue: PickerHSV(c, keepHue: 0))
        _shown = State(initialValue: c)
        _emitted = State(initialValue: PickerColor.pack(c))
    }

    var body: some View {
        VStack(spacing: 0) {
            topBar
            ZStack(alignment: .bottom) {
                Group {
                    if tab == "rgb" { rgbPage } else { wheelPage }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                if dragged != nil { trash }
                if let notice, dragged == nil {
                    Text(notice)
                        .font(.aurea(size: 13))
                        .foregroundStyle(AureaColors.text)
                        .padding(.horizontal, 16).padding(.vertical, 10)
                        .background(AureaColors.surfaceHigh, in: RoundedRectangle(cornerRadius: 12))
                        .padding(.bottom, 10)
                        .allowsHitTesting(false)
                }
            }
            paletteArea
        }
        .foregroundStyle(AureaColors.text)
        .background(AureaColors.panelFrameDivider.ignoresSafeArea())
        .coordinateSpace(name: Self.space)
        .overlay { liftedSwatch }
        .onPreferenceChange(FullPaletteCellKey.self) { cells = $0 }
        .onPreferenceChange(FullPaletteTrashKey.self) { trashFrame = $0 }
        .accessibilityElement(children: .contain)
        .accessibilityLabel(request.title)
        .accessibilityAddTraits(.isModal)
        .accessibilityAction(.escape) { close() }
        .accessibilityIdentifier("colorpicker.full.screen")
    }

    // MARK: Cor

    private var code: String {
        model.engine.formatColorCode(shown[0], g: shown[1], b: shown[2], a: shown[3], withAlpha: withAlpha)
    }

    private func push(_ next: PickerHSV) {
        hsv = next
        let c = next.rgba(alpha: withAlpha)
        shown = c
        emit(c)
    }

    private func setAll(_ c: [Float]) {
        var v = PickerColor.rgba(c)
        if !withAlpha { v[3] = 1 }
        hsv = PickerHSV(v, keepHue: hsv.h)
        shown = v
        emit(v)
    }

    private func emit(_ c: [Float]) {
        let packed = PickerColor.pack(c)
        guard packed != emitted else { return }
        emitted = packed
        request.onChange(c[0], c[1], c[2], c[3])
    }

    /// ✓ ou escape: sempre aplica (a final de novo, se mudou desde o começo).
    private func close() {
        if PickerColor.pack(shown) != PickerColor.pack(initial) {
            request.onChange(shown[0], shown[1], shown[2], shown[3])
        }
        onDismiss()
    }

    private func flash(_ text: String) {
        notice = text
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.8) { if notice == text { notice = nil } }
    }

    // MARK: Topo

    private var topBar: some View {
        GeometryReader { geo in
            let unit = geo.size.width / 2.3
            HStack(spacing: 0) {
                tabButton("wheel", key: "om_ap_cp_wheel", tag: "colorpicker.full.wheel").frame(width: unit)
                tabButton("rgb", key: "om_ap_cp_rgb", tag: "colorpicker.full.rgb").frame(width: unit)
                Button(action: close) {
                    CupertinoGlyph.text(CupertinoGlyph.CheckmarkAlt, size: 22, color: AureaColors.accent)
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                        .background(AureaColors.panelFrameBackground)
                        .contentShape(Rectangle())
                }
                .buttonStyle(AureaPressStyle(shrink: 1))
                .frame(width: max(44, geo.size.width - 2 * unit))
                .accessibilityLabel(AureaText.t("om_ap_cp_done"))
                .accessibilityIdentifier("colorpicker.full.done")
            }
        }
        .frame(height: 45)
    }

    private func tabButton(_ id: String, key: String, tag: String) -> some View {
        let on = (tab == "rgb" ? "rgb" : "wheel") == id
        return Button { tab = id } label: {
            Text(AureaText.t(key))
                .font(.aurea(size: 15, weight: .semibold))
                .foregroundStyle(on ? AureaColors.accent : AureaColors.text)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .overlay(alignment: .bottom) {
                    Rectangle().fill(on ? AureaColors.accent : Color.clear).frame(height: 2)
                }
                .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .accessibilityAddTraits(on ? .isSelected : [])
        .accessibilityIdentifier(tag)
    }

    // MARK: Cartão atual | nova

    private func compareCard(width: CGFloat, height: CGFloat) -> some View {
        let packed = PickerColor.pack(shown)
        return HStack(spacing: 0) {
            Button { setAll(initial) } label: {
                AureaColorSwatch(color: PickerColor.color(initial))
            }
            .buttonStyle(.plain)
            .accessibilityLabel(AureaText.t("om_ap_cp_current"))
            .accessibilityIdentifier("colorpicker.full.current")
            AureaColorSwatch(color: PickerColor.color(shown))
                .accessibilityElement(children: .ignore)
                .accessibilityLabel(AureaText.t("om_ap_cp_new"))
                .accessibilityValue(code)
                .accessibilityIdentifier("colorpicker.full.new")
        }
        .frame(width: width, height: height)
        .clipShape(RoundedRectangle(cornerRadius: 7.5))
        .overlay(alignment: .topTrailing) {
            if !palette.contains(packed) {
                Button { palette.add(packed) } label: {
                    CupertinoGlyph.text(CupertinoGlyph.Plus, size: 13, color: AureaColors.text)
                        .frame(width: 24, height: 24)
                        .background(AureaColors.fxListSurface, in: Circle())
                        .frame(width: 44, height: 44)
                        .contentShape(Rectangle())
                }
                .buttonStyle(AureaPressStyle(shrink: 1))
                .offset(x: 22, y: -22)
                .accessibilityLabel(AureaText.t("om_ap_cp_add"))
                .accessibilityIdentifier("colorpicker.full.add")
            }
        }
    }

    // MARK: Roda

    private var wheelPage: some View {
        VStack(spacing: 0) {
            GeometryReader { geo in
                let d = max(80, min(geo.size.width - 30, geo.size.height - 20))
                wheel(diameter: d)
                    .frame(width: d, height: d)
                    .position(x: geo.size.width / 2, y: geo.size.height / 2)
            }
            VStack(spacing: 0) {
                hsvRuler(.saturation)
                hsvRuler(.brightness)
                if withAlpha { hsvRuler(.alpha) }
            }
            .frame(height: 132, alignment: .top)
        }
    }

    private func wheel(diameter d: CGFloat) -> some View {
        let thickness: CGFloat = 22
        let radius = d / 2 - thickness / 2
        let angle = Double(hsv.h - 90) * .pi / 180
        let center = CGPoint(x: d / 2, y: d / 2)
        return ZStack {
            Circle()
                .strokeBorder(AngularGradient(colors: PickerColor.hueStops, center: .center,
                                              startAngle: .degrees(-90), endAngle: .degrees(270)),
                              lineWidth: thickness)
            // Marcas a cada 30°.
            Canvas { context, size in
                let c = CGPoint(x: size.width / 2, y: size.height / 2)
                let outer = min(size.width, size.height) / 2 - thickness - 4
                var ticks = Path()
                for k in 0..<12 {
                    let a = (Double(k) * 30 - 90) * .pi / 180
                    let u = CGPoint(x: CGFloat(cos(a)), y: CGFloat(sin(a)))
                    ticks.move(to: CGPoint(x: c.x + u.x * outer, y: c.y + u.y * outer))
                    ticks.addLine(to: CGPoint(x: c.x + u.x * (outer - 7), y: c.y + u.y * (outer - 7)))
                }
                context.stroke(ticks, with: .color(AureaColors.muted), lineWidth: 1.5)
            }
            .allowsHitTesting(false)
            // Alça de 25 com o miolo de 17,5 na matiz (é ela o elemento ajustável da matiz).
            Circle()
                .fill(AureaColors.onImage)
                .frame(width: 25, height: 25)
                .overlay(Circle().fill(PickerColor.hue(hsv.h)).frame(width: 17.5, height: 17.5))
                .shadow(color: AureaColors.lightShadow, radius: 2, x: 0, y: 1)
                .position(x: center.x + CGFloat(cos(angle)) * radius, y: center.y + CGFloat(sin(angle)) * radius)
                .allowsHitTesting(false)
                .accessibilityElement(children: .ignore)
                .accessibilityLabel(AureaText.t("om_ap_cp_hue"))
                .accessibilityValue("\(Int(hsv.h.rounded()) % 360)°")
                .accessibilityAdjustableAction { direction in
                    var next = hsv
                    switch direction {
                    case .increment: next.h = (next.h + 5).truncatingRemainder(dividingBy: 360)
                    case .decrement: next.h = (next.h + 355).truncatingRemainder(dividingBy: 360)
                    @unknown default: return
                    }
                    push(next)
                }
                .accessibilityIdentifier("colorpicker.full.hue")
            VStack(spacing: 8) {
                compareCard(width: 70, height: 50)
                Text(code).font(.aurea(size: 15).monospacedDigit()).foregroundStyle(AureaColors.text)
            }
        }
        .contentShape(Circle())
        .gesture(DragGesture(minimumDistance: 0)
            .onChanged { value in
                let active: Bool
                if let held = wheelActive {
                    active = held
                } else {
                    let distance = hypot(value.startLocation.x - center.x, value.startLocation.y - center.y)
                    active = distance >= d / 2 - thickness - 12
                    wheelActive = active
                }
                guard active else { return }
                var a = Float(atan2(value.location.y - center.y, value.location.x - center.x) * 180 / .pi) + 90
                if a < 0 { a += 360 }
                if a >= 360 { a -= 360 }
                var next = hsv
                next.h = a
                push(next)
            }
            .onEnded { _ in wheelActive = nil })
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("colorpicker.full.wheelRing")
    }

    private enum HSVChannel { case saturation, brightness, alpha }

    private func hsvRuler(_ channel: HSVChannel) -> some View {
        let value: Float
        let colors: [Color]
        let glyph: Character
        let key: String
        let tag: String
        switch channel {
        case .saturation:
            value = hsv.s
            colors = [PickerColor.color(AureaColorSpace.rgba(h: hsv.h, s: 0, v: hsv.v, a: 1)),
                      PickerColor.color(AureaColorSpace.rgba(h: hsv.h, s: 1, v: hsv.v, a: 1))]
            glyph = CupertinoGlyph.DropFill; key = "om_ap_cp_saturation"; tag = "colorpicker.full.saturation"
        case .brightness:
            value = hsv.v
            colors = [ColorPickerInk.dark, PickerColor.color(AureaColorSpace.rgba(h: hsv.h, s: hsv.s, v: 1, a: 1))]
            glyph = CupertinoGlyph.Lightbulb; key = "om_ap_cp_brightness"; tag = "colorpicker.full.brightness"
        case .alpha:
            value = hsv.a
            let opaque = PickerColor.color(PickerColor.opaque(shown))
            colors = [opaque.opacity(0), opaque]
            glyph = CupertinoGlyph.Grid; key = "om_ap_cp_alpha"; tag = "colorpicker.full.alpha"
        }
        let set: (Float) -> Void = { x in
            var next = hsv
            switch channel {
            case .saturation: next.s = x
            case .brightness: next.v = x
            case .alpha: next.a = x
            }
            push(next)
        }
        return ruler(value: value, colors: colors, checker: channel == .alpha,
                     icon: CupertinoGlyph.text(glyph, size: 18, color: AureaColors.muted),
                     valueText: cpPercent(value), step: 0.01, label: AureaText.t(key), tag: tag, set: set)
    }

    // MARK: RGB

    private var rgbPage: some View {
        VStack(spacing: 0) {
            Spacer(minLength: 12)
            compareCard(width: 140, height: 70)
            Text(rgbText)
                .font(.aurea(size: 15).monospacedDigit())
                .foregroundStyle(AureaColors.text)
                .padding(.top, 12)
                .accessibilityIdentifier("colorpicker.full.code")
            Spacer(minLength: 12)
            VStack(spacing: 0) {
                rgbRuler(0, letter: "R", key: "om_ap_cp_red", tag: "colorpicker.full.red")
                rgbRuler(1, letter: "G", key: "om_ap_cp_green", tag: "colorpicker.full.green")
                rgbRuler(2, letter: "B", key: "om_ap_cp_blue", tag: "colorpicker.full.blue")
                if withAlpha { rgbRuler(3, letter: "A", key: "om_ap_cp_alpha", tag: "colorpicker.full.alphaByte") }
            }
            .frame(height: 176, alignment: .top)
        }
    }

    /// `#RRGGBB (A%)`.
    private var rgbText: String {
        let hex = model.engine.formatColorCode(shown[0], g: shown[1], b: shown[2], a: 1, withAlpha: false)
        return withAlpha ? hex + " (" + cpPercent(shown[3]) + ")" : hex
    }

    private func rgbRuler(_ index: Int, letter: String, key: String, tag: String) -> some View {
        let value = shown[index]
        let colors: [Color]
        if index == 3 {
            let opaque = PickerColor.color(PickerColor.opaque(shown))
            colors = [opaque.opacity(0), opaque]
        } else {
            var low = PickerColor.opaque(shown), high = low
            low[index] = 0
            high[index] = 1
            colors = [PickerColor.color(low), PickerColor.color(high)]
        }
        let set: (Float) -> Void = { x in
            var c = shown
            c[index] = (x * 255).rounded() / 255
            setAll(c)
        }
        return ruler(value: value, colors: colors, checker: index == 3,
                     icon: Text(letter).font(.aurea(size: 14, weight: .semibold)).foregroundStyle(AureaColors.muted),
                     valueText: "\(Int((cpClamp01(value) * 255).rounded()))", step: 1.0 / 255,
                     label: AureaText.t(key), tag: tag, set: set)
    }

    /// Régua de 44: ícone de 50 à esquerda, trilha, valor de 40 à direita.
    private func ruler<Icon: View>(value: Float, colors: [Color], checker: Bool, icon: Icon, valueText: String,
                                   step: Float, label: String, tag: String,
                                   set: @escaping (Float) -> Void) -> some View {
        let thumb = PickerColor.color(shown)
        return HStack(spacing: 0) {
            icon.frame(width: 50)
            FullPickerSlider(value: value, colors: colors, checker: checker, thumb: thumb, onValue: set)
                .frame(maxWidth: .infinity)
            Text(valueText)
                .font(.aurea(size: 13).monospacedDigit())
                .foregroundStyle(AureaColors.text)
                .lineLimit(1)
                .minimumScaleFactor(0.8)
                .frame(width: 40)
        }
        .frame(height: 44)
        .padding(.trailing, 6)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(label)
        .accessibilityValue(valueText)
        .accessibilityAdjustableAction { direction in
            switch direction {
            case .increment: set(cpClamp01(value + step))
            case .decrement: set(cpClamp01(value - step))
            @unknown default: break
            }
        }
        .accessibilityIdentifier(tag)
    }

    // MARK: Paleta (112)

    private var items: [UInt32] { working ?? palette.colors }

    private var paletteArea: some View {
        let columns = Array(repeating: GridItem(.fixed(30), spacing: 0), count: ColorPickerMetrics.columns)
        return ScrollView(.vertical, showsIndicators: false) {
            LazyVGrid(columns: columns, spacing: 0) {
                ForEach(items, id: \.self) { packed in
                    cell(packed)
                }
            }
            .padding(.vertical, 6)
            .frame(maxWidth: .infinity)
        }
        .scrollDisabled(dragged != nil)
        .frame(height: 112)
        .background(AureaColors.fxDarkest)
        .accessibilityElement(children: .contain)
        .accessibilityLabel(AureaText.t("om_ap_cp_palette"))
        .accessibilityIdentifier("colorpicker.full.palette")
    }

    private func cell(_ packed: UInt32) -> some View {
        let c = PickerColor.unpack(packed)
        let blocked = !withAlpha && c[3] < 1
        let dark = model.engine.colorWantsDarkText(c[0], g: c[1], b: c[2], a: c[3])
        let hex = model.engine.formatColorCode(c[0], g: c[1], b: c[2], a: c[3], withAlpha: true)
        let index = items.firstIndex(of: packed) ?? 0
        return AureaColorSwatch(color: PickerColor.color(c))
            .frame(width: 26, height: 26)
            .clipShape(RoundedRectangle(cornerRadius: 5))
            .overlay {
                if blocked {
                    CupertinoGlyph.text(CupertinoGlyph.XmarkCircle, size: 15,
                                        color: dark ? ColorPickerInk.dark : ColorPickerInk.light)
                }
            }
            .overlay(RoundedRectangle(cornerRadius: 5)
                .stroke(packed == PickerColor.pack(shown) ? AureaColors.text : Color.clear, lineWidth: 1.5))
            .frame(width: 30, height: 30)
            .opacity(dragged == packed ? 0.15 : 1)
            .contentShape(Rectangle())
            .background(GeometryReader { g in
                Color.clear.preference(key: FullPaletteCellKey.self, value: [packed: g.frame(in: .named(Self.space))])
            })
            .onTapGesture {
                guard dragged == nil, Date() >= tapBlockedUntil else { return }
                pick(c, blocked: blocked)
            }
            .simultaneousGesture(LongPressGesture(minimumDuration: 0.4)
                .sequenced(before: DragGesture(minimumDistance: 0, coordinateSpace: .named(Self.space)))
                .onChanged { value in dragChanged(value, packed: packed) }
                .onEnded { _ in dragEnded() })
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(AureaText.t("om_ap_cp_swatch", hex))
            .accessibilityAddTraits(.isButton)
            .accessibilityAction(.default) { pick(c, blocked: blocked) }
            .accessibilityAction(named: AureaText.t("panel_apagar")) { palette.remove(packed) }
            .accessibilityIdentifier("colorpicker.full.swatch.\(index)")
    }

    private func pick(_ c: [Float], blocked: Bool) {
        if blocked { flash(AureaText.t("om_ap_cp_unavailable")); return }
        setAll(c)
    }

    private func dragChanged(_ value: SequenceGesture<LongPressGesture, DragGesture>.Value, packed: UInt32) {
        switch value {
        case .first(true):
            _ = lift(packed)
        case .second(true, let drag?):
            let current = dragged == nil ? lift(packed) : (working ?? palette.colors)
            dragPoint = drag.location
            let trashHit = trashFrame != .zero && trashFrame.contains(drag.location)
            overTrash = trashHit
            var list = current
            guard !trashHit, let from = list.firstIndex(of: packed),
                  let target = cells.first(where: { $0.key != packed && $0.value.contains(drag.location) })?.key,
                  let to = list.firstIndex(of: target) else { return }
            // Arrastar sobre outra amostra reordena AO VIVO (grava ao soltar).
            list.remove(at: from)
            list.insert(packed, at: min(to, list.count))
            withAnimation(.easeInOut(duration: 0.15)) { working = list }
        default:
            break
        }
    }

    /// Segurar levanta a amostra: escala 2,0 e alfa 0,8 em 300 ms; a lixeira aparece.
    /// Devolve a lista de trabalho (a ordem ao vivo).
    private func lift(_ packed: UInt32) -> [UInt32] {
        guard dragged == nil else { return working ?? palette.colors }
        let list = palette.colors
        dragged = packed
        working = list
        if let frame = cells[packed] { dragPoint = CGPoint(x: frame.midX, y: frame.midY) }
        lifted = false
        withAnimation(.easeOut(duration: 0.3)) { lifted = true }
        UIImpactFeedbackGenerator(style: .medium).impactOccurred()
        return list
    }

    private func dragEnded() {
        guard let packed = dragged else { return }
        if overTrash {
            palette.remove(packed)
        } else if let working {
            palette.replace(working)
        }
        dragged = nil
        working = nil
        lifted = false
        overTrash = false
        tapBlockedUntil = Date().addingTimeInterval(0.35)
    }

    @ViewBuilder private var liftedSwatch: some View {
        if let packed = dragged {
            AureaColorSwatch(color: PickerColor.color(PickerColor.unpack(packed)))
                .frame(width: 26, height: 26)
                .clipShape(RoundedRectangle(cornerRadius: 5))
                .scaleEffect(lifted ? 2 : 1)
                .opacity(lifted ? 0.8 : 1)
                .position(dragPoint)
                .allowsHitTesting(false)
                .accessibilityHidden(true)
        }
    }

    /// A lixeira acima da paleta enquanto uma amostra está levantada.
    private var trash: some View {
        HStack(spacing: 8) {
            CupertinoGlyph.text(CupertinoGlyph.Trash, size: 20, color: overTrash ? AureaColors.danger : AureaColors.text)
            Text(AureaText.t("om_ap_cp_trash"))
                .font(.aurea(size: 13))
                .foregroundStyle(overTrash ? AureaColors.danger : AureaColors.text)
        }
        .frame(maxWidth: .infinity)
        .frame(height: 56)
        .background(overTrash ? AureaColors.danger.opacity(0.18) : AureaColors.fxLowSurface)
        .background(GeometryReader { g in
            Color.clear.preference(key: FullPaletteTrashKey.self, value: g.frame(in: .named(Self.space)))
        })
        .accessibilityElement(children: .combine)
        .accessibilityIdentifier("colorpicker.full.trash")
    }
}
