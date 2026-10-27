// =============================================================================
//  Aurea / platform / ios / app / AnchoredMenu.swift
//
//  Omega fundação · F2: MENU POPUP ANCORADO (o menu popup da referência).
//  Par do `ui/ds/AnchoredMenu.kt`: mesmos tipos de linha, medidas, cores e ids.
//
//  Todo menu da tela abre preso ao botão que o abriu: 250 pt (ou a largura da
//  âncora), raio 8, linhas de 40, no máximo 8 linhas visíveis (o resto rola, com
//  setas de rolagem), abaixo da âncora alinhado pela borda de INÍCIO e para cima
//  quando não cabe. NÃO é o `Menu` do sistema.
//
//  COMO FUNCIONA: a âncora mede o próprio retângulo na tela na hora de abrir
//  (uma sonda UIKit atrás dela, 0.3.4) e o menu é desenhado numa camada própria
//  por cima de TUDO —
//  uma janela transparente, o mesmo que o `Popup` do Compose faz no Android.
//  Assim ele não é cortado por ScrollView nem fica atrás de folhas e telas
//  cheias (exportação, Novo projeto).
//
//  USO:
//
//    @State private var open = false
//    Button { open = true } label: { … }
//        .aureaAnchoredMenu(isPresented: $open, tag: "layer.menu") {
//            [
//                .header("Camada"),
//                .item("Duplicar", glyph: CupertinoGlyph.PlusSquareOnSquare, tag: "layer.menu.duplicate") { … },
//                .item("Solo", checked: model.solo, keepOpen: true) { … },
//                .item("Colar estilo", submenu: [ … ]),
//                .divider,
//                .item("Excluir", destructive: true) { … },
//            ]
//        }
// =============================================================================
import SwiftUI
import UIKit

/// Tom das peças comuns da Omega (menu, caixa suspensa, diálogo). `.light` =
/// papéis claros (`AureaColors.light*`; escuros nos temas escuros), o padrão
/// fora do editor. `.editor` = tons do editor (variante escura da referência).
enum AureaSurfaceTone { case light, editor }

/// Uma célula da fileira de ícones: glifo do AUREA ou amostra de cor; `label` é
/// o rótulo de acessibilidade.
struct MenuIconCell {
    let label: String
    var glyph: Character? = nil
    var swatch: Color? = nil
    var tag: String? = nil
    let onClick: () -> Void
}

/// Uma linha de 40 pt (par do `MenuEntry.Item`): simples, com ícone (`glyph` ou
/// `swatch`), sub-rótulo, opção vigente (`selected`: barra de 4 no início),
/// marcada (`checked`: ✓ no fim), com "›" (`submenu` abre uma subpágina no
/// mesmo menu; `chevron` só desenha a seta), desabilitada (alfa 0,25) e
/// destrutiva. `keepOpen`: o toque não fecha (alternâncias).
struct MenuItem {
    var label: String
    var glyph: Character? = nil
    var swatch: Color? = nil
    var sublabel: String? = nil
    var selected: Bool = false
    var checked: Bool = false
    var submenu: [MenuEntry]? = nil
    var chevron: Bool = false
    var enabled: Bool = true
    var destructive: Bool = false
    var keepOpen: Bool = false
    var tag: String? = nil
    var onClick: () -> Void = {}
}

/// Uma linha do menu ancorado (par do `MenuEntry` do Kotlin).
enum MenuEntry {
    case row(MenuItem)
    case iconRow(cells: [MenuIconCell], selectedIndex: Int, keepOpen: Bool, tag: String?)
    case heading(String, tag: String?)
    case divider

