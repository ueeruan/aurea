// =============================================================================
//  Aurea / platform / ios / app / Sheets.swift
//
//  Os primitivos de folha e diálogo da UI aprovada (`ui/ds/Sheets.kt`).
//
//  O app antigo usava a folha modal do Material 3 (fundo `#151C24`, raio 18) e,
//  para confirmações e escolhas curtas, os diálogos Cupertino do SDK —
//  reproduzidos aqui com as medidas do SDK (largura 270, raio 14, ação 17 em
//  `#6FAED9`).
//
//  COMO SE USA NO iOS: `AureaModalSheet` é o CONTEÚDO de um `.sheet` (quem
//  apresenta é a tela); `AureaActionSheet`, `AureaAlert` e `AureaNamePrompt` já
//  trazem o véu e se apresentam sozinhos num `ZStack`, como o `Dialog` do
//  Android.
// =============================================================================
import SwiftUI

// =============================================================================
// Folha modal (o corpo que o `.sheet` apresenta)
// =============================================================================

/// A FOLHA MODAL: puxador de 36 × 5 e o corpo sobre `#151C24`. Mesmo conteúdo,
/// mesma ordem de seções e mesmos rótulos da folha do Android — quem abre nos
/// dois aparelhos vê a mesma folha.
struct AureaModalSheet<Content: View>: View {
    private let content: Content

    init(@ViewBuilder content: () -> Content) {
        self.content = content()
    }

    var body: some View {
        VStack(spacing: 0) {
            Capsule()
                .fill(AureaColors.sheetGrab)
                .frame(width: AureaDims.sheetGrabW, height: AureaDims.sheetGrabMenu)
                .padding(.top, 10)
                .padding(.bottom, 6)
            content
        }
        .frame(maxWidth: .infinity)
        .background(AureaColors.surface)
    }
}

// =============================================================================
// Action sheet (Cupertino: margem 8, raio 14, "Cancelar" separado)
// =============================================================================

/// Uma ação da folha. `destructive` pinta em vermelho; `enabled` falso apaga sem
/// sumir (diz que a ação existe).
struct SheetAction {
    let label: String
    let destructive: Bool
    let enabled: Bool
    let onClick: () -> Void

    init(_ label: String, destructive: Bool = false, enabled: Bool = true,
         onClick: @escaping () -> Void) {
        self.label = label
        self.destructive = destructive
        self.enabled = enabled
        self.onClick = onClick
    }
}

/// Action sheet no estilo Cupertino. `items` é (rótulo, ação) na ORDEM do
/// Android — a mão aprende a posição, não o texto.
struct AureaActionSheet: View {
    private let title: String
    private let items: [SheetAction]
    private let onDismiss: () -> Void

    init(title: String, items: [(String, () -> Void)], onDismiss: @escaping () -> Void) {
        self.title = title
        self.items = items.map { SheetAction($0.0, onClick: $0.1) }
        self.onDismiss = onDismiss
    }
    init(title: String, actions: [SheetAction], onDismiss: @escaping () -> Void) {
        self.title = title; self.items = actions; self.onDismiss = onDismiss
    }

    var body: some View {
        ZStack(alignment: .bottom) {
            AureaColors.menuScrim
                .ignoresSafeArea()
                .onTapGesture { onDismiss() }
            VStack(spacing: AureaDims.s2) {
                VStack(spacing: 0) {
                    if !title.isEmpty {
                        Text(title)
                            .font(.aurea(size: 13, weight: .semibold))
                            .foregroundStyle(AureaColors.actionSheetText)
                            .multilineTextAlignment(.center)
                            .padding(.horizontal, AureaDims.dialogPadH)
                            .padding(.vertical, 13.5)
                        divider
                    }
                    ForEach(Array(items.enumerated()), id: \.offset) { index, item in
                        if index > 0 { divider }
                        Text(item.label)
                            .font(.aurea(size: 17))
                            .foregroundStyle(!item.enabled ? AureaColors.disabled : item.destructive ? AureaColors.danger : AureaColors.accent)
                            .frame(maxWidth: .infinity)
                            .frame(minHeight: AureaDims.actionRowMinH)
                            .contentShape(Rectangle())
                            .aureaTappable(shrink: 1, enabled: item.enabled) {
                                onDismiss()
                                item.onClick()
                            }
                    }
                }
                .background(AureaColors.actionSheetBg,
                            in: RoundedRectangle(cornerRadius: AureaDims.actionSheetRadius))
                Text(AureaText.t("common_cancel"))
                    .font(.aurea(size: 17, weight: .semibold))
                    .foregroundStyle(AureaColors.accent)
                    .frame(maxWidth: .infinity)
                    .frame(minHeight: AureaDims.actionRowMinH)
                    .background(AureaColors.actionCancelBg,
                                in: RoundedRectangle(cornerRadius: AureaDims.actionSheetRadius))
                    .contentShape(Rectangle())
                    .aureaTappable(shrink: 1, action: onDismiss)
            }
            .padding(AureaDims.actionSheetMargin)
        }
    }

