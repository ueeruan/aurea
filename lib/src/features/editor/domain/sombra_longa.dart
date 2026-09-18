import 'dart:math' as math;
import 'dart:ui' show Color;

import 'effect.dart';
import 'sombra_projetada.dart';

/// SOMBRA LONGA — A SILHUETA ESTICADA, E NAO DESLOCADA.
///
/// ==========================================================================
/// POR QUE ESTA FICHA NAO TEM ID DO AFTER EFFECTS
/// ==========================================================================
///
/// Todo o resto do lote de 18/09 foi medido contra um render do AE. Este
/// nao: o AE do dono NAO TEM Long Shadow. O `S_LongShadow` do Sapphire
/// nao esta instalado, e o `S_Shadow`/`S_EdgeShadow` tambem nao existem
/// na maquina dele.
///
/// Entao aqui a ficha e NOSSA, e esta escrito assim de proposito. O que
/// ela faz e o classico "long shadow" do motion design: a silhueta da
/// camada esticada numa direcao ate um comprimento, chapada numa cor so.
/// Sem plugin de referencia nao ha o que copiar — e o que nao se pode
/// fazer e inventar um id do AE e fingir que foi medido.
///
/// ==========================================================================
/// A CONTA
/// ==========================================================================
///
/// A sombra longa e a UNIAO de todas as copias da silhueta deslocadas de 0
/// ate `comprimento` ao longo da direcao. A conta direta seria desenhar
/// essas copias — e sao centenas, cada uma um `saveLayer` por quadro, que
/// e exatamente o que congela o palco neste projeto.
///
/// O shader faz a mesma pergunta por pixel: "andando na direcao da luz a
/// partir daqui, algum ponto ate o comprimento esta dentro da silhueta?".
/// Se algum estiver, este pixel e sombra. E a mesma uniao, calculada de
/// tras para diante, e uma leitura de textura por passo.
///
/// O DECAIMENTO e o unico parametro que a uniao nao explica sozinha: em
/// vez de a sombra ser chapada ate a ponta e acabar de repente, o peso de
/// cada passo cai linearmente de 1 ate `1 - queda`. Com queda 0 (o padrao)
/// a sombra e chapada, que e o long shadow classico.
///
/// A DIRECAO E A MESMA DA SOMBRA PROJETADA, de proposito: 135° e a luz em
/// cima a esquerda, e a sombra caindo embaixo a direita. Quem aprendeu uma
/// nao precisa aprender a outra, e as duas contas moram no mesmo arquivo
/// (`sombra_projetada.dart`) — inclusive a margem, que e a mesma coisa.
const efeitosSombraLonga = <EffectType, EffectSpec>{
  EffectType.sombraLonga: EffectSpec(
    id: 'sombra_longa',
    name: 'Sombra longa',
    category: 'Perspective',
    hasColor: true,
    // Preto translucido e nao preto puro: o long shadow do motion design
    // quase nunca e opaco, e nascer em 100% de opacidade obriga a mexer em
    // dois parametros para sair do lugar.
    defaultColor: Color(0xB3000000),
    synonyms: [
      'sombra longa',
      'long shadow',
      'sombra esticada',
      'sombra reta',
      'silhueta esticada',
      'material design',
    ],
    params: {
      'distancia': EffectParam(
        'Comprimento',
        200,
        0,
        2000,
        unit: 'px',
        decimals: 1,
        relative: true,
      ),
      'direcao': EffectParam(
        'Ângulo',
        135,
        0,
        360,
        unit: '°',
        decimals: 1,
      ),
      'opacidade': EffectParam(
        'Opacidade',
        100,
        0,
        100,
        unit: '%',
        decimals: 1,
      ),
      'suavidade': EffectParam(
        'Suavidade',
        0,
        0,
        200,
        unit: 'px',
        decimals: 1,
        relative: true,
      ),
      'queda': EffectParam(
        'Queda',
        0,
        0,
        100,
        unit: '%',
        decimals: 1,
      ),
    },
    montar: ['distancia', 'direcao', 'suavidade'],
    presets: [
      EffectPronto('Longa', {'distancia': 200, 'direcao': 135}),
      EffectPronto('Material', {
        'distancia': 400,
        'direcao': 135,
        'opacidade': 35,
        'suavidade': 0,
      }),
      EffectPronto('Desvanecendo', {
        'distancia': 600,
        'direcao': 135,
        'queda': 100,
      }),
      EffectPronto('Baixa', {'distancia': 300, 'direcao': 315}),
      EffectPronto('Difusa', {'distancia': 250, 'direcao': 135, 'suavidade': 24}),
    ],
  ),
};

/// QUANTOS PASSOS A MARCHA DA NO MAXIMO.
///
/// Cada passo e uma leitura de textura por pixel. Noventa e seis passos
/// cobrem 200 px com passo de 2 px — abaixo disso a borda da sombra
/// comeca a serrilhar em silhueta fina, e acima disso o custo por quadro
/// sobe sem o olho ver. O teto existe para um comprimento de 2000 px nao
/// virar 2000 leituras por pixel.
const int kTetoDePassosDaSombraLonga = 96;

/// A MARCHA: de quanto e cada passo, e quantos sao.
///
/// O passo nunca e menor que 1 px logico: abaixo disso a maioria dos
/// passos cai dentro do mesmo texel e o resultado e o mesmo com custo
/// maior. Com comprimento curto, entao, a marcha da um passo por pixel em
/// vez de estourar o teto de passos.
({double passo, int passos}) marchaDaSombraLonga({
  required double distancia,
  int teto = kTetoDePassosDaSombraLonga,
}) {
  if (!distancia.isFinite || distancia <= 0 || teto <= 0) {
    return (passo: 0, passos: 0);
  }
  final passo = math.max(1.0, distancia / teto);
  final passos = (distancia / passo).ceil().clamp(1, teto);
  return (passo: passo, passos: passos);
}

/// A CAIXA QUE A SOMBRA LONGA PRECISA, com a suavidade incluida.
///
/// E a MESMA conta da sombra projetada, e nao uma parecida: o alcance da
/// silhueta esticada ate `comprimento` e o do deslocamento de
/// `comprimento`, lado a lado. Duas contas para a mesma pergunta
/// divergiriam no dia em que uma das duas mudasse.
MargemDaSombra margemDaSombraLonga({
  required double distancia,
  required double direcao,
  required double suavidade,
}) => margemDaSombra(
  distancia: distancia,
  direcao: direcao,
  suavidade: suavidade,
);
