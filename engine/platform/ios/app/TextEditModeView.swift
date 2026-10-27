// =============================================================================
//  Aurea / platform / ios / app / TextEditModeView.swift
//
//  Omega conteúdo (área 06, P0-1 / P0-3 / P1-10): o MODO DE TEXTO.
//
//  - No topo, a BARRA DE ESTILO (no lugar da barra superior):
//    [Alinhamento 38][Fonte flex ▾][Tamanho 80 ▾][Cor 38][✓ 40] (texto 3D: só
//    alinhamento, fonte e ✓). Cada peça abre um popup preso embaixo dela; só um
//    aberto por vez; tocar fora fecha (`TextStylePopupLayer`, por cima do editor).
//  - Embaixo, o PAINEL GRANDE (do transporte até o fim) com a faixa de entrada
//    de 60 pt: cada letra vai na hora para a camada (uma rajada de 1 s por passo
//    de desfazer, AureaModel.typeText). O teclado abre sozinho; se ele cobrir a
//    faixa, a faixa sobe junto.
//  Par do TextMode.kt (mesmos ids).
// =============================================================================
import SwiftUI
import UIKit

enum TextPopup { case none, font, size, color }

@MainActor final class TextModeState: ObservableObject {
    static let shared = TextModeState()
    @Published var popup: TextPopup = .none
    @Published var fontBrowser = false
    /// Texto 3D com a faixa vazia: o ✓ fica desligado (o motor guarda o último texto válido).
    @Published var draftBlank = false
    func toggle(_ p: TextPopup) { popup = popup == p ? .none : p }
}

/// Os tamanhos do popup (a régua fina continua nas Opções de texto). 0.3.6: iam só
/// até 144 px; agora seguem até o teto do motor (`max`), e o número ao lado aceita
/// qualquer valor digitado (par do TextMode.kt).
enum TextSizeSteps {
    static let values: [Int] = [5, 6, 7, 8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 36, 40, 48, 56, 64, 72, 80, 96, 112, 144,
                                192, 256, 320, 400, 512, 640, 800, 1000, 1300, 1600, 2000]
    /// O maior tamanho de texto (px): o mesmo teto do motor (`TextSetSize`, 1…2000).
    static let max: Float = 2000
    /// O tamanho digitado (aceita vírgula), preso a 1…`max`; nil se não for número.
    static func parse(_ text: String) -> Float? {
        guard let v = Float(text.trimmingCharacters(in: .whitespaces).replacingOccurrences(of: ",", with: ".")), v.isFinite else { return nil }
        return Swift.min(Swift.max(v, 1), max)
    }
    /// O passo mais perto de um tamanho (px).
    static func nearest(_ size: Float) -> Int {
        var best = 0
        for i in values.indices where abs(Float(values[i]) - size) < abs(Float(values[best]) - size) { best = i }
        return best
    }
}

/// O que a barra lê da camada do modo (texto 2D ou 3D).
@MainActor private struct TextModeInfo {
    let is3D: Bool
    let content: String
    let alignment: Int
    let size: Float
    let color: [Float]
    let font: ContentFontItem?

    init(_ model: AureaModel, fonts: [ContentFontItem]) {
        let id = model.primarySelection ?? 0
        if let recipe = model.engine.text3D(forLayer: id) {
            is3D = true
            content = recipe["content"] as? String ?? ""
            alignment = (recipe["alignment"] as? NSNumber)?.intValue ?? 0
            size = 72
            color = ((recipe["color"] as? [NSNumber]) ?? []).map(\.floatValue)
            let path = recipe["fontPath"] as? String ?? ""
            font = path.isEmpty ? nil : (fonts.first { $0.path == path } ?? ContentFontItem(family: URL(fileURLWithPath: path).deletingPathExtension().lastPathComponent, path: path))
        } else {
            let info = model.engine.text(forLayer: id) ?? [:]
            is3D = false
            content = info["content"] as? String ?? ""
            alignment = (info["alignment"] as? NSNumber)?.intValue ?? 0
            size = (info["size"] as? NSNumber)?.floatValue ?? 72
            let c = ((info["color"] as? [NSNumber]) ?? []).map(\.floatValue)
            color = c.count >= 4 ? c : [1, 1, 1, 1]
            let family = info["fontFamily"] as? String ?? ""
            if family.isEmpty { font = nil } else {
                let weight = (info["fontWeight"] as? NSNumber)?.uint32Value ?? 400
                let italic = (info["fontItalic"] as? NSNumber)?.boolValue ?? false
                font = fonts.first { $0.family == family && $0.weight == weight && $0.italic == italic }
                    ?? fonts.first { $0.family == family }
                    ?? ContentFontItem(family: family, weight: weight, italic: italic)
            }
        }
    }
}

