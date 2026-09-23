package com.aurea.aurea.editor.panels

import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
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
    @androidx.annotation.StringRes val label: Int? = null,
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
    @androidx.annotation.StringRes val name: Int? = null,
    val keywords: String = "",
    val principal: List<Int>? = null,
    val params: Map<Int, ParamHuman> = emptyMap(),
)

/** Regra padrão: até 6 parâmetros visíveis mostra tudo; acima, os 5 primeiros. */
private const val DEFAULT_PRINCIPAL = 5
private const val SHOW_ALL_UP_TO = 6

private val Percent0 = ParamHuman(scale = 100f, suffix = "%", decimals = 0)

/**
 * As 12 células da matriz (linha = canal de saída, coluna = de onde vem). Uma
 * frase por célula, e não "linha + coluna": "Vermelho do verde" não se monta
 * por concatenação em russo nem em árabe.
 */
private val MatrixLabels = listOf(
    R.string.fx_matrix_rr, R.string.fx_matrix_rg, R.string.fx_matrix_rb, R.string.fx_matrix_ro,
    R.string.fx_matrix_gr, R.string.fx_matrix_gg, R.string.fx_matrix_gb, R.string.fx_matrix_go,
    R.string.fx_matrix_br, R.string.fx_matrix_bg, R.string.fx_matrix_bb, R.string.fx_matrix_bo,
)

