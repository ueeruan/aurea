import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui';

/// RASTREAR UM PONTO NO VIDEO.
///
/// E a peca que faltava para tres coisas ao mesmo tempo: grudar um texto
/// num objeto que anda, tirar o tremor da mao, e reenquadrar seguindo o
/// que se move. Sem rastreio, cada uma dessas viraria keyframe na mao,
/// quadro a quadro.
///
/// O metodo e o mais simples que funciona: pega um pedacinho do quadro
/// (o "molde"), procura ele no quadro seguinte dentro de uma janela, e
/// fica onde a semelhanca e maior. A semelhanca e a CORRELACAO CRUZADA
/// NORMALIZADA — normalizada porque a luz muda entre um quadro e outro,
/// e uma soma de diferencas simples confundiria "a cena escureceu" com
/// "o objeto andou".

/// Um quadro em tons de cinza.
class GrayFrame {
  const GrayFrame(this.pixels, this.width, this.height);

  final Uint8List pixels;
  final int width;
  final int height;

  int at(int x, int y) {
    if (x < 0 || y < 0 || x >= width || y >= height) return 0;
    return pixels[y * width + x];
  }
}

/// Onde o ponto estava, e o quanto se pode confiar nisso.
class TrackPoint {
  const TrackPoint({
    required this.frame,
    required this.position,
    required this.confidence,
  });

  final int frame;
  final Offset position;

  /// 0..1. Abaixo de ~0,5 o rastreio praticamente perdeu o alvo — e
  /// melhor a interface dizer isso do que entregar um movimento errado
  /// com cara de certo.
  final double confidence;
}

/// Correlacao cruzada normalizada entre o molde de [a] centrado em [ca] e
/// a mesma janela de [b] centrada em [cb]. Devolve -1..1.
double ncc(
  GrayFrame a,
  Offset ca,
  GrayFrame b,
  Offset cb,
  int raio,
) {
  final ax = ca.dx.round(), ay = ca.dy.round();
  final bx = cb.dx.round(), by = cb.dy.round();

  var somaA = 0.0, somaB = 0.0;
  var n = 0;
  for (var dy = -raio; dy <= raio; dy++) {
    for (var dx = -raio; dx <= raio; dx++) {
      somaA += a.at(ax + dx, ay + dy);
      somaB += b.at(bx + dx, by + dy);
      n++;
    }
  }
  if (n == 0) return 0;
  final mediaA = somaA / n;
  final mediaB = somaB / n;

  var num = 0.0, denA = 0.0, denB = 0.0;
  for (var dy = -raio; dy <= raio; dy++) {
    for (var dx = -raio; dx <= raio; dx++) {
      final va = a.at(ax + dx, ay + dy) - mediaA;
      final vb = b.at(bx + dx, by + dy) - mediaB;
      num += va * vb;
      denA += va * va;
      denB += vb * vb;
    }
  }
  // Area lisa (parede branca) tem variancia zero: nao da para rastrear,
  // e fingir que da e o que faz o rastreio "escorregar".
  if (denA < 1e-6 || denB < 1e-6) return 0;
  return num / math.sqrt(denA * denB);
}

/// Acha [alvo] de [a] dentro de [b], procurando ate [busca] pixels.
///
/// Devolve a posicao e a semelhanca. A busca e grosseira e depois fina:
/// varrer pixel a pixel numa janela de 40 custa 6561 comparacoes por
/// quadro, e o celular sente.
(Offset, double) matchPatch(
  GrayFrame a,
  Offset alvo,
  GrayFrame b, {
  int patch = 12,
  int busca = 24,
  Offset? seed,
}) {
  // A busca comeca de ONDE O PONTO ESTAVA no quadro anterior, nao da
  // posicao original. Sem isso, um objeto que anda 30 px em dez quadros
  // sai da janela de busca e o rastreio o perde no meio do caminho.
  var melhor = seed ?? alvo;
  var melhorScore = -2.0;

  for (final passo in const [3, 1]) {
    final raioBusca = passo == 3 ? busca : passo * 2;
    final centro = melhor;
    for (var dy = -raioBusca; dy <= raioBusca; dy += passo) {
      for (var dx = -raioBusca; dx <= raioBusca; dx += passo) {
        final cand = Offset(centro.dx + dx, centro.dy + dy);
        final s = ncc(a, alvo, b, cand, patch);
        if (s > melhorScore) {
          melhorScore = s;
          melhor = cand;
        }
      }
    }
  }
  return (melhor, melhorScore.clamp(-1.0, 1.0));
}

