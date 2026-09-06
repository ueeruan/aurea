import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/application/preview_stats.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_test/flutter_test.dart';

/// TickerProvider de teste: o ticker nunca roda sozinho, o teste
/// controla o tempo.
class _FakeVsync implements TickerProvider {
  Ticker? ticker;

  @override
  Ticker createTicker(TickerCallback onTick) => ticker = Ticker(onTick);
}

void main() {
  // O Ticker do PlaybackController precisa do binding do scheduler.
  TestWidgetsFlutterBinding.ensureInitialized();

  group('registrador de frames (PR-J0)', () {
    test('reproducao lisa: mediana correta e zero travadas', () {
      final r = analyzeIntervals(List<double>.filled(120, 33.3));
      expect(r.medianMs, closeTo(33.3, 0.01));
      expect(r.stutters, 0);
      expect(r.gapS, 0);
    });

    test('travada periodica: acha a cadencia e o desvio baixo', () {
      // 33 ms de rotina e uma travada de 120 ms a cada 30 frames (~1 s).
      final ints = <double>[];
      for (var i = 0; i < 300; i++) {
        ints.add(i % 30 == 29 ? 120.0 : 33.0);
      }
      final r = analyzeIntervals(ints);
      expect(r.medianMs, 33.0);
      expect(r.stutters, 10);
      expect(r.peakMs, 120);
      // 29x33 ms + 120 ms = ~1,08 s entre travadas, MUITO regular.
      expect(r.gapS, closeTo(1.1, 0.05));
      expect(r.gapSdS, lessThan(0.05));
    });

    test('travada irregular tem desvio ALTO (assinatura de GC)', () {
      final ints = <double>[];
      for (final espera in [40, 12, 95, 30, 61]) {
        ints
          ..addAll(List<double>.filled(espera, 33.0))
          ..add(140.0);
      }
      final r = analyzeIntervals(ints);
      expect(r.stutters, 5);
      expect(r.gapSdS, greaterThan(0.5));
    });

    test('serie vazia nao explode', () {
      final r = analyzeIntervals(const []);
      expect(r.medianMs, 0);
      expect(r.stutters, 0);
    });
  });

  group('fim da deriva por construcao (PR-J1)', () {
    late PlaybackController pc;

    setUp(() {
      pc = PlaybackController(
        vsync: _FakeVsync(),
        durationOf: () => const Duration(seconds: 30),
      );
    });

    tearDown(() => pc.dispose());

    test('parado: ancoragem nao mexe no relogio', () {
      pc.seek(const Duration(seconds: 5));
      pc.anchorToMedia(const Duration(seconds: 9));
      expect(pc.time.value, const Duration(seconds: 5));
    });

    test('deriva pequena e absorvida em fracao, nunca em bloco', () {
      pc.play();
      pc.seek(const Duration(seconds: 5));
      final antes = pc.time.value;
      // Midia 100 ms atras do relogio: a correcao NAO pode saltar 100ms.
      pc.anchorToMedia(antes - const Duration(milliseconds: 100));
      // O tempo exposto so muda no proximo tick; o que importa e que a
      // correcao aplicada foi fracionada (<= 20 ms), nao o erro inteiro.
      expect(pc.debugBaseShiftUs.abs(), lessThanOrEqualTo(20000));
      expect(pc.debugBaseShiftUs, lessThan(0));
    });

    test('dessincronia real (>1 s) realinha de uma vez', () {
      pc.play();
      pc.seek(const Duration(seconds: 5));
      pc.anchorToMedia(const Duration(seconds: 12));
      expect(pc.debugBaseShiftUs, 7000000);
    });

    test('deriva zero nao mexe no relogio', () {
      pc.play();
      pc.seek(const Duration(seconds: 5));
      pc.anchorToMedia(pc.time.value);
      expect(pc.debugBaseShiftUs, 0);
    });
  });
}
