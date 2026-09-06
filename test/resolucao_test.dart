import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/color_space.dart';

void main() {
  group('mesmo tamanho aparente em qualquer resolucao', () {
    test('em 1080p o valor pensado em 1080p nao muda', () {
      // A garantia que permite mexer nisto sem quebrar projeto salvo.
      expect(pxAt1080(60, 1920, 1080), closeTo(60, 1e-9));
      expect(pxAt1080(20, 1080, 1920), closeTo(20, 1e-9));
      expect(pxAt1080(0, 1920, 1080), 0);
    });

    test('em 4K dobra, que e o mesmo tamanho aparente', () {
      expect(pxAt1080(60, 3840, 2160), closeTo(120, 1e-9));
    });

    test('em 720p encolhe na mesma proporcao', () {
      expect(pxAt1080(60, 1280, 720), closeTo(40, 1e-9));
    });

    test('vale o MENOR lado, nao a largura', () {
      // Retrato e paisagem com o mesmo menor lado dao o mesmo valor: e
      // o menor lado que define o quanto cabe na tela.
      expect(pxAt1080(60, 1080, 1920), pxAt1080(60, 1920, 1080));
    });

    test('a ida e a volta se cancelam', () {
      const w = 3840, h = 2160;
      final px = radiusToPixels(0.1, w, h);
      expect(pixelsToRadius(px, w, h), closeTo(0.1, 1e-9));
    });
  });
}
