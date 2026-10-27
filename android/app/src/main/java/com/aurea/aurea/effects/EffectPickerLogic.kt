package com.aurea.aurea.effects

import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.editor.panels.normalizeSearch
import com.aurea.aurea.engine.EffectCatalogEntry

// =============================================================================
//  O ESCOLHEDOR DE EFEITOS — a LÓGICA pura (sem Compose, testada na JVM).
//
//  O painel "Efeitos" mostra a pilha da camada; "+ Adicionar efeito" abre a tela
//  cheia "Adicionar efeito" (destaques, recentes, ladrilhos de GRUPO e busca).
//  Tudo aqui é montado a partir do CATÁLOGO que o motor publica — nenhuma lista
//  fixa de efeitos: efeito ou categoria novos aparecem sozinhos (categoria que a
//  tabela de grupos não conhece cai em "Outros"). As FERRAMENTAS-EFEITO
//  (legendas, rastreio de câmera, máscara) entram como entradas do catálogo.
//
//  Regras:
//   · camada com efeito abre na pilha; sem nada, direto em "Adicionar efeito";
//   · efeito de ÁUDIO (categoria "Áudio") só aparece para camada com som — a não
//     ser o que GERA som (o Tom), que serve para qualquer camada;
//   · a busca não liga para acento nem caixa, e acha pelo nome em qualquer um
//     dos 7 idiomas do app, pelos sinônimos e pela descrição; o nome vem antes.
// =============================================================================

/** Id da ficha "Todos" (testTag `effects.category.all`). */
const val ALL_CATEGORIES_ID = "all"

/**
 * Id estável de uma categoria para testTag/identificador: o nome do motor sem
 * acento, em minúsculas, com `_` no lugar do que não é letra ou número
 * ("Controles de expressão" → `controles_de_expressao`, "Áudio" → `audio`).
 */
fun effectCategoryId(category: String): String {
    val slug = normalizeSearch(category).replace(NotSlug, "_").trim('_')
    // Nome sem letra latina nenhuma: o hash FNV do nome (o mesmo do iOS).
    return slug.ifEmpty { Integer.toHexString(effectTypeId(category)) }
}

private val NotSlug = Regex("[^a-z0-9]+")

/** Id do cartão de um efeito (testTag `effects.card.<id>`): o `typeId` sem sinal, igual no iOS. */
fun effectCardId(typeId: Int): String = Integer.toUnsignedString(typeId)

// --- Áudio -------------------------------------------------------------------

private val AudioCategories = setOf("audio", "som", "sound")

/** A categoria é de SOM (o efeito não mexe no pixel, mexe no áudio da camada). */
fun isAudioCategory(category: String): Boolean = normalizeSearch(category) in AudioCategories

/**
 * Efeitos que GERAM som (o Tom): servem até para camada muda, então não se
 * escondem. Reconhecidos pela chave (as grafias que o motor pode usar) ou pelo
 * nome do motor.
 */
private val SoundGeneratorIds: Set<Int> by lazy {
    listOf(
        "aurea.audio.tone", "aurea.audio.tom", "aurea.audio.tone_generator",
        "aurea.audio.test_tone", "aurea.generate.tone", "aurea.generate.tone_generator",
    ).mapTo(HashSet()) { effectTypeId(it) }
}
private val SoundGeneratorNames = setOf("tom", "tone", "gerador de tom", "tone generator", "tom de teste", "test tone")

fun makesSound(entry: EffectCatalogEntry): Boolean =
    entry.typeId in SoundGeneratorIds || normalizeSearch(entry.name) in SoundGeneratorNames

/** O que o catálogo oferece para ESTA camada: sem som, some o que só mexe no som. */
fun pickableEffects(catalog: List<EffectCatalogEntry>, layerHasAudio: Boolean, layerKind: Int = 0): List<EffectCatalogEntry> =
    catalog.filter {
        it.typeId !in setOf(effectTypeId("aurea.motion.oscillate"), effectTypeId("aurea.layout.grid_builder"), effectTypeId("aurea.layout.grid_item"), effectTypeId("aurea.light.scene_flare")) &&
            (layerHasAudio || !isAudioCategory(it.category) || makesSound(it)) &&
            (it.typeId != effectTypeId("aurea.generate.raster_extrude") || layerKind in setOf(1, 2, 4, 5, 12))
    }

// --- Ferramentas que moram no catálogo de efeitos ------------------------------

