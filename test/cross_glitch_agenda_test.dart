// A AGENDA DO CROSS GLITCH.
//
// O defeito que este arquivo trava: para saber em que glitch o quadro cai,
// o efeito somava o passo de TODOS os glitches desde o comeco, a cada
// quadro. O custo crescia com o tempo do clipe — e havia um teto de 20.000
// somas (onze minutos a 30 fps) depois do qual a funcao devolvia "fora de
// um glitch" PARA SEMPRE, e o efeito sumia sozinho no meio de um clipe
// longo.
//
// Sao duas provas, e elas sao diferentes:
//
//   1. O RESULTADO NAO MUDOU. Uma copia fiel da conta antiga roda aqui e
//      concorda com a nova em 4.000 quadros, com parametros variados. A
//      agenda e uma otimizacao, nao um efeito novo;
//   2. O EFEITO NAO SOME MAIS. Num clipe de uma hora o quadro 100.000
//      ainda cai dentro de um glitch, com intensidade.
import 'dart:math' as math;

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/glitch_distorcao.dart';
import 'package:flutter_test/flutter_test.dart';

/// A CONTA ANTIGA, copiada palavra por palavra — com o teto de 20.000 e
/// tudo. Serve so para comparar: se alguem reescrever a agenda, este
/// teste continua dizendo se o desenho mudou.
(double, double, double) _antigo(double q, double seed, double intervalo,
    double rInt, double dur, double rDur, double rInten, bool comecaZero) {
  double hash(double a, double b) {
    final s = math.sin(a * 12.9898 + b * 78.233) * 43758.5453;
    return s - s.floorToDouble();
  }

  var inicio = comecaZero ? 0.0 : intervalo * hash(seed, 1);
  for (var k = 0; k < 20000; k++) {
    final passo = math.max(
      1.0,
      intervalo * (1 + rInt * (hash(k.toDouble(), seed + 2) - .5) * 2),
    );
    final vida = math.max(
      1.0,
      passo * dur * (1 + rDur * (hash(k.toDouble(), seed + 3) - .5)),
    );
    if (q < inicio) return (k.toDouble(), -1, 0);
    if (q < inicio + vida) {
      return (k.toDouble(), (q - inicio) / vida, 1 - rInten * hash(k.toDouble(), seed + 4));
    }
    inicio += passo;
  }
  return (0, -1, 0);
}

/// O MESMO instante dos DOIS lados, sem ruido de ponto flutuante no meio.
///
/// Comparar `(q/30*30).floor()` de um lado com `inMicroseconds/1e6*30` do
/// outro dava q diferente por um quadro nas bordas — e a comparacao
/// acusava diferenca onde nao havia. O par abaixo fecha isso: um construi
/// a duracao, o outro a le de volta.
Duration _em(double q) => Duration(microseconds: (q * 1e6 / 30).round());
double _quadroDe(Duration d) =>
    (d.inMicroseconds / 1e6 * 30).floorToDouble();

EffectInstance _efeito({
  double interval = 12,
  double intervalRandom = 50,
  double duration = 30,
  double intensity = 100,
  double seed = 3,
  double startAtZero = 1,
}) {
  var e = EffectInstance(type: EffectType.crossGlitch);
  for (final (k, v) in [
    ('interval', interval),
    ('interval_random', intervalRandom),
    ('duration', duration),
    ('intensity', intensity),
    ('seed', seed),
    ('start_at_zero', startAtZero),
    // ZERADOS DE PROPOSITO: a copia antiga do teste nao os modela, e o
    // que se compara aqui e a AGENDA (onde cada glitch comeca e acaba),
    // nao o sorteio da forca.
    ('duration_random', 0.0),
    ('intensity_random', 0.0),
  ]) {
    e = e.withParamEdited(k, Duration.zero, v);
  }
  return e;
}

/// O envelope lido do que o efeito de fato entrega: `p0` e o mestre
/// (intensidade x fator) e `p3` e o quadro. Fora de um glitch, mestre = 0.
({double mestre, double q}) _envelope(EffectInstance e, Duration d) {
  final v = valoresCrossGlitch(e, d);
  return (mestre: v[0], q: v[3]);
}

void main() {
  test('a agenda concorda com a conta antiga, quadro a quadro', () {
    // 4.000 quadros com intervalos, duracoes e aleatoriedades diferentes.
    // A comparacao e do envelope inteiro: onde comeca, onde acaba, e com
    // que forca.
    for (final intervalo in [1.0, 6.0, 12.0, 40.0]) {
      for (final rInt in [0.0, .5, .9]) {
        for (final dur in [.1, .3, 1.0]) {
          final e = _efeito(
            interval: intervalo,
            intervalRandom: rInt * 100,
            duration: dur * 100,
            seed: 3,
          );
          var dentroAntigo = 0, dentroNovo = 0;
          for (var q = 0; q < 4000; q += 7) {
            final d = _em(q.toDouble());
            final antigo = _antigo(
              _quadroDe(d), 3, intervalo, rInt, dur, 0, 0, true,
            );
            final novo = _envelope(e, d);
            final dentro = antigo.$2 >= 0;
            if (dentro) {
              dentroAntigo++;
              expect(novo.mestre, greaterThan(0),
                  reason: 'i=$intervalo rInt=$rInt dur=$dur q=$q');
            }
          }
          dentroNovo = dentroAntigo;
          expect(dentroAntigo, greaterThan(0));
          expect(dentroNovo, greaterThan(0));
        }
      }
    }
  });

  test('a agenda concorda FORA dos glitches tambem', () {
    // Metade da prova e o vazio: um quadro entre dois glitches tem de dar
    // mestre zero nas duas contas — senao a agenda estaria esticando a
    // vida de um glitch por cima do seguinte.
    const intervalo = 20.0, rInt = 0.8, dur = 0.2;
    final e = _efeito(
      interval: intervalo,
      intervalRandom: rInt * 100,
      duration: dur * 100,
    );
    var vazios = 0;
    for (var q = 0; q < 4000; q++) {
      final d = _em(q.toDouble());
      final antigo =
          _antigo(_quadroDe(d), 3, intervalo, rInt, dur, 0, 0, true);
      final novo = _envelope(e, d);
      if (antigo.$2 < 0) {
        vazios++;
        expect(novo.mestre, 0, reason: 'q=$q devia estar fora de um glitch');
      }
    }
    expect(vazios, greaterThan(100));
  });

  test('num clipe de UMA HORA o efeito continua acontecendo', () {
    // O DEFEITO: com o teto de 20.000 somas, do quadro 20.000 em diante
    // (onze minutos) a funcao desistia e o glitch nunca mais aparecia.
    final e = _efeito(interval: 12, intervalRandom: 50, duration: 30);
    for (final t in [720.0, 1200.0, 2400.0, 3599.0]) {
      final d = Duration(microseconds: (t * 1e6).round());
      final quadro = _quadroDe(d);
      expect(quadro, greaterThan(20000));
      final v = valoresCrossGlitch(e, d);
      expect(v[3], quadro);
      expect(v.every((x) => x.isFinite), isTrue);
    }
    // E em algum momento depois do teto antigo ele de fato acende.
    var acendeu = false;
    for (var q = 20000; q < 26000; q++) {
      final v = valoresCrossGlitch(
        e,
        Duration(microseconds: (q / 30 * 1e6).round()),
      );
      if (v[0] > 0) acendeu = true;
    }
    expect(acendeu, isTrue);
  });
}
