// =============================================================================
//  Aurea / platform / ios / app / EffectsHuman.swift
//
//  A CAMADA HUMANA DOS EFEITOS — porte fiel de
//  `android/.../editor/panels/EffectsHuman.kt` + `effects/EffectCatalogMeta.kt`.
//
//  O motor não publica "principal/avançado" nem o nome de gente: publica o
//  `typeId`, que é o FNV-1a 32 da chave estável ("aurea.blur.gaussian"). A
//  tabela abaixo é indexada pelo MESMO hash, então trocar o nome exibido no
//  motor não a quebra; efeito fora da tabela cai na regra padrão (rótulo do
//  motor, primeiros N parâmetros).
//
//  ParamFlags cross the ObjC bridge. EffectsView applies the same hidden and
//  animatable flags as Android; this table controls labels and presentation.
// =============================================================================
import Foundation
import SwiftUI

// =============================================================================
// O índice da tabela: o mesmo hash do motor
// =============================================================================

/// FNV-1a 32 da chave — bits iguais ao `aurea::effect_type_id` (o `typeId`).
func fxEffectTypeId(_ key: String) -> UInt32 {
    var h: UInt32 = 0x811C_9DC5
    for byte in key.utf8 {
        h ^= UInt32(byte)
        h = h &* 16_777_619
    }
    return h
}

/// A busca não distingue acento nem caixa ("saturacao" acha "Saturação").
func fxNormalizeSearch(_ s: String) -> String {
    let folded = s.folding(options: [.diacriticInsensitive, .caseInsensitive], locale: Locale(identifier: "pt_BR"))
    return folded.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
}

// =============================================================================
// Como UM parâmetro aparece
// =============================================================================

/// Rótulo humano, fator de exibição (motor × [scale] = o que a pessoa vê),
/// sufixo ("%", "°", "px", "x") e casas FIXAS. Nulo = regra padrão.
struct FxParamHuman {
    var label: String? = nil
    var scale: Float? = nil
    var suffix: String? = nil
    var decimals: Int? = nil
}

/// Um efeito do catálogo em linguagem de gente. [principal] = índices dos
/// parâmetros mostrados de cara, NA ORDEM da lista; o resto vai para "Avançado".
/// Nulo = regra padrão (até 6 mostra tudo; acima, os 5 primeiros).
struct FxEffectHuman {
    var name: String? = nil
    var keywords: String = ""
    var principal: [Int]? = nil
    var params: [Int: FxParamHuman] = [:]
}

private let FX_DEFAULT_PRINCIPAL = 5
private let FX_SHOW_ALL_UP_TO = 6

private let FxPercent0 = FxParamHuman(scale: 100, suffix: "%", decimals: 0)

/// As 12 células da matriz (linha = canal de saída, coluna = de onde vem). Uma
/// frase por célula: "Vermelho do verde" não se monta por concatenação.
private let FxMatrixLabels = [
    "fx_matrix_rr", "fx_matrix_rg", "fx_matrix_rb", "fx_matrix_ro",
    "fx_matrix_gr", "fx_matrix_gg", "fx_matrix_gb", "fx_matrix_go",
    "fx_matrix_br", "fx_matrix_bg", "fx_matrix_bb", "fx_matrix_bo",
]

