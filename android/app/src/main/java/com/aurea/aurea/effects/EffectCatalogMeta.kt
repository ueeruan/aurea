package com.aurea.aurea.effects

import com.aurea.aurea.editor.panels.effectDisplayName
import com.aurea.aurea.editor.panels.effectNameRank
import com.aurea.aurea.editor.panels.effectSearchText
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.editor.panels.normalizeSearch
import com.aurea.aurea.engine.EffectCatalogEntry
import com.aurea.aurea.engine.ParamType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R

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

/**
 * Onde o efeito funciona (Fase 7.3 §62).
 *
 * [label] é ID de recurso: o rótulo muda de idioma, o NOME do enum não — é ele
 * que está no código e que casa com a tabela de cada efeito.
 */
enum class EffectTarget(@StringRes val label: Int) {
    Imagem(R.string.target_image),
    Video(R.string.target_video),
    Texto(R.string.target_text),
    Vetor(R.string.target_vector),
    Forma(R.string.target_shape),
    Cena3D(R.string.target_3d),
    PreComposicao(R.string.target_precomp),
    Ajuste(R.string.target_adjust),
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

    put("aurea.transform", EffectMeta(R.string.fx_desc_transform, AllTargets))
    put("aurea.color.exposure", EffectMeta(R.string.fx_desc_color_exposure, AllTargets))
    put("aurea.color.brightness_contrast", EffectMeta(R.string.fx_desc_color_brightness_contrast, AllTargets))
    put("aurea.color.saturation", EffectMeta(R.string.fx_desc_color_saturation, AllTargets))
    put("aurea.color.tint", EffectMeta(R.string.fx_desc_color_tint, AllTargets))
    put("aurea.color.matrix", EffectMeta(R.string.fx_desc_color_matrix, AllTargets))
    put("aurea.color.levels", EffectMeta(R.string.fx_desc_color_levels, AllTargets))
    put("aurea.color.curves", EffectMeta(R.string.fx_desc_color_curves, AllTargets))
    put("aurea.blur.gaussian", EffectMeta(R.string.fx_desc_blur_gaussian, AllTargets))
    put("aurea.blur.sharpen", EffectMeta(R.string.fx_desc_blur_sharpen, AllTargets))
    put("aurea.light.glow", EffectMeta(R.string.fx_desc_light_glow, AllTargets))
    put(
        "aurea.stylize.motion_tile",
        EffectMeta(
            R.string.fx_desc_stylize_motion_tile,
            listOf(EffectTarget.Imagem, EffectTarget.Video, EffectTarget.Texto, EffectTarget.Vetor, EffectTarget.Forma, EffectTarget.PreComposicao, EffectTarget.Ajuste),
        ),
    )
    put("aurea.key.luma", EffectMeta(R.string.fx_desc_key_luma, listOf(EffectTarget.Imagem, EffectTarget.Video, EffectTarget.PreComposicao)))
    put("aurea.key.chroma", EffectMeta(R.string.fx_desc_key_chroma, listOf(EffectTarget.Imagem, EffectTarget.Video, EffectTarget.PreComposicao)))
    put("aurea.time.echo", EffectMeta(R.string.fx_desc_time_echo, AllTargets))
    // --- Fase 7.3: o pacote novo ---------------------------------------------
    put("aurea.color.invert", EffectMeta(R.string.fx_desc_color_invert, AllTargets))
    put("aurea.color.colorama", EffectMeta(R.string.fx_desc_color_colorama, AllTargets))
    put("aurea.blur.unsharp", EffectMeta(R.string.fx_desc_blur_unsharp, AllTargets))
    put("aurea.blur.lens", EffectMeta(
        R.string.fx_desc_blur_lens,
        AllTargets,
    ))
    put("aurea.light.deep_glow", EffectMeta(R.string.fx_desc_light_deep_glow, AllTargets))
    put("aurea.light.rays", EffectMeta(R.string.fx_desc_light_rays, AllTargets))
    put("aurea.light.sweep", EffectMeta(R.string.fx_desc_light_sweep, AllTargets))
    put(
        "aurea.distort.shake",
        EffectMeta(
            R.string.fx_desc_distort_shake,
            listOf(EffectTarget.Imagem, EffectTarget.Video, EffectTarget.Texto, EffectTarget.Vetor, EffectTarget.Forma, EffectTarget.PreComposicao, EffectTarget.Ajuste),
        ),
    )
    put("aurea.distort.turbulence", EffectMeta(R.string.fx_desc_distort_turbulence, AllTargets))
    put("aurea.distort.wave_warp", EffectMeta(R.string.fx_desc_distort_wave_warp, AllTargets))
    put("aurea.distort.warp", EffectMeta(R.string.fx_desc_distort_warp, AllTargets))
    put("aurea.distort.ripple_dissolve", EffectMeta(R.string.fx_desc_distort_ripple_dissolve, AllTargets))
    put("aurea.stylize.scanlines", EffectMeta(R.string.fx_desc_stylize_scanlines, AllTargets))
    put("aurea.stylize.grain", EffectMeta(R.string.fx_desc_stylize_grain, AllTargets))
    put("aurea.stylize.halftone", EffectMeta(R.string.fx_desc_stylize_halftone, AllTargets))
    put("aurea.stylize.minimax", EffectMeta(R.string.fx_desc_stylize_minimax, AllTargets))
    put("aurea.stylize.pixel_sort", EffectMeta(R.string.fx_desc_stylize_pixel_sort, AllTargets))
    put("aurea.stylize.film_damage", EffectMeta(R.string.fx_desc_stylize_film_damage, AllTargets))
    put("aurea.stylize.jpeg_damage", EffectMeta(R.string.fx_desc_stylize_jpeg_damage, AllTargets))
    put("aurea.stylize.holomatrix", EffectMeta(R.string.fx_desc_stylize_holomatrix, AllTargets))
    put("aurea.glitch.glitchify", EffectMeta(R.string.fx_desc_glitch_glitchify, AllTargets))
    put("aurea.glitch.vhs", EffectMeta(R.string.fx_desc_glitch_vhs, AllTargets))
    put("aurea.glitch.uni_vhs", EffectMeta(R.string.fx_desc_glitch_uni_vhs, AllTargets))
    put("aurea.glitch.signal", EffectMeta(R.string.fx_desc_glitch_signal, AllTargets))
    put("aurea.glitch.cross", EffectMeta(R.string.fx_desc_glitch_cross, AllTargets))
    put("aurea.time.posterize", EffectMeta(
        R.string.fx_desc_time_posterize,
        listOf(EffectTarget.Video, EffectTarget.PreComposicao, EffectTarget.Imagem, EffectTarget.Ajuste),
        "stop motion quadros taxa travada",
    ))
    put("aurea.time.warp_rgb", EffectMeta(
        R.string.fx_desc_time_warp_rgb,
        listOf(EffectTarget.Video, EffectTarget.PreComposicao, EffectTarget.Imagem),
        "rgb no tempo canais separados atraso de cor",
    ))
    put("aurea.control.slider", EffectMeta(R.string.fx_desc_control_slider, AllTargets))
    put("aurea.control.angle", EffectMeta(R.string.fx_desc_control_angle, AllTargets))
    put("aurea.control.checkbox", EffectMeta(R.string.fx_desc_control_checkbox, AllTargets))
    put("aurea.control.color", EffectMeta(R.string.fx_desc_control_color, AllTargets))
    put("aurea.control.point", EffectMeta(R.string.fx_desc_control_point, AllTargets))
}

