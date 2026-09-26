// =============================================================================
//  Aurea / platform / ios / app / DesignSystem.swift
//
//  O coração dos painéis, portado linha a linha do Android:
//    · `ui/ds/PropertyControls.kt` — número pt-BR, régua de riscos, o GESTO do
//      número, caixa de valor, chip do rótulo, linha de propriedade, interruptor,
//      escolha, losango de keyframe e ícone de curva;
//    · `editor/panels/PanelChrome.kt` — cabeçalho, trilho esquerdo, trilho
//      direito, abas de parâmetro e aviso.
//
//  As assinaturas são as do CONTRATO (`UI_CONTRACT.md`, seção "A API CONGELADA"):
//  seis agentes escrevem painéis contra elas ao mesmo tempo. Nada aqui muda de
//  nome sem mudar o contrato.
// =============================================================================
import SwiftUI

/// ui/ds/EffectsControls.kt: shared by Effects and Particular.
struct AdvancedToggle: View {
    let open: Bool
    let count: Int
    let onToggle: () -> Void

    var body: some View {
        Button(action: onToggle) {
            HStack(spacing: 6) {
                Text(AureaText.t("ds_avancado")).font(.aurea(size: 14, weight: .semibold))
                    .foregroundStyle(AureaColors.accent)
                CupertinoGlyph.text(open ? CupertinoGlyph.ChevronUp : CupertinoGlyph.ChevronDown,
                                    size: AureaDims.iconXs, color: AureaColors.accent)
                Spacer()
                if !open {
                    Text(count == 1 ? AureaText.t("ds_1_ajuste") : AureaText.t("ds_adjustment_count", count))
                        .font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                }
            }.padding(.horizontal, 6).frame(height: AureaDims.minTap).contentShape(Rectangle())
        }.buttonStyle(.plain)
    }
}

// =============================================================================
// Número pt-BR
// =============================================================================

/// O número em pt-BR, com casas FIXAS (decisão D-1 = [A]): vírgula decimal e
/// sempre as mesmas casas ("0,82", "100,0%"). Casas variáveis fazem a caixa
/// "dançar" de largura durante o arrasto (bug B-03); o "-0,0" que o
/// arredondamento de um negativo minúsculo produz vira "0,0" para o campo não
/// piscar o sinal.
func numeroPtBr(_ v: Float, casas: Int = 1) -> String {
    let n = v.isFinite ? v : 0
    let c = min(max(casas, 0), 6)
    var s = String(format: "%.\(c)f", Double(n))
    if s.hasPrefix("-"), s.dropFirst().allSatisfy({ $0 == "0" || $0 == "." }) { s.removeFirst() }
    return s.replacingOccurrences(of: ".", with: ",")
}

/// Número + unidade como a A.01 escrevia: símbolos colados ("100,0%", "45°",
/// "30,0px"); palavra com espaço ("0,00 stops"), senão a unidade gruda no
/// número e vira ruído.
func comUnidade(_ texto: String, _ unit: String) -> String {
    if unit.isEmpty { return texto }
    let soSimbolos = unit.count <= 2 || !unit.allSatisfy { $0.isLetter }
    return soSimbolos ? texto + unit : texto + " " + unit
}

// =============================================================================
// Estado do keyframe e da expressão (três/quatro estados, dos dados do motor)
// =============================================================================

/// O que o losango mostra: parado, animado sem marca aqui, marca no cabeçote.
enum KeyframeLook { case none, animated, keyHere }

/// O que o "=" mostra: sem expressão, ligada, com erro, desligada.
enum ExpressionLook { case none, ok, error, off }

/// Cor do "=" de expressão (`expressionColor` do Android).
func expressionColor(_ look: ExpressionLook) -> Color {
    switch look {
    case .error: return AureaColors.expressionError
    case .ok: return AureaColors.accent
    default: return AureaColors.muted
    }
}

// =============================================================================
// Pressão (o `tocavel` do Android: encolhe para 0,965 e escurece para 0,82)
// =============================================================================

/// O feedback de toque da UI aprovada: sem ripple, o botão encolhe e escurece.
struct AureaPressStyle: ButtonStyle {
    var shrink: CGFloat = AureaMotion.pressScale

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .scaleEffect(configuration.isPressed ? shrink : 1)
            .opacity(configuration.isPressed ? AureaMotion.pressAlpha : 1)
            .animation(.easeOut(duration: configuration.isPressed ? 0.09 : 0.22),
                       value: configuration.isPressed)
    }
}

extension View {
    /// Torna qualquer conteúdo tocável com o retorno de pressão do app.
    func aureaTappable(shrink: CGFloat = AureaMotion.pressScale,
                       enabled: Bool = true,
                       action: @escaping () -> Void) -> some View {
        Button(action: action) { self }
            .buttonStyle(AureaPressStyle(shrink: shrink))
            .disabled(!enabled)
    }
}

// =============================================================================
// Régua de riscos (a fita)
// =============================================================================

/// Passo entre riscos, medido na referência (9 dp) — igual em toda régua.
let TickStep: CGFloat = AureaDims.tickStep

