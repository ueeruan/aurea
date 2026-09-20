import 'dart:math' as math;
import 'dart:typed_data';

import 'element3d.dart';

/// Static linear HDR radiance. Baking runs on an isolate, then the GPU builds
/// roughness mip levels once; camera movement only changes matrices.
Float32List environmentRadiance(EnvironmentKind kind, {int width = 512}) {
  final height = width ~/ 2, pixels = Float32List(width * (width ~/ 2) * 4);
  for (var y = 0; y < height; y++) {
    final latitude = math.pi * (y + 0.5) / height;
    final vertical = math.cos(latitude), ring = math.sin(latitude);
    for (var x = 0; x < width; x++) {
      final longitude = 2 * math.pi * ((x + 0.5) / width - 0.5);
      final color = environmentColor(
        kind,
        ring * math.sin(longitude),
        vertical,
        ring * math.cos(longitude),
      );
      final i = (y * width + x) * 4;
      pixels[i] = color.$1;
      pixels[i + 1] = color.$2;
      pixels[i + 2] = color.$3;
      pixels[i + 3] = 1;
    }
  }
  return pixels;
}

/// O MAPA DE AMBIENTE COMO A PLACA O QUER: a esfera inteira em radiancia
/// linear, NORMALIZADA, e com a cadeia de desfoque ja pronta.
///
/// ============================ POR QUE ISTO EXISTE =====================
///
/// Um metal nao tem cor propria: ele e o que ele reflete. Refletindo DUAS
/// CORES — o ceu em cima e o chao embaixo — o ouro sai como um bronze
/// fosco e chapado, sem nenhuma softbox desenhando a quina da letra. O
/// olho reconhece metal pelo que aparece NA superficie: a caixa de luz
/// esticada na lateral do chanfro, o escuro entre uma luz e outra.
///
/// O ambiente de estudio ja estava escrito no aplicativo desde o pintor de
/// CPU (`environmentColor`), com tres softboxes e radiancia muito acima de
/// 1. O que faltava era ele CHEGAR na placa.
///
/// ============================ AS DUAS DECISOES ========================
///
///  1. A MEDIA VIRA 1. A radiancia do arquivo tem picos de 14, e a cena ja
///     tem um ambiente plano (0,28 no estudio) que diz QUANTA luz existe.
///     Dividindo o mapa pela sua propria media, o brilho medio da cena
///     continua exatamente o de antes e o que muda e so a DIRECAO — as
///     softboxes aparecem e o resto escurece. Sem isso, um ambiente de
///     estudio estouraria a cena inteira para branco.
///  2. A CADEIA DE DESFOQUE SAI DAQUI, e nao da placa. Uma tabela de mips
///     gerada na GPU custa um passe por nivel na subida e nem toda placa
///     de celular tem a geracao garantida; aqui ela e uma media de 2x2 em
///     Dart, que roda uma vez por ambiente e fica guardada.
///
/// A MEDIA E POR AREA, e nao por pixel: numa esfera, os polos ocupam
/// dezenas de pixels e um pedaco minimo da area. Contar por pixel daria a
/// luz do teto um peso que ela nao tem, e a cena sairia clara demais.
class MapaDeAmbiente3D {
  MapaDeAmbiente3D(this.pixels, this.largura, this.niveis);

  /// OS NIVEIS CONCATENADOS: o nivel `k` tem `largura >> k` por
  /// `(largura >> k) / 2` pixels de quatro floats. O quarto canal vai em 1
  /// — a placa le RGBA e um alvo de tres canais nao existe.
  final Float32List pixels;

  /// A LARGURA DO NIVEL 0. A altura e sempre a metade.
  final int largura;

  final int niveis;

  /// Quantos floats o mapa inteiro ocupa.
  int get totalDeFloats => pixels.length;
}

/// QUANTOS NIVEIS A CADEIA TEM. Da 256x128 ate 2x1: o ultimo e quase uma
/// cor so, que e o que uma superficie bem rugosa devolve. Parar em 4x2
/// deixaria oito celulas na tela de um metal fosco, e o olho ve a malha.
const int niveisDoMapaDeAmbiente = 8;

