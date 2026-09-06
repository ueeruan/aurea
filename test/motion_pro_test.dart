import 'dart:ui';

import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layout_ops.dart';
import 'package:flutter_test/flutter_test.dart';

LayoutBox _box(String id, double cx, double cy, double w, double h) =>
    (id: id, center: Offset(cx, cy), size: Size(w, h));

void main() {
  group('alinhar (PR-X1)', () {
    test('tres camadas de tamanhos diferentes a esquerda: bordas coincidem',
        () {
      final boxes = [
        _box('a', 300, 100, 200, 50),
        _box('b', 500, 300, 80, 50),
        _box('c', 700, 500, 400, 50),
      ];
      final moved = alignLayers(boxes, AlignEdge.left,
          to: AlignTo.selection);
      // Borda esquerda da selecao = 300-100 = 200.
      final left = <String, double>{
        for (final b in boxes)
          b.id: (moved[b.id] ?? b.center).dx - b.size.width / 2,
      };
      expect(left['a'], 200.0);
      expect(left['b'], 200.0);
      expect(left['c'], 200.0);
    });

    test('alinhar a composicao usa a caixa do quadro', () {
      final moved = alignLayers([_box('a', 10, 10, 100, 40)],
          AlignEdge.right,
          to: AlignTo.composition, compSize: const Size(1080, 1920));
      expect(moved['a']!.dx, 1030.0); // 1080 - 50
      expect(moved['a']!.dy, 10.0); // eixo oposto intacto
    });

    test('centro vertical nao mexe no eixo horizontal', () {
      final moved = alignLayers([_box('a', 77, 10, 100, 40)],
          AlignEdge.centerV,
          to: AlignTo.composition, compSize: const Size(1000, 1000));
      expect(moved['a'], const Offset(77, 500));
    });

    test('camada ja alinhada nao entra no resultado', () {
      final moved = alignLayers([_box('a', 500, 10, 100, 40)],
          AlignEdge.centerH,
          to: AlignTo.composition, compSize: const Size(1000, 1000));
      expect(moved, isEmpty);
    });
  });

  group('distribuir (PR-X1) — as duas modalidades sao distintas', () {
    // Extremos fixos; o do meio e largo, para que centro != vao.
    final boxes = [
      _box('a', 100, 0, 100, 10),
      _box('m', 300, 0, 300, 10),
      _box('z', 900, 0, 100, 10),
    ];

    test('por CENTRO deixa os centros equidistantes', () {
      final moved =
          distributeLayers(boxes, DistributeAxis.horizontal,
              DistributeMode.byCenter);
      expect(moved['m']!.dx, 500.0); // (100 + 900) / 2
    });

    test('por VAO IGUAL deixa os espacos identicos', () {
      final moved = distributeLayers(
          boxes, DistributeAxis.horizontal, DistributeMode.byGap);
      // Livre = (850 - 150) - 300 = 400, em 2 vaos = 200 cada.
      // Centro do meio = 150 + 200 + 150 = 500... conferindo os vaos:
      final cx = moved['m']!.dx;
      final gapEsq = (cx - 150) - 150; // borda esq do meio - borda dir de 'a'
      final gapDir = 850 - (cx + 150);
      expect(gapEsq, closeTo(gapDir, 1e-9));
      expect(gapEsq, closeTo(200, 1e-9));
    });

    test('as duas dao resultados DIFERENTES com tamanhos distintos', () {
      final assimetrico = [
        _box('a', 100, 0, 100, 10),
        _box('m', 300, 0, 300, 10),
        _box('z', 700, 0, 20, 10),
      ];
      final porCentro = distributeLayers(assimetrico,
          DistributeAxis.horizontal, DistributeMode.byCenter);
      final porVao = distributeLayers(assimetrico,
          DistributeAxis.horizontal, DistributeMode.byGap);
      expect(porCentro['m']!.dx, isNot(closeTo(porVao['m']!.dx, 0.5)));
    });

    test('extremos nunca se movem; menos de 3 camadas nao faz nada', () {
      final moved = distributeLayers(
          boxes, DistributeAxis.horizontal, DistributeMode.byGap);
      expect(moved.containsKey('a'), isFalse);
      expect(moved.containsKey('z'), isFalse);
      expect(
          distributeLayers(boxes.take(2).toList(),
              DistributeAxis.horizontal, DistributeMode.byCenter),
          isEmpty);
    });

    test('espacamento exato encosta com o gap pedido', () {
      final moved = spaceLayers([
        _box('a', 100, 0, 100, 10),
        _box('b', 400, 0, 60, 10),
        _box('c', 900, 0, 40, 10),
      ], DistributeAxis.horizontal, 20);
      // 'a' termina em 150; 'b' comeca em 170 -> centro 200.
      expect(moved['b']!.dx, 200.0);
      // 'b' termina em 230; 'c' comeca em 250 -> centro 270.
      expect(moved['c']!.dx, 270.0);
    });
  });

  group('loop de keyframes (PR-X6)', () {
    // 0 -> 100 em 1 s.
    final track = AnimatedDouble(0)
        .withKeyframe(Duration.zero, 0)
        .withKeyframe(const Duration(seconds: 1), 100);

    test('sem loop: segura o ultimo valor', () {
      expect(track.valueAt(const Duration(seconds: 3)), 100);
    });

    test('CICLO: o segundo ciclo repete o primeiro, instante a instante',
        () {
      final loop = track.withLoop(const LoopSpec(mode: LoopMode.cycle));
      // A fronteira exata (t = fim) fica no fim do ciclo anterior, como
      // no AE — por isso a varredura comeca depois dela.
      for (var ms = 50; ms < 1000; ms += 50) {
        final primeiro = loop.valueAt(Duration(milliseconds: ms));
        final segundo = loop.valueAt(Duration(milliseconds: 1000 + ms));
        final terceiro = loop.valueAt(Duration(milliseconds: 2000 + ms));
        expect(segundo, closeTo(primeiro, 1e-9), reason: '$ms ms');
        expect(terceiro, closeTo(primeiro, 1e-9), reason: '$ms ms');
      }
    });

    test('DESLOCADO: no fim do ciclo N o valor e inicial + N*delta', () {
      final loop = track.withLoop(const LoopSpec(mode: LoopMode.offset));
      // delta = 100 por volta.
      expect(loop.valueAt(const Duration(seconds: 2)), closeTo(200, 1e-9));
      expect(loop.valueAt(const Duration(seconds: 3)), closeTo(300, 1e-9));
      expect(loop.valueAt(const Duration(milliseconds: 2500)),
          closeTo(250, 1e-9));
    });

    test('VAI-E-VOLTA alterna a direcao a cada volta', () {
      final loop =
          track.withLoop(const LoopSpec(mode: LoopMode.pingPong));
      // Segunda volta corre de tras para frente.
      expect(loop.valueAt(const Duration(milliseconds: 1250)),
          closeTo(75, 1e-9));
      expect(loop.valueAt(const Duration(milliseconds: 1500)),
          closeTo(50, 1e-9));
      expect(loop.valueAt(const Duration(seconds: 2)), closeTo(0, 1e-9));
      // Terceira volta corre para frente de novo.
      expect(loop.valueAt(const Duration(milliseconds: 2250)),
          closeTo(25, 1e-9));
    });

    test('CONTINUAR mantem a velocidade do ultimo trecho', () {
      final loop =
          track.withLoop(const LoopSpec(mode: LoopMode.continueValue));
      // 100 unidades por segundo, indefinidamente.
      expect(loop.valueAt(const Duration(seconds: 2)), closeTo(200, 1e-9));
      expect(loop.valueAt(const Duration(seconds: 5)), closeTo(500, 1e-9));
    });

    test('loop ANTES do primeiro keyframe', () {
      final loop = track.withLoop(
          const LoopSpec(mode: LoopMode.cycle, when: LoopWhen.both));
      expect(loop.valueAt(const Duration(milliseconds: -250)),
          closeTo(75, 1e-9));
    });

    test('conta N: so os ultimos keyframes entram no ciclo', () {
      final tri = AnimatedDouble(0)
          .withKeyframe(Duration.zero, 0)
          .withKeyframe(const Duration(seconds: 1), 100)
          .withKeyframe(const Duration(seconds: 2), 20)
          .withLoop(const LoopSpec(mode: LoopMode.cycle, count: 2));
      // Ciclo usa so o trecho 1s..2s (100 -> 20).
      expect(tri.valueAt(const Duration(milliseconds: 2500)),
          closeTo(60, 1e-9));
    });

    test('loop nao altera nada DENTRO do intervalo dos keyframes', () {
      final loop = track.withLoop(const LoopSpec(mode: LoopMode.offset));
      for (var ms = 0; ms <= 1000; ms += 100) {
        expect(loop.valueAt(Duration(milliseconds: ms)),
            closeTo(track.valueAt(Duration(milliseconds: ms)), 1e-9));
      }
    });
  });

  group('inverter no tempo (PR-X7)', () {
    test('espelha o percurso mantendo o intervalo', () {
      final t = AnimatedDouble(0)
          .withKeyframe(Duration.zero, 0)
          .withKeyframe(const Duration(milliseconds: 200), 10)
          .withKeyframe(const Duration(seconds: 1), 100);
      final r = t.reversedInTime();
      expect(r.valueAt(Duration.zero), 100);
      expect(r.valueAt(const Duration(seconds: 1)), 0);
      expect(r.valueAt(const Duration(milliseconds: 800)), 10);
    });
  });
}
