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
  static LadoDaAlca? naAlca(EstadoDaTimeline e, Layer l, Offset p) {
    final c = clipe(e, l);
    const t = AureaDims.alcaDeTrim;
    if (p.dx >= c.x0 - t && p.dx < c.x0) return LadoDaAlca.inicio;
    if (p.dx > c.x1 && p.dx <= c.x1 + t) return LadoDaAlca.fim;
    return null;
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
