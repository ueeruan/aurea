import 'dart:math' as math;
import 'dart:ui' show Color, Offset;

import 'effect.dart';

/// DOBRA DE PAGINA (CC Page Turn).
///
/// O QUE ELA FAZ: levanta um lado da camada e enrola esse lado num
/// cilindro, como quem vira a folha de um livro. O lado de la do vinco
/// continua plano e intacto; o lado de ca sobe, curva e passa por cima —
/// e o que passou do topo do rolo simplesmente nao cobre mais nada.
///
/// ==========================================================================
/// O QUE FOI MEDIDO, E O QUE FOI MODELADO
/// ==========================================================================
///
/// A referencia antiga saiu chapada (fonte lisa de 48x48, sem conteudo para
/// a dobra cruzar). As novas estao em `build/qa/ae-novos/R3_pt_*.png`, com
/// um gradiente de canto a canto e dois quadrados, quatro variantes.
///
/// DO RENDER VEM A GEOMETRIA, e ela confere:
///
///   * o vinco e uma RETA, e o lado plano fica intacto — medido linha a
///     linha, a mudanca comeca exatamente na reta e nao ha um pixel
///     alterado do outro lado;
///   * o rolo comprime o conteudo e o projeto de volta por cima do plano:
///     o canto inferior direito do quadrado azul, que estava em (167,167),
///     aparece em (163,140) — 27 px mais para cima, e mais perto do vinco
///     do que estava. E a assinatura de um cilindro, e nao de um
///     deslocamento;
///   * ha um REFLEXO correndo pela dobra, e ele nao esta no meio dela:
///     esta onde a normal do cilindro bisseta a luz.
///
/// O RESTO E MODELO, e vale dizer em voz alta: o CC Page Turn tem um
/// controle de verso (Back Page / Paper Color) que esta aqui como "Verso" e
/// "Cor do papel", com o mesmo sentido, mas a Constante de Projecao e
/// ortografica — nao ha camera em perspectiva dentro do efeito. O que se
/// perde com isso e a mudanca de tamanho do rolo conforme ele se aproxima
/// do observador, que num rolo de raio pequeno e de poucos pixels.
const efeitosDobraDePagina = <EffectType, EffectSpec>{
  EffectType.dobraDePagina: EffectSpec(
    id: 'cc_page_turn',
    name: 'Dobra de página',
    category: 'Distort',
    hasColor: true,
    // O cinza-papel do AE: (0.769, 0.769, 0.706).
    defaultColor: Color(0xFFC4C4B4),
    synonyms: [
      'page turn',
      'page curl',
      'dobra de pagina',
      'dobra',
      'virar a pagina',
      'curvar a folha',
      'canto levantado',
    ],
    params: {
      'posicao': EffectParam(
        'Posição da dobra',
        0.75,
        -1,
        2,
        kind: ParamKind.point,
        relative: true,
      ),
      'posicao_y': EffectParam(
        'Posição Y',
        0.5,
        -1,
        2,
        kind: ParamKind.point,
        relative: true,
      ),
      'angulo': EffectParam(
        'Ângulo da dobra',
        -60,
        -360,
        360,
        unit: '°',
        decimals: 1,
      ),
      'raio': EffectParam(
        'Raio da dobra',
        50,
        1,
        2000,
        unit: 'px',
        decimals: 1,
        relative: true,
      ),
      'luz': EffectParam(
        'Direção da luz',
        -60,
        -360,
        360,
        unit: '°',
        decimals: 1,
      ),
      'verso': EffectParam(
        'Verso',
        85,
        0,
        100,
        unit: '%',
        decimals: 1,
      ),
      'brilho': EffectParam(
        'Brilho',
        100,
        0,
        300,
        unit: '%',
        decimals: 1,
      ),
    },
    montar: ['angulo', 'raio', 'luz'],
    presets: [
      EffectPronto('Padrão do AE', {}),
      EffectPronto('Canto do canto', {'posicao': .9, 'raio': 90}),
      EffectPronto('Meia folha', {'posicao': .5, 'raio': 140}),
      EffectPronto('Rolo apertado', {'raio': 18, 'brilho': 160}),
      EffectPronto('Papel fosco', {'verso': 20, 'brilho': 0}),
    ],
  ),
};