    private var divider: some View {
        Rectangle()
            .fill(AureaColors.actionDivider)
            .frame(height: AureaDims.dialogDivider)
    }
}

// =============================================================================
// Alerta e pedido de nome (Cupertino: 270, raio 14)
// =============================================================================

/// Diálogo de confirmação no estilo Cupertino (270 dp, raio 14).
///
/// `cancelLabel` nulo = o rótulo do catálogo, no idioma do app. Esconder o botão
/// de cancelar é `showCancel` = false, não um rótulo vazio: com sete idiomas, um
/// `nil` que significa duas coisas vira bug de tradução.
struct AureaAlert: View {
    private let title: String
    private let message: String?
    private let confirmLabel: String
    private let cancelLabel: String?
    private let showCancel: Bool
    private let destructive: Bool
    private let onConfirm: () -> Void
    private let onDismiss: () -> Void
    private let extra: AnyView?

    init(title: String,
         message: String? = nil,
         confirmLabel: String = "OK",
         cancelLabel: String? = nil,
         showCancel: Bool = true,
         destructive: Bool = false,
         onConfirm: @escaping () -> Void,
         onDismiss: @escaping () -> Void,
         extra: AnyView? = nil) {
        self.title = title
        self.message = message
        self.confirmLabel = confirmLabel
        self.cancelLabel = cancelLabel
        self.showCancel = showCancel
        self.destructive = destructive
        self.onConfirm = onConfirm
        self.onDismiss = onDismiss
        self.extra = extra
    }

    var body: some View {
        ZStack {
            AureaColors.scrim
                .ignoresSafeArea()
                .onTapGesture { onDismiss() }
            VStack(spacing: 0) {
                VStack(spacing: AureaDims.s1) {
                    Text(title)
                        .font(.aurea(size: 17, weight: .semibold))
                        .foregroundStyle(AureaColors.text)
                        .multilineTextAlignment(.center)
                    if let message {
                        Text(message)
                            .font(.aurea(size: 13))
                            .foregroundStyle(AureaColors.text)
                            .multilineTextAlignment(.center)
                    }
                    if let extra { extra }
                }
                .padding(.horizontal, AureaDims.dialogPadH)
                .padding(.top, AureaDims.dialogPadTop)
                .padding(.bottom, AureaDims.dialogPadBottom)
                .frame(maxWidth: .infinity)
                divider
                HStack(spacing: 0) {
                    if showCancel {
                        dialogButton(cancelLabel ?? AureaText.t("common_cancel"),
                                     bold: false, color: AureaColors.accent) { onDismiss() }
                        Rectangle()
                            .fill(AureaColors.actionDivider)
                            .frame(width: AureaDims.dialogDivider, height: AureaDims.dialogRowH)
                    }
                    dialogButton(confirmLabel, bold: true,
                                 color: destructive ? AureaColors.destructiveCupertino : AureaColors.accent) {
                        onDismiss()
                        onConfirm()
                    }
                }
                .frame(minHeight: AureaDims.dialogRowH)
            }
            .frame(width: AureaDims.dialogW)
            .background(AureaColors.dialogBg,
                        in: RoundedRectangle(cornerRadius: AureaDims.dialogRadius))
        }
    }

