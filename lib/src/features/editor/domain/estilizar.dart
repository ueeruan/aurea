import 'dart:math' as math;
import 'dart:ui';

import 'effect.dart';

/// ABA ESTILIZAR, LOTE 1 (16/09, pedido do dono: "copiando do AE,
/// IDENTICOS"). Os nomes, faixas e padroes foram LIDOS do After Effects
/// 2026 do dono (ExtendScript, efeito aplicado e lido parametro a
/// parametro), e as contas foram ajustadas contra quadros renderizados
/// por ele (`comp.saveFrameToPng`):
///
///   CC Threshold      luma Rec.601 >= limiar            (99,95 % dos pixels)
///   CC Threshold RGB  o mesmo por canal                 (100 %)
///   CC Vignette       queda cos^4 da lente pelo angulo de visao
///   CC Block Load     varreduras progressivas de blocos 2^n
///   S_ScanLines       periodo = largura/(2*freq), p = .5+.5*S*sen,
///                     x = entrada^gama, saida = x+(2p-1)*min(x,1-x)
///                     (erro 0,05 a 0,24 em 255 nos dois renders)
///   S_HalfTone        periodo = largura/freq, contraste 1,5 na luma
///   S_EdgeColorize    sigma = Edge Smooth/2, ganho 3,4, quatro direcoes
///                     (correlacao 0,94 com o render)
///
/// O desenho roda em `shaders/estilizar.frag`, uma passada por efeito.
const efeitosDeEstilizar = <EffectType, EffectSpec>{
  EffectType.threshold: EffectSpec(
    id: 'cc_threshold',
    name: 'CC Threshold',
    category: 'Stylize',
    synonyms: ['threshold', 'limiar', 'preto e branco', 'alto contraste', 'cc'],
    params: {
      'threshold': EffectParam('Limiar', 127.5, 0, 255, decimals: 1, dragStep: .25),
      'channel': EffectParam(
        'Canal',
        0,
        0,
        3,
        kind: ParamKind.choice,
        options: ['Luminância', 'Vermelho', 'Verde', 'Azul'],
      ),
      'invert': EffectParam('Inverter', 0, 0, 1, kind: ParamKind.toggle),
      'blend': EffectParam('Misturar com original', 0, 0, 100, unit: '%', decimals: 1),
    },
    montar: ['threshold', 'blend'],
    presets: [
      EffectPronto('Tinta', {'threshold': 90}),
      EffectPronto('Meio a meio', {'threshold': 127.5}),
      EffectPronto('Negativo', {'threshold': 127.5, 'invert': 1}),
    ],
  ),
  EffectType.thresholdRgb: EffectSpec(
    id: 'cc_threshold_rgb',
    name: 'CC Threshold RGB',
    category: 'Stylize',
    synonyms: ['threshold rgb', 'limiar rgb', 'pop art', 'cc'],
    params: {
      'red_threshold': EffectParam('Limiar vermelho', 127.5, 0, 255, decimals: 1, dragStep: .25),
      'green_threshold': EffectParam('Limiar verde', 127.5, 0, 255, decimals: 1, dragStep: .25),
      'blue_threshold': EffectParam('Limiar azul', 127.5, 0, 255, decimals: 1, dragStep: .25),
      'invert_red': EffectParam('Inverter vermelho', 0, 0, 1, kind: ParamKind.toggle),
      'invert_green': EffectParam('Inverter verde', 0, 0, 1, kind: ParamKind.toggle),
      'invert_blue': EffectParam('Inverter azul', 0, 0, 1, kind: ParamKind.toggle),
      'blend': EffectParam('Misturar com original', 0, 0, 100, unit: '%', decimals: 1),
    },
    montar: ['red_threshold', 'green_threshold', 'blue_threshold'],
    presets: [
      EffectPronto('Pop', {'red_threshold': 100, 'green_threshold': 140, 'blue_threshold': 120}),
      EffectPronto('Quente', {'red_threshold': 80, 'green_threshold': 150, 'blue_threshold': 190}),
      EffectPronto('Frio', {'red_threshold': 190, 'green_threshold': 130, 'blue_threshold': 80}),
    ],
  ),
  EffectType.vignette: EffectSpec(
    id: 'cc_vignette',
    name: 'CC Vignette',
    category: 'Stylize',
    synonyms: ['vinheta', 'vignette', 'escurecer bordas', 'cc'],
    params: {
      'amount': EffectParam('Quantidade', 100, -1000, 1000, decimals: 1, dragStep: .5),
      'angle_of_view': EffectParam('Ângulo de visão', 45, 0, 120, unit: '°', decimals: 1, dragStep: .2),
      'center_x': EffectParam('Centro X', .5, -1, 2, kind: ParamKind.point, relative: true),
      'center_y': EffectParam('Centro Y', .5, -1, 2, kind: ParamKind.point, relative: true),
      'pin_highlights': EffectParam('Proteger luzes', 0, 0, 100, decimals: 1),
    },
    montar: ['amount', 'angle_of_view'],
    presets: [
      EffectPronto('Suave', {'amount': 60, 'angle_of_view': 45}),
      EffectPronto('Lente', {'amount': 100, 'angle_of_view': 70}),
      EffectPronto('Clarear bordas', {'amount': -80, 'angle_of_view': 60}),
    ],
  ),
  EffectType.blockLoad: EffectSpec(
    id: 'cc_block_load',
    name: 'CC Block Load',
    category: 'Stylize',
    procedural: false,
    synonyms: ['block load', 'carregar blocos', 'pixel', 'download', 'progressivo', 'cc'],
    params: {
      'completion': EffectParam('Conclusão', 0, 0, 100, unit: '%', decimals: 1, dragStep: .2),
      'scans': EffectParam('Varreduras', 4, 1, 16, decimals: 0, dragStep: .05),
      'start_cleared': EffectParam('Começar vazio', 1, 0, 1, kind: ParamKind.toggle),
      'bilinear': EffectParam('Bilinear', 0, 0, 1, kind: ParamKind.toggle),
    },
    montar: ['completion', 'scans'],
    presets: [
      EffectPronto('Metade', {'completion': 50}),
      EffectPronto('Blocos grandes', {'completion': 20, 'scans': 6}),
      EffectPronto('Suave', {'completion': 40, 'bilinear': 1}),
    ],
  ),
  EffectType.scanLines: EffectSpec(
    id: 's_scanlines',
    name: 'S_ScanLines',
    category: 'Stylize',
    hasColor: true,
    defaultColor: Color(0xFFFFFFFF),
    colorLabels: ['Escala de cor'],
    synonyms: ['scanlines', 'scan lines', 'linhas de tv', 'crt', 'monitor', 'sapphire'],
    params: {
      'lines_frequency': EffectParam('Frequência', 50, 1, 2000, decimals: 1, dragStep: .1),
      'lines_sharpness': EffectParam('Nitidez das linhas', 1, 0, 50, decimals: 2, dragStep: .01),
      'lines_angle': EffectParam('Ângulo', 0, -360, 360, unit: '°', decimals: 1, dragStep: .2),
      'lines_shift': EffectParam('Deslocar linhas', 0, -100, 100, decimals: 2, dragStep: .005),
      'shift_red': EffectParam('Deslocar vermelho', 0, -10, 10, decimals: 2, dragStep: .005),
      'shift_green': EffectParam('Deslocar verde', 0, -10, 10, decimals: 2, dragStep: .005),
      'shift_blue': EffectParam('Deslocar azul', 0, -10, 10, decimals: 2, dragStep: .005),
      'add_noise': EffectParam('Ruído', 0, 0, 10, decimals: 2, dragStep: .005),
      'noise_freq_rel': EffectParam('Frequência do ruído', 1, .01, 50, decimals: 2, dragStep: .01),
      'brightness': EffectParam('Brilho', 1, 0, 10, decimals: 2, dragStep: .005),
      'offset': EffectParam('Deslocamento', 0, -1, 1, decimals: 3, dragStep: .002),
      'gamma': EffectParam('Gama', 1.5, .1, 10, decimals: 2, dragStep: .005),
      'saturation': EffectParam('Saturação', 1, 0, 10, decimals: 2, dragStep: .005),
      'smooth_source': EffectParam('Suavizar fonte', 0, 0, 100, decimals: 1, dragStep: .05, relative: true),
    },
    montar: ['lines_frequency', 'lines_sharpness', 'gamma'],
    presets: [
      EffectPronto('TV desalinhada', {'shift_red': -.33, 'shift_blue': .33}),
      EffectPronto('Linhas finas', {'lines_frequency': 180, 'gamma': 1.2}),
      EffectPronto('CRT', {'lines_frequency': 90, 'lines_sharpness': 2, 'add_noise': .1}),
    ],
  ),
  EffectType.halfTone: EffectSpec(
    id: 's_halftone',
    name: 'S_HalfTone',
    category: 'Stylize',
    hasColor: true,
    defaultColor: Color(0xFFFFFFFF),
    extraColors: 1,
    defaultExtraColors: [Color(0xFF000000)],
    colorLabels: ['Cor 1', 'Cor 0'],
    synonyms: ['halftone', 'meio-tom', 'reticula', 'pontos', 'quadrinho', 'pop art', 'sapphire'],
    params: {
      'dots': EffectParam('Pontos', 0, 0, 1, kind: ParamKind.choice, options: ['Pretos', 'Brancos']),
      'dots_frequency': EffectParam('Frequência', 50, 0, 1000, decimals: 1, dragStep: .1),
      'dots_angle': EffectParam('Ângulo', -30, -360, 360, unit: '°', decimals: 1, dragStep: .2),
      'dots_rel_width': EffectParam('Largura relativa', 1, .01, 10, decimals: 2, dragStep: .005),
      'dots_sharpness': EffectParam('Nitidez', 4, 0, 100, decimals: 2, dragStep: .02),
      'dots_lighten': EffectParam('Clarear', 0, -1, 1, decimals: 3, dragStep: .002),
      'smooth_source': EffectParam('Suavizar fonte', 0, 0, 100, decimals: 1, dragStep: .05, relative: true),
      'dots_shift_x': EffectParam('Deslocar X', 0, -5000, 5000, decimals: 1, relative: true),
      'dots_shift_y': EffectParam('Deslocar Y', 0, -5000, 5000, decimals: 1, relative: true),
    },
    montar: ['dots_frequency', 'dots_angle', 'dots_sharpness'],
    presets: [
      EffectPronto('Jornal', {'dots_frequency': 90, 'dots_angle': 45}),
      EffectPronto('Pontos grandes', {'dots_frequency': 25}),
      EffectPronto('Pontos brancos', {'dots': 1}),
    ],
  ),
  EffectType.edgeColorize: EffectSpec(
    id: 's_edge_colorize',
    name: 'S_EdgeColorize',
    category: 'Stylize',
    hasColor: true,
    defaultColor: Color(0xFF000000),
    extraColors: 4,
    defaultExtraColors: [
      Color(0xFFFFD97F), // Top    [1 .85 .5]
      Color(0xFF00197F), // Right  [0 .1 .5]
      Color(0xFF4C4C4C), // Bottom [.3 .3 .3]
      Color(0xFF7F0000), // Left   [.5 0 0]
    ],
    colorLabels: ['Fundo', 'Topo', 'Direita', 'Base', 'Esquerda'],
    synonyms: ['edge colorize', 'bordas coloridas', 'contorno', 'neon', 'sapphire'],
    params: {
      'edge_smooth': EffectParam('Suavizar bordas', 5.376, 0, 200, decimals: 2, dragStep: .02, relative: true),
      'subpixel_smooth': EffectParam('Suavizar subpixel', 1, 0, 1, kind: ParamKind.toggle),
      'brightness': EffectParam('Brilho', 1, 0, 20, decimals: 2, dragStep: .005),
      'rotate_colors': EffectParam('Girar cores', 0, -360, 360, unit: '°', decimals: 1, dragStep: .2),
    },
    montar: ['edge_smooth', 'brightness', 'rotate_colors'],
    presets: [
      EffectPronto('Neon', {'brightness': 2.5}),
      EffectPronto('Bordas grossas', {'edge_smooth': 14}),
      EffectPronto('Girado', {'rotate_colors': 90}),
    ],
  ),
};