/// A TABELA. A ordem da declaração é a ordem do catálogo (não depende do
/// idioma — ordenar pelo nome traduzido reordenaria a lista ao trocar de
/// língua).
private let FxTable: [(key: String, effect: FxEffectHuman)] = [
    ("aurea.distort.bender", FxEffectHuman(name: "fx_name_bender", keywords: "bender bend curve curvar pontos", principal: [0, 1, 2, 3])),
    ("aurea.distort.bend", FxEffectHuman(name: "fx_name_bend", keywords: "bend fold dobra papel", principal: [0, 1, 2, 5])),
    ("aurea.distort.curl", FxEffectHuman(name: "fx_name_curl", keywords: "curl roll enrolar cilindro", principal: [0, 1, 2, 3, 6])),
    ("aurea.distort.page_turn", FxEffectHuman(name: "fx_name_page_turn", keywords: "page turn pagina papel", principal: [0, 1, 2, 3, 8])),
    ("aurea.distort.mesh_warp", FxEffectHuman(name: "fx_name_mesh_warp", keywords: "mesh warp malha deformacao grade bezier entortar", principal: [0, 1, 2])),
    ("aurea.distort.puppet", FxEffectHuman(name: "fx_name_puppet", keywords: "puppet fantoche pino pin marionete rig personagem deformar arap", principal: [0, 1, 2])),
    ("aurea.distort.liquid_glass", FxEffectHuman(name: "fx_name_liquid_glass", keywords: "liquid glass vidro liquido lente refracao glassmorphism", principal: [0, 1, 2, 3, 5])),
    ("aurea.transition.noise_dissolve", FxEffectHuman(name: "fx_name_noise_dissolve", keywords: "dissolve dissolver ruido queimar desintegrar", principal: [0, 1, 3, 8, 10])),
    ("aurea.stylize.eight_bit", FxEffectHuman(name: "fx_name_eight_bit", keywords: "8 bit 8bit pixel retro dither pontilhado game", principal: [0, 1, 2, 3])),
    ("aurea.glitch.jpeg_codec", FxEffectHuman(keywords: "jpeg glitch compression compressao dct quantization dano", principal: [0, 1, 2, 4, 5])),
    ("aurea.glitch.analog_signal", FxEffectHuman(keywords: "signal analog analogico ntsc pal vhs television", principal: [0, 1, 2, 3, 4])),
    ("aurea.light.deep_glow_2", FxEffectHuman(keywords: "deep glow 2 brilho bloom halo", principal: [0, 1, 2, 3, 13])),
    ("aurea.light.shadow_studio_3", FxEffectHuman(keywords: "shadow studio 3 sombra long radial inner", principal: [0, 1, 2, 3, 4])),
    ("aurea.generate.tracery", FxEffectHuman(keywords: "tracery color detection boxes rastreio cor conexoes", principal: [0, 1, 3, 7, 14], params: [2: FxParamHuman(decimals: 2)])),
    ("aurea.text.transform", FxEffectHuman(name: "text_transform_name", keywords: "text transform texto transformar letras palavras linhas intervalo fase", principal: Array(0...17))),
    ("aurea.text.animator", FxEffectHuman(name: "text_animator_name", keywords: "text animator animador texto letras palavras linhas cor blur desfoque random aleatorio", principal: Array(0...23))),
    ("aurea.stylize.bevel_alpha", FxEffectHuman(name: "bevel_alpha_name", keywords: "bevel alpha bisel alfa relevo texto borda", principal: Array(0...4))),
    ("aurea.color.gradient_map", FxEffectHuman(name: "gradient_map_name", keywords: "gradient map mapa degrade gradiente texto cor", principal: Array(0...4))),
    ("aurea.text3d.layout", FxEffectHuman(keywords: "letter rotation letras rotacao delay atraso cylinder twist random aleatorio",
        principal: [0, 1, 2, 12, 13, 9, 10])),
    // Formas 3D: o mesmo layout, parte a parte (mesmos índices do Text 3D Layout).
    ("aurea.shape3d.layout", FxEffectHuman(keywords: "shape parts partes forma 3d rotation rotacao delay atraso spread espalhar explodir twist random aleatorio",
        principal: [0, 1, 2, 12, 13, 9, 4])),
    ("aurea.transform", FxEffectHuman(
        name: "fx_name_transform",
        keywords: "transform mover posicao escala girar rotacao opacidade",
        principal: [1, 2, 3, 4],
        params: [
            0: FxParamHuman(label: "fx_pivo"),
            2: FxParamHuman(suffix: "%", decimals: 0),
        ])),
    ("aurea.color.exposure", FxEffectHuman(
        name: "fx_name_exposure",
        keywords: "exposure luz clarear escurecer gama gamma",
        principal: [0],
        params: [
            0: FxParamHuman(suffix: "", decimals: 2),
            1: FxParamHuman(label: "fx_compensacao", scale: 100, suffix: "%", decimals: 0),
            2: FxParamHuman(label: "fx_tons_medios", decimals: 2),
            3: FxParamHuman(label: "fx_aplicar_em"),
        ])),
    ("aurea.color.brightness_contrast", FxEffectHuman(
        name: "fx_name_brightness_contrast",
        keywords: "brightness contrast clarear",
        params: [0: FxParamHuman(decimals: 0), 1: FxParamHuman(decimals: 0)])),
    ("aurea.color.saturation", FxEffectHuman(
        name: "fx_name_saturation",
        keywords: "saturation cor viva preto e branco desbotar",
        params: [0: FxParamHuman(decimals: 0)])),
    ("aurea.color.tint", FxEffectHuman(
        name: "fx_name_tint",
        keywords: "tint colorir duotone",
        params: [
            0: FxParamHuman(label: "fx_cor_sombras"),
            1: FxParamHuman(label: "fx_cor_luzes"),
        ])),
    ("aurea.color.matrix", FxEffectHuman(
        name: "fx_name_color_matrix",
        keywords: "matriz de cor channel mixer rgb canais",
        principal: [0, 5, 10],
        params: (0..<12).reduce(into: [Int: FxParamHuman]()) { out, i in
            // Coluna 3 é o deslocamento (%); as outras, o ganho (×).
            out[i] = i % 4 == 3
                ? FxParamHuman(label: FxMatrixLabels[i], scale: 100, suffix: "%", decimals: 0)
                : FxParamHuman(label: FxMatrixLabels[i], suffix: "x", decimals: 2)
        })),
    ("aurea.color.levels", FxEffectHuman(
        name: "fx_name_levels",
        keywords: "levels niveis preto branco",
        principal: [0, 1, 2],
        params: [
            0: FxParamHuman(label: "fx_ponto_preto", decimals: 0),
            1: FxParamHuman(label: "fx_ponto_branco", decimals: 0),
            2: FxParamHuman(label: "fx_tons_medios", decimals: 2),
            3: FxParamHuman(decimals: 0),
            4: FxParamHuman(decimals: 0),
        ])),
    ("aurea.color.curves", FxEffectHuman(name: "fx_name_curves", keywords: "curves curva tons")),
    ("aurea.blur.gaussian", FxEffectHuman(
        name: "fx_name_gaussian_blur",
        keywords: "blur gaussian gaussiano borrar embacar",
        principal: [0, 1],
        params: [
            0: FxParamHuman(label: "fx_intensidade"),
            1: FxParamHuman(label: "fx_direcao"),
            2: FxParamHuman(label: "fx_esticar_bordas"),
        ])),
    ("aurea.blur.sharpen", FxEffectHuman(name: "fx_name_sharpen", keywords: "sharpen nitidez realcar detalhe")),
    ("aurea.light.glow", FxEffectHuman(
        name: "fx_name_glow",
        keywords: "glow brilho luz neon",
        principal: [2, 1, 0, 3],
        params: [
            0: FxParamHuman(label: "fx_limite"),
            2: FxParamHuman(suffix: "x", decimals: 1),
        ])),
    ("aurea.stylize.motion_tile", FxEffectHuman(
        name: "fx_name_motion_tile",
        keywords: "motion tile azulejos repetir ladrilho mosaico tijolo fase espelhar parede de video",
        principal: [10, 1, 2, 5, 7],
        params: [
            0: FxParamHuman(label: "fx_centro"),
            1: FxParamHuman(label: "fx_mt_largura_ladrilho"),
            2: FxParamHuman(label: "fx_mt_altura_ladrilho"),
            3: FxParamHuman(label: "fx_mt_largura_saida"),
            4: FxParamHuman(label: "fx_mt_altura_saida"),
            5: FxParamHuman(label: "fx_mt_espelhar_bordas"),
            7: FxParamHuman(label: "fx_fase"),
            8: FxParamHuman(label: "fx_mt_fase_horizontal"),
            10: FxParamHuman(label: "fx_escala", suffix: "%", decimals: 1),
        ])),
    ("aurea.key.luma", FxEffectHuman(
        name: "fx_name_luma_key",
        keywords: "chave de luma luma key remover preto branco",
        params: [
            0: FxParamHuman(label: "fx_remover"),
            1: FxParamHuman(label: "fx_limite"),
        ])),
    ("aurea.key.chroma", FxEffectHuman(
        name: "fx_name_chroma_key_advanced",
        keywords: "chave de croma chroma key fundo verde green screen remover cor",
        principal: [0, 1, 2],
        params: [
            0: FxParamHuman(label: "fx_cor_remover"),
            3: FxParamHuman(label: "fx_limpar_contorno"),
            9: FxParamHuman(label: "fx_pre_desfoque", suffix: "px", decimals: 1),
        ])),
    ("aurea.time.echo", FxEffectHuman(
        name: "fx_name_echo_trail",
        keywords: "echo eco rastro trail copias",
        principal: [0, 1, 2],
        params: [
            1: FxParamHuman(label: "fx_intervalo", suffix: "quadros", decimals: 1),
            2: FxParamHuman(label: "fx_desvanecer"),
            3: FxParamHuman(label: "fx_separar_cores", suffix: "quadros", decimals: 1),
        ])),
    // --- Fase 7.3: o pacote novo ---------------------------------------------
    ("aurea.color.invert", FxEffectHuman(name: "fx_name_invert", keywords: "inverter negativo inverter cor")),
    ("aurea.stylize.scanlines", FxEffectHuman(
        name: "fx_name_scanlines",
        keywords: "scanline varredura crt tv tubo linha",
        principal: [0, 1, 3, 4],
        params: [
            0: FxParamHuman(label: "fx_altura_linha", suffix: "px", decimals: 1),
            3: FxParamHuman(label: "fx_suavidade", decimals: 0),
            4: FxParamHuman(label: "fx_contraste", decimals: 0),
            6: FxParamHuman(label: "fx_canal"),
            8: FxParamHuman(label: "fx_rolagem", suffix: "px", decimals: 0),
        ])),
    ("aurea.stylize.grain", FxEffectHuman(
        name: "fx_name_grain",
        keywords: "grain grao filme ruido textura analogico",
        principal: [0, 1, 2, 4, 7],
        params: [
            0: FxParamHuman(label: "fx_intensidade", decimals: 0),
            1: FxParamHuman(label: "fx_tamanho_grao", suffix: "px", decimals: 1),
            2: FxParamHuman(label: "fx_grao_cor", decimals: 0),
            3: FxParamHuman(label: "fx_rugosidade", suffix: "x", decimals: 2),
            4: FxParamHuman(label: "fx_sombras", decimals: 0),
            5: FxParamHuman(label: "fx_luzes", decimals: 0),
            7: FxParamHuman(label: "fx_animado"),
            8: FxParamHuman(label: "fx_monocromatico"),
        ])),
    ("aurea.stylize.halftone", FxEffectHuman(
        name: "fx_name_halftone",
        keywords: "halftone meio tom reticula pontos impressao jornal pontilhado cmyk angulos",
        // Os principais são os do Color Halftone do AE: o raio máximo e os
        // quatro ângulos das retículas.
        principal: [12, 13, 14, 15, 16],
        params: [
            0: FxParamHuman(label: "fx_passo_grade", suffix: "px", decimals: 0),
            1: FxParamHuman(label: "fx_contraste", decimals: 0),
            2: FxParamHuman(label: "fx_angulo", suffix: "°", decimals: 0),
            3: FxParamHuman(label: "fx_suavidade", decimals: 0),
            4: FxParamHuman(label: "fx_rotacao_canal", suffix: "°", decimals: 0),
            5: FxParamHuman(label: "fx_padrao"),
            6: FxParamHuman(label: "fx_grades_separadas"),
            7: FxParamHuman(label: "fx_fundo_claro", decimals: 0),
            8: FxParamHuman(label: "fx_ganho_ponto", decimals: 0),
            10: FxParamHuman(label: "fx_centro_x", suffix: "px", decimals: 0),
            11: FxParamHuman(label: "fx_centro_y", suffix: "px", decimals: 0),
            12: FxParamHuman(label: "fx_raio_maximo", suffix: "px", decimals: 1),
            13: FxParamHuman(label: "fx_angulo_canal_1", suffix: "°", decimals: 0),
            14: FxParamHuman(label: "fx_angulo_canal_2", suffix: "°", decimals: 0),
            15: FxParamHuman(label: "fx_angulo_canal_3", suffix: "°", decimals: 0),
            16: FxParamHuman(label: "fx_angulo_canal_4", suffix: "°", decimals: 0),
        ])),
    ("aurea.stylize.minimax", FxEffectHuman(
        name: "fx_name_minimax",
        keywords: "minimax dilatar erodir morfologia matte afinar engrossar",
        principal: [1, 0, 4, 3],
        params: [
            0: FxParamHuman(label: "fx_raio", suffix: "px", decimals: 0),
            1: FxParamHuman(label: "fx_operacao"),
            2: FxParamHuman(label: "fx_forma"),
            3: FxParamHuman(label: "fx_intensidade", decimals: 0),
            4: FxParamHuman(label: "fx_comparar"),
        ])),
    ("aurea.blur.unsharp", FxEffectHuman(
        name: "fx_name_unsharp",
        keywords: "unsharp mascara de nitidez sharpen afiar detalhe",
        principal: [0, 1, 2],
        params: [
            0: FxParamHuman(label: "fx_intensidade", decimals: 0),
            1: FxParamHuman(label: "fx_raio", suffix: "px", decimals: 1),
            2: FxParamHuman(label: "fx_limiar", decimals: 0),
            4: FxParamHuman(label: "fx_mistura", decimals: 0),
        ])),
    ("aurea.blur.lens", FxEffectHuman(
        name: "fx_name_lens_blur",
        keywords: "lens blur desfoque de lente bokeh iris",
        principal: [0, 1, 2, 4],
        params: [
            0: FxParamHuman(label: "fx_raio", suffix: "px", decimals: 0),
            1: FxParamHuman(label: "fx_ganho_luzes", decimals: 0),
            2: FxParamHuman(label: "fx_lados_iris", decimals: 0),
            3: FxParamHuman(label: "fx_rotacao_iris", suffix: "°", decimals: 0),
            4: FxParamHuman(label: "fx_qualidade", decimals: 0),
            7: FxParamHuman(label: "fx_mistura", decimals: 0),
            8: FxParamHuman(label: "fx_curvatura_iris", decimals: 0),
            9: FxParamHuman(label: "fx_escala_x", decimals: 0),
            10: FxParamHuman(label: "fx_escala_y", decimals: 0),
        ])),
    ("aurea.distort.shake", FxEffectHuman(
        name: "fx_name_shake",
        keywords: "shake tremor camera balancar vibrar tremer",
        principal: [8, 2, 10, 5, 9, 11],
        params: [
            0: FxParamHuman(label: "fx_amplitude_x", suffix: "px", decimals: 0),
            1: FxParamHuman(label: "fx_amplitude_y", suffix: "px", decimals: 0),
            2: FxParamHuman(label: "fx_frequencia", suffix: "Hz", decimals: 2),
            4: FxParamHuman(label: "fx_eixos_separados"),
            5: FxParamHuman(label: "fx_rotacao", suffix: "°", decimals: 0),
            6: FxParamHuman(label: "fx_suavizacao", decimals: 0),
            7: FxParamHuman(label: "fx_mistura", decimals: 0),
        ])),
    ("aurea.distort.turbulence", FxEffectHuman(
        name: "fx_name_turbulence",
        keywords: "turbulencia displacement deslocamento ruido organico fumaca",
        principal: [12, 0, 1, 2, 4],
        params: [
            0: FxParamHuman(label: "fx_intensidade", suffix: "px", decimals: 0),
            1: FxParamHuman(label: "fx_tamanho_ruido", suffix: "px", decimals: 0),
            2: FxParamHuman(label: "fx_complexidade", suffix: "oitavas", decimals: 0),
            3: FxParamHuman(label: "fx_evolucao", suffix: "°", decimals: 1),
            4: FxParamHuman(label: "fx_deslocamento_x", suffix: "px", decimals: 0),
            5: FxParamHuman(label: "fx_deslocamento_y", suffix: "px", decimals: 0),
            8: FxParamHuman(label: "fx_bordas"),
            9: FxParamHuman(label: "fx_girar_deslocamento", suffix: "°", decimals: 0),
        ])),
    ("aurea.distort.wave_warp", FxEffectHuman(
        name: "fx_name_wave_warp",
        keywords: "wave warp onda ondular senoide agua",
        principal: [0, 1, 2, 4, 9],
        params: [
            0: FxParamHuman(label: "fx_altura_onda", suffix: "px", decimals: 0),
            1: FxParamHuman(label: "fx_largura_onda", suffix: "px", decimals: 0),
            2: FxParamHuman(label: "fx_velocidade", suffix: "px/q", decimals: 0),
            3: FxParamHuman(label: "fx_fase", suffix: "°", decimals: 0),
            4: FxParamHuman(label: "fx_direcao"),
            5: FxParamHuman(label: "fx_onda_quadrada"),
            6: FxParamHuman(label: "fx_bordas"),
            7: FxParamHuman(label: "fx_travar_nas_bordas"),
        ])),
    ("aurea.distort.warp", FxEffectHuman(
        name: "fx_name_warp",
        keywords: "warp lente distorcer empurrar puxar torcer esfera canto bulge pinch twist",
        principal: [0, 1, 2, 3],
        params: [
            0: FxParamHuman(label: "fx_modo"),
            1: FxParamHuman(label: "fx_intensidade", suffix: "px", decimals: 0),
            2: FxParamHuman(label: "fx_raio", suffix: "px", decimals: 0),
            3: FxParamHuman(label: "fx_centro"),
            4: FxParamHuman(label: "fx_bordas"),
            5: FxParamHuman(label: "fx_mistura", decimals: 0),
            6: FxParamHuman(label: "fx_luz_esfera", decimals: 0),
        ])),
    ("aurea.distort.ripple_dissolve", FxEffectHuman(
        name: "fx_name_ripple_dissolve",
        keywords: "ripple dissolve ondulacao dissolver transicao circular agua",
        principal: [0, 1, 2, 3],
        params: [
            0: FxParamHuman(label: "fx_progresso", decimals: 0),
            1: FxParamHuman(label: "fx_ondulacao", suffix: "px", decimals: 0),
            2: FxParamHuman(label: "fx_comprimento_onda", suffix: "px", decimals: 0),
            3: FxParamHuman(label: "fx_suavidade_borda", decimals: 0),
            4: FxParamHuman(label: "fx_centro"),
            5: FxParamHuman(label: "fx_velocidade_onda", suffix: "x", decimals: 2),
            7: FxParamHuman(label: "fx_distorcer_imagem_junto"),
            8: FxParamHuman(label: "fx_fora_dentro"),
        ])),
    ("aurea.light.deep_glow", FxEffectHuman(
        name: "fx_name_deep_glow",
        keywords: "deep glow brilho profundo halo neon luz bloom",
        principal: [2, 13, 0, 14, 4],
        params: [
            0: FxParamHuman(label: "fx_limite", decimals: 0),
            1: FxParamHuman(label: "fx_raio_nucleo", suffix: "px", decimals: 0),
            2: FxParamHuman(label: "fx_raio_halo", suffix: "px", decimals: 0),
            3: FxParamHuman(label: "fx_forca_nucleo", suffix: "x", decimals: 2),
            4: FxParamHuman(label: "fx_forca_halo", suffix: "x", decimals: 2),
            5: FxParamHuman(label: "fx_cor_brilho"),
            6: FxParamHuman(label: "fx_preservar_sombras"),
            7: FxParamHuman(label: "fx_halo_tela"),
            10: FxParamHuman(label: "fx_so_brilho"),
            11: FxParamHuman(label: "fx_estouro", decimals: 0),
        ])),
    ("aurea.light.rays", FxEffectHuman(
        name: "fx_name_rays",
        keywords: "rays raios de luz god rays sol volumetrico spread",
        principal: [0, 1, 2, 3, 4],
        params: [
            0: FxParamHuman(label: "fx_intensidade", suffix: "x", decimals: 2),
            1: FxParamHuman(label: "fx_comprimento", decimals: 0),
            2: FxParamHuman(label: "fx_limite", decimals: 0),
            3: FxParamHuman(label: "fx_decaimento", decimals: 0),
            4: FxParamHuman(label: "fx_ponto_luz"),
            5: FxParamHuman(label: "fx_amostras", decimals: 0),
            7: FxParamHuman(label: "fx_guardar_cor_fonte"),
            8: FxParamHuman(label: "fx_girar_cor", suffix: "°", decimals: 0),
        ])),
    ("aurea.light.sweep", FxEffectHuman(
        name: "fx_name_light_sweep",
        keywords: "light sweep faixa de luz brilho varredura reflexo",
        principal: [9, 1, 2, 4, 5, 10],
        params: [:])),
    ("aurea.color.colorama", FxEffectHuman(
        name: "fx_name_colorama",
        keywords: "colorama remapeamento de cor arco-iris psicodelico mapa de cor",
        principal: [0, 1, 2, 4, 5],
        params: [
            0: FxParamHuman(label: "fx_fase", suffix: "voltas", decimals: 2),
            1: FxParamHuman(label: "fx_ciclos", suffix: "x", decimals: 2),
            2: FxParamHuman(label: "fx_saturacao", decimals: 0),
            3: FxParamHuman(label: "fx_brilho", decimals: 0),
            4: FxParamHuman(label: "fx_entrada"),
            5: FxParamHuman(label: "fx_mistura", decimals: 0),
            6: FxParamHuman(label: "fx_inverter_arco_iris"),
            7: FxParamHuman(label: "fx_peso_croma", decimals: 0),
            9: FxParamHuman(label: "fx_ganho", suffix: "x", decimals: 2),
        ])),
    ("aurea.stylize.omino_diffusion", FxEffectHuman(
        name: "fx_name_omino_diffusion", keywords: "omino omine diffusion difusao glitch paleta faixas",
        principal: [0, 1, 2, 5, 6], params: [
            0: FxParamHuman(label: "fx_intensidade", decimals: 0),
            1: FxParamHuman(label: "fx_diffusion_weight", decimals: 2),
            2: FxParamHuman(label: "fx_angulo", decimals: 0),
            3: FxParamHuman(label: "fx_diffusion_reach", decimals: 0),
            4: FxParamHuman(label: "fx_amostras", decimals: 0),
            5: FxParamHuman(label: "fx_diffusion_stripes", decimals: 1),
            6: FxParamHuman(label: "fx_diffusion_palette", decimals: 0),
            7: FxParamHuman(label: "fx_diffusion_falloff", decimals: 0),
        ])),
    ("aurea.stylize.pixel_sort", FxEffectHuman(
        name: "fx_name_pixel_sort",
        keywords: "pixel sort ordenar pixels derreter listras glitch sort",
        principal: [0, 1, 2, 4],
        params: [
            0: FxParamHuman(label: "fx_limiar_baixo", decimals: 0),
            1: FxParamHuman(label: "fx_limiar_alto", decimals: 0),
            2: FxParamHuman(label: "fx_comprimento", decimals: 0),
            3: FxParamHuman(label: "fx_aleatoriedade", decimals: 0),
            4: FxParamHuman(label: "fx_direcao"),
            5: FxParamHuman(label: "fx_sentido_inverso"),
            6: FxParamHuman(label: "fx_ordenar"),
            8: FxParamHuman(label: "fx_faixa_tom"),
            9: FxParamHuman(label: "fx_passo", suffix: "px", decimals: 0),
        ])),
    ("aurea.stylize.film_damage", FxEffectHuman(
        name: "fx_name_film_damage",
        keywords: "film damage dano de filme poeira riscos arranhao projetor pelicula",
        principal: [0, 1, 2, 3, 4],
        params: [
            0: FxParamHuman(label: "fx_poeira", decimals: 0),
            1: FxParamHuman(label: "fx_riscos", decimals: 0),
            2: FxParamHuman(label: "fx_piscar", decimals: 0),
            3: FxParamHuman(label: "fx_balanco_porta", suffix: "px", decimals: 1),
            4: FxParamHuman(label: "fx_queimado", decimals: 0),
            5: FxParamHuman(label: "fx_emenda"),
            7: FxParamHuman(label: "fx_mistura", decimals: 0),
            8: FxParamHuman(label: "fx_tamanho_poeira", suffix: "px", decimals: 1),
            9: FxParamHuman(label: "fx_comprimento_risco", decimals: 0),
            11: FxParamHuman(label: "fx_calor_queimado", decimals: 0),
        ])),
    ("aurea.stylize.jpeg_damage", FxEffectHuman(
        name: "fx_name_jpeg_damage",
        keywords: "jpeg damage dano compressao artefato bloco qualidade",
        principal: [0, 1, 2, 3],
        params: [
            0: FxParamHuman(label: "fx_qualidade", decimals: 0),
            1: FxParamHuman(label: "fx_blocos", decimals: 0),
            2: FxParamHuman(label: "fx_anelamento", decimals: 0),
            3: FxParamHuman(label: "fx_dano_cor", decimals: 0),
            4: FxParamHuman(label: "fx_tamanho_bloco", suffix: "px", decimals: 0),
            5: FxParamHuman(label: "fx_suavizar_bloco", decimals: 0),
            7: FxParamHuman(label: "fx_mistura", decimals: 0),
            8: FxParamHuman(label: "fx_blocos_corrompidos", decimals: 0),
        ])),
    ("aurea.stylize.holomatrix", FxEffectHuman(
        name: "fx_name_holo_matrix",
        keywords: "holo matrix holograma projecao grade tecnologica scanner",
        principal: [0, 1, 3, 4, 6],
        params: [
            0: FxParamHuman(label: "fx_mistura_cor", decimals: 0),
            1: FxParamHuman(label: "fx_grade", decimals: 0),
            2: FxParamHuman(label: "fx_celulas_grade", decimals: 0),
            3: FxParamHuman(label: "fx_brilho_bordas", decimals: 0),
            4: FxParamHuman(label: "fx_posicao_varredura", decimals: 0),
            5: FxParamHuman(label: "fx_largura_varredura", decimals: 0),
            6: FxParamHuman(label: "fx_interferencia", decimals: 0),
            7: FxParamHuman(label: "fx_velocidade_varredura", suffix: "x", decimals: 2),
            9: FxParamHuman(label: "fx_fundo_aceso", decimals: 0),
            11: FxParamHuman(label: "fx_mistura", decimals: 0),
        ])),
    ("aurea.glitch.glitchify", FxEffectHuman(
        name: "fx_name_glitchify",
        keywords: "glitchify glitch defeito digital rasgo bloco corrupcao",
        principal: [17, 18, 1, 3, 13, 34],
        params: [
            0: FxParamHuman(label: "fx_altura_faixa", suffix: "px", decimals: 0),
            1: FxParamHuman(label: "fx_deslocamento", suffix: "px", decimals: 0),
            2: FxParamHuman(label: "fx_picos", decimals: 0),
            3: FxParamHuman(label: "fx_separacao_rgb", suffix: "px", decimals: 0),
            4: FxParamHuman(label: "fx_frequencia", suffix: "quadros", decimals: 1),
            6: FxParamHuman(label: "fx_travar_quadro"),
            7: FxParamHuman(label: "fx_mistura", decimals: 0),
            8: FxParamHuman(label: "fx_blocos_verticais"),
            9: FxParamHuman(label: "fx_corrupcao_cor", decimals: 0),
        ])),
    ("aurea.glitch.vhs", FxEffectHuman(
        name: "fx_name_vhs",
        keywords: "vhs fita cassette videocassete tracking dropouts analogico videotape",
        principal: [0, 1, 2, 3, 4, 5, 6],
        params: [
            0: FxParamHuman(label: "fx_borrado_luma", suffix: "px", decimals: 1),
            1: FxParamHuman(label: "fx_alargar_cor", suffix: "px", decimals: 1),
            2: FxParamHuman(label: "fx_instabilidade", suffix: "px", decimals: 0),
            3: FxParamHuman(label: "fx_perdas_fita", decimals: 0),
            4: FxParamHuman(label: "fx_varredura_cabecote", decimals: 0),
            5: FxParamHuman(label: "fx_ruido", decimals: 0),
            6: FxParamHuman(label: "fx_degradacao_cor", decimals: 0),
            7: FxParamHuman(label: "fx_sangramento", decimals: 0),
            10: FxParamHuman(label: "fx_velocidade_instabilidade", suffix: "x", decimals: 2),
            11: FxParamHuman(label: "fx_altura_perda", suffix: "px", decimals: 1),
            12: FxParamHuman(label: "fx_mistura", decimals: 0),
        ])),
    ("aurea.glitch.uni_vhs", FxEffectHuman(
        name: "fx_name_uni_vhs",
        keywords: "vhs fita estilizado anos 80 retro neon chroma warp",
        principal: [0, 1, 2, 3, 6],
        params: [
            0: FxParamHuman(label: "fx_separacao_rgb", suffix: "px", decimals: 0),
            1: FxParamHuman(label: "fx_ondulacao", suffix: "px", decimals: 0),
            2: FxParamHuman(label: "fx_brilho_sujo", decimals: 0),
            3: FxParamHuman(label: "fx_vinheta", decimals: 0),
            4: FxParamHuman(label: "fx_varredura", decimals: 0),
            5: FxParamHuman(label: "fx_ruido", decimals: 0),
            6: FxParamHuman(label: "fx_saturacao", decimals: 0),
            7: FxParamHuman(label: "fx_mistura", decimals: 0),
            9: FxParamHuman(label: "fx_frequencia_ondulacao", suffix: "x", decimals: 1),
        ])),
    ("aurea.glitch.signal", FxEffectHuman(
        name: "fx_name_signal",
        keywords: "signal sinal interferencia transmissao banda sincronia chiado",
        principal: [0, 1, 2, 3, 4],
        params: [
            0: FxParamHuman(label: "fx_bandas_perdidas", decimals: 0),
            1: FxParamHuman(label: "fx_deslocamento", suffix: "px", decimals: 0),
            2: FxParamHuman(label: "fx_deriva", suffix: "px/q", decimals: 2),
            3: FxParamHuman(label: "fx_altura_banda", suffix: "px", decimals: 0),
            4: FxParamHuman(label: "fx_ruido_sinal", decimals: 0),
            6: FxParamHuman(label: "fx_frequencia", suffix: "quadros", decimals: 1),
            7: FxParamHuman(label: "fx_mistura", decimals: 0),
            8: FxParamHuman(label: "fx_perder_sincronia"),
            9: FxParamHuman(label: "fx_separacao_cor", suffix: "px", decimals: 0),
        ])),
    ("aurea.glitch.cross", FxEffectHuman(
        name: "fx_name_cross_glitch",
        keywords: "cross glitch cruz transicao varredura rasgo",
        principal: [0, 1, 2, 4],
        params: [
            0: FxParamHuman(label: "fx_progresso", decimals: 0),
            1: FxParamHuman(label: "fx_largura_faixa", decimals: 0),
            2: FxParamHuman(label: "fx_deslocamento", suffix: "px", decimals: 0),
            3: FxParamHuman(label: "fx_ruido_fundo", decimals: 0),
            4: FxParamHuman(label: "fx_separacao_rgb", suffix: "px", decimals: 0),
            6: FxParamHuman(label: "fx_frequencia", suffix: "quadros", decimals: 1),
            7: FxParamHuman(label: "fx_mistura", decimals: 0),
            8: FxParamHuman(label: "fx_faixa_vertical"),
            9: FxParamHuman(label: "fx_faixa_horizontal"),
        ])),
    ("aurea.time.posterize", FxEffectHuman(
        name: "fx_name_posterize_time",
        keywords: "posterize time posterizar tempo taxa quadros stop motion animacao",
        principal: [0, 1],
        params: [
            0: FxParamHuman(label: "fx_quadros_segundo", suffix: "fps", decimals: 1),
            1: FxParamHuman(label: "fx_segurar_quadro"),
        ])),
    ("aurea.time.warp_rgb", FxEffectHuman(
        name: "fx_name_time_warp_rgb",
        keywords: "rgb no tempo time warp separar canais atrasar cor chromatic",
        principal: [0, 1, 2, 3, 4],
        params: [
            0: FxParamHuman(label: "fx_vermelho_c031", suffix: "quadros", decimals: 1),
            1: FxParamHuman(label: "fx_verde_14e6", suffix: "quadros", decimals: 1),
            2: FxParamHuman(label: "fx_azul_582d", suffix: "quadros", decimals: 1),
            3: FxParamHuman(label: "fx_unidade"),
            4: FxParamHuman(label: "fx_intensidade", decimals: 0),
            5: FxParamHuman(label: "fx_prender_nas_pontas"),
        ])),
    ("aurea.time.motion_detect", FxEffectHuman(
        name: "fx_name_time_motion_detect",
        keywords: "motion detect detectar movimento diferenca quadro anterior mudou mexeu",
        principal: [0, 1, 2, 3, 4, 5],
        params: [
            0: FxParamHuman(label: "core2_fx_motion_delay", decimals: 0),
            1: FxParamHuman(label: "fx_brilho", decimals: 2),
            2: FxParamHuman(label: "core2_fx_offset_darks", decimals: 2),
            3: FxParamHuman(label: "fx_saturacao", decimals: 2),
            4: FxParamHuman(label: "core2_fx_motion_mode"),
            5: FxParamHuman(label: "fx_mistura", decimals: 0),
        ])),
    ("aurea.stylize.crt_emulator", FxEffectHuman(
        name: "fx_name_crt_emulator",
        keywords: "crt emulator emulador tv tubo televisao retro scanline varredura fosforo curvatura tela antiga",
        principal: [0, 1, 3, 4, 8, 11, 16],
        params: [
            0: FxParamHuman(label: "fx3_curvatura", decimals: 0),
            1: FxParamHuman(label: "fx3_linhas_intensidade", decimals: 0),
            2: FxParamHuman(label: "fx3_linhas_densidade", decimals: 0),
            3: FxParamHuman(label: "fx3_mascara"),
            4: FxParamHuman(label: "fx3_mascara_intensidade", decimals: 0),
            5: FxParamHuman(label: "fx3_mascara_tamanho", suffix: "px", decimals: 1),
            6: FxParamHuman(label: "fx_vinheta", decimals: 0),
            7: FxParamHuman(label: "fx3_convergencia", suffix: "px", decimals: 1),
            8: FxParamHuman(label: "fx3_brilho_bloom", decimals: 0),
            9: FxParamHuman(label: "fx3_raio_brilho", suffix: "px", decimals: 1),
            10: FxParamHuman(label: "fx3_cintilacao", decimals: 0),
            11: FxParamHuman(label: "fx3_faixa_rolando", decimals: 0),
            12: FxParamHuman(label: "fx3_velocidade_faixa", decimals: 2),
            13: FxParamHuman(label: "fx_ruido", decimals: 0),
            14: FxParamHuman(label: "fx3_brilho_geral", decimals: 0),
            15: FxParamHuman(label: "fx_contraste", decimals: 0),
            16: FxParamHuman(label: "fx_mistura", decimals: 0),
        ])),
    ("aurea.glitch.dissolve_shake", FxEffectHuman(
        name: "fx_name_dissolve_shake",
        keywords: "dissolve shake tremor dissolvente tremer quebrar fragmentos glitch desintegrar ruido",
        principal: [0, 1, 2, 3, 4, 10],
        params: [
            0: FxParamHuman(label: "fx_amplitude", suffix: "px", decimals: 0),
            1: FxParamHuman(label: "fx_frequencia", suffix: "Hz", decimals: 1),
            2: FxParamHuman(label: "fx3_dissolucao", decimals: 0),
            3: FxParamHuman(label: "fx3_tamanho_fragmento", suffix: "px", decimals: 0),
            4: FxParamHuman(label: "fx_dispersao", suffix: "px", decimals: 0),
            5: FxParamHuman(label: "fx_aleatoriedade", decimals: 0),
            6: FxParamHuman(label: "fx3_transparencia_fragmentos", decimals: 0),
            7: FxParamHuman(label: "fx3_velocidade_evolucao", decimals: 1),
            8: FxParamHuman(label: "fx3_eixos"),
            9: FxParamHuman(label: "fx_semente", decimals: 0),
            10: FxParamHuman(label: "fx_mistura", decimals: 0),
        ])),
    ("aurea.glitch.datamosh", FxEffectHuman(
        name: "fx_name_datamosh",
        keywords: "datamosh data mosh glitch compressao codec blocos macrobloco arrastar derreter i-frame p-frame pixel bleed",
        principal: [0, 1, 2, 3, 4, 5, 6],
        params: [
            0: FxParamHuman(label: "fx_intensidade", decimals: 0),
            1: FxParamHuman(label: "fx_tamanho_bloco", suffix: "px", decimals: 0),
            2: FxParamHuman(label: "fx_datamosh_hold_frames", decimals: 0),
            3: FxParamHuman(label: "fx_datamosh_drag", decimals: 2),
            4: FxParamHuman(label: "fx_datamosh_corruption", decimals: 0),
            5: FxParamHuman(label: "fx_datamosh_color_bleed", decimals: 0),
            6: FxParamHuman(label: "fx_semente", decimals: 0),
        ])),
    ("aurea.distort.displacement_map", FxEffectHuman(
        name: "fx_name_displacement_map",
        keywords: "displacement map mapa de deslocamento deslocar camada mapa distorcer canal",
        principal: [0, 1, 2, 3, 4, 8],
        params: [
            0: FxParamHuman(label: "fx3_camada_mapa"),
            1: FxParamHuman(label: "fx3_canal_horizontal"),
            2: FxParamHuman(label: "fx3_canal_vertical"),
            3: FxParamHuman(label: "fx3_desloc_h_max", suffix: "px", decimals: 1),
            4: FxParamHuman(label: "fx3_desloc_v_max", suffix: "px", decimals: 1),
            5: FxParamHuman(label: "fx3_comportamento_mapa"),
            6: FxParamHuman(label: "fx_bordas"),
            7: FxParamHuman(label: "fx3_expandir_saida"),
            8: FxParamHuman(label: "fx_mistura", decimals: 0),
        ])),
    ("aurea.time.remap", FxEffectHuman(
        name: "fx_name_time_remap",
        // O parâmetro Tempo É a curva de remapeamento da camada. O "Manter o tom
        // do áudio" do AE não existe aqui porque o motor não faz time-stretch:
        // um interruptor que não faz nada seria pior que a linha que falta.
        keywords: "remapear tempo time remap curva velocidade camera lenta rampa",
        principal: [0, 1],
        params: [
            0: FxParamHuman(label: "fx_tempo", suffix: "s", decimals: 2),
            1: FxParamHuman(label: "fx_interpolacao_do_tempo"),
        ])),
    ("aurea.control.slider", FxEffectHuman(name: "fx_name_slider_control", keywords: "expressao slider controle", params: [0: FxParamHuman(decimals: 1)])),
    ("aurea.control.angle", FxEffectHuman(name: "fx_name_angle_control", keywords: "expressao angulo controle")),
    ("aurea.control.checkbox", FxEffectHuman(name: "fx_name_checkbox_control", keywords: "expressao caixa controle")),
    ("aurea.control.color", FxEffectHuman(name: "fx_name_color_control", keywords: "expressao cor controle")),
    ("aurea.control.point", FxEffectHuman(name: "fx_name_point_control", keywords: "expressao ponto controle")),
    // --- Pacote de paridade: movimento, transições por forma e acabamento ---
    ("aurea.motion.oscillate.cycles", FxEffectHuman(
        name: "fx_name_oscillate", keywords: "oscilar oscillate seno triangulo fase orbita",
        principal: [0, 1, 2, 3, 4, 5],
        params: [2: FxParamHuman(suffix: "Hz", decimals: 2), 3: FxParamHuman(suffix: "px", decimals: 0),
                 5: FxParamHuman(decimals: 2)])),
    ("aurea.motion.oscillate", FxEffectHuman(
        name: "fx_name_oscillate",
        keywords: "oscilar oscillate vai e vem pendular onda seno movimento decaimento balanco",
        principal: [1, 0, 2, 8, 3, 4],
        params: [
            0: FxParamHuman(label: "fx_direcao"),
            1: FxParamHuman(label: "fx_amplitude", suffix: "px", decimals: 0),
            2: FxParamHuman(label: "fx_frequencia", suffix: "Hz", decimals: 2),
            3: FxParamHuman(label: "fx_fase"),
            4: FxParamHuman(label: "fx_rotacao"),
            5: FxParamHuman(label: "fx_pulso_escala", decimals: 0),
            6: FxParamHuman(label: "fx_forma_onda"),
            7: FxParamHuman(label: "fx_semente"),
            8: FxParamHuman(label: "fx_decaimento", suffix: "1/s", decimals: 2),
            9: FxParamHuman(label: "fx_pivo"),
        ])),
    ("aurea.motion.swing", FxEffectHuman(
        name: "fx_name_swing",
        keywords: "balancar swing pendulo pendulum pivo girar oscilar",
        principal: [0, 1, 2, 3, 4],
        params: [
            0: FxParamHuman(label: "fx_angulo"),
            1: FxParamHuman(label: "fx_frequencia", suffix: "Hz", decimals: 2),
            2: FxParamHuman(label: "fx_pivo"),
            3: FxParamHuman(label: "fx_fase"),
            4: FxParamHuman(label: "fx_decaimento", suffix: "1/s", decimals: 2),
        ])),
    ("aurea.motion.wiggle", FxEffectHuman(
        name: "fx_name_wiggle",
        keywords: "wiggle agitar aleatorio tremer mexer posicao rotacao escala",
        principal: [0, 1, 2, 3, 4, 5],
        params: [
            0: FxParamHuman(label: "fx_frequencia", suffix: "Hz", decimals: 2),
            1: FxParamHuman(label: "fx_posicao_x", suffix: "px", decimals: 0),
            2: FxParamHuman(label: "fx_posicao_y", suffix: "px", decimals: 0),
            3: FxParamHuman(label: "fx_rotacao"),
            4: FxParamHuman(label: "fx_escala", decimals: 0),
            5: FxParamHuman(label: "fx_intensidade", decimals: 0),
            6: FxParamHuman(label: "fx_oitavas"),
            7: FxParamHuman(label: "fx_segurar_saltos"),
            8: FxParamHuman(label: "fx_semente"),
            9: FxParamHuman(label: "fx_pivo"),
        ])),
    ("aurea.motion.twitch", FxEffectHuman(
        name: "fx_name_twitch",
        keywords: "tremor trancos twitch shake tremer impacto camera na mao glitch sacudir",
        principal: [0, 1, 2, 3, 4, 5],
        params: [
            0: FxParamHuman(label: "fx_frequencia", suffix: "Hz", decimals: 1),
            1: FxParamHuman(label: "fx_intensidade", decimals: 1),
            2: FxParamHuman(label: "fx_rotacao"),
            3: FxParamHuman(label: "fx_escala", decimals: 1),
            4: FxParamHuman(label: "fx_suavizar", decimals: 0),
            5: FxParamHuman(label: "fx_decaimento", suffix: "1/s", decimals: 2),
            6: FxParamHuman(label: "fx_semente"),
        ])),
    ("aurea.transition.iris_wipe", FxEffectHuman(
        name: "fx_name_iris_wipe",
        keywords: "iris wipe circulo poligono transicao revelar abrir fechar",
        principal: [0, 1, 3, 5],
        params: [
            0: FxParamHuman(label: "fx_conclusao", decimals: 0),
            1: FxParamHuman(label: "fx_suavidade", decimals: 0),
            2: FxParamHuman(label: "fx_sentido_inverso"),
            3: FxParamHuman(label: "fx_centro"),
            4: FxParamHuman(label: "fx_rotacao"),
            5: FxParamHuman(label: "fx_lados_iris"),
        ])),
    ("aurea.transition.box_wipe", FxEffectHuman(
        name: "fx_name_box_wipe",
        keywords: "caixa box wipe retangulo transicao revelar",
        principal: [0, 1, 3, 4],
        params: [
            0: FxParamHuman(label: "fx_conclusao", decimals: 0),
            1: FxParamHuman(label: "fx_suavidade", decimals: 0),
            2: FxParamHuman(label: "fx_sentido_inverso"),
            3: FxParamHuman(label: "fx_centro"),
            4: FxParamHuman(label: "fx_rotacao"),
        ])),
    ("aurea.transition.venetian_blinds", FxEffectHuman(
        name: "fx_name_venetian_blinds",
        keywords: "persianas venetian blinds faixas listras transicao",
        principal: [0, 4, 3, 1],
        params: [
            0: FxParamHuman(label: "fx_conclusao", decimals: 0),
            1: FxParamHuman(label: "fx_suavidade", decimals: 0),
            2: FxParamHuman(label: "fx_sentido_inverso"),
            3: FxParamHuman(label: "fx_direcao"),
            4: FxParamHuman(label: "fx_faixas", decimals: 0),
        ])),
    ("aurea.blur.radial", FxEffectHuman(
        name: "fx_name_radial_blur",
        keywords: "desfoque radial radial blur zoom blur spin giro rotacional velocidade",
        principal: [0, 1, 2, 3, 4],
        params: [
            0: FxParamHuman(label: "fx_tipo"),
            1: FxParamHuman(label: "fx_intensidade", decimals: 0),
            2: FxParamHuman(label: "fx_centro"),
            3: FxParamHuman(label: "fx_qualidade"),
            4: FxParamHuman(label: "fx_mistura", decimals: 0),
        ])),
    ("aurea.distort.mirror", FxEffectHuman(
        name: "fx_name_mirror",
        keywords: "espelho mirror refletir simetria reflexo",
        params: [
            0: FxParamHuman(label: "fx_centro"),
            1: FxParamHuman(label: "fx_angulo"),
            2: FxParamHuman(label: "fx_trocar_lado"),
        ])),
    ("aurea.transform.crop", FxEffectHuman(
        name: "fx_name_crop_edges",
        keywords: "cortar crop recortar bordas margens aparar",
        params: [
            0: FxParamHuman(label: "fx_esquerda", decimals: 0),
            1: FxParamHuman(label: "fx_topo", decimals: 0),
            2: FxParamHuman(label: "fx_direita", decimals: 0),
            3: FxParamHuman(label: "fx_base", decimals: 0),
            4: FxParamHuman(label: "fx_suavidade_borda", suffix: "px", decimals: 0),
        ])),
    ("aurea.stylize.vignette", FxEffectHuman(
        name: "fx_name_vignette",
        keywords: "vinheta vignette escurecer bordas cantos",
        params: [
            0: FxParamHuman(label: "fx_intensidade", decimals: 0),
            1: FxParamHuman(label: "fx_tamanho", decimals: 0),
            2: FxParamHuman(label: "fx_suavidade", decimals: 0),
            3: FxParamHuman(label: "fx_arredondamento", decimals: 0),
            4: FxParamHuman(label: "fx_centro"),
            5: FxParamHuman(label: "fx_cor"),
        ])),
    ("aurea.stylize.mosaic", FxEffectHuman(
        name: "fx_name_mosaic",
        keywords: "mosaico mosaic pixelar pixelate led painel celulas pixel parede de led matriz de pontos dot matrix",
        principal: [6, 0, 1, 2, 3, 5],
        params: [
            0: FxParamHuman(label: "fx_tamanho_celula", suffix: "px", decimals: 0),
            1: FxParamHuman(label: "fx_vao_celulas", decimals: 0),
            2: FxParamHuman(label: "fx_celulas_redondas"),
            3: FxParamHuman(label: "fx_sombreado", decimals: 0),
            4: FxParamHuman(label: "fx_vinheta_celula", decimals: 0),
            5: FxParamHuman(label: "fx_cor_fundo"),
            6: FxParamHuman(label: "fx_estilo"),
            7: FxParamHuman(label: "fx_vinheta", decimals: 0),
        ])),
    ("aurea.stylize.find_edges", FxEffectHuman(
        name: "fx_name_find_edges",
        keywords: "detectar bordas find edges contorno sobel desenho lapis",
        params: [
            0: FxParamHuman(label: "fx_intensidade", decimals: 0),
            1: FxParamHuman(label: "fx_largura", suffix: "px", decimals: 1),
            2: FxParamHuman(label: "fx_inverter"),
            3: FxParamHuman(label: "fx_mistura", decimals: 0),
        ])),
    // --- Pacote do editor antigo: o que só ele tinha ---
    ("aurea.color.fill", FxEffectHuman(
        name: "fx_name_fill",
        keywords: "preencher fill cor chapada tinta solido",
        params: [
            0: FxParamHuman(label: "fx_cor"),
            1: FxParamHuman(label: "fx_opacidade", decimals: 0),
        ])),
    ("aurea.color.balance_hls", FxEffectHuman(
        name: "fx_name_color_balance_hls",
        keywords: "equilibrio balanco cor hls matiz luz saturacao",
        params: [
            0: FxParamHuman(label: "fx_matiz", suffix: "°", decimals: 0),
            1: FxParamHuman(label: "fx_luminosidade", decimals: 0),
            2: FxParamHuman(label: "fx_saturacao", decimals: 0),
        ])),
    ("aurea.blur.zoom", FxEffectHuman(
        name: "fx_name_zoom_blur",
        keywords: "zoom rastro radial lente empurrar desfoque",
        params: [
            0: FxParamHuman(label: "fx_centro"),
            1: FxParamHuman(label: "fx_intensidade", decimals: 0),
            2: FxParamHuman(label: "fx_esticar_bordas"),
            3: FxParamHuman(label: "fx_mistura", decimals: 0),
        ])),
    ("aurea.distort.bulge", FxEffectHuman(
        name: "fx_name_bulge",
        keywords: "bojo bulge pinca estufar puxar lente centro",
        params: [
            0: FxParamHuman(label: "fx_centro"),
            1: FxParamHuman(label: "fx_raio", suffix: "%", decimals: 0),
            2: FxParamHuman(label: "fx_altura", suffix: "%", decimals: 0),
            3: FxParamHuman(label: "fx_mistura", decimals: 0),
        ])),
    ("aurea.pattern.checkerboard", FxEffectHuman(
        name: "fx_name_checkerboard",
        keywords: "xadrez checkerboard quadriculado tabuleiro celulas padrao",
        params: [
            0: FxParamHuman(label: "fx_largura_celula", suffix: "px", decimals: 0),
            1: FxParamHuman(label: "fx_altura_celula", suffix: "px", decimals: 0),
            2: FxParamHuman(label: "fx_ancora"),
            3: FxParamHuman(label: "fx_rotacao", suffix: "°", decimals: 0),
            4: FxParamHuman(label: "fx_suavidade_borda", suffix: "px", decimals: 0),
            5: FxParamHuman(label: "fx_inverter"),
            6: FxParamHuman(label: "fx_opacidade", decimals: 0),
            7: FxParamHuman(label: "fx_cor"),
        ])),
    ("aurea.pattern.hexagonal", FxEffectHuman(
        name: "fx_name_hexagonal",
        keywords: "hexagonal favos abelha painel led malha matriz",
        params: [
            0: FxParamHuman(label: "fx_tamanho_celula", suffix: "px", decimals: 0),
            1: FxParamHuman(label: "fx_ancora"),
            2: FxParamHuman(label: "fx_rotacao", suffix: "°", decimals: 0),
            3: FxParamHuman(label: "fx_espessura", suffix: "px", decimals: 0),
            4: FxParamHuman(label: "fx_suavidade_borda", suffix: "px", decimals: 0),
            5: FxParamHuman(label: "fx_inverter"),
            6: FxParamHuman(label: "fx_opacidade", decimals: 0),
            7: FxParamHuman(label: "fx_cor"),
        ])),
    ("aurea.stylize.drop_shadow", FxEffectHuman(
        name: "fx_name_drop_shadow",
        keywords: "sombra projetada drop shadow atras texto caixa distancia",
        params: [
            0: FxParamHuman(label: "fx_cor_sombra"),
            1: FxParamHuman(label: "fx_opacidade", decimals: 0),
            2: FxParamHuman(label: "fx_direcao", suffix: "°", decimals: 0),
            3: FxParamHuman(label: "fx_distancia", suffix: "px", decimals: 0),
            4: FxParamHuman(label: "fx_suavidade", suffix: "px", decimals: 0),
            5: FxParamHuman(label: "fxo_shadow_only"),
        ])),
    ("aurea.stylize.border", FxEffectHuman(
        name: "fx_name_border",
        keywords: "borda contorno moldura traco outline",
        params: [
            0: FxParamHuman(label: "fx_cor"),
            1: FxParamHuman(label: "fx_largura", suffix: "px", decimals: 0),
            2: FxParamHuman(label: "fx_opacidade", decimals: 0),
        ])),
    // --- IA ---
    ("aurea.ai.depth_map", FxEffectHuman(
        name: "fx_name_depth_map",
        keywords: "profundidade depth mapa ia ai midas distancia perto longe z matte fundo",
        params: [
            0: FxParamHuman(label: "fx_mistura", decimals: 0),
            1: FxParamHuman(label: "fx_inverter"),
            2: FxParamHuman(label: "fx_suavizacao", decimals: 0),
        ])),
    ("aurea.color.hue_saturation", FxEffectHuman(
        name: "fx_name_hue_saturation",
        keywords: "matiz saturacao hue saturation luminosidade colorir tom cor",
        principal: [0, 1, 2, 3],
        params: [
            0: FxParamHuman(label: "fx_matiz"),
            1: FxParamHuman(label: "fx_saturacao", decimals: 0),
            2: FxParamHuman(label: "fx_luminosidade", decimals: 0),
            3: FxParamHuman(label: "fx_colorir"),
            4: FxParamHuman(label: "fx_matiz_colorir"),
            5: FxParamHuman(label: "fx_saturacao_colorir", decimals: 0),
            6: FxParamHuman(label: "fx_mistura", decimals: 0),
        ])),
    // --- Geradores e recorte do editor antigo ---
    ("aurea.generate.fractal_noise", FxEffectHuman(
        name: "fx_name_fractal_noise",
        keywords: "ruido fractal fractal noise nuvem fumaca textura turbulencia perlin simplex",
        principal: [0, 1, 2, 6, 7, 8],
        params: [
            0: FxParamHuman(label: "fx_tipo_ruido"),
            1: FxParamHuman(label: "fx_tipo_fractal"),
            2: FxParamHuman(label: "fx_contraste", decimals: 0),
            3: FxParamHuman(label: "fx_brilho", decimals: 0),
            4: FxParamHuman(label: "fx_inverter"),
            5: FxParamHuman(label: "fx_estouro"),
            6: FxParamHuman(label: "fx_escala", suffix: "px", decimals: 0),
            7: FxParamHuman(label: "fx_complexidade", suffix: "oitavas", decimals: 1),
            8: FxParamHuman(label: "fx_evolucao"),
            9: FxParamHuman(label: "fx_deslocamento", suffix: "px", decimals: 0),
            10: FxParamHuman(label: "fx_rotacao"),
            11: FxParamHuman(label: "fx_semente"),
            12: FxParamHuman(label: "fx_influencia_oitavas", decimals: 0),
            13: FxParamHuman(label: "fx_opacidade", decimals: 0),
            14: FxParamHuman(label: "fx_mistura_camada"),
            15: FxParamHuman(label: "fx_preencher_caixa"),
        ])),
    ("aurea.generate.gradient_ramp", FxEffectHuman(
        name: "fx_name_gradient_ramp",
        keywords: "degrade gradient ramp gradiente linear radial duas cores",
        principal: [0, 1, 2, 3, 4, 6],
        params: [
            0: FxParamHuman(label: "fx_forma"),
            1: FxParamHuman(label: "fx_ponto_inicial"),
            2: FxParamHuman(label: "fx_ponto_final"),
            3: FxParamHuman(label: "fx_cor_inicial"),
            4: FxParamHuman(label: "fx_cor_final"),
            5: FxParamHuman(label: "fx_dispersao", suffix: "px", decimals: 0),
            6: FxParamHuman(label: "fx_mistura_original", decimals: 0),
            7: FxParamHuman(label: "fx_mistura_camada"),
            8: FxParamHuman(label: "fx_preencher_caixa"),
        ])),
    ("aurea.generate.four_color_gradient", FxEffectHuman(
        name: "fx_name_four_color_gradient",
        keywords: "degrade 4 cores four color gradient quatro cores cantos",
        principal: [4, 5, 6, 7, 8, 10],
        params: [
            0: FxParamHuman(label: "fx_ponto_1"),
            1: FxParamHuman(label: "fx_ponto_2"),
            2: FxParamHuman(label: "fx_ponto_3"),
            3: FxParamHuman(label: "fx_ponto_4"),
            4: FxParamHuman(label: "fx_cor_1"),
            5: FxParamHuman(label: "fx_cor_2"),
            6: FxParamHuman(label: "fx_cor_3"),
            7: FxParamHuman(label: "fx_cor_4"),
            8: FxParamHuman(label: "fx_suavidade", decimals: 0),
            9: FxParamHuman(label: "fx_ruido", decimals: 0),
            10: FxParamHuman(label: "fx_opacidade", decimals: 0),
            11: FxParamHuman(label: "fx_mistura_camada"),
            12: FxParamHuman(label: "fx_preencher_caixa"),
        ])),
    ("aurea.generate.audio_spectrum", FxEffectHuman(
        name: "fx_name_audio_spectrum",
        keywords: "espectro de audio audio spectrum som musica barras visualizador equalizador",
        principal: [0, 1, 4, 5, 7, 8, 10],
        params: [
            0: FxParamHuman(label: "fx_fonte_som"),
            1: FxParamHuman(label: "fx_bandas"),
            2: FxParamHuman(label: "fx_ponto_inicial"),
            3: FxParamHuman(label: "fx_ponto_final"),
            4: FxParamHuman(label: "fx_altura_maxima", suffix: "px", decimals: 0),
            5: FxParamHuman(label: "fx_espessura", suffix: "px", decimals: 1),
            6: FxParamHuman(label: "fx_suavidade", decimals: 0),
            7: FxParamHuman(label: "fx_cor_dentro"),
            8: FxParamHuman(label: "fx_cor_fora"),
            9: FxParamHuman(label: "fx_giro_matiz"),
            10: FxParamHuman(label: "fx_exibicao"),
            11: FxParamHuman(label: "fx_lado"),
            12: FxParamHuman(label: "fx_em_circulo"),
            13: FxParamHuman(label: "fx_compor_original"),
            14: FxParamHuman(label: "fx_sensibilidade", decimals: 0),
        ])),
    ("aurea.stylize.stroke_outline", FxEffectHuman(
        name: "fx_name_stroke_outline",
        keywords: "contorno stroke outline traco borda silhueta",
        params: [
            0: FxParamHuman(label: "fx_largura", suffix: "px", decimals: 1),
            1: FxParamHuman(label: "fx_cor"),
            2: FxParamHuman(label: "fx_suavidade", suffix: "px", decimals: 1),
            3: FxParamHuman(label: "fx_posicao"),
            4: FxParamHuman(label: "fx_opacidade", decimals: 0),
        ])),
    ("aurea.key.matte_refine", FxEffectHuman(
        name: "fx_name_matte_refine",
        keywords: "refinar recorte matte refine choke feather encolher suavizar mascara",
        params: [
            0: FxParamHuman(label: "fx_encolher", suffix: "px", decimals: 1),
            1: FxParamHuman(label: "fx_suavizar_borda", suffix: "px", decimals: 1),
            2: FxParamHuman(label: "fx_mostrar_mascara"),
        ])),
    // --- Pacote de áudio: efeitos de SOM (categoria Áudio) e os visuais ---
    ("aurea.audio.backwards", FxEffectHuman(
        name: "afx_name_backwards",
        keywords: "reverso backwards tras para frente inverter som trocar canais",
        params: [0: FxParamHuman(label: "afx_p_swap_channels")])),
    ("aurea.audio.delay", FxEffectHuman(
        name: "afx_name_delay",
        keywords: "atraso delay eco echo repeticao realimentacao feedback",
        principal: [0, 1, 2, 3, 4],
        params: [
            0: FxParamHuman(label: "afx_p_delay_time", decimals: 0),
            1: FxParamHuman(label: "afx_p_delay_amount", decimals: 0),
            2: FxParamHuman(label: "afx_p_feedback", decimals: 0),
            3: FxParamHuman(label: "afx_p_dry_out", decimals: 0),
            4: FxParamHuman(label: "afx_p_wet_out", decimals: 0),
        ])),
    ("aurea.audio.flange_chorus", FxEffectHuman(
        name: "afx_name_flange_chorus",
        keywords: "flange flanger chorus coro vozes modulacao",
        principal: [0, 1, 2, 3, 7, 8],
        params: [
            0: FxParamHuman(label: "afx_p_voice_separation", decimals: 1),
            1: FxParamHuman(label: "afx_p_voices"),
            2: FxParamHuman(label: "afx_p_mod_rate", decimals: 2),
            3: FxParamHuman(label: "afx_p_mod_depth", decimals: 0),
            4: FxParamHuman(label: "afx_p_voice_phase", decimals: 0),
            5: FxParamHuman(label: "afx_p_invert_phase"),
            6: FxParamHuman(label: "afx_p_stereo_voices"),
            7: FxParamHuman(label: "afx_p_dry_out", decimals: 0),
            8: FxParamHuman(label: "afx_p_wet_out", decimals: 0),
        ])),
    ("aurea.audio.high_low_pass", FxEffectHuman(
        name: "afx_name_high_low_pass",
        keywords: "passa alta passa baixa high low pass filtro corte graves agudos",
        params: [
            0: FxParamHuman(label: "afx_p_filter_options"),
            1: FxParamHuman(label: "afx_p_cutoff", decimals: 0),
            2: FxParamHuman(label: "afx_p_dry_out", decimals: 0),
            3: FxParamHuman(label: "afx_p_wet_out", decimals: 0),
        ])),
    ("aurea.audio.stereo_mixer", FxEffectHuman(
        name: "afx_name_stereo_mixer",
        keywords: "mixer estereo stereo pan balanco nivel fase canais",
        params: [
            0: FxParamHuman(label: "afx_p_left_level", decimals: 0),
            1: FxParamHuman(label: "afx_p_right_level", decimals: 0),
            2: FxParamHuman(label: "afx_p_left_pan", decimals: 0),
            3: FxParamHuman(label: "afx_p_right_pan", decimals: 0),
            4: FxParamHuman(label: "afx_p_invert_phase"),
        ])),
    ("aurea.audio.modulator", FxEffectHuman(
        name: "afx_name_modulator",
        keywords: "modulador modulator vibrato tremolo",
        params: [
            0: FxParamHuman(label: "afx_p_mod_type"),
            1: FxParamHuman(label: "afx_p_mod_rate", decimals: 2),
            2: FxParamHuman(label: "afx_p_mod_depth", decimals: 1),
            3: FxParamHuman(label: "afx_p_amp_mod", decimals: 0),
        ])),
    ("aurea.audio.parametric_eq", FxEffectHuman(
        name: "afx_name_parametric_eq",
        keywords: "eq equalizador parametrico parametric bandas graves agudos",
        principal: Array(0..<12),
        params: [
            0: FxParamHuman(label: "afx_p_band1_enable"),
            1: FxParamHuman(label: "afx_p_band1_freq", decimals: 0),
            2: FxParamHuman(label: "afx_p_band1_width", decimals: 1),
            3: FxParamHuman(label: "afx_p_band1_gain", decimals: 1),
            4: FxParamHuman(label: "afx_p_band2_enable"),
            5: FxParamHuman(label: "afx_p_band2_freq", decimals: 0),
            6: FxParamHuman(label: "afx_p_band2_width", decimals: 1),
            7: FxParamHuman(label: "afx_p_band2_gain", decimals: 1),
            8: FxParamHuman(label: "afx_p_band3_enable"),
            9: FxParamHuman(label: "afx_p_band3_freq", decimals: 0),
            10: FxParamHuman(label: "afx_p_band3_width", decimals: 1),
            11: FxParamHuman(label: "afx_p_band3_gain", decimals: 1),
        ])),
    ("aurea.audio.room_reverb", FxEffectHuman(
        name: "afx_name_reverb",
        keywords: "reverb reverberacao sala eco ambiencia",
        params: [
            0: FxParamHuman(label: "afx_p_reverb_time", decimals: 0),
            1: FxParamHuman(label: "afx_p_diffusion", decimals: 0),
            2: FxParamHuman(label: "afx_p_decay", decimals: 0),
            3: FxParamHuman(label: "afx_p_brightness", decimals: 0),
            4: FxParamHuman(label: "afx_p_dry_out", decimals: 0),
            5: FxParamHuman(label: "afx_p_wet_out", decimals: 0),
        ])),
    ("aurea.audio.tone", FxEffectHuman(
        name: "afx_name_tone",
        keywords: "tom tone gerador seno onda quadrada bip nota acorde",
        principal: [0, 1, 2, 3, 4, 5, 6],
        params: [
            0: FxParamHuman(label: "afx_p_waveform"),
            1: FxParamHuman(label: "afx_p_freq1", decimals: 2),
            2: FxParamHuman(label: "afx_p_freq2", decimals: 2),
            3: FxParamHuman(label: "afx_p_freq3", decimals: 2),
            4: FxParamHuman(label: "afx_p_freq4", decimals: 2),
            5: FxParamHuman(label: "afx_p_freq5", decimals: 2),
            6: FxParamHuman(label: "afx_p_level", decimals: 0),
        ])),
    ("aurea.generate.audio_waveform", FxEffectHuman(
        name: "afx_name_audio_waveform",
        keywords: "forma de onda waveform audio osciloscopio som visualizador",
        principal: [0, 1, 2, 4, 7, 10, 11, 13],
        params: [
            0: FxParamHuman(label: "afx_p_audio_layer"),
            1: FxParamHuman(label: "fx_ponto_inicial"),
            2: FxParamHuman(label: "fx_ponto_final"),
            3: FxParamHuman(label: "afx_p_displayed_samples"),
            4: FxParamHuman(label: "fx_altura_maxima", suffix: "px", decimals: 0),
            5: FxParamHuman(label: "afx_p_audio_duration", decimals: 0),
            6: FxParamHuman(label: "afx_p_audio_offset", decimals: 0),
            7: FxParamHuman(label: "fx_espessura", suffix: "px", decimals: 1),
            8: FxParamHuman(label: "fx_suavidade", decimals: 0),
            9: FxParamHuman(label: "afx_p_random_seed"),
            10: FxParamHuman(label: "afx_p_inside_color"),
            11: FxParamHuman(label: "afx_p_outside_color"),
            12: FxParamHuman(label: "afx_p_waveform_options"),
            13: FxParamHuman(label: "afx_p_display_options"),
            14: FxParamHuman(label: "fx_compor_original"),
        ])),
    ("aurea.generate.spectrum_analyzer", FxEffectHuman(
        name: "afx_name_spectrum",
        keywords: "espectro de audio audio spectrum som musica barras visualizador equalizador frequencias",
        principal: [0, 1, 2, 3, 4, 5, 6, 7, 12, 13, 18, 19],
        params: [
            0: FxParamHuman(label: "afx_p_audio_layer"),
            1: FxParamHuman(label: "fx_ponto_inicial"),
            2: FxParamHuman(label: "fx_ponto_final"),
            3: FxParamHuman(label: "afx_p_polar"),
            4: FxParamHuman(label: "afx_p_start_freq", decimals: 0),
            5: FxParamHuman(label: "afx_p_end_freq", decimals: 0),
            6: FxParamHuman(label: "afx_p_freq_bands"),
            7: FxParamHuman(label: "fx_altura_maxima", suffix: "px", decimals: 0),
            8: FxParamHuman(label: "afx_p_audio_duration", decimals: 0),
            9: FxParamHuman(label: "afx_p_audio_offset", decimals: 0),
            10: FxParamHuman(label: "fx_espessura", suffix: "px", decimals: 1),
            11: FxParamHuman(label: "fx_suavidade", decimals: 0),
            12: FxParamHuman(label: "afx_p_inside_color"),
            13: FxParamHuman(label: "afx_p_outside_color"),
            14: FxParamHuman(label: "afx_p_blend_overlap"),
            15: FxParamHuman(label: "afx_p_hue_interp"),
            16: FxParamHuman(label: "afx_p_dynamic_hue"),
            17: FxParamHuman(label: "afx_p_color_symmetry"),
            18: FxParamHuman(label: "afx_p_display_options"),
            19: FxParamHuman(label: "afx_p_side_options"),
            20: FxParamHuman(label: "afx_p_duration_averaging"),
            21: FxParamHuman(label: "fx_compor_original"),
        ])),
    ("aurea.stylize.ball_grid", FxEffectHuman(
        name: "afx_name_balls",
        keywords: "bolas esferas balls spheres grade particulas explodir dispersar torcer",
        principal: [0, 1, 2, 3, 4, 5, 6],
        params: [
            0: FxParamHuman(label: "afx_p_scatter", suffix: "px", decimals: 0),
            1: FxParamHuman(label: "afx_p_rotation_axis"),
            2: FxParamHuman(label: "afx_p_rotation"),
            3: FxParamHuman(label: "afx_p_twist_property"),
            4: FxParamHuman(label: "afx_p_twist_angle"),
            5: FxParamHuman(label: "afx_p_grid_spacing", suffix: "px", decimals: 1),
            6: FxParamHuman(label: "afx_p_ball_size", decimals: 0),
            7: FxParamHuman(label: "afx_p_instability_state"),
            8: FxParamHuman(label: "afx_p_instability", suffix: "px", decimals: 1),
            9: FxParamHuman(label: "fx_deform_light_direction"),
            10: FxParamHuman(label: "fx_mistura", decimals: 0),
        ])),
    // Desintegrar: a camada se desfaz em fragmentos que voam.
    ("aurea.transition.disintegrate", FxEffectHuman(
        name: "afx_name_disintegrate",
        keywords: "desintegrar disintegrate particulas particles fragmentos poeira dust cinzas dissolver explodir transicao",
        principal: [0, 1, 2, 3, 5, 11],
        params: [
            0: FxParamHuman(decimals: 0),
            2: FxParamHuman(suffix: "px", decimals: 1),
            3: FxParamHuman(decimals: 0),
            5: FxParamHuman(suffix: "px", decimals: 0),
            6: FxParamHuman(label: "particular_turbulence", suffix: "px", decimals: 0),
            7: FxParamHuman(suffix: "px", decimals: 0),
            8: FxParamHuman(decimals: 0),
            11: FxParamHuman(label: "afx_p_edge_glow", decimals: 0),
            12: FxParamHuman(label: "fx_mistura", decimals: 0),
        ])),
    // Particular: as partículas do app antigo.
    ("aurea.generate.particular", FxEffectHuman(
        name: "afx_name_particular",
        keywords: "particulas particles particular emissor emitter neve snow chuva rain fogo fire faiscas sparks fogos fireworks poeira dust bokeh",
        principal: [0, 9, 13, 15, 22, 24, 27, 32, 33, 35, 38])),
]

