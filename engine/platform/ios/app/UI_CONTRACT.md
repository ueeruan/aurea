// =============================================================================
//  Aurea / platform / ios / app / UI_CONTRACT.md
//
//  O CONTRATO da casca SwiftUI do iOS. Quem escreve uma tela do iOS lê ISTO
//  antes: o Android APROVADO é a especificação, e o iOS é o MESMO produto — não
//  um app parecido. Implementação nativa diferente; design igual.
//
//  Regras (valem para todo arquivo Swift desta pasta):
//
//  R1. A REFERÊNCIA é o arquivo Kotlin correspondente. Mesma ordem de seções,
//      mesma hierarquia de containers, mesmas medidas, mesmos rótulos, mesmos
//      estados, mesmos gestos. Se o Android tem `Effects → Glow → Intensidade →
//      Raio`, o iOS tem exatamente esse fluxo. Não invente layout.
//  R2. NADA de estado local falso. Todo valor vem do núcleo C++ pela ponte
//      (`AureaEngine`), como no Android vem do `EditorStore` + JNI. Não existe
//      "mock para parecer que funciona": se o núcleo não tem a operação, a tela
//      não finge — ela não oferece o controle.
//  R3. Cor, medida e tipografia SÓ dos tokens (`Theme.swift`, que espelha
//      `AureaTokens.kt` + `ShellTokens.kt` + `EditorLayout.kt`). Nenhum número
//      solto, nenhum `Color(red:...)` inventado.
//  R4. Texto SÓ pelo catálogo: `AureaStrings.t("chave_do_android")`. A chave é a
//      MESMA do Android. Faltando uma chave: ACRESCENTE ao fim da lista `KEYS`
//      de `tools/_ios_strings.py` (nunca reordene nem remova) e rode
//      `python tools/_ios_strings.py`. Nunca use literal pt-BR no lugar.
//  R5. Use os componentes do `DesignSystem.swift` (relação abaixo). Faltando um,
//      crie-o NO SEU PRÓPRIO ARQUIVO, `private`, com nome único — não edite
//      arquivo de outro dono.
//  R6. Um arquivo tem UM dono. Não toque em arquivo que não é seu.
//  R7. Verificação antes do commit: `python tools/_ios_check.py` (checa chaves
//      de texto, símbolos do núcleo e nomes de tipo duplicados). Corrija até 0.
//  R8. Commit com `git commit -- <os SEUS arquivos>` (commit parcial), mensagem
//      em pt-BR, terminando com a linha de co-autoria do Claude.
//
//  ===========================================================================
//  A API CONGELADA (implementada em DesignSystem.swift e companhia)
//  ===========================================================================
//
//  VALORES
//    enum KeyframeLook { case none, animated, keyHere }
//    enum ExpressionLook { case none, ok, error, off }
//    func numeroPtBr(_ v: Float, casas: Int = 1) -> String      // "1.234,5"
//    func comUnidade(_ texto: String, _ unit: String) -> String
//
//  RÉGUA E GESTO DO NÚMERO (o coração do painel: é assim que se muda valor)
//    struct TickRuler: View {
//        init(value: @escaping () -> Float, unitsPerDp: Float, active: Bool,
//             height: CGFloat = 40, verticalPadding: CGFloat = 8)
//    }
//    extension View {
//        func valueDrag(enabled: Bool, start: @escaping () -> Float,
//                       unitsPerDp: @escaping () -> Float,
//                       min: Float, max: Float,
//                       onStart: @escaping () -> Void,
//                       onValue: @escaping (Float) -> Void,
//                       onEnd: @escaping () -> Void) -> some View
//    }
//    struct ValueBox: View { init(_ text: String, width: CGFloat = 60,
//                                 onTap: (() -> Void)? = nil) }
//
//  LINHAS DE PROPRIEDADE
//    struct PropertyLabelChip: View { init(_ title: String, expression: ExpressionLook,
//                                          onTap: (() -> Void)? = nil) }
//    struct PropertyRow: View {
//        init(_ label: String, value: String, look: KeyframeLook,
//             expression: ExpressionLook = .none, enabled: Bool = true,
//             onKeyframe: (() -> Void)? = nil,
//             onExpression: (() -> Void)? = nil,
//             onTap: (() -> Void)? = nil)
//    }
//    struct PropertyCustomRow<Content: View>: View {
//        init(_ label: String, selected: Bool, onSelect: @escaping () -> Void,
//             @ViewBuilder content: () -> Content)
//    }
//    struct AureaToggle: View { init(checked: Bool, onCheckedChange: @escaping (Bool) -> Void) }
//    struct ChoiceChips: View { init(_ items: [String], selected: Int,
//                                    onSelect: @escaping (Int) -> Void) }
//    struct Chip: View { init(_ label: String, on: Bool, onClick: @escaping () -> Void) }
//    struct SectionTitle: View { init(_ t: String) }
//    struct KeyframeDiamondIcon: View { init(look: KeyframeLook, enabled: Bool) }
//    struct CurveRailIcon: View { init(enabled: Bool, animated: Bool) }
//    struct ColorWell: View { init(color: Color, onClick: @escaping () -> Void) }
//
//  CROMO DE PAINEL (a moldura que TODO painel usa, como no Android)
//    struct PanelHeader: View { init(title: String, onBack: @escaping () -> Void) }
//    struct LeftRail: View {                    // ‹ · ◇ · curva · ⋯
//        init(keyframeLook: KeyframeLook, onKeyframe: (() -> Void)?,
//             curveAnimated: Bool, onCurve: (() -> Void)?, onMore: (() -> Void)?,
//             onBack: @escaping () -> Void)
//    }
//    struct RailMoreButton: View { init(active: Bool, onClick: @escaping () -> Void) }
//    struct RightRail: View { init(modes: [String], selected: Int, onSelect: @escaping (Int) -> Void) }
//    struct ParamTabs: View { init(_ tabs: [String], selected: Int, onSelect: @escaping (Int) -> Void) }
//    struct PanelNotice: View { init(_ text: String) }
//    struct ChromeButton: View
//    struct ChromeVectorButton: View
//    struct ActivityIndicator: View { init(size: CGFloat = 22, color: Color = AureaColors.text) }
//    extension View { func drawTopHairline() -> some View }
//
//  FOLHAS E MENUS (os modais; no iOS são `.sheet`/`fullScreenCover`, mesma
//  aparência e mesma ordem de itens que a folha do Android)
//    struct AureaModalSheet<Content: View>: View { init(@ViewBuilder content: () -> Content) }
//    struct AureaActionSheet: View { init(title: String, items: [(String, () -> Void)],
//                                         onDismiss: @escaping () -> Void) }
//    struct AureaAlert: View
//    struct AureaNamePrompt: View
//    struct ShellMenuSheet<Content: View>: View
//    struct MenuSection: View { init(title: String) }
//    struct MenuItemRow: View { init(glyph: String, label: String, value: String = "",
//                                    on: Bool = false, danger: Bool = false,
//                                    disabled: Bool = false, action: @escaping () -> Void) }
//    struct ShellPopupMenu: View { init(items: [PopupItem], onDismiss: @escaping () -> Void,
//                                       width: CGFloat = 250) }
//    struct GoToTimeDialog: View
//
//  NÚMERO E COR (as duas folhas de edição)
//    struct NumericKeypadSheet: View { init(request: KeypadRequest, onDismiss: @escaping () -> Void) }
//    struct ColorPickerSheet: View
//    struct AureaAdjustSheet: View
//
//  CARTÃO DE EFEITO
//    struct EffectCard: View
//    struct EffectTile: View
//    struct AdvancedToggle: View { init(open: Bool, count: Int, onToggle: @escaping () -> Void) }
//
//  ===========================================================================
//  O QUE JÁ EXISTE E NÃO MUDA (dono: ninguém nesta rodada)
//  ===========================================================================
//    AureaModel.swift        o estado do app (espelha o EditorStore) — SE PRECISAR
//                            de um getter/ação nova, você ADICIONA ao fim, na
//                            seção marcada com o seu nome de área, e avisa no
//                            relatório. Não reescreva o arquivo.
//    AureaBridge.h/.mm       a ponte C++ (não precisa mexer)
//    AureaEngine.h/.mm       a superfície ObjC — idem: adicione ao fim, se faltar
//    PreviewMetalView.swift  o preview (CAMetalLayer) — pronto
//    Theme.swift             dono: A (design system)
//    AureaStrings.swift      GERADO — não edite à mão