private val Table: Map<Int, EffectHuman> = buildMap {
    fun put(key: String, e: EffectHuman) = put(effectTypeId(key), e)
    put(
        "aurea.transform",
        EffectHuman(
            name = R.string.fx_name_transform,
            keywords = "transform mover posicao escala girar rotacao opacidade",
            principal = listOf(1, 2, 3, 4),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_pivo),
                2 to ParamHuman(suffix = "%", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.color.exposure",
        EffectHuman(
            name = R.string.fx_name_exposure,
            keywords = "exposure luz clarear escurecer",
            principal = listOf(0),
            params = mapOf(
                0 to ParamHuman(suffix = "", decimals = 2),
                1 to ParamHuman(label = R.string.fx_compensacao, scale = 100f, suffix = "%", decimals = 0),
                2 to ParamHuman(label = R.string.fx_tons_medios, decimals = 2),
            ),
        ),
    )
    put(
        "aurea.color.brightness_contrast",
        EffectHuman(name = R.string.fx_name_brightness_contrast, keywords = "brightness contrast clarear", params = mapOf(0 to ParamHuman(decimals = 0), 1 to ParamHuman(decimals = 0))),
    )
    put(
        "aurea.color.saturation",
        EffectHuman(name = R.string.fx_name_saturation, keywords = "saturation cor viva preto e branco desbotar", params = mapOf(0 to ParamHuman(decimals = 0))),
    )
    put(
        "aurea.color.tint",
        EffectHuman(
            name = R.string.fx_name_tint,
            keywords = "tint colorir duotone",
            params = mapOf(0 to ParamHuman(label = R.string.fx_cor_sombras), 1 to ParamHuman(label = R.string.fx_cor_luzes)),
        ),
    )
    put(
        "aurea.color.matrix",
        EffectHuman(
            name = R.string.fx_name_color_matrix,
            keywords = "matriz de cor channel mixer rgb canais",
            principal = listOf(0, 5, 10),
            params = (0 until 12).associateWith { i ->
                // Coluna 3 é o deslocamento (%); as outras, o ganho (×).
                if (i % 4 == 3) ParamHuman(label = MatrixLabels[i], scale = 100f, suffix = "%", decimals = 0)
                else ParamHuman(label = MatrixLabels[i], suffix = "x", decimals = 2)
            },
        ),
    )
    put(
        "aurea.color.levels",
        EffectHuman(
            name = R.string.fx_name_levels,
            keywords = "levels niveis preto branco",
            principal = listOf(0, 1, 2),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_ponto_preto, decimals = 0),
                1 to ParamHuman(label = R.string.fx_ponto_branco, decimals = 0),
                2 to ParamHuman(label = R.string.fx_tons_medios, decimals = 2),
                3 to ParamHuman(decimals = 0),
                4 to ParamHuman(decimals = 0),
            ),
        ),
    )
    put("aurea.color.curves", EffectHuman(name = R.string.fx_name_curves, keywords = "curves curva tons"))
    put(
        "aurea.blur.gaussian",
        EffectHuman(
            name = R.string.fx_name_gaussian_blur,
            keywords = "blur gaussian gaussiano borrar embacar",
            principal = listOf(0, 1),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_intensidade),
                1 to ParamHuman(label = R.string.fx_direcao),
                2 to ParamHuman(label = R.string.fx_esticar_bordas),
            ),
        ),
    )
    put("aurea.blur.sharpen", EffectHuman(name = R.string.fx_name_sharpen, keywords = "sharpen nitidez realcar detalhe"))
    put(
        "aurea.light.glow",
        EffectHuman(
            name = R.string.fx_name_glow,
            keywords = "glow brilho luz neon",
            principal = listOf(2, 1, 0, 3),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_limite),
                2 to ParamHuman(suffix = "x", decimals = 1),
            ),
        ),
    )
    put(
        "aurea.stylize.motion_tile",
        EffectHuman(
            name = R.string.fx_name_motion_tile,
            keywords = "motion tile azulejos repetir ladrilho",
            principal = listOf(1, 2, 5, 7),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_centro),
                1 to ParamHuman(label = R.string.fx_largura),
                2 to ParamHuman(label = R.string.fx_altura),
                3 to ParamHuman(label = R.string.fx_largura_total),
                4 to ParamHuman(label = R.string.fx_altura_total),
                5 to ParamHuman(label = R.string.fx_espelhar),
                6 to ParamHuman(label = R.string.fx_esticar_bordas),
                7 to ParamHuman(label = R.string.fx_deslocamento),
                8 to ParamHuman(label = R.string.fx_deslocar_horizontal),
            ),
        ),
    )
    put(
        "aurea.key.luma",
        EffectHuman(
            name = R.string.fx_name_luma_key,
            keywords = "chave de luma luma key remover preto branco",
            params = mapOf(0 to ParamHuman(label = R.string.fx_remover), 1 to ParamHuman(label = R.string.fx_limite)),
        ),
    )
    put(
        "aurea.key.chroma",
        EffectHuman(
            name = R.string.fx_name_chroma_key,
            keywords = "chave de croma chroma key fundo verde green screen remover cor",
            principal = listOf(0, 1, 2),
            params = mapOf(0 to ParamHuman(label = R.string.fx_cor_remover), 3 to ParamHuman(label = R.string.fx_limpar_contorno)),
        ),
    )
    put(
        "aurea.time.echo",
        EffectHuman(
            name = R.string.fx_name_echo_trail,
            keywords = "echo eco rastro trail copias",
            principal = listOf(0, 1, 2),
            params = mapOf(
                1 to ParamHuman(label = R.string.fx_intervalo, suffix = "quadros", decimals = 1),
                2 to ParamHuman(label = R.string.fx_desvanecer),
                3 to ParamHuman(label = R.string.fx_separar_cores, suffix = "quadros", decimals = 1),
            ),
        ),
    )
    // --- Fase 7.3: o pacote novo ---------------------------------------------
    // Os parâmetros principais vêm primeiro (o resto vai para "Avançado"), e o
    // sufixo/casas são os que fazem o número fazer sentido na tela.
    put("aurea.color.invert", EffectHuman(name = R.string.fx_name_invert, keywords = "inverter negativo inverter cor"))
    put(
        "aurea.stylize.scanlines",
        EffectHuman(
            name = R.string.fx_name_scanlines,
            keywords = "scanline varredura crt tv tubo linha",
            principal = listOf(0, 1, 3, 4),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_altura_linha, suffix = "px", decimals = 1),
                3 to ParamHuman(label = R.string.fx_suavidade, decimals = 0),
                4 to ParamHuman(label = R.string.fx_contraste, decimals = 0),
                6 to ParamHuman(label = R.string.fx_canal),
                8 to ParamHuman(label = R.string.fx_rolagem, suffix = "px", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.stylize.grain",
        EffectHuman(
            name = R.string.fx_name_grain,
            keywords = "grain grao filme ruido textura analogico",
            principal = listOf(0, 1, 2, 4, 7),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_intensidade, decimals = 0),
                1 to ParamHuman(label = R.string.fx_tamanho_grao, suffix = "px", decimals = 1),
                2 to ParamHuman(label = R.string.fx_grao_cor, decimals = 0),
                3 to ParamHuman(label = R.string.fx_rugosidade, suffix = "x", decimals = 2),
                4 to ParamHuman(label = R.string.fx_sombras, decimals = 0),
                5 to ParamHuman(label = R.string.fx_luzes, decimals = 0),
                7 to ParamHuman(label = R.string.fx_animado),
                8 to ParamHuman(label = R.string.fx_monocromatico),
            ),
        ),
    )
    put(
        "aurea.stylize.halftone",
        EffectHuman(
            name = R.string.fx_name_halftone,
            keywords = "halftone meio tom reticula pontos impressao jornal pontilhado cmyk angulos",
            // Os principais são os do Color Halftone do AE: o raio máximo e os
            // quatro ângulos das retículas. O resto (passo, contraste, suavidade,
            // padrão…) fica em "Avançado".
            principal = listOf(12, 13, 14, 15, 16),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_passo_grade, suffix = "px", decimals = 0),
                1 to ParamHuman(label = R.string.fx_contraste, decimals = 0),
                2 to ParamHuman(label = R.string.fx_angulo, suffix = "°", decimals = 0),
                3 to ParamHuman(label = R.string.fx_suavidade, decimals = 0),
                4 to ParamHuman(label = R.string.fx_rotacao_canal, suffix = "°", decimals = 0),
                5 to ParamHuman(label = R.string.fx_padrao),
                6 to ParamHuman(label = R.string.fx_grades_separadas),
                7 to ParamHuman(label = R.string.fx_fundo_claro, decimals = 0),
                8 to ParamHuman(label = R.string.fx_ganho_ponto, decimals = 0),
                10 to ParamHuman(label = R.string.fx_centro_x, suffix = "px", decimals = 0),
                11 to ParamHuman(label = R.string.fx_centro_y, suffix = "px", decimals = 0),
                12 to ParamHuman(label = R.string.fx_raio_maximo, suffix = "px", decimals = 1),
                13 to ParamHuman(label = R.string.fx_angulo_canal_1, suffix = "°", decimals = 0),
                14 to ParamHuman(label = R.string.fx_angulo_canal_2, suffix = "°", decimals = 0),
                15 to ParamHuman(label = R.string.fx_angulo_canal_3, suffix = "°", decimals = 0),
                16 to ParamHuman(label = R.string.fx_angulo_canal_4, suffix = "°", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.stylize.minimax",
        EffectHuman(
            name = R.string.fx_name_minimax,
            keywords = "minimax dilatar erodir morfologia matte afinar engrossar",
            principal = listOf(1, 0, 4, 3),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_raio, suffix = "px", decimals = 0),
                1 to ParamHuman(label = R.string.fx_operacao),
                2 to ParamHuman(label = R.string.fx_forma),
                3 to ParamHuman(label = R.string.fx_intensidade, decimals = 0),
                4 to ParamHuman(label = R.string.fx_comparar),
            ),
        ),
    )
    put(
        "aurea.blur.unsharp",
        EffectHuman(
            name = R.string.fx_name_unsharp,
            keywords = "unsharp mascara de nitidez sharpen afiar detalhe",
            principal = listOf(0, 1, 2),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_intensidade, decimals = 0),
                1 to ParamHuman(label = R.string.fx_raio, suffix = "px", decimals = 1),
                2 to ParamHuman(label = R.string.fx_limiar, decimals = 0),
                4 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
            ),
        ),
    )
    put(
        "aurea.blur.lens",
        EffectHuman(
            name = R.string.fx_name_lens_blur,
            keywords = "lens blur desfoque de lente bokeh iris",
            principal = listOf(0, 1, 2, 4),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_raio, suffix = "px", decimals = 0),
                1 to ParamHuman(label = R.string.fx_ganho_luzes, decimals = 0),
                2 to ParamHuman(label = R.string.fx_lados_iris, decimals = 0),
                3 to ParamHuman(label = R.string.fx_rotacao_iris, suffix = "°", decimals = 0),
                4 to ParamHuman(label = R.string.fx_qualidade, decimals = 0),
                7 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
            ),
        ),
    )
    put(
        "aurea.distort.shake",
        EffectHuman(
            name = R.string.fx_name_shake,
            keywords = "shake tremor camera balancar vibrar tremer",
            principal = listOf(0, 1, 2, 4, 5),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_amplitude_x, suffix = "px", decimals = 0),
                1 to ParamHuman(label = R.string.fx_amplitude_y, suffix = "px", decimals = 0),
                2 to ParamHuman(label = R.string.fx_frequencia, suffix = "x", decimals = 2),
                4 to ParamHuman(label = R.string.fx_eixos_separados),
                5 to ParamHuman(label = R.string.fx_rotacao, suffix = "°", decimals = 0),
                6 to ParamHuman(label = R.string.fx_suavizacao, decimals = 0),
                7 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
            ),
        ),
    )
    put(
        "aurea.distort.turbulence",
        EffectHuman(
            name = R.string.fx_name_turbulence,
            keywords = "turbulencia displacement deslocamento ruido organico fumaca",
            principal = listOf(0, 1, 2, 4),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_intensidade, suffix = "px", decimals = 0),
                1 to ParamHuman(label = R.string.fx_tamanho_ruido, suffix = "px", decimals = 0),
                2 to ParamHuman(label = R.string.fx_complexidade, suffix = "oitavas", decimals = 0),
                3 to ParamHuman(label = R.string.fx_evolucao, suffix = "px/q", decimals = 1),
                4 to ParamHuman(label = R.string.fx_deslocamento_x, suffix = "px", decimals = 0),
                5 to ParamHuman(label = R.string.fx_deslocamento_y, suffix = "px", decimals = 0),
                8 to ParamHuman(label = R.string.fx_bordas),
                10 to ParamHuman(label = R.string.fx_girar_deslocamento, suffix = "°", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.distort.wave_warp",
        EffectHuman(
            name = R.string.fx_name_wave_warp,
            keywords = "wave warp onda ondular senoide agua",
            principal = listOf(0, 1, 2, 4),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_altura_onda, suffix = "px", decimals = 0),
                1 to ParamHuman(label = R.string.fx_largura_onda, suffix = "px", decimals = 0),
                2 to ParamHuman(label = R.string.fx_velocidade, suffix = "px/q", decimals = 0),
                3 to ParamHuman(label = R.string.fx_fase, suffix = "°", decimals = 0),
                4 to ParamHuman(label = R.string.fx_direcao),
                5 to ParamHuman(label = R.string.fx_onda_quadrada),
                6 to ParamHuman(label = R.string.fx_bordas),
                7 to ParamHuman(label = R.string.fx_travar_nas_bordas),
            ),
        ),
    )
    put(
        "aurea.distort.warp",
        EffectHuman(
            name = R.string.fx_name_warp,
            keywords = "warp lente distorcer empurrar puxar torcer esfera canto bulge pinch twist",
            principal = listOf(0, 1, 2, 3),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_modo),
                1 to ParamHuman(label = R.string.fx_intensidade, suffix = "px", decimals = 0),
                2 to ParamHuman(label = R.string.fx_raio, suffix = "px", decimals = 0),
                3 to ParamHuman(label = R.string.fx_centro),
                4 to ParamHuman(label = R.string.fx_bordas),
                5 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
                6 to ParamHuman(label = R.string.fx_luz_esfera, decimals = 0),
            ),
        ),
    )
    put(
        "aurea.distort.ripple_dissolve",
        EffectHuman(
            name = R.string.fx_name_ripple_dissolve,
            keywords = "ripple dissolve ondulacao dissolver transicao circular agua",
            principal = listOf(0, 1, 2, 3),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_progresso, decimals = 0),
                1 to ParamHuman(label = R.string.fx_ondulacao, suffix = "px", decimals = 0),
                2 to ParamHuman(label = R.string.fx_comprimento_onda, suffix = "px", decimals = 0),
                3 to ParamHuman(label = R.string.fx_suavidade_borda, decimals = 0),
                4 to ParamHuman(label = R.string.fx_centro),
                5 to ParamHuman(label = R.string.fx_velocidade_onda, suffix = "x", decimals = 2),
                7 to ParamHuman(label = R.string.fx_distorcer_imagem_junto),
                8 to ParamHuman(label = R.string.fx_fora_dentro),
            ),
        ),
    )
    put(
        "aurea.light.deep_glow",
        EffectHuman(
            name = R.string.fx_name_deep_glow,
            keywords = "deep glow brilho profundo halo neon luz bloom",
            principal = listOf(0, 1, 2, 3, 4),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_limite, decimals = 0),
                1 to ParamHuman(label = R.string.fx_raio_nucleo, suffix = "px", decimals = 0),
                2 to ParamHuman(label = R.string.fx_raio_halo, suffix = "px", decimals = 0),
                3 to ParamHuman(label = R.string.fx_forca_nucleo, suffix = "x", decimals = 2),
                4 to ParamHuman(label = R.string.fx_forca_halo, suffix = "x", decimals = 2),
                5 to ParamHuman(label = R.string.fx_cor_brilho),
                6 to ParamHuman(label = R.string.fx_preservar_sombras),
                7 to ParamHuman(label = R.string.fx_halo_tela),
                10 to ParamHuman(label = R.string.fx_so_brilho),
                11 to ParamHuman(label = R.string.fx_estouro, decimals = 0),
            ),
        ),
    )
    put(
        "aurea.light.rays",
        EffectHuman(
            name = R.string.fx_name_rays,
            keywords = "rays raios de luz god rays sol volumetrico spread",
            principal = listOf(0, 1, 2, 3, 4),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_intensidade, suffix = "x", decimals = 2),
                1 to ParamHuman(label = R.string.fx_comprimento, decimals = 0),
                2 to ParamHuman(label = R.string.fx_limite, decimals = 0),
                3 to ParamHuman(label = R.string.fx_decaimento, decimals = 0),
                4 to ParamHuman(label = R.string.fx_ponto_luz),
                5 to ParamHuman(label = R.string.fx_amostras, decimals = 0),
                7 to ParamHuman(label = R.string.fx_guardar_cor_fonte),
                8 to ParamHuman(label = R.string.fx_girar_cor, suffix = "°", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.light.sweep",
        EffectHuman(
            name = R.string.fx_name_light_sweep,
            keywords = "light sweep faixa de luz brilho varredura reflexo",
            principal = listOf(0, 1, 2, 4, 5),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_posicao, decimals = 0),
                1 to ParamHuman(label = R.string.fx_largura, decimals = 0),
                2 to ParamHuman(label = R.string.fx_intensidade, suffix = "x", decimals = 2),
                3 to ParamHuman(label = R.string.fx_suavidade_borda, decimals = 0),
                4 to ParamHuman(label = R.string.fx_angulo, suffix = "°", decimals = 0),
                5 to ParamHuman(label = R.string.fx_relevo, decimals = 0),
                6 to ParamHuman(label = R.string.fx_multiplicar),
                7 to ParamHuman(label = R.string.fx_so_onde_imagem_clara),
            ),
        ),
    )
    put(
        "aurea.color.colorama",
        EffectHuman(
            name = R.string.fx_name_colorama,
            keywords = "colorama remapeamento de cor arco-iris psicodelico mapa de cor",
            principal = listOf(0, 1, 2, 4, 5),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_fase, suffix = "voltas", decimals = 2),
                1 to ParamHuman(label = R.string.fx_ciclos, suffix = "x", decimals = 2),
                2 to ParamHuman(label = R.string.fx_saturacao, decimals = 0),
                3 to ParamHuman(label = R.string.fx_brilho, decimals = 0),
                4 to ParamHuman(label = R.string.fx_entrada),
                5 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
                6 to ParamHuman(label = R.string.fx_inverter_arco_iris),
                7 to ParamHuman(label = R.string.fx_peso_croma, decimals = 0),
                9 to ParamHuman(label = R.string.fx_ganho, suffix = "x", decimals = 2),
            ),
        ),
    )
    put(
        "aurea.stylize.pixel_sort",
        EffectHuman(
            name = R.string.fx_name_pixel_sort,
            keywords = "pixel sort ordenar pixels derreter listras glitch sort",
            principal = listOf(0, 1, 2, 4),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_limiar_baixo, decimals = 0),
                1 to ParamHuman(label = R.string.fx_limiar_alto, decimals = 0),
                2 to ParamHuman(label = R.string.fx_comprimento, decimals = 0),
                3 to ParamHuman(label = R.string.fx_aleatoriedade, decimals = 0),
                4 to ParamHuman(label = R.string.fx_direcao),
                5 to ParamHuman(label = R.string.fx_sentido_inverso),
                6 to ParamHuman(label = R.string.fx_ordenar),
                8 to ParamHuman(label = R.string.fx_faixa_tom),
                9 to ParamHuman(label = R.string.fx_passo, suffix = "px", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.stylize.film_damage",
        EffectHuman(
            name = R.string.fx_name_film_damage,
            keywords = "film damage dano de filme poeira riscos arranhao projetor pelicula",
            principal = listOf(0, 1, 2, 3, 4),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_poeira, decimals = 0),
                1 to ParamHuman(label = R.string.fx_riscos, decimals = 0),
                2 to ParamHuman(label = R.string.fx_piscar, decimals = 0),
                3 to ParamHuman(label = R.string.fx_balanco_porta, suffix = "px", decimals = 1),
                4 to ParamHuman(label = R.string.fx_queimado, decimals = 0),
                5 to ParamHuman(label = R.string.fx_emenda),
                7 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
                8 to ParamHuman(label = R.string.fx_tamanho_poeira, suffix = "px", decimals = 1),
                9 to ParamHuman(label = R.string.fx_comprimento_risco, decimals = 0),
                11 to ParamHuman(label = R.string.fx_calor_queimado, decimals = 0),
            ),
        ),
    )
    put(
        "aurea.stylize.jpeg_damage",
        EffectHuman(
            name = R.string.fx_name_jpeg_damage,
            keywords = "jpeg damage dano compressao artefato bloco qualidade",
            principal = listOf(0, 1, 2, 3),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_qualidade, decimals = 0),
                1 to ParamHuman(label = R.string.fx_blocos, decimals = 0),
                2 to ParamHuman(label = R.string.fx_anelamento, decimals = 0),
                3 to ParamHuman(label = R.string.fx_dano_cor, decimals = 0),
                4 to ParamHuman(label = R.string.fx_tamanho_bloco, suffix = "px", decimals = 0),
                5 to ParamHuman(label = R.string.fx_suavizar_bloco, decimals = 0),
                7 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
                8 to ParamHuman(label = R.string.fx_blocos_corrompidos, decimals = 0),
            ),
        ),
    )
    put(
        "aurea.stylize.holomatrix",
        EffectHuman(
            name = R.string.fx_name_holo_matrix,
            keywords = "holo matrix holograma projecao grade tecnologica scanner",
            principal = listOf(0, 1, 3, 4, 6),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_mistura_cor, decimals = 0),
                1 to ParamHuman(label = R.string.fx_grade, decimals = 0),
                2 to ParamHuman(label = R.string.fx_celulas_grade, decimals = 0),
                3 to ParamHuman(label = R.string.fx_brilho_bordas, decimals = 0),
                4 to ParamHuman(label = R.string.fx_posicao_varredura, decimals = 0),
                5 to ParamHuman(label = R.string.fx_largura_varredura, decimals = 0),
                6 to ParamHuman(label = R.string.fx_interferencia, decimals = 0),
                7 to ParamHuman(label = R.string.fx_velocidade_varredura, suffix = "x", decimals = 2),
                9 to ParamHuman(label = R.string.fx_fundo_aceso, decimals = 0),
                11 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
            ),
        ),
    )
    put(
        "aurea.glitch.glitchify",
        EffectHuman(
            name = R.string.fx_name_glitchify,
            keywords = "glitchify glitch defeito digital rasgo bloco corrupcao",
            principal = listOf(0, 1, 2, 3, 4),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_altura_faixa, suffix = "px", decimals = 0),
                1 to ParamHuman(label = R.string.fx_deslocamento, suffix = "px", decimals = 0),
                2 to ParamHuman(label = R.string.fx_picos, decimals = 0),
                3 to ParamHuman(label = R.string.fx_separacao_rgb, suffix = "px", decimals = 0),
                4 to ParamHuman(label = R.string.fx_frequencia, suffix = "quadros", decimals = 1),
                6 to ParamHuman(label = R.string.fx_travar_quadro),
                7 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
                8 to ParamHuman(label = R.string.fx_blocos_verticais),
                9 to ParamHuman(label = R.string.fx_corrupcao_cor, decimals = 0),
            ),
        ),
    )
    put(
        "aurea.glitch.vhs",
        EffectHuman(
            name = R.string.fx_name_vhs,
            keywords = "vhs fita cassette videocassete tracking dropouts analogico videotape",
            principal = listOf(0, 1, 2, 3, 4, 5, 6),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_borrado_luma, suffix = "px", decimals = 1),
                1 to ParamHuman(label = R.string.fx_alargar_cor, suffix = "px", decimals = 1),
                2 to ParamHuman(label = R.string.fx_instabilidade, suffix = "px", decimals = 0),
                3 to ParamHuman(label = R.string.fx_perdas_fita, decimals = 0),
                4 to ParamHuman(label = R.string.fx_varredura_cabecote, decimals = 0),
                5 to ParamHuman(label = R.string.fx_ruido, decimals = 0),
                6 to ParamHuman(label = R.string.fx_degradacao_cor, decimals = 0),
                7 to ParamHuman(label = R.string.fx_sangramento, decimals = 0),
                10 to ParamHuman(label = R.string.fx_velocidade_instabilidade, suffix = "x", decimals = 2),
                11 to ParamHuman(label = R.string.fx_altura_perda, suffix = "px", decimals = 1),
                12 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
            ),
        ),
    )
    put(
        "aurea.glitch.uni_vhs",
        EffectHuman(
            name = R.string.fx_name_uni_vhs,
            keywords = "vhs fita estilizado anos 80 retro neon chroma warp",
            principal = listOf(0, 1, 2, 3, 6),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_separacao_rgb, suffix = "px", decimals = 0),
                1 to ParamHuman(label = R.string.fx_ondulacao, suffix = "px", decimals = 0),
                2 to ParamHuman(label = R.string.fx_brilho_sujo, decimals = 0),
                3 to ParamHuman(label = R.string.fx_vinheta, decimals = 0),
                4 to ParamHuman(label = R.string.fx_varredura, decimals = 0),
                5 to ParamHuman(label = R.string.fx_ruido, decimals = 0),
                6 to ParamHuman(label = R.string.fx_saturacao, decimals = 0),
                7 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
                9 to ParamHuman(label = R.string.fx_frequencia_ondulacao, suffix = "x", decimals = 1),
            ),
        ),
    )
    put(
        "aurea.glitch.signal",
        EffectHuman(
            name = R.string.fx_name_signal,
            keywords = "signal sinal interferencia transmissao banda sincronia chiado",
            principal = listOf(0, 1, 2, 3, 4),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_bandas_perdidas, decimals = 0),
                1 to ParamHuman(label = R.string.fx_deslocamento, suffix = "px", decimals = 0),
                2 to ParamHuman(label = R.string.fx_deriva, suffix = "px/q", decimals = 2),
                3 to ParamHuman(label = R.string.fx_altura_banda, suffix = "px", decimals = 0),
                4 to ParamHuman(label = R.string.fx_ruido_sinal, decimals = 0),
                6 to ParamHuman(label = R.string.fx_frequencia, suffix = "quadros", decimals = 1),
                7 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
                8 to ParamHuman(label = R.string.fx_perder_sincronia),
                9 to ParamHuman(label = R.string.fx_separacao_cor, suffix = "px", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.glitch.cross",
        EffectHuman(
            name = R.string.fx_name_cross_glitch,
            keywords = "cross glitch cruz transicao varredura rasgo",
            principal = listOf(0, 1, 2, 4),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_progresso, decimals = 0),
                1 to ParamHuman(label = R.string.fx_largura_faixa, decimals = 0),
                2 to ParamHuman(label = R.string.fx_deslocamento, suffix = "px", decimals = 0),
                3 to ParamHuman(label = R.string.fx_ruido_fundo, decimals = 0),
                4 to ParamHuman(label = R.string.fx_separacao_rgb, suffix = "px", decimals = 0),
                6 to ParamHuman(label = R.string.fx_frequencia, suffix = "quadros", decimals = 1),
                7 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
                8 to ParamHuman(label = R.string.fx_faixa_vertical),
                9 to ParamHuman(label = R.string.fx_faixa_horizontal),
            ),
        ),
    )
    put(
        "aurea.time.posterize",
        EffectHuman(
            name = R.string.fx_name_posterize_time,
            keywords = "posterize time posterizar tempo taxa quadros stop motion animacao",
            principal = listOf(0, 1),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_quadros_segundo, suffix = "fps", decimals = 1),
                1 to ParamHuman(label = R.string.fx_segurar_quadro),
            ),
        ),
    )
    put(
        "aurea.time.warp_rgb",
        EffectHuman(
            name = R.string.fx_name_time_warp_rgb,
            keywords = "rgb no tempo time warp separar canais atrasar cor chromatic",
            principal = listOf(0, 1, 2, 3, 4),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_vermelho_c031, suffix = "quadros", decimals = 1),
                1 to ParamHuman(label = R.string.fx_verde_14e6, suffix = "quadros", decimals = 1),
                2 to ParamHuman(label = R.string.fx_azul_582d, suffix = "quadros", decimals = 1),
                3 to ParamHuman(label = R.string.fx_unidade),
                4 to ParamHuman(label = R.string.fx_intensidade, decimals = 0),
                5 to ParamHuman(label = R.string.fx_prender_nas_pontas),
            ),
        ),
    )
    put("aurea.control.slider", EffectHuman(name = R.string.fx_name_slider_control, keywords = "expressao slider controle", params = mapOf(0 to ParamHuman(decimals = 1))))
    put("aurea.control.angle", EffectHuman(name = R.string.fx_name_angle_control, keywords = "expressao angulo controle"))
    put("aurea.control.checkbox", EffectHuman(name = R.string.fx_name_checkbox_control, keywords = "expressao caixa controle"))
    put("aurea.control.color", EffectHuman(name = R.string.fx_name_color_control, keywords = "expressao cor controle"))
    put("aurea.control.point", EffectHuman(name = R.string.fx_name_point_control, keywords = "expressao ponto controle"))
}