/**
 * Ferramentas que o usuário procura como EFEITO (pedido de 2026-10-01): as
 * legendas automáticas (categoria Texto), o rastreio de câmera (Movimentar e
 * transformar) e a máscara (Fosco, máscara e chave). No catálogo elas são uma
 * entrada como outra qualquer — busca, recentes e favoritos valem —, mas o toque
 * ABRE a ferramenta existente em vez de pôr um efeito na pilha do motor.
 *
 * [key] é uma chave falsa e estável (o `typeId` é o FNV dela, como o de um
 * efeito); [category] é a categoria que manda a entrada para o grupo certo.
 */
enum class EffectTool(val key: String, val category: String) {
    Captions("aurea.tool.auto_captions", "Texto"),
    CameraTrack("aurea.tool.camera_track", "Rastreio"),
    Mask("aurea.tool.mask", "Máscara"),
    ;

    val typeId: Int get() = effectTypeId(key)
}

/** A ferramenta por trás de um `typeId` do catálogo (nulo = efeito de verdade). */
fun effectToolOf(typeId: Int): EffectTool? = EffectTool.entries.firstOrNull { it.typeId == typeId }

/** Rastreio de câmera só tem o que analisar num VÍDEO (tipo 1); o resto serve para qualquer camada. */
fun pickableTools(layerKind: Int): List<EffectTool> =
    EffectTool.entries.filter { it != EffectTool.CameraTrack || layerKind == 1 }

/** A ferramenta como entrada do catálogo; [name] é o rótulo no idioma do app. */
fun toolCatalogEntry(tool: EffectTool, name: String): EffectCatalogEntry =
    EffectCatalogEntry(typeId = tool.typeId, effectClass = 0, paramCount = 0, name = name, category = tool.category)

// --- Grupos da tela "Adicionar efeito" -----------------------------------------

/**
 * Os GRUPOS (os ladrilhos de categoria do navegador de efeitos). O motor
 * publica categorias em português e em inglês, misturadas ("Cor" e "Color",
 * "Distorcer" e "Distort"); a tela junta tudo em grupos de gente, na ordem
 * abaixo (a ordem dos ladrilhos na tela). [id] é o testTag/identificador
 * (`effects.category.<id>`), igual no iOS.
 *
 * Omega efeitos (navegador, P0-6): primeiro os grupos da referência, na ordem
 * dela (Cor e luz … Visibilidade, Texto); depois os extras do AUREA (Estilizar,
 * Glitch, Tempo, Áudio, Utilitário, Plugins); "Outros" sempre por último.
 */
enum class EffectGroup(val id: String) {
    ColorLight("color_light"),
    DrawEdge("draw_edge"),
    Blur("blur"),
    Distort("distort"),
    Procedural("procedural"),
    ThreeD("3d"),
    Motion("motion"),
    Repeat("repeat"),
    Matte("matte"),
    Visibility("visibility"),
    Text("text"),
    Stylize("stylize"),
    Glitch("glitch"),
    Time("time"),
    Audio("audio"),
    Utility("utility"),
    /** 0.3.0: efeitos instalados por plugin (categoria "Plugins" do motor). */
    Plugins("plugins"),
    Other("other"),
}

/** Categoria do motor (normalizada) → grupo. Categoria nova sem entrada cai em "Outros". */
private val GroupByCategory: Map<String, EffectGroup> = buildMap {
    listOf("cor", "color", "colour", "luz", "light", "glow e luz").forEach { put(it, EffectGroup.ColorLight) }
    listOf("desenho", "drawing", "contorno", "edge").forEach { put(it, EffectGroup.DrawEdge) }
    listOf("desfoque", "blur", "nitidez", "sharpen").forEach { put(it, EffectGroup.Blur) }
    listOf("distorcer", "distort", "distorcao").forEach { put(it, EffectGroup.Distort) }
    listOf("transform", "transformar", "movimento", "motion", "rastreio", "tracking").forEach { put(it, EffectGroup.Motion) }
    listOf("repetir", "repeat").forEach { put(it, EffectGroup.Repeat) }
    listOf("estilizar", "stylize", "stylise").forEach { put(it, EffectGroup.Stylize) }
    put("glitch", EffectGroup.Glitch)
    listOf("gerar", "generate", "pattern", "ruido", "noise").forEach { put(it, EffectGroup.Procedural) }
    listOf("recorte", "keying", "key", "mascara", "matte").forEach { put(it, EffectGroup.Matte) }
    listOf("transicao", "transition", "opacidade", "opacity", "visibilidade", "visibility").forEach { put(it, EffectGroup.Visibility) }
    listOf("tempo", "time").forEach { put(it, EffectGroup.Time) }
    listOf("texto", "text").forEach { put(it, EffectGroup.Text) }
    put("3d", EffectGroup.ThreeD)
    put("plugins", EffectGroup.Plugins)
    listOf("audio", "som", "sound").forEach { put(it, EffectGroup.Audio) }
    listOf("utilitario", "utility", "controles de expressao", "expression controls").forEach { put(it, EffectGroup.Utility) }
}