    private var divider: some View {
        Rectangle()
            .fill(AureaColors.actionDivider)
            .frame(height: AureaDims.dialogDivider)
    }

    private func dialogButton(_ label: String, bold: Bool, color: Color,
                              onClick: @escaping () -> Void) -> some View {
        Text(label)
            .font(.aurea(size: 16.8, weight: bold ? .semibold : .regular))
            .foregroundStyle(color)
            .frame(maxWidth: .infinity)
            .frame(minHeight: AureaDims.dialogRowH)
            .contentShape(Rectangle())
            .aureaTappable(shrink: 1, action: onClick)
    }
}

/// Pede um nome (renomear camada/projeto). Vazio não confirma.
struct AureaNamePrompt: View {
    private let title: String
    private let initial: String
    private let onConfirm: (String) -> Void
    private let onDismiss: () -> Void

    @State private var text: String = ""
    @FocusState private var focused: Bool

    init(title: String, initial: String = "", onConfirm: @escaping (String) -> Void,
         onDismiss: @escaping () -> Void) {
        self.title = title
        self.initial = initial
        self.onConfirm = onConfirm
        self.onDismiss = onDismiss
    }

    init(title: String, onConfirm: @escaping (String) -> Void,
         onDismiss: @escaping () -> Void) {
        self.init(title: title, initial: "", onConfirm: onConfirm, onDismiss: onDismiss)
    }

    private var trimmed: String { text.trimmingCharacters(in: .whitespacesAndNewlines) }

    var body: some View {
        AureaAlert(title: title, confirmLabel: "OK",
                   onConfirm: { if !trimmed.isEmpty { onConfirm(trimmed) } },
                   onDismiss: onDismiss,
                   extra: AnyView(field))
            .onAppear {
                if text.isEmpty { text = initial }
                focused = true
            }
    }

    private var field: some View {
        TextField("", text: $text)
            .font(.aurea(size: 15))
            .foregroundStyle(AureaColors.text)
            .tint(AureaColors.accent)
            .submitLabel(.done)
            .focused($focused)
            .onSubmit { if !trimmed.isEmpty { onDismiss(); onConfirm(trimmed) } }
            .padding(.horizontal, AureaDims.s2)
            .padding(.vertical, 7)
            .background(AureaColors.fieldDialog,
                        in: RoundedRectangle(cornerRadius: AureaDims.nameFieldRadius))
            .padding(.top, 10)
    }
}

// NumericKeypad.kt: same arithmetic, display, key order and clamp semantics.
struct KeypadRequest: Identifiable {
    let id = UUID()
    let title: String
    let value: Float
    let unit: String
    let min: Float
    let max: Float
    let decimals: Int
    let onValue: (Float) -> Void
}

enum KeypadExpression {
    static func isOperator(_ c: Character) -> Bool { "+−-×÷".contains(c) }
    static func evaluate(_ text: String, percentOf: Double) -> Double? {
        let input = Array(text.trimmingCharacters(in: .whitespacesAndNewlines))
        guard !input.isEmpty else { return nil }
        var numbers: [Double] = [], ops: [Character] = []
        var i = 0, expectNumber = true
        var sign = 1.0
        while i < input.count {
            let c = input[i]
            if expectNumber {
                if c == "−" || c == "-" { sign = -sign; i += 1; continue }
                if c == "+" { i += 1; continue }
                let start = i
                while i < input.count && (input[i].isNumber || ",.:".contains(input[i])) { i += 1 }
                guard i > start else { return nil }
                let segments = String(input[start..<i]).split(separator: ":", omittingEmptySubsequences: false)
                var value = 0.0
                for segment in segments {
                    let raw = String(segment)
                    let normalized = raw.contains(",") ? raw.replacingOccurrences(of: ".", with: "").replacingOccurrences(of: ",", with: ".") : raw
                    guard let part = Double(normalized), part.isFinite else { return nil }
                    value = value * 60 + part
                }
                if i < input.count && input[i] == "%" { value = value / 100 * percentOf; i += 1 }
                numbers.append(sign * value); sign = 1; expectNumber = false
            } else {
                guard isOperator(c) else { return nil }
                ops.append(c == "-" ? "−" : c); i += 1; expectNumber = true
            }
        }
        guard !expectNumber else { return nil }
        var reduced = [numbers[0]], additions: [Character] = []
        for index in ops.indices {
            let next = numbers[index + 1]
            switch ops[index] {
            case "×": reduced[reduced.count - 1] *= next
            case "÷": guard next != 0 else { return nil }; reduced[reduced.count - 1] /= next
            default: additions.append(ops[index]); reduced.append(next)
            }
        }
        var result = reduced[0]
        for index in additions.indices { result += (additions[index] == "+" ? 1 : -1) * reduced[index + 1] }
        return result.isFinite ? result : nil
    }
}

