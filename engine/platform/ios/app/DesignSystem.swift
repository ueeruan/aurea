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

/// Passo entre riscos (redesenho 2026-09-29: 14 riscos na régua da linha de 40).
/// Constante local de propósito: o `AureaDims.tickStep` (9) mora no Theme.swift,
/// que é da casca do editor; o Android usa o mesmo 11 (`TickStep` do PropertyControls.kt).
let TickStep: CGFloat = 11

/// Onde os riscos começavam a sumir (régua antiga). A régua nova apaga pelo
/// brilho (`FxParamRowMath.tickBrightness`); fica para quem ainda lê o token.
let TickFade: CGFloat = AureaDims.tickFade

// =============================================================================
// A linha de parâmetro do redesenho 2026-09-29 (Efeitos.dc.html)
// =============================================================================

/// A CONTA da linha de parâmetro, espelho exato do `ParamRowMath.kt` (testado na
/// JVM em `ParamRowMathTest.kt`):
///  · rótulo: 13 pt até 8 letras; mais longo, 11 pt (numa linha até 12, depois duas);
///  · régua: o valor ACUMULA desde o início do gesto (`início + andado × porDp`),
///    preso na faixa estendida até o valor de partida (`dragBounds`);
///  · controle fino: dois dedos = 1/10; arrasto lento = 1/4, subindo até 1 com a
///    velocidade (o dedo rápido atravessa a faixa, o lento acerta o número);
///  · riscos: mais claros no centro, apagando para as bordas.
enum FxParamRowMath {
    /// Fonte do rótulo cheio / encolhido (pt).
    static let labelFullSize: CGFloat = 13
    static let labelSmallSize: CGFloat = 11
    /// Até quantas letras o rótulo cabe em 13 pt na caixa de 70.
    static let labelFullMaxChars = 8
    /// Até quantas letras o rótulo encolhido cabe numa linha só.
    static let labelOneLineMaxChars = 12
    /// Ganho com dois dedos na régua.
    static let twoFingerGain: Float = 0.1
    /// Ganho do arrasto lento (abaixo de `slowSpeed`).
    static let slowGain: Float = 0.25
    /// Velocidades (pt/ms) em que o ganho começa a subir e chega a 1.
    static let slowSpeed: Float = 0.08
    static let fastSpeed: Float = 0.35
    /// Peso da amostra nova na média da velocidade.
    static let speedSmoothing: Float = 0.35

    static func labelFontSize(_ label: String) -> CGFloat {
        label.trimmingCharacters(in: .whitespacesAndNewlines).count <= labelFullMaxChars ? labelFullSize : labelSmallSize
    }

    static func labelMaxLines(_ label: String) -> Int {
        label.trimmingCharacters(in: .whitespacesAndNewlines).count <= labelOneLineMaxChars ? 1 : 2
    }

    /// Ganho do arrasto: dois dedos, lento (fino) ou rápido (1:1).
    static func scrubGain(pointers: Int, speed: Float) -> Float {
        if pointers >= 2 { return twoFingerGain }
        let s = abs(speed)
        if !s.isFinite || s >= fastSpeed { return 1 }
        if s <= slowSpeed { return slowGain }
        let t = (s - slowSpeed) / (fastSpeed - slowSpeed)
        return slowGain + (1 - slowGain) * t
    }

    /// Velocidade suavizada (pt/ms) com a amostra `dx` em `dt`; `dt` inválido mantém a anterior.
    static func smoothSpeed(previous: Float, dx: Float, dt: Float) -> Float {
        if !(dt > 0) || !dx.isFinite { return previous }
        let sample = abs(dx) / dt
        return previous + (sample - previous) * speedSmoothing
    }

    /// Faixa de UM arrasto: `min...max`, estendida até o valor de partida `from`
    /// quando ele está fora (valor digitado além do slider). Nunca troca os lados.
    static func dragBounds(min lo: Float, max hi: Float, from: Float) -> (Float, Float) {
        let a = Swift.min(lo, hi)
        let b = Swift.max(lo, hi)
        if !from.isFinite { return (a, b) }
        return (Swift.min(a, from), Swift.max(b, from))
    }

    /// O valor do arrasto: `início + andado × porDp`, preso em `min...max`
    /// estendida até o início (valor digitado além da régua não salta de volta).
    /// Início não finito parte do zero; resultado nunca é NaN.
    static func scrubValue(from: Float, walked: Float, unitsPerDp: Float, min: Float, max: Float) -> Float {
        let start = from.isFinite ? from : 0
        let lo = min.isNaN ? -Float.infinity : min
        let hi = max.isNaN ? Float.infinity : max
        let (l, h) = dragBounds(min: lo, max: hi, from: start)
        let v = start + walked * (unitsPerDp.isFinite ? unitsPerDp : 0)
        if v.isNaN { return start }
        return Swift.min(Swift.max(v, l), h)
    }

    /// Brilho do risco em `x` numa régua de largura `width`: 1 no centro, 0 nas bordas.
    static func tickBrightness(x: CGFloat, width: CGFloat) -> CGFloat {
        if !(width > 0) { return 0 }
        let half = width / 2
        return Swift.min(Swift.max(1 - abs(x - half) / half, 0), 1)
    }
}

/// Cores da LINHA DE PARÂMETRO (painel, cartão do efeito, rótulo, caixa de valor,
/// trilho e riscos) — `ParamRowColors` do Android. O destaque continua sendo o
/// do tema (`AureaColors.accent`, #6FAED9).
enum ParamRowColors {
    static let panel = Color(hex: 0x121826)
    static let card = Color(hex: 0x252F43)
    static let labelOn = Color(hex: 0x121826)
    static let labelOff = Color(hex: 0x2B364B)
    static let labelOffText = Color(hex: 0xAAB6C3)
    static let valueBox = Color(hex: 0x1B2333)
    static let railLine = Color(hex: 0x222B3B)
    static let railDisabled = Color(hex: 0x3E4859)
    static let tickEdge = Color(hex: tickEdgeHex)
    static let tickCenter = Color(hex: tickCenterHex)
    static let rgbText = Color(hex: 0x6F8A9E)
    static let swatchBorder = Color(hex: 0x3C475C)

    static let tickEdgeHex: UInt32 = 0x3C475C
    static let tickCenterHex: UInt32 = 0x97A3BA

    /// O risco entre a cor da borda e a do centro (`lerp(TickEdge, TickCenter, t)`).
    static func tick(_ t: CGFloat) -> Color {
        let k = Double(Swift.min(Swift.max(t, 0), 1))
        func channel(_ shift: UInt32) -> Double {
            let a = Double((tickEdgeHex >> shift) & 0xFF) / 255
            let b = Double((tickCenterHex >> shift) & 0xFF) / 255
            return a + (b - a) * k
        }
        return Color(.sRGB, red: channel(16), green: channel(8), blue: channel(0), opacity: 1)
    }
}

/// Property controls keep readable numbers and 52pt touch targets.
enum ParamRowDims {
    static let row: CGFloat = 56
    static let gap: CGFloat = 6
    static let labelW: CGFloat = 76
    static let labelH: CGFloat = 52
    static let valueW: CGFloat = 76
    static let radius: CGFloat = 6
}

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
    // Os riscos têm ~70 % da altura da linha do meio (22 de 32 na referência).
    let inset = (bottom - top) * 0.155
    let center = size.width / 2
    // Onde o valor ZERO cai no papel, em dp a partir da borda esquerda. No iOS
    // o desenho já está em pontos e 1 ponto é 1 dp — a mesma unidade do Android.
    let perDp = unitsPerDp > 0 ? unitsPerDp : 1
    let v = value.isFinite ? value : 0
    let base = CGFloat(-v / perDp) + center
    let whole = (base / step).rounded(.down)
    var x = base - whole * step - step
    while x <= size.width {
        // Riscos uniformes de 1 pt que acendem perto do centro e somem nas bordas.
        let b = FxParamRowMath.tickBrightness(x: x, width: size.width)
        if b > 0 {
            var line = Path()
            line.move(to: CGPoint(x: x, y: top + inset))
            line.addLine(to: CGPoint(x: x, y: bottom - inset))
            ctx.stroke(line, with: .color(ParamRowColors.tick(b * b).opacity(Double(min(1, b * 4)))),
                       lineWidth: 1)
        }
        x += step
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
/// desenho descolaria do dedo. A régua corre sob o indicador: esquerda aumenta.
///
/// O início e o fim do gesto são avisados para quem abre/fecha o passo de
/// desfazer (um arrasto = um desfazer). Cancelamento também fecha.
///
/// CONTROLE FINO (`FxParamRowMath.scrubGain`): o andado soma cada passo do dedo
/// multiplicado pelo ganho da velocidade suavizada — devagar = 1/4, rápido = 1:1.
/// O "dois dedos = 1/10" do Android fica de fora: o `DragGesture` do SwiftUI é de
/// um toque só; o arrasto lento já dá a precisão.
private struct ValueDragModifier: ViewModifier {
    let enabled: Bool
    let start: () -> Float
    let unitsPerDp: () -> Float
    let min: Float
    let max: Float
    let onStart: () -> Void
    let onValue: (Float) -> Void
    let onEnd: () -> Void

    // O gesto entrega PONTOS e no iOS o ponto é a unidade densidade-independente
    // (o "dp" do Android): o `dx / density` do Kotlin já vem pronto.
    @State private var active = false
    @State private var from: Float = 0
    @GestureState private var dragging = false
    @State private var direction = 0
    /// Andado (dp, já com o ganho) desde o início do gesto.
    @State private var walked: Float = 0
    /// Velocidade suavizada (pt/ms) e a última amostra do dedo.
    @State private var speed: Float = FxParamRowMath.fastSpeed
    @State private var lastX: CGFloat = 0
    @State private var lastTime = Date()

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
                    walked = 0
                    speed = FxParamRowMath.fastSpeed
                    lastX = 0
                    // O primeiro passo (o limiar do gesto) conta como um quadro de 16 ms.
                    lastTime = g.time.addingTimeInterval(-0.016)
                    active = true
                    onStart()
                }
                // Passo deste evento: delta da translação (pontos = dp, como o
                // `dx / density` do Kotlin) e delta do tempo em ms.
                let dx = Float(g.translation.width - lastX)
                let dt = Float(g.time.timeIntervalSince(lastTime) * 1000)
                lastX = g.translation.width
                lastTime = g.time
                guard dx != 0 else { return }
                speed = FxParamRowMath.smoothSpeed(previous: speed, dx: dx, dt: dt)
                walked -= dx * FxParamRowMath.scrubGain(pointers: 1, speed: speed)
                let v = FxParamRowMath.scrubValue(from: from, walked: walked, unitsPerDp: unitsPerDp(),
                                                  min: min, max: max)
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

    // 0.3.4 painéis: largura padrão `PanelMetrics.fieldWidth` (72, antes 60), par do Android.
    init(_ text: String, width: CGFloat = PanelMetrics.fieldWidth, onTap: (() -> Void)? = nil) {
        self.text = text
        self.width = width
        self.onTap = onTap
        self.enabled = true
        self.tint = AureaColors.accent
    }

    init(_ text: String, enabled: Bool, tint: Color = AureaColors.accent,
         width: CGFloat = PanelMetrics.fieldWidth, onTap: (() -> Void)? = nil) {
        self.text = text
        self.width = width
        self.onTap = onTap
        self.enabled = enabled
        self.tint = tint
    }

    private var tappable: Bool { onTap != nil && enabled }

    var body: some View {
        // O número (timecode, "−12,0px", "100,0%") lê em LTR mesmo em árabe: sem
        // isso o bidi joga o sinal de menos para o fim ("12,0−").
        Pill(text: text, width: width, tappable: tappable, enabled: enabled, tint: tint, onTap: onTap)
            .keepLtr()
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
            // 0.3.4 painéis: número 15 que encolhe numa caixa de 36 (antes 13 em 24).
            Text(text)
                .font(.aurea(size: PanelMetrics.fieldValue, weight: .semibold).monospacedDigit())
                .foregroundStyle(enabled ? tint : AureaColors.muted)
                .underline(tappable)
                .lineLimit(1)
                .minimumScaleFactor(PanelMetrics.fieldValueMin / PanelMetrics.fieldValue)
                .multilineTextAlignment(.center)
                .padding(.horizontal, AureaDims.s1)
                .frame(width: width)
                .frame(minHeight: PanelMetrics.fieldHeight)
                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: AureaDims.valueBoxRadius))
                .frame(minHeight: PanelMetrics.touch)
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

