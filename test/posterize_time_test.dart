// POSTERIZE TIME: a camada em degraus.
//
// O QUE SE PROVA AQUI:
//
//   1. a conta e `floor(t * taxa) / taxa`, e nao um contador de quadros —
//      o mesmo instante cai sempre no mesmo degrau, em qualquer ordem de
//      leitura;
//   2. a 12 fps sobre uma composicao de 60, cinco instantes seguidos caem
//      no MESMO degrau (e a camada "segura" o quadro);
//   3. o FPS da composicao nao muda: quem quantiza e so a camada;
//   4. desligado, o tempo passa intacto — o efeito nao existe;
//   5. a taxa pode ter keyframe sem a grade se enroscar nela mesma.
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/posterize_time.dart';
import 'package:aurea/src/features/editor/domain/time_slice.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

Layer _camada({
  double? taxa,
  Map<String, double> extras = const {},
  Duration duracao = const Duration(seconds: 4),
}) {
  var efeitos = <EffectInstance>[];
  if (taxa != null) {
    var e = EffectInstance(type: EffectType.posterizeTime);
    e = e.withParamEdited('frame_rate', Duration.zero, taxa);
    for (final p in extras.entries) {
      e = e.withParamEdited(p.key, Duration.zero, p.value);
    }
    efeitos = [e];
  }
  return ShapeLayer(
    name: 'c',
    startTime: Duration.zero,
    duration: duracao,
    effects: efeitos,
  );
}

Duration _ms(int ms) => Duration(milliseconds: ms);

