import 'dart:ui' show Color;

import 'effect.dart';

/// VARREDURA DE LUZ (CC Light Sweep), MEDIDA CONTRA RENDER DO AE.
///
/// O QUE ELA FAZ: passa um facho reto de luz pela camada. Nao e um brilho
/// no meio: e uma FAIXA que atravessa o quadro inteiro, atravessa tambem o
/// que for transparente, e SOMA luz ao que estiver embaixo. E o efeito do
/// reflexo correndo num logo, e o do clarão que varre a cena.
///
/// A REFERENCIA DESTA VEZ TINHA CONTEUDO. A medicao anterior saiu chapada
/// porque a fonte era um solido liso de 48x48 — nao havia o que a luz
/// atravessasse. As novas sao um gradiente de canto a canto com dois
/// quadrados, quatro variantes de parametro, em `build/qa/ae-novos/R3_*.png`.
///
/// O QUE A MEDICAO DISSE (comp 200x200, Sweep Intensity 25, Width 50):
///
///   * A FAIXA E UMA RETA, e nao uma mancha. Com a direcao em 0 o que muda
///     de um pixel para outro e so a distancia ate a reta vertical que
///     passa pelo centro: a mesma luz em toda a altura do quadro. Uma
///     mancha redonda teria dado o mesmo numero so no centro.
///
///   * A INTENSIDADE E ABSOLUTA. O pico somou +63 tanto sobre um cinza 102
///     quanto sobre um cinza 146 — o mesmo valor, nao um fator. A luz
///     SOMA; nao multiplica nem clareia proporcionalmente.
///
///   * A INTENSIDADE E LINEAR NO PARAMETRO: 25% soma 63, que e
///     `255 * 25/100`. Com 60% o pico medido chegou a 254 num ponto onde
///     havia espaco para 272 — o corte em 255 comeu o resto, e o que
///     sobrou confere com a mesma conta.
///
///   * O PERFIL E QUADRATICO, e nao reto: `(1 - d/semi)^2`, com o pico em
///     `255 * Intensidade` e a semi-extensao em 2 vezes a Largura. O
///     ajuste, feito sobre 22 mil pixels de cada render e descartando o
///     que estoura em 255, deu pico 63,4 e 62,6 (o teorico e 63,75) e
///     semi 98,5 e 101,5 px com Largura 50 — 1,97 e 2,03 vez a Largura,
///     com erro medio de 0,6 e 2,1 niveis. Reta, cosseno, gaussiana e
///     smoothstep erram de 2 a 7 vezes mais; a reta que eu tinha antes
///     caia rapido demais no meio e devagar demais na ponta.
///
///   * O OUTRO RENDER CONFIRMA O MODELO COM OUTROS PARAMETROS. O de
///     Intensidade 60 e Largura 100 tem o corte em 255 comendo quase
///     tudo, entao ele nao serve para ajustar a forma — mas serve para
///     conferir: com `153 * (1 - d/200)^2` a diferenca e de 2,7 niveis
///     perto do pico e de 19 no fim da cauda.
///
///   * A COR DA LUZ ENTRA POR INTEIRO. O delta medido foi R+63 G+61 B+59,
///     que e exatamente a cor padrao do AE — um branco quente
///     (1 / 0,98 / 0,94) — multiplicada por 63.
///
/// NAO REPRODUZIDO: o realce de BORDA do CC Light Sweep. Nos quatro
/// renders medidos nao sobrou um degrau que separe a borda da faixa; o que
/// existe e a queda quadratica. Entra-lo aqui seria inventar um parametro.
///
/// CUIDADO COM A BANCADA: os quatro renders do Light Sweep trazem um
/// borrao claro nas ~20 primeiras linhas, com pico de +224, que NAO
/// acompanha nenhum parametro do efeito (fica em x~50 nos quatro). Nao e
/// da faixa: e da bancada, e foi descartado do ajuste. Quem for medir de
/// novo comece em `y >= 22`.
const efeitosLuzNaFaixa = <EffectType, EffectSpec>{
  EffectType.lightSweep: EffectSpec(
    id: 'cc_light_sweep',
    name: 'Varredura de luz',
    category: 'Light',
    hasColor: true,
    // O branco quente do AE: (1, 0.9804, 0.9412).
    defaultColor: Color(0xFFFFFAF0),
    synonyms: [
      'light sweep',
      'varredura de luz',
      'varredura',
      'facho',
      'reflexo',
      'brilho que passa',
      'luz que passa',
    ],
    params: {
      // A ORDEM E A DO EFEITO NO AE (1 a 5), e nao a de leitura.
      'centro': EffectParam(
        'Centro',
        0.5,
        -1,
        2,
        kind: ParamKind.point,
        relative: true,
      ),
      'centro_y': EffectParam(
        'Centro Y',
        0.25,
        -1,
        2,
        kind: ParamKind.point,
        relative: true,
      ),
      'direcao': EffectParam(
        'Direção',
        -30,
        -360,
        360,
        unit: '°',
        decimals: 1,
      ),
      'largura': EffectParam(
        'Largura',
        50,
        1,
        2000,
        unit: 'px',
        decimals: 1,
        relative: true,
      ),
      'intensidade': EffectParam(
        'Intensidade',
        25,
        0,
        400,
        unit: '%',
        decimals: 1,
      ),
      'recepcao': EffectParam(
        'Recepção',
        0,
        0,
        1,
        kind: ParamKind.choice,
        options: ['Somar', 'Tela'],
      ),
    },
    montar: ['direcao', 'largura', 'intensidade'],
    presets: [
      EffectPronto('Padrão do AE', {}),
      EffectPronto('Facho fino', {'largura': 14, 'intensidade': 60}),
      EffectPronto('Clarão', {'largura': 160, 'intensidade': 90}),
      EffectPronto('Reflexo frio', {'largura': 30, 'intensidade': 45}),
    ],
  ),
};
