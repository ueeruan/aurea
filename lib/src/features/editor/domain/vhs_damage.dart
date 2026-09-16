import 'effect.dart';

/// S_VHSDAMAGE (Boris FX Sapphire), aba Estilizar, lote 2 (16/09).
///
/// Nomes, faixas e padroes LIDOS do After Effects 2026 do dono. As contas
/// vieram de renders do proprio AE com cada estagio ligado sozinho sobre
/// imagens de sonda (comp 736x736):
///
///   Tape Noise      campo de deslocamento que independe da imagem e muda a
///                   cada quadro; ruido grosso = branco uniforme (193*C px)
///                   borrado com sigma .9*Dampen, fino = 184*F px por pixel;
///                   dx = DisplaceX*n, dy = -DisplaceY*n (mesmo campo)
///   Color Downsample celulas de croma 106,7/Scale px x 1,2*(106,7/Scale)/Rel
///                   Height px, media + bilinear; luma Rec.601 preservada
///   Color Aberration cada canal R-Y, G-Y, B-Y desloca Amount - Shift canal
///                   (px), reprojetado para luma zero; Saturation = escala
///                   linear; Sharpen = unsharp do croma (sigma ~3,3 px)
///   Color Bloom     Vintage3Strip ajustado (quadratico, rms 1/255) em
///                   mistura linear; Bloom Saturation = escala do croma
///   Luma Adjust     Y' = Offset + (Scale - Offset) * Y^(1/Boost); Blur Luma
///                   sigma = 44 px por unidade (Rel X/Y)
///   Scanlines       periodo = altura/(2*freq), x = c^(1+.5*min(I,1)),
///                   x + clamp(I*sen)*min(x,1-x), fase += Shift Speed * t
///   Streaks/Sparkles faixas com freq = Band Freq + Vary*aleatorio por quadro
///
/// As LUTs "VHS color 01-04" sao dados da Boris FX e NAO foram copiadas: as
/// quatro gradacoes do shader sao proprias. Interlaced Combing precisa do
/// quadro seguinte e fica fora (uma passada so).
const efeitosVhsDamage = <EffectType, EffectSpec>{
  EffectType.vhsDamage: EffectSpec(
    id: 's_vhs_damage',
    name: 'S_VHSDamage',
    category: 'Stylize',
    procedural: true,
    cost: 3,
    synonyms: ['vhs', 'vhs damage', 'fita', 'videocassete', 'analogico', 'retro', 'glitch', 'sapphire'],
    params: {
      'downsample': EffectParam('Reduzir resolução', 1, 0, 1, kind: ParamKind.toggle),
      'downsample_resolution': EffectParam('Resolução', 720, 1, 4096, decimals: 0, dragStep: 1),
      'color_style': EffectParam('Estilo de cor', 1, 0, 1, kind: ParamKind.toggle),
      'lut1': EffectParam('LUT 1', 2, 0, 4, kind: ParamKind.choice, options: _luts),
      'lut1_strength': EffectParam('Força da LUT 1', .45, 0, 1, decimals: 2, dragStep: .005),
      'lut2': EffectParam('LUT 2', 3, 0, 4, kind: ParamKind.choice, options: _luts),
      'lut2_strength': EffectParam('Força da LUT 2', 1, 0, 1, decimals: 2, dragStep: .005),
      'tint': EffectParam('Tingir', 0, -1, 1, decimals: 2, dragStep: .005),
      'color_downsample': EffectParam('Reduzir cor', 1, 0, 1, kind: ParamKind.toggle),
      'downsample_scale': EffectParam('Escala da cor', 6.67, .01, 100, decimals: 2, dragStep: .02),
      'rel_height': EffectParam('Altura relativa', .6, .01, 100, decimals: 2, dragStep: .005),
      'color_aberration': EffectParam('Aberração de cor', 1, 0, 1, kind: ParamKind.toggle),
      'shift_amount': EffectParam('Deslocar croma', 1.725, -200, 200, decimals: 2, dragStep: .02, relative: true),
      'shift_amount_y': EffectParam('Deslocar croma Y', 0, -200, 200, decimals: 2, dragStep: .02, relative: true),
      'shift_red': EffectParam('Deslocar vermelho', -.767, -200, 200, decimals: 2, dragStep: .02, relative: true),
      'shift_green': EffectParam('Deslocar verde', 0, -200, 200, decimals: 2, dragStep: .02, relative: true),
      'shift_blue': EffectParam('Deslocar azul', 0, -200, 200, decimals: 2, dragStep: .02, relative: true),
      'blur_amount': EffectParam('Desfoque do croma', 0, 0, 100, decimals: 2, dragStep: .01),
      'saturation': EffectParam('Saturação', 1, 0, 10, decimals: 2, dragStep: .005),
      'sharpen_amount': EffectParam('Nitidez do croma', 2, -10, 100, decimals: 2, dragStep: .01),
      'soften_amount': EffectParam('Suavizar croma', 0, 0, 100, decimals: 2, dragStep: .01),
      'color_bloom': EffectParam('Florescer cor', 1, 0, 1, kind: ParamKind.toggle),
      'bloom_vintage': EffectParam('Vintage', .5, 0, 10, decimals: 2, dragStep: .005),
      'bloom_saturation': EffectParam('Saturação do florescer', .73, 0, 10, decimals: 2, dragStep: .005),
      'luma_adjust': EffectParam('Ajustar luma', 1, 0, 1, kind: ParamKind.toggle),
      'offset_darks': EffectParam('Clarear sombras', 0, -1, 1, decimals: 3, dragStep: .002),
      'boost_mids': EffectParam('Realçar meios-tons', 1, .1, 10, decimals: 2, dragStep: .005),
      'scale_lights': EffectParam('Escala das luzes', 1, -10, 10, decimals: 2, dragStep: .005),
      'blur_luma': EffectParam('Desfoque da luma', .0115, 0, 1, decimals: 4, dragStep: .0005),
      'blur_rel_x': EffectParam('Desfoque relativo X', .05, 0, 10, decimals: 2, dragStep: .005),
      'blur_rel_y': EffectParam('Desfoque relativo Y', 1, 0, 10, decimals: 2, dragStep: .005),
      'tape_noise': EffectParam('Ruído de fita', 1, 0, 1, kind: ParamKind.toggle),
      'coarseness': EffectParam('Ruído grosso', .16, 0, 1, decimals: 3, dragStep: .002),
      'dampen_coarseness': EffectParam('Agrupar ruído grosso', 1.03, 0, 100, decimals: 2, dragStep: .01),
      'fine_noise': EffectParam('Ruído fino', .01, 0, .1, decimals: 3, dragStep: .0005),
      'displace_x': EffectParam('Deslocar X', .33, -1, 1, decimals: 2, dragStep: .005),
      'displace_y': EffectParam('Deslocar Y', -.04, -1, 1, decimals: 2, dragStep: .005),
      'fast_forward': EffectParam('Avanço rápido', 0, 0, 10, decimals: 2, dragStep: .005),
      'ff_band_frequency': EffectParam('Faixas do avanço', 1, 0, 100, decimals: 2, dragStep: .01),
      'ff_band_frequency_vary': EffectParam('Variar faixas do avanço', 2, -100, 100, decimals: 2, dragStep: .01),
      'ff_band_height': EffectParam('Altura das faixas', .1, 0, 1, decimals: 2, dragStep: .005),
      'ff_band_shift': EffectParam('Deslocar faixas', 1.03, 0, 100, decimals: 2, dragStep: .005),
      'ff_band_shift_vary': EffectParam('Variar deslocamento', .7, -100, 100, decimals: 2, dragStep: .005),
      'scanlines': EffectParam('Linhas de varredura', 1, 0, 1, kind: ParamKind.toggle),
      'lines_frequency': EffectParam('Frequência das linhas', 120, 0, 2000, decimals: 1, dragStep: .1),
      'lines_intensity': EffectParam('Intensidade das linhas', .02, 0, 10, decimals: 3, dragStep: .002),
      'shift_speed': EffectParam('Velocidade das linhas', 1.5, -10, 10, decimals: 2, dragStep: .01),
      'add_streaks_noise': EffectParam('Ruído nos riscos', .25, 0, 1, decimals: 2, dragStep: .005),
      'streaks': EffectParam('Riscos', 1, 0, 1, kind: ParamKind.toggle),
      'streaks_amount': EffectParam('Quantidade de riscos', .8, 0, 10, decimals: 2, dragStep: .005),
      'streaks_opacity': EffectParam('Opacidade dos riscos', .9, 0, 10, decimals: 2, dragStep: .005),
      'streaks_frequency': EffectParam('Frequência dos riscos', 960, 1, 5000, decimals: 0, dragStep: 1),
      'streaks_length': EffectParam('Comprimento dos riscos', 50, 0, 1000, decimals: 1, dragStep: .1),
      'black_streaks': EffectParam('Riscos pretos', .25, 0, 1, decimals: 2, dragStep: .005),
      'streaks_band_freq': EffectParam('Faixas de riscos', 1, 0, 100, decimals: 2, dragStep: .01),
      'streaks_band_freq_vary': EffectParam('Variar faixas de riscos', 3, -100, 100, decimals: 2, dragStep: .01),
      'streaks_band_size': EffectParam('Tamanho das faixas de riscos', .23, 0, 1, decimals: 2, dragStep: .005),
      'streaks_band_shift': EffectParam('Deslocar faixas de riscos', .15, 0, 100, decimals: 2, dragStep: .005),
      'streaks_band_shift_vary': EffectParam('Variar faixas de riscos (desl.)', .12, -100, 100, decimals: 2, dragStep: .005),
      'streaks_band_roll_speed': EffectParam('Rolagem dos riscos', .43, -10, 10, decimals: 2, dragStep: .005),
      'sparkles': EffectParam('Faíscas', 1, 0, 1, kind: ParamKind.toggle),
      'sparkles_amount': EffectParam('Quantidade de faíscas', 1.2, 0, 10, decimals: 2, dragStep: .005),
      'sparkles_opacity': EffectParam('Opacidade das faíscas', 1.2, 0, 10, decimals: 2, dragStep: .005),
      'sparkles_frequency': EffectParam('Frequência das faíscas', 1200, 1, 5000, decimals: 0, dragStep: 1),
      'sparkles_length': EffectParam('Comprimento das faíscas', 5, 0, 1000, decimals: 1, dragStep: .1),
      'black_sparkles': EffectParam('Faíscas pretas', 0, 0, 1, decimals: 2, dragStep: .005),
      'sparkles_band_freq': EffectParam('Faixas de faíscas', 2, 0, 100, decimals: 2, dragStep: .01),
      'sparkles_band_freq_vary': EffectParam('Variar faixas de faíscas', 3, -100, 100, decimals: 2, dragStep: .01),
      'sparkles_band_size': EffectParam('Tamanho das faixas de faíscas', .6, 0, 1, decimals: 2, dragStep: .005),
      'sparkles_band_shift': EffectParam('Deslocar faixas de faíscas', .4, 0, 100, decimals: 2, dragStep: .005),
      'sparkles_band_shift_vary': EffectParam('Variar faixas de faíscas (desl.)', .6, -100, 100, decimals: 2, dragStep: .005),
      'sparkles_band_roll_speed': EffectParam('Rolagem das faíscas', 0, -10, 10, decimals: 2, dragStep: .005),
      'random_seed': EffectParam('Semente', .123, 0, 32767, kind: ParamKind.seed, decimals: 3),
      'mix_with_source': EffectParam('Misturar com original', 0, 0, 1, decimals: 2, dragStep: .005),
    },
    montar: ['displace_x', 'streaks_amount', 'mix_with_source'],
    presets: [
      EffectPronto('Fita gasta', {'coarseness': .3, 'streaks_amount': 2, 'sparkles_amount': 2.5}),
      EffectPronto('Avanço rápido', {'fast_forward': 1.5}),
      EffectPronto('Só a cor', {'tape_noise': 0, 'streaks': 0, 'sparkles': 0, 'scanlines': 0}),
    ],
  ),
};