    /// `MenuEntry.Item(...)` do Kotlin.
    static func item(_ label: String, glyph: Character? = nil, swatch: Color? = nil, sublabel: String? = nil,
                     selected: Bool = false, checked: Bool = false, submenu: [MenuEntry]? = nil,
                     chevron: Bool = false, enabled: Bool = true, destructive: Bool = false,
                     keepOpen: Bool = false, tag: String? = nil,
                     onClick: @escaping () -> Void = {}) -> MenuEntry {
        .row(MenuItem(label: label, glyph: glyph, swatch: swatch, sublabel: sublabel, selected: selected,
                      checked: checked, submenu: submenu, chevron: chevron, enabled: enabled,
                      destructive: destructive, keepOpen: keepOpen, tag: tag, onClick: onClick))
    }
    /// `MenuEntry.IconRow(...)` do Kotlin.
    static func icons(_ cells: [MenuIconCell], selectedIndex: Int = -1, keepOpen: Bool = false, tag: String? = nil) -> MenuEntry {
        .iconRow(cells: cells, selectedIndex: selectedIndex, keepOpen: keepOpen, tag: tag)
    }
    /// `MenuEntry.Header(...)` do Kotlin.
    static func header(_ title: String, tag: String? = nil) -> MenuEntry { .heading(title, tag: tag) }
}

/// Medidas do menu (as da referência; `AnchoredMenuDefaults` do Kotlin).
enum AnchoredMenuMetrics {
    static let width: CGFloat = 250
    static let radius: CGFloat = 8
    static let rowHeight: CGFloat = 40
    static let maxVisibleRows = 8
    static let verticalPadding: CGFloat = 4
    /// 8 linhas + o respiro de cima e de baixo; acima disso rola.
    static let maxHeight: CGFloat = rowHeight * CGFloat(maxVisibleRows) + verticalPadding * 2
    static let screenMargin: CGFloat = 8
    static let anchorGap: CGFloat = 4
    static let disabledAlpha: Double = 0.25
    /// 0.3.4: alturas FIXAS dos outros tipos de linha (título 28, divisória 1 + 4 + 4,
    /// "‹ voltar" 40 + a linha de 1). Com elas a altura do menu sai de uma conta,
    /// sem depender de medir a lista por preferência (ver `contentHeight`).
    static let headingHeight: CGFloat = 28
    static let dividerHeight: CGFloat = 9
    static let backRowHeight: CGFloat = rowHeight + 1

    /// Altura da lista inteira de uma página (sem o limite de 8 linhas).
    ///
    /// 0.3.4 (relato do iPhone 13): o menu só aparecia DEPOIS de medir a própria
    /// altura por `PreferenceKey` de dentro de um `ScrollView`; quando essa medida
    /// não chegava, a caixa ficava com 1 pt e opacidade 0, e a janela do menu, que
    /// cobre a tela, engolia o toque seguinte: "o valor fica cinza e nada abre". A
    /// altura agora é calculada; a medida, quando chega, só refina.
    static func contentHeight(_ entries: [MenuEntry], back: Bool) -> CGFloat {
        entries.reduce(back ? backRowHeight : 0) { $0 + $1.rowHeight } + verticalPadding * 2
    }
    /// A altura visível da página (até 8 linhas; acima disso rola).
    static func pageHeight(_ entries: [MenuEntry], back: Bool) -> CGFloat {
        min(contentHeight(entries, back: back), maxHeight)
    }
}

extension MenuEntry {
    /// A altura desta linha no menu (as medidas fixas de `AnchoredMenuMetrics`).
    var rowHeight: CGFloat {
        switch self {
        case .row, .iconRow: return AnchoredMenuMetrics.rowHeight
        case .heading: return AnchoredMenuMetrics.headingHeight
        case .divider: return AnchoredMenuMetrics.dividerHeight
        }
    }
}

/// Onde o menu abre (conta pura; mesmos casos do `AnchoredMenuPosition.kt`).
enum AnchoredMenuPosition {
    /// Borda esquerda do menu. Início em LTR = esquerda; em RTL = direita; `alignEnd` inverte.
    static func x(anchorLeft: CGFloat, anchorRight: CGFloat, windowWidth: CGFloat, popupWidth: CGFloat,
                  rtl: Bool, alignEnd: Bool, margin: CGFloat) -> CGFloat {
        let alignRight = rtl != alignEnd
        let raw = alignRight ? anchorRight - popupWidth : anchorLeft
        return min(max(raw, margin), max(margin, windowWidth - popupWidth - margin))
    }
    /// Topo do menu: embaixo se couber, senão em cima, senão preso dentro da janela.
    static func y(anchorTop: CGFloat, anchorBottom: CGFloat, windowHeight: CGFloat, popupHeight: CGFloat,
                  margin: CGFloat, gap: CGFloat) -> CGFloat {
        let below = anchorBottom + gap
        let above = anchorTop - gap - popupHeight
        if below + popupHeight <= windowHeight - margin { return below }
        if above >= margin { return above }
        return min(max(below, margin), max(margin, windowHeight - popupHeight - margin))
    }
}