/** Descrição, alvos e palavras extras de um efeito. */
class EffectMeta(
    @StringRes val description: Int,
    val targets: List<EffectTarget> = AllTargets,
    val keywords: String = "",
)

/**
 * A descrição de gente. Efeito sem ficha não fica mudo: diz o que ele é.
 *
 * @Composable porque a frase padrão é texto de interface — e ela leva a
 * CATEGORIA dentro, que é rótulo traduzido, não o nome que o motor publica.
 */
@Composable
fun effectDescription(typeId: Int, category: String): String =
    Table[typeId]?.let { stringResource(it.description) }
        ?: stringResource(R.string.effect_default_description, effectCategoryLabel(category))

/** Onde ele funciona. Fora da tabela, em todo lugar (é o que o motor faz). */
fun effectTargets(typeId: Int): List<EffectTarget> = Table[typeId]?.targets ?: AllTargets

/** A linha curta de compatibilidade: "Vídeo, imagem e pré-composição". */
@Composable
fun effectCompatibilityLine(typeId: Int): String = when (val t = effectTargets(typeId)) {
    AllTargets -> stringResource(R.string.effect_any_layer)
    else -> t.map { stringResource(it.label) }.joinToString(", ")
}

/**
 * A linha de custo do cartão: "Desfoque · pesado para o celular".
 *
 * Sem custo acima do normal é só a categoria — a frase inteira existe porque a
 * montagem com `·` e a palavra "para o celular" são texto de interface.
 */
