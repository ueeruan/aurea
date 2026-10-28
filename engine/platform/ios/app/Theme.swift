// =============================================================================
//  Aurea / platform / ios / app / Theme.swift
//
//  As cores, a tipografia e as medidas da casca. Tudo vem do MESMO lugar que o
//  Android: `ui/theme/AureaTokens.kt` (cores, tipografia, espaço, raio),
//  `editor/ShellTokens.kt` (peças só da casca do editor) e `editor/EditorLayout.kt`
//  (as alturas das zonas).
//
//  Por que copiar os números em vez de "desenhar bonito": o Aurea já tem uma
//  identidade aprovada (Beta A.01), e o iOS é a MESMA experiência — quem abre os
//  dois aparelhos tem que reconhecer o app. Onde o iOS pede outra coisa (um
//  gesto, uma folha modal), a diferença é do sistema, não do desenho.
//
//  DOIS NOMES PARA O MESMO TOKEN: o Kotlin escreve `AureaColors.Accent` e o
//  Swift já escrevia `AureaColors.accent`. Os dois nomes existem aqui, apontando
//  para o mesmo valor — quem porta uma tela linha a linha do Kotlin não precisa
//  traduzir nome, e quem lê o resto da casca Swift continua em paz. Nunca um
//  valor novo: é sempre o mesmo `static let` com um apelido.
//
//  E NADA DE NÚMERO SOLTO: o que a tela usa vem daqui. Medida de DESENHO (dentro
//  de um `Canvas`) pode ser número — é geometria, não layout.
// =============================================================================
import SwiftUI
import CoreText

extension Font {
    /// Android's default UI font, bundled from the reference system with its license.
    static func aurea(size: CGFloat, weight: Font.Weight = .regular, design: Font.Design = .default) -> Font {
        if design == .monospaced { return .system(size: size, weight: weight, design: .monospaced) }
        return .custom("Roboto-Regular", fixedSize: size).weight(weight)
    }
}


/// Paleta de um tema (par do `AureaPalette` do Android, mesmos ids e valores):
/// só fundos, superfícies, marca e destaque mudam entre temas.
struct AureaPalette: Identifiable {
    let id: String
    let brandDeep: Color
    let brand: Color
    let accent: Color
    let keyframe: Color
    let background: Color
    let surface: Color
    let surfaceHigh: Color
    let chip: Color
    let chipHigh: Color
    let border: Color
    let muted: Color
    let subtle: Color
    let onAccent: Color
    let accentDim: Color
    let keyframeDim: Color
    let stage: Color
    let hairline: Color
    let editorTopBar: Color
    let editorPanel: Color
    let editorPanelHigh: Color
    let pill: Color
    let actionDim: Color
    let statusBarVeil: Color
    let systemBarVeil: Color
    let tickWeak: Color
    let tickStrong: Color
    let railModeFill: Color
    let fieldFilled: Color
    // --- Editor principal (redesenho 2026-09-29) --------------------------------
    // Opcionais com padrão derivado dos campos acima: as paletas que não dizem
    // nada continuam compilando e ganham um tom coerente com o resto delas.
    /// Fundo do editor, barra do topo, transporte e timeline.
    var editorCanvasOverride: Color? = nil
    /// Barra de adicionar (e a área segura de baixo, sob ela).
    var editorBarOverride: Color? = nil
    /// Traço de 1 pt no topo da barra de adicionar.
    var editorBarLineOverride: Color? = nil
    /// Pílula da fileira da camada na timeline.
    var editorRowPillOverride: Color? = nil
    /// Quadradinho de 22 pt do glifo dentro da pílula.
    var editorGlyphBoxOverride: Color? = nil
    /// Ícone e texto sobre a cor de marca (`brand`/`action`). Nil = branco #F7F9FB
    /// (todas as paletas escuras).
    var onBrandOverride: Color? = nil
    // Omega fundação: papéis das telas fora do editor. Nil = derivados dos tons
    // deste tema (`AureaLightRoles.mapped(from:)`); nenhuma paleta passa outro.
    var lightRolesOverride: AureaLightRoles? = nil
    // Omega timeline (área 03): cabeçalho da linha, aba do tempo atual e fundo da
    // linha arrastada. Nil = derivados dos tons deste tema (par do Android).
    var timelineHeaderOverride: Color? = nil
    var timelineTimeTabOverride: Color? = nil
    var timelineDragRowOverride: Color? = nil
    var timelineHeader: Color { timelineHeaderOverride ?? editorRowPill }
    var timelineTimeTab: Color { timelineTimeTabOverride ?? editorRowPill }
    var timelineDragRow: Color { timelineDragRowOverride ?? chipHigh }
    // Omega painel: (área 04) fundo do gráfico do editor de curva (P10 da
    // referência; par do `curveGraph` do Android). Todo tema deriva do campo
    // alto que já tem.
    var curveGraph: Color { chipHigh }
    /// Os papéis claros em uso (ver `AureaLightRoles`).
    var light: AureaLightRoles { lightRolesOverride ?? .mapped(from: self) }
    /// Omega efeitos (área 07): papéis do painel de efeitos (ver `AureaFxRoles`;
    /// par do `fx` do Android), derivados dos tons do tema.
    var fx: AureaFxRoles { .mapped(from: self) }
    /// Omega conteúdo (área 06): papéis dos editores de conteúdo (ver `AureaContentRoles`).
    var content: AureaContentRoles { .mapped(from: self) }
    /// Omega home (área 01): papéis só da Home (ver `AureaHomeRoles`; par do `home` do Android).
    var home: AureaHomeRoles { .mapped(from: self) }
    var editorCanvas: Color { editorCanvasOverride ?? editorPanelHigh }
    var editorBar: Color { editorBarOverride ?? surfaceHigh }
    var editorBarLine: Color { editorBarLineOverride ?? border }
    var editorRowPill: Color { editorRowPillOverride ?? chip }
    var editorGlyphBox: Color { editorGlyphBoxOverride ?? chipHigh }
    /// Omega fundação: faltava (o `AureaColors.onAction` já lia `palette.onBrand`).
    /// Mesmo padrão do Android: branco #F7F9FB nos temas escuros.
    var onBrand: Color { onBrandOverride ?? Color(hex: 0xFFF7F9FB) }
    static let aurea = AureaPalette(id: "aurea", brandDeep: Color(hex: 0xFF123A63), brand: Color(hex: 0xFF245D8C), accent: Color(hex: 0xFF6FAED9), keyframe: Color(hex: 0xFFA9D3EC), background: Color(hex: 0xFF0F141A), surface: Color(hex: 0xFF151C24), surfaceHigh: Color(hex: 0xFF1B2530), chip: Color(hex: 0xFF212D3A), chipHigh: Color(hex: 0xFF323D49), border: Color(hex: 0xFF273442), muted: Color(hex: 0xFFAAB6C3), subtle: Color(hex: 0xFF7C8A99), onAccent: Color(hex: 0xFF0B1117), accentDim: Color(hex: 0xFF1D3A55), keyframeDim: Color(hex: 0xFF22405A), stage: Color(hex: 0xFF0A0E13), hairline: Color(hex: 0xB8273442), editorTopBar: Color(hex: 0xFF0F141A), editorPanel: Color(hex: 0xFF0F141A), editorPanelHigh: Color(hex: 0xFF151C24), pill: Color(hex: 0xFF1B2530), actionDim: Color(hex: 0xFF16304A), statusBarVeil: Color(hex: 0xFF070A0E), systemBarVeil: Color(hex: 0xFF0B0F13), tickWeak: Color(hex: 0xFF43516A), tickStrong: Color(hex: 0xFF7485A3), railModeFill: Color(hex: 0xFF1E222D), fieldFilled: Color(hex: 0xFF272B33),
                                    editorCanvasOverride: Color(hex: 0xFF161C2A), editorBarOverride: Color(hex: 0xFF1B2330), editorBarLineOverride: Color(hex: 0xFF2A3444),
                                    editorRowPillOverride: Color(hex: 0xFF222B3B), editorGlyphBoxOverride: Color(hex: 0xFF2E3B4E))
    // 0.4.1: tema do mockup "interface 03" (Hanszx, iOS True AMOLED) — o padrão. Par do Android.
    static let amoled = AureaPalette(id: "amoled", brandDeep: Color(hex: 0xFF003A80), brand: Color(hex: 0xFF0A84FF), accent: Color(hex: 0xFF0A84FF), keyframe: Color(hex: 0xFF64D2FF), background: Color(hex: 0xFF000000), surface: Color(hex: 0xFF121214), surfaceHigh: Color(hex: 0xFF1C1C1E), chip: Color(hex: 0xFF1C1C1E), chipHigh: Color(hex: 0xFF2C2C2E), border: Color(hex: 0xFF242426), muted: Color(hex: 0xFF8E8E93), subtle: Color(hex: 0xFF636366), onAccent: Color(hex: 0xFFFFFFFF), accentDim: Color(hex: 0xFF06213D), keyframeDim: Color(hex: 0xFF0B2A3A), stage: Color(hex: 0xFF000000), hairline: Color(hex: 0x24FFFFFF), editorTopBar: Color(hex: 0xFF000000), editorPanel: Color(hex: 0xFF0E0E10), editorPanelHigh: Color(hex: 0xFF121214), pill: Color(hex: 0xFF1C1C1E), actionDim: Color(hex: 0xFF0A2A4A), statusBarVeil: Color(hex: 0xFF000000), systemBarVeil: Color(hex: 0xFF000000), tickWeak: Color(hex: 0xFF48484A), tickStrong: Color(hex: 0xFF8E8E93), railModeFill: Color(hex: 0xFF141416), fieldFilled: Color(hex: 0xFF1C1C1E), editorCanvasOverride: Color(hex: 0xFF000000), editorBarOverride: Color(hex: 0xFF000000), editorBarLineOverride: Color(hex: 0x14FFFFFF), editorRowPillOverride: Color(hex: 0xFF121214), editorGlyphBoxOverride: Color(hex: 0xFF1C1C1E), onBrandOverride: Color(hex: 0xFFFFFFFF))
    static let midnight = AureaPalette(id: "midnight", brandDeep: Color(hex: 0xFF15325A), brand: Color(hex: 0xFF2C66A0), accent: Color(hex: 0xFF7DB8E6), keyframe: Color(hex: 0xFFB3D9F0), background: Color(hex: 0xFF000000), surface: Color(hex: 0xFF0B0D10), surfaceHigh: Color(hex: 0xFF14171C), chip: Color(hex: 0xFF1A1E24), chipHigh: Color(hex: 0xFF2A2F37), border: Color(hex: 0xFF1E232A), muted: Color(hex: 0xFFA7B0BA), subtle: Color(hex: 0xFF77818C), onAccent: Color(hex: 0xFF05080B), accentDim: Color(hex: 0xFF15283C), keyframeDim: Color(hex: 0xFF1A3247), stage: Color(hex: 0xFF000000), hairline: Color(hex: 0xB81E232A), editorTopBar: Color(hex: 0xFF000000), editorPanel: Color(hex: 0xFF000000), editorPanelHigh: Color(hex: 0xFF0B0D10), pill: Color(hex: 0xFF14171C), actionDim: Color(hex: 0xFF102438), statusBarVeil: Color(hex: 0xFF000000), systemBarVeil: Color(hex: 0xFF000000), tickWeak: Color(hex: 0xFF3A4250), tickStrong: Color(hex: 0xFF6C788C), railModeFill: Color(hex: 0xFF15181E), fieldFilled: Color(hex: 0xFF1A1D22))
    static let graphite = AureaPalette(id: "graphite", brandDeep: Color(hex: 0xFF3A4250), brand: Color(hex: 0xFF566273), accent: Color(hex: 0xFFB7C4D3), keyframe: Color(hex: 0xFFD6DEE8), background: Color(hex: 0xFF16181C), surface: Color(hex: 0xFF1D2025), surfaceHigh: Color(hex: 0xFF25292F), chip: Color(hex: 0xFF2B3037), chipHigh: Color(hex: 0xFF3A4048), border: Color(hex: 0xFF33383F), muted: Color(hex: 0xFFB0B6BE), subtle: Color(hex: 0xFF838A94), onAccent: Color(hex: 0xFF101215), accentDim: Color(hex: 0xFF2E343C), keyframeDim: Color(hex: 0xFF343B45), stage: Color(hex: 0xFF111316), hairline: Color(hex: 0xB833383F), editorTopBar: Color(hex: 0xFF16181C), editorPanel: Color(hex: 0xFF16181C), editorPanelHigh: Color(hex: 0xFF1D2025), pill: Color(hex: 0xFF25292F), actionDim: Color(hex: 0xFF2A3038), statusBarVeil: Color(hex: 0xFF0D0E10), systemBarVeil: Color(hex: 0xFF121417), tickWeak: Color(hex: 0xFF4A515C), tickStrong: Color(hex: 0xFF7F8896), railModeFill: Color(hex: 0xFF23272D), fieldFilled: Color(hex: 0xFF2A2E34))
    static let emerald = AureaPalette(id: "emerald", brandDeep: Color(hex: 0xFF0F4A3A), brand: Color(hex: 0xFF1C7A5E), accent: Color(hex: 0xFF5BD6A8), keyframe: Color(hex: 0xFFA6ECD2), background: Color(hex: 0xFF0B1411), surface: Color(hex: 0xFF111D19), surfaceHigh: Color(hex: 0xFF172722), chip: Color(hex: 0xFF1D302A), chipHigh: Color(hex: 0xFF2C403A), border: Color(hex: 0xFF223A33), muted: Color(hex: 0xFFA6BDB5), subtle: Color(hex: 0xFF789088), onAccent: Color(hex: 0xFF06110D), accentDim: Color(hex: 0xFF163A30), keyframeDim: Color(hex: 0xFF1D4539), stage: Color(hex: 0xFF080F0C), hairline: Color(hex: 0xB8223A33), editorTopBar: Color(hex: 0xFF0B1411), editorPanel: Color(hex: 0xFF0B1411), editorPanelHigh: Color(hex: 0xFF111D19), pill: Color(hex: 0xFF172722), actionDim: Color(hex: 0xFF123326), statusBarVeil: Color(hex: 0xFF060B09), systemBarVeil: Color(hex: 0xFF09100D), tickWeak: Color(hex: 0xFF3B5249), tickStrong: Color(hex: 0xFF6C8C80), railModeFill: Color(hex: 0xFF18251F), fieldFilled: Color(hex: 0xFF1F2B27))
    static let amethyst = AureaPalette(id: "amethyst", brandDeep: Color(hex: 0xFF3A2470), brand: Color(hex: 0xFF6246B8), accent: Color(hex: 0xFFB9A2FF), keyframe: Color(hex: 0xFFDCCFFF), background: Color(hex: 0xFF110E19), surface: Color(hex: 0xFF181423), surfaceHigh: Color(hex: 0xFF201B2E), chip: Color(hex: 0xFF282238), chipHigh: Color(hex: 0xFF383049), border: Color(hex: 0xFF2F2842), muted: Color(hex: 0xFFB5ADC6), subtle: Color(hex: 0xFF877E99), onAccent: Color(hex: 0xFF0D0A14), accentDim: Color(hex: 0xFF2C2248), keyframeDim: Color(hex: 0xFF362B55), stage: Color(hex: 0xFF0C0A12), hairline: Color(hex: 0xB82F2842), editorTopBar: Color(hex: 0xFF110E19), editorPanel: Color(hex: 0xFF110E19), editorPanelHigh: Color(hex: 0xFF181423), pill: Color(hex: 0xFF201B2E), actionDim: Color(hex: 0xFF261E42), statusBarVeil: Color(hex: 0xFF08070C), systemBarVeil: Color(hex: 0xFF0D0B13), tickWeak: Color(hex: 0xFF4A4260), tickStrong: Color(hex: 0xFF7D7399), railModeFill: Color(hex: 0xFF211C2D), fieldFilled: Color(hex: 0xFF272233))
    static let sunset = AureaPalette(id: "sunset", brandDeep: Color(hex: 0xFF6A3212), brand: Color(hex: 0xFFB45A1E), accent: Color(hex: 0xFFFFB060), keyframe: Color(hex: 0xFFFFD6A8), background: Color(hex: 0xFF15100C), surface: Color(hex: 0xFF1E1712), surfaceHigh: Color(hex: 0xFF281F18), chip: Color(hex: 0xFF30251D), chipHigh: Color(hex: 0xFF40342B), border: Color(hex: 0xFF3A2C22), muted: Color(hex: 0xFFC4B4A6), subtle: Color(hex: 0xFF96867A), onAccent: Color(hex: 0xFF140C06), accentDim: Color(hex: 0xFF45280F), keyframeDim: Color(hex: 0xFF4E3218), stage: Color(hex: 0xFF100C09), hairline: Color(hex: 0xB83A2C22), editorTopBar: Color(hex: 0xFF15100C), editorPanel: Color(hex: 0xFF15100C), editorPanelHigh: Color(hex: 0xFF1E1712), pill: Color(hex: 0xFF281F18), actionDim: Color(hex: 0xFF3E230D), statusBarVeil: Color(hex: 0xFF0B0806), systemBarVeil: Color(hex: 0xFF110D0A), tickWeak: Color(hex: 0xFF5A4838), tickStrong: Color(hex: 0xFF907A66), railModeFill: Color(hex: 0xFF271E17), fieldFilled: Color(hex: 0xFF2E251E))
    static let all: [AureaPalette] = [amoled, aurea, midnight, graphite, emerald, amethyst, sunset]
    /// Id desconhecido (inclusive o "omega" salvo pela 0.3.2) cai na AUREA.
    static func of(_ id: String?) -> AureaPalette { all.first { $0.id == id } ?? amoled }
}

