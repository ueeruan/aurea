package com.aurea.aurea.effects

import com.aurea.aurea.editor.panels.effectDisplayName
import com.aurea.aurea.editor.panels.effectSearchText
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.editor.panels.normalizeSearch
import com.aurea.aurea.engine.EffectCatalogEntry
import com.aurea.aurea.engine.ParamType
import com.aurea.aurea.ui.theme.CupertinoGlyph

// =============================================================================
//  O QUE O MOTOR NÃO CONTA SOBRE UM EFEITO (Fase 7.3 §12, §62, §68).
//
//  O motor publica key, nome, categoria e classe. O navegador precisa de mais:
//  a descrição curta de gente, a compatibilidade e o custo relativo. Tudo isso
//  é chaveado pelo `typeId` (FNV-1a da chave estável), então trocar o nome do
//  efeito no motor não quebra nada disto.
//
//  Efeito fora das tabelas cai na regra padrão — nunca fica sem descrição nem
//  sem compatibilidade.
// =============================================================================

/** Ordem das categorias no navegador; o que o motor inventar entra no fim, em ordem alfabética. */
val EffectCategoryOrder = listOf(
    "Distorcer",
    "Glitch",
    "Estilizar",
    "Cor",
    "Luz",
    "Desfoque",
    "Ruído",
    "Nitidez",
    "Tempo",
    "Transição",
    "Recorte",
    "Gerar",
    "Utilitário",
    "Controles de expressão",
)

/** Custo relativo derivado da classe do efeito: o que o usuário sente na prévia. */
fun effectCost(effectClass: Int): Int = when (effectClass) {
    1 -> 2      // Neighborhood: precisa da vizinhança
    2 -> 2      // Domain: quebra a fusão
    3 -> 3      // Temporal: guarda quadros
    4 -> 4      // Global: varre o quadro inteiro
    else -> 1   // PerPixel: funde com os vizinhos
}

/** Onde o efeito funciona (Fase 7.3 §62). */
enum class EffectTarget(val label: String) {
    Imagem("Imagem"),
    Video("Vídeo"),
    Texto("Texto"),
    Vetor("Vetor"),
    Forma("Forma"),
    Cena3D("Cena 3D"),
    PreComposicao("Pré-composição"),
    Ajuste("Camada de ajuste"),
}

/**
 * Onde cada efeito funciona. O motor aplica em qualquer camada que vire
 * textura — o que muda é só o que faz SENTIDO: um recorte por croma num texto
 * não tem o que recortar, um efeito de tempo não tem o que remapear.
 *
 * O padrão (efeito fora da tabela) é a lista inteira: prometer menos do que o
 * motor entrega seria esconder recurso que funciona.
 */
private val AllTargets = EffectTarget.entries.toList()