/** Efeitos que moram num grupo diferente do da categoria do motor (o que eles FAZEM manda). */
private val GroupByKey: Map<Int, EffectGroup> by lazy {
    buildMap {
        put(effectTypeId("aurea.generate.raster_extrude"), EffectGroup.ThreeD)
        listOf(
            "aurea.stylize.stroke_outline", "aurea.stylize.border", "aurea.stylize.drop_shadow",
            "aurea.stylize.find_edges", "aurea.stylize.bevel_alpha", "aurea.draw.drawing_progress",
        ).forEach { put(effectTypeId(it), EffectGroup.DrawEdge) }
        listOf(
            "aurea.transform", "aurea.motion.oscillate.cycles", "aurea.motion.swing", "aurea.motion.wiggle",
            "aurea.motion.twitch", "aurea.distort.shake", "aurea.distort.corner_pin", "aurea.transform.parenting_helper",
        ).forEach { put(effectTypeId(it), EffectGroup.Motion) }
        listOf(
            "aurea.stylize.repeat_tile", "aurea.stylize.motion_tile", "aurea.stylize.tiler", "aurea.stylize.hex_tile",
            "aurea.distort.kaleidoscope", "aurea.distort.mirror", "aurea.distort.griddler", "aurea.stylize.ball_grid",
        ).forEach { put(effectTypeId(it), EffectGroup.Repeat) }
    }
}

/** O grupo de uma entrada do catálogo (efeito ou ferramenta). */
fun effectGroupOf(entry: EffectCatalogEntry): EffectGroup =
    GroupByKey[entry.typeId] ?: GroupByCategory[normalizeSearch(entry.category)] ?: EffectGroup.Other

/** Os grupos que têm alguma entrada, na ordem fixa, cada um com as entradas na ordem recebida. */
fun groupEntries(sorted: List<EffectCatalogEntry>): List<Pair<EffectGroup, List<EffectCatalogEntry>>> {
    val byGroup = sorted.groupBy(::effectGroupOf)
    return EffectGroup.entries.mapNotNull { g -> byGroup[g]?.takeIf { it.isNotEmpty() }?.let { g to it } }
}

// --- Chaves das listas preguiçosas (bugs 0.3.4, bug 3) -------------------------
//
// Toda chave de LazyColumn/LazyRow/LazyVerticalGrid do navegador de efeitos leva
// o PREFIXO do tipo de item ("section:", "header:", "category:", "action:",
// "effect:", "tag:", "empty:", "plugin:"): um id cru de um tipo (o grupo
// "plugins", um typeId, uma etiqueta) nunca bate com o de outro. Chave repetida
// derruba o app ("Key … was already used", SubcomposeLayout).

/** Chave fixa da porta "Gerenciar plugins" no fim da tela inicial do navegador. */
const val PLUGIN_DOOR_KEY = "action:manage-plugins"

/** Faixa de recomendados da tela inicial. */
const val HOME_RECOMMENDED_KEY = "section:recommended"

/** Faixa Recentes/Favoritos da tela inicial. */
const val HOME_QUICK_KEY = "section:quick"

/** O título (só acessibilidade) das categorias na tela inicial. */
const val HOME_CATEGORIES_HEADER_KEY = "header:categories"

/** A linha "nada aqui" de uma grade ou da busca. */
const val EMPTY_ITEM_KEY = "empty:message"

/**
 * Chave de uma linha de ladrilhos na tela inicial. Prefixada para nunca colidir
 * com as chaves fixas da lista (destaques, rápidos, porta dos plugins): o id cru
 * do grupo "plugins" batia com a chave da porta e o Compose derrubava o app.
 */
fun groupRowKey(row: List<EffectGroup>): String = "category:" + row.joinToString(",") { it.id }

/** Chave de um efeito (cartão, recomendado, resultado da busca): o typeId sem sinal. */
fun effectItemKey(typeId: Int): String = "effect:" + Integer.toUnsignedString(typeId)

/** Chave de uma etiqueta sugerida na busca. */
fun tagItemKey(tag: String): String = "tag:$tag"

/** Chave da linha de um plugin instalado (gerenciador de plugins). */
fun pluginItemKey(id: String): String = "plugin:$id"