/// O tema em uso. Trocar exige reconstruir as telas (o modelo muda `themeId`,
/// que é a identidade da raiz).
enum AureaTheme {
    static var palette: AureaPalette = .of(UserDefaults.standard.string(forKey: "aurea.theme"))
}

// =============================================================================
// Omega fundação: PAPÉIS DAS TELAS FORA DO EDITOR (o nome "light" ficou da 0.3.2)
//
// Home, gaveta, folha "Novo projeto", página de exportação, diálogos e menus
// popup pintam SÓ com estes papéis (`AureaColors.light*`). 0.3.3: a paleta clara
// da Omega saiu; em todo tema cada papel cai no tom escuro que o tema já tem
// (texto claro, destaque do tema). Par exato de `AureaLightRoles` do
// AureaTokens.kt (mesmos nomes e valores).
// =============================================================================
struct AureaLightRoles {
    /// Verdadeiro só se os papéis forem claros. Nenhuma paleta é clara desde a 0.3.3.
    let isLight: Bool
    /// Fundo da página: abas da Home, gaveta, cabeçalhos.
    let background: Color
    /// Folha/página agrupada: "Novo projeto", página de exportação.
    let sheet: Color
    /// Superfície: cartões, menus popup, caixas suspensas, campos, barra de abas.
    let surface: Color
    /// Fundo dos diálogos.
    let dialog: Color
    /// Superfície alternativa: grupo da gaveta, linha pressionada ou escolhida.
    let surfaceAlt: Color
    /// Texto e ícone principais.
    let text: Color
    /// Texto secundário: rótulo, sublinha, valor à direita.
    let textMuted: Color
    /// Dica, placeholder e texto desabilitado.
    let textFaint: Color
    /// Fio divisório (0,5–1 pt).
    let divider: Color
    /// Preenchimento forte: segmento ativo, botão principal (Exportar).
    let ink: Color
    /// Texto e ícone sobre `ink`.
    let onInk: Color
    /// Destaque legível sobre estas superfícies: botão positivo, ✓, barra da linha escolhida.
    let accentInk: Color
    /// Ação destrutiva sobre estas superfícies.
    let danger: Color
    /// Sombra de cartões e menus.
    let shadow: Color

    /// Cada papel no tom escuro que o tema já tem.
    static func mapped(from p: AureaPalette) -> AureaLightRoles {
        AureaLightRoles(
            isLight: false,
            background: p.background, sheet: p.surface, surface: p.surfaceHigh,
            dialog: p.surfaceHigh, surfaceAlt: p.chip,
            text: Color(hex: 0xFFF7F9FB), textMuted: p.muted, textFaint: p.subtle,
            divider: p.border, ink: p.brand, onInk: p.onBrand,
            accentInk: p.accent, danger: Color(hex: 0xFFFF453A), shadow: Color(hex: 0x73000000))
    }
}

// =============================================================================
// Omega home: PAPÉIS SÓ DA HOME (área 01). O que a Home pinta e os papéis comuns
// (AureaLightRoles) não cobrem: gaveta, miniatura, barra de seleção, véu do menu
// rápido, cobertura, avatar sem login, pílulas das subabas, sombras e o selo de
// duração. 0.3.3: em todo tema, os tons escuros do tema. Par exato de
// `AureaHomeRoles` do AureaTokens.kt.
// =============================================================================
struct AureaHomeRoles {
    let drawer: Color
    let thumb: Color
    let selectionBar: Color
    let onSelectionBar: Color
    let menuScrim: Color
    let cover: Color
    let avatar: Color
    let onAvatar: Color
    let pillSelected: Color
    let onPillSelected: Color
    let pillUnselected: Color
    let onPillUnselected: Color
    let pillPressed: Color
    let onPillPressed: Color
    let shadowSoft: Color
    let shadowStrong: Color
    let badge: Color
    let onBadge: Color

    /// Temas escuros: cada papel no tom escuro que o tema já tem.
    static func mapped(from p: AureaPalette) -> AureaHomeRoles {
        AureaHomeRoles(
            drawer: p.surface, thumb: p.chip,
            selectionBar: p.chipHigh, onSelectionBar: Color(hex: 0xFFF7F9FB),
            menuScrim: p.background.opacity(0.8), cover: Color(hex: 0xB3000000),
            avatar: p.chipHigh, onAvatar: Color(hex: 0xFFF7F9FB),
            pillSelected: p.brand, onPillSelected: p.onBrand,
            pillUnselected: p.surfaceHigh, onPillUnselected: p.muted,
            pillPressed: p.chip, onPillPressed: Color(hex: 0xFFF7F9FB),
            shadowSoft: Color(hex: 0x26000000), shadowStrong: Color(hex: 0x59000000),
            badge: Color(hex: 0xB3000000), onBadge: Color(hex: 0xFFFFFFFF))
    }
}

// =============================================================================
// Omega efeitos (área 07): PAPÉIS do painel de efeitos, das linhas de parâmetro
// e do navegador de efeitos. Cada papel cai no tom que o tema já tem (0.3.3: sem
// valores próprios da Omega). Par exato de
// `AureaFxRoles` do AureaTokens.kt (mesmos nomes e valores).
// =============================================================================
struct AureaFxRoles {
    /// «painel» (A9): fundo do painel de efeitos.
    let panel: Color
    /// «superfície de lista» (S9): linha do efeito, cabeçalho e corpo dos controles, navegador.
    let listSurface: Color
    /// «superfície baixa» (S10): "+ Adicionar efeito", faixa da seção, recomendados.
    let lowSurface: Color
    /// «afundado» (A11): rótulo ativo, caixa pressionada, suspenso.
    let sunken: Color
    /// «mais escuro» (A12): grade de categorias, folha de detalhe, borda das etiquetas.
    let darkest: Color
    /// «busca» (S8) e «campo» (S6) do navegador.
    let search: Color
    let field: Color
    /// «segmento» (P11) e «segmento ativo» (P10) da escolha segmentada.
    let segment: Color
    let segmentOn: Color
    /// «rótulo» (P11 a 30 %) e «véu 20 %» (caixa de valor).
    let label: Color
    let valueVeil: Color
    /// «eixo X» (C1), «eixo Z» (L3_A) e «agulha» (C1).
    let axisX: Color
    let axisZ: Color
    let needle: Color
    /// «risco» (D1), «trilho» (P3) e trilho desabilitado.
    let tick: Color
    let track: Color
    let trackDisabled: Color
    /// «texto de dica» (S2), «texto desabilitado» (S1), «texto de cor» (P6), «rótulo de eixo» (P3).
    let tipText: Color
    let disabledText: Color
    let colorText: Color
    let axisLabel: Color
    /// «ícone de lista» (⋮ e 🗑 do cabeçalho) e «texto secundário» (P5).
    let listIcon: Color
    let textSecondary: Color
    /// Véu da folha de detalhe (preto 30 %).
    let scrim: Color

    /// Temas escuros: cada papel no tom que o tema já tem.
    static func mapped(from p: AureaPalette) -> AureaFxRoles {
        AureaFxRoles(
            // 0.3.3 tema: o tom da moldura dos painéis (acima do `sunken`).
            panel: p.editorBar, listSurface: p.chipHigh, lowSurface: p.editorRowPill,
            sunken: p.background, darkest: p.stage, search: p.border,
            field: p.chip, segment: p.chip, segmentOn: p.chipHigh,
            label: p.chip, valueVeil: Color(hex: 0x33000000),
            axisX: p.keyframe, axisZ: p.accent, needle: p.accent,
            tick: p.tickStrong, track: p.muted, trackDisabled: p.chipHigh,
            tipText: p.subtle, disabledText: p.subtle, colorText: p.muted,
            axisLabel: p.muted, listIcon: Color(hex: 0xFFF7F9FB), textSecondary: p.muted,
            scrim: Color(hex: 0x4D000000))
    }
}

// =============================================================================
// Omega conteúdo (área 06): PAPÉIS dos editores de conteúdo (modo de texto,
// navegador de fontes, desenho e Editar pontos). Cada papel cai no tom que o
// tema já tem (0.3.3: sem valores próprios da Omega).
// Par exato de `AureaContentRoles` do AureaTokens.kt (mesmos nomes e valores).
// =============================================================================
struct AureaContentRoles {
    /// «barra de ferramentas mais escura» (K2): a barra de estilo do modo de texto.
    let styleBar: Color
    /// «botão de estilo»: as peças da barra de estilo e o corpo dos popups.
    let styleButton: Color
    /// «superfície mais escura» (A1): a faixa de entrada do texto.
    let inputStrip: Color
    /// «fundo do painel de edição» (A3): o painel grande do modo de texto.
    let editPanel: Color
    /// «superfície do navegador» (S10) e «linha fina» (branco 10 %).
    let browser: Color
    let browserLine: Color
    /// «fundo do painel» (A9) e «superfície elevada» (A2): gaveta de filtros e cabeçalho.
    let drawer: Color
    let drawerHeader: Color
    /// «superfície elevada» (S10): o trackpad do Editar pontos.
    let trackpad: Color
    /// «texto secundário» (P6): texto-guia do trackpad.
    let guide: Color
    /// «linha forte» (A11): divisórias de 0,7 das colunas.
    let strongLine: Color
    /// Marcador da faixa de pontos (o escolhido usa o destaque).
    let pointMarker: Color
    /// «texto terciário»: dica do campo de texto.
    let hint: Color
    /// Pílula do valor (espessura do desenho).
    let valuePill: Color

    /// Temas escuros: cada papel no tom que o tema já tem.
    static func mapped(from p: AureaPalette) -> AureaContentRoles {
        AureaContentRoles(
            styleBar: p.stage, styleButton: p.chip, inputStrip: p.surface,
            editPanel: p.editorPanelHigh, browser: p.editorPanel, browserLine: p.border,
            drawer: p.surfaceHigh, drawerHeader: p.surface, trackpad: p.editorRowPill,
            guide: p.muted, strongLine: p.background, pointMarker: p.subtle,
            hint: p.subtle, valuePill: p.editorRowPill)
    }
}

// =============================================================================
// Cores (AureaTokens.kt)
// =============================================================================
enum AureaColors {
    static let sheetGrab = Color(hex: 0xAAB6C3).opacity(0.5)
    static let menuScrim = Color.black.opacity(0.54)
    static let actionDivider = HomeColors.sheetDivider
    static let dialogBg = HomeColors.alertFill
    static let actionSheetText = HomeColors.sheetTitle
    static let actionSheetBg = HomeColors.sheetFill
    static let actionCancelBg = Color(hex: 0x2C2C2C)
    // --- Marca ---------------------------------------------------------------
    static var brandDeep: Color { AureaTheme.palette.brandDeep }
    static var brand: Color { AureaTheme.palette.brand }
    static var accent: Color { AureaTheme.palette.accent }
    static var keyframe: Color { AureaTheme.palette.keyframe }
    static var background: Color { AureaTheme.palette.background }
    static var surface: Color { AureaTheme.palette.surface }
    static var surfaceHigh: Color { AureaTheme.palette.surfaceHigh }
    static var chip: Color { AureaTheme.palette.chip }
    static var chipHigh: Color { AureaTheme.palette.chipHigh }
    static var border: Color { AureaTheme.palette.border }
    static let text        = Color(hex: 0xF7F9FB)
    static var muted: Color { AureaTheme.palette.muted }
    static var subtle: Color { AureaTheme.palette.subtle }
    static var onAccent: Color { AureaTheme.palette.onAccent }
    static var accentDim: Color { AureaTheme.palette.accentDim }
    static var keyframeDim: Color { AureaTheme.palette.keyframeDim }
    static let danger      = Color(hex: 0xFF6B6B)
    static let warning     = Color(hex: 0xFFC978)
    static let success     = Color(hex: 0x4CD08A)
    static var stage: Color { AureaTheme.palette.stage }
    static let playhead    = Color(hex: 0xFFFFFFFF)
    static var hairline: Color { AureaTheme.palette.hairline }

    // --- Cromo do editor (AmColors) -----------------------------------------
    static var editorTopBar: Color { AureaTheme.palette.editorTopBar }
    static var editorPanel: Color { AureaTheme.palette.editorPanel }
    static var editorPanelHigh: Color { AureaTheme.palette.editorPanelHigh }
    static var pill: Color { AureaTheme.palette.pill }
    // Editor principal (redesenho 2026-09-29).
    static var editorCanvas: Color { AureaTheme.palette.editorCanvas }
    static var editorBar: Color { AureaTheme.palette.editorBar }
    static var editorBarLine: Color { AureaTheme.palette.editorBarLine }
    static var editorRowPill: Color { AureaTheme.palette.editorRowPill }
    static var editorGlyphBox: Color { AureaTheme.palette.editorGlyphBox }
    /// Ícone do transporte sem efeito (desfazer sem histórico, duplicar sem camada).
    static let transportDisabled = Color(hex: 0x4C5566)
    /// Fundo atrás do quadro da prévia.
    static let previewBackdrop = Color(hex: 0x0B0F16)
    static var action: Color { brand }
    static var onAction: Color { AureaTheme.palette.onBrand }
    static var actionDim: Color { AureaTheme.palette.actionDim }
    static var selection: Color { brandDeep }
    static var selectionText: Color { keyframe }
    static let disabled        = Color.white.opacity(0.25)
    static var statusBarVeil: Color { AureaTheme.palette.statusBarVeil }

    // --- Barra de sistema (a Home é edge-to-edge) ----------------------------
    static var systemBarVeil: Color { AureaTheme.palette.systemBarVeil }
    static let navigationBar = Color(hex: 0x000000)
    static let scrim         = Color(hex: 0x000000).opacity(0.54)
    static let sheetScrim    = Color(hex: 0x0A0E13).opacity(0.35)

    // --- Sobre imagem (cards com miniatura) ----------------------------------
    static let onImage   = Color(hex: 0xFFFFFFFF)
    static let onImage70 = Color(hex: 0xFFFFFFFF).opacity(0.70)
    static let imageScrim = Color(hex: 0x000000).opacity(0.70)

    // --- Aviso "beta" (Sobre) -------------------------------------------------
    static let beta       = Color(hex: 0xFFB020)
    static let betaFill   = Color(hex: 0xFFB020).opacity(0.13)
    static let betaBorder = Color(hex: 0xFFB020).opacity(0.33)

    // --- Realce de pressionar -------------------------------------------------
    static let pressHighlight = Color.white.opacity(0.05)
    static let rowHighlight   = Color.white.opacity(0.06)
    static let overlayPressed = Color.white.opacity(0.10)

    // --- Controles nativos (Cupertino) ---------------------------------------
    static let segmentSeparator   = Color(hex: 0x8E8E93).opacity(0.30)
    static let segmentThumbShadow = Color(hex: 0x000000).opacity(0.12)
    static var segmentTrack: Color { background }
    static var segmentThumb: Color { surfaceHigh }
    static let switchOffTrack     = Color(hex: 0x787880).opacity(0.32)

    // --- Campos ---------------------------------------------------------------
    static let field            = Color(hex: 0x000000)
    static let fieldBorder      = Color(hex: 0xFFFFFF).opacity(0.20)
    static let fieldPlaceholder = Color(hex: 0xEBEBF5).opacity(0.30)
    static var fieldFilled: Color { AureaTheme.palette.fieldFilled }
    static let fieldDialog      = Color(hex: 0x1C1C1E)