/// As cores do menu num tom (todas por token).
struct MenuColors {
    let background, text, muted, pressed, accent, danger, divider, shadow: Color
    static func of(_ tone: AureaSurfaceTone) -> MenuColors {
        switch tone {
        case .light:
            return MenuColors(background: AureaColors.lightSurface, text: AureaColors.lightText,
                              muted: AureaColors.lightTextMuted, pressed: AureaColors.lightSurfaceAlt,
                              accent: AureaColors.lightAccentInk, danger: AureaColors.lightDanger,
                              divider: AureaColors.lightDivider, shadow: AureaColors.lightShadow)
        case .editor:
            return MenuColors(background: AureaColors.pill, text: AureaColors.text, muted: AureaColors.muted,
                              pressed: AureaColors.chipHigh, accent: AureaColors.accent, danger: AureaColors.danger,
                              divider: AureaColors.border, shadow: AureaColors.lightShadow)
        }
    }
}

// =============================================================================
// A âncora
// =============================================================================

extension View {
    /// Abre um menu ancorado a esta view (Omega fundação, F2).
    ///
    /// - Parameters:
    ///   - isPresented: aberto ou fechado (o menu põe `false` ao fechar sozinho).
    ///   - width: largura (250); ignorada com `matchAnchorWidth`.
    ///   - matchAnchorWidth: o menu fica com a largura da âncora (caixa suspensa).
    ///   - tone: `.light` (padrão) ou `.editor`.
    ///   - alignEnd: alinha pela borda de fim da âncora (o padrão é o início, como a referência).
    ///   - tag: `accessibilityIdentifier` da caixa do menu (= `testTag` do Android).
    ///   - entries: as linhas; lidas de novo a cada redesenho (uma alternância `keepOpen` se atualiza).
    func aureaAnchoredMenu(isPresented: Binding<Bool>,
                           width: CGFloat = AnchoredMenuMetrics.width,
                           matchAnchorWidth: Bool = false,
                           tone: AureaSurfaceTone = .light,
                           alignEnd: Bool = false,
                           tag: String? = nil,
                           entries: @escaping () -> [MenuEntry]) -> some View {
        modifier(AnchoredMenuAnchor(isPresented: isPresented, width: width, matchAnchorWidth: matchAnchorWidth,
                                    tone: tone, alignEnd: alignEnd, tag: tag, entries: entries))
    }
}

/// Guarda o que o menu aberto lê (retângulo da âncora e as linhas mais novas).
final class AnchoredMenuBox: ObservableObject {
    let id = UUID()
    /// Retângulo da âncora na CENA (pontos da tela), medido na hora de abrir.
    var anchor: CGRect = .zero
    var entries: () -> [MenuEntry] = { [] }
    /// 0.3.4: a view UIKit invisível atrás da âncora. Medir por ela dá o
    /// retângulo real na tela em qualquer lugar (folha, tela cheia, editor), sem
    /// um `GeometryReader` que refaz a âncora a cada quadro de rolagem.
    weak var probe: UIView?

    /// Mede a âncora agora: retângulo na cena e a cena dela (nil = fora de janela).
    func measure() -> (rect: CGRect, scene: UIWindowScene?)? {
        guard let view = probe, let window = view.window else { return nil }
        let inWindow = view.convert(view.bounds, to: nil)
        guard let scene = window.windowScene else { return (inWindow, nil) }
        return (window.convert(inWindow, to: scene.coordinateSpace), scene)
    }
}

/// A sonda da âncora: uma `UIView` vazia, sem toque e sem acessibilidade, do
/// tamanho da âncora. Só serve para medir (`AnchoredMenuBox.measure`).
private struct AnchoredMenuProbe: UIViewRepresentable {
    let box: AnchoredMenuBox
    func makeUIView(context: Context) -> UIView {
        let view = UIView()
        view.isUserInteractionEnabled = false
        view.backgroundColor = .clear
        view.isAccessibilityElement = false
        view.accessibilityElementsHidden = true
        box.probe = view
        return view
    }
    func updateUIView(_ view: UIView, context: Context) { box.probe = view }
}