/// Um risco forte a cada 5 (índice ABSOLUTO no papel: anda junto com os fracos).
private let ticksPerStrong = 5

/// Onde os riscos começam a sumir, contado de cada borda (a fita não termina: some).
let TickFade: CGFloat = AureaDims.tickFade

/// A RÉGUA DE RISCOS, pintada num `Canvas` só (trinta riscos como views seriam
/// trinta nós refeitos a cada quadro do arrasto).
///
/// Os riscos SEGUEM O DEDO: a posição de cada um é `centro + valor/porDp`, então
/// arrastar para a direita aumenta o valor e os riscos andam para a direita —
/// a conta, os riscos e o número concordam. `value` é lido DENTRO do desenho:
/// mudar o valor só repinta, não recompõe.
struct TickRuler: View {
    private let value: () -> Float
    private let unitsPerDp: Float
    private let active: Bool
    private let height: CGFloat
    private let verticalPadding: CGFloat

    /// - Parameters:
    ///   - value: o valor atual, lido a cada desenho (nunca capturado antes).
    ///   - unitsPerDp: quantas unidades o dedo anda por dp de arrasto.
    ///   - active: centro aceso (a régua em edição); a outra do par fica branca.
    init(value: @escaping () -> Float, unitsPerDp: Float, active: Bool,
         height: CGFloat = 40, verticalPadding: CGFloat = 8) {
        self.value = value
        self.unitsPerDp = unitsPerDp
        self.active = active
        self.height = height
        self.verticalPadding = verticalPadding
    }

    var body: some View {
        Canvas { context, size in
            drawTicks(context, size: size, value: value(), unitsPerDp: unitsPerDp,
                      active: active, pad: verticalPadding)
        }
        .frame(height: height)
    }
}

private func drawTicks(_ ctx: GraphicsContext, size: CGSize, value: Float,
                       unitsPerDp: Float, active: Bool, pad: CGFloat) {
    let top = pad
    let bottom = size.height - pad
    if bottom <= top || size.width <= 0 { return }
    let step = TickStep
    let fade = TickFade
    let center = size.width / 2
    // Onde o valor ZERO cai no papel, em dp a partir da borda esquerda. No iOS
    // o desenho já está em pontos e 1 ponto é 1 dp — a mesma unidade do Android.
    let perDp = unitsPerDp > 0 ? unitsPerDp : 1
    let v = value.isFinite ? value : 0
    let base = CGFloat(v / perDp) + center
    let whole = (base / step).rounded(.down)
    let phase = base - whole * step
    var x = phase - step
    var k = 0
    while x <= size.width {
        let fromEdge = min(x, size.width - x)
        if fromEdge > 0 {
            let f = fromEdge >= fade ? 1 : fromEdge / fade
            let index = k - 1 - Int(whole)
            let strong = ((index % ticksPerStrong) + ticksPerStrong) % ticksPerStrong == 0
            // O Android multiplica o alfa do token pelo fade; aqui o mesmo.
            let color = AureaColors.muted.opacity(f * (strong ? 0.60 : 0.25))
            var line = Path()
            line.move(to: CGPoint(x: x, y: top))
            line.addLine(to: CGPoint(x: x, y: bottom))
            ctx.stroke(line, with: .color(color), lineWidth: strong ? 1.5 : 1)
        }
        x += step
        k += 1
    }
    var centerLine = Path()
    centerLine.move(to: CGPoint(x: center, y: top))
    centerLine.addLine(to: CGPoint(x: center, y: bottom))
    ctx.stroke(centerLine, with: .color(active ? AureaColors.accent : AureaColors.playhead),
               lineWidth: 2)
}

// =============================================================================
// O gesto do número
// =============================================================================

/// O GESTO de um número: arrasto horizontal que ACUMULA desde o início
/// (`novo = início + andado × porDp`, preso na faixa). Somar delta a delta
/// sobre o valor que volta do motor acumularia o arredondamento dele e o
/// desenho descolaria do dedo. Direita aumenta.
///
/// O início e o fim do gesto são avisados para quem abre/fecha o passo de
/// desfazer (um arrasto = um desfazer). Cancelamento também fecha.
private struct ValueDragModifier: ViewModifier {
    let enabled: Bool
    let start: () -> Float
    let unitsPerDp: () -> Float
    let min: Float
    let max: Float
    let onStart: () -> Void
    let onValue: (Float) -> Void
    let onEnd: () -> Void

    /// A densidade da tela. O gesto entrega PONTOS e no iOS o ponto é a unidade
    /// densidade-independente (o "dp" do Android) — a conta pontos → px → dp é o
    /// mesmo `dx / density` do Kotlin, escrita com o displayScale do ambiente em
    /// vez do `UIScreen.main` (que não existe em view alguma).
    @Environment(\.displayScale) private var displayScale
    @State private var active = false
    @State private var from: Float = 0
    @GestureState private var dragging = false
    @State private var direction = 0

    func body(content: Content) -> some View {
        content.simultaneousGesture(drag, including: enabled ? .all : .none)
            .onChange(of: dragging) { held in if !held { finish() } }
            .onChange(of: enabled) { on in if !on { finish() } }
            .onDisappear { finish() }
    }
    private func finish() { direction = 0; if active { active = false; onEnd() } }