/// O MAPA DE AMBIENTE DESTE TIPO DE CENA, pronto para a placa.
///
/// [largura] e o lado do nivel 0. Duzentos e cinquenta e seis e o ponto em
/// que a tira vertical do estudio metal ainda tem cinco pixels de largura —
/// menos que isso ela borra e o reflexo perde a unica quina reta que ele
/// tinha.
MapaDeAmbiente3D mapaDeAmbiente3D(
  EnvironmentKind kind, {
  int largura = 256,
  int niveis = niveisDoMapaDeAmbiente,
}) {
  var l = largura, a = largura ~/ 2;
  var total = 0;
  for (var k = 0; k < niveis; k++) {
    total += l * a * 4;
    l = l <= 2 ? l : l >> 1;
    a = a <= 1 ? a : a >> 1;
  }
  final pixels = Float32List(total);

  // ---- nivel 0: a esfera amostrada na direcao de cada texel
  var deslocamento = 0;
  var media = 0.0, peso = 0.0;
  l = largura;
  a = largura ~/ 2;
  for (var y = 0; y < a; y++) {
    final latitude = math.pi * (y + 0.5) / a;
    final vertical = math.cos(latitude), anel = math.sin(latitude);
    final pesoDaFaixa = anel;
    for (var x = 0; x < l; x++) {
      final longitude = 2 * math.pi * ((x + 0.5) / l - 0.5);
      final cor = environmentColor(
        kind,
        anel * math.sin(longitude),
        vertical,
        anel * math.cos(longitude),
      );
      final i = deslocamento + (y * l + x) * 4;
      pixels[i] = cor.$1;
      pixels[i + 1] = cor.$2;
      pixels[i + 2] = cor.$3;
      pixels[i + 3] = 1;
      media += (0.2126 * cor.$1 + 0.7152 * cor.$2 + 0.0722 * cor.$3) * pesoDaFaixa;
      peso += pesoDaFaixa;
    }
  }
  deslocamento += l * a * 4;
  final escala = media > 1e-6 && peso > 0 ? peso / media : 1.0;

  // ---- os niveis seguintes: media de 2x2, com a esfera dando a volta
  //
  // A MEDIA E PONDERADA PELA AREA DE CADA FAIXA, e nao por pixel. Numa
  // esfera, a faixa colada no polo tem a mesma contagem de pixels que a do
  // equador e uma fracao minima da area. Sem o peso, cada nivel desceria
  // puxado pelos polos: o nivel mais grosso ficaria com uma cor que nao e a
  // media de nada, e um metal bem rugoso sairia claro ou escuro demais
  // conforme o estudio tivesse o teto escuro.
  var larguraAnterior = l, alturaAnterior = a;
  var deslocamentoAnterior = 0;
  for (var k = 1; k < niveis; k++) {
    final ln = larguraAnterior <= 2 ? larguraAnterior : larguraAnterior >> 1;
    final an = alturaAnterior <= 1 ? alturaAnterior : alturaAnterior >> 1;
    for (var y = 0; y < an; y++) {
      final y0 = math.min(y * 2, alturaAnterior - 1);
      final y1 = math.min(y * 2 + 1, alturaAnterior - 1);
      final peso0 = math.sin(math.pi * (y0 + 0.5) / alturaAnterior);
      final peso1 = math.sin(math.pi * (y1 + 0.5) / alturaAnterior);
      final peso = peso0 * 2 + peso1 * 2;
      for (var x = 0; x < ln; x++) {
        final x0 = math.min(x * 2, larguraAnterior - 1);
        final x1 = (x * 2 + 1) % larguraAnterior;
        final i = deslocamento + (y * ln + x) * 4;
        for (var c = 0; c < 3; c++) {
          final linha0 =
              pixels[deslocamentoAnterior + (y0 * larguraAnterior + x0) * 4 + c] +
              pixels[deslocamentoAnterior + (y0 * larguraAnterior + x1) * 4 + c];
          final linha1 =
              pixels[deslocamentoAnterior + (y1 * larguraAnterior + x0) * 4 + c] +
              pixels[deslocamentoAnterior + (y1 * larguraAnterior + x1) * 4 + c];
          pixels[i + c] = peso > 1e-9
              ? (linha0 * peso0 + linha1 * peso1) / peso
              : (linha0 + linha1) * 0.25;
        }
        pixels[i + 3] = 1;
      }
    }
    deslocamentoAnterior = deslocamento;
    deslocamento += ln * an * 4;
    larguraAnterior = ln;
    alturaAnterior = an;
  }

  // ---- a normalizacao, no fim e sobre TUDO: a media do nivel 0 vale 1
  for (var i = 0; i < total; i += 4) {
    pixels[i] *= escala;
    pixels[i + 1] *= escala;
    pixels[i + 2] *= escala;
  }

  return MapaDeAmbiente3D(pixels, largura, niveis);
}