/// O DESENHO do rótulo da linha (redesenho 2026-09-29), sem gesto: caixa 70 × 36,
/// raio 6, SEMPRE sublinhado (a promessa de que toca). Escolhido = fundo escuro
/// #121826 e texto no destaque em negrito; os outros #2B364B com texto apagado.
/// Rótulo longo encolhe para 11 pt e quebra em duas linhas
/// (`FxParamRowMath.labelFontSize`). O losango pequeno no canto diz que a trilha
/// anima (cheio = marca no cabeçote); o "=" no outro canto, que tem expressão.
/// `PropertyLabelChip` e o rótulo das linhas de Efeitos usam este mesmo desenho.
struct ParamRowLabel: View {
    let label: String
    let selected: Bool
    var keyframe: KeyframeLook = .none
    var expression: ExpressionLook = .none
    var width: CGFloat = ParamRowDims.labelW

    var body: some View {
        Text(label)
            .font(.aurea(size: 14, weight: selected ? .bold : .regular))
            .multilineTextAlignment(.center)
            .lineLimit(FxParamRowMath.labelMaxLines(label))
            .minimumScaleFactor(0.86)
            .truncationMode(.tail)
            .foregroundStyle(selected ? AureaColors.accent : ParamRowColors.labelOffText)
            .padding(.horizontal, 4)
            .frame(width: width, height: ParamRowDims.labelH)
            .background(selected ? ParamRowColors.labelOn : ParamRowColors.labelOff,
                        in: RoundedRectangle(cornerRadius: ParamRowDims.radius))
            .overlay(alignment: .topTrailing) {
                if expression != .none {
                    ExpressionBadge(look: expression)
                        .offset(x: 2)
                }
            }
            .overlay(alignment: .topLeading) {
                if keyframe != .none {
                    Canvas { context, size in
                        var diamond = Path()
                        diamond.move(to: CGPoint(x: size.width / 2, y: 0))
                        diamond.addLine(to: CGPoint(x: size.width, y: size.height / 2))
                        diamond.addLine(to: CGPoint(x: size.width / 2, y: size.height))
                        diamond.addLine(to: CGPoint(x: 0, y: size.height / 2))
                        diamond.closeSubpath()
                        if keyframe == .keyHere { context.fill(diamond, with: .color(AureaColors.keyframe)) }
                        else { context.stroke(diamond, with: .color(AureaColors.keyframe), lineWidth: 1) }
                    }.frame(width: 7, height: 7).offset(x: -1, y: 3)
                }
            }
    }
}

/// O RÓTULO DA LINHA tocável (`ParamRowLabel` + toque). A escolha vem do
/// ambiente (`aureaSelectedProperty`) porque a API congelada não tem parâmetro.
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
        ParamRowLabel(label: title, selected: selected, keyframe: keyframe, expression: expression)
            .contentShape(Rectangle())
            .aureaTappable(shrink: 1, enabled: onTap != nil) { onTap?() }
    }
}

/// Precise numeric value: centered tabular digits, with typing on tap.
struct ParamValueBox: View {
    private let text: String
    private let width: CGFloat
    private let enabled: Bool
    private let onTap: (() -> Void)?

    init(_ text: String, width: CGFloat = ParamRowDims.valueW, enabled: Bool = true, onTap: (() -> Void)? = nil) {
        self.text = text
        self.width = width
        self.enabled = enabled
        self.onTap = onTap
    }

    var body: some View {
        Text(text)
            .font(.aurea(size: 16).monospacedDigit())
            .foregroundStyle(enabled ? AureaColors.text : AureaColors.muted)
            .lineLimit(1)
            .minimumScaleFactor(0.75)
            .truncationMode(.tail)
            .multilineTextAlignment(.center)
            .padding(.horizontal, 4)
            .frame(width: width, height: ParamRowDims.labelH, alignment: .center)
            // Signs and digits retain their order in right-to-left languages.
            .keepLtr()
            .background(ParamRowColors.valueBox, in: RoundedRectangle(cornerRadius: ParamRowDims.radius))
            .contentShape(Rectangle())
            .aureaTappable(shrink: 1, enabled: onTap != nil && enabled) { onTap?() }
    }
}

