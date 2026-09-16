import 'dart:math' as math;

import 'effect.dart';

/// CORRECAO DE COR, RECOMECADA DO ZERO (16/09, pedido do dono: "os
/// primeiros efeitos de correcao de cor, poderosos e otimizados; o
/// principal e o Unsharp Mask").
///
/// Cinco efeitos com os nomes, as faixas e a conta do After Effects:
///
///   Unsharp Mask           quantidade 0..500 %, raio em px, limiar 0..255
///   Levels                 entrada e saida 0..255, gama
///   Brightness & Contrast  brilho -150..150, contraste -100..100, legado
///   Hue/Saturation         matiz, saturacao e luminosidade; colorir
///   Exposure               stops em LUZ LINEAR, deslocamento, gama
///
/// ONDE A CONTA RODA. Os quatro de cor olham um pixel so, e por isso
/// FUNDEM: Levels + Hue/Saturation + Exposure em sequencia viram UMA
/// passada de GPU (`shaders/correcao_de_cor.frag`), e nao tres texturas
/// fora da tela. De quebra, sem o arredondamento de 8 bits entre um
/// efeito e o outro. O Unsharp Mask precisa da vizinhanca e tem shader
/// proprio (`shaders/unsharp_mask.frag`).
///
/// Cada conta existe aqui tambem, em Dart, com os MESMOS uniformes que o
/// shader recebe: os testes comparam as duas pixel a pixel.
const efeitosDeCorrecaoDeCor = <EffectType, EffectSpec>{
  EffectType.unsharpMask: EffectSpec(
    id: 'unsharp_mask',
    name: 'Unsharp Mask',
    category: 'Color',
    cost: 2,
    synonyms: [
      'nitidez',
      'sharpen',
      'afiar',
      'unsharp',
      'mascara de nitidez',
      'mascara de desfocagem',
      'usm',
      'detalhe',
      'foco',
      'blur & sharpen',
      'clareza',
    ],
    params: {
      'amount': EffectParam(
        'Quantidade',
        50,
        0,
        500,
        unit: '%',
        decimals: 0,
        dragStep: .5,
      ),
      'radius': EffectParam(
        'Raio',
        1,
        0.1,
        100,
        relative: true,
        decimals: 1,
        dragStep: .02,
      ),
      'threshold': EffectParam('Limiar', 0, 0, 255, decimals: 0, dragStep: .2),
      'luma_only': EffectParam(
        'Só luminância',
        0,
        0,
        1,
        kind: ParamKind.toggle,
      ),
    },
    montar: ['amount', 'radius', 'threshold'],
    presets: [
      EffectPronto('Nitidez leve', {'amount': 40, 'radius': 1.0}),
      EffectPronto('Nitidez forte', {
        'amount': 120,
        'radius': 1.5,
        'threshold': 2,
      }),
      EffectPronto('Clareza', {'amount': 30, 'radius': 25}),
    ],
  ),
  EffectType.levels: EffectSpec(
    id: 'levels',
    name: 'Levels',
    category: 'Color',
    synonyms: [
      'niveis',
      'níveis',
      'levels',
      'gama',
      'contraste',
      'ponto preto',
      'ponto branco',
    ],
    params: {
      'input_black': EffectParam('Entrada preto', 0, 0, 255, decimals: 1),
      'input_white': EffectParam('Entrada branco', 255, 0, 255, decimals: 1),
      'gamma': EffectParam('Gama', 1, 0.1, 10, decimals: 2, dragStep: .004),
      'output_black': EffectParam('Saída preto', 0, 0, 255, decimals: 1),
      'output_white': EffectParam('Saída branco', 255, 0, 255, decimals: 1),
    },
    montar: ['input_black', 'input_white', 'gamma'],
    presets: [
      EffectPronto('Mais contraste', {'input_black': 16, 'input_white': 235}),
      EffectPronto('Clarear meios-tons', {'gamma': 1.35}),
      EffectPronto('Preto lavado', {'output_black': 28}),
    ],
  ),
  EffectType.brightnessContrast: EffectSpec(
    id: 'brightness_contrast',
    name: 'Brightness & Contrast',
    category: 'Color',
    synonyms: [
      'brilho',
      'contraste',
      'brilho e contraste',
      'brightness',
      'contrast',
      'brightness contrast',
    ],
    params: {
      'brightness': EffectParam('Brilho', 0, -150, 150, decimals: 1),
      'contrast': EffectParam('Contraste', 0, -100, 100, decimals: 1),
      'use_legacy': EffectParam(
        'Modo legado',
        0,
        0,
        1,
        kind: ParamKind.toggle,
      ),
    },
    montar: ['brightness', 'contrast'],
    presets: [
      EffectPronto('Contraste suave', {'contrast': 20}),
      EffectPronto('Punch', {'brightness': 10, 'contrast': 35}),
      EffectPronto('Desbotado', {'brightness': 15, 'contrast': -30}),
    ],
  ),
  EffectType.hueSaturation: EffectSpec(
    id: 'hue_saturation',
    name: 'Hue/Saturation',
    category: 'Color',
    synonyms: [
      'matiz',
      'saturacao',
      'saturação',
      'luminosidade',
      'colorir',
      'colorize',
      'dessaturar',
      'preto e branco',
      'sepia',
      'hue',
      'saturation',
    ],
    params: {
      'master_hue': EffectParam('Matiz', 0, -180, 180, unit: '°', decimals: 1),
      'master_saturation': EffectParam('Saturação', 0, -100, 100, decimals: 0),
      'master_lightness': EffectParam(
        'Luminosidade',
        0,
        -100,
        100,
        decimals: 0,
      ),
      'colorize': EffectParam('Colorir', 0, 0, 1, kind: ParamKind.toggle),
      'colorize_hue': EffectParam(
        'Matiz ao colorir',
        0,
        0,
        360,
        unit: '°',
        decimals: 1,
      ),
      'colorize_saturation': EffectParam(
        'Saturação ao colorir',
        25,
        0,
        100,
        decimals: 0,
      ),
      'colorize_lightness': EffectParam(
        'Luminosidade ao colorir',
        0,
        -100,
        100,
        decimals: 0,
      ),
    },
    montar: ['master_hue', 'master_saturation', 'master_lightness'],
    presets: [
      EffectPronto('Preto e branco', {'master_saturation': -100}),
      EffectPronto('Sépia', {
        'colorize': 1,
        'colorize_hue': 35,
        'colorize_saturation': 30,
      }),
      EffectPronto('Cores vivas', {'master_saturation': 35}),
    ],
  ),
  EffectType.exposure: EffectSpec(
    id: 'exposure',
    name: 'Exposure',
    category: 'Color',
    synonyms: [
      'exposicao',
      'exposição',
      'stops',
      'luz',
      'clarear',
      'escurecer',
      'exposure',
    ],
    params: {
      'exposure': EffectParam(
        'Exposição',
        0,
        -10,
        10,
        decimals: 2,
        dragStep: .01,
      ),
      'offset': EffectParam(
        'Deslocamento',
        0,
        -1,
        1,
        decimals: 3,
        dragStep: .0005,
      ),
      'gamma': EffectParam(
        'Correção de gama',
        1,
        0.1,
        9.99,
        decimals: 2,
        dragStep: .004,
      ),
      'bypass_linear': EffectParam(
        'Ignorar luz linear',
        0,
        0,
        1,
        kind: ParamKind.toggle,
      ),
    },
    montar: ['exposure', 'offset', 'gamma'],
    presets: [
      EffectPronto('Mais um stop', {'exposure': 1}),
      EffectPronto('Menos um stop', {'exposure': -1}),
      EffectPronto('Abrir sombras', {'gamma': 1.25}),
    ],
  ),
};

