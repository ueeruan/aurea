import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/color_space.dart';

void main() {
  group('Curva do sRGB', () {
    test('as pontas nao se movem', () {
      expect(srgbToLinear(0), 0);
      expect(srgbToLinear(1), closeTo(1, 1e-9));
      expect(linearToSrgb(0), 0);
      expect(linearToSrgb(1), closeTo(1, 1e-9));
    });

    // Ida e volta tem de devolver o mesmo numero: se nao devolver, todo
    // efeito que passa pelo espaco linear muda a imagem so de passar.
    test('ida e volta devolve o mesmo', () {
      for (final v in [0.0, 0.02, 0.1, 0.25, 0.5, 0.75, 0.99, 1.0]) {
        expect(linearToSrgb(srgbToLinear(v)), closeTo(v, 1e-6),
            reason: 'valor $v');
      }
    });

    // O ponto que explica o glow acinzentado: meio-tom em sRGB e MUITO
    // menos da metade da luz.
    test('meio-tom em sRGB e ~21% da luz', () {
      expect(srgbToLinear(0.5), closeTo(0.2140, 0.001));
    });

    test('a curva e monotona', () {
      var anterior = -1.0;
      for (var i = 0; i <= 100; i++) {
        final v = srgbToLinear(i / 100);
        expect(v, greaterThan(anterior));
        anterior = v;
      }
    });

    test('valor fora da faixa e aparado', () {
      expect(srgbToLinear(-1), 0);
      expect(srgbToLinear(2), closeTo(1, 1e-9));
      expect(linearToSrgb(-1), 0);
      expect(linearToSrgb(2), closeTo(1, 1e-9));
    });

    test('o trecho reto perto do preto e reto mesmo', () {
      expect(srgbToLinear(0.02), closeTo(0.02 / 12.92, 1e-9));
    });
  });

  group('Luminancia', () {
    test('branco e um, preto e zero', () {
      expect(linearLuminance(1, 1, 1), closeTo(1, 1e-9));
      expect(linearLuminance(0, 0, 0), 0);
    });

    // Calcular luminancia em sRGB e o erro que faz o verde parecer
    // menos claro do que e.
    test('verde pesa mais que vermelho e azul', () {
      final v = linearLuminance(0, 1, 0);
      expect(v, greaterThan(linearLuminance(1, 0, 0)));
      expect(v, greaterThan(linearLuminance(0, 0, 1)));
    });
  });

  group('Unidade do raio', () {
    // O mesmo numero tem de dar o mesmo tamanho APARENTE ao trocar a
    // resolucao do projeto. E o que faltava para o efeito bater.
    test('a fracao segue o menor lado', () {
      expect(radiusToPixels(0.1, 1920, 1080), closeTo(108, 0.001));
      expect(radiusToPixels(0.1, 3840, 2160), closeTo(216, 0.001));
    });

    test('vertical usa a largura, que e o menor lado', () {
      expect(radiusToPixels(0.1, 1080, 1920), closeTo(108, 0.001));
    });

    test('ida e volta bate', () {
      expect(pixelsToRadius(radiusToPixels(0.25, 1920, 1080), 1920, 1080),
          closeTo(0.25, 1e-9));
    });

    test('zero e zero, e negativo nao existe', () {
      expect(radiusToPixels(0, 1920, 1080), 0);
      expect(radiusToPixels(-5, 1920, 1080), 0);
      expect(pixelsToRadius(10, 0, 0), 0);
    });
  });
}