/// A tabela indexada pelo `typeId`, e a posição declarada (para ordenar sem
/// depender do idioma).
private let FxTableById: [UInt32: FxEffectHuman] = {
    var out: [UInt32: FxEffectHuman] = [:]
    for entry in FxTable { out[fxEffectTypeId(entry.key)] = entry.effect }
    return out
}()

private let FxTableOrder: [UInt32: Int] = {
    var out: [UInt32: Int] = [:]
    for (index, entry) in FxTable.enumerated() { out[fxEffectTypeId(entry.key)] = index }
    return out
}()

/// Quantos efeitos a tabela cobre (o relatório da sessão usa este número).
var fxEffectTableCount: Int { FxTable.count }

/// Stable English names; localized names remain searchable aliases.
func fxEffectDisplayName(_ typeId: UInt32, _ engineName: String) -> String {
    guard let key = FxTableById[typeId]?.name else { return FxEnglishEffectNames[typeId] ?? engineName }
    return AureaText.english(key)
}

private let FxEnglishEffectNames: [UInt32: String] = Dictionary(uniqueKeysWithValues: [
    "aurea.light.scene_flare": "3D Flare", "aurea.color.cube_lut": "LUT (.cube)",
    "aurea.light.halation": "Halation", "aurea.light.lens_flare": "Lens Flare",
    "aurea.distort.ripple": "Ripple", "aurea.distort.optics_compensation": "Optics Compensation",
    "aurea.blur.box": "Box Blur", "aurea.blur.directional": "Directional Blur",
    "aurea.transition.linear_wipe": "Linear Wipe", "aurea.transition.radial_wipe": "Radial Wipe",
    "aurea.transition.block_dissolve": "Block Dissolve",
].map { (fxEffectTypeId($0.key), $0.value) })