    // --- Régua (AmTickRuler) --------------------------------------------------
    static var tickWeak: Color { AureaTheme.palette.tickWeak }
    static var tickStrong: Color { AureaTheme.palette.tickStrong }

    // --- Diálogos Cupertino ----------------------------------------------------
    static let destructiveCupertino = Color(hex: 0xFF453A)
    /// O "=" com erro (`expressionColor` de `PropertyControls.kt`).
    static let expressionError = Color(hex: 0xFF6B5E)

    static var brandGradient: [Color] { [brandDeep, brand, accent] }

    // --- Painéis da A.01 (valores escritos à mão no Flutter, aqui viram token) ---
    static var railModeFill: Color { AureaTheme.palette.railModeFill }
    static let railDisabled        = Color(hex: 0x434956)
    static let controlButton       = Color(hex: 0x434A60)
    static let dialTrack           = Color(hex: 0x2E3548)
    static let dialValueBox        = Color(hex: 0x242436)
    static let curveGrid           = Color(hex: 0x34405A)
    static let curvePresetBorder   = Color(hex: 0x333B4F)
    static let effectPreviewTop    = Color(hex: 0x232B3A)
    static let effectPreviewBottom = Color(hex: 0x606F98)
    static let effectPreviewDisc   = Color(hex: 0xFF4D2D)
    static let blendThumbBottom    = Color(hex: 0xFF8A3D)
    static let blendThumbTop       = Color(hex: 0x3D9BFF)

    // --- Omega fundação: papéis das telas fora do editor (ver AureaLightRoles) ----
    // 0.3.3 tema: em todos os temas resolvem para os tons escuros da paleta
    // (fundo, superfície, texto claro, destaque). Home, gaveta, Novo projeto,
    // exportação, diálogos e menus popup pintam só com estes.
    /// Sempre falso desde a 0.3.3 (nenhuma paleta é clara); fica para quem ainda lê.
    static var lightIsLight: Bool { AureaTheme.palette.light.isLight }
    static var lightBackground: Color { AureaTheme.palette.light.background }
    static var lightSheet: Color { AureaTheme.palette.light.sheet }
    static var lightSurface: Color { AureaTheme.palette.light.surface }
    static var lightDialog: Color { AureaTheme.palette.light.dialog }
    static var lightSurfaceAlt: Color { AureaTheme.palette.light.surfaceAlt }
    static var lightText: Color { AureaTheme.palette.light.text }
    static var lightTextMuted: Color { AureaTheme.palette.light.textMuted }
    static var lightTextFaint: Color { AureaTheme.palette.light.textFaint }
    static var lightDivider: Color { AureaTheme.palette.light.divider }
    static var lightInk: Color { AureaTheme.palette.light.ink }
    static var lightOnInk: Color { AureaTheme.palette.light.onInk }
    static var lightAccentInk: Color { AureaTheme.palette.light.accentInk }
    static var lightDanger: Color { AureaTheme.palette.light.danger }
    static var lightShadow: Color { AureaTheme.palette.light.shadow }
    static var LightIsLight: Bool { lightIsLight }
    static var LightBackground: Color { lightBackground }
    static var LightSheet: Color { lightSheet }
    static var LightSurface: Color { lightSurface }
    static var LightDialog: Color { lightDialog }
    static var LightSurfaceAlt: Color { lightSurfaceAlt }
    static var LightText: Color { lightText }
    static var LightTextMuted: Color { lightTextMuted }
    static var LightTextFaint: Color { lightTextFaint }
    static var LightDivider: Color { lightDivider }
    static var LightInk: Color { lightInk }
    static var LightOnInk: Color { lightOnInk }
    static var LightAccentInk: Color { lightAccentInk }
    static var LightDanger: Color { lightDanger }
    static var LightShadow: Color { lightShadow }

    // --- Omega home: papéis só da Home (ver AureaHomeRoles) -------------------
    static var homeDrawer: Color { AureaTheme.palette.home.drawer }
    static var homeThumb: Color { AureaTheme.palette.home.thumb }
    static var homeSelectionBar: Color { AureaTheme.palette.home.selectionBar }
    static var homeOnSelectionBar: Color { AureaTheme.palette.home.onSelectionBar }
    static var homeMenuScrim: Color { AureaTheme.palette.home.menuScrim }
    static var homeCover: Color { AureaTheme.palette.home.cover }
    static var homeAvatar: Color { AureaTheme.palette.home.avatar }
    static var homeOnAvatar: Color { AureaTheme.palette.home.onAvatar }
    static var homePillSelected: Color { AureaTheme.palette.home.pillSelected }
    static var homeOnPillSelected: Color { AureaTheme.palette.home.onPillSelected }
    static var homePillUnselected: Color { AureaTheme.palette.home.pillUnselected }
    static var homeOnPillUnselected: Color { AureaTheme.palette.home.onPillUnselected }
    static var homePillPressed: Color { AureaTheme.palette.home.pillPressed }
    static var homeOnPillPressed: Color { AureaTheme.palette.home.onPillPressed }
    static var homeShadowSoft: Color { AureaTheme.palette.home.shadowSoft }
    static var homeShadowStrong: Color { AureaTheme.palette.home.shadowStrong }
    static var homeBadge: Color { AureaTheme.palette.home.badge }
    static var homeOnBadge: Color { AureaTheme.palette.home.onBadge }

    // --- Omega fundação: moldura dos painéis de propriedade (F5, PanelFrame) ----
    /// Fundo da moldura e das colunas (A9 da referência). 0.3.3 tema: o tom da
    /// barra do editor, um degrau acima do fundo; no AUREA o `editorPanel` é
    /// igual ao `background`, e as divisórias e a aba ativa sumiriam.
    static var panelFrameBackground: Color { AureaTheme.palette.editorBar }
    /// Véu do conteúdo central: branco 10 % sobre a moldura.
    static let panelFrameContent = Color(hex: 0x1AFFFFFF)
    /// Divisórias de 0,7 e fundo da aba ativa (A11).
    static var panelFrameDivider: Color { AureaTheme.palette.background }
    /// Barra do painel de propriedade, mais clara que a da camada (S8).
    static var panelBar: Color { AureaTheme.palette.border }
    static var PanelFrameBackground: Color { panelFrameBackground }
    static let PanelFrameContent = panelFrameContent
    static var PanelFrameDivider: Color { panelFrameDivider }
    static var PanelBar: Color { panelBar }

    // Omega painel: (área 04) papéis do painel da camada da referência (par do Android).
    /// Fundo do painel da camada (A9). 0.3.3 tema: o mesmo tom da moldura (ver `panelFrameBackground`).
    static var layerPanel: Color { AureaTheme.palette.editorBar }
    /// Pílula do trio de tempo e cartão do painel embutido (S8).
    static var layerPanelButton: Color { AureaTheme.palette.border }
    /// Botão pressionado (A11).
    static var layerPanelPressed: Color { AureaTheme.palette.background }
    /// Corpo dos painéis embutidos de velocidade e volume (A10).
    static var layerPanelBody: Color { AureaTheme.palette.surface }
    /// Botões de retempo (S9).
    static var layerPanelRetime: Color { AureaTheme.palette.chipHigh }
    /// Ícone e rótulo desabilitados (S1).
    static var layerPanelDisabled: Color { AureaTheme.palette.tickStrong }
    /// Caixa de valor dos painéis embutidos (branco 5 %).
    static let layerPanelValueBox = Color(hex: 0x0DFFFFFF)
    /// Fundo do gráfico do editor de curva (P10).
    static var curveGraphBackground: Color { AureaTheme.palette.curveGraph }
    /// Coluna de presets e grupos do editor de curva (S9).
    static var curveColumn: Color { AureaTheme.palette.chipHigh }

    // --- Omega efeitos (área 07): papéis do painel de efeitos e do navegador ----
    // Ver `AureaFxRoles` (par dos `AureaColors.Fx*` do AureaTokens.kt).
    static var fxPanel: Color { AureaTheme.palette.fx.panel }
    static var fxListSurface: Color { AureaTheme.palette.fx.listSurface }
    static var fxLowSurface: Color { AureaTheme.palette.fx.lowSurface }
    static var fxSunken: Color { AureaTheme.palette.fx.sunken }
    static var fxDarkest: Color { AureaTheme.palette.fx.darkest }
    static var fxSearch: Color { AureaTheme.palette.fx.search }
    static var fxField: Color { AureaTheme.palette.fx.field }
    static var fxSegment: Color { AureaTheme.palette.fx.segment }
    static var fxSegmentOn: Color { AureaTheme.palette.fx.segmentOn }
    static var fxLabel: Color { AureaTheme.palette.fx.label }
    static var fxValueVeil: Color { AureaTheme.palette.fx.valueVeil }
    static var fxAxisX: Color { AureaTheme.palette.fx.axisX }
    static var fxAxisZ: Color { AureaTheme.palette.fx.axisZ }
    static var fxNeedle: Color { AureaTheme.palette.fx.needle }
    static var fxTick: Color { AureaTheme.palette.fx.tick }
    static var fxTrack: Color { AureaTheme.palette.fx.track }
    static var fxTrackDisabled: Color { AureaTheme.palette.fx.trackDisabled }
    static var fxTipText: Color { AureaTheme.palette.fx.tipText }
    static var fxDisabledText: Color { AureaTheme.palette.fx.disabledText }
    static var fxColorText: Color { AureaTheme.palette.fx.colorText }
    static var fxAxisLabel: Color { AureaTheme.palette.fx.axisLabel }
    static var fxListIcon: Color { AureaTheme.palette.fx.listIcon }
    static var fxTextSecondary: Color { AureaTheme.palette.fx.textSecondary }
    static var fxScrim: Color { AureaTheme.palette.fx.scrim }
    // Omega conteúdo (área 06): papéis dos editores de conteúdo (ver `AureaContentRoles`;
    // par dos `AureaColors.Content*` do AureaTokens.kt).
    static var contentStyleBar: Color { AureaTheme.palette.content.styleBar }
    static var contentStyleButton: Color { AureaTheme.palette.content.styleButton }
    static var contentInputStrip: Color { AureaTheme.palette.content.inputStrip }
    static var contentEditPanel: Color { AureaTheme.palette.content.editPanel }
    static var contentBrowser: Color { AureaTheme.palette.content.browser }
    static var contentBrowserLine: Color { AureaTheme.palette.content.browserLine }
    static var contentDrawer: Color { AureaTheme.palette.content.drawer }
    static var contentDrawerHeader: Color { AureaTheme.palette.content.drawerHeader }
    static var contentTrackpad: Color { AureaTheme.palette.content.trackpad }
    static var contentGuide: Color { AureaTheme.palette.content.guide }
    static var contentStrongLine: Color { AureaTheme.palette.content.strongLine }
    static var contentPointMarker: Color { AureaTheme.palette.content.pointMarker }
    static var contentHint: Color { AureaTheme.palette.content.hint }
    static var contentValuePill: Color { AureaTheme.palette.content.valuePill }

    // --- Timeline da A.01 (AureaTimeline.kt) ---------------------------------
    static let tickMajor  = Color(hex: 0x8A97AD)
    static let tickMinor  = Color(hex: 0x5A6880)
    static let keyframeOn = Color(hex: 0xFFC107)
    static let trimHandle = Color(hex: 0xF2F5F9)
    /// Trilho da barra de tempo (branco 22 %, `ShellColors.Track22`).
    static let track = Color.white.opacity(0.22)

    /// Paleta das etiquetas de camada (`ShellColors.LabelPalette`).
    static let labelPalette: [Color] = [
        Color(hex: 0xE85B81), Color(hex: 0xFFB020), accent, Color(hex: 0x2BE3A0),
        Color(hex: 0x35C4E7), keyframe, Color(hex: 0x3D7BFF), Color(hex: 0xFF7A3D),
        Color(hex: 0xFF4D5E), Color(hex: 0xFFE14D), Color(hex: 0xB0B8C4), Color(hex: 0x4A5160),
    ]

    // --- Espelho PascalCase dos nomes do Kotlin (o mesmo token, dois nomes) ---
    static var BrandDeep: Color { brandDeep }
    static var Brand: Color { brand }
    static var Accent: Color { accent }
    static var Keyframe: Color { keyframe }
    static var Background: Color { background }
    static var Surface: Color { surface }
    static var SurfaceHigh: Color { surfaceHigh }
    static let Text = text
    static var Chip: Color { chip }
    static var ChipHigh: Color { chipHigh }
    static var Border: Color { border }
    static var Muted: Color { muted }
    static var Subtle: Color { subtle }
    static var OnAccent: Color { onAccent }
    static var AccentDim: Color { accentDim }
    static let Danger = danger, Warning = warning, Success = success
    static var KeyframeDim: Color { keyframeDim }
    static let Playhead = playhead
    static var Stage: Color { stage }
    static var Hairline: Color { hairline }
    static var EditorTopBar: Color { editorTopBar }
    static var EditorPanel: Color { editorPanel }
    static var EditorPanelHigh: Color { editorPanelHigh }
    static var OnAction: Color { onAction }
    static var Pill: Color { pill }
    static var Action: Color { action }
    static var ActionDim: Color { actionDim }
    static let Disabled = disabled
    static var Selection: Color { selection }
    static var SelectionText: Color { selectionText }
    static let NavigationBar = navigationBar
    static var StatusBarVeil: Color { statusBarVeil }
    static var SystemBarVeil: Color { systemBarVeil }
    static let Scrim = scrim, SheetScrim = sheetScrim
    static let OnImage = onImage, OnImage70 = onImage70, ImageScrim = imageScrim
    static let Beta = beta, BetaFill = betaFill, BetaBorder = betaBorder
    static let PressHighlight = pressHighlight, RowHighlight = rowHighlight, OverlayPressed = overlayPressed
    static let SegmentSeparator = segmentSeparator, SegmentThumbShadow = segmentThumbShadow
    static let SwitchOffTrack = switchOffTrack
    static var SegmentTrack: Color { segmentTrack }
    static var SegmentThumb: Color { segmentThumb }
    static let Field = field, FieldBorder = fieldBorder, FieldPlaceholder = fieldPlaceholder
    static let FieldDialog = fieldDialog
    static var FieldFilled: Color { fieldFilled }
    static var TickWeak: Color { tickWeak }
    static var TickStrong: Color { tickStrong }
    static let DestructiveCupertino = destructiveCupertino
    static var BrandGradient: [Color] { brandGradient }
    static let RailDisabled = railDisabled, ControlButton = controlButton
    static var RailModeFill: Color { railModeFill }
    static let DialTrack = dialTrack, DialValueBox = dialValueBox, CurveGrid = curveGrid
    static let CurvePresetBorder = curvePresetBorder
    static let EffectPreviewTop = effectPreviewTop, EffectPreviewBottom = effectPreviewBottom
    static let EffectPreviewDisc = effectPreviewDisc
    static let BlendThumbBottom = blendThumbBottom, BlendThumbTop = blendThumbTop
    static let TickMajor = tickMajor, TickMinor = tickMinor, KeyframeOn = keyframeOn, TrimHandle = trimHandle
    static let Track = track, LabelPalette = labelPalette
}

extension Color {
    /// Tokens RGB de 6 digitos sao opacos; tokens ARGB de 8 digitos
    /// preservam o canal alfa explicito do Android.
    init(hex: UInt32) {
        let a = hex <= 0x00FF_FFFF ? 1.0 : Double((hex >> 24) & 0xFF) / 255.0
        let r = Double((hex >> 16) & 0xFF) / 255.0
        let g = Double((hex >> 8) & 0xFF) / 255.0
        let b = Double(hex & 0xFF) / 255.0
        self.init(.sRGB, red: r, green: g, blue: b, opacity: a)
    }
}

