package com.aurea.aurea.editor.panels

import com.aurea.aurea.engine.ParamType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import kotlin.math.abs

// =============================================================================
//  A CAMADA HUMANA DOS EFEITOS (Fase 7.2): nome, palavras de busca, rótulos,
//  unidade de exibição e quais parâmetros são PRINCIPAIS. O motor não publica
//  "principal/avançado" nem a chave do efeito na ponte — só o `typeId`, que é o
//  FNV-1a 32 da chave estável ("aurea.blur.gaussian"). A tabela é indexada pelo
//  mesmo hash calculado aqui, então trocar o nome exibido no motor não a quebra;
//  efeito fora da tabela cai na regra padrão (rótulo do motor, primeiros N).
// =============================================================================

/** FNV-1a 32 da chave — o mesmo `aurea::effect_type_id` (bits iguais ao `typeId` lido como Int). */
internal fun effectTypeId(key: String): Int {
    var h = 0x811C9DC5.toInt()
    for (b in key.toByteArray(Charsets.UTF_8)) {
        h = h xor (b.toInt() and 0xFF)
        h *= 16777619
    }
    return h
}

/**
 * Como UM parâmetro aparece: rótulo simples, fator de exibição (motor × [scale]
 * = o que a pessoa vê), sufixo ("%", "°", "px", "x") e casas FIXAS. Campos nulos
 * = regra padrão.
 */
internal class ParamHuman(
    val label: String? = null,
    val scale: Float? = null,
    val suffix: String? = null,
    val decimals: Int? = null,
)

/**
 * Um efeito do catálogo em linguagem de gente. [principal] = índices dos
 * parâmetros mostrados de cara, NA ORDEM da lista; o resto vai para "Avançado".
 * Nulo = regra padrão ([DEFAULT_PRINCIPAL]).
 */
internal class EffectHuman(
    val name: String? = null,
    val keywords: String = "",
    val principal: List<Int>? = null,
    val params: Map<Int, ParamHuman> = emptyMap(),
)

/** Regra padrão: até 6 parâmetros visíveis mostra tudo; acima, os 5 primeiros. */
private const val DEFAULT_PRINCIPAL = 5
private const val SHOW_ALL_UP_TO = 6

private val Percent0 = ParamHuman(scale = 100f, suffix = "%", decimals = 0)

private val MatrixRows = listOf("Vermelho", "Verde", "Azul")
private val MatrixCols = listOf("do vermelho", "do verde", "do azul", "extra")