/// One vector uses one ruler. Axis selection changes the presentation; callers
/// retain native property IDs, grouped keys and their own undo boundaries.
struct VectorPropertyRow: View {
    let label: String
    let values: [Float]
    let axes: [String]
    let selectedAxis: Int
    let unitsPerPoint: Float
    let range: ClosedRange<Float>
    let formattedValue: (Int, Float) -> String
    var compactValue: ((Int, Float) -> String)? = nil
    // Focused transform faces use the free panel area below the axis fields.
    // Effects retain their compact rows by default.
    var focusedPanel = false
    var enabled = true
    var selected = false
    var look: KeyframeLook = .none
    var expression: ExpressionLook = .none
    var identifier = ""
    var onMenu: (() -> Void)? = nil
    let onSelect: (Int) -> Void
    let onKeypad: (Int) -> Void
    let onBegin: () -> Void
    let onValue: (Int, Float) -> Void
    let onEnd: () -> Void
    @State private var dragging = false
    @State private var live: Float = 0
    @State private var dragAxis = 0
    @State private var availableWidth: CGFloat = 320
    private var count: Int { min(4, min(values.count, axes.count)) }
    private var axis: Int { max(0, min(count - 1, dragging ? dragAxis : selectedAxis)) }
    private var amount: Float { values.indices.contains(axis) ? values[axis] : 0 }
    private func text(_ index: Int) -> String {
        let amount = dragging && index == dragAxis ? live : values[index]
        return compactValue?(index, amount) ?? formattedValue(index, amount)
    }
    private func widths(_ width: CGFloat) -> [CGFloat] {
        let maximum = max(44, (width - CGFloat(max(0, count - 1)) * 4) / CGFloat(max(1, count)))
        return (0..<count).map { min(text($0).count > 6 ? 64 : 44, maximum) }
    }
    private func inline(_ width: CGFloat) -> Bool { width >= 44 + widths(width).reduce(0, +) + CGFloat(max(0, count - 1)) * 4 + 84 }
    var body: some View {
        GeometryReader { geometry in
            let width = geometry.size.width
            let boxWidths = widths(width)
            let boxesWidth = boxWidths.reduce(0, +) + CGFloat(max(0, count - 1)) * 4
            let focusedInline = width >= 44 + boxesWidth + ParamRowDims.gap
            let focusedHeaderHeight = focusedInline ? ParamRowDims.labelH : ParamRowDims.labelH * 2 + 4
            Group {
            if focusedPanel {
                VStack(spacing: 8) {
                    if focusedInline {
                        HStack(spacing: ParamRowDims.gap) {
                            name(width: min(76, max(44, width - boxesWidth - ParamRowDims.gap)))
                            Spacer(minLength: 0)
                            boxes(boxWidths)
                        }.frame(height: ParamRowDims.labelH)
                    } else {
                        VStack(spacing: 4) {
                            name(width: 76).frame(maxWidth: .infinity, alignment: .leading)
                            boxes(boxWidths).frame(maxWidth: .infinity, alignment: .trailing)
                        }
                    }
                    ruler(height: min(120, max(44, geometry.size.height - focusedHeaderHeight - 8)), expanded: true)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                VStack(spacing: 4) {
                    HStack(spacing: ParamRowDims.gap) {
                        name(width: inline(width) ? min(76, max(44, width - boxesWidth - 84)) : 76)
                        ruler(height: ParamRowDims.labelH)
                        if inline(width) { boxes(boxWidths) }
                    }
                    if !inline(width) { boxes(boxWidths).frame(maxWidth: .infinity, alignment: .trailing) }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
            }
            .onAppear { availableWidth = width }
            .onChange(of: width) { availableWidth = $0 }
        }
        .frame(minHeight: focusedPanel ? 180 : nil, maxHeight: focusedPanel ? .infinity : nil)
        .frame(height: focusedPanel ? nil : inline(availableWidth) ? ParamRowDims.row : ParamRowDims.labelH * 2 + 4)
        .accessibilityElement(children: .contain).accessibilityIdentifier(identifier)
        .onDisappear(perform: finish)
    }
    private func ruler(height: CGFloat, expanded: Bool = false) -> some View {
        TickRuler(value: { dragging ? live : amount }, unitsPerDp: unitsPerPoint,
                  active: enabled && (selected || dragging), height: height, verticalPadding: 4)
            .frame(maxWidth: .infinity, maxHeight: expanded ? .infinity : nil)
            .frame(minWidth: 72, minHeight: expanded ? 44 : ParamRowDims.labelH)
            .contentShape(Rectangle())
            .valueDrag(enabled: enabled && count > 0, start: { amount }, unitsPerDp: { unitsPerPoint },
                       min: range.lowerBound, max: range.upperBound,
                       onStart: { dragAxis = axis; live = amount; dragging = true; onSelect(dragAxis); onBegin() },
                       onValue: { live = $0; onValue(dragAxis, $0) }, onEnd: finish)
            .accessibilityElement().accessibilityLabel(label + " · " + (axes.indices.contains(axis) ? axes[axis] : ""))
            .accessibilityValue(formattedValue(axis, dragging ? live : amount))
            .accessibilityAdjustableAction { direction in
                let delta = unitsPerPoint * 10 * (direction == .increment ? 1 : direction == .decrement ? -1 : 0)
                guard enabled, count > 0, delta != 0 else { return }
                onSelect(axis); onBegin(); onValue(axis, (amount + delta).clamped(to: range)); onEnd()
            }.accessibilityIdentifier(identifier + ".ruler")
    }
    private func boxes(_ widths: [CGFloat]) -> some View {
        HStack(spacing: 4) {
            ForEach(0..<count, id: \.self) { index in
                Button {
                    guard enabled, !dragging else { return }
                    if selected && index == axis { onKeypad(index) } else { finish(); onSelect(index) }
                } label: {
                    VStack(spacing: 2) {
                        Text(text(index)).font(.aurea(size: 16).monospacedDigit()).lineLimit(1)
                            .minimumScaleFactor(0.75).truncationMode(.tail)
                        Text(axes[index]).font(.aurea(size: 12, weight: .semibold))
                    }.foregroundStyle(index == 0 ? AureaColors.keyframe : index == 2 ? AureaColors.accent : AureaColors.text)
                        .frame(width: widths[index], height: ParamRowDims.labelH)
                        .background(selected && index == axis ? ParamRowColors.labelOn : ParamRowColors.valueBox,
                                    in: RoundedRectangle(cornerRadius: ParamRowDims.radius)).contentShape(Rectangle())
                }.buttonStyle(.plain)
                    .onLongPressGesture(minimumDuration: 0.5) { guard enabled, !dragging else { return }; onSelect(index); onKeypad(index) }
                    .disabled(!enabled || dragging)
                    .accessibilityLabel(label + " · " + axes[index]).accessibilityValue(formattedValue(index, values[index]))
                    .accessibilityAddTraits(selected && index == axis ? .isSelected : [])
                    .accessibilityIdentifier(identifier + ".axis.\(index)")
            }
        }.keepLtr()
    }
    private func name(width: CGFloat) -> some View {
        ParamRowLabel(label: label, selected: selected, keyframe: look, expression: expression, width: width)
            .contentShape(Rectangle()).onTapGesture { if enabled && !dragging { onSelect(axis) } }
            .onLongPressGesture(minimumDuration: 0.5) { if enabled && !dragging { onMenu?() } }
            .accessibilityLabel(label).accessibilityAddTraits(.isButton)
    }
    private func finish() { if dragging { dragging = false; onEnd() } }
}

/// Display only: keep complete values/units in precise input and accessibility.
func compactPropertyNumber(_ value: Float, decimals: Int) -> String {
    var text = numeroPtBr(value, casas: decimals)
    if decimals > 0 && (text.contains(",") || text.contains(".")) {
        while text.last == "0" { text.removeLast() }
        if text.last == "," || text.last == "." { text.removeLast() }
    }
    return text
}

/// A LINHA DE PROPRIEDADE (texto): nome à esquerda e valor preciso à direita.
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
        HStack(spacing: ParamRowDims.gap) {
            PropertyLabelChip(label, expression: expression, onTap: onTap)
            Spacer(minLength: 0)
            ParamValueBox(value, enabled: enabled, onTap: onTap)
            if look != .none {
                KeyframeDiamondIcon(look: look, enabled: enabled)
                    .frame(width: 44, height: ParamRowDims.labelH)
                    .contentShape(Rectangle())
                    .aureaTappable(shrink: 1, enabled: enabled && onKeyframe != nil) { onKeyframe?() }
            }
        }
        .frame(height: ParamRowDims.row)
        .opacity(enabled ? 1 : 0.45)
    }
}

/// Linha "rótulo + controle livre" (interruptor, escolha, cor): o mesmo rótulo
/// de 70 à esquerda para a coluna dos nomes não pular entre tipos.
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
        HStack(spacing: ParamRowDims.gap) {
            PropertyLabelChip(label, expression: expression, keyframe: keyframe, onTap: onSelect)
                .aureaSelectedProperty(selected ? label : nil)
                .onLongPressGesture { onExpression?() }
            content
                .frame(maxWidth: .infinity, alignment: .leading)
        }
        .frame(minHeight: ParamRowDims.row)
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
            // 0.3.4: o traço fino do estilo dos ícones (AureaIconStyle.line na caixa de 24), cantos redondos.
            let line = StrokeStyle(lineWidth: AureaIconStyle.line * min(size.width, size.height) / 24, lineCap: .round, lineJoin: .round)
            ctx.stroke(diamond, with: .color(color), style: line)
            let arm: CGFloat = 3.5
            var bar = Path()
            bar.move(to: CGPoint(x: c.x - arm, y: c.y))
            bar.addLine(to: CGPoint(x: c.x + arm, y: c.y))
            ctx.stroke(bar, with: .color(color), style: line)
            if look != .keyHere {
                var stem = Path()
                stem.move(to: CGPoint(x: c.x, y: c.y - arm))
                stem.addLine(to: CGPoint(x: c.x, y: c.y + arm))
                ctx.stroke(stem, with: .color(color), style: line)
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
            // 0.3.4: o traço fino do estilo dos ícones (AureaIconStyle.line na caixa de 24).
            let w = AureaIconStyle.line * min(size.width, size.height) / 24
            ctx.stroke(Path(roundedRect: box, cornerRadius: 4),
                       with: .color(color.opacity(0.5)), lineWidth: w)
            var curve = Path()
            curve.move(to: CGPoint(x: 4, y: size.height - 5))
            curve.addCurve(to: CGPoint(x: size.width - 4, y: 5),
                           control1: CGPoint(x: size.width * 0.45, y: size.height - 5),
                           control2: CGPoint(x: size.width * 0.55, y: 5))
            ctx.stroke(curve, with: .color(color),
                       style: StrokeStyle(lineWidth: w, lineCap: .round))
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

/// O TRILHO ESQUERDO (redesenho 2026-09-29, 58 de largura com o cabelo de 1 à
/// direita): botões de 44 empilhados do topo com vão de 6 — `‹` voltar · ◇+
/// keyframe · curva · (=) — e o `⋯` preso no pé. Sempre nesta ordem (a mão
/// aprende posição antes de ícone). O losango e a curva ficam apagados quando
/// não há o que marcar ou curvar.
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
        VStack(spacing: LeftRailLayout.gap) {
            // 0.3.4 painéis: ícones da coluna em `PanelMetrics.railIcon` (26).
            cell(label: AureaText.t("panel_voltar_ferramentas"), action: onBack) {
                MaterialGlyph("rounded.ChevronLeft", size: PanelMetrics.railIcon).mirrorsInRtl()
            }
            cell(label: keyframeLook == .keyHere
                    ? AureaText.t("panel_tirar_keyframe_daqui")
                    : AureaText.t("panel_marcar_keyframe_aqui"),
                 action: onKeyframe) {
                KeyframeDiamondIcon(look: keyframeLook, enabled: onKeyframe != nil)
                    .scaleEffect(PanelMetrics.railIcon / AureaDims.diamondIcon)
            }
            cell(label: AureaText.t("panel_editar_curva_propriedade"), action: onCurve) {
                CurveRailIcon(enabled: onCurve != nil, animated: curveAnimated)
                    .scaleEffect(PanelMetrics.railIcon / AureaDims.curveIcon)
            }
            if let onExpression {
                cell(label: AureaText.t("panel_adicionar_expressao"), action: onExpression) {
                    Text("=").font(.aurea(size: PanelMetrics.railExpression, weight: .bold))
                        .foregroundStyle(expression == .none ? AureaColors.text : AureaColors.accent)
                }
            }
            Spacer(minLength: 0)
            if let onMore {
                RailMoreButton(active: false, onClick: onMore)
                    .frame(width: LeftRailLayout.cell, height: LeftRailLayout.cell)
            }
        }
        .padding(.top, LeftRailLayout.padTop)
        .padding(.bottom, LeftRailLayout.padBottom)
        .frame(width: LeftRailLayout.width)
        .frame(maxHeight: .infinity)
        .overlay(alignment: .trailing) {
            Rectangle()
                .fill(ParamRowColors.railLine)
                .frame(width: 1)
        }
    }