/**
 * Um efeito por `typeId`, na ordem recebida. O catálogo do motor não repete
 * tipos, mas um plugin cuja chave cai no mesmo hash de outra entrada (ou de uma
 * ferramenta-efeito) repetiria o cartão e a chave da grade: fica a primeira.
 */
fun distinctEffects(entries: List<EffectCatalogEntry>): List<EffectCatalogEntry> = entries.distinctBy { it.typeId }

/** Um item da tela inicial do navegador, na ordem em que aparece, com a sua chave. */
sealed class BrowserHomeRow(val key: String) {
    data object Recommended : BrowserHomeRow(HOME_RECOMMENDED_KEY)
    data object Quick : BrowserHomeRow(HOME_QUICK_KEY)
    data object CategoriesHeader : BrowserHomeRow(HOME_CATEGORIES_HEADER_KEY)
    class Categories(val groups: List<EffectGroup>) : BrowserHomeRow(groupRowKey(groups))
    data object PluginDoor : BrowserHomeRow(PLUGIN_DOOR_KEY)
}

/**
 * Os itens da tela inicial: recomendados e rápidos (se houver), o título das
 * categorias, as linhas de 2 ladrilhos e a porta dos plugins, sempre no fim.
 */
fun browserHomeRows(hasRecommended: Boolean, hasQuick: Boolean, groups: List<EffectGroup>): List<BrowserHomeRow> = buildList {
    if (hasRecommended) add(BrowserHomeRow.Recommended)
    if (hasQuick) add(BrowserHomeRow.Quick)
    add(BrowserHomeRow.CategoriesHeader)
    groups.distinct().chunked(2).forEach { add(BrowserHomeRow.Categories(it)) }
    add(BrowserHomeRow.PluginDoor)
}

/** O efeito cuja prévia (escurecida) vira o fundo do ladrilho do grupo. */
private val GroupBannerKeys: Map<EffectGroup, String> = mapOf(
    EffectGroup.ColorLight to "aurea.color.colorama",
    EffectGroup.Blur to "aurea.blur.radial",
    EffectGroup.Distort to "aurea.distort.wave_warp",
    EffectGroup.Motion to "aurea.distort.corner_pin",
    EffectGroup.Repeat to "aurea.stylize.motion_tile",
    EffectGroup.Stylize to "aurea.stylize.halftone",
    EffectGroup.Glitch to "aurea.glitch.glitchify",
    EffectGroup.DrawEdge to "aurea.stylize.find_edges",
    EffectGroup.Procedural to "aurea.generate.fractal_noise",
    EffectGroup.Matte to "aurea.key.chroma",
    EffectGroup.Visibility to "aurea.transition.radial_wipe",
    EffectGroup.Time to "aurea.time.warp_rgb",
)

/**
 * A entrada que dá a prévia do ladrilho: a escolhida para o grupo se ela está
 * no catálogo; senão o primeiro EFEITO do grupo (ferramenta não tem prévia).
 */
fun groupBannerEntry(group: EffectGroup, entries: List<EffectCatalogEntry>): EffectCatalogEntry? {
    val preferred = GroupBannerKeys[group]?.let(::effectTypeId)
    return entries.firstOrNull { it.typeId == preferred } ?: entries.firstOrNull { effectToolOf(it.typeId) == null }
}

/** A faixa de DESTAQUES: estes, na ordem, os que existirem para esta camada. */
private val FeaturedKeys = listOf(
    "aurea.draw.drawing_progress", "aurea.key.chroma", "aurea.blur.directional", "aurea.light.deep_glow",
    "aurea.glitch.vhs", EffectTool.Captions.key, "aurea.stylize.halftone", "aurea.distort.wave_warp",
    "aurea.light.rays", "aurea.color.colorama", "aurea.stylize.pixel_sort", "aurea.generate.fractal_noise",
)

/** Os destaques fixos e, depois deles, os efeitos marcados como novos pelo motor (sem repetir). */
fun featuredEntries(sorted: List<EffectCatalogEntry>): List<EffectCatalogEntry> {
    val byId = sorted.associateBy { it.typeId }
    val curated = FeaturedKeys.mapNotNull { byId[effectTypeId(it)] }
    val ids = curated.mapTo(HashSet()) { it.typeId }
    return curated + sorted.filter { it.isNew && it.typeId !in ids }
}

/**
 * RECOMENDADOS (Omega efeitos, navegador): o AUREA não tem configuração remota,
 * então a lista começa na curadoria fixa ([FeaturedKeys]); fica só o que esta
 * camada aceita ([pickableEffects]), saem os efeitos que a camada JÁ tem
 * ([applied], por `typeId`) e, se sobrar vaga, entram os novos do motor. No
 * máximo [max] (6, como a referência).
 */