/// Os que olham UM pixel e por isso fundem numa passada so.
const efeitosDeCorPorPixel = <EffectType>{
  EffectType.levels,
  EffectType.brightnessContrast,
  EffectType.hueSaturation,
  EffectType.exposure,
};

/// Quantas operacoes cabem numa passada do shader de cor.
const operacoesPorPassada = 4;

/// Os modos do shader de cor. A ORDEM DOS SLOTS e contrato com
/// `shaders/correcao_de_cor.frag`: cada operacao sao dois vec4, `a` e
/// `b`, com o modo em a.x.
abstract final class ModoDeCor {
  static const niveis = 1;
  static const brilhoContraste = 2;
  static const matizSaturacao = 3;
  static const exposicao = 4;
}

typedef CorRgb = ({double r, double g, double b});

/// UMA OPERACAO DE COR com os numeros ja no formato do shader.
///
/// O que da para calcular uma vez por quadro (1/gama, 2^stops, a
/// extensao da entrada) e calculado aqui, e nao dois milhoes de vezes
/// por quadro na GPU.
class OperacaoDeCor {
  const OperacaoDeCor(this.modo, this.a, this.b);

  final int modo;

  /// a.y, a.z, a.w.
  final List<double> a;

  /// b.x, b.y, b.z, b.w.
  final List<double> b;