    private var drag: some Gesture {
        DragGesture(minimumDistance: 4)
            .updating($dragging) { _, held, _ in held = true }
            .onChanged { g in
                if direction == 0 { direction = abs(g.translation.width) >= abs(g.translation.height) ? 1 : -1 }
                guard direction == 1 else { return }
                if !active {
                    let s = start()
                    from = s.isFinite ? s : 0
                    active = true
                    onStart()
                }
                let px = Double(g.translation.width) * Double(displayScale)
                let dp = px / Double(displayScale)
                let lo = min.isNaN ? -Float.infinity : min
                let hi = max.isNaN ? Float.infinity : max
                let v = (from + Float(dp) * unitsPerDp()).clamped(to: lo...hi)
                if v.isFinite { onValue(v) }
            }
            .onEnded { _ in
                finish()
            }
    }
}

extension View {
    func valueDrag(enabled: Bool,
                   start: @escaping () -> Float,
                   unitsPerDp: @escaping () -> Float,
                   min: Float,
                   max: Float,
                   onStart: @escaping () -> Void,
                   onValue: @escaping (Float) -> Void,
                   onEnd: @escaping () -> Void) -> some View {
        modifier(ValueDragModifier(enabled: enabled, start: start, unitsPerDp: unitsPerDp,
                                   min: min, max: max, onStart: onStart,
                                   onValue: onValue, onEnd: onEnd))
    }
}

// =============================================================================
// Caixa de valor ("pílula")
// =============================================================================

/// A CAIXA DE VALOR (`CampoDeValor`): altura 24, raio 8, fundo `campo`, número
/// 13 w600 em `accent` SUBLINHADO (o sublinhado é a promessa de que dá para
/// digitar; sem `onTap` não há sublinhado). O número encolhe antes de vazar.
struct ValueBox: View {
    private let text: String
    private let width: CGFloat
    private let onTap: (() -> Void)?
    private let enabled: Bool
    private let tint: Color

    init(_ text: String, width: CGFloat = 60, onTap: (() -> Void)? = nil) {
        self.text = text
        self.width = width
        self.onTap = onTap
        self.enabled = true
        self.tint = AureaColors.accent
    }

    init(_ text: String, enabled: Bool, tint: Color = AureaColors.accent,
         width: CGFloat = 60, onTap: (() -> Void)? = nil) {
        self.text = text
        self.width = width
        self.onTap = onTap
        self.enabled = enabled
        self.tint = tint
    }

    private var tappable: Bool { onTap != nil && enabled }

    var body: some View {
        Pill(text: text, width: width, tappable: tappable, enabled: enabled, tint: tint, onTap: onTap)
    }

    /// Uma peça só para a caixa não repetir a montagem nos dois inits.
    private struct Pill: View {
        let text: String
        let width: CGFloat
        let tappable: Bool
        let enabled: Bool
        let tint: Color
        let onTap: (() -> Void)?

        var body: some View {
            Text(text)
                .font(.aurea(size: 13, weight: .semibold).monospacedDigit())
                .foregroundStyle(enabled ? tint : AureaColors.muted)
                .underline(tappable)
                .lineLimit(1)
                .minimumScaleFactor(0.62)
                .multilineTextAlignment(.center)
                .padding(.horizontal, AureaDims.s1)
                .frame(width: width, height: AureaDims.valueBoxH)
                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: AureaDims.valueBoxRadius))
                .contentShape(Rectangle())
                .aureaTappable(shrink: 1, enabled: tappable) { onTap?() }
        }
    }
}

// =============================================================================
// Chip do rótulo e linhas de propriedade
// =============================================================================

private struct AureaSelectedPropertyKey: EnvironmentKey {
    static let defaultValue: String? = nil
}

extension EnvironmentValues {
    /// Qual rótulo de propriedade está escolhido (o chip aceso do Android).
    /// O chip lê daqui porque a API congelada não tem parâmetro de escolha.
    var aureaSelectedProperty: String? {
        get { self[AureaSelectedPropertyKey.self] }
        set { self[AureaSelectedPropertyKey.self] = newValue }
    }
}

extension View {
    /// Marca qual linha está escolhida para os `PropertyLabelChip` abaixo dela.
    func aureaSelectedProperty(_ label: String?) -> some View {
        environment(\.aureaSelectedProperty, label)
    }
}

/// O "=" pequeno das linhas com expressão.
struct ExpressionBadge: View {
    let look: ExpressionLook

    var body: some View {
        Text("=")
            .font(.aurea(size: 11, weight: .heavy))
            .foregroundStyle(expressionColor(look))
    }
}

/// O CHIP DO RÓTULO (94 × 32): nome em ATÉ DUAS LINHAS, 12 w600, sem encolher a
/// fonte (o bug B-01 era o rótulo de uma linha que encolhia até ficar ilegível).
/// Escolhido = fundo `campo` e texto `accent` sublinhado.
struct PropertyLabelChip: View {
    private let title: String
    private let expression: ExpressionLook
    private let onTap: (() -> Void)?
    private let keyframe: KeyframeLook