// =============================================================================
// Cores que só a casca do editor usa (ShellTokens.kt, `ShellColors`)
// =============================================================================
enum ShellColors {
    /// `CromoEditor.apagado`: o tempo da barra do projeto (branco 40 %).
    static let white40         = Color.white.opacity(0.40)
    static let dockRow         = Color(hex: 0x1E222D)
    static let dockTile        = Color(hex: 0x222634)
    static let dockTileContent = Color(hex: 0xD4D8E2)
    static let badgeNew        = Color(hex: 0xFFD600)
    static let fab             = Color(hex: 0x1E2130)
    static let fabShadow       = Color(hex: 0x000000).opacity(0.45)
    static let floatingDark    = Color(hex: 0x12151A).opacity(0.80)
    static let resolutionChip  = Color(hex: 0x171D25).opacity(0.80)
    static let outlineUnder    = Color(hex: 0x0A0E13).opacity(0.55)
    static let snapLine        = Color(hex: 0xFF6B6B).opacity(0.80)
    static let busyVeil        = Color(hex: 0x17191D).opacity(0.87)
    static let objectCard      = Color(hex: 0x0D0E12)
    static let shapeTile       = Color(hex: 0x000000)
    static let shapeFill       = Color(hex: 0x9E9E9E)
    static let pageDotOff      = Color(hex: 0x484E5C)
    static let camera3D        = Color(hex: 0x8BD5FF)
    static let text3D          = Color(hex: 0xFFD36B)
    static let phone3D         = Color(hex: 0xC9CDD4)
    static let track22         = Color.white.opacity(0.22)
    static let menuHandle      = Color(hex: 0xAAB6C3).opacity(0.40)
    static let sheetHandle     = Color(hex: 0xAAB6C3).opacity(0.50)
    static let menuScrim       = Color(hex: 0x000000).opacity(0.54)
    static let settingsScrim   = Color(hex: 0x000000).opacity(0.38)
    static let popupScrim      = Color(hex: 0x0A0E13).opacity(0.25)
    static let disabledMuted   = Color(hex: 0xAAB6C3).opacity(0.60)
    static let swatch          = Color.white.opacity(0.24)
    static let navigationBar   = Color(hex: 0x000000)
    static let accentHalf      = Color(hex: 0x6FAED9).opacity(0.50)
    static let labelPalette    = AureaColors.labelPalette

    static let White40 = white40, DockRow = dockRow, DockTile = dockTile
    static let DockTileContent = dockTileContent, BadgeNew = badgeNew, Fab = fab
    static let FabShadow = fabShadow, FloatingDark = floatingDark, ResolutionChip = resolutionChip
    static let OutlineUnder = outlineUnder, SnapLine = snapLine, BusyVeil = busyVeil
    static let ObjectCard = objectCard, ShapeTile = shapeTile, ShapeFill = shapeFill
    static let PageDotOff = pageDotOff, Camera3D = camera3D, Text3D = text3D, Phone3D = phone3D
    static let Track22 = track22, MenuHandle = menuHandle, SheetHandle = sheetHandle
    static let MenuScrim = menuScrim, SettingsScrim = settingsScrim, PopupScrim = popupScrim
    static let DisabledMuted = disabledMuted, Swatch = swatch, NavigationBar = navigationBar
    static let AccentHalf = accentHalf, LabelPalette = labelPalette
}

// =============================================================================
// Tipografia (AureaType.kt)
//
//  O Flutter do app antigo faz TODO texto herdar 15 sp, −0,1 de espaçamento e
//  altura de linha 1,35 (bodyMedium do M3). Sem essa base as alturas dos
//  componentes não batem com os prints.
//
//  `AureaType` guarda a FONTE (o que a casca já usava: `.font(AureaType.x)`).
//  `AureaTextSpec` guarda a fonte + cor + espaçamento + altura de linha, para
//  quem precisa do estilo inteiro de uma vez (`.aureaText(.Value)`).
// =============================================================================
enum AureaType {
    // A base de tudo (15 sp, −0,1, altura 1,35).
    static let base = AureaTextSpec.base

    // --- Escala (nomes do Kotlin) --------------------------------------------
    static let Display       = AureaTextSpec.display.font
    static let HeadlineLarge = AureaTextSpec.headlineLarge.font
    static let ScreenTitle   = AureaTextSpec.screenTitle.font
    static let TitleLarge    = AureaTextSpec.titleLarge.font
    static let TitleMedium   = AureaTextSpec.titleMedium.font
    static let TitleSmall    = AureaTextSpec.titleSmall.font
    static let BodyLarge     = AureaTextSpec.bodyLarge.font
    static let BodySmall     = AureaTextSpec.bodySmall.font
    static let LabelLarge    = AureaTextSpec.labelLarge.font

    // --- Home / telas fora do editor -----------------------------------------
    static let Greeting        = AureaTextSpec.greeting.font
    static let TabLabel        = AureaTextSpec.tabLabel.font
    static let Button          = AureaTextSpec.button.font
    static let ShortcutLabel   = AureaTextSpec.shortcutLabel.font
    static let HeroKicker      = AureaTextSpec.heroKicker.font
    static let HeroTitle       = AureaTextSpec.heroTitle.font
    static let HeroSpec        = AureaTextSpec.heroSpec.font
    static let HeroPill        = AureaTextSpec.heroPill.font
    static let ListCount       = AureaTextSpec.listCount.font
    static let CardTitle       = AureaTextSpec.cardTitle.font
    static let CardSpec        = AureaTextSpec.cardSpec.font
    static let LinkRow         = AureaTextSpec.linkRow.font
    static let FeatureTitle    = AureaTextSpec.featureTitle.font
    static let FeatureSubtitle = AureaTextSpec.featureSubtitle.font
    static let Empty           = AureaTextSpec.empty.font
    static let BatchCount      = AureaTextSpec.batchCount.font
    static let BatchAction     = AureaTextSpec.batchAction.font
    static let BatchDanger     = AureaTextSpec.batchDanger.font
    static let SearchText      = AureaTextSpec.searchText.font
    static let SearchPlaceholder = AureaTextSpec.searchPlaceholder.font
    static let Note            = AureaTextSpec.note.font
    static let Footer          = AureaTextSpec.footer.font
    static let Pill            = AureaTextSpec.pill.font
    static let ChipLabel       = AureaTextSpec.chipLabel.font
    static let Segment         = AureaTextSpec.segment.font
    static let DialogField     = AureaTextSpec.dialogField.font
    static let VersionPill     = AureaTextSpec.versionPill.font
    static let BetaTitle       = AureaTextSpec.betaTitle.font
    static let BetaBody        = AureaTextSpec.betaBody.font

    // --- Folha "Novo projeto" -------------------------------------------------
    static let SheetSpec       = AureaTextSpec.sheetSpec.font
    static let FrameLabel      = AureaTextSpec.frameLabel.font
    static let FrameHint       = AureaTextSpec.frameHint.font
    static let FormatLabel     = AureaTextSpec.formatLabel.font
    static let FormatHint      = AureaTextSpec.formatHint.font
    static let Caps            = AureaTextSpec.caps.font
    static let NameField       = AureaTextSpec.nameField.font
    static let NamePlaceholder = AureaTextSpec.namePlaceholder.font
    static let DimLabel        = AureaTextSpec.dimLabel.font
    static let DimField        = AureaTextSpec.dimField.font
    static let Times           = AureaTextSpec.times.font

    // --- Editor (AureaEstilos) ------------------------------------------------
    static let EditorTitle = AureaTextSpec.editorTitle.font
    static let Property    = AureaTextSpec.property.font
    static let Value       = AureaTextSpec.value.font
    static let Label       = AureaTextSpec.labelStyle.font
    static let Section     = AureaTextSpec.section.font
    static let Body        = AureaTextSpec.bodyStyle.font

    // --- Apelidos em camelCase que a casca Swift já usava ---------------------
    static let title     = Font.aurea(size: 22, weight: .semibold, design: .rounded)
    static let section   = AureaTextSpec.editorSection.font
    static let body      = AureaTextSpec.bodyStyle.font
    static let label     = AureaTextSpec.rowLabel.font
    static let value     = Font.aurea(size: 13, weight: .medium, design: .monospaced)
    static let tiny      = Font.aurea(size: 11, weight: .medium)
    static let tabLabel  = AureaTextSpec.tabLabel.font

    /// Número tabular (tnum do Kotlin): algarismos de largura fixa.
    static func tabular(_ size: CGFloat = 14, weight: Font.Weight = .bold) -> Font {
        .aurea(size: size, weight: weight, design: .monospaced)
    }
}

/// Fonte + cor + espaçamento + altura de linha de um estilo do Kotlin.
struct AureaTextSpec {
    let font: Font
    let size: CGFloat
    let color: Color
    let tracking: CGFloat
    let lineHeight: CGFloat

    /// Deriva um estilo da BASE (o `merge` que o Flutter herdava): espaçamento
    /// −0,1 e altura de linha 1,35 quando não dito.
    static func of(_ size: CGFloat, _ weight: Font.Weight = .regular,
                   tracking: CGFloat = -0.1, color: Color = AureaColors.text,
                   lineHeight: CGFloat = 1.35) -> AureaTextSpec {
        AureaTextSpec(font: .aurea(size: size, weight: weight), size: size,
                      color: color, tracking: tracking, lineHeight: lineHeight)
    }

    // --- Escala ---------------------------------------------------------------
    static let base          = AureaTextSpec.of(15)
    static let display       = AureaTextSpec.of(30, .heavy, tracking: -0.8, lineHeight: 1.05)
    static let headlineLarge = AureaTextSpec.of(34, .bold, tracking: -0.8, lineHeight: 1.1)
    static let screenTitle   = AureaTextSpec.of(21, .bold, tracking: -0.4)
    static let titleLarge    = AureaTextSpec.of(22, .bold, tracking: -0.5, lineHeight: 1.27)
    static let titleMedium   = AureaTextSpec.of(17, .semibold, tracking: -0.3, lineHeight: 1.5)
    static let titleSmall    = AureaTextSpec.of(17, .bold, tracking: -0.4)
    static let bodyLarge     = AureaTextSpec.of(17, tracking: -0.2, lineHeight: 1.5)
    static let bodySmall     = AureaTextSpec.of(13, tracking: 0, color: AureaColors.muted, lineHeight: 1.33)
    static let labelLarge    = AureaTextSpec.of(17, .semibold, tracking: -0.2, lineHeight: 1.43)

    // --- Home / telas fora do editor -----------------------------------------
    static let greeting        = AureaTextSpec.of(13.5, color: AureaColors.muted)
    static let tabLabel        = AureaTextSpec.of(10.5, .medium, tracking: 0.1)
    static let button          = AureaTextSpec.of(17, .semibold, tracking: -0.2, color: AureaColors.onAccent)
    static let shortcutLabel   = AureaTextSpec.of(12, .semibold)
    static let heroKicker      = AureaTextSpec.of(11, .bold, tracking: 0.4, color: AureaColors.accent)
    static let heroTitle       = AureaTextSpec.of(18, .bold, tracking: -0.2, color: AureaColors.onImage)
    static let heroSpec        = AureaTextSpec.of(11, color: AureaColors.onImage70)
    static let heroPill        = AureaTextSpec.of(12.5, .bold, color: AureaColors.onAccent)
    static let listCount       = AureaTextSpec.of(15, .bold)
    static let cardTitle       = AureaTextSpec.of(14, .semibold, tracking: -0.1)
    static let cardSpec        = AureaTextSpec.of(11, color: AureaColors.muted)
    static let linkRow         = AureaTextSpec.of(14.5)
    static let featureTitle    = AureaTextSpec.of(15, .semibold)
    static let featureSubtitle = AureaTextSpec.of(12.5, color: AureaColors.muted)
    static let empty           = AureaTextSpec.of(13.5, color: AureaColors.muted)
    static let batchCount      = AureaTextSpec.of(13, color: AureaColors.muted)
    static let batchAction     = AureaTextSpec.of(13)
    static let batchDanger     = AureaTextSpec.of(13, color: AureaColors.danger)
    static let searchText      = AureaTextSpec.of(17)
    static let searchPlaceholder = AureaTextSpec.of(17, color: AureaColors.fieldPlaceholder)
    static let note            = AureaTextSpec.of(12.5, color: AureaColors.muted)
    static let footer          = AureaTextSpec.of(11, color: AureaColors.subtle)
    static let pill            = AureaTextSpec.of(12, .semibold, color: AureaColors.accent)
    static let chipLabel       = AureaTextSpec.of(12, .semibold)
    static let segment         = AureaTextSpec.of(13, .semibold, tracking: -0.1)
    static let dialogField     = AureaTextSpec.of(15)
    static let versionPill     = AureaTextSpec.of(12, .semibold, color: AureaColors.accent)
    static let betaTitle       = AureaTextSpec.of(13, .bold, color: AureaColors.beta)
    static let betaBody        = AureaTextSpec.of(11, color: AureaColors.muted)

    // --- Folha "Novo projeto" -------------------------------------------------
    static let sheetSpec       = AureaTextSpec.of(12.5, color: AureaColors.muted)
    static let frameLabel      = AureaTextSpec.of(22, .bold, tracking: -0.3)
    static let frameHint       = AureaTextSpec.of(12, color: AureaColors.muted)
    static let formatLabel     = AureaTextSpec.of(12.5, .semibold, tracking: -0.1)
    static let formatHint      = AureaTextSpec.of(10, color: AureaColors.muted)
    static let caps            = AureaTextSpec.of(12, .medium, tracking: 0.6, color: AureaColors.muted)
    static let nameField       = AureaTextSpec.of(17, tracking: -0.2)
    static let namePlaceholder = AureaTextSpec.of(17, tracking: -0.2, color: AureaColors.muted)
    static let dimLabel        = AureaTextSpec.of(11, color: AureaColors.muted)
    static let dimField        = AureaTextSpec.of(16)
    static let times           = AureaTextSpec.of(16)

    // --- Editor (AureaEstilos) ------------------------------------------------
    static let editorTitle = AureaTextSpec.of(14, .semibold)
    static let property    = AureaTextSpec.of(12.5, color: AureaColors.muted)
    static let value       = AureaTextSpec(font: .aurea(size: 14, weight: .bold).monospacedDigit(),
                                           size: 14, color: AureaColors.keyframe,
                                           tracking: 0, lineHeight: 1.35)
    static let labelStyle  = AureaTextSpec.of(10, color: AureaColors.muted)
    static let section     = AureaTextSpec.of(11, .semibold, tracking: 0.3, color: AureaColors.muted)
    static let bodyStyle   = AureaTextSpec.of(13)

    // --- Peças da casca -------------------------------------------------------
    /// Nome do parâmetro de uma linha de propriedade (12 sp w600 muted).
    static let rowLabel = AureaTextSpec.of(12, .semibold, color: AureaColors.muted)
    /// Título de seção de painel (13 sp w700 muted).
    static let editorSection = AureaTextSpec.of(13, .bold, color: AureaColors.muted)
}

extension View {
    /// Aplica um estilo do catálogo (fonte, cor, espaçamento e altura de linha).
    func aureaText(_ spec: AureaTextSpec) -> some View {
        self.font(spec.font)
            .foregroundStyle(spec.color)
            .tracking(aureaTracking(spec.tracking))
            .lineSpacing(max(0, spec.lineHeight * spec.size - (UIFont(name: "Roboto-Regular", size: spec.size)?.lineHeight ?? spec.size)))
    }
    /// Same typography metrics without overriding a selected/disabled color.
    func aureaFont(_ spec: AureaTextSpec) -> some View {
        let fontHeight = UIFont(name: "Roboto-Regular", size: spec.size)?.lineHeight ?? spec.size
        let extra = max(0, spec.lineHeight * spec.size - fontHeight)
        return self.font(spec.font).tracking(aureaTracking(spec.tracking)).lineSpacing(extra).padding(.vertical, extra / 2)
    }
}

// =============================================================================
// Medidas (AureaDims + ShellDims)
// =============================================================================
enum AureaDims {
    // --- Espaço ---------------------------------------------------------------
    static let s1: CGFloat = 4
    static let s2: CGFloat = 8
    static let s3: CGFloat = 12
    static let s4: CGFloat = 16
    static let s5: CGFloat = 24
    static let s6: CGFloat = 32
    static let minTap: CGFloat = 44
    /// Recuo lateral das telas fora do editor.
    static let gutter: CGFloat = 20
    static let roundTarget: CGFloat = 44
    static let roundCircle: CGFloat = 36
    static let listEndSpace: CGFloat = 120
    static let pad: CGFloat = 14

    // --- Raios ----------------------------------------------------------------
    static let radiusXs: CGFloat = 5
    static let radiusSm: CGFloat = 8
    static let radiusChip: CGFloat = 10
    static let radiusCard: CGFloat = 12
    static let radiusMd: CGFloat = 14
    static let radiusLg: CGFloat = 16
    static let radiusSheet: CGFloat = 18
    static let radiusXl: CGFloat = 20
    static let radiusClip: CGFloat = 8
    static let radiusPill: CGFloat = 999
    static let corner: CGFloat = 10

    // --- Ícones ---------------------------------------------------------------
    static let iconXs: CGFloat = 13
    static let iconSm: CGFloat = 16
    static let iconMd: CGFloat = 20
    static let iconLg: CGFloat = 24
    static let iconXl: CGFloat = 32

