import 'dart:math' as math;
import 'dart:ui';

import '../../../../../core/ds/ds.dart';
import '../../../domain/layer.dart';
import 'estado_da_timeline.dart';

/// Qual alca de trim.
enum LadoDaAlca { inicio, fim }

/// A GEOMETRIA DE UMA LINHA — a MESMA conta para o pintor e para o toque.
///
/// Tudo em coordenadas da linha, que sao as da timeline (a linha ocupa a
/// largura inteira; o cabecalho de 70 fica por cima da ponta esquerda).
abstract final class GeometriaDaLinha {
  /// As pontas do clipe na tela, com a vista de agora.
  static ({double x0, double x1}) clipe(EstadoDaTimeline e, Layer l) {
    final x0 = e.xDoTempo(l.startTime.inMicroseconds);
    final x1 = e.xDoTempo(l.endTime.inMicroseconds);
    return (x0: x0, x1: x1);
  }

  /// A caixa desenhada do clipe (23 de altura, recuo 2,5).
  static Rect caixa(double x0, double x1) => Rect.fromLTRB(
    x0,
    AureaDims.recuoDoClipe,
    x1,
    AureaDims.linhaDeCamada - AureaDims.recuoDoClipe,
  );

  /// O dedo caiu no CORPO do clipe? (A linha inteira na vertical: 28 e
  /// pouco para o dedo, e nao ha outra coisa acima ou abaixo do clipe.)
  static bool noCorpo(EstadoDaTimeline e, Layer l, Offset p) {
    final c = clipe(e, l);
    return p.dx >= c.x0 && p.dx <= c.x1;
  }

  /// O dedo caiu numa ALCA DE TRIM? Cada uma tem [AureaDims.alcaDeTrim] de
  /// toque FORA do clipe — dentro dele, o toque e do clipe (mover).
  ///
  /// A PONTA COLADA NA BORDA NAO PERDE A ALCA: com o fim do clipe a menos
  /// de 30 da borda direita (ou o comeco a menos de 30 do cabecalho de 70,
  /// que fica por cima), a zona entra no clipe ate completar os 30 a
  /// vista — sem passar do meio dele, que continua sendo do mover. Era o
  /// caso de logo depois de importar: o fim do video a ~10 da borda e o
  /// arrasto virava scrub.
  static LadoDaAlca? naAlca(EstadoDaTimeline e, Layer l, Offset p) {
    final z = zonasDasAlcas(e, l);
    final i = z.inicio;
    if (i != null && p.dx >= i.$1 && p.dx < i.$2) return LadoDaAlca.inicio;
    final f = z.fim;
    if (f != null && p.dx > f.$1 && p.dx <= f.$2) return LadoDaAlca.fim;
    return null;
  }

  /// As faixas de toque das duas alcas ([de, ate]), com a vista de agora.
  /// Nula = a ponta esta escondida (atras do cabecalho ou alem da borda):
  /// o que nao se ve nao se apara — rola-se o tempo antes.
  static ({(double, double)? inicio, (double, double)? fim}) zonasDasAlcas(
    EstadoDaTimeline e,
    Layer l,
  ) {
    final c = clipe(e, l);
    const t = AureaDims.alcaDeTrim;
    const esquerda = AureaDims.cabecalhoDaCamada;
    final direita = e.largura > 0 ? e.largura : double.infinity;
    final meio = (c.x0 + c.x1) / 2;
    (double, double)? inicio;
    if (c.x0 >= esquerda) {
      inicio = c.x0 - t >= esquerda
          ? (c.x0 - t, c.x0)
          : (esquerda, math.max(c.x0, math.min(esquerda + t, meio)));
    }
    (double, double)? fim;
    if (c.x1 <= direita) {
      fim = c.x1 + t <= direita
          ? (c.x1, c.x1 + t)
          : (math.min(c.x1, math.max(direita - t, meio)), direita);
    }
    return (inicio: inicio, fim: fim);
  }

  /// O DESENHO DA ALCA (17 x 15), colado na ponta e FORA do clipe; quando
  /// fora nao cabe a vista (a ponta colada na borda ou no cabecalho), ele
  /// entra no clipe — a alca que se toca e a alca que se ve.
  static Rect desenhoDaAlca(
    double x,
    double cy,
    LadoDaAlca lado, {
    required double largura,
  }) {
    final d = AureaDims.desenhoDaAlcaDeTrim;
    final top = cy - d.height / 2;
    if (lado == LadoDaAlca.inicio) {
      final dentro = x - d.width < AureaDims.cabecalhoDaCamada;
      return Rect.fromLTWH(dentro ? x : x - d.width, top, d.width, d.height);
    }
    final dentro = x + d.width > largura;
    return Rect.fromLTWH(dentro ? x - d.width : x, top, d.width, d.height);
  }

  /// O centro vertical dos losangos na linha da camada: na faixa de baixo
  /// do clipe, para nao cair em cima do nome.
  static double get yDoLosangoNoClipe =>
      AureaDims.linhaDeCamada -
      AureaDims.recuoDoClipe -
      AureaDims.raioDoKeyframe -
      2;

  /// Na linha de propriedade o losango fica no meio.
  static double get yDoLosangoNaTrilha => AureaDims.linhaDeCamada / 2;

  /// O instante (local, µs) do losango sob o dedo, dentro da folga de
  /// toque ([AureaDims.folgaDoKeyframe]); o mais perto ganha. [inicioUs] e
  /// o comeco da camada no projeto.
  static int? losangoEm(
    EstadoDaTimeline e,
    int inicioUs,
    List<int> temposLocais,
    Offset p,
  ) {
    if (temposLocais.isEmpty) return null;
    const meia = AureaDims.folgaDoKeyframe / 2;
    int? melhor;
    var melhorD = meia;
    // Busca binaria pelo instante sob o dedo: uma camada pode ter centenas
    // de marcas, e o toque pergunta isto a cada ponteiro que desce.
    final alvo = e.tempoDoX(p.dx) - inicioUs;
    var lo = 0;
    var hi = temposLocais.length;
    while (lo < hi) {
      final mid = (lo + hi) >> 1;
      if (temposLocais[mid] < alvo) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    for (var i = lo - 2; i <= lo + 1; i++) {
      if (i < 0 || i >= temposLocais.length) continue;
      final d = (e.xDoTempo(inicioUs + temposLocais[i]) - p.dx).abs();
      if (d <= melhorD) {
        melhorD = d;
        melhor = temposLocais[i];
      }
    }
    return melhor;
  }
}