void main() {
  group('a conta do degrau', () {
    test('floor(t * taxa) / taxa, ponto a ponto', () {
      // A 12 fps o degrau e de 83,333 ms.
      expect(quantizarTempo(_ms(0), 12), Duration.zero);
      expect(quantizarTempo(_ms(10), 12), Duration.zero);
      expect(quantizarTempo(_ms(80), 12), Duration.zero);
      expect(quantizarTempo(_ms(90), 12), const Duration(microseconds: 83333));
      // 166 ms ainda esta DENTRO do segundo degrau (que vai ate 166,67).
      expect(quantizarTempo(_ms(166), 12), const Duration(microseconds: 83333));
      expect(
        quantizarTempo(_ms(167), 12),
        const Duration(microseconds: 166667),
      );
    });

    test('taxa zero ou invalida nao quantiza', () {
      for (final t in [0.0, -1.0, double.nan, double.infinity]) {
        expect(quantizarTempo(_ms(123), t), _ms(123), reason: 'taxa $t');
      }
    });

    test('tempo negativo tambem tem degrau, e para TRAS', () {
      final q = quantizarTempo(const Duration(milliseconds: -50), 12);
      expect(q, const Duration(microseconds: -83333));
      expect(q.isNegative, isTrue);
    });

    test('o mesmo instante devolve o mesmo, em qualquer ordem', () {
      // Nenhum contador guardado entre chamadas: se houvesse, ler 200 ms,
      // depois 100 ms e depois 200 ms de novo daria dois resultados.
      final a = quantizarTempo(_ms(200), 12);
      quantizarTempo(_ms(100), 12);
      quantizarTempo(_ms(999), 12);
      expect(quantizarTempo(_ms(200), 12), a);
    });

    test('a 60 fps SOBRE uma composicao de 60, nada muda de lugar', () {
      // O INSTANTE EXATO de cada quadro de 60 e 16666,67 us, que nao
      // existe em microssegundos inteiros: o valor mais proximo E o
      // degrau. A prova e que o degrau nunca anda para tras nem pula.
      Duration? anterior;
      for (var quadro = 0; quadro < 120; quadro++) {
        final exato = (quadro * 1e6 / 60).round();
        final t = Duration(microseconds: exato);
        final q = quantizarTempo(t, 60);
        expect(q.inMicroseconds, lessThanOrEqualTo(exato));
        // O DEGRAU E O MAIOR MULTIPLO DE 1/60 QUE NAO PASSA DO INSTANTE:
        // o atraso nunca chega a um degrau inteiro.
        expect(exato - q.inMicroseconds, lessThan(16667));
        if (anterior != null) {
          expect(q, greaterThanOrEqualTo(anterior));
        }
        anterior = q;
      }
    });
  });

  group('na camada', () {
    test('12 fps sobre 60: cinco instantes seguidos, UM degrau', () {
      final l = _camada(taxa: 12);
      // 83,333 ms e o degrau a 12 fps. Os primeiros cinco quadros de 60
      // caem todos dentro do primeiro degrau.
      final primeiro = l.localTime(Duration.zero);
      for (var quadro = 1; quadro < 5; quadro++) {
        final t = Duration(microseconds: quadro * 1000000 ~/ 60);
        expect(l.localTime(t), primeiro, reason: 'quadro $quadro a 60 fps');
      }
      // E o setimo ja mudou de degrau: 100 ms passa dos 83,333 ms.
      final setimo = l.localTime(Duration(microseconds: 6 * 1000000 ~/ 60));
      expect(setimo, greaterThan(primeiro));
      expect(setimo, const Duration(microseconds: 83333));
    });

    test('a 24 fps o degrau dura ~41,7 ms', () {
      final l = _camada(taxa: 24);
      expect(l.localTime(_ms(0)), Duration.zero);
      expect(l.localTime(_ms(40)), Duration.zero);
      expect(l.localTime(_ms(42)), const Duration(microseconds: 41667));
    });

    test('SEM o efeito o tempo passa intacto', () {
      final l = _camada();
      for (final ms in [0, 7, 33, 100, 999]) {
        expect(l.localTime(_ms(ms)), _ms(ms));
      }
    });

    test('efeito DESLIGADO tambem deixa o tempo intacto', () {
      var e = EffectInstance(type: EffectType.posterizeTime)
          .withParamEdited('frame_rate', Duration.zero, 12);
      e = e.copyWith(enabled: false);
      final l = ShapeLayer(
        name: 'c',
        startTime: Duration.zero,
        duration: const Duration(seconds: 4),
        effects: [e],
      );
      expect(l.localTime(_ms(50)), _ms(50));
    });

    test('o startTime da camada continua valendo', () {
      var e = EffectInstance(type: EffectType.posterizeTime)
          .withParamEdited('frame_rate', Duration.zero, 12);
      final l = ShapeLayer(
        name: 'c',
        startTime: _ms(1000),
        duration: const Duration(seconds: 4),
        effects: [e],
      );
      // 1090 - 1000 = 90 ms de tempo local: ja passou do primeiro degrau
      // (83,33 ms), e nao chegou no segundo (166,67).
      expect(l.localTime(_ms(1090)), const Duration(microseconds: 83333));
      expect(l.localTime(_ms(1080)), Duration.zero);
    });

    test('exportacao pede o mesmo degrau da previa, sem quantizar de novo', () {
      final l = _camada(taxa: 12);
      final t = _ms(90);
      final degrauDaPrevia = l.localTime(t);
      expect(degrauDaPrevia, const Duration(microseconds: 83333));
      expect(instantesDeOutroTempo([l], t, 60), {degrauDaPrevia});
    });
  });

  group('a ficha', () {
    final spec = effectSpecs[EffectType.posterizeTime]!;

    test('existe, e a taxa e em quadros por segundo', () {
      expect(spec.name, 'Posterize Time');
      expect(spec.id, 'posterize_time');
      final p = spec.params['frame_rate']!;
      expect(p.unit, 'fps');
      expect(p.initial, 12);
      expect(p.min, greaterThan(0));
      expect(p.max, lessThanOrEqualTo(120));
    });

    test('os tres presets apontam para chave que existe', () {
      expect(spec.presets.length, 3);
      for (final p in spec.presets) {
        expect(p.valores, isNotEmpty);
        for (final k in p.valores.keys) {
          expect(spec.params.containsKey(k), isTrue);
        }
      }
      expect(spec.montar, ['frame_rate']);
    });

    test('a taxa com keyframe muda o degrau no tempo', () {
      var e = EffectInstance(type: EffectType.posterizeTime);
      e = e.withParamEdited('frame_rate', Duration.zero, 4);
      e = e.withKeyframeToggled(Duration.zero);
      e = e.withParamEdited(
        'frame_rate',
        const Duration(seconds: 2),
        24,
        forcar: true,
      );
      final l = ShapeLayer(
        name: 'c',
        startTime: Duration.zero,
        duration: const Duration(seconds: 4),
        effects: [e],
      );
      // A 4 fps o degrau e de 250 ms: 100 ms cai no zero.
      expect(l.localTime(_ms(100)), Duration.zero);
      // A 24 fps (fim da rampa) o degrau e de 41,7 ms: 100 ms ja subiu
      // dois degraus.
      expect(l.localTime(_ms(2000 + 100)), greaterThan(_ms(2000)));
    });

    test('taxa fora da faixa nao estoura o tempo', () {
      for (final taxa in [0.0, -12.0, 5000.0]) {
        final l = _camada(taxa: taxa);
        for (final ms in [0, 16, 100, 1000]) {
          final q = l.localTime(_ms(ms));
          expect(q.inMicroseconds.isFinite, isTrue);
          expect(
            q.inMicroseconds.abs(),
            lessThanOrEqualTo(_ms(ms).inMicroseconds.abs() + 1000000),
          );
        }
      }
    });
  });
}