    @Environment(\.aureaSelectedProperty) private var selectedProperty

    init(_ title: String, expression: ExpressionLook, keyframe: KeyframeLook = .none, onTap: (() -> Void)? = nil) {
        self.title = title
        self.expression = expression
        self.onTap = onTap
        self.keyframe = keyframe
    }

    private var selected: Bool { selectedProperty == title }

    var body: some View {
        Text(title)
            .font(.aurea(size: 12, weight: .semibold))
            .multilineTextAlignment(.center)
            .lineLimit(2)
            .foregroundStyle(selected ? AureaColors.accent : AureaColors.muted)
            .underline(selected)
            .padding(.horizontal, AureaDims.labelChipPadH)
            .frame(width: AureaDims.labelChipW, height: AureaDims.labelChipH)
            .background(selected ? AureaColors.chip : Color.clear,
                        in: RoundedRectangle(cornerRadius: AureaDims.chipRadius))
            .overlay(alignment: .topTrailing) {
                if expression != .none {
                    ExpressionBadge(look: expression)
                        .offset(x: AureaDims.s1)
                }
            }
            .overlay(alignment: .topLeading) {
                if keyframe != .none {
                    Canvas { context, size in
                        var diamond = Path()
                        diamond.move(to: CGPoint(x: 3.5, y: 0)); diamond.addLine(to: CGPoint(x: 7, y: 3.5))
                        diamond.addLine(to: CGPoint(x: 3.5, y: 7)); diamond.addLine(to: CGPoint(x: 0, y: 3.5)); diamond.closeSubpath()
                        if keyframe == .keyHere { context.fill(diamond, with: .color(AureaColors.keyframe)) }
                        else { context.stroke(diamond, with: .color(AureaColors.keyframe), lineWidth: 1) }
                    }.frame(width: 7, height: 7).offset(x: -3, y: 3)
                }
            }
            .contentShape(Rectangle())
            .aureaTappable(shrink: 1, enabled: onTap != nil) { onTap?() }
    }
}

/// A LINHA DE PROPRIEDADE: 48 dp = `[chip 94] 8 [valor ...]`.
///
/// O valor é TEXTO já formatado por quem chama (o motor manda o número); quem
/// arrasta é a régua ao lado, com o `valueDrag`.
struct PropertyRow: View {
    private let label: String
    private let value: String
    private let look: KeyframeLook
    private let expression: ExpressionLook
    private let enabled: Bool
    private let onKeyframe: (() -> Void)?
    private let onExpression: (() -> Void)?
    private let onTap: (() -> Void)?

    init(_ label: String, value: String, look: KeyframeLook,
         expression: ExpressionLook = .none, enabled: Bool = true,
         onKeyframe: (() -> Void)? = nil,
         onExpression: (() -> Void)? = nil,
         onTap: (() -> Void)? = nil) {
        self.label = label
        self.value = value
        self.look = look
        self.expression = expression
        self.enabled = enabled
        self.onKeyframe = onKeyframe
        self.onExpression = onExpression
        self.onTap = onTap
    }

    var body: some View {
        HStack(spacing: AureaDims.s2) {
            PropertyLabelChip(label, expression: expression, onTap: onTap)
            Spacer(minLength: 0)
            ValueBox(value, enabled: enabled, onTap: onTap)
            if look != .none {
                KeyframeDiamondIcon(look: look, enabled: enabled)
                    .contentShape(Rectangle())
                    .aureaTappable(shrink: 1, enabled: enabled && onKeyframe != nil) { onKeyframe?() }
            }
        }
        .frame(height: AureaDims.propertyRowHeight)
        .opacity(enabled ? 1 : 0.45)
    }
}

/// Linha "rótulo + controle livre" (interruptor, escolha, cor): o mesmo chip de
/// 94 à esquerda para a coluna dos nomes não pular entre tipos.
struct PropertyCustomRow<Content: View>: View {
    private let label: String
    private let selected: Bool
    private let onSelect: () -> Void
    private let content: Content
    private let keyframe: KeyframeLook
    private let expression: ExpressionLook
    private let onExpression: (() -> Void)?

    init(_ label: String, selected: Bool, onSelect: @escaping () -> Void, keyframe: KeyframeLook = .none,
         expression: ExpressionLook = .none, onExpression: (() -> Void)? = nil,
         @ViewBuilder content: () -> Content) {
        self.label = label
        self.selected = selected
        self.onSelect = onSelect
        self.content = content()
        self.keyframe = keyframe
        self.expression = expression
        self.onExpression = onExpression
    }

    var body: some View {
        HStack(spacing: AureaDims.s2) {
            PropertyLabelChip(label, expression: expression, keyframe: keyframe, onTap: onSelect)
                .aureaSelectedProperty(selected ? label : nil)
                .onLongPressGesture { onExpression?() }
            content
                .frame(maxWidth: .infinity, alignment: .leading)
        }
        .frame(minHeight: AureaDims.propertyRowHeight)
    }
}

// =============================================================================
// Interruptor e escolha
// =============================================================================