/**
 * Nome exibido: o da tabela (traduzido), senão o do motor.
 *
 * @Composable porque o nome da tabela é recurso: a busca indexa o nome NO
 * IDIOMA do app, então quem monta o índice também precisa disto aqui dentro.
 */
@Composable
internal fun effectDisplayName(typeId: Int, engineName: String): String =
    Table[typeId]?.name?.let { stringResource(it) } ?: engineName

/** Texto onde a busca procura: nome humano, nome do motor, categoria e sinônimos. */
@Composable
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
 * A posição do efeito na ordem em que a TABELA o declara (o `buildMap` guarda a
 * ordem de inserção). Serve para ordenar a lista sem depender do idioma: pelo
 * nome traduzido, a lista se reordenaria ao trocar de língua.
 */
private val effectTableOrder: Map<Int, Int> by lazy { Table.keys.withIndex().associate { (i, k) -> k to i } }

/** Ordem declarada do efeito; fora da tabela vai para o fim. */
internal fun effectNameRank(typeId: Int): Int = effectTableOrder[typeId] ?: Int.MAX_VALUE

/**
 * A exibição resolvida de um parâmetro. O motor guarda na unidade dele; a linha
 * mostra `motor × scale` com [suffix] e [decimals] fixos (a caixa não "dança").
 */
internal data class ParamDisplay(
    /** Rótulo da tabela humana, quando existe (o do motor é o reserva). */
    @androidx.annotation.StringRes val labelRes: Int?,
    /** Rótulo que o MOTOR publica — nome técnico, não traduzido. */
    val engineLabel: String,
    val scale: Float,
    val suffix: String,
    val decimals: Int,
) {
    fun toDisplay(engine: Float) = engine * scale
    fun toEngine(display: Float) = if (scale != 0f) display / scale else display
}

/** O rótulo da linha, no idioma do app. */
@Composable
internal fun ParamDisplay.label(): String = labelRes?.let { stringResource(it) } ?: engineLabel

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
        labelRes = h?.label,
        engineLabel = s.label,
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