fun recommendedEffects(
    catalog: List<EffectCatalogEntry>,
    layerKind: Int,
    applied: Collection<Int>,
    max: Int = 6,
    layerHasAudio: Boolean = true,
): List<EffectCatalogEntry> {
    val offered = pickableEffects(catalog, layerHasAudio, layerKind)
    val byId = offered.associateBy { it.typeId }
    val taken = applied.toHashSet()
    val curated = FeaturedKeys.mapNotNull { byId[effectTypeId(it)] }.filter { it.typeId !in taken }
    val ids = curated.mapTo(HashSet()) { it.typeId }
    val fresh = offered.filter { it.isNew && it.typeId !in ids && it.typeId !in taken }
    return (curated + fresh).distinctBy { it.typeId }.take(max.coerceAtLeast(0))
}

/** Recentes: uma página de até 8, como a referência. */
const val QUICK_PAGE_SIZE = 8

/**
 * Itens por página de FAVORITOS para a largura [widthDp] da tela: 2 linhas de
 * ⌊(L − 20) / 69,5⌋ itens (o item tem 62 dp + 7,5 de espaço). 8 em 360 dp.
 */
fun favoritesPerPage(widthDp: Float): Int = 2 * maxOf(1, kotlin.math.floor((widthDp - 20f) / 69.5f).toInt())

/** Colunas da grade de uma categoria: ⌊(L − 30) / 103⌋ (cartão de 88 + 15), no mínimo 1. */
fun categoryGridColumns(widthDp: Float): Int = maxOf(1, kotlin.math.floor((widthDp - 30f) / 103f).toInt())

/**
 * As páginas da faixa Recentes/Favoritos: a de Recentes (só se houver) e as de
 * Favoritos, [perPage] por página. Devolve (é a de Recentes?, itens). Os dois
 * vazios = nenhuma página (a faixa some).
 */
fun <T> quickPages(recents: List<T>, favorites: List<T>, perPage: Int): List<Pair<Boolean, List<T>>> {
    val pages = ArrayList<Pair<Boolean, List<T>>>()
    if (recents.isNotEmpty()) pages += true to recents.take(minOf(QUICK_PAGE_SIZE, perPage.coerceAtLeast(1)))
    favorites.chunked(perPage.coerceAtLeast(1)).forEach { pages += false to it }
    return pages
}

/** O código curto de uma variação (linha do detalhe, selo da miniatura): "01", "02"… */
fun presetCode(preset: Int): String = if (preset < 0) "" else String.format(java.util.Locale.ROOT, "%02d", preset + 1)

/** A ordem do item [index] numa página de 2 linhas que enche por COLUNA (como a referência): (coluna, linha). */
fun quickCell(index: Int): Pair<Int, Int> = (index / 2) to (index % 2)

/**
 * A grade de uma categoria em ordem ALFABÉTICA pelo nome no idioma do app
 * ([nameOf]), com o colador do [locale] (sem diferenciar caixa nem acento).
 */
fun <T> sortedByName(entries: List<T>, locale: java.util.Locale, nameOf: (T) -> String): List<T> {
    val collator = java.text.Collator.getInstance(locale).apply { strength = java.text.Collator.PRIMARY }
    return entries.sortedWith { a, b -> collator.compare(nameOf(a), nameOf(b)) }
}

/** O grupo antes e o depois de [group] na ordem da tela (nulo nas pontas). */
fun adjacentGroups(groups: List<EffectGroup>, group: EffectGroup): Pair<EffectGroup?, EffectGroup?> {
    val i = groups.indexOf(group)
    if (i < 0) return null to null
    return groups.getOrNull(i - 1) to groups.getOrNull(i + 1)
}

/**
 * O recorte da prévia de comparação que mostra SÓ o efeito: a prévia do motor
 * é "antes | depois" lado a lado (`preview_compare.frag`), então a metade da
 * direita é o resultado. Devolve (x, y, largura, altura) em pixels da imagem,
 * cortado para preencher [dstAspect] (largura/altura) sem esticar.
 */
fun effectOnlyCrop(width: Int, height: Int, dstAspect: Float): IntArray {
    val left = width / 2 + 1
    val halfW = (width - left).coerceAtLeast(1)
    val srcAspect = halfW.toFloat() / height.coerceAtLeast(1)
    return if (srcAspect > dstAspect) {
        val w = (height * dstAspect).toInt().coerceIn(1, halfW)
        intArrayOf(left + (halfW - w) / 2, 0, w, height)
    } else {
        val h = (halfW / dstAspect).toInt().coerceIn(1, height)
        intArrayOf(left, (height - h) / 2, halfW, h)
    }
}