const _luts = ['Nenhuma', 'VHS cor 01', 'VHS cor 02', 'VHS cor 03', 'VHS cor 04'];

/// Os 64 floats de p0..p15, na ordem do `shaders/vhs_damage.frag`.
List<double> valoresVhsDamage(EffectInstance e, Duration local) {
  double v(String k) {
    final p = e.spec.params[k]!;
    final bruto = e.paramAt(k, local);
    if (!bruto.isFinite) return p.initial;
    return bruto.clamp(p.min, p.max).toDouble();
  }

  const chaves = [
    'downsample', 'color_style', 'color_downsample', 'color_aberration', 'color_bloom',
    'luma_adjust', 'tape_noise', 'scanlines', 'streaks', 'sparkles',
  ];
  var mascara = 0;
  for (var i = 0; i < chaves.length; i++) {
    if (v(chaves[i]) > .5) mascara |= 1 << i;
  }
  return [
    // p0
    v('downsample_resolution'), v('lut1').roundToDouble(), v('lut1_strength'), v('lut2').roundToDouble(),
    // p1
    v('lut2_strength'), v('tint'), v('downsample_scale'), v('rel_height'),
    // p2 (px do AE)
    v('shift_amount'), v('shift_amount_y'), v('shift_red'), v('shift_green'),
    // p3
    v('shift_blue'), v('saturation'), v('sharpen_amount') - v('soften_amount'), v('blur_amount'),
    // p4
    v('bloom_vintage'), v('bloom_saturation'), v('offset_darks'), v('boost_mids'),
    // p5
    v('scale_lights'), v('blur_luma'), v('blur_rel_x'), v('blur_rel_y'),
    // p6
    v('coarseness'), v('dampen_coarseness'), v('fine_noise'), v('displace_x'),
    // p7
    v('displace_y'), v('fast_forward'), v('ff_band_frequency'), v('ff_band_frequency_vary'),
    // p8
    v('ff_band_height'), v('ff_band_shift'), v('lines_frequency'), v('lines_intensity'),
    // p9
    v('shift_speed'), v('streaks_amount'), v('streaks_opacity'), v('streaks_frequency'),
    // p10
    v('streaks_length'), v('black_streaks'), v('streaks_band_freq'), v('streaks_band_freq_vary'),
    // p11
    v('streaks_band_size'), v('streaks_band_shift'), v('streaks_band_shift_vary'), v('streaks_band_roll_speed'),
    // p12
    v('sparkles_amount'), v('sparkles_opacity'), v('sparkles_frequency'), v('sparkles_length'),
    // p13
    v('black_sparkles'), v('sparkles_band_freq'), v('sparkles_band_freq_vary'), v('sparkles_band_size'),
    // p14
    v('sparkles_band_shift'), v('sparkles_band_shift_vary'), v('sparkles_band_roll_speed'), v('add_streaks_noise'),
    // p15
    mascara.toDouble(), v('random_seed'), v('mix_with_source'), v('ff_band_shift_vary'),
  ];
}
