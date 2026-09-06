import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/layer_meta.dart';

void main() {
  group('Janela de exposicao', () {
    // 180 graus e o padrao cinematografico: o obturador fica aberto
    // metade do quadro.
    test('180 graus abre meio quadro', () {
      const mb = MotionBlurSpec(shutterAngle: 180, shutterPhase: 0);
      final (a, b) = mb.exposureWindow();
      expect(b - a, closeTo(0.5, 1e-9));
    });

    // A REGRA que separa fase implementada de fase esquecida: -90
    // CENTRALIZA o borrao no quadro; 0 arrasta para frente.
    test('fase -90 centra o borrao', () {
      const mb = MotionBlurSpec(shutterAngle: 180, shutterPhase: -90);
      final (a, b) = mb.exposureWindow();
      expect(a, closeTo(-0.25, 1e-9));
      expect(b, closeTo(0.25, 1e-9));
      expect(a + b, closeTo(0, 1e-9));
    });

    test('fase 0 arrasta para frente', () {
      const mb = MotionBlurSpec(shutterAngle: 180, shutterPhase: 0);
      final (a, b) = mb.exposureWindow();
      expect(a, 0);
      expect(b, closeTo(0.5, 1e-9));
    });

    // Se as duas janelas dessem iguais, a fase nao estaria implementada.
    test('as duas fases dao janelas diferentes', () {
      const centrada = MotionBlurSpec(shutterPhase: -90);
      const arrastada = MotionBlurSpec(shutterPhase: 0);
      expect(centrada.exposureWindow(), isNot(arrastada.exposureWindow()));
    });

    test('angulo zero nao abre janela', () {
      const mb = MotionBlurSpec(shutterAngle: 0, shutterPhase: 0);
      final (a, b) = mb.exposureWindow();
      expect(b - a, 0);
    });

    test('360 graus abre o quadro inteiro', () {
      const mb = MotionBlurSpec(shutterAngle: 360, shutterPhase: -180);
      final (a, b) = mb.exposureWindow();
      expect(b - a, closeTo(1, 1e-9));
      expect(a, closeTo(-0.5, 1e-9));
    });

    test('os padroes sao os do cinema', () {
      const mb = MotionBlurSpec();
      expect(mb.shutterAngle, 180);
      expect(mb.shutterPhase, -90);
      expect(mb.samples, 16);
      expect(mb.adaptiveLimit, 32);
      expect(mb.enabled, isFalse);
    });
  });
}