/// A NORMAL DO VINCO, e para que lado a folha enrola.
///
/// O vinco e a reta que passa pela posicao da dobra no angulo pedido. A
/// normal e perpendicular a ela; o lado para onde ela aponta e o que
/// levanta.
///
/// O SENTIDO E ESCOLHA, e nao medida: o render do AE mostra a folha
/// subindo para o lado de baixo-direita com o angulo no padrao, e e esse o
/// sentido que a normal daqui produz. Trocar o sinal do angulo levanta o
/// outro lado, que e o que faz o efeito servir para os quatro cantos.
Offset normalDaDobra(double anguloGraus) {
  if (!anguloGraus.isFinite) return const Offset(0, 1);
  final rad = anguloGraus * math.pi / 180;
  return Offset(-math.sin(rad), math.cos(rad));
}

/// ONDE A DOBRA POE UMA COORDENADA DA CAMADA — a conta do shader, escrita
/// em Dart, para poder ser conferida sem GPU.
///
/// Recebe um ponto em pixels da camada e devolve ONDE ELE APARECE depois
/// da dobra. Fora do rolo nao ha resposta: devolve nulo, porque ali a
/// folha ja passou por cima do quadro e nao cobre mais nada.
///
/// A CONTA, em uma linha: um ponto que estava a `u` da dobra sobe o
/// cilindro andando um arco `u`, o que o poe a `raio * sin(u/raio)` do
/// vinco NA TELA — mais perto do que estava, porque o arco e maior que a
/// corda. E e essa diferenca entre `u` e `raio*sin(u/raio)` que comprime
/// o desenho, cada vez mais forte quanto mais longe da dobra.
///
/// O TOPO DO ROLO E `u = raio * pi/2`: dali em diante a folha passa da
/// vertical e o que se veria seria o VERSO, que nao e o que este efeito
/// desenha. Por isso o nulo, e nao um numero.
Offset? ondeAparece({
  required Offset ponto,
  required Offset centro,
  required Offset normal,
  required double raio,
}) {
  if (!raio.isFinite || raio <= 0.5) return ponto;
  final v = ponto - centro;
  final aoLongo = v.dx * (-normal.dy) + v.dy * normal.dx;
  final u = v.dx * normal.dx + v.dy * normal.dy;
  if (u <= 0) return ponto;
  if (u >= raio * math.pi / 2) return null;
  final aparente = raio * math.sin(u / raio);
  return Offset(
    centro.dx + normal.dx * aparente - normal.dy * aoLongo,
    centro.dy + normal.dy * aparente + normal.dx * aoLongo,
  );
}

/// O INVERSO: QUE PONTO DA CAMADA APARECE NESTA COORDENADA DA TELA.
///
/// E o que o shader faz, pixel a pixel. O lado plano e ele mesmo; no lado
/// do rolo ha dois candidatos (a frente e o fundo do cilindro projetam no
/// mesmo lugar) e vale a frente, que e a superficie que olha para quem
/// ve. Sem solucao, o pixel fica vazio.
Offset? pontoQueAparece({
  required Offset tela,
  required Offset centro,
  required Offset normal,
  required double raio,
}) {
  if (!raio.isFinite || raio <= 0.5) return tela;
  final v = tela - centro;
  final aoLongo = v.dx * (-normal.dy) + v.dy * normal.dx;
  final d = v.dx * normal.dx + v.dy * normal.dy;
  final u = d <= 0 ? d : raio * math.asin((d / raio).clamp(-1.0, 1.0));
  return Offset(
    centro.dx + normal.dx * u - normal.dy * aoLongo,
    centro.dy + normal.dy * u + normal.dx * aoLongo,
  );
}