/// O INTERRUPTOR (CupertinoSwitch da A.01): 51 × 31, ligado em `acao` #245D8C,
/// desligado em `campoAlto`; bola branca de 27 que desliza em 200 ms.
struct AureaToggle: View {
    private let checked: Bool
    private let enabled: Bool
    private let onCheckedChange: (Bool) -> Void

    init(checked: Bool, onCheckedChange: @escaping (Bool) -> Void) {
        self.checked = checked
        self.enabled = true
        self.onCheckedChange = onCheckedChange
    }

    init(checked: Bool, enabled: Bool, onCheckedChange: @escaping (Bool) -> Void) {
        self.checked = checked
        self.enabled = enabled
        self.onCheckedChange = onCheckedChange
    }

    var body: some View {
        ZStack(alignment: .leading) {
            RoundedRectangle(cornerRadius: AureaDims.toggleRadius)
                .fill(checked ? AureaColors.action : AureaColors.chipHigh)
            Circle()
                .fill(Color.white)
                .shadow(radius: 2, y: 1)
                .frame(width: AureaDims.toggleKnob, height: AureaDims.toggleKnob)
                .padding(.leading, checked ? AureaDims.toggleTravel : AureaDims.togglePad)
        }
        .frame(width: AureaDims.toggleW, height: AureaDims.toggleH)
        .opacity(enabled ? 1 : 0.45)
        .animation(.easeOut(duration: AureaMotion.normal), value: checked)
        .contentShape(Rectangle())
        .aureaTappable(shrink: 1, enabled: enabled) { onCheckedChange(!checked) }
    }
}

/// A ESCOLHA COM TUDO À VISTA (`_LinhaDeEscolha` da A.01): chips em fileira que
/// quebra linha, vão 6; aceso = `destaqueApagado` + texto `destaque`.
struct ChoiceChips: View {
    private let items: [String]
    private let selected: Int
    private let onSelect: (Int) -> Void

    init(_ items: [String], selected: Int, onSelect: @escaping (Int) -> Void) {
        self.items = items
        self.selected = selected
        self.onSelect = onSelect
    }

    var body: some View {
        AureaFlowLayout(hGap: 6, vGap: 6) {
            ForEach(Array(items.enumerated()), id: \.offset) { index, item in
                Chip(item, on: index == selected) { onSelect(index) }
            }
        }
        .padding(.vertical, AureaDims.chipPadV)
    }
}

/// Um chip da escolha (o mesmo desenho do item de `ChoiceChips`).
struct Chip: View {
    private let label: String
    private let on: Bool
    private let onClick: () -> Void

    init(_ label: String, on: Bool, onClick: @escaping () -> Void) {
        self.label = label
        self.on = on
        self.onClick = onClick
    }

    var body: some View {
        Text(label)
            .font(.aurea(size: 12))
            .foregroundStyle(on ? AureaColors.accent : AureaColors.text)
            .padding(.horizontal, AureaDims.chipPadH)
            .padding(.vertical, AureaDims.chipPadV)
            .background(on ? AureaColors.accentDim : AureaColors.chip,
                        in: RoundedRectangle(cornerRadius: AureaDims.chipRadius))
            .contentShape(Rectangle())
            .aureaTappable(shrink: 1, action: onClick)
    }
}

/// Título de seção de um painel (13 w700 muted + o respiro de 6 do Android).
struct SectionTitle: View {
    private let title: String

    init(_ t: String) { self.title = t }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text(title)
                .font(AureaType.Section)
                .foregroundStyle(AureaColors.muted)
            Spacer().frame(height: 6)
        }
    }
}

// =============================================================================
// Ícones pintados do trilho (losango de keyframe e curva)
// =============================================================================

/// O LOSANGO DO TRILHO (`_DiamondKeyframePainter`): losango vazado com "+"
/// (sem marca aqui) ou "−" (há marca aqui). Três estados de cor, lidos do
/// modelo: parado = branco, animado sem marca aqui = `keyframe`, marca aqui =
/// `accent`. Sem alvo = `#434956`.
struct KeyframeDiamondIcon: View {
    private let look: KeyframeLook
    private let enabled: Bool

    init(look: KeyframeLook, enabled: Bool) {
        self.look = look
        self.enabled = enabled
    }

    var body: some View {
        Canvas { ctx, size in
            let color: Color
            if !enabled { color = AureaColors.railDisabled }
            else if look == .keyHere { color = AureaColors.accent }
            else if look == .animated { color = AureaColors.keyframe }
            else { color = .white }
            let inset: CGFloat = 2
            let c = CGPoint(x: size.width / 2, y: size.height / 2)
            var diamond = Path()
            diamond.move(to: CGPoint(x: c.x, y: inset))
            diamond.addLine(to: CGPoint(x: size.width - inset, y: c.y))
            diamond.addLine(to: CGPoint(x: c.x, y: size.height - inset))
            diamond.addLine(to: CGPoint(x: inset, y: c.y))
            diamond.closeSubpath()
            ctx.stroke(diamond, with: .color(color), lineWidth: 1.6)
            let arm: CGFloat = 3.5
            var bar = Path()
            bar.move(to: CGPoint(x: c.x - arm, y: c.y))
            bar.addLine(to: CGPoint(x: c.x + arm, y: c.y))
            ctx.stroke(bar, with: .color(color), lineWidth: 1.5)
            if look != .keyHere {
                var stem = Path()
                stem.move(to: CGPoint(x: c.x, y: c.y - arm))
                stem.addLine(to: CGPoint(x: c.x, y: c.y + arm))
                ctx.stroke(stem, with: .color(color), lineWidth: 1.5)
            }
        }
        .frame(width: AureaDims.diamondIcon, height: AureaDims.diamondIcon)
    }
}

