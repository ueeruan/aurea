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
    // --- Fase 7.3: o pacote novo ---------------------------------------------
    // Os parâmetros principais vêm primeiro (o resto vai para "Avançado"), e o
    // sufixo/casas são os que fazem o número fazer sentido na tela.
    put("aurea.color.invert", EffectHuman(keywords = "inverter negativo inverter cor"))
    put(
        "aurea.stylize.scanlines",
        EffectHuman(
            keywords = "scanline varredura crt tv tubo linha",
            principal = listOf(0, 1, 3, 4),
            params = mapOf(
                0 to ParamHuman(label = "Altura da linha", suffix = "px", decimals = 1),
                3 to ParamHuman(label = "Suavidade", decimals = 0),
                4 to ParamHuman(label = "Contraste", decimals = 0),
                6 to ParamHuman(label = "Canal"),
                8 to ParamHuman(label = "Rolagem", suffix = "px", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.stylize.grain",
        EffectHuman(
            keywords = "grain grao filme ruido textura analogico",
            principal = listOf(0, 1, 2, 4, 7),
            params = mapOf(
                0 to ParamHuman(label = "Intensidade", decimals = 0),
                1 to ParamHuman(label = "Tamanho do grão", suffix = "px", decimals = 1),
                2 to ParamHuman(label = "Grão de cor", decimals = 0),
                3 to ParamHuman(label = "Rugosidade", suffix = "x", decimals = 2),
                4 to ParamHuman(label = "Sombras", decimals = 0),
                5 to ParamHuman(label = "Luzes", decimals = 0),
                7 to ParamHuman(label = "Animado"),
                8 to ParamHuman(label = "Monocromático"),
            ),
        ),
    )
    put(
        "aurea.stylize.halftone",
        EffectHuman(
            keywords = "halftone meio tom reticula pontos impressao jornal pontilhado",
            principal = listOf(0, 1, 2, 5, 7),
            params = mapOf(
                0 to ParamHuman(label = "Tamanho do ponto", suffix = "px", decimals = 0),
                1 to ParamHuman(label = "Contraste", decimals = 0),
                2 to ParamHuman(label = "Ângulo", suffix = "°", decimals = 0),
                3 to ParamHuman(label = "Suavidade", decimals = 0),
                4 to ParamHuman(label = "Rotação por canal", suffix = "°", decimals = 0),
                5 to ParamHuman(label = "Padrão"),
                6 to ParamHuman(label = "Grades separadas"),
                7 to ParamHuman(label = "Fundo claro", decimals = 0),
                8 to ParamHuman(label = "Ganho do ponto", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.stylize.minimax",
        EffectHuman(
            keywords = "minimax dilatar erodir morfologia matte afinar engrossar",
            principal = listOf(1, 0, 4, 3),
            params = mapOf(
                0 to ParamHuman(label = "Raio", suffix = "px", decimals = 0),
                1 to ParamHuman(label = "Operação"),
                2 to ParamHuman(label = "Forma"),
                3 to ParamHuman(label = "Intensidade", decimals = 0),
                4 to ParamHuman(label = "Comparar por"),
            ),
        ),
    )
    put(
        "aurea.blur.unsharp",
        EffectHuman(
            keywords = "unsharp mascara de nitidez sharpen afiar detalhe",
            principal = listOf(0, 1, 2),
            params = mapOf(
                0 to ParamHuman(label = "Intensidade", decimals = 0),
                1 to ParamHuman(label = "Raio", suffix = "px", decimals = 1),
                2 to ParamHuman(label = "Limiar", decimals = 0),
                4 to ParamHuman(label = "Mistura", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.blur.lens",
        EffectHuman(
            keywords = "lens blur desfoque de lente bokeh iris",
            principal = listOf(0, 1, 2, 4),
            params = mapOf(
                0 to ParamHuman(label = "Raio", suffix = "px", decimals = 0),
                1 to ParamHuman(label = "Ganho das luzes", decimals = 0),
                2 to ParamHuman(label = "Lados da íris", decimals = 0),
                3 to ParamHuman(label = "Rotação da íris", suffix = "°", decimals = 0),
                4 to ParamHuman(label = "Qualidade", decimals = 0),
                7 to ParamHuman(label = "Mistura", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.distort.shake",
        EffectHuman(
            keywords = "shake tremor camera balancar vibrar tremer",
            principal = listOf(0, 1, 2, 4, 5),
            params = mapOf(
                0 to ParamHuman(label = "Amplitude X", suffix = "px", decimals = 0),
                1 to ParamHuman(label = "Amplitude Y", suffix = "px", decimals = 0),
                2 to ParamHuman(label = "Frequência", suffix = "x", decimals = 2),
                4 to ParamHuman(label = "Eixos separados"),
                5 to ParamHuman(label = "Rotação", suffix = "°", decimals = 0),
                6 to ParamHuman(label = "Suavização", decimals = 0),
                7 to ParamHuman(label = "Mistura", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.distort.turbulence",
        EffectHuman(
            keywords = "turbulencia displacement deslocamento ruido organico fumaca",
            principal = listOf(0, 1, 2, 4),
            params = mapOf(
                0 to ParamHuman(label = "Intensidade", suffix = "px", decimals = 0),
                1 to ParamHuman(label = "Tamanho do ruído", suffix = "px", decimals = 0),
                2 to ParamHuman(label = "Complexidade", suffix = "oitavas", decimals = 0),
                3 to ParamHuman(label = "Evolução", suffix = "px/q", decimals = 1),
                4 to ParamHuman(label = "Deslocamento X", suffix = "px", decimals = 0),
                5 to ParamHuman(label = "Deslocamento Y", suffix = "px", decimals = 0),
                8 to ParamHuman(label = "Bordas"),
                10 to ParamHuman(label = "Girar o deslocamento", suffix = "°", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.distort.wave_warp",
        EffectHuman(
            keywords = "wave warp onda ondular senoide agua",
            principal = listOf(0, 1, 2, 4),
            params = mapOf(
                0 to ParamHuman(label = "Altura da onda", suffix = "px", decimals = 0),
                1 to ParamHuman(label = "Largura de onda", suffix = "px", decimals = 0),
                2 to ParamHuman(label = "Velocidade", suffix = "px/q", decimals = 0),
                3 to ParamHuman(label = "Fase", suffix = "°", decimals = 0),
                4 to ParamHuman(label = "Direção"),
                5 to ParamHuman(label = "Onda quadrada"),
                6 to ParamHuman(label = "Bordas"),
                7 to ParamHuman(label = "Travar nas bordas"),
            ),
        ),
    )
    put(
        "aurea.distort.warp",
        EffectHuman(
            keywords = "warp lente distorcer empurrar puxar torcer esfera canto bulge pinch twist",
            principal = listOf(0, 1, 2, 3),
            params = mapOf(
                0 to ParamHuman(label = "Modo"),
                1 to ParamHuman(label = "Intensidade", suffix = "px", decimals = 0),
                2 to ParamHuman(label = "Raio", suffix = "px", decimals = 0),
                3 to ParamHuman(label = "Centro"),
                4 to ParamHuman(label = "Bordas"),
                5 to ParamHuman(label = "Mistura", decimals = 0),
                6 to ParamHuman(label = "Luz da esfera", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.distort.ripple_dissolve",
        EffectHuman(
            keywords = "ripple dissolve ondulacao dissolver transicao circular agua",
            principal = listOf(0, 1, 2, 3),
            params = mapOf(
                0 to ParamHuman(label = "Progresso", decimals = 0),
                1 to ParamHuman(label = "Ondulação", suffix = "px", decimals = 0),
                2 to ParamHuman(label = "Comprimento da onda", suffix = "px", decimals = 0),
                3 to ParamHuman(label = "Suavidade da borda", decimals = 0),
                4 to ParamHuman(label = "Centro"),
                5 to ParamHuman(label = "Velocidade da onda", suffix = "x", decimals = 2),
                7 to ParamHuman(label = "Distorcer a imagem junto"),
                8 to ParamHuman(label = "De fora para dentro"),
            ),
        ),
    )
    put(
        "aurea.light.deep_glow",
        EffectHuman(
            keywords = "deep glow brilho profundo halo neon luz bloom",
            principal = listOf(0, 1, 2, 3, 4),
            params = mapOf(
                0 to ParamHuman(label = "Limite", decimals = 0),
                1 to ParamHuman(label = "Raio do núcleo", suffix = "px", decimals = 0),
                2 to ParamHuman(label = "Raio do halo", suffix = "px", decimals = 0),
                3 to ParamHuman(label = "Força do núcleo", suffix = "x", decimals = 2),
                4 to ParamHuman(label = "Força do halo", suffix = "x", decimals = 2),
                5 to ParamHuman(label = "Cor do brilho"),
                6 to ParamHuman(label = "Preservar as sombras"),
                7 to ParamHuman(label = "Halo em tela"),
                10 to ParamHuman(label = "Só o brilho"),
                11 to ParamHuman(label = "Estouro", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.light.rays",
        EffectHuman(
            keywords = "rays raios de luz god rays sol volumetrico spread",
            principal = listOf(0, 1, 2, 3, 4),
            params = mapOf(
                0 to ParamHuman(label = "Intensidade", suffix = "x", decimals = 2),
                1 to ParamHuman(label = "Comprimento", decimals = 0),
                2 to ParamHuman(label = "Limite", decimals = 0),
                3 to ParamHuman(label = "Decaimento", decimals = 0),
                4 to ParamHuman(label = "Ponto de luz"),
                5 to ParamHuman(label = "Amostras", decimals = 0),
                7 to ParamHuman(label = "Guardar a cor da fonte"),
                8 to ParamHuman(label = "Girar a cor", suffix = "°", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.light.sweep",
        EffectHuman(
            keywords = "light sweep faixa de luz brilho varredura reflexo",
            principal = listOf(0, 1, 2, 4, 5),
            params = mapOf(
                0 to ParamHuman(label = "Posição", decimals = 0),
                1 to ParamHuman(label = "Largura", decimals = 0),
                2 to ParamHuman(label = "Intensidade", suffix = "x", decimals = 2),
                3 to ParamHuman(label = "Suavidade da borda", decimals = 0),
                4 to ParamHuman(label = "Ângulo", suffix = "°", decimals = 0),
                5 to ParamHuman(label = "Relevo", decimals = 0),
                6 to ParamHuman(label = "Multiplicar"),
                7 to ParamHuman(label = "Só onde a imagem é clara"),
            ),
        ),
    )
    put(
        "aurea.color.colorama",
        EffectHuman(
            keywords = "colorama remapeamento de cor arco-iris psicodelico mapa de cor",
            principal = listOf(0, 1, 2, 4, 5),
            params = mapOf(
                0 to ParamHuman(label = "Fase", suffix = "voltas", decimals = 2),
                1 to ParamHuman(label = "Ciclos", suffix = "x", decimals = 2),
                2 to ParamHuman(label = "Saturação", decimals = 0),
                3 to ParamHuman(label = "Brilho", decimals = 0),
                4 to ParamHuman(label = "Entrada"),
                5 to ParamHuman(label = "Mistura", decimals = 0),
                6 to ParamHuman(label = "Inverter o arco-íris"),
                7 to ParamHuman(label = "Peso do croma", decimals = 0),
                9 to ParamHuman(label = "Ganho", suffix = "x", decimals = 2),
            ),
        ),
    )
    put(
        "aurea.stylize.pixel_sort",
        EffectHuman(
            keywords = "pixel sort ordenar pixels derreter listras glitch sort",
            principal = listOf(0, 1, 2, 4),
            params = mapOf(
                0 to ParamHuman(label = "Limiar baixo", decimals = 0),
                1 to ParamHuman(label = "Limiar alto", decimals = 0),
                2 to ParamHuman(label = "Comprimento", decimals = 0),
                3 to ParamHuman(label = "Aleatoriedade", decimals = 0),
                4 to ParamHuman(label = "Direção"),
                5 to ParamHuman(label = "Sentido inverso"),
                6 to ParamHuman(label = "Ordenar por"),
                8 to ParamHuman(label = "Por faixa de tom"),
                9 to ParamHuman(label = "Passo", suffix = "px", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.stylize.film_damage",
        EffectHuman(
            keywords = "film damage dano de filme poeira riscos arranhao projetor pelicula",
            principal = listOf(0, 1, 2, 3, 4),
            params = mapOf(
                0 to ParamHuman(label = "Poeira", decimals = 0),
                1 to ParamHuman(label = "Riscos", decimals = 0),
                2 to ParamHuman(label = "Piscar", decimals = 0),
                3 to ParamHuman(label = "Balanço de porta", suffix = "px", decimals = 1),
                4 to ParamHuman(label = "Queimado", decimals = 0),
                5 to ParamHuman(label = "Emenda"),
                7 to ParamHuman(label = "Mistura", decimals = 0),
                8 to ParamHuman(label = "Tamanho da poeira", suffix = "px", decimals = 1),
                9 to ParamHuman(label = "Comprimento do risco", decimals = 0),
                11 to ParamHuman(label = "Calor do queimado", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.stylize.jpeg_damage",
        EffectHuman(
            keywords = "jpeg damage dano compressao artefato bloco qualidade",
            principal = listOf(0, 1, 2, 3),
            params = mapOf(
                0 to ParamHuman(label = "Qualidade", decimals = 0),
                1 to ParamHuman(label = "Blocos", decimals = 0),
                2 to ParamHuman(label = "Anelamento", decimals = 0),
                3 to ParamHuman(label = "Dano de cor", decimals = 0),
                4 to ParamHuman(label = "Tamanho do bloco", suffix = "px", decimals = 0),
                5 to ParamHuman(label = "Suavizar o bloco", decimals = 0),
                7 to ParamHuman(label = "Mistura", decimals = 0),
                8 to ParamHuman(label = "Blocos corrompidos", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.stylize.holomatrix",
        EffectHuman(
            keywords = "holo matrix holograma projecao grade tecnologica scanner",
            principal = listOf(0, 1, 3, 4, 6),
            params = mapOf(
                0 to ParamHuman(label = "Mistura da cor", decimals = 0),
                1 to ParamHuman(label = "Grade", decimals = 0),
                2 to ParamHuman(label = "Células da grade", decimals = 0),
                3 to ParamHuman(label = "Brilho das bordas", decimals = 0),
                4 to ParamHuman(label = "Posição da varredura", decimals = 0),
                5 to ParamHuman(label = "Largura da varredura", decimals = 0),
                6 to ParamHuman(label = "Interferência", decimals = 0),
                7 to ParamHuman(label = "Velocidade da varredura", suffix = "x", decimals = 2),
                9 to ParamHuman(label = "Fundo aceso", decimals = 0),
                11 to ParamHuman(label = "Mistura", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.glitch.glitchify",
        EffectHuman(
            keywords = "glitchify glitch defeito digital rasgo bloco corrupcao",
            principal = listOf(0, 1, 2, 3, 4),
            params = mapOf(
                0 to ParamHuman(label = "Altura da faixa", suffix = "px", decimals = 0),
                1 to ParamHuman(label = "Deslocamento", suffix = "px", decimals = 0),
                2 to ParamHuman(label = "Picos", decimals = 0),
                3 to ParamHuman(label = "Separação RGB", suffix = "px", decimals = 0),
                4 to ParamHuman(label = "Frequência", suffix = "quadros", decimals = 1),
                6 to ParamHuman(label = "Travar o quadro"),
                7 to ParamHuman(label = "Mistura", decimals = 0),
                8 to ParamHuman(label = "Blocos verticais"),
                9 to ParamHuman(label = "Corrupção de cor", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.glitch.vhs",
        EffectHuman(
            keywords = "vhs fita cassette videocassete tracking dropouts analogico videotape",
            principal = listOf(0, 1, 2, 3, 4, 5, 6),
            params = mapOf(
                0 to ParamHuman(label = "Borrado da luma", suffix = "px", decimals = 1),
                1 to ParamHuman(label = "Alargar a cor", suffix = "px", decimals = 1),
                2 to ParamHuman(label = "Instabilidade", suffix = "px", decimals = 0),
                3 to ParamHuman(label = "Perdas de fita", decimals = 0),
                4 to ParamHuman(label = "Varredura de cabeçote", decimals = 0),
                5 to ParamHuman(label = "Ruído", decimals = 0),
                6 to ParamHuman(label = "Degradação de cor", decimals = 0),
                7 to ParamHuman(label = "Sangramento", decimals = 0),
                10 to ParamHuman(label = "Velocidade da instabilidade", suffix = "x", decimals = 2),
                11 to ParamHuman(label = "Altura da perda", suffix = "px", decimals = 1),
                12 to ParamHuman(label = "Mistura", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.glitch.uni_vhs",
        EffectHuman(
            keywords = "vhs fita estilizado anos 80 retro neon chroma warp",
            principal = listOf(0, 1, 2, 3, 6),
            params = mapOf(
                0 to ParamHuman(label = "Separação RGB", suffix = "px", decimals = 0),
                1 to ParamHuman(label = "Ondulação", suffix = "px", decimals = 0),
                2 to ParamHuman(label = "Brilho sujo", decimals = 0),
                3 to ParamHuman(label = "Vinheta", decimals = 0),
                4 to ParamHuman(label = "Varredura", decimals = 0),
                5 to ParamHuman(label = "Ruído", decimals = 0),
                6 to ParamHuman(label = "Saturação", decimals = 0),
                7 to ParamHuman(label = "Mistura", decimals = 0),
                9 to ParamHuman(label = "Frequência da ondulação", suffix = "x", decimals = 1),
            ),
        ),
    )
    put(
        "aurea.glitch.signal",
        EffectHuman(
            keywords = "signal sinal interferencia transmissao banda sincronia chiado",
            principal = listOf(0, 1, 2, 3, 4),
            params = mapOf(
                0 to ParamHuman(label = "Bandas perdidas", decimals = 0),
                1 to ParamHuman(label = "Deslocamento", suffix = "px", decimals = 0),
                2 to ParamHuman(label = "Deriva", suffix = "px/q", decimals = 2),
                3 to ParamHuman(label = "Altura da banda", suffix = "px", decimals = 0),
                4 to ParamHuman(label = "Ruído de sinal", decimals = 0),
                6 to ParamHuman(label = "Frequência", suffix = "quadros", decimals = 1),
                7 to ParamHuman(label = "Mistura", decimals = 0),
                8 to ParamHuman(label = "Perder a sincronia"),
                9 to ParamHuman(label = "Separação de cor", suffix = "px", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.glitch.cross",
        EffectHuman(
            keywords = "cross glitch cruz transicao varredura rasgo",
            principal = listOf(0, 1, 2, 4),
            params = mapOf(
                0 to ParamHuman(label = "Progresso", decimals = 0),
                1 to ParamHuman(label = "Largura da faixa", decimals = 0),
                2 to ParamHuman(label = "Deslocamento", suffix = "px", decimals = 0),
                3 to ParamHuman(label = "Ruído de fundo", decimals = 0),
                4 to ParamHuman(label = "Separação RGB", suffix = "px", decimals = 0),
                6 to ParamHuman(label = "Frequência", suffix = "quadros", decimals = 1),
                7 to ParamHuman(label = "Mistura", decimals = 0),
                8 to ParamHuman(label = "Faixa vertical"),
                9 to ParamHuman(label = "Faixa horizontal"),
            ),
        ),
    )
    put(
        "aurea.time.posterize",
        EffectHuman(
            keywords = "posterize time posterizar tempo taxa quadros stop motion animacao",
            principal = listOf(0, 1),
            params = mapOf(
                0 to ParamHuman(label = "Quadros por segundo", suffix = "fps", decimals = 1),
                1 to ParamHuman(label = "Segurar o quadro"),
            ),
        ),
    )
    put(
        "aurea.time.warp_rgb",
        EffectHuman(
            keywords = "rgb no tempo time warp separar canais atrasar cor chromatic",
            principal = listOf(0, 1, 2, 3, 4),
            params = mapOf(
                0 to ParamHuman(label = "Vermelho", suffix = "quadros", decimals = 1),
                1 to ParamHuman(label = "Verde", suffix = "quadros", decimals = 1),
                2 to ParamHuman(label = "Azul", suffix = "quadros", decimals = 1),
                3 to ParamHuman(label = "Unidade"),
                4 to ParamHuman(label = "Intensidade", decimals = 0),
                5 to ParamHuman(label = "Prender nas pontas"),
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