  /// Os oito floats, na ordem em que o shader os declara.
  List<double> get uniformes => [
    modo.toDouble(),
    for (var i = 0; i < 3; i++) i < a.length ? a[i] : 0,
    for (var i = 0; i < 4; i++) i < b.length ? b[i] : 0,
  ];

  /// A operacao do efeito no instante [local], ou NULA quando os numeros
  /// deixam a imagem intacta — tudo no neutro nao paga conta nenhuma.
  static OperacaoDeCor? de(EffectInstance e, Duration local) {
    double v(String k) {
      final bruto = e.paramAt(k, local);
      final p = e.spec.params[k];
      if (p == null) return bruto.isFinite ? bruto : 0;
      if (!bruto.isFinite) return p.initial;
      return bruto.clamp(p.min, p.max).toDouble();
    }

    switch (e.type) {
      case EffectType.levels:
        return niveis(
          entradaPreto: v('input_black'),
          entradaBranco: v('input_white'),
          gama: v('gamma'),
          saidaPreto: v('output_black'),
          saidaBranco: v('output_white'),
        );
      case EffectType.brightnessContrast:
        return brilhoContraste(
          brilho: v('brightness'),
          contraste: v('contrast'),
          legado: v('use_legacy') >= 0.5,
        );
      case EffectType.hueSaturation:
        return matizSaturacao(
          matiz: v('master_hue'),
          saturacao: v('master_saturation'),
          luminosidade: v('master_lightness'),
          colorir: v('colorize') >= 0.5,
          matizColorir: v('colorize_hue'),
          saturacaoColorir: v('colorize_saturation'),
          luminosidadeColorir: v('colorize_lightness'),
        );
      case EffectType.exposure:
        return exposicao(
          stops: v('exposure'),
          deslocamento: v('offset'),
          gama: v('gamma'),
          ignorarLinear: v('bypass_linear') >= 0.5,
        );
      default:
        return null;
    }
  }

  /// LEVELS (After Effects, 8 bpc): tudo em 0..255.
  ///
  ///   x = clamp((v - entradaPreto) / (entradaBranco - entradaPreto))
  ///   v' = saidaPreto + (saidaBranco - saidaPreto) * x^(1/gama)
  ///
  /// Entrada invertida (preto maior que branco) inverte a imagem, como
  /// no AE. Entrada com extensao zero vira um corte seco.
  static OperacaoDeCor? niveis({
    required double entradaPreto,
    required double entradaBranco,
    required double gama,
    required double saidaPreto,
    required double saidaBranco,
  }) {
    if (entradaPreto.abs() < 1e-4 &&
        (entradaBranco - 255).abs() < 1e-4 &&
        (gama - 1).abs() < 1e-4 &&
        saidaPreto.abs() < 1e-4 &&
        (saidaBranco - 255).abs() < 1e-4) {
      return null;
    }
    final extensao = (entradaBranco - entradaPreto) / 255;
    final inverso = extensao.abs() < 1e-6
        ? 1e6
        : 1 / extensao; // corte seco, com o sinal certo
    return OperacaoDeCor(
      ModoDeCor.niveis,
      [entradaPreto / 255, inverso, 1 / gama.clamp(0.01, 100)],
      [saidaPreto / 255, (saidaBranco - saidaPreto) / 255],
    );
  }