/// O ÍCONE DE CURVA DO TRILHO (`_CurveIconPainter`): caixa arredondada a 50 %
/// + curva em S. Inativo `#434956`; animado `accent`; senão muted.
struct CurveRailIcon: View {
    private let enabled: Bool
    private let animated: Bool

    init(enabled: Bool, animated: Bool) {
        self.enabled = enabled
        self.animated = animated
    }

    var body: some View {
        Canvas { ctx, size in
            let color: Color
            if !enabled { color = AureaColors.railDisabled }
            else if animated { color = AureaColors.accent }
            else { color = AureaColors.muted }
            let box = CGRect(x: 1, y: 1, width: size.width - 2, height: size.height - 2)
            ctx.stroke(Path(roundedRect: box, cornerRadius: 4),
                       with: .color(color.opacity(0.5)), lineWidth: 1.3)
            var curve = Path()
            curve.move(to: CGPoint(x: 4, y: size.height - 5))
            curve.addCurve(to: CGPoint(x: size.width - 4, y: 5),
                           control1: CGPoint(x: size.width * 0.45, y: size.height - 5),
                           control2: CGPoint(x: size.width * 0.55, y: 5))
            ctx.stroke(curve, with: .color(color),
                       style: StrokeStyle(lineWidth: 1.5, lineCap: .round))
        }
        .frame(width: AureaDims.curveIcon, height: AureaDims.curveIcon)
    }
}

// =============================================================================
// Cromo de painel (PanelChrome.kt)
// =============================================================================

/// O CABEÇALHO do painel (ContextSheet): borda superior 1 dp `#273442` e faixa
/// de 44 dp `#151C24` com `‹` e o título 14 w600.
struct PanelHeader: View {
    private let title: String
    private let onBack: () -> Void

    init(title: String, onBack: @escaping () -> Void) {
        self.title = title
        self.onBack = onBack
    }

    var body: some View {
        VStack(spacing: 0) {
            Rectangle()
                .fill(AureaColors.border)
                .frame(height: AureaDims.hairline)
            HStack(spacing: 0) {
                Button(action: onBack) {
                    MaterialGlyph("filled.ChevronLeft", size: AureaDims.panelBackIcon)
                        .frame(width: AureaDims.panelBackTarget, height: AureaDims.panelHeader)
                        .contentShape(Rectangle())
                }
                .buttonStyle(AureaPressStyle(shrink: 1))
                .accessibilityLabel(AureaText.t("pn_back_to_layer_tools"))
                Text(title)
                    .font(AureaType.EditorTitle)
                    .foregroundStyle(AureaColors.text)
                    .lineLimit(1)
                    .truncationMode(.tail)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.trailing, AureaDims.s3)
            }
            .frame(height: AureaDims.panelHeader)
            .background(AureaColors.surface)
        }
    }
}

/// O TRILHO ESQUERDO (46 dp): `‹` voltar · ◇ keyframe · curva · ⋯ — células de
/// alturas iguais, sempre nesta ordem (a mão aprende posição antes de ícone). O
/// losango e a curva ficam apagados quando não há o que marcar ou curvar.
struct LeftRail: View {
    private let keyframeLook: KeyframeLook
    private let onKeyframe: (() -> Void)?
    private let curveAnimated: Bool
    private let onCurve: (() -> Void)?
    private let onMore: (() -> Void)?
    private let onExpression: (() -> Void)?
    private let expression: ExpressionLook
    private let onBack: () -> Void

    init(keyframeLook: KeyframeLook, onKeyframe: (() -> Void)? = nil,
         curveAnimated: Bool = false, onCurve: (() -> Void)? = nil,
         onMore: (() -> Void)? = nil, expression: ExpressionLook = .none, onExpression: (() -> Void)? = nil, onBack: @escaping () -> Void) {
        self.keyframeLook = keyframeLook
        self.onKeyframe = onKeyframe
        self.curveAnimated = curveAnimated
        self.onCurve = onCurve
        self.onMore = onMore
        self.expression = expression
        self.onExpression = onExpression
        self.onBack = onBack
    }