struct NumericKeypadSheet: View {
    let request: KeypadRequest
    let onDismiss: () -> Void
    @State private var text = ""
    @State private var selectedAll = true
    private let rows = [["7", "8", "9", "⌫"], ["4", "5", "6", "÷"], ["1", "2", "3", "×"], [",", "0", "±", "−"], [":", "%", "=", "+"]]
    private var result: Double? { KeypadExpression.evaluate(text, percentOf: request.unit == "%" ? 100 : (request.max.isFinite ? Double(request.max) : 100)) }
    private var value: Float? {
        guard let result, result.isFinite else { return nil }
        let clamped = Float(result).clamped(to: (request.min.isNaN ? -.infinity : request.min)...(request.max.isNaN ? .infinity : request.max))
        return clamped.isFinite ? clamped : nil
    }
    private func display(_ value: Float) -> String { numeroPtBr(value, casas: request.decimals).replacingOccurrences(of: "-", with: "−") }
    private var hint: String {
        guard let result, let value else { return text.isEmpty ? "" : AureaText.t("ds_conta_incompleta") }
        let formatted = comUnidade(numeroPtBr(value, casas: request.decimals), request.unit)
        if Double(value) != result { return "Fica em " + formatted }
        return !selectedAll && (text.dropFirst().contains(where: KeypadExpression.isOperator) || text.contains("%") || text.contains(":")) ? "= " + formatted : ""
    }
    var body: some View {
        ZStack(alignment: .bottom) {
            AureaColors.sheetScrim.ignoresSafeArea().onTapGesture(perform: onDismiss)
            VStack(alignment: .leading, spacing: 0) {
                Text(request.title).font(.aurea(size: 15, weight: .bold)).padding(.bottom, 10)
                HStack(spacing: 6) {
                    Spacer(minLength: 0)
                    Text(text).font(.aurea(size: 26, weight: .bold).monospacedDigit()).lineLimit(1).minimumScaleFactor(0.4)
                        .background(selectedAll ? AureaColors.accentDim : .clear, in: RoundedRectangle(cornerRadius: 4))
                    if !request.unit.isEmpty { Text(request.unit).font(.aurea(size: 16)).foregroundStyle(AureaColors.muted) }
                }.padding(.horizontal, 14).padding(.vertical, 12).background(AureaColors.stage, in: RoundedRectangle(cornerRadius: 12))
                Text(hint).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).frame(maxWidth: .infinity, alignment: .trailing).frame(height: 22)
                ForEach(rows.indices, id: \.self) { row in
                    HStack(spacing: 6) {
                        ForEach(rows[row], id: \.self) { key in
                            Button { press(key) } label: {
                                Group {
                                    if key == "⌫" { CupertinoGlyph.text(CupertinoGlyph.DeleteLeft, size: 22) }
                                    else { Text(key).font(.aurea(size: 21, weight: .semibold)) }
                                }.foregroundStyle("÷×−+=".contains(key) ? AureaColors.accent : AureaColors.text)
                                    .frame(maxWidth: .infinity).frame(height: 48)
                                    .background("÷×−+=".contains(key) ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
                            }.buttonStyle(.plain).simultaneousGesture(LongPressGesture().onEnded { _ in
                                if key == "⌫" { text = ""; selectedAll = false }
                            })
                        }
                    }.padding(.horizontal, 3).padding(.bottom, 6)
                }
                HStack(spacing: 10) {
                    Button(AureaText.t("ds_cancelar"), action: onDismiss)
                        .frame(maxWidth: .infinity).padding(.vertical, 12).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                    Button { if let value { request.onValue(value); onDismiss() } } label: {
                        Text("OK").fontWeight(.bold).frame(maxWidth: .infinity).padding(.vertical, 12)
                            .foregroundStyle(value == nil ? AureaColors.disabled : AureaColors.onAction)
                            .background(value == nil ? AureaColors.chipHigh : AureaColors.accent, in: RoundedRectangle(cornerRadius: 8))
                    }.disabled(value == nil)
                }.font(.aurea(size: 15)).buttonStyle(.plain).padding(.top, 4)
            }.padding(.horizontal, 16).padding(.top, 14).padding(.bottom, 12)
                .background(AureaColors.surface, in: RoundedRectangle(cornerRadius: 13.5))
                .foregroundStyle(AureaColors.text)
        }.onAppear { text = display(request.value) }
    }
    private func press(_ key: String) {
        switch key {
        case "⌫": text = selectedAll ? "" : String(text.dropLast())
        case "±":
            var chars = Array(text), i = text.count
            while i > 0 && (chars[i - 1].isNumber || ",:%".contains(chars[i - 1])) { i -= 1 }
            if i > 0 && chars[i - 1] == "−" && (i == 1 || KeypadExpression.isOperator(chars[i - 2])) { chars.remove(at: i - 1) }
            else { chars.insert("−", at: i) }
            text = String(chars)
        case "=": if let result, abs(result) <= Double(Float.greatestFiniteMagnitude) { text = display(Float(result)) }
        case "%": if text.last?.isNumber == true { text += key }
        case "÷", "×", "−", "+":
            if text.isEmpty { if key == "−" { text = key } }
            else if let last = text.last, KeypadExpression.isOperator(last), !(key == "−" && (last == "×" || last == "÷")) { text = String(text.dropLast()) + key }
            else { text += key }
        case ",":
            let base = selectedAll ? "" : text
            let tail = String(base.reversed().prefix { $0.isNumber || ",:".contains($0) }.reversed())
            if !(tail.split(separator: ":", omittingEmptySubsequences: false).last?.contains(",") ?? false) {
                text = base + (tail.isEmpty || tail.last == ":" ? "0," : ",")
            }
        case ":": if !selectedAll && text.last?.isNumber == true { text += key }
        default: text = (selectedAll ? "" : text) + key
        }
        selectedAll = false
    }
}