private struct AnchoredMenuAnchor: ViewModifier {
    @Binding var isPresented: Bool
    let width: CGFloat
    let matchAnchorWidth: Bool
    let tone: AureaSurfaceTone
    let alignEnd: Bool
    let tag: String?
    let entries: () -> [MenuEntry]
    @StateObject private var box = AnchoredMenuBox()
    @Environment(\.layoutDirection) private var direction

    func body(content: Content) -> some View {
        // As linhas mais novas (sem estado do SwiftUI: o menu as lê ao redesenhar).
        box.entries = entries
        return content
            .background(AnchoredMenuProbe(box: box).allowsHitTesting(false).accessibilityHidden(true))
            .onChange(of: isPresented) { shown in
                if shown { schedulePresent() } else { AureaPopupPresenter.shared.dismiss(owner: box.id, notify: false) }
            }
            .onAppear { if isPresented { schedulePresent() } }
            .onDisappear { AureaPopupPresenter.shared.dismiss(owner: box.id, notify: true) }
    }

    /// 0.3.4: abre no PRÓXIMO giro do laço principal, fora da atualização do
    /// SwiftUI que rodou o `onChange`. Criar a janela e o `UIHostingController`
    /// dentro da atualização de outra hierarquia fazia o primeiro desenho do
    /// menu se perder em aparelho. Se não der para abrir, `isPresented` volta a
    /// falso — nunca fica preso em verdadeiro (preso, o próximo toque não mudava
    /// nada e a caixa parecia morta).
    private func schedulePresent() {
        // Tudo lido AGORA (dentro do body/ação instalados), não no bloco adiado.
        let box = self.box
        let binding = $isPresented
        let width = self.width, matchAnchorWidth = self.matchAnchorWidth
        let tone = self.tone, alignEnd = self.alignEnd, tag = self.tag, direction = self.direction
        func attempt(_ left: Int) {
            guard binding.wrappedValue else { return }
            // A âncora ainda não está numa janela (acabou de aparecer): tenta de
            // novo um pouco depois; saiu da tela de vez: desfaz o "aberto".
            guard let measured = box.measure() else {
                if left > 0 { DispatchQueue.main.asyncAfter(deadline: .now() + 0.05) { attempt(left - 1) } }
                else { binding.wrappedValue = false }
                return
            }
            box.anchor = measured.rect
            let presenter = AureaPopupPresenter.shared
            let menuWidth = matchAnchorWidth && box.anchor.width > 0 ? box.anchor.width : width
            let shown = presenter.present(owner: box.id, scene: measured.scene, onDismiss: { binding.wrappedValue = false }) {
                AnchoredMenuOverlay(box: box, width: menuWidth, tone: tone, alignEnd: alignEnd, tag: tag,
                                    direction: direction) {
                    presenter.dismiss(owner: box.id, notify: true)
                }
            }
            if !shown { binding.wrappedValue = false }
        }
        DispatchQueue.main.async { attempt(4) }
    }
}

// =============================================================================
// A camada por cima de tudo (o `Popup` do Compose)
// =============================================================================

/// Apresenta UMA camada flutuante por vez numa janela transparente acima do app.
/// Abrir outra fecha a anterior (avisando o dono).
final class AureaPopupPresenter {
    static let shared = AureaPopupPresenter()
    private var window: UIWindow?
    private weak var previousKey: UIWindow?
    private var owner: UUID?
    private var onDismiss: (() -> Void)?