/// Segue um ponto por toda a sequencia.
///
/// O molde vem SEMPRE do primeiro quadro. Atualizar o molde a cada
/// quadro parece melhor e e pior: o erro de cada passo se acumula, e em
/// cem quadros o rastreio esta seguindo outra coisa (a "deriva do
/// molde", que e o erro classico de quem escreve isso pela primeira
/// vez).
List<TrackPoint> trackSequence(
  List<GrayFrame> frames,
  Offset inicio, {
  int patch = 12,
  int busca = 24,
}) {
  if (frames.isEmpty) return const [];
  final out = <TrackPoint>[
    TrackPoint(frame: 0, position: inicio, confidence: 1),
  ];
  var atual = inicio;

  for (var i = 1; i < frames.length; i++) {
    final (p, s) = matchPatch(frames.first, inicio, frames[i],
        patch: patch, busca: busca, seed: atual);
    // Perdeu o alvo: fica onde estava em vez de pular para o outro lado
    // da tela — um salto e sempre pior que um travamento.
    if (s < 0.35) {
      out.add(TrackPoint(frame: i, position: atual, confidence: s.clamp(0, 1)));
      continue;
    }
    atual = p;
    out.add(TrackPoint(frame: i, position: p, confidence: s));
  }
  return out;
}

/// Media movel do caminho.
///
/// E o coracao da estabilizacao: o caminho SUAVE e para onde a camera
/// "queria" ir; a diferenca entre ele e o caminho real e o tremor. Uma
/// janela grande demais mata o movimento de proposito (a panoramica);
/// pequena demais nao tira o tremor.
List<Offset> smoothPath(List<Offset> pontos, {int janela = 15}) {
  if (pontos.isEmpty || janela <= 1) return pontos;
  final raio = janela ~/ 2;
  return [
    for (var i = 0; i < pontos.length; i++)
      () {
        var sx = 0.0, sy = 0.0;
        var n = 0;
        for (var k = -raio; k <= raio; k++) {
          final j = i + k;
          if (j < 0 || j >= pontos.length) continue;
          sx += pontos[j].dx;
          sy += pontos[j].dy;
          n++;
        }
        return n == 0 ? pontos[i] : Offset(sx / n, sy / n);
      }(),
  ];
}

/// Quanto cada quadro precisa ser DESLOCADO para o tremor sumir.
///
/// Positivo significa "mova a imagem para ca". A soma dos deslocamentos
/// e zero por construcao (a media do caminho suave e a media do real),
/// entao a imagem nao escorrega para um canto ao longo do clipe.
List<Offset> stabilizeOffsets(List<TrackPoint> track, {int janela = 15}) {
  if (track.isEmpty) return const [];
  final reais = [for (final p in track) p.position];
  final suaves = smoothPath(reais, janela: janela);
  return [
    for (var i = 0; i < reais.length; i++) suaves[i] - reais[i],
  ];
}

/// Quanto a imagem precisa ser AMPLIADA para o deslocamento nao mostrar
/// borda preta.
///
/// Estabilizar sem ampliar mostra o vazio nas bordas — e o defeito que
/// denuncia estabilizacao caseira na hora.
double stabilizeZoom(
  List<Offset> offsets,
  int width,
  int height,
) {
  if (offsets.isEmpty || width <= 0 || height <= 0) return 1;
  var maxX = 0.0, maxY = 0.0;
  for (final o in offsets) {
    maxX = math.max(maxX, o.dx.abs());
    maxY = math.max(maxY, o.dy.abs());
  }
  final zx = (width + 2 * maxX) / width;
  final zy = (height + 2 * maxY) / height;
  return math.max(1.0, math.max(zx, zy));
}