  /// BRIGHTNESS & CONTRAST.
  ///
  /// LEGADO (o do AE antigo e do Photoshop antes do CS3): o brilho soma
  /// niveis a todos os pixels e estoura; o contraste escala em torno do
  /// cinza medio — no +100 vira um corte seco.
  ///
  /// NORMAL: ajuste proporcional, que nunca tira o preto do preto nem o
  /// branco do branco. O brilho e uma curva de gama (so os meios-tons
  /// andam); o contraste e uma curva em S em torno do cinza medio, com
  /// inclinacao 2^(contraste/70) no pivo. A Adobe nao publica a curva
  /// exata do modo novo; esta segue o comportamento descrito na ajuda
  /// (sem recorte de sombras e altas) e nunca inverte nada.
  static OperacaoDeCor? brilhoContraste({
    required double brilho,
    required double contraste,
    required bool legado,
  }) {
    if (brilho.abs() < 1e-4 && contraste.abs() < 1e-4) return null;
    if (legado) {
      final inclinacao = contraste > 0
          ? 1 / math.max(1 - contraste / 100, 1 / 255)
          : 1 + contraste / 100;
      return OperacaoDeCor(ModoDeCor.brilhoContraste, [
        1,
        brilho / 255,
        inclinacao,
      ], const []);
    }
    return OperacaoDeCor(ModoDeCor.brilhoContraste, [
      0,
      gamaDoBrilho(brilho),
      inclinacaoDoContraste(contraste),
    ], const []);
  }

  /// O expoente da curva de brilho: +150 leva o cinza medio a ~200/255,
  /// -150 a ~36/255, simetrico em stops.
  static double gamaDoBrilho(double brilho) =>
      math.pow(2, -brilho.clamp(-150, 150) / 100 * 1.5).toDouble();

  static double inclinacaoDoContraste(double contraste) =>
      math.pow(2, contraste.clamp(-100, 100) / 70).toDouble();

  /// HUE/SATURATION (o algoritmo do Photoshop, que o AE segue).
  ///
  /// Matiz gira no hexcone HSL. Saturacao positiva empurra o pixel para
  /// longe do cinza de mesma luminosidade ate a saturacao cheia (o ganho
  /// para ali — nunca inverte); negativa puxa para o cinza. Luminosidade
  /// mistura com o branco ou com o preto. COLORIR troca matiz e
  /// saturacao de todos os pixels e guarda so a luminancia.
  static OperacaoDeCor? matizSaturacao({
    required double matiz,
    required double saturacao,
    required double luminosidade,
    required bool colorir,
    required double matizColorir,
    required double saturacaoColorir,
    required double luminosidadeColorir,
  }) {
    if (colorir) {
      return OperacaoDeCor(
        ModoDeCor.matizSaturacao,
        [1, (matizColorir / 360) % 1.0, saturacaoColorir.clamp(0, 100) / 100],
        [luminosidadeColorir.clamp(-100, 100) / 100],
      );
    }
    if (matiz.abs() < 1e-4 &&
        saturacao.abs() < 1e-4 &&
        luminosidade.abs() < 1e-4) {
      return null;
    }
    return OperacaoDeCor(
      ModoDeCor.matizSaturacao,
      [0, (matiz / 360) % 1.0, saturacao.clamp(-100, 100) / 100],
      [luminosidade.clamp(-100, 100) / 100],
    );
  }