    var body: some View {
        VStack(spacing: 0) {
            cell(label: AureaText.t("panel_voltar_ferramentas"), action: onBack) {
                MaterialGlyph("rounded.ChevronLeft", size: AureaDims.iconLg)
            }
            cell(label: keyframeLook == .keyHere
                    ? AureaText.t("panel_tirar_keyframe_daqui")
                    : AureaText.t("panel_marcar_keyframe_aqui"),
                 action: onKeyframe) {
                KeyframeDiamondIcon(look: keyframeLook, enabled: onKeyframe != nil)
            }
            cell(label: AureaText.t("panel_editar_curva_propriedade"), action: onCurve) {
                CurveRailIcon(enabled: onCurve != nil, animated: curveAnimated)
            }
            if let onExpression {
                cell(label: AureaText.t("panel_adicionar_expressao"), action: onExpression) {
                    Text("=").font(.aurea(size: 22, weight: .bold))
                        .foregroundStyle(expression == .none ? AureaColors.text : AureaColors.accent)
                }
            }
            if let onMore {
                VStack {
                    RailMoreButton(active: false, onClick: onMore)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .frame(width: AureaDims.railW)
        .frame(maxHeight: .infinity)
    }

    @ViewBuilder
    private func cell<Content: View>(label: String, action: (() -> Void)?,
                                     @ViewBuilder content: () -> Content) -> some View {
        let target = content()
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .contentShape(Rectangle())
        if let action {
            Button(action: action) { target }
                .buttonStyle(AureaPressStyle(shrink: 1))
                .accessibilityLabel(label)
        } else {
            target
        }
    }
}

/// O `⋯` DO TRILHO que não esconde um modo (`AmMenuIcon`): aceso e com um ponto
/// quando há modo ligado lá dentro.
struct RailMoreButton: View {
    private let active: Bool
    private let onClick: () -> Void

    init(active: Bool, onClick: @escaping () -> Void) {
        self.active = active
        self.onClick = onClick
    }

    var body: some View {
        ZStack {
            CupertinoGlyph.text(CupertinoGlyph.Ellipsis, size: AureaDims.iconLg,
                                color: active ? AureaColors.accent : AureaColors.text)
            if active {
                Circle()
                    .fill(AureaColors.accent)
                    .frame(width: AureaDims.railMoreDot, height: AureaDims.railMoreDot)
                    .offset(x: AureaDims.railMoreDotX, y: AureaDims.railMoreDotY)
            }
        }
        .frame(width: AureaDims.minTap, height: AureaDims.minTap)
        .contentShape(Rectangle())
        .aureaTappable(shrink: 1, action: onClick)
    }
}

/// O TRILHO DIREITO (40 dp): os modos empilhados em `spaceEvenly`; o vigente com
/// fundo `#1E222D`, borda 1,5 `accent` e ícone aceso. O botão encolhe quando a
/// coluna não cabe (cinco modos num painel baixo), em vez de rolar e esconder
/// justamente o último modo.
///
/// Cada modo é uma STRING: o nome de um SF Symbol, ou um glifo Cupertino de um
/// caractere só (a mesma fonte dos ícones do Android).
struct RightRail: View {
    private let modes: [String]
    private let selected: Int
    private let onSelect: (Int) -> Void

    init(modes: [String], selected: Int, onSelect: @escaping (Int) -> Void) {
        self.modes = modes
        self.selected = selected
        self.onSelect = onSelect
    }

    var body: some View {
        GeometryReader { geo in
            let cell: CGFloat = 44
            ScrollView(.vertical, showsIndicators: false) {
            VStack(spacing: 0) {
                Spacer(minLength: 0)
                ForEach(Array(modes.enumerated()), id: \.offset) { index, mode in
                    let on = index == selected
                    railIcon(mode, cell: cell, on: on)
                        .frame(width: cell, height: cell)
                        .background(on ? AureaColors.railModeFill : Color.clear,
                                    in: RoundedRectangle(cornerRadius: AureaDims.railModeRadius))
                        .overlay {
                            if on {
                                RoundedRectangle(cornerRadius: AureaDims.railModeRadius)
                                    .stroke(AureaColors.accent, lineWidth: AureaDims.railModeStroke)
                            }
                        }
                        .contentShape(Rectangle())
                        .aureaTappable(shrink: 1) { onSelect(index) }
                        .accessibilityElement(children: .combine)
                        .accessibilityIdentifier("aurea.panel.mode.\(index)")
                        .padding(.vertical, 2)
                }
                Spacer(minLength: 0)
            }
            .frame(maxWidth: .infinity)
            .frame(minHeight: geo.size.height)
            }
            .accessibilityIdentifier("aurea.panel.tools")
        }
        .frame(width: 48)
        .frame(maxHeight: .infinity)
    }

    @ViewBuilder
    private func railIcon(_ mode: String, cell: CGFloat, on: Bool) -> some View {
        let tint = on ? AureaColors.accent : AureaColors.muted
        let side = min(AureaDims.iconMd, cell * 0.6)
        if mode.hasPrefix("material:") {
            MaterialGlyph(String(mode.dropFirst(9)), size: side, color: tint)
        } else if mode.count == 1, let glyph = mode.first {
            CupertinoGlyph.text(glyph, size: side, color: tint)
        } else {
            Image(systemName: mode)
                .font(.aurea(size: side, weight: .medium))
                .foregroundStyle(tint)
        }
    }
}

/// AS ABAS DE PARÂMETRO (`AmParamTabs`): 48 de altura; abas que cabem dividem a
/// linha por igual; chip de 36, raio 9, `campo` (acesa `accentApagado`), texto
/// 12,5 (acesa w700 `accent`).
struct ParamTabs: View {
    private let tabs: [String]
    private let selected: Int
    private let onSelect: (Int) -> Void
    private let animated: (Int) -> Bool

    init(_ tabs: [String], selected: Int, onSelect: @escaping (Int) -> Void) {
        self.tabs = tabs
        self.selected = selected
        self.onSelect = onSelect
        self.animated = { _ in false }
    }

    init(_ tabs: [String], selected: Int, onSelect: @escaping (Int) -> Void,
         animated: @escaping (Int) -> Bool) {
        self.tabs = tabs
        self.selected = selected
        self.onSelect = onSelect
        self.animated = animated
    }

    var body: some View {
        HStack(spacing: 0) {
            ForEach(Array(tabs.enumerated()), id: \.offset) { index, tab in
                let on = index == selected
                HStack(spacing: AureaDims.paramTabDot) {
                    Text(tab)
                        .font(.aurea(size: 12.5, weight: on ? .bold : .medium))
                        .foregroundStyle(on ? AureaColors.accent : AureaColors.text)
                        .lineLimit(1)
                        .truncationMode(.tail)
                    if animated(index) {
                        Circle()
                            .fill(AureaColors.accent)
                            .frame(width: AureaDims.paramTabDot, height: AureaDims.paramTabDot)
                    }
                }
                .padding(.horizontal, AureaDims.paramTabPadH)
                .frame(maxWidth: .infinity)
                .frame(height: AureaDims.paramTabH - 2 * AureaDims.paramTabPadV)
                .background(on ? AureaColors.accentDim : AureaColors.chip,
                            in: RoundedRectangle(cornerRadius: AureaDims.paramTabRadius))
                .padding(.horizontal, AureaDims.paramTabGap)
                .contentShape(Rectangle())
                .aureaTappable(shrink: 1) { onSelect(index) }
            }
        }
        .padding(.horizontal, AureaDims.paramTabPadH)
        .padding(.vertical, AureaDims.paramTabPadV)
        .frame(height: AureaDims.paramTabH)
    }
}

/// Aviso de painel sem controle (texto 12,5 muted, várias linhas).
struct PanelNotice: View {
    private let text: String

    init(_ text: String) { self.text = text }

    var body: some View {
        Text(text)
            .font(AureaType.Property)
            .foregroundStyle(AureaColors.muted)
            .fixedSize(horizontal: false, vertical: true)
            .padding(.vertical, AureaDims.noticePadV)
            .frame(maxWidth: .infinity, alignment: .leading)
    }
}

extension View {
    /// A borda de 1 dp no topo (o cabelo que separa um painel da barra de cima).
    func drawTopHairline() -> some View {
        overlay(alignment: .top) {
            Rectangle()
                .fill(AureaColors.border)
                .frame(height: AureaDims.hairline)
        }
    }
}

// =============================================================================
// Fileira que quebra linha (o `FlowRow` do Material, que o SwiftUI não tem)
// =============================================================================

/// Um `Layout` de linhas que quebram: mede cada filho e vai enchendo a linha até
/// não caber mais. É o que o `FlowRow` faz no Android.
struct AureaFlowLayout: Layout {
    var hGap: CGFloat = 6
    var vGap: CGFloat = 6

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let maxWidth = proposal.width ?? .infinity
        var rows: CGFloat = 1
        var x: CGFloat = 0
        var rowHeight: CGFloat = 0
        var widest: CGFloat = 0
        for sub in subviews {
            let size = sub.sizeThatFits(.unspecified)
            if x > 0, x + size.width > maxWidth {
                widest = max(widest, x - hGap)
                rows += 1
                x = 0
                rowHeight = 0
            }
            x += size.width + hGap
            rowHeight = max(rowHeight, size.height)
        }
        widest = max(widest, max(0, x - hGap))
        let height = rows * rowHeight + (rows - 1) * vGap
        return CGSize(width: min(widest, maxWidth == .infinity ? widest : maxWidth), height: height)
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        var x = bounds.minX
        var y = bounds.minY
        var rowHeight: CGFloat = 0
        for sub in subviews {
            let size = sub.sizeThatFits(.unspecified)
            if x > bounds.minX, x + size.width > bounds.maxX {
                x = bounds.minX
                y += rowHeight + vGap
                rowHeight = 0
            }
            sub.place(at: CGPoint(x: x, y: y), proposal: ProposedViewSize(size))
            x += size.width + hGap
            rowHeight = max(rowHeight, size.height)
        }
    }
}

struct ColorWell: View {
    let color: Color
    let onClick: () -> Void
    var body: some View {
        Button(action: onClick) {
            RoundedRectangle(cornerRadius: 6).fill(color)
                .overlay(RoundedRectangle(cornerRadius: 6).stroke(AureaColors.border, lineWidth: 1))
                .frame(width: 32, height: 24).frame(minWidth: 44, minHeight: 44)
        }.buttonStyle(.plain).accessibilityLabel(AureaText.t("panel_cor"))
    }
}