    // --- Controles ------------------------------------------------------------
    static let buttonHeight: CGFloat = 54
    static let controlRow: CGFloat = 54
    static let searchField: CGFloat = 40
    static let chipHeight: CGFloat = 34
    static let tabBarHeight: CGFloat = 54
    static let compactBarHeight: CGFloat = 52
    static let compactBarThreshold: CGFloat = 64
    static let switchWidth: CGFloat = 59
    static let switchHeight: CGFloat = 39

    // --- Casca do editor (ShellDims + EditorLayout) ---------------------------
    static let hairline: CGFloat = 1
    static let hairlineThin: CGFloat = 0.5
    static let selectionStroke: CGFloat = 2
    static let multiSelectionStroke: CGFloat = 1.5
    static let topBar: CGFloat = 44
    static let transport: CGFloat = 46
    static let strip: CGFloat = 8
    static let panelHeader: CGFloat = 44
    static let sheetHandle: CGFloat = 12
    static let fullscreenTimeBar: CGFloat = 44
    static let stageInset: CGFloat = 8
    static let fab: CGFloat = 52
    static let fabMargin: CGFloat = 18
    static let touchSlop: CGFloat = 18
    static let handleSlop: CGFloat = 4
    static let snapTolerance: CGFloat = 10
    static let hitSlack: CGFloat = 12
    static let axisHandleTarget: CGFloat = 26

    // --- Régua de riscos (AmTickRuler) ----------------------------------------
    static let tickStep: CGFloat = 9
    static let tickFade: CGFloat = 24
    static let tickRulerHeight: CGFloat = 40
    static let tickRulerPad: CGFloat = 8

    // --- Linha de propriedade -------------------------------------------------
    static let propertyRowHeight: CGFloat = 48
    static let labelChipW: CGFloat = 94
    static let labelChipH: CGFloat = 32
    static let labelChipPadH: CGFloat = 6
    static let valueBoxH: CGFloat = 24
    static let valueBoxW: CGFloat = 60
    static let valueBoxRadius: CGFloat = 8
    static let diamondIcon: CGFloat = 22
    static let curveIcon: CGFloat = 20
    static let colorWell: CGFloat = 30
    static let colorWellRadius: CGFloat = 6

    // --- Interruptor e chips --------------------------------------------------
    static let toggleW: CGFloat = 51
    static let toggleH: CGFloat = 31
    static let toggleKnob: CGFloat = 27
    static let toggleTravel: CGFloat = 22
    static let togglePad: CGFloat = 2
    static let toggleRadius: CGFloat = 15.5
    static let chipRadius: CGFloat = 8
    static let chipPadH: CGFloat = 11
    static let chipPadV: CGFloat = 6

    // --- Cromo de painel ------------------------------------------------------
    static let railW: CGFloat = 46
    static let railModeW: CGFloat = 40
    static let railCell: CGFloat = 36
    static let railModeRadius: CGFloat = 8
    static let railModeStroke: CGFloat = 1.5
    static let railMoreDot: CGFloat = 6
    /// O ponto do `⋯` aceso, medido da referência (o `padding(start: 26,
    /// bottom: 16)` do Android sobre um alvo de 44).
    static let railMoreDotX: CGFloat = 13
    static let railMoreDotY: CGFloat = -9
    static let chromeButtonW: CGFloat = 40
    static let chromeButtonH: CGFloat = 44
    static let chromeGlyph: CGFloat = 21
    static let chromeGlyphSm: CGFloat = 19
    static let panelBackIcon: CGFloat = 26
    static let panelBackTarget: CGFloat = 48
    static let paramTabH: CGFloat = 48
    static let paramTabRadius: CGFloat = 9
    static let paramTabPadH: CGFloat = 8
    static let paramTabPadV: CGFloat = 6
    static let paramTabGap: CGFloat = 3
    static let paramTabDot: CGFloat = 5
    static let noticePadV: CGFloat = 10

    // --- Folhas, diálogos e menus ---------------------------------------------
    static let sheetRadius: CGFloat = 18
    static let adjustRadius: CGFloat = 13.5
    static let sheetGrabW: CGFloat = 36
    static let sheetGrabMenu: CGFloat = 5
    static let sheetGrabAdjust: CGFloat = 4
    static let sheetGrabRadius: CGFloat = 3
    static let actionSheetRadius: CGFloat = 14
    static let actionSheetMargin: CGFloat = 8
    static let actionRowMinH: CGFloat = 57
    static let dialogW: CGFloat = 270
    static let dialogRadius: CGFloat = 14
    static let dialogRowH: CGFloat = 45
    static let dialogDivider: CGFloat = 0.3
    static let dialogPadH: CGFloat = 16
    static let dialogPadTop: CGFloat = 19
    static let dialogPadBottom: CGFloat = 16
    static let nameFieldRadius: CGFloat = 7
    static let timeFieldRadius: CGFloat = 7
    static let menuRowMinH: CGFloat = 48
    static let menuIcon: CGFloat = 20
    static let menuIconGap: CGFloat = 14
    static let menuCheck: CGFloat = 18
    static let menuSectionPadH: CGFloat = 20
    static let menuSectionPadTop: CGFloat = 14
    static let menuSectionPadBottom: CGFloat = 4
    static let popupW: CGFloat = 250
    static let popupRowH: CGFloat = 40
    static let popupRadius: CGFloat = 8
    static let popupPadV: CGFloat = 4
    static let popupBarW: CGFloat = 4
    static let popupBarH: CGFloat = 24
    static let popupBarGap: CGFloat = 10
    static let popupMargin: CGFloat = 8

    // --- Teclado numérico e seletor de cor ------------------------------------
    static let keypadCellH: CGFloat = 48
    static let keypadRadius: CGFloat = 10
    static let keypadFont: CGFloat = 21
    static let keypadGap: CGFloat = 3
    static let keypadRowGap: CGFloat = 6
    static let keypadDisplayRadius: CGFloat = 12
    static let keypadDisplayFont: CGFloat = 26
    static let keypadDisplayPadH: CGFloat = 14
    static let keypadDisplayPadV: CGFloat = 12
    static let keypadHintH: CGFloat = 22
    static let keypadButtonPad: CGFloat = 12
    static let colorBoardH: CGFloat = 170
    static let colorStripH: CGFloat = 26
    static let colorStripTarget: CGFloat = 30
    static let colorStripHandle: CGFloat = 7
    static let colorStripStroke: CGFloat = 2.5
    static let colorSampleW: CGFloat = 76
    static let colorSampleH: CGFloat = 28
    static let colorCell: CGFloat = 30
    static let colorHexBoxW: CGFloat = 96
    static let colorAlphaBoxW: CGFloat = 52
    static let checkerCell: CGFloat = 6

    // --- Indicador de atividade -----------------------------------------------
    static let activitySize: CGFloat = 22

    // --- Espelho PascalCase (o mesmo valor, o nome do Kotlin) -----------------
    static let S1 = s1, S2 = s2, S3 = s3, S4 = s4, S5 = s5, S6 = s6
    static let MinTap = minTap, Gutter = gutter, RoundTarget = roundTarget, RoundCircle = roundCircle
    static let ListEndSpace = listEndSpace, Pad = pad
    static let RadiusXs = radiusXs, RadiusSm = radiusSm, RadiusChip = radiusChip
    static let RadiusCard = radiusCard, RadiusMd = radiusMd, RadiusLg = radiusLg
    static let RadiusSheet = radiusSheet, RadiusXl = radiusXl, RadiusClip = radiusClip
    static let RadiusPill = radiusPill
    static let IconXs = iconXs, IconSm = iconSm, IconMd = iconMd, IconLg = iconLg, IconXl = iconXl
    static let ButtonHeight = buttonHeight, ControlRow = controlRow, SearchField = searchField
    static let ChipHeight = chipHeight, TabBarHeight = tabBarHeight
    static let CompactBarHeight = compactBarHeight, CompactBarThreshold = compactBarThreshold
    static let SwitchWidth = switchWidth, SwitchHeight = switchHeight
    static let Hairline = hairline, HairlineThin = hairlineThin
    static let SelectionStroke = selectionStroke, MultiSelectionStroke = multiSelectionStroke
    static let EditorTopBar = topBar, EditorTransport = transport, PreviewResizeStrip = strip
    static let PanelHeader = panelHeader
    // ShellDims (Kotlin `ShellDims`)
    static let TopBar = topBar, Transport = transport, Strip = strip, SheetHandle = sheetHandle
    static let FullscreenTimeBar = fullscreenTimeBar, StageInset = stageInset
    static let Fab = fab, FabMargin = fabMargin, TouchSlop = touchSlop, HandleSlop = handleSlop
    static let SnapTolerance = snapTolerance, HitSlack = hitSlack
    static let AxisHandleTarget = axisHandleTarget
    // Régua e linha de propriedade
    static let TickStep = tickStep, TickFade = tickFade
    static let PropertyRowHeight = propertyRowHeight, LabelChipW = labelChipW, LabelChipH = labelChipH
    static let ValueBoxH = valueBoxH, ValueBoxW = valueBoxW
    // Cromo
    static let RailW = railW, RailModeW = railModeW, RailCell = railCell
    static let ChromeButtonW = chromeButtonW, ChromeButtonH = chromeButtonH, ChromeGlyph = chromeGlyph
    static let ParamTabH = paramTabH
    // Folhas
    static let SheetRadius = sheetRadius, AdjustRadius = adjustRadius, DialogW = dialogW
    static let DialogRadius = dialogRadius, DialogRowH = dialogRowH
    static let PopupW = popupW, PopupRowH = popupRowH, PopupRadius = popupRadius
    static let MenuRowMinH = menuRowMinH, MenuIcon = menuIcon
    // Teclado e cor
    static let KeypadCellH = keypadCellH, KeypadRadius = keypadRadius
    static let ColorBoardH = colorBoardH, ColorStripH = colorStripH
}

/// Medidas que só a casca do editor usa (espelho de `ShellDims`).
enum ShellDims {
    // Omega fundação (F6): as barras do editor seguem o EditorLayout (42 / 46), como o ShellDims.kt.
    static let topBar: CGFloat = EditorLayout.topBar
    static let transport: CGFloat = EditorLayout.transport
    static let strip: CGFloat = AureaDims.strip
    static let sheetHandle: CGFloat = AureaDims.sheetHandle
    static let fullscreenTimeBar: CGFloat = AureaDims.fullscreenTimeBar
    static let stageInset: CGFloat = AureaDims.stageInset
    static let fab: CGFloat = AureaDims.fab
    static let fabMargin: CGFloat = AureaDims.fabMargin
    static let touchSlop: CGFloat = AureaDims.touchSlop
    static let handleSlop: CGFloat = AureaDims.handleSlop
    static let snapTolerance: CGFloat = AureaDims.snapTolerance
    static let hitSlack: CGFloat = AureaDims.hitSlack
    static let axisHandleTarget: CGFloat = AureaDims.axisHandleTarget

    static let TopBar = topBar, Transport = transport, Strip = strip, SheetHandle = sheetHandle
    static let FullscreenTimeBar = fullscreenTimeBar, StageInset = stageInset
    static let Fab = fab, FabMargin = fabMargin, TouchSlop = touchSlop, HandleSlop = handleSlop
    static let SnapTolerance = snapTolerance, HitSlack = hitSlack
    static let AxisHandleTarget = axisHandleTarget
}

/// Raios prontos (AureaShape.kt).
enum AureaShape {
    static let Xs = RoundedRectangle(cornerRadius: AureaDims.radiusXs)
    static let Sm = RoundedRectangle(cornerRadius: AureaDims.radiusSm)
    static let Chip = RoundedRectangle(cornerRadius: AureaDims.radiusChip)
    static let Card = RoundedRectangle(cornerRadius: AureaDims.radiusCard)
    static let Md = RoundedRectangle(cornerRadius: AureaDims.radiusMd)
    static let Lg = RoundedRectangle(cornerRadius: AureaDims.radiusLg)
    static let Sheet = RoundedRectangle(cornerRadius: AureaDims.radiusSheet)
    static let Xl = RoundedRectangle(cornerRadius: AureaDims.radiusXl)
    static let Pill = RoundedRectangle(cornerRadius: AureaDims.radiusPill)
    static let Circle = SwiftUI.Circle()
}

/// Movimento: 100/200/300 ms; entrada desacelera, saída acelera (t²). Sem molas.
enum AureaMotion {
    static let fast: Double = 0.10
    static let normal: Double = 0.20
    static let slow: Double = 0.30
    static let FAST: Double = fast
    static let NORMAL: Double = normal
    static let SLOW: Double = slow

    /// Entrada ≈ Curves.decelerate; saída acelera (t²).
    static let enter = Animation.timingCurve(0.0, 0.0, 0.58, 1.0, duration: normal)
    static let exit = Animation.timingCurve(1.0 / 3.0, 0.0, 2.0 / 3.0, 1.0 / 3.0, duration: fast)
    static let Enter = enter
    static let Exit = exit

    /// Tocável: escala 0,965 e opacidade 0,82 enquanto pressionado.
    static let pressScale: CGFloat = 0.965
    static let pressAlpha: Double = 0.82
    static let PRESS_SCALE = pressScale
    static let PRESS_ALPHA = pressAlpha
}

/// Elevação: as sombras do Material e os vidros das barras.
enum AureaElevation {
    static let card: CGFloat = 1
    static let sheet: CGFloat = 8
    static let raised: CGFloat = 16
    static let Card = card, Sheet = sheet, Raised = raised

    static let tabBarBlur: CGFloat = 24
    static let compactBarBlur: CGFloat = 18
    static let batchBarBlur: CGFloat = 20

    static var tabBarTint: Color { AureaColors.background.opacity(0.72) }
    static var tabBarFallback: Color { AureaColors.background.opacity(0.97) }
    static var batchBarTint: Color { AureaColors.surface.opacity(0.88) }
    static var batchBarFallback: Color { AureaColors.surface }
}

/// Timeline da A.01 (`am_timeline.dart@aba36bb`). Só a timeline usa.
enum AureaTimeline {
    static let background = Color(hex: 0x1A1C28)
    // --- Cores ---------------------------------------------------------------
    static let tickMajor  = Color(hex: 0x8A97AD)
    static let tickMinor  = Color(hex: 0x5A6880)
    static let headerPill = Color(hex: 0x292D3D)
    static let swatch     = Color(hex: 0xFFE899)
    static let swatchGlyph = Color(hex: 0x0F141A)
    static let keyframeOn = Color(hex: 0xFFC107)
    static let trimHandle = Color(hex: 0xF2F5F9)

    static let TickMajor = tickMajor, TickMinor = tickMinor, HeaderPill = headerPill
    static let Swatch = swatch, SwatchGlyph = swatchGlyph, KeyframeOn = keyframeOn
    static let TrimHandle = trimHandle

    // --- Fileiras (visual aprovado: calha + clipe no tom do tipo) -------------
    /// Glifo do tipo na calha (tom muted).
    static let gutterIcon = Color(hex: 0x8B97A5)
    /// O olho pequeno no canto do glifo: mais apagado ainda.
    static let gutterEye = Color(hex: 0x998B97A5)
    /// Contorno do clipe escolhido e as alças de aparar.
    static let clipSelected = Color(hex: 0xFFFFFF)
    /// Clipes de camada oculta: a fileira inteira a 40 %.
    static let hiddenAlpha: Double = 0.4

    static let amber = ClipTone(body: Color(hex: 0x3A2A12), stripe: Color(hex: 0xEF9F27), text: Color(hex: 0xFAC775), wave: Color(hex: 0xEF9F27))
    static let teal = ClipTone(body: Color(hex: 0x0C3A2E), stripe: Color(hex: 0x1D9E75), text: Color(hex: 0x9FE1CB), wave: Color(hex: 0x5DCAA5))
    static let purple = ClipTone(body: Color(hex: 0x2A2656), stripe: Color(hex: 0x7F77DD), text: Color(hex: 0xCECBF6), wave: Color(hex: 0x7F77DD))
    static let gray = ClipTone(body: Color(hex: 0x232A33), stripe: Color(hex: 0x888780), text: Color(hex: 0xD3D1C7), wave: Color(hex: 0x888780))
    static let blue = ClipTone(body: Color(hex: 0x0F2A45), stripe: Color(hex: 0x378ADD), text: Color(hex: 0xB5D4F4), wave: Color(hex: 0x378ADD))
    static let pink = ClipTone(body: Color(hex: 0x3A1826), stripe: Color(hex: 0xD4537E), text: Color(hex: 0xF4C0D1), wave: Color(hex: 0xD4537E))
    static let green = ClipTone(body: Color(hex: 0x1F2E0F), stripe: Color(hex: 0x639922), text: Color(hex: 0xC0DD97), wave: Color(hex: 0x639922))