  /// EXPOSURE, EM LUZ LINEAR — e o que quase todo clone erra.
  ///
  /// Um stop e o dobro de LUZ, nao o dobro do numero guardado no pixel.
  /// A conta desfaz a curva do sRGB, faz (v * 2^stops + deslocamento) ^
  /// (1/gama) e refaz a curva. "Ignorar luz linear" faz a mesma conta
  /// direto no numero guardado, como o AE.
  static OperacaoDeCor? exposicao({
    required double stops,
    required double deslocamento,
    required double gama,
    required bool ignorarLinear,
  }) {
    if (stops.abs() < 1e-5 &&
        deslocamento.abs() < 1e-6 &&
        (gama - 1).abs() < 1e-5) {
      return null;
    }
    return OperacaoDeCor(
      ModoDeCor.exposicao,
      [
        ignorarLinear ? 0 : 1,
        math.pow(2, stops).toDouble(),
        deslocamento,
      ],
      [1 / gama.clamp(0.01, 100)],
    );
  }
}

/// A sequencia de operacoes de cor de uma pilha de efeitos contiguos,
/// sem as neutras e sem as desligadas.
List<OperacaoDeCor> operacoesDeCor(
  Iterable<EffectInstance> efeitos,
  Duration local,
) => [
  for (final e in efeitos)
    if (e.enabled && efeitosDeCorPorPixel.contains(e.type))
      ?OperacaoDeCor.de(e, local),
];

// --------------------------------------------------------------------
// A MESMA CONTA DO SHADER, EM DART. Espelho linha a linha de
// `shaders/correcao_de_cor.frag`; os testes comparam os dois.

const _lumaR = .2126, _lumaG = .7152, _lumaB = .0722;

double _clamp01(double v) => v < 0 ? 0 : (v > 1 ? 1 : v);

CorRgb _clampRgb(CorRgb c) => (r: _clamp01(c.r), g: _clamp01(c.g), b: _clamp01(c.b));

double srgbParaLinear(double c) =>
    c <= 0.04045 ? c / 12.92 : math.pow((c + 0.055) / 1.055, 2.4).toDouble();

double linearParaSrgb(double c) {
  if (c < 0) c = 0;
  return c <= 0.0031308
      ? c * 12.92
      : 1.055 * math.pow(c, 1 / 2.4).toDouble() - 0.055;
}

({double h, double s, double l}) rgbParaHsl(CorRgb c) {
  final mx = math.max(c.r, math.max(c.g, c.b));
  final mn = math.min(c.r, math.min(c.g, c.b));
  final l = (mx + mn) * .5, d = mx - mn;
  var h = 0.0, s = 0.0;
  if (d > .00001) {
    s = l > .5 ? d / math.max(2 - mx - mn, .00001) : d / math.max(mx + mn, .00001);
    if (mx == c.r) {
      h = (c.g - c.b) / d + (c.g < c.b ? 6 : 0);
    } else if (mx == c.g) {
      h = (c.b - c.r) / d + 2;
    } else {
      h = (c.r - c.g) / d + 4;
    }
    h /= 6;
  }
  return (h: h, s: s, l: l);
}

CorRgb hslParaRgb(double h, double s, double l) {
  double canal(double n) {
    final k = ((h * 6 + n) % 6 - 3).abs() - 1;
    final kk = k < 0 ? 0.0 : (k > 1 ? 1.0 : k);
    return l + (1 - (2 * l - 1).abs()) * s * (kk - .5);
  }

  return (r: canal(0), g: canal(4), b: canal(2));
}