/// A ordem declarada do efeito; fora da tabela vai para o fim.
func fxEffectNameRank(_ typeId: UInt32) -> Int { FxTableOrder[typeId] ?? Int.max }

/// O texto onde a busca procura: nome humano, nome do motor, categoria e os
/// sinônimos da tabela.
func fxEffectSearchText(_ typeId: UInt32, _ engineName: String, _ category: String) -> String {
    fxNormalizeSearch([
        fxEffectDisplayName(typeId, engineName), engineName, category,
        (FxTableById[typeId]?.name).map { AureaText.t($0) } ?? "",
        FxTableById[typeId]?.keywords ?? "",
    ].joined(separator: " "))
}

// =============================================================================
// Unidade humana de UM parâmetro
// =============================================================================

/// A exibição resolvida de um parâmetro. O motor guarda na unidade dele; a
/// linha mostra `motor × scale` com o sufixo e as casas FIXAS (a caixa não
/// "dança" de largura durante o arrasto).
struct FxParamDisplay {
    /// Rótulo da tabela humana, quando existe (o do motor é a reserva).
    var labelKey: String?
    /// Rótulo que o MOTOR publica — nome técnico, não traduzido.
    var engineLabel: String
    var scale: Float
    var suffix: String
    var decimals: Int

    func toDisplay(_ engine: Float) -> Float { engine * scale }
    func toEngine(_ display: Float) -> Float { scale != 0 ? display / scale : display }
    /// O rótulo da linha, no idioma do app.
    var label: String { labelKey.map { AureaText.t($0) } ?? engineLabel }
}

