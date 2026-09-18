import 'dart:math' as math;
import 'dart:ui' show Color, Offset;

import 'effect.dart';

/// SOMBRA PROJETADA (ADBE Drop Shadow), LIDO DO AFTER EFFECTS.
///
/// O QUE ELA FAZ: copia o ALFA da camada, pinta essa silhueta com a cor
/// escolhida, desfoca e joga fora de lugar. A camada continua inteira por
/// cima, na posicao original. Nao e a sombra de uma caixa: e a sombra da
/// FORMA — um recorte vazado projeta sombra vazada, e uma camada com alfa
/// parcial projeta sombra com a mesma meia-opacidade.
///
/// E O EFEITO QUE ABRE A CAMADA PARA FORA. O After Effects aumenta o
/// quadro da camada para caber a sombra; sem isso a sombra nasceria
/// cortada na borda da camada, que era exatamente o defeito visivel.
///
/// MEDIDO CONTRA RENDER DO AE (comp 200x200, solido branco 80x80 centrado
/// em (100,100), direcao 135, distancia 30, cor vermelha, opacidade 255):
///
///   * DESLOCAMENTO `(+21, +21)`. Na linha y=100 o quadrado ocupa 60..139 e
///     ha vermelho de 141 a 159:
///
///       dx = -distancia * cos(direcao)
///       dy = +distancia * sin(direcao)     // y para BAIXO, como no motor
///
///     O sinal NEGATIVO no cosseno nao e escolha: a direcao e a da LUZ
///     (135 poe a luz em cima a esquerda), e a sombra cai do lado oposto.
///     Conferido nos quatro quadrantes e nas quatro direcoes cardinais.
///
///   * DESFOQUE: com Suavidade 20 a queda vai de 255 em 147 ate 0 em 174 —
///     10% a 90% em ~12 px, o que numa gaussiana e `sigma = 4,5`:
///
///       sigma = suavidade * 0.225
///
///     Com suavidade 0 nao ha desfoque NENHUM, e a sombra termina na borda
///     exata do quadrado deslocado (159, e nao 160,3). Por isso o passe
///     tira o filtro de desfoque inteiro quando sigma e zero, em vez de
///     pedir um desfoque de raio zero.
const efeitosSombraProjetada = <EffectType, EffectSpec>{
  EffectType.sombraProjetada: EffectSpec(
    id: 'adbe_drop_shadow',
    name: 'Sombra projetada',
    category: 'Generate',
    hasColor: true,
    defaultColor: Color(0xFF000000),
    synonyms: [
      'drop shadow',
      'sombra projetada',
      'sombra',
      'projetar sombra',
      'shadow',
      'relevo',
    ],
    params: {
      // A ORDEM DOS SETE E A DO PROPRIO EFEITO no AE (indices 1 a 6), e
      // nao a ordem de leitura: quem ja mexeu no Drop Shadow procura
      // Distancia e Direcao onde sempre estiveram.
      'distancia': EffectParam(
        'Distância',
        5,
        0,
        1000,
        unit: 'px',
        decimals: 1,
        relative: true,
      ),
      'direcao': EffectParam(
        'Direção',
        135,
        0,
        360,
        unit: '°',
        decimals: 1,
      ),
      'suavidade': EffectParam(
        'Suavidade',
        0,
        0,
        500,
        unit: 'px',
        decimals: 1,
        relative: true,
      ),
      'opacidade': EffectParam(
        'Opacidade',
        100,
        0,
        100,
        unit: '%',
        decimals: 1,
      ),
      'somente_sombra': EffectParam(
        'Somente sombra',
        0,
        0,
        1,
        kind: ParamKind.toggle,
      ),
    },
    montar: ['distancia', 'direcao', 'suavidade'],
    presets: [
      // Os tres do After Effects, com os valores de fabrica dele.
      EffectPronto('Suave', {'distancia': 5, 'direcao': 135, 'suavidade': 0}),
      EffectPronto('Longa', {'distancia': 32, 'direcao': 135, 'suavidade': 12}),
      EffectPronto('Difusa', {'distancia': 12, 'direcao': 135, 'suavidade': 40}),
      EffectPronto('Contraluz', {'distancia': 20, 'direcao': -45}),
      EffectPronto('Só a sombra', {'somente_sombra': 1, 'distancia': 18}),
    ],
  ),
};

/// O DESLOCAMENTO DA SOMBRA, em pixels da camada, com y para baixo.
///
/// A direcao e a da LUZ. 135° (o padrao do AE) poe a luz em cima a
/// esquerda e a sombra embaixo a direita — `(+21, +21)` com distancia 30,
/// que e o que o render do AE mostra.
Offset deslocamentoDaSombra({
  required double distancia,
  required double direcao,
}) {
  if (!distancia.isFinite || distancia == 0) return Offset.zero;
  final rad = direcao * math.pi / 180;
  final dx = -distancia * math.cos(rad);
  final dy = distancia * math.sin(rad);
  if (!dx.isFinite || !dy.isFinite) return Offset.zero;
  return Offset(dx, dy);
}