    /// A cena onde o app está na frente (a da janela-chave, se houver).
    static func activeScene() -> UIWindowScene? {
        let scenes = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }
        return scenes.first { $0.activationState == .foregroundActive && $0.windows.contains(where: \.isKeyWindow) }
            ?? scenes.first { $0.activationState == .foregroundActive }
            ?? scenes.first { $0.activationState == .foregroundInactive }
            ?? scenes.first
    }

    /// Mostra `content` por cima de tudo. `onDismiss` é chamado quando a camada
    /// fecha por outro motivo que não o dono (toque fora, outro menu, item).
    /// `scene`: a cena da âncora (nil = a cena ativa). Devolve falso se não houve
    /// onde mostrar (quem chama desfaz o "aberto").
    @discardableResult
    func present<Content: View>(owner: UUID, scene preferred: UIWindowScene? = nil, onDismiss: @escaping () -> Void,
                                @ViewBuilder content: () -> Content) -> Bool {
        if let current = self.owner, current != owner { dismiss(owner: current, notify: true) }
        guard let scene = preferred ?? Self.activeScene() else { return false }
        let host = UIHostingController(rootView: AnyView(content()))
        host.view.backgroundColor = .clear
        host.view.accessibilityViewIsModal = true
        if let window, self.owner == owner {
            window.rootViewController = host
            window.isHidden = false
            return true
        }
        previousKey = scene.windows.first(where: \.isKeyWindow)
        let layer = UIWindow(windowScene: scene)
        // 0.3.4: a janela cobre a cena inteira e fica ACIMA de toda janela visível
        // do app (folhas e telas cheias moram na janela principal; anúncios e
        // alertas podem ter janela própria), nunca abaixo do nível de alerta.
        layer.frame = scene.coordinateSpace.bounds
        let highest = scene.windows.filter { !$0.isHidden && $0 !== layer }.map(\.windowLevel.rawValue).max()
            ?? UIWindow.Level.normal.rawValue
        layer.windowLevel = UIWindow.Level(rawValue: max(UIWindow.Level.alert.rawValue + 1, highest + 1))
        layer.backgroundColor = .clear
        layer.rootViewController = host
        layer.isHidden = false
        layer.makeKeyAndVisible()
        window = layer
        self.owner = owner
        self.onDismiss = onDismiss
        UIAccessibility.post(notification: .screenChanged, argument: host.view)
        return true
    }

    /// Fecha a camada se ela for de `owner`. `notify` avisa o dono (põe o `isPresented` dele em falso).
    func dismiss(owner: UUID, notify: Bool) {
        guard self.owner == owner else { return }
        let callback = onDismiss
        let closing = window
        closing?.isHidden = true
        window = nil
        self.owner = nil
        onDismiss = nil
        previousKey?.makeKey()
        previousKey = nil
        // A janela pode estar no meio do próprio toque (item do menu): solta no próximo giro.
        DispatchQueue.main.async { _ = closing }
        if notify { callback?() }
    }
}

extension AureaPopupPresenter {
    /// Mostra um diálogo (`AureaAlert`/`AureaDialog`) por cima de tudo, fora da
    /// hierarquia de quem chama — ex.: de dentro da moldura de 200 do painel,
    /// onde um `.overlay` ficaria preso. `close` fecha a camada (passe como
    /// `onDismiss` do diálogo).
    func presentDialog<Content: View>(@ViewBuilder _ dialog: (_ close: @escaping () -> Void) -> Content) {
        let owner = UUID()
        let close: () -> Void = { [weak self] in self?.dismiss(owner: owner, notify: false) }
        present(owner: owner, onDismiss: {}) { dialog(close) }
    }
}

private struct AnchoredMenuSizeKey: PreferenceKey {
    static var defaultValue: CGSize = .zero
    static func reduce(value: inout CGSize, nextValue: () -> CGSize) { value = nextValue() }
}

/// A janela do menu: o toque fora fecha; o menu fica onde `AnchoredMenuPosition` manda.
private struct AnchoredMenuOverlay: View {
    @ObservedObject var box: AnchoredMenuBox
    let width: CGFloat
    let tone: AureaSurfaceTone
    let alignEnd: Bool
    let tag: String?
    let direction: LayoutDirection
    let dismiss: () -> Void
    @State private var size: CGSize = .zero
    /// Altura calculada da página aberta (nil = a raiz); ver `AnchoredMenuMetrics.contentHeight`.
    @State private var pageHeight: CGFloat?