CorRgb aplicarOperacaoDeCor(CorRgb c, OperacaoDeCor op) {
  final u = op.uniformes;
  final a = [u[0], u[1], u[2], u[3]], b = [u[4], u[5], u[6], u[7]];
  switch (op.modo) {
    case ModoDeCor.niveis:
      double canal(double v) {
        var x = _clamp01((v - a[1]) * a[2]);
        if ((a[3] - 1).abs() > 0.0001) x = math.pow(x, a[3]).toDouble();
        return b[0] + b[1] * x;
      }
      c = (r: canal(c.r), g: canal(c.g), b: canal(c.b));
    case ModoDeCor.brilhoContraste:
      if (a[1] > .5) {
        double canal(double v) => (v + a[2] - .5) * a[3] + .5;
        c = (r: canal(c.r), g: canal(c.g), b: canal(c.b));
      } else {
        double canal(double v) {
          var x = _clamp01(v);
          if ((a[2] - 1).abs() > 0.0001) x = math.pow(x, a[2]).toDouble();
          if ((a[3] - 1).abs() > 0.0001) {
            x = x < .5
                ? .5 * math.pow(2 * x, a[3]).toDouble()
                : 1 - .5 * math.pow(2 - 2 * x, a[3]).toDouble();
          }
          return x;
        }

        c = (r: canal(c.r), g: canal(c.g), b: canal(c.b));
      }
    case ModoDeCor.matizSaturacao:
      c = _clampRgb(c);
      final claro = b[0];
      if (a[1] > .5) {
        var y = c.r * _lumaR + c.g * _lumaG + c.b * _lumaB;
        y = claro >= 0 ? y + (1 - y) * claro : y * (1 + claro);
        c = hslParaRgb(a[2], a[3], y);
      } else {
        if (a[2].abs() > .00001) {
          final hsl = rgbParaHsl(c);
          c = hslParaRgb((hsl.h + a[2]) % 1.0, hsl.s, hsl.l);
        }
        final sat = a[3];
        if (sat.abs() > .00001) {
          final mx = math.max(c.r, math.max(c.g, c.b));
          final mn = math.min(c.r, math.min(c.g, c.b));
          final l = (mx + mn) * .5, d = mx - mn;
          final s = d < .00001
              ? 0.0
              : (l > .5
                    ? d / math.max(2 - mx - mn, .00001)
                    : d / math.max(mx + mn, .00001));
          final k = sat < 0 ? sat : 1 / math.max(math.max(1 - sat, s), .0001) - 1;
          c = (r: c.r + (c.r - l) * k, g: c.g + (c.g - l) * k, b: c.b + (c.b - l) * k);
        }
        if (claro.abs() > .00001) {
          double canal(double v) => claro >= 0 ? v + (1 - v) * claro : v * (1 + claro);
          c = (r: canal(c.r), g: canal(c.g), b: canal(c.b));
        }
      }
    case ModoDeCor.exposicao:
      final linear = a[1] > .5;
      double canal(double v) {
        var x = linear ? srgbParaLinear(_clamp01(v)) : v;
        x = math.max(x * a[2] + a[3], 0);
        if ((b[0] - 1).abs() > 0.0001) x = math.pow(x, b[0]).toDouble();
        return linear ? linearParaSrgb(x) : x;
      }
      c = (r: canal(c.r), g: canal(c.g), b: canal(c.b));
  }
  return _clampRgb(c);
}

// --------------------------------------------------------------------
// UNSHARP MASK

/// Os numeros do Unsharp Mask num instante, ou nulo quando a quantidade
/// e zero (a imagem sai intacta e a passada nem acontece).
class ParametrosDeNitidez {
  const ParametrosDeNitidez({
    required this.quantidade,
    required this.raio,
    required this.limiar,
    required this.soLuminancia,
  });

  /// Fracao: 50 % vira 0,5.
  final double quantidade;

  /// Pixel pensado em 1080p (o menor lado da composicao).
  final double raio;

  /// Fracao de 0..1 (o AE mostra em niveis 0..255).
  final double limiar;

  final bool soLuminancia;