// --- Navegar -----------------------------------------------------------------

/** A grade sem busca: tudo (categoria nula) ou só a categoria escolhida, na ordem do catálogo. */
fun browseEffects(sorted: List<EffectCatalogEntry>, category: String?): List<EffectCatalogEntry> =
    if (category == null) sorted else sorted.filter { it.category == category }

/** Os recentes que ainda estão no catálogo desta camada, na ordem de uso. */
fun recentEffects(recents: List<Int>, pickable: List<EffectCatalogEntry>): List<EffectCatalogEntry> {
    val byId = pickable.associateBy { it.typeId }
    return recents.distinct().mapNotNull { byId[it] }
}

/** Os favoritos, na ordem do catálogo (estável: favoritar não embaralha a linha). */
fun favoriteEffects(favorites: Set<Int>, sorted: List<EffectCatalogEntry>): List<EffectCatalogEntry> =
    sorted.filter { it.typeId in favorites }

// --- Preferências do aparelho (recentes e favoritos) --------------------------

/** O efeito vai para a frente dos recentes, sem repetir, no máximo [max]. */
fun pushRecent(recents: List<Int>, typeId: Int, max: Int): List<Int> =
    (listOf(typeId) + recents.filter { it != typeId }).take(max)

/** Marca ou desmarca o favorito. */
fun toggleFavorite(favorites: Set<Int>, typeId: Int): Set<Int> =
    if (typeId in favorites) favorites - typeId else favorites + typeId

/** Os recentes gravados ("12,-5,7"): lixo e repetição somem. */
fun parseRecents(stored: String?): List<Int> =
    stored.orEmpty().split(',').mapNotNull { it.trim().toIntOrNull() }.distinct()

fun formatRecents(recents: List<Int>): String = recents.joinToString(",")

// --- Omega efeitos (navegador): favoritos e recentes com VARIAÇÃO ------------

/**
 * Um favorito ou recente: o efeito e, se houver, a variação ([preset] = índice
 * em `EditorStore.effectPresets(typeId)`; −1 = o efeito base). Gravado como
 * [key]: "<typeId>" ou "<typeId>/<preset>", com o typeId SEM SINAL (o mesmo
 * texto no iOS e no `effectCardId`).
 */
data class EffectRef(val typeId: Int, val preset: Int = -1) {
    val isBase: Boolean get() = preset < 0
    val key: String get() = if (preset >= 0) "${Integer.toUnsignedString(typeId)}/$preset" else Integer.toUnsignedString(typeId)
}

/**
 * Lê uma referência gravada. Aceita a lista de inteiros antiga (com sinal,
 * "-5"), o typeId sem sinal ("4294967291") e "id/preset". Lixo = nulo.
 */
fun parseEffectRef(stored: String): EffectRef? {
    val text = stored.trim()
    if (text.isEmpty()) return null
    val slash = text.indexOf('/')
    val idPart = if (slash < 0) text else text.substring(0, slash)
    val id = idPart.trim().toLongOrNull()?.takeIf { it in Int.MIN_VALUE.toLong()..0xFFFF_FFFFL } ?: return null
    if (slash < 0) return EffectRef(id.toInt())
    val preset = text.substring(slash + 1).trim().toIntOrNull()?.takeIf { it >= 0 } ?: return null
    return EffectRef(id.toInt(), preset)
}

/** Os recentes gravados ("12,-5/1,7"): lixo e repetição somem; a ordem fica. */
fun parseEffectRefs(stored: String?): List<EffectRef> =
    stored.orEmpty().split(',').mapNotNull(::parseEffectRef).distinct()

/** Os favoritos gravados (conjunto de textos): migra os inteiros antigos como efeito base. */
fun parseEffectRefSet(stored: Collection<String>): Set<EffectRef> = stored.mapNotNullTo(LinkedHashSet(), ::parseEffectRef)

fun formatEffectRefs(refs: List<EffectRef>): String = refs.joinToString(",") { it.key }

/** O item vai para a frente dos recentes, sem repetir, no máximo [max]. */
fun pushRecentRef(recents: List<EffectRef>, ref: EffectRef, max: Int): List<EffectRef> =
    (listOf(ref) + recents.filter { it != ref }).take(max)

/** Marca ou desmarca o favorito (efeito base ou uma variação). */
fun toggleFavoriteRef(favorites: Set<EffectRef>, ref: EffectRef): Set<EffectRef> =
    if (ref in favorites) favorites - ref else favorites + ref