    var body: some View {
        GeometryReader { geometry in
            let origin = geometry.frame(in: .global).origin
            let window = geometry.size
            let menuWidth = min(width, max(1, window.width - 2 * AnchoredMenuMetrics.screenMargin))
            let anchor = box.anchor.offsetBy(dx: -origin.x, dy: -origin.y)
            // 0.3.4: a altura vem da conta (aparece já no primeiro quadro); a medida só refina.
            let height = size.height > 8 ? size.height
                : (pageHeight ?? AnchoredMenuMetrics.pageHeight(box.entries(), back: false))
            let x = AnchoredMenuPosition.x(anchorLeft: anchor.minX, anchorRight: anchor.maxX, windowWidth: window.width,
                                           popupWidth: menuWidth, rtl: direction == .rightToLeft, alignEnd: alignEnd,
                                           margin: AnchoredMenuMetrics.screenMargin)
            let y = AnchoredMenuPosition.y(anchorTop: anchor.minY, anchorBottom: anchor.maxY, windowHeight: window.height,
                                           popupHeight: height, margin: AnchoredMenuMetrics.screenMargin,
                                           gap: AnchoredMenuMetrics.anchorGap)
            ZStack(alignment: .topLeading) {
                Color.clear.contentShape(Rectangle()).onTapGesture(perform: dismiss)
                    .accessibilityHidden(true)
                AnchoredMenuPanel(entries: { box.entries() }, colors: MenuColors.of(tone), width: menuWidth, dismiss: dismiss,
                                  onPage: { pageHeight = $0 })
                    .environment(\.layoutDirection, direction)
                    .frame(width: menuWidth)
                    .background(GeometryReader { panel in
                        Color.clear.preference(key: AnchoredMenuSizeKey.self, value: panel.size)
                    })
                    .accessibilityElement(children: .contain)
                    .modifier(OptionalIdentifier(tag: tag))
                    .accessibilityAddTraits(.isModal)
                    .accessibilityAction(.escape, dismiss)
                    .offset(x: x, y: y)
            }
            // Coordenadas da tela: sempre LTR; o conteúdo do menu segue o idioma.
            .environment(\.layoutDirection, .leftToRight)
        }
        .ignoresSafeArea()
        .onPreferenceChange(AnchoredMenuSizeKey.self) { size = $0 }
    }
}

// =============================================================================
// O menu
// =============================================================================

private struct MenuContentHeightKey: PreferenceKey {
    static var defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) { value = nextValue() }
}
private struct MenuScrollOffsetKey: PreferenceKey {
    static var defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) { value = nextValue() }
}

/// A caixa do menu (pode ser usada também dentro de outra tela, sem âncora).
struct AnchoredMenuPanel: View {
    let entries: () -> [MenuEntry]
    let colors: MenuColors
    let width: CGFloat
    let dismiss: () -> Void
    /// Avisa a altura calculada da página que abriu (subpágina ou volta).
    var onPage: ((CGFloat) -> Void)? = nil
    /// Caminho das subpáginas abertas (índices): resolvido a cada redesenho.
    @State private var path: [Int] = []
    @State private var forward = true
    /// Redesenha depois de uma alternância `keepOpen` (as linhas vêm do dono).
    @State private var tick = 0

    var body: some View {
        // `tick` muda depois de uma alternância `keepOpen`: lê de novo as linhas do dono.
        let _ = tick
        let resolved = resolve(entries(), path)
        ZStack {
            MenuPageView(title: resolved.title, entries: resolved.page, colors: colors,
                         onBack: { go(Array(path.dropLast()), forward: false) },
                         onOpen: { go(path + [$0], forward: true) },
                         onToggle: { DispatchQueue.main.async { tick &+= 1 } },
                         dismiss: dismiss)
                .id(path)
                .transition(.asymmetric(
                    insertion: .offset(x: (forward ? 1 : -1) * width / 2).combined(with: .opacity),
                    removal: .offset(x: (forward ? -1 : 1) * width / 2).combined(with: .opacity)))
        }
        .background(colors.background, in: RoundedRectangle(cornerRadius: AnchoredMenuMetrics.radius))
        .overlay(RoundedRectangle(cornerRadius: AnchoredMenuMetrics.radius).stroke(colors.divider, lineWidth: 0.5))
        .clipShape(RoundedRectangle(cornerRadius: AnchoredMenuMetrics.radius))
        .shadow(color: colors.shadow, radius: 8, x: 0, y: 3)
    }

    private func go(_ next: [Int], forward: Bool) {
        self.forward = forward
        onPage?(AnchoredMenuMetrics.pageHeight(resolve(entries(), next).page, back: !next.isEmpty))
        // Subpágina: entra deslizando meia largura com repique (250 ms); sai em 200 ms.
        withAnimation(forward ? .spring(response: 0.25, dampingFraction: 0.7) : .easeIn(duration: 0.2)) { path = next }
    }