private val Table: Map<Int, EffectMeta> = buildMap {
    fun put(key: String, meta: EffectMeta) = put(effectTypeId(key), meta)

    put("aurea.transform", EffectMeta("Move, gira, escala e muda a opacidade da camada sem perder qualidade.", AllTargets))
    put("aurea.color.exposure", EffectMeta("Clareia ou escurece como a exposição de uma câmera: multiplica a luz sem estourar a cor.", AllTargets))
    put("aurea.color.brightness_contrast", EffectMeta("Sobe o brilho e abre ou fecha o contraste em volta do cinza médio.", AllTargets))
    put("aurea.color.saturation", EffectMeta("Deixa a cor mais viva ou leva tudo para o preto e branco.", AllTargets))
    put("aurea.color.tint", EffectMeta("Pinta as sombras de uma cor e as luzes de outra — o duotone do cinema.", AllTargets))
    put("aurea.color.matrix", EffectMeta("Mistura os canais R, G e B entre si: troca, soma e cruza cores.", AllTargets))
    put("aurea.color.levels", EffectMeta("Define onde começa o preto, onde termina o branco e onde fica o meio.", AllTargets))
    put("aurea.color.curves", EffectMeta("A curva de tom: controle fino de luz e de cada canal de cor.", AllTargets))
    put("aurea.blur.gaussian", EffectMeta("Desfoque suave e ajustável, com direção — dá para desfocar só num eixo.", AllTargets))
    put("aurea.blur.sharpen", EffectMeta("Realça os detalhes e as bordas sem inventar textura.", AllTargets))
    put("aurea.light.glow", EffectMeta("Espalha a luz das partes claras: o brilho que sai da tela.", AllTargets))
    put("aurea.light.rays", EffectMeta("Raios de luz que saem de um ponto, como o sol entrando pela lente.", AllTargets))
    put("aurea.light.sweep", EffectMeta("Uma faixa de luz atravessa a imagem, com largura e ângulo ajustáveis.", AllTargets))
    put("aurea.light.deep_glow", EffectMeta("Brilho em várias passadas: núcleo forte, halo largo e cor própria.", AllTargets))
    put(
        "aurea.stylize.motion_tile",
        EffectMeta(
            "Repete a imagem em mosaico até cobrir o quadro, com espelho e fase opcionais.",
            listOf(EffectTarget.Imagem, EffectTarget.Video, EffectTarget.Texto, EffectTarget.Vetor, EffectTarget.Forma, EffectTarget.PreComposicao, EffectTarget.Ajuste),
        ),
    )
    put("aurea.key.luma", EffectMeta("Remove o preto (ou o branco) da camada e deixa o resto aparecer.", listOf(EffectTarget.Imagem, EffectTarget.Video, EffectTarget.PreComposicao)))
    put("aurea.key.chroma", EffectMeta("Tira o fundo verde ou azul e limpa o derramamento da cor na borda.", listOf(EffectTarget.Imagem, EffectTarget.Video, EffectTarget.PreComposicao)))
    put("aurea.time.echo", EffectMeta("Deixa rastro: cópias da própria imagem atrasadas no tempo, com desvanecimento.", AllTargets))
    put("aurea.time.posterize", EffectMeta("Trava a taxa de quadros: a imagem passa a andar em passos, como animação desenhada.", AllTargets))
    put("aurea.time.warp_rgb", EffectMeta("Cada canal de cor vem de um instante diferente — o deslocamento RGB no tempo.", AllTargets))
    // --- Fase 7.3: o pacote novo ---------------------------------------------
    put("aurea.color.invert", EffectMeta("O negativo: cada cor vira o seu contrário, como um filme revelado errado.", AllTargets))
    put("aurea.color.colorama", EffectMeta("Remapeia a cor: a luz de cada pixel vira uma posição num arco-íris que gira.", AllTargets))
    put("aurea.blur.unsharp", EffectMeta("Máscara de nitidez de verdade: original + ganho × (original − borrado), com limiar para não amplificar o grão.", AllTargets))
    put("aurea.blur.lens", EffectMeta(
        "Desfoque de lente: um disco de amostras com o formato da íris, e as luzes pesam mais — é o bokeh, não um borrão.",
        AllTargets,
    ))
    put("aurea.light.deep_glow", EffectMeta("Brilho em dois halos, um apertado e um largo, com cor própria: o núcleo estoura e o ambiente preenche.", AllTargets))
    put("aurea.light.rays", EffectMeta("Raios de luz que saem de um ponto: a clareira das partes claras se espalha em linha reta.", AllTargets))
    put("aurea.light.sweep", EffectMeta("Uma lâmina de luz atravessa a imagem. Com relevo, ela acende só onde a superfície está virada para ela.", AllTargets))
    put(
        "aurea.distort.shake",
        EffectMeta(
            "Tremor determinístico: a camada treme igual toda vez que você reabre o projeto, com frequência e eixos separados.",
            listOf(EffectTarget.Imagem, EffectTarget.Video, EffectTarget.Texto, EffectTarget.Vetor, EffectTarget.Forma, EffectTarget.PreComposicao, EffectTarget.Ajuste),
        ),
    )
    put("aurea.distort.turbulence", EffectMeta("Um campo de ruído empurra cada pixel. O campo evolui com o tempo — fumaça, calor, água.", AllTargets))
    put("aurea.distort.wave_warp", EffectMeta("Uma onda atravessa a imagem, horizontal, vertical ou na diagonal, e pode ser travada nas bordas.", AllTargets))
    put("aurea.distort.warp", EffectMeta("Lente: empurrar, puxar, torcer, esfera e canto, com raio e ponto próprios.", AllTargets))
    put("aurea.distort.ripple_dissolve", EffectMeta("A imagem some em círculos que crescem do centro com a borda ondulando. Anime o progresso para virar transição.", AllTargets))
    put("aurea.stylize.scanlines", EffectMeta("Varredura de tela: a linha escurece o que está atrás dela, com altura, suavidade e canal próprios.", AllTargets))
    put("aurea.stylize.grain", EffectMeta("Grão de filme: cristal do tamanho que você quiser, luma e croma separados, mais forte nas sombras.", AllTargets))
    put("aurea.stylize.halftone", EffectMeta("Meio-tom: a imagem vira pontos, com a grade girada por canal para as três retículas não brigarem.", AllTargets))
    put("aurea.stylize.minimax", EffectMeta("Dilata ou erode o que estiver claro: engrossa ou afina um recorte e limpa um pixel de borda.", AllTargets))
    put("aurea.stylize.pixel_sort", EffectMeta("Ordena os pixels de cada linha pela luz: a imagem derrete em riscos.", AllTargets))
    put("aurea.stylize.film_damage", EffectMeta("Filme danificado: poeira, riscos, piscar, balanço de porta, queimado e emenda — cada um com o seu controle.", AllTargets))
    put("aurea.stylize.jpeg_damage", EffectMeta("Dano de JPEG: os blocos, o anelamento das bordas e a cor em meia resolução. Qualidade escala os três.", AllTargets))
    put("aurea.stylize.holomatrix", EffectMeta("Holograma: a imagem vira projeção, com grade técnica, varredura e interferência.", AllTargets))
    put("aurea.glitch.glitchify", EffectMeta("Glitch digital: a imagem se parte em blocos que deslizam, os canais se separam e blocos inteiros saem do lugar.", AllTargets))
    put("aurea.glitch.vhs", EffectMeta("VHS de verdade: borrão, croma alargada, instabilidade de tracking, perdas de fita, varredura e degradação de cor.", AllTargets))
    put("aurea.glitch.uni_vhs", EffectMeta("O VHS estilizado: separação RGB grande, ondulação, brilho sujo e vinheta — o vocabulário da fita a serviço do visual.", AllTargets))
    put("aurea.glitch.signal", EffectMeta("Sinal de transmissão: bandas que se perdem, sincronia que escorrega e deriva constante.", AllTargets))
    put("aurea.glitch.cross", EffectMeta("Duas faixas, uma vertical e uma horizontal, varrem a imagem lendo do lugar errado. Anime o progresso para virar transição.", AllTargets))
    put("aurea.time.posterize", EffectMeta(
        "Trava a taxa de quadros: a imagem passa a andar em passos, como animação desenhada.",
        listOf(EffectTarget.Video, EffectTarget.PreComposicao, EffectTarget.Imagem, EffectTarget.Ajuste),
        "stop motion quadros taxa travada",
    ))
    put("aurea.time.warp_rgb", EffectMeta(
        "Cada canal de cor vem de um instante diferente: o vermelho do quadro de trás, o azul do da frente.",
        listOf(EffectTarget.Video, EffectTarget.PreComposicao, EffectTarget.Imagem),
        "rgb no tempo canais separados atraso de cor",
    ))
    put("aurea.control.slider", EffectMeta("Um valor de 0 a 100 que você liga na expressão de outro parâmetro.", AllTargets))
    put("aurea.control.angle", EffectMeta("Um ângulo que você liga na expressão de outro parâmetro.", AllTargets))
    put("aurea.control.checkbox", EffectMeta("Um liga/desliga que você liga na expressão de outro parâmetro.", AllTargets))
    put("aurea.control.color", EffectMeta("Uma cor que você liga na expressão de outro parâmetro.", AllTargets))
    put("aurea.control.point", EffectMeta("Um ponto X/Y que você liga na expressão de outro parâmetro.", AllTargets))
}