struct ActionSheetRequest: Identifiable {
    let id = UUID()
    let title: String
    let actions: [SheetAction]
    init(title: String, items: [(String, () -> Void)]) {
        self.title = title; self.actions = items.map { SheetAction($0.0, onClick: $0.1) }
    }
    init(title: String, actions: [SheetAction]) { self.title = title; self.actions = actions }
}

struct NamePromptRequest: Identifiable {
    let id = UUID()
    let title: String
    let initial: String
    let onConfirm: (String) -> Void
    init(title: String, initial: String = "", onConfirm: @escaping (String) -> Void) {
        self.title = title; self.initial = initial; self.onConfirm = onConfirm
    }
}

// Original ui/ds/ColorPicker.kt. Requests and callbacks use display sRGB.
struct ColorSheetRequest: Identifiable {
    let id = UUID()
    let title: String
    let initial: [Float]
    let withAlpha: Bool
    let onChange: (Float, Float, Float, Float) -> Void
    let onDone: () -> Void
    init(title: String, initial: [Float], withAlpha: Bool = true,
         onChange: @escaping (Float, Float, Float, Float) -> Void, onDone: @escaping () -> Void) {
        self.title = title; self.initial = initial; self.withAlpha = withAlpha
        self.onChange = onChange; self.onDone = onDone
    }
}

