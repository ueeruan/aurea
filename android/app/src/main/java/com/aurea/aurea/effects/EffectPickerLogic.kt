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

/** As duas abas do painel de efeitos. */
enum class EffectsTab { Applied, Add }

/** Camada com efeito abre na pilha; camada sem efeito abre direto no catálogo. */
fun initialEffectsTab(effectCount: Int): EffectsTab = if (effectCount > 0) EffectsTab.Applied else EffectsTab.Add

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
fun pickableEffects(catalog: List<EffectCatalogEntry>, layerHasAudio: Boolean): List<EffectCatalogEntry> =
    catalog.filter { it.typeId !in setOf(effectTypeId("aurea.motion.oscillate"), effectTypeId("aurea.layout.grid_builder"), effectTypeId("aurea.layout.grid_item"), effectTypeId("aurea.light.scene_flare")) && (layerHasAudio || !isAudioCategory(it.category) || makesSound(it)) }

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
 * Os GRUPOS (os ladrilhos de categoria da tela "Adicionar efeito"). O motor
 * publica categorias em português e em inglês, misturadas ("Cor" e "Color",
 * "Distorcer" e "Distort"); a tela junta tudo em grupos de gente, na ordem
 * abaixo. [id] é o testTag/identificador (`effects.category.<id>`), igual no iOS.
 */
enum class EffectGroup(val id: String) {
    ColorLight("color_light"),
    Blur("blur"),
    Distort("distort"),
    Motion("motion"),
    Stylize("stylize"),
    Glitch("glitch"),
    DrawEdge("draw_edge"),
    Procedural("procedural"),
    Matte("matte"),
    Time("time"),
    Text("text"),
    ThreeD("3d"),
    Audio("audio"),
    Utility("utility"),
    Other("other"),
}

/** Categoria do motor (normalizada) → grupo. Categoria nova sem entrada cai em "Outros". */
private val GroupByCategory: Map<String, EffectGroup> = buildMap {
    listOf("cor", "color", "colour", "luz", "light", "glow e luz").forEach { put(it, EffectGroup.ColorLight) }
    listOf("desfoque", "blur", "nitidez", "sharpen").forEach { put(it, EffectGroup.Blur) }
    listOf("distorcer", "distort", "distorcao").forEach { put(it, EffectGroup.Distort) }
    listOf("transform", "transformar", "movimento", "motion", "rastreio", "tracking").forEach { put(it, EffectGroup.Motion) }
    listOf("estilizar", "stylize", "stylise").forEach { put(it, EffectGroup.Stylize) }
    put("glitch", EffectGroup.Glitch)
    listOf("gerar", "generate", "pattern", "ruido", "noise").forEach { put(it, EffectGroup.Procedural) }
    listOf("recorte", "keying", "key", "mascara", "matte").forEach { put(it, EffectGroup.Matte) }
    listOf("tempo", "time", "transicao", "transition").forEach { put(it, EffectGroup.Time) }
    listOf("texto", "text").forEach { put(it, EffectGroup.Text) }
    put("3d", EffectGroup.ThreeD)
    listOf("audio", "som", "sound").forEach { put(it, EffectGroup.Audio) }
    listOf("utilitario", "utility", "controles de expressao", "expression controls").forEach { put(it, EffectGroup.Utility) }
}

/** Efeitos que moram num grupo diferente do da categoria do motor (o que eles FAZEM manda). */
private val GroupByKey: Map<Int, EffectGroup> by lazy {
    buildMap {
        listOf(
            "aurea.stylize.stroke_outline", "aurea.stylize.border", "aurea.stylize.drop_shadow",
            "aurea.stylize.find_edges", "aurea.stylize.bevel_alpha",
        ).forEach { put(effectTypeId(it), EffectGroup.DrawEdge) }
        listOf(
            "aurea.transform", "aurea.motion.oscillate.cycles", "aurea.motion.swing", "aurea.motion.wiggle",
            "aurea.motion.twitch", "aurea.distort.shake", "aurea.distort.corner_pin", "aurea.transform.parenting_helper",
        ).forEach { put(effectTypeId(it), EffectGroup.Motion) }
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

/** O efeito cuja prévia (escurecida) vira o fundo do ladrilho do grupo. */
private val GroupBannerKeys: Map<EffectGroup, String> = mapOf(
    EffectGroup.ColorLight to "aurea.color.colorama",
    EffectGroup.Blur to "aurea.blur.radial",
    EffectGroup.Distort to "aurea.distort.wave_warp",
    EffectGroup.Motion to "aurea.distort.corner_pin",
    EffectGroup.Stylize to "aurea.stylize.halftone",
    EffectGroup.Glitch to "aurea.glitch.glitchify",
    EffectGroup.DrawEdge to "aurea.stylize.find_edges",
    EffectGroup.Procedural to "aurea.generate.fractal_noise",
    EffectGroup.Matte to "aurea.key.chroma",
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
    EffectTool.Captions.key, "aurea.light.deep_glow", "aurea.glitch.vhs", "aurea.stylize.halftone",
    "aurea.distort.wave_warp", "aurea.light.rays", "aurea.color.colorama", "aurea.blur.radial",
    "aurea.stylize.pixel_sort", "aurea.generate.fractal_noise",
)

fun featuredEntries(sorted: List<EffectCatalogEntry>): List<EffectCatalogEntry> {
    val byId = sorted.associateBy { it.typeId }
    return FeaturedKeys.mapNotNull { byId[effectTypeId(it)] }
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
 * relevância e, no empate, a do catálogo:
 *  0 nome igual à busca · 1 nome que começa com ela · 2 uma palavra do nome
 *  começa com ela · 3 todos os termos no nome · 4 achado no resto (sinônimo,
 *  categoria, descrição).
 */
fun searchEffects(
    sorted: List<EffectCatalogEntry>,
    docs: Map<Int, EffectSearchDoc>,
    query: String,
): List<EffectCatalogEntry> {
    val q = normalizeSearch(query).replace(Spaces, " ")
    if (q.isEmpty()) return emptyList()
    val terms = q.split(' ')
    return sorted.mapNotNull { e ->
        val doc = docs[e.typeId] ?: return@mapNotNull null
        if (!terms.all { doc.text.contains(it) }) return@mapNotNull null
        val rank = when {
            doc.names.any { it == q } -> 0
            doc.names.any { it.startsWith(q) } -> 1
            doc.names.any { n -> n.split(' ').any { it.startsWith(q) } } -> 2
            doc.names.any { n -> terms.all { n.contains(it) } } -> 3
            else -> 4
        }
        rank to e
    }.sortedBy { it.first }.map { it.second }
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
