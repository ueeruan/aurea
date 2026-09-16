import 'dart:math' as math;

import 'effect.dart';

/// S_PIXELSORT (Sapphire), ABA ESTILIZAR LOTE 2 (16/09).
///
/// Nomes, faixas e padroes lidos do After Effects 2026 do dono (Center e
/// comprimentos em px da comp de 736 px; aqui o ponto e relativo e os
/// comprimentos sao px do AE, multiplicados pela escala no shader).
///
/// O shader (`shaders/pixel_sort.frag`) nao ordena a linha inteira: corta
/// cada linha em blocos de 28 pontos de reticula (12 px), acha a faixa
/// acima/abaixo do limiar que contem o pixel (faixas escuras mais curtas que
/// 2,33 x Soften sao atravessadas; a ponta avanca ate 1,25 x Soften no
/// escuro; pontos isolados menores que o Soften nao contam), recorta pelos
/// reinicios (espacamento medio 50000/Randomly Restart px), amostra a faixa
/// em 20 pontos, ordena pela chave e devolve o quantil da posicao. Esses
/// numeros foram ajustados contra o render do AE: com os padroes, a fracao
/// de pixels alterados (13,8 % x 13,7 %) e os percentis de comprimento das
/// faixas batem com o original.
///
/// Ficou de fora: Matte/Mocha e Apply Mask (sem entrada de matte no Aurea),
/// Soft Borders e o menu Opacity. "Combined Threshold Mask" mostra o mesmo
/// que "Threshold Mask".
const efeitosPixelSort = <EffectType, EffectSpec>{
  EffectType.pixelSort: EffectSpec(
    id: 's_pixel_sort',
    name: 'S_PixelSort',
    category: 'Stylize',
    cost: 3,
    synonyms: ['pixel sort', 'ordenar pixels', 'glitch', 'faixas', 'datamosh', 'sapphire'],
    params: {
      'mode': EffectParam(
        'Modo',
        1,
        1,
        3,
        kind: ParamKind.choice,
        options: ['Linear', 'Radial', 'Circular'],
      ),
      'sort_angle': EffectParam('Ângulo', 0, -360, 360, unit: '°', decimals: 1, dragStep: .2),
      'center_x': EffectParam('Centro X', .5, -1, 2, kind: ParamKind.point, relative: true),
      'center_y': EffectParam('Centro Y', .5, -1, 2, kind: ParamKind.point, relative: true),
      'radial_start_angle': EffectParam('Ângulo inicial (radial)', 0, -360, 360, unit: '°', decimals: 1, dragStep: .2),
      'radial_degrees_sorted': EffectParam('Graus ordenados (radial)', 360, 0, 360, unit: '°', decimals: 1, dragStep: .2),
      'radial_inner_radius': EffectParam('Raio interno (radial)', 96, 0, 5000, decimals: 1, dragStep: .5, relative: true),
      'ray_length': EffectParam('Comprimento do raio', 768, 0, 5000, decimals: 1, dragStep: .5, relative: true),
      'vary_radius': EffectParam('Variar raio', .1, 0, 1, decimals: 3, dragStep: .002),
      'circular_start_angle': EffectParam('Ângulo inicial (circular)', 0, -360, 360, unit: '°', decimals: 1, dragStep: .2),
      'circular_degrees_sorted': EffectParam('Graus ordenados (circular)', 270, 0, 360, unit: '°', decimals: 1, dragStep: .2),
      'circle_center_x': EffectParam('Centro do círculo X', .5, -1, 2, kind: ParamKind.point, relative: true),
      'circle_center_y': EffectParam('Centro do círculo Y', .5, -1, 2, kind: ParamKind.point, relative: true),
      'vary_start': EffectParam('Variar início', .15, 0, 1, decimals: 3, dragStep: .002),
      'circular_inner_radius': EffectParam('Raio interno (circular)', 0, 0, 5000, decimals: 1, dragStep: .5, relative: true),
      'thickness': EffectParam('Espessura', 1056, 0, 5000, decimals: 1, dragStep: .5, relative: true),
      'threshold': EffectParam('Limiar', .3, -1, 2, decimals: 3, dragStep: .002),
      'sort_direction': EffectParam(
        'Ordenar',
        2,
        1,
        2,
        kind: ParamKind.choice,
        options: ['Abaixo do limiar', 'Acima do limiar'],
      ),
      'reverse_sort_direction': EffectParam('Inverter ordem', 0, 0, 1, kind: ParamKind.toggle),
      'sort_type': EffectParam(
        'Tipo de ordenação',
        1,
        1,
        10,
        kind: ParamKind.choice,
        options: [
          'Monocromático',
          'Média',
          'Mínimo',
          'Máximo',
          'Vermelho',
          'Verde',
          'Azul',
          'Matiz',
          'Saturação',
          'Brilho',
        ],
      ),
      'random_restart': EffectParam('Reiniciar ao acaso', 100, 0, 1000, decimals: 1, dragStep: .5),
      'soften_threshold_mask': EffectParam('Suavizar máscara', 24, 0, 500, decimals: 1, dragStep: .1, relative: true),
      'downsample': EffectParam('Reduzir resolução', 0, 0, 1, kind: ParamKind.toggle),
      'sort_resolution': EffectParam('Resolução da ordenação', 720, 1, 8192, decimals: 0, dragStep: 1),
      'seed': EffectParam('Semente', .273, 0, 32767, kind: ParamKind.seed, decimals: 3),
      'mix_with_source': EffectParam('Misturar com original', 0, 0, 1, decimals: 3, dragStep: .002),
      'show': EffectParam(
        'Mostrar',
        1,
        1,
        5,
        kind: ParamKind.choice,
        options: [
          'Resultado',
          'Valores de ordenação',
          'Máscara do limiar',
          'Ruído de reinício',
          'Máscara combinada',
        ],
      ),
    },
    montar: ['threshold', 'sort_angle', 'random_restart'],
    presets: [
      EffectPronto('Faixas claras', {'threshold': .3, 'sort_direction': 2}),
      EffectPronto('Sombras escorridas', {'threshold': .35, 'sort_direction': 1, 'sort_angle': 90}),
      EffectPronto('Explosão radial', {'mode': 2, 'threshold': .25}),
    ],
  ),
};