/// A FORMA de um parâmetro (sem o valor): o que o cartão precisa para montar as
/// linhas. Muda só quando o efeito muda.
struct FxParamSlot: Equatable {
    var index: Int
    var type: Int
    var min: Float
    var max: Float
    var label: String
    var unit: String
    var enumLabels: [String]

    var components: Int { fxComponentCount(type) }
    var animatable: Bool { fxComponentCount(type) > 0 }

    /// Sensibilidade, na unidade do MOTOR: `(max − min) / 500` por ponto
    /// (atravessar a régua ≈ a faixa inteira em poucos arrastos). Ângulo =
    /// 0,5 °/ponto; faixas enormes presas em 2 unidades/ponto; sem faixa, 0,5.
    var unitsPerDp: Float {
        if type == fxParamAngle { return 0.5 }
        let range = max - min
        return (range.isFinite && range > 0) ? Swift.min(range / 500, 2) : 0.5
    }
}

// `aurea::ParamType` (Parameter.hpp).
let fxParamFloat = 0
let fxParamInt = 1
let fxParamBool = 2
let fxParamColor = 3
let fxParamPoint2D = 4
let fxParamPoint3D = 5
let fxParamAngle = 6
let fxParamEnum = 7
/// Outra camada da composição ("Camada de áudio"). O valor é o ÍNDICE da
/// camada (parte baixa do id); −1 = nenhuma.
let fxParamLayerRef = 10
let fxParamTextureRef = 11