// MARK: - A barra de estilo

/// A BARRA DE ESTILO do modo de texto (no lugar da barra superior). Os popups
/// crescem para baixo, por cima do palco (`TextStylePopupLayer`).
struct TextStyleBar: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var state = TextModeState.shared
    @ObservedObject private var library = FontLibrary.shared
    @ObservedObject private var fontRequest = FontBrowserRequest.shared

    var body: some View {
        let info = TextModeInfo(model, fonts: library.items)
        let blank = info.is3D && (state.draftBlank || info.content.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
        let fontName = info.font?.family.isEmpty == false ? info.font!.family : AureaText.t("panel_padrao_aparelho")
        HStack(spacing: 0) {
            piece(width: 38, AureaText.t(info.alignment == 1 ? "om_txm_align_center_cd" : info.alignment == 2 ? "om_txm_align_right_cd" : "om_txm_align_left_cd"),
                  "text.style.align", active: false) {
                state.popup = .none
                model.cycleTextAlignment()
            } content: { tint in
                ContentIcon(kind: info.alignment == 1 ? .alignCenter : info.alignment == 2 ? .alignRight : .alignLeft, size: 22, tint: tint)
            }
            piece(width: nil, AureaText.t("om_txm_font_cd", fontName), "text.style.font", active: state.popup == .font) {
                state.toggle(.font)
            } content: { tint in
                HStack(spacing: 4) {
                    // O nome na própria fonte, ajustado entre 8 e 16 pt numa linha.
                    Text(fontName).font(ContentFontPreview.font(info.font, size: 16) ?? .aurea(size: 16))
                        .foregroundStyle(tint).lineLimit(1).minimumScaleFactor(0.5)
                        .frame(maxWidth: .infinity, alignment: .leading)
                    CupertinoGlyph.text(state.popup == .font ? CupertinoGlyph.ChevronUp : CupertinoGlyph.ChevronDown, size: 12, color: tint)
                }
                .padding(.horizontal, 8)
            }
            if !info.is3D {
                let size = Int(info.size.rounded())
                piece(width: 80, AureaText.t("om_txm_size_cd", size), "text.style.size", active: state.popup == .size) {
                    state.toggle(.size)
                } content: { tint in
                    HStack(spacing: 4) {
                        // Só o número: o AUREA trabalha em px (sem "pt").
                        Text("\(size)").font(.aurea(size: 14, weight: .semibold)).foregroundStyle(tint).lineLimit(1)
                            .frame(maxWidth: .infinity, alignment: .leading)
                        CupertinoGlyph.text(state.popup == .size ? CupertinoGlyph.ChevronUp : CupertinoGlyph.ChevronDown, size: 12, color: tint)
                    }
                    .padding(.horizontal, 8)
                }
                piece(width: 38, AureaText.t("om_txm_color_cd"), "text.style.color", active: state.popup == .color) {
                    state.toggle(.color)
                } content: { _ in
                    ContentColorDot(rgba: info.color)
                }
            }
            piece(width: 40, AureaText.t("om_txm_done"), "text.style.done", active: false, enabled: !blank) {
                state.popup = .none
                model.closeTextEdit()
            } content: { tint in
                CupertinoGlyph.text(CupertinoGlyph.CheckmarkAlt, size: 20, color: blank ? AureaColors.railDisabled : tint)
            }
        }
        .padding(.horizontal, 1.5)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(AureaColors.contentStyleBar)
        .onAppear { library.reload(model.engine); state.popup = .none }
        .onChange(of: model.textEditLayer) { _ in state.popup = .none }
        .fullScreenCover(isPresented: $state.fontBrowser) {
            FontBrowserView(onPick: { f in
                model.applyFontAnywhere(f)
                FontPrefs.shared.recordUse(f)
                state.fontBrowser = false
            }, onDismiss: { state.fontBrowser = false })
            .environmentObject(model)
        }
    }

    /// Uma peça da barra: fundo "botão de estilo", 1,5 pt de margem.
    private func piece<Content: View>(width: CGFloat?, _ label: String, _ tag: String, active: Bool, enabled: Bool = true,
                                      action: @escaping () -> Void, @ViewBuilder content: (Color) -> Content) -> some View {
        let tint = active ? AureaColors.accent : AureaColors.text
        return Button(action: action) {
            content(tint)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(AureaColors.contentStyleButton, in: RoundedRectangle(cornerRadius: 4))
                .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .disabled(!enabled)
        .padding(1.5)
        .frame(width: width)
        .frame(maxWidth: width == nil ? .infinity : nil)
        .accessibilityLabel(label)
        .accessibilityAddTraits(active ? .isSelected : [])
        .accessibilityIdentifier(tag)
    }
}

// MARK: - Os popups da barra (por cima do editor, logo abaixo da barra)

/// O conector de 8 pt, o corpo do popup na largura toda e a camada invisível
/// que fecha ao tocar fora. `top` = altura da barra de estilo.
struct TextStylePopupLayer: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var state = TextModeState.shared
    let top: CGFloat

    var body: some View {
        if model.textModeActive && state.popup != .none {
            VStack(spacing: 0) {
                Color.clear.frame(height: top).allowsHitTesting(false)
                AureaColors.contentStyleButton.frame(height: 8)
                switch state.popup {
                case .font: FontMiniPopup()
                case .size: SizePopup()
                case .color: ColorPopup()
                case .none: EmptyView()
                }
                Color.clear
                    .contentShape(Rectangle())
                    .onTapGesture { state.popup = .none }
                    .accessibilityElement()
                    .accessibilityLabel(AureaText.t("common_close"))
                    .accessibilityAddTraits(.isButton)
                    .accessibilityIdentifier("text.popup.closer")
            }
        }
    }
}

