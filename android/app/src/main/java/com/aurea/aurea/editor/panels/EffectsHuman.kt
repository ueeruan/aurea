package com.aurea.aurea.editor.panels

import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalContext
import android.content.res.Configuration
import java.util.Locale
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
    put("aurea.glitch.jpeg_codec", EffectHuman(keywords = "jpeg glitch compression compressao dct quantization dano",
        principal = listOf(0, 1, 2, 4, 5)))
    put("aurea.glitch.analog_signal", EffectHuman(keywords = "signal analog analogico ntsc pal vhs television",
        principal = listOf(0, 1, 2, 3, 4)))
    put("aurea.light.deep_glow_2", EffectHuman(keywords = "deep glow 2 brilho bloom halo",
        principal = listOf(0, 1, 2, 3, 13)))
    put("aurea.light.shadow_studio_3", EffectHuman(keywords = "shadow studio 3 sombra long radial inner",
        principal = listOf(0, 1, 2, 3, 4)))
    put("aurea.generate.tracery", EffectHuman(keywords = "tracery color detection boxes rastreio cor conexoes",
        principal = listOf(0, 1, 3, 7, 14), params = mapOf(2 to ParamHuman(decimals = 2))))
    put("aurea.text.transform", EffectHuman(name = R.string.text_transform_name,
        keywords = "text transform texto transformar letras palavras linhas intervalo fase",
        principal = (0..17).toList()))
    put("aurea.text.animator", EffectHuman(name = R.string.text_animator_name,
        keywords = "text animator animador texto letras palavras linhas cor blur desfoque random aleatorio",
        principal = (0..23).toList()))
    put("aurea.stylize.bevel_alpha", EffectHuman(name = R.string.bevel_alpha_name,
        keywords = "bevel alpha bisel alfa relevo texto borda", principal = (0..4).toList()))
    put("aurea.color.gradient_map", EffectHuman(name = R.string.gradient_map_name,
        keywords = "gradient map mapa degrade gradiente texto cor", principal = (0..4).toList()))
    put("aurea.text3d.layout", EffectHuman(keywords = "letter rotation letras rotacao delay atraso cylinder twist random aleatorio",
        principal = listOf(0, 1, 2, 12, 13, 9, 10)))
    // Formas 3D: o mesmo layout, parte a parte (mesmos índices do Text 3D Layout).
    put("aurea.shape3d.layout", EffectHuman(keywords = "shape parts partes forma 3d rotation rotacao delay atraso spread espalhar explodir twist random aleatorio",
        principal = listOf(0, 1, 2, 12, 13, 9, 4)))
    put("aurea.stylize.omino_diffusion", EffectHuman(
        name = R.string.fx_name_omino_diffusion, keywords = "omino omine diffusion difusao glitch paleta faixas",
        principal = listOf(0, 1, 2, 5, 6), params = mapOf(
            0 to ParamHuman(label = R.string.fx_intensidade, decimals = 0),
            1 to ParamHuman(label = R.string.fx_diffusion_weight, decimals = 2),
            2 to ParamHuman(label = R.string.fx_angulo, decimals = 0),
            3 to ParamHuman(label = R.string.fx_diffusion_reach, decimals = 0),
            4 to ParamHuman(label = R.string.fx_amostras, decimals = 0),
            5 to ParamHuman(label = R.string.fx_diffusion_stripes, decimals = 1),
            6 to ParamHuman(label = R.string.fx_diffusion_palette, decimals = 0),
            7 to ParamHuman(label = R.string.fx_diffusion_falloff, decimals = 0),
        )))
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
            keywords = "exposure luz clarear escurecer gama gamma",
            principal = listOf(0),
            params = mapOf(
                0 to ParamHuman(suffix = "", decimals = 2),
                1 to ParamHuman(label = R.string.fx_compensacao, scale = 100f, suffix = "%", decimals = 0),
                2 to ParamHuman(label = R.string.fx_tons_medios, decimals = 2),
                3 to ParamHuman(label = R.string.fx_aplicar_em),
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
            keywords = "motion tile azulejos repetir ladrilho mosaico tijolo fase espelhar parede de video",
            principal = listOf(10, 1, 2, 5, 7),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_centro),
                1 to ParamHuman(label = R.string.fx_mt_largura_ladrilho),
                2 to ParamHuman(label = R.string.fx_mt_altura_ladrilho),
                3 to ParamHuman(label = R.string.fx_mt_largura_saida),
                4 to ParamHuman(label = R.string.fx_mt_altura_saida),
                5 to ParamHuman(label = R.string.fx_mt_espelhar_bordas),
                7 to ParamHuman(label = R.string.fx_fase),
                8 to ParamHuman(label = R.string.fx_mt_fase_horizontal),
                10 to ParamHuman(label = R.string.fx_escala, suffix = "%", decimals = 1),
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
            name = R.string.fx_name_chroma_key_advanced,
            keywords = "chave de croma chroma key fundo verde green screen remover cor",
            principal = listOf(0, 1, 2),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_cor_remover),
                3 to ParamHuman(label = R.string.fx_limpar_contorno),
                9 to ParamHuman(label = R.string.fx_pre_desfoque, suffix = "px", decimals = 1),
            ),
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
                8 to ParamHuman(label = R.string.fx_curvatura_iris, decimals = 0),
                9 to ParamHuman(label = R.string.fx_escala_x, decimals = 0),
                10 to ParamHuman(label = R.string.fx_escala_y, decimals = 0),
            ),
        ),
    )
    put(
        "aurea.distort.shake",
        EffectHuman(
            name = R.string.fx_name_shake,
            keywords = "shake tremor camera balancar vibrar tremer",
            principal = listOf(8, 2, 10, 5, 9, 11),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_amplitude_x, suffix = "px", decimals = 0),
                1 to ParamHuman(label = R.string.fx_amplitude_y, suffix = "px", decimals = 0),
                2 to ParamHuman(label = R.string.fx_frequencia, suffix = "Hz", decimals = 2),
                4 to ParamHuman(label = R.string.fx_eixos_separados),
                5 to ParamHuman(label = R.string.fx_rotacao, suffix = "°", decimals = 0),
                6 to ParamHuman(label = R.string.fx_suavizacao, decimals = 0),
                7 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
                15 to ParamHuman(label = R.string.fx_direcao),
                16 to ParamHuman(label = R.string.fx_decaimento, suffix = "1/s", decimals = 2),
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
                3 to ParamHuman(label = R.string.fx_evolucao, suffix = "°", decimals = 1),
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
            principal = listOf(2, 13, 0, 14, 4),
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
            principal = listOf(9, 1, 2, 4, 5, 10),
            params = mapOf(
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
            principal = listOf(17, 18, 1, 3, 13, 34),
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
    put(
        "aurea.time.motion_detect",
        EffectHuman(
            name = R.string.fx_name_time_motion_detect,
            keywords = "motion detect detectar movimento diferenca quadro anterior mudou mexeu",
            principal = listOf(0, 1, 2, 3, 4, 5),
            params = mapOf(
                0 to ParamHuman(label = R.string.core2_fx_motion_delay, decimals = 0),
                1 to ParamHuman(label = R.string.fx_brilho, decimals = 2),
                2 to ParamHuman(label = R.string.core2_fx_offset_darks, decimals = 2),
                3 to ParamHuman(label = R.string.fx_saturacao, decimals = 2),
                4 to ParamHuman(label = R.string.core2_fx_motion_mode),
                5 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
            ),
        ),
    )
    put(
        "aurea.stylize.crt_emulator",
        EffectHuman(
            name = R.string.fx_name_crt_emulator,
            keywords = "crt emulator emulador tv tubo televisao retro scanline varredura fosforo curvatura tela antiga",
            principal = listOf(0, 1, 3, 4, 8, 11, 16),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx3_curvatura, decimals = 0),
                1 to ParamHuman(label = R.string.fx3_linhas_intensidade, decimals = 0),
                2 to ParamHuman(label = R.string.fx3_linhas_densidade, decimals = 0),
                3 to ParamHuman(label = R.string.fx3_mascara),
                4 to ParamHuman(label = R.string.fx3_mascara_intensidade, decimals = 0),
                5 to ParamHuman(label = R.string.fx3_mascara_tamanho, suffix = "px", decimals = 1),
                6 to ParamHuman(label = R.string.fx_vinheta, decimals = 0),
                7 to ParamHuman(label = R.string.fx3_convergencia, suffix = "px", decimals = 1),
                8 to ParamHuman(label = R.string.fx3_brilho_bloom, decimals = 0),
                9 to ParamHuman(label = R.string.fx3_raio_brilho, suffix = "px", decimals = 1),
                10 to ParamHuman(label = R.string.fx3_cintilacao, decimals = 0),
                11 to ParamHuman(label = R.string.fx3_faixa_rolando, decimals = 0),
                12 to ParamHuman(label = R.string.fx3_velocidade_faixa, decimals = 2),
                13 to ParamHuman(label = R.string.fx_ruido, decimals = 0),
                14 to ParamHuman(label = R.string.fx3_brilho_geral, decimals = 0),
                15 to ParamHuman(label = R.string.fx_contraste, decimals = 0),
                16 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
            ),
        ),
    )
    put(
        "aurea.glitch.dissolve_shake",
        EffectHuman(
            name = R.string.fx_name_dissolve_shake,
            keywords = "dissolve shake tremor dissolvente tremer quebrar fragmentos glitch desintegrar ruido",
            principal = listOf(0, 1, 2, 3, 4, 10),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_amplitude, suffix = "px", decimals = 0),
                1 to ParamHuman(label = R.string.fx_frequencia, suffix = "Hz", decimals = 1),
                2 to ParamHuman(label = R.string.fx3_dissolucao, decimals = 0),
                3 to ParamHuman(label = R.string.fx3_tamanho_fragmento, suffix = "px", decimals = 0),
                4 to ParamHuman(label = R.string.fx_dispersao, suffix = "px", decimals = 0),
                5 to ParamHuman(label = R.string.fx_aleatoriedade, decimals = 0),
                6 to ParamHuman(label = R.string.fx3_transparencia_fragmentos, decimals = 0),
                7 to ParamHuman(label = R.string.fx3_velocidade_evolucao, decimals = 1),
                8 to ParamHuman(label = R.string.fx3_eixos),
                9 to ParamHuman(label = R.string.fx_semente, decimals = 0),
                10 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
            ),
        ),
    )
    put(
        "aurea.glitch.datamosh",
        EffectHuman(
            name = R.string.fx_name_datamosh,
            keywords = "datamosh data mosh glitch compressao codec blocos macrobloco arrastar derreter i-frame p-frame pixel bleed",
            principal = listOf(0, 1, 2, 3, 4, 5, 6),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_intensidade, decimals = 0),
                1 to ParamHuman(label = R.string.fx_tamanho_bloco, suffix = "px", decimals = 0),
                2 to ParamHuman(label = R.string.fx_datamosh_hold_frames, decimals = 0),
                3 to ParamHuman(label = R.string.fx_datamosh_drag, decimals = 2),
                4 to ParamHuman(label = R.string.fx_datamosh_corruption, decimals = 0),
                5 to ParamHuman(label = R.string.fx_datamosh_color_bleed, decimals = 0),
                6 to ParamHuman(label = R.string.fx_semente, decimals = 0),
            ),
        ),
    )
    put(
        "aurea.distort.displacement_map",
        EffectHuman(
            name = R.string.fx_name_displacement_map,
            keywords = "displacement map mapa de deslocamento deslocar camada mapa distorcer canal",
            principal = listOf(0, 1, 2, 3, 4, 8),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx3_camada_mapa),
                1 to ParamHuman(label = R.string.fx3_canal_horizontal),
                2 to ParamHuman(label = R.string.fx3_canal_vertical),
                3 to ParamHuman(label = R.string.fx3_desloc_h_max, suffix = "px", decimals = 1),
                4 to ParamHuman(label = R.string.fx3_desloc_v_max, suffix = "px", decimals = 1),
                5 to ParamHuman(label = R.string.fx3_comportamento_mapa),
                6 to ParamHuman(label = R.string.fx_bordas),
                7 to ParamHuman(label = R.string.fx3_expandir_saida),
                8 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
            ),
        ),
    )
    put(
        "aurea.time.remap",
        EffectHuman(
            name = R.string.fx_name_time_remap,
            // O parâmetro Tempo É a curva de remapeamento da camada: a mesma que
            // o gráfico do painel de velocidade edita. O "Manter o tom do áudio"
            // do AE não existe aqui porque o motor não faz time-stretch — um
            // interruptor que não faz nada seria pior que a linha que falta.
            keywords = "remapear tempo time remap curva velocidade camera lenta rampa",
            principal = listOf(0, 1),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_tempo, suffix = "s", decimals = 2),
                1 to ParamHuman(label = R.string.fx_interpolacao_do_tempo),
            ),
        ),
    )
    put("aurea.control.slider", EffectHuman(name = R.string.fx_name_slider_control, keywords = "expressao slider controle", params = mapOf(0 to ParamHuman(decimals = 1))))
    put("aurea.control.angle", EffectHuman(name = R.string.fx_name_angle_control, keywords = "expressao angulo controle"))
    put("aurea.control.checkbox", EffectHuman(name = R.string.fx_name_checkbox_control, keywords = "expressao caixa controle"))
    put("aurea.control.color", EffectHuman(name = R.string.fx_name_color_control, keywords = "expressao cor controle"))
    put("aurea.control.point", EffectHuman(name = R.string.fx_name_point_control, keywords = "expressao ponto controle"))
    // --- Pacote de paridade: movimento, transições por forma e acabamento ---
    put("aurea.motion.oscillate.cycles", EffectHuman(
        name = R.string.fx_name_oscillate, keywords = "oscilar oscillate seno triangulo fase orbita",
        principal = listOf(0, 1, 2, 3, 4, 5),
        params = mapOf(2 to ParamHuman(suffix = "Hz", decimals = 2), 3 to ParamHuman(suffix = "px", decimals = 0),
            5 to ParamHuman(decimals = 2))))
    put("aurea.motion.oscillate", EffectHuman(
        name = R.string.fx_name_oscillate,
        keywords = "oscilar oscillate vai e vem pendular onda seno movimento decaimento balanco",
        principal = listOf(1, 0, 2, 8, 3, 4),
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_direcao),
            1 to ParamHuman(label = R.string.fx_amplitude, suffix = "px", decimals = 0),
            2 to ParamHuman(label = R.string.fx_frequencia, suffix = "Hz", decimals = 2),
            3 to ParamHuman(label = R.string.fx_fase),
            4 to ParamHuman(label = R.string.fx_rotacao),
            5 to ParamHuman(label = R.string.fx_pulso_escala, decimals = 0),
            6 to ParamHuman(label = R.string.fx_forma_onda),
            7 to ParamHuman(label = R.string.fx_semente),
            8 to ParamHuman(label = R.string.fx_decaimento, suffix = "1/s", decimals = 2),
            9 to ParamHuman(label = R.string.fx_pivo),
        )))
    put("aurea.motion.swing", EffectHuman(
        name = R.string.fx_name_swing,
        keywords = "balancar swing pendulo pendulum pivo girar oscilar",
        principal = listOf(0, 1, 2, 3, 4),
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_angulo),
            1 to ParamHuman(label = R.string.fx_frequencia, suffix = "Hz", decimals = 2),
            2 to ParamHuman(label = R.string.fx_pivo),
            3 to ParamHuman(label = R.string.fx_fase),
            4 to ParamHuman(label = R.string.fx_decaimento, suffix = "1/s", decimals = 2),
        )))
    put("aurea.motion.wiggle", EffectHuman(
        name = R.string.fx_name_wiggle,
        keywords = "wiggle agitar aleatorio tremer mexer posicao rotacao escala",
        principal = listOf(0, 1, 2, 3, 4, 5),
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_frequencia, suffix = "Hz", decimals = 2),
            1 to ParamHuman(label = R.string.fx_posicao_x, suffix = "px", decimals = 0),
            2 to ParamHuman(label = R.string.fx_posicao_y, suffix = "px", decimals = 0),
            3 to ParamHuman(label = R.string.fx_rotacao),
            4 to ParamHuman(label = R.string.fx_escala, decimals = 0),
            5 to ParamHuman(label = R.string.fx_intensidade, decimals = 0),
            6 to ParamHuman(label = R.string.fx_oitavas),
            7 to ParamHuman(label = R.string.fx_segurar_saltos),
            8 to ParamHuman(label = R.string.fx_semente),
            9 to ParamHuman(label = R.string.fx_pivo),
        )))
    put("aurea.motion.twitch", EffectHuman(
        name = R.string.fx_name_twitch,
        keywords = "tremor trancos twitch shake tremer impacto camera na mao glitch sacudir",
        principal = listOf(0, 1, 2, 3, 4, 5),
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_frequencia, suffix = "Hz", decimals = 1),
            1 to ParamHuman(label = R.string.fx_intensidade, decimals = 1),
            2 to ParamHuman(label = R.string.fx_rotacao),
            3 to ParamHuman(label = R.string.fx_escala, decimals = 1),
            4 to ParamHuman(label = R.string.fx_suavizar, decimals = 0),
            5 to ParamHuman(label = R.string.fx_decaimento, suffix = "1/s", decimals = 2),
            6 to ParamHuman(label = R.string.fx_semente),
        )))
    put("aurea.transition.iris_wipe", EffectHuman(
        name = R.string.fx_name_iris_wipe,
        keywords = "iris wipe circulo poligono transicao revelar abrir fechar",
        principal = listOf(0, 1, 3, 5),
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_conclusao, decimals = 0),
            1 to ParamHuman(label = R.string.fx_suavidade, decimals = 0),
            2 to ParamHuman(label = R.string.fx_sentido_inverso),
            3 to ParamHuman(label = R.string.fx_centro),
            4 to ParamHuman(label = R.string.fx_rotacao),
            5 to ParamHuman(label = R.string.fx_lados_iris),
        )))
    put("aurea.transition.box_wipe", EffectHuman(
        name = R.string.fx_name_box_wipe,
        keywords = "caixa box wipe retangulo transicao revelar",
        principal = listOf(0, 1, 3, 4),
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_conclusao, decimals = 0),
            1 to ParamHuman(label = R.string.fx_suavidade, decimals = 0),
            2 to ParamHuman(label = R.string.fx_sentido_inverso),
            3 to ParamHuman(label = R.string.fx_centro),
            4 to ParamHuman(label = R.string.fx_rotacao),
        )))
    put("aurea.transition.venetian_blinds", EffectHuman(
        name = R.string.fx_name_venetian_blinds,
        keywords = "persianas venetian blinds faixas listras transicao",
        principal = listOf(0, 4, 3, 1),
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_conclusao, decimals = 0),
            1 to ParamHuman(label = R.string.fx_suavidade, decimals = 0),
            2 to ParamHuman(label = R.string.fx_sentido_inverso),
            3 to ParamHuman(label = R.string.fx_direcao),
            4 to ParamHuman(label = R.string.fx_faixas, decimals = 0),
        )))
    put("aurea.blur.radial", EffectHuman(
        name = R.string.fx_name_radial_blur,
        keywords = "desfoque radial radial blur zoom blur spin giro rotacional velocidade",
        principal = listOf(0, 1, 2, 3, 4),
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_tipo),
            1 to ParamHuman(label = R.string.fx_intensidade, decimals = 0),
            2 to ParamHuman(label = R.string.fx_centro),
            3 to ParamHuman(label = R.string.fx_qualidade),
            4 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
        )))
    put("aurea.distort.mirror", EffectHuman(
        name = R.string.fx_name_mirror,
        keywords = "espelho mirror refletir simetria reflexo",
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_centro),
            1 to ParamHuman(label = R.string.fx_angulo),
            2 to ParamHuman(label = R.string.fx_trocar_lado),
        )))
    put("aurea.transform.crop", EffectHuman(
        name = R.string.fx_name_crop_edges,
        keywords = "cortar crop recortar bordas margens aparar",
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_esquerda, decimals = 0),
            1 to ParamHuman(label = R.string.fx_topo, decimals = 0),
            2 to ParamHuman(label = R.string.fx_direita, decimals = 0),
            3 to ParamHuman(label = R.string.fx_base, decimals = 0),
            4 to ParamHuman(label = R.string.fx_suavidade_borda, suffix = "px", decimals = 0),
        )))
    put("aurea.stylize.vignette", EffectHuman(
        name = R.string.fx_name_vignette,
        keywords = "vinheta vignette escurecer bordas cantos",
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_intensidade, decimals = 0),
            1 to ParamHuman(label = R.string.fx_tamanho, decimals = 0),
            2 to ParamHuman(label = R.string.fx_suavidade, decimals = 0),
            3 to ParamHuman(label = R.string.fx_arredondamento, decimals = 0),
            4 to ParamHuman(label = R.string.fx_centro),
            5 to ParamHuman(label = R.string.fx_cor),
        )))
    put("aurea.stylize.mosaic", EffectHuman(
        name = R.string.fx_name_mosaic,
        keywords = "mosaico mosaic pixelar pixelate led painel celulas pixel parede de led matriz de pontos dot matrix",
        principal = listOf(6, 0, 1, 2, 3, 5),
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_tamanho_celula, suffix = "px", decimals = 0),
            1 to ParamHuman(label = R.string.fx_vao_celulas, decimals = 0),
            2 to ParamHuman(label = R.string.fx_celulas_redondas),
            3 to ParamHuman(label = R.string.fx_sombreado, decimals = 0),
            4 to ParamHuman(label = R.string.fx_vinheta_celula, decimals = 0),
            5 to ParamHuman(label = R.string.fx_cor_fundo),
            6 to ParamHuman(label = R.string.fx_estilo),
            7 to ParamHuman(label = R.string.fx_vinheta, decimals = 0),
        )))
    put("aurea.stylize.find_edges", EffectHuman(
        name = R.string.fx_name_find_edges,
        keywords = "detectar bordas find edges contorno sobel desenho lapis",
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_intensidade, decimals = 0),
            1 to ParamHuman(label = R.string.fx_largura, suffix = "px", decimals = 1),
            2 to ParamHuman(label = R.string.fx_inverter),
            3 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
        )))
    // --- Pacote do editor antigo: o que só ele tinha ---
    put("aurea.color.fill", EffectHuman(
        name = R.string.fx_name_fill,
        keywords = "preencher fill cor chapada tinta solido",
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_cor),
            1 to ParamHuman(label = R.string.fx_opacidade, decimals = 0),
        )))
    put("aurea.color.balance_hls", EffectHuman(
        name = R.string.fx_name_color_balance_hls,
        keywords = "equilibrio balanco cor hls matiz luz saturacao",
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_matiz, suffix = "°", decimals = 0),
            1 to ParamHuman(label = R.string.fx_luminosidade, decimals = 0),
            2 to ParamHuman(label = R.string.fx_saturacao, decimals = 0),
        )))
    put("aurea.blur.zoom", EffectHuman(
        name = R.string.fx_name_zoom_blur,
        keywords = "zoom rastro radial lente empurrar desfoque",
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_centro),
            1 to ParamHuman(label = R.string.fx_intensidade, decimals = 0),
            2 to ParamHuman(label = R.string.fx_esticar_bordas),
            3 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
        )))
    put("aurea.distort.bulge", EffectHuman(
        name = R.string.fx_name_bulge,
        keywords = "bojo bulge pinca estufar puxar lente centro",
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_centro),
            1 to ParamHuman(label = R.string.fx_raio, suffix = "%", decimals = 0),
            2 to ParamHuman(label = R.string.fx_altura, suffix = "%", decimals = 0),
            3 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
        )))
    put("aurea.pattern.checkerboard", EffectHuman(
        name = R.string.fx_name_checkerboard,
        keywords = "xadrez checkerboard quadriculado tabuleiro celulas padrao",
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_largura_celula, suffix = "px", decimals = 0),
            1 to ParamHuman(label = R.string.fx_altura_celula, suffix = "px", decimals = 0),
            2 to ParamHuman(label = R.string.fx_ancora),
            3 to ParamHuman(label = R.string.fx_rotacao, suffix = "°", decimals = 0),
            4 to ParamHuman(label = R.string.fx_suavidade_borda, suffix = "px", decimals = 0),
            5 to ParamHuman(label = R.string.fx_inverter),
            6 to ParamHuman(label = R.string.fx_opacidade, decimals = 0),
            7 to ParamHuman(label = R.string.fx_cor),
        )))
    put("aurea.pattern.hexagonal", EffectHuman(
        name = R.string.fx_name_hexagonal,
        keywords = "hexagonal favos abelha painel led malha matriz",
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_tamanho_celula, suffix = "px", decimals = 0),
            1 to ParamHuman(label = R.string.fx_ancora),
            2 to ParamHuman(label = R.string.fx_rotacao, suffix = "°", decimals = 0),
            3 to ParamHuman(label = R.string.fx_espessura, suffix = "px", decimals = 0),
            4 to ParamHuman(label = R.string.fx_suavidade_borda, suffix = "px", decimals = 0),
            5 to ParamHuman(label = R.string.fx_inverter),
            6 to ParamHuman(label = R.string.fx_opacidade, decimals = 0),
            7 to ParamHuman(label = R.string.fx_cor),
        )))
    put("aurea.stylize.drop_shadow", EffectHuman(
        name = R.string.fx_name_drop_shadow,
        keywords = "sombra projetada drop shadow atras texto caixa distancia",
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_cor_sombra),
            1 to ParamHuman(label = R.string.fx_opacidade, decimals = 0),
            2 to ParamHuman(label = R.string.fx_direcao, suffix = "°", decimals = 0),
            3 to ParamHuman(label = R.string.fx_distancia, suffix = "px", decimals = 0),
            4 to ParamHuman(label = R.string.fx_suavidade, suffix = "px", decimals = 0),
            5 to ParamHuman(label = R.string.fxo_shadow_only),
        )))
    put("aurea.stylize.border", EffectHuman(
        name = R.string.fx_name_border,
        keywords = "borda contorno moldura traco outline",
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_cor),
            1 to ParamHuman(label = R.string.fx_largura, suffix = "px", decimals = 0),
            2 to ParamHuman(label = R.string.fx_opacidade, decimals = 0),
        )))
    // --- IA ---
    put("aurea.ai.depth_map", EffectHuman(
        name = R.string.fx_name_depth_map,
        keywords = "profundidade depth mapa ia ai midas distancia perto longe z matte fundo",
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
            1 to ParamHuman(label = R.string.fx_inverter),
            2 to ParamHuman(label = R.string.fx_suavizacao, decimals = 0),
        )))
    put("aurea.color.hue_saturation", EffectHuman(
        name = R.string.fx_name_hue_saturation,
        keywords = "matiz saturacao hue saturation luminosidade colorir tom cor",
        principal = listOf(0, 1, 2, 3),
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_matiz),
            1 to ParamHuman(label = R.string.fx_saturacao, decimals = 0),
            2 to ParamHuman(label = R.string.fx_luminosidade, decimals = 0),
            3 to ParamHuman(label = R.string.fx_colorir),
            4 to ParamHuman(label = R.string.fx_matiz_colorir),
            5 to ParamHuman(label = R.string.fx_saturacao_colorir, decimals = 0),
            6 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
        )))
    // --- Geradores e recorte do editor antigo ---
    put("aurea.generate.fractal_noise", EffectHuman(
        name = R.string.fx_name_fractal_noise,
        keywords = "ruido fractal fractal noise nuvem fumaca textura turbulencia perlin simplex",
        principal = listOf(0, 1, 2, 6, 7, 8),
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_tipo_ruido),
            1 to ParamHuman(label = R.string.fx_tipo_fractal),
            2 to ParamHuman(label = R.string.fx_contraste, decimals = 0),
            3 to ParamHuman(label = R.string.fx_brilho, decimals = 0),
            4 to ParamHuman(label = R.string.fx_inverter),
            5 to ParamHuman(label = R.string.fx_estouro),
            6 to ParamHuman(label = R.string.fx_escala, suffix = "px", decimals = 0),
            7 to ParamHuman(label = R.string.fx_complexidade, suffix = "oitavas", decimals = 1),
            8 to ParamHuman(label = R.string.fx_evolucao),
            9 to ParamHuman(label = R.string.fx_deslocamento, suffix = "px", decimals = 0),
            10 to ParamHuman(label = R.string.fx_rotacao),
            11 to ParamHuman(label = R.string.fx_semente),
            12 to ParamHuman(label = R.string.fx_influencia_oitavas, decimals = 0),
            13 to ParamHuman(label = R.string.fx_opacidade, decimals = 0),
            14 to ParamHuman(label = R.string.fx_mistura_camada),
            15 to ParamHuman(label = R.string.fx_preencher_caixa),
        )))
    put("aurea.generate.gradient_ramp", EffectHuman(
        name = R.string.fx_name_gradient_ramp,
        keywords = "degrade gradient ramp gradiente linear radial duas cores",
        principal = listOf(0, 1, 2, 3, 4, 6),
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_forma),
            1 to ParamHuman(label = R.string.fx_ponto_inicial),
            2 to ParamHuman(label = R.string.fx_ponto_final),
            3 to ParamHuman(label = R.string.fx_cor_inicial),
            4 to ParamHuman(label = R.string.fx_cor_final),
            5 to ParamHuman(label = R.string.fx_dispersao, suffix = "px", decimals = 0),
            6 to ParamHuman(label = R.string.fx_mistura_original, decimals = 0),
            7 to ParamHuman(label = R.string.fx_mistura_camada),
            8 to ParamHuman(label = R.string.fx_preencher_caixa),
        )))
    put("aurea.generate.four_color_gradient", EffectHuman(
        name = R.string.fx_name_four_color_gradient,
        keywords = "degrade 4 cores four color gradient quatro cores cantos",
        principal = listOf(4, 5, 6, 7, 8, 10),
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_ponto_1),
            1 to ParamHuman(label = R.string.fx_ponto_2),
            2 to ParamHuman(label = R.string.fx_ponto_3),
            3 to ParamHuman(label = R.string.fx_ponto_4),
            4 to ParamHuman(label = R.string.fx_cor_1),
            5 to ParamHuman(label = R.string.fx_cor_2),
            6 to ParamHuman(label = R.string.fx_cor_3),
            7 to ParamHuman(label = R.string.fx_cor_4),
            8 to ParamHuman(label = R.string.fx_suavidade, decimals = 0),
            9 to ParamHuman(label = R.string.fx_ruido, decimals = 0),
            10 to ParamHuman(label = R.string.fx_opacidade, decimals = 0),
            11 to ParamHuman(label = R.string.fx_mistura_camada),
            12 to ParamHuman(label = R.string.fx_preencher_caixa),
        )))
    put("aurea.generate.audio_spectrum", EffectHuman(
        name = R.string.fx_name_audio_spectrum,
        keywords = "espectro de audio audio spectrum som musica barras visualizador equalizador",
        principal = listOf(0, 1, 4, 5, 7, 8, 10),
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_fonte_som),
            1 to ParamHuman(label = R.string.fx_bandas),
            2 to ParamHuman(label = R.string.fx_ponto_inicial),
            3 to ParamHuman(label = R.string.fx_ponto_final),
            4 to ParamHuman(label = R.string.fx_altura_maxima, suffix = "px", decimals = 0),
            5 to ParamHuman(label = R.string.fx_espessura, suffix = "px", decimals = 1),
            6 to ParamHuman(label = R.string.fx_suavidade, decimals = 0),
            7 to ParamHuman(label = R.string.fx_cor_dentro),
            8 to ParamHuman(label = R.string.fx_cor_fora),
            9 to ParamHuman(label = R.string.fx_giro_matiz),
            10 to ParamHuman(label = R.string.fx_exibicao),
            11 to ParamHuman(label = R.string.fx_lado),
            12 to ParamHuman(label = R.string.fx_em_circulo),
            13 to ParamHuman(label = R.string.fx_compor_original),
            14 to ParamHuman(label = R.string.fx_sensibilidade, decimals = 0),
        )))
    put("aurea.stylize.stroke_outline", EffectHuman(
        name = R.string.fx_name_stroke_outline,
        keywords = "contorno stroke outline traco borda silhueta",
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_largura, suffix = "px", decimals = 1),
            1 to ParamHuman(label = R.string.fx_cor),
            2 to ParamHuman(label = R.string.fx_suavidade, suffix = "px", decimals = 1),
            3 to ParamHuman(label = R.string.fx_posicao),
            4 to ParamHuman(label = R.string.fx_opacidade, decimals = 0),
        )))
    put("aurea.key.matte_refine", EffectHuman(
        name = R.string.fx_name_matte_refine,
        keywords = "refinar recorte matte refine choke feather encolher suavizar mascara",
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_encolher, suffix = "px", decimals = 1),
            1 to ParamHuman(label = R.string.fx_suavizar_borda, suffix = "px", decimals = 1),
            2 to ParamHuman(label = R.string.fx_mostrar_mascara),
        )))
    // --- Pacote de áudio: efeitos de SOM (categoria Áudio) e os visuais ---
    put("aurea.audio.backwards", EffectHuman(
        name = R.string.afx_name_backwards,
        keywords = "reverso backwards tras para frente inverter som trocar canais",
        params = mapOf(0 to ParamHuman(label = R.string.afx_p_swap_channels)),
    ))
    put("aurea.audio.delay", EffectHuman(
        name = R.string.afx_name_delay,
        keywords = "atraso delay eco echo repeticao realimentacao feedback",
        principal = listOf(0, 1, 2, 3, 4),
        params = mapOf(
            0 to ParamHuman(label = R.string.afx_p_delay_time, decimals = 0),
            1 to ParamHuman(label = R.string.afx_p_delay_amount, decimals = 0),
            2 to ParamHuman(label = R.string.afx_p_feedback, decimals = 0),
            3 to ParamHuman(label = R.string.afx_p_dry_out, decimals = 0),
            4 to ParamHuman(label = R.string.afx_p_wet_out, decimals = 0),
        ),
    ))
    put("aurea.audio.flange_chorus", EffectHuman(
        name = R.string.afx_name_flange_chorus,
        keywords = "flange flanger chorus coro vozes modulacao",
        principal = listOf(0, 1, 2, 3, 7, 8),
        params = mapOf(
            0 to ParamHuman(label = R.string.afx_p_voice_separation, decimals = 1),
            1 to ParamHuman(label = R.string.afx_p_voices),
            2 to ParamHuman(label = R.string.afx_p_mod_rate, decimals = 2),
            3 to ParamHuman(label = R.string.afx_p_mod_depth, decimals = 0),
            4 to ParamHuman(label = R.string.afx_p_voice_phase, decimals = 0),
            5 to ParamHuman(label = R.string.afx_p_invert_phase),
            6 to ParamHuman(label = R.string.afx_p_stereo_voices),
            7 to ParamHuman(label = R.string.afx_p_dry_out, decimals = 0),
            8 to ParamHuman(label = R.string.afx_p_wet_out, decimals = 0),
        ),
    ))
    put("aurea.audio.high_low_pass", EffectHuman(
        name = R.string.afx_name_high_low_pass,
        keywords = "passa alta passa baixa high low pass filtro corte graves agudos",
        params = mapOf(
            0 to ParamHuman(label = R.string.afx_p_filter_options),
            1 to ParamHuman(label = R.string.afx_p_cutoff, decimals = 0),
            2 to ParamHuman(label = R.string.afx_p_dry_out, decimals = 0),
            3 to ParamHuman(label = R.string.afx_p_wet_out, decimals = 0),
        ),
    ))
    put("aurea.audio.stereo_mixer", EffectHuman(
        name = R.string.afx_name_stereo_mixer,
        keywords = "mixer estereo stereo pan balanco nivel fase canais",
        params = mapOf(
            0 to ParamHuman(label = R.string.afx_p_left_level, decimals = 0),
            1 to ParamHuman(label = R.string.afx_p_right_level, decimals = 0),
            2 to ParamHuman(label = R.string.afx_p_left_pan, decimals = 0),
            3 to ParamHuman(label = R.string.afx_p_right_pan, decimals = 0),
            4 to ParamHuman(label = R.string.afx_p_invert_phase),
        ),
    ))
    put("aurea.audio.modulator", EffectHuman(
        name = R.string.afx_name_modulator,
        keywords = "modulador modulator vibrato tremolo",
        params = mapOf(
            0 to ParamHuman(label = R.string.afx_p_mod_type),
            1 to ParamHuman(label = R.string.afx_p_mod_rate, decimals = 2),
            2 to ParamHuman(label = R.string.afx_p_mod_depth, decimals = 1),
            3 to ParamHuman(label = R.string.afx_p_amp_mod, decimals = 0),
        ),
    ))
    put("aurea.audio.parametric_eq", EffectHuman(
        name = R.string.afx_name_parametric_eq,
        keywords = "eq equalizador parametrico parametric bandas graves agudos",
        principal = (0 until 12).toList(),
        params = mapOf(
            0 to ParamHuman(label = R.string.afx_p_band1_enable),
            1 to ParamHuman(label = R.string.afx_p_band1_freq, decimals = 0),
            2 to ParamHuman(label = R.string.afx_p_band1_width, decimals = 1),
            3 to ParamHuman(label = R.string.afx_p_band1_gain, decimals = 1),
            4 to ParamHuman(label = R.string.afx_p_band2_enable),
            5 to ParamHuman(label = R.string.afx_p_band2_freq, decimals = 0),
            6 to ParamHuman(label = R.string.afx_p_band2_width, decimals = 1),
            7 to ParamHuman(label = R.string.afx_p_band2_gain, decimals = 1),
            8 to ParamHuman(label = R.string.afx_p_band3_enable),
            9 to ParamHuman(label = R.string.afx_p_band3_freq, decimals = 0),
            10 to ParamHuman(label = R.string.afx_p_band3_width, decimals = 1),
            11 to ParamHuman(label = R.string.afx_p_band3_gain, decimals = 1),
        ),
    ))
    put("aurea.audio.room_reverb", EffectHuman(
        name = R.string.afx_name_reverb,
        keywords = "reverb reverberacao sala eco ambiencia",
        params = mapOf(
            0 to ParamHuman(label = R.string.afx_p_reverb_time, decimals = 0),
            1 to ParamHuman(label = R.string.afx_p_diffusion, decimals = 0),
            2 to ParamHuman(label = R.string.afx_p_decay, decimals = 0),
            3 to ParamHuman(label = R.string.afx_p_brightness, decimals = 0),
            4 to ParamHuman(label = R.string.afx_p_dry_out, decimals = 0),
            5 to ParamHuman(label = R.string.afx_p_wet_out, decimals = 0),
        ),
    ))
    put("aurea.audio.tone", EffectHuman(
        name = R.string.afx_name_tone,
        keywords = "tom tone gerador seno onda quadrada bip nota acorde",
        principal = listOf(0, 1, 2, 3, 4, 5, 6),
        params = mapOf(
            0 to ParamHuman(label = R.string.afx_p_waveform),
            1 to ParamHuman(label = R.string.afx_p_freq1, decimals = 2),
            2 to ParamHuman(label = R.string.afx_p_freq2, decimals = 2),
            3 to ParamHuman(label = R.string.afx_p_freq3, decimals = 2),
            4 to ParamHuman(label = R.string.afx_p_freq4, decimals = 2),
            5 to ParamHuman(label = R.string.afx_p_freq5, decimals = 2),
            6 to ParamHuman(label = R.string.afx_p_level, decimals = 0),
        ),
    ))
    put("aurea.generate.audio_waveform", EffectHuman(
        name = R.string.afx_name_audio_waveform,
        keywords = "forma de onda waveform audio osciloscopio som visualizador",
        principal = listOf(0, 1, 2, 4, 7, 10, 11, 13),
        params = mapOf(
            0 to ParamHuman(label = R.string.afx_p_audio_layer),
            1 to ParamHuman(label = R.string.fx_ponto_inicial),
            2 to ParamHuman(label = R.string.fx_ponto_final),
            3 to ParamHuman(label = R.string.afx_p_displayed_samples),
            4 to ParamHuman(label = R.string.fx_altura_maxima, suffix = "px", decimals = 0),
            5 to ParamHuman(label = R.string.afx_p_audio_duration, decimals = 0),
            6 to ParamHuman(label = R.string.afx_p_audio_offset, decimals = 0),
            7 to ParamHuman(label = R.string.fx_espessura, suffix = "px", decimals = 1),
            8 to ParamHuman(label = R.string.fx_suavidade, decimals = 0),
            9 to ParamHuman(label = R.string.afx_p_random_seed),
            10 to ParamHuman(label = R.string.afx_p_inside_color),
            11 to ParamHuman(label = R.string.afx_p_outside_color),
            12 to ParamHuman(label = R.string.afx_p_waveform_options),
            13 to ParamHuman(label = R.string.afx_p_display_options),
            14 to ParamHuman(label = R.string.fx_compor_original),
        ),
    ))
    put("aurea.generate.spectrum_analyzer", EffectHuman(
        name = R.string.afx_name_spectrum,
        keywords = "espectro de audio audio spectrum som musica barras visualizador equalizador frequencias",
        principal = listOf(0, 1, 2, 3, 4, 5, 6, 7, 12, 13, 18, 19),
        params = mapOf(
            0 to ParamHuman(label = R.string.afx_p_audio_layer),
            1 to ParamHuman(label = R.string.fx_ponto_inicial),
            2 to ParamHuman(label = R.string.fx_ponto_final),
            3 to ParamHuman(label = R.string.afx_p_polar),
            4 to ParamHuman(label = R.string.afx_p_start_freq, decimals = 0),
            5 to ParamHuman(label = R.string.afx_p_end_freq, decimals = 0),
            6 to ParamHuman(label = R.string.afx_p_freq_bands),
            7 to ParamHuman(label = R.string.fx_altura_maxima, suffix = "px", decimals = 0),
            8 to ParamHuman(label = R.string.afx_p_audio_duration, decimals = 0),
            9 to ParamHuman(label = R.string.afx_p_audio_offset, decimals = 0),
            10 to ParamHuman(label = R.string.fx_espessura, suffix = "px", decimals = 1),
            11 to ParamHuman(label = R.string.fx_suavidade, decimals = 0),
            12 to ParamHuman(label = R.string.afx_p_inside_color),
            13 to ParamHuman(label = R.string.afx_p_outside_color),
            14 to ParamHuman(label = R.string.afx_p_blend_overlap),
            15 to ParamHuman(label = R.string.afx_p_hue_interp),
            16 to ParamHuman(label = R.string.afx_p_dynamic_hue),
            17 to ParamHuman(label = R.string.afx_p_color_symmetry),
            18 to ParamHuman(label = R.string.afx_p_display_options),
            19 to ParamHuman(label = R.string.afx_p_side_options),
            20 to ParamHuman(label = R.string.afx_p_duration_averaging),
            21 to ParamHuman(label = R.string.fx_compor_original),
        ),
    ))
    put("aurea.stylize.ball_grid", EffectHuman(
        name = R.string.afx_name_balls,
        keywords = "bolas esferas balls spheres grade particulas explodir dispersar torcer",
        principal = listOf(0, 1, 2, 3, 4, 5, 6),
        params = mapOf(
            0 to ParamHuman(label = R.string.afx_p_scatter, suffix = "px", decimals = 0),
            1 to ParamHuman(label = R.string.afx_p_rotation_axis),
            2 to ParamHuman(label = R.string.afx_p_rotation),
            3 to ParamHuman(label = R.string.afx_p_twist_property),
            4 to ParamHuman(label = R.string.afx_p_twist_angle),
            5 to ParamHuman(label = R.string.afx_p_grid_spacing, suffix = "px", decimals = 1),
            6 to ParamHuman(label = R.string.afx_p_ball_size, decimals = 0),
            7 to ParamHuman(label = R.string.afx_p_instability_state),
            8 to ParamHuman(label = R.string.afx_p_instability, suffix = "px", decimals = 1),
        ),
    ))
    // Particular: as partículas do app antigo. Principais: taxa, velocidade,
    // abertura, gravidade, vida, tamanho, opacidade, cores e mistura.
    put("aurea.generate.particular", EffectHuman(
        name = R.string.afx_name_particular,
        keywords = "particulas particles particular emissor emitter neve snow chuva rain fogo fire faiscas sparks fogos fireworks poeira dust bokeh",
        principal = listOf(0, 9, 13, 15, 22, 24, 27, 32, 33, 35, 38),
    ))
}