/// Floats do `shaders/pixel_sort.frag`, na ordem p0.x, p0.y, ...:
///
///   p0 = modo (1..3), angulo (rad), centro x, centro y (0..1)
///   p1 = angulo inicial radial (rad), graus radiais (rad), raio interno
///        radial (px AE), comprimento do raio (px AE)
///   p2 = variar raio, angulo inicial circular (rad), graus circulares
///        (rad), variar inicio
///   p3 = centro do circulo x, y (0..1), raio interno circular (px AE),
///        espessura (px AE)
///   p4 = limiar, direcao (1 abaixo, 2 acima), inverter, tipo (1..10)
///   p5 = reiniciar ao acaso, suavizar mascara (px AE), reduzir, resolucao
///   p6 = semente, misturar com original, mostrar (1..5), 0
List<double> valoresPixelSort(EffectInstance e, Duration local) {
  double v(String k) {
    final p = e.spec.params[k]!;
    final bruto = e.paramAt(k, local);
    if (!bruto.isFinite) return p.initial;
    return bruto.clamp(p.min, p.max).toDouble();
  }

  const graus = math.pi / 180;
  return [
    v('mode').roundToDouble(),
    v('sort_angle') * graus,
    v('center_x'),
    v('center_y'),
    v('radial_start_angle') * graus,
    v('radial_degrees_sorted') * graus,
    v('radial_inner_radius'),
    v('ray_length'),
    v('vary_radius'),
    v('circular_start_angle') * graus,
    v('circular_degrees_sorted') * graus,
    v('vary_start'),
    v('circle_center_x'),
    v('circle_center_y'),
    v('circular_inner_radius'),
    v('thickness'),
    v('threshold'),
    v('sort_direction').roundToDouble(),
    v('reverse_sort_direction'),
    v('sort_type').roundToDouble(),
    v('random_restart'),
    v('soften_threshold_mask'),
    v('downsample'),
    v('sort_resolution').roundToDouble(),
    v('seed'),
    v('mix_with_source'),
    v('show').roundToDouble(),
    0,
  ];
}
