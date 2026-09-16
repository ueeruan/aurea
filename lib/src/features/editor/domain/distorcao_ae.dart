import 'effect.dart';

/// ABA DISTORCER (16/09): CC Lens, Optics Compensation e Turbulent Displace,
/// com parametros lidos do After Effects 2026 do dono e mapeamentos medidos
/// anel a anel nos quadros que ele renderizou (`shaders/distorcao_ae.frag`).
const efeitosDistorcaoAe = <EffectType, EffectSpec>{
  EffectType.ccLens: EffectSpec(
    id: 'cc_lens',
    name: 'CC Lens',
    category: 'Distort',
    synonyms: ['lente', 'lens', 'bolha', 'lupa', 'olho de peixe', 'cc'],
    params: {
      'center_x': EffectParam('Centro X', .5, -1, 2, kind: ParamKind.point, relative: true),
      'center_y': EffectParam('Centro Y', .5, -1, 2, kind: ParamKind.point, relative: true),
      'size': EffectParam('Tamanho', 50, 0, 500, decimals: 1, dragStep: .2),
      'convergence': EffectParam('Convergência', 100, -200, 100, decimals: 1, dragStep: .5),
    },
    montar: ['size', 'convergence'],
    presets: [
      EffectPronto('Lupa', {'size': 50, 'convergence': 100}),
      EffectPronto('Bolha pequena', {'size': 30, 'convergence': 50}),
      EffectPronto('Encolher', {'size': 70, 'convergence': -100}),
    ],
  ),
  EffectType.opticsCompensation: EffectSpec(
    id: 'optics_compensation',
    name: 'Optics Compensation',
    category: 'Distort',
    synonyms: ['compensação de ótica', 'compensacao de otica', 'olho de peixe', 'fisheye', 'lente', 'barril', 'fov'],
    params: {
      'field_of_view': EffectParam('Campo de visão (FOV)', 0, 0, 179.99, unit: '°', decimals: 1, dragStep: .2),
      'reverse_lens_distortion': EffectParam('Inverter distorção de lente', 0, 0, 1, kind: ParamKind.toggle),
      'fov_orientation': EffectParam(
        'Orientação do campo de visão',
        0,
        0,
        2,
        kind: ParamKind.choice,
        options: ['Horizontal', 'Vertical', 'Diagonal'],
      ),
      'view_center_x': EffectParam('Centro da exibição X', .5, -1, 2, kind: ParamKind.point, relative: true),
      'view_center_y': EffectParam('Centro da exibição Y', .5, -1, 2, kind: ParamKind.point, relative: true),
    },
    montar: ['field_of_view', 'reverse_lens_distortion'],
    presets: [
      EffectPronto('Barril', {'field_of_view': 60}),
      EffectPronto('Olho de peixe', {'field_of_view': 120}),
      EffectPronto('Zoom de lente', {'field_of_view': 60, 'reverse_lens_distortion': 1}),
    ],
  ),
  EffectType.turbulentDisplace: EffectSpec(
    id: 'turbulent_displace',
    name: 'Turbulent Displace',
    category: 'Distort',
    procedural: true,
    synonyms: ['deslocamento turbulento', 'turbulencia', 'ondular', 'distorcer', 'agua', 'fumaça'],
    params: {
      'displacement': EffectParam(
        'Deslocamento',
        0,
        0,
        8,
        kind: ParamKind.choice,
        options: [
          'Turbulento',
          'Protuberância',
          'Torção',
          'Turbulento suave',
          'Protuberância suave',
          'Torção suave',
          'Vertical',
          'Horizontal',
          'Cruzado',
        ],
      ),
      'amount': EffectParam('Valor', 50, -10000, 10000, decimals: 1, dragStep: .5),
      'size': EffectParam('Tamanho', 100, 2, 1000, decimals: 1, dragStep: .5),
      'offset_x': EffectParam('Deslocar X', 0, -10000, 10000, decimals: 1, relative: true),
      'offset_y': EffectParam('Deslocar Y', 0, -10000, 10000, decimals: 1, relative: true),
      'complexity': EffectParam('Complexidade', 1, 1, 10, decimals: 1, dragStep: .02),
      'evolution': EffectParam('Evolução', 0, -36000, 36000, unit: '°', decimals: 1, dragStep: .5),
      'random_seed': EffectParam('Distribuição aleatória', 0, 0, 100000, decimals: 0, dragStep: .1),
      'pinning': EffectParam(
        'Fixação',
        2,
        0,
        2,
        kind: ParamKind.choice,
        options: ['Nenhuma', 'Fixar cantos', 'Fixar todas as bordas'],
      ),
    },
    montar: ['amount', 'size', 'evolution'],
    presets: [
      EffectPronto('Água', {'amount': 20, 'size': 60, 'complexity': 2}),
      EffectPronto('Fumaça', {'amount': 80, 'size': 150, 'complexity': 4}),
      EffectPronto('Torção', {'displacement': 2, 'amount': 60}),
    ],
  ),
};

double _v(EffectInstance e, String k, Duration local) {
  final p = e.spec.params[k]!;
  final bruto = e.paramAt(k, local);
  if (!bruto.isFinite) return p.initial;
  return bruto.clamp(p.min, p.max).toDouble();
}

List<double> valoresCcLens(EffectInstance e, Duration local) => [
  1,
  _v(e, 'center_x', local),
  _v(e, 'center_y', local),
  _v(e, 'size', local),
  _v(e, 'convergence', local),
];

List<double> valoresOpticsCompensation(EffectInstance e, Duration local) => [
  2,
  _v(e, 'field_of_view', local),
  _v(e, 'reverse_lens_distortion', local),
  _v(e, 'fov_orientation', local).roundToDouble() + 1,
  _v(e, 'view_center_x', local),
  _v(e, 'view_center_y', local),
  0,
  0,
];

List<double> valoresTurbulentDisplace(EffectInstance e, Duration local) => [
  3,
  _v(e, 'displacement', local).roundToDouble() + 1,
  _v(e, 'amount', local),
  _v(e, 'size', local),
  _v(e, 'offset_x', local),
  _v(e, 'offset_y', local),
  _v(e, 'complexity', local),
  _v(e, 'evolution', local),
  _v(e, 'random_seed', local),
  // Nenhuma = 1, cantos = 2, bordas = 3 (o padrao do AE).
  _v(e, 'pinning', local).roundToDouble() + 1,
];