/// O DESVIO PADRAO DO DESFOQUE, tirado do render do AE: 10% a 90% em
/// ~12 px com Suavidade 20 e uma gaussiana de `sigma = 4,5`.
double sigmaDaSombra(double suavidade) {
  if (!suavidade.isFinite || suavidade <= 0) return 0;
  return suavidade * 0.225;
}

/// QUANTAS VEZES SIGMA VAI ATE A CAUDA SUMIR.
///
/// A gaussiana nao termina, mas em 3 sigma ela ja caiu para 1% do pico —
/// abaixo do passo de 1/255 em qualquer cor. E o numero que o proprio
/// Impeller usa para dimensionar a saida do `ImageFilter.blur`.
const double kCaudaDaGaussiana = 3;

/// QUANTO A CAMADA PRECISA ABRIR EM CADA LADO para a sombra caber.
///
/// NAO E UM NUMERO SO, e essa e a economia: a sombra cai de UM lado. Com
/// a luz em cima a esquerda, o lado de cima a esquerda so precisa da
/// cauda do desfoque, e o lado de baixo a direita precisa da distancia
/// inteira mais a cauda. Uma margem simetrica igual ao pior lado gastaria
/// o dobro de area de camada — e cada pixel a mais e area que o
/// compositor preenche por quadro.
class MargemDaSombra {
  const MargemDaSombra(this.esquerda, this.topo, this.direita, this.baixo);

  final double esquerda;
  final double topo;
  final double direita;
  final double baixo;

  double get largura => esquerda + direita;
  double get altura => topo + baixo;

  bool get vazia => largura <= 0 && altura <= 0;

  @override
  String toString() =>
      'MargemDaSombra($esquerda, $topo, $direita, $baixo)';
}

MargemDaSombra margemDaSombra({
  required double distancia,
  required double direcao,
  required double suavidade,
}) {
  final d = deslocamentoDaSombra(distancia: distancia, direcao: direcao);
  final cauda = kCaudaDaGaussiana * sigmaDaSombra(suavidade);
  if (!cauda.isFinite || cauda < 0) return const MargemDaSombra(0, 0, 0, 0);
  return MargemDaSombra(
    math.max(0, -d.dx) + cauda,
    math.max(0, -d.dy) + cauda,
    math.max(0, d.dx) + cauda,
    math.max(0, d.dy) + cauda,
  );
}

/// ONDE A CAIXA ABERTA CAI EM RELACAO A CAIXA ORIGINAL DA CAMADA.
///
/// O `OverflowBox` posiciona o filho pelo `alignment`, e a conta e a dele:
///
///   deslocamento = (tamanhoDoPai - tamanhoDoFilho) * (alignment + 1) / 2
///
/// Queremos `deslocamento = -margem.esquerda`, entao:
///
///   alignment = 2 * margem.esquerda / (tamanhoDoFilho - tamanhoDoPai) - 1
///
/// A INTUICAO QUE ENGANGA: margem grande do lado ESQUERDO da alinhamento
/// POSITIVO. O `alignment` posiciona o FILHO dentro do PAI, e o filho e a
/// caixa maior: alinhar o filho pelo canto direito empurra a sobra para a
/// esquerda, que e o que a margem a esquerda pede. Uma margem de 40 numa
/// camada de 100 da exatamente 1 (canto direito com canto direito); uma
/// margem de 0 do lado esquerdo da -1 (canto esquerdo com canto
/// esquerdo, que e a caixa crescendo so para a direita).
///
/// MARGEM ZERO E O CASO NORMAL, e nao um erro: com a sombra caindo so
/// para um lado, o outro lado tem margem zero e ainda precisa do
/// alinhamento -1. Quem nao tem o que alinhar e o eixo SEM FOLGA (caixa
/// do mesmo tamanho), e ali a resposta e zero, e nao uma divisao por
/// zero.
///
/// Devolve os DOIS EIXOS em -1..1 e nao um `Alignment`: este arquivo e
/// dominio e nao conhece widget nenhum. Quem monta a caixa e o passe.
({double x, double y}) alinhamentoDaSombra({
  required MargemDaSombra margem,
  required double larguraDaCamada,
  required double alturaDaCamada,
}) {
  double eixo(double antes, double depois, double lado) {
    final folga = depois - antes;
    if (!folga.isFinite || folga.abs() < 0.001) return 0;
    if (!lado.isFinite) return 0;
    return (2 * lado / folga - 1).clamp(-1.0, 1.0);
  }

  return (
    x: eixo(larguraDaCamada, larguraDaCamada + margem.largura, margem.esquerda),
    y: eixo(alturaDaCamada, alturaDaCamada + margem.altura, margem.topo),
  );
}