    private func resolve(_ root: [MenuEntry], _ path: [Int]) -> (title: String?, page: [MenuEntry]) {
        var page = root
        var title: String? = nil
        for index in path {
            guard index < page.count, case let .row(item) = page[index], let sub = item.submenu else { return (nil, root) }
            page = sub
            title = item.label
        }
        return (title, page)
    }
}

private struct MenuPageView: View {
    let title: String?
    let entries: [MenuEntry]
    let colors: MenuColors
    let onBack: () -> Void
    let onOpen: (Int) -> Void
    let onToggle: () -> Void
    let dismiss: () -> Void
    @State private var contentHeight: CGFloat = 0
    @State private var offset: CGFloat = 0

    var body: some View {
        // 0.3.4: a altura da lista sai da conta; a medida (preferência de dentro
        // do ScrollView) só substitui quando chega. Sem ela o menu ficava com 1 pt.
        let content = contentHeight > 0 ? contentHeight
            : AnchoredMenuMetrics.contentHeight(entries, back: title != nil)
        let visible = min(content, AnchoredMenuMetrics.maxHeight)
        ScrollView(.vertical, showsIndicators: false) {
            VStack(spacing: 0) {
                if let title { backRow(title) }
                ForEach(Array(entries.enumerated()), id: \.offset) { index, entry in
                    row(index, entry)
                }
            }
            .padding(.vertical, AnchoredMenuMetrics.verticalPadding)
            .background(GeometryReader { content in
                Color.clear
                    .preference(key: MenuContentHeightKey.self, value: content.size.height)
                    .preference(key: MenuScrollOffsetKey.self, value: -content.frame(in: .named("aurea.menu")).minY)
            })
        }
        .coordinateSpace(name: "aurea.menu")
        .frame(height: max(1, visible))
        .onPreferenceChange(MenuContentHeightKey.self) { contentHeight = $0 }
        .onPreferenceChange(MenuScrollOffsetKey.self) { offset = $0 }
        .overlay(alignment: .top) { if offset > 1 { cue(CupertinoGlyph.ChevronUp) } }
        .overlay(alignment: .bottom) { if content - offset - visible > 1 { cue(CupertinoGlyph.ChevronDown) } }
    }

    /// Setas de rolagem: só quando há mais lista acima ou abaixo.
    private func cue(_ glyph: Character) -> some View {
        CupertinoGlyph.text(glyph, size: 12, color: colors.text)
            .frame(maxWidth: .infinity).frame(height: 14)
            .background(colors.background.opacity(0.92))
            .allowsHitTesting(false)
            .accessibilityHidden(true)
    }

    private func backRow(_ title: String) -> some View {
        VStack(spacing: 0) {
            Button(action: onBack) {
                HStack(spacing: 8) {
                    CupertinoGlyph.text(CupertinoGlyph.ChevronLeft, size: 16, color: colors.text).mirrorsInRtl()
                    Text(title).font(.aurea(size: 14, weight: .semibold)).foregroundStyle(colors.text).lineLimit(1)
                    Spacer(minLength: 0)
                }
                .padding(.leading, 8).padding(.trailing, 14)
                .frame(height: AnchoredMenuMetrics.rowHeight).contentShape(Rectangle())
            }
            .buttonStyle(AureaPressStyle(shrink: 1))
            .accessibilityLabel(AureaText.t("om_menu_back"))
            Rectangle().fill(colors.divider).frame(height: 1)
        }
    }

    @ViewBuilder
    private func row(_ index: Int, _ entry: MenuEntry) -> some View {
        switch entry {
        case .row(let item):
            itemRow(item) {
                if item.submenu != nil { onOpen(index) }
                else if item.keepOpen { item.onClick(); onToggle() }
                else { dismiss(); item.onClick() }
            }
        case .iconRow(let cells, let selectedIndex, let keepOpen, let tag):
            iconRow(cells, selectedIndex: selectedIndex, keepOpen: keepOpen, tag: tag)
        case .heading(let title, let tag):
            Text(title).font(.aurea(size: 11, weight: .semibold)).foregroundStyle(colors.muted).lineLimit(1)
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(.horizontal, 14).padding(.bottom, 4)
                // Altura fixa (a conta de `AnchoredMenuMetrics.contentHeight`).
                .frame(height: AnchoredMenuMetrics.headingHeight, alignment: .bottom)
                .accessibilityAddTraits(.isHeader)
                .modifier(OptionalIdentifier(tag: tag))
        case .divider:
            Rectangle().fill(colors.divider).frame(height: 1).padding(.vertical, 4)
        }
    }