/// Modos do `shaders/estilizar.frag`.
const modoDeEstilo = <EffectType, int>{
  EffectType.threshold: 1,
  EffectType.thresholdRgb: 2,
  EffectType.vignette: 3,
  EffectType.blockLoad: 4,
  EffectType.scanLines: 5,
  EffectType.halfTone: 6,
  EffectType.edgeColorize: 7,
};

/// Os numeros de um efeito de Estilizar no instante, na ordem do shader:
/// modo + 16 floats (p0..p3). As cores vao a parte.
class QuadroDeEstilo {
  const QuadroDeEstilo(this.modo, this.valores, this.cores);

  final int modo;
  final List<double> valores;

  /// Ate cinco cores RGBA (0..1).
  final List<Color> cores;

  static QuadroDeEstilo? de(EffectInstance e, Duration local) {
    final modo = modoDeEstilo[e.type];
    if (modo == null) return null;
    double v(String k) {
      final p = e.spec.params[k]!;
      final bruto = e.paramAt(k, local);
      if (!bruto.isFinite) return p.initial;
      return bruto.clamp(p.min, p.max).toDouble();
    }

    const graus = math.pi / 180;
    final valores = switch (e.type) {
      EffectType.threshold => [
        v('threshold') / 255,
        v('channel').roundToDouble(),
        v('invert'),
        v('blend') / 100,
      ],
      EffectType.thresholdRgb => [
        v('red_threshold') / 255,
        v('green_threshold') / 255,
        v('blue_threshold') / 255,
        v('blend') / 100,
        v('invert_red'),
        v('invert_green'),
        v('invert_blue'),
      ],
      EffectType.vignette => [
        v('amount') / 100,
        // tan(metade do angulo) * 1,046 — o fator medido no render.
        math.tan(v('angle_of_view') * graus / 2) * 1.046,
        v('center_x'),
        v('center_y'),
        v('pin_highlights') / 100,
      ],
      EffectType.blockLoad => [
        v('completion') / 100,
        v('scans').roundToDouble(),
        v('start_cleared'),
        v('bilinear'),
      ],
      EffectType.scanLines => [
        v('lines_frequency'),
        v('lines_sharpness'),
        v('lines_angle') * graus,
        v('lines_shift'),
        v('shift_red'),
        v('shift_green'),
        v('shift_blue'),
        v('add_noise'),
        v('noise_freq_rel'),
        v('brightness'),
        v('offset'),
        v('gamma'),
        v('saturation'),
        v('smooth_source'),
      ],
      EffectType.halfTone => [
        v('dots'),
        v('dots_frequency'),
        v('dots_angle') * graus,
        v('dots_rel_width'),
        v('dots_sharpness'),
        v('dots_lighten'),
        v('smooth_source'),
        v('dots_shift_x'),
        v('dots_shift_y'),
      ],
      EffectType.edgeColorize => [
        v('edge_smooth'),
        v('brightness'),
        v('rotate_colors') * graus,
      ],
      _ => const <double>[],
    };
    return QuadroDeEstilo(modo, valores, [e.color, ...e.extraColors]);
  }
}

/// Referencias em Dart das contas por pixel (os testes comparam com o
/// shader). Entrada e saida 0..1, cor sem pre-multiplicar.
double lumaRec601(double r, double g, double b) =>
    .299 * r + .587 * g + .114 * b;

/// S_ScanLines num pixel: [v] e a distancia (em texels, para cima) ao
/// centro medida perpendicular as linhas; [periodo] em texels.
double scanLineCanal(
  double entrada,
  double v, {
  required double periodo,
  required double nitidez,
  required double gama,
  double deslocar = 0,
}) {
  final p = (.5 + .5 * nitidez * math.sin(2 * math.pi * (v / periodo + deslocar)))
      .clamp(0.0, 1.0);
  final x = math.pow(entrada.clamp(0.0, 1.0), gama).toDouble();
  return x + (2 * p - 1) * math.min(x, 1 - x);
}

/// CC Vignette: o fator no raio [r] (texels) para meia largura [meia].
double vinhetaFator(double r, double meia, double quantidade, double tanMeioAngulo) {
  final t = r / meia * tanMeioAngulo;
  final f = 1 / math.pow(1 + t * t, 2); // cos^4(atan t)
  return 1 - quantidade * (1 - f);
}