    // --- Barras do redesenho 2026-09-29 (tons médios; `text` = tinta do nome) ---
    static let barText = ClipTone(body: Color(hex: 0x3B4E7A), stripe: Color(hex: 0x7F9BD6), text: Color(hex: 0xE8EEF8), wave: Color(hex: 0x9FB6E6))
    static let barShape = ClipTone(body: Color(hex: 0x7A5A2B), stripe: Color(hex: 0xE8A33D), text: Color(hex: 0xF7ECD9), wave: Color(hex: 0xE8A33D))
    static let barMedia = ClipTone(body: Color(hex: 0x4A7C3F), stripe: Color(hex: 0x8FCB7E), text: Color(hex: 0xE4F0DF), wave: Color(hex: 0xB9E0AE))
    static let barAudio = ClipTone(body: Color(hex: 0x245247), stripe: Color(hex: 0x9FD3B8), text: Color(hex: 0xD6F0E3), wave: Color(hex: 0x9FD3B8))
    static let bar3D = ClipTone(body: Color(hex: 0x7A4A24), stripe: Color(hex: 0xE89A5B), text: Color(hex: 0xF7E4D2), wave: Color(hex: 0xE89A5B))
    static let barParticles = ClipTone(body: Color(hex: 0x5E3470), stripe: Color(hex: 0xC58BE0), text: Color(hex: 0xF0E2F6), wave: Color(hex: 0xC58BE0))
    static let barOther = ClipTone(body: Color(hex: 0x3A4456), stripe: Color(hex: 0x9AA6B8), text: Color(hex: 0xE1E6EE), wave: Color(hex: 0x9AA6B8))
    /// Tinta escura do ≡ perto do fim da barra.
    static let barGrip = Color.black.opacity(0.45)
    /// Divisória entre as miniaturas da tira de imagem/vídeo.
    static let filmDivider = Color.black.opacity(0.25)
    /// Glifo da forma na pílula (ponto âmbar) e o da música.
    static let pillShapeDot = Color(hex: 0xE8A33D)
    static let pillAudioNote = Color(hex: 0x9FD3B8)
    /// Régua do redesenho: forte e fino.
    static let rulerMajor = Color(hex: 0x6E7A8C)
    static let rulerMinor = Color(hex: 0x4C5566)
    static let previewBuffer = Color(hex: 0x4DA3FF)

    /// Tom da barra por tipo (redesenho 2026-09-29, par do Android): texto azul,
    /// forma âmbar, imagem/vídeo verde, áudio verde-água, 3D cobre, partículas
    /// roxo, o resto cinza-azulado.
    static func tone(_ type: TimelineLayerType) -> ClipTone {
        switch type {
        case .text: return barText
        case .shape: return barShape
        case .video, .image: return barMedia
        case .audio: return barAudio
        case .model3D: return bar3D
        case .particles: return barParticles
        case .adjustment, .camera, .light, .null, .group: return barOther
        }
    }

    // --- Medidas (dp) --------------------------------------------------------
    /// Régua 30; `rulerGap` = faixa do relógio (30) + 4 até a 1ª fileira.
    static let rulerTicks: CGFloat = 14
    static let rulerGap: CGFloat = 30
    static let row: CGFloat = 32
    static let bar: CGFloat = 26
    static let barRadius: CGFloat = 0
    /// Geometry measured from the 5.0.280 reference; original AUREA graphics.
    static let referenceRowsTop: CGFloat = 42
    static let referenceRow: CGFloat = 28
    static let referenceBar: CGFloat = 23
    static let referenceBarTop: CGFloat = 2.5
    static let referenceBarRadius: CGFloat = 1.5
    static let referenceTrimWidth: CGFloat = 30
    static let keyframeTrack: CGFloat = 11
    /// A pílula da fileira (olho + glifo) cobre de 0 a 78; as barras passam por baixo.
    static let headerColumn: CGFloat = 70
    static let gutterIconSize: CGFloat = 17
    static let gutterEyeSize: CGFloat = 12
    static let clipStripe: CGFloat = 3
    static let clipSelStroke: CGFloat = 2
    static let clipIcon: CGFloat = 12
    static let playhead: CGFloat = 1
    static let playheadKnob: CGFloat = 8

    static let RulerTicks = rulerTicks, RulerGap = rulerGap, Row = row, Bar = bar
    static let BarRadius = barRadius, KeyframeTrack = keyframeTrack
    static let HeaderColumn = headerColumn
    static let Playhead = playhead, PlayheadKnob = playheadKnob

    // --- Omega 0.3.2 (área 03): papéis da timeline (par do AureaTimeline.kt) ------
    /// Medidas da Omega: cartão com cantos de 3 e o cabeçalho visível de 62.
    static let omegaBarRadius: CGFloat = 3
    static let omegaHeaderColumn: CGFloat = 62
    /// Pílula do cabeçalho, aba do tempo atual e linha arrastada (do tema).
    static var omegaHeader: Color { AureaTheme.palette.timelineHeader }
    static var omegaTimeTab: Color { AureaTheme.palette.timelineTimeTab }
    static var omegaDragRow: Color { AureaTheme.palette.timelineDragRow }
    /// Traço de 0,5 pt da pílula (branco 10 %) e tiques da régua (branco 15 %).
    static let omegaHeaderBorder = Color(hex: 0x1AFFFFFF)
    static let omegaTick = Color(hex: 0x26FFFFFF)
    /// As três barrinhas da alça ≡ da linha e a sombra delas.
    static let omegaGrip = Color(hex: 0x4DFFFFFF)
    static let omegaGripShadow = Color(hex: 0xB31B1F32)
    /// Marcadores da régua, pela aparência (índice mod 4).
    static let bookmarks: [Color] = [Color(hex: 0xFFF16586), Color(hex: 0xFFFAD3A5), Color(hex: 0xFF4BE29D), Color(hex: 0xFF3C8CE7)]
    /// Fundo da linha da câmera principal (vermelho 10 %).
    static let mainCameraRow = Color(hex: 0x1AD13665)
    /// 0.3.4 (bugs-034, cores): rótulo do clipe em branco sobre a cor sólida do tipo
    /// (`ClipColors.solid`), com uma sombra curta preta a 50 %; onda e zebra de quadros.
    static let clipLabel = Color(hex: 0xFFFFFFFF)
    static let clipLabelShadow = Color(hex: 0x80000000)
    /// Realce no topo do cartão: brilho branco 14 % que some em `clipSheenDepth` da altura, e fio branco 20 %.
    static let clipSheen = Color(hex: 0x24FFFFFF)
    static let clipSheenLine = Color(hex: 0x33FFFFFF)
    static let clipSheenDepth: CGFloat = 0.45
    static let clipWave = Color(hex: 0xFFEAEAEA)
    static let clipZebra = Color(hex: 0x10FFFFFF)
    /// Sombra leve do cartão e a do clipe levantado.
    static let clipShadow = Color(hex: 0x40000000)
    static let clipLiftShadow = Color(hex: 0x4D000000)
    /// Losango em edição (branco, traço preto), dos outros parâmetros (160/255) e parcial.
    static let keyFill = Color(hex: 0xFFFFFFFF)
    static let keyStroke = Color(hex: 0xFF000000)
    static let keyAlt = Color(hex: 0xA0FFFFFF)
    static let keyAltStroke = Color(hex: 0xFFFFFFFF)
    static let keyPartial = Color(hex: 0xFF9D9FA5)
    /// Alças de corte e de velocidade do modo foco (o alfa varia com o gesto).
    static let trimGrip = Color(hex: 0xFFFFFFFF)
}

/// Fundo escuro, faixa sólida, texto claro e forma de onda de um tipo de clipe.
struct ClipTone {
    let body: Color
    let stripe: Color
    let text: Color
    let wave: Color
}

// =============================================================================
// As alturas das zonas do editor — porte literal de `EditorLayout` do Android.
//
//  A regra que a casca A.01 cobrava: abrir painel, adicionar ou trocar de aba
//  NUNCA move o preview. O painel tira espaço da TIMELINE, e a timeline nunca
//  fica abaixo do piso. No iOS isso também é o que mantém o CAMetalLayer com o
//  mesmo tamanho durante um arrasto (redimensionar o drawable a cada gesto
//  custaria uma recriação de swapchain).
// =============================================================================
enum SheetContent { case none, hint, dock, panel, curve, batch, adding, addBar }

struct EditorMetrics {
    var topBar: CGFloat
    var preview: CGFloat
    var strip: CGFloat
    var transport: CGFloat
    var timeline: CGFloat
    var sheet: CGFloat
    /// Omega fundação: a folha é desenhada POR CIMA da timeline (o painel da
    /// camada de 200 cobre a zona por baixo). Falso = empilhada embaixo dela.
    var overlay: Bool = false

    /// A zona inferior inteira: timeline + folha empilhada (com `overlay`, só a timeline).
    var zone: CGFloat { timeline + (overlay ? 0 : sheet) }
    /// A parte da timeline à vista (com `overlay`, o que o painel não cobre: 80).
    var visibleTimeline: CGFloat { overlay ? max(0, timeline - sheet) : timeline }
    /// O que a timeline tem coberto embaixo (o `obscuredBottom` da área 03).
    var obscuredTimeline: CGFloat { overlay ? min(sheet, timeline) : 0 }
    /// Soma das faixas empilhadas: é sempre a altura da área do editor.
    var total: CGFloat { topBar + preview + strip + transport + zone }
}

/// Omega fundação (F6, geometria da referência; par do `EditorLayout.kt`): no
/// telefone em retrato o topo tem 42, o transporte 46 e embaixo fica uma ZONA
/// FIXA de 280 (timeline + barra de adicionar, ou a timeline com o painel da
/// camada de 200 por cima). A prévia é o resto e NUNCA muda de altura ao abrir
/// ou fechar painel. 0.3.3: a divisa arrastável volta (`preferred`, ver `solve`).
/// O largo fica.
enum EditorLayout {
    /// Barra superior da referência (botões de 40 com alvo de 44+).
    static let topBar: CGFloat = 42
    /// Barra de reprodução da referência.
    static let transport: CGFloat = 46
    static let strip: CGFloat = 0
    static let timelineMin: CGFloat = 110
    static let previewMin: CGFloat = 96
    private static let sheetHandle: CGFloat = 12
    /// Lote: 4 + tempo 52 + 8 + tela 48 + 8 de respiro.
    private static let batchBody: CGFloat = 120
    private static let hintBody: CGFloat = 30
    /// A barra fixa de adicionar do AUREA (fica, exceção do dono): ícone 23 +
    /// nome 11, itens de 64 e 3 de respiro em cima e embaixo (fora a área segura).
    static let addBar: CGFloat = 70
    // --- Omega adicionar (área 08, P0-1) ----------------------------------------
    /// O conteúdo do botão aberto na barra de adicionar: a área de conteúdo do popup
    /// da referência (251 − 58 = 193). Fica POR CIMA da timeline, logo acima da
    /// barra (que não sai do lugar): a zona continua 210 + 70. Par do `ADD_CONTENT`.
    static let addContentHeight: CGFloat = 193
    /// Altura do conteúdo do adicionar nesta geometria (nunca maior que a timeline à vista).
    static func addContent(_ m: EditorMetrics) -> CGFloat { min(addContentHeight, m.timeline) }
    // 0.3.3 barra: altura nominal do bloco da grade do "+" (par de `ADD_BAR_GRID_HEIGHT`):
    // margem 4 + bloco 7 + cartão 8 + 2 fileiras de (ladrilho 46 + 4 + nome 15) + 6 + 8 + 7 + 4.
    static let addBarGridHeight: CGFloat = 174 // = 4 + 7 + 8 + 2 × (46 + 4 + 15) + 6 + 8 + 7 + 4
    /// Doca da camada, compacta (EditorLayout.kt `DOCK`): fileira rápida de
    /// ícones e fichas baixas; a altura é a do conteúdo.
    static let dockQuick: CGFloat = 48
    static let dockTile: CGFloat = 60
    static let dock: CGFloat = 8 + dockQuick + 2 * (8 + dockTile) + 8
    static func dockTileHeight(fontScale: CGFloat = 1) -> CGFloat { dockTile + 28 * (min(max(fontScale, 1), 2) - 1) }
    /// A doca com `rows` fileiras de fichas (1 ou 2): a altura é a do conteúdo.
    static func dockHeight(rows: Int, fontScale: CGFloat = 1) -> CGFloat { 8 + dockQuick + CGFloat(min(max(rows, 1), 2)) * (8 + dockTileHeight(fontScale: fontScale)) + 8 }

    // --- Omega fundação (F6): zona inferior fixa -------------------------------
    /// A zona inferior fixa do retrato (`edit_timeline_height` da referência).
    static let bottomZoneHeight: CGFloat = 280
    /// O painel da camada e os painéis de propriedade (`edit_panel_height`).
    static let layerPanelHeight: CGFloat = 200
    /// O que sobra da timeline com o painel aberto: régua 42 + uma linha.
    static let timelineFocus: CGFloat = 80
    /// O painel cresce com a fonte do sistema: 200 em 1×, 240 em 2×.
    static func layerPanel(fontScale: CGFloat = 1) -> CGFloat { layerPanelHeight + 40 * (min(max(fontScale, 1), 2) - 1) }
    /// 0.3.6: o teto dos painéis de propriedade (par do `EditorLayout.panelMax` do Android):
    /// com a divisa no alto (ou a Edição numa tela alta) o painel ocupava a zona inteira;
    /// passando disso, a timeline fica com o resto. Cresce com a fonte.
    static let panelMaxHeight: CGFloat = 360
    static func panelMax(fontScale: CGFloat = 1) -> CGFloat { panelMaxHeight + 60 * (min(max(fontScale, 1), 2) - 1) }
    // 0.3.4 painéis: o painel de propriedade sobe até logo abaixo da linha em foco.
    /// Régua da timeline (`TimelineMetrics.rowsTop`): 42 em 1×, +14 por 1× de fonte.
    static let focusRuler: CGFloat = 42
    /// A linha da camada em foco (`TimelineMetrics.row`): 28 em 1×, +24 por 1× de fonte.
    static let focusRow: CGFloat = 28
    /// Folga entre a linha em foco e o topo do painel.
    static let focusGap: CGFloat = 10
    /// A faixa de timeline à vista com o painel aberto: régua + linha em foco + folga
    /// (`timelineFocus` = 80 em 1×); cresce com a fonte junto com a régua e a linha.
    /// Par do `EditorLayout.timelineFocus(fontScale)` do Android.
    static func timelineFocusHeight(fontScale: CGFloat) -> CGFloat {
        let s = min(max(fontScale, 1), 2) - 1
        return focusRuler + 14 * s + focusRow + 24 * s + focusGap
    }
    /// A zona inferior: o painel + a faixa de foco da timeline (280 em 1×).
    static func bottomZone(fontScale: CGFloat = 1) -> CGFloat { layerPanel(fontScale: fontScale) + timelineFocusHeight(fontScale: fontScale) }

    /// Folga do palco em volta do quadro ajustado (layout antigo; fica para quem lê o token).
    static let previewFitMargin: CGFloat = 16
    /// Fração do palco do layout antigo (fica para quem lê o token).
    static let naturalPreviewFraction: CGFloat = 0.45
    /// Margem lateral do palco fora da tela cheia.
    static let previewSideMargin: CGFloat = 8

    static func workspace(_ totalHeight: CGFloat) -> CGFloat {
        max(0, totalHeight - topBar - transport - strip)
    }

    // 0.3.3 recursos: a divisa palco/timeline volta (arrastar o transporte na vertical).
    /// A menor zona inferior que a divisa deixa: timeline mínima (110) + barra de adicionar (70).
    static let zoneMin: CGFloat = timelineMin + addBar
    /// O palco mais alto possível arrastando a divisa: a zona nunca fica abaixo de `zoneMin`.
    static func maxPreview(_ total: CGFloat) -> CGFloat { max(previewMin, workspace(total) - zoneMin) }