    private func itemRow(_ item: MenuItem, onTap: @escaping () -> Void) -> some View {
        let ink = item.destructive ? colors.danger : colors.text
        return Button(action: onTap) {
            HStack(spacing: 0) {
                // Barra de 4 da opção vigente.
                Rectangle().fill(item.selected ? colors.accent : Color.clear).frame(width: 4, height: 24)
                if let swatch = item.swatch {
                    RoundedRectangle(cornerRadius: 6).fill(swatch).frame(width: 24, height: 24)
                        .overlay(RoundedRectangle(cornerRadius: 6).stroke(colors.divider, lineWidth: 1))
                        .padding(.leading, 6)
                } else if let glyph = item.glyph {
                    CupertinoGlyph.text(glyph, size: 20, color: ink).frame(width: 40, height: 40)
                }
                Spacer().frame(width: item.glyph != nil && item.swatch == nil ? 2 : 10)
                VStack(alignment: .leading, spacing: 0) {
                    Text(item.label).font(.aurea(size: 14)).foregroundStyle(ink).lineLimit(1)
                    if let sublabel = item.sublabel {
                        Text(sublabel).font(.aurea(size: 10)).foregroundStyle(colors.muted).lineLimit(1)
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                if item.checked {
                    CupertinoGlyph.text(CupertinoGlyph.CheckmarkAlt, size: 16, color: colors.accent).padding(.leading, 6)
                }
                if item.submenu != nil || item.chevron {
                    CupertinoGlyph.text(CupertinoGlyph.ChevronRight, size: 14, color: colors.muted).mirrorsInRtl().padding(.leading, 6)
                }
                Spacer().frame(width: 12)
            }
            .frame(height: AnchoredMenuMetrics.rowHeight)
            .background(item.selected ? colors.pressed : Color.clear)
            .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .disabled(!item.enabled)
        .opacity(item.enabled ? 1 : AnchoredMenuMetrics.disabledAlpha)
        .accessibilityAddTraits(item.selected || item.checked ? .isSelected : [])
        .modifier(OptionalIdentifier(tag: item.tag))
    }

    private func iconRow(_ cells: [MenuIconCell], selectedIndex: Int, keepOpen: Bool, tag: String?) -> some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 0) {
                ForEach(Array(cells.enumerated()), id: \.offset) { index, cell in
                    let on = index == selectedIndex
                    Button {
                        if keepOpen { cell.onClick(); onToggle() } else { dismiss(); cell.onClick() }
                    } label: {
                        ZStack {
                            if let swatch = cell.swatch {
                                RoundedRectangle(cornerRadius: 3).fill(swatch).frame(width: 20, height: 20)
                            } else if let glyph = cell.glyph {
                                CupertinoGlyph.text(glyph, size: 20, color: colors.text)
                            }
                        }
                        .frame(width: 28, height: 28)
                        .overlay { if on { RoundedRectangle(cornerRadius: 4).stroke(colors.text, lineWidth: 2) } }
                        .frame(width: 34, height: 40).contentShape(Rectangle())
                    }
                    .buttonStyle(AureaPressStyle(shrink: 1))
                    .accessibilityLabel(cell.label)
                    .accessibilityAddTraits(on ? .isSelected : [])
                    .modifier(OptionalIdentifier(tag: cell.tag))
                }
            }
            .padding(.horizontal, 6)
        }
        .frame(height: AnchoredMenuMetrics.rowHeight)
        .modifier(OptionalIdentifier(tag: tag))
    }
}

/// `accessibilityIdentifier` só quando há id (= `testTag` do Android).
struct OptionalIdentifier: ViewModifier {
    let tag: String?
    func body(content: Content) -> some View {
        if let tag { content.accessibilityIdentifier(tag) } else { content }
    }
}