// -----------------------------------------------------------------------------
// EQ paramétrico: a resposta do gráfico, com a MESMA conta do filtro que toca
// (`audio::eq_band` no motor: pico/vale RBJ, Q = 100 / largura %).
// -----------------------------------------------------------------------------
private func fxEqBand(_ hz: Double, _ widthPercent: Double, _ gainDb: Double) -> [Double] {
    let rate = 48000.0
    let f = min(max(hz, 10.0), rate * 0.49)
    let q = 100.0 / min(max(widthPercent, 0.5), 1000.0)
    let a = pow(10.0, min(max(gainDb, -60.0), 60.0) / 40.0)
    let w0 = 2.0 * Double.pi * f / rate
    let alpha = sin(w0) / (2.0 * q)
    let c = cos(w0)
    let a0 = 1.0 + alpha / a
    return [(1.0 + alpha * a) / a0, -2.0 * c / a0, (1.0 - alpha * a) / a0, -2.0 * c / a0, (1.0 - alpha / a) / a0]
}

private func fxBiquadDb(_ b: [Double], _ hz: Double) -> Double {
    let w = 2.0 * Double.pi * min(max(hz, 0.0), 24000.0) / 48000.0
    let c1 = cos(w), s1 = -sin(w), c2 = cos(2 * w), s2 = -sin(2 * w)
    let nr = b[0] + b[1] * c1 + b[2] * c2
    let ni = b[1] * s1 + b[2] * s2
    let dr = 1.0 + b[3] * c1 + b[4] * c2
    let di = b[3] * s1 + b[4] * s2
    let mag = (nr * nr + ni * ni).squareRoot() / max((dr * dr + di * di).squareRoot(), 1e-30)
    return 20.0 * log10(max(mag, 1e-12))
}