@Composable
fun effectCostLine(category: String, cost: Int): String {
    val label = effectCategoryLabel(category)
    return if (cost > 1) stringResource(R.string.effect_cost_heavy, label) else label
}

/**
 * O RÓTULO de uma categoria, no idioma do app.
 *
 * A categoria em si é IDENTIDADE do motor (a string "Distorcer" viaja no
 * catálogo e no `.aurea`): o que muda é só o que o usuário lê. Categoria que a
 * UI ainda não conhece cai no próprio nome — melhor um rótulo em português do
 * que um espaço vazio.
 */
@Composable
fun effectCategoryLabel(category: String): String = when (normalizeSearch(category)) {
    "distorcer" -> stringResource(R.string.cat_distort)
    "glitch" -> stringResource(R.string.cat_glitch)
    "estilizar" -> stringResource(R.string.cat_stylize)
    "cor" -> stringResource(R.string.cat_colour)
    "luz", "glow e luz" -> stringResource(R.string.cat_light)
    "desfoque" -> stringResource(R.string.cat_blur)
    "ruido" -> stringResource(R.string.cat_noise)
    "nitidez" -> stringResource(R.string.cat_sharpen)
    "tempo" -> stringResource(R.string.cat_time)
    "transicao" -> stringResource(R.string.cat_transition)
    "recorte" -> stringResource(R.string.cat_cutout)
    "gerar" -> stringResource(R.string.cat_generate)
    "utilitario" -> stringResource(R.string.cat_utility)
    "controles de expressao" -> stringResource(R.string.cat_expr)
    else -> category
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

/**
 * Os efeitos agrupados por categoria, na ordem das fichas e por nome humano.
 *
 * A ordenação NÃO pode depender do idioma (senão a lista se reordena ao trocar
 * de língua): dentro da categoria vale a ordem em que a tabela humana declara
 * os efeitos, e o nome do motor decide o que não está na tabela.
 */
fun arrangeCatalog(catalog: List<EffectCatalogEntry>, categories: List<String>): List<EffectCatalogEntry> =
    catalog.sortedWith(compareBy({ categories.indexOf(it.category) }, { effectNameRank(it.typeId) }, { it.name }))

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
@Composable
private fun catalogSearchText(entry: EffectCatalogEntry): String = normalizeSearch(
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

/**
 * Índice de busca do catálogo inteiro.
 *
 * O texto indexado é LOCALIZADO (nome humano e descrição vêm do catálogo do
 * idioma), então isto precisa de contexto composable — e o `remember` é
 * chaveado no IDIOMA: trocar de idioma reindexa a busca, senão "blur" não
 * acharia nada depois de mudar para inglês.
 *
 * A parte cara (`normalizeSearch`, que passa regex em cada descrição) fica
 * DENTRO do `remember`; a montagem das frases, que `stringResource` obriga a
 * fazer fora, é só concatenação.
 */
@Composable
fun catalogHaystack(catalog: List<EffectCatalogEntry>): Map<Int, String> {
    val locale = androidx.compose.ui.platform.LocalConfiguration.current.locales[0]
    val parts = catalog.associate { e ->
        e.typeId to listOf(
            effectDisplayName(e.typeId, e.name),
            e.name,
            e.category,
            Table[e.typeId]?.keywords.orEmpty(),
            effectDescription(e.typeId, e.category),
        ).joinToString(" ")
    }
    return remember(catalog, locale) { parts.mapValues { (_, text) -> normalizeSearch(text) } }
}