enum AureaColorSpace {
    static func engineToDisplay(_ values: [Float]) -> [Float] {
        (0..<4).map { index in
            let value = (index < values.count ? values[index] : index == 3 ? 1 : 0).clamped(to: 0...1)
            if index == 3 { return value }
            return value <= 0.0031308 ? value * 12.92 : 1.055 * pow(value, 1 / 2.4) - 0.055
        }
    }
    static func displayToEngine(_ r: Float, _ g: Float, _ b: Float, _ a: Float) -> [Float] {
        [r, g, b].map { value in
            let x = value.clamped(to: 0...1)
            return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4)
        } + [a.clamped(to: 0...1)]
    }
    static func color(_ rgba: [Float]) -> Color {
        let v = (0..<4).map { index in (index < rgba.count ? rgba[index] : index == 3 ? Float(1) : 0).clamped(to: 0...1) }
        return Color(.sRGB, red: Double(v[0]), green: Double(v[1]), blue: Double(v[2]), opacity: Double(v[3]))
    }
    static func hsv(_ rgba: [Float]) -> (h: Float, s: Float, v: Float, a: Float) {
        let v = (0..<4).map { index in (index < rgba.count ? rgba[index] : index == 3 ? Float(1) : 0).clamped(to: 0...1) }
        // Android colorToHSV receives an ARGB8 color.
        let r = (v[0] * 255).rounded() / 255, g = (v[1] * 255).rounded() / 255, b = (v[2] * 255).rounded() / 255
        let hi = max(r, max(g, b)), lo = min(r, min(g, b)), d = hi - lo
        var h: Float = 0
        if d > 0 {
            if hi == r { h = ((g - b) / d).truncatingRemainder(dividingBy: 6) }
            else if hi == g { h = (b - r) / d + 2 }
            else { h = (r - g) / d + 4 }
            h *= 60; if h < 0 { h += 360 }
        }
        return (h, hi == 0 ? 0 : d / hi, hi, v[3])
    }
    static func rgba(h: Float, s: Float, v: Float, a: Float) -> [Float] {
        let hue = (h.truncatingRemainder(dividingBy: 360) + 360).truncatingRemainder(dividingBy: 360) / 60
        let c = v * s, x = c * (1 - abs(hue.truncatingRemainder(dividingBy: 2) - 1)), m = v - c
        let raw: [Float]
        switch Int(hue) {
        case 0: raw = [c, x, 0]
        case 1: raw = [x, c, 0]
        case 2: raw = [0, c, x]
        case 3: raw = [0, x, c]
        case 4: raw = [x, 0, c]
        default: raw = [c, 0, x]
        }
        return raw.map { (($0 + m).clamped(to: 0...1) * 255).rounded() / 255 } + [a.clamped(to: 0...1)]
    }
}

struct AureaColorSwatch: View {
    let color: Color
    var body: some View {
        Canvas { context, size in
            context.fill(Path(CGRect(origin: .zero, size: size)), with: .color(Color(hex: 0x3A4150)))
            var y: CGFloat = 0, row = 0
            while y < size.height {
                var x: CGFloat = row % 2 == 0 ? 6 : 0
                while x < size.width {
                    context.fill(Path(CGRect(x: x, y: y, width: 6, height: 6)), with: .color(Color(hex: 0x2A303B)))
                    x += 12
                }
                y += 6; row += 1
            }
            context.fill(Path(CGRect(origin: .zero, size: size)), with: .color(color))
        }
    }
}