/// Resposta somada das bandas ligadas (12 valores: ativar, Hz, largura %, dB × 3).
func fxEqResponseDb(_ values: [Float], _ hz: Double) -> Double {
    var db = 0.0
    for band in 0..<3 {
        let o = band * 4
        guard values.count > o + 3, values[o] >= 0.5 else { continue }
        db += fxBiquadDb(fxEqBand(Double(values[o + 1]), Double(values[o + 2]), Double(values[o + 3])), hz)
    }
    return db
}

/// Espelho de `aurea::component_count`.
func fxComponentCount(_ type: Int) -> Int {
    switch type {
    case fxParamFloat, fxParamInt, fxParamBool, fxParamAngle, fxParamEnum: return 1
    case fxParamPoint2D: return 2
    case fxParamPoint3D: return 3
    case fxParamColor: return 4
    default: return 0
    }
}

/// Regra padrão das unidades humanas (o que a tabela não declarar):
/// - ângulo → "°", 0 casas; inteiro → 0 casas;
/// - `%` do motor → "%"; `px` → "px" (0 casas acima de 20 de faixa, senão 1);
/// - faixa 0..1 sem unidade → ×100 "%" (nunca "0,600");
/// - senão casas pela faixa: > 20 → 0, > 2 → 1, senão 2 (nunca 6 casas).
func fxParamDisplay(_ typeId: UInt32, _ s: FxParamSlot) -> FxParamDisplay {
    let h = FxTableById[typeId]?.params[s.index]
    let range = abs(s.max - s.min)
    let finite = range.isFinite
    let unit = s.unit
    var scale: Float = 1
    var suffix = unit
    var decimals: Int
    if s.type == fxParamAngle {
        suffix = "°"; decimals = 0
    } else if s.type == fxParamInt {
        decimals = 0
    } else if unit == "%" {
        suffix = "%"; decimals = (!finite || range > 20) ? 0 : 1
    } else if unit == "px" {
        suffix = "px"; decimals = (!finite || range > 20) ? 0 : 1
    } else if unit.isEmpty && finite && s.min >= 0 && s.max <= 1 {
        scale = 100; suffix = "%"; decimals = 0
    } else {
        decimals = (!finite || range > 20) ? 0 : (range > 2 ? 1 : 2)
    }
    return FxParamDisplay(labelKey: h?.label,
                          engineLabel: s.label,
                          scale: h?.scale ?? scale,
                          suffix: h?.suffix ?? suffix,
                          decimals: h?.decimals ?? decimals)
}

/// Os parâmetros visíveis de um efeito em PRINCIPAIS (na ordem da tabela) e
/// AVANÇADOS (na ordem do motor).
func fxSplitPrincipal(_ typeId: UInt32, _ visible: [FxParamSlot]) -> (main: [FxParamSlot], rest: [FxParamSlot]) {
    guard let wanted = FxTableById[typeId]?.principal else {
        if visible.count <= FX_SHOW_ALL_UP_TO { return (visible, []) }
        return (Array(visible.prefix(FX_DEFAULT_PRINCIPAL)), Array(visible.dropFirst(FX_DEFAULT_PRINCIPAL)))
    }
    let byIndex = Dictionary(visible.map { ($0.index, $0) }, uniquingKeysWith: { first, _ in first })
    let main = wanted.compactMap { byIndex[$0] }
    // Se a tabela não bate com o motor (efeito mudou), nada some: o que sobrou é
    // avançado.
    if main.isEmpty { return (visible, []) }
    let mainSet = Set(main.map(\.index))
    return (main, visible.filter { !mainSet.contains($0.index) })
}

// =============================================================================
// Categorias, descrições, alvos e custo (EffectCatalogMeta.kt)
// =============================================================================

/// Ordem das categorias no navegador; o que o motor inventar entra no fim.
let fxEffectCategoryOrder = [
    "Distorcer", "Glitch", "Estilizar", "Cor", "Luz", "Desfoque", "Ruído",
    "Nitidez", "Tempo", "Transição", "Recorte", "Gerar", "Utilitário",
    "Controles de expressão",
]

/// O RÓTULO de uma categoria, no idioma do app. A categoria em si é IDENTIDADE
/// do motor (a string viaja no catálogo e no `.aurea`): o que muda é o que a
/// pessoa lê. Categoria que a UI não conhece cai no próprio nome.
func fxEffectCategoryLabel(_ category: String) -> String {
    switch fxNormalizeSearch(category) {
    case "distorcer": return AureaText.t("cat_distort")
    case "glitch": return AureaText.t("cat_glitch")
    case "estilizar": return AureaText.t("cat_stylize")
    case "cor": return AureaText.t("cat_colour")
    case "luz", "glow e luz": return AureaText.t("cat_light")
    case "desfoque": return AureaText.t("cat_blur")
    case "ruido": return AureaText.t("cat_noise")
    case "nitidez": return AureaText.t("cat_sharpen")
    case "tempo": return AureaText.t("cat_time")
    case "transicao": return AureaText.t("cat_transition")
    case "recorte": return AureaText.t("cat_cutout")
    case "gerar": return AureaText.t("cat_generate")
    case "utilitario": return AureaText.t("cat_utility")
    case "controles de expressao": return AureaText.t("cat_expr")
    case "pattern": return AureaText.t("cat_pattern")
    case "audio": return AureaText.t("cat_audio")
    // Ferramentas que viraram efeito: categoria em pt fixa (como no Android).
    case "texto": return AureaText.t("panel_texto")
    case "rastreio": return AureaText.t("panel_rastreio")
    case "mascara": return AureaText.t("sh_dock_mask")
    default: return category
    }
}

/// Mesmos glifos da cartela de espera do Android; não simula o efeito.
func fxCategoryGlyph(_ category: String) -> Character {
    switch fxNormalizeSearch(category) {
    case "cor": return CupertinoGlyph.ColorFilter
    case "desfoque": return CupertinoGlyph.DropFill
    case "luz", "glow e luz": return CupertinoGlyph.Sparkles
    case "estilizar": return CupertinoGlyph.SquareGrid2x2
    case "distorcer": return CupertinoGlyph.Move
    case "recorte": return CupertinoGlyph.Scissors
    case "tempo": return CupertinoGlyph.Timer
    case "controles de expressao": return CupertinoGlyph.SliderHorizontal3
    case "audio": return CupertinoGlyph.MusicNote
    default: return CupertinoGlyph.WandStars
    }
}

/// Onde o efeito funciona. O motor aplica em qualquer camada que vire textura;
/// o que muda é só o que faz SENTIDO.
enum FxTarget: String, CaseIterable {
    case imagem, video, texto, vetor, forma, cena3D, preComposicao, ajuste

    var label: String {
        switch self {
        case .imagem: return AureaText.t("target_image")
        case .video: return AureaText.t("target_video")
        case .texto: return AureaText.t("target_text")
        case .vetor: return AureaText.t("target_vector")
        case .forma: return AureaText.t("target_shape")
        case .cena3D: return AureaText.t("target_3d")
        case .preComposicao: return AureaText.t("target_precomp")
        case .ajuste: return AureaText.t("target_adjust")
        }
    }
}

private let FxAllTargets = FxTarget.allCases
private let FxVisualTargets: [FxTarget] = [.imagem, .video, .texto, .vetor, .forma, .preComposicao, .ajuste]
private let FxKeyTargets: [FxTarget] = [.imagem, .video, .preComposicao]
private let FxTimeTargets: [FxTarget] = [.video, .preComposicao, .imagem, .ajuste]

/// Descrição de gente, alvos e palavras extras, por `typeId`.
private let FxMetaTable: [UInt32: (description: String, targets: [FxTarget], keywords: String)] = {
    var out: [UInt32: (String, [FxTarget], String)] = [:]
    func put(_ key: String, _ description: String, _ targets: [FxTarget] = FxAllTargets, _ keywords: String = "") {
        out[fxEffectTypeId(key)] = (description, targets, keywords)
    }
    put("aurea.transform", "fx_desc_transform")
    put("aurea.color.exposure", "fx_desc_color_exposure")
    put("aurea.color.brightness_contrast", "fx_desc_color_brightness_contrast")
    put("aurea.color.saturation", "fx_desc_color_saturation")
    put("aurea.color.tint", "fx_desc_color_tint")
    put("aurea.color.matrix", "fx_desc_color_matrix")
    put("aurea.color.levels", "fx_desc_color_levels")
    put("aurea.color.curves", "fx_desc_color_curves")
    put("aurea.blur.gaussian", "fx_desc_blur_gaussian")
    put("aurea.blur.sharpen", "fx_desc_blur_sharpen")
    put("aurea.light.glow", "fx_desc_light_glow")
    put("aurea.stylize.motion_tile", "fx_desc_stylize_motion_tile", FxVisualTargets)
    put("aurea.key.luma", "fx_desc_key_luma", FxKeyTargets)
    put("aurea.key.chroma", "fx_desc_key_chroma", FxKeyTargets)
    put("aurea.time.echo", "fx_desc_time_echo")
    // --- Fase 7.3: o pacote novo ---------------------------------------------
    put("aurea.color.invert", "fx_desc_color_invert")
    put("aurea.color.colorama", "fx_desc_color_colorama")
    put("aurea.blur.unsharp", "fx_desc_blur_unsharp")
    put("aurea.blur.lens", "fx_desc_blur_lens")
    put("aurea.light.deep_glow", "fx_desc_light_deep_glow")
    put("aurea.light.rays", "fx_desc_light_rays")
    put("aurea.light.sweep", "fx_desc_light_sweep")
    put("aurea.distort.shake", "fx_desc_distort_shake", FxVisualTargets)
    put("aurea.distort.turbulence", "fx_desc_distort_turbulence")
    put("aurea.distort.wave_warp", "fx_desc_distort_wave_warp")
    put("aurea.distort.warp", "fx_desc_distort_warp")
    put("aurea.distort.ripple_dissolve", "fx_desc_distort_ripple_dissolve")
    put("aurea.stylize.scanlines", "fx_desc_stylize_scanlines")
    put("aurea.stylize.grain", "fx_desc_stylize_grain")
    put("aurea.stylize.halftone", "fx_desc_stylize_halftone")
    put("aurea.stylize.minimax", "fx_desc_stylize_minimax")
    put("aurea.stylize.omino_diffusion", "fx_desc_omino_diffusion")
    put("aurea.stylize.pixel_sort", "fx_desc_stylize_pixel_sort")
    put("aurea.stylize.film_damage", "fx_desc_stylize_film_damage")
    put("aurea.stylize.jpeg_damage", "fx_desc_stylize_jpeg_damage")
    put("aurea.stylize.holomatrix", "fx_desc_stylize_holomatrix")
    put("aurea.glitch.glitchify", "fx_desc_glitch_glitchify")
    put("aurea.glitch.vhs", "fx_desc_glitch_vhs")
    put("aurea.glitch.uni_vhs", "fx_desc_glitch_uni_vhs")
    put("aurea.glitch.signal", "fx_desc_glitch_signal")
    put("aurea.glitch.cross", "fx_desc_glitch_cross")
    put("aurea.time.posterize", "fx_desc_time_posterize", FxTimeTargets, "stop motion quadros taxa travada")
    put("aurea.time.warp_rgb", "fx_desc_time_warp_rgb", [.video, .preComposicao, .imagem], "rgb no tempo canais separados atraso de cor")
    put("aurea.time.motion_detect", "fx_desc_time_motion_detect", [.video], "detectar movimento diferenca quadro anterior")
    put("aurea.stylize.crt_emulator", "fx_desc_stylize_crt_emulator", FxAllTargets, "crt tv tubo retro varredura fosforo")
    put("aurea.glitch.dissolve_shake", "fx_desc_glitch_dissolve_shake", FxAllTargets, "tremor dissolver fragmentos quebrar")
    put("aurea.glitch.datamosh", "fx_desc_glitch_datamosh", FxAllTargets, "datamosh compressao blocos arrastar derreter codec")
    put("aurea.distort.displacement_map", "fx_desc_distort_displacement_map", FxAllTargets, "mapa deslocamento camada distorcer")
    put("aurea.control.slider", "fx_desc_control_slider")
    put("aurea.control.angle", "fx_desc_control_angle")
    put("aurea.control.checkbox", "fx_desc_control_checkbox")
    put("aurea.control.color", "fx_desc_control_color")
    put("aurea.control.point", "fx_desc_control_point")
    // --- Pacote de paridade: movimento, transições por forma e acabamento ---
    put("aurea.motion.oscillate", "fx_desc_motion_oscillate")
    put("aurea.motion.oscillate.cycles", "fx_desc_oscillate_cycles")
    put("aurea.motion.swing", "fx_desc_motion_swing")
    put("aurea.motion.wiggle", "fx_desc_motion_wiggle")
    put("aurea.motion.twitch", "fx_desc_motion_twitch")
    put("aurea.transition.iris_wipe", "fx_desc_transition_iris_wipe")
    put("aurea.transition.box_wipe", "fx_desc_transition_box_wipe")
    put("aurea.transition.venetian_blinds", "fx_desc_transition_venetian_blinds")
    put("aurea.blur.radial", "fx_desc_blur_radial")
    put("aurea.distort.mirror", "fx_desc_distort_mirror")
    put("aurea.transform.crop", "fx_desc_transform_crop")
    put("aurea.stylize.vignette", "fx_desc_stylize_vignette")
    put("aurea.stylize.mosaic", "fx_desc_stylize_mosaic")
    put("aurea.stylize.find_edges", "fx_desc_stylize_find_edges")
    put("aurea.color.hue_saturation", "fx_desc_color_hue_saturation")
    // --- Geradores e recorte do editor antigo ---
    put("aurea.generate.fractal_noise", "fx_desc_generate_fractal_noise", FxAllTargets, "fractal noise nuvem fumaca textura turbulencia")
    put("aurea.generate.gradient_ramp", "fx_desc_generate_gradient_ramp", FxAllTargets, "ramp gradiente degrade linear radial")
    put("aurea.generate.four_color_gradient", "fx_desc_generate_four_color_gradient", FxAllTargets, "4 color gradient quatro cores degrade")
    put("aurea.generate.audio_spectrum", "fx_desc_generate_audio_spectrum", FxAllTargets, "audio spectrum espectro som musica barras visualizador")
    put("aurea.stylize.stroke_outline", "fx_desc_stylize_stroke_outline", FxAllTargets, "stroke contorno traco borda silhueta outline")
    put("aurea.key.matte_refine", "fx_desc_key_matte_refine", [.imagem, .video, .preComposicao, .texto, .forma], "choke feather encolher suavizar mascara matte recorte")
    // --- Pacote do editor antigo: o que só ele tinha ---
    put("aurea.color.fill", "fx_desc_color_fill", FxAllTargets, "preencher fill cor chapada tinta solido")
    put("aurea.color.balance_hls", "fx_desc_color_balance_hls", FxAllTargets, "equilibrio balanco cor hls matiz luz saturacao")
    put("aurea.blur.zoom", "fx_desc_blur_zoom", FxAllTargets, "zoom rastro radial lente empurrar desfoque")
    put("aurea.distort.bulge", "fx_desc_distort_bulge", FxAllTargets, "bojo bulge pinca estufar puxar lente centro")
    put("aurea.pattern.checkerboard", "fx_desc_pattern_checkerboard", FxAllTargets, "xadrez checkerboard quadriculado tabuleiro celulas padrao")
    put("aurea.pattern.hexagonal", "fx_desc_pattern_hexagonal", FxAllTargets, "hexagonal favos abelha painel led malha matriz")
    put("aurea.stylize.drop_shadow", "fx_desc_stylize_drop_shadow", FxAllTargets, "sombra projetada drop shadow atras texto caixa distancia")
    put("aurea.stylize.border", "fx_desc_stylize_border", FxAllTargets, "borda contorno moldura traco outline")
    // --- Pacote de áudio: efeitos de SOM (categoria Áudio) e os visuais ---
    let sound: [FxTarget] = [.video, .preComposicao]
    put("aurea.audio.backwards", "afx_desc_backwards", sound, "reverso backwards tras para frente inverter som audio trocar canais")
    put("aurea.audio.delay", "afx_desc_delay", sound, "atraso delay eco echo repeticao realimentacao feedback audio")
    put("aurea.audio.flange_chorus", "afx_desc_flange_chorus", sound, "flange flanger chorus coro vozes modulacao audio")
    put("aurea.audio.high_low_pass", "afx_desc_high_low_pass", sound, "passa alta passa baixa high low pass filtro corte graves agudos audio")
    put("aurea.audio.stereo_mixer", "afx_desc_stereo_mixer", sound, "mixer estereo stereo pan balanco nivel fase canais audio")
    put("aurea.audio.modulator", "afx_desc_modulator", sound, "modulador modulator vibrato tremolo audio")
    put("aurea.audio.parametric_eq", "afx_desc_parametric_eq", sound, "eq equalizador parametrico parametric bandas graves agudos audio")
    put("aurea.audio.room_reverb", "afx_desc_reverb", sound, "reverb reverberacao sala eco ambiencia audio")
    put("aurea.audio.tone", "afx_desc_tone", FxAllTargets, "tom tone gerador seno onda quadrada bip nota acorde audio")
    put("aurea.generate.audio_waveform", "afx_desc_audio_waveform", FxAllTargets, "forma de onda waveform audio osciloscopio som visualizador")
    put("aurea.generate.spectrum_analyzer", "afx_desc_spectrum", FxAllTargets, "espectro spectrum audio barras frequencias visualizador equalizador")
    put("aurea.stylize.ball_grid", "afx_desc_balls", FxVisualTargets, "bolas esferas balls spheres grade particulas explodir dispersar")
    put("aurea.transition.disintegrate", "afx_desc_disintegrate", FxVisualTargets, "desintegrar disintegrate particulas particles fragmentos poeira dust cinzas dissolver explodir transicao")
    put("aurea.shape3d.layout", "fx_desc_shape3d_layout", [.cena3D], "forma 3d shape partes parts girar espalhar explodir atraso aleatorio")
    put("aurea.generate.particular", "afx_desc_particular", FxAllTargets, "particulas particles particular emissor emitter neve snow chuva rain fogo fire faiscas sparks fogos fireworks poeira dust bokeh")
    // --- IA: a rede lê a FONTE da camada (foto ou vídeo) ---
    put("aurea.ai.depth_map", "fx_desc_ai_depth_map", [.imagem, .video], "profundidade depth mapa ia ai midas distancia perto longe z matte fundo")
    put("aurea.distort.puppet", "fx_desc_puppet", FxAllTargets, "fantoche puppet pino pin marionete rig personagem deformar")
    return out
}()

