import 'dart:math' as math;
import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/text_path.dart';

void main() {
  group('Construir o caminho', () {
    test('circulo tem o perimetro do raio pedido', () {
      final p = buildTextPath(
          const TextPathSpec(kind: TextPathKind.circle, radius: 100));
      expect(pathLength(p), closeTo(2 * math.pi * 100, 2));
    });

    test('arco de 180 graus tem metade do circulo', () {
      final p = buildTextPath(const TextPathSpec(
          kind: TextPathKind.arc, radius: 100, sweepDeg: 180));
      expect(pathLength(p), closeTo(math.pi * 100, 2));
    });

    // O selo comeca no topo: e onde a pessoa espera a primeira letra.
    test('o circulo comeca no topo', () {
      final p = buildTextPath(
          const TextPathSpec(kind: TextPathKind.circle, radius: 100));
      final inicio = placeOnPath(p, 0);
      expect(inicio, isNotNull);
      expect(inicio!.position.dx, closeTo(0, 1));
      expect(inicio.position.dy, closeTo(-100, 1));
    });

    test('reto e camada nao geram caminho proprio', () {
      expect(pathLength(buildTextPath(const TextPathSpec())), 0);
      expect(
          pathLength(
              buildTextPath(const TextPathSpec(kind: TextPathKind.layer))),
          0);
    });

    test('so o modo reto conta como desligado', () {
      expect(const TextPathSpec().active, isFalse);
      for (final k in TextPathKind.values) {
        if (k == TextPathKind.none) continue;
        expect(TextPathSpec(kind: k).active, isTrue);
      }
    });
  });

  group('Colocar no caminho', () {
    final circulo = buildTextPath(
        const TextPathSpec(kind: TextPathKind.circle, radius: 100));

    test('anda ao longo da curva conforme a distancia', () {
      final a = placeOnPath(circulo, 0)!;
      final b = placeOnPath(circulo, 2 * math.pi * 100 / 4)!;
      // Um quarto de volta a partir do topo cai na direita.
      expect(b.position.dx, closeTo(100, 2));
      expect(b.position.dy, closeTo(0, 2));
      expect((a.position - b.position).distance, greaterThan(100));
    });

    // Letra que nao coube simplesmente nao aparece, em vez de empilhar
    // na ponta do caminho.
    test('distancia fora do caminho devolve null', () {
      expect(placeOnPath(circulo, -10), isNull);
      expect(placeOnPath(circulo, pathLength(circulo) + 10), isNull);
    });

    test('o angulo acompanha a tangente', () {
      final topo = placeOnPath(circulo, 0)!;
      // No topo de um circulo desenhado no sentido horario, a tangente
      // aponta para a direita.
      expect(math.cos(topo.angleRad), closeTo(1, 0.05));
    });

    test('sem perpendicular, a letra fica em pe', () {
      final p = placeOnPath(circulo, 150,
          spec: const TextPathSpec(
              kind: TextPathKind.circle, perpendicular: false))!;
      expect(p.angleRad, 0);
    });

    test('acima e abaixo deslocam para lados opostos', () {
      const alto = TextPathSpec(
          kind: TextPathKind.circle, align: TextPathAlign.above);
      const baixo = TextPathSpec(
          kind: TextPathKind.circle, align: TextPathAlign.below);
      final a = placeOnPath(circulo, 0, spec: alto, glyphHeight: 40)!;
      final b = placeOnPath(circulo, 0, spec: baixo, glyphHeight: 40)!;
      final centro = placeOnPath(circulo, 0)!;
      expect((a.position - centro.position).distance, closeTo(20, 1));
      expect((b.position - centro.position).distance, closeTo(20, 1));
      expect(a.position, isNot(b.position));
    });

    test('invertido percorre a curva de tras para frente', () {
      const rev = TextPathSpec(kind: TextPathKind.circle, reverse: true);
      final a = placeOnPath(circulo, 0, spec: rev)!;
      final fim = placeOnPath(circulo, pathLength(circulo))!;
      expect((a.position - fim.position).distance, lessThan(2));
    });

    test('caminho vazio nao coloca nada', () {
      expect(placeOnPath(Path(), 0), isNull);
    });
  });

  group('Copiar com mudanca', () {
    test('limpar a forma tira o vinculo', () {
      const s = TextPathSpec(
          kind: TextPathKind.layer, shapeLayerId: 'abc');
      expect(s.copyWith(clearShape: true).shapeLayerId, isNull);
      expect(s.copyWith(offset: 10).shapeLayerId, 'abc');
    });

    test('todo modo tem rotulo', () {
      for (final k in TextPathKind.values) {
        expect(textPathKindLabel(k).trim(), isNotEmpty);
      }
    });
  });
}