/// Popup da fonte: o mini navegador (recentes + favoritas, 68 pt) e "Ver todas as fontes".
private struct FontMiniPopup: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var library = FontLibrary.shared
    @ObservedObject private var prefs = FontPrefs.shared
    @ObservedObject private var state = TextModeState.shared

    var body: some View {
        let current = TextModeInfo(model, fonts: library.items).font
        let list = prefs.miniList(library.items, current: current)
        let currentKey = current?.key
        VStack(alignment: .leading, spacing: 0) {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 0) {
                    ForEach(0..<list.count, id: \.self) { i in
                        let f = list[i].0
                        item(i, f, header: list[i].1, on: f == nil ? currentKey == nil : f?.key == currentKey) {
                            model.applyFontAnywhere(f)
                            if let f { FontPrefs.shared.recordUse(f) }
                        }
                    }
                }
            }
            .frame(height: 68)
            .accessibilityIdentifier("text.font.mini")
            Button {
                state.popup = .none
                state.fontBrowser = true
            } label: {
                HStack(spacing: 6) {
                    CupertinoGlyph.text(CupertinoGlyph.Textformat, size: 16)
                    Text(AureaText.t("om_txm_view_all_fonts")).font(.aurea(size: 12.5)).foregroundStyle(AureaColors.text)
                }
                .padding(.horizontal, 10).frame(height: 44).contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle(shrink: 1))
            .accessibilityIdentifier("text.font.viewAll")
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(AureaColors.contentStyleButton)
    }

    private func item(_ index: Int, _ f: ContentFontItem?, header: Int, on: Bool, action: @escaping () -> Void) -> some View {
        let name = f?.name ?? AureaText.t("panel_padrao_aparelho")
        let headerLabel: String? = header == 1 ? AureaText.t("om_txm_recent_cd") : header == 2 ? AureaText.t("om_txm_favorites_cd") : nil
        return Button(action: action) {
            HStack(spacing: 0) {
                if header != 0 {
                    CupertinoGlyph.text(header == 1 ? CupertinoGlyph.Clock : CupertinoGlyph.StarFill, size: 14, color: AureaColors.muted)
                        .frame(width: 20)
                }
                VStack(spacing: 0) {
                    // A amostra: 24 pt, ajustada até 14 pt.
                    Text(FontScripts.sample(f?.scripts ?? FontScripts.latin, long: false))
                        .font(ContentFontPreview.font(f, size: 24) ?? .aurea(size: 24))
                        .foregroundStyle(AureaColors.text)
                        .lineLimit(1).minimumScaleFactor(14.0 / 24.0)
                        .frame(height: 35)
                    Text(name).font(.aurea(size: 9)).foregroundStyle(on ? AureaColors.accent : AureaColors.text)
                        .lineLimit(2).multilineTextAlignment(.center)
                }
                .frame(maxWidth: .infinity)
            }
            .frame(width: header != 0 ? 81 : 61, height: 64)
            .background(on ? AureaColors.contentStyleBar : Color.clear, in: RoundedRectangle(cornerRadius: 5))
            .padding(2)
            .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .accessibilityLabel(headerLabel.map { "\($0) · \(name)" } ?? name)
        .accessibilityAddTraits(on ? .isSelected : [])
        .accessibilityIdentifier("text.font.mini.item.\(index)")
    }
}