/** Um item de Recentes/Favoritos já achado no catálogo desta camada. */
data class QuickItem(val entry: EffectCatalogEntry, val preset: Int = -1) {
    val ref: EffectRef get() = EffectRef(entry.typeId, preset)
}

/**
 * Os recentes que ainda valem nesta camada, na ordem de uso: o efeito está no
 * catálogo oferecido e a variação ainda existe ([presetCount] do tipo).
 */
fun recentItems(
    recents: List<EffectRef>,
    pickable: List<EffectCatalogEntry>,
    presetCount: (Int) -> Int,
    max: Int = QUICK_PAGE_SIZE,
): List<QuickItem> {
    val byId = pickable.associateBy { it.typeId }
    return recents.distinct().mapNotNull { r ->
        val e = byId[r.typeId] ?: return@mapNotNull null
        if (r.preset >= presetCount(r.typeId)) null else QuickItem(e, r.preset)
    }.take(max)
}

/**
 * Os favoritos na ordem do catálogo (favoritar não embaralha a faixa): o efeito
 * base e depois as variações dele, pela ordem do preset.
 */
fun favoriteItems(
    favorites: Set<EffectRef>,
    sorted: List<EffectCatalogEntry>,
    presetCount: (Int) -> Int,
): List<QuickItem> {
    if (favorites.isEmpty()) return emptyList()
    val byType = favorites.groupBy { it.typeId }
    return sorted.flatMap { e ->
        val refs = byType[e.typeId] ?: return@flatMap emptyList()
        val count = presetCount(e.typeId)
        refs.map { it.preset }.filter { it < count }.distinct().sorted().map { QuickItem(e, it) }
    }
}

// --- Buscar ------------------------------------------------------------------

/**
 * O que a busca sabe de UM efeito, já normalizado: [names] são os nomes (o do
 * motor e o de cada idioma do app) — acertar neles põe o efeito na frente; [text]
 * é tudo (nomes, categoria em todos os idiomas, sinônimos, descrição).
 */
class EffectSearchDoc(val names: List<String>, val text: String)

fun effectSearchDoc(names: Collection<String>, extra: Collection<String>): EffectSearchDoc {
    val n = names.map(::normalizeSearch).filter { it.isNotEmpty() }.distinct()
    val all = (n + extra.map(::normalizeSearch)).filter { it.isNotEmpty() }.distinct()
    return EffectSearchDoc(n, all.joinToString(" "))
}

/**
 * A busca: todo termo tem de aparecer em algum lugar do efeito. A ordem é por
 * PONTOS e, no empate, a do catálogo. Pontos = os da busca do AUREA em 7
 * idiomas (4 nome igual · 3 nome começa · 2 palavra do nome começa · 1 todos os
 * termos no nome · 0 achado no resto) SOMADOS aos da referência
 * ([effectSearchScore], com [fields]: etiqueta ×7, nome ×3, inglês ×2,
 * descrição ×1). Sem [fields], vale só a primeira parte.
 */
fun searchEffects(
    sorted: List<EffectCatalogEntry>,
    docs: Map<Int, EffectSearchDoc>,
    query: String,
    fields: Map<Int, EffectScoreFields> = emptyMap(),
): List<EffectCatalogEntry> {
    val q = normalizeSearch(query).replace(Spaces, " ")
    if (q.isEmpty()) return emptyList()
    val terms = q.split(' ')
    return sorted.mapIndexedNotNull { order, e ->
        val doc = docs[e.typeId] ?: return@mapIndexedNotNull null
        if (!terms.all { doc.text.contains(it) }) return@mapIndexedNotNull null
        val rank = when {
            doc.names.any { it == q } -> 0
            doc.names.any { it.startsWith(q) } -> 1
            doc.names.any { n -> n.split(' ').any { it.startsWith(q) } } -> 2
            doc.names.any { n -> terms.all { n.contains(it) } } -> 3
            else -> 4
        }
        Triple((4 - rank) + effectSearchScore(fields[e.typeId], q), order, e)
    }.sortedWith(compareByDescending<Triple<Int, Int, EffectCatalogEntry>> { it.first }.thenBy { it.second }).map { it.third }
}

/**
 * O que a pontuação da referência olha em UM efeito, já sem acento e em
 * minúsculas: as etiquetas (rótulos no idioma do app), o nome mostrado, o nome
 * em inglês e a descrição.
 */
class EffectScoreFields(tags: Collection<String>, name: String, english: String, description: String) {
    val tags: List<String> = tags.map(::normalizeSearch).filter { it.isNotEmpty() }
    val name: String = normalizeSearch(name)
    val english: String = normalizeSearch(english)
    val description: String = normalizeSearch(description)
}