private val Table: Map<Int, EffectHuman> = buildMap {
    fun put(key: String, e: EffectHuman) = put(effectTypeId(key), e)
    put(
        "aurea.transform",
        EffectHuman(
            keywords = "transform mover posicao escala girar rotacao opacidade",
            principal = listOf(1, 2, 3, 4),
            params = mapOf(
                0 to ParamHuman(label = "Pivô"),
                2 to ParamHuman(suffix = "%", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.color.exposure",
        EffectHuman(
            keywords = "exposure luz clarear escurecer",
            principal = listOf(0),
            params = mapOf(
                0 to ParamHuman(suffix = "", decimals = 2),
                1 to ParamHuman(label = "Compensação", scale = 100f, suffix = "%", decimals = 0),
                2 to ParamHuman(label = "Tons médios", decimals = 2),
            ),
        ),
    )
    put(
        "aurea.color.brightness_contrast",
        EffectHuman(keywords = "brightness contrast clarear", params = mapOf(0 to ParamHuman(decimals = 0), 1 to ParamHuman(decimals = 0))),
    )
    put(
        "aurea.color.saturation",
        EffectHuman(keywords = "saturation cor viva preto e branco desbotar", params = mapOf(0 to ParamHuman(decimals = 0))),
    )
    put(
        "aurea.color.tint",
        EffectHuman(
            keywords = "tint colorir duotone",
            params = mapOf(0 to ParamHuman(label = "Cor das sombras"), 1 to ParamHuman(label = "Cor das luzes")),
        ),
    )
    put(
        "aurea.color.matrix",
        EffectHuman(
            name = "Misturar canais",
            keywords = "matriz de cor channel mixer rgb canais",
            principal = listOf(0, 5, 10),
            params = (0 until 12).associateWith { i ->
                val row = i / 4
                val col = i % 4
                when {
                    col == 3 -> ParamHuman(label = "${MatrixRows[row]} ${MatrixCols[col]}", scale = 100f, suffix = "%", decimals = 0)
                    col == row -> ParamHuman(label = MatrixRows[row], suffix = "x", decimals = 2)
                    else -> ParamHuman(label = "${MatrixRows[row]} ${MatrixCols[col]}", suffix = "x", decimals = 2)
                }
            },
        ),
    )
    put(
        "aurea.color.levels",
        EffectHuman(
            keywords = "levels niveis preto branco",
            principal = listOf(0, 1, 2),
            params = mapOf(
                0 to ParamHuman(label = "Ponto preto", decimals = 0),
                1 to ParamHuman(label = "Ponto branco", decimals = 0),
                2 to ParamHuman(label = "Tons médios", decimals = 2),
                3 to ParamHuman(decimals = 0),
                4 to ParamHuman(decimals = 0),
            ),
        ),
    )
    put("aurea.color.curves", EffectHuman(keywords = "curves curva tons"))
    put(
        "aurea.blur.gaussian",
        EffectHuman(
            name = "Desfoque",
            keywords = "blur gaussian gaussiano borrar embacar",
            principal = listOf(0, 1),
            params = mapOf(
                0 to ParamHuman(label = "Intensidade"),
                1 to ParamHuman(label = "Direção"),
                2 to ParamHuman(label = "Esticar bordas"),
            ),
        ),
    )
    put("aurea.blur.sharpen", EffectHuman(keywords = "sharpen nitidez realcar detalhe"))
    put(
        "aurea.light.glow",
        EffectHuman(
            keywords = "glow brilho luz neon",
            principal = listOf(2, 1, 0, 3),
            params = mapOf(
                0 to ParamHuman(label = "Limite"),
                2 to ParamHuman(suffix = "x", decimals = 1),
            ),
        ),
    )
    put(
        "aurea.stylize.motion_tile",
        EffectHuman(
            name = "Mosaico",
            keywords = "motion tile azulejos repetir ladrilho",
            principal = listOf(1, 2, 5, 7),
            params = mapOf(
                0 to ParamHuman(label = "Centro"),
                1 to ParamHuman(label = "Largura"),
                2 to ParamHuman(label = "Altura"),
                3 to ParamHuman(label = "Largura total"),
                4 to ParamHuman(label = "Altura total"),
                5 to ParamHuman(label = "Espelhar"),
                6 to ParamHuman(label = "Esticar bordas"),
                7 to ParamHuman(label = "Deslocamento"),
                8 to ParamHuman(label = "Deslocar na horizontal"),
            ),
        ),
    )
    put(
        "aurea.key.luma",
        EffectHuman(
            name = "Recorte por brilho",
            keywords = "chave de luma luma key remover preto branco",
            params = mapOf(0 to ParamHuman(label = "Remover"), 1 to ParamHuman(label = "Limite")),
        ),
    )
    put(
        "aurea.key.chroma",
        EffectHuman(
            name = "Recorte por cor",
            keywords = "chave de croma chroma key fundo verde green screen remover cor",
            principal = listOf(0, 1, 2),
            params = mapOf(0 to ParamHuman(label = "Cor a remover"), 3 to ParamHuman(label = "Limpar contorno")),
        ),
    )
    put(
        "aurea.time.echo",
        EffectHuman(
            keywords = "echo eco rastro trail copias",
            principal = listOf(0, 1, 2),
            params = mapOf(
                1 to ParamHuman(label = "Intervalo", suffix = "quadros", decimals = 1),
                2 to ParamHuman(label = "Desvanecer"),
                3 to ParamHuman(label = "Separar cores", suffix = "quadros", decimals = 1),
            ),
        ),
    )
    put("aurea.control.slider", EffectHuman(keywords = "expressao slider controle", params = mapOf(0 to ParamHuman(decimals = 1))))
    put("aurea.control.angle", EffectHuman(keywords = "expressao angulo controle"))
    put("aurea.control.checkbox", EffectHuman(keywords = "expressao caixa controle"))
    put("aurea.control.color", EffectHuman(keywords = "expressao cor controle"))
    put("aurea.control.point", EffectHuman(keywords = "expressao ponto controle"))
}

internal fun effectHuman(typeId: Int): EffectHuman? = Table[typeId]

/** Nome exibido: o da tabela, senão o do motor. */
internal fun effectDisplayName(typeId: Int, engineName: String): String = Table[typeId]?.name ?: engineName

/** Texto onde a busca procura: nome humano, nome do motor, categoria e sinônimos. */
internal fun effectSearchText(typeId: Int, engineName: String, category: String): String =
    normalizeSearch(listOf(effectDisplayName(typeId, engineName), engineName, category, Table[typeId]?.keywords.orEmpty()).joinToString(" "))

/** Ícone da categoria (rótulo visual do cartão do navegador — não é prévia). */
internal fun categoryGlyph(category: String): Char = when (normalizeSearch(category)) {
    "cor" -> CupertinoGlyph.ColorFilter
    "desfoque" -> CupertinoGlyph.DropFill
    "luz", "glow e luz" -> CupertinoGlyph.Sparkles
    "estilizar" -> CupertinoGlyph.SquareGrid2x2
    "distorcer" -> CupertinoGlyph.Move
    "recorte" -> CupertinoGlyph.Scissors
    "tempo" -> CupertinoGlyph.Timer
    "controles de expressao" -> CupertinoGlyph.SliderHorizontal3
    else -> CupertinoGlyph.WandStars
}

/**
 * A exibição resolvida de um parâmetro. O motor guarda na unidade dele; a linha
 * mostra `motor × scale` com [suffix] e [decimals] fixos (a caixa não "dança").
 */
internal data class ParamDisplay(val label: String, val scale: Float, val suffix: String, val decimals: Int) {
    fun toDisplay(engine: Float) = engine * scale
    fun toEngine(display: Float) = if (scale != 0f) display / scale else display
}

/**
 * Regra padrão das unidades humanas:
 * - `%` do motor / flag Percent → "%" (o valor já está em %);
 * - flag Relative (fração da camada, 0..1) → ×100 "%";
 * - faixa 0..1 sem unidade → ×100 "%" (nunca "0,600");
 * - ângulo → "°"; pixels → "px"; inteiro → 0 casas;
 * - senão casas pela faixa: > 20 → 0, > 2 → 1, senão 2 (nunca 6 casas).
 */
internal fun paramDisplay(typeId: Int, s: ParamSlot): ParamDisplay {
    val h = Table[typeId]?.params?.get(s.index)
    val range = abs(s.max - s.min)
    val finite = range.isFinite()
    val unit = s.unit
    val percentFlag = (s.flags and ParamType.FLAG_PERCENT) != 0 || unit == "%"
    val relative = (s.flags and ParamType.FLAG_RELATIVE) != 0
    val pixels = (s.flags and ParamType.FLAG_PIXELS) != 0 || unit == "px"
    var scale = 1f
    var suffix = unit
    val decimals: Int
    when {
        s.type == ParamType.ANGLE -> { suffix = "°"; decimals = 0 }
        s.type == ParamType.INT -> decimals = 0
        relative -> { scale = 100f; suffix = "%"; decimals = 0 }
        percentFlag -> { suffix = "%"; decimals = if (!finite || range > 20f) 0 else 1 }
        pixels -> { suffix = "px"; decimals = if (!finite || range > 20f) 0 else 1 }
        unit.isEmpty() && finite && s.min >= 0f && s.max <= 1f -> { scale = 100f; suffix = "%"; decimals = 0 }
        else -> decimals = when {
            !finite || range > 20f -> 0
            range > 2f -> 1
            else -> 2
        }
    }
    return ParamDisplay(
        label = h?.label ?: s.label,
        scale = h?.scale ?: scale,
        suffix = h?.suffix ?: suffix,
        decimals = h?.decimals ?: decimals,
    )
}

/**
 * Os parâmetros visíveis de um efeito divididos em PRINCIPAIS (na ordem da
 * tabela) e AVANÇADOS (na ordem do motor).
 */
internal fun splitPrincipal(typeId: Int, visible: List<ParamSlot>): Pair<List<ParamSlot>, List<ParamSlot>> {
    val wanted = Table[typeId]?.principal
    if (wanted == null) {
        if (visible.size <= SHOW_ALL_UP_TO) return visible to emptyList()
        return visible.take(DEFAULT_PRINCIPAL) to visible.drop(DEFAULT_PRINCIPAL)
    }
    val byIndex = visible.associateBy { it.index }
    val main = wanted.mapNotNull { byIndex[it] }
    val mainSet = main.map { it.index }.toSet()
    // Se a tabela não bate com o motor (efeito mudou), nada some: o que sobrou é avançado.
    if (main.isEmpty()) return visible to emptyList()
    return main to visible.filter { it.index !in mainSet }
}