/// Popup do tamanho: régua de passos; um arraste = um passo de desfazer. 0.3.6: e o
/// número, para digitar qualquer valor até `TextSizeSteps.max` (par do TextMode.kt).
private struct SizePopup: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var library = FontLibrary.shared
    @State private var dragging = false
    @State private var typed = ""
    @FocusState private var typing: Bool

    var body: some View {
        let size = TextModeInfo(model, fonts: library.items).size
        HStack(spacing: 10) {
            slider(size: size)
            TextField("", text: $typed)
                // Teclado de números com a tecla de retorno (o decimalPad não tem como confirmar).
                .keyboardType(.numbersAndPunctuation)
                .multilineTextAlignment(.center)
                .font(.aurea(size: 13).monospacedDigit())
                .foregroundStyle(AureaColors.text)
                .focused($typing)
                .submitLabel(.done)
                // Confirmar tira o foco; aplicar fica num lugar só (a perda do foco).
                .onSubmit { typing = false }
                .frame(width: 64, height: 32)
                .background(AureaColors.fxTrack, in: RoundedRectangle(cornerRadius: 6))
                .accessibilityLabel(AureaText.t("panel_tamanho_texto"))
                .accessibilityIdentifier("text.size.field")
                .onAppear { typed = "\(Int(size.rounded()))" }
                .onChange(of: Int(size.rounded())) { value in if !typing { typed = "\(value)" } }
                .onChange(of: typing) { focused in if !focused { applyTyped(size) } }
        }
        .padding(.leading, 16).padding(.trailing, 8)
        .frame(height: 44)
        .frame(maxWidth: .infinity)
        .background(AureaColors.contentStyleButton)
        .onDisappear { if dragging { dragging = false; model.endGesture() } }
    }

    /// Aplica o número digitado num passo de desfazer; sem número, volta ao atual.
    private func applyTyped(_ size: Float) {
        if let id = model.primarySelection, let value = TextSizeSteps.parse(typed), Int(value.rounded()) != Int(size.rounded()) {
            model.beginGesture("tamanho do texto")
            model.engine.setText(id, size: value); model.refreshModel(force: true)
            model.endGesture()
            typed = "\(Int(value.rounded()))"
        } else {
            typed = "\(Int(size.rounded()))"
        }
    }

    private func slider(size: Float) -> some View {
        let index = TextSizeSteps.nearest(size)
        let frac = CGFloat(index) / CGFloat(TextSizeSteps.values.count - 1)
        return GeometryReader { geo in
            let w = geo.size.width
            ZStack(alignment: .leading) {
                Capsule().fill(AureaColors.fxTrack).frame(height: 3)
                Capsule().fill(AureaColors.accent).frame(width: w * frac, height: 3)
                Circle().fill(AureaColors.accent).frame(width: 18, height: 18).offset(x: w * frac - 9)
            }
            .frame(maxHeight: .infinity)
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { g in
                    guard let id = model.primarySelection else { return }
                    if !dragging { dragging = true; model.beginGesture("tamanho do texto") }
                    let i = Int((g.location.x / max(1, w) * CGFloat(TextSizeSteps.values.count - 1)).rounded())
                    let step = TextSizeSteps.values[min(TextSizeSteps.values.count - 1, max(0, i))]
                    if Int(size.rounded()) != step { model.engine.setText(id, size: Float(step)); model.refreshModel(force: true) }
                }
                .onEnded { _ in
                    if dragging { dragging = false; model.endGesture() }
                })
        }
        .accessibilityElement()
        .accessibilityLabel(AureaText.t("om_txm_size_cd", Int(size.rounded())))
        .accessibilityAdjustableAction { direction in
            guard let id = model.primarySelection else { return }
            let next = direction == .increment ? min(TextSizeSteps.values.count - 1, index + 1) : max(0, index - 1)
            model.engine.setText(id, size: Float(TextSizeSteps.values[next])); model.refreshModel(force: true)
        }
        .accessibilityIdentifier("text.size.slider")
    }
}