/** Força do acerto de [query] em [field] (os dois normalizados): igual 3, começa 2, contém 1, nada 0. */
fun searchMatch(field: String, query: String): Int = when {
    query.isEmpty() || field.isEmpty() -> 0
    field == query -> 3
    field.startsWith(query) -> 2
    field.contains(query) -> 1
    else -> 0
}

/** A pontuação da referência: etiqueta ×7, nome ×3, nome em inglês ×2, descrição ×1 (× a força do acerto). */
fun effectSearchScore(fields: EffectScoreFields?, query: String): Int {
    if (fields == null) return 0
    val q = normalizeSearch(query).replace(Spaces, " ")
    if (q.isEmpty()) return 0
    val tag = fields.tags.maxOfOrNull { searchMatch(it, q) } ?: 0
    return tag * 7 + searchMatch(fields.name, q) * 3 + searchMatch(fields.english, q) * 2 + searchMatch(fields.description, q)
}

/** Os efeitos que têm TODAS as etiquetas [chips] (fichas no campo), na ordem recebida. */
fun effectsWithTags(
    entries: List<EffectCatalogEntry>,
    chips: Collection<String>,
    tagsOf: (EffectCatalogEntry) -> Collection<String>,
): List<EffectCatalogEntry> =
    if (chips.isEmpty()) entries else entries.filter { e -> tagsOf(e).let { t -> chips.all { it in t } } }

/**
 * As etiquetas cujo rótulo ([labels]: id → rótulo no idioma do app) casa com o
 * que se digita: as que começam com o texto antes das que só o contêm. [exclude]
 * são as fichas já no campo.
 */
fun matchingTags(labels: Map<String, String>, query: String, exclude: Collection<String> = emptyList()): List<String> {
    val q = normalizeSearch(query).replace(Spaces, " ")
    if (q.isEmpty()) return emptyList()
    return labels.entries.mapNotNull { (id, label) ->
        if (id in exclude) return@mapNotNull null
        val strength = searchMatch(normalizeSearch(label), q)
        if (strength == 0) null else id to strength
    }.sortedByDescending { it.second }.map { it.first }
}

private val Spaces = Regex("\\s+")

/**
 * Sinônimos por CATEGORIA, em vários idiomas: quem não sabe em que categoria o
 * efeito mora ainda acha pelo que ele faz ("blur", "размытие", "buram"). A chave
 * é a categoria do motor normalizada; categoria nova sem entrada não perde nada
 * (continua achada pelo nome e pela descrição).
 */
fun categorySynonyms(category: String): String = CategorySynonyms[normalizeSearch(category)].orEmpty()

private val CategorySynonyms = mapOf(
    "distorcer" to "distort distortion deform deformar distorsion warp torcer entortar искажение деформация विकृति تشويه distorsi",
    "glitch" to "glitch falha defeito erro error digital сбой глитч ग्लिच خلل gangguan",
    "estilizar" to "stylize stylise estilo style artistico artistic estilizar стилизация стиль शैली أسلوب gaya",
    "cor" to "color colour correcao correction grading lut tom tono цвет रंग لون warna",
    "luz" to "light luz brilho glow shine flare свет свечение प्रकाश चमक ضوء توهج cahaya",
    "glow e luz" to "light luz brilho glow shine flare свет свечение प्रकाश चमक ضوء توهج cahaya",
    "desfoque" to "blur desfoque desenfoque borrar borrao embacar размытие धुंधला ضبابية تمويه buram kabur",
    "ruido" to "noise ruido grao grain granulado шум зерно शोर ضوضاء derau",
    "nitidez" to "sharpen sharp nitidez enfocar realce резкость तीक्ष्णता حدة ketajaman tajam",
    "tempo" to "time tempo tiempo velocidade eco echo время समय وقت waktu",
    "transicao" to "transition transicao transicion wipe cortina entrada saida переход संक्रमण انتقال transisi",
    "recorte" to "key keying chroma croma recorte mascara matte fundo verde green screen кеинг хромакей क्रोमा مفتاح potong",
    "gerar" to "generate generator gerar generar gerador render fill генерировать उत्पन्न توليد buat",
    "utilitario" to "utility tool ferramenta utilitario herramienta утилита उपयोगिता أداة utilitas",
    "controles de expressao" to "expression control controle slider expressao expresion выражение अभिव्यक्ति تعبير ekspresi",
    "pattern" to "pattern padrao padroes patron textura узор पैटर्न نمط pola",
    "audio" to "audio som sound sonido reverb eco echo delay voz звук ध्वनि ऑडियो صوت suara",
)