/// The adjustment sheet is fixed while controls drag. Only the veil or Done
/// dismisses it, matching AureaAdjustSheet (not the native iOS sheet gesture).
struct AureaBottomOverlay<Content: View>: View {
    var modal = false
    let onDismiss: () -> Void
    @ViewBuilder var content: () -> Content
    @State private var shown = false
    @State private var closing = false
    var body: some View {
        GeometryReader { geometry in
            ZStack(alignment: .bottom) {
                (modal ? Color.black.opacity(0.54) : AureaColors.sheetScrim)
                    .opacity(shown ? 1 : 0).ignoresSafeArea().onTapGesture { dismiss() }
                VStack(spacing: 0) {
                    if modal {
                        Capsule().fill(Color.white.opacity(0.18)).frame(width: 36, height: 5).padding(.top, 10).padding(.bottom, 6)
                            .contentShape(Rectangle()).gesture(DragGesture().onEnded { if $0.translation.height > 60 { dismiss() } })
                    }
                    content()
                }
                .padding(.bottom, geometry.safeAreaInsets.bottom)
                .frame(maxWidth: .infinity)
                .background(AureaColors.surface, in: AureaSheetTopShape(radius: modal ? 18 : 13.5))
                .contentShape(Rectangle()).onTapGesture {}
                .offset(y: shown ? 0 : geometry.size.height)
            }.ignoresSafeArea(.container, edges: .bottom)
                .onAppear { withAnimation(.easeOut(duration: 0.22)) { shown = true } }
        }
    }
    private func dismiss() {
        guard !closing else { return }; closing = true
        withAnimation(.easeIn(duration: 0.15)) { shown = false }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.15, execute: onDismiss)
    }
}
private struct AureaSheetTopShape: Shape {
    let radius: CGFloat
    func path(in rect: CGRect) -> Path {
        var p = Path()
        p.move(to: CGPoint(x: rect.minX, y: rect.maxY)); p.addLine(to: CGPoint(x: rect.minX, y: rect.minY + radius))
        p.addQuadCurve(to: CGPoint(x: rect.minX + radius, y: rect.minY), control: CGPoint(x: rect.minX, y: rect.minY))
        p.addLine(to: CGPoint(x: rect.maxX - radius, y: rect.minY))
        p.addQuadCurve(to: CGPoint(x: rect.maxX, y: rect.minY + radius), control: CGPoint(x: rect.maxX, y: rect.minY))
        p.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY)); p.closeSubpath(); return p
    }
}

