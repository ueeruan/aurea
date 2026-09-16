import 'effect.dart';

/// S_AUTOPAINT (Sapphire), ABA ESTILIZAR LOTE 2 (16/09).
///
/// Nomes, faixas e padroes lidos do After Effects 2026 do dono. O desenho
/// roda em `shaders/auto_paint.frag`: uma pincelada por celula de uma grade
/// com jitter (Seed; Jitter Frames re-sorteia a cada N quadros), direcao
/// pelo gradiente da luma medido entre os centros vizinhos (Van Gogh segue
/// a borda, Hairy Paint cruza a borda, Pointalize e celula pontuda sem
/// direcao), cor da fonte no centro, ordem de pintura sorteada e nitidez
/// depois da pintura (anel de 6 amostras da propria pintura).
///
/// Ficou de fora: mascara/Mocha e o menu Opacity (o Aurea ja mascara a
/// camada por conta propria).
const efeitosAutoPaint = <EffectType, EffectSpec>{
  EffectType.autoPaint: EffectSpec(
    id: 's_auto_paint',
    name: 'S_AutoPaint',
    category: 'Stylize',
    synonyms: ['auto paint', 'pintura', 'pincel', 'pinceladas', 'van gogh', 'oleo', 'aquarela', 'sapphire'],
    params: {
      'style': EffectParam(
        'Estilo',
        1,
        1,
        3,
        kind: ParamKind.choice,
        options: ['Van Gogh', 'Hairy Paint', 'Pointalize'],
      ),
      'frequency': EffectParam('Frequência', 50, .1, 1000, decimals: 1, dragStep: .1),
      'stroke_length': EffectParam('Comprimento do traço', 2, -20, 20, decimals: 2, dragStep: .01),
      'stroke_align': EffectParam('Alinhar traços', .2, 0, 10, decimals: 2, dragStep: .005),
      'smooth_colors': EffectParam('Suavizar cores', 0, 0, 200, decimals: 1, dragStep: .05, relative: true),
      'seed': EffectParam('Semente', 0, 0, 32767, kind: ParamKind.seed, decimals: 0),
      'jitter_frames': EffectParam('Re-sortear a cada (quadros)', 0, 0, 100, decimals: 0, dragStep: .05),
      'sharpen': EffectParam('Nitidez', 1, -10, 10, decimals: 2, dragStep: .005),
      'sharpen_width': EffectParam('Largura da nitidez', .1, 0, 2, decimals: 3, dragStep: .001),
      'mix_with_source': EffectParam('Misturar com original', 0, 0, 1, decimals: 3, dragStep: .002),
    },
    montar: ['frequency', 'stroke_length', 'sharpen'],
    presets: [
      EffectPronto('Óleo', {'frequency': 50, 'stroke_length': 2}),
      EffectPronto('Pinceladas grossas', {'frequency': 25, 'stroke_length': 3}),
      EffectPronto('Pontilhismo', {'style': 3, 'frequency': 90}),
    ],
  ),
};

/// Floats do `shaders/auto_paint.frag`, na ordem p0.x, p0.y, ...:
///
///   p0 = estilo (1..3), frequencia, comprimento do traco, alinhar
///   p1 = suavizar cores (px do AE), semente, jitter frames, nitidez
///   p2 = largura da nitidez, misturar com original, 0, 0
List<double> valoresAutoPaint(EffectInstance e, Duration local) {
  double v(String k) {
    final p = e.spec.params[k]!;
    final bruto = e.paramAt(k, local);
    if (!bruto.isFinite) return p.initial;
    return bruto.clamp(p.min, p.max).toDouble();
  }

  return [
    v('style').roundToDouble(),
    v('frequency'),
    v('stroke_length'),
    v('stroke_align'),
    v('smooth_colors'),
    v('seed').roundToDouble(),
    v('jitter_frames').roundToDouble(),
    v('sharpen'),
    v('sharpen_width'),
    v('mix_with_source'),
    0,
    0,
  ];
}
