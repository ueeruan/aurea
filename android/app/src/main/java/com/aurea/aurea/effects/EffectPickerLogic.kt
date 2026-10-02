package com.aurea.aurea.effects

import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.editor.panels.normalizeSearch
import com.aurea.aurea.engine.EffectCatalogEntry

// =============================================================================
//  O ESCOLHEDOR DE EFEITOS — a LÓGICA pura (sem Compose, testada na JVM).
//
//  O painel "Efeitos" tem duas abas: "Na camada" (a pilha) e "Adicionar" (o
//  catálogo). Tudo aqui é montado a partir do CATÁLOGO que o motor publica —
//  nenhuma lista fixa de efeitos: efeito ou categoria novos aparecem sozinhos.
//
//  Regras:
//   · abre em "Na camada" se a camada já tem efeito; senão direto em "Adicionar";
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
