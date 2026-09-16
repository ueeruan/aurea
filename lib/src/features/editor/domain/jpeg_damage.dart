import 'effect.dart';

/// S_JPEGDAMAGE (Sapphire), com compressao JPEG de verdade na GPU
/// (`shaders/jpeg_damage.frag`, duas passadas: DCT quantizada e IDCT).
///
/// Parametros, faixas e padroes lidos do After Effects 2026 do dono. As
/// tabelas de quantizacao sao as do padrao JPEG (ITU T.81, Anexo K) com a
/// escala de qualidade da IJG — publicas, nao do plugin.
const efeitosJpegDamage = <EffectType, EffectSpec>{
  EffectType.jpegDamage: EffectSpec(
    id: 's_jpeg_damage',
    name: 'S_JpegDamage',
    category: 'Stylize',
    cost: 3,
    synonyms: ['jpeg', 'compressao', 'artefato', 'blocos', 'baixa qualidade', 'dano digital', 'sapphire'],
    params: {
      'quality': EffectParam('Qualidade', .1, .01, 1, decimals: 3, dragStep: .001),
      'res_factor': EffectParam('Fator de resolução', 1, 1, 32, decimals: 0, dragStep: .02),
      'res_rel_x': EffectParam('Resolução relativa X', 1, .01, 10, decimals: 2, dragStep: .005),
      'all_freq_scale': EffectParam('Escala de todas as frequências', 1, 0, 10, decimals: 2, dragStep: .005),
      'x_freq_scale': EffectParam('Escala de frequência X', 1, 0, 10, decimals: 2, dragStep: .005),
      'y_freq_scale': EffectParam('Escala de frequência Y', 1, 0, 10, decimals: 2, dragStep: .005),
      'low_freq_scale': EffectParam('Escala das baixas', 1, 0, 10, decimals: 2, dragStep: .005),
      'mid_freq_scale': EffectParam('Escala das médias', 1, 0, 10, decimals: 2, dragStep: .005),
      'high_freq_scale': EffectParam('Escala das altas', 1, 0, 10, decimals: 2, dragStep: .005),
      'affect_luma': EffectParam('Afetar luma', 1, 0, 10, decimals: 2, dragStep: .005),
      'affect_chroma': EffectParam('Afetar croma', .5, 0, 10, decimals: 2, dragStep: .005),
      'error_rate': EffectParam('Taxa de erros', 0, 0, 64, decimals: 2, dragStep: .02),
      'err_block_density': EffectParam('Densidade de blocos com erro', .75, 0, 1, decimals: 2, dragStep: .002),
      'error_amp': EffectParam('Amplitude do erro', 1, 0, 20, decimals: 2, dragStep: .005),
      'error_coherence': EffectParam('Coerência do erro', 1, 0, 20, decimals: 2, dragStep: .005),
      'jitter_frames': EffectParam('Variar a cada quadros', 1, 0, 100, decimals: 0, dragStep: .05),
      'rand_seed': EffectParam('Semente', .123, 0, 1000, decimals: 3, dragStep: .002),
      'scale_lights': EffectParam('Escalar luzes', 1, 0, 10, decimals: 2, dragStep: .005),
      'offset_darks': EffectParam('Deslocar sombras', 0, -1, 1, decimals: 3, dragStep: .002),
      'saturation': EffectParam('Saturação', 1, -5, 10, decimals: 2, dragStep: .005),
      'flip_noise_vertically': EffectParam('Inverter ruído na vertical', 0, 0, 1, kind: ParamKind.toggle),
    },
    montar: ['quality', 'res_factor', 'error_rate'],
    presets: [
      EffectPronto('Compressão forte', {'quality': .03}),
      EffectPronto('Blocos grandes', {'quality': .05, 'res_factor': 4}),
      EffectPronto('Transmissão ruim', {'quality': .08, 'error_rate': 6, 'err_block_density': .4}),
    ],
  ),
};

/// Tabela de luminancia do Anexo K, linha a linha.
const _luma = <int>[
  16, 11, 10, 16, 24, 40, 51, 61, //
  12, 12, 14, 19, 26, 58, 60, 55,
  14, 13, 16, 24, 40, 57, 69, 56,
  14, 17, 22, 29, 51, 87, 80, 62,
  18, 22, 37, 56, 68, 109, 103, 77,
  24, 35, 55, 64, 81, 104, 113, 92,
  49, 64, 78, 87, 103, 121, 120, 101,
  72, 92, 95, 98, 112, 100, 103, 99,
];

/// Os 4x4 de baixa frequencia da tabela de crominancia do Anexo K.
const _croma4 = <int>[17, 18, 24, 47, 18, 21, 26, 66, 24, 26, 56, 99, 47, 66, 99, 99];

/// Escala da IJG: qualidade 0..1 do Sapphire vira 1..100.
double _escalaIjg(double qualidade) {
  final q = (qualidade * 100).clamp(1.0, 100.0);
  return q < 50 ? 5000 / q : 200 - 2 * q;
}

int passoDeQuantizacao(int base, double qualidade) =>
    ((base * _escalaIjg(qualidade) + 50) / 100).floor().clamp(1, 32767);

/// Floats 8..111 do shader: p0..p5 e as tabelas ja escaladas.
List<double> valoresJpegDamage(EffectInstance e, Duration local) {
  double v(String k) {
    final p = e.spec.params[k]!;
    final bruto = e.paramAt(k, local);
    if (!bruto.isFinite) return p.initial;
    return bruto.clamp(p.min, p.max).toDouble();
  }

  final qualidade = v('quality');
  return [
    v('res_factor').roundToDouble(), v('res_rel_x'), v('all_freq_scale'), v('x_freq_scale'),
    v('y_freq_scale'), v('low_freq_scale'), v('mid_freq_scale'), v('high_freq_scale'),
    v('affect_luma'), v('affect_chroma'), v('error_rate'), v('err_block_density'),
    v('error_amp'), v('error_coherence'), v('jitter_frames').roundToDouble(), v('rand_seed'),
    v('scale_lights'), v('offset_darks'), v('saturation'), v('flip_noise_vertically'),
    0, 0, 0, 0, // p5 livre
    for (final b in _luma) passoDeQuantizacao(b, qualidade).toDouble(),
    for (final b in _croma4) passoDeQuantizacao(b, qualidade).toDouble(),
  ];
}