    @ViewBuilder
    private func cell<Content: View>(label: String, action: (() -> Void)?,
                                     @ViewBuilder content: () -> Content) -> some View {
        let target = content()
            .frame(width: LeftRailLayout.cell, height: LeftRailLayout.cell)
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

/// Medidas do trilho esquerdo (`LeftRail` do PanelChrome.kt, redesenho 2026-09-29).
private enum LeftRailLayout {
    static let width: CGFloat = 58
    static let cell: CGFloat = 44
    static let gap: CGFloat = 6
    static let padTop: CGFloat = 8
    static let padBottom: CGFloat = 4
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
            CupertinoGlyph.text(CupertinoGlyph.Ellipsis, size: PanelMetrics.railIcon,
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
        let side = min(24, cell * 0.6)  // 0.3.4 painéis: 24 (par do RightRail do Android)
        if mode.hasPrefix("material:") {
            MaterialGlyph(String(mode.dropFirst(9)), size: side, color: tint)
        } else if mode.count == 1, let glyph = mode.first {
            CupertinoGlyph.text(glyph, size: side, color: tint)
        } else {
            Image(systemName: mode)
                .font(.aurea(size: side, weight: AureaIconWeight.symbol))
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
                        .font(.aurea(size: PanelMetrics.paramTabsText, weight: on ? .bold : .medium))
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
                .frame(height: PanelMetrics.paramTabsHeight - 2 * AureaDims.paramTabPadV)
                .background(on ? AureaColors.accentDim : AureaColors.chip,
                            in: RoundedRectangle(cornerRadius: AureaDims.paramTabRadius))
                .padding(.horizontal, AureaDims.paramTabGap)
                .contentShape(Rectangle())
                .aureaTappable(shrink: 1) { onSelect(index) }
            }
        }
        .padding(.horizontal, AureaDims.paramTabPadH)
        .padding(.vertical, AureaDims.paramTabPadV)
        .frame(height: PanelMetrics.paramTabsHeight)
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

// =============================================================================
// Árabe (RTL): o que espelha e o que NÃO espelha — par do `ui/i18n/Ltr.kt`
// =============================================================================

/// O locale da interface a partir do idioma ESCOLHIDO no app (não o do sistema).
///
/// Árabe usa algarismos ocidentais (0-9), não árabe-índicos (٠-٩): decisão de
/// produto, a mesma do `AppLanguage.westernDigits` do Android — timecode,
/// régua, valores de painel e coordenadas são sempre 0-9.
extension AureaLanguage {
    var uiLocale: Locale {
        let idioma = resolved
        if idioma == .ar { return Locale(identifier: "ar@numbers=latn") }
        return Locale(identifier: idioma.rawValue)
    }
    /// Árabe corre da direita para a esquerda; os outros idiomas do app, não.
    var layoutDirection: LayoutDirection { resolved == .ar ? .rightToLeft : .leftToRight }
}

extension View {
    /// Força LTR de LAYOUT (HStack, leading/trailing, alinhamentos) neste ramo,
    /// mesmo com o app em árabe. Vale para as superfícies onde o eixo X É o
    /// dado: timeline, régua, playhead, curvas e gráficos, palco/preview,
    /// gizmos, transporte, barras de tempo e teclado numérico. O tempo corre
    /// para a direita em qualquer idioma — senão o quadro 0 iria para a direita
    /// e arrastar para "avançar" andaria para trás. Par do `KeepLtr` do Kotlin.
    ///
    /// Dentro deste ramo `mirrorsInRtl()` não espelha nada (o ambiente é LTR).
    func keepLtr() -> some View {
        environment(\.layoutDirection, .leftToRight)
    }

    /// Espelha o ícone na horizontal quando o layout é RTL (voltar, chevrons de
    /// lista, desfazer/refazer fora do transporte). Não usar em play/pausa,
    /// controles de mídia, timeline nem logotipos. Num ramo `keepLtr()` não faz
    /// nada — é o que mantém coerentes os ícones do transporte e da timeline.
    func mirrorsInRtl(_ on: Bool = true) -> some View {
        flipsForRightToLeftLayoutDirection(on)
    }
}

/// Um trecho LTR dentro de texto que pode correr em RTL (par do `ltr()` do
/// Kotlin). Timecode, coordenada, número com unidade ou nome de arquivo colados
/// numa frase árabe são reordenados pelo algoritmo bidirecional ("00:02:03"
/// pode virar "03:02:00"); o isolamento U+2066 (LRI) … U+2069 (PDI) prende o
/// trecho no sentido dele. Em idiomas LTR não muda nada visível.
func ltrIsolado(_ texto: String) -> String {
    "\u{2066}" + texto + "\u{2069}"
}

extension CupertinoGlyph {
    /// Glifos com sentido (apontam para "trás"/"frente" da leitura) que
    /// espelham em RTL. Fora: play/pausa, início/fim do transporte e as setas
    /// de alinhar/aparar (são posição na tela ou no tempo, não leitura).
    static let espelhaEmRtl: Set<Character> = [
        ChevronLeft, ChevronRight, ChevronBack, ArrowtriangleRightFill,
        ArrowUturnLeft, ArrowUturnRight, DeleteLeft,
    ]
    static func mirrorsInRtl(_ glyph: Character) -> Bool { espelhaEmRtl.contains(glyph) }
}

/// Tracking (espaço entre letras) dos estilos do Aurea — ZERO em árabe. A
/// escrita árabe é cursiva: espaçar as letras quebra a ligação entre elas, e a
/// tipografia árabe não usa tracking. Par do `aureaTracking` do Kotlin
/// (AureaTokens.kt). O logotipo "aurea" (latino) continua com o dele.
func aureaTracking(_ value: CGFloat) -> CGFloat {
    AureaText.language.resolved == .ar ? 0 : value
}

// =============================================================================
// Omega fundação · F3: CAIXA SUSPENSA (a da folha "Novo projeto" da referência)
//
// Caixa de 180 × 45, raio 8, valor à esquerda e ⌄ à direita; o toque abre o
// menu ancorado (AnchoredMenu.swift) com a LARGURA DA CAIXA, a opção vigente
// marcada com a barra de 4. Par do `AureaDropdown` do `ui/ds/Dropdown.kt`.
//
//   AureaDropdown(value: "1080p", tag: "newProject.resolution",
//                 accessibilityLabel: AureaText.t("editor_resolucao")) {
//       sizes.map { s in .item(s.label, selected: s == current) { pick(s) } }
//   }
// =============================================================================

/// 0.3.4: abre o menu da caixa. Se o toque chegou com o estado ainda "aberto"
/// (o menu cobre a tela inteira, então um toque na caixa com o menu aberto de
/// verdade não acontece), o estado ficou para trás: fecha e abre de novo no
/// próximo giro, para a caixa nunca ficar morta.
func aureaReopenMenu(_ open: Binding<Bool>) {
    guard open.wrappedValue else { open.wrappedValue = true; return }
    open.wrappedValue = false
    DispatchQueue.main.async { open.wrappedValue = true }
}

enum AureaDropdownMetrics {
    static let width: CGFloat = 180
    static let height: CGFloat = 45
    static let radius: CGFloat = 8
}

/// Caixa suspensa. `width` nil = a largura que o pai der. `tone`: `.light`
/// (página clara) ou `.editor` (painel escuro do editor). `swatch`: amostra de
/// cor antes do valor (`.clear` = xadrez, fundo transparente). `tag` = id da
/// caixa. Bug 6b (0.3.5): o menu é o nativo (`Menu`); cada linha leva o `tag` dela.
struct AureaDropdown: View {
    private let value: String
    private let width: CGFloat?
    private let tone: AureaSurfaceTone
    private let swatch: Color?
    private let enabled: Bool
    private let tag: String?
    private let label: String?
    private let entries: () -> [MenuEntry]

    init(value: String, width: CGFloat? = AureaDropdownMetrics.width, tone: AureaSurfaceTone = .light,
         swatch: Color? = nil, enabled: Bool = true, tag: String? = nil, accessibilityLabel: String? = nil,
         entries: @escaping () -> [MenuEntry]) {
        self.value = value; self.width = width; self.tone = tone; self.swatch = swatch
        self.enabled = enabled; self.tag = tag; self.label = accessibilityLabel; self.entries = entries
    }

    var body: some View {
        let light = tone == .light
        let box = light ? AureaColors.lightSurface : AureaColors.chip
        let ink = light ? AureaColors.lightText : AureaColors.text
        let muted = light ? AureaColors.lightTextMuted : AureaColors.muted
        let line = light ? AureaColors.lightDivider : AureaColors.border
        // Bug 6b (0.3.5): a caixa abre o MENU NATIVO do SwiftUI (`Menu`), com a caixa
        // igual. O menu ancorado numa janela própria (AnchoredMenu.swift) não abria
        // para os testadores dentro das folhas (Novo projeto, Ajustes do projeto) nem
        // depois da correção da 0.3.4; o `Menu` do sistema abre por cima de qualquer
        // folha, dentro de ScrollView, e grava a opção pela ação da própria linha.
        Menu {
            AureaDropdownMenuItems(entries: entries())
        } label: {
            HStack(spacing: 0) {
                if let swatch {
                    AureaColorSwatch(color: swatch).clipShape(RoundedRectangle(cornerRadius: 5)).frame(width: 22, height: 22)
                        .overlay(RoundedRectangle(cornerRadius: 5).stroke(line, lineWidth: 1)).padding(.trailing, 10)
                }
                Text(value).font(.aurea(size: 15, weight: .medium)).foregroundStyle(ink).lineLimit(1)
                    .frame(maxWidth: .infinity, alignment: .leading)
                CupertinoGlyph.text(CupertinoGlyph.ChevronDown, size: 14, color: muted).padding(.leading, 6)
            }
            .padding(.horizontal, 12)
            .frame(maxWidth: .infinity)
            .frame(height: AureaDropdownMetrics.height)
            .background(box, in: RoundedRectangle(cornerRadius: AureaDropdownMetrics.radius))
            .contentShape(Rectangle())
        }
        // A ordem das linhas é a da lista, também quando o menu abre para cima.
        .menuOrder(.fixed)
        .disabled(!enabled)
        .opacity(enabled ? 1 : AnchoredMenuMetrics.disabledAlpha)
        .frame(width: width)
        .accessibilityLabel(label ?? value)
        .accessibilityValue(value)
        .modifier(OptionalIdentifier(tag: tag))
    }
}

/// Bug 6b (0.3.5): as linhas do `MenuEntry` no menu nativo da caixa suspensa.
/// A opção vigente (`selected`/`checked`) sai marcada pelo próprio sistema (um
/// `Toggle` ligado no menu); tocar numa linha chama a ação dela (`onClick`), que
/// grava o valor no estado da tela (a folha mostra o novo valor na caixa).
/// Subpágina = submenu; título = seção; fileira de ícones = uma linha por célula.
struct AureaDropdownMenuItems: View {
    let entries: [MenuEntry]

    var body: some View {
        ForEach(Array(entries.enumerated()), id: \.offset) { _, entry in
            switch entry {
            case .row(let item):
                AureaDropdownMenuRow(item: item)
            case .iconRow(let cells, let selectedIndex, _, _):
                ForEach(Array(cells.enumerated()), id: \.offset) { index, cell in
                    Toggle(isOn: Binding(get: { index == selectedIndex }, set: { _ in cell.onClick() })) { Text(cell.label) }
                        .modifier(OptionalIdentifier(tag: cell.tag))
                }
            case .heading(let title, _):
                Text(title)
            case .divider:
                Divider()
            }
        }
    }
}

private struct AureaDropdownMenuRow: View {
    let item: MenuItem

    var body: some View {
        if let submenu = item.submenu {
            Menu { AureaDropdownMenuItems(entries: submenu) } label: { Text(item.label) }
                .disabled(!item.enabled)
        } else {
            Toggle(isOn: Binding(get: { item.selected || item.checked }, set: { _ in item.onClick() })) {
                if let sublabel = item.sublabel {
                    Text(item.label)
                    Text(sublabel)
                } else {
                    Text(item.label)
                }
            }
            .disabled(!item.enabled)
            .modifier(OptionalIdentifier(tag: item.tag))
        }
    }
}

// =============================================================================
// Omega fundação · F5: MOLDURA COMUM DOS PAINÉIS DE PROPRIEDADE
// (`fragment_settings_base` + `fragment_setting_actionbar` da referência)
//
// Par de `PanelFrame`/`PanelBar` do `editor/panels/PanelChrome.kt` (mesmos ids):
// coluna esquerda de 60 com 4 células que repartem a altura (‹ · ◆± · Curva ·
// ⋯, e o "=" do AUREA entre Curva e ⋯ quando há), conteúdo no centro rolando por
// dentro, coluna direita com até 4 abas de ícone (só com 2 ou mais), divisórias
// de 0,7, altura de 200 (dada por quem hospeda). Segurar ◆ com keyframe no
// quadro pede "apagar todos os keyframes"; Curva com menos de 2 keyframes avisa.
//
//   PanelFrame(rail: PanelRail(onBack: close, keyframeLook: look, onKeyframe: toggle,
//                              onDeleteAllKeyframes: deleteAll, onCurve: openCurve, curveReady: ready),
//              tabs: [PanelTab(id: "move", label: …, glyph: …, alt: PanelTab(id: "pivot", label: …, glyph: …)),
//                     PanelTab(id: "scale", label: …, glyph: …)],
//              selectedTab: tab, onTab: { tab = $0 }) {
//       // conteúdo da aba
//   }
// =============================================================================

enum PanelFrameMetrics {
    static let height: CGFloat = 200
    static let railWidth: CGFloat = PanelMetrics.railWidth
    static let divider: CGFloat = 0.7
    static let tabInset: CGFloat = PanelMetrics.tabInset
    static let tabRadius: CGFloat = PanelMetrics.tabRadius
    static let disabledAlpha: Double = 0.15
    static let maxTabs = 4
}

/// 0.3.4 painéis: AS MEDIDAS CENTRAIS dos painéis de propriedade (moldura e
/// campos), ~25 % maiores que as da Omega ("está muito compacto"). Todo painel
/// (Transformar, Câmera, Cor e preenchimento, Borda e sombra, Mesclagem, Efeitos,
/// Velocidade/Som, Curva) lê daqui. Par do `PanelMetrics` do `PanelChrome.kt`:
/// os mesmos valores (pt = dp). Três campos de `fieldWidth` + o fio + `fieldGap`
/// cabem na coluna central de 360 (360 − 2 × 60 − 2 × 0,7 − 12 = 226); o número
/// encolhe antes de vazar.
enum PanelMetrics {
    // --- Moldura ---
    static let railWidth: CGFloat = 60
    /// Ícones das colunas (‹, ◆, curva, ⋯ e as abas): antes 22.
    static let railIcon: CGFloat = 26
    /// O "=" da expressão na coluna esquerda.
    static let railExpression: CGFloat = 26
    static let tabInset: CGFloat = 5
    static let tabRadius: CGFloat = 7
    /// Alvo de toque mínimo de qualquer controle do painel.
    static let touch: CGFloat = 44
    // --- Campos de valor (X/Y/Z, Largura/Altura, Inclinação, ângulos) ---
    static let fieldWidth: CGFloat = 72
    static let fieldHeight: CGFloat = 36
    static let fieldRadius: CGFloat = 7
    static let fieldGap: CGFloat = 8
    /// Número do campo (antes 10); encolhe até `fieldValueMin`.
    static let fieldValue: CGFloat = 15
    static let fieldValueMin: CGFloat = 9
    /// Rótulo embaixo do campo (antes 9).
    static let fieldLabel: CGFloat = 12
    static let fieldLabelGap: CGFloat = 3
    /// Respiro em cima da fileira de campos (antes 6/10).
    static let fieldRowTop: CGFloat = 12
    /// A fileira de campos inteira: respiro + campo + vão + rótulo (≈ 17).
    static let fieldBlock: CGFloat = 68
    // --- Textos ---
    /// Título de cartão/seção dentro do painel (antes 13–13,5).
    static let title: CGFloat = 15
    /// Rótulo de linha / título de controle (antes 10–11).
    static let label: CGFloat = 13
    /// Texto secundário e notas (antes 10–11,5).
    static let note: CGFloat = 12
    /// A dica do trackpad e de áreas de gesto (antes 12).
    static let hint: CGFloat = 14
    /// Botões pequenos dentro do painel ("Centro", "Preciso", chips).
    static let chipHeight: CGFloat = 36
    static let chipText: CGFloat = 14
    static let chipPad: CGFloat = 14
    // --- Réguas ---
    /// Régua de um par no painel de 200 (antes 48–52); 68 + 8 + 56 + 8 + 56 + 4 = 200.
    static let ruler: CGFloat = 56
    /// Régua única no painel de 200 (antes 56–72).
    static let rulerTall: CGFloat = 80
    /// Teto ao crescer com o painel alto.
    static let rulerMax: CGFloat = 128
    static let rulerGap: CGFloat = 8
    static let rulerBottom: CGFloat = 4
    // --- Abas de parâmetro (ParamTabs) ---
    static let paramTabsHeight: CGFloat = 54
    static let paramTabsText: CGFloat = 14

    /// A altura que sobra para as réguas abaixo de uma fileira de campos.
    static func rulerSpace(_ height: CGFloat) -> CGFloat { height - fieldBlock - rulerGap - rulerBottom }
    /// Altura de cada uma de `count` réguas para encher `available`, entre `min` e
    /// `rulerMax`: com o painel alto elas crescem; num painel baixo ficam em `min` e rola.
    static func fillRulers(_ available: CGFloat, count: Int, min minimum: CGFloat) -> CGFloat {
        guard count > 0, available.isFinite else { return minimum }
        let each = (available - rulerGap * CGFloat(count - 1)) / CGFloat(count)
        return Swift.min(Swift.max(each, minimum), Swift.max(minimum, rulerMax))
    }
}

/// 0.3.4 painéis: uma fileira de campos centrada; se não couber (telefone estreito,
/// 3D, fonte grande), rola de lado em vez de cortar. Par do `TfFieldRow(scroll = true)`.
struct PanelFieldRow<Content: View>: View {
    private let content: () -> Content
    init(@ViewBuilder content: @escaping () -> Content) { self.content = content }
    var body: some View {
        ViewThatFits(in: .horizontal) {
            content().frame(maxWidth: .infinity)
            ScrollView(.horizontal, showsIndicators: false) { content().padding(.horizontal, 6) }
        }
    }
}

/// Uma aba de ícone da coluna direita. `id` é o que `onTab` devolve. `alt`: a
/// aba irmã — tocar de novo na aba ativa troca para ela e o toque seguinte
/// volta. Ícone: `glyph` (CupertinoGlyph) ou `icon` (SF Symbol ou "material:Nome").
struct PanelTab {
    let id: String
    let label: String
    var glyph: Character? = nil
    var icon: String? = nil
    private let altBox: [PanelTab]
    var alt: PanelTab? { altBox.first }

    init(id: String, label: String, glyph: Character? = nil, icon: String? = nil, alt: PanelTab? = nil) {
        self.id = id; self.label = label; self.glyph = glyph; self.icon = icon
        self.altBox = alt.map { [$0] } ?? []
    }
}

/// O que a coluna esquerda faz (par do `PanelRail` do Kotlin).
/// - `onKeyframe` nil = o painel não tem parâmetro com keyframe: ◆ e Curva a 15 %, sem toque.
/// - `onDeleteAllKeyframes`: segurar ◆ com keyframe no quadro pede confirmação e chama isto.
/// - `onCurve` + `curveReady`: com menos de 2 keyframes o toque mostra o aviso.
/// - `onExpression`: a célula "=" (extra do AUREA), só quando não nil.
/// - `moreEntries` (menu ancorado) ou `onMore`: o ⋯; os dois nil = sem ⋯.
/// - `supportsAnimation` falso: painel sem animação — só o ‹.
struct PanelRail {
    let onBack: () -> Void
    var keyframeLook: KeyframeLook = .none
    var onKeyframe: (() -> Void)? = nil
    var onDeleteAllKeyframes: (() -> Void)? = nil
    var onCurve: (() -> Void)? = nil
    var curveReady: Bool = false
    var expression: ExpressionLook = .none
    var onExpression: (() -> Void)? = nil
    var moreEntries: (() -> [MenuEntry])? = nil
    var onMore: (() -> Void)? = nil
    var moreActive: Bool = false
    var supportsAnimation: Bool = true
    // Omega efeitos (área 07): o ⋯ a 15 % e sem toque (o ⋯ da lista de efeitos
    // fica inativo com os controles de um efeito abertos, como na referência).
    var moreEnabled: Bool = true
}

/// A moldura comum dos painéis de propriedade.
struct PanelFrame<Content: View>: View {
    private let rail: PanelRail
    private let tabs: [PanelTab]
    private let selectedTab: String?
    private let onTab: (String) -> Void
    private let scrollContent: Bool
    private let content: () -> Content
    @State private var moreOpen = false

    /// - `tabs`: no máximo 4; com 0 ou 1 a coluna direita não existe.
    /// - `selectedTab`: o `PanelTab.id` ativo (ou o id da `alt` dele).
    /// - `scrollContent`: o conteúdo rola por dentro (padrão); falso para listas próprias.
    init(rail: PanelRail, tabs: [PanelTab] = [], selectedTab: String? = nil, onTab: @escaping (String) -> Void = { _ in },
         scrollContent: Bool = true, @ViewBuilder content: @escaping () -> Content) {
        self.rail = rail; self.tabs = tabs; self.selectedTab = selectedTab; self.onTab = onTab
        self.scrollContent = scrollContent; self.content = content
    }

    var body: some View {
        HStack(spacing: 0) {
            leftRail
            divider
            Group {
                if scrollContent {
                    ScrollView(.vertical) { content().frame(maxWidth: .infinity, alignment: .topLeading) }
                } else {
                    content()
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
            .background(AureaColors.panelFrameContent)
            .accessibilityElement(children: .contain)
            .accessibilityIdentifier("panel.content")
            if tabs.count >= 2 {
                divider
                rightRail
            }
        }
        .background(AureaColors.panelFrameBackground)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("panel.frame")
    }

    private var divider: some View {
        Rectangle().fill(AureaColors.panelFrameDivider).frame(width: PanelFrameMetrics.divider).frame(maxHeight: .infinity)
    }

    private var leftRail: some View {
        VStack(spacing: 0) {
            cell("panel.rail.back", label: AureaText.t("panel_voltar_ferramentas"), enabled: true, action: rail.onBack) {
                CupertinoGlyph.text(CupertinoGlyph.ChevronLeft, size: PanelMetrics.railIcon).mirrorsInRtl()
            }
            if rail.supportsAnimation {
                cell("panel.rail.keyframe",
                     label: AureaText.t(rail.keyframeLook == .keyHere ? "panel_tirar_keyframe_daqui" : "panel_marcar_keyframe_aqui"),
                     enabled: rail.onKeyframe != nil, action: rail.onKeyframe,
                     longAction: rail.keyframeLook == .keyHere && rail.onDeleteAllKeyframes != nil ? askDeleteAll : nil) {
                    // 0.3.4 painéis: os ícones pintados no tamanho da coluna (26).
                    KeyframeDiamondIcon(look: rail.keyframeLook, enabled: true)
                        .scaleEffect(PanelMetrics.railIcon / AureaDims.diamondIcon)
                }
                cell("panel.rail.curve", label: AureaText.t("panel_editar_curva_propriedade"), enabled: rail.onCurve != nil,
                     action: rail.onCurve.map { openCurve in { if rail.curveReady { openCurve() } else { curveWarning() } } }) {
                    CurveRailIcon(enabled: true, animated: rail.keyframeLook != .none)
                        .scaleEffect(PanelMetrics.railIcon / AureaDims.curveIcon)
                }
                if let onExpression = rail.onExpression {
                    cell("panel.rail.expression",
                         label: AureaText.t(rail.expression == .none ? "panel_adicionar_expressao" : "panel_editar_expressao"),
                         enabled: true, action: onExpression) {
                        Text("=").font(.aurea(size: PanelMetrics.railExpression, weight: .bold))
                            .foregroundStyle(rail.expression == .none ? AureaColors.text : expressionColor(rail.expression))
                    }
                }
                if let entries = rail.moreEntries {
                    cell("panel.rail.more", label: AureaText.t("editor_mais"), enabled: rail.moreEnabled, action: { moreOpen = true }) {
                        CupertinoGlyph.text(CupertinoGlyph.Ellipsis, size: PanelMetrics.railIcon, color: rail.moreActive ? AureaColors.accent : AureaColors.text)
                    }
                    .aureaAnchoredMenu(isPresented: $moreOpen, tag: "panel.rail.more.menu", entries: entries)
                } else if let onMore = rail.onMore {
                    cell("panel.rail.more", label: AureaText.t("editor_mais"), enabled: rail.moreEnabled, action: onMore) {
                        CupertinoGlyph.text(CupertinoGlyph.Ellipsis, size: PanelMetrics.railIcon, color: rail.moreActive ? AureaColors.accent : AureaColors.text)
                    }
                } else {
                    Color.clear.frame(maxHeight: .infinity)
                }
            } else {
                Color.clear.frame(maxHeight: .infinity)
                Color.clear.frame(maxHeight: .infinity)
                Color.clear.frame(maxHeight: .infinity)
            }
        }
        .frame(width: PanelFrameMetrics.railWidth)
        .frame(maxHeight: .infinity)
    }

    /// Uma célula da coluna esquerda: reparte a altura com as outras.
    @ViewBuilder
    private func cell<Icon: View>(_ id: String, label: String, enabled: Bool, action: (() -> Void)?,
                                  longAction: (() -> Void)? = nil, @ViewBuilder icon: () -> Icon) -> some View {
        let face = icon()
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .contentShape(Rectangle())
            .opacity(enabled ? 1 : PanelFrameMetrics.disabledAlpha)
        if let action, enabled {
            if let longAction {
                // ◆: toque marca/tira; segurar pede "apagar todos".
                face
                    .onTapGesture(perform: action)
                    .onLongPressGesture(minimumDuration: 0.5, perform: longAction)
                    .accessibilityElement(children: .ignore)
                    .accessibilityLabel(label)
                    .accessibilityAddTraits(.isButton)
                    .accessibilityAction(.default, action)
                    .accessibilityAction(named: AureaText.t("om_panel_delete_all_keyframes_q"), longAction)
                    .accessibilityIdentifier(id)
            } else {
                Button(action: action) { face }
                    .buttonStyle(AureaPressStyle(shrink: 1))
                    .accessibilityLabel(label)
                    .accessibilityIdentifier(id)
            }
        } else {
            face
                .accessibilityElement(children: .ignore)
                .accessibilityLabel(label)
                .accessibilityAddTraits(.isButton)
                .disabled(true)
                .accessibilityIdentifier(id)
        }
    }

    private var rightRail: some View {
        VStack(spacing: 0) {
            ForEach(Array(tabs.prefix(PanelFrameMetrics.maxTabs).enumerated()), id: \.offset) { index, tab in
                let altOn = tab.alt.map { selectedTab == $0.id } ?? false
                let on = selectedTab == tab.id || altOn
                let shown = altOn ? (tab.alt ?? tab) : tab
                let tint = on ? AureaColors.accent : AureaColors.text
                Button {
                    if let alt = tab.alt, selectedTab == tab.id { onTab(alt.id) } else { onTab(tab.id) }
                } label: {
                    tabIcon(shown, tint: tint)
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                        .background(on ? AureaColors.panelFrameDivider : Color.clear,
                                    in: RoundedRectangle(cornerRadius: PanelFrameMetrics.tabRadius))
                        .padding(PanelFrameMetrics.tabInset)
                        .contentShape(Rectangle())
                }
                .buttonStyle(AureaPressStyle(shrink: 1))
                .accessibilityLabel(shown.label)
                .accessibilityAddTraits(on ? .isSelected : [])
                .accessibilityIdentifier("panel.tab.\(index)")
            }
        }
        .frame(width: PanelFrameMetrics.railWidth)
        .frame(maxHeight: .infinity)
    }

    @ViewBuilder
    private func tabIcon(_ tab: PanelTab, tint: Color) -> some View {
        // 0.3.4 painéis: abas de ícone de 26 (antes 22).
        if let glyph = tab.glyph {
            CupertinoGlyph.text(glyph, size: PanelMetrics.railIcon, color: tint)
        } else if let icon = tab.icon, icon.hasPrefix("material:") {
            MaterialGlyph(String(icon.dropFirst(9)), size: PanelMetrics.railIcon, color: tint)
        } else if let icon = tab.icon {
            Image(systemName: icon).font(.aurea(size: PanelMetrics.railIcon - 2, weight: AureaIconWeight.symbol)).foregroundStyle(tint)
        }
    }

    private func askDeleteAll() {
        guard let delete = rail.onDeleteAllKeyframes else { return }
        AureaPopupPresenter.shared.presentDialog { close in
            AureaAlert(title: AureaText.t("om_panel_delete_all_keyframes_q"), confirmLabel: AureaText.t("panel_apagar"),
                       destructive: true, onConfirm: delete, onDismiss: close)
        }
    }

    private func curveWarning() {
        AureaPopupPresenter.shared.presentDialog { close in
            AureaAlert(title: AureaText.t("om_panel_keyframes_needed"), message: AureaText.t("pn_curve_need_two_keys"),
                       showCancel: false, onConfirm: {}, onDismiss: close)
        }
    }
}

/// A BARRA DO PAINEL DE PROPRIEDADE (P0-6 da área 04 / P0-6 da área 06): fundo
/// `panelBar`, ‹ de 44 e o título À ESQUERDA, com reticências; `trailing` à
/// direita e um ⋮ opcional (`moreEntries`, menu ancorado). Altura = a da barra
/// superior (42). Ids: `panel.bar.back`, `panel.bar.title`, `panel.bar.more`.
struct PanelBar<Trailing: View>: View {
    private let title: String
    private let onBack: () -> Void
    private let tag: String?
    private let moreEntries: (() -> [MenuEntry])?
    private let trailing: () -> Trailing
    @State private var moreOpen = false

    init(title: String, onBack: @escaping () -> Void, tag: String? = nil, moreEntries: (() -> [MenuEntry])? = nil,
         @ViewBuilder trailing: @escaping () -> Trailing) {
        self.title = title; self.onBack = onBack; self.tag = tag; self.moreEntries = moreEntries; self.trailing = trailing
    }

    var body: some View {
        HStack(spacing: 0) {
            Button(action: onBack) {
                CupertinoGlyph.text(CupertinoGlyph.ChevronLeft, size: 22).mirrorsInRtl()
                    .frame(width: 44, height: 44).contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle(shrink: 1))
            .accessibilityLabel(AureaText.t("pn_back_to_layer_tools"))
            .accessibilityIdentifier("panel.bar.back")
            Text(title)
                .font(.aurea(size: 15, weight: .semibold))
                .foregroundStyle(AureaColors.text)
                .lineLimit(1).truncationMode(.tail)
                .frame(maxWidth: .infinity, alignment: .leading)
                .accessibilityAddTraits(.isHeader)
                .accessibilityIdentifier("panel.bar.title")
            trailing()
            if let moreEntries {
                Button { moreOpen = true } label: {
                    CupertinoGlyph.text(CupertinoGlyph.Ellipsis, size: 22)
                        .frame(width: 44, height: 44).contentShape(Rectangle())
                }
                .buttonStyle(AureaPressStyle(shrink: 1))
                .accessibilityLabel(AureaText.t("editor_mais"))
                .accessibilityIdentifier("panel.bar.more")
                .aureaAnchoredMenu(isPresented: $moreOpen, alignEnd: true, tag: "panel.bar.more.menu", entries: moreEntries)
            } else {
                Spacer().frame(width: 6)
            }
        }
        .frame(height: EditorLayout.topBar)
        .frame(maxWidth: .infinity)
        .background(AureaColors.panelBar)
        .accessibilityElement(children: .contain)
        .modifier(OptionalIdentifier(tag: tag))
    }
}

extension PanelBar where Trailing == EmptyView {
    init(title: String, onBack: @escaping () -> Void, tag: String? = nil, moreEntries: (() -> [MenuEntry])? = nil) {
        self.init(title: title, onBack: onBack, tag: tag, moreEntries: moreEntries) { EmptyView() }
    }
}

// =============================================================================
// Omega efeitos (área 07): CONTROLES DE PARÂMETRO da referência
// (linha de ajuste, slider e caixa de valor) — par dos `FxParamLabel`,
// `FxValueBox`, `AureaSlider`, `FxRuler` e `FxSegmented` do PropertyControls.kt.
// Só o painel de efeitos usa; `PropertyRow`/`TickRuler`/`ParamRowLabel` ficam
// como estão para os outros painéis. A CONTA mora em `FxParamMath`
// (EffectsHuman.swift); as cores, nos papéis `AureaColors.fx*`.
// =============================================================================

/// Losango 9×8 do rótulo (só com keyframe): cheio = marca no cabeçote.
private struct FxKeyIndicator: View {
    let filled: Bool
    let color: Color
    var body: some View {
        Canvas { context, size in
            var diamond = Path()
            diamond.move(to: CGPoint(x: size.width / 2, y: 0.5))
            diamond.addLine(to: CGPoint(x: size.width - 0.5, y: size.height / 2))
            diamond.addLine(to: CGPoint(x: size.width / 2, y: size.height - 0.5))
            diamond.addLine(to: CGPoint(x: 0.5, y: size.height / 2))
            diamond.closeSubpath()
            if filled { context.fill(diamond, with: .color(color)) }
            else { context.stroke(diamond, with: .color(color), lineWidth: 1) }
        }
        .accessibilityHidden(true)
    }
}

/// O RÓTULO da linha (`propertyLabel`): 75 × (alt − 6), raio 4, fundo `fxLabel`
/// (pressionado ou ativo: `fxSunken`), texto 8–12 em até 2 linhas, centralizado:
/// `text` normal, `accent` ativo, `fxDisabledText` desabilitado (sem toque). ◆ 9×8
/// no canto superior esquerdo SÓ com keyframe; o "=" da expressão (extra do AUREA).
struct FxParamLabel: View {
    let label: String
    var height: CGFloat = FxParamMath.ROW_RULER - 12
    var width: CGFloat = FxParamMath.LABEL_W
    var active = false
    var enabled = true
    var keyframe: KeyframeLook = .none
    var expression: ExpressionLook = .none
    var onTap: (() -> Void)? = nil
    var onLongPress: (() -> Void)? = nil
    @State private var pressed = false

    var body: some View {
        let lit = enabled && (active || pressed)
        let ink: Color = !enabled ? AureaColors.fxDisabledText : active ? AureaColors.accent : AureaColors.text
        // 0.3.4 painéis: até 13 (antes 12), par do `FxRow.LabelText` do Android.
        return Text(label)
            .font(.aurea(size: PanelMetrics.label))
            .minimumScaleFactor(8.0 / PanelMetrics.label)
            .lineLimit(2)
            .multilineTextAlignment(.center)
            .foregroundStyle(ink)
            .padding(3)
            .frame(width: width, height: height)
            .background(lit ? AureaColors.fxSunken : AureaColors.fxLabel, in: RoundedRectangle(cornerRadius: 4))
            .overlay(alignment: .topLeading) {
                if keyframe != .none {
                    FxKeyIndicator(filled: keyframe == .keyHere,
                                   color: active && enabled ? AureaColors.accent : AureaColors.text.opacity(0.5))
                        .frame(width: 9, height: 8).offset(x: -1, y: -1)
                }
            }
            .overlay(alignment: .topTrailing) {
                if expression != .none { ExpressionBadge(look: expression).offset(x: 1, y: -2) }
            }
            // A linha inteira responde (38 de altura nas linhas baixas).
            .contentShape(Rectangle().inset(by: -FxParamMath.LABEL_MARGIN))
            .onTapGesture { if enabled { onTap?() } }
            .onLongPressGesture(minimumDuration: 0.5, maximumDistance: 10, perform: {
                if enabled { onLongPress?() }
            }, onPressingChanged: { down in pressed = down && enabled && onTap != nil })
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(label)
            .accessibilityAddTraits(.isButton)
            .accessibilityAddTraits(active ? .isSelected : [])
            .accessibilityAction { if enabled { onTap?() } }
    }
}

/// A CAIXA DE VALOR: 56 × (alt − 6), raio 4, véu preto de 20 %, número 8–14
/// alinhado ao fim, sempre LTR; `aux` é a linha auxiliar acima ("N× +" das voltas
/// de um ângulo, 8–10 em `fxDisabledText`). Tocar abre o teclado (quem chama).
struct FxValueBox: View {
    let text: String
    var height: CGFloat = FxParamMath.ROW_RULER - 12
    var width: CGFloat = FxParamMath.VALUE_W
    var aux: String? = nil
    var enabled = true
    var onTap: (() -> Void)? = nil
    @State private var pressed = false

    var body: some View {
        VStack(alignment: .trailing, spacing: 0) {
            if let aux, !aux.isEmpty {
                Text(aux)
                    .font(.aurea(size: PanelMetrics.note).monospacedDigit())
                    .minimumScaleFactor(0.8)
                    .lineLimit(1)
                    .foregroundStyle(AureaColors.fxDisabledText)
            }
            // 0.3.4 painéis: até 15 (antes 14), par do `FxRow.ValueText` do Android.
            Text(text)
                .font(.aurea(size: PanelMetrics.fieldValue).monospacedDigit())
                .minimumScaleFactor(8.0 / PanelMetrics.fieldValue)
                .lineLimit(1)
                .foregroundStyle(enabled ? AureaColors.text : AureaColors.fxDisabledText)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .trailing)
        .padding(.horizontal, 6)
        .frame(width: width, height: height)
        .background(pressed && enabled && onTap != nil ? AureaColors.fxSunken : AureaColors.fxValueVeil,
                    in: RoundedRectangle(cornerRadius: 4))
        .keepLtr()
        .contentShape(Rectangle())
        .onTapGesture { if enabled { onTap?() } }
        .onLongPressGesture(minimumDuration: 0.5, maximumDistance: 10, perform: {},
                            onPressingChanged: { down in pressed = down })
        .accessibilityElement(children: .ignore)
        .accessibilityLabel((aux.map { $0 + " " } ?? "") + text)
        .accessibilityAddTraits(onTap != nil && enabled ? .isButton : [])
        .accessibilityAction { if enabled { onTap?() } }
    }
}

/// O SLIDER da referência: trilho de 2,5 com ponta redonda em
/// `fxTrack`; preenchimento `accent` da ORIGEM até o polegar (bipolar quando a
/// origem está no meio; `fxTrackDisabled` desabilitado); polegar de 18 em `text`;
/// 10 pt de pista parada em cada valor de encaixe (o padrão). O toque parado não
/// muda nada; passou de ½ touch slop, o valor vai para a posição do dedo
/// (absoluto), quantizado em `step`. Um passo de desfazer por arrasto (quem chama
/// abre em `onBegin` e fecha em `onEnd`). Valores na unidade MOSTRADA.
struct AureaSlider: View {
    let value: Float
    let min: Float
    let max: Float
    var origin: Float? = nil
    var step: Float = 0
    var snaps: [Float] = []
    var enabled = true
    var identifier: String? = nil
    var accessibilityLabel = ""
    var accessibilityValue = ""
    let onBegin: () -> Void
    let onValue: (Float) -> Void
    let onEnd: () -> Void

    @State private var active = false
    @State private var live: Float = 0
    @State private var direction = 0
    @GestureState private var held = false

    private static let thumb: CGFloat = 18
    private var shown: Float { active ? live : value }
    private var snapPx: Float { Float(FxParamMath.SLIDER_SNAP_DP) }

    var body: some View {
        GeometryReader { geometry in
            let track = Swift.max(1, geometry.size.width - AureaSlider.thumb)
            Canvas { context, size in draw(context, size: size, track: track) }
                .contentShape(Rectangle())
                .simultaneousGesture(drag(track: track), including: enabled ? .all : .none)
        }
        .keepLtr()
        .onChange(of: held) { down in if !down { finish() } }
        .onChange(of: enabled) { on in if !on { finish() } }
        .onDisappear { finish() }
        .accessibilityElement()
        .accessibilityLabel(accessibilityLabel)
        .accessibilityValue(accessibilityValue)
        .accessibilityAdjustableAction { move in
            let unit: Float = step > 0 ? step : (max - min) / 20
            let delta: Float = move == .increment ? unit : move == .decrement ? -unit : 0
            guard enabled, delta != 0, unit.isFinite else { return }
            onBegin(); onValue(Swift.min(Swift.max(value + delta, min), max)); onEnd()
        }
        .modifier(OptionalIdentifier(tag: identifier))
    }

    private func x(_ v: Float, track: CGFloat) -> CGFloat {
        AureaSlider.thumb / 2 + CGFloat(FxParamMath.sliderX(value: v, width: Float(track), min: min, max: max,
                                                             snaps: snaps, snapPx: snapPx))
    }

    private func draw(_ context: GraphicsContext, size: CGSize, track: CGFloat) {
        let r = AureaSlider.thumb / 2
        let cy = size.height / 2
        let round = StrokeStyle(lineWidth: 2.5, lineCap: .round)
        var line = Path()
        line.move(to: CGPoint(x: r, y: cy))
        line.addLine(to: CGPoint(x: r + track, y: cy))
        context.stroke(line, with: .color(AureaColors.fxTrack), style: round)
        let o = origin.flatMap { $0.isFinite ? $0 : nil } ?? min
        let xo = x(Swift.min(Swift.max(o, min), max), track: track)
        let xv = x(shown, track: track)
        if abs(xv - xo) > 0.5 {
            var fill = Path()
            fill.move(to: CGPoint(x: xo, y: cy))
            fill.addLine(to: CGPoint(x: xv, y: cy))
            context.stroke(fill, with: .color(enabled ? AureaColors.accent : AureaColors.fxTrackDisabled), style: round)
        }
        context.fill(Path(ellipseIn: CGRect(x: xv - r, y: cy - r, width: 2 * r, height: 2 * r)),
                     with: .color(enabled ? AureaColors.text : AureaColors.fxDisabledText))
    }

    private func drag(track: CGFloat) -> some Gesture {
        DragGesture(minimumDistance: FxParamMath.TOUCH_SLOP * FxParamMath.SLIDER_SLOP_FRACTION)
            .updating($held) { _, state, _ in state = true }
            .onChanged { g in
                if direction == 0 { direction = abs(g.translation.width) >= abs(g.translation.height) ? 1 : -1 }
                guard direction == 1 else { return }
                if !active { live = value; active = true; onBegin() }
                let v = FxParamMath.sliderValueAt(x: Float(g.location.x - AureaSlider.thumb / 2), width: Float(track),
                                                  min: min, max: max, step: step, snaps: snaps, snapPx: snapPx)
                if v.isFinite && v != live { live = v; onValue(v) }
            }
            .onEnded { _ in finish() }
    }

    private func finish() {
        direction = 0
        if active { active = false; onEnd() }
    }
}

/// Uma régua por vez arremessa; enquanto isso, o toque numa outra é ignorado
/// (referência `K52.Rw`). Só a thread principal mexe aqui.
enum FxRulerFling {
    static var owner: UUID?
}

/// A RÉGUA da referência (`ValueSpinner`): risco a cada 5,5 pt com 0,5 de largura
/// em `fxTick`, a cada 5 riscos em `text`; alfa triangular até as bordas; padding
/// vertical de 8; agulha de 1,5 na altura toda (`needle`: `fxNeedle` ou a cor do
/// eixo ativo); riscos de encaixe (padrão e 0) com 1,25 px e alfa (α+1)/2.
/// 1 risco = `step` (unidade mostrada) e o valor anda de risco em risco
/// (`snapToTick`). O gesto começa depois de ¾ do touch slop (vertical acima do
/// slop é da rolagem); dedo para a esquerda aumenta; arremesso acima de 300 pt/s
/// com a desaceleração do `UIScrollView` (0,998/ms), parando na ponta da faixa;
/// prende na faixa (estendida até o valor de partida); vibração leve ao cruzar um
/// encaixe; dois dedos = 1/10 (controle fino do AUREA). Tocar durante o próprio
/// arremesso o para e continua o MESMO gesto; enquanto OUTRA régua arremessa, o
/// toque aqui é ignorado. Um passo de desfazer por gesto, arremesso incluído.
/// Mesmo comportamento do `FxRuler` do EffectsControls.kt.
struct FxRuler: View {
    let value: Float
    let step: Float
    let min: Float
    let max: Float
    var snaps: [Float] = []
    var needle: Color = AureaColors.fxNeedle
    var enabled = true
    var identifier: String? = nil
    var accessibilityLabel = ""
    var accessibilityValue = ""
    let onBegin: () -> Void
    let onValue: (Float) -> Void
    let onEnd: () -> Void

    @Environment(\.displayScale) private var displayScale
    @State private var token = UUID()
    /// Passo de desfazer aberto (arrastando ou arremessando): a fita mostra `live`.
    @State private var dragging = false
    @State private var live: Float = 0
    @State private var start: Float = 0
    @State private var walked: CGFloat = 0
    @State private var lastX: CGFloat = 0
    /// 0 = esperando o slop, 1 = arrastando, −1 = não é desta régua.
    @State private var phase = 0
    @State private var gain: CGFloat = 1
    /// Amostras do dedo: x = instante (s), y = deslocamento horizontal (pt).
    @State private var samples: [CGPoint] = []
    @State private var fling: Task<Void, Never>?
    @State private var haptic = UIImpactFeedbackGenerator(style: .light)
    @GestureState private var held = false
    /// Bugs 0.3.4 (bug 6a): o arrasto só começa no toque (distância 0) enquanto
    /// ESTA régua arremessa (tocar a para e continua o mesmo gesto). Parada, ele
    /// espera 4 pt como o `AureaSlider`: um arrasto de distância 0 é reconhecido no
    /// toque e, dentro do ScrollView do corpo, tirava a rolagem vertical do painel.
    /// Só muda entre gestos (armado ao arremessar, desarmado no fim do gesto).
    @State private var catchArmed = false
    private var dragStartDistance: CGFloat {
        catchArmed ? 0 : FxParamMath.TOUCH_SLOP * FxParamMath.SLIDER_SLOP_FRACTION
    }

    private var shown: Float { dragging ? live : value }
    private var tickStep: Float { step > 0 && step.isFinite ? step : 1 }
    private var spacing: CGFloat { FxParamMath.TICK_SPACING }
    private var snapsInRange: [Float] {
        let a = Swift.min(min, max), b = Swift.max(min, max)
        return snaps.filter { $0.isFinite && $0 >= a && $0 <= b }
    }

    var body: some View {
        Canvas { context, size in draw(context, size: size) }
            .contentShape(Rectangle())
            .simultaneousGesture(drag, including: enabled ? .all : .none)
            .keepLtr()
            .onChange(of: held) { down in
                // O fim normal passa pelo `onEnded` (que pode arremessar); aqui só o
                // cancelamento (a rolagem levou o toque), depois dele.
                if !down {
                    DispatchQueue.main.async {
                        phase = 0
                        if fling == nil { finish() }
                        catchArmed = fling != nil
                    }
                }
            }
            .onChange(of: enabled) { on in if !on { stopFling(); finish() } }
            .onAppear { FxTouchCounter.shared.install() }
            .onDisappear { stopFling(); finish() }
            .accessibilityElement()
            .accessibilityLabel(accessibilityLabel)
            .accessibilityValue(accessibilityValue)
            .accessibilityAdjustableAction { move in
                let unit = tickStep * Float(FxParamMath.MAJOR_EVERY)
                let delta: Float = move == .increment ? unit : move == .decrement ? -unit : 0
                guard enabled, delta != 0 else { return }
                let a = Swift.min(min, max, value), b = Swift.max(min, max, value)
                onBegin(); onValue(Swift.min(Swift.max(value + delta, a), b)); onEnd()
            }
            .modifier(OptionalIdentifier(tag: identifier))
    }

    private func draw(_ context: GraphicsContext, size: CGSize) {
        let top: CGFloat = 8
        let bottom = size.height - 8
        let center = size.width / 2
        guard bottom > top, center > 0 else { return }
        let v = Double(shown.isFinite ? shown : 0)
        let ticks = v / Double(tickStep)
        let whole = floor(ticks)
        let fraction = CGFloat(ticks - whole)
        let reach = Int(center / spacing) + 2
        for j in -reach...reach {
            let x = center + (CGFloat(j) - fraction) * spacing
            let alpha = 1 - abs(x - center) / center
            if alpha <= 0 { continue }
            let index = whole + Double(j)
            let major = index.truncatingRemainder(dividingBy: Double(FxParamMath.MAJOR_EVERY)) == 0
            var line = Path()
            line.move(to: CGPoint(x: x, y: top))
            line.addLine(to: CGPoint(x: x, y: bottom))
            context.stroke(line, with: .color((major ? AureaColors.text : AureaColors.fxTick).opacity(Double(alpha))), lineWidth: 0.5)
        }
        // Riscos de encaixe (padrão e zero): 1,25 px, opacidade reforçada.
        for s in snapsInRange {
            let x = center + CGFloat((Double(s) - v) / Double(tickStep)) * spacing
            let alpha = 1 - abs(x - center) / center
            if alpha <= 0 { continue }
            var line = Path()
            line.move(to: CGPoint(x: x, y: top))
            line.addLine(to: CGPoint(x: x, y: bottom))
            context.stroke(line, with: .color(AureaColors.text.opacity(Double((alpha + 1) / 2))),
                           lineWidth: 1.25 / Swift.max(displayScale, 1))
        }
        var needleLine = Path()
        needleLine.move(to: CGPoint(x: center, y: 0))
        needleLine.addLine(to: CGPoint(x: center, y: size.height))
        context.stroke(needleLine, with: .color(enabled ? needle : AureaColors.fxDisabledText), lineWidth: 1.5)
    }

    private var drag: some Gesture {
        DragGesture(minimumDistance: dragStartDistance)
            .updating($held) { _, state, _ in state = true }
            .onChanged { g in changed(g) }
            .onEnded { g in ended(g) }
    }

    private func changed(_ g: DragGesture.Value) {
        if phase < 0 { return }
        let dx = g.translation.width
        let dy = g.translation.height
        if phase == 0 {
            if dx == 0 && dy == 0 {
                // O dedo desceu: outra régua arremessando = este gesto é ignorado;
                // a própria arremessando = parar o arremesso já é gesto.
                if let owner = FxRulerFling.owner, owner != token { phase = -1; return }
                if let task = fling {
                    task.cancel()
                    fling = nil
                    if FxRulerFling.owner == token { FxRulerFling.owner = nil }
                    if dragging {
                        phase = 1
                        start = live
                        walked = 0
                        lastX = 0
                        samples = []
                    }
                }
                return
            }
            // Vertical (acima do slop) é da rolagem da lista.
            if abs(dy) > FxParamMath.TOUCH_SLOP && abs(dy) > abs(dx) { phase = -1; return }
            if abs(dx) <= FxParamMath.TOUCH_SLOP * FxParamMath.RULER_SLOP_FRACTION { return }
            if let owner = FxRulerFling.owner, owner != token { phase = -1; return }
            phase = 1
            lastX = dx
            walked = 0
            samples = []
            if !dragging {
                dragging = true
                live = value.isFinite ? value : 0
                haptic.prepare()
                onBegin()
            }
            start = live
            return
        }
        let delta = dx - lastX
        lastX = dx
        gain = FxTouchCounter.shared.count >= 2 ? CGFloat(FxParamRowMath.twoFingerGain) : 1
        samples.append(CGPoint(x: g.time.timeIntervalSinceReferenceDate, y: dx))
        if samples.count > 8 { samples.removeFirst(samples.count - 8) }
        if delta != 0 {
            walked -= delta * gain   // dedo para a esquerda aumenta
            apply(FxParamMath.rulerValueAfter(start: start, scrolledPx: Float(walked), spacingPx: Float(spacing), step: tickStep))
        }
    }

    private func ended(_ g: DragGesture.Value) {
        defer { phase = 0; catchArmed = fling != nil }
        guard phase == 1, dragging else { return }
        let speed = velocity(at: g.time.timeIntervalSinceReferenceDate)
        if abs(speed) > FxParamMath.FLING_MIN_PX_S { startFling(-speed * gain) } else { finish() }
    }

    /// Velocidade do dedo (pt/s) nos últimos 100 ms; parado antes de soltar = 0.
    private func velocity(at end: TimeInterval) -> CGFloat {
        let recent = samples.filter { end - Double($0.x) <= 0.1 }
        guard let first = recent.first, let last = recent.last, last.x > first.x, end - Double(last.x) < 0.05 else { return 0 }
        return (last.y - first.y) / (last.x - first.x)
    }

    /// O valor anda de risco em risco, preso na faixa estendida até a partida.
    private func apply(_ raw: Float) {
        let (lo, hi) = FxParamRowMath.dragBounds(min: min, max: max, from: start)
        let next = FxParamMath.limitRange(FxParamMath.snapToTick(raw, step: tickStep), min: lo, max: hi)
        guard next.isFinite, next != live else { return }
        if FxParamMath.crossedSnap(from: live, to: next, snaps: snapsInRange) { haptic.impactOccurred(intensity: 0.2) }
        live = next
        onValue(next)
    }

    /// Fecha o passo de desfazer (fim do arrasto ou do arremesso).
    private func finish() {
        guard dragging else { return }
        dragging = false
        onEnd()
    }

    private func startFling(_ speed: CGFloat) {
        FxRulerFling.owner = token
        catchArmed = true
        fling = Task { @MainActor in
            // UIScrollView.DecelerationRate.normal: a velocidade cai 0,2 % por ms.
            let rate = 0.998
            var velocity = Double(speed) / 1000
            var last = Date()
            while !Task.isCancelled {
                try? await Task.sleep(nanoseconds: 16_000_000)
                if Task.isCancelled { break }
                let now = Date()
                let dt = Swift.max(1, now.timeIntervalSince(last) * 1000)
                last = now
                let decay = pow(rate, dt)
                let move = CGFloat(velocity * (decay - 1) / log(rate))
                velocity *= decay
                walked += move
                let before = live
                apply(FxParamMath.rulerValueAfter(start: start, scrolledPx: Float(walked), spacingPx: Float(spacing), step: tickStep))
                // Chegou à ponta da faixa: para (limitRange).
                if live == before && move != 0 {
                    let (lo, hi) = FxParamRowMath.dragBounds(min: min, max: max, from: start)
                    if live <= lo || live >= hi { break }
                }
                if abs(velocity) < 0.02 { break }
            }
            if !Task.isCancelled {
                fling = nil
                if FxRulerFling.owner == token { FxRulerFling.owner = nil }
                finish()
                // Sem dedo na régua: volta a esperar os 4 pt (com dedo, o fim do gesto desarma).
                if phase == 0 { catchArmed = false }
            }
        }
    }

    /// Para o arremesso (sumiu, desabilitou) e fecha o passo dele.
    private func stopFling() {
        guard let task = fling else { return }
        task.cancel()
        fling = nil
        if FxRulerFling.owner == token { FxRulerFling.owner = nil }
        catchArmed = false
        finish()
    }
}


/// A ESCOLHA SEGMENTADA (`effect_setting_selector`, RADIO): grupo de raio 5 com
/// botões iguais de 27,5 em `fxSegment` (vigente `fxSegmentOn`), texto `text`
/// (vigente `accent`, desabilitado `fxDisabledText`), uma linha com reticências.
/// `selected` −1 = nenhum marcado (a "Variação"). Ids `<id>` e `<id>.<k>`.
struct FxSegmented: View {
    let options: [String]
    let selected: Int
    var enabled = true
    var identifier: String? = nil
    var label = ""
    let onSelect: (Int) -> Void

    var body: some View {
        HStack(spacing: 1) {
            ForEach(Array(options.enumerated()), id: \.offset) { k, option in
                let on = k == selected
                Button { onSelect(k) } label: {
                    // 0.3.4 painéis: 36 com texto 13 (antes 27,5 / 12), par do `FxRow.Segment`.
                    Text(option)
                        .font(.aurea(size: PanelMetrics.label))
                        .lineLimit(1)
                        .truncationMode(.tail)
                        .foregroundStyle(!enabled ? AureaColors.fxDisabledText : on ? AureaColors.accent : AureaColors.text)
                        .padding(.horizontal, 4)
                        .frame(maxWidth: .infinity)
                        .frame(height: PanelMetrics.fieldHeight)
                        .background(on ? AureaColors.fxSegmentOn : AureaColors.fxSegment)
                        .contentShape(Rectangle())
                }
                .buttonStyle(AureaPressStyle(shrink: 1))
                .disabled(!enabled)
                .accessibilityLabel(label.isEmpty ? option : label + " · " + option)
                .accessibilityAddTraits(on ? .isSelected : [])
                .modifier(OptionalIdentifier(tag: identifier.map { "\($0).\(k)" }))
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: 5))
        .accessibilityElement(children: .contain)
        .modifier(OptionalIdentifier(tag: identifier))
    }
}

/// A ESCOLHA SUSPENSA (estilo DROPDOWN): caixa de raio 5 em `fxSunken` com inset
/// 4, a opção vigente e ▾ no destaque; o toque abre o menu ancorado com as opções
/// (a vigente marcada). Ids `<tag>` (caixa), `<tag>.menu` e `<tag>.<k>`.
struct FxDropdownBox: View {
    let text: String
    let options: [String]
    let selected: Int
    var enabled = true
    var tag: String? = nil
    var label = ""
    let onPick: (Int) -> Void
    @State private var open = false

    var body: some View {
        let ink = enabled ? AureaColors.accent : AureaColors.fxDisabledText
        return Button { aureaReopenMenu($open) } label: {
            HStack(spacing: 6) {
                Text(text)
                    .font(.aurea(size: 14))
                    .foregroundStyle(ink)
                    .lineLimit(1)
                    .truncationMode(.tail)
                    .frame(maxWidth: .infinity, alignment: .leading)
                CupertinoGlyph.text(CupertinoGlyph.ChevronDown, size: 14, color: ink)
            }
            .padding(.horizontal, 10)
            .frame(maxWidth: .infinity)
            .frame(height: PanelMetrics.fieldHeight)  // 0.3.4 painéis: antes 30
            .background(AureaColors.fxSunken, in: RoundedRectangle(cornerRadius: 5))
            .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .disabled(!enabled)
        .aureaAnchoredMenu(isPresented: $open, matchAnchorWidth: true, tag: tag.map { "\($0).menu" }) {
            options.enumerated().map { (index, option) -> MenuEntry in
                .item(option, selected: index == selected, tag: tag.map { "\($0).\(index)" }) { onPick(index) }
            }
        }
        .accessibilityLabel(label.isEmpty ? text : label)
        .accessibilityValue(text)
        .modifier(OptionalIdentifier(tag: tag))
        .padding(4)
    }
}