/// Popup da cor: o seletor compacto, preso à direita.
private struct ColorPopup: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var library = FontLibrary.shared

    var body: some View {
        let color = TextModeInfo(model, fonts: library.items).color
        HStack(spacing: 0) {
            Spacer(minLength: 0)
            ContentColorPicker(color: color, tag: "text.color.picker", label: AureaText.t("om_txm_color_cd"),
                               onStart: { model.beginGesture("cor do texto") },
                               onChange: { c in
                                   guard let id = model.primarySelection, c.count >= 4 else { return }
                                   model.engine.setText(id, colorR: c[0], g: c[1], b: c[2], a: c[3])
                                   model.refreshModel(force: true)
                               },
                               onEnd: { model.endGesture() })
                .padding(8)
                .frame(width: 281)
                .background(AureaColors.contentStyleButton)
        }
    }
}

// MARK: - O painel grande com a faixa de entrada

/// O PAINEL GRANDE do modo de texto: fundo do painel de edição e a faixa de
/// entrada de 60 pt no topo (sobe acima do teclado se ele cobrir a faixa).
struct TextEditPanel: View {
    @EnvironmentObject private var model: AureaModel
    @State private var text = ""
    @State private var loadedLayer: Int64?
    @State private var keyboardTop: CGFloat = .infinity

    private var info: (content: String, alignment: Int, is3D: Bool) {
        let id = model.textEditLayer ?? 0
        if let recipe = model.engine.text3D(forLayer: id) {
            return (recipe["content"] as? String ?? "", (recipe["alignment"] as? NSNumber)?.intValue ?? 0, true)
        }
        let t = model.engine.text(forLayer: id) ?? [:]
        return (t["content"] as? String ?? "", (t["alignment"] as? NSNumber)?.intValue ?? 0, false)
    }