  static ParametrosDeNitidez? de(EffectInstance e, Duration local) {
    double v(String k) {
      final bruto = e.paramAt(k, local);
      final p = e.spec.params[k]!;
      if (!bruto.isFinite) return p.initial;
      return bruto.clamp(p.min, p.max).toDouble();
    }

    final quantidade = v('amount') / 100;
    if (quantidade < 1e-4) return null;
    return ParametrosDeNitidez(
      quantidade: quantidade,
      raio: v('radius'),
      limiar: v('threshold') / 255,
      soLuminancia: v('luma_only') >= .5,
    );
  }
}

/// QUANTAS AMOSTRAS o desfoque do Unsharp Mask pode gastar por pixel
/// quando o raio passa da vizinhanca exata. Tocando, o minimo; parado, o
/// meio; exportando, o maximo — no raio pequeno (o uso comum) as tres
/// dao exatamente a mesma imagem.
abstract final class AmostrasDeNitidez {
  static const rascunho = 16;
  static const previa = 24;
  static const exportacao = 48;
}

/// Os limites em sigma (texels) de cada nucleo exato do shader.
const sigmaNucleo3x3 = .62, sigmaNucleo21 = 1.12, sigmaNucleo37 = 1.62;

/// UNSHARP MASK DE REFERENCIA, para os testes: a mesma conta do shader
/// nos nucleos exatos (sigma abaixo de [sigmaNucleo37] texels).
///
/// [rgba] e pre-multiplicado, 0..1, linha a linha. O raio ja vem em
/// texels (o shader converte px em 1080p para texel sozinho).
List<double> unsharpMaskReferencia(
  List<double> rgba,
  int largura,
  int altura, {
  required double sigma,
  required double quantidade,
  double limiar = 0,
  bool soLuminancia = false,
}) {
  assert(sigma < sigmaNucleo37, 'a referencia cobre so os nucleos exatos');
  final alcance = sigma < sigmaNucleo3x3 ? 1 : (sigma < sigmaNucleo21 ? 2 : 3);
  final raio2 = sigma < sigmaNucleo3x3 ? 2 : (sigma < sigmaNucleo21 ? 5 : 10);
  final k = -.5 / (sigma * sigma);
  final saida = List<double>.filled(rgba.length, 0);
  double texel(int x, int y, int c) {
    final xx = x.clamp(0, largura - 1), yy = y.clamp(0, altura - 1);
    return rgba[(yy * largura + xx) * 4 + c];
  }

  for (var y = 0; y < altura; y++) {
    for (var x = 0; x < largura; x++) {
      final soma = [0.0, 0.0, 0.0, 0.0];
      var peso = 0.0;
      for (var j = -alcance; j <= alcance; j++) {
        for (var i = -alcance; i <= alcance; i++) {
          final d2 = i * i + j * j;
          if (d2 > raio2) continue;
          final w = math.exp(k * d2);
          for (var c = 0; c < 4; c++) {
            soma[c] += texel(x + i, y + j, c) * w;
          }
          peso += w;
        }
      }
      final o = [for (var c = 0; c < 4; c++) texel(x, y, c)];
      final d = [for (var c = 0; c < 4; c++) o[c] - soma[c] / peso];
      final r = List<double>.filled(4, 0);
      double peneira(double dif) {
        if (limiar <= 0) return 1;
        const meio = .5 / 255;
        final t = _clamp01((dif.abs() - (limiar - meio)) / (2 * meio));
        return t * t * (3 - 2 * t);
      }

      if (soLuminancia) {
        final dy = d[0] * _lumaR + d[1] * _lumaG + d[2] * _lumaB;
        final w = peneira(dy);
        for (var c = 0; c < 3; c++) {
          r[c] = o[c] + dy * quantidade * w;
        }
        r[3] = o[3];
      } else {
        for (var c = 0; c < 4; c++) {
          r[c] = o[c] + d[c] * quantidade * peneira(d[c]);
        }
      }
      final a = _clamp01(r[3]);
      final i0 = (y * largura + x) * 4;
      for (var c = 0; c < 3; c++) {
        saida[i0 + c] = r[c].clamp(0.0, a);
      }
      saida[i0 + 3] = a;
    }
  }
  return saida;
}
