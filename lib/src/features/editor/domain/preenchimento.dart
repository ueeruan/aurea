import 'dart:ui' show Color;

import 'effect.dart';

/// PREENCHIMENTO (ADBE Fill), LIDO DO AFTER EFFECTS.
///
/// O QUE ELE FAZ: troca a COR de tudo o que a camada desenha, e **preserva o
/// alfa**. Nao e "pintar um retangulo por cima": o que e transparente
/// continua transparente, e o que tem meia opacidade continua com meia
/// opacidade. E o efeito que transforma um logo colorido na versao de uma
/// cor so, e e o que faz um recorte (chroma key) virar silhueta chapada.
///
/// MEDIDO CONTRA RENDER DO AE (build/qa/ae-novos/Fill.png, 128x128, um
/// solido branco de 48x48 em (44,44), cor azul e opacidade 1):
///
///   * o quadrado inteiro saiu `(0, 0, 255, 255)` — a cor pedida, e nao
///     uma mistura com o branco de origem;
///   * o pixel (100,100), FORA do quadrado, saiu `(0, 0, 0, 0)` — o alfa
///     de origem foi preservado, e o preenchimento nao pintou o quadro
///     inteiro.
///
/// A CONTA, entao, e `rgb = cor` e `a = a_origem * opacidade`. O que da
/// trabalho e a DIFUSAO: os dois parametros espalham a mascara do
/// preenchimento, e sao o unico lugar onde a cor nao entra chapada.
///
/// FICOU DE FORA: a mascara de preenchimento (o Aurea mascara a camada por
/// conta propria, e um seletor de mascara aqui seria um segundo sistema
/// fazendo a mesma coisa) e o `Inverter`, que inverte a mascara — sem
/// mascara escolhida ele nao tem o que inverter.
const efeitosPreenchimento = <EffectType, EffectSpec>{
  EffectType.preenchimento: EffectSpec(
    id: 'adbe_fill',
    name: 'Preenchimento',
    category: 'Generate',
    hasColor: true,
    defaultColor: Color(0xFF7C62FF),
    synonyms: [
      'fill',
      'preenchimento',
      'preencher',
      'cor solida',
      'silhueta',
      'chapa',
      'cor unica',
    ],
    params: {
      'difusao_h': EffectParam(
        'Difusão horizontal',
        0,
        0,
        500,
        unit: 'px',
        decimals: 1,
      ),
      'difusao_v': EffectParam(
        'Difusão vertical',
        0,
        0,
        500,
        unit: 'px',
        decimals: 1,
      ),
      'opacidade': EffectParam('Opacidade', 100, 0, 100, unit: '%', decimals: 1),
    },
    // A COR MANDA NA MONTAGEM: mudar a cor nao pode custar um quadro
    // inteiro de espera.
    montar: ['difusao_h', 'difusao_v'],
    presets: [
      EffectPronto('Silhueta', {'opacidade': 100}),
      EffectPronto('Sombra chapada', {'opacidade': 55}),
    ],
  ),
};