    var body: some View {
        let current = info
        GeometryReader { geo in
            let frame = geo.frame(in: .global)
            let strip: CGFloat = 60
            let covered = max(0, frame.maxY - keyboardTop)
            let top = geo.size.height - covered >= strip ? 0 : geo.size.height - covered - strip
            ZStack(alignment: .topLeading) {
                AureaColors.contentEditPanel
                VStack(alignment: .leading, spacing: 0) {
                    ZStack(alignment: .leading) {
                        AureaColors.contentInputStrip
                        if text.isEmpty {
                            Text(AureaText.t("om_txm_hint")).font(.aurea(size: 18)).foregroundStyle(AureaColors.contentHint)
                                .padding(.horizontal, 12).allowsHitTesting(false).accessibilityHidden(true)
                        }
                        TextModeInput(text: $text, alignment: current.alignment, selectAll: model.textEditSelectAll) { next in
                            TextModeState.shared.draftBlank = current.is3D && next.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
                            model.typeText(next)
                        }
                        .padding(4)
                        .id(model.textEditLayer ?? 0)
                    }
                    .frame(height: strip)
                    if current.is3D && text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                        Text(AureaText.t("text_content_3d_required")).font(.aurea(size: 12.5)).foregroundStyle(AureaColors.muted)
                            .padding(12)
                    }
                }
                .offset(y: top)
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("text.edit.mode")
        .onAppear { load() }
        .onChange(of: model.textEditLayer) { _ in load() }
        // Desfazer (ou outra mudança de fora) troca o texto: a faixa acompanha.
        .onChange(of: model.status.modelRevision) { _ in
            let content = info.content
            if content != text && !model.textTyping { text = content }
        }
        // Escolher outra camada (ou nenhuma) sai do modo.
        .onChange(of: model.primarySelection) { id in
            if model.textEditLayer != nil && model.textEditLayer != id { model.closeTextEdit() }
        }
        .onReceive(NotificationCenter.default.publisher(for: UIResponder.keyboardWillChangeFrameNotification)) { note in
            guard let end = (note.userInfo?[UIResponder.keyboardFrameEndUserInfoKey] as? NSValue)?.cgRectValue else { return }
            keyboardTop = end.minY >= UIScreen.main.bounds.height - 1 ? .infinity : end.minY
        }
        .onReceive(NotificationCenter.default.publisher(for: UIResponder.keyboardWillHideNotification)) { _ in keyboardTop = .infinity }
    }

    private func load() {
        guard loadedLayer != model.textEditLayer else { return }
        loadedLayer = model.textEditLayer
        text = info.content
        TextModeState.shared.draftBlank = false
    }
}

/// A faixa de entrada: UITextView (18 pt), foco ao aparecer e, no texto recém-criado,
/// tudo escolhido (digitar troca o "Texto" de partida).
private struct TextModeInput: UIViewRepresentable {
    @Binding var text: String
    var alignment: Int
    var selectAll: Bool
    let onChange: (String) -> Void

    private var nativeAlignment: NSTextAlignment { alignment == 1 ? .center : (alignment == 2 ? .right : .natural) }

    func makeCoordinator() -> Coordinator { Coordinator(self) }

    func makeUIView(context: Context) -> UITextView {
        let view = TextModeTextView()
        view.delegate = context.coordinator
        view.backgroundColor = .clear
        view.font = .systemFont(ofSize: 18)
        view.textColor = UIColor(AureaColors.text)
        view.tintColor = UIColor(AureaColors.accent)
        view.textContainerInset = UIEdgeInsets(top: 12, left: 8, bottom: 12, right: 8)
        view.textContainer.lineFragmentPadding = 0
        view.isScrollEnabled = true
        view.text = text
        view.textAlignment = nativeAlignment
        view.selectAllInitially = selectAll
        view.accessibilityIdentifier = "text.edit.input"
        view.accessibilityLabel = AureaText.t("om_txm_hint")
        return view
    }

    func updateUIView(_ view: UITextView, context: Context) {
        context.coordinator.parent = self
        view.textAlignment = nativeAlignment
        if view.text != text && view.markedTextRange == nil {
            view.text = text
            let end = (text as NSString).length
            view.selectedRange = NSRange(location: end, length: 0)
        }
    }

    final class Coordinator: NSObject, UITextViewDelegate {
        var parent: TextModeInput
        init(_ parent: TextModeInput) { self.parent = parent }
        func textViewDidChange(_ textView: UITextView) {
            let value = textView.text ?? ""
            parent.text = value
            parent.onChange(value)
        }
    }
}

private final class TextModeTextView: UITextView {
    var selectAllInitially = false
    private var focusedOnce = false
    override func didMoveToWindow() {
        super.didMoveToWindow()
        guard window != nil, !focusedOnce else { return }
        DispatchQueue.main.async { [weak self] in
            guard let self, self.window != nil, !self.focusedOnce else { return }
            if self.becomeFirstResponder() {
                self.focusedOnce = true
                let count = (self.text as NSString).length
                self.selectedRange = NSRange(location: self.selectAllInitially ? 0 : count, length: self.selectAllInitially ? count : 0)
            }
        }
    }
}