    /// O retrato do telefone (e do tablet em pé): prévia = área − 42 − 46 − zona,
    /// igual em todos os `SheetContent`. Dentro da zona: `.addBar` = timeline (210)
    /// + barra (70) embaixo; `.dock`, `.panel`, `.curve` = timeline na zona inteira
    /// e a folha (200) POR CIMA (`overlay`; ficam 80 de timeline); `.none` = só a
    /// timeline; `.hint`, `.batch`, `.adding` = empilhados embaixo dela. Tela baixa
    /// demais: a prévia fica no piso e a zona encolhe (a soma é sempre a altura).
    /// 0.3.3 recursos: `preferred` > 0 é a altura da prévia escolhida arrastando a
    /// divisa (guardada no aparelho), de `previewMin` até `maxPreview`: a zona cresce
    /// (mais timeline) ou encolhe até `zoneMin`. O painel da camada (`.dock`,
    /// `.panel`, `.curve`) e o adicionar aberto precisam da zona inteira: com a prévia
    /// maior que a automática, ela devolve o espaço enquanto eles estão abertos (como
    /// na 0.3.2). Sem preferência, a prévia é a automática (área − 42 − 46 − 280).
    /// `width` e `aspect` ficam para quem já chamava. Par do `EditorLayout.solve`.
    static func solve(total: CGFloat, content: SheetContent, fullscreen: Bool,
                      width: CGFloat = 0, aspect: CGFloat = 0, preferred: CGFloat = 0, dockRows: Int = 2, fontScale: CGFloat = 1) -> EditorMetrics {
        if fullscreen {
            return EditorMetrics(topBar: 0, preview: max(0, total - transport), strip: 0,
                                 transport: transport, timeline: 0, sheet: 0)
        }
        let ws = workspace(total)
        let panel = layerPanel(fontScale: fontScale)
        let focus = timelineFocusHeight(fontScale: fontScale)
        let previewFloor = min(previewMin, ws * 0.25)
        let automatic = min(max(previewFloor, ws - bottomZone(fontScale: fontScale)), ws)
        // 0.3.3 recursos: a altura arrastada na divisa (0 = automática).
        let chosen: CGFloat = preferred > 0 && preferred.isFinite
            ? min(min(max(preferred, previewFloor), max(previewFloor, ws - zoneMin)), ws)
            : automatic
        let needsWholeZone = content == .dock || content == .panel || content == .curve || content == .adding
        let preview = needsWholeZone ? min(chosen, automatic) : chosen
        let zone = ws - preview

        func stacked(_ body: CGFloat) -> EditorMetrics {
            let sheet = min(max(body, 0), zone)
            return EditorMetrics(topBar: topBar, preview: preview, strip: strip, transport: transport,
                                 timeline: zone - sheet, sheet: sheet)
        }
        func over(_ body: CGFloat) -> EditorMetrics {
            // Ao menos a faixa de foco da timeline fica à vista (menos numa tela baixa).
            // 0.3.4 painéis: o teto é metade da zona (com 0,3 a faixa de foco da fonte grande cortava a linha).
            let sheet = min(body, max(0, zone - min(focus, zone * 0.5)))
            return EditorMetrics(topBar: topBar, preview: preview, strip: strip, transport: transport,
                                 timeline: zone, sheet: max(0, sheet), overlay: true)
        }
        switch content {
        case .none: return stacked(0)
        case .addBar: return stacked(addBar)
        case .hint: return stacked(sheetHandle + hintBody)
        case .batch: return stacked(sheetHandle + batchBody)
        // Omega adicionar: a mesma zona da barra (210 + 70); o conteúdo fica por cima da timeline.
        case .adding: return stacked(addBar)
        case .dock: return over(min(dockHeight(rows: dockRows, fontScale: fontScale), panel))
        // 0.3.4 painéis: o painel de propriedade ocupa a zona inteira até logo abaixo
        // da linha em foco (régua + linha + folga), também com a divisa no alto.
        // 0.3.6: até `panelMax`; o que passa disso fica com a timeline.
        case .panel, .curve: return over(min(zone, panelMax(fontScale: fontScale)))
        }
    }

    /// Layout largo: SÓ em paisagem com largura ≥ 600 (o mesmo do Android).
    /// Tablet EM PÉ usa o layout do celular (palco, transporte, timeline e
    /// doca empilhados): o "≥ 900 mesmo em pé" mandava o iPad 13" para o largo,
    /// com a coluna da direita só com a dica, a timeline baixa e o palco
    /// recriado a cada troca — "a UI inteira sumiu" no tablet.
    static func isWide(_ width: CGFloat, _ height: CGFloat) -> Bool {
        (width >= 520 && width > height && height < 600) || (width >= 900 && width > height)
    }

    static func wideTimeline(_ totalHeight: CGFloat) -> CGFloat {
        min(max((totalHeight - topBar - transport - strip) * 0.34, 88), 280)
    }

    /// A largura da folha no layout largo (`wideSheetWidth`): 40 %, presa a 280–380.
    static func wideSheetWidth(_ width: CGFloat) -> CGFloat {
        min(max(width * 0.4, 240), 420)
    }
}
enum EditorLayoutMetrics {
    static let topBar: CGFloat = EditorLayout.topBar
    static let transport: CGFloat = EditorLayout.transport
    static let strip: CGFloat = EditorLayout.strip
    static let timelineMin: CGFloat = EditorLayout.timelineMin
    static let previewMin: CGFloat = EditorLayout.previewMin
    static let TOP_BAR = topBar, TRANSPORT = transport, STRIP = strip
    static let TIMELINE_MIN = timelineMin, PREVIEW_MIN = previewMin

    static func workspace(_ totalHeight: CGFloat) -> CGFloat { EditorLayout.workspace(totalHeight) }
    static func solve(total: CGFloat, content: SheetContent, fullscreen: Bool) -> EditorMetrics {
        EditorLayout.solve(total: total, content: content, fullscreen: fullscreen)
    }
    static func isWide(_ width: CGFloat, _ height: CGFloat) -> Bool { EditorLayout.isWide(width, height) }
    static func wideTimeline(_ totalHeight: CGFloat) -> CGFloat { EditorLayout.wideTimeline(totalHeight) }
    static func wideSheetWidth(_ width: CGFloat) -> CGFloat { EditorLayout.wideSheetWidth(width) }
}

extension Comparable {
    func clamped(to range: ClosedRange<Self>) -> Self {
        min(max(self, range.lowerBound), range.upperBound)
    }
}

// =============================================================================
// Glifos Cupertino
//
//  O Android desenha cada ícone como um glifo da fonte `CupertinoIcons.ttf`
//  (MIT, pacote cupertino_icons 1.0.9 — licença em docs/licenses) numa caixa
//  N×N com `fontSize = N`. A MESMA fonte viaja para o iOS (o .ttf está em
//  `engine/platform/ios/app/CupertinoIcons.ttf`, registrado em `UIAppFonts` e
//  em `registerCupertinoFonts()`): o glifo é o mesmo desenho, no mesmo lugar.
//
//  Se o registro falhar (arquivo fora do bundle), o glifo sai vazio: é o sinal
//  honesto de que a fonte não chegou, em vez de um ícone parecido que não é o
//  aprovado.
// =============================================================================
enum CupertinoFont {
    /// O nome de família do .ttf (tabela `name`, id 1/4/6).
    static let family = "CupertinoIcons"
    static let fileName = "CupertinoIcons.ttf"

    private static var registered = false

    /// Registra a fonte uma vez. O `UIAppFonts` do Info.plist já a carrega no
    /// início do app; esta chamada cobre o caso de o arquivo ter entrado no
    /// bundle por outro caminho.
    static func register() {
        if registered { return }
        registered = true
        guard let url = Bundle.main.url(forResource: "CupertinoIcons", withExtension: "ttf") else { return }
        CTFontManagerRegisterFontsForURL(url as CFURL, .process, nil)
    }

    /// A fonte no tamanho pedido.
    static func font(_ size: CGFloat) -> Font {
        register()
        return .custom(family, fixedSize: size)
    }
}

// =============================================================================
// 0.3.4: o estilo dos ícones do app, num ponto só (par do `AureaIconStyle` do
// CupertinoIcons.kt, mesmos valores). Minimalista e limpo: ícone de CONTORNO,
// traço fino e uniforme, desenhado UMA vez (sem contorno extra, sem cópias).
// =============================================================================
enum AureaIconStyle {
    /// Traço dos ícones desenhados à mão, na caixa de 24 (multiplique pela
    /// unidade `tamanho / 24`). É o peso dos glifos da fonte CupertinoIcons, que
    /// já são de traço fino; cantos e pontas redondos.
    static let line: CGFloat = 1.5
    /// Peso dos SF Symbols: o regular, no mesmo traço fino dos glifos da fonte.
    static let symbol: Font.Weight = .regular

    /// Glifo cheio → o de contorno da mesma fonte. Vale para todo ícone que passa
    /// por `CupertinoGlyph.text` e pelos glifos da timeline. Ficam cheios só os
    /// que são estado ou marca: play/pausa, losango de keyframe, marcador de
    /// batida, estrela de favorito, check de selecionado/feito, caixa marcada,
    /// solo, ponto de cor (`CircleFill`), as metades de círculo e o triangulinho
    /// de 7–8 pt que marca "tem menu" no canto de um botão (os de abrir/fechar
    /// seção já usam chevron).
    private static let outline: [Character: Character] = [
        CupertinoGlyph.VideocamFill: CupertinoGlyph.Videocam,
        CupertinoGlyph.PhotoFill: CupertinoGlyph.Photo,
        CupertinoGlyph.FolderFill: CupertinoGlyph.Folder,
        CupertinoGlyph.CameraFill: CupertinoGlyph.Camera,
        CupertinoGlyph.CubeFill: CupertinoGlyph.Cube,
        CupertinoGlyph.CubeBoxFill: CupertinoGlyph.CubeBox,
        CupertinoGlyph.HouseFill: CupertinoGlyph.House,
        CupertinoGlyph.MicFill: CupertinoGlyph.Mic,
        CupertinoGlyph.PaintbrushFill: CupertinoGlyph.Paintbrush,
        CupertinoGlyph.DropFill: CupertinoGlyph.Drop,
        CupertinoGlyph.GearAltFill: CupertinoGlyph.Gear,
        CupertinoGlyph.InfoCircleFill: CupertinoGlyph.InfoCircle,
        CupertinoGlyph.ArrowDownCircleFill: CupertinoGlyph.ArrowDownCircle,
        CupertinoGlyph.XmarkCircleFill: CupertinoGlyph.XmarkCircle,
        CupertinoGlyph.SquareStack3dDownRightFill: CupertinoGlyph.SquareStack3dDownRight,
        CupertinoGlyph.CaptionsBubbleFill: CupertinoGlyph.CaptionsBubble,
        CupertinoGlyph.LockFill: CupertinoGlyph.Lock,
        CupertinoGlyph.LockOpenFill: CupertinoGlyph.LockOpen,
        CupertinoGlyph.LinkCircleFill: CupertinoGlyph.LinkCircle,
        CupertinoGlyph.EyeFill: CupertinoGlyph.Eye,
        // 0.3.6: a tesoura da fonte tem as lâminas cheias (traço grosso ao lado dos
        // outros ícones); a de contorno da mesma fonte tem o traço fino.
        CupertinoGlyph.Scissors: CupertinoGlyph.ScissorsAlt,
    ]

    /// O glifo no estilo do app (ver `outline`).
    static func glyph(_ codepoint: Character) -> Character { outline[codepoint] ?? codepoint }
}

/// Nome da 0.3.3, mantido para as chamadas `AureaIconWeight.symbol` já espalhadas.
typealias AureaIconWeight = AureaIconStyle

/// 0.3.4: ícones de linha próprios no lugar dos vetores do Material (cheios ou de
/// traço 2): traço `AureaIconStyle.line`, pontas e cantos redondos, na caixa de 24.
/// A mesma geometria do `AureaLineIcons` do CupertinoIcons.kt. Cada polilinha é
/// x0, y0, x1, y1…
enum AureaLineIcon {
    static let chevronLeft: [[CGFloat]] = [[15, 6, 9, 12, 15, 18]]
    static let alignStarts: [[CGFloat]] = [[4, 6, 20, 6], [4, 10, 14, 10], [4, 14, 20, 14], [4, 18, 14, 18]]
    static let alignEnds: [[CGFloat]] = [[4, 6, 20, 6], [10, 10, 20, 10], [4, 14, 20, 14], [10, 18, 20, 18]]
    static let stairs: [[CGFloat]] = [[4.5, 19.5, 9.5, 19.5, 9.5, 14.5, 14.5, 14.5, 14.5, 9.5, 19.5, 9.5, 19.5, 4.5]]

    /// O desenho próprio que substitui o vetor do Material de nome `name`
    /// (o mesmo nome do `MaterialGlyph`), ou nil para os que continuam do Material.
    static func forMaterial(_ name: String) -> [[CGFloat]]? {
        switch name {
        case "filled.ChevronLeft", "rounded.ChevronLeft": return chevronLeft
        case "automirrored.rounded.FormatAlignLeft": return alignStarts
        case "automirrored.rounded.FormatAlignRight": return alignEnds
        case "rounded.Stairs": return stairs
        default: return nil
        }
    }

    /// Desenha as polilinhas numa caixa `size`×`size`.
    static func view(_ strokes: [[CGFloat]], size: CGFloat, color: Color) -> some View {
        Canvas { context, box in
            let u = min(box.width, box.height) / 24
            let o = CGPoint(x: (box.width - 24 * u) / 2, y: (box.height - 24 * u) / 2)
            var path = Path()
            for s in strokes where s.count >= 4 {
                path.move(to: CGPoint(x: o.x + s[0] * u, y: o.y + s[1] * u))
                var i = 2
                while i + 1 < s.count {
                    path.addLine(to: CGPoint(x: o.x + s[i] * u, y: o.y + s[i + 1] * u))
                    i += 2
                }
            }
            context.stroke(path, with: .color(color),
                           style: StrokeStyle(lineWidth: AureaIconStyle.line * u, lineCap: .round, lineJoin: .round))
        }
        .frame(width: size, height: size)
    }
}