/** Descrição, alvos e palavras extras de um efeito. */
class EffectMeta(
    val description: String,
    val targets: List<EffectTarget> = AllTargets,
    val keywords: String = "",
)

internal fun effectMeta(typeId: Int): EffectMeta? = Table[typeId]

/** A descrição de gente. Efeito sem ficha não fica mudo: diz o que ele é. */
fun effectDescription(typeId: Int, category: String): String =
    Table[typeId]?.description ?: "Efeito de $category. Os parâmetros estão no painel do efeito."

/** Onde ele funciona. Fora da tabela, em todo lugar (é o que o motor faz). */
fun effectTargets(typeId: Int): List<EffectTarget> = Table[typeId]?.targets ?: AllTargets

/** A linha curta de compatibilidade: "Vídeo, imagem e pré-composição". */
fun effectCompatibilityLine(typeId: Int): String = when (val t = effectTargets(typeId)) {
    AllTargets -> "Funciona em qualquer camada"
    else -> t.joinToString(", ") { it.label }
}

/** Categorias do catálogo, na ordem do navegador. */
fun effectCategories(catalog: List<EffectCatalogEntry>): List<String> {
    val names = catalog.map { it.category }.distinct()
    return names.sortedWith(
        compareBy(
            { n -> EffectCategoryOrder.indexOf(n).let { if (it < 0) EffectCategoryOrder.size else it } },
            { n -> n.lowercase() },
        ),
    )
}