/// A descrição de gente. Efeito sem ficha não fica mudo: diz o que ele é (a
/// frase leva a CATEGORIA, que é rótulo traduzido).
func fxEffectDescription(_ typeId: UInt32, _ category: String) -> String {
    if let key = FxMetaTable[typeId]?.description { return AureaText.t(key) }
    return AureaText.t("effect_default_description", fxEffectCategoryLabel(category))
}

/// Onde ele funciona. Fora da tabela, em todo lugar (é o que o motor faz).
func fxEffectTargets(_ typeId: UInt32) -> [FxTarget] { FxMetaTable[typeId]?.targets ?? FxAllTargets }

/// A linha curta de compatibilidade: "Vídeo, imagem e pré-composição".
func fxEffectCompatibilityLine(_ typeId: UInt32) -> String {
    let targets = fxEffectTargets(typeId)
    if targets.count == FxAllTargets.count { return AureaText.t("effect_any_layer") }
    return targets.map(\.label).joined(separator: ", ")
}

/// A classe vem do catálogo do core, como no Android.
func fxEffectCost(_ effectClass: UInt32) -> Int {
    switch effectClass {
    case 1, 2: return 2
    case 3: return 3
    case 4: return 4
    default: return 1
    }
}

/// A linha de custo do cartão: "Desfoque · pesado para o celular".
func fxEffectCostLine(_ category: String, _ cost: Int) -> String {
    let label = fxEffectCategoryLabel(category)
    return cost > 1 ? AureaText.t("effect_cost_heavy", label) : label
}

// =============================================================================
// O navegador: filtro, ordenação e índice de busca
// =============================================================================

/// O que o seletor de categoria mostra.
enum FxEffectFilter: Equatable {
    case all
    case recent
    case favorite
    case category(String)
}

/// Categorias do catálogo, na ordem do navegador.
func fxEffectCategories(_ catalog: [EffectCatalogItem]) -> [String] {
    let names = Array(Set(catalog.map(\.category)))
    return names.sorted { a, b in
        let ra = fxEffectCategoryOrder.firstIndex(of: a) ?? fxEffectCategoryOrder.count
        let rb = fxEffectCategoryOrder.firstIndex(of: b) ?? fxEffectCategoryOrder.count
        if ra != rb { return ra < rb }
        return a.lowercased() < b.lowercased()
    }
}

/// Os efeitos agrupados por categoria, na ordem das fichas e por nome humano.
/// A ordenação NÃO pode depender do idioma.
func fxArrangeCatalog(_ catalog: [EffectCatalogItem], _ categories: [String]) -> [EffectCatalogItem] {
    catalog.sorted { a, b in
        let ca = categories.firstIndex(of: a.category) ?? categories.count
        let cb = categories.firstIndex(of: b.category) ?? categories.count
        if ca != cb { return ca < cb }
        let ra = fxEffectNameRank(a.typeId), rb = fxEffectNameRank(b.typeId)
        if ra != rb { return ra < rb }
        return a.name < b.name
    }
}

/// O texto indexado pela busca: nome humano, nome do motor, categoria,
/// sinônimos do painel e a descrição.
func fxCatalogHaystack(_ catalog: [EffectCatalogItem]) -> [UInt32: String] {
    var out: [UInt32: String] = [:]
    for entry in catalog {
        let text = [
            fxEffectDisplayName(entry.typeId, entry.name),
            (FxTableById[entry.typeId]?.name).map { AureaText.t($0) } ?? "",
            entry.name,
            entry.category,
            FxTableById[entry.typeId]?.keywords ?? "",
            FxMetaTable[entry.typeId]?.keywords ?? "",
            fxEffectDescription(entry.typeId, entry.category),
        ].joined(separator: " ")
        out[entry.typeId] = fxNormalizeSearch(text)
    }
    return out
}

/// Os efeitos que passam pelo filtro e pela busca, na ordem de exibição.
/// Busca vazia = o filtro manda; busca cheia = a busca manda (em todo o
/// catálogo).
func fxFilterCatalog(_ catalog: [EffectCatalogItem],
                     _ sorted: [EffectCatalogItem],
                     _ haystack: [UInt32: String],
                     _ query: String,
                     _ filter: FxEffectFilter,
                     _ recents: [UInt32],
                     _ favorites: Set<UInt32>) -> [EffectCatalogItem] {
    let q = fxNormalizeSearch(query)
    if !q.isEmpty {
        let terms = q.split(separator: " ").map(String.init).filter { !$0.isEmpty }
        return sorted.filter { entry in
            let hay = haystack[entry.typeId] ?? ""
            return terms.allSatisfy { hay.contains($0) }
        }
    }
    switch filter {
    case .all:
        return sorted
    case .recent:
        return recents.compactMap { id in catalog.first { $0.typeId == id } }
    case .favorite:
        return sorted.filter { favorites.contains($0.typeId) }
    case .category(let name):
        return sorted.filter { $0.category == name }
    }
}

// =============================================================================
// Preferências do catálogo (favoritos e recentes) — do APARELHO, não do
// projeto, como no Android: o mesmo efeito é favorito em qualquer projeto.
// =============================================================================

final class FxEffectPrefs: ObservableObject {
    static let recentMax = 12
    private let favoritesKey = "aurea.efeitos.favoritos"
    private let recentsKey = "aurea.efeitos.recentes"

    @Published private(set) var favorites: Set<UInt32>
    @Published private(set) var recents: [UInt32]

    init() {
        let stored = UserDefaults.standard.stringArray(forKey: "aurea.efeitos.favoritos") ?? []
        favorites = Set(stored.compactMap { UInt32($0) })
        let recent = UserDefaults.standard.string(forKey: "aurea.efeitos.recentes") ?? ""
        recents = recent.split(separator: ",").compactMap { UInt32($0) }
    }

    func isFavorite(_ typeId: UInt32) -> Bool { favorites.contains(typeId) }

    @discardableResult
    func toggleFavorite(_ typeId: UInt32) -> Bool {
        let on = !favorites.contains(typeId)
        if on { favorites.insert(typeId) } else { favorites.remove(typeId) }
        UserDefaults.standard.set(favorites.map(String.init), forKey: favoritesKey)
        return on
    }

    func addRecent(_ typeId: UInt32) {
        recents = ([typeId] + recents.filter { $0 != typeId }).prefix(Self.recentMax).map { $0 }
        UserDefaults.standard.set(recents.map(String.init).joined(separator: ","), forKey: recentsKey)
    }

    func clearRecents() {
        recents = []
        UserDefaults.standard.removeObject(forKey: recentsKey)
    }
}

// MARK: - Texto que o motor publica no efeito, no idioma do app

/// Unidades que o motor escreve em português; "%", "px", "Hz", "°"... ficam como estão.
private let FxUnitKeys: [String: String] = ["quadros": "fxu_frames", "px/quadro": "fxu_px_per_frame", "oitavas": "fxu_octaves", "voltas": "fxu_turns"]

/// Rótulo, opções e unidade de um parâmetro no idioma do app, pela IDENTIDADE
/// (typeId + índice → tabela gerada `EffectI18nTable`). Sem entrada, o texto do motor.
func fxLocalizedParam(_ typeId: UInt32, index: Int, label: String, unit: String, options: [String]) -> (label: String, unit: String, options: [String]) {
    let entry = EffectI18nTable.entries["\(typeId)#\(index)"]
    let localizedUnit = FxUnitKeys[unit].map { AureaText.t($0) } ?? unit
    guard let entry else { return (label, localizedUnit, options) }
    // Opções só pela identidade completa: se o motor mudou a lista, fica a dele.
    let localizedOptions = entry.options.count == options.count ? entry.options.map { AureaText.t($0) } : options
    return (AureaText.t(entry.label), localizedUnit, localizedOptions)
}

/// A ficha de um parâmetro do catálogo (`effectSpecs`) com rótulo, opções e unidade traduzidos.
func fxLocalizedSpec(_ typeId: UInt32, _ spec: [String: Any]) -> [String: Any] {
    let index = (spec["index"] as? NSNumber)?.intValue ?? -1
    let text = fxLocalizedParam(typeId, index: index, label: spec["label"] as? String ?? "", unit: spec["unit"] as? String ?? "",
                                options: spec["enumLabels"] as? [String] ?? [])
    var out = spec
    out["label"] = text.label
    out["unit"] = text.unit
    out["enumLabels"] = text.options
    return out
}