/**
 * Effect names stay in English in every app language. Localized names remain
 * search aliases, so old search habits and saved expression names still work.
 */
@Composable
internal fun effectDisplayName(typeId: Int, engineName: String): String {
    val context = LocalContext.current
    val configuration = androidx.compose.ui.platform.LocalConfiguration.current
    val english = remember(context, configuration) {
        context.createConfigurationContext(Configuration(configuration).apply { setLocale(Locale.ENGLISH) }).resources
    }
    return englishEffectName(typeId, engineName, english)
}

internal fun englishEffectName(typeId: Int, engineName: String, english: android.content.res.Resources): String =
    Table[typeId]?.name?.let { english.getString(it) } ?: EnglishEffectNames[typeId] ?: engineName

private val EnglishEffectNames = mapOf(
    "aurea.light.scene_flare" to "3D Flare", "aurea.color.cube_lut" to "LUT (.cube)",
    "aurea.light.halation" to "Halation", "aurea.light.lens_flare" to "Lens Flare",
    "aurea.distort.ripple" to "Ripple", "aurea.distort.optics_compensation" to "Optics Compensation",
    "aurea.blur.box" to "Box Blur", "aurea.blur.directional" to "Directional Blur",
    "aurea.transition.linear_wipe" to "Linear Wipe", "aurea.transition.radial_wipe" to "Radial Wipe",
    "aurea.transition.block_dissolve" to "Block Dissolve",
).mapKeys { effectTypeId(it.key) }

/** Texto onde a busca procura: nome humano, nome do motor, categoria e sinônimos. */
@Composable
internal fun effectSearchText(typeId: Int, engineName: String, category: String): String =
    normalizeSearch(listOf(effectDisplayName(typeId, engineName), Table[typeId]?.name?.let { stringResource(it) }.orEmpty(), engineName, category, Table[typeId]?.keywords.orEmpty()).joinToString(" "))

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
    "audio" -> CupertinoGlyph.MusicNote
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
