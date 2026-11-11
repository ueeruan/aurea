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
    /** Efeito que será PAGO numa versão futura (grátis nesta): selo "Grátis por enquanto" no
     *  navegador e o aviso ao adicionar. A troca para pago é só aqui (sem cobrança agora). */
    val futurePaid: Boolean = false,
)

/** O efeito será pago numa versão futura (hoje grátis)? Um lugar só: [EffectHuman.futurePaid]. */
internal fun effectFuturePaid(typeId: Int): Boolean = Table[typeId]?.futurePaid == true

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
    put("aurea.stylize.pixel_encoder", EffectHuman(keywords = "pixel encoder pixel_encoder megakrunch ascii letras grunge",
        principal = listOf(0, 1, 2, 3, 4)))
    put("aurea.stylize.pix_dither", EffectHuman(keywords = "pixdither pix dither pontilhado paleta atkinson bayer", principal = listOf(0, 1, 2)))
    put("aurea.time.low_fps", EffectHuman(keywords = "low fps stop motion travado quadros choppy tremido", principal = listOf(0, 1, 3),
        params = mapOf(0 to ParamHuman(suffix = " fps", decimals = 0), 1 to ParamHuman(decimals = 1), 3 to ParamHuman(decimals = 0))))
    put("aurea.light.glow_scan", EffectHuman(keywords = "glow scan varredura scanner faixa de luz brilho light sweep shine logo",
        principal = listOf(0, 1, 5, 8), params = mapOf(0 to ParamHuman(decimals = 0), 8 to ParamHuman(decimals = 0), 10 to ParamHuman(decimals = 0))))
    put("aurea.color.threshold", EffectHuman(keywords = "threshold limiar limite preto e branco duas cores 2 cores", principal = listOf(0, 1),
        params = mapOf(0 to ParamHuman(decimals = 0), 1 to ParamHuman(decimals = 0))))
    put("aurea.color.posterize", EffectHuman(keywords = "posterize posterizar niveis tons reduzir cores", principal = listOf(0, 1),
        params = mapOf(0 to ParamHuman(suffix = "", decimals = 0), 1 to ParamHuman(suffix = "", decimals = 2))))
    put("aurea.color.palettize", EffectHuman(keywords = "palettize paleta gameboy game boy nes pico-8 cga lospec gimp gpl hex retro pontilhado dither oklab",
        principal = listOf(0, 2, 4), params = mapOf(3 to ParamHuman(decimals = 0), 4 to ParamHuman(decimals = 0))))
    put("aurea.glitch.video_glitch", EffectHuman(keywords = "bcc glitch video continuum blocos flicker tremor", principal = listOf(0, 1, 2, 3, 55)))
    put("aurea.distort.displace_transform", EffectHuman(keywords = "displace displacer pro mapa escala rotacao aberracao", principal = listOf(0, 1, 2, 3, 4),
        params = mapOf(1 to ParamHuman(label = R.string.fx_deslocamento), 2 to ParamHuman(label = R.string.fx_escala))))
    put("aurea.light.starglow", EffectHuman(keywords = "starglow trapcode estrela brilho star raios shimmer",
        principal = listOf(0, 1, 2, 9, 15), params = mapOf(0 to ParamHuman(label = R.string.fx_comprimento))))
    put("aurea.distort.lens_gradient", EffectHuman(keywords = "s_distort sapphire distort lente lens refracao matte",
        principal = listOf(0, 2, 4, 5, 6)))
    put("aurea.transition.wipe_flux", EffectHuman(keywords = "wipeflux s_wipeflux sapphire fluxo bolhas transicao",
        principal = listOf(0, 1, 3, 8, 9), params = mapOf(5 to ParamHuman(scale = 1f, suffix = "", decimals = 3))))
    put("aurea.transition.wipe_plasma", EffectHuman(keywords = "wipeplasma s_wipeplasma sapphire plasma filamentos transicao",
        principal = listOf(0, 1, 3, 8, 9), params = mapOf(5 to ParamHuman(scale = 1f, suffix = "", decimals = 3))))
    put("aurea.distort.bender", EffectHuman(name = R.string.fx_name_bender,
        keywords = "bender bend curve curvar pontos", principal = listOf(0, 1, 2, 3)))
    put("aurea.distort.bend", EffectHuman(name = R.string.fx_name_bend,
        keywords = "bend fold dobra papel", principal = listOf(0, 1, 2, 5)))
    put("aurea.distort.curl", EffectHuman(name = R.string.fx_name_curl,
        keywords = "curl roll enrolar cilindro", principal = listOf(0, 1, 2, 3, 6)))
    put("aurea.distort.page_turn", EffectHuman(name = R.string.fx_name_page_turn,
        keywords = "page turn pagina papel cc", principal = listOf(0, 1, 2, 3, 8)))
    put("aurea.distort.mesh_warp", EffectHuman(name = R.string.fx_name_mesh_warp,
        keywords = "mesh warp malha deformacao grade bezier entortar", principal = listOf(0, 1, 2)))
    put("aurea.distort.puppet", EffectHuman(name = R.string.fx_name_puppet,
        keywords = "puppet fantoche pino pin marionete rig personagem deformar arap", principal = listOf(0, 1, 2)))
    put("aurea.distort.headbop", EffectHuman(name = R.string.fx_name_headbop,
        keywords = "headbop head bop cabeça balançar batida musica ritmo danca dancar pessoa beat nod groove", principal = listOf(0, 1, 2, 3, 13),
        futurePaid = true))
    put("aurea.move.path", EffectHuman(name = R.string.beta_move_path,
        keywords = "move along path caminho vetor trajetória percurso orientar", principal = listOf(0, 1, 2, 3, 4, 5)))
    put("aurea.distort.liquid_glass", EffectHuman(name = R.string.fx_name_liquid_glass,
        keywords = "liquid glass vidro liquido lente refracao glassmorphism", principal = listOf(0, 1, 2, 3, 5)))
    put("aurea.transition.noise_dissolve", EffectHuman(name = R.string.fx_name_noise_dissolve,
        keywords = "dissolve dissolver ruido queimar desintegrar", principal = listOf(0, 1, 3, 8, 10)))
    put("aurea.stylize.eight_bit", EffectHuman(name = R.string.fx_name_eight_bit,
        keywords = "8 bit 8bit pixel retro dither pontilhado game", principal = listOf(0, 1, 2, 3)))
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
    put("aurea.text.progress", EffectHuman(name = R.string.text_progress_name,
        keywords = "text progress progresso texto digitar typewriter cursor", principal = (0..3).toList()))
    put("aurea.text.randomizer", EffectHuman(name = R.string.text_randomizer_name,
        keywords = "text randomizer aleatorio decodificar decode senha censura caracteres", principal = (0..6).toList()))
    put("aurea.text.spacing", EffectHuman(name = R.string.text_spacing_name,
        keywords = "text spacing espacamento letras linhas tracking entrelinha", principal = (0..1).toList()))
    put("aurea.text.animator", EffectHuman(name = R.string.text_animator_name,
        keywords = "text animator animador texto letras palavras linhas cor blur desfoque random aleatorio",
        principal = (0..23).toList()))
    // 0.4.6: o animador completo (referência: animador de texto do After Effects).
    put("aurea.text.animator_pro", EffectHuman(name = R.string.text_animator_pro_name,
        keywords = "text animator pro animador texto avancado after effects seletor intervalo indice oscilacao wiggly ancora matiz linhas",
        principal = (0..43).toList()))
    put("aurea.stylize.bevel_alpha", EffectHuman(name = R.string.bevel_alpha_name,
        keywords = "bevel alpha bisel alfa relevo texto borda", principal = (0..4).toList()))
    put("aurea.color.gradient_map", EffectHuman(name = R.string.gradient_map_name,
        keywords = "gradient map mapa degrade gradiente texto cor tritone cc toner", principal = (0..4).toList()))
    put("aurea.text3d.layout", EffectHuman(keywords = "letter rotation letras rotacao delay atraso cylinder twist random aleatorio",
        principal = listOf(0, 1, 2, 12, 13, 9, 10)))
    // Formas 3D: o mesmo layout, parte a parte (mesmos índices do Text 3D Layout).
    put("aurea.shape3d.layout", EffectHuman(keywords = "shape parts partes forma 3d rotation rotacao delay atraso spread espalhar explodir twist random aleatorio",
        principal = listOf(0, 1, 2, 12, 13, 9, 4)))
    // Efeitos 3D (beta 12/10/2026): partes do objeto 3D, Shatter e Stroke.
    put("aurea.3d.part_transform", EffectHuman(name = R.string.fx3d_name_part_transform,
        keywords = "animar parte 3d animate part mover girar escala objeto modelo forma keyframe peca", principal = (0..7).toList()))
    put("aurea.3d.part_hide", EffectHuman(name = R.string.fx3d_name_part_hide,
        keywords = "esconder partes 3d hide parts sumir ocultar visibilidade objeto modelo forma peca", principal = (0..3).toList()))
    put("aurea.3d.part_texture", EffectHuman(name = R.string.fx3d_name_part_texture,
        keywords = "trocar textura 3d swap texture imagem image material parte objeto modelo forma galeria", principal = (0..3).toList()))
    put("aurea.3d.shatter", EffectHuman(name = R.string.fx3d_name_shatter,
        keywords = "3d shatter quebrar estilhacar explodir explosao pecas vidro cacos shatter break glass", principal = listOf(0, 1, 2, 4, 5, 3)))
    put("aurea.3d.stroke", EffectHuman(name = R.string.fx3d_name_stroke,
        keywords = "3d stroke traco linha contorno caminho mascara circulo espiral neon trapcode write on", principal = listOf(0, 1, 3, 4, 5, 9)))
    // --- Pacote CC (CCEffects.cpp e irmãos): o nome "CC" fica só na busca ---
    put("aurea.distort.kaleidoscope", EffectHuman(name = R.string.cc_name_kaleidoscope,
        keywords = "cc kaleida caleidoscopio kaleidoscope espelho mirror simetria symmetry mandala flor flower", principal = listOf(2, 1, 0, 3, 4)))
    put("aurea.distort.sphere_wrap", EffectHuman(name = R.string.cc_name_sphere_wrap,
        keywords = "cc sphere esfera sphere globo globe planeta planet bola ball 3d embrulhar wrap", principal = listOf(0, 1, 3, 2, 4, 5)))
    put("aurea.distort.cylinder_wrap", EffectHuman(name = R.string.cc_name_cylinder_wrap,
        keywords = "cc cylinder cilindro cylinder tubo tube lata can rolo roll 3d embrulhar wrap", principal = listOf(0, 1, 3, 2, 4, 5)))
    put("aurea.distort.twister", EffectHuman(name = R.string.cc_name_twister,
        keywords = "cc twister torcer twist fita ribbon girar espiral spiral virar flip", principal = listOf(0, 1, 2, 3, 4)))
    put("aurea.distort.flow_pinch", EffectHuman(name = R.string.cc_name_flow_pinch,
        keywords = "cc flo motion flow fluxo pinca pinch sugar suck buraco negro black hole liquify liquido", principal = listOf(0, 1, 2, 3, 4)))
    put("aurea.distort.griddler", EffectHuman(name = R.string.cc_name_griddler,
        keywords = "cc griddler grade grid ladrilhos tiles mosaico mosaic quadrados squares cartoes cards", principal = listOf(2, 3, 0, 1, 4)))
    put("aurea.stylize.repeat_tile", EffectHuman(name = R.string.cc_name_repeat_tile,
        keywords = "cc repetile repetir repeat ladrilho tile espelhar mirror expandir expand padrao pattern tijolos bricks", principal = listOf(4, 0, 1, 2, 3, 6)))
    put("aurea.distort.smear", EffectHuman(name = R.string.cc_name_smear,
        keywords = "cc smear borrar smear arrastar drag dedo finger liquify esticar stretch", principal = listOf(0, 1, 2, 3)))
    put("aurea.distort.split", EffectHuman(name = R.string.cc_name_split,
        keywords = "cc split 2 rasgar split abrir open corte cut fenda slit rachar crack rasgo tear", principal = listOf(0, 1, 2, 3)))
    put("aurea.distort.slant", EffectHuman(name = R.string.cc_name_slant,
        keywords = "cc slant inclinar slant italico italic cisalhar shear sombra projetada cast shadow", principal = listOf(0, 1, 2, 3, 4)))
    put("aurea.stylize.tiler", EffectHuman(name = R.string.cc_name_tiler,
        keywords = "cc tiler mosaico tiler copias copies repetir repeat grade grid", principal = listOf(0, 1, 2)))
    put("aurea.stylize.hex_tile", EffectHuman(name = R.string.cc_name_hex_tile,
        keywords = "cc hextile hexagono hexagon colmeia honeycomb ladrilho tile mosaico mosaic", principal = listOf(2, 0, 1, 3, 4, 5)))
    put("aurea.blur.vector_blur", EffectHuman(name = R.string.cc_name_vector_blur,
        keywords = "cc vector blur desfoque vetorial vector blur pinceladas brush strokes pintura painting", principal = listOf(1, 0, 2, 3, 5, 4)))
    put("aurea.light.light_burst", EffectHuman(name = R.string.cc_name_light_burst,
        keywords = "cc light burst explosao de luz light burst raios rays brilho glow zoom radial", principal = listOf(0, 1, 2, 3, 4, 5)))
    put("aurea.stylize.star_field", EffectHuman(name = R.string.cc_name_star_field,
        keywords = "cc star burst estrelas stars campo field pontos dots espaco space warp voar fly", principal = listOf(1, 3, 4, 0, 2, 5)))
    put("aurea.color.threshold_rgb", EffectHuman(name = R.string.cc_name_threshold_rgb,
        keywords = "cc threshold rgb limiar limite threshold canais channels pop art contraste", principal = listOf(0, 1, 2, 3, 4, 5)))
    put("aurea.color.color_offset", EffectHuman(name = R.string.cc_name_color_offset,
        keywords = "cc color offset deslocar cor color shift fase phase psicodelico psychedelic matiz hue", principal = listOf(0, 1, 2, 3, 4)))
    put("aurea.stylize.scatter", EffectHuman(name = R.string.cc_name_scatter,
        keywords = "cc scatterize espalhar scatter spray grao grain dispersar desintegrar poeira dust", principal = listOf(0, 1, 2, 3, 4)))
    put("aurea.stylize.blobby", EffectHuman(name = R.string.cc_name_blobby,
        keywords = "cc blobbylize bolhas blobs liquido liquid gosma goo derretido melt relevo", principal = listOf(1, 2, 0, 3, 5, 6)))
    put("aurea.stylize.glass", EffectHuman(name = R.string.cc_name_glass,
        keywords = "cc glass vidro glass relevo emboss refracao refraction bump transparente", principal = listOf(1, 2, 3, 0, 4, 6)))
    put("aurea.stylize.kernel", EffectHuman(name = R.string.cc_name_kernel,
        keywords = "cc kernel nucleo kernel convolucao convolution matriz matrix nitidez sharpen relevo emboss bordas edges", principal = listOf(0, 1, 2, 3, 4, 5, 6, 7, 8, 11)))
    put("aurea.light.spotlight", EffectHuman(name = R.string.cc_name_spotlight,
        keywords = "cc spotlight holofote spotlight luz light palco stage cone foco focus lanterna", principal = listOf(0, 1, 3, 6, 7, 4)))
    put("aurea.distort.drizzle", EffectHuman(name = R.string.cc_name_drizzle,
        keywords = "cc drizzle garoa drizzle chuva rain gotas drops ondas ripples agua water poca puddle", principal = listOf(0, 3, 4, 1, 2, 5)))
    put("aurea.transition.jaws", EffectHuman(name = R.string.cc_name_jaws,
        keywords = "cc jaws mandibulas jaws dentes teeth transicao transition abrir open serra saw", principal = listOf(0, 5, 1, 2, 3, 4)))
    put("aurea.transition.scale_wipe", EffectHuman(name = R.string.cc_name_scale_wipe,
        keywords = "cc scale wipe esticar stretch varredura wipe riscos streaks transicao transition", principal = listOf(0, 1, 2)))
    put("aurea.transition.radial_scale_wipe", EffectHuman(name = R.string.cc_name_radial_scale_wipe,
        keywords = "cc radial scalewipe scale wipe varredura radial radial wipe furo hole escala scale transicao transition", principal = listOf(0, 1, 2)))
    put("aurea.transition.burn_film", EffectHuman(name = R.string.cc_name_burn_film,
        keywords = "cc burn film queimar burn filme film fogo fire brasa ember transicao transition", principal = listOf(0, 1, 2)))
    put("aurea.transition.block_load", EffectHuman(name = R.string.cc_name_block_load,
        keywords = "cc block load blocos blocks carregar loading pixel jpeg progressivo progressive transicao transition", principal = listOf(0, 1, 2, 3)))
    // --- 0.1.8 BETA 2: Saber (o plugin de sabre/neon) e Luma Dissolve ---
    put("aurea.light.saber", EffectHuman(
        keywords = "saber video copilot sabre de luz lightsaber neon energia energy brilho glow raio laser traco stroke contorno outline eletrico electric fogo fire plasma magia magic write on desenhar mascara mask texto",
        principal = listOf(6, 7, 8, 1, 2, 3, 4, 5, 11, 12, 31)))
    put("aurea.transition.luma_dissolve", EffectHuman(
        keywords = "luma dissolve dissolveluma dissolver luminancia luminance brilho transicao transition sumir aparecer fade entrada saida gradiente gradient ruido noise queimar burn padrao pattern pixel grao grain blocos blocks hexagonal halftone camuflagem camo chamas flame tinta ink respingos splatter bolinhas polkadot estilhacos shards",
        principal = listOf(0, 3, 1, 10, 2, 8, 9, 5, 12, 13)))
    // --- 0.2.5: Progresso do desenho — corte exato no vetor, varredura nas imagens.
    // Mesmos rótulos e principais no iOS (EffectsHuman.swift).
    put("aurea.draw.drawing_progress", EffectHuman(
        name = R.string.fx_name_drawing_progress,
        keywords = "avanço do desenho avanco do desenho drawing progress inicio fim progresso do desenho drawing progress write on writeon desenhar desenho draw traco traço stroke contorno linha line art aparar trim path vetor vector caneta pen lapis tinta ink nanquim revelar reveal escrita handwriting caligrafia assinatura signature",
        principal = listOf(5, 0, 1, 2, 3, 4),
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_progresso, decimals = 0),
            1 to ParamHuman(label = R.string.fxl_draw_order),
            2 to ParamHuman(label = R.string.drawprog_reverse),
            3 to ParamHuman(label = R.string.fxl_tip_softness, decimals = 0),
            4 to ParamHuman(label = R.string.fx_angulo),
        )))
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
            keywords = "tint colorir duotone cc toner",
            params = mapOf(0 to ParamHuman(label = R.string.fx_cor_sombras), 1 to ParamHuman(label = R.string.fx_cor_luzes)),
        ),
    )
    put(
        "aurea.color.matrix",
        EffectHuman(
            name = R.string.fx_name_color_matrix,
            keywords = "matriz de cor channel mixer rgb canais shift channels",
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
            keywords = "chave de croma chroma key fundo verde green screen remover cor keylight linear color ultra",
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
            keywords = "grain grao filme ruido textura analogico add noise",
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
            keywords = "unsharp mascara de nitidez sharpen afiar detalhe mask",
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
            keywords = "lens blur desfoque de lente bokeh iris camera",
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
            keywords = "turbulencia displacement deslocamento ruido organico fumaca turbulent displace",
            principal = listOf(12, 0, 1, 2, 4),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_intensidade, suffix = "px", decimals = 0),
                1 to ParamHuman(label = R.string.fx_tamanho_ruido, suffix = "px", decimals = 0),
                2 to ParamHuman(label = R.string.fx_complexidade, suffix = "oitavas", decimals = 0),
                3 to ParamHuman(label = R.string.fx_evolucao, suffix = "°", decimals = 1),
                4 to ParamHuman(label = R.string.fx_deslocamento_x, suffix = "px", decimals = 0),
                5 to ParamHuman(label = R.string.fx_deslocamento_y, suffix = "px", decimals = 0),
                8 to ParamHuman(label = R.string.fx_bordas),
                9 to ParamHuman(label = R.string.fx_girar_deslocamento, suffix = "°", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.distort.wave_warp",
        EffectHuman(
            name = R.string.fx_name_wave_warp,
            keywords = "wave warp onda ondular senoide agua",
            principal = listOf(0, 1, 2, 4, 9),
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
            keywords = "warp lente distorcer empurrar puxar torcer esfera canto bulge pinch twist spherize twirl swirl",
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
            keywords = "deep glow brilho profundo halo neon luz bloom deepglow 2",
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
            keywords = "rays raios de luz god rays sol volumetrico spread cc light",
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
            keywords = "light sweep faixa de luz brilho varredura reflexo cc",
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
            keywords = "pixel sort ordenar pixels derreter listras glitch sort angulo radial intervalo",
            principal = listOf(0, 1, 2, 4, 10),
            params = mapOf(
                0 to ParamHuman(label = R.string.fx_limiar_baixo, decimals = 0),
                1 to ParamHuman(label = R.string.fx_limiar_alto, decimals = 0),
                2 to ParamHuman(label = R.string.fx_comprimento, decimals = 0),
                3 to ParamHuman(label = R.string.fx_aleatoriedade, decimals = 0),
                4 to ParamHuman(label = R.string.fx_direcao),
                5 to ParamHuman(label = R.string.fx_sentido_inverso),
                6 to ParamHuman(label = R.string.fx_ordenar),
                7 to ParamHuman(label = R.string.fxl_seed, decimals = 0),
                8 to ParamHuman(label = R.string.fx_faixa_tom),
                9 to ParamHuman(label = R.string.fx_passo, suffix = "px", decimals = 0),
                10 to ParamHuman(label = R.string.fx_angulo, suffix = "°", decimals = 1),
                11 to ParamHuman(label = R.string.fx_centro),
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
            keywords = "glitchify glitch defeito digital rasgo bloco corrupcao cinema spice transicao fatia jpeg webp compressao pontilhado ruido canal",
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
            keywords = "signal glitch sinal glitch de sinal interferencia transmissao banda sincronia chiado",
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
    // Edições aleatórias (Tempo, 0.2.7): trechos embaralhados pela semente.
    put("aurea.time.random_edits", EffectHuman(
        name = R.string.fx027_name_random_edits,
        keywords = "edicoes aleatorias random edits cortes embaralhar remontar jump cut sapphire",
        principal = listOf(0, 1),
        params = mapOf(
            0 to ParamHuman(label = R.string.fx027_edit_length, suffix = "quadros", decimals = 0),
            1 to ParamHuman(label = R.string.fxl_seed, decimals = 3),
        )))
    put(
        "aurea.time.slice",
        EffectHuman(
            name = R.string.fx_name_time_slice,
            keywords = "timeslice time slice fatias faixas tempo slit scan helice instantes",
            principal = listOf(0, 1, 2, 3),
            params = mapOf(
                0 to ParamHuman(label = R.string.fxl_slice_direction, suffix = "°", decimals = 0),
                1 to ParamHuman(label = R.string.fxl_slice_number, decimals = 0),
                2 to ParamHuman(label = R.string.fxl_frame_offset, suffix = "quadros", decimals = 1),
                3 to ParamHuman(label = R.string.fxl_interpolate_frames),
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
    // 0.4.5 (pedidos de usuários): glitch pesado, blocos no estilo do DaVinci e
    // o Shift Channels do AE. Mesma tabela no iOS (EffectsHuman.swift).
    put(
        "aurea.glitch.hyper_glitch",
        EffectHuman(
            name = R.string.fx045g_name_hyper_glitch,
            keywords = "hyper glitch hiper glitch pesado forte intenso blocos fatias rgb split pixel sort rastro compressao corrupcao datamosh",
            principal = listOf(0, 1, 2, 4, 5, 6, 8),
            params = mapOf(
                0 to ParamHuman(decimals = 0),
                2 to ParamHuman(suffix = "Hz", decimals = 1),
                3 to ParamHuman(suffix = "px", decimals = 0),
                5 to ParamHuman(suffix = "px", decimals = 0),
                7 to ParamHuman(suffix = "px", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.glitch.block_glitch",
        EffectHuman(
            name = R.string.fx045g_name_block_glitch,
            keywords = "block glitch blocos glitch de blocos davinci resolve fusion bloco retangular deslocar canais trocados macrobloco",
            principal = listOf(0, 1, 2, 4, 5, 6),
            params = mapOf(
                0 to ParamHuman(decimals = 0),
                1 to ParamHuman(suffix = "px", decimals = 0),
                2 to ParamHuman(suffix = "px", decimals = 0),
                4 to ParamHuman(suffix = "px", decimals = 0),
                5 to ParamHuman(suffix = "Hz", decimals = 1),
                7 to ParamHuman(suffix = "px", decimals = 0),
            ),
        ),
    )
    put(
        "aurea.color.shift_channels",
        EffectHuman(
            name = R.string.fx045g_name_shift_channels,
            keywords = "shift channels trocar canais mudar canais set channels canal alfa vermelho verde azul luminancia rgb trocar cores",
            principal = listOf(0, 1, 2, 3),
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
    // Câmera lenta por vetores e desfoque calculado da filmagem (0.1.8). Nomes
    // próprios; a busca acha pelos nomes dos plugins de mesmo propósito.
    put("aurea.time.flow_retime", EffectHuman(
        keywords = "twixtor kee flow retime camera lenta slow motion slowmo super slow optical flow vetores interpolar interpolacao quadros frame interpolation velocidade remapear tempo time remap timewarp suave duplicados repetidos duplicate frames chronos topaz camara lenta замедленная съемка замедление स्लो मोशन धीमी गति حركة بطيئة gerak lambat",
        principal = listOf(0, 1, 2, 3, 4),
        params = mapOf(5 to ParamHuman(suffix = "%", decimals = 0))))
    // Aprimorar (0.3.0): a restauração do Topaz (Proteus) com nome próprio.
    put("aurea.enhance.video", EffectHuman(
        name = R.string.fx030e_name,
        keywords = "aprimorar enhance topaz proteus restaurar melhorar qualidade upscale ruido denoise compressao deblock nitidez sharpen halo grao grain detalhe",
        principal = listOf(0, 1, 2, 3, 4, 12),
        params = mapOf(
            1 to ParamHuman(suffix = "%", decimals = 0),
            2 to ParamHuman(suffix = "%", decimals = 0),
            3 to ParamHuman(suffix = "%", decimals = 0),
            4 to ParamHuman(suffix = "%", decimals = 0),
            5 to ParamHuman(suffix = "%", decimals = 0),
            6 to ParamHuman(suffix = "%", decimals = 0),
            7 to ParamHuman(suffix = "%", decimals = 0),
            8 to ParamHuman(suffix = "px", decimals = 1),
            9 to ParamHuman(suffix = "%", decimals = 0),
            10 to ParamHuman(decimals = 0),
            12 to ParamHuman(suffix = "%", decimals = 0),
        )))
    // Copiar fundo (0.3.0): a camada vira uma janela; os efeitos DEPOIS dele
    // agem nas camadas abaixo, só dentro dela (como uma camada de ajuste recortada).
    put("aurea.utility.copy_background", EffectHuman(
        name = R.string.fx030k_name,
        keywords = "copiar fundo copy background janela window ajustar parte do video forma shape mascara recorte camada de ajuste adjustment layer backdrop",
        principal = listOf(0, 1),
        params = mapOf(
            0 to ParamHuman(label = R.string.fx030k_window),
            1 to ParamHuman(label = R.string.fx030k_layer_mix, suffix = "%", decimals = 0),
        )))
    put("aurea.time.flow_motion_blur", EffectHuman(
        keywords = "rsmb reelsmart motion blur desfoque de movimento borrao vetores optical flow obturador shutter",
        principal = listOf(0, 1, 2)))
    put(
        "aurea.time.remap",
        EffectHuman(
            name = R.string.fx_name_time_remap,
            // O parâmetro Tempo É a curva de remapeamento da camada: a mesma que
            // o gráfico do painel de velocidade edita. O "Manter o tom do áudio"
            // do AE não existe aqui porque o motor não faz time-stretch — um
            // interruptor que não faz nada seria pior que a linha que falta.
            keywords = "remapear tempo time remap curva velocidade camera lenta rampa twixtor slow motion",
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
        keywords = "wiggle agitar aleatorio tremer mexer posicao rotacao escala wiggler random",
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
        keywords = "desfoque radial radial blur zoom blur spin giro rotacional velocidade cc fast radial fast blur",
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
        keywords = "espelho mirror refletir simetria reflexo espelhar camada mirror layer flip inverter horizontal vertical dois eixos both axes quatro quadrantes sobrepor copia overlay copy alight motion am",
        principal = listOf(0, 1, 2, 3, 4),
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_centro),
            1 to ParamHuman(label = R.string.fx_angulo),
            2 to ParamHuman(label = R.string.fx_trocar_lado),
            5 to ParamHuman(decimals = 0),
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
        keywords = "vinheta vignette escurecer bordas cantos cc",
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
        keywords = "zoom rastro radial lente empurrar desfoque cc fast blur cc radial fast blur radial fast blur mais claro mais escuro brightest darkest",
        params = mapOf(
            0 to ParamHuman(label = R.string.fx_centro),
            1 to ParamHuman(label = R.string.fx_intensidade, decimals = 0),
            2 to ParamHuman(label = R.string.fx_esticar_bordas),
            3 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
            4 to ParamHuman(label = R.string.fxl_mode),
        )))
    put("aurea.distort.bulge", EffectHuman(
        name = R.string.fx_name_bulge,
        keywords = "bojo bulge pinca estufar puxar lente centro pinch",
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
        keywords = "ruido fractal fractal noise nuvem fumaca textura turbulencia perlin simplex turbulent",
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
    // Aurora (3D desde a 0.3.0): os controles na ordem de um gerador de aurora do AE.
    put("aurea.generate.aurora", EffectHuman(
        name = R.string.fx027_name_aurora,
        keywords = "aurora boreal northern lights ceu estrelas noite reflexo agua polar crates 3d camera",
        principal = listOf(1, 3, 4, 8, 9, 12),
        params = mapOf(
            0 to ParamHuman(label = R.string.fxs_distortion, decimals = 1),
            1 to ParamHuman(label = R.string.fx027_amount, decimals = 0),
            2 to ParamHuman(label = R.string.fxl_scale, decimals = 2),
            3 to ParamHuman(label = R.string.fxl_length, decimals = 1),
            4 to ParamHuman(label = R.string.fxl_exposure, decimals = 1),
            5 to ParamHuman(label = R.string.fxl_detail, decimals = 1),
            6 to ParamHuman(label = R.string.fx3_curvatura, decimals = 2),
            7 to ParamHuman(label = R.string.fx027_sky_glow, decimals = 1),
            8 to ParamHuman(label = R.string.fxl_color),
            9 to ParamHuman(label = R.string.fx027_secondary_color),
            10 to ParamHuman(label = R.string.fx027_sky_color),
            11 to ParamHuman(label = R.string.fx027_time_offset, suffix = "s", decimals = 2),
            12 to ParamHuman(label = R.string.fxl_speed, decimals = 1),
            13 to ParamHuman(label = R.string.fx027_horizon, decimals = 0),
            14 to ParamHuman(label = R.string.fx027_reflection, decimals = 0),
            15 to ParamHuman(label = R.string.fx027_ripple, decimals = 0),
            16 to ParamHuman(label = R.string.fx027_stars, decimals = 0),
            17 to ParamHuman(label = R.string.fx027_tilt, suffix = "°", decimals = 1),
            18 to ParamHuman(label = R.string.fx027_pan, suffix = "°", decimals = 1),
            19 to ParamHuman(label = R.string.fxl_field_of_view, suffix = "°", decimals = 0),
            20 to ParamHuman(label = R.string.fx027_sky_height, decimals = 0),
            21 to ParamHuman(label = R.string.fxl_opacity, decimals = 0),
            22 to ParamHuman(label = R.string.fx030_follow_camera),
            23 to ParamHuman(label = R.string.fxl_quality),
        )))
    // Correção de cor (0.3.0): o corretor completo (os painéis Filtros/Ajustar usam o mesmo efeito).
    put("aurea.color.grade", EffectHuman(
        name = R.string.fx030_name_color_grade,
        keywords = "correcao de cor color grade grading lumetri resolve log slog vlog logc apple log lut rodas wheels lift gamma gain hsl curvas temperatura",
        principal = listOf(0, 1, 2, 3, 4, 11, 12, 76),
        params = mapOf(
            1 to ParamHuman(label = R.string.grade_temperature, decimals = 0),
            2 to ParamHuman(label = R.string.grade_tint, decimals = 0),
            3 to ParamHuman(label = R.string.grade_exposure, suffix = "EV", decimals = 2),
            4 to ParamHuman(label = R.string.grade_contrast, decimals = 0),
            11 to ParamHuman(label = R.string.grade_saturation, decimals = 0),
            12 to ParamHuman(label = R.string.grade_vibrance, decimals = 0),
            76 to ParamHuman(label = R.string.grade_intensity, decimals = 0),
        )))
    // Tempo amplo (0.3.0): o CC Wide Time do AE.
    put("aurea.time.wide_time", EffectHuman(
        name = R.string.fx030_name_wide_time,
        keywords = "wide time cc tempo amplo rastro eco borrao quadros frente tras echo time blend trail edit",
        principal = listOf(0, 1, 2, 3),
        params = mapOf(
            0 to ParamHuman(label = R.string.fx030_forward_steps, decimals = 0),
            1 to ParamHuman(label = R.string.fx030_backward_steps, decimals = 0),
            2 to ParamHuman(label = R.string.fx030_native_motion_blur),
            3 to ParamHuman(label = R.string.fx030_layer_motion),
        )))
    // Constelação (0.3.0): rede 3D de pontos, linhas e triângulos (como o Plexus do AE).
    put("aurea.generate.constellation", EffectHuman(
        name = R.string.fx030c_name,
        keywords = "constelacao constellation plexus rede network pontos linhas triangulos particulas 3d conexoes trapcode",
        principal = listOf(0, 1, 33, 43, 20, 61),
        params = mapOf(
            0 to ParamHuman(label = R.string.fx030c_source),
            1 to ParamHuman(label = R.string.fx030c_count, decimals = 0),
            2 to ParamHuman(label = R.string.fxl_seed, decimals = 0),
            3 to ParamHuman(label = R.string.fx030c_width, suffix = "%", decimals = 0),
            4 to ParamHuman(label = R.string.fx030c_height, suffix = "%", decimals = 0),
            5 to ParamHuman(label = R.string.fx030c_depth, suffix = "%", decimals = 0),
            6 to ParamHuman(label = R.string.fx030c_radius, suffix = "%", decimals = 0),
            7 to ParamHuman(label = R.string.fx030c_tube_radius, suffix = "%", decimals = 0),
            8 to ParamHuman(label = R.string.fx030c_grid_x, decimals = 0),
            9 to ParamHuman(label = R.string.fx030c_grid_y, decimals = 0),
            10 to ParamHuman(label = R.string.fx030c_grid_z, decimals = 0),
            11 to ParamHuman(label = R.string.fx030c_path_layer),
            12 to ParamHuman(label = R.string.fx030c_jitter, suffix = "px", decimals = 0),
            13 to ParamHuman(label = R.string.fx030c_center),
            14 to ParamHuman(label = R.string.fx030c_position_z, suffix = "px", decimals = 0),
            15 to ParamHuman(label = R.string.fx030c_scale, suffix = "%", decimals = 0),
            16 to ParamHuman(label = R.string.fx030c_rotation_x, suffix = "°", decimals = 1),
            17 to ParamHuman(label = R.string.fx030c_rotation_y, suffix = "°", decimals = 1),
            18 to ParamHuman(label = R.string.fx030c_rotation_z, suffix = "°", decimals = 1),
            19 to ParamHuman(label = R.string.fx030c_spin_x, suffix = "°/s", decimals = 1),
            20 to ParamHuman(label = R.string.fx030c_spin_y, suffix = "°/s", decimals = 1),
            21 to ParamHuman(label = R.string.fx030c_spin_z, suffix = "°/s", decimals = 1),
            22 to ParamHuman(label = R.string.fx030c_noise),
            23 to ParamHuman(label = R.string.fx030c_noise_amount, suffix = "px", decimals = 0),
            24 to ParamHuman(label = R.string.fx030c_noise_scale, suffix = "px", decimals = 0),
            25 to ParamHuman(label = R.string.fx030c_noise_speed, decimals = 2),
            26 to ParamHuman(label = R.string.fx030c_noise_octaves, decimals = 0),
            27 to ParamHuman(label = R.string.fx030c_field),
            28 to ParamHuman(label = R.string.fx030c_field_center),
            29 to ParamHuman(label = R.string.fx030c_field_z, suffix = "px", decimals = 0),
            30 to ParamHuman(label = R.string.fx030c_field_radius, suffix = "px", decimals = 0),
            31 to ParamHuman(label = R.string.fx030c_field_strength, suffix = "px", decimals = 0),
            32 to ParamHuman(label = R.string.fx030c_lines),
            33 to ParamHuman(label = R.string.fx030c_line_max, suffix = "px", decimals = 0),
            34 to ParamHuman(label = R.string.fx030c_line_min, suffix = "px", decimals = 0),
            35 to ParamHuman(label = R.string.fx030c_connections, decimals = 0),
            36 to ParamHuman(label = R.string.fx030c_line_width, suffix = "px", decimals = 1),
            37 to ParamHuman(label = R.string.fx030c_line_opacity, suffix = "%", decimals = 0),
            38 to ParamHuman(label = R.string.fx030c_line_falloff, decimals = 2),
            39 to ParamHuman(label = R.string.fx030c_line_color_mode),
            40 to ParamHuman(label = R.string.fx030c_line_color),
            41 to ParamHuman(label = R.string.fx030c_line_perspective),
            42 to ParamHuman(label = R.string.fx030c_points),
            43 to ParamHuman(label = R.string.fx030c_point_size, suffix = "px", decimals = 1),
            44 to ParamHuman(label = R.string.fx030c_point_opacity, suffix = "%", decimals = 0),
            45 to ParamHuman(label = R.string.fx030c_color_points),
            46 to ParamHuman(label = R.string.fx030c_point_perspective),
            47 to ParamHuman(label = R.string.fx030c_point_softness, suffix = "%", decimals = 0),
            48 to ParamHuman(label = R.string.fx030c_only_connected),
            49 to ParamHuman(label = R.string.fx030c_triangles),
            50 to ParamHuman(label = R.string.fx030c_tri_max, suffix = "px", decimals = 0),
            51 to ParamHuman(label = R.string.fx030c_tri_opacity, suffix = "%", decimals = 0),
            52 to ParamHuman(label = R.string.fx030c_tri_color_mode),
            53 to ParamHuman(label = R.string.fx030c_tri_color),
            54 to ParamHuman(label = R.string.fx030c_depth_fog),
            55 to ParamHuman(label = R.string.fx030c_depth_near, suffix = "%", decimals = 0),
            56 to ParamHuman(label = R.string.fx030c_depth_far, suffix = "%", decimals = 0),
            57 to ParamHuman(label = R.string.fx030c_depth_color),
            58 to ParamHuman(label = R.string.fx030c_near_color),
            59 to ParamHuman(label = R.string.fx030c_far_color),
            60 to ParamHuman(label = R.string.fxl_opacity, suffix = "%", decimals = 0),
            61 to ParamHuman(label = R.string.fx030c_blend),
            62 to ParamHuman(label = R.string.fx030c_show_layer),
            63 to ParamHuman(label = R.string.fx030_follow_camera),
            64 to ParamHuman(label = R.string.fxl_field_of_view, suffix = "°", decimals = 0),
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
    put("aurea.generate.raster_extrude", EffectHuman(
        name = R.string.fx_name_raster_extrude,
        keywords = "extrusao extrusion extrude depth profundidade volume silhueta alpha alfa 3d reflexo reflection metal hdri",
        principal = listOf(0, 1, 4, 5, 6),
        params = mapOf(
            0 to ParamHuman(label = R.string.fxo_depth, suffix = "px", decimals = 1),
            1 to ParamHuman(label = R.string.fxl_side_color),
            2 to ParamHuman(label = R.string.fxl_shading, scale = 100f, suffix = "%", decimals = 0),
            3 to ParamHuman(label = R.string.fxl_quality),
            // Reflexo do ambiente, como nos modelos 3D (HDRI/estúdio da cena).
            4 to ParamHuman(label = R.string.fxl_environment_reflection, scale = 100f, suffix = "%", decimals = 0),
            5 to ParamHuman(label = R.string.fxl_metal, scale = 100f, suffix = "%", decimals = 0),
            6 to ParamHuman(label = R.string.fx_rugosidade, scale = 100f, suffix = "%", decimals = 0),
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
            5 to ParamHuman(label = R.string.fxo_depth, suffix = "px", decimals = 1),
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
        keywords = "bolas esferas balls spheres grade particulas explodir dispersar torcer cc ball cc ball action",
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
            9 to ParamHuman(label = R.string.fx_deform_light_direction),
            10 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
        ),
    ))
    // Desintegrar: a camada se desfaz em fragmentos que voam. Principais:
    // conclusão, direção, tamanho, aleatoriedade, velocidade e brilho da borda.
    put("aurea.transition.disintegrate", EffectHuman(
        name = R.string.afx_name_disintegrate,
        keywords = "desintegrar disintegrate particulas particles fragmentos poeira dust cinzas dissolver explodir transicao",
        principal = listOf(0, 1, 2, 3, 5, 11),
        params = mapOf(
            0 to ParamHuman(decimals = 0),
            2 to ParamHuman(suffix = "px", decimals = 1),
            3 to ParamHuman(decimals = 0),
            5 to ParamHuman(suffix = "px", decimals = 0),
            6 to ParamHuman(label = R.string.particular_turbulence, suffix = "px", decimals = 0),
            7 to ParamHuman(suffix = "px", decimals = 0),
            8 to ParamHuman(decimals = 0),
            11 to ParamHuman(label = R.string.afx_p_edge_glow, decimals = 0),
            12 to ParamHuman(label = R.string.fx_mistura, decimals = 0),
        ),
    ))
    // Particular: as partículas do app antigo. Principais: taxa, velocidade,
    // abertura, gravidade, vida, tamanho, opacidade, cores e mistura.
    put("aurea.generate.particular", EffectHuman(
        name = R.string.afx_name_particular,
        keywords = "particulas particles particular emissor emitter neve snow chuva rain fogo fire faiscas sparks fogos fireworks poeira dust bokeh cc rainfall snowfall particle world",
        principal = listOf(0, 9, 13, 15, 22, 24, 27, 32, 33, 35, 38),
    ))
    // --- 0.4.5: pedidos de usuários. Nomes de produtos de terceiros (Light Wrap
    // dos compositores, Magic Bullet Looks, Ballpoint/Dust dos pacotes de
    // transição) só como palavras de busca, como no pacote CC. Mesmos
    // principais no iOS (EffectsHuman.swift).
    put("aurea.distort.water_reflection", EffectHuman(
        name = R.string.fx045_name_water_reflection,
        keywords = "reflexo na agua water reflection espelho mirror lago lake rio river mar sea oceano ocean ondas waves ondulacao ripple poca puddle superficie surface horizonte horizon",
        principal = listOf(0, 2, 4, 5, 6, 7, 3, 11),
    ))
    // 0.5.2: pacote "Mar" (SeaEffects.cpp) + a Ondulação radial com velocidade e
    // brilho. Mesmos principais no iOS (EffectsHuman.swift).
    put("aurea.generate.ocean", EffectHuman(name = R.string.sea_name_ocean,
        keywords = "oceano ocean mar sea agua water onda ondas wave waves praia beach horizonte horizon gerstner espuma foam sol sun ceu sky fundo background oceano realista desenho",
        principal = listOf(0, 1, 2, 3, 10, 11, 5, 6, 14, 15, 7)))
    put("aurea.distort.underwater", EffectHuman(name = R.string.sea_name_underwater,
        keywords = "embaixo dagua debaixo dagua underwater submerso mar sea oceano ocean agua water onda wave fundo do mar caustica caustics raios de luz god rays particulas mergulho dive piscina pool",
        principal = listOf(0, 3, 6, 7, 8, 5, 2)))
    put("aurea.light.caustics", EffectHuman(name = R.string.sea_name_caustics,
        keywords = "causticas caustics luz na agua water light mar sea oceano ocean piscina pool fundo onda wave reflexo",
        principal = listOf(1, 3, 2, 4, 5, 0, 6)))
    put("aurea.transition.shore_wave", EffectHuman(name = R.string.sea_name_shore_wave,
        keywords = "onda na praia shore wave wave wipe transicao transition mar sea oceano ocean praia beach espuma foam agua water onda ondas",
        principal = listOf(0, 1, 2, 3, 6, 8, 4, 9)))
    put("aurea.distort.ripple", EffectHuman(
        keywords = "ondulacao radial ripple onda wave anel rings agua water gota drop lago lake mar sea poca puddle",
        principal = listOf(0, 1, 2, 3, 6, 7, 5, 4)))
    put("aurea.light.light_wrap", EffectHuman(
        name = R.string.fx045_name_light_wrap,
        keywords = "light wrap envolver com luz luz de borda edge light envoltorio wrap composicao compositing recorte keying chroma fundo background integrar blend",
        principal = listOf(0, 1, 2, 3, 4, 5),
    ))
    put("aurea.color.looks", EffectHuman(
        name = R.string.fx045_name_looks,
        keywords = "magic bullet looks look looks de cinema cinematic filme film preset filtro filter teal orange bleach bypass noir vintage cyberpunk sepia pastel hora dourada golden hour noite americana day for night faroeste western correcao de cor color grade",
        principal = listOf(0, 1, 2, 3, 4, 5),
    ))
    put("aurea.transition.ballpoint", EffectHuman(
        name = R.string.fx045_name_ballpoint,
        keywords = "ballpoint caneta esferografica pen bic rabisco scribble rabiscar hachura hatch desenho drawing escrita handwriting revelar reveal tinta ink transicao transition",
        principal = listOf(0, 1, 2, 3, 4, 5, 9),
    ))
    put("aurea.transition.dust", EffectHuman(
        name = R.string.fx045_name_dust,
        keywords = "dust poeira po areia sand cinzas ashes vento wind desintegrar disintegrate particulas particles desfazer dissolver dissolve sumir transicao transition",
        principal = listOf(0, 1, 5, 6, 7, 2, 13),
    ))
    // Sinônimos de busca (nomes do After Effects e da referência) dos efeitos sem ficha.
    put("aurea.blur.box", EffectHuman(keywords = "fast box blur"))
    put("aurea.blur.directional", EffectHuman(keywords = "motion blur"))
    put("aurea.light.lens_flare", EffectHuman(keywords = "optical flares"))
    put("aurea.distort.fisheye", EffectHuman(keywords = "cc lens fisheye"))
    put("aurea.distort.corner_pin", EffectHuman(keywords = "cc power pin"))
    put("aurea.color.cube_lut", EffectHuman(keywords = "apply color lut"))
    put("aurea.key.rotobrush", EffectHuman(keywords = "roto brush rotoscope"))
    // 0.4.5: Remover fundo (sem tela verde). Principais: modelo, contrair/expandir,
    // suavidade, borda fina, visualização e inverter; o resto em Avançado.
    put("aurea.key.background_remove", EffectHuman(name = R.string.bg_remove_fx_name,
        keywords = "remove background background remover remover fundo tirar fundo recortar pessoa sem tela verde chroma key green screen " +
            "quitar fondo borrar fondo удалить фон बैकग्राउंड हटाएँ إزالة الخلفية hapus latar blurrr capcut cutout segment",
        principal = listOf(10, 2, 1, 9, 5, 3),
    ))
    put("aurea.color.rgb_split", EffectHuman(keywords = "rgb shift"))
    // 0.4.5 (pedidos de usuários): ladrilhos que se mexem e caixa oca do Alight
    // Motion, Time Displacement do AE. Nomes em inglês; a busca acha nos 7 idiomas
    // pelo nome traduzido (R.string.fx045d_name_*) e pelas palavras abaixo.
    put("aurea.distort.tile_shift", EffectHuman(name = R.string.fx045d_name_tile_shift,
        keywords = "tile shift deslocar ladrilhos ladrilho azulejo mosaico grade quadrados blocos fileiras colunas aleatorio glitch alight motion am",
        principal = listOf(0, 1, 2, 3, 4), params = mapOf(0 to ParamHuman(decimals = 0), 1 to ParamHuman(decimals = 0), 3 to ParamHuman(decimals = 0))))
    put("aurea.distort.hex_tile_rotate", EffectHuman(name = R.string.fx045d_name_hex_tile_rotate,
        keywords = "hexagon tile rotate hexagono hexagonal hex colmeia honeycomb celulas girar rotacao rodar mosaico alight motion am",
        principal = listOf(0, 1, 2, 3, 4), params = mapOf(0 to ParamHuman(decimals = 0), 3 to ParamHuman(decimals = 0))))
    put("aurea.distort.hex_tile_shift", EffectHuman(name = R.string.fx045d_name_hex_tile_shift,
        keywords = "hexagon tile shift hexagono hexagonal hex colmeia honeycomb celulas deslocar mover mosaico alight motion am",
        principal = listOf(0, 1, 2, 3, 4), params = mapOf(0 to ParamHuman(decimals = 0), 1 to ParamHuman(decimals = 0), 3 to ParamHuman(decimals = 0))))
    put("aurea.distort.hollow_box", EffectHuman(name = R.string.fx045d_name_hollow_box,
        keywords = "hollow box caixa oca vazada cubo cube box 3d lados recorte contorno moldura alight motion am",
        principal = listOf(6, 7, 8, 10, 2, 3, 4), params = mapOf(3 to ParamHuman(decimals = 1), 4 to ParamHuman(decimals = 0), 5 to ParamHuman(decimals = 1))))
    put("aurea.time.time_displacement", EffectHuman(name = R.string.fx045d_name_time_displacement,
        keywords = "time displacement deslocamento no tempo deslocamento temporal slit scan fenda atraso mapa luminancia brilho after effects ae kee",
        principal = listOf(0, 1, 2, 3, 4, 5), params = mapOf(2 to ParamHuman(decimals = 2), 3 to ParamHuman(decimals = 0))))
    // 0.4.6: efeitos novos (GapEffects046.cpp).
    put("aurea.distort.swirl", EffectHuman(name = R.string.fx046_name_swirl,
        keywords = "swirl twirl redemoinho girar espiral vortex", principal = listOf(0, 1, 2, 3)))
    put("aurea.distort.polar_coordinates", EffectHuman(name = R.string.fx046_name_polar,
        keywords = "polar coordinates coordenadas polares circulo planeta tiny planet", principal = listOf(0, 1, 2)))
    put("aurea.distort.tunnel", EffectHuman(name = R.string.fx046_name_tunnel,
        keywords = "tunnel tunel infinito viagem warp", principal = listOf(0, 1, 2, 3, 4, 5)))
    put("aurea.distort.magnify", EffectHuman(name = R.string.fx046_name_magnify,
        keywords = "magnify lupa zoom ampliar lente", principal = listOf(0, 1, 2, 3, 4, 5)))
    put("aurea.stylize.long_shadow", EffectHuman(name = R.string.fx046_name_long_shadow,
        keywords = "long shadow sombra longa flat design", principal = listOf(0, 1, 2, 3, 4, 5)))
    put("aurea.stylize.roughen_edges", EffectHuman(name = R.string.fx046_name_roughen,
        keywords = "roughen edges bordas asperas corroer papel rasgado", principal = listOf(0, 1, 2, 3)))
    put("aurea.stylize.contour_lines", EffectHuman(name = R.string.fx046_name_contour,
        keywords = "contour lines curvas de nivel topografico mapa linhas", principal = listOf(0, 1, 2, 3, 4)))
    put("aurea.light.inner_glow", EffectHuman(name = R.string.fx046_name_inner_glow,
        keywords = "inner glow brilho interno luz borda dentro", principal = listOf(0, 1, 2, 3, 4)))
    put("aurea.generate.lightning", EffectHuman(name = R.string.fx046_name_lightning,
        keywords = "lightning raio relampago eletrico eletricidade descarga", principal = listOf(0, 1, 2, 3, 4, 5, 6, 7, 8, 9)))
    put("aurea.generate.voronoi_cells", EffectHuman(name = R.string.fx046_name_voronoi,
        keywords = "voronoi cells celulas vitral mosaico bolhas", principal = listOf(0, 1, 2, 3, 4, 5, 6)))
    // Lote "AE 2026" (AeEffects2026*.cpp): os efeitos do AE que faltavam. Nomes em
    // inglês (R.string.aefx_name_*); a busca acha pelos nomes nos 7 idiomas e pelas
    // palavras abaixo. Mesma lista no iOS (EffectsHuman.swift).
    put("aurea.key.unmult", EffectHuman(name = R.string.aefx_name_unmult,
        keywords = "unmult unmultiply remover preto fundo preto tirar preto luz fogo logo transparencia add screen knock out black", principal = listOf(0, 1, 2, 3)))
    put("aurea.color.leave_color", EffectHuman(name = R.string.aefx_name_leave_color,
        keywords = "leave color manter cor so uma cor cor seletiva sin city pb parcial isolar cor descolorir", principal = listOf(0, 1, 2, 3, 4)))
    put("aurea.color.change_color", EffectHuman(name = R.string.aefx_name_change_color,
        keywords = "change color mudar cor trocar cor substituir cor matiz hue recolorir", principal = listOf(1, 2, 3, 4, 5, 6, 7)))
    put("aurea.color.change_to_color", EffectHuman(name = R.string.aefx_name_change_to_color,
        keywords = "change to color mudar para cor trocar cor de para substituir recolorir", principal = listOf(0, 1, 2, 3, 4, 7)))
    put("aurea.color.photo_filter", EffectHuman(name = R.string.aefx_name_photo_filter,
        keywords = "photo filter filtro de foto quente frio aquecimento resfriamento warming cooling sepia lente", principal = listOf(0, 1, 2, 3)))
    put("aurea.color.black_white", EffectHuman(name = R.string.aefx_name_black_white,
        keywords = "black white preto e branco pb monocromatico cinza tons de cinza bw grayscale", principal = listOf(0, 1, 2, 3, 4, 5, 6, 7)))
    put("aurea.color.selective_color", EffectHuman(name = R.string.aefx_name_selective_color,
        keywords = "selective color cor seletiva cmyk ciano magenta amarelo preto correcao", principal = listOf(0, 1, 2, 3, 4)))
    put("aurea.color.alpha_levels", EffectHuman(name = R.string.aefx_name_alpha_levels,
        keywords = "alpha levels niveis do alfa transparencia opacidade canal alfa"))
    put("aurea.color.remove_color_matting", EffectHuman(name = R.string.aefx_name_remove_color_matting,
        keywords = "remove color matting remover fosco de cor borda halo premultiplicado unpremultiply"))
    put("aurea.color.arithmetic", EffectHuman(name = R.string.aefx_name_arithmetic,
        keywords = "arithmetic aritmetica canal somar subtrair multiplicar diferenca bits"))
    put("aurea.key.color_range", EffectHuman(name = R.string.aefx_name_color_range,
        keywords = "color range faixa de cor chave key keying recorte lab yuv rgb tela verde", principal = listOf(0, 1, 2, 3, 4, 5, 6, 7)))
    put("aurea.stylize.strobe_light", EffectHuman(name = R.string.aefx_name_strobe_light,
        keywords = "strobe light luz estroboscopica flash piscar balada pisca", principal = listOf(0, 1, 2, 3, 4, 5, 6)))
    put("aurea.stylize.noise_hls", EffectHuman(name = R.string.aefx_name_noise_hls,
        keywords = "noise hls ruido hls matiz luminosidade saturacao granulado grao"))
    put("aurea.stylize.noise_hls_auto", EffectHuman(name = R.string.aefx_name_noise_hls_auto,
        keywords = "noise hls auto ruido hls automatico animado matiz luminosidade saturacao grao"))
    put("aurea.stylize.noise_alpha", EffectHuman(name = R.string.aefx_name_noise_alpha,
        keywords = "noise alpha ruido alfa transparencia granulado dissolver", principal = listOf(0, 1, 2, 3, 4, 5)))
    put("aurea.color.blend_layers", EffectHuman(name = R.string.aefx_name_blend_layers,
        keywords = "blend mesclar misturar camadas crossfade transicao cruzada so cor clarear escurecer"))
    put("aurea.key.difference_matte", EffectHuman(name = R.string.aefx_name_difference_matte,
        keywords = "difference matte fosco por diferenca chave recorte fundo limpo plate"))
    put("aurea.distort.warp_styles", EffectHuman(name = R.string.aefx_name_warp_styles,
        keywords = "warp styles deformar estilos arco bandeira onda peixe bojo torcer inflar espremer olho de peixe texto"))
    put("aurea.stylize.emboss", EffectHuman(name = R.string.aefx_name_emboss,
        keywords = "emboss relevo gravado alto relevo cinza"))
    put("aurea.stylize.color_emboss", EffectHuman(name = R.string.aefx_name_color_emboss,
        keywords = "color emboss relevo colorido gravado alto relevo"))
    put("aurea.stylize.radial_shadow", EffectHuman(name = R.string.aefx_name_radial_shadow,
        keywords = "radial shadow sombra radial luz pontual projetada", principal = listOf(0, 1, 2, 3, 4, 5, 6, 7)))
    put("aurea.stylize.inner_shadow", EffectHuman(name = R.string.aefx_name_inner_shadow,
        keywords = "inner shadow sombra interna photoshop estilo de camada"))
    put("aurea.stylize.bevel_edges", EffectHuman(name = R.string.aefx_name_bevel_edges,
        keywords = "bevel edges bisel das bordas chanfro moldura botao 3d"))
    put("aurea.stylize.brush_strokes", EffectHuman(name = R.string.aefx_name_brush_strokes,
        keywords = "brush strokes pinceladas pintura pintado oleo aquarela artistico pincel"))
    put("aurea.key.simple_wire_removal", EffectHuman(name = R.string.aefx_name_simple_wire_removal,
        keywords = "simple wire removal remocao de fio cabo apagar fio retoque limpar"))
    put("aurea.stylize.texturize", EffectHuman(name = R.string.aefx_name_texturize,
        keywords = "texturize texturizar textura relevo lona tela papel"))
    // 0.5.2: Textura (TextureOverlayEffect.cpp) — folha procedural por cima da camada.
    put("aurea.stylize.texture_overlay", EffectHuman(name = R.string.tex_section,
        keywords = "textura texture overlay sobreposicao papel paper papel amassado crumpled grao de filme film grain poeira dust arranhoes scratches concreto concrete lona canvas tecido fabric grunge meio-tom halftone ruido noise vidro fosco frosted glass pedra stone madeira wood metal escovado brushed couro leather imagem galeria foto vintage", principal = listOf(0, 1, 2, 3, 4, 5, 6, 7)))
    put("aurea.stylize.plastic", EffectHuman(name = R.string.aefx_name_plastic,
        keywords = "plastic plastico relevo brilho especular metal 3d luz", principal = listOf(1, 2, 3, 6, 9, 10, 13, 14)))
    put("aurea.blur.bilateral", EffectHuman(name = R.string.aefx_name_bilateral_blur,
        keywords = "bilateral blur desfoque bilateral suavizar pele beauty preservar bordas"))
    put("aurea.blur.smart", EffectHuman(name = R.string.aefx_name_smart_blur,
        keywords = "smart blur desfoque inteligente suavizar bordas cartoon"))
    put("aurea.blur.cross", EffectHuman(name = R.string.aefx_name_cross_blur,
        keywords = "cross blur desfoque em cruz horizontal vertical estrela"))
    put("aurea.generate.circle", EffectHuman(name = R.string.aefx_name_circle,
        keywords = "circle circulo anel aro forma disco gerar", principal = listOf(0, 1, 2, 3, 7, 8, 9)))
    put("aurea.generate.ellipse", EffectHuman(name = R.string.aefx_name_ellipse,
        keywords = "ellipse elipse anel oval aro brilho forma gerar"))
    put("aurea.generate.fractal", EffectHuman(name = R.string.aefx_name_fractal,
        keywords = "fractal mandelbrot julia psicodelico gerar zoom infinito", principal = listOf(0, 1, 2, 3, 4, 5, 8, 13)))
    put("aurea.generate.eyedropper_fill", EffectHuman(name = R.string.aefx_name_eyedropper_fill,
        keywords = "eyedropper fill preencher conta gotas amostra cor media chapada"))
    put("aurea.transition.grid_wipe", EffectHuman(name = R.string.aefx_name_grid_wipe,
        keywords = "grid wipe limpar em grade transicao ladrilhos quadrados portas"))
    put("aurea.transition.light_wipe", EffectHuman(name = R.string.aefx_name_light_wipe,
        keywords = "light wipe limpar com luz transicao brilho clarao"))
    put("aurea.transition.line_sweep", EffectHuman(name = R.string.aefx_name_line_sweep,
        keywords = "line sweep varredura em linhas transicao listras faixas escada"))
    put("aurea.generate.rain", EffectHuman(name = R.string.aefx_name_rain,
        keywords = "rain chuva gotas tempestade clima gerar"))
    put("aurea.generate.snow", EffectHuman(name = R.string.aefx_name_snow,
        keywords = "snow neve flocos inverno clima natal gerar"))
    put("aurea.ai.depth_matte", EffectHuman(name = R.string.aefx_name_depth_matte,
        keywords = "depth matte fosco por profundidade ia ai recorte frente fundo distancia"))
    put("aurea.ai.fog_3d", EffectHuman(name = R.string.aefx_name_fog_3d,
        keywords = "fog 3d neblina nevoa ia ai profundidade atmosfera bruma"))
    put("aurea.audio.bass_treble", EffectHuman(name = R.string.aefx_name_bass_treble,
        keywords = "bass treble graves agudos equalizador eq tom audio som"))
    put("aurea.audio.reverb", EffectHuman(name = R.string.aefx_name_reverb_legacy,
        keywords = "reverb antigo envio eco sala"))
    // Lote "AE 2026", segunda leva (AeEffects2026Extra.cpp).
    put("aurea.color.channel_combiner", EffectHuman(name = R.string.aefx_name_channel_combiner,
        keywords = "channel combiner combinador de canais canal hls yuv luminosidade matiz trocar canais"))
    put("aurea.color.broadcast_colors", EffectHuman(name = R.string.aefx_name_broadcast_colors,
        keywords = "broadcast colors cores de transmissao ntsc pal ire seguro tv legal"))
    put("aurea.color.video_limiter", EffectHuman(name = R.string.aefx_name_video_limiter,
        keywords = "video limiter limitador de video luma croma legal broadcast faixa"))
    put("aurea.color.color_neutralizer", EffectHuman(name = R.string.aefx_name_color_neutralizer,
        keywords = "color neutralizer neutralizador de cor dominante balanco de branco cast corrigir"))
    put("aurea.color.overbrights", EffectHuman(name = R.string.aefx_name_overbrights,
        keywords = "overbrights excessos de brilho hdr estourado acima de 1 clipping"))
    put("aurea.stylize.threads", EffectHuman(name = R.string.aefx_name_threads,
        keywords = "threads fios tecido trama costura pano tafeta"))
    put("aurea.distort.vr_rotate_sphere", EffectHuman(name = R.string.aefx_name_vr_rotate_sphere,
        keywords = "vr rotate sphere girar esfera 360 equiretangular panoramica"))
    put("aurea.distort.vr_sphere_to_plane", EffectHuman(name = R.string.aefx_name_vr_sphere_to_plane,
        keywords = "vr sphere to plane esfera para plano 360 reframe enquadrar"))
    put("aurea.distort.vr_plane_to_sphere", EffectHuman(name = R.string.aefx_name_vr_plane_to_sphere,
        keywords = "vr plane to sphere plano para esfera 360 equiretangular"))
    put("aurea.distort.vr_converter", EffectHuman(name = R.string.aefx_name_vr_converter,
        keywords = "vr converter conversor 360 cubo cubemap olho de peixe equiretangular"))
    put("aurea.color.compound_arithmetic", EffectHuman(name = R.string.aefx_name_compound_arithmetic,
        keywords = "compound arithmetic aritmetica composta camadas somar subtrair multiplicar diferenca"))
    put("aurea.color.calculations", EffectHuman(name = R.string.aefx_name_calculations,
        keywords = "calculations calculos canais mesclar camada cinza alfa"))
    put("aurea.stylize.glasses_3d", EffectHuman(name = R.string.aefx_name_glasses_3d,
        keywords = "3d glasses oculos 3d anaglifo estereo vermelho azul"))
    put("aurea.transition.glass_wipe", EffectHuman(name = R.string.aefx_name_glass_wipe,
        keywords = "glass wipe limpar com vidro transicao refracao gradiente"))
    put("aurea.transition.warpomatic", EffectHuman(name = R.string.aefx_name_warpomatic,
        keywords = "warpomatic transicao deformar morph reator"))
    put("aurea.stylize.environment", EffectHuman(name = R.string.aefx_name_environment,
        keywords = "environment ambiente reflexo espelho cromado mapa de ambiente"))
    put("aurea.audio.distortion", EffectHuman(name = R.string.aefx_name_distortion,
        keywords = "distortion distorcao saturacao overdrive fuzz valvula bitcrusher audio som"))
    put("aurea.audio.gate", EffectHuman(name = R.string.aefx_name_gate,
        keywords = "gate portao de ruido noise gate limiar silencio audio som"))
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
    Table[typeId]?.name?.let { english.getString(it) } ?: EnglishEffectNames[typeId]
        ?: com.aurea.aurea.effects.PluginTexts.name(typeId, "en") ?: engineName

private val EnglishEffectNames = mapOf(
    "aurea.light.scene_flare" to "3D Flare", "aurea.color.cube_lut" to "LUT (.cube)",
    "aurea.light.halation" to "Halation", "aurea.light.lens_flare" to "Lens Flare",
    "aurea.distort.ripple" to "Ripple", "aurea.distort.optics_compensation" to "Optics Compensation",
    "aurea.blur.box" to "Box Blur", "aurea.blur.directional" to "Directional Blur",
    "aurea.transition.linear_wipe" to "Linear Wipe", "aurea.transition.radial_wipe" to "Radial Wipe",
    "aurea.transition.block_dissolve" to "Block Dissolve",
).mapKeys { effectTypeId(it.key) }

/**
 * O Motion Tile e os efeitos com opção de LADRILHO / repetição de borda (tile,
 * repetir, espelhar): buscar "motion tile", "mosaico", "tile", "ladrilho"...
 * acha todos, nos 7 idiomas (`R.string.fx_search_tile`). Mesma lista no iOS
 * (`FxTileSearchEffects`, EffectsHuman.swift).
 */
internal val TileSearchEffects: Set<Int> = listOf(
    "aurea.stylize.motion_tile",
    "aurea.distort.shake",
    "aurea.distort.turbulence",
    "aurea.distort.wave_warp",
    "aurea.distort.warp",
    "aurea.distort.flow_pinch",
    "aurea.stylize.repeat_tile",
    "aurea.stylize.tiler",
    "aurea.stylize.hex_tile",
    "aurea.distort.displace_transform",
    "aurea.glitch.video_glitch",
    "aurea.distort.lens_gradient",
    "aurea.distort.displacement_map",
    "aurea.transform.offset",
    "aurea.repeat.grid",
    "aurea.distort.tile_shift",
    "aurea.distort.hex_tile_rotate",
    "aurea.distort.hex_tile_shift",
).map { effectTypeId(it) }.toSet()

/** Os termos de ladrilho deste efeito no idioma de `res` (vazio = não ladrilha). */
internal fun tileSearchTerms(typeId: Int, res: android.content.res.Resources): String =
    if (typeId in TileSearchEffects) res.getString(R.string.fx_search_tile) else ""

/** Texto onde a busca procura: nome humano, nome do motor, categoria e sinônimos. */
@Composable
internal fun effectSearchText(typeId: Int, engineName: String, category: String): String =
    normalizeSearch(listOf(effectDisplayName(typeId, engineName), Table[typeId]?.name?.let { stringResource(it) }.orEmpty(), engineName, category, Table[typeId]?.keywords.orEmpty(),
        tileSearchTerms(typeId, LocalContext.current.resources)).joinToString(" "))

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
    "desenho", "drawing" -> CupertinoGlyph.PencilOutline
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
    val finalScale = h?.scale ?: scale
    // Omega efeitos (P2-3): o efeito DECLAROU o passo da régua → as casas vêm do
    // passo (regra da referência: < 0,00009 → 5 … < 0,9 → 1), na unidade mostrada.
    val stepDecimals = if (s.stepDeclared && s.step > 0f && s.type != ParamType.INT) {
        com.aurea.aurea.ui.ds.FxParamMath.decimalsForStep(s.step * abs(finalScale))
    } else {
        null
    }
    return ParamDisplay(
        labelRes = h?.label,
        engineLabel = s.label,
        scale = finalScale,
        suffix = h?.suffix ?: suffix,
        decimals = h?.decimals ?: stepDecimals ?: decimals,
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