/** Os efeitos agrupados por categoria, na ordem das fichas e por nome humano. */
fun arrangeCatalog(catalog: List<EffectCatalogEntry>, categories: List<String>): List<EffectCatalogEntry> =
    catalog.sortedWith(compareBy({ categories.indexOf(it.category) }, { effectDisplayName(it.typeId, it.name) }))

/** Ícone da categoria — o rótulo visual do cartão (não é a prévia). */
fun categoryGlyph(category: String): Char = when (normalizeSearch(category)) {
    "cor" -> CupertinoGlyph.ColorFilter
    "desfoque" -> CupertinoGlyph.DropFill
    "luz", "glow e luz" -> CupertinoGlyph.Sparkles
    "estilizar" -> CupertinoGlyph.SquareGrid2x2
    "distorcer" -> CupertinoGlyph.Move
    "recorte" -> CupertinoGlyph.Scissors
    "tempo", "transicao" -> CupertinoGlyph.Timer
    "ruido" -> CupertinoGlyph.Waveform
    "nitidez" -> CupertinoGlyph.Eyedropper
    "glitch" -> CupertinoGlyph.Bolt
    "gerar" -> CupertinoGlyph.WandStars
    "controles de expressao" -> CupertinoGlyph.SliderHorizontal3
    else -> CupertinoGlyph.WandStars
}

/**
 * O texto onde a busca procura: nome humano, nome do motor, categoria,
 * sinônimos do painel e a descrição. "vhs" acha o VHS pelo nome; "analogico"
 * acha pela descrição.
 */
fun catalogSearchText(entry: EffectCatalogEntry): String = normalizeSearch(
    listOf(
        effectSearchText(entry.typeId, entry.name, entry.category),
        effectDescription(entry.typeId, entry.category),
        Table[entry.typeId]?.keywords.orEmpty(),
    ).joinToString(" "),
)

/** O que o seletor de categoria mostra. */
sealed interface EffectFilter {
    data object All : EffectFilter
    data object Recent : EffectFilter
    data object Favorite : EffectFilter
    data class Category(val name: String) : EffectFilter
}

/**
 * Os efeitos que passam pelo filtro e pela busca, na ordem de exibição.
 * Busca vazia = o filtro manda; busca cheia = a busca manda (em todo o catálogo).
 */
fun filterCatalog(
    catalog: List<EffectCatalogEntry>,
    sorted: List<EffectCatalogEntry>,
    haystack: Map<Int, String>,
    query: String,
    filter: EffectFilter,
    recents: List<Int>,
    favorites: Set<Int>,
): List<EffectCatalogEntry> {
    val q = normalizeSearch(query)
    if (q.isNotEmpty()) {
        val terms = q.split(' ').filter { it.isNotEmpty() }
        return sorted.filter { e -> terms.all { haystack[e.typeId].orEmpty().contains(it) } }
    }
    return when (filter) {
        EffectFilter.All -> sorted
        EffectFilter.Recent -> recents.mapNotNull { id -> catalog.firstOrNull { it.typeId == id } }
        EffectFilter.Favorite -> sorted.filter { it.typeId in favorites }
        is EffectFilter.Category -> sorted.filter { it.category == filter.name }
    }
}

/** Índice de busca do catálogo inteiro (uma passada, memoizado por quem chama). */
fun catalogHaystack(catalog: List<EffectCatalogEntry>): Map<Int, String> =
    catalog.associate { it.typeId to catalogSearchText(it) }

/** Rótulo humano do tipo de parâmetro, para a ficha de documentação visual (§68). */
fun paramTypeLabel(type: Int): String = when (type) {
    ParamType.FLOAT -> "número"
    ParamType.INT -> "inteiro"
    ParamType.BOOL -> "ligado/desligado"
    ParamType.COLOR -> "cor"
    ParamType.POINT2D -> "ponto"
    ParamType.POINT3D -> "ponto 3D"
    ParamType.ANGLE -> "ângulo"
    ParamType.ENUM -> "escolha"
    ParamType.CURVE -> "curva"
    ParamType.GRADIENT -> "degradê"
    else -> "valor"
}