struct ColorPickerSheet: View {
    let request: ColorSheetRequest
    let onDismiss: () -> Void
    @State private var hue: Float = 0
    @State private var saturation: Float = 0
    @State private var brightness: Float = 0
    @State private var alpha: Float = 1
    private let quick: [UInt32] = [0xFFFFFF, 0x000000, 0x6FAED9, 0xA9D3EC, 0x35C4E7, 0x2BE3A0, 0xFFB020, 0xFF6B6B, 0xFF4FA3, 0xAAB6C3, 0x1B2530, 0xF7F9FB]
    private var rgba: [Float] { AureaColorSpace.rgba(h: hue, s: saturation, v: brightness, a: alpha) }
    private var current: Color { AureaColorSpace.color(rgba) }
    var body: some View {
        AureaBottomOverlay(onDismiss: onDismiss) {
            VStack(alignment: .leading, spacing: 0) {
                HStack(spacing: 12) {
                    Text(AureaText.t("ds_cor")).font(.aurea(size: 17, weight: .bold))
                    HStack(spacing: 0) {
                        AureaColorSwatch(color: AureaColorSpace.color(request.initial)).contentShape(Rectangle()).onTapGesture { setColor(request.initial) }
                        AureaColorSwatch(color: current)
                    }.frame(width: 76, height: 28).clipShape(RoundedRectangle(cornerRadius: 8))
                    Spacer(minLength: 0)
                    Button(AureaText.t("ds_pronto"), action: onDismiss).font(.aurea(size: 15, weight: .semibold)).foregroundStyle(AureaColors.accent)
                        .padding(.vertical, 6).padding(.horizontal, 4).buttonStyle(AureaPressStyle())
                }.padding(.bottom, 10)
                board.padding(.bottom, 14)
                hueStrip
                if request.withAlpha {
                    HStack(spacing: 8) {
                        ColorValueStrip(value: alpha, onValue: { alpha = $0; push() }) {
                            let opaque = AureaColorSpace.color([rgba[0], rgba[1], rgba[2], 1])
                            AureaColorSwatch(color: .clear)
                                .overlay(LinearGradient(colors: [opaque.opacity(0), opaque], startPoint: .leading, endPoint: .trailing))
                        }
                        Text("\(Int((alpha * 100).rounded()))%").font(.aurea(size: 13, weight: .semibold)).foregroundStyle(AureaColors.accent)
                            .frame(width: 52, height: 30).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                    }.padding(.top, 10)
                }
                HStack(spacing: 4) {
                    Text("#").font(.aurea(size: 14)).foregroundStyle(AureaColors.muted)
                    Text(String(format: "%02X%02X%02X", Int((rgba[0] * 255).rounded()), Int((rgba[1] * 255).rounded()), Int((rgba[2] * 255).rounded())))
                        .font(.aurea(size: 14).monospacedDigit()).padding(.horizontal, 8).padding(.vertical, 6)
                        .frame(width: 96).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                    Text("H \(Int(hue.rounded()))°  S \(Int((saturation * 100).rounded()))%  V \(Int((brightness * 100).rounded()))%")
                        .font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).padding(.leading, 4)
                }.padding(.top, 12)
                Text(AureaText.t("ds_rapidas")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).padding(.top, 12).padding(.bottom, 8)
                AureaFlowLayout(hGap: 10, vGap: 10) {
                    ForEach(quick, id: \.self) { rgb in
                        Circle().fill(Color(hex: rgb)).overlay(Circle().stroke(AureaColors.border, lineWidth: 1)).frame(width: 30, height: 30)
                            .contentShape(Circle()).onTapGesture { setColor([Float((rgb >> 16) & 255) / 255, Float((rgb >> 8) & 255) / 255, Float(rgb & 255) / 255, alpha]) }
                    }
                }
            }.padding(.init(top: 12, leading: 18, bottom: 16, trailing: 18)).foregroundStyle(AureaColors.text)
        }.onAppear { load(request.initial) }
    }
    private var board: some View {
        GeometryReader { geometry in
            let hueColor = AureaColorSpace.color(AureaColorSpace.rgba(h: hue, s: 1, v: 1, a: 1))
            ZStack(alignment: .topLeading) {
                LinearGradient(colors: [.white, hueColor], startPoint: .leading, endPoint: .trailing)
                LinearGradient(colors: [.clear, .black], startPoint: .top, endPoint: .bottom)
                Circle().stroke(.white, lineWidth: 2.5).frame(width: 18, height: 18)
                    .position(x: CGFloat(saturation) * geometry.size.width, y: CGFloat(1 - brightness) * geometry.size.height)
            }.clipShape(RoundedRectangle(cornerRadius: 10)).contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0).onChanged { value in
                    saturation = Float(value.location.x / max(1, geometry.size.width)).clamped(to: 0...1)
                    brightness = 1 - Float(value.location.y / max(1, geometry.size.height)).clamped(to: 0...1)
                    push()
                })
        }.frame(height: 170)
    }
    private var hueStrip: some View {
        ColorValueStrip(value: hue / 360, onValue: { hue = $0 * 360; push() }) {
            LinearGradient(colors: (0...6).map { AureaColorSpace.color(AureaColorSpace.rgba(h: Float($0) * 60, s: 1, v: 1, a: 1)) }, startPoint: .leading, endPoint: .trailing)
        }
    }
    private func load(_ rgba: [Float]) { let hsv = AureaColorSpace.hsv(rgba); hue = hsv.h; saturation = hsv.s; brightness = hsv.v; alpha = hsv.a }
    private func setColor(_ rgba: [Float]) { load(rgba); push() }
    private func push() { let c = rgba; request.onChange(c[0], c[1], c[2], c[3]) }
}
private struct ColorValueStrip<Background: View>: View {
    let value: Float
    let onValue: (Float) -> Void
    @ViewBuilder let background: () -> Background
    var body: some View {
        GeometryReader { geometry in
            ZStack {
                background().frame(height: 26).clipShape(RoundedRectangle(cornerRadius: 6))
                RoundedRectangle(cornerRadius: 7).stroke(.white, lineWidth: 2.5).frame(width: 14, height: 30)
                    .position(x: min(max(7, CGFloat(value) * geometry.size.width), max(7, geometry.size.width - 7)), y: 15)
            }.contentShape(Rectangle()).gesture(DragGesture(minimumDistance: 0).onChanged { onValue(Float($0.location.x / max(1, geometry.size.width)).clamped(to: 0...1)) })
        }.frame(height: 30)
    }
}
