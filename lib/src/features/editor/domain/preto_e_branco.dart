import 'dart:ui' show Color;

import 'effect.dart';

/// BLACK & WHITE (19/09), a conta do After Effects.
///
/// SEIS FAIXAS DE COR dizem quanto cada familia entra no cinza. Nao e
/// `rgb -> luminancia`: um vermelho puro com Vermelhos em 40 sai cinza
/// escuro, com 100 sai a luminancia natural da cor, com 200 sai claro e
/// com -100 sai preto. Quem tira a cor e a PESSOA, faixa por faixa — que
/// e o que faz um ceu azul ficar dramatico e uma pele continuar clara no
/// mesmo ajuste.
///
/// CINZA PURO NAO SE MEXE. A faixa manda misturada pela saturacao do
/// pixel: cor cheia obedece a faixa inteira, cinza nao obedece a
/// ninguem. Sem isso, "tirar a cor" viraria "lavar a imagem".
///
/// A CONTA E A MESMA DO SHADER, linha a linha: `shaders/preto_e_branco.frag`.
const efeitosPretoEBranco = <EffectType, EffectSpec>{
  EffectType.pretoEBranco: EffectSpec(
    id: 'black_and_white',
    name: 'Black & White',
    category: 'Color',
    hasColor: true,
    // SEPIA DE NASCENCA, e nao branco: o tingimento nasce DESLIGADO
    // (Tingir 0), entao a cor so entra quando a pessoa pede — e quando
    // pede sem escolher, o que ela quer ver e sepia, nao um cinza que
    // nao mudou nada.
    defaultColor: kCorDoSepia,
    colorLabels: ['Cor do tingimento'],
    synonyms: [
      'black and white', 'preto e branco', 'pb', 'monocromatico',
      'escala de cinza', 'grayscale', 'dessaturar', 'tirar a cor',
      'black & white', 'b&w',
    ],
    params: {
      // AS FAIXAS. -200 estoura para o preto, 300 lava para o branco; os
      // padroes sao os do After Effects.
      'reds': EffectParam('Vermelhos', 40, -200, 300, decimals: 1, dragStep: .5),
      'yellows': EffectParam('Amarelos', 60, -200, 300, decimals: 1, dragStep: .5),
      'greens': EffectParam('Verdes', 40, -200, 300, decimals: 1, dragStep: .5),
      'cyans': EffectParam('Ciano', 60, -200, 300, decimals: 1, dragStep: .5),
      'blues': EffectParam('Azuis', 20, -200, 300, decimals: 1, dragStep: .5),
      'magentas': EffectParam('Magentas', 80, -200, 300, decimals: 1, dragStep: .5),
      'tint': EffectParam('Tingir', 0, 0, 100, unit: '%', decimals: 1, dragStep: .2),
      'mix': EffectParam('Mistura', 100, 0, 100, unit: '%', decimals: 1, dragStep: .2),
    },
    montar: ['reds', 'blues', 'mix'],
    presets: [
      EffectPronto('Padrão', {}),
      // CEU ESCURO, PELE CLARA: o azul cai e o vermelho sobe — o ajuste
      // classico de paisagem, e a prova de que as faixas sao
      // independentes.
      EffectPronto('Céu dramático', {'blues': -60, 'cyans': -20, 'reds': 90, 'yellows': 80}),
      EffectPronto('Alto contraste', {'reds': 140, 'yellows': 130, 'greens': 20, 'blues': -80, 'magentas': 20}),
      EffectPronto('Sépia', {'tint': 100, 'reds': 60, 'yellows': 80}),
    ],
  ),
};

/// A COR DE NASCENCA do tingimento quando a pessoa liga sem escolher:
/// o sepia do papel velho, e nao o branco do nascimento do efeito.
const Color kCorDoSepia = Color(0xFFE8C79A);

/// Os doze floats do `shaders/preto_e_branco.frag`: as seis faixas em
/// fracao, a mistura, o tingimento e a cor.
List<double> valoresPretoEBranco(EffectInstance e, Duration local) {
  final spec = efeitosPretoEBranco[e.type]!.params;
  double v(String k) {
    final p = spec[k];
    final bruto = e.paramAt(k, local);
    if (p == null) return bruto.isFinite ? bruto : 0;
    if (!bruto.isFinite) return p.initial;
    return bruto.clamp(p.min, p.max).toDouble();
  }

  final cor = e.color;
  return [
    v('reds') / 100,
    v('yellows') / 100,
    v('greens') / 100,
    v('cyans') / 100,
    v('blues') / 100,
    v('magentas') / 100,
    v('mix') / 100,
    v('tint') / 100,
    cor.r,
    cor.g,
    cor.b,
    0,
  ];
}