/// Os codepoints usados pela UI (nomes do pacote cupertino_icons, os MESMOS do
/// `CupertinoGlyph`/`ShellGlyph` do Kotlin).
enum CupertinoGlyph {
    /// Reserved for Aurea's own vector; matches the Android drawing.
    static let MotionBlur: Character = "\u{E000}"
    // --- Navegação e ações ----------------------------------------------------
    static let AddCircled: Character = "\u{F48A}"
    static let Arrow2Squarepath: Character = "\u{F4E6}"
    static let ArrowDownRightSquare: Character = "\u{F4F7}"
    static let ArrowDownToLine: Character = "\u{F4FB}"
    static let ArrowDownCircleFill: Character = "\u{F4EC}"
    static let ArrowDownCircle: Character = "\u{F4EB}"
    static let ArrowLeftRight: Character = "\u{F500}"
    static let ArrowLeftRightSquare: Character = "\u{F503}"
    static let ArrowLeftToLine: Character = "\u{F507}"
    static let ArrowRightArrowLeft: Character = "\u{F50B}"
    static let ArrowRightToLine: Character = "\u{F514}"
    static let ArrowTurnLeftDown: Character = "\u{F519}"
    static let ArrowTurnUpRight: Character = "\u{F51E}"
    static let ArrowUpArrowDown: Character = "\u{F51F}"
    static let ArrowUpDownSquare: Character = "\u{F52D}"
    static let ArrowUpToLine: Character = "\u{F53D}"
    static let ArrowUpDoc: Character = "\u{F528}"
    static let ArrowUturnLeft: Character = "\u{F544}"
    static let ArrowUturnRight: Character = "\u{F549}"
    static let ArrowCounterclockwise: Character = "\u{F21C}"
    static let BackwardEnd: Character = "\u{F578}"
    static let BackwardEndAlt: Character = "\u{F579}"
    static let Bolt: Character = "\u{F593}"
    static let Book: Character = "\u{F3E7}"
    static let Bookmark: Character = "\u{F3E9}"
    static let Camera: Character = "\u{F3F5}"
    static let CameraFill: Character = "\u{F3F6}"
    static let CameraViewfinder: Character = "\u{F5B9}"
    static let CaptionsBubble: Character = "\u{F5BE}"
    static let CaptionsBubbleFill: Character = "\u{F5BF}"
    static let ChartBarAltFill: Character = "\u{F8B7}"
    static let CheckmarkAlt: Character = "\u{F8C1}"
    static let CheckmarkCircle: Character = "\u{F3FE}"
    static let CheckmarkCircleFill: Character = "\u{F3FF}"
    static let CheckmarkSeal: Character = "\u{F5CB}"
    static let CheckmarkSquare: Character = "\u{F5CF}"
    static let CheckmarkSquareFill: Character = "\u{F5D0}"
    static let ChevronDown: Character = "\u{F5D5}"
    static let ChevronLeft: Character = "\u{F3D2}"
    static let ChevronRight: Character = "\u{F3D3}"
    static let ChevronUp: Character = "\u{F5E5}"
    static let Circle: Character = "\u{F401}"
    static let CircleFill: Character = "\u{F400}"
    static let CircleLefthalfFill: Character = "\u{F5F0}"
    static let Clock: Character = "\u{F4BE}"
    static let CloudDownload: Character = "\u{F8C4}"
    static let ColorFilter: Character = "\u{F8C8}"
    static let Crop: Character = "\u{F618}"
    static let Cube: Character = "\u{F61A}"
    static let CubeBox: Character = "\u{F61B}"
    static let CubeBoxFill: Character = "\u{F61C}"
    static let CubeFill: Character = "\u{F61D}"
    static let Trash: Character = "\u{F4C4}"
    static let DeleteLeft: Character = "\u{F621}"
    static let DevicePhonePortrait: Character = "\u{F8CF}"
    static let DocOnClipboard: Character = "\u{F632}"
    static let DocOnDoc: Character = "\u{F634}"
    static let DocText: Character = "\u{F638}"
    static let DropFill: Character = "\u{F8D9}"
    static let Drop: Character = "\u{F8D8}"
    static let Ellipsis: Character = "\u{F46A}"
    static let ExclamationmarkBubble: Character = "\u{F656}"
    static let ExclamationmarkTriangle: Character = "\u{F660}"
    static let Eye: Character = "\u{F424}"
    static let EyeFill: Character = "\u{F425}"
    static let EyeSlash: Character = "\u{F662}"
    static let Eyedropper: Character = "\u{F664}"
    static let Film: Character = "\u{F66B}"
    static let Flag: Character = "\u{F42C}"
    static let Folder: Character = "\u{F434}"
    static let FolderFill: Character = "\u{F435}"
    static let ForwardEnd: Character = "\u{F67F}"
    static let ForwardEndAlt: Character = "\u{F680}"
    static let Fullscreen: Character = "\u{F386}"
    static let FullscreenExit: Character = "\u{F37D}"
    static let Gear: Character = "\u{F43C}"
    static let GearAltFill: Character = "\u{F43D}"
    static let Grid: Character = "\u{F6A5}"
    static let House: Character = "\u{F447}"
    static let HouseFill: Character = "\u{F6CA}"
    static let InfoCircle: Character = "\u{F44C}"
    static let InfoCircleFill: Character = "\u{F6CF}"
    static let Lightbulb: Character = "\u{F6DD}"
    static let LineHorizontal3: Character = "\u{F6E1}"
    static let Link: Character = "\u{F6E5}"
    static let LinkCircleFill: Character = "\u{F6E7}"
    static let LinkCircle: Character = "\u{F6E6}"
    static let Lock: Character = "\u{F4C8}"
    static let LockFill: Character = "\u{F4C9}"
    static let LockOpen: Character = "\u{F6FA}"
    static let Minus: Character = "\u{F70F}"
    static let MinusCircle: Character = "\u{F463}"
    // Microfone (Gravar voz): contorno e cheio.
    static let Mic: Character = "\u{F460}"
    static let MicFill: Character = "\u{F461}"
    static let Move: Character = "\u{F8F8}"
    static let MusicNote: Character = "\u{F46B}"
    static let MusicNote2: Character = "\u{F46C}"
    static let Paintbrush: Character = "\u{F72E}"
    static let PauseFill: Character = "\u{F478}"
    static let Pencil: Character = "\u{F37E}"
    static let PencilOutline: Character = "\u{F73D}"
    static let PersonCropCircle: Character = "\u{F419}"
    static let Photo: Character = "\u{F767}"
    static let PhotoFill: Character = "\u{F768}"
    static let PhotoOnRectangle: Character = "\u{F76A}"
    static let Play: Character = "\u{F487}"
    static let PlayFill: Character = "\u{F488}"
    static let PlayRectangle: Character = "\u{F771}"
    static let Plus: Character = "\u{F489}"
    static let PlusSquare: Character = "\u{F77E}"
    static let PlusSquareOnSquare: Character = "\u{F781}"
    static let QuestionCircle: Character = "\u{F78F}"
    static let RectangleStack: Character = "\u{F3C9}"
    static let Repeat: Character = "\u{F7BF}"
    static let Rhombus: Character = "\u{F7C2}"
    static let RhombusFill: Character = "\u{F7C3}"
    static let Scissors: Character = "\u{F7C9}"
    static let Search: Character = "\u{F4A5}"
    static let SliderHorizontal3: Character = "\u{F7DC}"
    static let SmallcircleCircle: Character = "\u{F7DF}"
    static let Sparkles: Character = "\u{F7E8}"
    static let Speaker2: Character = "\u{F7EB}"
    static let SpeakerSlash: Character = "\u{F7EE}"
    static let Speedometer: Character = "\u{F7F5}"
    static let Square: Character = "\u{F7F8}"
    static let SquareArrowUp: Character = "\u{F4CA}"
    static let SquareGrid2x2: Character = "\u{F804}"
    static let SquareOnSquare: Character = "\u{F80D}"
    static let SquareStack3dDownRight: Character = "\u{F817}"
    static let SquareStack3dDownRightFill: Character = "\u{F818}"
    static let SquareStack3dUp: Character = "\u{F819}"
    static let SuitDiamond: Character = "\u{F831}"
    static let SuitDiamondFill: Character = "\u{F832}"
    static let Textformat: Character = "\u{F85C}"
    static let Timer: Character = "\u{F868}"
    static let Tv: Character = "\u{F881}"
    static let Videocam: Character = "\u{F4CC}"
    static let VideocamFill: Character = "\u{F4CD}"
    static let WandStars: Character = "\u{F892}"
    static let Waveform: Character = "\u{F894}"
    static let Wrench: Character = "\u{F8A0}"
    static let Xmark: Character = "\u{F404}"
    static let XmarkCircle: Character = "\u{F405}"
    static let XmarkCircleFill: Character = "\u{F36E}"

    // --- Painéis e controles de propriedade ----------------------------------
    static let ArrowtriangleDownFill: Character = "\u{F55D}"
    static let ArrowtriangleRightFill: Character = "\u{F569}"
    static let ArrowUp: Character = "\u{F366}"
    static let ArrowDown: Character = "\u{F35D}"
    static let ChevronBack: Character = "\u{F3CF}"
    static let Star: Character = "\u{F81F}"
    static let StarFill: Character = "\u{F822}"
    static let Scribble: Character = "\u{F7CB}"
    static let Tortoise: Character = "\u{F86A}"
    static let Hare: Character = "\u{F6B9}"

    // --- Glifos que só a casca usa (ShellGlyph.kt) ---------------------------
    static let SquareOnCircle: Character = "\u{F80C}"
    static let CircleGridHex: Character = "\u{F5EE}"
    static let SliderHorizontalBelowRectangle: Character = "\u{F7DD}"
    static let CircleGrid3x3: Character = "\u{F5EC}"
    static let Viewfinder: Character = "\u{F88D}"
    static let SquareSplit2x2: Character = "\u{F813}"
    static let PaintbrushFill: Character = "\u{F72F}"
    static let RectangleArrowUpRightArrowDownLeft: Character = "\u{F79E}"
    static let ArrowUpLeftArrowDownRight: Character = "\u{F386}"
    static let Nosign: Character = "\u{F727}"
    static let FolderBadgePlus: Character = "\u{F678}"
    static let SquareGrid3x2: Character = "\u{F806}"
    static let LockOpenFill: Character = "\u{F6FB}"
    static let Snow: Character = "\u{F7E7}"
    static let BookmarkSolid: Character = "\u{F3EA}"
    static let Metronome: Character = "\u{F70B}"
    static let ScissorsAlt: Character = "\u{F905}"
    static let WandRaysInverse: Character = "\u{F891}"
    static let PauseCircle: Character = "\u{F736}"
    static let PlayCircle: Character = "\u{F76F}"
    static let SquareFill: Character = "\u{F7FF}"
    static let SquareStack3dDownDottedline: Character = "\u{F816}"
    static let RectangleDock: Character = "\u{F7A3}"
    static let WaveformPathEcg: Character = "\u{F89A}"
    static let Table: Character = "\u{F844}"
    static let TextformatAlt: Character = "\u{F860}"

    /// O glifo desenhado no tamanho pedido — a mesma conta do Android (caixa
    /// N×N com `fontSize = N`). É o helper que qualquer tela usa quando não
    /// quer um `CupertinoIcon` inteiro.
    @ViewBuilder static func text(_ codepoint: Character, size: CGFloat,
                     color: Color = AureaColors.text) -> some View {
        if codepoint == MotionBlur {
            Canvas { context, dimensions in
                let unit = min(dimensions.width, dimensions.height) / 24
                // 0.3.4: o traço fino do estilo dos ícones (AureaIconStyle.line).
                let line = AureaIconStyle.line * unit
                context.stroke(Path(ellipseIn: CGRect(x: 11.6 * unit, y: 6.6 * unit,
                    width: 10.8 * unit, height: 10.8 * unit)), with: .color(color), lineWidth: line)
                let trails: [(CGFloat, CGFloat, Double)] = [(2, 7, 0.55), (1, 12, 0.85), (3, 17, 0.55)]
                for (x, y, alpha) in trails {
                    var path = Path()
                    path.move(to: CGPoint(x: x * unit, y: y * unit))
                    path.addLine(to: CGPoint(x: 9 * unit, y: y * unit))
                    context.stroke(path, with: .color(color.opacity(alpha)),
                        style: StrokeStyle(lineWidth: line, lineCap: .round))
                }
            }.frame(width: size, height: size)
        } else {
            // 0.3.4: o glifo no estilo do app (cheio → contorno, AureaIconStyle), desenhado uma vez.
            Text(String(AureaIconStyle.glyph(codepoint)))
                .font(CupertinoFont.font(size))
                .foregroundColor(color)
        }
    }
}

/// Espelho de `ShellGlyph` (os mesmos codepoints, o mesmo nome).
enum ShellGlyph {
    static let SquareOnCircle = CupertinoGlyph.SquareOnCircle
    static let CircleGridHex = CupertinoGlyph.CircleGridHex
    static let Scribble = CupertinoGlyph.Scribble
    static let SliderHorizontalBelowRectangle = CupertinoGlyph.SliderHorizontalBelowRectangle
    static let CircleGrid3x3 = CupertinoGlyph.CircleGrid3x3
    static let Viewfinder = CupertinoGlyph.Viewfinder
    static let SquareSplit2x2 = CupertinoGlyph.SquareSplit2x2
    static let PaintbrushFill = CupertinoGlyph.PaintbrushFill
    static let RectangleArrowUpRightArrowDownLeft = CupertinoGlyph.RectangleArrowUpRightArrowDownLeft
    static let ArrowUpLeftArrowDownRight = CupertinoGlyph.ArrowUpLeftArrowDownRight
    static let Nosign = CupertinoGlyph.Nosign
    static let FolderBadgePlus = CupertinoGlyph.FolderBadgePlus
    static let SquareGrid3x2 = CupertinoGlyph.SquareGrid3x2
    static let LockOpenFill = CupertinoGlyph.LockOpenFill
    static let Snow = CupertinoGlyph.Snow
    static let BookmarkSolid = CupertinoGlyph.BookmarkSolid
    static let Metronome = CupertinoGlyph.Metronome
    static let ScissorsAlt = CupertinoGlyph.ScissorsAlt
    static let WandRaysInverse = CupertinoGlyph.WandRaysInverse
    static let PauseCircle = CupertinoGlyph.PauseCircle
    static let PlayCircle = CupertinoGlyph.PlayCircle
    static let SquareFill = CupertinoGlyph.SquareFill
    static let SquareStack3dDownDottedline = CupertinoGlyph.SquareStack3dDownDottedline
    static let RectangleDock = CupertinoGlyph.RectangleDock
    static let WaveformPathEcg = CupertinoGlyph.WaveformPathEcg
    static let Table = CupertinoGlyph.Table
    static let TextformatAlt = CupertinoGlyph.TextformatAlt
}

// =============================================================================
// Peças reutilizadas da casca (a base que a casca já usava)
// =============================================================================
/// Cabeçalho de seção dos painéis.
struct AureaSectionHeader: View {
    let title: String
    var trailing: AnyView? = nil

    var body: some View {
        HStack {
            Text(title)
                .font(AureaType.Section)
                .foregroundStyle(AureaColors.text)
            Spacer()
            if let trailing { trailing }
        }
        .padding(.horizontal, AureaDims.pad)
        .padding(.top, 10)
        .padding(.bottom, 4)
    }
}

/// Botão de ícone da casca (topo, transporte, linhas de camada).
struct AureaIconButton: View {
    let systemName: String
    var active: Bool = false
    var enabled: Bool = true
    var size: CGFloat = AureaDims.iconMd
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Image(systemName: systemName)
                .font(.aurea(size: size, weight: AureaIconWeight.symbol))
                .foregroundStyle(enabled ? (active ? AureaColors.accent : AureaColors.text) : AureaColors.disabled)
                .frame(width: AureaDims.chromeButtonW, height: AureaDims.chromeButtonW)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .disabled(!enabled)
    }
}

/// Rótulo + valor de uma linha de propriedade.
struct AureaPropertyRow<Content: View>: View {
    let title: String
    @ViewBuilder var content: Content

    var body: some View {
        HStack(spacing: 10) {
            Text(title)
                .font(AureaType.label)
                .foregroundStyle(AureaColors.muted)
                .frame(width: AureaDims.labelChipW, alignment: .leading)
            content
        }
        .padding(.horizontal, AureaDims.pad)
        .padding(.vertical, 5)
    }
}

/// Campo numérico do inspetor. Escreve o valor no motor pelo mesmo caminho dos
/// sliders — nunca guarda estado próprio além do texto em edição.
struct AureaNumberField: View {
    let value: Float
    var format: String = "%.1f"
    let onChange: (Float) -> Void

    @State private var text: String = ""
    @FocusState private var focused: Bool

    var body: some View {
        TextField("", text: $text)
            .keyboardType(.numbersAndPunctuation)
            .multilineTextAlignment(.center)
            .keepLtr()   // número com sinal: o menos fica na frente também no árabe
            .font(AureaType.value)
            .foregroundStyle(AureaColors.text)
            .padding(.vertical, 6)
            .frame(minWidth: AureaDims.valueBoxW + AureaDims.s1)
            .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: AureaDims.nameFieldRadius))
            .focused($focused)
            .onAppear { text = String(format: format, value) }
            // `onChange(of:perform:)` de um parâmetro: o de dois parâmetros é
            // iOS 17, e o piso do app é 16.
            .onChange(of: value) { newValue in
                if !focused { text = String(format: format, newValue) }
            }
            .onSubmit {
                if let parsed = Float(text.replacingOccurrences(of: ",", with: ".")), parsed.isFinite { onChange(parsed) }
                else { text = String(format: format, value) }
            }
            .onChange(of: focused) { editing in
                if !editing, let parsed = Float(text.replacingOccurrences(of: ",", with: ".")), parsed.isFinite, parsed != value { onChange(parsed) }
            }
    }
}

/// Losango de keyframe: aceso quando há keyframe NESTE instante.
struct AureaKeyframeDiamond: View {
    let on: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Image(systemName: on ? "diamond.fill" : "diamond")
                .font(.aurea(size: 12, weight: AureaIconWeight.symbol))
                .foregroundStyle(on ? AureaColors.keyframeOn : AureaColors.subtle)
                .frame(width: AureaDims.colorWell, height: AureaDims.colorWell)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

/// Exact vector paths from the Compose 1.7.8 dependency used by Android.
struct MaterialGlyph: View {
    let name: String
    var size: CGFloat
    var color: Color
    init(_ name: String, size: CGFloat = 24, color: Color = AureaColors.text) {
        self.name = name; self.size = size; self.color = color
    }
    var body: some View {
        // 0.3.4: desenhado uma vez; os que têm desenho de linha próprio (AureaLineIcon,
        // par do `AureaLineIcons` do Android) saem no traço fino do estilo dos ícones.
        Group {
            if let strokes = AureaLineIcon.forMaterial(name) {
                AureaLineIcon.view(strokes, size: size, color: color)
            } else {
                Image("Material-" + name.replacingOccurrences(of: ".", with: "-"))
                    .resizable().renderingMode(.template).scaledToFit()
                    .frame(width: size, height: size).foregroundStyle(color)
            }
        }
            // Os `automirrored` do Compose viram sozinhos em RTL; aqui é explícito.
            // O chevron de voltar dos painéis também espelha (aponta para "trás").
            .flipsForRightToLeftLayoutDirection(name.hasPrefix("automirrored.") || name.hasSuffix(".ChevronLeft"))
    }
}
